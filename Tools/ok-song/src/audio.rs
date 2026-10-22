//! Audio for songs, in Rust: WAV in/out, resampling, the vocal + accompaniment mix, loudness (ITU-R BS.1770-4
//! integrated, with a 4x oversampled true peak), the mouth envelope and OGG Vorbis for the game.

use std::io::Write;
use std::num::{NonZeroU32, NonZeroU8};
use std::path::Path;

use anyhow::{bail, Context, Result};
use serde::Serialize;

pub const OUTPUT_RATE: u32 = 48_000;

/// Channels of samples (each the same length) at `rate`.
#[derive(Clone)]
pub struct Audio {
    pub rate: u32,
    pub channels: Vec<Vec<f32>>,
}

impl Audio {
    pub fn frames(&self) -> usize {
        self.channels.first().map(|c| c.len()).unwrap_or(0)
    }

    pub fn seconds(&self) -> f64 {
        self.frames() as f64 / self.rate as f64
    }

    pub fn read_wav(path: &Path) -> Result<Self> {
        let mut r = hound::WavReader::open(path).with_context(|| format!("open {}", path.display()))?;
        let spec = r.spec();
        let n = spec.channels as usize;
        if n == 0 {
            bail!("{} has no channels", path.display());
        }
        let interleaved: Vec<f32> = match spec.sample_format {
            hound::SampleFormat::Float => r.samples::<f32>().collect::<Result<_, _>>()?,
            hound::SampleFormat::Int => {
                let scale = 1.0 / (1u64 << (spec.bits_per_sample - 1)) as f32;
                r.samples::<i32>().map(|s| s.map(|v| v as f32 * scale)).collect::<Result<_, _>>()?
            }
        };
        let mut channels = vec![Vec::with_capacity(interleaved.len() / n); n];
        for (i, v) in interleaved.iter().enumerate() {
            channels[i % n].push(*v);
        }
        Ok(Self { rate: spec.sample_rate, channels })
    }

    /// 32-bit float WAV.
    pub fn write_wav(&self, path: &Path) -> Result<()> {
        let spec = hound::WavSpec { channels: self.channels.len() as u16, sample_rate: self.rate, bits_per_sample: 32, sample_format: hound::SampleFormat::Float };
        let mut w = hound::WavWriter::create(path, spec).with_context(|| format!("create {}", path.display()))?;
        for i in 0..self.frames() {
            for c in &self.channels {
                w.write_sample(c[i])?;
            }
        }
        w.finalize()?;
        Ok(())
    }

    pub fn mono(&self) -> Vec<f32> {
        let n = self.channels.len() as f32;
        (0..self.frames()).map(|i| self.channels.iter().map(|c| c[i]).sum::<f32>() / n).collect()
    }

    pub fn resampled(&self, rate: u32) -> Self {
        if rate == self.rate {
            return self.clone();
        }
        Self { rate, channels: self.channels.iter().map(|c| resample(c, self.rate, rate)).collect() }
    }

    /// Everything finite, not silent, and how many samples sit at full scale.
    pub fn check(&self, what: &str) -> Result<AudioCheck> {
        let mut peak = 0f32;
        let mut clipped = 0usize;
        let mut sum = 0f64;
        let mut count = 0usize;
        for c in &self.channels {
            for v in c {
                if !v.is_finite() {
                    bail!("INVALID_AUDIO: {what} has a non-finite sample");
                }
                peak = peak.max(v.abs());
                if v.abs() >= 0.999 {
                    clipped += 1;
                }
                sum += (*v as f64) * (*v as f64);
                count += 1;
            }
        }
        let rms = if count == 0 { 0.0 } else { (sum / count as f64).sqrt() };
        if self.frames() < self.rate as usize || rms < 1e-4 {
            bail!("INVALID_AUDIO: {what} is shorter than a second or silent (rms {rms:.6})");
        }
        Ok(AudioCheck { seconds: self.seconds(), rate: self.rate, channels: self.channels.len(), frames: self.frames(), peak, rms, clipped })
    }
}

#[derive(Serialize, Clone, Debug)]
pub struct AudioCheck {
    pub seconds: f64,
    pub rate: u32,
    pub channels: usize,
    pub frames: usize,
    pub peak: f32,
    pub rms: f64,
    pub clipped: usize,
}

fn gcd(a: u64, b: u64) -> u64 {
    if b == 0 { a } else { gcd(b, a % b) }
}

