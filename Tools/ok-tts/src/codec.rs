//! Qwen3-TTS 12Hz speech tokenizer decoder: 16 codebooks at 12.5 Hz -> 24 kHz mono waveform.
//! Runs in f32 with activations laid out [channels, time]. Mirrors
//! qwen_tts/core/tokenizer_12hz/modeling_qwen3_tts_tokenizer_v2.py (Qwen3TTSTokenizerV2Decoder).

use anyhow::Result;

use crate::gpu::{ptr_f32, Arg, Buf, Gpu};
use crate::weights::Store;

pub const SAMPLES_PER_FRAME: usize = 1920;
const CODEBOOKS: usize = 16;
const ENTRIES: usize = 2048;
const CB_DIM: usize = 256;
const LATENT: usize = 1024;
const TF_HIDDEN: usize = 512;
const TF_HEADS: usize = 16;
const TF_HEAD_DIM: usize = 64;
const TF_INTER: usize = 1024;
const TF_WINDOW: i32 = 72;
const CHUNK: usize = 300;
const LEFT_CONTEXT: usize = 25;

struct Conv {
    w: Buf, // [k][co][ci]
    b: Buf,
    co: usize,
    ci: usize,
    k: usize,
    dil: usize,
}

struct TConv {
    w: Buf, // [co * k][ci]
    b: Buf,
    co: usize,
    ci: usize,
    k: usize,
    stride: usize,
}

struct Snake {
    a: Buf,
    ib: Buf,
}

struct TfLayer {
    ln1: Buf,
    ln2: Buf,
    wqkv: Buf,
    wo: Buf,
    wgu: Buf,
    wdown: Buf,
    attn_scale: Buf,
    mlp_scale: Buf,
}

struct ConvNext {
    dw_w: Buf,
    dw_b: Buf,
    ln_w: Buf,
    ln_b: Buf,
    pw1: Conv,
    pw2: Conv,
    gamma: Buf,
}

struct ResUnit {
    s1: Snake,
    c1: Conv,
    s2: Snake,
    c2: Conv,
}

struct Block {
    snake: Snake,
    up: TConv,
    res: Vec<ResUnit>,
}

pub struct Codec {
    tables: Buf,
    out_first: Buf,
    out_rest: Buf,
    pre_conv: Conv,
    in_proj_w: Buf,
    in_proj_b: Buf,
    layers: Vec<TfLayer>,
    tf_norm: Buf,
    out_proj_w: Buf,
    out_proj_b: Buf,
    inv_freq: Buf,
    upsample: Vec<(TConv, ConvNext)>,
    dec0: Conv,
    blocks: Vec<Block>,
    final_snake: Snake,
    final_conv: Conv,
}

fn up(gpu: &Gpu, v: &[f32]) -> Result<Buf> {
    gpu.upload(v)
}

fn load_conv(gpu: &Gpu, st: &Store, prefix: &str, dil: usize) -> Result<Conv> {
    let (w, shape) = st.f32(&format!("{prefix}.weight"))?;
    let (co, ci, k) = (shape[0], shape[1], shape[2]);
    let mut r = vec![0f32; w.len()];
    for o in 0..co {
        for i in 0..ci {
            for j in 0..k {
                r[(j * co + o) * ci + i] = w[(o * ci + i) * k + j];
            }
        }
    }
    let b = match st.f32(&format!("{prefix}.bias")) {
        Ok((b, _)) => b,
        Err(_) => vec![0.0; co],
    };
    Ok(Conv { w: up(gpu, &r)?, b: up(gpu, &b)?, co, ci, k, dil })
}

/// Linear layer weight [out, in] used as a 1x1 conv.
fn load_linear_as_conv(gpu: &Gpu, st: &Store, prefix: &str) -> Result<Conv> {
    let (w, shape) = st.f32(&format!("{prefix}.weight"))?;
    let (b, _) = st.f32(&format!("{prefix}.bias"))?;
    Ok(Conv { w: up(gpu, &w)?, b: up(gpu, &b)?, co: shape[0], ci: shape[1], k: 1, dil: 1 })
}

