//! Seed-VC length regulator + DiT flow-matching estimator (U-ViT skips, AdaLN-RMSNorm, MLP head) and its CFG Euler
//! solver, in Rust + CUDA (f32). Mirrors `modules/length_regulator.py`, `modules/diffusion_transformer.py` and
//! `modules/flow_matching.py` for the f0-conditioned 44 kHz model.

use std::path::Path;

use anyhow::Result;

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

const D: usize = 768;
const HEADS: usize = 12;
const HD: usize = 64;
const LAYERS: usize = 17;
const MELS: usize = 128;
const STYLE: usize = 192;
const F0_BINS: usize = 256;

struct Lin {
    w: Buf,
    b: Option<Buf>,
    out: usize,
    inp: usize,
}

struct Layer {
    wqkv: Lin,
    wo: Lin,
    w1w3: Lin,
    w2: Lin,
    attn_norm: Buf,
    attn_mod: Lin,
    ffn_norm: Buf,
    ffn_mod: Lin,
    skip: Lin,
}

struct ConvK3 {
    w: Buf, // [3][co][ci]
    b: Buf,
    gn: (Buf, Buf),
}

pub struct Dit {
    layers: Vec<Layer>,
    norm: Buf,
    norm_mod: Lin,
    t1: Lin,
    t2: Lin,
    cond_proj: Lin,
    merge: Lin,
    mlp1: Lin,
    mlp2: Lin,
    theta: Buf,
    // length regulator
    lr_in: Lin,
    lr_f0: Vec<f32>,
    lr_convs: Vec<ConvK3>,
    lr_out: Lin,
}

fn lin(gpu: &Gpu, st: &Shards, p: &str, bias: bool) -> Result<Lin> {
    let shape = st.shape(&format!("{p}.weight"))?;
    let w = st.f32(&format!("{p}.weight"))?;
    Ok(Lin { w: gpu.upload(&w)?, b: if bias { Some(gpu.upload(&st.f32(&format!("{p}.bias"))?)?) } else { None }, out: shape[0], inp: shape[1] })
}

fn lin_cat(gpu: &Gpu, st: &Shards, a: &str, b: &str) -> Result<Lin> {
    let mut w = st.f32(a)?;
    w.extend(st.f32(b)?);
    let inp = st.shape(a)?[1];
    Ok(Lin { out: w.len() / inp, w: gpu.upload(&w)?, b: None, inp })
}

