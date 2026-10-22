//! `ok-tts consistency`: does a fixed speaker embedding (Base, x-vector clone) keep one voice across lines better than
//! the voice description (VoiceDesign)? Auxiliary evidence only — the plan's acceptance is blind listening.
//!
//!   ok-tts consistency --base DIR --corpus lines.txt --out DIR [--personality 08] [--age 18] [--other 00,03,m0]
//!                      [--ref-text TEXT] [--model VOICEDESIGN_DIR] [--voices voices.json]
//!
//! 1. VoiceDesign speaks a reference line and every corpus line in the chosen voice (and three lines in each --other
//!    voice, to see what "another person" scores).
//! 2. Base clones the reference line's speaker embedding and speaks the same corpus lines.
//! 3. Every clip is embedded with the Base speaker encoder; reported: cosine of each line to the reference, both sets,
//!    raw and after subtracting the mean embedding of all clips (the raw vectors share a large common part).
//! WAVs and result.json go to --out (local only: generated voices are not versioned).

use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use serde_json::json;

use crate::audio::{peak_normalize, trim_silence, wav_bytes, write_atomic};
use crate::engine::{Engine, SAMPLE_RATE};
use crate::speaker::{cosine, SpeakerEncoder};
use crate::talker::GenConfig;
use crate::text::clean_line;
use crate::weights::Store;

pub struct ConsistencyArgs {
    pub design: PathBuf,
    pub base: PathBuf,
    pub voices: PathBuf,
    pub corpus: PathBuf,
    pub out: PathBuf,
    pub personality: String,
    pub other: String,
    pub age: u32,
    pub ref_text: String,
    pub positions: usize,
}

fn finish(mut samples: Vec<f32>, path: &Path) -> Result<Vec<f32>> {
    trim_silence(&mut samples, SAMPLE_RATE);
    peak_normalize(&mut samples);
    write_atomic(path, &wav_bytes(&samples, SAMPLE_RATE))?;
    Ok(samples)
}

fn stats(v: &[f64]) -> serde_json::Value {
    let n = v.len().max(1) as f64;
    let mean = v.iter().sum::<f64>() / n;
    let sd = (v.iter().map(|x| (x - mean).powi(2)).sum::<f64>() / n).sqrt();
    let min = v.iter().cloned().fold(f64::INFINITY, f64::min);
    json!({"n": v.len(), "mean": mean, "sd": sd, "min": min})
}

