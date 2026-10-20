//! One real DeepSeek call through the same path the game uses, to check the key, endpoint and model.
//! cargo run --release --example smoke -- <deepseek.json> <scratch save dir>
use ok_mind::config::Config;
use ok_mind::mind::Mind;
use serde_json::{json, Value};
use std::time::{Duration, Instant};

fn main() {
    let mut args = std::env::args().skip(1);
    let settings = args.next().expect("usage: smoke <deepseek.json> <save dir>");
    let save_dir = args.next().expect("usage: smoke <deepseek.json> <save dir>");
    let cfg = Config::parse(&json!({"save_dir": save_dir, "deepseek_settings": settings, "trace_content": false}).to_string()).unwrap();
    let mind = Mind::open(cfg).unwrap();
    println!("status {}", mind.query(r#"{"kind":"status"}"#).unwrap());
    // A long fixed system prompt: DeepSeek caches prefixes in 64-token units, so a short one never hits.
    let rules = (1..=40).map(|n| format!("規則{n}：回答要自然、簡短、有禮貌，不要提到自己是程式。")).collect::<Vec<_>>().join("\n");
    let messages = json!([
        {"role": "system", "content": format!("{rules}\n只輸出一個 JSON 物件：{{\"lines\": [\"一句話\"]}}")},
        {"role": "user", "content": "用一句十字以內的話打招呼。"}
    ]);
    // Twice with the same prefix: the second call should report prompt-cache hits.
    for round in 0..2 {
        mind.submit(&json!({"kind": "chat", "tag": format!("smoke{round}"), "messages": messages, "max_tokens": 60}).to_string()).unwrap();
        let deadline = Instant::now() + Duration::from_secs(90);
        loop {
            if let Some(text) = mind.poll() {
                let results: Vec<Value> = serde_json::from_str(&text).unwrap();
                for r in results {
                    println!("round {round}: ok={} model={} ms={} usage={} content={}", r["ok"], r["model"], r["ms"], r["usage"], r["content"]);
                    if r["ok"] != true {
                        println!("error: {}", r["error"]);
                    }
                }
                break;
            }
            assert!(Instant::now() < deadline, "no reply within 90 s");
            std::thread::sleep(Duration::from_millis(50));
        }
    }
    mind.close().unwrap();
}
