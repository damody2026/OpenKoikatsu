//! OK base library converter: turns every shared character resource of the
//! installation (vanilla `abdata` and character mods) into OK models.
//!
//! No card is involved. Each Unity source is opened once and everything the
//! character pipeline can ask of it is converted: texture and prefab
//! directories, every texture (PNG, sampling, colour space, GPU texels), every
//! prefab (meshes with and without disabled renderers, skeletons, bone
//! implants, colour use, renderer maps, face/eye settings), materials,
//! shaders, dynamic bones and the body rig data. The game tree itself (lists,
//! mod manifests, configs, which files exist) is logged by `gamefs`.
//! Resumable: finished sources are listed in `converted.tsv`.
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use crate::unity::ParsedBundle;

/// A Unity source to convert: a file under the game root or a zipmod entry.
#[derive(Clone)]
struct Source { label: String, path: PathBuf, entry: Option<String> }

const CHARACTER_PREFIXES: [&str; 4] = ["abdata/chara/", "abdata/list/characustom/", "abdata/studio/base/", "abdata/list/shapecorrect/"];
/// Vanilla Studio items, lights and maps (game files only; mod items come in per scene). Animations and sounds are
/// not item sources.
const STUDIO_PREFIX: &str = "abdata/studio/";
const STUDIO_SKIP: [&str; 2] = ["abdata/studio/anime/", "abdata/studio/sound/"];
const BODY_RIGS: [&str; 2] = ["p_cf_body_bone", "p_cf_head_bone"];
const FULL_SKELETON_PROBES: [&str; 3] = ["", "cf_J_hairF_top", "k_f_kosi02_01"];
const FACE_CONTROLLERS: [&str; 3] = ["MouthCtrl", "EyesCtrl", "EyebrowCtrl"];
const CREATE_SHADERS: [&str; 2] = ["Koikano/create_skinface", "Koikano/create_topN"];

fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = crate::gamefs::read_dir(dir) else { return; };
    for entry in entries.flatten() {
        let path = entry.path();
        if entry.file_type().is_ok_and(|t| t.is_dir()) { walk(&path, out); } else { let _ = crate::gamefs::metadata(&path); out.push(path); }
    }
}

fn rel(game: &Path, path: &Path) -> String {
    path.strip_prefix(game).unwrap_or(path).to_string_lossy().replace('\\', "/").to_ascii_lowercase()
}

/// Log the installation (directory trees, configs, plugin presence, mod
/// manifests and lists) and collect the character sources.
fn inventory(game: &Path) -> Vec<Source> {
    let mut sources = Vec::new();
    let mut files = Vec::new();
    // Root-level markers (require_complete_mods, ...): the listing proves absence.
    let _ = crate::gamefs::read_dir(game).map(|it| it.count());
    for dir in ["abdata", "BepInEx/plugins", "BepInEx/config", "UserData/config", "kks_decomp/plugins"] { walk(&game.join(dir), &mut files); }
    for file in &files {
        let r = rel(game, file);
        let text = [".cfg", ".xml", ".csv", ".txt", ".png"].iter().any(|e| r.ends_with(e));
        if text && !r.starts_with("abdata/") || r.starts_with("abdata/") && r.ends_with(".png") { let _ = crate::gamefs::read(file); }
        let bundle = r.ends_with(".unity3d") || (r.starts_with("abdata/") && !r.contains('.'));
        let studio = r.starts_with(STUDIO_PREFIX) && !STUDIO_SKIP.iter().any(|p| r.starts_with(p));
        let character = CHARACTER_PREFIXES.iter().any(|p| r.starts_with(p)) || r.starts_with("kks_decomp/plugins/") || studio;
        if bundle && character { sources.push(Source { label: r, path: file.clone(), entry: None }); }
    }
    let managers = game.join("CharaStudio_Data/globalgamemanagers");
    if crate::gamefs::read(&managers).is_ok() { let _ = crate::clothes_gpu::quality(game); }
    let mut zips = Vec::new();
    walk(&game.join("mods"), &mut zips);
    zips.retain(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("zipmod") || e.eq_ignore_ascii_case("zip")));
    zips.sort();
    for zip in zips {
        let Some(mut archive) = crate::gamefs::open_zip(&zip) else { continue; };
        let names = archive.file_names();
        let mut manifest = String::new();
        for (i, name) in names.iter().enumerate() {
            let n = name.replace('\\', "/").to_ascii_lowercase();
            if [".xml", ".csv", ".txt"].iter().any(|e| n.ends_with(e)) || n.ends_with(".png") && n.starts_with("abdata/") {
                if let Ok(mut f) = archive.by_index(i) {
                    let mut bytes = Vec::new();
                    if f.read_to_end(&mut bytes).is_ok() && n == "manifest.xml" { manifest = String::from_utf8_lossy(&bytes).to_ascii_lowercase(); }
                }
            }
        }
        // Character mods: list/chara bundles, uncensors (body parts) and
        // Material Editor shader packs.
        let uncensor_or_shader = ["<body", "<penis", "<balls", "<materialeditor"].iter().any(|t| manifest.contains(t));
        let character_zip = uncensor_or_shader || names.iter().any(|n| { let n = n.replace('\\', "/").to_ascii_lowercase(); CHARACTER_PREFIXES.iter().any(|p| n.starts_with(p)) || n.ends_with(".csv") });
        if !character_zip { continue; }
        for name in names {
            let n = name.replace('\\', "/").to_ascii_lowercase();
            if !n.ends_with(".unity3d") || n.contains("/thumb/") { continue; }
            sources.push(Source { label: format!("{}|{n}", rel(game, &zip)), path: zip.clone(), entry: Some(name) });
        }
    }
    sources
}

