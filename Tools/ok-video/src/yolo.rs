//! YOLOv8x person detector (Docs/VIDEO_MOTION_NATIVE_SPEC.md §2.1–2.3): ultralytics letterbox,
//! the yolov8x graph with BatchNorm folded into the convolutions, DFL box decoding and NMS.

use candle_core::{DType, Device, Tensor, D};
use std::collections::HashMap;
use std::path::Path;

type R<T> = Result<T, String>;
fn e<E: std::fmt::Display>(x: E) -> String { x.to_string() }

#[derive(Clone, Copy, Debug)]
pub struct Det {
    /// x1, y1, x2, y2 in full-image pixels
    pub b: [f32; 4],
    pub score: f32,
}

struct Conv {
    w: Tensor,
    b: Tensor,
    stride: usize,
    pad: usize,
    act: bool,
}

impl Conv {
    fn fwd(&self, x: &Tensor) -> R<Tensor> {
        let y = x.conv2d(&self.w, self.pad, self.stride, 1, 1).map_err(e)?;
        let y = y.broadcast_add(&self.b).map_err(e)?;
        if self.act { y.silu().map_err(e) } else { Ok(y) }
    }
}

pub struct Yolo {
    convs: HashMap<String, Conv>,
    dev: Device,
}

impl Yolo {
    pub fn load(path: &Path, dev: &Device) -> R<Self> {
        let raw = candle_core::safetensors::load(path, &Device::Cpu).map_err(|x| format!("{}：{x}", path.display()))?;
        let get = |k: &str| -> R<Tensor> { raw.get(k).ok_or_else(|| format!("YOLO 權重缺少 {k}"))?.to_dtype(DType::F32).map_err(e) };
        let mut convs = HashMap::new();
        for key in raw.keys() {
            let Some(base) = key.strip_suffix(".conv.weight") else { continue };
            if base.ends_with(".dfl") {
                continue; // fixed arange(16) weights, applied directly in forward
            }
            // Conv(c1,c2,k,s) + BatchNorm2d(eps=1e-3) folded.
            let w = get(key)?;
            let k = w.dim(2).map_err(e)?;
            let gamma = get(&format!("{base}.bn.weight"))?;
            let beta = get(&format!("{base}.bn.bias"))?;
            let mean = get(&format!("{base}.bn.running_mean"))?;
            let var = get(&format!("{base}.bn.running_var"))?;
            let scale = (gamma / (var + 1e-3).map_err(e)?.sqrt().map_err(e)?).map_err(e)?;
            let w = w.broadcast_mul(&scale.reshape(((), 1, 1, 1)).map_err(e)?).map_err(e)?;
            let b = (beta - (mean * &scale).map_err(e)?).map_err(e)?;
            let stride = if Self::strided(base) { 2 } else { 1 };
            convs.insert(base.to_string(), Conv { w: w.to_device(dev).map_err(e)?, b: b.reshape((1, (), 1, 1)).and_then(|b| b.to_device(dev)).map_err(e)?, stride, pad: k / 2, act: true });
        }
        // Plain Conv2d heads (1×1 with bias) and the fixed DFL weights.
        for s in 0..3 {
            for br in ["cv2", "cv3"] {
                let base = format!("model.22.{br}.{s}.2");
                let w = get(&format!("{base}.weight"))?.to_device(dev).map_err(e)?;
                let b = get(&format!("{base}.bias"))?.reshape((1, (), 1, 1)).and_then(|b| b.to_device(dev)).map_err(e)?;
                convs.insert(base, Conv { w, b, stride: 1, pad: 0, act: false });
            }
        }
        if !convs.contains_key("model.21.cv2") {
            return Err(format!("{} 不是 yolov8x 權重", path.display()));
        }
        Ok(Yolo { convs, dev: dev.clone() })
    }

