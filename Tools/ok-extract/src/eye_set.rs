//! Eye sets (character list cha_eyeset, category 2): what ChaControl.ChangeEyesPtn does for an eyes pattern. The
//! pattern number a card or scene stores is a list row, not a FaceBlendShape pattern: the row names the blend shape
//! pattern (EyesPtn), whether the eye base and pupils show (n_eyebase, n_hitomi), which gag eye object shows
//! (cf_O_gag_eye_00..02, n_special) with its texture, and a pupil expression texture (EpsTex: hearts, stars).

use std::path::Path;

use serde::Serialize;

/// Character list category of the eye sets.
pub const CATEGORY: i32 = 2;

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct EyeSet {
    /// The list row (ChaFileStatus.eyesPtn).
    pub id: i32,
    /// FaceBlendShape eye pattern.
    pub eyes_ptn: i32,
    /// n_eyebase shown (eye whites, eyelines, tears, pupils).
    pub eye_base: bool,
    /// n_hitomi shown (pupils).
    pub hitomi: bool,
    /// 1..3: cf_O_gag_eye_00..02 shown; 0: none.
    pub gag: i32,
    /// Gag eye texture (bundle, asset), and its sprite sheet (columns, rows) from TileAnim.
    pub gag_bundle: String,
    pub gag_texture: String,
    pub gag_tiles: [i32; 2],
    /// What ChangeGagEyesMaterial hands the gag material (Koikano/sub_texture_animation): `_TileAnimation` (TileAnim
    /// columns/rows/speed, w 0), `_SizeSpeed`, `_SizeWidth`, `_AngleSpeed`. Yurayura is set too, but the KKS shader
    /// never reads it.
    pub gag_tile_animation: [f32; 4],
    pub gag_size_speed: f32,
    pub gag_size_width: f32,
    pub gag_angle_speed: f32,
    /// Pupil expression texture (bundle, asset) drawn over the pupils.
    pub expression_bundle: String,
    pub expression_texture: String,
}

impl EyeSet {
    /// Anything beyond the blend shape: these change which parts show and their textures.
    pub fn changes_parts(&self) -> bool { !self.eye_base || !self.hitomi || self.gag != 0 || !self.expression_texture.is_empty() }
    /// The gag material moves: it flips through its sprite sheet (TileAnim columns×rows ≥ 1 with a speed), pulses its
    /// size or turns. The drawing then gets the whole sheet and these values (`apply_gag_animation`).
    pub fn animates_gag(&self) -> bool {
        let [x, y, z, _] = self.gag_tile_animation;
        (x.floor() * y.floor() >= 1. && z != 0.) || (self.gag_size_speed != 0. && self.gag_size_width != 0.) || self.gag_angle_speed != 0.
    }
    /// Writes the gag material's animation values (ChangeGagEyesMaterial) into the drawn gag eye's material.
    pub fn apply_gag_animation(&self, mat: &mut crate::unity::MeshMaterial) {
        mat.colors.insert("_TileAnimation".into(), self.gag_tile_animation);
        mat.floats.insert("_SizeSpeed".into(), self.gag_size_speed);
        mat.floats.insert("_SizeWidth".into(), self.gag_size_width);
        mat.floats.insert("_AngleSpeed".into(), self.gag_angle_speed);
    }
}

fn asset(v: String) -> String { if v == "0" { String::new() } else { v } }

/// For each gag eye object (1..3), the lowest eyes pattern under `root` that shows it.
pub fn gag_patterns(root: &Path) -> Vec<i32> {
    let lists = crate::list::load_characustom_dir_shared(&root.join("abdata/list/characustom"));
    let mut by_gag = std::collections::BTreeMap::new();
    for e in lists.entries().filter(|e| e.category_no == CATEGORY) {
        let gag = e.info("EyeObjNo").trim().parse::<i32>().unwrap_or(0).clamp(0, 3);
        if gag != 0 { by_gag.entry(gag).and_modify(|p: &mut i32| *p = (*p).min(e.id)).or_insert(e.id); }
    }
    by_gag.into_values().collect()
}