fn read_source(source: &Source) -> Option<Vec<u8>> {
    match &source.entry {
        None => crate::gamefs::read(&source.path).ok(),
        Some(name) => {
            let mut zip = crate::gamefs::open_zip(&source.path)?;
            let mut f = zip.by_name(name).ok()?;
            let mut bytes = Vec::new();
            f.read_to_end(&mut bytes).ok()?;
            Some(bytes)
        }
    }
}

/// List entries (vanilla and mods) per source: their prefab names and the
/// categories they serve.
#[derive(Default)]
struct Hints { prefabs: BTreeMap<String, BTreeSet<String>>, categories: BTreeMap<String, BTreeSet<i32>> }

fn hints(game: &Path) -> Hints {
    let mut h = Hints::default();
    let mut add = |key: String, entry: &crate::list::ListEntry| {
        for name in [entry.main_data.clone(), entry.info("MainData")] { if !name.is_empty() && name != "0" { h.prefabs.entry(key.clone()).or_default().insert(name); } }
        h.categories.entry(key).or_default().insert(entry.category_no);
    };
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    for entry in vanilla.by_id.values() {
        if !entry.main_ab.is_empty() {
            let ab = entry.main_ab.replace('\\', "/").to_ascii_lowercase();
            add(format!("abdata/{ab}"), entry);
            add(format!("file:{}", ab.rsplit('/').next().unwrap_or(&ab)), entry);
        }
    }
    let _ = crate::list::load_mods_catalog(&game.join("mods"));
    // Per zipmod, not the merged catalog: ids that collide across mods are
    // dropped from the merged one but still resolved by GUID to their own zip.
    let mut zips = Vec::new();
    walk(&game.join("mods"), &mut zips);
    zips.retain(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("zipmod") || e.eq_ignore_ascii_case("zip")));
    let per_zip: Vec<(PathBuf, crate::list::ChaListCatalog)> = zips.into_iter().map(|z| { let c = crate::list::catalog_from_zipmod(&z); (z, c) }).collect();
    for (zip, catalog) in &per_zip {
      for entry in catalog.by_id.values() {
        let zip = zip.to_string_lossy().into_owned();
        let zip = &zip;
        let ab = entry.main_ab.replace('\\', "/").to_ascii_lowercase();
        if ab.is_empty() { continue; }
        // Keyed by zip + bundle file name; the zip entry path ends with main_ab.
        add(format!("{}|{}", rel(game, Path::new(zip)), ab.rsplit('/').next().unwrap_or(&ab)), entry);
        // A list zip can name bundles of another zip: also by file name.
        add(format!("file:{}", ab.rsplit('/').next().unwrap_or(&ab)), entry);
      }
    }
    // Studio lists name the item, light and map prefabs of their bundles.
    for (bundle, names) in crate::studio_items::prefab_hints(&crate::studio_items::catalog(game)) { h.prefabs.entry(bundle).or_default().extend(names); }
    // The pipeline also builds these game-level indexes; log what they read.
    let _ = crate::mods::find_guids(&game.join("mods"), None);
    let _ = crate::mods::override_index(&game.join("mods"));
    h
}

