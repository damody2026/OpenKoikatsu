//! Host-side DSP for the voice-conversion front ends: real STFT frames, librosa-compatible mel filter banks and the
//! Seed-VC / BigVGAN log-mel spectrogram.

use rustfft::num_complex::Complex;
use rustfft::FftPlanner;

/// Periodic Hann window (torch.hann_window).
pub fn hann(n: usize) -> Vec<f64> {
    (0..n).map(|i| 0.5 - 0.5 * (2.0 * std::f64::consts::PI * i as f64 / n as f64).cos()).collect()
}

/// Magnitude spectra |FFT(frame * window)| of frames starting every `hop` samples of `x` (no padding here), one row
/// of n_fft / 2 + 1 bins per frame. `power` squares instead (no sqrt).
pub fn stft_mag(x: &[f64], n_fft: usize, hop: usize, window: &[f64], power: bool, eps: f64) -> Vec<Vec<f64>> {
    let mut planner = FftPlanner::<f64>::new();
    let fft = planner.plan_fft_forward(n_fft);
    let frames = if x.len() >= window.len() { (x.len() - window.len()) / hop + 1 } else { 0 };
    let off = (n_fft - window.len()) / 2;
    let mut buf = vec![Complex::new(0.0, 0.0); n_fft];
    (0..frames)
        .map(|m| {
            buf.iter_mut().for_each(|c| *c = Complex::new(0.0, 0.0));
            for (i, w) in window.iter().enumerate() {
                buf[off + i] = Complex::new(x[m * hop + i] * w, 0.0);
            }
            fft.process(&mut buf);
            buf[..n_fft / 2 + 1].iter().map(|c| {
                let p = c.re * c.re + c.im * c.im;
                if power { p } else { (p + eps).sqrt() }
            }).collect()
        })
        .collect()
}

pub fn reflect_pad(x: &[f64], left: usize, right: usize) -> Vec<f64> {
    let n = x.len() as isize;
    (-(left as isize)..n + right as isize)
        .map(|i| {
            let mut j = i;
            if j < 0 {
                j = -j;
            }
            if j >= n {
                j = 2 * (n - 1) - j;
            }
            x[j as usize]
        })
        .collect()
}

fn hz_to_mel(f: f64, htk: bool) -> f64 {
    if htk {
        return 2595.0 * (1.0 + f / 700.0).log10();
    }
    let f_sp = 200.0 / 3.0;
    let min_log_hz = 1000.0;
    let min_log_mel = min_log_hz / f_sp;
    let logstep = 6.4f64.ln() / 27.0;
    if f >= min_log_hz { min_log_mel + (f / min_log_hz).ln() / logstep } else { f / f_sp }
}

fn mel_to_hz(m: f64, htk: bool) -> f64 {
    if htk {
        return 700.0 * (10f64.powf(m / 2595.0) - 1.0);
    }
    let f_sp = 200.0 / 3.0;
    let min_log_hz = 1000.0;
    let min_log_mel = min_log_hz / f_sp;
    let logstep = 6.4f64.ln() / 27.0;
    if m >= min_log_mel { min_log_hz * (logstep * (m - min_log_mel)).exp() } else { f_sp * m }
}

/// librosa.filters.mel (norm="slaney" when `slaney_norm`): [n_mels][n_fft / 2 + 1].
pub fn mel_bank(sr: f64, n_fft: usize, n_mels: usize, fmin: f64, fmax: f64, htk: bool, slaney_norm: bool) -> Vec<Vec<f64>> {
    let bins = n_fft / 2 + 1;
    let fft_f: Vec<f64> = (0..bins).map(|i| i as f64 * sr / n_fft as f64).collect();
    let (lo, hi) = (hz_to_mel(fmin, htk), hz_to_mel(fmax, htk));
    let mel_f: Vec<f64> = (0..n_mels + 2).map(|i| mel_to_hz(lo + (hi - lo) * i as f64 / (n_mels + 1) as f64, htk)).collect();
    (0..n_mels)
        .map(|m| {
            let (f0, f1, f2) = (mel_f[m], mel_f[m + 1], mel_f[m + 2]);
            let enorm = if slaney_norm { 2.0 / (f2 - f0) } else { 1.0 };
            fft_f.iter().map(|f| {
                let lower = (f - f0) / (f1 - f0);
                let upper = (f2 - f) / (f2 - f1);
                // librosa weights are float32
                ((0f64.max(lower.min(upper)) * enorm) as f32) as f64
            }).collect()
        })
        .collect()
}

/// Seed-VC / BigVGAN log-mel at 44.1 kHz: reflect pad (n_fft - hop) / 2, |STFT| (+1e-9), slaney mel, log(max(x, 1e-5)).
/// Returns [n_mels][frames].
pub fn log_mel_44k(x: &[f32], n_fft: usize, hop: usize, n_mels: usize, sr: f64) -> Vec<Vec<f32>> {
    let xd: Vec<f64> = x.iter().map(|v| *v as f64).collect();
    let p = (n_fft - hop) / 2;
    let xp = reflect_pad(&xd, p, p);
    let mags = stft_mag(&xp, n_fft, hop, &hann(n_fft), false, 1e-9);
    let bank = mel_bank(sr, n_fft, n_mels, 0.0, sr / 2.0, false, true);
    let frames = mags.len();
    let mut out = vec![vec![0f32; frames]; n_mels];
    for (t, m) in mags.iter().enumerate() {
        for (k, row) in bank.iter().enumerate() {
            let s: f64 = row.iter().zip(m).map(|(a, b)| a * b).sum();
            out[k][t] = (s.max(1e-5)).ln() as f32;
        }
    }
    out
}
