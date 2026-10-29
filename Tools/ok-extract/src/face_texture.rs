//! Face textures of the open: the face list entry, face/detail/makeup PNGs, colour spaces and the face composite.
use super::*;

/// `ChaFileFace.headId` is a local slot after Sideloader. Vanilla `bo_head`
/// only has ids 0/200/201, so `get(100, headId)` misses mod heads (this card:
/// headId=1, KKEx guid `HeadMod`). Without a list row, CreateFaceTexture never
/// runs and the face keeps the bundle's empty render-texture (black).
pub(crate) fn resolve_face_entry(
    vanilla: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String, PathBuf>,
    zip_lists: &mut std::collections::HashMap<String, ChaListCatalog>,
) -> Option<ListEntry> {
    if let Some(r) = card.resolves.iter().find(|r| {
        r.category_no == 100 && r.property.to_ascii_lowercase().contains("headid")
    }) {
        let zip = guid_path(guid_index, &r.mod_id);
        return lookup_entry(
            vanilla,
            zip_lists,
            zip.map(|p| p.as_path()),
            &r.mod_id,
            100,
            r.slot,
            r.local_slot,
        );
    }
    vanilla
        .get(100, card.head_id)
        .or_else(|| vanilla.get(100, 0))
        .cloned()
}

pub(crate) fn face_named_png(
    game_root: &Path,
    entry: Option<&ListEntry>,
    tex_name: &str,
    ab_key: &str,
    head_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    let name = if tex_name.is_empty() || tex_name == "0" {
        "cf_face_00_t"
    } else {
        tex_name
    };
    let hi = character_hi_poly();
    let names: Vec<String> = if !hi {
        vec![format!("{name}_low"), name.to_string()]
    } else {
        vec![name.to_string(), format!("{name}_low")]
    };
    let refs: Vec<&str> = names.iter().map(|s| s.as_str()).collect();
    if let Some(e) = entry {
        let ab = {
            let v = e.info(ab_key);
            if v.is_empty() || v == "0" {
                e.main_ab.clone()
            } else {
                v
            }
        };
        if let Some(zip) = e.extra.get("__zip") {
            if let Some(bytes) = crate::mods::zipmod_bundle_named(std::path::Path::new(zip), &ab) {
                if let Some((_, b)) = crate::unity::named_png_from_bytes(bytes, &refs) {
                    return Some(b);
                }
            }
        }
        if let Some(bytes) = crate::mods::override_bundle(&game_root.join("mods"), &ab) {
            if let Some((_, b)) = crate::unity::named_png_from_bytes(bytes, &refs) {
                return Some(b);
            }
        }
        if let Some(b) = png_from_ab(game_root, &ab, &refs, head_texs) {
            return Some(b);
        }
    }
    pick_named_png(head_texs, &refs).map(|(_, b)| b)
}

pub(crate) fn face_base_color_space(
    game_root: &Path,
    entry: Option<&ListEntry>,
    tex_name: &str,
    decoded_png: &[u8],
) -> Option<i32> {
    let started=Instant::now();
    let names = if character_hi_poly() {
        vec![tex_name.to_string(), format!("{tex_name}_low")]
    } else {
        vec![format!("{tex_name}_low"), tex_name.to_string()]
    };
    let refs: Vec<&str> = names.iter().map(String::as_str).collect();
    let check = |found:Option<(String,Vec<u8>,Option<i32>)>| -> Option<i32> {
        if std::env::var_os("OK_APPEARANCE_PROFILE").is_some(){eprintln!("APPEARANCE_PROFILE phase=face_color_probe texture={tex_name} cumulative_ms={:.3}",started.elapsed().as_secs_f64()*1000.);}
        let (_, png, color_space) = found?;
        if png == decoded_png { color_space } else { None }
    };
    if let Some(e) = entry {
        let source_ab = {
            let explicit = e.info("MainTexAB");
            if explicit.is_empty() || explicit == "0" { e.main_ab.clone() } else { explicit }
        };
        if let Some(zip) = e.extra.get("__zip") {
            if let Some(cs)=check(crate::unity::named_png_with_color_space_from_zip(Path::new(zip),&source_ab,&refs)){return Some(cs);}
        }
        if let Some(zip) = crate::mods::override_bundle_source(&game_root.join("mods"), &source_ab) {
            if let Some(cs)=check(crate::unity::named_png_with_color_space_from_zip(&zip,&source_ab,&refs)){return Some(cs);}
        }
        if let Some(cs)=check(crate::unity::named_png_with_color_space_from_bundle(&game_root.join("abdata").join(&source_ab),&refs)){return Some(cs);}
    }
    // Face Fixes and head mods commonly reuse the vanilla create texture.
    // Accept its import flag only when its decoded pixels equal the selected
    // source, so a mod-specific face atlas cannot inherit vanilla metadata.
    let vanilla = game_root.join("abdata/chara/bo_head_00.unity3d");
    check(crate::unity::named_png_with_color_space_from_bundle(&vanilla,&refs))
}