fn load_tconv(gpu: &Gpu, st: &Store, prefix: &str, stride: usize) -> Result<TConv> {
    let (w, shape) = st.f32(&format!("{prefix}.weight"))?;
    let (ci, co, k) = (shape[0], shape[1], shape[2]);
    // [ci][co][k] -> [(co * k + j)][ci]
    let mut r = vec![0f32; w.len()];
    for i in 0..ci {
        for o in 0..co {
            for j in 0..k {
                r[(o * k + j) * ci + i] = w[(i * co + o) * k + j];
            }
        }
    }
    let (b, _) = st.f32(&format!("{prefix}.bias"))?;
    Ok(TConv { w: up(gpu, &r)?, b: up(gpu, &b)?, co, ci, k, stride })
}

fn load_snake(gpu: &Gpu, st: &Store, prefix: &str) -> Result<Snake> {
    let (alpha, _) = st.f32(&format!("{prefix}.alpha"))?;
    let (beta, _) = st.f32(&format!("{prefix}.beta"))?;
    let a: Vec<f32> = alpha.iter().map(|x| x.exp()).collect();
    let ib: Vec<f32> = beta.iter().map(|x| 1.0 / (x.exp() + 1e-9)).collect();
    Ok(Snake { a: up(gpu, &a)?, ib: up(gpu, &ib)? })
}

fn f(gpu: &Gpu, st: &Store, name: &str) -> Result<Buf> {
    up(gpu, &st.f32(name)?.0)
}

impl Codec {
    pub fn load(gpu: &Gpu, st: &Store) -> Result<Self> {
        let p = "decoder";
        // Codebooks: embedding_sum / clamp(cluster_usage, 1e-5); index 0 = rvq_first, 1..15 = rvq_rest.
        let mut tables = Vec::with_capacity(CODEBOOKS * ENTRIES * CB_DIM);
        for q in 0..CODEBOOKS {
            let (group, idx) = if q == 0 { ("rvq_first", 0) } else { ("rvq_rest", q - 1) };
            let base = format!("{p}.quantizer.{group}.vq.layers.{idx}._codebook");
            let (sum, _) = st.f32(&format!("{base}.embedding_sum"))?;
            let (usage, _) = st.f32(&format!("{base}.cluster_usage"))?;
            for e in 0..ENTRIES {
                let u = usage[e].max(1e-5);
                tables.extend(sum[e * CB_DIM..(e + 1) * CB_DIM].iter().map(|v| v / u));
            }
        }
        let mut layers = Vec::new();
        for i in 0..8 {
            let l = format!("{p}.pre_transformer.layers.{i}");
            let mut qkv = st.f32(&format!("{l}.self_attn.q_proj.weight"))?.0;
            qkv.extend(st.f32(&format!("{l}.self_attn.k_proj.weight"))?.0);
            qkv.extend(st.f32(&format!("{l}.self_attn.v_proj.weight"))?.0);
            let mut gu = st.f32(&format!("{l}.mlp.gate_proj.weight"))?.0;
            gu.extend(st.f32(&format!("{l}.mlp.up_proj.weight"))?.0);
            layers.push(TfLayer {
                ln1: f(gpu, st, &format!("{l}.input_layernorm.weight"))?,
                ln2: f(gpu, st, &format!("{l}.post_attention_layernorm.weight"))?,
                wqkv: up(gpu, &qkv)?,
                wo: f(gpu, st, &format!("{l}.self_attn.o_proj.weight"))?,
                wgu: up(gpu, &gu)?,
                wdown: f(gpu, st, &format!("{l}.mlp.down_proj.weight"))?,
                attn_scale: f(gpu, st, &format!("{l}.self_attn_layer_scale.scale"))?,
                mlp_scale: f(gpu, st, &format!("{l}.mlp_layer_scale.scale"))?,
            });
        }
        let inv: Vec<f32> = (0..TF_HEAD_DIM / 2).map(|i| 1.0 / 10000f32.powf((2 * i) as f32 / TF_HEAD_DIM as f32)).collect();
        let mut upsample = Vec::new();
        for i in 0..2 {
            let u = format!("{p}.upsample.{i}");
            let (dw, _) = st.f32(&format!("{u}.1.dwconv.conv.weight"))?;
            upsample.push((
                load_tconv(gpu, st, &format!("{u}.0.conv"), 2)?,
                ConvNext {
                    dw_w: up(gpu, &dw)?,
                    dw_b: f(gpu, st, &format!("{u}.1.dwconv.conv.bias"))?,
                    ln_w: f(gpu, st, &format!("{u}.1.norm.weight"))?,
                    ln_b: f(gpu, st, &format!("{u}.1.norm.bias"))?,
                    pw1: load_linear_as_conv(gpu, st, &format!("{u}.1.pwconv1"))?,
                    pw2: load_linear_as_conv(gpu, st, &format!("{u}.1.pwconv2"))?,
                    gamma: f(gpu, st, &format!("{u}.1.gamma"))?,
                },
            ));
        }
        let mut blocks = Vec::new();
        for (i, rate) in [8usize, 5, 4, 3].iter().enumerate() {
            let b = format!("{p}.decoder.{}.block", i + 1);
            let mut res = Vec::new();
            for (j, dil) in [1usize, 3, 9].iter().enumerate() {
                let r = format!("{b}.{}", j + 2);
                res.push(ResUnit {
                    s1: load_snake(gpu, st, &format!("{r}.act1"))?,
                    c1: load_conv(gpu, st, &format!("{r}.conv1.conv"), *dil)?,
                    s2: load_snake(gpu, st, &format!("{r}.act2"))?,
                    c2: load_conv(gpu, st, &format!("{r}.conv2.conv"), 1)?,
                });
            }
            blocks.push(Block { snake: load_snake(gpu, st, &format!("{b}.0"))?, up: load_tconv(gpu, st, &format!("{b}.1.conv"), *rate)?, res });
        }
        let out_first = st.f32(&format!("{p}.quantizer.rvq_first.output_proj.weight"))?.0;
        let out_rest = st.f32(&format!("{p}.quantizer.rvq_rest.output_proj.weight"))?.0;
        Ok(Self {
            tables: up(gpu, &tables)?,
            out_first: up(gpu, &out_first)?,
            out_rest: up(gpu, &out_rest)?,
            pre_conv: load_conv(gpu, st, &format!("{p}.pre_conv.conv"), 1)?,
            in_proj_w: f(gpu, st, &format!("{p}.pre_transformer.input_proj.weight"))?,
            in_proj_b: f(gpu, st, &format!("{p}.pre_transformer.input_proj.bias"))?,
            layers,
            tf_norm: f(gpu, st, &format!("{p}.pre_transformer.norm.weight"))?,
            out_proj_w: f(gpu, st, &format!("{p}.pre_transformer.output_proj.weight"))?,
            out_proj_b: f(gpu, st, &format!("{p}.pre_transformer.output_proj.bias"))?,
            inv_freq: up(gpu, &inv)?,
            upsample,
            dec0: load_conv(gpu, st, &format!("{p}.decoder.0.conv"), 1)?,
            blocks,
            final_snake: load_snake(gpu, st, &format!("{p}.decoder.5"))?,
            final_conv: load_conv(gpu, st, &format!("{p}.decoder.6.conv"), 1)?,
        })
    }

