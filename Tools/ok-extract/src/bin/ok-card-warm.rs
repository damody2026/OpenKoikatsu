// Fills the game's open-card cache (oklib-cache) for a folder of OK cards, so the card reader opens them and switches
// their outfits from the cache (~0.1 s) instead of building each one (~4-10 s):
//   ok-card-warm LIBRARY CARD_DIR [--shard I/N] [--outfits all|first] [--priority FILE] [--until-pid PID] [--also DIR]...
// OK cards and original KKS cards (opened like the reader: base library + OK_GAME_ROOT's zipmods) alike.
// Every card's first outfit goes first, then the other outfits. Entries already cached are skipped, so it can run at
// every launch.
//
// Several processes share the work (opens inside one process are sequential; an OK card pack mounts process-wide):
// before building an entry a process claims it with `<key>.warming` in the cache folder, so any free process takes
// the next unclaimed entry. `--shard I/N` only staggers where each process starts in the folder queue.
//
// `--priority FILE`: the reader appends a card path per line when the user selects or loads a card, and
// `CARD<TAB>OUTFIT<TAB>S0,..,S8` for a clothing variant it wants ready (clothes half off ...). Between entries every
// process rereads it and builds the newest requests first (a card: all its outfits), so the card being looked at,
// its outfit switches and its clothing variants come from the cache. `--until-pid PID`: when the folder queue is done, keep serving priority
// requests until that process (the reader) exits.
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime};

struct Warm {
    lib: PathBuf,
    built: usize,
    cached: usize,
    failed: usize,
}

impl Warm {
    /// Builds (card, outfit, clothes states) unless cached or claimed by another process. True when this process
    /// built it. All worn (zeros) is the plain open.
    fn entry(&mut self, card: &Path, outfit: i32, states: [u8; 9], why: &str) -> bool {
        let Some(key) = ok_extract::oklib_session_cache_key_states(&self.lib, card, outfit, states) else { return false };
        if ok_extract::oklib_session_cached(&key) { self.cached += 1; return false; }
        let Some(_claim) = ok_extract::oklib::claim_warming(&key) else { return false };
        // A request the reader is waiting for runs at normal priority; folder warm-up stays below normal.
        set_priority(why == "priority");
        let t = Instant::now();
        let variant = if states == [0; 9] { String::new() } else { format!(" clothes {states:?}") };
        let start = ok_extract::oklib::card_memory_begin();
        let opened = ok_extract::open_card_like_reader(&self.lib, Path::new(""), card, outfit, states);
        ok_extract::release_open_caches();
        eprintln!("{}", ok_extract::oklib::card_memory_line("warm", card, start));
        match opened {
            Ok(parts) => { self.built += 1; println!("warm[{why}] {} outfit {outfit}{variant}: {parts} parts in {:.1}s", card.display(), t.elapsed().as_secs_f64()); true }
            Err(e) => { self.failed += 1; println!("warm[{why}] {} outfit {outfit}{variant}: failed: {e}", card.display()); false }
        }
    }
}

fn outfit_count(card: &Path) -> usize {
    ok_extract::parse_card(card).ok().map(|c| c.coordinate_count.max(1)).unwrap_or(1)
}

/// One priority request: a card (all its outfits, all worn) or one exact variant (outfit and clothes states).
#[derive(Clone, PartialEq)]
enum Request { Card(PathBuf), Variant(PathBuf, i32, [u8; 9]) }

