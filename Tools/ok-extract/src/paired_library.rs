//! The OK base library's animation root (`<lib>/paired`): the shared paired / common animation resources
//! (PairedAnimations: H pairs and interaction poses, their controllers, clips and IK data, Maker and Studio poses)
//! converted into OK data, so no running session reads a Unity bundle for them.
//!
//! The animation system already keeps decoded data in its own format (`anim::disk_cache`: controller graphs, clip
//! catalogues, decoded clips, avatar TOS, yure policies, pair catalogues, paired IK, the body-bone skeleton). A
//! conversion runs every query the shared resources can answer against the original root with `set_export`, so
//! each decoded entry is written into `<out>/cache`; `okpaired.json` marks the root and lists what was converted.
//! Sessions given the library root answer from those entries and record a miss instead of opening a bundle.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::anim::disk_cache;

pub const FORMAT: &str = "ok-paired-1";

/// What a conversion covered (stored as `okpaired.json`, the root's marker).
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Inventory {
    pub format: String,
    /// Converted from (folder name only).
    pub source: String,
    /// Animation sources whose names (clips or controller states) were converted.
    pub sources: Vec<String>,
    /// `source|name` keys of those sources (the states and clips the library can play).
    pub keys: Vec<String>,
    /// Raw clips (`source|clip`) the keys play, each decoded once.
    pub leaves: usize,
    /// Raw clips the original could not decode either (unsupported curves).
    pub unsupported: Vec<String>,
    /// Paired layer clips: (key, layer, resolved clip).
    pub pair_layers: Vec<(String, i32, String)>,
    pub pairs: usize,
    pub poses: usize,
    pub motions: usize,
    pub ik_config: bool,
    /// The pack (`cache/pack.bin`): entries, distinct contents, bytes.
    #[serde(default)]
    pub pack_entries: usize,
    #[serde(default)]
    pub pack_contents: usize,
    #[serde(default)]
    pub pack_bytes: u64,
}

fn hex(bytes: &[u8]) -> String { bytes.iter().map(|b| format!("{b:02X}")).collect() }

/// `states:controller@` / `states:studio@` source of one controller of a bundle.
fn controller_source(kind: &str, bundle: &str, owner: &str) -> String {
    let json = serde_json::to_vec(&[bundle, owner, "", ""]).unwrap_or_default();
    format!("states:{kind}@{}", hex(&json))
}

/// Animation bundles of a root (relative to `abdata`, '/'), without the pair lists, the body-bone prefab and oo_base.
fn bundles(root: &Path) -> Vec<String> {
    fn walk(dir: &Path, base: &Path, out: &mut Vec<String>) {
        let Ok(rd) = std::fs::read_dir(dir) else { return; };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() { walk(&p, base, out); continue; }
            if !p.extension().is_some_and(|x| x.eq_ignore_ascii_case("unity3d")) { continue; }
            let Ok(rel) = p.strip_prefix(base) else { continue; };
            let rel = rel.to_string_lossy().replace('\\', "/");
            if rel.starts_with("h/list/") || rel.starts_with("studio/base/") || rel.starts_with("chara/") { continue; }
            out.push(rel);
        }
    }
    let base = root.join("abdata");
    let mut out = Vec::new();
    walk(&base, &base, &mut out);
    out.sort();
    out
}

