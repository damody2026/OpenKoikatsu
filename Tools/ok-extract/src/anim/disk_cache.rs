#[allow(unused_imports)] use crate::gamefs::GamePath as _;
// Decoded animation data on disk, so a pose does not decode Unity bundles again in the next session.
//
// Each entry is our own layout (bincode of the decoded structs) compressed with LZ4: loading is a file read, an LZ4
// decompression and a flat deserialize, instead of LZMA/LZ4 bundle extraction, type-tree parsing and curve decoding.
// The cache lives where OK_ANIM_CACHE_DIR points (the host sets it to its Saved folder; never inside the game or
// shared resource folders, which may be links to another installation). Entries are keyed by the resource root's
// manifest.json revision: installed packs are immutable within a revision (same rule as the in-memory caches), so a
// new revision simply starts a new folder. Roots without a manifest are not cached.
//
// OK base library animation roots (`<lib>/paired`, marked by `okpaired.json`) carry these entries themselves in one
// pack (`cache/pack.bin`: each distinct content once, then an index of entry name -> content): such a root has no
// Unity bundles, every query is answered from the pack, and a query without an entry is recorded as a miss
// (`library_misses`) instead of falling back to a bundle. `set_export` makes a conversion collect every entry it reads
// or computes; `finish_export` writes the pack.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

use sha2::{Digest, Sha256};

const MAGIC: &[u8; 8] = b"OKANIMC1";
/// OK base library pack: `PACK` + contents + index (count, then name / offset / length per entry) + index offset + `PACK`.
const PACK: &[u8; 8] = b"OKANIMP1";
const PACK_FILE: &str = "pack.bin";
/// Bump when any cached struct changes its serialized layout.
const FORMAT: u32 = 1;

fn cache_root() -> Option<PathBuf> {
    static ROOT: OnceLock<Option<PathBuf>> = OnceLock::new();
    ROOT.get_or_init(|| std::env::var_os("OK_ANIM_CACHE_DIR").filter(|v| !v.is_empty()).map(PathBuf::from)).clone()
}

fn hex(bytes: &[u8]) -> String { bytes.iter().map(|b| format!("{b:02x}")).collect() }

/// Marker file of an OK base library animation root.
pub(crate) const LIBRARY_MARKER: &str = "okpaired.json";

/// Whether the root is an OK base library animation root (no Unity bundles; decoded entries only).
pub(crate) fn is_library(root: &Path) -> bool {
    static ROOTS: OnceLock<Mutex<HashMap<PathBuf, bool>>> = OnceLock::new();
    let roots = ROOTS.get_or_init(|| Mutex::new(HashMap::new()));
    if let Some(known) = roots.lock().ok().and_then(|r| r.get(root).copied()) { return known; }
    // The real file system: a running base library replays gamefs paths under it, where this marker is not recorded.
    let yes = std::path::Path::is_file(&root.join(LIBRARY_MARKER));
    if let Ok(mut r) = roots.lock() { r.insert(root.to_path_buf(), yes); }
    yes
}

static EXPORT: std::sync::RwLock<Option<PathBuf>> = std::sync::RwLock::new(None);
/// Conversion state: entry name (`kind/<hash>.bin`) -> content hash; the contents wait under `<export>/blob/`.
static EXPORTED: Mutex<Option<HashMap<String, [u8; 32]>>> = Mutex::new(None);
/// Conversion: collect every entry read or computed for a library pack in this `cache/` folder (None stops).
pub(crate) fn set_export(dir: Option<PathBuf>) {
    if let Ok(mut m) = EXPORTED.lock() { *m = dir.as_ref().map(|_| HashMap::new()); }
    if let Ok(mut e) = EXPORT.write() { *e = dir; }
}
fn pack_name(kind: &str, key: &str) -> String { format!("{kind}/{}", entry_name(kind, key)) }
fn exporting(name: &str) -> Option<bool> { EXPORTED.lock().ok()?.as_ref().map(|m| m.contains_key(name)) }

