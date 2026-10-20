use super::*;
use crate::queue::{Job, Priority, Queue, RateGate};
use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpListener;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

static NEXT_DIR: AtomicU32 = AtomicU32::new(0);

fn temp_dir() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("ok-mind-test-{}-{}", std::process::id(), NEXT_DIR.fetch_add(1, Ordering::Relaxed)));
    let _ = std::fs::remove_dir_all(&dir);
    dir
}

/// A one-request-per-connection fake of the DeepSeek endpoint. Replies are served in order; bodies are recorded.
fn mock_server(replies: Vec<(u16, Value)>) -> (String, Arc<Mutex<Vec<Value>>>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let url = format!("http://{}/chat/completions", listener.local_addr().unwrap());
    let seen = Arc::new(Mutex::new(Vec::new()));
    let record = seen.clone();
    std::thread::spawn(move || {
        for (status, body) in replies {
            let (stream, _) = listener.accept().unwrap();
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut length = 0usize;
            loop {
                let mut line = String::new();
                reader.read_line(&mut line).unwrap();
                if line == "\r\n" || line.is_empty() {
                    break;
                }
                if let Some(v) = line.to_ascii_lowercase().strip_prefix("content-length:") {
                    length = v.trim().parse().unwrap();
                }
            }
            let mut buf = vec![0u8; length];
            reader.read_exact(&mut buf).unwrap();
            record.lock().unwrap().push(serde_json::from_slice(&buf).unwrap_or(Value::Null));
            let text = body.to_string();
            let mut stream = stream;
            write!(
                stream,
                "HTTP/1.1 {status} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{text}",
                text.len()
            )
            .unwrap();
        }
    });
    (url, seen)
}

fn reply(content: &str, hit: u64, miss: u64) -> Value {
    json!({"model": "deepseek-v4-flash", "choices": [{"message": {"role": "assistant", "content": content}, "finish_reason": "stop"}],
           "usage": {"prompt_tokens": hit + miss, "completion_tokens": 7, "prompt_cache_hit_tokens": hit, "prompt_cache_miss_tokens": miss}})
}

fn cstring(v: &Value) -> CString {
    CString::new(v.to_string()).unwrap()
}

unsafe fn take(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    let s = CStr::from_ptr(p).to_str().unwrap().to_string();
    okm_free(p);
    Some(s)
}

unsafe fn wait_results(h: *mut OkmHandle, count: usize) -> Vec<Value> {
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut all = Vec::new();
    while all.len() < count && Instant::now() < deadline {
        if let Some(text) = take(okm_poll(h)) {
            all.extend(serde_json::from_str::<Vec<Value>>(&text).unwrap());
        } else {
            std::thread::sleep(Duration::from_millis(10));
        }
    }
    all
}

#[test]
fn ping_round_trip_and_state_is_saved() {
    let dir = temp_dir();
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "test"})).as_ptr());
        assert!(!h.is_null(), "{:?}", take(okm_last_error()));
        let id = okm_submit(h, cstring(&json!({"kind": "ping", "echo": {"hello": "世界"}})).as_ptr());
        assert!(id > 0);
        let results = wait_results(h, 1);
        assert_eq!(results[0]["id"], json!(id));
        assert_eq!(results[0]["echo"]["hello"], "世界");
        assert!(take(okm_poll(h)).is_none(), "results are delivered once");
        let status: Value = serde_json::from_str(&take(okm_query(h, cstring(&json!({"kind": "status"})).as_ptr())).unwrap()).unwrap();
        assert_eq!(status["model"], "deepseek-v4-flash");
        okm_close(h);
    }
    assert!(dir.join("state.json").exists());
}

