//! Qwen3-TTS talker (28-layer Qwen3, codebook 0) and code predictor (5 layers, codebooks 1..15).
//! Mirrors faster_qwen3_tts.generate.fast_generate for a single, unpadded sequence, where the
//! interleaved M-RoPE reduces to ordinary rotate-half RoPE with position = token index.

use anyhow::{bail, Result};

use std::sync::Mutex;

use crate::gpu::{ptr_bf16, Arg, Buf, Gpu, Graph, W};
use crate::sampling::Sampler;
use crate::weights::Store;

pub const HIDDEN: usize = 2048;
pub const NUM_CODE_GROUPS: usize = 16;
const TALKER_VOCAB: usize = 3072;
const PRED_VOCAB: usize = 2048;

pub const CODEC_EOS: u32 = 2150;
pub const CODEC_BOS: u32 = 2149;
pub const CODEC_PAD: u32 = 2148;
pub const CODEC_THINK: u32 = 2154;
pub const CODEC_THINK_BOS: u32 = 2156;
pub const CODEC_THINK_EOS: u32 = 2157;
pub const TTS_BOS: u32 = 151672;
pub const TTS_EOS: u32 = 151673;
pub const TTS_PAD: u32 = 151671;

struct Layer {
    ln1: Buf,
    ln2: Buf,
    wqkv: W,
    q_norm: Buf,
    k_norm: Buf,
    wo: W,
    wgu: W,
    wdown: W,
}

/// A layer matrix (rows of `names` stacked): bf16, or int8 (`gpu::W::int8`, about half the VRAM).
fn load_w(gpu: &Gpu, st: &Store, names: &[String], int8: bool) -> Result<W> {
    if !int8 {
        return Ok(W::B(if names.len() == 1 { st.bf16(gpu, &names[0])? } else { st.bf16_cat(gpu, names)? }));
    }
    let mut w = Vec::new();
    let mut inp = 0;
    for n in names {
        let (v, shape) = st.bf16_as_f32(n)?;
        inp = shape[1];
        w.extend(v);
    }
    W::int8(gpu, &w, inp)
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
            head_dim: usize, inter: usize, theta: f32, cache_len: usize, int8: bool) -> Result<Self> {
        let mut out = Vec::new();
        for i in 0..layers {
            let p = format!("{prefix}.layers.{i}");
            out.push(Layer {
                ln1: st.bf16(gpu, &format!("{p}.input_layernorm.weight"))?,
                ln2: st.bf16(gpu, &format!("{p}.post_attention_layernorm.weight"))?,
                wqkv: load_w(gpu, st, &[format!("{p}.self_attn.q_proj.weight"), format!("{p}.self_attn.k_proj.weight"), format!("{p}.self_attn.v_proj.weight")], int8)?,
                q_norm: st.bf16(gpu, &format!("{p}.self_attn.q_norm.weight"))?,
                k_norm: st.bf16(gpu, &format!("{p}.self_attn.k_norm.weight"))?,
                wo: load_w(gpu, st, &[format!("{p}.self_attn.o_proj.weight")], int8)?,
                wgu: load_w(gpu, st, &[format!("{p}.mlp.gate_proj.weight"), format!("{p}.mlp.up_proj.weight")], int8)?,
                wdown: load_w(gpu, st, &[format!("{p}.mlp.down_proj.weight")], int8)?,
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
            l.wqkv.linear(gpu, qkv.ptr, normed.ptr, n, h, qkv_dim)?;
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
            l.wo.linear(gpu, proj.ptr, attn.ptr, n, nh * hd, h)?;
            gpu.launch1("add_bf16", n * h, &[Arg::P(resid.ptr), Arg::P(resid.ptr), Arg::P(proj.ptr), Arg::I(elems)])?;
            gpu.launch("rmsnorm_bf16", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(resid.ptr), Arg::P(l.ln2.ptr), Arg::I(h as i32), Arg::F(self.eps)])?;
            l.wgu.linear(gpu, gu.ptr, normed.ptr, n, h, 2 * self.inter)?;
            gpu.launch1("silu_mul_bf16", n * self.inter, &[Arg::P(act.ptr), Arg::P(gu.ptr), Arg::I(self.inter as i32), Arg::I(n as i32)])?;
            l.wdown.linear(gpu, proj.ptr, act.ptr, n, self.inter, h)?;
            gpu.launch1("add_bf16", n * h, &[Arg::P(resid.ptr), Arg::P(resid.ptr), Arg::P(proj.ptr), Arg::I(elems)])?;
        }
        let out = gpu.alloc(n * h * 2)?;
        gpu.launch("rmsnorm_bf16", (rows, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(resid.ptr), Arg::P(self.norm.ptr), Arg::I(h as i32), Arg::F(self.eps)])?;
        Ok(out)
    }
}

