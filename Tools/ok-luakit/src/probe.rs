//! GamePoseProbe helpers (Tools/GamePoseProbe/*.lua): whole-image numeric
//! passes that mirror the original PIL/numpy code.
//!
//!   probe contact-sheet <spec.json|->   PIL-identical LANCZOS thumbnails + labels
//!   probe fg-bounds <png> <threshold>   numpy: where(max|rgb - rgb[0,0]| > t) bbox
//!   probe diff-count <a> <b> <threshold> numpy: count(max|a - b| > t) over RGB
//!   probe content-presence <png>        audit_capture_regression.image_content_presence fractions
//!   probe mean-abs-diff <a> <b>         float(abs(a.astype(float) - b).mean()) over RGB
//!   probe linalg <ops.json|->            numpy float64 inv / matmul with OpenBLAS rounding
//!   probe tex-sample <spec.json|->       numpy bilinear texture sampling at UVs (float64)
//!   probe tex-diff <spec.json|->         max |a - b| of two float64 images (same ops as tex-sample)
//!   probe alpha-flip <target> <source>   |alpha diff| stats direct and with source rows flipped
//!   probe rgba-max-diff <a> <b> [--mask m] max |a - b| over RGBA (only where m alpha == 255)
use crate::{arg, read_input, write_output, Res};
use ab_glyph::{Font, FontRef, PxScale, ScaleFont};
use serde_json::{json, Value};

pub fn run(args: &[String]) -> Res<Value> {
    let cmd = arg(args, 0, "probe command")?;
    let rest = &args[1..];
    match cmd {
        "contact-sheet" => contact_sheet(rest),
        "fg-bounds" => fg_bounds(rest),
        "diff-count" => diff_count(rest),
        "content-presence" => content_presence(rest),
        "mean-abs-diff" => mean_abs_diff(rest),
        "linalg" => linalg(rest),
        "tex-sample" => tex_sample(rest),
        "tex-diff" => tex_diff(rest),
        "alpha-flip" => alpha_flip(rest),
        "rgba-max-diff" => rgba_max_diff(rest),
        other => Err(format!("unknown probe command: {other}")),
    }
}

/// Decode to packed RGB8 (PIL convert("RGB") drops alpha).
pub fn load_rgb(path: &str) -> Res<(u32, u32, Vec<u8>)> {
    let img = image::ImageReader::open(path)
        .map_err(|e| format!("{path}: {e}"))?
        .with_guessed_format()
        .map_err(|e| format!("{path}: {e}"))?
        .decode()
        .map_err(|e| format!("{path}: {e}"))?;
    let rgb = img.to_rgb8();
    Ok((rgb.width(), rgb.height(), rgb.into_raw()))
}

// ---------------------------------------------------------------- PIL resample
// Port of Pillow's libImaging/Resample.c (8 bpc path, LANCZOS filter).
const PRECISION_BITS: u32 = 32 - 8 - 2;

fn sinc(x: f64) -> f64 {
    if x == 0.0 {
        return 1.0;
    }
    let x = x * std::f64::consts::PI;
    x.sin() / x
}

fn lanczos(x: f64) -> f64 {
    if (-3.0..3.0).contains(&x) {
        sinc(x) * sinc(x / 3.0)
    } else {
        0.0
    }
}

/// precompute_coeffs + normalize_coeffs_8bpc: (ksize, bounds[xmin,xmax], fixed-point kk).
fn coeffs(in_size: usize, in0: f32, in1: f32, out_size: usize) -> (usize, Vec<(usize, usize)>, Vec<i32>) {
    let support_base = 3.0f64;
    let scale = (in1 - in0) as f64 / out_size as f64;
    let filterscale = if scale < 1.0 { 1.0 } else { scale };
    let support = support_base * filterscale;
    let ksize = (support.ceil() as usize) * 2 + 1;
    let mut kk = vec![0f64; out_size * ksize];
    let mut bounds = Vec::with_capacity(out_size);
    for xx in 0..out_size {
        let center = in0 as f64 + (xx as f64 + 0.5) * scale;
        let mut ww = 0.0;
        let ss = 1.0 / filterscale;
        let mut xmin = (center - support + 0.5) as i64; // C cast truncates toward zero
        if xmin < 0 {
            xmin = 0;
        }
        let mut xmax = (center + support + 0.5) as i64;
        if xmax > in_size as i64 {
            xmax = in_size as i64;
        }
        xmax -= xmin;
        let k = &mut kk[xx * ksize..(xx + 1) * ksize];
        for x in 0..xmax as usize {
            let w = lanczos((x as f64 + xmin as f64 - center + 0.5) * ss);
            k[x] = w;
            ww += w;
        }
        for x in 0..xmax as usize {
            if ww != 0.0 {
                k[x] /= ww;
            }
        }
        bounds.push((xmin as usize, xmax as usize));
    }
    let fixed = kk
        .iter()
        .map(|&v| {
            if v < 0.0 {
                (-0.5 + v * (1u32 << PRECISION_BITS) as f64) as i32
            } else {
                (0.5 + v * (1u32 << PRECISION_BITS) as f64) as i32
            }
        })
        .collect();
    (ksize, bounds, fixed)
}

