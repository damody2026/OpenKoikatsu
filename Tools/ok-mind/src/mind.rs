//! The mind: workers pull jobs by priority, results wait in a box until the game thread polls.
use crate::config::{Config, LlmSettings};
use crate::context::{self, Context, Contexts};
use crate::deepseek::{self, CallError, ChatReply, ChatRequest, Message};
use crate::memory::{self, MemoryRecord, Memories, RecallQuery};
use crate::profile::{self, Profile, Profiles};
use crate::queue::{Job, Priority, Queue, RateGate};
use crate::store::{self, State, Stats, Trace};
use serde_json::{json, Value};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

struct Shared {
    cfg: Config,
    llm: Result<LlmSettings, String>,
    queue: Queue,
    gate: RateGate,
    results: Mutex<Vec<Value>>,
    state: Mutex<State>,
    session: Mutex<Stats>,
    trace: Trace,
    contexts: Contexts,
    memories: Memories,
    profiles: Profiles,
    in_flight: AtomicUsize,
    finance: Option<crate::finance::Finance>,
}

pub struct Mind {
    shared: Arc<Shared>,
    next_id: AtomicU64,
    workers: Mutex<Vec<JoinHandle<()>>>,
}

fn now_ms() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis() as u64).unwrap_or(0)
}

fn panic_text(payload: Box<dyn std::any::Any + Send>) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "unknown panic".into()
    }
}

fn text_field<'a>(job: &'a Value, name: &str) -> Result<&'a str, String> {
    job[name].as_str().filter(|s| !s.trim().is_empty()).ok_or_else(|| format!("job_invalid: {name} is required"))
}

impl Mind {
    pub fn open(cfg: Config) -> Result<Mind, String> {
        std::fs::create_dir_all(&cfg.save_dir).map_err(|e| format!("save_dir_unusable: {e}"))?;
        crate::text::init(cfg.data_dir.as_deref());
        let state = store::load_state(&cfg.save_dir)?;
        let shared = Arc::new(Shared {
            llm: cfg.llm(),
            queue: Queue::new(),
            gate: RateGate::new(cfg.per_minute),
            results: Mutex::new(Vec::new()),
            state: Mutex::new(state),
            session: Mutex::new(Stats::default()),
            trace: Trace::new(&cfg.save_dir),
            contexts: Contexts::new(&cfg.save_dir),
            memories: Memories::new(&cfg.save_dir),
            profiles: Profiles::new(&cfg.save_dir, cfg.data_dir.as_ref().map(|d| d.join("StreetSocialData").join("Profiles"))),
            in_flight: AtomicUsize::new(0),
            finance: match &cfg.data_dir {
                Some(d) => Some(crate::finance::Finance::open_with(&cfg.save_dir, d, cfg.finance_seed.unwrap_or_else(|| now_ms() ^ 0x5EED_0F_C0FFEE), cfg.wallet_currency.as_deref().unwrap_or("TWD"))?),
                None => None,
            },
            cfg,
        });
        let mut workers = Vec::new();
        for n in 0..shared.cfg.workers {
            let s = shared.clone();
            let handle = std::thread::Builder::new()
                .name(format!("ok-mind-{n}"))
                .spawn(move || worker(s))
                .map_err(|e| format!("worker_spawn_failed: {e}"))?;
            workers.push(handle);
        }
        // Each heroine's memory file read and parsed now, off the caller: the first memory written for her in a session
        // otherwise read it on the game thread (~3 ms).
        {
            let s = shared.clone();
            let _ = std::thread::Builder::new().name("ok-mind-preload".into()).spawn(move || s.memories.preload());
        }
        Ok(Mind { shared, next_id: AtomicU64::new(1), workers: Mutex::new(workers) })
    }

    pub fn submit(&self, job_json: &str) -> Result<u64, String> {
        let job: Value = serde_json::from_str(job_json).map_err(|e| format!("job_invalid: {e}"))?;
        let kind = job["kind"].as_str().ok_or("job_invalid: kind is required")?.to_string();
        let default_priority = match kind.as_str() {
            "chat" | "talk" => "dialogue",
            "summarize" => "summary",
            "reflect" => "reflection",
            "profile" => "profile",
            _ => "background",
        };
        let priority_name = job["priority"].as_str().unwrap_or(default_priority);
        let priority = Priority::parse(priority_name).ok_or_else(|| format!("job_invalid: unknown priority {priority_name}"))?;
        match kind.as_str() {
            "ping" | "chat" | "talk" | "observe" | "summarize" | "profile" | "reflect" | "note" => {}
            "debug_panic" if self.shared.cfg.debug => {}
            other => return Err(format!("job_invalid: unknown kind {other}")),
        }
        if kind == "chat" {
            chat_request(&job)?;
        }
        if kind == "talk" {
            text_field(&job, "heroine")?;
            text_field(&job, "system")?;
            text_field(&job, "user")?;
            if !job["legacy_history"].is_null() {
                serde_json::from_value::<Vec<Message>>(job["legacy_history"].clone()).map_err(|e| format!("job_invalid: legacy_history: {e}"))?;
            }
        }
        if kind == "summarize" || kind == "profile" || kind == "observe" || kind == "reflect" || kind == "note" {
            text_field(&job, "heroine")?;
        }
        if kind == "profile" && !job["card"].is_object() {
            return Err("job_invalid: card must be an object".into());
        }
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        if kind == "observe" {
            // A rule- or template-written memory: stored at once, no LLM (plan §6.0).
            let heroine = text_field(&job, "heroine")?.to_string();
            let record = observation(&job)?;
            let memory_id = self.shared.memories.add(&heroine, record)?;
            self.shared.results.lock().unwrap().push(json!({"id": id, "kind": "observe", "ok": true, "heroine": heroine, "memory": memory_id, "tag": job["tag"].clone()}));
            return Ok(id);
        }
        if kind == "note" {
            let heroine = text_field(&job, "heroine")?.to_string();
            let messages = add_note(&self.shared, &heroine, text_field(&job, "text")?)?;
            self.shared.results.lock().unwrap().push(json!({"id": id, "kind": "note", "ok": true, "heroine": heroine, "context_messages": messages, "tag": job["tag"].clone()}));
            return Ok(id);
        }
        if kind == "ping" {
            // No worker needed; the answer still arrives through poll like every other job.
            self.shared.results.lock().unwrap().push(json!({"id": id, "kind": "ping", "ok": true, "echo": job["echo"].clone()}));
            return Ok(id);
        }
        if !self.shared.queue.push(Job { id, priority, kind, payload: job }) {
            return Err("closed".into());
        }
        Ok(id)
    }

