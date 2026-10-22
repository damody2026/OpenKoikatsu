//! The three model stages behind one boundary, so each can be swapped (a native Rust port replaces a Python
//! prototype without touching jobs, cache or the game). Today all three are prototypes that run the upstream projects'
//! own entry points in their own environments: HeartMuLa `examples/run_music_generation.py`, `python -m demucs`,
//! Seed-VC `inference.py`. Nothing here is native inference yet; `doctor` says so.

use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use anyhow::{bail, Context, Result};
use serde_json::{json, Value};

use crate::config::{Config, StageConfig};

/// A failure with the code the job reports (BACKEND_TIMEOUT, GPU_OOM, CANCELLED, MODEL_MISSING, BACKEND_FAILED).
#[derive(Debug)]
pub struct StageError {
    pub code: &'static str,
    pub message: String,
}

impl std::fmt::Display for StageError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for StageError {}

pub fn stage_error(code: &'static str, message: impl Into<String>) -> anyhow::Error {
    StageError { code, message: message.into() }.into()
}

pub struct Runner<'a> {
    pub cfg: &'a Config,
    pub cancel: Arc<AtomicBool>,
}

impl Runner<'_> {
    /// Run one program to completion, its output in `log`. The child is killed on cancel or timeout (only this
    /// child: nothing else on the machine is touched).
    fn run(&self, program: &Path, args: &[String], cwd: &Path, log: &Path, timeout: Duration) -> Result<()> {
        if !program.is_file() {
            return Err(stage_error("MODEL_MISSING", format!("{} not found", program.display())));
        }
        let log_file = std::fs::File::create(log).with_context(|| format!("create {}", log.display()))?;
        let mut cmd = Command::new(program);
        cmd.args(args).current_dir(cwd).stdin(Stdio::null()).stdout(log_file.try_clone()?).stderr(log_file);
        for (k, v) in &self.cfg.env {
            cmd.env(k, v);
        }
        if !self.cfg.dll_dirs.is_empty() {
            let mut path = std::ffi::OsString::new();
            for d in &self.cfg.dll_dirs {
                path.push(d.as_os_str());
                path.push(";");
            }
            path.push(std::env::var_os("PATH").unwrap_or_default());
            cmd.env("PATH", path);
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
        }
        let mut child = cmd.spawn().with_context(|| format!("start {}", program.display()))?;
        let started = Instant::now();
        loop {
            if let Some(status) = child.try_wait()? {
                if status.success() {
                    return Ok(());
                }
                let tail = log_tail(log);
                let code = if tail.contains("CUDA out of memory") || tail.contains("OutOfMemoryError") { "GPU_OOM" } else { "BACKEND_FAILED" };
                return Err(stage_error(code, format!("{} exited with {status}: {tail}", program.display())));
            }
            if self.cancel.load(Ordering::SeqCst) {
                let _ = child.kill();
                let _ = child.wait();
                return Err(stage_error("CANCELLED", "cancelled"));
            }
            if started.elapsed() > timeout {
                let _ = child.kill();
                let _ = child.wait();
                return Err(stage_error("BACKEND_TIMEOUT", format!("{} ran over {} s", program.display(), timeout.as_secs())));
            }
            std::thread::sleep(Duration::from_millis(200));
        }
    }

    fn python(stage: &StageConfig) -> Result<&Path> {
        stage.python.as_deref().ok_or_else(|| stage_error("MODEL_MISSING", format!("{}: no python configured", stage.backend)))
    }

    /// Lyrics + tags -> a song (stereo WAV). The upstream entry has no seed; the stage cache makes a song repeatable.
    pub fn generate(&self, lyrics_model: &str, tags: &[String], max_ms: u32, dir: &Path) -> Result<PathBuf> {
        let st = &self.cfg.generate;
        if st.backend != "heartmula-python" {
            bail!("unknown generate backend {}", st.backend);
        }
        let repo = st.repo.as_deref().context("generate.repo")?;
        let ckpt = st.ckpt.as_deref().context("generate.ckpt")?;
        if !ckpt.join("HeartMuLa-oss-3B").is_dir() && !ckpt.join("config.json").is_file() {
            return Err(stage_error("MODEL_MISSING", format!("no HeartMuLa checkpoint in {}", ckpt.display())));
        }
        std::fs::write(dir.join("lyrics.txt"), lyrics_model)?;
        std::fs::write(dir.join("tags.txt"), tags.join(","))?;
        let out = dir.join("song.wav");
        let args = vec![
            "examples/run_music_generation.py".into(), "--model_path".into(), ckpt.display().to_string(),
            "--version".into(), st.version.clone().unwrap_or_else(|| "3B".into()),
            "--lyrics".into(), dir.join("lyrics.txt").display().to_string(), "--tags".into(), dir.join("tags.txt").display().to_string(),
            "--save_path".into(), out.display().to_string(), "--max_audio_length_ms".into(), max_ms.to_string(), "--lazy_load".into(), "true".into(),
        ];
        self.run(Self::python(st)?, &args, repo, &dir.join("generate.log"), Duration::from_secs(st.timeout_seconds))?;
        if !out.is_file() {
            return Err(stage_error("INVALID_AUDIO", "the generator wrote no song"));
        }
        Ok(out)
    }

    /// Song -> (vocals, accompaniment), both at the separator's rate and starting at the song's first sample.
    pub fn separate(&self, song: &Path, dir: &Path) -> Result<(PathBuf, PathBuf)> {
        let st = &self.cfg.separate;
        if st.backend != "demucs-python" {
            bail!("unknown separate backend {}", st.backend);
        }
        let model = st.model.clone().unwrap_or_else(|| "htdemucs_ft".into());
        let args = vec!["-m".into(), "demucs".into(), "--two-stems".into(), "vocals".into(), "-n".into(), model.clone(),
                        "-o".into(), dir.join("sep").display().to_string(), song.display().to_string()];
        self.run(Self::python(st)?, &args, dir, &dir.join("separate.log"), Duration::from_secs(st.timeout_seconds))?;
        let stem = song.file_stem().and_then(|s| s.to_str()).unwrap_or("song");
        let base = dir.join("sep").join(&model).join(stem);
        let (v, a) = (base.join("vocals.wav"), base.join("no_vocals.wav"));
        if !v.is_file() || !a.is_file() {
            return Err(stage_error("INVALID_AUDIO", "the separator wrote no stems"));
        }
        let (vo, ao) = (dir.join("vocals.wav"), dir.join("accompaniment.wav"));
        std::fs::rename(&v, &vo)?;
        std::fs::rename(&a, &ao)?;
        Ok((vo, ao))
    }

    /// The original vocal sung in the voice of `reference` (singing model, F0 conditioned, the same melody: no shift).
    pub fn convert(&self, vocals: &Path, reference: &Path, dir: &Path) -> Result<PathBuf> {
        let st = &self.cfg.convert;
        if st.backend != "seedvc-python" {
            bail!("unknown convert backend {}", st.backend);
        }
        let repo = st.repo.as_deref().context("convert.repo")?;
        let out_dir = dir.join("vc");
        std::fs::create_dir_all(&out_dir)?;
        let args = vec![
            "inference.py".into(), "--source".into(), vocals.display().to_string(), "--target".into(), reference.display().to_string(),
            "--output".into(), out_dir.display().to_string(), "--diffusion-steps".into(), st.diffusion_steps.unwrap_or(30).to_string(),
            "--inference-cfg-rate".into(), st.inference_cfg_rate.unwrap_or(0.7).to_string(), "--f0-condition".into(), "true".into(),
            "--auto-f0-adjust".into(), "false".into(), "--semi-tone-shift".into(), "0".into(), "--fp16".into(), "true".into(),
        ];
        self.run(Self::python(st)?, &args, repo, &dir.join("convert.log"), Duration::from_secs(st.timeout_seconds))?;
        let found = std::fs::read_dir(&out_dir)?.flatten().map(|e| e.path()).find(|p| p.extension().and_then(|x| x.to_str()) == Some("wav"));
        let found = found.ok_or_else(|| stage_error("INVALID_AUDIO", "the converter wrote no vocal"))?;
        let out = dir.join("converted.wav");
        std::fs::rename(&found, &out)?;
        Ok(out)
    }
}

