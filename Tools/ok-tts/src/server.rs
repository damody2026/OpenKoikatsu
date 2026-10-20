//! Local HTTP API used by StreetSocial (same contract as the earlier E:\TTS\tts_server.py):
//!   GET  /health
//!   POST /synthesize {"text", "personality", "age", "out_dir"} -> {"ok", "path", "cached", "gen_seconds", "audio_seconds", ...}
//!   POST /synthesize_stream (same body) -> 16-bit mono PCM at 24 kHz, sent in chunks while it is generated
//!        (first audio after ~4 frames). The finished line is also written to the cache, so a following
//!        /synthesize for the same line answers at once with its path.
//! Binds 127.0.0.1 only. The model loads in the background; /health reports ready=false until then.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

use anyhow::{bail, Result};
use serde::Deserialize;
use serde_json::{json, Value};
use sha1_smol::Sha1;

use crate::audio::{encode_ogg, pcm16, peak_normalize, rms, trim_silence, write_atomic, StreamTrim};
use crate::engine::{Engine, MODEL_ID, SAMPLE_RATE};
use crate::talker::GenConfig;
use crate::text::{clean_line, VoiceChoice};

const MAX_TEXT_CHARS: usize = 400;
const CACHE_VERSION: &str = "ok-tts-3"; // 3: one seed per voice

#[derive(Default)]
struct Stats {
    requests: AtomicU64,
    generated: AtomicU64,
    cached: AtomicU64,
    failed: AtomicU64,
    gen_ms: AtomicU64,
}

struct State {
    engine: Mutex<Option<Engine>>,
    ready: AtomicBool,
    load_error: Mutex<Option<String>>,
    stats: Stats,
}

#[derive(Deserialize)]
struct SynthRequest {
    text: String,
    #[serde(default)]
    personality: Value,
    #[serde(default)]
    age: Option<u32>,
    out_dir: String,
}

pub fn serve(port: u16, workers: usize, load: impl FnOnce() -> Result<Engine> + Send + 'static) -> Result<()> {
    let server = tiny_http::Server::http(("127.0.0.1", port)).map_err(|e| anyhow::anyhow!("bind 127.0.0.1:{port}: {e}"))?;
    let server = Arc::new(server);
    let state = Arc::new(State { engine: Mutex::new(None), ready: AtomicBool::new(false), load_error: Mutex::new(None), stats: Stats::default() });
    crate::log!("ok-tts: listening on http://127.0.0.1:{port} (model loads in the background)");
    {
        let state = state.clone();
        std::thread::spawn(move || match load().and_then(|e| warmup(e)) {
            Ok(engine) => {
                *state.engine.lock().unwrap() = Some(engine);
                state.ready.store(true, Ordering::SeqCst);
                crate::log!("ok-tts: READY");
            }
            Err(e) => {
                crate::log!("ok-tts: LOAD FAILED: {e:#}");
                *state.load_error.lock().unwrap() = Some(format!("{e:#}"));
                std::process::exit(1);
            }
        });
    }
    let mut handles = Vec::new();
    for _ in 0..workers.max(1) {
        let server = server.clone();
        let state = state.clone();
        handles.push(std::thread::spawn(move || {
            while let Ok(req) = server.recv() {
                handle(&state, req);
            }
        }));
    }
    for h in handles {
        let _ = h.join();
    }
    Ok(())
}

fn warmup(engine: Engine) -> Result<Engine> {
    let t = std::time::Instant::now();
    let s = engine.speak("这里是观光地，海边有饭店。", "-13", 25, GenConfig::default())?;
    let level = rms(&s.samples);
    crate::log!("ok-tts: warmup {:.2}s audio={:.2}s rms={level:.4}", t.elapsed().as_secs_f64(), s.samples.len() as f64 / SAMPLE_RATE as f64);
    if level < 0.002 {
        bail!("warmup is silent");
    }
    Ok(engine)
}

