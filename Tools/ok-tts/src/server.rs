//! Local HTTP API used by StreetSocial (same contract as the earlier E:\TTS\tts_server.py):
//!   GET  /health -> {"ok", "ready", "model", "protocol", "capabilities", counters...}
//!   GET  /voice?personality=17&age=18 -> the voice design a line would use (answers while the model loads)
//!   POST /synthesize {"text", "personality", "age", "out_dir"} -> {"ok", "path", "cached", "gen_seconds", "audio_seconds", ...}
//!   POST /synthesize_stream (same body) -> 16-bit little-endian mono PCM at 24 kHz, sent in pieces while it is
//!        generated (first audio after ~4 frames). A line already in the cache is streamed from its file. The finished
//!        line is written to the cache, so a following /synthesize for it answers with its path without generating.
//!   POST /cancel {"channel", "below"} -> requests of that channel with generation < below stop (queued or generating)
//!   GET  /requests[?id=..] -> what became of a request (complete / cancelled / hangup / failed) and its timings
//! Optional request fields (protocol 2): "request_id", "channel", "generation", "segment_index", and for measuring
//! "stream_later_frames".
//! Binds 127.0.0.1 only. The model loads in the background; /health reports ready=false until then, and cached lines
//! are answered meanwhile.
//!
//! One engine does one synthesis at a time (it owns the GPU state). Everything else stays outside its lock: voice
//! choice, cache lookups, OGG encoding and writing. Requests for the same line share one synthesis: the second
//! waits for the first one's file instead of generating it again.

