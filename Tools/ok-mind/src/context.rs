//! One persistent DeepSeek context per heroine (plan §7.0).
//!
//! Every request is the previous request plus new messages, so DeepSeek's prefix cache hits everything but the
//! newest turn. Nothing before the tail is ever edited except at compaction: when the history passes
//! `compact_at_chars`, it is replaced by one summary turn plus the last `keep_tail_chars` of the conversation.
use crate::deepseek::Message;
use crate::store::write_atomic;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

pub const CONTEXT_SCHEMA: u32 = 1;
pub const SUMMARY_HEADER: &str = "[長期記憶摘要]";
pub const SUMMARY_ACK: &str = "（記住了）";

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct Context {
    pub schema: u32,
    pub heroine: String,
    pub system: String,
    /// user / assistant turns, byte-for-byte what was sent and received.
    pub history: Vec<Message>,
    pub compactions: u32,
    pub turns: u64,
    /// History before this index is already in her memory stream (end-of-meeting summaries).
    pub summarized_upto: usize,
}

impl Context {
    pub fn chars(&self) -> usize {
        self.history.iter().map(|m| m.content.chars().count()).sum()
    }
}

/// Index of the first message of the kept tail: at least `keep` chars from the end, starting on a user turn.
/// Returns 0 when everything already fits (nothing to compact).
pub fn tail_start(history: &[Message], keep: usize) -> usize {
    let mut chars = 0usize;
    let mut start = history.len();
    while start > 0 && chars < keep {
        start -= 1;
        chars += history[start].content.chars().count();
    }
    // Never split an exchange: the tail begins with the player's turn.
    while start > 0 && start < history.len() && history[start].role != "user" {
        start += 1;
    }
    if start >= history.len() {
        // The newest exchange alone is longer than `keep`: keep just that exchange.
        start = history.iter().rposition(|m| m.role == "user").unwrap_or(0);
    }
    start
}

/// Her own words (replies and the summary of earlier ones) in the player's language's script (deepseek::localize).
/// Replies are converted as they arrive; this repairs a history saved before that (or in the other script, after the
/// player changed language), which she would otherwise keep imitating. The game's own turns are left as they are.
pub fn traditional_history(history: &mut [Message]) {
    localize_history(history, "zh-TW");
}

pub fn localize_history(history: &mut [Message], lang: &str) {
    for m in history.iter_mut().filter(|m| m.role == "assistant" || m.content.starts_with(SUMMARY_HEADER)) {
        let converted = crate::deepseek::localize(&m.content, lang);
        if converted != m.content {
            m.content = converted;
        }
    }
}

pub fn summary_turns(summary: &str) -> Vec<Message> {
    vec![
        Message { role: "user".into(), content: format!("{SUMMARY_HEADER}\n{}", summary.trim()) },
        Message { role: "assistant".into(), content: SUMMARY_ACK.into() },
    ]
}

pub fn compaction_instruction(keep_tail_chars: usize, summary_chars: usize, lang: &str) -> String {
    crate::text::t(lang, "mind.compact_prompt", &[("keep_tail_chars", keep_tail_chars.to_string()), ("summary_chars", summary_chars.to_string()), ("summary_header", SUMMARY_HEADER.to_string())])
}

fn file_name(heroine: &str) -> String {
    let safe: String = heroine.chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '_' }).collect();
    format!("{safe}.json")
}

pub struct Contexts {
    dir: PathBuf,
    open: Mutex<HashMap<String, Arc<Mutex<Option<Context>>>>>,
}

impl Contexts {
    pub fn new(save_dir: &Path) -> Contexts {
        Contexts { dir: save_dir.join("context"), open: Mutex::new(HashMap::new()) }
    }

    /// The per-heroine slot; holding its lock serialises that heroine's turns.
    pub fn slot(&self, heroine: &str) -> Arc<Mutex<Option<Context>>> {
        self.open.lock().unwrap().entry(heroine.to_string()).or_insert_with(|| Arc::new(Mutex::new(None))).clone()
    }

    pub fn load(&self, heroine: &str) -> Result<Option<Context>, String> {
        let path = self.dir.join(file_name(heroine));
        crate::store::settle(&path);
        if !path.exists() {
            return Ok(None);
        }
        let text = std::fs::read_to_string(&path).map_err(|e| format!("context_unreadable: {e}"))?;
        let ctx: Context = serde_json::from_str(&text).map_err(|e| format!("context_invalid: {e}"))?;
        if ctx.schema != CONTEXT_SCHEMA || ctx.heroine != heroine {
            return Err(format!("context_mismatch: {}", path.display()));
        }
        Ok(Some(ctx))
    }

    pub fn save(&self, ctx: &Context) -> Result<(), String> {
        std::fs::create_dir_all(&self.dir).map_err(|e| format!("context_dir: {e}"))?;
        let text = serde_json::to_string(ctx).map_err(|e| e.to_string())?;
        write_atomic(&self.dir.join(file_name(&ctx.heroine)), text.as_bytes())
    }
}
