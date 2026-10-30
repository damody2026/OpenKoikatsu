//! Colour work on textures: hair diffuse and tints, body create colours, line textures, overtex overlays and stamps, PNG tints.
use super::*;

/// Hair shader KKHairDiffuse `GetDiffuse() * MainTex`.
/// `GetDiffuse`: `d = 1 + mask.r*(C-1); d += mask.g*(C2-d); d += mask.b*(C3-d)`.
/// Missing ColorMask samples the shader default ("black") ??GetDiffuse = 1.
pub fn hair_get_diffuse(
    main: &[u8],
    mask: Option<&[u8]>,
    c1: [f32; 4],
    c2: [f32; 4],
    c3: [f32; 4],
) -> Option<Vec<u8>> {
    use std::io::Cursor;let _spot=crate::profile::spot("hair_get_diffuse");
    if !crate::okstrict::assembling() { return Some(main.to_vec()); }
    let src = crate::teximg::load_from_memory(main).ok()?.into_rgba8();
    let (w, h) = src.dimensions();
    if w == 0 || h == 0 {
        return None;
    }
    let mask_img = mask.and_then(|m| crate::teximg::load_from_memory(m).ok().map(|i| i.to_rgba8()));
    let (mw, mh) = mask_img
        .as_ref()
        .map(|i| i.dimensions())
        .unwrap_or((1, 1));
    let mut out = src.clone();
    // Rows are independent: baked in parallel (the same bytes as one by one).
    use rayon::prelude::*;
    let pixels: &mut [u8] = &mut out;
    pixels.par_chunks_mut(w as usize * 4).enumerate().for_each(|(y, row)| {
        let y = y as u32;
        for x in 0..w {
            let p = src.get_pixel(x, y).0;
            let (mr, mg, mb) = if let Some(mi) = mask_img.as_ref() {
                let mx = x * mw / w.max(1);
                let my = y * mh / h.max(1);
                let m = mi.get_pixel(mx.min(mw - 1), my.min(mh - 1)).0;
                (m[0] as f32 / 255.0, m[1] as f32 / 255.0, m[2] as f32 / 255.0)
            } else {
                (0.0, 0.0, 0.0)
            };
            let mut d = [
                1.0 + mr * (c1[0] - 1.0),
                1.0 + mr * (c1[1] - 1.0),
                1.0 + mr * (c1[2] - 1.0),
            ];
            d[0] += mg * (c2[0] - d[0]);
            d[1] += mg * (c2[1] - d[1]);
            d[2] += mg * (c2[2] - d[2]);
            d[0] += mb * (c3[0] - d[0]);
            d[1] += mb * (c3[1] - d[1]);
            d[2] += mb * (c3[2] - d[2]);
            row[x as usize * 4..x as usize * 4 + 4].copy_from_slice(&[
                ((p[0] as f32) * d[0]).clamp(0.0, 255.0) as u8,
                ((p[1] as f32) * d[1]).clamp(0.0, 255.0) as u8,
                ((p[2] as f32) * d[2]).clamp(0.0, 255.0) as u8,
                p[3],
            ]);
        }
    });
    crate::teximg::encode_rgba_owned(out)
}

/// Fallback when ColorMask is missing: `_Color * MainTex` (ColorMask.r = 1).
pub fn tint_hair_png(png: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    hair_get_diffuse(png, None, color, color, color)
}

pub fn overlay_skin_png(png: &[u8], skin: [f32; 4]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut img = crate::teximg::load_from_memory(png).ok()?.into_rgba8();
    let (sr, sg, sb) = (
        skin[0].clamp(0.0, 1.0),
        skin[1].clamp(0.0, 1.0),
        skin[2].clamp(0.0, 1.0),
    );
    // KK body shader is MainTex * skinColor. Multiply keeps nipples / navel / makeup.
    for p in img.pixels_mut() {
        if (p[0] as u16) + (p[1] as u16) + (p[2] as u16) < 18 {
            continue;
        }
        p[0] = ((p[0] as f32) * sr).clamp(0.0, 255.0) as u8;
        p[1] = ((p[1] as f32) * sg).clamp(0.0, 255.0) as u8;
        p[2] = ((p[2] as f32) * sb).clamp(0.0, 255.0) as u8;
    }
    crate::teximg::encode_rgba_owned(img)
}

/// Koikano/create_skinbody fragment, without optional sunburn/paint textures:
/// MainTex * Color * lerp(1, Color2, ColorMask.g * Color2.a)
///         * lerp(1, Color5, ColorMask.b).
/// ColorMask.r is not read by this base/nail branch (DXBC blob 1).
pub fn apply_body_create_colors(
    main: &[u8], mask: &[u8], skin: [f32; 4], sub: [f32; 4], nail: [f32; 4],
) -> Option<Vec<u8>> {
    apply_body_create_colors_sampled(main,mask,skin,sub,nail,false)
}

