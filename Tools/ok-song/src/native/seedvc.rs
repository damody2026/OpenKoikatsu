//! Seed-VC singing voice conversion (F0 conditioned, 44.1 kHz) in Rust + CUDA. Mirrors Seed-VC `inference.py` with
//! `--f0-condition true`: Whisper-small content features, CAMPPlus speaker style, RMVPE F0, the length regulator, the
//! DiT flow-matching model (CFG) and the BigVGAN v2 vocoder.

use std::path::Path;

use anyhow::{Context, Result};
use serde_json::{json, Value};

use super::gpu::Gpu;
use super::mula::{compare, npy};

pub const RATE: u32 = 44_100;
pub const HOP: usize = 512;
pub const N_FFT: usize = 2048;
pub const N_MELS: usize = 128;

/// Log-mel [128][frames] (row major) of 44.1 kHz audio.
pub fn mel(x: &[f32]) -> (Vec<f32>, usize) {
    let m = super::dsp::log_mel_44k(x, N_FFT, HOP, N_MELS, RATE as f64);
    let frames = m[0].len();
    (m.into_iter().flatten().collect(), frames)
}

/// Parity of the parts ported so far against seedvc_dump.py output.
pub fn check(gpu: &Gpu, models: &Path, refdir: &Path, parts: &str) -> Result<Value> {
    let mut report = serde_json::Map::new();
    if parts.contains("mel") {
        let x = npy(&refdir.join("mel_in_2.npy"))?;
        let (m, frames) = mel(&x);
        report.insert("mel_frames".into(), frames.into());
        report.insert("mel".into(), compare(&m, &npy(&refdir.join("mel_out_2.npy"))?));
    }
    if parts.contains("voc") {
        let v = super::bigvgan::BigVgan::load(gpu, &models.join("bigvgan.safetensors"))?;
        let m = npy(&refdir.join("voc_in_2.npy"))?;
        let t = std::time::Instant::now();
        let y = v.forward(gpu, &m, m.len() / N_MELS)?;
        report.insert("voc_seconds".into(), t.elapsed().as_secs_f64().into());
        report.insert("voc".into(), compare(&y, &npy(&refdir.join("voc_out_2.npy"))?));
    }
    if parts.contains("pipe") {
        // the orchestration on the dump's own 44.1 kHz inputs, intermediate by intermediate
        let vc = SeedVc::load(gpu, models)?;
        let src = npy(&refdir.join("mel_in_1.npy"))?;
        let refw = npy(&refdir.join("mel_in_2.npy"))?;
        let src16 = crate::audio::resample(&src, RATE, 16000);
        let ref16 = crate::audio::resample(&refw, RATE, 16000);
        report.insert("src16".into(), compare(&src16[..src16.len().min(480000)], &npy(&refdir.join("sem_in_1.npy"))?[..src16.len().min(480000)]));
        report.insert("src16_len".into(), src16.len().into());
        let (s_alt, n_alt) = vc.semantic(gpu, &src16)?;
        let r = npy(&refdir.join("sem_out_1.npy"))?;
        report.insert("s_alt".into(), compare(&s_alt[..r.len().min(s_alt.len())], &r[..r.len().min(s_alt.len())]));
        report.insert("n_alt".into(), n_alt.into());
        let (_, frames) = mel(&src);
        let (mel2, frames2) = mel(&refw);
        report.insert("mel2".into(), compare(&mel2, &npy(&refdir.join("mel_out_2.npy"))?));
        let f0_alt = vc.rmvpe.f0(gpu, &src16, 0.03)?;
        let f0r = npy(&refdir.join("f0_out_2.npy"))?;
        let fa: Vec<f32> = f0_alt.iter().map(|v| *v as f32).collect();
        report.insert("f0_alt".into(), compare(&fa[..fa.len().min(f0r.len())], &f0r[..fa.len().min(f0r.len())]));
        let f0s: Vec<f64> = f0_alt.iter().map(|f| (f + 1e-5).ln().exp()).collect();
        let cond = vc.dit.regulate(gpu, &s_alt, n_alt, &f0s, frames, false)?;
        let lr = npy(&refdir.join("lr_out_1.npy"))?;
        report.insert("cond".into(), compare(&gpu.read::<f32>(cond.ptr, lr.len().min(frames * 768))?, &lr[..lr.len().min(frames * 768)]));
        let (fb, fbn) = super::campplus::fbank(&ref16);
        let style = vc.campplus.embed(gpu, &fb, fbn)?;
        report.insert("style".into(), compare(&style, &npy(&refdir.join("style_out.npy"))?));
        report.insert("frames".into(), json!([frames, frames2]));
    }
    if parts.contains("conv") {
        let vc = SeedVc::load(gpu, models)?;
        let src = npy(&refdir.join("mel_in_1.npy"))?;
        let refw = npy(&refdir.join("mel_in_2.npy"))?;
        let o = ConvertOptions { steps: 30, cfg_rate: 0.7, seed: 1, vibrato_cents: 0.0, glide_ms: 0.0, soft_f0: false, scoop_cents: 0.0, drift_cents: 0.0, jitter_cents: 0.0 };
        let tm = std::time::Instant::now();
        let y = vc.convert(gpu, &src, &refw, &o, &|| false)?;
        report.insert("conv_seconds".into(), tm.elapsed().as_secs_f64().into());
        report.insert("conv_samples".into(), y.len().into());
        crate::audio::Audio { rate: RATE, channels: vec![y] }.write_wav(&refdir.join("native_conv.wav"))?;
        // the first chunk with the dump's own noise, through the same code path as convert()
        let cond = npy(&refdir.join("cfm_mu_1.npy"))?;
        let t = cond.len() / 768;
        let prompt = npy(&refdir.join("cfm_prompt_1.npy"))?;
        let p = prompt.len() / 128;
        let c = gpu.upload(&cond)?;
        let m = vc.dit.infer(gpu, &c, t, &prompt, p, &npy(&refdir.join("cfm_style_1.npy"))?, &npy(&refdir.join("cfm_noise_1.npy"))?, 30, 0.7)?;
        let len = t - p;
        let mut part = vec![0f32; 128 * len];
        for ch in 0..128 {
            part[ch * len..(ch + 1) * len].copy_from_slice(&m[ch * t + p..(ch + 1) * t]);
        }
        report.insert("chunk1_mel".into(), compare(&part, &npy(&refdir.join("voc_in_1.npy"))?));
        let w = vc.vocoder.forward(gpu, &part, len)?;
        report.insert("chunk1_wave".into(), compare(&w, &npy(&refdir.join("voc_out_1.npy"))?));
    }
    if parts.contains("f0") {
        let r = super::rmvpe::Rmvpe::load(gpu, &models.join("rmvpe.safetensors"))?;
        let x = npy(&refdir.join("f0_in_1.npy"))?;
        let tm = std::time::Instant::now();
        let f = r.f0(gpu, &x, 0.03)?;
        report.insert("f0_seconds".into(), tm.elapsed().as_secs_f64().into());
        let rf = npy(&refdir.join("f0_out_1.npy"))?;
        let ff: Vec<f32> = f.iter().map(|v| *v as f32).collect();
        report.insert("f0".into(), compare(&ff, &rf));
        let voiced_mismatch = ff.iter().zip(&rf).filter(|(a, b)| (**a > 1.0) != (**b > 1.0)).count();
        let cents: Vec<f64> = ff.iter().zip(&rf).filter(|(a, b)| **a > 1.0 && **b > 1.0).map(|(a, b)| 1200.0 * (*a as f64 / *b as f64).log2().abs()).collect();
        report.insert("f0_voicing_mismatch".into(), voiced_mismatch.into());
        report.insert("f0_max_cents".into(), cents.iter().cloned().fold(0.0, f64::max).into());
        report.insert("f0_frames".into(), ff.len().into());
    }
    if parts.contains("style") {
        let (fb, frames) = super::campplus::fbank(&npy(&refdir.join("sem_in_2.npy"))?);
        let rf = npy(&refdir.join("style_in.npy"))?;
        report.insert("fbank".into(), compare(&fb, &rf));
        let c = super::campplus::CamPlus::load(gpu, &models.join("campplus.safetensors"))?;
        let e = c.embed(gpu, &rf, rf.len() / 80)?;
        report.insert("style_from_ref_fbank".into(), compare(&e, &npy(&refdir.join("style_out.npy"))?));
        let e2 = c.embed(gpu, &fb, frames)?;
        report.insert("style".into(), compare(&e2, &npy(&refdir.join("style_out.npy"))?));
    }
    if parts.contains("sem") {
        let w = super::whisper::Whisper::load(gpu, &models.join("whisper-small"))?;
        let x = npy(&refdir.join("sem_in_2.npy"))?;
        let f = w.features(&x);
        let tm = std::time::Instant::now();
        let (y, n) = w.encode(gpu, &x)?;
        report.insert("sem_seconds".into(), tm.elapsed().as_secs_f64().into());
        report.insert("sem_frames".into(), n.into());
        report.insert("sem".into(), compare(&y, &npy(&refdir.join("sem_out_2.npy"))?));
        report.insert("sem_features_max".into(), f.iter().cloned().fold(f32::MIN, f32::max).into());
        if refdir.join("sem_feat.npy").is_file() {
            let rf = npy(&refdir.join("sem_feat.npy"))?;
            report.insert("sem_features".into(), compare(&f, &rf));
            report.insert("sem_features_head".into(), serde_json::json!({"mine": &f[..4], "ref": &rf[..4], "mine_t100": &f[100..104], "ref_t100": &rf[100..104]}));
        }
        if refdir.join("sem_out32.npy").is_file() {
            report.insert("sem_f32".into(), compare(&y, &npy(&refdir.join("sem_out32.npy"))?));
        }
    }
    if parts.contains("lr") || parts.contains("est") || parts.contains("cfm") {
        let dit = super::dit::Dit::load(gpu, &models.join("dit.safetensors"))?;
        if parts.contains("lr") {
            let x = npy(&refdir.join("lr_x_2.npy"))?;
            let f0: Vec<f64> = npy(&refdir.join("lr_f0_2.npy"))?.iter().map(|v| *v as f64).collect();
            let frames = npy(&refdir.join("lr_ylens_2.npy"))?[0] as usize;
            let r = dit.regulate(gpu, &x, x.len() / 768, &f0, frames, false)?;
            let refv = npy(&refdir.join("lr_out_2.npy"))?;
            report.insert("lr".into(), compare(&gpu.read::<f32>(r.ptr, refv.len())?, &refv));
        }
        if parts.contains("est") {
            let x = gpu.upload(&npy(&refdir.join("est_in0.npy"))?)?;
            let p = gpu.upload(&npy(&refdir.join("est_in1.npy"))?)?;
            let tt = npy(&refdir.join("est_in3.npy"))?[0];
            let style = npy(&refdir.join("est_in4.npy"))?;
            let cond = npy(&refdir.join("est_in5.npy"))?;
            let t = cond.len() / 2 / 768;
            let c = gpu.upload(&cond)?;
            let y = dit.estimate(gpu, x.ptr, p.ptr, c.ptr, &style, t, 2, tt)?;
            let refv = npy(&refdir.join("est_out.npy"))?;
            report.insert("est".into(), compare(&gpu.read::<f32>(y.ptr, refv.len())?, &refv));
        }
        if parts.contains("cfm") {
            let mu = npy(&refdir.join("cfm_mu_1.npy"))?;
            let t = mu.len() / 768;
            let prompt = npy(&refdir.join("cfm_prompt_1.npy"))?;
            let p = prompt.len() / 128;
            let style = npy(&refdir.join("cfm_style_1.npy"))?;
            let noise = npy(&refdir.join("cfm_noise_1.npy"))?;
            let c = gpu.upload(&mu)?;
            let tm = std::time::Instant::now();
            let y = dit.infer(gpu, &c, t, &prompt, p, &style, &noise, 30, 0.7)?;
            report.insert("cfm_seconds".into(), tm.elapsed().as_secs_f64().into());
            report.insert("cfm".into(), compare(&y, &npy(&refdir.join("cfm_out_1.npy"))?));
        }
    }
    Ok(report.into())
}

