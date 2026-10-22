//! The generate stage in native Rust: HeartMuLa tokens, then HeartCodec audio (the language model is dropped before
//! the codec loads, so both never sit in VRAM together).

use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

use anyhow::Result;
use serde_json::{json, Value};

use super::codec::{Codec, SeededNoise, SAMPLE_RATE};
use super::mula::{Mula, Sampling, FRAME_MS};
use crate::audio::Audio;
use crate::backend::stage_error;

/// Bumped when a native stage changes its output for the same inputs (part of the cache keys).
pub const GENERATE_REVISION: &str = "heartmula-rust-1";
pub const SEPARATE_REVISION: &str = "demucs-rust-1";

pub struct GenerateOptions {
    pub cfg_scale: f32,
    pub top_k: i32,
    pub temperature: f32,
    pub seed: u64,
}

/// Demucs htdemucs_ft: (vocals, accompaniment = drums + bass + other) at 44.1 kHz, like the Python prototype.
pub fn separate(models: &Path, song: &Audio, cancel: &AtomicBool) -> Result<(Audio, Audio, Value)> {
    use super::demucs::{Separator, RATE};
    let gpu = super::device(0).map_err(|e| stage_error("BACKEND_FAILED", format!("CUDA: {e:#}")))?;
    let t = std::time::Instant::now();
    let sep = Separator::load(&gpu, models)?;
    let load = t.elapsed().as_secs_f64();
    let s = song.resampled(RATE);
    let mix: [Vec<f32>; 2] = match s.channels.len() {
        1 => [s.channels[0].clone(), s.channels[0].clone()],
        _ => [s.channels[0].clone(), s.channels[1].clone()],
    };
    let t2 = std::time::Instant::now();
    let src = sep.separate(&gpu, &mix, &|| cancel.load(Ordering::SeqCst))
        .map_err(|e| if cancel.load(Ordering::SeqCst) { stage_error("CANCELLED", "cancelled") } else { e })?;
    let acc: Vec<Vec<f32>> = (0..2).map(|c| (0..mix[0].len()).map(|i| src[0][c][i] + src[1][c][i] + src[2][c][i]).collect()).collect();
    let [vl, vr] = src.into_iter().nth(3).unwrap();
    let report = json!({"revision": SEPARATE_REVISION, "load_seconds": load, "separate_seconds": t2.elapsed().as_secs_f64()});
    Ok((Audio { rate: RATE, channels: vec![vl, vr] }, Audio { rate: RATE, channels: acc }, report))
}

pub fn generate(ckpt: &Path, lyrics: &str, tags: &str, max_ms: u32, o: &GenerateOptions, cancel: &AtomicBool) -> Result<(Audio, Value)> {
    let gpu = super::device(0).map_err(|e| stage_error("BACKEND_FAILED", format!("CUDA: {e:#}")))?;
    let max_frames = (max_ms as usize / FRAME_MS).max(1);
    let t = std::time::Instant::now();
    let frames = {
        let m = Mula::load(&gpu, ckpt, o.cfg_scale != 1.0, max_frames, 1024)?;
        let load = t.elapsed().as_secs_f64();
        let p = m.prompt(tags, lyrics)?;
        if p.len() > 1024 {
            return Err(stage_error("BAD_REQUEST", format!("lyrics and tags are {} tokens (limit 1024)", p.len())));
        }
        let s = Sampling { cfg: o.cfg_scale, temperature: o.temperature, top_k: o.top_k, seed: o.seed };
        let t2 = std::time::Instant::now();
        let frames = m.generate(&gpu, &p, max_frames, &s, &|| cancel.load(Ordering::SeqCst), |_| {})
            .map_err(|e| if cancel.load(Ordering::SeqCst) { stage_error("CANCELLED", "cancelled") } else { e })?;
        (frames, load, t2.elapsed().as_secs_f64(), p.len())
    };
    gpu.trim_pool()?;
    let (frames, mula_load, mula_seconds, prompt_tokens) = frames;
    if frames.is_empty() {
        return Err(stage_error("INVALID_AUDIO", "the language model ended the song at once"));
    }
    if cancel.load(Ordering::SeqCst) {
        return Err(stage_error("CANCELLED", "cancelled"));
    }
    let t3 = std::time::Instant::now();
    let codec = Codec::load(&gpu, &ckpt.join("HeartCodec-oss"))?;
    let codec_load = t3.elapsed().as_secs_f64();
    let t4 = std::time::Instant::now();
    let [l, r] = codec.detokenize(&gpu, &frames, &mut SeededNoise(o.seed ^ 0x9e37_79b9_7f4a_7c15), None, |_, _| {})?;
    let report = json!({
        "revision": GENERATE_REVISION, "frames": frames.len(), "prompt_tokens": prompt_tokens,
        "mula_load_seconds": mula_load, "mula_seconds": mula_seconds, "codec_load_seconds": codec_load, "codec_seconds": t4.elapsed().as_secs_f64(),
        "seed": o.seed, "cfg_scale": o.cfg_scale, "top_k": o.top_k, "temperature": o.temperature,
    });
    Ok((Audio { rate: SAMPLE_RATE as u32, channels: vec![l, r] }, report))
}
