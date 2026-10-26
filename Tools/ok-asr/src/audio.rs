//! Audio in: WAV files, the default microphone (cpal / WASAPI), mono 16 kHz resampling.

use std::path::Path;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Arc, Mutex};

use anyhow::{anyhow, bail, Context, Result};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};

use crate::mel::{HOP, SAMPLE_RATE};

/// The microphone choice (part of a device name; None = system default), shared and changeable at runtime.
pub type DeviceChoice = Arc<Mutex<Option<String>>>;

/// Longest recording kept; the rest of a long press is dropped.
pub const MAX_SECONDS: f32 = 30.0;

pub fn read_wav(path: &Path) -> Result<(Vec<f32>, u32)> {
    let mut r = hound::WavReader::open(path).with_context(|| format!("open {}", path.display()))?;
    let spec = r.spec();
    let ch = spec.channels.max(1) as usize;
    let interleaved: Vec<f32> = match spec.sample_format {
        hound::SampleFormat::Float => r.samples::<f32>().collect::<std::result::Result<_, _>>()?,
        hound::SampleFormat::Int => {
            let scale = 1.0 / (1u64 << (spec.bits_per_sample - 1)) as f32;
            r.samples::<i32>().map(|s| s.map(|v| v as f32 * scale)).collect::<std::result::Result<_, _>>()?
        }
    };
    Ok((downmix(&interleaved, ch), spec.sample_rate))
}

/// A WAV or an OGG Vorbis file (a song from ok-song), mono.
pub fn read_audio(path: &Path) -> Result<(Vec<f32>, u32)> {
    if !path.extension().is_some_and(|e| e.eq_ignore_ascii_case("ogg")) {
        return read_wav(path);
    }
    let file = std::fs::File::open(path).with_context(|| format!("open {}", path.display()))?;
    let mut r = lewton::inside_ogg::OggStreamReader::new(std::io::BufReader::new(file)).with_context(|| format!("ogg {}", path.display()))?;
    let (ch, rate) = (r.ident_hdr.audio_channels.max(1) as usize, r.ident_hdr.audio_sample_rate);
    let mut interleaved = Vec::new();
    while let Some(packet) = r.read_dec_packet_itl().with_context(|| format!("decode {}", path.display()))? {
        interleaved.extend(packet.into_iter().map(|s| s as f32 / 32768.0));
    }
    Ok((downmix(&interleaved, ch), rate))
}

pub fn downmix(interleaved: &[f32], channels: usize) -> Vec<f32> {
    if channels <= 1 {
        return interleaved.to_vec();
    }
    interleaved.chunks_exact(channels).map(|f| f.iter().sum::<f32>() / channels as f32).collect()
}

/// Windowed-sinc resampler (Hann window, 32 zero crossings each side, cutoff below the lower Nyquist).
pub fn resample(input: &[f32], from: u32, to: u32) -> Vec<f32> {
    if from == to || input.is_empty() {
        return input.to_vec();
    }
    const ZEROS: f64 = 32.0;
    let ratio = to as f64 / from as f64;
    let cutoff = ratio.min(1.0) * 0.95;
    let half = ZEROS / cutoff; // taps each side, in input samples
    let out_len = (input.len() as f64 * ratio).floor() as usize;
    let mut out = Vec::with_capacity(out_len);
    for o in 0..out_len {
        let center = o as f64 / ratio;
        let lo = (center - half).ceil().max(0.0) as usize;
        let hi = ((center + half).floor() as usize).min(input.len() - 1);
        let (mut acc, mut norm) = (0.0f64, 0.0f64);
        for i in lo..=hi {
            let x = i as f64 - center;
            let arg = x * cutoff;
            let sinc = if arg.abs() < 1e-9 { 1.0 } else { (std::f64::consts::PI * arg).sin() / (std::f64::consts::PI * arg) };
            let win = 0.5 + 0.5 * (std::f64::consts::PI * x / half).cos();
            let w = sinc * win;
            acc += input[i] as f64 * w;
            norm += w;
        }
        out.push(if norm.abs() > 1e-12 { (acc / norm) as f32 } else { 0.0 });
    }
    out
}