#[derive(Deserialize)]
struct Demos { #[serde(default)] demos: Vec<Demo> }
#[derive(Deserialize)]
struct Demo { #[serde(default)] key: String, #[serde(default)] source: String }

fn demos(root: &Path) -> Vec<Demo> {
    std::fs::read(root.join("editor-demos.json")).ok().and_then(|b| serde_json::from_slice::<Demos>(&b).ok()).map(|d| d.demos).unwrap_or_default()
}

/// Every source the root can answer for: its demos and pairs, each bundle as clips and as controller states, and
/// each controller of a bundle in both the game and the Studio scene profile.
fn sources_of(root: &Path) -> BTreeSet<String> {
    let mut sources = BTreeSet::new();
    for d in demos(root) { if !d.source.is_empty() { sources.insert(d.source); } }
    for p in crate::h_playback::pairs(root).iter().chain(crate::h_playback::interaction_poses(root).iter()) {
        for s in [&p.female_source, &p.male_source] { if !s.is_empty() { sources.insert(s.clone()); } }
    }
    for b in bundles(root) {
        sources.insert(b.clone());
        let states = format!("states:{b}");
        let owners: BTreeSet<String> = crate::anim::clip_names_for_source(root, &states).iter()
            .filter_map(|l| l.split_once(" / ").map(|(o, _)| o.to_owned())).collect();
        sources.insert(states);
        for o in owners {
            sources.insert(controller_source("controller", &b, &o));
            sources.insert(controller_source("studio", &b, &o));
        }
    }
    sources
}

/// Converts the shared animation resources at `source` (a PairedAnimations root) into the library root `out`.
pub fn convert(source: &Path, out: &Path) -> Result<Inventory, String> {
    if !source.join("abdata").is_dir() { return Err(format!("{}: no abdata folder", source.display())); }
    if !source.join("manifest.json").is_file() { return Err(format!("{}: no manifest.json (the revision of the converted data)", source.display())); }
    let cache = out.join("cache");
    // `cache/` is this conversion's own output (an interrupted run leaves no marker): start it afresh, so no entry
    // of an earlier run stays behind. Nothing else in the folder is touched.
    if cache.is_dir() {
        if std::fs::symlink_metadata(&cache).map(|m| m.file_type().is_symlink()).unwrap_or(true) { return Err(format!("{}: a link, not removed", cache.display())); }
        std::fs::remove_dir_all(&cache).map_err(|e| format!("{}: {e}", cache.display()))?;
    }
    let _ = std::fs::remove_file(out.join(disk_cache::LIBRARY_MARKER));
    std::fs::create_dir_all(&cache).map_err(|e| format!("{}: {e}", cache.display()))?;
    for f in ["manifest.json", "editor-demos.json"] {
        if source.join(f).is_file() { std::fs::copy(source.join(f), out.join(f)).map_err(|e| format!("{f}: {e}"))?; }
    }
    disk_cache::set_export(Some(cache));
    let result = convert_into(source).and_then(|mut inv| {
        let (entries, contents, bytes) = disk_cache::finish_export()?;
        inv.pack_entries = entries;
        inv.pack_contents = contents;
        inv.pack_bytes = bytes;
        Ok(inv)
    });
    disk_cache::set_export(None);
    let mut inventory = result?;
    inventory.source = source.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    std::fs::write(out.join(disk_cache::LIBRARY_MARKER), serde_json::to_vec_pretty(&inventory).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    Ok(inventory)
}

fn convert_into(source: &Path) -> Result<Inventory, String> {
    let mut inv = Inventory { format: FORMAT.into(), ..Default::default() };
    let (motions, ik) = crate::h_playback::prepare_library(source)?;
    inv.motions = motions;
    inv.ik_config = ik;
    let pairs = crate::h_playback::pairs(source);
    let poses = crate::h_playback::interaction_poses(source);
    inv.pairs = pairs.len();
    inv.poses = poses.len();
    let _ = crate::anim::clip_names(source);
    let sources = sources_of(source);
    let mut keys: BTreeSet<String> = demos(source).into_iter().map(|d| d.key).filter(|k| !k.is_empty()).collect();
    for s in &sources {
        for n in crate::anim::clip_names_for_source(source, s) { keys.insert(format!("{s}|{n}")); }
        let _ = crate::anim::clip_count_for_source(source, s);
    }
    // Paired layer clips are keys of their own (`states:layers@…`): the gallery and the interaction scene ask for
    // every state of a pair's source with the role's layer.
    let mut layered = BTreeSet::new();
    for p in pairs.iter().chain(poses.iter()) {
        for (s, layer) in [(&p.female_source, p.female_layer), (&p.male_source, p.male_layer)] {
            if s.is_empty() || layer <= 0 { continue; }
            for label in crate::anim::clip_names_for_source(source, s) { layered.insert((format!("{s}|{label}"), layer)); }
        }
    }
    for (key, layer) in layered {
        if let Some(clip) = crate::anim::native_pair_clip(source, &key, layer) { keys.insert(clip.clone()); inv.pair_layers.push((key, layer, clip)); }
    }
    // A trial conversion (`OK_PAIRED_SAMPLE=N`): every Nth key only.
    if let Some(n) = std::env::var("OK_PAIRED_SAMPLE").ok().and_then(|v| v.parse::<usize>().ok()).filter(|n| *n > 1) {
        keys = keys.into_iter().step_by(n).collect();
    }
    // Keys share their leaves (the same clips under several controllers and scene profiles): decode each raw clip
    // once, in source order within one preparation scope, so a bundle's files are reused across its clips.
    let mut leaves = BTreeSet::new();
    for k in &keys { leaves.extend(crate::anim::leaf_clips(source, k)); }
    eprintln!("ok-paired-library: {} keys play {} raw clips", keys.len(), leaves.len());
    crate::anim::begin_animation_preparation();
    for (i, raw) in leaves.iter().enumerate() {
        if i % 200 == 0 { eprintln!("ok-paired-library: prepared {i}/{}", leaves.len()); crate::anim::release_decoded_clips(); }
        if !crate::anim::prepare_leaf_clip(source, raw) { inv.unsupported.push(raw.clone()); }
    }
    crate::anim::end_animation_preparation();
    inv.leaves = leaves.len();
    inv.keys = keys.into_iter().collect();
    inv.sources = sources.into_iter().collect();
    Ok(inv)
}

/// Checks a converted library against its original: the same names, preparations, durations, pairs and paired
/// layers, and no query the library could not answer. Returns a report (lines) or the first mismatches.
pub fn verify(source: &Path, library: &Path) -> Result<Vec<String>, String> {
    let inv: Inventory = serde_json::from_slice(&std::fs::read(library.join(disk_cache::LIBRARY_MARKER)).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    let _ = disk_cache::library_misses();
    let mut bad = Vec::new();
    let pairs = |r: &Path| serde_json::to_string(&(crate::h_playback::pairs(r), crate::h_playback::interaction_poses(r))).unwrap_or_default();
    if pairs(source) != pairs(library) { bad.push("pair catalogue differs".to_string()); }
    if let Err(e) = crate::h_playback::prepare_library(library) { bad.push(format!("paired IK: {e}")); }
    for s in &inv.sources {
        let (a, b) = (crate::anim::clip_names_for_source(source, s), crate::anim::clip_names_for_source(library, s));
        if a != b { bad.push(format!("names of {s}: {} vs {}", a.len(), b.len())); }
    }
    // Keys: playable in the library exactly when in the original, with the same duration (a spread sample of up to
    // 4000 keys keeps the check to minutes).
    let step = (inv.keys.len() / 4000).max(1);
    for k in inv.keys.iter().step_by(step) {
        let (a, b) = (crate::anim::prepare_animation_clip(source, k), crate::anim::prepare_animation_clip(library, k));
        if a != b { bad.push(format!("preparable {a} vs {b}: {k}")); continue; }
        let (a, b) = (crate::anim::clip_duration(source, k), crate::anim::clip_duration(library, k));
        if (a - b).abs() > 1e-5 && !(a.is_nan() && b.is_nan()) { bad.push(format!("duration of {k}: {a} vs {b}")); }
    }
    for (key, layer, clip) in &inv.pair_layers {
        if crate::anim::native_pair_clip(library, key, *layer).as_deref() != Some(clip.as_str()) { bad.push(format!("pair layer {layer} of {key}")); }
    }
    // Misses first: they explain the mismatches after them.
    let misses = disk_cache::library_misses();
    for (i, m) in misses.iter().take(20).enumerate() { bad.insert(i, format!("library miss: {m}")); }
    if !bad.is_empty() {
        let total = bad.len() + misses.len().saturating_sub(20);
        bad.truncate(40);
        bad.insert(0, format!("{total} problems"));
        return Err(bad.join("\n"));
    }
    Ok(vec![format!("{} sources, {} keys ({} checked), {} raw clips ({} unsupported in the original too), {} pair layers, {} pairs, {} poses, {} MotionIK assets, IK config {}: all answered by the library",
        inv.sources.len(), inv.keys.len(), inv.keys.len().div_ceil(step), inv.leaves, inv.unsupported.len(), inv.pair_layers.len(), inv.pairs, inv.poses, inv.motions, inv.ik_config)])
}

/// The library animation root of a base library directory (`<lib>/paired`), if it was converted.
pub fn library_root(base_library: &Path) -> Option<PathBuf> {
    let root = base_library.join("paired");
    root.join(disk_cache::LIBRARY_MARKER).is_file().then_some(root)
}


#[cfg(test)]
mod acceptance {
    /// As a running game uses it: OK cards opened from the base library (gamefs replays paths under it) play the same
    /// pair state once from PairedAnimations and once from the converted animation root; the poses must match and the
    /// library must answer every query.
    #[test]
    #[ignore = "OK base library + converted paired root: an OK card poses a pair state from the library like from the bundles"]
    fn ok_card_plays_a_library_pair_state() {
        let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let resources = manifest.join("../../Content/IndependentResources");
        let (bundles, library) = (resources.join("PairedAnimations"), resources.join("base_v1/paired"));
        let pairs = crate::h_playback::pairs(&library);
        let pair = pairs.first().expect("pairs from the library under replay");
        let labels = crate::anim::clip_names_for_source(&library, &pair.female_source);
        let label = labels.iter().find(|l| l.ends_with("Base Layer.WLoop") || l.contains("Loop")).or(labels.first()).expect("a state").clone();
        let key = std::ffi::CString::new(format!("{}|{label}", pair.female_source)).unwrap();
        let bones = ["cf_j_hips", "cf_j_spine02", "cf_j_neck", "cf_j_arm00_L", "cf_j_forearm01_R", "cf_j_thigh00_L", "cf_j_leg01_R"];
        let pose = |root: &std::path::Path| -> Vec<[f32; 3]> {
            let mut s = crate::oklib::open_test_card(&manifest.join("../../Content/CharacterCards/CrowdAPNG/C002.png"), 0).unwrap();
            s.refresh_static_interaction(0.);
            let c_root = std::ffi::CString::new(root.to_string_lossy().replace('\\', "/")).unwrap();
            let code = unsafe { crate::ffi::ok_select_common_animation(&mut s, c_root.as_ptr(), key.as_ptr()) };
            assert_eq!(code, 0, "selecting {key:?} from {}", root.display());
            s.refresh_static_interaction(0.7);
            bones.iter().map(|b| s.body_bone_euler(b).unwrap_or_default()).collect()
        };
        let a = pose(&bundles);
        let _ = crate::anim::disk_cache::library_misses();
        let b = pose(&library);
        let misses = crate::anim::disk_cache::library_misses();
        let worst = a.iter().zip(&b).flat_map(|(x, y)| (0..3).map(move |i| (x[i] - y[i]).abs())).fold(0f32, f32::max);
        println!("{} pairs; {label}: bundles {a:?}\nlibrary {b:?}\nworst {worst:.4} deg; misses {misses:?}", pairs.len());
        assert!(misses.is_empty(), "every query answered by the library");
        assert!(worst < 0.01, "the library poses like the bundles");
    }
}
