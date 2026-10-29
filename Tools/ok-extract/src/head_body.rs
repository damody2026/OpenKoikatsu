//! Head and body parts of the open: head draw materials, head detail textures, the body texture composite and uncensor genitals.
use super::*;

pub(crate) fn head_uses_item_draw(mesh: &ExtractedMesh) -> bool {
    mesh.mat.as_ref().is_some_and(|m|
        m.shader.eq_ignore_ascii_case("Koikano/main_clothes_item")
        || m.shader.eq_ignore_ascii_case("Shader Forge/main_item"))
}

pub(crate) fn head_uses_eye_draw(mesh:&ExtractedMesh)->bool {
    mesh.mat.as_ref().is_some_and(|m| m.shader.eq_ignore_ascii_case("Koikano/main_eye")
        || m.shader.eq_ignore_ascii_case("Shader Forge/toon_eye_lod0")
        || (m.shader.is_empty() && m.float("_KanoVerShift").is_some()
            && m.float("_rotation1").is_some() && m.slot("_overtex1").is_some()))
}

pub(crate) fn source_eye_draw(game:&Path,vanilla:&ChaListCatalog,mods:&ChaListCatalog,card:&CardInfo,
    raw:&ExtractedMesh,guid_index:&std::collections::HashMap<String,PathBuf>)
    ->Option<crate::eye_gaze::EyeDraw> {
    let mut mesh=raw.clone();
    apply_me_shader(game,Some(&card.me_shader),0,"head",None,&mut mesh);
    apply_me_floats(&card.me_float,0,"head",None,&mut mesh);
    if !head_uses_eye_draw(&mesh) {return None;}
    let mat=mesh.mat.as_ref()?;
    let kind=if mat.shader.eq_ignore_ascii_case("Shader Forge/toon_eye_lod0") {2}else{1};
    let lerp=|a:f32,b:f32,t:f32|a+(b-a)*t.clamp(0.,1.);
    let color=|key:&str,default:[f32;4]|me_material_color(Some(&card.me_color),0,"head",None,&mesh,key)
        .unwrap_or(default);
    let textures=std::array::from_fn(|i| {
        let prop=if i==0 {"overtex1"} else {"overtex2"};
        me_texture(Some(&card.me_tex),0,"head",None,&mesh,prop).map(|p|p.to_vec()).or_else(|| {
            let (cat,id,key)=if i==0 {(410,card.hl_up_id,"EyeHiUpTex")} else {(411,card.hl_down_id,"EyeHiDownTex")};
            // UniversalAutoResolver pins each saved local slot to its GUID.
            // A merged mod catalog may contain the same id as a vanilla row.
            // Reuse the source face resolver and load from that entry's bundle;
            // unrelated head/body texture pools cannot supply this asset.
            let entry=face_detail_entry(vanilla,mods,card,guid_index,cat,id)?;
            let name=entry.info(key);
            if name.is_empty() || name=="0" {return None;}
            png_from_list_entry(game,&entry,&[&name],&[],&[]).as_deref().and_then(flip_png_v)
        })
    });
    let right=raw.name.to_ascii_lowercase().contains("_r");
    let rotation=lerp(0.02,-0.02,card.shape_face.get(33).copied().unwrap_or(0.5))*if right {-1.}else{1.};
    // ChangeSettingEyeTilt precedes MaterialEditor. Preserve a ME float when present.
    let name=crate::card::me_mat_key(&mat.name);
    let me_rotation=card.me_float.iter().find(|((obj,coord,slot,m,p),_)|
        *obj==4 && *coord==0 && *slot==0 && *m==name && p.trim_start_matches('_').eq_ignore_ascii_case("rotation"))
        .map(|(_,v)|*v).unwrap_or(rotation);
    Some(crate::eye_gaze::EyeDraw {
        kind,
        textures,colors:[color("overcolor1",card.hl_up_color),color("overcolor2",card.hl_down_color)],
        offsets:[[lerp(0.2,-0.2,card.hl_up_x),lerp(0.1,-0.1,card.hl_up_y)],
            [lerp(0.2,-0.2,card.hl_down_x),lerp(0.1,-0.1,card.hl_down_y)]],
        rotations:[me_rotation,mat.float("_rotation1").unwrap_or(0.5),mat.float("_rotation2").unwrap_or(0.5)],
        color:if kind==1 {color("color",mat.color("_Color").unwrap_or([1.;4]))}else{[1.;4]},
        enabled:mat.float("_isHighLight").unwrap_or(1.),
        raw_uvs:std::array::from_fn(|i| {let uv=if i==0 {&raw.uv2}else{&raw.uv3};
            if uv.len()==raw.positions.len() {uv.clone()} else {vec![[0.;2];raw.positions.len()]}}),
        texcoord_st:std::array::from_fn(|i| std::array::from_fn(|j| (kind==1).then(||mat.float(&format!("_texcoord{}_ST_{}",i+2,["x","y","z","w"][j]))).flatten()
            .unwrap_or([1.,1.,0.,0.][j]))),
        main_texcoord_st:std::array::from_fn(|j|mat.float(&format!("_texcoord_ST_{}",["x","y","z","w"][j])).unwrap_or([1.,1.,0.,0.][j])),
        sample_uvs:Vec::new(),
    })
}