impl Dit {
    pub fn load(gpu: &Gpu, path: &Path) -> Result<Self> {
        let st = Shards::open_file(path)?;
        let e = "net.cfm.module.estimator";
        let mut layers = Vec::new();
        for i in 0..LAYERS {
            let p = format!("{e}.transformer.layers.{i}");
            layers.push(Layer {
                wqkv: lin(gpu, &st, &format!("{p}.attention.wqkv"), false)?,
                wo: lin(gpu, &st, &format!("{p}.attention.wo"), false)?,
                w1w3: lin_cat(gpu, &st, &format!("{p}.feed_forward.w1.weight"), &format!("{p}.feed_forward.w3.weight"))?,
                w2: lin(gpu, &st, &format!("{p}.feed_forward.w2"), false)?,
                attn_norm: gpu.upload(&st.f32(&format!("{p}.attention_norm.norm.weight"))?)?,
                attn_mod: lin(gpu, &st, &format!("{p}.attention_norm.project_layer"), true)?,
                ffn_norm: gpu.upload(&st.f32(&format!("{p}.ffn_norm.norm.weight"))?)?,
                ffn_mod: lin(gpu, &st, &format!("{p}.ffn_norm.project_layer"), true)?,
                skip: lin(gpu, &st, &format!("{p}.skip_in_linear"), true)?,
            });
        }
        let theta: Vec<f32> = (0..HD / 2).map(|i| 1.0 / 10000f32.powf((2 * i) as f32 / HD as f32)).collect();
        let l = "net.length_regulator.module";
        let mut lr_convs = Vec::new();
        for k in 0..4 {
            let (ci, gi) = (3 * k, 3 * k + 1);
            let w = st.f32(&format!("{l}.model.{ci}.weight"))?;
            let mut r = vec![0f32; w.len()];
            for o in 0..D {
                for i in 0..D {
                    for j in 0..3 {
                        r[(j * D + o) * D + i] = w[(o * D + i) * 3 + j];
                    }
                }
            }
            lr_convs.push(ConvK3 {
                w: gpu.upload(&r)?,
                b: gpu.upload(&st.f32(&format!("{l}.model.{ci}.bias"))?)?,
                gn: (gpu.upload(&st.f32(&format!("{l}.model.{gi}.weight"))?)?, gpu.upload(&st.f32(&format!("{l}.model.{gi}.bias"))?)?),
            });
        }
        // the final 1x1 conv as a linear layer over [ci][t] columns
        let wo = st.f32(&format!("{l}.model.12.weight"))?;
        Ok(Self {
            layers,
            norm: gpu.upload(&st.f32(&format!("{e}.transformer.norm.norm.weight"))?)?,
            norm_mod: lin(gpu, &st, &format!("{e}.transformer.norm.project_layer"), true)?,
            t1: lin(gpu, &st, &format!("{e}.t_embedder.mlp.0"), true)?,
            t2: lin(gpu, &st, &format!("{e}.t_embedder.mlp.2"), true)?,
            cond_proj: lin(gpu, &st, &format!("{e}.cond_projection"), true)?,
            merge: lin(gpu, &st, &format!("{e}.cond_x_merge_linear"), true)?,
            mlp1: lin(gpu, &st, &format!("{e}.final_mlp.0"), true)?,
            mlp2: lin(gpu, &st, &format!("{e}.final_mlp.2"), true)?,
            theta: gpu.upload(&theta)?,
            lr_in: lin(gpu, &st, &format!("{l}.content_in_proj"), true)?,
            lr_f0: st.f32(&format!("{l}.f0_embedding.weight"))?,
            lr_convs,
            lr_out: Lin { w: gpu.upload(&wo)?, b: Some(gpu.upload(&st.f32(&format!("{l}.model.12.bias"))?)?), out: D, inp: D },
        })
    }

    fn linear(&self, gpu: &Gpu, l: &Lin, x: u64, rows: usize) -> Result<Buf> {
        let y = match &l.b {
            Some(b) => {
                let y = gpu.alloc(rows * l.out * 4)?;
                gpu.launch1("fill_rows_f32", rows * l.out, &[Arg::P(y.ptr), Arg::P(b.ptr), Arg::I(l.out as i32), Arg::I(rows as i32), Arg::I(1)])?;
                y
            }
            None => gpu.zeros(rows * l.out * 4)?,
        };
        gpu.linear_f32(y.ptr, x, l.w.ptr, rows, l.inp, l.out, 1.0)?;
        Ok(y)
    }