/// Mono 16 kHz, trimmed to whole mel hops (keeps feature frames and the attention mask in step).
pub fn to_model_rate(samples: &[f32], rate: u32) -> Vec<f32> {
    let mut s = resample(samples, rate, SAMPLE_RATE);
    s.truncate(s.len() / HOP * HOP);
    s
}

pub fn rms(s: &[f32]) -> f32 {
    if s.is_empty() {
        return 0.0;
    }
    (s.iter().map(|v| v * v).sum::<f32>() / s.len() as f32).sqrt()
}

pub struct Recording {
    pub samples: Vec<f32>, // mono, device rate
    pub rate: u32,
    pub device: String,
}

enum Command {
    Start(Sender<Result<String>>),
    Stop(Sender<Result<Recording>>),
}

/// Names of the input devices, the system default first.
pub fn input_devices() -> Vec<String> {
    let host = cpal::default_host();
    let default = host.default_input_device().and_then(|d| d.name().ok());
    let mut names: Vec<String> = host.input_devices().map(|it| it.filter_map(|d| d.name().ok()).collect()).unwrap_or_default();
    if let Some(d) = &default {
        names.retain(|n| n != d);
        names.insert(0, d.clone());
    }
    names
}

/// The microphone stream lives on its own thread (cpal streams are not Send everywhere).
pub struct Recorder {
    tx: Mutex<Sender<Command>>,
    active: Arc<Mutex<bool>>,
}

impl Recorder {
    /// `device`: part of an input device name (case-insensitive); None or no match uses the system default.
    pub fn new(device: DeviceChoice) -> Self {
        let (tx, rx) = channel::<Command>();
        let active = Arc::new(Mutex::new(false));
        let flag = active.clone();
        std::thread::spawn(move || {
            let mut current: Option<(cpal::Stream, Arc<Mutex<Vec<f32>>>, usize, u32, String)> = None;
            while let Ok(cmd) = rx.recv() {
                match cmd {
                    Command::Start(reply) => {
                        if current.is_some() {
                            let _ = reply.send(Err(anyhow!("already recording")));
                            continue;
                        }
                        let hint = device.lock().unwrap().clone();
                        match open_stream(hint.as_deref()) {
                            Ok((stream, buf, ch, rate, name)) => {
                                *flag.lock().unwrap() = true;
                                let _ = reply.send(Ok(name.clone()));
                                current = Some((stream, buf, ch, rate, name));
                            }
                            Err(e) => {
                                let _ = reply.send(Err(e));
                            }
                        }
                    }
                    Command::Stop(reply) => {
                        *flag.lock().unwrap() = false;
                        match current.take() {
                            Some((stream, buf, ch, rate, device)) => {
                                drop(stream);
                                let data = std::mem::take(&mut *buf.lock().unwrap());
                                let _ = reply.send(Ok(Recording { samples: downmix(&data, ch), rate, device }));
                            }
                            None => {
                                let _ = reply.send(Err(anyhow!("not recording")));
                            }
                        }
                    }
                }
            }
        });
        Self { tx: Mutex::new(tx), active }
    }

    pub fn recording(&self) -> bool {
        *self.active.lock().unwrap()
    }

    /// Returns the device name.
    pub fn start(&self) -> Result<String> {
        let (tx, rx) = channel();
        self.tx.lock().unwrap().send(Command::Start(tx)).map_err(|_| anyhow!("recorder stopped"))?;
        rx.recv().map_err(|_| anyhow!("recorder stopped"))?
    }

    pub fn stop(&self) -> Result<Recording> {
        let (tx, rx) = channel();
        self.tx.lock().unwrap().send(Command::Stop(tx)).map_err(|_| anyhow!("recorder stopped"))?;
        rx.recv().map_err(|_| anyhow!("recorder stopped"))?
    }
}

