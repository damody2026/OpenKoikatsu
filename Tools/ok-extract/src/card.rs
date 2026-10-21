#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use rmpv::Value;
use std::fs;
use std::io::Cursor;
use std::path::Path;

#[derive(Debug, Clone)]
pub struct ResolveInfo {
    pub mod_id: String,
    pub slot: i32,
    pub local_slot: i32,
    pub property: String,
    pub category_no: i32,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct CardInfo {
    pub marker: String,
    pub lastname: String,
    pub firstname: String,
    pub nickname: String,
    pub head_id: i32,
    pub hair_ids: [i32; 4],
    /// ChaFileHair.glossId selects category mt_hairgloss (433).
    pub hair_gloss_id: i32,
    /// ChaFileHair.parts pos/rot/scl/length (index = HairKind).
    pub hair_parts: [HairPart; 4],
    pub shape_face: Vec<f32>,
    pub shape_body: Vec<f32>,
    pub bust_softness: f32,
    pub bust_weight: f32,
    pub collider_overrides: crate::dbone::game_colliders::CardOverrides,
    /// DynamicBoneEditor (`com.deathweasel.bepinex.dynamicboneeditor` AccessoryDynamicBoneData): per outfit, accessory
    /// slot and DynamicBone root name, the parameters the maker changed (None keeps the prefab value).
    pub dynamic_bone_edits: Vec<crate::dbone::DynamicBoneEdit>,
    pub clothes_ids: Vec<i32>,
    /// ChaFileClothes.parts[i].emblemeId / emblemeId2.
    pub clothes_emblem_ids: Vec<[i32; 2]>,
    /// ChaFileClothes.parts[i].colorInfo[0..3].baseColor
    pub clothes_colors: Vec<[[f32; 4]; 4]>,
    /// ChaFileClothes.parts[i].colorInfo[0..3] pattern / patternColor / tiling.
    pub clothes_patterns: Vec<[ClothesColorPattern; 4]>,
    pub sub_parts_id: Vec<i32>,
    pub hair_color: [f32; 4],
    pub skin_color: [f32; 4],
    pub clothes_color: [f32; 4],
    pub eye_color: [f32; 4],
    pub eye_sub_color: [f32; 4],
    pub eyebrow_color: [f32; 4],
    pub eyeline_color: [f32; 4],
    pub accessory: Vec<AccPart>,
    pub resolves: Vec<ResolveInfo>,
    pub nip_id: i32,
    pub nip_color: [f32; 4],
    pub nip_gloss_power: f32,
    pub areola_size: f32,
    pub underhair_id: i32,
    pub underhair_color: [f32; 4],
    pub skin_sub_color: [f32; 4],
    /// ChaControl.CreateBodyTexture writes this as `_Color5`; mask blue selects nail tint.
    pub nail_color: [f32; 4],
    /// ChaFileParameter.sex: 0 = male, 1 = female (ChaInfo/ChaControl).
    pub sex: i32,
    /// Custom.body.typeBone (0 = vanilla bone layout).
    pub type_bone: i32,
    /// Status.nipStandRate blends body shape index 13.
    pub nip_stand_rate: f32,
    /// ChaFileStatus.eyesOpenMax; the saved status can be 1, while ChaControl
    /// separately limits the actual eye FBS controller to .92.
    pub eyes_open_max: f32,
    /// ChaFileStatus.eyebrowOpenMax, clamped to [0,1] by ChaControl.
    pub eyebrow_open_max: f32,
    pub eyes_blink: bool,
    /// ChaFileStatus.hohoAkaRate. `ChangeHohoAkaRate` sets `_overcolor2.a =
    /// lerp(0, 0.2, rate)` on `matDraw` (ChaControl.cs:7820).
    pub hoho_aka_rate: f32,
    /// ChaFileStatus.skinTuyaRate blends saved body gloss towards 1.
    pub skin_tuya_rate: f32,
    /// ChaFileStatus.tearsLv: 0 none, 1..3 the small / medium / large tears object (ChaControl.ChangeTearsLv).
    pub tears_lv: i32,
    /// ChaFileStatus.coordinateType: the outfit the character wears (-1: not saved).
    /// ChaFileStatus face: eyebrow / eyes / mouth pattern, eye and neck look pattern (-1: not in the card).
    pub status_eyebrow_ptn: i32,
    pub status_eyes_ptn: i32,
    pub status_mouth_ptn: i32,
    pub status_eyes_look_ptn: i32,
    pub status_neck_look_ptn: i32,
    pub status_coordinate: i32,
    /// ChaFileStatus.clothesState: per clothes part, 0 on, 1 half, 2/3 off (empty: not saved).
    pub status_clothes: Vec<u8>,
    /// ChaFileStatus.showAccessory: per accessory slot (20), whether it is shown.
    pub show_accessory: Vec<bool>,
    /// ChaFileStatus.siruLv: fluid level 0..2 per part (face, chest front, chest back, lower front, lower back).
    pub siru_lv: [i32; 5],
    // Face part ids / colors needed to resolve per-mesh textures (ChaControl).
    pub white_id: i32,
    pub eyebrow_id: i32,
    pub eyeline_up_id: i32,
    pub eyeline_down_id: i32,
    pub nose_id: i32,
    pub pupil_ids: [i32; 2],
    /// ChaFileFace.pupilWidth/Height/X/Y (defaults 0.9/0.9/0.5/0.5).
    pub pupil_width: f32,
    pub pupil_height: f32,
    pub pupil_x: f32,
    pub pupil_y: f32,
    pub lip_line_id: i32,
    /// ChaControl activates RefObjKey.ObjDoubleTooth from this saved face flag.
    pub double_tooth: bool,
    pub mole_id: i32,
    pub mole_color: [f32; 4],
    /// ChaFileFace.moleLayout (default 0.5,0.5,0,0.5).
    pub mole_layout: [f32; 4],
    pub paint_layouts: [[f32; 4]; 2],
    pub pupils: [PupilInfo; 2],
    pub hl_up_id: i32,
    pub hl_down_id: i32,
    pub hl_up_color: [f32; 4],
    pub hl_down_color: [f32; 4],
    pub hl_up_y: f32,
    pub hl_down_y: f32,
    pub hl_up_x: f32,
    pub hl_down_x: f32,
    pub detail_id: i32,
    /// ChaFileBody.detailId / detailPower drive Koikano/main_skin's
    /// `_NormalMapDetail` and `_DetailNormalMapScale` (ChaControl.cs:4807).
    pub body_detail_id: i32,
    pub body_detail_power: f32,
    /// ChaControl writes these to the body draw material, before MaterialEditor.
    pub skin_gloss_power: f32,
    pub nail_gloss_power: f32,
    /// Face draw values, distinct from the create-face texture parameters.
    pub face_detail_power: f32,
    pub cheek_gloss_power: f32,
    pub lip_gloss_power: f32,
    /// ChaControl.VisibleAddBodyLine overwrites cf_m_body._linetexon.
    pub body_draw_add_line: bool,
    pub white_base_color: [f32; 4],
    pub white_sub_color: [f32; 4],
    pub lip_line_color: [f32; 4],
    pub eye_hi_color: [f32; 4],
    pub cheek_id: i32,
    pub cheek_color: [f32; 4],
    pub paint_ids: [i32; 2],
    pub paint_colors: [[f32; 4]; 2],
    pub eyeshadow_id: i32,
    pub eyeshadow_color: [f32; 4],
    pub lip_id: i32,
    pub lip_color: [f32; 4],
    /// UncensorSelector BodyGUID (mod body replacement), e.g. "SAC.innie".
    pub body_guid: Option<String>,
    /// UncensorSelector PenisGUID / BallsGUID. Empty means the config default.
    pub penis_guid: Option<String>,
    pub balls_guid: Option<String>,
    /// ChaFileParameter values UncensorSelector folds into its stable random seed.
    pub personality: i32,
    pub birth_day: i32,
    /// `voicePitch` when the card has it, otherwise `voiceRate`.
    pub voice_pitch: f32,
    /// ABM/ABMX per-bone modifiers from `KKABMPlugin.ABMData`.
    pub abm: Vec<crate::abmx::AbmBone>,
    pub pregnancy:crate::pregnancy::Settings,
    /// KSOX skin overlays: (coordinate index, KoikatsuSkinOverlayType) -> PNG.
    /// Types: 1 BodyOver, 2 FaceOver, 3 BodyUnder, 4 FaceUnder (KoikatsuCard.cs).
    pub ksox: std::collections::HashMap<(i32, i32), Vec<u8>>,
    /// KCOX BodyMask/BraMask/InnerMask replace Material.SetTexture inputs.
    pub clothes_masks: std::collections::HashMap<(i32, String), Vec<u8>>,
    /// KCOX component-name keyed albedo layers; Override clears the RT first.
    pub clothes_overlays: std::collections::HashMap<(i32, String), (Vec<u8>, bool)>,
    /// Number of outfit coordinate slots in the card (`Coordinate` array).
    pub coordinate_count: usize,
    /// Material Editor `RendererProperties.Enabled == 0`.
    /// `(object_type, coordinate, slot, renderer name lowercased)`.
    /// ObjectType: 1 clothing, 2 accessory, 3 hair, 4 character.
    pub me_renderer_off: std::collections::HashSet<(i32, i32, i32, String)>,
    pub me_renderer_on: std::collections::HashSet<(i32, i32, i32, String)>,
    /// Material Editor `MaterialTexturePropertyList` resolved against
    /// `TextureDictionary`. Key is
    /// `(object_type, coordinate, slot, material lower, property lower)`.
    /// These PNGs are what the running material samples (SetTexture), not the
    /// list-item source the create-RT would have blitted.
    pub me_tex: MeTexMap,
    /// Material Editor `MaterialColorPropertyList`. Key matches `me_tex`
    /// without the texture id: `(object_type, coordinate, slot, material, property)`.
    pub me_color: MeColorMap,
    /// Material Editor `MaterialShaderList`: the runtime shader and optional
    /// render queue, keyed by object type, coordinate, slot and material.
    pub me_shader: MeShaderMap,
    /// Material Editor `MaterialCopyList`, replayed before shader and texture
    /// overrides. A copy adds another material pass over the same renderer.
    pub me_copies: Vec<MeMaterialCopy>,
    /// Material Editor numeric properties. The plugin stores their values as strings.
    pub me_float: MeFloatMap,
    /// HairAccessoryCustomizer `HairAccessories`: `(coordinate, slot)`.
    /// Coordinate-level `CoordinateHairAccessories` overrides the current outfit.
    pub hair_acc: std::collections::HashMap<(i32, i32), HairAccessoryInfo>,
}

/// `(object_type, coordinate, slot, material, property) -> shader color`.
pub type MeColorMap = std::collections::HashMap<(i32, i32, i32, String, String), [f32; 4]>;

/// `(object_type, coordinate, slot, material, property) -> PNG`.
pub type MeTexMap = std::collections::HashMap<(i32, i32, i32, String, String), std::sync::Arc<Vec<u8>>>;
pub type MeShaderMap = std::collections::HashMap<(i32, i32, i32, String), (String, Option<i32>)>;
pub type MeFloatMap = std::collections::HashMap<(i32, i32, i32, String, String), f32>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MeMaterialCopy {
    pub object_type: i32,
    pub coordinate: i32,
    pub slot: i32,
    pub source: String,
    pub copy: String,
}

pub(crate) fn me_mat_key(name: &str) -> String {
    // MaterialEditor's NameFormatted removes the suffix text, not its leading
    // separator. An instance marker before a modded material's hash therefore
    // leaves a significant space ("HairF _(<hash>)d").
    name.to_ascii_lowercase()
        .replace("(instance)", "")
        .replace(" instance", "")
        .trim()
        .to_string()
}

#[derive(Debug, Clone)]
pub struct ClothesColorPattern {
    /// ChaFileClothes.PartsInfo.ColorInfo.pattern (cat 430 id; 0 = none).
    pub pattern: i32,
    pub pattern_color: [f32; 4],
    /// create_topN DXBC uses `20 - 19 * tiling` (no saturation).
    pub tiling: [f32; 2],
    pub offset: [f32; 2],
    pub rotate: f32,
}

impl Default for ClothesColorPattern {
    fn default() -> Self {
        Self {
            pattern: 0,
            pattern_color: [1.0, 1.0, 1.0, 1.0],
            tiling: [0.0, 0.0],
            offset: [0.5, 0.5],
            rotate: 0.5,
        }
    }
}

#[derive(Debug, Clone)]
pub struct HairPart {
    pub id: i32,
    /// ChaControl.ChangeShakeHair disables prefab DynamicBone when true.
    pub no_shake: bool,
    pub length: f32,
    pub pos: [f32; 3],
    pub rot: [f32; 3],
    pub scl: [f32; 3],
    /// ChaFileHair.parts.baseColor → shader `_Color`.
    pub color: [f32; 4],
    /// `_Color2`
    pub start_color: [f32; 4],
    /// `_Color3`
    pub end_color: [f32; 4],
    /// ChaFileHair.parts.outlineColor → `_LineColor`.
    pub outline_color: [f32; 4],
    /// ChaFileHair.parts.glossColor → `_GlossColor`.
    pub gloss_color: [f32; 4],
}

#[derive(Debug, Clone)]
pub struct PupilInfo {
    pub id: i32,
    pub base_color: [f32; 4],
    pub sub_color: [f32; 4],
    pub grad_mask_id: i32,
    pub grad_blend: f32,
    pub grad_offset_y: f32,
    pub grad_scale: f32,
}

impl Default for PupilInfo {
    fn default() -> Self {
        Self {
            id: 0,
            base_color: [0.32, 0.55, 0.88, 1.0],
            sub_color: [0.32, 0.55, 0.88, 1.0],
            grad_mask_id: 0,
            grad_blend: 0.0,
            grad_offset_y: 0.5,
            grad_scale: 0.5,
        }
    }
}

impl Default for HairPart {
    fn default() -> Self {
        Self {
            id: 0,
            no_shake: false,
            length: 0.0,
            pos: [0.0; 3],
            rot: [0.0; 3],
            scl: [1.0, 1.0, 1.0],
            color: [1.0, 1.0, 1.0, 1.0],
            start_color: [1.0, 1.0, 1.0, 1.0],
            end_color: [1.0, 1.0, 1.0, 1.0],
            outline_color: [0.0, 0.0, 0.0, 1.0],
            gloss_color: [217.0 / 255.0, 217.0 / 255.0, 217.0 / 255.0, 1.0],
        }
    }
}

/// `addMove[j, 0/1/2]` for one of `N_move` (j=0) / `N_move2` (j=1).
/// `UpdateAccessoryMoveFromInfo` writes position×0.01, euler, scale.
#[derive(Debug, Clone, Copy)]
pub struct AccMove {
    pub pos: [f32; 3],
    pub rot: [f32; 3],
    pub scale: [f32; 3],
}

impl Default for AccMove {
    fn default() -> Self {
        Self {
            pos: [0.0; 3],
            rot: [0.0; 3],
            scale: [1.0, 1.0, 1.0],
        }
    }
}

/// HairAccessoryCustomizer `HairAccessoryInfo` (MessagePack keys).
#[derive(Debug, Clone, Copy)]
pub struct HairAccessoryInfo {
    pub color_match: bool,
    pub hair_gloss: bool,
    pub outline_color: [f32; 4],
    pub accessory_color: [f32; 4],
    pub hair_length: f32,
    pub gloss_color: [f32; 4],
}

impl Default for HairAccessoryInfo {
    fn default() -> Self {
        Self {
            color_match: true,
            hair_gloss: true,
            outline_color: [0.0, 0.0, 0.0, 1.0],
            accessory_color: [1.0, 0.0, 0.0, 1.0],
            hair_length: 0.0,
            gloss_color: [0.8490566, 0.8490566, 0.8490566, 1.0],
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct AccPart {
    pub type_id: i32,
    pub id: i32,
    /// ChaControl.ChangeShakeAccessory disables prefab DynamicBone when true.
    pub no_shake: bool,
    pub parent_key: String,
    /// j=0 mirror of `add_move[0]` (existing call sites).
    pub add_pos: [f32; 3],
    pub add_rot: [f32; 3],
    pub add_scale: [f32; 3],
    /// `addMove[j]` for `N_move` / `N_move2`.
    pub add_move: [AccMove; 2],
    /// ChaFileAccessory.parts.color[0] -> `_Color`
    pub color: [f32; 4],
    /// ChaFileAccessory.parts.color[1] -> `_Color2`
    pub color2: [f32; 4],
    /// ChaFileAccessory.parts.color[2] -> `_Color3`
    pub color3: [f32; 4],
    /// ChaFileAccessory.parts.color[3] -> `_Color4` (rendAlpha)
    pub color4: [f32; 4],
}

fn vec4(v: &Value) -> [f32; 4] {
    if let Some(a) = v.as_array() {
        return [
            a.first().map(as_f32).unwrap_or(0.0),
            a.get(1).map(as_f32).unwrap_or(0.0),
            a.get(2).map(as_f32).unwrap_or(0.0),
            a.get(3).map(as_f32).unwrap_or(0.0),
        ];
    }
    [
        map_get(v, "x").map(as_f32).unwrap_or(0.0),
        map_get(v, "y").map(as_f32).unwrap_or(0.0),
        map_get(v, "z").map(as_f32).unwrap_or(0.0),
        map_get(v, "w").map(as_f32).unwrap_or(0.0),
    ]
}

fn vec3(v: &Value) -> [f32; 3] {
    if let Some(a) = v.as_array() {
        return [
            a.first().map(as_f32).unwrap_or(0.0),
            a.get(1).map(as_f32).unwrap_or(0.0),
            a.get(2).map(as_f32).unwrap_or(0.0),
        ];
    }
    [
        map_get(v, "x").map(as_f32).unwrap_or(0.0),
        map_get(v, "y").map(as_f32).unwrap_or(0.0),
        map_get(v, "z").map(as_f32).unwrap_or(0.0),
    ]
}

fn read_u32_be(data: &[u8], pos: usize) -> Option<u32> {
    if pos + 4 > data.len() {
        return None;
    }
    Some(u32::from_be_bytes(data[pos..pos + 4].try_into().ok()?))
}

pub(crate) fn find_iend_end(data: &[u8]) -> Option<usize> {
    if data.len() < 8 {
        return None;
    }
    let mut pos = 8usize;
    while pos + 12 <= data.len() {
        let length = read_u32_be(data, pos)? as usize;
        let typ = &data[pos + 4..pos + 8];
        let next = pos.checked_add(12)?.checked_add(length)?;
        if next > data.len() {
            return None;
        }
        pos = next;
        if typ == b"IEND" {
            return Some(pos);
        }
    }
    None
}

fn read_7bit(cur: &mut Cursor<&[u8]>) -> Result<u32, String> {
    use std::io::Read;
    let mut result: u32 = 0;
    let mut shift = 0;
    for _ in 0..5 {
        let mut b = [0u8; 1];
        cur.read_exact(&mut b).map_err(|_| "eof 7bit")?;
        result |= u32::from(b[0] & 0x7f) << shift;
        if b[0] & 0x80 == 0 {
            return Ok(result);
        }
        shift += 7;
    }
    Err("7bit overflow".into())
}

pub(crate) fn read_dotnet_string(cur: &mut Cursor<&[u8]>) -> Result<String, String> {
    use std::io::Read;
    let n = read_7bit(cur)? as usize;
    let mut buf = vec![0u8; n];
    cur.read_exact(&mut buf).map_err(|_| "eof string")?;
    Ok(String::from_utf8_lossy(&buf).into_owned())
}

pub(crate) fn read_i32_le(cur: &mut Cursor<&[u8]>) -> Result<i32, String> {
    use std::io::Read;
    let mut b = [0u8; 4];
    cur.read_exact(&mut b).map_err(|_| "eof i32")?;
    Ok(i32::from_le_bytes(b))
}

pub(crate) fn read_i64_le(cur: &mut Cursor<&[u8]>) -> Result<i64, String> {
    use std::io::Read;
    let mut b = [0u8; 8];
    cur.read_exact(&mut b).map_err(|_| "eof i64")?;
    Ok(i64::from_le_bytes(b))
}

fn map_get<'a>(v: &'a Value, key: &str) -> Option<&'a Value> {
    let map = v.as_map()?;
    for (k, val) in map {
        if k.as_str() == Some(key) {
            return Some(val);
        }
        if let Some(n) = k.as_i64() {
            if n.to_string() == key {
                return Some(val);
            }
        }
    }
    None
}

fn as_i32(v: &Value) -> i32 {
    v.as_i64().unwrap_or(0) as i32
}

fn as_str(v: &Value) -> String {
    v.as_str().unwrap_or("").to_string()
}

fn as_f32(v: &Value) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn float_array(v: &Value) -> Vec<f32> {
    v.as_array()
        .map(|a| a.iter().map(as_f32).collect())
        .unwrap_or_default()
}

fn parse_resolve_blob(bytes: &[u8], out: &mut Vec<ResolveInfo>) {
    let val = rmpv::decode::read_value(&mut Cursor::new(bytes)).ok();
    let Some(val) = val else {
        return;
    };
    let mod_id = map_get(&val, "ModID").map(as_str).unwrap_or_default();
    if mod_id.is_empty() {
        return;
    }
    out.push(ResolveInfo {
        mod_id,
        slot: map_get(&val, "Slot").map(as_i32).unwrap_or(0),
        local_slot: map_get(&val, "LocalSlot").map(as_i32).unwrap_or(0),
        property: map_get(&val, "Property").map(as_str).unwrap_or_default(),
        category_no: map_get(&val, "CategoryNo").map(as_i32).unwrap_or(0),
        name: map_get(&val, "Name").map(as_str).unwrap_or_default(),
    });
}

fn parse_kkex(val: &Value, out: &mut Vec<ResolveInfo>) {
    const KEYS: &[&str] = &[
        "com.bepis.sideloader.universalautoresolver",
        "EC.Core.Sideloader.UniversalAutoResolver",
    ];
    let Some(plugin) = KEYS.iter().find_map(|k| map_get(val, k)) else {
        return;
    };
    let data = if let Some(arr) = plugin.as_array() {
        arr.get(1)
    } else {
        map_get(plugin, "1").or(Some(plugin))
    };
    let Some(data) = data else {
        return;
    };
    let Some(info) = map_get(data, "info") else {
        return;
    };
    let Some(arr) = info.as_array() else {
        return;
    };
    for item in arr {
        if let Some(bin) = item.as_slice() {
            parse_resolve_blob(bin, out);
        } else if item.is_map() {
            parse_resolve_blob(&rmp_encode(item), out);
        }
    }
}

fn rmp_encode(v: &Value) -> Vec<u8> {
    let mut buf = Vec::new();
    let _ = rmpv::encode::write_value(&mut buf, v);
    buf
}

/// Material Editor `RendererPropertyList`. `Property` 0 is `Enabled`;
/// `Value` `"0"` means the game will not draw that renderer.
fn parse_material_editor(
    plugin: &Value,
    out: &mut std::collections::HashSet<(i32, i32, i32, String)>,
) {
    parse_material_editor_visibility(plugin,out,"0")
}
fn parse_material_editor_visibility(plugin:&Value,out:&mut std::collections::HashSet<(i32,i32,i32,String)>,requested:&str) {
    let data = if let Some(arr) = plugin.as_array() {
        arr.get(1)
    } else {
        map_get(plugin, "1").or(Some(plugin))
    };
    let Some(data) = data else {
        return;
    };
    let inner = map_get(data, "data").unwrap_or(data);
    let Some(bin) = map_get(inner, "RendererPropertyList").and_then(|v| v.as_slice()) else {
        return;
    };
    let Ok(list) = rmpv::decode::read_value(&mut Cursor::new(bin)) else {
        return;
    };
    let Some(arr) = list.as_array() else {
        return;
    };
    for item in arr {
        let prop = map_get(item, "Property").and_then(|x| x.as_i64()).unwrap_or(-1);
        let val = map_get(item, "Value").and_then(|x| x.as_str()).unwrap_or("");
        if prop != 0 || val != requested {
            continue;
        }
        let ot = map_get(item, "ObjectType").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        let coord = map_get(item, "CoordinateIndex").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        let slot = map_get(item, "Slot").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        let name = map_get(item, "RendererName")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .trim()
            .to_ascii_lowercase();
        if name.is_empty() {
            continue;
        }
        out.insert((ot, coord, slot, name));
    }
    if !out.is_empty() && card_diagnostics_enabled() {
        eprintln!("ok-extract: material editor renderer off {}", out.len());
    }
}

/// DynamicBoneEditor's `AccessoryDynamicBoneData`: a MessagePack list of DynamicBoneData
/// (`CoordinateIndex`, `Slot`, `BoneName` = the component's m_Root name, and the edited `FreezeAxis`, `Weight`,
/// `Damping`, `Elasticity`, `Stiffness`, `Inertia`, `Radius`; nil keeps the prefab value; `*Original` is undo data).
fn parse_dynamic_bone_editor(data: &Value) -> Vec<crate::dbone::DynamicBoneEdit> {
    let Some(raw) = map_get(data, "AccessoryDynamicBoneData") else { return Vec::new() };
    let decoded = match raw.as_slice() { Some(bytes) => rmpv::decode::read_value(&mut Cursor::new(bytes)).ok(), None => Some(raw.clone()) };
    let Some(Value::Array(rows)) = decoded else { return Vec::new() };
    let number = |row: &Value, key: &str| map_get(row, key).and_then(|v| v.as_f64().or_else(|| v.as_i64().map(|i| i as f64))).map(|v| v as f32);
    rows.iter().filter_map(|row| Some(crate::dbone::DynamicBoneEdit {
        coordinate: map_get(row, "CoordinateIndex")?.as_i64()? as i32,
        slot: map_get(row, "Slot")?.as_i64()? as i32,
        bone: map_get(row, "BoneName")?.as_str()?.to_owned(),
        freeze_axis: map_get(row, "FreezeAxis").and_then(Value::as_i64).map(|a| a.clamp(0, 3) as usize),
        weight: number(row, "Weight"),
        damping: number(row, "Damping"),
        elasticity: number(row, "Elasticity"),
        stiffness: number(row, "Stiffness"),
        inertia: number(row, "Inertia"),
        radius: number(row, "Radius"),
    })).collect()
}

fn me_plugin_data<'a>(plugin: &'a Value) -> Option<&'a Value> {
    let data = if let Some(arr) = plugin.as_array() {
        arr.get(1)
    } else {
        map_get(plugin, "1").or(Some(plugin))
    }?;
    Some(map_get(data, "data").unwrap_or(data))
}

