//! Local HTTP API for StreetSocial voice input. Binds 127.0.0.1 only; the model loads in the background.
//!   GET  /health
//!   GET  /devices                       {"devices": [...], "current": "..."} (system default first)
//!   POST /device {"device": "name"}     use this microphone from now on ("" = system default)
//!   POST /record/start                  start the default microphone
//!   POST /record/stop                   stop and transcribe -> {"ok", "text", "language", "audio_seconds", "asr_seconds"}
//!   POST /record/cancel                 stop and discard
//!   POST /transcribe {"path": "x.wav"}  transcribe a file (tests)
//!   POST /transcribe_spans {"path": "song.ogg", "spans": [[s, e], ...]} -> {"texts": [...]}  each span on its own (song captions)
//!   POST /listen/start                  hands-free mode: keep the microphone open, cut utterances by voice activity
//!   POST /listen/stop
//!   POST /listen/mute {"mute": true}    drop audio while the game plays her voice (no echo transcripts)
//!        {"mute": false, "duck": true}  or keep listening for the player talking over her, at a raised threshold
//!   GET  /listen/poll?after=N           {"listening", "speaking", "muted", "level", "results": [{"id", "text", ...}]}
//! Optional body {"language": "Chinese"} on /record/stop and /transcribe forces the language.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::channel;
use std::sync::{Arc, Mutex};

use anyhow::{bail, Result};
use serde::Deserialize;
use serde_json::{json, Value};

use crate::audio::{input_devices, read_wav, rms, to_model_rate, DeviceChoice, Recorder};
use crate::engine::{Engine, MIN_SAMPLES, MODEL_ID};
use crate::listen::{Listener, Utterance};

/// Hands-free results kept for polling.
const HEARD_KEEP: usize = 50;

#[derive(Default)]
struct Stats {
    requests: AtomicU64,
    failed: AtomicU64,
    asr_ms: AtomicU64,
}

struct State {
    engine: Mutex<Option<Engine>>,
    ready: AtomicBool,
    load_error: Mutex<Option<String>>,
    recorder: Recorder,
    listener: Listener,
    device: DeviceChoice,
    heard: Mutex<Vec<Value>>,
    heard_id: AtomicU64,
    stats: Stats,
}

#[derive(Deserialize, Default)]
struct Options {
    #[serde(default)]
    path: Option<String>,
    /// The player's language for how Chinese is shown (text::set_script).
    #[serde(default)]
    script: Option<String>,
    #[serde(default)]
    language: Option<String>,
    /// /transcribe_spans: [start, end] seconds of `path`, each transcribed on its own.
    #[serde(default)]
    spans: Option<Vec<[f64; 2]>>,
    #[serde(default)]
    mute: Option<bool>,
    duck: Option<bool>,
    #[serde(default)]
    device: Option<String>,
}

