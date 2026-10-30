//! Textures from list catalogues and clothes create slots: emblems, catalogue PNG loaders, runtime row order, the clothes create bake and overlays.
use super::*;

/// ChangeCustomEmblem assigns a texture from mt_emblem to renderers using
/// main_clothes_emblem. With no selected texture the game's shader clips the
/// emblem; a white fallback would instead draw the bare emblem polygon.
pub(crate) fn apply_clothes_emblems(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    clothes_kind: usize,
    meshes: &mut Vec<ExtractedMesh>,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) {
    let ids = card.clothes_emblem_ids.get(clothes_kind).copied().unwrap_or([0; 2]);
    let mut textures: [Option<Vec<u8>>; 2] = [None, None];
    for (i, id) in ids.into_iter().enumerate() {
        if id != 0 {
            textures[i] = load_catalog_png_ab(
                game_root, vanilla, mod_catalog, 431, id,
                "MainTex", "MainTexAB", head_texs, body_texs,
            );
        }
    }
    meshes.retain_mut(|mesh| {
        let emblem = mesh.mat.as_ref().is_some_and(|mat| {
            mat.shader.to_ascii_lowercase().contains("main_clothes_emblem")
                || mat.name.to_ascii_lowercase().contains("emblem")
        }) || mesh.name.to_ascii_lowercase().contains("emblem");
        if !emblem { return true; }
        let slot = usize::from(mesh.name.to_ascii_lowercase().contains("emblem_02"));
        if let Some(png) = textures[slot].as_ref() {
            mesh.texture_png = Some(png.clone());
            true
        } else {
            false
        }
    });
}

/// ChaControl.CreateClothesTexture (ChaControl.cs:10840-10920): the albedo and
/// colour mask of a clothes item come from the *list entry*
/// (`MainTex`/`ColorMaskTex`, `_low` when `!hiPoly`), never from the renderer
/// material (whose `_MainTex` is a runtime render texture left empty in the
/// bundle). `key` is e.g. `"MainTex"` or `"ColorMaskTex"`.
pub(crate) fn clothes_list_png(
    game_root: &Path,
    entry: &ListEntry,
    key: &str,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    clothes_list_png_ab(
        game_root,
        entry,
        key,
        clothes_ab_key_for(key),
        head_texs,
        body_texs,
    )
}

pub(crate) fn clothes_ab_key_for(tex_key: &str) -> &'static str {
    match tex_key {
        "MainTex" => "MainTexAB",
        "MainTex02" => "MainTex02AB",
        "MainTex03" => "MainTex03AB",
        "ColorMaskTex" => "ColorMaskAB",
        "ColorMask02Tex" => "ColorMask02AB",
        "ColorMask03Tex" => "ColorMask03AB",
        _ => "MainAB",
    }
}

/// `CreateClothesTexture`: `MainTexAB`/`ColorMaskAB`/… fall back to `MainAB`
/// when the list cell is empty or `"0"`.
pub(crate) fn clothes_tex_ab(entry: &ListEntry, ab_key: &str) -> String {
    let a = entry.info(ab_key);
    if a.is_empty() || a == "0" {
        entry.main_ab.clone()
    } else {
        a
    }
}

pub(crate) fn clothes_list_png_ab(
    game_root: &Path,
    entry: &ListEntry,
    key: &str,
    ab_key: &str,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    let name = entry.info(key);
    if name.is_empty() || name == "0" {
        return None;
    }
    let ab = clothes_tex_ab(entry, ab_key);
    let zip = entry.extra.get("__zip").map(String::as_str);
    let low = format!("{name}_low");
    if !character_hi_poly() {
        if let Some(p) = png_from_bundle(game_root, &ab, zip, &[&low], head_texs, body_texs) {
            return Some(p);
        }
    }
    png_from_bundle(game_root, &ab, zip, &[&name], head_texs, body_texs)
}

/// `ChangeCustomClothes` (ChaControl.cs:2859-2966): create-RT index for a mesh.
pub(crate) fn clothes_create_slot(renderer_ids: &[i64], map: &crate::unity::ClothesRendererMap) -> Option<usize> {
    // Native writes normal01/alpha01, normal02, normal03, accessory in
    // that order. A renderer appearing in several groups keeps the last write.
    if renderer_ids.iter().any(|id| map.accessory.contains(id)) {
        Some(0)
    } else if renderer_ids.iter().any(|id| map.normal03.contains(id)) {
        Some(2)
    } else if renderer_ids.iter().any(|id| map.normal02.contains(id)) {
        Some(1)
    } else if renderer_ids.iter().any(|id| {
        map.normal01.contains(id) || map.alpha01.contains(id) || map.accessory.contains(id)
    }) {
        Some(0)
    } else {
        None
    }
}

