//! Caption timing check for a sung song: the line spans the old rule gave (one line per breath when the counts match,
//! else even shares of the voice) and align_heard from each breath's transcription, as JSON for comparing against the audio (e.g. ASR of each span).
//!
//!   cargo run --example song_lines -- <song>.mouth.json <lyrics.txt> [heard.json: one transcription per breath] [windows.json: per window]

use ok_rules::song::{align_heard, even_lines, phrases, refine_heard, song_windows, unit_texts};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: song_lines <song>.mouth.json <lyrics.txt>");
        std::process::exit(2);
    }
    let mouth: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(&args[1]).expect("mouth.json")).expect("mouth json");
    let env: Vec<f32> = mouth["values"].as_array().expect("values").iter().map(|v| v.as_f64().unwrap_or(0.0) as f32).collect();
    let hop = mouth["hop_ms"].as_f64().unwrap_or(10.0) as f32 / 1000.0;
    let lyrics = std::fs::read_to_string(&args[2]).expect("lyrics");
    let texts = unit_texts(&lyrics);
    let breaths = phrases(&env, hop);
    let old = if breaths.len() == texts.len() || breaths.is_empty() {
        breaths.clone()
    } else {
        even_lines(&env, hop, (breaths[0].0, breaths[breaths.len() - 1].1), texts.len())
    };
    let heard: Vec<String> = args.get(3).map(|p| serde_json::from_str(&std::fs::read_to_string(p).expect("heard")).expect("heard json")).unwrap_or_default();
    let from_heard = if heard.len() == breaths.len() { align_heard(&breaths, &heard, &texts, &texts) } else { Vec::new() };
    let windows: Vec<String> = args.get(4).map(|p| serde_json::from_str(&std::fs::read_to_string(p).expect("windows")).expect("windows json")).unwrap_or_default();
    let refined = refine_heard(&from_heard, &breaths, &texts, &windows);
    let heard_lines: Vec<(f32, String)> = from_heard.iter().map(|(a, t, _)| (*a, t.clone())).collect();
    println!("{}", serde_json::json!({"texts": texts, "breaths": breaths, "windows": song_windows(&breaths), "old": old, "heard": heard_lines, "refined": refined}));
}
