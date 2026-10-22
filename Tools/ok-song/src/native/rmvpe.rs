//! RMVPE pitch estimator (lj1995 rmvpe.pt, Seed-VC's F0) in Rust + CUDA, f32: 16 kHz audio -> 128-band HTK log-mel
//! (n_fft 1024, hop 160, 30 Hz - 8 kHz) -> deep U-Net -> BiGRU -> 360 pitch bins -> local-average cents -> Hz.

use std::path::Path;

use anyhow::Result;

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

struct Conv {
    w: Buf, // [kh * kw][co][ci]
    b: Option<Buf>,
    co: usize,
    ci: usize,
    k: usize,
}

struct Bn {
    s: Buf,
    t: Buf,
}

struct Block {
    c1: Conv,
    b1: Bn,
    c2: Conv,
    b2: Bn,
    short: Option<Conv>,
}

struct Dec {
    tconv: Conv, // [(kh * 3 + kw) * co + c][ci]
    bn: Bn,
    blocks: Vec<Block>,
}

pub struct Rmvpe {
    in_bn: (f32, f32),
    enc: Vec<Vec<Block>>,
    inter: Vec<Vec<Block>>,
    dec: Vec<Dec>,
    cnn: Conv,
    gru: [(Buf, Buf, Buf, Buf); 2], // w_ih, b_ih, w_hh, b_hh per direction
    fc: (Buf, Buf),
    zero: Buf,
    bank: Vec<Vec<f64>>,
}

fn conv(gpu: &Gpu, st: &Shards, p: &str, bias: bool) -> Result<Conv> {
    let w = st.f32(&format!("{p}.weight"))?;
    let s = st.shape(&format!("{p}.weight"))?;
    let (co, ci, k) = (s[0], s[1], s[2]);
    let taps = k * k;
    let mut r = vec![0f32; w.len()];
    for o in 0..co {
        for i in 0..ci {
            for j in 0..taps {
                r[(j * co + o) * ci + i] = w[(o * ci + i) * taps + j];
            }
        }
    }
    let b = if bias { Some(gpu.upload(&st.f32(&format!("{p}.bias"))?)?) } else { None };
    Ok(Conv { w: gpu.upload(&r)?, b, co, ci, k })
}

fn bn(gpu: &Gpu, st: &Shards, p: &str) -> Result<Bn> {
    let (g, b) = (st.f32(&format!("{p}.weight"))?, st.f32(&format!("{p}.bias"))?);
    let (m, v) = (st.f32(&format!("{p}.running_mean"))?, st.f32(&format!("{p}.running_var"))?);
    let s: Vec<f32> = (0..g.len()).map(|i| g[i] / (v[i] + 1e-5).sqrt()).collect();
    let t: Vec<f32> = (0..g.len()).map(|i| b[i] - m[i] * s[i]).collect();
    Ok(Bn { s: gpu.upload(&s)?, t: gpu.upload(&t)? })
}

fn block(gpu: &Gpu, st: &Shards, p: &str) -> Result<Block> {
    let short = if st.has(&format!("{p}.shortcut.weight")) { Some(conv(gpu, st, &format!("{p}.shortcut"), true)?) } else { None };
    Ok(Block { c1: conv(gpu, st, &format!("{p}.conv.0"), false)?, b1: bn(gpu, st, &format!("{p}.conv.1"))?,
               c2: conv(gpu, st, &format!("{p}.conv.3"), false)?, b2: bn(gpu, st, &format!("{p}.conv.4"))?, short })
}