pub struct Talker {
    stack: Stack,
    codec_embed: Buf,
    text_embed: Buf,
    fc1_w: Buf,
    fc1_b: Buf,
    fc2_w: Buf,
    fc2_b: Buf,
    codec_head: Buf,
    pred: Stack,
    pred_proj_w: Buf,
    pred_proj_b: Buf,
    pred_embeds: Vec<Buf>,
    pred_heads: Vec<Buf>,
    // Fixed buffers the captured predictor graph reads and writes.
    g_past: Buf, // bf16 [HIDDEN]: talker hidden of the previous position
    g_cur: Buf,  // i32 [16]: codes of the current frame (cur[0] from the talker)
    g_rng: Buf,  // u64 [2]: seed, frame counter
    pred_graph: Mutex<Option<((bool, u32, usize), Graph)>>,
}

/// Settings for one utterance; defaults follow E:\TTS\STATUS.md (talker) and faster_qwen3_tts (predictor).
#[derive(Clone)]
pub struct GenConfig {
    pub talker: Sampler,
    pub predictor: Sampler,
    pub repetition_penalty: f32,
    pub min_new_tokens: usize,
    pub max_new_tokens: usize,
    pub seed: u64,
}

impl Default for GenConfig {
    fn default() -> Self {
        Self {
            talker: Sampler { temperature: 0.65, top_k: 30, greedy: false },
            predictor: Sampler { temperature: 0.9, top_k: 50, greedy: false },
            repetition_penalty: 1.05,
            min_new_tokens: 2,
            max_new_tokens: 2048,
            seed: 0,
        }
    }
}

/// Input embeddings prepared for one utterance.
pub struct Prompt {
    pub prefill: Buf,
    pub prefill_len: usize,
    pub trailing: Buf,
    pub trailing_len: usize,
    pub tts_pad: Buf,
}

impl Talker {
    pub fn load(gpu: &Gpu, st: &Store, cache_len: usize) -> Result<Self> {
        // The 28-layer talker's matrices as int8 (about 1.3 GB less VRAM); OK_TTS_WEIGHTS=bf16 keeps them bf16. The small
        // code predictor stays bf16.
        let int8 = std::env::var("OK_TTS_WEIGHTS").map(|v| v != "bf16").unwrap_or(true);
        let stack = Stack::load(gpu, st, "talker.model", 28, HIDDEN, 16, 8, 128, 6144, 1_000_000.0, cache_len, int8)?;
        let pred = Stack::load(gpu, st, "talker.code_predictor.model", 5, 1024, 16, 8, 128, 3072, 1_000_000.0, NUM_CODE_GROUPS + 2, false)?;
        let mut pred_embeds = Vec::new();
        let mut pred_heads = Vec::new();
        for i in 0..NUM_CODE_GROUPS - 1 {
            pred_embeds.push(st.bf16(gpu, &format!("talker.code_predictor.model.codec_embedding.{i}.weight"))?);
            pred_heads.push(st.bf16(gpu, &format!("talker.code_predictor.lm_head.{i}.weight"))?);
        }
        Ok(Self {
            stack,
            codec_embed: st.bf16(gpu, "talker.model.codec_embedding.weight")?,
            text_embed: st.bf16(gpu, "talker.model.text_embedding.weight")?,
            fc1_w: st.bf16(gpu, "talker.text_projection.linear_fc1.weight")?,
            fc1_b: st.bf16(gpu, "talker.text_projection.linear_fc1.bias")?,
            fc2_w: st.bf16(gpu, "talker.text_projection.linear_fc2.weight")?,
            fc2_b: st.bf16(gpu, "talker.text_projection.linear_fc2.bias")?,
            codec_head: st.bf16(gpu, "talker.codec_head.weight")?,
            pred,
            pred_proj_w: st.bf16(gpu, "talker.code_predictor.small_to_mtp_projection.weight")?,
            pred_proj_b: st.bf16(gpu, "talker.code_predictor.small_to_mtp_projection.bias")?,
            pred_embeds,
            pred_heads,
            g_past: gpu.zeros(HIDDEN * 2)?,
            g_cur: gpu.zeros(NUM_CODE_GROUPS * 4)?,
            g_rng: gpu.zeros(16)?,
            pred_graph: Mutex::new(None),
        })
    }

