use serde::Deserialize;
use std::path::PathBuf;

pub const DEFAULT_ENDPOINT: &str = "https://api.deepseek.com/chat/completions";
pub const DEFAULT_MODEL: &str = "deepseek-v4-flash";

/// What Unreal passes to okm_open. Everything is optional except save_dir.
#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct Config {
    pub save_dir: PathBuf,
    /// Same file the FreeTalk code reads (Saved/StreetSocial/deepseek.json): api_key, endpoint, model.
    pub deepseek_settings: Option<PathBuf>,
    pub endpoint: Option<String>,
    pub model: Option<String>,
    /// Only for tests; real runs take DEEPSEEK_API_KEY or the settings file.
    pub api_key: Option<String>,
    pub workers: usize,
    pub per_minute: u32,
    pub timeout_s: u64,
    /// A reply she is speaking now (dialogue priority, no thinking) normally comes in 2–3 s (20 s on a slow day): a request that hangs is given
    /// up and retried after this, not after the minute background jobs may need.
    pub dialogue_timeout_s: u64,
    /// Waits before each retry of a 429 / 5xx / network failure (DeepSeek guidance: back off 1 s, 10 s, 20 s).
    pub retry_delays_ms: Vec<u64>,
    /// Write full prompts and replies to trace.jsonl (no API key is ever written).
    pub trace_content: bool,
    /// Enables the debug_panic job, used to prove a Rust panic never reaches Unreal.
    pub debug: bool,
    /// Per-heroine context compaction (plan §7.0): past this many chars of history, summarise all but the tail.
    pub compact_at_chars: usize,
    pub keep_tail_chars: usize,
    /// How long the summary that replaces the rest may be (characters).
    pub summary_chars: usize,
    /// Content/IndependentResources: StreetSocialData/sports and MarketData (plan §15). None disables `fin` queries.
    pub data_dir: Option<PathBuf>,
    /// Lotto and scratch-card seed for a new finance.json; None takes the clock.
    pub finance_seed: Option<u64>,
    /// Wallet currency for deposits and withdrawals: "TWD" (default) or "CNY".
    pub wallet_currency: Option<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            save_dir: PathBuf::new(),
            deepseek_settings: None,
            endpoint: None,
            model: None,
            api_key: None,
            workers: 2,
            per_minute: 30,
            timeout_s: 60,
            dialogue_timeout_s: 30,
            retry_delays_ms: vec![1_000, 10_000, 20_000],
            trace_content: true,
            debug: false,
            // Past 200,000 characters: everything but the last 30,000 becomes a summary of about 10,000.
            compact_at_chars: 200_000,
            keep_tail_chars: 30_000,
            summary_chars: 10_000,
            data_dir: None,
            finance_seed: None,
            wallet_currency: None,
        }
    }
}

#[derive(Debug, Clone)]
pub struct LlmSettings {
    pub endpoint: String,
    pub model: String,
    pub api_key: String,
}

#[derive(Deserialize, Default)]
struct SettingsFile {
    api_key: Option<String>,
    endpoint: Option<String>,
    model: Option<String>,
}

impl Config {
    pub fn parse(json: &str) -> Result<Config, String> {
        let cfg: Config = serde_json::from_str(json).map_err(|e| format!("config_invalid: {e}"))?;
        if cfg.save_dir.as_os_str().is_empty() {
            return Err("config_invalid: save_dir is required".into());
        }
        if cfg.workers == 0 || cfg.workers > 8 {
            return Err("config_invalid: workers must be 1..8".into());
        }
        if cfg.keep_tail_chars == 0 || cfg.keep_tail_chars >= cfg.compact_at_chars {
            return Err("config_invalid: keep_tail_chars must be below compact_at_chars".into());
        }
        if cfg.per_minute == 0 {
            return Err("config_invalid: per_minute must be > 0".into());
        }
        Ok(cfg)
    }

    /// Key: environment, then config (tests), then the settings file. Endpoint and model: config, then file, then default.
    pub fn llm(&self) -> Result<LlmSettings, String> {
        let file: SettingsFile = match &self.deepseek_settings {
            Some(path) if path.exists() => {
                let text = std::fs::read_to_string(path).map_err(|e| format!("deepseek_settings_unreadable: {e}"))?;
                serde_json::from_str(text.trim_start_matches('\u{feff}')).map_err(|e| format!("deepseek_settings_invalid: {e}"))?
            }
            _ => SettingsFile::default(),
        };
        let key = std::env::var("DEEPSEEK_API_KEY")
            .ok()
            .filter(|k| !k.trim().is_empty())
            .or_else(|| self.api_key.clone())
            .or(file.api_key)
            .map(|k| k.trim().to_string())
            .filter(|k| !k.is_empty())
            .ok_or_else(|| "no_api_key: set DEEPSEEK_API_KEY or Saved/StreetSocial/deepseek.json".to_string())?;
        Ok(LlmSettings {
            endpoint: self.endpoint.clone().or(file.endpoint).unwrap_or_else(|| DEFAULT_ENDPOINT.into()),
            model: self.model.clone().or(file.model).unwrap_or_else(|| DEFAULT_MODEL.into()),
            api_key: key,
        })
    }
}