#[allow(clippy::type_complexity)]
/// True when an input device whose name contains `hint` is currently plugged in.
pub(crate) fn device_present(hint: &str) -> bool {
    let h = hint.to_lowercase();
    cpal::default_host().input_devices().map(|mut it| it.any(|d| d.name().map(|n| n.to_lowercase().contains(&h)).unwrap_or(false))).unwrap_or(false)
}

/// open_stream that turns a panic inside the audio backend (device vanishing mid-enumeration) into an error,
/// so the listener thread survives and retries.
pub(crate) fn open_stream_safe(hint: Option<&str>) -> Result<(cpal::Stream, Arc<Mutex<Vec<f32>>>, usize, u32, String)> {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| open_stream(hint))).unwrap_or_else(|_| Err(anyhow!("audio backend panicked while opening the microphone")))
}

/// Set by the stream's error callback (device unplugged, driver reset); cleared by every open_stream.
pub(crate) static STREAM_FAILED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub(crate) fn open_stream(hint: Option<&str>) -> Result<(cpal::Stream, Arc<Mutex<Vec<f32>>>, usize, u32, String)> {
    let host = cpal::default_host();
    let wanted = hint.map(str::to_lowercase).filter(|h| !h.is_empty());
    let chosen = wanted.and_then(|h| host.input_devices().ok()?.find(|d| d.name().map(|n| n.to_lowercase().contains(&h)).unwrap_or(false)));
    let device = match chosen {
        Some(d) => d,
        None => host.default_input_device().ok_or_else(|| anyhow!("no microphone (default input device)"))?,
    };
    let name = device.name().unwrap_or_else(|_| "microphone".into());
    let supported = device.default_input_config().context("microphone config")?;
    let format = supported.sample_format();
    let config: cpal::StreamConfig = supported.into();
    let channels = config.channels.max(1) as usize;
    let rate = config.sample_rate.0;
    let cap = (MAX_SECONDS * rate as f32) as usize * channels;
    let buf = Arc::new(Mutex::new(Vec::<f32>::with_capacity(cap)));
    STREAM_FAILED.store(false, std::sync::atomic::Ordering::SeqCst);
    let err = |e| {
        crate::log!("ok-asr: microphone error: {e}");
        STREAM_FAILED.store(true, std::sync::atomic::Ordering::SeqCst);
    };
    let stream = match format {
        cpal::SampleFormat::F32 => {
            let b = buf.clone();
            device.build_input_stream(&config, move |d: &[f32], _: &_| push(&b, cap, d.iter().copied()), err, None)?
        }
        cpal::SampleFormat::I16 => {
            let b = buf.clone();
            device.build_input_stream(&config, move |d: &[i16], _: &_| push(&b, cap, d.iter().map(|&v| v as f32 / 32768.0)), err, None)?
        }
        cpal::SampleFormat::U16 => {
            let b = buf.clone();
            device.build_input_stream(&config, move |d: &[u16], _: &_| push(&b, cap, d.iter().map(|&v| (v as f32 - 32768.0) / 32768.0)), err, None)?
        }
        cpal::SampleFormat::I32 => {
            let b = buf.clone();
            device.build_input_stream(&config, move |d: &[i32], _: &_| push(&b, cap, d.iter().map(|&v| v as f32 / 2147483648.0)), err, None)?
        }
        other => bail!("unsupported microphone sample format {other:?}"),
    };
    stream.play().context("start microphone")?;
    Ok((stream, buf, channels, rate, name))
}

fn push(buf: &Mutex<Vec<f32>>, cap: usize, data: impl Iterator<Item = f32>) {
    let mut b = buf.lock().unwrap();
    for v in data {
        if b.len() >= cap {
            break;
        }
        b.push(v);
    }
}
