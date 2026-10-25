//! One song for one heroine: the request, the voice she really speaks with, the cache keys, and the run through the
//! stages. Stages are cached on their own: the song (lyrics + style) and its stems are shared by every heroine, only
//! the converted vocal and the mix are hers.
//!
//!   work_root/
//!     voices/<tag>-<ref sha1>/      reference.wav + voice.json (from ok-tts voice-export)
//!     stages/song/<key>/            song.wav                     + DONE
//!     stages/stems/<key>/           vocals.wav accompaniment.wav + DONE
//!     cache/<artifact key>/         song.ogg vocals.ogg mouth.json lyrics.txt manifest.json + DONE
//!     jobs/<job id>/                work in progress (removed after publishing)
//! A directory counts only with its DONE marker, written last; a cached artifact also has to match the SHA-256 of
//! every file its manifest lists.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Instant;

use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

use crate::audio::{self, Audio};
use crate::backend::{revisions, stage_error, Runner, StageError};
use crate::config::Config;

pub const RECIPE_VERSION: u32 = 1;
const MAX_LYRICS_CHARS: usize = 640;
const MIX_TARGET_LUFS: f64 = -16.0;
const MIX_CEILING_DBTP: f64 = -1.0;

#[derive(Deserialize, Serialize, Clone, Debug, PartialEq)]
pub struct SongRequest {
    pub schema: u32,
    pub request_id: String,
    #[serde(default)]
    pub instance_id: String,
    #[serde(default)]
    pub save_id: String,
    #[serde(default)]
    pub event_id: String,
    #[serde(default)]
    pub generation: u64,
    pub heroine_id: String,
    /// Her voice: her card (a voice stored in it wins) and her design (personality + age), as ok-tts resolves them.
    #[serde(default)]
    pub card: Option<String>,
    pub personality: String,
    #[serde(default = "default_age")]
    pub age: u32,
    pub lyrics_display: String,
    pub lyrics_model: String,
    pub tags: Vec<String>,
    /// Accepted for the contract; the current generator has no seed (its stage cache keeps a song).
    #[serde(default)]
    pub seed: Option<u64>,
    #[serde(default = "default_max")]
    pub max_duration_ms: u32,
    #[serde(default = "default_recipe")]
    pub recipe_version: u32,
}

fn default_age() -> u32 {
    18
}
fn default_max() -> u32 {
    30_000
}
fn default_recipe() -> u32 {
    RECIPE_VERSION
}

fn clean_text(s: &str) -> String {
    s.chars().filter(|c| !c.is_control() || *c == '\n').collect::<String>().trim().to_string()
}

