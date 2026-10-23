//! ok-song.json: where the work goes, where ok-tts is, and how each stage's backend runs.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use serde::Deserialize;

#[derive(Deserialize, Clone, Debug)]
pub struct StageConfig {
    /// "heartmula-rust" (native), or "heartmula-python", "demucs-python", "seedvc-python" (prototypes calling upstream entry points)
    pub backend: String,
    #[serde(default)]
    pub python: Option<PathBuf>,
    #[serde(default)]
    pub repo: Option<PathBuf>,
    #[serde(default)]
    pub ckpt: Option<PathBuf>,
    #[serde(default)]
    pub version: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
    #[serde(default)]
    pub diffusion_steps: Option<u32>,
    #[serde(default)]
    pub inference_cfg_rate: Option<f64>,
    #[serde(default = "default_timeout")]
    pub timeout_seconds: u64,
    /// Native HeartMuLa sampling (upstream defaults: cfg 3, top-k 50, temperature 1). The seed defaults to one derived
    /// from the lyrics and tags, so the same request makes the same song.
    #[serde(default)]
    pub cfg_scale: Option<f64>,
    #[serde(default)]
    pub topk: Option<u32>,
    #[serde(default)]
    pub temperature: Option<f64>,
    #[serde(default)]
    pub seed: Option<u64>,
    /// Seed-VC: natural pitch motion on the generated singing (seedvc::humanize_f0); unset = none.
    #[serde(default)]
    pub vibrato_cents: Option<f64>,
    #[serde(default)]
    pub glide_ms: Option<f64>,
    /// Seed-VC: blend neighbouring pitch-bin embeddings (dit::Dit::regulate).
    #[serde(default)]
    pub soft_f0: Option<bool>,
    /// Seed-VC: undo pitch-correction-like singing in the generated vocal (seedvc::deautotune_f0): each note slides up
    /// from `scoop_cents` below and wanders by up to `drift_cents`. Defaults 60 / 20; 0 / 0 = off.
    #[serde(default)]
    pub scoop_cents: Option<f64>,
    #[serde(default)]
    pub drift_cents: Option<f64>,
}

impl StageConfig {
    pub fn native(&self) -> bool {
        self.backend.ends_with("-rust")
    }
}

fn default_timeout() -> u64 {
    900
}

#[derive(Deserialize, Clone, Debug)]
pub struct Config {
    pub work_root: PathBuf,
    pub ok_tts: PathBuf,
    pub ok_tts_profiles: PathBuf,
    pub ok_tts_voices: PathBuf,
    #[serde(default)]
    pub dll_dirs: Vec<PathBuf>,
    #[serde(default)]
    pub env: BTreeMap<String, String>,
    pub generate: StageConfig,
    pub separate: StageConfig,
    pub convert: StageConfig,
    #[serde(default = "default_cache_limit")]
    pub cache_limit_mb: u64,
}

fn default_cache_limit() -> u64 {
    4096
}

impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        let text = std::fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
        serde_json::from_str(text.trim_start_matches('\u{feff}')).with_context(|| format!("parse {}", path.display()))
    }

    /// --config, else ok-song.json beside the exe, else the example in the crate (development).
    pub fn find(explicit: Option<&str>) -> Result<(Self, PathBuf)> {
        let exe_dir = std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default();
        let candidates: Vec<PathBuf> = match explicit {
            Some(p) => vec![PathBuf::from(p)],
            None => vec![exe_dir.join("ok-song.json"), exe_dir.join("../../ok-song.json"), exe_dir.join("../../ok-song.example.json")],
        };
        for c in &candidates {
            if c.is_file() {
                return Ok((Self::load(c)?, c.clone()));
            }
        }
        anyhow::bail!("no ok-song.json (looked at {:?}); copy ok-song.example.json", candidates)
    }

    pub fn jobs_dir(&self) -> PathBuf {
        self.work_root.join("jobs")
    }

    pub fn cache_dir(&self) -> PathBuf {
        self.work_root.join("cache")
    }

    pub fn stage_cache_dir(&self) -> PathBuf {
        self.work_root.join("stages")
    }

    pub fn voices_dir(&self) -> PathBuf {
        self.work_root.join("voices")
    }
}
