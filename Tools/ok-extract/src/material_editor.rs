//! Material Editor and renderer settings of the open: ME slots, textures and alpha masks, hair gloss, the toon ramp, shadow/line settings and first-load options.
use super::*;

pub(crate) fn me_slot(role: &str, property: Option<&str>) -> Option<i32> {
    let r = role.to_ascii_lowercase();
    let p = property.unwrap_or("");
    // ChaControl.objHair indices are back/front/side/other. MaterialEditor
    // stores hair overrides under those indices, not the catalog item ID.
    if r.contains("hair") && !r.contains("acc") {
        return match r.as_str() {
            "hair_b" => Some(0), "hair_f" => Some(1),
            "hair_s" => Some(2), "hair_o" => Some(3),
            _ => p.to_ascii_lowercase().split("hair.parts[").nth(1)
                .and_then(|tail| tail.split(']').next())
                .and_then(|index| index.parse().ok()),
        };
    }
    if r.contains("acc") {
        return accessory_slot_from_property(p).map(|s| s as i32);
    }
    if r.contains("clothes") {
        let pl = p.to_ascii_lowercase();
        // The three top sub-parts all belong to ChaFileClothes.parts[0].
        // Material Editor stores their overrides under clothes slot 0.
        if pl.contains("clothestopparts") {
            return Some(0);
        }
        let i = pl.find("clothes")?;
        let rest = &pl[i + "clothes".len()..];
        let n: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        return n.parse().ok();
    }
    if r == "body" || r == "head" || r == "mouth" {
        return Some(0);
    }
    None
}

pub(crate) fn me_object_type(role: &str) -> Option<i32> {
    let r = role.to_ascii_lowercase();
    if r.contains("acc") {
        Some(2)
    } else if r.contains("clothes") {
        Some(1)
    } else if r.contains("hair") {
        Some(3)
    } else if r == "body" || r == "head" || r == "mouth" {
        Some(4)
    } else {
        None
    }
}

/// Material Editor `Enabled=0` for every renderer of this mesh.
pub(crate) fn me_hides_mesh(
    off: Option<&std::collections::HashSet<(i32, i32, i32, String)>>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &ExtractedMesh,
) -> bool {
    let Some(off) = off else {
        return false;
    };
    if off.is_empty() {
        return false;
    }
    let Some(ot) = me_object_type(role) else {
        return false;
    };
    let Some(slot) = me_slot(role, property) else {
        return false;
    };
    let coord = if ot == 4 { 0 } else { outfit };
    let names: Vec<String> = if mesh.renderer_names.is_empty() {
        vec![mesh.name.to_ascii_lowercase()]
    } else {
        mesh.renderer_names
            .iter()
            .map(|n| n.to_ascii_lowercase())
            .collect()
    };
    if names.is_empty() {
        return false;
    }
    names.iter().all(|name| {
        off.contains(&(ot, coord, slot, name.clone()))
            || off.iter().any(|(t, c, s, me)| {
                *t == ot
                    && *c == coord
                    && *s == slot
                    && (name == me || name.starts_with(&format!("{me}_")))
            })
    })
}

pub(crate) fn me_face_mat_names(mesh_name: &str) -> Vec<String> {
    let ln = mesh_name.to_ascii_lowercase();
    let mut names = Vec::new();
    if ln == "cf_o_face" {
        names.push("cf_m_face_00".into());
    } else if ln.contains("eyeline") && ln.contains("kage") {
        names.push("cf_m_eyeline_kage".into());
    } else if ln.contains("eyeline") && (ln.contains("low") || ln.contains("down")) {
        names.push("cf_m_eyeline_down".into());
    } else if ln.contains("eyeline") {
        names.push("cf_m_eyeline_00_up".into());
    } else if ln.contains("hitomi") && ln.contains("02") {
        names.push("cf_m_hitomi_00".into());
    } else if ln.contains("mayuge") {
        names.push("cf_m_mayuge_00".into());
    } else if ln.contains("nose") {
        names.push("cf_m_noseline_00".into());
    }
    names
}