/// Windowed-sinc (Blackman, 32 zero crossings each side) band-limited resampling by the rational ratio to/from,
/// polyphase: one precomputed kernel per output phase.
pub fn resample(input: &[f32], from: u32, to: u32) -> Vec<f32> {
    if from == to || input.is_empty() {
        return input.to_vec();
    }
    const HALF: f64 = 32.0;
    let g = gcd(from as u64, to as u64);
    let (up, down) = ((to as u64 / g) as usize, (from as u64 / g) as usize);
    let cutoff = (to as f64 / from as f64).min(1.0) * 0.97;
    let width = (HALF / cutoff).ceil() as i64;
    let taps = (2 * width) as usize;
    // kernel[phase][j] weighs input[center - width + 1 + j] for an output at center + phase/up.
    let kernels: Vec<Vec<f32>> = (0..up).map(|phase| {
        let frac = phase as f64 / up as f64;
        let mut k: Vec<f64> = (0..taps).map(|j| {
            let x = frac - (j as i64 - width + 1) as f64;
            let arg = x * cutoff;
            let sinc = if arg.abs() < 1e-12 { 1.0 } else { (std::f64::consts::PI * arg).sin() / (std::f64::consts::PI * arg) };
            let pos = (x / width as f64 + 1.0) / 2.0;
            if !(0.0..=1.0).contains(&pos) {
                return 0.0;
            }
            sinc * (0.42 - 0.5 * (2.0 * std::f64::consts::PI * pos).cos() + 0.08 * (4.0 * std::f64::consts::PI * pos).cos())
        }).collect();
        let sum: f64 = k.iter().sum();
        k.iter_mut().for_each(|v| *v /= sum);
        k.into_iter().map(|v| v as f32).collect()
    }).collect();
    let out_len = (input.len() as u64 * up as u64 / down as u64) as usize;
    let mut out = Vec::with_capacity(out_len);
    for n in 0..out_len {
        let pos = n as u64 * down as u64;
        let center = (pos / up as u64) as i64;
        let kernel = &kernels[(pos % up as u64) as usize];
        let first = center - width + 1;
        let mut acc = 0f32;
        if first >= 0 && (first as usize + taps) <= input.len() {
            let window = &input[first as usize..first as usize + taps];
            for (a, b) in window.iter().zip(kernel) {
                acc += a * b;
            }
        } else {
            for (j, w) in kernel.iter().enumerate() {
                let k = first + j as i64;
                if k >= 0 && (k as usize) < input.len() {
                    acc += input[k as usize] * w;
                }
            }
        }
        out.push(acc);
    }
    out
}

/// Second-order section (direct form I).
#[derive(Clone, Copy)]
struct Biquad {
    b: [f64; 3],
    a: [f64; 2],
}

impl Biquad {
    fn run(&self, x: &[f32]) -> Vec<f64> {
        let (mut x1, mut x2, mut y1, mut y2) = (0f64, 0f64, 0f64, 0f64);
        x.iter().map(|&v| {
            let v = v as f64;
            let y = self.b[0] * v + self.b[1] * x1 + self.b[2] * x2 - self.a[0] * y1 - self.a[1] * y2;
            x2 = x1;
            x1 = v;
            y2 = y1;
            y1 = y;
            y
        }).collect()
    }
}

