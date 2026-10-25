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
    /// Last: fields drop in order, and the device buffers above are freed on this GPU's stream. With the GPU dropped
    /// first, those frees failed silently and a dropped engine kept its whole model in device memory.
    pub gpu: Gpu,
}

pub struct Speech {
    pub samples: Vec<f32>,
    pub frames: usize,
    pub gen_seconds: f64,
    /// Of gen_seconds, the time spent decoding codes to audio (streaming only; 0 otherwise).
    pub codec_seconds: f64,
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

/// The Base model sizes ok-tts runs (voice cloning from a speaker embedding). 1.7B is the default: its voices are
/// closer to each heroine's own (TTS plan 20.5); 0.6B takes about 1 GB less VRAM.
pub struct BaseSize {
    pub label: &'static str,
    pub repo: crate::hfget::Repo,
}

pub const BASE_SIZES: [BaseSize; 2] = [
    BaseSize { label: "1.7B", repo: crate::hfget::Repo { id: "Qwen/Qwen3-TTS-12Hz-1.7B-Base", rev: "fd4b254389122332181a7c3db7f27e918eec64e3" } },
    BaseSize { label: "0.6B", repo: crate::hfget::Repo { id: "Qwen/Qwen3-TTS-12Hz-0.6B-Base", rev: "5d83992436eae1d760afd27aff78a71d676296fc" } },
];

/// `--base-size`, else OK_TTS_BASE_SIZE, else 1.7B.
pub fn base_size(arg: Option<&str>) -> Result<&'static BaseSize> {
    let want = arg.map(str::to_string).or_else(|| std::env::var("OK_TTS_BASE_SIZE").ok()).filter(|s| !s.trim().is_empty()).unwrap_or_else(|| "1.7B".into());
    BASE_SIZES.iter().find(|s| s.label.eq_ignore_ascii_case(want.trim())).ok_or_else(|| anyhow!("base size must be 1.7B or 0.6B, not {want}"))
}

/// Where a Base model is downloaded when it is nowhere on this machine: <exe>/models/<name> (not versioned).
pub fn base_download_dir(size: &BaseSize) -> PathBuf {
    crate::exe_dir().join("models").join(size.repo.name())
}

/// A Base model already on this machine: OK_TTS_BASE_MODEL (an explicit folder, any size), the downloaded copy
/// (<exe>/models), or a Hugging Face cache (HF_HUB_CACHE, HF_HOME/hub, ~/.cache/huggingface/hub, E:\TTS\hf-cache\hub).
pub fn find_base_model(size: &BaseSize) -> Option<PathBuf> {
    let snapshot = |root: PathBuf| -> Option<PathBuf> {
        if root.join("model.safetensors").is_file() {
            return Some(root);
        }
        std::fs::read_dir(&root).ok()?.flatten().map(|e| e.path()).find(|p| p.join("model.safetensors").is_file())
    };
    if let Some(p) = std::env::var("OK_TTS_BASE_MODEL").ok().filter(|p| !p.trim().is_empty()).and_then(|p| snapshot(PathBuf::from(p))) {
        return Some(p);
    }
    let own = base_download_dir(size);
    if own.join(crate::hfget::MARKER).is_file() {
        return Some(own);
    }
    let cache_name = format!("models--{}", size.repo.id.replace('/', "--"));
    let env = |k: &str| std::env::var(k).ok().filter(|v| !v.trim().is_empty()).map(PathBuf::from);
    let caches = [env("HF_HUB_CACHE"), env("HF_HOME").map(|h| h.join("hub")), env("USERPROFILE").map(|h| h.join(".cache/huggingface/hub")), Some(PathBuf::from(r"E:\TTS\hf-cache\hub"))];
    caches.into_iter().flatten().find_map(|c| snapshot(c.join(&cache_name).join("snapshots")))
}

