//! Speaker encoder of the Qwen3-TTS Base models (ECAPA-TDNN, `speaker_encoder.*`), on the CPU in f32: 24 kHz audio ->
//! 128-band log mel (n_fft 1024, hop 256, Slaney filters up to 12 kHz) -> one speaker embedding (2048 values for 1.7B, 1024 for 0.6B), the row the
//! Base talker takes in place of a voice description (x-vector mode). Follows qwen_tts `Qwen3TTSSpeakerEncoder` and
//! `mel_spectrogram`; it runs once per reference clip, so plain loops are fast enough.

use anyhow::{bail, Result};
use safetensors::Dtype;

use crate::talker::bf16_to_f32;
use crate::weights::Store;

const N_FFT: usize = 1024;
const HOP: usize = 256;
const MELS: usize = 128;
const RATE: f64 = 24_000.0;
const F_MAX: f64 = 12_000.0;
const CHANNELS: usize = 512;
const SCALE: usize = 8;

/// Conv1d with PyTorch "same" reflect padding.
struct Conv {
    w: Vec<f32>, // [out][in][k]
    b: Vec<f32>,
    out: usize,
    inp: usize,
    k: usize,
    dilation: usize,
}

impl Conv {
    fn load(st: &Store, name: &str, dilation: usize) -> Result<Self> {
        let (wb, shape) = st.bytes(&format!("{name}.weight"), Dtype::BF16)?;
        let (bb, _) = st.bytes(&format!("{name}.bias"), Dtype::BF16)?;
        let f = |b: &[u8]| -> Vec<f32> { b.chunks_exact(2).map(|c| bf16_to_f32(u16::from_le_bytes([c[0], c[1]]))).collect() };
        if shape.len() != 3 {
            bail!("{name}: expected a conv1d weight, got {shape:?}");
        }
        Ok(Self { w: f(wb), b: f(bb), out: shape[0], inp: shape[1], k: shape[2], dilation })
    }

    /// x: [inp][t] -> [out][t]
    fn forward(&self, x: &[f32], t: usize) -> Vec<f32> {
        debug_assert_eq!(x.len(), self.inp * t);
        let total = self.dilation * (self.k - 1);
        let left = total / 2;
        // Reflect padding: index -1 -> 1, t -> t-2.
        let src = |i: isize| -> usize {
            let n = t as isize;
            let mut j = i;
            if j < 0 {
                j = -j;
            }
            if j >= n {
                j = 2 * (n - 1) - j;
            }
            j.clamp(0, n - 1) as usize
        };
        let mut y = vec![0f32; self.out * t];
        for o in 0..self.out {
            let row = &mut y[o * t..(o + 1) * t];
            row.iter_mut().for_each(|v| *v = self.b[o]);
            for c in 0..self.inp {
                let xin = &x[c * t..(c + 1) * t];
                for kk in 0..self.k {
                    let w = self.w[(o * self.inp + c) * self.k + kk];
                    if w == 0.0 {
                        continue;
                    }
                    let shift = (kk * self.dilation) as isize - left as isize;
                    if shift == 0 {
                        for (v, xv) in row.iter_mut().zip(xin) {
                            *v += w * xv;
                        }
                    } else {
                        for (i, v) in row.iter_mut().enumerate() {
                            *v += w * xin[src(i as isize + shift)];
                        }
                    }
                }
            }
        }
        y
    }
}

fn relu(v: &mut [f32]) {
    v.iter_mut().for_each(|x| *x = x.max(0.0));
}

struct SeRes2 {
    tdnn1: Conv,
    res2: Vec<Conv>,
    tdnn2: Conv,
    se1: Conv,
    se2: Conv,
}

