//! BigVGAN v2 (nvidia/bigvgan_v2_44khz_128band_512x) in Rust + CUDA, f32: log-mel [128][T] -> 44.1 kHz audio.
//! Anti-aliased SnakeBeta activations (2x up, snake, 2x down with the checkpoint's Kaiser filters), AMP blocks
//! (kernels 3 / 7 / 11, dilations 1 / 3 / 5), transposed-conv upsampling 8 4 2 2 2 2; weight norm folded at load.

use std::path::Path;

use anyhow::Result;

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

const UPS: [(usize, usize); 6] = [(8, 16), (4, 8), (2, 4), (2, 4), (2, 4), (2, 4)];
const KERNELS: [usize; 3] = [3, 7, 11];
const DILATIONS: [usize; 3] = [1, 3, 5];

struct Conv {
    w: Buf, // [k][co][ci]
    b: Option<Buf>,
    co: usize,
    ci: usize,
    k: usize,
}

struct TConv {
    w: Buf, // [(k * co + c)][ci]
    b: Buf,
    co: usize,
    ci: usize,
    k: usize,
    stride: usize,
}

struct Act {
    a: Buf,
    ib: Buf,
    up: Buf,
    down: Buf,
}

struct Amp {
    convs1: Vec<Conv>,
    convs2: Vec<Conv>,
    acts: Vec<Act>,
    dil: [usize; 3],
}

pub struct BigVgan {
    pre: Conv,
    ups: Vec<TConv>,
    blocks: Vec<Amp>,
    post_act: Act,
    post: Conv,
}

/// weight_norm (dim 0) folded: w = g * v / ||v||.
fn wn(st: &Shards, p: &str) -> Result<(Vec<f32>, Vec<usize>)> {
    let g = st.f32(&format!("{p}.weight_g"))?;
    let v = st.f32(&format!("{p}.weight_v"))?;
    let shape = st.shape(&format!("{p}.weight_v"))?;
    let per = v.len() / shape[0];
    let mut w = vec![0f32; v.len()];
    for i in 0..shape[0] {
        let s = &v[i * per..(i + 1) * per];
        let n = s.iter().map(|x| (*x as f64).powi(2)).sum::<f64>().sqrt() as f32;
        for (d, x) in w[i * per..(i + 1) * per].iter_mut().zip(s) {
            *d = g[i] * *x / n;
        }
    }
    Ok((w, shape))
}

fn conv(gpu: &Gpu, st: &Shards, p: &str) -> Result<Conv> {
    let (w, shape) = wn(st, p)?;
    let (co, ci, k) = (shape[0], shape[1], shape[2]);
    let mut r = vec![0f32; w.len()];
    for o in 0..co {
        for i in 0..ci {
            for j in 0..k {
                r[(j * co + o) * ci + i] = w[(o * ci + i) * k + j];
            }
        }
    }
    let b = if st.has(&format!("{p}.bias")) { Some(gpu.upload(&st.f32(&format!("{p}.bias"))?)?) } else { None };
    Ok(Conv { w: gpu.upload(&r)?, b, co, ci, k })
}

fn tconv(gpu: &Gpu, st: &Shards, p: &str, stride: usize) -> Result<TConv> {
    let (w, shape) = wn(st, p)?;
    let (ci, co, k) = (shape[0], shape[1], shape[2]);
    let mut r = vec![0f32; w.len()];
    for i in 0..ci {
        for o in 0..co {
            for j in 0..k {
                r[(j * co + o) * ci + i] = w[(i * co + o) * k + j];
            }
        }
    }
    Ok(TConv { w: gpu.upload(&r)?, b: gpu.upload(&st.f32(&format!("{p}.bias"))?)?, co, ci, k, stride })
}

fn act(gpu: &Gpu, st: &Shards, p: &str) -> Result<Act> {
    let a: Vec<f32> = st.f32(&format!("{p}.act.alpha"))?.iter().map(|x| x.exp()).collect();
    let ib: Vec<f32> = st.f32(&format!("{p}.act.beta"))?.iter().map(|x| 1.0 / (x.exp() + 1e-9)).collect();
    Ok(Act { a: gpu.upload(&a)?, ib: gpu.upload(&ib)?, up: gpu.upload(&st.f32(&format!("{p}.upsample.filter"))?)?,
             down: gpu.upload(&st.f32(&format!("{p}.downsample.lowpass.filter"))?)? })
}

impl BigVgan {
    pub fn load(gpu: &Gpu, path: &Path) -> Result<Self> {
        let st = Shards::open_file(path)?;
        let g = "generator";
        let mut ups = Vec::new();
        let mut blocks = Vec::new();
        for (i, (u, _)) in UPS.iter().enumerate() {
            ups.push(tconv(gpu, &st, &format!("{g}.ups.{i}.0"), *u)?);
            for j in 0..KERNELS.len() {
                let p = format!("{g}.resblocks.{}", i * KERNELS.len() + j);
                let mut convs1 = Vec::new();
                let mut convs2 = Vec::new();
                let mut acts = Vec::new();
                for d in 0..3 {
                    convs1.push(conv(gpu, &st, &format!("{p}.convs1.{d}"))?);
                    convs2.push(conv(gpu, &st, &format!("{p}.convs2.{d}"))?);
                }
                for a in 0..6 {
                    acts.push(act(gpu, &st, &format!("{p}.activations.{a}"))?);
                }
                blocks.push(Amp { convs1, convs2, acts, dil: DILATIONS });
            }
        }
        Ok(Self {
            pre: conv(gpu, &st, &format!("{g}.conv_pre"))?,
            ups,
            blocks,
            post_act: act(gpu, &st, &format!("{g}.activation_post"))?,
            post: conv(gpu, &st, &format!("{g}.conv_post"))?,
        })
    }

