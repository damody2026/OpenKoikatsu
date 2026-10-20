//! ViT-H/16 backbone shared by ViTPose-H and HMR2 (Docs/VIDEO_MOTION_NATIVE_SPEC.md §3–4),
//! the ViTPose heatmap head with flip test and UDP+DARK decoding, and the HMR2 cross-attention
//! token decoder that produces GVHMR's 1024-d image feature.

use crate::crop;
use candle_core::{DType, Device, Tensor};
use std::collections::HashMap;
use std::path::Path;

type R<T> = Result<T, String>;
fn e<E: std::fmt::Display>(x: E) -> String { x.to_string() }

pub struct Weights {
    w: HashMap<String, Tensor>,
    what: &'static str,
}

impl Weights {
    pub fn load(path: &Path, dev: &Device, what: &'static str, keep: &[&str]) -> R<Self> {
        let raw = candle_core::safetensors::load(path, dev).map_err(|x| format!("{}：{x}", path.display()))?;
        let mut w = HashMap::new();
        for (k, v) in raw {
            if keep.iter().any(|p| k.starts_with(p)) && v.dtype() != DType::I64 {
                w.insert(k, v.to_dtype(DType::F32).map_err(e)?);
            }
        }
        let s = Weights { w, what };
        s.p("backbone.blocks.31.mlp.fc2.weight")?;
        Ok(s)
    }
    fn p(&self, k: &str) -> R<&Tensor> { self.w.get(k).ok_or_else(|| format!("{} 權重缺少 {k}", self.what)) }
    fn linear(&self, x: &Tensor, name: &str, bias: bool) -> R<Tensor> {
        let y = x.broadcast_matmul(&self.p(&format!("{name}.weight"))?.t().map_err(e)?).map_err(e)?;
        if bias { y.broadcast_add(self.p(&format!("{name}.bias"))?).map_err(e) } else { Ok(y) }
    }
    fn ln(&self, x: &Tensor, name: &str, eps: f32) -> R<Tensor> {
        candle_nn::ops::layer_norm(x, self.p(&format!("{name}.weight"))?, self.p(&format!("{name}.bias"))?, eps).map_err(e)
    }

    /// (B,3,256,192) → tokens (B,192,1280) after last_norm.
    pub fn backbone(&self, x: &Tensor) -> R<Tensor> {
        let b = x.dim(0).map_err(e)?;
        let x = x.conv2d(self.p("backbone.patch_embed.proj.weight")?, 2, 16, 1, 1).map_err(e)?;
        let x = x.broadcast_add(&self.p("backbone.patch_embed.proj.bias")?.reshape((1, 1280, 1, 1)).map_err(e)?).map_err(e)?;
        let x = x.flatten_from(2).and_then(|t| t.transpose(1, 2)).map_err(e)?; // (B,192,1280)
        let pos = self.p("backbone.pos_embed")?;
        let pos = pos.narrow(1, 1, 192).and_then(|p| p.broadcast_add(&pos.narrow(1, 0, 1)?)).map_err(e)?;
        let mut x = x.broadcast_add(&pos).map_err(e)?;
        for i in 0..32 {
            let n = format!("backbone.blocks.{i}");
            let h = self.ln(&x, &format!("{n}.norm1"), 1e-6)?;
            let qkv = self.linear(&h, &format!("{n}.attn.qkv"), true)?; // (B,192,3840)
            let qkv = qkv.reshape((b, 192, 3, 16, 80)).and_then(|t| t.permute((2, 0, 3, 1, 4))).map_err(e)?;
            let q = qkv.get(0).and_then(|t| t.contiguous()).map_err(e)?;
            let k = qkv.get(1).and_then(|t| t.contiguous()).map_err(e)?;
            let v = qkv.get(2).and_then(|t| t.contiguous()).map_err(e)?;
            let s = (q.matmul(&k.t().map_err(e)?).map_err(e)? * (80f64).powf(-0.5)).map_err(e)?;
            let a = candle_nn::ops::softmax_last_dim(&s).and_then(|s| s.matmul(&v)).map_err(e)?;
            let a = a.transpose(1, 2).and_then(|t| t.reshape((b, 192, 1280))).map_err(e)?;
            x = (x + self.linear(&a, &format!("{n}.attn.proj"), true)?).map_err(e)?;
            let h = self.ln(&x, &format!("{n}.norm2"), 1e-6)?;
            let h = self.linear(&h, &format!("{n}.mlp.fc1"), true)?.gelu_erf().map_err(e)?;
            x = (x + self.linear(&h, &format!("{n}.mlp.fc2"), true)?).map_err(e)?;
        }
        self.ln(&x, "backbone.last_norm", 1e-6)
    }