// KKS Pushup saves the original body sliders in Pushup_BodyData.  On reload it
// writes the effective sliders back to ChaFileBody before ShapeBodyInfoFemale
// evaluates its animation keys (PushupController.RecalculateBody).  The PNG's
// shapeValueBody is therefore not the shape worn with outfit 0.
const PUSHUP_SHAPE_KEYS: [&str; 10] = [
    "Size", "VerticalPosition", "HorizontalAngle", "HorizontalPosition",
    "VerticalAngle", "Depth", "Roundness", "AreolaDepth", "NippleWidth", "NippleDepth",
];

#[derive(Clone, Copy)]
struct PushupCloth {
    shape: [f32; 10],
    firmness: f32,
    lift: f32,
    together: f32,
    squeeze: f32,
    center: f32,
    enabled: bool,
    flatten: bool,
    advanced: bool,
}

fn pushup_shape(v: &Value, fallback: &[f32]) -> [f32; 10] {
    let mut out = [0.5; 10];
    for (i, key) in PUSHUP_SHAPE_KEYS.iter().enumerate() {
        out[i] = map_get(v, key).map(as_f32)
            .or_else(|| fallback.get(i + 4).copied()).unwrap_or(0.5);
    }
    out
}

fn pushup_cloth(v: &Value, fallback: &[f32]) -> PushupCloth {
    let num = |key: &str, default: f32| map_get(v, key).map(as_f32).unwrap_or(default);
    let flag = |key: &str, default: bool| map_get(v, key).and_then(Value::as_bool).unwrap_or(default);
    PushupCloth {
        shape: pushup_shape(v, fallback),
        firmness: num("Firmness", 0.9), lift: num("Lift", 0.6),
        together: num("PushTogether", 0.55), squeeze: num("Squeeze", 0.6),
        center: num("CenterNipples", 0.5),
        enabled: flag("EnablePushup", false), flatten: flag("FlattenNipples", true),
        advanced: flag("UseAdvanced", false),
    }
}

fn pushup_value(inner: &Value, key: &str) -> Option<Value> {
    let bytes = map_get(inner, key)?.as_slice()?;
    rmpv::decode::read_value(&mut Cursor::new(bytes)).ok()
}

fn pushup_coord(inner: &Value, key: &str, outfit: i32, fallback: &[f32]) -> Option<PushupCloth> {
    let dict = pushup_value(inner, key)?;
    let entry = dict.as_map()?.iter().find(|(k, _)| k.as_i64() == Some(outfit as i64))?.1.clone();
    Some(pushup_cloth(&entry, fallback))
}

/// Port of PushupController.CalculatePush/CalculatePushFromClothes.  A missing
/// plugin record leaves the original card sliders untouched.
fn apply_pushup_shape(plugin: &Value, outfit: i32, clothes: &[i32], shape: &mut [f32]) {
    if shape.len() < 14 { return; }
    let Some(inner) = me_plugin_data(plugin) else { return; };
    let base = pushup_value(inner, "Pushup_BodyData")
        .map(|v| pushup_shape(&v, shape)).unwrap_or_else(|| pushup_shape(&Value::Nil, shape));
    let bra = pushup_coord(inner, "Pushup_BraData", outfit, shape)
        .filter(|c| c.enabled && clothes.get(2).copied().unwrap_or(0) != 0);
    let top = pushup_coord(inner, "Pushup_TopData", outfit, shape)
        .filter(|c| c.enabled && clothes.first().copied().unwrap_or(0) != 0);
    let selected = match (bra, top) {
        (None, None) => return,
        (Some(b), None) => b,
        (None, Some(t)) => t,
        (Some(_), Some(t)) if t.advanced => t,
        (Some(b), Some(_)) if b.advanced => b,
        (Some(b), Some(t)) => PushupCloth {
            firmness: b.firmness.max(t.firmness), lift: b.lift.max(t.lift),
            together: b.together.max(t.together), squeeze: b.squeeze.max(t.squeeze),
            center: b.center.max(t.center), flatten: b.flatten || t.flatten,
            advanced: false, enabled: true, shape: base,
        },
    };
    let mut effective = if selected.advanced { selected.shape } else { base };
    if !selected.advanced {
        effective[1] = base[1].max(selected.lift);
        effective[2] = base[2].min(1.0 - selected.together);
        effective[3] = base[3].min(1.0 - selected.together);
        let depth_limit = 1.0 - selected.squeeze;
        if depth_limit < base[5] {
            effective[5] = depth_limit;
            effective[0] = base[0] + (base[5] - depth_limit) / 10.0;
        }
        effective[4] = 0.5 - (0.5 - base[4]) * (1.0 - selected.center);
        if selected.flatten { effective[7] = 0.0; effective[9] = 0.0; }
    }
    shape[4..14].copy_from_slice(&effective);
}

/// Material Editor stores the textures the maker assigned (`TextureDictionary`
/// id -> PNG) and which material slot they replace
/// (`MaterialTexturePropertyList`). Card hair/eye colours can stay at the
/// list default while these PNGs are what the game actually draws.
fn parse_material_editor_textures(plugin: &Value, out: &mut MeTexMap) {
    parse_material_editor_textures_borrowed(plugin, None, out)
}

type PngDictionary=std::collections::HashMap<i64,std::sync::Arc<Vec<u8>>>;
struct CachedPngDictionary {
    bytes:Vec<u8>,pngs:std::sync::Arc<PngDictionary>,retained_bytes:usize,
}
/// Owned by one editing context. Exact byte equality owns cache reuse; no
/// sampled hash, borrowed pointer, plugin name, or character ID is an identity.
pub(crate) struct PngDictionaryCache {
    entries:std::collections::VecDeque<CachedPngDictionary>,
    budget:usize,limit:usize,retained_bytes:usize,hits:usize,misses:usize,
    pub parse_phases:Vec<(String,[f64;3])>,
}
impl Default for PngDictionaryCache {
    fn default()->Self{Self{entries:Default::default(),budget:128*1024*1024,limit:8,retained_bytes:0,hits:0,misses:0,parse_phases:Vec::new()}}
}
impl PngDictionaryCache {
    pub fn stats(&self)->[usize;2]{[self.hits,self.misses]}
    pub fn retained_bytes(&self)->usize{self.retained_bytes}
    fn read(&mut self,bytes:&[u8])->Option<std::sync::Arc<PngDictionary>>{
        if let Some(index)=self.entries.iter().position(|entry|entry.bytes.as_slice()==bytes){
            let entry=self.entries.remove(index)?;let pngs=entry.pngs.clone();self.entries.push_back(entry);
            self.hits=self.hits.saturating_add(1);return Some(pngs);
        }
        self.misses=self.misses.saturating_add(1);
        let pngs=std::sync::Arc::new(read_png_dictionary(bytes)?);
        let retained_bytes=bytes.len().saturating_add(pngs.values().map(|png|png.len()).sum::<usize>());
        if retained_bytes<=self.budget&&self.limit>0{
            while self.entries.len()>=self.limit||self.retained_bytes>self.budget-retained_bytes{
                let oldest=self.entries.pop_front()?;self.retained_bytes-=oldest.retained_bytes;
            }
            self.entries.push_back(CachedPngDictionary{bytes:bytes.to_vec(),pngs:pngs.clone(),retained_bytes});
            self.retained_bytes+=retained_bytes;
        }
        Some(pngs)
    }
}
fn read_png_dictionary(bytes:&[u8])->Option<PngDictionary>{
    let value=rmpv::decode::read_value_ref(&mut Cursor::new(bytes)).ok()?;
    let rmpv::ValueRef::Map(dict)=value else{return None};
    let mut pngs=std::collections::HashMap::new();
    for(key,value)in &dict{
        let id=match key{rmpv::ValueRef::Integer(id)=>id.as_i64(),_=>None};
        let bytes=match value{rmpv::ValueRef::Binary(bytes)=>Some(*bytes),rmpv::ValueRef::String(text)=>Some(text.as_bytes()),_=>None};
        if let(Some(id),Some(bytes))=(id,bytes){if bytes.starts_with(b"\x89PNG"){pngs.insert(id,bytes);}}
    }
    // Last valid PNG for a duplicate ID wins, as before. Only final values
    // acquire ownership; bindings to one ID share the same immutable PNG.
    Some(pngs.into_iter().map(|(id,bytes)|(id,std::sync::Arc::new(bytes.to_vec()))).collect())
}

fn parse_material_editor_textures_borrowed(plugin: &Value, dictionary: Option<&[u8]>, out: &mut MeTexMap) {
    parse_material_editor_textures_cached(plugin,dictionary,out,None)
}
fn parse_material_editor_textures_cached(plugin:&Value,dictionary:Option<&[u8]>,out:&mut MeTexMap,
    cache:Option<&mut PngDictionaryCache>){
    let Some(inner) = me_plugin_data(plugin) else {
        return;
    };
    let Some(dict_bin) = dictionary.or_else(||map_get(inner, "TextureDictionary").and_then(|v| v.as_slice())) else {
        return;
    };
    let pngs=if let Some(cache)=cache{cache.read(dict_bin)}else{read_png_dictionary(dict_bin).map(std::sync::Arc::new)};
    let Some(pngs)=pngs else{return};
    let Some(list_bin) = map_get(inner, "MaterialTexturePropertyList").and_then(|v| v.as_slice())
    else {
        return;
    };
    let Ok(list_val) = rmpv::decode::read_value(&mut Cursor::new(list_bin)) else {
        return;
    };
    let Some(arr) = list_val.as_array() else {
        return;
    };
    let texture_diagnostics=std::env::var("OK_ME_TEX_DIAG").as_deref()==Ok("1");
    for item in arr {
        let Some(tex_id) = map_get(item, "TexID").and_then(|x| x.as_i64()) else {
            continue;
        };
        let Some(png) = pngs.get(&tex_id) else {
            continue;
        };
        let mat = map_get(item, "MaterialName")
            .and_then(|x| x.as_str())
            .unwrap_or("");
        let prop = map_get(item, "Property")
            .and_then(|x| x.as_str())
            .unwrap_or("");
        if mat.is_empty() || prop.is_empty() {
            continue;
        }
        let ot = map_get(item, "ObjectType").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        let coord = map_get(item, "CoordinateIndex")
            .and_then(|x| x.as_i64())
            .unwrap_or(0) as i32;
        let slot = map_get(item, "Slot").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        if texture_diagnostics {
            eprintln!("ME texture saved object={ot} coord={coord} slot={slot} material={mat:?} property={prop:?} bytes={}",png.len());
        }
        out.insert(
            (ot, coord, slot, me_mat_key(mat), prop.to_ascii_lowercase()),
            png.clone(),
        );
    }
    if !out.is_empty() && card_diagnostics_enabled() {
        eprintln!("ok-extract: material editor textures {}", out.len());
    }
}