#[test]
fn chat_retries_429_and_reports_cache_usage() {
    let dir = temp_dir();
    let (url, seen) = mock_server(vec![(429, json!({"error": "busy"})), (200, reply("{\"lines\":[\"你好\"]}", 640, 22))]);
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "test", "endpoint": url, "retry_delays_ms": [0, 0, 0]})).as_ptr());
        assert!(!h.is_null());
        let job = json!({"kind": "chat", "tag": "freetalk:girl1", "messages": [{"role": "system", "content": "s"}, {"role": "user", "content": "嗨"}], "max_tokens": 300});
        let id = okm_submit(h, cstring(&job).as_ptr());
        let r = &wait_results(h, 1)[0];
        assert_eq!(r["id"], json!(id));
        assert_eq!(r["ok"], true, "{r}");
        assert_eq!(r["attempts"], 2);
        assert_eq!(r["tag"], "freetalk:girl1");
        assert_eq!(r["content"], "{\"lines\":[\"你好\"]}");
        assert_eq!(r["usage"]["cache_hit_tokens"], 640);
        assert_eq!(r["usage"]["cache_miss_tokens"], 22);
        okm_close(h);
    }
    let bodies = seen.lock().unwrap();
    assert_eq!(bodies.len(), 2);
    assert_eq!(bodies[1]["model"], "deepseek-v4-flash");
    assert_eq!(bodies[1]["response_format"]["type"], "json_object");
    assert_eq!(bodies[1]["max_tokens"], 300);
    assert_eq!(bodies[1]["thinking"]["type"], "disabled", "dialogue never spends tokens on reasoning");
    let trace = std::fs::read_to_string(dir.join("trace.jsonl")).unwrap();
    assert!(trace.contains("\"cache_hit_tokens\":640"));
    assert!(!trace.contains("Bearer"), "the key never reaches the trace");
}

#[test]
fn fatal_http_error_is_not_retried() {
    let dir = temp_dir();
    let (url, seen) = mock_server(vec![(401, json!({"error": "bad key"}))]);
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "x", "endpoint": url, "retry_delays_ms": [0, 0, 0]})).as_ptr());
        okm_submit(h, cstring(&json!({"kind": "chat", "messages": [{"role": "user", "content": "a"}]})).as_ptr());
        let r = &wait_results(h, 1)[0];
        assert_eq!(r["ok"], false);
        assert!(r["error"].as_str().unwrap().starts_with("http_401"));
        okm_close(h);
    }
    assert_eq!(seen.lock().unwrap().len(), 1);
}

#[test]
fn a_panicking_job_never_takes_the_mind_down() {
    let dir = temp_dir();
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "x", "debug": true, "workers": 1})).as_ptr());
        okm_submit(h, cstring(&json!({"kind": "debug_panic"})).as_ptr());
        let r = &wait_results(h, 1)[0];
        assert_eq!(r["ok"], false);
        assert!(r["error"].as_str().unwrap().starts_with("panic:"));
        // The single worker survived and still serves jobs.
        let id = okm_submit(h, cstring(&json!({"kind": "ping"})).as_ptr());
        assert_eq!(wait_results(h, 1)[0]["id"], json!(id));
        okm_close(h);
    }
}

#[test]
fn bad_input_returns_errors_not_crashes() {
    unsafe {
        assert!(okm_open(std::ptr::null()).is_null());
        assert_eq!(take(okm_last_error()).unwrap(), "null_argument");
        assert!(okm_open(cstring(&json!({"workers": 2})).as_ptr()).is_null());
        assert!(take(okm_last_error()).unwrap().contains("save_dir"));
        assert_eq!(okm_submit(std::ptr::null_mut(), cstring(&json!({"kind": "ping"})).as_ptr()), 0);
        assert_eq!(take(okm_last_error()).unwrap(), "null_handle");
        let h = okm_open(cstring(&json!({"save_dir": temp_dir(), "api_key": "x"})).as_ptr());
        assert_eq!(okm_submit(h, c"not json".as_ptr()), 0);
        assert!(take(okm_last_error()).unwrap().starts_with("job_invalid"));
        assert_eq!(okm_submit(h, cstring(&json!({"kind": "debug_panic"})).as_ptr()), 0, "debug jobs need debug mode");
        assert_eq!(okm_submit(h, cstring(&json!({"kind": "chat", "messages": []})).as_ptr()), 0);
        okm_close(h);
        okm_close(std::ptr::null_mut());
        okm_free(std::ptr::null());
    }
}

