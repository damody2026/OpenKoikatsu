//! File-scoped Unity PPtrs. A PathID is never an identity across serialized files.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::collections::{HashMap, HashSet};
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use unity_asset_binary::asset::SerializedFile;
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub(crate) struct PPtr {
    pub file: i32,
    pub path: i64,
}
impl PPtr {
    pub(crate) fn read(value: &UnityValue) -> Option<Self> {
        let UnityValue::Object(fields) = value else {
            return None;
        };
        let file = i32::try_from(fields.get("m_FileID")?.as_i64()?).ok()?;
        let path = fields.get("m_PathID")?.as_i64()?;
        (file >= 0 && path != 0).then_some(Self { file, path })
    }
    pub(crate) fn local(value: &UnityValue) -> Option<i64> {
        Self::read(value).filter(|p| p.file == 0).map(|p| p.path)
    }
}

#[derive(Default)]
pub(crate) struct Scripts {
    classes: HashMap<PPtr, String>,
}
impl Scripts {
    pub(crate) fn local(file: &SerializedFile) -> Self {
        let classes = script_classes(file)
            .into_iter()
            .map(|(path, class)| (PPtr { file: 0, path }, class))
            .collect();
        Self { classes }
    }
    pub(crate) fn class(&self, value: Option<&UnityValue>) -> Option<&str> {
        self.classes.get(&PPtr::read(value?)?).map(String::as_str)
    }
}
fn script_classes(file: &SerializedFile) -> HashMap<i64, String> {
    file.objects
        .iter()
        .filter(|o| o.type_id == 115)
        .filter_map(|info| {
            let obj = UnityObject::from_serialized_file(file, info).ok()?;
            // m_Name is an asset label, not a managed type identity.
            let class = obj.class.properties().get("m_ClassName")?.as_str()?;
            Some((info.path_id, class.to_owned()))
        })
        .collect()
}
fn file_key(name: &str) -> String {
    name.replace('\\', "/")
        .rsplit('/')
        .next()
        .unwrap_or(name)
        .to_ascii_lowercase()
}