    /// InterpolateRegulator: content [n][768] + F0 (Hz, per 10 ms frame) -> condition [frames][768] (row major).
    /// `soft_f0`: instead of the nearest of the 256 pitch bins (about 20 cents apart, so slides and vibrato become
    /// stairs), blend the embeddings of the two bins around the exact pitch, with the pitch interpolated in time too.
    pub fn regulate(&self, gpu: &Gpu, content: &[f32], n: usize, f0: &[f64], frames: usize, soft_f0: bool) -> Result<Buf> {
        let c = gpu.upload(content)?;
        let x = self.linear(gpu, &self.lr_in, c.ptr, n)?;
        let xh: Vec<f32> = gpu.read(x.ptr, n * D)?;
        // nearest interpolation to `frames`, plus the quantized F0 embedding (also nearest-interpolated), as [768][frames]
        let mut cols = vec![0f32; D * frames];
        let (scale_c, scale_f) = (n as f32 / frames as f32, f0.len() as f32 / frames as f32);
        let coarse = f0_to_coarse(f0);
        let exact = f0_bin_position(f0);
        for t in 0..frames {
            let sc = ((t as f32 * scale_c).floor() as usize).min(n - 1);
            let sf = ((t as f32 * scale_f).floor() as usize).min(f0.len() - 1);
            let e = &self.lr_f0[coarse[sf] * D..(coarse[sf] + 1) * D];
            // soft: the exact (fractional) bin, linearly between the two neighbouring F0 frames when both are voiced
            let pos = if soft_f0 && exact[sf].is_some() {
                let x = t as f64 * f0.len() as f64 / frames as f64;
                let (a, w) = ((x.floor() as usize).min(f0.len() - 1), x.fract());
                match (exact[a], exact.get(a + 1).copied().flatten()) {
                    (Some(p), Some(q)) => Some(p * (1.0 - w) + q * w),
                    (Some(p), None) => Some(p),
                    _ => exact[sf],
                }
            } else {
                None
            };
            match pos {
                Some(p) => {
                    let p = p.clamp(1.0, (F0_BINS - 1) as f64);
                    let lo = p.floor() as usize;
                    let hi = (lo + 1).min(F0_BINS - 1);
                    let w = (p - lo as f64) as f32;
                    let (el, eh) = (&self.lr_f0[lo * D..(lo + 1) * D], &self.lr_f0[hi * D..(hi + 1) * D]);
                    for k in 0..D {
                        cols[k * frames + t] = xh[sc * D + k] + el[k] * (1.0 - w) + eh[k] * w;
                    }
                }
                None => {
                    for k in 0..D {
                        cols[k * frames + t] = xh[sc * D + k] + e[k];
                    }
                }
            }
        }
        let mut h = gpu.upload(&cols)?;
        let g = gpu.alloc(D * frames * 4)?;
        for cv in &self.lr_convs {
            let y = gpu.alloc(D * frames * 4)?;
            gpu.launch1("fill_rows_f32", D * frames, &[Arg::P(y.ptr), Arg::P(cv.b.ptr), Arg::I(D as i32), Arg::I(frames as i32), Arg::I(0)])?;
            for j in 0..3 {
                gpu.launch1("gather_shift_f32", D * frames, &[Arg::P(g.ptr), Arg::P(h.ptr), Arg::I(D as i32), Arg::I(1), Arg::I(frames as i32),
                    Arg::I(1), Arg::I(frames as i32), Arg::I(1), Arg::I(0), Arg::I(1), Arg::I(j as i32 - 1)])?;
                gpu.chan_matmul_f32(y.ptr, ptr_f32(cv.w.ptr, j * D * D), g.ptr, D, D, frames, frames, 1.0)?;
            }
            gpu.launch("groupnorm1_cbt_f32", (1, 1, 1), (1024, 1, 1), 0, &[Arg::P(y.ptr), Arg::P(cv.gn.0.ptr), Arg::P(cv.gn.1.ptr),
                Arg::I(D as i32), Arg::I(1), Arg::I(frames as i32), Arg::F(1e-5)])?;
            gpu.launch1("mish_f32", D * frames, &[Arg::P(y.ptr), Arg::L((D * frames) as i64)])?;
            h = y;
        }
        let y = gpu.alloc(D * frames * 4)?;
        gpu.launch1("fill_rows_f32", D * frames, &[Arg::P(y.ptr), Arg::P(self.lr_out.b.as_ref().unwrap().ptr), Arg::I(D as i32), Arg::I(frames as i32), Arg::I(0)])?;
        gpu.chan_matmul_f32(y.ptr, self.lr_out.w.ptr, h.ptr, D, D, frames, frames, 1.0)?;
        // to rows [frames][768]
        let rows = gpu.alloc(D * frames * 4)?;
        gpu.launch("transpose_f32", ((frames as u32).div_ceil(32), (D as u32).div_ceil(32), 1), (32, 8, 1), 0,
            &[Arg::P(rows.ptr), Arg::P(y.ptr), Arg::I(D as i32), Arg::I(frames as i32)])?;
        Ok(rows)
    }