    fn batch_norm(&self, x: &Tensor, name: &str) -> R<Tensor> {
        let c = |k: &str| -> R<Tensor> { self.p(&format!("{name}.{k}"))?.reshape((1, (), 1, 1)).map_err(e) };
        let inv = (c("running_var")? + 1e-5).and_then(|v| v.sqrt()).and_then(|v| v.recip()).map_err(e)?;
        let (mean, w, b) = (c("running_mean")?, c("weight")?, c("bias")?);
        x.broadcast_sub(&mean).and_then(|t| t.broadcast_mul(&inv)).and_then(|t| t.broadcast_mul(&w)).and_then(|t| t.broadcast_add(&b)).map_err(e)
    }

    /// ViTPose heatmaps (B,17,64,48) from tokens.
    fn heatmaps(&self, tokens: &Tensor) -> R<Tensor> {
        let b = tokens.dim(0).map_err(e)?;
        let x = tokens.transpose(1, 2).and_then(|t| t.reshape((b, 1280, 16, 12))).map_err(e)?;
        let x = x.conv_transpose2d(self.p("keypoint_head.deconv_layers.0.weight")?, 1, 0, 2, 1).map_err(e)?;
        let x = self.batch_norm(&x, "keypoint_head.deconv_layers.1")?.relu().map_err(e)?;
        let x = x.conv_transpose2d(self.p("keypoint_head.deconv_layers.3.weight")?, 1, 0, 2, 1).map_err(e)?;
        let x = self.batch_norm(&x, "keypoint_head.deconv_layers.4")?.relu().map_err(e)?;
        let x = x.conv2d(self.p("keypoint_head.final_layer.weight")?, 0, 1, 1, 1).map_err(e)?;
        x.broadcast_add(&self.p("keypoint_head.final_layer.bias")?.reshape((1, 17, 1, 1)).map_err(e)?).map_err(e)
    }

    /// Flip-tested heatmaps for a crop batch (B,3,256,192): (B,17,64,48) on the CPU.
    pub fn vitpose(&self, x: &Tensor) -> R<Vec<f32>> {
        let dev = x.device();
        let flip_w = |n: usize| Tensor::from_vec((0..n as u32).rev().collect::<Vec<_>>(), n, dev).map_err(e);
        let xf = x.index_select(&flip_w(192)?, 3).map_err(e)?;
        let b = x.dim(0).map_err(e)?;
        let both = Tensor::cat(&[x, &xf], 0).map_err(e)?;
        let heat = self.heatmaps(&self.backbone(&both)?)?;
        let h = heat.narrow(0, 0, b).map_err(e)?;
        let hf = heat.narrow(0, b, b).map_err(e)?;
        let swap: Vec<u32> = vec![0, 2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15];
        let hf = hf.index_select(&Tensor::from_vec(swap, 17, dev).map_err(e)?, 1).and_then(|t| t.index_select(&flip_w(48).unwrap(), 3)).map_err(e)?;
        ((h + hf).map_err(e)? * 0.5).and_then(|t| t.flatten_all()).and_then(|t| t.to_vec1::<f32>()).map_err(e)
    }