    fn strided(base: &str) -> bool {
        matches!(base, "model.0" | "model.1" | "model.3" | "model.5" | "model.7" | "model.16" | "model.19")
    }
    fn c(&self, name: &str, x: &Tensor) -> R<Tensor> {
        self.convs.get(name).ok_or_else(|| format!("YOLO 缺少卷積 {name}"))?.fwd(x)
    }
    fn c2f(&self, i: usize, x: &Tensor, n: usize, shortcut: bool) -> R<Tensor> {
        let y = self.c(&format!("model.{i}.cv1"), x)?;
        let half = y.dim(1).map_err(e)? / 2;
        let mut ys = vec![y.narrow(1, 0, half).map_err(e)?, y.narrow(1, half, half).map_err(e)?];
        for k in 0..n {
            let last = ys.last().unwrap().clone();
            let h = self.c(&format!("model.{i}.m.{k}.cv1"), &last)?;
            let h = self.c(&format!("model.{i}.m.{k}.cv2"), &h)?;
            ys.push(if shortcut { (last + h).map_err(e)? } else { h });
        }
        self.c(&format!("model.{i}.cv2"), &Tensor::cat(&ys, 1).map_err(e)?)
    }
    fn sppf(&self, x: &Tensor) -> R<Tensor> {
        let x = self.c("model.9.cv1", x)?;
        let pool = |t: &Tensor| -> R<Tensor> { t.pad_with_same(2, 2, 2).and_then(|t| t.pad_with_same(3, 2, 2)).and_then(|t| t.max_pool2d_with_stride(5, 1)).map_err(e) };
        let y1 = pool(&x)?;
        let y2 = pool(&y1)?;
        let y3 = pool(&y2)?;
        self.c("model.9.cv2", &Tensor::cat(&[&x, &y1, &y2, &y3], 1).map_err(e)?)
    }
    fn up(x: &Tensor) -> R<Tensor> {
        let (_, _, h, w) = x.dims4().map_err(e)?;
        x.upsample_nearest2d(h * 2, w * 2).map_err(e)
    }

