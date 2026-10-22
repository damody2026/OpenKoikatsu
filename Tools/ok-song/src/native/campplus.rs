//! CAM++ speaker embedding (funasr campplus_cn_common, Seed-VC's style vector) in Rust + CUDA, f32, with the
//! torchaudio kaldi fbank front end (80 bins, povey window, pre-emphasis 0.97, 512-point FFT, no dither).
//! BatchNorm runs as a per-channel affine with the running statistics.

use std::path::Path;

use anyhow::Result;
use rustfft::num_complex::Complex;
use rustfft::FftPlanner;

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

/// torchaudio.compliance.kaldi.fbank(wave_16k, num_mel_bins=80, dither=0), then minus the per-bin mean: [frames][80].
pub fn fbank(wave: &[f32]) -> (Vec<f32>, usize) {
    let (flen, shift, nfft, bins) = (400usize, 160usize, 512usize, 80usize);
    if wave.len() < flen {
        return (Vec::new(), 0);
    }
    let frames = 1 + (wave.len() - flen) / shift;
    let window: Vec<f64> = (0..flen).map(|n| (0.5 - 0.5 * (2.0 * std::f64::consts::PI * n as f64 / (flen - 1) as f64).cos()).powf(0.85)).collect();
    let mel = |f: f64| 1127.0 * (1.0 + f / 700.0).ln();
    let (lo, hi) = (mel(20.0), mel(8000.0));
    let delta = (hi - lo) / (bins + 1) as f64;
    let width = 16000.0 / nfft as f64;
    let bank: Vec<Vec<f64>> = (0..bins)
        .map(|b| {
            let (l, c, r) = (lo + b as f64 * delta, lo + (b + 1) as f64 * delta, lo + (b + 2) as f64 * delta);
            (0..nfft / 2 + 1).map(|k| {
                if k == nfft / 2 {
                    return 0.0;
                }
                let m = mel(width * k as f64);
                let up = (m - l) / (c - l);
                let down = (r - m) / (r - c);
                up.min(down).max(0.0)
            }).collect()
        })
        .collect();
    let mut planner = FftPlanner::<f64>::new();
    let fft = planner.plan_fft_forward(nfft);
    let eps = f32::EPSILON as f64;
    let mut out = vec![0f64; frames * bins];
    let mut buf = vec![Complex::new(0.0, 0.0); nfft];
    for t in 0..frames {
        let mut x: Vec<f64> = wave[t * shift..t * shift + flen].iter().map(|v| *v as f64).collect();
        let mean = x.iter().sum::<f64>() / flen as f64;
        x.iter_mut().for_each(|v| *v -= mean);
        for j in (1..flen).rev() {
            x[j] -= 0.97 * x[j - 1];
        }
        x[0] -= 0.97 * x[0];
        buf.iter_mut().for_each(|c| *c = Complex::new(0.0, 0.0));
        for j in 0..flen {
            buf[j] = Complex::new(x[j] * window[j], 0.0);
        }
        fft.process(&mut buf);
        for (b, row) in bank.iter().enumerate() {
            let e: f64 = row.iter().zip(&buf).map(|(w, c)| w * c.norm_sqr()).sum();
            out[t * bins + b] = e.max(eps).ln();
        }
    }
    for b in 0..bins {
        let m = (0..frames).map(|t| out[t * bins + b]).sum::<f64>() / frames as f64;
        for t in 0..frames {
            out[t * bins + b] -= m;
        }
    }
    (out.into_iter().map(|v| v as f32).collect(), frames)
}

struct Conv {
    w: Buf, // [kf * kt][co][ci]
    b: Option<Buf>,
    co: usize,
    ci: usize,
    kf: usize,
    kt: usize,
}

/// BatchNorm (eval) as y = x * s + t.
struct Bn {
    s: Buf,
    t: Buf,
}

struct Res {
    conv1: Conv,
    bn1: Bn,
    conv2: Conv,
    bn2: Bn,
    short: Option<(Conv, Bn)>,
}

struct Dense {
    bn1: Bn,
    lin1: Conv,
    bn2: Bn,
    local: Conv,
    l1: Conv,
    l2: Conv,
    dil: usize,
}

pub struct CamPlus {
    conv1: Conv,
    bn1: Bn,
    res: Vec<Res>,
    conv2: Conv,
    bn2: Bn,
    tdnn: Conv,
    tdnn_bn: Bn,
    blocks: Vec<(Vec<Dense>, Bn, Conv)>,
    out_bn: Bn,
    dense: Vec<f32>, // [192][1024]
    dense_bn: (Vec<f32>, Vec<f32>),
    zero: Buf,
}

