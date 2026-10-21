//! Studio's sky (SceneInfo.skyInfo: Enable, Pattern). With the sky on and a map without its own sky (MapSkyInfo),
//! Studio clears the camera to the skybox and loads the pattern's Funly Sky Studio profile (the StudioSky list:
//! no, name, rotation, bundle, profile asset, manifest) into its TimeOfDayController
//! (SystemButtonCtrl.SkyInfo.SetPattern). The profile draws through its skybox material
//! (`Funly/Sky Studio/Skybox/3D Standard`); the KKS profiles are single keyframes, and the material keeps the values
//! the controller applies (colours, sizes, positions), so the library keeps each pattern's material: keywords,
//! values, textures, plus the moon / sun matrices the controller sets at run time (their orbit's `Position`
//! transform, Studio scene `SkySystemController/Moon|Sun/Position`).
//!
//! `--oklib studio-sky <lib>` writes `studio/sky.json` and the textures under `studio/sky/`; the shader programs are
//! translated offline (Tools/generate_studio_sky.lua, OKStudio StudioSkyPrograms.inl).

use std::collections::BTreeMap;
use std::path::Path;

use serde::{Deserialize, Serialize};
use unity_asset_binary::object::UnityObject;
use unity_asset_core::UnityValue;

pub const FILE_NAME: &str = "studio/sky.json";
pub const ENVIRONMENT_FILE_NAME: &str = "studio/map_environments.json";

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MapEnvironment {
    pub map: i32,
    pub model: String,
    pub ambient_mode: i32,
    pub ambient_intensity: f32,
    /// Unity's baked RenderSettings.m_AmbientProbe, in RGB groups of nine.
    /// These are already the coefficients the renderer receives; do not multiply intensity again.
    pub probe: [[f32; 9]; 3],
}