/// BS.1770 K-weighting for any rate (the published 48 kHz filters, re-derived from their analog prototypes).
fn k_weighting(rate: u32) -> (Biquad, Biquad) {
    let fs = rate as f64;
    // Stage 1: high shelf.
    let (f0, g, q) = (1681.974450955533, 3.999843853973347, 0.7071752369554196);
    let k = (std::f64::consts::PI * f0 / fs).tan();
    let vh = 10f64.powf(g / 20.0);
    let vb = vh.powf(0.4996667741545416);
    let a0 = 1.0 + k / q + k * k;
    let shelf = Biquad {
        b: [(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0],
        a: [2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0],
    };
    // Stage 2: high pass.
    let (f0, q) = (38.13547087602444, 0.5003270373238773);
    let k = (std::f64::consts::PI * f0 / fs).tan();
    let a0 = 1.0 + k / q + k * k;
    let high = Biquad { b: [1.0, -2.0, 1.0], a: [2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0] };
    (shelf, high)
}

/// Integrated loudness in LUFS (None for something too short or below the absolute gate throughout).
pub fn integrated_lufs(audio: &Audio) -> Option<f64> {
    let (shelf, high) = k_weighting(audio.rate);
    let weighted: Vec<Vec<f64>> = audio.channels.iter().map(|c| {
        let s: Vec<f32> = shelf.run(c).into_iter().map(|v| v as f32).collect();
        high.run(&s)
    }).collect();
    let block = (audio.rate as f64 * 0.4) as usize;
    let hop = (audio.rate as f64 * 0.1) as usize;
    if audio.frames() < block {
        return None;
    }
    let mut blocks = Vec::new();
    let mut start = 0;
    while start + block <= audio.frames() {
        let z: f64 = weighted.iter().map(|c| c[start..start + block].iter().map(|v| v * v).sum::<f64>() / block as f64).sum();
        blocks.push(z);
        start += hop;
    }
    let loud = |z: f64| -0.691 + 10.0 * z.max(1e-12).log10();
    let abs: Vec<f64> = blocks.into_iter().filter(|z| loud(*z) > -70.0).collect();
    if abs.is_empty() {
        return None;
    }
    let mean = abs.iter().sum::<f64>() / abs.len() as f64;
    let rel_gate = loud(mean) - 10.0;
    let rel: Vec<f64> = abs.into_iter().filter(|z| loud(*z) > rel_gate).collect();
    if rel.is_empty() {
        return None;
    }
    Some(loud(rel.iter().sum::<f64>() / rel.len() as f64))
}

/// True peak (dBTP) from 4x oversampling.
pub fn true_peak_db(audio: &Audio) -> f64 {
    let peak = audio.channels.iter().map(|c| resample(c, audio.rate, audio.rate * 4).into_iter().fold(0f32, |m, v| m.max(v.abs()))).fold(0f32, f32::max);
    20.0 * (peak.max(1e-9) as f64).log10()
}

#[derive(Serialize, Clone, Debug)]
pub struct MixReport {
    pub target_lufs: f64,
    pub measured_lufs_before: Option<f64>,
    pub gain_db: f64,
    pub lufs: Option<f64>,
    pub true_peak_dbtp: f64,
    /// "target" (reached), "peak-limited" (gain held back for the true-peak ceiling), "gain-capped", "unmeasured"
    pub outcome: String,
}

/// Her converted vocal (mono) over the accompaniment (stereo) at OUTPUT_RATE, both starting at sample 0, then one
/// gain toward `target_lufs` that never pushes the true peak over `ceiling_dbtp` nor amplifies by more than 12 dB.
pub fn mix(vocals: &Audio, accompaniment: &Audio, target_lufs: f64, ceiling_dbtp: f64) -> Result<(Audio, MixReport)> {
    let v = vocals.resampled(OUTPUT_RATE).mono();
    let acc = accompaniment.resampled(OUTPUT_RATE);
    let len = v.len().max(acc.frames());
    let mut channels = vec![vec![0f32; len]; 2];
    for (ch, out) in channels.iter_mut().enumerate() {
        let a = &acc.channels[ch.min(acc.channels.len() - 1)];
        for i in 0..len {
            out[i] = v.get(i).copied().unwrap_or(0.0) + a.get(i).copied().unwrap_or(0.0);
        }
    }
    let mut out = Audio { rate: OUTPUT_RATE, channels };
    let before = integrated_lufs(&out);
    let peak = true_peak_db(&out);
    let (gain_db, outcome) = match before {
        None => (0.0f64.min(ceiling_dbtp - peak), "unmeasured"),
        Some(l) => {
            let want = target_lufs - l;
            let room = ceiling_dbtp - peak;
            if want > room {
                (room, "peak-limited")
            } else if want > 12.0 {
                (12.0, "gain-capped")
            } else {
                (want, "target")
            }
        }
    };
    let g = 10f64.powf(gain_db / 20.0) as f32;
    out.channels.iter_mut().for_each(|c| c.iter_mut().for_each(|s| *s *= g));
    let report = MixReport { target_lufs, measured_lufs_before: before, gain_db, lufs: integrated_lufs(&out), true_peak_dbtp: true_peak_db(&out), outcome: outcome.into() };
    Ok((out, report))
}

#[derive(Serialize, Clone, Debug)]
pub struct Envelope {
    pub schema: u32,
    pub hop_ms: u32,
    pub sample_rate: u32,
    pub frames: usize,
    /// seconds of audio the first value describes the start of (the song's sample 0)
    pub time_origin: f64,
    pub smoothing: String,
    pub values: Vec<f32>,
}

/// Mouth opening from her vocal alone (not the mix: drums must not move her mouth): RMS per 10 ms, scaled so the
/// loud 95th percentile is 1, with a floor below which the mouth stays shut, then a fast-attack / slow-release follow.
pub fn mouth_envelope(vocals: &Audio) -> Envelope {
    let v = vocals.mono();
    let hop = (vocals.rate as usize) / 100;
    let mut rms: Vec<f32> = v.chunks(hop.max(1)).map(|w| (w.iter().map(|x| x * x).sum::<f32>() / w.len() as f32).sqrt()).collect();
    let mut sorted = rms.clone();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let p95 = sorted.get(sorted.len() * 95 / 100).copied().unwrap_or(0.0).max(1e-4);
    let floor = 0.08;
    let mut level = 0f32;
    for r in rms.iter_mut() {
        let x = ((*r / p95).min(1.0) - floor).max(0.0) / (1.0 - floor);
        level = if x > level { level + (x - level) * 0.6 } else { level + (x - level) * 0.25 };
        *r = level;
    }
    Envelope { schema: 1, hop_ms: 10, sample_rate: vocals.rate, frames: v.len(), time_origin: 0.0,
               smoothing: "rms10ms/p95 floor0.08 attack0.6 release0.25".into(), values: rms }
}

/// OGG Vorbis (quality ~0.6) of all channels.
pub fn encode_ogg(audio: &Audio) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    let mut enc = vorbis_rs::VorbisEncoderBuilder::new(NonZeroU32::new(audio.rate).unwrap(), NonZeroU8::new(audio.channels.len() as u8).context("no channels")?, &mut out)?
        .bitrate_management_strategy(vorbis_rs::VorbisBitrateManagementStrategy::QualityVbr { target_quality: 0.6 })
        .build()?;
    let mut start = 0;
    while start < audio.frames() {
        let end = (start + 4096).min(audio.frames());
        let blocks: Vec<&[f32]> = audio.channels.iter().map(|c| &c[start..end]).collect();
        enc.encode_audio_block(&blocks)?;
        start = end;
    }
    enc.finish()?;
    Ok(out)
}