/// Writes the collected entries as the library pack (`<export>/pack.bin`) and removes the staged contents.
/// Returns (entries, distinct contents, pack bytes).
pub(crate) fn finish_export() -> Result<(usize, usize, u64), String> {
    use std::io::Write;
    let dir = EXPORT.read().ok().and_then(|e| e.clone()).ok_or("no export in progress")?;
    let entries = EXPORTED.lock().map_err(|_| "export state poisoned")?.clone().unwrap_or_default();
    let blobs = dir.join("blob");
    let pack = dir.join(PACK_FILE);
    let tmp = dir.join(format!("{PACK_FILE}.tmp"));
    let mut out = std::io::BufWriter::new(std::fs::File::create(&tmp).map_err(|e| format!("{}: {e}", tmp.display()))?);
    out.write_all(PACK).map_err(|e| e.to_string())?;
    let mut hashes: Vec<[u8; 32]> = entries.values().copied().collect();
    hashes.sort();
    hashes.dedup();
    let mut at: HashMap<[u8; 32], (u64, u64)> = HashMap::with_capacity(hashes.len());
    let mut offset = PACK.len() as u64;
    for h in &hashes {
        let bytes = std::fs::read(blobs.join(format!("{}.bin", hex(h)))).map_err(|e| format!("staged content {}: {e}", hex(h)))?;
        out.write_all(&bytes).map_err(|e| e.to_string())?;
        at.insert(*h, (offset, bytes.len() as u64));
        offset += bytes.len() as u64;
    }
    let index_at = offset;
    let mut names: Vec<(&String, &[u8; 32])> = entries.iter().collect();
    names.sort();
    out.write_all(&(names.len() as u64).to_le_bytes()).map_err(|e| e.to_string())?;
    for (name, h) in names {
        let (o, l) = at[h];
        out.write_all(&(name.len() as u16).to_le_bytes()).map_err(|e| e.to_string())?;
        out.write_all(name.as_bytes()).map_err(|e| e.to_string())?;
        out.write_all(&o.to_le_bytes()).map_err(|e| e.to_string())?;
        out.write_all(&l.to_le_bytes()).map_err(|e| e.to_string())?;
    }
    out.write_all(&index_at.to_le_bytes()).map_err(|e| e.to_string())?;
    out.write_all(PACK).map_err(|e| e.to_string())?;
    out.flush().map_err(|e| e.to_string())?;
    drop(out);
    std::fs::rename(&tmp, &pack).map_err(|e| format!("{}: {e}", pack.display()))?;
    let size = std::fs::metadata(&pack).map(|m| m.len()).unwrap_or(0);
    std::fs::remove_dir_all(&blobs).map_err(|e| format!("{}: {e}", blobs.display()))?;
    Ok((entries.len(), hashes.len(), size))
}

/// The pack index of a library root (read once): entry name -> (offset, length).
fn pack_index(root: &Path) -> Option<std::sync::Arc<HashMap<String, (u64, u64)>>> {
    use std::io::{Read, Seek, SeekFrom};
    static INDEXES: OnceLock<Mutex<HashMap<PathBuf, Option<std::sync::Arc<HashMap<String, (u64, u64)>>>>>> = OnceLock::new();
    let indexes = INDEXES.get_or_init(|| Mutex::new(HashMap::new()));
    if let Some(known) = indexes.lock().ok()?.get(root) { return known.clone(); }
    let read = || -> Option<HashMap<String, (u64, u64)>> {
        let mut file = std::fs::File::open(root.join("cache").join(PACK_FILE)).ok()?;
        let len = file.metadata().ok()?.len();
        if len < 24 { return None; }
        let mut tail = [0u8; 16];
        file.seek(SeekFrom::Start(len - 16)).ok()?;
        file.read_exact(&mut tail).ok()?;
        if &tail[8..] != PACK { return None; }
        let index_at = u64::from_le_bytes(tail[..8].try_into().ok()?);
        if index_at >= len - 16 { return None; }
        let mut index = vec![0u8; (len - 16 - index_at) as usize];
        file.seek(SeekFrom::Start(index_at)).ok()?;
        file.read_exact(&mut index).ok()?;
        let count = u64::from_le_bytes(index.get(..8)?.try_into().ok()?) as usize;
        let mut map = HashMap::with_capacity(count);
        let mut p = 8usize;
        for _ in 0..count {
            let n = u16::from_le_bytes(index.get(p..p + 2)?.try_into().ok()?) as usize;
            let name = std::str::from_utf8(index.get(p + 2..p + 2 + n)?).ok()?.to_owned();
            p += 2 + n;
            let o = u64::from_le_bytes(index.get(p..p + 8)?.try_into().ok()?);
            let l = u64::from_le_bytes(index.get(p + 8..p + 16)?.try_into().ok()?);
            p += 16;
            if o.checked_add(l)? > index_at { return None; }
            map.insert(name, (o, l));
        }
        Some(map)
    };
    let index = read().map(std::sync::Arc::new);
    indexes.lock().ok()?.insert(root.to_path_buf(), index.clone());
    index
}

fn read_pack(root: &Path, name: &str) -> Option<Vec<u8>> {
    use std::io::{Read, Seek, SeekFrom};
    let (o, l) = *pack_index(root)?.get(name)?;
    let mut file = std::fs::File::open(root.join("cache").join(PACK_FILE)).ok()?;
    file.seek(SeekFrom::Start(o)).ok()?;
    let mut bytes = vec![0u8; l as usize];
    file.read_exact(&mut bytes).ok()?;
    Some(bytes)
}

static MISSES: Mutex<Vec<String>> = Mutex::new(Vec::new());
/// Queries a library root could not answer since the last call (cleared), as `kind<TAB>key`.
pub(crate) fn library_misses() -> Vec<String> { MISSES.lock().map(|mut m| std::mem::take(&mut *m)).unwrap_or_default() }
/// A query a library root could not answer.
pub(crate) fn note_miss(kind: &str, key: &str) {
    if let Ok(mut m) = MISSES.lock() { let line = format!("{kind}\t{key}"); if !m.contains(&line) { m.push(line); } }
}