/// Material identity also covers mod heads with renamed mouth submeshes.
pub(crate) fn apply_head_draw_material_overrides(card: &CardInfo, mesh: &mut ExtractedMesh) {
    if !head_uses_item_draw(mesh) && !mesh.mat.as_ref().is_some_and(|m|m.shader.eq_ignore_ascii_case("xukmi/MainItemPlus")) { return; }
    let native_item=mesh.mat.as_ref().is_some_and(|m|m.shader.eq_ignore_ascii_case("xukmi/MainItemPlus"));
    let Some(name) = mesh.mat.as_ref().map(|m| crate::card::me_mat_key(&m.name)) else { return; };
    for ((object, coord, slot, material, prop), value) in &card.me_color {
        if *object == 4 && *coord == 0 && *slot == 0 && *material == name {
            let key = if prop.starts_with('_') { prop.clone() } else { format!("_{prop}") };
            set_material_color(mesh, &key, *value);
        }
    }
    if let Some(png) = me_texture(Some(&card.me_tex), 0, "head", None, mesh, "maintex") {
        mesh.texture_png = Some(png.to_vec());
        if native_item {mark_material_editor_texture(mesh,"_MainTex");}
    }
    // Bundle masks need row canonicalization; ME PNGs already use runtime rows.
    let me_color_mask = me_texture(Some(&card.me_tex), 0, "head", None, mesh, "colormask")
        .map(|png| png.to_vec());
    if me_color_mask.is_some() {
        if native_item {mark_material_editor_texture(mesh,"_ColorMask");}
        else {mesh.runtime_textures.remove("_ColorMask");}
    }
    mesh.color_mask_png = me_color_mask.or_else(|| mesh.color_mask_png.take().map(orient_raw_mask));
    for prop in ["detailmask", "linemask", "normalmap", "normalmask", "alphamask"] {
        if let Some(png) = me_texture(Some(&card.me_tex), 0, "head", None, mesh, prop).map(|p| p.to_vec()) {
            match prop {
                "detailmask" => {
                    mesh.detail_mask_png = Some(png);
                    if native_item {mark_material_editor_texture(mesh,"_DetailMask");}
                    else {mesh.runtime_textures.remove("_DetailMask");}
                },
                "linemask" => {mesh.line_mask_png = Some(png);
                    if native_item {mark_material_editor_texture(mesh,"_LineMask");}},
                "normalmap" => {mesh.normal_map_png = Some(png);
                    if native_item {mark_material_editor_texture(mesh,"_NormalMap");}},
                "alphamask" => if native_item {mesh.alpha_mask_png=Some(png);mark_material_editor_texture(mesh,"_AlphaMask");},
                _ => mesh.normal_mask_png = Some(png),
            }
        }
    }
}

/// Unity renders slot1 of rendEyelineUp on the same last submesh as slot0.
/// Keep its independent material identity and discard the stroke RT binding.
pub(crate) fn eyeline_shadow_mesh(source:&crate::unity::ParsedBundle,stroke:&ExtractedMesh)->ExtractedMesh {
    let mut shadow=stroke.clone();shadow.name=format!("{}_kage",stroke.name);
    shadow.mat=source.renderer_material_slot(&stroke.renderer_ids,1)
        .or_else(||source.material_named("cf_m_eyeline_kage"));
    if shadow.mat.is_none() {
        eprintln!("ok-extract: eyeline shadow material slot1 missing renderer={:?}",stroke.renderer_ids);
        shadow.mat=stroke.mat.clone().map(|mut m| {m.name="cf_m_eyeline_kage".into();m});
    }
    shadow.texture_png=None;shadow.runtime_textures.remove("_MainTex");shadow
}

pub(crate) fn head_detail_source_spec(card:&CardInfo,ln:&str)->Option<(i32,i32,Vec<&'static str>,[f32;4])> {
    Some(if ln.contains("mayuge") {
        (406,card.eyebrow_id,vec!["EyebrowTex"],card.eyebrow_color)
    } else if ln.contains("eyeline") && ln.contains("low") {
        (413,card.eyeline_down_id,vec!["EyelineDownTex"],card.eyeline_color)
    } else if ln.contains("eyeline") && ln.ends_with("_kage") {
        (412,card.eyeline_up_id,vec!["EyelineShadowTex"],card.skin_color)
    } else if ln.contains("eyeline") {
        (412,card.eyeline_up_id,vec!["EyelineUpTex"],card.eyeline_color)
    } else if ln.contains("nose") {
        (414,card.nose_id,vec!["NoseTex"],[1.;4])
    } else if ln.contains("hitomi") && ln.contains("02") {
        let c=mix4(card.eye_color,card.eye_sub_color,0.65);
        (408,card.pupil_ids[usize::from(ln.contains("_r"))],vec!["EyeTex"],lin_to_srgb(c))
    } else if ln.contains("hitomi") {
        (407,card.white_id,vec!["EyeWhiteTex"],card.white_base_color)
    } else {return None;})
}

pub(crate) fn head_detail_original_wrap(game:&Path,vanilla:&ChaListCatalog,mods:&ChaListCatalog,
    card:&CardInfo,guid_index:&std::collections::HashMap<String,PathBuf>,ln:&str)->Option<i32> {
    let (cat,id,keys,_)=head_detail_source_spec(card,ln)?;
    if cat==407 || cat==408 {return Some(1);} // CreateEyeTexture installed its RT.
    let entry=face_detail_entry_for_property(vanilla,mods,card,guid_index,cat,id,None)?;
    let mut names=vec![];
    for key in keys {
        let base=entry.info(key);
        if !base.is_empty() && base!="0" {names.extend(detail_asset_names(&base,ln.contains("eyeline"),card.head_id));}
    }
    if !entry.main_tex.is_empty() && entry.main_tex!="0" {names.extend(detail_asset_names(&entry.main_tex,ln.contains("eyeline"),card.head_id));}
    if names.is_empty() || entry.main_ab.is_empty() || entry.main_ab=="0" {return Some(0);} // ChangeTexture(null).
    let refs:Vec<&str>=names.iter().map(String::as_str).collect();
    named_texture_sampling(game,&entry,&entry.main_ab,&refs).map(|s|s.wrap_u)
}

