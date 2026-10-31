//! KK Studio scene files (`UserData/Studio/scene/*.png`, Studio.SceneInfo): the object tree and the scene map, for
//! import into OK Studio. Characters keep their embedded card data (the bytes after a card's PNG), so the importer
//! can rebuild a KKS card and convert it to an OK card.
//!
//! Layout (.NET BinaryReader): PNG thumbnail, version string, object count and (key, type, object)…, then the scene
//! settings, starting with the map number. Objects are ObjectInfo (key, ChangeAmount, tree state, visible) plus their
//! own fields; folders, routes and items carry child objects, characters carry children per accessory slot.

use serde::Serialize;

use crate::studio_pose::ChangeAmount;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Version([i32; 4]);
impl Version {
    /// .NET `Version`: missing components are -1 (and compare lower than 0).
    pub fn parse(s: &str) -> Version {
        let mut v = [-1; 4];
        for (i, p) in s.trim().split('.').take(4).enumerate() { v[i] = p.parse().unwrap_or(-1); }
        Version(v)
    }
    fn at_least(&self, s: &str) -> bool { *self >= Version::parse(s) }
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct Color { pub r: f32, pub g: f32, pub b: f32, pub a: f32 }

/// NeckLookControllerVer2.SaveNeckLookCtrl: pattern and absolute local rotations, in source aBones order.
#[derive(Clone, Debug, Serialize)]
pub struct StudioNeck {
    pub pattern: i32,
    pub rotations: Vec<[f32; 4]>,
}

fn read_neck(bytes: &[u8]) -> Result<Option<StudioNeck>, String> {
    if bytes.is_empty() {
        return Ok(None);
    }
    if bytes.len() < 8 {
        return Err("truncated Studio neck data".into());
    }
    let pattern = i32::from_le_bytes(bytes[0..4].try_into().unwrap());
    if pattern != 4 {
        return Ok(None);
    }
    let count = i32::from_le_bytes(bytes[4..8].try_into().unwrap());
    if !(0..=64).contains(&count) || bytes.len() != 8 + count as usize * 16 {
        return Err("invalid Studio neck data length".into());
    }
    let mut rotations = Vec::new();
    for chunk in bytes[8..].chunks_exact(16) {
        let mut q = [0.; 4];
        for (i, v) in q.iter_mut().enumerate() {
            *v = f32::from_le_bytes(chunk[i * 4..i * 4 + 4].try_into().unwrap());
        }
        let norm = q.iter().map(|x| x * x).sum::<f32>();
        if !norm.is_finite() || norm < 1e-12 {
            return Err("invalid Studio neck quaternion".into());
        }
        rotations.push(crate::finalik::qnorm(q));
    }
    Ok(Some(StudioNeck { pattern, rotations }))
}

/// EyeLookCalc.SaveAngle: two local quaternions, followed (since 0.0.8) by H0,H1,V0,V1.
fn read_eyes(bytes: &[u8], version: &Version) -> Result<Option<Vec<f32>>, String> {
    if bytes.is_empty() { return Ok(None); }
    let expected = if version.at_least("0.0.8") { 48 } else { 32 };
    if bytes.len() != expected { return Err("invalid Studio eyes data length".into()); }
    let mut values: Vec<f32> = bytes.chunks_exact(4)
        .map(|v| f32::from_le_bytes(v.try_into().unwrap())).collect();
    if values.iter().any(|v| !v.is_finite()) { return Err("invalid Studio eyes value".into()); }
    for q in values[..8].chunks_exact_mut(4) {
        let n = q.iter().map(|v| v*v).sum::<f32>();
        if !n.is_finite() || n < 1e-12 { return Err("invalid Studio eyes quaternion".into()); }
        for v in q { *v /= n.sqrt(); }
    }
    values.resize(12, 0.);
    Ok(Some(values))
}

#[derive(Clone, Debug, Serialize)]
#[serde(tag = "kind")]
pub enum Object {
    Character {
        key: i32, change: ChangeAmount, visible: bool, sex: i32,
        /// Card data range in the scene file (productNo .. end of the data block).
        card_offset: usize, card_len: usize,
        /// Face thumbnail PNG of the card (usable as the rebuilt card's image).
        face_png_offset: usize, face_png_len: usize,
        /// Plugin data (ExtendedSave "KKEx" msgpack) the scene stores after the card data (0 length: none).
        ext_offset: usize, ext_len: usize,
        fk: Vec<(i32, ChangeAmount)>, ik: Vec<(i32, ChangeAmount)>,
        enable_fk: bool, active_fk: [bool; 7], enable_ik: bool, active_ik: [bool; 5],
        neck: Option<StudioNeck>,
        eyes: Option<Vec<f32>>,
        anime: [i32; 3], anime_speed: f32, anime_time: f32, hand: [i32; 2], mouth_open: f32,
        /// OICharInfo animePattern and animeOptionParam1/2 (H motions: pattern, height, breast), and isAnimeForceLoop.
        anime_pattern: f32, anime_options: [f32; 2], anime_force_loop: bool,
        /// OICharInfo nipple (0..1) and siru levels (face, chest front, chest back, lower front, lower back).
        nipple: f32, siru: [u8; 5],
        /// Children attached to accessory slots (slot, objects).
        children: Vec<(i32, Vec<Object>)>,
    },
    Item {
        key: i32, change: ChangeAmount, visible: bool, group: i32, category: i32, no: i32,
        colors: Vec<Color>,
        /// The three pattern slots (OIItemInfo.pattern; their colours are colors[3..6]).
        patterns: Vec<Pattern>,
        anime_speed: f32, anime_pattern: i32, anime_time: f32, alpha: f32, enable_fk: bool,
        /// Outline (OIItemInfo.lineColor／lineWidth), emission (emissionColor／emissionPower) and lightCancel.
        line_color: Color, line_width: f32, emission_color: Color, emission_power: f32, light_cancel: f32,
        /// Item FK: bone name and its change (rotation) when `enable_fk`.
        fk: Vec<(String, ChangeAmount)>, children: Vec<Object>,
    },
    Light { key: i32, change: ChangeAmount, visible: bool, no: i32, color: Color, intensity: f32, range: f32, spot_angle: f32, shadow: bool, enable: bool },
    Folder { key: i32, change: ChangeAmount, visible: bool, name: String, children: Vec<Object> },
    /// OIRouteInfo: its children ride the route's child root, which rests on the start point (`points[0]`) and moves
    /// through the points when the route plays (OCIRoute.Play / Stop).
    Route { key: i32, change: ChangeAmount, visible: bool, name: String, children: Vec<Object>, points: Vec<RoutePoint>,
        /// Playing when saved, back to the start after the last point, the children turned along the path (0 none, 1 XY, 2 Y).
        active: bool, looped: bool, orient: i32 },
    Camera { key: i32, change: ChangeAmount, visible: bool, name: String, active: bool },
    Text { key: i32, change: ChangeAmount, visible: bool, id: i32 },
}

#[derive(Clone, Debug, Serialize)]
pub struct Scene {
    pub version: String, pub objects: Vec<Object>, pub map: i32,
    /// The Studio view the scene was saved with (SceneInfo.cameraSaveData), if the file has it.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub camera: Option<StudioCamera>,
    /// Screen settings and background (SceneInfo), if the file has them.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub effects: Option<SceneEffects>,
    /// Material Editor's scene data (its SceneController, in the scene's trailing ExtendedSave block): the material
    /// colour and value edits of Studio items, by object key.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub material_edits: Vec<MaterialEdit>,
}

/// A point of a Studio route (OIRoutePointInfo), in the route's space.
#[derive(Clone, Debug, Serialize)]
pub struct RoutePoint {
    pub change: ChangeAmount,
    /// Travel speed to the next point (units per second, iTween `speed`) and its StudioTween.EaseType.
    pub speed: f32,
    pub ease: i32,
    /// Curve (through the aid point) rather than a straight line to the next point; `link`: the curve runs on through
    /// this point from the previous one.
    pub curve: bool,
    /// The curve's aid point, in the point's space (None: not placed yet, half way to the next point).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aid: Option<ChangeAmount>,
    pub link: bool,
}

/// One Material Editor edit of a Studio object.
#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct MaterialEdit {
    /// The object's scene key (ObjectInfo.dicKey).
    pub id: i32,
    /// Material name as Material Editor keys it (without " (Instance)"); the renderer's name for "renderer".
    pub material: String,
    /// Shader property (`_Color`, `_Glossiness` ...); for "renderer" the RendererProperties name (Enabled,
    /// ShadowCastingMode, ReceiveShadows, RecalculateNormals); empty for "shader".
    pub property: String,
    /// "color" (RGBA, gamma as authored), "float" (x), "texture" (`text`: the PNG, base64), "texture_st" (the texture's
    /// scale xy and offset zw), "shader" (`text`: the shader name, x: the render queue or -1) or "renderer" (x).
    pub kind: String,
    pub value: [f32; 4],
    #[serde(skip_serializing_if = "String::is_empty")]
    pub text: String,
}

/// Standard base64 (with padding), for binary data carried in scene JSON.
pub(crate) fn base64(bytes: &[u8]) -> String {
    const A: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for c in bytes.chunks(3) {
        let n = (u32::from(c[0]) << 16) | (u32::from(*c.get(1).unwrap_or(&0)) << 8) | u32::from(*c.get(2).unwrap_or(&0));
        for k in 0..4 { out.push(if k <= c.len() { A[(n >> (18 - 6 * k)) as usize & 63] as char } else { '=' }); }
    }
    out
}

/// The scene's trailing ExtendedSave block ("KKEx", version, length, msgpack of plugin id -> PluginData), the last
/// block of the file.
fn scene_plugins(bytes: &[u8]) -> Option<rmpv::Value> {
    let marker = b"\x04KKEx";
    let at = bytes.windows(marker.len()).rposition(|w| w == marker)?;
    let p = at + marker.len();
    let len = bytes.get(p + 4..p + 8).map(|b| i32::from_le_bytes([b[0], b[1], b[2], b[3]]))?;
    let data = usize::try_from(len).ok().and_then(|l| bytes.get(p + 8..p + 8 + l))?;
    rmpv::decode::read_value(&mut std::io::Cursor::new(data)).ok()
}

