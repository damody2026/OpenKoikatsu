//! Textures of Unity bundles: inline payload repair, Texture2D payloads to PNG (and the derived PNG store), named and exact texture lookups, texture picking for meshes, materials.
use super::*;

pub(crate) fn recover_inline_texture_data(raw: &[u8], min_bytes: usize) -> Option<Vec<u8>> {
    let mut found = None;
    // The inline image size precedes the mip chain. Name padding and Unity
    // version change its header offset; streamed textures have no such blob.
    let name_bytes=u32::from_le_bytes(raw.get(..4)?.try_into().ok()?) as usize;
    let after_name=(name_bytes.checked_add(7)?) & !3;
    if after_name>raw.len(){return None;}
    let header_end=raw.len().min(after_name.checked_add(256)?);
    for pos in (after_name..header_end.saturating_sub(4)).step_by(4) {
        let size = u32::from_le_bytes(raw[pos..pos + 4].try_into().ok()?) as usize;
        let remaining = raw.len().saturating_sub(pos + 4);
        if size >= min_bytes && size <= remaining && remaining - size <= 128 {
            found = Some((pos + 4, size));
        }
    }
    let (start, size) = found?;
    Some(raw[start..start + size].to_vec())
}

pub(crate) fn repair_inline_texture_payload(raw: &[u8], tex: &mut unity_asset_decode::texture::Texture2D, min_bytes: usize) {
    // A best-effort converter can return the header/trailer along with the
    // mip chain. A nonempty payload large enough for mip 0 is not proof that
    // it starts at the declared byte array. Prefer the serialized array count
    // and its bounds for both truncated and oversized converter results.
    if let Some(bytes) = recover_inline_texture_data(raw, min_bytes) {
        if tex.image_data != bytes {
            if std::env::var_os("OK_TEXDIAG").is_some() {
                eprintln!("Texture payload repaired name={} old={} declared={}",tex.name,tex.image_data.len(),bytes.len());
            }
            tex.data_size = bytes.len() as i32;
            tex.image_data = bytes;
        }
    }
}

pub(crate) fn read_texture_payload<B: StreamBytes>(object:&UnityObject,files:&[(String,B)],raw_bundle:Option<&[u8]>)->Option<unity_asset_decode::texture::Texture2D> {
    use unity_asset_decode::texture::{Texture2D, TextureFormat, TextureProcessor};
    let proc = TextureProcessor::new(version());
    let mut tex = proc
        .convert_object(object)
        .ok()
        .or_else(|| parse_tex_unity2019(object.raw_data()))?;
    let fill_stream = |t: &mut Texture2D| {
        if !t.image_data.is_empty() || !t.is_streamed() {
            return;
        }
        if let Some(b) = load_stream(
            files,
            &t.stream_info.path,
            t.stream_info.offset,
            t.stream_info.size,
        ) {
            t.image_data = b;
            t.data_size = t.image_data.len() as i32;
        } else if let Some(raw) = raw_bundle {
            if let Some(b) = try_slice(raw, t.stream_info.offset, t.stream_info.size) {
                t.image_data = b;
                t.data_size = t.image_data.len() as i32;
            }
        }
    };
    fill_stream(&mut tex);
    if tex.image_data.is_empty() {
        if let Some(mut t2) = parse_tex_unity2019(object.raw_data()) {
            fill_stream(&mut t2);
            if !t2.image_data.is_empty() {
                tex = t2;
            }
        }
    }
    // Validate the declared inline mip chain. The converter can return either
    // a tiny prefix or a large span that still includes part of the header.
    let blocks = if tex.width >= 4 && tex.height >= 4 {
        ((tex.width as usize + 3) / 4)
            .checked_mul((tex.height as usize + 3) / 4)
    } else {
        None
    };
    let top_mip_bytes = match tex.format {
        TextureFormat::DXT1 => blocks.and_then(|count| count.checked_mul(8)),
        TextureFormat::DXT5 | TextureFormat::BC7 => blocks.and_then(|count| count.checked_mul(16)),
        TextureFormat::Alpha8 => (tex.width as usize).checked_mul(tex.height as usize),
        TextureFormat::RGB24 => (tex.width as usize).checked_mul(tex.height as usize)?.checked_mul(3),
        TextureFormat::RGBA32 | TextureFormat::ARGB32 | TextureFormat::BGRA32 =>
            (tex.width as usize).checked_mul(tex.height as usize)?.checked_mul(4),
        TextureFormat::RGB565 | TextureFormat::RGBA4444 | TextureFormat::ARGB4444 | TextureFormat::R16 =>
            (tex.width as usize).checked_mul(tex.height as usize)?.checked_mul(2),
        _ => None,
    };
    // Texels read from the stream (.resS) are exactly what Unity uploads; the inline-array repair would take a
    // false length match in the object header instead (small streamed textures, e.g. the 8x8 Studio ColorMasks).
    let from_stream = tex.is_streamed() && tex.image_data.len() == tex.stream_info.size as usize;
    if let Some(min_bytes) = top_mip_bytes.filter(|_| !from_stream) {
        repair_inline_texture_payload(object.raw_data(), &mut tex, min_bytes);
    }
    Some(tex)
}

pub(crate) fn png_from_texture<B: StreamBytes>(
    object: &UnityObject,
    files: &[(String, B)],
    raw_bundle: Option<&[u8]>,
) -> Option<(String, Vec<u8>)> {
    let tex=read_texture_payload(object,files,raw_bundle)?;
    png_from_payload(tex,object.name())
}

/// PNG of a texture's texels (OK textures store the texels; Unity and OK
/// paths decode through this one function).
/// A Texture2D object of `files` (a parsed bundle's files, for streamed texels) as PNG, found by the caller (by path id).
pub(crate) fn texture_object_png(object: &UnityObject, files: &[(String, BundleFileBytes)]) -> Option<Vec<u8>> {
    read_texture_payload(object, files, None).and_then(|t| png_from_payload(t, object.name())).map(|(_, png)| png)
}

