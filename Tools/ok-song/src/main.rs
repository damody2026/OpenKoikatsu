//! ok-song: a heroine sings. Song jobs for OpenKoikatsu: a short song from lyrics + style (generate), split into vocal
//! and accompaniment (separate), the vocal sung again in her own voice (convert, from the voice ok-tts gives her), then
//! mixed in Rust with a mouth envelope from her vocal alone, cached and served to the game.
//!
//!   ok-song doctor   [--config ok-song.json]                     what each stage runs, weights, environments, GPU
//!   ok-song generate --request REQ.json [--config ...]           one song without the service; prints its manifest
//!   ok-song serve    [--port 47880] [--parent-pid N] [--config ...]
//!   ok-song status   --job ID [--port 47880]
//!   ok-song cancel   --job ID [--port 47880]
//!   ok-song mula-check --ref DIR [--ckpt E:/HeartMuLa/ckpt] [--frames 12]   native HeartMuLa vs a Python dump
//!   ok-song mula-generate --tags T --lyrics FILE --out FRAMES.json [--seconds 30] [--cfg 3] [--topk 50] [--seed N]
//!   ok-song codec-check --frames F.json --ref DIR [--first-draw 33]   native HeartCodec vs a Python dump
//!   ok-song demucs-check --ref DIR [--models DIR]   native Demucs (htdemucs_ft) vs a Python dump
//!   ok-song seedvc-check --ref DIR [--models DIR] [--parts mel,voc,lr,est,cfm,sem,style,f0,pipe,conv]   native Seed-VC vs a dump
//!   ok-song prepare-models --seedvc SEEDVC_CHECKPOINTS --out DIR   torch checkpoints -> safetensors (no Python)
//!   ok-song mix      --vocal V.wav --accompaniment A.wav --out DIR   the Rust mix + envelope alone (checks, tests)
//!
//! The three model stages run natively in Rust + CUDA (src/native: HeartMuLa + HeartCodec, Demucs htdemucs_ft, Seed-VC)
//! when ok-song.json selects the "-rust" backends; the "-python" prototypes behind the same boundary remain for
//! comparison. `doctor` and /health report native_rust_inference. The *-check commands compare against Python dumps.

mod audio;
mod backend;
mod config;
mod job;
mod log;
mod native;
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

/// Native stages load CUDA DLLs (cuBLAS, NVRTC) at run time: look beside the exe and in <exe>/cuda first.
fn add_dll_dirs() {
    let Some(exe) = std::env::current_exe().ok().and_then(|p| p.parent().map(PathBuf::from)) else { return };
    let mut path = std::env::var_os("PATH").unwrap_or_default();
    for d in [exe.join("cuda"), exe] {
        if d.is_dir() {
            let mut p = d.into_os_string();
            p.push(";");
            p.push(&path);
            path = p;
        }
    }
    std::env::set_var("PATH", path);
}

