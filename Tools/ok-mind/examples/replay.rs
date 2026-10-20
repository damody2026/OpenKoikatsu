//! Trace replay (plan §16 P12): reads trace.jsonl and reports calls, latency, tokens and prompt-cache hits by kind and
//! by real day, and checks that each heroine's consecutive talk requests extend the previous prefix (plan §7.0).
//! With --resend <id> it sends one recorded request again through the game's path and prints both replies.
//!
//!   cargo run --release --example replay -- <trace.jsonl>
//!   cargo run --release --example replay -- <trace.jsonl> --resend <id> <deepseek.json> <scratch save dir>
use ok_mind::config::Config;
use ok_mind::mind::Mind;
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::time::{Duration, Instant};

#[derive(Default)]
struct Agg {
    calls: u64,
    ok: u64,
    ms: u64,
    hit: u64,
    miss: u64,
    out: u64,
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let path = args.first().expect("usage: replay <trace.jsonl> [--resend <id> <deepseek.json> <save dir>]");
    let text = std::fs::read_to_string(path).expect("trace unreadable");
    let rows: Vec<Value> = text.lines().filter_map(|l| serde_json::from_str(l).ok()).collect();
    let mut by_kind: BTreeMap<String, Agg> = BTreeMap::new();
    let mut by_day: BTreeMap<String, Agg> = BTreeMap::new();
    let mut last_talk: BTreeMap<String, Vec<Value>> = BTreeMap::new();
    let (mut prefix_ok, mut prefix_broken) = (0, 0);
    for r in &rows {
        let kind = r["step"].as_str().or(r["kind"].as_str()).unwrap_or("?").to_string();
        let day = r["t"].as_u64().map(|t| (t / 86_400_000).to_string()).unwrap_or_default();
        for agg in [by_kind.entry(kind.clone()).or_default(), by_day.entry(day).or_default()] {
            agg.calls += 1;
            agg.ok += r["ok"].as_bool().unwrap_or(false) as u64;
            agg.ms += r["ms"].as_u64().unwrap_or(0);
            agg.hit += r["usage"]["cache_hit_tokens"].as_u64().unwrap_or(0);
            agg.miss += r["usage"]["cache_miss_tokens"].as_u64().unwrap_or(0);
            agg.out += r["usage"]["completion_tokens"].as_u64().unwrap_or(0);
        }
        if kind == "talk" {
            if let (Some(h), Some(req)) = (r["heroine"].as_str(), r["request"].as_array()) {
                if let Some(prev) = last_talk.get(h) {
                    // The previous request (minus its last user turn, which became history) must be a prefix of this one.
                    let keep = prev.len().saturating_sub(1);
                    if req.len() >= keep && prev[..keep] == req[..keep] { prefix_ok += 1 } else { prefix_broken += 1 }
                }
                last_talk.insert(h.to_string(), req.clone());
            }
        }
    }
    let show = |name: &str, a: &Agg| {
        let ratio = if a.hit + a.miss > 0 { a.hit as f64 / (a.hit + a.miss) as f64 * 100.0 } else { 0.0 };
        println!("{name:<14} calls {:>5}  ok {:>5}  avg {:>6} ms  prompt {:>8} (cache {:>5.1}%)  out {:>7}", a.calls, a.ok, if a.calls > 0 { a.ms / a.calls } else { 0 }, a.hit + a.miss, ratio, a.out);
    };
    println!("== by kind ({} rows)", rows.len());
    for (k, a) in &by_kind {
        show(k, a);
    }
    println!("== by real day (UTC day number)");
    for (k, a) in &by_day {
        show(k, a);
    }
    println!("== talk prefix extension: {prefix_ok} kept, {prefix_broken} broken (compaction breaks it once by design)");

    if let Some(i) = args.iter().position(|a| a == "--resend") {
        let id: u64 = args.get(i + 1).and_then(|s| s.parse().ok()).expect("--resend <id>");
        let settings = args.get(i + 2).expect("--resend <id> <deepseek.json> <save dir>");
        let save = args.get(i + 3).expect("--resend <id> <deepseek.json> <save dir>");
        let row = rows.iter().find(|r| r["id"].as_u64() == Some(id) && r["request"].is_array()).expect("no recorded request with that id");
        let cfg = Config::parse(&json!({"save_dir": save, "deepseek_settings": settings, "trace_content": false}).to_string()).unwrap();
        let mind = Mind::open(cfg).unwrap();
        mind.submit(&json!({"kind": "chat", "tag": "replay", "messages": row["request"], "max_tokens": 400}).to_string()).unwrap();
        let deadline = Instant::now() + Duration::from_secs(120);
        loop {
            if let Some(text) = mind.poll() {
                let results: Vec<Value> = serde_json::from_str(&text).unwrap();
                for r in results {
                    println!("recorded: {}", row["response"].as_str().unwrap_or(""));
                    println!("replayed: {} (usage {})", r["content"].as_str().unwrap_or(""), r["usage"]);
                }
                break;
            }
            assert!(Instant::now() < deadline, "no reply within 120 s");
            std::thread::sleep(Duration::from_millis(50));
        }
        mind.close().unwrap();
    }
}
