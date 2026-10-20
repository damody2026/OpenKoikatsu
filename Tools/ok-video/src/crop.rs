//! `vitfeat_extractor.get_batch` crop (Docs/VIDEO_MOTION_NATIVE_SPEC.md §3.1), shared by
//! ViTPose and HMR2: optional 5×5 Gaussian anti-alias on the half-resolution frame, the
//! isotropic affine to 256×256 (bilinear, border 0), ImageNet normalisation, and the
//! central 192 columns both networks use. Output is CHW f32 (3, 256, 192).

pub const H: usize = 256;
pub const W: usize = 192;
const MEAN: [f32; 3] = [0.485, 0.456, 0.406];
const STD: [f32; 3] = [0.229, 0.224, 0.225];

fn reflect101(i: isize, n: usize) -> usize {
    let n = n as isize;
    if n == 1 {
        return 0;
    }
    let mut i = i;
    loop {
        if i < 0 {
            i = -i;
        } else if i >= n {
            i = 2 * n - 2 - i;
        } else {
            return i as usize;
        }
    }
}

/// cv2.GaussianBlur(img, (5,5), sigma) on packed RGB u8, BORDER_REFLECT_101.
pub fn blur5(img: &[u8], w: usize, h: usize, sigma: f64) -> Vec<u8> {
    let mut k = [0f64; 5];
    for (i, v) in k.iter_mut().enumerate() {
        let x = i as f64 - 2.;
        *v = (-x * x / (2. * sigma * sigma)).exp();
    }
    let s: f64 = k.iter().sum();
    k.iter_mut().for_each(|v| *v /= s);
    let mut tmp = vec![0f32; w * h * 3];
    for y in 0..h {
        for x in 0..w {
            for c in 0..3 {
                let mut a = 0.;
                for (i, kv) in k.iter().enumerate() {
                    a += kv * img[(y * w + reflect101(x as isize + i as isize - 2, w)) * 3 + c] as f64;
                }
                tmp[(y * w + x) * 3 + c] = a as f32;
            }
        }
    }
    let mut out = vec![0u8; w * h * 3];
    for y in 0..h {
        for x in 0..w {
            for c in 0..3 {
                let mut a = 0.;
                for (i, kv) in k.iter().enumerate() {
                    a += kv * tmp[(reflect101(y as isize + i as isize - 2, h) * w + x) * 3 + c] as f64;
                }
                out[(y * w + x) * 3 + c] = a.round().clamp(0., 255.) as u8;
            }
        }
    }
    out
}

/// One crop. `img` is the half-resolution frame (packed RGB), (cx, cy, s) the full-resolution box.
pub fn crop(img: &[u8], w: usize, h: usize, cx: f64, cy: f64, s: f64, out: &mut [f32]) {
    let d = s * 0.5 / 256. / 2.;
    let blurred;
    let src: &[u8] = if d > 1.1 {
        blurred = blur5(img, w, h, (d - 1.) / 2.);
        &blurred
    } else {
        img
    };
    let (cx, cy, hs) = (cx * 0.5, cy * 0.5, s * 0.5 / 2.);
    let k = 255. / (2. * hs); // dst pixels per src pixel
    let (x0, y0) = (cx - hs, cy - hs);
    for oy in 0..H {
        let sy = oy as f64 / k + y0;
        for ox in 0..W {
            let sx = (ox + 32) as f64 / k + x0;
            // OpenCV INTER_LINEAR samples at 1/32-pixel resolution (INTER_BITS = 5).
            let fx = (sx * 32.).round() / 32.;
            let fy = (sy * 32.).round() / 32.;
            let (ix, iy) = (fx.floor(), fy.floor());
            let (ax, ay) = (fx - ix, fy - iy);
            let (ix, iy) = (ix as isize, iy as isize);
            let px = |x: isize, y: isize, c: usize| -> f64 {
                if x < 0 || y < 0 || x >= w as isize || y >= h as isize { 0. } else { src[(y as usize * w + x as usize) * 3 + c] as f64 }
            };
            for c in 0..3 {
                let v = (1. - ay) * ((1. - ax) * px(ix, iy, c) + ax * px(ix + 1, iy, c)) + ay * ((1. - ax) * px(ix, iy + 1, c) + ax * px(ix + 1, iy + 1, c));
                let v = v.round().clamp(0., 255.) as f32;
                out[c * H * W + oy * W + ox] = (v / 255. - MEAN[c]) / STD[c];
            }
        }
    }
}