    /// HMR2 feature (B,1024): one zero token through the 6-layer cross-attention decoder.
    pub fn hmr2(&self, x: &Tensor) -> R<Vec<f32>> {
        let ctx = self.backbone(x)?;
        let b = ctx.dim(0).map_err(e)?;
        let dev = ctx.device();
        let t = "smpl_head.transformer";
        let tok = self.p(&format!("{t}.to_token_embedding.bias"))?.broadcast_add(&self.p(&format!("{t}.pos_embedding"))?.reshape(1024).map_err(e)?).map_err(e)?;
        let mut x = tok.reshape((1, 1, 1024)).and_then(|t| t.broadcast_as((b, 1, 1024))).and_then(|t| t.contiguous()).map_err(e)?;
        let _ = dev;
        let heads = |t: Tensor, n: usize| -> R<Tensor> { t.reshape((b, n, 8, 64)).and_then(|t| t.transpose(1, 2)).and_then(|t| t.contiguous()).map_err(e) };
        let merge = |t: Tensor| -> R<Tensor> { t.transpose(1, 2).and_then(|t| t.reshape((b, 1, 512))).map_err(e) };
        for i in 0..6 {
            let n = format!("{t}.transformer.layers.{i}");
            // self-attention
            let h = self.ln(&x, &format!("{n}.0.norm"), 1e-5)?;
            let qkv = self.linear(&h, &format!("{n}.0.fn.to_qkv"), false)?;
            let q = heads(qkv.narrow(2, 0, 512).map_err(e)?, 1)?;
            let k = heads(qkv.narrow(2, 512, 512).map_err(e)?, 1)?;
            let v = heads(qkv.narrow(2, 1024, 512).map_err(e)?, 1)?;
            let a = candle_nn::ops::softmax_last_dim(&(q.matmul(&k.t().map_err(e)?).map_err(e)? * 0.125).map_err(e)?).and_then(|s| s.matmul(&v)).map_err(e)?;
            x = (x + self.linear(&merge(a)?, &format!("{n}.0.fn.to_out.0"), true)?).map_err(e)?;
            // cross-attention on the (unnormalised) backbone tokens
            let h = self.ln(&x, &format!("{n}.1.norm"), 1e-5)?;
            let q = heads(self.linear(&h, &format!("{n}.1.fn.to_q"), false)?, 1)?;
            let kv = self.linear(&ctx, &format!("{n}.1.fn.to_kv"), false)?;
            let k = heads(kv.narrow(2, 0, 512).map_err(e)?, 192)?;
            let v = heads(kv.narrow(2, 512, 512).map_err(e)?, 192)?;
            let a = candle_nn::ops::softmax_last_dim(&(q.matmul(&k.t().map_err(e)?).map_err(e)? * 0.125).map_err(e)?).and_then(|s| s.matmul(&v)).map_err(e)?;
            x = (x + self.linear(&merge(a)?, &format!("{n}.1.fn.to_out.0"), true)?).map_err(e)?;
            // feed-forward
            let h = self.ln(&x, &format!("{n}.2.norm"), 1e-5)?;
            let h = self.linear(&h, &format!("{n}.2.fn.net.0"), true)?.gelu_erf().map_err(e)?;
            x = (x + self.linear(&h, &format!("{n}.2.fn.net.3"), true)?).map_err(e)?;
        }
        x.flatten_all().and_then(|t| t.to_vec1::<f32>()).map_err(e)
    }
}

/// Batch of crops (B × 3·256·192) → device tensor.
pub fn crops_tensor(crops: &[f32], b: usize, dev: &Device) -> R<Tensor> {
    Tensor::from_slice(crops, (b, 3, crop::H, crop::W), dev).map_err(e)
}

// ---------------------------------------------------------------- heatmap decoding

const HM_H: usize = 64;
const HM_W: usize = 48;

