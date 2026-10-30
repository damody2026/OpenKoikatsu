//! The render state of every shader the game and its mods ship, by shader name (`shader_states.json` in the base
//! library): the main colour pass's cull and whether an outline pass exists, read from each Shader object's passes
//! (`unity::shader_pass_state`). Materials recorded before their shader's state was kept, materials whose shader lives
//! in another file and materials given another shader (Material Editor) take it from here by name.
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, OnceLock};

pub const FILE_NAME: &str = "shader_states.json";

#[derive(Clone, Copy, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct State { pub cull: i64, pub outline: bool }

#[derive(Default, serde::Serialize, serde::Deserialize)]
pub struct Catalog {
    pub shaders: BTreeMap<String, State>,
    /// Names whose copies disagree (the state most copies have is kept), with the number of copies of each state.
    #[serde(default)]
    pub conflicts: BTreeMap<String, Vec<(State, usize)>>,
}

fn load(lib: &Path) -> Arc<Catalog> {
    static CACHE: OnceLock<Mutex<BTreeMap<PathBuf, Arc<Catalog>>>> = OnceLock::new();
    let mut cache = CACHE.get_or_init(Default::default).lock().unwrap();
    cache.entry(lib.to_path_buf()).or_insert_with(|| Arc::new(std::fs::read_to_string(lib.join(FILE_NAME)).ok()
        .and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default())).clone()
}

/// Gives each material the state of the shader it now has, unless it already carries that shader's own record.
pub fn complete(lib: &Path, meshes: &mut [crate::unity::ExtractedMesh]) {
    let catalog = load(lib);
    if catalog.shaders.is_empty() { return; }
    for mat in meshes.iter_mut().filter_map(|m| m.mat.as_mut()) {
        let key = crate::unity::shader_state_key(&mat.shader) as f32;
        if mat.floats.get("_OKShaderState") == Some(&key) { continue; }
        let Some(state) = catalog.shaders.get(&mat.shader) else { continue };
        mat.floats.insert("_OKShaderCull".into(), state.cull as f32);
        mat.floats.insert("_OKShaderOutline".into(), if state.outline { 1.0 } else { 0.0 });
        mat.floats.insert("_OKShaderState".into(), key);
    }
}

/// Every shader of one bundle: (name, state). A cull bound to a material property counts as Back (Unity's default).
fn bundle_states(bytes: Vec<u8>) -> Vec<(String, State)> {
    // Uncached: a pass over every bundle must not fill the bundle cache.
    let parsed = crate::unity::parse_bundle_uncached(bytes.into());
    parsed.with_serialized(|files| {
        let mut out = Vec::new();
        for file in files {
            for info in file.objects_of_type(48) {
                let Ok(o) = unity_asset_decode::object::UnityObject::from_serialized_file(file, info) else { continue };
                let name = crate::unity::shader_runtime_name(&o);
                let Some(s) = crate::unity::shader_pass_state(&o).filter(|_| !name.is_empty()) else { continue };
                out.push((name, State { cull: s.cull.unwrap_or(2), outline: s.outline }));
            }
        }
        out
    })
}

/// `shader-states <lib> [threads]`: reads the shaders of every game bundle and zipmod bundle into `shader_states.json`.
pub fn convert(game: &Path, lib: &Path, threads: usize) -> Result<(usize, usize, usize), String> {
    use std::io::Read;
    // Work items: a game bundle path, or a zipmod (its bundles read from the archive).
    let mut items: Vec<PathBuf> = Vec::new();
    let mut dirs = vec![game.join("abdata")];
    while let Some(d) = dirs.pop() {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() { dirs.push(p) } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("unity3d")) { items.push(p) }
        }
    }
    let bundles_in_game = items.len();
    items.extend(crate::mods::all_zipmods(&game.join("mods")));
    let next = std::sync::atomic::AtomicUsize::new(0);
    let found: Mutex<BTreeMap<String, BTreeMap<(i64, bool), usize>>> = Mutex::default();
    let read = std::sync::atomic::AtomicUsize::new(0);
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            scope.spawn(|| loop {
                let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                let Some(path) = items.get(i) else { break };
                let mut states = Vec::new();
                if i < bundles_in_game {
                    if let Ok(bytes) = std::fs::read(path) { states.extend(bundle_states(bytes)); read.fetch_add(1, std::sync::atomic::Ordering::Relaxed); }
                } else if let Ok(mut zip) = std::fs::File::open(path).map_err(|_| ()).and_then(|f| zip::ZipArchive::new(f).map_err(|_| ())) {
                    let names: Vec<String> = zip.file_names().filter(|n| n.to_ascii_lowercase().ends_with(".unity3d")).map(str::to_owned).collect();
                    for n in names {
                        let Ok(mut f) = zip.by_name(&n) else { continue };
                        let mut bytes = Vec::new();
                        if f.read_to_end(&mut bytes).is_err() { continue; }
                        states.extend(bundle_states(bytes));
                        read.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    }
                }
                if i % 500 == 0 { eprintln!("shader-states {i}/{}", items.len()); }
                let mut found = found.lock().unwrap();
                for (name, s) in states { *found.entry(name).or_default().entry((s.cull, s.outline)).or_default() += 1; }
            });
        }
    });
    let mut catalog = Catalog::default();
    for (name, copies) in found.into_inner().unwrap() {
        let state = |(cull, outline): (i64, bool)| State { cull, outline };
        let best = copies.iter().max_by_key(|(_, n)| **n).map(|(s, _)| *s).unwrap();
        if copies.len() > 1 { catalog.conflicts.insert(name.clone(), copies.iter().map(|(s, n)| (state(*s), *n)).collect()); }
        catalog.shaders.insert(name, state(best));
    }
    let text = serde_json::to_string_pretty(&catalog).map_err(|e| e.to_string())?;
    std::fs::write(lib.join(FILE_NAME), text).map_err(|e| format!("{FILE_NAME}: {e}"))?;
    Ok((read.into_inner(), catalog.shaders.len(), catalog.conflicts.len()))
}
