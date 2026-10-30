//! Game file gateway for the OK base library.
//!
//! Character generation observes the installation through these functions:
//! file reads, directory listings, existence checks, metadata and zipmod
//! entries. Outside a library session they are the std operations.
//!
//! Recording (`Record`): the real installation is read and every observation
//! under the game root is logged. Unity bundles are logged only by content id;
//! their data lives in the library as OK models (oklib queries). Other files
//! (configs, mod manifests, list CSVs, loose sideloader PNGs) are kept as data.
//!
//! Replay (`Replay`): the game root is the library directory; every
//! observation is answered from the log, Unity bundles as in-memory model
//! handles. Nothing of the installation is needed for shared resources.
//!
//! Fallback (replay only, opt-in when opening an original KKS card): zipmods the
//! library does not hold (card-specific mods, entries of a recorded zipmod that
//! were never converted) are read from real directories: the card's directory
//! (loose `.zipmod`s there count as `mods/`) and the zipmods of the game's `mods/`
//! that the card names by GUID. Only `mods/` is served: vanilla data always comes
//! from the library, so an original card needs the library plus its own zipmods.
use std::collections::BTreeMap;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, RwLock};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode { Record, Replay }

#[derive(Clone, Debug, serde::Deserialize)]
pub enum Entry {
    Missing,
    Dir,
    /// Exists (a plugin, an unread file); content never observed.
    Present(u64),
    Data(Vec<u8>),
    Model(crate::oklib::Id, u64),
    /// A replayed library's `Data` left in its mapped index (offset, length): the index's file contents are about a
    /// GB that every library session used to hold in memory. Written as `Data`, never read back as this.
    #[serde(skip_deserializing)]
    Mapped(usize, usize),
}

impl serde::Serialize for Entry {
    fn serialize<S: serde::Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        use serde::ser::SerializeTupleVariant;
        match self {
            Entry::Missing => s.serialize_unit_variant("Entry", 0, "Missing"),
            Entry::Dir => s.serialize_unit_variant("Entry", 1, "Dir"),
            Entry::Present(len) => s.serialize_newtype_variant("Entry", 2, "Present", len),
            Entry::Data(bytes) => s.serialize_newtype_variant("Entry", 3, "Data", bytes),
            Entry::Model(id, len) => {
                let mut t = s.serialize_tuple_variant("Entry", 4, "Model", 2)?;
                t.serialize_field(id)?;
                t.serialize_field(len)?;
                t.end()
            }
            Entry::Mapped(..) => s.serialize_newtype_variant("Entry", 3, "Data", &self.data().unwrap_or_default()),
        }
    }
}

impl Entry {
    /// Recorded file content (`Data`, or `Data` left in the mapped index).
    fn data(&self) -> Option<Vec<u8>> {
        match self {
            Entry::Data(bytes) => Some(bytes.clone()),
            Entry::Mapped(offset, len) => INDEX_MAP.read().unwrap().as_ref().and_then(|m| m.get(*offset..*offset + *len)).map(<[u8]>::to_vec),
            _ => None,
        }
    }
    fn is_data(&self) -> bool { matches!(self, Entry::Data(_) | Entry::Mapped(..)) }
    fn data_len(&self) -> Option<usize> {
        match self { Entry::Data(bytes) => Some(bytes.len()), Entry::Mapped(_, len) => Some(*len), _ => None }
    }
}

/// The decompressed index of the replayed library, mapped (`Entry::Mapped` points into it).
static INDEX_MAP: RwLock<Option<Arc<memmap2::Mmap>>> = RwLock::new(None);

/// Borrowed twins of the index types: the index deserialized in place from its mapped bytes.
#[derive(serde::Deserialize)]
enum EntryRef<'a> { Missing, Dir, Present(u64), Data(#[serde(borrow)] &'a [u8]), Model(crate::oklib::Id, u64) }
#[derive(serde::Deserialize)]
struct ZipIndexRef<'a> { names: Vec<(String, u64, u32, bool)>, #[serde(borrow)] entries: BTreeMap<usize, EntryRef<'a>> }
#[derive(serde::Deserialize)]
struct IndexRef<'a> {
    #[serde(borrow)] files: BTreeMap<String, EntryRef<'a>>,
    dirs: BTreeMap<String, Vec<(String, bool)>>,
    #[serde(borrow)] zips: BTreeMap<String, ZipIndexRef<'a>>,
}

/// A replayed library's index from a mapped, decompressed copy kept in the library's work area (made once per index
/// file, written through a map). None when that copy cannot be made or read: the index is then loaded whole.
fn mapped_index(library: &Path) -> Option<(Index, Arc<memmap2::Mmap>)> {
    let source = index_path(library);
    let meta = std::fs::metadata(&source).ok()?;
    let stamp = meta.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_nanos();
    let dir = library.join("work").join("index-cache");
    let cache = dir.join(format!("{}-{stamp}.raw", meta.len()));
    let compressed_file = std::fs::File::open(&source).ok()?;
    let compressed = unsafe { memmap2::Mmap::map(&compressed_file) }.ok()?;
    let raw_len = u32::from_le_bytes(compressed.get(..4)?.try_into().ok()?) as u64;
    if std::fs::metadata(&cache).map(|m| m.len()).ok() != Some(raw_len) {
        std::fs::create_dir_all(&dir).ok()?;
        let tmp = dir.join(format!("{}.{}.tmp", cache.file_name()?.to_string_lossy(), std::process::id()));
        let file = std::fs::OpenOptions::new().read(true).write(true).create(true).truncate(true).open(&tmp).ok()?;
        file.set_len(raw_len).ok()?;
        let mut out = unsafe { memmap2::MmapMut::map_mut(&file) }.ok()?;
        let written = lz4_flex::block::decompress_into(&compressed[4..], &mut out[..]).ok();
        let flushed = out.flush().is_ok();
        drop(out);
        drop(file);
        if written != Some(raw_len as usize) || !flushed { let _ = std::fs::remove_file(&tmp); return None; }
        if std::fs::rename(&tmp, &cache).is_err() { let _ = std::fs::remove_file(&tmp); }
        // Earlier copies belong to replaced index files.
        for old in std::fs::read_dir(&dir).ok()?.flatten() {
            if old.path() != cache && old.path().extension().is_some_and(|e| e == "raw") { let _ = std::fs::remove_file(old.path()); }
        }
    }
    drop(compressed);
    let file = std::fs::File::open(&cache).ok()?;
    let map = Arc::new(unsafe { memmap2::Mmap::map(&file) }.ok()?);
    if map.len() as u64 != raw_len { return None; }
    let parsed: IndexRef = bincode::deserialize(&map[..]).ok()?;
    let base = map.as_ptr() as usize;
    let entry = |e: EntryRef| match e {
        EntryRef::Missing => Entry::Missing,
        EntryRef::Dir => Entry::Dir,
        EntryRef::Present(len) => Entry::Present(len),
        EntryRef::Data(bytes) => Entry::Mapped(bytes.as_ptr() as usize - base, bytes.len()),
        EntryRef::Model(id, len) => Entry::Model(id, len),
    };
    let index = Index {
        files: parsed.files.into_iter().map(|(k, e)| (k, entry(e))).collect(),
        dirs: parsed.dirs,
        zips: parsed.zips.into_iter().map(|(k, z)| (k, ZipIndex { names: z.names, entries: z.entries.into_iter().map(|(i, e)| (i, entry(e))).collect() })).collect(),
    };
    Some((index, map))
}

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct ZipIndex {
    /// (name, uncompressed size, crc32, is_dir) per archive index.
    pub names: Vec<(String, u64, u32, bool)>,
    pub entries: BTreeMap<usize, Entry>,
}

#[derive(Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct Index {
    pub files: BTreeMap<String, Entry>,
    pub dirs: BTreeMap<String, Vec<(String, bool)>>,
    pub zips: BTreeMap<String, ZipIndex>,
}

struct State {
    mode: Mode,
    root: String,
    index: Mutex<Index>,
    dirty: std::sync::atomic::AtomicBool,
}

static STATE: RwLock<Option<Arc<State>>> = RwLock::new(None);

/// A real directory consulted in replay for what the library lacks.
#[derive(Clone, Debug)]
pub struct Fallback {
    /// A directory whose `mods/` is consulted (empty: none).
    pub root: PathBuf,
    /// The card's directory: `mods/<path>` also resolves to `<root>/<file name>`.
    pub loose_mods: bool,
    /// Single archives exposed as `mods/<file name>` (the zipmods a card names by GUID).
    pub files: Vec<PathBuf>,
    /// `root` is the game installation: its whole `mods/` answers only what the library cannot (a recorded
    /// zipmod whose model is not in this checkout), the library's own models keep precedence, and nothing it
    /// serves is taken into a card being recorded (the recording finds such zipmods through its gaps instead).
    pub installation: bool,
    /// The installation's settings (shadow ramp, UncensorSelector configuration) are read from `root` too. Not when
    /// only a zipmod folder was given: those come from the shared resources (the library, the project's Config).
    pub settings: bool,
}
static FALLBACK: RwLock<Vec<Fallback>> = RwLock::new(Vec::new());