/// Sideloader's Studio items (UniversalAutoResolver "itemInfo", StudioResolveInfo): scene object key (DicKey) -> the
/// mod's GUID and the item's slot in the mod's own list. The scene saved the session's local slot as the item's number.
pub fn scene_mod_items(bytes: &[u8]) -> std::collections::HashMap<i32, (String, i32)> {
    use crate::card::map_get;
    let mut out = std::collections::HashMap::new();
    let Some(blocks) = scene_plugins(bytes) else { return out };
    for key in ["com.bepis.sideloader.universalautoresolver", "EC.Core.Sideloader.UniversalAutoResolver"] {
        let Some(plugin) = map_get(&blocks, key) else { continue };
        let data = plugin.as_array().and_then(|a| a.get(1)).unwrap_or(plugin);
        for info in map_get(data, "itemInfo").and_then(rmpv::Value::as_array).into_iter().flatten() {
            let decoded = info.as_slice().and_then(|b| rmpv::decode::read_value(&mut std::io::Cursor::new(b)).ok());
            let info = decoded.as_ref().unwrap_or(info);
            let guid = map_get(info, "ModID").and_then(rmpv::Value::as_str).unwrap_or_default();
            let slot = map_get(info, "Slot").and_then(rmpv::Value::as_i64);
            // KKS sideloader writes SceneDicKey (with SceneObjectOrder, Group, Category); older ones DicKey.
            let dic = map_get(info, "SceneDicKey").or_else(|| map_get(info, "DicKey")).and_then(rmpv::Value::as_i64);
            if let (false, Some(slot), Some(dic)) = (guid.is_empty(), slot, dic) { out.insert(dic as i32, (guid.to_owned(), slot as i32)); }
        }
    }
    out
}

/// KKVMDPlay's scene data ("KKVMDPlayExtSave": XML VMDPlaySaveData): the scene part (VMDSceneData: camera motion, music)
/// and each character's part (VMDCharaData by its dicKey: the VMD motion file and the plugin's retarget settings), every
/// XML attribute kept by name. The files are paths on the machine that saved the scene.
pub fn scene_vmd(bytes: &[u8]) -> Option<(serde_json::Map<String, serde_json::Value>, std::collections::HashMap<i32, serde_json::Map<String, serde_json::Value>>)> {
    use crate::card::map_get;
    let blocks = scene_plugins(bytes)?;
    let plugin = map_get(&blocks, "KKVMDPlayExtSave")?;
    let data = plugin.as_array().and_then(|a| a.get(1)).unwrap_or(plugin);
    let xml = map_get(data, "xml").and_then(|v| v.as_str().map(str::to_owned).or_else(|| v.as_slice().map(|b| String::from_utf8_lossy(b).into_owned())))?;
    let doc = roxmltree::Document::parse(xml.trim_start_matches('\u{feff}')).ok()?;
    let attrs = |n: roxmltree::Node| n.attributes().map(|a| (a.name().to_owned(), serde_json::Value::from(a.value()))).collect::<serde_json::Map<_, _>>();
    let mut scene = serde_json::Map::new();
    let mut chars = std::collections::HashMap::new();
    for n in doc.root_element().children().filter(|n| n.is_element()) {
        match n.tag_name().name() {
            "VMDSceneData" => scene = attrs(n),
            "VMDCharaData" => if let Some(key) = n.attribute("dicKey").and_then(|k| k.parse::<i32>().ok()) { chars.insert(key, attrs(n)); },
            _ => {}
        }
    }
    Some((scene, chars))
}

/// Material Editor edits in the scene's plugin data.
fn scene_material_edits(bytes: &[u8]) -> Vec<MaterialEdit> {
    use crate::card::{map_get, me_plugin_data, parse_color};
    let Some(blocks) = scene_plugins(bytes) else { return Vec::new() };
    let Some(plugin) = map_get(&blocks, "com.deathweasel.bepinex.materialeditor") else { return Vec::new() };
    let Some(inner) = me_plugin_data(plugin) else { return Vec::new() };
    let mut out = Vec::new();
    for (list, kind) in [("MaterialColorPropertyList", "color"), ("MaterialFloatPropertyList", "float")] {
        let Some(bin) = map_get(inner, list).and_then(rmpv::Value::as_slice) else { continue };
        let Ok(items) = rmpv::decode::read_value(&mut std::io::Cursor::new(bin)) else { continue };
        for item in items.as_array().into_iter().flatten() {
            let id = map_get(item, "ID").and_then(rmpv::Value::as_i64).unwrap_or(-1) as i32;
            let material = map_get(item, "MaterialName").and_then(rmpv::Value::as_str).unwrap_or("").trim_end_matches(" (Instance)").to_string();
            let property = map_get(item, "Property").and_then(rmpv::Value::as_str).unwrap_or("").to_string();
            let value = match (kind, map_get(item, "Value")) {
                ("color", Some(v)) => parse_color(v),
                ("float", Some(v)) => match v.as_str().and_then(|s| s.parse::<f32>().ok()).or_else(|| v.as_f64().map(|f| f as f32)) { Some(f) => [f, 0.0, 0.0, 0.0], None => continue },
                _ => continue,
            };
            if id < 0 || material.is_empty() || property.is_empty() { continue; }
            out.push(MaterialEdit { id, material, property, kind: kind.into(), value, text: String::new() });
        }
    }
    let list = |name: &str| -> Vec<rmpv::Value> {
        map_get(inner, name).and_then(rmpv::Value::as_slice)
            .and_then(|bin| rmpv::decode::read_value(&mut std::io::Cursor::new(bin)).ok())
            .and_then(|v| v.as_array().cloned()).unwrap_or_default()
    };
    let text = |item: &rmpv::Value, key: &str| map_get(item, key).and_then(rmpv::Value::as_str).unwrap_or("").to_string();
    let id_of = |item: &rmpv::Value| map_get(item, "ID").and_then(rmpv::Value::as_i64).unwrap_or(-1) as i32;
    let material_of = |item: &rmpv::Value| text(item, "MaterialName").trim_end_matches(" (Instance)").to_string();
    // MaterialShaderList: the material's shader replaced (and its render queue, when set).
    for item in list("MaterialShaderList") {
        let (id, material, shader) = (id_of(&item), material_of(&item), text(&item, "ShaderName"));
        if id < 0 || material.is_empty() || shader.is_empty() { continue; }
        let queue = map_get(&item, "RenderQueue").and_then(rmpv::Value::as_i64).map_or(-1.0, |q| q as f32);
        out.push(MaterialEdit { id, material, property: String::new(), kind: "shader".into(), value: [queue, 0.0, 0.0, 0.0], text: shader });
    }
    // MaterialTexturePropertyList: a texture from TextureDictionary (by TexID) and / or its offset and scale.
    // Material Editor serializes the dictionary as msgpack bytes too (like the lists); older saves keep a plain map.
    let dictionary = map_get(inner, "TextureDictionary").map(|v| v.as_slice()
        .and_then(|bin| rmpv::decode::read_value(&mut std::io::Cursor::new(bin)).ok()).unwrap_or_else(|| v.clone()));
    let textures: Vec<(i64, Vec<u8>)> = dictionary.as_ref().and_then(rmpv::Value::as_map).into_iter().flatten()
        .filter_map(|(k, v)| Some((k.as_i64()?, v.as_slice()?.to_vec()))).collect();
    let vec2 = |v: Option<&rmpv::Value>| -> Option<[f32; 2]> {
        let v = v?;
        let f = |x: &rmpv::Value| x.as_f64().map(|f| f as f32);
        if let Some(a) = v.as_array() { return Some([f(a.first()?)?, f(a.get(1)?)?]); }
        Some([f(map_get(v, "x")?)?, f(map_get(v, "y")?)?])
    };
    for item in list("MaterialTexturePropertyList") {
        let (id, material, property) = (id_of(&item), material_of(&item), text(&item, "Property"));
        if id < 0 || material.is_empty() || property.is_empty() { continue; }
        let tex = map_get(&item, "TexID").and_then(rmpv::Value::as_i64).and_then(|t| textures.iter().find(|(k, _)| *k == t));
        if let Some((_, png)) = tex {
            out.push(MaterialEdit { id, material: material.clone(), property: property.clone(), kind: "texture".into(), value: [0.0; 4], text: base64(png) });
        }
        let (scale, offset) = (vec2(map_get(&item, "Scale")), vec2(map_get(&item, "Offset")));
        if scale.is_some() || offset.is_some() {
            let (s, o) = (scale.unwrap_or([1.0, 1.0]), offset.unwrap_or([0.0, 0.0]));
            out.push(MaterialEdit { id, material, property, kind: "texture_st".into(), value: [s[0], s[1], o[0], o[1]], text: String::new() });
        }
    }
    // RendererPropertyList: MaterialAPI.RendererProperties of a renderer (by its GameObject name).
    const RENDERER_PROPERTIES: [&str; 4] = ["Enabled", "ShadowCastingMode", "ReceiveShadows", "RecalculateNormals"];
    for item in list("RendererPropertyList") {
        let (id, renderer) = (id_of(&item), text(&item, "RendererName"));
        let Some(property) = map_get(&item, "Property").and_then(rmpv::Value::as_i64).and_then(|p| RENDERER_PROPERTIES.get(p as usize)) else { continue };
        let Some(value) = map_get(&item, "Value").and_then(|v| v.as_str().and_then(|s| s.parse::<f32>().ok()).or_else(|| v.as_f64().map(|f| f as f32))) else { continue };
        if id < 0 || renderer.is_empty() { continue; }
        out.push(MaterialEdit { id, material: renderer, property: (*property).into(), kind: "renderer".into(), value: [value, 0.0, 0.0, 0.0], text: String::new() });
    }
    out
}