#[test]
#[ignore="installed face/iris color-space source and mismatch oracle"]
pub(crate) fn face_color_space_cached_lookup_matches_legacy_on_installed_sources(){
    let game=&crate::test_support::game_path("");
    let catalog=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mut sources=vec![(None,"cf_face_00_t".to_string(),game.join("abdata/chara/bo_head_00.unity3d"))];
    for (cat,key) in [(408,"EyeTex"),(407,"EyeWhiteTex"),(406,"EyebrowTex")]{
        let entry=catalog.get(cat,0).expect("installed vanilla source").clone();
        let name=entry.info(key);assert!(!name.is_empty()&&name!="0","real source required {cat}");
        let path=game.join("abdata").join(&entry.main_ab);sources.push((Some(entry),name,path));
    }
    for (entry,name,path) in sources{
        let (_,png,_)=crate::unity::named_png_with_color_space_from_bundle(&path,&[&name]).expect("real source PNG and import metadata");
        let flipped=flip_png_v(&png).unwrap();let mut altered=png.clone();altered.push(0);
        for (kind,input) in [("source",png),("flipped",flipped),("mismatch",altered)]{
            let started=Instant::now();let expected=face_base_color_space_reference(game,entry.as_ref(),&name,&input);let old_ms=started.elapsed().as_secs_f64()*1000.;
            let started=Instant::now();let actual=face_base_color_space(game,entry.as_ref(),&name,&input);let new_ms=started.elapsed().as_secs_f64()*1000.;
            assert_eq!(actual,expected,"import metadata changed {name}/{kind}");
            assert_eq!(face_base_color_space(game,entry.as_ref(),&name,&input),expected,"warm metadata changed {name}/{kind}");
            eprintln!("COLOR_SPACE_ORACLE texture={name} variant={kind} old_ms={old_ms:.3} new_ms={new_ms:.3}");
        }
    }
}

#[cfg(test)]
pub(crate) fn face_base_color_space_reference(
    game_root: &Path,
    entry: Option<&ListEntry>,
    tex_name: &str,
    decoded_png: &[u8],
) -> Option<i32> {
    let names = if character_hi_poly() {
        vec![tex_name.to_string(), format!("{tex_name}_low")]
    } else {
        vec![format!("{tex_name}_low"), tex_name.to_string()]
    };
    let refs: Vec<&str> = names.iter().map(String::as_str).collect();
    let check = |bundle: Vec<u8>| -> Option<i32> {
        let (_, png, color_space) =
            crate::unity::named_png_with_color_space_from_bytes(bundle, &refs)?;
        if png == decoded_png { color_space } else { None }
    };
    if let Some(e) = entry {
        let source_ab = {
            let explicit = e.info("MainTexAB");
            if explicit.is_empty() || explicit == "0" { e.main_ab.clone() } else { explicit }
        };
        if let Some(zip) = e.extra.get("__zip") {
            if let Some(bytes) = crate::mods::zipmod_bundle_named(Path::new(zip), &source_ab) {
                if let Some(cs) = check(bytes.to_vec()) { return Some(cs); }
            }
        }
        if let Some(bytes) = crate::mods::override_bundle(&game_root.join("mods"), &source_ab) {
            if let Some(cs) = check(bytes) { return Some(cs); }
        }
        if let Ok(bytes) = crate::gamefs::read_source(game_root.join("abdata").join(&source_ab)) {
            if let Some(cs) = check(bytes.to_vec()) { return Some(cs); }
        }
    }
    // Face Fixes and head mods commonly reuse the vanilla create texture.
    // Accept its import flag only when its decoded pixels equal the selected
    // source, so a mod-specific face atlas cannot inherit vanilla metadata.
    let vanilla = game_root.join("abdata/chara/bo_head_00.unity3d");
    crate::gamefs::read(vanilla).ok().and_then(check)
}

/// Reproduce ChaControl.CreateFaceTexture: head MainTex * _Color, ColorMask
/// picking _Color/_Color2 (lips vs skin), then paint/cheek/lipline/mole/lip.
pub(crate) fn compose_face_texture(
    game_root: &Path, vanilla: &ChaListCatalog, mod_catalog: &ChaListCatalog,
    card: &CardInfo, head_texs: &[(String, Vec<u8>)], body_texs: &[(String, Vec<u8>)],
    skin: [f32; 4], skin_sub: [f32; 4], outfit: i32,
    guid_index: &std::collections::HashMap<String, PathBuf>,
) -> Option<Vec<u8>> {let _spot=crate::profile::spot("compose_face_texture");
    if std::env::var("OK_FACE_LEGACY").as_deref() == Ok("1") {
        compose_face_texture_legacy(game_root, vanilla, mod_catalog, card, head_texs,
                                   body_texs, skin, skin_sub, outfit, guid_index)
    } else {
        face_create_source::compose(game_root, vanilla, mod_catalog, card, skin,
                                    skin_sub, outfit, guid_index)
    }
}