/// The default Base model (OK_TTS_BASE_SIZE or 1.7B) if it is on this machine (tools; the server downloads it).
pub fn default_base_model_dir() -> Option<PathBuf> {
    find_base_model(base_size(None).ok()?)
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
    /// English one). Base (profiles set): `speaker` (a character's own voice) or else the fixed speaker embedding of the
    /// voice's profile, no description. VoiceDesign ignores `speaker`.
    fn prompt(&self, text: &str, personality: &str, age: u32, speaker: Option<&[f32]>, cfg: &mut GenConfig) -> Result<(Prompt, VoiceChoice)> {
        let voice = self.voices.choose(personality, age);
        // One seed per voice, not per line: VoiceDesign samples the timbre too, so a per-line seed made her voice drift.
        cfg.seed = voice.seed;
        let language = language_of(text);
        let assistant_ids = self.tokenizer.assistant(text)?;
        let prompt = match &self.profiles {
            Some(_) if speaker.is_some() => self.talker.build_prompt(&self.gpu, &[], &assistant_ids, language, speaker)?,
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
    pub fn speak(&self, text: &str, personality: &str, age: u32, cfg: GenConfig) -> Result<Speech> {
        self.speak_voice(text, personality, age, None, cfg)
    }

    /// `speak` in a given voice (Base: a speaker embedding of the character's own, instead of her design's profile).
    pub fn speak_voice(&self, text: &str, personality: &str, age: u32, speaker: Option<&[f32]>, mut cfg: GenConfig) -> Result<Speech> {
        self.gpu.bind()?;
        let t = Instant::now();
        let (prompt, voice) = self.prompt(text, personality, age, speaker, &mut cfg)?;
        let frames = self.talker.generate(&self.gpu, &prompt, &cfg)?;
        let samples = if frames.is_empty() { Vec::new() } else { self.codec.decode(&self.gpu, &frames)? };
        debug_assert_eq!(samples.len(), frames.len() * SAMPLES_PER_FRAME);
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), codec_seconds: 0.0, voice_key: voice.key, age: voice.age })
    }

    /// VoiceDesign with an explicit description and seed (making candidate voices). A Base engine has no use for it.
    pub fn speak_design(&self, text: &str, instruct: &str, seed: u64, mut cfg: GenConfig) -> Result<Speech> {
        self.gpu.bind()?;
        let t = Instant::now();
        cfg.seed = seed;
        let language = language_of(text);
        let instruct = if language == LANGUAGE_ENGLISH { english_instruct(instruct) } else { instruct.to_string() };
        let instruct_ids = self.tokenizer.instruct(&instruct)?;
        let assistant_ids = self.tokenizer.assistant(text)?;
        let prompt = self.talker.build_prompt(&self.gpu, &instruct_ids, &assistant_ids, language, None)?;
        let budget = self.talker.max_positions().saturating_sub(prompt.prefill_len + 2);
        cfg.max_new_tokens = cfg.max_new_tokens.min(budget);
        let frames = self.talker.generate(&self.gpu, &prompt, &cfg)?;
        let samples = if frames.is_empty() { Vec::new() } else { self.codec.decode(&self.gpu, &frames)? };
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), codec_seconds: 0.0, voice_key: "design".into(), age: 0 })
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
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), codec_seconds: 0.0, voice_key: "clone".into(), age: 0 })
    }

    /// Like `speak`, but audio is handed to `sink` while the rest is still being generated: the first
    /// stream_first_frames frames, then every `later_frames` (default stream_later_frames) more (4 frames = 320 ms
    /// of speech), are decoded with the usual left context.
    /// Returns the whole utterance like `speak` (for the cache file).
    pub fn speak_stream(&self, text: &str, personality: &str, age: u32, speaker: Option<&[f32]>, mut cfg: GenConfig, later_frames: Option<usize>,
                        sink: &mut dyn FnMut(&[f32]) -> Result<()>) -> Result<Speech> {
        self.gpu.bind()?;
        let t = Instant::now();
        let (prompt, voice) = self.prompt(text, personality, age, speaker, &mut cfg)?;
        let mut frames = Vec::new();
        let mut samples = Vec::new();
        let mut decoded = 0usize;
        let mut codec_seconds = 0.0f64;
        self.talker.generate_stream(&self.gpu, &prompt, &cfg, &mut |new| {
            frames.extend_from_slice(new);
            let want = if decoded == 0 { self.stream_first_frames } else { later_frames.unwrap_or(self.stream_later_frames) };
            if frames.len() - decoded >= want.max(1) {
                let c = Instant::now();
                let pcm = self.codec.decode_tail(&self.gpu, &frames, decoded)?;
                codec_seconds += c.elapsed().as_secs_f64();
                decoded = frames.len();
                samples.extend_from_slice(&pcm);
                sink(&pcm)?;
            }
            Ok(())
        })?;
        if decoded < frames.len() {
            let c = Instant::now();
            let pcm = self.codec.decode_tail(&self.gpu, &frames, decoded)?;
            codec_seconds += c.elapsed().as_secs_f64();
            samples.extend_from_slice(&pcm);
            sink(&pcm)?;
        }
        Ok(Speech { samples, frames: frames.len(), gen_seconds: t.elapsed().as_secs_f64(), codec_seconds, voice_key: voice.key, age: voice.age })
    }
}
