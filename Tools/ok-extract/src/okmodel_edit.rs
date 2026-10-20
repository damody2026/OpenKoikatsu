//! Edits of an OK model (`models/xx/<id>.okedit.json`): an overlay on the
//! converted data, applied whenever the model is read through the library.
//! The converted records never change; deleting the file restores them.
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use crate::oklib::Id;

#[derive(Clone, Debug, Default, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct MaterialEdit {
    #[serde(default)] pub colors: BTreeMap<String, [f32; 4]>,
    #[serde(default)] pub floats: BTreeMap<String, f32>,
    /// shader property -> texture name in this model
    #[serde(default)] pub textures: BTreeMap<String, String>,
}

#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct MeshEdit {
    #[serde(default)] pub hidden: bool,
    #[serde(default)] pub translate: [f32; 3],
    /// Euler degrees (X, Y, Z applied in that order).
    #[serde(default)] pub rotate: [f32; 3],
    #[serde(default = "one3")] pub scale: [f32; 3],
}
fn one3() -> [f32; 3] { [1.0; 3] }
impl Default for MeshEdit { fn default() -> Self { MeshEdit { hidden: false, translate: [0.0; 3], rotate: [0.0; 3], scale: [1.0; 3] } } }

#[derive(Clone, Debug, Default, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct ModelEdits {
    /// material name (lowercase) -> edit
    #[serde(default)] pub materials: BTreeMap<String, MaterialEdit>,
    /// texture name (lowercase) -> replacement PNG (file next to the edits)
    #[serde(default)] pub textures: BTreeMap<String, String>,
    /// "prefab/mesh" (lowercase) -> edit
    #[serde(default)] pub meshes: BTreeMap<String, MeshEdit>,
}

pub fn path(lib: &Path, id: &Id) -> PathBuf { crate::oklib::model_path(lib, id).with_extension("okedit.json") }
fn assets(lib: &Path, id: &Id) -> PathBuf { crate::oklib::model_path(lib, id).with_extension("okedit") }

type Cached = (PathBuf, Option<std::time::SystemTime>, Option<Arc<ModelEdits>>);
static CACHE: Mutex<Vec<Cached>> = Mutex::new(Vec::new());

/// Edits of a model in the active library (re-read when the file changes).
pub fn active(id: &Id) -> Option<Arc<ModelEdits>> {
    let store = crate::oklib::active()?;
    if store.mode != crate::oklib::Mode::Replay { return None; }
    let p = path(&store.dir, id);
    let stamp = std::fs::metadata(&p).ok().and_then(|m| m.modified().ok());
    let mut cache = CACHE.lock().unwrap();
    if let Some(hit) = cache.iter().find(|c| c.0 == p && c.1 == stamp) { return hit.2.clone(); }
    let edits = stamp.and_then(|_| std::fs::read(&p).ok()).and_then(|b| serde_json::from_slice::<ModelEdits>(&b).ok())
        .filter(|e| *e != ModelEdits::default()).map(Arc::new);
    cache.retain(|c| c.0 != p);
    if cache.len() > 256 { cache.clear(); }
    cache.push((p, stamp, edits.clone()));
    edits
}

pub fn load(lib: &Path, id: &Id) -> ModelEdits {
    std::fs::read(path(lib, id)).ok().and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default()
}

/// Save edits; a replacement texture given as a PNG path outside the model
/// folder is copied next to the edits (the library stays self-contained).
pub fn save(lib: &Path, id: &Id, mut edits: ModelEdits) -> Result<(), String> {
    let dir = assets(lib, id);
    for (name, file) in edits.textures.iter_mut() {
        let source = PathBuf::from(&*file);
        if source.is_absolute() && source.parent() != Some(dir.as_path()) {
            std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
            let bytes = std::fs::read(&source).map_err(|e| format!("{}: {e}", source.display()))?;
            crate::teximg::load_from_memory(&bytes).map_err(|e| format!("{}: {e}", source.display()))?;
            let dest = dir.join(format!("{}.png", name.replace(['/', '\\', ':'], "_")));
            std::fs::write(&dest, bytes).map_err(|e| e.to_string())?;
            *file = dest.file_name().unwrap().to_string_lossy().into_owned();
        }
    }
    let p = path(lib, id);
    if edits == ModelEdits::default() { let _ = std::fs::remove_file(&p); stamp(lib); return Ok(()); }
    std::fs::write(&p, serde_json::to_vec_pretty(&edits).unwrap()).map_err(|e| e.to_string())?;
    stamp(lib);
    Ok(())
}