/// Bumped when the native conversion changes its output for the same inputs (part of the artifact cache key).
pub const CONVERT_REVISION: &str = "seedvc-rust-3";

/// Build the model folder `SeedVc::load` reads from the upstream downloads (Seed-VC `checkpoints/` with its hf_cache):
/// the torch checkpoints become f32 safetensors, Whisper-small is copied as is. No Python involved.
pub fn prepare(checkpoints: &Path, out: &Path) -> Result<Value> {
    fn find(dir: &Path, name: &str) -> Option<std::path::PathBuf> {
        for e in std::fs::read_dir(dir).ok()?.flatten() {
            let p = e.path();
            if p.is_dir() {
                if let Some(f) = find(&p, name) {
                    return Some(f);
                }
            } else if p.file_name().and_then(|n| n.to_str()) == Some(name) {
                return Some(p);
            }
        }
        None
    }
    std::fs::create_dir_all(out.join("whisper-small"))?;
    let mut report = serde_json::Map::new();
    for (src, dst) in [("DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2.pth", "dit.safetensors"), ("bigvgan_generator.pt", "bigvgan.safetensors"),
                       ("campplus_cn_common.bin", "campplus.safetensors"), ("rmvpe.pt", "rmvpe.safetensors")] {
        let p = find(checkpoints, src).with_context(|| format!("{src} not under {}", checkpoints.display()))?;
        let t = super::torchpt::load(&p)?;
        super::torchpt::write_safetensors(&out.join(dst), &t)?;
        report.insert(dst.into(), json!({"from": p, "tensors": t.len()}));
    }
    let whisper = find(checkpoints, "preprocessor_config.json").and_then(|p| p.parent().map(Path::to_path_buf))
        .context("whisper-small snapshot (preprocessor_config.json) not found")?;
    for f in ["model.safetensors", "preprocessor_config.json", "config.json"] {
        std::fs::copy(whisper.join(f), out.join("whisper-small").join(f)).with_context(|| format!("copy {f}"))?;
    }
    report.insert("whisper-small".into(), json!({"from": whisper}));
    Ok(report.into())
}

