//! Model bundle: talker + code predictor + codec decoder + text side.

use std::path::{Path, PathBuf};
use std::time::Instant;

use std::collections::HashMap;

use anyhow::{anyhow, Context, Result};

use crate::codec::{Codec, SAMPLES_PER_FRAME};
use crate::gpu::Gpu;
use crate::profile::{profile_id, Profile};
use crate::talker::{GenConfig, Prompt, Talker};
use crate::text::{english_instruct, language_of, TextTokenizer, VoiceChoice, Voices, LANGUAGE_ENGLISH};
use crate::weights::Store;

pub const SAMPLE_RATE: u32 = 24_000;
pub const MODEL_ID: &str = "Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign";
pub const BASE_MODEL_ID: &str = "Qwen/Qwen3-TTS-12Hz-1.7B-Base";

pub struct Engine {
    pub gpu: Gpu,
    pub talker: Talker,
    pub codec: Codec,
    pub tokenizer: TextTokenizer,
    pub voices: Voices,
    /// Base model: the fixed voice of every voice design (profile id -> profile). None: VoiceDesign.
    pub profiles: Option<HashMap<String, Profile>>,
    /// Frames decoded per streamed piece: the first piece, then each later one. More frames per later piece
    /// decode the left-context frames fewer times; the first stays small so her voice starts soon.
    pub stream_first_frames: usize,
    pub stream_later_frames: usize,
}

pub struct Speech {
    pub samples: Vec<f32>,
    pub frames: usize,
    pub gen_seconds: f64,
    pub voice_key: String,
    pub age: u32,
}

/// Default model location: the Hugging Face cache used by E:\TTS (first snapshot found).
pub fn default_model_dir() -> Option<PathBuf> {
    let roots = [std::env::var("OK_TTS_MODEL").ok().map(PathBuf::from), Some(PathBuf::from(r"E:\TTS\hf-cache\hub\models--Qwen--Qwen3-TTS-12Hz-1.7B-VoiceDesign\snapshots"))];
    for root in roots.into_iter().flatten() {
        if root.join("model.safetensors").is_file() {
            return Some(root);
        }
        if let Ok(entries) = std::fs::read_dir(&root) {
            for e in entries.flatten() {
                if e.path().join("model.safetensors").is_file() {
                    return Some(e.path());
                }
            }
        }
    }
    None
}

/// The Base model (voice cloning from a speaker embedding): OK_TTS_BASE_MODEL or the E:\TTS cache.
pub fn default_base_model_dir() -> Option<PathBuf> {
    let roots = [std::env::var("OK_TTS_BASE_MODEL").ok().map(PathBuf::from), Some(PathBuf::from(r"E:\TTS\hf-cache\hub\models--Qwen--Qwen3-TTS-12Hz-1.7B-Base\snapshots"))];
    for root in roots.into_iter().flatten() {
        if root.join("model.safetensors").is_file() {
            return Some(root);
        }
        if let Ok(entries) = std::fs::read_dir(&root) {
            for e in entries.flatten() {
                if e.path().join("model.safetensors").is_file() {
                    return Some(e.path());
                }
            }
        }
    }
    None
}

impl Engine {
    pub fn load(model_dir: &Path, voices: &Path, max_positions: usize) -> Result<Self> {
        let t = Instant::now();
        let gpu = Gpu::new(0)?;
        let talker_store = Store::open(&model_dir.join("model.safetensors"))?;
        let talker = Talker::load(&gpu, &talker_store, max_positions).context("talker weights")?;
        drop(talker_store);
        let codec_store = Store::open(&model_dir.join("speech_tokenizer").join("model.safetensors"))?;
        let codec = Codec::load(&gpu, &codec_store).context("codec weights")?;
        drop(codec_store);
        let tokenizer = TextTokenizer::load(model_dir)?;
        let voices = Voices::load(voices)?;
        gpu.sync()?;
        crate::log!("ok-tts: model loaded in {:.1}s from {}", t.elapsed().as_secs_f64(), model_dir.display());
        Ok(Self { gpu, talker, codec, tokenizer, voices, profiles: None, stream_first_frames: 4, stream_later_frames: 4 })
    }

