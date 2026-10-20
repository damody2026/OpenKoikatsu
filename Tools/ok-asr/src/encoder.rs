//! Qwen3-ASR audio tower (Qwen3ASRAudioEncoder) in f32:
//! mel [128, T] is cut into 100-frame chunks; each chunk runs three Conv2d(3, stride 2, pad 1) + GELU
//! (128 x 100 -> 480 x 16 x 13), conv_out (7680 -> 896) and gets sinusoid positions from 0 again.
//! The valid rows of all chunks form one sequence for 18 pre-LayerNorm layers whose attention is
//! bidirectional within windows of 104 tokens; then ln_post, proj1 + GELU, proj2 (896 -> 1024).

use anyhow::Result;

use crate::gpu::{Arg, Buf, Gpu};
use crate::mel::{encoder_tokens, MELS};
use crate::weights::Store;

const D: usize = 896;
const HEADS: usize = 14;
const HEAD_DIM: usize = D / HEADS;
const FFN: usize = 3584;
const LAYERS: usize = 18;
const CONV: usize = 480;
const CHUNK: usize = 100; // n_window * 2
const WINDOW: usize = 13 * (800 / CHUNK); // tokens per attention window (n_window_infer)
pub const OUT_DIM: usize = 1024;
const EPS: f32 = 1e-5;

struct Layer {
    ln1_w: Buf,
    ln1_b: Buf,
    wqkv: Buf,
    bqkv: Buf,
    wo: Buf,
    bo: Buf,
    ln2_w: Buf,
    ln2_b: Buf,
    w1: Buf,
    b1: Buf,
    w2: Buf,
    b2: Buf,
}

pub struct Encoder {
    conv: [(Buf, Buf); 3],
    conv_out: Buf,
    positions: Buf, // [13, D] sinusoid rows
    layers: Vec<Layer>,
    ln_post_w: Buf,
    ln_post_b: Buf,
    proj1_w: Buf,
    proj1_b: Buf,
    proj2_w: Buf,
    proj2_b: Buf,
}

/// SinusoidsPositionEmbedding(1500, 896)[:rows], computed in f32 like the PyTorch buffer.
fn sinusoids(rows: usize) -> Vec<f32> {
    let half = D / 2;
    let step = (10000f64.ln() / (half - 1) as f64) as f32;
    let inv: Vec<f32> = (0..half).map(|i| (-step * i as f32).exp()).collect();
    let mut out = Vec::with_capacity(rows * D);
    for t in 0..rows {
        out.extend(inv.iter().map(|&f| (t as f32 * f).sin()));
        out.extend(inv.iter().map(|&f| (t as f32 * f).cos()));
    }
    out
}

impl Encoder {
    pub fn load(gpu: &Gpu, st: &Store) -> Result<Self> {
        let p = "thinker.audio_tower";
        let conv = [1, 2, 3].map(|i| -> Result<(Buf, Buf)> {
            Ok((st.f32(gpu, &format!("{p}.conv2d{i}.weight"))?, st.f32(gpu, &format!("{p}.conv2d{i}.bias"))?))
        });
        let [c1, c2, c3] = conv;
        let mut layers = Vec::new();
        for i in 0..LAYERS {
            let l = format!("{p}.layers.{i}");
            let a = format!("{l}.self_attn");
            layers.push(Layer {
                ln1_w: st.f32(gpu, &format!("{l}.self_attn_layer_norm.weight"))?,
                ln1_b: st.f32(gpu, &format!("{l}.self_attn_layer_norm.bias"))?,
                wqkv: st.f32_cat(gpu, &[format!("{a}.q_proj.weight"), format!("{a}.k_proj.weight"), format!("{a}.v_proj.weight")])?,
                bqkv: st.f32_cat(gpu, &[format!("{a}.q_proj.bias"), format!("{a}.k_proj.bias"), format!("{a}.v_proj.bias")])?,
                wo: st.f32(gpu, &format!("{a}.out_proj.weight"))?,
                bo: st.f32(gpu, &format!("{a}.out_proj.bias"))?,
                ln2_w: st.f32(gpu, &format!("{l}.final_layer_norm.weight"))?,
                ln2_b: st.f32(gpu, &format!("{l}.final_layer_norm.bias"))?,
                w1: st.f32(gpu, &format!("{l}.fc1.weight"))?,
                b1: st.f32(gpu, &format!("{l}.fc1.bias"))?,
                w2: st.f32(gpu, &format!("{l}.fc2.weight"))?,
                b2: st.f32(gpu, &format!("{l}.fc2.bias"))?,
            });
        }
        Ok(Self {
            conv: [c1?, c2?, c3?],
            conv_out: st.f32(gpu, &format!("{p}.conv_out.weight"))?,
            positions: gpu.upload(&sinusoids(13))?,
            layers,
            ln_post_w: st.f32(gpu, &format!("{p}.ln_post.weight"))?,
            ln_post_b: st.f32(gpu, &format!("{p}.ln_post.bias"))?,
            proj1_w: st.f32(gpu, &format!("{p}.proj1.weight"))?,
            proj1_b: st.f32(gpu, &format!("{p}.proj1.bias"))?,
            proj2_w: st.f32(gpu, &format!("{p}.proj2.weight"))?,
            proj2_b: st.f32(gpu, &format!("{p}.proj2.bias"))?,
        })
    }