    /// Samples for frames[start..], decoded with up to LEFT_CONTEXT earlier frames as context, exactly like
    /// one chunk of `decode`. Streaming calls this as frames arrive.
    pub fn decode_tail(&self, gpu: &Gpu, frames: &[[u32; CODEBOOKS]], start: usize) -> Result<Vec<f32>> {
        let context = start.min(LEFT_CONTEXT);
        let chunk = self.forward(gpu, &frames[start - context..])?;
        Ok(chunk[context * SAMPLES_PER_FRAME..].to_vec())
    }

    /// Decode frames of 16 codes into samples, chunked like chunked_decode(300, 25).
    pub fn decode(&self, gpu: &Gpu, frames: &[[u32; CODEBOOKS]]) -> Result<Vec<f32>> {
        let mut wav = Vec::with_capacity(frames.len() * SAMPLES_PER_FRAME);
        let mut start = 0;
        while start < frames.len() {
            let end = (start + CHUNK).min(frames.len());
            let context = if start > LEFT_CONTEXT { LEFT_CONTEXT } else { start };
            let chunk = self.forward(gpu, &frames[start - context..end])?;
            wav.extend_from_slice(&chunk[context * SAMPLES_PER_FRAME..]);
            start = end;
        }
        Ok(wav)
    }

    /// Optional stage dumps for parity checks: (name, [C, T] buffer, C, T).
    pub fn forward_debug(&self, gpu: &Gpu, frames: &[[u32; CODEBOOKS]], mut tap: impl FnMut(&str, &Buf, usize, usize) -> Result<()>) -> Result<Vec<f32>> {
        self.run(gpu, frames, &mut tap)
    }

