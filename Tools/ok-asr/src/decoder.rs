//! Qwen3-ASR thinker text model: Qwen3-0.6B (28 layers) with greedy decoding.
//! The audio-only prompt gives every token the same position on all three M-RoPE axes, so the
//! interleaved M-RoPE reduces to ordinary rotate-half RoPE with position = token index (get_rope_index).
//! `Stack` is the Qwen3 layer stack from Tools/ok-tts/src/talker.rs.

use anyhow::{bail, Result};

use crate::gpu::{ptr_bf16, Arg, Buf, Gpu};
use crate::weights::Store;

pub const HIDDEN: usize = 1024;
pub const VOCAB: usize = 151_936;
pub const ENDOFTEXT: u32 = 151_643;
pub const IM_END: u32 = 151_645;

struct Layer {
    ln1: Buf,
    ln2: Buf,
    wqkv: Buf,
    q_norm: Buf,
    k_norm: Buf,
    wo: Buf,
    wgu: Buf,
    wdown: Buf,
}

/// A stack of Qwen3 decoder layers with its own KV cache.
pub struct Stack {
    layers: Vec<Layer>,
    norm: Buf,
    hidden: usize,
    heads: usize,
    kv_heads: usize,
    head_dim: usize,
    inter: usize,
    eps: f32,
    inv_freq: Buf,
    kcache: Vec<Buf>,
    vcache: Vec<Buf>,
    cache_len: usize,
}

impl Stack {
    #[allow(clippy::too_many_arguments)]
    fn load(gpu: &Gpu, st: &Store, prefix: &str, layers: usize, hidden: usize, heads: usize, kv_heads: usize,
            head_dim: usize, inter: usize, theta: f32, cache_len: usize) -> Result<Self> {
        let mut out = Vec::new();
        for i in 0..layers {
            let p = format!("{prefix}.layers.{i}");
            out.push(Layer {
                ln1: st.bf16(gpu, &format!("{p}.input_layernorm.weight"))?,
                ln2: st.bf16(gpu, &format!("{p}.post_attention_layernorm.weight"))?,
                wqkv: st.bf16_cat(gpu, &[format!("{p}.self_attn.q_proj.weight"), format!("{p}.self_attn.k_proj.weight"), format!("{p}.self_attn.v_proj.weight")])?,
                q_norm: st.bf16(gpu, &format!("{p}.self_attn.q_norm.weight"))?,
                k_norm: st.bf16(gpu, &format!("{p}.self_attn.k_norm.weight"))?,
                wo: st.bf16(gpu, &format!("{p}.self_attn.o_proj.weight"))?,
                wgu: st.bf16_cat(gpu, &[format!("{p}.mlp.gate_proj.weight"), format!("{p}.mlp.up_proj.weight")])?,
                wdown: st.bf16(gpu, &format!("{p}.mlp.down_proj.weight"))?,
            });
        }
        // Same formula as transformers' default RoPE init (float32).
        let inv: Vec<f32> = (0..head_dim / 2).map(|i| 1.0 / theta.powf((2 * i) as f32 / head_dim as f32)).collect();
        let cache_bytes = kv_heads * cache_len * head_dim * 2;
        let mut kcache = Vec::new();
        let mut vcache = Vec::new();
        for _ in 0..layers {
            kcache.push(gpu.zeros(cache_bytes)?);
            vcache.push(gpu.zeros(cache_bytes)?);
        }
        Ok(Self {
            layers: out,
            norm: st.bf16(gpu, &format!("{prefix}.norm.weight"))?,
            hidden, heads, kv_heads, head_dim, inter, eps: 1e-6,
            inv_freq: gpu.upload(&inv)?,
            kcache, vcache, cache_len,
        })
    }