fn respond(req: tiny_http::Request, code: u16, body: Value) {
    let data = body.to_string().into_bytes();
    let header = tiny_http::Header::from_bytes(&b"Content-Type"[..], &b"application/json; charset=utf-8"[..]).unwrap();
    let _ = req.respond(tiny_http::Response::from_data(data).with_status_code(code).with_header(header));
}

fn handle(state: &Arc<State>, mut req: tiny_http::Request) {
    let url = req.url().trim_end_matches('/').to_string();
    match (req.method(), url.as_str()) {
        (tiny_http::Method::Get, "/health") => {
            let s = &state.stats;
            let ready = state.ready.load(Ordering::SeqCst);
            respond(req, 200, json!({
                "ok": ready, "ready": ready, "model": MODEL_ID, "engine": "ok-tts (Rust, CUDA)",
                "error": state.load_error.lock().unwrap().clone(),
                "requests": s.requests.load(Ordering::Relaxed), "generated": s.generated.load(Ordering::Relaxed),
                "cached": s.cached.load(Ordering::Relaxed), "failed": s.failed.load(Ordering::Relaxed),
                "gen_seconds": s.gen_ms.load(Ordering::Relaxed) as f64 / 1000.0,
            }));
        }
        (tiny_http::Method::Get, path) if path.starts_with("/voice") => {
            // /voice?personality=17&age=18 -> the prompt the next /synthesize would use (shown in the game UI).
            let query = req.url().split_once('?').map(|(_, q)| q.to_string()).unwrap_or_default();
            let mut personality = String::from("29");
            let mut age = 18u32;
            for pair in query.split('&') {
                match pair.split_once('=') {
                    Some(("personality", v)) => personality = percent_decode(v),
                    Some(("age", v)) => age = v.parse().unwrap_or(18),
                    _ => {}
                }
            }
            let guard = state.engine.lock().unwrap();
            match guard.as_ref() {
                Some(engine) => {
                    let v = engine.voices.choose(&personality, age);
                    respond(req, 200, json!({"ok": true, "personality": v.key, "age": v.age, "name": v.name, "label": v.label, "instruct": v.instruct}));
                }
                None => respond(req, 503, json!({"ok": false, "error": "model is still loading"})),
            }
        }
        (tiny_http::Method::Post, "/synthesize_stream") => {
            state.stats.requests.fetch_add(1, Ordering::Relaxed);
            if !state.ready.load(Ordering::SeqCst) {
                respond(req, 503, json!({"ok": false, "error": "model is still loading"}));
                return;
            }
            let mut body = String::new();
            let parsed = if req.body_length().unwrap_or(0) > 65536 { Err(anyhow::anyhow!("body must be at most 65536 bytes")) } else {
                std::io::Read::read_to_string(req.as_reader(), &mut body).map_err(anyhow::Error::from)
                    .and_then(|_| Ok(serde_json::from_str::<SynthRequest>(body.trim_start_matches('\u{feff}'))?))
            };
            match parsed.and_then(|r| stream_synthesis(state.clone(), r)) {
                Ok(reader) => {
                    let headers = vec![
                        tiny_http::Header::from_bytes(&b"Content-Type"[..], &b"audio/L16; rate=24000; channels=1"[..]).unwrap(),
                        tiny_http::Header::from_bytes(&b"X-Sample-Rate"[..], SAMPLE_RATE.to_string().as_bytes()).unwrap(),
                    ];
                    let _ = req.respond(tiny_http::Response::new(tiny_http::StatusCode(200), headers, reader, None, None));
                }
                Err(e) => {
                    state.stats.failed.fetch_add(1, Ordering::Relaxed);
                    crate::log!("ok-tts: STREAM FAIL {e:#}");
                    respond(req, 400, json!({"ok": false, "error": format!("{e:#}")}));
                }
            }
        }
        (tiny_http::Method::Post, "/synthesize") => {
            state.stats.requests.fetch_add(1, Ordering::Relaxed);
            if !state.ready.load(Ordering::SeqCst) {
                respond(req, 503, json!({"ok": false, "error": "model is still loading"}));
                return;
            }
            let mut body = String::new();
            let read = if req.body_length().unwrap_or(0) > 65536 { Err(anyhow::anyhow!("body must be at most 65536 bytes")) } else {
                std::io::Read::read_to_string(req.as_reader(), &mut body).map_err(anyhow::Error::from)
            };
            let result = read.and_then(|_| {
                let r: SynthRequest = serde_json::from_str(body.trim_start_matches('\u{feff}'))?;
                synthesize(state, r)
            });
            match result {
                Ok(v) => respond(req, 200, v),
                Err(e) => {
                    state.stats.failed.fetch_add(1, Ordering::Relaxed);
                    crate::log!("ok-tts: FAIL {e:#}");
                    respond(req, 400, json!({"ok": false, "error": format!("{e:#}")}));
                }
            }
        }
        _ => respond(req, 404, json!({"ok": false, "error": "not found"})),
    }
}