#[test]
fn queue_serves_dialogue_first_then_oldest() {
    let q = Queue::new();
    let job = |id, p| Job { id, priority: p, kind: "chat".into(), payload: Value::Null };
    q.push(job(1, Priority::Reflection));
    q.push(job(2, Priority::Summary));
    q.push(job(3, Priority::Dialogue));
    q.push(job(4, Priority::Summary));
    let order: Vec<u64> = (0..4).map(|_| q.pop().unwrap().id).collect();
    assert_eq!(order, vec![3, 2, 4, 1]);
    q.close();
    assert!(q.pop().is_none());
    assert!(!q.push(job(5, Priority::Dialogue)));
}

#[test]
fn rate_gate_caps_per_minute_and_pause_spares_dialogue() {
    let gate = RateGate::new(2);
    let t = Instant::now();
    assert!(gate.admit(Priority::Summary, t).is_zero());
    assert!(gate.admit(Priority::Summary, t).is_zero());
    assert!(gate.admit(Priority::Summary, t) > Duration::from_secs(59));
    assert!(gate.admit(Priority::Summary, t + Duration::from_secs(61)).is_zero());
    let gate = RateGate::new(10);
    gate.pause(t + Duration::from_secs(5));
    assert!(gate.admit(Priority::Reflection, t) >= Duration::from_secs(4));
    assert!(gate.admit(Priority::Dialogue, t).is_zero());
}

#[test]
fn settings_file_fills_model_and_key() {
    let dir = temp_dir();
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("deepseek.json");
    std::fs::write(&path, "\u{feff}{\"api_key\":\" k \",\"model\":\"deepseek-v4-flash\"}").unwrap();
    let cfg = config::Config::parse(&json!({"save_dir": dir, "deepseek_settings": path}).to_string()).unwrap();
    if std::env::var("DEEPSEEK_API_KEY").map(|k| k.trim().is_empty()).unwrap_or(true) {
        let llm = cfg.llm().unwrap();
        assert_eq!(llm.api_key, "k");
        assert_eq!(llm.model, "deepseek-v4-flash");
        assert_eq!(llm.endpoint, config::DEFAULT_ENDPOINT);
    }
}

fn talk_job(heroine: &str, system: &str, user: &str) -> Value {
    json!({"kind": "talk", "tag": "freetalk", "heroine": heroine, "system": system, "user": user})
}

fn messages_of(body: &Value) -> Vec<(String, String)> {
    body["messages"].as_array().unwrap().iter().map(|m| (m["role"].as_str().unwrap().to_string(), m["content"].as_str().unwrap().to_string())).collect()
}