pub(crate) fn png_from_payload(mut tex: unity_asset_decode::texture::Texture2D, object_name: Option<String>) -> Option<(String, Vec<u8>)> {let _spot=crate::profile::spot("png_from_payload");
    if !crate::okstrict::assembling() {
        let name = if tex.name.is_empty() { object_name.unwrap_or_else(|| "tex".into()) } else { tex.name.clone() };
        return solid_png([1.0; 4], 1, 1).map(|png| (name, png));
    }
    use crate::memosnap::{TEX,active,get,put};
    // Recorded into / replayed from the card (OK_CARD_MEMO=tex): keyed by the texels, format and size.
    if !active(TEX) {
        // Block-compressed maps decode from the texels, format and size alone: the same texture of another card,
        // outfit or open (the base body's and face's, a shared mod's) is decoded and encoded once (png_transform_cache).
        use unity_asset_decode::texture::TextureFormat;
        let banded = tex.width as u64 * tex.height as u64 >= 512 * 512;
        if matches!(tex.format, TextureFormat::DXT1 | TextureFormat::BC7) || (banded && tex.format == TextureFormat::DXT5) {
            let name = if tex.name.is_empty() { object_name.clone().unwrap_or_else(|| "tex".into()) } else { tex.name.clone() };
            let parameters = format!("{:?}|{}|{}|{}", tex.format, tex.width, tex.height, crate::teximg::encoding_tag());
            let data = std::mem::take(&mut tex.image_data);
            let png = crate::png_transform_cache::transform("payload_png_v1", &data, parameters.as_bytes(), || {
                tex.image_data = data.clone();
                png_from_payload_uncached(tex, object_name).map(|(_, png)| png)
            })?;
            return Some((name, png));
        }
        return png_from_payload_uncached(tex, object_name);
    }
    let key = {
        use sha2::Digest;
        let mut h = sha2::Sha256::new();
        h.update(b"payload_v1");
        h.update(format!("{:?}|{}|{}|{}", tex.format, tex.width, tex.height, crate::teximg::raw_enabled()).as_bytes());
        h.update(&tex.image_data);
        h.finalize().to_vec()
    };
    let name = if tex.name.is_empty() { object_name.clone().unwrap_or_else(|| "tex".into()) } else { tex.name.clone() };
    if let Some(bytes) = get(TEX, &key) { return Some((name, (*bytes).clone())); }
    let out = png_from_payload_uncached(tex, object_name)?;
    put(TEX, &key, || Some(out.1.clone()));
    Some(out)
}

/// A library model's texture as derived (name, PNG), kept between processes: (model id, texture name, encoding) names
/// it, and a library model is content-addressed. The first card of a process derived the base body's and head's again
/// (~0.5 s). Bounded: past 8 GB the oldest go.
pub(crate) fn derived_png_path(id: &crate::oklib::Id, key: &str) -> Option<std::path::PathBuf> {
    use sha2::Digest;
    let mut h = sha2::Sha256::new();
    h.update(b"derived_png_v1");
    h.update(id);
    h.update(key.as_bytes());
    h.update(crate::teximg::encoding_tag().as_bytes());
    let hash = h.finalize();
    let hex: String = hash[..12].iter().map(|b| format!("{b:02x}")).collect();
    Some(crate::oklib::session_cache_location()?.join("derived-textures").join(&hex[..2]).join(format!("{hex}.tex")))
}
pub(crate) fn derived_png_load(id: &crate::oklib::Id, key: &str) -> Option<(String, Vec<u8>)> {
    let bytes = std::fs::read(derived_png_path(id, key)?).ok()?;
    let n = u32::from_le_bytes(bytes.get(..4)?.try_into().ok()?) as usize;
    let name = String::from_utf8(bytes.get(4..4 + n)?.to_vec()).ok()?;
    Some((name, bytes.get(4 + n..)?.to_vec()))
}
pub(crate) fn derived_png_save(id: &crate::oklib::Id, key: &str, name: &str, png: &[u8]) {
    use std::sync::atomic::{AtomicU64, Ordering};
    let Some(path) = derived_png_path(id, key) else { return };
    let Some(dir) = path.parent() else { return };
    let _ = std::fs::create_dir_all(dir);
    let mut out = Vec::with_capacity(4 + name.len() + png.len());
    out.extend_from_slice(&(name.len() as u32).to_le_bytes()); out.extend_from_slice(name.as_bytes()); out.extend_from_slice(png);
    static WRITES: AtomicU64 = AtomicU64::new(0);
    let n = WRITES.fetch_add(1, Ordering::Relaxed);
    let tmp = path.with_extension(format!("{}.{n}.tmp", std::process::id()));
    if std::fs::write(&tmp, &out).is_ok() && std::fs::rename(&tmp, &path).is_err() { let _ = std::fs::remove_file(&tmp); }
    // Now and then: trim the oldest beyond the budget.
    if n % 256 == 255 {
        let Some(root) = dir.parent() else { return };
        let mut files: Vec<_> = std::fs::read_dir(root).into_iter().flatten().flatten()
            .flat_map(|d| std::fs::read_dir(d.path()).into_iter().flatten().flatten())
            .filter_map(|e| { let m = e.metadata().ok()?; Some((m.modified().ok()?, m.len(), e.path())) }).collect();
        files.sort();
        let mut total: u64 = files.iter().map(|f| f.1).sum();
        for (_, len, p) in files { if total <= 8 << 30 { break; } if std::fs::remove_file(&p).is_ok() { total -= len; } }
    }
}

