//! Voices on the Base backend, chosen and fixed while the game runs.
//!
//!   POST /voice/candidates {"personality","age","out_dir", "description"?, "count"?, "ref_text"?, "preview_text"?}
//!        VoiceDesign (loaded for the call, then freed) speaks the reference line `count` times (1..6, default 3) with new
//!        seeds; each take becomes a candidate voice. For each: its reference clip and a preview line spoken by Base in
//!        that voice, as OGG files in out_dir. -> {"candidates": [{"candidate", "seed", "reference_path", "preview_path"}]}
//!        Synthesis waits meanwhile (one GPU engine at a time); expect tens of seconds.
//!   POST /voice/fix {"candidate"? , "personality","age", "profile"?: true, "card"?: PATH}
//!        Keep a voice: as the default of that design (<profiles>/<key>@<age>.okvoice, the previous one kept as
//!        .okvoice.prev) and/or in a character card (okVo chunk). Without "candidate" the design's current voice is
//!        written into the card, fixing it there even if the default changes later.
//!   GET  /voice/card?card=PATH -> the voice stored in a card (or null)
//!   POST /voice/card/remove {"card"} -> the card speaks with her design's default voice again
//! Synthesis requests with "card": a voice stored in that card wins over the design's default.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::SystemTime;

use anyhow::{bail, Context, Result};
use serde_json::{json, Value};

use crate::audio::{encode_ogg, peak_normalize, trim_silence, write_atomic};
use crate::cardvoice;
use crate::engine::{Engine, SAMPLE_RATE};
use crate::profile::{finish_reference, make_profile, now_utc, profile_id, snapshot_name, Profile, VoiceMeta, DEFAULT_REF_TEXT, ENCODER, SCHEMA};
use crate::speaker::SpeakerEncoder;
use crate::talker::GenConfig;
use crate::text::{clean_line, Voices};

const DEFAULT_PREVIEW: &str = "你今天怎么这么晚才来？我等了好久耶。不过算了，看在你带了点心的份上。";
const MAX_CANDIDATES: usize = 32;

pub struct BaseVoices {
    /// profile id -> tag of its embedding (in the cache key)
    pub tags: Mutex<HashMap<String, String>>,
    pub profiles_dir: PathBuf,
    pub base_model: PathBuf,
    pub design_model: Option<PathBuf>,
    pub voices_path: PathBuf,
    pub positions: usize,
    pub encoder: SpeakerEncoder,
    cards: Mutex<HashMap<PathBuf, (Option<SystemTime>, Option<Arc<Profile>>)>>,
    candidates: Mutex<Vec<Profile>>,
}

impl BaseVoices {
    pub fn new(tags: HashMap<String, String>, profiles_dir: PathBuf, base_model: PathBuf, design_model: Option<PathBuf>, voices_path: PathBuf,
               positions: usize, encoder: SpeakerEncoder) -> Self {
        Self { tags: Mutex::new(tags), profiles_dir, base_model, design_model, voices_path, positions, encoder,
               cards: Mutex::new(HashMap::new()), candidates: Mutex::new(Vec::new()) }
    }

    /// The voice stored in a card, read again only when the file changes.
    pub fn card_voice(&self, card: &Path) -> Result<Option<Arc<Profile>>> {
        let mtime = std::fs::metadata(card).with_context(|| format!("card {}", card.display()))?.modified().ok();
        if let Some((m, v)) = self.cards.lock().unwrap().get(card) {
            if *m == mtime && mtime.is_some() {
                return Ok(v.clone());
            }
        }
        let v = cardvoice::read_file(card)?.map(Arc::new);
        self.cards.lock().unwrap().insert(card.to_path_buf(), (mtime, v.clone()));
        Ok(v)
    }

    fn candidate(&self, tag: &str) -> Option<Profile> {
        self.candidates.lock().unwrap().iter().find(|p| p.tag() == tag).cloned()
    }
}

fn str_field<'a>(v: &'a Value, k: &str) -> Option<&'a str> {
    v[k].as_str().filter(|s| !s.trim().is_empty())
}

fn personality_age(body: &Value) -> (String, u32) {
    let p = match &body["personality"] {
        Value::Number(n) => n.to_string(),
        Value::String(s) => s.clone(),
        _ => "29".into(),
    };
    (p, body["age"].as_u64().unwrap_or(18) as u32)
}

fn absolute_dir(body: &Value) -> Result<PathBuf> {
    let d = PathBuf::from(str_field(body, "out_dir").context("out_dir is required")?);
    if !d.is_absolute() {
        bail!("out_dir must be an absolute path");
    }
    std::fs::create_dir_all(&d)?;
    Ok(d)
}

fn card_path(body: &Value) -> Result<Option<PathBuf>> {
    match str_field(body, "card") {
        None => Ok(None),
        Some(c) => {
            let p = PathBuf::from(c);
            if !p.is_absolute() || !p.is_file() {
                bail!("card must be an existing card file (absolute path)");
            }
            Ok(Some(p))
        }
    }
}

/// New seeds every call, so asking again gives other voices.
fn fresh_seed(i: usize) -> u64 {
    let t = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(0);
    let mut x = t ^ (i as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15);
    x ^= x >> 33;
    x = x.wrapping_mul(0xff51_afd7_ed55_8ccd);
    x ^ (x >> 33)
}