impl SongRequest {
    pub fn validate(&mut self) -> Result<()> {
        if self.schema != 1 {
            bail!("schema must be 1");
        }
        if self.request_id.is_empty() || self.request_id.len() > 128 || self.heroine_id.is_empty() {
            bail!("request_id and heroine_id are required");
        }
        self.lyrics_display = clean_text(&self.lyrics_display);
        self.lyrics_model = clean_text(&self.lyrics_model);
        if self.lyrics_model.is_empty() || self.lyrics_model.chars().count() > MAX_LYRICS_CHARS || self.lyrics_display.chars().count() > MAX_LYRICS_CHARS {
            bail!("lyrics must be 1..{MAX_LYRICS_CHARS} characters");
        }
        if self.tags.is_empty() || self.tags.len() > 12 || self.tags.iter().any(|t| t.is_empty() || t.len() > 40 || t.contains(',')) {
            bail!("tags: 1..12 words without commas");
        }
        self.tags = self.tags.iter().map(|t| clean_text(t).to_lowercase()).collect();
        // Up to two minutes: her full songs are about a minute and a half (two verses, chorus three times, a bridge).
        self.max_duration_ms = self.max_duration_ms.clamp(10_000, 120_000);
        if self.recipe_version != RECIPE_VERSION {
            bail!("recipe_version {} is not supported (this ok-song makes {RECIPE_VERSION})", self.recipe_version);
        }
        if let Some(card) = &self.card {
            let p = Path::new(card);
            if !p.is_absolute() || !p.is_file() || p.extension().and_then(|x| x.to_str()).map(|x| x.to_lowercase()) != Some("png".into()) {
                bail!("card must be an existing .png card (absolute path)");
            }
        }
        Ok(())
    }
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    let mut h = Sha256::new();
    h.update(bytes);
    h.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

fn sha256_file(path: &Path) -> Result<String> {
    Ok(sha256_hex(&std::fs::read(path).with_context(|| format!("read {}", path.display()))?))
}

pub(crate) fn key_of(v: &Value) -> String {
    sha256_hex(v.to_string().as_bytes())[..32].to_string()
}

#[derive(Serialize, Clone, Debug)]
pub struct Voice {
    pub tag: String,
    pub source: String,
    pub reference_sha1: String,
    pub reference: PathBuf,
    pub report: Value,
}

/// The voice she speaks with, exported by ok-tts (read only), copied under work_root/voices.
pub fn resolve_voice(cfg: &Config, req: &SongRequest) -> Result<Voice> {
    let tmp = cfg.voices_dir().join(format!("tmp-{}-{}", std::process::id(), sha256_hex(req.request_id.as_bytes()).get(..12).unwrap_or("x")));
    std::fs::create_dir_all(&tmp)?;
    let mut cmd = Command::new(&cfg.ok_tts);
    cmd.args(["voice-export", "--personality", &req.personality, "--age", &req.age.to_string()]);
    cmd.arg("--profiles").arg(&cfg.ok_tts_profiles).arg("--voices").arg(&cfg.ok_tts_voices).arg("--out").arg(&tmp);
    if let Some(card) = &req.card {
        cmd.args(["--card", card]);
    }
    let out = cmd.output().with_context(|| format!("run {}", cfg.ok_tts.display()))?;
    if !out.status.success() {
        let _ = std::fs::remove_dir_all(&tmp);
        let msg = String::from_utf8_lossy(&out.stderr).lines().last().unwrap_or("").to_string() + &String::from_utf8_lossy(&out.stdout);
        let code = if msg.contains("VOICE_MISSING") { "VOICE_MISSING" } else { "INVALID_AUDIO" };
        return Err(stage_error(code, msg.trim().to_string()));
    }
    let report: Value = serde_json::from_slice(&out.stdout).context("voice-export output")?;
    let tag = report["embedding_tag"].as_str().context("embedding_tag")?.to_string();
    let reference_sha1 = report["reference_wav_sha1"].as_str().context("reference_wav_sha1")?.to_string();
    let dir = cfg.voices_dir().join(format!("{tag}-{}", &reference_sha1[..12]));
    if !dir.join("reference.wav").is_file() {
        std::fs::create_dir_all(&dir)?;
        std::fs::copy(tmp.join("reference.wav"), dir.join("reference.wav"))?;
        std::fs::copy(tmp.join("voice.json"), dir.join("voice.json"))?;
    }
    let _ = std::fs::remove_dir_all(&tmp);
    Ok(Voice { tag, source: report["source"].as_str().unwrap_or("").to_string(), reference_sha1, reference: dir.join("reference.wav"), report })
}

/// Keys of the three cached steps.
#[derive(Serialize, Clone, Debug)]
pub struct Keys {
    pub song: String,
    pub stems: String,
    pub artifact: String,
}

pub fn keys(cfg: &Config, req: &SongRequest, voice: &Voice) -> Keys {
    let rev = revisions(cfg);
    let song = key_of(&json!({"recipe": req.recipe_version, "generate": rev["generate"], "lyrics_model": req.lyrics_model,
                              "tags": req.tags, "max_ms": req.max_duration_ms}));
    let stems = key_of(&json!({"song": song, "separate": rev["separate"]}));
    let artifact = key_of(&json!({"stems": stems, "convert": rev["convert"], "voice_tag": voice.tag, "reference_sha1": voice.reference_sha1,
                                  "lyrics_display": req.lyrics_display, "mix": {"target_lufs": MIX_TARGET_LUFS, "ceiling_dbtp": MIX_CEILING_DBTP,
                                  "rate": audio::OUTPUT_RATE, "fade_out_s": 2}, "envelope": "rms10ms/p95 floor0.08 attack0.6 release0.25", "f0_shift": 0}));
    Keys { song, stems, artifact }
}

fn done(dir: &Path) -> bool {
    dir.join("DONE").is_file()
}

/// Move a finished step into place, or keep the one another job published first.
fn publish(work: &Path, target: &Path) -> Result<()> {
    std::fs::write(work.join("DONE"), b"ok")?;
    if done(target) {
        return Ok(());
    }
    if target.exists() {
        // An earlier attempt that never finished (no DONE): it is ours to replace.
        std::fs::remove_dir_all(target).with_context(|| format!("remove stale {}", target.display()))?;
    }
    std::fs::create_dir_all(target.parent().context("parent")?)?;
    std::fs::rename(work, target).with_context(|| format!("publish {}", target.display()))?;
    Ok(())
}

/// A cached artifact that is whole: DONE, a manifest, and every listed file with its SHA-256.
pub fn valid_artifact(dir: &Path) -> Option<Value> {
    if !done(dir) {
        return None;
    }
    let m: Value = serde_json::from_slice(&std::fs::read(dir.join("manifest.json")).ok()?).ok()?;
    for f in m["files"].as_array()? {
        let name = f["name"].as_str()?;
        if sha256_file(&dir.join(name)).ok()? != f["sha256"].as_str()? {
            return None;
        }
    }
    Some(m)
}

/// Where a job is; the service shows it on GET /jobs/{id}.
pub trait Progress {
    fn stage(&self, stage: &str);
}

pub struct RunOutcome {
    pub manifest: Value,
    pub cache_hit: bool,
}

/// Make the song for this request (or find it): generating -> separating -> converting -> mixing -> validating.
pub fn run(cfg: &Config, req: &SongRequest, voice: &Voice, keys: &Keys, job_dir: &Path, cancel: Arc<AtomicBool>, progress: &dyn Progress) -> Result<RunOutcome> {
    let artifact_dir = cfg.cache_dir().join(&keys.artifact);
    if let Some(m) = valid_artifact(&artifact_dir) {
        return Ok(RunOutcome { manifest: m, cache_hit: true });
    }
    let runner = Runner { cfg, cancel: cancel.clone() };
    let mut timings = serde_json::Map::new();
    let check_cancel = || -> Result<()> {
        if cancel.load(std::sync::atomic::Ordering::SeqCst) { Err(stage_error("CANCELLED", "cancelled")) } else { Ok(()) }
    };
    // 1. The song (shared by every heroine).
    let song_dir = cfg.stage_cache_dir().join("song").join(&keys.song);
    if !done(&song_dir) {
        progress.stage("generating");
        let t = Instant::now();
        let work = job_dir.join("song");
        std::fs::create_dir_all(&work)?;
        let song = runner.generate(&req.lyrics_model, &req.tags, req.max_duration_ms, &work)?;
        Audio::read_wav(&song)?.check("the generated song")?;
        publish(&work, &song_dir)?;
        timings.insert("generate_seconds".into(), json!(t.elapsed().as_secs_f64()));
    }
    check_cancel()?;
    // 2. Its stems (shared too).
    let stems_dir = cfg.stage_cache_dir().join("stems").join(&keys.stems);
    if !done(&stems_dir) {
        progress.stage("separating");
        let t = Instant::now();
        let work = job_dir.join("stems");
        std::fs::create_dir_all(&work)?;
        let (v, a) = runner.separate(&song_dir.join("song.wav"), &work)?;
        let (va, aa) = (Audio::read_wav(&v)?, Audio::read_wav(&a)?);
        va.check("the separated vocal")?;
        aa.check("the accompaniment")?;
        if (va.frames() as i64 - aa.frames() as i64).abs() > (va.rate / 100) as i64 {
            return Err(stage_error("INVALID_AUDIO", "vocal and accompaniment lengths differ"));
        }
        publish(&work, &stems_dir)?;
        timings.insert("separate_seconds".into(), json!(t.elapsed().as_secs_f64()));
    }
    check_cancel()?;
    // 3. Her voice.
    progress.stage("converting");
    let t = Instant::now();
    let work = job_dir.join("artifact");
    std::fs::create_dir_all(&work)?;
    let converted = runner.convert(&stems_dir.join("vocals.wav"), &voice.reference, &work)?;
    timings.insert("convert_seconds".into(), json!(t.elapsed().as_secs_f64()));
    check_cancel()?;
    // 4. Mix, envelope, files.
    progress.stage("mixing");
    let t = Instant::now();
    let vocal = Audio::read_wav(&converted)?;
    let vocal_check = vocal.check("her converted vocal")?;
    let accompaniment = Audio::read_wav(&stems_dir.join("accompaniment.wav"))?;
    let offset_ms = (vocal.seconds() - accompaniment.seconds()) * 1000.0;
    if offset_ms.abs() > 50.0 {
        return Err(stage_error("INVALID_AUDIO", format!("her vocal is {offset_ms:.0} ms longer than the accompaniment")));
    }
    let (mixed, mix_report) = audio::mix(&vocal, &accompaniment, MIX_TARGET_LUFS, MIX_CEILING_DBTP)?;
    let mix_check = mixed.check("the mix")?;
    let envelope = audio::mouth_envelope(&vocal);
    audio::write_atomic(&work.join("song.ogg"), &audio::encode_ogg(&mixed)?)?;
    let vocal48 = Audio { rate: audio::OUTPUT_RATE, channels: vec![audio::resample(&vocal.mono(), vocal.rate, audio::OUTPUT_RATE)] };
    audio::write_atomic(&work.join("vocals.ogg"), &audio::encode_ogg(&vocal48)?)?;
    audio::write_atomic(&work.join("mouth.json"), serde_json::to_string(&envelope)?.as_bytes())?;
    audio::write_atomic(&work.join("lyrics.txt"), req.lyrics_display.as_bytes())?;
    timings.insert("mix_seconds".into(), json!(t.elapsed().as_secs_f64()));
    // 5. Manifest last, then publish.
    progress.stage("validating");
    let mut files = Vec::new();
    for name in ["song.ogg", "vocals.ogg", "mouth.json", "lyrics.txt"] {
        files.push(json!({"name": name, "sha256": sha256_file(&work.join(name))?, "bytes": std::fs::metadata(work.join(name))?.len()}));
    }
    let manifest = json!({
        "schema": 1, "artifact_key": keys.artifact, "keys": keys, "heroine_id": req.heroine_id, "voice": voice,
        "lyrics_display": req.lyrics_display, "lyrics_model": req.lyrics_model, "tags": req.tags,
        "seed": req.seed, "seed_supported": false, "max_duration_ms": req.max_duration_ms, "recipe_version": req.recipe_version,
        "backends": revisions(cfg), "mix": mix_report, "song": mix_check, "vocal": vocal_check, "vocal_offset_ms": offset_ms,
        "envelope": {"hop_ms": envelope.hop_ms, "values": envelope.values.len()}, "timings": timings,
        "vram_peak_mb": Value::Null, "vram_note": "not measured per stage (the GPU is shared with the game)", "files": files,
    });
    audio::write_atomic(&work.join("manifest.json"), serde_json::to_string_pretty(&manifest)?.as_bytes())?;
    for f in manifest["files"].as_array().unwrap() {
        if sha256_file(&work.join(f["name"].as_str().unwrap()))? != f["sha256"].as_str().unwrap() {
            return Err(stage_error("INVALID_AUDIO", "a file changed while publishing"));
        }
    }
    let _ = std::fs::remove_dir_all(work.join("vc"));
    publish(&work, &artifact_dir)?;
    Ok(RunOutcome { manifest, cache_hit: false })
}

fn dir_bytes(dir: &Path) -> u64 {
    std::fs::read_dir(dir).map(|it| it.flatten().filter_map(|e| e.metadata().ok()).filter(|m| m.is_file()).map(|m| m.len()).sum()).unwrap_or(0)
}

/// Keep the finished songs under `limit_mb`: the least recently finished go first; `keep` (songs asked for while this
/// service runs: an event may be about to play them) and anything unfinished stay. Only folders directly in
/// work_root/cache are removed (made by this tool; no junctions there). Returns how many were removed.
pub fn trim_cache(cfg: &Config, keep: &[String]) -> usize {
    let limit = cfg.cache_limit_mb.saturating_mul(1024 * 1024);
    let Ok(entries) = std::fs::read_dir(cfg.cache_dir()) else { return 0 };
    let mut items: Vec<(std::time::SystemTime, u64, PathBuf, String)> = entries.flatten().filter_map(|e| {
        let p = e.path();
        let key = p.file_name()?.to_str()?.to_string();
        let t = std::fs::metadata(p.join("DONE")).and_then(|m| m.modified()).ok()?;
        Some((t, dir_bytes(&p), p, key))
    }).collect();
    let mut total: u64 = items.iter().map(|i| i.1).sum();
    items.sort_by_key(|i| i.0);
    let mut removed = 0;
    for (_, bytes, path, key) in items {
        if total <= limit {
            break;
        }
        if keep.contains(&key) || path.symlink_metadata().map(|m| m.file_type().is_symlink()).unwrap_or(true) {
            continue;
        }
        if std::fs::remove_dir_all(&path).is_ok() {
            total -= bytes;
            removed += 1;
        }
    }
    removed
}

/// The error code of a failure (StageError's, else INTERNAL).
pub fn error_code(e: &anyhow::Error) -> &'static str {
    e.downcast_ref::<StageError>().map(|s| s.code).unwrap_or("INTERNAL")
}