pub fn apply_body_create_colors_sampled(
    main:&[u8],mask:&[u8],skin:[f32;4],sub:[f32;4],nail:[f32;4],main_srgb:bool,
)->Option<Vec<u8>> {
    use std::io::Cursor;
    let src = crate::teximg::load_from_memory(main).ok()?.into_rgba8();
    let mask = crate::teximg::load_from_memory(mask).ok()?.into_rgba8();
    let (w, h) = src.dimensions();
    let (mw, mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 {
        return None;
    }
    let mut out = src;
    // Same arithmetic per texel as before, without recomputing the card colours' and the u8 texels' transfer
    // functions for every channel of every texel, and with the rows in parallel (~0.3 s for a 2048 body).
    let linear=|v:f32,c:usize| if c<3 {crate::srgb_channel_to_linear(v)} else {v};
    let (sub_l,nail_l,skin_l):([f32;4],[f32;4],[f32;4])=(std::array::from_fn(|c|linear(sub[c],c)),std::array::from_fn(|c|linear(nail[c],c)),std::array::from_fn(|c|linear(skin[c],c)));
    let base_lut:[f32;256]=std::array::from_fn(|i|{let base=i as f32/255.;if main_srgb {crate::srgb_channel_to_linear(base)} else {base}});
    let raw_lut:[f32;256]=std::array::from_fn(|i|i as f32/255.);
    {
        use rayon::prelude::*;
        let row=w as usize*4;
        out.as_mut().par_chunks_mut(row).enumerate().for_each(|(y,line)|{
            for x in 0..w as usize {
                let m=crate::sample_rgba_clamped(&mask,(x as f32+0.5)/w as f32,(y as f32+0.5)/h as f32);
                let sub_weight=m[1]*sub[3];
                let nail_weight=m[2];
                let p=&mut line[x*4..x*4+4];
                for c in 0..4 {
                    let sub_tint = 1.0 + sub_weight * (sub_l[c] - 1.0);
                    let nail_tint = 1.0 + nail_weight * (nail_l[c] - 1.0);
                    let base=if c<3 {base_lut[p[c] as usize]} else {raw_lut[p[c] as usize]};
                    let value=base * skin_l[c] * sub_tint * nail_tint;
                    let value=if c<3 {crate::linear_channel_to_srgb(value)}else{value};
                    p[c]=(value*255.).clamp(0.,255.).round() as u8;
                }
            }
        });
    }
    crate::teximg::encode_rgba_owned(out)
}

/// `Koikano/main_skin` `_LINETEXON_ON`: multiply `_DetailMask` (black lines on white).
pub fn multiply_line_tex(base: &[u8], line: &[u8]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.into_rgba8();
    let src = crate::teximg::load_from_memory(line).ok()?.into_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    for y in 0..dh {
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let sy = (y as u64 * sh as u64 / dh as u64) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1)).0;
            let d = dst.get_pixel_mut(x, y);
            d[0] = ((d[0] as u16 * p[0] as u16) / 255) as u8;
            d[1] = ((d[1] as u16 * p[1] as u16) / 255) as u8;
            d[2] = ((d[2] as u16 * p[2] as u16) / 255) as u8;
        }
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Display material `_overtex1` / `_overtex2`: lerp(base, over * color, over.a) on the same UV.
pub fn overlay_overtex(base: &[u8], over: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.into_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.into_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    let (cr, cg, cb, ca) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
        color[3].clamp(0.0, 1.0),
    );
    let mut used = 0u32;
    for y in 0..dh {
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let sy = (y as u64 * sh as u64 / dh as u64) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32) / 255.0 * ca;
            if a < 0.02 {
                continue;
            }
            used += 1;
            let d = dst.get_pixel_mut(x, y);
            let sr = (p[0] as f32) * cr;
            let sg = (p[1] as f32) * cg;
            let sb = (p[2] as f32) * cb;
            d[0] = (d[0] as f32 * (1.0 - a) + sr * a).clamp(0.0, 255.0) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + sg * a).clamp(0.0, 255.0) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + sb * a).clamp(0.0, 255.0) as u8;
        }
    }
    if used < 8 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