    pub fn max_positions(&self) -> usize {
        self.stack.cache_len
    }

    fn embed(&self, gpu: &Gpu, table: u64, ids: &[u32]) -> Result<Buf> {
        let ids_i: Vec<i32> = ids.iter().map(|&x| x as i32).collect();
        let dev_ids = gpu.upload(&ids_i)?;
        let out = gpu.alloc(ids.len() * HIDDEN * 2)?;
        gpu.launch("embed_bf16", (ids.len() as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(table), Arg::P(dev_ids.ptr), Arg::I(HIDDEN as i32)])?;
        Ok(out)
    }

    fn bias_linear(&self, gpu: &Gpu, x: u64, rows: usize, inp: usize, out_dim: usize, w: u64, b: u64) -> Result<Buf> {
        let y = gpu.alloc(rows * out_dim * 2)?;
        gpu.launch1("fill_rows_bf16", rows * out_dim, &[Arg::P(y.ptr), Arg::P(b), Arg::I(out_dim as i32), Arg::I(rows as i32)])?;
        gpu.linear_bf16(y.ptr, x, w, rows, inp, out_dim, 1.0)?;
        Ok(y)
    }

    /// text_projection(text_embedding(ids)): [n, HIDDEN]
    pub fn text_project(&self, gpu: &Gpu, ids: &[u32]) -> Result<Buf> {
        let n = ids.len();
        let e = self.embed(gpu, self.text_embed.ptr, ids)?;
        let h = self.bias_linear(gpu, e.ptr, n, HIDDEN, HIDDEN, self.fc1_w.ptr, self.fc1_b.ptr)?;
        gpu.launch1("silu_bf16", n * HIDDEN, &[Arg::P(h.ptr), Arg::I((n * HIDDEN) as i32)])?;
        self.bias_linear(gpu, h.ptr, n, HIDDEN, HIDDEN, self.fc2_w.ptr, self.fc2_b.ptr)
    }

    pub fn codec_rows(&self, gpu: &Gpu, ids: &[u32]) -> Result<Buf> {
        self.embed(gpu, self.codec_embed.ptr, ids)
    }