    pub fn query(&self, query_json: &str) -> Result<String, String> {
        let q: Value = serde_json::from_str(query_json).map_err(|e| format!("query_invalid: {e}"))?;
        let s = &self.shared;
        match q["kind"].as_str() {
            Some("status") => {
                let (model, llm_error) = match &s.llm {
                    Ok(l) => (Value::from(l.model.clone()), Value::Null),
                    Err(e) => (Value::Null, Value::from(e.clone())),
                };
                Ok(json!({
                    "version": VERSION,
                    "abi": crate::ABI_VERSION,
                    "model": model,
                    "llm_error": llm_error,
                    "queued": s.queue.len(),
                    "in_flight": s.in_flight.load(Ordering::Relaxed),
                    "session": *s.session.lock().unwrap(),
                    "lifetime": s.state.lock().unwrap().stats,
                })
                .to_string())
            }
            Some("context") => {
                let heroine = text_field(&q, "heroine")?;
                let slot = s.contexts.slot(heroine);
                let mut guard = slot.lock().unwrap();
                if guard.is_none() {
                    *guard = s.contexts.load(heroine)?;
                }
                Ok(match guard.as_ref() {
                    None => json!({"heroine": heroine, "exists": false}),
                    Some(c) => json!({"heroine": heroine, "exists": true, "chars": c.chars(), "messages": c.history.len(),
                                      "turns": c.turns, "compactions": c.compactions}),
                }
                .to_string())
            }
            // A rollback point of her mind (a line of the conversation on screen): how long her context is, how often
            // it was compacted and the id her next memory takes.
            Some("checkpoint") => {
                let heroine = text_field(&q, "heroine")?;
                let slot = s.contexts.slot(heroine);
                let mut guard = slot.lock().unwrap();
                if guard.is_none() {
                    *guard = s.contexts.load(heroine)?;
                }
                let (messages, compactions) = guard.as_ref().map(|c| (c.history.len(), c.compactions)).unwrap_or((0, 0));
                Ok(json!({"heroine": heroine, "messages": messages, "compactions": compactions, "memory_next": s.memories.next(heroine)?}).to_string())
            }
            // Back to a checkpoint (the player rolled the conversation back and says something else): her context loses
            // the turns after it and her memory the records made since. A context compacted since cannot be cut back
            // exactly: it is left as it is (`context_kept`), the memories are still dropped.
            Some("rollback") => {
                let heroine = text_field(&q, "heroine")?;
                let messages = q["messages"].as_u64().ok_or("query_invalid: messages is required")? as usize;
                let compactions = q["compactions"].as_u64().unwrap_or(0) as u32;
                let slot = s.contexts.slot(heroine);
                let mut guard = slot.lock().unwrap();
                if guard.is_none() {
                    *guard = s.contexts.load(heroine)?;
                }
                let mut dropped_messages = 0usize;
                let mut context_kept = false;
                if let Some(ctx) = guard.as_mut() {
                    if ctx.compactions == compactions && messages <= ctx.history.len() {
                        dropped_messages = ctx.history.len() - messages;
                        ctx.history.truncate(messages);
                        ctx.summarized_upto = ctx.summarized_upto.min(messages);
                        ctx.turns = ctx.turns.saturating_sub((dropped_messages / 2) as u64);
                        s.contexts.save(ctx)?;
                    } else {
                        context_kept = dropped_messages == 0 && (ctx.compactions != compactions || messages > ctx.history.len());
                    }
                }
                let dropped_memories = s.memories.truncate(heroine, q["memory_next"].as_u64().unwrap_or(0))?;
                Ok(json!({"heroine": heroine, "ok": true, "dropped_messages": dropped_messages, "dropped_memories": dropped_memories, "context_kept": context_kept}).to_string())
            }
            Some("recall") => {
                let heroine = text_field(&q, "heroine")?;
                let strings = |name: &str| -> Vec<String> { q[name].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect()).unwrap_or_default() };
                let query = RecallQuery { entities: strings("entities"), keywords: strings("keywords"), now: q["now"].as_i64().unwrap_or(0), k: q["k"].as_u64().unwrap_or(5).min(20) as usize };
                Ok(json!({"heroine": heroine, "memories": s.memories.recall(heroine, &query)?}).to_string())
            }
            Some("memories") => {
                let heroine = text_field(&q, "heroine")?;
                Ok(json!({"heroine": heroine, "memories": s.memories.all(heroine)?}).to_string())
            }
            Some("profile") => {
                let heroine = text_field(&q, "heroine")?;
                Ok(json!({"heroine": heroine, "profile": s.profiles.load(heroine)?}).to_string())
            }
            // Plan §15: lottery, sports betting, markets and predictions; synchronous, never touches the LLM.
            // "kind" routes the query here; the op's own kind (an order's market/limit, an alert's above/below) comes
            // as "fin_kind" and is put back before the op reads it.
            Some("fin") => match &s.finance {
                Some(f) => {
                    let mut q = q.clone();
                    if let Some(o) = q.as_object_mut() {
                        match o.remove("fin_kind") {
                            Some(k) => {
                                o.insert("kind".into(), k);
                            }
                            None => {
                                o.remove("kind");
                            }
                        }
                    }
                    Ok(f.op(&q)?.to_string())
                }
                None => Err("fin_disabled: data_dir not configured".into()),
            },
            Some(other) => Err(format!("query_invalid: unknown kind {other}")),
            None => Err("query_invalid: kind is required".into()),
        }
    }

    pub fn poll(&self) -> Option<String> {
        let mut results = self.shared.results.lock().unwrap();
        if results.is_empty() {
            return None;
        }
        let out = Value::Array(std::mem::take(&mut *results));
        Some(out.to_string())
    }

    pub fn save(&self) -> Result<(), String> {
        self.save_queued()?;
        store::flush()
    }

    /// Every write queued on the store's writer, without waiting for the disk (store::flush waits).
    pub fn save_queued(&self) -> Result<(), String> {
        self.shared.memories.save_all()?;
        let state = self.shared.state.lock().unwrap().clone();
        store::save_state(&self.shared.cfg.save_dir, &state)
    }

    /// Never blocks the game for long: idle workers stop at once; a worker stuck in an HTTP call is left to
    /// finish on its own (its result is dropped). The DLL stays loaded for the life of the process.
    pub fn close(&self) -> Result<(), String> {
        self.shared.queue.close();
        let deadline = Instant::now() + Duration::from_secs(2);
        let mut workers = std::mem::take(&mut *self.workers.lock().unwrap());
        while !workers.is_empty() && Instant::now() < deadline {
            workers.retain(|w| !w.is_finished());
            std::thread::sleep(Duration::from_millis(10));
        }
        for w in workers.into_iter().filter(|w| w.is_finished()) {
            let _ = w.join();
        }
        self.save()
    }
}