pub(crate) fn compose_face_texture_legacy(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
    skin: [f32; 4],
    skin_sub: [f32; 4],
    outfit: i32,
    guid_index: &std::collections::HashMap<String, PathBuf>,
) -> Option<Vec<u8>> {
    let mut zip_lists = std::collections::HashMap::new();
    let entry = resolve_face_entry(vanilla, card, guid_index, &mut zip_lists);
    let base_name = entry
        .as_ref()
        .map(|e| {
            if e.main_tex.is_empty() {
                "cf_face_00_t".to_string()
            } else {
                e.main_tex.clone()
            }
        })
        .unwrap_or_else(|| "cf_face_00_t".to_string());
    let Some(base) = face_named_png(game_root, entry.as_ref(), &base_name, "MainTexAB", head_texs)
    else {
        eprintln!(
            "ok-extract: face tex missing headId={} entry={:?}",
            card.head_id,
            entry.as_ref().map(|e| e.name.as_str())
        );
        return None;
    };
    eprintln!(
        "ok-extract: face tex headId={} entry={} bytes={}",
        card.head_id,
        entry.as_ref().map(|e| e.name.as_str()).unwrap_or("vanilla-fallback"),
        base.len()
    );
    if std::env::var("OK_FACE_RAW").is_ok() {
        let _ = std::fs::write(
            r"C:\Users\Damody\AppData\Local\Temp\opencode\face_raw.png",
            &base,
        );
    }
    // The Unity source Texture2D and its Blit target may have opposite PNG row
    // order. KSOX FaceUnder is painted onto that Blit target *before* the face
    // create shader (Hooks.OverlayBlitImpl), so map its rows to our source
    // atlas before compositing. Decide from the raw mouth patch: overlays can
    // obscure the patch and must not influence the coordinate decision.
    let face_needs_vertical_flip = face_cavity_is_at_bottom(&base);
    // The game's `Graphics.Blit(texMain, temporary)` converts this imported
    // linear Texture2D into the sRGB render target. March's captured texMain
    // matches the exact IEC transfer curve (99.58% of channel bytes), while
    // retaining decoder bytes makes the mouth/chin patch nearly half as bright.
    let color_space = face_base_color_space(game_root, entry.as_ref(), &base_name, &base);
    eprintln!("ok-extract: face source colorSpace={color_space:?}");
    let base = if color_space == Some(0) {
        linear_texture_to_srgb_png(&base).unwrap_or(base)
    } else {
        base
    };
    let base = ksox_overlay_png(card, outfit, 4)
        .and_then(|mut ov| {
            if face_needs_vertical_flip {
                ov = flip_png_vertical(&ov)?;
            }
            composite_overlay(&base, &ov)
        })
        .unwrap_or(base);
    let img = crate::teximg::load_from_memory(&base).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    if std::env::var("OK_OVER_DUMP").is_ok() {
        eprintln!(
            "ok-extract: FACECOMPOSE overlay={} flat={:?} lip_id={}",
            std::env::var("OK_FACE_OVERLAY").is_ok(),
            std::env::var("OK_FACE_FLAT").ok(),
            card.lip_id
        );
    }
    if let Ok(v) = std::env::var("OK_FACE_FLAT") {
        // Diagnostic: replace the composed face texture so the dark chin patch
        // can be attributed to the texture (`1`) or the geometry (`checker`).
        if v == "uv" {
            // Encode the sampled texture coordinate as colour (R=u, G=v_img);
            // lighting scales both equally so R/G recovers the UV at any pixel.
            let mut im = image::RgbaImage::new(w, h);
            for y in 0..h {
                for x in 0..w {
                    im.put_pixel(
                        x,
                        y,
                        image::Rgba([(x * 255 / w.max(1)) as u8, (y * 255 / h.max(1)) as u8, 0, 255]),
                    );
                }
            }
            return encode_png(&im);
        }
        if v == "cavity" {
            // Paint the raw `cf_face_00_t` mouth-cavity rectangle magenta so the
            // dark chin patch can be traced to the surface sampling it.
            let mut im = image::RgbaImage::new(w, h);
            for y in 0..h {
                for x in 0..w {
                    let u = x as f32 / w as f32;
                    let vi = y as f32 / h as f32;
                    let cavity = (0.30..=0.70).contains(&u) && (0.79..=0.99).contains(&vi);
                    im.put_pixel(
                        x,
                        y,
                        if cavity {
                            image::Rgba([255, 0, 255, 255])
                        } else {
                            image::Rgba([220, 185, 172, 255])
                        },
                    );
                }
            }
            return encode_png(&im);
        }
        if v == "checker" {
            let mut im = image::RgbaImage::new(w, h);
            for y in 0..h {
                for x in 0..w {
                    let c = if ((x / 32) + (y / 32)) % 2 == 0 { 230u8 } else { 70u8 };
                    im.put_pixel(
                        x,
                        y,
                        image::Rgba([c, (x * 255 / w.max(1)) as u8, (y * 255 / h.max(1)) as u8, 255]),
                    );
                }
            }
            return encode_png(&im);
        }
        return crate::unity::solid_png([0.86, 0.72, 0.68, 1.0], w, h);
    }
    let mut out = img.clone();
    let maskname = entry
        .as_ref()
        .map(|e| e.info("ColorMaskTex"))
        .unwrap_or_default();
    let mask = if !maskname.is_empty() && maskname != "0" {
        face_named_png(game_root, entry.as_ref(), &maskname, "ColorMaskAB", head_texs)
            .and_then(|b| crate::teximg::load_from_memory(&b).ok())
            .map(|i| i.to_rgba8())
    } else {
        None
    };
    match &mask {
        Some(mi) => {
            let (mw, mh) = mi.dimensions();
            for y in 0..h {
                for x in 0..w {
                    let p = img.get_pixel(x, y).0;
                    let m = mi
                        .get_pixel(x * mw / w.max(1), y * mh / h.max(1))
                        .0;
                    // `cf_face_00_mc`: face island is red (r=1,g=0), unused
                    // background is yellow (r=1,g=1), mouth rect is blue (b=1).
                    // Koikano/create_skinface ps_4_0: multiply the two mask
                    // tint terms, then clamp up to mask.b before MainTex.
                    let mr = (m[0] as f32 / 255.0).clamp(0.0, 1.0);
                    let mg = (m[1] as f32 / 255.0).clamp(0.0, 1.0);
                    let mut q = [0u8; 4];
                    for c in 0..3 {
                        let b = p[c] as f32 / 255.0;
                        let t = 1.0 + (skin[c] - 1.0) * mr;
                        let col = (t * (1.0 + (skin_sub[c] - 1.0) * mg))
                            .max(m[2] as f32 / 255.0);
                        q[c] = (b * col * 255.0).clamp(0.0, 255.0) as u8;
                    }
                    q[3] = 255;
                    out.put_pixel(x, y, image::Rgba(q));
                }
            }
        }
        None => {
            if let Some(t) = crate::unity::tint_png(&base, skin) {
                out = crate::teximg::load_from_memory(&t).ok()?.to_rgba8();
            }
        }
    }
    // The vanilla `cf_face_00_t` paints the mouth cavity as a hard-edged dark
    // rectangle (img x 332-691, y 845-1018). `cf_O_face` samples its top for the
    // chin (dump: chin uv v 0.10-0.17 => img y ~892-920) and its bottom for the
    // inner mouth, so the chin shows a dark W. The compiled `cf_m_face_create`
    // composites a mouth/lip layer there; approximate it by filling the
    // rectangle's upper half with the surrounding skin
    // (`OK_FACE_CAVITY_FILL=skin`, default) or the lip colour (`=lip`) while the
    // lower half stays dark for an open mouth (`=0` disables).
    // Mouth-cavity fill is off. The dark rectangle is the source texture's
    // inner mouth; filling it was a dump-pixel workaround, not CreateFaceTexture.
    let cavity_fill = std::env::var("OK_FACE_CAVITY_FILL").unwrap_or_else(|_| "0".into());
    if cavity_fill != "0" {
        let x0 = (0.324 * w as f32) as i32;
        let x1 = (0.675 * w as f32) as i32;
        let y0 = (0.825 * h as f32) as i32;
        let y1 = (0.994 * h as f32) as i32;
        let top_end = y0 + (((y1 - y0) as f32) * 0.5) as i32;
        let taper = ((0.03 * h as f32) as i32).max(3);
        let ref_xy = ((w / 2).min(w - 1), (y0 - 8).max(0) as u32);
        let refc = out.get_pixel(ref_xy.0, ref_xy.1).0;
        let fill = if cavity_fill == "lip" {
            [
                (card.lip_color[0].clamp(0.0, 1.0) * 255.0) as u8,
                (card.lip_color[1].clamp(0.0, 1.0) * 255.0) as u8,
                (card.lip_color[2].clamp(0.0, 1.0) * 255.0) as u8,
            ]
        } else {
            [refc[0], refc[1], refc[2]]
        };
        for y in y0..=top_end {
            for x in x0..=x1 {
                let e = (x - x0).min(x1 - x).min(y - y0).max(0);
                let b = top_end - y;
                let t = (e as f32 / taper as f32)
                    .min(1.0)
                    .min((b as f32 / taper as f32).min(1.0));
                let p = out.get_pixel(x as u32, y as u32).0;
                let mut q = [0u8; 4];
                for c in 0..3 {
                    q[c] = (p[c] as f32 * (1.0 - t) + fill[c] as f32 * t) as u8;
                }
                q[3] = p[3];
                out.put_pixel(x as u32, y as u32, image::Rgba(q));
            }
        }
    }
    let mut png = encode_png(&out)?;
    // ChaControl.CreateFaceTexture composites the makeup layers into `_MainTex`
    // through `customTexCtrlFace` / `cf_m_face_create`, in this order:
    //   _Texture3 paint0 (_Color3, _paint1)  _Texture7 paint1 (_Color7, _paint2)
    //   _Texture4 cheek (_Color4)            _Texture5 lipline (_Color5)
    //   _Texture6 mole (_Color6, _hokuro)
    // The compiled shader supplies the blend equation. Makeup list rows must
    // first be resolved through the card's Sideloader GUID where present.
    let overlay = |png: &mut Vec<u8>,
                   cat: i32,
                   id: i32,
                   prop: &str,
                   color: [f32; 4],
                   tag: &str| {
        if id == 0 {
            return;
        }
        if let Some(over) = load_card_makeup_png(
            game_root,
            vanilla,
            mod_catalog,
            card,
            guid_index,
            cat,
            id,
            prop,
            head_texs,
            body_texs,
        ) {
            if std::env::var("OK_OVER_DUMP").is_ok() {
                let _ = std::fs::write(
                    format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\over_{tag}.png"),
                    &over,
                );
            }
            if let Some(blended) = crate::unity::overlay_overtex(png, &over, color) {
                *png = blended;
            }
        }
    };
    // Paint, lipline and mole remain opt-in while their UV sampling is moved
    // over from `Koikano/create_skinface`; cheek is handled separately below.
    if std::env::var("OK_FACE_OVERLAY").is_ok() {
        for (i, id) in card.paint_ids.iter().enumerate() {
            overlay(
                &mut png,
                405,
                *id,
                "PaintTex",
                card.paint_colors[i],
                &format!("paint{i}"),
            );
        }
        overlay(
            &mut png,
            404,
            card.lip_line_id,
            "LiplineTex",
            card.lip_line_color,
            "lipline",
        );
    }
    // CreateFaceTexture `_Texture4` + `_Color4` (ChaControl.cs:3612/3620).
    // Unlike MainTex it uses `_tex4uv` from the compiled shader.
    if let Some(cheek) = load_card_makeup_png(game_root, vanilla, mod_catalog,
        card, guid_index, 402, card.cheek_id, "CheekTex", head_texs, body_texs) {
        if std::env::var("OK_OVER_DUMP").is_ok() {
            let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\over_cheek.png", &cheek);
        }
        if let Some(blended) = overlay_face_create_cheek(&png, &cheek, card.cheek_color) {
            png = blended;
        }
    }
    // Hoho is a draw-material slot, not part of CreateFaceTexture. Its source
    // RGB, status alpha, UV2 and MaterialEditor replacement are retained below.
    // Lip: `ChangeSettingLip` puts `mt_lip/LipTex` on `_overtex1` with
    // `_overcolor1` = `lipColor` (ChaControl.cs:4207/4222). `cf_m_face_create`
    // composites it; we approximate with the same face-UV `lerp(base, tex*color,
    // tex.a)` used for the other makeup layers. id 0 is the default lip row.
    if std::env::var("OK_FACE_OVERLAY").is_ok() {
        if std::env::var("OK_OVER_DUMP").is_ok() {
            let v = vanilla
                .get(403, card.lip_id)
                .map(|e| (e.main_ab.clone(), e.info("LipTex"), e.main_tex.clone()));
            let m = mod_catalog
                .get(403, card.lip_id)
                .map(|e| (e.main_ab.clone(), e.info("LipTex"), e.main_tex.clone()));
            eprintln!("ok-extract: LIPDIAG id={} vanilla={v:?} mod={m:?}", card.lip_id);
        }
        if let Some(over) = load_catalog_png_allow0(
            game_root,
            vanilla,
            mod_catalog,
            403,
            card.lip_id,
            "LipTex",
            head_texs,
            body_texs,
        ) {
            if std::env::var("OK_OVER_DUMP").is_ok() {
                let _ = std::fs::write(
                    r"C:\Users\Damody\AppData\Local\Temp\opencode\over_lip.png",
                    &over,
                );
            }
            if let Some(blended) = crate::unity::overlay_overtex(&png, &over, card.lip_color) {
                png = blended;
            }
        }
    }
    // Mole (`_Texture6` + `_hokuro`): ChaControl.cs:3652-3658 and
    // KoikatsuFaceTextureBaker.GetMoleLayout / SetVector(material, "_hokuro", ...)
    // -> `_hokuro = (lerp(0.25,-0.25,x), lerp(0.3,-0.3,y), 0, lerp(0,0.7,w))`.
    // `_hokuro` places the mole through the compiled `cf_m_face_create` shader.
    // Sampling with `(uv-offset)/scale` lands the band on the eyes, so keep it
    // behind a flag until the shader's exact mapping is known.
    if std::env::var("OK_MOLE").is_ok() && card.mole_id != 0 {
        let l = card.mole_layout;
        let hx = 0.25 + (-0.25 - 0.25) * l[0];
        let hy = 0.3 + (-0.3 - 0.3) * l[1];
        let hw = 0.7 * l[3];
        let over = load_catalog_png(
            game_root,
            vanilla,
            mod_catalog,
            415,
            card.mole_id,
            "MoleTex",
            head_texs,
            body_texs,
        );
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: mole id={} layout={:?} hokuro=({hx:.4},{hy:.4},{hw:.4}) tex={}",
                card.mole_id,
                card.mole_layout,
                over.is_some()
            );
        }
        if let Some(over) = over {
            if let Some(blended) =
                crate::unity::overlay_overtex_uv(&png, &over, card.mole_color, [hx, hy], [hw, hw])
            {
                png = blended;
            }
        }
    }
    // `_overtex1` lip is set on matDraw after this create pass. Face paint and
    // mole still require their DXBC coordinate transform and paintmask sample.
    // Mod face atlases arrive in either row orientation.  The common face
    // mesh has its chin at UV (0.5, 0.125), and Unity's rendered _MainTex
    // places the mouth-cavity rectangle above the facial skin in PNG rows.
    // Normalize by the atlas's actual cavity location, not the character or
    // the bundle filename: a fixed flip fixes one head mod but breaks another.
    let mut face = crate::teximg::load_from_memory(&png).ok()?.to_rgba8();
    if face_needs_vertical_flip {
        image::imageops::flip_vertical_in_place(&mut face);
    }
    // KSOX FaceOver runs on the shader destination RenderTexture, after the
    // create pass. Its PNG rows are already in the final draw atlas direction.
    let png = encode_png(&face)?;
    if let Some(ov) = ksox_overlay_png(card, outfit, 2) {
        composite_overlay(&png, &ov).or(Some(png))
    } else {
        Some(png)
    }
}

