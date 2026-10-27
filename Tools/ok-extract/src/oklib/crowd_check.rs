//! `ok-extract <game> --oklib crowd-check <lib> <card>...`: whether the hundred-girl room (Tools/run_crowd_room100.lua)
//! can use a card, without Unreal. Opens outfit 0 the way the room's runtime does (OK card: the library and its own
//! pack; an original card: the library plus its zipmods) and looks at the parts the runtime builds.
//!
//! - dressed: a drawn `clothes_top` or `clothes_bot` part. The room leaves out a girl wearing neither (the card's cover
//!   image is not outfit 0 and cannot tell).
//! - unsupported: drawn parts whose material maps to no shared runtime recipe (the room would fail the card with
//!   "Source material coverage incomplete"). The rule mirrors `OKRuntimeSharedMaterialRecipe`
//!   (Plugins/KKRuntimePose/Source/KKRuntimePose/Private/KKRuntimeSharedMaterial.cpp): a shader family added there
//!   must be added here too.
//!
//! Drawn parts are those `OKBuildRuntimeRigMetadata` (KKRuntimeRigMetadata.cpp) keeps: render-visible ones, the
//! tears (hidden but built, left out when their material is unsupported), never the default-hidden genitals.
use std::path::Path;

/// Shaders the runtime draws whatever the part's kind/blend (the first block of OKRuntimeSharedMaterialRecipe).
const ANY_KIND: &[&str] = &[
    "Koikano/main_skin", "Shader Forge/main_skin", "Koikano/main_nose", "Koikano/main_eyeb", "Koikano/main_clothes_opaque",
    "Koikano/main_clothes_item", "Koikano/main_clothes_alpha", "Koikano/low_clothes_alpha", "IBL_Shader_alpha", "IBL_Shader",
    "KKUSSitem", "xukmi/MainItemPlus", "Shader Forge/toon_glasses_lod0", "Shader Forge/main_StandardMDK_studio",
    "Shader Forge/main_item_studio", "Goo", "Shader Forge/main_item_ditherd", "Shader Forge/main_item_emission", "KKUSS",
    "Shader Forge/toon_eyew_lod0", "DA/Chara/Oil Painting", "BetterMatcap", "Koikano/hair_main_sun",
    "Koikano/hair_main_sun_front", "Koikano/main_clothes_item_glasses",
];
/// Kind-0 translucent unlit parts: (shader, eye draw kind).
const EYES: &[(&str, i32)] = &[
    ("Koikano/main_eye", 1), ("Shader Forge/toon_eye_lod0", 2), ("Koikano/main_eyew", 0), ("Shader Forge/toon_nose_lod0", 0),
    ("Shader Forge/toon_eyew_lod0", 0),
];
/// Renderers the runtime never draws (default-hidden genitals).
const HIDDEN: &[&str] = &["o_dankon", "o_dan_f", "cm_o_dan00", "cm_o_dan_f"];

fn supported(shader: Option<&str>, kind: u8, blend: &str, unlit: bool, eye: i32) -> bool {
    if shader.is_some_and(|s| ANY_KIND.contains(&s)) { return true; }
    let is = |want: &str| shader.is_some_and(|s| s.eq_ignore_ascii_case(want));
    let translucent = blend.eq_ignore_ascii_case("translucent");
    match kind {
        0 => translucent && unlit && EYES.iter().any(|&(s, k)| k == eye && is(s)),
        // By family: xukmi's MainItemStudioAlphaPlus draws as main_item_studio_alpha.
        4 => translucent && unlit && shader.is_some_and(|s| crate::shader_family(s) == "shader forge/main_item_studio_alpha"),
        1 => blend.eq_ignore_ascii_case("masked") && unlit && (is("Shader Forge/main_hair") || is("Shader Forge/main_hair_front")),
        2 | 8 => true,
        // ForgeAlpha needs the source material (masked or translucent by _OKNativeAlphaOpaque).
        11 => shader.is_some(),
        _ => false,
    }
}

pub struct Verdict {
    pub dressed: bool,
    /// Roles of the drawn parts, sorted.
    pub roles: Vec<String>,
    /// Drawn parts the runtime cannot draw, as name:shader:kind:blend.
    pub unsupported: Vec<String>,
}

/// Outfit 0 of `card` as the room sees it.
pub fn check(lib: &Path, game: &Path, card: &Path) -> Result<Verdict, String> {
    super::ensure_library(lib)?;
    let session = if super::is_library_card(card) { crate::session::open(lib, Some(card), 0)? }
        else { super::open_with_zipmods(lib, game, card, 0)? };
    let (mut roles, mut unsupported) = (Vec::<String>::new(), Vec::new());
    for p in &session.parts {
        let tear = p.name.to_ascii_lowercase().starts_with("cf_o_namida");
        if (!p.render_visible && !tear) || HIDDEN.iter().any(|h| p.name.eq_ignore_ascii_case(h)) { continue; }
        let shader = p.source_material.as_ref().map(|m| m.shader.as_str());
        let eye = p.eye_draw.as_ref().map_or(0, |d| d.kind);
        if !supported(shader, p.shader_kind, &p.blend, p.unlit, eye) {
            // A tear the runtime cannot draw is left out, not a failure.
            if tear { continue; }
            unsupported.push(format!("{}:{}:{}:{}", p.name, shader.unwrap_or("-"), p.shader_kind, p.blend));
        }
        if !roles.contains(&p.role) { roles.push(p.role.clone()); }
    }
    roles.sort();
    let dressed = roles.iter().any(|r| r == "clothes_top" || r == "clothes_bot");
    Ok(Verdict { dressed, roles, unsupported })
}

/// One `OKCROWD` line per card (tab separated):
///   OKCROWD <card> <dressed 0|1> <roles, comma separated> <unsupported parts, " | " separated, or ->
///   OKCROWD <card> error <message>
pub fn cli(lib: &Path, game: &Path, cards: &[String]) -> Result<(), String> {
    let mut failed = 0;
    for card in cards {
        let made = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| check(lib, game, Path::new(card)))).unwrap_or_else(|_| Err("panic".into()));
        crate::unity::release_bundle_caches();
        match made {
            Ok(v) => println!("OKCROWD\t{card}\t{}\t{}\t{}", u8::from(v.dressed), v.roles.join(","),
                if v.unsupported.is_empty() { "-".into() } else { v.unsupported.join(" | ") }),
            Err(e) => { failed += 1; println!("OKCROWD\t{card}\terror\t{}", e.replace(['\t', '\n'], " ")); }
        }
    }
    if failed > 0 { return Err(format!("{failed} card(s) could not be opened")); }
    Ok(())
}
