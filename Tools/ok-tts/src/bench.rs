//! `ok-tts bench`: a client that measures a running server the way the game uses it.
//!
//!   ok-tts bench --endpoint http://127.0.0.1:47861 --corpus lines.txt --out-dir D:/abs/cache [--rounds 3]
//!                [--personality 08] [--age 18] [--mode stream|file] [--fresh] [--later-frames 4,8,12] [--json result.json]
//!
//! The corpus has one line per row, optionally "personality<TAB>text". `--fresh` gives every round its own cache
//! directory, so each round measures generation (misses); without it the rounds after the first are cache hits.
//! For each request: time to the response head, to the first PCM bytes, to the end, the audio length and the
//! server's own record (/requests?id=..: queue, first_pcm, generated, cache_ready, status).

use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpStream;
use std::path::PathBuf;
use std::time::{Duration, Instant};

use anyhow::{bail, Context, Result};
use serde_json::{json, Value};

pub struct BenchArgs {
    pub endpoint: String,
    pub corpus: PathBuf,
    pub out_dir: PathBuf,
    pub rounds: usize,
    pub personality: String,
    pub age: u32,
    pub stream: bool,
    pub fresh: bool,
    pub json: Option<PathBuf>,
    /// Frames per streamed piece after the first, tried in turn for every line (empty: the server default).
    pub later_frames: Vec<usize>,
}

struct Response {
    status: u16,
    headers: Vec<(String, String)>,
    head_s: f64,
    first_byte_s: Option<f64>,
    /// (seconds, body bytes so far) at every read; chunk headers count too, a few bytes each.
    arrivals: Vec<(f64, usize)>,
    end_s: f64,
    body: Vec<u8>,
}

impl Response {
    fn header(&self, name: &str) -> Option<&str> {
        self.headers.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).map(|(_, v)| v.as_str())
    }
}

fn host_port(endpoint: &str) -> Result<String> {
    let rest = endpoint.strip_prefix("http://").context("endpoint must start with http://")?;
    Ok(rest.trim_end_matches('/').to_string())
}

/// One HTTP/1.1 exchange like the game's (a stream arrives chunked); the connection closes after it, so the body ends at EOF.
fn exchange(endpoint: &str, method: &str, path: &str, body: Option<&str>) -> Result<Response> {
    let started = Instant::now();
    let addr = host_port(endpoint)?;
    let mut stream = TcpStream::connect(&addr).with_context(|| format!("connect {addr}"))?;
    stream.set_read_timeout(Some(Duration::from_secs(120)))?;
    let body = body.unwrap_or("");
    let request = format!("{method} {path} HTTP/1.1\r\nHost: {addr}\r\nConnection: close\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: {}\r\n\r\n{body}", body.len());
    stream.write_all(request.as_bytes())?;
    let mut reader = BufReader::new(stream);
    let mut line = String::new();
    reader.read_line(&mut line)?;
    let status: u16 = line.split_whitespace().nth(1).and_then(|s| s.parse().ok()).with_context(|| format!("bad status line {line:?}"))?;
    let mut headers = Vec::new();
    loop {
        line.clear();
        reader.read_line(&mut line)?;
        let l = line.trim_end();
        if l.is_empty() {
            break;
        }
        if let Some((k, v)) = l.split_once(':') {
            headers.push((k.trim().to_string(), v.trim().to_string()));
        }
    }
    let head_s = started.elapsed().as_secs_f64();
    let chunked = headers.iter().any(|(k, v)| k.eq_ignore_ascii_case("transfer-encoding") && v.eq_ignore_ascii_case("chunked"));
    let mut raw = Vec::new();
    let mut first_byte_s = None;
    let mut arrivals = Vec::new();
    let mut buf = [0u8; 16384];
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        first_byte_s.get_or_insert(started.elapsed().as_secs_f64());
        raw.extend_from_slice(&buf[..n]);
        arrivals.push((started.elapsed().as_secs_f64(), raw.len()));
    }
    let body = if chunked { dechunk(&raw)? } else { raw };
    Ok(Response { status, headers, head_s, first_byte_s, arrivals, end_s: started.elapsed().as_secs_f64(), body })
}