    /// Build the prefill exactly like _build_talker_inputs_local (streaming mode). VoiceDesign: an instruct and no
    /// speaker. Base (x-vector clone): no instruct and a speaker embedding, which goes after the language tags.
    /// `assistant_ids` is "<|im_start|>assistant\n{text}<|im_end|>\n<|im_start|>assistant\n".
    pub fn build_prompt(&self, gpu: &Gpu, instruct_ids: &[u32], assistant_ids: &[u32], language_id: u32, speaker: Option<&[f32]>) -> Result<Prompt> {
        if assistant_ids.len() < 9 {
            bail!("text is empty");
        }
        let row = HIDDEN * 2;
        let add = |dst: u64, a: u64, b: u64, rows: usize| -> Result<()> {
            gpu.launch1("add_bf16", rows * HIDDEN, &[Arg::P(dst), Arg::P(a), Arg::P(b), Arg::I((rows * HIDDEN) as i32)])
        };
        let special = self.text_project(gpu, &[TTS_BOS, TTS_EOS, TTS_PAD])?;
        let (tts_bos, tts_eos, tts_pad) = (special.ptr, special.at(row), special.at(2 * row));
        let tags = self.codec_rows(gpu, &[CODEC_THINK, CODEC_THINK_BOS, language_id, CODEC_THINK_EOS, CODEC_PAD, CODEC_BOS])?;
        // codec rows: think, think_bos, language, think_eos, [speaker], pad, bos
        let codec = match speaker {
            None => tags,
            Some(s) => {
                if s.len() != HIDDEN {
                    bail!("speaker embedding has {} values, expected {HIDDEN}", s.len());
                }
                let all = gpu.alloc(7 * row)?;
                gpu.copy(all.ptr, tags.ptr, 4 * row)?;
                let spk: Vec<u16> = s.iter().map(|v| f32_to_bf16(*v)).collect();
                let spk = gpu.upload(&spk)?;
                gpu.copy(all.at(4 * row), spk.ptr, row)?;
                gpu.copy(all.at(5 * row), tags.at(4 * row), 2 * row)?;
                all
            }
        };
        let tagged = if speaker.is_some() { 6 } else { 5 };
        let instruct = if instruct_ids.is_empty() { None } else { Some(self.text_project(gpu, instruct_ids)?) };
        let li = instruct_ids.len();
        let prefill_len = li + 3 + tagged + 1;
        let prefill = gpu.alloc(prefill_len * row)?;
        let mut at = 0usize;
        if let Some(ins) = &instruct {
            gpu.copy(prefill.ptr, ins.ptr, li * row)?;
            at += li;
        }
        // role: "<|im_start|>assistant\n"
        let role = self.text_project(gpu, &assistant_ids[..3])?;
        gpu.copy(prefill.at(at * row), role.ptr, 3 * row)?;
        at += 3;
        // [pad, .., pad, bos] + codec[0..tagged]
        for i in 0..tagged {
            let text = if i + 1 < tagged { tts_pad } else { tts_bos };
            add(prefill.at((at + i) * row), text, codec.at(i * row), 1)?;
        }
        at += tagged;
        // first text token + codec bos
        let first = self.text_project(gpu, &assistant_ids[3..4])?;
        add(prefill.at(at * row), first.ptr, codec.at(tagged * row), 1)?;
        // trailing: remaining text tokens, then tts_eos
        let text_rest = &assistant_ids[4..assistant_ids.len() - 5];
        let trailing_len = text_rest.len() + 1;
        let trailing = gpu.alloc(trailing_len * row)?;
        if !text_rest.is_empty() {
            let t = self.text_project(gpu, text_rest)?;
            gpu.copy(trailing.ptr, t.ptr, text_rest.len() * row)?;
        }
        gpu.copy(trailing.at(text_rest.len() * row), tts_eos, row)?;
        let pad = gpu.alloc(row)?;
        gpu.copy(pad.ptr, tts_pad, row)?;
        gpu.sync()?;
        Ok(Prompt { prefill, prefill_len, trailing, trailing_len, tts_pad: pad })
    }

    fn logits(&self, gpu: &Gpu, hidden_row: u64, head: u64, inp: usize, vocab: usize) -> Result<Vec<f32>> {
        let out = gpu.alloc(vocab * 2)?;
        gpu.linear_bf16(out.ptr, hidden_row, head, 1, inp, vocab, 0.0)?;
        let raw: Vec<u16> = gpu.read(out.ptr, vocab)?;
        Ok(raw.into_iter().map(bf16_to_f32).collect())
    }

    /// Prefill only: logits of the last position (for parity checks).
    pub fn prefill_logits(&self, gpu: &Gpu, prompt: &Prompt) -> Result<(Vec<f32>, Buf)> {
        let hidden = self.stack.forward(gpu, prompt.prefill.ptr, prompt.prefill_len, 0)?;
        let last = ptr_bf16(hidden.ptr, (prompt.prefill_len - 1) * HIDDEN);
        Ok((self.logits(gpu, last, self.codec_head.ptr, HIDDEN, TALKER_VOCAB)?, hidden))
    }

