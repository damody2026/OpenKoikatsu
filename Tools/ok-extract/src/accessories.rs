//! Hair and accessories of the open: hair parts and colours, accessory bundle sources and colouring, Material Editor shaders, and the worn clothes selection.
use super::*;

pub(crate) fn simple_iris_png() -> Option<Vec<u8>> {
    use std::io::Cursor;
    let w = 64u32;
    let mut img = image::RgbaImage::new(w, w);
    let cx = (w as f32 - 1.0) * 0.5;
    for y in 0..w {
        for x in 0..w {
            let dx = x as f32 - cx;
            let dy = y as f32 - cx;
            let r = (dx * dx + dy * dy).sqrt() / cx;
            let px = if r < 0.22 {
                image::Rgba([8, 8, 12, 255])
            } else if r < 0.72 {
                let t = ((r - 0.22) / 0.5).clamp(0.0, 1.0);
                image::Rgba([
                    (30.0 + 40.0 * t) as u8,
                    (80.0 + 50.0 * (1.0 - t)) as u8,
                    (180.0 + 40.0 * (1.0 - t)) as u8,
                    255,
                ])
            } else if r < 0.95 {
                image::Rgba([220, 230, 240, 255])
            } else {
                image::Rgba([0, 0, 0, 0])
            };
            img.put_pixel(x, y, px);
        }
    }
    crate::teximg::encode_rgba_owned(img)
}

pub(crate) fn display_hair(c: [f32; 4]) -> [f32; 4] {
    c
}

// ChaControl.ChangeSettingHairOutlineColor / ChangeSettingHairGlossColor.
pub(crate) fn set_hair_part_materials(meshes:&mut [ExtractedMesh], hair:&crate::card::HairPart) {
    for mesh in meshes {
        set_material_color(mesh,"_LineColor",hair.outline_color);
        if mesh.mat.as_ref().is_some_and(|m|is_native_hair_shader(&m.shader.to_ascii_lowercase())) || mesh.hair_shader_hint {
            set_material_color(mesh,"_GlossColor",hair.gloss_color);
        }
    }
}

pub(crate) fn hair_cols_for(card: &CardInfo, cat: i32) -> [[f32; 4]; 3] {
    let i = (cat - 101) as usize;
    if (101..=104).contains(&cat) && i < 4 {
        let p = &card.hair_parts[i];
        return [p.color, p.start_color, p.end_color];
    }
    // ChaControl.ChangeAccessoryColor rendHair: all three = parts[0].startColor.
    let p = &card.hair_parts[0];
    let c = if p.start_color[0] + p.start_color[1] + p.start_color[2] > 0.04 {
        p.start_color
    } else {
        p.color
    };
    [c, c, c]
}

#[allow(dead_code)]
pub(crate) fn hair_tint_for(card: &CardInfo, cat: i32) -> [f32; 4] {
    hair_cols_for(card, cat)[0]
}