fn chat_request(job: &Value) -> Result<ChatRequest, String> {
    let messages: Vec<Message> = serde_json::from_value(job["messages"].clone()).map_err(|e| format!("job_invalid: messages: {e}"))?;
    if messages.is_empty() {
        return Err("job_invalid: messages is empty".into());
    }
    Ok(ChatRequest {
        messages,
        max_tokens: job["max_tokens"].as_u64().unwrap_or(400).clamp(1, 8192) as u32,
        temperature: job["temperature"].as_f64().unwrap_or(1.0).clamp(0.0, 2.0) as f32,
        json: job["json"].as_bool().unwrap_or(true),
        thinking: job["thinking"].as_bool().unwrap_or(false),
    })
}

fn worker(s: Arc<Shared>) {
    let agent = deepseek::agent(s.cfg.timeout_s);
    let quick = deepseek::agent(s.cfg.dialogue_timeout_s);
    // Compaction writes a summary of thousands of characters: minutes, not the seconds a spoken reply gets.
    let long = deepseek::agent(s.cfg.timeout_s.max(300));
    while let Some(job) = s.queue.pop() {
        let agent = if job.priority == Priority::Dialogue { &quick } else { &agent };
        s.in_flight.fetch_add(1, Ordering::Relaxed);
        let id = job.id;
        let kind = job.kind.clone();
        let tag = job.payload["tag"].clone();
        let result = catch_unwind(AssertUnwindSafe(|| run(&s, agent, &job)))
            .unwrap_or_else(|p| json!({"ok": false, "error": format!("panic: {}", panic_text(p))}));
        s.in_flight.fetch_sub(1, Ordering::Relaxed);
        if s.queue.is_closed() {
            break;
        }
        let mut out = result;
        out["id"] = json!(id);
        out["kind"] = json!(kind);
        out["tag"] = tag;
        let compact_after = out.as_object_mut().and_then(|o| o.remove("compact_after")).and_then(|v| v.as_str().map(String::from));
        // Her context grew past the limit: compacted after the reply went out, so she is never kept waiting on it
        // (inline, a summary that timed out at the dialogue timeout held every one of her turns for 150 s). Her slot
        // is taken before the reply is handed over, so her next turn always starts from the compacted context.
        let slot = compact_after.as_ref().map(|h| s.contexts.slot(h));
        let mut guard = slot.as_ref().map(|sl| sl.lock().unwrap());
        s.results.lock().unwrap().push(out);
        if let Some(guard) = guard.as_mut() {
            if let Some(ctx) = guard.as_mut() {
                if ctx.chars() > s.cfg.compact_at_chars {
                    match compact(&s, &long, &job, ctx) {
                        Ok(()) => {
                            let saved = s.contexts.save(ctx);
                            s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "compact", "ok": true, "context_chars": ctx.chars(), "save_error": saved.err()}));
                        }
                        Err(e) => s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "compact", "ok": false, "error": e})),
                    }
                }
            }
        }
    }
}

fn run(s: &Shared, agent: &ureq::Agent, job: &Job) -> Value {
    match job.kind.as_str() {
        "chat" => run_chat(s, agent, job),
        "talk" => run_talk(s, agent, job),
        "summarize" => run_summarize(s, agent, job),
        "reflect" => run_reflect(s, agent, job),
        "profile" => run_profile(s, agent, job),
        "debug_panic" => panic!("debug_panic requested by job {}", job.id),
        other => json!({"ok": false, "error": format!("unknown kind {other}")}),
    }
}

/// Sleeps in short steps so closing the mind is never stuck behind a long wait.
fn wait(s: &Shared, mut d: Duration) -> bool {
    while !d.is_zero() {
        if s.queue.is_closed() {
            return false;
        }
        let step = d.min(Duration::from_millis(100));
        std::thread::sleep(step);
        d -= step;
    }
    !s.queue.is_closed()
}

struct Called {
    reply: Result<ChatReply, String>,
    attempts: u32,
    ms: u64,
}

/// The player's language a job asks for ("lang" in its payload; zh-TW when absent).
pub fn job_lang(job: &Job) -> &str {
    job.payload.get("lang").and_then(Value::as_str).filter(|l| matches!(*l, "zh-TW" | "zh-CN" | "en")).unwrap_or("zh-TW")
}