/// Like the game's player: playback starts once START_SAMPLES have arrived, then plays in real time. Returns the
/// start time and how many seconds in total the player would have run dry waiting for the next piece.
fn playback(arrivals: &[(f64, usize)]) -> Option<(f64, f64)> {
    const START_SAMPLES: usize = 4800;
    let seconds = |bytes: usize| bytes as f64 / 2.0 / 24000.0;
    let start = arrivals.iter().find(|(_, b)| b / 2 >= START_SAMPLES).or(arrivals.last())?.0;
    let (mut buffered, mut stall) = (0.0f64, 0.0f64);
    for (t, bytes) in arrivals {
        if *t > start {
            // What arrived so far runs out at start + stalls + buffered; a later piece leaves a gap.
            let runs_dry = start + stall + buffered;
            if *t > runs_dry {
                stall += t - runs_dry;
            }
        }
        buffered = seconds(*bytes);
    }
    Some((start, stall))
}

fn dechunk(raw: &[u8]) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < raw.len() {
        let eol = raw[i..].windows(2).position(|w| w == b"\r\n").context("chunk size line")? + i;
        let size_text = std::str::from_utf8(&raw[i..eol])?;
        let size = usize::from_str_radix(size_text.split(';').next().unwrap_or("").trim(), 16)?;
        i = eol + 2;
        if size == 0 {
            break;
        }
        out.extend_from_slice(raw.get(i..i + size).context("chunk past the end")?);
        i += size + 2;
    }
    Ok(out)
}

fn percentile(values: &mut [f64], p: f64) -> Option<f64> {
    if values.is_empty() {
        return None;
    }
    values.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let rank = ((p / 100.0) * values.len() as f64).ceil() as usize;
    Some(values[rank.clamp(1, values.len()) - 1])
}

fn summary(rows: &[Value], key: &str) -> Value {
    let mut v: Vec<f64> = rows.iter().filter_map(|r| r[key].as_f64()).collect();
    let n = v.len();
    json!({"n": n, "p50": percentile(&mut v, 50.0), "p95": percentile(&mut v, 95.0), "max": v.last()})
}