/// Studio main camera (CameraControl.CameraData): the camera sits at `rotation(rotate) * distance + pos`, looking along
/// its rotation's +Z; `fov` is Unity's vertical field of view in degrees.
#[derive(Clone, Debug, Serialize)]
pub struct StudioCamera { pub pos: [f32; 3], pub rotate: [f32; 3], pub distance: [f32; 3], pub fov: f32, pub lights: Vec<StudioLight> }

/// The scene's screen settings (SceneInfo, the System menu): Amplify Color LUT (`ace_no` in the Filter list; the
/// grading LUT is lerp(that LUT, Lut_Amount, `ace_blend`): Studio's camera blends towards Lut_Amount), ambient occlusion, bloom, depth of field, vignette, fog, sun shafts,
/// shadows, the global outline / ramp / ambient shadow, and the background image (a file under the game folder) and
/// frame.
#[derive(Clone, Debug, Default, Serialize)]
pub struct SceneEffects {
    /// SceneInfo.caMap: the map root's local position and Euler rotation (Unity), set by MapCtrl.Reflect.
    pub map_pos: [f32; 3], pub map_rot: [f32; 3],
    /// SceneInfo.sunLightType: the map's time of day (SunLightInfo.Info.Type 0 morning, 1 day, 2 evening, 3 night).
    pub sun_light_type: i32,
    pub ace_no: i32, pub ace_blend: f32,
    pub aoe: bool, pub aoe_color: Color, pub aoe_radius: f32,
    pub bloom: bool, pub bloom_intensity: f32, pub bloom_blur: f32, pub bloom_threshold: f32,
    pub depth: bool, pub depth_focal_size: f32, pub depth_aperture: f32,
    pub vignette: bool,
    pub fog: bool, pub fog_color: Color, pub fog_height: f32, pub fog_start: f32,
    pub sun_shafts: bool, pub sun_threshold_color: Color, pub sun_color: Color,
    pub shadow: bool, pub face_normal: bool, pub face_shadow: bool,
    pub line_color_g: f32, pub ambient_shadow: Color, pub line_width_g: f32, pub ramp_g: i32, pub ambient_shadow_g: f32,
    // SceneInfo only serializes this field from 1.1.0.0. Older scenes
    // retain the installed graphic setting; absence must not become mode 0.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub shader_type: Option<i32>,
    /// SceneInfo.skyInfo (≥ 1.1.2.0, MessagePack {Enable, Pattern}): Studio's sky dome and its pattern.
    pub sky: bool, pub sky_pattern: i32,
    pub background: String, pub frame: String,
}

/// What SceneInfo.Load reads after the map: the screen settings, the main camera, the stored views, Studio's lights,
/// sound and the background.
fn main_camera(r: &mut Reader, v: &Version) -> Result<(StudioCamera, SceneEffects), String> {
    let mut e = SceneEffects::default();
    let map = r.change()?; // caMap
    (e.map_pos, e.map_rot) = (map.pos, map.rot);
    e.sun_light_type = r.i32()?;
    r.bool()?; // mapOption
    e.ace_no = r.i32()?;
    if v.at_least("0.0.2") { e.ace_blend = r.f32()?; }
    if !v.at_least("0.0.2") { r.bool()?; r.f32()?; r.string()?; }
    if v.at_least("0.0.2") { e.aoe = r.bool()?; e.aoe_color = r.json_color()?; e.aoe_radius = r.f32()?; }
    e.bloom = r.bool()?; e.bloom_intensity = r.f32()?; e.bloom_blur = r.f32()?;
    if v.at_least("0.0.2") { e.bloom_threshold = r.f32()?; }
    if !v.at_least("0.0.2") { r.bool()?; }
    e.depth = r.bool()?; e.depth_focal_size = r.f32()?; e.depth_aperture = r.f32()?;
    e.vignette = r.bool()?;
    if !v.at_least("0.0.2") { r.f32()?; }
    e.fog = r.bool()?;
    if v.at_least("0.0.2") { e.fog_color = r.json_color()?; e.fog_height = r.f32()?; e.fog_start = r.f32()?; }
    e.sun_shafts = r.bool()?;
    if v.at_least("0.0.2") { e.sun_threshold_color = r.json_color()?; e.sun_color = r.json_color()?; }
    if v.at_least("0.0.4") { r.i32()?; } // sunCaster
    if v.at_least("0.0.2") { e.shadow = r.bool()?; }
    if v.at_least("0.0.4") { e.face_normal = r.bool()?; e.face_shadow = r.bool()?; e.line_color_g = r.f32()?; e.ambient_shadow = r.json_color()?; }
    if v.at_least("0.0.5") { e.line_width_g = r.f32()?; e.ramp_g = r.i32()?; e.ambient_shadow_g = r.f32()?; }
    if v.at_least("1.1.0.0") { e.shader_type = Some(r.i32()?); }
    if v.at_least("1.1.2.0") {
        let n = r.bytes_len("sky info")?;
        let bytes = r.take(n)?;
        // MessagePack of SkyInfo: a map {"Enable": bool, "Pattern": int} or the array [Enable, Pattern].
        if let Ok(value) = rmpv::decode::read_value(&mut std::io::Cursor::new(bytes)) {
            let field = |key: &str, index: usize| match &value {
                rmpv::Value::Map(m) => m.iter().find(|(k, _)| k.as_str() == Some(key)).map(|(_, v)| v.clone()),
                rmpv::Value::Array(a) => a.get(index).cloned(),
                _ => None,
            };
            e.sky = field("Enable", 0).and_then(|v| v.as_bool()).unwrap_or(false);
            e.sky_pattern = field("Pattern", 1).and_then(|v| v.as_i64()).unwrap_or(0) as i32;
        }
    }
    let kind = r.i32()?;
    let pos = r.v3()?;
    let rotate = r.v3()?;
    let distance = if kind == 1 { [0.0, 0.0, r.f32()?] } else { r.v3()? };
    let fov = r.f32()?;
    if !(1.0..=179.0).contains(&fov) || pos.iter().chain(&rotate).chain(&distance).any(|x| !x.is_finite()) { return Err(format!("implausible camera (fov {fov})")); }
    // The ten stored views, then Studio's own lights (CameraLightCtrl.LightInfo): the character light turns with the
    // camera, the map light stands in the world; then BGM, environment sound and outside sound, the background, the frame.
    let mut lights = Vec::new();
    let tail = (|| -> Result<(), String> {
        for _ in 0..10 { let k = r.i32()?; r.v3()?; r.v3()?; if k == 1 { r.f32()?; } else { r.v3()?; } r.f32()?; }
        for map in [false, true] {
            let color = r.json_color()?;
            let intensity = r.f32()?;
            let rot = [r.f32()?, r.f32()?];
            let shadow = r.bool()?;
            let kind = if map { r.i32()? } else { 1 };
            lights.push(StudioLight { camera: !map, color, intensity, rot, shadow, kind });
        }
        Ok(())
    })();
    if tail.is_err() { lights.clear(); }
    else {
        let sound = (|| -> Result<(String, String), String> {
            r.i32()?; r.i32()?; r.bool()?; // BGMCtrl
            r.i32()?; r.i32()?; r.bool()?; // ENVCtrl
            r.i32()?; r.string()?; r.bool()?; // OutsideSoundCtrl
            Ok((r.string()?, r.string()?))
        })();
        if let Ok((background, frame)) = sound { e.background = background; e.frame = frame; }
    }
    Ok((StudioCamera { pos, rotate, distance, fov, lights }, e))
}

/// Studio's character light (`camera`: rotated by `rot` from the camera) or map light (`rot` in the world); `kind` is
/// Unity's LightType (1 directional).
#[derive(Clone, Debug, Serialize)]
pub struct StudioLight { pub camera: bool, pub color: Color, pub intensity: f32, pub rot: [f32; 2], pub shadow: bool, pub kind: i32 }

struct Reader<'a> { b: &'a [u8], p: usize }
impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], String> {
        let s = self.b.get(self.p..self.p.checked_add(n).ok_or("bad length")?).ok_or_else(|| format!("truncated at {}", self.p))?;
        self.p += n;
        Ok(s)
    }
    fn i32(&mut self) -> Result<i32, String> { Ok(i32::from_le_bytes(self.take(4)?.try_into().unwrap())) }
    fn i64(&mut self) -> Result<i64, String> { Ok(i64::from_le_bytes(self.take(8)?.try_into().unwrap())) }
    fn f32(&mut self) -> Result<f32, String> { Ok(f32::from_le_bytes(self.take(4)?.try_into().unwrap())) }
    fn bool(&mut self) -> Result<bool, String> { Ok(self.take(1)?[0] != 0) }
    fn count(&mut self, what: &str) -> Result<usize, String> {
        let n = self.i32()?;
        if !(0..=100_000).contains(&n) { return Err(format!("bad {what} count {n} at {}", self.p - 4)); }
        Ok(n as usize)
    }
    /// A byte length (images and blobs can be megabytes).
    fn bytes_len(&mut self, what: &str) -> Result<usize, String> {
        let n = self.i32()?;
        if !(0..=1 << 28).contains(&n) { return Err(format!("bad {what} length {n} at {}", self.p - 4)); }
        Ok(n as usize)
    }
    fn string(&mut self) -> Result<String, String> {
        let (mut len, mut shift) = (0usize, 0);
        loop {
            let byte = self.take(1)?[0];
            len |= ((byte & 0x7f) as usize) << shift;
            if byte & 0x80 == 0 { break; }
            shift += 7;
            if shift > 28 { return Err("bad string length".into()); }
        }
        Ok(String::from_utf8_lossy(self.take(len)?).into_owned())
    }
    fn v3(&mut self) -> Result<[f32; 3], String> { Ok([self.f32()?, self.f32()?, self.f32()?]) }
    fn change(&mut self) -> Result<ChangeAmount, String> { Ok(ChangeAmount { pos: self.v3()?, rot: self.v3()?, scale: self.v3()? }) }
    /// Unity JsonUtility colour (`{"r":..,"g":..,"b":..,"a":..}`).
    fn json_color(&mut self) -> Result<Color, String> {
        let s = self.string()?;
        let v: serde_json::Value = serde_json::from_str(&s).unwrap_or_default();
        let f = |k: &str| v.get(k).and_then(|x| x.as_f64()).unwrap_or(0.) as f32;
        Ok(Color { r: f("r"), g: f("g"), b: f("b"), a: f("a") })
    }
    fn color(&mut self) -> Result<Color, String> { Ok(Color { r: self.f32()?, g: self.f32()?, b: self.f32()?, a: self.f32()? }) }
}