/// Every Seed-VC model, loaded once per conversion.
pub struct SeedVc {
    whisper: super::whisper::Whisper,
    campplus: super::campplus::CamPlus,
    rmvpe: super::rmvpe::Rmvpe,
    dit: super::dit::Dit,
    vocoder: super::bigvgan::BigVgan,
}

pub struct ConvertOptions {
    pub steps: usize,
    pub cfg_rate: f32,
    pub seed: u64,
    /// Natural pitch motion added to the source's F0 before conversion (0 / 0 = the contour as it is): the generated
    /// singing holds its notes dead flat and jumps between them like pitch correction, which sounds electronic.
    pub vibrato_cents: f32,
    pub glide_ms: f32,
    /// Blend the two nearest pitch-bin embeddings instead of snapping to one (Dit::regulate).
    pub soft_f0: bool,
    /// Undo pitch-correction-like singing (deautotune_f0): a slide up into each note from `scoop_cents` below, and a
    /// slow wander of up to `drift_cents` on held notes. 0 = off.
    pub scoop_cents: f32,
    pub drift_cents: f32,
    /// Fast small wobble on held notes (new target every 50 ms), so the pitch is never frozen. 0 = off.
    pub jitter_cents: f32,
}

/// The generated singing often sits exactly on the semitones and jumps between them like hard pitch correction. Real
/// singers slide into a note from below and wander a little around it: each note (a voiced stretch within 50 cents of
/// where it began, at least 80 ms) starts `scoop_cents` low and rises over 60 ms, and gets a smooth random offset of up
/// to `drift_cents` (new target every 250 ms, cosine eased), plus a faster one of up to `jitter_cents` (every 50 ms):
/// generated notes hold a frozen pitch, real ones never do. Deterministic for a given `seed`.
pub fn deautotune_f0(f0: &[f64], scoop_cents: f64, drift_cents: f64, jitter_cents: f64, seed: u64) -> Vec<f64> {
    let mut out = f0.to_vec();
    let mut rng = seed ^ 0x9e37_79b9_7f4a_7c15;
    let mut rand = move || {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        (rng >> 11) as f64 / (1u64 << 53) as f64 * 2.0 - 1.0
    };
    let mut i = 0;
    while i < f0.len() {
        if f0[i] <= 1.0 {
            i += 1;
            continue;
        }
        let start = i;
        while i < f0.len() && f0[i] > 1.0 {
            i += 1;
        }
        let cents: Vec<f64> = f0[start..i].iter().map(|f| 1200.0 * (f / 440.0).log2()).collect();
        let n = cents.len();
        let mut k = 0;
        while k < n {
            let (s, anchor) = (k, cents[k]);
            while k < n && (cents[k] - anchor).abs() < 50.0 {
                k += 1;
            }
            if k == s {
                k += 1;
                continue;
            }
            if k - s < 8 {
                continue;
            }
            let len = k - s;
            let points: Vec<f64> = (0..=len / 25 + 1).map(|_| rand() * drift_cents).collect();
            let wobble: Vec<f64> = (0..=len / 5 + 1).map(|_| rand() * jitter_cents).collect();
            for (t, j) in (s..k).enumerate() {
                let mut off = 0.0;
                if t < 6 {
                    off -= scoop_cents * (1.0 - t as f64 / 6.0).powi(2);
                }
                if drift_cents > 0.0 {
                    let (seg, w) = (t / 25, (t % 25) as f64 / 25.0);
                    let e = 0.5 - 0.5 * (std::f64::consts::PI * w).cos();
                    // fade the drift in and out with the note so the note boundaries stay where they were
                    let fade = ((t as f64 / 8.0).min(1.0)) * (((len - t) as f64 / 8.0).min(1.0));
                    off += fade * (points[seg] * (1.0 - e) + points[seg + 1] * e);
                }
                if jitter_cents > 0.0 {
                    let (seg, w) = (t / 5, (t % 5) as f64 / 5.0);
                    let e = 0.5 - 0.5 * (std::f64::consts::PI * w).cos();
                    off += wobble[seg] * (1.0 - e) + wobble[seg + 1] * e;
                }
                out[start + j] = 440.0 * 2f64.powf((cents[j] + off) / 1200.0);
            }
        }
    }
    out
}

