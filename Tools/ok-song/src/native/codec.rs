//! HeartCodec decoder in Rust + CUDA (f32): RVQ codes -> conditioning features -> flow-matching transformer (10 Euler
//! steps, CFG) -> 25 Hz latents -> scalar waveform decoder -> 48 kHz stereo. Mirrors
//! `heartlib/heartcodec/models/{decoder,flow_matching,transformer,sq_codec}.py`.

use std::path::Path;

use anyhow::{bail, Result};

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

pub const SAMPLE_RATE: usize = 48_000;
const LATENT: usize = 256;
const MU: usize = 512;
const RVQ_DIM: usize = 32;
const ENTRIES: usize = 8192;
const QUANTIZERS: usize = 8;
const SEGMENT_SECONDS: f64 = 29.76;

struct Lin {
    w: Buf,
    b: Option<Buf>,
    inp: usize,
    out: usize,
}

struct Proj {
    conv: Buf, // [3][out][in]
    conv_b: Buf,
    lin: Lin,
    cin: usize,
    cout: usize,
}

/// The transformer blocks' matrices as IEEE halves (half the VRAM of f32); `Codec::unpack` widens one for its GEMM.
struct Block {
    attn_norm: Buf,
    wqkv: Buf,
    wo: Buf,
    mlp_norm: Buf,
    wgu: Buf,
    wdown: Buf,
    table: Buf,
}

struct Stage {
    blocks: Vec<Block>,
    dim: usize,
    head_dim: usize,
    inter: usize,
    theta: Buf,
    l1: Lin,
    l2: Lin,
    ada: Lin,
    out_table: Buf,
}

struct SConv {
    w: Buf, // [k][co][ci]
    b: Buf,
    co: usize,
    ci: usize,
    k: usize,
    dil: usize,
    left: usize,
    right: usize,
}

struct STConv {
    w: Buf, // [(co * k + j)][ci]
    b: Buf,
    co: usize,
    ci: usize,
    k: usize,
    stride: usize,
}

struct ResUnit {
    c1: SConv,
    a1: Buf,
    c2: SConv,
    a2: Buf,
}

pub struct Codec {
    rvq: Buf,
    rvq_out: Lin,
    cond: Lin,
    proj_in: Proj,
    stage1: Stage,
    conn: Proj,
    stage2: Stage,
    proj_out: Proj,
    conv0: SConv,
    ups: Vec<(STConv, Vec<ResUnit>)>,
    post: SConv,
    post_a: Buf,
    last: SConv,
    pub steps: usize,
    pub guidance: f32,
}

fn up(gpu: &Gpu, v: &[f32]) -> Result<Buf> {
    gpu.upload(v)
}

fn up16(gpu: &Gpu, v: &[f32]) -> Result<Buf> {
    gpu.upload(&v.iter().map(|f| super::weights::f16_bits(*f)).collect::<Vec<u16>>())
}

fn lin(gpu: &Gpu, st: &Shards, p: &str, bias: bool) -> Result<Lin> {
    let shape = st.shape(&format!("{p}.weight"))?;
    Ok(Lin { w: up(gpu, &st.f32(&format!("{p}.weight"))?)?, b: if bias { Some(up(gpu, &st.f32(&format!("{p}.bias"))?)?) } else { None },
             out: shape[0], inp: shape[1] })
}

/// Conv weight [co, ci, k] as k matrices [co, ci].
fn by_tap(w: &[f32], co: usize, ci: usize, k: usize) -> Vec<f32> {
    let mut r = vec![0f32; w.len()];
    for o in 0..co {
        for i in 0..ci {
            for j in 0..k {
                r[(j * co + o) * ci + i] = w[(o * ci + i) * k + j];
            }
        }
    }
    r
}

fn proj(gpu: &Gpu, st: &Shards, p: &str) -> Result<Proj> {
    let shape = st.shape(&format!("{p}.ffn_1.weight"))?;
    let (co, ci, k) = (shape[0], shape[1], shape[2]);
    if k != 3 {
        bail!("{p}: kernel {k}");
    }
    Ok(Proj { conv: up(gpu, &by_tap(&st.f32(&format!("{p}.ffn_1.weight"))?, co, ci, k))?, conv_b: up(gpu, &st.f32(&format!("{p}.ffn_1.bias"))?)?,
              lin: lin(gpu, st, &format!("{p}.ffn_2"), true)?, cin: ci, cout: co })
}

fn cat_rows(st: &Shards, names: &[String]) -> Result<Vec<f32>> {
    let mut v = Vec::new();
    for n in names {
        v.extend(st.f32(n)?);
    }
    Ok(v)
}