#[cfg(test)]
#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
pub(crate) fn runtime_texture_catalog_wrap_matches_native() {
    let game=&crate::test_support::game_path("");
    let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_346095.png")).unwrap();
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let wanted=card.resolves.iter().map(|r|r.mod_id.clone()).collect();
    let guids=crate::mods::find_guids(&game.join("mods"),Some(&wanted));
    // Independent native R6 rows: catalog眉毛/眼線 Clamp; create eye RT Clamp.
    for name in ["cf_o_mayuge","cf_o_eyeline","cf_ohitomi_l02"] {
        assert_eq!(head_detail_original_wrap(game,&vanilla,&mods,&card,&guids,name),Some(1),"{name}");
    }
    // Tongue is not a catalog replacement; MaterialAPI keeps its asset's wrap.
    assert_eq!(head_detail_original_wrap(game,&vanilla,&mods,&card,&guids,"o_tang"),None);
}

pub(crate) fn resolve_head_detail_texture(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String, PathBuf>,
    outfit: i32,
    mesh: &mut ExtractedMesh,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) {
    // Native stderr can synchronously stall a redirected pipe/file. Runtime
    // selection timings are returned as structured data; per-asset text is
    // opt-in diagnostics and must not block ordinary editing.
    let diagnostics=std::env::var_os("OK_DIAG").is_some()||std::env::var_os("OK_APPEARANCE_PROFILE").is_some();
    let ln = mesh.name.to_ascii_lowercase();
    // Draw materials are not create_* RenderTextures. Replay MaterialEditor
    // before deciding the shader, retaining the source material and textures.
    apply_me_shader(game_root, Some(&card.me_shader), 0, "head", None, mesh);
    apply_me_floats(&card.me_float, 0, "head", None, mesh);
    apply_head_draw_material_overrides(card, mesh);
    if mesh.mat.as_ref().is_some_and(|m|m.shader.eq_ignore_ascii_case("xukmi/MainItemPlus")) {
        // Head draw materials can also be replaced by the installed native
        // item program. Keep its raw albedo and use the same runtime normal
        // encoding/row boundary as native accessories, without CPU tinting.
        let convert_normals=material_editor_converts_normals(game_root);
        if convert_normals {
            if let Some(png)=mesh.normal_map_png.take() {
                let png=convert_runtime_normal_map(png);
                mark_native_runtime_normal_texture(mesh,"_NormalMap");
                mesh.normal_map_png=Some(linear_texture_to_srgb_png(&png).unwrap_or(png));
            }
        }
        if me_maintex(Some(&card.me_tex),0,"head",None,mesh).is_none() {
            mesh.texture_png=mesh.texture_png.take().map(|png|flip_png_v(&png).unwrap_or(png));
        }
        if mesh.texture_png.is_none() {mesh.texture_png=crate::unity::solid_png([1.;4],8,8);}
        for name in ["_NormalMapDetail","_AnotherRamp","_EmissionMask","_ReflectionMapCap","_ReflectMapDetail","_Texture2","_Texture3","_liquidmask"] {
            let key=name.trim_start_matches('_').to_ascii_lowercase();
            let replacement=me_texture(Some(&card.me_tex),0,"head",None,mesh,&key).map(|png|png.to_vec());
            let replaced=replacement.is_some();
            if let Some(png)=replacement.or_else(||mesh.shader_extra_textures.remove(name)) {
                if replaced {mark_material_editor_texture(mesh,name);}
                let png=if name=="_NormalMapDetail"&&convert_normals {
                    mark_native_runtime_normal_texture(mesh,name);
                    let converted=convert_runtime_normal_map(png);
                    linear_texture_to_srgb_png(&converted).unwrap_or(converted)
                } else if !replaced {flip_png_v(&png).unwrap_or(png)} else {png};
                mesh.shader_extra_textures.insert(name.into(),png);
            }
        }
        return;
    }
    if head_uses_item_draw(mesh) {
        // MaterialEditor also converts head normals to a runtime RT. Share
        // the same verified blit path as body/clothes/accessories.
        if material_editor_converts_normals(game_root) {
            mesh.normal_map_png = mesh.normal_map_png.take().map(convert_runtime_normal_map);
        }
        // The shader defaults to white MainTex. UE evaluates all four draw
        // colours; a null tongue texture must not become a guessed pink.
        if mesh.texture_png.is_none() {
            mesh.texture_png = crate::unity::solid_png([1.0; 4], 8, 8);
        }
        return;
    }
    // Material Editor replaces _MainTex, not the draw material's _Color.
    // ChaControl.ChangeSettingEyebrowColor / ChangeSettingEyelineColor set
    // _Color separately, and ME can override that property again. The dump
    // RenderTexture therefore matches this PNG before the display tint.
    if let Some(png) = me_maintex(Some(&card.me_tex), 0, "head", None, mesh) {
        let base_color = if ln.contains("mayuge") {
            Some(card.eyebrow_color)
        } else if ln.contains("eyeline") && ln.ends_with("_kage") {
            Some(card.skin_color)
        } else if ln.contains("eyeline") {
            Some(card.eyeline_color)
        } else {
            None
        };
        let draw_color = me_material_color(
            Some(&card.me_color), 0, "head", None, mesh, "color"
        ).or(base_color);
        if diagnostics { crate::appearance::timings::measure("detail.log",||eprintln!(
            "ok-extract: ME MainTex role=head name={} mat={} bytes={} draw_color={draw_color:?}",
            mesh.name,
            mesh.mat.as_ref().map(|m| m.name.as_str()).unwrap_or(""),
            png.len()
        )); }
        mesh.texture_png = Some(draw_color
            .and_then(|color| crate::unity::tint_png(png, color))
            .unwrap_or_else(|| png.to_vec()));
        // CreateEyeTexture/CreateEyeWhiteTexture have installed their RT on
        // these same targets before ME runs, even when the CPU bake is skipped.
        if ln.contains("hitomi") { mark_runtime_main_texture(mesh); }
        if let Some(wrap)=head_detail_original_wrap(game_root,vanilla,mod_catalog,card,guid_index,&ln) {
            // ChaControl's catalog ChangeTexture precedes MaterialEditor;
            // the prefab's texture is no longer the replaced source.
            mesh.runtime_textures.insert("_MainTex".into(),[1,wrap,wrap,1,0]);
        }
        mark_material_editor_main_texture(mesh);
        return;
    }
    // ChaListDefine.CategoryNo values from ChaListDefine.cs.
    let Some((cat,id,keys,tint))=head_detail_source_spec(card,&ln) else {return;};
    let pupil_property=if ln.contains("_r") {"ChaFileFace.Pupil2"}else{"ChaFileFace.Pupil1"};
    let Some(entry) = face_detail_entry_for_property(vanilla, mod_catalog, card, guid_index, cat, id,
        (cat==408).then_some(pupil_property)) else {
        return;
    };
    // The list row stores texture *keys* (EyebrowTex...) whose values are the
    // asset names inside main_ab. Eyeline also tries `_{headId}` (ChaControl.cs:4043).
    let bases: Vec<String> = keys
        .iter()
        .map(|k| entry.info(k))
        .filter(|s| !s.is_empty() && s != "0")
        .collect();
    let mut names: Vec<String> = Vec::new();
    let head_suffix = ln.contains("eyeline");
    for b in &bases {
        names.extend(detail_asset_names(b, head_suffix, card.head_id));
    }
    if !entry.main_tex.is_empty() && entry.main_tex != "0" {
        names.extend(detail_asset_names(&entry.main_tex, head_suffix, card.head_id));
    }
    if (entry.main_ab.is_empty() || entry.main_ab=="0" || names.is_empty()) && cat==414 {
        // ChangeTexture assigns null for the list's explicit zero selection.
        // main_nose / toon_nose_lod0 default MainTex is black; its coverage
        // is zero. Keep that result instead of the prefab's grey placeholder.
        mesh.texture_png=crate::unity::solid_png([0.;4],8,8);
        return;
    }
    if names.is_empty() {
        return;
    }
    let name_refs: Vec<&str> = names.iter().map(|s| s.as_str()).collect();
    let direct = ln.contains("mayuge") || ln.contains("eyebrow") || ln.contains("eyeline");
    let texture = if ln.contains("hitomi") {
            // Compose directly from decoded source rows. Re-encoding a 4K/8K
            // flipped PNG before writing a 512 RT duplicates the costly work.
            runtime_row_source_with_color_space(game_root,&entry,&entry.main_ab,&name_refs)
                .or_else(||png_from_list_entry(game_root,&entry,&name_refs,head_texs,body_texs).map(|png|{
                    let cs=face_base_color_space(game_root,Some(&entry),bases.first().unwrap_or(&entry.main_tex),&png);
                    (png,false,cs)
                }))
    } else {
        let png=if direct{direct_catalog_png(game_root,&entry,&name_refs)}
            else{png_from_list_entry(game_root,&entry,&name_refs,head_texs,body_texs)};
        png.map(|png|(if ln.contains("hitomi"){
            png_in_runtime_row_order(game_root,&entry,&entry.main_ab,&name_refs).unwrap_or(png)
        }else{png},false,None))
    };
    if let Some((png,source_flip,source_color_space)) = texture {
        if ln.contains("nose")
            || ln.contains("eyeline")
            || ln.contains("mayuge")
            || ln.contains("eyebrow")
        {
            // ChangeSettingEyebrow directly assigns the catalog Texture2D.
            // Decoding retains Unity's bottom row first; UE mesh coordinates
            // store 1-v, so png_in_runtime_row_order reversed those rows above.
            // Sideloader PNG overrides already use upload row order and remain
            // unchanged in that helper. MaterialEditor PNG replacements returned
            // above. Native GPU coverage verifies all three source paths; PNG
            // readback alone cannot identify the original texture provenance.
            let tinted = crate::appearance::timings::measure("detail.tint",||crate::unity::tint_png(&png, tint)).unwrap_or(png);
            let flip = if direct {
                false
            } else {
                true
            };
            mesh.texture_png = if flip {
                flip_png_v(&tinted).or(Some(tinted))
            } else {
                Some(tinted)
            };
        } else if ln.contains("hitomi") && !ln.contains("02") {
            // `ctCreateEyeW` is `cf_m_eyewhite_create` and `RebuildTextureAndSetMaterial`
            // Blits into the RT (ChaControl.cs:10758 / CustomTextureCreate.cs:186).
            // The source `cw_t_sirome_kage_*` keeps the white oval at the top; the
            // RT the sclera mesh samples has it at the bottom. Same V flip as face.
            let source_linear=source_color_space==Some(0);
            let bake_start=Instant::now();
            let baked = bake_eye_white_create_rt(&png, card.white_base_color, card.white_sub_color, source_linear,source_flip)
                .or_else(|| white_on_black_alpha(&png, tint));
            if std::env::var_os("OK_APPEARANCE_PROFILE").is_some(){eprintln!("APPEARANCE_PROFILE phase=eye_white_bake part={} png_bytes={} source_size={:?} ms={:.3}",mesh.name,png.len(),png.get(16..24).map(|b|(u32::from_be_bytes(b[..4].try_into().unwrap()),u32::from_be_bytes(b[4..].try_into().unwrap()))),bake_start.elapsed().as_secs_f64()*1000.);}
            mesh.texture_png = baked;
            if mesh.texture_png.is_some() { mark_runtime_main_texture(mesh); }
        } else if ln.contains("hitomi") {
            let iris_started=Instant::now();
            let right = ln.contains("_r");
            let pi = card.pupils[usize::from(right)].clone();
            let grad = eye_gradient_png(game_root, vanilla, mod_catalog, card, guid_index, pi.grad_mask_id,right);
            let hl_up = if card.hl_up_id != 0 {
                load_catalog_png(
                    game_root,
                    vanilla,
                    mod_catalog,
                    410,
                    card.hl_up_id,
                    "EyeHiUpTex",
                    head_texs,
                    body_texs,
                )
            } else {
                None
            };
            let hl_down = if card.hl_down_id != 0 {
                load_catalog_png(
                    game_root,
                    vanilla,
                    mod_catalog,
                    411,
                    card.hl_down_id,
                    "EyeHiDownTex",
                    head_texs,
                    body_texs,
                )
            } else {
                None
            };
            if std::env::var("OK_EYEDUMP").is_ok() {
                for (n, b) in [
                    ("iris", Some(&png)),
                    ("grad", grad.as_ref()),
                    ("hlup", hl_up.as_ref()),
                    ("hldown", hl_down.as_ref()),
                ] {
                    if let Some(bytes) = b {
                        let _ = std::fs::write(format!("eyedump_{n}.png"), bytes);
                    }
                }
            }
            if std::env::var("OK_EYEDIAG").is_ok() {
                eprintln!(
                    "ok-extract: EYEDIAG {} hl_up_id={} up={} hl_down_id={} down={} grad={} grad_id={} pupilWHXY=({:.2},{:.2},{:.2},{:.2})",
                    mesh.name,
                    card.hl_up_id,
                    hl_up.as_ref().map(|b| b.len()).unwrap_or(0),
                    card.hl_down_id,
                    hl_down.as_ref().map(|b| b.len()).unwrap_or(0),
                    grad.as_ref().map(|b| b.len()).unwrap_or(0),
                    pi.grad_mask_id,
                    card.pupil_width,
                    card.pupil_height,
                    card.pupil_x,
                    card.pupil_y
                );
                for (cat, id, key) in [
                    (410, card.hl_up_id, "EyeHiUpTex"),
                    (411, card.hl_down_id, "EyeHiDownTex"),
                    (409, pi.grad_mask_id, "ColorMaskTex"),
                ] {
                    let v = vanilla.get(cat, id);
                    let vein = v
                        .map(|e| (e.main_ab.clone(), e.info(key), e.main_tex.clone()));
                    let m = mod_catalog.get(cat, id);
                    let mein = m
                        .map(|e| (e.main_ab.clone(), e.info(key), e.main_tex.clone()));
                    eprintln!(
                        "ok-extract: EYECAT cat={cat} id={id} key={key} vanilla={vein:?} mod={mein:?}"
                    );
                }
            }
            if std::env::var_os("OK_APPEARANCE_PROFILE").is_some(){eprintln!("APPEARANCE_PROFILE phase=iris_inputs part={} ms={:.3}",mesh.name,iris_started.elapsed().as_secs_f64()*1000.);}
            let color_started=Instant::now();
            // Carry the selected Texture2D import flag with its bytes. The iris
            // rows below are flipped for the create RT, so a second byte-identity
            // lookup after that flip cannot recover the source colour space.
            let mut source_linear=source_color_space==Some(0);
            let png=if source_flip {flip_runtime_blit_v(png)} else {png};
            if std::env::var_os("OK_APPEARANCE_PROFILE").is_some(){eprintln!("APPEARANCE_PROFILE phase=iris_color_space part={} ms={:.3}",mesh.name,color_started.elapsed().as_secs_f64()*1000.);}
            let png=if let Some(overlay)=ksox_overlay_png(card,outfit,if right {9}else{7}) {
                let encoded=if source_linear {linear_texture_to_srgb_png(&png).unwrap_or(png)}else{png};
                source_linear=false;composite_overlay(&encoded,&overlay).unwrap_or(encoded)
            }else{png};
            let bake_started=Instant::now();
            mesh.texture_png = bake_iris_with_space(
                &png,
                &pi,
                grad.as_deref(),
                if head_uses_eye_draw(mesh) {None}else{hl_up.as_deref()},
                if head_uses_eye_draw(mesh) {None}else{hl_down.as_deref()},
                card.hl_up_color,
                card.hl_down_color,
                card.hl_up_y,
                card.hl_down_y,
                source_linear,
            )
            .or_else(|| rgb_on_black_alpha(&png));
            if std::env::var_os("OK_APPEARANCE_PROFILE").is_some(){eprintln!("APPEARANCE_PROFILE phase=iris_bake part={} ms={:.3}",mesh.name,bake_started.elapsed().as_secs_f64()*1000.);}
            if let Some(base)=mesh.texture_png.take() {
                mesh.texture_png=Some(ksox_overlay_png(card,outfit,if right {10}else{8})
                    .and_then(|overlay|composite_overlay(&base,&overlay)).unwrap_or(base));
                mark_runtime_main_texture(mesh);
            }
            let (scale, offset) = iris_uv_transform(
                card.pupil_width,
                card.pupil_height,
                card.pupil_x,
                card.pupil_y,
                right,
            );
            if std::env::var("OK_DIAG").is_ok() && !mesh.uvs.is_empty() {
                let (mut u0, mut u1, mut v0, mut v1) = (f32::MAX, f32::MIN, f32::MAX, f32::MIN);
                for uv in &mesh.uvs {
                    u0 = u0.min(uv[0]);
                    u1 = u1.max(uv[0]);
                    v0 = v0.min(uv[1]);
                    v1 = v1.max(uv[1]);
                }
                eprintln!(
                    "ok-extract: iris rawUV {} u[{u0:.4},{u1:.4}] v[{v0:.4},{v1:.4}]",
                    mesh.name
                );
            }
            apply_uv_st(&mut mesh.uvs, scale, offset);
            if diagnostics { crate::appearance::timings::measure("detail.log",||eprintln!(
                "ok-extract: iris UV {} scale={scale:?} offset={offset:?} pupil=({:.3},{:.3},{:.3},{:.3})",
                mesh.name, card.pupil_width, card.pupil_height, card.pupil_x, card.pupil_y
            )); }
        } else {
            mesh.texture_png = crate::unity::tint_png(&png, tint);
        }
        mesh.texture_name = entry.main_tex.clone();
        if diagnostics { crate::appearance::timings::measure("detail.log",||eprintln!(
            "ok-extract: face tex {} <- cat={cat} id={} ab={} asset={:?}",
            mesh.name, entry.id, entry.main_ab, names
        )); }
    }
}