    fn forward(&self, gpu: &Gpu, frames: &[[u32; CODEBOOKS]]) -> Result<Vec<f32>> {
        self.run(gpu, frames, &mut |_, _, _, _| Ok(()))
    }

    fn run(&self, gpu: &Gpu, frames: &[[u32; CODEBOOKS]], tap: &mut dyn FnMut(&str, &Buf, usize, usize) -> Result<()>) -> Result<Vec<f32>> {
        let t = frames.len();
        let codes: Vec<i32> = frames.iter().flat_map(|f| f.iter().map(|&c| c as i32)).collect();
        let codes = gpu.upload(&codes)?;
        let z1 = gpu.alloc(CB_DIM * t * 4)?;
        let z2 = gpu.alloc(CB_DIM * t * 4)?;
        gpu.launch("rvq_gather", (t as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(z1.ptr), Arg::P(self.tables.ptr), Arg::P(codes.ptr), Arg::I(16), Arg::I(0), Arg::I(1), Arg::I(CB_DIM as i32), Arg::I(ENTRIES as i32), Arg::I(t as i32)])?;
        gpu.launch("rvq_gather", (t as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(z2.ptr), Arg::P(self.tables.ptr), Arg::P(codes.ptr), Arg::I(16), Arg::I(1), Arg::I(16), Arg::I(CB_DIM as i32), Arg::I(ENTRIES as i32), Arg::I(t as i32)])?;
        let q = gpu.alloc(512 * t * 4)?;
        gpu.chan_matmul_f32(q.ptr, self.out_first.ptr, z1.ptr, 512, CB_DIM, t, t, 0.0)?;
        gpu.chan_matmul_f32(q.ptr, self.out_rest.ptr, z2.ptr, 512, CB_DIM, t, t, 1.0)?;
        tap("codec_quantized", &q, 512, t)?;
        let x = self.conv(gpu, &self.pre_conv, &q, t)?;
        tap("codec_pre_conv", &x, LATENT, t)?;
        let x = self.transformer(gpu, &x, t)?;
        tap("codec_pre_transformer", &x, LATENT, t)?;
        let mut x = x;
        let mut len = t;
        for (tc, cn) in &self.upsample {
            x = self.tconv(gpu, tc, &x, len)?;
            len *= tc.stride;
            x = self.convnext(gpu, cn, &x, len)?;
        }
        tap("codec_upsample_out", &x, LATENT, len)?;
        x = self.conv(gpu, &self.dec0, &x, len)?;
        tap("codec_decoder0", &x, self.dec0.co, len)?;
        let mut ch = self.dec0.co;
        for (bi, b) in self.blocks.iter().enumerate() {
            x = self.snake(gpu, &b.snake, &x, ch, len)?;
            x = self.tconv(gpu, &b.up, &x, len)?;
            len *= b.up.stride;
            ch = b.up.co;
            for r in &b.res {
                let y = self.snake(gpu, &r.s1, &x, ch, len)?;
                let y = self.conv(gpu, &r.c1, &y, len)?;
                let y = self.snake(gpu, &r.s2, &y, ch, len)?;
                let y = self.conv(gpu, &r.c2, &y, len)?;
                let sum = gpu.alloc(ch * len * 4)?;
                gpu.launch1("add_scaled_f32", ch * len, &[Arg::P(sum.ptr), Arg::P(x.ptr), Arg::P(y.ptr), Arg::P(0), Arg::I(ch as i32), Arg::I(len as i32), Arg::I(0)])?;
                x = sum;
            }
            if bi == 0 { tap("codec_block0", &x, ch, len)?; }
            if bi == 3 { tap("codec_block3", &x, ch, len)?; }
        }
        x = self.snake(gpu, &self.final_snake, &x, ch, len)?;
        let y = self.conv(gpu, &self.final_conv, &x, len)?;
        gpu.launch1("clamp_f32", len, &[Arg::P(y.ptr), Arg::I(len as i32), Arg::F(-1.0), Arg::F(1.0)])?;
        gpu.read::<f32>(y.ptr, len)
    }