/// Byte length of a PNG at the start of `b` (0 when it does not start with one).
pub fn png_len(b: &[u8]) -> usize {
    if !b.starts_with(&[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a]) { return 0; }
    let mut p = 8;
    while p + 12 <= b.len() {
        let len = u32::from_be_bytes(b[p..p + 4].try_into().unwrap()) as usize;
        let kind = &b[p + 4..p + 8];
        p += 12 + len;
        if kind == b"IEND" { return p.min(b.len()); }
    }
    0
}

/// ObjectInfo: key, change amount and (for tree objects) tree state and visibility.
fn base(r: &mut Reader, other: bool) -> Result<(i32, ChangeAmount, bool), String> {
    let key = r.i32()?;
    let change = r.change()?;
    let mut visible = true;
    if other { r.i32()?; visible = r.bool()?; }
    Ok((key, change, visible))
}

fn bones(r: &mut Reader) -> Result<Vec<(i32, ChangeAmount)>, String> {
    let n = r.count("bone")?;
    let mut out = Vec::with_capacity(n);
    for _ in 0..n { let k = r.i32()?; let (_, c, _) = base(r, false)?; out.push((k, c)); }
    Ok(out)
}

fn children(r: &mut Reader, v: &Version, depth: usize) -> Result<Vec<Object>, String> {
    let n = r.count("child")?;
    let mut out = Vec::with_capacity(n);
    for _ in 0..n { let kind = r.i32()?; out.push(object(r, v, kind, depth + 1)?); }
    Ok(out)
}

/// Studio PatternInfo: mt_pattern id (0 none; -1 a user file), user file, clamp, UV (xy offset, zw scale), rotation.
#[derive(Clone, Debug, Serialize)]
pub struct Pattern { pub key: i32, pub file: String, pub clamp: bool, pub uv: [f32; 4], pub rot: f32 }

fn pattern(r: &mut Reader) -> Result<Pattern, String> {
    let key = r.i32()?;
    let file = r.string()?;
    let clamp = r.bool()?;
    let uv: serde_json::Value = serde_json::from_str(&r.string()?).unwrap_or_default();
    let f = |k: &str, d: f32| uv.get(k).and_then(|x| x.as_f64()).map(|x| x as f32).unwrap_or(d);
    let rot = r.f32()?;
    Ok(Pattern { key, file, clamp, uv: [f("x", 0.0), f("y", 0.0), f("z", 1.0), f("w", 1.0)], rot })
}

/// ChaFile.LoadFile with noLoadPNG: (offset, len) of the card data and of its face PNG.
fn chara(r: &mut Reader) -> Result<(usize, usize, usize, usize), String> {
    let png = png_len(&r.b[r.p..]);
    r.p += png;
    let start = r.p;
    let product = r.i32()?;
    if product > 100 { return Err(format!("bad card product {product}")); }
    let mark = r.string()?;
    if !mark.starts_with("【KoiKatuChara") { return Err(format!("not card data: {mark}")); }
    r.string()?;
    let face_len = r.bytes_len("face png")?;
    let face_off = r.p;
    r.take(face_len)?;
    let header = r.bytes_len("block header")?;
    r.take(header)?;
    let data = r.i64()?;
    if !(0..=1 << 30).contains(&data) { return Err(format!("bad card data length {data}")); }
    r.take(data as usize)?;
    Ok((start, r.p - start, face_off, face_len))
}

fn object(r: &mut Reader, v: &Version, kind: i32, depth: usize) -> Result<Object, String> {
    if depth > 1024 { return Err("object tree too deep".into()); }
    match kind {
        0 => {
            let (key, change, visible) = base(r, true)?;
            let sex = r.i32()?;
            let (card_offset, card_len, face_png_offset, face_png_len) = chara(r)?;
            // ExtendedSave appends the character's plugin data: "KKEx", version, length, msgpack.
            let (mut ext_offset, mut ext_len) = (0, 0);
            let mark = r.p;
            if r.string().ok().as_deref() == Some("KKEx") {
                r.i32()?;
                ext_len = r.bytes_len("extended data")?;
                ext_offset = r.p;
                r.take(ext_len)?;
            } else { r.p = mark; }
            let fk = bones(r)?;
            let ik = bones(r)?;
            let slots = r.count("accessory slot")?;
            let mut kids = Vec::new();
            for _ in 0..slots { let slot = r.i32()?; kids.push((slot, children(r, v, depth)?)); }
            r.i32()?; // kinematic mode
            let anime = [r.i32()?, r.i32()?, r.i32()?];
            let hand = [r.i32()?, r.i32()?];
            let nipple = r.f32()?;
            let siru: [u8; 5] = r.take(5)?.try_into().unwrap();
            let mouth_open = r.f32()?;
            r.bool()?; // lip sync
            base(r, false)?; // look-at target
            let enable_ik = r.bool()?;
            let mut active_ik = [false; 5];
            for a in &mut active_ik { *a = r.bool()?; }
            let enable_fk = r.bool()?;
            let mut active_fk = [false; 7];
            for a in &mut active_fk { *a = r.bool()?; }
            for _ in 0..if v.at_least("0.0.9") { 8 } else { 4 } { r.bool()?; }
            let anime_speed = r.f32()?;
            let anime_pattern = r.f32()?;
            r.bool()?; // option visible
            let anime_force_loop = r.bool()?;
            let voices = r.count("voice")?;
            r.take(voices * 12)?;
            r.i32()?; // voice repeat
            r.bool()?; r.f32()?; r.bool()?; // son visible / length, simple
            r.json_color()?;
            let anime_options = [r.f32()?, r.f32()?];
            let neck_len = r.bytes_len("neck data")?;
            let neck = read_neck(r.take(neck_len)?)?;
            let eyes_len = r.bytes_len("eyes data")?;
            let eyes = read_eyes(r.take(eyes_len)?, v)?;
            let anime_time = r.f32()?;
            for _ in 0..2 { let n = r.count("access")?; r.take(n * 8)?; }
            Ok(Object::Character { key, change, visible, sex, card_offset, card_len, face_png_offset, face_png_len, ext_offset, ext_len, fk, ik, enable_fk, active_fk, enable_ik, active_ik, neck, eyes, anime, anime_speed, anime_time, hand, mouth_open, anime_pattern, anime_options, anime_force_loop, nipple, siru, children: kids })
        }
        1 => {
            let (key, change, visible) = base(r, true)?;
            let (group, category, no) = (r.i32()?, r.i32()?, r.i32()?);
            let anime_pattern = if v.at_least("1.1.1.0") { r.i32()? } else { 0 };
            let anime_speed = r.f32()?;
            let mut colors = Vec::new();
            for _ in 0..if v.at_least("0.0.3") { 8 } else { 7 } { colors.push(r.json_color()?); }
            let patterns = vec![pattern(r)?, pattern(r)?, pattern(r)?];
            let alpha = r.f32()?;
            let (mut line_color, mut line_width) = (Color::default(), 1.0);
            if v.at_least("0.0.4") { line_color = r.json_color()?; line_width = r.f32()?; }
            let (mut emission_color, mut emission_power, mut light_cancel) = (Color::default(), 0.0, 0.0);
            if v.at_least("0.0.7") { emission_color = r.json_color()?; emission_power = r.f32()?; light_cancel = r.f32()?; }
            if v.at_least("0.0.6") { pattern(r)?; }
            let enable_fk = r.bool()?;
            let n = r.count("item bone")?;
            let mut fk = Vec::with_capacity(n);
            for _ in 0..n { let name = r.string()?; let (_, c, _) = base(r, false)?; fk.push((name, c)); }
            if v.at_least("1.0.1") { r.bool()?; }
            let anime_time = r.f32()?;
            let children = children(r, v, depth)?;
            Ok(Object::Item { key, change, visible, group, category, no, colors, patterns, anime_speed, anime_pattern, anime_time, alpha, line_color, line_width, emission_color, emission_power, light_cancel, enable_fk, fk, children })
        }
        2 => {
            let (key, change, visible) = base(r, true)?;
            let no = r.i32()?;
            let color = r.color()?;
            let (intensity, range, spot_angle) = (r.f32()?, r.f32()?, r.f32()?);
            let (shadow, enable) = (r.bool()?, r.bool()?);
            r.bool()?; // draw target
            Ok(Object::Light { key, change, visible, no, color, intensity, range, spot_angle, shadow, enable })
        }
        3 => {
            let (key, change, visible) = base(r, true)?;
            let name = r.string()?;
            Ok(Object::Folder { key, change, visible, name, children: children(r, v, depth)? })
        }
        4 => {
            let (key, change, visible) = base(r, true)?;
            let name = r.string()?;
            let children = children(r, v, depth)?;
            let count = r.count("route point")?;
            let mut points = Vec::with_capacity(count);
            for _ in 0..count {
                let (_, change, _) = base(r, false)?;
                let speed = r.f32()?;
                let ease = r.i32()?;
                if *v == Version::parse("1.0.3") { r.bool()?; }
                let (mut curve, mut aid, mut link) = (false, None, false);
                if v.at_least("1.0.4.1") {
                    curve = r.i32()? == 1;
                    let (_, aid_change, _) = base(r, false)?;
                    // An aid never placed sits half way to the next point (OCIRoutePoint.InitAidPos).
                    aid = r.bool()?.then_some(aid_change);
                }
                if v.at_least("1.0.4.2") { link = r.bool()?; }
                points.push(RoutePoint { change, speed, ease, curve, aid, link });
            }
            let (mut active, mut looped, mut orient) = (false, true, 0);
            if v.at_least("1.0.3") { active = r.bool()?; looped = r.bool()?; r.bool()?; }
            if v.at_least("1.0.4") { orient = r.i32()?; }
            if v.at_least("1.0.4.1") { r.json_color()?; }
            Ok(Object::Route { key, change, visible, name, children, points, active, looped, orient })
        }
        5 => {
            let (key, change, visible) = base(r, true)?;
            let name = r.string()?;
            let active = r.bool()?;
            Ok(Object::Camera { key, change, visible, name, active })
        }
        7 => {
            let (key, change, visible) = base(r, true)?;
            let id = r.i32()?;
            r.json_color()?; r.json_color()?; r.f32()?;
            let n = r.bytes_len("text data")?;
            r.take(n)?;
            Ok(Object::Text { key, change, visible, id })
        }
        other => Err(format!("unknown object type {other}")),
    }
}