/// Priority requests, newest first, without repeats. A line is `CARD` or `CARD<TAB>OUTFIT<TAB>S0,..,S8`.
fn read_priority(file: &Path) -> Vec<Request> {
    let text = std::fs::read_to_string(file).unwrap_or_default();
    let mut out: Vec<Request> = Vec::new();
    for line in text.lines().rev().map(str::trim).filter(|l| !l.is_empty()) {
        let mut fields = line.split('\t');
        let card = PathBuf::from(fields.next().unwrap_or_default());
        if !card.is_file() { continue; }
        let request = match (fields.next().and_then(|o| o.trim().parse::<i32>().ok()), fields.next()) {
            (Some(outfit), Some(states)) => {
                let values: Vec<u8> = states.split(',').filter_map(|v| v.trim().parse().ok()).collect();
                let Ok(states) = <[u8; 9]>::try_from(values) else { continue };
                Request::Variant(card, outfit, states)
            }
            _ => Request::Card(card),
        };
        if !out.contains(&request) { out.push(request); }
        if out.len() >= 12 { break; }
    }
    out
}

/// Builds the newest requests not cached yet, one entry per call. True when anything was attempted.
fn serve_priority(w: &mut Warm, file: Option<&Path>, stamp: &mut Option<SystemTime>, done: &mut Vec<(PathBuf, i32, [u8; 9])>) -> bool {
    let Some(file) = file else { return false };
    let modified = std::fs::metadata(file).and_then(|m| m.modified()).ok();
    if modified.is_none() { return false; }
    if modified != *stamp { *stamp = modified; done.clear(); }
    for request in read_priority(file) {
        let entries: Vec<(PathBuf, i32, [u8; 9])> = match request {
            Request::Card(card) => (0..outfit_count(&card) as i32).map(|o| (card.clone(), o, [0; 9])).collect(),
            Request::Variant(card, outfit, states) => vec![(card, outfit, states)],
        };
        for entry in entries {
            if done.contains(&entry) { continue; }
            done.push(entry.clone());
            w.entry(&entry.0, entry.1, entry.2, "priority");
            return true;
        }
    }
    false
}

#[cfg(windows)]
fn process_alive(pid: u32) -> bool {
    #[link(name = "kernel32")]
    extern "system" {
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> isize;
        fn GetExitCodeProcess(process: isize, code: *mut u32) -> i32;
        fn CloseHandle(handle: isize) -> i32;
    }
    const QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    const STILL_ACTIVE: u32 = 259;
    unsafe {
        let h = OpenProcess(QUERY_LIMITED_INFORMATION, 0, pid);
        if h == 0 { return false; }
        let mut code = 0u32;
        let ok = GetExitCodeProcess(h, &mut code) != 0 && code == STILL_ACTIVE;
        CloseHandle(h);
        ok
    }
}
#[cfg(not(windows))]
fn process_alive(_pid: u32) -> bool { false }