/// One logical request: waits for the rate gate, retries 429/5xx/network failures, records stats and the trace.
fn call_llm(s: &Shared, agent: &ureq::Agent, job: &Job, step: &str, req: &ChatRequest) -> Called {
    let started = Instant::now();
    // A Simplified game reads the whole request in Simplified: a name or text its tables have not translated yet (the
    // zh-TW source as fallback) does not mix scripts into what the model reads. Deterministic, so a cached prefix stays
    // the same from one request to the next.
    let localized = (job_lang(job) == "zh-CN").then(|| {
        let mut r = req.clone();
        for m in &mut r.messages {
            m.content = deepseek::localize(&m.content, "zh-CN");
        }
        r
    });
    let req = localized.as_ref().unwrap_or(req);
    let llm = match &s.llm {
        Ok(l) => l,
        Err(e) => return Called { reply: Err(e.clone()), attempts: 0, ms: 0 },
    };
    let mut attempts = 0u32;
    let reply = loop {
        loop {
            let d = s.gate.admit(job.priority, Instant::now());
            if d.is_zero() {
                break;
            }
            if !wait(s, d) {
                return Called { reply: Err("closed".into()), attempts, ms: started.elapsed().as_millis() as u64 };
            }
        }
        attempts += 1;
        match deepseek::call(agent, llm, req) {
            Ok(mut reply) => {
                reply.content = deepseek::localize(&reply.content, job_lang(job));
                break Ok(reply);
            }
            Err(CallError::Retryable { status, .. }) if (attempts as usize) <= s.cfg.retry_delays_ms.len() => {
                let delay = Duration::from_millis(s.cfg.retry_delays_ms[attempts as usize - 1]);
                if status == Some(429) {
                    s.gate.pause(Instant::now() + delay);
                }
                s.session.lock().unwrap().retries += 1;
                s.state.lock().unwrap().stats.retries += 1;
                if !wait(s, delay) {
                    return Called { reply: Err("closed".into()), attempts, ms: started.elapsed().as_millis() as u64 };
                }
            }
            Err(e) => break Err(e.message()),
        }
    };
    let ms = started.elapsed().as_millis() as u64;
    let mut trace = json!({
        "t": now_ms(), "id": job.id, "kind": job.kind, "step": step, "tag": job.payload["tag"], "priority": job.priority.name(),
        "model": llm.model, "ms": ms, "attempts": attempts, "messages": req.messages.len(),
    });
    if s.cfg.trace_content {
        trace["request"] = json!(req.messages);
    }
    match &reply {
        Ok(r) => {
            for stats in [&mut *s.session.lock().unwrap(), &mut s.state.lock().unwrap().stats] {
                stats.calls += 1;
                stats.ok += 1;
                stats.prompt_tokens += r.usage.prompt_tokens;
                stats.completion_tokens += r.usage.completion_tokens;
                stats.cache_hit_tokens += r.usage.cache_hit_tokens;
                stats.cache_miss_tokens += r.usage.cache_miss_tokens;
            }
            trace["ok"] = json!(true);
            trace["usage"] = json!(r.usage);
            if s.cfg.trace_content {
                trace["response"] = json!(r.content);
            }
        }
        Err(e) => {
            for stats in [&mut *s.session.lock().unwrap(), &mut s.state.lock().unwrap().stats] {
                stats.calls += 1;
                stats.failed += 1;
            }
            trace["ok"] = json!(false);
            trace["error"] = json!(e);
        }
    }
    if let Some(h) = job.payload["heroine"].as_str() {
        trace["heroine"] = json!(h);
    }
    s.trace.write(&trace);
    Called { reply, attempts, ms }
}

fn run_chat(s: &Shared, agent: &ureq::Agent, job: &Job) -> Value {
    let req = match chat_request(&job.payload) {
        Ok(r) => r,
        Err(e) => return json!({"ok": false, "error": e}),
    };
    let mut c = call_llm(s, agent, job, "chat", &req);
    // Asked for a JSON object, and what came back is not one: the group talk lost a whole turn to a reply whose last
    // string ended in 」 with the response format echoed after it ({"type": "json_object"}). Mended when only its end
    // is broken (text after the object, an unclosed string or bracket), otherwise asked again once.
    if req.json {
        if let Ok(r) = &mut c.reply {
            if !is_json_object(&r.content) {
                let mended = leading_json_object(&r.content).or_else(|| close_json_object(&r.content));
                if let Some(mended) = mended {
                    s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "chat_json_mended"}));
                    r.content = mended;
                }
            }
        }
        if matches!(&c.reply, Ok(r) if !is_json_object(&r.content)) {
            s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "chat_json_malformed"}));
            let mut again = req.clone();
            if let Ok(r) = &c.reply {
                again.messages.push(Message { role: "assistant".into(), content: r.content.clone() });
            }
            again.messages.push(Message { role: "user".into(), content: crate::text::t(job_lang(job), "mind.malformed_json_reminder", &[]) });
            let c2 = call_llm(s, agent, job, "chat", &again);
            if matches!(&c2.reply, Ok(r2) if is_json_object(&r2.content)) {
                c = c2;
            }
        }
    }
    match c.reply {
        Ok(r) => json!({"ok": true, "content": r.content, "model": r.model, "finish_reason": r.finish_reason,
                        "usage": r.usage, "ms": c.ms, "attempts": c.attempts}),
        Err(e) => json!({"ok": false, "error": e, "ms": c.ms, "attempts": c.attempts}),
    }
}

