//! Whisper-small encoder (Seed-VC's content features) in Rust + CUDA, f32: 16 kHz audio padded to 30 s -> log-mel
//! (WhisperFeatureExtractor, torch path) -> conv stem + 12 pre-LN transformer layers -> [1500][768].

use std::path::Path;

use anyhow::{Context, Result};
use rustfft::num_complex::Complex;
use rustfft::FftPlanner;

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

const D: usize = 768;
const HEADS: usize = 12;
const HD: usize = 64;
const N_FFT: usize = 400;
const HOP: usize = 160;
const SAMPLES: usize = 480_000;
const FRAMES: usize = 3000;
const MELS: usize = 80;

struct Lin {
    w: Buf,
    b: Buf,
    out: usize,
    inp: usize,
}

struct Layer {
    ln1: (Buf, Buf),
    qkv: Lin,
    out: Lin,
    ln2: (Buf, Buf),
    fc1: Lin,
    fc2: Lin,
}

pub struct Whisper {
    conv1: (Buf, Buf),
    conv2: (Buf, Buf),
    pos: Buf,
    layers: Vec<Layer>,
    ln: (Buf, Buf),
    filters: Vec<f64>, // [80][201] as stored in preprocessor_config.json
}

fn taps(w: &[f32], co: usize, ci: usize, k: usize) -> Vec<f32> {
    let mut r = vec![0f32; w.len()];
    for o in 0..co {
        for i in 0..ci {
            for j in 0..k {
                r[(j * co + o) * ci + i] = w[(o * ci + i) * k + j];
            }
        }
    }
    r
}

impl Whisper {
    /// `dir`: the Hugging Face snapshot (model.safetensors, preprocessor_config.json).
    pub fn load(gpu: &Gpu, dir: &Path) -> Result<Self> {
        let st = Shards::open_file(&dir.join("model.safetensors"))?;
        let pre: serde_json::Value = serde_json::from_slice(&std::fs::read(dir.join("preprocessor_config.json"))?)?;
        let filters: Vec<f64> = pre["mel_filters"].as_array().context("mel_filters")?.iter()
            .flat_map(|r| r.as_array().unwrap().iter().map(|v| v.as_f64().unwrap() as f32 as f64)).collect();
        let e = "model.encoder";
        let pair = |p: &str| -> Result<(Buf, Buf)> {
            Ok((gpu.upload(&st.f32(&format!("{p}.weight"))?)?, gpu.upload(&st.f32(&format!("{p}.bias"))?)?))
        };
        let lin = |p: &str| -> Result<Lin> {
            let s = st.shape(&format!("{p}.weight"))?;
            Ok(Lin { w: gpu.upload(&st.f32(&format!("{p}.weight"))?)?, b: gpu.upload(&st.f32(&format!("{p}.bias"))?)?, out: s[0], inp: s[1] })
        };
        let mut layers = Vec::new();
        for i in 0..12 {
            let p = format!("{e}.layers.{i}");
            let mut w = st.f32(&format!("{p}.self_attn.q_proj.weight"))?;
            w.extend(st.f32(&format!("{p}.self_attn.k_proj.weight"))?);
            w.extend(st.f32(&format!("{p}.self_attn.v_proj.weight"))?);
            let mut b = st.f32(&format!("{p}.self_attn.q_proj.bias"))?;
            b.extend(std::iter::repeat_n(0f32, D));
            b.extend(st.f32(&format!("{p}.self_attn.v_proj.bias"))?);
            layers.push(Layer {
                ln1: pair(&format!("{p}.self_attn_layer_norm"))?,
                qkv: Lin { w: gpu.upload(&w)?, b: gpu.upload(&b)?, out: 3 * D, inp: D },
                out: lin(&format!("{p}.self_attn.out_proj"))?,
                ln2: pair(&format!("{p}.final_layer_norm"))?,
                fc1: lin(&format!("{p}.fc1"))?,
                fc2: lin(&format!("{p}.fc2"))?,
            });
        }
        Ok(Self {
            conv1: (gpu.upload(&taps(&st.f32(&format!("{e}.conv1.weight"))?, D, MELS, 3))?, gpu.upload(&st.f32(&format!("{e}.conv1.bias"))?)?),
            conv2: (gpu.upload(&taps(&st.f32(&format!("{e}.conv2.weight"))?, D, D, 3))?, gpu.upload(&st.f32(&format!("{e}.conv2.bias"))?)?),
            pos: gpu.upload(&st.f32(&format!("{e}.embed_positions.weight"))?)?,
            layers,
            ln: pair(&format!("{e}.layer_norm"))?,
            filters,
        })
    }

    /// WhisperFeatureExtractor (torch path): [80][3000].
    pub fn features(&self, wave: &[f32]) -> Vec<f32> {
        let mut x: Vec<f64> = wave.iter().take(SAMPLES).map(|v| *v as f64).collect();
        x.resize(SAMPLES, 0.0);
        let xp = super::dsp::reflect_pad(&x, N_FFT / 2, N_FFT / 2);
        let w = super::dsp::hann(N_FFT);
        let mut planner = FftPlanner::<f64>::new();
        let fft = planner.plan_fft_forward(N_FFT);
        let bins = N_FFT / 2 + 1;
        let mut mel = vec![0f64; MELS * FRAMES];
        let mut buf = vec![Complex::new(0.0, 0.0); N_FFT];
        for t in 0..FRAMES {
            for n in 0..N_FFT {
                buf[n] = Complex::new(xp[t * HOP + n] * w[n], 0.0);
            }
            fft.process(&mut buf);
            for m in 0..MELS {
                let mut s = 0.0;
                for k in 0..bins {
                    s += self.filters[m * bins + k] * buf[k].norm_sqr();
                }
                mel[m * FRAMES + t] = s.max(1e-10).log10();
            }
        }
        let mx = mel.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
        mel.iter().map(|v| ((v.max(mx - 8.0) + 4.0) / 4.0) as f32).collect()
    }