    /// "same" conv with dilation over [ci][t].
    fn conv(&self, gpu: &Gpu, c: &Conv, x: u64, t: usize, dil: usize) -> Result<Buf> {
        let y = match &c.b {
            Some(b) => {
                let y = gpu.alloc(c.co * t * 4)?;
                gpu.launch1("fill_rows_f32", c.co * t, &[Arg::P(y.ptr), Arg::P(b.ptr), Arg::I(c.co as i32), Arg::I(t as i32), Arg::I(0)])?;
                y
            }
            None => gpu.zeros(c.co * t * 4)?,
        };
        let pad = dil * (c.k - 1) / 2;
        let g = gpu.alloc(c.ci * t * 4)?;
        for j in 0..c.k {
            gpu.launch1("gather_shift_f32", c.ci * t, &[Arg::P(g.ptr), Arg::P(x), Arg::I(c.ci as i32), Arg::I(1), Arg::I(t as i32),
                Arg::I(1), Arg::I(t as i32), Arg::I(1), Arg::I(0), Arg::I(1), Arg::I((j * dil) as i32 - pad as i32)])?;
            gpu.chan_matmul_f32(y.ptr, ptr_f32(c.w.ptr, j * c.co * c.ci), g.ptr, c.co, c.ci, t, t, 1.0)?;
        }
        Ok(y)
    }

    fn act(&self, gpu: &Gpu, a: &Act, x: u64, c: usize, t: usize) -> Result<Buf> {
        let u = gpu.alloc(c * 2 * t * 4)?;
        gpu.launch1("aa_up2_f32", c * 2 * t, &[Arg::P(u.ptr), Arg::P(x), Arg::P(a.up.ptr), Arg::I(c as i32), Arg::I(t as i32)])?;
        gpu.launch1("snake_f32", c * 2 * t, &[Arg::P(u.ptr), Arg::P(u.ptr), Arg::P(a.a.ptr), Arg::P(a.ib.ptr), Arg::I(c as i32), Arg::I((2 * t) as i32)])?;
        let y = gpu.alloc(c * t * 4)?;
        gpu.launch1("aa_down2_f32", c * t, &[Arg::P(y.ptr), Arg::P(u.ptr), Arg::P(a.down.ptr), Arg::I(c as i32), Arg::I(t as i32)])?;
        Ok(y)
    }

    fn amp(&self, gpu: &Gpu, b: &Amp, x: &Buf, c: usize, t: usize) -> Result<Buf> {
        let out = gpu.alloc(c * t * 4)?;
        gpu.copy(out.ptr, x.ptr, c * t * 4)?;
        for d in 0..3 {
            let h = self.act(gpu, &b.acts[2 * d], out.ptr, c, t)?;
            let h = self.conv(gpu, &b.convs1[d], h.ptr, t, b.dil[d])?;
            let h = self.act(gpu, &b.acts[2 * d + 1], h.ptr, c, t)?;
            let h = self.conv(gpu, &b.convs2[d], h.ptr, t, 1)?;
            gpu.launch1("add_f32", c * t, &[Arg::P(out.ptr), Arg::P(out.ptr), Arg::P(h.ptr), Arg::L((c * t) as i64)])?;
        }
        Ok(out)
    }

    /// mel [128][frames] (row major) -> samples (frames * 512).
    pub fn forward(&self, gpu: &Gpu, mel: &[f32], frames: usize) -> Result<Vec<f32>> {
        let m = gpu.upload(mel)?;
        let mut x = self.conv(gpu, &self.pre, m.ptr, frames, 1)?;
        let mut t = frames;
        for (i, up) in self.ups.iter().enumerate() {
            let full = gpu.alloc(up.k * up.co * t * 4)?;
            gpu.chan_matmul_f32(full.ptr, up.w.ptr, x.ptr, up.k * up.co, up.ci, t, t, 0.0)?;
            let to = t * up.stride;
            let y = gpu.alloc(up.co * to * 4)?;
            // padding (k - u) / 2 on both sides: output sample m is full position m + p
            gpu.launch1("col2im_f_f32", up.co * to, &[Arg::P(y.ptr), Arg::P(full.ptr), Arg::P(up.b.ptr), Arg::I(up.co as i32), Arg::I(up.k as i32),
                Arg::I(t as i32), Arg::I(1), Arg::I(up.stride as i32), Arg::I(((up.k - up.stride) / 2) as i32), Arg::I(to as i32)])?;
            drop(full);
            t = to;
            let c = up.co;
            let acc = gpu.zeros(c * t * 4)?;
            for j in 0..KERNELS.len() {
                let r = self.amp(gpu, &self.blocks[i * KERNELS.len() + j], &y, c, t)?;
                gpu.launch1("add_f32", c * t, &[Arg::P(acc.ptr), Arg::P(acc.ptr), Arg::P(r.ptr), Arg::L((c * t) as i64)])?;
            }
            gpu.launch1("scale_f32", c * t, &[Arg::P(acc.ptr), Arg::L((c * t) as i64), Arg::F(1.0 / KERNELS.len() as f32)])?;
            x = acc;
        }
        let c = self.ups.last().unwrap().co;
        let h = self.act(gpu, &self.post_act, x.ptr, c, t)?;
        let y = self.conv(gpu, &self.post, h.ptr, t, 1)?;
        gpu.launch1("clamp_scale_f32", t, &[Arg::P(y.ptr), Arg::L(t as i64), Arg::F(1.0), Arg::F(-1.0), Arg::F(1.0)])?;
        gpu.read(y.ptr, t)
    }
}
