//! Hands-free mode: the microphone stays open, an energy VAD cuts each utterance (speech, then a pause),
//! and every utterance is handed to the server for transcription. While muted (her TTS is playing) the
//! audio is dropped so the speakers are never transcribed as the player.

use std::sync::mpsc::{channel, Sender};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::{anyhow, Result};

use crate::audio::{device_present, downmix, open_stream_safe, DeviceChoice};

const FRAME_MS: usize = 20;
/// Frames above the threshold before an utterance starts (80 ms), and the audio kept from before it.
const START_FRAMES: usize = 4;
const PREROLL_MS: usize = 300;
/// A pause this long ends the utterance (clause pauses measured 0.80-0.86 s must not split a sentence);
/// shorter and longer utterances are dropped or cut.
const END_MS: usize = 1000;
const MIN_MS: usize = 400;
const MAX_MS: usize = 20_000;
/// The speech threshold follows the room: this many times the noise floor, kept between FLOOR_MIN_RMS and
/// THRESHOLD_MAX_RMS so normal speech always starts an utterance.
const THRESHOLD_RATIO: f32 = 3.5;
const FLOOR_MIN_RMS: f32 = 0.006;
const THRESHOLD_MAX_RMS: f32 = 0.03;
/// While the game plays her voice ("ducked"), speech must be this much louder than usual and last twice as long
/// to start an utterance: the player can still talk over her (barge-in), her voice leaking from speakers mostly
/// cannot. Headphones make it reliable.
const DUCK_BOOST: f32 = 3.0;

pub struct Utterance {
    pub samples: Vec<f32>, // mono, device rate
    pub rate: u32,
}

#[derive(Default, Clone)]
pub struct Status {
    pub active: bool,
    pub muted: bool,
    pub ducked: bool,
    pub speaking: bool,
    pub level: f32,
    pub device: String,
}

enum Command {
    Start(Sender<Result<String>>),
    Stop,
    /// The device choice changed: reopen on the current choice in one step (no-op when not listening).
    Reopen(Sender<Result<String>>),
    /// Test input: mono samples at `rate`, mixed into the microphone signal as if the player said them.
    Inject(Vec<f32>, u32),
}

pub struct Listener {
    tx: Mutex<Sender<Command>>,
    status: Arc<Mutex<Status>>,
}

struct Vad {
    floor: f32,
    above: usize,
    in_speech: bool,
    silence: usize,
    preroll: Vec<Vec<f32>>,
    utterance: Vec<f32>,
}

impl Vad {
    fn new() -> Self {
        Self { floor: 0.003, above: 0, in_speech: false, silence: 0, preroll: Vec::new(), utterance: Vec::new() }
    }

    fn reset(&mut self) {
        self.above = 0;
        self.in_speech = false;
        self.silence = 0;
        self.preroll.clear();
        self.utterance.clear();
    }

    /// One frame in; a finished utterance out.
    fn push(&mut self, frame: &[f32], rate: u32) -> Option<Vec<f32>> {
        self.push_ducked(frame, rate, false)
    }