impl SeRes2 {
    fn forward(&self, x: &[f32], t: usize) -> Vec<f32> {
        let mut h = self.tdnn1.forward(x, t);
        relu(&mut h);
        // Res2Net: split into SCALE parts; part 0 passes, part i>0 goes through block i-1 (plus the previous output).
        let part = CHANNELS / SCALE;
        let mut out = vec![0f32; CHANNELS * t];
        out[..part * t].copy_from_slice(&h[..part * t]);
        let mut prev: Vec<f32> = Vec::new();
        for i in 1..SCALE {
            let mut input = h[i * part * t..(i + 1) * part * t].to_vec();
            if i > 1 {
                input.iter_mut().zip(&prev).for_each(|(a, b)| *a += b);
            }
            let mut y = self.res2[i - 1].forward(&input, t);
            relu(&mut y);
            out[i * part * t..(i + 1) * part * t].copy_from_slice(&y);
            prev = y;
        }
        let mut h = self.tdnn2.forward(&out, t);
        relu(&mut h);
        // Squeeze-excitation over the time mean.
        let mean: Vec<f32> = (0..CHANNELS).map(|c| h[c * t..(c + 1) * t].iter().sum::<f32>() / t as f32).collect();
        let mut s = self.se1.forward(&mean, 1);
        relu(&mut s);
        let s = self.se2.forward(&s, 1);
        for c in 0..CHANNELS {
            let g = 1.0 / (1.0 + (-s[c]).exp());
            for i in 0..t {
                h[c * t + i] = h[c * t + i] * g + x[c * t + i];
            }
        }
        h
    }
}

pub struct SpeakerEncoder {
    block0: Conv,
    blocks: Vec<SeRes2>,
    mfa: Conv,
    asp_tdnn: Conv,
    asp_conv: Conv,
    fc: Conv,
    mel_basis: Vec<f64>, // [MELS][N_FFT/2+1]
}

impl SpeakerEncoder {
    pub fn load(st: &Store) -> Result<Self> {
        let p = "speaker_encoder";
        let mut blocks = Vec::new();
        for (i, dilation) in [(1usize, 2usize), (2, 3), (3, 4)] {
            let b = format!("{p}.blocks.{i}");
            blocks.push(SeRes2 {
                tdnn1: Conv::load(st, &format!("{b}.tdnn1.conv"), 1)?,
                res2: (0..SCALE - 1).map(|j| Conv::load(st, &format!("{b}.res2net_block.blocks.{j}.conv"), dilation)).collect::<Result<_>>()?,
                tdnn2: Conv::load(st, &format!("{b}.tdnn2.conv"), 1)?,
                se1: Conv::load(st, &format!("{b}.se_block.conv1"), 1)?,
                se2: Conv::load(st, &format!("{b}.se_block.conv2"), 1)?,
            });
        }
        Ok(Self {
            block0: Conv::load(st, &format!("{p}.blocks.0.conv"), 1)?,
            blocks,
            mfa: Conv::load(st, &format!("{p}.mfa.conv"), 1)?,
            asp_tdnn: Conv::load(st, &format!("{p}.asp.tdnn.conv"), 1)?,
            asp_conv: Conv::load(st, &format!("{p}.asp.conv"), 1)?,
            fc: Conv::load(st, &format!("{p}.fc"), 1)?,
            mel_basis: slaney_mel_basis(),
        })
    }

    /// Length of the embedding: the talker's width (1.7B: 2048, 0.6B: 1024).
    pub fn dim(&self) -> usize {
        self.fc.out
    }