fn stage(gpu: &Gpu, st: &Shards, blocks: &str, n: usize, dim: usize, head_dim: usize, ada: &str, table: &str) -> Result<Stage> {
    let mut out = Vec::new();
    for i in 0..n {
        let p = format!("flow_matching.estimator.{blocks}.{i}");
        out.push(Block {
            attn_norm: up(gpu, &st.f32(&format!("{p}.attn_norm.weight"))?)?,
            wqkv: up16(gpu, &cat_rows(st, &[format!("{p}.attn.q_proj.weight"), format!("{p}.attn.k_proj.weight"), format!("{p}.attn.v_proj.weight")])?)?,
            wo: up16(gpu, &st.f32(&format!("{p}.attn.o_proj.weight"))?)?,
            mlp_norm: up(gpu, &st.f32(&format!("{p}.mlp_norm.weight"))?)?,
            wgu: up16(gpu, &cat_rows(st, &[format!("{p}.mlp.gate.weight"), format!("{p}.mlp.up.weight")])?)?,
            wdown: up16(gpu, &st.f32(&format!("{p}.mlp.down.weight"))?)?,
            table: up(gpu, &st.f32(&format!("{p}.scale_shift_table"))?)?,
        });
    }
    let inter = st.shape(&format!("flow_matching.estimator.{blocks}.0.mlp.gate.weight"))?[0];
    // RotaryEmbedding: 1 / base^(arange(0, d, 2) / d) in f32.
    let theta: Vec<f32> = (0..head_dim / 2).map(|i| 1.0 / 10000f32.powf((2 * i) as f32 / head_dim as f32)).collect();
    let e = format!("flow_matching.estimator.{ada}");
    Ok(Stage {
        blocks: out,
        dim,
        head_dim,
        inter,
        theta: up(gpu, &theta)?,
        l1: lin(gpu, st, &format!("{e}.emb.timestep_embedder.linear_1"), true)?,
        l2: lin(gpu, st, &format!("{e}.emb.timestep_embedder.linear_2"), true)?,
        ada: lin(gpu, st, &format!("{e}.linear"), true)?,
        out_table: up(gpu, &st.f32(&format!("flow_matching.estimator.{table}"))?)?,
    })
}

/// weight_norm (dim 0): w = g * v / ||v|| per first-axis slice.
fn weight_norm(st: &Shards, p: &str) -> Result<(Vec<f32>, Vec<usize>)> {
    let g = st.f32(&format!("{p}.parametrizations.weight.original0"))?;
    let v = st.f32(&format!("{p}.parametrizations.weight.original1"))?;
    let shape = st.shape(&format!("{p}.parametrizations.weight.original1"))?;
    let per = v.len() / shape[0];
    let mut w = vec![0f32; v.len()];
    for i in 0..shape[0] {
        let s = &v[i * per..(i + 1) * per];
        let n = (s.iter().map(|x| (*x as f64) * (*x as f64)).sum::<f64>()).sqrt() as f32;
        for (d, x) in w[i * per..(i + 1) * per].iter_mut().zip(s) {
            *d = g[i] * *x / n;
        }
    }
    Ok((w, shape))
}

fn sconv(gpu: &Gpu, st: &Shards, p: &str, dil: usize, causal: bool, normed: bool) -> Result<SConv> {
    let (w, shape) = if normed { weight_norm(st, p)? } else { (st.f32(&format!("{p}.weight"))?, st.shape(&format!("{p}.weight"))?) };
    let (co, ci, k) = (shape[0], shape[1], shape[2]);
    let pad = dil * (k - 1);
    let (left, right) = if causal { (pad, 0) } else { (pad / 2, pad / 2) };
    Ok(SConv { w: up(gpu, &by_tap(&w, co, ci, k))?, b: up(gpu, &st.f32(&format!("{p}.bias"))?)?, co, ci, k, dil, left, right })
}

fn stconv(gpu: &Gpu, st: &Shards, p: &str, stride: usize) -> Result<STConv> {
    let (w, shape) = weight_norm(st, p)?;
    let (ci, co, k) = (shape[0], shape[1], shape[2]);
    if k != 2 * stride {
        bail!("{p}: causal transposed conv needs kernel = 2 * stride");
    }
    let mut r = vec![0f32; w.len()];
    for i in 0..ci {
        for o in 0..co {
            for j in 0..k {
                r[(o * k + j) * ci + i] = w[(i * co + o) * k + j];
            }
        }
    }
    Ok(STConv { w: up(gpu, &r)?, b: up(gpu, &st.f32(&format!("{p}.bias"))?)?, co, ci, k, stride })
}

fn launch_n(gpu: &Gpu, name: &str, n: usize, args: &[Arg]) -> Result<()> {
    gpu.launch1(name, n, args)
}