pub(crate) fn compose_body_texture(
    mesh: &mut ExtractedMesh,
    card: Option<&CardInfo>,
    skin: [f32; 4],
    sub: [f32; 4],
    body_texs: &[(String, Vec<u8>)],
    outfit: i32,
) {let _spot=crate::profile::spot("compose_body_texture");
    // The same composite for the same inputs in every open of a view (crate::view_memo): the body mesh as it comes in
    // (its content, not only its outfit: a garment state may change it), the outfit, the colours and the textures.
    if !crate::okstrict::assembling() { return; }
    // The composite reads the card (its digest), the outfit (only for its KSOX body overlay), the colours, the body
    // textures and, only when those lack cf_body_00_t, the mesh's own texture; it writes the texture, its name and the
    // _MainTex runtime flag. Hashing the whole mesh for the key cost ~20 ms an open.
    // The key hashes the overlay and the body textures instead of naming the outfit: the outfits of a card mostly share
    // both, and each composited them again (~350 ms an outfit). Two CRC-32s (each half on its own, with the names and
    // lengths) hash them in a few ms.
    let key = {
        let mut h = [crc32fast::Hasher::new(), crc32fast::Hasher::new()];
        let mut add = |bytes: &[u8]| { let (a, b) = bytes.split_at(bytes.len() / 2); h[0].update(a); h[1].update(b); };
        for (name, bytes) in body_texs {
            add(name.as_bytes()); add(&(bytes.len() as u64).to_le_bytes());
            add(bytes);
        }
        if pick_named_png(body_texs, &["cf_body_00_t"]).is_none() { add(mesh.texture_png.as_deref().unwrap_or_default()); }
        let overlay = card.and_then(|card| card.ksox.get(&(crate::clothes_texture::ksox_overlay_coordinate(card, outfit, 3)?, 3)));
        add(b"overlay"); add(&(overlay.map_or(u64::MAX, |o| o.len() as u64)).to_le_bytes());
        add(overlay.map_or(&[][..], |o| o.as_slice()));
        let [a, b] = h;
        let h = ((a.finalize() as u64) << 32) | b.finalize() as u64;
        match card {
            Some(card) => format!("compose_body_texture|{:016x}|{skin:?}|{sub:?}|{h:016x}", card.source_digest),
            None => format!("compose_body_texture|-|{skin:?}|{sub:?}|{h:016x}"),
        }
    };
    let made = crate::view_memo(key, || {
        let mut made = ExtractedMesh { texture_png: mesh.texture_png.clone(), ..Default::default() };
        compose_body_texture_uncached(&mut made, card, skin, sub, body_texs, outfit);
        (made.texture_name.is_empty()).then_some(None).unwrap_or(Some((made.texture_png, made.texture_name)))
    });
    if let Some((png, name)) = made {
        mesh.texture_png = png;
        mark_runtime_main_texture(mesh);
        mesh.texture_name = name;
    }
}

