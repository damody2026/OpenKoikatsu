//! ok-asr: Qwen3-ASR-0.6B speech recognition for OpenKoikatsu, in Rust on CUDA (cudarc + cuBLAS + NVRTC).
//!
//!   ok-asr serve       [--port 47861] [--model DIR] [--device NAME | --settings asr-settings.json] [--parent-pid N]   (exits when process N exits)
//!   ok-asr transcribe  --wav FILE [--language Chinese] [--model DIR]
//!   ok-asr record      [--seconds 4] [--language Chinese] [--model DIR]   (microphone test)
//!   ok-asr vad         --wav FILE                                           (hands-free cuts + transcripts)
//!   ok-asr devices                                                         (list microphones)
//!
//! --device NAME (or OK_ASR_DEVICE) picks the first input device whose name contains NAME;
//! otherwise the Windows default recording device is used.
//!
//! CUDA DLLs (cublas64_12, cublasLt64_12, nvrtc64_120_0, nvrtc-builtins) are searched next to the exe,
//! in <exe>/cuda, in --cuda-dir, then on PATH. Only the NVIDIA driver must be installed.

mod log;
mod parent;
mod audio;
mod decoder;
mod encoder;
mod engine;
mod gpu;
mod listen;
mod mel;
mod server;
mod text;
mod weights;

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

use crate::engine::{default_model_dir, Engine};

struct Args {
    cmd: String,
    opts: HashMap<String, String>,
}

fn parse_args() -> Args {
    let mut it = std::env::args().skip(1);
    let cmd = it.next().unwrap_or_else(|| "help".into());
    let mut opts = HashMap::new();
    let rest: Vec<String> = it.collect();
    let mut i = 0;
    while i < rest.len() {
        if let Some(name) = rest[i].strip_prefix("--") {
            let value = rest.get(i + 1).filter(|v| !v.starts_with("--")).cloned();
            i += if value.is_some() { 2 } else { 1 };
            opts.insert(name.to_string(), value.unwrap_or_default());
            continue;
        }
        i += 1;
    }
    Args { cmd, opts }
}

fn exe_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_else(|| PathBuf::from("."))
}

/// Make the CUDA runtime DLLs findable before cudarc loads them.
fn add_dll_dirs(extra: Option<&String>) {
    let mut dirs: Vec<PathBuf> = vec![exe_dir(), exe_dir().join("cuda")];
    if let Some(e) = extra {
        dirs.insert(0, PathBuf::from(e));
    }
    let mut path = std::env::var_os("PATH").unwrap_or_default();
    for d in dirs.iter().rev() {
        if d.is_dir() {
            let mut p = d.as_os_str().to_owned();
            p.push(";");
            p.push(&path);
            path = p;
        }
    }
    std::env::set_var("PATH", path);
}

fn model_dir(args: &Args) -> Result<PathBuf> {
    match args.opts.get("model") {
        Some(m) => Ok(PathBuf::from(m)),
        None => default_model_dir().context("model not found; pass --model <Qwen3-ASR-0.6B dir> or set OK_ASR_MODEL"),
    }
}

/// `--settings FILE`: the game's asr-settings.json ({"device": ...}), read here as UTF-8 so a Chinese device name
/// never has to survive a command line.
fn settings_device(args: &Args) -> Option<String> {
    let text = std::fs::read_to_string(args.opts.get("settings")?).ok()?;
    let json: serde_json::Value = serde_json::from_str(text.trim_start_matches('\u{feff}')).ok()?;
    json.get("device")?.as_str().map(str::to_owned)
}

fn device(args: &Args) -> Option<String> {
    args.opts.get("device").cloned().or_else(|| settings_device(args)).or_else(|| std::env::var("OK_ASR_DEVICE").ok()).filter(|d| !d.trim().is_empty())
}

fn max_positions(args: &Args) -> usize {
    args.opts.get("max-positions").and_then(|v| v.parse().ok()).unwrap_or(1024)
}

fn main() {
    let args = parse_args();
    if args.cmd == "serve" {
        // The server writes its own log (logs/ok-asr.log beside the exe), whoever starts it.
        log::init("ok-asr");
    }
    if args.cmd == "serve" {
        if let Some(pid) = args.opts.get("parent-pid").and_then(|p| p.parse().ok()) {
            parent::watch(pid);
        }
    }
    add_dll_dirs(args.opts.get("cuda-dir"));
    let result = match args.cmd.as_str() {
        "serve" => cmd_serve(&args),
        "transcribe" => cmd_transcribe(&args),
        "record" => cmd_record(&args),
        "vad" => cmd_vad(&args),
        "devices" => {
            for (i, d) in audio::input_devices().iter().enumerate() {
                println!("{}{d}", if i == 0 { "* " } else { "  " });
            }
            Ok(())
        }
        _ => {
            crate::log!("usage: ok-asr serve|transcribe|record [options]  (see source header)");
            Ok(())
        }
    };
    if let Err(e) = result {
        crate::log!("ok-asr: error: {e:#}");
        std::process::exit(1);
    }
}

fn cmd_serve(args: &Args) -> Result<()> {
    let model = model_dir(args)?;
    let port: u16 = args.opts.get("port").map(|p| p.parse()).transpose()?.unwrap_or(47861);
    let positions = max_positions(args);
    server::serve(port, device(args), move || Engine::load(&model, positions))
}

fn print(t: &engine::Transcript) {
    println!("{}", serde_json::json!({"text": t.text, "language": t.language, "raw": t.raw,
        "audio_seconds": t.audio_seconds, "asr_seconds": t.asr_seconds}));
}

fn cmd_transcribe(args: &Args) -> Result<()> {
    let engine = Engine::load(&model_dir(args)?, max_positions(args))?;
    let (samples, rate) = audio::read_wav(Path::new(args.opts.get("wav").context("--wav is required")?))?;
    let language = args.opts.get("language").map(String::as_str);
    let audio = audio::to_model_rate(&samples, rate);
    let runs: usize = args.opts.get("runs").and_then(|r| r.parse().ok()).unwrap_or(1);
    for _ in 0..runs {
        print(&engine.transcribe(&audio, language)?);
    }
    Ok(())
}

fn cmd_record(args: &Args) -> Result<()> {
    let engine = Engine::load(&model_dir(args)?, max_positions(args))?;
    let seconds: f32 = args.opts.get("seconds").and_then(|s| s.parse().ok()).unwrap_or(4.0);
    let recorder = audio::Recorder::new(std::sync::Arc::new(std::sync::Mutex::new(device(args))));
    crate::log!("ok-asr: recording {seconds}s from {} ...", recorder.start()?);
    std::thread::sleep(std::time::Duration::from_secs_f32(seconds));
    let r = recorder.stop()?;
    crate::log!("ok-asr: {} samples at {} Hz, rms {:.4}", r.samples.len(), r.rate, audio::rms(&r.samples));
    let audio = audio::to_model_rate(&r.samples, r.rate);
    print(&engine.transcribe(&audio, args.opts.get("language").map(String::as_str))?);
    Ok(())
}

fn cmd_vad(args: &Args) -> Result<()> {
    let engine = Engine::load(&model_dir(args)?, max_positions(args))?;
    let (samples, rate) = audio::read_wav(Path::new(args.opts.get("wav").context("--wav is required")?))?;
    for (i, u) in listen::split_utterances(&samples, rate).iter().enumerate() {
        let audio = audio::to_model_rate(u, rate);
        let t = engine.transcribe(&audio, None)?;
        println!("utterance {} {:.2}s: {}", i + 1, u.len() as f64 / rate as f64, t.text);
    }
    Ok(())
}