pub(crate) fn face_cavity_is_at_bottom(base: &[u8]) -> bool {
    let Ok(face) = crate::teximg::load_from_memory(base) else { return false; };
    let face = face.to_rgba8();
    let (w, h) = face.dimensions();
    if w < 64 || h < 64 { return false; }
    let luma = |ya: f32, yb: f32| -> f64 {
        let mut total = 0.0;
        let mut count = 0u64;
        for y in ((h as f32 * ya) as u32)..((h as f32 * yb) as u32) {
            for x in ((w as f32 * 0.40) as u32)..((w as f32 * 0.60) as u32) {
                let p = face.get_pixel(x, y).0;
                total += 0.299 * p[0] as f64 + 0.587 * p[1] as f64 + 0.114 * p[2] as f64;
                count += 1;
            }
        }
        total / count.max(1) as f64
    };
    luma(0.83, 0.98) + 40.0 < luma(0.02, 0.17)
}

pub(crate) fn flip_png_vertical(png: &[u8]) -> Option<Vec<u8>> {
    png_transform_cache::transform("flip_v_v1",png,&[],||flip_png_vertical_uncached(png))
}
pub(crate) fn flip_png_vertical_uncached(png: &[u8]) -> Option<Vec<u8>> {
    let mut image = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    image::imageops::flip_vertical_in_place(&mut image);
    encode_png(&image)
}

