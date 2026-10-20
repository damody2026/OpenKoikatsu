//! Output files: OGG Vorbis for the game (its runtime speech decoder only reads OGG), WAV for checks.

use std::io::Write;
use std::num::{NonZeroU32, NonZeroU8};
use std::path::Path;

use anyhow::{Context, Result};

pub fn peak_normalize(samples: &mut [f32]) {
    let peak = samples.iter().fold(0f32, |m, v| m.max(v.abs()));
    if peak > 0.99 {
        let k = 0.99 / peak;
        samples.iter_mut().for_each(|v| *v *= k);
    }
}

/// Drop leading / trailing silence (the model sometimes appends seconds of it), keeping a short pad.
pub fn trim_silence(samples: &mut Vec<f32>, rate: u32) {
    let win = (rate / 50) as usize; // 20 ms
    let loud = |w: &[f32]| rms(w) > 0.01;
    let windows: Vec<bool> = samples.chunks(win).map(loud).collect();
    let (Some(first), Some(last)) = (windows.iter().position(|&l| l), windows.iter().rposition(|&l| l)) else { return };
    let start = (first * win).saturating_sub(rate as usize / 10);
    let end = ((last + 1) * win + rate as usize / 5).min(samples.len());
    samples.truncate(end);
    samples.drain(..start);
}

pub fn rms(samples: &[f32]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }
    (samples.iter().map(|v| (*v as f64) * (*v as f64)).sum::<f64>() / samples.len() as f64).sqrt() as f32
}

pub fn encode_ogg(samples: &[f32], rate: u32) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    let mut enc = vorbis_rs::VorbisEncoderBuilder::new(NonZeroU32::new(rate).unwrap(), NonZeroU8::new(1).unwrap(), &mut out)?
        .build()?;
    for block in samples.chunks(4096) {
        enc.encode_audio_block([block])?;
    }
    enc.finish()?;
    Ok(out)
}

/// Write beside the target and rename, so a reader never sees a half file.
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

pub fn wav_bytes(samples: &[f32], rate: u32) -> Vec<u8> {
    let data = samples.len() * 2;
    let mut v = Vec::with_capacity(44 + data);
    v.extend_from_slice(b"RIFF");
    v.extend_from_slice(&((36 + data) as u32).to_le_bytes());
    v.extend_from_slice(b"WAVEfmt ");
    v.extend_from_slice(&16u32.to_le_bytes());
    v.extend_from_slice(&1u16.to_le_bytes());
    v.extend_from_slice(&1u16.to_le_bytes());
    v.extend_from_slice(&rate.to_le_bytes());
    v.extend_from_slice(&(rate * 2).to_le_bytes());
    v.extend_from_slice(&2u16.to_le_bytes());
    v.extend_from_slice(&16u16.to_le_bytes());
    v.extend_from_slice(b"data");
    v.extend_from_slice(&(data as u32).to_le_bytes());
    for s in samples {
        v.extend_from_slice(&((s.clamp(-1.0, 1.0) * 32767.0).round() as i16).to_le_bytes());
    }
    v
}

/// trim_silence + peak_normalize for audio that arrives in pieces: leading silence is dropped (keeping
/// 100 ms before the first loud window), quiet stretches are held back until speech continues, and at
/// the end at most 200 ms of trailing silence is let out. Samples are clamped to +-0.99.
pub struct StreamTrim {
    rate: u32,
    started: bool,
    rest: Vec<f32>,
    lead: Vec<f32>,
    held: Vec<f32>,
}

impl StreamTrim {
    pub fn new(rate: u32) -> Self {
        Self { rate, started: false, rest: Vec::new(), lead: Vec::new(), held: Vec::new() }
    }

    pub fn push(&mut self, input: &[f32]) -> Vec<f32> {
        let win = (self.rate / 50) as usize;
        self.rest.extend(input.iter().map(|v| v.clamp(-0.99, 0.99)));
        let mut out = Vec::new();
        let whole = self.rest.len() / win * win;
        let windows: Vec<f32> = self.rest.drain(..whole).collect();
        for w in windows.chunks(win) {
            let loud = rms(w) > 0.01;
            if !self.started {
                self.lead.extend_from_slice(w);
                let keep = self.rate as usize / 10;
                if self.lead.len() > keep {
                    self.lead.drain(..self.lead.len() - keep);
                }
                if loud {
                    self.started = true;
                    out.append(&mut self.lead);
                }
            } else if loud {
                out.append(&mut self.held);
                out.extend_from_slice(w);
            } else {
                self.held.extend_from_slice(w);
            }
        }
        out
    }

    pub fn finish(&mut self) -> Vec<f32> {
        if !self.started {
            return Vec::new();
        }
        let mut tail = std::mem::take(&mut self.held);
        tail.extend(self.rest.drain(..));
        tail.truncate(self.rate as usize / 5);
        tail
    }
}

/// 16-bit little-endian PCM bytes.
pub fn pcm16(samples: &[f32]) -> Vec<u8> {
    samples.iter().flat_map(|v| ((v.clamp(-1.0, 1.0) * 32767.0) as i16).to_le_bytes()).collect()
}