    fn push_ducked(&mut self, frame: &[f32], rate: u32, ducked: bool) -> Option<Vec<f32>> {
        let rms = (frame.iter().map(|v| v * v).sum::<f32>() / frame.len().max(1) as f32).sqrt();
        let boost = if ducked { DUCK_BOOST } else { 1.0 };
        let threshold = (self.floor * THRESHOLD_RATIO).clamp(FLOOR_MIN_RMS, THRESHOLD_MAX_RMS) * boost;
        let start_frames = if ducked { START_FRAMES * 2 } else { START_FRAMES };
        let frames = |ms: usize| ms / FRAME_MS;
        if !self.in_speech {
            // Follow quieter frames quickly and louder ones very slowly, so speech onsets do not lift the floor.
            // Not while ducked: her voice is not the room's noise.
            if !ducked {
            self.floor = if rms < self.floor { 0.5 * self.floor + 0.5 * rms } else { self.floor * 1.002 + 0.000_01 }.max(0.0005);
            }
            self.preroll.push(frame.to_vec());
            if self.preroll.len() > frames(PREROLL_MS) {
                self.preroll.remove(0);
            }
            self.above = if rms > threshold { self.above + 1 } else { 0 };
            if self.above >= start_frames {
                self.in_speech = true;
                self.silence = 0;
                self.utterance = self.preroll.concat();
                self.preroll.clear();
            }
            return None;
        }
        self.utterance.extend_from_slice(frame);
        self.silence = if rms > threshold * 0.7 { 0 } else { self.silence + 1 };
        let per_ms = rate as usize / 1000;
        if self.silence < frames(END_MS) && self.utterance.len() < MAX_MS * per_ms {
            return None;
        }
        // Keep 200 ms of the closing pause; drop clicks and coughs shorter than MIN_MS.
        let trailing = self.silence.saturating_sub(frames(200)) * FRAME_MS * per_ms;
        let mut done = std::mem::take(&mut self.utterance);
        done.truncate(done.len().saturating_sub(trailing));
        self.reset();
        (done.len() >= MIN_MS * per_ms).then_some(done)
    }
}

/// Runs the same VAD over recorded samples (mono, `rate`): the utterances hands-free mode would send.
pub fn split_utterances(samples: &[f32], rate: u32) -> Vec<Vec<f32>> {
    let frame = rate as usize * FRAME_MS / 1000;
    let mut vad = Vad::new();
    let mut out = Vec::new();
    for chunk in samples.chunks_exact(frame).chain(std::iter::repeat_n(&[0.0f32; 0][..], 0)) {
        if let Some(u) = vad.push(chunk, rate) {
            out.push(u);
        }
    }
    // Trailing silence so an utterance at the very end closes like it would live.
    let silence = vec![0.0f32; frame];
    for _ in 0..(END_MS / FRAME_MS + 1) {
        if let Some(u) = vad.push(&silence, rate) {
            out.push(u);
        }
    }
    out
}