/// The eye set of a pattern from the lists under `root` (a game or the OK base library); unknown patterns fall back
/// to row 0 as the game does.
pub fn eye_set(root: &Path, ptn: i32) -> Option<EyeSet> {
    let mut lists = crate::list::load_characustom_dir_shared(&root.join("abdata/list/characustom"));
    // An OK card opened without a game root reads the lists of the OK base library.
    if lists.get(CATEGORY, 0).is_none() {
        if let Some(lib) = std::env::var_os("OK_BASE_LIBRARY").map(std::path::PathBuf::from) {
            lists = crate::list::load_characustom_dir_shared(&lib.join("abdata/list/characustom"));
        }
    }
    let e = lists.get(CATEGORY, ptn).or_else(|| lists.get(CATEGORY, 0))?;
    let int = |k: &str| e.info(k).trim().parse::<i32>().unwrap_or(0);
    let tiles: Vec<i32> = e.info("TileAnim").split('/').map(|v| v.trim().parse::<f32>().unwrap_or(1.) as i32).collect();
    // ChangeEyesPtn: TileAnim "x/y/z" parsed only when it has three parts, else zero.
    let anim: Vec<f32> = e.info("TileAnim").split('/').map(|v| v.trim().parse::<f32>().unwrap_or(0.)).collect();
    let tile_animation = if anim.len() >= 3 { [anim[0], anim[1], anim[2], 0.] } else { [0.; 4] };
    let float = |k: &str| e.info(k).trim().parse::<f32>().unwrap_or(0.);
    Some(EyeSet {
        id: e.id,
        eyes_ptn: int("EyesPtn"),
        eye_base: int("EyeBase") == 1,
        hitomi: int("EyeHitomi") != 0,
        gag: int("EyeObjNo").clamp(0, 3),
        gag_bundle: asset(e.info("MainTexAB")),
        gag_texture: asset(e.info("MainTex")),
        gag_tiles: [tiles.first().copied().unwrap_or(1).max(1), tiles.get(1).copied().unwrap_or(1).max(1)],
        gag_tile_animation: tile_animation,
        gag_size_speed: float("SizeSpeed"),
        gag_size_width: float("SizeWidth"),
        gag_angle_speed: float("AngleSpeed"),
        expression_bundle: asset(e.info("EpsTexAB")),
        expression_texture: asset(e.info("EpsTex")),
    })
}

/// Which eye reference a head renderer belongs to (vanilla head hierarchy; ChaReference finds these by name, so
/// heads keep the names): 0 not an eye part, 1 n_eyebase (eyelines, eye whites, tears), 2 n_hitomi (pupils),
/// 3..5 the gag eye objects 00..02.
pub fn eye_reference(name: &str) -> i32 {
    let n = name.to_ascii_lowercase();
    if let Some(k) = n.strip_prefix("cf_o_gag_eye_0").and_then(|r| r.chars().next()).and_then(|c| c.to_digit(10)) {
        return if k <= 2 { 3 + k as i32 } else { 0 };
    }
    if n.starts_with("cf_ohitomi_") && n.trim_end_matches("_low").ends_with("02") { return 2; }
    if n.starts_with("cf_o_eyeline") || n.starts_with("cf_ohitomi_") || n.starts_with("cf_o_namida_") { return 1; }
    0
}

/// Whether the head part shows under the eye set.
pub fn part_shown(set: &EyeSet, name: &str) -> Option<bool> {
    match eye_reference(name) {
        0 => None,
        1 => (!set.eye_base).then_some(false),
        2 => (!set.eye_base || !set.hitomi).then_some(false),
        k => Some(set.gag == k - 2),
    }
}

/// The pupil expression texture (ChaControl writes it to the eyes' _expression with _exppower 1).
pub fn expression_png(root: &Path, set: &EyeSet) -> Option<Vec<u8>> {
    if set.expression_texture.is_empty() { return None; }
    let name = [set.expression_texture.as_str()];
    crate::png_from_bundle(root, &set.expression_bundle, None, &name, &[], &[])
        .or_else(|| std::env::var_os("OK_BASE_LIBRARY").and_then(|lib| crate::png_from_bundle(Path::new(&lib), &set.expression_bundle, None, &name, &[], &[])))
}