pub(crate) const CLOTHES_CREATE_KEYS: [(&str, &str); 3] = [
    ("MainTex", "ColorMaskTex"),
    ("MainTex02", "ColorMask02Tex"),
    ("MainTex03", "ColorMask03Tex"),
];

/// Bake the 3 create RTs (`CreateClothesTexture` i=0..2). Slot 0 is required
/// by the game; 1/2 exist only when `MainTex02`/`MainTex03` are set.
pub(crate) fn bake_clothes_create_textures(
    game_root: &Path,
    entry: &ListEntry,
    cols: [[f32; 4]; 4],
    pats: &[ClothesPatternSrc],
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> ([Option<Vec<u8>>; 3], bool) {
    let mut baked = [None, None, None];
    let mut used_mask = false;
    for (i, (mt, mc)) in CLOTHES_CREATE_KEYS.iter().enumerate() {
        let t0 = Instant::now();
        let Some(main) = clothes_list_png(game_root, entry, mt, head_texs, body_texs) else {
            continue;
        };
        let tex = if let Some(mask) = clothes_list_png(game_root, entry, mc, head_texs, body_texs)
        {
            if i == 0 {
                used_mask = true;
            }
            profile_tick("bake: list pngs", t0);
            let sampling=clothes_mask_sampling(game_root,entry,mc);
            let main_srgb=clothes_mask_sampling(game_root,entry,mt).srgb;
            match clothes_gpu_bake(game_root,entry,mt,mc,cols,pats) {
                Ok(png)=>{eprintln!("ok-extract: native clothes GPU bake key={mt} adapter={}",clothes_gpu::identity());png},
                Err(error)=>{eprintln!("ok-extract: clothes CPU fallback key={mt}: {error}");apply_clothes_color_mask_sampled(&main,&mask,cols,pats,sampling,Some(main_srgb)).unwrap_or(main)}
            }
        } else {
            main
        };
        baked[i] = Some(tex);
    }
    (baked, used_mask)
}

/// CustomTextureCreate allocates a default Bilinear, Clamp, sRGB RenderTexture
/// with automatic mips. Mark only successful RT writes;
/// ordinary hair/asset PNGs retain their authored Texture2D provenance.
pub(crate) fn mark_runtime_main_texture(mesh: &mut ExtractedMesh) {
    mesh.runtime_textures.insert("_MainTex".into(), [1, 1, 1, 1, 1]);
}

/// These draw shaders declare _MainTex = "white" in ShaderLab. Null is a
/// valid material input, not a failed texture import or UE default material.
pub(crate) fn apply_clothes_default_main_texture(mesh: &mut ExtractedMesh) {
    if mesh.texture_png.is_some() { return; }
    let Some(mat) = mesh.mat.as_ref() else { return; };
    if matches!(mat.shader.to_ascii_lowercase().as_str(),
        "shader forge/main_opaque" | "koikano/main_clothes_opaque") {
        mesh.texture_png = crate::unity::solid_png([1.;4], 8, 8);
    }
}

/// MaterialAPI.SetTexture copies the replaced texture's wrapMode (or Repeat
/// for a null texture). A previous create RT takes precedence over the prefab.
pub(crate) fn clear_unbound_normal_map(mesh: &mut ExtractedMesh) {let _spot=crate::profile::spot("clear_unbound_normal_map");
    if mesh.mat.as_ref().is_some_and(|mat|mat.slot("_NormalMap").is_none()) {
        mesh.normal_map_png = None;
        mesh.runtime_textures.remove("_NormalMap");
    }
}

pub(crate) fn mark_material_editor_main_texture(mesh: &mut ExtractedMesh) {
    mark_material_editor_texture(mesh,"_MainTex");
}

pub(crate) fn mark_material_editor_texture(mesh: &mut ExtractedMesh, property: &str) {
    let wrap = mesh.runtime_textures.get(property).map(|s|s[1])
        .or_else(||mesh.mat.as_ref().and_then(|m|m.tex_wraps.get(property).copied())).unwrap_or(0);
    mesh.runtime_textures.insert(property.into(), [1,wrap,wrap,1,1]);
}

#[cfg(test)]
#[test]
pub(crate) fn runtime_texture_material_editor_copies_replaced_wrap() {
    let mut mesh = ExtractedMesh::default();
    mark_material_editor_main_texture(&mut mesh);
    assert_eq!(mesh.runtime_textures["_MainTex"], [1,0,0,1,1]); // null original
    mesh.runtime_textures.clear();
    let mut material = crate::unity::MeshMaterial::default();
    material.tex_wraps.insert("_MainTex".into(),2);
    mesh.mat = Some(material);
    mark_material_editor_main_texture(&mut mesh);
    assert_eq!(mesh.runtime_textures["_MainTex"], [1,2,2,1,1]); // authored Mirror
    mark_runtime_main_texture(&mut mesh);
    mark_material_editor_main_texture(&mut mesh);
    assert_eq!(mesh.runtime_textures["_MainTex"], [1,1,1,1,1]); // create RT replaces asset
}

#[cfg(test)]
#[test]
pub(crate) fn runtime_texture_tracks_create_assignment_not_role() {
    let mut meshes = vec![ExtractedMesh::default(), ExtractedMesh {
        submesh_index: 1, texture_png: Some(vec![9]), ..Default::default()
    }];
    apply_clothes_create_textures(&mut meshes, None, &[Some(vec![1,2,3]), None, None]);
    assert!(meshes[0].runtime_textures.is_empty());
    assert!(meshes[0].texture_png.is_none());
    meshes[0].renderer_ids=vec![10];
    let map=crate::unity::ClothesRendererMap {normal01:[10].into(),..Default::default()};
    apply_clothes_create_textures(&mut meshes, Some(&map), &[Some(vec![1,2,3]), None, None]);
    assert_eq!(meshes[0].runtime_textures.get("_MainTex"), Some(&[1,1,1,1,1]));
    assert!(meshes[1].runtime_textures.is_empty());
    assert_eq!(meshes[1].texture_png, Some(vec![9]));
    let mut missing = vec![ExtractedMesh::default()];
    apply_clothes_create_textures(&mut missing, None, &[None,None,None]);
    assert!(missing[0].runtime_textures.is_empty());
}

/// A list item's renderers as the game builds them before any card customises them (OK model editor
/// preview): material alpha masks, and for clothes `CreateClothesTexture` from the list textures with the
/// item's default colours (its material `_Color`..`_Color4`), no patterns.
pub(crate) fn preview_list_item(game_root: &Path, entry: &ListEntry, prefab: &str, meshes: &mut [ExtractedMesh]) {
    resolve_material_alpha_masks(game_root, meshes);
    if !matches!(entry.category_no, 105..=112 | 200..=212) { return; }
    let colour = |k: &str| meshes.iter().find_map(|m| m.mat.as_ref().and_then(|x| x.colors.get(k).copied()));
    let cols = ["_Color", "_Color2", "_Color3", "_Color4"].map(|k| colour(k).unwrap_or([1.0; 4]));
    let zip = entry.extra.get("__zip").map(PathBuf::from);
    let map = accessory_bundle_bytes(game_root, zip.as_deref(), entry).and_then(|b| crate::unity::clothes_renderer_map(&b, prefab));
    let (baked, _) = bake_clothes_create_textures(game_root, entry, cols, &[], &[], &[]);
    apply_clothes_create_textures(meshes, map.as_ref(), &baked);
    finalize_clothes_texture(meshes);
}

/// Session parts of one list item's meshes through the card pipeline's own finishing (`push_meshes`)
/// with no card data and no character skeleton (OK model editor preview).
pub(crate) fn preview_session_parts(game_root: &Path, role: &str, meshes: Vec<ExtractedMesh>, entry: Option<&ListEntry>)
    -> Result<Vec<crate::session::SessionPart>, String> {
    let mut parts = Vec::new();
    let mut session_parts = Vec::new();
    let mut seen = std::collections::HashSet::new();
    push_meshes(&mut parts, &mut session_parts, None, game_root, role, meshes, entry.map(|e| e.id), entry.map(|e| e.category_no),
        None, None, entry.map(|e| e.name.clone()), None, None, None, None, &mut seen, None, None, None, None, None, None, None, 0, None, None, None, None)?;
    // No character skeleton: the posed meshes carry no weights and stay static (not bound to a body).
    for p in &mut session_parts { if p.weights.is_empty() { p.skel_kind = 0; } }
    Ok(session_parts)
}

#[cfg(test)]
#[test]
pub(crate) fn studio_mdk_preview_normalizes_source_rows_once() {
    let mut image = image::RgbaImage::new(1, 2);
    // Unity's first source row is the bottom row. UE's PNG row zero is top.
    image.put_pixel(0, 0, image::Rgba([255, 0, 0, 255]));
    image.put_pixel(0, 1, image::Rgba([0, 0, 255, 255]));
    let mut png = Vec::new();
    image::DynamicImage::ImageRgba8(image).write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png).unwrap();
    for (role, shader) in [("part", "Shader Forge/main_StandardMDK_studio"), ("accessory", "Shader Forge/main_StandardMDK_studio"), ("part", "Standard"), ("part", "Legacy Shaders/Particles/Additive")] {
        let mesh = ExtractedMesh {
            name: "mdk_row_probe".into(),
            positions: vec![[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]],
            normals: vec![[0., 0., 1.]; 3],
            uvs: vec![[0., 0.], [1., 0.], [0., 1.]],
            indices: vec![0, 1, 2],
            mat: Some(crate::unity::MeshMaterial { shader: shader.into(), keywords: vec!["_EMISSION".into()], ..Default::default() }),
            texture_png: Some(png.clone()),
            shader_extra_textures: [("_EmissionMap".into(), png.clone()), ("_OKLightmap".into(), png.clone()), ("_OKLightmapDirection".into(), png.clone())].into(),
            ..Default::default()
        };
        let parts = preview_session_parts(Path::new(""), role, vec![mesh], None).unwrap();
        assert_eq!(parts.len(), 1);
        for bytes in [parts[0].png.as_ref().unwrap(), &parts[0].shader_textures["_EmissionMap"], &parts[0].shader_textures["_OKLightmap"], &parts[0].shader_textures["_OKLightmapDirection"]] {
            let decoded = crate::teximg::load_from_memory(bytes).unwrap().into_rgba8();
            assert_eq!(decoded.get_pixel(0, 0).0, [0, 0, 255, 255], "{role}: source top row");
            assert_eq!(decoded.get_pixel(0, 1).0, [255, 0, 0, 255], "{role}: source bottom row");
        }
    }
}