    /// Teacher-forced predictor logits for one frame (parity checks).
    pub fn predict_forced(&self, gpu: &Gpu, past_hidden: u64, first: u32, toks: &[u32]) -> Result<Vec<Vec<f32>>> {
        let row = HIDDEN * 2;
        let first_e = self.codec_rows(gpu, &[first])?;
        let pin = gpu.alloc(2 * row)?;
        gpu.copy(pin.ptr, past_hidden, row)?;
        gpu.copy(pin.at(row), first_e.ptr, row)?;
        let h = self.bias_linear(gpu, pin.ptr, 2, HIDDEN, 1024, self.pred_proj_w.ptr, self.pred_proj_b.ptr)?;
        let out = self.pred.forward(gpu, h.ptr, 2, 0)?;
        let mut all = vec![self.logits(gpu, ptr_bf16(out.ptr, 1024), self.pred_heads[0].ptr, 1024, PRED_VOCAB)?];
        for cb in 1..NUM_CODE_GROUPS - 1 {
            let e = self.embed(gpu, self.pred_embeds[cb - 1].ptr, &[toks[cb - 1]])?;
            let h = self.bias_linear(gpu, e.ptr, 1, HIDDEN, 1024, self.pred_proj_w.ptr, self.pred_proj_b.ptr)?;
            let out = self.pred.forward(gpu, h.ptr, 1, cb + 1)?;
            all.push(self.logits(gpu, out.ptr, self.pred_heads[cb].ptr, 1024, PRED_VOCAB)?);
        }
        Ok(all)
    }

    /// Draw one token on the device from bf16 logits; nothing comes back to the host.
    #[allow(clippy::too_many_arguments)]
    fn sample_dev(&self, gpu: &Gpu, logits: u64, vocab: usize, s: &crate::sampling::Sampler, out: u64, suppress_from: usize,
                  ban: i32, seen: u64, rep: f32, seed: u64, counter: u64, eos_step: u64, step: i32, rng: u64) -> Result<()> {
        gpu.launch("sample_bf16", (1, 1, 1), (1024, 1, 1), 0, &[
            Arg::P(logits), Arg::I(vocab as i32), Arg::F(s.temperature), Arg::I(s.top_k as i32), Arg::I(s.greedy as i32),
            Arg::I(suppress_from as i32), Arg::I(CODEC_EOS as i32), Arg::I(ban), Arg::P(seen), Arg::F(rep), Arg::I((seen != 0) as i32),
            Arg::L(seed as i64), Arg::L(counter as i64), Arg::P(out), Arg::I(CODEC_EOS as i32), Arg::P(eos_step), Arg::I(step), Arg::P(rng)])
    }

