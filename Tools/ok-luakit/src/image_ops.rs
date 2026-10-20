//! Image codecs and pixel analysis. Raw buffers are tightly packed 8-bit
//! rows in one of the modes RGBA, RGB, LA, L (PIL semantics for conversion).
use crate::{arg, opt, read_input, write_output, Res};
use image::{DynamicImage, ImageReader};
use serde_json::{json, Value};

pub fn channels(mode: &str) -> Res<usize> {
    Ok(match mode {
        "RGBA" => 4,
        "RGB" => 3,
        "LA" => 2,
        "L" => 1,
        other => return Err(format!("unsupported mode {other}")),
    })
}

fn load(path: &str) -> Res<DynamicImage> {
    ImageReader::open(path)
        .map_err(|e| format!("{path}: {e}"))?
        .with_guessed_format()
        .map_err(|e| format!("{path}: {e}"))?
        .decode()
        .map_err(|e| format!("{path}: {e}"))
}

fn color_name(c: image::ColorType) -> &'static str {
    use image::ColorType::*;
    match c {
        L8 => "L",
        La8 => "LA",
        Rgb8 => "RGB",
        Rgba8 => "RGBA",
        L16 => "I;16",
        La16 => "LA16",
        Rgb16 => "RGB16",
        Rgba16 => "RGBA16",
        Rgb32F => "RGB32F",
        Rgba32F => "RGBA32F",
        _ => "other",
    }
}

#[inline]
fn luma(r: u8, g: u8, b: u8) -> u8 {
    // PIL ImagingConvert L: ITU-R 601-2 with 16-bit fixed point rounding.
    ((r as u32 * 19595 + g as u32 * 38470 + b as u32 * 7471 + 0x8000) >> 16) as u8
}

/// Convert a raw buffer between modes like PIL Image.convert (alpha dropped, not composited).
pub fn convert_raw(src: &[u8], from: &str, to: &str) -> Res<Vec<u8>> {
    let (fc, tc) = (channels(from)?, channels(to)?);
    let n = src.len() / fc;
    let mut out = Vec::with_capacity(n * tc);
    for p in src.chunks_exact(fc) {
        let (r, g, b, a) = match fc {
            4 => (p[0], p[1], p[2], p[3]),
            3 => (p[0], p[1], p[2], 255),
            2 => (p[0], p[0], p[0], p[1]),
            _ => (p[0], p[0], p[0], 255),
        };
        match tc {
            4 => out.extend_from_slice(&[r, g, b, a]),
            3 => out.extend_from_slice(&[r, g, b]),
            2 => out.extend_from_slice(&[if fc <= 2 { r } else { luma(r, g, b) }, a]),
            _ => out.push(if fc <= 2 { r } else { luma(r, g, b) }),
        }
    }
    Ok(out)
}

fn to_mode(img: &DynamicImage, mode: &str) -> Res<Vec<u8>> {
    let rgba = img.to_rgba8();
    let src_mode = match img.color() {
        image::ColorType::L8 | image::ColorType::L16 => "L",
        image::ColorType::La8 | image::ColorType::La16 => "LA",
        _ => "RGBA",
    };
    // Grey sources keep their exact value for L/LA instead of re-deriving luma.
    if src_mode != "RGBA" {
        let la = img.to_luma_alpha8();
        return convert_raw(la.as_raw(), "LA", mode);
    }
    convert_raw(rgba.as_raw(), "RGBA", mode)
}

/// image-info <file>
pub fn info(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "file")?;
    let reader = ImageReader::open(path).map_err(|e| format!("{path}: {e}"))?.with_guessed_format().map_err(|e| e.to_string())?;
    let format = reader.format().map(|f| format!("{f:?}").to_uppercase());
    let img = reader.decode().map_err(|e| format!("{path}: {e}"))?;
    Ok(json!({ "width": img.width(), "height": img.height(), "color": color_name(img.color()), "format": format }))
}

/// image-decode <file> <raw_out> [mode=RGBA]
pub fn decode(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "file")?;
    let out = arg(args, 1, "raw_out")?;
    let mode = args.get(2).map(|s| s.as_str()).unwrap_or("RGBA");
    let img = load(path)?;
    let raw = to_mode(&img, mode)?;
    write_output(out, &raw)?;
    Ok(json!({ "width": img.width(), "height": img.height(), "mode": mode, "source_mode": color_name(img.color()) }))
}