/// Material Editor `MaterialColorPropertyList`. Applied after
/// `ChangeAccessoryColor` / HairAccessoryCustomizer, before the texture swap.
fn parse_material_editor_colors(plugin: &Value, out: &mut MeColorMap) {
    let Some(inner) = me_plugin_data(plugin) else {
        return;
    };
    let Some(bin) = map_get(inner, "MaterialColorPropertyList").and_then(|v| v.as_slice()) else {
        return;
    };
    let Ok(list_val) = rmpv::decode::read_value(&mut Cursor::new(bin)) else {
        return;
    };
    let Some(arr) = list_val.as_array() else {
        return;
    };
    for item in arr {
        let mat = map_get(item, "MaterialName")
            .and_then(|x| x.as_str())
            .unwrap_or("");
        let prop = map_get(item, "Property")
            .and_then(|x| x.as_str())
            .unwrap_or("");
        if mat.is_empty() || prop.is_empty() {
            continue;
        }
        let Some(val) = map_get(item, "Value") else {
            continue;
        };
        let ot = map_get(item, "ObjectType").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        let coord = map_get(item, "CoordinateIndex")
            .and_then(|x| x.as_i64())
            .unwrap_or(0) as i32;
        let slot = map_get(item, "Slot").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
        out.insert(
            (ot, coord, slot, me_mat_key(mat), prop.to_ascii_lowercase()),
            parse_color(val),
        );
    }
}

fn parse_material_editor_shaders(plugin: &Value, out: &mut MeShaderMap) {
    let Some(inner) = me_plugin_data(plugin) else { return; };
    let Some(bin) = map_get(inner, "MaterialShaderList").and_then(Value::as_slice) else { return; };
    let Ok(list) = rmpv::decode::read_value(&mut Cursor::new(bin)) else { return; };
    let Some(items) = list.as_array() else { return; };
    for item in items {
        let material = map_get(item, "MaterialName").and_then(Value::as_str).unwrap_or("");
        let shader = map_get(item, "ShaderName").and_then(Value::as_str).unwrap_or("");
        if material.is_empty() || shader.is_empty() { continue; }
        let ot = map_get(item, "ObjectType").and_then(Value::as_i64).unwrap_or(0) as i32;
        let coord = map_get(item, "CoordinateIndex").and_then(Value::as_i64).unwrap_or(0) as i32;
        let slot = map_get(item, "Slot").and_then(Value::as_i64).unwrap_or(0) as i32;
        let queue = map_get(item, "RenderQueue").and_then(Value::as_i64).map(|x| x as i32);
        out.insert((ot, coord, slot, me_mat_key(material)), (shader.to_string(), queue));
    }
}

fn parse_material_editor_copies(plugin: &Value, out: &mut Vec<MeMaterialCopy>) {
    let Some(inner) = me_plugin_data(plugin) else { return; };
    let Some(bin) = map_get(inner, "MaterialCopyList").and_then(Value::as_slice) else { return; };
    let Ok(list) = rmpv::decode::read_value(&mut Cursor::new(bin)) else { return; };
    let Some(items) = list.as_array() else { return; };
    for item in items {
        let source = me_mat_key(map_get(item, "MaterialName").and_then(Value::as_str).unwrap_or(""));
        let copy = me_mat_key(map_get(item, "MaterialCopyName").and_then(Value::as_str).unwrap_or(""));
        if source.is_empty() || copy.is_empty() { continue; }
        let copy = MeMaterialCopy {
            object_type: map_get(item, "ObjectType").and_then(Value::as_i64).unwrap_or(0) as i32,
            coordinate: map_get(item, "CoordinateIndex").and_then(Value::as_i64).unwrap_or(0) as i32,
            slot: map_get(item, "Slot").and_then(Value::as_i64).unwrap_or(0) as i32,
            source, copy,
        };
        if !out.contains(&copy) { out.push(copy); }
    }
}

fn parse_material_editor_floats(plugin: &Value, out: &mut MeFloatMap) {
    let Some(inner) = me_plugin_data(plugin) else { return; };
    let Some(bin) = map_get(inner, "MaterialFloatPropertyList").and_then(Value::as_slice) else { return; };
    let Ok(list) = rmpv::decode::read_value(&mut Cursor::new(bin)) else { return; };
    let Some(items) = list.as_array() else { return; };
    for item in items {
        let material = me_mat_key(map_get(item, "MaterialName").and_then(Value::as_str).unwrap_or(""));
        let property = map_get(item, "Property").and_then(Value::as_str).unwrap_or("").to_ascii_lowercase();
        let Some(value) = map_get(item, "Value").and_then(Value::as_str).and_then(|s| s.parse::<f32>().ok()) else { continue; };
        if material.is_empty() || property.is_empty() { continue; }
        let ot = map_get(item, "ObjectType").and_then(Value::as_i64).unwrap_or(0) as i32;
        let coord = map_get(item, "CoordinateIndex").and_then(Value::as_i64).unwrap_or(0) as i32;
        let slot = map_get(item, "Slot").and_then(Value::as_i64).unwrap_or(0) as i32;
        out.insert((ot, coord, slot, material, property), value);
    }
}

/// MaterialAPI applies nullable texture offset/scale independently of TexID.
/// Reuse the material ST component transport; untouched axes retain authored
/// values. Original fields are restoration history, not load-time overrides.
fn parse_material_editor_texture_transforms(plugin: &Value, out: &mut MeFloatMap) {
    let Some(inner)=me_plugin_data(plugin) else{return};
    let Some(bin)=map_get(inner,"MaterialTexturePropertyList").and_then(Value::as_slice) else{return};
    let Ok(list)=rmpv::decode::read_value(&mut Cursor::new(bin)) else{return};
    let Some(items)=list.as_array() else{return};
    for item in items {
        let material=me_mat_key(map_get(item,"MaterialName").and_then(Value::as_str).unwrap_or(""));
        let property=map_get(item,"Property").and_then(Value::as_str).unwrap_or("").trim_start_matches('_').to_ascii_lowercase();
        if material.is_empty() || property.is_empty(){continue;}
        let ot=map_get(item,"ObjectType").and_then(Value::as_i64).unwrap_or(0) as i32;
        let coord=map_get(item,"CoordinateIndex").and_then(Value::as_i64).unwrap_or(0) as i32;
        let slot=map_get(item,"Slot").and_then(Value::as_i64).unwrap_or(0) as i32;
        for (field,axes) in [("Scale",["x","y"]),("Offset",["z","w"])] {
            let Some(values)=map_get(item,field).and_then(Value::as_array).filter(|v|v.len()==2) else{continue};
            let pair:Option<Vec<f32>>=values.iter().map(|v|v.as_f64().map(|n|n as f32).filter(|n|n.is_finite())).collect();
            let Some(pair)=pair else{continue};
            for (axis,value) in axes.iter().zip(pair) {
                out.insert((ot,coord,slot,material.clone(),format!("{property}_st_{axis}")),value);
            }
        }
    }
}

fn parse_hair_accessory_info(v: &Value) -> HairAccessoryInfo {
    let mut info = HairAccessoryInfo::default();
    if let Some(b) = map_get(v, "ColorMatch").and_then(|x| x.as_bool()) {
        info.color_match = b;
    }
    if let Some(b) = map_get(v, "HairGloss").and_then(|x| x.as_bool()) {
        info.hair_gloss = b;
    }
    if let Some(c) = map_get(v, "OutlineColor") {
        info.outline_color = parse_color(c);
    }
    if let Some(c) = map_get(v, "AccessoryColor") {
        info.accessory_color = parse_color(c);
    }
    if let Some(n) = map_get(v, "HairLength") {
        info.hair_length = as_f32(n);
    }
    if let Some(c) = map_get(v, "GlossColor") {
        info.gloss_color = parse_color(c);
    }
    info
}

fn insert_hair_acc_slots(
    coord: i32,
    slots: &Value,
    out: &mut std::collections::HashMap<(i32, i32), HairAccessoryInfo>,
) {
    let Some(map) = slots.as_map() else {
        return;
    };
    for (k, v) in map {
        let Some(slot) = k.as_i64() else {
            continue;
        };
        out.insert((coord, slot as i32), parse_hair_accessory_info(v));
    }
}

/// KKEx `com.deathweasel.bepinex.hairaccessorycustomizer`.
/// `HairAccessories` is `Dictionary<coordinate, Dictionary<slot, HairAccessoryInfo>>`.
/// `CoordinateHairAccessories` (no coordinate key) overrides `outfit`.
fn parse_hair_accessories(
    plugin: &Value,
    outfit: i32,
    out: &mut std::collections::HashMap<(i32, i32), HairAccessoryInfo>,
) {
    let Some(inner) = me_plugin_data(plugin) else {
        return;
    };
    if let Some(bin) = map_get(inner, "HairAccessories").and_then(|v| v.as_slice()) {
        if let Ok(val) = rmpv::decode::read_value(&mut Cursor::new(bin)) {
            if let Some(map) = val.as_map() {
                for (k, v) in map {
                    let Some(coord) = k.as_i64() else {
                        continue;
                    };
                    insert_hair_acc_slots(coord as i32, v, out);
                }
            }
        }
    } else if let Some(val) = map_get(inner, "HairAccessories") {
        if let Some(map) = val.as_map() {
            for (k, v) in map {
                let Some(coord) = k.as_i64() else {
                    continue;
                };
                insert_hair_acc_slots(coord as i32, v, out);
            }
        }
    }
    // Coordinate card / embedded coordinate extended data: this outfit wins.
    let coord_bin = map_get(inner, "CoordinateHairAccessories").and_then(|v| v.as_slice());
    if let Some(bin) = coord_bin {
        if let Ok(val) = rmpv::decode::read_value(&mut Cursor::new(bin)) {
            insert_hair_acc_slots(outfit, &val, out);
        }
    } else if let Some(val) = map_get(inner, "CoordinateHairAccessories") {
        insert_hair_acc_slots(outfit, val, out);
    }
}

/// MessagePack-CSharp `Vector3[2,3]` is `[2, 3, flat]` with flat row-major
/// `[j0pos, j0rot, j0scl, j1pos, j1rot, j1scl]`. A nested
/// `[[pos,rot,scl],[pos,rot,scl]]` layout is also accepted.
fn xml_attr<'a>(tag: &'a str, name: &str) -> Option<&'a str> {
    let key = format!("{name}=\"");
    let i = tag.find(&key)?;
    let rest = &tag[i + key.len()..];
    let end = rest.find('"')?;
    Some(&rest[..end])
}

fn xml_f32(tag: &str, name: &str) -> f32 {
    xml_attr(tag, name).and_then(|s| s.parse().ok()).unwrap_or(0.0)
}

/// MoreAccessories `additionalAccessories` XML. Extra parts are concatenated
/// after the vanilla 20 (`CharAdditionalData` + `ChaControl` range patch), so
/// slot index is `20 + order` inside the matching `accessorySet type="{coord}"`.
fn append_more_accessories(xml: &str, outfit: i32, info: &mut CardInfo) {
    let mut extra = Vec::new();
    let mut in_set = false;
    for raw in xml.split('<').skip(1) {
        let tag = raw.split('>').next().unwrap_or("");
        let name = tag.split_whitespace().next().unwrap_or("");
        if name == "accessorySet" {
            let ty = xml_attr(tag, "type").and_then(|s| s.parse::<i32>().ok());
            in_set = ty == Some(outfit);
            continue;
        }
        if name.starts_with('/') {
            if name == "/accessorySet" {
                in_set = false;
            }
            continue;
        }
        if !in_set || name != "accessory" {
            continue;
        }
        let type_id = xml_attr(tag, "type").and_then(|s| s.parse().ok()).unwrap_or(120);
        if type_id == 120 {
            extra.push(AccPart::default());
            continue;
        }
        let mut moves = [AccMove::default(); 2];
        for j in 0..2 {
            moves[j].pos = [
                xml_f32(tag, &format!("addMove{j}0x")),
                xml_f32(tag, &format!("addMove{j}0y")),
                xml_f32(tag, &format!("addMove{j}0z")),
            ];
            moves[j].rot = [
                xml_f32(tag, &format!("addMove{j}1x")),
                xml_f32(tag, &format!("addMove{j}1y")),
                xml_f32(tag, &format!("addMove{j}1z")),
            ];
            moves[j].scale = [
                xml_f32(tag, &format!("addMove{j}2x")),
                xml_f32(tag, &format!("addMove{j}2y")),
                xml_f32(tag, &format!("addMove{j}2z")),
            ];
            if moves[j].scale == [0.0; 3] {
                moves[j].scale = [1.0, 1.0, 1.0];
            }
        }
        let color_of = |k: usize| -> [f32; 4] {
            [
                xml_f32(tag, &format!("color{k}r")),
                xml_f32(tag, &format!("color{k}g")),
                xml_f32(tag, &format!("color{k}b")),
                xml_f32(tag, &format!("color{k}a")),
            ]
        };
        extra.push(AccPart {
            type_id,
            id: xml_attr(tag, "id").and_then(|s| s.parse().ok()).unwrap_or(0),
            no_shake: xml_attr(tag, "noShake").is_some_and(|s| s.eq_ignore_ascii_case("true")),
            parent_key: xml_attr(tag, "parentKey").unwrap_or("").to_string(),
            add_pos: moves[0].pos,
            add_rot: moves[0].rot,
            add_scale: moves[0].scale,
            add_move: moves,
            color: color_of(0),
            color2: color_of(1),
            color3: color_of(2),
            color4: color_of(3),
        });
    }
    if extra.is_empty() {
        return;
    }
    if info.accessory.len() < 20 {
        info.accessory.resize(20, AccPart::default());
    }
    info.accessory.truncate(20);
    info.accessory.extend(extra);
    if card_diagnostics_enabled() { eprintln!(
        "ok-extract: more accessories outfit={outfit} slots={}",
        info.accessory.len()
    ); }
}

fn read_add_moves(arr: &[Value]) -> [AccMove; 2] {
    let mut out = [AccMove::default(); 2];
    let fill_row = |row: &[Value], dst: &mut AccMove| {
        if let Some(v) = row.first() {
            dst.pos = vec3(v);
        }
        if let Some(v) = row.get(1) {
            dst.rot = vec3(v);
        }
        if let Some(v) = row.get(2) {
            dst.scale = vec3(v);
        }
    };
    if let Some(flat) = arr.get(2).and_then(|v| v.as_array()) {
        let looks_flat = flat.len() >= 6
            && flat.first().and_then(|e| e.as_array()).is_some();
        if looks_flat {
            for j in 0..2 {
                let base = j * 3;
                if let Some(slice) = flat.get(base..base + 3) {
                    fill_row(slice, &mut out[j]);
                }
            }
            return out;
        }
    }
    let nested = arr
        .first()
        .and_then(|v| v.as_array())
        .and_then(|row| row.first())
        .map(|e| e.as_array().is_some())
        .unwrap_or(false);
    if nested {
        for j in 0..2 {
            if let Some(row) = arr.get(j).and_then(|v| v.as_array()) {
                fill_row(row, &mut out[j]);
            }
        }
        return out;
    }
    if let Some(row) = arr.first().and_then(|v| v.as_array()) {
        fill_row(row, &mut out[0]);
    }
    out
}

/// KoiClothesOverlayController.ReadOverlayExtData / BodyMaskHook.
/// Mask textures replace the input regardless of ClothesTexData.Override;
/// that flag controls clothing-albedo compositing, not mask replacement.
fn parse_clothes_masks(plugin: &Value, out: &mut std::collections::HashMap<(i32, String), Vec<u8>>) {
    let Some(data)=me_plugin_data(plugin) else {return};
    let Some(bytes)=map_get(data,"Overlays").and_then(Value::as_slice) else {return};
    let Some(overlays)=read_msgpack(bytes) else {return};
    let Some(coordinates)=overlays.as_map() else {return};
    for (coordinate,values) in coordinates {
        let Some(coordinate)=coordinate.as_i64().and_then(|v|i32::try_from(v).ok()) else {continue};
        let Some(values)=values.as_map() else {continue};
        for (name,value) in values {
            let Some(name)=name.as_str().filter(|n| matches!(*n,"BodyMask"|"BraMask"|"InnerMask")) else {continue};
            let texture=value.as_array().and_then(|v|v.first())
                .or_else(||value.as_map().and_then(|v|v.iter().find(|(k,_)|k.as_i64()==Some(0)).map(|(_,v)|v)));
            if let Some(bytes)=texture.and_then(Value::as_slice).filter(|b|!b.is_empty()) {
                out.insert((coordinate,name.to_string()),bytes.to_vec());
            }
        }
    }
}

fn parse_clothes_overlays(plugin: &Value, out: &mut std::collections::HashMap<(i32, String), (Vec<u8>,bool)>) {
    let Some(data)=me_plugin_data(plugin) else{return};
    // The importer reproduces Studio; this flag does not disable mask hooks.
    if map_get(data,"EnableInStudio").and_then(Value::as_bool)==Some(false){return;}
    let Some(bytes)=map_get(data,"Overlays").and_then(Value::as_slice) else{return};
    let Some(overlays)=read_msgpack(bytes) else{return};
    let Some(coordinates)=overlays.as_map() else{return};
    for (coordinate,values) in coordinates {
        let Some(coordinate)=coordinate.as_i64().and_then(|v|i32::try_from(v).ok()) else{continue};
        let Some(values)=values.as_map() else{continue};
        for (name,value) in values {
            let Some(name)=name.as_str().filter(|n|matches!(*n,"ct_clothesTop"|"ct_clothesBot"|"ct_bra"|"ct_shorts"|"ct_gloves"|"ct_panst"|"ct_socks"|"ct_shoes_inner"|"ct_shoes_outer"|"ct_top_parts_A"|"ct_top_parts_B"|"ct_top_parts_C")) else{continue};
            let field=|i:usize|value.as_array().and_then(|v|v.get(i)).or_else(||value.as_map().and_then(|v|v.iter().find(|(k,_)|k.as_i64()==Some(i as i64)).map(|(_,v)|v)));
            let png=field(0).and_then(Value::as_slice).unwrap_or_default();
            let clear=field(1).and_then(Value::as_bool).unwrap_or(false);
            if clear || !png.is_empty(){out.insert((coordinate,name.into()),(png.to_vec(),clear));}
        }
    }
}