#[inline]
fn clip8(v: i32) -> u8 {
    let s = v >> PRECISION_BITS;
    s.clamp(0, 255) as u8
}

/// Image.resize((w, h), LANCZOS) on packed RGB8 (no box, no reducing_gap).
pub fn pil_resize_lanczos(sw: usize, sh: usize, src: &[u8], w: usize, h: usize) -> Vec<u8> {
    const C: usize = 3;
    let need_h = w != sw;
    let need_v = h != sh;
    let (kh, bh, kkh) = coeffs(sw, 0.0, sw as f32, w);
    let (kv, mut bv, kkv) = coeffs(sh, 0.0, sh as f32, h);
    let ybox_first = bv[0].0;
    let ybox_last = bv[h - 1].0 + bv[h - 1].1;
    let mut cur: Vec<u8>;
    let (mut cw, mut ch);
    let half = 1i32 << (PRECISION_BITS - 1);
    if need_h {
        for b in bv.iter_mut() {
            b.0 -= ybox_first;
        }
        let th = ybox_last - ybox_first;
        let mut tmp = vec![0u8; w * th * C];
        for yy in 0..th {
            let row = &src[(yy + ybox_first) * sw * C..(yy + ybox_first + 1) * sw * C];
            for xx in 0..w {
                let (xmin, xmax) = bh[xx];
                let k = &kkh[xx * kh..(xx + 1) * kh];
                let mut ss = [half; C];
                for x in 0..xmax {
                    let p = (x + xmin) * C;
                    for c in 0..C {
                        ss[c] = ss[c].wrapping_add((row[p + c] as i32).wrapping_mul(k[x]));
                    }
                }
                for c in 0..C {
                    tmp[(yy * w + xx) * C + c] = clip8(ss[c]);
                }
            }
        }
        cur = tmp;
        cw = w;
        ch = th;
    } else {
        cur = src.to_vec();
        cw = sw;
        ch = sh;
    }
    if need_v {
        let mut out = vec![0u8; w * h * C];
        for yy in 0..h {
            let (ymin, ymax) = bv[yy];
            let k = &kkv[yy * kv..(yy + 1) * kv];
            for xx in 0..cw {
                let mut ss = [half; C];
                for y in 0..ymax {
                    let p = ((y + ymin) * cw + xx) * C;
                    for c in 0..C {
                        ss[c] = ss[c].wrapping_add((cur[p + c] as i32).wrapping_mul(k[y]));
                    }
                }
                for c in 0..C {
                    out[(yy * cw + xx) * C + c] = clip8(ss[c]);
                }
            }
        }
        cur = out;
        ch = h;
    }
    let _ = ch;
    cur
}

// ---------------------------------------------------------------- text
// ImageDraw.text(xy, s, fill) without a font uses Pillow's default font
// (Aileron Regular, 10 px, FreeType hinted, basic layout, no kerning).
// assets/pil-default-font-10.json holds those glyph masks for ASCII 32..126 as
// drawn by Pillow 12 (draw.text on black, patch offset from the origin and the
// integer advance), so ASCII labels reproduce PIL pixels exactly. Glyphs that
// overlap combine like Pillow's font_render (src + dst*(255-src)/255); the mask
// is blended onto the image like fill_mask_L. Other characters fall back to
// ab_glyph outlines of the same font (approximate).
static AILERON: &[u8] = include_bytes!("../assets/aileron-regular-pil.ttf");
static ATLAS: &str = include_str!("../assets/pil-default-font-10.json");

#[inline]
fn div255(v: u32) -> u32 {
    let t = v + 128;
    ((t >> 8) + t) >> 8
}

struct Glyph {
    adv: i32,
    x: i32,
    y: i32,
    w: usize,
    h: usize,
    px: Vec<u8>,
}

fn atlas() -> Res<std::collections::HashMap<u32, Glyph>> {
    let v: Value = serde_json::from_str(ATLAS).map_err(|e| e.to_string())?;
    let mut out = std::collections::HashMap::new();
    for (k, g) in v.as_object().ok_or("atlas")? {
        let hex = g["px"].as_str().unwrap_or("");
        let px = (0..hex.len() / 2).map(|i| u8::from_str_radix(&hex[2 * i..2 * i + 2], 16).unwrap_or(0)).collect();
        out.insert(k.parse::<u32>().map_err(|e| e.to_string())?, Glyph {
            adv: g["adv"].as_i64().unwrap_or(0) as i32,
            x: g["x"].as_i64().unwrap_or(0) as i32,
            y: g["y"].as_i64().unwrap_or(0) as i32,
            w: g["w"].as_u64().unwrap_or(0) as usize,
            h: g["h"].as_u64().unwrap_or(0) as usize,
            px,
        });
    }
    Ok(out)
}