/// Export map scene lighting independently of character-maker preferences.
/// No mesh/model memo or bincode structure is changed by this converter.
pub fn convert_map_environments(game: &Path, lib: &Path) -> Result<usize, String> {
    let catalog: crate::studio_items::Catalog = serde_json::from_slice(
        &std::fs::read(lib.join(crate::studio_items::FILE_NAME)).map_err(|e|e.to_string())?
    ).map_err(|e|e.to_string())?;
    let mut maps = Vec::new();
    for map in &catalog.maps {
        let bundle = map.bundle.strip_prefix("abdata/").unwrap_or(&map.bundle);
        let Some(src) = Sources::open(game, bundle) else { return Err(format!("map {}: source bundle missing",map.no)); };
        let mut environments = Vec::new();
        src.bundles[0].with_serialized(|files| for file in files {
            for info in file.objects_of_type(104) {
                let Ok(object) = UnityObject::from_serialized_file(file, &info) else { continue; };
                let p = object.class.properties();
                let Some(UnityValue::Object(coefficients)) = p.get("m_AmbientProbe") else { continue; };
                let mut probe = [[0.;9];3];
                let mut seen = [false;27];
                for (key,value) in coefficients {
                    let Some(index) = key.strip_prefix("sh[").and_then(|s|s.strip_suffix(']')).and_then(|s|s.trim().parse::<usize>().ok()).filter(|i|*i<27) else {continue;};
                    let Some(value) = value.as_f64().filter(|v|v.is_finite()) else {continue;};
                    probe[index/9][index%9] = value as f32;
                    seen[index] = true;
                }
                if seen.iter().all(|v|*v) {
                    environments.push(MapEnvironment {map:map.no,model:map.model.clone(),probe,
                        ambient_mode:p.get("m_AmbientMode").and_then(|v|v.as_i64()).unwrap_or(0) as i32,
                        ambient_intensity:p.get("m_AmbientIntensity").and_then(|v|v.as_f64()).unwrap_or(1.) as f32});
                }
            }
        });
        if environments.len()>1 { return Err(format!("map {}: ambiguous scene RenderSettings",map.no)); }
        if let Some(environment) = environments.pop() { maps.push(environment); }
    }
    let count = maps.len();
    let document = serde_json::json!({"format":"ok-studio-map-environments-1","maps":maps});
    std::fs::write(lib.join(ENVIRONMENT_FILE_NAME),serde_json::to_vec_pretty(&document).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
    Ok(count)
}
pub const TEXTURE_DIR: &str = "studio/sky";

/// A texture of a sky material: the PNG (relative to the library), its `_ST`, and how Unity samples it.
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct SkyTexture {
    pub file: String,
    pub st: [f32; 4],
    /// Imported as sRGB (m_ColorSpace 1): sampled with the sRGB decode in Studio's linear colour space.
    pub srgb: bool,
    /// Point filtering (m_FilterMode 0), clamped addressing (m_WrapU 1).
    pub point: bool,
    pub clamp: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct SkyPattern {
    pub no: i32,
    pub name: String,
    /// Profile and skybox material asset names (diagnostics).
    pub profile: String,
    pub material: String,
    /// The shader keywords the controller turns on from the profile's features (TimeOfDayController
    /// .SynchronizeAllShaderKeywords): they pick the program variant. `material_keywords`: as the material was saved.
    pub keywords: Vec<String>,
    pub material_keywords: Vec<String>,
    pub floats: BTreeMap<String, f32>,
    /// m_Colors (colour and vector properties), as stored (sRGB for colours).
    pub colors: BTreeMap<String, [f32; 4]>,
    pub textures: BTreeMap<String, SkyTexture>,
    /// Matrices the controller sets (column-major, as Unity uploads them): `_MoonWorldToLocalMat`,
    /// `_SunWorldToLocalMat`.
    pub matrices: BTreeMap<String, [f32; 16]>,
}

/// A map's own sky (MapSkyInfo on its `Map` root, with a TimeOfDayController): one entry per time of day
/// (SunLightInfo.Info.Type: 0 morning, 1 day, 2 evening, 3 night), each a pattern number of `patterns` (-1: the
/// controller keeps its profile) and the GameObjects it shows / hides (Info._actives, _inactives, by name).
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct MapSkyEntry {
    pub pattern: i32,
    pub actives: Vec<String>,
    pub inactives: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct MapSky {
    /// The map's number (studio Map list).
    pub map: i32,
    /// The times of day its SunLightInfo has (Map.RangeTimeZone picks the scene's when present, else the first).
    pub sun_types: Vec<i32>,
    pub skies: Vec<MapSkyEntry>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct SkyCatalog {
    pub format: String,
    pub patterns: Vec<SkyPattern>,
    /// Maps with their own sky: Studio then always clears to the skybox and ignores the scene's sky setting
    /// (Map.Reserve, SystemButtonCtrl.SkyInfo.Apply).
    #[serde(default)]
    pub maps: Vec<MapSky>,
}

/// Map skies' patterns are numbered from here (the StudioSky list uses small numbers).
pub const MAP_PATTERN_BASE: i32 = 1000;

/// Which sky profile: by asset name (StudioSky list) or by path id (a map's MapSkyInfo).
enum Profile<'a> { Named(&'a str), At(i64) }

fn obj<'a>(v: &'a UnityValue, k: &str) -> Option<&'a UnityValue> { v.as_object().and_then(|o| o.get(k)) }
fn f32_of(v: &UnityValue, k: &str, d: f32) -> f32 { obj(v, k).and_then(|x| x.as_f64()).map(|x| x as f32).unwrap_or(d) }
/// (FileID, PathID) of a PPtr value.
fn pptr(v: Option<&UnityValue>) -> Option<(i64, i64)> {
    let v = v?;
    Some((obj(v, "m_FileID").and_then(|x| x.as_i64()).unwrap_or(0), obj(v, "m_PathID").and_then(|x| x.as_i64())?))
}
/// Pairs of a serialized map (`[key, value]` arrays or `{first, second}` objects).
fn pairs(v: Option<&UnityValue>) -> Vec<(String, UnityValue)> {
    let Some(UnityValue::Array(a)) = v else { return Vec::new() };
    a.iter().filter_map(|e| match e {
        UnityValue::Array(p) if p.len() == 2 => Some((p[0].as_str()?.to_owned(), p[1].clone())),
        UnityValue::Object(o) => Some((o.get("first")?.as_str()?.to_owned(), o.get("second")?.clone())),
        _ => None,
    }).collect()
}

/// The parsed bundles a sky profile draws from: its own and its AssetBundle dependencies (path ids are unique
/// across them, so a PPtr into another file is found by its path id).
struct Sources { bundles: Vec<std::sync::Arc<crate::unity::ParsedBundle>> }

impl Sources {
    fn open(game: &Path, bundle: &str) -> Option<Self> {
        let abdata = game.join("abdata");
        let read = |b: &str| crate::gamefs::read(abdata.join(b)).ok().map(crate::unity::parse_bundle_cached);
        let first = read(bundle)?;
        let mut deps = Vec::new();
        first.with_serialized(|files| for f in files {
            for info in f.objects_of_type(142) {
                let Ok(o) = UnityObject::from_serialized_file(f, &info) else { continue };
                if let Some(UnityValue::Array(a)) = o.class.properties().get("m_Dependencies") {
                    deps.extend(a.iter().filter_map(|d| d.as_str().map(str::to_owned)));
                }
            }
        });
        let mut bundles = vec![first];
        bundles.extend(deps.iter().filter_map(|d| read(d)));
        Some(Self { bundles })
    }
    /// Adds the bundles of `dirs` (relative to abdata) not opened yet: a scene bundle's AssetBundle object does not
    /// list every bundle its scene draws from (the map skies' profiles live in studio/map/materials and studio/sky).
    fn with_dirs(mut self, game: &Path, dirs: &[&str]) -> Self {
        for dir in dirs {
            let Ok(entries) = crate::gamefs::read_dir(&game.join("abdata").join(dir)) else { continue };
            let mut files: Vec<_> = entries.filter_map(|e| e.ok()).map(|e| e.path())
                .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("unity3d"))).collect();
            files.sort();
            self.bundles.extend(files.iter().filter_map(|p| crate::gamefs::read(p).ok().map(crate::unity::parse_bundle_cached)));
        }
        self
    }
    /// Calls `f` with the object of `class` named `name` (first match).
    fn named<R>(&self, class: i32, name: &str, f: impl Fn(&UnityObject) -> R) -> Option<R> {
        self.bundles.iter().find_map(|b| b.with_serialized(|files| files.iter().find_map(|s| s.objects_of_type(class).into_iter().find_map(|i| {
            let o = UnityObject::from_serialized_file(s, &i).ok()?;
            (o.name().as_deref() == Some(name)).then(|| f(&o))
        }))))
    }
    /// Calls `f` with the object at `path_id` (and the files of its bundle, for streamed data).
    fn at<R>(&self, class: i32, path_id: i64, f: impl Fn(&UnityObject, &[(String, crate::unity::BundleFileBytes)]) -> R) -> Option<R> {
        self.bundles.iter().find_map(|b| b.with_serialized(|files| files.iter().find_map(|s| s.objects_of_type(class).into_iter()
            .find(|i| i.path_id == path_id)
            .and_then(|i| UnityObject::from_serialized_file(s, &i).ok())
            .map(|o| f(&o, &b.files)))))
    }
}