pub(crate) fn png_from_payload_uncached(tex: unity_asset_decode::texture::Texture2D, object_name: Option<String>) -> Option<(String, Vec<u8>)> {
    use std::io::Cursor;
    use unity_asset_decode::texture::{TextureFormat,TextureProcessor};
    let proc=TextureProcessor::new(version());
    if tex.image_data.is_empty() {
        return None;
    }
    let w = tex.width as u32;
    let h = tex.height as u32;
    if w < 4 || h < 4 {
        return None;
    }
    // Large BC1/BC3/BC7 maps: decoded in bands of block rows in parallel (blocks are independent; the bytes are the
    // ones the single-call paths below produce: [p>>16, p>>8, p, p>>24] of the decoder's packed texel).
    if w as usize * h as usize >= 512 * 512 && matches!(tex.format, TextureFormat::DXT1 | TextureFormat::DXT5 | TextureFormat::BC7) {
        if let Some(img) = decode_bc_banded(&tex.image_data, w as usize, h as usize, tex.format) {
            let buf = crate::teximg::encode_rgba_owned(img)?;
            let name = if tex.name.is_empty() { object_name.unwrap_or_else(|| "tex".into()) } else { tex.name };
            return Some((name, buf));
        }
    }
    let img = if tex.format == TextureFormat::DXT1 {
        // Unity's DXT1 is DXGI BC1 RGBA: endpoint0 <= endpoint1 reserves
        // selector 3 for transparent black. The generic decoder uses BC1 RGB
        // and silently turns cutouts into opaque black (including iris masks).
        // Keep the decoder's BGRA packing here; the shared S3TC correction
        // below changes it to RGBA exactly once.
        decode_unity_dxt1(&tex.image_data, w, h)?
    } else if tex.format == TextureFormat::BC7 {
        // unity-asset-decode's validate() rejects BC7; decode it directly.
        let mut output = vec![0u32; (w as usize) * (h as usize)];
        texture2ddecoder::decode_bc7(&tex.image_data, w as usize, h as usize, &mut output).ok()?;
        let rgba: Vec<u8> = output
            .iter()
            .flat_map(|&p| {
                [
                    ((p >> 16) & 0xFF) as u8,
                    ((p >> 8) & 0xFF) as u8,
                    (p & 0xFF) as u8,
                    ((p >> 24) & 0xFF) as u8,
                ]
            })
            .collect();
        image::RgbaImage::from_raw(w, h, rgba)?
    } else {
        match proc.decode_texture(&tex) {
            Ok(image)=>image,
            Err(error)=>{if std::env::var_os("OK_TEXDIAG").is_some(){eprintln!("Texture decode name={} dimensions={}x{} format={:?} bytes={} error={error}",tex.name,w,h,tex.format,tex.image_data.len());}return None;}
        }
    };
    // texture2ddecoder packs `color(r,g,b,a)` as little-endian [b,g,r,a]; both it
    // and unity-asset-decode's S3TC/BC wrappers then read back byte 0 as "R", so
    // red and blue are swapped for DXT1/DXT5 (BC7 fixed above).
    let img = if matches!(tex.format, TextureFormat::DXT1 | TextureFormat::DXT5) {
        let mut im = img;
        for p in im.pixels_mut() {
            p.0.swap(0, 2);
        }
        im
    } else {
        img
    };
    let buf = crate::teximg::encode_rgba_owned(img)?;
    let name = if tex.name.is_empty() {
        object_name.unwrap_or_else(|| "tex".into())
    } else {
        tex.name
    };
    Some((name, buf))
}

pub(crate) fn keep_base_tex(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.contains("cf_body")
        || n.contains("cf_face")
        || n.contains("cf_head")
        || n.contains("cf_t_")
        || n.contains("nip")
        || n.contains("underhair")
        || n.contains("hitomi")
        || n.contains("eyeline")
        || n.contains("mayuge")
        || n.contains("noseline")
        || n.contains("sirome")
        || n.contains("tooth")
        || n.contains("tang")
        || n.contains("face")
        || n.contains("head")
        || n.contains("khohoaka")
        || n.contains("hoho")
}

pub(crate) fn textures_from_serialized(
    serialized: &unity_asset_binary::asset::SerializedFile,
    files: &[(String, Vec<u8>)],
    filter: bool,
) -> Vec<(String, Vec<u8>)> {
    const TEXTURE_2D: i32 = 28;
    let mut out = Vec::new();
    for info in serialized.objects_of_type(TEXTURE_2D) {
        let Ok(object) = UnityObject::from_serialized_file(serialized, info) else {
            continue;
        };
        if filter {
            let n = object.name().unwrap_or_default();
            if !keep_base_tex(&n) {
                continue;
            }
        }
        if let Some(t) = png_from_texture(&object, files, None) {
            out.push(t);
        }
    }
    out
}

/// Rank a catalog texture name against a list key (`eri`, `cw_t_mayuge_011_vl`).
/// Exact / `_low` beat substring hits; `kage` / thumb / mole copies lose.
pub(crate) fn tex_name_rank(have: &str, want: &str) -> i32 {
    let h = have.to_ascii_lowercase();
    let w = want.to_ascii_lowercase();
    if w.is_empty() || h.contains("thumb") {
        return 0;
    }
    let kage = (h.contains("kage") && !h.contains("eyeshadow")) || h.starts_with("mole_");
    if h == w {
        return 1000;
    }
    if h == format!("{w}_low") {
        return 900;
    }
    if !kage && (h.starts_with(&w) || w.starts_with(&h)) {
        return 400;
    }
    if !kage && h.contains(&w) {
        return 100;
    }
    if kage && (h == format!("{w}_kage") || h == format!("{w}_kage_low") || h.contains(&w)) {
        return 10;
    }
    0
}

pub fn pick_named_png(textures: &[(String, Vec<u8>)], names: &[&str]) -> Option<(String, Vec<u8>)> {
    let mut best: Option<(i32, usize, String, Vec<u8>)> = None;
    for want in names {
        for (n, b) in textures {
            let r = tex_name_rank(n, want);
            if r <= 0 {
                continue;
            }
            let key = (r, b.len(), n.clone(), b.clone());
            let better = match &best {
                None => true,
                Some(cur) => key.0 > cur.0 || (key.0 == cur.0 && key.1 > cur.1),
            };
            if better {
                best = Some(key);
            }
        }
        if let Some((_, _, n, b)) = best {
            return Some((n, b));
        }
    }
    None
}

