//! The render state of every shader the game and its mods ship, by shader name (`shader_states.json` in the base
//! library): the main colour pass's cull and depth write and whether an outline pass exists, read from each Shader object's passes
//! (`unity::shader_pass_state`). Materials recorded before their shader's state was kept, materials whose shader lives
//! in another file and materials given another shader (Material Editor) take it from here by name. The catalog says
//! which game and mods it was read from (`inventory`) and in what form (`version`); `ensure_in_background` reads it again
//! when either no longer matches, and a running process picks the new file up by its modification time.
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, OnceLock};

pub const FILE_NAME: &str = "shader_states.json";
/// The catalog's form: 2 keeps the main pass's depth write.
pub const VERSION: u32 = 2;

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, serde::Serialize, serde::Deserialize)]
pub struct State {
    pub cull: i64,
    pub outline: bool,
    /// The main pass's depth write (absent in catalogs written before it was kept: unknown).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub zwrite: Option<ZWrite>,
}

/// A fixed depth write (0 / 1), or the material property it is bound to (`ZWrite [_ZWrite]`).
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, serde::Serialize, serde::Deserialize)]
#[serde(untagged)]
pub enum ZWrite { Fixed(i64), Property(String) }

#[derive(Default, serde::Serialize, serde::Deserialize)]
pub struct Catalog {
    /// `VERSION` when written (0: before it was kept).
    #[serde(default)]
    pub version: u32,
    /// The bundles and zipmods it was read from: count, total size and newest modification time (`inventory`).
    #[serde(default)]
    pub inventory: String,
    pub shaders: BTreeMap<String, State>,
    /// Names whose copies disagree (the state most copies have is kept), with the number of copies of each state.
    #[serde(default)]
    pub conflicts: BTreeMap<String, Vec<(State, usize)>>,
}

fn read(lib: &Path) -> Catalog {
    std::fs::read_to_string(lib.join(FILE_NAME)).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

/// The library's catalog, read again when the file changes (a background `ensure_in_background` replaced it).
fn load(lib: &Path) -> Arc<Catalog> {
    static CACHE: OnceLock<Mutex<BTreeMap<PathBuf, (Option<std::time::SystemTime>, Arc<Catalog>)>>> = OnceLock::new();
    let modified = std::fs::metadata(lib.join(FILE_NAME)).and_then(|m| m.modified()).ok();
    let mut cache = CACHE.get_or_init(Default::default).lock().unwrap();
    if let Some((when, catalog)) = cache.get(lib) { if *when == modified { return catalog.clone(); } }
    let catalog = Arc::new(read(lib));
    cache.insert(lib.to_path_buf(), (modified, catalog.clone()));
    catalog
}

/// Every game bundle, then every zipmod (whose bundles are read from the archive), and how many are game bundles.
fn sources(game: &Path) -> (Vec<PathBuf>, usize) {
    let mut items: Vec<PathBuf> = Vec::new();
    let mut dirs = vec![game.join("abdata")];
    while let Some(d) = dirs.pop() {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() { dirs.push(p) } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("unity3d")) { items.push(p) }
        }
    }
    items.sort();
    let bundles_in_game = items.len();
    items.extend(crate::mods::all_zipmods(&game.join("mods")));
    (items, bundles_in_game)
}

/// The sources' count, total size and newest modification time: a changed game or mods folder reads differently.
fn inventory(items: &[PathBuf]) -> String {
    let (mut bytes, mut newest) = (0u64, 0u64);
    for p in items {
        let Ok(m) = std::fs::metadata(p) else { continue };
        bytes += m.len();
        let t = m.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs());
        newest = newest.max(t);
    }
    format!("{} {bytes} {newest}", items.len())
}