fn draw_text(canvas: &mut [u8], cw: usize, chh: usize, x: i32, y: i32, text: &str, color: [u8; 3]) -> Res<()> {
    let glyphs = atlas()?;
    let font = FontRef::try_from_slice(AILERON).map_err(|e| e.to_string())?;
    let upem = font.units_per_em().unwrap_or(1000.0);
    let scale = PxScale::from(10.0 * font.height_unscaled() / upem);
    let scaled = font.as_scaled(scale);
    let mut mask = vec![0u8; cw * chh];
    let mut put = |mx: i32, my: i32, v: u8| {
        if mx < 0 || my < 0 || mx as usize >= cw || my as usize >= chh || v == 0 {
            return;
        }
        let m = &mut mask[my as usize * cw + mx as usize];
        let (s, d) = (v as u32, *m as u32);
        *m = if d == 0 { v } else { div255(s * 255 + d * (255 - s)) as u8 };
    };
    let mut pen = x;
    for chr in text.chars() {
        if let Some(g) = glyphs.get(&(chr as u32)) {
            for gy in 0..g.h {
                for gx in 0..g.w {
                    put(pen + g.x + gx as i32, y + g.y + gy as i32, g.px[gy * g.w + gx]);
                }
            }
            pen += g.adv;
        } else {
            let id = scaled.glyph_id(chr);
            let glyph = id.with_scale_and_position(scale, ab_glyph::point(pen as f32, y as f32 + scaled.ascent().ceil() - 1.0));
            if let Some(outline) = font.outline_glyph(glyph) {
                let bb = outline.px_bounds();
                outline.draw(|gx, gy, cov| put(bb.min.x as i32 + gx as i32, bb.min.y as i32 + gy as i32, (cov.clamp(0.0, 1.0) * 255.0 + 0.5) as u8));
            }
            pen += scaled.h_advance(id).round() as i32;
        }
    }
    for (i, &m) in mask.iter().enumerate() {
        if m == 0 {
            continue;
        }
        let a = m as u32;
        for c in 0..3 {
            let d = canvas[i * 3 + c] as u32;
            canvas[i * 3 + c] = div255(d * (255 - a) + color[c] as u32 * a) as u8;
        }
    }
    Ok(())
}

fn rgb3(v: &Value, d: [u8; 3]) -> [u8; 3] {
    v.as_array()
        .map(|a| {
            let mut c = d;
            for (i, x) in a.iter().take(3).enumerate() {
                c[i] = x.as_u64().unwrap_or(0) as u8;
            }
            c
        })
        .unwrap_or(d)
}

/// contact-sheet spec:
/// {width,height,background:[r,g,b],output,
///  items:[{file,x,y,w,h}]   (PIL im.convert("RGB").resize((w,h), LANCZOS) pasted at x,y),
///  texts:[{x,y,text,fill:[r,g,b]}]}  (ImageDraw.text with the default font)  drawn in order given by "order": "texts-first" (default) | "items-first"
pub fn contact_sheet(args: &[String]) -> Res<Value> {
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| e.to_string())?;
    let w = spec["width"].as_u64().ok_or("width")? as usize;
    let h = spec["height"].as_u64().ok_or("height")? as usize;
    let bg = rgb3(&spec["background"], [0, 0, 0]);
    let mut canvas = vec![0u8; w * h * 3];
    for p in canvas.chunks_exact_mut(3) {
        p.copy_from_slice(&bg);
    }
    let paste = |canvas: &mut Vec<u8>, item: &Value| -> Res<()> {
        let file = item["file"].as_str().ok_or("item.file")?;
        let (sw, sh, src) = load_rgb(file)?;
        let tw = item["w"].as_u64().unwrap_or(sw as u64) as usize;
        let th = item["h"].as_u64().unwrap_or(sh as u64) as usize;
        let thumb = if tw == sw as usize && th == sh as usize { src } else { pil_resize_lanczos(sw as usize, sh as usize, &src, tw, th) };
        let x0 = item["x"].as_i64().unwrap_or(0);
        let y0 = item["y"].as_i64().unwrap_or(0);
        for yy in 0..th {
            let ty = y0 + yy as i64;
            if ty < 0 || ty as usize >= h {
                continue;
            }
            for xx in 0..tw {
                let tx = x0 + xx as i64;
                if tx < 0 || tx as usize >= w {
                    continue;
                }
                let s = (yy * tw + xx) * 3;
                let d = (ty as usize * w + tx as usize) * 3;
                canvas[d..d + 3].copy_from_slice(&thumb[s..s + 3]);
            }
        }
        Ok(())
    };
    let texts = spec["texts"].as_array().cloned().unwrap_or_default();
    let items = spec["items"].as_array().cloned().unwrap_or_default();
    let draw_all_text = |canvas: &mut Vec<u8>| -> Res<()> {
        for t in &texts {
            draw_text(canvas, w, h, t["x"].as_i64().unwrap_or(0) as i32, t["y"].as_i64().unwrap_or(0) as i32,
                t["text"].as_str().unwrap_or(""), rgb3(&t["fill"], [255, 255, 255]))?;
        }
        Ok(())
    };
    let items_first = spec["order"].as_str() == Some("items-first");
    if !items_first {
        draw_all_text(&mut canvas)?;
    }
    for item in &items {
        paste(&mut canvas, item)?;
    }
    if items_first {
        draw_all_text(&mut canvas)?;
    }
    let out = spec["output"].as_str().ok_or("output")?;
    let img = image::RgbImage::from_raw(w as u32, h as u32, canvas).ok_or("canvas size")?;
    let mut bytes = Vec::new();
    img.write_to(&mut std::io::Cursor::new(&mut bytes), image::ImageFormat::Png).map_err(|e| e.to_string())?;
    write_output(out, &bytes)?;
    Ok(json!({"path": out, "width": w, "height": h}))
}