pub fn retryable(code: &str) -> bool {
    matches!(code, "GPU_OOM" | "BACKEND_TIMEOUT" | "BACKEND_FAILED")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn req() -> SongRequest {
        SongRequest {
            schema: 1, request_id: "r1".into(), instance_id: String::new(), save_id: "s".into(), event_id: "e".into(), generation: 1,
            heroine_id: "apng-x".into(), card: None, personality: "08".into(), age: 18, lyrics_display: "晚風".into(),
            lyrics_model: "晚风".into(), tags: vec!["Piano".into()], seed: None, max_duration_ms: 30_000, recipe_version: 1,
        }
    }

    fn cfg() -> Config {
        serde_json::from_str(include_str!("../ok-song.example.json")).unwrap()
    }

    #[test]
    fn validation_cleans_and_bounds() {
        let mut r = req();
        r.lyrics_model = "晚风\u{7}吹".into();
        r.max_duration_ms = 999_999;
        r.validate().unwrap();
        assert_eq!(r.lyrics_model, "晚风吹");
        assert_eq!(r.tags, vec!["piano"]);
        assert_eq!(r.max_duration_ms, 120_000);
        let mut bad = req();
        bad.tags = vec!["a,b".into()];
        assert!(bad.validate().is_err());
    }

    #[test]
    fn keys_share_the_song_and_split_the_voice() {
        let c = cfg();
        let v1 = Voice { tag: "aaaa".into(), source: "profile".into(), reference_sha1: "1".repeat(40), reference: PathBuf::new(), report: Value::Null };
        let v2 = Voice { tag: "bbbb".into(), ..v1.clone() };
        let (k1, k2) = (keys(&c, &req(), &v1), keys(&c, &req(), &v2));
        assert_eq!(k1.song, k2.song);
        assert_eq!(k1.stems, k2.stems);
        assert_ne!(k1.artifact, k2.artifact);
        let mut other = req();
        other.request_id = "r2".into();
        other.event_id = "e2".into();
        assert_eq!(keys(&c, &other, &v1).artifact, k1.artifact, "request and event ids are not part of the key");
        other.lyrics_model = "别的".into();
        assert_ne!(keys(&c, &other, &v1).song, k1.song);
    }

    #[test]
    fn a_damaged_artifact_is_not_valid() {
        let dir = std::env::temp_dir().join(format!("ok-song-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("song.ogg"), b"abc").unwrap();
        let m = json!({"files": [{"name": "song.ogg", "sha256": sha256_hex(b"abc")}]});
        std::fs::write(dir.join("manifest.json"), m.to_string()).unwrap();
        assert!(valid_artifact(&dir).is_none(), "no DONE yet");
        std::fs::write(dir.join("DONE"), b"ok").unwrap();
        assert!(valid_artifact(&dir).is_some());
        std::fs::write(dir.join("song.ogg"), b"abd").unwrap();
        assert!(valid_artifact(&dir).is_none(), "changed file");
        let _ = std::fs::remove_dir_all(&dir);
    }
}