/// Reads the catalog again on a background thread (once per library in a process) when it is missing, of an older
/// form or read from other game files than `game` has now. Another process doing it (a lock file younger than two
/// hours) is left to finish. Materials built meanwhile keep the old catalog's states.
pub fn ensure_in_background(game: PathBuf, lib: PathBuf) {
    static STARTED: OnceLock<Mutex<std::collections::BTreeSet<PathBuf>>> = OnceLock::new();
    if !STARTED.get_or_init(Default::default).lock().unwrap().insert(lib.clone()) { return; }
    if !game.join("abdata").is_dir() { return; }
    std::thread::Builder::new().name("ok-shader-states".into()).spawn(move || {
        let (items, _) = sources(&game);
        let current = read(&lib);
        if current.version >= VERSION && current.inventory == inventory(&items) && !current.shaders.is_empty() { return; }
        let lock = lib.join(format!("{FILE_NAME}.lock"));
        let busy = std::fs::metadata(&lock).and_then(|m| m.modified()).ok()
            .and_then(|t| t.elapsed().ok()).is_some_and(|age| age.as_secs() < 2 * 3600);
        if busy { return; }
        crate::ffi::ffi_log(0, &format!("OK shader states: reading {} game bundles and zipmods again (version {} -> {VERSION})", items.len(), current.version));
        let _ = std::fs::write(&lock, std::process::id().to_string());
        let threads = std::thread::available_parallelism().map_or(2, |n| (n.get() / 4).max(1));
        let message = match convert(&game, &lib, threads) {
            Ok((bundles, shaders, _)) => format!("OK shader states read again: bundles={bundles} shaders={shaders}"),
            Err(e) => format!("OK shader states: {e}"),
        };
        eprintln!("{message}");
        crate::ffi::ffi_log(0, &message);
        let _ = std::fs::remove_file(&lock);
    }).ok();
}

/// Gives each material the state of the shader it now has, unless it already carries that shader's own record (then
/// only the depth write, when that record predates it).
pub fn complete(lib: &Path, meshes: &mut [crate::unity::ExtractedMesh]) {
    let catalog = load(lib);
    if catalog.shaders.is_empty() { return; }
    for mat in meshes.iter_mut().filter_map(|m| m.mat.as_mut()) {
        let key = crate::unity::shader_state_key(&mat.shader) as f32;
        let Some(state) = catalog.shaders.get(&mat.shader) else { continue };
        let zwrite = state.zwrite.as_ref().map(|z| crate::unity::resolve_zwrite(&match z {
            ZWrite::Fixed(v) => Ok(*v), ZWrite::Property(name) => Err(name.clone()) }, &mat.floats));
        if mat.floats.get("_OKShaderState") == Some(&key) {
            if let Some(z) = zwrite { mat.floats.entry("_OKShaderZWrite".into()).or_insert(z); }
            continue;
        }
        match zwrite { Some(z) => { mat.floats.insert("_OKShaderZWrite".into(), z); } None => { mat.floats.remove("_OKShaderZWrite"); } }
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
                let zwrite = match s.zwrite { Ok(v) => ZWrite::Fixed(v), Err(name) => ZWrite::Property(name) };
                out.push((name, State { cull: s.cull.unwrap_or(2), outline: s.outline, zwrite: Some(zwrite) }));
            }
        }
        out
    })
}

/// `shader-states <lib> [threads]`: reads the shaders of every game bundle and zipmod bundle into `shader_states.json`.
pub fn convert(game: &Path, lib: &Path, threads: usize) -> Result<(usize, usize, usize), String> {
    use std::io::Read;
    // Work items: a game bundle path, or a zipmod (its bundles read from the archive).
    let (items, bundles_in_game) = sources(game);
    let next = std::sync::atomic::AtomicUsize::new(0);
    let found: Mutex<BTreeMap<String, BTreeMap<State, usize>>> = Mutex::default();
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
                for (name, s) in states { *found.entry(name).or_default().entry(s).or_default() += 1; }
            });
        }
    });
    let mut catalog = Catalog { version: VERSION, inventory: inventory(&items), ..Default::default() };
    for (name, copies) in found.into_inner().unwrap() {
        let best = copies.iter().max_by_key(|(_, n)| **n).map(|(s, _)| s.clone()).unwrap();
        if copies.len() > 1 { catalog.conflicts.insert(name.clone(), copies.iter().map(|(s, n)| (s.clone(), *n)).collect()); }
        catalog.shaders.insert(name, best);
    }
    let text = serde_json::to_string_pretty(&catalog).map_err(|e| e.to_string())?;
    // Written beside and renamed over, so a reader never sees half a file.
    let temp = lib.join(format!("{FILE_NAME}.tmp"));
    std::fs::write(&temp, text).map_err(|e| format!("{FILE_NAME}: {e}"))?;
    std::fs::rename(&temp, lib.join(FILE_NAME)).map_err(|e| format!("{FILE_NAME}: {e}"))?;
    Ok((read.into_inner(), catalog.shaders.len(), catalog.conflicts.len()))
}