fn source_hints<'a>(h: &'a Hints, source: &Source) -> (BTreeSet<String>, BTreeSet<i32>) {
    let mut prefabs = BTreeSet::new();
    let mut cats = BTreeSet::new();
    let keys = match &source.entry {
        None => vec![source.label.clone(), format!("file:{}", source.label.rsplit('/').next().unwrap_or(&source.label))],
        Some(name) => {
            let file = name.replace('\\', "/").to_ascii_lowercase();
            let zip = source.label.split('|').next().unwrap_or_default().to_owned();
            // Vanilla entries overridden by this zip use the abdata key.
            vec![format!("{zip}|{}", file.rsplit('/').next().unwrap_or(&file)), file.clone(), format!("file:{}", file.rsplit('/').next().unwrap_or(&file))]
        }
    };
    for key in keys {
        if let Some(p) = h.prefabs.get(&key) { prefabs.extend(p.iter().cloned()); }
        if let Some(c) = h.categories.get(&key) { cats.extend(c.iter().copied()); }
    }
    (prefabs, cats)
}

/// Every query of one source.
fn convert_source(game: &Path, source: &Source, bytes: &[u8], prefab_hints: &BTreeSet<String>, cats: &BTreeSet<i32>) -> String {
    let parsed: std::sync::Arc<ParsedBundle> = crate::unity::parse_bundle_cached(bytes.to_vec());
    match &source.entry {
        Some(_) => parsed.configure_game_references_from_zip(game, Some(&source.path)),
        None => parsed.configure_game_references(game),
    }
    let label = source.label.as_str();
    // Textures.
    let tex = parsed.texture_directory();
    for key in tex.exact.keys() {
        let _ = parsed.texture_png_named(key);
        let _ = parsed.texture_sampling_named(key);
        let _ = parsed.library_texture_color_space(key);
        // GPU texels share their chunks with the texel payload: always kept.
        let _ = parsed.texture_gpu_named(key);
    }
    let _ = parsed.textures_keep_base();
    // Name lookups fall back to the whole-bundle (legacy) reader when the
    // directory is incomplete or a texture does not decode.
    if !tex.complete || tex.exact.keys().any(|k| parsed.texture_png_named(k).is_none()) { let _ = crate::unity::legacy_texture_payloads(bytes); }
    // Prefabs.
    let clothes = cats.iter().any(|c| (105..=112).contains(c) || (200..=212).contains(c)) || (cats.is_empty() && label.contains('|'));
    let dir = parsed.prefab_directory();
    let mut prefabs: BTreeSet<String> = dir.top.iter().cloned().collect();
    for roots in dir.roots.iter().flatten() { prefabs.extend(roots.iter().cloned()); }
    prefabs.extend(prefab_hints.iter().cloned());
    let mut meshes = 0;
    let mut skeletons: Vec<crate::pose::Skeleton> = Vec::new();
    for name in &prefabs {
        let _ = parsed.skeleton_for_prefab_name(name);
        for include_disabled in [false, true] {
            let (list, _) = if include_disabled { parsed.meshes_for_maindata_including_disabled(name) } else { parsed.meshes_for_maindata(name) };
            for m in &list {
                let _ = parsed.skeleton(m.path_id);
                if let Some(sk) = &m.smr_skel { let _ = parsed.bone_implants_for_skeleton(sk); if source.entry.is_some() { skeletons.push(sk.clone()); } }
                if !m.renderer_ids.is_empty() { let _ = parsed.renderer_material_slot(&m.renderer_ids, 1); }
            }
            meshes += list.len();
        }
        let _ = parsed.accessory_color_use(name);
        let _ = parsed.accessory_body_binding(name);

        // Renderer maps / state nodes are read for clothes only.
        if clothes {
            let _ = crate::unity::clothes_renderer_map(bytes, name);
            let _ = crate::unity::prefab_node_names(bytes, name);
        }
        // Head bundles (whatever list names them): they carry the face mesh.
        if cats.contains(&100) || label.contains("bo_head") || dir.meshes.contains("cf_o_face") {
            let _ = parsed.face_runtime_settings(name);
            let _ = parsed.eye_material_settings(name);
            for c in FACE_CONTROLLERS { let _ = parsed.face_control_patterns(name, c); }
        }
    }
    // Accessory component lookups by any GameObject that owns components
    // (lists can name a child object, e.g. a hair part).
    let components = parsed.library_component_objects();
    for name in &components {
        if prefabs.contains(name) { continue; }
        let _ = parsed.accessory_color_use(name);
        let _ = parsed.accessory_body_binding(name);
    }
    if source.entry.is_some() {
        // also without the zip context (as for bodies and overrides)
        parsed.configure_game_references_from_zip(game, None);
        for name in prefabs.iter().chain(&components) { let _ = parsed.accessory_body_binding(name); }
        parsed.configure_game_references_from_zip(game, Some(&source.path));
    }
    // Rigs and body data.
    for rig in BODY_RIGS {
        if !parsed.contains_prefab(rig) { continue; }
        if let Some(sk) = parsed.skeleton_for_prefab_name(rig) {
            let _ = parsed.body_dynamics_for_skeleton(&sk);
            let _ = parsed.body_colliders_for_skeleton(&sk);
        }
        let _ = parsed.studio_pv_config(rig);
        let _ = parsed.eye_look_config(rig);
        let _ = parsed.prefab_has_component_field(rig, "eyeLookScript");
    }
    for probe in FULL_SKELETON_PROBES { let _ = parsed.skeleton_full_named(probe); }
    parsed.library_prepare_dbones();
    // Script-resolved data of a zipmod bundle is also read without the zip
    // context (bodies, overrides loaded as game bundles): record both.
    if source.entry.is_some() {
        // No zip origin (configure_game_references alone keeps the origin).
        parsed.configure_game_references_from_zip(game, None);
        parsed.library_prepare_dbones();
        for rig in BODY_RIGS {
            if !parsed.contains_prefab(rig) { continue; }
            if let Some(sk) = parsed.skeleton_for_prefab_name(rig) {
                let _ = parsed.body_dynamics_for_skeleton(&sk);
                let _ = parsed.body_colliders_for_skeleton(&sk);
            }
        }
        for sk in &skeletons { let _ = parsed.bone_implants_for_skeleton(sk); }
        for name in &prefabs { let _ = parsed.accessory_body_binding(name); }
    }
    let file_name = label.rsplit(['/', '|']).next().unwrap_or(label).to_owned();
    // Whole-bundle loads: the base body/head bundles (and their sideloader
    // overrides, same file name) and head items (cat 100).
    let base = ["oo_base.unity3d", "bo_head_00.unity3d"].contains(&file_name.as_str());
    // Texture-only bundles are read whole too (entries without a prefab).
    if base || cats.contains(&100) || dir.meshes.contains("cf_o_face") || prefabs.is_empty() {
        for m in parsed.decode_all().0 { if !m.renderer_ids.is_empty() { let _ = parsed.renderer_material_slot(&m.renderer_ids, 1); } }
    }
    if base {
        let _ = crate::unity::load_base_bundle_from_memory(bytes.to_vec(), &file_name);
        if file_name == "oo_base.unity3d" {
            for m in parsed.decode_all().0 {
                let _ = crate::unity::skeleton_for_mesh_memory(bytes, &m.name);
                if source.entry.is_none() { let _ = crate::unity::skeleton_for_mesh(&source.path, &m.name); }
            }
        }
    }
    // Materials and shaders.
    let (materials, shaders) = parsed.library_inventory();
    for m in &materials { let _ = parsed.material_named(m); }
    for s in &shaders {
        let _ = crate::unity::shader_bundle_declares_property(bytes, s, "_rimV");
        let _ = crate::unity::shader_bundle_has_dxbc(bytes, s);
        let _ = crate::unity::shader_bundle_has_legacy_dxbc(bytes, s);
    }
    for s in CREATE_SHADERS { if shaders.iter().any(|n| n == s) { let _ = parsed.shader_pixel_dxbc_named(s); } }
    if label.contains("list/") { let _ = crate::list::text_assets_from_bytes(bytes); }
    // Shape-key tables and lists are TextAssets in arbitrary bundles.
    crate::list::text_assets_status_from_bytes(bytes);
    format!("textures={} prefabs={} meshes={meshes} materials={} shaders={}", tex.exact.len(), prefabs.len(), materials.len(), shaders.len())
}