/// Parses a scene file's objects and map. Objects nest up to 1024 deep, so the recursive parse runs on its own
/// thread with a large stack (the caller's — the main thread, a test or UE's game thread — may be small).
pub fn read(bytes: &[u8]) -> Result<Scene, String> {
    std::thread::scope(|s| {
        std::thread::Builder::new().name("ok-scene-read".into()).stack_size(128 << 20).spawn_scoped(s, || read_nested(bytes))
            .map_err(|e| format!("scene reader thread: {e}"))?
            .join().map_err(|_| "scene reader panicked".to_string())?
    })
}

fn read_nested(bytes: &[u8]) -> Result<Scene, String> {
    let mut r = Reader { b: bytes, p: png_len(bytes) };
    if r.p == 0 { return Err("not a KK Studio scene (no PNG)".into()); }
    let version = r.string()?;
    let v = Version::parse(&version);
    if v.0[0] < 0 { return Err(format!("not a KK Studio scene (version {version:?})")); }
    let n = r.count("object")?;
    let mut objects = Vec::with_capacity(n);
    for _ in 0..n {
        r.i32()?; // key
        let kind = r.i32()?;
        objects.push(object(&mut r, &v, kind, 0).map_err(|e| format!("object {}: {e}", objects.len()))?);
    }
    let map = r.i32()?;
    // Older or damaged tails leave the camera out rather than failing the scene.
    let (camera, effects) = main_camera(&mut r, &v).ok().map(|(c, e)| (Some(c), Some(e))).unwrap_or((None, None));
    Ok(Scene { version, objects, map, camera, effects, material_edits: scene_material_edits(bytes) })
}

/// The KKS card a scene character was saved from: its face thumbnail as the card image, its card data, and the
/// plugin data the scene kept beside it as the card's KKEx block (merged over one the card has, the scene's winning).
pub fn card_bytes(scene: &[u8], object: &Object) -> Result<Vec<u8>, String> {
    let Object::Character { card_offset, card_len, face_png_offset, face_png_len, ext_offset, ext_len, .. } = object else { return Err("not a character".into()); };
    let face = scene.get(*face_png_offset..face_png_offset + face_png_len).filter(|f| png_len(f) > 0).ok_or("character has no face thumbnail")?;
    let mut card = face.to_vec();
    card.extend_from_slice(scene.get(*card_offset..card_offset + card_len).ok_or("card data out of range")?);
    if *ext_len == 0 { return Ok(card); }
    let ext = scene.get(*ext_offset..ext_offset + ext_len).ok_or("extended data out of range")?;
    let mut blocks = crate::appearance::blocks_from_card(&card)?;
    let decode = |b: &[u8]| rmpv::decode::read_value(&mut std::io::Cursor::new(b)).map_err(|e| e.to_string());
    let mut merged = blocks.get("KKEx").map(|b| decode(b)).transpose()?.unwrap_or(rmpv::Value::Map(Vec::new()));
    if let (rmpv::Value::Map(into), rmpv::Value::Map(from)) = (&mut merged, decode(ext)?) {
        for (k, v) in from {
            if let Some(slot) = into.iter_mut().find(|(key, _)| *key == k) { slot.1 = v; } else { into.push((k, v)); }
        }
    }
    let mut raw = Vec::new();
    rmpv::encode::write_value(&mut raw, &merged).map_err(|e| e.to_string())?;
    blocks.insert("KKEx".into(), raw);
    crate::appearance::write_blocks(&card, blocks)
}

#[derive(Default, Serialize)]
pub struct Stats {
    pub files: usize, pub parsed: usize, pub failed: Vec<(String, String)>,
    /// Scenes whose main camera (SceneInfo.cameraSaveData) was read.
    pub main_cameras: usize,
    pub characters: usize, pub items: usize, pub lights: usize, pub folders: usize, pub routes: usize, pub cameras: usize, pub texts: usize,
    /// Routes with children and at least two points (they move when played), and routes whose start point (where the
    /// children rest) is not at the route's origin.
    pub routes_moving: usize, pub routes_moved_start: usize,
    pub vanilla_items: usize, pub mod_items: usize,
    /// Catalogue items posed with item FK (enabled, with bones).
    pub fk_items: usize, pub fk_item_ids: std::collections::BTreeMap<(i32, i32, i32), usize>, pub maps: std::collections::BTreeMap<i32, usize>,
    /// Characters playing a Studio motion: in the OK animation library, or not (mod motions), by (group, category, no).
    pub library_motions: usize, pub other_motions: std::collections::BTreeMap<(i32, i32, i32), usize>,
}

fn count(objects: &[Object], s: &mut Stats, vanilla: &dyn Fn(i32, i32, i32) -> bool) { count_with(objects, s, vanilla, &|_, _, _| true) }
fn count_with(objects: &[Object], s: &mut Stats, vanilla: &dyn Fn(i32, i32, i32) -> bool, motion: &dyn Fn(i32, i32, i32) -> bool) {
    for o in objects {
        match o {
            Object::Character { children, anime, .. } => {
                s.characters += 1;
                if anime[0] >= 0 { if motion(anime[0], anime[1], anime[2]) { s.library_motions += 1 } else { *s.other_motions.entry((anime[0], anime[1], anime[2])).or_default() += 1 } }
                for (_, c) in children { count_with(c, s, vanilla, motion); }
            }
            Object::Item { group, category, no, children, enable_fk, fk, .. } => { s.items += 1; if *enable_fk && !fk.is_empty() && vanilla(*group, *category, *no) { s.fk_items += 1; *s.fk_item_ids.entry((*group, *category, *no)).or_default() += 1 } if vanilla(*group, *category, *no) { s.vanilla_items += 1 } else { s.mod_items += 1 }; count_with(children, s, vanilla, motion); }
            Object::Light { .. } => s.lights += 1,
            Object::Folder { children, .. } => { s.folders += 1; count_with(children, s, vanilla, motion); }
            Object::Route { children, points, .. } => {
                s.routes += 1;
                if points.len() > 1 && !children.is_empty() { s.routes_moving += 1; }
                if !children.is_empty() && points.first().is_some_and(|p| p.change.pos.iter().chain(&p.change.rot).any(|v| v.abs() > 1e-3)) { s.routes_moved_start += 1; }
                count_with(children, s, vanilla, motion);
            }
            Object::Camera { .. } => s.cameras += 1,
            Object::Text { .. } => s.texts += 1,
        }
    }
}