impl Rmvpe {
    pub fn load(gpu: &Gpu, path: &Path) -> Result<Self> {
        let st = Shards::open_file(path)?;
        let blocks = |p: &str| -> Result<Vec<Block>> { (0..4).map(|i| block(gpu, &st, &format!("{p}.conv.{i}"))).collect() };
        let enc = (0..5).map(|i| blocks(&format!("unet.encoder.layers.{i}"))).collect::<Result<Vec<_>>>()?;
        let inter = (0..4).map(|i| blocks(&format!("unet.intermediate.layers.{i}"))).collect::<Result<Vec<_>>>()?;
        let mut dec = Vec::new();
        for i in 0..5 {
            let p = format!("unet.decoder.layers.{i}");
            let w = st.f32(&format!("{p}.conv1.0.weight"))?;
            let s = st.shape(&format!("{p}.conv1.0.weight"))?;
            let (ci, co) = (s[0], s[1]);
            // ConvTranspose2d weight [ci][co][3][3] -> [(kh * 3 + kw) * co + c][ci]
            let mut r = vec![0f32; w.len()];
            for i2 in 0..ci {
                for o in 0..co {
                    for j in 0..9 {
                        r[(j * co + o) * ci + i2] = w[(i2 * co + o) * 9 + j];
                    }
                }
            }
            dec.push(Dec { tconv: Conv { w: gpu.upload(&r)?, b: None, co, ci, k: 3 }, bn: bn(gpu, &st, &format!("{p}.conv1.1"))?,
                           blocks: (0..4).map(|k| block(gpu, &st, &format!("{p}.conv2.{k}"))).collect::<Result<Vec<_>>>()? });
        }
        let (g, b) = (st.f32("unet.encoder.bn.weight")?[0], st.f32("unet.encoder.bn.bias")?[0]);
        let (m, v) = (st.f32("unet.encoder.bn.running_mean")?[0], st.f32("unet.encoder.bn.running_var")?[0]);
        let s = g / (v + 1e-5).sqrt();
        let dir = |sfx: &str| -> Result<(Buf, Buf, Buf, Buf)> {
            Ok((gpu.upload(&st.f32(&format!("fc.0.gru.weight_ih_l0{sfx}"))?)?, gpu.upload(&st.f32(&format!("fc.0.gru.bias_ih_l0{sfx}"))?)?,
                gpu.upload(&st.f32(&format!("fc.0.gru.weight_hh_l0{sfx}"))?)?, gpu.upload(&st.f32(&format!("fc.0.gru.bias_hh_l0{sfx}"))?)?))
        };
        Ok(Self {
            in_bn: (s, b - m * s),
            enc,
            inter,
            dec,
            cnn: conv(gpu, &st, "cnn", true)?,
            gru: [dir("")?, dir("_reverse")?],
            fc: (gpu.upload(&st.f32("fc.1.weight")?)?, gpu.upload(&st.f32("fc.1.bias")?)?),
            zero: gpu.zeros(1024 * 4)?,
            bank: super::dsp::mel_bank(16000.0, 1024, 128, 30.0, 8000.0, true, true),
        })
    }

    /// log-mel [128][frames] (center = true).
    pub fn mel(&self, wave: &[f32]) -> (Vec<f32>, usize) {
        let x: Vec<f64> = wave.iter().map(|v| *v as f64).collect();
        let xp = super::dsp::reflect_pad(&x, 512, 512);
        let mags = super::dsp::stft_mag(&xp, 1024, 160, &super::dsp::hann(1024), false, 0.0);
        let frames = mags.len();
        let mut out = vec![0f32; 128 * frames];
        for (t, m) in mags.iter().enumerate() {
            for (k, row) in self.bank.iter().enumerate() {
                let s: f64 = row.iter().zip(m).map(|(a, b)| a * b).sum();
                out[k * frames + t] = (s as f32).max(1e-5).ln();
            }
        }
        (out, frames)
    }

