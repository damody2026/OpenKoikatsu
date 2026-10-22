//! Fixed voices. VoiceDesign speaks one reference line once; the Base speaker encoder turns it into a speaker embedding;
//! from then on the Base model speaks every line with that embedding (x-vector clone), so a character keeps one voice
//! across lines instead of VoiceDesign re-imagining the description for each line.
//!
//!   ok-tts profile [--base DIR] [--model VOICEDESIGN_DIR] [--profiles DIR] [--voices voices.json]
//!                  [--keys 08,m0] [--ages 18,21] [--ref-text TEXT] [--force]
//!
//! A voice is one `.okvoice` file (format below): <profiles>/<key>@<age>.okvoice for the default voice of a design
//! (personality + age, or a male tone). The same bytes go into a character card when a character gets a voice of her
//! own. A profile whose design (description, seed) or reference text changed is rebuilt; one the player chose at
//! runtime (source "runtime") is kept unless --force. Profiles are local files beside the exe (Build/ is not
//! versioned). Without --keys every female voice at every age and every male tone is made. Schema 1 profiles
//! (<id>.json + <id>.wav) are converted.
//!
//! `.okvoice` (all integers little-endian):
//!   "OKVOICE1" | u32 meta length | meta JSON (VoiceMeta) | u32 dim | dim x bf16 embedding | u32 OGG length | OGG
//! The embedding is stored as bf16 because that is what the talker takes; the reference clip is OGG Vorbis (24 kHz
//! mono), kept so the voice can be listened to and re-embedded.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};
use sha1_smol::Sha1;

use crate::audio::{encode_ogg,peak_normalize, trim_silence, write_atomic};
use crate::engine::{Engine, SAMPLE_RATE};
use crate::speaker::SpeakerEncoder;
use crate::talker::{bf16_to_f32, f32_to_bf16, GenConfig};
use crate::text::{clean_line, VoiceChoice, Voices};
use crate::weights::Store;

pub const SCHEMA: u32 = 2;
pub const ENCODER: &str = "ok-tts speaker.rs 1";
pub const DEFAULT_REF_TEXT: &str = "今天的风好大，我们去那边的咖啡店坐一下吧，我请你喝热可可，然后再慢慢走回去。";
const MAGIC: &[u8; 8] = b"OKVOICE1";

#[derive(Serialize, Deserialize, Clone)]
pub struct VoiceMeta {
    pub schema: u32,
    /// "<voice key>@<age>" for a design's default voice ("08@18", "m0@0"); anything for a character's own voice.
    pub id: String,
    /// "design" (made by `ok-tts profile`), "runtime" (the player chose it among candidates)
    pub source: String,
    /// The voice description VoiceDesign read for the reference line.
    pub description: String,
    pub seed: u64,
    pub ref_text: String,
    pub ref_audio_sha1: String,
    pub design_model: String,
    pub base_model: String,
    pub encoder: String,
    pub created: String,
}

#[derive(Clone)]
pub struct Profile {
    pub meta: VoiceMeta,
    /// bf16 values widened to f32
    pub embedding: Vec<f32>,
    pub ref_ogg: Vec<u8>,
}

impl Profile {
    /// Identifies the embedding (part of the cache key: a changed voice must not reuse old lines).
    pub fn tag(&self) -> String {
        embedding_tag(&self.embedding)
    }

    pub fn to_bytes(&self) -> Result<Vec<u8>> {
        let meta = serde_json::to_vec(&self.meta)?;
        let mut out = Vec::with_capacity(32 + meta.len() + self.embedding.len() * 2 + self.ref_ogg.len());
        out.extend_from_slice(MAGIC);
        out.extend_from_slice(&(meta.len() as u32).to_le_bytes());
        out.extend_from_slice(&meta);
        out.extend_from_slice(&(self.embedding.len() as u32).to_le_bytes());
        for v in &self.embedding {
            out.extend_from_slice(&f32_to_bf16(*v).to_le_bytes());
        }
        out.extend_from_slice(&(self.ref_ogg.len() as u32).to_le_bytes());
        out.extend_from_slice(&self.ref_ogg);
        Ok(out)
    }

    pub fn from_bytes(b: &[u8]) -> Result<Self> {
        let mut at = 0usize;
        let mut take = |n: usize| -> Result<&[u8]> {
            let s = b.get(at..at + n).context("okvoice is truncated")?;
            at += n;
            Ok(s)
        };
        if take(8)? != MAGIC {
            bail!("not an okvoice");
        }
        let u32_at = |s: &[u8]| u32::from_le_bytes([s[0], s[1], s[2], s[3]]) as usize;
        let n = u32_at(take(4)?);
        let meta: VoiceMeta = serde_json::from_slice(take(n)?).context("okvoice metadata")?;
        let dim = u32_at(take(4)?);
        let embedding = take(dim * 2)?.chunks_exact(2).map(|c| bf16_to_f32(u16::from_le_bytes([c[0], c[1]]))).collect();
        let n = u32_at(take(4)?);
        let ref_ogg = take(n)?.to_vec();
        Ok(Self { meta, embedding, ref_ogg })
    }