pub fn named_png_from_bytes(bundle_bytes: impl Into<SourceBytes>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let bundle_bytes = bundle_bytes.into();
    let parsed = parse_bundle_ref(&bundle_bytes);
    named_png_cached_fallback(&parsed,names,||Some(bundle_bytes.to_vec()))
}
pub(crate) fn named_png_cached_fallback(parsed:&ParsedBundle,names:&[&str],source:impl FnOnce()->Option<Vec<u8>>)->Option<(String,Vec<u8>)>{
    let key=names.iter().map(|s|s.to_ascii_lowercase()).collect::<Vec<_>>();
    if parsed.missing_texture_queries.borrow().contains(&key){return None;}
    let result=named_png_from_parsed(parsed,names);
    // A complete metadata directory proves an absent name. Re-reading and
    // eagerly decompressing the same bundle cannot discover another object.
    // Decode failures and incomplete/legacy directories keep the old fallback.
    let dir=parsed.texture_directory();
    let absent=dir.complete&&!dir.catalog.iter().any(|(have,_)|names.iter().any(|want|tex_name_rank(have,want)>0));
    let result=result.or_else(||if absent{None}else{named_png_from_bytes_legacy(source()?,names)});
    if result.is_none(){let mut missing=parsed.missing_texture_queries.borrow_mut();if missing.len()>=128{missing.pop_front();}missing.push_back(key);}
    result
}
pub(crate) fn named_png_from_parsed(parsed:&ParsedBundle,names:&[&str])->Option<(String,Vec<u8>)> {
    named_png_in_directory(&parsed.texture_directory(),names,&|n|parsed.texture_png_named(n))
}
/// AssetBundle name lookup over a texture directory: an exact unique match,
/// else the best ranked catalog name (rank, then pixel area) that decodes.
pub(crate) fn named_png_in_directory(dir:&TexDirectory,names:&[&str],decode:&dyn Fn(&str)->Option<Vec<u8>>)->Option<(String,Vec<u8>)> {
    for want in names {
        let key=want.to_ascii_lowercase();
        if dir.exact.contains_key(&key) {
            let exact=dir.names.get(&key).filter(|v|!v.1&&tex_name_rank(&v.0,want)==1000).map(|v|v.0.clone())
                .or_else(||dir.assets.get(&key).filter(|n|tex_name_rank(n,want)==1000).cloned());
            if let Some(exact)=exact {if let Some(png)=decode(&exact){return Some((exact,png));}}
        }
        let mut ranked:Vec<(i32,u32,String)>=dir.catalog.iter().filter_map(|(n,px)|{let r=tex_name_rank(n,want);(r>0).then_some((r,*px,n.clone()))}).collect();
        ranked.sort_by(|a,b|b.0.cmp(&a.0).then(b.1.cmp(&a.1)));
        for (_,_,n) in ranked {if let Some(png)=decode(&n){return Some((n,png));}}
    }
    None
}

/// AssetBundle.LoadAsset<Texture2D> uses an exact asset name. Catalog fallback
/// candidates must not be replaced by prefix/substring matches.
pub fn exact_png_from_bytes(bundle_bytes: Vec<u8>, name: &str) -> Option<Vec<u8>> {
    exact_png_from_ref(&bundle_bytes,name)
}

/// Candidate-name probes must not clone the entire (often >100 MiB)
/// source bundle. ParsedBundle owns its data on a cache miss.
pub(crate) fn exact_png_from_ref(bundle_bytes: &[u8], name: &str) -> Option<Vec<u8>> {
    let parsed = parse_bundle_ref(bundle_bytes);
    parsed.exact_texture_png(name)
}

/// Unity maker thumbnails are usually Sprite rectangles inside Texture2D atlases.
pub fn sprite_png_from_bytes(bytes: Vec<u8>, name: &str) -> Option<Vec<u8>> {
 crate::oklib::memo_bytes(&bytes.clone(), "sprite_png", name, || sprite_png_from_bytes_unity(bytes, name))
}
pub(crate) fn sprite_png_from_bytes_unity(bytes: Vec<u8>, name: &str) -> Option<Vec<u8>> {
 let parsed=parse_bundle_cached(bytes);
 parsed.with_serialized(|files| {for file in files {for info in file.objects_of_type(213) {
  let Ok(sprite)=UnityObject::from_serialized_file(file,info) else {continue;};
  if !sprite.name().unwrap_or_default().eq_ignore_ascii_case(name){continue;}
  let props=sprite.class.properties();let rd=props.get("m_RD")?;
  let texture=uv_obj(rd,"texture").and_then(uv_pptr)?;
  let rect=uv_obj(rd,"textureRect")?;
  let x=uv_obj(rect,"x").map(uv_f32)? as u32;let y=uv_obj(rect,"y").map(uv_f32)? as u32;
  let w=uv_obj(rect,"width").map(uv_f32)? as u32;let h=uv_obj(rect,"height").map(uv_f32)? as u32;
  let texture_info=file.objects_of_type(28).into_iter().find(|i|i.path_id==texture)?;
  let obj=UnityObject::from_serialized_file(file,texture_info).ok()?;
  let (_,png)=png_from_texture(&obj,&parsed.files,None)?;
  let image=crate::teximg::load_from_memory(&png).ok()?.to_rgba8();
  if w==0||h==0||x+w>image.width()||y+h>image.height(){return None;}
  let mut cropped=image::imageops::crop_imm(&image,x,y,w,h).to_image();
  let settings=uv_obj(rd,"settingsRaw").map(uv_i32).unwrap_or(0);
  // SpritePackingRotation occupies bits 2..5 of SpriteSettings.
  cropped=match (settings>>2)&15 {1=>image::imageops::flip_horizontal(&cropped),2=>image::imageops::flip_vertical(&cropped),3=>image::imageops::rotate180(&cropped),4=>image::imageops::rotate270(&cropped),_=>cropped};
  cropped=image::imageops::flip_vertical(&cropped);
  let mut out=std::io::Cursor::new(Vec::new());image::DynamicImage::ImageRgba8(cropped).write_to(&mut out,image::ImageFormat::Png).ok()?;return Some(out.into_inner());
 }}None})
}