pub fn candidates(engine: &Mutex<Option<Engine>>, bv: &BaseVoices, voices: &Voices, body: &Value) -> Result<Value> {
    let (personality, age) = personality_age(body);
    let out_dir = absolute_dir(body)?;
    let design_dir = bv.design_model.as_ref().context("VoiceDesign model not found; candidates need it (OK_TTS_MODEL)")?;
    let voice = voices.choose(&personality, age);
    let description = str_field(body, "description").map(str::to_string).unwrap_or_else(|| voice.instruct.clone());
    let count = body["count"].as_u64().unwrap_or(3).clamp(1, 6) as usize;
    let ref_text = clean_line(str_field(body, "ref_text").unwrap_or(DEFAULT_REF_TEXT));
    let preview_text = clean_line(str_field(body, "preview_text").unwrap_or(DEFAULT_PREVIEW));
    let guard = engine.lock().unwrap();
    let base = guard.as_ref().context("model is still loading")?;
    // 1. References from VoiceDesign, loaded beside Base for this call only.
    let design = Engine::load(design_dir, &bv.voices_path, bv.positions)?;
    let mut takes = Vec::new();
    for i in 0..count {
        let seed = fresh_seed(i);
        let s = design.speak_design(&ref_text, &description, seed, GenConfig::default())?;
        takes.push((seed, finish_reference(s.samples)?));
    }
    drop(design);
    base.gpu.trim_pool()?;
    // 2. Each take as a voice, and a preview line in it by Base.
    let mut list = Vec::new();
    for (seed, samples) in takes {
        let meta = VoiceMeta {
            schema: SCHEMA, id: profile_id(&voice), source: "runtime".into(), description: description.clone(), seed, ref_text: ref_text.clone(),
            ref_audio_sha1: String::new(), design_model: snapshot_name(design_dir), base_model: snapshot_name(&bv.base_model),
            encoder: ENCODER.into(), created: now_utc(),
        };
        let p = make_profile(&bv.encoder, &samples, meta)?;
        let tag = p.tag();
        let mut preview = base.speak_voice(&preview_text, &personality, age, Some(&p.embedding), GenConfig::default())?.samples;
        trim_silence(&mut preview, SAMPLE_RATE);
        peak_normalize(&mut preview);
        let reference_path = out_dir.join(format!("voice-candidate-{tag}-reference.ogg"));
        let preview_path = out_dir.join(format!("voice-candidate-{tag}-preview.ogg"));
        write_atomic(&reference_path, &p.ref_ogg)?;
        write_atomic(&preview_path, &encode_ogg(&preview, SAMPLE_RATE)?)?;
        list.push(json!({"candidate": tag, "seed": seed, "reference_path": reference_path, "preview_path": preview_path}));
        let mut c = bv.candidates.lock().unwrap();
        c.retain(|x| x.tag() != tag);
        c.push(p);
        let excess = c.len().saturating_sub(MAX_CANDIDATES);
        c.drain(..excess);
    }
    drop(guard);
    crate::log!("ok-tts: VOICE {} candidates for {} ({} chars of description)", list.len(), profile_id(&voice), description.chars().count());
    Ok(json!({"ok": true, "personality": voice.key, "age": voice.age, "description": description, "candidates": list}))
}

pub fn fix(engine: &Mutex<Option<Engine>>, bv: &BaseVoices, voices: &Voices, body: &Value) -> Result<Value> {
    let (personality, age) = personality_age(body);
    let voice = voices.choose(&personality, age);
    let id = profile_id(&voice);
    let as_profile = body["profile"].as_bool().unwrap_or(false);
    let card = card_path(body)?;
    if !as_profile && card.is_none() {
        bail!("say where to keep the voice: \"profile\": true and/or \"card\"");
    }
    let mut chosen = match str_field(body, "candidate") {
        Some(tag) => bv.candidate(tag).with_context(|| format!("no candidate {tag} (ask /voice/candidates again)"))?,
        None => {
            let guard = engine.lock().unwrap();
            let e = guard.as_ref().context("model is still loading")?;
            e.profiles.as_ref().and_then(|p| p.get(&id)).cloned().with_context(|| format!("no voice profile {id}"))?
        }
    };
    chosen.meta.id = id.clone();
    let tag = chosen.tag();
    if as_profile {
        let path = bv.profiles_dir.join(format!("{id}.okvoice"));
        if path.is_file() {
            std::fs::copy(&path, path.with_extension("okvoice.prev"))?;
        }
        chosen.save(&path)?;
        let mut guard = engine.lock().unwrap();
        if let Some(p) = guard.as_mut().and_then(|e| e.profiles.as_mut()) {
            p.insert(id.clone(), chosen.clone());
        }
        bv.tags.lock().unwrap().insert(id.clone(), tag.clone());
    }
    if let Some(card) = &card {
        cardvoice::write_file(card, Some(&chosen))?;
        bv.cards.lock().unwrap().remove(card);
    }
    crate::log!("ok-tts: VOICE fixed {id} tag={tag} profile={as_profile} card={}", card.as_ref().map(|c| c.display().to_string()).unwrap_or_default());
    Ok(json!({"ok": true, "id": id, "tag": tag, "profile": as_profile, "card": card}))
}

pub fn card_info(bv: &BaseVoices, card: &Path) -> Result<Value> {
    Ok(match bv.card_voice(card)? {
        Some(p) => json!({"ok": true, "voice": {"tag": p.tag(), "id": p.meta.id, "source": p.meta.source, "description": p.meta.description,
                                                  "seed": p.meta.seed, "created": p.meta.created}}),
        None => json!({"ok": true, "voice": null}),
    })
}

pub fn card_remove(bv: &BaseVoices, body: &Value) -> Result<Value> {
    let card = card_path(body)?.context("card is required")?;
    cardvoice::write_file(&card, None)?;
    bv.cards.lock().unwrap().remove(&card);
    Ok(json!({"ok": true}))
}