    /// y[rows, out] = x * w^T + b
    fn affine(&self, gpu: &Gpu, y: u64, x: u64, w: u64, b: u64, rows: usize, inp: usize, out: usize) -> Result<()> {
        gpu.launch1("fill_rows_f32", rows * out, &[Arg::P(y), Arg::P(b), Arg::I(out as i32), Arg::I(rows as i32), Arg::I(1)])?;
        gpu.linear_f32(y, x, w, rows, inp, out, 1.0)
    }

    fn gelu(&self, gpu: &Gpu, x: u64, n: usize) -> Result<()> {
        gpu.launch1("gelu_f32", n, &[Arg::P(x), Arg::L(n as i64)])
    }

    fn layernorm(&self, gpu: &Gpu, y: u64, x: u64, w: u64, b: u64, rows: usize) -> Result<()> {
        gpu.launch("layernorm_rows_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(y), Arg::P(x), Arg::P(w), Arg::P(b), Arg::I(D as i32), Arg::F(EPS)])
    }

    /// One chunk [MELS, CHUNK] (zero padded) -> rows [13, D] after conv_out and positions.
    fn chunk(&self, gpu: &Gpu, mel: u64, out: u64) -> Result<()> {
        let (mut c, mut h, mut w) = (1usize, MELS, CHUNK);
        let mut src: Option<Buf> = None;
        for (wt, bias) in &self.conv {
            let (ho, wo) = ((h - 1) / 2 + 1, (w - 1) / 2 + 1);
            let plane = ho * wo;
            let cols = gpu.alloc(c * 9 * plane * 4)?;
            let input = src.as_ref().map(|b| b.ptr).unwrap_or(mel);
            gpu.launch1("im2col_3x3s2_f32", c * 9 * plane, &[Arg::P(cols.ptr), Arg::P(input), Arg::I(c as i32), Arg::I(h as i32), Arg::I(w as i32), Arg::I(ho as i32), Arg::I(wo as i32)])?;
            let y = gpu.alloc(CONV * plane * 4)?;
            gpu.launch1("fill_rows_f32", CONV * plane, &[Arg::P(y.ptr), Arg::P(bias.ptr), Arg::I(CONV as i32), Arg::I(plane as i32), Arg::I(0)])?;
            gpu.chan_matmul_f32(y.ptr, wt.ptr, cols.ptr, CONV, c * 9, plane, plane, 1.0)?;
            self.gelu(gpu, y.ptr, CONV * plane)?;
            src = Some(y);
            (c, h, w) = (CONV, ho, wo);
        }
        let conv = src.expect("three conv layers");
        let rows = gpu.alloc(w * CONV * h * 4)?;
        gpu.launch1("cnn_rows_f32", w * CONV * h, &[Arg::P(rows.ptr), Arg::P(conv.ptr), Arg::I(CONV as i32), Arg::I(h as i32), Arg::I(w as i32)])?;
        gpu.linear_f32(out, rows.ptr, self.conv_out.ptr, w, CONV * h, D, 0.0)?;
        gpu.launch1("add_f32", w * D, &[Arg::P(out), Arg::P(out), Arg::P(self.positions.ptr), Arg::L((w * D) as i64)])
    }

    /// mel [MELS, frames] (host) -> audio embeddings [tokens, OUT_DIM] f32 on the device.
    pub fn forward(&self, gpu: &Gpu, mel: &[f32], frames: usize) -> Result<(Buf, usize)> {
        let chunks = frames.div_ceil(CHUNK);
        let total = encoder_tokens(frames);
        // Host side split: chunk k is mel[:, k*100 .. k*100 + len], zero padded to 100 frames.
        let mut padded = vec![0f32; chunks * MELS * CHUNK];
        for k in 0..chunks {
            let len = (frames - k * CHUNK).min(CHUNK);
            for m in 0..MELS {
                let src = &mel[m * frames + k * CHUNK..m * frames + k * CHUNK + len];
                padded[(k * MELS + m) * CHUNK..(k * MELS + m) * CHUNK + len].copy_from_slice(src);
            }
        }
        let dev_mel = gpu.upload(&padded)?;
        let mut x = gpu.alloc(total * D * 4)?;
        let rows13 = gpu.alloc(13 * D * 4)?;
        let mut at = 0usize;
        for k in 0..chunks {
            let len = (frames - k * CHUNK).min(CHUNK);
            let valid = encoder_tokens(len);
            self.chunk(gpu, dev_mel.at(k * MELS * CHUNK * 4), rows13.ptr)?;
            gpu.copy(x.at(at * D * 4), rows13.ptr, valid * D * 4)?;
            at += valid;
        }
        debug_assert_eq!(at, total);

        let n = total;
        let normed = gpu.alloc(n * D * 4)?;
        let qkv = gpu.alloc(n * 3 * D * 4)?;
        let attn = gpu.alloc(n * D * 4)?;
        let proj = gpu.alloc(n * D * 4)?;
        let ffn = gpu.alloc(n * FFN * 4)?;
        let smem = ((HEAD_DIM + WINDOW) * 4) as u32;
        for l in &self.layers {
            self.layernorm(gpu, normed.ptr, x.ptr, l.ln1_w.ptr, l.ln1_b.ptr, n)?;
            self.affine(gpu, qkv.ptr, normed.ptr, l.wqkv.ptr, l.bqkv.ptr, n, D, 3 * D)?;
            gpu.launch("attention_window_f32", (n as u32, HEADS as u32, 1), (128, 1, 1), smem, &[
                Arg::P(attn.ptr), Arg::P(qkv.ptr), Arg::I((3 * D) as i32), Arg::I(HEADS as i32), Arg::I(HEAD_DIM as i32),
                Arg::I(n as i32), Arg::I(WINDOW as i32), Arg::F(1.0 / (HEAD_DIM as f32).sqrt())])?;
            self.affine(gpu, proj.ptr, attn.ptr, l.wo.ptr, l.bo.ptr, n, D, D)?;
            gpu.launch1("add_f32", n * D, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(proj.ptr), Arg::L((n * D) as i64)])?;
            self.layernorm(gpu, normed.ptr, x.ptr, l.ln2_w.ptr, l.ln2_b.ptr, n)?;
            self.affine(gpu, ffn.ptr, normed.ptr, l.w1.ptr, l.b1.ptr, n, D, FFN)?;
            self.gelu(gpu, ffn.ptr, n * FFN)?;
            self.affine(gpu, proj.ptr, ffn.ptr, l.w2.ptr, l.b2.ptr, n, FFN, D)?;
            gpu.launch1("add_f32", n * D, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(proj.ptr), Arg::L((n * D) as i64)])?;
        }
        self.layernorm(gpu, normed.ptr, x.ptr, self.ln_post_w.ptr, self.ln_post_b.ptr, n)?;
        self.affine(gpu, proj.ptr, normed.ptr, self.proj1_w.ptr, self.proj1_b.ptr, n, D, D)?;
        self.gelu(gpu, proj.ptr, n * D)?;
        x = gpu.alloc(n * OUT_DIM * 4)?;
        self.affine(gpu, x.ptr, proj.ptr, self.proj2_w.ptr, self.proj2_b.ptr, n, D, OUT_DIM)?;
        Ok((x, n))
    }
}