pub(crate) fn named_png_from_bytes_legacy(bundle_bytes: Vec<u8>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    named_png_from_bytes_legacy_unity(bundle_bytes,names)
}
/// Every Texture2D of a bundle in object order with its texels as the eager
/// (legacy) loader reads them: the data the legacy name lookup ranks over.
pub(crate) fn legacy_texture_payloads(bytes: &[u8]) -> Vec<(String, Option<unity_asset_decode::texture::Texture2D>)> {
    crate::oklib::memo_bytes(bytes, "legacy_texture_payloads", &(), || {
        let mut out = Vec::new();
        each_legacy_texture(bytes, |name, payload| out.push((name, payload())));
        out
    })
}
/// Each (name, texels) of a stored `legacy_texture_payloads` result in order, deserialized one at a time.
pub(crate) fn each_stored_texture_payload(raw: &[u8], mut each: impl FnMut(String, Option<unity_asset_decode::texture::Texture2D>)) -> Option<()> {
    use serde::de::{Deserializer, SeqAccess, Visitor};
    struct List<'f, F>(&'f mut F);
    impl<'de, F: FnMut(String, Option<unity_asset_decode::texture::Texture2D>)> Visitor<'de> for List<'_, F> {
        type Value = ();
        fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result { f.write_str("a texture payload list") }
        fn visit_seq<S: SeqAccess<'de>>(self, mut seq: S) -> Result<(), S::Error> {
            // (texels read as one byte string: element by element was most of an accessory's colouring)
            while let Some((name, payload)) = seq.next_element::<(String, Option<crate::unity::TexturePayload>)>()? { (self.0)(name, payload.map(|p| p.0)); }
            Ok(())
        }
    }
    use bincode::Options;
    // The options of `bincode::deserialize`, which stored the list.
    let mut de = bincode::Deserializer::from_slice(raw, bincode::DefaultOptions::new().with_fixint_encoding().allow_trailing_bytes());
    with_texture_plans(|| de.deserialize_seq(List(&mut each))).ok()
}
/// The eager loader's Texture2D objects of a bundle in order, each with its texels read when asked for. The
/// bundle's files are read through the loaded bundle on demand, not copied out whole.
pub(crate) fn each_legacy_texture(bytes: &[u8], mut each: impl FnMut(String, &dyn Fn() -> Option<unity_asset_decode::texture::Texture2D>)) {
    // UnityFS: loaded lazily; `check_blocks` fails where decompressing the whole bundle at once would, without
    // keeping the decompressed data (hundreds of MB). Its files are the directory nodes, as the eager loader's.
    let source = bytes.to_source();
    let Ok(lazy) = load_bundle_source(source.clone(), false) else { return };
    let (bundle, files) = if lazy.header.is_unity_fs() {
        if lazy.check_blocks().is_err() { return; }
        let files: Vec<unity_asset_binary::bundle::BundleFileInfo> = lazy.nodes.iter()
            .map(|n| unity_asset_binary::bundle::BundleFileInfo::new(n.name.clone(), n.offset, n.size)).collect();
        (Arc::new(lazy), files)
    } else {
        drop(lazy);
        let Ok(bundle) = load_bundle_source(source, true) else { return };
        let _ = bundle.data_checked();
        let files = bundle.files.clone();
        (Arc::new(bundle), files)
    };
    // A file that cannot be read answers no stream read, as when it was left out of the eagerly extracted list.
    let files: Vec<(String, BundleFileBytes)> = files.into_iter()
        .map(|f| (f.name.clone(), BundleFileBytes { bundle: bundle.clone(), file: f })).collect();
    for (_n, data) in &files {
        let Ok(serialized) = parse_shared_serialized(data.clone()) else { continue };
        for info in serialized.objects_of_type(28) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let name = object.name().unwrap_or_default();
            each(name, &|| read_texture_payload(&object, &files, Some(bytes)));
        }
    }
}
#[allow(dead_code)]
pub(crate) fn named_png_from_bytes_legacy_unity(bundle_bytes: Vec<u8>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let wants: Vec<String> = names.iter().map(|s| s.to_ascii_lowercase()).collect();
    let mut best: Option<(i32, String, Vec<u8>)> = None;
    let mut consider = |name: String, payload: &dyn Fn() -> Option<unity_asset_decode::texture::Texture2D>| {
        let score = wants.iter().map(|w| tex_name_rank(&name.to_ascii_lowercase(), w)).max().unwrap_or(0);
        if score == 0 || !best.as_ref().map(|b| score > b.0).unwrap_or(true) { return; }
        if let Some((n, png)) = payload().and_then(|p| png_from_payload(p, Some(name.clone()))) { best = Some((score, n, png)); }
    };
    // Recording an OK base library keeps every payload (the memo); otherwise only candidates are decoded.
    if crate::oklib::active().is_some() {
        // Replaying: the stored list is read one texture at a time (a large pack's list was hundreds of MB once
        // decompressed, and as much again deserialized), with the same ranking in the same order.
        let streamed = crate::oklib::memo_bytes_visit(&bundle_bytes, "legacy_texture_payloads", &(),
            |raw| each_stored_texture_payload(raw, |name, payload| consider(name, &|| payload.clone())));
        if streamed.is_none() {
            for (name, payload) in legacy_texture_payloads(&bundle_bytes) { consider(name, &|| payload.clone()); }
        }
    } else {
        each_legacy_texture(&bundle_bytes, consider);
    }
    best.map(|(_, n, b)| (n, b))
}
#[allow(dead_code)]
pub(crate) fn named_png_from_bytes_legacy_eager(bundle_bytes: Vec<u8>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let raw = bundle_bytes.clone();
    let Ok(bundle) = load_bundle_unbounded(bundle_bytes) else {
        return None;
    };
    let _ = bundle.data_checked();
    let mut files = Vec::new();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        files.push((file.name.clone(), bytes));
    }
    let wants: Vec<String> = names.iter().map(|s| s.to_ascii_lowercase()).collect();
    let mut best: Option<(i32, String, Vec<u8>)> = None;
    for (_n, bytes) in &files {
        let Ok(serialized) = parse_serialized_file(bytes.clone()) else {
            continue;
        };
        for info in serialized.objects_of_type(28) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, info) else {
                continue;
            };
            let nm = object.name().unwrap_or_default();
            let ln = nm.to_ascii_lowercase();
            let base_score = wants.iter().map(|w| tex_name_rank(&ln, w)).max().unwrap_or(0);
            if base_score == 0 {
                continue;
            }
            let score = base_score;
            if best.as_ref().map(|b| score > b.0).unwrap_or(true) {
                if let Some(t) = png_from_texture(&object, &files, Some(&raw)) {
                    best = Some((score, t.0, t.1));
                }
            }
        }
    }
    best.map(|(_, n, b)| (n, b))
}