impl Listener {
    /// `device`: part of an input device name, as for the push-to-talk recorder.
    pub fn new(device: DeviceChoice, on_utterance: impl Fn(Utterance) + Send + 'static) -> Self {
        let (tx, rx) = channel::<Command>();
        let status = Arc::new(Mutex::new(Status::default()));
        let shared = status.clone();
        std::thread::spawn(move || {
            let mut stream: Option<(cpal::Stream, Arc<Mutex<Vec<f32>>>, usize, u32)> = None;
            let mut vad = Vad::new();
            let mut pending: Vec<f32> = Vec::new();
            let mut last_audio = std::time::Instant::now();
            // The player wants the microphone open: a lost one is retried every second, without end.
            let mut want_open = false;
            let mut next_try = std::time::Instant::now();
            let mut injected: std::collections::VecDeque<f32> = std::collections::VecDeque::new();
            loop {
                // Commands are checked between 20 ms analysis passes.
                match rx.recv_timeout(Duration::from_millis(FRAME_MS as u64)) {
                    Ok(Command::Start(reply)) => {
                        if stream.is_some() {
                            let _ = reply.send(Ok(shared.lock().unwrap().device.clone()));
                            continue;
                        }
                        let hint = device.lock().unwrap().clone();
                        match open_stream_safe(hint.as_deref()) {
                            Ok((s, buf, ch, rate, name)) => {
                                vad = Vad::new();
                                pending.clear();
                                let mut st = shared.lock().unwrap();
                                st.active = true;
                                st.device = name.clone();
                                drop(st);
                                stream = Some((s, buf, ch, rate));
                                last_audio = std::time::Instant::now();
                                want_open = true;
                                crate::log!("ok-asr: listening on {name} ({rate} Hz, {ch} ch)");
                                let _ = reply.send(Ok(name));
                            }
                            Err(e) => {
                                crate::log!("ok-asr: microphone open failed: {e:#}");
                                // Keep trying in the background: the device may be plugged back in any moment.
                                want_open = true;
                                next_try = std::time::Instant::now() + Duration::from_secs(1);
                                let _ = reply.send(Err(e));
                            }
                        }
                    }
                    Ok(Command::Reopen(reply)) => {
                        if !want_open {
                            let _ = reply.send(Ok(String::new()));
                            continue;
                        }
                        // Dropped before the new one opens: two streams on one device can fail on some drivers.
                        stream = None;
                        pending.clear();
                        vad.reset();
                        let hint = device.lock().unwrap().clone();
                        match open_stream_safe(hint.as_deref()) {
                            Ok((s, buf, ch, rate, name)) => {
                                let mut st = shared.lock().unwrap();
                                st.active = true;
                                st.device = name.clone();
                                drop(st);
                                stream = Some((s, buf, ch, rate));
                                last_audio = std::time::Instant::now();
                                crate::log!("ok-asr: listening on {name} ({rate} Hz, {ch} ch)");
                                let _ = reply.send(Ok(name));
                            }
                            Err(e) => {
                                // Not fatal: the once-a-second retry keeps going with the same choice.
                                crate::log!("ok-asr: microphone open failed: {e:#}");
                                let mut st = shared.lock().unwrap();
                                st.active = false;
                                st.speaking = false;
                                drop(st);
                                next_try = std::time::Instant::now() + Duration::from_secs(1);
                                let _ = reply.send(Err(e));
                            }
                        }
                    }
                    Ok(Command::Inject(samples, from)) => {
                        if let Some((_, _, _, rate)) = stream.as_ref() {
                            let resampled = if from == *rate { samples } else { crate::audio::resample(&samples, from, *rate) };
                            crate::log!("ok-asr: injecting {:.2} s of test speech", resampled.len() as f32 / *rate as f32);
                            injected.extend(resampled);
                        }
                    }
                    Ok(Command::Stop) => {
                        if stream.is_some() {
                            crate::log!("ok-asr: listening stopped");
                        }
                        stream = None;
                        want_open = false;
                        let mut st = shared.lock().unwrap();
                        st.active = false;
                        st.speaking = false;
                    }
                    Err(std::sync::mpsc::RecvTimeoutError::Timeout) => {}
                    Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => return,
                }
                // Unplugged, taken over or reset (error callback, or no audio for 2 s): drop the dead stream.
                if stream.is_some()
                    && (crate::audio::STREAM_FAILED.load(std::sync::atomic::Ordering::SeqCst) || last_audio.elapsed() > Duration::from_secs(2))
                {
                    crate::log!("ok-asr: microphone lost; retrying every second until it is back");
                    stream = None;
                    pending.clear();
                    vad.reset();
                    next_try = std::time::Instant::now();
                    let mut st = shared.lock().unwrap();
                    st.active = false;
                    st.speaking = false;
                }
                // Running on the fallback (default) microphone while the chosen one was unplugged: switch back as
                // soon as it is plugged in again.
                if stream.is_some() && std::time::Instant::now() >= next_try {
                    next_try = std::time::Instant::now() + Duration::from_secs(2);
                    let hint = device.lock().unwrap().clone().unwrap_or_default().to_lowercase();
                    let current = shared.lock().unwrap().device.to_lowercase();
                    if !hint.is_empty() && !current.contains(&hint) && device_present(&hint) {
                        crate::log!("ok-asr: chosen microphone is back; switching to it");
                        stream = None;
                        pending.clear();
                        vad.reset();
                        next_try = std::time::Instant::now();
                    }
                }
                // Reopen without end, once a second (the hinted device again once it is plugged back in).
                if stream.is_none() && want_open && std::time::Instant::now() >= next_try {
                    next_try = std::time::Instant::now() + Duration::from_secs(1);
                    let hint = device.lock().unwrap().clone();
                    match open_stream_safe(hint.as_deref()) {
                        Ok((s, buf, ch, rate, name)) => {
                            vad = Vad::new();
                            pending.clear();
                            let mut st = shared.lock().unwrap();
                            st.active = true;
                            st.device = name.clone();
                            drop(st);
                            stream = Some((s, buf, ch, rate));
                            last_audio = std::time::Instant::now();
                            crate::log!("ok-asr: microphone back: {name} ({rate} Hz, {ch} ch)");
                        }
                        Err(e) => crate::log!("ok-asr: microphone still unavailable: {e:#}"),
                    }
                }
                let Some((_, buf, ch, rate)) = stream.as_ref() else { continue };
                let raw = std::mem::take(&mut *buf.lock().unwrap());
                if !raw.is_empty() {
                    last_audio = std::time::Instant::now();
                }
                let mut mono = downmix(&raw, *ch);
                for v in mono.iter_mut() {
                    match injected.pop_front() {
                        Some(x) => *v += x,
                        None => break,
                    }
                }
                pending.extend(mono);
                let frame = *rate as usize * FRAME_MS / 1000;
                let (muted, ducked) = { let st = shared.lock().unwrap(); (st.muted, st.ducked) };
                let mut level = 0.0f32;
                while pending.len() >= frame {
                    let chunk: Vec<f32> = pending.drain(..frame).collect();
                    level = level.max((chunk.iter().map(|v| v * v).sum::<f32>() / frame as f32).sqrt());
                    if muted {
                        vad.reset();
                        continue;
                    }
                    if let Some(samples) = vad.push_ducked(&chunk, *rate, ducked) {
                        on_utterance(Utterance { samples, rate: *rate });
                    }
                }
                let mut st = shared.lock().unwrap();
                st.speaking = vad.in_speech;
                st.level = level;
            }
        });
        Self { tx: Mutex::new(tx), status }
    }

