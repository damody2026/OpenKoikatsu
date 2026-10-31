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
mod belly_bulge;
mod card;
mod face_create;
mod face_create_source;
mod face_create_gpu;
mod dbone;
pub mod px;
mod finalik;
mod point_contact;
mod h_playback;
pub use h_playback::h_pose_table;
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
mod shader_states;
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

/// Shader families that sample their main texture with V reversed, so a part drawn by them keeps (u, 1 - v).
/// CM3D2's ports (CM3D2/Lighted, Lighted_Trans, Toony_*), which mod maps converted from Custom Maid 3D 2 carry: their
/// meshes keep CM3D2's V and the shaders reverse it. Checked on Petardo's Kitchen against the scene's own picture
/// (back wall red tiles, cupboard fronts, the trees behind the window); Standard and Unlit items of the same scenes
/// (the helipad's lettering) draw with V as stored.
pub(crate) fn shader_samples_flipped_v(shader: &str) -> bool {
    shader.to_ascii_lowercase().starts_with("cm3d2/")
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
        // main_item_studio itself is One/Zero with ZWrite (its _ALPHATEST_ON variant clips): a material that keeps an
        // alpha shader's queue 3000 after Material Editor gave it this shader still draws opaque, only later (read by its
        // queue, 205 parts of 83283571_p1 drew blended).
        if shader_family(&mat.shader) == "shader forge/main_item_studio" {
            return (if alphatest { "masked" } else { "opaque" }.into(), toon_unlit);
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
        // Unity's built-in Unlit/* and the CM3D2 ports some mod maps bring (Petardo's Kitchen: CM3D2/Lighted,
        // CM3D2/Lighted_Trans): none of Koikatsu's toon inputs apply. Unlit/* draws its texture as is; CM3D2/Lighted's
        // textures carry their lighting baked in, so it is drawn unlit too (as a Koikatsu toon part with its near-black
        // _ShadowColor the whole room came out black). A cutout variant clips, a Transparent / _Trans one blends.
        if shader.starts_with("unlit/") || shader.starts_with("cm3d2/") {
            let blend = if shader.contains("cutout") || alphatest { "masked" }
                else if shader.contains("transparent") || shader.contains("_trans") || alphablend || mat.render_queue >= 3000 { "translucent" }
                else { "opaque" };
            return (blend.into(), true);
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

mod entries;
pub(crate) use entries::*;

mod catalog_textures;
pub(crate) use catalog_textures::*;

mod texture_bake;
pub(crate) use texture_bake::*;

mod face_texture;
pub(crate) use face_texture::*;

mod head_body;
pub(crate) use head_body::*;

mod material_editor;
pub use material_editor::*;

mod push;
pub(crate) use push::*;

mod accessories;
pub(crate) use accessories::*;

mod clothes_texture;
pub use clothes_texture::*;

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

mod placement;
pub(crate) use placement::*;

mod face_shapes;
pub(crate) use face_shapes::*;

mod rig;
pub(crate) use rig::*;

pub fn open_session(game_root: &Path, card_path: Option<&Path>, outfit: i32) -> Result<usize, String> {
    let sess = crate::session::open(game_root, card_path, outfit)?;
    Ok(sess.parts.len())
}
/// Builds the per-process memos every card open needs (lists, mods catalog, GUID index, StripAll migrations,
/// UncensorSelector and MaterialEditor manifests, toon ramp, H pair metadata) for a game folder or OK base library,
/// so the first card a reader opens costs what later ones do. The mods-tree scans run in parallel. Safe to call
/// from a background thread, also while a card opens: it works in a pool of its own whose threads belong to no open
/// (`oklib::mark_outside_opens`; they were handed the card being opened) and run below normal priority, so a card
/// picked right after startup is not slowed by it. It mounts no card pack.
pub fn prewarm(game_root: &Path) -> Result<(), String> {
    let t = Instant::now();
    crate::oklib::ensure_library(game_root)?;
    let mods_dir = game_root.join("mods");
    static POOL: std::sync::OnceLock<Option<rayon::ThreadPool>> = std::sync::OnceLock::new();
    let pool = POOL.get_or_init(|| rayon::ThreadPoolBuilder::new()
        .num_threads(cpu_threads().clamp(2, 16)).thread_name(|i| format!("OKPrewarm{i}"))
        .start_handler(|_| { crate::oklib::mark_outside_opens(); below_normal_priority(); }).build().ok());
    let work = || rayon::scope(|scope| {
        scope.spawn(|_| { let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom")); let _ = configured_ramp_png(game_root, &vanilla); });
        scope.spawn(|_| { let _ = crate::list::load_mods_catalog_shared(&mods_dir); });
        scope.spawn(|_| { let _ = crate::sqlcache::guid_index_shared(&mods_dir); });
        scope.spawn(|_| { let _ = crate::mods::strip_all_guids(&mods_dir); });
        scope.spawn(|_| { let _ = crate::mods::uncensor_sources(game_root); });
        scope.spawn(|_| { let _ = me_manifest_sections(game_root); });
        scope.spawn(|_| { let _ = crate::h_playback::pairs(game_root); });
        // Original KKS cards open from the library plus the installation's zipmods they name (OK_GAME_ROOT).
        if let Some(game) = std::env::var_os("OK_GAME_ROOT").map(PathBuf::from).filter(|g| g.join("mods").is_dir()) {
            scope.spawn(move |_| crate::mods::prewarm_guid_index(&game.join("mods")));
        }
    });
    match pool { Some(pool) => pool.install(work), None => { crate::oklib::mark_outside_opens(); work() } }
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

mod extract;
pub(crate) use extract::extract_session_from_info;

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
/// This thread below normal priority (background work beside the opens a user waits for).
fn below_normal_priority() {
    #[cfg(windows)]
    {
        #[link(name = "kernel32")]
        extern "system" { fn GetCurrentThread() -> isize; fn SetThreadPriority(thread: isize, priority: i32) -> i32; }
        const THREAD_PRIORITY_BELOW_NORMAL: i32 = -1;
        // SAFETY: GetCurrentThread returns this thread's pseudo handle, which needs no closing.
        unsafe { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); }
    }
}
pub fn cpu_threads() -> usize {
    std::env::var("OK_THREADS").ok().and_then(|v| v.parse().ok()).filter(|&n: &usize| n > 0)
        .unwrap_or_else(|| std::thread::available_parallelism().map_or(4, |n| n.get()))
}

/// `f` with these garment states requested for the opens it makes (tools: ok-card-open-bench).
pub fn with_clothes_states<R>(states: [u8; 9], f: impl FnOnce() -> R) -> R {
    let _states = crate::unity::request_clothes_states(states);
    f()
}

/// `unity::bundle_texture_report` for the CLI (`--bundle-textures`).
pub fn bundle_texture_report(bytes: Vec<u8>) -> Vec<(String, u32, bool)> { crate::unity::bundle_texture_report(bytes) }