pub fn run(a: &ConsistencyArgs) -> Result<()> {
    let lines: Vec<String> = std::fs::read_to_string(&a.corpus).with_context(|| format!("read {}", a.corpus.display()))?
        .lines().map(str::trim).filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(|l| clean_line(l.rsplit('\t').next().unwrap_or(l))).filter(|l| !l.is_empty()).collect();
    if lines.is_empty() {
        bail!("the corpus has no lines");
    }
    std::fs::create_dir_all(&a.out)?;
    let ref_text = clean_line(&a.ref_text);

    // 1. VoiceDesign
    let design = Engine::load(&a.design, &a.voices, a.positions)?;
    let reference = design.speak(&ref_text, &a.personality, a.age, GenConfig::default())?;
    let reference = finish(reference.samples, &a.out.join("ref.wav"))?;
    let mut design_clips = Vec::new();
    for (i, line) in lines.iter().enumerate() {
        let s = design.speak(line, &a.personality, a.age, GenConfig::default())?;
        design_clips.push(finish(s.samples, &a.out.join(format!("design_{i:02}.wav")))?);
    }
    let mut others: Vec<(String, Vec<Vec<f32>>)> = Vec::new();
    for voice in a.other.split(',').map(str::trim).filter(|v| !v.is_empty()) {
        let mut clips = Vec::new();
        for (k, line) in lines.iter().rev().take(3).enumerate() {
            let s = design.speak(line, voice, a.age, GenConfig::default())?;
            clips.push(finish(s.samples, &a.out.join(format!("other_{voice}_{k}.wav")))?);
        }
        others.push((voice.to_string(), clips));
    }
    let seed = design.voices.choose(&a.personality, a.age).seed;
    drop(design);

    // 2. Base, cloning the reference clip
    let store = Store::open(&a.base.join("model.safetensors"))?;
    let encoder = SpeakerEncoder::load(&store).context("speaker encoder")?;
    drop(store);
    let ref_embed = encoder.embed(&reference)?;
    let base = Engine::load(&a.base, &a.voices, a.positions)?;
    let mut base_clips = Vec::new();
    let mut base_rtf = Vec::new();
    for (i, line) in lines.iter().enumerate() {
        let s = base.speak_clone(line, &ref_embed, seed, GenConfig::default())?;
        base_rtf.push(s.gen_seconds / (s.samples.len().max(1) as f64 / SAMPLE_RATE as f64));
        base_clips.push(finish(s.samples, &a.out.join(format!("base_{i:02}.wav")))?);
    }
    drop(base);

    // 3. Speaker similarity, raw and centred on the mean of every clip's embedding.
    let embed_all = |clips: &[Vec<f32>]| -> Result<Vec<Vec<f32>>> { clips.iter().map(|c| encoder.embed(c)).collect() };
    let design_e = embed_all(&design_clips)?;
    let base_e = embed_all(&base_clips)?;
    let other_e: Vec<(String, Vec<Vec<f32>>)> = others.iter().map(|(v, c)| Ok((v.clone(), embed_all(c)?))).collect::<Result<_>>()?;
    let mut all: Vec<&Vec<f32>> = vec![&ref_embed];
    all.extend(design_e.iter());
    all.extend(base_e.iter());
    other_e.iter().for_each(|(_, e)| all.extend(e.iter()));
    let dim = ref_embed.len();
    let mean: Vec<f32> = (0..dim).map(|d| all.iter().map(|e| e[d]).sum::<f32>() / all.len() as f32).collect();
    let centred = |e: &[f32]| -> Vec<f32> { e.iter().zip(&mean).map(|(a, b)| a - b).collect() };
    let ref_c = centred(&ref_embed);
    let to_ref = |set: &[Vec<f32>]| -> (Vec<f64>, Vec<f64>) {
        (set.iter().map(|e| cosine(e, &ref_embed)).collect(), set.iter().map(|e| cosine(&centred(e), &ref_c)).collect())
    };
    let pairwise = |set: &[Vec<f32>]| -> Vec<f64> {
        let c: Vec<Vec<f32>> = set.iter().map(|e| centred(e)).collect();
        let mut v = Vec::new();
        for i in 0..c.len() {
            for j in i + 1..c.len() {
                v.push(cosine(&c[i], &c[j]));
            }
        }
        v
    };
    let (design_raw, design_cen) = to_ref(&design_e);
    let (base_raw, base_cen) = to_ref(&base_e);
    let other_scores: Vec<serde_json::Value> = other_e.iter().map(|(v, e)| {
        let (raw, cen) = to_ref(e);
        json!({"voice": v, "raw": stats(&raw), "centred": stats(&cen)})
    }).collect();
    let summary = json!({
        "design_to_reference": {"raw": stats(&design_raw), "centred": stats(&design_cen)},
        "base_to_reference": {"raw": stats(&base_raw), "centred": stats(&base_cen)},
        "other_voices_to_reference": other_scores,
        "design_pairwise_centred": stats(&pairwise(&design_e)), "base_pairwise_centred": stats(&pairwise(&base_e)),
        "base_rtf": stats(&base_rtf),
    });
    let mut result = summary.clone();
    result["personality"] = json!(a.personality);
    result["age"] = json!(a.age);
    result["reference_text"] = json!(ref_text);
    result["lines"] = json!(lines);
    result["design_centred"] = json!(design_cen);
    result["base_centred"] = json!(base_cen);
    std::fs::write(a.out.join("result.json"), serde_json::to_string_pretty(&result)?)?;
    println!("{}", serde_json::to_string_pretty(&summary)?);
    println!("wrote {}", a.out.display());
    Ok(())
}

fn read_wav(path: &Path) -> Result<Vec<f32>> {
    let b = std::fs::read(path).with_context(|| format!("read {}", path.display()))?;
    let data = b.windows(4).position(|w| w == b"data").context("no data chunk")? + 8;
    Ok(b[data..].chunks_exact(2).map(|c| i16::from_le_bytes([c[0], c[1]]) as f32 / 32768.0).collect())
}

/// `ok-tts speaker-sim --base DIR a.wav b.wav ...` (16-bit mono 24 kHz WAVs as `consistency` writes them): the
/// speaker-embedding cosine of every pair, raw and centred on the mean of the given clips. A clip named `x.wav#1`
/// / `x.wav#2` stands for its first / second half (the same voice, different words: a self-check of the encoder).
pub fn similarity(base: &Path, clips: &[String]) -> Result<()> {
    let store = Store::open(&base.join("model.safetensors"))?;
    let encoder = SpeakerEncoder::load(&store)?;
    let mut embeds = Vec::new();
    for c in clips {
        let (file, half) = match c.rsplit_once('#') {
            Some((f, h)) => (f, h.parse::<usize>().ok()),
            None => (c.as_str(), None),
        };
        let s = read_wav(Path::new(file))?;
        let s = match half {
            Some(1) => s[..s.len() / 2].to_vec(),
            Some(2) => s[s.len() / 2..].to_vec(),
            _ => s,
        };
        embeds.push(encoder.embed(&s)?);
    }
    let dim = embeds[0].len();
    let mean: Vec<f32> = (0..dim).map(|d| embeds.iter().map(|e| e[d]).sum::<f32>() / embeds.len() as f32).collect();
    let centred: Vec<Vec<f32>> = embeds.iter().map(|e| e.iter().zip(&mean).map(|(a, b)| a - b).collect()).collect();
    for i in 0..clips.len() {
        for j in i + 1..clips.len() {
            let name = |k: usize| Path::new(&clips[k]).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            println!("{:>24} {:>24} raw={:.4} centred={:+.4}", name(i), name(j), cosine(&embeds[i], &embeds[j]), cosine(&centred[i], &centred[j]));
        }
    }
    Ok(())
}