    /// Run `n` tokens starting at cache position `pos0`; returns the final-normed hidden states [n, hidden].
    pub fn forward(&self, gpu: &Gpu, x: u64, n: usize, pos0: usize) -> Result<Buf> {
        if pos0 + n > self.cache_len {
            bail!("sequence too long: {} > {}", pos0 + n, self.cache_len);
        }
        let (h, hd, nh, kvh) = (self.hidden, self.head_dim, self.heads, self.kv_heads);
        let qkv_dim = (nh + 2 * kvh) * hd;
        let resid = gpu.alloc(n * h * 2)?;
        gpu.copy(resid.ptr, x, n * h * 2)?;
        let normed = gpu.alloc(n * h * 2)?;
        let qkv = gpu.alloc(n * qkv_dim * 2)?;
        let attn = gpu.alloc(n * nh * hd * 2)?;
        let proj = gpu.alloc(n * h * 2)?;
        let gu = gpu.alloc(n * 2 * self.inter * 2)?;
        let act = gpu.alloc(n * self.inter * 2)?;
        let rows = n as u32;
        let elems = (n * h) as i32;
        for (li, l) in self.layers.iter().enumerate() {
            gpu.launch("rmsnorm_bf16", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(resid.ptr), Arg::P(l.ln1.ptr), Arg::I(h as i32), Arg::F(self.eps)])?;
            gpu.linear_bf16(qkv.ptr, normed.ptr, l.wqkv.ptr, n, h, qkv_dim, 0.0)?;
            gpu.launch("qk_norm_rope_bf16", (rows, nh as u32, 1), (hd as u32, 1, 1), 0, &[
                Arg::P(qkv.ptr), Arg::I(qkv_dim as i32), Arg::I(0), Arg::P(l.q_norm.ptr), Arg::I(hd as i32), Arg::F(self.eps),
                Arg::P(self.inv_freq.ptr), Arg::I(pos0 as i32), Arg::P(0), Arg::I(self.cache_len as i32)])?;
            gpu.launch("qk_norm_rope_bf16", (rows, kvh as u32, 1), (hd as u32, 1, 1), 0, &[
                Arg::P(qkv.ptr), Arg::I(qkv_dim as i32), Arg::I((nh * hd) as i32), Arg::P(l.k_norm.ptr), Arg::I(hd as i32), Arg::F(self.eps),
                Arg::P(self.inv_freq.ptr), Arg::I(pos0 as i32), Arg::P(self.kcache[li].ptr), Arg::I(self.cache_len as i32)])?;
            gpu.launch("store_v_bf16", (rows, kvh as u32, 1), (hd as u32, 1, 1), 0, &[
                Arg::P(qkv.ptr), Arg::I(qkv_dim as i32), Arg::I(((nh + kvh) * hd) as i32), Arg::I(hd as i32), Arg::I(pos0 as i32),
                Arg::P(self.vcache[li].ptr), Arg::I(self.cache_len as i32)])?;
            let smem = ((hd + pos0 + n) * 4) as u32;
            gpu.launch("attention_bf16", (rows, nh as u32, 1), (128, 1, 1), smem, &[
                Arg::P(attn.ptr), Arg::P(qkv.ptr), Arg::I(qkv_dim as i32), Arg::P(self.kcache[li].ptr), Arg::P(self.vcache[li].ptr),
                Arg::I(nh as i32), Arg::I(kvh as i32), Arg::I(hd as i32), Arg::I(self.cache_len as i32), Arg::I(pos0 as i32), Arg::I(0),
                Arg::F(1.0 / (hd as f32).sqrt())])?;
            gpu.linear_bf16(proj.ptr, attn.ptr, l.wo.ptr, n, nh * hd, h, 0.0)?;
            gpu.launch1("add_bf16", n * h, &[Arg::P(resid.ptr), Arg::P(resid.ptr), Arg::P(proj.ptr), Arg::I(elems)])?;
            gpu.launch("rmsnorm_bf16", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(resid.ptr), Arg::P(l.ln2.ptr), Arg::I(h as i32), Arg::F(self.eps)])?;
            gpu.linear_bf16(gu.ptr, normed.ptr, l.wgu.ptr, n, h, 2 * self.inter, 0.0)?;
            gpu.launch1("silu_mul_bf16", n * self.inter, &[Arg::P(act.ptr), Arg::P(gu.ptr), Arg::I(self.inter as i32), Arg::I(n as i32)])?;
            gpu.linear_bf16(proj.ptr, act.ptr, l.wdown.ptr, n, self.inter, h, 0.0)?;
            gpu.launch1("add_bf16", n * h, &[Arg::P(resid.ptr), Arg::P(resid.ptr), Arg::P(proj.ptr), Arg::I(elems)])?;
        }
        let out = gpu.alloc(n * h * 2)?;
        gpu.launch("rmsnorm_bf16", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(resid.ptr), Arg::P(self.norm.ptr), Arg::I(h as i32), Arg::F(self.eps)])?;
        Ok(out)
    }
}

