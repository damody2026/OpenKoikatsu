//! Model bundle: mel front end + audio tower + Qwen3 decoder + tokenizer.

use std::path::{Path, PathBuf};
use std::time::Instant;

use anyhow::{bail, Context, Result};

use crate::decoder::{Decoder, HIDDEN};
use crate::encoder::{Encoder, OUT_DIM};
use crate::gpu::{Arg, Gpu};
use crate::mel::{encoder_tokens, MelFrontEnd, SAMPLE_RATE};
use crate::text::{parse_output, to_display, TextTokenizer};
use crate::weights::Store;

pub const MODEL_ID: &str = "Qwen/Qwen3-ASR-0.6B";
/// Shortest clip worth sending to the model (0.3 s).
pub const MIN_SAMPLES: usize = SAMPLE_RATE as usize * 3 / 10;

pub struct Engine {
    pub gpu: Gpu,
    mel: MelFrontEnd,
    encoder: Encoder,
    decoder: Decoder,
    tokenizer: TextTokenizer,
}

pub struct Transcript {
    /// Display text (Traditional Chinese when the speech is Chinese).
    pub text: String,
    pub language: String,
    pub raw: String,
    pub audio_seconds: f64,
    pub asr_seconds: f64,
}

/// OK_ASR_MODEL, else E:\ASR\Qwen3-ASR-0.6B.
pub fn default_model_dir() -> Option<PathBuf> {
    [std::env::var("OK_ASR_MODEL").ok().map(PathBuf::from), Some(PathBuf::from(r"E:\ASR\Qwen3-ASR-0.6B"))]
        .into_iter()
        .flatten()
        .find(|p| p.join("model.safetensors").is_file())
}

impl Engine {
    pub fn load(model_dir: &Path, max_positions: usize) -> Result<Self> {
        let t = Instant::now();
        let gpu = Gpu::new(0)?;
        let store = Store::open(&model_dir.join("model.safetensors"))?;
        let encoder = Encoder::load(&gpu, &store).context("audio tower weights")?;
        let decoder = Decoder::load(&gpu, &store, max_positions).context("decoder weights")?;
        drop(store);
        let tokenizer = TextTokenizer::load(model_dir)?;
        gpu.sync()?;
        crate::log!("ok-asr: model loaded in {:.1}s from {}", t.elapsed().as_secs_f64(), model_dir.display());
        Ok(Self { gpu, mel: MelFrontEnd::new(), encoder, decoder, tokenizer })
    }

    /// 16 kHz mono samples (a multiple of 160) -> transcript. `language` (e.g. "Chinese") forces text-only output.
    pub fn transcribe(&self, samples: &[f32], language: Option<&str>) -> Result<Transcript> {
        let t = Instant::now();
        let audio_seconds = samples.len() as f64 / SAMPLE_RATE as f64;
        if samples.len() < MIN_SAMPLES {
            bail!("audio is too short ({audio_seconds:.2}s)");
        }
        self.gpu.bind()?;
        let (features, frames) = self.mel.features(samples);
        let tokens = encoder_tokens(frames);
        let (prompt, audio_at) = self.tokenizer.prompt("", tokens, language)?;
        if prompt.len() + 8 > self.decoder.max_positions() {
            bail!("audio is too long ({audio_seconds:.1}s)");
        }
        let (audio_f32, rows) = self.encoder.forward(&self.gpu, &features, frames)?;
        debug_assert_eq!(rows, tokens);
        debug_assert_eq!(OUT_DIM, HIDDEN);
        let audio = self.gpu.alloc(rows * HIDDEN * 2)?;
        self.gpu.launch1("f32_to_bf16", rows * HIDDEN, &[Arg::P(audio.ptr), Arg::P(audio_f32.ptr), Arg::I((rows * HIDDEN) as i32)])?;
        let ids = self.decoder.generate(&self.gpu, &prompt, audio.ptr, audio_at, rows, 256)?;
        let raw = self.tokenizer.decode(&ids);
        let (lang, text) = parse_output(&raw, language);
        Ok(Transcript { text: to_display(&lang, &text), language: lang, raw, audio_seconds, asr_seconds: t.elapsed().as_secs_f64() })
    }
}