/// `--oklib scene-stats <lib> <scene dir>`: parses every scene under the directory (recursively) and counts objects.
pub fn stats(lib: &std::path::Path, dir: &std::path::Path) -> Result<Stats, String> {
    let catalog: crate::studio_items::Catalog = serde_json::from_str(&std::fs::read_to_string(lib.join(crate::studio_items::FILE_NAME)).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    let known: std::collections::HashSet<(i32, i32, i32)> = catalog.items.iter().map(|i| (i.group, i.category, i.no)).collect();
    let vanilla = |g: i32, c: i32, n: i32| known.contains(&(g, c, n));
    let anims: Option<crate::studio_anims::AnimCatalog> = std::fs::read_to_string(lib.join(crate::studio_anims::FILE_NAME)).ok().and_then(|t| serde_json::from_str(&t).ok());
    let motions: std::collections::HashSet<(i32, i32, i32)> = anims.iter().flat_map(|c| c.anims.iter().filter(|a| !a.file.is_empty()).map(|a| (a.group, a.category, a.no))).collect();
    let motion = |g: i32, c: i32, n: i32| motions.contains(&(g, c, n));
    let mut files = Vec::new();
    fn walk(d: &std::path::Path, out: &mut Vec<std::path::PathBuf>) {
        let Ok(rd) = std::fs::read_dir(d) else { return; };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() { walk(&p, out); } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")) { out.push(p); }
        }
    }
    walk(dir, &mut files);
    files.sort();
    let mut s = Stats { files: files.len(), ..Stats::default() };
    for f in &files {
        let Ok(bytes) = std::fs::read(f) else { continue; };
        match read(&bytes) {
            Ok(scene) => { s.parsed += 1; s.main_cameras += scene.camera.is_some() as usize; *s.maps.entry(scene.map).or_default() += 1; count_with(&scene.objects, &mut s, &vanilla, &motion); }
            Err(e) => s.failed.push((f.display().to_string(), e)),
        }
    }
    Ok(s)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    #[ignore = "diagnostic: the sideloader resolver's scene data of OK_SCENE"]
    fn print_scene_resolver_data() {
        let Ok(path) = std::env::var("OK_SCENE") else { return };
        let bytes = std::fs::read(path).unwrap();
        let blocks = scene_plugins(&bytes).unwrap();
        for key in ["com.bepis.sideloader.universalautoresolver", "EC.Core.Sideloader.UniversalAutoResolver"] {
            let Some(plugin) = crate::card::map_get(&blocks, key) else { continue };
            let data = plugin.as_array().and_then(|a| a.get(1)).unwrap_or(plugin);
            for (k, v) in data.as_map().into_iter().flatten() {
                let decoded = v.as_slice().and_then(|b| rmpv::decode::read_value(&mut std::io::Cursor::new(b)).ok());
                let text = format!("{}", decoded.as_ref().unwrap_or(v));
                println!("{k}: {}", &text[..text.len().min(600)]);
            }
        }
    }
    // OK_CMP_NATIVE=<original capture>, OK_CMP_OK=<OK Studio picture> (a CompareKStudio picture: its right half),
    // OK_CMP_GRID=<columns>x<rows> (default 8x4): cells whose mean colour differs by more than 10 % (diagnostic).
    #[test]
    #[ignore = "diagnostic: compares the pictures named by OK_CMP_NATIVE and OK_CMP_OK"]
    fn print_picture_grid_difference() {
        let (Ok(a), Ok(b)) = (std::env::var("OK_CMP_NATIVE"), std::env::var("OK_CMP_OK")) else { return };
        let mut b = image::open(b).unwrap().to_rgb8();
        // OK_CMP_NATIVE=card: the scene card's own picture, the left half of the CompareKStudio picture.
        let a = if a == "card" { image::imageops::crop_imm(&b, 0, 0, b.width() / 2, b.height()).to_image() } else { image::open(a).unwrap().to_rgb8() };
        if b.width() >= 2 * b.height() * 16 / 9 - 4 { b = image::imageops::crop_imm(&b, b.width() / 2, 0, b.width() / 2, b.height()).to_image(); }
        let b = image::imageops::resize(&b, a.width(), a.height(), image::imageops::FilterType::Triangle);
        let grid = std::env::var("OK_CMP_GRID").unwrap_or_else(|_| "8x4".into());
        let (cols, rows) = grid.split_once('x').map(|(c, r)| (c.parse::<u32>().unwrap(), r.parse::<u32>().unwrap())).unwrap();
        let mut over = 0;
        for r in 0..rows {
            let mut line = String::new();
            for c in 0..cols {
                let (x0, y0, x1, y1) = (c * a.width() / cols, r * a.height() / rows, (c + 1) * a.width() / cols, (r + 1) * a.height() / rows);
                let mean = |img: &image::RgbImage| {
                    let mut s = [0f64; 3];
                    for y in y0..y1 { for x in x0..x1 { let p = img.get_pixel(x, y); for k in 0..3 { s[k] += p[k] as f64; } } }
                    let n = ((x1 - x0) * (y1 - y0)) as f64;
                    s.map(|v| v / n)
                };
                let (ma, mb) = (mean(&a), mean(&b));
                // Relative difference of the cell's mean luminance, and the largest channel difference (0..255).
                let lum = |m: [f64; 3]| 0.299 * m[0] + 0.587 * m[1] + 0.114 * m[2];
                let rel = (lum(mb) - lum(ma)) / lum(ma).max(8.0);
                let channel = (0..3).map(|k| (mb[k] - ma[k]).abs()).fold(0.0, f64::max);
                let flag = rel.abs() > 0.10 || channel > 25.5;
                over += usize::from(flag);
                line += &format!("{}{:+4.0}%/{:3.0} ", if flag { '*' } else { ' ' }, rel * 100.0, channel);
                // OK_CMP_MEANS=1: each cell's mean colour, original then OK.
                if std::env::var_os("OK_CMP_MEANS").is_some() {
                    line += &format!("[{:.0},{:.0},{:.0}|{:.0},{:.0},{:.0}] ", ma[0], ma[1], ma[2], mb[0], mb[1], mb[2]);
                }
            }
            println!("{line}");
        }
        println!("{over} of {} cells over 10 %", cols * rows);
    }

    // OK_SCENE_DIR=<folder>: which scene plugins carry data, over every KStudio scene below it (diagnostic).
    #[test]
    #[ignore = "diagnostic: reads the scenes below OK_SCENE_DIR"]
    fn print_scene_plugin_usage() {
        // Scene trees nest up to 1024 deep (parsing, walking and dropping them recurse).
        std::thread::Builder::new().stack_size(1 << 30).spawn(scene_plugin_usage).unwrap().join().unwrap();
    }
    fn scene_plugin_usage() {
        fn walk(dir: &std::path::Path, out: &mut Vec<std::path::PathBuf>) {
            for e in std::fs::read_dir(dir).into_iter().flatten().flatten() {
                let p = e.path();
                if p.is_dir() { walk(&p, out) } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")) { out.push(p) }
            }
        }
        let mut files = Vec::new();
        walk(std::path::Path::new(&std::env::var("OK_SCENE_DIR").expect("OK_SCENE_DIR")), &mut files);
        let mut used: std::collections::BTreeMap<String, (usize, String)> = Default::default();
        for f in &files {
            let Ok(bytes) = std::fs::read(f) else { continue };
            // Scenes whose routes carry children away from the route's origin (start point moved).
            fn moved(objects: &[super::Object]) -> bool {
                objects.iter().any(|o| match o {
                    super::Object::Route { children, points, .. } => !children.is_empty()
                        && points.first().is_some_and(|p| p.change.pos.iter().any(|v| v.abs() > 0.05)) || moved(children),
                    super::Object::Item { children, .. } | super::Object::Folder { children, .. } => moved(children),
                    _ => false,
                })
            }
            if super::read(&bytes).is_ok_and(|s| moved(&s.objects)) {
                let e = used.entry("(route start point moved)".into()).or_default();
                e.0 += 1;
                if e.0 <= 6 { e.1 += &format!("{} ", f.display()); }
            }
            if let Some(fx) = super::read_nested(&bytes).ok().and_then(|s| s.effects).filter(|e| e.sky) {
                let e = used.entry(format!("(Studio sky pattern {})", fx.sky_pattern)).or_default();
                e.0 += 1;
                if e.1.is_empty() { e.1 = f.display().to_string(); }
            }
            let Some(blocks) = super::scene_plugins(&bytes) else { continue };
            for (k, v) in blocks.as_map().into_iter().flatten() {
                let data = v.as_array().and_then(|a| a.get(1)).unwrap_or(v);
                if data.is_nil() || data.as_map().is_some_and(|m| m.is_empty()) { continue; }
                if k.as_str() == Some("com.deathweasel.bepinex.materialeditor") {
                    for (list, value) in data.as_map().into_iter().flatten() {
                        if value.is_nil() { continue; }
                        let e = used.entry(format!("(item Material Editor {})", list.as_str().unwrap_or("?"))).or_default();
                        e.0 += 1;
                        if e.1.is_empty() { e.1 = f.display().to_string(); }
                    }
                }
                let e = used.entry(k.as_str().unwrap_or("?").to_string()).or_default();
                e.0 += 1;
                if e.1.is_empty() { e.1 = format!("{} {}", f.display(), format!("{data}").chars().take(300).collect::<String>()); }
            }
        }
        println!("{} scenes", files.len());
        for (k, (n, example)) in used { println!("{n:5} {k}\n      {example}"); }
    }

    // OK_SCENE=<KStudio png>: print its version, map and screen settings (diagnostic).
    #[test]
    #[ignore = "diagnostic: reads the scene named by OK_SCENE"]
    fn print_scene_effects() {
        let Ok(path) = std::env::var("OK_SCENE") else { return };
        let scene = read(&std::fs::read(path).unwrap()).unwrap();
        eprintln!("version={} map={} effects={}", scene.version, scene.map, serde_json::to_string(&scene.effects).unwrap());
        for e in scene.material_edits.iter().take(40) { eprintln!("me {:?}", e); }
        eprintln!("material edits {}", scene.material_edits.len());
        // The other Material Editor lists, decoded (they are msgpack inside msgpack).
        let bytes = std::fs::read(std::env::var("OK_SCENE").unwrap()).unwrap();
        let Some(blocks) = super::scene_plugins(&bytes) else { return };
        let Some(inner) = crate::card::map_get(&blocks, "com.deathweasel.bepinex.materialeditor").and_then(crate::card::me_plugin_data) else { return };
        for (k, v) in inner.as_map().into_iter().flatten() {
            let shown = v.as_slice().and_then(|b| rmpv::decode::read_value(&mut std::io::Cursor::new(b)).ok()).map(|d| d.to_string()).unwrap_or_else(|| v.to_string());
            eprintln!("me {k}: {}", shown.chars().take(1500).collect::<String>());
        }
    }
    #[test]
    #[ignore = "game oracle: reads an official KStudio scene with routes (set OK_TEST_GAME_ROOT)"]
    fn routes_keep_their_points() {
        let bytes = std::fs::read(crate::test_support::game_path("UserData/Studio/scene/Official/koikatu_cs0004888.png")).unwrap();
        let scene = super::read(&bytes).unwrap();
        fn routes(objects: &[super::Object], out: &mut Vec<(usize, usize, bool)>) {
            for o in objects {
                match o {
                    super::Object::Route { children, points, looped, .. } => { out.push((points.len(), children.len(), *looped)); routes(children, out); }
                    super::Object::Item { children, .. } | super::Object::Folder { children, .. } => routes(children, out),
                    _ => {}
                }
            }
        }
        let mut found = Vec::new();
        routes(&scene.objects, &mut found);
        println!("routes (points, children, loop): {found:?}");
        assert_eq!(found.len(), 4);
        assert!(found.iter().all(|(points, children, _)| *points >= 10 && *children >= 2), "{found:?}");
    }
    #[test]
    fn base64_pads_like_rfc4648() {
        assert_eq!(super::base64(b""), "");
        assert_eq!(super::base64(b"f"), "Zg==");
        assert_eq!(super::base64(b"fo"), "Zm8=");
        assert_eq!(super::base64(b"foo"), "Zm9v");
        assert_eq!(super::base64(b"foobar"), "Zm9vYmFy");
        assert_eq!(super::base64(&[0xff, 0xee]), "/+4=");
    }
    #[test]
    fn versions_compare_like_dotnet() {
        assert!(Version::parse("1.0.4") < Version::parse("1.0.4.1"));
        assert!(Version::parse("1.1.2.1").at_least("1.1.1.0"));
        assert!(!Version::parse("0.0.8").at_least("0.0.9"));
        assert!(Version::parse("0.0.10") > Version::parse("0.0.9"));
    }
    #[test]
    #[ignore = "base library: reads all fourteen Studio sample scene cards"]
    fn sample_shader_mode_presence_matches_source_version() {
        let samples = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../Content/IndependentResources/base_v1/studio/samples");
        let mut checked = 0;
        for entry in std::fs::read_dir(samples).unwrap() {
            let path = entry.unwrap().path();
            if path.extension().and_then(|s|s.to_str()) != Some("png") { continue; }
            let scene = read(&std::fs::read(&path).unwrap()).unwrap();
            let effects = scene.effects.as_ref().expect("sample has complete effect settings");
            let saved = Version::parse(&scene.version).at_least("1.1.0.0");
            assert_eq!(effects.shader_type.is_some(), saved, "{}", path.display());
            let json = serde_json::to_value(effects).unwrap();
            assert_eq!(json.get("shader_type").is_some(), saved, "{}", path.display());
            checked += 1;
        }
        assert_eq!(checked, 14);
    }
    #[test]
    #[ignore = "installed game: parses the official scenes"]
    fn official_scenes() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let s = stats(&lib, &crate::test_support::game_path("/UserData/Studio/scene/Official")).unwrap();
        println!("files={} parsed={} chars={} items={} (vanilla {} mod {}) lights={} folders={} cameras={} failed={}", s.files, s.parsed, s.characters, s.items, s.vanilla_items, s.mod_items, s.lights, s.folders, s.cameras, s.failed.len());
        let other: usize = s.other_motions.values().sum();
        println!("motions: {} from the OK animation library, {other} others ({} distinct): {:?}", s.library_motions, s.other_motions.len(), s.other_motions.iter().take(12).collect::<Vec<_>>());
        for (f, e) in &s.failed { println!("FAIL {f} {e}"); }
        // A few old scenes end early (the game cannot load them either); everything else parses.
        assert!(s.parsed * 1000 >= s.files * 999, "{} of {} parsed", s.parsed, s.files);
    }
}