/// Material Editor texture for this mesh. The material key identifies which
/// renderer receives the replacement when a slot has several materials.
pub(crate) fn native_item_has_texture_property(property: &str) -> bool {
    static PROPERTIES:std::sync::OnceLock<std::collections::HashSet<String>>=std::sync::OnceLock::new();
    PROPERTIES.get_or_init(|| {
        // Generated from the installed shader's ParsedForm properties, shared
        // with the native UE adapter. MaterialAPI ignores absent properties.
        let schema:serde_json::Value=serde_json::from_str(include_str!(
            "../../../Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/MainItemPlusBindings.json")).expect("generated MainItemPlus property schema");
        schema["properties"].as_array().expect("shader property array").iter()
            .filter(|p|p["m_Type"].as_i64()==Some(4))
            .filter_map(|p|p["m_Name"].as_str()).map(|p|p.trim_start_matches('_').to_ascii_lowercase()).collect()
    }).contains(property.trim_start_matches('_').to_ascii_lowercase().as_str())
}

pub(crate) fn me_texture<'a>(
    map: Option<&'a crate::card::MeTexMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &ExtractedMesh,
    tex_property: &str,
) -> Option<&'a [u8]> {let _spot=crate::profile::spot("me_texture");
    let map = map?;
    if map.is_empty() {
        return None;
    }
    if mesh.mat.as_ref().is_some_and(|m|m.shader.eq_ignore_ascii_case("xukmi/MainItemPlus"))
        && !native_item_has_texture_property(tex_property) {return None;}
    let ot = me_object_type(role)?;
    let slot = me_slot(role, property)?;
    let coord = if ot == 4 { 0 } else { outfit };
    let mut mats = Vec::new();
    if let Some(mat) = mesh.mat.as_ref() {
        let key = crate::card::me_mat_key(&mat.name);
        if !key.is_empty() {
            mats.push(key);
        }
    }
    if ot == 4 {
        for n in me_face_mat_names(&mesh.name) {
            if !mats.iter().any(|m| m == &n) {
                mats.push(n);
            }
        }
    }
    for mat in mats {
        if let Some(png) = map.get(&(ot, coord, slot, mat, tex_property.to_string())) {
            return Some(png.as_slice());
        }
    }
    if tex_property != "maintex" {
        return None;
    }
    // Accessory hair often has one MainTex override for the slot and a material
    // name the bundle did not keep. Use it only when the slot has a single
    // distinct override, so a clothes slot with body/body_1 stays per-material.
    let hairish = role.contains("hair")
        || mesh.hair_shader_hint
        || mesh.mat.as_ref().is_some_and(|m| is_native_hair_shader(&m.shader));
    if hairish {
        let mut found: Option<&std::sync::Arc<Vec<u8>>> = None;
        let mut multi = false;
        for ((t, c, s, mat, prop), png) in map.iter() {
            if *t != ot || *c != coord || *s != slot || prop != "maintex" || mat.contains(".mecopy")
            {
                continue;
            }
            if let Some(prev) = found {
                if !std::sync::Arc::ptr_eq(prev, png) {
                    multi = true;
                    break;
                }
            } else {
                found = Some(png);
            }
        }
        if !multi {
            if let Some(png) = found {
                return Some(png.as_slice());
            }
        }
    }
    None
}

/// Prefab materials can reference alpha textures in the shared `mt_mask_*`
/// bundles rather than in their own mesh bundle. Keep the authored mask when
/// the equipped outer garment does not supply an OverBra/OverInner override.
pub(crate) fn resolve_material_alpha_masks(game_root: &Path, meshes: &mut [ExtractedMesh]) {
    let mut found: std::collections::HashMap<String, Option<Vec<u8>>> = std::collections::HashMap::new();
    for mesh in meshes {
        if mesh.alpha_mask_png.is_some() { continue; }
        let Some(name) = mesh.mat.as_ref().and_then(|m| m.slot("_AlphaMask")) else { continue; };
        let png = found.entry(name.to_ascii_lowercase()).or_insert_with(|| material_alpha_mask(game_root, name));
        mesh.alpha_mask_png = png.clone();
    }
}