// unity_asset_decode's Texture2D rows are opposite to the game-exported
// RenderTexture PNGs and card Material Editor PNGs. Canonicalize only masks
// originating from serialized Texture2D. ME overrides are already oriented
// and must not be flipped a second time.
pub(crate) fn orient_raw_mask(png: Vec<u8>) -> Vec<u8> {
    flip_png_vertical(&png).unwrap_or(png)
}

pub(crate) fn linear_texture_to_srgb_png(png: &[u8]) -> Option<Vec<u8>> {
    png_transform_cache::transform("linear_to_srgb_v1",png,&[],||linear_texture_to_srgb_png_uncached(png))
}
pub(crate) fn linear_texture_to_srgb_png_uncached(png: &[u8]) -> Option<Vec<u8>> {
    let mut image = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    for pixel in image.pixels_mut() {
        for c in 0..3 {
            pixel[c] = linear_to_srgb_u8(pixel[c]);
        }
    }
    encode_png(&image)
}

/// Resolve a makeup texture through the card's Sideloader record. A global
/// category/id catalog can contain another mod's row at the same local id.
#[allow(clippy::too_many_arguments)]
pub(crate) fn load_card_makeup_png(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String, PathBuf>,
    cat: i32,
    id: i32,
    key: &str,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    if id == 0 { return None; }
    if let Some(resolve) = card.resolves.iter().find(|r|
        r.category_no == cat && (r.slot == id || r.local_slot == id)) {
        if let Some(zip) = guid_path(guid_index, &resolve.mod_id) {
            let catalog = catalog_from_zipmod(zip);
            if let Some(entry) = catalog.get_slot(cat, resolve.slot, resolve.local_slot) {
                let tex = entry.info(key);
                if std::env::var("OK_DIAG").is_ok() {
                    eprintln!("ok-extract: makeup resolve guid={} cat={} slot={} local={} row={} row_id={} ab={} tex={} zip={}",
                        resolve.mod_id, cat, resolve.slot, resolve.local_slot, entry.name,
                        entry.id, entry.main_ab, tex, zip.display());
                }
                let names: Vec<&str> = [tex.as_str(), entry.main_tex.as_str()]
                    .into_iter().filter(|n| !n.is_empty() && *n != "0").collect();
                if !names.is_empty() {
                    let zip_str = zip.to_string_lossy();
                    if let Some(png) = sideloader_png(Some(&zip_str), game_root, &entry.main_ab, &names)
                        .or_else(|| crate::mods::zipmod_bundle_named(zip, &entry.main_ab)
                            .and_then(|bytes| crate::unity::named_png_from_bytes(bytes, &names).map(|(_, b)| b))) {
                        return Some(png);
                    }
                }
                eprintln!("ok-extract: ERROR makeup asset missing guid={} cat={} slot={} local={} tex={}",
                    resolve.mod_id, cat, resolve.slot, resolve.local_slot, tex);
                return None;
            }
            eprintln!("ok-extract: ERROR makeup row missing guid={} cat={} slot={} local={}",
                resolve.mod_id, cat, resolve.slot, resolve.local_slot);
            return None;
        }
        eprintln!("ok-extract: ERROR makeup mod missing guid={} cat={} slot={}",
            resolve.mod_id, cat, resolve.slot);
        return None;
    }
    // Vanilla ids have no Sideloader record. Prefer the game's vanilla row
    // when a mod happens to reuse that id.
    for entry in [vanilla.get(cat, id), mod_catalog.get(cat, id)].into_iter().flatten() {
        let tex = entry.info(key);
        let names: Vec<&str> = [tex.as_str(), entry.main_tex.as_str()]
            .into_iter().filter(|n| !n.is_empty() && *n != "0").collect();
        if let Some(png) = png_from_bundle(game_root, &entry.main_ab,
            entry.extra.get("__zip").map(|s| s.as_str()), &names, head_texs, body_texs) {
            return Some(png);
        }
    }
    None
}