/// Import step 1: parses a scene, writes every character's KKS card to `out_dir/char_<key>.kks.png`, and returns
/// the scene as JSON with each character's `card` path (offsets dropped).
pub fn export(scene_path: &std::path::Path, out_dir: &std::path::Path) -> Result<serde_json::Value, String> {
    let bytes = std::fs::read(scene_path).map_err(|e| format!("{}: {e}", scene_path.display()))?;
    let scene = read(&bytes)?;
    std::fs::create_dir_all(out_dir).map_err(|e| e.to_string())?;
    let mut cards = std::collections::HashMap::new();
    let mut errors = Vec::new();
    fn walk(bytes: &[u8], objects: &[Object], out_dir: &std::path::Path, cards: &mut std::collections::HashMap<i32, (String, serde_json::Value)>, errors: &mut Vec<String>) {
        for o in objects {
            match o {
                Object::Character { key, children, .. } => {
                    match card_bytes(bytes, o) {
                        Ok(card) => {
                            let path = out_dir.join(format!("char_{key}.kks.png"));
                            // The card's ChaFileStatus as the scene saved it (tears, blush, gloss, accessory slots shown).
                            let status = crate::card::parse_card_bytes(&card, 0).map(|i| serde_json::json!({
                                "tears_lv": i.tears_lv, "hoho_aka_rate": i.hoho_aka_rate, "skin_tuya_rate": i.skin_tuya_rate, "show_accessory": i.show_accessory,
                                "coordinate": i.status_coordinate, "clothes_state": i.status_clothes,
                                "eyebrow_ptn": i.status_eyebrow_ptn, "eyes_ptn": i.status_eyes_ptn, "mouth_ptn": i.status_mouth_ptn,
                                "eyes_look_ptn": i.status_eyes_look_ptn, "neck_look_ptn": i.status_neck_look_ptn,
                            })).unwrap_or(serde_json::Value::Null);
                            match std::fs::write(&path, card) { Ok(()) => { cards.insert(*key, (path.display().to_string(), status)); } Err(e) => errors.push(format!("character {key}: {e}")) }
                        }
                        Err(e) => errors.push(format!("character {key}: {e}")),
                    }
                    for (_, c) in children { walk(bytes, c, out_dir, cards, errors); }
                }
                Object::Item { children, .. } | Object::Folder { children, .. } | Object::Route { children, .. } => walk(bytes, children, out_dir, cards, errors),
                _ => {}
            }
        }
    }
    walk(&bytes, &scene.objects, out_dir, &mut cards, &mut errors);
    let mut json = serde_json::to_value(&scene).map_err(|e| e.to_string())?;
    let mods = scene_mod_items(&bytes);
    let vmd = scene_vmd(&bytes);
    let no_vmd = std::collections::HashMap::new();
    let vmd_chars = vmd.as_ref().map(|v| &v.1).unwrap_or(&no_vmd);
    fn mark(v: &mut serde_json::Value, cards: &std::collections::HashMap<i32, (String, serde_json::Value)>, mods: &std::collections::HashMap<i32, (String, i32)>, vmd: &std::collections::HashMap<i32, serde_json::Map<String, serde_json::Value>>) {
        match v {
            serde_json::Value::Object(m) => {
                // A mod item: its mod's GUID and slot in the mod's list (studio_mods resolves it).
                if m.get("kind").and_then(|k| k.as_str()) == Some("Item") {
                    let key = m.get("key").and_then(|k| k.as_i64()).unwrap_or(-1) as i32;
                    if let Some((guid, slot)) = mods.get(&key) { m.insert("mod_guid".into(), guid.clone().into()); m.insert("mod_slot".into(), (*slot).into()); }
                }
                if m.get("kind").and_then(|k| k.as_str()) == Some("Character") {
                    let key = m.get("key").and_then(|k| k.as_i64()).unwrap_or(-1) as i32;
                    if let Some((p, status)) = cards.get(&key) { m.insert("card".into(), p.clone().into()); m.insert("status".into(), status.clone()); }
                    // KKVMDPlay drives this character with a VMD motion (scene_vmd).
                    if let Some(d) = vmd.get(&key) { m.insert("vmd".into(), serde_json::Value::Object(d.clone())); }
                    for k in ["card_offset", "card_len", "face_png_offset", "face_png_len", "ext_offset", "ext_len"] { m.remove(k); }
                }
                for (_, x) in m.iter_mut() { mark(x, cards, mods, vmd); }
            }
            serde_json::Value::Array(a) => for x in a { mark(x, cards, mods, vmd); },
            _ => {}
        }
    }
    mark(&mut json, &cards, &mods, vmd_chars);
    if let Some((scene_vmd, _)) = &vmd { json["vmd_scene"] = serde_json::Value::Object(scene_vmd.clone()); }
    json["card_errors"] = errors.into();
    Ok(json)
}

#[cfg(test)]
mod import_tests {
    #[test]
    #[ignore = "installed game + OK base library: a scene character becomes an OK card that opens"]
    fn scene_character_to_ok_card() {
        let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let lib = manifest.join("../../Content/IndependentResources/base_v1");
        let game = &crate::test_support::game_path("");
        let out = std::env::temp_dir().join("ok_scene_import_test");
        let _ = std::fs::remove_dir_all(&out);
        // A scene with characters and a KKEx block (plugin data kept beside the card).
        let scene = game.join("UserData/Studio/scene/Official/koikatu_cs0000059.png");
        let json = super::export(&scene, &out).unwrap();
        let card = json["objects"].as_array().unwrap().iter().find_map(|o| o["card"].as_str().map(str::to_owned)).expect("a character card");
        assert!(json["card_errors"].as_array().unwrap().is_empty(), "{:?}", json["card_errors"]);
        let blocks = crate::appearance::blocks_from_card(&std::fs::read(&card).unwrap()).unwrap();
        assert!(blocks.contains_key("Custom") && blocks.contains_key("KKEx"), "blocks {:?}", blocks.keys().collect::<Vec<_>>());
        let (digest, parts, bytes, pack) = crate::oklib::make_ok_card(&lib, game, std::path::Path::new(&card)).unwrap();
        let ok = out.join("char.ok.png");
        std::fs::write(&ok, &bytes).unwrap();
        println!("card {card}: digest {digest} parts {parts} pack {pack} -> {} bytes", bytes.len());
        let session = crate::oklib::open_test_card(&ok, 0).unwrap();
        assert!(session.parts.len() > 10, "OK card opens with {} parts", session.parts.len());
    }
    /// Screen settings and Studio lights of a sample scene (OK_SCENE, default 633: the 1F toilet), as the import reads them.
    #[test]
    #[ignore = "OK base library: a sample scene's screen settings"]
    fn sample_scene_settings() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let name = std::env::var("OK_SCENE").unwrap_or_else(|_| "koikatu_cs0000633".into());
        let out = std::env::temp_dir().join("ok_scene_probe");
        let v = super::export(&lib.join("studio/samples").join(format!("{name}.png")), &out).unwrap();
        for k in ["effects", "camera", "map"] { if let Some(x) = v.get(k) { let s = x.to_string(); println!("{k}: {}", &s[..s.len().min(3000)]); } }
        if let Ok(path) = std::env::var("OK_SCENE_DUMP") { std::fs::write(path, serde_json::to_string_pretty(&v).unwrap()).unwrap(); }
    }
}

#[cfg(test)]
mod item_animation_tests {
    #[test]
    #[ignore = "base library sample scenes: source item clock fields"]
    fn rotating_parent_keeps_saved_phase() {
        let path=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1/studio/samples/koikatu_cs0001268.png");
        let scene=super::read(&std::fs::read(path).unwrap()).unwrap();
        let item=scene.objects.iter().find(|o|matches!(o,super::Object::Item{key:424,..})).unwrap();
        match item {super::Object::Item{anime_pattern,anime_time,anime_speed,children,..}=>{
            assert_eq!(*anime_pattern,0);
            assert!((*anime_time-311.40283203125).abs()<0.00001);
            assert!((*anime_speed-0.2197189331).abs()<0.000001);
            assert_eq!(children.len(),2);
        },_=>unreachable!()}
    }
}