fn conv(gpu: &Gpu, st: &Shards, p: &str, bias: bool) -> Result<Conv> {
    let w = st.f32(&format!("{p}.weight"))?;
    let s = st.shape(&format!("{p}.weight"))?;
    let (co, ci, kf, kt) = match s.as_slice() {
        [co, ci, k] => (*co, *ci, 1, *k),
        [co, ci, kf, kt] => (*co, *ci, *kf, *kt),
        _ => anyhow::bail!("{p}: shape {s:?}"),
    };
    let taps = kf * kt;
    let mut r = vec![0f32; w.len()];
    for o in 0..co {
        for i in 0..ci {
            for j in 0..taps {
                r[(j * co + o) * ci + i] = w[(o * ci + i) * taps + j];
            }
        }
    }
    let b = if bias { Some(gpu.upload(&st.f32(&format!("{p}.bias"))?)?) } else { None };
    Ok(Conv { w: gpu.upload(&r)?, b, co, ci, kf, kt })
}

fn bn_host(st: &Shards, p: &str, affine: bool) -> Result<(Vec<f32>, Vec<f32>)> {
    let mean = st.f32(&format!("{p}.running_mean"))?;
    let var = st.f32(&format!("{p}.running_var"))?;
    let n = mean.len();
    let (g, b) = if affine { (st.f32(&format!("{p}.weight"))?, st.f32(&format!("{p}.bias"))?) } else { (vec![1.0; n], vec![0.0; n]) };
    let s: Vec<f32> = (0..n).map(|i| g[i] / (var[i] + 1e-5).sqrt()).collect();
    let t: Vec<f32> = (0..n).map(|i| b[i] - mean[i] * s[i]).collect();
    Ok((s, t))
}

fn bn(gpu: &Gpu, st: &Shards, p: &str) -> Result<Bn> {
    let (s, t) = bn_host(st, p, true)?;
    Ok(Bn { s: gpu.upload(&s)?, t: gpu.upload(&t)? })
}

impl CamPlus {
    pub fn load(gpu: &Gpu, path: &Path) -> Result<Self> {
        let st = Shards::open_file(path)?;
        let mut res = Vec::new();
        for l in ["layer1", "layer2"] {
            for i in 0..2 {
                let p = format!("head.{l}.{i}");
                let short = if st.has(&format!("{p}.shortcut.0.weight")) {
                    Some((conv(gpu, &st, &format!("{p}.shortcut.0"), false)?, bn(gpu, &st, &format!("{p}.shortcut.1"))?))
                } else {
                    None
                };
                res.push(Res { conv1: conv(gpu, &st, &format!("{p}.conv1"), false)?, bn1: bn(gpu, &st, &format!("{p}.bn1"))?,
                               conv2: conv(gpu, &st, &format!("{p}.conv2"), false)?, bn2: bn(gpu, &st, &format!("{p}.bn2"))?, short });
            }
        }
        let mut blocks = Vec::new();
        for (bi, (n, dil)) in [(12usize, 1usize), (24, 2), (16, 2)].iter().enumerate() {
            let mut layers = Vec::new();
            for i in 0..*n {
                let p = format!("xvector.block{}.tdnnd{}", bi + 1, i + 1);
                layers.push(Dense {
                    bn1: bn(gpu, &st, &format!("{p}.nonlinear1.batchnorm"))?,
                    lin1: conv(gpu, &st, &format!("{p}.linear1"), false)?,
                    bn2: bn(gpu, &st, &format!("{p}.nonlinear2.batchnorm"))?,
                    local: conv(gpu, &st, &format!("{p}.cam_layer.linear_local"), false)?,
                    l1: conv(gpu, &st, &format!("{p}.cam_layer.linear1"), true)?,
                    l2: conv(gpu, &st, &format!("{p}.cam_layer.linear2"), true)?,
                    dil: *dil,
                });
            }
            let t = format!("xvector.transit{}", bi + 1);
            blocks.push((layers, bn(gpu, &st, &format!("{t}.nonlinear.batchnorm"))?, conv(gpu, &st, &format!("{t}.linear"), false)?));
        }
        let dense_key = if st.has("xvector.dense.linear.weight") { "xvector.dense" } else { "dense" };
        Ok(Self {
            conv1: conv(gpu, &st, "head.conv1", false)?,
            bn1: bn(gpu, &st, "head.bn1")?,
            res,
            conv2: conv(gpu, &st, "head.conv2", false)?,
            bn2: bn(gpu, &st, "head.bn2")?,
            tdnn: conv(gpu, &st, "xvector.tdnn.linear", false)?,
            tdnn_bn: bn(gpu, &st, "xvector.tdnn.nonlinear.batchnorm")?,
            blocks,
            out_bn: bn(gpu, &st, "xvector.out_nonlinear.batchnorm")?,
            dense: st.f32(&format!("{dense_key}.linear.weight"))?,
            dense_bn: bn_host(&st, &format!("{dense_key}.nonlinear.batchnorm"), false)?,
            zero: gpu.zeros(2048 * 4)?,
        })
    }