    /// Enqueue codebooks 1..15 for the frame in g_cur, from g_past; captured once into a graph.
    fn record_predictor(&self, gpu: &Gpu, sampler: &Sampler) -> Result<()> {
        let row = HIDDEN * 2;
        let cur = |q: usize| self.g_cur.ptr + (q * 4) as u64;
        let pin = gpu.alloc(2 * row)?;
        gpu.copy(pin.ptr, self.g_past.ptr, row)?;
        gpu.launch("embed_bf16", (1, 1, 1), (256, 1, 1), 0, &[Arg::P(pin.at(row)), Arg::P(self.codec_embed.ptr), Arg::P(cur(0)), Arg::I(HIDDEN as i32)])?;
        let h = self.bias_linear(gpu, pin.ptr, 2, HIDDEN, 1024, self.pred_proj_w.ptr, self.pred_proj_b.ptr)?;
        let out = self.pred.forward(gpu, h.ptr, 2, 0)?;
        let logits = gpu.alloc(PRED_VOCAB * 2)?;
        gpu.linear_bf16(logits.ptr, ptr_bf16(out.ptr, 1024), self.pred_heads[0].ptr, 1, 1024, PRED_VOCAB, 0.0)?;
        self.sample_dev(gpu, logits.ptr, PRED_VOCAB, sampler, cur(1), PRED_VOCAB, -1, 0, 1.0, 0, 1, 0, 0, self.g_rng.ptr)?;
        let e = gpu.alloc(row)?;
        for cb in 1..NUM_CODE_GROUPS - 1 {
            gpu.launch("embed_bf16", (1, 1, 1), (256, 1, 1), 0, &[Arg::P(e.ptr), Arg::P(self.pred_embeds[cb - 1].ptr), Arg::P(cur(cb)), Arg::I(HIDDEN as i32)])?;
            let h = self.bias_linear(gpu, e.ptr, 1, HIDDEN, 1024, self.pred_proj_w.ptr, self.pred_proj_b.ptr)?;
            let out = self.pred.forward(gpu, h.ptr, 1, cb + 1)?;
            gpu.linear_bf16(logits.ptr, out.ptr, self.pred_heads[cb].ptr, 1, 1024, PRED_VOCAB, 0.0)?;
            self.sample_dev(gpu, logits.ptr, PRED_VOCAB, sampler, cur(cb + 1), PRED_VOCAB, -1, 0, 1.0, 0, (cb + 1) as u64, 0, 0, self.g_rng.ptr)?;
        }
        gpu.launch("inc_u64", (1, 1, 1), (32, 1, 1), 0, &[Arg::P(self.g_rng.ptr + 8)])
    }

    /// Full autoregressive generation, device resident: codes are drawn on the GPU straight into a frame
    /// buffer that the next embedding lookups read, and the host only polls for EOS every few frames.
    pub fn generate(&self, gpu: &Gpu, prompt: &Prompt, cfg: &GenConfig) -> Result<Vec<[u32; NUM_CODE_GROUPS]>> {
        let mut all = Vec::new();
        self.generate_stream(gpu, prompt, cfg, &mut |frames| {
            all.extend_from_slice(frames);
            Ok(())
        })?;
        Ok(all)
    }

