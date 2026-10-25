//! Models that are not versioned: a Hugging Face repository at a fixed commit, downloaded on first use into a folder of
//! its own (ok-tts: <exe>/models/<name>/). Every file is checked after download (LFS files by SHA-256, the others by
//! their git blob SHA-1); a partial file resumes; `.ok-complete` is written last, so a folder without it is never used.
//! HF_ENDPOINT picks a mirror and HTTPS_PROXY / HTTP_PROXY a proxy, as in huggingface_hub.

use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use serde::Deserialize;
use sha2::Digest;

pub const MARKER: &str = ".ok-complete";

/// A repository at one commit (what was tested; a newer commit is never picked up by itself).
pub struct Repo {
    pub id: &'static str,
    pub rev: &'static str,
}

impl Repo {
    /// "Qwen3-TTS-12Hz-1.7B-Base" for "Qwen/Qwen3-TTS-12Hz-1.7B-Base".
    pub fn name(&self) -> &'static str {
        self.id.rsplit('/').next().unwrap_or(self.id)
    }
}

/// What a download is doing now ("" when none): shown by /health while the model is not there yet.
static STATUS: Mutex<String> = Mutex::new(String::new());

/// The download's progress in percent (-1: not downloading), for a game to show in its own words.
static PERCENT: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-1);

pub fn status() -> String {
    STATUS.lock().unwrap().clone()
}

pub fn percent() -> Option<u32> {
    u32::try_from(PERCENT.load(std::sync::atomic::Ordering::Relaxed)).ok()
}

pub fn set_status(s: String) {
    if s.is_empty() {
        PERCENT.store(-1, std::sync::atomic::Ordering::Relaxed);
    }
    *STATUS.lock().unwrap() = s;
}

#[derive(Deserialize)]
struct Entry {
    #[serde(rename = "type")]
    kind: String,
    path: String,
    #[serde(default)]
    size: u64,
    /// git blob SHA-1 (of the pointer file for LFS files)
    #[serde(default)]
    oid: String,
    lfs: Option<Lfs>,
}

#[derive(Deserialize)]
struct Lfs {
    oid: String,
    size: u64,
}

/// `dir` holds the whole repository at `repo.rev` (downloaded now if it does not).
pub fn ensure(repo: &Repo, dir: &Path) -> Result<()> {
    if dir.join(MARKER).is_file() {
        return Ok(());
    }
    let endpoint = std::env::var("HF_ENDPOINT").ok().filter(|e| !e.trim().is_empty()).unwrap_or_else(|| "https://huggingface.co".into());
    let endpoint = endpoint.trim_end_matches('/');
    let agent = ureq::AgentBuilder::new().try_proxy_from_env(true).timeout_connect(Duration::from_secs(30)).timeout_read(Duration::from_secs(120)).build();
    let list_url = format!("{endpoint}/api/models/{}/tree/{}?recursive=true", repo.id, repo.rev);
    crate::log!("ok-tts: {} is not here; downloading it from {endpoint} into {}", repo.id, dir.display());
    set_status(format!("listing {}", repo.id));
    let body = agent.get(&list_url).call().with_context(|| format!("list {list_url}"))?.into_string()?;
    let entries: Vec<Entry> = serde_json::from_str(&body).with_context(|| format!("file list of {}", repo.id))?;
    let files: Vec<&Entry> = entries.iter().filter(|e| e.kind == "file").collect();
    if files.is_empty() {
        bail!("{} at {} lists no files", repo.id, repo.rev);
    }
    let total: u64 = files.iter().map(|f| f.lfs.as_ref().map_or(f.size, |l| l.size)).sum();
    let mut done = 0u64;
    for f in &files {
        let size = f.lfs.as_ref().map_or(f.size, |l| l.size);
        let url = format!("{endpoint}/{}/resolve/{}/{}", repo.id, repo.rev, f.path);
        let path = dir.join(&f.path);
        let check = match &f.lfs {
            Some(l) => Check::Sha256(l.oid.clone()),
            None => Check::GitBlob(f.oid.clone()),
        };
        fetch(&agent, &url, &path, size, &check, repo.id, done, total)?;
        done += size;
    }
    let marker = serde_json::json!({"repo": repo.id, "rev": repo.rev, "files": files.iter().map(|f| &f.path).collect::<Vec<_>>()});
    crate::audio::write_atomic(&dir.join(MARKER), serde_json::to_string_pretty(&marker)?.as_bytes())?;
    set_status(String::new());
    crate::log!("ok-tts: downloaded {} ({:.2} GB)", repo.id, total as f64 / 1e9);
    Ok(())
}