    /// 24 kHz mono samples (at least ~0.5 s) -> speaker embedding (`dim` values).
    pub fn embed(&self, samples: &[f32]) -> Result<Vec<f32>> {
        let (mel, t) = self.mel(samples)?;
        let mut h = self.block0.forward(&mel, t);
        relu(&mut h);
        let mut outs = Vec::new();
        for b in &self.blocks {
            h = b.forward(&h, t);
            outs.push(h.clone());
        }
        let cat: Vec<f32> = outs.concat(); // [1536][t]
        let mut h = self.mfa.forward(&cat, t);
        relu(&mut h);
        let ch = 3 * CHANNELS;
        // Attentive statistics pooling.
        let stats = |w: &dyn Fn(usize, usize) -> f64| -> (Vec<f32>, Vec<f32>) {
            let mut mean = vec![0f32; ch];
            let mut std = vec![0f32; ch];
            for c in 0..ch {
                let row = &h[c * t..(c + 1) * t];
                let m: f64 = row.iter().enumerate().map(|(i, v)| w(c, i) * *v as f64).sum();
                let var: f64 = row.iter().enumerate().map(|(i, v)| w(c, i) * (*v as f64 - m).powi(2)).sum();
                mean[c] = m as f32;
                std[c] = var.max(1e-12).sqrt() as f32;
            }
            (mean, std)
        };
        let (mean, std) = stats(&|_, _| 1.0 / t as f64);
        let mut att_in = h.clone();
        for c in 0..ch {
            att_in.extend(std::iter::repeat(mean[c]).take(t));
        }
        for c in 0..ch {
            att_in.extend(std::iter::repeat(std[c]).take(t));
        }
        let mut a = self.asp_tdnn.forward(&att_in, t);
        relu(&mut a);
        a.iter_mut().for_each(|v| *v = v.tanh());
        let mut a = self.asp_conv.forward(&a, t);
        for c in 0..ch {
            let row = &mut a[c * t..(c + 1) * t];
            let max = row.iter().cloned().fold(f32::NEG_INFINITY, f32::max);
            let sum: f32 = row.iter_mut().map(|v| {
                *v = (*v - max).exp();
                *v
            }).sum();
            row.iter_mut().for_each(|v| *v /= sum);
        }
        let (mean, std) = stats(&|c, i| a[c * t + i] as f64);
        let pooled: Vec<f32> = mean.into_iter().chain(std).collect();
        Ok(self.fc.forward(&pooled, 1))
    }

    /// log mel [MELS][frames], like qwen_tts mel_spectrogram(center=False, reflect pad (n_fft-hop)/2).
    fn mel(&self, y: &[f32]) -> Result<(Vec<f32>, usize)> {
        let pad = (N_FFT - HOP) / 2;
        if y.len() < N_FFT {
            bail!("reference audio is too short");
        }
        let n = y.len() as isize;
        let at = |i: isize| -> f64 {
            let mut j = i - pad as isize;
            if j < 0 {
                j = -j;
            }
            if j >= n {
                j = 2 * (n - 1) - j;
            }
            y[j.clamp(0, n - 1) as usize] as f64
        };
        let padded = y.len() + 2 * pad;
        let frames = (padded - N_FFT) / HOP + 1;
        let bins = N_FFT / 2 + 1;
        let window: Vec<f64> = (0..N_FFT).map(|i| 0.5 - 0.5 * (2.0 * std::f64::consts::PI * i as f64 / N_FFT as f64).cos()).collect();
        let mut out = vec![0f32; MELS * frames];
        let mut re = vec![0f64; N_FFT];
        let mut im = vec![0f64; N_FFT];
        for f in 0..frames {
            for i in 0..N_FFT {
                re[i] = at((f * HOP + i) as isize) * window[i];
                im[i] = 0.0;
            }
            fft(&mut re, &mut im);
            let mag: Vec<f64> = (0..bins).map(|k| (re[k] * re[k] + im[k] * im[k] + 1e-9).sqrt()).collect();
            for m in 0..MELS {
                let basis = &self.mel_basis[m * bins..(m + 1) * bins];
                let v: f64 = basis.iter().zip(&mag).map(|(a, b)| a * b).sum();
                out[m * frames + f] = v.max(1e-5).ln() as f32;
            }
        }
        Ok((out, frames))
    }
}