    /// The estimator for `b` batch rows: x, prompt [b][128][t]; cond [b][t][768]; style [b][192] -> [b][128][t].
    pub fn estimate(&self, gpu: &Gpu, x: u64, prompt: u64, cond: u64, style: &[f32], t: usize, b: usize, tstep: f32) -> Result<Buf> {
        let rows = b * t;
        // timestep embedding (same t for every row)
        let half = 128;
        let mut fe = vec![0f32; 256];
        for i in 0..half {
            let f = (-(10000f64.ln() as f32) * i as f32 / half as f32).exp();
            let a = 1000.0 * tstep * f;
            fe[i] = a.cos();
            fe[half + i] = a.sin();
        }
        let fe = gpu.upload(&fe)?;
        let h = self.linear(gpu, &self.t1, fe.ptr, 1)?;
        gpu.launch1("silu_f32", D, &[Arg::P(h.ptr), Arg::I(D as i32)])?;
        let t1 = self.linear(gpu, &self.t2, h.ptr, 1)?;
        // x_in = [x^T | prompt^T | cond_projection(cond) | style] -> merge
        let w_in = 2 * MELS + D + STYLE;
        let cat = gpu.alloc(rows * w_in * 4)?;
        let cp = self.linear(gpu, &self.cond_proj, cond, rows)?;
        let tr = gpu.alloc(t * MELS * 4)?;
        for bi in 0..b {
            for (src, off) in [(x, 0), (prompt, MELS)] {
                gpu.launch("transpose_f32", ((t as u32).div_ceil(32), (MELS as u32).div_ceil(32), 1), (32, 8, 1), 0,
                    &[Arg::P(tr.ptr), Arg::P(src + (bi * MELS * t * 4) as u64), Arg::I(MELS as i32), Arg::I(t as i32)])?;
                gpu.launch1("copy_cols_f32", t * MELS, &[Arg::P(ptr_f32(cat.ptr, bi * t * w_in)), Arg::I(w_in as i32), Arg::I(off as i32), Arg::P(tr.ptr),
                    Arg::I(MELS as i32), Arg::L(t as i64)])?;
            }
            gpu.launch1("copy_cols_f32", t * D, &[Arg::P(ptr_f32(cat.ptr, bi * t * w_in)), Arg::I(w_in as i32), Arg::I((2 * MELS) as i32),
                Arg::P(ptr_f32(cp.ptr, bi * t * D)), Arg::I(D as i32), Arg::L(t as i64)])?;
            let srow: Vec<f32> = (0..t).flat_map(|_| style[bi * STYLE..(bi + 1) * STYLE].iter().copied()).collect();
            let sb = gpu.upload(&srow)?;
            gpu.launch1("copy_cols_f32", t * STYLE, &[Arg::P(ptr_f32(cat.ptr, bi * t * w_in)), Arg::I(w_in as i32), Arg::I((2 * MELS + D) as i32),
                Arg::P(sb.ptr), Arg::I(STYLE as i32), Arg::L(t as i64)])?;
        }
        drop((cp, tr));
        let mut xs = self.linear(gpu, &self.merge, cat.ptr, rows)?;
        drop(cat);
        // AdaLN modulations from t1 (one row, shared by every batch row): [scale | shift]
        let modrow = |l: &Lin| -> Result<Buf> {
            let m = self.linear(gpu, l, t1.ptr, 1)?;
            let mb = gpu.alloc(b * 2 * D * 4)?;
            for bi in 0..b {
                gpu.copy(mb.ptr + (bi * 2 * D * 4) as u64, m.ptr, 2 * D * 4)?;
            }
            Ok(mb)
        };
        let mut skips: Vec<Buf> = Vec::new();
        let normed = gpu.alloc(rows * D * 4)?;
        let scores = gpu.alloc(t * t * 4)?;
        let attn = gpu.alloc(rows * D * 4)?;
        for (i, l) in self.layers.iter().enumerate() {
            if i > LAYERS / 2 {
                let s = skips.pop().unwrap();
                let cat2 = gpu.alloc(rows * 2 * D * 4)?;
                gpu.launch1("copy_cols_f32", rows * D, &[Arg::P(cat2.ptr), Arg::I((2 * D) as i32), Arg::I(0), Arg::P(xs.ptr), Arg::I(D as i32), Arg::L(rows as i64)])?;
                gpu.launch1("copy_cols_f32", rows * D, &[Arg::P(cat2.ptr), Arg::I((2 * D) as i32), Arg::I(D as i32), Arg::P(s.ptr), Arg::I(D as i32), Arg::L(rows as i64)])?;
                xs = self.linear(gpu, &l.skip, cat2.ptr, rows)?;
            }
            let m = modrow(&l.attn_mod)?;
            gpu.launch("ada_rmsnorm_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(xs.ptr), Arg::P(l.attn_norm.ptr), Arg::P(m.ptr),
                Arg::I(D as i32), Arg::I(t as i32), Arg::F(1e-5)])?;
            let qkv = self.linear(gpu, &l.wqkv, normed.ptr, rows)?;
            for off in [0, D] {
                gpu.launch("rope_il_f32", (rows as u32, HEADS as u32, 1), ((HD / 2) as u32, 1, 1), 0, &[
                    Arg::P(qkv.ptr), Arg::I((3 * D) as i32), Arg::I(off as i32), Arg::I(HD as i32), Arg::P(self.theta.ptr), Arg::I(t as i32)])?;
            }
            for bi in 0..b {
                let base = qkv.ptr + (bi * t * 3 * D * 4) as u64;
                for h in 0..HEADS {
                    gpu.sgemm(true, false, t, t, HD, ptr_f32(base, D + h * HD), 3 * D, ptr_f32(base, h * HD), 3 * D, 0.0, scores.ptr, t)?;
                    gpu.launch("softmax_rows_f32", (t as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(scores.ptr), Arg::I(t as i32), Arg::F(1.0 / (HD as f32).sqrt())])?;
                    gpu.sgemm(false, false, HD, t, t, ptr_f32(base, 2 * D + h * HD), 3 * D, scores.ptr, t, 0.0, ptr_f32(attn.ptr, bi * t * D + h * HD), D)?;
                }
            }
            drop(qkv);
            let o = self.linear(gpu, &l.wo, attn.ptr, rows)?;
            gpu.launch1("add_f32", rows * D, &[Arg::P(xs.ptr), Arg::P(xs.ptr), Arg::P(o.ptr), Arg::L((rows * D) as i64)])?;
            let m = modrow(&l.ffn_mod)?;
            gpu.launch("ada_rmsnorm_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(xs.ptr), Arg::P(l.ffn_norm.ptr), Arg::P(m.ptr),
                Arg::I(D as i32), Arg::I(t as i32), Arg::F(1e-5)])?;
            let gu = self.linear(gpu, &l.w1w3, normed.ptr, rows)?;
            let inter = l.w1w3.out / 2;
            let act = gpu.alloc(rows * inter * 4)?;
            gpu.launch1("silu_mul_f32", rows * inter, &[Arg::P(act.ptr), Arg::P(gu.ptr), Arg::I(inter as i32), Arg::I(rows as i32)])?;
            drop(gu);
            let f = self.linear(gpu, &l.w2, act.ptr, rows)?;
            gpu.launch1("add_f32", rows * D, &[Arg::P(xs.ptr), Arg::P(xs.ptr), Arg::P(f.ptr), Arg::L((rows * D) as i64)])?;
            if i < LAYERS / 2 {
                let keep = gpu.alloc(rows * D * 4)?;
                gpu.copy(keep.ptr, xs.ptr, rows * D * 4)?;
                skips.push(keep);
            }
        }
        let m = modrow(&self.norm_mod)?;
        gpu.launch("ada_rmsnorm_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(xs.ptr), Arg::P(self.norm.ptr), Arg::P(m.ptr),
            Arg::I(D as i32), Arg::I(t as i32), Arg::F(1e-5)])?;
        let h = self.linear(gpu, &self.mlp1, normed.ptr, rows)?;
        gpu.launch1("silu_f32", rows * D, &[Arg::P(h.ptr), Arg::I((rows * D) as i32)])?;
        let y = self.linear(gpu, &self.mlp2, h.ptr, rows)?;
        // rows [b][t][128] -> [b][128][t]
        let out = gpu.alloc(rows * MELS * 4)?;
        for bi in 0..b {
            gpu.launch("transpose_f32", ((MELS as u32).div_ceil(32), (t as u32).div_ceil(32), 1), (32, 8, 1), 0,
                &[Arg::P(out.ptr + (bi * MELS * t * 4) as u64), Arg::P(y.ptr + (bi * t * MELS * 4) as u64), Arg::I(t as i32), Arg::I(MELS as i32)])?;
        }
        Ok(out)
    }

    /// CFM inference: cond [t][768] (prompt part first), prompt mel [128][p], style [192], noise [128][t] -> mel [128][t].
    #[allow(clippy::too_many_arguments)]
    pub fn infer(&self, gpu: &Gpu, cond: &Buf, t: usize, prompt: &[f32], p: usize, style: &[f32], noise: &[f32], steps: usize, cfg: f32) -> Result<Vec<f32>> {
        let n = MELS * t;
        // batch rows: 0 conditional, 1 with prompt, style and content zeroed
        let mut px = vec![0f32; 2 * n];
        for c in 0..MELS {
            for f in 0..p {
                px[c * t + f] = prompt[c * p + f];
            }
        }
        let px = gpu.upload(&px)?;
        let conds = gpu.zeros(2 * t * D * 4)?;
        gpu.copy(conds.ptr, cond.ptr, t * D * 4)?;
        let mut st = style.to_vec();
        st.extend(std::iter::repeat_n(0.0, STYLE));
        let mut x0 = noise.to_vec();
        for c in 0..MELS {
            for f in 0..p {
                x0[c * t + f] = 0.0;
            }
        }
        let x = gpu.upload(&x0)?;
        let xx = gpu.alloc(2 * n * 4)?;
        // torch.linspace(0, 1, steps + 1)
        let step = 1.0f32 / steps as f32;
        let half = (steps + 1) / 2;
        let span: Vec<f32> = (0..=steps).map(|i| if i < half { step * i as f32 } else { 1.0 - step * (steps - i) as f32 }).collect();
        let mut tt = span[0];
        for s in 1..=steps {
            let dt = span[s] - span[s - 1];
            gpu.copy(xx.ptr, x.ptr, n * 4)?;
            gpu.copy(xx.ptr + (n * 4) as u64, x.ptr, n * 4)?;
            let d = self.estimate(gpu, xx.ptr, px.ptr, conds.ptr, &st, t, 2, tt)?;
            gpu.launch1("seedvc_euler_f32", n, &[Arg::P(x.ptr), Arg::P(d.ptr), Arg::I(MELS as i32), Arg::I(t as i32), Arg::I(p as i32), Arg::F(dt), Arg::F(cfg)])?;
            tt += dt;
        }
        gpu.read(x.ptr, n)
    }
}