/// Unity's Quaternion.LookRotation(forward, up) as a rotation matrix's columns (right, up, forward).
fn look_rotation(f: [f32; 3]) -> [[f32; 3]; 3] {
    let n = |v: [f32; 3]| { let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt().max(1e-12); [v[0] / l, v[1] / l, v[2] / l] };
    let cross = |a: [f32; 3], b: [f32; 3]| [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    let f = n(f);
    let r = n(cross([0., 1., 0.], f));
    [r, cross(f, r), f]
}

/// worldToLocalMatrix of an orbit's `Position` (column-major): the orbit faces away from its body
/// (OrbitingBody.LayoutOribit: forward = -direction), and Position sits 10000 behind it, turned 180° about Y.
pub fn orbit_world_to_local(direction: [f32; 3]) -> [f32; 16] {
    let [r, u, f] = look_rotation([-direction[0], -direction[1], -direction[2]]);
    // World = R · T(0,0,-10000) · Ry(180); its inverse = Ry(180)ᵀ · T(0,0,10000) · Rᵀ. Rows of Rᵀ are r, u, f.
    let rows = [r, u, f];
    let local = |k: usize, w: [f32; 3]| rows[k][0] * w[0] + rows[k][1] * w[1] + rows[k][2] * w[2];
    // A point p: q = Rᵀp + (0,0,10000); then Ry(180)ᵀ: (x, y, z) → (-x, y, -z).
    let mut m = [0f32; 16];
    for (c, axis) in [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]].iter().enumerate() {
        m[c * 4] = -local(0, *axis);
        m[c * 4 + 1] = local(1, *axis);
        m[c * 4 + 2] = -local(2, *axis);
    }
    m[12] = 0.;
    m[13] = 0.;
    m[14] = -10000.;
    m[15] = 1.;
    m
}