use std::collections::{HashMap, HashSet, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use serde::Deserialize;
use serde_json::{json, Value};
use sha1_smol::Sha1;

use crate::audio::{decode_ogg, encode_ogg, pcm16, peak_normalize, rms, trim_silence, write_atomic, StreamTrim};
use crate::engine::{Engine, MODEL_ID, SAMPLE_RATE};
use crate::talker::GenConfig;
use crate::text::{clean_line, VoiceChoice, Voices};

const MAX_TEXT_CHARS: usize = 400;
const CACHE_VERSION: &str = "ok-tts-3"; // 3: one seed per voice
const PROTOCOL: u32 = 2;
const CAPABILITIES: &[&str] = &["stream", "stream_cache_hit", "shared_synthesis", "cancel", "request_status", "voice_while_loading"];
/// How long a request waits for another one making the same line before it tries itself.
const JOIN_WAIT: Duration = Duration::from_secs(60);
const RECENT: usize = 256;

#[derive(Default)]
struct Stats {
    requests: AtomicU64,
    generated: AtomicU64,
    cached: AtomicU64,
    joined: AtomicU64,
    cancelled: AtomicU64,
    failed: AtomicU64,
    gen_ms: AtomicU64,
}

/// One line being made; requests for the same cache file wait on it. `done`: Some(true) = the file is there.
struct Flight {
    done: Mutex<Option<bool>>,
    cv: Condvar,
}

struct State {
    engine: Mutex<Option<Engine>>,
    voices: Voices,
    ready: AtomicBool,
    load_error: Mutex<Option<String>>,
    stats: Stats,
    inflight: Mutex<HashMap<PathBuf, Arc<Flight>>>,
    /// channel -> lowest generation still wanted
    cancel_floor: Mutex<HashMap<String, u64>>,
    active: Mutex<HashSet<String>>,
    recent: Mutex<VecDeque<Value>>,
    next_id: AtomicU64,
}

#[derive(Deserialize)]
struct SynthRequest {
    text: String,
    #[serde(default)]
    personality: Value,
    #[serde(default)]
    age: Option<u32>,
    out_dir: String,
    #[serde(default)]
    request_id: Option<String>,
    #[serde(default)]
    channel: Option<String>,
    #[serde(default)]
    generation: Option<u64>,
    #[serde(default)]
    segment_index: Option<i64>,
    /// Frames per streamed piece after the first, for this request only (measuring; 1..=25).
    #[serde(default)]
    stream_later_frames: Option<usize>,
}

/// A validated request: the cleaned line, its voice and its cache file.
struct Job {
    id: String,
    text: String,
    chars: usize,
    personality: String,
    age: u32,
    voice: VoiceChoice,
    path: PathBuf,
    channel: Option<String>,
    generation: Option<u64>,
    segment: Option<i64>,
    later_frames: Option<usize>,
}

/// Times of one request, in seconds since the server received it (one process, one monotonic clock).
struct Trace {
    kind: &'static str,
    received: Instant,
    marks: Vec<(&'static str, f64)>,
}

impl Trace {
    fn new(kind: &'static str, received: Instant) -> Self {
        Self { kind, received, marks: Vec::new() }
    }

    fn mark(&mut self, name: &'static str) {
        let t = self.received.elapsed().as_secs_f64();
        self.marks.push((name, t));
    }

    /// Log the request's outcome and keep it for /requests.
    fn finish(mut self, state: &State, job: &Job, status: &str, cache: &str, extra: Value) {
        self.mark("end");
        let marks: serde_json::Map<String, Value> = self.marks.iter().map(|(k, v)| (k.to_string(), json!((v * 1000.0).round() / 1000.0))).collect();
        let times: Vec<String> = self.marks.iter().map(|(k, v)| format!("{k}={v:.3}")).collect();
        crate::log!("ok-tts: REQ id={} kind={} status={status} cache={cache} p={} chars={} seg={} {} {}", job.id, self.kind, job.voice.key, job.chars,
                    job.segment.map(|s| s.to_string()).unwrap_or_else(|| "-".into()), times.join(" "), job.text);
        let mut record = json!({"request_id": job.id, "kind": self.kind, "status": status, "cache": cache, "text": job.text,
                                "personality": job.voice.key, "age": job.voice.age, "channel": job.channel, "generation": job.generation,
                                "segment_index": job.segment, "times": marks});
        if let (Some(r), Some(e)) = (record.as_object_mut(), extra.as_object()) {
            r.extend(e.clone());
        }
        state.active.lock().unwrap().remove(&job.id);
        let mut recent = state.recent.lock().unwrap();
        if recent.len() >= RECENT {
            recent.pop_front();
        }
        recent.push_back(record);
    }
}

pub fn serve(port: u16, workers: usize, voices: Voices, load: impl FnOnce() -> Result<Engine> + Send + 'static) -> Result<()> {
    let server = tiny_http::Server::http(("127.0.0.1", port)).map_err(|e| anyhow::anyhow!("bind 127.0.0.1:{port}: {e}"))?;
    let server = Arc::new(server);
    let state = Arc::new(State {
        engine: Mutex::new(None), voices, ready: AtomicBool::new(false), load_error: Mutex::new(None), stats: Stats::default(),
        inflight: Mutex::new(HashMap::new()), cancel_floor: Mutex::new(HashMap::new()), active: Mutex::new(HashSet::new()),
        recent: Mutex::new(VecDeque::new()), next_id: AtomicU64::new(1),
    });
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

fn read_body(req: &mut tiny_http::Request) -> Result<String> {
    if req.body_length().unwrap_or(0) > 65536 {
        bail!("body must be at most 65536 bytes");
    }
    let mut body = String::new();
    std::io::Read::read_to_string(req.as_reader(), &mut body)?;
    Ok(body.trim_start_matches('\u{feff}').to_string())
}

fn query_pairs(url: &str) -> Vec<(String, String)> {
    let query = url.split_once('?').map(|(_, q)| q).unwrap_or("");
    query.split('&').filter_map(|p| p.split_once('=')).map(|(k, v)| (k.to_string(), percent_decode(v))).collect()
}

fn error_code(e: &anyhow::Error) -> u16 {
    if format!("{e:#}").contains("still loading") { 503 } else { 400 }
}

fn handle(state: &Arc<State>, mut req: tiny_http::Request) {
    let received = Instant::now();
    let url = req.url().to_string();
    let path = url.split('?').next().unwrap_or("").trim_end_matches('/').to_string();
    match (req.method(), path.as_str()) {
        (tiny_http::Method::Get, "/health") => {
            let s = &state.stats;
            let ready = state.ready.load(Ordering::SeqCst);
            respond(req, 200, json!({
                "ok": ready, "ready": ready, "model": MODEL_ID, "engine": "ok-tts (Rust, CUDA)",
                "protocol": PROTOCOL, "capabilities": CAPABILITIES, "pcm": {"format": "s16le", "rate": SAMPLE_RATE, "channels": 1},
                "error": state.load_error.lock().unwrap().clone(),
                "requests": s.requests.load(Ordering::Relaxed), "generated": s.generated.load(Ordering::Relaxed),
                "cached": s.cached.load(Ordering::Relaxed), "joined": s.joined.load(Ordering::Relaxed),
                "cancelled": s.cancelled.load(Ordering::Relaxed), "failed": s.failed.load(Ordering::Relaxed),
                "gen_seconds": s.gen_ms.load(Ordering::Relaxed) as f64 / 1000.0,
            }));
        }
        (tiny_http::Method::Get, "/voice") => {
            // The prompt the next /synthesize would use (shown in the game UI).
            let mut personality = String::from("29");
            let mut age = 18u32;
            for (k, v) in query_pairs(&url) {
                match k.as_str() {
                    "personality" => personality = v,
                    "age" => age = v.parse().unwrap_or(18),
                    _ => {}
                }
            }
            let v = state.voices.choose(&personality, age);
            respond(req, 200, json!({"ok": true, "personality": v.key, "age": v.age, "name": v.name, "label": v.label, "instruct": v.instruct}));
        }
        (tiny_http::Method::Get, "/requests") => {
            let id = query_pairs(&url).into_iter().find(|(k, _)| k == "id").map(|(_, v)| v);
            let recent = state.recent.lock().unwrap();
            match id {
                Some(id) => match recent.iter().rev().find(|r| r["request_id"] == id.as_str()) {
                    Some(r) => respond(req, 200, json!({"ok": true, "request": r})),
                    None if state.active.lock().unwrap().contains(&id) => respond(req, 200, json!({"ok": true, "request": {"request_id": id, "status": "running"}})),
                    None => respond(req, 404, json!({"ok": false, "error": "unknown request"})),
                },
                None => {
                    let list: Vec<&Value> = recent.iter().rev().take(50).collect();
                    respond(req, 200, json!({"ok": true, "requests": list}));
                }
            }
        }
        (tiny_http::Method::Post, "/cancel") => {
            let parsed = read_body(&mut req).and_then(|b| Ok(serde_json::from_str::<Value>(&b)?));
            match parsed {
                Ok(v) => {
                    let (Some(channel), Some(below)) = (v["channel"].as_str(), v["below"].as_u64()) else {
                        respond(req, 400, json!({"ok": false, "error": "channel and below are required"}));
                        return;
                    };
                    let mut floors = state.cancel_floor.lock().unwrap();
                    let floor = floors.entry(channel.to_string()).or_insert(0);
                    *floor = (*floor).max(below);
                    respond(req, 200, json!({"ok": true, "channel": channel, "below": *floor}));
                }
                Err(e) => respond(req, 400, json!({"ok": false, "error": format!("{e:#}")})),
            }
        }
        (tiny_http::Method::Post, "/synthesize_stream") => {
            state.stats.requests.fetch_add(1, Ordering::Relaxed);
            let parsed = read_body(&mut req).and_then(|b| Ok(serde_json::from_str::<SynthRequest>(&b)?));
            match parsed.and_then(|r| stream_synthesis(state.clone(), r, received)) {
                Ok((reader, id, cache)) => {
                    let headers = vec![
                        tiny_http::Header::from_bytes(&b"Content-Type"[..], &b"audio/L16; rate=24000; channels=1"[..]).unwrap(),
                        tiny_http::Header::from_bytes(&b"X-Sample-Rate"[..], SAMPLE_RATE.to_string().as_bytes()).unwrap(),
                        tiny_http::Header::from_bytes(&b"X-Sample-Format"[..], &b"s16le"[..]).unwrap(),
                        tiny_http::Header::from_bytes(&b"X-Request-Id"[..], id.as_bytes()).unwrap(),
                        tiny_http::Header::from_bytes(&b"X-Cache"[..], cache.as_bytes()).unwrap(),
                    ];
                    let _ = req.respond(tiny_http::Response::new(tiny_http::StatusCode(200), headers, reader, None, None));
                }
                Err(e) => {
                    state.stats.failed.fetch_add(1, Ordering::Relaxed);
                    crate::log!("ok-tts: STREAM FAIL {e:#}");
                    respond(req, error_code(&e), json!({"ok": false, "error": format!("{e:#}")}));
                }
            }
        }
        (tiny_http::Method::Post, "/synthesize") => {
            state.stats.requests.fetch_add(1, Ordering::Relaxed);
            let result = read_body(&mut req).and_then(|b| synthesize(state, serde_json::from_str(&b)?, received));
            match result {
                Ok(v) => respond(req, 200, v),
                Err(e) => {
                    state.stats.failed.fetch_add(1, Ordering::Relaxed);
                    crate::log!("ok-tts: FAIL {e:#}");
                    respond(req, error_code(&e), json!({"ok": false, "error": format!("{e:#}")}));
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

fn prepare(state: &State, r: SynthRequest) -> Result<Job> {
    let text = clean_line(&r.text);
    let chars = text.chars().count();
    if chars == 0 || chars > MAX_TEXT_CHARS {
        bail!("text must be 1..{MAX_TEXT_CHARS} characters");
    }
    let out_dir = PathBuf::from(&r.out_dir);
    if !out_dir.is_absolute() {
        bail!("out_dir must be an absolute path");
    }
    let personality = personality_string(&r.personality);
    let age = r.age.unwrap_or(18);
    let voice = state.voices.choose(&personality, age);
    let path = cache_path(&out_dir, &voice, &text);
    let id = r.request_id.filter(|s| !s.is_empty() && s.len() <= 128)
        .unwrap_or_else(|| format!("s{}", state.next_id.fetch_add(1, Ordering::Relaxed)));
    state.active.lock().unwrap().insert(id.clone());
    Ok(Job { id, text, chars, personality, age, voice, path, channel: r.channel, generation: r.generation, segment: r.segment_index,
            later_frames: r.stream_later_frames.map(|n| n.clamp(1, 25)) })
}

/// The caller has moved on (a newer conversation generation was announced on /cancel).
fn cancelled(state: &State, job: &Job) -> bool {
    match (&job.channel, job.generation) {
        (Some(c), Some(g)) => state.cancel_floor.lock().unwrap().get(c).is_some_and(|floor| g < *floor),
        _ => false,
    }
}

enum Claim {
    Owner(FlightGuard),
    Wait(Arc<Flight>),
}

/// The right to make one line; dropping it without `complete` tells the waiters it failed.
struct FlightGuard {
    state: Arc<State>,
    path: PathBuf,
    flight: Arc<Flight>,
    ok: bool,
}

impl FlightGuard {
    fn complete(mut self) {
        self.ok = true;
    }
}

impl Drop for FlightGuard {
    fn drop(&mut self) {
        self.state.inflight.lock().unwrap().remove(&self.path);
        *self.flight.done.lock().unwrap() = Some(self.ok);
        self.flight.cv.notify_all();
    }
}

fn claim(state: &Arc<State>, path: &Path) -> Claim {
    let mut map = state.inflight.lock().unwrap();
    if let Some(f) = map.get(path) {
        return Claim::Wait(f.clone());
    }
    let flight = Arc::new(Flight { done: Mutex::new(None), cv: Condvar::new() });
    map.insert(path.to_path_buf(), flight.clone());
    Claim::Owner(FlightGuard { state: state.clone(), path: path.to_path_buf(), flight, ok: false })
}

fn wait_flight(flight: &Flight) {
    let done = flight.done.lock().unwrap();
    let _ = flight.cv.wait_timeout_while(done, JOIN_WAIT, |d| d.is_none());
}

/// Trim, limit and write a finished line to its cache file (outside the engine lock).
fn store(samples: &mut Vec<f32>, path: &Path) -> Result<f64> {
    if samples.is_empty() || rms(samples) < 0.002 {
        bail!("synthesis was silent");
    }
    trim_silence(samples, SAMPLE_RATE);
    peak_normalize(samples);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    write_atomic(path,&encode_ogg(samples, SAMPLE_RATE)?)?;
    Ok(samples.len() as f64 / SAMPLE_RATE as f64)
}

fn synthesize(state: &Arc<State>, r: SynthRequest, received: Instant) -> Result<Value> {
    let job = prepare(state, r)?;
    let mut trace = Trace::new("file", received);
    let result = synthesize_job(state, &job, &mut trace);
    match &result {
        Ok((v, cache)) => trace.finish(state, &job, "complete", cache, json!({"gen_seconds": v["gen_seconds"], "audio_seconds": v["audio_seconds"]})),
        Err(e) => {
            let status = if format!("{e:#}").contains("cancelled") { "cancelled" } else { "failed" };
            trace.finish(state, &job, status, "miss", json!({"error": format!("{e:#}")}));
        }
    }
    result.map(|(v, _)| v)
}

fn synthesize_job(state: &Arc<State>, job: &Job, trace: &mut Trace) -> Result<(Value, &'static str)> {
    let base = |cached: bool| json!({"ok": true, "path": job.path, "cached": cached, "text": job.text, "personality": job.voice.key,
                                      "age": job.voice.age, "request_id": job.id});
    let mut joined = false;
    for _ in 0..3 {
        if cached(&job.path) {
            let stat = if joined { &state.stats.joined } else { &state.stats.cached };
            stat.fetch_add(1, Ordering::Relaxed);
            let mut v = base(true);
            v["gen_seconds"] = json!(0.0);
            return Ok((v, if joined { "joined" } else { "hit" }));
        }
        if cancelled(state, job) {
            state.stats.cancelled.fetch_add(1, Ordering::Relaxed);
            bail!("cancelled");
        }
        let guard = match claim(state, &job.path) {
            Claim::Wait(f) => {
                trace.mark("join");
                wait_flight(&f);
                joined = true;
                continue;
            }
            Claim::Owner(g) => g,
        };
        if !state.ready.load(Ordering::SeqCst) {
            bail!("model is still loading");
        }
        trace.mark("queue_enter");
        let lock = state.engine.lock().unwrap();
        trace.mark("queue_leave");
        if cancelled(state, job) {
            state.stats.cancelled.fetch_add(1, Ordering::Relaxed);
            bail!("cancelled");
        }
        let engine = lock.as_ref().ok_or_else(|| anyhow::anyhow!("model is not loaded"))?;
        let mut speech = engine.speak(&job.text, &job.personality, job.age, GenConfig::default())?;
        drop(lock);
        trace.mark("generated");
        let audio_seconds = store(&mut speech.samples, &job.path)?;
        guard.complete();
        trace.mark("cache_ready");
        state.stats.generated.fetch_add(1, Ordering::Relaxed);
        state.stats.gen_ms.fetch_add((speech.gen_seconds * 1000.0) as u64, Ordering::Relaxed);
        crate::log!("ok-tts: GEN p={} age={} chars={} gen={:.2}s audio={audio_seconds:.2}s {}", speech.voice_key, speech.age, job.chars, speech.gen_seconds, job.text);
        let mut v = base(false);
        v["gen_seconds"] = json!((speech.gen_seconds * 1000.0).round() / 1000.0);
        v["audio_seconds"] = json!((audio_seconds * 1000.0).round() / 1000.0);
        return Ok((v, "miss"));
    }
    bail!("another request for this line did not finish it")
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

/// A cached line as one PCM piece (the same trimmed, limited audio the file replays).
fn cached_pcm(path: &Path) -> Result<Vec<u8>> {
    Ok(pcm16(&decode_ogg(&std::fs::read(path)?)?))
}

/// Streams a line: from its cache file when there is one, else generated on its own thread (holding the
/// engine) and fed to the response as audio comes out. A client that hangs up, or a cancelled generation,
/// stops the synthesis at the next piece.
fn stream_synthesis(state: Arc<State>, r: SynthRequest, received: Instant) -> Result<(ChannelReader, String, &'static str)> {
    let job = prepare(&state, r)?;
    let (tx, rx) = std::sync::mpsc::channel::<Vec<u8>>();
    let reader = ChannelReader { rx, buf: Vec::new(), pos: 0 };
    let id = job.id.clone();
    let mut trace = Trace::new("stream", received);
    if cached(&job.path) {
        if let Ok(pcm) = cached_pcm(&job.path) {
            trace.mark("first_pcm");
            let audio = pcm.len() as f64 / 2.0 / SAMPLE_RATE as f64;
            let _ = tx.send(pcm);
            state.stats.cached.fetch_add(1, Ordering::Relaxed);
            trace.finish(&state, &job, "complete", "hit", json!({"audio_seconds": audio}));
            return Ok((reader, id, "hit"));
        }
    }
    if !state.ready.load(Ordering::SeqCst) && !state.inflight.lock().unwrap().contains_key(&job.path) {
        state.active.lock().unwrap().remove(&job.id);
        bail!("model is still loading");
    }
    std::thread::spawn(move || {
        let (status, cache, extra) = stream_job(&state, &job, &tx, &mut trace);
        drop(tx);
        if status == "cancelled" {
            state.stats.cancelled.fetch_add(1, Ordering::Relaxed);
        }
        trace.finish(&state, &job, status, cache, extra);
    });
    Ok((reader, id, "miss"))
}

fn stream_job(state: &Arc<State>, job: &Job, tx: &std::sync::mpsc::Sender<Vec<u8>>, trace: &mut Trace) -> (&'static str, &'static str, Value) {
    let mut joined = false;
    for _ in 0..3 {
        if cached(&job.path) {
            return match cached_pcm(&job.path) {
                Ok(pcm) => {
                    trace.mark("first_pcm");
                    let audio = pcm.len() as f64 / 2.0 / SAMPLE_RATE as f64;
                    let _ = tx.send(pcm);
                    (if joined { &state.stats.joined } else { &state.stats.cached }).fetch_add(1, Ordering::Relaxed);
                    ("complete", if joined { "joined" } else { "hit" }, json!({"audio_seconds": audio}))
                }
                Err(e) => ("failed", "hit", json!({"error": format!("cached file: {e:#}")})),
            };
        }
        if cancelled(state, job) {
            return ("cancelled", "miss", json!({}));
        }
        let guard = match claim(state, &job.path) {
            Claim::Wait(f) => {
                trace.mark("join");
                wait_flight(&f);
                joined = true;
                continue;
            }
            Claim::Owner(g) => g,
        };
        trace.mark("queue_enter");
        let lock = state.engine.lock().unwrap();
        trace.mark("queue_leave");
        if cancelled(state, job) {
            return ("cancelled", "miss", json!({}));
        }
        let Some(engine) = lock.as_ref() else { return ("failed", "miss", json!({"error": "model is not loaded"})) };
        let mut trim = StreamTrim::new(SAMPLE_RATE);
        let mut first_pcm = None;
        let mut sink = |pcm: &[f32]| -> Result<()> {
            if cancelled(state, job) {
                bail!("cancelled");
            }
            let out = trim.push(pcm);
            if !out.is_empty() {
                first_pcm.get_or_insert_with(|| job_time(trace));
                tx.send(pcm16(&out)).map_err(|_| anyhow::anyhow!("listener hung up"))?;
            }
            Ok(())
        };
        let result = engine.speak_stream(&job.text, &job.personality, job.age, GenConfig::default(), job.later_frames, &mut sink);
        drop(sink);
        let speech = match result {
            Ok(s) => s,
            Err(e) => {
                drop(lock);
                if let Some(t) = first_pcm {
                    trace.marks.push(("first_pcm", t));
                }
                let msg = format!("{e:#}");
                let status = if msg.contains("cancelled") { "cancelled" } else if msg.contains("hung up") { "hangup" } else { "failed" };
                if status == "failed" {
                    crate::log!("ok-tts: stream stopped: {msg}");
                }
                return (status, "miss", json!({"error": msg}));
            }
        };
        let tail = trim.finish();
        if !tail.is_empty() {
            let _ = tx.send(pcm16(&tail));
        }
        drop(lock);
        if let Some(t) = first_pcm {
            trace.marks.push(("first_pcm", t));
        }
        trace.mark("generated");
        let gen_seconds = speech.gen_seconds;
        let raw_audio = speech.samples.len() as f64 / SAMPLE_RATE as f64;
        let mut samples = speech.samples;
        let stored = store(&mut samples, &job.path);
        if stored.is_ok() {
            guard.complete();
            trace.mark("cache_ready");
        }
        state.stats.generated.fetch_add(1, Ordering::Relaxed);
        state.stats.gen_ms.fetch_add((gen_seconds * 1000.0) as u64, Ordering::Relaxed);
        let mut extra = json!({"gen_seconds": (gen_seconds * 1000.0).round() / 1000.0, "audio_seconds": (raw_audio * 1000.0).round() / 1000.0,
                               "rtf": (gen_seconds / raw_audio.max(1e-3) * 1000.0).round() / 1000.0});
        if let Err(e) = stored {
            extra["cache_error"] = json!(format!("{e:#}"));
        }
        return ("complete", "miss", extra);
    }
    ("failed", "miss", json!({"error": "another request for this line did not finish it"}))
}

fn job_time(trace: &Trace) -> f64 {
    trace.received.elapsed().as_secs_f64()
}

fn cached(path: &Path) -> bool {
    std::fs::metadata(path).map(|m| m.len() > 1000).unwrap_or(false)
}

fn percent_decode(v: &str) -> String {
    let bytes = v.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 3 <= bytes.len() {
            if let Some(b) = std::str::from_utf8(&bytes[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok()) {
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