#[cfg(test)]
#[test]
pub(crate) fn studio_item_preview_normalizes_main_and_mask_rows_once() {
    let mut image = image::RgbaImage::new(1, 2);
    // Unity's first source row is the bottom row. UE's PNG row zero is top.
    image.put_pixel(0, 0, image::Rgba([255, 0, 0, 255]));
    image.put_pixel(0, 1, image::Rgba([0, 0, 255, 255]));
    let mut png = Vec::new();
    image::DynamicImage::ImageRgba8(image).write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png).unwrap();
    for shader in ["Shader Forge/main_item_studio", "Shader Forge/main_item_studio_alpha"] {
        let mesh = ExtractedMesh {
            name: "item_row_probe".into(),
            positions: vec![[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]],
            normals: vec![[0., 0., 1.]; 3],
            uvs: vec![[0., 0.], [1., 0.], [0., 1.]],
            indices: vec![0, 1, 2],
            mat: Some(crate::unity::MeshMaterial { shader: shader.into(), ..Default::default() }),
            texture_png: Some(png.clone()),
            color_mask_png: Some(png.clone()),
            ..Default::default()
        };
        let parts = preview_session_parts(Path::new(""), "part", vec![mesh], None).unwrap();
        let mask = parts[0].item_draw.as_ref().and_then(|d| d.color_mask.as_ref()).expect("item draw colour mask");
        for bytes in [parts[0].png.as_ref().unwrap(), mask] {
            let decoded = crate::teximg::load_from_memory(bytes).unwrap().into_rgba8();
            assert_eq!(decoded.get_pixel(0, 0).0, [0, 0, 255, 255], "{shader}: source top row");
            assert_eq!(decoded.get_pixel(0, 1).0, [255, 0, 0, 255], "{shader}: source bottom row");
        }
    }
}