/// The gag eye texture: the whole sprite sheet when the material animates it (`animates_gag`), else the first frame.
pub fn gag_texture_png(root: &Path, set: &EyeSet) -> Option<Vec<u8>> {
    if set.gag_texture.is_empty() { return None; }
    let png = crate::png_from_bundle(root, &set.gag_bundle, None, &[set.gag_texture.as_str()], &[], &[])
        .or_else(|| std::env::var_os("OK_BASE_LIBRARY").and_then(|lib|
            crate::png_from_bundle(Path::new(&lib), &set.gag_bundle, None, &[set.gag_texture.as_str()], &[], &[])))?;
    let [cols, rows] = set.gag_tiles;
    if (cols <= 1 && rows <= 1) || set.animates_gag() { return Some(png); }
    let img = crate::teximg::load_from_memory(&png).ok()?.into_rgba8();
    let (w, h) = (img.width() / cols as u32, img.height() / rows as u32);
    // Frame 0 is the top-left tile (Unity UV origin bottom-left; the PNG is top-down).
    let tile = image::imageops::crop_imm(&img, 0, 0, w.max(1), h.max(1)).to_image();
    let mut out = Vec::new();
    image::DynamicImage::ImageRgba8(tile).write_to(&mut std::io::Cursor::new(&mut out), image::ImageFormat::Png).ok()?;
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn eye_references_follow_the_head_hierarchy() {
        assert_eq!(eye_reference("cf_Ohitomi_L02"), 2);
        assert_eq!(eye_reference("cf_Ohitomi_R"), 1);
        assert_eq!(eye_reference("cf_O_eyeline_low"), 1);
        assert_eq!(eye_reference("cf_O_namida_M"), 1);
        assert_eq!(eye_reference("cf_O_gag_eye_01"), 4);
        assert_eq!(eye_reference("cf_O_face"), 0);
        let swirl = EyeSet { eyes_ptn: 21, eye_base: false, hitomi: false, gag: 2, ..Default::default() };
        assert_eq!(part_shown(&swirl, "cf_O_gag_eye_01"), Some(true));
        assert_eq!(part_shown(&swirl, "cf_O_gag_eye_00"), Some(false));
        assert_eq!(part_shown(&swirl, "cf_Ohitomi_L"), Some(false));
        let normal = EyeSet { eye_base: true, hitomi: true, ..Default::default() };
        assert_eq!(part_shown(&normal, "cf_Ohitomi_L02"), None);
        assert_eq!(part_shown(&normal, "cf_O_gag_eye_02"), Some(false));
    }

    /// The gag eye sets of the game's list hand their material moving values (TileAnim, SizeSpeed, SizeWidth,
    /// AngleSpeed); at least one of them animates, and an animated set keeps its whole sprite sheet.
    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn gag_eye_sets_carry_their_animation() {
        let root = &crate::test_support::game_path("");
        if !root.join("abdata/list/characustom").is_dir() { eprintln!("skip: no game"); return; }
        let mut animated = 0;
        for ptn in 0..64 {
            let Some(set) = eye_set(root, ptn) else { continue };
            if set.gag == 0 || set.id != ptn { continue; }
            eprintln!("eyes {ptn}: gag {} tiles {:?} anim {:?} size {}/{} angle {}", set.gag, set.gag_tiles, set.gag_tile_animation,
                set.gag_size_speed, set.gag_size_width, set.gag_angle_speed);
            if set.animates_gag() {
                animated += 1;
                let mut mat = crate::unity::MeshMaterial::default();
                set.apply_gag_animation(&mut mat);
                assert_eq!(mat.color("_TileAnimation"), Some(set.gag_tile_animation));
                assert_eq!(mat.float("_AngleSpeed"), Some(set.gag_angle_speed));
            }
        }
        assert!(animated > 0, "no animated gag eye set");
    }
}