/// Game settings files (logical keys) read from the installation when one is given (see `read`).
const INSTALLATION_SETTINGS: &[&str] = &["userdata/config/system.xml", "bepinex/config/com.deathweasel.bepinex.uncensorselector.cfg"];

/// The game installation exposed by the current fallback, if any.
pub fn fallback_installation() -> Option<PathBuf> {
    FALLBACK.read().unwrap().iter().find(|f| f.installation && f.settings && !f.root.as_os_str().is_empty()).map(|f| f.root.clone())
}
/// The card directory exposed by the current fallback, if any.
pub fn fallback_card_dir() -> Option<PathBuf> {
    FALLBACK.read().unwrap().iter().find(|f| f.loose_mods && !f.root.as_os_str().is_empty()).map(|f| f.root.clone())
}
/// The bytes the replayed OK card's pack holds for `path` (not the library's).
pub fn overlay_bytes(path: &Path) -> Option<Vec<u8>> {
    let st = state()?;
    let key = logical(&st, path)?;
    replay_overlay()?.index.lock().unwrap().files.get(&key).and_then(entry_bytes)
}
/// Takes `bytes` as `path`'s content into the card being recorded (no effect otherwise).
pub fn record_overlay_bytes(path: &Path, bytes: &[u8]) {
    let Some(st) = state() else { return };
    let Some(key) = logical(&st, path) else { return };
    overlay_file(&key, Entry::Data(bytes.to_vec()));
}

/// Card-specific observations layered over the library index (replay only), consulted first.
/// Recording: what the zipmod fallback serves while an original card becomes an OK card.
/// Replaying: the pack an OK card carries (`okLc`), so it opens from OK data alone.
pub struct Overlay { record: bool, index: Mutex<Index> }
static OVERLAY: RwLock<Option<Arc<Overlay>>> = RwLock::new(None);

/// Sets (or clears) the overlay; `record` collects fallback observations into it.
pub fn set_overlay(index: Option<Index>, record: bool) {
    FALLBACK_GENERATION.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    *OVERLAY.write().unwrap() = index.map(|index| Arc::new(Overlay { record, index: Mutex::new(index) }));
}
/// Removes the overlay and returns its observations.
pub fn take_overlay() -> Option<Index> {
    let o = OVERLAY.write().unwrap().take()?;
    let index = std::mem::take(&mut *o.index.lock().unwrap());
    Some(index)
}
/// The process-wide overlay (the one being recorded while an original card is converted).
pub fn process_overlay() -> Option<Arc<Overlay>> { OVERLAY.read().unwrap().clone() }
/// A replaying overlay over `index`, for one open's pack (oklib::PackScope).
pub fn replay_overlay_of(index: Index) -> Arc<Overlay> { Arc::new(Overlay { record: false, index: Mutex::new(index) }) }
/// The open's own pack on this thread (oklib::pack_scope), else the process-wide overlay.
fn overlay() -> Option<Arc<Overlay>> {
    if let Some(scope) = crate::oklib::pack_scope() { return Some(scope.overlay.clone()); }
    OVERLAY.read().unwrap().clone()
}
/// The overlay answers lookups only when replaying an OK card's pack. While recording it is write-only:
/// the card being converted must read the real zipmods, so that every query reaches the recorder.
fn replay_overlay() -> Option<Arc<Overlay>> { overlay().filter(|o| !o.record) }
fn recording_overlay() -> Option<Arc<Overlay>> { overlay().filter(|o| o.record) }
fn overlay_file(key: &str, entry: Entry) {
    if let Some(o) = recording_overlay() { o.index.lock().unwrap().files.insert(key.to_owned(), entry); }
}
fn content_entry(path: &Path, bytes: &[u8]) -> Entry {
    if is_unity(path, bytes) { Entry::Model(crate::oklib::content_id(bytes), bytes.len() as u64) } else { Entry::Data(bytes.to_vec()) }
}

/// Where a real archive's entry reads are logged.
#[derive(Clone)]
enum Sink { State(Arc<State>), Overlay(Arc<Overlay>) }
impl Sink {
    fn with<R>(&self, f: impl FnOnce(&mut Index) -> R) -> R {
        match self {
            Sink::State(st) => { let r = f(&mut st.index.lock().unwrap()); st.dirty.store(true, std::sync::atomic::Ordering::Relaxed); r }
            Sink::Overlay(o) => f(&mut o.index.lock().unwrap()),
        }
    }
}
static FALLBACK_USED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// Sets the fallback directories (empty: closed replay, the default).
pub fn set_fallback(roots: Vec<Fallback>) {
    *FALLBACK.write().unwrap() = roots;
    FALLBACK_GENERATION.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
}
/// Zipmods exposed by the current fallback (archives named by the card, plus a card directory marker).
pub fn fallback_summary() -> (usize, usize) {
    let roots = FALLBACK.read().unwrap();
    (roots.iter().map(|f| f.files.len()).sum(), roots.iter().filter(|f| f.loose_mods).count())
}
/// Changes whenever the fallback or the overlay does (a key part of memos of the tree it serves).
pub fn fallback_generation() -> u64 { FALLBACK_GENERATION.load(std::sync::atomic::Ordering::Relaxed) }
/// Identity of what the fallback can serve (each archive's path, size and time; the card directory's
/// archives): part of the cache key of sessions built with it. Worked out once per fallback generation: in-process
/// memos key on it (MaterialEditor shader registrations, library memos), ~1000 times a card conversion at ~1 ms each
/// (a stat of every archive and a listing of the card directory).
pub fn fallback_identity() -> String {
    static LAST: Mutex<Option<(u64, String)>> = Mutex::new(None);
    let generation = FALLBACK_GENERATION.load(std::sync::atomic::Ordering::Relaxed);
    if let Some((g, identity)) = LAST.lock().unwrap_or_else(|e| e.into_inner()).as_ref() { if *g == generation { return identity.clone(); } }
    let identity = fallback_identity_now();
    *LAST.lock().unwrap_or_else(|e| e.into_inner()) = Some((generation, identity.clone()));
    identity
}
fn fallback_identity_now() -> String {
    let roots = FALLBACK.read().unwrap();
    let mut out = String::new();
    let mut roots_seen = Vec::new();
    let mut add = |p: &Path| {
        let m = std::fs::metadata(p).ok();
        out.push_str(&format!("{}|{}|{:?};", normalize(p), m.as_ref().map_or(0, |m| m.len()), m.and_then(|m| m.modified().ok())));
    };
    for f in roots.iter() {
        for p in &f.files { add(p); }
        if f.loose_mods {
            let mut loose: Vec<PathBuf> = std::fs::read_dir(&f.root).into_iter().flatten().flatten().map(|e| e.path())
                .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("zipmod") || e.eq_ignore_ascii_case("zip"))).collect();
            loose.sort();
            for p in &loose { add(p); }
        }
        if f.installation && !f.root.as_os_str().is_empty() {
            roots_seen.push(format!("{} {:016x}", normalize(&f.root), crate::list::zip_list_sig(&f.root.join("mods"))));
        } else if !f.root.as_os_str().is_empty() && !f.loose_mods { roots_seen.push(normalize(&f.root)); }
    }
    for r in roots_seen { out.push_str(&format!("root {r};")); }
    drop(roots);
    // The UncensorSelector configuration a library session would read (installation, card directory, project).
    for p in crate::mods::uncensor_config_candidates() {
        let m = std::fs::metadata(&p).ok();
        out.push_str(&format!("cfg {}|{}|{:?};", normalize(&p), m.as_ref().map_or(0, |m| m.len()), m.and_then(|m| m.modified().ok())));
    }
    out
}
pub fn reset_fallback_used() {
    // Only this thread's own open (at an open's start its pack is not mounted yet: never another card's flag).
    if let Some(scope) = crate::oklib::current_pack_scope() { scope.fallback_used.store(false, std::sync::atomic::Ordering::Relaxed); }
    FALLBACK_USED.store(false, std::sync::atomic::Ordering::Relaxed);
}
/// The open on this thread read installed zipmods (its own flag when it has a pack, so cards opening at once do not mix).
fn mark_fallback_used() {
    if let Some(scope) = crate::oklib::pack_scope() { scope.fallback_used.store(true, std::sync::atomic::Ordering::Relaxed); }
    FALLBACK_USED.store(true, std::sync::atomic::Ordering::Relaxed);
}
/// Whether anything was served from a fallback directory since `reset_fallback_used`.
pub fn fallback_used() -> bool {
    // (the open on this thread: after it, as at the end of session_open, the process-wide flag)
    if let Some(scope) = crate::oklib::current_pack_scope() { return scope.fallback_used.load(std::sync::atomic::Ordering::Relaxed); }
    FALLBACK_USED.load(std::sync::atomic::Ordering::Relaxed)
}

fn is_mods_key(key: &str) -> bool { key == "mods" || key.starts_with("mods/") }