/// Humanized F0 (Hz per 10 ms frame, 0 = unvoiced): within each voiced run the contour is smoothed over `glide_ms`
/// (note changes become short slides), and notes held longer than 250 ms get a 5.5 Hz vibrato of `vibrato_cents` that
/// fades in after 200 ms and out over the last 80 ms.
pub fn humanize_f0(f0: &[f64], vibrato_cents: f64, glide_ms: f64) -> Vec<f64> {
    let mut out = f0.to_vec();
    let half = ((glide_ms / 10.0) / 2.0).round() as usize;
    let mut i = 0;
    while i < f0.len() {
        if f0[i] <= 1.0 {
            i += 1;
            continue;
        }
        let start = i;
        while i < f0.len() && f0[i] > 1.0 {
            i += 1;
        }
        let cents: Vec<f64> = f0[start..i].iter().map(|f| 1200.0 * (f / 440.0).log2()).collect();
        let n = cents.len();
        let mut c: Vec<f64> = (0..n).map(|k| {
            let (a, b) = (k.saturating_sub(half), (k + half + 1).min(n));
            cents[a..b].iter().sum::<f64>() / (b - a) as f64
        }).collect();
        if vibrato_cents > 0.0 {
            // notes: stretches that stay within 50 cents of where they began
            let mut k = 0;
            while k < n {
                let (s, anchor) = (k, cents[k]);
                while k < n && (cents[k] - anchor).abs() < 50.0 {
                    k += 1;
                }
                if k - s >= 25 {
                    for j in s..k {
                        let t = (j - s) as f64;
                        let fade = ((t - 20.0) / 15.0).clamp(0.0, 1.0) * ((k - j) as f64 / 8.0).clamp(0.0, 1.0);
                        c[j] += vibrato_cents * fade * (2.0 * std::f64::consts::PI * 5.5 * t * 0.01).sin();
                    }
                }
                if k == s {
                    k += 1;
                }
            }
        }
        for (k, v) in c.iter().enumerate() {
            out[start + k] = 440.0 * 2f64.powf(v / 1200.0);
        }
    }
    out
}

