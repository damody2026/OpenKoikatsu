//! Seed-VC singing voice conversion (F0 conditioned, 44.1 kHz) in Rust + CUDA. Mirrors Seed-VC `inference.py` with
//! `--f0-condition true`: Whisper-small content features, CAMPPlus speaker style, RMVPE F0, the length regulator, the
//! DiT flow-matching model (CFG) and the BigVGAN v2 vocoder.

use std::path::Path;

use anyhow::Result;
use serde_json::Value;

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
            let r = dit.regulate(gpu, &x, x.len() / 768, &f0, frames)?;
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