pub(crate) fn resolve(
    files: &[(&str, &SerializedFile)],
    root: Option<&Path>,
    origin: Option<&Path>,
) -> Vec<Scripts> {
    let local: Vec<_> = files.iter().map(|(_, file)| script_classes(file)).collect();
    let mut external_cache: HashMap<String, Option<HashMap<i64, String>>> = HashMap::new();
    files.iter().enumerate().map(|(index,(_,file))| {
        let mut scripts=Scripts { classes:local[index].iter().map(|(id,name)|
            (PPtr {file:0,path:*id},name.clone())).collect() };
        let wanted: HashSet<_>=file.objects.iter().filter(|o|o.type_id==114).filter_map(|info| {
            let obj=UnityObject::from_serialized_file(file,info).ok()?;
            PPtr::read(obj.class.properties().get("m_Script")?).filter(|p|p.file>0)
        }).collect();
        for pointer in wanted {
            let Some(target)=file.externals.get(pointer.file as usize-1) else {
                eprintln!("ok-extract: invalid external MonoScript FileID={} PathID={}",pointer.file,pointer.path); continue;
            };
            let key=file_key(&target.path);
            let candidates: Vec<_>=files.iter().enumerate().filter(|(_, (name,_))|file_key(name)==key).collect();
            let class=if candidates.len()==1 {local[candidates[0].0].get(&pointer.path).cloned()}
            else if candidates.is_empty() {
                external_cache.entry(key.clone()).or_insert_with(|| {
                    let root=root?;
                    let bytes=match load_external(root,&key,origin) {Ok(bytes)=>bytes,Err(error)=>{
                        eprintln!("ok-extract: {error}");return None;}};
                    let file=unity_asset_binary::asset::parse_serialized_file(bytes).ok()?;
                    Some(script_classes(&file))
                }).as_ref().and_then(|classes|classes.get(&pointer.path)).cloned()
            } else {None};
            if let Some(class)=class {scripts.classes.insert(pointer,class);}
            else {eprintln!("ok-extract: unresolved MonoScript {} FileID={} PathID={} (no local-ID fallback)",
                target.path,pointer.file,pointer.path);}
        }
        scripts
    }).collect()
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
struct Source {
    path: PathBuf,
    length: u64,
    modified: u128,
    entries: Vec<(String, Vec<String>)>,
}
#[derive(Clone, Default, serde::Serialize, serde::Deserialize)]
struct Catalog {
    sources: Vec<Source>,
}
thread_local! {
    static CATALOG:std::cell::RefCell<Option<(PathBuf,std::rc::Rc<Catalog>)>>=const {std::cell::RefCell::new(None)};
    // (root, mounted card pack, signature): the pack's own files are part of what the root serves.
    static EPOCH:std::cell::RefCell<Option<(PathBuf,String,String)>>=const {std::cell::RefCell::new(None)};
    // Optional per-thread change notification for a long-lived source owner.
    static WATCH:std::cell::RefCell<Option<(PathBuf,crate::appearance::catalog_watch::Watch)>>=const {std::cell::RefCell::new(None)};
}
/// Install change notifications for this thread's source owner, then take
/// one complete signature. Later loads skip the full metadata walk only while
/// the healthy watch has observed no change since that signature; any
/// notification (or an unavailable watch) recomputes it exactly as before.
pub(crate) fn watch_current_thread(root: &Path) -> String {
    WATCH.with(|w| *w.borrow_mut() = Some((root.to_owned(), crate::appearance::catalog_watch::Watch::new(root))));
    EPOCH.with(|epoch| *epoch.borrow_mut() = None);
    begin_load(root)
}
fn watched_unchanged(root: &Path) -> Option<String> {
    let unchanged = WATCH.with(|w| {
        w.borrow_mut().as_mut().is_some_and(|(r, watch)| r == root && watch.available() && !watch.changed())
    });
    if !unchanged {
        return None;
    }
    let pack = crate::oklib::mounted_pack_identity();
    EPOCH.with(|epoch| epoch.borrow().as_ref().filter(|(r, k, _)| r == root && *k == pack).map(|(_, _, s)| s.clone()))
}
pub(crate) fn clear_cache() {
    CATALOG.with(|c| *c.borrow_mut() = None);
}
fn inventory_signature(root: &Path) -> String {let _spot=crate::profile::spot("view: inventory_signature");
    use std::hash::{Hash, Hasher};
    // An OK card's strict open lists nothing of the library's trees (okstrict.rs); the library cannot change while
    // it is active, and the card's own pack has its identity.
    if crate::okstrict::active().is_some() && crate::gamefs::serves_from_library(root) {
        return format!("ok card:{}", crate::oklib::mounted_pack_identity());
    }
    let mut paths = Vec::new();
    collect(&root.join("abdata"), &mut paths, false);
    collect(&root.join("mods"), &mut paths, true);
    paths.sort();
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    for path in &paths {
        path.hash(&mut hash);
        if let Ok(meta) = crate::gamefs::metadata(path) {
            meta.len().hash(&mut hash);
            meta.modified().ok().hash(&mut hash);
        }
    }
    format!("sources={}:hash={:016x}", paths.len(), hash.finish())
}
/// At a card/extraction boundary, refresh the metadata-only source epoch before
/// consulting any session/prefab cache. Edits to older dependencies also count.
pub(crate) fn begin_load(root: &Path) -> String {
    if let Some(signature) = watched_unchanged(root) {
        return signature;
    }
    let signature = inventory_signature(root);
    let pack = crate::oklib::mounted_pack_identity();
    let changed = EPOCH.with(|epoch| {
        let changed = epoch
            .borrow()
            .as_ref()
            .is_some_and(|(r, _, s)| r != root || s != &signature);
        *epoch.borrow_mut() = Some((root.to_owned(), pack, signature.clone()));
        changed
    });
    if changed {
        super::clear_bundle_cache();
        crate::clear_entry_memo();
    }
    signature
}
pub(crate) fn current_signature(root: &Path) -> String {
    let cached = EPOCH.with(|epoch| {
        epoch
            .borrow()
            .as_ref()
            .filter(|(r, k, _)| r == root && *k == crate::oklib::mounted_pack_identity())
            .map(|(_, _, s)| s.clone())
    });
    cached.unwrap_or_else(|| begin_load(root))
}
fn collect(path: &Path, out: &mut Vec<PathBuf>, mods: bool) {
    let Ok(entries) = crate::gamefs::read_dir(path) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        // Avoid directory junction loops and indexing resources outside this installation.
        let Ok(kind) = entry.file_type() else {
            continue;
        };
        if kind.is_symlink() {
            continue;
        }
        if kind.ok_is_dir() {
            collect(&path, out, mods);
        } else if kind.ok_is_file() {
            let ext = path
                .extension()
                .and_then(|x| x.to_str())
                .unwrap_or("")
                .to_ascii_lowercase();
            if (mods && (ext == "zipmod" || ext == "zip"))
                || (!mods && (ext == "unity3d" || ext == "assets" || ext.is_empty()))
            {
                out.push(path);
            }
        }
    }
}
fn cstring(reader: &mut impl Read) -> std::io::Result<String> {
    let mut bytes = Vec::new();
    for _ in 0..4096 {
        let mut byte = [0];
        reader.read_exact(&mut byte)?;
        if byte[0] == 0 {
            return String::from_utf8(bytes).map_err(|e| std::io::Error::other(e));
        }
        bytes.push(byte[0]);
    }
    Err(std::io::Error::other("unterminated UnityFS name"))
}
/// Read only UnityFS directory metadata. Payload blocks/meshes/textures stay compressed.
fn nodes(reader: &mut (impl Read + Seek)) -> Result<Vec<String>, String> {
    use unity_asset_binary::{
        bundle::{BundleCompression, BundleHeader},
        reader::{BinaryReader, ByteOrder},
    };
    reader.seek(SeekFrom::Start(0)).map_err(|e| e.to_string())?;
    let mut header = Vec::new();
    let value = cstring(reader).map_err(|e| e.to_string())?;
    header.extend(value.bytes());
    header.push(0);
    if !header.starts_with(b"UnityFS\0") {
        return Err("not UnityFS".into());
    }
    let mut version = [0; 4];
    reader.read_exact(&mut version).map_err(|e| e.to_string())?;
    header.extend(version);
    for _ in 0..2 {
        let value = cstring(reader).map_err(|e| e.to_string())?;
        header.extend(value.bytes());
        header.push(0);
    }
    let mut tail = [0; 20];
    reader.read_exact(&mut tail).map_err(|e| e.to_string())?;
    header.extend(tail);
    let h = BundleHeader::from_reader(&mut BinaryReader::new(&header, ByteOrder::Big))
        .map_err(|e| e.to_string())?;
    let mut offset = header.len() as u64;
    if h.version >= 7 {
        offset = (offset + 15) & !15;
    } else {
        let modern =
            unity_asset_decode::unity_version::UnityVersion::parse_version(&h.unity_revision)
                .map(|v| v.major > 2019 || (v.major == 2019 && v.minor >= 4))
                .unwrap_or(false);
        if modern {
            let padding = ((16 - offset % 16) % 16) as usize;
            let mut bytes = vec![0; padding];
            reader.read_exact(&mut bytes).map_err(|e| e.to_string())?;
            if bytes.iter().all(|b| *b == 0) {
                offset += padding as u64;
            }
        }
    }
    let size = h.compressed_blocks_info_size as usize;
    if size > 64 * 1024 * 1024 {
        return Err("UnityFS directory exceeds 64 MB".into());
    }
    if h.block_info_at_end() {
        reader
            .seek(SeekFrom::End(-(size as i64)))
            .map_err(|e| e.to_string())?;
    } else {
        reader
            .seek(SeekFrom::Start(offset))
            .map_err(|e| e.to_string())?;
    }
    let mut bytes = vec![0; size];
    reader.read_exact(&mut bytes).map_err(|e| e.to_string())?;
    let raw = BundleCompression::decompress_blocks_info_limited(&h, &bytes, Some(64 * 1024 * 1024))
        .map_err(|e| e.to_string())?;
    let mut r = BinaryReader::new(&raw, ByteOrder::Big);
    r.read_bytes(16).map_err(|e| e.to_string())?;
    let count = r.read_i32().map_err(|e| e.to_string())?;
    if count < 0 {
        return Err("negative UnityFS block count".into());
    }
    r.read_bytes(
        (count as usize)
            .checked_mul(10)
            .ok_or("block count overflow")?,
    )
    .map_err(|e| e.to_string())?;
    let count = r.read_i32().map_err(|e| e.to_string())?;
    if !(0..=1_000_000).contains(&count) {
        return Err("invalid UnityFS node count".into());
    }
    let mut names = Vec::new();
    for _ in 0..count {
        r.read_bytes(20).map_err(|e| e.to_string())?;
        names.push(r.read_cstring().map_err(|e| e.to_string())?);
    }
    Ok(names)
}

