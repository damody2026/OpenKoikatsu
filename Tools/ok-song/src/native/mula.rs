//! HeartMuLa (music language model) in Rust + CUDA: a Llama-3.2 3B backbone predicts codebook 0 of each 80 ms frame,
//! a 3-layer Llama decoder predicts codebooks 1..7, both with classifier-free guidance (cond and uncond rows in one
//! batch). Mirrors `heartlib/heartmula/modeling_heartmula.py` + `pipelines/music_generation.py` in bf16.

use std::path::Path;

use anyhow::{bail, Context, Result};

use super::gpu::{Arg, Buf, Gpu};
use super::weights::Shards;

pub const CODEBOOKS: usize = 8;
pub const AUDIO_VOCAB: usize = 8197;
pub const AUDIO_EOS: i32 = 8193;
pub const TEXT_BOS: u32 = 128000;
pub const TEXT_EOS: u32 = 128001;
pub const FRAME_MS: usize = 80;

struct Layer {
    sa_norm: Buf,
    wqkv: Buf,
    wo: Buf,
    mlp_norm: Buf,
    w13: Buf,
    w2: Buf,
}

/// torchtune Llama-3.2 decoder stack (no embeddings / output head) with a KV cache per batch row.
pub struct Llama {
    layers: Vec<Layer>,
    norm: Buf,
    hidden: usize,
    heads: usize,
    kv_heads: usize,
    head_dim: usize,
    inter: usize,
    theta: Buf,
    batch: usize,
    kcache: Vec<Buf>,
    vcache: Vec<Buf>,
    cache_len: usize,
}

/// torchtune `Llama3ScaledRoPE.apply_scaling` (low 1, high 4, old context 8192), in f32 like the buffer it builds.
fn llama3_theta(dim: usize, base: f32, scale: f32) -> Vec<f32> {
    let (low, high, old) = (1.0f32, 4.0f32, 8192.0f32);
    let (low_wl, high_wl) = (old / low, old / high);
    (0..dim / 2)
        .map(|i| {
            let f = 1.0 / base.powf((2 * i) as f32 / dim as f32);
            let wl = 2.0 * std::f32::consts::PI / f;
            if wl < high_wl {
                f
            } else if wl > low_wl {
                f / scale
            } else {
                let smooth = (old / wl - low) / (high - low);
                (1.0 - smooth) * f / scale + smooth * f
            }
        })
        .collect()
}

impl Llama {
    #[allow(clippy::too_many_arguments)]
    fn load(gpu: &Gpu, st: &Shards, prefix: &str, layers: usize, hidden: usize, heads: usize, kv_heads: usize,
            inter: usize, batch: usize, cache_len: usize) -> Result<Self> {
        let mut out = Vec::new();
        for i in 0..layers {
            let p = format!("{prefix}.layers.{i}");
            out.push(Layer {
                sa_norm: st.bf16_one(gpu, &format!("{p}.sa_norm.scale"))?,
                wqkv: st.bf16(gpu, &[format!("{p}.attn.q_proj.weight"), format!("{p}.attn.k_proj.weight"), format!("{p}.attn.v_proj.weight")])?,
                wo: st.bf16_one(gpu, &format!("{p}.attn.output_proj.weight"))?,
                mlp_norm: st.bf16_one(gpu, &format!("{p}.mlp_norm.scale"))?,
                w13: st.bf16(gpu, &[format!("{p}.mlp.w1.weight"), format!("{p}.mlp.w3.weight")])?,
                w2: st.bf16_one(gpu, &format!("{p}.mlp.w2.weight"))?,
            });
        }
        let head_dim = hidden / heads;
        let theta = gpu.upload(&llama3_theta(head_dim, 500_000.0, 32.0))?;
        let mut kcache = Vec::new();
        let mut vcache = Vec::new();
        let bytes = batch * kv_heads * cache_len * head_dim * 2;
        for _ in 0..layers {
            kcache.push(gpu.zeros(bytes)?);
            vcache.push(gpu.zeros(bytes)?);
        }
        Ok(Self { layers: out, norm: st.bf16_one(gpu, &format!("{prefix}.norm.scale"))?, hidden, heads, kv_heads, head_dim, inter,
                  theta, batch, kcache, vcache, cache_len })
    }