/// The un-rounded bin position of each voiced frame (what f0_to_coarse rounds), None for unvoiced ones.
pub fn f0_bin_position(f0: &[f64]) -> Vec<Option<f64>> {
    let mel_min = 1127.0 * (1.0f64 + 50.0 / 700.0).ln();
    let mel_max = 1127.0 * (1.0f64 + 1100.0 / 700.0).ln();
    let a = (F0_BINS as f64 - 2.0) / (mel_max - mel_min);
    let b = mel_min * a - 1.0;
    f0.iter().map(|f| if *f > 1.0 { Some(1127.0 * (1.0 + f / 700.0).ln() * a - b) } else { None }).collect()
}

/// length_regulator.f0_to_coarse (256 bins), in f64 like the numpy / float64 path upstream.
pub fn f0_to_coarse(f0: &[f64]) -> Vec<usize> {
    let mel_min = 1127.0 * (1.0f64 + 50.0 / 700.0).ln();
    let mel_max = 1127.0 * (1.0f64 + 1100.0 / 700.0).ln();
    let a = (F0_BINS as f64 - 2.0) / (mel_max - mel_min);
    let b = mel_min * a - 1.0;
    f0.iter()
        .map(|f| {
            let mut m = 1127.0 * (1.0 + f / 700.0).ln();
            if m > 0.0 {
                m = m * a - b;
            }
            let mut c = m.round_ties_even() as i64;
            if c <= 0 {
                c = 0;
            }
            if c < 1 {
                c += 1;
            }
            // upstream zeroes values >= bins first, so its "+ (bins - 1)" never fires: they end up as bin 0
            if c >= F0_BINS as i64 {
                c = 0;
            }
            c as usize
        })
        .collect()
}