/// ZIP deflate streams cannot seek. Buffer only bytes requested by the directory
/// reader; a rare tail-directory bundle necessarily consumes this one entry.
struct BufferedEntry<R> {
    reader: R,
    bytes: Vec<u8>,
    position: usize,
}
impl<R: Read> BufferedEntry<R> {
    fn new(reader: R) -> Self {
        Self {
            reader,
            bytes: Vec::new(),
            position: 0,
        }
    }
    fn fill(&mut self, end: usize) -> std::io::Result<()> {
        if end > self.bytes.len() {
            let remaining = end - self.bytes.len();
            (&mut self.reader)
                .take(remaining as u64)
                .read_to_end(&mut self.bytes)?;
        }
        Ok(())
    }
}
impl<R: Read> Read for BufferedEntry<R> {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        let end = self
            .position
            .checked_add(out.len())
            .ok_or_else(|| std::io::Error::other("entry offset overflow"))?;
        self.fill(end)?;
        if self.position >= self.bytes.len() {
            return Ok(0);
        }
        let n = out
            .len()
            .min(self.bytes.len().saturating_sub(self.position));
        out[..n].copy_from_slice(&self.bytes[self.position..self.position + n]);
        self.position += n;
        Ok(n)
    }
}
impl<R: Read> Seek for BufferedEntry<R> {
    fn seek(&mut self, from: SeekFrom) -> std::io::Result<u64> {
        let offset = match from {
            SeekFrom::Start(offset) => i128::from(offset),
            SeekFrom::Current(offset) => self.position as i128 + i128::from(offset),
            SeekFrom::End(offset) => {
                self.reader.read_to_end(&mut self.bytes)?;
                self.bytes.len() as i128 + i128::from(offset)
            }
        };
        let offset =
            usize::try_from(offset).map_err(|_| std::io::Error::other("invalid entry offset"))?;
        self.fill(offset)?;
        self.position = offset;
        Ok(offset as u64)
    }
}
fn source_entries(path: &Path) -> Vec<(String, Vec<String>)> {
    let ext = path.extension().and_then(|x| x.to_str()).unwrap_or("");
    if ext.eq_ignore_ascii_case("zipmod") || ext.eq_ignore_ascii_case("zip") {
        let Some(mut zip) = crate::gamefs::open_zip(path) else {
            return Vec::new();
        };
        let mut out = Vec::new();
        for index in 0..zip.len() {
            let Ok(entry) = zip.by_index(index) else {
                continue;
            };
            let name = entry.name().to_owned();
            if !name.to_ascii_lowercase().ends_with(".unity3d") {
                continue;
            }
            let Ok(names) = nodes(&mut BufferedEntry::new(entry)) else {
                continue;
            };
            out.push((name, names));
        }
        out
    } else {
        let Ok(mut file) = std::fs::File::open(path) else {
            return Vec::new();
        };
        if let Ok(names) = nodes(&mut file) {
            vec![(String::new(), names)]
        } else if ext.eq_ignore_ascii_case("assets") {
            vec![(
                String::new(),
                vec![path.file_name().unwrap().to_string_lossy().into_owned()],
            )]
        } else {
            Vec::new()
        }
    }
}
fn catalog(root: &Path) -> std::rc::Rc<Catalog> {
    CATALOG.with(|memory| {
        if let Some((cached_root, catalog)) = &*memory.borrow() {
            if cached_root == root {
                return catalog.clone();
            }
        }
        let catalog = std::rc::Rc::new(build_catalog(root));
        *memory.borrow_mut() = Some((root.to_owned(), catalog.clone()));
        catalog
    })
}