    /// (B,3,H,W) letterboxed RGB in [0,1] → (B, 84, A) like ultralytics (xywh pixels + class scores).
    fn forward(&self, x: &Tensor) -> R<Tensor> {
        let x0 = self.c("model.0", x)?;
        let x1 = self.c("model.1", &x0)?;
        let x2 = self.c2f(2, &x1, 3, true)?;
        let x3 = self.c("model.3", &x2)?;
        let x4 = self.c2f(4, &x3, 6, true)?;
        let x5 = self.c("model.5", &x4)?;
        let x6 = self.c2f(6, &x5, 6, true)?;
        let x7 = self.c("model.7", &x6)?;
        let x8 = self.c2f(8, &x7, 3, true)?;
        let x9 = self.sppf(&x8)?;
        let x12 = self.c2f(12, &Tensor::cat(&[&Self::up(&x9)?, &x6], 1).map_err(e)?, 3, false)?;
        let x15 = self.c2f(15, &Tensor::cat(&[&Self::up(&x12)?, &x4], 1).map_err(e)?, 3, false)?;
        let x18 = self.c2f(18, &Tensor::cat(&[&self.c("model.16", &x15)?, &x12], 1).map_err(e)?, 3, false)?;
        let x21 = self.c2f(21, &Tensor::cat(&[&self.c("model.19", &x18)?, &x9], 1).map_err(e)?, 3, false)?;
        let mut outs = Vec::new();
        let dev = &self.dev;
        for (s, (feat, stride)) in [(&x15, 8f64), (&x18, 16.), (&x21, 32.)].into_iter().enumerate() {
            let (b, _, h, w) = feat.dims4().map_err(e)?;
            let bx = self.c(&format!("model.22.cv2.{s}.2"), &self.c(&format!("model.22.cv2.{s}.1"), &self.c(&format!("model.22.cv2.{s}.0"), feat)?)?)?;
            let cl = self.c(&format!("model.22.cv3.{s}.2"), &self.c(&format!("model.22.cv3.{s}.1"), &self.c(&format!("model.22.cv3.{s}.0"), feat)?)?)?;
            // DFL: softmax over 16 bins, expectation of 0..15.
            let dist = bx.reshape((b, 4, 16, h * w)).map_err(e)?;
            let dist = candle_nn::ops::softmax(&dist, 2).map_err(e)?;
            let bins = Tensor::arange(0f32, 16., dev).map_err(e)?.reshape((1, 1, 16, 1)).map_err(e)?;
            let dist = dist.broadcast_mul(&bins).and_then(|t| t.sum(2)).map_err(e)?; // (B,4,hw)
            let mut ax = Vec::with_capacity(h * w);
            let mut ay = Vec::with_capacity(h * w);
            for y in 0..h {
                for x in 0..w {
                    ax.push(x as f32 + 0.5);
                    ay.push(y as f32 + 0.5);
                }
            }
            let ax = Tensor::from_vec(ax, (1, h * w), dev).map_err(e)?;
            let ay = Tensor::from_vec(ay, (1, h * w), dev).map_err(e)?;
            let g = |i: usize| dist.narrow(1, i, 1).and_then(|t| t.squeeze(1)).map_err(e);
            let (l, t, r, bt) = (g(0)?, g(1)?, g(2)?, g(3)?);
            let x1 = ax.broadcast_sub(&l).map_err(e)?;
            let y1 = ay.broadcast_sub(&t).map_err(e)?;
            let x2 = ax.broadcast_add(&r).map_err(e)?;
            let y2 = ay.broadcast_add(&bt).map_err(e)?;
            let cx = ((&x1 + &x2).map_err(e)? * (0.5 * stride)).map_err(e)?;
            let cy = ((&y1 + &y2).map_err(e)? * (0.5 * stride)).map_err(e)?;
            let ww = ((&x2 - &x1).map_err(e)? * stride).map_err(e)?;
            let hh = ((&y2 - &y1).map_err(e)? * stride).map_err(e)?;
            let boxes = Tensor::stack(&[cx, cy, ww, hh], 1).map_err(e)?; // (B,4,hw)
            let scores = candle_nn::ops::sigmoid(&cl.reshape((b, 80, h * w)).map_err(e)?).map_err(e)?;
            outs.push(Tensor::cat(&[boxes, scores], 1).map_err(e)?);
        }
        Tensor::cat(&outs, D::Minus1).map_err(e)
    }

    /// Person detections for one RGB frame (packed u8, full resolution).
    pub fn detect(&self, rgb: &[u8], w: usize, h: usize, conf: f32) -> R<Vec<Det>> {
        let lb = letterbox(rgb, w, h);
        let x = Tensor::from_slice(&lb.data, (1, 3, lb.h, lb.w), &self.dev).map_err(e)?;
        let out = self.forward(&x)?.squeeze(0).map_err(e)?; // (84, A)
        let a = out.dim(1).map_err(e)?;
        let v = out.flatten_all().and_then(|t| t.to_vec1::<f32>()).map_err(e)?;
        let mut cand = Vec::new();
        for i in 0..a {
            // best class must be "person" and above the threshold (ultralytics multi_label=False)
            let mut best = 0;
            let mut bs = v[4 * a + i];
            for c in 1..80 {
                let s = v[(4 + c) * a + i];
                if s > bs {
                    bs = s;
                    best = c;
                }
            }
            if best != 0 || bs <= conf {
                continue;
            }
            let (cx, cy, bw, bh) = (v[i], v[a + i], v[2 * a + i], v[3 * a + i]);
            cand.push(Det { b: [cx - bw / 2., cy - bh / 2., cx + bw / 2., cy + bh / 2.], score: bs });
        }
        let mut keep = nms(cand, 0.7);
        keep.truncate(300);
        for d in &mut keep {
            d.b = lb.unmap(d.b, w, h);
        }
        Ok(keep)
    }
}