pub(crate) fn apply_clothes_create_textures(
    meshes: &mut [ExtractedMesh],
    map: Option<&crate::unity::ClothesRendererMap>,
    baked: &[Option<Vec<u8>>; 3],
) {
    let Some(map)=map else{return;};
    if baked[0].is_none(){return;}
    for m in meshes.iter_mut() {
        // `.material` in `ChaControl.ChangeCustomClothes` writes only the
        // renderer's first material. Other submeshes retain their own maps.
        if m.submesh_index != 0 { continue; }
        // Replay writes rather than choosing one group up front: if a later
        // group's RT is absent, Unity retains an earlier successful write.
        for (write,slot) in [0,1,2,0].into_iter().enumerate() {
            let selected=m.renderer_ids.iter().any(|id|match write {
                0=>map.normal01.contains(id)||map.alpha01.contains(id),
                1=>map.normal02.contains(id),2=>map.normal03.contains(id),
                _=>map.accessory.contains(id),
            });
            if selected {if let Some(tex)=baked[slot].as_ref() {
                m.texture_png=Some(tex.clone());mark_runtime_main_texture(m);
            }}
        }
    }
}

/// KCOX ChangeCustomClothes postfix runs after the game's create/emblem writes,
/// before MaterialEditor. Only shared runtime RTs selected by the component
/// receive the layer; it does not replace every material on the clothing mesh.
pub(crate) fn assigned_clothes_slot(ids:&[i64],map:&crate::unity::ClothesRendererMap,baked:&[Option<Vec<u8>>;3])->Option<usize> {
    baked[0].as_ref()?;
    [0,1,2,0].into_iter().enumerate().filter_map(|(write,slot)| {
        (baked[slot].is_some() && ids.iter().any(|id|match write {
            0=>map.normal01.contains(id)||map.alpha01.contains(id),
            1=>map.normal02.contains(id),2=>map.normal03.contains(id),_=>map.accessory.contains(id),
        })).then_some(slot)
    }).last()
}