/// The base mask bundles' texture of this name (every clothes item of every open looked it up in each mt_mask_ bundle
/// again): once per view (crate::view_memo; the open's own thread asks).
fn material_alpha_mask(game_root: &Path, name: &str) -> Option<Vec<u8>> {
    crate::view_memo(format!("material_alpha_mask|{}|{name}", game_root.display()), || {
        let mask_dir = game_root.join("abdata/chara");
        let mut bundles: Vec<PathBuf> = crate::gamefs::read_dir(&mask_dir)
            .into_iter().flatten().filter_map(|e| e.ok().map(|e| e.path()))
            .filter(|p| p.file_name().and_then(|s| s.to_str())
                .is_some_and(|s| s.starts_with("mt_mask_") && s.ends_with(".unity3d")))
            .collect();
        bundles.sort();
        bundles.iter().find_map(|p| crate::unity::named_png_from_bundle(p, &[name]).map(|(_,png)| orient_raw_mask(png)))
    })
}

/// ChaControl.LoadHairGlossMask resolves ChaFileHair.glossId in category 433,
/// then applies that shared texture to native hair renderers. HairAccessory-
/// Customizer uses the same texHairGloss for custom hair accessory renderers.
pub(crate) fn selected_hair_gloss_texture(
    game_root: &Path, card: &CardInfo,
    vanilla: &ChaListCatalog, mods: &ChaListCatalog,
    guid_index: &std::collections::HashMap<String, PathBuf>,
) -> Option<(Vec<u8>, crate::unity::TextureSampling)> {
    let load = |entry: &ListEntry, zip_override: Option<&Path>| {
        if entry.main_tex.is_empty() || entry.main_tex == "0" { return None; }
        let ab = entry.info("MainTexAB");
        let zip = zip_override.map(|p| p.to_string_lossy().into_owned())
            .or_else(|| entry.extra.get("__zip").cloned());
        let mut source = entry.clone();
        if let Some(zip) = zip { source.extra.insert("__zip".into(), zip); }
        // Match the UV-facing row convention used by the other shader inputs.
        // Serialized Texture2D rows need conversion; Sideloader PNG overrides
        // already use runtime rows and must not be flipped twice.
        let png = png_in_runtime_row_order(game_root, &source, &ab, &[&entry.main_tex])?;
        let sampling = named_texture_sampling(game_root, &source, &ab,
            &[&entry.main_tex]).unwrap_or_default();
        Some((png, sampling))
    };
    if let Some(resolve) = card.resolves.iter().find(|r|
        r.category_no == 433 && r.property.to_ascii_lowercase().contains("glossid"))
    {
        // KKEx pins the runtime id to one zipmod. Resolve its local slot in
        // that zip rather than relying on the merged catalog's id collisions.
        let zip = guid_path(guid_index, &resolve.mod_id)?;
        let catalog = crate::list::zipmod_catalog(zip);
        let entry = catalog.get_slot(433, resolve.slot, resolve.local_slot)?;
        let result = load(entry, Some(zip))?;
        eprintln!("ok-extract: hair gloss id={} tex={} bytes={}",
            card.hair_gloss_id, entry.main_tex, result.0.len());
        return Some(result);
    }
    for entry in [mods.get(433, card.hair_gloss_id), vanilla.get(433, card.hair_gloss_id)]
        .into_iter().flatten()
    {
        if let Some(result) = load(entry, None) {
            eprintln!("ok-extract: hair gloss id={} tex={} bytes={}",
                card.hair_gloss_id, entry.main_tex, result.0.len());
            return Some(result);
        }
    }
    None
}

pub(crate) fn set_selected_hair_gloss(meshes: &mut [ExtractedMesh], gloss: Option<&[u8]>) {
    for mesh in meshes {
        let hair_shader = mesh.mat.as_ref().is_some_and(|m|
            is_native_hair_shader(&m.shader)) || mesh.hair_shader_hint;
        if hair_shader {
            mesh.hair_gloss_png = gloss.map(|p| p.to_vec());
        }
    }
}