/// Standard3dShaderDefinition: feature → shader keyword, and the feature (with the value) it depends on.
const FEATURES: &[(&str, &str, Option<(&str, bool)>)] = &[
    ("GradientSkyFeature", "GRADIENT_BACKGROUND", None), ("SunFeature", "SUN", None), ("MoonFeature", "MOON", None),
    ("CloudFeature", "CLOUDS", None), ("FogFeature", "HORIZON_FOG", None),
    ("StarBasicFeature", "STARS_BASIC", Some(("MobileQualityFeature", true))),
    ("StarLayer1Feature", "STAR_LAYER_1", Some(("MobileQualityFeature", false))),
    ("StarLayer2Feature", "STAR_LAYER_2", Some(("MobileQualityFeature", false))),
    ("StarLayer3Feature", "STAR_LAYER_3", Some(("MobileQualityFeature", false))),
    ("VertexGradientSkyFeature", "VERTEX_GRADIENT_BACKGROUND", None),
    ("SunCustomTextureFeature", "SUN_CUSTOM_TEXTURE", None),
    ("SunSpriteSheetFeature", "SUN_SPRITE_SHEET", Some(("SunCustomTextureFeature", true))),
    ("SunAlphaBlendFeature", "SUN_ALPHA_BLEND", Some(("SunCustomTextureFeature", true))),
    ("SunRotationFeature", "SUN_ROTATION", Some(("SunCustomTextureFeature", true))),
    ("MoonCustomTextureFeature", "MOON_CUSTOM_TEXTURE", None),
    ("MoonSpriteSheetFeature", "MOON_SPRITE_SHEET", Some(("MoonCustomTextureFeature", true))),
    ("MoonAlphaBlendFeature", "MOON_ALPHA_BLEND", Some(("MoonCustomTextureFeature", true))),
    ("MoonRotationFeature", "MOON_ROTATION", Some(("MoonCustomTextureFeature", true))),
    ("StarLayer1CustomTextureFeature", "STAR_LAYER_1_CUSTOM_TEXTURE", None),
    ("StarLayer1SpriteSheetFeature", "STAR_LAYER_1_SPRITE_SHEET", Some(("StarLayer1CustomTextureFeature", true))),
    ("StarLayer2CustomTextureFeature", "STAR_LAYER_2_CUSTOM_TEXTURE", None),
    ("StarLayer2SpriteSheetFeature", "STAR_LAYER_2_SPRITE_SHEET", Some(("StarLayer2CustomTextureFeature", true))),
    ("StarLayer3CustomTextureFeature", "STAR_LAYER_3_CUSTOM_TEXTURE", None),
    ("StarLayer3SpriteSheetFeature", "STAR_LAYER_3_SPRITE_SHEET", Some(("StarLayer3CustomTextureFeature", true))),
    ("NoiseCloudFeature", "NOISE_CLOUDS", Some(("CloudFeature", true))),
    ("CubemapCloudFeature", "CUBEMAP_CLOUDS", Some(("CloudFeature", true))),
    ("CubemapNormalCloudFeature", "CUBEMAP_NORMAL_CLOUDS", Some(("CloudFeature", true))),
    ("CubemapCloudTextureFormatRGBFeature", "CUBEMAP_CLOUD_FORMAT_RGB", Some(("CubemapCloudFeature", true))),
    ("CubemapCloudTextureFormatRGBAFeature", "CUBEMAP_CLOUD_FORMAT_RGBA", Some(("CubemapCloudFeature", true))),
    ("CubemapCloudDoubleLayerFeature", "CUBEMAP_CLOUD_DOUBLE_LAYER", Some(("CubemapCloudFeature", true))),
    ("CubemapCloudDoubleLayerCubemap", "CUBEMAP_CLOUD_DOUBLE_LAYER_CUSTOM_TEXTURE", Some(("CubemapCloudDoubleLayerFeature", true))),
    ("CubemapNormalCloudDoubleLayerFeature", "CUBEMAP_NORMAL_CLOUD_DOUBLE_LAYER", Some(("CubemapNormalCloudFeature", true))),
];