fn run() -> Result<()> {
    add_dll_dirs();
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
        "mula-check" => {
            let gpu = native::device(0)?;
            let ckpt = PathBuf::from(args.opts.get("ckpt").map(String::as_str).unwrap_or("E:/HeartMuLa/ckpt"));
            let t = std::time::Instant::now();
            let m = native::mula::Mula::load(&gpu, &ckpt, true, 64, 256)?;
            crate::log!("ok-song: HeartMuLa loaded in {:.1} s", t.elapsed().as_secs_f64());
            let frames = args.opts.get("frames").map(|v| v.parse()).transpose()?.unwrap_or(12);
            let r = m.check(&gpu, &PathBuf::from(args.opts.get("ref").context("--ref")?), frames)?;
            println!("{}", serde_json::to_string_pretty(&r)?);
        }
        "mula-generate" => {
            let gpu = native::device(0)?;
            let ckpt = PathBuf::from(args.opts.get("ckpt").map(String::as_str).unwrap_or("E:/HeartMuLa/ckpt"));
            let opt = |k: &str, d: f64| -> Result<f64> { Ok(args.opts.get(k).map(|v| v.parse()).transpose()?.unwrap_or(d)) };
            let seconds = opt("seconds", 30.0)?;
            let max_frames = (seconds * 1000.0 / native::mula::FRAME_MS as f64) as usize;
            let lyrics = std::fs::read_to_string(args.opts.get("lyrics").context("--lyrics FILE")?)?;
            let t = std::time::Instant::now();
            let m = native::mula::Mula::load(&gpu, &ckpt, true, max_frames, 1024)?;
            let load = t.elapsed().as_secs_f64();
            let p = m.prompt(args.opts.get("tags").context("--tags")?, &lyrics)?;
            let s = native::mula::Sampling { cfg: opt("cfg", 3.0)? as f32, temperature: opt("temperature", 1.0)? as f32,
                top_k: opt("topk", 50.0)? as i32, seed: opt("seed", 1.0)? as u64 };
            let t = std::time::Instant::now();
            let frames = m.generate(&gpu, &p, max_frames, &s, &|| false, |_| {})?;
            let gen = t.elapsed().as_secs_f64();
            let out = PathBuf::from(args.opts.get("out").context("--out")?);
            std::fs::write(&out, serde_json::to_vec(&serde_json::json!({"frames": frames}))?)?;
            println!("{}", serde_json::json!({"frames": frames.len(), "audio_seconds": frames.len() as f64 * 0.08, "load_seconds": load,
                "generate_seconds": gen, "ms_per_frame": gen * 1000.0 / frames.len().max(1) as f64, "prompt_tokens": p.len()}));
        }
        "codec-check" => {
            let gpu = native::device(0)?;
            let ckpt = PathBuf::from(args.opts.get("ckpt").map(String::as_str).unwrap_or("E:/HeartMuLa/ckpt"));
            let t = std::time::Instant::now();
            let codec = native::codec::Codec::load(&gpu, &ckpt.join("HeartCodec-oss"))?;
            crate::log!("ok-song: HeartCodec loaded in {:.1} s", t.elapsed().as_secs_f64());
            let v: serde_json::Value = serde_json::from_slice(&std::fs::read(args.opts.get("frames").context("--frames")?)?)?;
            let frames: Vec<[i32; 8]> = serde_json::from_value(v["frames"].clone())?;
            let first = args.opts.get("first-draw").map(|v| v.parse()).transpose()?.unwrap_or(33);
            let t = std::time::Instant::now();
            let mut r = native::codec::check(&gpu, &codec, &frames, &PathBuf::from(args.opts.get("ref").context("--ref")?), first)?;
            r["seconds"] = t.elapsed().as_secs_f64().into();
            println!("{}", serde_json::to_string_pretty(&r)?);
        }
        "demucs-check" => {
            let gpu = native::device(0)?;
            let models = PathBuf::from(args.opts.get("models").map(String::as_str)
                .unwrap_or("E:/OkSong/hf-cache/hub/models--adefossez--HTDemucs-ft/snapshots/d74ac89c3a1e874fc78f152555cf4d8533f06cd4"));
            let r = native::demucs::check(&gpu, &models, &PathBuf::from(args.opts.get("ref").context("--ref")?))?;
            println!("{}", serde_json::to_string_pretty(&r)?);
        }
        "seedvc-check" => {
            let gpu = native::device(0)?;
            let models = PathBuf::from(args.opts.get("models").map(String::as_str).unwrap_or("E:/OkSong/models-st"));
            let parts = args.opts.get("parts").map(String::as_str).unwrap_or("mel,voc");
            let r = native::seedvc::check(&gpu, &models, &PathBuf::from(args.opts.get("ref").context("--ref")?), parts)?;
            println!("{}", serde_json::to_string_pretty(&r)?);
        }
        "prepare-models" => {
            let r = native::seedvc::prepare(&PathBuf::from(args.opts.get("seedvc").context("--seedvc <Seed-VC checkpoints dir>")?),
                                            &PathBuf::from(args.opts.get("out").context("--out DIR")?))?;
            println!("{}", serde_json::to_string_pretty(&r)?);
        }
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