    /// 3x3 "same" conv over [ci][h][w].
    fn conv(&self, gpu: &Gpu, c: &Conv, x: u64, h: usize, w: usize) -> Result<Buf> {
        let n = h * w;
        let y = gpu.alloc(c.co * n * 4)?;
        let b = c.b.as_ref().map(|b| b.ptr).unwrap_or(self.zero.ptr);
        gpu.launch1("fill_rows_f32", c.co * n, &[Arg::P(y.ptr), Arg::P(b), Arg::I(c.co as i32), Arg::I(n as i32), Arg::I(0)])?;
        if c.k == 1 {
            gpu.chan_matmul_f32(y.ptr, c.w.ptr, x, c.co, c.ci, n, n, 1.0)?;
            return Ok(y);
        }
        let g = gpu.alloc(c.ci * n * 4)?;
        for a in 0..3 {
            for bb in 0..3 {
                gpu.launch1("gather_shift_f32", c.ci * n, &[Arg::P(g.ptr), Arg::P(x), Arg::I(c.ci as i32), Arg::I(h as i32), Arg::I(w as i32),
                    Arg::I(h as i32), Arg::I(w as i32), Arg::I(1), Arg::I(a as i32 - 1), Arg::I(1), Arg::I(bb as i32 - 1)])?;
                gpu.chan_matmul_f32(y.ptr, ptr_f32(c.w.ptr, (a * 3 + bb) * c.co * c.ci), g.ptr, c.co, c.ci, n, n, 1.0)?;
            }
        }
        Ok(y)
    }