/// Real paths that may serve `key`, each with whether it is the installation's (see `Fallback::installation`).
/// While a card is recorded the installation serves nothing: what the card needs from it is found through gaps.
fn fallback_candidates(key: &str) -> Vec<(PathBuf, bool)> {
    let roots = FALLBACK.read().unwrap();
    let mut out = Vec::new();
    if !is_mods_key(key) { return out; }
    let recording = recording_overlay().is_some();
    let file_name = key.strip_prefix("mods/").map(|rest| rest.rsplit('/').next().unwrap_or(rest));
    for f in roots.iter() {
        if f.installation && recording { continue; }
        if !f.root.as_os_str().is_empty() {
            out.push((f.root.join(key), f.installation));
            if let (true, Some(name)) = (f.loose_mods, file_name) { out.push((f.root.join(name), false)); }
        }
        if let Some(name) = file_name {
            out.extend(f.files.iter().filter(|p| p.file_name().is_some_and(|n| n.to_string_lossy().eq_ignore_ascii_case(name))).map(|p| (p.clone(), false)));
        }
    }
    out
}
/// First real file or directory for `key` in the fallback directories.
fn fallback_path(key: &str, want_dir: Option<bool>) -> Option<(PathBuf, std::fs::Metadata)> {
    fallback_source(key, want_dir).map(|(p, m, _)| (p, m))
}
/// `fallback_path` plus whether the path is the installation's.
fn fallback_source(key: &str, want_dir: Option<bool>) -> Option<(PathBuf, std::fs::Metadata, bool)> {
    for (path, installation) in fallback_candidates(key) {
        if let Ok(m) = std::fs::metadata(&path) {
            if want_dir.is_none_or(|d| d == m.is_dir()) {
                mark_fallback_used();
                return Some((path, m, installation));
            }
        }
    }
    None
}
/// Real listings of `key` in the fallback directories (the card directory adds its zipmods to `mods`).
fn fallback_listing(key: &str) -> Option<Vec<(String, bool)>> {
    if !is_mods_key(key) { return None; }
    let roots = FALLBACK.read().unwrap().clone();
    let mut out: Vec<(String, bool)> = Vec::new();
    let mut found = false;
    let mut add = |name: String, dir: bool| { if !out.iter().any(|(n, _)| n.eq_ignore_ascii_case(&name)) { out.push((name, dir)); } };
    // Archives the library already records elsewhere under mods/ are not listed twice. Only the mods root lists
    // fallback archives: building this set for every listed directory cost ~1.3 s per walk of the mods tree.
    let recorded: std::collections::HashSet<String> = if key != "mods" { Default::default() } else {
        state().map(|st| st.index.lock().unwrap().zips.keys()
            .filter_map(|k| k.rsplit('/').next().map(str::to_owned)).collect()).unwrap_or_default() };
    let mut add_zip = |name: String, add: &mut dyn FnMut(String, bool)| {
        let lower = name.to_ascii_lowercase();
        if (lower.ends_with(".zipmod") || lower.ends_with(".zip")) && !recorded.contains(&lower) { add(name, false); }
    };
    let recording = recording_overlay().is_some();
    for f in &roots {
        if f.installation && recording { continue; }
        if !f.root.as_os_str().is_empty() {
            if let Ok(rd) = std::fs::read_dir(f.root.join(key)) {
                found = true;
                for e in rd.flatten() { add(e.file_name().to_string_lossy().into_owned(), e.file_type().map(|t| t.is_dir()).unwrap_or(false)); }
            }
        }
        if key == "mods" {
            if f.loose_mods {
                if let Ok(rd) = std::fs::read_dir(&f.root) {
                    for e in rd.flatten() {
                        if e.file_type().is_ok_and(|t| t.is_file()) { found = true; add_zip(e.file_name().to_string_lossy().into_owned(), &mut add); }
                    }
                }
            }
            for p in &f.files {
                if let Some(name) = p.file_name() { found = true; add_zip(name.to_string_lossy().into_owned(), &mut add); }
            }
        }
    }
    if found { Some(out) } else { None }
}

fn state() -> Option<Arc<State>> { STATE.read().unwrap().clone() }

/// True when `root` is the OK base library being replayed: a session opened from it reads OK data only (and the
/// card's own zipmods through the fallback), never project copies of Unity files such as the uncensor pack.
pub fn serves_from_library(root: &Path) -> bool {
    state().is_some_and(|st| st.mode == Mode::Replay && normalize(root) == st.root)
}

pub fn normalize(path: &Path) -> String {
    // `\` is `/`, a leading `//?/` goes. (Every file query normalizes a path: one string and the part list.)
    let lossy = path.to_string_lossy();
    let sep = |c: char| c == '/' || c == '\\';
    let s: &str = &lossy;
    let mut head = s.chars();
    let s = match (head.next(), head.next(), head.next(), head.next()) {
        (Some(a), Some(b), Some('?'), Some(d)) if sep(a) && sep(b) && sep(d) => &s[4..],
        _ => s,
    };
    let mut parts: Vec<&str> = Vec::new();
    for part in s.split(sep) {
        match part {
            "" | "." => { if parts.is_empty() && s.starts_with(sep) { parts.push(""); } }
            ".." => { parts.pop(); }
            p => parts.push(p),
        }
    }
    let mut out = parts.join("/");
    out.make_ascii_lowercase();
    out
}

fn logical(st: &State, path: &Path) -> Option<String> {
    let n = normalize(path);
    if n == st.root { return Some(String::new()); }
    let rest = n.strip_prefix(&st.root)?.strip_prefix('/')?;
    // Library bookkeeping and the extractor's own caches are not game data.
    if rest.split('/').any(|p| p == "oklib.json" || p.starts_with("calls.ok") || p.starts_with("sources.ok")) { return None; }
    // The library's own working area (verification outputs, packs being recorded) is not game data.
    if rest == "work" || rest.starts_with("work/") || rest == "models" || rest.starts_with("models/") { return None; }
    // Cards and coordinates are the user's own data, never library content.
    if rest.starts_with("userdata/") && !rest.starts_with("userdata/config") { return None; }
    Some(rest.to_owned())
}

pub fn index_path(dir: &Path) -> PathBuf { dir.join("sources.okidx") }

pub fn activate(mode: Mode, root: &Path, library: &Path) -> Result<(), String> {
    if mode == Mode::Replay {
        if let Some((index, map)) = mapped_index(library) {
            *INDEX_MAP.write().unwrap() = Some(map);
            *STATE.write().unwrap() = Some(Arc::new(State { mode, root: normalize(root), index: Mutex::new(index), dirty: false.into() }));
            return Ok(());
        }
    }
    let index = match std::fs::read(index_path(library)) {
        Ok(bytes) => {
            let raw = lz4_flex::block::decompress_size_prepended(&bytes).map_err(|e| e.to_string())?;
            bincode::deserialize(&raw).map_err(|e| e.to_string())?
        }
        Err(_) if mode == Mode::Record => Index::default(),
        Err(e) => return Err(format!("{}: {e}", index_path(library).display())),
    };
    *STATE.write().unwrap() = Some(Arc::new(State { mode, root: normalize(root), index: Mutex::new(index), dirty: false.into() }));
    Ok(())
}

pub fn save(library: &Path) -> Result<(), String> {
    let Some(st) = state() else { return Ok(()); };
    if st.mode != Mode::Record || !st.dirty.swap(false, std::sync::atomic::Ordering::Relaxed) { return Ok(()); }
    let raw = bincode::serialize(&*st.index.lock().unwrap()).map_err(|e| e.to_string())?;
    let tmp = index_path(library).with_extension("tmp");
    std::fs::write(&tmp, lz4_flex::block::compress_prepend_size(&raw)).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, index_path(library)).map_err(|e| e.to_string())
}

pub fn deactivate() { *STATE.write().unwrap() = None; }

pub fn stats() -> (usize, usize, usize) {
    state().map(|st| { let i = st.index.lock().unwrap(); (i.files.len(), i.dirs.len(), i.zips.len()) }).unwrap_or_default()
}

/// A listed ancestor directory without the next path component proves the
/// path does not exist in the installation.
fn proven_absent(index: &Index, key: &str) -> bool {
    let parts: Vec<&str> = key.split('/').collect();
    for depth in (0..parts.len()).rev() {
        let dir = parts[..depth].join("/");
        if let Some(children) = index.dirs.get(&dir) {
            let next = parts[depth];
            return match children.iter().find(|(n, _)| n.eq_ignore_ascii_case(next)) {
                None => true,
                Some((_, is_dir)) => !*is_dir && depth + 1 < parts.len(),
            };
        }
    }
    false
}

fn missing(what: &str, logical: &str) {
    if let Some(store) = crate::oklib::active() { store.note_missing(format!("GAMEFS {what} {logical}")); }
}

fn not_found() -> io::Error { io::ErrorKind::NotFound.into() }

fn record(st: &State, key: String, entry: Entry) {
    let mut index = st.index.lock().unwrap();
    // Content beats presence: never downgrade an observed file to Present.
    if let (Some(Entry::Data(_) | Entry::Mapped(..) | Entry::Model(..)), Entry::Present(_)) = (index.files.get(&key), &entry) { return; }
    index.files.insert(key, entry);
    st.dirty.store(true, std::sync::atomic::Ordering::Relaxed);
}

fn is_unity(path: &Path, bytes: &[u8]) -> bool {
    crate::oklib::is_unity_bundle(bytes) || path.file_name().is_some_and(|n| n.eq_ignore_ascii_case("globalgamemanagers"))
}

fn entry_bytes(entry: &Entry) -> Option<Vec<u8>> {
    match entry {
        Entry::Data(_) | Entry::Mapped(..) => entry.data(),
        Entry::Model(id, _) => Some(crate::oklib::token(id)),
        _ => None,
    }
}