fn personality_string(v: &Value) -> String {
    match v {
        Value::Number(n) => n.to_string(),
        Value::String(s) => s.clone(),
        _ => "29".into(),
    }
}

fn synthesize(state: &State, r: SynthRequest) -> Result<Value> {
    let text = clean_line(&r.text);
    let chars = text.chars().count();
    if chars == 0 || chars > MAX_TEXT_CHARS {
        bail!("text must be 1..{MAX_TEXT_CHARS} characters");
    }
    let out_dir = PathBuf::from(&r.out_dir);
    if !out_dir.is_absolute() {
        bail!("out_dir must be an absolute path");
    }
    std::fs::create_dir_all(&out_dir)?;
    let personality = personality_string(&r.personality);
    let age = r.age.unwrap_or(18);
    let guard = state.engine.lock().unwrap();
    let engine = guard.as_ref().ok_or_else(|| anyhow::anyhow!("model is not loaded"))?;
    let voice = engine.voices.choose(&personality, age);
    let path = cache_path(&out_dir, &voice, &text);
    if cached(&path) {
        state.stats.cached.fetch_add(1, Ordering::Relaxed);
        return Ok(json!({"ok": true, "path": path, "cached": true, "gen_seconds": 0.0, "text": text, "personality": voice.key, "age": voice.age}));
    }
    let mut speech = engine.speak(&text, &personality, age, GenConfig::default())?;
    drop(guard);
    if speech.samples.is_empty() || rms(&speech.samples) < 0.002 {
        bail!("synthesis was silent");
    }
    trim_silence(&mut speech.samples, SAMPLE_RATE);
    peak_normalize(&mut speech.samples);
    write_atomic(&path, &encode_ogg(&speech.samples, SAMPLE_RATE)?)?;
    let audio_seconds = speech.samples.len() as f64 / SAMPLE_RATE as f64;
    state.stats.generated.fetch_add(1, Ordering::Relaxed);
    state.stats.gen_ms.fetch_add((speech.gen_seconds * 1000.0) as u64, Ordering::Relaxed);
    crate::log!("ok-tts: GEN p={} age={} chars={chars} gen={:.2}s audio={audio_seconds:.2}s {text}", speech.voice_key, speech.age, speech.gen_seconds);
    Ok(json!({"ok": true, "path": path, "cached": false, "gen_seconds": (speech.gen_seconds * 1000.0).round() / 1000.0,
              "audio_seconds": (audio_seconds * 1000.0).round() / 1000.0, "text": text, "personality": speech.voice_key, "age": speech.age}))
}

/// Where a line in this voice is cached (shared by /synthesize and /synthesize_stream).
fn cache_path(out_dir: &Path, voice: &VoiceChoice, text: &str) -> PathBuf {
    let mut digest = Sha1::new();
    digest.update(format!("{CACHE_VERSION}|{MODEL_ID}|{}|{}|{text}", voice.instruct, voice.seed).as_bytes());
    out_dir.join(format!("{}.ogg", digest.digest()))
}