/// KSOX plugin data (`KoikatsuSkinOverlayData` in KoikatsuCard.cs):
/// plugin value = [version, data map]; `Lookup` = msgpack
/// `{coordinate: {overlayType: textureId}}`, `_TextureID_<id>` = PNG bytes.
fn parse_ksox(
    plugin: &Value,
    out: &mut std::collections::HashMap<(i32, i32), Vec<u8>>,
) {
    let data = if let Some(arr) = plugin.as_array() {
        arr.get(1)
    } else {
        map_get(plugin, "1").or(Some(plugin))
    };
    let Some(data) = data else {
        return;
    };
    // KoiSkinOverlayController.OnReload / ReadLegacyData: versions 0/1
    // store PNGs directly under TexType names and copy them to every outfit.
    // Coordinate 0 is the shared fallback used by ksox_overlay_png.
    let version = plugin.as_array().and_then(|a| a.first())
        .or_else(|| map_get(plugin, "0")).and_then(Value::as_i64).unwrap_or(0);
    if version <= 1 {
        for (name, kinds) in [
            ("BodyOver", &[1][..]), ("FaceOver", &[2][..]),
            ("BodyUnder", &[3][..]), ("FaceUnder", &[4][..]),
            ("EyeUnder", &[7,9][..]), ("EyeOver", &[8,10][..]),
            ("EyeUnderL", &[7][..]), ("EyeOverL", &[8][..]),
            ("EyeUnderR", &[9][..]), ("EyeOverR", &[10][..]),
            ("EyebrowUnder", &[20][..]), ("EyelineUnder", &[30][..]),
        ] {
            if let Some(bytes) = map_get(data, name).and_then(Value::as_slice).filter(|b| !b.is_empty()) {
                for &kind in kinds { out.insert((0, kind), bytes.to_vec()); }
            }
        }
        return;
    }
    let mut textures: std::collections::HashMap<i32, Vec<u8>> =
        std::collections::HashMap::new();
    if let Some(m) = data.as_map() {
        for (k, v) in m {
            if let Some(name) = k.as_str() {
                if let Some(id) = name.strip_prefix("_TextureID_") {
                    if let (Ok(id), Some(bytes)) = (id.parse::<i32>(), v.as_slice()) {
                        textures.insert(id, bytes.to_vec());
                    }
                }
            }
        }
    }
    let Some(lookup_bytes) = map_get(data, "Lookup").and_then(|x| x.as_slice()) else {
        return;
    };
    let Some(lookup) = read_msgpack(lookup_bytes) else {
        return;
    };
    let Some(lm) = lookup.as_map() else {
        return;
    };
    for (coord, tmap) in lm {
        let Some(coord) = coord.as_i64() else {
            continue;
        };
        let Some(tmap) = tmap.as_map() else {
            continue;
        };
        for (t, tid) in tmap {
            let (Some(t), Some(tid)) = (t.as_i64(), tid.as_i64()) else {
                continue;
            };
            if let Some(png) = textures.get(&(tid as i32)) {
                out.insert((coord as i32, t as i32), png.clone());
            }
        }
    }
}

pub(crate) fn read_msgpack(bytes: &[u8]) -> Option<Value> {
    if let Ok(v) = rmpv::decode::read_value(&mut Cursor::new(bytes)) {
        if v.is_map() || v.is_array() {
            return Some(v);
        }
    }
    if bytes.len() > 4 {
        if let Ok(v) = rmpv::decode::read_value(&mut Cursor::new(&bytes[4..])) {
            if v.is_map() || v.is_array() {
                return Some(v);
            }
        }
    }
    None
}

#[allow(dead_code)]
fn clothes_part_color(p: &Value) -> [f32; 4] {
    clothes_part_colors4(p)[0]
}

fn clothes_part_colors4(p: &Value) -> [[f32; 4]; 4] {
    let mut out = [[1.0f32, 1.0, 1.0, 1.0]; 4];
    if let Some(arr) = map_get(p, "colorInfo").and_then(|v| v.as_array()) {
        for (i, c) in arr.iter().take(4).enumerate() {
            if let Some(base) = map_get(c, "baseColor")
                .or_else(|| map_get(c, "color"))
                .or_else(|| map_get(c, "mainColor"))
            {
                out[i] = parse_color(base);
            } else {
                out[i] = parse_color(c);
            }
        }
        return out;
    }
    out[0] = clothes_part_color_fallback(p);
    out
}

fn clothes_part_patterns4(p: &Value) -> [ClothesColorPattern; 4] {
    let mut out: [ClothesColorPattern; 4] = std::array::from_fn(|_| ClothesColorPattern::default());
    if let Some(arr) = map_get(p, "colorInfo").and_then(|v| v.as_array()) {
        for (i, c) in arr.iter().take(4).enumerate() {
            out[i].pattern = map_get(c, "pattern")
                .map(as_i32)
                .unwrap_or(0);
            if let Some(pc) = map_get(c, "patternColor") {
                out[i].pattern_color = parse_color(pc);
            }
            if let Some(t) = map_get(c, "tiling").and_then(|v| v.as_array()) {
                out[i].tiling = [
                    t.first().and_then(|x| x.as_f64()).unwrap_or(0.0) as f32,
                    t.get(1).and_then(|x| x.as_f64()).unwrap_or(0.0) as f32,
                ];
            }
            if let Some(t) = map_get(c, "offset").and_then(|v| v.as_array()) {
                out[i].offset = [t.first().and_then(|x|x.as_f64()).unwrap_or(0.5) as f32,
                    t.get(1).and_then(|x|x.as_f64()).unwrap_or(0.5) as f32];
            }
            out[i].rotate = map_get(c,"rotate").and_then(|v|v.as_f64()).unwrap_or(0.5) as f32;
        }
    }
    out
}

fn clothes_part_color_fallback(p: &Value) -> [f32; 4] {
    for key in ["color", "baseColor", "clothesColor", "startColor"] {
        let Some(c) = map_get(p, key) else {
            continue;
        };
        if let Some(arr) = c.as_array() {
            if let Some(first) = arr.first() {
                if let Some(base) = map_get(first, "baseColor")
                    .or_else(|| map_get(first, "color"))
                    .or_else(|| map_get(first, "mainColor"))
                {
                    return parse_color(base);
                }
                return parse_color(first);
            }
        }
        if let Some(base) = map_get(c, "baseColor")
            .or_else(|| map_get(c, "color1"))
            .or_else(|| map_get(c, "mainColor"))
        {
            return parse_color(base);
        }
        return parse_color(c);
    }
    [1.0, 1.0, 1.0, 1.0]
}

/// ChaFileCoordinate.SaveBytes layout:
/// [i32 clothesLen][clothes msgpack][i32 accessoryLen][accessory msgpack]
/// [bool enableMakeup][i32 makeupLen][makeup msgpack].
fn parse_coordinate_bytes(bin: &[u8]) -> Option<(Value, Value, Value, bool)> {
    let mut pos = 0usize;
    let rd_i32 = |b: &[u8], p: &mut usize| -> Option<i32> {
        let v = i32::from_le_bytes(b.get(*p..*p + 4)?.try_into().ok()?);
        *p += 4;
        Some(v)
    };
    let clothes_len = rd_i32(bin, &mut pos)?.max(0) as usize;
    let clothes = read_msgpack(bin.get(pos..pos + clothes_len)?)?;
    pos += clothes_len;
    let acc_len = rd_i32(bin, &mut pos)?.max(0) as usize;
    let accessory = read_msgpack(bin.get(pos..pos + acc_len)?)?;
    pos += acc_len;
    let enable_makeup = *bin.get(pos)? != 0;
    pos += 1;
    let mk_len = rd_i32(bin, &mut pos)?.max(0) as usize;
    let makeup = read_msgpack(bin.get(pos..pos + mk_len)?)?;
    Some((clothes, accessory, makeup, enable_makeup))
}

fn apply_accessory(acc: &Value, info: &mut CardInfo) {
    let Some(parts) = map_get(acc, "parts").and_then(|v| v.as_array()) else {
        return;
    };
    info.accessory = parts
        .iter()
        .map(|p| {
            let mut a = AccPart {
                type_id: map_get(p, "type").map(as_i32).unwrap_or(0),
                id: map_get(p, "id").map(as_i32).unwrap_or(0),
                no_shake: map_get(p, "noShake").and_then(Value::as_bool).unwrap_or(false),
                parent_key: map_get(p, "parentKey").map(as_str).unwrap_or_default(),
                color: [1.0, 1.0, 1.0, 1.0],
                ..Default::default()
            };
            a.add_scale = [1.0, 1.0, 1.0];
            a.add_move = [AccMove::default(); 2];
            a.color2 = [1.0, 1.0, 1.0, 1.0];
            a.color3 = [1.0, 1.0, 1.0, 1.0];
            a.color4 = [1.0, 1.0, 1.0, 1.0];
            if let Some(arr) = map_get(p, "color").and_then(|v| v.as_array()) {
                if let Some(c) = arr.first() {
                    a.color = parse_color(c);
                }
                if let Some(c) = arr.get(1) {
                    a.color2 = parse_color(c);
                }
                if let Some(c) = arr.get(2) {
                    a.color3 = parse_color(c);
                }
                if let Some(c) = arr.get(3) {
                    a.color4 = parse_color(c);
                }
            }
            // ChaFileAccessory.PartsInfo.addMove is Vector3[2,3]
            // (N_move / N_move2 × pos/rot/scale). Both rows are kept;
            // UpdateAccessoryMoveFromInfo writes j to N_move(j=0)/N_move2.
            if let Some(arr) = map_get(p, "addMove").and_then(|v| v.as_array()) {
                a.add_move = read_add_moves(arr);
                a.add_pos = a.add_move[0].pos;
                a.add_rot = a.add_move[0].rot;
                a.add_scale = a.add_move[0].scale;
            }
            a
        })
        .collect();
    if std::env::var("OK_DIAG").is_ok() {
        for (i, p) in parts.iter().enumerate() {
            let ty = map_get(p, "type").map(as_i32).unwrap_or(0);
            let id = map_get(p, "id").map(as_i32).unwrap_or(0);
            if ty == 120 && id == 0 {
                continue;
            }
            let am = map_get(p, "addMove")
                .map(|v| {
                    let s = format!("{v:?}");
                    s.chars().take(1400).collect::<String>()
                })
                .unwrap_or_default();
            eprintln!(
                "ok-extract: acc slot {i} type={ty} id={id} parent={:?} hide={:?} partsOfHead={:?} noShake={:?} addMove={am}",
                map_get(p, "parentKey"),
                map_get(p, "hideCategory").map(as_i32),
                map_get(p, "partsOfHead"),
                map_get(p, "noShake"),
            );
        }
    }
}

fn apply_makeup(mk: &Value, info: &mut CardInfo) {
    info.lip_id = map_get(mk, "lipId").map(as_i32).unwrap_or(info.lip_id);
    if let Some(c) = map_get(mk, "lipColor") {
        info.lip_color = parse_color(c);
    }
    info.cheek_id = map_get(mk, "cheekId").map(as_i32).unwrap_or(info.cheek_id);
    if let Some(c) = map_get(mk, "cheekColor") {
        info.cheek_color = parse_color(c);
    }
    info.eyeshadow_id = map_get(mk, "eyeshadowId").map(as_i32).unwrap_or(info.eyeshadow_id);
    if let Some(c) = map_get(mk, "eyeshadowColor") {
        info.eyeshadow_color = parse_color(c);
    }
    if let Some(a) = map_get(mk, "paintId").and_then(|v| v.as_array()) {
        for (i, x) in a.iter().take(2).enumerate() {
            info.paint_ids[i] = as_i32(x);
        }
    }
    if let Some(a) = map_get(mk, "paintColor").and_then(|v| v.as_array()) {
        for (i, x) in a.iter().take(2).enumerate() {
            info.paint_colors[i] = parse_color(x);
        }
    }
    if let Some(a) = map_get(mk, "paintLayout").and_then(|v| v.as_array()) {
        for (i, x) in a.iter().take(2).enumerate() {
            info.paint_layouts[i] = vec4(x);
        }
    }
}

fn apply_coordinate_slot(
    clothes: &Value,
    accessory: &Value,
    makeup: &Value,
    enable_makeup: bool,
    info: &mut CardInfo,
) {
    if enable_makeup {
        apply_makeup(makeup, info);
    }
    if let Some(parts) = map_get(clothes, "parts").and_then(|v| v.as_array()) {
        info.clothes_ids = parts
            .iter()
            .map(|p| map_get(p, "id").map(as_i32).unwrap_or(0))
            .collect();
        info.clothes_emblem_ids = parts.iter().map(|p| [
            map_get(p, "emblemeId").map(as_i32).unwrap_or(0),
            map_get(p, "emblemeId2").map(as_i32).unwrap_or(0),
        ]).collect();
        info.clothes_colors = parts.iter().map(clothes_part_colors4).collect();
        info.clothes_patterns = parts.iter().map(clothes_part_patterns4).collect();
        if let Some(first) = info.clothes_colors.first() {
            info.clothes_color = first[0];
        }
    }
    if let Some(sub) = map_get(clothes, "subPartsId").and_then(|v| v.as_array()) {
        info.sub_parts_id = sub.iter().map(as_i32).collect();
    }
    apply_accessory(accessory, info);
}

fn unwrap_bin_map(v: &Value) -> Option<Value> {
    if v.is_map() {
        return Some(v.clone());
    }
    if let Some(bin) = v.as_slice() {
        return read_msgpack(bin);
    }
    None
}

pub(crate) fn lst_blocks(header: &Value) -> Vec<(String, i32, i32)> {
    let mut out = Vec::new();
    let Some(lst) = map_get(header, "lstInfo").and_then(|v| v.as_array()) else {
        return out;
    };
    for item in lst {
        let name = map_get(item, "name").map(as_str).unwrap_or_default();
        let pos = map_get(item, "pos").map(as_i32).unwrap_or(0);
        let size = map_get(item, "size").map(as_i32).unwrap_or(0);
        if !name.is_empty() {
            out.push((name, pos, size));
        }
    }
    out
}

fn apply_face(face: &Value, info: &mut CardInfo) {
    if let Some(value) = map_get(face, "doubleTooth").and_then(Value::as_bool) {
        info.double_tooth = value;
    }
    info.head_id = map_get(face, "headId").map(as_i32).unwrap_or(info.head_id);
    info.detail_id = map_get(face, "detailId").map(as_i32).unwrap_or(info.detail_id);
    info.face_detail_power = map_get(face, "detailPower").map(as_f32).unwrap_or(info.face_detail_power);
    info.cheek_gloss_power = map_get(face, "cheekGlossPower").map(as_f32).unwrap_or(info.cheek_gloss_power);
    info.lip_gloss_power = map_get(face, "lipGlossPower").map(as_f32).unwrap_or(info.lip_gloss_power);
    if let Some(shape) = map_get(face, "shapeValueFace") {
        if std::env::var("OK_DIAG").is_ok() {
            if let Some(a) = shape.as_array() {
                let kinds: Vec<String> = a
                    .iter()
                    .map(|v| match v {
                        Value::Integer(_) => "i".to_string(),
                        Value::F32(_) => "f32".to_string(),
                        Value::F64(_) => "f64".to_string(),
                        Value::Nil => "nil".to_string(),
                        other => format!("{}", other),
                    })
                    .collect();
                eprintln!("ok-extract: FACESHAPE n={} kinds={:?}", a.len(), kinds);
            }
        }
        info.shape_face = float_array(shape);
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!("ok-extract: FACESHAPE_RAW {:?}", info.shape_face);
        }
        // Vanilla ChaFileControl.CheckDataRangeFace clamps to [0,1], but the
        // live game has SliderUnlocker which NOPs that check and extrapolates
        // AnimationKeyInfo.GetInfo outside [0,1]. Keep the raw card values.
    }
    info.white_id = map_get(face, "whiteId").map(as_i32).unwrap_or(info.white_id);
    if let Some(v) = map_get(face, "pupilWidth") {
        info.pupil_width = as_f32(v);
    }
    if let Some(v) = map_get(face, "pupilHeight") {
        info.pupil_height = as_f32(v);
    }
    if let Some(v) = map_get(face, "pupilX") {
        info.pupil_x = as_f32(v);
    }
    if let Some(v) = map_get(face, "pupilY") {
        info.pupil_y = as_f32(v);
    }
    info.eyebrow_id = map_get(face, "eyebrowId").map(as_i32).unwrap_or(info.eyebrow_id);
    info.eyeline_up_id = map_get(face, "eyelineUpId")
        .map(as_i32)
        .unwrap_or(info.eyeline_up_id);
    info.eyeline_down_id = map_get(face, "eyelineDownId")
        .map(as_i32)
        .unwrap_or(info.eyeline_down_id);
    info.nose_id = map_get(face, "noseId").map(as_i32).unwrap_or(info.nose_id);
    info.lip_line_id = map_get(face, "lipLineId")
        .map(as_i32)
        .unwrap_or(info.lip_line_id);
    info.mole_id = map_get(face, "moleId").map(as_i32).unwrap_or(info.mole_id);
    if let Some(c) = map_get(face, "moleColor") {
        info.mole_color = parse_color(c);
    }
    if let Some(v) = map_get(face, "moleLayout") {
        info.mole_layout = vec4(v);
    }
    info.hl_up_id = map_get(face, "hlUpId").map(as_i32).unwrap_or(info.hl_up_id);
    info.hl_down_id = map_get(face, "hlDownId").map(as_i32).unwrap_or(info.hl_down_id);
    info.hl_up_x = map_get(face, "hlUpX").map(as_f32).unwrap_or(info.hl_up_x);
    info.hl_down_x = map_get(face, "hlDownX").map(as_f32).unwrap_or(info.hl_down_x);
    if let Some(c) = map_get(face, "hlUpColor") {
        info.hl_up_color = parse_color(c);
        info.eye_hi_color = info.hl_up_color;
    }
    if let Some(c) = map_get(face, "hlDownColor") {
        info.hl_down_color = parse_color(c);
    }
    if let Some(v) = map_get(face, "hlUpY") {
        info.hl_up_y = as_f32(v);
    }
    if let Some(v) = map_get(face, "hlDownY") {
        info.hl_down_y = as_f32(v);
    }
    if let Some(c) = map_get(face, "whiteBaseColor") {
        info.white_base_color = parse_color(c);
    }
    if let Some(c) = map_get(face, "whiteSubColor") {
        info.white_sub_color = parse_color(c);
    }
    if let Some(c) = map_get(face, "lipLineColor") {
        info.lip_line_color = parse_color(c);
    }
    if let Some(h) = map_get(face, "hlUpColor").or_else(|| map_get(face, "hlDownColor")) {
        info.eye_hi_color = parse_color(h);
    }
    if let Some(pupil) = map_get(face, "pupil").and_then(|v| v.as_array()) {
        for (i, p) in pupil.iter().take(2).enumerate() {
            info.pupil_ids[i] = map_get(p, "id").map(as_i32).unwrap_or(info.pupil_ids[i]);
            info.pupils[i].id = info.pupil_ids[i];
            if let Some(col) = map_get(p, "baseColor") {
                info.pupils[i].base_color = parse_color(col);
            }
            if let Some(col) = map_get(p, "subColor") {
                info.pupils[i].sub_color = parse_color(col);
            }
            info.pupils[i].grad_mask_id =
                map_get(p, "gradMaskId").map(as_i32).unwrap_or(info.pupils[i].grad_mask_id);
            info.pupils[i].grad_blend =
                map_get(p, "gradBlend").map(as_f32).unwrap_or(info.pupils[i].grad_blend);
            info.pupils[i].grad_offset_y =
                map_get(p, "gradOffsetY").map(as_f32).unwrap_or(info.pupils[i].grad_offset_y);
            info.pupils[i].grad_scale =
                map_get(p, "gradScale").map(as_f32).unwrap_or(info.pupils[i].grad_scale);
        }
        if let Some(first) = pupil.first() {
            if let Some(col) = map_get(first, "baseColor")
                .or_else(|| map_get(first, "color"))
                .or_else(|| map_get(first, "mainColor"))
            {
                let c = parse_color(col);
                if c[0] + c[1] + c[2] > 0.05 {
                    info.eye_color = c;
                }
            }
            if let Some(col) = map_get(first, "subColor") {
                info.eye_sub_color = parse_color(col);
            }
        }
    }
    if let Some(col) = map_get(face, "eyebrowColor") {
        info.eyebrow_color = parse_color(col);
    }
    if let Some(col) = map_get(face, "eyelineColor") {
        info.eyeline_color = parse_color(col);
    }
    if let Some(mk) = map_get(face, "baseMakeup") {
        info.cheek_id = map_get(mk, "cheekId").map(as_i32).unwrap_or(info.cheek_id);
        if let Some(c) = map_get(mk, "cheekColor") {
            info.cheek_color = parse_color(c);
        }
        if let Some(a) = map_get(mk, "paintId").and_then(|v| v.as_array()) {
            for (i, x) in a.iter().take(2).enumerate() {
                info.paint_ids[i] = as_i32(x);
            }
        }
        if let Some(a) = map_get(mk, "paintColor").and_then(|v| v.as_array()) {
            for (i, x) in a.iter().take(2).enumerate() {
                info.paint_colors[i] = parse_color(x);
            }
        }
        if let Some(a) = map_get(mk, "paintLayout").and_then(|v| v.as_array()) {
            for (i, x) in a.iter().take(2).enumerate() {
                info.paint_layouts[i] = vec4(x);
            }
        }
        info.eyeshadow_id = map_get(mk, "eyeshadowId").map(as_i32).unwrap_or(info.eyeshadow_id);
        if let Some(c) = map_get(mk, "eyeshadowColor") {
            info.eyeshadow_color = parse_color(c);
        }
        info.lip_id = map_get(mk, "lipId").map(as_i32).unwrap_or(info.lip_id);
        if let Some(c) = map_get(mk, "lipColor") {
            info.lip_color = parse_color(c);
        }
    }
}