    pub fn load(path: &Path) -> Result<Self> {
        Self::from_bytes(&std::fs::read(path).with_context(|| format!("read {}", path.display()))?).with_context(|| format!("profile {}", path.display()))
    }

    pub fn save(&self, path: &Path) -> Result<()> {
        write_atomic(path, &self.to_bytes()?)
    }
}

/// Rounded to bf16 first, so a voice read back from a file or card has the same tag as when it was made.
pub fn embedding_tag(embedding: &[f32]) -> String {
    let mut d = Sha1::new();
    for v in embedding {
        d.update(&f32_to_bf16(*v).to_le_bytes());
    }
    d.digest().to_string()[..16].to_string()
}

pub fn profile_id(voice: &VoiceChoice) -> String {
    format!("{}@{}", voice.key, voice.age)
}

pub fn sha1_hex(bytes: &[u8]) -> String {
    let mut d = Sha1::new();
    d.update(bytes);
    d.digest().to_string()
}

pub fn now_utc() -> String {
    let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    format!("unix:{secs}")
}

/// A finished reference clip -> a voice: its embedding (CPU) and the clip as OGG.
pub fn make_profile(encoder: &SpeakerEncoder, samples: &[f32], meta: VoiceMeta) -> Result<Profile> {
    let ref_ogg = encode_ogg(samples, SAMPLE_RATE)?;
    let embedding = encoder.embed(samples)?.into_iter().map(|v| bf16_to_f32(f32_to_bf16(v))).collect();
    Ok(Profile { meta: VoiceMeta { ref_audio_sha1: sha1_hex(&ref_ogg), ..meta }, embedding, ref_ogg })
}

/// Trim and limit a reference clip like every cached line.
pub fn finish_reference(mut samples: Vec<f32>) -> Result<Vec<f32>> {
    trim_silence(&mut samples, SAMPLE_RATE);
    peak_normalize(&mut samples);
    if samples.len() < SAMPLE_RATE as usize {
        bail!("the reference is shorter than a second");
    }
    Ok(samples)
}

/// Every profile in a directory, by id.
pub fn load_all(dir: &Path) -> Result<HashMap<String, Profile>> {
    let mut out = HashMap::new();
    let entries = match std::fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return Ok(out),
    };
    for e in entries.flatten() {
        let p = e.path();
        if p.extension().and_then(|x| x.to_str()) != Some("okvoice") {
            continue;
        }
        let prof = Profile::load(&p)?;
        if prof.meta.schema != SCHEMA {
            bail!("profile {} has schema {}, expected {SCHEMA}", p.display(), prof.meta.schema);
        }
        out.insert(prof.meta.id.clone(), prof);
    }
    Ok(out)
}

#[derive(Deserialize)]
struct LegacyProfile {
    id: String,
    seed: u64,
    ref_text: String,
    design_model: String,
    base_model: String,
    encoder: String,
    embedding: Vec<f32>,
}

/// Schema 1 (<id>.json + <id>.wav) -> <id>.okvoice; the old pair is removed once the new file is written.
fn convert_legacy(dir: &Path, voices: &Voices) -> Result<usize> {
    let mut n = 0;
    let Ok(entries) = std::fs::read_dir(dir) else { return Ok(0) };
    for e in entries.flatten() {
        let json = e.path();
        if json.extension().and_then(|x| x.to_str()) != Some("json") {
            continue;
        }
        let Ok(old) = serde_json::from_str::<LegacyProfile>(&std::fs::read_to_string(&json)?) else { continue };
        let wav = json.with_extension("wav");
        let samples = crate::consistency::wav_samples(&std::fs::read(&wav).with_context(|| format!("read {}", wav.display()))?)?;
        let (key, age) = old.id.rsplit_once('@').unwrap_or((old.id.as_str(), "18"));
        let description = voices.choose(key, age.parse().unwrap_or(18)).instruct;
        let ref_ogg = encode_ogg(&samples, SAMPLE_RATE)?;
        let p = Profile {
            meta: VoiceMeta { schema: SCHEMA, id: old.id.clone(), source: "design".into(), description, seed: old.seed, ref_text: old.ref_text,
                              ref_audio_sha1: sha1_hex(&ref_ogg), design_model: old.design_model, base_model: old.base_model,
                              encoder: old.encoder, created: now_utc() },
            embedding: old.embedding.into_iter().map(|v| bf16_to_f32(f32_to_bf16(v))).collect(),
            ref_ogg,
        };
        p.save(&dir.join(format!("{}.okvoice", old.id)))?;
        std::fs::remove_file(&json)?;
        std::fs::remove_file(&wav)?;
        n += 1;
    }
    Ok(n)
}

