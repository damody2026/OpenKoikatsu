//! Fixed voices: one profile per voice design (personality + age, or a male tone). VoiceDesign speaks one reference
//! line once; the Base speaker encoder turns it into a speaker embedding; from then on the Base model speaks every line
//! with that embedding (x-vector clone), so a character keeps one voice across lines instead of VoiceDesign
//! re-imagining the description for each line.
//!
//!   ok-tts profile [--base DIR] [--model VOICEDESIGN_DIR] [--profiles DIR] [--voices voices.json]
//!                  [--keys 08,m0] [--ages 18,21] [--ref-text TEXT] [--force]
//!
//! Writes <profiles>/<key>@<age>.wav (the reference) and <key>@<age>.json (schema 1: the embedding and what it was made
//! from). A profile whose design (instruct, seed) or reference text changed is rebuilt; others are kept. Profiles are
//! local files beside the exe (Build/ is not versioned). Without --keys every female voice at every age and every male
//! tone is made.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};
use sha1_smol::Sha1;

use crate::audio::{peak_normalize, trim_silence, wav_bytes, write_atomic};
use crate::engine::{Engine, SAMPLE_RATE};
use crate::speaker::SpeakerEncoder;
use crate::talker::GenConfig;
use crate::text::{clean_line, VoiceChoice, Voices};
use crate::weights::Store;

pub const SCHEMA: u32 = 1;
pub const DEFAULT_REF_TEXT: &str = "今天的风好大，我们去那边的咖啡店坐一下吧，我请你喝热可可，然后再慢慢走回去。";

#[derive(Serialize, Deserialize, Clone)]
pub struct Profile {
    pub schema: u32,
    /// "<voice key>@<age>", e.g. "08@18", "m0@0"
    pub id: String,
    pub instruct_sha1: String,
    pub seed: u64,
    pub ref_text: String,
    pub ref_wav_sha1: String,
    pub design_model: String,
    pub base_model: String,
    pub encoder: String,
    pub embedding: Vec<f32>,
}

impl Profile {
    /// Identifies the embedding (part of the cache key: a rebuilt profile must not reuse old lines).
    pub fn tag(&self) -> String {
        let mut d = Sha1::new();
        for v in &self.embedding {
            d.update(&v.to_le_bytes());
        }
        d.digest().to_string()[..16].to_string()
    }
}

pub fn profile_id(voice: &VoiceChoice) -> String {
    format!("{}@{}", voice.key, voice.age)
}

pub fn sha1_hex(bytes: &[u8]) -> String {
    let mut d = Sha1::new();
    d.update(bytes);
    d.digest().to_string()
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
        if p.extension().and_then(|x| x.to_str()) != Some("json") {
            continue;
        }
        let prof: Profile = serde_json::from_str(&std::fs::read_to_string(&p)?).with_context(|| format!("profile {}", p.display()))?;
        if prof.schema != SCHEMA {
            bail!("profile {} has schema {}, expected {SCHEMA}", p.display(), prof.schema);
        }
        out.insert(prof.id.clone(), prof);
    }
    Ok(out)
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

fn snapshot_name(dir: &Path) -> String {
    let parts: Vec<String> = dir.components().rev().take(3).map(|c| c.as_os_str().to_string_lossy().into_owned()).collect();
    parts.into_iter().rev().collect::<Vec<_>>().join("/")
}

pub fn build(a: &BuildArgs) -> Result<()> {
    let voices = Voices::load(&a.voices)?;
    std::fs::create_dir_all(&a.out)?;
    let ref_text = clean_line(&a.ref_text);
    let mut wanted = all_choices(&voices, a.ages.as_deref());
    if let Some(keys) = &a.keys {
        wanted.retain(|c| keys.iter().any(|k| voices.choose(k, c.age).key == c.key));
    }
    let existing = load_all(&a.out)?;
    let stale: Vec<VoiceChoice> = wanted.into_iter().filter(|c| {
        a.force || match existing.get(&profile_id(c)) {
            Some(p) => p.instruct_sha1 != sha1_hex(c.instruct.as_bytes()) || p.seed != c.seed || p.ref_text != ref_text,
            None => true,
        }
    }).collect();
    println!("profiles: {} to make in {}", stale.len(), a.out.display());
    if stale.is_empty() {
        return Ok(());
    }
    // 1. References from VoiceDesign (its GPU memory is freed before anything else loads).
    let design = Engine::load(&a.design, &a.voices, a.positions)?;
    for (i, c) in stale.iter().enumerate() {
        let id = profile_id(c);
        let s = design.speak(&ref_text, &c.key, c.age, GenConfig::default())?;
        let mut samples = s.samples;
        trim_silence(&mut samples, SAMPLE_RATE);
        peak_normalize(&mut samples);
        if samples.len() < SAMPLE_RATE as usize {
            bail!("reference for {id} is shorter than a second");
        }
        write_atomic(&a.out.join(format!("{id}.wav")), &wav_bytes(&samples, SAMPLE_RATE))?;
        println!("[{}/{}] {id} reference {:.2}s", i + 1, stale.len(), samples.len() as f64 / SAMPLE_RATE as f64);
    }
    drop(design);
    // 2. Embeddings (CPU).
    let store = Store::open(&a.base.join("model.safetensors"))?;
    let encoder = SpeakerEncoder::load(&store).context("speaker encoder")?;
    drop(store);
    for c in &stale {
        let id = profile_id(c);
        let wav = std::fs::read(a.out.join(format!("{id}.wav")))?;
        let samples = crate::consistency::wav_samples(&wav)?;
        let profile = Profile {
            schema: SCHEMA, id: id.clone(), instruct_sha1: sha1_hex(c.instruct.as_bytes()), seed: c.seed, ref_text: ref_text.clone(),
            ref_wav_sha1: sha1_hex(&wav), design_model: snapshot_name(&a.design), base_model: snapshot_name(&a.base),
            encoder: "ok-tts speaker.rs 1".into(), embedding: encoder.embed(&samples)?,
        };
        write_atomic(&a.out.join(format!("{id}.json")), serde_json::to_string(&profile)?.as_bytes())?;
    }
    println!("profiles: done");
    Ok(())
}