/// Character.UpdateGlobalShader loads the selected mt_ramp texture from the
/// system graphic settings. The GUID resolves Sideloader ramp replacements.
pub(crate) fn configured_ramp_png(game_root: &Path, vanilla: &ChaListCatalog) -> Option<Vec<u8>> {
    fn tag<'a>(xml: &'a str, name: &str) -> Option<&'a str> {
        let start = format!("<{name}>");
        let end = format!("</{name}>");
        let (_, tail) = xml.split_once(&start)?;
        Some(tail.split_once(&end)?.0.trim())
    }
    let xml = crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml")).ok()?;
    let id: i32 = tag(&xml, "rampId")?.parse().ok()?;
    let guid = tag(&xml, "rampGUID").unwrap_or_default();
    // The same setting gives the same ramp: reading its zipmod again on every open cost ~0.5 s.
    type RampMemo = std::sync::Mutex<Vec<(String, Option<Vec<u8>>)>>;
    static MEMO: RampMemo = std::sync::Mutex::new(Vec::new());
    let memo_key = format!("{}|{id}|{guid}|{}", game_root.display(), crate::oklib::mounted_pack_mods_identity());
    // (not while an OK card's answers are recorded: its own open must ask the ramp's GUID, tree_answers.rs)
    if let Some((_, hit)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _)| *k == memo_key && !crate::tree_answers::recording()) {
        return hit.clone();
    }
    let found = configured_ramp_png_uncached(game_root, vanilla, id, guid);
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    if memo.len() >= 16 { memo.remove(0); }
    memo.push((memo_key, found.clone()));
    found
}
pub(crate) fn configured_ramp_png_uncached(game_root: &Path, vanilla: &ChaListCatalog, id: i32, guid: &str) -> Option<Vec<u8>> {
    if !guid.is_empty() {
        let wanted = std::collections::HashSet::from([guid.to_string()]);
        let index = crate::mods::find_guids(&game_root.join("mods"), Some(&wanted));
        let zip = guid_path(&index, guid)?;
        // The ramp is that archive's (once per archive generation, whatever mods view a card pack makes).
        return crate::gamefs::zip_memo(&format!("ramp {id}"), zip, || {
            let entry = crate::list::catalog_from_zipmod(zip).get(432, id)?.clone();
            let bundle = crate::mods::zipmod_bundle_named(zip, &entry.info("MainTexAB"))?;
            crate::unity::named_png_from_bytes(bundle, &[&entry.main_tex]).map(|(_, b)| b)
        });
    }
    let entry = vanilla.get(432, id)?;
    let bundle = crate::gamefs::read_source(game_root.join("abdata").join(entry.info("MainTexAB"))).ok()?;
    crate::unity::named_png_from_bytes(bundle, &[&entry.main_tex]).map(|(_, b)| b)
}

