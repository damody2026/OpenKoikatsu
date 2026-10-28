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

mod entries;
pub(crate) use entries::*;

mod catalog_textures;
pub(crate) use catalog_textures::*;

mod texture_bake;
pub(crate) use texture_bake::*;

mod face_texture;
pub(crate) use face_texture::*;

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
pub fn cpu_threads() -> usize {
    std::env::var("OK_THREADS").ok().and_then(|v| v.parse().ok()).filter(|&n: &usize| n > 0)
        .unwrap_or_else(|| std::thread::available_parallelism().map_or(4, |n| n.get()))
}

/// `unity::bundle_texture_report` for the CLI (`--bundle-textures`).
pub fn bundle_texture_report(bytes: Vec<u8>) -> Vec<(String, u32, bool)> { crate::unity::bundle_texture_report(bytes) }