pub fn named_png_from_bundle(path: &Path, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let parsed=parse_bundle_path_cached(path)?;
    named_png_cached_fallback(&parsed,names,||crate::gamefs::read(path).ok())
}

pub(crate) fn named_png_from_zip(path:&Path,asset:&str,names:&[&str])->Option<(String,Vec<u8>)>{
    zip_absent_memo(path,asset,names,||{
        let parsed=parse_bundle_zip_cached(path,asset)?;
        named_png_cached_fallback(&parsed,names,||crate::mods::zipmod_bundle_named(path,asset).map(|b|b.to_vec()))
    })
}

/// A name the zip's bundle does not serve, remembered for the mounted pack: a name that ranks against the catalog but
/// decodes to nothing falls back to re-reading the whole bundle (`missing_texture_queries` only lasts as long as one
/// ParsedBundle), ~1.3 s for a hair ribbon's ColorMask in every outfit. Only the absence is kept, never pixels.
fn zip_absent_memo<T>(path:&Path,asset:&str,names:&[&str],lookup:impl Fn()->Option<T>)->Option<T>{
    let mut found=None;
    let present=crate::pack_memo(format!("zip_texture_present|{}|{asset}|{names:?}",path.display()),||{found=Some(lookup());found.as_ref().is_some_and(Option::is_some)});
    if !present {return None;}
    found.unwrap_or_else(&lookup)
}

/// Preserve the serialized Texture2D import colour space when a face texture
/// is baked through Unity's linear-source -> sRGB RenderTexture Blit.
pub fn named_png_with_color_space_from_bytes(
    bundle_bytes: Vec<u8>, names: &[&str],
) -> Option<(String, Vec<u8>, Option<i32>)> {
    let parsed=parse_bundle_ref(&bundle_bytes);
    let (name, png) = named_png_cached_fallback(&parsed,names,||Some(bundle_bytes))?;
    let color_space = parsed.texture_color_space(&name);
    Some((name, png, color_space))
}

pub fn named_png_with_color_space_from_bundle(
    path: &Path, names: &[&str],
) -> Option<(String, Vec<u8>, Option<i32>)> {
    let parsed=parse_bundle_path_cached(path)?;
    let(name,png)=named_png_cached_fallback(&parsed,names,||crate::gamefs::read(path).ok())?;
    let color_space=parsed.texture_color_space(&name);Some((name,png,color_space))
}

pub(crate) fn named_png_with_color_space_from_zip(path:&Path,asset:&str,names:&[&str])
    ->Option<(String,Vec<u8>,Option<i32>)> {
    zip_absent_memo(path,asset,names,||{
        let parsed=parse_bundle_zip_cached(path,asset)?;
        let(name,png)=named_png_cached_fallback(&parsed,names,||crate::mods::zipmod_bundle_named(path,asset).map(|b|b.to_vec()))?;
        let color_space=parsed.texture_color_space(&name);Some((name,png,color_space))
    })
}

pub(crate) fn score_texture(mesh_name: &str, hint: &str, tex_name: &str, bytes_len: usize) -> i64 {
    let n = mesh_name.to_ascii_lowercase();
    let h = hint.to_ascii_lowercase();
    let t = tex_name.to_ascii_lowercase();
    let mut s = 0i64;
    if !h.is_empty() && (t == h || t.contains(&h) || h.contains(&t)) {
        s += 80_000_000;
    }
    if t.contains(&n) || n.contains(&t) {
        s += 50_000_000;
    }
    if n.contains("body") && (t.contains("body") || t.contains("skin") || t.contains("cf_m_body")) {
        s += 40_000_000;
    }
    if (n.contains("face") || n.contains("head"))
        && (t.contains("face") || t.contains("head") || t.contains("cf_m_face") || t.contains("cf_o_face"))
    {
        s += 40_000_000;
    }
    if n.contains("hair") && t.contains("hair") {
        s += 40_000_000;
    }
    if t.ends_with("_t") || t.contains("_t_") || t.contains("main") {
        s += 2_000_000;
    }
    if is_aux_tex(tex_name) {
        s -= 40_000_000;
    }
    s + bytes_len.min(5_000_000) as i64
}

/// ColorMask / DetailMask / normal / thumbs are not albedo.
pub(crate) fn is_aux_tex(name: &str) -> bool {
    let t = name.to_ascii_lowercase();
    t.contains("thumb")
        || t.contains("colormask")
        || t.contains("_mc")
        || t.contains("_md")
        || t.ends_with("_n")
        || t.contains("_n_")
        || t.contains("normal")
}

pub(crate) fn pick_texture_for_mesh<'a>(
    name: &str,
    hint: &str,
    textures: &'a [(String, Vec<u8>)],
) -> Option<&'a (String, Vec<u8>)> {
    if textures.is_empty() {
        return None;
    }
    if textures.len() == 1 {
        return textures.first();
    }
    let (best, score) = textures
        .iter()
        .map(|t| (t, score_texture(name, hint, &t.0, t.1.len())))
        .max_by_key(|(_, s)| *s)?;
    // Require a name match. Byte-size-only scores used to glue a random 2MB atlas
    // onto every mesh (pupils, teeth, tongue).
    let n = name.to_ascii_lowercase();
    let h = hint.to_ascii_lowercase();
    if score >= 10_000_000 {
        Some(best)
    } else if n.contains("hair")
        || n.contains("kami")
        || h.contains("hair")
        || h.contains("kami")
    {
        textures
            .iter()
            .filter(|t| t.1.len() > 2048 && !t.0.to_ascii_lowercase().contains("thumb"))
            .max_by_key(|t| t.1.len())
            .or(Some(best))
    } else if textures.len() == 1 {
        Some(best)
    } else {
        None
    }
}