    /// `x` is [batch * n, hidden] (batch-major), positions pos0..pos0+n. Returns the final-normed states.
    /// `probe(layer, resid)` sees each layer's output (the residual stream) when given.
    pub fn forward(&self, gpu: &Gpu, x: u64, n: usize, pos0: usize, mut probe: Option<&mut dyn FnMut(usize, u64) -> Result<()>>) -> Result<Buf> {
        if pos0 + n > self.cache_len {
            bail!("sequence too long: {} > {}", pos0 + n, self.cache_len);
        }
        let (h, hd, nh, kvh, b) = (self.hidden, self.head_dim, self.heads, self.kv_heads, self.batch);
        let rows = b * n;
        let qkv_dim = (nh + 2 * kvh) * hd;
        let resid = gpu.alloc(rows * h * 2)?;
        gpu.copy(resid.ptr, x, rows * h * 2)?;
        let normed = gpu.alloc(rows * h * 2)?;
        let qkv = gpu.alloc(rows * qkv_dim * 2)?;
        let attn = gpu.alloc(rows * nh * hd * 2)?;
        let proj = gpu.alloc(rows * h * 2)?;
        let gu = gpu.alloc(rows * 2 * self.inter * 2)?;
        let act = gpu.alloc(rows * self.inter * 2)?;
        let elems = (rows * h) as i32;
        let eps = 1e-5f32;
        let cache_stride = (kvh * self.cache_len * hd * 2) as u64;
        for (li, l) in self.layers.iter().enumerate() {
            gpu.launch("rmsnorm_bf16", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(resid.ptr), Arg::P(l.sa_norm.ptr), Arg::I(h as i32), Arg::F(eps)])?;
            gpu.linear_bf16(qkv.ptr, normed.ptr, l.wqkv.ptr, rows, h, qkv_dim, 0.0)?;
            for bi in 0..b {
                let q = qkv.ptr + (bi * n * qkv_dim * 2) as u64;
                let kc = self.kcache[li].ptr + bi as u64 * cache_stride;
                let vc = self.vcache[li].ptr + bi as u64 * cache_stride;
                let half = (hd / 2) as u32;
                gpu.launch("rope_il_bf16", (n as u32, nh as u32, 1), (half, 1, 1), 0, &[
                    Arg::P(q), Arg::I(qkv_dim as i32), Arg::I(0), Arg::I(hd as i32), Arg::P(self.theta.ptr), Arg::I(pos0 as i32), Arg::P(0), Arg::I(self.cache_len as i32)])?;
                gpu.launch("rope_il_bf16", (n as u32, kvh as u32, 1), (half, 1, 1), 0, &[
                    Arg::P(q), Arg::I(qkv_dim as i32), Arg::I((nh * hd) as i32), Arg::I(hd as i32), Arg::P(self.theta.ptr), Arg::I(pos0 as i32), Arg::P(kc), Arg::I(self.cache_len as i32)])?;
                gpu.launch("store_v_bf16", (n as u32, kvh as u32, 1), (128, 1, 1), 0, &[
                    Arg::P(q), Arg::I(qkv_dim as i32), Arg::I(((nh + kvh) * hd) as i32), Arg::I(hd as i32), Arg::I(pos0 as i32), Arg::P(vc), Arg::I(self.cache_len as i32)])?;
                let smem = ((hd + pos0 + n) * 4) as u32;
                gpu.launch("attention_bf16", (n as u32, nh as u32, 1), (128, 1, 1), smem, &[
                    Arg::P(attn.ptr + (bi * n * nh * hd * 2) as u64), Arg::P(q), Arg::I(qkv_dim as i32), Arg::P(kc), Arg::P(vc),
                    Arg::I(nh as i32), Arg::I(kvh as i32), Arg::I(hd as i32), Arg::I(self.cache_len as i32), Arg::I(pos0 as i32), Arg::I(0),
                    Arg::F(1.0 / (hd as f32).sqrt())])?;
            }
            gpu.linear_bf16(proj.ptr, attn.ptr, l.wo.ptr, rows, nh * hd, h, 0.0)?;
            gpu.launch1("add_bf16", rows * h, &[Arg::P(resid.ptr), Arg::P(resid.ptr), Arg::P(proj.ptr), Arg::I(elems)])?;
            gpu.launch("rmsnorm_bf16", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(normed.ptr), Arg::P(resid.ptr), Arg::P(l.mlp_norm.ptr), Arg::I(h as i32), Arg::F(eps)])?;
            gpu.linear_bf16(gu.ptr, normed.ptr, l.w13.ptr, rows, h, 2 * self.inter, 0.0)?;
            gpu.launch1("silu_mul_bf16", rows * self.inter, &[Arg::P(act.ptr), Arg::P(gu.ptr), Arg::I(self.inter as i32), Arg::I(rows as i32)])?;
            gpu.linear_bf16(proj.ptr, act.ptr, l.w2.ptr, rows, self.inter, h, 0.0)?;
            gpu.launch1("add_bf16", rows * h, &[Arg::P(resid.ptr), Arg::P(resid.ptr), Arg::P(proj.ptr), Arg::I(elems)])?;
            if let Some(p) = probe.as_mut() {
                p(li, resid.ptr)?;
            }
        }
        let out = gpu.alloc(rows * h * 2)?;
        gpu.launch("rmsnorm_bf16", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(resid.ptr), Arg::P(self.norm.ptr), Arg::I(h as i32), Arg::F(eps)])?;
        Ok(out)
    }
}