impl Codec {
    pub fn load(gpu: &Gpu, dir: &Path) -> Result<Self> {
        let st = Shards::open(dir)?;
        let mut tables = Vec::with_capacity(QUANTIZERS * ENTRIES * RVQ_DIM);
        for q in 0..QUANTIZERS {
            tables.extend(st.f32(&format!("flow_matching.vq_embed.layers.{q}._codebook.embed"))?);
        }
        let mut ups = Vec::new();
        let strides = [5usize, 4, 4, 4, 3];
        for (i, s) in strides.iter().enumerate() {
            let p = format!("scalar_model.decoder.{}", i + 1);
            let mut units = Vec::new();
            for (u, d) in [1usize, 3, 5, 7, 9].iter().enumerate() {
                let q = format!("{p}.convs.{u}");
                units.push(ResUnit {
                    c1: sconv(gpu, &st, &format!("{q}.conv1"), *d, true, true)?,
                    a1: up(gpu, &st.f32(&format!("{q}.activation1.weight"))?)?,
                    c2: sconv(gpu, &st, &format!("{q}.conv2"), 1, true, true)?,
                    a2: up(gpu, &st.f32(&format!("{q}.activation2.weight"))?)?,
                });
            }
            ups.push((stconv(gpu, &st, &format!("{p}.up_conv.layer"), *s)?, units));
        }
        Ok(Self {
            rvq: up(gpu, &tables)?,
            rvq_out: lin(gpu, &st, "flow_matching.vq_embed.project_out", true)?,
            cond: lin(gpu, &st, "flow_matching.cond_feature_emb", true)?,
            proj_in: proj(gpu, &st, "flow_matching.estimator.proj_in")?,
            stage1: stage(gpu, &st, "transformer_blocks", 24, 1536, 64, "adaln_single", "scale_shift_table")?,
            conn: proj(gpu, &st, "flow_matching.estimator.connection_proj")?,
            stage2: stage(gpu, &st, "transformer_blocks_2", 6, 3072, 128, "adaln_single_2", "scale_shift_table_2")?,
            proj_out: proj(gpu, &st, "flow_matching.estimator.proj_out")?,
            conv0: sconv(gpu, &st, "scalar_model.decoder.0", 1, false, true)?,
            ups,
            post: sconv(gpu, &st, "scalar_model.decoder.6.conv", 1, true, false)?,
            post_a: up(gpu, &st.f32("scalar_model.decoder.6.activation.weight")?)?,
            last: sconv(gpu, &st, "scalar_model.decoder.7", 1, true, true)?,
            steps: 10,
            guidance: 1.25,
        })
    }

    /// A block matrix of `n` halves widened to f32 for one GEMM.
    fn unpack(&self, gpu: &Gpu, w: &Buf, n: usize) -> Result<Buf> {
        let f = gpu.alloc(n * 4)?;
        launch_n(gpu, "f16_to_f32", n, &[Arg::P(f.ptr), Arg::P(w.ptr), Arg::L(n as i64)])?;
        Ok(f)
    }

    /// y[rows, out] = x W^T (+ b).
    fn linear(&self, gpu: &Gpu, l: &Lin, x: u64, rows: usize) -> Result<Buf> {
        let y = gpu.alloc(rows * l.out * 4)?;
        let beta = if let Some(b) = &l.b {
            launch_n(gpu, "fill_rows_f32", rows * l.out, &[Arg::P(y.ptr), Arg::P(b.ptr), Arg::I(l.out as i32), Arg::I(rows as i32), Arg::I(1)])?;
            1.0
        } else {
            0.0
        };
        gpu.linear_f32(y.ptr, x, l.w.ptr, rows, l.inp, l.out, beta)?;
        Ok(y)
    }

    /// ProjectLayer: conv1d k3 (pad 1) over time on rows [b][t][cin], times 3^-0.5, then a linear layer.
    fn project(&self, gpu: &Gpu, p: &Proj, x: u64, b: usize, t: usize) -> Result<Buf> {
        let xp = gpu.alloc(b * (t + 2) * p.cin * 4)?;
        launch_n(gpu, "pad_rows_f32", b * (t + 2) * p.cin, &[Arg::P(xp.ptr), Arg::P(x), Arg::I(b as i32), Arg::I(t as i32), Arg::I(p.cin as i32), Arg::I(1)])?;
        let y = gpu.alloc(b * t * p.cout * 4)?;
        launch_n(gpu, "fill_rows_f32", b * t * p.cout, &[Arg::P(y.ptr), Arg::P(p.conv_b.ptr), Arg::I(p.cout as i32), Arg::I((b * t) as i32), Arg::I(1)])?;
        for bi in 0..b {
            for j in 0..3 {
                gpu.linear_f32(ptr_f32(y.ptr, bi * t * p.cout), ptr_f32(xp.ptr, (bi * (t + 2) + j) * p.cin), ptr_f32(p.conv.ptr, j * p.cout * p.cin),
                               t, p.cin, p.cout, 1.0)?;
            }
        }
        launch_n(gpu, "scale_f32", b * t * p.cout, &[Arg::P(y.ptr), Arg::L((b * t * p.cout) as i64), Arg::F(1.0 / 3f32.sqrt())])?;
        self.linear(gpu, &p.lin, y.ptr, b * t)
    }