    /// The prompt for one line and the voice it is in. VoiceDesign: the voice's description (an English line gets an
    /// English one). Base (profiles set): the voice's fixed speaker embedding, no description.
    fn prompt(&self, text: &str, personality: &str, age: u32, cfg: &mut GenConfig) -> Result<(Prompt, VoiceChoice)> {
        let voice = self.voices.choose(personality, age);
        // One seed per voice, not per line: VoiceDesign samples the timbre too, so a per-line seed made her voice drift.
        cfg.seed = voice.seed;
        let language = language_of(text);
        let assistant_ids = self.tokenizer.assistant(text)?;
        let prompt = match &self.profiles {
            Some(profiles) => {
                let id = profile_id(&voice);
                let p = profiles.get(&id).ok_or_else(|| anyhow!("no voice profile {id} (run ok-tts profile)"))?;
                self.talker.build_prompt(&self.gpu, &[], &assistant_ids, language, Some(&p.embedding))?
            }
            None => {
                let instruct = if language == LANGUAGE_ENGLISH { english_instruct(&voice.instruct) } else { voice.instruct.clone() };
                let instruct_ids = self.tokenizer.instruct(&instruct)?;
                self.talker.build_prompt(&self.gpu, &instruct_ids, &assistant_ids, language, None)?
            }
        };
        let budget = self.talker.max_positions().saturating_sub(prompt.prefill_len + 2);
        cfg.max_new_tokens = cfg.max_new_tokens.min(budget);
        Ok((prompt, voice))
    }

    /// Synthesize one already-cleaned line.
    pub fn speak(&self, text: &str, personality: &str, age: u32, mut cfg: GenConfig) -> Result<Speech> {
        self.gpu.bind()?;
        let t = Instant::now();
        let (prompt, voice) = self.prompt(text, personality, age, &mut cfg)?;
        let frames = self.talker.generate(&self.gpu, &prompt, &cfg)?;
        let samples = if frames.is_empty() { Vec::new() } else { self.codec.decode(&self.gpu, &frames)? };
        debug_assert_eq!(samples.len(), frames.len() * SAMPLES_PER_FRAME);
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), voice_key: voice.key, age: voice.age })
    }

    /// Base model, x-vector clone of any speaker embedding (speaker::SpeakerEncoder), no voice description.
    pub fn speak_clone(&self, text: &str, speaker: &[f32], seed: u64, mut cfg: GenConfig) -> Result<Speech> {
        self.gpu.bind()?;
        let t = Instant::now();
        cfg.seed = seed;
        let language = language_of(text);
        let assistant_ids = self.tokenizer.assistant(text)?;
        let prompt = self.talker.build_prompt(&self.gpu, &[], &assistant_ids, language, Some(speaker))?;
        let budget = self.talker.max_positions().saturating_sub(prompt.prefill_len + 2);
        cfg.max_new_tokens = cfg.max_new_tokens.min(budget);
        let frames = self.talker.generate(&self.gpu, &prompt, &cfg)?;
        let samples = if frames.is_empty() { Vec::new() } else { self.codec.decode(&self.gpu, &frames)? };
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), voice_key: "clone".into(), age: 0 })
    }

    /// Like `speak`, but audio is handed to `sink` while the rest is still being generated: the first
    /// stream_first_frames frames, then every `later_frames` (default stream_later_frames) more (4 frames = 320 ms
    /// of speech), are decoded with the usual left context.
    /// Returns the whole utterance like `speak` (for the cache file).
    pub fn speak_stream(&self, text: &str, personality: &str, age: u32, mut cfg: GenConfig, later_frames: Option<usize>,
                        sink: &mut dyn FnMut(&[f32]) -> Result<()>) -> Result<Speech> {
        self.gpu.bind()?;
        let t = Instant::now();
        let (prompt, voice) = self.prompt(text, personality, age, &mut cfg)?;
        let mut frames = Vec::new();
        let mut samples = Vec::new();
        let mut decoded = 0usize;
        self.talker.generate_stream(&self.gpu, &prompt, &cfg, &mut |new| {
            frames.extend_from_slice(new);
            let want = if decoded == 0 { self.stream_first_frames } else { later_frames.unwrap_or(self.stream_later_frames) };
            if frames.len() - decoded >= want.max(1) {
                let pcm = self.codec.decode_tail(&self.gpu, &frames, decoded)?;
                decoded = frames.len();
                samples.extend_from_slice(&pcm);
                sink(&pcm)?;
            }
            Ok(())
        })?;
        if decoded < frames.len() {
            let pcm = self.codec.decode_tail(&self.gpu, &frames, decoded)?;
            samples.extend_from_slice(&pcm);
            sink(&pcm)?;
        }
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), voice_key: voice.key, age: voice.age })
    }
}