/// The serialized files every source of `root` holds. Listing the tree and reading each bundle's headers is a scan of
/// the whole tree (okstrict::scanning): while converting, none of it is the card's own data. Run on the opening
/// thread instead, a conversion whose accessory names an external file counted all 8,427 headers as reads its OK card
/// lacks (C032, when the index file was being rewritten by another worker). Sources unchanged since this process's
/// previous index are not read again; a game root also keeps an index file, a library root none (no disk caches
/// unless OK_LIBRARY_CACHE, oklib::library_scan_load).
fn build_catalog(root: &Path) -> Catalog {
    use std::sync::{Arc, Mutex};
    static LAST: Mutex<Option<(PathBuf, Arc<Catalog>)>> = Mutex::new(None);
    let library = crate::gamefs::serves_from_library(root);
    let cache = root.join(".ok_serialized_index_v1.bin");
    let last = LAST.lock().unwrap_or_else(|e| e.into_inner()).clone().filter(|(r, _)| r == root).map(|(_, c)| c);
    let root_owned = root.to_owned();
    let catalog = crate::okstrict::scanning(move || {
        let root = root_owned.as_path();
        let previous: Catalog = match last {
            Some(last) => (*last).clone(),
            None if library => crate::oklib::library_scan_load("serialized-index-v1").unwrap_or_default(),
            None => crate::gamefs::read(&cache).ok().and_then(|b| bincode::deserialize(&b).ok()).unwrap_or_default(),
        };
        let mut previous: HashMap<_, _> = previous
            .sources
            .into_iter()
            .map(|s| (s.path.clone(), s))
            .collect();
        let mut paths = Vec::new();
        collect(&root.join("abdata"), &mut paths, false);
        collect(&root.join("mods"), &mut paths, true);
        paths.sort();
        let mut catalog = Catalog::default();
        let mut refreshed = 0;
        for path in paths {
            let Ok(meta) = crate::gamefs::metadata(&path) else {
                continue;
            };
            let modified = meta
                .modified()
                .ok()
                .and_then(|v| v.duration_since(std::time::UNIX_EPOCH).ok())
                .map(|v| v.as_nanos())
                .unwrap_or(0);
            if let Some(old) = previous
                .remove(&path)
                .filter(|s| s.length == meta.len() && s.modified == modified)
            {
                catalog.sources.push(old);
                continue;
            }
            let entries = source_entries(&path);
            refreshed += 1;
            catalog.sources.push(Source {
                path,
                length: meta.len(),
                modified,
                entries,
            });
        }
        eprintln!(
            "ok-extract: serialized reference index sources={} refreshed={refreshed}",
            catalog.sources.len()
        );
        if refreshed > 0 {
            if library {
                crate::oklib::library_scan_save("serialized-index-v1", &catalog);
            } else if let Ok(bytes) = bincode::serialize(&catalog) {
                // Whole or not at all: other processes read it meanwhile.
                let tmp = cache.with_extension(format!("{}.tmp", std::process::id()));
                if std::fs::write(&tmp, bytes).is_ok() && std::fs::rename(&tmp, &cache).is_err() { let _ = std::fs::remove_file(&tmp); }
            }
        }
        catalog
    });
    *LAST.lock().unwrap_or_else(|e| e.into_inner()) = Some((root.to_owned(), Arc::new(catalog.clone())));
    catalog
}
fn load_external(root: &Path, key: &str, origin: Option<&Path>) -> Result<Vec<u8>, String> {
    let (assets, bytes) = read_external_source(root, key, origin)?;
    if assets {
        return Ok(bytes);
    }
    let bundle = super::load_bundle_unbounded(bytes).map_err(|e| e.to_string())?;
    bundle
        .extract_file_data(serialized_node(&bundle, key)?)
        .map_err(|e| e.to_string())
}
/// The one source (bundle or `.assets`) holding serialized file `key`:
/// (is a plain `.assets` file, its bytes).
fn read_external_source(root: &Path, key: &str, origin: Option<&Path>) -> Result<(bool, Vec<u8>), String> {
    let catalog = catalog(root);
    let matches: Vec<_> = catalog
        .sources
        .iter()
        .flat_map(|s| {
            s.entries
                .iter()
                .filter(move |(_, names)| names.iter().any(|n| file_key(n) == key))
                .map(move |(entry, _)| (s, entry))
        })
        .collect();
    let scoped: Vec<_> = matches
        .iter()
        .copied()
        .filter(|(source, _)| origin == Some(source.path.as_path()))
        .collect();
    let matches = if scoped.is_empty() { matches } else { scoped };
    if matches.len() != 1 {
        return Err(format!(
            "external file {key}: {} matching sources",
            matches.len()
        ));
    }
    let (source, entry) = matches[0];
    let bytes = if entry.is_empty() {
        crate::gamefs::read(&source.path).map_err(|e| e.to_string())?
    } else {
        let mut zip = crate::gamefs::open_zip(&source.path).ok_or("zipmod unreadable")?;
        let mut entry = zip.by_name(entry).map_err(|e| e.to_string())?;
        let mut bytes = Vec::new();
        entry.read_to_end(&mut bytes).map_err(|e| e.to_string())?;
        bytes
    };
    let assets = source
        .path
        .extension()
        .is_some_and(|e| e.eq_ignore_ascii_case("assets"));
    Ok((assets, bytes))
}
fn serialized_node<'a>(bundle: &'a unity_asset_binary::bundle::AssetBundle, key: &str)
    -> Result<&'a unity_asset_binary::bundle::BundleFileInfo, String> {
    let matches: Vec<_> = bundle
        .files
        .iter()
        .filter(|file| file_key(&file.name) == key)
        .collect();
    if matches.len() != 1 {
        return Err(format!("external file {key}: ambiguous bundle nodes"));
    }
    Ok(matches[0])
}