pub fn serve(port: u16, device: Option<String>, load: impl FnOnce() -> Result<Engine> + Send + 'static) -> Result<()> {
    let server = Arc::new(tiny_http::Server::http(("127.0.0.1", port)).map_err(|e| anyhow::anyhow!("bind 127.0.0.1:{port}: {e}"))?);
    crate::log!("ok-asr: microphone {}", device.as_deref().map(|d| format!("matching \"{d}\"")).unwrap_or_else(|| "= system default".into()));
    let (utterances, heard_rx) = channel::<Utterance>();
    let device: DeviceChoice = Arc::new(Mutex::new(device));
    let state = Arc::new(State {
        engine: Mutex::new(None),
        ready: AtomicBool::new(false),
        load_error: Mutex::new(None),
        recorder: Recorder::new(device.clone()),
        device: device.clone(),
        listener: Listener::new(device, move |u| {
            let _ = utterances.send(u);
        }),
        heard: Mutex::new(Vec::new()),
        heard_id: AtomicU64::new(0),
        stats: Stats::default(),
    });
    {
        // Hands-free utterances are transcribed one by one, in the order they were spoken.
        let state = state.clone();
        std::thread::spawn(move || {
            while let Ok(u) = heard_rx.recv() {
                if !state.ready.load(Ordering::SeqCst) {
                    continue;
                }
                match recognize(&state, &u.samples, u.rate, None) {
                    Ok(mut v) if v["speech"].as_bool().unwrap_or(false) => {
                        v["id"] = json!(state.heard_id.fetch_add(1, Ordering::SeqCst) + 1);
                        let mut heard = state.heard.lock().unwrap();
                        heard.push(v);
                        let extra = heard.len().saturating_sub(HEARD_KEEP);
                        heard.drain(..extra);
                    }
                    Ok(_) => {}
                    Err(e) => crate::log!("ok-asr: hands-free FAIL {e:#}"),
                }
            }
        });
    }
    crate::log!("ok-asr: listening on http://127.0.0.1:{port} (model loads in the background)");
    {
        let state = state.clone();
        std::thread::spawn(move || match load().and_then(warmup) {
            Ok(engine) => {
                *state.engine.lock().unwrap() = Some(engine);
                state.ready.store(true, Ordering::SeqCst);
                crate::log!("ok-asr: READY");
            }
            Err(e) => {
                crate::log!("ok-asr: LOAD FAILED: {e:#}");
                *state.load_error.lock().unwrap() = Some(format!("{e:#}"));
                std::process::exit(1);
            }
        });
    }
    let mut handles = Vec::new();
    for _ in 0..4 {
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

/// One silent pass so NVRTC kernels and cuBLAS heuristics are warm before the first real request.
fn warmup(engine: Engine) -> Result<Engine> {
    let t = std::time::Instant::now();
    let tone: Vec<f32> = (0..16_000).map(|i| (i as f32 * 0.05).sin() * 0.01).collect();
    let _ = engine.transcribe(&tone, None)?;
    crate::log!("ok-asr: warmup {:.2}s", t.elapsed().as_secs_f64());
    Ok(engine)
}

fn respond(req: tiny_http::Request, code: u16, body: Value) {
    let data = body.to_string().into_bytes();
    let header = tiny_http::Header::from_bytes(&b"Content-Type"[..], &b"application/json; charset=utf-8"[..]).unwrap();
    let _ = req.respond(tiny_http::Response::from_data(data).with_status_code(code).with_header(header));
}

fn read_options(req: &mut tiny_http::Request) -> Result<Options> {
    if req.body_length().unwrap_or(0) > 65536 {
        bail!("body must be at most 65536 bytes");
    }
    let mut body = String::new();
    std::io::Read::read_to_string(req.as_reader(), &mut body)?;
    let body = body.trim_start_matches('\u{feff}').trim();
    Ok(if body.is_empty() { Options::default() } else { serde_json::from_str(body)? })
}

fn handle(state: &State, mut req: tiny_http::Request) {
    let url = req.url().trim_end_matches('/').to_string();
    let ready = state.ready.load(Ordering::SeqCst);
    match (req.method(), url.as_str()) {
        (tiny_http::Method::Get, "/health") => {
            let s = &state.stats;
            respond(req, 200, json!({
                "ok": ready, "ready": ready, "model": MODEL_ID, "engine": "ok-asr (Rust, CUDA)",
                "recording": state.recorder.recording(),
                "error": state.load_error.lock().unwrap().clone(),
                "requests": s.requests.load(Ordering::Relaxed), "failed": s.failed.load(Ordering::Relaxed),
                "asr_seconds": s.asr_ms.load(Ordering::Relaxed) as f64 / 1000.0,
            }));
        }
        (tiny_http::Method::Post, "/listen/start") => match read_options(&mut req).map(|o| {
            if let Some(s) = o.script {
                crate::text::set_script(&s);
            }
        }).and_then(|_| state.listener.start()) {
            Ok(device) => {
                crate::log!("ok-asr: hands-free listening on {device}");
                respond(req, 200, json!({"ok": true, "device": device}));
            }
            Err(e) => respond(req, 409, json!({"ok": false, "error": format!("{e:#}")})),
        },
        (tiny_http::Method::Post, "/listen/stop") => {
            state.listener.stop();
            respond(req, 200, json!({"ok": true}));
        }
        (tiny_http::Method::Post, "/listen/inject") => {
            // Development: {"path": wav} is heard as if spoken into the microphone (hands-free must be on).
            let mut body = String::new();
            let _ = std::io::Read::read_to_string(req.as_reader(), &mut body);
            let path = serde_json::from_str::<serde_json::Value>(body.trim_start_matches('\u{feff}')).ok()
                .and_then(|v| v.get("path").and_then(|p| p.as_str()).map(str::to_owned));
            match path.map(|p| crate::audio::read_wav(std::path::Path::new(&p))) {
                Some(Ok((samples, rate))) => {
                    state.listener.inject(samples, rate);
                    respond(req, 200, json!({"ok": true}));
                }
                Some(Err(e)) => respond(req, 400, json!({"ok": false, "error": format!("{e:#}")})),
                None => respond(req, 400, json!({"ok": false, "error": "path required"})),
            }
        }
        (tiny_http::Method::Post, "/listen/mute") => match read_options(&mut req) {
            Ok(o) => {
                state.listener.set_muted(o.mute.unwrap_or(true));
                state.listener.set_ducked(o.duck.unwrap_or(false));
                respond(req, 200, json!({"ok": true}));
            }
            Err(e) => respond(req, 400, json!({"ok": false, "error": format!("{e:#}")})),
        },
        (tiny_http::Method::Get, path) if path.starts_with("/listen/poll") => {
            let after: u64 = req.url().split_once("after=").and_then(|(_, v)| v.split('&').next()?.parse().ok()).unwrap_or(0);
            let st = state.listener.status();
            let results: Vec<Value> = state.heard.lock().unwrap().iter().filter(|v| v["id"].as_u64().unwrap_or(0) > after).cloned().collect();
            respond(req, 200, json!({"ok": true, "ready": ready, "listening": st.active, "muted": st.muted, "ducked": st.ducked, "speaking": st.speaking,
                                     "level": st.level, "device": st.device, "results": results}));
        }
        (tiny_http::Method::Get, "/devices") => {
            let st = state.listener.status();
            let devices = input_devices();
            // The device in use while listening, else the configured choice resolved like open_stream does.
            let wanted = state.device.lock().unwrap().clone().map(|d| d.to_lowercase()).filter(|d| !d.is_empty());
            let current = if st.active { st.device } else {
                wanted.and_then(|w| devices.iter().find(|d| d.to_lowercase().contains(&w)).cloned()).or_else(|| devices.first().cloned()).unwrap_or_default()
            };
            respond(req, 200, json!({"ok": true, "devices": devices, "current": current}));
        }
        (tiny_http::Method::Post, "/device") => match read_options(&mut req) {
            Ok(o) => {
                let name = o.device.unwrap_or_default();
                *state.device.lock().unwrap() = (!name.trim().is_empty()).then(|| name.clone());
                crate::log!("ok-asr: microphone set to {}", if name.is_empty() { "system default" } else { &name });
                match state.listener.restart() {
                    Ok(()) => respond(req, 200, json!({"ok": true, "device": name})),
                    Err(e) => respond(req, 409, json!({"ok": false, "error": format!("{e:#}")})),
                }
            }
            Err(e) => respond(req, 400, json!({"ok": false, "error": format!("{e:#}")})),
        },
        (tiny_http::Method::Post, "/record/start") => match state.recorder.start() {
            Ok(device) => {
                crate::log!("ok-asr: recording from {device}");
                respond(req, 200, json!({"ok": true, "device": device}));
            }
            Err(e) => respond(req, 409, json!({"ok": false, "error": format!("{e:#}")})),
        },
        (tiny_http::Method::Post, "/record/cancel") => {
            let _ = state.recorder.stop();
            respond(req, 200, json!({"ok": true}));
        }
        // A song's lines as she sang them: each [start, end] span of the file transcribed on its own (the game lines its
        // captions up with these; OKStreetSocialSong.inl).
        (tiny_http::Method::Post, "/transcribe_spans") => {
            state.stats.requests.fetch_add(1, Ordering::Relaxed);
            let result = read_options(&mut req).and_then(|opts| {
                if !ready {
                    bail!("model is still loading");
                }
                let path = PathBuf::from(opts.path.clone().unwrap_or_default());
                if !path.is_absolute() {
                    bail!("path must be an absolute WAV or OGG path");
                }
                let spans = opts.spans.clone().ok_or_else(|| anyhow::anyhow!("spans is required"))?;
                let (samples, rate) = crate::audio::read_audio(&path)?;
                let at = |s: f64| ((s.max(0.0) * rate as f64) as usize).min(samples.len());
                let mut texts = Vec::with_capacity(spans.len());
                for [start, end] in spans {
                    let (a, b) = (at(start - 0.1), at(end + 0.1));
                    let r = if b > a { recognize(state, &samples[a..b], rate, opts.language.as_deref())? } else { json!({"text": ""}) };
                    texts.push(r["text"].as_str().unwrap_or("").to_string());
                }
                Ok(json!({"ok": true, "texts": texts}))
            });
            match result {
                Ok(v) => respond(req, 200, v),
                Err(e) => {
                    state.stats.failed.fetch_add(1, Ordering::Relaxed);
                    crate::log!("ok-asr: FAIL {e:#}");
                    respond(req, 400, json!({"ok": false, "error": format!("{e:#}")}));
                }
            }
        }
        (tiny_http::Method::Post, "/record/stop") | (tiny_http::Method::Post, "/transcribe") => {
            state.stats.requests.fetch_add(1, Ordering::Relaxed);
            let from_mic = url == "/record/stop";
            let result = read_options(&mut req).and_then(|opts| {
                let (samples, rate) = if from_mic {
                    let r = state.recorder.stop()?;
                    crate::log!("ok-asr: recorded {:.2}s at {} Hz from {}", r.samples.len() as f64 / r.rate as f64, r.rate, r.device);
                    (r.samples, r.rate)
                } else {
                    let path = PathBuf::from(opts.path.clone().unwrap_or_default());
                    if !path.is_absolute() {
                        bail!("path must be an absolute WAV path");
                    }
                    read_wav(&path)?
                };
                if !ready {
                    bail!("model is still loading");
                }
                recognize(state, &samples, rate, opts.language.as_deref())
            });
            match result {
                Ok(v) => respond(req, 200, v),
                Err(e) => {
                    state.stats.failed.fetch_add(1, Ordering::Relaxed);
                    crate::log!("ok-asr: FAIL {e:#}");
                    respond(req, 400, json!({"ok": false, "error": format!("{e:#}")}));
                }
            }
        }
        _ => respond(req, 404, json!({"ok": false, "error": "not found"})),
    }
}

fn recognize(state: &State, samples: &[f32], rate: u32, language: Option<&str>) -> Result<Value> {
    let audio = to_model_rate(samples, rate);
    let seconds = audio.len() as f64 / 16_000.0;
    // A tap of the button or a silent room: report no speech instead of letting the model guess.
    if audio.len() < MIN_SAMPLES || rms(&audio) < 0.003 {
        return Ok(json!({"ok": true, "text": "", "language": "", "audio_seconds": seconds, "asr_seconds": 0.0, "speech": false}));
    }
    let guard = state.engine.lock().unwrap();
    let engine = guard.as_ref().ok_or_else(|| anyhow::anyhow!("model is not loaded"))?;
    let t = engine.transcribe(&audio, language)?;
    drop(guard);
    state.stats.asr_ms.fetch_add((t.asr_seconds * 1000.0) as u64, Ordering::Relaxed);
    crate::log!("ok-asr: ASR audio={:.2}s asr={:.2}s lang={} {}", t.audio_seconds, t.asr_seconds, t.language, t.text);
    Ok(json!({"ok": true, "text": t.text, "language": t.language, "raw": t.raw, "speech": !t.text.is_empty(),
              "audio_seconds": (t.audio_seconds * 1000.0).round() / 1000.0, "asr_seconds": (t.asr_seconds * 1000.0).round() / 1000.0}))
}
