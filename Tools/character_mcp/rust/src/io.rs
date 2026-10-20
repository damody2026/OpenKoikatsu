use anyhow::{Context, Result, bail};
use serde_json::Value;
use std::{
    fs,
    io::Write,
    path::{Path, PathBuf},
    thread,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio_util::sync::CancellationToken;

pub fn now() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs_f64()
}
pub fn id() -> String {
    uuid::Uuid::new_v4().simple().to_string()
}
pub fn valid_id(s: &str) -> Result<()> {
    if s.len() != 32
        || !s
            .bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
    {
        bail!("Invalid dataset/job/run ID");
    }
    Ok(())
}
fn retryable(e: &std::io::Error) -> bool {
    matches!(
        e.kind(),
        std::io::ErrorKind::PermissionDenied | std::io::ErrorKind::NotFound
    ) || matches!(e.raw_os_error(), Some(32 | 33))
}
pub fn atomic_json(path: &Path, value: &Value) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let temp = path.with_file_name(format!(
        "{}.{}.tmp",
        path.file_name().unwrap_or_default().to_string_lossy(),
        id()
    ));
    let result = (|| -> Result<()> {
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp)?;
        serde_json::to_writer_pretty(&mut file, value)?;
        file.flush()?;
        drop(file);
        for attempt in 0..=100 {
            match fs::rename(&temp, path) {
                Ok(()) => return Ok(()),
                Err(e) if retryable(&e) && attempt < 100 => {
                    thread::sleep(Duration::from_millis(10))
                }
                Err(e) => return Err(e.into()),
            }
        }
        unreachable!()
    })();
    let _ = fs::remove_file(temp);
    result
}
pub fn read_json(path: &Path, max: u64) -> Result<Value> {
    for attempt in 0..=100 {
        let bytes = (|| -> std::io::Result<Vec<u8>> {
            if fs::metadata(path)?.len() > max {
                return Err(std::io::Error::other("Oversized JSON document"));
            }
            fs::read(path)
        })();
        match bytes {
            Ok(bytes) => {
                return Ok(serde_json::from_slice(
                    bytes.strip_prefix(&[239, 187, 191]).unwrap_or(&bytes),
                )?);
            }
            Err(e) if retryable(&e) && attempt < 100 => thread::sleep(Duration::from_millis(10)),
            Err(e) => return Err(e.into()),
        }
    }
    unreachable!()
}
pub trait Editor: Send + Sync {
    fn root(&self) -> &Path;
    fn rpc(&self, action: &str, args: &Value, cancel: &CancellationToken) -> Result<Value>;
}
#[derive(Clone)]
pub struct Bridge {
    pub root: PathBuf,
    pub timeout: f64,
}
impl Bridge {
    pub fn new(root: PathBuf, timeout: f64) -> Result<Self> {
        let root = std::path::absolute(root)?;
        for name in ["requests", "responses", "images", "fitting"] {
            fs::create_dir_all(root.join(name))?;
        }
        Ok(Self { root, timeout })
    }
    pub fn owned_file(&self, folder: &str, path: &Path, max: u64) -> Result<PathBuf> {
        owned_file(&self.root, folder, path, max)
    }
}
pub fn owned_file(root: &Path, folder: &str, path: &Path, max: u64) -> Result<PathBuf> {
    let actual = fs::canonicalize(path).context("Output file unavailable")?;
    if actual.parent() != Some(fs::canonicalize(root.join(folder))?.as_path())
        || fs::metadata(&actual)?.len() > max
    {
        bail!("Invalid output path/size");
    }
    Ok(actual)
}
impl Editor for Bridge {
    fn root(&self) -> &Path {
        &self.root
    }
    fn rpc(&self, action: &str, args: &Value, cancel: &CancellationToken) -> Result<Value> {
        if cancel.is_cancelled() {
            bail!(
                "Request cancelled; an already-started edit may finish. Re-query state before retrying."
            );
        }
        if action == "progress" {
            let mut status = read_json(&self.root.join("status.json"), 8_000_000)
                .context("Editor heartbeat unavailable; start the editor with -CharacterMCP")?;
            let age = (now() - status["heartbeat"].as_f64().unwrap_or(0.)).max(0.);
            status["heartbeat_age_seconds"] = age.into();
            status["responsive"] = (age < 2.).into();
            return Ok(status);
        }
        let name = format!("{}.json", id());
        let request = self.root.join("requests").join(&name);
        let response = self.root.join("responses").join(&name);
        let timeout = if action == "state" {
            self.timeout.min(10.)
        } else {
            self.timeout
        };
        let mut payload = args.clone();
        payload["action"] = action.into();
        payload["deadline"] = (now() + timeout).into();
        atomic_json(&request, &payload)?;
        let result = (|| -> Result<Value> {
            let start = std::time::Instant::now();
            while start.elapsed().as_secs_f64() < timeout {
                if cancel.is_cancelled() {
                    bail!(
                        "Request cancelled; an already-started edit may finish. Re-query state before retrying."
                    );
                }
                if response.exists() {
                    let data = read_json(&response, 8_000_000)?;
                    if data["ok"] != true {
                        bail!(
                            "{}",
                            data["error"].as_str().unwrap_or("Editor request failed")
                        );
                    }
                    return Ok(data.get("result").cloned().unwrap_or(serde_json::json!({})));
                }
                thread::sleep(Duration::from_millis(25));
            }
            bail!(
                "Editor timed out. Start character edit mode with -CharacterMCP and the matching mailbox directory. An already-started edit may finish; re-query state."
            )
        })();
        let _ = fs::remove_file(&request);
        let _ = fs::remove_file(&response);
        result
    }
}