pub(crate) fn apply_clothes_overlay(card:&CardInfo,outfit:i32,component:&str,meshes:&mut [ExtractedMesh],map:Option<&crate::unity::ClothesRendererMap>,baked:&[Option<Vec<u8>>;3]) {
    let Some((overlay,clear))=card.clothes_overlays.get(&(outfit,component.into())) else{return};
    let Some(map)=map else{eprintln!("ok-extract: KCOX unresolved component renderer map {component}");return};
    apply_clothes_overlay_rt(component,meshes,map,baked,overlay,*clear);
}

pub(crate) fn apply_clothes_overlay_rt(component:&str,meshes:&mut [ExtractedMesh],map:&crate::unity::ClothesRendererMap,baked:&[Option<Vec<u8>>;3],overlay:&[u8],clear:bool) {
    // KCOX selects ordered first renderers, then mutates their current shared
    // RenderTexture. A non-RT stops the loop; aliasing may apply the layer twice.
    for renderer in &map.overlay_renderers {
        let Some(slot)=assigned_clothes_slot(&[*renderer],map,baked) else{break;};
        // A selected renderer may have no exported mesh. Its RT is still shared
        // with other renderers and can be updated through one of those copies.
        let Some(source)=meshes.iter().find(|m|m.submesh_index==0 && assigned_clothes_slot(&m.renderer_ids,map,baked)==Some(slot)) else{break;};
        if !source.runtime_textures.contains_key("_MainTex"){break;}
        let Some(mut base)=source.texture_png.clone() else{break;};
        if clear {
            if let Ok(image)=crate::teximg::load_from_memory(&base) {base=crate::unity::solid_png([0.;4],image.width(),image.height()).unwrap_or(base);}
        }
        if !overlay.is_empty(){base=composite_overlay(&base,overlay).unwrap_or(base);}
        for mesh in meshes.iter_mut().filter(|m|m.submesh_index==0 && assigned_clothes_slot(&m.renderer_ids,map,baked)==Some(slot)) {
            mesh.texture_png=Some(base.clone());
        }
        eprintln!("ok-extract: KCOX albedo component={component} renderer={renderer} slot={slot} override={clear} bytes={}",overlay.len());
    }
}