pub fn iou(a: &[f32; 4], b: &[f32; 4]) -> f32 {
    let iw = (a[2].min(b[2]) - a[0].max(b[0])).max(0.);
    let ih = (a[3].min(b[3]) - a[1].max(b[1])).max(0.);
    let inter = iw * ih;
    let ua = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter;
    if ua > 0. { inter / ua } else { 0. }
}

/// Greedy NMS by score; suppress IoU > thr.
pub fn nms(mut d: Vec<Det>, thr: f32) -> Vec<Det> {
    d.sort_by(|a, b| b.score.partial_cmp(&a.score).unwrap());
    let mut keep: Vec<Det> = Vec::new();
    'outer: for c in d {
        for k in &keep {
            if iou(&c.b, &k.b) > thr {
                continue 'outer;
            }
        }
        keep.push(c);
    }
    keep
}

struct Letterbox {
    data: Vec<f32>,
    w: usize,
    h: usize,
    nw: usize,
    nh: usize,
    left: usize,
    top: usize,
}

/// Python round(): half to even.
fn round_even(x: f64) -> f64 {
    let r = x.round();
    if (x - x.trunc()).abs() == 0.5 { 2. * (x / 2.).round() } else { r }
}

/// ultralytics LetterBox(640, auto=True, stride=32, center=True, pad 114) + /255, RGB CHW.
fn letterbox(rgb: &[u8], w: usize, h: usize) -> Letterbox {
    let r = f64::min(640. / h as f64, 640. / w as f64);
    let nw = round_even(w as f64 * r) as usize;
    let nh = round_even(h as f64 * r) as usize;
    let dw = ((640 - nw) % 32) as f64 / 2.;
    let dh = ((640 - nh) % 32) as f64 / 2.;
    let (top, bottom) = (round_even(dh - 0.1) as usize, round_even(dh + 0.1) as usize);
    let (left, right) = (round_even(dw - 0.1) as usize, round_even(dw + 0.1) as usize);
    let (ow, oh) = (nw + left + right, nh + top + bottom);
    let mut data = vec![114f32 / 255.; 3 * ow * oh];
    // cv2.resize INTER_LINEAR (half-pixel centres)
    let (sx, sy) = (w as f64 / nw as f64, h as f64 / nh as f64);
    for y in 0..nh {
        let fy = ((y as f64 + 0.5) * sy - 0.5).max(0.);
        let y0 = (fy as usize).min(h - 1);
        let y1 = (y0 + 1).min(h - 1);
        let ay = fy - y0 as f64;
        for x in 0..nw {
            let fx = ((x as f64 + 0.5) * sx - 0.5).max(0.);
            let x0 = (fx as usize).min(w - 1);
            let x1 = (x0 + 1).min(w - 1);
            let ax = fx - x0 as f64;
            for c in 0..3 {
                let p = |xx: usize, yy: usize| rgb[(yy * w + xx) * 3 + c] as f64;
                let v = (1. - ay) * ((1. - ax) * p(x0, y0) + ax * p(x1, y0)) + ay * ((1. - ax) * p(x0, y1) + ax * p(x1, y1));
                data[c * ow * oh + (y + top) * ow + x + left] = v.round().clamp(0., 255.) as f32 / 255.;
            }
        }
    }
    Letterbox { data, w: ow, h: oh, nw, nh, left, top }
}

impl Letterbox {
    /// scale_boxes back to the original frame, clipped.
    fn unmap(&self, b: [f32; 4], w: usize, h: usize) -> [f32; 4] {
        let (gx, gy) = (self.nw as f32 / w as f32, self.nh as f32 / h as f32);
        let x = |v: f32| ((v - self.left as f32) / gx).clamp(0., w as f32);
        let y = |v: f32| ((v - self.top as f32) / gy).clamp(0., h as f32);
        [x(b[0]), y(b[1]), x(b[2]), y(b[3])]
    }
}