/// A serialized file another bundle references (`externals[FileID-1].path`,
/// e.g. `archive:/CAB-x/CAB-x`), parsed, with the other nodes of the bundle
/// that holds it: its objects' stream data (`.resS`) lives there.
pub(crate) fn load_external_with_streams(root: &Path, external_path: &str, origin: Option<&Path>)
    -> Result<(SerializedFile, Vec<(String, Vec<u8>)>), String> {
    let key = file_key(external_path);
    let (assets, bytes) = read_external_source(root, &key, origin)?;
    if assets {
        let file = unity_asset_binary::asset::parse_serialized_file(bytes).map_err(|e| format!("external file {key}: {e}"))?;
        return Ok((file, Vec::new()));
    }
    let bundle = super::load_bundle_unbounded(bytes).map_err(|e| e.to_string())?;
    let node = serialized_node(&bundle, &key)?;
    let data = bundle.extract_file_data(node).map_err(|e| e.to_string())?;
    let file = unity_asset_binary::asset::parse_serialized_file(data).map_err(|e| format!("external file {key}: {e}"))?;
    let streams = bundle.files.iter()
        .filter(|f| file_key(&f.name) != key)
        .filter_map(|f| Some((f.name.clone(), bundle.extract_file_data(f).ok()?)))
        .collect();
    Ok((file, streams))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[cfg(windows)]
    #[test]
    fn watched_epoch_skips_walk_until_installed_sources_change() {
        let id = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let root = std::env::temp_dir().join(format!("ok-reference-watch-{id}"));
        std::fs::create_dir_all(root.join("abdata/chara")).unwrap();
        std::fs::create_dir_all(root.join("mods")).unwrap();
        std::fs::write(root.join("abdata/chara/a.unity3d"), [1]).unwrap();
        std::thread::spawn(move || {
            let first = watch_current_thread(&root);
            assert_eq!(begin_load(&root), first, "unchanged sources reuse the epoch");
            assert!(watched_unchanged(&root).is_some());
            std::fs::write(root.join("mods/b.zipmod"), [2, 3]).unwrap();
            let start = std::time::Instant::now();
            let changed = loop {
                let signature = begin_load(&root);
                if signature != first { break signature; }
                assert!(start.elapsed() < std::time::Duration::from_secs(2), "added source was not observed");
                std::thread::sleep(std::time::Duration::from_millis(5));
            };
            assert!(changed.starts_with("sources=2:"));
            assert_eq!(begin_load(&root), changed);
            let _ = std::fs::remove_dir_all(&root);
        }).join().unwrap();
    }
    fn pointer(file: i64, path: i64) -> UnityValue {
        UnityValue::Object(
            [
                ("m_FileID".into(), UnityValue::Integer(file)),
                ("m_PathID".into(), UnityValue::Integer(path)),
            ]
            .into_iter()
            .collect(),
        )
    }
    #[test]
    fn external_script_pointer_identity() {
        let scripts = Scripts {
            classes: [
                (PPtr { file: 0, path: 7 }, "DynamicBoneCollider".into()),
                (PPtr { file: 1, path: 7 }, "DynamicBone".into()),
                (PPtr { file: 2, path: 7 }, "BoneImplantProcess".into()),
            ]
            .into_iter()
            .collect(),
        };
        assert_eq!(
            scripts.class(Some(&pointer(0, 7))),
            Some("DynamicBoneCollider")
        );
        assert_eq!(scripts.class(Some(&pointer(1, 7))), Some("DynamicBone"));
        assert_eq!(
            scripts.class(Some(&pointer(2, 7))),
            Some("BoneImplantProcess")
        );
        assert_eq!(
            scripts.class(Some(&pointer(3, 7))),
            None,
            "missing external must not select the local same-ID class"
        );
        assert_eq!(
            PPtr::local(&pointer(1, 7)),
            None,
            "unsupported external transforms must not alias a local bone"
        );
        assert_eq!(PPtr::local(&pointer(0, -7)), Some(-7));
        for p in [
            pointer(-1, 7),
            pointer(0, 0),
            pointer(i64::MAX, 7),
            UnityValue::Integer(7),
        ] {
            assert!(PPtr::read(&p).is_none());
        }
    }
    #[test]
    fn external_script_epoch_tracks_older_dependency() {
        let root = PathBuf::from("../../Saved/Tests")
            .join(format!("ReferenceEpoch273-{}", std::process::id()));
        std::fs::create_dir_all(root.join("abdata")).unwrap();
        let older = root.join("abdata/older.assets");
        let newer = root.join("abdata/newer.assets");
        std::fs::write(&older, b"old").unwrap();
        std::fs::write(&newer, b"new").unwrap();
        let before = begin_load(&root);
        CATALOG.with(|memory| {
            *memory.borrow_mut() = Some((root.clone(), std::rc::Rc::new(Catalog::default())))
        });
        std::fs::write(&older, b"edited older dependency").unwrap();
        let after = begin_load(&root);
        assert_ne!(before, after);
        assert_eq!(current_signature(&root), after);
        CATALOG.with(|memory| {
            assert!(
                memory.borrow().is_none(),
                "changed epoch clears the reference index"
            )
        });
    }
    #[test]
    fn external_script_stream_truncated_offset_is_eof() {
        let mut stream = BufferedEntry::new(std::io::Cursor::new(b"short"));
        stream.seek(SeekFrom::Start(100)).unwrap();
        assert_eq!(stream.read(&mut [0; 4]).unwrap(), 0);
        assert!(nodes(&mut stream).is_err());
    }
    fn fixture() -> PathBuf {
        PathBuf::from("../../Saved/Tests/ExternalScripts273")
    }
    fn serialized(bundle: &super::super::ParsedBundle) -> Vec<(String, SerializedFile)> {
        bundle
            .files
            .iter()
            .filter_map(|(name, bytes)| {
                crate::unity::parse_shared_serialized(bytes.clone())
                    .ok()
                    .map(|file| (name.clone(), file))
            })
            .collect()
    }
    #[test]
    #[ignore = "run make_external_script_fixture.py; original and rewritten actual game resources"]
    fn external_script_original_and_dependency_controls() {
        let root = fixture();
        clear_cache();
        for (label, original) in [("body", "oo_base.unity3d"), ("hair", "ao_hair_00.unity3d")] {
            let source = super::super::parse_bundle_cached(
                crate::gamefs::read(root.join(format!("{label}_source.unity3d"))).unwrap(),
            );
            let dependency = super::super::parse_bundle_cached(
                crate::gamefs::read(root.join(format!("{label}_scripts.unity3d"))).unwrap(),
            );
            let original = super::super::parse_bundle_cached(
                crate::gamefs::read(&crate::test_support::game_path("/abdata/chara").join(original))
                    .unwrap(),
            );
            let originals = serialized(&original);
            let sources = serialized(&source);
            let dependencies = serialized(&dependency);
            let sf = &sources[0].1;
            let original = &originals[0].1;
            let local = Scripts::local(sf);
            let named: Vec<_> = sources
                .iter()
                .chain(&dependencies)
                .map(|(name, file)| (name.as_str(), file))
                .collect();
            let internal = resolve(&named, None, None);
            let named: Vec<_> = sources
                .iter()
                .map(|(name, file)| (name.as_str(), file))
                .collect();
            let external = resolve(&named, Some(&root), None);
            let mut count = 0;
            for info in sf.objects.iter().filter(|o| o.type_id == 114) {
                let obj = UnityObject::from_serialized_file(sf, info).unwrap();
                let pointer = obj.class.properties().get("m_Script").unwrap();
                assert_eq!(
                    local.class(Some(pointer)),
                    None,
                    "same-ID local decoy must never be used"
                );
                let original_obj = UnityObject::from_serialized_file(
                    original,
                    original.find_object(info.path_id).unwrap(),
                )
                .unwrap();
                let expected = Scripts::local(original);
                let expected = expected.class(original_obj.class.properties().get("m_Script"));
                assert_eq!(internal[0].class(Some(pointer)), expected);
                assert_eq!(external[0].class(Some(pointer)), expected);
                count += 1;
            }
            let expected = crate::dbone::dbone_set(original);
            for scripts in [&internal[0], &external[0]] {
                let actual = crate::dbone::dbone_set_resolved(sf, scripts);
                assert_eq!(
                    serde_json::to_value(&actual).unwrap(),
                    serde_json::to_value(&expected).unwrap(),
                    "all ordinary config/collider fields are preserved"
                );
            }
            source.configure_game_references(&root);
            if label == "body" {
                let skel = source.skeleton_for_prefab_name("p_cf_body_bone").unwrap();
                let expected = crate::dbone::ver02::parse(original, &skel);
                assert_eq!(expected.len(), 4);
                let actual = source.body_dynamics_for_skeleton(&skel);
                assert_eq!(
                    bincode::serialize(&actual).unwrap(),
                    bincode::serialize(&expected).unwrap()
                );
                assert_eq!(source.body_colliders_for_skeleton(&skel).count(), 24);
            } else {
                let skel = source
                    .skeleton_for_prefab_name("p_acs_head_hanayure")
                    .unwrap();
                let actual = source.dynamic_bones_for_skeleton(&skel);
                assert!(
                    !actual.is_empty(),
                    "production ParsedBundle uses zipmod dependency resolution"
                );
            }
            eprintln!("external scripts {label}: {count} actual MonoBehaviour classes and complete physics config preserved");
        }
        clear_cache();
        let warm = catalog(&root);
        assert_eq!(warm.sources.len(), 2);
        assert!(load_external(&root, "missing-cab", None).is_err());
    }
    #[test]
    #[ignore = "original game bundles and generated fixture"]
    fn external_script_catalog_rejects_duplicate_sources() {
        let root = PathBuf::from("../../Saved/Tests")
            .join(format!("ReferenceCollision273-{}", std::process::id()));
        std::fs::create_dir_all(root.join("abdata")).unwrap();
        let bytes = crate::gamefs::read(fixture().join("hair_scripts.unity3d")).unwrap();
        let file = super::super::load_bundle_unbounded(bytes.clone())
            .unwrap()
            .files[0]
            .name
            .clone();
        let first = root.join("abdata/first.unity3d");
        let second = root.join("abdata/second.unity3d");
        std::fs::write(&first, &bytes).unwrap();
        begin_load(&root);
        assert_eq!(
            load_external(&root, &file_key(&file), None).unwrap(),
            super::super::load_bundle_unbounded(bytes.clone())
                .unwrap()
                .extract_file_data(
                    &super::super::load_bundle_unbounded(bytes.clone())
                        .unwrap()
                        .files[0]
                )
                .unwrap()
        );
        std::fs::write(&second, &bytes).unwrap();
        begin_load(&root);
        assert!(load_external(&root, &file_key(&file), None)
            .unwrap_err()
            .contains("2 matching sources"));
        // The loader knows the package that supplied the prefab. Its matching
        // dependency wins over another package/base bundle with the same CAB.
        std::fs::create_dir_all(root.join("mods")).unwrap();
        let package = root.join("mods/source.zipmod");
        {
            use std::io::Write;
            let mut zip = zip::ZipWriter::new(std::fs::File::create(&package).unwrap());
            let options = zip::write::SimpleFileOptions::default();
            zip.start_file("abdata/chara/scripts.unity3d", options)
                .unwrap();
            zip.write_all(&bytes).unwrap();
            zip.start_file("abdata/chara/source.unity3d", options)
                .unwrap();
            zip.write_all(&crate::gamefs::read(fixture().join("hair_source.unity3d")).unwrap())
                .unwrap();
            zip.finish().unwrap();
        }
        begin_load(&root);
        assert!(load_external(&root, &file_key(&file), Some(&package)).is_ok());
        let source = super::super::parse_bundle_cached(
            crate::mods::zipmod_one_unity3d(&package, "chara/source.unity3d")
                .unwrap()
                .1,
        );
        let skel = source
            .skeleton_for_prefab_name("p_acs_head_hanayure")
            .unwrap();
        source.configure_game_references_from_zip(&root, Some(&package));
        assert!(!source.dynamic_bones_for_skeleton(&skel).is_empty());
        source.configure_game_references_from_zip(&root, None);
        assert!(
            source.dynamic_bones_for_skeleton(&skel).is_empty(),
            "changing source scope must invalidate cached script/physics maps"
        );
        // Remove only this test's exact new file, then prove a stale duplicate
        // cannot survive the next load boundary or persistent-index refresh.
        std::fs::remove_file(&second).unwrap();
        std::fs::remove_file(&package).unwrap();
        begin_load(&root);
        assert!(load_external(&root, &file_key(&file), None).is_ok());
    }
    #[test]
    #[ignore = "original game bundles and generated fixture"]
    fn external_script_directory_streaming_control() {
        for path in [
            crate::test_support::game_path("/abdata/chara/oo_base.unity3d"),
            fixture().join("hair_scripts.unity3d"),
        ] {
            let bytes = crate::gamefs::read(&path).unwrap();
            let mut stream = BufferedEntry::new(std::io::Cursor::new(&bytes));
            let names = nodes(&mut stream).unwrap();
            let bundle = super::super::load_bundle_unbounded(bytes.clone()).unwrap();
            assert_eq!(
                names,
                bundle
                    .files
                    .iter()
                    .map(|f| f.name.clone())
                    .collect::<Vec<_>>()
            );
            assert!(
                stream.bytes.len() < bytes.len() / 2,
                "front metadata must not read payload blocks"
            );
            eprintln!(
                "directory {}: {} / {} bytes read",
                path.display(),
                stream.bytes.len(),
                bytes.len()
            );
        }
    }
}


/// The game file whose bundle holds serialized file `external_path` (a plain bundle file, not a zip entry).
pub(crate) fn external_source_path(root: &Path, external_path: &str, origin: Option<&Path>) -> Option<PathBuf> {
    let key = file_key(external_path);
    let catalog = catalog(root);
    let matches: Vec<_> = catalog.sources.iter()
        .flat_map(|s| s.entries.iter().filter(|(_, names)| names.iter().any(|n| file_key(n) == key)).map(move |(entry, _)| (s, entry)))
        .collect();
    let scoped: Vec<_> = matches.iter().copied().filter(|(s, _)| origin == Some(s.path.as_path())).collect();
    let matches = if scoped.is_empty() { matches } else { scoped };
    match matches.as_slice() { [(s, entry)] if entry.is_empty() => Some(s.path.clone()), _ => None }
}