fn compose_body_texture_uncached(
    mesh: &mut ExtractedMesh,
    card: Option<&CardInfo>,
    skin: [f32; 4],
    sub: [f32; 4],
    body_texs: &[(String, Vec<u8>)],
    outfit: i32,
) {
    if !crate::okstrict::assembling() { return; }
    // ChaControl.CreateBodyTexture assigns cf_body_00_t and cf_body_00_mc
    // directly to the create material, with the card's skin colors.
    let mut tex = pick_named_png(body_texs, &["cf_body_00_t"])
        .map(|(_, b)| b)
        .or_else(|| mesh.texture_png.clone())
        .or_else(|| crate::unity::solid_png(skin, 256, 256));
    let Some(mut png) = tex.take() else {
        return;
    };
    let mut body_source_srgb=false;
    // KSOX Hooks.OverlayBlitImpl paints BodyUnder on the Blit source before
    // Koikano/create_skinbody runs; the shader colors that composite together.
    if let Some(ov) = card.and_then(|c| ksox_overlay_png(c, outfit, 3)) {
        // Authored base PNG rows precede the final Blit V reversal, while
        // TextureContainer overlay PNG rows already match RT ReadPixels.
        let ov=flip_runtime_blit_v(ov);
        if let Some(composited) = composite_overlay_space(&png,&ov,false,true) {
            png = composited;body_source_srgb=true;
        }
    }
    let mask_name=if card.is_some_and(|c|c.sex==0){"cm_body_00_mc"}else{"cf_body_00_mc"};
    if let Some((_, mask)) = pick_named_png(body_texs, &[mask_name]) {
        let nail = card.map(|c| c.nail_color).unwrap_or([1.0; 4]);
        if let Some(blended) = crate::unity::apply_body_create_colors_sampled(&png,&mask,skin,sub,nail,body_source_srgb) {
            png = blended;
        }
    } else if let Some(skinned) = crate::unity::overlay_skin_png(&png, skin) {
        png = skinned;
    }
    // Unlike the face compositor, this body path starts from the directly
    // decoded cf_body_00_t. Match the live Maker body's row orientation
    // before UE applies its Unity-to-UE V conversion.
    mesh.texture_png = Some(flip_runtime_blit_v(png));
    mark_runtime_main_texture(mesh);
    mesh.texture_name = "cf_body_00_t".into();
}