    fn linear(&self, gpu: &Gpu, l: &Lin, x: u64, rows: usize) -> Result<Buf> {
        let y = gpu.alloc(rows * l.out * 4)?;
        gpu.launch1("fill_rows_f32", rows * l.out, &[Arg::P(y.ptr), Arg::P(l.b.ptr), Arg::I(l.out as i32), Arg::I(rows as i32), Arg::I(1)])?;
        gpu.linear_f32(y.ptr, x, l.w.ptr, rows, l.inp, l.out, 1.0)?;
        Ok(y)
    }

    fn conv(&self, gpu: &Gpu, w: &(Buf, Buf), x: u64, ci: usize, t: usize, stride: usize) -> Result<(Buf, usize)> {
        let to = (t + 2 - 3) / stride + 1;
        let y = gpu.alloc(D * to * 4)?;
        gpu.launch1("fill_rows_f32", D * to, &[Arg::P(y.ptr), Arg::P(w.1.ptr), Arg::I(D as i32), Arg::I(to as i32), Arg::I(0)])?;
        let g = gpu.alloc(ci * to * 4)?;
        for j in 0..3 {
            gpu.launch1("gather_shift_f32", ci * to, &[Arg::P(g.ptr), Arg::P(x), Arg::I(ci as i32), Arg::I(1), Arg::I(to as i32),
                Arg::I(1), Arg::I(t as i32), Arg::I(1), Arg::I(0), Arg::I(stride as i32), Arg::I(j as i32 - 1)])?;
            gpu.chan_matmul_f32(y.ptr, ptr_f32(w.0.ptr, j * D * ci), g.ptr, D, ci, to, to, 1.0)?;
        }
        gpu.launch1("gelu_f32", D * to, &[Arg::P(y.ptr), Arg::L((D * to) as i64)])?;
        Ok((y, to))
    }

    fn layernorm(&self, gpu: &Gpu, x: u64, rows: usize, n: &(Buf, Buf)) -> Result<Buf> {
        let out = gpu.alloc(rows * D * 4)?;
        gpu.launch("layernorm_affine_rows_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(x), Arg::P(n.0.ptr), Arg::P(n.1.ptr),
            Arg::I(D as i32), Arg::F(1e-5)])?;
        Ok(out)
    }

    /// 16 kHz audio (at most 30 s) -> the first len / 320 + 1 encoder frames, [n][768].
    pub fn encode(&self, gpu: &Gpu, wave: &[f32]) -> Result<(Vec<f32>, usize)> {
        let f = gpu.upload(&self.features(wave))?;
        let (h1, t1) = self.conv(gpu, &self.conv1, f.ptr, MELS, FRAMES, 1)?;
        let (h2, t) = self.conv(gpu, &self.conv2, h1.ptr, D, t1, 2)?;
        drop(h1);
        let x = gpu.alloc(t * D * 4)?;
        gpu.launch("transpose_f32", ((t as u32).div_ceil(32), (D as u32).div_ceil(32), 1), (32, 8, 1), 0,
            &[Arg::P(x.ptr), Arg::P(h2.ptr), Arg::I(D as i32), Arg::I(t as i32)])?;
        gpu.launch1("add_f32", t * D, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(self.pos.ptr), Arg::L((t * D) as i64)])?;
        let scores = gpu.alloc(t * t * 4)?;
        let attn = gpu.alloc(t * D * 4)?;
        for l in &self.layers {
            let h = self.layernorm(gpu, x.ptr, t, &l.ln1)?;
            let qkv = self.linear(gpu, &l.qkv, h.ptr, t)?;
            for hh in 0..HEADS {
                gpu.sgemm(true, false, t, t, HD, ptr_f32(qkv.ptr, D + hh * HD), 3 * D, ptr_f32(qkv.ptr, hh * HD), 3 * D, 0.0, scores.ptr, t)?;
                gpu.launch("softmax_rows_f32", (t as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(scores.ptr), Arg::I(t as i32), Arg::F(1.0 / (HD as f32).sqrt())])?;
                gpu.sgemm(false, false, HD, t, t, ptr_f32(qkv.ptr, 2 * D + hh * HD), 3 * D, scores.ptr, t, 0.0, ptr_f32(attn.ptr, hh * HD), D)?;
            }
            let o = self.linear(gpu, &l.out, attn.ptr, t)?;
            gpu.launch1("add_f32", t * D, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(o.ptr), Arg::L((t * D) as i64)])?;
            let h = self.layernorm(gpu, x.ptr, t, &l.ln2)?;
            let a = self.linear(gpu, &l.fc1, h.ptr, t)?;
            gpu.launch1("gelu_f32", t * l.fc1.out, &[Arg::P(a.ptr), Arg::L((t * l.fc1.out) as i64)])?;
            let o = self.linear(gpu, &l.fc2, a.ptr, t)?;
            gpu.launch1("add_f32", t * D, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(o.ptr), Arg::L((t * D) as i64)])?;
        }
        let y = self.layernorm(gpu, x.ptr, t, &self.ln)?;
        let n = (wave.len() / 320 + 1).min(t);
        Ok((gpu.read(y.ptr, n * D)?, n))
    }
}