pub fn overlay_overtex_mapped(
    base: &[u8],
    over: &[u8],
    color: [f32; 4],
    uv0: &[[f32; 2]],
    uv_over: &[[f32; 2]],
    indices: &[u32],
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    if uv0.len() != uv_over.len() || uv0.is_empty() || indices.len() < 3 {
        return None;
    }
    let mut dst = crate::teximg::load_from_memory(base).ok()?.into_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.into_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    let (cr, cg, cb, ca) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
        color[3].clamp(0.0, 1.0),
    );
    let mut used = 0u32;
    for tri in indices.chunks_exact(3) {
        let i0 = tri[0] as usize;
        let i1 = tri[1] as usize;
        let i2 = tri[2] as usize;
        if i0 >= uv0.len() || i1 >= uv0.len() || i2 >= uv0.len() {
            continue;
        }
        let p = |uv: [f32; 2]| -> [f32; 2] {
            [uv[0] * dw as f32, (1.0 - uv[1]) * dh as f32]
        };
        let a = p(uv0[i0]);
        let b = p(uv0[i1]);
        let c = p(uv0[i2]);
        let minx = a[0].min(b[0]).min(c[0]).floor().max(0.0) as i32;
        let miny = a[1].min(b[1]).min(c[1]).floor().max(0.0) as i32;
        let maxx = a[0].max(b[0]).max(c[0]).ceil().min(dw as f32 - 1.0) as i32;
        let maxy = a[1].max(b[1]).max(c[1]).ceil().min(dh as f32 - 1.0) as i32;
        if minx > maxx || miny > maxy {
            continue;
        }
        let den = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if den.abs() < 1e-8 {
            continue;
        }
        let o0 = uv_over[i0];
        let o1 = uv_over[i1];
        let o2 = uv_over[i2];
        for y in miny..=maxy {
            for x in minx..=maxx {
                let px = x as f32 + 0.5;
                let py = y as f32 + 0.5;
                let w1 = ((px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1])) / den;
                let w2 = ((b[0] - a[0]) * (py - a[1]) - (px - a[0]) * (b[1] - a[1])) / den;
                let w0 = 1.0 - w1 - w2;
                if w0 < -0.001 || w1 < -0.001 || w2 < -0.001 {
                    continue;
                }
                let uo = (w0 * o0[0] + w1 * o1[0] + w2 * o2[0]).clamp(0.0, 1.0);
                let vo = (w0 * o0[1] + w1 * o1[1] + w2 * o2[1]).clamp(0.0, 1.0);
                let sx = (uo * sw as f32) as u32;
                let sy = ((1.0 - vo) * sh as f32) as u32;
                let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
                let aov = (p[3] as f32) / 255.0 * ca;
                if aov < 0.02 {
                    continue;
                }
                used += 1;
                let d = dst.get_pixel_mut(x as u32, y as u32);
                d[0] = (d[0] as f32 * (1.0 - aov) + (p[0] as f32) * cr * aov).clamp(0.0, 255.0) as u8;
                d[1] = (d[1] as f32 * (1.0 - aov) + (p[1] as f32) * cg * aov).clamp(0.0, 255.0) as u8;
                d[2] = (d[2] as f32 * (1.0 - aov) + (p[2] as f32) * cb * aov).clamp(0.0, 255.0) as u8;
            }
        }
    }
    eprintln!("ok-extract: overlay_mapped used={used} dst={dw}x{dh} src={sw}x{sh}");
    let _ = std::fs::create_dir_all(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw");
    let _ = dst.save(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw\overlay_mapped.png");
    if used < 8 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Same as `overlay_overtex`, but samples `over` with the per-layer UV layout
/// the game passes to `cf_m_face_create` (`_hokuro` for the mole, `_paint1/2`
/// for face paint): `uv' = (uv - offset) / scale`.
pub fn overlay_overtex_uv(
    base: &[u8],
    over: &[u8],
    color: [f32; 4],
    offset: [f32; 2],
    scale: [f32; 2],
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.into_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.into_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 || scale[0] == 0.0 || scale[1] == 0.0 {
        return None;
    }
    let (cr, cg, cb) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
    );
    let mut used = 0u32;
    for y in 0..dh {
        for x in 0..dw {
            let u = (x as f32 + 0.5) / dw as f32;
            let v = (y as f32 + 0.5) / dh as f32;
            let su = (u - offset[0]) / scale[0];
            let sv = (v - offset[1]) / scale[1];
            if !(0.0..1.0).contains(&su) || !(0.0..1.0).contains(&sv) {
                continue;
            }
            let sx = (su * sw as f32) as u32;
            let sy = (sv * sh as f32) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32) / 255.0;
            if a < 0.02 {
                continue;
            }
            used += 1;
            let d = dst.get_pixel_mut(x, y);
            d[0] = (d[0] as f32 * (1.0 - a) + (p[0] as f32) * cr * a).clamp(0.0, 255.0) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + (p[1] as f32) * cg * a).clamp(0.0, 255.0) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + (p[2] as f32) * cb * a).clamp(0.0, 255.0) as u8;
        }
    }
    if used < 4 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Composite an Illusion mask texture (shape in alpha/luma) onto `base`,