pub(crate) fn is_head_detail(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    if n == "cf_o_face" || n.contains("gag") {
        return false;
    }
    n.contains("eye")
        || n.contains("mayu")
        || n.contains("eyebrow")
        || n.contains("eyeline")
        || n.contains("tooth")
        || n.contains("tang")
        || n.contains("canine")
        || n.contains("noseline")
        || n.contains("nose")
        || n.contains("sirome")
        || n.contains("hitomi")
}

/// Mouth-pattern override (`FBSCtrlMouth`). Defaults to pattern 0
/// (`ChaFileStatus.mouthPtn` / `CvsDrawCtrl.ChangeMouthDefault`).
/// `OK_MOUTH_PTN=off` (or `-1`) uses `neutral_fbs_weight` instead.
pub(crate) fn mouth_ptn_override() -> Option<usize> {
    match std::env::var("OK_MOUTH_PTN") {
        Ok(s) => {
            let t = s.trim().to_ascii_lowercase();
            if t == "off" || t == "-1" {
                None
            } else {
                Some(t.parse::<usize>().unwrap_or(0).min(4))
            }
        }
        Err(_) => Some(0),
    }
}

/// Open amount (percent) for `mouth_ptn_override`.
/// Default 80: pattern 0 smile (`def_cl`/`def_op`) with mouth open.
/// `OK_MOUTH_OPEN` 0–100.
pub(crate) fn mouth_open_percent() -> f32 {
    std::env::var("OK_MOUTH_OPEN")
        .ok()
        .and_then(|s| s.trim().parse::<f32>().ok())
        .unwrap_or(80.0)
        .clamp(0.0, 100.0)
}