/// In-place radix-2 FFT (length a power of two).
fn fft(re: &mut [f64], im: &mut [f64]) {
    let n = re.len();
    let mut j = 0;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if i < j {
            re.swap(i, j);
            im.swap(i, j);
        }
    }
    let mut len = 2;
    while len <= n {
        let ang = -2.0 * std::f64::consts::PI / len as f64;
        for start in (0..n).step_by(len) {
            for k in 0..len / 2 {
                let (s, c) = (ang * k as f64).sin_cos();
                let (a, b) = (start + k, start + k + len / 2);
                let tr = re[b] * c - im[b] * s;
                let ti = re[b] * s + im[b] * c;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
        len <<= 1;
    }
}

/// librosa.filters.mel(sr=24000, n_fft=1024, n_mels=128, fmin=0, fmax=12000) with the Slaney scale and norm.
fn slaney_mel_basis() -> Vec<f64> {
    let f_sp = 200.0 / 3.0;
    let min_log_hz = 1000.0;
    let min_log_mel = min_log_hz / f_sp;
    let logstep = (6.4f64).ln() / 27.0;
    let hz_to_mel = |hz: f64| if hz >= min_log_hz { min_log_mel + (hz / min_log_hz).ln() / logstep } else { hz / f_sp };
    let mel_to_hz = |mel: f64| if mel >= min_log_mel { min_log_hz * (logstep * (mel - min_log_mel)).exp() } else { f_sp * mel };
    let bins = N_FFT / 2 + 1;
    let fft_freqs: Vec<f64> = (0..bins).map(|k| k as f64 * (RATE / 2.0) / (bins - 1) as f64).collect();
    let (lo, hi) = (hz_to_mel(0.0), hz_to_mel(F_MAX));
    let mel_f: Vec<f64> = (0..MELS + 2).map(|i| mel_to_hz(lo + (hi - lo) * i as f64 / (MELS + 1) as f64)).collect();
    let mut w = vec![0f64; MELS * bins];
    for m in 0..MELS {
        let (d0, d1) = (mel_f[m + 1] - mel_f[m], mel_f[m + 2] - mel_f[m + 1]);
        let enorm = 2.0 / (mel_f[m + 2] - mel_f[m]);
        for k in 0..bins {
            let lower = (fft_freqs[k] - mel_f[m]) / d0;
            let upper = (mel_f[m + 2] - fft_freqs[k]) / d1;
            w[m * bins + k] = lower.min(upper).max(0.0) * enorm;
        }
    }
    w
}

pub fn cosine(a: &[f32], b: &[f32]) -> f64 {
    let dot: f64 = a.iter().zip(b).map(|(x, y)| *x as f64 * *y as f64).sum();
    let na: f64 = a.iter().map(|x| (*x as f64).powi(2)).sum::<f64>().sqrt();
    let nb: f64 = b.iter().map(|x| (*x as f64).powi(2)).sum::<f64>().sqrt();
    dot / (na * nb).max(1e-12)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fft_finds_a_cosine() {
        let n = 64;
        let mut re: Vec<f64> = (0..n).map(|i| (2.0 * std::f64::consts::PI * 5.0 * i as f64 / n as f64).cos()).collect();
        let mut im = vec![0.0; n];
        fft(&mut re, &mut im);
        let mag: Vec<f64> = (0..n / 2).map(|k| (re[k] * re[k] + im[k] * im[k]).sqrt()).collect();
        let peak = mag.iter().enumerate().max_by(|a, b| a.1.partial_cmp(b.1).unwrap()).unwrap().0;
        assert_eq!(peak, 5);
        assert!((mag[5] - n as f64 / 2.0).abs() < 1e-9);
    }

    #[test]
    fn slaney_filters_are_triangles_in_order() {
        let w = slaney_mel_basis();
        let bins = N_FFT / 2 + 1;
        let peaks: Vec<usize> = (0..MELS).map(|m| {
            let row = &w[m * bins..(m + 1) * bins];
            row.iter().enumerate().max_by(|a, b| a.1.partial_cmp(b.1).unwrap()).unwrap().0
        }).collect();
        assert!(peaks.windows(2).all(|p| p[0] <= p[1]), "filter centres rise with the band");
        assert!(w.iter().all(|v| *v >= 0.0));
        // Nothing above fmax (12 kHz = the last bin at 24 kHz) is left out, nothing below 0 Hz exists.
        assert!(w[(MELS - 1) * bins..].iter().any(|v| *v > 0.0));
    }
}
