//! Demucs htdemucs_ft (vocal / accompaniment separation) in Rust + CUDA, f32. Mirrors `demucs/htdemucs.py`
//! (HTDemucs: frequency + time branches, cross-domain transformer), `demucs/apply.py` (7.8 s segments, 25 % overlap,
//! triangular cross-fade; no random shifts, so the result is repeatable) and the normalization of `demucs/api.py`.
//! The bag has one model per source; each model contributes only its own source, as the bag weights say.

use std::path::Path;

use anyhow::{bail, Result};

use super::gpu::{ptr_f32, Arg, Buf, Gpu};
use super::weights::Shards;

pub const RATE: u32 = 44_100;
const NFFT: usize = 4096;
const HOP: usize = 1024;
const BINS: usize = 2048;
/// int(Fraction(39, 5) * 44100)
pub const SEGMENT: usize = 343_980;
pub const SOURCES: [&str; 4] = ["drums", "bass", "other", "vocals"];
/// htdemucs_ft.yaml: model ids in source order (weights are one-hot).
pub const BAG: [&str; 4] = ["f7e0c4bc", "d12395a8", "92cfc3b6", "04573f0d"];

struct Conv {
    w: Buf, // [kf * kt][co][ci]
    b: Buf,
    co: usize,
    ci: usize,
    kf: usize,
    kt: usize,
}

struct TConv {
    w: Buf, // [(k * co + c)][ci]
    b: Buf,
    co: usize,
    ci: usize,
    k: usize,
}

struct DLayer {
    c1: Conv,
    n1: (Buf, Buf),
    c2: Conv,
    n2: (Buf, Buf),
    scale: Buf,
    dil: usize,
}

struct Enc {
    conv: Conv,
    dconv: Vec<DLayer>,
    rewrite: Conv,
}

struct Dec {
    rewrite: Conv,
    dconv: Vec<DLayer>,
    conv_tr: TConv,
}

struct Lin {
    w: Buf,
    b: Buf,
    out: usize,
    inp: usize,
}

struct TLayer {
    cross: bool,
    in_q: Lin,
    in_kv: Lin,
    in_qkv: Lin,
    out: Lin,
    n1: (Buf, Buf),
    n2: (Buf, Buf),
    n3: Option<(Buf, Buf)>,
    l1: Lin,
    l2: Lin,
    g1: Buf,
    g2: Buf,
    nout: (Buf, Buf),
}

pub struct Model {
    enc: Vec<Enc>,
    tenc: Vec<Enc>,
    dec: Vec<Dec>,
    tdec: Vec<Dec>,
    freq_emb: Buf,
    up: Conv,
    down: Conv,
    up_t: Conv,
    down_t: Conv,
    norm_in: (Buf, Buf),
    norm_in_t: (Buf, Buf),
    layers: Vec<TLayer>,
    layers_t: Vec<TLayer>,
}