#[cfg(test)]
#[test]
pub(crate) fn clothes_overlay_follows_live_shared_rt_and_native_stop() {
    let white=crate::unity::solid_png([1.;4],2,2).unwrap();
    let red=crate::unity::solid_png([1.,0.,0.,1.],2,2).unwrap();
    let baked=[Some(white.clone()),Some(white.clone()),None];
    let map=crate::unity::ClothesRendererMap {
        normal01:[10,11].into(),normal02:[20,21].into(),normal03:[10].into(),
        overlay_renderers:vec![10,20],..Default::default()};
    let mut meshes:Vec<_>=[10,11,20,21,99].into_iter().map(|id|ExtractedMesh {
        renderer_ids:vec![id],texture_png:Some(white.clone()),..Default::default()}).collect();
    apply_clothes_create_textures(&mut meshes,Some(&map),&baked);
    apply_clothes_overlay_rt("control",&mut meshes,&map,&baked,&red,false);
    for mesh in &meshes[..4] {assert_eq!(crate::teximg::load_from_memory(mesh.texture_png.as_ref().unwrap()).unwrap().into_rgba8().get_pixel(0,0).0,[255,0,0,255]);}
    assert_eq!(meshes[4].texture_png,Some(white.clone()));
    let mut stopped=meshes.clone();
    for mesh in &mut stopped {mesh.texture_png=Some(white.clone());}
    let map=crate::unity::ClothesRendererMap {overlay_renderers:vec![99,20],..map};
    apply_clothes_overlay_rt("stop",&mut stopped,&map,&baked,&red,true);
    assert!(stopped.iter().all(|m|m.texture_png==Some(white.clone())));
    // The first selected renderer has no mesh export but still owns RT0.
    let map=crate::unity::ClothesRendererMap {normal01:[10,11].into(),overlay_renderers:vec![10],..Default::default()};
    let mut shared=vec![ExtractedMesh {renderer_ids:vec![11],texture_png:Some(white.clone()),..Default::default()}];
    apply_clothes_create_textures(&mut shared,Some(&map),&baked);
    let half=crate::unity::solid_png([1.,0.,0.,0.5],2,2).unwrap();
    apply_clothes_overlay_rt("unexported",&mut shared,&map,&baked,&half,false);
    let once=shared[0].texture_png.clone();
    shared[0].texture_png=Some(white.clone());
    // Both selected renderers reference RT0 in this alias control.
    let map=crate::unity::ClothesRendererMap {overlay_renderers:vec![10,10],..map};
    apply_clothes_overlay_rt("alias",&mut shared,&map,&baked,&half,false);
    assert_ne!(shared[0].texture_png,once,"native repeated shared-RT mutation must not be deduplicated");
}

/// Load a named PNG from an explicit list-entry bundle path (mod zip or
/// `abdata/<ab>`). `ab` is the entry's assetbundle key value.
/// Sideloader.RedirectHook (Sideloader.cs:979): a texture load from bundle
/// `sideloader/mt_eyeline_up_00.unity3d` asset `cw_t_eyeline_up_055` is replaced
/// by `abdata/sideloader/mt_eyeline_up_00/cw_t_eyeline_up_055.png` when that png
/// exists in any zipmod. Upper Eyes ships only those pngs, no unity3d.
pub(crate) fn sideloader_png(zip: Option<&str>, game_root: &Path, ab: &str, names: &[&str]) -> Option<Vec<u8>> {let _spot=crate::profile::spot("sideloader_png");
    let stem = ab
        .trim_start_matches("abdata/")
        .trim_end_matches(".unity3d")
        .trim_matches('/');
    if stem.is_empty() {
        return None;
    }
    for name in names {
        if name.is_empty() || *name == "0" {
            continue;
        }
        let rel = format!("abdata/{stem}/{name}.png");
        if let Some(zip) = zip {
            if let Some(b) = crate::mods::zipmod_file_suffix(Path::new(zip), &rel) {
                return Some(b);
            }
        }
        let disk = game_root.join(&rel);
        if let Ok(b) = crate::gamefs::read(&disk) {
            if b.len() > 16 {
                return Some(b);
            }
        }
    }
    None
}

pub(crate) fn png_from_bundle(
    game_root: &Path,
    ab: &str,
    zip: Option<&str>,
    names: &[&str],
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    // Sideloader png overrides the bundle asset of the same name.
    if let Some(b) = sideloader_png(zip, game_root, ab, names) {
        return Some(b);
    }
    if let Some((_, b)) = pick_named_png(head_texs, names) {
        return Some(b);
    }
    if let Some((_, b)) = pick_named_png(body_texs, names) {
        return Some(b);
    }
    if let Some(zip) = zip {
        if let Some((_,b))=crate::unity::named_png_from_zip(Path::new(zip),ab,names){
            return Some(b);
        }
    }
    png_from_ab(game_root, ab, names, &[])
}