fn log_tail(log: &Path) -> String {
    let mut s = String::new();
    if let Ok(mut f) = std::fs::File::open(log) {
        let _ = f.read_to_string(&mut s);
    }
    let lines: Vec<&str> = s.lines().filter(|l| !l.trim().is_empty()).collect();
    lines[lines.len().saturating_sub(3)..].join(" | ")
}

fn git_head(repo: &Path) -> Option<String> {
    let out = Command::new("git").arg("-C").arg(repo).args(["rev-parse", "HEAD"]).output().ok()?;
    out.status.success().then(|| String::from_utf8_lossy(&out.stdout).trim().to_string())
}

/// What each stage runs, with its revision (part of the cache keys: a new upstream commit or model makes new songs).
pub fn revisions(cfg: &Config) -> Value {
    let rev = |st: &StageConfig| -> Value {
        json!({"backend": st.backend, "native_rust": false, "repo": st.repo, "repo_head": st.repo.as_deref().and_then(git_head),
               "model": st.model, "version": st.version, "diffusion_steps": st.diffusion_steps, "inference_cfg_rate": st.inference_cfg_rate})
    };
    json!({"generate": rev(&cfg.generate), "separate": rev(&cfg.separate), "convert": rev(&cfg.convert)})
}

/// Doctor: every stage's program, repository, weights and what the GPU has now. Checks files and runs a one-line
/// import in each environment; it does not just test a port.
pub fn doctor(cfg: &Config) -> Value {
    let probe = |python: Option<&Path>, code: &str| -> Value {
        let Some(py) = python else { return json!({"ok": false, "error": "no python configured"}) };
        let mut cmd = Command::new(py);
        cmd.args(["-c", code]);
        for (k, v) in &cfg.env {
            cmd.env(k, v);
        }
        match cmd.output() {
            Ok(o) if o.status.success() => json!({"ok": true, "says": String::from_utf8_lossy(&o.stdout).trim()}),
            Ok(o) => json!({"ok": false, "error": String::from_utf8_lossy(&o.stderr).lines().last().unwrap_or("").to_string()}),
            Err(e) => json!({"ok": false, "error": e.to_string()}),
        }
    };
    let gpu = Command::new("nvidia-smi").args(["--query-gpu=name,memory.used,memory.total,driver_version", "--format=csv,noheader"]).output().ok()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string());
    let ckpt = cfg.generate.ckpt.clone().unwrap_or_default();
    let weights: Vec<Value> = ["HeartMuLa-oss-3B", "HeartCodec-oss", "tokenizer.json", "gen_config.json"].iter().map(|n| {
        let p = ckpt.join(n);
        json!({"path": p, "exists": p.exists()})
    }).collect();
    json!({
        "schema": 1, "native_rust_inference": false,
        "note": "all three stages are Python prototypes calling upstream entry points; mixing, loudness, envelope, cache and service are Rust",
        "gpu": gpu, "ok_tts": {"path": cfg.ok_tts, "exists": cfg.ok_tts.is_file()},
        "generate": {"revisions": revisions(cfg)["generate"], "weights": weights,
                     "env": probe(cfg.generate.python.as_deref(), "import torch,heartlib;print(torch.__version__, torch.cuda.is_available())")},
        "separate": {"revisions": revisions(cfg)["separate"], "env": probe(cfg.separate.python.as_deref(), "import torch,demucs;print(torch.__version__, demucs.__version__)")},
        "convert": {"revisions": revisions(cfg)["convert"], "env": probe(cfg.convert.python.as_deref(), "import torch,librosa,dac;print(torch.__version__)")},
        "dll_dirs": cfg.dll_dirs.iter().map(|d| json!({"path": d, "exists": d.is_dir()})).collect::<Vec<_>>(),
        "work_root": cfg.work_root,
    })
}