/// SkyProfile.IsFeatureEnabled (recursive): on, and every feature it depends on has the value it asks for.
fn feature_enabled(status: &BTreeMap<String, bool>, key: &str) -> bool {
    if !status.get(key).copied().unwrap_or(false) { return false; }
    let mut at = key;
    while let Some((_, _, Some((dep, want)))) = FEATURES.iter().find(|f| f.0 == at) {
        if status.get(*dep).copied().unwrap_or(false) != *want { return false; }
        at = dep;
    }
    true
}

/// The keywords a profile's features turn on (sorted).
pub fn feature_keywords(status: &BTreeMap<String, bool>) -> Vec<String> {
    let mut out: Vec<String> = FEATURES.iter().filter(|f| feature_enabled(status, f.0)).map(|f| f.1.to_owned()).collect();
    out.sort();
    out
}

/// A profile's featureStatus (keys and their bools).
fn feature_status(profile: &UnityValue) -> BTreeMap<String, bool> {
    let Some(fs) = obj(profile, "featureStatus") else { return BTreeMap::new() };
    let keys: Vec<String> = match obj(fs, "m_Keys") { Some(UnityValue::Array(a)) => a.iter().filter_map(|k| k.as_str().map(str::to_owned)).collect(), _ => Vec::new() };
    let values: Vec<bool> = match obj(fs, "m_Values") {
        Some(UnityValue::Bytes(b)) => b.iter().map(|x| *x != 0).collect(),
        Some(UnityValue::Array(a)) => a.iter().map(|x| x.as_bool().unwrap_or_else(|| x.as_i64().unwrap_or(0) != 0)).collect(),
        _ => Vec::new(),
    };
    keys.into_iter().zip(values).collect()
}

/// The sky patterns of the game's StudioSky lists (later bundles and sheets override earlier rows, as in the game).
pub fn patterns(game: &Path) -> Vec<(i32, String, String, String)> {
    let dir = game.join("abdata/studio/info");
    let mut files: Vec<_> = crate::gamefs::read_dir(&dir).map(|it| it.filter_map(|e| e.ok()).map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("unity3d"))).collect()).unwrap_or_default();
    files.sort();
    let mut rows = BTreeMap::new();
    for file in files {
        let Ok(bytes) = crate::gamefs::read(&file) else { continue };
        for (name, sheet) in crate::studio_items::sheets(bytes) {
            if !name.starts_with("StudioSky") { continue; }
            for r in &sheet {
                let Some(no) = r.first().and_then(|c| c.trim().parse::<i32>().ok()) else { continue };
                let cell = |i: usize| r.get(i).map(|s| s.trim().to_owned()).unwrap_or_default();
                rows.insert(no, (no, cell(1), cell(5).replace('\\', "/").to_ascii_lowercase(), cell(6)));
            }
        }
    }
    rows.into_values().filter(|r| !r.2.is_empty() && !r.3.is_empty()).collect()
}