/// Every shadow ramp the game's graphics settings offer (ChaListDefine 432:
/// vanilla abdata lists plus Sideloader zipmods), written as the same
/// linear PNG `configured_ramp_png` would load, with a catalogue for the
/// render-style picker. Keys: `kk_<id>` (vanilla), `<guid>_<local id>` (mods).
pub fn export_character_ramps(game_root: &Path, out_dir: &Path) -> Result<serde_json::Value, String> {
    use rayon::prelude::*;
    use sha2::Digest;
    std::fs::create_dir_all(out_dir).map_err(|e| e.to_string())?;
    let clean = |s: &str| s.chars().filter(|c| !matches!(*c, '\u{180e}' | '\u{200b}')).collect::<String>().trim().to_string();
    let key_of = |s: &str| s.chars().map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_lowercase() } else { '_' }).collect::<String>();
    let mut rows = Vec::new();
    let mut write = |key: String, entry: &ListEntry, guid: &str, bundle: Vec<u8>| -> Result<(), String> {
        let Some((_, png)) = crate::unity::named_png_from_bytes(bundle.clone(), &[&entry.main_tex]) else {
            eprintln!("ok-ramps: texture {} missing for {key}", entry.main_tex);
            return Ok(());
        };
        let file = format!("{key}.png");
        std::fs::write(out_dir.join(&file), &*crate::teximg::as_png(&png)).map_err(|e| e.to_string())?;
        rows.push(serde_json::json!({
            "key": key, "id": entry.id, "guid": guid, "name": clean(&entry.name),
            "bundle": entry.info("MainTexAB"), "texture": entry.main_tex, "file": file,
            "source_sha256": format!("{:x}", sha2::Sha256::digest(&bundle)),
            "png_sha256": format!("{:x}", sha2::Sha256::digest(&png)),
        }));
        Ok(())
    };
    let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom"));
    let mut ids: Vec<_> = vanilla.by_id.values().filter(|e| e.category_no == 432).cloned().collect();
    ids.sort_by_key(|e| e.id);
    for entry in &ids {
        let path = game_root.join("abdata").join(entry.info("MainTexAB"));
        let bundle = std::fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        write(format!("kk_{}", entry.id), entry, "", bundle)?;
    }
    let index = crate::mods::find_guids(&game_root.join("mods"), None);
    let mut zips: Vec<_> = index.into_iter().collect();
    zips.sort();
    let found: Vec<(String, std::path::PathBuf, Vec<ListEntry>)> = zips.par_iter().filter_map(|(guid, zip)| {
        let mut es: Vec<_> = catalog_from_zipmod(zip).by_id.into_values().filter(|e| e.category_no == 432).collect();
        if es.is_empty() { return None; }
        es.sort_by_key(|e| e.id);
        Some((guid.clone(), zip.clone(), es))
    }).collect();
    for (guid, zip, entries) in found {
        for entry in &entries {
            let Some(bundle) = crate::mods::zipmod_bundle_named(&zip, &entry.info("MainTexAB")) else {
                eprintln!("ok-ramps: bundle {} missing in {}", entry.info("MainTexAB"), zip.display());
                continue;
            };
            write(format!("{}_{}", key_of(&guid), entry.id), entry, &guid, bundle.to_vec())?;
        }
    }
    let catalogue = serde_json::json!({"schema": 1, "game_root": game_root.to_string_lossy(), "color_space": "Linear", "ramps": rows});
    std::fs::write(out_dir.join("ramps.json"), serde_json::to_string_pretty(&catalogue).unwrap()).map_err(|e| e.to_string())?;
    Ok(catalogue)
}

/// Studio.SystemButtonCtrl.SetAmbientShadow passes
/// `1 - SceneInfo.ambientShadowG` to `_ambientshadowG.a`.
/// The preview compares against Studio captures, whose default scene value
/// comes from GraphicData.shadowDepth (0.26 when the setting is absent).
/// Manager.Character uses the depth directly outside Studio; that is a
/// separate scene mode and must not be substituted for Studio's global.
pub(crate) fn configured_shadow_depth(game_root: &Path) -> f32 {
    let studio_depth = crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml"))
        .ok()
        .and_then(|xml| xml.split_once("<shadowDepth>")
            .and_then(|(_, tail)| tail.split_once("</shadowDepth>"))
            .and_then(|(value, _)| value.trim().parse::<f32>().ok()))
        .unwrap_or(0.26)
        .clamp(0.0, 1.0);
    1.0 - studio_depth
}

/// Character.UpdateGlobalShader sets `_linewidthG` from GraphicData.lineWidth.
/// This controls the main_skin `_linetexon` line-mask tail.
pub(crate) fn configured_line_width(game_root: &Path) -> f32 {
    crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml"))
        .ok()
        .and_then(|xml| xml.split_once("<lineWidth>")
            .and_then(|(_, tail)| tail.split_once("</lineWidth>"))
            .and_then(|(value, _)| value.trim().parse::<f32>().ok()))
        .unwrap_or(0.5)
        .clamp(0.0, 1.0)
}

