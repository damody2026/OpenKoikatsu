//! Local HTTP service (127.0.0.1 only):
//!   GET  /health                 schema, version, backends, busy, queue length
//!   POST /jobs                   a SongRequest -> 202 {"job_id", "status", "cache_hit"}; the same request_id with the same
//!                                body is the same job, with another body 409
//!   GET  /jobs/{id}              status, stage, seconds so far, error {code, stage, message, retryable}, manifest when ready
//!   POST /jobs/{id}/cancel       idempotent; a cancelled job never publishes
//! One heavy job runs at a time (a worker thread); the rest wait in order. Jobs live in memory: after a restart the game
//! asks again and a finished song comes straight from the cache.

use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::time::Instant;

use anyhow::Result;
use serde_json::{json, Value};

use crate::config::Config;
use crate::job::{self, Keys, Progress, SongRequest, Voice};

pub const SCHEMA: u32 = 1;

struct Job {
    id: String,
    request: SongRequest,
    voice: Option<Voice>,
    keys: Option<Keys>,
    status: String,
    stage: String,
    created: Instant,
    finished: Option<f64>,
    cache_hit: bool,
    error: Option<Value>,
    manifest: Option<Value>,
    cancel: Arc<AtomicBool>,
}

impl Job {
    fn view(&self, cfg: &Config) -> Value {
        let dir = self.keys.as_ref().map(|k| cfg.cache_dir().join(&k.artifact));
        json!({
            "job_id": self.id, "request_id": self.request.request_id, "event_id": self.request.event_id, "save_id": self.request.save_id,
            "generation": self.request.generation, "heroine_id": self.request.heroine_id,
            "voice_tag": self.voice.as_ref().map(|v| v.tag.clone()), "voice_source": self.voice.as_ref().map(|v| v.source.clone()),
            "status": self.status, "stage": self.stage, "seconds": self.finished.unwrap_or_else(|| self.created.elapsed().as_secs_f64()),
            "progress": Value::Null, "cache_hit": self.cache_hit, "error": self.error,
            "artifact_key": self.keys.as_ref().map(|k| k.artifact.clone()),
            "files": if self.status == "ready" { json!({"song": dir.as_ref().map(|d| d.join("song.ogg")), "mouth": dir.as_ref().map(|d| d.join("mouth.json")),
                                                       "lyrics": dir.as_ref().map(|d| d.join("lyrics.txt")), "manifest": dir.as_ref().map(|d| d.join("manifest.json"))}) } else { Value::Null },
            "duration_seconds": self.manifest.as_ref().map(|m| m["song"]["seconds"].clone()),
        })
    }
}

struct State {
    cfg: Config,
    jobs: Mutex<HashMap<String, Job>>,
    by_request: Mutex<HashMap<String, String>>,
    queue: Mutex<VecDeque<String>>,
    wake: Condvar,
    busy: AtomicBool,
    next: Mutex<u64>,
}

struct JobProgress<'a> {
    state: &'a State,
    id: String,
}

impl Progress for JobProgress<'_> {
    fn stage(&self, stage: &str) {
        if let Some(j) = self.state.jobs.lock().unwrap().get_mut(&self.id) {
            if j.status != "cancelled" {
                j.status = stage.to_string();
                j.stage = stage.to_string();
            }
        }
        crate::log!("ok-song: job {} -> {stage}", self.id);
    }
}