/// Runtime sampling/upload uses PNG row order, like native ReadPixels PNGs.
/// Sideloader images already have that order; decoded Unity Texture2D rows
/// need conversion. Resolve from the selected bundle, not a shared name pool.
pub(crate) fn png_in_runtime_row_order(game: &Path, entry: &ListEntry, ab: &str, names: &[&str]) -> Option<Vec<u8>> {
    runtime_row_source(game,entry,ab,names).map(|(png,flip)|if flip{flip_runtime_blit_v(png)}else{png})
}
pub(crate) fn runtime_row_source(game:&Path,entry:&ListEntry,ab:&str,names:&[&str])->Option<(Vec<u8>,bool)>{
    let zip=entry.extra.get("__zip").map(String::as_str);
    if let Some(png)=sideloader_png(zip,game,ab,names){return Some((png,false));}
    png_from_bundle(game,ab,zip,names,&[],&[]).map(|png|(png,true))
}
/// Keep import metadata attached to the selected pixels. Looking the same
/// texture up again to discover its colour space doubles bundle IO/decoding
/// and can accidentally consult a different source with an identical name.
pub(crate) fn runtime_row_source_with_color_space(game:&Path,entry:&ListEntry,ab:&str,names:&[&str])->Option<(Vec<u8>,bool,Option<i32>)>{
    let profile=std::env::var_os("OK_APPEARANCE_PROFILE").is_some();let started=Instant::now();
    let zip = entry.extra.get("__zip").map(String::as_str);
    // Loose PNGs have no serialized Texture2D import flag. Preserve the
    // previous sRGB default, rather than borrow a bundle's import metadata.
    if let Some(png) = sideloader_png(zip, game, ab, names) { return Some((png,false,None)); }
    if let Some(zip)=zip {
            if let Some((_,png,cs))=crate::unity::named_png_with_color_space_from_zip(Path::new(zip),ab,names){
                if profile{eprintln!("APPEARANCE_PROFILE phase=row_source ab={ab} png_bytes={} total_ms={:.3}",png.len(),started.elapsed().as_secs_f64()*1000.);}
                return Some((png,true,cs));
            }
    }
    if ab.is_empty()||ab=="0"{return None;}
    crate::unity::named_png_with_color_space_from_bundle(&game.join("abdata").join(ab),names)
        .map(|(_,png,cs)|(png,true,cs))
}

pub(crate) fn direct_catalog_png(game: &Path, entry: &ListEntry, names: &[&str]) -> Option<Vec<u8>> {
    let profile=std::env::var_os("OK_APPEARANCE_PROFILE").is_some();
    let started=Instant::now();
    let zip = entry.extra.get("__zip").map(String::as_str);
    let path=game.join("abdata").join(&entry.main_ab);
    let mut source_bytes=0;
    // Zip provenance must use the same archive/entry/mtime identity as other
    // runtime texture paths. A byte-sampled parser key can reuse stale data
    // after a same-size archive edit and needlessly reads an entire warm bundle.
    let bundle = crate::appearance::timings::measure("catalog.bundle",||zip.and_then(|z| crate::unity::parse_bundle_zip_cached(Path::new(z), &entry.main_ab))
        .or_else(||{source_bytes=crate::gamefs::metadata(&path).map_or(0,|m|m.len());crate::unity::parse_bundle_path_cached(&path)}));
    let read_ms=started.elapsed().as_secs_f64()*1000.;
    for name in names {
        if let Some(png) = crate::appearance::timings::measure("catalog.override",||sideloader_png(zip, game, &entry.main_ab, &[name])) { return Some(png); }
        if let Some(png) = crate::appearance::timings::measure("catalog.texture",||bundle.as_ref().and_then(|b| b.exact_texture_png(name))) {
            let lookup_ms=started.elapsed().as_secs_f64()*1000.-read_ms;
            let png=crate::appearance::timings::measure("catalog.flip",||flip_runtime_blit_v(png));
            if profile{eprintln!("APPEARANCE_PROFILE phase=direct_catalog ab={} name={name} bundle_bytes={source_bytes} read_ms={read_ms:.3} lookup_ms={lookup_ms:.3} total_ms={:.3}",entry.main_ab,started.elapsed().as_secs_f64()*1000.);}
            return Some(png);
        }
    }
    None
}

pub(crate) fn eye_gradient_png(game: &Path, vanilla: &ChaListCatalog, mods: &ChaListCatalog,
    card: &CardInfo, guid_index: &std::collections::HashMap<String,PathBuf>, id: i32, right:bool) -> Option<Vec<u8>> {
    if id == 0 { return None; }
    let property=if right{"ChaFileFace.PupilGradient2"}else{"ChaFileFace.PupilGradient1"};
    let entry = face_detail_entry_for_property(vanilla, mods, card, guid_index, 409, id,Some(property))?;
    let name = entry.info("ColorMaskTex");
    let ab = entry.info("ColorMaskAB");
    png_in_runtime_row_order(game, &entry, if ab.is_empty() || ab=="0" { &entry.main_ab } else { &ab }, &[&name])
}