pub fn fg_bounds(args: &[String]) -> Res<Value> {
    let file = arg(args, 0, "png")?;
    let t: i32 = arg(args, 1, "threshold")?.parse().map_err(|_| "threshold")?;
    let (w, h, px) = load_rgb(file)?;
    let r0 = [px[0] as i32, px[1] as i32, px[2] as i32];
    let (mut x0, mut y0, mut x1, mut y1, mut n) = (i64::MAX, i64::MAX, -1i64, -1i64, 0u64);
    for y in 0..h as usize {
        for x in 0..w as usize {
            let i = (y * w as usize + x) * 3;
            let d = (0..3).map(|c| (px[i + c] as i32 - r0[c]).abs()).max().unwrap();
            if d > t {
                n += 1;
                x0 = x0.min(x as i64);
                y0 = y0.min(y as i64);
                x1 = x1.max(x as i64);
                y1 = y1.max(y as i64);
            }
        }
    }
    if n == 0 {
        return Ok(json!({"count": 0, "width": w, "height": h, "bounds": null}));
    }
    Ok(json!({"count": n, "width": w, "height": h, "bounds": [x0, y0, x1, y1]}))
}

pub fn diff_count(args: &[String]) -> Res<Value> {
    let a = arg(args, 0, "a")?;
    let b = arg(args, 1, "b")?;
    let t: i32 = arg(args, 2, "threshold")?.parse().map_err(|_| "threshold")?;
    let (wa, ha, pa) = load_rgb(a)?;
    let (wb, hb, pb) = load_rgb(b)?;
    if (wa, ha) != (wb, hb) {
        return Err(format!("size mismatch {wa}x{ha} vs {wb}x{hb}"));
    }
    let mut n = 0u64;
    let mut max = 0i32;
    for (p, q) in pa.chunks_exact(3).zip(pb.chunks_exact(3)) {
        let d = (0..3).map(|c| (p[c] as i32 - q[c] as i32).abs()).max().unwrap();
        max = max.max(d);
        if d > t {
            n += 1;
        }
    }
    Ok(json!({"count": n, "max": max, "width": wa, "height": ha}))
}

/// image_content_presence: background = per-channel median of the four corner
/// pixels; foreground = max|rgb - background| > 8; fractions over the image and
/// its centre half. Sums are integers, so the float results equal numpy's.
pub fn content_presence(args: &[String]) -> Res<Value> {
    let (w, h, px) = load_rgb(arg(args, 0, "png")?)?;
    let (w, h) = (w as usize, h as usize);
    let corner = |x: usize, y: usize| -> [f64; 3] {
        let i = (y * w + x) * 3;
        [px[i] as f64, px[i + 1] as f64, px[i + 2] as f64]
    };
    let cs = [corner(0, 0), corner(w - 1, 0), corner(0, h - 1), corner(w - 1, h - 1)];
    let mut bg = [0f64; 3];
    for c in 0..3 {
        let mut v: Vec<f64> = cs.iter().map(|p| p[c]).collect();
        v.sort_by(|a, b| a.partial_cmp(b).unwrap());
        bg[c] = (v[1] + v[2]) / 2.0;
    }
    let (mut all, mut centre) = (0u64, 0u64);
    let (y0, y1, x0, x1) = (h / 4, 3 * h / 4, w / 4, 3 * w / 4);
    for y in 0..h {
        for x in 0..w {
            let i = (y * w + x) * 3;
            let d = (0..3).map(|c| (px[i + c] as f64 - bg[c]).abs()).fold(0f64, f64::max);
            if d > 8.0 {
                all += 1;
                if y >= y0 && y < y1 && x >= x0 && x < x1 {
                    centre += 1;
                }
            }
        }
    }
    let cn = ((y1 - y0) * (x1 - x0)) as f64;
    Ok(json!({"foreground_fraction": all as f64 / (w * h) as f64,
              "center_foreground_fraction": if cn > 0.0 { centre as f64 / cn } else { f64::NAN },
              "width": w, "height": h}))
}

