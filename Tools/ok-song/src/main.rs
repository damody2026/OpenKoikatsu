//! ok-song: a heroine sings. Song jobs for OpenKoikatsu: a short song from lyrics + style (generate), split into vocal
//! and accompaniment (separate), the vocal sung again in her own voice (convert, from the voice ok-tts gives her), then
//! mixed in Rust with a mouth envelope from her vocal alone, cached and served to the game.
//!
//!   ok-song doctor   [--config ok-song.json]                     what each stage runs, weights, environments, GPU
//!   ok-song generate --request REQ.json [--config ...]           one song without the service; prints its manifest
//!   ok-song serve    [--port 47880] [--parent-pid N] [--config ...]
//!   ok-song status   --job ID [--port 47880]
//!   ok-song cancel   --job ID [--port 47880]
//!   ok-song mix      --vocal V.wav --accompaniment A.wav --out DIR   the Rust mix + envelope alone (checks, tests)
//!
//! The model stages are prototypes running upstream Python entry points (backend.rs); they sit behind one boundary so
//! native Rust ports replace them. `doctor` and /health report native_rust_inference=false until then.

mod audio;
mod backend;
mod config;
mod job;
mod log;
mod parent;
mod server;

use std::collections::HashMap;
use std::io::{Read, Write};
use std::path::PathBuf;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;

use anyhow::{bail, Context, Result};

struct Args {
    cmd: String,
    opts: HashMap<String, String>,
}

fn parse_args() -> Args {
    let mut it = std::env::args().skip(1);
    let cmd = it.next().unwrap_or_else(|| "help".into());
    let rest: Vec<String> = it.collect();
    let mut opts = HashMap::new();
    let mut i = 0;
    while i < rest.len() {
        if let Some(name) = rest[i].strip_prefix("--") {
            let v = rest.get(i + 1).filter(|v| !v.starts_with("--")).cloned();
            i += if v.is_some() { 2 } else { 1 };
            opts.insert(name.to_string(), v.unwrap_or_default());
        } else {
            i += 1;
        }
    }
    Args { cmd, opts }
}

struct Print;

impl job::Progress for Print {
    fn stage(&self, stage: &str) {
        crate::log!("ok-song: {stage}");
    }
}

/// A tiny HTTP/1.1 exchange with the local service (status / cancel).
fn http(port: u16, method: &str, path: &str) -> Result<String> {
    let mut s = std::net::TcpStream::connect(("127.0.0.1", port)).with_context(|| format!("connect 127.0.0.1:{port}"))?;
    write!(s, "{method} {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n")?;
    let mut out = String::new();
    s.read_to_string(&mut out)?;
    Ok(out.split_once("\r\n\r\n").map(|(_, b)| b.to_string()).unwrap_or(out))
}

fn run() -> Result<()> {
    let args = parse_args();
    let port: u16 = args.opts.get("port").map(|p| p.parse()).transpose()?.unwrap_or(47880);
    match args.cmd.as_str() {
        "doctor" => {
            let (cfg, path) = config::Config::find(args.opts.get("config").map(String::as_str))?;
            let mut report = backend::doctor(&cfg);
            report["config"] = serde_json::json!(path);
            println!("{}", serde_json::to_string_pretty(&report)?);
        }
        "generate" => {
            let (cfg, _) = config::Config::find(args.opts.get("config").map(String::as_str))?;
            let path = PathBuf::from(args.opts.get("request").context("--request REQ.json is required")?);
            let mut req: job::SongRequest = serde_json::from_str(std::fs::read_to_string(&path)?.trim_start_matches('\u{feff}'))?;
            req.validate()?;
            let voice = job::resolve_voice(&cfg, &req)?;
            let keys = job::keys(&cfg, &req, &voice);
            let job_dir = cfg.jobs_dir().join(format!("cli-{}", std::process::id()));
            std::fs::create_dir_all(&job_dir)?;
            let out = job::run(&cfg, &req, &voice, &keys, &job_dir, Arc::new(AtomicBool::new(false)), &Print);
            let _ = std::fs::remove_dir_all(&job_dir);
            let out = out?;
            println!("{}", serde_json::to_string_pretty(&serde_json::json!({"cache_hit": out.cache_hit, "dir": server::artifact_dir(&cfg, &keys), "manifest": out.manifest}))?);
        }
        "serve" => {
            log::init("ok-song");
            if let Some(pid) = args.opts.get("parent-pid").and_then(|p| p.parse().ok()) {
                parent::watch(pid);
            }
            let (cfg, path) = config::Config::find(args.opts.get("config").map(String::as_str))?;
            crate::log!("ok-song: config {}", path.display());
            server::serve(cfg, port)?;
        }
        "status" => println!("{}", http(port, "GET", &format!("/jobs/{}", args.opts.get("job").context("--job")?))?),
        "cancel" => println!("{}", http(port, "POST", &format!("/jobs/{}/cancel", args.opts.get("job").context("--job")?))?),
        "mix" => {
            let v = audio::Audio::read_wav(&PathBuf::from(args.opts.get("vocal").context("--vocal")?))?;
            let a = audio::Audio::read_wav(&PathBuf::from(args.opts.get("accompaniment").context("--accompaniment")?))?;
            let out = PathBuf::from(args.opts.get("out").context("--out DIR")?);
            std::fs::create_dir_all(&out)?;
            let t = std::time::Instant::now();
            let (mixed, report) = audio::mix(&v, &a, -16.0, -1.0)?;
            audio::write_atomic(&out.join("song.ogg"), &audio::encode_ogg(&mixed)?)?;
            let env = audio::mouth_envelope(&v);
            audio::write_atomic(&out.join("mouth.json"), serde_json::to_string(&env)?.as_bytes())?;
            println!("{}", serde_json::to_string_pretty(&serde_json::json!({"mix": report, "seconds": mixed.seconds(), "took": t.elapsed().as_secs_f64(),
                                                                           "envelope_values": env.values.len()}))?);
        }
        _ => bail!("usage: ok-song doctor|generate|serve|status|cancel|mix (see the source header)"),
    }
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        crate::log!("ok-song: error: {e:#}");
        std::process::exit(1);
    }
}