pub(crate) fn attach_textures(meshes: &mut [ExtractedMesh], textures: &[(String, Vec<u8>)], hint: &str) {
    if textures.is_empty() {
        return;
    }
    for m in meshes {
        if m.texture_png.is_some() {
            continue;
        }
        if let Some((tn, bytes)) = pick_texture_for_mesh(&m.name, hint, textures) {
            m.texture_name = tn.clone();
            m.texture_png = Some(bytes.clone());
        }
    }
}

pub(crate) fn pptr_path_id(v: &unity_asset_core::UnityValue) -> Option<i64> {
    match v {
        unity_asset_core::UnityValue::Object(o) => o
            .get("m_PathID")
            .or_else(|| o.get("m_PathId"))
            .and_then(|x| x.as_i64()),
        unity_asset_core::UnityValue::Integer(i) => Some(*i),
        _ => None,
    }
}

/// The materials of one serialized file (path id -> material), with the texture names and wrap modes it adds to
/// `tex_id_name`/`tex_id_wrap` (shaders resolve inside the file).
/// Where material textures outside the bundle's own files live: `own` is the external path of the file being read
/// (None for the bundle itself), `external_name` names a texture of another file, `source` collects texture name
/// (lower case) -> external path for every texture a material takes from outside the bundle.
pub(crate) struct TextureSources<'a> {
    pub own: Option<&'a str>,
    pub external_name: &'a mut dyn FnMut(&str, i64) -> Option<String>,
    pub source: &'a mut HashMap<String, String>,
}

pub(crate) fn materials_in(ser: &unity_asset_binary::asset::SerializedFile, tex_id_name: &mut HashMap<i64, String>, tex_id_wrap: &mut HashMap<i64, i32>, sources: &mut TextureSources) -> HashMap<i64, MeshMaterial> {
    let mut shader_id_name: HashMap<i64, String> = HashMap::new();
    let mut shader_id_rim: HashMap<i64, bool> = HashMap::new();
    let mut mat_info: HashMap<i64, MeshMaterial> = HashMap::new();
    shader_id_name.clear();
    shader_id_rim.clear();
    // Cubemaps (Skybox/Cubemap `_Tex`) are named too; only their own readers decode them.
    for info in ser.objects_of_type(89) {
        if let Some(n) = UnityObject::from_serialized_file(ser, info).ok().and_then(|o| o.name()).filter(|n| !n.is_empty()) { tex_id_name.insert(info.path_id, n); }
    }
    for info in ser.objects_of_type(28) {
        if let Ok(object) = UnityObject::from_serialized_file(ser, info) {
            let n = object.name().unwrap_or_default();
            if !n.is_empty() {
                tex_id_name.insert(object.path_id(), n);
            }
            if let Some(settings) = object.class.properties().get("m_TextureSettings") {
                let wrap = uv_obj(settings,"m_WrapU").or_else(||uv_obj(settings,"m_WrapMode"))
                    .and_then(|v|v.as_i64()).unwrap_or(0) as i32;
                tex_id_wrap.insert(object.path_id(),wrap);
            }
        }
    }
    for info in ser.objects_of_type(48) {
        if let Ok(object) = UnityObject::from_serialized_file(ser, info) {
            let n = shader_runtime_name(&object);
            if !n.is_empty() {
                shader_id_name.insert(object.path_id(), n);
            }
            if let Some(has_rim) = shader_declares_property(&object, "_rimV") {
                shader_id_rim.insert(object.path_id(), has_rim);
            }
        }
    }
    for info in ser.objects_of_type(21) {
        let Ok(object) = UnityObject::from_serialized_file(ser, info) else {
            continue;
        };
        let props = object.class.properties();
        let mut m = MeshMaterial {
            name: object.name().unwrap_or_default(),
            shader: props
                .get("m_Shader")
                .and_then(local_pptr_path_id)
                .and_then(|id| shader_id_name.get(&id).cloned())
                .unwrap_or_default(),
            shader_has_rim: props.get("m_Shader").and_then(local_pptr_path_id)
                .and_then(|id| shader_id_rim.get(&id).copied()),
            render_queue: props
                .get("m_CustomRenderQueue")
                .and_then(|v| v.as_i64())
                .unwrap_or(-1) as i32,
            keywords: props
                .get("m_ShaderKeywords")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .split_whitespace()
                .map(|s| s.to_string())
                .collect(),
            colors: HashMap::new(),
            floats: HashMap::new(),
            tex_slots: Vec::new(),
            tex_wraps: HashMap::new(),
        };
        if let Some(unity_asset_core::UnityValue::Object(sp)) =
            props.get("m_SavedProperties")
        {
            if let Some(unity_asset_core::UnityValue::Array(envs)) = sp.get("m_TexEnvs")
            {
                for e in envs {
                    let (key, val) = match e {
                        unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                            let k = pair[0].as_str().unwrap_or("").to_string();
                            (k, pair[1].clone())
                        }
                        unity_asset_core::UnityValue::Object(eo) => (
                            eo.get("first")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string(),
                            eo.get("second")
                                .cloned()
                                .unwrap_or(unity_asset_core::UnityValue::Null),
                        ),
                        _ => continue,
                    };
                    store_tex_env_transform(&mut m, &key, &val);
                    let tex_ref = val
                        .as_object()
                        .and_then(|so| so.get("m_Texture"))
                        .and_then(references::PPtr::read);
                    // A texture of another serialized file (FileID > 0): named and decoded from that file.
                    if let Some(p) = tex_ref.filter(|p| p.file > 0) {
                        let Some(ext) = ser.externals.get(p.file as usize - 1).map(|e| e.path.clone()) else { continue; };
                        let Some(name) = (sources.external_name)(&ext, p.path) else { continue; };
                        sources.source.insert(name.to_ascii_lowercase(), ext);
                        m.tex_slots.push((key, name));
                        continue;
                    }
                    let tex_id = tex_ref.map(|p| p.path);
                    if let Some(wrap) = tex_id.and_then(|id|tex_id_wrap.get(&id)) {
                        m.tex_wraps.insert(key.clone(),*wrap);
                    }
                    let Some(name) = tex_id.and_then(|id| tex_id_name.get(&id).cloned())
                    else {
                        continue;
                    };
                    if let Some(own) = sources.own { sources.source.insert(name.to_ascii_lowercase(), own.to_owned()); }
                    m.tex_slots.push((key, name));
                }
            }
            if let Some(unity_asset_core::UnityValue::Array(cols)) = sp.get("m_Colors") {
                for e in cols {
                    let (key, val) = match e {
                        unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                            let k = pair[0].as_str().unwrap_or("").to_string();
                            (k, pair[1].clone())
                        }
                        unity_asset_core::UnityValue::Object(eo) => (
                            eo.get("first")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string(),
                            eo.get("second")
                                .cloned()
                                .unwrap_or(unity_asset_core::UnityValue::Null),
                        ),
                        _ => continue,
                    };
                    if let Some(c) = val.as_object() {
                        let col = [
                            c.get("r").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32,
                            c.get("g").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32,
                            c.get("b").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32,
                            c.get("a").and_then(|v| v.as_f64()).unwrap_or(1.0) as f32,
                        ];
                        m.colors.insert(key, col);
                    }
                }
            }
            if let Some(unity_asset_core::UnityValue::Array(fs)) = sp.get("m_Floats") {
                for e in fs {
                    let (key, val) = match e {
                        unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                            let k = pair[0].as_str().unwrap_or("").to_string();
                            (k, pair[1].clone())
                        }
                        unity_asset_core::UnityValue::Object(eo) => (
                            eo.get("first")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string(),
                            eo.get("second")
                                .cloned()
                                .unwrap_or(unity_asset_core::UnityValue::Null),
                        ),
                        _ => continue,
                    };
                    let f = val.as_f64().or_else(|| {
                        val.as_object()
                            .and_then(|o| o.get("x"))
                            .and_then(|v| v.as_f64())
                    });
                    if let Some(f) = f {
                        m.floats.insert(key, f as f32);
                    }
                }
            }
        }
        mat_info.insert(object.path_id(), m);
    }
    mat_info
}