pub fn mean_abs_diff(args: &[String]) -> Res<Value> {
    let (wa, ha, pa) = load_rgb(arg(args, 0, "a")?)?;
    let (wb, hb, pb) = load_rgb(arg(args, 1, "b")?)?;
    if (wa, ha) != (wb, hb) {
        return Err(format!("size mismatch {wa}x{ha} vs {wb}x{hb}"));
    }
    let sum: u64 = pa.iter().zip(pb.iter()).map(|(&x, &y)| (x as i32 - y as i32).unsigned_abs() as u64).sum();
    Ok(json!({"mean": sum as f64 / pa.len() as f64, "width": wa, "height": ha}))
}

// ---------------------------------------------------------------- linalg
// numpy.linalg.inv and `@` on float64 go through OpenBLAS (DYNAMIC_ARCH). These
// reproduce its operation order and FMA use for small matrices, verified
// bit-for-bit against numpy 2.5 / OpenBLAS 0.3.33 (SkylakeX core): dgemm
// accumulates fma(a_ik, b_kj, acc) from 0 over ascending k; inv = dgesv with
// identity = getf2 (left-looking: x - sum, pivot by max |.|, scale by 1/pivot)
// then forward/back substitution with inverted diagonal.

fn mat(v: &Value) -> Res<Vec<Vec<f64>>> {
    v.as_array().ok_or("matrix must be an array of rows")?.iter()
        .map(|r| r.as_array().ok_or_else(|| "row".to_string()).map(|r| r.iter().map(|x| x.as_f64().unwrap_or(f64::NAN)).collect()))
        .collect()
}

fn chain(pairs: impl Iterator<Item = (f64, f64)>) -> f64 {
    let mut acc = 0.0f64;
    for (a, b) in pairs {
        acc = a.mul_add(b, acc);
    }
    acc
}

pub fn np_matmul(a: &[Vec<f64>], b: &[Vec<f64>]) -> Res<Vec<Vec<f64>>> {
    let k = b.len();
    if a.iter().any(|r| r.len() != k) {
        return Err("matmul: shape mismatch".into());
    }
    let m = b.first().map(|r| r.len()).unwrap_or(0);
    Ok(a.iter().map(|row| (0..m).map(|j| chain((0..k).map(|t| (row[t], b[t][j])))).collect()).collect())
}

pub fn np_inv(m: &[Vec<f64>]) -> Res<Vec<Vec<f64>>> {
    let n = m.len();
    if m.iter().any(|r| r.len() != n) {
        return Err("inv: matrix must be square".into());
    }
    let mut a: Vec<Vec<f64>> = m.to_vec();
    let mut ipiv = vec![0usize; n];
    for j in 0..n {
        for i in 0..j {
            let ip = ipiv[i];
            if ip != i {
                let t = a[i][j];
                a[i][j] = a[ip][j];
                a[ip][j] = t;
            }
        }
        for i in 1..j {
            let d = chain((0..i).map(|t| (a[i][t], a[t][j])));
            a[i][j] -= d;
        }
        for i in j..n {
            if j > 0 {
                let d = chain((0..j).map(|t| (a[i][t], a[t][j])));
                a[i][j] -= d;
            }
        }
        let mut jp = j;
        for i in j + 1..n {
            if a[i][j].abs() > a[jp][j].abs() {
                jp = i;
            }
        }
        ipiv[j] = jp;
        if jp != j {
            for t in 0..=j {
                let v = a[j][t];
                a[j][t] = a[jp][t];
                a[jp][t] = v;
            }
        }
        if a[j][j] == 0.0 {
            return Err("Singular matrix".into());
        }
        let r = 1.0 / a[j][j];
        for i in j + 1..n {
            a[i][j] *= r;
        }
    }
    // dgetrs: B = P * I, forward with unit L, backward with U (inverted diagonal).
    let mut b: Vec<Vec<f64>> = (0..n).map(|i| (0..n).map(|j| if i == j { 1.0 } else { 0.0 }).collect()).collect();
    for i in 0..n {
        let ip = ipiv[i];
        if ip != i {
            b.swap(i, ip);
        }
    }
    for j in 0..n {
        for i in 0..n {
            let bb = b[i][j];
            for k in i + 1..n {
                b[k][j] = (-bb).mul_add(a[k][i], b[k][j]);
            }
        }
        for i in (0..n).rev() {
            let bb = b[i][j] * (1.0 / a[i][i]);
            b[i][j] = bb;
            for k in 0..i {
                b[k][j] = (-bb).mul_add(a[k][i], b[k][j]);
            }
        }
    }
    Ok(b)
}