pub fn run(a: &BenchArgs) -> Result<()> {
    if !a.out_dir.is_absolute() {
        bail!("--out-dir must be an absolute path");
    }
    let corpus = std::fs::read_to_string(&a.corpus).with_context(|| format!("read {}", a.corpus.display()))?;
    let lines: Vec<(String, String)> = corpus.lines().map(str::trim).filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(|l| match l.split_once('\t') {
            Some((p, t)) => (p.trim().to_string(), t.trim().to_string()),
            None => (a.personality.clone(), l.to_string()),
        })
        .collect();
    if lines.is_empty() {
        bail!("the corpus has no lines");
    }
    // A server that was just started loads its model first (up to two minutes).
    let waited = Instant::now();
    let health = loop {
        let h = exchange(&a.endpoint, "GET", "/health", None).ok().and_then(|r| serde_json::from_slice::<Value>(&r.body).ok());
        match h {
            Some(h) if h["ready"] == true => break h,
            _ if waited.elapsed() > Duration::from_secs(120) => bail!("{} is not ready after two minutes", a.endpoint),
            _ => std::thread::sleep(Duration::from_millis(250)),
        }
    };
    println!("server: ready={} protocol={} model={}", health["ready"], health["protocol"], health["model"]);
    let run_tag = format!("b{}", std::process::id());
    let mut rows = Vec::new();
    let variants: Vec<Option<usize>> = if a.later_frames.is_empty() { vec![None] } else { a.later_frames.iter().map(|n| Some(*n)).collect() };
    for round in 0..a.rounds.max(1) {
        let dir = if a.fresh { a.out_dir.join(format!("{run_tag}-r{round}")) } else { a.out_dir.clone() };
        for (i, (personality, text)) in lines.iter().enumerate() {
            // Each line is made once per variant (its own cache directory), in turn, so load changes hit all alike.
            for (v, later) in variants.iter().enumerate() {
                let dir = if variants.len() > 1 { dir.join(format!("v{v}")) } else { dir.clone() };
                let id = format!("{run_tag}-r{round}-{i}-v{v}");
                let mut body = json!({"text": text, "personality": personality, "age": a.age, "out_dir": dir, "request_id": id,
                                      "channel": "bench", "generation": round, "segment_index": i});
                if let Some(n) = later {
                    body["stream_later_frames"] = json!(n);
                }
                let path = if a.stream { "/synthesize_stream" } else { "/synthesize" };
                let r = exchange(&a.endpoint, "POST", path, Some(&body.to_string()))?;
                let audio_s = if a.stream { r.body.len() as f64 / 2.0 / 24000.0 } else { 0.0 };
                let record = server_record(&a.endpoint, &id);
                let file: Value = if a.stream { Value::Null } else { serde_json::from_slice(&r.body).unwrap_or(Value::Null) };
                let cache = r.header("X-Cache").map(str::to_string).or_else(|| record["cache"].as_str().map(str::to_string)).unwrap_or_default();
                let (play_start, stall) = match (a.stream, playback(&r.arrivals)) {
                    (true, Some((s, g))) => (json!(s), json!(g)),
                    _ => (Value::Null, Value::Null),
                };
                let row = json!({
                    "round": round, "index": i, "variant": later, "request_id": id, "personality": personality, "text": text, "http": r.status,
                    "cache": cache, "status": record["status"], "head_s": r.head_s,
                    "first_audio_s": if a.stream { json!(r.first_byte_s) } else { json!(r.end_s) },
                    "end_s": r.end_s, "play_start_s": play_start, "stall_s": stall,
                    "audio_s": if a.stream { json!(audio_s) } else { file["audio_seconds"].clone() },
                    "server": record["times"], "gen_s": record["gen_seconds"], "rtf": record["rtf"],
                });
                println!("r{round} #{i:<3} v{v} {:>4} {:<6} first={:>6.3}s end={:>6.3}s stall={:>5.3}s audio={:>5.2}s {}", r.status, cache,
                         row["first_audio_s"].as_f64().unwrap_or(f64::NAN), r.end_s, row["stall_s"].as_f64().unwrap_or(0.0),
                         row["audio_s"].as_f64().unwrap_or(0.0), text);
                rows.push(row);
            }
        }
    }
    let incomplete = rows.iter().filter(|r| r["status"] != "complete").count();
    let groups: Vec<Value> = variants.iter().map(|later| {
        let mine: Vec<Value> = rows.iter().filter(|r| r["variant"] == json!(later)).cloned().collect();
        let miss: Vec<Value> = mine.iter().filter(|r| r["cache"] == "miss").cloned().collect();
        let hit: Vec<Value> = mine.iter().filter(|r| r["cache"] == "hit" || r["cache"] == "joined").cloned().collect();
        let g = json!({
            "stream_later_frames": later,
            "miss": {"first_audio_s": summary(&miss, "first_audio_s"), "play_start_s": summary(&miss, "play_start_s"),
                     "stall_s": summary(&miss, "stall_s"), "end_s": summary(&miss, "end_s"), "rtf": summary(&miss, "rtf"),
                     "stalled_requests": miss.iter().filter(|r| r["stall_s"].as_f64().unwrap_or(0.0) > 0.0).count()},
            "hit": {"first_audio_s": summary(&hit, "first_audio_s"), "end_s": summary(&hit, "end_s")},
        });
        println!("later={} miss first_audio {} rtf {}", later.map(|n| n.to_string()).unwrap_or_else(|| "server".into()), g["miss"]["first_audio_s"], g["miss"]["rtf"]);
        println!("         play_start {} stall {} stalled={}", g["miss"]["play_start_s"], g["miss"]["stall_s"], g["miss"]["stalled_requests"]);
        println!("         hit first_audio {}", g["hit"]["first_audio_s"]);
        g
    }).collect();
    let report = json!({
        "endpoint": a.endpoint, "mode": if a.stream { "stream" } else { "file" }, "rounds": a.rounds, "fresh": a.fresh, "lines": lines.len(),
        "server": health, "incomplete": incomplete, "groups": groups, "rows": rows,
    });
    println!("not complete: {incomplete}");
    if let Some(out) = &a.json {
        std::fs::write(out, serde_json::to_string_pretty(&report)?)?;
        println!("wrote {}", out.display());
    }
    Ok(())
}

/// The server's record of a request; a stream's body ends before its cache file is written, so wait for that.
fn server_record(endpoint: &str, id: &str) -> Value {
    let started = Instant::now();
    loop {
        let record = exchange(endpoint, "GET", &format!("/requests?id={id}"), None).ok()
            .and_then(|x| serde_json::from_slice::<Value>(&x.body).ok()).map(|v| v["request"].clone()).unwrap_or(Value::Null);
        if record["status"] != "running" || started.elapsed() > Duration::from_secs(10) {
            return record;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}