pub struct Sampling {
    pub cfg: f32,
    pub temperature: f32,
    pub top_k: i32,
    pub seed: u64,
}

pub struct Mula {
    st: Shards,
    pub backbone: Llama,
    pub decoder: Llama,
    audio_emb: Buf,
    projection: Buf,
    c0_head: Buf,
    audio_head: Buf,
    hidden: usize,
    batch: usize,
    tokenizer: tokenizers::Tokenizer,
}

/// The prompt: tags, one MuQ slot (zero reference), lyrics; all as text-column tokens.
pub struct Prompt {
    pub tags: Vec<u32>,
    pub lyrics: Vec<u32>,
}

impl Prompt {
    pub fn len(&self) -> usize {
        self.tags.len() + 1 + self.lyrics.len()
    }
}

impl Mula {
    /// `ckpt` is the HeartMuLa `ckpt` folder (`HeartMuLa-oss-3B/`, `tokenizer.json`). `max_frames` sizes the KV cache.
    pub fn load(gpu: &Gpu, ckpt: &Path, cfg: bool, max_frames: usize, max_prompt: usize) -> Result<Self> {
        let st = Shards::open(&ckpt.join("HeartMuLa-oss-3B"))?;
        let tokenizer = tokenizers::Tokenizer::from_file(ckpt.join("tokenizer.json")).map_err(|e| anyhow::anyhow!("tokenizer: {e}"))?;
        let batch = if cfg { 2 } else { 1 };
        let backbone = Llama::load(gpu, &st, "backbone", 28, 3072, 24, 8, 8192, batch, max_prompt + max_frames + 1)?;
        let decoder = Llama::load(gpu, &st, "decoder", 3, 3072, 8, 4, 8192, batch, CODEBOOKS)?;
        Ok(Self {
            audio_emb: st.bf16_one(gpu, "audio_embeddings.weight")?,
            projection: st.bf16_one(gpu, "projection.weight")?,
            c0_head: st.bf16_one(gpu, "codebook0_head.weight")?,
            audio_head: st.bf16_transposed_blocks(gpu, "audio_head")?,
            hidden: 3072,
            batch,
            backbone,
            decoder,
            st,
            tokenizer,
        })
    }

    fn encode(&self, text: &str) -> Result<Vec<u32>> {
        let mut ids = self.tokenizer.encode(text, true).map_err(|e| anyhow::anyhow!("tokenize: {e}"))?.get_ids().to_vec();
        if ids.first() != Some(&TEXT_BOS) {
            ids.insert(0, TEXT_BOS);
        }
        if ids.last() != Some(&TEXT_EOS) {
            ids.push(TEXT_EOS);
        }
        Ok(ids)
    }

    /// Tags are lowercased and wrapped in `<tag>…</tag>`; lyrics are lowercased (as the upstream pipeline does).
    pub fn prompt(&self, tags: &str, lyrics: &str) -> Result<Prompt> {
        let mut t = tags.to_lowercase();
        if !t.starts_with("<tag>") {
            t = format!("<tag>{t}");
        }
        if !t.ends_with("</tag>") {
            t = format!("{t}</tag>");
        }
        Ok(Prompt { tags: self.encode(&t)?, lyrics: self.encode(&lyrics.to_lowercase())? })
    }