/// A turn with one heroine (plan §7.0): her persistent context + this turn's full-state message.
fn run_talk(s: &Shared, agent: &ureq::Agent, job: &Job) -> Value {
    let p = &job.payload;
    let (heroine, system, user) = match (text_field(p, "heroine"), text_field(p, "system"), text_field(p, "user")) {
        (Ok(h), Ok(sy), Ok(u)) => (h.to_string(), sy.to_string(), u.to_string()),
        _ => return json!({"ok": false, "error": "job_invalid: heroine, system and user are required"}),
    };
    let slot = s.contexts.slot(&heroine);
    // Held for the whole turn: the same heroine's turns never interleave.
    let mut guard = slot.lock().unwrap();
    if guard.is_none() {
        match s.contexts.load(&heroine) {
            Ok(loaded) => *guard = loaded,
            Err(e) => return json!({"ok": false, "error": e}),
        }
    }
    let ctx = guard.get_or_insert_with(|| {
        // First turn ever: bring over the conversation the game kept before the mind existed.
        let legacy: Vec<Message> = serde_json::from_value(p["legacy_history"].clone()).unwrap_or_default();
        Context { schema: context::CONTEXT_SCHEMA, heroine: heroine.clone(), system: system.clone(), history: legacy, ..Default::default() }
    });
    // Her earlier words in this language's script (the player may have changed language since).
    context::localize_history(&mut ctx.history, job_lang(job));
    // A changed persona or rule set replaces the system prompt; the cache misses once and the history stays.
    let system_changed = ctx.system != system;
    if system_changed {
        ctx.system = system.clone();
    }
    // Compaction runs after the reply (worker, compact_after); a failed one is tried again after her next turn.
    let compacted = false;
    let mut messages = Vec::with_capacity(ctx.history.len() + 2);
    messages.push(Message { role: "system".into(), content: ctx.system.clone() });
    messages.extend(ctx.history.iter().cloned());
    // The language she answers in (the persona and her history may be in another), asked on every turn. Her context
    // keeps the turn exactly as sent, so the next request repeats this prefix byte for byte (DeepSeek's cache).
    let user = format!("{user}{}", crate::text::t(job_lang(job), "mind.reply_language", &[]));
    messages.push(Message { role: "user".into(), content: user.clone() });
    let req = ChatRequest {
        messages,
        max_tokens: p["max_tokens"].as_u64().unwrap_or(400).clamp(1, 8192) as u32,
        temperature: p["temperature"].as_f64().unwrap_or(1.0).clamp(0.0, 2.0) as f32,
        json: p["json"].as_bool().unwrap_or(true),
        thinking: false,
    };
    let mut c = call_llm(s, agent, job, "talk", &req);
    // She must not know the future: a reply naming a later year than the turn's 【現在時間】 is asked again once.
    if let (Ok(r), Some(now)) = (&c.reply, game_year(&user)) {
        if let Some(y) = future_year(&r.content, now) {
            s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "talk_future_year", "year": y, "now": now}));
            let mut again = req.clone();
            again.messages.push(Message { role: "assistant".into(), content: r.content.clone() });
            again.messages.push(Message { role: "user".into(), content: crate::text::t(job_lang(job), "mind.future_year_reminder", &[("year", y.to_string()), ("now", now.to_string())]) });
            let c2 = call_llm(s, agent, job, "talk", &again);
            if let Ok(r2) = &c2.reply {
                if future_year(&r2.content, now).is_none() {
                    c = c2;
                }
            }
        }
    }
    // A whole reply object and then text of the model's own (it went on to write his next line, "【玩家说】…"): only the
    // object is her reply. Asked again, she saw that invented line as "his last line" and answered it, and the invented
    // line stayed in her history.
    if req.json {
        if let Ok(r) = &mut c.reply {
            if !talk_reply_well_formed(&r.content) {
                if let Some(object) = leading_json_object(&r.content).filter(|o| talk_reply_well_formed(o)) {
                    s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "talk_trailing_dropped", "dropped": r.content.len().saturating_sub(object.len())}));
                    r.content = object;
                }
            }
        }
    }
    // Asked for JSON but what came back is not an object with its lines (a malformed object, lines as one string
    // glued to stray keys): asked again once, so the turn is not lost to "no line".
    if req.json {
        if let Ok(r) = &c.reply {
            if !talk_reply_well_formed(&r.content) {
                s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "talk_malformed"}));
                let mut again = req.clone();
                again.messages.push(Message { role: "assistant".into(), content: r.content.clone() });
                again.messages.push(Message { role: "user".into(), content: crate::text::t(job_lang(job), "mind.malformed_reply_reminder", &[]) });
                let c2 = call_llm(s, agent, job, "talk", &again);
                if matches!(&c2.reply, Ok(r2) if talk_reply_well_formed(&r2.content)) {
                    c = c2;
                }
            }
        }
    }
    // Word for word what she said last turn (a weak model repeats itself when the turn looks alike): asked again once.
    if let (Ok(r), Some(last)) = (&c.reply, ctx.history.iter().rev().find(|m| m.role == "assistant")) {
        let lines = talk_reply_lines(&r.content);
        if !lines.is_empty() && lines == talk_reply_lines(&last.content) {
            s.trace.write(&json!({"t": now_ms(), "id": job.id, "step": "talk_repeated"}));
            let mut again = req.clone();
            again.messages.push(Message { role: "assistant".into(), content: r.content.clone() });
            again.messages.push(Message { role: "user".into(), content: crate::text::t(job_lang(job), "mind.repeated_reply_reminder", &[]) });
            let c2 = call_llm(s, agent, job, "talk", &again);
            if matches!(&c2.reply, Ok(r2) if talk_reply_well_formed(&r2.content) && talk_reply_lines(&r2.content) != lines) {
                c = c2;
            }
        }
    }
    match c.reply {
        Ok(r) => {
            // Stored exactly as sent and received, so the next request starts with this one.
            ctx.history.push(Message { role: "user".into(), content: user });
            ctx.history.push(Message { role: "assistant".into(), content: r.content.clone() });
            ctx.turns += 1;
            let saved = s.contexts.save(ctx);
            let mut out = json!({"ok": true, "heroine": heroine, "content": r.content, "model": r.model, "usage": r.usage,
                   "ms": c.ms, "attempts": c.attempts, "context_chars": ctx.chars(), "context_messages": ctx.history.len(),
                   "compacted": compacted, "system_changed": system_changed, "save_error": saved.err()});
            if ctx.chars() > s.cfg.compact_at_chars {
                out["compact_after"] = json!(heroine);
            }
            out
        }
        Err(e) => json!({"ok": false, "heroine": heroine, "error": e, "ms": c.ms, "attempts": c.attempts, "compacted": compacted}),
    }
}

/// A talk reply the game can use: one JSON object (a ```json fence around it is fine) whose "lines" is a list of
/// strings with at least one that is not blank.
fn talk_reply_well_formed(content: &str) -> bool {
    !talk_reply_lines(content).is_empty()
}

/// The first JSON object at the start of a reply (a ```json fence before it is fine), without whatever follows it.
fn leading_json_object(content: &str) -> Option<String> {
    let t = content.trim_start();
    let t = t.strip_prefix("```json").or_else(|| t.strip_prefix("```")).unwrap_or(t).trim_start();
    let mut values = serde_json::Deserializer::from_str(t).into_iter::<Value>();
    match values.next() {
        Some(Ok(v)) if v.is_object() => Some(t[..values.byte_offset()].to_string()),
        _ => None,
    }
}