fn apply_body(body: &Value, info: &mut CardInfo) {
    info.bust_softness = map_get(body, "bustSoftness").map(as_f32).unwrap_or(info.bust_softness);
    info.bust_weight = map_get(body, "bustWeight").map(as_f32).unwrap_or(info.bust_weight);
    info.body_detail_id = map_get(body, "detailId").map(as_i32).unwrap_or(info.body_detail_id);
    info.body_detail_power = map_get(body, "detailPower").map(as_f32).unwrap_or(info.body_detail_power);
    info.skin_gloss_power = map_get(body, "skinGlossPower").map(as_f32).unwrap_or(info.skin_gloss_power);
    info.nail_gloss_power = map_get(body, "nailGlossPower").map(as_f32).unwrap_or(info.nail_gloss_power);
    info.body_draw_add_line = map_get(body, "drawAddLine").and_then(Value::as_bool).unwrap_or(info.body_draw_add_line);
    if let Some(shape) = map_get(body, "shapeValueBody") {
        info.shape_body = float_array(shape);
    }
    info.type_bone = map_get(body, "typeBone").map(as_i32).unwrap_or(info.type_bone);
    for key in ["skinMainColor", "skinColor", "mainColor", "color"] {
        if let Some(c) = map_get(body, key) {
            let col = parse_color(c);
            if col[0] + col[1] + col[2] > 0.15 {
                info.skin_color = col;
                break;
            }
        }
    }
    if let Some(c) = map_get(body, "skinSubColor") {
        let col = parse_color(c);
        if col[0] + col[1] + col[2] > 0.05 {
            info.skin_sub_color = col;
        }
    }
    if let Some(c) = map_get(body, "nailColor") {
        info.nail_color = parse_color(c);
    }
    if let Some(n) = map_get(body, "nipId") {
        info.nip_id = as_i32(n);
    }
    if let Some(c) = map_get(body, "nipColor") {
        info.nip_color = parse_color(c);
    }
    if let Some(v) = map_get(body, "nipGlossPower") {
        info.nip_gloss_power = as_f32(v);
    }
    if let Some(n) = map_get(body, "areolaSize") {
        info.areola_size = as_f32(n);
    }
    if let Some(n) = map_get(body, "underhairId") {
        info.underhair_id = as_i32(n);
    }
    if let Some(c) = map_get(body, "underhairColor") {
        info.underhair_color = parse_color(c);
    }
}

fn parse_color(v: &Value) -> [f32; 4] {
    let mut c = [1.0f32, 1.0, 1.0, 1.0];
    if let Some(arr) = v.as_array() {
        for (i, x) in arr.iter().take(4).enumerate() {
            c[i] = as_f32(x);
        }
        return c;
    }
    if let Some(r) = map_get(v, "r").or_else(|| map_get(v, "0")) {
        c[0] = as_f32(r);
        c[1] = map_get(v, "g").or_else(|| map_get(v, "1")).map(as_f32).unwrap_or(c[1]);
        c[2] = map_get(v, "b").or_else(|| map_get(v, "2")).map(as_f32).unwrap_or(c[2]);
        c[3] = map_get(v, "a").or_else(|| map_get(v, "3")).map(as_f32).unwrap_or(1.0);
    }
    c
}

fn pick_hair_color(part: &Value) -> Option<[f32; 4]> {
    // ChaControl.ChangeSettingHairColor: _Color = baseColor.
    for key in ["baseColor", "startColor", "endColor", "color"] {
        if let Some(col) = map_get(part, key) {
            let c = parse_color(col);
            if c[0] + c[1] + c[2] > 0.04 {
                return Some(c);
            }
        }
    }
    if let Some(info) = map_get(part, "colorInfo")
        .or_else(|| map_get(part, "acsColorParts"))
        .and_then(|v| v.as_array())
        .and_then(|a| a.first())
    {
        if let Some(col) = map_get(info, "baseColor").or_else(|| map_get(info, "color")) {
            return Some(parse_color(col));
        }
        return Some(parse_color(info));
    }
    None
}

fn apply_hair(hair: &Value, info: &mut CardInfo) {
    info.hair_gloss_id = map_get(hair, "glossId").map(as_i32).unwrap_or(0);
    if std::env::var("OK_DIAG").is_ok() {
        if let Some(m) = hair.as_map() {
            let keys: Vec<String> = m
                .iter()
                .map(|(k, v)| {
                    let ks = k.as_str().unwrap_or("?");
                    let vs = match v {
                        Value::Map(_) => "{map}".to_string(),
                        Value::Array(a) => format!("[array {}]", a.len()),
                        other => format!("{other:?}"),
                    };
                    format!("{ks}={vs}")
                })
                .collect();
            eprintln!("ok-extract: hair block keys: {}", keys.join(", "));
        }
        if let Some(parts) = map_get(hair, "parts").and_then(|v| v.as_array()) {
            for (i, p) in parts.iter().enumerate() {
                if let Some(m) = p.as_map() {
                    let keys: Vec<String> = m
                        .iter()
                        .map(|(k, _)| k.as_str().unwrap_or("?").to_string())
                        .collect();
                    eprintln!("ok-extract: hair part {i} keys: {}", keys.join(", "));
                    if let Some(ac) = map_get(p, "acsColor") {
                        let cols: Vec<String> = ac
                            .as_array()
                            .map(|a| a.iter().map(|c| format!("{:?}", parse_color(c))).collect())
                            .unwrap_or_default();
                        eprintln!("ok-extract: hair part {i} acsColor={}", cols.join(" "));
                    }
                    if let Some(ci) = map_get(p, "colorInfo") {
                        eprintln!("ok-extract: hair part {i} colorInfo={ci:?}");
                    }
                }
            }
        }
    }
    if let Some(parts) = map_get(hair, "parts").and_then(|v| v.as_array()) {
        let mut best = info.hair_color;
        let mut best_chroma = -1.0f32;
        for (i, part) in parts.iter().take(4).enumerate() {
            info.hair_ids[i] = map_get(part, "id").map(as_i32).unwrap_or(0);
            info.hair_parts[i].id = info.hair_ids[i];
            info.hair_parts[i].no_shake = map_get(part, "noShake").and_then(Value::as_bool).unwrap_or(false);
            info.hair_parts[i].length = map_get(part, "length").map(as_f32).unwrap_or(0.0);
            if let Some(v) = map_get(part, "pos") {
                info.hair_parts[i].pos = vec3(v);
            }
            if let Some(v) = map_get(part, "rot") {
                info.hair_parts[i].rot = vec3(v);
            }
            if let Some(v) = map_get(part, "scl") {
                info.hair_parts[i].scl = vec3(v);
            }
            if let Some(c) = map_get(part, "baseColor") {
                info.hair_parts[i].color = parse_color(c);
            }
            if let Some(c) = map_get(part, "startColor") {
                info.hair_parts[i].start_color = parse_color(c);
            }
            if let Some(c) = map_get(part, "endColor") {
                info.hair_parts[i].end_color = parse_color(c);
            }
            if let Some(c) = map_get(part, "outlineColor") {
                info.hair_parts[i].outline_color = parse_color(c);
            }
            if let Some(c) = map_get(part, "glossColor") {
                info.hair_parts[i].gloss_color = parse_color(c);
            }
            if std::env::var("OK_DIAG").is_ok() {
                eprintln!(
                    "ok-extract: hair part {i} id={} base={:?} start={:?} end={:?}",
                    info.hair_parts[i].id,
                    info.hair_parts[i].color,
                    info.hair_parts[i].start_color,
                    info.hair_parts[i].end_color
                );
                for k in ["outlineColor", "glossColor", "acsColor"] {
                    if let Some(c) = map_get(part, k) {
                        eprintln!("ok-extract: hair part {i} {k}={:?}", parse_color(c));
                    }
                }
            }
            if let Some(col) = pick_hair_color(part) {
                if info.hair_parts[i].color[0] + info.hair_parts[i].color[1] + info.hair_parts[i].color[2]
                    < 0.04
                {
                    info.hair_parts[i].color = col;
                }
                if info.hair_parts[i].start_color[0]
                    + info.hair_parts[i].start_color[1]
                    + info.hair_parts[i].start_color[2]
                    < 0.04
                {
                    info.hair_parts[i].start_color = col;
                }
                if info.hair_parts[i].end_color[0]
                    + info.hair_parts[i].end_color[1]
                    + info.hair_parts[i].end_color[2]
                    < 0.04
                {
                    info.hair_parts[i].end_color = col;
                }
                let mx = col[0].max(col[1]).max(col[2]);
                let mn = col[0].min(col[1]).min(col[2]);
                let chroma = mx - mn;
                if info.hair_ids[i] != 0 && chroma >= best_chroma {
                    best = col;
                    best_chroma = chroma;
                }
            }
            // ChangeSettingHairColor always writes all three; missing channels
            // copy baseColor so GetDiffuse does not lerp toward default white.
            if info.hair_parts[i].start_color[0]
                + info.hair_parts[i].start_color[1]
                + info.hair_parts[i].start_color[2]
                > 2.94
            {
                info.hair_parts[i].start_color = info.hair_parts[i].color;
            }
            if info.hair_parts[i].end_color[0]
                + info.hair_parts[i].end_color[1]
                + info.hair_parts[i].end_color[2]
                > 2.94
            {
                info.hair_parts[i].end_color = info.hair_parts[i].color;
            }
        }
        info.hair_color = best;
    }
}

fn read_len_prefixed_maps(bytes: &[u8]) -> Vec<Value> {
    let mut pos = 0usize;
    let mut out = Vec::new();
    while pos + 4 <= bytes.len() {
        let Ok(raw) = bytes[pos..pos + 4].try_into() else {
            break;
        };
        let n = u32::from_le_bytes(raw) as usize;
        pos += 4;
        if n == 0 || pos + n > bytes.len() {
            break;
        }
        if let Some(v) = read_msgpack(&bytes[pos..pos + n]) {
            out.push(v);
        }
        pos += n;
    }
    out
}

fn apply_custom(val: &Value, raw: &[u8], info: &mut CardInfo) {
    if let Some(face) = map_get(val, "face") {
        apply_face(face, info);
        if let Some(body) = map_get(val, "body") {
            apply_body(body, info);
        }
        if let Some(hair) = map_get(val, "hair") {
            apply_hair(hair, info);
        }
        return;
    }
    if map_get(val, "shapeValueFace").is_some() {
        apply_face(val, info);
    }
    let parts = read_len_prefixed_maps(raw);
    if let Some(face) = parts.get(0) {
        apply_face(face, info);
    }
    if let Some(body) = parts.get(1) {
        apply_body(body, info);
    }
    if let Some(hair) = parts.get(2) {
        apply_hair(hair, info);
    }
}

pub fn parse_card(path: &Path) -> Result<CardInfo, String> {
    parse_card_outfit(path, 0)
}

pub fn parse_card_outfit(path: &Path, outfit: i32) -> Result<CardInfo, String> {
    let data = crate::gamefs::read(path).map_err(|e| format!("read card: {e}"))?;
    parse_card_bytes(&data, outfit)
}

pub(crate) fn parse_card_bytes(data: &[u8], outfit: i32) -> Result<CardInfo, String> {
    let mut info = parse_card_bytes_mode(data, outfit, true)?;
    // An OK Studio scene's character state (only while a session is opened for it).
    crate::studio_status::apply(&mut info);
    Ok(info)
}

// Keep MaterialEditor's opaque dictionary borrowed until its individual PNGs
// acquire shared ownership. All other plugin values retain the legacy parser's
// representation, including duplicate keys and the optional four-byte prefix.
fn read_card_extended(bytes: &[u8]) -> Option<(Value,Vec<Option<&[u8]>>)> {
    use rmpv::ValueRef;
    fn get<'a,'b>(v:&'b ValueRef<'a>,key:&str)->Option<&'b ValueRef<'a>> {
        let ValueRef::Map(map)=v else{return None};
        map.iter().find(|(k,_)|matches!(k,ValueRef::String(s) if s.as_str()==Some(key))).map(|(_,v)|v)
    }
    fn own(v:&ValueRef<'_>,skip:&ValueRef<'_>)->Value {
        if std::ptr::eq(v,skip){return Value::Nil;}
        match v {
            ValueRef::Array(a)=>Value::Array(a.iter().map(|v|own(v,skip)).collect()),
            ValueRef::Map(m)=>Value::Map(m.iter().map(|(k,v)|(own(k,skip),own(v,skip))).collect()),
            _=>v.to_owned(),
        }
    }
    let mut decoded=None;
    for input in [Some(bytes),bytes.get(4..)].into_iter().flatten(){
        if let Ok(value)=rmpv::decode::read_value_ref(&mut Cursor::new(input)){
            if matches!(value,ValueRef::Map(_)|ValueRef::Array(_)){decoded=Some(value);break;}
        }
    }
    let value=decoded?;
    let ValueRef::Map(map)=&value else{return Some((value.to_owned(),Vec::new()));};
    let mut dictionaries=Vec::with_capacity(map.len());let mut owned=Vec::with_capacity(map.len());
    for (key,plugin) in map {
        let material=matches!(key,ValueRef::String(s) if s.as_str().is_some_and(|s|s.to_ascii_lowercase().contains("materialeditor")));
        let dictionary=if material {
            let data=match plugin {ValueRef::Array(a)=>a.get(1),_=>get(plugin,"1").or(Some(plugin))};
            data.and_then(|data|get(get(data,"data").unwrap_or(data),"TextureDictionary"))
        }else{None};
        // Non-binary legacy encodings retain the original owned path.
        let bytes=dictionary.and_then(|v|match v{ValueRef::Binary(b)=>Some(*b),_=>None});
        owned.push((key.to_owned(),if bytes.is_some(){own(plugin,dictionary.unwrap())}else{plugin.to_owned()}));
        dictionaries.push(bytes);
    }
    Some((Value::Map(owned),dictionaries))
}