    /// Backbone input for the prompt, [batch * len, hidden] bf16 bits. Row 0 is conditional; row 1 (CFG) uses the
    /// unconditional embedding at every position.
    pub fn prompt_embeddings(&self, p: &Prompt) -> Result<Vec<u16>> {
        let h = self.hidden;
        let len = p.len();
        let mut out = Vec::with_capacity(self.batch * len * h);
        // muq_linear(zero reference) is its bias, rounded to bf16.
        let muq: Vec<u16> = self.st.f32("muq_linear.bias")?.into_iter().map(super::weights::bf16_bits).collect();
        for id in &p.tags {
            out.extend(self.st.row_bf16("text_embeddings.weight", *id as usize)?);
        }
        out.extend(muq);
        for id in &p.lyrics {
            out.extend(self.st.row_bf16("text_embeddings.weight", *id as usize)?);
        }
        if self.batch == 2 {
            let u = self.st.row_bf16("unconditional_text_embedding.weight", 0)?;
            for _ in 0..len {
                out.extend_from_slice(&u);
            }
        }
        Ok(out)
    }

    /// Codebook-0 logits [batch, vocab] for the backbone's last states `last` [batch, hidden].
    fn c0_logits(&self, gpu: &Gpu, last: u64) -> Result<Buf> {
        let logits = gpu.alloc(self.batch * AUDIO_VOCAB * 2)?;
        gpu.linear_bf16(logits.ptr, last, self.c0_head.ptr, self.batch, self.hidden, AUDIO_VOCAB, 0.0)?;
        Ok(logits)
    }

    /// Codebooks 1..7 from the backbone's last states, writing tokens[1..8]; tokens[0] already holds codebook 0.
    fn decode_frame(&self, gpu: &Gpu, last: u64, tokens: u64, s: &Sampling, rng: u64, stop: u64, mut trace: Option<&mut Vec<Vec<f32>>>,
                    force: Option<&[i32]>) -> Result<()> {
        let (h, b) = (self.hidden, self.batch);
        // [batch, 2, hidden]: last state then the codebook-0 embedding.
        let cur = gpu.alloc(b * 2 * h * 2)?;
        for bi in 0..b {
            gpu.copy(cur.ptr + (bi * 2 * h * 2) as u64, last + (bi * h * 2) as u64, h * 2)?;
        }
        let emb = gpu.alloc(b * h * 2)?;
        gpu.launch1("embed_one_bf16", b * h, &[Arg::P(emb.ptr), Arg::P(self.audio_emb.ptr), Arg::P(tokens), Arg::I(h as i32), Arg::I(b as i32)])?;
        for bi in 0..b {
            gpu.copy(cur.ptr + ((bi * 2 + 1) * h * 2) as u64, emb.ptr + (bi * h * 2) as u64, h * 2)?;
        }
        let proj = gpu.alloc(b * 2 * h * 2)?;
        let lastd = gpu.alloc(b * h * 2)?;
        let logits = gpu.alloc(b * AUDIO_VOCAB * 2)?;
        for i in 1..CODEBOOKS {
            let (input, n, pos0) = if i == 1 { (cur.ptr, 2, 0) } else { (emb.ptr, 1, i) };
            gpu.linear_bf16(proj.ptr, input, self.projection.ptr, b * n, h, h, 0.0)?;
            let out = self.decoder.forward(gpu, proj.ptr, n, pos0, None)?;
            for bi in 0..b {
                gpu.copy(lastd.ptr + (bi * h * 2) as u64, out.ptr + ((bi * n + n - 1) * h * 2) as u64, h * 2)?;
            }
            let head = self.audio_head.ptr + ((i - 1) * AUDIO_VOCAB * h * 2) as u64;
            gpu.linear_bf16(logits.ptr, lastd.ptr, head, b, h, AUDIO_VOCAB, 0.0)?;
            if let Some(t) = trace.as_mut() {
                t.push(read_bf16(gpu, logits.ptr, b * AUDIO_VOCAB)?);
            }
            let tok = tokens + (i * 4) as u64;
            gpu.launch("sample_cfg_bf16", (1, 1, 1), (1024, 1, 1), 0, &[
                Arg::P(logits.ptr), Arg::I(b as i32), Arg::I(AUDIO_VOCAB as i32), Arg::F(s.cfg), Arg::F(s.temperature), Arg::I(s.top_k),
                Arg::P(rng), Arg::I(i as i32), Arg::P(tok), Arg::I(AUDIO_EOS), Arg::P(stop)])?;
            if let Some(f) = force {
                // Parity checks: remember our pick, continue with the reference token.
                let mine: Vec<i32> = gpu.read(tok, 1)?;
                if let Some(t) = trace.as_mut() {
                    t.push(vec![mine[0] as f32]);
                }
                gpu.write(tok, &f[i..i + 1])?;
            }
            let table = self.audio_emb.ptr + (i * AUDIO_VOCAB * h * 2) as u64;
            gpu.launch1("embed_one_bf16", b * h, &[Arg::P(emb.ptr), Arg::P(table), Arg::P(tok), Arg::I(h as i32), Arg::I(b as i32)])?;
        }
        Ok(())
    }