/// Parts the card preview never shows, matching StudioEditor's
/// `KoikatsuMaterialConverter.ShouldHide`: tears, gag eyes, the shadow caster,
/// the male/female genital parts and the separate open-mouth teeth/tongue
/// (the neutral mouth is closed and drawn by the face mesh/blendshapes).
pub(crate) fn is_genital_display_mesh(name: &str) -> bool {
    // Separate penis and scrotum renderers. The condom (o_gomu) stays hidden.
    is_penis_mesh(name) || is_balls_mesh(name)
}

pub(crate) fn is_penis_mesh(name: &str) -> bool {
    matches!(name.to_ascii_lowercase().as_str(), "o_dankon" | "cm_o_dan00")
}

pub(crate) fn is_balls_mesh(name: &str) -> bool {
    matches!(name.to_ascii_lowercase().as_str(), "o_dan_f" | "cm_o_dan_f")
}

/// UncensorSelector.SetSkinColor builds a create-RT from the part's main
/// texture and color mask, then assigns that RT as the draw `_MainTex`.
/// The separate genital mesh has no such RT today, so it samples black.
pub(crate) fn paint_uncensor_genital_mesh(
    mesh: &mut ExtractedMesh,
    bundle: &crate::unity::ParsedBundle,
    matches: &[crate::mods::UncensorColorMatch],
    skin: [f32; 4],
    sub: [f32; 4],
    nail: [f32; 4],
) -> bool {
    let Some(part) = matches.iter().find(|item| item.object.eq_ignore_ascii_case(&mesh.name))
        .or_else(|| (matches.len() == 1).then(|| &matches[0])) else { return false };
    let Some(main) = bundle.texture_png_named(&part.main_tex) else { return false };
    let Some(mask) = bundle.texture_png_named(&part.color_mask) else { return false };
    let Some(painted) = crate::unity::apply_body_create_colors_sampled(&main, &mask, skin, sub, nail, false) else { return false };
    mesh.texture_png = Some(flip_runtime_blit_v(painted));
    mark_runtime_main_texture(mesh);
    mesh.texture_name = part.main_tex.clone();
    if mesh.mat.is_none() {
        mesh.mat = bundle.material_named(&part.material);
    }
    true
}

