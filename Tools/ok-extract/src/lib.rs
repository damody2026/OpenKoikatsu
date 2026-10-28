#[allow(unused_imports)] use crate::gamefs::GamePath as _;
pub mod appearance;
pub mod recycle_alloc;
mod anim;
#[cfg(test)]
pub(crate) mod test_support;
pub use anim::clip_names;
pub use anim::clip_names_for_source;
pub use anim::prepare_animation_clip;
pub use anim::prepare_animation_batch;
mod abmx;
mod accessory_extract;
mod editor_mods;
mod clothing_rules;
mod pregnancy;
mod card;
mod face_create;
mod face_create_source;
mod face_create_gpu;
mod dbone;
pub mod px;
mod finalik;
mod point_contact;
mod h_playback;
mod list;
pub mod lowpoly;
pub mod vertcolor;
pub mod video_motion;
mod mods;
mod pose;
mod gpu_rig;
mod ordered;
mod profile;
mod ffi;
mod native_profile;
mod png_transform_cache;
mod teximg;
pub mod cardbake;
pub mod memosnap;
pub mod tree_answers;
pub mod okstrict;
#[cfg(test)]
mod crowd_native_tests;
mod eye_gaze;
mod eye_look;
mod head_look;
mod hit_reaction;
mod session;
pub mod okanim;
#[cfg(test)]
mod dump_outfit_audit;
#[cfg(test)]
mod body_articulation_audit;
pub mod portable;
pub mod paired_library;
pub mod studio_status;
pub mod eye_set;
mod portable_resources;
mod portable_outfits;
#[cfg(feature = "three-export")]
pub use session::three_export;
mod pv;
mod shape;
mod skin_source;
mod clothes_gpu;
mod sqlcache;
pub mod oklib;
pub mod okphysics;
pub mod gamefs;
pub mod okclothes;
pub mod motion_catalogue;
pub mod okmodel_view;
pub mod okmodel_edit;
mod oklib_convert;
mod studio_items;
mod studio_child_animation;
mod studio_pose;
mod studio_scene;
mod studio_anims;
mod studio_sky;
mod skel_export;
mod unity;

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::{Path, PathBuf};
use std::time::Instant;

fn profile_enabled() -> bool {
    std::env::var("OK_PROFILE").is_ok()
}

fn profile_tick(label: &str, start: Instant) {
    if profile_enabled() {
        eprintln!(
            "ok-extract: PROFILE {label:<32} {:>8.1}ms",
            start.elapsed().as_secs_f64() * 1000.0
        );
    }
}

use serde::Serialize;

use crate::card::{parse_card_outfit, CardInfo};

pub use crate::card::{parse_card, parse_card_outfit as parse_card_for_outfit};
use crate::list::{catalog_from_zipmod, load_characustom_dir, ChaListCatalog, ListEntry};
use crate::unity::{
    attach_with_hint, is_mesh_category, load_base_bundle, meshes_for_maindata,
    meshes_skel_from_memory, named_png_from_bundle, pick_largest, pick_maindata, pick_named,
    pick_named_png, role_for_category, ExtractedMesh,
};

#[derive(Serialize, Default)]
struct ManifestPart {
    #[cfg(test)]
    #[serde(skip_serializing_if = "Option::is_none")]
    source_mesh: Option<String>,
    role: String,
    path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    guid: Option<String>,
    mesh: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    slot: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    property: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    category_no: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    texture: Option<String>,
    /// "opaque" | "masked" | "translucent"
    #[serde(default, skip_serializing_if = "String::is_empty")]
    blend: String,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    unlit: bool,
}

fn blend_for(role: &str, mesh: &str) -> (String, bool) {
    let r = role.to_ascii_lowercase();
    let m = mesh.to_ascii_lowercase();
    if r.contains("hair") || m.contains("hair") || m.contains("lash") || m.contains("kami") {
        ("masked".into(), false)
    } else if m.contains("mayuge") || m.contains("eyeline") || m.contains("eyebrow") {
        // StudioEditor draws these as transparent line overlays.
        ("translucent".into(), true)
    } else if m.contains("noseline") || m.contains("nose") {
        // The noseline is a multiply shadow; approximate as a soft translucent overlay.
        ("translucent".into(), true)
    } else if m.contains("cf_o_face") {
        // The create-RT albedo is already composed. DefaultLit changes its
        // card colors and lights the inner-mouth triangles independently.
        ("opaque".into(), true)
    } else if m.contains("hitomi") || m.contains("sirome") {
        // StudioEditor ConfigureFaceRenderQueues: sclera and iris are both
        // transparent, iris queued +1. Opaque sclera writes depth and
        // z-fights the iris sphere into shards.
        ("translucent".into(), true)
    } else if m.contains("tang") || m.contains("tooth") {
        ("opaque".into(), true)
    } else if r.contains("acc") {
        ("masked".into(), false)
    } else {
        ("opaque".into(), false)
    }
}

/// HANDOFF §15.2: blend comes from the Unity material, not mesh-name guesses.
/// `renderQueue>=3000` or keywords `_ALPHABLEND_ON`/`_ALPHAPREMULTIPLY_ON`
/// → translucent. Hair shader names → masked. Else `blend_for` name fallback
/// (head overlays still have empty `m_Shader` PPtrs).
fn toon_clothes_unlit(mat: &crate::unity::MeshMaterial) -> bool {
    let s = mat.shader.to_ascii_lowercase();
    (s.contains("main_opaque")
        || s.contains("main_clothes")
        || s.contains("main_item")
        || s.contains("main_emblem"))
        && !s.contains("hair")
        && !s.contains("skin")
}

/// The Shader Forge shader a material's shader stands for, lowercase. xukmi's KKShadersPlus replace them under CamelCase
/// names with a "Plus" suffix ("xukmi/MainItemStudioAlphaPlus" for "Shader Forge/main_item_studio_alpha"); their blend
/// state and `_alpha` follow the shader they replace. Any other shader is returned lowercased.
pub(crate) fn shader_family(shader: &str) -> String {
    let lower = shader.to_ascii_lowercase();
    let Some(name) = lower.strip_prefix("xukmi/").map(|_| &shader[6..]) else { return lower };
    let name = if name.to_ascii_lowercase().ends_with("plus") { &name[..name.len() - 4] } else { name };
    let mut snake = String::from("shader forge/");
    for (i, c) in name.chars().enumerate() {
        if c.is_ascii_uppercase() && i > 0 { snake.push('_'); }
        snake.push(c.to_ascii_lowercase());
    }
    snake
}

/// Shader-family identity, independent of role/renderer name. KKS renamed
/// the native hair shaders; accessory hair must retain their draw inputs too.
pub(crate) fn is_native_hair_shader(shader: &str) -> bool {
    let shader=shader.to_ascii_lowercase();
    shader.contains("main_hair") || matches!(shader.as_str(),"koikano/hair_main_sun"|"koikano/hair_main_sun_front")
}

pub(crate) fn blend_for_mesh(role: &str, mesh: &crate::unity::ExtractedMesh) -> (String, bool) {let _spot=crate::profile::spot("blend_for_mesh");
    // Gag eyes (cf_O_gag_eye_00..02, an eye set's drawn eyes): an unlit cut-out texture over the face.
    if crate::eye_set::eye_reference(&mesh.name) >= 3 {
        return ("translucent".into(), true);
    }
    let role_unlit = role.to_ascii_lowercase().contains("clothes");
    if let Some(mat) = mesh.mat.as_ref() {
        let alphablend = mat.keywords.iter().any(|k| {
            k.eq_ignore_ascii_case("_ALPHABLEND_ON")
                || k.eq_ignore_ascii_case("_ALPHAPREMULTIPLY_ON")
        });
        let alphatest = mat.keywords.iter().any(|k| k.eq_ignore_ascii_case("_ALPHATEST_ON"));
        let toon_unlit = toon_clothes_unlit(mat) || role_unlit;
        let shader = mat.shader.to_ascii_lowercase();
        // The source fragment exports alpha=1 after optional cutoff. A custom
        // render queue cannot turn this pass into a translucent shader.
        if matches!(shader.as_str(),"xukmi/mainitemplus"|"shader forge/main_item"|"shader forge/main_opaque") {
            return ("masked".into(), true);
        }
        // MainAlpha's recovered FORWARD is SrcAlpha/OneMinusSrcAlpha and
        // exports sampled alpha. Opaque optimization requires actual texture
        // proof later in SessionPart; queue/renderer role cannot supply it.
        if shader=="shader forge/main_alpha" {
            return ("translucent".into(),true);
        }
        // Material Editor can copy a hair material and replace its shader with
        // the studio item's alpha pass. The copied shader, not the source
        // renderer's hair classification, decides its blend state.
        if shader_family(&mat.shader).contains("main_item_studio_alpha") {
            return ("translucent".into(), true);
        }
        // Studio's light effects (waves, flares): Blend One One, unlit; their black is no light.
        if shader.contains("main_item_studio_add") {
            return ("additive".into(), true);
        }
        // Unity's built-in additive particle shaders (Studio's 2D lens flares: Legacy Shaders/Particles/Additive) blend
        // SrcAlpha One: the renderer folds source alpha and soft-particle fade into the contribution.
        if shader.contains("particles/additive") {
            return ("additive".into(), true);
        }
        // Native glasses use SrcAlpha/OneMinusSrcAlpha even when their
        // Material stores a default queue (-1), before Unity resolves it.
        if shader == "koikano/main_clothes_item_glasses"
            || shader == "shader forge/toon_glasses_lod0" {
            return ("translucent".into(), true);
        }
        // IBL_Shader_alpha starts with a color-masked depth prepass, but its
        // first FORWARD color pass is SrcAlpha/OneMinusSrcAlpha, ZWrite Off,
        // Cull Back. The recovered fragment exports MainTex alpha and never
        // discards. The shader is lit even for O_nosetama ("nose" in its name).
        if shader == "ibl_shader_alpha" {
            return ("translucent".into(), false);
        }
        // Unity renderQueue changes draw order, not a shader pass's Blend or
        // ZWrite state. The recovered main_hair ForwardBase is One/Zero with
        // ZWrite On and discards below _AlphaMask.r * _MainTex.a = 0.5.
        // main_hair_front blends but also writes depth; masked preserves its
        // depth ordering for the mostly binary authored alpha in this preview.
        if is_native_hair_shader(&shader) {
            return ("masked".into(), true);
        }
        if alphablend || mat.render_queue >= 3000 {
            let (_, unlit) = blend_for(role, &mesh.name);
            return ("translucent".into(), toon_unlit || unlit);
        }
        if shader.contains("main_alpha") {
            return ("translucent".into(), toon_unlit);
        }
        if shader.is_empty() && mesh.hair_shader_hint {
            return ("masked".into(), true);
        }
        if alphatest {
            return ("masked".into(), toon_unlit);
        }
        if toon_unlit {
            let (blend, _) = blend_for(role, &mesh.name);
            return (with_clothes_clip(role, blend), true);
        }
        // Studio primitives (cylinder / torus) reference Shader Forge/main_item
        // in another file, so m_Shader does not resolve here. Their custom
        // queue is the shader default (often 2450). DefaultLit then paints the
        // underside black. Treat an unresolved accessory shader like main_item.
        if shader.is_empty() && role.to_ascii_lowercase().contains("acc") {
            // Studio primitives (queue ~2450) are alpha-blended rings. Masked
            // clip keeps the dark cap, so the close-up shows a solid disc.
            return ("translucent".into(), true);
        }
    }
    let (blend, unlit) = blend_for(role, &mesh.name);
    (with_clothes_clip(role, blend), unlit || role_unlit)
}

/// Shader Forge/main_opaque and Koikano/main_clothes_opaque run
/// `clip(tex.a - _Cutoff)` (ChaControl assigns the create RT as `_MainTex`;
/// `_Cutoff` default 0.5). UE masked is that alphatest. Translucent
/// (`_ALPHABLEND_ON` / `main_alpha`) is unchanged.
fn with_clothes_clip(role: &str, blend: String) -> String {
    if blend == "opaque" && role.to_ascii_lowercase().contains("clothes") {
        "masked".into()
    } else {
        blend
    }
}

#[derive(Serialize, Default)]
struct Manifest {
    card: String,
    marker: String,
    lastname: String,
    firstname: String,
    nickname: String,
    head_id: i32,
    hair_ids: [i32; 4],
    shape_face: Vec<f32>,
    shape_body: Vec<f32>,
    clothes_ids: Vec<i32>,
    clothes_states: [u8; 9],
    shoes_type: u8,
    outfit: i32,
    missing: Vec<String>,
    mapped_sliders: Vec<String>,
    unmapped_face: Vec<i32>,
    unmapped_body: Vec<i32>,
    hair_color: [f32; 4],
    skin_color: [f32; 4],
    clothes_color: [f32; 4],
    sub_parts_id: Vec<i32>,
    parts: Vec<ManifestPart>,
}

fn guid_path<'a>(
    index: &'a std::collections::HashMap<String, PathBuf>,
    guid: &str,
) -> Option<&'a PathBuf> {
    index
        .get(guid)
        .or_else(|| {
            index
                .iter()
                .find(|(k, _)| k.eq_ignore_ascii_case(guid))
                .map(|(_, p)| p)
        })
        .filter(|p| p.ok_is_file())
}

thread_local! {
    static ENTRY_MEMO: std::cell::RefCell<
        std::collections::HashMap<String, (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>)>,
    > = std::cell::RefCell::new(std::collections::HashMap::new());
}

#[cfg(test)]
mod multi_character_tests;

thread_local! {
    /// The view the entry memos were filled in (extract_session).
    static ENTRY_EPOCH: std::cell::RefCell<String> = const { std::cell::RefCell::new(String::new()) };
}

pub(crate) fn clear_entry_memo() {
    ENTRY_MEMO.with(|m| m.borrow_mut().clear());
    ACCESSORY_SOURCE_MEMO.with(|m| m.borrow_mut().clear());
}

thread_local! {
    // The same accessory bundle is selected by mesh, colour and assignment
    // stages of one slot. Share one acquisition within an installed-source
    // epoch (cleared with the entry memo); bounded because bundles are large.
    static ACCESSORY_SOURCE_MEMO: std::cell::RefCell<Vec<((Option<PathBuf>,String,String),Option<std::sync::Arc<(crate::unity::SourceBytes,Option<PathBuf>)>>)>>
        = const { std::cell::RefCell::new(Vec::new()) };
}

fn entry_memo_key(zip: Option<&Path>, entry: &ListEntry) -> String {
    format!(
        "{}|{}|{}|{}|{:?}",
        zip.map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_default(),
        entry.main_ab,
        entry.main_data,
        entry.category_no,
        crate::unity::clothes_states()
    )
}

fn entry_sql_key(game_root: &Path, zip: Option<&Path>, entry: &ListEntry) -> String {
    if entry.main_ab.is_empty() { return String::new(); }
    match accessory_bundle_source(game_root, zip, entry).and_then(|(_,source)|source) {
        Some(source) => crate::sqlcache::zip_key(&source, &entry.main_ab),
        None => crate::sqlcache::file_key(&game_root.join("abdata").join(&entry.main_ab)),
    }
}

fn entry_prefab_variant_key(category: i32, shake_enabled: bool, clothes_states: [u8; 9]) -> String {
    // Parsed clothes prefabs have already dropped inactive n_top/n_bot/etc.
    // nodes. Their cache identity must include the runtime DrawClothes state;
    // a card can request several states of the same bundle in one process.
    if (105..=112).contains(&category) || (200..=212).contains(&category) {
        let states = clothes_states.iter().map(u8::to_string).collect::<Vec<_>>().join("");
        format!("shake={shake_enabled}|clothes={states}")
    } else {
        format!("shake={shake_enabled}")
    }
}

/// `ChaControl.SetClothesState` (decompiled, lines 2121-2244): a missing
/// StateType has no dictionary and accepts the requested value unchanged.
/// Type 0 allows 0/1/3, type 1 allows 0/3, type 2 allows all four.
fn set_clothes_state(
    states: &mut [u8; 9], kind: usize, mut state: u8,
    state_type: Option<u8>, not_bot: bool, not_shorts: bool,
    shoe_types: [Option<u8>; 2],
) {
    let previous = states[kind];
    if let Some(ty @ 0..=2) = state_type {
        loop {
            let allowed = match ty { 0 => state != 2, 1 => state == 0 || state == 3, _ => true };
            if allowed { states[kind] = state; break; }
            state = (state + 1) % 4;
            if state == previous { break; }
        }
    } else {
        states[kind] = state;
    }
    let linked = match kind {
        0 if not_bot => Some(1), 1 if not_bot => Some(0),
        2 if not_shorts => Some(3), 3 if not_shorts => Some(2),
        _ => None,
    };
    if let Some(other) = linked {
        if states[kind] == 3 { states[other] = 3; }
        else if states[other] == 3 { states[other] = state; }
    }
    if kind == 7 || kind == 8 {
        let other = if kind == 7 { 8 } else { 7 };
        if state != 0 || shoe_types[other - 7].is_some_and(|ty| ty <= 2) {
            states[other] = state;
        }
    }
}

fn resolve_clothes_states(
    requested: [u8; 9], types: [Option<u8>; 9], not_bot: bool, not_shorts: bool,
) -> [u8; 9] {
    // UpdateClothesStateAll calls SetClothesState in enum order. The value
    // passed to linked slots is the post-cycle request, not the prior value.
    let mut states = [0u8; 9];
    for kind in 0..9 {
        set_clothes_state(&mut states, kind, requested[kind], types[kind],
            not_bot, not_shorts, [types[7], types[8]]);
    }
    states
}

/// `AddClothesStateKind` adjusts state menus when the adjacent garment owns
/// the DEF node. Presence comes from that specific prefab's reference tree.
fn adjust_clothes_state_types(
    mut types: [Option<u8>; 9], refs: [bool; 4], not_bra: bool, not_shorts: bool,
) -> [Option<u8>; 9] {
    let mut top = types[0].unwrap_or(3);
    let mut bot = types[1].unwrap_or(3);
    if bot != 0 && refs[0] {
        if top == 0 { bot = 0; }
        else if top == 1 && bot == 2 { bot = 1; }
    }
    if top != 0 && refs[1] {
        if bot == 0 { top = 0; }
        else if bot == 1 && top == 2 { top = 1; }
    }
    types[0] = (top <= 2).then_some(top);
    types[1] = (bot <= 2).then_some(bot);
    let mut bra = if not_bra { 3 } else { types[2].unwrap_or(3) };
    let mut shorts = if not_bra && not_shorts { 3 } else { types[3].unwrap_or(3) };
    if shorts != 0 && shorts != 2 && refs[2] {
        if bra == 0 { shorts = 0; }
        else if bra == 1 && shorts == 3 { shorts = 1; }
    }
    if bra != 0 && bra != 2 && refs[3] {
        if shorts == 0 { bra = 0; }
        else if shorts == 1 && bra == 3 { bra = 1; }
    }
    types[2] = (bra <= 2).then_some(bra);
    types[3] = (shorts <= 2).then_some(shorts);
    types
}

fn selected_clothes_reference_flags(
    game_root: &Path, selected: &[SelectedClothes],
) -> [bool; 4] {
    std::array::from_fn(|kind| {
        let Some((entry, _, zip_path)) = selected[kind].as_ref() else { return false; };
        if is_dummy_part(&entry.main_data, Some(&entry.name)) { return false; }
        let zip = entry.extra.get("__zip").map(PathBuf::from).or_else(|| zip_path.clone());
        let Some(bytes) = accessory_bundle_bytes(game_root, zip.as_deref(), entry) else { return false; };
        let nodes = crate::unity::prefab_node_names(&bytes, &entry.main_data);
        let wanted = if kind == 0 || kind == 2 { "n_bot_a" } else { "n_top_a" };
        nodes.contains(wanted)
    })
}

pub(crate) fn meshes_for_entry(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {let _spot=crate::profile::spot("meshes_for_entry");
    meshes_for_entry_shake(game_root, zip, entry, true)
}

/// Foreground opens in progress (the reader opening the card the user asked for).
static FOREGROUND_OPENS: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
thread_local! { static BACKGROUND_OPEN: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
/// Marks this thread's opens as background work (the reader's clothing variant warm-up) that yields to foreground opens.
pub(crate) fn set_thread_background(on: bool) { BACKGROUND_OPEN.with(|b| b.set(on)); }
pub(crate) fn thread_is_background() -> bool { BACKGROUND_OPEN.with(|b| b.get()) }
/// Counts a foreground open while alive (no-op on a background thread).
pub(crate) struct ForegroundOpen(bool);
impl ForegroundOpen { pub(crate) fn begin() -> Self { let fg = !thread_is_background(); if fg { FOREGROUND_OPENS.fetch_add(1, std::sync::atomic::Ordering::SeqCst); } Self(fg) } }
impl Drop for ForegroundOpen { fn drop(&mut self) { if self.0 { FOREGROUND_OPENS.fetch_sub(1, std::sync::atomic::Ordering::SeqCst); } } }
/// A background open gives up at its next stage boundary while a foreground open waits: it holds the process-wide
/// card pack mount, and the card the user asked for must not queue behind a speculative build.
fn check_preempted() -> Result<(), String> {
    if thread_is_background() && FOREGROUND_OPENS.load(std::sync::atomic::Ordering::SeqCst) > 0 { Err("background open preempted by a foreground open".into()) } else { Ok(()) }
}

type EntryMeshes = (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>);
/// One prefetched item: filled by a helper thread, waited for by the extraction that asks for the same memo key.
type PrefetchSlot = std::sync::Arc<(std::sync::Mutex<Option<Option<EntryMeshes>>>, std::sync::Condvar)>;
// The prefetched items of the extraction running on this thread (cards opening at once each keep their own).
thread_local! { static ENTRY_PREFETCH: std::cell::RefCell<Option<std::collections::HashMap<String, PrefetchSlot>>> = const { std::cell::RefCell::new(None) }; }
thread_local! { static PREFETCH_WORKER: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }

fn entry_memo_key_for(game_root: &Path, zip: Option<&Path>, entry: &ListEntry, shake_enabled: bool) -> (String, String) {
    let variant = format!("{}|dynamicfix={}|references={}", entry_prefab_variant_key(entry.category_no, shake_enabled, crate::unity::clothes_states()),
        crate::dbone::dynamic_bones_fix_enabled(game_root),crate::unity::references::current_signature(game_root));
    let base_sql_key = entry_sql_key(game_root, zip, entry);
    let memo_key = format!("{base_sql_key}|{}|{variant}", entry_memo_key(zip, entry));
    (memo_key, format!("{base_sql_key}|{variant}"))
}

/// Starts extracting the items a card's hair, accessory and clothing passes are about to ask for, each on a helper
/// thread with this thread's clothes states and switches. The passes stay sequential and in order; an item's
/// `meshes_for_entry_shake` then takes the helper's result (waiting if it is still running) when its memo key is the
/// one predicted, and extracts itself otherwise. A wrong prediction only costs the helper's work.
fn prefetch_entries(game_root: &Path, items: Vec<(Option<PathBuf>, ListEntry, bool)>) {
    clear_entry_prefetch();
    if items.is_empty() || std::env::var_os("OK_NO_ENTRY_PREFETCH").is_some() { return; }
    let states = crate::unity::clothes_states();
    let requested = crate::unity::requested_clothes_states();
    let switches = crate::profile::snapshot();
    let mut jobs = Vec::new();
    {
        ENTRY_PREFETCH.with(|cell| { let mut store = cell.borrow_mut();
        let store = store.get_or_insert_with(std::collections::HashMap::new);
        for (zip, entry, shake) in items {
            if entry.main_data.is_empty() || entry.main_data.eq_ignore_ascii_case("p_dummy") { continue; }
            let (key, _) = entry_memo_key_for(game_root, zip.as_deref(), &entry, shake);
            if store.contains_key(&key) || ENTRY_MEMO.with(|m| m.borrow().contains_key(&key)) { continue; }
            let slot: PrefetchSlot = Default::default();
            store.insert(key.clone(), slot.clone());
            jobs.push((key, slot, zip, entry, shake));
        }
        });
    }
    // One helper per source bundle, its items in order: parsed bundles are cached per thread, so items of one large
    // pack spread over the pool each decompressed and kept their own copy (one card reached 20+ GB that way).
    let mut groups: Vec<((Option<PathBuf>, String), Vec<_>)> = Vec::new();
    for job in jobs {
        let source = (job.2.clone(), job.3.main_ab.to_ascii_lowercase());
        match groups.iter_mut().find(|(s, _)| *s == source) { Some((_, g)) => g.push(job), None => groups.push((source, vec![job])) }
    }
    // A few helpers take the groups in order (the order the passes ask for them): one per group at once parsed every
    // source bundle of the card together. Each helper drops what it parsed once a group's items are handed over.
    // A group of more than two items goes to two helpers, half each: the bundle is parsed twice at most, and a card
    // whose clothes all sit in one pack no longer waits for one helper to decode them one after another.
    let groups: Vec<Vec<_>> = groups.into_iter().flat_map(|(_, mut g)| {
        if g.len() <= 2 { return vec![g]; }
        let rest = g.split_off(g.len().div_ceil(2));
        vec![g, rest]
    }).collect();
    let queue = std::sync::Arc::new(std::sync::Mutex::new(groups.into_iter().collect::<std::collections::VecDeque<_>>()));
    let helpers = queue.lock().unwrap_or_else(|e| e.into_inner()).len().min(PREFETCH_HELPERS);
    if profile_enabled() { let sizes: Vec<usize> = queue.lock().unwrap_or_else(|e| e.into_inner()).iter().map(|g| g.len()).collect(); eprintln!("ok-extract: PROFILE prefetch {} groups {sizes:?} items, {helpers} helpers", sizes.len()); }
    for _ in 0..helpers {
        let (game_root, switches, queue) = (game_root.to_path_buf(), switches.clone(), queue.clone());
        let pack = crate::oklib::current_pack_scope();
        rayon::spawn(move || {
            // The asking open's card pack (cards opening at once each read their own).
            let _pack = crate::oklib::enter_pack_scope(pack);
            let _switches = crate::profile::scope(&switches);
            let _requested = crate::unity::request_clothes_states(requested);
            let _states = crate::unity::set_session_clothes_states(states);
            loop {
                let Some(group) = queue.lock().unwrap_or_else(|e| e.into_inner()).pop_front() else { break };
                for (key, slot, zip, entry, shake) in group {
                    PREFETCH_WORKER.with(|w| w.set(true));
                    // Only a result made under the same key may stand in for the asking thread's own extraction.
                    let mine = entry_memo_key_for(&game_root, zip.as_deref(), &entry, shake).0;
                    let same = mine == key;
                    if !same && profile_enabled() { eprintln!("ok-extract: PROFILE prefetch key differs\n  asked {key}\n  here  {mine}"); }
                    let t_item = std::time::Instant::now();
                    let result = same.then(|| std::panic::catch_unwind(std::panic::AssertUnwindSafe(||
                        meshes_for_entry_shake(&game_root, zip.as_deref(), &entry, shake))).ok()).flatten();
                    PREFETCH_WORKER.with(|w| w.set(false));
                    if profile_enabled() { eprintln!("ok-extract: PROFILE prefetch item {} {} {:.1}ms", entry.main_ab, entry.main_data, t_item.elapsed().as_secs_f64() * 1e3); }
                    *slot.0.lock().unwrap_or_else(|e| e.into_inner()) = Some(result);
                    slot.1.notify_all();
                }
                // Recording a card pack keeps every cache as it was (the pack records the queries that run).
                if !crate::oklib::recording_card_pack() { crate::unity::release_thread_bundle_cache(); }
            }
        });
    }
}

/// Prefetch helpers working at once on one card's items: as fast as one helper per group on the 10-card check
/// (every source bundle of a card parsed at once), with about a quarter less memory per card.
const PREFETCH_HELPERS: usize = 8;

struct PrefetchGuard;
impl Drop for PrefetchGuard { fn drop(&mut self) { clear_entry_prefetch(); } }

/// Drops prefetched items nobody asked for (end of an extraction), after their helpers finish: a helper must not
/// outlive the extraction's card pack mount (the next card's pack would answer its reads).
fn clear_entry_prefetch() {
    let slots = ENTRY_PREFETCH.with(|cell| cell.borrow_mut().take());
    for slot in slots.into_iter().flat_map(|s| s.into_values()) {
        let mut done = slot.0.lock().unwrap_or_else(|e| e.into_inner());
        while done.is_none() { done = slot.1.wait(done).unwrap_or_else(|e| e.into_inner()); }
    }
}

fn meshes_for_entry_shake(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    shake_enabled: bool,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    use crate::memosnap::{MESH,active,get,put};
    // Recorded into / replayed from the card (OK_CARD_MEMO=mesh), under the in-process memo's key.
    if !active(MESH) { return meshes_for_entry_shake_memo(game_root, zip, entry, shake_enabled); }
    let (memo_key, _) = entry_memo_key_for(game_root, zip, entry, shake_enabled);
    if let Some(bytes) = get(MESH, memo_key.as_bytes()) {
        if let Ok(hit) = bincode::deserialize::<EntryMeshes>(&bytes) { return hit; }
    }
    let result = meshes_for_entry_shake_memo(game_root, zip, entry, shake_enabled);
    put(MESH, memo_key.as_bytes(), || bincode::serialize(&result).ok());
    result
}

fn meshes_for_entry_shake_memo(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    shake_enabled: bool,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let (memo_key, _) = entry_memo_key_for(game_root, zip, entry, shake_enabled);
    if let Some(hit) = ENTRY_MEMO.with(|m| m.borrow().get(&memo_key).cloned()) {
        return hit;
    }
    if !PREFETCH_WORKER.with(|w| w.get()) {
        let slot = ENTRY_PREFETCH.with(|cell| cell.borrow_mut().as_mut().and_then(|s| s.remove(&memo_key)));
        let _prefetch_spot = crate::profile::spot(if slot.is_some() { "prefetch hit (wait)" } else { "prefetch miss" });
        if slot.is_none() && profile_enabled() && !entry.main_data.is_empty() {
            let keys: Vec<String> = ENTRY_PREFETCH.with(|cell| cell.borrow().as_ref().map(|s| s.keys().cloned().collect()).unwrap_or_default());
            let near = keys.iter().find(|k| k.contains(&entry.main_data)).cloned().unwrap_or_default();
            eprintln!("ok-extract: prefetch miss {} {} zip={:?} shake={shake_enabled}\n  key ={memo_key}\n  near={near}", entry.main_ab, entry.main_data, zip);
        }
        if let Some(slot) = slot {
            let mut done = slot.0.lock().unwrap_or_else(|e| e.into_inner());
            while done.is_none() { done = slot.1.wait(done).unwrap_or_else(|e| e.into_inner()); }
            // Handed over as is: copying the meshes (texture bytes included, tens of MB) into this thread's memo
            // cost more than the extraction saved when an extraction asks for an item once. Within a view that is
            // opened again (a conversion opens every outfit, extract_session keeps the memo) it is asked again:
            // there the copy is kept, or every outfit extracted the prefetched items anew.
            if let Some(Some(hit)) = done.take() {
                if !crate::oklib::mounted_pack_identity().is_empty() { ENTRY_MEMO.with(|m| m.borrow_mut().insert(memo_key, hit.clone())); }
                return hit;
            }
        }
    }
    let variant = format!("{}|dynamicfix={}|references={}", entry_prefab_variant_key(entry.category_no, shake_enabled, crate::unity::clothes_states()),
        crate::dbone::dynamic_bones_fix_enabled(game_root),crate::unity::references::current_signature(game_root));
    let base_sql_key = entry_sql_key(game_root, zip, entry);
    let sql_key = if base_sql_key.is_empty() { String::new() }
        else { format!("{base_sql_key}|{variant}") };
    if let Some(hit) = crate::sqlcache::load_prefab(game_root, &sql_key, &entry.main_data) {
        ENTRY_MEMO.with(|m| m.borrow_mut().insert(memo_key, hit.clone()));
        return hit;
    }
    let t0 = Instant::now();
    let result = meshes_for_entry_inner(game_root, zip, entry, shake_enabled);
    profile_tick(
        &format!(
            "meshes_for_entry {} {} n={}",
            entry.main_ab,
            entry.main_data,
            result.0.len()
        ),
        t0,
    );
    if !result.0.is_empty() && !sql_key.is_empty() && !entry.main_data.is_empty() {
        crate::sqlcache::store_prefab(
            game_root,
            &sql_key,
            &entry.main_data,
            &result.0,
            result.1.as_ref(),
        );
        // A helper thread's memo would only grow (it hands its result over instead).
        if !PREFETCH_WORKER.with(|w| w.get()) { ENTRY_MEMO.with(|m| m.borrow_mut().insert(memo_key, result.clone())); }
    }
    result
}

fn meshes_for_entry_inner(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    shake_enabled: bool,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    if entry.main_data.eq_ignore_ascii_case("p_dummy") {
        return (Vec::new(), None);
    }
    let zip_bytes = accessory_bundle_source(game_root, zip, entry)
        .and_then(|(bytes,source)|source.map(|source|(source,bytes)));
    if let Some((source_zip,b)) = zip_bytes {
        {
            let (mut meshes, mut skel) = if entry.main_data.is_empty() {
                (Vec::new(), None)
            } else {
                crate::unity::meshes_skel_for_maindata_ref_with_category(&b, &entry.main_data, entry.category_no)
            };
            // ChaControl.ChangeHairNoAsync: hiPoly runs ChangeShakeHair
            // (DynamicBone on). Only !hiPoly && kind==1 disables it.
            hang_part_dynamic_bones(game_root, Some(&source_zip), &b, &mut meshes, skel.as_mut(),
                matches!(entry.category_no, 101..=112) || role_for_category(entry.category_no, "").starts_with("clothes") || role_for_category(entry.category_no, "").contains("acc"), shake_enabled);
            let kept: Vec<_> = meshes
                .iter()
                .filter(|m| !is_dummy_part(&m.name, Some(&entry.name)))
                .cloned()
                .collect();
            if kept.is_empty() {
                eprintln!(
                    "ok-extract: zip bundle parsed but 0 meshes ab={} data={} raw={}",
                    entry.main_ab,
                    entry.main_data,
                    meshes.len()
                );
                for m in &meshes {
                    eprintln!(
                        "ok-extract:   drop {} dummy={}",
                        m.name,
                        is_dummy_part(&m.name, Some(&entry.name))
                    );
                }
            } else {
                return (kept, skel);
            }
        }
    }
    if entry.main_ab.is_empty() {
        return (Vec::new(), None);
    }
    let path = game_root.join("abdata").join(&entry.main_ab);
    let Ok(bytes) = crate::gamefs::read_source(&path) else {
        return (Vec::new(), None);
    };
    let (mut meshes, mut skel) = if !entry.main_data.is_empty() {
        crate::unity::meshes_skel_for_maindata_ref_with_category(&bytes, &entry.main_data, entry.category_no)
    } else {
        let (m, _, s) = crate::unity::parse_bundle_ref(&bytes).decode_all();
        (m, s)
    };
    hang_part_dynamic_bones(game_root, None, &bytes, &mut meshes, skel.as_mut(),
        matches!(entry.category_no, 101..=112) || role_for_category(entry.category_no, "").starts_with("clothes") || role_for_category(entry.category_no, "").contains("acc"), shake_enabled);
    let meshes: Vec<_> = meshes
        .into_iter()
        .filter(|m| !is_dummy_part(&m.name, Some(&entry.name)))
        .collect();
    if !meshes.is_empty() {
        return (meshes, skel);
    }
    (meshes_for_maindata(bytes, &entry.main_data), skel)
}

fn hang_part_dynamic_bones(
    game_root: &Path,
    source_zip:Option<&Path>,
    bytes: &crate::unity::SourceBytes,
    meshes: &mut [ExtractedMesh],
    skel: Option<&mut crate::pose::Skeleton>,
    defer_accessory: bool,
    shake_enabled: bool,
) {
    let parsed = crate::unity::parse_bundle_ref(bytes);
    parsed.configure_game_references_from_zip(game_root,source_zip);
    let source_set = |sk: &crate::pose::Skeleton| {
        let set = parsed.dynamic_bones_for_skeleton(sk).with_game_fixes(game_root);
        if shake_enabled { set } else { set.after_no_shake() }
    };
    for m in meshes.iter_mut() {
        if let Some(sk) = m.smr_skel.as_mut() {
            m.bone_implants = parsed.bone_implants_for_skeleton(sk);
            let set = source_set(sk);
            if !set.is_empty() {
                m.deferred_dbones = Some(set.clone());
                if !defer_accessory { crate::dbone::hang_set(&set, sk); }
            }
        }
    }
    if let Some(sk) = skel.filter(|_| !defer_accessory) {
        let set = source_set(sk);
        if !set.is_empty() { crate::dbone::hang_set(&set, sk); }
    }
}

fn is_dummy_part(mesh_name: &str, display: Option<&str>) -> bool {
    // ChaControl.ChangeHairNoAsync / ChangeAccessoryNoAsync skip only
    // ListInfo MainData == "p_dummy".
    let n = mesh_name.to_ascii_lowercase();
    let d = display.unwrap_or("").to_ascii_lowercase();
    n.contains("p_dummy") || d.contains("p_dummy")
}

const EXTRACT_CACHE_VER: &str = "v-sacbody-353-native-mirrored-lossy-scale";
/// `o_body_a` SMR bone order for the mod-body skin data (`m_Skin` indices).
const SAC_BODY_BONES: &str = include_str!("../data/sac_body_bones.txt");

fn stamp_text(game_root: &Path, card: &Path, outfit: i32) -> String {
    let meta = crate::gamefs::metadata(card).ok();
    let len = meta.as_ref().map(|m| m.len()).unwrap_or(0);
    let mt = meta
        .and_then(|m| m.modified().ok())
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|d| d.as_micros())
        .unwrap_or(0);
    let me_cfg = game_root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    let sideloader_stamp=crate::gamefs::metadata(game_root.join("BepInEx/config/com.bepis.bepinex.sideloader.cfg")).ok().and_then(|m|Some((m.len(),m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_micros())));
    let me_stamp = crate::gamefs::metadata(me_cfg).ok().and_then(|m| {
        let modified = m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_micros();
        Some((m.len(), modified))
    });
    let me_plugins: Vec<_> = ["BepInEx/plugins/KKS_Plugins/KKS_MaterialEditor.dll", "BepInEx/plugins/KKS_MaterialEditor.dll"]
        .iter().map(|path| crate::gamefs::metadata(game_root.join(path)).ok().and_then(|m| {
            let modified = m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_micros();
            Some((m.len(), modified))
        })).collect();
    format!(
        "{EXTRACT_CACHE_VER}|{len}|{mt}|{outfit}|{}|{}|mods={}|dynamicfix={}|implant={}|materialeditor={me_stamp:?}|materialeditorplugin={me_plugins:?}|sideloader={sideloader_stamp:?}|references={}",
        card.display(), crate::sqlcache::env_sig(),
        crate::sqlcache::mods_inventory_sig(&game_root.join("mods")), crate::dbone::dynamic_bones_fix_enabled(game_root),
        crate::dbone::implant::installed(game_root),crate::unity::references::begin_load(game_root)
    )
}

fn stamp_fresh(game_root: &Path, out_dir: &Path, card: Option<&Path>, outfit: i32) -> bool {
    let Some(card) = card else {
        return false;
    };
    let Ok(s) = crate::gamefs::read_to_string(out_dir.join(".ok_stamp")) else {
        return false;
    };
    s.trim() == stamp_text(game_root, card, outfit)
        && out_dir.join("body.obj").ok_is_file()
        && out_dir.join("manifest.json").ok_is_file()
}

fn png_from_ab(
    game_root: &Path,
    ab: &str,
    names: &[&str],
    cached: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    if let Some((_, b)) = pick_named_png(cached, names) {
        return Some(b);
    }
    if ab.is_empty() || ab == "0" {
        return None;
    }
    let path = game_root.join("abdata").join(ab);
    named_png_from_bundle(&path, names).map(|(_, b)| b)
}

/// Load a texture referenced by a list entry. Mod entries record their source
/// zipmod in `__zip`, so their assetbundles are read straight from the zipmod.
fn png_from_list_entry(
    game_root: &Path,
    entry: &ListEntry,
    names: &[&str],
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    png_from_bundle(
        game_root,
        &entry.main_ab,
        entry.extra.get("__zip").map(|s| s.as_str()),
        names,
        head_texs,
        body_texs,
    )
}

/// `Character.CreateChara(..., hiPoly)`: preview and Studio use the high-poly
/// texture path. The game dump's bra/inner create RTs are 1024-wide, matching
/// the base texture rather than the 512-wide `_low` variant. ActionGame's
/// low-poly path remains available with `OK_FACE_HIPOLY=0`.
fn character_hi_poly() -> bool {
    std::env::var("OK_FACE_HIPOLY")
        .map(|v| v != "0")
        .unwrap_or(true)
}

fn meshes_for_accessory_entry(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    card: &CardInfo,
    outfit: i32,
    property: &str,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let (mut meshes, skel) = meshes_for_accessory_entry_prefab(game_root, zip, entry, card, outfit, property);
    apply_dynamic_bone_editor(&mut meshes, card, outfit, property);
    (meshes, skel)
}

/// DynamicBoneEditor sets the slot's component parameters on the instantiated accessory (after the prefab, per card
/// and outfit): applied to the slot's meshes, never inside the per-entry memo the prefab meshes come from. Every
/// accessory source path calls it (accessory_extract::emit too); setting the same values again is harmless.
pub(crate) fn apply_dynamic_bone_editor(meshes: &mut [ExtractedMesh], card: &CardInfo, outfit: i32, property: &str) {
    if let Some(slot) = accessory_slot_from_property(property) {
        let edits: Vec<&crate::dbone::DynamicBoneEdit> = card.dynamic_bone_edits.iter().filter(|e| e.coordinate == outfit && e.slot == slot as i32).collect();
        if !edits.is_empty() {
            for m in meshes.iter_mut() {
                if let (Some(set), Some(sk)) = (m.deferred_dbones.as_mut(), m.smr_skel.as_ref()) {
                    let changed = set.apply_editor(&edits, sk);
                    if changed > 0 && std::env::var_os("OK_DBONE_DIAG").is_some() { eprintln!("ok-extract: DynamicBoneEditor slot {slot} outfit {outfit} {}: {changed} component(s)", m.name); }
                }
            }
        }
    }
}

fn meshes_for_accessory_entry_prefab(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    card: &CardInfo,
    outfit: i32,
    property: &str,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let slot = accessory_slot_from_property(property);
    let shake_enabled = slot.and_then(|s| card.accessory.get(s))
        .is_none_or(|part| !part.no_shake);
    let normal = meshes_for_entry_shake(game_root, zip, entry, shake_enabled);
    let Some(slot) = slot else { return normal };
    let has_renderer_overrides = card.me_renderer_on.iter().any(|(ot, coord, s, _)| {
        *ot == 2 && *coord == outfit && *s == slot as i32
    });
    if !has_renderer_overrides {
        return normal;
    }
    // Material Editor applies renderer visibility after the prefab is loaded.
    // Only explicit Enabled=1 may restore a default-disabled candidate.
    // An Enabled=0 row for another renderer must not enable every sibling.
    let Some(shared) = accessory_bundle_shared(game_root, zip, entry) else { return normal };
    let (bytes,source_zip)=(&shared.0,shared.1.clone());
    let parsed = crate::unity::parse_bundle_ref(bytes);
    let (mut meshes, mut skel) = parsed.meshes_for_maindata_including_disabled(&entry.main_data);
    if meshes.is_empty() {
        return normal;
    }
    meshes.retain(|m| normal.0.iter().any(|n|n.path_id==m.path_id&&n.renderer_ids.iter().any(|id|m.renderer_ids.contains(id))) ||
        m.renderer_names.iter().any(|name|card.me_renderer_on.contains(&(2,outfit,slot as i32,name.to_ascii_lowercase()))));
    hang_part_dynamic_bones(game_root, source_zip.as_deref(), &bytes, &mut meshes, skel.as_mut(), true, shake_enabled);
    meshes.retain(|m| !is_dummy_part(&m.name, Some(&entry.name)));
    (meshes, skel)
}

/// ChangeCustomEmblem assigns a texture from mt_emblem to renderers using
/// main_clothes_emblem. With no selected texture the game's shader clips the
/// emblem; a white fallback would instead draw the bare emblem polygon.
fn apply_clothes_emblems(
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
fn clothes_list_png(
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

fn clothes_ab_key_for(tex_key: &str) -> &'static str {
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
fn clothes_tex_ab(entry: &ListEntry, ab_key: &str) -> String {
    let a = entry.info(ab_key);
    if a.is_empty() || a == "0" {
        entry.main_ab.clone()
    } else {
        a
    }
}

fn clothes_list_png_ab(
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
fn clothes_create_slot(renderer_ids: &[i64], map: &crate::unity::ClothesRendererMap) -> Option<usize> {
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

const CLOTHES_CREATE_KEYS: [(&str, &str); 3] = [
    ("MainTex", "ColorMaskTex"),
    ("MainTex02", "ColorMask02Tex"),
    ("MainTex03", "ColorMask03Tex"),
];

/// Bake the 3 create RTs (`CreateClothesTexture` i=0..2). Slot 0 is required
/// by the game; 1/2 exist only when `MainTex02`/`MainTex03` are set.
fn bake_clothes_create_textures(
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
fn mark_runtime_main_texture(mesh: &mut ExtractedMesh) {
    mesh.runtime_textures.insert("_MainTex".into(), [1, 1, 1, 1, 1]);
}

/// These draw shaders declare _MainTex = "white" in ShaderLab. Null is a
/// valid material input, not a failed texture import or UE default material.
fn apply_clothes_default_main_texture(mesh: &mut ExtractedMesh) {
    if mesh.texture_png.is_some() { return; }
    let Some(mat) = mesh.mat.as_ref() else { return; };
    if matches!(mat.shader.to_ascii_lowercase().as_str(),
        "shader forge/main_opaque" | "koikano/main_clothes_opaque") {
        mesh.texture_png = crate::unity::solid_png([1.;4], 8, 8);
    }
}

/// MaterialAPI.SetTexture copies the replaced texture's wrapMode (or Repeat
/// for a null texture). A previous create RT takes precedence over the prefab.
fn clear_unbound_normal_map(mesh: &mut ExtractedMesh) {let _spot=crate::profile::spot("clear_unbound_normal_map");
    if mesh.mat.as_ref().is_some_and(|mat|mat.slot("_NormalMap").is_none()) {
        mesh.normal_map_png = None;
        mesh.runtime_textures.remove("_NormalMap");
    }
}

fn mark_material_editor_main_texture(mesh: &mut ExtractedMesh) {
    mark_material_editor_texture(mesh,"_MainTex");
}

fn mark_material_editor_texture(mesh: &mut ExtractedMesh, property: &str) {
    let wrap = mesh.runtime_textures.get(property).map(|s|s[1])
        .or_else(||mesh.mat.as_ref().and_then(|m|m.tex_wraps.get(property).copied())).unwrap_or(0);
    mesh.runtime_textures.insert(property.into(), [1,wrap,wrap,1,1]);
}

#[cfg(test)]
#[test]
fn runtime_texture_material_editor_copies_replaced_wrap() {
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
fn runtime_texture_tracks_create_assignment_not_role() {
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
fn studio_mdk_preview_normalizes_source_rows_once() {
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
            let decoded = crate::teximg::load_from_memory(bytes).unwrap().to_rgba8();
            assert_eq!(decoded.get_pixel(0, 0).0, [0, 0, 255, 255], "{role}: source top row");
            assert_eq!(decoded.get_pixel(0, 1).0, [255, 0, 0, 255], "{role}: source bottom row");
        }
    }
}

#[cfg(test)]
#[test]
fn studio_item_preview_normalizes_main_and_mask_rows_once() {
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
            let decoded = crate::teximg::load_from_memory(bytes).unwrap().to_rgba8();
            assert_eq!(decoded.get_pixel(0, 0).0, [0, 0, 255, 255], "{shader}: source top row");
            assert_eq!(decoded.get_pixel(0, 1).0, [255, 0, 0, 255], "{shader}: source bottom row");
        }
    }
}

fn apply_clothes_create_textures(
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
fn assigned_clothes_slot(ids:&[i64],map:&crate::unity::ClothesRendererMap,baked:&[Option<Vec<u8>>;3])->Option<usize> {
    baked[0].as_ref()?;
    [0,1,2,0].into_iter().enumerate().filter_map(|(write,slot)| {
        (baked[slot].is_some() && ids.iter().any(|id|match write {
            0=>map.normal01.contains(id)||map.alpha01.contains(id),
            1=>map.normal02.contains(id),2=>map.normal03.contains(id),_=>map.accessory.contains(id),
        })).then_some(slot)
    }).last()
}

fn apply_clothes_overlay(card:&CardInfo,outfit:i32,component:&str,meshes:&mut [ExtractedMesh],map:Option<&crate::unity::ClothesRendererMap>,baked:&[Option<Vec<u8>>;3]) {
    let Some((overlay,clear))=card.clothes_overlays.get(&(outfit,component.into())) else{return};
    let Some(map)=map else{eprintln!("ok-extract: KCOX unresolved component renderer map {component}");return};
    apply_clothes_overlay_rt(component,meshes,map,baked,overlay,*clear);
}

fn apply_clothes_overlay_rt(component:&str,meshes:&mut [ExtractedMesh],map:&crate::unity::ClothesRendererMap,baked:&[Option<Vec<u8>>;3],overlay:&[u8],clear:bool) {
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
fn clothes_overlay_follows_live_shared_rt_and_native_stop() {
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
    for mesh in &meshes[..4] {assert_eq!(crate::teximg::load_from_memory(mesh.texture_png.as_ref().unwrap()).unwrap().to_rgba8().get_pixel(0,0).0,[255,0,0,255]);}
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
fn sideloader_png(zip: Option<&str>, game_root: &Path, ab: &str, names: &[&str]) -> Option<Vec<u8>> {let _spot=crate::profile::spot("sideloader_png");
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

fn png_from_bundle(
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
fn png_in_runtime_row_order(game: &Path, entry: &ListEntry, ab: &str, names: &[&str]) -> Option<Vec<u8>> {
    runtime_row_source(game,entry,ab,names).map(|(png,flip)|if flip{flip_runtime_blit_v(png)}else{png})
}
fn runtime_row_source(game:&Path,entry:&ListEntry,ab:&str,names:&[&str])->Option<(Vec<u8>,bool)>{
    let zip=entry.extra.get("__zip").map(String::as_str);
    if let Some(png)=sideloader_png(zip,game,ab,names){return Some((png,false));}
    png_from_bundle(game,ab,zip,names,&[],&[]).map(|png|(png,true))
}
/// Keep import metadata attached to the selected pixels. Looking the same
/// texture up again to discover its colour space doubles bundle IO/decoding
/// and can accidentally consult a different source with an identical name.
fn runtime_row_source_with_color_space(game:&Path,entry:&ListEntry,ab:&str,names:&[&str])->Option<(Vec<u8>,bool,Option<i32>)>{
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

fn direct_catalog_png(game: &Path, entry: &ListEntry, names: &[&str]) -> Option<Vec<u8>> {
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

fn eye_gradient_png(game: &Path, vanilla: &ChaListCatalog, mods: &ChaListCatalog,
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
fn load_catalog_png(
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
fn load_catalog_png_ab(
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
fn load_catalog_png_allow0(
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
fn load_catalog_display_texture(
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

fn assign_display_texture_settings(part: &mut crate::session::SessionPart, slot: &str, settings: Option<[i32; 5]>) {
    if let Some(settings) = settings { part.runtime_textures.insert(slot.into(), settings); }
    else { part.runtime_textures.remove(slot); }
}

/// Mean RGBA (0..1) of a PNG, for diagnostics.
fn png_mean(png: &[u8]) -> Option<[f32; 4]> {
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    if w == 0 || h == 0 {
        return None;
    }
    let mut sum = [0f64; 4];
    let n = (w * h) as f64;
    for p in img.pixels() {
        for c in 0..4 {
            sum[c] += p.0[c] as f64;
        }
    }
    Some([
        (sum[0] / n / 255.0) as f32,
        (sum[1] / n / 255.0) as f32,
        (sum[2] / n / 255.0) as f32,
        (sum[3] / n / 255.0) as f32,
    ])
}

fn encode_png(img: &image::RgbaImage) -> Option<Vec<u8>> {let _spot=crate::profile::spot("encode_png");
    // PNG, or the raw container with OK_RAW_TEXTURES=1 (teximg).
    crate::teximg::encode_rgba(img)
}

fn flip_png_v(png: &[u8]) -> Option<Vec<u8>> {let _spot=crate::profile::spot("flip_png_v");
    if std::env::var("OK_EYE_FLIP").as_deref() == Ok("0") {
        return Some(png.to_vec());
    }
    // Eye and face textures repeat across cards and outfits: decode+flip+encode once per content.
    png_transform_cache::transform("flip_png_v_v1",png,&[],||{
        let mut im = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
        image::imageops::flip_vertical_in_place(&mut im);
        encode_png(&im)
    })
}

fn lerp_f(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

/// Koikano/create_eyew PS 0001: lerp(_Color2, _Color, sampled R).
/// Material colours and sRGB samples are linear during the draw; the RT
/// encodes the result. Alpha comes from those colours, not texture coverage.
fn bake_eye_white(png: &[u8], base: [f32; 4], sub: [f32; 4]) -> Option<Vec<u8>> {
    bake_eye_white_with_space(png, base, sub, false)
}

/// ChaControl.InitBaseCustomTextureEtc creates a 512-square high-poly eye
/// white RT (256 low-poly), independent of the catalog texture dimensions.
fn bake_eye_white_create_rt(png:&[u8],base:[f32;4],sub:[f32;4],source_linear:bool,flip_source:bool)->Option<Vec<u8>>{
    bake_eye_white_create_rt_sized(png,base,sub,source_linear,if character_hi_poly(){512}else{256},flip_source)
}
fn bake_eye_white_create_rt_sized(png:&[u8],base:[f32;4],sub:[f32;4],source_linear:bool,size:u32,flip_source:bool)->Option<Vec<u8>>{
    let mut parameters=Vec::with_capacity(38);
    for v in base.into_iter().chain(sub){parameters.extend(v.to_bits().to_le_bytes());}
    parameters.push(u8::from(source_linear));parameters.extend(size.to_le_bytes());parameters.push(u8::from(flip_source));
    png_transform_cache::transform("create_eyew_rt_v1",png,&parameters,||{
        let image=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
        let decode:[f32;256]=std::array::from_fn(|i|{
            let v=i as f32/255.;if source_linear{v}else{srgb_channel_to_linear(v)}
        });
        let base_alpha=base[3];
        let base:[f32;3]=std::array::from_fn(|i|srgb_channel_to_linear(base[i]));
        let sub_rgb:[f32;3]=std::array::from_fn(|i|srgb_channel_to_linear(sub[i]));
        let mut result=image::RgbaImage::new(size,size);
        for(y,row)in result.rows_mut().enumerate(){for(x,pixel)in row.enumerate(){
            let shade=sample_red_clamped_lookup(&image,(x as f32+0.5)/size as f32,(y as f32+0.5)/size as f32,&decode,flip_source);
            for c in 0..3{pixel[c]=to_unorm8(linear_channel_to_srgb(lerp_f(sub_rgb[c],base[c],shade)));}
            // Source alpha never supplies the create_eyew output alpha.
            pixel[3]=to_unorm8(lerp_f(sub[3],base_alpha,shade));
        }}
        encode_png(&result)
    })
}
fn sample_red_clamped_lookup(image:&image::RgbaImage,u:f32,v:f32,decode:&[f32;256],flip_source:bool)->f32{
    let(width,height)=image.dimensions();let x=u*width as f32-0.5;let y=v*height as f32-0.5;
    let x0=x.floor();let y0=y.floor();let fx=x-x0;let fy=y-y0;
    let sample=|x:f32,y:f32|{
        let y=y.clamp(0.,(height-1) as f32) as u32;
        decode[image.get_pixel(x.clamp(0.,(width-1) as f32) as u32,if flip_source{height-1-y}else{y})[0] as usize]
    };
    lerp_f(lerp_f(sample(x0,y0),sample(x0+1.,y0),fx),lerp_f(sample(x0,y0+1.),sample(x0+1.,y0+1.),fx),fy)
}
fn bake_eye_white_with_space(png: &[u8], base: [f32; 4], sub: [f32; 4], source_linear: bool) -> Option<Vec<u8>> {
    let mut parameters=Vec::with_capacity(33);
    for v in base.into_iter().chain(sub){parameters.extend(v.to_bits().to_le_bytes());}
    parameters.push(u8::from(source_linear));
    png_transform_cache::transform("create_eyew_v1",png,&parameters,||bake_eye_white_uncached(png,base,sub,source_linear))
}
fn bake_eye_white_uncached(png: &[u8], base: [f32; 4], sub: [f32; 4], source_linear: bool) -> Option<Vec<u8>> {
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    let mut out = image::RgbaImage::new(w, h);
    // create_eyew depends only on the sampled red byte. Evaluate its exact
    // colour/alpha arithmetic once per possible value, rather than repeating
    // the sRGB powers for every texel of a potentially 4K source.
    let lookup: [[u8; 4]; 256] = std::array::from_fn(|red| {
            let shade = if source_linear {red as f32 / 255.0} else {srgb_channel_to_linear(red as f32 / 255.0)};
            let mut q = [0u8; 4];
            for c in 0..3 {
                let v = lerp_f(srgb_channel_to_linear(sub[c]), srgb_channel_to_linear(base[c]), shade);
                q[c] = to_unorm8(linear_channel_to_srgb(v));
            }
            // create_eyew interpolates colour alpha independently of source A.
            q[3] = to_unorm8(lerp_f(sub[3],base[3],shade));
            q
    });
    for (source, destination) in img.pixels().zip(out.pixels_mut()) {
        destination.0 = lookup[source[0] as usize];
    }
    encode_png(&out)
}

/// Koikano/create_eye PS (mm_base/0001): _Color2 -> _Color gradation,
/// multiplication/vivid-light blend in linear space, then the sRGB create RT.
fn bake_iris(
    png: &[u8], pupil: &crate::card::PupilInfo, grad: Option<&[u8]>,
    hl_up: Option<&[u8]>, hl_down: Option<&[u8]>,
    hl_up_color: [f32;4], hl_down_color: [f32;4], hl_up_y:f32, hl_down_y:f32,
) -> Option<Vec<u8>> {
    bake_iris_with_space(png,pupil,grad,hl_up,hl_down,hl_up_color,hl_down_color,hl_up_y,hl_down_y,false)
}

fn bake_iris_with_space(
    png:&[u8],pupil:&crate::card::PupilInfo,grad:Option<&[u8]>,
    hl_up:Option<&[u8]>,hl_down:Option<&[u8]>,hl_up_color:[f32;4],hl_down_color:[f32;4],
    hl_up_y:f32,hl_down_y:f32,source_linear:bool,
) -> Option<Vec<u8>> {
    let mut parameters=Vec::new();parameters.push(u8::from(source_linear));
    for value in pupil.base_color.into_iter().chain(pupil.sub_color).chain(hl_up_color).chain(hl_down_color)
        .chain([pupil.grad_blend,pupil.grad_offset_y,pupil.grad_scale,hl_up_y,hl_down_y]){parameters.extend(value.to_le_bytes());}
    for input in [grad,hl_up,hl_down]{parameters.push(u8::from(input.is_some()));if let Some(input)=input{parameters.extend((input.len() as u64).to_le_bytes());parameters.extend(input);}}
    png_transform_cache::transform("create_iris_rt_v1",png,&parameters,||bake_iris_with_space_uncached(png,pupil,grad,hl_up,hl_down,hl_up_color,hl_down_color,hl_up_y,hl_down_y,source_linear))
}

fn bake_iris_with_space_uncached(
    png:&[u8],pupil:&crate::card::PupilInfo,grad:Option<&[u8]>,
    hl_up:Option<&[u8]>,hl_down:Option<&[u8]>,hl_up_color:[f32;4],hl_down_color:[f32;4],
    hl_up_y:f32,hl_down_y:f32,source_linear:bool,
) -> Option<Vec<u8>> {
    let source=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let gradient=grad.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8()));
    let highlights=[hl_up,hl_down].map(|p|p.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8())));
    let colors=[hl_up_color,hl_down_color];let offsets=[lerp_f(0.1,-0.1,hl_up_y),lerp_f(0.1,-0.1,hl_down_y)];
    let scale=2.0*(1.0-lerp_f(-1.0,1.0,pupil.grad_scale));let offset=lerp_f(-0.5,0.5,pupil.grad_offset_y);
    let sub=pupil.sub_color.map(srgb_channel_to_linear);let base=pupil.base_color.map(srgb_channel_to_linear);
    let mut result=image::RgbaImage::new(512,512);
    for y in 0..512 {for x in 0..512 {
        let u=(x as f32+0.5)/512.0;let v=(y as f32+0.5)/512.0;
        let main=sample_rgba_clamped_space(&source,u,v,!source_linear);
        // create_eye enables AlphaToMask. With the single-sample create RT,
        // coverage below one half leaves its transparent cleared background.
        if main[3]<0.5 {continue;}
        let g=gradient.as_ref().map_or(1.0,|image|sample_rgba_linear(image,u*scale+0.5-scale*0.5,(v-offset)*scale+0.5-scale*0.5)[0]);
        let mut rgb=[0.0;3];
        for c in 0..3 {
            let tint=lerp_f(sub[c],base[c],g);
            let vivid=if main[c]>0.5 {tint/(2.0*(1.0-main[c])).max(0.00001)} else {1.0-(1.0-tint)*0.5/main[c].max(0.00001)};
            let linear=lerp_f(tint*main[c],vivid.clamp(0.0,1.0),pupil.grad_blend)*(main[0]*64.0).clamp(0.0,1.0);
            rgb[c]=linear_channel_to_srgb(linear);
        }
        // Known main_eye/toon_eye draw shaders receive their highlights at
        // draw time. Preserve the legacy fallback only for unknown draw shaders.
        for i in 0..2 {if let Some(highlight)=&highlights[i] {
            let h=sample_rgba_clamped(highlight,u,v-offsets[i]);let a=h[3]*colors[i][3]*main[3];
            for c in 0..3 {rgb[c]=lerp_f(rgb[c],h[c]*colors[i][c],a);}
        }}
        result.put_pixel(x,y,image::Rgba([to_unorm8(rgb[0]),to_unorm8(rgb[1]),to_unorm8(rgb[2]),to_unorm8(main[3])]));
    }}encode_png(&result)
}

#[test]
fn iris_lut_bake_matches_reference(){
    let image=image::RgbaImage::from_fn(16,16,|x,y|{let n=(x+y*16)as u8;image::Rgba([n,255-n,n.wrapping_mul(17),n.wrapping_mul(7)])});
    for linear in [false,true]{for i in 0..257{
        let u=i as f32/113.-0.5;let v=(i*31%257)as f32/127.-0.5;
        assert_eq!(sample_rgba_clamped_space(&image,u,v,linear).map(f32::to_bits),sample_rgba_clamped_space_reference(&image,u,v,linear).map(f32::to_bits),"bilinear sample {i}/{linear}");
    }}
    let png=encode_png(&image).unwrap();
    let grad=encode_png(&image::RgbaImage::from_fn(8,8,|x,y|image::Rgba([(x*19+y*11)as u8,0,0,255]))).unwrap();
    let highlight=encode_png(&image::RgbaImage::from_fn(8,8,|x,y|image::Rgba([255,(x*30)as u8,(y*30)as u8,155]))).unwrap();
    let pupil=crate::card::PupilInfo{base_color:[0.14,0.52,0.87,0.95],sub_color:[0.72,0.22,0.38,0.8],grad_blend:0.65,grad_offset_y:0.31,grad_scale:0.37,..Default::default()};
    for (linear,layers) in [(false,false),(true,false),(false,true),(true,true)]{
        let g=layers.then_some(grad.as_slice());let h=layers.then_some(highlight.as_slice());
        let expected=bake_iris_with_space_reference(&png,&pupil,g,h,h,[0.9,0.2,0.7,0.7],[0.3,0.7,0.8,0.5],0.2,0.8,linear).unwrap();
        let optimized=bake_iris_with_space_uncached(&png,&pupil,g,h,h,[0.9,0.2,0.7,0.7],[0.3,0.7,0.8,0.5],0.2,0.8,linear).unwrap();
        assert_eq!(optimized,expected,"uncached iris pixels {linear}/{layers}");
        for _ in 0..2{assert_eq!(bake_iris_with_space(&png,&pupil,g,h,h,[0.9,0.2,0.7,0.7],[0.3,0.7,0.8,0.5],0.2,0.8,linear),Some(expected.clone()),"cold/warm iris result {linear}/{layers}");}
    }
}

#[cfg(test)]
fn bake_iris_with_space_reference(
    png:&[u8],pupil:&crate::card::PupilInfo,grad:Option<&[u8]>,
    hl_up:Option<&[u8]>,hl_down:Option<&[u8]>,hl_up_color:[f32;4],hl_down_color:[f32;4],
    hl_up_y:f32,hl_down_y:f32,source_linear:bool,
) -> Option<Vec<u8>> {
    let source=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let gradient=grad.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8()));
    let highlights=[hl_up,hl_down].map(|p|p.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8())));
    let colors=[hl_up_color,hl_down_color];let offsets=[lerp_f(0.1,-0.1,hl_up_y),lerp_f(0.1,-0.1,hl_down_y)];
    let scale=2.0*(1.0-lerp_f(-1.0,1.0,pupil.grad_scale));let offset=lerp_f(-0.5,0.5,pupil.grad_offset_y);
    let mut result=image::RgbaImage::new(512,512);
    for y in 0..512 {for x in 0..512 {
        let u=(x as f32+0.5)/512.0;let v=(y as f32+0.5)/512.0;
        let main=sample_rgba_clamped_space_reference(&source,u,v,!source_linear);
        // create_eye enables AlphaToMask. With the single-sample create RT,
        // coverage below one half leaves its transparent cleared background.
        if main[3]<0.5 {continue;}
        let g=gradient.as_ref().map_or(1.0,|image|sample_rgba_linear_reference(image,u*scale+0.5-scale*0.5,(v-offset)*scale+0.5-scale*0.5)[0]);
        let mut rgb=[0.0;3];
        for c in 0..3 {
            let tint=lerp_f(srgb_channel_to_linear(pupil.sub_color[c]),srgb_channel_to_linear(pupil.base_color[c]),g);
            let vivid=if main[c]>0.5 {tint/(2.0*(1.0-main[c])).max(0.00001)} else {1.0-(1.0-tint)*0.5/main[c].max(0.00001)};
            let linear=lerp_f(tint*main[c],vivid.clamp(0.0,1.0),pupil.grad_blend)*(main[0]*64.0).clamp(0.0,1.0);
            rgb[c]=linear_channel_to_srgb(linear);
        }
        // Known main_eye/toon_eye draw shaders receive their highlights at
        // draw time. Preserve the legacy fallback only for unknown draw shaders.
        for i in 0..2 {if let Some(highlight)=&highlights[i] {
            let h=sample_rgba_clamped_reference(highlight,u,v-offsets[i]);let a=h[3]*colors[i][3]*main[3];
            for c in 0..3 {rgb[c]=lerp_f(rgb[c],h[c]*colors[i][c],a);}
        }}
        result.put_pixel(x,y,image::Rgba([to_unorm8(rgb[0]),to_unorm8(rgb[1]),to_unorm8(rgb[2]),to_unorm8(main[3])]));
    }}encode_png(&result)
}

#[cfg(test)]
fn sample_rgba_clamped_space_reference(image:&image::RgbaImage,u:f32,v:f32,linear:bool)->[f32;4] {
    let (width,height)=image.dimensions();let x=u*width as f32-0.5;let y=v*height as f32-0.5;
    let x0=x.floor();let y0=y.floor();let fx=x-x0;let fy=y-y0;
    let pixel=|x:f32,y:f32| image.get_pixel(x.clamp(0.,(width-1) as f32) as u32,y.clamp(0.,(height-1) as f32) as u32).0;
    let a=pixel(x0,y0);let b=pixel(x0+1.,y0);let c=pixel(x0,y0+1.);let d=pixel(x0+1.,y0+1.);
    std::array::from_fn(|i| {
        let channel=|p:[u8;4]| {let v=p[i] as f32/255.0;if linear&&i<3 {srgb_channel_to_linear(v)}else{v}};
        lerp_f(lerp_f(channel(a),channel(b),fx),lerp_f(channel(c),channel(d),fx),fy)
    })
}

#[cfg(test)]
fn sample_rgba_linear_reference(image:&image::RgbaImage,u:f32,v:f32)->[f32;4]{sample_rgba_clamped_space_reference(image,u,v,true)}
#[cfg(test)]
fn sample_rgba_clamped_reference(image:&image::RgbaImage,u:f32,v:f32)->[f32;4]{sample_rgba_clamped_space_reference(image,u,v,false)}
fn srgb_channel_to_linear(value:f32)->f32 {
    if value<=0.04045 {value/12.92}else{((value+0.055)/1.055).powf(2.4)}
}
fn linear_channel_to_srgb(value:f32)->f32 {
    if value<=0.0031308 {value*12.92}else{1.055*value.max(0.0).powf(1.0/2.4)-0.055}
}
fn to_unorm8(value:f32)->u8 {(value.clamp(0.0,1.0)*255.0).round() as u8}
/// The 8-bit sRGB -> linear and linear -> sRGB conversions of the texture passes, as tables: the same expressions
/// per byte value, without a powf per channel (a 4K normal map spent ~0.3 s in them).
fn srgb_to_linear_u8(value:u8)->u8 {
    static TABLE:std::sync::OnceLock<[u8;256]>=std::sync::OnceLock::new();
    TABLE.get_or_init(||std::array::from_fn(|v|(srgb_channel_to_linear(v as f32/255.)*255.).round().clamp(0.,255.) as u8))[value as usize]
}
fn linear_to_srgb_u8(value:u8)->u8 {
    static TABLE:std::sync::OnceLock<[u8;256]>=std::sync::OnceLock::new();
    TABLE.get_or_init(||std::array::from_fn(|v|{
        let linear = v as f32 / 255.0;
        let srgb = if linear <= 0.0031308 { linear * 12.92 } else { 1.055 * linear.powf(1.0 / 2.4) - 0.055 };
        (srgb * 255.0).round().clamp(0.0, 255.0) as u8
    }))[value as usize]
}
fn sample_rgba_linear(image:&image::RgbaImage,u:f32,v:f32)->[f32;4] {
    sample_rgba_clamped_space(image,u,v,true)
}

fn composite_over(under: &[u8], over: &[u8]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(under).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    for y in 0..dh {
        for x in 0..dw {
            let sx = x * sw / dw.max(1);
            let sy = y * sh / dh.max(1);
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1)).0;
            let a = p[3] as f32 / 255.0;
            if a <= 0.0 {
                continue;
            }
            let d = dst.get_pixel_mut(x, y);
            d[0] = (d[0] as f32 * (1.0 - a) + p[0] as f32 * a) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + p[1] as f32 * a) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + p[2] as f32 * a) as u8;
            d[3] = d[3].max(p[3]);
        }
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// `ChaFileFace.headId` is a local slot after Sideloader. Vanilla `bo_head`
/// only has ids 0/200/201, so `get(100, headId)` misses mod heads (this card:
/// headId=1, KKEx guid `HeadMod`). Without a list row, CreateFaceTexture never
/// runs and the face keeps the bundle's empty render-texture (black).
fn resolve_face_entry(
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

fn face_named_png(
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

fn face_base_color_space(
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
fn face_color_space_cached_lookup_matches_legacy_on_installed_sources(){
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
fn face_base_color_space_reference(
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
fn compose_face_texture(
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

fn compose_face_texture_legacy(
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

fn face_cavity_is_at_bottom(base: &[u8]) -> bool {
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

fn flip_png_vertical(png: &[u8]) -> Option<Vec<u8>> {
    png_transform_cache::transform("flip_v_v1",png,&[],||flip_png_vertical_uncached(png))
}
fn flip_png_vertical_uncached(png: &[u8]) -> Option<Vec<u8>> {
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

fn linear_texture_to_srgb_png(png: &[u8]) -> Option<Vec<u8>> {
    png_transform_cache::transform("linear_to_srgb_v1",png,&[],||linear_texture_to_srgb_png_uncached(png))
}
fn linear_texture_to_srgb_png_uncached(png: &[u8]) -> Option<Vec<u8>> {
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
fn load_card_makeup_png(
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
fn overlay_face_create_cheek(base: &[u8], cheek: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
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
fn white_on_black_alpha(png: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
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
fn rgb_on_black_alpha(png: &[u8]) -> Option<Vec<u8>> {
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

fn apply_uv_st(uvs: &mut [[f32; 2]], scale: [f32; 2], offset: [f32; 2]) {
    // Clamp belongs to the texture sampler after triangle interpolation.
    for uv in uvs {
        uv[0] = uv[0] * scale[0] + offset[0];
        uv[1] = uv[1] * scale[1] + offset[1];
    }
}

/// Encode a linear card colour to the sRGB space our textures live in.
fn lin_to_srgb(c: [f32; 4]) -> [f32; 4] {
    let f = |x: f32| x.clamp(0.0, 1.0).powf(1.0 / 2.2);
    [f(c[0]), f(c[1]), f(c[2]), c[3]]
}

fn mix4(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
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
fn face_detail_entry(
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String, PathBuf>,
    cat: i32,
    id: i32,
) -> Option<ListEntry> {
    face_detail_entry_for_property(vanilla,mod_catalog,card,guid_index,cat,id,None)
}

fn face_detail_entry_for_property(vanilla:&ChaListCatalog,mod_catalog:&ChaListCatalog,card:&CardInfo,
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

fn skin_catalog_texture(game:&Path,vanilla:&ChaListCatalog,mods:&ChaListCatalog,
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
fn detail_asset_names(base: &str, head_suffix: bool, head_id: i32) -> Vec<String> {
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

fn head_uses_item_draw(mesh: &ExtractedMesh) -> bool {
    mesh.mat.as_ref().is_some_and(|m|
        m.shader.eq_ignore_ascii_case("Koikano/main_clothes_item")
        || m.shader.eq_ignore_ascii_case("Shader Forge/main_item"))
}

fn head_uses_eye_draw(mesh:&ExtractedMesh)->bool {
    mesh.mat.as_ref().is_some_and(|m| m.shader.eq_ignore_ascii_case("Koikano/main_eye")
        || m.shader.eq_ignore_ascii_case("Shader Forge/toon_eye_lod0")
        || (m.shader.is_empty() && m.float("_KanoVerShift").is_some()
            && m.float("_rotation1").is_some() && m.slot("_overtex1").is_some()))
}

fn source_eye_draw(game:&Path,vanilla:&ChaListCatalog,mods:&ChaListCatalog,card:&CardInfo,
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
fn apply_head_draw_material_overrides(card: &CardInfo, mesh: &mut ExtractedMesh) {
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
fn eyeline_shadow_mesh(source:&crate::unity::ParsedBundle,stroke:&ExtractedMesh)->ExtractedMesh {
    let mut shadow=stroke.clone();shadow.name=format!("{}_kage",stroke.name);
    shadow.mat=source.renderer_material_slot(&stroke.renderer_ids,1)
        .or_else(||source.material_named("cf_m_eyeline_kage"));
    if shadow.mat.is_none() {
        eprintln!("ok-extract: eyeline shadow material slot1 missing renderer={:?}",stroke.renderer_ids);
        shadow.mat=stroke.mat.clone().map(|mut m| {m.name="cf_m_eyeline_kage".into();m});
    }
    shadow.texture_png=None;shadow.runtime_textures.remove("_MainTex");shadow
}

fn head_detail_source_spec(card:&CardInfo,ln:&str)->Option<(i32,i32,Vec<&'static str>,[f32;4])> {
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

fn head_detail_original_wrap(game:&Path,vanilla:&ChaListCatalog,mods:&ChaListCatalog,
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
fn runtime_texture_catalog_wrap_matches_native() {
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

fn resolve_head_detail_texture(
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

fn compose_body_texture(
    mesh: &mut ExtractedMesh,
    card: Option<&CardInfo>,
    skin: [f32; 4],
    sub: [f32; 4],
    body_texs: &[(String, Vec<u8>)],
    outfit: i32,
) {let _spot=crate::profile::spot("compose_body_texture");
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

fn is_head_detail(name: &str) -> bool {
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
fn mouth_ptn_override() -> Option<usize> {
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
fn mouth_open_percent() -> f32 {
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
fn is_genital_display_mesh(name: &str) -> bool {
    // Separate penis and scrotum renderers. The condom (o_gomu) stays hidden.
    is_penis_mesh(name) || is_balls_mesh(name)
}

fn is_penis_mesh(name: &str) -> bool {
    matches!(name.to_ascii_lowercase().as_str(), "o_dankon" | "cm_o_dan00")
}

fn is_balls_mesh(name: &str) -> bool {
    matches!(name.to_ascii_lowercase().as_str(), "o_dan_f" | "cm_o_dan_f")
}

/// UncensorSelector.SetSkinColor builds a create-RT from the part's main
/// texture and color mask, then assigns that RT as the draw `_MainTex`.
/// The separate genital mesh has no such RT today, so it samples black.
fn paint_uncensor_genital_mesh(
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

fn adopt_uncensor_genitals(
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
    let explicit = match kind {
        crate::mods::UncensorGenitalKind::Penis => card.penis_guid.as_deref(),
        crate::mods::UncensorGenitalKind::Balls => card.balls_guid.as_deref(),
    };
    let Some((source, bytes)) = crate::mods::uncensor_genital_for_card(game_root, kind, explicit, card.sex, seed) else {
        if !replaced_body {
            let before = sources.len();
            sources.retain(|mesh| !is_kind(&mesh.name));
            if sources.len() != before {
                eprintln!("ok-extract: removed vanilla {kind:?} meshes; no non-mosaic uncensor is installed");
            }
        } else {
            eprintln!("ok-extract: {kind:?} uncensor was not found; the body mesh is unchanged");
        }
        return;
    };
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
    if painted.is_empty() {
        eprintln!("ok-extract: {kind:?} uncensor {} had no paintable mesh", source.guid);
        if !replaced_body { sources.retain(|mesh| !is_kind(&mesh.name)); }
        return;
    }
    sources.retain(|mesh| !is_kind(&mesh.name));
    sources.extend(painted);
}

fn is_hidden_part(name: &str) -> bool {
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

fn me_slot(role: &str, property: Option<&str>) -> Option<i32> {
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

fn me_object_type(role: &str) -> Option<i32> {
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
fn me_hides_mesh(
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

fn me_face_mat_names(mesh_name: &str) -> Vec<String> {
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
fn native_item_has_texture_property(property: &str) -> bool {
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

fn me_texture<'a>(
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
fn resolve_material_alpha_masks(game_root: &Path, meshes: &mut [ExtractedMesh]) {
    let mut found: std::collections::HashMap<String, Option<Vec<u8>>> = std::collections::HashMap::new();
    let mask_dir = game_root.join("abdata/chara");
    let mut bundles: Vec<PathBuf> = crate::gamefs::read_dir(&mask_dir)
        .into_iter().flatten().filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.file_name().and_then(|s| s.to_str())
            .is_some_and(|s| s.starts_with("mt_mask_") && s.ends_with(".unity3d")))
        .collect();
    bundles.sort();
    for mesh in meshes {
        if mesh.alpha_mask_png.is_some() { continue; }
        let Some(name) = mesh.mat.as_ref().and_then(|m| m.slot("_AlphaMask")) else { continue; };
        let png = found.entry(name.to_ascii_lowercase()).or_insert_with(|| {
            bundles.iter().find_map(|p| crate::unity::named_png_from_bundle(p, &[name]).map(|(_,png)| orient_raw_mask(png)))
        });
        mesh.alpha_mask_png = png.clone();
    }
}

/// ChaControl.LoadHairGlossMask resolves ChaFileHair.glossId in category 433,
/// then applies that shared texture to native hair renderers. HairAccessory-
/// Customizer uses the same texHairGloss for custom hair accessory renderers.
fn selected_hair_gloss_texture(
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
        let catalog = catalog_from_zipmod(zip);
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

fn set_selected_hair_gloss(meshes: &mut [ExtractedMesh], gloss: Option<&[u8]>) {
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
fn configured_ramp_png_uncached(game_root: &Path, vanilla: &ChaListCatalog, id: i32, guid: &str) -> Option<Vec<u8>> {
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
fn configured_shadow_depth(game_root: &Path) -> f32 {
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
fn configured_line_width(game_root: &Path) -> f32 {
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
fn configured_line_depth(game_root: &Path) -> f32 {
    crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml"))
        .ok()
        .and_then(|xml| xml.split_once("<lineDepth>")
            .and_then(|(_, tail)| tail.split_once("</lineDepth>"))
            .and_then(|(value, _)| value.trim().parse::<f32>().ok()))
        .unwrap_or(1.0)
        .clamp(0.0, 1.0)
}

/// ChaShader.ChangeShaderType maps GraphicData.shaderType to _KanoVerShift.
fn configured_kano_ver_shift(game_root: &Path) -> f32 {
    let shader_type = crate::gamefs::read_to_string(game_root.join("UserData/config/system.xml"))
        .ok()
        .and_then(|xml| xml.split_once("<shaderType>")
            .and_then(|(_, tail)| tail.split_once("</shaderType>"))
            .and_then(|(value, _)| value.trim().parse::<i32>().ok()))
        .unwrap_or(0);
    if shader_type == 0 { 0.0 } else { 1.0 }
}

fn me_maintex<'a>(
    map: Option<&'a crate::card::MeTexMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &ExtractedMesh,
) -> Option<&'a [u8]> {let _spot=crate::profile::spot("me_maintex");
    me_texture(map, outfit, role, property, mesh, "maintex")
}

fn me_material_color(
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

fn me_shadow_color(
    map: Option<&crate::card::MeColorMap>, outfit: i32, role: &str,
    property: Option<&str>, mesh: &ExtractedMesh,
) -> Option<[f32; 4]> {
    me_material_color(map, outfit, role, property, mesh, "shadowcolor")
}

/// ChaControl's actual instantiated names and copyDynamicBone arguments.
fn clothes_load_settings(property: &str) -> (bool, &'static str) {
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

/// One push_meshes call kept for a PushBatch: what its rig, texture transforms and emit read (OK_FIRSTLOAD=A2).
pub(crate) struct PushCtx<'a, 's> {
    out_dir: Option<&'a Path>,
    game_root: &'a Path,
    role: String,
    slot: Option<i32>,
    category_no: Option<i32>,
    guid: Option<String>,
    property: Option<String>,
    name: Option<String>,
    // Borrowed for an immediate call; a kept call owns them, since a later pass can change the body (clothes).
    body_skel: Option<std::borrow::Cow<'s, crate::pose::Skeleton>>,
    head_skel: Option<std::borrow::Cow<'s, crate::pose::Skeleton>>,
    parent_key: Option<String>,
    hair_color: Option<[[f32; 4]; 3]>,
    part_skel: Option<crate::pose::Skeleton>,
    part_local: Option<crate::pose::Mat4>,
    me_tex: Option<&'a crate::card::MeTexMap>,
    me_color: Option<&'a crate::card::MeColorMap>,
    outfit: i32,
    alpha_mask: Option<(Vec<u8>, f32, f32)>,
    accessory_assignment: Option<crate::unity::AccessoryBodyBinding>,
    convert_normals: bool,
    // This thread's state when the call was made: the accessory moves (begin_acc_place), the clothes states and the
    // profile switches, re-applied on the worker that runs the item.
    acc_place: Option<[crate::pose::Mat4; 2]>,
    clothes_active: Option<[u8; 9]>,
    clothes_requested: Option<[u8; 9]>,
    overrides: Vec<(&'static str, Option<&'static str>)>,
    // The hair gloss texture's sampling for the emitted parts that carry one (accessory_extract).
    hair_gloss_sampling: Option<[i32; 5]>,
}
type PushWork = (usize, ExtractedMesh, bool, bool);
type PushDone = (usize, ExtractedMesh, Option<crate::session::AccessoryRuntime>);

/// OK_FIRSTLOAD=A2: push_meshes calls of one card open, kept until `flush`. A call has only a mesh or two, so
/// running its own meshes in parallel gained nothing; the batch runs every kept mesh's rig and texture transforms
/// (PNG decode/re-encode, flips, tints, masks, normal conversion) at once, then emits them in call order, which keeps
/// the output identical. Nothing between the calls reads the emitted parts.
#[derive(Default)]
pub(crate) struct PushBatch<'a> { groups: Vec<(PushCtx<'a, 'static>, Vec<PushWork>)> }
impl<'a> PushBatch<'a> {
    pub(crate) fn flush(&mut self, parts: &mut Vec<ManifestPart>, session_parts: &mut Vec<crate::session::SessionPart>) -> Result<(), String> {
        let groups = std::mem::take(&mut self.groups);
        let (ctxs, works): (Vec<PushCtx<'a, 'static>>, Vec<Vec<PushWork>>) = groups.into_iter().unzip();
        let items: Vec<(usize, PushWork)> = works.into_iter().enumerate().flat_map(|(g, w)| w.into_iter().map(move |item| (g, item))).collect();
        let done: Vec<(usize, PushDone)> = {
            use rayon::prelude::*;
            crate::session::native_parallel_work(|| items.into_par_iter().map(|(g, item)| (g, ctxs[g].run_on_worker(item))).collect())
        };
        let mut by_group: Vec<Vec<PushDone>> = (0..ctxs.len()).map(|_| Vec::new()).collect();
        for (g, d) in done { by_group[g].push(d); }
        for (ctx, done) in ctxs.iter().zip(by_group) { ctx.emit(done, parts, session_parts)?; }
        Ok(())
    }
}

impl<'a, 's> PushCtx<'a, 's> {
    fn run_on_worker(&self, item: PushWork) -> PushDone {
        let _profile = crate::profile::scope(&self.overrides);
        let _active = self.clothes_active.map(crate::unity::set_session_clothes_states);
        let _requested = self.clothes_requested.map(crate::unity::request_clothes_states);
        let (t, name) = (std::time::Instant::now(), item.1.name.clone());
        let done = self.transform(item);
        if std::env::var_os("OK_A2_TRACE").is_some() { eprintln!("ok-extract: A2 item {} {} {:.1}ms thread={:?}", self.role, name, t.elapsed().as_secs_f64()*1000., std::thread::current().id()); }
        done
    }
    /// The runtime rig and texture transforms of one mesh.
    fn transform(&self, (i, mut mesh, source_program, native_hair_color): PushWork) -> PushDone {
        let (role, property, game_root, outfit) = (self.role.as_str(), &self.property, self.game_root, self.outfit);
        let (body_skel, head_skel, part_skel, part_local) = (self.body_skel.as_deref(), self.head_skel.as_deref(), self.part_skel.as_ref(), self.part_local.as_ref());
        let (parent_key, hair_color, me_tex, me_color) = (self.parent_key.as_deref(), self.hair_color, self.me_tex, self.me_color);
        let (accessory_assignment, acc_place, convert_normals) = (self.accessory_assignment.as_ref(), &self.acc_place, self.convert_normals);
        let alpha_mask = self.alpha_mask.as_ref().map(|(mask, scale, offset)| (mask.as_slice(), *scale, *offset));
            let mut runtime_accessory = if role.contains("acc")
                && std::env::var("OK_RUNTIME_ACCESSORY").ok().as_deref() != Some("0") {
                let key = parent_key.unwrap_or("");
                let parent = bone_world(body_skel, head_skel, key);
                let locals = acc_place.clone()
                    .or_else(|| part_local.map(|l| [*l, identity_move()]));
                match (parent, locals) {
                    (Some(parent), Some(locals)) => {
                        if let Some(binding)=accessory_assignment.filter(|binding|ids_hit(&mesh.renderer_ids,&binding.renderers)) {
                            crate::session::AccessoryRuntime::from_assigned_accessory(&mesh,part_skel,key,&locals,&parent,binding.armature_root)
                        } else {crate::session::AccessoryRuntime::from_source(&mesh,part_skel,key,&locals,&parent)}
                    },
                    _ => None,
                }
            } else if role.contains("hair") {
                bone_world(body_skel, head_skel, "cf_J_FaceUp_ty").and_then(|parent|
                    crate::session::AccessoryRuntime::from_hair_source(&mesh, part_skel, &parent))
            } else if role.starts_with("clothes") {
                let (copy_dynamic, component) = clothes_load_settings(property.as_deref().unwrap_or(""));
                body_skel.and_then(|body| crate::session::AccessoryRuntime::from_clothes_source(
                    &mesh, part_skel, body, copy_dynamic, component, part_local, crate::dbone::implant::installed(game_root)))
            } else { None };
            if role.starts_with("clothes") {if let Some(runtime)=&mut runtime_accessory {runtime.coordinate_clothes();}}
            if let Some(runtime) = &mut runtime_accessory {
                let flags = crate::dbone::game_colliders::Flags::default();
                if let Some((positions, normals,tangents)) = runtime.update_render(body_skel, head_skel, &crate::dbone::BodyColliderSet::default(), flags, 0.0) {
                    mesh.positions = positions; mesh.normals = normals;mesh.tangents=tangents;
                }
            } else {
                rig_mesh(&mut mesh, role, body_skel, head_skel, parent_key, part_skel, part_local, None, acc_place.clone());
            }
            if std::env::var("OK_MATDIAG").is_ok() {
                if let Some(mm) = mesh.mat.as_ref() {
                    let slots: Vec<&str> = mm.tex_slots.iter().map(|(k, _)| k.as_str()).collect();
                    eprintln!(
                        "ok-extract: MAT {} role={} shader={} q={} kw={} colors={:?} slots={:?}",
                        mesh.name,
                        role,
                        mm.shader,
                        mm.render_queue,
                        mm.keywords.join(","),
                        mm.colors,
                        slots
                    );
                }
            }
            // Accessory colour is baked in `color_accessory_meshes`. A second
            // name-contains-hair tint here would multiply ColorMatch / ME again.
            let hair_like = role.contains("hair") && !role.contains("acc");
            if hair_like && std::env::var("OK_HAIR_DUMP").is_ok() {
                let stem: String = mesh
                    .name
                    .chars()
                    .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
                    .collect();
                if let Some(t) = mesh.texture_png.as_deref() {
                    let _ = std::fs::write(
                        format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\hd_main_{stem}.png"),
                        t,
                    );
                }
                if let Some(t) = mesh.color_mask_png.as_deref() {
                    let _ = std::fs::write(
                        format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\hd_mask_{stem}.png"),
                        t,
                    );
                }
            }
            if source_program || native_hair_color {
                // Both shader families sample the original full-resolution mask.
                if hair_like {
                    if let Some(cols) = hair_color {
                        for (key, color) in ["_Color", "_Color2", "_Color3"].iter().zip(cols.iter()) {
                            set_material_color(&mut mesh, key, *color);
                        }
                    }
                    if mesh.texture_png.is_none() {
                        mesh.texture_png = crate::unity::solid_png([1.0; 4], 8, 8);
                    }
                }
            } else if hair_like && std::env::var("OK_HAIR_RAW").is_ok() {
                // diagnostic: keep the raw hair texture (skip the colour-mask tint)
            } else if hair_like {
                if let Some(cols) = hair_color {
                    let white = crate::unity::solid_png([1.0, 1.0, 1.0, 1.0], 8, 8);
                    let png = mesh.texture_png.as_deref().or(white.as_deref());
                    if let Some(png) = png {
                        if let Some(tinted) = crate::unity::hair_get_diffuse(
                            png,
                            mesh.color_mask_png.as_deref(),
                            cols[0],
                            cols[1],
                            cols[2],
                        ) {
                            mesh.texture_png = Some(tinted);
                        }
                    } else {
                        mesh.texture_png = crate::unity::solid_png(cols[0], 64, 64);
                    }
                }
            } else if role.starts_with("clothes") {
                if let Some(cols) = hair_color {
                    if let Some(png) = mesh.texture_png.as_ref() {
                        if let Some(tinted) = crate::unity::tint_png(png, cols[0]) {
                            mesh.texture_png = Some(tinted);
                        }
                    }
                }
            }
            // Accessories already swapped ME MainTex inside `color_accessory_meshes`
            // and baked ColorMask on top. Replacing again would drop that bake.
            if role.contains("acc") {
                // already baked
            } else if let Some(png) = me_maintex(me_tex, outfit, role, property.as_deref(), &mesh) {
                eprintln!(
                    "ok-extract: ME MainTex role={role} name={} mat={} bytes={}",
                    mesh.name,
                    mesh.mat.as_ref().map(|m| m.name.as_str()).unwrap_or(""),
                    png.len()
                );
                mesh.texture_png = Some(png.to_vec());
                mark_material_editor_main_texture(&mut mesh);
            }
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "detailmask") {
                mesh.detail_mask_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_DetailMask");}
                else {mesh.runtime_textures.remove("_DetailMask");}
            }
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "linemask") {
                mesh.line_mask_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_LineMask");}
            }
            // A resolved material's null NormalMap is authoritative. A decoded
            // normal retained from an earlier renderer/material must not survive
            // a null binding. A later ME assignment still replaces it below.
            clear_unbound_normal_map(&mut mesh);
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "normalmap") {
                mesh.normal_map_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_NormalMap");}
            }
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "alphamask") {
                mesh.alpha_mask_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_AlphaMask");}
            }
            // MaterialEditor replaces _NormalMap with a blitted RenderTexture after
            // it applies texture overrides. Its normal_convert fragment writes
            // (1, source.g, source.b, source.a), not just a vertically flipped
            // source texture. Match the texture actually sampled by the game.
            if convert_normals {
                if let Some(png) = mesh.normal_map_png.take() {
                    let overridden=me_texture(me_tex,outfit,role,property.as_deref(),&mesh,"normalmap").is_some();
                    let srgb=overridden || mesh.runtime_textures.get("_OKSourceNormalMap").is_some_and(|s|s[3]==1);
                    let converted=convert_runtime_normal_map_rows(png,srgb,!overridden);
                    // MaterialEditor blits a linear normal source into a sRGB
                    // RenderTexture. Its stored RGB is sRGB encoded, then the
                    // native shader samples it through a sRGB sampler. Keep the
                    // legacy normal representation unchanged.
                    mesh.normal_map_png=Some(if source_program {
                        mark_native_runtime_normal_texture(&mut mesh,"_NormalMap");
                        linear_texture_to_srgb_png(&converted).unwrap_or(converted)
                    } else {converted});
                }
            }
            if role.contains("acc") {
                // Static Unity Texture2D rows are bottom-up. Accessory colour
                // baking preserves those rows; card ME PNGs are already top-down.
                // Normalize at the rendering boundary, before paired alpha masks,
                // because UE sends (u,1-v) for the native mesh UVs.
                if me_maintex(me_tex, outfit, role, property.as_deref(), &mesh).is_none() {
                    if let Some(png) = mesh.texture_png.take() {
                        mesh.texture_png = Some(flip_png_v(&png).unwrap_or(png));
                    }
                }
                let flip: [bool; 4] = ["detailmask", "linemask", "alphamask", "normalmap"]
                    .map(|key| me_texture(me_tex, outfit, role, property.as_deref(), &mesh, key).is_none());
                for (needs_flip, data) in [
                    // ParsedBundle already calls orient_raw_mask for these
                    // authored Texture2D slots. The legacy accessory bake expects
                    // its old orientation; the native shader samples the source
                    // UV and must not reverse the rows for a second time.
                    (flip[0] && !source_program, &mut mesh.detail_mask_png),
                    (flip[1] && !source_program, &mut mesh.line_mask_png),
                    (flip[2] && !source_program, &mut mesh.alpha_mask_png),
                    (flip[3] && !convert_normals, &mut mesh.normal_map_png),
                    (true, &mut mesh.hair_gloss_png),
                ] {
                    if needs_flip {
                        if let Some(png) = data.take() {
                            *data = Some(flip_png_v(&png).unwrap_or(png));
                        }
                    }
                }
            }
            if source_program || native_hair_color {
                if let Some(overrides)=me_color {
                    if let (Some(ot),Some(s),Some(mat))=(me_object_type(role),me_slot(role,property.as_deref()),mesh.mat.as_mut()) {
                        let material=crate::card::me_mat_key(&mat.name);
                        for ((t,coord,slot,name,prop),value) in overrides {
                            if *t==ot && *coord==(if ot==4 {0}else{outfit}) && *slot==s && *name==material {
                                let requested=if prop.starts_with('_'){prop.clone()}else{format!("_{prop}")};
                                // MeColorMap intentionally normalizes names. Replace
                                // the authored canonical key so exact-case lookup
                                // cannot keep the pre-ME value instead.
                                let key=mat.colors.keys().filter(|k|k.eq_ignore_ascii_case(&requested)).min().cloned().unwrap_or(requested);
                                mat.colors.insert(key,*value);
                            }
                        }
                    }
                }
                let me_color_mask = me_texture(me_tex,outfit,role,property.as_deref(),&mesh,"colormask");
                if native_hair_color || source_program {
                    if let Some(png) = me_color_mask {
                        mesh.color_mask_png = Some(png.to_vec());
                        // SetTexture changes the source to a runtime texture.
                        if source_program {mark_material_editor_texture(&mut mesh,"_ColorMask");}
                        else {mesh.runtime_textures.remove("_ColorMask");}
                    }
                }
                if let Some(png)=mesh.color_mask_png.take() {
                    mesh.color_mask_png=Some(if me_color_mask.is_some() {
                        png
                    } else {flip_png_v(&png).unwrap_or(png)});
                }
            }
            if source_program {
                for name in ["_NormalMapDetail","_AnotherRamp","_EmissionMask","_ReflectionMapCap","_ReflectMapDetail","_Texture2","_Texture3","_liquidmask"] {
                    let property_name=name.trim_start_matches('_').to_ascii_lowercase();
                    let override_png=me_texture(me_tex,outfit,role,property.as_deref(),&mesh,&property_name).map(|png|png.to_vec());
                    let is_override=override_png.is_some();
                    let data=override_png.or_else(||mesh.shader_extra_textures.remove(name));
                    if let Some(data)=data {
                        if is_override {mark_material_editor_texture(&mut mesh,name);}
                        let data=if name=="_NormalMapDetail" && convert_normals {
                            let srgb=is_override || mesh.runtime_textures.get("_OKSourceNormalMapDetail").is_some_and(|s|s[3]==1);
                            mark_native_runtime_normal_texture(&mut mesh,name);
                            let converted=convert_runtime_normal_map_rows(data,srgb,!is_override);
                            linear_texture_to_srgb_png(&converted).unwrap_or(converted)
                        }else if is_override {data}else{flip_png_v(&data).unwrap_or(data)};
                        mesh.shader_extra_textures.insert(name.into(),data);
                    }
                }
                if mesh.texture_png.is_none() {mesh.texture_png=crate::unity::solid_png([1.;4],8,8);}
            } else if mesh.mat.as_ref().is_some_and(|mat| matches!(mat.shader.as_str(), "Shader Forge/main_StandardMDK_studio" | "Standard" | "Standard (Specular setup)" | "Legacy Shaders/Particles/Additive")) {
                // Like MainTex above, every MDK slot follows the native mesh's
                // (u, 1-v) rendering UV convention. Keep their rows aligned.
                // Studio props have role "part", so the accessory-only MainTex
                // normalization above did not run for them.
                if !role.contains("acc") && me_maintex(me_tex, outfit, role, property.as_deref(), &mesh).is_none() {
                    if let Some(png) = mesh.texture_png.take() {
                        mesh.texture_png = Some(flip_png_v(&png).unwrap_or(png));
                    }
                }
                for name in ["_BumpMap", "_ParallaxMap", "_MetallicGlossMap", "_EmissionMap"] {
                    if let Some(png) = mesh.shader_extra_textures.remove(name) {
                        mesh.shader_extra_textures.insert(name.into(), flip_png_v(&png).unwrap_or(png));
                    }
                }
            } else if !role.contains("acc") && mesh.mat.as_ref().is_some_and(|mat| { let family = shader_family(&mat.shader); family.contains("main_item_studio") && !family.contains("_add") }) {
                // Studio items (role "part") drawn by main_item_studio: like the MDK slots above and the accessory MainTex,
                // the main texture and colour mask follow the native mesh's (u, 1-v) UV convention (the parasol's canopy
                // sampled its wood finial and ColorMask's black half before; native: the coloured canopy). Not the light
                // effects (main_item_studio_add): their rows were already right (0001's sword light sampled the texture's
                // filled lower half, a yellow wash, once flipped here; native: the streaked upper half, a rainbow).
                if me_maintex(me_tex, outfit, role, property.as_deref(), &mesh).is_none() {
                    if let Some(png) = mesh.texture_png.take() { mesh.texture_png = Some(flip_png_v(&png).unwrap_or(png)); }
                }
                if me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "colormask").is_none() {
                    if let Some(png) = mesh.color_mask_png.take() { mesh.color_mask_png = Some(flip_png_v(&png).unwrap_or(png)); }
                }
            } else if mesh.hair_shader_hint || mesh.mat.as_ref().is_some_and(|mat| is_native_hair_shader(&mat.shader)) {
                // Forge hair samples _AnotherRamp with N dot view. Preserve a
                // MaterialEditor replacement and the original texture's sampling
                // provenance independently of the global _RampG. Match the
                // session's hair_shader_hint fallback for unresolved Shader PPtrs.
                let override_png = me_texture(me_tex,outfit,role,property.as_deref(),&mesh,"anotherramp")
                    .map(|png|png.to_vec());
                let replaced = override_png.is_some();
                let data = override_png.or_else(||mesh.shader_extra_textures.remove("_AnotherRamp"));
                if let Some(data) = data {
                    if replaced { mesh.runtime_textures.remove("_AnotherRamp"); }
                    let data = if replaced { data } else { flip_png_v(&data).unwrap_or(data) };
                    mesh.shader_extra_textures.insert("_AnotherRamp".into(), data);
                }
            }
            // Baked atlases use the same (u, 1-v) UV convention as main textures.
            // Reflection atlases are constructed in sampling order, not source Texture2D order.
            for name in ["_OKLightmap", "_OKLightmapDirection"] {
                if let Some(png) = mesh.shader_extra_textures.remove(name) {
                    mesh.shader_extra_textures.insert(name.into(), flip_png_v(&png).unwrap_or(png));
                }
            }
            let material_mask = if role.starts_with("clothes") {
                mesh.alpha_mask_png.as_deref().map(|mask| (mask, 1.0, 0.0))
            } else { None };
            if source_program {
                // ChaControl assigns the equipped outer layer's mask to the
                // inner/bra renderer. Native shaders must receive that binding;
                // skipping CPU alpha baking must not drop the dynamic mask.
                if let Some((mask, v_scale, v_offset)) = alpha_mask {
                    apply_native_clothes_over_mask(&mut mesh, mask, v_scale, v_offset,
                        clothes_alpha_state(crate::unity::clothes_states()[0]));
                }
            } else if let (Some((mask, v_scale, v_offset)), Some(png)) = (alpha_mask.or(material_mask), mesh.texture_png.as_deref()) {
                let dynamic_alpha = role == "clothes_bra"
                    || property.as_deref().is_some_and(|p| p.ends_with("clothesTopParts0"));
                let flags = if dynamic_alpha {
                    clothes_alpha_state(crate::unity::clothes_states()[0])
                } else {
                    [true, true]
                };
                mesh.texture_png = apply_clothes_over_mask(png, mask, v_scale, v_offset, flags);
            } else if mesh.mat.as_ref().is_some_and(|mat| is_native_hair_shader(&mat.shader)) {
                if let (Some(mask), Some(png)) = (mesh.alpha_mask_png.as_deref(), mesh.texture_png.as_deref()) {
                    mesh.texture_png = apply_hair_alpha_mask(png, mask);
                }
            }
        (i, mesh, runtime_accessory)
    }
    /// The transformed meshes as parts, in order.
    fn emit(&self, done: Vec<PushDone>, parts: &mut Vec<ManifestPart>, session_parts: &mut Vec<crate::session::SessionPart>) -> Result<(), String> {
        let (role, property, slot, outfit, out_dir, me_color) = (self.role.as_str(), &self.property, self.slot, self.outfit, self.out_dir, self.me_color);
        let (guid, category_no, name) = (&self.guid, self.category_no, &self.name);
        for (i, mut mesh, runtime_accessory) in done {
        // Include the card property in the stem: two accessory slots can share
        // the same item id (slot 14/15 = the two Hachikuji headband halves), so
        // without it both write to the same OBJ and the first is overwritten.
        let stem = match property.as_deref() {
            Some(p) if !p.is_empty() => format!(
                "{}_{}_{}_{}_{}_sub{}",
                role,
                slot.unwrap_or(0),
                i,
                sanitize(&mesh.name),
                sanitize(p),
                mesh.submesh_index,
            ),
            _ => format!("{}_{}_{}_{}_sub{}", role, slot.unwrap_or(0), i, sanitize(&mesh.name), mesh.submesh_index),
        };
        if role.starts_with("clothes") { apply_clothes_default_main_texture(&mut mesh); }
        let (blend, unlit) = blend_for_mesh(role, &mesh);
        let (pth, tex) = emit_part(out_dir, session_parts, &stem, role, &mesh, &blend, unlit)?;
        if let Some(part)=session_parts.last_mut(){part.source_property=property.clone();}
        if let (Some(runtime), Some(part)) = (runtime_accessory, session_parts.last_mut()) {
            part.runtime_accessory = Some(runtime);
            part.rigid_follow_bone = None;
            part.rigid_follow_c = std::ffi::CString::new("").unwrap();
            // 4 = accessory skinned by its own persistent DynamicBone rig.
            part.skel_kind = 4;
        }
        if let Some(color) = me_shadow_color(me_color, outfit, role, property.as_deref(), &mesh) {
            if let Some(part) = session_parts.last_mut() {
                part.shadow_color = color;
            }
        }
        if let Some(color) = me_material_color(me_color, outfit, role, property.as_deref(), &mesh, "linecolor") {
            if let Some(part) = session_parts.last_mut() {
                part.line_color = color;
            }
        }
        parts.push(ManifestPart {
            #[cfg(test)]
            source_mesh: Some(mesh.name.clone()),
            role: role.into(),
            path: pth,
            guid: guid.clone(),
            mesh: mesh.name.clone(),
            slot,
            property: property.clone(),
            category_no,
            name: name.clone(),
            texture: tex,
            blend,
            unlit,
        });
            if let (Some(sampling), Some(part)) = (self.hair_gloss_sampling, session_parts.last_mut()) {
                if part.hair_gloss.is_some() { part.runtime_textures.insert("_HairGloss".into(), sampling); }
            }
        }
        Ok(())
    }
}

fn push_meshes<'a>(
    parts: &mut Vec<ManifestPart>,
    session_parts: &mut Vec<crate::session::SessionPart>,
    out_dir: Option<&'a Path>,
    game_root: &'a Path,
    role: &str,
    meshes: Vec<ExtractedMesh>,
    slot: Option<i32>,
    category_no: Option<i32>,
    guid: Option<String>,
    property: Option<String>,
    name: Option<String>,
    body_skel: Option<&crate::pose::Skeleton>,
    head_skel: Option<&crate::pose::Skeleton>,
    parent_key: Option<&str>,
    hair_color: Option<[[f32; 4]; 3]>,
    seen_meshes: &mut std::collections::HashSet<String>,
    part_skel: Option<&crate::pose::Skeleton>,
    part_local: Option<&crate::pose::Mat4>,
    me_off: Option<&std::collections::HashSet<(i32, i32, i32, String)>>,
    me_tex: Option<&'a crate::card::MeTexMap>,
    me_color: Option<&'a crate::card::MeColorMap>,
    me_shader: Option<&crate::card::MeShaderMap>,
    me_float: Option<&crate::card::MeFloatMap>,
    outfit: i32,
    alpha_mask: Option<(&[u8], f32, f32)>,
    accessory_assignment: Option<&crate::unity::AccessoryBodyBinding>,
    hair_gloss_sampling: Option<[i32; 5]>,
    batch: Option<&mut PushBatch<'a>>,
) -> Result<(), String> {let _spot=crate::profile::spot("push_meshes");
    check_preempted()?;
    let convert_normals = material_editor_converts_normals(game_root);
    let remove_rim = material_editor_removes_rim(game_root);
    let mut work = Vec::with_capacity(meshes.len());
    for (i, mut mesh) in meshes.into_iter().enumerate() {
        if is_dummy_part(&mesh.name, name.as_deref()) || is_hidden_part(&mesh.name) {
            continue;
        }
        if me_hides_mesh(me_off, outfit, role, property.as_deref(), &mesh) {
            eprintln!(
                "ok-extract: skip material-editor off role={role} name={} renderers={:?}",
                mesh.name, mesh.renderer_names
            );
            continue;
        }
        apply_me_shader(game_root, me_shader, outfit, role, property.as_deref(), &mut mesh);
        if let Some(overrides)=me_float { apply_me_floats(overrides,outfit,role,property.as_deref(),&mut mesh); }
        apply_material_editor_remove_rim(remove_rim, me_float, outfit, role, property.as_deref(), &mut mesh);
        let source_program=mesh.mat.as_ref().is_some_and(|m|matches!(m.shader.to_ascii_lowercase().as_str(),"xukmi/mainitemplus"|"shader forge/main_item"|"shader forge/main_alpha"|"shader forge/main_opaque"));
        let native_hair_color=mesh.mat.as_ref().is_some_and(|m|
            is_native_hair_shader(&m.shader)) || mesh.hair_shader_hint;
        // Head/face renderers are emitted from the head prefab. An accessory
        // or a second SMR with the same name (head mod + vanilla, or a
        // disabled colour variant that slipped through) must not be drawn again.
        // `cf_O_face` is only emitted by the face block below, with the composed
        // texture. A later clothes/head-mod pass used to emit the same name with
        // the bundle's raw texture and the editor drew that one.
        let bare = mesh.name.to_ascii_lowercase();
        if bare == "cf_o_face" {
            continue;
        }
        if is_head_detail(&mesh.name) && seen_meshes.contains(&bare) {
            continue;
        }
        // Key duplicates by mesh name AND the card property. Two accessory
        // slots can share the same mesh (e.g. the two Hachikuji headbands at
        // slots 14/15 are the left/right halves) and both must be emitted;
        // without the property the second slot is dropped and the accessory
        // renders as half.
        let material_key = mesh.mat.as_ref().map(|m| crate::card::me_mat_key(&m.name)).unwrap_or_default();
        let mesh_key = match property.as_deref() {
            Some(p) if !p.is_empty() => format!("{}|{p}|sub{}|{material_key}", mesh.name.to_ascii_lowercase(), mesh.submesh_index),
            _ => format!("{}|sub{}|{material_key}", mesh.name.to_ascii_lowercase(), mesh.submesh_index),
        };
        if !seen_meshes.insert(mesh_key) {
            continue;
        }
        work.push((i, mesh, source_program, native_hair_color));
    }
    // The call's inputs, the skeletons borrowed (immediate) or owned (kept by the batch).
    macro_rules! push_ctx {($body:expr, $head:expr) => {PushCtx { out_dir, game_root, role: role.to_string(), slot, category_no,
        guid: guid.clone(), property: property.clone(), name: name.clone(), body_skel: $body, head_skel: $head,
        parent_key: parent_key.map(str::to_string), hair_color, part_skel: part_skel.cloned(), part_local: part_local.copied(),
        me_tex, me_color, outfit, alpha_mask: alpha_mask.map(|(mask, scale, offset)| (mask.to_vec(), scale, offset)),
        accessory_assignment: accessory_assignment.cloned(), convert_normals,
        acc_place: ACC_PLACE.with(|c| c.borrow().clone()), clothes_active: crate::unity::active_clothes_states(),
        clothes_requested: crate::unity::explicit_requested_clothes_states(), overrides: crate::profile::snapshot(), hair_gloss_sampling }}}
    if first_load_option("A2") {
        if let Some(batch) = batch {
            use std::borrow::Cow;
            batch.groups.push((push_ctx!(body_skel.map(|s| Cow::Owned(s.clone())), head_skel.map(|s| Cow::Owned(s.clone()))), work));
            return Ok(());
        }
    }
    let ctx = push_ctx!(body_skel.map(std::borrow::Cow::Borrowed), head_skel.map(std::borrow::Cow::Borrowed));
    let done: Vec<PushDone> = work.into_iter().map(|item| ctx.transform(item)).collect();
    ctx.emit(done, parts, session_parts)
}

fn lookup_entry(
    vanilla: &ChaListCatalog,
    zip_lists: &mut std::collections::HashMap<String, ChaListCatalog>,
    zip_path: Option<&Path>,
    guid: &str,
    category_no: i32,
    slot: i32,
    local_slot: i32,
) -> Option<ListEntry> {
    if let Some(zp) = zip_path {
        let cat = zip_lists
            .entry(guid.to_string())
            .or_insert_with(|| catalog_from_zipmod(zp));
        let hit = cat.get_slot(category_no, slot, local_slot).cloned();
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: lookup guid={guid} cat={category_no} slot={slot} local={local_slot} zip_hit={:?}",
                hit.as_ref().map(|e| (&e.name, &e.main_data, &e.main_ab))
            );
        }
        if let Some(e) = hit {
            return Some(e);
        }
        // A KKEx resolve pins this slot to a mod. Do NOT silently fall back to
        // the vanilla catalog: that loads a same-named vanilla mesh instead of
        // the mod prefab and hides the mistake. Report and skip.
        eprintln!(
            "ok-extract: ERROR KKEx entry missing guid={guid} cat={category_no} slot={slot} local={local_slot} zip={}",
            zp.display()
        );
        return None;
    }
    vanilla.get_slot(category_no, slot, local_slot).cloned()
}

fn outfit_prefix(outfit: i32) -> String {
    format!("outfit{outfit}.")
}

fn simple_iris_png() -> Option<Vec<u8>> {
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

fn display_hair(c: [f32; 4]) -> [f32; 4] {
    c
}

// ChaControl.ChangeSettingHairOutlineColor / ChangeSettingHairGlossColor.
fn set_hair_part_materials(meshes:&mut [ExtractedMesh], hair:&crate::card::HairPart) {
    for mesh in meshes {
        set_material_color(mesh,"_LineColor",hair.outline_color);
        if mesh.mat.as_ref().is_some_and(|m|is_native_hair_shader(&m.shader.to_ascii_lowercase())) || mesh.hair_shader_hint {
            set_material_color(mesh,"_GlossColor",hair.gloss_color);
        }
    }
}

fn hair_cols_for(card: &CardInfo, cat: i32) -> [[f32; 4]; 3] {
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
fn hair_tint_for(card: &CardInfo, cat: i32) -> [f32; 4] {
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
fn accessory_bundle_bytes(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<crate::unity::SourceBytes> {let _spot=crate::profile::spot("accessory_bundle_bytes");
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

fn accessory_assignment(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<crate::unity::AccessoryBodyBinding> {
    if crate::appearance::timings::measure("assignment.plugins",||accessory_clothes_plugins(game_root).is_empty()) {return None;}
    let shared=crate::appearance::timings::measure("assignment.source",||accessory_bundle_shared(game_root,zip,entry))?;
    let parsed=crate::unity::parse_bundle_ref(&shared.0);parsed.configure_game_references_from_zip(game_root,shared.1.as_deref());
    parsed.accessory_body_binding(&entry.main_data)
}
fn accessory_bundle_source(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<(crate::unity::SourceBytes,Option<PathBuf>)> {
    accessory_bundle_shared(game_root,zip,entry).map(|shared|(shared.0.clone(),shared.1.clone()))
}
fn accessory_bundle_shared(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<std::sync::Arc<(crate::unity::SourceBytes,Option<PathBuf>)>> {
    let key=(zip.map(Path::to_path_buf),entry.main_ab.clone(),entry.main_data.clone());
    if let Some(hit)=ACCESSORY_SOURCE_MEMO.with(|m|m.borrow().iter().find(|(k,_)|*k==key).map(|(_,v)|v.clone())) {return hit;}
    let found=accessory_bundle_source_uncached(game_root,zip,entry).map(std::sync::Arc::new);
    ACCESSORY_SOURCE_MEMO.with(|m|{let mut m=m.borrow_mut();if m.len()>=4{m.remove(0);}m.push((key,found.clone()));});
    found
}
fn accessory_bundle_source_uncached(game_root:&Path,zip:Option<&Path>,entry:&ListEntry)->Option<(crate::unity::SourceBytes,Option<PathBuf>)> {
    let contains = |bytes: &crate::unity::SourceBytes| entry.main_data.is_empty()
        || crate::unity::parse_bundle_ref(bytes).contains_prefab(&entry.main_data);
    zip.and_then(|z|crate::mods::zipmod_one_unity3d_source(z,&entry.main_ab)
            .map(|(_,b)|b).filter(&contains).map(|b|(b,Some(z.to_owned()))))
        .or_else(||crate::mods::override_bundle_with_source(&game_root.join("mods"),&entry.main_ab)
            .map(|(z,b)|(crate::unity::SourceBytes::from(b),z)).filter(|(b,_)|contains(b)).map(|(b,z)|(b,Some(z))))
        .or_else(||crate::gamefs::read_source(game_root.join("abdata").join(&entry.main_ab)).ok().map(|b|(b,None)))
}

/// Authored material colour (`_Color`/`_Color2`/`_Color3`/`_Color4`).
fn mat_color(mesh: &ExtractedMesh, key: &str) -> [f32; 4] {
    let is_hair = mesh.hair_shader_hint || mesh.mat.as_ref().is_some_and(|m|
        is_native_hair_shader(&m.shader));
    let default = match key {
        "_Color2" if is_hair => [0.7843137, 0.7843137, 0.7843137, 1.0],
        "_Color3" if is_hair => [0.5, 0.5, 0.5, 1.0],
        _ => [1.0; 4],
    };
    mesh.mat.as_ref().and_then(|mm| mm.color(key)).unwrap_or(default)
}

fn ids_hit(ids: &[i64], set: &std::collections::HashSet<i64>) -> bool {
    !ids.is_empty() && ids.iter().any(|id| set.contains(id))
}

fn shift_length_bones(sk: &mut crate::pose::Skeleton, bones: &[(String, Option<f32>)], delta: f32) {
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
fn apply_hair_accessory_length(
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

fn me_shader_color(
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

const CLOTHES_PROP: [&str; 9] = [
    "ClothesTop", "ClothesBot", "ClothesBra", "ClothesShorts",
    "ClothesGloves", "ClothesPants", "ClothesSocks",
    "ClothesShoesInner", "ClothesShoesOuter",
];

type SelectedClothes = Option<(ListEntry, String, Option<PathBuf>)>;
type SelectedTopSubpart = Option<(i32, ListEntry, String, Option<PathBuf>)>;

fn select_card_clothes(
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

fn select_card_top_subparts(
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
fn me_shader_registration(game_root: &Path, shader: &str) -> (bool, Option<bool>) {
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
fn me_shader_registration_in_tree(game_root: &Path, shader: &str,
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
fn me_manifest_sections(game_root: &Path) -> std::sync::Arc<Vec<(PathBuf, String)>> {
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

fn me_shader_registered(game_root: &Path, shader: &str) -> bool {
    me_shader_registration(game_root, shader).0
}

fn apply_me_shader(
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

fn apply_me_floats(
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
fn apply_native_skin_draw_settings(mesh: &mut ExtractedMesh, card: &CardInfo, outfit: i32, body: bool) {
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
fn apply_me_material_copies(
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
fn color_accessory_meshes(
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
fn scale_studio_item_alpha(m: &mut ExtractedMesh, shader: &str) {
    if !shader.contains("main_item_studio_alpha") { return; }
    let alpha = m.mat.as_ref().and_then(|mat| mat.float("_alpha")).unwrap_or(1.0);
    if alpha < 1.0 {
        if let Some(png) = m.texture_png.take() {
            m.texture_png = crate::unity::scale_png_alpha(&png, alpha).or(Some(png));
        }
    }
}

fn bake_accessory_glass_color(png: Option<&[u8]>, color: [f32; 4]) -> Option<Vec<u8>> {
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
fn accessory_colors(card: &CardInfo, property: &str) -> Option<[[f32; 4]; 4]> {
    let slot = accessory_slot_from_property(property)?;
    let a = card.accessory.get(slot)?;
    Some([a.color, a.color2, a.color3, a.color4])
}

#[allow(dead_code)]
fn acc_tint_for(color: [f32; 4]) -> Option<[f32; 4]> {
    if (color[0] - 1.0).abs() < 0.02
        && (color[1] - 1.0).abs() < 0.02
        && (color[2] - 1.0).abs() < 0.02
    {
        None
    } else {
        Some(color)
    }
}

/// Illusion clothes: MainTex * (Color1*mask.r + Color2*mask.g + Color3*mask.b).
/// A clothes colour channel's pattern (StudioEditor `KoikatsuClothesBake`).
struct ClothesPatternSrc {
    png: Option<image::RgbaImage>,
    gpu: Option<clothes_gpu::Texture>,
    color: [f32; 4],
    tiling: [f32; 2],
    offset: [f32; 2],
    rotate: f32,
    sampling: crate::unity::TextureSampling,
}

fn named_texture_sampling(game:&Path,entry:&ListEntry,ab:&str,names:&[&str])->Option<crate::unity::TextureSampling> {
    let zip=entry.extra.get("__zip").map(Path::new);
    for name in names {
        if sideloader_png(entry.extra.get("__zip").map(String::as_str),game,ab,&[name]).is_some(){
            // Sideloader.LoadTextureFromPNG chooses wrap from the whole PNG path.
            let path=format!("{ab}/{name}");let wrap=if path.contains("clamp"){1}else{0};
            return Some(crate::unity::TextureSampling{wrap_u:wrap,wrap_v:wrap,..Default::default()});
        }
    }
    let read=|parsed:std::sync::Arc<crate::unity::ParsedBundle>| {
        for name in names {
            if !character_hi_poly() {
                if let Some(sampling)=parsed.texture_sampling_named(&format!("{name}_low")){return Some(sampling);}
            }
            if let Some(sampling)=parsed.texture_sampling_named(name){return Some(sampling);}
        }
        None
    };
    zip.and_then(|z|crate::unity::parse_bundle_zip_cached(z,ab)).and_then(read)
        .or_else(||crate::mods::override_bundle_source(&game.join("mods"),ab)
            .and_then(|p|crate::unity::parse_bundle_zip_cached(&p,ab)).and_then(read))
        .or_else(||crate::unity::parse_bundle_path_cached(&game.join("abdata").join(ab)).and_then(read))
}

fn named_texture_gpu(game:&Path,entry:&ListEntry,ab:&str,name:&str)->Option<clothes_gpu::Texture> {
    if sideloader_png(entry.extra.get("__zip").map(String::as_str),game,ab,&[name]).is_some(){return None;}
    let read=|bytes:crate::unity::SourceBytes| {
        let parsed=crate::unity::parse_bundle_ref(&bytes);
        if !character_hi_poly() {if let Some(tex)=parsed.texture_gpu_named(&format!("{name}_low")){return Some(tex);}}
        parsed.texture_gpu_named(name)
    };
    entry.extra.get("__zip").and_then(|z|crate::mods::zipmod_bundle_named(Path::new(z),ab)).and_then(|b|read(b.into()))
        .or_else(||crate::mods::override_bundle_with_source(&game.join("mods"),ab).and_then(|(_,b)|read(b.into())))
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(ab)).ok().and_then(read))
}

fn clothes_gpu_bake(game:&Path,entry:&ListEntry,main_key:&str,mask_key:&str,colors:[[f32;4];4],patterns:&[ClothesPatternSrc])->Result<Vec<u8>,String> {
    let quality=clothes_gpu::quality(game).ok_or("installed quality profile unresolved")?;
    clothes_gpu_bake_with_quality(game,entry,main_key,mask_key,colors,patterns,quality)
}

fn clothes_gpu_bake_with_quality(game:&Path,entry:&ListEntry,main_key:&str,mask_key:&str,colors:[[f32;4];4],patterns:&[ClothesPatternSrc],quality:u32)->Result<Vec<u8>,String> {
    let texture=|key:&str|named_texture_gpu(game,entry,&clothes_tex_ab(entry,clothes_ab_key_for(key)),&entry.info(key)).ok_or_else(||format!("authored GPU texture unresolved: {key}"));
    let t0=Instant::now();let main=texture(main_key)?;let mask=texture(mask_key)?;profile_tick("gpu bake: textures",t0);let t0=Instant::now();
    let pat=|i:usize|match patterns.get(i) {Some(p) if p.png.is_some()=>p.gpu.clone().ok_or_else(||format!("authored GPU pattern unresolved: {i}")),_=>Ok(clothes_gpu::Texture::white())};
    let textures=[pat(0)?,mask,pat(1)?,pat(2)?,main];
    let ab="chara/mm_base.unity3d";
    let read=|bytes:crate::unity::SourceBytes|crate::unity::parse_bundle_ref(&bytes).shader_pixel_dxbc_named("Koikano/create_topN");
    let shader=crate::mods::override_bundle_with_source(&game.join("mods"),ab).and_then(|(_,b)|read(b.into()))
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(ab)).ok().and_then(read)).ok_or("native clothes create PS unresolved")?;profile_tick("gpu bake: shader",t0);let t0=Instant::now();
    let mut cb=[[0.0;4];16];cb[2]=[1.0,1.0,0.0,0.0];cb[7]=cb[2];
    for (i,start) in [3,8,12].into_iter().enumerate() {
        let p=patterns.get(i);let tiling=p.map(|p|p.tiling).unwrap_or([0.0;2]);let offset=p.map(|p|p.offset).unwrap_or([0.5;2]);
        cb[start]=[tiling[0],tiling[1],offset[0],offset[1]];cb[start+1][0]=p.map(|p|p.rotate).unwrap_or(0.5);
        cb[start+2]=p.map(|p|p.color).unwrap_or([1.0;4]);cb[start+3]=colors[i];
        for k in 0..3 {cb[start+2][k]=srgb_channel_to_linear(cb[start+2][k]);cb[start+3][k]=srgb_channel_to_linear(cb[start+3][k]);}
    }
    if !crate::okstrict::assembling() { return crate::unity::solid_png([1.0;4],1,1).ok_or_else(||"solid png".into()); }
    let r=clothes_gpu::bake(&shader,&textures,&cb,quality);profile_tick("gpu bake: bake",t0);r
}

fn clothes_mask_sampling(game:&Path,entry:&ListEntry,key:&str)->crate::unity::TextureSampling {
    let ab=clothes_tex_ab(entry,clothes_ab_key_for(key));
    named_texture_sampling(game,entry,&ab,&[&entry.info(key)]).unwrap_or_default()
}

fn sample_clothes_mask(image:&image::RgbaImage,u:f32,v:f32,sampling:crate::unity::TextureSampling)->[f32;4] {
    let (w,h)=image.dimensions();
    let address=|i:i32,n:u32,mode:i32|->u32 {
        let n=n as i32;
        match mode {
            0=>i.rem_euclid(n) as u32,
            2=>{let p=i.rem_euclid(2*n);(if p<n{p}else{2*n-1-p}) as u32},
            3=>{let p=if i<0{-1-i}else{i};p.min(n-1) as u32},
            _=>i.clamp(0,n-1) as u32,
        }
    };
    let pixel=|x:i32,y:i32|image.get_pixel(address(x,w,sampling.wrap_u),address(y,h,sampling.wrap_v)).0;
    let x=u*w as f32-0.5;let y=v*h as f32-0.5;
    let channel=|p:[u8;4],i:usize|{let value=p[i] as f32/255.0;if sampling.srgb && i<3 {srgb_channel_to_linear(value)}else{value}};
    if sampling.filter==0 {let p=pixel((x+0.5).floor() as i32,(y+0.5).floor() as i32);return std::array::from_fn(|i|channel(p,i));}
    let x0=x.floor() as i32;let y0=y.floor() as i32;let fx=x-x0 as f32;let fy=y-y0 as f32;
    let a=pixel(x0,y0);let b=pixel(x0+1,y0);let c=pixel(x0,y0+1);let d=pixel(x0+1,y0+1);
    std::array::from_fn(|i|lerp_f(lerp_f(channel(a,i),channel(b,i),fx),lerp_f(channel(c,i),channel(d,i),fx),fy))
}

/// `sample_clothes_mask` at the pixel centres of a `w`×`h` target, set up once: each column's and row's texels and
/// weights, and each byte's channel value. The arithmetic and its order are those of sampling pixel by pixel, so the
/// bytes are the same (a card's accessory bakes spent most of a conversion's CPU in the per-pixel set-up).
struct GridSampler<'a> {
    image: &'a image::RgbaImage,
    cols: Vec<(u32, u32, f32)>,
    rows: Vec<(u32, u32, f32)>,
    nearest: bool,
    /// [alpha, colour] channel values per byte.
    lut: [[f32; 256]; 2],
}

impl<'a> GridSampler<'a> {
    fn new(image: &'a image::RgbaImage, w: u32, h: u32, sampling: crate::unity::TextureSampling) -> Self {
        let (iw, ih) = image.dimensions();
        let address = |i: i32, n: u32, mode: i32| -> u32 {
            let n = n as i32;
            match mode {
                0 => i.rem_euclid(n) as u32,
                2 => { let p = i.rem_euclid(2 * n); (if p < n { p } else { 2 * n - 1 - p }) as u32 }
                3 => { let p = if i < 0 { -1 - i } else { i }; p.min(n - 1) as u32 }
                _ => i.clamp(0, n - 1) as u32,
            }
        };
        let nearest = sampling.filter == 0;
        let axis = |n: u32, size: u32, mode: i32| -> Vec<(u32, u32, f32)> {
            (0..n).map(|i| {
                let x = (i as f32 + 0.5) / n as f32 * size as f32 - 0.5;
                if nearest { let p = address((x + 0.5).floor() as i32, size, mode); (p, p, 0.0) }
                else { let x0 = x.floor() as i32; (address(x0, size, mode), address(x0 + 1, size, mode), x - x0 as f32) }
            }).collect()
        };
        let lut = [std::array::from_fn(|v| v as f32 / 255.0), std::array::from_fn(|v| {
            let value = v as f32 / 255.0;
            if sampling.srgb { srgb_channel_to_linear(value) } else { value }
        })];
        GridSampler { image, cols: axis(w, iw, sampling.wrap_u), rows: axis(h, ih, sampling.wrap_v), nearest, lut }
    }

    fn sample(&self, x: u32, y: u32) -> [f32; 4] {
        let (x0, x1, fx) = self.cols[x as usize];
        let (y0, y1, fy) = self.rows[y as usize];
        let channel = |p: [u8; 4], i: usize| self.lut[(i < 3) as usize][p[i] as usize];
        let a = self.image.get_pixel(x0, y0).0;
        if self.nearest { return std::array::from_fn(|i| channel(a, i)); }
        let b = self.image.get_pixel(x1, y0).0;
        let c = self.image.get_pixel(x0, y1).0;
        let d = self.image.get_pixel(x1, y1).0;
        std::array::from_fn(|i| lerp_f(lerp_f(channel(a, i), channel(b, i), fx), lerp_f(channel(c, i), channel(d, i), fx), fy))
    }
}

/// Colour-mask bakes without patterns already made in this process, by their inputs: the outfits of one card bake
/// the same accessories again. Dropped with the bundle caches (between the cards of a batch) or past 512 MiB.
static MASK_BAKES: std::sync::Mutex<(usize, Vec<([u8; 16], Option<std::sync::Arc<Vec<u8>>>)>)> = std::sync::Mutex::new((0, Vec::new()));

pub(crate) fn clear_mask_bakes() { *MASK_BAKES.lock().unwrap() = (0, Vec::new()); }

fn apply_clothes_color_mask(
    main: &[u8],
    mask: &[u8],
    colors: [[f32; 4]; 4],
    patterns: &[ClothesPatternSrc],
) -> Option<Vec<u8>> {
    // Direct accessory shader baking retains its separate colour convention.
    let sampling=crate::unity::TextureSampling{srgb:false,..Default::default()};
    if !patterns.is_empty() { return apply_clothes_color_mask_sampled(main,mask,colors,patterns,sampling,None); }
    let key = {
        use sha2::Digest;
        let mut h = sha2::Sha256::new();
        for part in [main, mask] { h.update((part.len() as u64).to_le_bytes()); h.update(part); }
        for c in colors.iter().flatten() { h.update(c.to_bits().to_le_bytes()); }
        h.update(std::env::var("OK_CLOTHES_MASK").unwrap_or_default().as_bytes());
        let mut id = [0u8; 16];
        id.copy_from_slice(&h.finalize()[..16]);
        id
    };
    if let Some((_, made)) = MASK_BAKES.lock().unwrap().1.iter().find(|(k, _)| *k == key) { return made.as_ref().map(|m| m.to_vec()); }
    let made = apply_clothes_color_mask_sampled(main,mask,colors,patterns,sampling,None);
    let mut bakes = MASK_BAKES.lock().unwrap();
    if bakes.0 > 512 << 20 { *bakes = (0, Vec::new()); }
    bakes.0 += made.as_ref().map_or(0, Vec::len);
    bakes.1.push((key, made.clone().map(std::sync::Arc::new)));
    made
}

fn apply_clothes_color_mask_sampled(
    main:&[u8],mask:&[u8],colors:[[f32;4];4],patterns:&[ClothesPatternSrc],sampling:crate::unity::TextureSampling,main_srgb:Option<bool>,
) -> Option<Vec<u8>> {
    // Koikano/create_topN PS DXBC 0001: three transformed pattern samples,
    // alpha-dependent pattern colour arithmetic, then cascading RGB masks.
    if !crate::okstrict::assembling() { return Some(main.to_vec()); }
    let mask_mode = std::env::var("OK_CLOTHES_MASK").unwrap_or_default();
    let src = crate::teximg::load_from_memory(main).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask).ok()?.to_rgba8();
    let (w, h) = src.dimensions();
    let (mw, mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 {
        return None;
    }
    // Precompute each channel's base colour and pattern sampler.
    let mut base = [[1.0f32; 3]; 3];
    let mut has_pat = [false; 3];
    let mut pat_col = [[1.0f32; 3]; 3];
    let mut pat_scale = [[1.0f32; 2]; 3];
    for c in 0..3 {
        base[c] = [colors[c][0], colors[c][1], colors[c][2]];
        if main_srgb.is_some(){base[c]=base[c].map(srgb_channel_to_linear);}
        if let Some(p) = patterns.get(c) {
            pat_col[c] = [p.color[0], p.color[1], p.color[2]];
            if main_srgb.is_some(){pat_col[c]=pat_col[c].map(srgb_channel_to_linear);}
            let sx = 20.0 - 19.0 * p.tiling[0];
            let sy = 20.0 - 19.0 * p.tiling[1];
            pat_scale[c] = [sx, sy];
            has_pat[c] = p.png.is_some();
        }
    }
    let mut out = src;
    let grid = GridSampler::new(&mask, w, h, sampling);
    let rotations: Vec<(f32, f32)> = patterns.iter().map(|p| ((p.rotate - 0.5) * std::f32::consts::TAU).sin_cos()).collect();
    // Each pixel depends only on its own inputs: rows are baked in parallel (the same bytes as one by one; a heavy
    // accessory's bakes took seconds of an open on one thread).
    use rayon::prelude::*;
    let pixels: &mut [u8] = &mut out;
    pixels.par_chunks_mut(w as usize * 4).enumerate().for_each(|(y, row)| {
        let y = y as u32;
        for x in 0..w {
            let m = grid.sample(x, y);
            let wr = m[0].clamp(0.0, 1.0);
            let wg = m[1].clamp(0.0, 1.0);
            let wb = m[2].clamp(0.0, 1.0);
            let mut ch = [[0f32; 3]; 3];
            for c in 0..3 {
                ch[c] = base[c];
                if has_pat[c] {
                    let pattern = &patterns[c];
                    if let Some(p) = pattern.png.as_ref() {
                        let u=(x as f32+0.5)/w as f32;
                        let v=(y as f32+0.5)/h as f32;
                        let qx=(u+2.0*pattern.offset[0]-1.0)*pat_scale[c][0]-0.5;
                        let qy=(v+2.0*pattern.offset[1]-1.0)*pat_scale[c][1]-0.5;
                        let (sn,cs)=rotations[c];
                        let ps=sample_clothes_mask(p,cs*qx+sn*qy+0.5,
                            -sn*qx+cs*qy+0.5,pattern.sampling);
                        let alpha=pattern.color[3];
                        let avg=((ps[0]+ps[1]+ps[2])/3.0).max(1.0-alpha);
                        let low=if ps[3]>=0.0 && ps[3]<=0.25 {1.0_f32}else{0.0};
                        let mix=(ps[3]*3.0-2.0).clamp(0.0,1.0).max(low).max(1.0-alpha);
                        let original=low.min(alpha);
                        for k in 0..3 {
                            let target=pat_col[c][k]+avg*(base[c][k]-pat_col[c][k])-ps[k]*pat_col[c][k];
                            let tinted=ps[k]*pat_col[c][k]+mix*target;
                            ch[c][k]=tinted+original*(ps[k]-tinted);
                        }
                    }
                }
            }
            let mut tint = [1.0f32; 3];
            if mask_mode == "norm" {
                let weight = wr + wg + wb;
                let inv = 1.0 / weight.max(0.0001);
                let t = weight.clamp(0.0, 1.0);
                for k in 0..3 {
                    let mixed = (ch[0][k] * wr + ch[1][k] * wg + ch[2][k] * wb) * inv;
                    tint[k] = 1.0 * (1.0 - t) + mixed * t;
                }
            } else if mask_mode == "add" {
                for k in 0..3 {
                    tint[k] = (ch[0][k] * wr + ch[1][k] * wg + ch[2][k] * wb)
                        .clamp(0.0, 1.0);
                }
            } else {
                for k in 0..3 {
                    let mut t = 1.0f32;
                    t += (ch[0][k] - t) * wr;
                    t += (ch[1][k] - t) * wg;
                    t += (ch[2][k] - t) * wb;
                    tint[k] = t;
                }
            }
            let p = &mut row[x as usize * 4..x as usize * 4 + 4];
            for k in 0..3 {
                let mut source=p[k] as f32/255.0;
                if main_srgb==Some(true){source=srgb_channel_to_linear(source);}
                let value=source*tint[k];
                p[k]=to_unorm8(if main_srgb.is_some(){linear_channel_to_srgb(value)}else{value});
            }
        }
    });
    encode_png(&out)
}

/// Match the vertical orientation of a Unity Graphics.Blit output. This is
/// shared by the clothes colour bake and MaterialEditor's normal conversion.
fn flip_runtime_blit_v(png: Vec<u8>) -> Vec<u8> {
    crate::teximg::load_from_memory(&png).ok()
        .and_then(|img| {
            let mut im = img.to_rgba8();
            image::imageops::flip_vertical_in_place(&mut im);
            encode_png(&im)
        })
        .unwrap_or(png)
}

/// MaterialEditor's embedded normal_convert shader, Windows PS 4.0:
/// sample _MainTex, write RGB/A as (1, sample.g, sample.b, sample.a).
/// Graphics.Blit also reverses the PNG row direction used by our decoder.
fn convert_runtime_normal_map(png: Vec<u8>) -> Vec<u8> {
    convert_runtime_normal_map_sampled(png,false)
}

fn convert_runtime_normal_map_sampled(png: Vec<u8>,source_srgb:bool) -> Vec<u8> {
    convert_runtime_normal_map_rows(png,source_srgb,true)
}

fn convert_runtime_normal_map_rows(png: Vec<u8>,source_srgb:bool,flip_rows:bool) -> Vec<u8> {let _spot=crate::profile::spot("convert_runtime_normal_map_rows");
    // Same source and switches, same output: outfit switches and shared base normals reuse it (~0.1 s per map).
    png_transform_cache::transform("runtime_normal_v1",&png,&[source_srgb as u8,flip_rows as u8],||Some(convert_runtime_normal_map_rows_uncached(png.clone(),source_srgb,flip_rows))).unwrap_or(png)
}
fn convert_runtime_normal_map_rows_uncached(png: Vec<u8>,source_srgb:bool,flip_rows:bool) -> Vec<u8> {
    crate::teximg::load_from_memory(&png).ok()
        .and_then(|img| {
            let mut im = img.to_rgba8();
            if flip_rows {image::imageops::flip_vertical_in_place(&mut im);}
            for pixel in im.pixels_mut() {
                pixel[0] = 255;
                if source_srgb {
                    for c in 1..3 {pixel[c]=srgb_to_linear_u8(pixel[c]);}
                }
            }
            encode_png(&im)
        })
        .unwrap_or(png)
}

/// MaterialEditorPlugin.ConvertNormalMap allocates a default sRGB RT with
/// automatic mips and preserves the replaced texture's wrapMode. Native shader
/// programs sample that view; the legacy linear-normal adapter is separate.
fn mark_native_runtime_normal_texture(mesh:&mut ExtractedMesh,property:&str) {
    let wrap=mesh.runtime_textures.get(property).map(|s|s[1])
        .or_else(||mesh.mat.as_ref().and_then(|m|m.tex_wraps.get(property).copied())).unwrap_or(0);
    mesh.runtime_textures.insert(property.into(),[1,wrap,wrap,1,1]);
}

fn material_editor_converts_normals(game_root: &Path) -> bool {
    let path = game_root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    crate::gamefs::read_to_string(path).ok().map(|cfg| {
        cfg.lines().find_map(|line| line.trim().strip_prefix("Convert Normal Maps ="))
            .map(|value| value.trim().eq_ignore_ascii_case("true"))
            .unwrap_or(true)
    // The project-owned standalone source intentionally has no installed
    // BepInEx config. Its supported MaterialEditor semantics use the decompiled
    // Config.Bind default (true), rather than treating absence as explicit false.
    // Keep unmarked installed sources and explicit config values compatible.
    }).unwrap_or_else(|| game_root.join("require_complete_mods").ok_is_file())
}

/// MaterialEditorCharaController.RemoveRim runs after card float overrides.
/// It changes only clothing, hair and accessories with a `_rimV` shader
/// property and only when that exact material/slot has no saved rim override.
fn apply_material_editor_remove_rim(
    remove_rim: bool,
    overrides: Option<&crate::card::MeFloatMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &mut ExtractedMesh,
) {
    if !remove_rim { return; }
    let (Some(ot @ (1 | 2 | 3)), Some(slot), Some(mat)) =
        (me_object_type(role), me_slot(role, property), mesh.mat.as_mut()) else { return; };
    if !material_has_rim_property(mat) { return; }
    let name = crate::card::me_mat_key(&mat.name);
    let has_card_value = overrides.is_some_and(|map| map.keys().any(|(t, coord, s, material, prop)|
        *t == ot && *coord == outfit && *s == slot && *material == name
            && prop.trim_start_matches('_').eq_ignore_ascii_case("rimv")));
    if !has_card_value { mat.floats.insert("_rimV".into(), 0.0); }
}

fn material_editor_removes_rim(game_root: &Path) -> bool {
    if !game_root.join("BepInEx/plugins/KKS_Plugins/KKS_MaterialEditor.dll").ok_is_file()
        && !game_root.join("BepInEx/plugins/KKS_MaterialEditor.dll").ok_is_file() { return false; }
    let path = game_root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    crate::gamefs::read_to_string(path).ok().and_then(|cfg| {
        cfg.lines().find_map(|line| line.trim().strip_prefix("Remove Rim Lighting =")
            .map(|value| value.trim().eq_ignore_ascii_case("true")))
    }).unwrap_or(false)
}

/// Prefer the referenced Unity Shader's serialized property declaration,
/// which is the source of Material.HasProperty even for unsaved default values.
/// Some replacement shaders are external to the material bundle; for those,
/// use the installed MaterialEditor shader declarations or a serialized float.
fn material_has_rim_property(mat: &crate::unity::MeshMaterial) -> bool {
    if let Some(has_rim) = mat.shader_has_rim { return has_rim; }
    if mat.float("_rimV").is_some() { return true; }
    matches!(mat.shader.to_ascii_lowercase().as_str(),
        "koikano/main_clothes_item_glasses"
        | "koikano/hair_main_sun"
        | "koikano/hair_main_sun_front"
        | "shader forge/main_item_emission"
        | "shader forge/main_item_studio_alpha"
        | "shader forge/main_alpha"
        | "shader forge/main_hair"
        | "shader forge/main_hair_front"
        | "shader forge/main_item"
        | "shader forge/main_item_studio"
        | "shader forge/main_opaque"
        | "shader forge/main_opaque2"
        | "shader forge/main_skin")
}

/// The game's clothes albedo is a runtime RenderTexture produced by
/// `cf_m_clothesN_create`; Unity's blit writes it vertically flipped relative
/// to the source `MainTex`, and the renderer samples the RT with the mesh UV.
/// Our CPU bake has the source orientation, so flip V to match the game.
fn finalize_clothes_texture(meshes: &mut [ExtractedMesh]) {
    if std::env::var("OK_CLOTH_NOFLIP").is_ok() {
        return;
    }
    // Each mesh's own texture (decode, flip, encode): all at once.
    use rayon::prelude::*;
    crate::session::native_parallel_work(|| meshes.par_iter_mut().for_each(|m| {
        if let Some(png) = m.texture_png.take() {
            m.texture_png = Some(flip_runtime_blit_v(png));
        }
    }));
}

/// Load the three colour channels' pattern textures (cat 430) for a clothes part.
fn pattern_resolve_matches(r:&crate::card::ResolveInfo,outfit:i32,part:usize,channel:usize,id:i32)->bool {
    let Some(kind)=["Top","Bot","Bra","Shorts","Gloves","Pants","Socks","ShoesInner","ShoesOuter"].get(part) else{return false};
    r.category_no==430 && (r.slot==id||r.local_slot==id)
        && r.property==format!("outfit{outfit}.ChaFileClothes.Clothes{kind}Pattern{channel}")
}

fn build_clothes_patterns(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String,PathBuf>,
    part: usize,
    ch0: usize,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
    outfit: i32,
) -> Vec<ClothesPatternSrc> {
    // `ch0` is the `ColorInfo` index feeding shader channel 1 (`_Color`).
    // `ChaControl.ChangeCustomClothes`: main clothes and sub-kinds A/B use
    // `colorInfo[0]`; sub-kind C (`!main && 2 == kind`) uses `colorInfo[3]`.
    let pats = card.clothes_patterns.get(part);
    let idx = [ch0, 1, 2];
    let mut out = Vec::with_capacity(3);
    for c in 0..3 {
        let (id, color, tiling, offset, rotate) = pats
            .map(|p| {
                let e = &p[idx[c]];
                (e.pattern, e.pattern_color, e.tiling, e.offset, e.rotate)
            })
            .unwrap_or((0, [1.0, 1.0, 1.0, 1.0], [0.0, 0.0], [0.5,0.5], 0.5));
        let mut pattern_sampling=crate::unity::TextureSampling::default();
        let mut gpu=None;
        let png = if id != 0 {
            let mut zip_lists=std::collections::HashMap::new();
            let entry=if let Some(resolve)=card.resolves.iter().find(|r|
                pattern_resolve_matches(r,outfit,part,idx[c],id)) {
                lookup_entry(vanilla,&mut zip_lists,guid_path(guid_index,&resolve.mod_id).map(|p|p.as_path()),
                    &resolve.mod_id,430,resolve.slot,resolve.local_slot)
            }else{vanilla.get(430,id).or_else(||mod_catalog.get(430,id)).cloned()};
            entry.and_then(|entry| {
                // ChangeCustomClothes uses MainTexAB, independently of MainAB.
                let ab=entry.info("MainTexAB");let name=entry.info("MainTex");
                if ab.is_empty() || ab=="0" || name.is_empty() || name=="0" {return None;}
                pattern_sampling=named_texture_sampling(game_root,&entry,&ab,&[&name]).unwrap_or_default();
                gpu=named_texture_gpu(game_root,&entry,&ab,&name);
                let name=if character_hi_poly(){name}else{format!("{name}_low")};
                png_from_bundle(game_root,&ab,entry.extra.get("__zip").map(|p|p.as_str()),
                    &[name.as_str()],head_texs,body_texs)
            })
            .and_then(|b| crate::teximg::load_from_memory(&b).ok())
            .map(|i| i.to_rgba8())
        } else {
            None
        };
        if std::env::var("OK_PATDIAG").is_ok() && id != 0 {
            eprintln!(
                "ok-extract: clothes pattern part={part} ch={c} id={id} tiling={tiling:?} loaded={}",
                png.is_some()
            );
        }
        out.push(ClothesPatternSrc { png, gpu, color, tiling, offset, rotate, sampling:pattern_sampling });
    }
    out
}

/// Studio item patterns (OIItemInfo patterns: the character list's mt_pattern, category 430, as clothes patterns):
/// (id, name) of every pattern, from `root` (a game root, or the OK base library replaying one).
pub fn studio_patterns(root: &Path) -> Vec<(i32, String)> {
    let vanilla = load_characustom_dir(&root.join("abdata/list/characustom"));
    let mut out: Vec<(i32, String)> = vanilla.entries().filter(|e| e.category_no == 430 && e.id > 0).map(|e| (e.id, e.name.clone())).collect();
    out.sort();
    out
}

/// A Studio item pattern's mask texture as PNG. Studio takes the list's thumbnail without its "thumb/" folder and
/// "thumb_" prefix (OCIItem.SetPatternTex); MainTexAB / MainTex (clothes patterns) when that gives nothing.
pub fn studio_pattern_png(root: &Path, id: i32) -> Option<Vec<u8>> {
    let vanilla = load_characustom_dir(&root.join("abdata/list/characustom"));
    let entry = vanilla.get(430, id)?.clone();
    let zip = entry.extra.get("__zip").map(|p| p.as_str());
    let usable = |s: &str| !s.is_empty() && s != "0";
    let (thumb_ab, thumb) = (entry.info("ThumbAB").replace("thumb/", ""), entry.info("ThumbTex").replace("thumb_", ""));
    if usable(&thumb_ab) && usable(&thumb) {
        if let Some(png) = png_from_bundle(root, &thumb_ab, zip, &[thumb.as_str()], &[], &[]) { return Some(png); }
    }
    let (ab, name) = (entry.info("MainTexAB"), entry.info("MainTex"));
    if !usable(&ab) || !usable(&name) { return None; }
    png_from_bundle(root, &ab, zip, &[name.as_str()], &[], &[])
}

/// Load the `OverBodyMask` of a clothes list entry (its own `OverBodyMaskAB`
/// bundle), matching `ChaControl.LoadAlphaMaskTexture(..., type 0)`.
fn load_over_clothes_mask(game_root: &Path, e: &ListEntry, key: &str) -> Option<Vec<u8>> {
    let name = e.info(key);
    let ab = e.info(&format!("{key}AB"));
    if name.is_empty() || name == "0" || ab.is_empty() || ab == "0" {
        return None;
    }
    let low = format!("{name}_low");
    let names: Vec<&str> = if character_hi_poly() { vec![&name] } else { vec![&low, &name] };
    // Mod bundles live inside the zipmod, not on disk; use the entry's source zip.
    let zip = e.extra.get("__zip").map(String::as_str);
    if let Some(zip) = zip {
        if let Some(bytes) = crate::mods::zipmod_bundle_named(Path::new(zip), &ab) {
            if let Some((_, b)) = crate::unity::named_png_from_bytes(bytes, &names) {
                return Some(b);
            }
        }
    }
    // Sideloader serves one abdata from every zipmod: the mask bundle may come from another mod than the item (a
    // sweater whose masks ship in a shared pack), or the mask be a png it overrides. `--mask-report` found these missed.
    if let Some(b) = sideloader_png(zip, game_root, &ab, &names) {
        return Some(b);
    }
    if let Some((_, b)) = crate::mods::override_bundle(&game_root.join("mods"), &ab).and_then(|b| crate::unity::named_png_from_bytes(b, &names)) {
        return Some(b);
    }
    let found = png_from_ab(game_root, &ab, &names, &[]);
    if found.is_none() {
        eprintln!("ok-extract: {key} {name} not found in {ab} (zip={:?})", e.extra.get("__zip"));
    }
    found
}

/// `--mask-report`: every clothes mask a card's outfits ask for (OverBodyMask, OverBraMask, OverInnerMask) and where
/// it can be found, one line each. `ok`: the extraction finds it. `ours`: it exists (a Sideloader png, or the bundle in
/// another zipmod) but the extraction does not look there, which is ours to fix. `mod`: no source has it (the mod ships
/// without it; the game cannot load it either). `none`: the item asks for no mask.
pub fn mask_report(game_root: &Path, card_path: &Path) -> Result<Vec<String>, String> {
    let bytes = std::fs::read(card_path).map_err(|e| format!("read {}: {e}", card_path.display()))?;
    let count = crate::card::parse_card_bytes_mode(&bytes, 0, false).map(|c| c.coordinate_count).unwrap_or(1).max(1);
    let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom"));
    let mods_dir = game_root.join("mods");
    let mod_catalog = crate::list::load_mods_catalog_shared(&mods_dir);
    let mut rows = Vec::new();
    for outfit in 0..count as i32 {
        let card = parse_card_outfit(card_path, outfit).map_err(|e| format!("parse {} outfit {outfit}: {e}", card_path.display()))?;
        let wanted: std::collections::HashSet<String> = card.resolves.iter().map(|r| r.mod_id.clone()).collect();
        let guids = if wanted.is_empty() { Default::default() } else { crate::mods::find_guids(&mods_dir, Some(&wanted)) };
        for key in ["OverBodyMask", "OverBraMask", "OverInnerMask"] {
            let kind = match key { "OverBodyMask" => "BodyMask", "OverBraMask" => "BraMask", _ => "InnerMask" };
            let Some(entry) = active_clothes_mask_entry(&card, &vanilla, &mod_catalog, &guids, outfit, key) else { continue };
            let (name, ab) = (entry.info(key), entry.info(&format!("{key}AB")));
            let item = format!("{} outfit {outfit} {key}", card_path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default());
            if card.clothes_masks.contains_key(&(outfit, kind.to_string())) { rows.push(format!("ok   {item}: in the card")); continue; }
            if name.is_empty() || name == "0" || ab.is_empty() || ab == "0" { rows.push(format!("none {item}")); continue; }
            let low = format!("{name}_low");
            let names: Vec<&str> = if character_hi_poly() { vec![&name] } else { vec![&low, &name] };
            let zip = entry.extra.get("__zip").map(String::as_str);
            // The extraction now searches every source below too; a source it still misses would be "ours".
            let source = if load_over_clothes_mask(game_root, &entry, key).is_some() { Some(("ok  ", "found")) }
                else if sideloader_png(zip, game_root, &ab, &names).is_some() { Some(("ours", "Sideloader png")) }
                else if crate::mods::override_bundle(&mods_dir, &ab).and_then(|b| crate::unity::named_png_from_bytes(b, &names)).is_some() { Some(("ours", "bundle in another zipmod")) }
                else { None };
            rows.push(match source {
                Some((tag, what)) => format!("{tag} {item}: {name} in {ab} ({what})"),
                None => format!("mod  {item}: {name} in {ab} found nowhere (zip={})", zip.unwrap_or("-")),
            });
        }
    }
    Ok(rows)
}

fn set_material_color(mesh: &mut ExtractedMesh, property: &str, value: [f32; 4]) {
    if let Some(mat) = mesh.mat.as_mut() {
        mat.colors.retain(|name, _| !name.eq_ignore_ascii_case(property));
        mat.colors.insert(property.to_string(), value);
    }
}

// ChaControl.ChangeSettingLip/EyeShadow and ChangeHohoAkaRate operate on
// matDraw after CreateFaceTexture. MaterialEditor then overrides that material.
fn apply_native_face_overlays(game: &Path, vanilla: &ChaListCatalog, mods: &ChaListCatalog,
    card: &CardInfo, outfit: i32, mesh: &mut ExtractedMesh) {
    if !mesh.mat.as_ref().is_some_and(|m|matches!(m.shader.to_ascii_lowercase().as_str(),"koikano/main_skin"|"shader forge/main_skin"|"xukmi/skinplus")) {return;}
    let mut hoho=mesh.mat.as_ref().and_then(|m|m.color("_overcolor2")).unwrap_or([1.;4]);
    hoho[3]=0.2*card.hoho_aka_rate.clamp(0.,1.);
    if crate::gamefs::read_to_string(game.join("UserData/config/system.xml")).ok()
        .is_some_and(|s|s.contains("<hohoAka>false</hohoAka>")) {hoho[3]=0.;}
    for (slot,color) in [("_overcolor1",card.lip_color),("_overcolor2",hoho),("_overcolor3",card.eyeshadow_color)] {
        let value=me_material_color(Some(&card.me_color),outfit,"head",None,mesh,slot.trim_start_matches('_')).unwrap_or(color);
        set_material_color(mesh,slot,value);
    }
    // This existing prefab texture is authored Texture2D data. Unlike the
    // catalog display helper it has not crossed the runtime PNG row boundary.
    if let Some(png)=mesh.shader_extra_textures.remove("_overtex2") {
        mesh.shader_extra_textures.insert("_overtex2".into(),flip_png_v(&png).unwrap_or(png));
    }
    for (slot,category,id,key) in [("_overtex1",403,card.lip_id,"LipTex"),("_overtex3",401,card.eyeshadow_id,"EyeshadowTex")] {
        mesh.shader_extra_textures.remove(slot);mesh.runtime_textures.remove(slot);
        if let Some((png,settings))=load_catalog_display_texture(game,vanilla,mods,category,id,key) {
            mesh.shader_extra_textures.insert(slot.into(),png);
            if let Some(settings)=settings {mesh.runtime_textures.insert(slot.into(),settings);}
        }
    }
    for slot in ["_overtex1","_overtex2","_overtex3"] {
        if let Some(png)=me_texture(Some(&card.me_tex),outfit,"head",None,mesh,slot.trim_start_matches('_')).map(|p|p.to_vec()) {
            mesh.shader_extra_textures.insert(slot.into(),png);mark_material_editor_texture(mesh,slot);
        }
    }
}

fn load_over_body_mask(game_root: &Path, e: &ListEntry) -> Option<Vec<u8>> {
    load_over_clothes_mask(game_root, e, "OverBodyMask")
}

/// `ChangeClothesTopPartsAsync` assigns the subpart A mask to the bra and
/// subpart B mask to the inner renderer. Both are loaded from the equipped
/// list entry, not the underlying bra/inner material's prefab texture.
fn active_clothes_mask_entry(
    card: &CardInfo, vanilla: &ChaListCatalog, mod_catalog: &ChaListCatalog,
    guid_index: &std::collections::HashMap<String,PathBuf>, outfit: i32, key: &str,
) -> Option<ListEntry> {
    let selected=select_card_clothes(card,outfit,vanilla,mod_catalog,guid_index);
    let top=selected.first()?.as_ref()?;
    let top_type=top.0.info("Kind").parse().unwrap_or(0);
    let subparts=select_card_top_subparts(card,outfit,top_type,vanilla,mod_catalog,guid_index);
    let sub_index=if key=="OverInnerMask" {1}else{0};
    Some(subparts.get(sub_index).and_then(Option::as_ref)
        .map(|(_,entry,_,_)|entry).unwrap_or(&top.0).clone())
}

fn over_layer_mask_png(
    game_root: &Path, card: &CardInfo, vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog, guid_index: &std::collections::HashMap<String,PathBuf>,
    outfit: i32, key: &str,
) -> Option<Vec<u8>> {let _spot=crate::profile::spot("over_layer_mask_png");
    // ChangeClothesTop loads its list mask first. Only Kind 1/2 loads SubA/B
    // afterwards, replacing that mask. IDs/resolves retained from another
    // style do not load subparts, even when their ID zero is a valid item.
    let entry=active_clothes_mask_entry(card,vanilla,mod_catalog,guid_index,outfit,key)?;
    let mask_kind=match key {"OverBodyMask"=>"BodyMask","OverBraMask"=>"BraMask","OverInnerMask"=>"InnerMask",_=>""};
    if let Some(png)=card.clothes_masks.get(&(outfit,mask_kind.to_string())) {return flip_png_v(png);}
    load_over_clothes_mask(game_root,&entry,key)
}

/// ChaFileDefine.alphaState toggles the two _AlphaMask channels. Clothes
/// combine them with MainTex.a; Koikano/main_skin clips the channels alone.
fn clothes_alpha_state(top_state: u8) -> [bool; 2] {
    // ChaFileDefine.alphaState[state, 0..2], used by ChangeAlphaMask.
    match top_state {
        0 => [true, true],
        1 | 2 => [false, true],
        _ => [false, false],
    }
}

fn apply_native_clothes_over_mask(mesh: &mut ExtractedMesh, mask: &[u8],
    v_scale: f32, v_offset: f32, enabled: [bool; 2]) {
    // Catalog textures are Unity bottom-origin; native shader PNG bindings
    // use top-origin rows, matching the existing CPU mask sampling contract.
    mesh.alpha_mask_png = flip_png_v(mask);
    mesh.runtime_textures.remove("_AlphaMask");
    if let Some(mat) = mesh.mat.as_mut() {
        for (key, value) in [("_AlphaMask_ST_x", 1.), ("_AlphaMask_ST_y", v_scale),
            ("_AlphaMask_ST_z", 0.), ("_AlphaMask_ST_w", v_offset),
            ("_alpha_a", if enabled[0] {1.} else {0.}),
            ("_alpha_b", if enabled[1] {1.} else {0.})] {
            mat.floats.retain(|name, _| !name.eq_ignore_ascii_case(key));
            mat.floats.insert(key.into(), value);
        }
    }
}

#[test]
fn native_clothes_dynamic_mask_matches_cpu_rows_and_keeps_main_texture() {
    let mut mask = image::RgbaImage::new(2, 2);
    mask.put_pixel(0,0,image::Rgba([0,255,255,255]));
    mask.put_pixel(1,0,image::Rgba([255,0,255,255]));
    mask.put_pixel(0,1,image::Rgba([255,255,255,255]));
    mask.put_pixel(1,1,image::Rgba([128,255,255,255]));
    let mask = encode_png(&mask).unwrap();
    let main = crate::unity::solid_png([1.;4],2,2).unwrap();
    for state in 0..4 {
        let flags=clothes_alpha_state(state);
        let mut mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {
            shader:"Shader Forge/main_opaque".into(),..Default::default()}),
            texture_png:Some(main.clone()),..Default::default()};
        apply_native_clothes_over_mask(&mut mesh,&mask,1.,0.,flags);
        let part=crate::session::SessionPart::from_mesh("clothes_top",&mesh,"masked",true);
        assert_eq!(part.shader_textures["_MainTex"],main);
        assert_eq!(part.alpha_mask_st,[1.,1.,0.,0.]);
        let native=crate::teximg::load_from_memory(&part.shader_textures["_AlphaMask"]).unwrap().to_rgba8();
        let cpu=crate::teximg::load_from_memory(&apply_clothes_over_mask(&main,&mask,1.,0.,flags).unwrap()).unwrap().to_rgba8();
        for (a,b) in native.pixels().zip(cpu.pixels()) {
            assert_eq!(masked_alpha(255,[a[0],a[1]],flags),b[3]);
        }
        let mat=part.source_material.unwrap();
        assert_eq!(mat.float("_alpha_a"),Some(if flags[0] {1.}else{0.}));
        assert_eq!(mat.float("_alpha_b"),Some(if flags[1] {1.}else{0.}));
    }
}

fn masked_alpha(alpha: u8, mask: [u8; 2], enabled: [bool; 2]) -> u8 {
    let red = if enabled[0] { mask[0] } else { 255 };
    let green = if enabled[1] { mask[1] } else { 255 };
    alpha.min(red).min(green)
}

fn apply_clothes_over_mask(png: &[u8], mask_png: &[u8], v_scale: f32, v_offset: f32, enabled: [bool; 2]) -> Option<Vec<u8>> {
    let mut parameters=Vec::with_capacity(mask_png.len()+10);
    parameters.extend_from_slice(&v_scale.to_le_bytes());parameters.extend_from_slice(&v_offset.to_le_bytes());
    parameters.extend([enabled[0] as u8,enabled[1] as u8]);parameters.extend_from_slice(mask_png);
    png_transform_cache::transform("clothes_over_mask_v1",png,&parameters,||apply_clothes_over_mask_uncached(png,mask_png,v_scale,v_offset,enabled))
}
fn apply_clothes_over_mask_uncached(png: &[u8], mask_png: &[u8], v_scale: f32, v_offset: f32, enabled: [bool; 2]) -> Option<Vec<u8>> {
    let mut main = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask_png).ok()?.to_rgba8();
    let (w,h) = main.dimensions();
    let (mw,mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 { return None; }
    for y in 0..h {
        let v = ((y as f32 + 0.5) / h as f32 * v_scale + v_offset).rem_euclid(1.0);
        let my = ((1.0 - v) * mh as f32).floor().min((mh-1) as f32) as u32;
        for x in 0..w {
            let mx = (x as u64 * mw as u64 / w as u64) as u32;
            let m = mask.get_pixel(mx.min(mw-1),my).0;
            let p = main.get_pixel_mut(x,y);
            p[3] = masked_alpha(p[3], [m[0], m[1]], enabled);
        }
    }
    encode_png(&main)
}

/// Shader Forge/main_hair and main_hair_front discard when
/// `_MainTex.a * _AlphaMask.r < 0.5`. Material Editor can supply the mask even
/// when the prefab material's `_AlphaMask` slot is empty. Both decoded PNGs
/// use the same top-origin row convention, so combine corresponding texels.
fn apply_hair_alpha_mask(png: &[u8], mask_png: &[u8]) -> Option<Vec<u8>> {let _spot=crate::profile::spot("apply_hair_alpha_mask");
    png_transform_cache::transform("hair_alpha_mask_v1",png,mask_png,||apply_hair_alpha_mask_uncached(png,mask_png))
}
fn apply_hair_alpha_mask_uncached(png: &[u8], mask_png: &[u8]) -> Option<Vec<u8>> {
    let mut main = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask_png).ok()?.to_rgba8();
    let (w, h) = main.dimensions();
    let (mw, mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 { return None; }
    for y in 0..h {
        let my = (y as u64 * mh as u64 / h as u64).min((mh - 1) as u64) as u32;
        for x in 0..w {
            let mx = (x as u64 * mw as u64 / w as u64).min((mw - 1) as u64) as u32;
            let alpha = &mut main.get_pixel_mut(x, y)[3];
            *alpha = ((*alpha as u16 * mask.get_pixel(mx, my)[0] as u16 + 127) / 255) as u8;
        }
    }
    encode_png(&main)
}

/// Body `_AlphaMask` texture supplied by the equipped top / inner sub-clothes
/// (`ChaControl.cs:5968/6245/9444`). Port of StudioEditor `KoikatsuBodyMaskBake`:
///   body.a *= lerp(1, mask.r, chR) * lerp(1, mask.g, chG)  with chR=chG=1.
fn over_body_mask_png(
    game_root: &Path, card: &CardInfo, vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog, guid_index: &std::collections::HashMap<String,PathBuf>,
    outfit: i32,
) -> Option<Vec<u8>> {
    over_layer_mask_png(game_root,card,vanilla,mod_catalog,guid_index,outfit,"OverBodyMask")
}

/// ChaControl's alpha_a/alpha_b are binary outfit-state switches. Whiten
/// disabled channels, then let the GPU sample and min the two channels at the
/// body's authored UV, matching Koikano/main_skin t0 and avoiding a MainTex
/// alpha bake that a later Material Editor override can erase.
/// The face's draw blend: "masked" when its _AlphaMask (with the material's _alpha_a/_alpha_b) can clip anything.
fn face_alpha_blend(mesh: &mut ExtractedMesh) -> &'static str {
    let Some(mask) = mesh.alpha_mask_png.take() else { return "opaque" };
    let enabled = ["_alpha_a", "_alpha_b"].map(|key| mesh.mat.as_ref().and_then(|m| m.float(key)).unwrap_or(1.0) != 0.0);
    mesh.alpha_mask_png = if enabled == [false, false] { None } else { body_mask_for_alpha_state(&mask, enabled) };
    if mesh.alpha_mask_png.is_some() { "masked" } else { "opaque" }
}

fn body_mask_for_alpha_state(mask_png: &[u8], enabled: [bool; 2]) -> Option<Vec<u8>> {
    if enabled == [true, true] { return Some(mask_png.to_vec()); }
    png_transform_cache::transform("body_alpha_state_v1",mask_png,&[enabled[0] as u8,enabled[1] as u8],||body_mask_for_alpha_state_uncached(mask_png,enabled))
}
fn body_mask_for_alpha_state_uncached(mask_png: &[u8], enabled: [bool; 2]) -> Option<Vec<u8>> {
    if enabled == [true, true] { return Some(mask_png.to_vec()); }
    let mut mask = crate::teximg::load_from_memory(mask_png).ok()?.to_rgba8();
    for pixel in mask.pixels_mut() {
        if !enabled[0] { pixel[0] = 255; }
        if !enabled[1] { pixel[1] = 255; }
    }
    encode_png(&mask)
}

/// KSOX skin overlay for (outfit, kind); mirrors `KoikatsuSkinOverlayData.TryGetTexture`
/// (use the outfit's coordinate, else coordinate 0, else the first).
fn ksox_overlay_png(card: &CardInfo, outfit: i32, kind: i32) -> Option<Vec<u8>> {
    if card.ksox.is_empty() {
        return None;
    }
    let has = |c: i32| card.ksox.keys().any(|(cc, _)| *cc == c);
    let coord = if has(outfit) {
        outfit
    } else if has(0) {
        0
    } else {
        card.ksox.keys().next().map(|(c, _)| *c)?
    };
    card.ksox.get(&(coord, kind)).cloned()
}

fn sample_rgba_clamped(image:&image::RgbaImage,u:f32,v:f32)->[f32;4] {
    sample_rgba_clamped_space(image,u,v,false)
}
fn sample_rgba_clamped_space(image:&image::RgbaImage,u:f32,v:f32,linear:bool)->[f32;4] {
    static TRANSFER:std::sync::OnceLock<[f32;256]>=std::sync::OnceLock::new();
    let transfer=TRANSFER.get_or_init(||std::array::from_fn(|i|srgb_channel_to_linear(i as f32/255.0)));
    let (width,height)=image.dimensions();let x=u*width as f32-0.5;let y=v*height as f32-0.5;
    let x0=x.floor();let y0=y.floor();let fx=x-x0;let fy=y-y0;
    let pixel=|x:f32,y:f32| image.get_pixel(x.clamp(0.,(width-1) as f32) as u32,y.clamp(0.,(height-1) as f32) as u32).0;
    let a=pixel(x0,y0);let b=pixel(x0+1.,y0);let c=pixel(x0,y0+1.);let d=pixel(x0+1.,y0+1.);
    std::array::from_fn(|i| {
        let channel=|p:[u8;4]| {let v=p[i] as f32/255.0;if linear&&i<3 {transfer[p[i] as usize]}else{v}};
        lerp_f(lerp_f(channel(a),channel(b),fx),lerp_f(channel(c),channel(d),fx),fy)
    })
}

/// KSOX PS lerps RGB in linear space and emits max(base.a, overlay.a).
/// Its SrcAlpha/OneMinusSrcAlpha pass blends back over the existing RT.
/// KSOX face/body/eye hooks all use this same composite material and pass.
fn composite_overlay(base:&[u8],overlay:&[u8])->Option<Vec<u8>> {
    composite_overlay_space(base,overlay,true,true)
}

fn composite_overlay_space(base:&[u8],overlay:&[u8],base_srgb:bool,output_srgb:bool)->Option<Vec<u8>> {
    if !crate::okstrict::assembling() { return Some(base.to_vec()); }
    let mut parameters=Vec::with_capacity(overlay.len()+2);parameters.extend([base_srgb as u8,output_srgb as u8]);parameters.extend_from_slice(overlay);
    png_transform_cache::transform("composite_overlay_v1",base,&parameters,||composite_overlay_space_uncached(base,overlay,base_srgb,output_srgb))
}
fn composite_overlay_space_uncached(base:&[u8],overlay:&[u8],base_srgb:bool,output_srgb:bool)->Option<Vec<u8>> {
    let mut dst=crate::teximg::load_from_memory(base).ok()?.to_rgba8();let over=crate::teximg::load_from_memory(overlay).ok()?.to_rgba8();
    let (w,h)=dst.dimensions();if w==0||h==0||over.width()==0||over.height()==0{return None;}
    for y in 0..h {for x in 0..w {
        let p=sample_rgba_linear(&over,(x as f32+0.5)/w as f32,(y as f32+0.5)/h as f32);
        let d=dst.get_pixel_mut(x,y);let alpha=d[3] as f32/255.0;let source_alpha=alpha.max(p[3]);
        for c in 0..3 {
            let base=d[c] as f32/255.;let base=if base_srgb {srgb_channel_to_linear(base)}else{base};
            let value=lerp_f(base,p[c],p[3]*source_alpha);
            d[c]=to_unorm8(if output_srgb {linear_channel_to_srgb(value)}else{value});
        }
        d[3]=to_unorm8(lerp_f(alpha,source_alpha,source_alpha));
    }}encode_png(&dst)
}

pub(crate) fn run_no_panic<F, T>(f: F) -> Result<T, String>
where
    F: FnOnce() -> Result<T, String> + std::panic::UnwindSafe,
{
    match std::panic::catch_unwind(f) {
        Ok(r) => r,
        Err(e) => {
            let msg = if let Some(s) = e.downcast_ref::<&str>() {
                (*s).to_string()
            } else if let Some(s) = e.downcast_ref::<String>() {
                s.clone()
            } else {
                "internal error".into()
            };
            eprintln!("ok-extract error: {msg}");
            Err(msg)
        }
    }
}

fn write_mesh(
    out_dir: &Path,
    file_stem: &str,
    mesh: &ExtractedMesh,
    tint: Option<[f32; 4]>,
) -> std::io::Result<(PathBuf, Option<String>)> {
    let safe: String = file_stem
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '_' { c } else { '_' })
        .collect();
    let path = out_dir.join(format!("{safe}.obj"));
    let obj = if mesh.obj.is_empty() {
        crate::unity::mesh_to_obj(&mesh.name, &mesh.positions, &mesh.uvs, &mesh.indices)
    } else {
        mesh.obj.clone()
    };
    std::fs::write(&path, obj)?;
    let tex = if let Some(png) = &mesh.texture_png {
        let bytes = tint
            .and_then(|c| crate::unity::tint_png(png, c))
            .unwrap_or_else(|| png.clone());
        let tp = format!("{safe}.png");
        std::fs::write(out_dir.join(&tp), &*crate::teximg::as_png(&bytes))?;
        Some(tp)
    } else {
        None
    };
    Ok((path, tex))
}

fn emit_part(
    out_dir: Option<&Path>,
    session_parts: &mut Vec<crate::session::SessionPart>,
    stem: &str,
    role: &str,
    mesh: &ExtractedMesh,
    blend: &str,
    unlit: bool,
) -> Result<(String, Option<String>), String> {let _spot=crate::profile::spot("emit_part");
    session_parts.push(crate::session::SessionPart::from_mesh(
        role, mesh, blend, unlit,
    ));
    if let Some(dir) = out_dir {
        let (p, tex) = write_mesh(dir, stem, mesh, None).map_err(|e| e.to_string())?;
        // Optional source-input audit. These are the exact PNG bytes handed
        // to the session shader path, beside the exported mesh/albedo. Keep
        // them out of normal exports to avoid duplicating large textures.
        if std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() == Ok("1")
            || (std::env::var("OK_DUMP_HAIR_INPUTS").as_deref() == Ok("1")
                && (mesh.hair_shader_hint || mesh.mat.as_ref().is_some_and(|m|
                    is_native_hair_shader(&m.shader))))
        {
            for (suffix, bytes) in [
                ("color-mask.png", mesh.color_mask_png.as_deref()),
                ("detail-mask.png", mesh.detail_mask_png.as_deref()),
                ("hair-gloss.png", mesh.hair_gloss_png.as_deref()),
            ] {
                if let Some(bytes) = bytes {
                    std::fs::write(p.with_extension(suffix), &*crate::teximg::as_png(bytes)).map_err(|e| e.to_string())?;
                }
            }
            if std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() == Ok("1") {
                let mut inputs = std::collections::BTreeMap::new();
                for (property,bytes) in [("_MainTex",&mesh.texture_png),("_ColorMask",&mesh.color_mask_png),
                    ("_DetailMask",&mesh.detail_mask_png),("_LineMask",&mesh.line_mask_png),
                    ("_AlphaMask",&mesh.alpha_mask_png),("_NormalMap",&mesh.normal_map_png),("_HairGloss",&mesh.hair_gloss_png)] {
                    if let Some(bytes)=bytes {let file=p.with_extension(format!("shader-{property}.png"));
                        std::fs::write(&file,&*crate::teximg::as_png(bytes)).map_err(|e|e.to_string())?;
                        inputs.insert(property.to_string(),file.file_name().unwrap().to_string_lossy().into_owned());}
                }
                // The session table is the exact set handed to native shaders,
                // including ME replacements. Null shader defaults stay absent.
                for (property, bytes) in &session_parts.last().unwrap().shader_textures {
                    if !property.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
                        return Err(format!("Unsafe shader input property: {property}"));
                    }
                    let file = p.with_extension(format!("shader-{property}.png"));
                    std::fs::write(&file, &*crate::teximg::as_png(bytes)).map_err(|e| e.to_string())?;
                    inputs.insert(property.clone(), file.file_name().unwrap().to_string_lossy().into_owned());
                }
                let audit = serde_json::json!({"mesh":mesh.name,"role":role,"material":mesh.mat,
                    "shader_inputs":inputs,"runtime_textures":mesh.runtime_textures,
                    "global_ramp":"toon-ramp.png","scope":"session PNG inputs, not GPU mip or final colour parity"});
                std::fs::write(p.with_extension("material-inputs.json"),serde_json::to_vec_pretty(&audit).map_err(|e|e.to_string())?)
                    .map_err(|e|e.to_string())?;
            }
        }
        let name = p
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_else(|| format!("{stem}.obj"));
        Ok((name, tex))
    } else {
        Ok((format!("{stem}.obj"), None))
    }
}

fn pick_path<'a>(
    meshes: &'a [ExtractedMesh],
    want: &str,
    pid: i64,
) -> Option<&'a ExtractedMesh> {
    meshes
        .iter()
        .find(|m| m.path_id == pid && m.name.eq_ignore_ascii_case(want))
        .or_else(|| meshes.iter().find(|m| m.path_id == pid))
}

pub(crate) fn skin_mesh(mesh: &mut ExtractedMesh, skel: &crate::pose::Skeleton) -> bool {
    skin_mesh_impl(mesh,skel,true)
}
pub(crate) fn skin_mesh_geometry(mesh:&mut ExtractedMesh,skel:&crate::pose::Skeleton)->bool {
    skin_mesh_impl(mesh,skel,false)
}
fn skin_mesh_impl(mesh:&mut ExtractedMesh,skel:&crate::pose::Skeleton,emit_obj:bool)->bool {
    let ln = mesh.name.to_ascii_lowercase();
    // Unity skins every renderer with its authored weights. Classifying by
    // asset name made mod hair such as `35_mesh` take the old repair/prune/
    // spike path and shifted its game-dump bounds by 16 mm. Use the same LBS
    // rule for all card meshes, regardless of name or source category.
    let sm = std::env::var("OK_SMOOTH")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(0);
    crate::pose::smooth_weights(mesh, sm);
    // Unity: pal = bone.localToWorld * mesh.bindPose. Never require
    // bind_poses.len() >= worlds.len() ??o_body_a is 100 binds / 101 SMR bones
    // and falling back to inv(rest) made pal = I (raw mesh, head/body split).
    let n = if !mesh.bind_poses.is_empty() {
        mesh.bind_poses.len().min(skel.worlds.len())
    } else {
        skel.worlds.len().min(skel.rest.len())
    };
    if n == 0 || mesh.weights.is_empty() {
        return false;
    }
    if mesh.rest_positions.len() != mesh.positions.len() {
        mesh.rest_positions = mesh.positions.clone();
    }
    let posed = skel.worlds[..n].to_vec();
    if std::env::var("OK_BONEDIAG").is_ok() && ln.starts_with("o_body_") {
        let mut out = String::new();
        for (i, w) in posed.iter().enumerate() {
            let nm = skel.names.get(i).cloned().unwrap_or_default();
            if nm.is_empty() {
                continue;
            }
            out.push_str(&format!("{nm} {:.6} {:.6} {:.6}\n", w[0][3], w[1][3], w[2][3]));
        }
        let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\our_bones.txt", out);
        let mut loc = String::new();
        for t in skel.tfs.values() {
            let par = t
                .parent
                .and_then(|p| skel.tfs.get(&p))
                .map(|p| p.name.clone())
                .unwrap_or_default();
            loc.push_str(&format!(
                "{} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {}\n",
                t.name, t.pos[0], t.pos[1], t.pos[2], t.rot[0], t.rot[1], t.rot[2], t.rot[3],
                t.scale[0], t.scale[1], t.scale[2], par
            ));
        }
        let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\our_bone_locals.txt", loc);
    }
    let binds = if mesh.bind_poses.len() >= n {
        mesh.bind_poses[..n].to_vec()
    } else {
        crate::pose::binds_from_rest(&skel.rest[..n])
    };
    if binds.len() != posed.len() {
        return false;
    }
    if std::env::var("OK_SKINDIAG").is_ok()
        && (ln.contains("cf_o_face") || ln.contains("hair_f_04_00"))
    {
        let s_b = crate::pose::world_scale_pub(&binds[0]);
        let s_p = crate::pose::world_scale_pub(&posed[0]);
        let sm = crate::pose::mat_mul_pub(&posed[0], &binds[0]);
        let s_m = crate::pose::world_scale_pub(&sm);
        let pp = crate::pose::mat_pos(&posed[0]);
        eprintln!(
            "SKINDIAG {} n={n} bind0s=({:.4},{:.4},{:.4}) p0=({:.4},{:.4},{:.4}) ps=({:.4},{:.4},{:.4}) sm=({:.4},{:.4},{:.4})",
            mesh.name,
            s_b[0], s_b[1], s_b[2],
            pp[0], pp[1], pp[2],
            s_p[0], s_p[1], s_p[2],
            s_m[0], s_m[1], s_m[2]
        );
    }
    let Some(mut pos) = (if std::env::var("OK_DQS").is_ok() {
        crate::pose::skin_positions_dqs(mesh, &posed, &binds)
    } else {
        crate::pose::skin_positions(mesh, &posed, &binds)
    }) else {
        return false;
    };
    if std::env::var("OK_SKINDIAG").is_ok() && ln.contains("hitomi") {
        let (r0, r1) = crate::pose::aabb(&mesh.rest_positions);
        let (s0, s1) = crate::pose::aabb(&pos);
        eprintln!(
            "SKINDIAG {} rest x[{:.4},{:.4}] y[{:.4},{:.4}] z[{:.4},{:.4}] -> skin x[{:.4},{:.4}] y[{:.4},{:.4}] z[{:.4},{:.4}]",
            mesh.name,
            r0[0], r1[0], r0[1], r1[1], r0[2], r1[2],
            s0[0], s1[0], s0[1], s1[1], s0[2], s1[2]
        );
        for i in 0..n {
            let m = crate::pose::mat_mul_pub(&posed[i], &binds[i]);
            let s = crate::pose::world_scale_pub(&m);
            if (s[0] - 1.0).abs() > 0.1 || (s[1] - 1.0).abs() > 0.1 || (s[2] - 1.0).abs() > 0.1 {
                eprintln!(
                    "SKINBONE {} [{}] {} prod_scale={:?}",
                    mesh.name,
                    i,
                    skel.names.get(i).map(|x| x.as_str()).unwrap_or("?"),
                    s
                );
            }
        }
    }
    // Region-limited dual-quaternion skinning: only the shoulder/arm vertices
    // use DQS, which removes the deltoid collapse without the knee bulging that
    // full DQS causes.
    if std::env::var("OK_DQSH").is_ok() {
        if let Some(dq) = crate::pose::skin_positions_dqs(mesh, &posed, &binds) {
            for i in 0..pos.len() {
                let b = mesh.bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
                let w = mesh.weights.get(i).copied().unwrap_or([0.0; 4]);
                let arm = (0..4).any(|k| {
                    w[k] > 0.15
                        && skel
                            .bone_ids
                            .get(b[k] as usize)
                            .and_then(|id| skel.tfs.get(id))
                            .map(|t| {
                                let n = &t.name;
                                n.contains("shoulder")
                                    || n.contains("arm")
                                    || n.contains("forearm")
                                    || n.contains("hand")
                                    || n.contains("wrist")
                            })
                            .unwrap_or(false)
                });
                if arm {
                    pos[i] = dq[i];
                }
            }
        }
    }
    // Laplacian relaxation used to be applied here but it carves a notch into
    // the pose+shape shoulder, so it is only kept behind an opt-in flag.
    if std::env::var("OK_RELAX").is_ok() {
        crate::pose::relax_positions(&mut pos, &mesh.indices, 1, 0.2);
    }
    if mesh.rest_normals.len() != mesh.normals.len() {
        mesh.rest_normals = mesh.normals.clone();
    }
    mesh.normals = mesh.rest_normals.clone();
    if let Some(nrm) = crate::pose::skin_normals(mesh, &posed, &binds) {
        mesh.normals = nrm;
    }
    if std::env::var("OK_DIAG").is_ok() && mesh.name == "o_body_a" {
        let mut band = vec![f32::MIN; 24];
        for p in &pos {
            let bx = ((p[0] + 0.24) / 0.02).floor() as i32;
            if (0..24).contains(&bx) && p[2] > -0.12 {
                let b = bx as usize;
                if p[1] > band[b] {
                    band[b] = p[1];
                }
            }
        }
        let prof: Vec<String> = band
            .iter()
            .enumerate()
            .map(|(i, y)| format!("{:.3}@{:.2}", y, -0.24 + i as f32 * 0.02))
            .collect();
        eprintln!("ok-extract: DIAG shoulder-top {}", prof.join(" "));
        let bnames: Vec<String> = skel
            .bone_ids
            .iter()
            .enumerate()
            .filter_map(|(i, id)| {
                skel.tfs.get(id).map(|t| format!("{i}:{}", t.name))
            })
            .collect();
        eprintln!("ok-extract: DIAG bones {}", bnames.join(" "));
        for (i, p) in pos.iter().enumerate() {
            if p[0].abs() > 0.20 || p[0].abs() < -0.20 {
                let mut parts = Vec::new();
                for k in 0..4 {
                    let bi = mesh.bone_idx[i][k] as usize;
                    let w = mesh.weights[i][k];
                    if w > 0.04 {
                        let nm = skel
                            .bone_ids
                            .get(bi)
                            .and_then(|id| skel.tfs.get(id))
                            .map(|t| t.name.clone())
                            .unwrap_or_else(|| "?".into());
                        parts.push(format!("{nm}:{w:.2}"));
                    }
                }
                eprintln!(
                    "ok-extract: DIAG spike v{i} ({:.3},{:.3},{:.3}) [{}]",
                    p[0],
                    p[1],
                    p[2],
                    parts.join(",")
                );
            }
        }
        for (i, p) in pos.iter().enumerate() {
            if p[1] > 0.85 && p[1] < 1.20 && p[2] > 0.05 {
                let mut parts = Vec::new();
                for k in 0..4 {
                    let bi = mesh.bone_idx[i][k] as usize;
                    let w = mesh.weights[i][k];
                    if w > 0.04 {
                        let nm = skel
                            .bone_ids
                            .get(bi)
                            .and_then(|id| skel.tfs.get(id))
                            .map(|t| t.name.clone())
                            .unwrap_or_else(|| "?".into());
                        parts.push(format!("{nm}:{w:.2}"));
                    }
                }
                eprintln!(
                    "ok-extract: DIAG breast v{i} ({:.3},{:.3},{:.3}) [{}]",
                    p[0],
                    p[1],
                    p[2],
                    parts.join(",")
                );
            }
        }
        let mut shown = 0;
        for (i, p) in pos.iter().enumerate() {
            let mut bw = 0.0f32;
            for k in 0..4 {
                let nm = skel
                    .bone_ids
                    .get(mesh.bone_idx[i][k] as usize)
                    .and_then(|id| skel.tfs.get(id))
                    .map(|t| t.name.clone())
                    .unwrap_or_default();
                if nm.contains("bust") {
                    bw = bw.max(mesh.weights[i][k]);
                }
            }
            if bw > 0.05 && shown < 25 {
                eprintln!(
                    "ok-extract: DIAG bustw v{i} ({:.3},{:.3},{:.3}) maxbust={:.2}",
                    p[0], p[1], p[2], bw
                );
                shown += 1;
            }
        }
    }
    if emit_obj || std::env::var("OK_SKINDIAG").is_ok() {
    let (mn, mx) = crate::pose::aabb(&pos);
    let mut max_bi = 0u16;
    for b in &mesh.bone_idx {
        for k in 0..4 {
            if (b[k] as usize) < 4096 {
                max_bi = max_bi.max(b[k]);
            }
        }
    }
    eprintln!(
        "ok-extract: skinned {} nv={} aabb x=[{:.3},{:.3}] y=[{:.3},{:.3}] z=[{:.3},{:.3}] bones={n} bind={} max_bi={max_bi}",
        mesh.name, mesh.positions.len(), mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], mesh.bind_poses.len()
    );
    }
    if emit_obj {crate::pose::rebuild_obj(mesh, pos);}else{mesh.positions=pos;}
    true
}

/// ChaReference: HeadParent=cf_s_head, HairParent=cf_J_FaceUp_ty.
/// Head bone lives on the body; face bones live on p_cf_head_bone which is
/// SetParent(cf_s_head, worldPositionStays: false).
pub(crate) fn head_onto_body(body: Option<&crate::pose::Skeleton>, head: Option<&crate::pose::Skeleton>) -> crate::pose::Mat4 {
    let parent = body
        .and_then(|b| b.world_tf("cf_s_head"))
        .unwrap_or_else(crate::pose::ident);
    let root = head
        .map(|h| h.root_world())
        .unwrap_or_else(crate::pose::ident);
    crate::pose::under_parent(&parent, &root)
}

fn is_head_attach(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.starts_with("cf_j_")
        || n.starts_with("a_n_hair")
        || n.starts_with("a_n_head")
        || n.starts_with("a_n_megane")
        || n.starts_with("a_n_ear")
        || n.starts_with("a_n_nose")
        || n.starts_with("a_n_mouth")
}

pub(crate) fn bone_world(
    body: Option<&crate::pose::Skeleton>,
    head: Option<&crate::pose::Skeleton>,
    name: &str,
) -> Option<crate::pose::Mat4> {
    if name.is_empty() || name.eq_ignore_ascii_case("none") || name == "0" {
        return None;
    }
    // ChaReference: HairParent / a_n_hair_* / a_n_head* live on the head prefab
    // (CreateReferenceInfo case 1). oo_base also contains p_cf_head_bone, so
    // looking up those names on the body skeleton would hit the unparented copy.
    if is_head_attach(name) {
        if let Some(h) = head {
            if let Some(m) = h.world_tf(name) {
                let attach = head_onto_body(body, head);
                return Some(crate::pose::mat_mul_pub(&attach, &m));
            }
        }
    }
    if let Some(b) = body {
        if let Some(m) = b.world_tf(name) {
            return Some(m);
        }
    }
    if let Some(h) = head {
        if let Some(m) = h.world_tf(name) {
            let attach = head_onto_body(body, head);
            return Some(crate::pose::mat_mul_pub(&attach, &m));
        }
    }
    None
}

/// Preserve the actual Transform ancestry for DynamicBone. A composed socket
/// matrix can contain shear and cannot be reduced to one local TRS.
pub(crate) fn attachment_transform_chain(body:Option<&crate::pose::Skeleton>,head:Option<&crate::pose::Skeleton>,name:&str,local:&crate::pose::Mat4)->Option<Vec<crate::pose::Tf>> {
    use crate::pose::{Tf,ident,mat_mul_pub,trs};
    let path=|skel:&crate::pose::Skeleton,key:&str|->Option<Vec<Tf>> {
        let mut id=skel.names.iter().position(|n|n.eq_ignore_ascii_case(key)).and_then(|i|skel.bone_ids.get(i).copied())
            .or_else(||skel.tfs.iter().filter(|(_,tf)|tf.name.eq_ignore_ascii_case(key)).map(|(id,_)|*id).min())?;
        let mut chain=Vec::new();let mut seen=std::collections::HashSet::new();
        loop {if !seen.insert(id){return None;}let tf=skel.tfs.get(&id)?;chain.push(tf.clone());
            match tf.parent.filter(|p|*p!=0) {Some(parent)=>id=parent,None=>break,}}
        chain.reverse();Some(chain)
    };
    let use_head=head.is_some_and(|h|h.world_tf(name).is_some())
        && (is_head_attach(name)||body.is_none_or(|b|b.world_tf(name).is_none()));
    let mut chain=if use_head {
        let mut prefix=body.and_then(|b|path(b,"cf_s_head")).unwrap_or_default();
        let mut suffix=path(head?,name)?;
        // Head SetParent(cf_s_head,false) replaces the authored root world.
        let root=suffix.first_mut()?;root.pos=[0.;3];root.rot=[0.,0.,0.,1.];root.scale=[1.;3];
        prefix.extend(suffix);prefix
    } else {path(body?,name)?};
    let mut extra=Tf {name:"__ok_attachment_local".into(),parent:None,sibling_index:None,pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
    crate::pose::replace_local_trs(&mut extra,local);chain.push(extra);
    let represented=chain.iter().fold(ident(),|m,tf|mat_mul_pub(&m,&trs(tf.pos,tf.rot,tf.scale)));
    let expected=mat_mul_pub(&bone_world(body,head,name)?,local);
    if represented.iter().flatten().zip(expected.iter().flatten()).any(|(a,b)|!a.is_finite()||(a-b).abs()>2e-5){return None;}
    Some(chain)
}

fn accessory_slot_from_property(property: &str) -> Option<usize> {
    let p = property.to_ascii_lowercase();
    let i = p.find("accessory")?;
    let rest = &p[i + 9..];
    let n: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    if n.is_empty() {
        None
    } else {
        n.parse().ok()
    }
}

fn acc_move_matrix(mv: crate::card::AccMove) -> crate::pose::Mat4 {
    let pos = [mv.pos[0] * 0.01, mv.pos[1] * 0.01, mv.pos[2] * 0.01];
    let scl = [
        mv.scale[0].clamp(0.01, 100.0),
        mv.scale[1].clamp(0.01, 100.0),
        mv.scale[2].clamp(0.01, 100.0),
    ];
    crate::pose::local_trs(pos, mv.rot, scl)
}

fn identity_move() -> crate::pose::Mat4 {
    crate::pose::local_trs([0.0; 3], [0.0; 3], [1.0, 1.0, 1.0])
}

/// `UpdateAccessoryMoveFromInfo`: HideHair==1 resets both N_move locals to identity.
fn acc_place_moves(a: &crate::card::AccPart, hide_hair: bool) -> [crate::pose::Mat4; 2] {
    if hide_hair {
        let id = identity_move();
        return [id, id];
    }
    [acc_move_matrix(a.add_move[0]), acc_move_matrix(a.add_move[1])]
}

fn hide_hair_list(entry: &ListEntry) -> bool {
    entry.info("HideHair") == "1"
}

thread_local! {
    static ACC_PLACE: std::cell::RefCell<Option<[crate::pose::Mat4; 2]>> =
        const { std::cell::RefCell::new(None) };
}

struct AccPlaceGuard;

impl Drop for AccPlaceGuard {
    fn drop(&mut self) {
        ACC_PLACE.with(|c| *c.borrow_mut() = None);
    }
}

fn begin_acc_place(moves: [crate::pose::Mat4; 2]) -> AccPlaceGuard {
    ACC_PLACE.with(|c| *c.borrow_mut() = Some(moves));
    AccPlaceGuard
}

fn acc_local_for(card: &CardInfo, property: &str) -> Option<crate::pose::Mat4> {
    let slot = accessory_slot_from_property(property)?;
    let a = card.accessory.get(slot)?;
    let m = acc_move_matrix(a.add_move[0]);
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: acc_local slot={slot} j0={:?} j1={:?}",
            a.add_move[0], a.add_move[1]
        );
    }
    Some(m)
}

fn resolve_acc_parent(card_key: &str, catalog_parent: &str) -> String {
    // ChaControl.ChangeAccessoryNoAsync (ChaControl.cs:10352):
    //   if (parentKey == "") parentKey = list.Parent;
    //   ChangeAccessoryParent(slot, parentKey);
    if std::env::var("OK_ACCPAR").is_ok() {
        eprintln!("ok-extract: ACCPAR card={card_key:?} list={catalog_parent:?}");
    }
    // The card stores the generic `a_n_head` for accessories whose list Parent is
    // the specific `a_n_headtop` (HEXA Hachikuji Headband). `OK_ACCLISTPARENT`
    // prefers the list value; the default uses the card (what ChaControl.cs:10352
    // does, and what the game's mesh dump shows).
    if std::env::var("OK_ACCLISTPARENT").is_ok()
        && card_key == "a_n_head"
        && catalog_parent == "a_n_headtop"
    {
        return catalog_parent.to_string();
    }
    if card_key.is_empty() {
        catalog_parent.to_string()
    } else {
        card_key.to_string()
    }
}

fn parent_for_part(
    card: &CardInfo,
    property: &str,
    entry_parent: &str,
    role: &str,
) -> Option<String> {
    if role.contains("hair") {
        return Some("cf_J_FaceUp_ty".into());
    }
    if role.contains("acc") {
        if let Some(slot) = accessory_slot_from_property(property) {
            if let Some(a) = card.accessory.get(slot) {
                if !a.parent_key.is_empty() {
                    return Some(resolve_acc_parent(&a.parent_key, entry_parent));
                }
            }
        }
        if !entry_parent.is_empty() && entry_parent != "0" && entry_parent != "null" {
            return Some(entry_parent.to_string());
        }
    }
    None
}

pub(crate) fn apply_parent(mesh: &mut ExtractedMesh, m: &crate::pose::Mat4) {
    apply_parent_geometry(mesh, m);
    crate::pose::rebuild_obj(mesh, mesh.positions.clone());
}

/// Live playback consumes positions/normals; OBJ text belongs to export only.
pub(crate) fn apply_parent_geometry(mesh: &mut ExtractedMesh, m: &crate::pose::Mat4) {
    crate::pose::apply_mat(&mut mesh.positions, m);
    for n in &mut mesh.normals {
        *n = crate::pose::rot_normal(m, *n);
    }
}

#[cfg(test)]
mod live_skin_geometry_tests {
    #[test]
    fn live_geometry_matches_export_without_generating_obj() {
        use super::*;
        let mut rotated=crate::pose::ident();
        rotated[0][0]=0.;rotated[0][1]=-1.;rotated[1][0]=1.;rotated[1][1]=0.;
        rotated[0][3]=2.;rotated[2][3]=-0.3;
        let skeleton=crate::pose::Skeleton {names:vec!["a".into(),"b".into()],
            worlds:vec![crate::pose::ident(),rotated],rest:vec![crate::pose::ident();2],
            smr_count:2,tfs:Default::default(),bone_ids:vec![1,2],source_mesh:0};
        let source=ExtractedMesh {name:"live_geometry_fixture".into(),
            positions:vec![[0.2,0.4,0.7],[1.,-0.5,0.3],[0.;3]],
            rest_positions:vec![[0.2,0.4,0.7],[1.,-0.5,0.3],[0.;3]],
            normals:vec![[0.,1.,0.];3],rest_normals:vec![[0.,1.,0.];3],
            weights:vec![[0.75,0.25,0.,0.],[0.,1.,0.,0.],[0.;4]],
            bone_idx:vec![[0,1,0,0];3],bind_poses:vec![crate::pose::ident();2],
            indices:vec![0,1,2],uvs:vec![[0.,0.];3],..Default::default()};
        let mut exported=source.clone();let mut live=source;
        assert!(skin_mesh(&mut exported,&skeleton));assert!(skin_mesh_geometry(&mut live,&skeleton));
        assert_eq!(live.positions,exported.positions);assert_eq!(live.normals,exported.normals);
        assert!(live.obj.is_empty());assert!(!exported.obj.is_empty());
        apply_parent(&mut exported,&rotated);apply_parent_geometry(&mut live,&rotated);
        assert_eq!(live.positions,exported.positions);assert_eq!(live.normals,exported.normals);
        assert!(live.obj.is_empty());
    }
}

/// `SetParent(parent, worldPositionStays: false)` then optional accessory
/// local TRS: world = parent * local * inv(prefab_root) * skinned_prefab.
fn snap_under_parent(
    mesh: &mut ExtractedMesh,
    parent_world: &crate::pose::Mat4,
    part_skel: Option<&crate::pose::Skeleton>,
    part_local: Option<&crate::pose::Mat4>,
) {
    // ChaControl.UpdateAccessoryMoveFromInfo replaces the prefab `N_move` node's
    // local TRS with the card's addMove, so
    //   meshWorld = parentWorld * N_moveParentWorld * addMove * inv(N_moveWorld)
    // (see `ChaControl.cs:2570` + `ChangeAccessoryParent`). `move_nodes` holds
    // (N_moveParentWorld, N_moveWorld) in prefab space. Without this term the
    // authored N_move transform is left in place, so accessories whose prefab
    // ships a non-identity N_move (scale/rotation) render offset/mirrored.
    let move_node = mesh.move_nodes.first().copied().flatten();
    let mut m = *parent_world;
    if let Some(l) = part_local {
        if let Some((n_parent, n_self)) = move_node {
            m = crate::pose::mat_mul_pub(&m, &n_parent);
            m = crate::pose::mat_mul_pub(&m, l);
            m = crate::pose::mat_mul_pub(&m, &crate::pose::mat_inv_pub(&n_self));
        }
    }
    // Non-skinned accessories (MeshFilter) do not get the mesh GameObject's
    // prefab transform from bind poses, so apply it here or they land at the
    // parent origin (an accessory hair arc becomes a halo above the head).
    if !mesh.uses_skin_renderer() {
        if let Some(p) = mesh.prefab_local.as_ref() {
            m = crate::pose::mat_mul_pub(&m, p);
        }
    }
    if std::env::var("OK_SNAPDIAG").is_ok() && !mesh.positions.is_empty() {
        eprintln!(
            "ok-extract: SNAPDIAG {} before p0={:?} parent={:?} local={:?} m={:?} move0={:?} move1={:?} prefab_local={:?}",
            mesh.name,
            mesh.positions[0],
            crate::pose::mat_pos(parent_world),
            part_local.map(|l| crate::pose::mat_pos(l)),
            crate::pose::mat_pos(&m),
            move_node.map(|(p, s)| (crate::pose::mat_pos(&p), crate::pose::mat_pos(&s))),
            mesh.move_nodes.get(1).copied().flatten().map(|(p, s)| (crate::pose::mat_pos(&p), crate::pose::mat_pos(&s))),
            mesh.prefab_local.as_ref().map(crate::pose::mat_pos)
        );
    }
    if let Some(ps) = part_skel.filter(|_|mesh.uses_skin_renderer()) {
        // With `move_nodes` the N_move inverse already removes the prefab
        // transform; otherwise subtract the skeleton root so we don't double the
        // prefab root transform.
        if move_node.is_none() {
            let root = ps.root_world();
            eprintln!(
                "ok-extract: snap {} root pos={:?} scl={:?}",
                mesh.name,
                crate::pose::mat_pos(&root),
                crate::pose::world_scale_pub(&root)
            );
            m = crate::pose::mat_mul_pub(&m, &crate::pose::mat_inv_pub(&root));
        }
    }
    apply_parent(mesh, &m);
    if std::env::var("OK_SNAPDIAG").is_ok() && !mesh.positions.is_empty() {
        eprintln!(
            "ok-extract: SNAPDIAG {} after p0={:?} m={:?}",
            mesh.name,
            mesh.positions[0],
            crate::pose::mat_pos(&m)
        );
    }
}

/// Body-space bake of `ChangeAccessoryParent` + `UpdateAccessoryMoveFromInfo`.
/// Both `N_move` and `N_move2` locals are replaced, then the mesh is left in
/// body space with `parent_bone` cleared (the viewer must not attach again).
fn snap_accessory(
    mesh: &mut ExtractedMesh,
    parent_world: &crate::pose::Mat4,
    part_skel: Option<&crate::pose::Skeleton>,
    locals: &[crate::pose::Mat4; 2],
) {
    let m = accessory_placement_matrix(mesh, parent_world, part_skel, locals);
    if crate::pose::accdiag_matches(&mesh.name) {
        eprintln!("ok-extract: ACCDIAG snap mesh={} attach_matrix={:?} locals={:?} move_nodes={:?} renderer_prefab={:?}",
            mesh.name, m, locals, mesh.move_nodes, mesh.prefab_local);
    }
    apply_parent(mesh, &m);
    mesh.parent_bone = None;
}

pub(crate) fn accessory_placement_matrix(
    mesh: &ExtractedMesh,
    parent_world: &crate::pose::Mat4,
    part_skel: Option<&crate::pose::Skeleton>,
    locals: &[crate::pose::Mat4; 2],
) -> crate::pose::Mat4 {
    let mut chain = crate::pose::ident();
    let mut any = false;
    for j in 0..2 {
        let Some((n_parent, n_self)) = mesh.move_nodes[j] else {
            continue;
        };
        let mut step = n_parent;
        step = crate::pose::mat_mul_pub(&step, &locals[j]);
        step = crate::pose::mat_mul_pub(&step, &crate::pose::mat_inv_pub(&n_self));
        chain = crate::pose::mat_mul_pub(&chain, &step);
        any = true;
    }
    let mut m = if any {
        crate::pose::mat_mul_pub(parent_world, &chain)
    } else {
        // UpdateAccessoryMoveFromInfo writes only existing objAcsMove[j]
        // objects. A prefab without N_move has no card move to apply.
        *parent_world
    };
    // A bundle may contain a Transform hierarchy even for a MeshFilter.
    // Such a rigid renderer has no skin palette to apply its prefab matrix.
    if !mesh.uses_skin_renderer() {
        if let Some(p) = mesh.prefab_local.as_ref() {
            m = crate::pose::mat_mul_pub(&m, p);
        }
    }
    if mesh.uses_skin_renderer() && part_skel.is_some() && !any {
        if let Some(ps) = part_skel {
            m = crate::pose::mat_mul_pub(&m, &crate::pose::mat_inv_pub(&ps.root_world()));
        }
    }
    // The skinned vertex is already in the head bone's world space. Applying
    // the renderer's transform scale here shrinks it a second time. BakeMesh
    // followed by TransformPoint is a diagnostic export, not the GPU skin path.
    m
}

/// UpdateAccessoryMoveFromInfo writes each Transform independently. A second
/// move node may affect only some SMR bones; one post-skin matrix cannot express
/// that hierarchy (the neck ring and hanging tie share one renderer).
pub(crate) fn accessory_skeleton_with_moves(mesh: &ExtractedMesh,
    part_skel: Option<&crate::pose::Skeleton>, locals: &[crate::pose::Mat4; 2])
    -> Option<crate::pose::Skeleton>
{
    if !mesh.uses_skin_renderer() || !mesh.move_nodes.iter().any(Option::is_some) { return None; }
    let mut skel = mesh.smr_skel.clone().or_else(|| part_skel.cloned())?;
    let mut replacements = Vec::new();
    for j in 0..2 {
        let Some((_, authored_world)) = mesh.move_nodes[j] else { continue; };
        let name = if j == 0 { "N_move" } else { "N_move2" };
        let id = skel.tfs.iter().filter(|(_, tf)|
            tf.name.eq_ignore_ascii_case("N_move") || tf.name.eq_ignore_ascii_case("N_move2"))
            .filter(|(id, _)| {
                let world = crate::pose::world_of_id(**id, &skel.tfs);
                world.iter().flatten().zip(authored_world.iter().flatten())
                    .all(|(a,b)| (a-b).abs() < 1e-5)
            }).min_by_key(|(id,tf)| {
                // Split accessory prefabs can alias their original N_move2 as
                // the instantiated item's first move node. Coincident authored
                // matrices do not establish identity: prefer an actual SMR bone
                // ancestor, then the conventional name for ordinary prefabs.
                let referenced = skel.bone_ids.iter().any(|bone| {
                    let mut current = Some(*bone);
                    for _ in 0..skel.tfs.len() {
                        let Some(node) = current else { break; };
                        if node == **id { return true; }
                        current = skel.tfs.get(&node).and_then(|tf| tf.parent);
                    }
                    false
                });
                (!referenced, !tf.name.eq_ignore_ascii_case(name), **id)
            }).map(|(id,_)| *id)?;
        replacements.push((id,j));
    }
    for (id,j) in replacements { crate::pose::replace_local_trs(skel.tfs.get_mut(&id)?, &locals[j]); }
    skel.rebuild_worlds();
    Some(skel)
}

#[cfg(test)]
#[test]
fn accessory_moves_preserve_independent_skin_branches() {
    use crate::pose::{Skeleton,Tf,ident,world_of_id,local_trs};
    let tf = |name: &str, parent| Tf {name:name.into(),parent,sibling_index:None,
        pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
    for nested in [false,true] {
        let mut tfs=std::collections::HashMap::from([
            (1,tf("root",None)),(2,tf("N_move",Some(1))),
            (3,tf("N_move2",Some(if nested {2}else{1}))),
            (4,tf("fixed",Some(2))),(5,tf("hanging",Some(3)))]);
        tfs.get_mut(&3).unwrap().pos=[0.,7.,0.];
        let mut skel=Skeleton {names:vec!["fixed".into(),"hanging".into()],worlds:vec![],
            rest:vec![ident();2],smr_count:2,tfs,bone_ids:vec![4,5],source_mesh:0};
        skel.rebuild_worlds();
        let mut mesh=ExtractedMesh {renderer_skinned:Some(true),smr_skel:Some(skel.clone()),
            positions:vec![[1.,0.,0.];3],normals:vec![[0.,1.,0.];3],
            weights:vec![[1.,0.,0.,0.],[1.,0.,0.,0.],[0.5,0.5,0.,0.]],
            bone_idx:vec![[0,0,0,0],[1,0,0,0],[0,1,0,0]],bind_poses:vec![ident();2],
            move_nodes:[Some((ident(),world_of_id(2,&skel.tfs))),
                Some((world_of_id(if nested{2}else{1},&skel.tfs),world_of_id(3,&skel.tfs)))],
            ..Default::default()};
        let locals=[local_trs([10.,0.,0.],[0.;3],[2.,1.,1.]),
            local_trs([0.,3.,0.],[0.,0.,90.],[1.;3])];
        let moved=accessory_skeleton_with_moves(&mesh,None,&locals).unwrap();
        assert!(skin_mesh(&mut mesh,&moved));
        let expected=if nested {[[12.,0.,0.],[10.,4.,0.],[11.,2.,0.]]}
            else {[[12.,0.,0.],[0.,4.,0.],[6.,2.,0.]]};
        for (a,b) in mesh.positions.iter().zip(expected) {for i in 0..3 {assert!((a[i]-b[i]).abs()<2e-6);}}
        assert_eq!(moved.rest,skel.rest,"card placement must not rewrite mesh bind/rest data");
    }
}

#[cfg(test)]
#[test]
fn accessory_split_move_alias_uses_actual_skin_ancestor() {
    use crate::pose::{Skeleton,Tf,ident,local_trs};
    let tf = |name:&str,parent| Tf {name:name.into(),parent,sibling_index:None,
        pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
    let tfs=std::collections::HashMap::from([(1,tf("root",None)),
        (2,tf("N_move",Some(1))),(3,tf("N_move2",Some(1))),(4,tf("right",Some(3)))]);
    let mut skel=Skeleton {names:vec!["right".into()],worlds:vec![],rest:vec![ident()],
        smr_count:1,tfs,bone_ids:vec![4],source_mesh:0};skel.rebuild_worlds();
    let mesh=ExtractedMesh {renderer_skinned:Some(true),smr_skel:Some(skel),
        move_nodes:[Some((ident(),ident())),None],..Default::default()};
    let moved=accessory_skeleton_with_moves(&mesh,None,&[local_trs([3.,2.,1.],[0.;3],[1.;3]),ident()]).unwrap();
    assert_eq!(crate::pose::mat_pos(&moved.worlds[0]),[3.,2.,1.]);
    assert_eq!(moved.tfs[&2].pos,[0.;3],"an unrelated coincident node must remain authored");
}

#[test]
fn attachment_chain_preserves_body_shear_and_prefers_live_head() {
    use crate::pose::{Skeleton,Tf,ident,trs,mat_mul_pub};
    let tf=|name:&str,parent,pos,rot,scale|Tf{name:name.into(),parent,sibling_index:None,pos,rot,scale};
    let angle=std::f32::consts::FRAC_PI_8;
    let mut body=Skeleton{names:vec!["a_n_neck".into(),"cf_s_head".into()],worlds:vec![],rest:vec![ident();2],smr_count:2,bone_ids:vec![2,3],source_mesh:0,
        tfs:std::collections::HashMap::from([(1,tf("root",None,[0.;3],[0.,0.,0.,1.],[2.,1.,1.])),(2,tf("a_n_neck",Some(1),[0.,1.,0.],[0.,0.,angle.sin(),angle.cos()],[1.;3])),(3,tf("cf_s_head",Some(2),[0.,2.,0.],[0.,0.,0.,1.],[0.8,1.,1.2])),(4,tf("a_n_headside",Some(1),[99.;3],[0.,0.,0.,1.],[1.;3]))])};body.rebuild_worlds();
    let mut head=Skeleton{names:vec!["p_cf_head_bone".into(),"a_n_headside".into()],worlds:vec![],rest:vec![ident();2],smr_count:2,bone_ids:vec![100,101],source_mesh:0,
        tfs:std::collections::HashMap::from([(100,tf("p_cf_head_bone",None,[0.,10.,0.],[0.,0.,0.,1.],[1.;3])),(101,tf("a_n_headside",Some(100),[0.,0.5,0.],[0.,0.,angle.sin(),angle.cos()],[1.;3]))])};head.rebuild_worlds();
    for name in ["a_n_neck","a_n_headside"] {
        let chain=attachment_transform_chain(Some(&body),Some(&head),name,&ident()).unwrap();
        let actual=chain.iter().fold(ident(),|m,tf|mat_mul_pub(&m,&trs(tf.pos,tf.rot,tf.scale)));
        let expected=bone_world(Some(&body),Some(&head),name).unwrap();
        assert!(actual.iter().flatten().zip(expected.iter().flatten()).all(|(a,b)|(a-b).abs()<1e-6));
        assert!(actual[0][3].abs()<99.,"unparented body copy of head socket must not win");
    }
}

/// `FBSCtrlMouth` `PtnSet` for the mouth target meshes, transcribed from the
/// vanilla head prefab (`bo_head_00.unity3d`, see dump `face_blendshape.json`).
/// For each pattern the pair is `(close, open)` blendshape indices; the mouth
/// blends between them with `FBSCtrlMouth.CalcBlend(openRate)`
/// (`FBSBase.CalculateBlendShape`: close = 100 - open*100, open = open*100).
fn mouth_ptn_pair(mesh_name: &str, ptn: usize) -> Option<(i32, i32)> {
    let n = mesh_name.to_ascii_lowercase();
    let (close, open): (&[i32], &[i32]) = if n.contains("cf_o_face") {
        (&[28, 31, 32, 31, 31], &[29, 30, 33, 34, 35])
    } else if n.contains("cf_o_noseline") {
        (&[25, 28, 29, 28, 28], &[26, 27, 30, 31, 32])
    } else if n.contains("cf_o_tooth") {
        (&[0, 0, 4, 0, 0], &[1, 2, 5, 6, 7])
    } else if n.contains("cf_o_canine") {
        (&[0, 0, 4, 0, 0], &[1, 2, 5, 6, 7])
    } else if n.contains("o_tang") {
        (&[0, 3, 4, 3, 3], &[1, 2, 5, 6, 7])
    } else {
        return None;
    };
    match (close.get(ptn), open.get(ptn)) {
        (Some(&c), Some(&o)) => Some((c, o)),
        _ => None,
    }
}

/// Live CPU mouth baseline is the loaded controller's pattern-0 closed pair;
/// GPU mouth morphs start raw. Channels outside this controller stay unowned.
fn runtime_mouth_channel_weights(channel_count: usize, patterns: &[(i32,i32)], raw_gpu: bool) -> Vec<Option<f32>> {
    let mut weights=vec![None;channel_count];
    for &(close,open) in patterns {
        for index in [close,open] { if index>=0 { if let Some(w)=weights.get_mut(index as usize) { *w=Some(0.0); } } }
    }
    if !raw_gpu {
        if let Some(&(close,_))=patterns.first() {
            if close>=0 { if let Some(w)=weights.get_mut(close as usize) { *w=Some(100.0); } }
        }
    }
    weights
}

#[cfg(test)]
#[test]
fn live_mouth_baseline_uses_authored_controller_indices() {
    // Unknown channel names are immaterial: the controller defines the set.
    // Include reordered channels, a shared close/open and a missing close.
    let patterns=[(3,1),(5,2),(7,7),(-1,0)];
    assert_eq!(runtime_mouth_channel_weights(8,&patterns,false),
        vec![Some(0.),Some(0.),Some(0.),Some(100.),None,Some(0.),None,Some(0.)]);
    assert_eq!(runtime_mouth_channel_weights(8,&patterns,true),
        vec![Some(0.),Some(0.),Some(0.),Some(0.),None,Some(0.),None,Some(0.)]);
    assert_eq!(runtime_mouth_channel_weights(4,&[(2,2)],false),
        vec![None,None,Some(100.),None]);
    assert_eq!(runtime_mouth_channel_weights(3,&[(-1,1),(8,-1)],false),
        vec![None,Some(0.),None]);
    assert_eq!(runtime_mouth_channel_weights(0,&patterns,false),Vec::<Option<f32>>::new());
}

struct HeadFbsInput<'a> {
    mouth: Option<&'a [(i32,i32)]>,
    eyes: Option<&'a [(i32,i32)]>,
    eyebrow: Option<&'a [(i32,i32)]>,
}

/// FBSBase.CalculateBlendShape clears every referenced channel then accumulates
/// the selected pair, including shared Close/Open indices and the -1 sentinel.
fn controller_neutral_weights(channel_count: usize, patterns: &[(i32,i32)], open: f32) -> Vec<Option<f32>> {
    let mut weights = runtime_mouth_channel_weights(channel_count, patterns, true);
    let percent = fbs_integer_open_percent(open);
    if let Some(&(close, opened)) = patterns.first() {
        for (index, value) in [(close, 100-percent), (opened, percent)] {
            if index >= 0 {
                if let Some(Some(weight)) = weights.get_mut(index as usize) {
                    *weight += value as f32;
                }
            }
        }
    }
    weights
}

fn head_neutral_weights(channel_count: usize, input: &HeadFbsInput<'_>, eyes_open: f32, eyebrow_open: f32) -> Vec<Option<f32>> {
    let mut weights = vec![None; channel_count];
    // FaceBlendShape.LateUpdate evaluates Eyebrow before Eyes; a shared channel
    // is overwritten by the later controller, not added across controllers.
    for (patterns, open) in [
        (input.eyebrow, eyebrow_open),
        (input.eyes, eyes_open),
    ] {
        if let Some(patterns) = patterns {
            for (destination, source) in weights.iter_mut().zip(controller_neutral_weights(channel_count, patterns, open)) {
                if source.is_some() { *destination = source; }
            }
        }
    }
    weights
}

#[cfg(test)]
#[test]
fn neutral_controller_indices_support_custom_mod_channels() {
    assert_eq!(controller_neutral_weights(8,&[(3,1),(5,2),(7,7),(-1,0)],0.61337),
        vec![Some(0.),Some(61.),Some(0.),Some(39.),None,Some(0.),None,Some(0.)]);
    assert_eq!(controller_neutral_weights(4,&[(2,2)],0.92),vec![None,None,Some(100.),None]);
    assert_eq!(controller_neutral_weights(3,&[(-1,1)],0.92),vec![None,Some(92.),None]);
    assert_eq!(controller_neutral_weights(3,&[(1,-1)],0.92),vec![None,Some(8.),None]);
    // Geometric effect uses indices, not the renamed channel labels. Channel 1
    // would have been missed entirely by the former _def_op name heuristic.
    let weights = controller_neutral_weights(2,&[(0,1)],0.92);
    let deltas = [[1.,0.,0.],[0.,1.,0.]];
    let mut position=[0.;3];
    for (delta, weight) in deltas.iter().zip(weights) {
        for axis in 0..3 { position[axis] += delta[axis]*weight.unwrap_or(0.)/100.; }
    }
    assert_eq!(position,[0.08,0.92,0.]);
    let input=HeadFbsInput { mouth:None, eyes:Some(&[(0,1)]), eyebrow:Some(&[(1,2)]) };
    assert_eq!(head_neutral_weights(3,&input,0.92,0.61337),vec![Some(8.),Some(92.),Some(61.)]);
}

/// The weight (percent) apply_default_fbs bakes into each channel of a head
/// mesh; 0 where it bakes nothing. The live face subtracts exactly these as its
/// baseline, so a mouth baked open here is not opened a second time.
fn default_fbs_weights(mesh_name: &str, channels: &[&str], authored_fbs: Option<&HeadFbsInput<'_>>) -> Vec<f32> {
    let mut weights = vec![0.0f32; channels.len()];
    if std::env::var("OK_NOFBS").is_ok() || channels.is_empty() {
        return weights;
    }
    // Live GPU path still bakes eye/brow neutral into the bind mesh, but leaves
    // kuti_* for SetBlendShapeWeight. Dump + OK_SKELMESH without OK_GPU_SKIN
    // keeps the old skip.
    let gpu_skin = crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1");
    if crate::profile::var("OK_SKELMESH").ok().as_deref() == Some("1") && !gpu_skin {
        return weights;
    }
    // Head stays CPU-baked unless OK_HEAD_GPUSKIN. Skipping kuti then leaves
    // the mouth at the closed bind pose, because the editor morph path only
    // runs for skel_kind 1/2/3.
    let mouth_on_gpu = gpu_skin && crate::profile::var("OK_HEAD_GPUSKIN").is_ok();
    let authored_mouth = authored_fbs.and_then(|input| input.mouth);
    // Legacy OBJ preview mouth override. Live mouth baselines and head eye/brow
    // baselines below use authored controller indices, including custom names.
    let mouth = mouth_ptn_override().and_then(|ptn| mouth_ptn_pair(mesh_name, ptn));
    let mouth_open = mouth_open_percent();
    // Live mouth setters subtract the authored pattern-0 closed delta from
    // CPU vertices, then add the selected pair. GPU morph meshes stay raw.
    // Use the loaded controller's target indices, including mod channel names.
    let fallback = mouth_ptn_pair(mesh_name,0).map(|pair| vec![pair]);
    let live_weights = if gpu_skin {
        authored_mouth.or(fallback.as_deref()).map(|patterns|
            runtime_mouth_channel_weights(channels.len(),patterns,mouth_on_gpu))
    } else { None };
    let neutral_weights = authored_fbs.map(|input| head_neutral_weights(channels.len(),input,
        f32::from_bits(EYES_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed)),
        f32::from_bits(EYEBROW_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed))));
    for (i, name) in channels.iter().enumerate() {
        let weight = if let Some(weight) = live_weights.as_ref().and_then(|weights| weights[i]) {
            weight
        } else if let Some(weight) = neutral_weights.as_ref().and_then(|weights| weights[i]) {
            weight
        } else if mouth_on_gpu && name.to_ascii_lowercase().contains("kuti") {
            0.0
        } else if let Some((close, open)) = mouth {
            if name.to_ascii_lowercase().contains("kuti") {
                let idx = i as i32;
                if close == open && idx == close {
                    // close/open share a channel: the two weights sum.
                    100.0
                } else if idx == close {
                    100.0 - mouth_open
                } else if idx == open {
                    mouth_open
                } else {
                    0.0
                }
            } else {
                if authored_fbs.is_some() { 0.0 } else { neutral_fbs_weight(name) }
            }
        } else {
            if authored_fbs.is_some() { 0.0 } else { neutral_fbs_weight(name) }
        };
        weights[i] = weight.max(0.0);
    }
    weights
}

fn apply_default_fbs(mesh: &mut ExtractedMesh, authored_fbs: Option<&HeadFbsInput<'_>>) {
    if std::env::var("OK_FBS_DIAG").is_ok() {
        let sample: Vec<_> = mesh
            .blend_shapes
            .iter()
            .filter(|(n, _)| n.contains("kuti") || mesh.name.eq_ignore_ascii_case("cf_Ohitomi_L"))
            .map(|(n, d)| format!("{n}:{}", d.len()))
            .collect();
        eprintln!(
            "ok-extract: FBS {} channels={} sample={:?}",
            mesh.name,
            mesh.blend_shapes.len(),
            sample
        );
    }
    let names: Vec<&str> = mesh.blend_shapes.iter().map(|(name, _)| name.as_str()).collect();
    let weights = default_fbs_weights(&mesh.name, &names, authored_fbs);
    let n = mesh.positions.len();
    let mut touched = 0usize;
    let mut maxd = 0.0f32;
    for ((_, deltas), &w) in mesh.blend_shapes.iter().zip(&weights) {
        if w <= 0.0 {
            continue;
        }
        let f = w / 100.0;
        for (idx, d) in deltas {
            let i = *idx as usize;
            if i < n {
                mesh.positions[i][0] += d[0] * f;
                mesh.positions[i][1] += d[1] * f;
                mesh.positions[i][2] += d[2] * f;
                touched += 1;
                maxd = maxd.max((d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt() * f);
            }
        }
    }
    if std::env::var("OK_FBS_DIAG").is_ok() {
        eprintln!("ok-extract: FBS APPLY {} touched={touched} maxd={maxd:.4}", mesh.name);
    }
}

/// Exact controller OpenMax bits, after ChaControl's separate status/controller
/// clamps. Set before meshes are emitted; names do not determine source channels.
static EYES_OPEN_MAX_BITS: std::sync::atomic::AtomicU32 =
    std::sync::atomic::AtomicU32::new(0.92f32.to_bits());
static EYEBROW_OPEN_MAX_BITS: std::sync::atomic::AtomicU32 =
    std::sync::atomic::AtomicU32::new(1.0f32.to_bits());

/// ChaControl.ChangeEyesOpenMax keeps status in [0,1], but the actual FBS
/// controller's OpenMax is clamped separately to [0,.92]. Do not change the
/// stored card value or confuse this with the blink/gaze correction stage.
fn eye_controller_open_max(status: f32) -> f32 {
    status.clamp(0.0,0.92)
}

fn neutral_fbs_weight(name: &str) -> f32 {
    // FaceBlendShape pattern 0 blends each control between its `_def_cl` and
    // `_def_op` targets; the weights sum to 100. The dump at rest shows:
    //   mouth  (`kuti_*`)      `_cl`=100 `_op`=0   (closed)
    //   eyes   (`siro*`, line) source-clamped OpenMax (default .92)
    //   brows  (`mayuge*`)     `_cl`=0   `_op`=100 (always fully "open")
    let n = name.to_ascii_lowercase();
    let is_mouth = n.contains("kuti");
    let is_brow = n.contains("mayuge") || n.contains("mayu");
    let open = if is_mouth {
        0.0
    } else if is_brow {
        f32::from_bits(EYEBROW_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed))
    } else {
        f32::from_bits(EYES_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed))
    };
    if let Ok(v) = std::env::var("OK_FBS_DEFCL") {
        if n.ends_with("_def_cl") && !is_mouth {
            if let Ok(f) = v.parse::<f32>() {
                return f;
            }
        }
    }
    let percent = fbs_integer_open_percent(open);
    if n.ends_with("_def_cl") {
        (100 - percent) as f32
    } else if n.ends_with("_def_op") {
        percent as f32
    } else {
        0.0
    }
}

fn fbs_integer_open_percent(open: f32) -> i32 {
    (open.clamp(0.0,1.0)*100.0) as i32
}

#[cfg(test)]
#[test]
fn neutral_fbs_integer_percent_matches_native() {
    assert_eq!(fbs_integer_open_percent(0.926),92);
    assert_eq!(fbs_integer_open_percent(0.61337),61);
    assert_eq!(fbs_integer_open_percent(0.00999),0);
    assert_eq!(fbs_integer_open_percent(0.91999),91);
    assert_eq!(fbs_integer_open_percent(1.0),100);
    assert_eq!(fbs_integer_open_percent(-1.0),0);
    assert_eq!(fbs_integer_open_percent(2.0),100);
    // ChangeEyesOpenMax clamps the controller before FBSBase quantization.
    assert_eq!(eye_controller_open_max(1.0),0.92);
    assert_eq!(eye_controller_open_max(2.0),0.92);
    assert_eq!(eye_controller_open_max(-1.0),0.0);
    assert_eq!(eye_controller_open_max(0.61337),0.61337);
    assert_eq!(fbs_integer_open_percent(eye_controller_open_max(1.0)),92);
    assert_eq!(fbs_integer_open_percent(eye_controller_open_max(0.91999)),91);
}

fn rig_mesh(
    mesh: &mut ExtractedMesh,
    role: &str,
    body_skel: Option<&crate::pose::Skeleton>,
    head_skel: Option<&crate::pose::Skeleton>,
    parent_key: Option<&str>,
    part_skel: Option<&crate::pose::Skeleton>,
    part_local: Option<&crate::pose::Mat4>,
    authored_fbs: Option<&HeadFbsInput<'_>>,
    // The accessory's hierarchy moves (begin_acc_place), passed in so the rig is the same on any thread.
    acc_place: Option<[crate::pose::Mat4; 2]>,
) {let _spot=crate::profile::spot("rig_mesh");
    let r = role.to_ascii_lowercase();
    if r == "body" {
        mesh.skel_kind = 1;
        if let Some(sk) = body_skel {
            let _ = skin_mesh(mesh, sk);
        }
        return;
    }
    if r == "mouth" {
        mesh.skel_kind = 1;
        if let (Some(sk), Some(ps)) = (body_skel, part_skel) {
            remap_part_to_body(mesh, sk, ps);
            let _ = skin_mesh(mesh, sk);
        } else if let Some(sk) = body_skel {
            let _ = skin_mesh(mesh, sk);
        }
        return;
    }
    if r.contains("head") || r.contains("face") {
        mesh.skel_kind = 2;
        apply_default_fbs(mesh,authored_fbs);
        // Head detail meshes (eyeline / mayuge / noseline / hitomi ...) each ship
        // their own SMR bone order; remap to the face skeleton by name before
        // skinning, otherwise the wrong bones stretch them across the face.
        if let (Some(sk), Some(ps)) = (head_skel.or(body_skel), part_skel) {
            remap_part_to_body(mesh, sk, ps);
            let _ = skin_mesh(mesh, sk);
        } else if let Some(sk) = head_skel.or(body_skel) {
            let _ = skin_mesh(mesh, sk);
        }
        if std::env::var("OK_MAYUDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("mayuge") {
            let mut left = 0;
            let mut right = 0;
            for (i, p) in mesh.rest_positions.iter().enumerate() {
                if p[0] < 0.0 {
                    left += 1;
                } else {
                    right += 1;
                }
                let _ = i;
            }
            eprintln!(
                "ok-extract: MAYUW counts left(x<0)={left} right={right} weights={} bids={}",
                mesh.weights.len(),
                mesh.bone_idx.len()
            );
            for i in [0usize, 1, 80, 160, 240] {
                if i < mesh.weights.len() {
                    eprintln!(
                        "ok-extract: MAYUW   v{i} rest={:?} bidx={:?} w={:?}",
                        mesh.rest_positions.get(i),
                        mesh.bone_idx.get(i),
                        mesh.weights.get(i)
                    );
                }
            }
            if let Some(sk) = head_skel.or(body_skel) {
                for n in [
                    "cf_J_MayuTip_s_L",
                    "cf_J_MayuMid_s_L",
                    "cf_J_MayuTip_s_R",
                    "cf_J_MayuMid_s_R",
                ] {
                    if let Some(m) = sk.world_tf(n) {
                        let p = crate::pose::mat_pos(&m);
                        eprintln!(
                            "ok-extract: MAYUDIAG {} {} pos=({:.4},{:.4},{:.4}) col0=({:.4},{:.4},{:.4}) col1=({:.4},{:.4},{:.4})",
                            mesh.name, n,
                            p[0], p[1], p[2],
                            m[0][0], m[1][0], m[2][0],
                            m[0][1], m[1][1], m[2][1]
                        );
                    }
                }
            }
        }
        let delta = head_onto_body(body_skel, head_skel);
        if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1")
            && crate::profile::var("OK_HEAD_GPUSKIN").is_err()
        {
            if let Some(sk) = head_skel.or(body_skel) {
                transform_baked_mouth_deltas(mesh, sk, &delta);
            }
        }
        if std::env::var("OK_DIAG").is_ok() {
            let s = crate::pose::world_scale_pub(&delta);
            let rs = head_skel.map(|h| crate::pose::world_scale_pub(&h.root_world())).unwrap_or([0.0; 3]);
            let bs = body_skel
                .and_then(|b| b.world_tf("cf_s_head"))
                .map(|m| crate::pose::world_scale_pub(&m))
                .unwrap_or([0.0; 3]);
            eprintln!("ok-extract: head delta scale={s:?} headroot={rs:?} cf_s_head={bs:?} headroot_pos={:?} cf_s_head_pos={:?} delta_pos={:?}",
                head_skel.map(|h| crate::pose::mat_pos(&h.root_world())),
                body_skel.and_then(|b| b.world_tf("cf_s_head")).map(|m| crate::pose::mat_pos(&m)),
                crate::pose::mat_pos(&delta));
        }
        // `delta` already contains cf_s_head's per-axis scale. Multiplying
        // that scale into the skinned vertices again made the face match the
        // dumper's BakeMesh + renderer.TransformPoint values, which apply
        // renderer scale a second time. KKS's boneWorld * bindpose vertices
        // (the GPU skinning space) are larger; hair already uses that space.
        apply_parent(mesh, &delta);
        // `positions` are already in body space (head skeleton skin + cf_s_head).
        // The kind-2 GPU bake skins the head-local rest pose again and multiplies
        // `head_onto_body` on a shared skeleton that already contains that
        // attach, so the face renders a head-height above the hair. Keep the
        // baked verts, same as hair that failed bone remap.
        if std::env::var("OK_CHINDIAG").is_ok() && mesh.name.eq_ignore_ascii_case("cf_O_face") {
            let mut tot = 0usize;
            for (i, p) in mesh.positions.iter().enumerate() {
                if p[1] < 1.34 && p[1] > 1.28 && p[0].abs() > 0.03 && p[0].abs() < 0.05 && p[2] > -0.01
                {
                    tot += 1;
                    if tot <= 4 {
                        let bname = mesh.bone_idx.get(i).and_then(|bi| {
                            head_skel.and_then(|sk| sk.names.get(bi[3] as usize).map(|s| s.clone()))
                        });
                        eprintln!(
                            "CHINVERT i={i} p={p:?} bi={:?} w={:?} b3={bname:?}",
                            mesh.bone_idx.get(i),
                            mesh.weights.get(i)
                        );
                        if let Some(sk) = head_skel {
                            if let Some(bi) = mesh.bone_idx.get(i) {
                                for b in bi {
                                    let name = sk
                                        .names
                                        .get(*b as usize)
                                        .map(|s| s.as_str())
                                        .unwrap_or("?");
                                    eprintln!("  bone {b} {name}");
                                }
                            }
                        }
                    }
                }
            }
            eprintln!("CHINVERT side_n={tot}");
        }
        if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1")
            && crate::profile::var("OK_HEAD_GPUSKIN").is_err()
        {
            mesh.rest_positions = mesh.positions.clone();
            mesh.rest_normals = mesh.normals.clone();
            mesh.skel_kind = 0;
            mesh.weights.clear();
            mesh.bone_idx.clear();
            mesh.rigid_follow_bone = Some("cf_s_head".into());
        }
        if std::env::var("OK_DIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("hitomi") {
            let mut mn = [f32::MAX; 3];
            let mut mx = [f32::MIN; 3];
            for p in &mesh.positions {
                for k in 0..3 {
                    mn[k] = mn[k].min(p[k]);
                    mx[k] = mx[k].max(p[k]);
                }
            }
            eprintln!(
                "ok-extract: EYEAABB {} x[{:.3},{:.3}] y[{:.3},{:.3}] z[{:.3},{:.3}]",
                mesh.name, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]
            );
        }
        return;
    }
    // Legacy fallback for hair without an owned skinned rig. Normal hair uses
    // AccessoryRuntime::from_hair_source: copyWeights=0 and its private prefab
    // skeleton, attached below the live HairParent without bone-name remapping.
    if r.contains("hair") {
        let has_full_head = match (head_skel, part_skel) {
            (Some(hsk), Some(ps)) => ps
                .names
                .first()
                .map(|root| hsk.index_of(root).is_some())
                .unwrap_or(false),
            _ => false,
        };
        if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1") {
            let owned = mesh.smr_skel.clone().or_else(|| part_skel.cloned());
            if mesh.name.contains("cf_hair_31") {
                let _ = std::fs::write(
                    r"C:\portable\OpenKoikatsu\Saved\hair_gpu.txt",
                    format!(
                        "env=1 head={} smr={} part={} bones={}\n",
                        head_skel.is_some(),
                        mesh.smr_skel.is_some(),
                        part_skel.is_some(),
                        owned.as_ref().map(|s| s.names.len()).unwrap_or(0)
                    ),
                );
            }
            if let (Some(hsk), Some(ps)) = (head_skel, owned.as_ref()) {
                let mapped = ps
                    .names
                    .iter()
                    .filter(|n| hsk.index_of(n).is_some())
                    .count();
                if mapped > 0 {
                    if mesh.rest_positions.len() != mesh.positions.len() {
                        mesh.rest_positions = mesh.positions.clone();
                    }
                    remap_part_to_body(mesh, hsk, ps);
                    mesh.skel_kind = 3;
                    eprintln!(
                        "ok-extract: hair {} gpu-skin head_full mapped={mapped}/{}",
                        mesh.name,
                        ps.names.len()
                    );
                    return;
                }
            }
        }
        if has_full_head {
            let hsk = head_skel.unwrap();
            let ps = part_skel.unwrap();
            remap_part_to_body(mesh, hsk, ps);
            if crate::profile::var("OK_SKELMESH").ok().as_deref() == Some("1") {
                let hung = skin_mesh(mesh, hsk);
                mesh.rest_positions = mesh.positions.clone();
                let delta = head_onto_body(body_skel, Some(hsk));
                apply_parent(mesh, &delta);
                eprintln!("ok-extract: hair {} pmc under head skinned={hung}", mesh.name);
                return;
            }
            let hung = skin_mesh(mesh, hsk);
            mesh.rest_positions = mesh.positions.clone();
            let delta = head_onto_body(body_skel, Some(hsk));
            apply_parent(mesh, &delta);
            eprintln!("ok-extract: hair {} full-head remap skinned={hung}", mesh.name);
            return;
        }
        // Fallback: skin with the hair prefab's own skeleton, then snap under
        // HairParent (cf_J_FaceUp_ty).
        let skel_owned = mesh.smr_skel.clone().or_else(|| part_skel.cloned());
        if let Some(mut ordered) = skel_owned {
            if !mesh.bone_names.is_empty() {
                ordered.set_bone_order(&mesh.bone_names);
            }
            let _ = skin_mesh(mesh, &ordered);
        }
        mesh.rest_positions = mesh.positions.clone();
        if let Some(m) = bone_world(body_skel, head_skel, "cf_J_FaceUp_ty") {
            // Hair prefabs skin against their own (scale 1) skeleton and the
            // game parents them under HairParent. `snap_under_parent` already
            // applies HairParent's world (including the head-shape scale), so an
            // extra world_scale(m) here double-counts it: the T-pose dump
            // (20260914_023857) shows the hair comes out ~0.78x too small with
            // it. `OK_ACCSCALE` re-enables the old behaviour for A/B only.
            if std::env::var("OK_ACCSCALE").is_ok() {
                let d = crate::pose::world_scale_pub(&m);
                if d[0] > 0.0 && d[1] > 0.0 && d[2] > 0.0 {
                    for p in &mut mesh.positions {
                        p[0] *= d[0];
                        p[1] *= d[1];
                        p[2] *= d[2];
                    }
                }
            }
            snap_under_parent(mesh, &m, part_skel, None);
        }
        if mesh.skel_kind != 3 {
            // Bone-name remap failed (head mods, accessory hair with its own
            // skeleton). The skin + snap_under_parent above already left world
            // verts. Collapsing those onto one socket (often a_n_headside)
            // stacks every strand on a single joint. Keep the baked verts.
            // This authored socket also belongs to CPU-source exports: a
            // MeshRenderer without weights still follows ChangeHair's parent.
            mesh.parent_bone = None;
            mesh.rigid_follow_bone = Some("cf_J_FaceUp_ty".into());
            eprintln!("ok-extract: hair {} world-bake (no head-bone remap)", mesh.name);
        }
        return;
    }
    // ChangeAccessoryParent: SetParent(RefObjKey from parentKey, false).
    // Vertices are baked once into body space (parent * both N_move locals).
    // parent_bone stays empty so the viewer does not attach again.
    if r.contains("acc") {
        let key = parent_key.unwrap_or("");
        let hierarchy_moves = acc_place
            .or_else(|| part_local.map(|l| [*l, identity_move()]));
        let moved_skel = hierarchy_moves.as_ref()
            .and_then(|locals| accessory_skeleton_with_moves(mesh, part_skel, locals));
        let moves_in_skeleton = moved_skel.is_some();
        let skel_owned = moved_skel.or_else(|| mesh.smr_skel.clone()).or_else(|| part_skel.cloned())
            .filter(|_|mesh.uses_skin_renderer());
        if let Some(mut ordered) = skel_owned {
            if !mesh.bone_names.is_empty() {
                ordered.set_bone_order(&mesh.bone_names);
            }
            if let (Some(set), Some(parent)) = (
                mesh.deferred_dbones.as_ref(), bone_world(body_skel, head_skel, key)
            ) {
                let locals = acc_place
                    .or_else(|| part_local.map(|l| [*l, identity_move()]));
                if let Some(locals) = locals {
                    let placement = if moves_in_skeleton { parent }
                        else { accessory_placement_matrix(mesh, &parent, part_skel, &locals) };
                    let local=crate::pose::mat_mul_pub(&crate::pose::mat_inv_pub(&parent),&placement);
                    let chain=attachment_transform_chain(body_skel,head_skel,key,&local);
                    let used = if let Some(chain)=chain.filter(|_|std::env::var("OK_DBSTST").is_err()&&std::env::var("OK_DB_MOVE").is_err()) {
                        crate::dbone::hang_set_attached_with_chain(set,&mut ordered,&chain)
                    } else {crate::dbone::hang_set_attached(set, &mut ordered, &placement)};
                    if used > 0 { eprintln!("ok-extract: acc {} attached DynamicBone components={used}", mesh.name); }
                }
            }
            if std::env::var("OK_ACCBONE").is_ok() && crate::pose::accdiag_matches(&mesh.name) {
                let tfs_names: Vec<String> = ordered
                    .tfs
                    .values()
                    .map(|t| t.name.clone())
                    .filter(|n| {
                        n.contains("joint")
                            || n.contains("nc")
                            || n.contains("N_move")
                            || n.contains("slot")
                            || n.contains("miko2")
                            || n.contains("ca_")
                    })
                    .collect();
                eprintln!(
                    "ok-extract: ACCBONE {} smr_bone_names={:?} skel_names={:?} tfs_names={:?}",
                    mesh.name, mesh.bone_names, ordered.names, tfs_names
                );
                for (i, n) in ordered.names.iter().enumerate() {
                    let id = ordered.bone_ids.get(i).copied();
                    let tf = id.and_then(|id| ordered.tfs.get(&id));
                    let w = ordered.worlds.get(i);
                    eprintln!(
                        "ok-extract: ACCBONE {} [{}] {} local_pos={:?} local_rot={:?} local_scl={:?} world={:?}",
                        mesh.name,
                        i,
                        n,
                        tf.map(|t| t.pos),
                        tf.map(|t| t.rot),
                        tf.map(|t| t.scale),
                        w.map(crate::pose::mat_pos)
                    );
                }
            }
            let hung = skin_mesh(mesh, &ordered);
            if crate::pose::accdiag_matches(&mesh.name) {
                let max_bi = mesh
                    .bone_idx
                    .iter()
                    .flat_map(|b| b.iter())
                    .copied()
                    .max()
                    .unwrap_or(0);
                eprintln!(
                    "ok-extract: ACCDIAG {} rest_p0={:?} bind0={:?} skinned_p0={:?} bone_names={} bind_poses={} max_bi={} smr_names={:?}",
                    mesh.name,
                    mesh.rest_positions.first(),
                    mesh.bind_poses.first().map(crate::pose::mat_pos),
                    mesh.positions.first(),
                    mesh.bone_names.len(),
                    mesh.bind_poses.len(),
                    max_bi,
                    mesh.bone_names,
                );
                eprintln!(
                    "ok-extract: ACCW {} bi0={:?} w0={:?} bi1={:?} w1={:?}",
                    mesh.name,
                    mesh.bone_idx.first(),
                    mesh.weights.first(),
                    mesh.bone_idx.get(1),
                    mesh.weights.get(1)
                );
                for (i, w) in ordered.worlds.iter().enumerate() {
                    eprintln!("ok-extract: ACCDIAG {} world{i}={:?}", mesh.name, w);
                }
            }
            eprintln!("ok-extract: acc {} skinned={hung}", mesh.name);
        }
        mesh.rest_positions = mesh.positions.clone();
        mesh.parent_bone = None;
        if let Some(parent) = bone_world(body_skel, head_skel, key) {
            let locals = acc_place.or_else(|| {
                part_local.map(|l| [*l, identity_move()])
            });
            if let Some(locals) = locals {
                if moves_in_skeleton { apply_parent(mesh, &parent); }
                else { snap_accessory(mesh, &parent, part_skel, &locals); }
            } else {
                snap_under_parent(mesh, &parent, part_skel, None);
            }
            mesh.parent_bone = None;
            mesh.rigid_follow_bone = Some(key.to_string());
            // Session.skin_all restores rest buffers before its animation
            // update. The attachment is already baked into world space here.
            // Keeping the pre-placement rest buffer undoes both card move and
            // the renderer transform on the first frame.
            mesh.rest_positions = mesh.positions.clone();
            mesh.rest_normals = mesh.normals.clone();
            eprintln!("ok-extract: acc {} body-bake parent {key}", mesh.name);
            if crate::pose::accdiag_matches(&mesh.name) {
                eprintln!(
                    "ok-extract: ACCDIAG {} parent={} P_pos={:?} final_p0={:?}",
                    mesh.name,
                    key,
                    crate::pose::mat_pos(&parent),
                    mesh.positions.first()
                );
            }
        } else {
            eprintln!("ok-extract: acc {} missing parent '{key}'", mesh.name);
        }
        return;
    }
    if r.starts_with("clothes") {
        mesh.skel_kind = 1;
        // A prefab's sub-meshes can ship different SMR bone lists (e.g. the
        // skirt `o_bot_skirt01_a` has 13 bones, `_b` has 11). Remap each mesh
        // with its OWN SMR skeleton, not the entry-level one, or the second
        // mesh is skinned to the wrong bones and stretches.
        let own = mesh.smr_skel.clone();
        let ps = own.as_ref().or(part_skel);
        if let (Some(sk), Some(ps)) = (body_skel, ps) {
            remap_part_to_body(mesh, sk, ps);
            let _ = skin_mesh(mesh, sk);
        } else if let Some(sk) = body_skel {
            let _ = skin_mesh(mesh, sk);
        }
    }
}

/// Remap a part mesh (clothes / mouth) that ships its own SMR bone list onto the
/// body skeleton by bone name: rewrite vertex bone indices and re-index the part
/// bind poses into body-skeleton order, so the part keeps its own (vanilla) bind
/// instead of the mod body's rest.
fn remap_part_to_body(
    mesh: &mut ExtractedMesh,
    sk: &crate::pose::Skeleton,
    ps: &crate::pose::Skeleton,
) {
    // A part bone that is not itself in the target skeleton (e.g. the iris mesh
    // has only `cf_J_hitomi_tx_L`, a child of `cf_J_Eye_rz_L`) resolves to its
    // nearest ancestor that is. At rest those coincide, so the part is placed
    // correctly instead of being pinned to bone 0 (which crumples it).
    let resolve = |n: &str| -> Option<u16> {
        if let Some(i) = sk.index_of(n) {
            return Some(i as u16);
        }
        let start = ps
            .tfs
            .iter()
            .find(|(_, t)| t.name == n)
            .map(|(id, _)| *id)?;
        let mut cur = ps.tfs.get(&start).and_then(|t| t.parent);
        let mut guard = 0;
        while let Some(p) = cur {
            guard += 1;
            if guard > 64 {
                break;
            }
            let Some(t) = ps.tfs.get(&p) else { break };
            if let Some(i) = sk.index_of(&t.name) {
                return Some(i as u16);
            }
            cur = t.parent;
        }
        None
    };
    let map: Vec<u16> = ps.names.iter().map(|n| resolve(n).unwrap_or(0)).collect();
    if std::env::var("OK_RDIAG").is_ok() {
        eprintln!(
            "RDIAG {} target_bones={} part_bones={}",
            mesh.name,
            sk.names.len(),
            ps.names.len()
        );
        for (ci, n) in ps.names.iter().enumerate() {
            let r = resolve(n);
            let tn = r
                .and_then(|i| sk.names.get(i as usize))
                .map(|x| x.as_str())
                .unwrap_or("<none->0>");
            eprintln!("RDIAG   [{ci}] {n} -> {r:?} {tn}");
        }
    }
    if std::env::var("OK_MAYUDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("mayuge") {
        let dups: Vec<&str> = {
            let mut seen = std::collections::HashSet::new();
            sk.names
                .iter()
                .filter(|n| n.contains("Mayu") && !seen.insert(n.as_str()))
                .map(|s| s.as_str())
                .collect()
        };
        eprintln!(
            "ok-extract: MAYUBIND target_bones={} dup_mayu_names={:?} part_binds={}",
            sk.names.len(),
            dups,
            mesh.bind_poses.len()
        );
        for (ci, n) in ps.names.iter().enumerate() {
            let bi = map[ci];
            let bp = mesh
                .bind_poses
                .get(ci)
                .map(|m| (m[0][3], m[1][3], m[2][3]));
            let wn = sk.names.get(bi as usize).map(|s| s.as_str()).unwrap_or("?");
            eprintln!(
                "ok-extract: MAYUBIND   ci={ci} {n} -> bi={bi} {wn} bindT={bp:?}"
            );
        }
    }
    for q in mesh.bone_idx.iter_mut() {
        for b in q.iter_mut() {
            let i = *b as usize;
            if i < map.len() {
                *b = map[i];
            }
        }
    }
    if !mesh.bind_poses.is_empty() {
        let world_n = sk.worlds.len();
        let mut nb = crate::pose::binds_from_rest(&sk.rest);
        if nb.len() < world_n {
            let last = *nb.last().unwrap_or(&[[0.0f32; 4]; 4]);
            nb.resize(world_n, last);
        }
        if std::env::var("OK_NOPARTBIND").is_err() {
            for (ci, n) in ps.names.iter().enumerate() {
                if let Some(bi) = resolve(n) {
                    if ci < mesh.bind_poses.len() && (bi as usize) < nb.len() {
                        nb[bi as usize] = mesh.bind_poses[ci];
                    }
                }
            }
        }
        mesh.bind_poses = nb;
    } else {
        mesh.bind_poses.clear();
    }
}

pub fn open_session(game_root: &Path, card_path: Option<&Path>, outfit: i32) -> Result<usize, String> {
    let sess = crate::session::open(game_root, card_path, outfit)?;
    Ok(sess.parts.len())
}
/// Builds the per-process memos every card open needs (lists, mods catalog, GUID index, StripAll migrations,
/// UncensorSelector and MaterialEditor manifests, toon ramp, H pair metadata) for a game folder or OK base library,
/// so the first card a reader opens costs what later ones do. The mods-tree scans run in parallel. Safe to call
/// from a background thread while nothing else is open; it mounts no card pack.
pub fn prewarm(game_root: &Path) -> Result<(), String> {
    let t = Instant::now();
    crate::oklib::ensure_library(game_root)?;
    let mods_dir = game_root.join("mods");
    std::thread::scope(|scope| {
        scope.spawn(|| { let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom")); let _ = configured_ramp_png(game_root, &vanilla); });
        scope.spawn(|| { let _ = crate::list::load_mods_catalog_shared(&mods_dir); });
        scope.spawn(|| { let _ = crate::sqlcache::guid_index_shared(&mods_dir); });
        scope.spawn(|| { let _ = crate::mods::strip_all_guids(&mods_dir); });
        scope.spawn(|| { let _ = crate::mods::uncensor_sources(game_root); });
        scope.spawn(|| { let _ = me_manifest_sections(game_root); });
        scope.spawn(|| { let _ = crate::h_playback::pairs(game_root); });
        // Original KKS cards open from the library plus the installation's zipmods they name (OK_GAME_ROOT).
        if let Some(game) = std::env::var_os("OK_GAME_ROOT").map(PathBuf::from).filter(|g| g.join("mods").is_dir()) {
            scope.spawn(move || crate::mods::prewarm_guid_index(&game.join("mods")));
        }
    });
    eprintln!("ok-extract: prewarm {} in {:.2}s", game_root.display(), t.elapsed().as_secs_f64());
    Ok(())
}
/// `open_session` with explicit clothes states (`fileStatus.clothesState`, 0 worn .. 3 off), as the reader's
/// clothing variants open.
pub fn open_session_states(game_root: &Path, card_path: Option<&Path>, outfit: i32, states: [u8; 9]) -> Result<usize, String> {
    let _scope = crate::unity::request_clothes_states(states);
    open_session(game_root, card_path, outfit)
}
/// Opens a card the way the reader's `ok_session_open` does with a base library configured: an original KKS card from
/// the library plus its zipmods (`game`, or OK_GAME_ROOT when `game` has no mods/), an OK card from OK data only.
/// Fills the same open-cache entry the reader looks up.
pub fn open_card_like_reader(lib: &Path, game: &Path, card: &Path, outfit: i32, states: [u8; 9]) -> Result<usize, String> {
    let _scope = crate::unity::request_clothes_states(states);
    if let Some(result) = crate::oklib::open_original_card(game, card, outfit) { return result.map(|s| s.parts.len()); }
    open_session(lib, Some(card), outfit)
}
/// Between independent card opens of a batch process (ok-card-warm): every thread drops the bundles and memos it
/// parsed for the previous card (`unity::release_bundle_caches`).
pub fn release_open_caches() { crate::unity::release_bundle_caches(); }
/// Regression check for open-path optimisations: (freshly built session bytes, the open cache's entry for the same
/// key if any). Opens like the reader without reading or writing the cache.
pub fn open_card_compare_cache(lib: &Path, card: &Path, outfit: i32) -> Result<(Vec<u8>, Option<Vec<u8>>), String> {
    let key = crate::oklib::session_cache_key(lib, card, outfit);
    // The entry as stored ("OKC2", fallback identity, lz4), whatever fallback the process that built it had.
    let cached = key.as_deref().and_then(|k| {
        let bytes = std::fs::read(crate::oklib::session_cache_location()?.join(format!("{k}.okpreview"))).ok()?;
        if bytes.len() < 8 || &bytes[..4] != b"OKC2" { return None; }
        let n = u32::from_le_bytes(bytes[4..8].try_into().ok()?) as usize;
        lz4_flex::block::decompress_size_prepended(bytes.get(8 + n..)?).ok()
    });
    std::env::set_var("OK_NOCACHE", "1");
    let fresh = match crate::oklib::open_original_card(Path::new(""), card, outfit) {
        Some(result) => result,
        None => crate::session::open(lib, Some(card), outfit),
    };
    std::env::remove_var("OK_NOCACHE");
    Ok((fresh?.to_preview_bytes()?, cached))
}
/// A fresh open of (card, outfit) as a baked entry for the card's `okBk` chunk (ok-card-bake): the reader's open-cache
/// key, the fallback identity when installed zipmods were read, and the preview bytes. Neither reads nor writes the
/// open cache.
pub fn bake_card_entry(lib: &Path, card: &Path, outfit: i32) -> Result<crate::cardbake::Entry, String> {
    Ok(bake_card(lib, card, outfit, 0)?.0)
}
/// `bake_card_entry`, and with `memo_kinds` (memosnap kinds) also the memo results the open used, for `okMm`.
pub fn bake_card(lib: &Path, card: &Path, outfit: i32, memo_kinds: u8) -> Result<(crate::cardbake::Entry, Option<crate::cardbake::Entry>), String> {
    let key = crate::oklib::session_cache_key(lib, card, outfit).ok_or("the card has no open-cache key")?;
    std::env::set_var("OK_NOCACHE", "1");
    crate::gamefs::reset_fallback_used();
    if memo_kinds != 0 { crate::memosnap::begin_record(memo_kinds); }
    let opened = match crate::oklib::open_original_card(Path::new(""), card, outfit) {
        Some(result) => result,
        None => crate::session::open(lib, Some(card), outfit),
    };
    let memo = if memo_kinds != 0 { Some(crate::memosnap::end_record()?) } else { None };
    std::env::remove_var("OK_NOCACHE");
    let fallback = if crate::gamefs::fallback_used() { crate::gamefs::fallback_identity() } else { String::new() };
    let open = crate::cardbake::Entry { key: key.clone(), fallback: fallback.clone(), preview: opened?.to_preview_bytes()? };
    Ok((open, memo.map(|preview| crate::cardbake::Entry { key, fallback, preview })))
}
/// `oklib_session_cache_key` for explicit clothes states.
pub fn oklib_session_cache_key_states(lib: &Path, card: &Path, outfit: i32, states: [u8; 9]) -> Option<String> {
    let _scope = crate::unity::request_clothes_states(states);
    crate::oklib::session_cache_key(lib, card, outfit)
}
/// The open-cache key of an OK card under a base library (None: not cacheable).
pub fn oklib_session_cache_key(lib: &Path, card: &Path, outfit: i32) -> Option<String> {
    crate::oklib::session_cache_key(lib, card, outfit)
}
/// Whether the open cache holds an entry for this key (it may still be rejected on load if its zipmods changed).
pub fn oklib_session_cached(key: &str) -> bool { crate::oklib::session_cache_has(key) }
/// Whether the last library open was served by the open cache.
pub fn oklib_last_from_cache() -> bool { crate::oklib::LAST_FROM_CACHE.load(std::sync::atomic::Ordering::Relaxed) }

pub fn extract_card(game_root: &Path, card_path: Option<&Path>, out_dir: &Path) -> Result<PathBuf, String> {
    extract_card_outfit(game_root, card_path, out_dir, 0)
}

pub fn extract_card_outfit(
    game_root: &Path,
    card_path: Option<&Path>,
    out_dir: &Path,
    outfit: i32,
) -> Result<PathBuf, String> {
    // A prior normal export cannot satisfy an opt-in material-input audit.
    // Re-emit from source so diagnostics never silently return a bare manifest.
    if std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() != Ok("1")
        && std::env::var("OK_DUMP_HAIR_INPUTS").as_deref() != Ok("1")
        && stamp_fresh(game_root, out_dir, card_path, outfit) {
        eprintln!(
            "ok-extract: cache hit {}, skip reparse",
            out_dir.display()
        );
        return Ok(out_dir.join("manifest.json"));
    }
    let _ = extract_session(game_root, card_path, outfit, Some(out_dir))?;
    Ok(out_dir.join("manifest.json"))
}

/// Opaque and alpha-tested parts that draw the very same surface (a Material Editor copy on the renderer, a second
/// accessory slot with the same item, a hair renderer's extra material). Unity draws them in order with ZTest LEqual,
/// so the later material wins every equal-depth pixel: a decal layer (straps, cuffs) clipped by its alpha sits on the
/// layer below. Each part gets its place in such a group, in session order (the draw order); Unreal draws the group in
/// that order (`r.OK.OpaqueSortPriority`, engine patch OpaqueSortPriority), so equal depths resolve the same way.
/// Translucent parts keep layer 0: they are ordered by their own sort priority.
pub(crate) fn assign_draw_layers(parts: &mut [crate::session::SessionPart]) {
    let mut layers: std::collections::HashMap<(u32, u32, usize, usize), u16> = std::collections::HashMap::new();
    for part in parts.iter_mut() {
        part.draw_layer = 0;
        if !matches!(part.blend.as_str(), "opaque" | "masked") || part.rest_positions.is_empty() { continue; }
        let mut positions = crc32fast::Hasher::new();
        for v in &part.rest_positions { for x in v { positions.update(&x.to_bits().to_le_bytes()); } }
        let mut indices = crc32fast::Hasher::new();
        for i in &part.indices { indices.update(&i.to_le_bytes()); }
        let key = (positions.finalize(), indices.finalize(), part.rest_positions.len(), part.indices.len());
        let layer = layers.entry(key).or_insert(0);
        part.draw_layer = *layer;
        *layer = layer.saturating_add(1);
    }
}

#[cfg(test)]
#[test]
fn coincident_layers_take_draw_order() {
    let mesh = |name: &str| ExtractedMesh {
        name: name.into(),
        positions: vec![[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]],
        normals: vec![[0., 0., 1.]; 3],
        uvs: vec![[0., 0.], [1., 0.], [0., 1.]],
        indices: vec![0, 1, 2],
        mat: Some(crate::unity::MeshMaterial { shader: "Shader Forge/main_item".into(), ..Default::default() }),
        ..Default::default()
    };
    let mut other = mesh("other");
    other.positions[2] = [0., 2., 0.];
    let mut parts = preview_session_parts(Path::new(""), "accessory", vec![mesh("base"), mesh("copy"), other, mesh("again")], None).unwrap();
    for p in &mut parts { p.blend = "masked".into(); }
    let mut glass = parts[0].clone();
    glass.blend = "translucent".into();
    parts.push(glass);
    let before: Vec<_> = parts.iter().map(|p| p.rest_positions.clone()).collect();
    assign_draw_layers(&mut parts);
    assert_eq!(parts.iter().map(|p| p.draw_layer).collect::<Vec<_>>(), [0, 1, 0, 2, 0]);
    assert!(parts.iter().zip(&before).all(|(p, b)| &p.rest_positions == b), "geometry stays as the source has it");
    assign_draw_layers(&mut parts);
    assert_eq!(parts.iter().map(|p| p.draw_layer).collect::<Vec<_>>(), [0, 1, 0, 2, 0], "recomputing is stable");
}

pub(crate) fn extract_session(
    game_root: &Path,
    card_path: Option<&Path>,
    outfit: i32,
    out_dir: Option<&Path>,
) -> Result<crate::session::OkSession, String> {
    if !game_root.join("abdata").ok_is_dir() {
        return Err(format!("game_root missing abdata: {}", game_root.display()));
    }
    // Reject an invalid or missing card before touching an existing export.
    // Otherwise a mistyped path silently becomes the default character and
    // may also erase the previous card's OBJ/PNG files.
    let parsed_card: Option<CardInfo> = card_path
        .map(|path| parse_card_outfit(path, outfit)
            .map_err(|e| format!("parse card {}: {e}", path.display())))
        .transpose()?;
    let t = Instant::now();
    let session = extract_session_from_info(game_root,card_path,outfit,out_dir,parsed_card);
    profile_tick("extract_session TOTAL", t);
    session
}

/// Runtime APNG editing already owns the exact current card metadata. Reuse
/// the same extraction pipeline without saving a temporary source PNG or
/// resolving a stale filename/SQL card cache. The path is report provenance
/// only; neither extraction nor accessory context creation depends on it.
pub(crate) fn extract_session_from_info(
    game_root:&Path,card_path:Option<&Path>,outfit:i32,out_dir:Option<&Path>,
    mut parsed_card:Option<CardInfo>,
)->Result<crate::session::OkSession,String>{
    if !game_root.join("abdata").ok_is_dir(){
        return Err(format!("game_root missing abdata: {}",game_root.display()));
    }
    if let Some(card)=parsed_card.as_mut(){crate::mods::apply_strip_all_migrations(game_root,card);}
    if game_root.join("require_complete_mods").ok_is_file() {
        if let Some(card)=parsed_card.as_ref() {
            let wanted:std::collections::HashSet<String>=card.resolves.iter().map(|r|r.mod_id.clone()).filter(|g|!g.is_empty()).collect();
            let found=crate::mods::find_guids(&game_root.join("mods"),Some(&wanted));
            let mut missing:Vec<_>=wanted.into_iter().filter(|g|!found.contains_key(g)).collect();missing.sort();
            if !missing.is_empty(){return Err(format!("missing adjacent zipmod GUIDs: {}",missing.join(", ")));}
            // Body descriptor GUIDs belong to the selector registry, not the
            // package manifest registry. Use the same resolver as extraction.
            if let Some(g)=card.body_guid.as_deref().filter(|g|!g.is_empty()&&!matches!(*g,"Default.Body.Female"|"Default.Body.Male")) {
                if crate::mods::uncensor_body_for_card(game_root,Some(g),card.sex).is_none() {
                    return Err(format!("missing adjacent zipmod body descriptor: {g}"));
                }
            }
        }
    }
    // Within one view (a card pack mounted: a conversion, or an OK card opened again) the entries answer the same, so
    // what an earlier open of it extracted is kept: a conversion opens every outfit, and outfits 1.. extracted outfit
    // 0's accessories again. Their reads were recorded by that first open into the same pack. Any other open starts
    // afresh (begin_load still clears on a changed reference signature).
    let view = crate::oklib::mounted_pack_identity();
    let epoch = format!("{}|{view}|{}|{}", game_root.display(), crate::oklib::mounted_pack_mods_identity(), crate::dbone::dynamic_bones_fix_enabled(game_root));
    let previous = ENTRY_EPOCH.with(|e| e.replace(epoch.clone()));
    if view.is_empty() || previous != epoch { clear_entry_memo(); }
    crate::unity::references::begin_load(game_root);
    let t_all = Instant::now();
    // OK_PROFILE: the per-slot stage timings (appearance::timings) of this open too.
    let stages = std::env::var_os("OK_PROFILE").map(|_| crate::appearance::timings::Capture::begin());
    let mut t = Instant::now();
    if let Some(dir) = out_dir {
        std::fs::create_dir_all(dir).map_err(|e| format!("mkdir: {e}"))?;
        if let Ok(rd) = crate::gamefs::read_dir(dir) {
            for entry in rd.flatten() {
                let p = entry.path();
                if let Some(ext) = p.extension().and_then(|e| e.to_str()) {
                    if matches!(ext, "obj" | "png" | "json") {
                        let _ = std::fs::remove_file(p);
                    }
                }
            }
        }
    }
    let mut session_parts: Vec<crate::session::SessionPart> = Vec::new();
    let mut manifest = Manifest::default();
    manifest.clothes_states = crate::unity::clothes_states();
    manifest.shoes_type = crate::unity::requested_shoes_type();
    let mut parts: Vec<ManifestPart> = Vec::new();
    let mut skin_tint = [0.78, 0.56, 0.46, 1.0];
    let mut skin_sub = skin_tint;
    profile_tick("parse_card", t);
    t = Instant::now();
    EYES_OPEN_MAX_BITS.store(
        eye_controller_open_max(parsed_card.as_ref().map_or(1.0,|c| c.eyes_open_max)).to_bits(),
        std::sync::atomic::Ordering::Relaxed,
    );
    EYEBROW_OPEN_MAX_BITS.store(
        parsed_card.as_ref().map_or(1.0,|c| c.eyebrow_open_max).clamp(0.0,1.0).to_bits(),
        std::sync::atomic::Ordering::Relaxed,
    );
    if let Some(c) = parsed_card.as_ref() {
        skin_tint = c.skin_color;
        skin_sub = c.skin_sub_color;
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!("ok-extract: card eyesOpenMax={}", c.eyes_open_max);
        }
    }

    let t_sub = Instant::now();
    let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom"));
    profile_tick("lists: vanilla", t_sub);
    let t_sub = Instant::now();
    let ramp_png = configured_ramp_png(game_root, &vanilla);
    profile_tick("lists: ramp", t_sub);
    if std::env::var("OK_DUMP_HAIR_INPUTS").as_deref() == Ok("1")
        || std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() == Ok("1") {
        if let (Some(dir), Some(bytes)) = (out_dir, ramp_png.as_deref()) {
            std::fs::write(dir.join("toon-ramp.png"), &*crate::teximg::as_png(bytes)).map_err(|e| e.to_string())?;
        }
    }
    eprintln!("ok-extract: global toon ramp bytes={}", ramp_png.as_ref().map_or(0, Vec::len));
    let mods_dir = game_root.join("mods");
    let t_sub = Instant::now();
    let mod_catalog = crate::list::load_mods_catalog_shared(&mods_dir);
    let mod_catalog = &*mod_catalog;
    profile_tick("lists: mods catalog", t_sub);
    let t_sub = Instant::now();
    let guid_index: std::collections::HashMap<String, PathBuf> = parsed_card
        .as_ref()
        .map(|c| {
            if c.resolves.is_empty() {
                Default::default()
            } else {
                let wanted: std::collections::HashSet<String> =
                    c.resolves.iter().map(|r| r.mod_id.clone()).collect();
                crate::mods::find_guids(&mods_dir, Some(&wanted))
            }
        })
        .unwrap_or_default();
    // Resolve the list entries before creating body masks or reading prefab
    // variants. The game derives state availability and linked clothing from
    // these entries before DrawClothes, including for clothes later hidden.
    profile_tick("lists: guids", t_sub);
    let t_sub = Instant::now();
    let selected_clothes = parsed_card.as_ref().map(|card|
        select_card_clothes(card, outfit, &vanilla, &mod_catalog, &guid_index));
    profile_tick("lists: select clothes", t_sub);
    let selected_top_type: i32 = selected_clothes.as_ref()
        .and_then(|slots| slots[0].as_ref())
        .and_then(|(entry, _, _)| entry.info("Kind").parse().ok()).unwrap_or(0);
    let selected_top_subparts = parsed_card.as_ref().map(|card|
        select_card_top_subparts(card, outfit, selected_top_type,
            &vanilla, &mod_catalog, &guid_index));
    let mut state_types = std::array::from_fn(|kind| selected_clothes.as_ref()
        .and_then(|slots| slots[kind].as_ref())
        .and_then(|(entry, _, _)| entry.info("StateType").trim().parse::<u8>().ok()));
    let linked_not_bot = selected_clothes.as_ref().and_then(|slots| slots[0].as_ref())
        .is_some_and(|(entry, _, _)| entry.info("Coordinate").trim() == "2");
    let linked_not_shorts = selected_clothes.as_ref().and_then(|slots| slots[2].as_ref())
        .is_some_and(|(entry, _, _)| entry.info("Coordinate").trim() == "2");
    let requested_states = crate::unity::requested_clothes_states();
    let mut reference_flags = [false; 4];
    if requested_states != [0; 9] {
        if let Some(slots) = selected_clothes.as_ref() {
            reference_flags = selected_clothes_reference_flags(game_root, slots);
            let top_not_bra = slots[0].as_ref()
                .is_some_and(|(entry, _, _)| entry.info("NotBra").trim() == "1");
            let sub_not_bra: [Option<bool>; 2] = std::array::from_fn(|kind|
                selected_top_subparts.as_ref()
                    .and_then(|sub| sub[kind].as_ref())
                    .and_then(|(_, entry, _, _)|
                        (!is_dummy_part(&entry.main_data, Some(&entry.name)))
                            .then(|| entry.info("NotBra").trim() == "1")));
            let not_bra = match selected_top_type {
                1 => sub_not_bra[0].unwrap_or(top_not_bra),
                2 if sub_not_bra.iter().any(Option::is_some) =>
                    sub_not_bra.iter().any(|value| *value == Some(true)),
                _ => top_not_bra,
            };
            state_types = adjust_clothes_state_types(
                state_types, reference_flags, not_bra, linked_not_shorts);
        }
    }
    let resolved_states = resolve_clothes_states(
        requested_states, state_types,
        linked_not_bot, linked_not_shorts);
    let _clothes_scope = crate::unity::set_session_clothes_states(resolved_states);
    manifest.clothes_states = resolved_states;
    // Whatever way this extraction ends (an error, a preempted background open), its helpers finish before it
    // returns, while its card pack is still mounted.
    let _prefetch_guard = PrefetchGuard;
    // Predict the hair/accessory (KKEx resolves) and clothing items the item passes extract, with the same
    // filters, lookups and shake switches, and start extracting them on helper threads now (`prefetch_entries`),
    // so they run while this thread builds the body, face and uncensor parts.
    if let Some(card) = parsed_card.as_ref() {
        let want_outfit = outfit_prefix(outfit);
        let mut items: Vec<(Option<PathBuf>, ListEntry, bool)> = Vec::new();
        let mut lists: std::collections::HashMap<String, ChaListCatalog> = std::collections::HashMap::new();
        let mut seen = std::collections::HashSet::new();
        for resolve in &card.resolves {
            if !is_mesh_category(resolve.category_no) { continue; }
            let p = resolve.property.to_ascii_lowercase();
            if p.contains("outfit") && !p.contains(&want_outfit) { continue; }
            let role = role_for_category(resolve.category_no, &resolve.property);
            if role.starts_with("clothes") || is_dummy_part("", Some(&resolve.name)) { continue; }
            if !seen.insert(format!("{}:{}:{}", resolve.category_no, resolve.slot, resolve.property)) { continue; }
            let zip_path = guid_path(&guid_index, &resolve.mod_id);
            let Some(entry) = lookup_entry(&vanilla, &mut lists, zip_path.map(|p| p.as_path()), &resolve.mod_id,
                resolve.category_no, resolve.slot, resolve.local_slot) else { continue };
            let shake = if role.starts_with("acc") {
                accessory_slot_from_property(&resolve.property).and_then(|s| card.accessory.get(s)).is_none_or(|p| !p.no_shake)
            } else {
                !(101..=104).contains(&resolve.category_no) || !card.hair_parts[(resolve.category_no - 101) as usize].no_shake
            };
            items.push((zip_path.cloned(), entry, shake));
        }
        if let Some(slots) = selected_clothes.as_ref() {
            for (kind, slot) in slots.iter().enumerate() {
                let Some((entry, _, zip_path)) = slot.as_ref() else { continue };
                if !crate::unity::clothes_kind_visible(kind, &manifest.clothes_states, manifest.shoes_type)
                    || is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
                let zip = entry.extra.get("__zip").map(PathBuf::from).or_else(|| zip_path.clone());
                items.push((zip, entry.clone(), true));
            }
        }
        // Hair slots (ChaFileHair.parts), as the hair-slot pass looks them up.
        for (i, &hid) in card.hair_ids.iter().enumerate() {
            if hid == 0 { continue; }
            let cat = 101 + i as i32;
            let resolve = card.resolves.iter().find(|r| (r.slot == hid || r.local_slot == hid) && (r.category_no == cat || (101..=104).contains(&r.category_no)));
            let (guid, zip_path) = resolve.map_or((String::new(), None), |r| (r.mod_id.clone(), guid_path(&guid_index, &r.mod_id).cloned()));
            let Some(entry) = lookup_entry(&vanilla, &mut lists, zip_path.as_deref(), &guid, cat, hid, hid) else { continue };
            if is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
            items.push((zip_path, entry, !card.hair_parts[i].no_shake));
        }
        // Accessory slots without a KKEx resolve (the ChaFileAccessory.parts pass): vanilla id, else the mod list.
        let resolved_slots: std::collections::HashSet<usize> = card.resolves.iter()
            .filter(|r| is_mesh_category(r.category_no) && role_for_category(r.category_no, &r.property).starts_with("acc"))
            .filter(|r| { let p = r.property.to_ascii_lowercase(); !p.contains("outfit") || p.contains(&want_outfit) })
            .filter_map(|r| accessory_slot_from_property(&r.property)).collect();
        for (slot_i, a) in card.accessory.iter().enumerate() {
            if a.type_id == 120 || !is_mesh_category(a.type_id) || resolved_slots.contains(&slot_i) { continue; }
            if !role_for_category(a.type_id, "").starts_with("acc") { continue; }
            let Some(entry) = vanilla.get(a.type_id, a.id).or_else(|| mod_catalog.get(a.type_id, a.id)).cloned() else { continue };
            if is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
            let zip = entry.extra.get("__zip").map(PathBuf::from);
            items.push((zip, entry, !a.no_shake));
        }
        // The top's sailor/jacket subparts (the subclothes pass).
        if manifest.clothes_states[0] != 3 {
            for (_, entry, _, zip_path) in selected_top_subparts.iter().flatten().flatten() {
                if is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
                let zip = entry.extra.get("__zip").map(PathBuf::from).or_else(|| zip_path.clone());
                items.push((zip, entry.clone(), true));
            }
        }
        prefetch_entries(game_root, items);
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!("ok-extract: clothes requested={requested_states:?} resolved={resolved_states:?} types={state_types:?} refs={reference_flags:?} notBot={linked_not_bot} notShorts={linked_not_shorts}");
    }
    let selected_hair_gloss = parsed_card.as_ref()
        .and_then(|card| selected_hair_gloss_texture(game_root, card, &vanilla, &mod_catalog, &guid_index));
    profile_tick("lists+mods+guids", t);
    t = Instant::now();
    let body_bundle = game_root.join("abdata/chara/oo_base.unity3d");
    // ChaControl.InitBaseCustomTextureBody loads the draw material from
    // mm_base, independently of the body mesh bundle (ChaControl.cs:10709).
    let skin_material_bundle = crate::gamefs::read_source(game_root.join("abdata/chara/mm_base.unity3d"))
        .ok().map(crate::unity::parse_bundle_cached);
    let vanilla_head_bundle = game_root.join("abdata/chara/bo_head_00.unity3d");
    let mut head_bundle = vanilla_head_bundle.clone();
    // Sideloader lets mods replace vanilla assetbundles. `[DeathWeasel][KKS]
    // Face Fixes` ships `chara/bo_head_00.unity3d` with the fixed face/eye
    // meshes, skeleton and shape animation the game actually loads. Opt in with
    // OK_FACE_FIXES so the vanilla pipeline stays available for comparison.
    if std::env::var("OK_FACE_FIXES").is_ok() {
        if let Some(b) = crate::mods::override_bundle(&mods_dir, "chara/bo_head_00.unity3d") {
            let tmp = std::env::temp_dir().join("ok_bo_head_00.unity3d");
            if std::fs::write(&tmp, &b).is_ok() {
                eprintln!("ok-extract: FACE_FIXES override bytes={} -> {}", b.len(), tmp.display());
                head_bundle = tmp;
            }
        } else {
            eprintln!("ok-extract: FACE_FIXES requested but no override found");
        }
    }
    let t_body = Instant::now();
    let (mut body_meshes, mut body_skel, mut body_texs) = load_base_bundle(&body_bundle);
    profile_tick("load_base_bundle body", t_body);
    // StudioEditor builds the head from the armature prefab `p_cf_head_bone`
    // (in the base bundle) parented under `cf_s_head`, not from the head mesh's
    // own SMR skeleton (KoikatsuCharacterAssembler.cs:237-274). That armature
    // has the full head bone tree (97 bones, scale 1) so the head/hair scale is
    // inherited from `cf_s_head` alone.
    let body_bundle_bytes = crate::gamefs::read_source(&body_bundle).unwrap_or_default();
    let parsed_body = crate::unity::parse_bundle_cached(body_bundle_bytes.clone());
    parsed_body.configure_game_references(game_root);
    // ChaControl.Load: sexFemale selects p_cf_body_00 or p_cm_body_00.
    // Filtering the requested prefab prevents a largest-mesh female fallback.
    if parsed_card.as_ref().is_some_and(|card|card.sex==0) {
        let (male_meshes,_)=parsed_body.meshes_for_maindata("p_cm_body_00");
        if !male_meshes.iter().any(|m|m.name.eq_ignore_ascii_case("o_body_a")) {
            return Err("male body prefab p_cm_body_00 has no o_body_a".into());
        }
        body_skel=male_meshes.iter().find(|m|m.name.eq_ignore_ascii_case("o_body_a"))
            .and_then(|m|m.smr_skel.clone());
        if body_skel.is_none() {return Err("male body renderer has no source skeleton".into());}
        body_meshes=male_meshes;
        eprintln!("ok-extract: selected male body prefab p_cm_body_00");
    }
    if let (Some(body), Some(bone_prefab)) = (
        body_skel.as_mut(), parsed_body.skeleton_for_prefab_name("p_cf_body_bone"),
    ) {
        let added = body.merge_missing_tree_transforms(&bone_prefab);
        if added > 0 {
            eprintln!("ok-extract: body collider tree merged {added} transforms");
        }
    }
    // ChaControl.cs:8085-8104: objHeadBone = `p_cf_head_bone` loaded from
    // `studio/base/00.unity3d` (not oo_base), parented under cf_s_head.
    let head_bone_bundle = game_root.join("abdata/studio/base/00.unity3d");
    let parsed_head_rig=crate::mods::override_bundle(&game_root.join("mods"),"studio/base/00.unity3d").map(crate::unity::SourceBytes::from)
        .or_else(||crate::gamefs::read_source(&head_bone_bundle).ok()).map(crate::unity::parse_bundle_cached);
    let prefab_head_skel = parsed_head_rig.as_ref()
        .and_then(|rig|rig.skeleton_for_prefab_name("p_cf_head_bone"))
        .or_else(|| parsed_body.skeleton_for_prefab_name("p_cf_head_bone"));
    let pv_config=parsed_head_rig.as_ref().and_then(|rig|rig.studio_pv_config("p_cf_body_bone"));
    if let (Some(body),Some(rig))=(&mut body_skel,&parsed_head_rig) {
        if let Some(source)=rig.skeleton_for_prefab_name("p_cf_body_bone") {body.merge_missing_tree_transforms(&source);}
    }
    let head_has_eye_controller=parsed_head_rig.as_ref().map_or_else(
        ||parsed_body.prefab_has_component_field("p_cf_head_bone","eyeLookScript"),
        |rig|rig.prefab_has_component_field("p_cf_head_bone","eyeLookScript"));
    let mut eye_look_config=parsed_head_rig.as_ref().and_then(|rig|rig.eye_look_config("p_cf_head_bone"))
        .or_else(||parsed_body.eye_look_config("p_cf_head_bone"));
    // EyeLookCalc.Awake runs on the newly instantiated rig BEFORE copying the
    // selected head prefab or applying sliders/ABMX. Preserve that Init basis.
    if let (Some(c),Some(rig))=(&mut eye_look_config,&prefab_head_skel) {
        c.init=crate::eye_look::Runtime::init_from_skeleton(rig,c);
    }
    // `[DeathWeasel][KKS] Face Fixes` overrides chara/bo_head_00.unity3d and is
    // what the game actually loads: its face skeleton places the eyes/nose
    // correctly and its meshes are the fixed ones. Its bundle omits
    // cf_face_00_t/_mc, so the custom face texture keeps using the vanilla
    // bundle's textures (composite is built from head_texs).
    // NOTE: the game loads `[DeathWeasel][KKS] Face Fixes` over
    // abdata/chara/bo_head_00.unity3d. Its skeleton places the eyes/nose
    // correctly (verified against the mesh dump face bones), but its meshes
    // ship changed UVs and its detail maps are not provided, so rendering the
    // override against the vanilla textures (which this pipeline uses) is
    // worse than the vanilla meshes. Keep vanilla until the override's own
    // textures + cf_m_face shader can be reproduced.
    let t_head = Instant::now();
    let head_bundle_bytes = crate::gamefs::read_source(&head_bundle).unwrap_or_default();
    let mut parsed_head = crate::unity::parse_bundle_cached(head_bundle_bytes);
    profile_tick("load_base_bundle head", t_head);
    let mut head_texs = parsed_head.textures_keep_base();
    // The Face Fixes bundle omits `cf_face_00_t`/`_mc`, so borrow the vanilla
    // bundle's textures for anything the override does not ship.
    if head_bundle != vanilla_head_bundle {
        let vb = crate::gamefs::read_source(&vanilla_head_bundle).unwrap_or_default();
        let vanilla_head = crate::unity::parse_bundle_cached(vb);
        let have: std::collections::HashSet<String> =
            head_texs.iter().map(|(n, _)| n.to_ascii_lowercase()).collect();
        for (n, b) in vanilla_head.textures_keep_base() {
            if !have.contains(&n.to_ascii_lowercase()) {
                head_texs.push((n, b));
            }
        }
        eprintln!("ok-extract: FACE_FIXES merged textures={}", head_texs.len());
    }
    // The game instantiates the list entry's `MainData` prefab; the bundle holds
    // several prefabs that reuse mesh names with different SMR bone lists. Only
    // the active prefab's meshes have the SMR the live game uses (e.g. the iris
    // is 1 bone, the sclera 54), so filter to it instead of name-matching.
    // `mod_catalog.get(100, headId)` is ambiguous: every sideloader head reuses
    // a small local id (usually 1). Resolve the card's own KKEx row, same as
    // the face texture.
    let mut face_zip_lists = std::collections::HashMap::new();
    let face_entry = parsed_card.as_ref().and_then(|card| {
        resolve_face_entry(&vanilla, card, &guid_index, &mut face_zip_lists)
    });
    if let Some(card) = parsed_card.as_ref() {
        if face_entry.is_none() && card.resolves.iter().any(|r| {
            r.category_no == 100 && r.property.to_ascii_lowercase().contains("headid")
        }) {
            // The mod is not installed: the game keeps its default head.
            eprintln!("ok-extract: head KKEx resolve has no list entry for headId={}; default head", card.head_id);
        }
    }
    let head_main = face_entry
        .as_ref()
        .map(|e| e.info("MainData"))
        .filter(|s| !s.is_empty() && s != "0")
        .unwrap_or_else(|| "p_cf_head_00".to_string());
    let head_shape_asset = face_entry
        .as_ref()
        .map(|e| e.info("ShapeAnime"))
        .filter(|s| !s.is_empty() && s != "0")
        .unwrap_or_else(|| "cf_anmShapeHead_00".to_string());
    eprintln!(
        "ok-extract: head entry={} main={} ab={}",
        face_entry.as_ref().map(|e| e.name.as_str()).unwrap_or("none"),
        head_main,
        face_entry.as_ref().map(|e| e.main_ab.as_str()).unwrap_or("")
    );
    // ChaControl.LoadCharaFbxData (ChaControl.cs:10596-10607) instantiates
    // `GetInfo(MainAB)` / `GetInfo(MainData)`, not always vanilla bo_head_00.
    // A sideloader head (KK_384283 Head Mod) ships its own mesh and eye textures.
    // Shape curves live in the same bundle as the head prefab (`ShapeAnime` =
    // `cf_anmShapeHead_00`). A sideloader head's curves are the mod's face
    // algorithm; vanilla keys are only the fallback when that asset is absent.
    let shape_bundle = head_bundle.clone();
    let mut mod_shape_keys = None;
    let mut head_key = crate::sqlcache::file_key(&head_bundle);
    if let Some(entry) = face_entry.as_ref() {
        let ab = entry.main_ab.as_str();
        if !ab.is_empty()
            && ab != "0"
            && !ab.eq_ignore_ascii_case("chara/bo_head_00.unity3d")
        {
            let zip = entry.extra.get("__zip").map(std::path::Path::new);
            if let Some(bytes) = accessory_bundle_bytes(game_root, zip, entry) {
                // Read from the bundle in memory (it was written to one temporary file name that cards opening at once
                // overwrote for each other).
                let trial = crate::shape::load_head_keys_named_bytes(&bytes, &head_shape_asset);
                if !trial.is_empty() {
                    eprintln!(
                        "ok-extract: head shape keys from mod bundle n={}",
                        trial.len()
                    );
                    mod_shape_keys = Some(trial);
                }
                let parsed = crate::unity::parse_bundle_cached(bytes);
                let (ms, _) = parsed.meshes_for_maindata(&head_main);
                if pick_named(&ms, "cf_O_face").is_some() {
                    eprintln!(
                        "ok-extract: head from list ab={ab} main={head_main} meshes={}",
                        ms.len()
                    );
                    let mut texs = parsed.textures_keep_base();
                    let have: std::collections::HashSet<String> =
                        texs.iter().map(|(n, _)| n.to_ascii_lowercase()).collect();
                    for (n, b) in std::mem::take(&mut head_texs) {
                        if !have.contains(&n.to_ascii_lowercase()) {
                            texs.push((n, b));
                        }
                    }
                    head_texs = texs;
                    parsed_head = parsed;
                    head_key = match zip {
                        Some(z) => crate::sqlcache::zip_key(z, ab),
                        None => format!("ab:{ab}|v{}", crate::sqlcache::CACHE_VER),
                    };
                } else {
                    eprintln!("ok-extract: list head ab={ab} has no cf_O_face, keeping vanilla");
                }
            }
        }
    }
    let mut head_shape_keys = mod_shape_keys.unwrap_or_else(|| crate::shape::load_head_keys_named(&shape_bundle, &head_shape_asset));
    if head_shape_keys.is_empty() && shape_bundle != vanilla_head_bundle {
        head_shape_keys = crate::shape::load_head_keys_named(&vanilla_head_bundle, &head_shape_asset);
    }
    if head_shape_keys.is_empty() {
        eprintln!("ok-extract: head ShapeAnime asset missing: {head_shape_asset}");
    }
    let t_head_md = Instant::now();
    let (head_filtered, mut head_skel) =
        if let Some(hit) = crate::sqlcache::load_prefab(game_root, &head_key, &head_main) {
            eprintln!("ok-extract: sql head prefab hit {head_main}");
            hit
        } else {
            let got = parsed_head.meshes_for_maindata(&head_main);
            crate::sqlcache::store_prefab(
                game_root,
                &head_key,
                &head_main,
                &got.0,
                got.1.as_ref(),
            );
            got
        };
    profile_tick("meshes_skel_for_maindata head", t_head_md);
    let head_meshes = if head_filtered.is_empty() {
        parsed_head.decode_all().0
    } else {
        head_filtered
    };
    let head_mouth_patterns = parsed_head.face_mouth_patterns(&head_main);
    let head_eye_patterns = parsed_head.face_control_patterns(&head_main,"EyesCtrl");
    let head_eyebrow_patterns = parsed_head.face_control_patterns(&head_main,"EyebrowCtrl");
    let mut head_face_settings=parsed_head.face_runtime_settings(&head_main);
    head_face_settings[3]=if parsed_card.as_ref().map_or(true,|card|card.eyes_blink) {1.}else{0.};
    head_face_settings[11]=if head_has_eye_controller {1.}else{0.};
    let head_eye_materials=parsed_head.eye_material_settings(&head_main);
    // Emitted material passes may have a different display name but retain
    // the source renderer's entire FBS controller identity.
    let mut head_fbs_sources = std::collections::HashMap::<String, String>::new();
    eprintln!("ok-extract: head FaceBlendShape mouth targets={} eyes={} eyebrow={}",
        head_mouth_patterns.len(),head_eye_patterns.len(),head_eyebrow_patterns.len());
    // ChaControl.cs:8115 + 8497: aaWeightsHead.CreateBoneList(objHeadBone =
    // "p_cf_head_bone") then AssignedWeightsAndSetBounds remaps every head SMR's
    // own bone list (component.bones[i].name) onto that armature. Set
    // `OK_OLDPHEAD` to keep the head mesh prefab's own SMR skeleton instead.
    if std::env::var("OK_OLDPHEAD").is_ok() {
        if let Some(face) = head_meshes
            .iter()
            .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        {
            if let Some(s) = parsed_head.skeleton(face.path_id) {
                head_skel = Some(s);
            }
        }
    } else if let Some(s) = prefab_head_skel.clone() {
        eprintln!(
            "ok-extract: head_skel = p_cf_head_bone ({} bones)",
            s.names.len()
        );
        head_skel = Some(s);
    }
    // StudioEditor instantiates p_cf_head_bone then RebindSkinning every head
    // SMR onto that full name map. The face SMR is only 45 bones; iris/sclera
    // extras (cf_J_hitomi_tx_*) live in tfs but not in names until we append.
    if let Some(sk) = head_skel.as_mut() {
        let before = sk.names.len();
        sk.include_tree_bones();
        if sk.names.len() != before {
            eprintln!(
                "ok-extract: head_skel include_tree_bones smr={} names={} tfs={} hitomi={}",
                sk.smr_count,
                sk.names.len(),
                sk.tfs.len(),
                sk.index_of("cf_J_hitomi_tx_L").is_some()
            );
        }
    }
    // ChaControl.cs:8630 `CommonLib.CopySameNameTransform(objHeadBone, objHead)`:
    // the head mesh prefab's LOCAL transforms overwrite every same-named node of
    // `p_cf_head_bone` before the face shape is applied (ChaControl.cs:8726
    // InitShapeFace targets objHeadBone). The armature authors the head attach
    // nodes at different positions than the mesh, so without this the hair
    // accessories hang ~0.03 high / ~0.016 forward (see pose.rs).
    if std::env::var("OK_NOCOPYTRF").is_err() {
        if let Some(mesh_skel) = parsed_head.skeleton_for_prefab_name(&head_main) {
            if let Some(sk) = head_skel.as_mut() {
                let changed = crate::pose::copy_same_name_transform(sk, &mesh_skel);
                if changed > 0 {
                    eprintln!(
                        "ok-extract: CopySameNameTransform head mesh '{}' onto p_cf_head_bone changed={changed}",
                        head_main
                    );
                }
            }
        }
    }
    // The supplied Studio T-pose captures use ChaControl's !hiPoly body-bone
    // reference layout even though their generated texture RTs use the 1024px
    // path. Keep texture resolution selection separate from attach nodes.
    // OK_BONE_HIPOLY=1 opts into the unmodified prefab references.
    if std::env::var("OK_BONE_HIPOLY").ok().as_deref() != Some("1") {
        if let Some(sk) = body_skel.as_mut() {
            crate::pose::apply_low_poly_body_references(sk);
        }
    }
    let mut body_rest = body_skel.clone();
    // Parse local PPtrs against the original prefab IDs. Merging the tree
    // into the SMR rig assigns synthetic IDs; runtime rebinds by these names.
    let body_dynamics = parsed_body.skeleton_for_prefab_name("p_cf_body_bone")
        .map(|s| parsed_body.body_dynamics_for_skeleton(&s)).unwrap_or_default();
    let body_colliders = parsed_body.skeleton_for_prefab_name("p_cf_body_bone")
        .map(|s| parsed_body.body_colliders_for_skeleton(&s)).unwrap_or_default();
    let mut clothes_dynamics = crate::dbone::DBoneSet::default();
    eprintln!("ok-extract: body Ver02 components={}", body_dynamics.len());
    let head_rest = head_skel.clone();
    profile_tick("load_base_bundles", t);
    check_preempted()?;
    t = Instant::now();
    eprintln!(
        "ok-extract: body texs={} names={:?} head meshes={}",
        body_texs.len(),
        body_texs
            .iter()
            .map(|(n, b)| format!("{n}:{}", b.len()))
            .collect::<Vec<_>>(),
        head_meshes.len()
    );
    let correct = crate::shape::load_correct_info(game_root);
    let (sex, type_bone, nip_rate) = parsed_card
        .as_ref()
        .map(|c| (c.sex, c.type_bone, c.nip_stand_rate))
        .unwrap_or((0, 0, 0.0));
    let mut body_head_scale = [1.0f32; 3];
    if let (Some(sk), Some(card)) = (body_skel.as_mut(), parsed_card.as_ref()) {
        if !card.shape_body.is_empty() && std::env::var("OK_NOSHAPE").is_err() {
            body_head_scale = crate::shape::apply_body_shape(
                game_root,
                sk,
                &card.shape_body,
                &correct,
                sex,
                type_bone,
                nip_rate,
            );
        }
    }
    let head_correct = 1.0 / (1.0 + correct.get(2).map(|c| c.scl[1]).unwrap_or(0.0));
    match head_skel.as_mut() {
        Some(sk) => {
            if let Some(card) = parsed_card.as_ref() {
                if !card.shape_face.is_empty() {
                    // The Face Fixes override ships the fixed skeleton/meshes but
                    // no shape animation, so fall back to the vanilla bundle's
                    // `cf_anmShapeHead_00` (the game keeps using it).
                    crate::shape::apply_head_shape_keys(
                        sk,
                        &head_shape_keys,
                        &card.shape_face,
                        body_head_scale,
                        head_correct,
                        type_bone,
                    );
                }
            }
            crate::pose::assigned_weights_remap(sk);
            // ABM/ABMX head modifiers are part of the card and applied by the
            // game to the head skeleton (`OK_NO_ABM_HEAD` disables for A/B).
            if std::env::var("OK_NO_ABM_HEAD").is_err() {
                if let Some(card) = parsed_card.as_ref() {
                    let names: Vec<String> =
                        card.abm.iter().map(|b| b.name.clone()).collect();
                    let base = crate::pose::capture_baseline(sk, &names);
                    crate::pose::apply_abm(sk, &card.abm, 0, &base);
                }
            }
            if std::env::var("OK_DIAG").is_ok() {
                for n in [
                    "cf_J_FaceRoot",
                    "cf_J_Nose_tip",
                    "cf_J_Eye_rz_L",
                    "cf_J_MouthBase_ty",
                    "a_n_head",
                    "a_n_headflont",
                    "a_n_headtop",
                    "a_n_headside",
                    "cf_J_Mayu_ty",
                    "cf_J_ChinLow",
                    "cf_J_Chin_Base",
                    "cf_J_Chin_s",
                    "cf_J_ChinTip_Base",
                    "cf_J_FaceLow_sx",
                ] {
                    if let Some(m) = sk.world_tf(n) {
                        let local = sk.tfs.values().find(|t| t.name == n);
                        eprintln!(
                            "ok-extract: HEADBONE {n} world={:?} wscl={:?} local_pos={:?} local_scl={:?} parent={}",
                            crate::pose::mat_pos(&m),
                            crate::pose::world_scale_pub(&m),
                            local.map(|t| t.pos),
                            local.map(|t| t.scale),
                            local.and_then(|t| t.parent).and_then(|p| sk.tfs.get(&p)).map(|t| t.name.as_str()).unwrap_or("")
                        );
                    }
                }
                let rw = sk.root_world();
                eprintln!(
                    "ok-extract: HEADBONE root pos={:?} scl={:?}",
                    crate::pose::mat_pos(&rw),
                    crate::pose::world_scale_pub(&rw)
                );
            }
        }
        None => eprintln!("ok-extract: no head skeleton"),
    }
    // The face SMR skeleton is pruned to its 45 bones; the game remaps hair
    // prefabs onto the head's full bone tree (cf_J_hairF_top / cf_J_hairB_top),
    // which carries the head-shape scale. Build that full skeleton once and
    // skin the hair against it (see the hair branch in `rig_mesh`).
    let head_full_rest = parsed_head.skeleton_full_named("cf_J_hairF_top");
    let mut head_full_skel = head_full_rest.clone();
    if let Some(sk) = head_full_skel.as_mut() {
        if let Some(card) = parsed_card.as_ref() {
            if !card.shape_face.is_empty() {
                crate::shape::apply_head_shape_keys(
                    sk,
                    &head_shape_keys,
                    &card.shape_face,
                    body_head_scale,
                    head_correct,
                    type_bone,
                );
            }
            // Hair is skinned against this full head tree, while face parts
            // use p_cf_head_bone above. ABMX runs on the shared objHeadBone in
            // Unity, so both reconstructions must receive the same modifiers.
            if std::env::var("OK_NO_ABM_HEAD").is_err() {
                let names: Vec<String> = card.abm.iter().map(|b| b.name.clone()).collect();
                let base = crate::pose::capture_baseline(sk, &names);
                crate::pose::apply_abm(sk, &card.abm, 0, &base);
            }
        }
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: head_full_skel names={} hairF={} hairF_scl={:?}",
                sk.names.len(),
                sk.index_of("cf_J_hairF_top").is_some(),
                sk.world_tf("cf_J_hairF_top")
                    .map(|m| crate::pose::world_scale_pub(&m))
            );
        }
    }
    profile_tick("shape_body+head", t);
    t = Instant::now();
    // ChaControl: InitShapeBody(objBodyBone) then AssignedWeights then AnimPlay(Fmannequin_00).
    if let Some(sk) = body_skel.as_mut() {
        crate::pose::assigned_weights_remap(sk);
        let abm_names: Vec<String> = parsed_card
            .as_ref()
            .map(|c| c.abm.iter().map(|b| b.name.clone()).collect())
            .unwrap_or_default();
        // The dump reference is the neutral T-pose.  Stand_D_00 is a Maker
        // animation, and applying it here moves March's body vertices by up to
        // 54 cm from the captured mesh.  A named pose remains opt-in for pose
        // previews; the card alone does not request one.
        let pose = std::env::var("OK_POSE").ok().filter(|_| crate::profile::var("OK_NOPOSE").is_err());
        let scene = std::env::var("OK_POSE_SCENE").ok().and_then(|v| v.parse().ok()).unwrap_or(0);
        let (arm, hand) = if let Some(state) = pose.as_ref() {
            let shaped = sk.clone();
            crate::anim::apply_clip_at(game_root, sk, state, f32::MAX);
            crate::shape::apply_body_shape_always(sk, &shaped);
            crate::anim::clip_limb_corrections_mode(game_root, state, scene)
        } else {
            (true, true)
        };
        crate::pose::apply_scene_limb_size_corrections(sk, arm, hand);
        if pose.as_ref().is_some_and(|state|
            crate::anim::clip_expression_bone_mode(game_root, state, scene))
            && std::env::var("OK_NOEXPR").is_err() {
            crate::pose::apply_expression_bone(sk);
        }
        // ABMX modifies each bone relative to its current (posed) local TRS
        // (`BoneModifier.Apply` runs after the Animator), so the baseline must be
        // captured AFTER the pose/expression pass, not before it.
        let abm_base = crate::pose::capture_baseline(sk, &abm_names);
        if let Some(card) = parsed_card.as_ref() {
            crate::pose::apply_abm(sk, &card.abm, 0, &abm_base);
            if std::env::var("OK_FINALIK").is_ok() {
                crate::finalik::apply_final_ik(
                    game_root,
                    &card.shape_body,
                    sk,
                    "Stand_D_00",
                    "p_cf_body_bone",
                );
            }
        }
    }
    profile_tick("pose+abm", t);
    t = Instant::now();
    let mut t_mark = Instant::now();
    if let Some(b) = body_skel.as_ref() {
        eprintln!(
            "ok-extract: body tf cf_s_head={} HairParent={} a_n_hair_pony={}",
            b.world_tf("cf_s_head").is_some(),
            b.world_tf("cf_J_FaceUp_ty").is_some(),
            b.world_tf("a_n_hair_pony").is_some()
        );
    }
    if let Some(h) = head_skel.as_ref() {
        eprintln!(
            "ok-extract: head tf cf_J_FaceUp_ty={} a_n_hair_pony={} root_ok",
            h.world_tf("cf_J_FaceUp_ty").is_some(),
            h.world_tf("a_n_hair_pony").is_some()
        );
        if let Some(m) = bone_world(body_skel.as_ref(), Some(h), "cf_J_FaceUp_ty") {
            eprintln!(
                "ok-extract: HairParent world pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
        if let Some(m) = h.world_tf("cf_J_FaceBase") {
            eprintln!(
                "ok-extract: FaceBase local-space world pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
        if let Some(m) = h.world_tf("a_n_headtop") {
            eprintln!(
                "ok-extract: a_n_headtop pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
        if let Some(m) = h.world_tf("a_n_headside") {
            eprintln!(
                "ok-extract: a_n_headside pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
    }
    if std::env::var("OK_ATTACH").is_ok() {
        // Neck/head chain, to compare against the dump skeleton.json locals.
        if let Some(b) = body_skel.as_ref() {
            for n in [
                "cf_j_hips",
                "cf_j_spine01",
                "cf_j_spine02",
                "cf_j_spine03",
                "cf_j_neck",
                "cf_j_head",
                "cf_s_spine03",
                "cf_s_neck",
                "cf_s_head",
                "cf_j_thigh00_R",
                "cf_j_leg01_R",
                "cf_j_leg03_R",
                "cf_j_foot_R",
            ] {
                if let Some(tf) = b.tfs.values().find(|t| t.name == n) {
                    eprintln!(
                        "ok-extract: BODYLOCAL {n} pos={:?} rot={:?} scl={:?}",
                        tf.pos, tf.rot, tf.scale
                    );
                }
                if let Some(m) = b.world_tf(n) {
                    eprintln!(
                        "ok-extract: BODYWORLD {n} r0={:?} r1={:?} r2={:?}",
                        m[0], m[1], m[2]
                    );
                }
            }
        }
        // ChaReference.RefObjKey -> actual transform (matches the game
        // skeleton.json `referenceObjects` ground truth world positions).
        for rk in [
            "HeadParent",
            "HairParent",
            "a_n_head",
            "a_n_headflont",
            "a_n_headtop",
            "a_n_headside",
            "a_n_hair_pony",
            "a_n_hair_twin_L",
            "a_n_hair_twin_R",
            "a_n_neck",
            "a_n_megane",
            "a_n_nose",
            "a_n_mouth",
            "a_n_nip_R",
            "a_n_nip_L",
            "a_n_bust_f",
            "a_n_bust",
            "a_n_waist_f",
            "a_n_waist_b",
            "a_n_waist_R",
            "a_n_heel_R",
            "a_n_heel_L",
            "a_n_elbo_R",
            "a_n_leg_R",
            "a_n_shoulder_R",
            "a_n_shoulder_L",
            "a_n_arm_R",
            "cf_j_foot_R",
            "cf_j_foot_L",
            "cf_j_leg03_R",
            "cf_j_leg01_R",
            "cf_j_thigh00_R",
            "cf_s_thigh02_R",
        ] {
            let target = match rk {
                "HeadParent" => "cf_s_head",
                "HairParent" => "cf_J_FaceUp_ty",
                other => other,
            };
            if let Some(m) = bone_world(body_skel.as_ref(), head_skel.as_ref(), target) {
                eprintln!(
                    "ok-extract: ATTACH {rk} obj={target} pos={:?} scl={:?}",
                    crate::pose::mat_pos(&m),
                    crate::pose::world_scale_pub(&m)
                );
            }
        }
    }
    if std::env::var("OK_DIAG").is_ok() {
        if let Some(h) = head_skel.as_ref() {
            for tf in h.tfs.values() {
                if !tf.name.starts_with("cf_J_") {
                    continue;
                }
                eprintln!(
                    "ok-extract: HEADLOCAL {} {:.6} {:.6} {:.6} | {:.6} {:.6} {:.6}",
                    tf.name, tf.pos[0], tf.pos[1], tf.pos[2], tf.scale[0], tf.scale[1], tf.scale[2]
                );
            }
        }
    }
    let mut seen_meshes: std::collections::HashSet<String> = std::collections::HashSet::new();
    let mut body_mesh = body_skel
        .as_ref()
        .and_then(|s| pick_path(&body_meshes, "o_body_a", s.source_mesh))
        .cloned()
        .or_else(|| pick_named(&body_meshes, "o_body_a").cloned())
        .or_else(|| pick_largest(&body_meshes).cloned());
    profile_tick("body tf/pose prelude", t_mark); t_mark = Instant::now();
    let mut replaced_body = false;
    let mut replacement_genitals: Vec<ExtractedMesh> = Vec::new();
    if let Some(card) = parsed_card.as_ref() {
        let nv = body_mesh.as_ref().map(|m| m.positions.len()).unwrap_or(0);
        if let Some((source, bytes)) =
            crate::mods::uncensor_body_for_card(&game_root,card.body_guid.as_deref(),card.sex)
        {
            let parsed=crate::unity::parse_bundle_ref(&bytes);
            parsed.configure_game_references(game_root);
            // The installed uncensor's skin textures and body meshes are the same for every card that uses it
            // (~0.25 s to decode each time); the card-shaped work below (skeleton merge, dynamics) still runs.
            type UncensorDecoded=(Vec<Option<Vec<u8>>>,Vec<ExtractedMesh>);
            static UNCENSOR_BODY:std::sync::Mutex<Option<(String,std::sync::Arc<UncensorDecoded>)>>=std::sync::Mutex::new(None);
            let stamp=crate::gamefs::metadata(&source.zipmod).ok();
            let memo_key=format!("{}|{}|{:?}|{}|{}|{}|{}",source.zipmod.display(),stamp.as_ref().map_or(0,|m|m.len()),stamp.as_ref().and_then(|m|m.modified().ok()),
                source.file,source.asset,source.main_texture,source.color_mask);
            let cached=UNCENSOR_BODY.lock().unwrap_or_else(|e|e.into_inner()).as_ref().filter(|(k,_)|stamp.is_some()&&*k==memo_key).map(|(_,d)|d.clone());
            let decoded=cached.unwrap_or_else(||{
                let d=std::sync::Arc::new((vec![parsed.texture_png_named(&source.main_texture),parsed.texture_png_named(&source.color_mask)],
                    parsed.meshes_for_maindata(&source.asset).0));
                *UNCENSOR_BODY.lock().unwrap_or_else(|e|e.into_inner())=Some((memo_key,d.clone()));
                d
            });
            // UncensorSelector's CreateBodyTexture transpiler redirects OOBase,
            // BodyMainTex and the sex-specific BodyColorMask, not just geometry.
            for (index,(name,alias)) in [(&source.main_texture,"cf_body_00_t"),
                (&source.color_mask,if card.sex==0{"cm_body_00_mc"}else{"cf_body_00_mc"})].into_iter().enumerate() {
                if let Some(png)=decoded.0[index].clone() {
                    let bytes=png.len();
                    body_texs.retain(|(n,_)|!n.eq_ignore_ascii_case(alias));
                    body_texs.push((alias.into(),png));
                    eprintln!("ok-extract: uncensor body skin {alias} <- {name} bytes={bytes} (vagina uses this uncensor texture, not the mosaic base)");
                } else {
                    eprintln!("ok-extract: uncensor body skin {name} missing from {}; vagina stays on the loaded body texture", source.file);
                }
            }
            let meshes=&decoded.1;
            if let Some(mut m) = meshes
                .iter()
                .filter(|m| m.name.eq_ignore_ascii_case("o_body_a") && !m.weights.is_empty())
                .max_by_key(|m| m.positions.len())
                .cloned()
            {
                // UncensorSelector replaces the body's mesh/rig, not the live
                // cf_m_body material. Keep the game's shader properties and
                // texture slots instead of inheriting the uncensor asset's
                // placeholder material (which loses _ShadowExtend, etc.).
                if let Some(base) = body_mesh.as_ref() {
                    if base.mat.is_some() {
                        m.mat = base.mat.clone();
                    }
                    if base.detail_mask_png.is_some() {
                        m.detail_mask_png = base.detail_mask_png.clone();
                        if let Some(settings) = base.runtime_textures.get("_DetailMask") {
                            m.runtime_textures.insert("_DetailMask".into(), *settings);
                        } else {
                            m.runtime_textures.remove("_DetailMask");
                        }
                    }
                    if base.line_mask_png.is_some() {
                        m.line_mask_png = base.line_mask_png.clone();
                    }
                    if base.normal_map_png.is_some() {
                        m.normal_map_png = base.normal_map_png.clone();
                    }
                }
                let nv2 = m.positions.len();
                if let Some(sk) = body_skel.as_mut() {
                    // RebindSkinning uses this replacement renderer's bones.
                    // A fixed SAC order cannot describe other installed bodies.
                    if let Some(source_skeleton)=m.smr_skel.as_ref() {
                        sk.merge_missing_tree_transforms(source_skeleton);
                        if let Some(rest)=body_rest.as_mut() {rest.merge_missing_tree_transforms(source_skeleton);}
                        let dynamics=parsed.dynamic_bones_for_skeleton(source_skeleton)
                            .with_game_fixes(game_root).retarget_body(source_skeleton,sk);
                        eprintln!("ok-extract: replacement body DynamicBone components={}",dynamics.len());
                        clothes_dynamics.extend(dynamics);
                    }
                    let names=&m.bone_names;
                    sk.set_bone_order(&names);
                    sk.include_tree_bones();
                    if let Some(rest) = body_rest.as_mut() {
                        rest.set_bone_order(&names);
                        rest.include_tree_bones();
                    }
                    eprintln!(
                        "ok-extract: body bones reordered to source renderer order={} first={:?}",
                        names.len(),
                        sk.names.iter().take(4).collect::<Vec<_>>()
                    );
                }
                eprintln!(
                    "ok-extract: uncensor body guid={} <- {} asset={} verts={nv2} (was {nv}) weights={}",
                    source.guid,source.file,source.asset,
                    m.weights.len()
                );
                body_mesh = Some(m);
                replaced_body = true;
                replacement_genitals = meshes.iter()
                    .filter(|item| is_genital_display_mesh(&item.name))
                    .cloned()
                    .collect();
            }
        }
    }
    // o_body_a is not weighted to the penis chain. Append those bones before
    // the body is skinned so its existing indices stay put and the separate
    // penis mesh can bind by name. A replacement body that bakes the penis
    // into o_body_a contributes no extra meshes here.
    let mut genital_sources: Vec<ExtractedMesh> = if replaced_body {
        replacement_genitals
    } else if parsed_card.as_ref().is_some_and(|card| card.sex == 0) {
        body_meshes.iter().filter(|item| is_genital_display_mesh(&item.name)).cloned().collect()
    } else {
        Vec::new()
    };
    profile_tick("uncensor body", t_mark); t_mark = Instant::now();
    if let Some(card) = parsed_card.as_ref() {
        let seed = card.birth_day.wrapping_add(card.personality)
            .wrapping_add((card.voice_pitch * 100.0) as i32);
        let nail = card.nail_color;
        adopt_uncensor_genitals(
            &mut genital_sources, &game_root, card, crate::mods::UncensorGenitalKind::Penis,
            is_penis_mesh, seed, skin_tint, skin_sub, nail, replaced_body, outfit,
        );
        adopt_uncensor_genitals(
            &mut genital_sources, &game_root, card, crate::mods::UncensorGenitalKind::Balls,
            is_balls_mesh, seed, skin_tint, skin_sub, nail, replaced_body, outfit,
        );
    }
    profile_tick("uncensor genitals", t_mark); t_mark = Instant::now();
    genital_sources.sort_by(|a, b| a.name.to_ascii_lowercase().cmp(&b.name.to_ascii_lowercase()));
    if !genital_sources.is_empty() {
        let added = body_skel.as_mut().map(|sk| {
            genital_sources.iter().filter_map(|mesh| mesh.smr_skel.as_ref())
                .map(|part| sk.append_missing_tree_bones(part)).sum::<usize>()
        }).unwrap_or(0);
        let added_rest = body_rest.as_mut().map(|sk| {
            genital_sources.iter().filter_map(|mesh| mesh.smr_skel.as_ref())
                .map(|part| sk.append_missing_tree_bones(part)).sum::<usize>()
        }).unwrap_or(0);
        eprintln!("ok-extract: genital bones appended skeleton={added} rest={added_rest}");
    }
    if let Some(mesh) = body_mesh.as_mut() {
        if let Some(bundle) = skin_material_bundle.as_ref() {
            if let Some(mat) = bundle.material_named(if parsed_card.as_ref().is_some_and(|c|c.sex==0) {"cm_m_body"}else{"cf_m_body"}) {
                if let Some(name) = mat.slot("_DetailMask") {
                    if let Some(png) = bundle.texture_png_named(name) {
                        mesh.detail_mask_png = Some(orient_raw_mask(png));
                        if let Some(sampling) = bundle.texture_sampling_named(name) {
                            mesh.runtime_textures.insert("_DetailMask".into(), [
                                sampling.filter, sampling.wrap_u, sampling.wrap_v,
                                i32::from(sampling.srgb), sampling.mip_count,
                            ]);
                        } else {
                            mesh.runtime_textures.remove("_DetailMask");
                        }
                    }
                }
                mesh.line_mask_png = mat.slot("_LineMask")
                    .and_then(|name| bundle.texture_png_named(name).map(orient_raw_mask))
                    .or_else(|| mesh.line_mask_png.take());
                mesh.normal_map_png = mat.slot("_NormalMap")
                    .and_then(|name| bundle.texture_png_named(name))
                    .or_else(|| mesh.normal_map_png.take());
                eprintln!("ok-extract: body draw mat={} shader={} detail={} line={} normal={} shadow_extend={:?}",
                    mat.name, mat.shader, mesh.detail_mask_png.as_ref().map_or(0, Vec::len),
                    mesh.line_mask_png.as_ref().map_or(0, Vec::len),
                    mesh.normal_map_png.as_ref().map_or(0, Vec::len), mat.float("_ShadowExtend"));
                mesh.mat = Some(mat);
            }
        }
        profile_tick("pre-skin body setup", t_mark); t_mark = Instant::now();
        let t_skin = Instant::now();
        rig_mesh(mesh, "body", body_skel.as_ref(), head_skel.as_ref(), None, None, None, None, None);
        profile_tick("skin+rig body", t_skin);
        let t_body_tex = Instant::now();
        compose_body_texture(
            mesh,
            parsed_card.as_ref(),
            skin_tint,
            skin_sub,
            &body_texs,
            outfit,
        );
        // ChaControl.ChangeSettingBodyDetail assigns the list's normal and
        // line detail to the draw material, then sets DetailNormalMapScale.
        // main_skin combines this detail normal with cf_m_body's base normal;
        // the Koikano and Shader Forge DXBC use different blend operators.
        let mut body_normal_detail_png = None;
        if let Some(card) = parsed_card.as_ref() {
            if let Some(mat) = mesh.mat.as_mut() {
                mat.floats.insert("_DetailNormalMapScale".into(), card.body_detail_power);
                // VisibleAddBodyLine overwrites the draw material after it is
                // loaded; mm_base's serialized default is always on.
                mat.floats.insert("_linetexon".into(), if card.body_draw_add_line { 1.0 } else { 0.0 });
            }
            if let Some(line) = load_catalog_png(
                game_root, &vanilla, &mod_catalog, 420, card.body_detail_id,
                "LineMask", &head_texs, &body_texs,
            ) {
                mesh.line_mask_png = Some(orient_raw_mask(line));
            }
            if let Some(detail) = load_catalog_png(
                game_root, &vanilla, &mod_catalog, 420, card.body_detail_id,
                "NormallMapDetail", &head_texs, &body_texs,
            ) {
                eprintln!("ok-extract: body normal detail id={} scale={:.3} bytes={}",
                    card.body_detail_id, card.body_detail_power, detail.len());
                // Koikano/main_skin samples _NormalMap and _NormalMapDetail
                // independently (0072.asm.txt t7/t8). Keep the detail map
                // separate; pre-blending it changes the sampled normal.
                body_normal_detail_png = Some(convert_runtime_normal_map(detail));
            } else {
                eprintln!("ok-extract: body normal detail unavailable id={}", card.body_detail_id);
            }
        }
        profile_tick("compose_body_texture", t_body_tex);
        eprintln!(
            "ok-extract: body tex {} bytes name={}",
            mesh.texture_png.as_ref().map(|p| p.len()).unwrap_or(0),
            mesh.texture_name
        );
        // ChaControl assigns the equipped top/inner's _AlphaMask to the draw
        // material. Keep it separate from MainTex: main_skin samples t0 at
        // the body's UV and clips min(max(r,1-alpha_a),max(g,1-alpha_b)).
        t_mark = Instant::now();
        // The body's normal map converts on another thread while the masks and MaterialEditor run below: the card's
        // MaterialEditor normal map when it has one (it replaces the body's below), else the body's own. Whatever the
        // body ends up with and was not converted here converts as before.
        let normal_source = parsed_card.as_ref()
            .and_then(|card| me_texture(Some(&card.me_tex), outfit, "body", None, mesh, "normalmap").map(|png| png.to_vec()))
            .or_else(|| mesh.normal_map_png.clone());
        let normal_job = normal_source.clone().map(|png| {
            let pack = crate::oklib::current_pack_scope();
            std::thread::spawn(move || { let _pack = crate::oklib::enter_pack_scope(pack); convert_runtime_normal_map(png) })
        });
        let mut body_blend = "opaque";
        if let Some(card) = parsed_card.as_ref() {
            mesh.alpha_mask_png = over_body_mask_png(game_root, card, &vanilla, &mod_catalog, &guid_index, outfit)
                .map(orient_raw_mask);
            if mesh.alpha_mask_png.is_some() {
                eprintln!("ok-extract: body over-mask loaded");
            }
        }
        // KSOX `BodyOver` overlay (type 1), applied after the alpha mask.
        if let Some(card) = parsed_card.as_ref() {
            if let Some(ov) = ksox_overlay_png(card, outfit, 1) {
                if let Some(png) = mesh.texture_png.as_ref() {
                    if let Some(c) = composite_overlay(png, &ov) {
                        mesh.texture_png = Some(c);
                        eprintln!("ok-extract: body over-overlay applied");
                    }
                }
            }
        }
        // Material Editor applies material texture overrides after ChaControl
        // creates the draw material. Body bypasses the generic clothing/hair
        // path, so apply the same renderer/material lookup explicitly here.
        if let Some(card) = parsed_card.as_ref() {
            // Body bypasses push_meshes. Replay its texture-coordinate edits
            // before emit_part captures the material ST fields. Keep this
            // addition confined to texture transforms, not unrelated numeric
            // shader properties that this legacy body path does not yet port.
            let transforms:crate::card::MeFloatMap=card.me_float.iter()
                .filter(|((_,_,_,_,property),_)|["_st_x","_st_y","_st_z","_st_w"].iter()
                    .any(|axis|property.to_ascii_lowercase().ends_with(axis)))
                .map(|(key,value)|(key.clone(),*value)).collect();
            apply_me_floats(&transforms,outfit,"body",None,mesh);
            if let Some(png) = me_maintex(Some(&card.me_tex), outfit, "body", None, mesh) {
                eprintln!("ok-extract: ME body MainTex bytes={}", png.len());
                mesh.texture_png = Some(png.to_vec());
                mark_material_editor_main_texture(mesh);
            }
            for (slot, setter) in [
                ("detailmask", 0u8), ("linemask", 1), ("normalmap", 2), ("alphamask", 3),
            ] {
                if let Some(png) = me_texture(Some(&card.me_tex), outfit, "body", None, mesh, slot) {
                    eprintln!("ok-extract: ME body {slot} bytes={}", png.len());
                    match setter {
                        0 => {
                            mesh.detail_mask_png = Some(png.to_vec());
                            mesh.runtime_textures.remove("_DetailMask");
                        },
                        1 => mesh.line_mask_png = Some(png.to_vec()),
                        2 => mesh.normal_map_png = Some(png.to_vec()),
                        _ => mesh.alpha_mask_png = Some(png.to_vec()),
                    }
                }
            }
        }
        if let Some(mask) = mesh.alpha_mask_png.take() {
            mesh.alpha_mask_png = body_mask_for_alpha_state(&mask, clothes_alpha_state(manifest.clothes_states[0]));
            if mesh.alpha_mask_png.is_some() { body_blend = "masked"; }
        }
        profile_tick("body masks+ME", t_mark); t_mark = Instant::now();
        let normal_now = mesh.normal_map_png.take();
        let converted = match normal_job {
            Some(job) if normal_now.is_some() && normal_now == normal_source => job.join().ok(),
            Some(job) => { let _ = job.join(); None }
            None => None,
        };
        mesh.normal_map_png = converted.or_else(|| normal_now.map(convert_runtime_normal_map));
        profile_tick("body normal convert", t_mark); t_mark = Instant::now();
        if let Some(card)=parsed_card.as_ref() {apply_native_skin_draw_settings(mesh,card,outfit,true);}
        let (p, tex) = emit_part(out_dir, &mut session_parts, "body", "body", mesh, body_blend, false)?;
        profile_tick("body draw+emit", t_mark); t_mark = Instant::now();
        if let Some(part) = session_parts.last_mut() {
            part.normal_detail = body_normal_detail_png;
        }
        if let (Some(card), Some(part)) = (parsed_card.as_ref(), session_parts.last_mut()) {
            // ChaControl.ChangeSettingNip / ChangeSettingUnderhair assign these
            // to the draw material after CreateBodyTexture. They use separate
            // authored UV channels and must not be baked into its MainTex.
            if let Some((png, settings)) = load_catalog_display_texture(
                game_root, &vanilla, &mod_catalog, 423, card.nip_id, "NipTex",
            ) {
                part.overtex_png = Some(png);
                assign_display_texture_settings(part, "_overtex1", settings);
            }
            part.overcolor = card.nip_color;
            part.nip_size = card.areola_size;
            part.nip_specular = card.nip_gloss_power;
            if card.underhair_id != 0 {
                if let Some((png, settings)) = load_catalog_display_texture(
                    game_root, &vanilla, &mod_catalog, 424, card.underhair_id, "UnderhairTex",
                ) {
                    part.overtex2_png = Some(png);
                    assign_display_texture_settings(part, "_overtex2", settings);
                }
                part.overcolor2 = card.underhair_color;
            }
            eprintln!("ok-extract: body display overtex nip={} underhair={} uv1={} uv2={} color={}",
                part.overtex_png.as_ref().map_or(0, Vec::len),
                part.overtex2_png.as_ref().map_or(0, Vec::len),
                part.uv2.len(), part.uv3.len(), part.vertex_colors.len());
        }
        seen_meshes.insert(mesh.name.to_ascii_lowercase());
        parts.push(ManifestPart {
            role: "body".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: body_blend.into(),
            ..Default::default()
        });
    }
    // Penis and scrotum are separate body renderers. A replacement body that
    // does not ship them keeps the penis inside o_body_a; do not also attach
    // the vanilla meshes to that rig. The gallery hides these until asked.
    let mut genital_emitted = 0;
    // Parts keep the order the sources list them (a HashMap here gave a different part order on every run).
    let mut genital_best: Vec<ExtractedMesh> = Vec::new();
    for mesh in genital_sources {
        if mesh.positions.is_empty() || !is_genital_display_mesh(&mesh.name) { continue; }
        match genital_best.iter_mut().find(|prev| prev.name.eq_ignore_ascii_case(&mesh.name)) {
            Some(prev) => { if prev.positions.len() < mesh.positions.len() { *prev = mesh; } }
            None => genital_best.push(mesh),
        }
    }
    for mut mesh in genital_best {
        if !seen_meshes.insert(mesh.name.to_ascii_lowercase()) { continue; }
        let part_skel = mesh.smr_skel.clone();
        mesh.skel_kind = 1;
        if let (Some(sk), Some(ps)) = (body_skel.as_ref(), part_skel.as_ref()) {
            remap_part_to_body(&mut mesh, sk, ps);
            let _ = skin_mesh(&mut mesh, sk);
        } else if let Some(sk) = body_skel.as_ref() {
            let _ = skin_mesh(&mut mesh, sk);
        }
        let stem = format!("genital_{}", mesh.name.replace(['/', '\\'], "_"));
        let (p, tex) = emit_part(out_dir, &mut session_parts, &stem, "body", &mesh, "opaque", false)?;
        eprintln!("ok-extract: genital mesh {} verts={} tex={}",
            mesh.name, mesh.positions.len(), mesh.texture_png.as_ref().map_or(0, Vec::len));
        genital_emitted += 1;
        parts.push(ManifestPart {
            role: "body".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: "opaque".into(),
            ..Default::default()
        });
    }
    if genital_emitted == 0 && parsed_card.as_ref().is_some_and(|card| card.sex == 0) {
        eprintln!("ok-extract: no separate penis or scrotum mesh; the display toggle has nothing to switch");
    }
    // NOTE: the mouth (o_mnpa/o_mnpb) is a face child whose SMR has its own
    // bone list and a runtime pattern atlas (cf_t_mnpb_00_XX); it needs the
    // mouth SMR skeleton + cf_m_face composite, not the body skeleton.
    // Mouth (lips): o_mnpa/o_mnpb have their own SMR bone list; rig with that
    // skeleton and use the cf_t_mnpb_00_XX pattern atlas.
    for mname in ["o_mnpb", "o_mnpa"] {
        let Some(mut mesh) = body_meshes
            .iter()
            .filter(|m| m.name.eq_ignore_ascii_case(mname))
            .max_by_key(|m| m.positions.len())
            .cloned()
        else {
            continue;
        };
        if !seen_meshes.insert(mesh.name.to_ascii_lowercase()) {
            continue;
        }
        // o_mnpa / o_mnpb are hidden by the card preview (StudioEditor hides
        // them too); they are not the mouth.
        if is_hidden_part(&mesh.name) {
            continue;
        }
        let part_skel = crate::unity::skeleton_for_mesh(&body_bundle, &mesh.name);
        rig_mesh(
            &mut mesh,
            "mouth",
            body_skel.as_ref(),
            head_skel.as_ref(),
            None,
            part_skel.as_ref(),
            None,
            None,
            None,
        );
        if std::env::var("OK_FACE_RAW").is_ok() {
            for i in 0..4 {
                let n = format!("cf_t_mnpb_00_{i:02}");
                if let Some((_, png)) = crate::unity::named_png_from_bundle(
                    &game_root.join("abdata/chara/etc.unity3d"),
                    &[&n],
                ) {
                    let _ = std::fs::write(
                        format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\mnpb_{i:02}.png"),
                        &png,
                    );
                }
            }
        }
        if let Some((_, png)) = crate::unity::named_png_from_bundle(
            &game_root.join("abdata/chara/etc.unity3d"),
            &["cf_t_mnpb_00_00"],
        ) {
            mesh.texture_png = Some(png);
        }
        let stem = format!("mouth_{}", sanitize(&mesh.name));
        let (p, tex) = emit_part(out_dir, &mut session_parts, &stem, "mouth", &mesh, "masked", false)?;
        parts.push(ManifestPart {
            role: "mouth".into(),
            path: p,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: "masked".into(),
            ..Default::default()
        });
    }
    if std::env::var("OK_FACE_RAW").is_ok() {
        for m in head_meshes.iter() {
            if m.name.to_ascii_lowercase().contains("face") {
                eprintln!(
                    "ok-extract: HEADFACE {} path={} v={} tris={}",
                    m.name,
                    m.path_id,
                    m.positions.len(),
                    m.indices.len() / 3
                );
            }
        }
    }
    // ChaControl loads the head prefab and draws the renderer named `cf_O_face`.
    // `p_cf_head_bone.source_mesh` is 0 (different bundle), so matching by path
    // id, or falling back to the largest mesh, bound the face texture to
    // `cf_O_eyeline` and never emitted the face. KK_091207's session had no
    // `cf_O_face`; the editor drew that eyeline opaque.
    let face_mesh = pick_named(&head_meshes, "cf_O_face").cloned().or_else(|| {
        eprintln!(
            "ok-extract: cf_O_face missing from head prefab '{head_main}', using vanilla p_cf_head_00"
        );
        let vb = crate::gamefs::read_source(&vanilla_head_bundle).unwrap_or_default();
        if vb.is_empty() {
            return None;
        }
        let parsed = crate::unity::parse_bundle_cached(vb);
        let (ms, _) = parsed.meshes_for_maindata("p_cf_head_00");
        pick_named(&ms, "cf_O_face").cloned()
    });
    if let Some(mut mesh) = face_mesh.filter(|_| std::env::var("OK_SKIP_FACE").is_err()) {
        if let Some(mat) = mesh.mat.as_ref() {
            eprintln!("ok-extract: face material={} shader={} rimV={:?} rimPower={:?} faceNormal={:?} shadowColor={:?}",
                mat.name, mat.shader, mat.float("_rimV"), mat.float("_rimpower"),
                mat.float("_FaceNormalG"), mat.color("_ShadowColor"));
        }
        eprintln!(
            "ok-extract: face mesh={} verts={} prefab={head_main}",
            mesh.name,
            mesh.positions.len()
        );
        // Diagnostic `OK_FACE_DROP_CAVITY`: drop `cf_O_face` triangles whose
        // vertices all sample the vanilla `cf_face_00_t` mouth-cavity rectangle
        // (a dark patch otherwise shows on the chin).
        if std::env::var("OK_FACE_DROP_CAVITY").is_ok() {
            let mut kept = Vec::with_capacity(mesh.indices.len());
            let mut dropped = 0usize;
            for t in mesh.indices.chunks_exact(3) {
                let all = t.iter().all(|&v| {
                    let q = mesh.uvs.get(v as usize).copied().unwrap_or([0.0, 2.0]);
                    (0.28..=0.72).contains(&q[0])
                        && ((0.78..=1.0).contains(&q[1]) || (0.005..=0.23).contains(&q[1]))
                });
                if all {
                    dropped += 1;
                } else {
                    kept.extend_from_slice(t);
                }
            }
            eprintln!(
                "ok-extract: DROPCAVITY face tris {} -> {} dropped={}",
                mesh.indices.len() / 3,
                kept.len() / 3,
                dropped
            );
            mesh.indices = kept;
        }
        // aaWeightsHead.AssignedWeightsAndSetBounds remaps the SMR's own bone
        // list (component.bones[i].name) onto objHeadBone = p_cf_head_bone
        // (ChaControl.cs:8497 / AssignedAnotherWeights.cs:150). Use the face
        // SMR's own skeleton as the rebind source.
        let face_own_skel = parsed_head.skeleton(mesh.path_id);
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: FACE own_skel={} head_skel={}",
                face_own_skel.as_ref().map(|s| s.names.len()).unwrap_or(0),
                head_skel.as_ref().map(|s| s.names.len()).unwrap_or(0)
            );
        }
        if std::env::var("OK_CHINDIAG").is_ok() {
            if let Some(ps) = face_own_skel.as_ref() {
                eprintln!("FACEOWN bones={}", ps.names.len());
                for (i, n) in ps.names.iter().enumerate() {
                    eprintln!("  own[{i}] {n}");
                }
            }
            if let Some(bi) = mesh.bone_idx.get(342) {
                eprintln!("RAW342 bi={bi:?} w={:?}", mesh.weights.get(342));
            }
            let mut low = 0usize;
            let mut hist = [0usize; 5];
            for w in &mesh.weights {
                let s = w[0] + w[1] + w[2] + w[3];
                let b = if s < 0.5 {
                    low += 1;
                    0
                } else if s < 0.9 {
                    1
                } else if s < 1.1 {
                    2
                } else {
                    3
                };
                hist[b] += 1;
            }
            eprintln!(
                "WEIGHTSUM n={} low<0.5={} mid={} ok={} hi={} ",
                mesh.weights.len(),
                hist[0],
                hist[1],
                hist[2],
                hist[3]
            );
        }
        let key = mesh.name.to_ascii_lowercase();
        let authored_fbs = HeadFbsInput {
            mouth:head_mouth_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyes:head_eye_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyebrow:head_eyebrow_patterns.get(&key).map(|(_,p)|p.as_slice()),
        };
        rig_mesh(
            &mut mesh,
            "head",
            body_skel.as_ref(),
            head_skel.as_ref(),
            None,
            face_own_skel.as_ref(),
            None,
            Some(&authored_fbs),
            None,
        );
        if std::env::var("OK_CHINDIAG").is_ok() {
            if let Some(sk) = head_skel.as_ref() {
                for n in [
                    "cf_J_ChinLow",
                    "cf_J_Chin_s",
                    "cf_J_ChinTip_Base",
                    "cf_J_CheekLow_s_L",
                    "cf_J_FaceLow_sx",
                    "cf_J_FaceBase",
                ] {
                    if let Some(w) = sk.world_tf(n) {
                        let p = crate::pose::mat_pos(&w);
                        eprintln!("CHINWORLD {n} {p:?}");
                    }
                }
            }
            let mut zero = 0usize;
            let mut tot = 0usize;
            for (i, p) in mesh.positions.iter().enumerate() {
                if p[1] < 1.34 && p[1] > 1.28 && p[0].abs() > 0.03 && p[0].abs() < 0.05 && p[2] > -0.01 {
                    tot += 1;
                    if tot <= 6 {
                        eprintln!(
                            "CHINVERT i={i} p={p:?} bi={:?} w={:?}",
                            mesh.bone_idx.get(i),
                            mesh.weights.get(i)
                        );
                        if let Some(sk) = head_skel.as_ref() {
                            if let Some(bi) = mesh.bone_idx.get(i) {
                                for b in bi {
                                    let name = sk.names.get(*b as usize).map(|s| s.as_str()).unwrap_or("?");
                                    eprintln!("  bone {b} {name}");
                                }
                            }
                        }
                    }
                    if mesh.bone_idx.get(i).map(|b| b[0] == 0).unwrap_or(false) {
                        zero += 1;
                    }
                }
            }
            eprintln!("CHINVERT side_n={tot} bone0={zero}");
        }
        // Material Editor's face MainTex replaces the draw texture after
        // CreateFaceTexture. Only bake the create shader when no replacement
        // is present for this material.
        if let Some(card) = parsed_card.as_ref() {
            profile_tick("genital+mouth+face prep", t_mark); t_mark = Instant::now();
            let t_face_tex = Instant::now();
            if let Some(png) = me_maintex(Some(&card.me_tex), outfit, "head", None, &mesh) {
                mesh.texture_png = Some(png.to_vec());
                // CreateFaceTexture has already installed its RT in the game
                // before MaterialEditor replaces it; no CPU bake is required.
                mark_runtime_main_texture(&mut mesh);
                mark_material_editor_main_texture(&mut mesh);
                eprintln!("ok-extract: face Material Editor MainTex applied bytes={}", png.len());
            } else if let Some(png) = compose_face_texture(
                game_root,
                &vanilla,
                &mod_catalog,
                card,
                &head_texs,
                &body_texs,
                // CreateFaceTexture writes the same card skinMainColor as the body.
                card.skin_color,
                card.skin_sub_color,
                outfit,
                &guid_index,
            ) {
                mesh.texture_png = Some(png);
                mark_runtime_main_texture(&mut mesh);
            } else {
                // A missing list row used to leave cf_O_face with the bundle's
                // empty render texture (solid black). Every card, mod head or
                // not, gets at least the skin colour.
                eprintln!(
                    "ok-extract: face compose failed headId={}, skin fallback",
                    card.head_id
                );
                mesh.texture_png = crate::unity::solid_png(skin_tint, 512, 512);
            }
            profile_tick("compose_face_texture", t_face_tex);
            // MaterialEditor applies texture properties to the draw material
            // after CustomTextureControl has built the face MainTex.  The
            // card's face DetailMask can replace the bundle default entirely;
            // leaving the default here puts its green mask edge on the brow.
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "detailmask") {
                mesh.detail_mask_png = Some(png.to_vec());
                mesh.runtime_textures.remove("_DetailMask");
            }
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "linemask") {
                mesh.line_mask_png = Some(png.to_vec());
            }
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "normalmap") {
                mesh.normal_map_png = Some(png.to_vec());
            }
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "alphamask") {
                mesh.alpha_mask_png = Some(png.to_vec());
            }
        } else if let Some(png) = mesh.texture_png.as_ref() {
            if let Some(t) = crate::unity::tint_png(png, skin_tint) {
                mesh.texture_png = Some(t);
            }
        }
        if std::env::var("OK_FACE_SKIN").is_ok() {
            mesh.texture_png = crate::unity::solid_png(skin_tint, 64, 64);
        }
        if let Some(card)=parsed_card.as_ref() {apply_native_skin_draw_settings(&mut mesh,card,outfit,false);}
        if let Some(card)=parsed_card.as_ref() {apply_native_face_overlays(game_root,&vanilla,&mod_catalog,card,outfit,&mut mesh);}
        // main_skin clips min(max(r,1-_alpha_a),max(g,1-_alpha_b)) < 0.5 on the face too: a card's _AlphaMask
        // (Material Editor) cuts the scalp that would show through its hair. Disabled channels are whitened as for
        // the body, and the face draws masked like the body.
        let face_blend = face_alpha_blend(&mut mesh);
        let (p, tex) =
            emit_part(out_dir, &mut session_parts, "head", "head", &mesh, face_blend, false)?;
        seen_meshes.insert(mesh.name.to_ascii_lowercase());
        parts.push(ManifestPart {
            role: "head".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: face_blend.into(),
            unlit: true,
            ..Default::default()
        });
    }
    profile_tick("face overlays+emit", t_mark);
    let mut seen_head = std::collections::HashSet::new();
    let t_hdet = Instant::now();
    // An OK Studio eyes pattern whose eye set shows a gag eye (ChaControl.ChangeEyesPtn): that object is built, with
    // the set's texture.
    let studio_eyes = crate::studio_status::current().and_then(|o| o.eyes_ptn).and_then(|p| crate::eye_set::eye_set(game_root, p));
    for mut mesh in head_meshes.iter().cloned() {
        let key = mesh.name.to_ascii_lowercase();
        // FBS target references are authoritative, including inactive tears
        // and arbitrarily named mod meshes. Visibility must not remove their
        // controller channels from the session.
        let fbs_target = head_mouth_patterns.contains_key(&key)
            || head_eye_patterns.contains_key(&key)
            || head_eyebrow_patterns.contains_key(&key);
        let shown_gag = studio_eyes.as_ref().is_some_and(|set| crate::eye_set::eye_reference(&mesh.name) >= 3
            && crate::eye_set::part_shown(set, &mesh.name) == Some(true));
        // Pupils of an eye set with an expression (hearts, stars): the texture they draw over themselves.
        if crate::eye_set::eye_reference(&mesh.name) == 2 {
            if let Some(png) = studio_eyes.as_ref().and_then(|set| crate::eye_set::expression_png(game_root, set)) { mesh.shader_extra_textures.insert("_expression".into(), png); }
        }
        if shown_gag {
            if let Some(png) = studio_eyes.as_ref().and_then(|set| crate::eye_set::gag_texture_png(game_root, set)) { mesh.texture_png = Some(png); }
            // Its sprite sheet flips, pulses and turns in the drawing (Koikano/sub_texture_animation).
            if let Some(set) = studio_eyes.as_ref().filter(|set| set.animates_gag()) { set.apply_gag_animation(mesh.mat.get_or_insert_with(Default::default)); }
        } else {
            if !fbs_target && !is_head_detail(&mesh.name) {
                continue;
            }
            if !fbs_target && is_hidden_part(&mesh.name) {
                continue;
            }
        }
        if std::env::var("OK_NOHEADDETAIL").is_ok() {
            continue;
        }
        if let Ok(skip) = std::env::var("OK_SKIP_DETAIL") {
            let ln = mesh.name.to_ascii_lowercase();
            if skip
                .split(',')
                .filter(|s| !s.is_empty())
                .any(|s| ln.contains(&s.to_ascii_lowercase()))
            {
                continue;
            }
        }
        if std::env::var("OK_SKIP_SCLERA").is_ok() {
            let ln = mesh.name.to_ascii_lowercase();
            if (ln == "cf_ohitomi_l" || ln == "cf_ohitomi_r") {
                continue;
            }
        }
        let head_key = mesh.name.to_ascii_lowercase();
        if !seen_head.insert(head_key.clone()) || !seen_meshes.insert(head_key) {
            continue;
        }
        let part_skel = parsed_head.skeleton(mesh.path_id);
        if std::env::var("OK_WDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("hitomi") {
            for i in 0..mesh.bone_idx.len().min(3) {
                let b = mesh.bone_idx[i];
                let w = mesh.weights.get(i).copied().unwrap_or([0.0; 4]);
                let n0 = part_skel
                    .as_ref()
                    .and_then(|s| s.names.get(b[0] as usize).cloned())
                    .unwrap_or_default();
                let n1 = part_skel
                    .as_ref()
                    .and_then(|s| s.names.get(b[1] as usize).cloned())
                    .unwrap_or_default();
                eprintln!(
                    "ok-extract: RAWWD {} v{i} i={:?} w={:?} n0={} n1={}",
                    mesh.name, b, w, n0, n1
                );
            }
        }
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: HDET {} path={} partskel={} binds={} verts={} normals={}",
                mesh.name,
                mesh.path_id,
                part_skel.as_ref().map(|s| s.names.len()).unwrap_or(0),
                mesh.bind_poses.len(),
                mesh.positions.len(),
                mesh.normals.len()
            );
        }
        let key = mesh.name.to_ascii_lowercase();
        let authored_fbs = HeadFbsInput {
            mouth:head_mouth_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyes:head_eye_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyebrow:head_eyebrow_patterns.get(&key).map(|(_,p)|p.as_slice()),
        };
        rig_mesh(
            &mut mesh,
            "head",
            body_skel.as_ref(),
            head_skel.as_ref(),
            None,
            part_skel.as_ref(),
            None,
            Some(&authored_fbs),
            None,
        );
        if let Some(card) = parsed_card.as_ref() {
            resolve_head_detail_texture(
                game_root,
                &vanilla,
                &mod_catalog,
                card,
                &guid_index,
                outfit,
                &mut mesh,
                &head_texs,
                &body_texs,
            );
        }
        if mesh.texture_png.is_none() {
            let ln = mesh.name.to_ascii_lowercase();
            if ln.contains("hitomi") {
                mesh.texture_png = simple_iris_png();
            } else if ln.contains("noseline") || ln.contains("nose") {
                mesh.texture_png = crate::unity::solid_png([0.04, 0.03, 0.04, 0.35], 32, 32);
            } else if ln.contains("eyeline") || ln.contains("mayuge") {
                // A missing line texture must not become an opaque dark quad
                // over the eyes. The game simply has no texture on that pass.
                mesh.texture_png = crate::unity::solid_png([0.0, 0.0, 0.0, 0.0], 32, 32);
            } else if ln.contains("tooth") || ln.contains("canine") || ln.contains("tang") {
                // A missing source MainTex uses the shader's white default.
                mesh.texture_png = crate::unity::solid_png([1.0; 4], 8, 8);
            }
        }
        let stem = format!("head_{}", sanitize(&mesh.name));
        seen_meshes.insert(mesh.name.to_ascii_lowercase());
        let (blend, unlit) = blend_for_mesh("head", &mesh);
        let ln = mesh.name.to_ascii_lowercase();
        let eyeline_up = ln.contains("eyeline") && !ln.contains("low");
        // Unity draws extra materials on the last submesh. rendEyelineUp has
        // materials[1]=EyelineShadowTex * skinMainColor (ChaControl.cs:4046/4073).
        if eyeline_up {
            if let Some(card) = parsed_card.as_ref() {
                // ChaControl.ChangeSettingEyelineUp assigns slot0 and slot1
                // separately. Preserve slot1 identity before looking up ME
                // MainTex/Color; the stroke's replacement belongs only to slot0.
                let mut kage = eyeline_shadow_mesh(&parsed_head,&mesh);
                resolve_head_detail_texture(game_root,&vanilla,&mod_catalog,card,&guid_index,
                    outfit,&mut kage,&head_texs,&body_texs);
                if kage.texture_png.is_some() {
                    head_fbs_sources.insert(kage.name.to_ascii_lowercase(), key.clone());
                    eprintln!("ok-extract: KAGE {} slot1={}",kage.name,
                        kage.mat.as_ref().map(|m|m.name.as_str()).unwrap_or(""));
                    let kstem = format!("head_{}", sanitize(&kage.name));
                    let (kp, ktex) = emit_part(
                        out_dir,
                        &mut session_parts,
                        &kstem,
                        "head",
                        &kage,
                        &blend,
                        unlit,
                    )?;
                    parts.push(ManifestPart {
                        #[cfg(test)]
                        source_mesh: Some(mesh.name.clone()),
                        role: "head".into(),
                        path: kp,
                        guid: None,
                        mesh: kage.name.clone(),
                        texture: ktex,
                        blend: blend.clone(),
                        unlit,
                        ..Default::default()
                    });
                }
            }
        }
        let (p, tex) = emit_part(out_dir, &mut session_parts, &stem, "head", &mesh, &blend, unlit)?;
        parts.push(ManifestPart {
            role: "head".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend,
            unlit,
            ..Default::default()
        });
    }
    profile_tick("head_detail meshes", t_hdet);

    let t_ctx = Instant::now();
    let mut accessory_context=None;
    if let Some(card) = parsed_card.as_ref() {
        let context=std::sync::Arc::new(crate::accessory_extract::Context {
            schema:1,dependencies:crate::accessory_extract::Context::dependencies(card,outfit)?,
            body:body_skel.clone(),head:head_full_skel.as_ref().or(head_skel.as_ref()).cloned(),
            head_textures:head_texs.clone(),body_textures:body_texs.clone(),
            hair_gloss:selected_hair_gloss.as_ref().map(|g|g.0.clone()),
            hair_gloss_sampling:selected_hair_gloss.as_ref().map(|g|[g.1.filter,g.1.wrap_u,g.1.wrap_v,i32::from(g.1.srgb),i32::from(g.1.mip_count>1)]),
        });
        accessory_context=Some(context.clone());
        // OK_FIRSTLOAD=A2: the item passes below keep their meshes here; flushed after the subclothes.
        let mut push_batch = PushBatch::default();
        manifest.card = card_path.map(|path|path.to_string_lossy().into_owned()).unwrap_or_default();
        manifest.marker = card.marker.clone();
        manifest.lastname = card.lastname.clone();
        manifest.firstname = card.firstname.clone();
        manifest.nickname = card.nickname.clone();
        manifest.head_id = card.head_id;
        manifest.hair_ids = card.hair_ids;
        manifest.shape_face = card.shape_face.clone();
        manifest.shape_body = card.shape_body.clone();
        manifest.clothes_ids = card.clothes_ids.clone();
        manifest.hair_color = display_hair(card.hair_color);
        manifest.skin_color = skin_tint;
        manifest.clothes_color = card.clothes_color;
        manifest.sub_parts_id = card.sub_parts_id.clone();
        manifest.outfit = outfit;
        manifest.mapped_sliders = vec!["bone_morph ShapeBodyInfoFemale+ShapeHeadInfoFemale".into()];
        manifest.unmapped_face.clear();
        manifest.unmapped_body.clear();
        let index = &guid_index;
        let mut zip_lists: std::collections::HashMap<String, ChaListCatalog> =
            std::collections::HashMap::new();
        let mut seen_part = std::collections::HashSet::new();
        // Accessory slots already emitted by the KKEx resolve pass, so the
        // vanilla `ChaFileAccessory.parts` pass does not drop the 2nd slot when
        // two slots share the same item id (e.g. the miko sleeve's L/R halves).
        let mut acc_slots_done: std::collections::HashSet<usize> =
            std::collections::HashSet::new();
        let want_outfit = outfit_prefix(outfit);
        profile_tick("accessory context+manifest", t_ctx);
        check_preempted()?;
        let t_res = Instant::now();
        for resolve in &card.resolves {
            if !is_mesh_category(resolve.category_no) {
                continue;
            }
            let p = resolve.property.to_ascii_lowercase();
            if p.contains("outfit") && !p.contains(&want_outfit) {
                continue;
            }
            let role_early = role_for_category(resolve.category_no, &resolve.property);
            if role_early.starts_with("clothes") {
                continue;
            }
            if role_early.starts_with("acc") {
                if let Some(si) = accessory_slot_from_property(&resolve.property) {
                    acc_slots_done.insert(si);
                    // An OK Studio state switches the slot off (ChaFileStatus.showAccessory): not loaded.
                    if crate::studio_status::accessory_hidden(si) { continue; }
                }
            }
            if is_dummy_part("", Some(&resolve.name)) {
                continue;
            }
            let key = format!("{}:{}:{}", resolve.category_no, resolve.slot, resolve.property);
            if !seen_part.insert(key) {
                continue;
            }
            let zip_path = guid_path(&index, &resolve.mod_id);
            if zip_path.is_none() {
                manifest.missing.push(resolve.mod_id.clone());
            }
            let entry = lookup_entry(
                &vanilla,
                &mut zip_lists,
                zip_path.map(|p| p.as_path()),
                &resolve.mod_id,
                resolve.category_no,
                resolve.slot,
                resolve.local_slot,
            );
            let Some(entry) = entry else {
                if zip_path.is_some() {
                    let dummy = ListEntry {
                        category_no: resolve.category_no,
                        id: resolve.slot,
                        name: resolve.name.clone(),
                        main_ab: String::new(),
                        main_data: String::new(),
                        main_tex: String::new(),
                        parent: String::new(),
                        extra: Default::default(),
                    };
                    let role = role_for_category(resolve.category_no, &resolve.property);
                    if role.starts_with("acc") {
                        let source=crate::accessory_extract::Source {entry:dummy.clone(),zip:zip_path.cloned(),guid:Some(resolve.mod_id.clone()),
                            category:resolve.category_no,id:resolve.slot,property:resolve.property.clone(),display:resolve.name.clone(),missing_entry:true};
                        crate::accessory_extract::emit(game_root,card,outfit,&source,&context,out_dir,&mut parts,&mut session_parts,&mut seen_meshes,Some(&mut push_batch))?;
                        continue;
                    }
                    let shake_enabled = if (101..=104).contains(&resolve.category_no) && role.contains("hair") && !role.contains("acc") {
                        !card.hair_parts[(resolve.category_no - 101) as usize].no_shake
                    } else if role.contains("acc") {
                        accessory_slot_from_property(&resolve.property)
                            .and_then(|s| card.accessory.get(s)).is_none_or(|p| !p.no_shake)
                    } else { true };
                    let (mut meshes, part_skel) = meshes_for_entry_shake(
                        game_root, zip_path.map(|p| p.as_path()), &dummy, shake_enabled);
                    if role.contains("hair") && !role.contains("acc") {
                        set_selected_hair_gloss(&mut meshes, selected_hair_gloss.as_ref().map(|g|g.0.as_slice()));
                        if (101..=104).contains(&resolve.category_no) {set_hair_part_materials(&mut meshes,&card.hair_parts[(resolve.category_no-101) as usize]);}
                    }
                    let pk = parent_for_part(card, &resolve.property, "", role);
                    let acc_local = acc_local_for(card, &resolve.property);
                    push_meshes(
                        &mut parts,
                        &mut session_parts,
                        out_dir,
                        game_root,
                        role,
                        meshes,
                        Some(resolve.slot),
                        Some(resolve.category_no),
                        Some(resolve.mod_id.clone()),
                        Some(resolve.property.clone()),
                        Some(resolve.name.clone()),
                        body_skel.as_ref(),
                        head_full_skel.as_ref().or(head_skel.as_ref()),
                        pk.as_deref(),
                        if role.contains("hair") && !role.contains("acc") {
                            Some(hair_cols_for(card, resolve.category_no))
                        } else {
                            None
                        },
                        &mut seen_meshes,
                        part_skel.as_ref(),
                        acc_local.as_ref(),
                        Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                        Some(&card.me_color),
                        Some(&card.me_shader),
                        Some(&card.me_float),
                        outfit,
                        None,
                        accessory_assignment(game_root,zip_path.map(|p|p.as_path()),&dummy).as_ref(), None, Some(&mut push_batch),
                    )?;
                }
                continue;
            };
            let role = role_for_category(resolve.category_no, &resolve.property);
            if role.starts_with("clothes") {
                continue;
            }
            if role.starts_with("acc") {
                let source=crate::accessory_extract::Source {entry:entry.clone(),zip:zip_path.cloned(),
                    guid:Some(resolve.mod_id.clone()),category:resolve.category_no,id:resolve.slot,
                    property:resolve.property.clone(),display:if resolve.name.is_empty(){entry.name.clone()}else{resolve.name.clone()},missing_entry:false};
                crate::accessory_extract::emit(game_root,card,outfit,&source,&context,out_dir,&mut parts,&mut session_parts,&mut seen_meshes,Some(&mut push_batch))?;
                continue;
            }
            let (mut meshes, part_skel) = meshes_for_entry_shake(game_root, zip_path.map(|p|p.as_path()), &entry,
                !(101..=104).contains(&resolve.category_no)||!card.hair_parts[(resolve.category_no-101) as usize].no_shake);
            if role.contains("hair") && !role.contains("acc") {
                set_selected_hair_gloss(&mut meshes, selected_hair_gloss.as_ref().map(|g|g.0.as_slice()));
            }
            let display = if resolve.name.is_empty() {
                entry.name.clone()
            } else {
                resolve.name.clone()
            };
            let pk = parent_for_part(card, &resolve.property, &entry.parent, role);
            let acc_local = acc_local_for(card, &resolve.property);
            let tint = if role.contains("hair") && !role.contains("acc") {
                Some(hair_cols_for(card, resolve.category_no))
            } else {
                None
            };
            if (101..=104).contains(&resolve.category_no) && role.contains("hair") {
                set_hair_part_materials(&mut meshes,&card.hair_parts[(resolve.category_no - 101) as usize]);
            }
            let assignment=if role.contains("acc") {accessory_assignment(game_root,zip_path.map(|p|p.as_path()),&entry)} else {None};
            push_meshes(
                &mut parts,
                &mut session_parts,
                out_dir,
                game_root,
                role,
                meshes,
                Some(resolve.slot),
                Some(resolve.category_no),
                Some(resolve.mod_id.clone()),
                Some(resolve.property.clone()),
                Some(display),
                body_skel.as_ref(),
                head_full_skel.as_ref().or(head_skel.as_ref()),
                pk.as_deref(),
                tint,
                &mut seen_meshes,
                part_skel.as_ref(),
                acc_local.as_ref(),
                Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                Some(&card.me_color),
                Some(&card.me_shader),
                Some(&card.me_float),
                outfit,
                None,
                assignment.as_ref(), None, Some(&mut push_batch),
            )?;
        }
        profile_tick("resolves (hair/acc KKEx)", t_res);
        // Hair slots from ChaFileHair.parts even if UAR skipped them.
        let t_hair = Instant::now();
        for (i, &hid) in card.hair_ids.iter().enumerate() {
            // Id 0 is a real vanilla hairstyle in every hair category (back hair 0 is the default long hair of many
            // cards); empty slots are "なし" catalog entries, which is_dummy_part skips below.
            let cat = 101 + i as i32;
            if std::env::var("OK_DIAG").is_ok() {
                for r in card.resolves.iter().filter(|r| (100..=110).contains(&r.category_no)) {
                    eprintln!(
                        "ok-extract: hairresolve cat={} slot={} local={} guid={} name={} prop={}",
                        r.category_no, r.slot, r.local_slot, r.mod_id, r.name, r.property
                    );
                }
            }
            if parts.iter().any(|p| p.slot == Some(hid) && p.category_no == Some(cat)) {
                continue;
            }
            let resolve = card.resolves.iter().find(|r| {
                (r.slot == hid || r.local_slot == hid)
                    && (r.category_no == cat || (101..=104).contains(&r.category_no))
            });
            let (guid, zip_path, property, display) = if let Some(r) = resolve {
                (
                    r.mod_id.clone(),
                    guid_path(&index, &r.mod_id).cloned(),
                    r.property.clone(),
                    r.name.clone(),
                )
            } else {
                (String::new(), None, format!("ChaFileHair.parts[{i}]"), String::new())
            };
            let entry = lookup_entry(
                &vanilla,
                &mut zip_lists,
                zip_path.as_deref(),
                &guid,
                cat,
                hid,
                hid,
            );
            if std::env::var("OK_DIAG").is_ok() {
                eprintln!(
                    "ok-extract: hair id={hid} cat={cat} resolve={:?} entry={:?}",
                    resolve.map(|r| (&r.mod_id, &r.name, &r.property, r.slot, r.local_slot, r.category_no)),
                    entry.as_ref().map(|e| (&e.name, &e.main_data, e.category_no))
                );
            }
            let Some(entry) = entry else {
                eprintln!("ok-extract: hair slot {hid} cat {cat} not in catalog");
                continue;
            };
            if is_dummy_part(&entry.main_data, Some(&entry.name))
                || is_dummy_part("", Some(&display))
            {
                eprintln!("ok-extract: skip dummy hair id={hid} cat={cat} {display}");
                continue;
            }
            let role = role_for_category(cat, &property);
            let (mut meshes, part_skel) = meshes_for_entry_shake(
                game_root, zip_path.as_deref(), &entry, !card.hair_parts[i].no_shake);
            set_selected_hair_gloss(&mut meshes, selected_hair_gloss.as_ref().map(|g|g.0.as_slice()));
            set_hair_part_materials(&mut meshes,&card.hair_parts[i]);
            let name = if display.is_empty() {
                entry.name.clone()
            } else {
                display
            };
            eprintln!(
                "ok-extract: extra hair id={hid} cat={cat} meshes={} name={name}",
                meshes.len()
            );
            push_meshes(
                &mut parts,
                &mut session_parts,
                out_dir,
                game_root,
                role,
                meshes,
                Some(hid),
                Some(cat),
                if guid.is_empty() { None } else { Some(guid) },
                Some(property),
                Some(name),
                body_skel.as_ref(),
                head_full_skel.as_ref().or(head_skel.as_ref()),
                Some("cf_J_FaceUp_ty"),
                Some(hair_cols_for(card, cat)),
                &mut seen_meshes,
                part_skel.as_ref(),
                None,
                Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                Some(&card.me_color),
                Some(&card.me_shader),
                Some(&card.me_float),
                outfit,
                None,
                None, None, Some(&mut push_batch),
            )?;
        }
        profile_tick("hair slots", t_hair);
        // Accessories from ChaFileAccessory.parts (slots with no KKEx entry, e.g. necklaces).
        let t_acc = Instant::now();
        for (slot_i, a) in card.accessory.iter().enumerate() {
            // ChaFileAccessory uses category 120 for an empty slot. Zero is
            // also a valid item id in other categories (e.g. stock glasses).
            if a.type_id == 120 || !is_mesh_category(a.type_id) {
                continue;
            }
            let role = role_for_category(a.type_id, "");
            if role.starts_with("clothes") {
                continue;
            }
            // ChangeAccessory only instantiates accessory categories; a slot
            // saved with another type shows nothing in the game.
            if !role.starts_with("acc") {
                eprintln!("ok-extract: accessory[{slot_i}] cat={} id={} is not an accessory category; skipped", a.type_id, a.id);
                continue;
            }
            // Skip only the slots the resolve pass already emitted; dedup by
            // (category,id) would drop a second slot sharing the same item id.
            if acc_slots_done.contains(&slot_i) || crate::studio_status::accessory_hidden(slot_i) {
                continue;
            }
            // ChaFileAccessory.parts stores the card's local id. Slots with a
            // KKEx resolve are added by the `card.resolves` pass above; what is
            // left here has no mod resolve, so the id is a *vanilla* item. A mod
            // catalog entry sharing the same (category, id) must not shadow it
            // (e.g. cat124 id14 is the vanilla `p_acs_neck_star`, not a mod item
            // that happens to also use id 14).
            let entry = vanilla
                .get(a.type_id, a.id)
                .cloned()
                .or_else(|| mod_catalog.get(a.type_id, a.id).cloned());
            let Some(entry) = entry else {
                eprintln!(
                    "ok-extract: accessory[{slot_i}] cat={} id={} not in catalog",
                    a.type_id, a.id
                );
                continue;
            };
            if is_dummy_part(&entry.main_data, Some(&entry.name)) {
                continue;
            }
            let source=crate::accessory_extract::Source {zip:entry.extra.get("__zip").map(PathBuf::from),
                guid:None,category:a.type_id,id:a.id,property:format!("outfit{outfit}.accessory{slot_i}"),
                display:entry.name.clone(),entry,missing_entry:false};
            crate::accessory_extract::emit(game_root,card,outfit,&source,&context,out_dir,&mut parts,&mut session_parts,&mut seen_meshes,Some(&mut push_batch))?;
        }
        profile_tick("accessories", t_acc);
        // Clothes from ChaFileClothes.parts (per outfit).
        // Sideloader's UniversalAutoResolver.ResolveStructure identifies an item
        // by (GUID, original Slot) from the per-outfit ResolveInfo whose
        // Property == "outfitN.ChaFileClothes.<Name>" (StructReference
        // ChaFileClothesGenerator). The id stored in the card is a resolved
        // LocalSlot that goes stale when the mod updates, so the ResolveInfo is
        // the authority. Only fall back to the card id for vanilla items.
        let t_clo = Instant::now();
        let bra_over_mask = over_layer_mask_png(game_root, card, &vanilla, &mod_catalog, &index, outfit, "OverBraMask");
        let inner_over_mask = over_layer_mask_png(game_root, card, &vanilla, &mod_catalog, &index, outfit, "OverInnerMask");
        let mut top_type = 0;
        let mut top_not_bra = false;
        let mut not_bot = false;
        let mut sub_not_bra = [None; 2];
        let mut hide_shorts_bot = false;
        let mut bottom_present = false;
        let mut not_shorts = false;
        let mut top_prefab_world = crate::pose::ident();
        for (kind, &cid) in card.clothes_ids.iter().enumerate() {
            if kind >= CLOTHES_PROP.len() {
                break;
            }
            // 105..=111 for parts[0..6]; both shoe slots are category 112.
            let cat = if kind < 7 { 105 + kind as i32 } else { 112 };
            let Some((entry, guid, zip_path)) = selected_clothes.as_ref()
                .and_then(|slots| slots.get(kind))
                .and_then(|slot| slot.as_ref()).cloned() else {
                continue;
            };
            if kind == 0 {
                top_type = entry.info("Kind").parse().unwrap_or(0);
                top_not_bra = entry.info("NotBra").trim() == "1";
                not_bot = entry.info("Coordinate").trim() == "2";
            } else if kind == 1 {
                let listed = entry.info("HideShorts");
                hide_shorts_bot = if listed.is_empty() {
                    [2, 3, 9, 16, 22, 25, 28, 29, 30, 32, 34, 35, 45].contains(&cid)
                } else {
                    listed.trim() == "1"
                };
            } else if kind == 2 {
                not_shorts = entry.info("Coordinate").trim() == "2";
            }
            if !crate::unity::clothes_kind_visible(kind, &manifest.clothes_states, manifest.shoes_type) {
                continue;
            }
            if is_dummy_part(&entry.main_data, Some(&entry.name)) {
                continue;
            }
            if kind == 1 && not_bot {
                continue;
            }
            let zip = entry
                .extra
                .get("__zip")
                .map(PathBuf::from)
                .or_else(|| zip_path.clone());
            let t_part = Instant::now();
            let (mut meshes, part_skel) = meshes_for_entry(game_root, zip.as_deref(), &entry);
            profile_tick("clothes meshes_for_entry", t_part);
            if kind == 0 { top_prefab_world = part_skel.as_ref().map_or_else(crate::pose::ident, |s| s.root_world()); }
            if meshes.is_empty() {
                continue;
            }
            if kind == 1 {
                bottom_present = true;
            }
            resolve_material_alpha_masks(game_root, &mut meshes);
            eprintln!(
                "ok-extract: clothes kind={kind} cat={cat} id={cid} meshes={} entry={}",
                meshes.len(),
                entry.name
            );
            let cols = card
                .clothes_colors
                .get(kind)
                .copied()
                .unwrap_or([[1.0, 1.0, 1.0, 1.0]; 4]);
            let t_part = Instant::now();
            let pats = build_clothes_patterns(
                game_root,
                &vanilla,
                &mod_catalog,
                card,
                &guid_index,
                kind,
                0,
                &head_texs,
                &body_texs,
                outfit,
            );
            profile_tick("clothes patterns", t_part);
            // ChaControl.CreateClothesTexture + ChangeCustomClothes: up to 3
            // create RTs (MainTex / MainTex02 / MainTex03) assigned to
            // rendNormal01 / rendNormal02 / rendNormal03.
            let t_part = Instant::now();
            let cmp = accessory_bundle_bytes(game_root, zip.as_deref(), &entry)
                .and_then(|b| crate::unity::clothes_renderer_map(&b, &entry.main_data));
            profile_tick("clothes renderer map", t_part);
            let t_part = Instant::now();
            let (baked, used_mask) = bake_clothes_create_textures(
                game_root,
                &entry,
                cols,
                &pats,
                &head_texs,
                &body_texs,
            );
            if baked[0].is_none() && std::env::var("OK_DIAG").is_ok() {
                eprintln!(
                    "ok-extract: clothes no list MainTex cat={cat} id={cid} mt='{}' zip={:?}",
                    entry.info("MainTex"),
                    entry.extra.get("__zip")
                );
            }
            profile_tick("clothes bake create", t_part);
            let t_part = Instant::now();
            apply_clothes_create_textures(&mut meshes, cmp.as_ref(), &baked);
            profile_tick("clothes apply create", t_part);
            let t_part = Instant::now();
            finalize_clothes_texture(&mut meshes);
            profile_tick("clothes finalize", t_part);
            let t_part = Instant::now();
            apply_clothes_emblems(game_root, &vanilla, &mod_catalog, card, kind, &mut meshes, &head_texs, &body_texs);
            profile_tick("clothes emblems", t_part);
            apply_clothes_overlay(card,outfit,["ct_clothesTop","ct_clothesBot","ct_bra","ct_shorts","ct_gloves","ct_panst","ct_socks","ct_shoes_inner","ct_shoes_outer"][kind],&mut meshes,cmp.as_ref(),&baked);
            if std::env::var("OK_CLOTHDIAG").is_ok() {
                for m in &meshes {
                    if let Some(png) = m.texture_png.as_deref() {
                        let slot = cmp
                            .as_ref()
                            .and_then(|mp| clothes_create_slot(&m.renderer_ids, mp))
                            .unwrap_or(0);
                        eprintln!(
                            "ok-extract: CLOTHDIAG kind={kind} cat={cat} mesh={} slot={slot} used={} col0={:?} mean={:?}",
                            m.name,
                            used_mask,
                            cols[0],
                            png_mean(png)
                        );
                    }
                }
            }
            let role = role_for_category(cat, "");
            let t_part = Instant::now();
            // ChaControl loads top/bottom with copyDynamicBone=true: the prefab
            // components retarget their roots onto aaWeightsBody before skinning.
            {
                if let (Some(part), Some(body), Some((bytes,source_zip))) = (
                    part_skel.as_ref(), body_skel.as_mut(),
                    accessory_bundle_source(game_root, zip.as_deref(), &entry),
                ) {
                    let parsed = crate::unity::parse_bundle_cached(bytes);
                    parsed.configure_game_references_from_zip(game_root,source_zip.as_deref());
                    clothes_dynamics.extend(crate::dbone::implant::shared_clothes(&parsed, part, body,
                        game_root, clothes_load_settings(&format!("outfit{outfit}.clothes{kind}")).1, None, kind<=1));
                }
            }
            profile_tick("clothes dynamics", t_part);
            let t_part = Instant::now();
            push_meshes(
                &mut parts,
                &mut session_parts,
                out_dir,
                game_root,
                role,
                meshes,
                Some(cid),
                Some(cat),
                if guid.is_empty() { None } else { Some(guid) },
                Some(format!("outfit{outfit}.clothes{kind}")),
                Some(entry.name.clone()),
                body_skel.as_ref(),
                head_skel.as_ref(),
                None,
                if used_mask {
                    None
                } else {
                    Some([cols[0]; 3])
                },
                &mut seen_meshes,
                part_skel.as_ref(),
                None,
                Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                Some(&card.me_color),
                Some(&card.me_shader),
                Some(&card.me_float),
                outfit,
                if kind == 2 {
                    let (scale, offset) = if entry.info("Coordinate") == "2" { (2.0, -1.0) } else { (1.0, 0.0) };
                    bra_over_mask.as_deref().map(|mask| (mask, scale, offset))
                } else { None },
                None, None, Some(&mut push_batch),
            )?;
            profile_tick("clothes push", t_part);
        }
        profile_tick("clothes", t_clo);
        // Sub-clothes: ChaFileClothes.subPartsId (ChaFileDefine.ClothesSubKind
        // partsA/B/C). ChaControl.ChangeClothesTopPartsNoAsync loads them only
        // when the top exists (Kind 1 or 2), at category
        // `(top.Kind == 1 ? 200 : 210) + kind`, parented under ct_clothesTop and
        // skinned to the body. Try jacket (210+kind) then sailor (200+kind).
        let t_sub = Instant::now();
        {
            // ChaControl.ChangeClothesTopPartsAsync picks category
            // `(1 == topType ? 200 : 210) + kind` from the top's list Kind.
            // The top's Kind selects the subpart list even when the saved ID
            // is zero. ID 0 is a real vanilla item (e.g. jacket subpart A),
            // not an absent-part sentinel. Resolved mod entries below retain
            // their explicit category and take precedence.
            for kind in 0..3usize {
                if manifest.clothes_states[0] == 3 {
                    continue;
                }
                let sid = card.sub_parts_id.get(kind).copied().unwrap_or(0);
                // Sailor sub-clothes are categories 200/201/202 and jackets
                // 210/211/212. `ChaControl.ChangeClothesTopPartsAsync` selects
                // `(1 == topType ? 200 : 210) + kind`; Sideloader records which
                // one is equipped via the ResolveInfo Property name
                // ("ClothesSailorSubX" / "ClothesJacketSubX"), which also pins
                // the item to (GUID, original Slot) so a stale card id does not
                // load the wrong mod.
                let Some((cat, entry, guid, zip_path)) = selected_top_subparts.as_ref()
                    .and_then(|sub| sub.get(kind))
                    .and_then(|choice| choice.as_ref()).cloned() else {
                    continue;
                };
                if is_dummy_part(&entry.main_data, Some(&entry.name)) {
                    continue;
                }
                if kind < 2 {
                    sub_not_bra[kind] = Some(entry.info("NotBra").trim() == "1");
                }
                let zip = entry
                    .extra
                    .get("__zip")
                    .map(PathBuf::from)
                    .or_else(|| zip_path.clone());
                let (mut meshes, part_skel) =
                    meshes_for_entry(game_root, zip.as_deref(), &entry);
                if meshes.is_empty() {
                    continue;
                }
                resolve_material_alpha_masks(game_root, &mut meshes);
                eprintln!(
                    "ok-extract: subclothes kind={kind} cat={cat} id={sid} meshes={} entry={} ab={} data={}",
                    meshes.len(),
                    entry.name, entry.main_ab, entry.main_data
                );
                // ChaControl.ChangeCustomClothes(false, kind, ...) reads the
                // *top* part's colors for every sub kind, and sub-kind C
                // (`!main && 2 == kind`) takes `_Color` from `colorInfo[3]`
                // instead of `colorInfo[0]`.
                let top_cols = card
                    .clothes_colors
                    .first()
                    .copied()
                    .unwrap_or([[1.0, 1.0, 1.0, 1.0]; 4]);
                let ch0 = if kind == 2 { 3 } else { 0 };
                let mut cols = [[1.0f32; 4]; 4];
                cols[0] = top_cols[ch0];
                cols[1] = top_cols[1];
                cols[2] = top_cols[2];
                let pats = build_clothes_patterns(
                    game_root,
                    &vanilla,
                    &mod_catalog,
                    card,
                    &guid_index,
                    0,
                    ch0,
                    &head_texs,
                    &body_texs,
                    outfit,
                );
                let cmp = accessory_bundle_bytes(game_root, zip.as_deref(), &entry)
                    .and_then(|b| crate::unity::clothes_renderer_map(&b, &entry.main_data));
                let (baked, used_mask) = bake_clothes_create_textures(
                    game_root,
                    &entry,
                    cols,
                    &pats,
                    &head_texs,
                    &body_texs,
                );
                apply_clothes_create_textures(&mut meshes, cmp.as_ref(), &baked);
                finalize_clothes_texture(&mut meshes);
                #[cfg(test)]
                if let Ok(folder)=std::env::var("OK_AUDIT_SUBPART_OUTPUT") {
                    let folder=PathBuf::from(folder);std::fs::create_dir_all(&folder).unwrap();
                    let prefix=format!("outfit{outfit}_sub{kind}");
                    for (i,png) in baked.iter().enumerate() {if let Some(png)=png {std::fs::write(folder.join(format!("{prefix}_baked{i}.png")),png).unwrap();}}
                    std::fs::write(folder.join(format!("{prefix}.json")),serde_json::to_vec(&serde_json::json!({
                        "colors":cols,"top_colors":top_cols,"entry":entry.extra,
                        "map":cmp.as_ref().map(|m|format!("{m:?}")),
                        "meshes":meshes.iter().map(|m|serde_json::json!({"name":m.name,"renderer_ids":m.renderer_ids})).collect::<Vec<_>>()
                    })).unwrap()).unwrap();
                }
                apply_clothes_emblems(game_root, &vanilla, &mod_catalog, card, 0, &mut meshes, &head_texs, &body_texs);
                apply_clothes_overlay(card,outfit,["ct_top_parts_A","ct_top_parts_B","ct_top_parts_C"][kind],&mut meshes,cmp.as_ref(),&baked);
                let role = role_for_category(cat, "");
                // Top subparts use the same copyDynamicBone path as the top.
                if let (Some(part), Some(body), Some((bytes,source_zip))) = (
                    part_skel.as_ref(), body_skel.as_mut(),
                    accessory_bundle_source(game_root, zip.as_deref(), &entry),
                ) {
                    let parsed = crate::unity::parse_bundle_cached(bytes);
                    parsed.configure_game_references_from_zip(game_root,source_zip.as_deref());
                    clothes_dynamics.extend(crate::dbone::implant::shared_clothes(&parsed, part, body,
                        game_root, clothes_load_settings(&format!("outfit{outfit}.clothesTopParts{kind}")).1, Some(&top_prefab_world),true));
                }
                push_meshes(
                    &mut parts,
                    &mut session_parts,
                    out_dir,
                    game_root,
                    role,
                    meshes,
                    Some(sid),
                    Some(cat),
                    if guid.is_empty() { None } else { Some(guid) },
                    Some(format!("outfit{outfit}.clothesTopParts{kind}")),
                    Some(entry.name.clone()),
                    body_skel.as_ref(),
                    head_skel.as_ref(),
                    None,
                    if used_mask {
                        None
                    } else {
                        Some([cols[0]; 3])
                    },
                    &mut seen_meshes,
                    part_skel.as_ref(),
                    Some(&top_prefab_world),
                    Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                    Some(&card.me_color),
                    Some(&card.me_shader),
                    Some(&card.me_float),
                    outfit,
                    if kind == 0 { inner_over_mask.as_deref().map(|mask| (mask, 1.0, 0.0)) } else { None },
                    None, None, Some(&mut push_batch),
                )?;
            }
        }
        profile_tick("subclothes", t_sub);
        let t_batch = Instant::now();
        push_batch.flush(&mut parts, &mut session_parts)?;
        profile_tick("deferred meshes", t_batch);
        // ChaControl.DrawClothes disables the bra renderer when the selected
        // top (or its sailor/jacket subparts) sets NotBra. The subpart rules
        // replace the top's value exactly as ChangeClothesTopPartsAsync does.
        let not_bra = match top_type {
            1 => sub_not_bra[0].unwrap_or(top_not_bra),
            2 if sub_not_bra.iter().any(Option::is_some) =>
                sub_not_bra.iter().any(|value| *value == Some(true)),
            _ => top_not_bra,
        };
        if not_bra {
            parts.retain(|part| part.role != "clothes_bra");
            session_parts.retain(|part| part.role != "clothes_bra");
        }
        // DrawClothes suppresses shorts if the bra occupies both coordinates,
        // or if a full worn bottom requests HideShorts. The latter uses the
        // list's HideShorts cell, falling back to vanilla botHideIds only when
        // that cell is absent.
        if not_shorts || (hide_shorts_bot && bottom_present && manifest.clothes_states[1] == 0) {
            parts.retain(|part| part.role != "clothes_shorts");
            session_parts.retain(|part| part.role != "clothes_shorts");
        }
        eprintln!("ok-extract: top_type={top_type} top_not_bra={top_not_bra} sub_not_bra={sub_not_bra:?} bra_visible={} hide_shorts_bot={hide_shorts_bot} not_shorts={not_shorts} shorts_visible={}", !not_bra, !not_shorts && !(hide_shorts_bot && bottom_present && manifest.clothes_states[1] == 0));
    }
    if parts.is_empty() && session_parts.is_empty() {
        return Err("no meshes extracted".into());
    }
    manifest.skin_color = skin_tint;
    manifest.parts = parts;
    #[cfg(test)]
    if let Ok(path)=std::env::var("OK_SOURCE_PART_MANIFEST_OUTPUT") {
        // Test-only provenance: preserve the existing emission order and card
        // property identity without exporting/reimporting geometry or textures.
        let bytes=serde_json::to_vec_pretty(&manifest).map_err(|e|e.to_string())?;
        std::fs::write(path,bytes).map_err(|e|e.to_string())?;
    }
    if let Some(dir) = out_dir {
        let manifest_path = dir.join("manifest.json");
        let json = serde_json::to_string_pretty(&manifest).map_err(|e| e.to_string())?;
        std::fs::write(&manifest_path, json).map_err(|e| e.to_string())?;
        if let Some(c) = card_path {
            let _ = std::fs::write(dir.join(".ok_stamp"), stamp_text(game_root, c, outfit));
        }
    }
    profile_tick("clothes+hair+write", t);
    let t_keys = Instant::now();
    let body_keys = crate::shape::load_keys_named(game_root, "chara/oo_base.unity3d", "cf_anmShapeBody");
    let head_keys = head_shape_keys;
    profile_tick("load shape keys (session)", t_keys);
    clear_entry_prefetch();
    profile_tick("extract_card_outfit TOTAL", t_all);
    for (name, s) in stages.map(crate::appearance::timings::Capture::finish).unwrap_or_default() {
        eprintln!("ok-extract: STAGE {name:28} {:8.1}ms x{} max {:.1}ms", s.total_ms, s.calls, s.max_ms);
    }
    crate::profile::dump_spots();
    let (shape_body, shape_face, sex, type_bone, nip, abm) = parsed_card
        .as_ref()
        .map(|c| {
            (
                c.shape_body.clone(),
                c.shape_face.clone(),
                c.sex,
                c.type_bone,
                c.nip_stand_rate,
                c.abm.clone(),
            )
        })
        .unwrap_or_else(|| (Vec::new(), Vec::new(), 0, 0, 0.0, Vec::new()));
    let sexfaces_installed = game_root.join("BepInEx/plugins/KKS_SexFaces.dll").ok_is_file();
    if let Some((_, sampling)) = selected_hair_gloss.as_ref() {
        // ChaControl.LoadHairGlossMask selects one Texture2D for the card;
        // HairAccessoryCustomizer can attach the same asset to mod hair.
        // Preserve that asset's sampler and mip policy for every part that
        // actually received it, independent of renderer/card names.
        for part in &mut session_parts {
            if part.hair_gloss.is_some() {
                part.runtime_textures.insert("_HairGloss".into(), [
                    sampling.filter, sampling.wrap_u, sampling.wrap_v,
                    i32::from(sampling.srgb), i32::from(sampling.mip_count > 1),
                ]);
            }
        }
    }
    for part in &mut session_parts {
        let display_key = part.name.to_ascii_lowercase();
        let key = head_fbs_sources.get(&display_key).unwrap_or(&display_key);
        if part.role == "head" {
            // ChaControl.UpdateVisible selects tears independently of eye
            // patterns; default tearsLv=0 hides all three referenced objects.
            // Retain their FBS data while keeping the preview draw state.
            if let Some(source) = head_meshes.iter().find(|mesh| mesh.name.eq_ignore_ascii_case(key)) {
                // ChaReference resolves GameObjects, not the Mesh asset name.
                // A mod can rename the asset without changing its tear object.
                if is_hidden_head_reference(source) {
                    // An OK Studio state shows the tears object of its level (ChaControl.ChangeTearsLv); otherwise
                    // the hidden references stay hidden.
                    let tears = source.renderer_names.iter().map(String::as_str).chain(std::iter::once(source.name.as_str())).find_map(crate::studio_status::tears_size);
                    part.render_visible = match (tears, crate::studio_status::current().and_then(|o| o.tears_lv)) {
                        (Some(size), Some(level)) => size == level,
                        _ => false,
                    };
                }
                // The OK Studio eye set hides n_eyebase / n_hitomi and shows its gag eye.
                if let Some(set) = crate::studio_status::current().and_then(|o| o.eyes_ptn).and_then(|p| crate::eye_set::eye_set(game_root, p)) {
                    for name in source.renderer_names.iter().map(String::as_str).chain(std::iter::once(source.name.as_str())) {
                        match crate::eye_set::part_shown(&set, name) {
                            Some(false) => { part.render_visible = false; break; }
                            Some(true) => { part.render_visible = true; break; }
                            None => {}
                        }
                    }
                }
                // ChaReference resolves the actual head GameObject, rather than
                // treating every mesh with canine in its name as double teeth.
                if is_double_tooth_reference(source) {
                    part.render_visible = parsed_card.as_ref().is_some_and(|card| card.double_tooth);
                    eprintln!("ok-extract: doubleTooth name={} visible={}", part.name, part.render_visible);
                }
            }
        }
        if let Some((target_index, patterns)) = head_mouth_patterns.get(key) {
            part.mouth_patterns = patterns.clone();
            if sexfaces_installed {
                let raw_x = head_meshes.iter()
                    .find(|m| m.name.eq_ignore_ascii_case(&part.name))
                    .map(|m| m.positions.iter().map(|p| p[0]).collect::<Vec<_>>());
                apply_sexfaces_mouth_extensions(part, *target_index, raw_x.as_deref());
            }
        }
        if part.role.contains("head") || part.role.contains("face") {
            part.eye_patterns=head_eye_patterns.get(key).map(|(_,p)|p.clone()).unwrap_or_default();
            part.eyebrow_patterns=head_eyebrow_patterns.get(key).map(|(_,p)|p.clone()).unwrap_or_default();
            let eyes=f32::from_bits(EYES_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed));
            let brow=f32::from_bits(EYEBROW_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed));
            part.face_default_rates=[eyes,brow];
            part.face_runtime_settings=head_face_settings;
            if let Some(settings)=head_eye_materials.get(key) {
                if let Some(raw)=head_meshes.iter().find(|mesh|mesh.name.eq_ignore_ascii_case(key)) {
                    let mut runtime=crate::eye_gaze::IrisRuntime {settings:*settings,raw_uvs:raw.uvs.clone()};
                    if let Some(card)=parsed_card.as_ref() {
                        let lerp=|a:f32,b:f32,t:f32| a+(b-a)*t.clamp(0.,1.);
                        runtime.settings[10]=lerp(0.2,-0.6,card.pupil_x)*if settings[0]==1. {-1.}else{1.};
                        runtime.settings[11]=lerp(-0.5,0.5,card.pupil_y);
                        runtime.settings[12]=lerp(1.8,-0.2,card.pupil_width);
                        runtime.settings[13]=lerp(1.8,-0.2,card.pupil_height);
                    }
                    runtime.apply(&mut part.uvs,0.,0.);
                    if let Some(card)=parsed_card.as_ref() {
                        part.eye_draw=source_eye_draw(game_root,&vanilla,&mod_catalog,card,raw,&guid_index);
                        if let Some(draw)=part.eye_draw.as_mut() {
                            draw.apply(runtime.st(0.,0.),&runtime.raw_uvs,&mut part.uv2,&mut part.uv3);
                            // EyeLookMaterialControll overrides MainTex ST each Update.
                            // Its transform is already on the dynamic UV stream.
                            part.main_tex_st=[1.,1.,0.,0.];
                        }
                    }
                    part.iris_runtime=Some(runtime);
                }
            }
            // The baseline is what apply_default_fbs baked into this mesh, with the inputs it had (an absent
            // controller as absent). A CPU head bakes the OBJ preview's pattern 0 at 80% open: assuming it closed
            // made every live mouth open twice, the tongue pushed out through the chin.
            fn none_if_empty(p:&[(i32,i32)])->Option<&[(i32,i32)]> {(!p.is_empty()).then_some(p)}
            let input=HeadFbsInput {mouth:none_if_empty(&part.mouth_patterns),
                eyes:none_if_empty(&part.eye_patterns),eyebrow:none_if_empty(&part.eyebrow_patterns)};
            let channels:Vec<&str>=part.blend_shapes.iter().map(|(name,_)|name.to_str().unwrap_or("")).collect();
            part.face_baseline_weights=default_fbs_weights(&part.name,&channels,Some(&input));
        }
    }
    // KK_InvisibleBody turns off every renderer under the body and head bones (body, face, eyes, hair) but not the
    // accessories at their attach points; the clothes hang under BodyTop and stay.
    if parsed_card.as_ref().is_some_and(|card| card.invisible_body) {
        for part in session_parts.iter_mut().filter(|p| !p.role.starts_with("clothes") && !p.role.starts_with("acc")) {
            part.render_visible = false;
        }
    }
    assign_draw_layers(&mut session_parts);
    Ok(crate::session::OkSession {
        accessory_context,
        editor_pregnancy:Default::default(),
        pv_config,pv_mask:15,pv_fk:false,
        eye_look_config,
        eye_look_runtime:None,
            head_look:crate::head_look::HeadLook::default(),
            physx_secondary:false,
            external_animation:None,
            scene_gpu_parts:Vec::new(),
            physics:Default::default(),
            body_interaction:None,
        game_root: game_root.to_path_buf(),
        ramp_png,
        shadow_depth: configured_shadow_depth(game_root),
        line_width: configured_line_width(game_root),
        line_depth: configured_line_depth(game_root),
        kano_ver_shift: configured_kano_ver_shift(game_root),
        body_rest,
        body_dynamics,
        body_dynamic_runtime: None,
        yure_policy:None,
        body_colliders,
        clothes_dynamics,
        clothes_dynamic_runtime: None,
        bust_softness: parsed_card.as_ref().map_or(0.5, |c| c.bust_softness),
        bust_weight: parsed_card.as_ref().map_or(0.5, |c| c.bust_weight),
        collider_settings: crate::dbone::game_colliders::Settings::load(game_root),
        collider_overrides: parsed_card.as_ref().map_or(crate::dbone::game_colliders::CardOverrides::default(), |c| c.collider_overrides),
        head_rest,
        body_skel,
        head_skel,
        body_keys,
        head_keys,
        hand_shapes: crate::shape::hand::HandShapes::load(game_root),
        user_fk: Vec::new(),
        user_fk_ids: Default::default(),
        hand_shape_ids: Default::default(),
        studio_ik_solver: None,
        studio_ik: Vec::new(),
            studio_ik_rotations: Vec::new(),
        correct,
        sex,
        type_bone,
        nip_stand_rate: nip,
        shape_body,
        shape_face,
        abm,
        parts: session_parts,
        coordinate_count: parsed_card
            .as_ref()
            .map(|c| c.coordinate_count)
            .unwrap_or(0),
        skel_export: None,
        anim_time: f32::MAX,
        paired_ik_runtime: None,
        anim_clip: std::env::var("OK_POSE").unwrap_or_else(|_| "Stand_D_00".to_string()),
        anim_scene_mode: 0,
        anim_parameters: std::collections::HashMap::new(),
        anim_clock:Default::default(),anim_crossfade:None,anim_layer_crossfades:Default::default(),
        body_shaped: None,
        shape_reference_scratch: None,
        head_shaped: None,
        head_full_rest,
        head_full_posed: head_full_skel,
    })
}

/// Preview visibility follows referenced GameObjects when a mod renames Mesh.
fn is_hidden_head_reference(mesh: &crate::unity::ExtractedMesh) -> bool {
    if mesh.renderer_names.is_empty() {
        is_hidden_part(&mesh.name)
    } else {
        mesh.renderer_names.iter().any(|name| is_hidden_part(name))
    }
}

#[cfg(test)]
#[test]
fn hidden_head_visibility_uses_renderer_reference() {
    for target in ["cf_O_namida_S", "cf_O_namida_M", "cf_O_namida_L"] {
        let mut mesh=crate::unity::ExtractedMesh {name:"custom_mod_asset".into(),
            renderer_names:vec![target.into()],..Default::default()};
        assert!(is_hidden_head_reference(&mesh));
        mesh.name=target.into();
        mesh.renderer_names=vec!["custom_visible_eye".into()];
        assert!(!is_hidden_head_reference(&mesh));
        mesh.renderer_names.clear();
        assert!(is_hidden_head_reference(&mesh));
    }
}

/// ChaReference resolves ObjDoubleTooth by GameObject name cf_O_canine.
fn is_double_tooth_reference(mesh: &crate::unity::ExtractedMesh) -> bool {
    if mesh.renderer_names.is_empty() {
        mesh.name.eq_ignore_ascii_case("cf_O_canine")
    } else {
        mesh.renderer_names.iter().any(|name| name.eq_ignore_ascii_case("cf_O_canine"))
    }
}

#[cfg(test)]
#[test]
fn double_tooth_reference_uses_renderer_identity() {
    let mut mesh = crate::unity::ExtractedMesh { name: "renamed_mod_mesh".into(),
        renderer_names: vec!["cf_O_canine".into()], ..Default::default() };
    assert!(is_double_tooth_reference(&mesh));
    mesh.renderer_names = vec!["another_canine".into()];
    assert!(!is_double_tooth_reference(&mesh));
    mesh.name = "cf_O_canine".into();
    assert!(!is_double_tooth_reference(&mesh), "mesh name cannot override source GameObject identity");
    mesh.renderer_names.clear();
    assert!(is_double_tooth_reference(&mesh));
    mesh.name = "mod_canine_attachment".into();
    assert!(!is_double_tooth_reference(&mesh));
}

/// KKS_SexFaces/FBSExtensions.cs PatchFaces(): the installed plugin extends the
/// prefab's 43 serialized PtnSet rows at runtime. Keep the source rows as the
/// authority and apply the same target-index rules, including its sigmoid in
/// unskinned mesh space for the six lopsided face shapes.
fn apply_sexfaces_mouth_extensions(
    part: &mut crate::session::SessionPart,
    target_index: usize,
    raw_x: Option<&[f32]>,
) {
    if target_index >= 5 || part.mouth_patterns.len() != 43 {
        return;
    }
    if target_index == 0 && raw_x.map_or(true, |x| x.len() != part.positions.len()) {
        eprintln!("ok-extract: SexFaces face vertex count mismatch for {}", part.name);
        return;
    }
    if [2usize, 11, 12, 19, 24, 27, 28, 37, 39].iter().any(|&pattern| {
        let (close, open) = part.mouth_patterns[pattern];
        close < 0 || open < 0 || close as usize >= part.blend_shapes.len()
            || open as usize >= part.blend_shapes.len()
    }) || (target_index == 0 && {
        let max_x = raw_x.unwrap().iter().copied().fold(f32::NEG_INFINITY, f32::max);
        !max_x.is_finite() || max_x <= 0.0
    }) {
        eprintln!("ok-extract: SexFaces source morphs unavailable for {}", part.name);
        return;
    }
    // AddAhegao: all targets use the base pattern except Tongue, which uses
    // TongueOut (24). keepOpen makes both slots the selected open shape.
    for base in [2usize, 11, 39] {
        let source = if target_index == 4 { 24 } else { base };
        let open = part.mouth_patterns[source].1;
        part.mouth_patterns.push((open, open));
    }
    // AddLopsided: only Face blends close/open by raw vertex x. All other
    // targets receive a new morph which copies their base open frame.
    for (base, lean_right) in [(12usize, false), (12, true),
                               (27, false), (27, true),
                               (28, false), (28, true)] {
        let (close, open) = part.mouth_patterns[base];
        let Some(close_shape) = part.blend_shapes.get(close as usize) else { return; };
        let Some(open_shape) = part.blend_shapes.get(open as usize) else { return; };
        let name = format!("sexfaces.lopsided.{}.{}",
            if lean_right { "right" } else { "left" },
            close_shape.0.to_string_lossy());
        let shape_index = if let Some(index) = part.blend_shapes.iter()
            .position(|(existing, _)| existing.to_bytes() == name.as_bytes()) {
            index as i32
        } else {
            let deltas = if target_index == 0 {
                let raw_x = raw_x.unwrap();
                let max_x = raw_x.iter().copied().fold(f32::NEG_INFINITY, f32::max);
                if !max_x.is_finite() || max_x <= 0.0 { return; }
                let mut close_dense = vec![[0.0f32; 3]; raw_x.len()];
                let mut open_dense = vec![[0.0f32; 3]; raw_x.len()];
                for &(index, delta) in &close_shape.1 {
                    if let Some(v) = close_dense.get_mut(index as usize) { *v = delta; }
                }
                for &(index, delta) in &open_shape.1 {
                    if let Some(v) = open_dense.get_mut(index as usize) { *v = delta; }
                }
                (0..raw_x.len()).filter_map(|i| {
                    let u = ((raw_x[i] + max_x) / (2.0 * max_x)).clamp(0.0, 1.0);
                    let sigmoid = ((u as f64 - 0.5) * 10.0).tanh() as f32 * 0.5 + 0.5;
                    let t = if lean_right { 1.0 - sigmoid } else { sigmoid };
                    let delta = [0, 1, 2].map(|k| close_dense[i][k] * t + open_dense[i][k] * (1.0 - t));
                    (delta.iter().any(|v| v.abs() > 1e-10)).then_some((i as u32, delta))
                }).collect::<Vec<_>>()
            } else {
                open_shape.1.clone()
            };
            let index = part.blend_shapes.len() as i32;
            part.morph_idx.push(deltas.iter().map(|(i, _)| *i).collect());
            part.morph_dxyz.push(deltas.iter().flat_map(|(_, d)| *d).collect());
            part.blend_shapes.push((std::ffi::CString::new(name).unwrap(), deltas));
            index
        };
        part.mouth_patterns.push((close, shape_index));
    }
    // AddClosedTeeth: the Teeth target (index 2) borrows BigI (28).
    for base in [19usize, 37] {
        let source = if target_index == 2 { 28 } else { base };
        part.mouth_patterns.push(part.mouth_patterns[source]);
    }
}

fn sanitize(s: &str) -> String {
    s.chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() {
                c
            } else {
                '_'
            }
        })
        .take(48)
        .collect()
}

fn cstr(p: *const c_char) -> Result<String, String> {
    if p.is_null() {
        return Err("null string".into());
    }
    let s = unsafe { CStr::from_ptr(p) };
    s.to_str()
        .map(|v| v.to_string())
        .map_err(|_| "utf8".into())
}

pub(crate) fn write_err(buf: *mut c_char, cap: c_int, msg: &str) {
    if buf.is_null() || cap <= 0 {
        return;
    }
    let owned: String = msg.chars().filter(|c| *c != '\0').collect();
    let Ok(c) = CString::new(owned) else {
        eprintln!("ok-extract: cannot encode error message");
        return;
    };
    let bytes = c.as_bytes_with_nul();
    let n = (cap as usize).min(bytes.len());
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, n);
        if n > 0 {
            *buf.add(n - 1) = 0;
        }
    }
}

#[no_mangle]
pub extern "C" fn ok_copy_portable_clothing_states(source:*const c_char,output:*const c_char,err:*mut c_char,cap:c_int)->c_int {
    match run_no_panic(||portable::copy_clothing_states(Path::new(&cstr(source)?),Path::new(&cstr(output)?))) {
        Ok(_)=>0,Err(error)=>{write_err(err,cap,&error);1}
    }
}

#[no_mangle]
pub extern "C" fn ok_extract_card(
    game_root_utf8: *const c_char,
    card_path_utf8: *const c_char,
    out_dir_utf8: *const c_char,
    err_buf: *mut c_char,
    err_cap: c_int,
) -> c_int {
    let run = run_no_panic(|| {
        let game = PathBuf::from(cstr(game_root_utf8)?);
        let out = PathBuf::from(cstr(out_dir_utf8)?);
        let card = if card_path_utf8.is_null() {
            None
        } else {
            let s = cstr(card_path_utf8)?;
            if s.is_empty() {
                None
            } else {
                Some(PathBuf::from(s))
            }
        };
        extract_card(&game, card.as_deref(), &out)?;
        Ok(())
    });
    match run {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("ok-extract error: {e}");
            write_err(err_buf, err_cap, &e);
            1
        }
    }
}

#[no_mangle]
pub extern "C" fn ok_extract_card_outfit(
    game_root_utf8: *const c_char,
    card_path_utf8: *const c_char,
    out_dir_utf8: *const c_char,
    outfit_index: c_int,
    err_buf: *mut c_char,
    err_cap: c_int,
) -> c_int {
    let run = run_no_panic(|| {
        let game = PathBuf::from(cstr(game_root_utf8)?);
        let out = PathBuf::from(cstr(out_dir_utf8)?);
        let card = if card_path_utf8.is_null() {
            None
        } else {
            let s = cstr(card_path_utf8)?;
            if s.is_empty() {
                None
            } else {
                Some(PathBuf::from(s))
            }
        };
        extract_card_outfit(&game, card.as_deref(), &out, outfit_index)?;
        Ok(())
    });
    match run {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("ok-extract error: {e}");
            write_err(err_buf, err_cap, &e);
            1
        }
    }
}

#[cfg(test)]
mod tests;

/// A CPU-baked face still needs its authored mouth channels. Transform each
/// sparse blendshape delta by the same LBS palette and head attachment as its
/// source vertex; translation cancels for a delta vector.
fn transform_baked_mouth_deltas(
    mesh: &mut ExtractedMesh,
    skel: &crate::pose::Skeleton,
    attach: &crate::pose::Mat4,
) {
    let n = if !mesh.bind_poses.is_empty() {
        mesh.bind_poses.len().min(skel.worlds.len())
    } else {
        skel.worlds.len().min(skel.rest.len())
    };
    if n == 0 || mesh.weights.len() != mesh.positions.len() || mesh.bone_idx.len() != mesh.positions.len() {
        return;
    }
    let binds = if mesh.bind_poses.len() >= n {
        mesh.bind_poses[..n].to_vec()
    } else {
        crate::pose::binds_from_rest(&skel.rest[..n])
    };
    let palette: Vec<_> = (0..n)
        .map(|i| crate::pose::mat_mul_pub(&skel.worlds[i], &binds[i]))
        .collect();
    let linear = |m: &crate::pose::Mat4, v: [f32; 3]| -> [f32; 3] {
        [m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
         m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
         m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]]
    };
    for (_name, sparse) in &mut mesh.blend_shapes {
        // Every authored channel is in the same mesh-local coordinate frame;
        // mod mouth controllers may use names without the vanilla kuti prefix.
        for (vertex, delta) in sparse {
            let i = *vertex as usize;
            let (Some(weights), Some(bones)) = (mesh.weights.get(i), mesh.bone_idx.get(i)) else { continue; };
            let mut sum = [0.0f32; 3];
            let mut total = 0.0f32;
            for k in 0..4 {
                let weight = weights[k];
                if weight <= 1e-6 { continue; }
                let Some(pal) = palette.get(bones[k] as usize) else { continue; };
                let d = linear(pal, *delta);
                for axis in 0..3 { sum[axis] += d[axis] * weight; }
                total += weight;
            }
            if total > 1e-5 {
                for axis in 0..3 { sum[axis] /= total; }
                *delta = linear(attach, sum);
            }
        }
    }
}








/// Dependency inventory for original cards; reads card metadata only, never a game installation.
pub fn standalone_card_requirements(path:&std::path::Path,outfit:i32)->Result<serde_json::Value,String>{
 let c=crate::card::parse_card_outfit(path,outfit)?;
 let guids:std::collections::BTreeSet<String>=c.resolves.iter().map(|r|r.mod_id.clone()).filter(|g|!g.is_empty()).collect();
 let body_descriptor=c.body_guid.as_deref().filter(|g|!g.is_empty()&&!matches!(*g,"Default.Body.Female"|"Default.Body.Male"));
 Ok(serde_json::json!({"sex":c.sex,"outfit":outfit,"guids":guids,"body_descriptor":body_descriptor}))
}

/// Authoring of common curves only; no character/card input or private bake.
pub fn export_common_runtime_clip(root:&std::path::Path,key:&str)->Result<Vec<u8>,String>{anim::export_shared_clip(root,key)}

/// Diagnostic: the card opened as the reader opens it (session, `outfit`), skinned at its opening pose, every part
/// written as `<index>_<role>_<name>.obj` (Unity space, the session's posed positions) into `out_dir`. Compare with the
/// game's mesh_dump of the same card/outfit to find a part that deforms differently (a top a breast pokes through).
pub fn export_session_posed_objs(game_root: &Path, card_path: &Path, outfit: i32, out_dir: &Path) -> Result<usize, String> {
    // OK_READER_LIB=<base library>: opened as the reader opens an original card (the library plus its zipmods).
    let mut session = match std::env::var_os("OK_READER_LIB") {
        Some(lib) => crate::oklib::open_with_zipmods(Path::new(&lib), game_root, card_path, outfit)?,
        None => crate::session::open(game_root, Some(card_path), outfit)?,
    };
    // OK_POSED_FRAMES=N: N frames of its animation at 60 Hz first (dynamic bones and physics settle as they do live).
    let frames: u32 = std::env::var("OK_POSED_FRAMES").ok().and_then(|s| s.parse().ok()).unwrap_or(0);
    for frame in 0..frames { session.apply_anim_frame(frame as f32 / 60.0, 1.0 / 60.0); }
    session.skin_all();
    std::fs::create_dir_all(out_dir).map_err(|e| e.to_string())?;
    for (index, part) in session.parts.iter().enumerate() {
        let safe: String = format!("{index}_{}_{}", part.role, part.name).chars()
            .map(|c| if c.is_alphanumeric() || c == '_' || c == '-' { c } else { '_' }).collect();
        let mut text = format!("# visible={}\n", part.render_visible);
        for p in &part.positions { text.push_str(&format!("v {} {} {}\n", p[0], p[1], p[2])); }
        for t in part.indices.chunks_exact(3) { text.push_str(&format!("f {} {} {}\n", t[0] + 1, t[1] + 1, t[2] + 1)); }
        std::fs::write(out_dir.join(format!("{safe}.obj")), text).map_err(|e| e.to_string())?;
    }
    Ok(session.parts.len())
}

/// Threads one process's pools may use: OK_THREADS (set by `--oklib cards` for its workers, which share the machine),
/// else the hardware threads.
pub fn cpu_threads() -> usize {
    std::env::var("OK_THREADS").ok().and_then(|v| v.parse().ok()).filter(|&n: &usize| n > 0)
        .unwrap_or_else(|| std::thread::available_parallelism().map_or(4, |n| n.get()))
}

/// `unity::bundle_texture_report` for the CLI (`--bundle-textures`).
pub fn bundle_texture_report(bytes: Vec<u8>) -> Vec<(String, u32, bool)> { crate::unity::bundle_texture_report(bytes) }