    /// Timestep conditioning for a stage: (block modulation [6 d], embedded timestep [d]).
    fn timestep(&self, gpu: &Gpu, s: &Stage, t: f32) -> Result<(Buf, Buf)> {
        // PixArt flow embedding: 512 channels, freqs = exp(-ln(10000) * i / 256), args = t * freqs * 1000.
        let half = 256;
        let mut v = vec![0f32; 512];
        for i in 0..half {
            let f = (-(10000f64.ln() as f32) * i as f32 / half as f32).exp();
            let a = t * f * 1000.0;
            v[i] = a.cos();
            v[half + i] = a.sin();
        }
        let x = up(gpu, &v)?;
        let h = self.linear(gpu, &s.l1, x.ptr, 1)?;
        launch_n(gpu, "silu_f32", s.dim, &[Arg::P(h.ptr), Arg::I(s.dim as i32)])?;
        let emb = self.linear(gpu, &s.l2, h.ptr, 1)?;
        let act = gpu.alloc(s.dim * 4)?;
        gpu.copy(act.ptr, emb.ptr, s.dim * 4)?;
        launch_n(gpu, "silu_f32", s.dim, &[Arg::P(act.ptr), Arg::I(s.dim as i32)])?;
        let m = self.linear(gpu, &s.ada, act.ptr, 1)?;
        Ok((m, emb))
    }