#[test]
fn talk_requests_extend_the_previous_request_exactly() {
    let dir = temp_dir();
    let (url, seen) = mock_server(vec![(200, reply("{\"lines\":[\"一\"]}", 0, 100)), (200, reply("{\"lines\":[\"二\"]}", 128, 20)), (200, reply("{\"lines\":[\"三\"]}", 192, 20))]);
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "k", "endpoint": url, "retry_delays_ms": []})).as_ptr());
        let mut first = talk_job("fp:abc/1", "你是小美", "[時間]2010/9/1\n[玩家說]嗨");
        first["legacy_history"] = json!([{"role": "user", "content": "舊的話"}, {"role": "assistant", "content": "{\"lines\":[\"舊回覆\"]}"}]);
        okm_submit(h, cstring(&first).as_ptr());
        let r1 = &wait_results(h, 1)[0];
        assert_eq!(r1["ok"], true, "{r1}");
        assert_eq!(r1["context_messages"], 4, "legacy pair + this exchange");
        okm_submit(h, cstring(&talk_job("fp:abc/1", "你是小美", "[時間]2010/9/1\n[玩家說]今天好嗎")).as_ptr());
        assert_eq!(wait_results(h, 1)[0]["ok"], true);
        // Another day: the legacy history is ignored now that her context exists.
        let mut third = talk_job("fp:abc/1", "你是小美", "[時間]2010/9/2\n[玩家說]早");
        third["legacy_history"] = json!([{"role": "user", "content": "不該出現"}]);
        okm_submit(h, cstring(&third).as_ptr());
        assert_eq!(wait_results(h, 1)[0]["usage"]["cache_hit_tokens"], 192);
        let status: Value = serde_json::from_str(&take(okm_query(h, cstring(&json!({"kind": "context", "heroine": "fp:abc/1"})).as_ptr())).unwrap()).unwrap();
        assert_eq!(status["turns"], 3);
        okm_close(h);
    }
    let bodies = seen.lock().unwrap();
    let (a, b, c) = (messages_of(&bodies[0]), messages_of(&bodies[1]), messages_of(&bodies[2]));
    assert_eq!(a[0], ("system".into(), "你是小美".into()));
    assert_eq!(a[1], ("user".into(), "舊的話".into()));
    // The full state goes in the user turn, then the language's standing reminder (mind_text reply_language).
    assert_eq!(a.last().unwrap().1, format!("[時間]2010/9/1\n[玩家說]嗨{}", crate::text::t("zh-TW", "mind.reply_language", &[])), "the full state goes in the user turn");
    // Every request is the previous request plus its reply plus one new turn: the cached prefix never changes.
    assert_eq!(&b[..a.len()], &a[..]);
    assert_eq!(b[a.len()], ("assistant".into(), "{\"lines\":[\"一\"]}".into()));
    assert_eq!(&c[..b.len()], &b[..]);
    assert!(!c.iter().any(|(_, t)| t == "不該出現"));
    assert!(dir.join("context").join("fp_abc_1.json").exists());
}

#[test]
fn long_context_is_compacted_to_a_summary_plus_the_tail() {
    let dir = temp_dir();
    let long = "長".repeat(60);
    let (url, seen) = mock_server(vec![
        (200, reply("{\"lines\":[\"a\"]}", 0, 10)),
        (200, reply("{\"lines\":[\"b\"]}", 0, 10)),
        (200, reply("我們第一次見面在操場，他答應週六去水族館。", 0, 10)),
        (200, reply("{\"lines\":[\"c\"]}", 0, 10)),
    ]);
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "k", "endpoint": url, "retry_delays_ms": [], "compact_at_chars": 150, "keep_tail_chars": 80})).as_ptr());
        for n in 0..3 {
            okm_submit(h, cstring(&talk_job("g", "S", &format!("{n}{long}"))).as_ptr());
            let r = &wait_results(h, 1)[0];
            assert_eq!(r["ok"], true, "{r}");
            // Compaction runs after a reply (never before one): the reply itself never reports it.
            assert_eq!(r["compacted"], false, "turn {n}");
        }
        let status: Value = serde_json::from_str(&take(okm_query(h, cstring(&json!({"kind": "context", "heroine": "g"})).as_ptr())).unwrap()).unwrap();
        assert_eq!(status["compactions"], 1);
        okm_close(h);
    }
    let bodies = seen.lock().unwrap();
    assert_eq!(bodies.len(), 4);
    let summary_req = messages_of(&bodies[2]);
    assert!(summary_req.last().unwrap().1.contains("長期記憶摘要"), "the compaction asks for a summary");
    assert_eq!(&summary_req[..summary_req.len() - 1], &messages_of(&bodies[1]).iter().cloned().chain([("assistant".to_string(), "{\"lines\":[\"b\"]}".to_string())]).collect::<Vec<_>>()[..], "the summary request reuses her cached context");
    let after = messages_of(&bodies[3]);
    assert_eq!(after[0].0, "system");
    assert!(after[1].1.starts_with("[長期記憶摘要]") && after[1].1.contains("水族館"));
    assert_eq!(after[2], ("assistant".into(), "（記住了）".into()));
    assert_eq!(after[3].0, "user", "the kept tail starts with the player's turn");
    assert!(after.last().unwrap().1.starts_with('2'));
    let kept: usize = after[3..after.len() - 1].iter().map(|(_, t)| t.chars().count()).sum();
    assert!(kept >= 60 && kept <= 200, "kept tail is about keep_tail_chars: {kept}");
}