pub(crate) fn parse_card_bytes_mode(data: &[u8], outfit: i32, apply_shape_plugins: bool) -> Result<CardInfo, String> {
    parse_card_bytes_mode_cached(data,outfit,apply_shape_plugins,None)
}
fn card_diagnostics_enabled()->bool {
    std::env::var_os("OK_DIAG").is_some() || std::env::var_os("OK_APPEARANCE_PROFILE").is_some()
}
pub(crate) fn parse_card_bytes_cached(data:&[u8],outfit:i32,cache:&mut PngDictionaryCache)->Result<CardInfo,String>{
    parse_card_bytes_mode_cached(data,outfit,true,Some(cache))
}
pub(crate) fn parse_card_bytes_mode_cached(data:&[u8],outfit:i32,apply_shape_plugins:bool,
    mut cache:Option<&mut PngDictionaryCache>)->Result<CardInfo,String>{
    let iend = find_iend_end(&data).ok_or("PNG IEND not found")?;
    if iend >= data.len() {
        return Err("no payload after IEND".into());
    }
    let extra = &data[iend..];
    let mut cur = Cursor::new(extra);
    let _product = read_i32_le(&mut cur)?;
    let marker = read_dotnet_string(&mut cur)?;
    let _version = read_dotnet_string(&mut cur)?;
    let face_len = read_i32_le(&mut cur)?;
    if face_len < 0 {
        return Err("bad face len".into());
    }
    {
        use std::io::{Seek, SeekFrom};
        cur.seek(SeekFrom::Current(face_len as i64))
            .map_err(|_| "seek face")?;
    }
    let header_len = read_i32_le(&mut cur)? as usize;
    let pos = cur.position() as usize;
    if pos + header_len > extra.len() {
        return Err("bad header len".into());
    }
    let header_bytes = &extra[pos..pos + header_len];
    cur.set_position((pos + header_len) as u64);
    let header = rmpv::decode::read_value(&mut Cursor::new(header_bytes))
        .map_err(|e| format!("header msgpack: {e}"))?;
    let _total = read_i64_le(&mut cur)?;
    let base = cur.position() as usize;
    let blocks = lst_blocks(&header);

    let mut info = CardInfo {
        marker,
        lastname: String::new(),
        firstname: String::new(),
        nickname: String::new(),
        head_id: 0,
        hair_ids: [0; 4],
        hair_gloss_id: 0,
        hair_parts: Default::default(),
        shape_face: Vec::new(),
        shape_body: Vec::new(),
        bust_softness: 0.5,
        collider_overrides: crate::dbone::game_colliders::CardOverrides::default(),
        dynamic_bone_edits: Vec::new(),
        bust_weight: 0.5,
        clothes_ids: Vec::new(),
        clothes_emblem_ids: Vec::new(),
        clothes_colors: Vec::new(),
        clothes_patterns: Vec::new(),
        sub_parts_id: Vec::new(),
        hair_color: [1.0, 1.0, 1.0, 1.0],
        skin_color: [0.78, 0.56, 0.46, 1.0],
        clothes_color: [1.0, 1.0, 1.0, 1.0],
        eye_color: [0.32, 0.55, 0.88, 1.0],
        eye_sub_color: [0.32, 0.55, 0.88, 1.0],
        eyebrow_color: [0.12, 0.08, 0.10, 1.0],
        eyeline_color: [0.08, 0.06, 0.08, 1.0],
        accessory: Vec::new(),
        resolves: Vec::new(),
        nip_id: 0,
        nip_color: [1.0, 1.0, 1.0, 1.0],
        nip_gloss_power: 0.5,
        areola_size: 0.5,
        underhair_id: 0,
        underhair_color: [0.0, 0.0, 0.0, 1.0],
        skin_sub_color: [0.78, 0.56, 0.46, 1.0],
        nail_color: [1.0, 1.0, 1.0, 1.0],
        // Viewer targets female cards (Character.CreateFemale -> sex 1).
        sex: 1,
        type_bone: 0,
        nip_stand_rate: 0.0,
        eyes_open_max: 1.0,
        eyebrow_open_max: 1.0,
        eyes_blink: true,
        hoho_aka_rate: 0.0,
        white_id: 0,
        eyebrow_id: 0,
        eyeline_up_id: 0,
        eyeline_down_id: 0,
        nose_id: 0,
        pupil_ids: [0, 0],
        pupil_width: 0.9,
        pupil_height: 0.9,
        pupil_x: 0.5,
        pupil_y: 0.5,
        lip_line_id: 0,
        double_tooth: false,
        mole_id: 0,
        mole_color: [1.0, 1.0, 1.0, 1.0],
        mole_layout: [0.5, 0.5, 0.0, 0.5],
        paint_layouts: [[0.5, 0.5, 0.5, 0.7], [0.5, 0.5, 0.5, 0.7]],
        pupils: Default::default(),
        hl_up_id: 0,
        hl_down_id: 0,
        hl_up_color: [1.0, 1.0, 1.0, 1.0],
        hl_down_color: [1.0, 1.0, 1.0, 1.0],
        hl_up_y: 0.5,
        hl_up_x: 0.5,
        hl_down_x: 0.5,
        hl_down_y: 0.5,
        detail_id: 0,
        body_detail_id: 0,
        // ChaFileBody constructor: detailPower=0, not the slider midpoint.
        body_detail_power: 0.0,
        skin_gloss_power: 0.0,
        skin_tuya_rate: 0.0,
        tears_lv: 0,
        status_eyebrow_ptn: -1,
        status_eyes_ptn: -1,
        status_mouth_ptn: -1,
        status_eyes_look_ptn: -1,
        status_neck_look_ptn: -1,
        status_coordinate: -1,
        status_clothes: Vec::new(),
        show_accessory: vec![true; 20],
        siru_lv: [0; 5],
        nail_gloss_power: 0.5,
        face_detail_power: 0.0,
        cheek_gloss_power: 0.0,
        lip_gloss_power: 0.0,
        body_draw_add_line: true,
        white_base_color: [1.0, 1.0, 1.0, 1.0],
        white_sub_color: [1.0, 1.0, 1.0, 1.0],
        lip_line_color: [1.0, 1.0, 1.0, 1.0],
        eye_hi_color: [1.0, 1.0, 1.0, 1.0],
        cheek_id: 0,
        cheek_color: [1.0, 1.0, 1.0, 1.0],
        paint_ids: [0, 0],
        paint_colors: [[1.0; 4], [1.0; 4]],
        eyeshadow_id: 0,
        eyeshadow_color: [1.0, 1.0, 1.0, 1.0],
        lip_id: 0,
        lip_color: [1.0, 1.0, 1.0, 1.0],
        body_guid: None,
        penis_guid: None,
        balls_guid: None,
        personality: 0,
        birth_day: 0,
        voice_pitch: 0.5,
        abm: Vec::new(),
        pregnancy:Default::default(),
        ksox: std::collections::HashMap::new(),
        clothes_masks: std::collections::HashMap::new(),
        clothes_overlays: std::collections::HashMap::new(),
        coordinate_count: 0,
        me_renderer_off: std::collections::HashSet::new(),
        me_renderer_on: std::collections::HashSet::new(),
        me_tex: MeTexMap::new(),
        me_color: MeColorMap::new(),
        me_shader: MeShaderMap::new(),
        me_copies: Vec::new(),
        me_float: MeFloatMap::new(),
        hair_acc: std::collections::HashMap::new(),
    };

    let mut more_acc_xml: Option<String> = None;
    let mut pushup_plugin: Option<Value> = None;
    if let Some(cache)=cache.as_deref_mut(){cache.parse_phases.clear();}
    for (name, bpos, bsize) in blocks {
        let start = base + bpos as usize;
        let end = start + bsize as usize;
        if end > extra.len() {
            continue;
        }
        let slice = &extra[start..end];
        let phase_begin=std::time::Instant::now();
        let parsed=if name=="KKEx"{read_card_extended(slice)}else{read_msgpack(slice).map(|v|(v,Vec::new()))};
        let Some((val,dictionaries)) = parsed else {
            continue;
        };
        let decode_ms=phase_begin.elapsed().as_secs_f64()*1000.;
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!("ok-extract: card block {name}");
        }
        match name.as_str() {
            "Parameter" => {
                info.lastname = map_get(&val, "lastname").map(as_str).unwrap_or_default();
                info.firstname = map_get(&val, "firstname").map(as_str).unwrap_or_default();
                info.nickname = map_get(&val, "nickname").map(as_str).unwrap_or_default();
                info.sex = map_get(&val, "sex").map(as_i32).unwrap_or(info.sex);
                info.personality = map_get(&val, "personality").map(as_i32).unwrap_or(info.personality);
                info.birth_day = map_get(&val, "birthDay").map(as_i32).unwrap_or(info.birth_day);
                if let Some(pitch) = map_get(&val, "voicePitch").or_else(|| map_get(&val, "voiceRate")) {
                    info.voice_pitch = as_f32(pitch);
                }
            }
            "Status" => {
                info.nip_stand_rate = map_get(&val, "nipStandRate")
                    .map(as_f32)
                    .unwrap_or(info.nip_stand_rate);
                info.eyes_open_max = map_get(&val, "eyesOpenMax")
                    .map(as_f32)
                    .unwrap_or(info.eyes_open_max);
                info.eyebrow_open_max = map_get(&val, "eyebrowOpenMax")
                    .map(as_f32)
                    .unwrap_or(info.eyebrow_open_max);
                info.eyes_blink = map_get(&val,"eyesBlink").and_then(Value::as_bool).unwrap_or(info.eyes_blink);
                info.hoho_aka_rate = map_get(&val, "hohoAkaRate")
                    .map(as_f32)
                    .unwrap_or(info.hoho_aka_rate);
                info.skin_tuya_rate = map_get(&val, "skinTuyaRate")
                    .map(as_f32).unwrap_or(info.skin_tuya_rate);
                info.tears_lv = map_get(&val, "tearsLv").map(as_i32).unwrap_or(info.tears_lv);
                info.status_coordinate = map_get(&val, "coordinateType").map(as_i32).unwrap_or(info.status_coordinate);
                info.status_eyebrow_ptn = map_get(&val, "eyebrowPtn").map(as_i32).unwrap_or(info.status_eyebrow_ptn);
                info.status_eyes_ptn = map_get(&val, "eyesPtn").map(as_i32).unwrap_or(info.status_eyes_ptn);
                info.status_mouth_ptn = map_get(&val, "mouthPtn").map(as_i32).unwrap_or(info.status_mouth_ptn);
                info.status_eyes_look_ptn = map_get(&val, "eyesLookPtn").map(as_i32).unwrap_or(info.status_eyes_look_ptn);
                info.status_neck_look_ptn = map_get(&val, "neckLookPtn").map(as_i32).unwrap_or(info.status_neck_look_ptn);
                match map_get(&val, "clothesState") {
                    Some(Value::Binary(b)) => info.status_clothes = b.clone(),
                    Some(Value::Array(a)) => info.status_clothes = a.iter().map(|v| as_i32(v).clamp(0, 255) as u8).collect(),
                    _ => {}
                }
                if let Some(Value::Array(shown)) = map_get(&val, "showAccessory") {
                    info.show_accessory = shown.iter().map(|v| v.as_bool().unwrap_or(true)).collect();
                }
                if let Some(Value::Array(siru)) = map_get(&val, "siruLv") {
                    for (i, v) in siru.iter().take(5).enumerate() { info.siru_lv[i] = as_i32(v); }
                }
            }
            "Custom" => {
                apply_custom(&val, slice, &mut info);
            }
            "Coordinate" => {
                let slot_bin = if let Some(arr) = val.as_array() {
                    info.coordinate_count = arr.len();
                    let idx = (outfit as usize).min(arr.len().saturating_sub(1));
                    arr.get(idx)
                        .and_then(|v| v.as_slice().map(|s| s.to_vec()))
                        .or_else(|| arr.first().and_then(|v| v.as_slice().map(|s| s.to_vec())))
                } else {
                    val.as_slice().map(|s| s.to_vec())
                };
                if let Some(bin) = slot_bin {
                    if let Some((clothes, accessory, makeup, enable_makeup)) =
                        parse_coordinate_bytes(&bin)
                    {
                        apply_coordinate_slot(
                            &clothes,
                            &accessory,
                            &makeup,
                            enable_makeup,
                            &mut info,
                        );
                    }
                }
            }
            "KKEx" => {
                if let Some(m) = val.as_map() {
                    for (plugin_index,(k, kv)) in m.iter().enumerate() {
                        if k.as_str().map(|s| s.eq_ignore_ascii_case("com.deathweasel.bepinex.pushup")).unwrap_or(false) {
                            pushup_plugin = Some(kv.clone());
                        }
                        if k.as_str()
                            .map(|s| s.to_lowercase().contains("uncensorselector"))
                            .unwrap_or(false)
                        {
                            if let Some(inner) = kv.as_array().and_then(|a| a.get(1)) {
                                if let Some(g) =
                                    map_get(inner, "BodyGUID").and_then(|x| x.as_str())
                                {
                                    info.body_guid = Some(g.to_string());
                                }
                                if let Some(g) = map_get(inner, "PenisGUID").and_then(|x| x.as_str()) {
                                    let g = g.trim();
                                    if !g.is_empty() { info.penis_guid = Some(g.to_string()); }
                                }
                                if let Some(g) = map_get(inner, "BallsGUID").and_then(|x| x.as_str()) {
                                    let g = g.trim();
                                    if !g.is_empty() { info.balls_guid = Some(g.to_string()); }
                                }
                            }
                        }
                        if k.as_str()
                            .map(|s| s.contains("ABMData"))
                            .unwrap_or(false)
                        {
                            if let Some(inner) = kv.as_array().and_then(|a| a.get(1)) {
                                if let Some(bytes) =
                                    map_get(inner, "boneData").and_then(|x| x.as_slice())
                                {
                                    info.abm = crate::abmx::decode(bytes);
                                }
                            }
                        }
                        if k.as_str()
                            .map(|s| s.eq_ignore_ascii_case("KSOX"))
                            .unwrap_or(false)
                        {
                            parse_ksox(kv, &mut info.ksox);
                        }
                        if k.as_str().is_some_and(|k|k.eq_ignore_ascii_case("KCOX")) {
                            parse_clothes_masks(kv, &mut info.clothes_masks);
                            parse_clothes_overlays(kv, &mut info.clothes_overlays);
                        }
                        if k.as_str()==Some("KK_PregnancyPlus") {
                            if let Some(data)=kv.as_array().and_then(|a|a.get(1)).and_then(Value::as_map){
                                for(key,value)in data{if let Some(key)=key.as_str(){
                                    if key=="GameplayEnabled"{info.pregnancy.enabled=value.as_bool().unwrap_or(true);}
                                    if key.starts_with("inflation"){if let Some(n)=value.as_f64(){info.pregnancy.values.insert(key.to_string(),n as f32);}}
                                }}
                            }
                        }
                        if k.as_str() == Some("com.deathweasel.bepinex.dynamicboneeditor") {
                            if let Some(data) = me_plugin_data(kv) {
                                info.dynamic_bone_edits = parse_dynamic_bone_editor(data);
                            }
                        }
                        if k.as_str() == Some("com.deathweasel.bepinex.colliders") {
                            if let Some(data) = me_plugin_data(kv) {
                                info.collider_overrides = crate::dbone::game_colliders::CardOverrides {
                                    breast: map_get(data, "BreastCollidersEnabled").and_then(Value::as_bool),
                                    floor: map_get(data, "FloorColliderEnabled").and_then(Value::as_bool),
                                    skirt: map_get(data, "SkirtCollidersEnabled").and_then(Value::as_bool),
                                };
                            }
                        }
                        if k.as_str()
                            .map(|s| s.to_ascii_lowercase().contains("materialeditor"))
                            .unwrap_or(false)
                        {
                            parse_material_editor(kv, &mut info.me_renderer_off);
                            parse_material_editor_visibility(kv,&mut info.me_renderer_on,"1");
                            parse_material_editor_textures_cached(kv,dictionaries.get(plugin_index).copied().flatten(),&mut info.me_tex,cache.as_deref_mut());
                            parse_material_editor_colors(kv, &mut info.me_color);
                            parse_material_editor_shaders(kv, &mut info.me_shader);
                            parse_material_editor_copies(kv, &mut info.me_copies);
                            parse_material_editor_floats(kv, &mut info.me_float);
                            parse_material_editor_texture_transforms(kv, &mut info.me_float);
                        }
                        if k.as_str()
                            .map(|s| s.to_ascii_lowercase().contains("hairaccessorycustomizer"))
                            .unwrap_or(false)
                        {
                            parse_hair_accessories(kv, outfit, &mut info.hair_acc);
                        }
                        if k.as_str()
                            .map(|s| s.to_ascii_lowercase().contains("moreaccessories"))
                            .unwrap_or(false)
                        {
                            if let Some(inner) = me_plugin_data(kv) {
                                if let Some(xml) = map_get(inner, "additionalAccessories")
                                    .and_then(|x| x.as_str())
                                {
                                    more_acc_xml = Some(xml.to_string());
                                }
                            }
                        }
                    }
                }
                parse_kkex(&val, &mut info.resolves);
            }
            _ => {}
        }
        let applied_ms=phase_begin.elapsed().as_secs_f64()*1000.;
        drop(val);drop(dictionaries);
        if let Some(cache)=cache.as_deref_mut(){cache.parse_phases.push((name,[decode_ms,applied_ms-decode_ms,phase_begin.elapsed().as_secs_f64()*1000.-applied_ms]));}
    }
    if let Some(xml) = more_acc_xml {
        append_more_accessories(&xml, outfit, &mut info);
    }
    if apply_shape_plugins {
        if let Some(plugin) = pushup_plugin.as_ref() {
            apply_pushup_shape(plugin, outfit, &info.clothes_ids, &mut info.shape_body);
        }
    }
    if !info.hair_acc.is_empty() && card_diagnostics_enabled() {
        eprintln!("ok-extract: hair accessories {}", info.hair_acc.len());
    }
    Ok(info)
}