/// Character.UpdateGlobalShader passes GraphicData.lineDepth to
/// ChaShader.ChangeLineColor, which sets `_LineColorG.a = 1 - lineDepth`.
pub(crate) fn configured_line_depth(game_root: &Path) -> f32 {
    crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml"))
        .ok()
        .and_then(|xml| xml.split_once("<lineDepth>")
            .and_then(|(_, tail)| tail.split_once("</lineDepth>"))
            .and_then(|(value, _)| value.trim().parse::<f32>().ok()))
        .unwrap_or(1.0)
        .clamp(0.0, 1.0)
}

/// ChaShader.ChangeShaderType maps GraphicData.shaderType to _KanoVerShift.
pub(crate) fn configured_kano_ver_shift(game_root: &Path) -> f32 {
    let shader_type = crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml"))
        .ok()
        .and_then(|xml| xml.split_once("<shaderType>")
            .and_then(|(_, tail)| tail.split_once("</shaderType>"))
            .and_then(|(value, _)| value.trim().parse::<i32>().ok()))
        .unwrap_or(0);
    if shader_type == 0 { 0.0 } else { 1.0 }
}

pub(crate) fn me_maintex<'a>(
    map: Option<&'a crate::card::MeTexMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &ExtractedMesh,
) -> Option<&'a [u8]> {let _spot=crate::profile::spot("me_maintex");
    me_texture(map, outfit, role, property, mesh, "maintex")
}

pub(crate) fn me_material_color(
    map: Option<&crate::card::MeColorMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &ExtractedMesh,
    property_name: &str,
) -> Option<[f32; 4]> {
    let map = map?;
    let ot = me_object_type(role)?;
    let slot = me_slot(role, property)?;
    let mat = crate::card::me_mat_key(&mesh.mat.as_ref()?.name);
    let coord = if ot == 4 { 0 } else { outfit };
    map.get(&(ot, coord, slot, mat, property_name.to_ascii_lowercase())).copied()
}

pub(crate) fn me_shadow_color(
    map: Option<&crate::card::MeColorMap>, outfit: i32, role: &str,
    property: Option<&str>, mesh: &ExtractedMesh,
) -> Option<[f32; 4]> {
    me_material_color(map, outfit, role, property, mesh, "shadowcolor")
}

/// ChaControl's actual instantiated names and copyDynamicBone arguments.
pub(crate) fn clothes_load_settings(property: &str) -> (bool, &'static str) {
    if let Some((_, index)) = property.rsplit_once(".clothesTopParts") {
        return (true, ["ct_top_parts_A", "ct_top_parts_B", "ct_top_parts_C"]
            .get(index.parse::<usize>().unwrap_or(usize::MAX)).copied().unwrap_or(""));
    }
    let kind = property.rsplit_once(".clothes").and_then(|(_, kind)| kind.parse::<usize>().ok());
    (kind.is_some_and(|kind| kind <= 1), kind.and_then(|kind|
        ["ct_clothesTop", "ct_clothesBot", "ct_bra", "ct_shorts", "ct_gloves", "ct_panst", "ct_socks", "ct_shoes_inner", "ct_shoes_outer"]
            .get(kind).copied()).unwrap_or(""))
}

/// Uncached first-load options, `OK_FIRSTLOAD=A2,A3`, `all` or `none` (read once; shared with the UE side's switch).
/// Unset: the measured defaults (A2, A3 here; B1, C3 on the UE side). Any list replaces the defaults.
pub(crate) fn first_load_option(name: &str) -> bool {
    static VALUE: std::sync::OnceLock<Vec<String>> = std::sync::OnceLock::new();
    VALUE.get_or_init(|| std::env::var("OK_FIRSTLOAD").ok().filter(|v| !v.trim().is_empty()).unwrap_or_else(|| "A2,A3".into()).split(',')
        .map(|v| v.trim().to_ascii_uppercase()).filter(|v| !v.is_empty()).collect())
        .iter().any(|v| v == "ALL" || v == name)
}