pub fn write_atomic(path: &Path, bytes: &[u8]) -> Result<()> {
    let tmp = path.with_extension(format!("{}.tmp", std::process::id()));
    {
        let mut f = std::fs::File::create(&tmp).with_context(|| format!("create {}", tmp.display()))?;
        f.write_all(bytes)?;
        f.sync_all()?;
    }
    std::fs::rename(&tmp, path).with_context(|| format!("rename to {}", path.display()))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tone(rate: u32, hz: f32, seconds: f32, amp: f32) -> Vec<f32> {
        (0..(rate as f32 * seconds) as usize).map(|i| amp * (2.0 * std::f32::consts::PI * hz * i as f32 / rate as f32).sin()).collect()
    }

    #[test]
    fn resampling_keeps_a_tone() {
        let x = tone(44_100, 1000.0, 1.0, 0.5);
        let y = resample(&x, 44_100, 48_000);
        assert_eq!(y.len(), 48_000);
        let peak = y[1000..47000].iter().fold(0f32, |m, v| m.max(v.abs()));
        assert!((peak - 0.5).abs() < 0.01, "peak {peak}");
    }

    #[test]
    fn a_full_scale_1k_sine_reads_about_minus_3_lufs() {
        // BS.1770: a 0 dBFS 997 Hz sine in one channel is -3.01 LKFS.
        let a = Audio { rate: 48_000, channels: vec![tone(48_000, 997.0, 5.0, 1.0)] };
        let l = integrated_lufs(&a).unwrap();
        assert!((l + 3.01).abs() < 0.1, "{l}");
    }

    #[test]
    fn mouth_is_shut_in_silence_and_open_on_voice() {
        let mut v = vec![0f32; 24_000];
        v.extend(tone(24_000, 220.0, 1.0, 0.3));
        let e = mouth_envelope(&Audio { rate: 24_000, channels: vec![v] });
        assert!(e.values[..90].iter().all(|x| *x < 0.01));
        assert!(e.values[150..190].iter().all(|x| *x > 0.8));
    }
}