fn build(raw: Vec<u8>, w: u32, h: u32, mode: &str) -> Res<DynamicImage> {
    let expect = w as usize * h as usize * channels(mode)?;
    if raw.len() != expect {
        return Err(format!("raw size {} != {}x{}x{}", raw.len(), w, h, channels(mode)?));
    }
    Ok(match mode {
        "RGBA" => DynamicImage::ImageRgba8(image::RgbaImage::from_raw(w, h, raw).unwrap()),
        "RGB" => DynamicImage::ImageRgb8(image::RgbImage::from_raw(w, h, raw).unwrap()),
        "LA" => DynamicImage::ImageLumaA8(image::GrayAlphaImage::from_raw(w, h, raw).unwrap()),
        _ => DynamicImage::ImageLuma8(image::GrayImage::from_raw(w, h, raw).unwrap()),
    })
}

fn save(img: &DynamicImage, out: &str) -> Res<()> {
    if let Some(parent) = std::path::Path::new(out).parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
    }
    let lower = out.to_ascii_lowercase();
    if lower.ends_with(".jpg") || lower.ends_with(".jpeg") {
        // JPEG has no alpha; PIL would refuse RGBA, we drop it.
        let rgb = DynamicImage::ImageRgb8(img.to_rgb8());
        let file = std::fs::File::create(out).map_err(|e| format!("{out}: {e}"))?;
        let mut enc = image::codecs::jpeg::JpegEncoder::new_with_quality(std::io::BufWriter::new(file), 95);
        enc.encode_image(&rgb).map_err(|e| format!("{out}: {e}"))
    } else {
        img.save(out).map_err(|e| format!("{out}: {e}"))
    }
}

/// image-encode <raw> <w> <h> <mode> <out>
pub fn encode(args: &[String]) -> Res<Value> {
    let raw = read_input(arg(args, 0, "raw")?)?;
    let w: u32 = arg(args, 1, "w")?.parse().map_err(|_| "bad width")?;
    let h: u32 = arg(args, 2, "h")?.parse().map_err(|_| "bad height")?;
    let mode = arg(args, 3, "mode")?;
    let out = arg(args, 4, "out")?;
    save(&build(raw, w, h, mode)?, out)?;
    Ok(json!({ "path": out }))
}

/// image-convert <raw> <w> <h> <from> <to> <raw_out>
pub fn convert(args: &[String]) -> Res<Value> {
    let raw = read_input(arg(args, 0, "raw")?)?;
    let from = arg(args, 3, "from")?;
    let to = arg(args, 4, "to")?;
    let out = convert_raw(&raw, from, to)?;
    write_output(arg(args, 5, "raw_out")?, &out)?;
    Ok(json!({ "mode": to }))
}

fn parse_box(s: Option<&str>, w: u32, h: u32) -> Res<(u32, u32, u32, u32)> {
    match s {
        None => Ok((0, 0, w, h)),
        Some(t) => {
            let v: Vec<u32> = t.split(',').map(|x| x.trim().parse::<u32>().map_err(|_| format!("bad box {t}"))).collect::<Res<_>>()?;
            if v.len() != 4 || v[2] > w || v[3] > h || v[0] >= v[2] || v[1] >= v[3] {
                return Err(format!("box {t} outside {w}x{h}"));
            }
            Ok((v[0], v[1], v[2], v[3]))
        }
    }
}

/// image-stats <file> [--box l,t,r,b] [--alpha-min N]
/// -> {count, mean[4], min[4], max[4], std[4], opaque, transparent} over RGBA
pub fn stats(args: &[String]) -> Res<Value> {
    let img = load(arg(args, 0, "file")?)?.to_rgba8();
    let (w, h) = img.dimensions();
    let (l, t, r, b) = parse_box(opt(args, "--box"), w, h)?;
    let amin: i32 = opt(args, "--alpha-min").and_then(|v| v.parse().ok()).unwrap_or(-1);
    let (mut sum, mut sq, mut mn, mut mx) = ([0f64; 4], [0f64; 4], [255u8; 4], [0u8; 4]);
    let (mut count, mut opaque, mut transparent) = (0u64, 0u64, 0u64);
    for y in t..b {
        for x in l..r {
            let p = img.get_pixel(x, y).0;
            if p[3] == 255 { opaque += 1; }
            if p[3] == 0 { transparent += 1; }
            if (p[3] as i32) <= amin { continue; }
            count += 1;
            for c in 0..4 {
                sum[c] += p[c] as f64;
                sq[c] += (p[c] as f64) * (p[c] as f64);
                mn[c] = mn[c].min(p[c]);
                mx[c] = mx[c].max(p[c]);
            }
        }
    }
    let n = count.max(1) as f64;
    let mean: Vec<f64> = sum.iter().map(|s| s / n).collect();
    let std: Vec<f64> = (0..4).map(|c| (sq[c] / n - mean[c] * mean[c]).max(0.0).sqrt()).collect();
    Ok(json!({ "width": w, "height": h, "count": count, "mean": mean, "std": std,
        "min": if count > 0 { mn.to_vec() } else { vec![] }, "max": if count > 0 { mx.to_vec() } else { vec![] },
        "opaque": opaque, "transparent": transparent }))
}