impl SeedVc {
    /// `dir` holds dit / bigvgan / campplus / rmvpe .safetensors and whisper-small/ (see Docs/HEROINE_SINGING_VALIDATION.md).
    pub fn load(gpu: &Gpu, dir: &Path) -> Result<Self> {
        Ok(Self {
            whisper: super::whisper::Whisper::load(gpu, &dir.join("whisper-small"))?,
            campplus: super::campplus::CamPlus::load(gpu, &dir.join("campplus.safetensors"))?,
            rmvpe: super::rmvpe::Rmvpe::load(gpu, &dir.join("rmvpe.safetensors"))?,
            dit: super::dit::Dit::load(gpu, &dir.join("dit.safetensors"))?,
            vocoder: super::bigvgan::BigVgan::load(gpu, &dir.join("bigvgan.safetensors"))?,
        })
    }

    /// Whisper features of 16 kHz audio of any length (30 s windows with 5 s of overlap, as upstream).
    fn semantic(&self, gpu: &Gpu, w16: &[f32]) -> Result<(Vec<f32>, usize)> {
        if w16.len() <= 16000 * 30 {
            return self.whisper.encode(gpu, w16);
        }
        let overlap = 5;
        let mut out = Vec::new();
        let mut n = 0;
        let mut buffer: Option<Vec<f32>> = None;
        let mut pos = 0usize;
        while pos < w16.len() {
            let chunk: Vec<f32> = match &buffer {
                None => w16[pos..(pos + 16000 * 30).min(w16.len())].to_vec(),
                Some(b) => b.iter().copied().chain(w16[pos..(pos + 16000 * (30 - overlap)).min(w16.len())].iter().copied()).collect(),
            };
            let (s, k) = self.whisper.encode(gpu, &chunk)?;
            let skip = if pos == 0 { 0 } else { (50 * overlap).min(k) };
            out.extend_from_slice(&s[skip * 768..k * 768]);
            n += k - skip;
            buffer = Some(chunk[chunk.len() - 16000 * overlap..].to_vec());
            pos += if pos == 0 { 30 * 16000 } else { chunk.len() - 16000 * overlap };
        }
        Ok((out, n))
    }