/// Folder for one resource root revision, or None when the root has no manifest (mutable game folders).
fn revision_dir(root: &Path) -> Option<PathBuf> {
    static DIRS: OnceLock<Mutex<HashMap<PathBuf, Option<PathBuf>>>> = OnceLock::new();
    if is_library(root) { return Some(root.join("cache")); }
    let base = cache_root()?;
    let dirs = DIRS.get_or_init(|| Mutex::new(HashMap::new()));
    if let Some(dir) = dirs.lock().ok()?.get(root) { return dir.clone(); }
    let dir = crate::gamefs::read(root.join("manifest.json")).ok().map(|manifest| {
        let mut h = Sha256::new();
        h.update(root.to_string_lossy().as_bytes());
        h.update([0u8]);
        h.update(&manifest);
        base.join(&hex(&h.finalize())[..24])
    });
    dirs.lock().ok()?.insert(root.to_path_buf(), dir.clone());
    dir
}

/// The resource root a bundle file belongs to: the nearest ancestor with a manifest.json.
pub(super) fn root_of_file(path: &Path) -> Option<PathBuf> {
    static ROOTS: OnceLock<Mutex<HashMap<PathBuf, Option<PathBuf>>>> = OnceLock::new();
    let parent = path.parent()?.to_path_buf();
    let roots = ROOTS.get_or_init(|| Mutex::new(HashMap::new()));
    if let Some(root) = roots.lock().ok()?.get(&parent) { return root.clone(); }
    let root = parent.ancestors().find(|dir| std::path::Path::is_file(&dir.join("manifest.json"))).map(Path::to_path_buf);
    roots.lock().ok()?.insert(parent, root.clone());
    root
}

fn entry_name(kind: &str, key: &str) -> String {
    let mut h = Sha256::new();
    h.update(kind.as_bytes());
    h.update([0u8]);
    h.update(key.as_bytes());
    format!("{}.bin", &hex(&h.finalize())[..32])
}

fn entry_path(root: &Path, kind: &str, key: &str) -> Option<PathBuf> {
    Some(revision_dir(root)?.join(kind).join(entry_name(kind, key)))
}

fn write_atomic(path: &Path, bytes: &[u8]) {
    let Some(dir) = path.parent() else { return; };
    if std::fs::create_dir_all(dir).is_err() { return; }
    // Unique temporary name + rename: concurrent writers (several preparation threads) never expose a partial file.
    static SERIAL: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let tmp = path.with_extension(format!("{}.{}.tmp", std::process::id(), SERIAL.fetch_add(1, std::sync::atomic::Ordering::Relaxed)));
    if std::fs::write(&tmp, bytes).is_ok() && std::fs::rename(&tmp, path).is_err() { let _ = std::fs::remove_file(&tmp); }
}

pub(crate) fn load<T: serde::de::DeserializeOwned>(root: &Path, kind: &str, key: &str) -> Option<T> {
    let name = pack_name(kind, key);
    let bytes = if is_library(root) {
        let Some(bytes) = read_pack(root, &name) else { note_miss(kind, key); return None; };
        bytes
    } else {
        crate::gamefs::read(entry_path(root, kind, key)?).ok()?
    };
    if exporting(&name) == Some(false) { write_export(&name, &bytes); }
    if bytes.len() < 12 || &bytes[..8] != MAGIC || u32::from_le_bytes(bytes[8..12].try_into().ok()?) != FORMAT { return None; }
    let raw = lz4_flex::decompress_size_prepended(&bytes[12..]).ok()?;
    bincode::deserialize(&raw).ok()
}

pub(crate) fn store<T: serde::Serialize>(root: &Path, kind: &str, key: &str, value: &T) {
    let path = entry_path(root, kind, key);
    let export = exporting(&pack_name(kind, key)).is_some();
    if path.is_none() && !export { return; }
    let Ok(raw) = bincode::serialize(value) else { return; };
    let mut bytes = Vec::with_capacity(raw.len() / 3 + 16);
    bytes.extend_from_slice(MAGIC);
    bytes.extend_from_slice(&FORMAT.to_le_bytes());
    bytes.extend_from_slice(&lz4_flex::compress_prepend_size(&raw));
    // A library root's entries are its content: written only by a conversion (export), never by a running session.
    if let Some(path) = path.filter(|_| !is_library(root)) { write_atomic(&path, &bytes); }
    if export { write_export(&pack_name(kind, key), &bytes); }
}

/// An exported entry: its content staged once under `blob/` (by hash), the entry recorded for the pack.
fn write_export(name: &str, bytes: &[u8]) {
    let Some(root) = EXPORT.read().ok().and_then(|e| e.clone()) else { return; };
    let hash: [u8; 32] = Sha256::digest(bytes).into();
    let blob = root.join("blob").join(format!("{}.bin", hex(&hash)));
    if !blob.ok_is_file() { write_atomic(&blob, bytes); }
    if let Ok(mut m) = EXPORTED.lock() { if let Some(m) = m.as_mut() { m.insert(name.to_owned(), hash); } }
}