#[test]
fn a_new_system_prompt_keeps_the_history() {
    let dir = temp_dir();
    let (url, seen) = mock_server(vec![(200, reply("{\"lines\":[\"a\"]}", 0, 10)), (200, reply("{\"lines\":[\"b\"]}", 0, 10))]);
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "k", "endpoint": url, "retry_delays_ms": []})).as_ptr());
        okm_submit(h, cstring(&talk_job("g", "舊設定", "1")).as_ptr());
        wait_results(h, 1);
        okm_close(h);
        // Reopen: the context comes back from disk.
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "k", "endpoint": url, "retry_delays_ms": []})).as_ptr());
        okm_submit(h, cstring(&talk_job("g", "新設定", "2")).as_ptr());
        let r = &wait_results(h, 1)[0];
        assert_eq!(r["system_changed"], true);
        okm_close(h);
    }
    let b = messages_of(&seen.lock().unwrap()[1]);
    assert_eq!(b[0].1, "新設定");
    assert_eq!(b[1].1, format!("1{}", crate::text::t("zh-TW", "mind.reply_language", &[])));
}

#[test]
fn tail_start_never_splits_an_exchange() {
    use crate::context::tail_start;
    use crate::deepseek::Message;
    let m = |r: &str, n: usize| Message { role: r.into(), content: "x".repeat(n) };
    let h = vec![m("user", 10), m("assistant", 10), m("user", 10), m("assistant", 10), m("user", 10), m("assistant", 10)];
    assert_eq!(tail_start(&h, 15), 4);
    assert_eq!(tail_start(&h, 25), 4, "rounds forward to a user turn");
    assert_eq!(tail_start(&h, 1000), 0);
    let big = vec![m("user", 10), m("assistant", 10), m("user", 500), m("assistant", 500)];
    assert_eq!(tail_start(&big, 100), 2, "an oversized last exchange is kept whole");
}

#[test]
fn notes_are_appended_after_the_last_turn() {
    let dir = temp_dir();
    let (url, seen) = mock_server(vec![(200, reply("{\"lines\":[\"a\"]}", 0, 10)), (200, reply("{\"lines\":[\"b\"]}", 0, 10))]);
    unsafe {
        let h = okm_open(cstring(&json!({"save_dir": dir, "api_key": "k", "endpoint": url, "retry_delays_ms": []})).as_ptr());
        okm_submit(h, cstring(&talk_job("g", "S", "1")).as_ptr());
        wait_results(h, 1);
        okm_submit(h, cstring(&json!({"kind": "note", "heroine": "g", "text": "聽小美說他昨天去了溫泉"})).as_ptr());
        assert_eq!(wait_results(h, 1)[0]["context_messages"], 4);
        okm_submit(h, cstring(&talk_job("g", "S", "2")).as_ptr());
        wait_results(h, 1);
        okm_close(h);
    }
    let bodies = seen.lock().unwrap();
    let (a, b) = (messages_of(&bodies[0]), messages_of(&bodies[1]));
    assert_eq!(&b[..a.len()], &a[..], "the note never edits the cached prefix");
    assert!(b[a.len() + 1].1.starts_with("[記憶更新]") && b[a.len() + 1].1.contains("溫泉"));
}