/// The extractor's own derived caches under the game root (`.ok_*`) are
/// bypassed in library sessions: record and replay derive the same data
/// from the observed sources instead.
fn is_cache(key: &str) -> bool { key.rsplit('/').next().is_some_and(|n| n.starts_with(".ok_")) }

static WATCH: RwLock<Option<String>> = RwLock::new(None);
static WATCHED: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
/// Count every access through this module under `root` (None stops). Acceptance of OK cards: a card reopened from
/// OK data must not touch the original game installation.
pub fn watch_root(root: Option<&Path>) { *WATCH.write().unwrap() = root.map(normalize); WATCHED.store(0, std::sync::atomic::Ordering::Relaxed); }
/// Accesses under the watched root since `watch_root`.
pub fn watched_accesses() -> usize { WATCHED.load(std::sync::atomic::Ordering::Relaxed) }
fn note_access(path: &Path) {
    if let Some(root) = WATCH.read().unwrap().as_deref() {
        let n = normalize(path);
        if n == root || n.starts_with(&format!("{root}/")) { WATCHED.fetch_add(1, std::sync::atomic::Ordering::Relaxed); }
    }
}

pub fn read<P: AsRef<Path>>(path: P) -> io::Result<Vec<u8>> {let _spot=crate::profile::spot("read");
    let path = path.as_ref();
    note_access(path);
    let Some(st) = state() else { return std::fs::read(path); };
    let Some(key) = logical(&st, path) else { return std::fs::read(path); };
    if is_cache(&key) { return Err(not_found()); }
    match st.mode {
        Mode::Record => {
            let result = std::fs::read(path);
            match &result {
                Ok(bytes) if is_unity(path, bytes) => record(&st, key, Entry::Model(crate::oklib::content_id(bytes), bytes.len() as u64)),
                Ok(bytes) => record(&st, key, Entry::Data(bytes.clone())),
                Err(e) if e.kind() == io::ErrorKind::NotFound => record(&st, key, Entry::Missing),
                Err(_) => {}
            }
            result
        }
        Mode::Replay => {
            if let Some(o) = replay_overlay() { if let Some(bytes) = o.index.lock().unwrap().files.get(&key).and_then(entry_bytes) { return Ok(bytes); } }
            // An OK card's open: a third-party mod's file only from its own pack (okstrict.rs).
            if crate::okstrict::active().is_some() && !crate::okstrict::library_path(&key) {
                let entry = st.index.lock().unwrap().files.get(&key).map(portable);
                if let Some(entry) = &entry {
                    if let Some(bytes) = entry_bytes(entry) {
                        if crate::okstrict::complete(|a| { a.index.files.insert(key.clone(), entry.clone()); }) { return Ok(bytes); }
                    }
                }
                // Converting: what the library does not hold is the zipmods' (read below and recorded).
                if !crate::okstrict::converting() {
                    if entry.is_some() { missing("read", &key); }
                    return Err(not_found());
                }
            }
            // The game's settings (shadow ramp, UncensorSelector) are the installation's when one is given, not the
            // ones the library recorded; a card being made carries them.
            if INSTALLATION_SETTINGS.contains(&key.as_str()) {
                if let Some(bytes) = fallback_installation().and_then(|g| std::fs::read(g.join(&key)).ok()) {
                    overlay_file(&key, Entry::Data(bytes.clone()));
                    return Ok(bytes);
                }
            }
            let index = st.index.lock().unwrap();
            if let Some(bytes) = index.files.get(&key).and_then(entry_bytes) { return Ok(bytes); }
            // A directory read as a file (a path built from an empty bundle name): not found, which is the answer.
            if index.dirs.contains_key(&key) { return Err(not_found()); }
            let known = index.files.get(&key).cloned();
            let absent = known.is_none() && proven_absent(&index, &key);
            drop(index);
            if let Some((real, _)) = fallback_path(&key, Some(false)) {
                let result = std::fs::read(&real);
                if let Ok(bytes) = &result { overlay_file(&key, content_entry(&real, bytes)); }
                return result;
            }
            match known {
                Some(Entry::Present(_)) => { missing("read", &key); Err(not_found()) }
                Some(_) => Err(not_found()),
                None => { if !absent { missing("read", &key); } Err(not_found()) }
            }
        }
    }
}

/// `read` for bundle sources: a large file of the real installation (no library active for it) is mapped instead
/// of copied into memory. Library content and recording go through `read`.
pub(crate) fn read_source<P: AsRef<Path>>(path: P) -> io::Result<crate::unity::SourceBytes> {
    let path = path.as_ref();
    let direct = match state() { None => true, Some(st) => logical(&st, path).is_none() };
    if direct && std::fs::metadata(path).is_ok_and(|m| m.len() >= crate::unity::MAP_AT_LEAST) {
        note_access(path);
        return crate::unity::SourceBytes::file(path);
    }
    read(path).map(crate::unity::SourceBytes::from)
}

pub fn read_to_string<P: AsRef<Path>>(path: P) -> io::Result<String> {
    String::from_utf8(read(path)?).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
}

#[derive(Clone, Copy, Debug)]
pub struct FileKind { dir: bool }
impl FileKind {
    pub fn is_dir(&self) -> bool { self.dir }
    pub fn is_file(&self) -> bool { !self.dir }
    pub fn is_symlink(&self) -> bool { false }
}

#[derive(Clone, Debug)]
pub struct Metadata { len: u64, dir: bool, modified: std::time::SystemTime, created: Option<std::time::SystemTime>, attributes: u32 }
impl Metadata {
    pub fn created(&self) -> io::Result<std::time::SystemTime> { self.created.ok_or_else(|| io::ErrorKind::Unsupported.into()) }
    /// Windows file attributes of a real file (0 for library sources).
    pub fn file_attributes(&self) -> u32 { self.attributes }
    pub fn len(&self) -> u64 { self.len }
    pub fn is_dir(&self) -> bool { self.dir }
    pub fn is_file(&self) -> bool { !self.dir }
    pub fn file_type(&self) -> FileKind { FileKind { dir: self.dir } }
    pub fn modified(&self) -> io::Result<std::time::SystemTime> { Ok(self.modified) }
}
fn from_std(m: std::fs::Metadata) -> Metadata {
    #[cfg(windows)] let attributes = { use std::os::windows::fs::MetadataExt; m.file_attributes() };
    #[cfg(not(windows))] let attributes = 0;
    Metadata { len: m.len(), dir: m.is_dir(), modified: m.modified().unwrap_or(std::time::UNIX_EPOCH), created: m.created().ok(), attributes }
}
/// Library sources never change during a replay; a fixed time keeps every
/// size/mtime cache stable.
fn library_time() -> std::time::SystemTime { std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_700_000_000) }

fn observe(st: &State, path: &Path, key: &str) -> Option<Entry> {
    if is_cache(key) { return Some(Entry::Missing); }
    match st.mode {
        Mode::Record => {
            let entry = match std::fs::metadata(path) {
                Ok(m) if m.is_dir() => Entry::Dir,
                Ok(m) => Entry::Present(m.len()),
                Err(_) => Entry::Missing,
            };
            record(st, key.to_owned(), entry.clone());
            Some(entry)
        }
        Mode::Replay => {
            let library = observe_library(st, key);
            if !matches!(library, Some(Entry::Missing) | None) { return library; }
            if let Some((_, m)) = fallback_path(key, None) {
                let entry = if m.is_dir() { Entry::Dir } else { Entry::Present(m.len()) };
                // Never downgrade content the overlay already holds.
                if let Some(o) = recording_overlay() { o.index.lock().unwrap().files.entry(key.to_owned()).or_insert(entry.clone()); }
                return Some(entry);
            }
            if library.is_none() { missing("stat", key); }
            library
        }
    }
}

fn observe_library(st: &State, key: &str) -> Option<Entry> {
    if let Some(o) = replay_overlay() {
        if let Some(entry) = observe_in(&o.index.lock().unwrap(), key) { if !matches!(entry, Entry::Missing) { return Some(entry); } }
    }
    let index = st.index.lock().unwrap();
    let found = observe_in(&index, key).or_else(|| proven_absent(&index, key).then_some(Entry::Missing));
    drop(index);
    // An OK card's open: what exists of a third-party mod is what its own pack holds (okstrict.rs).
    if let Some(entry) = found.as_ref().filter(|e| !matches!(e, Entry::Missing)) {
        if crate::okstrict::active().is_some() && !crate::okstrict::library_path(key) {
            let entry = portable(entry);
            if !crate::okstrict::complete(|a| { a.index.files.entry(key.to_owned()).or_insert(entry); }) {
                missing("stat", key);
                return Some(Entry::Missing);
            }
        }
    }
    found
}

/// A library entry as a card's pack keeps it (content left in the mapped index copied out).
fn portable(entry: &Entry) -> Entry {
    match entry { Entry::Mapped(..) => Entry::Data(entry.data().unwrap_or_default()), e => e.clone() }
}