/// Whether a reply is one JSON object (a ```json fence around it is fine).
fn is_json_object(content: &str) -> bool {
    let t = content.trim();
    let t = t.strip_prefix("```json").or_else(|| t.strip_prefix("```")).unwrap_or(t);
    let t = t.strip_suffix("```").unwrap_or(t).trim();
    matches!(serde_json::from_str::<Value>(t), Ok(v) if v.is_object())
}

/// A reply object whose end is broken, closed: the response format echoed after it is dropped, then an unclosed string,
/// a dangling comma or key, and the open brackets are closed. None when even that does not make one object.
fn close_json_object(content: &str) -> Option<String> {
    let mut t = content.trim();
    t = t.strip_prefix("```json").or_else(|| t.strip_prefix("```")).unwrap_or(t).trim_start();
    t = t.strip_suffix("```").unwrap_or(t).trim_end();
    if let Some(at) = t.rfind("{\"type\"") {
        if at > 0 && t[at..].contains("json_object") {
            t = t[..at].trim_end();
        }
    }
    if !t.starts_with('{') {
        return None;
    }
    let (mut in_string, mut escaped, mut open) = (false, false, Vec::new());
    for ch in t.chars() {
        if in_string {
            match (escaped, ch) {
                (true, _) => escaped = false,
                (false, '\\') => escaped = true,
                (false, '"') => in_string = false,
                _ => {}
            }
            continue;
        }
        match ch {
            '"' => in_string = true,
            '{' => open.push('}'),
            '[' => open.push(']'),
            '}' | ']' => {
                if open.pop() != Some(ch) {
                    return None;
                }
            }
            _ => {}
        }
    }
    let mut out = t.to_string();
    if in_string {
        if escaped {
            out.pop();
        }
        // Raw line breaks inside the unclosed string (the model went on after it) are not JSON.
        let end = out.trim_end().len();
        out.truncate(end);
        out.push('"');
    } else {
        let end = out.trim_end().len();
        out.truncate(end);
        if out.ends_with(',') {
            out.pop();
        } else if out.ends_with(':') {
            out.push_str("null");
        }
    }
    while let Some(close) = open.pop() {
        out.push(close);
    }
    serde_json::from_str::<Value>(&out).ok().filter(Value::is_object).map(|_| out)
}

/// The non-blank lines of a talk reply (empty when it is not such a JSON object).
fn talk_reply_lines(content: &str) -> Vec<String> {
    let t = content.trim();
    let t = t.strip_prefix("```json").or_else(|| t.strip_prefix("```")).unwrap_or(t);
    let t = t.strip_suffix("```").unwrap_or(t).trim();
    match serde_json::from_str::<Value>(t) {
        Ok(v) => v["lines"].as_array().map(|a| a.iter().filter_map(|l| l.as_str()).map(|x| x.trim().to_string()).filter(|x| !x.is_empty()).collect()).unwrap_or_default(),
        Err(_) => Vec::new(),
    }
}

/// The game year from a turn's 【現在時間】2010年9月13日… line.
fn game_year(user: &str) -> Option<i32> {
    let rest = &user[user.find("【現在時間】")? + "【現在時間】".len()..];
    let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    digits.parse().ok().filter(|y| (1990..2100).contains(y))
}

/// A year after `now` written as a year in the text ("2015年", "2015 年"); bare numbers such as ¥2015 are not years.
fn future_year(text: &str, now: i32) -> Option<i32> {
    let chars: Vec<char> = text.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        if chars[i].is_ascii_digit() && (i == 0 || !chars[i - 1].is_ascii_digit()) {
            let mut j = i;
            while j < chars.len() && chars[j].is_ascii_digit() {
                j += 1;
            }
            if j - i == 4 {
                let y: i32 = chars[i..j].iter().collect::<String>().parse().unwrap_or(0);
                let mut k = j;
                while k < chars.len() && chars[k] == ' ' {
                    k += 1;
                }
                if y > now && y < 2100 && k < chars.len() && chars[k] == '年' {
                    return Some(y);
                }
            }
            i = j;
        } else {
            i += 1;
        }
    }
    None
}

/// Replaces everything but the last keep_tail_chars with one summary turn written by the heroine herself.
/// The summary request is her full context plus an instruction, so it reuses the cached prefix.
fn compact(s: &Shared, agent: &ureq::Agent, job: &Job, ctx: &mut Context) -> Result<(), String> {
    let start = context::tail_start(&ctx.history, s.cfg.keep_tail_chars);
    if start == 0 {
        return Ok(());
    }
    let mut messages = Vec::with_capacity(ctx.history.len() + 2);
    messages.push(Message { role: "system".into(), content: ctx.system.clone() });
    messages.extend(ctx.history.iter().cloned());
    messages.push(Message { role: "user".into(), content: context::compaction_instruction(s.cfg.keep_tail_chars, s.cfg.summary_chars, job_lang(job)) });
    let req = ChatRequest { messages, max_tokens: 8192, temperature: 0.3, json: false, thinking: false };
    let summary = call_llm(s, agent, job, "compact", &req).reply?.content;
    let tail = ctx.history.split_off(start);
    ctx.history = context::summary_turns(&summary);
    ctx.history.extend(tail);
    ctx.compactions += 1;
    // The summary turn replaces messages [0, start); indices in the kept tail shift down by start - 2.
    ctx.summarized_upto = if ctx.summarized_upto > start { ctx.summarized_upto - start + 2 } else { 2 };
    s.contexts.save(ctx)
}