pub(crate) fn adopt_uncensor_genitals(
    sources: &mut Vec<ExtractedMesh>,
    game_root: &Path,
    card: &CardInfo,
    kind: crate::mods::UncensorGenitalKind,
    is_kind: fn(&str) -> bool,
    seed: i32,
    skin: [f32; 4],
    sub: [f32; 4],
    nail: [f32; 4],
    replaced_body: bool,
    outfit: i32,
) {
    if !sources.iter().any(|mesh| is_kind(&mesh.name)) { return; }
    // The installed uncensor's painted meshes follow from the card, the kind, the outfit (its skin draw settings), the
    // seed and the colours: once per view (every open of a conversion painted them again, ~15 ms).
    let painted = crate::view_memo(format!("uncensor_genitals|{:016x}|{kind:?}|{outfit}|{seed}|{skin:?}|{sub:?}|{nail:?}", card.source_digest),
        || uncensor_genitals_painted(game_root, card, kind, is_kind, seed, skin, sub, nail, outfit));
    match painted {
        None => {
            if !replaced_body {
                let before = sources.len();
                sources.retain(|mesh| !is_kind(&mesh.name));
                if sources.len() != before {
                    eprintln!("ok-extract: removed vanilla {kind:?} meshes; no non-mosaic uncensor is installed");
                }
            } else {
                eprintln!("ok-extract: {kind:?} uncensor was not found; the body mesh is unchanged");
            }
        }
        Some(painted) if painted.is_empty() => { if !replaced_body { sources.retain(|mesh| !is_kind(&mesh.name)); } }
        Some(painted) => {
            sources.retain(|mesh| !is_kind(&mesh.name));
            sources.extend(painted);
        }
    }
}

/// The installed uncensor's meshes of this kind painted for the card (None: no uncensor of it installed).
#[allow(clippy::too_many_arguments)]
fn uncensor_genitals_painted(game_root: &Path, card: &CardInfo, kind: crate::mods::UncensorGenitalKind, is_kind: fn(&str) -> bool,
    seed: i32, skin: [f32; 4], sub: [f32; 4], nail: [f32; 4], outfit: i32) -> Option<Vec<ExtractedMesh>> {
    let explicit = match kind {
        crate::mods::UncensorGenitalKind::Penis => card.penis_guid.as_deref(),
        crate::mods::UncensorGenitalKind::Balls => card.balls_guid.as_deref(),
    };
    let (source, bytes) = crate::mods::uncensor_genital_for_card(game_root, kind, explicit, card.sex, seed)?;
    let bundle = crate::unity::parse_bundle_cached(bytes);
    bundle.configure_game_references(game_root);
    // The installed uncensor's meshes before painting are the same for every card (~0.25 s to extract each time).
    type Memo = std::sync::Mutex<std::collections::HashMap<(PathBuf, u64, Option<std::time::SystemTime>, String, String), std::sync::Arc<Vec<ExtractedMesh>>>>;
    static MESHES: std::sync::OnceLock<Memo> = std::sync::OnceLock::new();
    let stamp = crate::gamefs::metadata(&source.zipmod).ok();
    let memo_key = (source.zipmod.clone(), stamp.as_ref().map_or(0, |m| m.len()), stamp.as_ref().and_then(|m| m.modified().ok()),
        source.file.clone(), source.asset.clone());
    let cached = stamp.is_some()
        .then(|| MESHES.get_or_init(Default::default).lock().unwrap_or_else(|e| e.into_inner()).get(&memo_key).cloned()).flatten();
    let meshes: Vec<ExtractedMesh> = match cached {
        Some(meshes) => (*meshes).clone(),
        None => {
            let (meshes, _) = bundle.meshes_for_maindata(&source.asset);
            let mut memo = MESHES.get_or_init(Default::default).lock().unwrap_or_else(|e| e.into_inner());
            if memo.len() >= 16 { memo.clear(); }
            memo.insert(memo_key, std::sync::Arc::new(meshes.clone()));
            meshes
        }
    };
    let mut painted = Vec::new();
    for mut mesh in meshes {
        if mesh.positions.is_empty() || !is_kind(&mesh.name) { continue; }
        if !paint_uncensor_genital_mesh(&mut mesh, &bundle, &source.color_matches, skin, sub, nail) {
            eprintln!("ok-extract: {kind:?} mesh {} has no color-match skin", mesh.name);
            continue;
        }
        if let Some(mat) = mesh.mat.as_mut() {
            mat.floats.insert("_linetexon".into(), if card.body_draw_add_line { 1.0 } else { 0.0 });
        }
        apply_native_skin_draw_settings(&mut mesh, card, outfit, true);
        eprintln!(
            "ok-extract: {kind:?} {} guid={} display={} tex={} verts={}",
            mesh.name, source.guid, source.display_name,
            mesh.texture_png.as_ref().map_or(0, Vec::len), mesh.positions.len()
        );
        painted.push(mesh);
    }
    if painted.is_empty() { eprintln!("ok-extract: {kind:?} uncensor {} had no paintable mesh", source.guid); }
    Some(painted)
}

pub(crate) fn is_hidden_part(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    let mouth_open = mouth_ptn_override().is_some()
        && mouth_open_percent() > 0.0
        && (n == "cf_o_canine" || n == "cf_o_tooth" || n == "o_tang");
    if mouth_open || crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1") {
        if n == "cf_o_canine" || n == "cf_o_tooth" || n == "o_tang" {
            return false;
        }
    }
    if mouth_open {
        return false;
    }
    // `OK_SHOW_MOUTH` also reveals o_mnpa/o_mnpb (the separate mouth meshes
    // StudioEditor hides) to check whether they cover the face's mouth cavity.
    if std::env::var("OK_SHOW_MOUTH").is_ok() && (n == "o_mnpa" || n == "o_mnpb") {
        return false;
    }
    n.starts_with("cf_o_namida_")
        || n.starts_with("cf_o_gag_eye_")
        || n.contains("highlight_")
        || n.contains("height bar")
        || n.contains("silhouette")
        || n == "background"
        || n == "o_shadowcaster"
        || n == "o_dankon"
        || n == "o_dan_f"
        || n == "cm_o_dan00"
        || n == "cm_o_dan_f"
        || n == "o_gomu"
        || n == "o_mnpa"
        || n == "o_mnpb"
        || n == "n_dankon"
        || n == "cf_o_canine"
        || n == "cf_o_tooth"
        || n == "o_tang"
}