fn observe_in(index: &Index, key: &str) -> Option<Entry> {
    {
        {
            if let Some(entry) = index.files.get(key) { return Some(entry.clone()); }
            if index.dirs.contains_key(key) || index.zips.contains_key(key) {
                return Some(if index.zips.contains_key(key) { Entry::Present(0) } else { Entry::Dir });
            }
            // A listed child of an observed directory.
            let (parent, name) = key.rsplit_once('/').unwrap_or(("", key));
            if let Some(children) = index.dirs.get(parent) {
                return Some(match children.iter().find(|(n, _)| n.eq_ignore_ascii_case(name)) {
                    Some((_, true)) => Entry::Dir,
                    Some((_, false)) => Entry::Present(0),
                    None => Entry::Missing,
                });
            }
            None
        }
    }
}

pub fn metadata<P: AsRef<Path>>(path: P) -> io::Result<Metadata> {
    note_access(path.as_ref());
    let path = path.as_ref();
    let Some(st) = state() else { return std::fs::metadata(path).map(from_std); };
    let Some(key) = logical(&st, path) else { return std::fs::metadata(path).map(from_std); };
    if st.mode == Mode::Record { observe(&st, path, &key); return std::fs::metadata(path).map(from_std); }
    match observe(&st, path, &key) {
        Some(Entry::Dir) => Ok(Metadata { len: 0, dir: true, modified: library_time(), created: None, attributes: 0 }),
        Some(Entry::Present(len)) | Some(Entry::Model(_, len)) => Ok(Metadata { len, dir: false, modified: library_time(), created: None, attributes: 0 }),
        Some(e) if e.is_data() => Ok(Metadata { len: e.data_len().unwrap_or(0) as u64, dir: false, modified: library_time(), created: None, attributes: 0 }),
        _ => Err(not_found()),
    }
}

/// Like `std::fs::symlink_metadata` for real files: a junction or symlink reports itself (reparse attribute set)
/// instead of its target. Library sources have no links, so a replay answers as `metadata`.
pub fn symlink_metadata<P: AsRef<Path>>(path: P) -> io::Result<Metadata> {
    note_access(path.as_ref());
    let path = path.as_ref();
    let Some(st) = state() else { return std::fs::symlink_metadata(path).map(from_std); };
    let Some(key) = logical(&st, path) else { return std::fs::symlink_metadata(path).map(from_std); };
    if st.mode == Mode::Record { observe(&st, path, &key); return std::fs::symlink_metadata(path).map(from_std); }
    metadata(path)
}

pub fn exists(path: &Path) -> bool { metadata(path).is_ok() }
pub fn is_file(path: &Path) -> bool { metadata(path).is_ok_and(|m| m.is_file()) }
pub fn is_dir(path: &Path) -> bool { metadata(path).is_ok_and(|m| m.is_dir()) }

/// `Path::is_file()` etc. through the gateway (`path.ok_is_file()`).
pub trait GamePath {
    fn ok_is_file(&self) -> bool;
    fn ok_exists(&self) -> bool;
    fn ok_is_dir(&self) -> bool;
}
impl GamePath for Path {
    fn ok_is_file(&self) -> bool { is_file(self) }
    fn ok_exists(&self) -> bool { exists(self) }
    fn ok_is_dir(&self) -> bool { is_dir(self) }
}
impl GamePath for str {
    fn ok_is_file(&self) -> bool { is_file(Path::new(self)) }
    fn ok_exists(&self) -> bool { exists(Path::new(self)) }
    fn ok_is_dir(&self) -> bool { is_dir(Path::new(self)) }
}
macro_rules! kind_path { ($($t:ty),*) => {$(impl GamePath for $t {
    fn ok_is_file(&self) -> bool { self.is_file() }
    fn ok_exists(&self) -> bool { true }
    fn ok_is_dir(&self) -> bool { self.is_dir() }
})*} }
kind_path!(FileKind, Metadata, std::fs::FileType, std::fs::Metadata);

pub fn canonicalize<P: AsRef<Path>>(path: P) -> io::Result<PathBuf> {
    let path = path.as_ref();
    if let Some(st) = state() {
        if let Some(key) = logical(&st, path) {
            if st.mode == Mode::Replay {
                return if exists(path) { Ok(PathBuf::from(normalize(path))) } else { Err(not_found()) };
            }
            observe(&st, path, &key);
        }
    }
    std::fs::canonicalize(path)
}

pub struct DirEntry { path: PathBuf, dir: bool }
impl DirEntry {
    pub fn path(&self) -> PathBuf { self.path.clone() }
    pub fn file_name(&self) -> std::ffi::OsString { self.path.file_name().unwrap_or_default().to_owned() }
    pub fn file_type(&self) -> io::Result<FileKind> { Ok(FileKind { dir: self.dir }) }
    /// The entry itself, not a link target (as `std::fs::DirEntry::metadata`).
    pub fn metadata(&self) -> io::Result<Metadata> { symlink_metadata(&self.path) }
}

pub fn read_dir<P: AsRef<Path>>(path: P) -> io::Result<std::vec::IntoIter<io::Result<DirEntry>>> {
    note_access(path.as_ref());
    let path = path.as_ref();
    let real = |path: &Path| -> io::Result<Vec<(String, bool)>> {
        let mut v = Vec::new();
        for e in std::fs::read_dir(path)? {
            let e = e?;
            v.push((e.file_name().to_string_lossy().into_owned(), e.file_type().map(|t| t.is_dir()).unwrap_or(false)));
        }
        Ok(v)
    };
    let list = match state().and_then(|st| logical(&st, path).map(|k| (st, k))) {
        None => real(path)?,
        Some((st, key)) => match st.mode {
            Mode::Record => {
                let list = real(path);
                if let Ok(list) = &list {
                    st.index.lock().unwrap().dirs.insert(key, list.clone());
                    st.dirty.store(true, std::sync::atomic::Ordering::Relaxed);
                }
                list?
            }
            Mode::Replay => {
                let overlaid = replay_overlay().and_then(|o| o.index.lock().unwrap().dirs.get(&key).cloned());
                // An OK card's open never lists the library's mods tree (okstrict.rs): only listings its pack holds.
                if overlaid.is_none() && is_mods_key(&key) && crate::okstrict::active().is_some() && !crate::okstrict::converting() {
                    missing("read_dir", &key);
                    return Err(not_found());
                }
                let library = overlaid.or_else(|| st.index.lock().unwrap().dirs.get(&key).cloned());
                let mut added = false;
                let list = match (library, fallback_listing(&key)) {
                    (Some(mut list), Some(real)) => {
                        for (name, dir) in real { if !list.iter().any(|(n, _)| n.eq_ignore_ascii_case(&name)) { list.push((name, dir)); added = true; } }
                        list
                    }
                    (Some(list), None) => list,
                    (None, Some(real)) => { added = true; real }
                    (None, None) => { missing("read_dir", &key); return Err(not_found()); }
                };
                if added {
                    mark_fallback_used();
                    if let Some(o) = recording_overlay() { o.index.lock().unwrap().dirs.insert(key.clone(), list.clone()); }
                }
                list
            }
        },
    };
    Ok(list.into_iter().map(|(name, dir)| Ok(DirEntry { path: path.join(name), dir })).collect::<Vec<_>>().into_iter())
}

// ---------------------------------------------------------------- zipmods

pub enum Zip {
    Real(zip::ZipArchive<std::io::BufReader<std::fs::File>>, Option<(Sink, String)>),
    /// A recorded zipmod; entries without recorded content come from the real archive (fallback) when present.
    /// The flag marks the installation's archive: its entries yield to models this checkout has.
    Library(Arc<State>, String, ZipIndex, Option<(PathBuf, bool)>),
}

/// The library's record of the archive at `key` under another folder: same file name and the same entries.
fn library_alias(st: &State, key: &str, own: Option<&ZipIndex>) -> Option<ZipIndex> {
    let name = key.rsplit('/').next()?;
    let index = st.index.lock().unwrap();
    index.zips.iter().find(|(k, z)| k.rsplit('/').next() == Some(name) && own.is_none_or(|o| o.names == z.names)).map(|(_, z)| z.clone())
}