/// painting a flat tint colour. Used for lips (and other `_Color`-tinted
/// overlays) where the texture RGB is itself a mask.
pub fn blend_mask_tint(base: &[u8], over: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.into_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.into_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    let (cr, cg, cb) = (
        color[0].clamp(0.0, 1.0),
        color[1].clamp(0.0, 1.0),
        color[2].clamp(0.0, 1.0),
    );
    let ca = color[3].clamp(0.0, 1.0);
    let mut used = 0u32;
    for y in 0..dh {
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let sy = (y as u64 * sh as u64 / dh as u64) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32 / 255.0) * ca;
            if a <= 0.0 {
                continue;
            }
            used += 1;
            let d = dst.get_pixel_mut(x, y);
            let sr = (p[0] as f32 / 255.0) * cr * 255.0;
            let sg = (p[1] as f32 / 255.0) * cg * 255.0;
            let sb = (p[2] as f32 / 255.0) * cb * 255.0;
            d[0] = (d[0] as f32 * (1.0 - a) + sr * a).clamp(0.0, 255.0) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + sg * a).clamp(0.0, 255.0) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + sb * a).clamp(0.0, 255.0) as u8;
        }
    }
    if used < 4 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Stamp a small overtex (centered areola) onto body UVs. `_nipsize` scales the blit.
pub fn stamp_overtex(
    base: &[u8],
    over: &[u8],
    color: [f32; 4],
    uvs: &[[f32; 2]],
    nipsize: f32,
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    if uvs.is_empty() {
        return None;
    }
    let mut dst = crate::teximg::load_from_memory(base).ok()?.into_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.into_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw < 8 || dh < 8 || sw < 4 || sh < 4 {
        return None;
    }
    let (cr, cg, cb) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
    );
    let scale = (0.018 + 0.03 * nipsize.clamp(0.0, 1.0)) * dw as f32;
    for uv in uvs {
        let cx = uv[0].fract().abs() * dw as f32;
        let cy = (1.0 - uv[1].fract().abs()) * dh as f32;
        let x0 = (cx - scale).max(0.0) as u32;
        let y0 = (cy - scale).max(0.0) as u32;
        let x1 = ((cx + scale) as u32).min(dw - 1);
        let y1 = ((cy + scale) as u32).min(dh - 1);
        for y in y0..=y1 {
            for x in x0..=x1 {
                let u = ((x as f32 - (cx - scale)) / (scale * 2.0)).clamp(0.0, 1.0);
                let v = ((y as f32 - (cy - scale)) / (scale * 2.0)).clamp(0.0, 1.0);
                let sx = (u * (sw - 1) as f32) as u32;
                let sy = (v * (sh - 1) as f32) as u32;
                let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32) / 255.0 * color[3].clamp(0.0, 1.0);
            if a < 0.02 {
                continue;
            }
                // Illusion overtex is a mask tinted by _Color; its own RGB is
                // often a green mask, so stamp the colour shaped by the mask.
                let d = dst.get_pixel_mut(x, y);
                let sr = (cr * 255.0).clamp(0.0, 255.0);
                let sg = (cg * 255.0).clamp(0.0, 255.0);
                let sb = (cb * 255.0).clamp(0.0, 255.0);
                d[0] = (d[0] as f32 * (1.0 - a) + sr * a).clamp(0.0, 255.0) as u8;
                d[1] = (d[1] as f32 * (1.0 - a) + sg * a).clamp(0.0, 255.0) as u8;
                d[2] = (d[2] as f32 * (1.0 - a) + sb * a).clamp(0.0, 255.0) as u8;
            }
        }
    }
    crate::teximg::encode_rgba_owned(dst)
}

pub fn tint_png(png: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    if !crate::okstrict::assembling() { return None; }
    let (cr, cg, cb) = (color[0].clamp(0.0, 2.0), color[1].clamp(0.0, 2.0), color[2].clamp(0.0, 2.0));
    if (cr - 1.0).abs() < 0.02 && (cg - 1.0).abs() < 0.02 && (cb - 1.0).abs() < 0.02 {
        return None;
    }
    let parameters:Vec<u8>=[cr,cg,cb].into_iter().flat_map(|v|v.to_bits().to_le_bytes()).collect();
    crate::png_transform_cache::transform("tint_rgb_v1",png,&parameters,||tint_png_uncached(png,[cr,cg,cb]))
}
pub(crate) fn tint_png_uncached(png:&[u8],color:[f32;3])->Option<Vec<u8>>{let _spot=crate::profile::spot("tint_png");
    use std::io::Cursor;
    let [cr,cg,cb]=color;
    let mut img = crate::teximg::load_from_memory(png).ok()?.into_rgba8();
    for p in img.pixels_mut() {
        p[0] = ((p[0] as f32) * cr).clamp(0.0, 255.0) as u8;
        p[1] = ((p[1] as f32) * cg).clamp(0.0, 255.0) as u8;
        p[2] = ((p[2] as f32) * cb).clamp(0.0, 255.0) as u8;
    }
    crate::teximg::encode_rgba_owned(img)
}