pub fn serve(cfg: Config, port: u16) -> Result<()> {
    std::fs::create_dir_all(cfg.jobs_dir())?;
    let server = Arc::new(tiny_http::Server::http(("127.0.0.1", port)).map_err(|e| anyhow::anyhow!("bind 127.0.0.1:{port}: {e}"))?);
    let state = Arc::new(State { cfg, jobs: Mutex::new(HashMap::new()), by_request: Mutex::new(HashMap::new()), queue: Mutex::new(VecDeque::new()),
                                 wake: Condvar::new(), busy: AtomicBool::new(false), next: Mutex::new(1) });
    crate::log!("ok-song: listening on http://127.0.0.1:{port}");
    {
        let state = state.clone();
        std::thread::spawn(move || worker(&state));
    }
    let mut handles = Vec::new();
    for _ in 0..4 {
        let (server, state) = (server.clone(), state.clone());
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

fn worker(state: &State) {
    loop {
        let id = {
            let mut q = state.queue.lock().unwrap();
            loop {
                if let Some(id) = q.pop_front() {
                    break id;
                }
                q = state.wake.wait(q).unwrap();
            }
        };
        let (req, voice, keys, cancel) = {
            let jobs = state.jobs.lock().unwrap();
            let Some(j) = jobs.get(&id) else { continue };
            if j.status == "cancelled" {
                continue;
            }
            (j.request.clone(), j.voice.clone().unwrap(), j.keys.clone().unwrap(), j.cancel.clone())
        };
        state.busy.store(true, Ordering::SeqCst);
        let job_dir = state.cfg.jobs_dir().join(&id);
        let _ = std::fs::create_dir_all(&job_dir);
        let progress = JobProgress { state, id: id.clone() };
        let waited_before = crate::native::voicelease::waited_total();
        // A panic inside a stage (a CUDA library that cannot be loaded, a bug) fails this job instead of killing the
        // worker: otherwise the job stays "generating" forever and no later job runs.
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| job::run(&state.cfg, &req, &voice, &keys, &job_dir, cancel.clone(), &progress)))
            .unwrap_or_else(|panic| {
                let what = panic.downcast_ref::<String>().cloned().or_else(|| panic.downcast_ref::<&str>().map(|s| s.to_string())).unwrap_or_default();
                Err(anyhow::anyhow!("internal error: {what}"))
            });
        state.busy.store(false, Ordering::SeqCst);
        let waited = crate::native::voicelease::waited_total().saturating_sub(waited_before);
        if !waited.is_zero() {
            crate::log!("ok-song: job {id} waited {:.1} s for voice lines (voicelease)", waited.as_secs_f64());
        }
        let mut jobs = state.jobs.lock().unwrap();
        if let Some(j) = jobs.get_mut(&id) {
            j.finished = Some(j.created.elapsed().as_secs_f64());
            match result {
                // A job cancelled while it ran keeps "cancelled" even if its work finished (and was published for others).
                _ if cancel.load(Ordering::SeqCst) => j.status = "cancelled".into(),
                Ok(out) => {
                    j.status = "ready".into();
                    j.stage = "ready".into();
                    j.cache_hit = out.cache_hit;
                    j.manifest = Some(out.manifest);
                }
                Err(e) => {
                    let code = job::error_code(&e);
                    j.status = if code == "CANCELLED" { "cancelled".into() } else { "failed".into() };
                    j.error = Some(json!({"code": code, "stage": j.stage, "message": format!("{e:#}"), "retryable": job::retryable(code)}));
                    crate::log!("ok-song: job {id} {}: {e:#}", j.status);
                }
            }
        }
        let keep: Vec<String> = jobs.values().filter_map(|j| j.keys.as_ref().map(|k| k.artifact.clone())).collect();
        drop(jobs);
        // The job's own scratch folder (no junctions: made right above by this process).
        let _ = std::fs::remove_dir_all(&job_dir);
        let removed = job::trim_cache(&state.cfg, &keep);
        if removed > 0 {
            crate::log!("ok-song: cache over {} MB, removed {removed} old songs", state.cfg.cache_limit_mb);
        }
    }
}

fn respond(req: tiny_http::Request, code: u16, body: Value) {
    let header = tiny_http::Header::from_bytes(&b"Content-Type"[..], &b"application/json; charset=utf-8"[..]).unwrap();
    let _ = req.respond(tiny_http::Response::from_data(body.to_string().into_bytes()).with_status_code(code).with_header(header));
}