pub fn open_zip<P: AsRef<Path>>(path: P) -> Option<Zip> {let _spot=crate::profile::spot("open_zip");
    note_access(path.as_ref());
    let path = path.as_ref();
    let open_real = |path: &Path, rec: Option<(Sink, String)>| -> Option<Zip> {
        let file = std::fs::File::open(path).ok()?;
        let mut zip = zip::ZipArchive::new(std::io::BufReader::new(file)).ok()?;
        if let Some((sink, key)) = &rec {
            sink.with(|index| if !index.zips.contains_key(key) {
                let mut names = Vec::with_capacity(zip.len());
                for i in 0..zip.len() {
                    names.push(match zip.by_index_raw(i) {
                        Ok(f) => (f.name().to_owned(), f.size(), f.crc32(), f.is_dir()),
                        Err(_) => (String::new(), 0, 0, false),
                    });
                }
                index.zips.insert(key.clone(), ZipIndex { names, entries: BTreeMap::new() });
            });
        }
        Some(Zip::Real(zip, rec))
    };
    let Some(st) = state() else { return open_real(path, None); };
    let Some(key) = logical(&st, path) else { return open_real(path, None); };
    match st.mode {
        Mode::Record => open_real(path, Some((Sink::State(st), key))),
        Mode::Replay => {
            // Library entries, completed by the overlay's (an OK card's pack, or what was read for one).
            let library = st.index.lock().unwrap().zips.get(&key).cloned();
            let overlaid = replay_overlay().and_then(|o| o.index.lock().unwrap().zips.get(&key).cloned());
            // An OK card's open: a third-party archive from its own pack, with the library's shared entries (list
            // bundles, a shared bundle in it) (okstrict.rs). Sealing reads the library's and copies what is read.
            // (converting: an archive the library does not record is the zipmods', below)
            let converting = crate::okstrict::converting();
            if is_mods_key(&key) && crate::okstrict::active().is_some() && !crate::okstrict::library_zip(&key) && !(converting && library.is_none()) {
                // A zipmod the card named was made reachable at mods/<file name> while it was converted; the library
                // records the same archive at its own path (sealing copies its entries from there).
                let library = library.or_else(|| if crate::okstrict::completing() { library_alias(&st, &key, overlaid.as_ref()) } else { None });
                let index = match (overlaid, library) {
                    (own, Some(lib)) if crate::okstrict::complete(|a| { a.index.zips.entry(key.clone()).or_insert_with(|| ZipIndex { names: lib.names.clone(), entries: BTreeMap::new() }); }) => {
                        let mut index = lib;
                        if let Some(own) = own { index.entries.extend(own.entries); }
                        index
                    }
                    (Some(mut own), lib) => {
                        for (i, e) in lib.map(|l| l.entries).unwrap_or_default() {
                            if !own.entries.contains_key(&i) && own.names.get(i).is_some_and(|n| crate::okstrict::library_entry(&key, &n.0)) { own.entries.insert(i, e); }
                        }
                        own
                    }
                    (None, _) => { missing("zip", &key); return None; }
                };
                // Converting: entries the library never converted come from the zipmod (recorded).
                let real = if converting { fallback_source(&key, Some(false)).map(|(p, _, installation)| (p, installation)) } else { None };
                return Some(Zip::Library(st, key, index, real));
            }
            let found = match (library, overlaid) {
                (Some(mut index), Some(extra)) => { index.entries.extend(extra.entries); Some(index) }
                (a, b) => a.or(b),
            };
            let real = || fallback_source(&key, Some(false)).map(|(p, _, installation)| (p, installation));
            match found {
                Some(index) => { let real = real(); Some(Zip::Library(st, key, index, real)) }
                None => match real() {
                    Some((real, _)) => open_real(&real, recording_overlay().map(|o| (Sink::Overlay(o), key))),
                    None => { missing("zip", &key); None }
                },
            }
        }
    }
}

pub struct ZipEntry<'a> {
    name: String,
    size: u64,
    crc: u32,
    dir: bool,
    source: ZipSource<'a>,
}
enum ZipSource<'a> {
    Real(zip::read::ZipFile<'a>, Option<(Sink, String, usize)>),
    Library(Option<io::Cursor<Vec<u8>>>, Arc<State>, Label<'a>),
    /// A recorded zipmod's entry whose content comes from the real archive: inflated on the first read, so walking
    /// an archive's entries for their names (manifest, suffix and list scans) no longer inflates every entry.
    Pending(PendingEntry<'a>, Arc<State>, Label<'a>),
}

/// A library entry's `zip|entry` label, made when a gap is reported (opening an entry is mostly a walk over names).
#[derive(Clone, Copy)]
struct Label<'a> { key: &'a str, name: &'a str }

impl Label<'_> {
    fn text(&self) -> String { format!("{}|{}", self.key, self.name.replace('\\', "/").to_ascii_lowercase()) }
}

/// (`names`: the archive's entry list for a recording, copied when the entry is loaded.)
struct PendingEntry<'a> { real: &'a Path, name: &'a str, key: &'a str, index: usize, names: Option<&'a [(String, u64, u32, bool)]> }

impl PendingEntry<'_> {
    fn load(&self) -> Option<Vec<u8>> {
        let bytes = std::fs::File::open(&self.real).ok()
            .and_then(|f| zip::ZipArchive::new(std::io::BufReader::new(f)).ok())
            .and_then(|mut z| { let mut f = z.by_name(self.name).ok()?; let mut v = Vec::with_capacity(f.size() as usize); f.read_to_end(&mut v).ok()?; Some(v) })?;
        mark_fallback_used();
        if let (Some(o), Some(names)) = (recording_overlay(), self.names.as_ref()) {
            let entry = content_entry(Path::new(self.name), &bytes);
            o.index.lock().unwrap().zips.entry(self.key.to_owned()).or_insert_with(|| ZipIndex { names: names.to_vec(), entries: BTreeMap::new() }).entries.insert(self.index, entry);
        }
        Some(bytes)
    }
}

impl Zip {
    pub fn len(&self) -> usize {
        match self { Zip::Real(z, _) => z.len(), Zip::Library(_, _, i, _) => i.names.len() }
    }
    pub fn is_empty(&self) -> bool { self.len() == 0 }
    pub fn file_names(&self) -> Vec<String> {
        match self {
            Zip::Real(z, _) => z.file_names().map(str::to_owned).collect(),
            Zip::Library(_, _, i, _) => i.names.iter().map(|n| n.0.clone()).collect(),
        }
    }
    pub fn index_for_name(&self, name: &str) -> Option<usize> {
        match self {
            Zip::Real(z, _) => z.index_for_name(name),
            Zip::Library(_, _, i, _) => i.names.iter().position(|n| n.0 == name),
        }
    }
    /// (offset, length) of entry `i`'s bytes in the archive file when the entry is stored uncompressed in a real
    /// archive read without recording: such an entry can be mapped instead of read.
    pub fn stored_range(&mut self, i: usize) -> Option<(u64, u64)> {
        let Zip::Real(z, None) = self else { return None };
        let f = z.by_index_raw(i).ok()?;
        (f.compression() == zip::CompressionMethod::Stored && f.compressed_size() == f.size()).then(|| (f.data_start(), f.size()))
    }

    /// Entry `i` was read whole in parts (`Read::read`, which logs nothing): a recording logs it as `read_to_end` does.
    pub fn note_read(&self, i: usize, bytes: &[u8]) {
        if let Zip::Real(_, Some((sink, key))) = self {
            let entry = if crate::oklib::is_unity_bundle(bytes) { Entry::Model(crate::oklib::content_id(bytes), bytes.len() as u64) } else { Entry::Data(bytes.to_vec()) };
            sink.with(|index| if let Some(z) = index.zips.get_mut(key) { z.entries.insert(i, entry); });
        }
    }

    pub fn by_index(&mut self, i: usize) -> zip::result::ZipResult<ZipEntry<'_>> {
        match self {
            Zip::Real(z, rec) => {
                let rec = rec.as_ref().map(|(sink, key)| (sink.clone(), key.clone(), i));
                let f = z.by_index(i)?;
                Ok(ZipEntry { name: f.name().to_owned(), size: f.size(), crc: f.crc32(), dir: f.is_dir(), source: ZipSource::Real(f, rec) })
            }
            Zip::Library(st, key, index, real) => {
                let (key, index, real): (&String, &ZipIndex, &Option<(PathBuf, bool)>) = (key, index, real);
                let (entry_name, size, crc, dir) = index.names.get(i).map(|n| (n.0.as_str(), n.1, n.2, n.3)).ok_or(zip::result::ZipError::FileNotFound)?;
                let name = entry_name.to_owned();
                // A zipmod of the card itself is read from the real archive even when the library knows the entry's model:
                // the library may hold only some queries of a model it never converted, and the card's pack records the rest.
                let recorded = index.entries.get(&i);
                // A card being sealed (okstrict.rs) takes the third-party entries it reads into its pack.
                if let Some(e) = recorded.filter(|_| is_mods_key(key) && crate::okstrict::active().is_some() && !crate::okstrict::library_entry(key, &name)) {
                    let own = replay_overlay().is_some_and(|o| o.index.lock().unwrap().zips.get(key.as_str()).is_some_and(|z| z.entries.contains_key(&i)));
                    if !own {
                        crate::okstrict::complete(|a| { a.index.zips.entry(key.clone()).or_insert_with(|| ZipIndex { names: index.names.clone(), entries: BTreeMap::new() }).entries.insert(i, portable(e)); });
                    }
                }
                // The installation's archive only fills in: a model this checkout has (the base library's shared
                // resources, a mounted pack) answers first, a model that is not here comes from the real entry.
                let use_real = match real {
                    Some((_, true)) => !recorded.is_some_and(|e| e.is_data() || matches!(e, Entry::Model(id, _) if crate::oklib::has_model(id))),
                    Some((_, false)) => !recorded.is_some_and(Entry::is_data),
                    None => false,
                };
                // A recorded model that is not in this checkout (a third-party zipmod's) with no real archive still reads
                // as its token: the item catalog scan walks every zipmod's bundles, and only a query a session really
                // makes on that model is a gap (it names the model, which `zips_with_model` turns into the zipmod).
                let data = if use_real { None } else { recorded.and_then(entry_bytes).map(io::Cursor::new) };
                let label = Label { key, name: entry_name };
                if data.is_none() && !dir {
                    // Not converted (character-specific): the real archive's entry, read when the entry is read.
                    if let Some((path, _)) = real.as_ref().filter(|_| use_real) {
                        let pending = PendingEntry { real: path, name: entry_name, key, index: i,
                            names: recording_overlay().map(|_| index.names.as_slice()) };
                        return Ok(ZipEntry { name, size, crc, dir, source: ZipSource::Pending(pending, st.clone(), label) });
                    }
                }
                Ok(ZipEntry { name, size, crc, dir, source: ZipSource::Library(data, st.clone(), label) })
            }
        }
    }
    pub fn by_name(&mut self, name: &str) -> zip::result::ZipResult<ZipEntry<'_>> {
        let i = self.index_for_name(name).ok_or(zip::result::ZipError::FileNotFound)?;
        self.by_index(i)
    }
}