/// linalg ops: {"ops":[{"op":"inv","a":M} | {"op":"matmul","a":A,"b":B} | {"op":"pow","a":[x..],"b":e}, ...]} -> [result, ...]
pub fn linalg(args: &[String]) -> Res<Value> {
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "ops")?)?).map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for op in spec["ops"].as_array().ok_or("ops")? {
        let r = match op["op"].as_str().unwrap_or("") {
            "inv" => np_inv(&mat(&op["a"])?)?,
            "matmul" => np_matmul(&mat(&op["a"])?, &mat(&op["b"])?)?,
            "pow" => {
                // Python float ** float: the C runtime pow() (UCRT), not x*x.
                let e = std::hint::black_box(op["b"].as_f64().unwrap_or(2.0));
                let xs: Vec<f64> = op["a"].as_array().ok_or("pow.a")?.iter().map(|x| x.as_f64().unwrap_or(f64::NAN)).collect();
                out.push(json!(xs.iter().map(|&x| std::hint::black_box(x).powf(e)).collect::<Vec<f64>>()));
                continue;
            }
            other => return Err(format!("unknown linalg op {other}")),
        };
        out.push(json!(r));
    }
    Ok(Value::Array(out))
}

// ---------------------------------------------------------------- tex-sample
// Bilinear sampling of a PNG at UV coordinates, written like the numpy code in
// verify_brow_uv_coverage / verify_item_uv_samples / verify_native_hair_inputs:
// pixels = asarray(convert("RGBA"), float64) / 255, optional per-channel ops
// (add then clip to [0,1], sRGB decode with pow 2.4), then one of the kernels:
//   "accumulate": xy = uv*[w,h] - .5, out += a[y,x] * (wx*wy) over (dy,dx) in
//                 (0,0),(0,1),(1,0),(1,1); address "clip" or "repeat"
//   "lerp":       ((p00*(1-fx) + p01*fx)*(1-fy) + (p10*(1-fx) + p11*fx)*fy) with
//                 wrap_u / wrap_v 0 repeat, 1 clamp, 2 mirror
// spec: {png, ops:[{add:d, channels:"rgba"|"rgb"|"a"} | {srgb:true}], flip,
//        kernel, address, wrap_u, wrap_v, shift:[du,dv],
//        uv:[[u,v],...] | uv_tsv:path (np.loadtxt(skiprows=1)[:,1:3])}
// -> {width, height, uv:[[u,v]...] (only for uv_tsv), samples:[[r,g,b,a]...]}

fn srgb_decode(v: f64) -> f64 {
    if v <= 0.04045 {
        v / 12.92
    } else {
        ((v + 0.055) / 1.055).powf(std::hint::black_box(2.4))
    }
}

fn load_rgba_f64(path: &str) -> Res<(usize, usize, Vec<[f64; 4]>)> {
    let img = image::ImageReader::open(path)
        .map_err(|e| format!("{path}: {e}"))?
        .with_guessed_format()
        .map_err(|e| format!("{path}: {e}"))?
        .decode()
        .map_err(|e| format!("{path}: {e}"))?;
    let rgba = img.to_rgba8();
    let (w, h) = (rgba.width() as usize, rgba.height() as usize);
    let px = rgba
        .pixels()
        .map(|p| [p[0] as f64 / 255.0, p[1] as f64 / 255.0, p[2] as f64 / 255.0, p[3] as f64 / 255.0])
        .collect();
    Ok((w, h, px))
}

pub fn loadtxt_uv(path: &str) -> Res<Vec<[f64; 2]>> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
    let mut out = Vec::new();
    for line in text.lines().skip(1) {
        let cols: Vec<&str> = line.split_whitespace().collect();
        if cols.is_empty() {
            continue;
        }
        if cols.len() < 3 {
            return Err(format!("{path}: row with fewer than 3 columns"));
        }
        let u: f64 = cols[1].parse().map_err(|_| format!("{path}: bad float {}", cols[1]))?;
        let v: f64 = cols[2].parse().map_err(|_| format!("{path}: bad float {}", cols[2]))?;
        out.push([u, v]);
    }
    Ok(out)
}