fn blur11(src: &[f32]) -> Vec<f32> {
    // cv2.GaussianBlur((11,11), 0) on float32: sigma = 2.0, BORDER_REFLECT_101.
    let mut k = [0f64; 11];
    for (i, v) in k.iter_mut().enumerate() {
        let x = i as f64 - 5.;
        *v = (-x * x / 8.).exp();
    }
    let s: f64 = k.iter().sum();
    let k: Vec<f32> = k.iter().map(|v| (v / s) as f32).collect();
    let r = |i: isize, n: usize| -> usize {
        let n = n as isize;
        let mut i = i;
        while i < 0 || i >= n {
            i = if i < 0 { -i } else { 2 * n - 2 - i };
        }
        i as usize
    };
    let mut tmp = vec![0f32; HM_H * HM_W];
    for y in 0..HM_H {
        for x in 0..HM_W {
            tmp[y * HM_W + x] = (0..11).map(|i| k[i] * src[y * HM_W + r(x as isize + i as isize - 5, HM_W)]).sum();
        }
    }
    let mut out = vec![0f32; HM_H * HM_W];
    for y in 0..HM_H {
        for x in 0..HM_W {
            out[y * HM_W + x] = (0..11).map(|i| k[i] * tmp[r(y as isize + i as isize - 5, HM_H) * HM_W + x]).sum();
        }
    }
    out
}

/// mmpose `keypoints_from_heatmaps(use_udp, post_dark_udp kernel 11)` for one frame's 17 maps,
/// mapped back with `transform_preds(use_udp)` into full-image pixels for box (cx, cy, s).
pub fn decode(heat: &[f32], bbx: [f64; 3]) -> [[f32; 3]; 17] {
    let mut out = [[0f32; 3]; 17];
    for j in 0..17 {
        let hm = &heat[j * HM_H * HM_W..(j + 1) * HM_H * HM_W];
        let (mut idx, mut maxv) = (0usize, f32::NEG_INFINITY);
        for (i, v) in hm.iter().enumerate() {
            if *v > maxv {
                maxv = *v;
                idx = i;
            }
        }
        let (mut px, mut py) = ((idx % HM_W) as f64, (idx / HM_W) as f64);
        if maxv > 0. {
            let lg: Vec<f32> = blur11(hm).iter().map(|v| v.clamp(0.001, 50.).ln()).collect();
            let at = |x: isize, y: isize| -> f64 { lg[(y.clamp(0, HM_H as isize - 1) as usize) * HM_W + x.clamp(0, HM_W as isize - 1) as usize] as f64 };
            let (x, y) = (px as isize, py as isize);
            let i = at(x, y);
            let (ix1, ix1_) = (at(x + 1, y), at(x - 1, y));
            let (iy1, iy1_) = (at(x, y + 1), at(x, y - 1));
            let (ix1y1, ix1_y1_) = (at(x + 1, y + 1), at(x - 1, y - 1));
            let dx = 0.5 * (ix1 - ix1_);
            let dy = 0.5 * (iy1 - iy1_);
            let dxx = ix1 - 2. * i + ix1_;
            let dyy = iy1 - 2. * i + iy1_;
            let dxy = 0.5 * (ix1y1 - ix1 - iy1 + i + i - ix1_ - iy1_ + ix1_y1_);
            let eps = f32::EPSILON as f64;
            let (a, b, c, d) = (dxx + eps, dxy, dxy, dyy + eps);
            let det = a * d - b * c;
            if det != 0. {
                px -= (d * dx - b * dy) / det;
                py -= (-c * dx + a * dy) / det;
            }
        } else {
            px = -1.;
            py = -1.;
        }
        let [cx, cy, s] = bbx;
        let x = px * (0.75 * s / (HM_W as f64 - 1.)) + cx - 0.375 * s;
        let y = py * (s / (HM_H as f64 - 1.)) + cy - 0.5 * s;
        out[j] = [x as f32, y as f32, maxv];
    }
    out
}
