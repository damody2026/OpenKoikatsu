// Bakes cards' opens into the cards, so the reader opens them faster the first time:
//   ok-card-bake LIBRARY OUT_DIR [--outfits first|all] [--codec lz4|zstd:LEVEL] [--memo KINDS] [--no-open] CARD...
// `okBk` (default, read with OK_CARD_BAKED=1): the whole open result, opened like a cache hit (cardbake.rs).
// `--memo tex,res,mesh` adds `okMm` (read with OK_CARD_MEMO=KINDS): the memo results the open used (texture work,
// list resolution, item meshes; memosnap.rs), so the open only assembles. `--no-open` leaves `okBk` out.
// Writes OUT_DIR/<card file name>; the source cards are not changed. Run it with the reader's other settings
// (OK_RAW_TEXTURES, OK_GAME_ROOT for original cards, OK_GPU_SKIN=1 OK_HEAD_GPUSKIN=1 OK_SKELMESH=1 OK_NOPOSE=1
// OK_UE_ANIM=1 as the live reader sets them): they are part of the open-cache key the reader looks up.
use std::path::{Path, PathBuf};
use std::time::Instant;

fn main() {
    if let Err(e) = run() { eprintln!("ok-card-bake: {e}"); std::process::exit(1); }
}

fn run() -> Result<(), String> {
    use ok_extract::cardbake::{self, Codec};
    let mut args = std::env::args().skip(1);
    let usage = "usage: ok-card-bake LIBRARY OUT_DIR [--outfits first|all] [--codec lz4|zstd:LEVEL] [--memo KINDS] [--no-open] CARD...";
    let lib = PathBuf::from(args.next().ok_or(usage)?);
    let out = PathBuf::from(args.next().ok_or(usage)?);
    let (mut all, mut codec, mut memo, mut open, mut cards) = (false, Codec::Lz4, 0u8, true, Vec::new());
    while let Some(a) = args.next() {
        match a.as_str() {
            "--outfits" => all = args.next().as_deref() == Some("all"),
            "--codec" => {
                let v = args.next().unwrap_or_default();
                codec = match v.strip_prefix("zstd") {
                    Some(level) => Codec::Zstd(level.trim_start_matches(':').parse().unwrap_or(3)),
                    None => Codec::Lz4,
                };
            }
            "--memo" => memo = ok_extract::memosnap::parse_kinds(&args.next().unwrap_or_default()),
            "--no-open" => open = false,
            _ => cards.push(PathBuf::from(a)),
        }
    }
    std::fs::create_dir_all(&out).map_err(|e| e.to_string())?;
    for var in ["OK_CARD_BAKED", "OK_CARD_MEMO"] {
        if std::env::var_os(var).is_some() { return Err(format!("run without {var} (it would read old baked entries)")); }
    }
    for card in &cards {
        let t = Instant::now();
        let count = if all { ok_extract::parse_card(card).map(|c| c.coordinate_count.max(1)).unwrap_or(1) } else { 1 };
        let (mut opens, mut memos) = (Vec::new(), Vec::new());
        for outfit in 0..count as i32 {
            let (o, m) = ok_extract::bake_card(&lib, card, outfit, memo)?;
            opens.push(o);
            memos.extend(m);
        }
        let source = std::fs::read(card).map_err(|e| e.to_string())?;
        let mut baked = source.clone();
        let mut report = String::new();
        if open {
            let payload = cardbake::build(&opens, codec)?;
            report += &format!(" okBk {:.1} MB (opens {:.1} MB)", mb(payload.len()), mb(opens.iter().map(|e| e.preview.len()).sum()));
            baked = cardbake::with_chunk(&baked, cardbake::BAKE_CHUNK, &payload)?;
        }
        if !memos.is_empty() {
            let payload = cardbake::build(&memos, codec)?;
            report += &format!(" okMm {:.1} MB ({})", mb(payload.len()), ok_extract::memosnap::describe(&memos[0].preview).trim());
            baked = cardbake::with_chunk(&baked, cardbake::MEMO_CHUNK, &payload)?;
        }
        let target = out.join(card.file_name().ok_or("card has no file name")?);
        write(&target, &baked)?;
        println!("{}: {} outfit(s),{report}, card {:.1} -> {:.1} MB, {:.1}s",
            card.display(), opens.len(), mb(source.len()), mb(baked.len()), t.elapsed().as_secs_f64());
    }
    Ok(())
}

fn mb(n: usize) -> f64 { n as f64 / 1048576.0 }

fn write(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let tmp = path.with_extension("baking");
    std::fs::write(&tmp, bytes).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, path).map_err(|e| e.to_string())
}