fn observation(job: &Value) -> Result<MemoryRecord, String> {
    let summary = text_field(job, "summary")?.chars().take(200).collect::<String>();
    let kind = job["memory_kind"].as_str().unwrap_or("observation").to_string();
    let strings = |name: &str| -> Vec<String> { job[name].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect()).unwrap_or_default() };
    Ok(MemoryRecord {
        t: memory::game_minutes(job["day"].as_i64().unwrap_or(0), job["minute"].as_i64().unwrap_or(0)),
        kind_type: job["memory_type"].as_str().unwrap_or("observation").to_string(),
        importance: job["importance"].as_f64().map(|v| v as f32).unwrap_or_else(|| memory::base_importance(&kind)),
        kind,
        summary,
        entities: strings("entities"),
        place: job["place"].as_str().unwrap_or("").to_string(),
        emotional_tag: job["emotional_tag"].as_str().unwrap_or("").to_string(),
        quoteable: job["quoteable"].as_bool().unwrap_or(false),
        source: job["source"].as_str().unwrap_or("self").to_string(),
        confidence: job["confidence"].as_f64().unwrap_or(1.0) as f32,
        author: job["author"].as_str().unwrap_or("template").to_string(),
        ..Default::default()
    })
}


/// End of an interaction (plan §6.5): a branch on her own context, so the prompt is a cache hit. The summary is
/// not written back into the context (it is already there); it becomes searchable memories.
fn run_summarize(s: &Shared, agent: &ureq::Agent, job: &Job) -> Value {
    let p = &job.payload;
    let heroine = p["heroine"].as_str().unwrap_or("").to_string();
    let slot = s.contexts.slot(&heroine);
    let mut guard = slot.lock().unwrap();
    if guard.is_none() {
        match s.contexts.load(&heroine) {
            Ok(c) => *guard = c,
            Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
        }
    }
    let Some(ctx) = guard.as_mut() else { return json!({"ok": true, "heroine": heroine, "memories": []}) };
    let start = ctx.summarized_upto.min(ctx.history.len());
    let fresh = ctx.history.len() - start;
    if fresh == 0 {
        return json!({"ok": true, "heroine": heroine, "memories": []});
    }
    let mut messages = vec![Message { role: "system".into(), content: ctx.system.clone() }];
    messages.extend(ctx.history.iter().cloned());
    messages.push(Message { role: "user".into(), content: crate::text::t(job_lang(job), "mind.summarize_instruction", &[("n", fresh.to_string())]) });
    let req = ChatRequest { messages, max_tokens: 800, temperature: 0.4, json: true, thinking: false };
    let c = call_llm(s, agent, job, "summarize", &req);
    let reply = match c.reply {
        Ok(r) => r,
        Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
    };
    let parsed: Value = serde_json::from_str(&reply.content).unwrap_or(Value::Null);
    let now = memory::game_minutes(p["day"].as_i64().unwrap_or(0), p["minute"].as_i64().unwrap_or(0));
    let mut ids = Vec::new();
    for m in parsed["memories"].as_array().map(|a| a.iter().take(5).collect::<Vec<_>>()).unwrap_or_default() {
        let summary = m["summary"].as_str().unwrap_or("").trim().chars().take(120).collect::<String>();
        if summary.is_empty() {
            continue;
        }
        let kind = m["kind"].as_str().unwrap_or("conversation").to_string();
        let record = MemoryRecord {
            t: now,
            kind_type: "conversation".into(),
            importance: m["importance"].as_f64().map(|v| v as f32).unwrap_or_else(|| memory::base_importance(&kind)),
            kind,
            summary,
            entities: vec!["player".into()],
            place: p["place"].as_str().unwrap_or("").to_string(),
            emotional_tag: m["emotional_tag"].as_str().unwrap_or("").to_string(),
            quoteable: m["quoteable"].as_bool().unwrap_or(false),
            source: "player".into(),
            confidence: 1.0,
            author: "llm".into(),
            ..Default::default()
        };
        match s.memories.add(&heroine, record) {
            Ok(id) => ids.push(id),
            Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
        }
    }
    ctx.summarized_upto = ctx.history.len();
    let _ = s.contexts.save(ctx);
    json!({"ok": true, "heroine": heroine, "memories": ids, "usage": reply.usage})
}

const PROFILE_SYSTEM: &str = "你是一款成人向戀愛模擬遊戲的角色設定師。所有角色都是 18 歲以上的成年人。你只輸出 JSON。";

/// First launch (plan §3.1): DeepSeek writes her profile from the card; afterwards the file is kept and can be
/// edited by hand. `force` regenerates.
fn run_profile(s: &Shared, agent: &ureq::Agent, job: &Job) -> Value {
    let p = &job.payload;
    let heroine = p["heroine"].as_str().unwrap_or("").to_string();
    if !p["force"].as_bool().unwrap_or(false) {
        match s.profiles.load(&heroine) {
            Ok(Some(existing)) => return json!({"ok": true, "heroine": heroine, "profile": existing, "generated": false}),
            Ok(None) => {}
            Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
        }
    }
    let places: Vec<String> = p["places"].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect()).unwrap_or_default();
    let mut last_error = String::new();
    for attempt in 0..2 {
        let mut prompt = profile::generation_prompt(&p["card"], &places);
        if attempt > 0 {
            prompt.push_str(&format!("\n上一次的結果不合格（{last_error}），請修正後重新輸出。"));
        }
        let req = ChatRequest {
            messages: vec![Message { role: "system".into(), content: PROFILE_SYSTEM.into() }, Message { role: "user".into(), content: prompt }],
            max_tokens: 3000,
            temperature: 0.9,
            json: true,
            thinking: false,
        };
        let content = match call_llm(s, agent, job, "profile", &req).reply {
            Ok(r) => r.content,
            Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
        };
        let generated: Profile = match serde_json::from_str(&content) {
            Ok(g) => g,
            Err(e) => {
                last_error = format!("JSON 格式錯誤：{e}");
                continue;
            }
        };
        match profile::validate(generated, &heroine, &places) {
            Ok(valid) => {
                if let Err(e) = s.profiles.save(&valid) {
                    return json!({"ok": false, "heroine": heroine, "error": e});
                }
                return json!({"ok": true, "heroine": heroine, "profile": valid, "generated": true});
            }
            Err(e) => last_error = e,
        }
    }
    json!({"ok": false, "heroine": heroine, "error": last_error})
}