/// image-diff <a> <b> [--threshold N] [--output diff.png] [--box l,t,r,b]
/// Per-pixel max channel difference over RGBA.
pub fn diff(args: &[String]) -> Res<Value> {
    let a = load(arg(args, 0, "a")?)?.to_rgba8();
    let b = load(arg(args, 1, "b")?)?.to_rgba8();
    if a.dimensions() != b.dimensions() {
        return Ok(json!({ "same_size": false, "a": [a.width(), a.height()], "b": [b.width(), b.height()] }));
    }
    let (w, h) = a.dimensions();
    let (l, t, r, bt) = parse_box(opt(args, "--box"), w, h)?;
    let threshold: u8 = opt(args, "--threshold").and_then(|v| v.parse().ok()).unwrap_or(0);
    let mut out = opt(args, "--output").map(|_| image::GrayImage::new(w, h));
    let (mut max, mut sum, mut sq, mut differing, mut total) = (0u8, 0f64, 0f64, 0u64, 0u64);
    let mut channel_max = [0u8; 4];
    for y in t..bt {
        for x in l..r {
            let (pa, pb) = (a.get_pixel(x, y).0, b.get_pixel(x, y).0);
            let mut d = 0u8;
            for c in 0..4 {
                let dc = pa[c].abs_diff(pb[c]);
                channel_max[c] = channel_max[c].max(dc);
                d = d.max(dc);
                sum += dc as f64;
                sq += (dc as f64) * (dc as f64);
            }
            total += 1;
            max = max.max(d);
            if d > threshold { differing += 1; }
            if let Some(o) = out.as_mut() { o.put_pixel(x, y, image::Luma([d])); }
        }
    }
    if let (Some(o), Some(p)) = (out, opt(args, "--output")) { save(&DynamicImage::ImageLuma8(o), p)?; }
    let n = (total * 4).max(1) as f64;
    Ok(json!({ "same_size": true, "width": w, "height": h, "max": max, "channel_max": channel_max,
        "mean": sum / n, "rms": (sq / n).sqrt(), "differing": differing, "total": total }))
}

/// image-resize <src> <dst> <w> <h> [filter]
pub fn resize(args: &[String]) -> Res<Value> {
    use image::imageops::FilterType::*;
    let img = load(arg(args, 0, "src")?)?;
    let dst = arg(args, 1, "dst")?;
    let w: u32 = arg(args, 2, "w")?.parse().map_err(|_| "bad width")?;
    let h: u32 = arg(args, 3, "h")?.parse().map_err(|_| "bad height")?;
    let filter = match args.get(4).map(|s| s.as_str()).unwrap_or("lanczos3") {
        "nearest" => Nearest,
        "triangle" | "bilinear" => Triangle,
        "catmullrom" | "bicubic" => CatmullRom,
        "gaussian" => Gaussian,
        _ => Lanczos3,
    };
    let out = img.resize_exact(w, h, filter);
    save(&out, dst)?;
    Ok(json!({ "width": w, "height": h }))
}

/// image-compose <spec.json|-> <dst> : {width,height,background:[r,g,b,a],items:[{file,x,y,w?,h?}]}
pub fn compose(args: &[String]) -> Res<Value> {
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| e.to_string())?;
    let dst = arg(args, 1, "dst")?;
    let w = spec["width"].as_u64().ok_or("width")? as u32;
    let h = spec["height"].as_u64().ok_or("height")? as u32;
    let bg: Vec<u8> = spec["background"].as_array().map(|a| a.iter().map(|v| v.as_u64().unwrap_or(0) as u8).collect()).unwrap_or(vec![0, 0, 0, 255]);
    let mut canvas = image::RgbaImage::from_pixel(w, h, image::Rgba([bg[0], bg[1], bg[2], *bg.get(3).unwrap_or(&255)]));
    for item in spec["items"].as_array().cloned().unwrap_or_default() {
        let file = item["file"].as_str().ok_or("item.file")?;
        let mut img = load(file)?;
        if let (Some(iw), Some(ih)) = (item["w"].as_u64(), item["h"].as_u64()) {
            img = img.resize_exact(iw as u32, ih as u32, image::imageops::FilterType::Lanczos3);
        }
        let (x, y) = (item["x"].as_i64().unwrap_or(0), item["y"].as_i64().unwrap_or(0));
        image::imageops::overlay(&mut canvas, &img.to_rgba8(), x, y);
    }
    save(&DynamicImage::ImageRgba8(canvas), dst)?;
    Ok(json!({ "path": dst }))
}