    /// Conv over [ci][f][t] (taps along F with stride sf / pad pf, along T with stride st / pad pt / dilation), into
    /// `out` (which may be a row offset of a larger buffer).
    #[allow(clippy::too_many_arguments)]
    fn conv(&self, gpu: &Gpu, c: &Conv, x: u64, f: usize, t: usize, sf: usize, pf: usize, st: usize, pt: usize, dil: usize, out: Option<u64>) -> Result<(Option<Buf>, usize, usize)> {
        let fo = (f + 2 * pf - c.kf) / sf + 1;
        let to = (t + 2 * pt - dil * (c.kt - 1) - 1) / st + 1;
        let n = fo * to;
        let own = if out.is_none() { Some(gpu.alloc(c.co * n * 4)?) } else { None };
        let y = out.unwrap_or_else(|| own.as_ref().unwrap().ptr);
        match &c.b {
            Some(b) => gpu.launch1("fill_rows_f32", c.co * n, &[Arg::P(y), Arg::P(b.ptr), Arg::I(c.co as i32), Arg::I(n as i32), Arg::I(0)])?,
            None => gpu.launch1("fill_rows_f32", c.co * n, &[Arg::P(y), Arg::P(self.zero.ptr), Arg::I(c.co as i32), Arg::I(n as i32), Arg::I(0)])?,
        }
        if c.kf == 1 && c.kt == 1 && sf == 1 && st == 1 {
            gpu.chan_matmul_f32(y, c.w.ptr, x, c.co, c.ci, n, n, 1.0)?;
            return Ok((own, fo, to));
        }
        let g = gpu.alloc(c.ci * n * 4)?;
        for a in 0..c.kf {
            for b in 0..c.kt {
                gpu.launch1("gather_shift_f32", c.ci * n, &[Arg::P(g.ptr), Arg::P(x), Arg::I(c.ci as i32), Arg::I(fo as i32), Arg::I(to as i32),
                    Arg::I(f as i32), Arg::I(t as i32), Arg::I(sf as i32), Arg::I(a as i32 - pf as i32), Arg::I(st as i32), Arg::I((b * dil) as i32 - pt as i32)])?;
                gpu.chan_matmul_f32(y, ptr_f32(c.w.ptr, (a * c.kt + b) * c.co * c.ci), g.ptr, c.co, c.ci, n, n, 1.0)?;
            }
        }
        Ok((own, fo, to))
    }

    fn affine(&self, gpu: &Gpu, out: u64, x: u64, b: &Bn, c: usize, n: usize, relu: bool) -> Result<()> {
        gpu.launch1("affine_act_f32", c * n, &[Arg::P(out), Arg::P(x), Arg::P(b.s.ptr), Arg::P(b.t.ptr), Arg::I(c as i32), Arg::L(n as i64), Arg::I(relu as i32)])
    }