    fn conv(&self, gpu: &Gpu, c: &Conv, x: &Buf, t: usize) -> Result<Buf> {
        let pad = (c.k - 1) * c.dil;
        let y = gpu.alloc(c.co * t * 4)?;
        gpu.launch1("fill_rows_f32", c.co * t, &[Arg::P(y.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(t as i32), Arg::I(0)])?;
        if pad == 0 {
            gpu.chan_matmul_f32(y.ptr, c.w.ptr, x.ptr, c.co, c.ci, t, t, 1.0)?;
            return Ok(y);
        }
        let tp = t + pad;
        let xp = gpu.alloc(c.ci * tp * 4)?;
        gpu.launch1("pad_left_f32", c.ci * tp, &[Arg::P(xp.ptr), Arg::P(x.ptr), Arg::I(c.ci as i32), Arg::I(t as i32), Arg::I(pad as i32)])?;
        for j in 0..c.k {
            gpu.chan_matmul_f32(y.ptr, ptr_f32(c.w.ptr, j * c.co * c.ci), ptr_f32(xp.ptr, j * c.dil), c.co, c.ci, t, tp, 1.0)?;
        }
        Ok(y)
    }

    fn tconv(&self, gpu: &Gpu, c: &TConv, x: &Buf, t: usize) -> Result<Buf> {
        let full = gpu.alloc(c.co * c.k * t * 4)?;
        gpu.chan_matmul_f32(full.ptr, c.w.ptr, x.ptr, c.co * c.k, c.ci, t, t, 0.0)?;
        let out = gpu.alloc(c.co * t * c.stride * 4)?;
        gpu.launch1("overlap_add_f32", c.co * t * c.stride, &[Arg::P(out.ptr), Arg::P(full.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(t as i32), Arg::I(c.k as i32), Arg::I(c.stride as i32)])?;
        Ok(out)
    }

    fn snake(&self, gpu: &Gpu, s: &Snake, x: &Buf, c: usize, t: usize) -> Result<Buf> {
        let y = gpu.alloc(c * t * 4)?;
        gpu.launch1("snake_f32", c * t, &[Arg::P(y.ptr), Arg::P(x.ptr), Arg::P(s.a.ptr), Arg::P(s.ib.ptr), Arg::I(c as i32), Arg::I(t as i32)])?;
        Ok(y)
    }

    fn convnext(&self, gpu: &Gpu, n: &ConvNext, x: &Buf, t: usize) -> Result<Buf> {
        let c = LATENT;
        let xp = gpu.alloc(c * (t + 6) * 4)?;
        gpu.launch1("pad_left_f32", c * (t + 6), &[Arg::P(xp.ptr), Arg::P(x.ptr), Arg::I(c as i32), Arg::I(t as i32), Arg::I(6)])?;
        let dw = gpu.alloc(c * t * 4)?;
        gpu.launch1("depthwise_conv_f32", c * t, &[Arg::P(dw.ptr), Arg::P(xp.ptr), Arg::P(n.dw_w.ptr), Arg::P(n.dw_b.ptr), Arg::I(c as i32), Arg::I(t as i32), Arg::I(7)])?;
        let ln = gpu.alloc(c * t * 4)?;
        gpu.launch("layernorm_cols_f32", (t as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(ln.ptr), Arg::P(dw.ptr), Arg::P(n.ln_w.ptr), Arg::P(n.ln_b.ptr), Arg::I(c as i32), Arg::I(t as i32), Arg::F(1e-6)])?;
        let h = self.conv(gpu, &n.pw1, &ln, t)?;
        gpu.launch1("gelu_f32", n.pw1.co * t, &[Arg::P(h.ptr), Arg::L((n.pw1.co * t) as i64)])?;
        let h2 = self.conv(gpu, &n.pw2, &h, t)?;
        let out = gpu.alloc(c * t * 4)?;
        gpu.launch1("add_scaled_f32", c * t, &[Arg::P(out.ptr), Arg::P(x.ptr), Arg::P(h2.ptr), Arg::P(n.gamma.ptr), Arg::I(c as i32), Arg::I(t as i32), Arg::I(0)])?;
        Ok(out)
    }

    fn transformer(&self, gpu: &Gpu, x_ct: &Buf, t: usize) -> Result<Buf> {
        let (h, nh, hd) = (TF_HIDDEN, TF_HEADS, TF_HEAD_DIM);
        let qkv_dim = 3 * nh * hd;
        let x_tc = gpu.alloc(t * LATENT * 4)?;
        self.transpose(gpu, &x_tc, x_ct, LATENT, t)?;
        let x = gpu.alloc(t * h * 4)?;
        gpu.launch1("fill_rows_f32", t * h, &[Arg::P(x.ptr), Arg::P(self.in_proj_b.ptr), Arg::I(h as i32), Arg::I(t as i32), Arg::I(1)])?;
        gpu.linear_f32(x.ptr, x_tc.ptr, self.in_proj_w.ptr, t, LATENT, h, 1.0)?;
        let normed = gpu.alloc(t * h * 4)?;
        let qkv = gpu.alloc(t * qkv_dim * 4)?;
        let attn = gpu.alloc(t * nh * hd * 4)?;
        let proj = gpu.alloc(t * h * 4)?;
        let gu = gpu.alloc(t * 2 * TF_INTER * 4)?;
        let act = gpu.alloc(t * TF_INTER * 4)?;
        let rows = t as u32;
        for l in &self.layers {
            gpu.launch("rmsnorm_f32", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(x.ptr), Arg::P(l.ln1.ptr), Arg::I(h as i32), Arg::F(1e-5)])?;
            gpu.linear_f32(qkv.ptr, normed.ptr, l.wqkv.ptr, t, h, qkv_dim, 0.0)?;
            for offset in [0, nh * hd] {
                gpu.launch("rope_f32", (rows, nh as u32, 1), (hd as u32, 1, 1), 0, &[Arg::P(qkv.ptr), Arg::I(qkv_dim as i32), Arg::I(offset as i32), Arg::I(hd as i32), Arg::P(self.inv_freq.ptr)])?;
            }
            let smem = ((hd + t) * 4) as u32;
            gpu.launch("attention_f32", (rows, nh as u32, 1), (128, 1, 1), smem, &[Arg::P(attn.ptr), Arg::P(qkv.ptr), Arg::I(qkv_dim as i32), Arg::I(nh as i32), Arg::I(hd as i32), Arg::I(t as i32), Arg::I(TF_WINDOW), Arg::F(1.0 / (hd as f32).sqrt())])?;
            gpu.linear_f32(proj.ptr, attn.ptr, l.wo.ptr, t, nh * hd, h, 0.0)?;
            gpu.launch1("add_scaled_f32", t * h, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(proj.ptr), Arg::P(l.attn_scale.ptr), Arg::I(h as i32), Arg::I(t as i32), Arg::I(1)])?;
            gpu.launch("rmsnorm_f32", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(x.ptr), Arg::P(l.ln2.ptr), Arg::I(h as i32), Arg::F(1e-5)])?;
            gpu.linear_f32(gu.ptr, normed.ptr, l.wgu.ptr, t, h, 2 * TF_INTER, 0.0)?;
            gpu.launch1("silu_mul_f32", t * TF_INTER, &[Arg::P(act.ptr), Arg::P(gu.ptr), Arg::I(TF_INTER as i32), Arg::I(t as i32)])?;
            gpu.linear_f32(proj.ptr, act.ptr, l.wdown.ptr, t, TF_INTER, h, 0.0)?;
            gpu.launch1("add_scaled_f32", t * h, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(proj.ptr), Arg::P(l.mlp_scale.ptr), Arg::I(h as i32), Arg::I(t as i32), Arg::I(1)])?;
        }
        gpu.launch("rmsnorm_f32", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(x.ptr), Arg::P(self.tf_norm.ptr), Arg::I(h as i32), Arg::F(1e-5)])?;
        let y = gpu.alloc(t * LATENT * 4)?;
        gpu.launch1("fill_rows_f32", t * LATENT, &[Arg::P(y.ptr), Arg::P(self.out_proj_b.ptr), Arg::I(LATENT as i32), Arg::I(t as i32), Arg::I(1)])?;
        gpu.linear_f32(y.ptr, normed.ptr, self.out_proj_w.ptr, t, h, LATENT, 1.0)?;
        let out = gpu.alloc(LATENT * t * 4)?;
        self.transpose(gpu, &out, &y, t, LATENT)?;
        Ok(out)
    }

    /// out[cols, rows] = in[rows, cols]^T
    fn transpose(&self, gpu: &Gpu, out: &Buf, inp: &Buf, rows: usize, cols: usize) -> Result<()> {
        gpu.launch("transpose_f32", (cols.div_ceil(32) as u32, rows.div_ceil(32) as u32, 1), (32, 8, 1), 0, &[Arg::P(out.ptr), Arg::P(inp.ptr), Arg::I(rows as i32), Arg::I(cols as i32)])
    }
}