type Probe<'a> = Option<&'a mut (dyn FnMut(&str, u64, usize) -> Result<()> + 'a)>;

fn up(gpu: &Gpu, v: &[f32]) -> Result<Buf> {
    gpu.upload(v)
}

fn conv(gpu: &Gpu, st: &Shards, p: &str) -> Result<Conv> {
    let w = st.f32(&format!("{p}.weight"))?;
    let shape = st.shape(&format!("{p}.weight"))?;
    let (co, ci, kf, kt) = match shape.as_slice() {
        [co, ci, k] => (*co, *ci, 1, *k),
        [co, ci, kf, kt] => (*co, *ci, *kf, *kt),
        _ => bail!("{p}: shape {shape:?}"),
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
    Ok(Conv { w: up(gpu, &r)?, b: up(gpu, &st.f32(&format!("{p}.bias"))?)?, co, ci, kf, kt })
}

fn tconv(gpu: &Gpu, st: &Shards, p: &str) -> Result<TConv> {
    let w = st.f32(&format!("{p}.weight"))?;
    let shape = st.shape(&format!("{p}.weight"))?;
    let (ci, co, k) = (shape[0], shape[1], shape[2]);
    let mut r = vec![0f32; w.len()];
    for i in 0..ci {
        for o in 0..co {
            for j in 0..k {
                r[(j * co + o) * ci + i] = w[(i * co + o) * k + j];
            }
        }
    }
    Ok(TConv { w: up(gpu, &r)?, b: up(gpu, &st.f32(&format!("{p}.bias"))?)?, co, ci, k })
}

fn pair(gpu: &Gpu, st: &Shards, p: &str) -> Result<(Buf, Buf)> {
    Ok((up(gpu, &st.f32(&format!("{p}.weight"))?)?, up(gpu, &st.f32(&format!("{p}.bias"))?)?))
}

fn dconv(gpu: &Gpu, st: &Shards, p: &str) -> Result<Vec<DLayer>> {
    let mut out = Vec::new();
    for d in 0..2 {
        let q = format!("{p}.dconv.layers.{d}");
        out.push(DLayer {
            c1: conv(gpu, st, &format!("{q}.0"))?,
            n1: pair(gpu, st, &format!("{q}.1"))?,
            c2: conv(gpu, st, &format!("{q}.3"))?,
            n2: pair(gpu, st, &format!("{q}.4"))?,
            scale: up(gpu, &st.f32(&format!("{q}.6.scale"))?)?,
            dil: 1 << d,
        });
    }
    Ok(out)
}

fn lin_rows(gpu: &Gpu, w: &[f32], b: &[f32], rows: std::ops::Range<usize>, inp: usize) -> Result<Lin> {
    Ok(Lin { w: up(gpu, &w[rows.start * inp..rows.end * inp])?, b: up(gpu, &b[rows.clone()])?, out: rows.len(), inp })
}

fn lin(gpu: &Gpu, st: &Shards, p: &str) -> Result<Lin> {
    let shape = st.shape(&format!("{p}.weight"))?;
    Ok(Lin { w: up(gpu, &st.f32(&format!("{p}.weight"))?)?, b: up(gpu, &st.f32(&format!("{p}.bias"))?)?, out: shape[0], inp: shape[1] })
}

fn tlayer(gpu: &Gpu, st: &Shards, p: &str, cross: bool) -> Result<TLayer> {
    let a = if cross { format!("{p}.cross_attn") } else { format!("{p}.self_attn") };
    let w = st.f32(&format!("{a}.in_proj_weight"))?;
    let b = st.f32(&format!("{a}.in_proj_bias"))?;
    let d = b.len() / 3;
    Ok(TLayer {
        cross,
        in_q: lin_rows(gpu, &w, &b, 0..d, d)?,
        in_kv: lin_rows(gpu, &w, &b, d..3 * d, d)?,
        in_qkv: lin_rows(gpu, &w, &b, 0..3 * d, d)?,
        out: lin(gpu, st, &format!("{a}.out_proj"))?,
        n1: pair(gpu, st, &format!("{p}.norm1"))?,
        n2: pair(gpu, st, &format!("{p}.norm2"))?,
        n3: if cross { Some(pair(gpu, st, &format!("{p}.norm3"))?) } else { None },
        l1: lin(gpu, st, &format!("{p}.linear1"))?,
        l2: lin(gpu, st, &format!("{p}.linear2"))?,
        g1: up(gpu, &st.f32(&format!("{p}.gamma_1.scale"))?)?,
        g2: up(gpu, &st.f32(&format!("{p}.gamma_2.scale"))?)?,
        nout: pair(gpu, st, &format!("{p}.norm_out"))?,
    })
}

fn n1(gpu: &Gpu, name: &str, n: usize, args: &[Arg]) -> Result<()> {
    gpu.launch1(name, n, args)
}

impl Model {
    pub fn load(gpu: &Gpu, path: &Path) -> Result<Self> {
        let st = Shards::open_file(path)?;
        let mut enc = Vec::new();
        let mut tenc = Vec::new();
        let mut dec = Vec::new();
        let mut tdec = Vec::new();
        for i in 0..4 {
            enc.push(Enc { conv: conv(gpu, &st, &format!("encoder.{i}.conv"))?, dconv: dconv(gpu, &st, &format!("encoder.{i}"))?,
                           rewrite: conv(gpu, &st, &format!("encoder.{i}.rewrite"))? });
            tenc.push(Enc { conv: conv(gpu, &st, &format!("tencoder.{i}.conv"))?, dconv: dconv(gpu, &st, &format!("tencoder.{i}"))?,
                            rewrite: conv(gpu, &st, &format!("tencoder.{i}.rewrite"))? });
            dec.push(Dec { rewrite: conv(gpu, &st, &format!("decoder.{i}.rewrite"))?, dconv: dconv(gpu, &st, &format!("decoder.{i}"))?,
                           conv_tr: tconv(gpu, &st, &format!("decoder.{i}.conv_tr"))? });
            tdec.push(Dec { rewrite: conv(gpu, &st, &format!("tdecoder.{i}.rewrite"))?, dconv: dconv(gpu, &st, &format!("tdecoder.{i}"))?,
                            conv_tr: tconv(gpu, &st, &format!("tdecoder.{i}.conv_tr"))? });
        }
        // ScaledEmbedding: embedding * scale (10).
        let emb: Vec<f32> = st.f32("freq_emb.embedding.weight")?.into_iter().map(|v| v * 10.0).collect();
        let mut layers = Vec::new();
        let mut layers_t = Vec::new();
        for i in 0..5 {
            let cross = i % 2 == 1;
            layers.push(tlayer(gpu, &st, &format!("crosstransformer.layers.{i}"), cross)?);
            layers_t.push(tlayer(gpu, &st, &format!("crosstransformer.layers_t.{i}"), cross)?);
        }
        Ok(Self {
            enc, tenc, dec, tdec,
            freq_emb: up(gpu, &emb)?,
            up: conv(gpu, &st, "channel_upsampler")?,
            down: conv(gpu, &st, "channel_downsampler")?,
            up_t: conv(gpu, &st, "channel_upsampler_t")?,
            down_t: conv(gpu, &st, "channel_downsampler_t")?,
            norm_in: pair(gpu, &st, "crosstransformer.norm_in")?,
            norm_in_t: pair(gpu, &st, "crosstransformer.norm_in_t")?,
            layers, layers_t,
        })
    }

    /// Conv over a [ci][f][t] map with taps along F (stride sf, pad pf) and T (stride st, pad pt, dilation dil).
    #[allow(clippy::too_many_arguments)]
    fn conv(&self, gpu: &Gpu, c: &Conv, x: u64, f: usize, t: usize, sf: usize, pf: usize, st: usize, pt: usize, dil: usize) -> Result<(Buf, usize, usize)> {
        let fo = (f + 2 * pf - c.kf) / sf + 1;
        let to = (t + 2 * pt - dil * (c.kt - 1) - 1) / st + 1;
        let n = fo * to;
        let y = gpu.alloc(c.co * n * 4)?;
        n1(gpu, "fill_rows_f32", c.co * n, &[Arg::P(y.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(n as i32), Arg::I(0)])?;
        if c.kf == 1 && c.kt == 1 && sf == 1 && st == 1 && pf == 0 && pt == 0 {
            gpu.chan_matmul_f32(y.ptr, c.w.ptr, x, c.co, c.ci, n, n, 1.0)?;
            return Ok((y, fo, to));
        }
        let g = gpu.alloc(c.ci * n * 4)?;
        for a in 0..c.kf {
            for b in 0..c.kt {
                n1(gpu, "gather_shift_f32", c.ci * n, &[Arg::P(g.ptr), Arg::P(x), Arg::I(c.ci as i32), Arg::I(fo as i32), Arg::I(to as i32),
                    Arg::I(f as i32), Arg::I(t as i32), Arg::I(sf as i32), Arg::I(a as i32 - pf as i32), Arg::I(st as i32),
                    Arg::I((b * dil) as i32 - pt as i32)])?;
                let j = a * c.kt + b;
                gpu.chan_matmul_f32(y.ptr, ptr_f32(c.w.ptr, j * c.co * c.ci), g.ptr, c.co, c.ci, n, n, 1.0)?;
            }
        }
        Ok((y, fo, to))
    }

    /// Transposed conv along F (stride 4) of a [ci][fi][t] map, cropped to [lo, lo + fo).
    fn tconv(&self, gpu: &Gpu, c: &TConv, x: u64, fi: usize, t: usize, lo: usize, fo: usize) -> Result<Buf> {
        let full = gpu.alloc(c.k * c.co * fi * t * 4)?;
        gpu.chan_matmul_f32(full.ptr, c.w.ptr, x, c.k * c.co, c.ci, fi * t, fi * t, 0.0)?;
        let out = gpu.alloc(c.co * fo * t * 4)?;
        n1(gpu, "col2im_f_f32", c.co * fo * t, &[Arg::P(out.ptr), Arg::P(full.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(c.k as i32),
            Arg::I(fi as i32), Arg::I(t as i32), Arg::I(4), Arg::I(lo as i32), Arg::I(fo as i32)])?;
        Ok(out)
    }

    fn gelu(&self, gpu: &Gpu, x: &Buf, n: usize) -> Result<()> {
        n1(gpu, "gelu_f32", n, &[Arg::P(x.ptr), Arg::L(n as i64)])
    }

    fn glu(&self, gpu: &Gpu, x: &Buf, c: usize, n: usize) -> Result<Buf> {
        let out = gpu.alloc(c * n * 4)?;
        n1(gpu, "glu_f32", c * n, &[Arg::P(out.ptr), Arg::P(x.ptr), Arg::I(c as i32), Arg::L(n as i64)])?;
        Ok(out)
    }

    /// DConv residual branch over T for each f of a [c][f][t] map (in place).
    fn dconv(&self, gpu: &Gpu, layers: &[DLayer], x: &Buf, c: usize, f: usize, t: usize) -> Result<()> {
        for l in layers {
            let (h, _, _) = self.conv(gpu, &l.c1, x.ptr, f, t, 1, 0, 1, l.dil, l.dil)?;
            let hid = l.c1.co;
            gpu.launch("groupnorm1_cbt_f32", (f as u32, 1, 1), (1024, 1, 1), 0, &[Arg::P(h.ptr), Arg::P(l.n1.0.ptr), Arg::P(l.n1.1.ptr),
                Arg::I(hid as i32), Arg::I(f as i32), Arg::I(t as i32), Arg::F(1e-5)])?;
            self.gelu(gpu, &h, hid * f * t)?;
            let (h2, _, _) = self.conv(gpu, &l.c2, h.ptr, f, t, 1, 0, 1, 0, 1)?;
            gpu.launch("groupnorm1_cbt_f32", (f as u32, 1, 1), (1024, 1, 1), 0, &[Arg::P(h2.ptr), Arg::P(l.n2.0.ptr), Arg::P(l.n2.1.ptr),
                Arg::I((2 * c) as i32), Arg::I(f as i32), Arg::I(t as i32), Arg::F(1e-5)])?;
            let g = self.glu(gpu, &h2, c, f * t)?;
            n1(gpu, "add_scaled_f32", c * f * t, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(g.ptr), Arg::P(l.scale.ptr), Arg::I(c as i32),
                Arg::I((f * t) as i32), Arg::I(0)])?;
        }
        Ok(())
    }

    /// HEncLayer: conv (stride 4 along F for the frequency branch, along T for the time branch), GELU, DConv, 1x1 rewrite + GLU.
    fn encode(&self, gpu: &Gpu, e: &Enc, x: u64, f: usize, t: usize, freq: bool) -> Result<(Buf, usize, usize)> {
        let (y, fo, to) = if freq {
            self.conv(gpu, &e.conv, x, f, t, 4, 2, 1, 0, 1)?
        } else {
            // the time branch pads T to a multiple of the stride first (the gather reads zeros past t)
            self.conv_padded_t(gpu, &e.conv, x, t, t.div_ceil(4) * 4)?
        };
        let c = e.conv.co;
        self.gelu(gpu, &y, c * fo * to)?;
        self.dconv(gpu, &e.dconv, &y, c, fo, to)?;
        let (z, _, _) = self.conv(gpu, &e.rewrite, y.ptr, fo, to, 1, 0, 1, 0, 1)?;
        Ok((self.glu(gpu, &z, c, fo * to)?, fo, to))
    }

    /// Time-branch strided conv on a sequence zero-padded from t to tp samples.
    fn conv_padded_t(&self, gpu: &Gpu, c: &Conv, x: u64, t: usize, tp: usize) -> Result<(Buf, usize, usize)> {
        let to = (tp + 4 - c.kt) / 4 + 1;
        let y = gpu.alloc(c.co * to * 4)?;
        n1(gpu, "fill_rows_f32", c.co * to, &[Arg::P(y.ptr), Arg::P(c.b.ptr), Arg::I(c.co as i32), Arg::I(to as i32), Arg::I(0)])?;
        let g = gpu.alloc(c.ci * to * 4)?;
        for b in 0..c.kt {
            n1(gpu, "gather_shift_f32", c.ci * to, &[Arg::P(g.ptr), Arg::P(x), Arg::I(c.ci as i32), Arg::I(1), Arg::I(to as i32),
                Arg::I(1), Arg::I(t as i32), Arg::I(1), Arg::I(0), Arg::I(4), Arg::I(b as i32 - 2)])?;
            gpu.chan_matmul_f32(y.ptr, ptr_f32(c.w.ptr, b * c.co * c.ci), g.ptr, c.co, c.ci, to, to, 1.0)?;
        }
        Ok((y, 1, to))
    }

    /// HDecLayer: + skip, rewrite (3 or 3x3) + GLU, DConv, transposed conv, crop, GELU unless last.
    #[allow(clippy::too_many_arguments)]
    fn decode(&self, gpu: &Gpu, d: &Dec, x: &Buf, skip: &Buf, f: usize, t: usize, freq: bool, length: usize, last: bool) -> Result<(Buf, usize)> {
        let cin = d.conv_tr.ci;
        let n = cin * f * t;
        n1(gpu, "add_f32", n, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(skip.ptr), Arg::L(n as i64)])?;
        let (pf, pt) = if freq { (1, 1) } else { (0, 1) };
        let (r, _, _) = self.conv(gpu, &d.rewrite, x.ptr, f, t, 1, pf, 1, pt, 1)?;
        let y = self.glu(gpu, &r, cin, f * t)?;
        drop(r);
        self.dconv(gpu, &d.dconv, &y, cin, f, t)?;
        let co = d.conv_tr.co;
        let (z, fo) = if freq {
            let fo = 4 * f;
            (self.tconv(gpu, &d.conv_tr, y.ptr, f, t, 2, fo)?, fo)
        } else {
            // time branch: the sequence is the "F" axis of a [c][t][1] map
            (self.tconv(gpu, &d.conv_tr, y.ptr, t, 1, 2, length)?, length)
        };
        let out_n = if freq { co * fo * t } else { co * length };
        if !last {
            self.gelu(gpu, &z, out_n)?;
        }
        Ok((z, fo))
    }

    fn linear(&self, gpu: &Gpu, l: &Lin, x: u64, rows: usize) -> Result<Buf> {
        let y = gpu.alloc(rows * l.out * 4)?;
        n1(gpu, "fill_rows_f32", rows * l.out, &[Arg::P(y.ptr), Arg::P(l.b.ptr), Arg::I(l.out as i32), Arg::I(rows as i32), Arg::I(1)])?;
        gpu.linear_f32(y.ptr, x, l.w.ptr, rows, l.inp, l.out, 1.0)?;
        Ok(y)
    }

    fn layernorm(&self, gpu: &Gpu, x: u64, rows: usize, d: usize, n: &(Buf, Buf)) -> Result<Buf> {
        let out = gpu.alloc(rows * d * 4)?;
        gpu.launch("layernorm_affine_rows_f32", (rows as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(out.ptr), Arg::P(x), Arg::P(n.0.ptr), Arg::P(n.1.ptr),
            Arg::I(d as i32), Arg::F(1e-5)])?;
        Ok(out)
    }

    /// nn.MultiheadAttention (8 heads) of q rows against kv rows; both already normed.
    fn mha(&self, gpu: &Gpu, l: &TLayer, q_in: u64, lq: usize, kv_in: u64, lk: usize) -> Result<Buf> {
        let d = 512;
        let (heads, hd) = (8, 64);
        let (qb, kvb);
        let (q, ldq, k, v, ldkv);
        if l.cross {
            qb = self.linear(gpu, &l.in_q, q_in, lq)?;
            kvb = self.linear(gpu, &l.in_kv, kv_in, lk)?;
            q = qb.ptr;
            ldq = d;
            k = kvb.ptr;
            v = ptr_f32(kvb.ptr, d);
            ldkv = 2 * d;
        } else {
            qb = self.linear(gpu, &l.in_qkv, q_in, lq)?;
            kvb = gpu.alloc(4)?;
            q = qb.ptr;
            ldq = 3 * d;
            k = ptr_f32(qb.ptr, d);
            v = ptr_f32(qb.ptr, 2 * d);
            ldkv = 3 * d;
        }
        let _ = &kvb;
        let scores = gpu.alloc(lq * lk * 4)?;
        let attn = gpu.alloc(lq * d * 4)?;
        for h in 0..heads {
            gpu.sgemm(true, false, lk, lq, hd, ptr_f32(k, h * hd), ldkv, ptr_f32(q, h * hd), ldq, 0.0, scores.ptr, lk)?;
            gpu.launch("softmax_rows_f32", (lq as u32, 1, 1), (256, 1, 1), 0, &[Arg::P(scores.ptr), Arg::I(lk as i32), Arg::F(1.0 / (hd as f32).sqrt())])?;
            gpu.sgemm(false, false, hd, lq, lk, ptr_f32(v, h * hd), ldkv, scores.ptr, lk, 0.0, ptr_f32(attn.ptr, h * hd), d)?;
        }
        self.linear(gpu, &l.out, attn.ptr, lq)
    }

    /// One transformer layer (norm first, layer scale, GroupNorm out) on x [lx, 512], attending to `other` if cross.
    fn tlayer(&self, gpu: &Gpu, l: &TLayer, x: &Buf, lx: usize, other: Option<(&Buf, usize)>) -> Result<()> {
        let d = 512;
        let n = lx * d;
        let h = self.layernorm(gpu, x.ptr, lx, d, &l.n1)?;
        let a = match other {
            Some((o, lo)) => {
                let k = self.layernorm(gpu, o.ptr, lo, d, &l.n2)?;
                self.mha(gpu, l, h.ptr, lx, k.ptr, lo)?
            }
            None => self.mha(gpu, l, h.ptr, lx, h.ptr, lx)?,
        };
        n1(gpu, "add_scaled_f32", n, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(a.ptr), Arg::P(l.g1.ptr), Arg::I(d as i32), Arg::I(lx as i32), Arg::I(1)])?;
        let norm2 = if l.cross { l.n3.as_ref().unwrap() } else { &l.n2 };
        let h = self.layernorm(gpu, x.ptr, lx, d, norm2)?;
        let f1 = self.linear(gpu, &l.l1, h.ptr, lx)?;
        self.gelu(gpu, &f1, lx * l.l1.out)?;
        let f2 = self.linear(gpu, &l.l2, f1.ptr, lx)?;
        n1(gpu, "add_scaled_f32", n, &[Arg::P(x.ptr), Arg::P(x.ptr), Arg::P(f2.ptr), Arg::P(l.g2.ptr), Arg::I(d as i32), Arg::I(lx as i32), Arg::I(1)])?;
        gpu.launch("groupnorm1_rows_f32", (1, 1, 1), (1024, 1, 1), 0, &[Arg::P(x.ptr), Arg::P(l.nout.0.ptr), Arg::P(l.nout.1.ptr), Arg::L(n as i64),
            Arg::I(d as i32), Arg::F(1e-5)])?;
        Ok(())
    }

    /// The model on one segment of SEGMENT normalized stereo samples: source `want` as [2][SEGMENT] in the same
    /// (normalized) space.
    pub fn forward(&self, gpu: &Gpu, mix: &[Vec<f32>; 2], want: usize, mut probe: Probe<'_>) -> Result<[Vec<f32>; 2]> {
        let length = SEGMENT;
        if mix[0].len() != length || mix[1].len() != length {
            bail!("segment must be {length} samples");
        }
        let le = length.div_ceil(HOP);
        // Spectrogram (CPU) -> real/imag as channels: [ch * 2 + ri][bin][frame].
        let spec: Vec<Vec<(f64, f64)>> = mix.iter().map(|x| stft(x, le)).collect();
        let mut mag = vec![0f32; 4 * BINS * le];
        for (ch, z) in spec.iter().enumerate() {
            for (i, (re, im)) in z.iter().enumerate() {
                mag[(ch * 2) * BINS * le + i] = *re as f32;
                mag[(ch * 2 + 1) * BINS * le + i] = *im as f32;
            }
        }
        let (mean, std) = mean_std(&mag);
        let xn: Vec<f32> = mag.iter().map(|v| ((*v as f64 - mean) / (1e-5 + std)) as f32).collect();
        let flat: Vec<f32> = mix[0].iter().chain(mix[1].iter()).copied().collect();
        let (meant, stdt) = mean_std(&flat);
        let xtn: Vec<f32> = flat.iter().map(|v| ((*v as f64 - meant) / (1e-5 + stdt)) as f32).collect();
        let x0 = up(gpu, &xn)?;
        let xt0 = up(gpu, &xtn)?;
        let t = le;
        let mut saved: Vec<(Buf, usize)> = Vec::new();
        let mut saved_t: Vec<(Buf, usize)> = Vec::new();
        let mut lengths_t = Vec::new();
        for i in 0..4 {
            let (src_t, lt) = if i == 0 { (xt0.ptr, length) } else { (saved_t[i - 1].0.ptr, saved_t[i - 1].1) };
            lengths_t.push(lt);
            let (yt, _, lto) = self.encode(gpu, &self.tenc[i], src_t, 1, lt, false)?;
            if let Some(p) = probe.as_mut() {
                p(&format!("tenc{i}"), yt.ptr, self.tenc[i].conv.co * lto)?;
            }
            saved_t.push((yt, lto));
            let (src, f) = if i == 0 { (x0.ptr, BINS) } else { (saved[i - 1].0.ptr, saved[i - 1].1) };
            let (y, fo, _) = self.encode(gpu, &self.enc[i], src, f, t, true)?;
            let c = self.enc[i].conv.co;
            if let Some(p) = probe.as_mut() {
                p(&format!("enc{i}"), y.ptr, c * fo * t)?;
            }
            if i == 0 {
                n1(gpu, "add_freq_emb_f32", c * fo * t, &[Arg::P(y.ptr), Arg::P(self.freq_emb.ptr), Arg::I(c as i32), Arg::I(fo as i32), Arg::I(t as i32), Arg::F(0.2)])?;
            }
            saved.push((y, fo));
        }
        drop(x0);
        drop(xt0);
        // Bottom: channel up-sampling, cross transformer, down-sampling.
        let (fb, ltb) = (saved[3].1, saved_t[3].1);
        let (xu, _, _) = self.conv(gpu, &self.up, saved[3].0.ptr, fb, t, 1, 0, 1, 0, 1)?;
        let (xtu, _, _) = self.conv(gpu, &self.up_t, saved_t[3].0.ptr, 1, ltb, 1, 0, 1, 0, 1)?;
        let d = 512;
        let lx = fb * t;
        let tok = gpu.alloc(lx * d * 4)?;
        n1(gpu, "cft_to_tokens_f32", lx * d, &[Arg::P(tok.ptr), Arg::P(xu.ptr), Arg::I(d as i32), Arg::I(fb as i32), Arg::I(t as i32)])?;
        let tok_t = gpu.alloc(ltb * d * 4)?;
        n1(gpu, "cft_to_tokens_f32", ltb * d, &[Arg::P(tok_t.ptr), Arg::P(xtu.ptr), Arg::I(d as i32), Arg::I(1), Arg::I(ltb as i32)])?;
        drop((xu, xtu));
        let xs = self.layernorm(gpu, tok.ptr, lx, d, &self.norm_in)?;
        let pe = up(gpu, &pos_2d(d, fb, t))?;
        n1(gpu, "add_f32", lx * d, &[Arg::P(xs.ptr), Arg::P(xs.ptr), Arg::P(pe.ptr), Arg::L((lx * d) as i64)])?;
        let xts = self.layernorm(gpu, tok_t.ptr, ltb, d, &self.norm_in_t)?;
        let pt = up(gpu, &pos_1d(ltb, d))?;
        n1(gpu, "add_f32", ltb * d, &[Arg::P(xts.ptr), Arg::P(xts.ptr), Arg::P(pt.ptr), Arg::L((ltb * d) as i64)])?;
        drop((tok, tok_t, pe, pt));
        for i in 0..5 {
            if i % 2 == 0 {
                self.tlayer(gpu, &self.layers[i], &xs, lx, None)?;
                self.tlayer(gpu, &self.layers_t[i], &xts, ltb, None)?;
            } else {
                let old = gpu.alloc(lx * d * 4)?;
                gpu.copy(old.ptr, xs.ptr, lx * d * 4)?;
                self.tlayer(gpu, &self.layers[i], &xs, lx, Some((&xts, ltb)))?;
                self.tlayer(gpu, &self.layers_t[i], &xts, ltb, Some((&old, lx)))?;
            }
            if i == 0 {
                if let Some(p) = probe.as_mut() {
                    p("ct_l0", xs.ptr, lx * d)?;
                }
            }
        }
        let xc = gpu.alloc(lx * d * 4)?;
        n1(gpu, "tokens_to_cft_f32", lx * d, &[Arg::P(xc.ptr), Arg::P(xs.ptr), Arg::I(d as i32), Arg::I(fb as i32), Arg::I(t as i32)])?;
        let xtc = gpu.alloc(ltb * d * 4)?;
        n1(gpu, "tokens_to_cft_f32", ltb * d, &[Arg::P(xtc.ptr), Arg::P(xts.ptr), Arg::I(d as i32), Arg::I(1), Arg::I(ltb as i32)])?;
        if let Some(p) = probe.as_mut() {
            p("ct_x", xc.ptr, lx * d)?;
            p("ct_xt", xtc.ptr, ltb * d)?;
        }
        drop((xs, xts));
        let (mut x, _, _) = self.conv(gpu, &self.down, xc.ptr, fb, t, 1, 0, 1, 0, 1)?;
        let (mut xt, _, _) = self.conv(gpu, &self.down_t, xtc.ptr, 1, ltb, 1, 0, 1, 0, 1)?;
        drop((xc, xtc));
        let mut f = fb;
        let mut lt = ltb;
        for i in 0..4 {
            let (skip, _) = saved.pop().unwrap();
            let last = i == 3;
            let (z, fo) = self.decode(gpu, &self.dec[i], &x, &skip, f, t, true, 0, last)?;
            if let Some(p) = probe.as_mut() {
                p(&format!("dec{i}"), z.ptr, self.dec[i].conv_tr.co * fo * t)?;
            }
            x = z;
            f = fo;
            let (skip_t, _) = saved_t.pop().unwrap();
            let length_t = lengths_t.pop().unwrap();
            let (zt, _) = self.decode(gpu, &self.tdec[i], &xt, &skip_t, 1, lt, false, length_t, last)?;
            if let Some(p) = probe.as_mut() {
                p(&format!("tdec{i}"), zt.ptr, self.tdec[i].conv_tr.co * length_t)?;
            }
            xt = zt;
            lt = length_t;
        }
        // x: [4 sources * (2 channels * re/im)][BINS][le]; xt: [4 sources * 2 channels][length].
        let fx: Vec<f32> = gpu.read(ptr_f32(x.ptr, want * 4 * BINS * le), 4 * BINS * le)?;
        let ft: Vec<f32> = gpu.read(ptr_f32(xt.ptr, want * 2 * length), 2 * length)?;
        let mut out: [Vec<f32>; 2] = [Vec::new(), Vec::new()];
        for (ch, o) in out.iter_mut().enumerate() {
            let re = &fx[(ch * 2) * BINS * le..(ch * 2 + 1) * BINS * le];
            let im = &fx[(ch * 2 + 1) * BINS * le..(ch * 2 + 2) * BINS * le];
            let z: Vec<(f64, f64)> = re.iter().zip(im).map(|(a, b)| (*a as f64 * std + mean, *b as f64 * std + mean)).collect();
            let wav = istft(&z, le, length);
            *o = wav.iter().zip(&ft[ch * length..(ch + 1) * length]).map(|(a, b)| (*a + (*b as f64 * stdt + meant)) as f32).collect();
        }
        Ok(out)
    }
}

/// Mean and unbiased standard deviation (torch.std).
fn mean_std(v: &[f32]) -> (f64, f64) {
    let n = v.len() as f64;
    let mean = v.iter().map(|x| *x as f64).sum::<f64>() / n;
    let var = v.iter().map(|x| (*x as f64 - mean).powi(2)).sum::<f64>() / (n - 1.0);
    (mean, var.sqrt())
}

/// create_2d_sin_embedding(d, F, T) rearranged to tokens (t * F + f) x d.
fn pos_2d(d: usize, f_n: usize, t_n: usize) -> Vec<f32> {
    let half = d / 2;
    let div: Vec<f32> = (0..half / 2).map(|i| ((2 * i) as f32 * -(10000f32.ln() / half as f32)).exp()).collect();
    let mut out = vec![0f32; f_n * t_n * d];
    for t in 0..t_n {
        for f in 0..f_n {
            let row = &mut out[(t * f_n + f) * d..(t * f_n + f + 1) * d];
            for (i, dv) in div.iter().enumerate() {
                row[2 * i] = (t as f32 * dv).sin();
                row[2 * i + 1] = (t as f32 * dv).cos();
                row[half + 2 * i] = (f as f32 * dv).sin();
                row[half + 2 * i + 1] = (f as f32 * dv).cos();
            }
        }
    }
    out
}

/// create_sin_embedding(T, d): [cos | sin] of pos / 10000^(i / (half - 1)).
fn pos_1d(t_n: usize, d: usize) -> Vec<f32> {
    let half = d / 2;
    let mut out = vec![0f32; t_n * d];
    for t in 0..t_n {
        for i in 0..half {
            let phase = t as f32 / 10000f32.powf(i as f32 / (half - 1) as f32);
            out[t * d + i] = phase.cos();
            out[t * d + half + i] = phase.sin();
        }
    }
    out
}

/// In-place radix-2 complex FFT (inverse when `inv`, unnormalized).
fn fft(re: &mut [f64], im: &mut [f64], inv: bool) {
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
        let ang = 2.0 * std::f64::consts::PI / len as f64 * if inv { 1.0 } else { -1.0 };
        let (wr, wi) = (ang.cos(), ang.sin());
        for start in (0..n).step_by(len) {
            let (mut cr, mut ci) = (1.0, 0.0);
            for k in 0..len / 2 {
                let (a, b) = (start + k, start + k + len / 2);
                let (xr, xi) = (re[b] * cr - im[b] * ci, re[b] * ci + im[b] * cr);
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
                let ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
        len <<= 1;
    }
}

fn hann() -> Vec<f64> {
    (0..NFFT).map(|n| 0.5 - 0.5 * (2.0 * std::f64::consts::PI * n as f64 / NFFT as f64).cos()).collect()
}

/// HTDemucs._spec: reflect-padded, normalized STFT (hann 4096, hop 1024), last bin and the 2 + 2 edge frames dropped.
/// Result [bin][frame] for `le` frames.
fn stft(x: &[f32], le: usize) -> Vec<(f64, f64)> {
    let l = x.len() as isize;
    let pad = (HOP / 2 * 3) as isize;
    let total = le * HOP + 3 * HOP;
    let reflect = |i: isize| -> f64 {
        let mut i = i - pad;
        if i < 0 {
            i = -i;
        }
        if i >= l {
            i = 2 * (l - 1) - i;
        }
        x[i as usize] as f64
    };
    let w = hann();
    let scale = 1.0 / (NFFT as f64).sqrt();
    let mut out = vec![(0.0, 0.0); BINS * le];
    let (mut re, mut im) = (vec![0.0; NFFT], vec![0.0; NFFT]);
    for m in 0..le {
        // frame m + 2 of the centered STFT starts at m * HOP of the reflect-padded signal (length `total`)
        for n in 0..NFFT {
            let i = (m * HOP + n) as isize;
            debug_assert!((i as usize) < total);
            re[n] = reflect(i) * w[n];
            im[n] = 0.0;
        }
        fft(&mut re, &mut im, false);
        for b in 0..BINS {
            out[b * le + m] = (re[b] * scale, im[b] * scale);
        }
    }
    out
}

/// HTDemucs._ispec for one channel: z [bin][frame] (le frames) -> `length` samples.
fn istft(z: &[(f64, f64)], le: usize, length: usize) -> Vec<f64> {
    let frames = le + 4;
    let w = hann();
    let ola_len = NFFT + HOP * (frames - 1);
    let mut ola = vec![0.0; ola_len];
    let mut env = vec![0.0; ola_len];
    let scale = (NFFT as f64).sqrt() / NFFT as f64;
    let (mut re, mut im) = (vec![0.0; NFFT], vec![0.0; NFFT]);
    for k in 0..frames {
        for n in 0..NFFT {
            env[k * HOP + n] += w[n] * w[n];
        }
        if k < 2 || k >= le + 2 {
            continue;
        }
        let m = k - 2;
        re.iter_mut().for_each(|v| *v = 0.0);
        im.iter_mut().for_each(|v| *v = 0.0);
        for b in 0..BINS {
            let (a, c) = z[b * le + m];
            re[b] = a;
            im[b] = if b == 0 { 0.0 } else { c };
            if b > 0 {
                re[NFFT - b] = a;
                im[NFFT - b] = -c;
            }
        }
        // bin NFFT/2 is the zero-padded Nyquist bin
        fft(&mut re, &mut im, true);
        for n in 0..NFFT {
            ola[k * HOP + n] += re[n] * scale * w[n];
        }
    }
    let pad = HOP / 2 * 3;
    let start = NFFT / 2 + pad;
    (0..length).map(|s| {
        let i = start + s;
        if env[i] > 1e-11 { ola[i] / env[i] } else { 0.0 }
    }).collect()
}

/// A bag of models (one per source) and the apply_model segmentation.
pub struct Separator {
    models: Vec<Model>,
}

impl Separator {
    pub fn load(gpu: &Gpu, dir: &Path) -> Result<Self> {
        let mut models = Vec::new();
        for id in BAG {
            models.push(Model::load(gpu, &dir.join(format!("{id}.safetensors")))?);
        }
        Ok(Self { models })
    }

    /// One model over the whole (normalized) mix: 25 % overlapping segments, triangular weights.
    fn apply(&self, gpu: &Gpu, model: &Model, mix: &[Vec<f32>; 2], want: usize, cancel: &dyn Fn() -> bool) -> Result<[Vec<f32>; 2]> {
        let len = mix[0].len();
        let seg = SEGMENT;
        let stride = (0.75 * seg as f64) as usize;
        let weight: Vec<f32> = {
            let half = seg / 2;
            let w: Vec<f32> = (1..=half).map(|v| v as f32).chain((1..=seg - half).rev().map(|v| v as f32)).collect();
            let mx = w.iter().cloned().fold(0f32, f32::max);
            w.into_iter().map(|v| v / mx).collect()
        };
        let mut out = [vec![0f32; len], vec![0f32; len]];
        let mut sum = vec![0f32; len];
        let mut offset = 0;
        while offset < len {
            if cancel() {
                bail!("cancelled");
            }
            let chunk = seg.min(len - offset);
            // TensorChunk.padded: centered on the chunk, real neighbours where they exist, zeros beyond the mix.
            let delta = seg - chunk;
            let start = offset as isize - (delta / 2) as isize;
            let input: [Vec<f32>; 2] = std::array::from_fn(|c| {
                (0..seg).map(|i| {
                    let j = start + i as isize;
                    if j < 0 || j as usize >= len { 0.0 } else { mix[c][j as usize] }
                }).collect()
            });
            let y = model.forward(gpu, &input, want, None)?;
            // center_trim back to the chunk
            let trim = delta / 2;
            for c in 0..2 {
                for i in 0..chunk {
                    out[c][offset + i] += weight[i] * y[c][trim + i];
                }
            }
            for i in 0..chunk {
                sum[offset + i] += weight[i];
            }
            offset += stride;
        }
        for c in 0..2 {
            for (v, s) in out[c].iter_mut().zip(&sum) {
                *v /= *s;
            }
        }
        Ok(out)
    }

    /// Stereo mix at 44.1 kHz -> every source (demucs.api normalization: the mono mean / std of the whole mix).
    pub fn separate(&self, gpu: &Gpu, mix: &[Vec<f32>; 2], cancel: &dyn Fn() -> bool) -> Result<Vec<[Vec<f32>; 2]>> {
        let mono: Vec<f32> = mix[0].iter().zip(&mix[1]).map(|(a, b)| (a + b) / 2.0).collect();
        let (mean, std) = mean_std(&mono);
        let std = std + 1e-8;
        let norm: [Vec<f32>; 2] = std::array::from_fn(|c| mix[c].iter().map(|v| ((*v as f64 - mean) / std) as f32).collect());
        let mut out = Vec::new();
        for (s, m) in self.models.iter().enumerate() {
            let y = self.apply(gpu, m, &norm, s, cancel)?;
            out.push(std::array::from_fn(|c| y[c].iter().map(|v| (*v as f64 * std + mean) as f32).collect()));
        }
        Ok(out)
    }
}

/// Parity against demucs_dump.py: model 3 on the first segment (intermediates), then the whole bag.
pub fn check(gpu: &Gpu, dir: &Path, refdir: &Path) -> Result<serde_json::Value> {
    use super::mula::{compare, npy};
    let mut report = serde_json::Map::new();
    let sep = Separator::load(gpu, dir)?;
    let mix = npy(&refdir.join("mix44.npy"))?;
    let len = mix.len() / 2;
    let mix: [Vec<f32>; 2] = [mix[..len].to_vec(), mix[len..].to_vec()];
    let meta: serde_json::Value = serde_json::from_slice(&std::fs::read(refdir.join("meta.json"))?)?;
    let (mean, std) = (meta["mean"].as_f64().unwrap(), meta["std"].as_f64().unwrap());
    let seg: [Vec<f32>; 2] = std::array::from_fn(|c| mix[c][..SEGMENT].iter().map(|v| ((*v as f64 - mean) / std) as f32).collect());
    let mut probe = |name: &str, ptr: u64, n: usize| -> Result<()> {
        let p = refdir.join(format!("{name}.npy"));
        if !p.is_file() {
            return Ok(());
        }
        let refv = npy(&p)?;
        let mine: Vec<f32> = gpu.read(ptr, n)?;
        report.insert(name.into(), compare(&mine, &refv));
        Ok(())
    };
    let y = sep.models[3].forward(gpu, &seg, 3, Some(&mut probe))?;
    let full = npy(&refdir.join("model3_out.npy"))?;
    let refv: Vec<f32> = full[3 * 2 * SEGMENT..4 * 2 * SEGMENT].to_vec();
    let mine: Vec<f32> = y[0].iter().chain(y[1].iter()).copied().collect();
    report.insert("model3_vocals".into(), compare(&mine, &refv));
    let t = std::time::Instant::now();
    let all = sep.separate(gpu, &mix, &|| false)?;
    report.insert("separate_seconds".into(), t.elapsed().as_secs_f64().into());
    let refs = npy(&refdir.join("sources.npy"))?;
    for (s, src) in all.iter().enumerate() {
        let mine: Vec<f32> = src[0].iter().chain(src[1].iter()).copied().collect();
        report.insert(format!("source_{}", SOURCES[s]), compare(&mine, &refs[s * 2 * len..(s + 1) * 2 * len]));
    }
    Ok(report.into())
}
