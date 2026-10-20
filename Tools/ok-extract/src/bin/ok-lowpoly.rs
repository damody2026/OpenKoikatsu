//! Write `<card>.lowpoly.glb/.json` for APNG cards, the same export the APNG
//! save runs. Inputs are card PNGs or a crowd selection.json (`cards`).
//!
//! ok-lowpoly [--budget N] [--outfit N] [--report FILE] INPUT...
use std::path::{Path, PathBuf};
use std::time::Instant;

fn cards(input: &Path) -> Result<Vec<PathBuf>, String> {
    if input.extension().is_some_and(|e| e.eq_ignore_ascii_case("json")) {
        let text = std::fs::read_to_string(input).map_err(|e| format!("{}: {e}", input.display()))?;
        let json: serde_json::Value = serde_json::from_str(text.trim_start_matches('\u{feff}')).map_err(|e| e.to_string())?;
        return json["cards"].as_array().ok_or("selection has no cards")?
            .iter().map(|c| c.as_str().map(PathBuf::from).ok_or_else(|| "card path is not a string".to_string())).collect();
    }
    Ok(vec![input.to_path_buf()])
}

fn run() -> Result<bool, String> {
    let mut budget = ok_extract::lowpoly::DEFAULT_TRIANGLE_BUDGET;
    let mut outfit = 0;
    let mut report = None;
    let mut inputs = Vec::new();
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--budget" => budget = args.next().and_then(|v| v.parse().ok()).ok_or("--budget N")?,
            "--outfit" => outfit = args.next().and_then(|v| v.parse().ok()).ok_or("--outfit N")?,
            "--report" => report = Some(PathBuf::from(args.next().ok_or("--report FILE")?)),
            _ => inputs.extend(cards(Path::new(&arg))?),
        }
    }
    if inputs.is_empty() { return Err("usage: ok-lowpoly [--budget N] [--outfit N] [--report FILE] CARD.png|selection.json...".into()); }
    // Same live-viewer environment the editor uses when it exports.
    for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
    if std::env::var_os("OK_BUILTIN_RESOURCES").is_none() {
        let resources = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/v1");
        std::env::set_var("OK_BUILTIN_RESOURCES", resources);
    }
    std::env::set_var("OK_CLOTHES_STATES", "0,0,0,0,0,0,0,0,0");
    let mut rows = Vec::new();
    let mut failed = 0;
    for card in &inputs {
        let started = Instant::now();
        let result = (|| {
            let mut session = ok_extract::portable::load_outfit(card, outfit)?;
            let source = ok_extract::lowpoly::capture(&mut session)?;
            drop(session);
            let (glb, json) = ok_extract::lowpoly::companion_paths(card);
            ok_extract::lowpoly::export(&source, &glb, &json, budget)
        })();
        let seconds = started.elapsed().as_secs_f64();
        match result {
            Ok(r) => {
                eprintln!("{} {} -> {} triangles, {:.1}s", card.display(), r["source_triangles"], r["low_triangles"], seconds);
                rows.push(serde_json::json!({"card": card, "ok": true, "seconds": seconds,
                    "source_triangles": r["source_triangles"], "low_triangles": r["low_triangles"], "within_budget": r["within_budget"]}));
            }
            Err(e) => {
                failed += 1;
                eprintln!("{} FAILED: {e}", card.display());
                rows.push(serde_json::json!({"card": card, "ok": false, "seconds": seconds, "error": e}));
            }
        }
    }
    let summary = serde_json::json!({"budget": budget, "outfit": outfit, "cards": rows.len(), "failed": failed, "rows": rows});
    let text = serde_json::to_string_pretty(&summary).map_err(|e| e.to_string())?;
    match report { Some(path) => std::fs::write(&path, text).map_err(|e| format!("{}: {e}", path.display()))?, None => println!("{text}") }
    Ok(failed == 0)
}

fn main() {
    match run() {
        Ok(true) => {}
        Ok(false) => std::process::exit(2),
        Err(e) => { eprintln!("{e}"); std::process::exit(1); }
    }
}