/// Load a catalog texture by (category, id, list key). Mods win over vanilla,
/// but a mod entry whose asset cannot be resolved falls back to vanilla (some
/// mods override these lists with incompatible bundles).
pub(crate) fn load_catalog_png(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    cat: i32,
    id: i32,
    key: &str,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {let _spot=crate::profile::spot("load_catalog_png");
    load_catalog_png_ab(
        game_root,
        vanilla,
        mod_catalog,
        cat,
        id,
        key,
        "",
        head_texs,
        body_texs,
    )
}

/// Like `load_catalog_png`, but the bundle is read from `ab_key` (e.g. the
/// eye gradation mask uses `ColorMaskAB` while its `MainAB` is empty).
#[allow(clippy::too_many_arguments)]
pub(crate) fn load_catalog_png_ab(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    cat: i32,
    id: i32,
    tex_key: &str,
    ab_key: &str,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    if id == 0 {
        return None;
    }
    for e in [mod_catalog.get(cat, id), vanilla.get(cat, id)]
        .into_iter()
        .flatten()
    {
        let mut names: Vec<String> = Vec::new();
        let t = e.info(tex_key);
        if !t.is_empty() && t != "0" {
            names.push(t);
        }
        if !e.main_tex.is_empty() && e.main_tex != "0" {
            names.push(e.main_tex.clone());
        }
        if names.is_empty() {
            continue;
        }
        let ab = if ab_key.is_empty() {
            e.main_ab.clone()
        } else {
            e.info(ab_key)
        };
        let refs: Vec<&str> = names.iter().map(|s| s.as_str()).collect();
        if let Some(png) = png_from_bundle(
            game_root,
            &ab,
            e.extra.get("__zip").map(|s| s.as_str()),
            &refs,
            head_texs,
            body_texs,
        ) {
            return Some(png);
        }
    }
    None
}

/// Like `load_catalog_png`, but allows list id 0. Several categories use id 0
/// as the *default* row (e.g. `mt_lip`: `ChangeSettingLip` calls
/// `ChangeTexture(..., lipId=0, ..., _overtex1)`), which `load_catalog_png`
/// rejects.
pub(crate) fn load_catalog_png_allow0(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    cat: i32,
    id: i32,
    tex_key: &str,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    for e in [mod_catalog.get(cat, id), vanilla.get(cat, id)]
        .into_iter()
        .flatten()
    {
        let mut names: Vec<String> = Vec::new();
        let t = e.info(tex_key);
        if !t.is_empty() && t != "0" {
            names.push(t);
        }
        if !e.main_tex.is_empty() && e.main_tex != "0" {
            names.push(e.main_tex.clone());
        }
        if names.is_empty() {
            continue;
        }
        let refs: Vec<&str> = names.iter().map(|s| s.as_str()).collect();
        if let Some(png) = png_from_bundle(
            game_root,
            &e.main_ab,
            e.extra.get("__zip").map(|s| s.as_str()),
            &refs,
            head_texs,
            body_texs,
        ) {
            return Some(png);
        }
    }
    None
}

/// Direct display textures retain the selected asset's sampler and convert
/// authored Texture2D rows to the PNG upload convention. Unlike a create-pass
/// input, these bytes are sampled directly by the runtime material.
pub(crate) fn load_catalog_display_texture(
    game: &Path, vanilla: &ChaListCatalog, mods: &ChaListCatalog,
    category: i32, id: i32, key: &str,
) -> Option<(Vec<u8>, Option<[i32; 5]>)> {
    let entry=mods.get(category,id).or_else(||vanilla.get(category,id))?;
    let name=entry.info(key);
    // ChangeTexture(null) is authoritative. A sibling/MainTex/vanilla row
    // must not turn a deliberately empty selected property into a texture.
    if name.is_empty() || name=="0" {return None;}
    let asset=if character_hi_poly() {name} else {format!("{name}_low")};
    let png=png_in_runtime_row_order(game,entry,&entry.main_ab,&[&asset])?;
    let sampling=named_texture_sampling(game,entry,&entry.main_ab,&[&asset])
        .map(|s|[s.filter,s.wrap_u,s.wrap_v,i32::from(s.srgb),s.mip_count]);
    Some((png,sampling))
}

pub(crate) fn assign_display_texture_settings(part: &mut crate::session::SessionPart, slot: &str, settings: Option<[i32; 5]>) {
    if let Some(settings) = settings { part.runtime_textures.insert(slot.into(), settings); }
    else { part.runtime_textures.remove(slot); }
}