#[cfg(test)]
mod tests {
    #[test]
    fn borrowed_material_texture_dictionary_keeps_bindings_and_duplicate_ids() {
        let first=vec![0;1024*1024];let mut png=b"\x89PNG".to_vec();png.extend(vec![37;1024*1024]);
        let dict=Value::Map(vec![(1.into(),Value::Binary(first)),(1.into(),Value::Binary(png.clone())),
            (2.into(),Value::Binary(b"not png".to_vec())),("ignored".into(),Value::Binary(png.clone()))]);
        let row=|id:i64,material:&str,property:&str|Value::Map(vec![("TexID".into(),id.into()),
            ("MaterialName".into(),material.into()),("Property".into(),property.into()),
            ("ObjectType".into(),4.into()),("CoordinateIndex".into(),2.into()),("Slot".into(),3.into())]);
        let list=Value::Array(vec![row(1,"cf_m_a (Instance)","_MainTex"),row(1,"cf_m_b","_OverTex2"),
            row(2,"cf_m_bad","_MainTex"),row(9,"cf_m_missing","_MainTex")]);
        let pack=|value:&Value|{let mut bytes=Vec::new();rmpv::encode::write_value(&mut bytes,value).unwrap();bytes};
        let plugin=Value::Array(vec![1.into(),Value::Map(vec![("TextureDictionary".into(),Value::Binary(pack(&dict))),
            ("MaterialTexturePropertyList".into(),Value::Binary(pack(&list)))])]);
        let mut actual=MeTexMap::new();parse_material_editor_textures(&plugin,&mut actual);
        assert_eq!(actual.len(),2);
        let a=&actual[&(4,2,3,me_mat_key("cf_m_a (Instance)"),"_maintex".into())];
        let b=&actual[&(4,2,3,me_mat_key("cf_m_b"),"_overtex2".into())];
        assert_eq!(a.as_slice(),png.as_slice());assert_eq!(b.as_slice(),png.as_slice());
        assert!(std::sync::Arc::ptr_eq(a,b),"bindings to the same dictionary id must share one owned PNG");
        let inner=me_plugin_data(&plugin).unwrap().clone();
        let variants=[plugin.clone(),inner.clone(),Value::Map(vec![("1".into(),inner.clone())]),
            Value::Array(vec![1.into(),Value::Map(vec![("data".into(),inner)])])];
        fn restore(v:&mut Value,bytes:&[u8])->bool {
            match v {
                Value::Map(m)=>{
                    for (k,v) in m.iter_mut(){if k.as_str()==Some("TextureDictionary")&&v.is_nil(){*v=Value::Binary(bytes.to_vec());return true;}}
                    m.iter_mut().any(|(_,v)|restore(v,bytes))
                },
                Value::Array(a)=>a.iter_mut().any(|v|restore(v,bytes)),_=>false,
            }
        }
        for variant in variants {
            let root=Value::Map(vec![("unknown".into(),Value::Binary(vec![19;1024])),
                ("MaterialEditor".into(),variant.clone()),("MaterialEditor".into(),variant.clone())]);
            for prefix in [false,true] {
                let mut bytes=if prefix{vec![255;4]}else{Vec::new()};bytes.extend(pack(&root));
                let (mut thin,dictionaries)=read_card_extended(&bytes).unwrap();
                let Value::Map(m)=&mut thin else{panic!("map expected")};
                for i in 1..=2 {
                    let dictionary=dictionaries[i].unwrap();
                    assert!(dictionary.as_ptr()>=bytes.as_ptr()&&dictionary.as_ptr()<unsafe{bytes.as_ptr().add(bytes.len())},"dictionary must borrow original card bytes");
                    let mut optimized=MeTexMap::new();parse_material_editor_textures_borrowed(&m[i].1,Some(dictionary),&mut optimized);
                    assert_eq!(optimized,actual);
                    assert!(restore(&mut m[i].1,dictionary));
                }
                assert_eq!(thin,root,"all other fields and duplicate plugin keys must remain exact");
            }
        }
    }
    #[test]
    fn cached_material_texture_dictionary_owns_assets_and_revalidates_bytes(){
        let pack=|value:&Value|{let mut bytes=Vec::new();rmpv::encode::write_value(&mut bytes,value).unwrap();bytes};
        let png=|byte:u8|{let mut bytes=b"\x89PNG".to_vec();bytes.extend([byte;32]);bytes};
        let dictionary=|byte:u8|pack(&Value::Map(vec![(1.into(),Value::Binary(png(9))),
            (1.into(),Value::Binary(png(byte))),(1.into(),Value::Binary(b"invalid".to_vec())),
            (2.into(),Value::String(String::from("\u{89}PNG legacy").into()))]));
        let row=|material:&str|Value::Map(vec![("TexID".into(),1.into()),("MaterialName".into(),material.into()),("Property".into(),"_MainTex".into())]);
        let plugin=|bytes:Vec<u8>,rows:Vec<Value>|Value::Array(vec![1.into(),Value::Map(vec![
            ("TextureDictionary".into(),Value::Binary(bytes)),("MaterialTexturePropertyList".into(),Value::Binary(pack(&Value::Array(rows))))])]);
        let mut cache=PngDictionaryCache::default();
        let first=plugin(dictionary(37),vec![row("a"),row("b")]);
        let mut expected=MeTexMap::new();parse_material_editor_textures(&first,&mut expected);
        let mut a=MeTexMap::new();parse_material_editor_textures_cached(&first,None,&mut a,Some(&mut cache));
        assert_eq!(a,expected);assert_eq!(cache.stats(),[0,1]);
        let first_owner=a.values().next().unwrap().clone();
        assert_eq!(first_owner.as_slice(),png(37));
        let repeated=plugin(dictionary(37),vec![row("a"),row("b")]);
        let mut b=MeTexMap::new();parse_material_editor_textures_cached(&repeated,None,&mut b,Some(&mut cache));
        assert_eq!(b,expected);assert_eq!(cache.stats(),[1,1]);
        assert!(b.values().all(|p|std::sync::Arc::ptr_eq(&first_owner,p)));
        // Binding edits still take effect with a warm immutable dictionary.
        let bindings=plugin(dictionary(37),vec![row("c")]);
        let mut edited=MeTexMap::new();parse_material_editor_textures_cached(&bindings,None,&mut edited,Some(&mut cache));
        assert_eq!(edited.len(),1);assert!(edited.keys().all(|(_,_,_,name,_)|name=="c"));
        assert!(std::sync::Arc::ptr_eq(&first_owner,edited.values().next().unwrap()));
        // Same byte length is insufficient identity: a changed PNG misses.
        assert_eq!(dictionary(37).len(),dictionary(38).len());
        let changed=plugin(dictionary(38),vec![row("a")]);let mut changed_map=MeTexMap::new();
        parse_material_editor_textures_cached(&changed,None,&mut changed_map,Some(&mut cache));
        assert_eq!(changed_map.values().next().unwrap().as_slice(),png(38));
        assert!(!std::sync::Arc::ptr_eq(&first_owner,changed_map.values().next().unwrap()));
        assert_eq!(cache.stats(),[2,2]);
        // Eviction is bounded and cannot invalidate already published owners.
        let mut small=PngDictionaryCache{budget:1024,limit:2,..Default::default()};
        let old=small.read(&dictionary(1)).unwrap();
        small.read(&dictionary(2)).unwrap();small.read(&dictionary(3)).unwrap();
        assert_eq!(small.entries.len(),2);assert!(small.retained_bytes<=small.budget);
        assert_eq!(old[&1].as_slice(),png(1));
        let reloaded=small.read(&dictionary(1)).unwrap();assert!(!std::sync::Arc::ptr_eq(&old[&1],&reloaded[&1]));
        let mut oversized=PngDictionaryCache{budget:1,limit:2,..Default::default()};
        let uncached=oversized.read(&dictionary(7)).unwrap();assert!(oversized.entries.is_empty());assert_eq!(oversized.retained_bytes,0);
        drop(cache);drop(first);drop(repeated);assert_eq!(first_owner.as_slice(),png(37));
        drop(oversized);assert_eq!(uncached[&1].as_slice(),png(7));
    }

    use super::*;
    use std::path::PathBuf;

    #[test]
    fn ksox_legacy_and_coordinate_storage() {
        let png = Value::Binary(vec![1,2,3]);
        let legacy = Value::Array(vec![Value::from(1), Value::Map(vec![
            (Value::from("FaceOver"),png.clone()),(Value::from("EyeOver"),png.clone()),
            (Value::from("EyeUnderR"),Value::Binary(vec![4,5])),
            (Value::from("BodyUnder"),Value::Binary(vec![])),
        ])]);
        let mut overlays=std::collections::HashMap::new();parse_ksox(&legacy,&mut overlays);
        assert_eq!(overlays.len(),4);
        for kind in [2,8,10] {assert_eq!(overlays.get(&(0,kind)),Some(&vec![1,2,3]));}
        assert_eq!(overlays.get(&(0,9)),Some(&vec![4,5]));
        let lookup=Value::Map(vec![(Value::from(3),Value::Map(vec![(Value::from(2),Value::from(17))]))]);
        let mut packed=Vec::new();rmpv::encode::write_value(&mut packed,&lookup).unwrap();
        let modern=Value::Array(vec![Value::from(2),Value::Map(vec![
            (Value::from("Lookup"),Value::Binary(packed)),(Value::from("_TextureID_17"),png),
            (Value::from("FaceOver"),Value::Binary(vec![99])),
        ])]);
        overlays.clear();parse_ksox(&modern,&mut overlays);
        assert_eq!(overlays.len(),1);assert_eq!(overlays.get(&(3,2)),Some(&vec![1,2,3]));
    }

    #[test]
    fn native_skin_constructor_defaults_for_missing_card_blocks() {
        let mut bytes=b"\x89PNG\r\n\x1a\n\0\0\0\0IEND\0\0\0\0".to_vec();
        bytes.extend_from_slice(&0i32.to_le_bytes());
        bytes.extend_from_slice(b"\x04test\x031.0");
        bytes.extend_from_slice(&0i32.to_le_bytes());
        let mut header=Vec::new();
        rmpv::encode::write_value(&mut header,&Value::Map(Vec::new())).unwrap();
        bytes.extend_from_slice(&(header.len() as i32).to_le_bytes());
        bytes.extend_from_slice(&header);
        bytes.extend_from_slice(&0i64.to_le_bytes());
        let path=std::env::temp_dir().join(format!("ok-skin326-missing-blocks-{}.png",std::process::id()));
        std::fs::write(&path,bytes).unwrap();
        let parsed=parse_card(&path);
        std::fs::remove_file(&path).unwrap();
        let info=parsed.unwrap();
        assert_eq!([info.body_detail_power,info.skin_gloss_power,info.nail_gloss_power],[0.,0.,0.5]);
        assert_eq!([info.face_detail_power,info.cheek_gloss_power,info.lip_gloss_power],[0.,0.,0.]);
        assert_eq!(info.skin_tuya_rate,0.);
    }

    #[test]
    fn native_skin_tuya_status_block_is_parsed() {
        let mut status=Vec::new();rmpv::encode::write_value(&mut status,&Value::Map(vec![
            (Value::from("skinTuyaRate"),Value::from(0.375))])).unwrap();
        let header=Value::Map(vec![(Value::from("lstInfo"),Value::Array(vec![Value::Map(vec![
            (Value::from("name"),Value::from("Status")),(Value::from("pos"),Value::from(0)),
            (Value::from("size"),Value::from(status.len() as i32))])]))]);
        let mut packed=Vec::new();rmpv::encode::write_value(&mut packed,&header).unwrap();
        let mut bytes=b"\x89PNG\r\n\x1a\n\0\0\0\0IEND\0\0\0\0".to_vec();
        bytes.extend_from_slice(&0i32.to_le_bytes());bytes.extend_from_slice(b"\x04test\x031.0");
        bytes.extend_from_slice(&0i32.to_le_bytes());bytes.extend_from_slice(&(packed.len() as i32).to_le_bytes());
        bytes.extend_from_slice(&packed);bytes.extend_from_slice(&(status.len() as i64).to_le_bytes());bytes.extend_from_slice(&status);
        let path=std::env::temp_dir().join(format!("ok-skin339-status-{}.png",std::process::id()));
        std::fs::write(&path,bytes).unwrap();let parsed=parse_card(&path);std::fs::remove_file(&path).unwrap();
        assert_eq!(parsed.unwrap().skin_tuya_rate,0.375);
    }

