//! Files under save_dir: state.json (atomic replace) and trace.jsonl (append, rotated).
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

pub const STATE_SCHEMA: u32 = 1;
const TRACE_ROTATE_BYTES: u64 = 50 * 1024 * 1024;

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Stats {
    pub calls: u64,
    pub ok: u64,
    pub failed: u64,
    pub retries: u64,
    pub prompt_tokens: u64,
    pub completion_tokens: u64,
    pub cache_hit_tokens: u64,
    pub cache_miss_tokens: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct State {
    pub schema: u32,
    /// Lifetime LLM accounting across sessions.
    pub stats: Stats,
}

impl Default for State {
    fn default() -> Self {
        State { schema: STATE_SCHEMA, stats: Stats::default() }
    }
}

pub fn state_path(dir: &Path) -> PathBuf {
    dir.join("state.json")
}

pub fn load_state(dir: &Path) -> Result<State, String> {
    let path = state_path(dir);
    if !path.exists() {
        return Ok(State::default());
    }
    let text = fs::read_to_string(&path).map_err(|e| format!("state_unreadable: {e}"))?;
    let state: State = serde_json::from_str(&text).map_err(|e| format!("state_invalid: {e}"))?;
    if state.schema != STATE_SCHEMA {
        return Err(format!("state_schema_unsupported: {}", state.schema));
    }
    Ok(state)
}

/// Write beside the target, flush to disk, then replace it: a crash never leaves half a file.
pub fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let tmp = path.with_extension("json.tmp");
    {
        let mut f = File::create(&tmp).map_err(|e| format!("write_failed: {e}"))?;
        f.write_all(bytes).map_err(|e| format!("write_failed: {e}"))?;
        f.sync_all().map_err(|e| format!("write_failed: {e}"))?;
    }
    fs::rename(&tmp, path).map_err(|e| format!("replace_failed: {e}"))
}

pub fn save_state(dir: &Path, state: &State) -> Result<(), String> {
    let text = serde_json::to_string_pretty(state).map_err(|e| e.to_string())?;
    write_atomic(&state_path(dir), text.as_bytes())
}

pub struct Trace {
    path: PathBuf,
    file: Mutex<Option<File>>,
}

impl Trace {
    pub fn new(dir: &Path) -> Trace {
        Trace { path: dir.join("trace.jsonl"), file: Mutex::new(None) }
    }
    /// Best effort: tracing must never fail a job.
    pub fn write(&self, line: &serde_json::Value) {
        let mut guard = self.file.lock().unwrap();
        if let Ok(meta) = fs::metadata(&self.path) {
            if meta.len() > TRACE_ROTATE_BYTES {
                *guard = None;
                let _ = fs::rename(&self.path, self.path.with_extension("1.jsonl"));
            }
        }
        if guard.is_none() {
            *guard = OpenOptions::new().create(true).append(true).open(&self.path).ok();
        }
        if let Some(f) = guard.as_mut() {
            let _ = writeln!(f, "{line}");
        }
    }
}