fn wrap_index(i: i64, size: i64, wrap: i64) -> Res<i64> {
    Ok(match wrap {
        0 => i.rem_euclid(size),
        1 => i.clamp(0, size - 1),
        2 => {
            let r = i.rem_euclid(size * 2);
            if r < size {
                r
            } else {
                size * 2 - 1 - r
            }
        }
        _ => return Err(format!("Unsupported source wrap: {wrap}")),
    })
}

pub fn tex_sample(args: &[String]) -> Res<Value> {
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| e.to_string())?;
    if spec["png"].is_null() {
        // uv only: np.loadtxt(uv_tsv, skiprows=1)[:, 1:3]
        let uv = loadtxt_uv(spec["uv_tsv"].as_str().ok_or("png or uv_tsv")?)?;
        return Ok(json!({"uv": uv.iter().map(|q| q.to_vec()).collect::<Vec<_>>()}));
    }
    let (w, h, px) = load_with_ops(&spec)?;
    let (uv, from_tsv) = if let Some(p) = spec["uv_tsv"].as_str() {
        (loadtxt_uv(p)?, true)
    } else {
        let list = spec["uv"].as_array().ok_or("uv")?;
        (list.iter().map(|r| [r[0].as_f64().unwrap_or(f64::NAN), r[1].as_f64().unwrap_or(f64::NAN)]).collect(), false)
    };
    let shift = [spec["shift"][0].as_f64(), spec["shift"][1].as_f64()];
    let flip = spec["flip"].as_bool().unwrap_or(false);
    let lerp = spec["kernel"].as_str() == Some("lerp");
    let repeat = spec["address"].as_str() == Some("repeat");
    let (wu, wv) = (spec["wrap_u"].as_i64().unwrap_or(0), spec["wrap_v"].as_i64().unwrap_or(0));
    let (wi, hi) = (w as i64, h as i64);
    let get = |x: i64, y: i64| px[(y as usize) * w + x as usize];
    let mut samples = Vec::with_capacity(uv.len());
    for q in &uv {
        let mut u = q[0];
        let mut v = q[1];
        if let Some(s) = shift[0] {
            u += s;
        }
        if let Some(s) = shift[1] {
            v += s;
        }
        let vy = if flip { 1.0 - v } else { v };
        let x = u * w as f64 - 0.5;
        let y = vy * h as f64 - 0.5;
        let (bx, by) = (x.floor(), y.floor());
        let (tx, ty) = (x - bx, y - by);
        let (bx, by) = (bx as i64, by as i64);
        let mut o = [0f64; 4];
        if lerp {
            let (x0, x1) = (wrap_index(bx, wi, wu)?, wrap_index(bx + 1, wi, wu)?);
            let (y0, y1) = (wrap_index(by, hi, wv)?, wrap_index(by + 1, hi, wv)?);
            let (p00, p01, p10, p11) = (get(x0, y0), get(x1, y0), get(x0, y1), get(x1, y1));
            for c in 0..4 {
                o[c] = (p00[c] * (1.0 - tx) + p01[c] * tx) * (1.0 - ty) + (p10[c] * (1.0 - tx) + p11[c] * tx) * ty;
            }
        } else {
            for dy in 0..2i64 {
                for dx in 0..2i64 {
                    let weight = (if dx == 1 { tx } else { 1.0 - tx }) * (if dy == 1 { ty } else { 1.0 - ty });
                    let (xx, yy) = if repeat {
                        ((bx + dx).rem_euclid(wi), (by + dy).rem_euclid(hi))
                    } else {
                        ((bx + dx).clamp(0, wi - 1), (by + dy).clamp(0, hi - 1))
                    };
                    let p = get(xx, yy);
                    for c in 0..4 {
                        o[c] += p[c] * weight;
                    }
                }
            }
        }
        samples.push(o.to_vec());
    }
    let mut res = json!({"width": w, "height": h, "samples": samples});
    if from_tsv {
        res["uv"] = json!(uv.iter().map(|q| q.to_vec()).collect::<Vec<_>>());
    }
    Ok(res)
}

/// Load spec.png as float64 RGBA / 255 and apply spec.ops.
fn load_with_ops(spec: &Value) -> Res<(usize, usize, Vec<[f64; 4]>)> {
    let (w, h, mut px) = load_rgba_f64(spec["png"].as_str().ok_or("png")?)?;
    for op in spec["ops"].as_array().cloned().unwrap_or_default() {
        if op["srgb"].as_bool() == Some(true) {
            for p in px.iter_mut() {
                for c in 0..3 {
                    p[c] = srgb_decode(p[c]);
                }
            }
        } else if let Some(d) = op["add"].as_f64() {
            let chans: Vec<usize> = match op["channels"].as_str().unwrap_or("rgba") {
                "a" => vec![3],
                "rgb" => vec![0, 1, 2],
                _ => vec![0, 1, 2, 3],
            };
            for p in px.iter_mut() {
                for &c in &chans {
                    let v = p[c] + d;
                    // np.maximum(0, x - d) or np.minimum(1, x + d)
                    p[c] = if d < 0.0 {
                        if v < 0.0 { 0.0 } else { v }
                    } else if v > 1.0 {
                        1.0
                    } else {
                        v
                    };
                }
            }
        }
    }
    Ok((w, h, px))
}