    pub fn start(&self) -> Result<String> {
        // A new session starts unmuted: a mute left over from a game that quit mid-line would drop every utterance.
        self.set_muted(false);
        self.set_ducked(false);
        let (tx, rx) = channel();
        self.tx.lock().unwrap().send(Command::Start(tx)).map_err(|_| anyhow!("listener stopped"))?;
        rx.recv().map_err(|_| anyhow!("listener stopped"))?
    }

    /// Mixes a recording into the live microphone (development tests of hands-free mode).
    pub fn inject(&self, samples: Vec<f32>, rate: u32) {
        let _ = self.tx.lock().unwrap().send(Command::Inject(samples, rate));
    }

    pub fn stop(&self) {
        let _ = self.tx.lock().unwrap().send(Command::Stop);
    }

    /// Reopen the stream (after the device choice changed) if it is running.
    pub fn restart(&self) -> Result<()> {
        let (tx, rx) = channel();
        self.tx.lock().unwrap().send(Command::Reopen(tx)).map_err(|_| anyhow!("listener stopped"))?;
        rx.recv().map_err(|_| anyhow!("listener stopped"))?.map(|_| ())
    }

    pub fn set_muted(&self, muted: bool) {
        let mut st = self.status.lock().unwrap();
        if st.muted != muted {
            crate::log!("ok-asr: {}", if muted { "muted (game voice playing)" } else { "unmuted" });
        }
        st.muted = muted;
    }

    /// Barge-in listening while the game's voice plays (see DUCK_BOOST).
    pub fn set_ducked(&self, ducked: bool) {
        let mut st = self.status.lock().unwrap();
        if st.ducked != ducked {
            crate::log!("ok-asr: {}", if ducked { "ducked (game voice playing, barge-in only)" } else { "unducked" });
        }
        st.ducked = ducked;
    }

    pub fn status(&self) -> Status {
        self.status.lock().unwrap().clone()
    }
}
