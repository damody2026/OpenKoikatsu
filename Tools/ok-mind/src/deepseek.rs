//! One blocking DeepSeek chat-completions call. Retries and rate limits live in the worker.
use crate::config::LlmSettings;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::time::Duration;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Message {
    pub role: String,
    pub content: String,
}

#[derive(Debug, Clone)]
pub struct ChatRequest {
    pub messages: Vec<Message>,
    pub max_tokens: u32,
    pub temperature: f32,
    pub json: bool,
    /// deepseek-v4-flash thinks by default and spends max_tokens on reasoning; dialogue turns it off.
    pub thinking: bool,
}

#[derive(Debug, Clone, Default, Serialize, PartialEq)]
pub struct Usage {
    pub prompt_tokens: u64,
    pub completion_tokens: u64,
    /// DeepSeek prefix-cache accounting (usage.prompt_cache_hit_tokens / prompt_cache_miss_tokens).
    pub cache_hit_tokens: u64,
    pub cache_miss_tokens: u64,
}

#[derive(Debug, Clone)]
pub struct ChatReply {
    pub content: String,
    pub model: String,
    pub finish_reason: String,
    pub usage: Usage,
}

#[derive(Debug)]
pub enum CallError {
    /// Worth retrying: 429, 5xx, timeouts and connection failures.
    Retryable { status: Option<u16>, message: String },
    Fatal(String),
}

impl CallError {
    pub fn message(&self) -> String {
        match self {
            CallError::Retryable { status: Some(s), message } => format!("http_{s}: {message}"),
            CallError::Retryable { status: None, message } => format!("network: {message}"),
            CallError::Fatal(m) => m.clone(),
        }
    }
}

pub fn agent(timeout_s: u64) -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(timeout_s.max(1)))
        // A generic agent string: nothing about the user ever goes into a request header.
        .user_agent("OpenKoikatsu-ok-mind/0.1")
        .build()
}

pub fn request_body(model: &str, req: &ChatRequest) -> Value {
    let mut body = json!({
        "model": model,
        "messages": req.messages,
        "max_tokens": req.max_tokens,
        "temperature": req.temperature,
        "stream": false,
        "thinking": { "type": if req.thinking { "enabled" } else { "disabled" } },
    });
    if req.json {
        body["response_format"] = json!({ "type": "json_object" });
    }
    body
}

/// What another OpenAI-style provider needs on top: OpenRouter turns a model's reasoning on or off with its own
/// "reasoning" field (DeepSeek's "thinking" is not read there, and a reasoning model spends the whole budget thinking).
pub fn provider_body(endpoint: &str, mut body: Value, req: &ChatRequest) -> Value {
    if endpoint.contains("openrouter.ai") {
        body["reasoning"] = json!({ "enabled": req.thinking });
    }
    body
}

pub fn call(agent: &ureq::Agent, llm: &LlmSettings, req: &ChatRequest) -> Result<ChatReply, CallError> {
    let response = agent
        .post(&llm.endpoint)
        .set("Authorization", &format!("Bearer {}", llm.api_key))
        .set("Content-Type", "application/json")
        .send_json(provider_body(&llm.endpoint, request_body(&llm.model, req), req));
    let response = match response {
        Ok(r) => r,
        Err(ureq::Error::Status(code, r)) => {
            let text = r.into_string().unwrap_or_default();
            let message = text.chars().take(300).collect::<String>();
            return Err(if code == 429 || code >= 500 {
                CallError::Retryable { status: Some(code), message }
            } else {
                CallError::Fatal(format!("http_{code}: {message}"))
            });
        }
        Err(ureq::Error::Transport(t)) => return Err(CallError::Retryable { status: None, message: t.to_string() }),
    };
    let root: Value = response.into_json().map_err(|e| CallError::Retryable { status: None, message: format!("body: {e}") })?;
    // A blank or choice-less reply is the model's hiccup, not a refusal: tried again like a 5xx.
    parse_reply(&root).map_err(|e| if e.starts_with("reply_blank") || e == "reply_without_choices" { CallError::Retryable { status: None, message: e } } else { CallError::Fatal(e) })
}

/// A reply in the player's language (Docs/STREET_I18N_PLAN_20261014.md): a long context can drift the model into the
/// other Chinese script however firmly the prompt asks, so Chinese replies are converted to the language's script
/// (zh-TW: Taiwan Traditional, zh-CN: Mainland Simplified); English is left as it is.
pub fn localize(text: &str, lang: &str) -> String {
    match lang {
        "en" => text.to_string(),
        "zh-CN" => zhconv::zhconv(text, zhconv::Variant::ZhCN),
        _ => zhconv::zhconv(text, zhconv::Variant::ZhTW),
    }
}

pub fn parse_reply(root: &Value) -> Result<ChatReply, String> {
    let choice = root["choices"].get(0).ok_or("reply_without_choices")?;
    let content = choice["message"]["content"].as_str().unwrap_or("").to_string();
    if content.trim().is_empty() {
        return Err(format!("reply_blank (finish_reason={})", choice["finish_reason"].as_str().unwrap_or("?")));
    }
    let u = &root["usage"];
    let n = |k: &str| u[k].as_u64().unwrap_or(0);
    Ok(ChatReply {
        content,
        model: root["model"].as_str().unwrap_or("").to_string(),
        finish_reason: choice["finish_reason"].as_str().unwrap_or("").to_string(),
        usage: Usage {
            prompt_tokens: n("prompt_tokens"),
            completion_tokens: n("completion_tokens"),
            cache_hit_tokens: n("prompt_cache_hit_tokens"),
            cache_miss_tokens: n("prompt_cache_miss_tokens"),
        },
    })
}