/// One pattern's sky material, with its textures written under `lib/studio/sky`.
fn pattern(src: &Sources, lib: &Path, no: i32, name: &str, which: Profile) -> Result<SkyPattern, String> {
    let read = |o: &UnityObject| {
        let p = UnityValue::Object(o.class.properties().clone());
        (o.name().unwrap_or_default(), pptr(obj(&p, "m_SkyboxMaterial")), feature_status(&p))
    };
    let (profile, material, status) = match which {
        Profile::Named(n) => src.named(114, n, read),
        Profile::At(id) => src.at(114, id, |o, _| read(o)),
    }.ok_or_else(|| format!("{name}: sky profile not found"))?;
    let profile = profile.as_str();
    let material = material.ok_or_else(|| format!("{profile}: no skybox material"))?;
    let mut out = SkyPattern { no, name: name.to_owned(), profile: profile.to_owned(), keywords: feature_keywords(&status), ..Default::default() };
    let saved = src.at(21, material.1, |o, _| {
        let p = o.class.properties();
        (o.name().unwrap_or_default(), p.get("m_ShaderKeywords").and_then(|v| v.as_str()).unwrap_or_default().to_owned(), p.get("m_SavedProperties").cloned())
    }).ok_or_else(|| format!("{profile}: skybox material {} not found", material.1))?;
    out.material = saved.0;
    out.material_keywords = saved.1.split_whitespace().map(str::to_owned).collect();
    out.material_keywords.sort();
    let saved = saved.2.unwrap_or(UnityValue::Null);
    for (k, v) in pairs(obj(&saved, "m_Floats")) { if let Some(x) = v.as_f64() { out.floats.insert(k, x as f32); } }
    for (k, v) in pairs(obj(&saved, "m_Colors")) {
        out.colors.insert(k, [f32_of(&v, "r", 0.), f32_of(&v, "g", 0.), f32_of(&v, "b", 0.), f32_of(&v, "a", 1.)]);
    }
    let dir = lib.join(TEXTURE_DIR);
    std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    for (k, v) in pairs(obj(&saved, "m_TexEnvs")) {
        let Some((_, id)) = pptr(obj(&v, "m_Texture")).filter(|p| p.1 != 0) else { continue };
        let st = [obj(&v, "m_Scale").map(|s| f32_of(s, "x", 1.)).unwrap_or(1.), obj(&v, "m_Scale").map(|s| f32_of(s, "y", 1.)).unwrap_or(1.),
            obj(&v, "m_Offset").map(|s| f32_of(s, "x", 0.)).unwrap_or(0.), obj(&v, "m_Offset").map(|s| f32_of(s, "y", 0.)).unwrap_or(0.)];
        let found = src.at(28, id, |o, files| {
            let p = o.class.properties();
            let settings = p.get("m_TextureSettings").cloned().unwrap_or(UnityValue::Null);
            let int = |v: &UnityValue, k: &str| obj(v, k).and_then(|x| x.as_i64()).unwrap_or(0);
            (o.name().unwrap_or_default(), crate::unity::texture_object_png(o, files),
                p.get("m_ColorSpace").and_then(|x| x.as_i64()).unwrap_or(1) == 1, int(&settings, "m_FilterMode") == 0, int(&settings, "m_WrapU") == 1)
        });
        let Some((tex_name, Some(png), srgb, point, clamp)) = found else { return Err(format!("{profile}: texture {k} ({id}) not decoded")) };
        // Named by the texture and its path id: the star data textures of different profiles share names.
        let file = format!("{TEXTURE_DIR}/{}_{:016x}.png", tex_name.replace(|c: char| !c.is_ascii_alphanumeric() && c != '-' && c != '_', "_"), id as u64);
        let target = lib.join(&file);
        if !std::fs::read(&target).is_ok_and(|old| old == png) { std::fs::write(&target, &png).map_err(|e| format!("{}: {e}", target.display()))?; }
        out.textures.insert(k, SkyTexture { file, st, srgb, point, clamp });
    }
    for (body, matrix) in [("_MoonPosition", "_MoonWorldToLocalMat"), ("_SunPosition", "_SunWorldToLocalMat")] {
        if let Some(d) = out.colors.get(body) { out.matrices.insert(matrix.to_owned(), orbit_world_to_local([d[0], d[1], d[2]])); }
    }
    Ok(out)
}