    /// Generate up to `max_frames` frames (80 ms each); stops at the end-of-audio token. `cancel` is polled per frame.
    pub fn generate(&self, gpu: &Gpu, p: &Prompt, max_frames: usize, s: &Sampling, cancel: &dyn Fn() -> bool,
                    mut on_frame: impl FnMut(usize)) -> Result<Vec<[i32; CODEBOOKS]>> {
        let (h, b) = (self.hidden, self.batch);
        let len = p.len();
        let x = gpu.upload(&self.prompt_embeddings(p)?)?;
        let tokens = gpu.zeros(CODEBOOKS * 4)?;
        let stop = gpu.zeros(4)?;
        let rng = gpu.upload(&[s.seed, 0u64])?;
        let last = gpu.alloc(b * h * 2)?;
        // The next frame's input: the frame's audio embeddings summed (text column masked out), same for both CFG rows.
        let next = gpu.alloc(b * h * 2)?;
        let mut frames = Vec::new();
        let mut n = len;
        let mut pos0 = 0;
        for f in 0..max_frames {
            if cancel() {
                bail!("cancelled");
            }
            let input = if f == 0 { x.ptr } else { next.ptr };
            // One frame (backbone step, codebook 0, decoder for codebooks 1..7, next input) is recorded as a CUDA graph
            // and submitted at once: ~900 small kernels launched one by one crawl while a game keeps the GPU busy.
            let tc = std::time::Instant::now();
            let graph = gpu.capture(|| {
                let out = self.backbone.forward(gpu, input, n, pos0, None)?;
                for bi in 0..b {
                    gpu.copy(last.ptr + (bi * h * 2) as u64, out.ptr + ((bi * n + n - 1) * h * 2) as u64, h * 2)?;
                }
                let logits = self.c0_logits(gpu, last.ptr)?;
                gpu.launch("sample_cfg_bf16", (1, 1, 1), (1024, 1, 1), 0, &[
                    Arg::P(logits.ptr), Arg::I(b as i32), Arg::I(AUDIO_VOCAB as i32), Arg::F(s.cfg), Arg::F(s.temperature), Arg::I(s.top_k),
                    Arg::P(rng.ptr), Arg::I(0), Arg::P(tokens.ptr), Arg::I(AUDIO_EOS), Arg::P(stop.ptr)])?;
                self.decode_frame(gpu, last.ptr, tokens.ptr, s, rng.ptr, stop.ptr, None, None)?;
                gpu.launch("inc_u64", (1, 1, 1), (32, 1, 1), 0, &[Arg::P(rng.ptr + 8)])?;
                gpu.launch1("audio_sum_bf16", h, &[Arg::P(next.ptr), Arg::P(self.audio_emb.ptr), Arg::P(tokens.ptr), Arg::I(CODEBOOKS as i32),
                    Arg::I(AUDIO_VOCAB as i32), Arg::I(h as i32), Arg::I(b as i32)])?;
                Ok(())
            })?;
            let tr = std::time::Instant::now();
            gpu.replay(&graph)?;
            let t: Vec<i32> = gpu.read(tokens.ptr, CODEBOOKS)?;
            if std::env::var_os("OK_SONG_PROFILE").is_some() && f % 10 == 1 {
                crate::log!("ok-song: frame {f}: record+instantiate {:.1} ms, run {:.1} ms", (tr - tc).as_secs_f64() * 1e3, tr.elapsed().as_secs_f64() * 1e3);
            }
            drop(graph);
            if t.iter().any(|&v| v >= AUDIO_EOS) {
                break;
            }
            frames.push(t.clone().try_into().unwrap());
            on_frame(f + 1);
            pos0 += n;
            n = 1;
        }
        Ok(frames)
    }