/// `Koikano/create_skinface` t1 (`_Texture4`) sampling and blend. The UV
/// vector is the `cf_m_face_create` material default observed in Maker;
/// DXBC computes (uv + offset) * (1 - scale) + (scale - 0.5).
pub(crate) fn overlay_face_create_cheek(base: &[u8], cheek: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(cheek).ok()?.to_rgba8();
    let (w, h) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if w == 0 || h == 0 || sw == 0 || sh == 0 { return None; }
    let uv = [0.5f32, 0.64, -0.33, -4.0];
    for y in 0..h {
        for x in 0..w {
            // This atlas is vertically flipped once after the bake. Its
            // pre-flip row therefore corresponds to Unity v=(y+0.5)/h.
            let u = (x as f32 + 0.5) / w as f32;
            let v = (y as f32 + 0.5) / h as f32;
            let tu = ((u + uv[0]) * (1.0 - uv[2]) + uv[2] - 0.5).clamp(0.0, 1.0);
            let tv = ((v + uv[1]) * (1.0 - uv[3]) + uv[3] - 0.5).clamp(0.0, 1.0);
            // The Unity texture is Clamp/Bilinear. PNG rows run downward.
            let fx = (tu * sw as f32 - 0.5).clamp(0.0, sw.saturating_sub(1) as f32);
            let fy = ((1.0 - tv) * sh as f32 - 0.5).clamp(0.0, sh.saturating_sub(1) as f32);
            let x0 = fx.floor() as u32;
            let y0 = fy.floor() as u32;
            let x1 = (x0 + 1).min(sw - 1);
            let y1 = (y0 + 1).min(sh - 1);
            let tx = fx - x0 as f32;
            let ty = fy - y0 as f32;
            let p00 = src.get_pixel(x0, y0).0;
            let p10 = src.get_pixel(x1, y0).0;
            let p01 = src.get_pixel(x0, y1).0;
            let p11 = src.get_pixel(x1, y1).0;
            let sample = |c: usize| {
                (p00[c] as f32 * (1.0 - tx) + p10[c] as f32 * tx) * (1.0 - ty)
                    + (p01[c] as f32 * (1.0 - tx) + p11[c] as f32 * tx) * ty
            };
            let a = (sample(3) / 255.0 * color[3]).clamp(0.0, 1.0);
            let out = dst.get_pixel_mut(x, y);
            for c in 0..3 {
                out[c] = ((out[c] as f32) * (1.0 - a) + sample(c) * color[c] * a)
                    .clamp(0.0, 255.0) as u8;
            }
        }
    }
    encode_png(&dst)
}