/// The skies of the game's Studio maps (their MapSkyInfo and SunLightInfo), adding their profiles to `patterns`.
fn map_skies(game: &Path, lib: &Path, patterns: &mut Vec<SkyPattern>) -> Result<Vec<MapSky>, String> {
    let catalog = crate::studio_items::catalog(game);
    let mut out = Vec::new();
    let mut by_profile: BTreeMap<i64, i32> = BTreeMap::new();
    for map in &catalog.maps {
        let bundle = map.bundle.strip_prefix("abdata/").unwrap_or(&map.bundle).to_owned();
        let Some(src) = Sources::open(game, &bundle) else { continue };
        // The map bundle's own MonoBehaviours with _infos: MapSkyInfo has a _controller, SunLightInfo not.
        let mut skies: Vec<Vec<(Option<(i64, i64)>, Vec<String>, Vec<String>)>> = Vec::new();
        let mut suns: Vec<Vec<i32>> = Vec::new();
        src.bundles[0].with_serialized(|files| for f in files {
            let names: BTreeMap<i64, String> = f.objects_of_type(1).into_iter()
                .filter_map(|i| UnityObject::from_serialized_file(f, &i).ok().and_then(|o| o.name()).map(|n| (i.path_id, n))).collect();
            for info in f.objects_of_type(114) {
                let Ok(o) = UnityObject::from_serialized_file(f, &info) else { continue };
                let p = UnityValue::Object(o.class.properties().clone());
                let Some(UnityValue::Array(infos)) = obj(&p, "_infos") else { continue };
                if obj(&p, "_controller").is_some() {
                    skies.push(infos.iter().map(|i| {
                        let list = |k: &str| -> Vec<String> { match obj(i, k) {
                            Some(UnityValue::Array(a)) => a.iter().filter_map(|g| pptr(Some(g)).and_then(|(_, id)| names.get(&id).cloned())).collect(),
                            _ => Vec::new(),
                        } };
                        (pptr(obj(i, "_profile")).filter(|p| p.1 != 0), list("_actives"), list("_inactives"))
                    }).collect());
                } else {
                    suns.push(infos.iter().filter_map(|i| obj(i, "type").and_then(|t| t.as_i64())).map(|t| t as i32).collect());
                }
            }
        });
        if skies.len() != 1 {
            if skies.len() > 1 { eprintln!("studio sky: map {} ({bundle}) has {} MapSkyInfo, skipped", map.no, skies.len()); }
            continue;
        }
        // Its profiles: in the map bundle, its dependencies, or the shared sky / map material bundles.
        let full = Sources::open(game, &bundle).map(|s| s.with_dirs(game, &["studio/map/materials", "studio/sky"])).ok_or_else(|| format!("{bundle}: not readable"))?;
        let mut entries = Vec::new();
        for (profile, actives, inactives) in skies.remove(0) {
            let pattern_no = match profile {
                Some((_, id)) => match by_profile.get(&id) {
                    Some(no) => *no,
                    None => {
                        let no = MAP_PATTERN_BASE + patterns.len() as i32;
                        let p = pattern(&full, lib, no, &format!("map {} {}", map.no, map.name), Profile::At(id))?;
                        patterns.push(p);
                        by_profile.insert(id, no);
                        no
                    }
                },
                None => -1,
            };
            entries.push(MapSkyEntry { pattern: pattern_no, actives, inactives });
        }
        out.push(MapSky { map: map.no, sun_types: suns.into_iter().next().unwrap_or_default(), skies: entries });
    }
    Ok(out)
}