/// `ChaControl.ChangeAccessoryColor` (ChaControl.cs:2598): a normal accessory
/// item's `_Color`/`_Color2`/`_Color3` come from the card's
/// `ChaFileAccessory.parts[slot].color[0..2]` and are combined through the
/// item's `_ColorMask` exactly like clothes (`MainTex * (
/// Color1*mask.r + Color2*mask.g + Color3*mask.b)`). `rendHair` renderers
/// instead use the hair start colour for all three (handled by `push_meshes`),
/// so hair-named meshes are skipped here.
/// Bundle bytes for an accessory entry (zipmod > override > vanilla abdata).
pub(crate) fn accessory_bundle_bytes(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<crate::unity::SourceBytes> {let _spot=crate::profile::spot("accessory_bundle_bytes");
    accessory_bundle_source(game_root,zip,entry).map(|(bytes,_)|bytes)
}

pub(crate) fn accessory_clothes_plugins(game_root:&Path)->Vec<PathBuf> {
    let mut pending=vec![game_root.join("BepInEx/plugins")];let mut found=Vec::new();
    while let Some(path)=pending.pop() {
        let Ok(entries)=crate::gamefs::read_dir(path) else {continue};
        for entry in entries.flatten() {
            let Ok(kind)=entry.file_type() else {continue};let path=entry.path();
            if kind.ok_is_dir() {pending.push(path);}
            else if path.file_name().and_then(|n|n.to_str()).is_some_and(|n|n.eq_ignore_ascii_case("KKS_AccessoryClothes.dll")) {found.push(path);}
        }
    }
    found.sort();found
}

pub(crate) fn accessory_assignment(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<crate::unity::AccessoryBodyBinding> {
    if crate::appearance::timings::measure("assignment.plugins",||accessory_clothes_plugins(game_root).is_empty()) {return None;}
    let shared=crate::appearance::timings::measure("assignment.source",||accessory_bundle_shared(game_root,zip,entry))?;
    let parsed=crate::unity::parse_bundle_ref(&shared.0);parsed.configure_game_references_from_zip(game_root,shared.1.as_deref());
    parsed.accessory_body_binding(&entry.main_data)
}
pub(crate) fn accessory_bundle_source(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<(crate::unity::SourceBytes,Option<PathBuf>)> {
    accessory_bundle_shared(game_root,zip,entry).map(|shared|(shared.0.clone(),shared.1.clone()))
}
pub(crate) fn accessory_bundle_shared(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<std::sync::Arc<(crate::unity::SourceBytes,Option<PathBuf>)>> {
    let key=(zip.map(Path::to_path_buf),entry.main_ab.clone(),entry.main_data.clone());
    if let Some(hit)=ACCESSORY_SOURCE_MEMO.with(|m|m.borrow().iter().find(|(k,_)|*k==key).map(|(_,v)|v.clone())) {return hit;}
    let found=accessory_bundle_source_uncached(game_root,zip,entry).map(std::sync::Arc::new);
    ACCESSORY_SOURCE_MEMO.with(|m|{let mut m=m.borrow_mut();if m.len()>=4{m.remove(0);}m.push((key,found.clone()));});
    found
}
pub(crate) fn accessory_bundle_source_uncached(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<(crate::unity::SourceBytes,Option<PathBuf>)> {
    let contains = |bytes: &crate::unity::SourceBytes| entry.main_data.is_empty()
        || crate::unity::parse_bundle_ref(bytes).contains_prefab(&entry.main_data);
    zip.and_then(|z|crate::mods::zipmod_one_unity3d_source(z,&entry.main_ab)
            .map(|(_,b)|b).filter(&contains).map(|b|(b,Some(z.to_owned()))))
        .or_else(||crate::mods::override_bundle_with_source(&game_root.join("mods"),&entry.main_ab)
            .map(|(z,b)|(crate::unity::SourceBytes::from(b),z)).filter(|(b,_)|contains(b)).map(|(b,z)|(b,Some(z))))
        .or_else(||crate::gamefs::read_source(game_root.join("abdata").join(&entry.main_ab)).ok().map(|b|(b,None)))
}

/// Authored material colour (`_Color`/`_Color2`/`_Color3`/`_Color4`).
pub(crate) fn mat_color(mesh: &ExtractedMesh, key: &str) -> [f32; 4] {
    let is_hair = mesh.hair_shader_hint || mesh.mat.as_ref().is_some_and(|m|
        is_native_hair_shader(&m.shader));
    let default = match key {
        "_Color2" if is_hair => [0.7843137, 0.7843137, 0.7843137, 1.0],
        "_Color3" if is_hair => [0.5, 0.5, 0.5, 1.0],
        _ => [1.0; 4],
    };
    mesh.mat.as_ref().and_then(|mm| mm.color(key)).unwrap_or(default)
}

pub(crate) fn ids_hit(ids: &[i64], set: &std::collections::HashSet<i64>) -> bool {
    !ids.is_empty() && ids.iter().any(|id| set.contains(id))
}

pub(crate) fn shift_length_bones(sk: &mut crate::pose::Skeleton, bones: &[(String, Option<f32>)], delta: f32) {
    if bones.is_empty() {
        return;
    }
    let mut hit = false;
    for tf in sk.tfs.values_mut() {
        if let Some((_, base)) = bones.iter().find(|(n, _)| n.eq_ignore_ascii_case(&tf.name)) {
            // ChaCustomHairComponent.Update assigns this absolute value every
            // frame. The prefab transform can already differ from baseLength.
            tf.pos[1] = base.unwrap_or(tf.pos[1]) + delta;
            hit = true;
        }
    }
    if hit {
        sk.rebuild_worlds();
    }
}

/// `ChaCustomHairComponent.Update`: `localY = baseLength + lerp(0, addLength, lengthRate)`.
pub(crate) fn apply_hair_accessory_length(
    meshes: &mut [ExtractedMesh],
    hint: &crate::unity::AccessoryColorUse,
    rate: f32,
) {
    let delta = hint.add_length * rate.clamp(0.0, 1.0);
    for m in meshes.iter_mut() {
        if crate::pose::accdiag_matches(&m.name) {
            eprintln!("ok-extract: ACCDIAG hair_length mesh={} rate={} add={} delta={} bones={:?}",
                m.name, rate, hint.add_length, delta, hint.length_bones);
        }
        if let Some(sk) = m.smr_skel.as_mut() {
            shift_length_bones(sk, &hint.length_bones, delta);
        }
    }
}

pub(crate) fn me_shader_color(
    card: &CardInfo,
    outfit: i32,
    property: &str,
    mesh: &ExtractedMesh,
    prop: &str,
) -> Option<[f32; 4]> {
    let ot = me_object_type("acc")?;
    let slot = me_slot("acc", Some(property))?;
    let mat = mesh.mat.as_ref().map(|mm| crate::card::me_mat_key(&mm.name))?;
    card.me_color
        .get(&(ot, outfit, slot, mat, prop.trim_start_matches('_').to_ascii_lowercase()))
        .copied()
}

pub(crate) const CLOTHES_PROP: [&str; 9] = [
    "ClothesTop", "ClothesBot", "ClothesBra", "ClothesShorts",
    "ClothesGloves", "ClothesPants", "ClothesSocks",
    "ClothesShoesInner", "ClothesShoesOuter",
];

pub(crate) type SelectedClothes = Option<(ListEntry, String, Option<PathBuf>)>;
pub(crate) type SelectedTopSubpart = Option<(i32, ListEntry, String, Option<PathBuf>)>;

pub(crate) fn select_card_clothes(
    card: &CardInfo, outfit: i32, vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog, index: &std::collections::HashMap<String, PathBuf>,
) -> Vec<SelectedClothes> {
    let mut zip_lists = std::collections::HashMap::new();
    CLOTHES_PROP.iter().enumerate().map(|(kind, name)| {
        let cid = card.clothes_ids.get(kind).copied().unwrap_or(0);
        let cat = if kind < 7 { 105 + kind as i32 } else { 112 };
        let prop = format!("outfit{outfit}.ChaFileClothes.{name}");
        let resolve = card.resolves.iter().find(|r| r.property == prop);
        if resolve.is_none() && cid == 0 { return None; }
        let (guid, slot, local) = match resolve {
            Some(r) => (r.mod_id.clone(), r.slot, 0),
            None => (String::new(), cid, cid),
        };
        let zip_path = if guid.is_empty() { None } else { guid_path(index, &guid).cloned() };
        let entry = lookup_entry(vanilla, &mut zip_lists, zip_path.as_deref(),
            &guid, cat, slot, local).or_else(|| {
                if guid.is_empty() {
                    mod_catalog.get(cat, slot).cloned().or_else(|| vanilla.get(cat, slot).cloned())
                } else { None }
            })?;
        Some((entry, guid, zip_path))
    }).collect()
}

pub(crate) fn select_card_top_subparts(
    card: &CardInfo, outfit: i32, top_type: i32,
    vanilla: &ChaListCatalog, mod_catalog: &ChaListCatalog,
    index: &std::collections::HashMap<String, PathBuf>,
) -> Vec<SelectedTopSubpart> {
    if top_type != 1 && top_type != 2 { return vec![None;3]; }
    let prefix=if top_type==1 {"ClothesSailorSub"}else{"ClothesJacketSub"};
    let mut zip_lists = std::collections::HashMap::new();
    (0..3usize).map(|kind| {
        let suffix = ["A", "B", "C"][kind];
        let sid = card.sub_parts_id.get(kind).copied().unwrap_or(0);
        for name in [format!("{prefix}{suffix}")] {
            let prop = format!("outfit{outfit}.ChaFileClothes.{name}");
            if let Some(r) = card.resolves.iter().find(|r| r.property == prop) {
                let zip_path = guid_path(index, &r.mod_id).cloned();
                if let Some(entry) = lookup_entry(vanilla, &mut zip_lists, zip_path.as_deref(),
                    &r.mod_id, r.category_no, r.slot, 0) {
                    return Some((r.category_no, entry, r.mod_id.clone(), zip_path));
                }
            }
        }
        if top_type != 1 && top_type != 2 { return None; }
        let cat = (if top_type == 1 { 200 } else { 210 }) + kind as i32;
        vanilla.get(cat, sid).cloned()
            .or_else(|| mod_catalog.get(cat, sid).cloned())
            .map(|entry| (cat, entry, String::new(), None))
    }).collect()
}

/// First value: MaterialEditor.LoadShader can use this shader on Windows.
/// Second value: its Unity Shader declaration for `_rimV`, when available.
pub(crate) fn me_shader_registration(game_root: &Path, shader: &str) -> (bool, Option<bool>) {
    static DEFAULT: std::sync::OnceLock<Option<std::collections::HashSet<String>>> = std::sync::OnceLock::new();
    static DEFAULT_RIM: std::sync::OnceLock<std::sync::Mutex<std::collections::HashMap<String, Option<bool>>>> = std::sync::OnceLock::new();
    static MOD_RESULTS: std::sync::OnceLock<std::sync::Mutex<std::collections::HashMap<String, (bool, Option<bool>)>>> = std::sync::OnceLock::new();
    let defaults = DEFAULT.get_or_init(|| {
        let default_xml = game_root.join(r"kks_decomp\plugins\KKS_Plugins\KKS_MaterialEditor\KK_Plugins.Resources.default.xml");
        let xml = crate::gamefs::read_to_string(default_xml).ok()?;
        let mut names = std::collections::HashSet::new();
        for tag in xml.split('<').filter(|s| s.starts_with("Shader ")) {
            if let Some((_, rest)) = tag.split_once("Name=\"") {
                if let Some((name, _)) = rest.split_once('"') { names.insert(name.to_ascii_lowercase()); }
            }
        }
        Some(names)
    });
    let Some(defaults) = defaults else { return (true, None); };
    if defaults.contains(&shader.to_ascii_lowercase()) {
        let cache = DEFAULT_RIM.get_or_init(|| std::sync::Mutex::new(std::collections::HashMap::new()));
        if let Some(&value) = cache.lock().unwrap().get(&shader.to_ascii_lowercase()) { return (true, value); }
        let directory = game_root.join(r"kks_decomp\plugins\KKS_Plugins\KKS_MaterialEditor");
        let mut rim = None;
        for bundle_name in ["KK_Plugins.Resources.EC_Shaders.unity3d", "KK_Plugins.Resources.KKS_Shaders.unity3d"] {
            let Ok(bytes) = crate::gamefs::read(directory.join(bundle_name)) else { continue; };
            if let Some(value) = crate::unity::shader_bundle_declares_property(&bytes, shader, "_rimV") {
                rim = Some(value);
                break;
            }
        }
        cache.lock().unwrap().insert(shader.to_ascii_lowercase(), rim);
        return (true, rim);
    }
    // xukmi's studio item shaders are drawn as the Shader Forge family they replace (translucent, `_alpha`, see
    // `shader_family`): no Vanilla+ program or zipmod is needed. An OK card opens with only the zipmods its items use,
    // so the shader mod was never found and a 0.6 hair-shadow overlay stayed on the opaque item shader (black forehead).
    if shader.to_ascii_lowercase().starts_with("xukmi/") && shader_family(shader).contains("main_item_studio") {
        return (true, None);
    }
    // An OK card's answer (tree_answers.rs): no manifest of the tree is read.
    let question = shader.to_ascii_lowercase();
    if let Some(found) = crate::tree_answers::lookup::<(bool, Option<bool>)>("me-shader", question.as_bytes()) { return found; }
    if crate::tree_answers::unanswered("me-shader", question.as_bytes()) { return (false, None); }
    let found = crate::okstrict::scanning(|| me_shader_registration_in_tree(game_root, shader, &MOD_RESULTS));
    crate::tree_answers::record("me-shader", question.as_bytes(), &found);
    found
}

/// `me_shader_registration` for a shader MaterialEditor's own resources do not declare: the tree's manifests.
pub(crate) fn me_shader_registration_in_tree(game_root: &Path, shader: &str,
    results: &std::sync::OnceLock<std::sync::Mutex<std::collections::HashMap<String, (bool, Option<bool>)>>>) -> (bool, Option<bool>) {
    let results = results.get_or_init(|| std::sync::Mutex::new(std::collections::HashMap::new()));
    // Which mods are readable depends on the root and on the zipmods the fallback serves (an OK card conversion adds
    // the ones the card uses unnamed and retries): a result found for one view is not an answer for another.
    let key = format!("{}\n{}\n{}", crate::gamefs::normalize(game_root), crate::gamefs::fallback_identity(), shader.to_ascii_lowercase());
    if let Some(&found) = results.lock().unwrap().get(&key) { return found; }
    // MaterialEditorPlugin.LoadXML reads only MaterialEditor and
    // KKS_MaterialEditor manifest children. A KK_MaterialEditor section may
    // contain a perfectly valid D3D shader but is ignored by the KKS plugin.
    let wanted = format!("Name=\"{shader}\"");
    for (zip, section) in me_manifest_sections(game_root).iter() {
        let zip = zip.as_path();
        let Some(at) = section.find(&wanted) else { continue; };
        let Some(start) = section[..at].rfind("<Shader ") else { continue; };
        let Some(end) = section[at..].find('>') else { continue; };
        let tag = &section[start..at + end];
        let Some((_, tail)) = tag.split_once("AssetBundle=\"") else { continue; };
        let Some((bundle_name, _)) = tail.split_once('"') else { continue; };
        let Some(bundle) = crate::mods::zipmod_bundle_named(&zip, bundle_name) else { continue; };
        // Migrate nested-table registrations for the implemented native
        // MainItemPlus adapter. Other new Vanilla+ families need their own UE
        // programs before replacing the established preview shader path.
        let usable=if shader.eq_ignore_ascii_case("xukmi/MainItemPlus") {
            crate::unity::shader_bundle_has_dxbc(&bundle,shader)
        } else {crate::unity::shader_bundle_has_legacy_dxbc(&bundle,shader)};
        if usable {
            let found = (true, crate::unity::shader_bundle_declares_property(&bundle, shader, "_rimV"));
            results.lock().unwrap().insert(key, found);
            return found;
        }
    }
    results.lock().unwrap().insert(key, (false, None));
    (false, None)
}

/// The MaterialEditor / KKS_MaterialEditor manifest sections of the mods tree, in zip order. Built once per tree view
/// (root, fallback, zip list): every shader a card asks for used to read all ~7000 manifests again (~1 s cold).
pub(crate) fn me_manifest_sections(game_root: &Path) -> std::sync::Arc<Vec<(PathBuf, String)>> {
    use std::sync::{Arc, Mutex};
    static MEMO: Mutex<Vec<(String, Arc<Vec<(PathBuf, String)>>)>> = Mutex::new(Vec::new());
    let mods = game_root.join("mods");
    let key = format!("{}|{}|{:016x}|{}", crate::gamefs::normalize(game_root), crate::gamefs::fallback_identity(),
        crate::list::zip_list_sig(&mods), crate::oklib::mounted_pack_mods_identity());
    if let Some((_, hit)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _)| *k == key) { return hit.clone(); }
    let zips = crate::mods::all_zipmods(&mods);
    // One archive's section. An archive the active library records cannot change, so its answer is kept by path
    // (no per-archive validation): a card pack's view then only reads the few archives it adds.
    fn section(zip: &Path) -> Option<String> {let _spot=crate::profile::spot("me_section");
        let bytes = crate::mods::zipmod_file_suffix(zip, "manifest.xml")?;
        let xml = std::str::from_utf8(&bytes).ok()?;
        ["MaterialEditor", "KKS_MaterialEditor"].iter().find_map(|name| {
            let (_, tail) = xml.split_once(&format!("<{name}>"))?;
            Some(tail.split_once(&format!("</{name}>"))?.0.to_string())
        })
    }
    static RECORDED: Mutex<Option<std::collections::HashMap<PathBuf, Option<String>>>> = Mutex::new(None);
    let recorded = crate::gamefs::library_records_zips(&zips);
    if profile_enabled() {
        eprintln!("ok-extract: me sections view zips={} recorded={} known={} first={:?}", zips.len(), recorded.iter().filter(|r| **r).count(),
            RECORDED.lock().unwrap_or_else(|e| e.into_inner()).as_ref().map_or(0, |m| m.len()), zips.first());
    }
    let known: Vec<Option<Option<String>>> = {
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        // The previous process's answers for this library (oklib::library_scan_load).
        if map.is_none() { *map = crate::oklib::library_scan_load::<std::collections::HashMap<PathBuf, Option<String>>>("me-sections-v1"); }
        zips.iter().zip(&recorded).map(|(z, r)| if *r { map.as_ref().and_then(|m| m.get(z).cloned()) } else { None }).collect()
    };
    let fresh_recorded = recorded.iter().zip(&known).any(|(r, k)| *r && k.is_none());
    let rows: Vec<Option<(PathBuf, String)>> = {
        use rayon::prelude::*;
        let fresh: Vec<Option<String>> = crate::oklib::pack_parallel(|| zips.par_iter().zip(known.par_iter()).map(|(zip, known)| match known {
            Some(answer) => answer.clone(),
            None => section(zip),
        }).collect());
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        let map = map.get_or_insert_with(Default::default);
        for ((zip, r), answer) in zips.iter().zip(&recorded).zip(&fresh) { if *r { map.entry(zip.clone()).or_insert_with(|| answer.clone()); } }
        if fresh_recorded { crate::oklib::library_scan_save("me-sections-v1", &*map); }
        zips.iter().zip(fresh).map(|(z, a)| a.map(|a| (z.clone(), a))).collect()
    };
    let out = Arc::new(rows.into_iter().flatten().collect::<Vec<_>>());
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    memo.retain(|(k, _)| *k != key);
    if memo.len() >= 8 { memo.remove(0); }
    memo.push((key, out.clone()));
    out
}

pub(crate) fn me_shader_registered(game_root: &Path, shader: &str) -> bool {
    me_shader_registration(game_root, shader).0
}

pub(crate) fn apply_me_shader(
    game_root: &Path,
    overrides: Option<&crate::card::MeShaderMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &mut ExtractedMesh,
) {let _spot=crate::profile::spot("apply_me_shader");
    let Some(ot) = me_object_type(role) else { return; };
    let Some(slot) = me_slot(role, property) else { return; };
    let Some(material) = mesh.mat.as_mut() else { return; };
    let key = (ot, if ot == 4 { 0 } else { outfit }, slot, crate::card::me_mat_key(&material.name));
    if let Some((shader, queue)) = overrides.and_then(|map| map.get(&key)) {
        // MaterialAPI.SetShader returns false when the requested shader is not
        // in LoadedShaders. Its subsequent SetRenderQueue still runs.
        let (registered, has_rim) = me_shader_registration(game_root, shader);
        if registered {
            material.shader = shader.clone();
            // The previous Shader declaration no longer applies after
            // MaterialAPI.SetShader, including when its answer was false.
            material.shader_has_rim = has_rim;
        }
        if let Some(queue) = queue { material.render_queue = *queue; }
    }
}

pub(crate) fn apply_me_floats(
    overrides: &crate::card::MeFloatMap,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &mut ExtractedMesh,
) {
    let (Some(ot), Some(slot), Some(mat)) = (me_object_type(role), me_slot(role, property), mesh.mat.as_mut()) else { return; };
    let name = crate::card::me_mat_key(&mat.name);
    for ((t, coord, s, material, prop), value) in overrides {
        if *t == ot && *coord == (if ot == 4 { 0 } else { outfit }) && *s == slot && *material == name {
            let key = if prop.starts_with('_') { prop.clone() } else { format!("_{prop}") };
            // Card properties are parsed lowercase, but Unity material keys
            // retain their original case. Replace the old key so an exact
            // material lookup cannot shadow the saved MaterialEditor value.
            let authored_key = mat.floats.keys()
                .find(|existing| existing.eq_ignore_ascii_case(&key))
                .cloned().unwrap_or_else(|| key.clone());
            mat.floats.retain(|existing, _| !existing.eq_ignore_ascii_case(&key));
            mat.floats.insert(authored_key, *value);
        }
    }
}

/// ChaControl writes CustomTextureControl.matDraw settings after texture
/// creation, before MaterialEditor reload. Prefab values are not card gloss.
pub(crate) fn apply_native_skin_draw_settings(mesh: &mut ExtractedMesh, card: &CardInfo, outfit: i32, body: bool) {
    let values=if body {
        [("_DetailNormalMapScale",card.body_detail_power),
         ("_SpecularPower",card.skin_gloss_power+(1.-card.skin_gloss_power)*card.skin_tuya_rate.clamp(0.,1.)),
         ("_SpecularPowerNail",card.nail_gloss_power)]
    } else {
        [("_DetailNormalMapScale",card.face_detail_power),
         ("_SpecularPower",card.cheek_gloss_power),("_SpecularPowerNail",card.lip_gloss_power)]
    };
    if let Some(mat)=mesh.mat.as_mut() {for (key,value) in values {
        mat.floats.retain(|k,_|!k.eq_ignore_ascii_case(key));mat.floats.insert(key.into(),value);
    }}
    // Replay only these implemented draw settings; keep unsupported shader
    // selection and unrelated legacy body overrides outside this change.
    let edits=card.me_float.iter().filter(|((_,_,_,_,key),_)|
        values.iter().any(|(name,_)|key.trim_start_matches('_').eq_ignore_ascii_case(name.trim_start_matches('_'))))
        .map(|(k,v)|(k.clone(),*v)).collect();
    apply_me_floats(&edits,outfit,if body {"body"}else{"head"},None,mesh);
}

/// `MaterialAPI.CopyMaterial` appends a second material to the renderer. Unity
/// draws its last material over the same submesh when there are more materials
/// than mesh submeshes. Replay this before per-material ME overrides.
pub(crate) fn apply_me_material_copies(
    meshes: &mut Vec<ExtractedMesh>,
    copies: &[crate::card::MeMaterialCopy],
    outfit: i32,
    role: &str,
    property: Option<&str>,
) {
    let (Some(ot), Some(slot)) = (me_object_type(role), me_slot(role, property)) else { return; };
    let mut additions = Vec::new();
    for copy in copies.iter().filter(|c| c.object_type == ot && c.coordinate == outfit && c.slot == slot) {
        let candidates: Vec<_> = meshes.iter().chain(additions.iter()).cloned().collect();
        for mesh in candidates {
            let Some(mat) = mesh.mat.as_ref() else { continue; };
            if crate::card::me_mat_key(&mat.name) != copy.source { continue; }
            let mut extra = mesh;
            extra.mat.as_mut().unwrap().name = copy.copy.clone();
            additions.push(extra);
        }
    }
    meshes.extend(additions);
}

/// Accessory colour, in game order:
/// 1. `ChangeAccessoryColor` per renderer group
/// 2. `HairAccessoryCustomizer.UpdateAccessory` when the slot has `HairAccessoryInfo`
/// 3. Material Editor colour properties
/// Albedo is baked with the material shader's ColorMask formula. Material
/// Editor MainTex replaces the source before that bake.
/// `rendAlpha` with `color[3].a == 0` is dropped.
pub(crate) fn color_accessory_meshes(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    card: &CardInfo,
    property: &str,
    outfit: i32,
    meshes: &mut Vec<ExtractedMesh>,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
    selected_hair_gloss: Option<&[u8]>,
) {
    apply_me_material_copies(meshes, &card.me_copies, outfit, "accessory", Some(property));
    let slot = accessory_slot_from_property(property);
    let part = slot.and_then(|s| card.accessory.get(s));
    let card_cols = part
        .map(|a| [a.color, a.color2, a.color3, a.color4])
        .unwrap_or([[1.0; 4]; 4]);
    let mut use_hint = crate::appearance::timings::measure("color.use_hint",||accessory_bundle_shared(game_root, zip, entry)
        .and_then(|b| crate::unity::parse_bundle_ref(&b.0).accessory_color_use(&entry.main_data)));
    // ClothesToAccessories.ConvertToChaAccessoryComponent converts a hair
    // prefab without an accessory component: rendNormal=rendHair,
    // rendAlpha=rendAccessory, useColor01/02/03=true. A genuine accessory's
    // authored renderer map is retained.
    if (101..=104).contains(&entry.category_no) {
        if let Some(hint)=use_hint.as_mut().filter(|h|h.has_hair_component && !h.has_accessory_component) {
            hint.normal=hint.custom_hair.clone();hint.alpha=hint.rend_accessory.clone();
            hint.hair.clear();hint.use01=true;hint.use02=true;hint.use03=true;
        }
    }
    let hac = slot.and_then(|s| card.hair_acc.get(&(outfit, s as i32)).copied());
    if let (Some(hint), Some(info)) = (use_hint.as_ref(), hac) {
        if hint.has_hair_component {
            apply_hair_accessory_length(meshes, hint, info.hair_length);
        }
    }
    let hair0 = &card.hair_parts[0];
    let entry_mask = {
        let n = entry.info("ColorMaskTex");
        if !n.is_empty() && n != "0" {
            Some(n)
        } else {
            None
        }
    };
    let mask_cache: std::sync::Mutex<std::collections::HashMap<String, Option<Vec<u8>>>> = Default::default();
    let color_key = ["_Color", "_Color2", "_Color3"];
    // Each renderer is coloured on its own (texture bakes, alpha, glass: decoded and encoded per texture): renderers
    // are coloured in parallel in the open's pool (its pack), kept in their order.
    let colour = |mut m: ExtractedMesh| -> Option<ExtractedMesh> {
        apply_me_shader(game_root, Some(&card.me_shader), outfit, "accessory", Some(property), &mut m);
        apply_me_floats(&card.me_float, outfit, "accessory", Some(property), &mut m);
        let shader = m
            .mat
            .as_ref()
            .map(|mm| mm.shader.to_ascii_lowercase())
            .unwrap_or_default();
        let in_normal = use_hint
            .as_ref()
            .map(|u| ids_hit(&m.renderer_ids, &u.normal))
            .unwrap_or(false);
        let in_alpha = use_hint
            .as_ref()
            .map(|u| ids_hit(&m.renderer_ids, &u.alpha))
            .unwrap_or(false);
        let in_hair = use_hint
            .as_ref()
            .map(|u| ids_hit(&m.renderer_ids, &u.hair))
            .unwrap_or(false);
        let in_custom_hair = use_hint
            .as_ref()
            .map(|u| u.has_hair_component && ids_hit(&m.renderer_ids, &u.custom_hair))
            .unwrap_or(false);
        let in_rend_acc = use_hint
            .as_ref()
            .map(|u| u.has_hair_component && ids_hit(&m.renderer_ids, &u.rend_accessory))
            .unwrap_or(false);
        m.hair_shader_hint = shader.is_empty() && (in_hair || in_custom_hair);
        if in_custom_hair && hac.is_some() {
            // HairAccessoryCustomizer.UpdateAccessory writes ChaControl's
            // selected texHairGloss, then clears it when HairGloss is false.
            m.hair_gloss_png = selected_hair_gloss.map(|png| png.to_vec());
        }
        let known = use_hint
            .as_ref()
            .map(|u| !u.normal.is_empty() || !u.alpha.is_empty() || !u.hair.is_empty())
            .unwrap_or(false);
        // 0 = authored default, 1 = rendNormal, 2 = rendAlpha, 3 = rendHair.
        let role: u8 = if in_hair {
            3
        } else if in_alpha {
            2
        } else if in_normal {
            1
        } else if !known {
            1
        } else {
            0
        };
        let mut cols = [
            mat_color(&m, color_key[0]),
            mat_color(&m, color_key[1]),
            mat_color(&m, color_key[2]),
            mat_color(&m, "_Color4"),
        ];
        // 1. ChangeAccessoryColor.
        match role {
            1 => {
                let use_i = |i: usize| match use_hint.as_ref() {
                    Some(u) => [u.use01, u.use02, u.use03][i],
                    None => true,
                };
                if use_i(0) {
                    cols[0] = card_cols[0];
                }
                if use_i(1) {
                    cols[1] = card_cols[1];
                }
                if use_i(2) {
                    cols[2] = card_cols[2];
                }
            }
            2 => {
                cols[3] = card_cols[3];
            }
            3 => {
                let s = hair0.start_color;
                cols[0] = s;
                cols[1] = s;
                cols[2] = s;
            }
            _ => {}
        }
        // 2. HairAccessoryCustomizer.UpdateAccessory (postfix, so it wins).
        if let Some(info) = hac {
            if in_custom_hair {
                if info.color_match {
                    cols[0] = hair0.color;
                    cols[1] = hair0.start_color;
                    cols[2] = hair0.end_color;
                }
                // HairAccessoryCustomizer.UpdateAccessory writes the hair
                // outline colour after ChangeAccessoryColor. The authored
                // bundle value can be a placeholder (red on some mod hairs).
                let outline = if info.color_match { hair0.outline_color } else { info.outline_color };
                set_material_color(&mut m, "_LineColor", outline);
                set_material_color(&mut m,"_GlossColor",if info.color_match {hair0.gloss_color} else {info.gloss_color});
                if !info.hair_gloss {
                    m.hair_gloss_png = None;
                }
            }
            if in_rend_acc {
                cols[0] = info.accessory_color;
                cols[1] = info.accessory_color;
                cols[2] = info.accessory_color;
            }
        }
        // 3. Material Editor colours override the shader colours.
        for (i, key) in color_key.iter().enumerate() {
            if let Some(c) = me_shader_color(card, outfit, property, &m, key) {
                cols[i] = c;
            }
        }
        if let Some(c) = me_shader_color(card, outfit, property, &m, "_LineColor") {
            set_material_color(&mut m, "_LineColor", c);
        }
        if let Some(c)=me_shader_color(card,outfit,property,&m,"_GlossColor"){set_material_color(&mut m,"_GlossColor",c);}
        if let Some(c) = me_shader_color(card, outfit, property, &m, "_Color4") {
            cols[3] = c;
        }
        // rendAlpha && color[3].a == 0: the game disables that renderer.
        if role == 2 && cols[3][3] == 0.0 {
            eprintln!("ok-extract: skip rendAlpha a=0 {}", m.name);
            return None;
        }
        let mask_name = m
            .mat
            .as_ref()
            .and_then(|mm| mm.slot("_ColorMask"))
            .map(|s| s.to_string())
            .filter(|s| !s.is_empty())
            .or_else(|| entry_mask.clone());
        let me_mask_png = me_texture(Some(&card.me_tex), outfit, "acc", Some(property), &m, "colormask")
            .map(|png| png.to_vec());
        let mask_from_me = me_mask_png.is_some();
        let mask_png = me_mask_png.or_else(|| m.color_mask_png.clone()).or_else(|| {
            mask_name.as_ref().and_then(|n| {
                let cached = mask_cache.lock().unwrap_or_else(|e| e.into_inner()).get(n).cloned();
                cached.unwrap_or_else(|| {
                    let png = png_from_list_entry(game_root, entry, &[n.as_str()], head_texs, body_texs);
                    mask_cache.lock().unwrap_or_else(|e| e.into_inner()).insert(n.clone(), png.clone());
                    png
                })
            })
        });
        // ME SetTexture replaces the albedo, then the shader still multiplies
        // ColorMask colours. A card TextureDictionary PNG is that source, not a
        // captured RenderTexture, so bake after the swap. White hair colours
        // (March 7th) leave an already-pink ME texture unchanged.
        let me_main_png = me_maintex(Some(&card.me_tex), outfit, "acc", Some(property), &m)
            .map(|png| png.to_vec());
        let main_from_me = me_main_png.is_some();
        if let Some(me) = me_main_png {
            m.texture_png = Some(me);
            mark_material_editor_main_texture(&mut m);
        }
        // Studio items (main_item_studio, _alpha) draw through the same ColorMask mix (SessionPart item_draw). A
        // Material Editor shader swap onto a hair accessory keeps the hair material's own colours; the colours the
        // game writes (ChangeAccessoryColor, HairAccessoryCustomizer) must reach the material, not a baked albedo.
        let studio_item = shader.contains("main_item_studio") && !shader.contains("_add");
        if studio_item || matches!(shader.as_str(),"xukmi/mainitemplus"|"shader forge/main_item"|"shader forge/main_alpha"|"shader forge/main_opaque") {
            // Keep shader inputs separate. Its fragment performs linear
            // ColorMask mixing; baking gamma colours here double-applies them.
            for (key,color) in color_key.iter().zip(cols.iter()) {set_material_color(&mut m,key,*color);}
            if mask_from_me {mark_material_editor_texture(&mut m,"_ColorMask");}
            m.color_mask_png=mask_png;
            if studio_item {
                // A serialized null _MainTex samples the shader's white default, not a bundle fallback texture.
                let null_main=m.mat.as_ref().is_some_and(|mat| mat.slot("_MainTex").is_none_or(str::is_empty));
                if m.texture_png.is_none() || (null_main && !main_from_me) {m.texture_png=crate::unity::solid_png([1.0;4],8,8);}
                scale_studio_item_alpha(&mut m,&shader);
            }
            return Some(m);
        }
        if is_native_hair_shader(&shader) || m.hair_shader_hint {
            // Shader Forge/main_hair samples the full-resolution ColorMask in
            // its pixel program. Baking it into a null MainTex's 8x8 white
            // fallback destroys the mask's strand detail and its mip chain.
            for (key, color) in color_key.iter().zip(cols.iter()) {
                set_material_color(&mut m, key, *color);
            }
            if mask_from_me {
                // MaterialEditor replaces this Texture2D with a runtime RT.
                // Its authored sampler metadata no longer describes the input.
                m.runtime_textures.remove("_ColorMask");
            }
            m.color_mask_png = mask_png;
            // A serialized null _MainTex uses the shader's white default.
            // ParsedBundle's convenience texture fallback can be another slot.
            let null_main=m.mat.as_ref().is_some_and(|mat| mat.slot("_MainTex").is_none_or(str::is_empty));
            if m.texture_png.is_none() || (null_main && !main_from_me) {
                m.texture_png=crate::unity::solid_png([1.0;4],8,8);
            }
            return Some(m);
        }
        // The native glasses shader declares a white _MainTex default.
        // A null texture is valid, and _Color4's alpha remains a shader input.
        let is_glasses = shader == "koikano/main_clothes_item_glasses"
            || shader == "shader forge/toon_glasses_lod0";
        if is_glasses && role == 2 {
            // Preserve native inputs separately from the legacy preview bake.
            for (key, color) in color_key.iter().zip(cols.iter()) {
                set_material_color(&mut m, key, *color);
            }
            set_material_color(&mut m, "_Color4", cols[3]);
            let null_main = m.mat.as_ref().is_some_and(|mat| mat.slot("_MainTex").is_none_or(str::is_empty));
            let native_main = if null_main && !main_from_me { crate::unity::solid_png([1.;4], 1, 1) }
                else { m.texture_png.clone().or_else(|| crate::unity::solid_png([1.;4], 1, 1)) };
            if let Some(png) = native_main { m.shader_extra_textures.insert("_MainTex".into(), png); }
            let png = m.texture_png.as_deref();
            m.texture_png = bake_accessory_glass_color(png, cols[3]);
            return Some(m);
        }
        let Some(png) = m.texture_png.clone() else {
            return Some(m);
        };
        let cols4 = [cols[0], cols[1], cols[2], [1.0, 1.0, 1.0, 1.0]];
        // ME PNGs and static bundle textures have opposite row directions.
        // Bake in the albedo's space: either mixed-source ordering must align
        // the mask, while two textures from the same source stay unchanged.
        let aligned_mask = if main_from_me != mask_from_me {
            mask_png.as_deref().and_then(flip_png_v)
        } else {
            None
        };
        let bake_mask = aligned_mask.as_deref().or(mask_png.as_deref());
        // No ColorMask: the shader samples black, so albedo stays the texture.
        // Do not invent a multiply. `rendAlpha` is `_Color4`, not the mask.
        let tinted = if role == 2 {
            crate::unity::tint_png(&png, cols[3])
        } else if is_native_hair_shader(&shader) || m.hair_shader_hint {
            // Renderer groups decide which colours ChangeAccessoryColor writes;
            // the material shader decides how its ColorMask combines them.
            // Many mod bundles omit the external shader asset, leaving its
            // name unresolved. ChaCustomHairComponent.rendHair then provides
            // the game's own hair-renderer classification.
            crate::unity::hair_get_diffuse(&png, bake_mask, cols[0], cols[1], cols[2])
        } else if let Some(mask_png) = bake_mask {
            apply_clothes_color_mask(&png, mask_png, cols4, &[])
        } else {
            None
        };
        if std::env::var("OK_ACCCOLOR").is_ok() {
            eprintln!(
                "ok-extract: ACCCOLOR {} role={} hair={} rendAcc={} match={:?} cols={:?} before={:?} after={:?}",
                m.name,
                role,
                in_custom_hair,
                in_rend_acc,
                hac.map(|h| h.color_match),
                cols4,
                png_mean(&png),
                tinted.as_deref().and_then(png_mean)
            );
        }
        if let Some(t) = tinted {
            m.texture_png = Some(t);
        }
        scale_studio_item_alpha(&mut m,&shader);
        Some(m)
    };
    use rayon::prelude::*;
    let drained: Vec<ExtractedMesh> = meshes.drain(..).collect();
    *meshes = crate::oklib::pack_parallel(|| drained.into_par_iter().filter_map(colour).collect());
}

/// Material Editor copies the source material, switches its shader, then
/// applies numeric properties. `main_item_studio_alpha` multiplies the
/// sampled alpha by `_alpha`; preserving the copied texture alone made
/// translucent hair overlays appear as solid sheets.
pub(crate) fn scale_studio_item_alpha(m: &mut ExtractedMesh, shader: &str) {
    if !shader.contains("main_item_studio_alpha") { return; }
    let alpha = m.mat.as_ref().and_then(|mat| mat.float("_alpha")).unwrap_or(1.0);
    if alpha < 1.0 {
        if let Some(png) = m.texture_png.take() {
            m.texture_png = crate::unity::scale_png_alpha(&png, alpha).or(Some(png));
        }
    }
}

pub(crate) fn bake_accessory_glass_color(png: Option<&[u8]>, color: [f32; 4]) -> Option<Vec<u8>> {
    let mut image = match png {
        Some(bytes) => crate::teximg::load_from_memory(bytes).ok()?.to_rgba8(),
        None => image::RgbaImage::from_pixel(1, 1, image::Rgba([255; 4])),
    };
    for pixel in image.pixels_mut() {
        for i in 0..4 {
            pixel[i] = (pixel[i] as f32 * color[i].clamp(0., 1.)).round() as u8;
        }
    }
    crate::teximg::encode_rgba_owned(image)
}

/// Card accessory colours for a KKEx resolve property (slot index), if any.
pub(crate) fn accessory_colors(card: &CardInfo, property: &str) -> Option<[[f32; 4]; 4]> {
    let slot = accessory_slot_from_property(property)?;
    let a = card.accessory.get(slot)?;
    Some([a.color, a.color2, a.color3, a.color4])
}

#[allow(dead_code)]
pub(crate) fn acc_tint_for(color: [f32; 4]) -> Option<[f32; 4]> {
    if (color[0] - 1.0).abs() < 0.02
        && (color[1] - 1.0).abs() < 0.02
        && (color[2] - 1.0).abs() < 0.02
    {
        None
    } else {
        Some(color)
    }
}