#[cfg(test)]
mod fixed_neck_tests {
    #[test]
    fn saved_eyes_layout_and_validation() {
        let values = [0f32,0.,0.,2.,0.,0.,0.,1.,-12.,8.,6.,7.];
        let bytes: Vec<u8> = values.iter().flat_map(|v|v.to_le_bytes()).collect();
        let latest = super::Version::parse("1.1.0.0");
        let decoded = super::read_eyes(&bytes,&latest).unwrap().unwrap();
        assert_eq!(decoded,[0.,0.,0.,1.,0.,0.,0.,1.,-12.,8.,6.,7.]);
        assert_eq!(super::read_eyes(&bytes[..32],&super::Version::parse("0.0.7")).unwrap().unwrap()[8..],[0.;4]);
        assert!(super::read_eyes(&bytes[..47],&latest).is_err());
        assert!(super::read_eyes(&[],&latest).unwrap().is_none());
        let mut invalid=bytes.clone();invalid[8..12].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(super::read_eyes(&invalid,&latest).is_err());
        assert!(super::read_eyes(&[0;48],&latest).is_err());
    }
    #[test]
    #[ignore = "native scene FIX eyes oracle: requires the local game and NativeFixedEyes6331 probe"]
    fn saved_fixed_eyes_match_native_6331() {
        let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let scene=super::export(&root.join("Content/IndependentResources/base_v1/studio/samples/koikatu_cs0006331.png"),&root.join("Saved/OKStudio/GeometryInputs20261021/source-cards")).unwrap();
        fn find(v:&serde_json::Value)->Option<&serde_json::Value>{
            if v["key"].as_i64()==Some(555) && v["kind"].as_str()==Some("Character") {return Some(v);}
            match v {serde_json::Value::Array(a)=>a.iter().find_map(find),serde_json::Value::Object(m)=>m.values().find_map(find),_=>None}
        }
        let object=find(&scene).expect("source character 555");
        let values:Vec<f32>=object["eyes"].as_array().unwrap().iter().map(|v|v.as_f64().unwrap() as f32).collect();
        let mut session=crate::session::open(&crate::test_support::game_path(""),Some(std::path::Path::new(object["card"].as_str().unwrap())),0).unwrap();
        let native:serde_json::Value=serde_json::from_slice(&std::fs::read(root.join("Saved/Tests/NativeFixedEyes6331/Scene1.selected-mesh.json")).unwrap()).unwrap();
        let mut rates=[0f32;3];
        let pattern=object["status"]["eyes_look_ptn"].as_i64().unwrap() as i32;
        assert_eq!(pattern,3);
        let code=unsafe{crate::ffi::ok_session_set_studio_fixed_eyes(&mut session,values.as_ptr(),12,pattern,rates.as_mut_ptr())};
        assert_eq!(code,0);
        for i in 0..3 {assert!((rates[i]-native["eyeLookRates"][i].as_f64().unwrap() as f32).abs()<1e-5,"rate{i}: {} != {}",rates[i],native["eyeLookRates"][i]);}
        let before=session.eye_look_runtime.as_ref().unwrap().rates;
        assert_ne!(unsafe{crate::ffi::ok_session_set_studio_fixed_eyes(&mut session,values.as_ptr(),12,1,rates.as_mut_ptr())},0);
        assert_eq!(session.eye_look_runtime.as_ref().unwrap().rates,before,"target row rejection preserves FIX state");
        eprintln!("saved FIX eyes native match rates={before:?} source angles={:?}",&values[8..]);
    }
    fn data(pattern: i32, qs: &[[f32; 4]]) -> Vec<u8> {
        let mut b = Vec::new();
        b.extend(pattern.to_le_bytes());
        b.extend((qs.len() as i32).to_le_bytes());
        for q in qs {
            for v in q {
                b.extend(v.to_le_bytes());
            }
        }
        b
    }
    #[test]
    fn fixed_neck_payload_validation() {
        let b = data(4, &[[0., 0., 0., 1.], [0.1, 0., 0., 1.]]);
        let n = super::read_neck(&b).unwrap().unwrap();
        assert_eq!(n.pattern, 4);
        assert_eq!(n.rotations.len(), 2);
        assert!((n.rotations[1].iter().map(|v| v * v).sum::<f32>() - 1.).abs() < 1e-6);
        assert!(super::read_neck(&b[..b.len() - 1]).is_err());
        assert!(super::read_neck(&data(4, &[[0.; 4]])).is_err());
        assert!(super::read_neck(&data(4, &[[f32::NAN, 0., 0., 1.]])).is_err());
        assert!(super::read_neck(&data(3, &[[0.; 4]])).unwrap().is_none());
    }
    #[test]
    #[ignore = "base library scene: saved FIX neck of 6331"]
    fn scene_6331_fixed_neck() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(
            "../../Content/IndependentResources/base_v1/studio/samples/koikatu_cs0006331.png",
        );
        let scene = super::read(&std::fs::read(path).unwrap()).unwrap();
        fn find(objects: &[super::Object]) -> Option<&super::StudioNeck> {
            for o in objects {
                match o {
                    super::Object::Character {
                        key: 555,
                        neck: Some(n),
                        ..
                    } => return Some(n),
                    super::Object::Folder { children, .. }
                    | super::Object::Item { children, .. }
                    | super::Object::Route { children, .. } => {
                        if let Some(n) = find(children) {
                            return Some(n);
                        }
                    }
                    _ => {}
                }
            }
            None
        }
        let neck = find(&scene.objects).unwrap();
        assert_eq!(neck.pattern, 4);
        assert_eq!(neck.rotations.len(), 2);
        println!("6331 source FIX locals {:?}", neck.rotations);
        assert!(neck
            .rotations
            .iter()
            .any(|q| q[0].abs() + q[1].abs() + q[2].abs() > 0.1));
    }
}

#[cfg(test)]
#[test]
#[ignore = "base library: 1268 source outfit conversion and closed reopen"]
fn scene_1268_clothing_source() {
    let lib = std::env::var_os("OK_BASE_LIBRARY").map(std::path::PathBuf::from).unwrap_or_else(||
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1"));
    let samples = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1/studio/samples");
    let bytes = std::fs::read(samples.join("koikatu_cs0001268.png")).unwrap();
    let scene = read(&bytes).unwrap();
    fn collect<'a>(objects: &'a [Object], out: &mut Vec<&'a Object>) {
        for o in objects {
            match o {
                Object::Character {key, children, ..} => {
                    if *key == 136 || *key == 280 { out.push(o); }
                    for (_, c) in children { collect(c, out); }
                }
                Object::Item{children,..}|Object::Folder{children,..}|Object::Route{children,..} => collect(children, out),
                _ => {}
            }
        }
    }
    let mut angels=Vec::new(); collect(&scene.objects, &mut angels); assert_eq!(angels.len(),2);
    let source=card_bytes(&bytes,angels[0]).unwrap();
    assert_eq!(source,card_bytes(&bytes,angels[1]).unwrap());
    assert_eq!(crate::okclothes::shown_outfit(&source).unwrap(),1,"display heuristic would select the uniform");
    let source_blocks=crate::appearance::blocks_from_card(&source).unwrap();
    let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Saved/OKStudio/1268-clothes-oracle");
    std::fs::create_dir_all(&root).unwrap(); let path=root.join("source.png");std::fs::write(&path,&source).unwrap();
    assert!(crate::oklib::remake_ok_card_for_outfit(&lib,std::path::Path::new(""),&path,-1).is_err());
    assert!(crate::oklib::remake_ok_card_for_outfit(&lib,std::path::Path::new(""),&path,7).is_err());
    for outfit in [0,1] {
        let info=crate::card::parse_card_bytes_mode(&source,outfit,false).unwrap();
        assert_eq!(info.clothes_ids[0],if outfit==0 {28} else {2});
        crate::oklib::ensure_library(&lib).unwrap();
        crate::session::open(&lib,Some(&path),outfit).unwrap(); // warm the original-card cache first
        let (_,_,card,_)=crate::oklib::remake_ok_card_for_outfit(&lib,std::path::Path::new(""),&path,outfit).unwrap();
        let payload:serde_json::Value=serde_json::from_slice(crate::oklib::png_chunk(&card,b"okLb").unwrap()).unwrap();
        assert!(!payload["models"].as_array().unwrap().is_empty(),"pack recording must visit dependencies even after a warm open");
        assert_eq!(crate::appearance::blocks_from_card(&card).unwrap(),source_blocks,"all source coordinates/status/plugins retain their exact bytes");
        let converted=root.join(format!("outfit{outfit}.png"));std::fs::write(&converted,&card).unwrap();
        let session=crate::session::open(&lib,Some(&converted),outfit).unwrap();
        let top:Vec<_>=session.parts.iter().filter(|p| p.role=="clothes_top"&&p.render_visible).map(|p|p.name.as_str()).collect();
        eprintln!("CLOTHING closed outfit={outfit} top={top:?} game_reads={}",crate::gamefs::fallback_used());
        assert!(!top.is_empty());
        if outfit==0 {assert!(top.iter().all(|n|n.contains("bathtowel")));}
        else {assert!(top.iter().any(|n|n.contains("jacket")));}
        assert!(!crate::gamefs::fallback_used());
        let reopened=crate::oklib::remake_ok_card_for_outfit(&lib,std::path::Path::new(""),&converted,outfit).unwrap().2;
        assert_eq!(crate::appearance::blocks_from_card(&reopened).unwrap(),source_blocks,"reconversion must not exchange coordinates");
    }
    assert_eq!(std::fs::read(path).unwrap(),source,"conversion never edits the source file");
}