    /// Transformer stage over rows [b * t, dim] (in place), then the output LayerNorm + modulation into a new buffer.
    fn run_stage(&self, gpu: &Gpu, s: &Stage, x: &Buf, b: usize, t: usize, tstep: f32, mut probe: Option<&mut (dyn FnMut(&str, u64) -> Result<()> + '_)>) -> Result<Buf> {
        let (d, hd) = (s.dim, s.head_dim);
        let heads = d / hd;
        let rows = b * t;
        let (m, emb) = self.timestep(gpu, s, tstep)?;
        let n = rows * d;
        let normed = gpu.alloc(n * 4)?;
        let attn = gpu.alloc(n * 4)?;
        let scores = gpu.alloc(t * t * 4)?;
        let act = gpu.alloc(rows * s.inter * 4)?;
        let scale = 1.0 / (hd as f32).sqrt();
        for (bi, blk) in s.blocks.iter().enumerate() {
            gpu.launch("rmsnorm_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(x.ptr), Arg::P(blk.attn_norm.ptr), Arg::I(d as i32), Arg::F(1e-6)])?;
            launch_n(gpu, "modulate_f32", n, &[Arg::P(normed.ptr), Arg::P(normed.ptr), Arg::P(blk.table.ptr), Arg::P(m.ptr), Arg::I(0),
                Arg::I(0), Arg::I(0), Arg::I(1), Arg::I(1), Arg::I(d as i32), Arg::I(t as i32), Arg::L(rows as i64)])?;
            let qkv = gpu.alloc(rows * 3 * d * 4)?;
            gpu.linear_f32(qkv.ptr, normed.ptr, self.unpack(gpu, &blk.wqkv, 3 * d * d)?.ptr, rows, d, 3 * d, 0.0)?;
            for off in [0, d] {
                gpu.launch("rope_il_f32", (rows as u32, heads as u32, 1), ((hd / 2) as u32, 1, 1), 0, &[
                    Arg::P(qkv.ptr), Arg::I((3 * d) as i32), Arg::I(off as i32), Arg::I(hd as i32), Arg::P(s.theta.ptr), Arg::I(t as i32)])?;
            }
            for bb in 0..b {
                let base = qkv.ptr + (bb * t * 3 * d * 4) as u64;
                for h in 0..heads {
                    let q = ptr_f32(base, h * hd);
                    let k = ptr_f32(base, d + h * hd);
                    let v = ptr_f32(base, 2 * d + h * hd);
                    // scores (row-major [query][key]) = Q K^T
                    gpu.sgemm(true, false, t, t, hd, k, 3 * d, q, 3 * d, 0.0, scores.ptr, t)?;
                    gpu.launch("softmax_rows_f32", (t as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(scores.ptr), Arg::I(t as i32), Arg::F(scale)])?;
                    // out[query, h*hd..] = P V
                    gpu.sgemm(false, false, hd, t, t, v, 3 * d, scores.ptr, t, 0.0, ptr_f32(attn.ptr, bb * t * d + h * hd), d)?;
                }
            }
            drop(qkv);
            gpu.linear_f32(normed.ptr, attn.ptr, self.unpack(gpu, &blk.wo, d * d)?.ptr, rows, d, d, 0.0)?;
            launch_n(gpu, "gated_add_f32", n, &[Arg::P(x.ptr), Arg::P(normed.ptr), Arg::P(blk.table.ptr), Arg::P(m.ptr), Arg::I(0), Arg::I(2),
                Arg::I(d as i32), Arg::I(t as i32), Arg::L(rows as i64)])?;
            gpu.launch("rmsnorm_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(x.ptr), Arg::P(blk.mlp_norm.ptr), Arg::I(d as i32), Arg::F(1e-6)])?;
            launch_n(gpu, "modulate_f32", n, &[Arg::P(normed.ptr), Arg::P(normed.ptr), Arg::P(blk.table.ptr), Arg::P(m.ptr), Arg::I(0),
                Arg::I(3), Arg::I(3), Arg::I(4), Arg::I(4), Arg::I(d as i32), Arg::I(t as i32), Arg::L(rows as i64)])?;
            let gu = gpu.alloc(rows * 2 * s.inter * 4)?;
            gpu.linear_f32(gu.ptr, normed.ptr, self.unpack(gpu, &blk.wgu, 2 * s.inter * d)?.ptr, rows, d, 2 * s.inter, 0.0)?;
            launch_n(gpu, "silu_mul_f32", rows * s.inter, &[Arg::P(act.ptr), Arg::P(gu.ptr), Arg::I(s.inter as i32), Arg::I(rows as i32)])?;
            drop(gu);
            gpu.linear_f32(normed.ptr, act.ptr, self.unpack(gpu, &blk.wdown, d * s.inter)?.ptr, rows, s.inter, d, 0.0)?;
            launch_n(gpu, "gated_add_f32", n, &[Arg::P(x.ptr), Arg::P(normed.ptr), Arg::P(blk.table.ptr), Arg::P(m.ptr), Arg::I(0), Arg::I(5),
                Arg::I(d as i32), Arg::I(t as i32), Arg::L(rows as i64)])?;
            if let Some(p) = probe.as_mut() {
                p(&format!("block{bi}"), x.ptr)?;
            }
        }
        let out = gpu.alloc(n * 4)?;
        gpu.launch("layernorm_rows_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(x.ptr), Arg::I(d as i32), Arg::F(1e-6)])?;
        // shift = table[0] + emb, scale = table[1] + emb.
        launch_n(gpu, "modulate_f32", n, &[Arg::P(out.ptr), Arg::P(out.ptr), Arg::P(s.out_table.ptr), Arg::P(emb.ptr), Arg::I(0),
            Arg::I(0), Arg::I(0), Arg::I(1), Arg::I(0), Arg::I(d as i32), Arg::I(t as i32), Arg::L(rows as i64)])?;
        Ok(out)
    }

    /// The flow-matching estimator: input rows [b][t][1024] -> velocity [b][t][256].
    fn estimator(&self, gpu: &Gpu, input: u64, b: usize, t: usize, tstep: f32, mut probe: Option<&mut (dyn FnMut(&str, u64) -> Result<()> + '_)>) -> Result<Buf> {
        let s = self.project(gpu, &self.proj_in, input, b, t)?;
        let s = self.run_stage(gpu, &self.stage1, &s, b, t, tstep, probe.as_deref_mut())?;
        // cat([hidden_states (1024), s (1536)]) per row
        let cat_w = 1024 + self.stage1.dim;
        let cat = gpu.alloc(b * t * cat_w * 4)?;
        launch_n(gpu, "copy_cols_f32", b * t * 1024, &[Arg::P(cat.ptr), Arg::I(cat_w as i32), Arg::I(0), Arg::P(input), Arg::I(1024), Arg::L((b * t) as i64)])?;
        launch_n(gpu, "copy_cols_f32", b * t * self.stage1.dim, &[Arg::P(cat.ptr), Arg::I(cat_w as i32), Arg::I(1024), Arg::P(s.ptr),
            Arg::I(self.stage1.dim as i32), Arg::L((b * t) as i64)])?;
        let x = self.project(gpu, &self.conn, cat.ptr, b, t)?;
        let x = self.run_stage(gpu, &self.stage2, &x, b, t, tstep, probe.as_deref_mut())?;
        self.project(gpu, &self.proj_out, x.ptr, b, t)
    }
}


/// Gaussian noise for the flow: from a seeded generator, or replayed from a Python dump in parity checks.
pub trait Noise {
    fn draw(&mut self, rows: usize, cols: usize) -> Result<Vec<f32>>;
}

/// xorshift64* + Box-Muller.
pub struct SeededNoise(pub u64);

impl Noise for SeededNoise {
    fn draw(&mut self, rows: usize, cols: usize) -> Result<Vec<f32>> {
        let mut next = || {
            self.0 ^= self.0 >> 12;
            self.0 ^= self.0 << 25;
            self.0 ^= self.0 >> 27;
            ((self.0.wrapping_mul(0x2545_f491_4f6c_dd1d) >> 11) as f64 + 0.5) / (1u64 << 53) as f64
        };
        let n = rows * cols;
        let mut v = Vec::with_capacity(n + 1);
        while v.len() < n {
            let (u1, u2) = (next(), next());
            let r = (-2.0 * u1.ln()).sqrt();
            let a = 2.0 * std::f64::consts::PI * u2;
            v.push((r * a.cos()) as f32);
            v.push((r * a.sin()) as f32);
        }
        v.truncate(n);
        Ok(v)
    }
}

/// torch.linspace(0, 1, n) in f32 (first half from the start, second half from the end).
fn linspace(n: usize) -> Vec<f32> {
    let step = 1.0f32 / (n - 1) as f32;
    let half = n / 2;
    (0..n).map(|i| if i < half { step * i as f32 } else { 1.0 - step * (n - 1 - i) as f32 }).collect()
}

impl Codec {
    /// RVQ codes [Q][T] -> conditioning features [2 T, 512] (project_out, cond_feature_emb, nearest x2).
    fn features(&self, gpu: &Gpu, codes: &[i32], t: usize) -> Result<(Buf, Buf)> {
        let c = gpu.upload(codes)?;
        let sum = gpu.alloc(t * RVQ_DIM * 4)?;
        gpu.launch("rvq_rows_f32", (t as u32, 1, 1), (32, 1, 1), 0, &[Arg::P(sum.ptr), Arg::P(self.rvq.ptr), Arg::P(c.ptr), Arg::I(QUANTIZERS as i32),
            Arg::I(ENTRIES as i32), Arg::I(RVQ_DIM as i32), Arg::I(t as i32)])?;
        let q = self.linear(gpu, &self.rvq_out, sum.ptr, t)?;
        let f = self.linear(gpu, &self.cond, q.ptr, t)?;
        let mu = gpu.alloc(2 * t * MU * 4)?;
        launch_n(gpu, "repeat_rows_f32", 2 * t * MU, &[Arg::P(mu.ptr), Arg::P(f.ptr), Arg::I(t as i32), Arg::I(MU as i32), Arg::I(2)])?;
        Ok((f, mu))
    }

    /// One segment: Euler-integrate the flow from `noise` with the first `ic` rows taken from `incontext`.
    fn flow(&self, gpu: &Gpu, mu: &Buf, t: usize, noise: &[f32], incontext: &Buf, ic: usize,
            mut probe: Option<&mut (dyn FnMut(&str, u64) -> Result<()> + '_)>) -> Result<Buf> {
        let n = t * LATENT;
        let noise = gpu.upload(noise)?;
        let x = gpu.alloc(n * 4)?;
        gpu.copy(x.ptr, noise.ptr, n * 4)?;
        let b = if self.guidance > 1.0 { 2 } else { 1 };
        let input = gpu.alloc(b * t * 1024 * 4)?;
        let span = linspace(self.steps + 1);
        let mut tt = span[0];
        let mut dt = span[1] - span[0];
        for step in 1..=self.steps {
            if ic > 0 {
                launch_n(gpu, "incontext_mix_f32", ic * LATENT, &[Arg::P(x.ptr), Arg::P(noise.ptr), Arg::P(incontext.ptr), Arg::L((ic * LATENT) as i64), Arg::F(tt)])?;
            }
            launch_n(gpu, "est_input_f32", b * t * 1024, &[Arg::P(input.ptr), Arg::P(x.ptr), Arg::P(incontext.ptr), Arg::P(mu.ptr), Arg::I(t as i32), Arg::I(b as i32)])?;
            let d = self.estimator(gpu, input.ptr, b, t, tt, if step == 1 { probe.as_deref_mut() } else { None })?;
            if step == 1 {
                if let Some(p) = probe.as_mut() {
                    p("est_in", input.ptr)?;
                    p("est_out", d.ptr)?;
                }
            }
            launch_n(gpu, "cfg_euler_f32", n, &[Arg::P(x.ptr), Arg::P(d.ptr), Arg::L(n as i64), Arg::F(dt), Arg::F(self.guidance), Arg::I(b as i32)])?;
            tt += dt;
            if step < self.steps {
                dt = span[step + 1] - tt;
            }
        }
        if ic > 0 {
            gpu.copy(x.ptr, incontext.ptr, ic * LATENT * 4)?;
        }
        Ok(x)
    }

    fn conv(&self, gpu: &Gpu, c: &SConv, x: &Buf, t: usize) -> Result<Buf> {
        let y = gpu.alloc(c.co * t * 4)?;
        launch_n(gpu, "fill_rows_f32", c.co * t, &[Arg::P(y.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(t as i32), Arg::I(0)])?;
        if c.k == 1 {
            gpu.chan_matmul_f32(y.ptr, c.w.ptr, x.ptr, c.co, c.ci, t, t, 1.0)?;
            return Ok(y);
        }
        let tp = t + c.left + c.right;
        let xp = gpu.alloc(c.ci * tp * 4)?;
        launch_n(gpu, "pad_lr_f32", c.ci * tp, &[Arg::P(xp.ptr), Arg::P(x.ptr), Arg::I(c.ci as i32), Arg::I(t as i32), Arg::I(c.left as i32), Arg::I(c.right as i32)])?;
        for j in 0..c.k {
            gpu.chan_matmul_f32(y.ptr, ptr_f32(c.w.ptr, j * c.co * c.ci), ptr_f32(xp.ptr, j * c.dil), c.co, c.ci, t, tp, 1.0)?;
        }
        Ok(y)
    }

    fn tconv(&self, gpu: &Gpu, c: &STConv, x: &Buf, t: usize) -> Result<Buf> {
        let full = gpu.alloc(c.co * c.k * t * 4)?;
        gpu.chan_matmul_f32(full.ptr, c.w.ptr, x.ptr, c.co * c.k, c.ci, t, t, 0.0)?;
        let out = gpu.alloc(c.co * t * c.stride * 4)?;
        launch_n(gpu, "overlap_add_f32", c.co * t * c.stride, &[Arg::P(out.ptr), Arg::P(full.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(t as i32),
            Arg::I(c.k as i32), Arg::I(c.stride as i32)])?;
        Ok(out)
    }

    fn prelu(&self, gpu: &Gpu, x: &Buf, n: usize, a: &Buf) -> Result<()> {
        launch_n(gpu, "prelu_f32", n, &[Arg::P(x.ptr), Arg::L(n as i64), Arg::P(a.ptr)])
    }

    /// ScalarModel.decode for one stereo half of a latent [t, 256]: 1920 samples per latent frame.
    fn scalar(&self, gpu: &Gpu, latent: &Buf, t: usize, ch: usize) -> Result<Vec<f32>> {
        let x = gpu.alloc(128 * t * 4)?;
        launch_n(gpu, "latent_channel_f32", 128 * t, &[Arg::P(x.ptr), Arg::P(latent.ptr), Arg::I(t as i32), Arg::I(ch as i32)])?;
        let mut x = self.conv(gpu, &self.conv0, &x, t)?;
        let mut len = t;
        for (upc, units) in &self.ups {
            x = self.tconv(gpu, upc, &x, len)?;
            len *= upc.stride;
            let c = upc.co;
            for u in units {
                let h = self.conv(gpu, &u.c1, &x, len)?;
                self.prelu(gpu, &h, c * len, &u.a1)?;
                let h2 = self.conv(gpu, &u.c2, &h, len)?;
                drop(h);
                self.prelu(gpu, &h2, c * len, &u.a2)?;
                launch_n(gpu, "add_f32", c * len, &[Arg::P(x.ptr), Arg::P(h2.ptr), Arg::P(x.ptr), Arg::L((c * len) as i64)])?;
            }
        }
        let c = self.post.ci;
        let rep = gpu.alloc(c * len * 2 * 4)?;
        launch_n(gpu, "repeat_time_f32", c * len * 2, &[Arg::P(rep.ptr), Arg::P(x.ptr), Arg::I(c as i32), Arg::I(len as i32), Arg::I(2)])?;
        drop(x);
        len *= 2;
        let y = self.conv(gpu, &self.post, &rep, len)?;
        drop(rep);
        self.prelu(gpu, &y, c * len, &self.post_a)?;
        let out = self.conv(gpu, &self.last, &y, len)?;
        gpu.read(out.ptr, len)
    }

    /// HeartCodecDecoder.detokenize: frames [T][8] -> stereo [2][T * 3840] at 48 kHz.
    pub fn detokenize(&self, gpu: &Gpu, frames: &[[i32; 8]], noise: &mut dyn Noise, mut probe: Option<&mut (dyn FnMut(&str, u64) -> Result<()> + '_)>,
                      mut progress: impl FnMut(usize, usize)) -> Result<[Vec<f32>; 2]> {
        if frames.is_empty() {
            bail!("no frames");
        }
        let min_codes = (SEGMENT_SECONDS * 12.5) as usize;
        let hop = min_codes / 93 * 80;
        let ovlp = min_codes - hop;
        let ovlp_frames = ovlp * 2;
        let target_len = (frames.len() as f64 / 12.5 * SAMPLE_RATE as f64) as usize;
        // Code repetition exactly as upstream (doubling, then crop).
        let mut codes: Vec<[i32; 8]> = frames.to_vec();
        if codes.len() < min_codes {
            while codes.len() < min_codes {
                codes.extend_from_within(..);
            }
            codes.truncate(min_codes);
        }
        if (codes.len() - ovlp) % hop > 0 {
            let want = (codes.len() - ovlp).div_ceil(hop) * hop + ovlp;
            while codes.len() < want {
                codes.extend_from_within(..);
            }
            codes.truncate(want);
        }
        let latent_len = (SEGMENT_SECONDS * 25.0) as usize;
        // The first draw is the (unused) first in-context latent.
        let _ = noise.draw(latent_len, LATENT)?;
        let starts: Vec<usize> = (0..=codes.len() - hop).step_by(hop).collect();
        let mut latents: Vec<Buf> = Vec::new();
        for (si, &s) in starts.iter().enumerate() {
            let window = &codes[s..s + min_codes];
            let mut q = vec![0i32; QUANTIZERS * min_codes];
            for (ti, f) in window.iter().enumerate() {
                for k in 0..QUANTIZERS {
                    q[k * min_codes + ti] = f[k];
                }
            }
            let (_f, mu) = self.features(gpu, &q, min_codes)?;
            let t = 2 * min_codes;
            let incontext = gpu.zeros(t * LATENT * 4)?;
            let ic = if si == 0 || ovlp_frames == 0 {
                0
            } else {
                let prev = latents.last().unwrap();
                gpu.copy(incontext.ptr, ptr_f32(prev.ptr, (t - ovlp_frames) * LATENT), ovlp_frames * LATENT * 4)?;
                let _ = noise.draw(latent_len - ovlp_frames, LATENT)?;
                ovlp_frames
            };
            let z = noise.draw(t, LATENT)?;
            let lat = self.flow(gpu, &mu, t, &z, &incontext, ic, if si == 0 { probe.as_deref_mut() } else { None })?;
            if let Some(p) = probe.as_mut() {
                p(&format!("latents_{si}"), lat.ptr)?;
            }
            latents.push(lat);
            progress(si + 1, starts.len() * 2);
        }
        let seg_samples = (SEGMENT_SECONDS * SAMPLE_RATE as f64) as usize;
        let hop_s = seg_samples / 93 * 80;
        let ovlp_s = seg_samples - hop_s;
        let mut out: [Vec<f32>; 2] = [Vec::new(), Vec::new()];
        for (si, lat) in latents.iter().enumerate() {
            let t = 2 * min_codes;
            for (ch, o) in out.iter_mut().enumerate() {
                let mut cur = self.scalar(gpu, lat, t, ch)?;
                cur.truncate(seg_samples);
                if o.is_empty() {
                    *o = cur;
                } else if ovlp_s == 0 {
                    o.extend_from_slice(&cur);
                } else {
                    // np.linspace(0, 1, ovlp) fade, in f64 like the upstream numpy window.
                    let base = o.len() - ovlp_s;
                    for i in 0..ovlp_s {
                        let w = if ovlp_s == 1 { 0.0 } else { i as f64 / (ovlp_s - 1) as f64 };
                        o[base + i] = (o[base + i] as f64 * (1.0 - w) + cur[i] as f64 * w) as f32;
                    }
                    o.extend_from_slice(&cur[ovlp_s..]);
                }
            }
            progress(starts.len() + si + 1, starts.len() * 2);
        }
        for o in out.iter_mut() {
            o.truncate(target_len);
        }
        Ok(out)
    }
}

/// Replays the `randn_*.npy` draws of a Python dump, starting at `first`.
pub struct DumpNoise {
    pub dir: std::path::PathBuf,
    pub next: usize,
}

impl Noise for DumpNoise {
    fn draw(&mut self, rows: usize, cols: usize) -> Result<Vec<f32>> {
        let v = super::mula::npy(&self.dir.join(format!("randn_{}.npy", self.next)))?;
        self.next += 1;
        if v.len() != rows * cols {
            bail!("noise draw {}: {} values, expected {}", self.next - 1, v.len(), rows * cols);
        }
        Ok(v)
    }
}

/// Parity against `codec_dump.py` output.
pub fn check(gpu: &Gpu, codec: &Codec, frames: &[[i32; 8]], dir: &Path, first_draw: usize) -> Result<serde_json::Value> {
    let mut report = serde_json::Map::new();
    let mut noise = DumpNoise { dir: dir.to_path_buf(), next: first_draw };
    let mut seen = std::collections::HashSet::new();
    let mut probe = |name: &str, ptr: u64| -> Result<()> {
        let file = match name {
            "block0" if seen.insert("block0") => "blk0_out",
            "est_in" if seen.insert("est_in") => "est_in",
            "est_out" if seen.insert("est_out") => "est_out",
            "latents_0" => "latents_0",
            "latents_1" => "latents_1",
            _ => return Ok(()),
        };
        let refv = super::mula::npy(&dir.join(format!("{file}.npy")))?;
        let mine: Vec<f32> = gpu.read(ptr, refv.len())?;
        report.insert(file.into(), super::mula::compare(&mine, &refv));
        Ok(())
    };
    let wav = codec.detokenize(gpu, frames, &mut noise, Some(&mut probe), |_, _| {})?;
    let refw = super::mula::npy(&dir.join("wav.npy"))?;
    let mine: Vec<f32> = wav[0].iter().chain(wav[1].iter()).copied().collect();
    report.insert("wav".into(), super::mula::compare(&mine, &refw));
    Ok(report.into())
}