    /// Same generation, handing finished frames to `on_frames` at every EOS poll (every 4 frames), so a
    /// caller can decode and play audio while the rest is still being generated.
    pub fn generate_stream(&self, gpu: &Gpu, prompt: &Prompt, cfg: &GenConfig,
                           on_frames: &mut dyn FnMut(&[[u32; NUM_CODE_GROUPS]]) -> Result<()>) -> Result<()> {
        const POLL: usize = 4;
        let row = HIDDEN * 2;
        let max_frames = cfg.max_new_tokens.min(self.stack.cache_len.saturating_sub(prompt.prefill_len + 1));
        if max_frames == 0 {
            return Ok(());
        }
        // frames[f][q]; one extra row so the talker can write the next frame's first code.
        let frames = gpu.zeros((max_frames + 1) * NUM_CODE_GROUPS * 4)?;
        let seen = gpu.zeros(TALKER_VOCAB * 4)?;
        let eos = gpu.upload(&[i32::MAX])?;
        let mut table_ptrs: Vec<u64> = vec![self.codec_embed.ptr];
        table_ptrs.extend(self.pred_embeds.iter().map(|b| b.ptr));
        let tables = gpu.upload(&table_ptrs)?;
        let code = |f: usize, q: usize| frames.ptr + ((f * NUM_CODE_GROUPS + q) * 4) as u64;
        let rep = cfg.repetition_penalty;
        let suppress = TALKER_VOCAB - 1024;

        let hidden = self.stack.forward(gpu, prompt.prefill.ptr, prompt.prefill_len, 0)?;
        let past = gpu.alloc(row)?;
        gpu.copy(past.ptr, hidden.at((prompt.prefill_len - 1) * row), row)?;
        let logits = gpu.alloc(TALKER_VOCAB * 2)?;
        gpu.linear_bf16(logits.ptr, past.ptr, self.codec_head.ptr, 1, HIDDEN, TALKER_VOCAB, 0.0)?;
        let ban0 = if cfg.min_new_tokens > 0 { CODEC_EOS as i32 } else { -1 };
        self.sample_dev(gpu, logits.ptr, TALKER_VOCAB, &cfg.talker, self.g_cur.ptr, suppress, ban0, seen.ptr, rep, cfg.seed, 0, eos.ptr, 0, 0)?;
        gpu.copy(self.g_past.ptr, past.ptr, row)?;
        gpu.write(self.g_rng.ptr, &[cfg.seed, 0u64])?;
        let key = (cfg.predictor.greedy, cfg.predictor.temperature.to_bits(), cfg.predictor.top_k);
        let mut graph = self.pred_graph.lock().unwrap();
        if graph.as_ref().map(|g| g.0) != Some(key) {
            *graph = None;
            let g = gpu.capture(|| self.record_predictor(gpu, &cfg.predictor))?;
            *graph = Some((key, g));
            // Capturing does not execute; restore the inputs it was recorded against.
            gpu.write(self.g_rng.ptr, &[cfg.seed, 0u64])?;
        }

        let step_in = gpu.alloc(row)?;
        let mut produced = 0usize;
        let mut handed = 0usize;
        // Frames [handed, upto) are final: never past EOS (it is not a frame) or what has been produced.
        let mut hand = |upto: usize, handed: &mut usize| -> Result<()> {
            if upto > *handed {
                let raw: Vec<i32> = gpu.read(code(*handed, 0), (upto - *handed) * NUM_CODE_GROUPS)?;
                let frames: Vec<[u32; NUM_CODE_GROUPS]> = raw.chunks_exact(NUM_CODE_GROUPS).map(|c| std::array::from_fn(|i| c[i] as u32)).collect();
                on_frames(&frames)?;
                *handed = upto;
            }
            Ok(())
        };
        for step in 0..max_frames {
            // ---- code predictor: codebooks 1..15 of frame `step` (one graph replay)
            gpu.replay(&graph.as_ref().unwrap().1)?;
            gpu.copy(code(step, 0), self.g_cur.ptr, NUM_CODE_GROUPS * 4)?;
            produced = step + 1;
            // ---- talker step: sum of the 16 codebook embeddings + trailing text (or tts_pad)
            let extra = if step < prompt.trailing_len { prompt.trailing.at(step * row) } else { prompt.tts_pad.ptr };
            gpu.launch1("codec_sum_bf16", HIDDEN, &[Arg::P(step_in.ptr), Arg::P(tables.ptr), Arg::P(self.g_cur.ptr), Arg::I(NUM_CODE_GROUPS as i32), Arg::I(HIDDEN as i32), Arg::P(extra)])?;
            let h = self.stack.forward(gpu, step_in.ptr, 1, prompt.prefill_len + step)?;
            gpu.linear_bf16(logits.ptr, h.ptr, self.codec_head.ptr, 1, HIDDEN, TALKER_VOCAB, 0.0)?;
            let ban = if step + 1 < cfg.min_new_tokens { CODEC_EOS as i32 } else { -1 };
            self.sample_dev(gpu, logits.ptr, TALKER_VOCAB, &cfg.talker, self.g_cur.ptr, suppress, ban, seen.ptr, rep, cfg.seed, (step as u64 + 1) << 8, eos.ptr, (step + 1) as i32, 0)?;
            gpu.copy(self.g_past.ptr, h.ptr, row)?;
            if (step + 1) % POLL == 0 || step + 1 == max_frames {
                let eos_at = gpu.read::<i32>(eos.ptr, 1)?[0];
                if eos_at != i32::MAX { break; }
                hand(produced, &mut handed)?;
            }
        }
        let eos_at = gpu.read::<i32>(eos.ptr, 1)?[0];
        let count = if eos_at == i32::MAX { produced } else { (eos_at as usize).min(produced) };
        hand(count, &mut handed)
    }
}

/// Round to nearest even.
pub fn f32_to_bf16(v: f32) -> u16 {
    let b = v.to_bits();
    ((b + 0x7fff + ((b >> 16) & 1)) >> 16) as u16
}

pub fn bf16_to_f32(h: u16) -> f32 {
    f32::from_bits((h as u32) << 16)
}