/// End of a game day for a heroine who spent time with the player (plan §6.5): a branch on her own context.
fn run_reflect(s: &Shared, agent: &ureq::Agent, job: &Job) -> Value {
    let p = &job.payload;
    let heroine = p["heroine"].as_str().unwrap_or("").to_string();
    let slot = s.contexts.slot(&heroine);
    let mut guard = slot.lock().unwrap();
    if guard.is_none() {
        match s.contexts.load(&heroine) {
            Ok(c) => *guard = c,
            Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
        }
    }
    let Some(ctx) = guard.as_mut() else { return json!({"ok": true, "heroine": heroine, "skipped": "no context"}) };
    let mut messages = vec![Message { role: "system".into(), content: ctx.system.clone() }];
    messages.extend(ctx.history.iter().cloned());
    let mut instruction = crate::text::t(job_lang(job), "mind.reflect_instruction", &[]);
    if let Some(extra) = p["today"].as_str() {
        instruction.push_str(&crate::text::t(job_lang(job), "mind.reflect_today", &[]));
        instruction.push_str(extra);
    }
    messages.push(Message { role: "user".into(), content: instruction });
    let req = ChatRequest { messages, max_tokens: 900, temperature: 0.5, json: true, thinking: false };
    let reply = match call_llm(s, agent, job, "reflect", &req).reply {
        Ok(r) => r,
        Err(e) => return json!({"ok": false, "heroine": heroine, "error": e}),
    };
    let parsed: Value = serde_json::from_str(&reply.content).unwrap_or(Value::Null);
    let now = memory::game_minutes(p["day"].as_i64().unwrap_or(0), p["minute"].as_i64().unwrap_or(0));
    let mut ids = Vec::new();
    let mut add = |kind_type: &str, kind: &str, text: &str, importance: f32| {
        let text = text.trim().chars().take(120).collect::<String>();
        if text.is_empty() {
            return;
        }
        let record = MemoryRecord { t: now, kind_type: kind_type.into(), kind: kind.into(), summary: text, entities: vec!["player".into()], importance, source: "self".into(), confidence: 1.0, author: "llm".into(), ..Default::default() };
        if let Ok(id) = s.memories.add(&heroine, record) {
            ids.push(id);
        }
    };
    if let Some(summary) = parsed["summary"].as_str() {
        add("summary", "summary", summary, 0.5);
    }
    for r in parsed["reflections"].as_array().map(|a| a.iter().take(3).collect::<Vec<_>>()).unwrap_or_default() {
        if let Some(text) = r.as_str() {
            add("reflection", "reflection", text, 0.7);
        }
    }
    json!({"ok": true, "heroine": heroine, "memories": ids, "rel_adjust": parsed["rel_adjust"], "intent": parsed["intent"], "usage": reply.usage})
}

/// Plan §7.0 rule 3: something she learned outside her talks with him (gossip, a scene she overheard) is appended to
/// her context as a fixed-format pair, never rewriting what came before. Done at once, no LLM.
fn add_note(s: &Shared, heroine: &str, text: &str) -> Result<usize, String> {
    let slot = s.contexts.slot(heroine);
    let mut guard = slot.lock().unwrap();
    if guard.is_none() {
        *guard = s.contexts.load(heroine)?;
    }
    // No context yet: she has never talked with him through the mind; the memory stream still has it.
    let Some(ctx) = guard.as_mut() else { return Ok(0) };
    ctx.history.push(Message { role: "user".into(), content: format!("[記憶更新]\n{}", text.trim()) });
    ctx.history.push(Message { role: "assistant".into(), content: context::SUMMARY_ACK.into() });
    s.contexts.save(ctx)?;
    Ok(ctx.history.len())
}

#[cfg(test)]
mod future_tests {
    use super::*;
    #[test]
    fn replies_naming_later_years_are_caught() {
        let user = "【現在時間】2010年9月13日 星期一 15:05（下午）（回答日期、星期、幾點以這個為準）\n【玩家說】你覺得以後會怎樣？";
        assert_eq!(game_year(user), Some(2010));
        assert_eq!(game_year("沒有時間"), None);
        assert_eq!(future_year(r#"{"lines":["等到2015年再說吧"]}"#, 2010), Some(2015));
        assert_eq!(future_year("2012 年的世界末日？", 2010), Some(2012));
        assert_eq!(future_year("我 2008年就來台灣了，今年是2010年", 2010), None);
        assert_eq!(future_year("這杯 2015 元太貴了", 2010), None);
        assert_eq!(future_year("電話 12015年", 2010), None);
    }
}

#[cfg(test)]
mod reply_tests {
    use super::*;
    #[test]
    fn text_after_her_reply_object_is_dropped() {
        let raw = "{\"lines\": [\"我其实刚吃饱了，\", \"分我一口尝尝也行。\"], \"favor_delta\": 1}\n\n【玩家说】好啊。今天下午你和胡桃在书局聊得开心吗？";
        assert!(!talk_reply_well_formed(raw));
        let object = leading_json_object(raw).unwrap();
        assert!(talk_reply_well_formed(&object));
        assert!(!object.contains("书局"));
        assert_eq!(leading_json_object("```json\n{\"lines\": [\"嗯。\"]}\n```").as_deref(), Some("{\"lines\": [\"嗯。\"]}"));
        assert_eq!(leading_json_object("好啊 {\"lines\": []}"), None);
        // The group talk's broken end: the last string closed with 」 and the response format echoed after it.
        let broken = "{\"lines\": [{\"who\": \"惠惠\", \"text\": \"給你就是了！\"}], \"favor\": {\"惠惠\": 2}, \"why\": \"惠惠害羞卻嘴硬。」\n\n{\"type\": \"json_object\"}";
        assert!(!is_json_object(broken));
        let mended = close_json_object(broken).unwrap();
        let v: Value = serde_json::from_str(&mended).unwrap();
        assert_eq!(v["lines"][0]["text"], "給你就是了！");
        assert_eq!(v["why"], "惠惠害羞卻嘴硬。」");
        assert_eq!(close_json_object("{\"lines\": [\"嗯\", ").as_deref(), Some("{\"lines\": [\"嗯\"]}"));
        assert_eq!(close_json_object("{\"a\": 1}}"), None);
        assert_eq!(close_json_object("好啊"), None);
        assert!(is_json_object("```json\n{\"a\": 1}\n```"));
    }
}