    #[test]
    #[ignore="installed card fixture for partial typed skin updates"]
    fn skin_draw_numeric_partial_updates_preserve_loaded_fields() {
        let mut info=parse_card(&crate::test_support::game_path("/UserData/chara/female/KK_380982.png")).unwrap();
        apply_body(&Value::Map(vec![(Value::from("skinGlossPower"),Value::from(0.25)),
            (Value::from("nailGlossPower"),Value::from(0.75)),
            (Value::from("detailPower"),Value::from(0.375))]),&mut info);
        apply_face(&Value::Map(vec![(Value::from("detailPower"),Value::from(0.125)),
            (Value::from("cheekGlossPower"),Value::from(0.5)),
            (Value::from("lipGlossPower"),Value::from(0.625))]),&mut info);
        apply_face(&Value::Map(Vec::new()),&mut info);
        apply_body(&Value::Map(Vec::new()),&mut info);
        assert_eq!([info.skin_gloss_power,info.nail_gloss_power,info.body_detail_power], [0.25,0.75,0.375]);
        assert_eq!([info.face_detail_power,info.cheek_gloss_power,info.lip_gloss_power], [0.125,0.5,0.625]);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn saved_double_tooth_flag_and_partial_face_updates() {
        let root = crate::test_support::game_path("/UserData/chara/female");
        for name in ["[Card] March_7th_keiyuu.png", "Alice_keiyuu.png",
            "Hayase_yuuka_keiyuu_V1_02 For KKS.png", "KK_091207.png", "KK_384283.png"] {
            let mut info = parse_card(&root.join(name)).expect("five-card reference fixture");
            eprintln!("DOUBLE_TOOTH_CARD name={name} visible={}", info.double_tooth);
            for value in [false, true, false] {
                apply_face(&Value::Map(vec![(Value::from("doubleTooth"), Value::from(value))]), &mut info);
                assert_eq!(info.double_tooth, value);
                apply_face(&Value::Map(Vec::new()), &mut info);
                assert_eq!(info.double_tooth, value, "partial update preserves loaded flag");
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn saved_hair_and_accessory_no_shake_flags() {
        let part = |id: i32, no_shake: bool| Value::Map(vec![
            (Value::from("id"), Value::from(id)),
            (Value::from("noShake"), Value::from(no_shake)),
        ]);
        let path = crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if !path.ok_exists() { return; }
        let mut info = parse_card(&path).expect("reference card");
        assert!(!info.accessory[10].no_shake);
        assert!(info.accessory[11].no_shake);
        apply_hair(&Value::Map(vec![(Value::from("parts"),
            Value::Array(vec![part(1, true), part(2, false)]))]), &mut info);
        assert!(info.hair_parts[0].no_shake);
        assert!(!info.hair_parts[1].no_shake);
        apply_accessory(&Value::Map(vec![(Value::from("parts"),
            Value::Array(vec![part(3, true), part(4, false)]))]), &mut info);
        assert!(info.accessory[0].no_shake);
        assert!(!info.accessory[1].no_shake);
    }

    #[test]
    #[ignore]
    fn diag_kk384283_material_editor() {
        use std::io::Seek;
        let path = crate::test_support::game_path(r"\UserData\chara\female\KK_384283.png");
        if !path.ok_exists() {
            return;
        }
        let data = crate::gamefs::read(&path).unwrap();
        let iend = find_iend_end(&data).unwrap();
        let extra = &data[iend..];
        let mut cur = Cursor::new(extra);
        let _ = read_i32_le(&mut cur).unwrap();
        let _ = read_dotnet_string(&mut cur).unwrap();
        let _ = read_dotnet_string(&mut cur).unwrap();
        let face_len = read_i32_le(&mut cur).unwrap();
        {
            use std::io::{Seek, SeekFrom};
            cur.seek(SeekFrom::Current(face_len as i64)).unwrap();
        }
        let header_len = read_i32_le(&mut cur).unwrap() as usize;
        let pos = cur.position() as usize;
        let header = rmpv::decode::read_value(&mut Cursor::new(&extra[pos..pos + header_len])).unwrap();
        cur.set_position((pos + header_len) as u64);
        let _ = read_i64_le(&mut cur).unwrap();
        let base = cur.position() as usize;
        for (name, bpos, bsize) in lst_blocks(&header) {
            if name != "KKEx" {
                continue;
            }
            let slice = &extra[base + bpos as usize..base + bpos as usize + bsize as usize];
            let Some(v) = read_msgpack(slice) else {
                eprintln!("KKEx decode none");
                return;
            };
            let Some(map) = v.as_map() else {
                eprintln!("KKEx not map");
                return;
            };
            for (k, kv) in map {
                let key = k.as_str().unwrap_or("?");
                if !key.to_ascii_lowercase().contains("material") {
                    continue;
                }
                eprintln!("plugin {key}");
                let data = kv.as_array().and_then(|a| a.get(1)).unwrap_or(kv);
                let Some(inner) = map_get(data, "data").or(Some(data)) else {
                    continue;
                };
                let Some(dmap) = inner.as_map() else {
                    eprintln!("  data not map {}", inner.to_string().chars().take(120).collect::<String>());
                    continue;
                };
                for (dk, dv) in dmap {
                    let dkey = dk.as_str().unwrap_or("?");
                    let nbytes = dv.as_slice().map(|b| b.len()).unwrap_or(0);
                    eprintln!("  field {dkey} bytes={nbytes}");
                    if dkey != "RendererPropertyList" {
                        continue;
                    }
                    let Some(bin) = dv.as_slice() else { continue };
                    let Ok(list) = rmpv::decode::read_value(&mut Cursor::new(bin)) else {
                        eprintln!("  RendererPropertyList decode fail");
                        continue;
                    };
                    let Some(arr) = list.as_array() else {
                        eprintln!("  RendererPropertyList not array {}", list.to_string().chars().take(200).collect::<String>());
                        continue;
                    };
                    eprintln!("  renderers {}", arr.len());
                    for item in arr {
                        let ot = map_get(item, "ObjectType").and_then(|x| x.as_i64()).unwrap_or(-1);
                        let coord = map_get(item, "CoordinateIndex").and_then(|x| x.as_i64()).unwrap_or(-1);
                        let slot = map_get(item, "Slot").and_then(|x| x.as_i64()).unwrap_or(-1);
                        let name = map_get(item, "RendererName").and_then(|x| x.as_str()).unwrap_or("?");
                        let prop = map_get(item, "Property").and_then(|x| x.as_i64()).unwrap_or(-1);
                        let val = map_get(item, "Value").and_then(|x| x.as_str()).unwrap_or("?");
                        if prop == 0 {
                            eprintln!("    ot={ot} coord={coord} slot={slot} prop={prop} val={val} name={name}");
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn probe_handoff2_cards() {
        let cards = [
            r"UserData\chara\female\[Card] March_7th_keiyuu.png",
            r"UserData\chara\female\Alice_keiyuu.png",
            r"UserData\chara\female\Hayase_yuuka_keiyuu_V1_02 For KKS.png",
        ];
        for path in cards {
            let p = crate::test_support::game_path(path);
            if !p.ok_exists() {
                eprintln!("missing {path}");
                continue;
            }
            let card = parse_card_outfit(&p, 0).expect("parse");
            let hair = &card.hair_parts[0];
            eprintln!(
                "CARD {} first='{}' last='{}' hair_ids={:?} base={:?} start={:?} end={:?} acc={} hair_acc={} me_color={} me_tex={}",
                p.file_name().and_then(|s| s.to_str()).unwrap_or("?"),
                card.firstname,
                card.lastname,
                card.hair_ids,
                hair.color,
                hair.start_color,
                hair.end_color,
                card.accessory.len(),
                card.hair_acc.len(),
                card.me_color.len(),
                card.me_tex.len()
            );
            let mut saved = 0;
            for ((ot, coord, slot, mat, prop), png) in &card.me_tex {
                if *ot == 2 && *coord == 0 && prop == "maintex" && *slot < 6 {
                    eprintln!("  metex slot={slot} mat={mat} bytes={}", png.len());
                    if saved < 2 {
                        let path = format!(
                            r"C:\Users\Damody\AppData\Local\Temp\opencode\me_{}_{}_{}.png",
                            card.firstname.replace(' ', "_"),
                            slot,
                            saved
                        );
                        let _ = std::fs::write(&path, png.as_slice());
                        eprintln!("    wrote {path}");
                        saved += 1;
                    }
                }
            }
            let mut me_n = 0;
            for ((ot, coord, slot, mat, prop), col) in &card.me_color {
                if *ot == 2 && *coord == 0 && prop.contains("color") && *slot < 8 {
                    if me_n < 12 {
                        eprintln!("  me ot={ot} slot={slot} mat={mat} prop={prop} col={col:?}");
                    }
                    me_n += 1;
                }
            }
            eprintln!("  me acc color rows shown/total-ish {me_n}");
            for (i, a) in card.accessory.iter().enumerate() {
                if a.id == 0 {
                    continue;
                }
                let hac = card.hair_acc.get(&(0, i as i32));
                eprintln!(
                    "  slot {i} type={} id={} parent={} move0={:?} move1={:?} c0={:?} hac={:?}",
                    a.type_id,
                    a.id,
                    a.parent_key,
                    a.add_move[0],
                    a.add_move[1],
                    a.color,
                    hac.map(|h| (h.color_match, h.hair_length, h.accessory_color, h.outline_color))
                );
            }
        }
    }

    #[test]
    fn material_editor_instance_name_preserves_internal_separator() {
        assert_eq!(
            me_mat_key("Hair_F310-HairF (Instance)_(-722896)d (Instance) (Instance)"),
            "hair_f310-hairf _(-722896)d"
        );
        assert_eq!(me_mat_key("Hair (Instance) (Instance)"), "hair");
        assert_eq!(me_mat_key("Hair Instance"), "hair");
        let root = crate::test_support::game_path(r"\UserData\chara\female");
        let path = root.join("KK_384914.png");
        if path.ok_is_file() {
            let card = parse_card_outfit(&path, 0).expect("card");
            let key = (2, 0, 5, me_mat_key("Hair_F310-HairF (Instance)_(-722896)d (Instance) (Instance)"), "color".to_string());
            let color = card.me_color.get(&key).expect("partial-hair MaterialEditor color");
            assert!((color[0] - 0.847).abs() < 1e-4, "{color:?}");
            assert!((color[1] - 0.815).abs() < 1e-4, "{color:?}");
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn pushup_advanced_top_uses_saved_outfit_shape() {
        let path = &crate::test_support::game_path(r"\UserData\chara\female\Hayase_yuuka_keiyuu_V1_02 For KKS.png");
        if !path.ok_is_file() { return; }
        let card = parse_card_outfit(path, 0).unwrap();
        // KKS Pushup's outfit-0 advanced top overrides the raw card's
        // 0.62/0.40/0.8748727/0.4770243.  These values also reproduce the
        // game's cf_a_bust* intermediate transforms in shape_body.json.
        for (index, want) in [(4, 0.58), (5, 0.48), (9, 0.6), (10, 0.45434362)] {
            assert!((card.shape_body[index] - want).abs() < 1e-5, "shape[{index}]");
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_march7_plugins() {
        let path = PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\[Card] March_7th_keiyuu.png"),
        );
        if !path.ok_exists() {
            return;
        }
        let data = crate::gamefs::read(&path).unwrap();
        let iend = find_iend_end(&data).unwrap();
        let extra = &data[iend..];
        let mut cur = Cursor::new(extra);
        let _ = read_i32_le(&mut cur).unwrap();
        let _ = read_dotnet_string(&mut cur).unwrap();
        let _ = read_dotnet_string(&mut cur).unwrap();
        let face_len = read_i32_le(&mut cur).unwrap();
        {
            use std::io::{Seek, SeekFrom};
            cur.seek(SeekFrom::Current(face_len as i64)).unwrap();
        }
        let header_len = read_i32_le(&mut cur).unwrap() as usize;
        let pos = cur.position() as usize;
        let header =
            rmpv::decode::read_value(&mut Cursor::new(&extra[pos..pos + header_len])).unwrap();
        cur.set_position((pos + header_len) as u64);
        let _ = read_i64_le(&mut cur).unwrap();
        let base = cur.position() as usize;
        for (name, bpos, bsize) in lst_blocks(&header) {
            if name != "KKEx" {
                continue;
            }
            let slice = &extra[base + bpos as usize..base + bpos as usize + bsize as usize];
            let Some(v) = read_msgpack(slice) else {
                eprintln!("KKEx decode none");
                return;
            };
            let Some(map) = v.as_map() else {
                eprintln!("KKEx not map");
                return;
            };
            for (k, kv) in map {
                let key = k.as_str().unwrap_or("?");
                eprintln!("plugin {key}");
                let low = key.to_ascii_lowercase();
                if !low.contains("material") && !low.contains("overlay") && !low.contains("kcox") {
                    continue;
                }
                let data = kv.as_array().and_then(|a| a.get(1)).unwrap_or(kv);
                let Some(inner) = map_get(data, "data").or(Some(data)) else {
                    continue;
                };
                let Some(dmap) = inner.as_map() else {
                    eprintln!(
                        "  data not map {}",
                        inner.to_string().chars().take(160).collect::<String>()
                    );
                    continue;
                };
                for (dk, dv) in dmap {
                    let dkey = dk.as_str().unwrap_or("?");
                    let nbytes = dv.as_slice().map(|b| b.len()).unwrap_or(0);
                    eprintln!("  field {dkey} bytes={nbytes}");
                    let Some(bin) = dv.as_slice() else { continue };
                    if !dkey.contains("List") && dkey != "TextureDictionary" {
                        continue;
                    }
                    let Ok(list) = rmpv::decode::read_value(&mut Cursor::new(bin)) else {
                        eprintln!("  {dkey} decode fail");
                        continue;
                    };
                    let items: Vec<&Value> = if let Some(arr) = list.as_array() {
                        arr.iter().collect()
                    } else if let Some(m) = list.as_map() {
                        eprintln!("  {dkey} map keys={}", m.len());
                        let want = [7i64, 10, 12, 13, 14, 17, 30, 36, 39, 40];
                        for (mk, mv) in m.iter() {
                            let id = mk.as_i64().unwrap_or(-1);
                            let Some(bytes) = mv.as_slice() else { continue };
                            if !want.contains(&id) {
                                continue;
                            }
                            let mag = bytes.get(..8).map(|b| format!("{b:02X?}")).unwrap_or_default();
                            let path = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\me_tex_{id}.bin");
                            let _ = std::fs::write(&path, bytes);
                            eprintln!("    tex {id} bytes={} mag={mag} -> {path}", bytes.len());
                        }
                        continue;
                    } else {
                        eprintln!(
                            "  {dkey} not list {}",
                            list.to_string().chars().take(120).collect::<String>()
                        );
                        continue;
                    };
                    eprintln!("  {dkey} n={}", items.len());
                    for item in items.iter() {
                        let mat = map_get(item, "MaterialName")
                            .and_then(|x| x.as_str())
                            .unwrap_or("");
                        let prop = map_get(item, "Property")
                            .map(|x| x.to_string())
                            .unwrap_or_default();
                        let slot = map_get(item, "Slot").and_then(|x| x.as_i64()).unwrap_or(-1);
                        let ot = map_get(item, "ObjectType")
                            .and_then(|x| x.as_i64())
                            .unwrap_or(-1);
                        let val = map_get(item, "Value")
                            .map(|x| x.to_string().chars().take(80).collect::<String>())
                            .unwrap_or_default();
                        let tex = map_get(item, "TexID")
                            .map(|x| x.to_string())
                            .unwrap_or_default();
                        let rname = map_get(item, "RendererName")
                            .and_then(|x| x.as_str())
                            .unwrap_or("");
                        if mat.is_empty() && rname.is_empty() && val.is_empty() {
                            continue;
                        }
                        let interesting = ot == 4
                            || mat.to_ascii_lowercase().contains("hitomi")
                            || mat.to_ascii_lowercase().contains("eyeline")
                            || mat.to_ascii_lowercase().contains("sirome")
                            || (dkey.contains("Texture") && mat.to_ascii_lowercase().contains("hair"));
                        if !interesting {
                            continue;
                        }
                        let coord = map_get(item, "CoordinateIndex")
                            .and_then(|x| x.as_i64())
                            .unwrap_or(-1);
                        eprintln!(
                            "    ot={ot} coord={coord} slot={slot} mat={mat} rend={rname} prop={prop} val={val} tex={tex}"
                        );
                    }
                }
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn real_card_custom_and_coordinate() {
        let path = crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if !path.ok_exists() {
            return;
        }
        let card = parse_card(&path).expect("parse");
        assert!(card.shape_face.len() >= 52, "shape_face={}", card.shape_face.len());
        assert!(card.shape_body.len() >= 32, "shape_body={}", card.shape_body.len());
        assert!(card.hair_ids.iter().any(|id| *id != 0), "hair all zero: {:?}", card.hair_ids);
        assert!(!card.clothes_ids.is_empty(), "clothes empty hair={:?} shapeB={}", card.hair_ids, card.shape_body.len());
        assert!(card.resolves.len() > 10);
        eprintln!(
            "ok-extract: nip_id={} areola={} underhair={}",
            card.nip_id, card.areola_size, card.underhair_id
        );
        assert!(card.hair_ids.iter().any(|id| *id != 0), "hair all zero: {:?}", card.hair_ids);
        assert!(
            card.sub_parts_id.iter().any(|id| *id != 0),
            "sub_parts_id empty or zero: {:?}",
            card.sub_parts_id
        );
        assert!(
            card.resolves.iter().any(|r| r.category_no == 210 || r.category_no == 200),
            "no sailor/jacket UAR"
        );
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_accessory_and_body_color() {
        let path = crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if !path.ok_exists() {
            return;
        }
        let data = crate::gamefs::read(&path).unwrap();
        let iend = find_iend_end(&data).unwrap();
        let extra = &data[iend..];
        let mut cur = Cursor::new(extra);
        let _ = read_i32_le(&mut cur).unwrap();
        let _ = read_dotnet_string(&mut cur).unwrap();
        let _ = read_dotnet_string(&mut cur).unwrap();
        let face_len = read_i32_le(&mut cur).unwrap();
        {
            use std::io::{Seek, SeekFrom};
            cur.seek(SeekFrom::Current(face_len as i64)).unwrap();
        }
        let header_len = read_i32_le(&mut cur).unwrap() as usize;
        let pos = cur.position() as usize;
        let header = rmpv::decode::read_value(&mut Cursor::new(&extra[pos..pos + header_len])).unwrap();
        let _ = read_i64_le(&mut cur).unwrap();
        let base = cur.position() as usize;
        let mut lines = Vec::new();
        for (name, bpos, bsize) in lst_blocks(&header) {
            let slice = &extra[base + bpos as usize..base + bpos as usize + bsize as usize];
            if name == "Coordinate" {
                let Some(v) = read_msgpack(slice) else {
                    lines.push("coord decode none".into());
                    continue;
                };
                let Some(arr) = v.as_array() else {
                    lines.push(format!("coord not array {}", v.to_string().chars().take(80).collect::<String>()));
                    continue;
                };
                let Some(slot) = unwrap_bin_map(&arr[0]) else {
                    lines.push("coord slot none".into());
                    continue;
                };
                if let Some(parts) = map_get(&slot, "accessory").and_then(|c| map_get(c, "parts")).and_then(|x| x.as_array()) {
                    lines.push(format!("acc_n={}", parts.len()));
                    for (i, p) in parts.iter().take(8).enumerate() {
                        let keys = p.as_map().map(|m| {
                            m.iter().map(|(k, val)| format!("{}:{}", k.as_str().unwrap_or("?"), if val.is_map() {"map"} else if val.is_array() {"arr"} else {"v"})).collect::<Vec<_>>().join(",")
                        }).unwrap_or_default();
                        lines.push(format!("  acc{i} keys={keys}"));
                        lines.push(format!("  acc{i} preview={}", p.to_string().chars().take(350).collect::<String>()));
                    }
                }
                if let Some(parts) = map_get(&slot, "parts").and_then(|x| x.as_array()) {
                    if let Some(p0) = parts.first() {
                        lines.push(format!("clothes0={}", p0.to_string().chars().take(400).collect::<String>()));
                    }
                }
            }
            if name == "Custom" {
                let maps = read_len_prefixed_maps(slice);
                if let Some(body) = maps.get(1) {
                    let keys = body.as_map().map(|m| {
                        m.iter().map(|(k, _)| k.as_str().unwrap_or("?").to_string()).collect::<Vec<_>>().join(",")
                    }).unwrap_or_default();
                    lines.push(format!("body_keys={keys}"));
                    for k in ["skinMainColor", "skinColor", "mainColor", "color", "sunburnColor", "paintColor"] {
                        if let Some(v) = map_get(body, k) {
                            lines.push(format!("  body.{k}={}", v.to_string().chars().take(200).collect::<String>()));
                        }
                    }
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\acc_body.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
        assert!(!lines.is_empty());
    }
}

#[cfg(test)]
mod clothes_mask_tests {
    use super::*;
    #[test]
    fn kcox_albedo_keeps_clear_without_png_and_coordinate_scope() {
        let entries=Value::Map(vec![(Value::from(3),Value::Map(vec![
            (Value::from("ct_top_parts_A"),Value::Array(vec![Value::Binary(vec![7,8]),Value::Boolean(false)])),
            (Value::from("ct_gloves"),Value::Map(vec![(Value::from(0),Value::Nil),(Value::from(1),Value::Boolean(true))])),
            (Value::from("ct_shorts"),Value::Array(vec![Value::Nil,Value::Boolean(false)])),
            (Value::from("BodyMask"),Value::Array(vec![Value::Binary(vec![9]),Value::Boolean(true)])),
        ]))]);
        let mut bin=Vec::new();rmpv::encode::write_value(&mut bin,&entries).unwrap();
        let plugin=Value::Array(vec![Value::from(1),Value::Map(vec![(Value::from("Overlays"),Value::Binary(bin))])]);
        let mut out=std::collections::HashMap::new();parse_clothes_overlays(&plugin,&mut out);
        assert_eq!(out.len(),2);
        assert_eq!(out[&(3,"ct_top_parts_A".into())],(vec![7,8],false));
        assert_eq!(out[&(3,"ct_gloves".into())],(vec![],true));
        assert!(!out.contains_key(&(0,"ct_top_parts_A".into())));
    }
    #[test]
    fn kcox_mask_coordinate_and_override_rules() {
        let entry=|bytes:Value, flag:bool|Value::Array(vec![bytes,Value::Boolean(flag)]);
        let masks=Value::Map(vec![(Value::from(0),Value::Map(vec![
            (Value::from("BodyMask"),entry(Value::Binary(vec![1,2]),false)),
            (Value::from("BraMask"),entry(Value::Binary(vec![3,4]),true)),
            (Value::from("InnerMask"),entry(Value::Nil,true)),
            (Value::from("ct_gloves"),entry(Value::Binary(vec![5,6]),false)),
        ])),(Value::from(3),Value::Map(vec![(Value::from("BodyMask"),entry(Value::Binary(vec![7,8]),false))]))]);
        let mut bytes=Vec::new();rmpv::encode::write_value(&mut bytes,&masks).unwrap();
        let plugin=Value::Array(vec![Value::from(1),Value::Map(vec![(Value::from("Overlays"),Value::Binary(bytes))])]);
        let mut out=std::collections::HashMap::new();parse_clothes_masks(&plugin,&mut out);
        assert_eq!(out.len(),3);
        assert_eq!(out.get(&(0,"BodyMask".into())).unwrap(),&[1,2]);
        assert_eq!(out.get(&(0,"BraMask".into())).unwrap(),&[3,4]);
        assert_eq!(out.get(&(3,"BodyMask".into())).unwrap(),&[7,8]);
        assert!(!out.contains_key(&(1,"BodyMask".into())));
        assert!(!out.contains_key(&(0,"InnerMask".into())));
    }

    #[test]
    fn material_editor_texture_transforms_without_tex_id() {
        let row=Value::Map(vec![
            (Value::from("ObjectType"),Value::from(4)),(Value::from("CoordinateIndex"),Value::from(0)),
            (Value::from("Slot"),Value::from(0)),(Value::from("MaterialName"),Value::from("Body (Instance)")),
            (Value::from("Property"),Value::from("NormalMap")),(Value::from("TexID"),Value::Nil),
            (Value::from("Scale"),Value::Array(vec![Value::F32(5.),Value::F32(-2.)])),
            (Value::from("Offset"),Value::Nil),
            (Value::from("OffsetOriginal"),Value::Array(vec![Value::F32(0.3),Value::F32(0.4)])),
        ]);
        let mut bytes=Vec::new();rmpv::encode::write_value(&mut bytes,&Value::Array(vec![row])).unwrap();
        let plugin=Value::Map(vec![(Value::from("MaterialTexturePropertyList"),Value::Binary(bytes))]);
        let mut values=MeFloatMap::new();parse_material_editor_texture_transforms(&plugin,&mut values);
        assert_eq!(values.len(),2);
        assert_eq!(values.get(&(4,0,0,"body".into(),"normalmap_st_x".into())),Some(&5.));
        assert_eq!(values.get(&(4,0,0,"body".into(),"normalmap_st_y".into())),Some(&-2.));
    }

    #[test]
    fn material_editor_texture_offsets_keep_coordinate_and_slot_identity() {
        let row=|coord,slot,offset:Value|Value::Map(vec![
            (Value::from("ObjectType"),Value::from(2)),(Value::from("CoordinateIndex"),Value::from(coord)),
            (Value::from("Slot"),Value::from(slot)),(Value::from("MaterialName"),Value::from("Mat (Instance)")),
            (Value::from("Property"),Value::from("_MainTex")),
            (Value::from("Offset"),offset),(Value::from("Scale"),Value::Nil),
            (Value::from("ScaleOriginal"),Value::Array(vec![Value::F32(9.),Value::F32(9.)])),
        ]);
        let mut bytes=Vec::new();
        rmpv::encode::write_value(&mut bytes,&Value::Array(vec![
            row(3,11,Value::Array(vec![Value::F32(-0.25),Value::F32(1.5)])),
            row(0,12,Value::Array(vec![Value::F32(f32::NAN),Value::F32(2.)])),
        ])).unwrap();
        let plugin=Value::Map(vec![(Value::from("MaterialTexturePropertyList"),Value::Binary(bytes))]);
        let mut values=MeFloatMap::new();parse_material_editor_texture_transforms(&plugin,&mut values);
        assert_eq!(values.len(),2);
        assert_eq!(values.get(&(2,3,11,"mat".into(),"maintex_st_z".into())),Some(&-0.25));
        assert_eq!(values.get(&(2,3,11,"mat".into(),"maintex_st_w".into())),Some(&1.5));
    }

    #[test]
    #[ignore="installed Siesta source/native texture transform control"]
    fn installed_material_editor_texture_transforms() {
        let info=parse_card(&crate::test_support::game_path("/UserData/chara/female/KK_380982.png")).unwrap();
        for axis in ["x","y"] {
            assert_eq!(info.me_float.get(&(4,0,0,"cf_m_body".into(),format!("normalmap_st_{axis}"))),Some(&5.));
        }
    }
}