fn run() -> Result<(), String> {
    let args: Vec<String> = std::env::args().collect();
    // ok-card-warm --check LIBRARY CARD...: each outfit's cache key and whether the open cache holds it.
    if args.get(1).map(String::as_str) == Some("--check") {
        let lib = PathBuf::from(args.get(2).ok_or("missing LIBRARY")?);
        for card in &args[3..] {
            let card = Path::new(card);
            for outfit in 0..outfit_count(card) as i32 {
                let key = ok_extract::oklib_session_cache_key(&lib, card, outfit);
                println!("{} @{outfit} key={} cached={}", card.display(), key.as_deref().unwrap_or("-"), key.as_deref().is_some_and(ok_extract::oklib_session_cached));
            }
        }
        return Ok(());
    }
    if args.len() < 3 { return Err("usage: ok-card-warm LIBRARY CARD_DIR [--shard I/N] [--outfits all|first] [--priority FILE] [--until-pid PID] [--also DIR]...".into()); }
    let lib = PathBuf::from(&args[1]);
    // Original KKS cards open from the configured base library (and OK_GAME_ROOT's zipmods), as in the reader.
    std::env::set_var("OK_BASE_LIBRARY", &lib);
    let dir = PathBuf::from(&args[2]);
    let (mut shard, mut shards, mut all_outfits) = (0usize, 1usize, true);
    let (mut priority, mut until_pid): (Option<PathBuf>, Option<u32>) = (None, None);
    let mut also: Vec<PathBuf> = Vec::new();
    let mut i = 3;
    while i < args.len() {
        let value = || args.get(i + 1).cloned().ok_or(format!("{} needs a value", args[i]));
        match args[i].as_str() {
            "--shard" => {
                let v = value()?;
                let (a, b) = v.split_once('/').ok_or("--shard needs I/N")?;
                shard = a.parse().map_err(|_| "bad shard")?;
                shards = b.parse().map_err(|_| "bad shard count")?;
                if shards == 0 || shard >= shards { return Err("shard out of range".into()); }
            }
            "--outfits" => all_outfits = value()? != "first",
            "--priority" => priority = Some(PathBuf::from(value()?)),
            "--until-pid" => until_pid = Some(value()?.parse().map_err(|_| "bad pid")?),
            "--also" => also.push(PathBuf::from(value()?)),
            other => return Err(format!("unknown option {other}")),
        }
        i += 2;
    }
    // The card folder first, then each --also folder (a KKS installation's UserData/chara/female ...): every
    // folder's first outfits, then the other outfits.
    let mut folders: Vec<Vec<PathBuf>> = Vec::new();
    for dir in std::iter::once(&dir).chain(also.iter()) {
        let mut cards: Vec<PathBuf> = match std::fs::read_dir(dir) {
            Ok(entries) => entries.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("png"))).collect(),
            Err(e) => { println!("warm: {}: {e}", dir.display()); continue; }
        };
        cards.sort();
        // Every process walks the whole folder (claims split it); each starts at its own offset.
        let offset = if cards.is_empty() { 0 } else { cards.len() * shard / shards };
        cards.rotate_left(offset);
        folders.push(cards);
    }
    let cards: Vec<PathBuf> = folders.iter().flatten().cloned().collect();
    let mut queue: Vec<(PathBuf, i32)> = cards.iter().map(|c| (c.clone(), 0)).collect();
    if all_outfits { for c in &cards { for o in 1..outfit_count(c) { queue.push((c.clone(), o as i32)); } } }
    let started = Instant::now();
    let mut w = Warm { lib, built: 0, cached: 0, failed: 0 };
    let (mut stamp, mut done) = (None, Vec::new());
    let mut next = 0;
    while next < queue.len() {
        if serve_priority(&mut w, priority.as_deref(), &mut stamp, &mut done) { continue; }
        let (card, outfit) = queue[next].clone();
        next += 1;
        w.entry(&card, outfit, [0; 9], "folder");
    }
    println!("warm folder done: {} built, {} already cached, {} failed, {} cards, {:.0}s", w.built, w.cached, w.failed, cards.len(), started.elapsed().as_secs_f64());
    if let Some(pid) = until_pid {
        while process_alive(pid) {
            if !serve_priority(&mut w, priority.as_deref(), &mut stamp, &mut done) { std::thread::sleep(Duration::from_millis(300)); }
        }
        println!("warm: reader {pid} exited");
    }
    Ok(())
}
// A background tool: below normal priority, so the game and the card reader stay responsive while it runs; normal
// while building an entry the reader asked for (below normal starves behind the game and its shader compiles).
#[cfg(windows)]
fn set_priority(normal: bool) {
    #[link(name = "kernel32")]
    extern "system" {
        fn GetCurrentProcess() -> isize;
        fn SetPriorityClass(process: isize, class: u32) -> i32;
    }
    const BELOW_NORMAL_PRIORITY_CLASS: u32 = 0x4000;
    const NORMAL_PRIORITY_CLASS: u32 = 0x20;
    unsafe { SetPriorityClass(GetCurrentProcess(), if normal { NORMAL_PRIORITY_CLASS } else { BELOW_NORMAL_PRIORITY_CLASS }); }
}
#[cfg(not(windows))]
fn set_priority(_normal: bool) {}

fn main() { set_priority(false); if let Err(e) = run() { eprintln!("{e}"); std::process::exit(1); } }