/// The HTTP body of a stream: PCM pieces from the synthesis thread; the body ends when it hangs up.
struct ChannelReader {
    rx: std::sync::mpsc::Receiver<Vec<u8>>,
    buf: Vec<u8>,
    pos: usize,
}

impl std::io::Read for ChannelReader {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        while self.pos >= self.buf.len() {
            match self.rx.recv() {
                Ok(piece) => {
                    self.buf = piece;
                    self.pos = 0;
                }
                Err(_) => return Ok(0),
            }
        }
        let n = out.len().min(self.buf.len() - self.pos);
        out[..n].copy_from_slice(&self.buf[self.pos..self.pos + n]);
        self.pos += n;
        Ok(n)
    }
}

/// Generates on its own thread (holding the engine) and feeds the response as audio comes out. A client
/// that hangs up stops the generation.
fn stream_synthesis(state: Arc<State>, r: SynthRequest) -> Result<ChannelReader> {
    let text = clean_line(&r.text);
    let chars = text.chars().count();
    if chars == 0 || chars > MAX_TEXT_CHARS {
        bail!("text must be 1..{MAX_TEXT_CHARS} characters");
    }
    let out_dir = PathBuf::from(&r.out_dir);
    if !out_dir.is_absolute() {
        bail!("out_dir must be an absolute path");
    }
    std::fs::create_dir_all(&out_dir)?;
    let personality = personality_string(&r.personality);
    let age = r.age.unwrap_or(18);
    let (tx, rx) = std::sync::mpsc::channel::<Vec<u8>>();
    std::thread::spawn(move || {
        let guard = state.engine.lock().unwrap();
        let Some(engine) = guard.as_ref() else { return };
        let voice = engine.voices.choose(&personality, age);
        let path = cache_path(&out_dir, &voice, &text);
        let started = std::time::Instant::now();
        let mut first_audio = None;
        let mut trim = StreamTrim::new(SAMPLE_RATE);
        let mut sink = |pcm: &[f32]| -> Result<()> {
            let out = trim.push(pcm);
            if !out.is_empty() {
                first_audio.get_or_insert(started.elapsed().as_secs_f64());
                tx.send(pcm16(&out)).map_err(|_| anyhow::anyhow!("listener hung up"))?;
            }
            Ok(())
        };
        match engine.speak_stream(&text, &personality, age, GenConfig::default(), &mut sink) {
            Ok(mut speech) => {
                let tail = trim.finish();
                if !tail.is_empty() {
                    let _ = tx.send(pcm16(&tail));
                }
                drop(tx);
                if !speech.samples.is_empty() && rms(&speech.samples) >= 0.002 && !cached(&path) {
                    trim_silence(&mut speech.samples, SAMPLE_RATE);
                    peak_normalize(&mut speech.samples);
                    if let Ok(ogg) = encode_ogg(&speech.samples, SAMPLE_RATE) {
                        let _ = write_atomic(&path, &ogg);
                    }
                }
                state.stats.generated.fetch_add(1, Ordering::Relaxed);
                state.stats.gen_ms.fetch_add((speech.gen_seconds * 1000.0) as u64, Ordering::Relaxed);
                crate::log!("ok-tts: STREAM p={} chars={chars} first_audio={:.2}s gen={:.2}s audio={:.2}s {text}", speech.voice_key,
                          first_audio.unwrap_or(0.0), speech.gen_seconds, speech.samples.len() as f64 / SAMPLE_RATE as f64);
            }
            Err(e) => crate::log!("ok-tts: stream stopped: {e:#}"),
        }
    });
    Ok(ChannelReader { rx, buf: Vec::new(), pos: 0 })
}

fn cached(path: &Path) -> bool {
    std::fs::metadata(path).map(|m| m.len() > 1000).unwrap_or(false)
}

fn percent_decode(v: &str) -> String {
    let bytes = v.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(b) = u8::from_str_radix(&v[i + 1..i + 3], 16) {
                out.push(b);
                i += 3;
                continue;
            }
        }
        out.push(if bytes[i] == b'+' { b' ' } else { bytes[i] });
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}