    /// Parity check against a Python dump (see Docs/HEROINE_SINGING_VALIDATION.md): returns a report.
    pub fn check(&self, gpu: &Gpu, dir: &Path, frames: usize) -> Result<serde_json::Value> {
        let meta: serde_json::Value = serde_json::from_slice(&std::fs::read(dir.join("prompt.json"))?)?;
        let ids = |k: &str| -> Vec<u32> { meta[k].as_array().unwrap().iter().map(|v| v.as_u64().unwrap() as u32).collect() };
        let cfg = meta["cfg"].as_f64().unwrap_or(1.5) as f32;
        let mine = self.prompt(meta["tags"].as_str().unwrap(), meta["lyrics"].as_str().unwrap())?;
        let p = Prompt { tags: ids("tags_ids"), lyrics: ids("lyrics_ids") };
        let mut report = serde_json::Map::new();
        report.insert("tokenizer_match".into(), (mine.tags == p.tags && mine.lyrics == p.lyrics).into());
        let (h, b) = (self.hidden, self.batch);
        let len = p.len();
        let emb = self.prompt_embeddings(&p)?;
        report.insert("bb_in".into(), compare(&emb.iter().map(|v| super::weights::bf16_value(*v)).collect::<Vec<_>>(), &npy(&dir.join("bb_in.npy"))?));
        let x = gpu.upload(&emb)?;
        let mut layers = Vec::new();
        let mut probe = |li: usize, ptr: u64| -> Result<()> {
            let refv = npy(&dir.join(format!("bb_layer_{li:02}.npy")))?;
            layers.push(compare(&read_bf16(gpu, ptr, b * len * h)?, &refv));
            Ok(())
        };
        let out = self.backbone.forward(gpu, x.ptr, len, 0, Some(&mut probe))?;
        report.insert("bb_layers".into(), layers.into());
        report.insert("bb_out_0".into(), compare(&read_bf16(gpu, out.ptr, b * len * h)?, &npy(&dir.join("bb_out_0.npy"))?));
        // Greedy frames with the reference's tokens fed back (teacher forcing), comparing c0 logits each frame.
        let refs: serde_json::Value = serde_json::from_slice(&std::fs::read(dir.join("frames.json"))?)?;
        let ref_frames: Vec<Vec<i32>> = refs["frames"].as_array().unwrap().iter()
            .map(|f| f.as_array().unwrap().iter().map(|v| v.as_i64().unwrap() as i32).collect()).collect();
        let c0_ref = npy(&dir.join("c0_logits.npy"))?;
        let s = Sampling { cfg, temperature: 1.0, top_k: 1, seed: 0 };
        let tokens = gpu.zeros(CODEBOOKS * 4)?;
        let stop = gpu.zeros(4)?;
        let rng = gpu.upload(&[0u64, 0u64])?;
        let last = gpu.alloc(b * h * 2)?;
        let mut out = out;
        let mut n = len;
        let mut pos0 = 0;
        let mut rows = Vec::new();
        let mut dec_trace = Vec::new();
        for f in 0..frames.min(ref_frames.len()) {
            for bi in 0..b {
                gpu.copy(last.ptr + (bi * h * 2) as u64, out.ptr + ((bi * n + n - 1) * h * 2) as u64, h * 2)?;
            }
            let logits = self.c0_logits(gpu, last.ptr)?;
            let c0 = compare(&read_bf16(gpu, logits.ptr, b * AUDIO_VOCAB)?, &c0_ref[f * b * AUDIO_VOCAB..(f + 1) * b * AUDIO_VOCAB]);
            gpu.launch("sample_cfg_bf16", (1, 1, 1), (1024, 1, 1), 0, &[
                Arg::P(logits.ptr), Arg::I(b as i32), Arg::I(AUDIO_VOCAB as i32), Arg::F(s.cfg), Arg::F(1.0), Arg::I(1),
                Arg::P(rng.ptr), Arg::I(0), Arg::P(tokens.ptr), Arg::I(AUDIO_EOS), Arg::P(stop.ptr)])?;
            // Teacher forcing: the decoder continues with the reference tokens even where our argmax differs.
            let mine0: Vec<i32> = gpu.read(tokens.ptr, 1)?;
            gpu.write(tokens.ptr, &ref_frames[f][..1])?;
            let mut tr = Vec::new();
            self.decode_frame(gpu, last.ptr, tokens.ptr, &s, rng.ptr, stop.ptr, Some(&mut tr), Some(&ref_frames[f]))?;
            // tr: [logits, pick] per codebook. Margin = guided(our best) - guided(reference token), 0 when equal.
            let mut picks = vec![mine0[0]];
            let mut margins = vec![guided_margin(&read_bf16(gpu, logits.ptr, b * AUDIO_VOCAB)?, b, cfg, ref_frames[f][0])];
            for k in 0..CODEBOOKS - 1 {
                picks.push(tr[2 * k + 1][0] as i32);
                margins.push(guided_margin(&tr[2 * k], b, cfg, ref_frames[f][k + 1]));
            }
            let matched = picks.iter().zip(&ref_frames[f]).filter(|(a, b)| a == b).count();
            if f == 0 {
                dec_trace = tr;
            }
            rows.push(serde_json::json!({"frame": f, "c0_logits": c0, "tokens_matched": matched, "mine": picks, "ref": ref_frames[f], "margins": margins}));
            gpu.write(tokens.ptr, &ref_frames[f])?;
            let next = gpu.alloc(b * h * 2)?;
            gpu.launch1("audio_sum_bf16", h, &[Arg::P(next.ptr), Arg::P(self.audio_emb.ptr), Arg::P(tokens.ptr), Arg::I(CODEBOOKS as i32),
                Arg::I(AUDIO_VOCAB as i32), Arg::I(h as i32), Arg::I(b as i32)])?;
            pos0 += n;
            n = 1;
            out = self.backbone.forward(gpu, next.ptr, 1, pos0, None)?;
        }
        report.insert("frames".into(), rows.into());
        report.insert("decoder_steps".into(), (dec_trace.len() / 2).into());
        Ok(report.into())
    }
}