impl ZipEntry<'_> {
    pub fn name(&self) -> &str { &self.name }
    pub fn size(&self) -> u64 { self.size }
    pub fn crc32(&self) -> u32 { self.crc }
    pub fn is_dir(&self) -> bool { self.dir }
    pub fn is_file(&self) -> bool { !self.dir }
}

impl ZipEntry<'_> {
    /// Turns a pending real-archive entry into its bytes (or a missing entry).
    fn resolve_pending(&mut self) {
        if let ZipSource::Pending(p, st, label) = &self.source {
            let data = p.load().map(io::Cursor::new);
            self.source = ZipSource::Library(data, st.clone(), *label);
        }
    }
}

impl Read for ZipEntry<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match &mut self.source {
            // Partial reads (header probes) are not logged; replay of a
            // partial read without recorded content is a reported gap.
            ZipSource::Real(f, _) => f.read(buf),
            ZipSource::Library(Some(c), _, _) => c.read(buf),
            ZipSource::Library(None, _, label) => { missing("zip-entry", &label.text()); Err(not_found()) }
            ZipSource::Pending(..) => { self.resolve_pending(); self.read(buf) }
        }
    }
    fn read_to_end(&mut self, out: &mut Vec<u8>) -> io::Result<usize> {
        match &mut self.source {
            ZipSource::Real(f, rec) => {
                let start = out.len();
                let n = f.read_to_end(out)?;
                if let Some((sink, key, i)) = rec.take() {
                    let bytes = &out[start..];
                    let entry = if crate::oklib::is_unity_bundle(bytes) { Entry::Model(crate::oklib::content_id(bytes), bytes.len() as u64) } else { Entry::Data(bytes.to_vec()) };
                    sink.with(|index| if let Some(z) = index.zips.get_mut(&key) { z.entries.insert(i, entry); });
                }
                Ok(n)
            }
            ZipSource::Library(Some(c), _, _) => c.read_to_end(out),
            ZipSource::Library(None, _, label) => { missing("zip-entry", &label.text()); Err(not_found()) }
            ZipSource::Pending(..) => {
                self.resolve_pending();
                // The loaded entry itself when nothing was read yet (not a second copy of it).
                if let ZipSource::Library(Some(c), _, _) = &mut self.source {
                    if out.is_empty() && c.position() == 0 { *out = std::mem::take(c.get_mut()); return Ok(out.len()); }
                }
                self.read_to_end(out)
            }
        }
    }    fn read_to_string(&mut self, out: &mut String) -> io::Result<usize> {
        let mut bytes = Vec::new();
        let n = self.read_to_end(&mut bytes)?;
        out.push_str(std::str::from_utf8(&bytes).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?);
        Ok(n)
    }
}

/// Value derived from the game tree under `dir`, built once per library activation and fallback set. Library sessions
/// read a recorded tree that cannot change while it is active, and the derived caches of a real installation (SQL,
/// `.ok_*` files) do not apply to it; without this every card rebuilt mod catalogs and override indexes from all
/// recorded zips. Outside a library session `build` runs as before.
pub fn library_memo<T: Clone + Send + Sync + 'static>(what: &str, dir: &Path, build: impl FnOnce() -> T) -> T {
    type Slot = (String, Arc<dyn std::any::Any + Send + Sync>);
    static MEMO: Mutex<Vec<Slot>> = Mutex::new(Vec::new());
    let Some(st) = state().filter(|st| st.mode == Mode::Replay) else { return build() };
    let n = normalize(dir);
    if n != st.root && !n.starts_with(&format!("{}/", st.root)) { return build(); }
    // An OK card's strict open derives nothing from the library's mods tree (okstrict.rs): an earlier open's value
    // must not stand in for the gap.
    if crate::okstrict::active().is_some() && logical(&st, dir).is_none_or(|k| is_mods_key(&k)) { return build(); }
    let key = format!("{what}\n{n}\n{}", fallback_identity());
    if let Some(hit) = MEMO.lock().unwrap().iter().find(|(k, _)| *k == key).and_then(|(_, v)| v.downcast_ref::<T>().cloned()) { return hit; }
    let value = build();
    let mut memo = MEMO.lock().unwrap();
    if memo.len() >= 32 { memo.remove(0); }
    memo.push((key, Arc::new(value.clone())));
    value
}

/// Value derived from one archive the active library recorded (its content cannot change while the library is
/// active), built once. Archives the library does not record — a card's own zipmods served through the fallback —
/// and every archive outside a library session are built each time.
/// Bumped whenever the fallback or the overlay changes (see `library_zip_memo`).
static FALLBACK_GENERATION: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

/// Whether a recorded archive has a bundle whose model is not in this checkout's library (remembered per archive).
fn zip_lacks_models(st: &State, key: &str) -> bool {
    static LACKS: std::sync::OnceLock<Mutex<std::collections::HashMap<String, bool>>> = std::sync::OnceLock::new();
    let memo = LACKS.get_or_init(Default::default);
    let memo_key = format!("{}\n{key}", st.root);
    if let Some(v) = memo.lock().unwrap().get(&memo_key) { return *v; }
    let dir = PathBuf::from(&st.root);
    let lacks = st.index.lock().unwrap().zips.get(key).is_some_and(|z| z.entries.values()
        .any(|e| matches!(e, Entry::Model(id, _) if !crate::oklib::model_file(&dir, id).is_file())));
    memo.lock().unwrap().insert(memo_key, lacks);
    lacks
}

pub fn library_zip_memo<T: Clone + Send + Sync + 'static>(what: &str, zip: &Path, build: impl FnOnce() -> T) -> T {
    type Slot = Arc<dyn std::any::Any + Send + Sync>;
    static MEMO: std::sync::OnceLock<Mutex<std::collections::HashMap<String, Slot>>> = std::sync::OnceLock::new();
    let Some(st) = state().filter(|st| st.mode == Mode::Replay) else { return build() };
    let Some(key) = logical(&st, zip) else { return build() };
    if !st.index.lock().unwrap().zips.contains_key(&key) { return build(); }
    // A recorded archive whose models this checkout lacks is served by whatever the fallback holds (the real
    // zipmod, or nothing), and a card being recorded must read it again: its values last one fallback generation.
    // (and, cards opening at once each with their own pack, on the pack serving it)
    // An OK card's strict open reads a third-party archive from its own pack (okstrict.rs): values last that pack; a
    // card being sealed reads it again, so what it reads is copied into the card.
    let own = crate::okstrict::active().is_some() && !crate::okstrict::library_zip(&key);
    if own && crate::okstrict::complete(|_| {}) { return build(); }
    let generation = if own || zip_lacks_models(&st, &key) { format!("{}|{}", FALLBACK_GENERATION.load(std::sync::atomic::Ordering::Relaxed), crate::oklib::mounted_pack_identity()) } else { String::new() };
    let key = format!("{what}\n{}\n{key}\n{generation}", st.root);
    let memo = MEMO.get_or_init(Default::default);
    if let Some(hit) = memo.lock().unwrap().get(&key).and_then(|v| v.downcast_ref::<T>().cloned()) { return hit; }
    let value = build();
    memo.lock().unwrap().insert(key, Arc::new(value.clone()));
    value
}

/// For each path, whether the active (replaying) library records that archive. A recorded archive's content cannot
/// change while the library is active; the others (a mounted card pack's own zipmods) are what a view adds.
pub fn library_records_zips(paths: &[PathBuf]) -> Vec<bool> {
    let Some(st) = state().filter(|st| st.mode == Mode::Replay) else { return vec![false; paths.len()] };
    let index = st.index.lock().unwrap();
    paths.iter().map(|p| logical(&st, p).is_some_and(|k| index.zips.contains_key(&k))).collect()
}

/// Value derived from one zipmod, built once per archive generation: an archive the active library records by its
/// logical key (`library_zip_memo`), any other archive (a game folder's mods, a card's own zipmods) by its path,
/// size and modification time. Mods-tree scans (manifests, GUIDs, overrides) then read only the archives that are
/// new or changed instead of all ~7000 on every card whose zip list differs.
pub fn zip_memo<T: Clone + Send + Sync + 'static>(what: &str, zip: &Path, build: impl FnOnce() -> T) -> T {
    if let Some(st) = state().filter(|st| st.mode == Mode::Replay) {
        if let Some(key) = logical(&st, zip) {
            if st.index.lock().unwrap().zips.contains_key(&key) { return library_zip_memo(what, zip, build); }
        }
    }
    type Slot = Arc<dyn std::any::Any + Send + Sync>;
    static MEMO: std::sync::OnceLock<Mutex<std::collections::HashMap<String, Slot>>> = std::sync::OnceLock::new();
    let Ok(meta) = metadata(zip) else { return build() };
    let Some(time) = meta.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()) else { return build() };
    let key = format!("{what}\n{}\n{}\n{}", normalize(zip), meta.len(), time.as_nanos());
    let memo = MEMO.get_or_init(Default::default);
    if let Some(hit) = memo.lock().unwrap().get(&key).and_then(|v| v.downcast_ref::<T>().cloned()) { return hit; }
    let value = build();
    let mut memo = memo.lock().unwrap();
    // Bounded: a few derived values per installed zipmod; a much larger map means many trees or generations.
    if memo.len() >= 200_000 { memo.clear(); }
    memo.insert(key, Arc::new(value.clone()));
    value
}