/// A save without her profile (new, or Saved/ wiped) starts from the shipped one in Content; a save that has its own
/// keeps it.
#[test]
fn profile_comes_from_content_when_the_save_has_none() {
    let shipped = temp_dir();
    let mut p = crate::profile::Profile::default();
    p.id = "apng-seed".into();
    p.age_in_setting = 20;
    p.money.currency = "CNY".into();
    crate::profile::Profiles::new(&shipped, None).save(&p).unwrap();
    let save = temp_dir();
    let profiles = crate::profile::Profiles::new(&save, Some(shipped.join("heroines")));
    let loaded = profiles.load("apng-seed").unwrap().expect("seeded from Content");
    assert_eq!(loaded.id, "apng-seed");
    assert!(save.join("heroines").join("apng-seed.json").exists(), "copied into the save");
    assert!(profiles.load("apng-missing").unwrap().is_none());
    let mut edited = loaded.clone();
    edited.age_in_setting = 25;
    profiles.save(&edited).unwrap();
    assert_eq!(profiles.load("apng-seed").unwrap().unwrap().age_in_setting, 25, "the save's own file wins");
}

#[test]
fn replies_are_taiwan_traditional_whatever_the_model_drifts_into() {
    let reply = |text: &str| crate::deepseek::localize(&crate::deepseek::parse_reply(&serde_json::json!({"choices": [{"message": {"content": text}, "finish_reason": "stop"}]})).unwrap().content, "zh-TW");
    assert_eq!(reply(r#"{"lines":["红豆汤圆？换话题我也会。"]}"#), r#"{"lines":["紅豆湯圓？換話題我也會。"]}"#);
    let kept = r#"{"lines":["這裡的紅豆湯圓，我記著了。"],"do_now":"talk","emotion":"害羞"}"#;
    assert_eq!(reply(kept), kept, "Traditional text and JSON stay as they are");
}

#[test]
fn a_saved_history_she_drifted_into_simplified_is_repaired_on_load() {
    use crate::deepseek::Message;
    let mut h = vec![
        Message { role: "user".into(), content: "【現在時間】2010年9月10日 记忆体".into() },
        Message { role: "assistant".into(), content: r#"{"lines":["你的嘴真金吗，还得等。"]}"#.into() },
        Message { role: "user".into(), content: format!("{}\n我们约好了", crate::context::SUMMARY_HEADER) },
    ];
    crate::context::traditional_history(&mut h);
    assert_eq!(h[0].content, "【現在時間】2010年9月10日 记忆体", "the game's turns are not touched");
    assert_eq!(h[1].content, r#"{"lines":["你的嘴真金嗎，還得等。"]}"#);
    assert!(h[2].content.ends_with("我們約好了"));
}

#[test]
fn instructions_follow_the_job_language() {
    let en = crate::text::t("en", "mind.summarize_instruction", &[("n".into(), "6".into())]);
    assert!(en.contains("last 6 messages") && en.ends_with(r#""emotional_tag": ""}]}"#), "{en}");
    let cn = crate::text::t("zh-CN", "mind.future_year_reminder", &[("year", "2015".into()), ("now", "2010".into())]);
    assert!(cn.contains("2015 年") && cn.contains("现在是 2010 年"), "{cn}");
    let tw = crate::context::compaction_instruction(3000, 10000, "zh-TW");
    assert!(tw.contains("最後約 3000 字") && tw.contains(crate::context::SUMMARY_HEADER), "{tw}");
    assert_eq!(crate::deepseek::localize("红豆汤圆", "zh-TW"), "紅豆湯圓");
    assert_eq!(crate::deepseek::localize("紅豆湯圓", "zh-CN"), "红豆汤圆");
    assert_eq!(crate::deepseek::localize("紅豆 soup", "en"), "紅豆 soup");
}