/// Turn a white-on-black mask (flat alpha) into a coloured RGBA where black is
/// transparent and brightness is coverage. Used for the eye white texture.
pub(crate) fn white_on_black_alpha(png: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    let alpha_varies = img.pixels().any(|p| p.0[3] < 245);
    let mut out = image::RgbaImage::new(w, h);
    for y in 0..h {
        for x in 0..w {
            let p = img.get_pixel(x, y).0;
            let a = if alpha_varies {
                p[3] as f32 / 255.0
            } else {
                (p[0] as f32 + p[1] as f32 + p[2] as f32) / (3.0 * 255.0)
            };
            out.put_pixel(
                x,
                y,
                image::Rgba([
                    (color[0].clamp(0.0, 1.0) * 255.0) as u8,
                    (color[1].clamp(0.0, 1.0) * 255.0) as u8,
                    (color[2].clamp(0.0, 1.0) * 255.0) as u8,
                    (a.clamp(0.0, 1.0) * 255.0) as u8,
                ]),
            );
        }
    }
    encode_png(&out)
}

/// Keep RGB, but when the source alpha is flat treat brightness as coverage.
/// Used for iris (`cw_t_hitomi_*`): black around the pupil becomes transparent
/// so a translucent iris composites over the opaque sclera (StudioEditor
/// queues iris after sclera instead of punching holes with a masked blend).
pub(crate) fn rgb_on_black_alpha(png: &[u8]) -> Option<Vec<u8>> {
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    let alpha_varies = img.pixels().any(|p| p.0[3] < 245);
    if alpha_varies {
        return Some(png.to_vec());
    }
    let mut out = image::RgbaImage::new(w, h);
    for y in 0..h {
        for x in 0..w {
            let mut p = img.get_pixel(x, y).0;
            let a = (p[0] as f32 + p[1] as f32 + p[2] as f32) / (3.0 * 255.0);
            p[3] = (a.clamp(0.0, 1.0) * 255.0) as u8;
            out.put_pixel(x, y, image::Rgba(p));
        }
    }
    encode_png(&out)
}