/// Logical keys of the recorded zips that hold an entry with this model.
pub fn zips_with_model(id: &crate::oklib::Id) -> Vec<String> {
    let Some(st) = state() else { return Vec::new() };
    let index = st.index.lock().unwrap();
    index.zips.iter().filter(|(_, z)| z.entries.values().any(|e| matches!(e, Entry::Model(m, _) if m == id))).map(|(k, _)| k.clone()).collect()
}

/// Lower-case file names of the zipmods the library records.
/// File names of the zipmods the library holds: recorded, and every bundle entry's model in this checkout
/// (a recorded third-party zipmod whose models stay out of version control is not held).
pub fn recorded_zip_names() -> std::collections::HashSet<String> {
    state().map(|st| st.index.lock().unwrap().zips.iter()
        .filter(|(_, z)| z.entries.values().all(|e| !matches!(e, Entry::Model(id, _) if !crate::oklib::has_model(id))))
        .filter_map(|(k, _)| k.rsplit('/').next().map(str::to_owned)).collect()).unwrap_or_default()
}

/// Converter labels (`path` or `zip|entry`) of every observed source with this model id.
pub fn labels_for_model(id: &crate::oklib::Id) -> Vec<String> {
    let Some(st) = state() else { return Vec::new() };
    let index = st.index.lock().unwrap();
    let mut out = Vec::new();
    for (k, e) in &index.files { if matches!(e, Entry::Model(m, _) if m == id) { out.push(k.clone()); } }
    for (zip, z) in &index.zips {
        for (i, e) in &z.entries {
            if matches!(e, Entry::Model(m, _) if m == id) { if let Some(n) = z.names.get(*i) { out.push(format!("{zip}|{}", n.0.replace('\\', "/").to_ascii_lowercase())); } }
        }
    }
    out
}

/// `labels_for_model` for every model at once (one pass over the index).
pub fn model_labels() -> std::collections::HashMap<crate::oklib::Id, Vec<String>> {
    let mut out: std::collections::HashMap<crate::oklib::Id, Vec<String>> = Default::default();
    let Some(st) = state() else { return out };
    let index = st.index.lock().unwrap();
    for (k, e) in &index.files { if let Entry::Model(m, _) = e { out.entry(*m).or_default().push(k.clone()); } }
    for (zip, z) in &index.zips {
        for (i, e) in &z.entries {
            if let (Entry::Model(m, _), Some(n)) = (e, z.names.get(*i)) {
                out.entry(*m).or_default().push(format!("{zip}|{}", n.0.replace('\\', "/").to_ascii_lowercase()));
            }
        }
    }
    out
}

/// (kind, extension, count, bytes) over files and zip entries.
pub fn summary() -> Vec<(String, String, usize, usize)> {
    let Some(st) = state() else { return Vec::new(); };
    let index = st.index.lock().unwrap();
    let mut out: BTreeMap<(String, String), (usize, usize)> = BTreeMap::new();
    let mut add = |name: &str, entry: &Entry| {
        let ext = name.rsplit('/').next().and_then(|n| n.rsplit_once('.')).map(|(_, e)| e.to_owned()).unwrap_or_default();
        let (kind, size) = match entry {
            Entry::Missing => ("missing", 0), Entry::Dir => ("dir", 0), Entry::Present(_) => ("present", 0),
            Entry::Data(_) | Entry::Mapped(..) => ("data", entry.data_len().unwrap_or(0)), Entry::Model(..) => ("model", 0),
        };
        let slot = out.entry((kind.to_owned(), ext)).or_default();
        slot.0 += 1; slot.1 += size;
    };
    for (k, e) in &index.files { add(k, e); }
    for z in index.zips.values() { for (i, e) in &z.entries { add(&z.names[*i].0.to_ascii_lowercase(), e); } }
    out.into_iter().map(|((k, e), (n, b))| (k, e, n, b)).collect()
}

/// Fold another library's observations (a converter shard) into the active index.
pub fn merge_index(other: &Path) -> Result<(), String> {
    let Some(st) = state() else { return Err("no active library".into()); };
    let Ok(bytes) = std::fs::read(index_path(other)) else { return Ok(()); };
    let raw = lz4_flex::block::decompress_size_prepended(&bytes).map_err(|e| e.to_string())?;
    let shard: Index = bincode::deserialize(&raw).map_err(|e| e.to_string())?;
    let mut index = st.index.lock().unwrap();
    for (key, entry) in shard.files {
        let keep = matches!((index.files.get(&key), &entry), (Some(Entry::Data(_) | Entry::Mapped(..) | Entry::Model(..)), Entry::Present(_) | Entry::Missing));
        if !keep { index.files.insert(key, entry); }
    }
    for (key, list) in shard.dirs { index.dirs.insert(key, list); }
    for (key, zip) in shard.zips {
        let slot = index.zips.entry(key).or_insert_with(|| ZipIndex { names: zip.names.clone(), entries: BTreeMap::new() });
        for (i, entry) in zip.entries { slot.entries.insert(i, entry); }
    }
    st.dirty.store(true, std::sync::atomic::Ordering::Relaxed);
    Ok(())
}

#[cfg(test)]
mod mapped_index_tests {
    use super::*;

    /// `normalize` gives what it gave when it replaced `\` in a copy first.
    #[test]
    fn normalize_matches_replacing_form() {
        fn replacing(path: &Path) -> String {
            let s = path.to_string_lossy().replace('\\', "/");
            let s = s.strip_prefix("//?/").unwrap_or(&s).to_owned();
            let mut parts: Vec<&str> = Vec::new();
            for part in s.split('/') {
                match part {
                    "" | "." => { if parts.is_empty() && s.starts_with('/') { parts.push(""); } }
                    ".." => { parts.pop(); }
                    p => parts.push(p),
                }
            }
            parts.join("/").to_ascii_lowercase()
        }
        for p in [r"\\?\D:\KoikatuSunshine\Mods\A.zipmod", "//?/d:/x", r"\/?\x", "/a/../../b//", "/../a", "a/..//b", r"C:\A\.\B\..\C",
            "", "/", "?", "//?", r"\\?\", "abc", "./x/./y/", r"E:\OpenKoikatsu\Content\IndependentResources\base_v1\abdata\chara\mm_base.unity3d", "a\\b/Ü/中"] {
            assert_eq!(normalize(Path::new(p)), replacing(Path::new(p)), "{p}");
        }
    }

    /// A replayed library's index read through its mapped cache holds file contents as `Mapped` offsets; they read
    /// and serialize exactly as the `Data` they came from.
    #[test]
    fn mapped_index_matches_loaded_index() {
        let library = std::env::temp_dir().join(format!("ok-mapped-index-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&library);
        std::fs::create_dir_all(&library).unwrap();
        let mut index = Index::default();
        index.files.insert("abdata/list/a.txt".into(), Entry::Data(b"first".to_vec()));
        index.files.insert("abdata/list/b.txt".into(), Entry::Data((0..=255u8).cycle().take(70_000).collect()));
        index.files.insert("abdata/chara/x.unity3d".into(), Entry::Model([7; 16], 42));
        index.files.insert("abdata/missing".into(), Entry::Missing);
        index.dirs.insert("abdata".into(), vec![("list".into(), true)]);
        let mut zip = ZipIndex { names: vec![("abdata/c.csv".into(), 3, 1, false)], entries: BTreeMap::new() };
        zip.entries.insert(0, Entry::Data(b"csv".to_vec()));
        index.zips.insert("mods/m.zipmod".into(), zip);
        let raw = bincode::serialize(&index).unwrap();
        std::fs::write(index_path(&library), lz4_flex::block::compress_prepend_size(&raw)).unwrap();

        let (mapped, map) = mapped_index(&library).expect("mapped index");
        *INDEX_MAP.write().unwrap() = Some(map);
        assert!(matches!(mapped.files["abdata/list/b.txt"], Entry::Mapped(..)));
        for (key, entry) in &index.files { assert_eq!(entry_bytes(&mapped.files[key]), entry_bytes(entry), "{key}"); }
        assert_eq!(entry_bytes(&mapped.zips["mods/m.zipmod"].entries[&0]).as_deref(), Some(&b"csv"[..]));
        assert_eq!(bincode::serialize(&mapped).unwrap(), raw, "written back as the original Data entries");
        // The cache is reused, and a replaced index file gets a new one.
        assert!(mapped_index(&library).is_some());
        *INDEX_MAP.write().unwrap() = None;
        let _ = std::fs::remove_dir_all(&library);
    }
}