/// Any model edit changes every card using that model: the open-card cache key includes this stamp.
fn stamp(lib: &Path) {
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    let _ = std::fs::write(lib.join("edits.stamp"), now.to_string());
}

/// Save a model with its current edits as a new library model ("Save As"): a copy of the converted model under a new
/// id derived from the original id and the edits, with those edits and their replacement textures. The original
/// model, its edits and every card using it are unchanged. Returns the new id.
pub fn save_as(lib: &Path, id: &Id) -> Result<Id, String> {
    let edits = load(lib, id);
    if edits == ModelEdits::default() { return Err("the model has no edits to save as a new model".into()); }
    let json = serde_json::to_vec(&edits).map_err(|e| e.to_string())?;
    let mut seed = id.to_vec();
    seed.extend_from_slice(b"\0okedit-save-as\0");
    seed.extend_from_slice(&json);
    let new = crate::oklib::hash16(&seed);
    let (from, to) = (crate::oklib::model_path(lib, id), crate::oklib::model_path(lib, &new));
    if let Some(parent) = to.parent() { std::fs::create_dir_all(parent).map_err(|e| e.to_string())?; }
    std::fs::copy(&from, &to).map_err(|e| format!("{}: {e}", from.display()))?;
    let (src, dst) = (assets(lib, id), assets(lib, &new));
    if src.is_dir() {
        std::fs::create_dir_all(&dst).map_err(|e| e.to_string())?;
        for entry in std::fs::read_dir(&src).map_err(|e| e.to_string())?.flatten() {
            std::fs::copy(entry.path(), dst.join(entry.file_name())).map_err(|e| e.to_string())?;
        }
    }
    std::fs::write(path(lib, &new), serde_json::to_vec_pretty(&edits).unwrap()).map_err(|e| e.to_string())?;
    stamp(lib);
    Ok(new)
}

/// Replacement PNG bytes of a texture.
pub fn texture_png(lib_dir: &Path, id: &Id, edits: &ModelEdits, name: &str) -> Option<Vec<u8>> {
    let file = edits.textures.get(&name.to_ascii_lowercase())?;
    let p = PathBuf::from(file);
    let p = if p.is_absolute() { p } else { assets(lib_dir, id).join(p) };
    std::fs::read(p).ok()
}

pub fn apply_material(edits: &ModelEdits, mat: &mut crate::unity::MeshMaterial) {
    let Some(e) = edits.materials.get(&mat.name.to_ascii_lowercase()) else { return };
    for (k, v) in &e.colors { mat.colors.insert(k.clone(), *v); }
    for (k, v) in &e.floats { mat.floats.insert(k.clone(), *v); }
    for (slot, tex) in &e.textures {
        match mat.tex_slots.iter_mut().find(|(s, _)| s == slot) { Some(entry) => entry.1 = tex.clone(), None => mat.tex_slots.push((slot.clone(), tex.clone())) }
    }
}

/// Vertex transform of a mesh edit (scale, then rotate X/Y/Z, then translate).
pub fn transform_mesh(e: &MeshEdit, positions: &mut [[f32; 3]], normals: &mut [[f32; 3]]) {
    let (sx, sy, sz) = (e.scale[0], e.scale[1], e.scale[2]);
    let r = |deg: f32| deg.to_radians();
    let (cx, snx) = (r(e.rotate[0]).cos(), r(e.rotate[0]).sin());
    let (cy, sny) = (r(e.rotate[1]).cos(), r(e.rotate[1]).sin());
    let (cz, snz) = (r(e.rotate[2]).cos(), r(e.rotate[2]).sin());
    let rot = |v: [f32; 3]| {
        let v = [v[0], v[1] * cx - v[2] * snx, v[1] * snx + v[2] * cx];
        let v = [v[0] * cy + v[2] * sny, v[1], -v[0] * sny + v[2] * cy];
        [v[0] * cz - v[1] * snz, v[0] * snz + v[1] * cz, v[2]]
    };
    for p in positions.iter_mut() {
        let v = rot([p[0] * sx, p[1] * sy, p[2] * sz]);
        *p = [v[0] + e.translate[0], v[1] + e.translate[1], v[2] + e.translate[2]];
    }
    for n in normals.iter_mut() {
        let v = rot([n[0] / sx.max(1e-6), n[1] / sy.max(1e-6), n[2] / sz.max(1e-6)]);
        let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt().max(1e-12);
        *n = [v[0] / l, v[1] / l, v[2] / l];
    }
}