/// How far the reference token is below our best guided logit (bf16 guidance as in the kernel).
fn guided_margin(logits: &[f32], b: usize, cfg: f32, ref_tok: i32) -> f32 {
    let rb = |x: f32| super::weights::bf16_value(super::weights::bf16_bits(x));
    let g = |i: usize| if b == 2 { let (c, u) = (logits[i], logits[AUDIO_VOCAB + i]); rb(u + rb(rb(c - u) * cfg)) } else { logits[i] };
    let best = (0..AUDIO_VOCAB).map(g).fold(f32::NEG_INFINITY, f32::max);
    best - g(ref_tok as usize)
}

pub fn read_bf16(gpu: &Gpu, ptr: u64, n: usize) -> Result<Vec<f32>> {
    let v: Vec<u16> = gpu.read(ptr, n)?;
    Ok(v.into_iter().map(super::weights::bf16_value).collect())
}

/// Max abs error, reference scale and cosine similarity.
pub fn compare(mine: &[f32], refv: &[f32]) -> serde_json::Value {
    if mine.len() != refv.len() {
        return serde_json::json!({"error": format!("length {} vs {}", mine.len(), refv.len())});
    }
    let (mut maxe, mut maxr, mut dot, mut na, mut nb) = (0f64, 0f64, 0f64, 0f64, 0f64);
    for (a, b) in mine.iter().zip(refv) {
        let (a, b) = (*a as f64, *b as f64);
        maxe = maxe.max((a - b).abs());
        maxr = maxr.max(b.abs());
        dot += a * b;
        na += a * a;
        nb += b * b;
    }
    serde_json::json!({"max_abs_err": maxe, "ref_max": maxr, "cos": dot / (na.sqrt() * nb.sqrt()).max(1e-30)})
}

/// Minimal .npy reader (little-endian f4, C order).
pub fn npy(path: &Path) -> Result<Vec<f32>> {
    let b = std::fs::read(path).with_context(|| format!("read {}", path.display()))?;
    if &b[..6] != b"\x93NUMPY" {
        bail!("{}: not npy", path.display());
    }
    let (hl, start) = if b[6] == 1 { (u16::from_le_bytes([b[8], b[9]]) as usize, 10) } else { (u32::from_le_bytes([b[8], b[9], b[10], b[11]]) as usize, 12) };
    let header = std::str::from_utf8(&b[start..start + hl])?;
    if header.contains("'fortran_order': True") {
        bail!("{}: unsupported header {header}", path.display());
    }
    let body = &b[start + hl..];
    if header.contains("'<f8'") {
        return Ok(body.chunks_exact(8).map(|c| f64::from_le_bytes(c.try_into().unwrap()) as f32).collect());
    }
    if !header.contains("'<f4'") {
        bail!("{}: unsupported header {header}", path.display());
    }
    Ok(body.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect())
}