/// tex-diff spec: {a:{png,ops}, b:{png,ops}, channels:"rgb"|"rgba"} ->
/// {max: float(abs(a[:,:,:k] - b[:,:,:k]).max()), width, height}
pub fn tex_diff(args: &[String]) -> Res<Value> {
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| e.to_string())?;
    let (wa, ha, a) = load_with_ops(&spec["a"])?;
    let (wb, hb, b) = load_with_ops(&spec["b"])?;
    if (wa, ha) != (wb, hb) {
        return Err(format!("shape mismatch {wa}x{ha} vs {wb}x{hb}"));
    }
    let k = if spec["channels"].as_str() == Some("rgba") { 4 } else { 3 };
    let mut m = f64::NEG_INFINITY;
    for (p, q) in a.iter().zip(b.iter()) {
        for c in 0..k {
            let d = (p[c] - q[c]).abs();
            if d.is_nan() || d > m {
                m = d;
            }
        }
    }
    Ok(json!({"max": m, "width": wa, "height": ha}))
}

fn load_rgba8(path: &str) -> Res<(usize, usize, Vec<u8>)> {
    let img = image::ImageReader::open(path)
        .map_err(|e| format!("{path}: {e}"))?
        .with_guessed_format()
        .map_err(|e| format!("{path}: {e}"))?
        .decode()
        .map_err(|e| format!("{path}: {e}"))?;
    let rgba = img.to_rgba8();
    Ok((rgba.width() as usize, rgba.height() as usize, rgba.into_raw()))
}

/// verify_brow_orientation: alpha arrays (int16), error = |target - source| and
/// |target - source[::-1]|; mean (integer sum / n), max, count > 16.
pub fn alpha_flip(args: &[String]) -> Res<Value> {
    let (tw, th, t) = load_rgba8(arg(args, 0, "target")?)?;
    let (sw, sh, s) = load_rgba8(arg(args, 1, "source")?)?;
    if (tw, th) != (sw, sh) {
        return Ok(json!({"shape_mismatch": true, "target": [th, tw], "source": [sh, sw]}));
    }
    let mut out = Vec::new();
    for flip in [false, true] {
        let (mut sum, mut max, mut over) = (0u64, 0i32, 0u64);
        for y in 0..th {
            let sy = if flip { th - 1 - y } else { y };
            for x in 0..tw {
                let d = (t[(y * tw + x) * 4 + 3] as i32 - s[(sy * tw + x) * 4 + 3] as i32).abs();
                sum += d as u64;
                max = max.max(d);
                if d > 16 {
                    over += 1;
                }
            }
        }
        out.push(json!({"flip_y": flip, "mean_abs_alpha": sum as f64 / (tw * th) as f64, "max_abs_alpha": max, "pixels_over_16": over}));
    }
    Ok(json!({"shape": [th, tw], "comparisons": out}))
}

/// verify_legacy_face_overlay: int16 |a - b| max over RGBA, optionally only at
/// pixels where mask alpha == 255 (the opaque count is returned).
pub fn rgba_max_diff(args: &[String]) -> Res<Value> {
    let (aw, ah, a) = load_rgba8(arg(args, 0, "a")?)?;
    let (bw, bh, b) = load_rgba8(arg(args, 1, "b")?)?;
    let mask = crate::opt(args, "--mask").map(load_rgba8).transpose()?;
    if (aw, ah) != (bw, bh) {
        return Ok(json!({"shape_mismatch": true, "a": [ah, aw, 4], "b": [bh, bw, 4]}));
    }
    if let Some((mw, mh, _)) = &mask {
        if (*mw, *mh) != (aw, ah) {
            return Err("mask shape mismatch".into());
        }
    }
    let (mut max, mut count) = (0i32, 0u64);
    for i in 0..aw * ah {
        if let Some((_, _, m)) = &mask {
            if m[i * 4 + 3] != 255 {
                continue;
            }
        }
        count += 1;
        for c in 0..4 {
            max = max.max((a[i * 4 + c] as i32 - b[i * 4 + c] as i32).abs());
        }
    }
    Ok(json!({"max": max, "count": count, "shape": [ah, aw, 4]}))
}