pub struct Decoder {
    stack: Stack,
    /// embed_tokens [VOCAB, HIDDEN] bf16 in host memory (about 0.3 GB less VRAM); rows are gathered and uploaded.
    embed: Vec<u16>,
    lm_head: Buf,
}

impl Decoder {
    pub fn load(gpu: &Gpu, st: &Store, cache_len: usize) -> Result<Self> {
        Ok(Self {
            stack: Stack::load(gpu, st, "thinker.model", 28, HIDDEN, 16, 8, 128, 3072, 1_000_000.0, cache_len)?,
            embed: st.bytes("thinker.model.embed_tokens.weight", safetensors::Dtype::BF16)?.0.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect(),
            lm_head: st.bf16(gpu, "thinker.lm_head.weight")?,
        })
    }

    pub fn max_positions(&self) -> usize {
        self.stack.cache_len
    }

    fn embed_ids(&self, gpu: &Gpu, out: u64, ids: &[u32]) -> Result<()> {
        let mut rows = Vec::with_capacity(ids.len() * HIDDEN);
        for &id in ids {
            let at = id as usize * HIDDEN;
            rows.extend_from_slice(self.embed.get(at..at + HIDDEN).ok_or_else(|| anyhow::anyhow!("token {id} out of range"))?);
        }
        let up = gpu.upload(&rows)?;
        gpu.copy(out, up.ptr, rows.len() * 2)
    }

    /// Greedy decoding. `prompt` holds the chat-template ids; positions audio_at..audio_at+audio_rows are
    /// <|audio_pad|> and are replaced by `audio` (bf16 [audio_rows, HIDDEN]). Returns the generated ids.
    pub fn generate(&self, gpu: &Gpu, prompt: &[u32], audio: u64, audio_at: usize, audio_rows: usize, max_new: usize) -> Result<Vec<u32>> {
        let n = prompt.len();
        let max_new = max_new.min(self.stack.cache_len.saturating_sub(n));
        let row = HIDDEN * 2;
        let x = gpu.alloc(n * row)?;
        self.embed_ids(gpu, x.ptr, prompt)?;
        gpu.copy(x.at(audio_at * row), audio, audio_rows * row)?;
        let hidden = self.stack.forward(gpu, x.ptr, n, 0)?;
        let logits = gpu.alloc(VOCAB * 2)?;
        let token = gpu.zeros(4)?;
        let step_in = gpu.alloc(row)?;
        let mut out = Vec::new();
        let mut last = ptr_bf16(hidden.ptr, (n - 1) * HIDDEN);
        let mut keep: Buf = hidden;
        for step in 0..max_new {
            gpu.linear_bf16(logits.ptr, last, self.lm_head.ptr, 1, HIDDEN, VOCAB, 0.0)?;
            gpu.launch("argmax_bf16", (1, 1, 1), (1024, 1, 1), 0, &[Arg::P(logits.ptr), Arg::I(VOCAB as i32), Arg::P(token.ptr)])?;
            let id = gpu.read::<i32>(token.ptr, 1)?[0] as u32;
            if id == ENDOFTEXT || id == IM_END {
                break;
            }
            out.push(id);
            if step + 1 == max_new {
                break;
            }
            self.embed_ids(gpu, step_in.ptr, &[id])?;
            keep = self.stack.forward(gpu, step_in.ptr, 1, n + step)?;
            last = keep.ptr;
        }
        drop(keep);
        Ok(out)
    }
}
