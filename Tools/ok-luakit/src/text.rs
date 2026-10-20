//! TrueType text rasterisation for generated textures (signs, labels).
use crate::{arg, read_input, Res};
use ab_glyph::{Font, FontVec, PxScale, ScaleFont};
use serde_json::{json, Value};

/// text-render <spec.json|->
/// {width,height,background:[r,g,b,a],output,items:[{text,font,size,x,y,color:[r,g,b,a],anchor:"lt"|"mm"|...}]}
/// Anchors follow PIL: first letter l/m/r horizontal, second t/m/b (top/middle/baseline-ish bottom).
/// Returns {path, items:[{bbox:[l,t,r,b]}]}.
pub fn render(args: &[String]) -> Res<Value> {
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| e.to_string())?;
    let w = spec["width"].as_u64().ok_or("width")? as u32;
    let h = spec["height"].as_u64().ok_or("height")? as u32;
    let rgba = |v: &Value, d: [u8; 4]| -> [u8; 4] {
        v.as_array().map(|a| {
            let mut c = d;
            for (i, x) in a.iter().take(4).enumerate() { c[i] = x.as_u64().unwrap_or(0) as u8; }
            if a.len() == 3 { c[3] = 255; }
            c
        }).unwrap_or(d)
    };
    let bg = rgba(&spec["background"], [0, 0, 0, 0]);
    let mut canvas = image::RgbaImage::from_pixel(w, h, image::Rgba(bg));
    let mut boxes = Vec::new();
    for item in spec["items"].as_array().cloned().unwrap_or_default() {
        let font_path = item["font"].as_str().ok_or("item.font")?;
        let data = std::fs::read(font_path).map_err(|e| format!("{font_path}: {e}"))?;
        let index = item["font_index"].as_u64().unwrap_or(0) as u32;
        let font = FontVec::try_from_vec_and_index(data, index).map_err(|e| format!("{font_path}: {e}"))?;
        let size = item["size"].as_f64().unwrap_or(32.0) as f32;
        let scaled = font.as_scaled(PxScale::from(size));
        let text = item["text"].as_str().unwrap_or("");
        let color = rgba(&item["color"], [0, 0, 0, 255]);
        // Layout on one line.
        let mut glyphs = Vec::new();
        let mut caret = 0f32;
        let mut prev = None;
        for ch in text.chars() {
            let id = scaled.glyph_id(ch);
            if let Some(p) = prev { caret += scaled.kern(p, id); }
            glyphs.push((id, caret));
            caret += scaled.h_advance(id);
            prev = Some(id);
        }
        let ascent = scaled.ascent();
        let descent = scaled.descent();
        let anchor = item["anchor"].as_str().unwrap_or("la");
        let mut chars = anchor.chars();
        let ha = chars.next().unwrap_or('l');
        let va = chars.next().unwrap_or('a');
        let x0 = item["x"].as_f64().unwrap_or(0.0) as f32 - match ha { 'm' => caret / 2.0, 'r' => caret, _ => 0.0 };
        let baseline = item["y"].as_f64().unwrap_or(0.0) as f32 + match va {
            't' | 'a' => ascent,
            'm' => (ascent + descent) / 2.0,
            'b' | 'd' => descent,
            _ => 0.0, // 's' baseline
        };
        let (mut bl, mut bt, mut br, mut bb) = (f32::MAX, f32::MAX, f32::MIN, f32::MIN);
        for (id, gx) in glyphs {
            let glyph = id.with_scale_and_position(PxScale::from(size), ab_glyph::point(x0 + gx, baseline));
            if let Some(outlined) = font.outline_glyph(glyph) {
                let bounds = outlined.px_bounds();
                bl = bl.min(bounds.min.x); bt = bt.min(bounds.min.y); br = br.max(bounds.max.x); bb = bb.max(bounds.max.y);
                outlined.draw(|gx, gy, cov| {
                    let (px, py) = (bounds.min.x as i32 + gx as i32, bounds.min.y as i32 + gy as i32);
                    if px < 0 || py < 0 || px >= w as i32 || py >= h as i32 { return; }
                    let dst = canvas.get_pixel_mut(px as u32, py as u32);
                    let a = cov.clamp(0.0, 1.0) * color[3] as f32 / 255.0;
                    let da = dst.0[3] as f32 / 255.0;
                    let oa = a + da * (1.0 - a);
                    for c in 0..3 {
                        let v = if oa > 0.0 { (color[c] as f32 * a + dst.0[c] as f32 * da * (1.0 - a)) / oa } else { 0.0 };
                        dst.0[c] = v.round().clamp(0.0, 255.0) as u8;
                    }
                    dst.0[3] = (oa * 255.0).round() as u8;
                });
            }
        }
        boxes.push(if bl <= br { json!({ "bbox": [bl.floor(), bt.floor(), br.ceil(), bb.ceil()], "advance": caret }) } else { json!({ "bbox": Value::Null, "advance": caret }) });
    }
    let out = spec["output"].as_str().ok_or("output")?;
    if let Some(parent) = std::path::Path::new(out).parent() {
        if !parent.as_os_str().is_empty() { std::fs::create_dir_all(parent).map_err(|e| e.to_string())?; }
    }
    canvas.save(out).map_err(|e| format!("{out}: {e}"))?;
    Ok(json!({ "path": out, "items": boxes }))
}
