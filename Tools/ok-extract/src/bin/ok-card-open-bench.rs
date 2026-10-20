// Times opening OK cards from the base library the way the game does (session::open):
//   ok-card-open-bench LIBRARY CARD [CARD ...]
//   ok-card-open-bench --sequence LIBRARY CARD[@OUTFIT] ...   (one uncached open each, in order: card / outfit switches)
// Per card: a cold open (OK_NOCACHE: nothing read from or written to the open cache), then two normal opens
// (the first may build and store the cache, the second should be served by it). Stage timings go to stderr.
use std::{path::Path, sync::atomic::Ordering, time::Instant};

fn run() -> Result<serde_json::Value, String> {
    let args: Vec<_> = std::env::args().collect();
    // ok-card-open-bench --compare LIBRARY CARD...: does a fresh open equal the open cache's (older build's) entry?
    if args.get(1).map(String::as_str) == Some("--compare") {
        let lib = Path::new(args.get(2).ok_or("missing LIBRARY")?);
        std::env::set_var("OK_BASE_LIBRARY", lib);
        // OK_BENCH_PREWARM=1: the reader's startup prewarm first (the layered mods catalogue, OK_FIRSTLOAD=A3, needs it).
        if std::env::var_os("OK_BENCH_PREWARM").is_some() { ok_extract::prewarm(lib)?; }
        let mut rows = Vec::new();
        for card in &args[3..] {
            let (fresh, cached) = ok_extract::open_card_compare_cache(lib, Path::new(card), 0)?;
            if let Some(dir) = std::env::var_os("OK_BENCH_DUMP") {
                let dir = Path::new(&dir);
                let _ = std::fs::write(dir.join("fresh.bin"), &fresh);
                if let Some(c) = &cached { let _ = std::fs::write(dir.join("cached.bin"), c); }
            }
            let verdict = match &cached { None => "no cache entry", Some(c) if *c == fresh => "IDENTICAL", Some(_) => "DIFFERENT" };
            eprintln!("ok-card-open-bench: compare {card}: {verdict} (fresh {} bytes, cached {})", fresh.len(), cached.as_ref().map_or(0, Vec::len));
            rows.push(serde_json::json!({"card": card, "verdict": verdict}));
        }
        return Ok(serde_json::json!({"compare": rows}));
    }
    // ok-card-open-bench --abm CARD: the card's bone modifiers (ABMX) and face/body sliders (diagnostics).
    if args.get(1).map(String::as_str) == Some("--abm") {
        let card = ok_extract::parse_card(Path::new(args.get(2).ok_or("missing CARD")?))?;
        for b in &card.abm { eprintln!("abm {} loc={} {:?}", b.name, b.location, b.coord.first().map(|d| (d.scale, d.length, d.pos, d.rot))); }
        eprintln!("shape_face={:?}", card.shape_face);
        return Ok(serde_json::json!({"abm": card.abm.len()}));
    }
    if args.get(1).map(String::as_str) == Some("--sequence") {
        std::env::set_var("OK_NOCACHE", "1");
        let lib = Path::new(args.get(2).ok_or("missing LIBRARY")?);
        // Original KKS cards open as the reader opens them: this library plus OK_GAME_ROOT's zipmods they name.
        std::env::set_var("OK_BASE_LIBRARY", lib);
        if std::env::var_os("OK_BENCH_PREWARM").is_some() { ok_extract::prewarm(lib)?; }
        let mut rows = Vec::new();
        for spec in &args[3..] {
            let (card, outfit) = spec.rsplit_once('@').map(|(c, o)| (c, o.parse().unwrap_or(0))).unwrap_or((spec.as_str(), 0));
            let start = Instant::now();
            // OK_BENCH_FRESH_THREAD=1: each open on a new thread, as a reader that spawns one per load.
            let parts = if std::env::var_os("OK_BENCH_FRESH_THREAD").is_some() {
                let (lib, card) = (lib.to_path_buf(), Path::new(card).to_path_buf());
                std::thread::spawn(move || ok_extract::open_card_like_reader(&lib, Path::new(""), &card, outfit, [0; 9])).join().map_err(|_| "open panicked")??
            } else { ok_extract::open_card_like_reader(lib, Path::new(""), Path::new(card), outfit, [0; 9])? };
            let seconds = start.elapsed().as_secs_f64();
            eprintln!("ok-card-open-bench: {card}@{outfit} {seconds:.2}s");
            rows.push(serde_json::json!({"card": card, "outfit": outfit, "seconds": seconds, "parts": parts}));
        }
        return Ok(serde_json::json!({"library": lib.display().to_string(), "sequence": rows}));
    }
    if args.len() < 3 { return Err("usage: ok-card-open-bench LIBRARY CARD [CARD ...]".into()); }
    let lib = Path::new(&args[1]);
    let mut rows = Vec::new();
    for card in &args[2..] {
        let card = Path::new(card);
        let key = ok_extract::oklib_session_cache_key(lib, card, 0);
        let mut times = Vec::new();
        for (label, nocache) in [("cold", true), ("normal1", false), ("normal2", false)] {
            if nocache { std::env::set_var("OK_NOCACHE", "1"); } else { std::env::remove_var("OK_NOCACHE"); }
            let start = Instant::now();
            let parts = ok_extract::open_session(lib, Some(card), 0)?;
            times.push(serde_json::json!({"run": label, "seconds": start.elapsed().as_secs_f64(), "parts": parts,
                "from_cache": ok_extract::oklib_last_from_cache()}));
        }
        rows.push(serde_json::json!({"card": card.display().to_string(), "bytes": std::fs::metadata(card).map(|m| m.len()).unwrap_or(0),
            "cache_key": key, "runs": times}));
    }
    let _ = Ordering::Relaxed;
    Ok(serde_json::json!({"library": lib.display().to_string(), "cards": rows}))
}
fn main() { match run() { Ok(r) => println!("{}", serde_json::to_string_pretty(&r).unwrap()), Err(e) => { eprintln!("{e}"); std::process::exit(1); } } }