pub(crate) fn local_pptr_path_id(v: &unity_asset_core::UnityValue) -> Option<i64> {
    if uv_obj(v, "m_FileID").and_then(|id| id.as_i64()).unwrap_or(0) != 0 {
        return None;
    }
    pptr_path_id(v)
}

pub(crate) fn version() -> UnityVersion {
    UnityVersion::parse_version("2019.4.9f1").unwrap_or_default()
}

pub(crate) fn mesh_stream_bytes<B: StreamBytes>(
    object: &UnityObject,
    files: &[(String, B)],
) -> Option<Vec<u8>> {
    use unity_asset_core::UnityValue;
    let props = object.class.properties();
    let UnityValue::Object(stream) = props.get("m_StreamData")? else {
        return None;
    };
    let path = stream
        .get("path")
        .and_then(|v| v.as_str())
        .unwrap_or_default();
    let offset = stream.get("offset").and_then(|v| v.as_i64()).unwrap_or(0) as u64;
    let size = stream.get("size").and_then(|v| v.as_i64()).unwrap_or(0) as u32;
    load_stream(files, path, offset, size)
}

pub fn unpack_illusion_albedo(png: &[u8]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let mut rsum = 0u64;
    let mut gsum = 0u64;
    let mut bsum = 0u64;
    let mut n = 0u64;
    for p in img.pixels() {
        if p[0] < 16 && p[1] < 16 && p[2] < 16 {
            continue;
        }
        rsum += p[0] as u64;
        gsum += p[1] as u64;
        bsum += p[2] as u64;
        n += 1;
    }
    if n == 0 {
        return None;
    }
    let r = rsum / n;
    let g = gsum / n;
    let b = bsum / n;
    // Packed Illusion albedo: luma in R, G/B near 0. Mean R can be dark.
    if r < g.saturating_mul(2) || r < b.saturating_mul(2) || g > 40 && b > 40 {
        return None;
    }
    let mut minv = 255u8;
    let mut maxv = 0u8;
    for p in img.pixels() {
        minv = minv.min(p[0]);
        maxv = maxv.max(p[0]);
    }
    if maxv.saturating_sub(minv) < 14 {
        return None;
    }
    let scale = (200.0 / (r as f32).max(1.0)).clamp(1.0, 16.0);
    let mut out = img;
    for p in out.pixels_mut() {
        let lift = |x: u8| -> u8 { ((x as f32) * scale).clamp(0.0, 255.0) as u8 };
        if p[1] > p[0] && p[1] > p[2] {
            let v = lift(p[1]);
            p[0] = v;
            p[1] = v;
            p[2] = v / 2;
        } else if p[2] > p[0] && p[2] > p[1] {
            let v = lift(p[2]);
            p[0] = v;
            p[1] = v / 2;
            p[2] = v;
        } else {
            let v = lift(p[0]);
            p[0] = v;
            p[1] = ((v as u16 * 210) / 255) as u8;
            p[2] = ((v as u16 * 200) / 255) as u8;
        }
    }
    crate::teximg::encode_rgba_owned(out)
}

pub fn solid_png(color: [f32; 4], w: u32, h: u32) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let px = image::Rgba([
        (color[0].clamp(0.0, 1.0) * 255.0) as u8,
        (color[1].clamp(0.0, 1.0) * 255.0) as u8,
        (color[2].clamp(0.0, 1.0) * 255.0) as u8,
        (color[3].clamp(0.0, 1.0) * 255.0) as u8,
    ]);
    let img = image::RgbaImage::from_pixel(w.max(1), h.max(1), px);
    crate::teximg::encode_rgba_owned(img)
}