/// StudioEditor `KoikatsuEyeTextureBaker.CalculateUvTransform`: the iris
/// material's main-texture transform (`SetMainTextureTransform`). `scale` is
/// `one + customScale`, `customScale = lerp(1.8,-0.2, pupilWidth/Height)`;
/// `offset = motion - customScale*0.5`, where `motion` comes from the pupil
/// X/Y sliders (mirrored for the right eye, clamped to unit length).
pub(crate) fn iris_uv_transform(
    width: f32,
    height: f32,
    x: f32,
    y: f32,
    right: bool,
) -> ([f32; 2], [f32; 2]) {
    let lerp = |a: f32, b: f32, t: f32| a + (b - a) * t;
    let custom = [
        lerp(1.8, -0.2, width),
        lerp(1.8, -0.2, height),
    ];
    let scale = [1.0 + custom[0], 1.0 + custom[1]];
    let mut eye = [lerp(0.2, -0.6, x), lerp(-0.5, 0.5, y)];
    if right {
        eye[0] = -eye[0];
    }
    let mag = (eye[0] * eye[0] + eye[1] * eye[1]).sqrt();
    if mag > 1.0 {
        eye[0] /= mag;
        eye[1] /= mag;
    }
    let motion = [
        eye[0] * 0.1 * lerp(1.0, 5.0, custom[0].clamp(0.,1.)),
        -eye[1] * 0.1 * lerp(1.0, 5.0, custom[1].clamp(0.,1.)),
    ];
    (
        scale,
        [motion[0] - custom[0] * 0.5, motion[1] - custom[1] * 0.5],
    )
}

pub(crate) fn apply_uv_st(uvs: &mut [[f32; 2]], scale: [f32; 2], offset: [f32; 2]) {
    // Clamp belongs to the texture sampler after triangle interpolation.
    for uv in uvs {
        uv[0] = uv[0] * scale[0] + offset[0];
        uv[1] = uv[1] * scale[1] + offset[1];
    }
}

/// Encode a linear card colour to the sRGB space our textures live in.
pub(crate) fn lin_to_srgb(c: [f32; 4]) -> [f32; 4] {
    let f = |x: f32| x.clamp(0.0, 1.0).powf(1.0 / 2.2);
    [f(c[0]), f(c[1]), f(c[2]), c[3]]
}

pub(crate) fn mix4(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
    [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
        a[3] + (b[3] - a[3]) * t,
    ]
}

/// KKEx row for a face-detail category (eyeline, brow, ...). Flat
/// `mod_catalog.get(cat, cardId)` misses sideloader ids such as Upper Eyes
/// `eyelineUpId=9997` (guid `8246asdf.UpperEyes`).
pub(crate) fn face_detail_entry(
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String, PathBuf>,
    cat: i32,
    id: i32,
) -> Option<ListEntry> {
    face_detail_entry_for_property(vanilla,mod_catalog,card,guid_index,cat,id,None)
}

pub(crate) fn face_detail_entry_for_property(vanilla:&ChaListCatalog,mod_catalog:&ChaListCatalog,card:&CardInfo,
    guid_index:&std::collections::HashMap<String,PathBuf>,cat:i32,id:i32,property:Option<&str>)->Option<ListEntry> {
    let mut zip_lists = std::collections::HashMap::new();
    if let Some(r) = card.resolves.iter().find(|r| r.category_no == cat && property.is_none_or(|p|r.property.eq_ignore_ascii_case(p))) {
        let zip = guid_path(guid_index, &r.mod_id);
        return lookup_entry(
            vanilla,
            &mut zip_lists,
            zip.map(|p| p.as_path()),
            &r.mod_id,
            cat,
            r.slot,
            r.local_slot,
        );
    }
    // No KKEx row: the card id is the vanilla list id. A mod that reused
    // local id 0 must not steal it — 226176 eyelineUpId=0 was resolving to
    // ToLove "Yami" instead of the vanilla row the game loads.
    vanilla
        .get(cat, id)
        .or_else(|| mod_catalog.get(cat, id))
        .cloned()
}

pub(crate) fn skin_catalog_texture(game:&Path,vanilla:&ChaListCatalog,mods:&ChaListCatalog,
    card:&CardInfo,guids:&std::collections::HashMap<String,PathBuf>,
    category:i32,id:i32,key:&str)->Result<Option<crate::skin_source::SkinTexture>,String> {
    let entry=face_detail_entry_for_property(vanilla,mods,card,guids,category,id,None)
        .ok_or_else(||format!("missing SkinPlus catalog row category={category} id={id}"))?;
    let name=entry.info(key);
    if entry.main_ab=="0" || name=="0" {return Ok(None);}
    if entry.main_ab.is_empty() || name.is_empty() {
        return Err(format!("missing SkinPlus catalog path category={category} id={id} key={key}"));
    }
    let bytes=accessory_bundle_bytes(game,entry.extra.get("__zip").map(|s|Path::new(s)),&entry)
        .ok_or_else(||format!("missing SkinPlus bundle {}",entry.main_ab))?;
    let bundle=crate::unity::parse_bundle_cached(bytes);
    let name=if character_hi_poly() {name}else{format!("{name}_low")};
    crate::skin_source::bundle_texture(&bundle,&name).map(Some)
        .ok_or_else(||format!("missing SkinPlus texture or sampler {name} in {}",entry.main_ab))
}

// ChaControl.GetTexture tries the head suffix, then the exact base asset.
pub(crate) fn detail_asset_names(base: &str, head_suffix: bool, head_id: i32) -> Vec<String> {
    let mut names = Vec::new();
    let mut push = |n: &str| {
        if !character_hi_poly() {
            names.push(format!("{n}_low"));
        }
        names.push(n.to_string());
    };
    if head_suffix && head_id != 0 {
        push(&format!("{base}_{head_id}"));
    }
    push(base);
    names
}