enum Check {
    Sha256(String),
    GitBlob(String),
}

impl Check {
    fn matches(&self, path: &Path, size: u64) -> Result<bool> {
        let mut f = std::fs::File::open(path)?;
        let mut buf = vec![0u8; 1 << 20];
        let mut sha256 = sha2::Sha256::new();
        let mut sha1 = sha1_smol::Sha1::new();
        sha1.update(format!("blob {size}\0").as_bytes());
        loop {
            let n = f.read(&mut buf)?;
            if n == 0 {
                break;
            }
            match self {
                Check::Sha256(_) => sha256.update(&buf[..n]),
                Check::GitBlob(_) => sha1.update(&buf[..n]),
            }
        }
        Ok(match self {
            Check::Sha256(want) => format!("{:x}", sha256.finalize()) == *want,
            Check::GitBlob(want) => sha1.digest().to_string() == *want,
        })
    }
}

/// One file into `path` (through `path`.part, resumed when it is there), checked before it is renamed into place.
#[allow(clippy::too_many_arguments)]
fn fetch(agent: &ureq::Agent, url: &str, path: &Path, size: u64, check: &Check, repo: &str, before: u64, total: u64) -> Result<()> {
    if path.is_file() && std::fs::metadata(path)?.len() == size && check.matches(path, size)? {
        return Ok(());
    }
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let part = PathBuf::from(format!("{}.part", path.display()));
    let mut have = std::fs::metadata(&part).map(|m| m.len()).unwrap_or(0);
    if have > size {
        have = 0;
    }
    let mut request = agent.get(url);
    if have > 0 {
        request = request.set("Range", &format!("bytes={have}-"));
    }
    let response = request.call().with_context(|| format!("download {url}"))?;
    // 206: the rest of a partial file; 200: the whole file (a server without ranges starts over).
    if response.status() != 206 {
        have = 0;
    }
    let mut out = std::fs::OpenOptions::new().create(true).write(true).open(&part)?;
    out.set_len(have)?;
    out.seek(SeekFrom::Start(have))?;
    let mut reader = response.into_reader();
    let mut buf = vec![0u8; 1 << 20];
    let mut got = have;
    let mut last_pct = u64::MAX;
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    loop {
        let n = reader.read(&mut buf).with_context(|| format!("download {url}"))?;
        if n == 0 {
            break;
        }
        out.write_all(&buf[..n])?;
        got += n as u64;
        let pct = (before + got) * 100 / total.max(1);
        if pct != last_pct {
            last_pct = pct;
            PERCENT.store(pct as i32, std::sync::atomic::Ordering::Relaxed);
            set_status(format!("downloading {repo} {pct}% ({:.0}/{:.0} MB, {name})", (before + got) as f64 / 1e6, total as f64 / 1e6));
            if pct % 5 == 0 {
                crate::log!("ok-tts: downloading {repo}: {pct}%");
            }
        }
    }
    out.flush()?;
    drop(out);
    if got != size {
        bail!("{name}: got {got} of {size} bytes (the download stopped; it resumes on the next start)");
    }
    set_status(format!("checking {name}"));
    if !check.matches(&part, size)? {
        let _ = std::fs::remove_file(&part);
        bail!("{name}: the downloaded file does not match its checksum (removed; it is downloaded again on the next start)");
    }
    if path.exists() {
        std::fs::remove_file(path)?;
    }
    std::fs::rename(&part, path)?;
    Ok(())
}
