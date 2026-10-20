//! Whisper log-mel features (transformers WhisperFeatureExtractor, as configured in
//! Qwen3-ASR preprocessor_config.json): 16 kHz, n_fft 400, hop 160, 128 Slaney mel bins over 0..8 kHz,
//! periodic Hann window, centered STFT with reflect padding, last frame dropped,
//! log10(max(mel, 1e-10)), clamped to (max - 8), then (x + 4) / 4.

pub const SAMPLE_RATE: u32 = 16_000;
pub const HOP: usize = 160;
const N_FFT: usize = 400;
const BINS: usize = N_FFT / 2 + 1;
pub const MELS: usize = 128;

pub struct MelFrontEnd {
    window: Vec<f64>,
    cos: Vec<f64>,
    sin: Vec<f64>,
    filters: Vec<f64>, // [MELS, BINS]
}

fn hz_to_mel(f: f64) -> f64 {
    let logstep = 27.0 / 6.4f64.ln();
    if f < 1000.0 { 3.0 * f / 200.0 } else { 15.0 + (f / 1000.0).ln() * logstep }
}

fn mel_to_hz(m: f64) -> f64 {
    let logstep = 6.4f64.ln() / 27.0;
    if m < 15.0 { 200.0 * m / 3.0 } else { 1000.0 * ((m - 15.0) * logstep).exp() }
}

impl MelFrontEnd {
    pub fn new() -> Self {
        let window = (0..N_FFT).map(|k| 0.5 - 0.5 * (2.0 * std::f64::consts::PI * k as f64 / N_FFT as f64).cos()).collect();
        let cos = (0..N_FFT).map(|k| (2.0 * std::f64::consts::PI * k as f64 / N_FFT as f64).cos()).collect();
        let sin = (0..N_FFT).map(|k| (2.0 * std::f64::consts::PI * k as f64 / N_FFT as f64).sin()).collect();
        // transformers.audio_utils.mel_filter_bank(norm="slaney", mel_scale="slaney")
        let fft_freqs: Vec<f64> = (0..BINS).map(|k| k as f64 * (SAMPLE_RATE as f64 / 2.0) / (BINS - 1) as f64).collect();
        let (lo, hi) = (hz_to_mel(0.0), hz_to_mel(8000.0));
        let points: Vec<f64> = (0..MELS + 2).map(|i| mel_to_hz(lo + (hi - lo) * i as f64 / (MELS + 1) as f64)).collect();
        let mut filters = vec![0.0; MELS * BINS];
        for m in 0..MELS {
            let enorm = 2.0 / (points[m + 2] - points[m]);
            for (k, &f) in fft_freqs.iter().enumerate() {
                let down = (f - points[m]) / (points[m + 1] - points[m]);
                let up = (points[m + 2] - f) / (points[m + 2] - points[m + 1]);
                filters[m * BINS + k] = down.min(up).max(0.0) * enorm;
            }
        }
        Self { window, cos, sin, filters }
    }

    /// 16 kHz mono samples (a multiple of HOP, at least N_FFT long) -> features [MELS, frames], frames.
    pub fn features(&self, samples: &[f32]) -> (Vec<f32>, usize) {
        let n = samples.len();
        let frames = n / HOP;
        let pad = N_FFT / 2;
        // Reflect padding (numpy "reflect": the edge sample is not repeated).
        let at = |j: isize| -> f64 {
            let n = n as isize;
            let i = if j < 0 { -j } else if j >= n { 2 * n - 2 - j } else { j };
            samples[i.clamp(0, n - 1) as usize] as f64
        };
        let mut logmel = vec![0f64; MELS * frames];
        let mut frame = vec![0f64; N_FFT];
        let mut power = vec![0f64; BINS];
        for t in 0..frames {
            for (k, v) in frame.iter_mut().enumerate() {
                *v = at((t * HOP + k) as isize - pad as isize) * self.window[k];
            }
            for (b, p) in power.iter_mut().enumerate() {
                let (mut re, mut im) = (0.0, 0.0);
                for (k, &v) in frame.iter().enumerate() {
                    let idx = (b * k) % N_FFT;
                    re += v * self.cos[idx];
                    im -= v * self.sin[idx];
                }
                *p = re * re + im * im;
            }
            for m in 0..MELS {
                let row = &self.filters[m * BINS..(m + 1) * BINS];
                let e: f64 = row.iter().zip(&power).map(|(w, p)| w * p).sum();
                logmel[m * frames + t] = e.max(1e-10).log10();
            }
        }
        let max = logmel.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
        let out = logmel.iter().map(|&v| ((v.max(max - 8.0) + 4.0) / 4.0) as f32).collect();
        (out, frames)
    }
}

/// Encoder output length for `frames` mel frames (_get_feat_extract_output_lengths).
pub fn encoder_tokens(frames: usize) -> usize {
    let leave = frames % 100;
    let feat = if leave == 0 { 0 } else { (leave - 1) / 2 + 1 };
    let tail = if feat == 0 { 0 } else { ((feat - 1) / 2 + 1 - 1) / 2 + 1 };
    tail + (frames / 100) * 13
}