    fn affine(&self, gpu: &Gpu, x: &Buf, b: &Bn, c: usize, n: usize, relu: bool) -> Result<()> {
        gpu.launch1("affine_act_f32", c * n, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(b.s.ptr), Arg::P(b.t.ptr), Arg::I(c as i32), Arg::L(n as i64), Arg::I(relu as i32)])
    }

    fn res(&self, gpu: &Gpu, b: &Block, x: Buf, h: usize, w: usize) -> Result<Buf> {
        let y = self.conv(gpu, &b.c1, x.ptr, h, w)?;
        self.affine(gpu, &y, &b.b1, b.c1.co, h * w, true)?;
        let y2 = self.conv(gpu, &b.c2, y.ptr, h, w)?;
        self.affine(gpu, &y2, &b.b2, b.c2.co, h * w, true)?;
        let n = b.c2.co * h * w;
        let s = match &b.short {
            Some(c) => self.conv(gpu, c, x.ptr, h, w)?,
            None => x,
        };
        gpu.launch1("add_f32", n, &[Arg::P(y2.ptr), Arg::P(y2.ptr), Arg::P(s.ptr), Arg::L(n as i64)])?;
        Ok(y2)
    }

    /// 16 kHz audio -> F0 per 10 ms frame (0 = unvoiced).
    pub fn f0(&self, gpu: &Gpu, wave: &[f32], thred: f64) -> Result<Vec<f64>> {
        let (mel, frames) = self.mel(wave);
        let tp = frames.div_ceil(32) * 32;
        // [1][T][128] (time is the "height"), zero padded to a multiple of 32, then the input BatchNorm
        let mut x0 = vec![0f32; tp * 128];
        for t in 0..frames {
            for f in 0..128 {
                x0[t * 128 + f] = mel[f * frames + t] * self.in_bn.0 + self.in_bn.1;
            }
        }
        for t in frames..tp {
            for f in 0..128 {
                x0[t * 128 + f] = self.in_bn.1;
            }
        }
        let mut x = gpu.upload(&x0)?;
        let (mut h, mut w) = (tp, 128);
        let mut skips: Vec<(Buf, usize, usize, usize)> = Vec::new();
        for layer in &self.enc {
            for b in layer {
                x = self.res(gpu, b, x, h, w)?;
            }
            let c = layer[0].c1.co;
            let p = gpu.alloc(c * (h / 2) * (w / 2) * 4)?;
            gpu.launch1("avgpool2_f32", c * (h / 2) * (w / 2), &[Arg::P(p.ptr), Arg::P(x.ptr), Arg::I(c as i32), Arg::I(h as i32), Arg::I(w as i32)])?;
            skips.push((x, c, h, w));
            x = p;
            h /= 2;
            w /= 2;
        }
        for layer in &self.inter {
            for b in layer {
                x = self.res(gpu, b, x, h, w)?;
            }
        }
        for d in &self.dec {
            let (skip, sc, sh, sw) = skips.pop().unwrap();
            let co = d.tconv.co;
            let full = gpu.alloc(9 * co * h * w * 4)?;
            gpu.chan_matmul_f32(full.ptr, d.tconv.w.ptr, x.ptr, 9 * co, d.tconv.ci, h * w, h * w, 0.0)?;
            let n = co * sh * sw;
            let cat = gpu.alloc((co + sc) * sh * sw * 4)?;
            gpu.launch1("col2im2d_s2_f32", n, &[Arg::P(cat.ptr), Arg::P(full.ptr), Arg::I(co as i32), Arg::I(h as i32), Arg::I(w as i32)])?;
            drop(full);
            self.affine(gpu, &cat, &d.bn, co, sh * sw, true)?;
            gpu.copy(cat.ptr + (n * 4) as u64, skip.ptr, sc * sh * sw * 4)?;
            x = cat;
            h = sh;
            w = sw;
            for b in &d.blocks {
                x = self.res(gpu, b, x, h, w)?;
            }
        }
        let y = self.conv(gpu, &self.cnn, x.ptr, h, w)?;
        // [3][T][128] -> rows [T][384]
        let yh: Vec<f32> = gpu.read(y.ptr, 3 * tp * 128)?;
        let mut rows = vec![0f32; frames * 384];
        for t in 0..frames {
            for c in 0..3 {
                rows[t * 384 + c * 128..t * 384 + (c + 1) * 128].copy_from_slice(&yh[(c * tp + t) * 128..(c * tp + t + 1) * 128]);
            }
        }
        let xr = gpu.upload(&rows)?;
        let hid = 256;
        let out = gpu.alloc(frames * 2 * hid * 4)?;
        for (di, (wih, bih, whh, bhh)) in self.gru.iter().enumerate() {
            let gi = gpu.alloc(frames * 3 * hid * 4)?;
            gpu.launch1("fill_rows_f32", frames * 3 * hid, &[Arg::P(gi.ptr), Arg::P(bih.ptr), Arg::I((3 * hid) as i32), Arg::I(frames as i32), Arg::I(1)])?;
            gpu.linear_f32(gi.ptr, xr.ptr, wih.ptr, frames, 384, 3 * hid, 1.0)?;
            gpu.launch("gru_dir_f32", (1, 1, 1), ((3 * hid) as u32, 1, 1), (4 * hid * 4) as u32, &[Arg::P(gi.ptr), Arg::P(whh.ptr), Arg::P(bhh.ptr),
                Arg::P(out.ptr), Arg::I(frames as i32), Arg::I(hid as i32), Arg::I((2 * hid) as i32), Arg::I((di * hid) as i32), Arg::I(di as i32)])?;
        }
        let logits = gpu.alloc(frames * 360 * 4)?;
        gpu.launch1("fill_rows_f32", frames * 360, &[Arg::P(logits.ptr), Arg::P(self.fc.1.ptr), Arg::I(360), Arg::I(frames as i32), Arg::I(1)])?;
        gpu.linear_f32(logits.ptr, out.ptr, self.fc.0.ptr, frames, 2 * hid, 360, 1.0)?;
        let sal: Vec<f32> = gpu.read(logits.ptr, frames * 360)?;
        Ok(decode(&sal, frames, thred))
    }
}

/// Sigmoid salience -> local average cents around the peak -> Hz (0 when the peak is <= thred).
fn decode(logits: &[f32], frames: usize, thred: f64) -> Vec<f64> {
    (0..frames)
        .map(|t| {
            let s: Vec<f64> = logits[t * 360..(t + 1) * 360].iter().map(|v| 1.0 / (1.0 + (-(*v as f64)).exp())).collect();
            let (center, mx) = s.iter().enumerate().fold((0, f64::MIN), |a, (i, v)| if *v > a.1 { (i, *v) } else { a });
            let (mut ps, mut ws) = (0.0, 0.0);
            for i in center as isize - 4..=center as isize + 4 {
                if i < 0 || i >= 360 {
                    continue;
                }
                let cents = 20.0 * i as f64 + 1997.379408437619;
                ps += s[i as usize] * cents;
                ws += s[i as usize];
            }
            if mx <= thred {
                return 0.0;
            }
            let f = 10.0 * 2f64.powf(ps / ws / 1200.0);
            if f == 10.0 { 0.0 } else { f }
        })
        .collect()
}