/// Writes `studio/sky.json` and the sky textures into the library; returns the pattern count.
pub fn convert(game: &Path, lib: &Path) -> Result<usize, String> {
    let mut catalog = SkyCatalog { format: "ok-studio-sky-1".into(), patterns: Vec::new(), maps: Vec::new() };
    for (no, name, bundle, profile) in patterns(game) {
        let src = Sources::open(game, &bundle).ok_or_else(|| format!("{bundle}: not readable"))?;
        catalog.patterns.push(pattern(&src, lib, no, &name, Profile::Named(&profile))?);
    }
    catalog.maps = map_skies(game, lib, &mut catalog.patterns)?;
    let target = lib.join(FILE_NAME);
    let json = serde_json::to_string_pretty(&catalog).map_err(|e| e.to_string())?;
    std::fs::write(&target, json).map_err(|e| format!("{}: {e}", target.display()))?;
    Ok(catalog.patterns.len())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[ignore = "native Studio ambient probe: OK_STUDIO_MAP and OK_STUDIO_NATIVE_SCENE_TSV"]
    fn map_baked_ambient_matches_native_probe() {
        let map: i32 = std::env::var("OK_STUDIO_MAP").unwrap().parse().unwrap();
        let native = std::fs::read_to_string(std::env::var("OK_STUDIO_NATIVE_SCENE_TSV").unwrap()).unwrap();
        let file = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1").join(ENVIRONMENT_FILE_NAME);
        let catalog: serde_json::Value = serde_json::from_slice(&std::fs::read(file).unwrap()).unwrap();
        let entry = catalog["maps"].as_array().unwrap().iter().find(|m|m["map"].as_i64()==Some(i64::from(map))).unwrap();
        let mut checked = [[false;9];3];
        let mut maximum = 0f64;
        for line in native.lines() {
            let fields: Vec<_> = line.split('\t').collect();
            if fields.first()!=Some(&"ambient_sh") {continue;}
            let c:usize=fields[1].parse().unwrap();
            let k:usize=fields[2].parse().unwrap();
            assert!(c<3 && k<9);
            let expected:f64=fields[3].parse().unwrap();
            let difference=(entry["probe"][c][k].as_f64().unwrap()-expected).abs();
            maximum=maximum.max(difference);
            checked[c][k]=true;
        }
        assert!(checked.into_iter().flatten().all(|v|v));
        eprintln!("native map {map} 27 ambient coefficients maximum error {maximum}");
        assert!(maximum<0.000001);
    }

    /// A body straight up (0,1,0): the orbit faces down, Position sits at the body; a direction towards it maps to
    /// local (0,0,~0) after the 10000 shift, and the matrix is a rotation (orthonormal 3x3) with that translation.
    #[test]
    fn orbit_matrix_puts_the_body_on_local_z() {
        let d = [0.3f32, 0.8, -0.52];
        let l = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt();
        let d = [d[0] / l, d[1] / l, d[2] / l];
        let m = orbit_world_to_local(d);
        let apply = |p: [f32; 3]| [0, 1, 2].map(|r| m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]);
        // The body's world position (10000 along its direction) is the Position's origin.
        let at = apply([d[0] * 10000., d[1] * 10000., d[2] * 10000.]);
        assert!(at.iter().all(|v| v.abs() < 0.05), "{at:?}");
        for c in 0..3 { let n: f32 = (0..3).map(|r| m[c * 4 + r] * m[c * 4 + r]).sum(); assert!((n - 1.).abs() < 1e-4); }
    }

    /// The installed game's StudioSky patterns convert (four Funly profiles: morning, day, evening, night).
    #[test]
    #[ignore = "installed game: converts the Studio sky patterns into a scratch library"]
    fn studio_sky_patterns_convert() {
        let game = &crate::test_support::game_path("");
        let lib = std::env::temp_dir().join("ok_studio_sky_test");
        let n = convert(game, &lib).unwrap();
        let c: SkyCatalog = serde_json::from_slice(&std::fs::read(lib.join(FILE_NAME)).unwrap()).unwrap();
        for p in &c.patterns {
            println!("{} {} {} {} keywords={:?} material keywords={:?} floats={} colors={} textures={:?}", p.no, p.name, p.profile, p.material, p.keywords, p.material_keywords, p.floats.len(), p.colors.len(),
                p.textures.iter().map(|(k, t)| format!("{k}={} srgb={} point={} clamp={}", t.file, t.srgb, t.point, t.clamp)).collect::<Vec<_>>());
        }
        for m in &c.maps {
            println!("map {} sun types {:?} skies {:?}", m.map, m.sun_types, m.skies.iter().map(|s| (s.pattern, s.actives.len(), s.inactives.len())).collect::<Vec<_>>());
        }
        assert!(n >= 4);
        let list: Vec<_> = c.patterns.iter().filter(|p| p.no < MAP_PATTERN_BASE).collect();
        assert!(list.len() >= 4 && list.iter().all(|p| p.keywords.iter().any(|k| k == "GRADIENT_BACKGROUND") && p.textures.contains_key("_CloudNoiseTexture")));
        assert!(!c.maps.is_empty(), "outdoor maps have their own sky (MapSkyInfo)");
    }
}