fn handle(state: &Arc<State>, mut req: tiny_http::Request) {
    let url = req.url().split('?').next().unwrap_or("").trim_end_matches('/').to_string();
    let parts: Vec<&str> = url.split('/').filter(|s| !s.is_empty()).collect();
    match (req.method(), parts.as_slice()) {
        (tiny_http::Method::Get, ["health"]) => {
            let q = state.queue.lock().unwrap().len();
            respond(req, 200, json!({"ok": true, "schema": SCHEMA, "version": env!("CARGO_PKG_VERSION"), "native_rust_inference": state.cfg.generate.native() && state.cfg.separate.native() && state.cfg.convert.native(),
                                     "backends": crate::backend::revisions(&state.cfg), "busy": state.busy.load(Ordering::SeqCst), "queue": q}));
        }
        (tiny_http::Method::Post, ["jobs"]) => {
            let mut body = String::new();
            if req.body_length().unwrap_or(0) > 65536 || std::io::Read::read_to_string(req.as_reader(), &mut body).is_err() {
                respond(req, 400, json!({"ok": false, "error": {"code": "BAD_REQUEST", "message": "body"}}));
                return;
            }
            match submit(state, body.trim_start_matches('\u{feff}')) {
                Ok((code, v)) => respond(req, code, v),
                Err(e) => respond(req, 400, json!({"ok": false, "error": {"code": job::error_code(&e), "message": format!("{e:#}"), "retryable": false}})),
            }
        }
        (tiny_http::Method::Get, ["jobs", id]) => {
            let jobs = state.jobs.lock().unwrap();
            match jobs.get(*id) {
                Some(j) => respond(req, 200, json!({"ok": true, "job": j.view(&state.cfg), "manifest": j.manifest})),
                None => respond(req, 404, json!({"ok": false, "error": {"code": "UNKNOWN_JOB"}})),
            }
        }
        (tiny_http::Method::Post, ["jobs", id, "cancel"]) => {
            let mut jobs = state.jobs.lock().unwrap();
            match jobs.get_mut(*id) {
                Some(j) => {
                    if !matches!(j.status.as_str(), "ready" | "failed" | "cancelled") {
                        j.cancel.store(true, Ordering::SeqCst);
                        j.status = "cancelled".into();
                        crate::log!("ok-song: job {id} cancel requested");
                    }
                    respond(req, 200, json!({"ok": true, "job": j.view(&state.cfg)}));
                }
                None => respond(req, 404, json!({"ok": false, "error": {"code": "UNKNOWN_JOB"}})),
            }
        }
        _ => respond(req, 404, json!({"ok": false, "error": {"code": "NOT_FOUND"}})),
    }
}

fn submit(state: &Arc<State>, body: &str) -> Result<(u16, Value)> {
    let mut request: SongRequest = serde_json::from_str(body)?;
    request.validate()?;
    if let Some(id) = state.by_request.lock().unwrap().get(&request.request_id).cloned() {
        let jobs = state.jobs.lock().unwrap();
        let j = &jobs[&id];
        if j.request != request {
            return Ok((409, json!({"ok": false, "error": {"code": "CONFLICT", "message": "this request_id was used with another body"}})));
        }
        return Ok((200, json!({"ok": true, "job": j.view(&state.cfg)})));
    }
    let voice = job::resolve_voice(&state.cfg, &request)?;
    let keys = job::keys(&state.cfg, &request, &voice);
    let id = {
        let mut n = state.next.lock().unwrap();
        *n += 1;
        format!("j{}-{}", *n, &keys.artifact[..8])
    };
    let cached = job::valid_artifact(&state.cfg.cache_dir().join(&keys.artifact));
    let mut job = Job { id: id.clone(), request: request.clone(), voice: Some(voice), keys: Some(keys), status: "queued".into(), stage: "queued".into(),
                        created: Instant::now(), finished: None, cache_hit: false, error: None, manifest: None, cancel: Arc::new(AtomicBool::new(false)) };
    if let Some(m) = cached {
        job.status = "ready".into();
        job.stage = "ready".into();
        job.cache_hit = true;
        job.finished = Some(0.0);
        job.manifest = Some(m);
    }
    let view = job.view(&state.cfg);
    let queued = job.status == "queued";
    state.jobs.lock().unwrap().insert(id.clone(), job);
    state.by_request.lock().unwrap().insert(request.request_id.clone(), id.clone());
    if queued {
        state.queue.lock().unwrap().push_back(id.clone());
        state.wake.notify_one();
    }
    crate::log!("ok-song: job {id} for {} ({})", request.heroine_id, if queued { "queued" } else { "cache hit" });
    Ok((202, json!({"ok": true, "job": view})))
}

/// The artifact folder a ready job published (for the CLI).
pub fn artifact_dir(cfg: &Config, keys: &Keys) -> PathBuf {
    cfg.cache_dir().join(&keys.artifact)
}