    /// Seed-VC inference.py with f0 conditioning, no auto F0 adjust and no pitch shift: `source` (the sung vocal) in
    /// the voice of `reference`. Both mono at 44.1 kHz; returns 44.1 kHz mono.
    pub fn convert(&self, gpu: &Gpu, source: &[f32], reference: &[f32], o: &ConvertOptions, cancel: &dyn Fn() -> bool) -> Result<Vec<f32>> {
        use crate::audio::resample;
        let reference = &reference[..reference.len().min(RATE as usize * 25)];
        let src16 = resample(source, RATE, 16000);
        let ref16 = resample(reference, RATE, 16000);
        let (s_alt, n_alt) = self.semantic(gpu, &src16)?;
        let (s_ori, n_ori) = self.semantic(gpu, &ref16)?;
        let (_, frames) = mel(source);
        let (mel2, frames2) = mel(reference);
        let (fb, fb_frames) = super::campplus::fbank(&ref16);
        let style = self.campplus.embed(gpu, &fb, fb_frames)?;
        let f0_ori = self.rmvpe.f0(gpu, &ref16, 0.03)?;
        let f0_alt = self.rmvpe.f0(gpu, &src16, 0.03)?;
        // shifted_f0_alt = exp(log(F0_alt + 1e-5)) (no auto adjust, no semitone shift)
        let f0_alt = if o.scoop_cents > 0.0 || o.drift_cents > 0.0 || o.jitter_cents > 0.0 {
            deautotune_f0(&f0_alt, o.scoop_cents as f64, o.drift_cents as f64, o.jitter_cents as f64, o.seed)
        } else { f0_alt };
        let f0_alt = if o.vibrato_cents > 0.0 || o.glide_ms > 0.0 { humanize_f0(&f0_alt, o.vibrato_cents as f64, o.glide_ms as f64) } else { f0_alt };
        let f0_alt: Vec<f64> = f0_alt.iter().map(|f| (f + 1e-5).ln().exp()).collect();
        let cond = self.dit.regulate(gpu, &s_alt, n_alt, &f0_alt, frames, o.soft_f0)?;
        let prompt = self.dit.regulate(gpu, &s_ori, n_ori, &f0_ori, frames2, o.soft_f0)?;
        let max_window = (RATE as usize / HOP) * 30;
        if frames2 >= max_window {
            anyhow::bail!("reference too long");
        }
        let max_source = max_window - frames2;
        let overlap_frames = 16;
        let overlap = overlap_frames * HOP;
        let mut noise = super::codec::SeededNoise(o.seed);
        let mut out: Vec<f32> = Vec::new();
        let mut previous: Vec<f32> = Vec::new();
        let mut done = 0usize;
        let fade = |prev: &[f32], cur: &mut [f32]| {
            let n = overlap;
            for i in 0..n.min(cur.len()) {
                let a = std::f64::consts::FRAC_PI_2 * i as f64 / (n - 1) as f64;
                let (fo, fi) = (a.cos().powi(2), (std::f64::consts::FRAC_PI_2 - a).cos().powi(2));
                cur[i] = (cur[i] as f64 * fi + prev[prev.len() - n + i] as f64 * fo) as f32;
            }
        };
        while done < frames {
            if cancel() {
                anyhow::bail!("cancelled");
            }
            let len = max_source.min(frames - done);
            let last = done + max_source >= frames;
            let t = frames2 + len;
            let cat = gpu.alloc(t * 768 * 4)?;
            gpu.copy(cat.ptr, prompt.ptr, frames2 * 768 * 4)?;
            gpu.copy(cat.ptr + (frames2 * 768 * 4) as u64, cond.ptr + (done * 768 * 4) as u64, len * 768 * 4)?;
            let z = super::codec::Noise::draw(&mut noise, 128, t)?;
            let m = self.dit.infer(gpu, &cat, t, &mel2, frames2, &style, &z, o.steps, o.cfg_rate)?;
            let mut part = vec![0f32; 128 * len];
            for c in 0..128 {
                part[c * len..(c + 1) * len].copy_from_slice(&m[c * t + frames2..(c + 1) * t]);
            }
            let wave = self.vocoder.forward(gpu, &part, len)?;
            if done == 0 {
                if last {
                    out.extend_from_slice(&wave);
                    break;
                }
                out.extend_from_slice(&wave[..wave.len() - overlap]);
                previous = wave[wave.len() - overlap..].to_vec();
            } else if last {
                let mut w = wave;
                fade(&previous, &mut w);
                out.extend_from_slice(&w);
                break;
            } else {
                let mut w = wave[..wave.len() - overlap].to_vec();
                fade(&previous, &mut w);
                out.extend_from_slice(&w);
                previous = wave[wave.len() - overlap..].to_vec();
            }
            done += len - overlap_frames;
        }
        Ok(out)
    }
}