/// Every voice the game can ask for: each female personality at each age (12 is one shared design), each male tone.
pub fn all_choices(voices: &Voices, ages: Option<&[u32]>) -> Vec<VoiceChoice> {
    let ages: Vec<u32> = match ages {
        Some(a) => a.to_vec(),
        None => {
            let mut a = voices.ages();
            a.push(12);
            a
        }
    };
    let mut out: Vec<VoiceChoice> = Vec::new();
    let mut seen = std::collections::HashSet::new();
    for key in voices.female_keys() {
        for age in &ages {
            let c = voices.choose(&key, *age);
            if seen.insert(profile_id(&c)) {
                out.push(c);
            }
        }
    }
    for key in voices.male_keys() {
        out.push(voices.choose(&key, 0));
    }
    out
}

pub struct BuildArgs {
    pub design: PathBuf,
    pub base: PathBuf,
    pub voices: PathBuf,
    pub out: PathBuf,
    pub keys: Option<Vec<String>>,
    pub ages: Option<Vec<u32>>,
    pub ref_text: String,
    pub force: bool,
    pub positions: usize,
}

pub fn snapshot_name(dir: &Path) -> String {
    let parts: Vec<String> = dir.components().rev().take(3).map(|c| c.as_os_str().to_string_lossy().into_owned()).collect();
    parts.into_iter().rev().collect::<Vec<_>>().join("/")
}

pub fn build(a: &BuildArgs) -> Result<()> {
    let voices = Voices::load(&a.voices)?;
    std::fs::create_dir_all(&a.out)?;
    let converted = convert_legacy(&a.out, &voices)?;
    if converted > 0 {
        println!("profiles: converted {converted} schema 1 profiles to .okvoice");
    }
    let ref_text = clean_line(&a.ref_text);
    let mut wanted = all_choices(&voices, a.ages.as_deref());
    if let Some(keys) = &a.keys {
        wanted.retain(|c| keys.iter().any(|k| voices.choose(k, c.age).key == c.key));
    }
    let existing = load_all(&a.out)?;
    let stale: Vec<VoiceChoice> = wanted.into_iter().filter(|c| {
        a.force || match existing.get(&profile_id(c)) {
            Some(p) if p.meta.source == "runtime" => false,
            Some(p) => p.meta.description != c.instruct || p.meta.seed != c.seed || p.meta.ref_text != ref_text,
            None => true,
        }
    }).collect();
    println!("profiles: {} to make in {}", stale.len(), a.out.display());
    if stale.is_empty() {
        return Ok(());
    }
    // 1. References from VoiceDesign (its GPU memory is freed before anything else loads).
    let design = Engine::load(&a.design, &a.voices, a.positions)?;
    let mut refs = Vec::new();
    for (i, c) in stale.iter().enumerate() {
        let s = design.speak(&ref_text, &c.key, c.age, GenConfig::default())?;
        let samples = finish_reference(s.samples).with_context(|| profile_id(c))?;
        println!("[{}/{}] {} reference {:.2}s", i + 1, stale.len(), profile_id(c), samples.len() as f64 / SAMPLE_RATE as f64);
        refs.push(samples);
    }
    drop(design);
    // 2. Embeddings (CPU).
    let store = Store::open(&a.base.join("model.safetensors"))?;
    let encoder = SpeakerEncoder::load(&store).context("speaker encoder")?;
    drop(store);
    for (c, samples) in stale.iter().zip(&refs) {
        let meta = VoiceMeta {
            schema: SCHEMA, id: profile_id(c), source: "design".into(), description: c.instruct.clone(), seed: c.seed,
            ref_text: ref_text.clone(), ref_audio_sha1: String::new(), design_model: snapshot_name(&a.design),
            base_model: snapshot_name(&a.base), encoder: ENCODER.into(), created: now_utc(),
        };
        make_profile(&encoder, samples, meta)?.save(&a.out.join(format!("{}.okvoice", profile_id(c))))?;
    }
    println!("profiles: done");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn okvoice_round_trip_keeps_the_tag() {
        let embedding: Vec<f32> = (0..2048).map(|i| ((i as f32) * 0.37).sin() * 3.0).collect();
        let p = Profile {
            meta: VoiceMeta { schema: SCHEMA, id: "08@18".into(), source: "design".into(), description: "女性".into(), seed: 7,
                              ref_text: "好。".into(), ref_audio_sha1: String::new(), design_model: "d".into(), base_model: "b".into(),
                              encoder: ENCODER.into(), created: "unix:0".into() },
            embedding: embedding.iter().map(|v| bf16_to_f32(f32_to_bf16(*v))).collect(),
            ref_ogg: vec![1, 2, 3],
        };
        let q = Profile::from_bytes(&p.to_bytes().unwrap()).unwrap();
        assert_eq!(q.embedding, p.embedding);
        assert_eq!(q.tag(), p.tag());
        assert_eq!(q.tag(), embedding_tag(&embedding));
        assert_eq!(q.ref_ogg, vec![1, 2, 3]);
        assert_eq!(q.meta.id, "08@18");
        assert!(Profile::from_bytes(&p.to_bytes().unwrap()[..20]).is_err());
    }
}