/// `convert <lib> [threads] [filter]`
pub fn convert(game: &Path, lib: &Path, threads: usize, filter: Option<&str>) -> Result<(), String> {
    use rayon::prelude::*;
    use std::io::Write;
    let started = std::time::Instant::now();
    let store = crate::oklib::activate(lib, crate::oklib::Mode::Record)?;
    crate::oklib::CONVERTING.store(true, std::sync::atomic::Ordering::Relaxed);
    crate::gamefs::activate(crate::gamefs::Mode::Record, game, lib)?;
    let mut sources = inventory(game);
    let h = hints(game);
    crate::gamefs::save(lib)?;
    if let Some(list) = filter.and_then(|f| f.strip_prefix('@')) {
        let wanted: HashSet<String> = std::fs::read_to_string(list).map_err(|e| format!("{list}: {e}"))?.lines().map(|l| l.trim().to_ascii_lowercase()).filter(|l| !l.is_empty()).collect();
        sources.retain(|s| wanted.contains(&s.label));
    } else if let Some(prefix) = filter.and_then(|f| f.strip_prefix('^')) {
        let p = prefix.to_ascii_lowercase(); sources.retain(|s| s.label.starts_with(&p));
    } else if let Some(filter) = filter { let f = filter.to_ascii_lowercase(); sources.retain(|s| s.label.contains(&f)); }
    let done: HashSet<String> = std::fs::read_to_string(lib.join("converted.tsv")).unwrap_or_default().lines()
        .filter_map(|l| l.split('\t').next().map(str::to_owned)).collect();
    // An explicit source list is converted even when listed as done: the
    // queries it lacks are added, existing records are kept.
    if !filter.is_some_and(|f| f.starts_with('@')) { sources.retain(|s| !done.contains(&s.label)); }
    println!("OKCONVERT sources={} done={} inventory {:.0}s", sources.len(), done.len(), started.elapsed().as_secs_f64());
    let log = Mutex::new(std::fs::OpenOptions::new().create(true).append(true).open(lib.join("converted.tsv")).map_err(|e| e.to_string())?);
    let count = std::sync::atomic::AtomicUsize::new(0);
    let total = sources.len();
    let pool = rayon::ThreadPoolBuilder::new().num_threads(threads.max(1)).build().map_err(|e| e.to_string())?;
    pool.install(|| sources.par_iter().for_each(|source| {
        let t0 = std::time::Instant::now();
        let (prefabs, cats) = source_hints(&h, source);
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let bytes = read_source(source).ok_or("unreadable")?;
            if !crate::oklib::is_unity_bundle(&bytes) { return Ok::<_, String>("not a Unity bundle".to_owned()); }
            let id = crate::oklib::id_hex(&crate::oklib::content_id(&bytes));
            let detail = if std::env::var("OKLIB_ONLY").as_deref() == Ok("text_assets") {
                crate::list::text_assets_status_from_bytes(&bytes); "text assets".to_owned()
            } else { convert_source(game, source, &bytes, &prefabs, &cats) };
            crate::unity::clear_bundle_cache();
            store.forget_model(&crate::oklib::content_id(&bytes));
            Ok(format!("{id} {detail}"))
        })).unwrap_or_else(|_| Err("panic".into()));
        let (status, detail) = match result { Ok(d) => ("ok", d), Err(e) => ("error", e) };
        let n = count.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
        let line = format!("{}\t{status}\t{:.1}\t{detail}\n", source.label, t0.elapsed().as_secs_f64());
        let _ = log.lock().unwrap().write_all(line.as_bytes());
        if n % 25 == 0 || n == total {
            store.flush();
            let _ = crate::gamefs::save(lib);
            println!("OKCONVERT {n}/{total} {:.0}s stored={}", started.elapsed().as_secs_f64(), store.stored.load(std::sync::atomic::Ordering::Relaxed));
        }
    }));
    // Studio catalogue with the library model of every item, light and map bundle.
    let mut studio = crate::studio_items::catalog(game);
    let unresolved = crate::studio_items::resolve_models(game, &mut studio);
    crate::studio_items::write(lib, &studio)?;
    println!("OKCONVERT studio items={} maps={} lights={} unresolved={unresolved}", studio.items.len(), studio.maps.len(), studio.lights.len());
    crate::gamefs::save(lib)?;
    crate::oklib::deactivate();
    crate::gamefs::deactivate();
    println!("OKCONVERT finished {total} sources in {:.0}s", started.elapsed().as_secs_f64());
    crate::oklib::print_profile();
    Ok(())
}