    /// fbank [frames][80] -> embedding [192].
    pub fn embed(&self, gpu: &Gpu, feat: &[f32], frames: usize) -> Result<Vec<f32>> {
        // [1][80 freq][T]
        let mut x0 = vec![0f32; 80 * frames];
        for t in 0..frames {
            for f in 0..80 {
                x0[f * frames + t] = feat[t * 80 + f];
            }
        }
        let x0 = gpu.upload(&x0)?;
        let t = frames;
        let (y, mut f, _) = self.conv(gpu, &self.conv1, x0.ptr, 80, t, 1, 1, 1, 1, 1, None)?;
        let mut x = y.unwrap();
        self.affine(gpu, x.ptr, x.ptr, &self.bn1, 32, f * t, true)?;
        for r in &self.res {
            let stride = if r.short.is_some() { 2 } else { 1 };
            let (h, fo, _) = self.conv(gpu, &r.conv1, x.ptr, f, t, stride, 1, 1, 1, 1, None)?;
            let h = h.unwrap();
            self.affine(gpu, h.ptr, h.ptr, &r.bn1, 32, fo * t, true)?;
            let (h2, _, _) = self.conv(gpu, &r.conv2, h.ptr, fo, t, 1, 1, 1, 1, 1, None)?;
            let h2 = h2.unwrap();
            self.affine(gpu, h2.ptr, h2.ptr, &r.bn2, 32, fo * t, false)?;
            let sc = match &r.short {
                Some((c, b)) => {
                    let (s, _, _) = self.conv(gpu, c, x.ptr, f, t, 2, 0, 1, 0, 1, None)?;
                    let s = s.unwrap();
                    self.affine(gpu, s.ptr, s.ptr, b, 32, fo * t, false)?;
                    s
                }
                None => x,
            };
            gpu.launch1("add_f32", 32 * fo * t, &[Arg::P(h2.ptr), Arg::P(h2.ptr), Arg::P(sc.ptr), Arg::L((32 * fo * t) as i64)])?;
            gpu.launch1("affine_act_f32", 32 * fo * t, &[Arg::P(h2.ptr), Arg::P(h2.ptr), Arg::P(self.ones(gpu, 32)?.ptr), Arg::P(gpu.zeros(32 * 4)?.ptr),
                Arg::I(32), Arg::L((fo * t) as i64), Arg::I(1)])?;
            x = h2;
            f = fo;
        }
        let (y, fo, _) = self.conv(gpu, &self.conv2, x.ptr, f, t, 2, 1, 1, 1, 1, None)?;
        let y = y.unwrap();
        self.affine(gpu, y.ptr, y.ptr, &self.bn2, 32, fo * t, true)?;
        // [32][fo][T] is already [(c * fo + f)][T] = [320][T]
        let (h, _, t2) = self.conv(gpu, &self.tdnn, y.ptr, 1, t, 1, 0, 2, 2, 1, None)?;
        let h = h.unwrap();
        self.affine(gpu, h.ptr, h.ptr, &self.tdnn_bn, 128, t2, true)?;
        let t = t2;
        let mut x = h;
        let mut ch = 128;
        for (layers, tbn, tlin) in &self.blocks {
            let total = ch + layers.len() * 32;
            let cat = gpu.alloc(total * t * 4)?;
            gpu.copy(cat.ptr, x.ptr, ch * t * 4)?;
            for l in layers {
                let a = gpu.alloc(ch * t * 4)?;
                self.affine(gpu, a.ptr, cat.ptr, &l.bn1, ch, t, true)?;
                let (b, _, _) = self.conv(gpu, &l.lin1, a.ptr, 1, t, 1, 0, 1, 0, 1, None)?;
                let b = b.unwrap();
                self.affine(gpu, b.ptr, b.ptr, &l.bn2, 128, t, true)?;
                // CAM layer
                let out = ptr_f32(cat.ptr, ch * t);
                self.conv(gpu, &l.local, b.ptr, 1, t, 1, 0, 1, l.dil, l.dil, Some(out))?;
                let ctx = gpu.alloc(128 * t * 4)?;
                gpu.launch("cam_context_f32", (128, 1, 1), (256, 1, 1), 0, &[Arg::P(ctx.ptr), Arg::P(b.ptr), Arg::I(t as i32), Arg::I(100)])?;
                let (c1, _, _) = self.conv(gpu, &l.l1, ctx.ptr, 1, t, 1, 0, 1, 0, 1, None)?;
                let c1 = c1.unwrap();
                gpu.launch1("affine_act_f32", 64 * t, &[Arg::P(c1.ptr), Arg::P(c1.ptr), Arg::P(self.ones(gpu, 64)?.ptr), Arg::P(gpu.zeros(64 * 4)?.ptr),
                    Arg::I(64), Arg::L(t as i64), Arg::I(1)])?;
                let (m, _, _) = self.conv(gpu, &l.l2, c1.ptr, 1, t, 1, 0, 1, 0, 1, None)?;
                let m = m.unwrap();
                gpu.launch1("sigmoid_mul_f32", 32 * t, &[Arg::P(out), Arg::P(m.ptr), Arg::L((32 * t) as i64)])?;
                ch += 32;
            }
            let a = gpu.alloc(ch * t * 4)?;
            self.affine(gpu, a.ptr, cat.ptr, tbn, ch, t, true)?;
            let (y, _, _) = self.conv(gpu, tlin, a.ptr, 1, t, 1, 0, 1, 0, 1, None)?;
            x = y.unwrap();
            ch /= 2;
        }
        self.affine(gpu, x.ptr, x.ptr, &self.out_bn, ch, t, true)?;
        let h: Vec<f32> = gpu.read(x.ptr, ch * t)?;
        // statistics pooling (mean, unbiased std), then the 1x1 dense layer and a non-affine BatchNorm
        let mut stats = vec![0f64; 2 * ch];
        for c in 0..ch {
            let r = &h[c * t..(c + 1) * t];
            let m = r.iter().map(|v| *v as f64).sum::<f64>() / t as f64;
            let v = r.iter().map(|x| (*x as f64 - m).powi(2)).sum::<f64>() / (t as f64 - 1.0);
            stats[c] = m;
            stats[ch + c] = v.sqrt();
        }
        let out: Vec<f32> = (0..192).map(|o| {
            let s: f64 = (0..2 * ch).map(|i| self.dense[o * 2 * ch + i] as f64 * stats[i]).sum();
            (s as f32) * self.dense_bn.0[o] + self.dense_bn.1[o]
        }).collect();
        Ok(out)
    }

    fn ones(&self, gpu: &Gpu, n: usize) -> Result<Buf> {
        gpu.upload(&vec![1f32; n])
    }
}
