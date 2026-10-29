//! OK base library: the shared character resources of the game and its mods
//! (bodies, heads, hair, clothes, accessories, textures, shaders), converted
//! to OK models so cards open without the game or any Unity data.
//!
//! Every query that turns Unity data into OK data goes through `memo`, keyed
//! by (source content id, query, arguments). Recording runs the query on the
//! real Unity source and appends the result to that source's OK model;
//! replaying answers it from the model. The extraction pipeline is unchanged:
//! `gamefs` hands it an in-memory model handle (`OKLIB001` + id) where it
//! would have read a Unity bundle.
//!
//! Layout of a library directory:
//!   oklib.json                 marker + version
//!   models/<xx>/<id>.okmodel   one OK model per Unity source (see `Model`)
//!   sources.okidx              game observations: paths -> models, configs, lists (gamefs)
//!   missing.txt                replay queries that were not recorded (coverage gaps)
use std::collections::{BTreeMap, HashMap};
use std::fs::File;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, RwLock};

mod crowd_check;

pub const TOKEN_MAGIC: &[u8; 8] = b"OKLIB001";
pub type Id = [u8; 16];
const MODEL_MAGIC: &[u8; 8] = b"OKMODEL2";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode { Record, Replay }

/// `.okmodel`: `OKMODEL2`, the 16-byte source id, then records appended in
/// conversion order: [u32 record length][u16 query length][query][u32 args
/// length][args (bincode)][u8 kind][payload].
///   kind 0: payload is the LZ4 value (bincode of the query result);
///   kind 1: payload is [u64 offset][u32 length] of an identical kind-0 value;
///   kind 2: payload is [u32 n] then n x [u64 offset][u32 length] of chunks.
/// Large values are split at content-defined boundaries; each distinct chunk
/// is stored once per model as a `#chunk` record (args = chunk hash), so the
/// same texture inside a mesh result and a texture result costs one copy.
#[derive(Default)]
struct Model {
    entries: HashMap<(String, Vec<u8>), Loc>,
    values: HashMap<u64, (u64, u32)>,
    chunks: HashMap<u64, (u64, u32)>,
    len: u64,
}
#[derive(Clone)]
enum Loc { Packed(u64, u32), Chunks(Vec<(u64, u32)>) }
const CHUNK_QUERY: &str = "#chunk";
const CHUNK_MIN: usize = 4 * 1024;
const CHUNK_MAX: usize = 64 * 1024;
const CHUNK_MASK: u64 = (1 << 14) - 1;

fn gear() -> &'static [u64; 256] {
    static GEAR: std::sync::OnceLock<[u64; 256]> = std::sync::OnceLock::new();
    GEAR.get_or_init(|| {
        use sha2::Digest;
        std::array::from_fn(|i| u64::from_le_bytes(sha2::Sha256::digest([b'O', b'K', i as u8]).as_slice()[..8].try_into().unwrap()))
    })
}
/// Content-defined chunk boundaries (gear rolling hash).
pub(crate) fn chunk_spans(data: &[u8]) -> Vec<std::ops::Range<usize>> {
    let gear = gear();
    let mut spans = Vec::new();
    let mut start = 0;
    while start < data.len() {
        let end = (start + CHUNK_MAX).min(data.len());
        let mut cut = end;
        let mut h = 0u64;
        let mut i = start + CHUNK_MIN.min(end - start);
        while i < end {
            h = (h << 1).wrapping_add(gear[data[i] as usize]);
            if h & CHUNK_MASK == 0 { cut = i + 1; break; }
            i += 1;
        }
        spans.push(start..cut);
        start = cut;
    }
    spans
}

pub struct Store {
    pub dir: PathBuf,
    pub mode: Mode,
    models: Mutex<HashMap<Id, Arc<Mutex<Model>>>>,
    missing: Mutex<HashMap<String, u32>>,
    pub hits: AtomicU64,
    pub misses: AtomicU64,
    pub stored: AtomicU64,
    pub conflicts: AtomicU64,
    conflict_log: Mutex<HashMap<String, u32>>,
    /// Card-specific models next to the library: recorded while an original card becomes an OK card,
    /// replayed from the pack an OK card carries.
    pack: RwLock<Option<Arc<Store>>>,
    /// An OK card's pack held in memory for its open (`Store::in_memory`): (model, query, args) -> stored value.
    /// Nothing is written to disk; the table is freed with the open.
    /// Also a card pack being recorded (`Store::recording_in_memory`): nothing goes to disk.
    records: Option<RwLock<HashMap<(Id, String, Vec<u8>), Vec<u8>>>>,
    /// The models `records` hold.
    record_models: RwLock<std::collections::HashSet<Id>>,
}

static ACTIVE: AtomicBool = AtomicBool::new(false);
/// The converter only needs ids/skeletons from stored results: packed values
/// are returned without restoring their texture references.
pub static CONVERTING: AtomicBool = AtomicBool::new(false);
static STORE: RwLock<Option<Arc<Store>>> = RwLock::new(None);

/// Whether `memo`/`memo_bytes` answer from (and record into) the library instead of running their computation.
pub(crate) fn memo_active() -> bool { ACTIVE.load(Ordering::Relaxed) }

pub fn active() -> Option<Arc<Store>> {
    if !ACTIVE.load(Ordering::Relaxed) { return None; }
    STORE.read().unwrap().clone()
}
pub fn mode() -> Option<Mode> { active().map(|s| s.mode) }
pub fn replaying() -> bool { mode() == Some(Mode::Replay) }

pub(crate) fn hex(id: &[u8]) -> String { id.iter().map(|b| format!("{b:02x}")).collect() }
pub fn id_hex(id: &Id) -> String { hex(id) }
pub fn id_from_hex_pub(s: &str) -> Option<Id> { id_from_hex(s) }
/// (query, args) of every record of a model.
pub fn model_records(dir: &Path, id: &Id) -> Result<Vec<(String, Vec<u8>)>, String> {
    let p = model_path(dir, id);
    if !p.is_file() { return Err(format!("{} not found", p.display())); }
    let mut v: Vec<_> = load_model(&p, false).entries.into_keys().collect();
    v.sort();
    Ok(v)
}
fn id_from_hex(s: &str) -> Option<Id> {
    if s.len() != 32 { return None; }
    let mut id = [0u8; 16];
    for (i, b) in id.iter_mut().enumerate() { *b = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).ok()?; }
    Some(id)
}

pub fn token_id(bytes: &[u8]) -> Option<Id> {
    if bytes.len() < 24 || &bytes[..8] != TOKEN_MAGIC { return None; }
    let mut id = [0u8; 16];
    id.copy_from_slice(&bytes[8..24]);
    Some(id)
}
/// In-memory model handle handed to the pipeline in place of Unity bytes.
pub fn token(id: &Id) -> Vec<u8> {
    let mut v = TOKEN_MAGIC.to_vec();
    v.extend_from_slice(id);
    v
}
/// Content id of Unity source bytes: length + CRC-32.
pub fn content_id(bytes: &[u8]) -> Id {
    if let Some(id) = token_id(bytes) { return id; }
    id_of(bytes.len() as u64, crc32fast::hash(bytes))
}
pub fn id_of(len: u64, crc: u32) -> Id {
    let mut id = [0u8; 16];
    id[..8].copy_from_slice(&len.to_le_bytes());
    id[8..12].copy_from_slice(&crc.to_le_bytes());
    id[12..].copy_from_slice(b"OKC1");
    id
}
pub fn is_unity_bundle(head: &[u8]) -> bool {
    [&b"UnityFS"[..], b"UnityWeb", b"UnityRaw", b"UnityArchive"].iter().any(|m| head.starts_with(m))
}
/// Identity of bundle bytes while a library is active (no hashing otherwise).
pub fn bundle_id(bytes: &[u8]) -> Option<Id> {
    if !ACTIVE.load(Ordering::Relaxed) { return token_id(bytes); }
    Some(content_id(bytes))
}

fn value_hash(bytes: &[u8]) -> u64 {
    use sha2::Digest;
    let digest = sha2::Sha256::digest(bytes);
    u64::from_le_bytes(digest[..8].try_into().unwrap())
}

#[cfg(windows)]
fn read_at(file: &File, buf: &mut [u8], offset: u64) -> std::io::Result<()> {
    use std::os::windows::fs::FileExt;
    let mut done = 0;
    while done < buf.len() {
        let n = file.seek_read(&mut buf[done..], offset + done as u64)?;
        if n == 0 { return Err(std::io::ErrorKind::UnexpectedEof.into()); }
        done += n;
    }
    Ok(())
}
#[cfg(not(windows))]
fn read_at(file: &File, buf: &mut [u8], offset: u64) -> std::io::Result<()> {
    use std::os::unix::fs::FileExt;
    file.read_exact_at(buf, offset)
}

pub fn model_path(dir: &Path, id: &Id) -> PathBuf {
    let h = hex(id);
    dir.join("models").join(&h[..2]).join(format!("{h}.okmodel"))
}

/// Whether the active library (or the pack mounted over it) has model `id` in this checkout. A library records
/// sources whose models stay out of version control (third-party mods); those come from the real zipmods.
pub fn has_model(id: &Id) -> bool {
    let Some(store) = active() else { return false };
    model_path(&store.dir, id).is_file() || store.pack().is_some_and(|p| p.has_own_model(id))
}
/// Directory of the active library.
pub fn active_dir() -> Option<PathBuf> { active().map(|s| s.dir.clone()) }

/// Scan a model's record headers (values are not read).
fn load_model(path: &Path, hashes: bool) -> Model {
    let mut model = Model::default();
    // Mapped: the record index is read in place (a model file can be hundreds of MB).
    let Ok(file) = File::open(path) else { return model; };
    let Ok(bytes) = (unsafe { memmap2::Mmap::map(&file) }) else { return model; };
    if bytes.len() < 24 || &bytes[..8] != MODEL_MAGIC { return model; }
    let mut at = 24usize;
    let u32_at = |b: &[u8], at: usize| b.get(at..at + 4).map(|s| u32::from_le_bytes(s.try_into().unwrap()));
    let u64_at = |b: &[u8], at: usize| b.get(at..at + 8).map(|s| u64::from_le_bytes(s.try_into().unwrap()));
    // A torn final record (interrupted conversion) ends the scan.
    while let Some(total) = u32_at(&bytes, at).map(|n| n as usize) {
        let start = at + 4;
        let Some(record) = bytes.get(start..start + total) else { break; };
        let parse = || -> Option<(String, Vec<u8>, Loc)> {
            let ql = u16::from_le_bytes(record.get(0..2)?.try_into().ok()?) as usize;
            let query = std::str::from_utf8(record.get(2..2 + ql)?).ok()?.to_owned();
            let al = u32_at(record, 2 + ql)? as usize;
            let args = record.get(6 + ql..6 + ql + al)?.to_vec();
            let k = 6 + ql + al;
            let loc = match *record.get(k)? {
                0 => Loc::Packed((start + k + 1) as u64, (total - k - 1) as u32),
                1 => Loc::Packed(u64_at(record, k + 1)?, u32_at(record, k + 9)?),
                2 => {
                    let n = u32_at(record, k + 1)? as usize;
                    Loc::Chunks((0..n).map(|i| Some((u64_at(record, k + 5 + i * 12)?, u32_at(record, k + 13 + i * 12)?))).collect::<Option<Vec<_>>>()?)
                }
                _ => return None,
            };
            Some((query, args, loc))
        };
        let Some((query, args, loc)) = parse() else { break; };
        if query == CHUNK_QUERY {
            if let (Ok(hash), Loc::Packed(o, l)) = (<[u8; 8]>::try_from(args.as_slice()), &loc) { model.chunks.insert(u64::from_le_bytes(hash), (*o, *l)); }
        } else {
            if let (true, Loc::Packed(o, l)) = (hashes, &loc) { model.values.entry(value_hash_at(&bytes, *o, *l)).or_insert((*o, *l)); }
            model.entries.insert((query, args), loc);
        }
        at = start + total;
    }
    model.len = at as u64;
    model
}
/// Hash of a stored (compressed) value, for whole-value deduplication.
fn value_hash_at(bytes: &[u8], offset: u64, len: u32) -> u64 {
    bytes.get(offset as usize..(offset + len as u64) as usize)
        .and_then(|b| lz4_flex::block::decompress_size_prepended(b).ok()).map(|v| value_hash(&v)).unwrap_or(0)
}

fn read_loc(file: &File, loc: &Loc) -> Option<Vec<u8>> {
    let read = |offset: u64, len: u32| -> Option<Vec<u8>> {
        let mut buf = vec![0u8; len as usize];
        read_at(file, &mut buf, offset).ok()?;
        lz4_flex::block::decompress_size_prepended(&buf).ok()
    };
    match loc {
        Loc::Packed(offset, len) => read(*offset, *len),
        Loc::Chunks(parts) => {
            let mut out = Vec::new();
            for (offset, len) in parts { out.extend(read(*offset, *len)?); }
            Some(out)
        }
    }
}

/// `read_loc` for a result that may be large: its compressed parts are read through a map of the model file and
/// decompressed straight into a mapped temporary file.
fn read_loc_source(file: &File, loc: &Loc) -> Option<crate::unity::SourceBytes> {
    let map = unsafe { memmap2::Mmap::map(file) }.ok()?;
    let parts: Vec<(u64, u32)> = match loc { Loc::Packed(offset, len) => vec![(*offset, *len)], Loc::Chunks(parts) => parts.clone() };
    let blocks: Vec<&[u8]> = parts.iter().map(|(offset, len)| map.get(*offset as usize..*offset as usize + *len as usize)).collect::<Option<_>>()?;
    let sizes: Vec<usize> = blocks.iter().map(|b| b.get(..4).map(|s| u32::from_le_bytes(s.try_into().unwrap()) as usize)).collect::<Option<_>>()?;
    let total: usize = sizes.iter().sum();
    if (total as u64) < crate::unity::MAP_AT_LEAST { return read_loc(file, loc).map(crate::unity::SourceBytes::from); }
    crate::unity::SourceBytes::spill_with(total as u64, |out| {
        let mut at = 0;
        for (block, size) in blocks.iter().zip(&sizes) {
            if lz4_flex::block::decompress_into(&block[4..], &mut out[at..at + size]).ok()? != *size { return None; }
            at += size;
        }
        Some(())
    })
}

fn record_bytes(query: &str, args: &[u8], kind: u8, payload: &[u8]) -> Vec<u8> {
    let mut record = Vec::with_capacity(16 + query.len() + args.len() + payload.len());
    record.extend_from_slice(&(query.len() as u16).to_le_bytes());
    record.extend_from_slice(query.as_bytes());
    record.extend_from_slice(&(args.len() as u32).to_le_bytes());
    record.extend_from_slice(args);
    record.push(kind);
    record.extend_from_slice(payload);
    let mut out = (record.len() as u32).to_le_bytes().to_vec();
    out.extend_from_slice(&record);
    out
}

impl Store {
    pub fn open(dir: &Path, mode: Mode) -> Result<Store, String> {
        if mode == Mode::Record { std::fs::create_dir_all(dir).map_err(|e| e.to_string())?; }
        let marker = dir.join("oklib.json");
        if !marker.is_file() {
            if mode == Mode::Replay { return Err(format!("{} is not an OK base library", dir.display())); }
            std::fs::write(&marker, serde_json::to_vec_pretty(&serde_json::json!({"format":"oklib","version":2})).unwrap())
                .map_err(|e| e.to_string())?;
        }
        Ok(Store {
            dir: dir.to_owned(), mode, models: Mutex::new(HashMap::new()), missing: Mutex::new(HashMap::new()),
            hits: AtomicU64::new(0), misses: AtomicU64::new(0), stored: AtomicU64::new(0), conflicts: AtomicU64::new(0),
            conflict_log: Mutex::new(HashMap::new()), pack: RwLock::new(None), records: None, record_models: Default::default(),
        })
    }

    /// A replayed card pack in memory: its records answer queries directly (no model files).
    fn in_memory(records: Vec<PackRecord>) -> Store {
        Store {
            dir: PathBuf::new(), mode: Mode::Replay, models: Mutex::new(HashMap::new()), missing: Mutex::new(HashMap::new()),
            hits: AtomicU64::new(0), misses: AtomicU64::new(0), stored: AtomicU64::new(0), conflicts: AtomicU64::new(0),
            conflict_log: Mutex::new(HashMap::new()), pack: RwLock::new(None),
            record_models: RwLock::new(records.iter().map(|r| r.0).collect()),
            records: Some(RwLock::new(records.into_iter().map(|PackRecord(id, query, args, value)| ((id, query, args), value)).collect())),
        }
    }

    /// A card pack being recorded in memory (begin_card_pack): the first value of a query is kept.
    fn recording_in_memory() -> Store {
        Store { mode: Mode::Record, ..Store::in_memory(Vec::new()) }
    }

    /// The records of an in-memory store, sorted.
    fn take_records(&self) -> Vec<PackRecord> {
        let mut out: Vec<PackRecord> = self.records.as_ref().map(|r| std::mem::take(&mut *r.write().unwrap()))
            .unwrap_or_default().into_iter().map(|((id, query, args), value)| PackRecord(id, query, args, value)).collect();
        out.sort_by(|a, b| (a.0, &a.1, &a.2).cmp(&(b.0, &b.1, &b.2)));
        out
    }

    /// Whether this store holds model `id` (a model file, or records of it in memory).
    fn has_own_model(&self, id: &Id) -> bool {
        if self.records.is_some() { self.record_models.read().unwrap().contains(id) } else { model_path(&self.dir, id).is_file() }
    }

    fn model(&self, id: &Id) -> Arc<Mutex<Model>> {
        let mut models = self.models.lock().unwrap();
        models.entry(*id).or_insert_with(|| Arc::new(Mutex::new(load_model(&model_path(&self.dir, id), self.mode == Mode::Record)))).clone()
    }

    /// Records held by the models loaded in this session.
    pub fn len(&self) -> usize { self.models.lock().unwrap().values().map(|m| m.lock().unwrap().entries.len()).sum() }

    pub fn get_raw(&self, id: &Id, query: &str, args: &[u8]) -> Option<Vec<u8>> {
        self.get_raw_src(id, query, args).map(|(raw, _)| raw)
    }

    /// The stored result and whether it came from the card pack (which holds complete values: its
    /// sources are not library models, so packed texture references could not be restored).
    pub fn get_raw_src(&self, id: &Id, query: &str, args: &[u8]) -> Option<(Vec<u8>, bool)> {
        // An OK card's open reads a model only third-party mods use from its own pack (okstrict.rs): the library's
        // copy is a gap, copied into the card while it is sealed.
        if self.mode == Mode::Replay && crate::okstrict::active().is_some() && !crate::okstrict::library_model(id) {
            if let Some(raw) = self.pack().and_then(|p| p.get_own(id, query, args)) { return Some((raw, true)); }
            let raw = self.get_own(id, query, args)?;
            if crate::okstrict::complete(|a| { a.records.insert((*id, query.to_owned(), args.to_vec()), raw.clone()); }) { return Some((raw, true)); }
            crate::okstrict::gap(|| format!("model {} {query} {}", hex(id), describe_args(args)));
            return None;
        }
        if let Some(raw) = self.get_own(id, query, args) { return Some((raw, false)); }
        self.pack().and_then(|p| p.get_own(id, query, args)).map(|raw| (raw, true))
    }

    /// `get_own` without holding a large result in memory (decompressed into a mapped temporary file).
    fn get_own_source(&self, id: &Id, query: &str, args: &[u8]) -> Option<crate::unity::SourceBytes> {
        if let Some(records) = &self.records { return records.read().unwrap().get(&(*id, query.to_owned(), args.to_vec())).cloned().map(crate::unity::SourceBytes::from); }
        let model = self.model(id);
        let loc = model.lock().unwrap().entries.get(&(query.to_owned(), args.to_vec()))?.clone();
        let file = File::open(model_path(&self.dir, id)).ok()?;
        read_loc_source(&file, &loc)
    }

    fn get_own(&self, id: &Id, query: &str, args: &[u8]) -> Option<Vec<u8>> {
        if let Some(records) = &self.records { return records.read().unwrap().get(&(*id, query.to_owned(), args.to_vec())).cloned(); }
        let model = self.model(id);
        let loc = model.lock().unwrap().entries.get(&(query.to_owned(), args.to_vec()))?.clone();
        let file = File::open(model_path(&self.dir, id)).ok()?;
        read_loc(&file, &loc)
    }

    /// The card pack: the one of the open running on this thread (PackScope), else the process-wide one.
    pub fn pack(&self) -> Option<Arc<Store>> {
        if self.mode == Mode::Replay { if let Some(scope) = pack_scope() { return scope.store.clone(); } }
        self.pack.read().unwrap().clone()
    }
    /// The pack being recorded (an original card becoming an OK card).
    // (recording is process-wide: an open's replayed pack is never one)
    fn recording_pack(&self) -> Option<Arc<Store>> { self.pack.read().unwrap().clone().filter(|p| p.mode == Mode::Record) }

    pub fn put_raw(&self, id: &Id, query: &str, args: &[u8], raw: &[u8]) {
        if self.mode != Mode::Record { return; }
        if let Some(records) = &self.records {
            records.write().unwrap().entry((*id, query.to_owned(), args.to_vec())).or_insert_with(|| raw.to_vec());
            self.record_models.write().unwrap().insert(*id);
            return;
        }
        let model = self.model(id);
        let mut m = model.lock().unwrap();
        let key = (query.to_owned(), args.to_vec());
        let path = model_path(&self.dir, id);
        if let Some(loc) = m.entries.get(&key).cloned() {
            let same = || File::open(&path).ok().and_then(|f| read_loc(&f, &loc)).is_some_and(|old| old == raw);
            // A refreshed query (OKLIB_REFRESH) appends the new value; the
            // later record of a key wins when the model is loaded.
            if refreshing(query) {
                if same() { return; }
                eprintln!("ok-extract: OKLIB_REFRESH replaced {query} {}", hex(id));
            } else {
                // Results keyed by skeleton topology must not depend on the card.
                if query.starts_with("skel:") && !same() {
                    self.conflicts.fetch_add(1, Ordering::Relaxed);
                    *self.conflict_log.lock().unwrap().entry(format!("{query} {}", hex(id))).or_default() += 1;
                }
                return;
            }
        }
        if m.len == 0 {
            if let Some(parent) = path.parent() { let _ = std::fs::create_dir_all(parent); }
            let mut header = MODEL_MAGIC.to_vec();
            header.extend_from_slice(id);
            if std::fs::write(&path, &header).is_err() { return; }
            m.len = header.len() as u64;
        }
        let mut out = Vec::new();
        let base = m.len;
        let loc;
        if raw.len() > CHUNK_MAX {
            let mut parts = Vec::new();
            let mut new_chunks = Vec::new();
            for span in chunk_spans(raw) {
                let piece = &raw[span];
                let h = value_hash(piece);
                if let Some(&loc) = m.chunks.get(&h).or_else(|| new_chunks.iter().find(|(k, _)| *k == h).map(|(_, l)| l)) { parts.push(loc); continue; }
                let packed = lz4_flex::block::compress_prepend_size(piece);
                let record = record_bytes(CHUNK_QUERY, &h.to_le_bytes(), 0, &packed);
                let at = (base + out.len() as u64 + record.len() as u64 - packed.len() as u64, packed.len() as u32);
                out.extend_from_slice(&record);
                new_chunks.push((h, at));
                parts.push(at);
            }
            let mut payload = (parts.len() as u32).to_le_bytes().to_vec();
            for (o, l) in &parts { payload.extend_from_slice(&o.to_le_bytes()); payload.extend_from_slice(&l.to_le_bytes()); }
            out.extend_from_slice(&record_bytes(query, args, 2, &payload));
            for (h, at) in new_chunks { m.chunks.insert(h, at); }
            loc = Loc::Chunks(parts);
        } else {
            let vh = value_hash(raw);
            match m.values.get(&vh).copied() {
                Some((o, l)) => {
                    let mut payload = o.to_le_bytes().to_vec();
                    payload.extend_from_slice(&l.to_le_bytes());
                    out.extend_from_slice(&record_bytes(query, args, 1, &payload));
                    loc = Loc::Packed(o, l);
                }
                None => {
                    let packed = lz4_flex::block::compress_prepend_size(raw);
                    let record = record_bytes(query, args, 0, &packed);
                    let at = (base + record.len() as u64 - packed.len() as u64, packed.len() as u32);
                    out.extend_from_slice(&record);
                    m.values.insert(vh, at);
                    loc = Loc::Packed(at.0, at.1);
                }
            }
        }
        let Ok(mut file) = std::fs::OpenOptions::new().append(true).open(&path) else { return; };
        if file.write_all(&out).is_err() { return; }
        m.len += out.len() as u64;
        m.entries.insert(key, loc);
        self.stored.fetch_add(1, Ordering::Relaxed);
    }

    pub fn flush(&self) {
        if self.records.is_some() { return; }
        let missing = self.missing.lock().unwrap();
        if !missing.is_empty() {
            let mut lines: Vec<_> = missing.iter().map(|(k, n)| format!("{n}\t{k}")).collect();
            lines.sort();
            let _ = std::fs::write(self.dir.join("missing.txt"), lines.join("\n") + "\n");
        }
        let conflicts = self.conflict_log.lock().unwrap();
        if !conflicts.is_empty() {
            let mut lines: Vec<_> = conflicts.iter().map(|(k, n)| format!("{n}\t{k}")).collect();
            lines.sort();
            let _ = std::fs::write(self.dir.join("conflicts.txt"), lines.join("\n") + "\n");
        }
    }

    pub fn note_missing(&self, what: String) {
        // (a card being sealed computes a query its own model lacks as every open did, and keeps the value: what that
        // computation could not read is no gap of the card, okstrict.rs)
        if !(what.starts_with("UNMEMOIZED ") && crate::okstrict::completing()) { crate::okstrict::gap(|| what.clone()); }
        self.misses.fetch_add(1, Ordering::Relaxed);
        *self.missing.lock().unwrap().entry(what).or_default() += 1;
    }
    pub fn clear_missing(&self) { self.missing.lock().unwrap().clear(); }
    pub fn missing_list(&self) -> Vec<(String, u32)> {
        let mut v: Vec<_> = self.missing.lock().unwrap().iter().map(|(k, n)| (k.clone(), *n)).collect();
        v.sort();
        v
    }
}

/// Every model of a library: (id, record count, bytes, queries by name).
pub fn model_summary(dir: &Path) -> Vec<(Id, usize, u64, BTreeMap<String, usize>)> {
    let mut out = Vec::new();
    let Ok(shards) = std::fs::read_dir(dir.join("models")) else { return out; };
    for shard in shards.flatten() {
        let Ok(files) = std::fs::read_dir(shard.path()) else { continue; };
        for file in files.flatten() {
            let path = file.path();
            let Some(id) = path.file_stem().and_then(|s| s.to_str()).and_then(id_from_hex) else { continue; };
            let model = load_model(&path, false);
            let mut queries = BTreeMap::new();
            let mut seen = std::collections::HashSet::new();
            let mut entries: Vec<_> = model.entries.iter().collect();
            entries.sort_by(|a, b| a.0.cmp(b.0));
            for ((query, _), loc) in entries {
                *queries.entry(query.clone()).or_default() += 1;
                // Stored bytes, each physical value/chunk counted once.
                let parts = match loc { Loc::Packed(o, l) => vec![(*o, *l)], Loc::Chunks(p) => p.clone() };
                for (o, l) in parts { if seen.insert(o) { *queries.entry(format!("~bytes {query}")).or_default() += l as usize; } }
            }
            *queries.entry("#chunks".to_owned()).or_default() += model.chunks.len();
            out.push((id, model.entries.len(), model.len, queries));
        }
    }
    out
}

/// Append every record of another library's models to `store`'s models.
/// Records already present are kept as they are.
pub fn merge_models(store: &Store, other: &Path) -> Result<usize, String> {
    let mut merged = 0;
    let Ok(shards) = std::fs::read_dir(other.join("models")) else { return Ok(0); };
    for shard in shards.flatten() {
        let Ok(files) = std::fs::read_dir(shard.path()) else { continue; };
        for file in files.flatten() {
            let path = file.path();
            let Some(id) = path.file_stem().and_then(|s| s.to_str()).and_then(id_from_hex) else { continue; };
            let model = load_model(&path, false);
            let handle = File::open(&path).map_err(|e| e.to_string())?;
            for ((query, args), loc) in &model.entries {
                let Some(raw) = read_loc(&handle, loc) else { continue; };
                let before = store.stored.load(Ordering::Relaxed);
                store.put_raw(&id, query, args, &raw);
                merged += (store.stored.load(Ordering::Relaxed) - before) as usize;
            }
        }
    }
    Ok(merged)
}

fn vendor_hook(label: &'static str) { note_unmemoized(label); }

/// Strict decode of every mesh-carrying record with the current types.
pub fn check_models(dir: &Path) -> Result<(usize, usize, Vec<String>), String> {
    let (mut models, mut checked, mut failures) = (0, 0, Vec::new());
    for shard in std::fs::read_dir(dir.join("models")).map_err(|e| e.to_string())?.flatten() {
        for file in std::fs::read_dir(shard.path()).map_err(|e| e.to_string())?.flatten() {
            let path = file.path();
            let model = load_model(&path, false);
            let Ok(handle) = File::open(&path) else { continue };
            models += 1;
            for ((query, _), loc) in &model.entries {
                let Some(raw) = read_loc(&handle, loc) else { failures.push(format!("{} {query} unreadable", path.display())); continue };
                let ok = crate::unity::with_texture_plans(|| match query.as_str() {
                    "meshes_for_maindata" => Some(bincode::deserialize::<(Vec<crate::unity::ExtractedMesh>, Vec<(i64, Vec<String>)>)>(&raw).is_ok()),
                    "decode_all" => Some(bincode::deserialize::<(Vec<crate::unity::ExtractedMesh>, Vec<(String, Vec<u8>)>, Option<crate::pose::Skeleton>)>(&raw).is_ok()),
                    "base_bundle" => Some(bincode::deserialize::<(Vec<crate::unity::ExtractedMesh>, Option<crate::pose::Skeleton>, Vec<(String, Vec<u8>)>)>(&raw).is_ok()),
                    _ => None,
                });
                let Some(ok) = ok else { continue };
                checked += 1;
                if !ok { failures.push(format!("{} {query}", path.file_name().unwrap_or_default().to_string_lossy())); }
            }
        }
    }
    Ok((models, checked, failures))
}

/// Rewrite every model of a library through `fix(query, value)` (format
/// migrations); also drops unreachable bytes. Each model is written to a
/// temporary file first and replaces the original only when complete.
pub fn repack(dir: &Path, threads: usize, fix: impl Fn(&str, &[u8]) -> Option<Result<Vec<u8>, String>> + Sync) -> Result<(usize, usize, Vec<String>), String> {
    use rayon::prelude::*;
    let mut paths = Vec::new();
    for shard in std::fs::read_dir(dir.join("models")).map_err(|e| e.to_string())?.flatten() {
        for file in std::fs::read_dir(shard.path()).map_err(|e| e.to_string())?.flatten() {
            if file.path().extension().is_some_and(|e| e == "okmodel") { paths.push(file.path()); }
        }
    }
    let temp = dir.join("work").join("repack");
    let _ = std::fs::remove_dir_all(&temp);
    let store = Store::open(&temp, Mode::Record)?;
    let failures = Mutex::new(Vec::new());
    let fixed = AtomicU64::new(0);
    let pool = rayon::ThreadPoolBuilder::new().num_threads(threads.max(1)).build().map_err(|e| e.to_string())?;
    pool.install(|| paths.par_iter().for_each(|path| {
        let Some(id) = path.file_stem().and_then(|s| s.to_str()).and_then(id_from_hex) else { return };
        let model = load_model(path, false);
        let Ok(handle) = File::open(path) else { return };
        let mut entries: Vec<_> = model.entries.iter().collect();
        entries.sort_by_key(|(_, loc)| match loc { Loc::Packed(o, _) => *o, Loc::Chunks(p) => p.first().map(|x| x.0).unwrap_or(0) });
        for ((query, args), loc) in entries {
            let Some(raw) = read_loc(&handle, loc) else { failures.lock().unwrap().push(format!("{} {query} unreadable", hex(&id))); continue };
            let raw = match fix(query, &raw) {
                None => raw,
                Some(Ok(v)) => { fixed.fetch_add(1, Ordering::Relaxed); v }
                Some(Err(e)) => { failures.lock().unwrap().push(format!("{} {query} {e}", hex(&id))); continue }
            };
            store.put_raw(&id, query, args, &raw);
        }
        store.forget_model(&id);
        let new = model_path(&temp, &id);
        if new.is_file() { let _ = std::fs::rename(&new, path); }
    }));
    let _ = std::fs::remove_dir_all(&temp);
    Ok((paths.len(), fixed.load(Ordering::Relaxed) as usize, failures.into_inner().unwrap()))
}

/// Conversion profile: inclusive seconds and calls per label (OKLIB_PROFILE=1).
static PROFILE: Mutex<BTreeMap<String, (f64, u64)>> = Mutex::new(BTreeMap::new());
pub fn profiling() -> bool { std::env::var_os("OKLIB_PROFILE").is_some() }
pub fn timed<R>(label: &str, f: impl FnOnce() -> R) -> R {
    if !profiling() { return f(); }
    let t = std::time::Instant::now();
    let r = f();
    let mut p = PROFILE.lock().unwrap();
    let e = p.entry(label.to_owned()).or_insert((0., 0));
    e.0 += t.elapsed().as_secs_f64(); e.1 += 1;
    r
}
pub fn print_profile() {
    let p = PROFILE.lock().unwrap();
    let mut v: Vec<_> = p.iter().collect();
    v.sort_by(|a, b| b.1.0.partial_cmp(&a.1.0).unwrap());
    for (k, (s, n)) in v.into_iter().take(30) { println!("OKPROFILE {s:9.2}s {n:7} {k}"); }
}

/// A game root that is an OK base library opens cards from the library alone
/// (no installation, no Unity data). Activated once per library directory.
pub fn ensure_library(game: &Path) -> Result<bool, String> {
    if !game.join("oklib.json").is_file() { return Ok(false); }
    // Cards open on several threads at once (the crowd loads its characters in parallel). Activation is two steps
    // (the store, then the file gateway): without the lock a second thread saw the store active while gamefs was
    // not yet, and read the library directory as a game installation ("game_root missing abdata").
    static ACTIVATING: Mutex<()> = Mutex::new(());
    let _one = ACTIVATING.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(store) = active() {
        if store.mode == Mode::Replay && crate::gamefs::normalize(&store.dir) == crate::gamefs::normalize(game)
            && crate::gamefs::serves_from_library(game) { return Ok(true); }
    }
    activate(game, Mode::Replay)?;
    crate::gamefs::activate(crate::gamefs::Mode::Replay, game, game)?;
    Ok(true)
}

impl Store {
    /// Models opened since the last `forget_models` (what a card uses).
    pub fn touched(&self) -> Vec<Id> { let mut v: Vec<Id> = self.models.lock().unwrap().keys().copied().collect(); v.sort(); v }
    pub fn forget_models(&self) { self.models.lock().unwrap().clear(); }
    /// Drop one model's in-memory index (conversion: the source is done).
    pub fn forget_model(&self, id: &Id) { self.models.lock().unwrap().remove(id); }
}

pub const CARD_CHUNK: &[u8; 4] = b"okLb";
/// An OK card's own resources (card-specific zipmod data converted to OK form).
pub const PACK_CHUNK: &[u8; 4] = b"okLc";
/// The card's identity carried over when it is made again (`remake_ok_card`): 64 hex digits, the identity the card
/// had before (Tools/prepare_street_social_selection.lua `card_identity`, `apng-<id>`). Names, profiles and the save
/// know a heroine by it; without it a card remade with a new pack (same character, new bytes) became a stranger.
pub const IDENTITY_CHUNK: &[u8; 4] = b"okId";

/// A card's identity: its `okId` chunk when it has one, else the sha256 of its bytes without the `okVo` chunk (its
/// own TTS voice), as `card_identity` in Tools/prepare_street_social_selection.lua computes it.
pub fn card_identity(bytes: &[u8]) -> Option<String> {
    use sha2::Digest;
    if let Some(id) = png_chunk(bytes, IDENTITY_CHUNK) {
        return std::str::from_utf8(id).ok().filter(|s| s.len() == 64 && s.bytes().all(|b| b.is_ascii_hexdigit())).map(str::to_owned);
    }
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return None; }
    let mut hash = sha2::Sha256::new();
    hash.update(&bytes[..8]);
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().ok()?) as usize;
        let end = pos.checked_add(12 + len).filter(|e| *e <= bytes.len())?;
        let kind = &bytes[pos + 4..pos + 8];
        if kind != b"okVo" { hash.update(&bytes[pos..end]); }
        pos = end;
        if kind == b"IEND" { break; }
    }
    hash.update(&bytes[pos..]);
    Some(hash.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

/// The card with its `okId` chunk set to `id` (placed right before IEND, replacing one it had).
pub fn with_card_identity(bytes: &[u8], id: &str) -> Result<Vec<u8>, String> {
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return Err("not a PNG card".into()); }
    let mut out = bytes[..8].to_vec();
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().unwrap()) as usize;
        let end = pos.checked_add(12 + len).filter(|e| *e <= bytes.len()).ok_or("truncated PNG chunk")?;
        let kind = &bytes[pos + 4..pos + 8];
        if kind == b"IEND" {
            let mut chunk = IDENTITY_CHUNK.to_vec();
            chunk.extend_from_slice(id.as_bytes());
            out.extend_from_slice(&(id.len() as u32).to_be_bytes());
            out.extend_from_slice(&chunk);
            out.extend_from_slice(&crc32fast::hash(&chunk).to_be_bytes());
            out.extend_from_slice(&bytes[pos..]);
            return Ok(out);
        }
        if kind != IDENTITY_CHUNK { out.extend_from_slice(&bytes[pos..end]); }
        pos = end;
    }
    Err("missing PNG IEND".into())
}

/// An OK card made again keeps the identity it had; an original KKS card made into an OK card for the first time
/// has none to keep (its identity is that of the OK card made).
fn keep_identity(input: &[u8], made: Vec<u8>) -> Result<Vec<u8>, String> {
    if png_chunk(input, CARD_CHUNK).is_none() && png_chunk(input, IDENTITY_CHUNK).is_none() { return Ok(made); }
    match card_identity(input) { Some(id) => with_card_identity(&made, &id), None => Ok(made) }
}

/// `okLc` payload: zstd of this. Records are kept uncompressed inside (models compress per record with
/// LZ4; one zstd stream over all of them is several times smaller) and rebuilt into model files on mount.
#[derive(serde::Serialize, serde::Deserialize)]
struct CardPack { version: u32, index: crate::gamefs::Index, records: Vec<PackRecord> }
/// (model, query, args, value). The byte fields go through serde as byte strings: the same bincode bytes as a Vec<u8>
/// (length, then the bytes), read in one copy instead of byte by byte (a 170 MB pack: ~1.05 s -> ~0.1 s).
#[derive(serde::Serialize, serde::Deserialize)]
struct PackRecord(Id, String, #[serde(with = "pack_bytes")] Vec<u8>, #[serde(with = "pack_bytes")] Vec<u8>);
mod pack_bytes {
    pub fn serialize<S: serde::Serializer>(v: &[u8], s: S) -> Result<S::Ok, S::Error> { s.serialize_bytes(v) }
    pub fn deserialize<'de, D: serde::Deserializer<'de>>(d: D) -> Result<Vec<u8>, D::Error> {
        struct V;
        impl<'de> serde::de::Visitor<'de> for V {
            type Value = Vec<u8>;
            fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result { f.write_str("bytes") }
            fn visit_byte_buf<E: serde::de::Error>(self, v: Vec<u8>) -> Result<Vec<u8>, E> { Ok(v) }
            fn visit_bytes<E: serde::de::Error>(self, v: &[u8]) -> Result<Vec<u8>, E> { Ok(v.to_vec()) }
            fn visit_seq<A: serde::de::SeqAccess<'de>>(self, mut seq: A) -> Result<Vec<u8>, A::Error> {
                let mut out = Vec::with_capacity(seq.size_hint().unwrap_or(0));
                while let Some(b) = seq.next_element()? { out.push(b); }
                Ok(out)
            }
        }
        d.deserialize_byte_buf(V)
    }
}
const CARD_PACK_VERSION: u32 = 2;

/// Start recording what an original card needs beyond the library (call with the library active and the
/// zipmod fallback set): gamefs observations into the overlay, Unity query results into `dir`.
/// True while an original card is being turned into an OK card (its okLc pack records what the card reads).
pub fn recording_card_pack() -> bool { active().is_some_and(|s| s.recording_pack().is_some()) }

pub fn begin_card_pack(dir: &Path) -> Result<(), String> {
    let store = active().filter(|s| s.mode == Mode::Replay).ok_or("library not active")?;
    let _ = std::fs::remove_dir_all(dir);
    // Parsed bundles answer repeated queries from their own caches: start cold so every query is recorded.
    crate::unity::clear_bundle_cache();
    *store.pack.write().unwrap() = Some(Arc::new(Store::recording_in_memory()));
    crate::gamefs::set_overlay(Some(Default::default()), true);
    Ok(())
}

/// Stop recording; the `okLc` payload (None when the library covered everything).
pub fn end_card_pack() -> Result<Option<Vec<u8>>, String> {
    match take_card_pack()? { Some(pack) => encode_card_pack(&pack).map(Some), None => Ok(None) }
}

/// `end_card_pack` before it is encoded (None when the library covered everything).
fn take_card_pack() -> Result<Option<CardPack>, String> {
    let index = crate::gamefs::take_overlay().unwrap_or_default();
    let Some(pack) = active().and_then(|s| s.pack.write().unwrap().take()) else { return Ok(None) };
    if pack.records.is_some() {
        let records = pack.take_records();
        if records.is_empty() && index.files.is_empty() && index.dirs.is_empty() && index.zips.is_empty() { return Ok(None); }
        return Ok(Some(CardPack { version: CARD_PACK_VERSION, index, records }));
    }
    pack.flush();
    let mut models = Vec::new();
    let mut records = Vec::new();
    if let Ok(shards) = std::fs::read_dir(pack.dir.join("models")) {
        for file in shards.flatten().flat_map(|d| std::fs::read_dir(d.path()).into_iter().flatten().flatten()) {
            let Some(id) = file.path().file_stem().and_then(|s| s.to_str()).and_then(id_from_hex) else { continue };
            let model = load_model(&file.path(), false);
            let handle = File::open(file.path()).map_err(|e| e.to_string())?;
            for ((query, args), loc) in &model.entries {
                let raw = read_loc(&handle, loc).ok_or_else(|| format!("card pack: unreadable {query}"))?;
                records.push((id, query.clone(), args.clone(), raw));
            }
            models.push((id, std::fs::metadata(file.path()).map(|m| m.len() as usize).unwrap_or(0)));
        }
    }
    models.sort_by(|a, b| a.0.cmp(&b.0));
    records.sort_by(|a, b| (a.0, &a.1, &a.2).cmp(&(b.0, &b.1, &b.2)));
    if std::env::var_os("OKLIB_PACK_SIZES").is_some() {
        for (id, records, bytes, queries) in model_summary(&pack.dir) {
            let mut by: Vec<_> = queries.iter().filter(|(k, _)| k.starts_with("~bytes ")).collect();
            by.sort_by(|a, b| b.1.cmp(a.1));
            eprintln!("ok-extract: card pack model {} records={records} bytes={bytes} {:?}", hex(&id), by.iter().take(8).collect::<Vec<_>>());
        }
    }
    let _ = std::fs::remove_dir_all(&pack.dir);
    let summary = format!("models={} model bytes={} records={} files={} dirs={} zips={} zip entries={}", models.len(),
        models.iter().map(|m| m.1).sum::<usize>(), records.len(), index.files.len(), index.dirs.len(), index.zips.len(),
        index.zips.values().map(|z| z.entries.len()).sum::<usize>());
    if records.is_empty() && index.files.is_empty() && index.dirs.is_empty() && index.zips.is_empty() { return Ok(None); }
    eprintln!("ok-extract: card pack {summary}");
    Ok(Some(CardPack { version: CARD_PACK_VERSION, index, records: records.into_iter().map(|(a, b, c, d)| PackRecord(a, b, c, d)).collect() }))
}

/// The `okLc` payload of a pack: zstd (OK_PACK_LEVEL, default 3) on all hardware threads. Level 9 made cards ~5%
/// smaller for several times the CPU, an eighth of a batch conversion's; reading is as fast either way.
/// Written in independent frames of this many bytes (each states its size), compressed and decoded in parallel.
const PACK_FRAME: usize = 16 << 20;

fn encode_card_pack(pack: &CardPack) -> Result<Vec<u8>, String> {
    use rayon::prelude::*;
    let raw = bincode::serialize(pack).map_err(|e| e.to_string())?;
    let level = std::env::var("OK_PACK_LEVEL").ok().and_then(|v| v.parse().ok()).unwrap_or(3);
    let frames = raw.par_chunks(PACK_FRAME).map(|part| zstd::bulk::compress(part, level)).collect::<Result<Vec<_>, _>>()
        .map_err(|e| e.to_string())?;
    Ok(frames.concat())
}

/// An `okLc` payload decompressed. Concatenated frames are one zstd stream to any reader; when every frame states
/// its size (packs written since 2026-10-29) they are decoded in parallel into their places: one frame of a 400 MB
/// pack took ~0.2 s of the card's open.
pub(crate) fn decode_card_pack_bytes(chunk: &[u8]) -> Result<Vec<u8>, String> {
    use rayon::prelude::*;
    use zstd::zstd_safe;
    let whole = || zstd::stream::decode_all(chunk).map_err(|e| format!("card pack: {e}"));
    let (mut frames, mut pos) = (Vec::new(), 0);
    while pos < chunk.len() {
        let len = zstd_safe::find_frame_compressed_size(&chunk[pos..]).ok().filter(|&l| l > 0);
        let size = zstd_safe::get_frame_content_size(&chunk[pos..]).ok().flatten();
        let (Some(len), Some(size)) = (len, size) else { return whole() };
        frames.push((pos, len, size as usize));
        pos += len;
    }
    if frames.len() < 2 { return whole(); }
    let mut out = vec![0u8; frames.iter().map(|f| f.2).sum()];
    let mut places = Vec::with_capacity(frames.len());
    let mut rest = &mut out[..];
    for &(_, _, size) in &frames { let (place, tail) = rest.split_at_mut(size); places.push(place); rest = tail; }
    frames.par_iter().zip(places.into_par_iter()).try_for_each(|(&(at, len, size), place)| {
        match zstd::bulk::decompress_to_buffer(&chunk[at..at + len], place) {
            Ok(n) if n == size => Ok(()),
            Ok(n) => Err(format!("card pack: frame of {n} bytes, {size} stated")),
            Err(e) => Err(format!("card pack: {e}")),
        }
    })?;
    Ok(out)
}

/// What a sealing or converting open copied from the library, added to a pack (its own entries keep precedence).
fn merge_additions(pack: &mut CardPack, adds: crate::okstrict::Additions) {
    for (key, entry) in adds.index.files { pack.index.files.entry(key).or_insert(entry); }
    for (key, dir) in adds.index.dirs { pack.index.dirs.entry(key).or_insert(dir); }
    for (key, zip) in adds.index.zips {
        let own = pack.index.zips.entry(key).or_insert_with(|| crate::gamefs::ZipIndex { names: zip.names.clone(), entries: Default::default() });
        for (i, entry) in zip.entries { own.entries.entry(i).or_insert(entry); }
    }
    let have: std::collections::HashSet<(Id, String, Vec<u8>)> = pack.records.iter().map(|r| (r.0, r.1.clone(), r.2.clone())).collect();
    pack.records.extend(adds.records.into_iter().filter(|(k, _)| !have.contains(k)).map(|((id, query, args), value)| PackRecord(id, query, args, value)));
    pack.records.sort_by(|a, b| (a.0, &a.1, &a.2).cmp(&(b.0, &b.1, &b.2)));
}

/// A PNG chunk's data.
pub fn png_chunk<'a>(bytes: &'a [u8], kind: &[u8; 4]) -> Option<&'a [u8]> {
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return None; }
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().ok()?) as usize;
        if &bytes[pos + 4..pos + 8] == kind { return bytes.get(pos + 8..pos + 8 + len); }
        if &bytes[pos + 4..pos + 8] == b"IEND" { return None; }
        pos += 12 + len;
    }
    None
}

/// Mounts an OK card's pack over the active library (overlay index, pack models extracted once to a
/// cache keyed by the pack's hash). Unmounted when the guard drops. None: the card carries no pack.
pub fn mount_card_pack(card: &Path) -> Result<Option<PackMount>, String> {
    let bytes = crate::card::card_file_bytes(card).map_err(|e| e.to_string())?;
    // An OK card opens from its own data and the shared resources only (okstrict.rs), a card without a pack too; an
    // original card being made into one (its pack recorded) reads its zipmods.
    let strict = (png_chunk(&bytes, CARD_CHUNK).is_some() && !recording_card_pack()).then(crate::okstrict::for_open);
    let tree = crate::tree_answers::parse(&bytes);
    if strict.is_some() && tree.is_none() && !crate::tree_answers::recording() {
        return Err(format!("{}: the OK card has no okLt answers (made before 2026-10-28; ok-extract X:/NoGame --oklib seal \
            <base library> <card> completes it)", card.display()));
    }
    let Some(chunk) = png_chunk(&bytes, PACK_CHUNK) else {
        if strict.is_none() { return Ok(None) }
        active().filter(|s| s.mode == Mode::Replay).ok_or("library not active")?;
        return Ok(Some(mount_scope(PackScope { overlay: crate::gamefs::replay_overlay_of(Default::default()), store: None,
            identity: String::new(), mods_identity: String::new(), fallback_used: AtomicBool::new(false), pool: std::sync::OnceLock::new(), tree, strict })));
    };
    // Each open has its own pack (PackScope), held in memory for that open only: many pack-carrying cards open at
    // once, and nothing is written to disk.
    active().filter(|s| s.mode == Mode::Replay).ok_or("library not active")?;
    let (profile, t) = (std::env::var_os("OK_PROFILE").is_some(), std::time::Instant::now());
    let raw = decode_card_pack_bytes(chunk)?;
    if profile { eprintln!("ok-extract: PROFILE pack zstd {} -> {} bytes {:>8.1}ms", chunk.len(), raw.len(), t.elapsed().as_secs_f64() * 1e3); }
    let pack: CardPack = bincode::deserialize(&raw).map_err(|e| format!("card pack: {e}"))?;
    if pack.version != CARD_PACK_VERSION { return Err(format!("card pack version {} (expected {CARD_PACK_VERSION})", pack.version)); }
    if profile { eprintln!("ok-extract: PROFILE pack decoded {:>8.1}ms ({} records)", t.elapsed().as_secs_f64() * 1e3, pack.records.len()); }
    let CardPack { index, records, .. } = pack;
    LAST_PACK_MODELS.store(records.iter().map(|r| r.0).collect::<std::collections::BTreeSet<_>>().len() as u64, Ordering::Relaxed);
    let pack_store = Arc::new(Store::in_memory(records));
    if profile { eprintln!("ok-extract: PROFILE pack store in memory {:>8.1}ms", t.elapsed().as_secs_f64() * 1e3); }
    // What the pack serves under mods/ (zipmods, their entries, listings): the mods-tree memos only depend on this.
    let mods_view = {
        let in_mods = |k: &String| k.to_ascii_lowercase().replace('\\', "/").contains("mods/");
        let files: Vec<_> = index.files.iter().filter(|(k, _)| in_mods(k)).collect();
        let dirs: Vec<_> = index.dirs.iter().filter(|(k, _)| in_mods(k)).collect();
        // Nothing under mods/: the library's own tree, the same memos as without a pack (and as a prewarm).
        if files.is_empty() && dirs.is_empty() && index.zips.is_empty() { String::new() }
        else { bincode::serialize(&(files, dirs, &index.zips)).map(|b| hex(&hash16(&b))).unwrap_or_else(|_| hex(&hash16(chunk))) }
    };
    Ok(Some(mount_scope(PackScope { overlay: crate::gamefs::replay_overlay_of(index), store: Some(pack_store), identity: hex(&hash16(chunk)),
        mods_identity: mods_view, fallback_used: AtomicBool::new(false), pool: std::sync::OnceLock::new(), tree, strict })))
}

fn mount_scope(scope: PackScope) -> PackMount {
    let scope = Arc::new(scope);
    ACTIVE_SCOPES.lock().unwrap_or_else(|e| e.into_inner()).push(Arc::downgrade(&scope));
    let previous = PACK_SCOPE.with(|c| c.replace(Some(scope.clone())));
    PackMount { scope, previous }
}

/// A replayed OK card's pack (okLc) for one open: its overlay index, models and identities. Set on the thread that
/// opens the card and on the threads of its own pool (pack_parallel), so cards opening at once each read their own
/// pack (it used to be process-wide: one pack-carrying card opened at a time, a hundred girls one after another).
pub struct PackScope {
    pub overlay: Arc<crate::gamefs::Overlay>,
    /// The pack's models (none: an OK card without a pack).
    store: Option<Arc<Store>>,
    identity: String,
    mods_identity: String,
    /// The open read installed zipmods (gamefs::fallback_used, per open).
    pub fallback_used: AtomicBool,
    /// What the card answers of its open's mods-tree questions (`okLt`, tree_answers.rs).
    pub tree: Option<Arc<crate::tree_answers::Table>>,
    /// An OK card's open: its own data and the shared resources only (okstrict.rs).
    pub strict: Option<Arc<crate::okstrict::Strict>>,
    pool: std::sync::OnceLock<Option<rayon::ThreadPool>>,
}
thread_local! { static PACK_SCOPE: std::cell::RefCell<Option<Arc<PackScope>>> = const { std::cell::RefCell::new(None) }; }
// A thread of an open's pool: weak, so the pool (held by the scope) ends with the open.
thread_local! { static POOL_SCOPE: std::cell::RefCell<std::sync::Weak<PackScope>> = const { std::cell::RefCell::new(std::sync::Weak::new()) }; }
/// This thread works for an open with its own pack (not the fallback to the only one open).
pub fn pack_scope_here() -> bool { own_scope().is_some() }
/// The open's pack on this thread, to hand to a job it starts elsewhere (enter_pack_scope there).
pub fn current_pack_scope() -> Option<Arc<PackScope>> { own_scope() }
/// Works for that open's pack until the guard drops (a spawned job of the open).
pub fn enter_pack_scope(scope: Option<Arc<PackScope>>) -> PackScopeGuard {
    let previous = scope.map(|s| PACK_SCOPE.with(|c| c.replace(Some(s))));
    PackScopeGuard(previous)
}
pub struct PackScopeGuard(Option<Option<Arc<PackScope>>>);
impl Drop for PackScopeGuard {
    fn drop(&mut self) { if let Some(previous) = self.0.take() { PACK_SCOPE.with(|c| *c.borrow_mut() = previous); } }
}
fn own_scope() -> Option<Arc<PackScope>> { PACK_SCOPE.with(|c| c.borrow().clone()).or_else(|| POOL_SCOPE.with(|c| c.borrow().upgrade())) }
/// Scopes of the opens running now (the fallback for a thread outside every open's pool).
static ACTIVE_SCOPES: Mutex<Vec<std::sync::Weak<PackScope>>> = Mutex::new(Vec::new());
/// The pack of the open this thread works for: its own, else the only one open (a worker outside every open's pool
/// while one card opens). Several open and none on this thread: none, and said once.
pub fn pack_scope() -> Option<Arc<PackScope>> {
    if let Some(s) = own_scope() { return Some(s); }
    let mut active = ACTIVE_SCOPES.lock().unwrap_or_else(|e| e.into_inner());
    active.retain(|w| w.strong_count() > 0);
    match active.len() {
        0 => None,
        1 => active[0].upgrade(),
        _ => {
            static SAID: AtomicBool = AtomicBool::new(false);
            if !SAID.swap(true, Ordering::Relaxed) { eprintln!("ok-extract: a card pack was read outside its open's threads while several cards open (pack_parallel missing) at{}", std::backtrace::Backtrace::force_capture()); }
            None
        }
    }
}
/// Runs parallel work of the current open (its rayon iterators) in its own pool, whose threads carry its pack; no
/// open on this thread: as it is.
pub fn pack_parallel<R: Send>(work: impl FnOnce() -> R + Send) -> R {
    let Some(scope) = own_scope() else { return work() };
    let pool = scope.pool.get_or_init(|| {
        let weak = Arc::downgrade(&scope);
        let workers = crate::cpu_threads().clamp(2, 8);
        rayon::ThreadPoolBuilder::new().num_threads(workers).thread_name(|i| format!("OKCardPack{i}"))
            .start_handler(move |_| POOL_SCOPE.with(|c| *c.borrow_mut() = weak.clone())).build().ok()
    });
    match pool {
        // Already one of its pool's threads: stay.
        Some(pool) if pool.current_thread_index().is_some() => work(),
        Some(pool) => pool.install(work),
        None => work(),
    }
}

static MOUNTED_PACK: Mutex<String> = Mutex::new(String::new());
/// Identity of the card pack mounted over the library right now ("" when none): per-process memos of what the
/// library serves must not mix two cards' own zipmods.
pub fn mounted_pack_identity() -> String {
    if let Some(scope) = pack_scope() { return scope.identity.clone(); }
    MOUNTED_PACK.lock().unwrap_or_else(|e| e.into_inner()).clone()
}
static MOUNTED_PACK_MODS: Mutex<String> = Mutex::new(String::new());
/// Identity of what the mounted card pack serves under mods/ ("" when none). Memos of the mods tree (manifest scans,
/// GUID lookups, uncensor sources) key on this instead of the whole pack, so opening another card that adds no
/// zipmods of its own reuses them instead of rescanning ~7000 zipmods.
pub fn mounted_pack_mods_identity() -> String {
    if let Some(scope) = pack_scope() { return scope.mods_identity.clone(); }
    MOUNTED_PACK_MODS.lock().unwrap_or_else(|e| e.into_inner()).clone()
}
/// Models in the last mounted OK card pack (diagnostics).
pub static LAST_PACK_MODELS: AtomicU64 = AtomicU64::new(0);
pub struct PackMount { scope: Arc<PackScope>, previous: Option<Arc<PackScope>> }
impl PackMount {
    /// The OK card's strict open (okstrict.rs).
    pub fn strict(&self) -> Option<Arc<crate::okstrict::Strict>> { self.scope.strict.clone() }
}
impl Drop for PackMount {
    fn drop(&mut self) {
        PACK_SCOPE.with(|c| *c.borrow_mut() = self.previous.take());
        ACTIVE_SCOPES.lock().unwrap_or_else(|e| e.into_inner()).retain(|w| w.upgrade().is_some_and(|s| !Arc::ptr_eq(&s, &self.scope)));
    }
}

/// `library_card` plus the card's own pack (`okLc`), replacing earlier ones.
pub fn library_card_with_pack(original: &[u8], payload: &[u8], pack: Option<&[u8]>) -> Result<Vec<u8>, String> {
    let card = library_card(original, payload)?;
    let Some(pack) = pack else { return Ok(card) };
    let (iend, _) = crate::portable::png_snapshot(&card)?;
    let mut out = card[..iend].to_vec();
    out.extend_from_slice(&(pack.len() as u32).to_be_bytes());
    let mut chunk = PACK_CHUNK.to_vec();
    chunk.extend_from_slice(pack);
    out.extend_from_slice(&chunk);
    out.extend_from_slice(&crc32fast::hash(&chunk).to_be_bytes());
    out.extend_from_slice(&card[iend..]);
    Ok(out)
}

/// Derived, deletable cache of sessions built from the library (a library
/// card opens in seconds the first time, instantly afterwards). Keyed by the
/// card bytes, outfit, requested clothes states and the library identity.
/// Opens read and write the open cache (not under OK_NOCACHE).
pub fn session_cache_enabled() -> bool { session_cache_dir().is_some() }
fn session_cache_dir() -> Option<PathBuf> {
    if std::env::var_os("OK_NOCACHE").is_some() { return None; }
    session_cache_location()
}
/// Claim on building one open-cache entry (`<key>.warming` in the cache folder), so the reader and the warm processes
/// never build the same entry at once. None: another live process holds it. Removed on drop.
pub struct WarmClaim(PathBuf, String);
impl Drop for WarmClaim {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.0);
        HELD_CLAIMS.lock().unwrap_or_else(|e| e.into_inner()).retain(|k| *k != self.1);
    }
}
/// Keys this process has claimed: its own claim is never waited for (ok-card-warm claims, then opens).
static HELD_CLAIMS: Mutex<Vec<String>> = Mutex::new(Vec::new());
fn claim_held_here(key: &str) -> bool { HELD_CLAIMS.lock().unwrap_or_else(|e| e.into_inner()).iter().any(|k| k == key) }
pub fn claim_warming(key: &str) -> Option<WarmClaim> {
    if claim_held_here(key) { return None; }
    let dir = session_cache_location()?;
    let claim = dir.join(format!("{key}.warming"));
    let _ = std::fs::create_dir_all(&dir);
    // A claim older than 3 minutes belongs to a process that died mid-build.
    let stale = std::fs::metadata(&claim).and_then(|m| m.modified()).ok()
        .and_then(|t| std::time::SystemTime::now().duration_since(t).ok()).is_some_and(|age| age > std::time::Duration::from_secs(180));
    if stale { let _ = std::fs::remove_file(&claim); }
    std::fs::OpenOptions::new().write(true).create_new(true).open(&claim).ok()?;
    HELD_CLAIMS.lock().unwrap_or_else(|e| e.into_inner()).push(key.to_owned());
    Some(WarmClaim(claim, key.to_owned()))
}
/// Waits while another process (ok-card-warm) holds the `<key>.warming` claim for this entry, up to 30 s. True when
/// it waited (the entry may be in the cache now). A claim older than 3 minutes is a dead process's and is ignored.
pub fn wait_for_warming(key: &str) -> bool {
    if claim_held_here(key) { return false; }
    let Some(claim) = session_cache_dir().map(|d| d.join(format!("{key}.warming"))) else { return false };
    let fresh = |p: &Path| std::fs::metadata(p).and_then(|m| m.modified()).ok()
        .and_then(|t| std::time::SystemTime::now().duration_since(t).ok()).is_some_and(|age| age < std::time::Duration::from_secs(180));
    if !fresh(&claim) { return false; }
    let start = std::time::Instant::now();
    while fresh(&claim) && start.elapsed() < std::time::Duration::from_secs(30) { std::thread::sleep(std::time::Duration::from_millis(40)); }
    eprintln!("ok-extract: waited {:.2}s for the warm process building {key}", start.elapsed().as_secs_f64());
    true
}
/// Where the game keeps opened OK cards (also when this process itself runs with OK_NOCACHE).
/// Answers about the archives the active library records (each archive's GUIDs, its MaterialEditor section), kept
/// across processes: a recorded archive cannot change while the library's index is unchanged, and the first card of
/// every process read ~7000 manifests again for them (~1 s). Keyed by the library folder, its index files' sizes and
/// times, and `what` with its own version; a missing or unreadable file is just rebuilt.
fn library_scan_path(what: &str) -> Option<PathBuf> {
    use sha2::Digest;
    let lib = active_dir()?;
    let mut h = sha2::Sha256::new();
    h.update(crate::gamefs::normalize(&lib).as_bytes());
    for f in ["oklib.json", "sources.okidx", "converted.tsv"] {
        let m = std::fs::metadata(lib.join(f)).ok()?;
        h.update(m.len().to_le_bytes());
        h.update(format!("{:?}", m.modified().ok()).as_bytes());
    }
    h.update(what.as_bytes());
    Some(session_cache_location()?.join("library-scans").join(format!("{what}-{}.bin", hex(&h.finalize()[..12]))))
}
pub fn library_scan_load<T: serde::de::DeserializeOwned>(what: &str) -> Option<T> {
    bincode::deserialize(&std::fs::read(library_scan_path(what)?).ok()?).ok()
}
pub fn library_scan_save<T: serde::Serialize>(what: &str, value: &T) {
    let Some(path) = library_scan_path(what) else { return };
    let Ok(bytes) = bincode::serialize(value) else { return };
    let _ = std::fs::create_dir_all(path.parent().unwrap_or(&path));
    static WRITES: AtomicU64 = AtomicU64::new(0);
    let tmp = path.with_extension(format!("{}.{}.tmp", std::process::id(), WRITES.fetch_add(1, Ordering::Relaxed)));
    if std::fs::write(&tmp, bytes).is_ok() && std::fs::rename(&tmp, &path).is_err() { let _ = std::fs::remove_file(&tmp); }
}
/// The folder of the disk caches (opened cards, derived library textures, library scans): none unless
/// `OK_LIBRARY_CACHE` names one. Since 2026-10-28 an OK card opens from itself and the library every time (it used to
/// default to %LOCALAPPDATA%\OpenKoikatsu\oklib-cache).
pub fn session_cache_location() -> Option<PathBuf> {
    std::env::var_os("OK_LIBRARY_CACHE").map(PathBuf::from)
}
/// Bump when extraction changes what an opened card contains (material/shader resolution, baked textures): cached
/// sessions are keyed by the card and the library only, so they would keep the old result.
/// 2: xukmi studio-alpha Material Editor shaders are applied (translucent hair-shadow overlays).
/// 3: hair parts with id 0 are loaded (vanilla back hair 0 "ロングヘア" was skipped as if empty).
/// 4: small streamed textures keep their .resS texels (the inline-array repair replaced them with header bytes; 85
///    textures in 37 vanilla bundles, e.g. co_top_00, bo_hair_b_00, mt_eye_white_00).
/// 5: hair under cf_J_FaceUp_ty gets its world geometry at open (it stayed head-local at the origin).
/// 6: parts of Unity Standard-family materials with _EMISSION keep their _EmissionMap (shader_textures).
/// 7: lightmapped map renderers carry their lightmap UVs (uv4), index and texture (okmodel_view::bake_lightmap).
/// 8: (C master) accessory DynamicBones take the card's DynamicBoneEditor parameters (weight, damping, elasticity, stiffness...).
/// 8: (E duo) an OK Studio eye set's moving gag eye carries its animation values (eye_set::apply_gag_animation) and whole
///    sprite sheet.
/// 9: the E (duo) line, above C master's 8: both lines share LOCALAPPDATA/OpenKoikatsu/oklib-cache, so one number
///    must not mean two different opens.
/// 10: C master and E duo merged (both 8s together); above either line's own number.
/// 11: Standard materials keep their _MetallicGlossMap (with _METALLICGLOSSMAP) and Studio map parts their reflection
///     probe atlas (`_OKReflection`).
/// 12: (E duo) Studio map parts take their Animators' material values at the clip's first frame, not only the steady
///     ones; inactive time-of-day parts (SunLightInfo visibleList) come with the map.
/// 13: Studio MDK keeps its authored normal, parallax, metallic and emission slots.
///     Directional lightmaps carry the direction/rebalancing texture separately.
/// 14: Studio Standard/MDK/particle Texture2D and baked lightmap rows follow rendering UVs.
/// 15: Studio renderers carry their native shadow casting mode, layer and enabled state.
/// 18: static and settled Animator activeSelf gate each prefab renderer instance through its ancestors.
/// 21: a Standard map part takes the reflection probe whose box intersects its bounds (was: holds its centre).
/// 22: interleaved records keep uv0 (offset+8 is the next channel). 23: built-in primitives and sky domes (_OKSkyCube).
/// 24: KK_InvisibleBody hides the body, head and hair parts.
/// 25: tiled main UVs (beyond ±8, a Studio floor's 0..10) are kept instead of zeroed; Studio main_item_studio main
/// texture and colour mask follow the (u, 1-v) rows. 26: not main_item_studio_add (its rows were already right).
/// 27: coincident opaque/masked layers (Material Editor copies, duplicated accessories) step outward in draw order.
/// 28: including an accessory's private-rig source mesh, which every pose is skinned from.
/// 29: the geometry stays put again; parts carry their draw layer and the engine orders them (OpaqueSortPriority).
const SESSION_CACHE_REVISION: u32 = 29;
/// What the library contributes to an open, by content (not file times): the same library in another clone, after a
/// git checkout or a copy gives the same identity, so its cached opens stay valid there. Memoized per process.
fn library_identity(lib: &Path) -> String {
    use sha2::Digest;
    static MEMO: std::sync::OnceLock<std::sync::Mutex<HashMap<(PathBuf, Vec<(u64, Option<std::time::SystemTime>)>), String>>> = std::sync::OnceLock::new();
    const FILES: [&str; 4] = ["oklib.json", "sources.okidx", "converted.tsv", "edits.stamp"];
    // Cheap stamp to notice changes inside this process; the identity itself is content-based.
    let stamp: Vec<_> = FILES.iter().map(|f| std::fs::metadata(lib.join(f)).map(|m| (m.len(), m.modified().ok())).unwrap_or((0, None))).collect();
    let memo_key = (lib.to_path_buf(), stamp);
    let memo = MEMO.get_or_init(Default::default);
    if let Some(v) = memo.lock().unwrap().get(&memo_key) { return v.clone(); }
    let mut h = sha2::Sha256::new();
    for f in FILES {
        if f == "edits.stamp" { continue; }
        let p = lib.join(f);
        let Ok(mut file) = std::fs::File::open(&p) else { h.update(b"-"); continue; };
        h.update(f.as_bytes());
        use std::io::{Read, Seek, SeekFrom};
        if f == "sources.okidx" {
            // Binary index (Git LFS, byte-identical everywhere): size plus first and last MiB; it is rewritten whole.
            const SPAN: u64 = 1 << 20;
            let len = file.metadata().map(|m| m.len()).unwrap_or(0);
            h.update(len.to_le_bytes());
            let mut buf = vec![0u8; SPAN.min(len) as usize];
            if file.read_exact(&mut buf).is_ok() { h.update(&buf); }
            if len > SPAN && file.seek(SeekFrom::End(-(SPAN as i64))).is_ok() && file.read_exact(&mut buf).is_ok() { h.update(&buf); }
        } else {
            // Text: git may check it out with CRLF in one clone and LF in another.
            let mut buf = Vec::new();
            let _ = file.read_to_end(&mut buf);
            buf.retain(|&b| b != b'\r');
            h.update(&buf);
        }
    }
    // Model edits (<id>.okedit.json beside the models): their contents, not the time edits.stamp was written.
    let mut edits: Vec<PathBuf> = Vec::new();
    for dir in std::fs::read_dir(lib.join("models")).into_iter().flatten().flatten() {
        for e in std::fs::read_dir(dir.path()).into_iter().flatten().flatten() {
            if e.file_name().to_string_lossy().ends_with(".okedit.json") { edits.push(e.path()); }
        }
    }
    edits.sort();
    for e in &edits {
        h.update(e.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default().as_bytes());
        let mut text = std::fs::read(e).unwrap_or_default();
        text.retain(|&b| b != b'\r');
        h.update(&text);
    }
    let id = hex(&h.finalize()[..16]);
    memo.lock().unwrap().insert(memo_key, id.clone());
    id
}
pub fn session_cache_key(lib: &Path, card: &Path, outfit: i32) -> Option<String> {
    use sha2::Digest;
    let bytes = std::fs::read(card).ok()?;
    let mut h = sha2::Sha256::new();
    // okopen3: bumped once for the face _AlphaMask (masked face); later output changes bump SESSION_CACHE_REVISION.
    h.update(b"okopen3");
    // Without baked opens (okBk): a baked card names the same entries as the plain card.
    h.update(&*crate::cardbake::strip(&bytes));
    h.update(outfit.to_le_bytes());
    // The effective clothes states: the reader passes them explicitly (all worn = zeros), a warm process does not;
    // both must name the same entry. All worn hashes as nothing, the key entries were first stored under.
    let states = crate::unity::requested_clothes_states();
    h.update(if states == [0u8; 9] { Vec::new() } else { states.to_vec() });
    match card_models(&bytes) {
        // An OK card names every library model it uses (okLb): only those matter, so the library growing (new models,
        // a longer index) keeps its cached opens. A model changes by being rebuilt (size) or edited (.okedit.json).
        Some(models) => for id in models {
            let dir = lib.join("models").join(id.get(..2).unwrap_or("__"));
            h.update(id.as_bytes());
            h.update(std::fs::metadata(dir.join(format!("{id}.okmodel"))).map(|m| m.len()).unwrap_or(0).to_le_bytes());
            let mut edit = std::fs::read(dir.join(format!("{id}.okedit.json"))).unwrap_or_default();
            edit.retain(|&b| b != b'\r');
            h.update(&edit);
        },
        // An original card resolves through the whole library.
        None => h.update(library_identity(lib).as_bytes()),
    }
    h.update(env!("CARGO_PKG_VERSION").as_bytes());
    h.update(SESSION_CACHE_REVISION.to_le_bytes());
    // The extractor's own source (build.rs): the cache is shared by every checkout and build on this machine (C and E,
    // the reader, the street, packaged stages), so an entry stored by other code is never reused, revision bump or not.
    h.update(env!("OK_EXTRACT_SOURCE_HASH").as_bytes());
    // An OK Studio character state is another entry; without one the key stays as before.
    if let Some(status) = crate::studio_status::cache_key_bytes() { h.update(b"studio-status"); h.update(&status); }
    // Raw texture containers (OK_RAW_TEXTURES=1) are a different entry from the PNG one.
    if crate::teximg::raw_enabled() { h.update(b"raw-textures"); }
    // A GPU-skinned open (the live reader) is another entry, and so is its head with OK_HEAD_GPUSKIN: without it the
    // head is baked static (kind 0) and ABMX head bones / face sliders never reach the face. Without OK_GPU_SKIN the
    // key stays as before.
    if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1") {
        h.update(b"gpu-skin");
        if crate::profile::var("OK_HEAD_GPUSKIN").is_ok() { h.update(b"head-skin"); }
    }
    // The installed zipmods (fallback) are not part of the key: an OK card that carries its own pack never reads them.
    // An open that did read them stores their identity in the entry, and the entry only counts while they are unchanged.
    Some(hex(&h.finalize()[..16]))
}
const ENTRY_MAGIC: &[u8; 4] = b"OKC2";
/// An entry for this key exists in the open cache.
pub fn session_cache_has(key: &str) -> bool {
    session_cache_dir().is_some_and(|d| d.join(format!("{key}.okpreview")).is_file())
}
/// Cached open for this key, if it is still valid: an entry that used the zipmod fallback must find it unchanged.
pub fn session_cache_load(key: &str) -> Option<Vec<u8>> {
    let path = session_cache_dir()?.join(format!("{key}.okpreview"));
    let bytes = std::fs::read(&path).ok()?;
    if bytes.len() < 8 || &bytes[..4] != ENTRY_MAGIC { return None; }
    let n = u32::from_le_bytes(bytes[4..8].try_into().ok()?) as usize;
    let inputs = std::str::from_utf8(bytes.get(8..8 + n)?).ok()?;
    // What the open read besides the card and the library (see session_cache_store_in): both must be the same now.
    // An entry without the configuration identity is from before it was stored and is rebuilt.
    let (fallback, config) = inputs.split_once(ENTRY_INPUT_SEPARATOR)?;
    if !fallback.is_empty() && fallback != crate::gamefs::fallback_identity() { return None; }
    if config != crate::mods::uncensor_config_identity() {
        eprintln!("ok-extract: open cache {key} was built with another UncensorSelector configuration ({config}); rebuilding");
        return None;
    }
    let out = lz4_flex::block::decompress_size_prepended(&bytes[8 + n..]).ok()?;
    // Least recently used goes first when the cache is trimmed: a hit counts as a use.
    if let Ok(f) = std::fs::File::options().write(true).open(&path) { let _ = f.set_modified(std::time::SystemTime::now()); }
    Some(out)
}
/// `fallback`: gamefs::fallback_identity() when the open read installed zipmods, else empty.
pub fn session_cache_store(key: &str, preview: &[u8], fallback: &str) {
    let Some(dir) = session_cache_dir() else { return; };
    session_cache_store_in(&dir, key, preview, fallback);
}
/// Store a verified open in the game's cache (acceptance runs with OK_NOCACHE, the game does not).
pub fn session_cache_warm(key: &str, preview: &[u8], fallback: &str) {
    if let Some(dir) = session_cache_location() { session_cache_store_in(&dir, key, preview, fallback); }
}
/// Cache budget: OK_LIBRARY_CACHE_GB (default 64). An open is ~60 MB, so 4 GB held fewer outfits than one card set.
fn session_cache_budget() -> u64 {
    let gb = std::env::var("OK_LIBRARY_CACHE_GB").ok().and_then(|v| v.parse::<u64>().ok()).filter(|v| *v > 0).unwrap_or(64);
    gb << 30
}
/// Separates the inputs an entry records: the zipmod fallback identity, then the UncensorSelector configuration.
const ENTRY_INPUT_SEPARATOR: char = '\u{1f}';
/// What an entry records besides the card and the library (session_cache_store_in), read now: on the opening thread,
/// whose card pack may hold the UncensorSelector configuration.
thread_local! { static DEFER_STORE: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
/// Opens on this thread store their open-cache entry on another thread (the game: the process outlives it, and the
/// girl is shown without waiting for the entry to be written). Command-line tools store it before they return.
pub struct DeferCacheStore(bool);
impl DeferCacheStore { pub fn begin() -> Self { Self(DEFER_STORE.with(|d| d.replace(true))) } }
impl Drop for DeferCacheStore { fn drop(&mut self) { DEFER_STORE.with(|d| d.set(self.0)); } }
pub fn defer_cache_store() -> bool { DEFER_STORE.with(|d| d.get()) }
pub fn session_cache_inputs(fallback: &str) -> String { format!("{fallback}{ENTRY_INPUT_SEPARATOR}{}", crate::mods::uncensor_config_identity()) }
/// session_cache_store with the inputs read on the opening thread (a store finished on another thread).
pub fn session_cache_store_prepared(key: &str, preview: &[u8], inputs: &str) {
    let Some(dir) = session_cache_dir() else { return; };
    session_cache_write(&dir, key, preview, inputs);
}
fn session_cache_store_in(dir: &Path, key: &str, preview: &[u8], fallback: &str) { session_cache_write(dir, key, preview, &session_cache_inputs(fallback)); }
fn session_cache_write(dir: &Path, key: &str, preview: &[u8], inputs: &str) {
    let dir = dir.to_owned();
    if std::fs::create_dir_all(&dir).is_err() { return; }
    // (stores of one process may run at once: deferred ones on their own threads)
    static WRITES: AtomicU64 = AtomicU64::new(0);
    let tmp = dir.join(format!("{key}.{}.{}.tmp", std::process::id(), WRITES.fetch_add(1, Ordering::Relaxed)));
    // The configuration decides the body when the card names none (a packaged game that did not find it stored the
    // censored base body here, and every reader, C and E alike, drew that). The key is only the card and the library.
    let mut data = Vec::with_capacity(preview.len() / 2);
    data.extend_from_slice(ENTRY_MAGIC);
    data.extend_from_slice(&(inputs.len() as u32).to_le_bytes());
    data.extend_from_slice(inputs.as_bytes());
    data.extend_from_slice(&lz4_flex::block::compress_prepend_size(preview));
    if std::fs::write(&tmp, &data).is_ok() {
        if std::fs::rename(&tmp, dir.join(format!("{key}.okpreview"))).is_err() { let _ = std::fs::remove_file(&tmp); }
    }
    // Bounded: drop the least recently used entries beyond the budget.
    let mut files: Vec<_> = std::fs::read_dir(&dir).into_iter().flatten().flatten()
        .filter_map(|e| { let m = e.metadata().ok()?; Some((m.modified().ok()?, m.len(), e.path())) }).collect();
    files.sort();
    let budget = session_cache_budget();
    let mut total: u64 = files.iter().map(|f| f.1).sum();
    for (_, len, path) in files { if total <= budget { break; } if std::fs::remove_file(&path).is_ok() { total -= len; } }
}

/// The card names OK base library models (`okLb`, written by `--oklib card`).
pub fn is_library_card(card: &Path) -> bool {
    let Ok(bytes) = std::fs::read(card) else { return false; };
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return false; }
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().unwrap()) as usize;
        let kind = &bytes[pos + 4..pos + 8];
        if kind == CARD_CHUNK { return true; }
        if kind == b"IEND" { return false; }
        pos += 12 + len;
    }
    false
}

/// Whether a PNG card carries a chunk of this type before IEND.
pub fn png_has_chunk(bytes: &[u8], kind: &[u8; 4]) -> bool {
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return false; }
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().unwrap()) as usize;
        let k = &bytes[pos + 4..pos + 8];
        if k == kind { return true; }
        if k == b"IEND" { return false; }
        pos = pos.saturating_add(12 + len);
    }
    false
}

/// The card without OK chunks (`okLb`, `okLc`, `okLt`, `okId`) and a legacy `okPv` snapshot: the original KKS card data.
pub fn strip_ok_chunks(bytes: &[u8]) -> Result<Vec<u8>, String> {
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return Err("not a PNG card".into()); }
    let mut out = bytes[..8].to_vec();
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().unwrap()) as usize;
        let end = pos.checked_add(12 + len).filter(|e| *e <= bytes.len()).ok_or("truncated PNG chunk")?;
        let kind = &bytes[pos + 4..pos + 8];
        if kind == b"IEND" { out.extend_from_slice(&bytes[pos..]); return Ok(out); }
        if kind != CARD_CHUNK && kind != PACK_CHUNK && kind != crate::tree_answers::CHUNK && kind != IDENTITY_CHUNK && kind != b"okPv" { out.extend_from_slice(&bytes[pos..end]); }
        pos = end;
    }
    Err("missing PNG IEND".into())
}

/// An OK card made from any card: an original KKS card, an OK card whose card data was edited (appearance, clothes,
/// parameters), or a legacy okPv card. OK chunks are dropped and the remaining original card data is converted again
/// from the base library, the card's zipmods and the zipmods the game uses for it (`make_ok_card`), so the models
/// list, digest and pack always match the card data.
pub fn remake_ok_card(lib: &Path, game: &Path, card: &Path) -> Result<(String, usize, Vec<u8>, usize), String> {
    let source = card_as_made(card)?;
    let (digest, parts, bytes, pack) = make_ok_card(lib, game, source.as_ref().map_or(card, |t| &t.0))?;
    let input = std::fs::read(card).map_err(|e| e.to_string())?;
    Ok((digest, parts, keep_identity(&input, bytes)?, pack))
}

/// Convert an explicitly selected outfit without renumbering coordinates or plugin references.
/// Studio's saved coordinate is authoritative; the single-character display heuristic is not.
pub fn remake_ok_card_for_outfit(lib: &Path, game: &Path, card: &Path, outfit: i32) -> Result<(String, usize, Vec<u8>, usize), String> {
    let bytes = std::fs::read(card).map_err(|e| e.to_string())?;
    let info = crate::card::parse_card_bytes_mode(&bytes, outfit, false)?;
    if outfit < 0 || outfit as usize >= info.coordinate_count {
        return Err(format!("outfit {outfit} outside {} saved coordinates", info.coordinate_count));
    }
    let source = card_as_made_with_policy(card, false)?;
    let (digest, parts, made, pack) = make_ok_card_for_outfit(lib, game, source.as_ref().map_or(card, |t| &t.0), outfit)?;
    Ok((digest, parts, keep_identity(&bytes, made)?, pack))
}

/// A temporary card next to the original, so zipmods kept in the card's folder are still the card's own; removed on drop.
struct TempCard(PathBuf);
impl Drop for TempCard { fn drop(&mut self) { let _ = std::fs::remove_file(&self.0); } }

/// The card an OK card is made from: OK chunks stripped, and the outfit shown moved to outfit 0
/// (okclothes::shown_outfit_first). None when that is the card itself.
fn card_as_made(card: &Path) -> Result<Option<TempCard>, String> {
    card_as_made_with_policy(card, true)
}
fn card_as_made_with_policy(card: &Path, choose_display_outfit: bool) -> Result<Option<TempCard>, String> {
    let bytes = std::fs::read(card).map_err(|e| format!("{}: {e}", card.display()))?;
    let plain = strip_ok_chunks(&bytes)?;
    let shown = if choose_display_outfit {
        crate::okclothes::shown_outfit_first(&plain).map_err(|e| format!("{}: shown outfit: {e}", card.display()))?
    } else { None };
    if shown.is_none() && plain.len() == bytes.len() { return Ok(None); }
    if let Some((_, k)) = &shown { eprintln!("OKCARD {} shows outfit {k} as outfit 0", card.display()); }
    let temp = TempCard(card.with_file_name(format!(".okremake-{}-{}.png", std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0))));
    std::fs::write(&temp.0, shown.map_or(plain, |s| s.0)).map_err(|e| format!("{}: {e}", temp.0.display()))?;
    Ok(Some(temp))
}

/// Game-mode digest of an original card as its OK card is made from it (card_as_made): what the OK card must give.
fn live_digest(game: &Path, card: &Path) -> Result<(String, usize, Vec<u8>), String> {
    let source = card_as_made(card)?;
    session_digest(game, source.as_ref().map_or(card, |t| &t.0))
}

/// A project card opened the way the game opens it (an OK card from the project's base library, a legacy okPv card
/// from its own snapshot, an original KKS card from the installed game). Tests and offline tools (animation sampling).
pub(crate) fn open_test_card(card: &Path, outfit: i32) -> Result<crate::session::OkSession, String> {
    if std::env::var_os("OK_BASE_LIBRARY").is_none() {
        std::env::set_var("OK_BASE_LIBRARY", Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1"));
    }
    if crate::portable::png_snapshot(&std::fs::read(card).map_err(|e| e.to_string())?)?.1.is_some() { return crate::portable::load_outfit(card, outfit); }
    let mut session = crate::session::open(&root_for_card(Path::new(""), Some(card)), Some(card), outfit)?;
    // The game shapes a session after opening it (portable::load does so itself).
    if session.body_shaped.is_none() { let body = session.shape_body.clone(); let face = session.shape_face.clone(); session.apply_shape(&body, &face); }
    Ok(session)
}

/// The library model ids an OK card names in its `okLb` payload (None: not an OK card, or no model list).
pub fn card_models(bytes: &[u8]) -> Option<Vec<String>> {
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return None; }
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().ok()?) as usize;
        let kind = &bytes[pos + 4..pos + 8];
        if kind == CARD_CHUNK {
            let payload: serde_json::Value = serde_json::from_slice(bytes.get(pos + 8..pos + 8 + len)?).ok()?;
            let mut ids: Vec<String> = payload["models"].as_array()?.iter().filter_map(|v| v.as_str().map(str::to_owned)).collect();
            ids.sort();
            ids.dedup();
            return Some(ids);
        }
        if kind == b"IEND" { return None; }
        pos += 12 + len;
    }
    None
}

/// The session digest an OK card recorded when it was made (its `okLb` payload).
pub fn library_card_digest(card: &Path) -> Option<String> {
    let bytes = std::fs::read(card).ok()?;
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return None; }
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().unwrap()) as usize;
        let kind = &bytes[pos + 4..pos + 8];
        if kind == CARD_CHUNK {
            let payload: serde_json::Value = serde_json::from_slice(bytes.get(pos + 8..pos + 8 + len)?).ok()?;
            return payload["digest"].as_str().map(str::to_owned);
        }
        if kind == b"IEND" { return None; }
        pos += 12 + len;
    }
    None
}

static ORIGINAL_OPEN: Mutex<()> = Mutex::new(());
/// How the last card opened through the library got its resources (diagnostics, UE log).
pub static LAST_OPEN: Mutex<String> = Mutex::new(String::new());
/// The last library session came from the open-card cache.
pub static LAST_FROM_CACHE: AtomicBool = AtomicBool::new(false);

/// The configured base library (`OK_BASE_LIBRARY`, a directory with `oklib.json`).
pub fn configured_library() -> Option<PathBuf> {
    std::env::var_os("OK_BASE_LIBRARY").map(PathBuf::from).filter(|l| l.join("oklib.json").is_file())
}

/// An original KKS card (no `okLb`, no portable pack) opened from the base library plus its own zipmods:
/// the card's directory (loose `.zipmod`s) and the game's `mods/` (when `game` is a directory). Vanilla
/// data comes only from the library. None when no library is configured or the card is not original.
pub fn open_original_card(game: &Path, card: &Path, outfit: i32) -> Option<Result<crate::session::OkSession, String>> {
    let lib = configured_library()?;
    let bytes = std::fs::read(card).ok()?;
    if is_library_card(card) || crate::portable::png_snapshot(&bytes).ok()?.1.is_some() { return None; }
    // Callers showing independent cards pass no game directory (or a configured one that is not installed):
    // the installation named by OK_GAME_ROOT still provides the card's zipmods.
    let configured = std::env::var_os("OK_GAME_ROOT").map(PathBuf::from).filter(|g| g.is_dir());
    let game = if game.join("mods").is_dir() { game } else { configured.as_deref().unwrap_or(game) };
    Some(open_with_zipmods(&lib, game, card, outfit))
}

/// `open_original_card` for an explicit library (tools).
pub fn open_with_zipmods(lib: &Path, game: &Path, card: &Path, outfit: i32) -> Result<crate::session::OkSession, String> {
    with_zipmods(lib, game, card, || {
        let (zips, card_dirs) = crate::gamefs::fallback_summary();
        let session = crate::session::open(lib, Some(card), outfit)?;
        LAST_OPEN.lock().unwrap().clone_from(&format!("original card from the OK base library + its zipmods \
            (named zipmods {zips}, card directory {card_dirs}, {})", if LAST_FROM_CACHE.load(Ordering::Relaxed) { "cached session".to_owned() }
            else { format!("zipmod data used {}", crate::gamefs::fallback_used()) }));
        Ok(session)
    })
}

/// Runs `f` with the library active and the card's zipmods as fallback (card directory, the game's mods/).
/// Fallback directories are process-wide: one original card at a time.
pub fn with_zipmods<R>(lib: &Path, game: &Path, card: &Path, f: impl FnOnce() -> Result<R, String>) -> Result<R, String> {
    with_zipmods_and(lib, game, card, &[], f)
}

/// Where a card's zipmods come from: an installation (its `mods/` and its settings), or only a zipmod folder named
/// `mods` (no game: the shadow ramp and UncensorSelector settings are the shared resources'). (root whose `mods/` is
/// searched, whether the root's settings are read)
pub fn zipmod_source(game: &Path) -> (PathBuf, bool) {
    match (game.file_name(), game.parent()) {
        (Some(name), Some(parent)) if name.eq_ignore_ascii_case("mods") && game.is_dir() => (parent.to_owned(), false),
        _ => (game.to_owned(), true),
    }
}

/// `with_zipmods` plus game zipmods the card uses without naming them (`extra`).
fn with_zipmods_and<R>(lib: &Path, game: &Path, card: &Path, extra: &[PathBuf], f: impl FnOnce() -> Result<R, String>) -> Result<R, String> {
    let _one = ORIGINAL_OPEN.lock().unwrap_or_else(|e| e.into_inner());
    ensure_library(lib)?;
    let (game, settings) = zipmod_source(game);
    let game = game.as_path();
    let mut roots = Vec::new();
    if let Some(dir) = card.parent() { roots.push(crate::gamefs::Fallback { root: dir.to_owned(), loose_mods: true, files: Vec::new(), installation: false, settings: false }); }
    if !game.as_os_str().is_empty() && game.is_dir() && crate::gamefs::normalize(game) != crate::gamefs::normalize(lib) {
        let t = std::time::Instant::now();
        let mut files = card_zipmods(game, card);
        if std::env::var_os("OK_TIMING").is_some() { eprintln!("OKTIME card_zipmods {:.2}s ({} zips)", t.elapsed().as_secs_f64(), files.len()); }
        for z in extra { if !files.contains(z) { files.push(z.clone()); } }
        roots.push(crate::gamefs::Fallback { root: PathBuf::new(), loose_mods: false, files, installation: false, settings: false });
        // Last: the installation itself, as the game sees it (zipmods the card uses without naming them, its
        // UncensorSelector configuration), for what this checkout's library does not hold.
        roots.push(crate::gamefs::Fallback { root: game.to_owned(), loose_mods: false, files: Vec::new(), installation: true, settings });
    }
    crate::gamefs::set_fallback(roots);
    let result = f();
    crate::gamefs::set_fallback(Vec::new());
    result
}

/// The game's zipmods an original card names by GUID (sideloader resolve list, uncensor choices).
pub fn card_zipmods(game: &Path, card: &Path) -> Vec<PathBuf> {
    let Ok(info) = crate::card::parse_card(card) else { return Vec::new() };
    let mut wanted: std::collections::HashSet<String> = info.resolves.iter().map(|r| r.mod_id.clone()).filter(|g| !g.is_empty()).collect();
    wanted.extend([&info.body_guid, &info.penis_guid, &info.balls_guid].into_iter().flatten().filter(|g| !g.is_empty()).cloned());
    if wanted.is_empty() { return Vec::new(); }
    let mut zips: Vec<PathBuf> = crate::mods::find_guids(&game.join("mods"), Some(&wanted)).into_values().collect();
    zips.sort();
    zips.dedup();
    zips
}

/// Bytes of an original card's own zipmods (card directory archives and the game zipmods it names).
pub fn card_zipmod_bytes(game: &Path, card: &Path) -> u64 {
    let mut total = 0;
    if let Some(dir) = card.parent() {
        for e in std::fs::read_dir(dir).into_iter().flatten().flatten() {
            let n = e.file_name().to_string_lossy().to_ascii_lowercase();
            if n.ends_with(".zipmod") || n.ends_with(".zip") { total += e.metadata().map(|m| m.len()).unwrap_or(0); }
        }
    }
    if !game.as_os_str().is_empty() && game.is_dir() {
        total += card_zipmods(game, card).iter().filter_map(|p| std::fs::metadata(p).ok()).map(|m| m.len()).sum::<u64>();
    }
    total
}

/// Size rule of an OK card: ≤ 110% of the original card when the library covers it; with a pack of
/// card-specific resources, ≤ the original card plus the zipmods it needed.
pub fn check_ok_card_size(original: u64, card: u64, pack: usize, zipmods: u64) -> Result<String, String> {
    if pack == 0 {
        let ratio = card as f64 / original.max(1) as f64;
        if ratio > 1.10 { return Err(format!("OK card is {:.1}% of the original (limit 110%)", ratio * 100.)); }
        Ok(format!("{:.1}% of the original", ratio * 100.))
    } else {
        let ratio = card as f64 / (original + zipmods).max(1) as f64;
        if ratio > 1.0 { return Err(format!("OK card is {:.1}% of the original card plus its zipmods (limit 100%)", ratio * 100.)); }
        Ok(format!("{:.1}% of the original card plus its zipmods ({} bytes)", ratio * 100., zipmods))
    }
}

/// An original card as an OK card: the library plus the card's zipmods, everything the library lacks
/// recorded into the card's pack. (digest, parts, OK card bytes, pack bytes).
/// Bytes of the zipmods the last `make_ok_card` took in although the card does not name them.
pub static LAST_UNNAMED_ZIPMOD_BYTES: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
/// File names of the zipmods the last `make_ok_card` needed but could not find (its OK card lacks their parts).
pub static LAST_ABSENT_ZIPMODS: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());

pub fn make_ok_card(lib: &Path, game: &Path, card: &Path) -> Result<(String, usize, Vec<u8>, usize), String> {
    make_ok_card_for_outfit(lib, game, card, 0)
}
fn make_ok_card_for_outfit(lib: &Path, game: &Path, card: &Path, outfit: i32) -> Result<(String, usize, Vec<u8>, usize), String> {
    let original = std::fs::read(card).map_err(|e| e.to_string())?;
    if is_library_card(card) { return Err(format!("{} is already an OK card", card.display())); }
    let outfits = crate::tree_answers::outfit_count(&original) as i32;
    if outfit < 0 || outfit >= outfits { return Err(format!("{}: outfit {outfit} of {outfits}", card.display())); }
    LAST_ABSENT_ZIPMODS.lock().unwrap().clear();
    LAST_UNNAMED_ZIPMOD_BYTES.store(0, Ordering::Relaxed);
    let work = lib.join("work").join(format!("convert_{}", std::process::id()));
    let (profile, clock) = (std::env::var_os("OK_PROFILE").is_some(), std::time::Instant::now());
    let lap = |what: &str| if profile { eprintln!("OKPROFILE convert {what} at {:.2}s", clock.elapsed().as_secs_f64()); };
    // One pass (okstrict.rs, "Converting"): every saved outfit is opened once, the tree's answers recorded (okLt), the
    // third-party data the library holds copied and what only the zipmods hold recorded from them (okLc).
    let made = with_zipmods_and(lib, game, card, &[], || {
        let store = active().ok_or("library not active")?;
        store.forget_models();
        store.clear_missing();
        begin_card_pack(&work)?;
        crate::tree_answers::begin(lib);
        let strict = crate::okstrict::Strict::converting();
        // Every conversion is a view of its own (the card's zipmods join the library's tree): memos keyed by the view
        // (the tree's zip list and GUID index among them) must not answer the next card of the same worker with this
        // one's tree (小绯雪 converted after Denia lost its own zipmod's GUID, and with it its clothes).
        static CONVERSIONS: AtomicU64 = AtomicU64::new(0);
        let view = format!("converting-{}-{}", std::process::id(), CONVERSIONS.fetch_add(1, Ordering::Relaxed));
        let scope = PackScope { overlay: crate::gamefs::process_overlay().ok_or("no recording overlay")?,
            store: store.pack.read().unwrap().clone(), identity: view.clone(), mods_identity: view,
            fallback_used: AtomicBool::new(false), pool: std::sync::OnceLock::new(), tree: None, strict: Some(strict.clone()) };
        let mount = mount_scope(scope);
        let mut first = None;
        let mut failed = Vec::new();
        let read_only = std::env::var("OK_CONVERT_READ_ONLY").as_deref() == Ok("1");
        lap("setup");
        for o in 0..outfits {
            let t = std::time::Instant::now();
            // (only outfit 0's session is the card's digest; the others are opened for what they read)
            crate::okstrict::set_read_only(o > 0 && read_only);
            let opened = if o == 0 { session_digest_outfit(lib, card, o).map(|d| first = Some(d)) } else { crate::session::open(lib, Some(card), o).map(|_| ()) };
            crate::okstrict::set_read_only(false);
            if let Err(e) = opened { failed.push(format!("outfit {o}: {e}")); }
            // The other garment states the game and the card reader show (CLOTHES_STATE_VARIANTS): only read. What
            // reads the same in every state (hair, accessories) the outfit's own open above read (state_variant_open).
            for states in CLOTHES_STATE_VARIANTS {
                let _states = crate::unity::request_clothes_states(states);
                let _variant = crate::StateVariantScope::begin();
                crate::okstrict::set_read_only(read_only);
                let opened = crate::session::open(lib, Some(card), o);
                crate::okstrict::set_read_only(false);
                if let Err(e) = opened { failed.push(format!("outfit {o} clothes {states:?}: {e}")); }
            }
            if profile { eprintln!("OKPROFILE convert outfit {o} {:.2}s", t.elapsed().as_secs_f64()); }
        }
        // An OK Studio eyes pattern that shows a gag eye builds that head object (cf_O_gag_eye_00..02), which no
        // ordinary open does: its rig reads the card's own head (a mod head's skeleton). Once per gag object, after
        // the outfits (the digest open is done), only read; the eye sets' textures are the base library's.
        for set in gag_eye_sets(lib) {
            let _status = crate::studio_status::request(crate::studio_status::StatusOverride { eyes_ptn: Some(set), ..Default::default() });
            crate::okstrict::set_read_only(read_only);
            let opened = crate::session::open(lib, Some(card), 0);
            crate::okstrict::set_read_only(false);
            if let Err(e) = opened { failed.push(format!("eyes pattern {set}: {e}")); }
        }
        lap("gag eyes");
        drop(mount);
        let answers = crate::tree_answers::end();
        lap("opens");
        let pack = take_card_pack();
        lap("answers+take");
        let models: Vec<String> = store.touched().iter().map(id_hex).collect();
        Ok::<_, String>((first, failed, strict.gaps(), strict.take_additions(), answers, pack, models))
    });
    let _ = std::fs::remove_dir_all(&work);
    let (first, failed, gaps, adds, answers, pack, models) = made?;
    if !gaps.is_empty() { return Err(crate::okstrict::failure(card, &gaps)); }
    if !failed.is_empty() { return Err(failed.join(" | ")); }
    let (digest, parts, _) = first.ok_or("outfit 0 did not open")?;
    // The pack is put together once (recorded, then what the library supplied) and encoded once.
    let mut pack = pack?.unwrap_or_else(|| CardPack { version: CARD_PACK_VERSION, index: Default::default(), records: Vec::new() });
    merge_additions(&mut pack, adds);
    lap("merge");
    let empty = pack.records.is_empty() && pack.index.files.is_empty() && pack.index.dirs.is_empty() && pack.index.zips.is_empty();
    let pack = if empty { None } else { Some(encode_card_pack(&pack)?) };
    lap("encode");
    let payload = serde_json::to_vec(&serde_json::json!({"format":"oklib-card","version":1,"digest":digest,"models":models})).unwrap();
    let bytes = library_card_with_pack(&original, &payload, pack.as_deref())?;
    let bytes = crate::tree_answers::with_answers(&bytes, &answers?)?;
    lap("card");
    Ok((digest, parts, bytes, pack.map_or(0, |p| p.len())))
}

/// The OK card made to open from its own data and the shared resources only (okstrict.rs): its answers (`okLt`)
/// recorded, then every outfit opened in completing mode, what those opens read of third-party mods from the library
/// copied into its pack (`okLc`), then every outfit opened strictly from the result. Also a summary of what was added.
///
/// Each step runs in a process of its own (`seal-step`): a value an earlier open of the process kept (an entry memo,
/// a ramp, a parsed bundle) answers a later open without the reads that step must make, so answers went unrecorded
/// and library data uncopied, and a check in the same process passed a card a new process could not open.
pub fn seal_card(lib: &Path, card: &[u8]) -> Result<(Vec<u8>, String), String> {
    static N: AtomicU64 = AtomicU64::new(0);
    let dir = lib.join("work");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let n = N.fetch_add(1, Ordering::Relaxed);
    let step_file = |step: &str| TempCard(dir.join(format!("seal_{}_{n}_{step}.png", std::process::id())));
    let (input, answered, sealed) = (step_file("in"), step_file("answers"), step_file("sealed"));
    std::fs::write(&input.0, card).map_err(|e| format!("{}: {e}", input.0.display()))?;
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let run = |args: &[&std::ffi::OsStr]| -> Result<String, String> {
        let out = std::process::Command::new(&exe).arg("X:/NoGame").arg("--oklib").args(args).output().map_err(|e| e.to_string())?;
        let text = String::from_utf8_lossy(&out.stdout).into_owned();
        if out.status.success() { Ok(text) } else { Err(text.lines().filter(|l| l.starts_with("OK")).collect::<Vec<_>>().join(" ")) }
    };
    run(&["seal-step".as_ref(), lib.as_os_str(), "answers".as_ref(), input.0.as_os_str(), answered.0.as_os_str()])?;
    let mut summary = run(&["seal-step".as_ref(), lib.as_os_str(), "complete".as_ref(), answered.0.as_os_str(), sealed.0.as_os_str()])?;
    // A read made by an open's background work (entry prefetch) can land after its step took what it copied (seen
    // under load): the strict check names it, and completing the sealed card again copies it.
    let mut check = run(&["strict-check".as_ref(), lib.as_os_str(), sealed.0.as_os_str()]);
    for _ in 0..2 {
        if check.is_ok() { break; }
        std::fs::copy(&sealed.0, &answered.0).map_err(|e| e.to_string())?;
        summary.push_str(&run(&["seal-step".as_ref(), lib.as_os_str(), "complete".as_ref(), answered.0.as_os_str(), sealed.0.as_os_str()])?);
        check = run(&["strict-check".as_ref(), lib.as_os_str(), sealed.0.as_os_str()]);
    }
    check?;
    let summary = summary.lines().filter_map(|l| l.strip_prefix("OKSEALSTEP ")).collect::<Vec<_>>().join("; then ");
    Ok((std::fs::read(&sealed.0).map_err(|e| e.to_string())?, summary))
}

/// One step of `seal_card` in this process: "answers" (okLt recorded) or "complete" (library data copied in).
fn seal_step(lib: &Path, step: &str, card: &[u8]) -> Result<(Vec<u8>, String), String> {
    if step == "answers" { return Ok((with_tree_answers(lib, card)?, String::new())); }
    if step != "complete" { return Err(format!("seal step {step}?")); }
    let answered = card.to_vec();
    let strict = crate::okstrict::begin_sealing();
    let completing = open_outfits(lib, &answered, "sealing");
    crate::okstrict::end_sealing();
    let adds = strict.take_additions();
    completing?;
    let summary = format!("added files={} zips={} zip entries={} model records={} ({} bytes)", adds.index.files.len(), adds.index.zips.len(),
        adds.index.zips.values().map(|z| z.entries.len()).sum::<usize>(), adds.records.len(), adds.records.values().map(Vec::len).sum::<usize>());
    let sealed = if adds.index.files.is_empty() && adds.index.zips.is_empty() && adds.records.is_empty() { answered } else { extend_card_pack(&answered, adds)? };
    Ok((sealed, summary))
}

/// Opens every saved outfit of the card (bytes) from the library, nothing read from or written to the open cache, with
/// cold bundle caches (an earlier open's parsed bundles would answer reads the card must make itself).
/// Garment states an OK card's outfits must open in besides the worn one: every garment half off (1), lowered (2) and
/// off (3), and the card reader's preloads (top four garments half off, the rest on or off). Opens in another state
/// ask other questions (which prefab nodes a garment has, the skeleton of the mesh that stays drawn): a conversion
/// opens these too, a strict check as well (the card reader's clothing warm load lacked 51 things of C000).
/// One eyes pattern (cha_eyeset row) of the library for each gag eye object it shows (1..3), the lowest row of each.
fn gag_eye_sets(lib: &Path) -> Vec<i32> {
    let mut by_gag = std::collections::BTreeMap::new();
    for ptn in 0..256 {
        let Some(set) = crate::eye_set::eye_set(lib, ptn) else { break };
        if set.id == ptn && set.gag != 0 { by_gag.entry(set.gag).or_insert(ptn); }
    }
    by_gag.into_values().collect()
}

pub(crate) const CLOTHES_STATE_VARIANTS: [[u8; 9]; 5] = [[1; 9], [2; 9], [3; 9], [1, 1, 1, 1, 0, 0, 0, 0, 0], [1, 1, 1, 1, 3, 3, 3, 3, 3]];

fn open_outfits(lib: &Path, card: &[u8], what: &str) -> Result<(), String> {
    ensure_library(lib)?;
    static N: AtomicU64 = AtomicU64::new(0);
    let dir = lib.join("work");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let temp = TempCard(dir.join(format!("{what}_{}_{}.png", std::process::id(), N.fetch_add(1, Ordering::Relaxed))));
    std::fs::write(&temp.0, card).map_err(|e| format!("{}: {e}", temp.0.display()))?;
    let nocache = std::env::var_os("OK_NOCACHE");
    std::env::set_var("OK_NOCACHE", "1");
    crate::unity::release_bundle_caches();
    let mut errors = Vec::new();
    for outfit in 0..crate::tree_answers::outfit_count(card) as i32 {
        if let Err(e) = crate::session::open(lib, Some(&temp.0), outfit) { errors.push(format!("outfit {outfit}: {e}")); }
        for states in CLOTHES_STATE_VARIANTS {
            let _states = crate::unity::request_clothes_states(states);
            if let Err(e) = crate::session::open(lib, Some(&temp.0), outfit) { errors.push(format!("outfit {outfit} clothes {states:?}: {e}")); }
        }
    }
    if nocache.is_none() { std::env::remove_var("OK_NOCACHE"); }
    if errors.is_empty() { Ok(()) } else { Err(format!("{what} open failed: {}", errors.join(" | "))) }
}

/// The card with what a sealing open copied from the library added to its pack (a pack made when it has none).
fn extend_card_pack(card: &[u8], adds: crate::okstrict::Additions) -> Result<Vec<u8>, String> {
    let mut pack = match png_chunk(card, PACK_CHUNK) {
        Some(chunk) => {
            let raw = decode_card_pack_bytes(chunk)?;
            bincode::deserialize::<CardPack>(&raw).map_err(|e| format!("card pack: {e}"))?
        }
        None => CardPack { version: CARD_PACK_VERSION, index: Default::default(), records: Vec::new() },
    };
    if pack.version != CARD_PACK_VERSION { return Err(format!("card pack version {} (expected {CARD_PACK_VERSION})", pack.version)); }
    merge_additions(&mut pack, adds);
    crate::tree_answers::with_chunk(card, PACK_CHUNK, &encode_card_pack(&pack)?)
}

/// The OK card with `okLt`: the answers its opens take from the library's mods tree (tree_answers.rs), recorded by
/// opening it from the library and its own pack, every saved outfit, with nothing read from or written to the open
/// cache. An outfit that fails to open is left out (its questions are asked of the tree, as before).
pub fn with_tree_answers(lib: &Path, card: &[u8]) -> Result<Vec<u8>, String> {
    ensure_library(lib)?;
    static N: AtomicU64 = AtomicU64::new(0);
    let dir = lib.join("work");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let temp = TempCard(dir.join(format!("answers_{}_{}.png", std::process::id(), N.fetch_add(1, Ordering::Relaxed))));
    let plain = crate::cardbake::strip_answers(card);
    std::fs::write(&temp.0, &*plain).map_err(|e| format!("{}: {e}", temp.0.display()))?;
    let coordinates = crate::card::parse_card_bytes_mode(card, 0, false).map(|c| c.coordinate_count).unwrap_or(1).max(1);
    let nocache = std::env::var_os("OK_NOCACHE");
    std::env::set_var("OK_NOCACHE", "1");
    crate::tree_answers::begin(lib);
    for outfit in 0..coordinates as i32 {
        if let Err(e) = crate::session::open(lib, Some(&temp.0), outfit) {
            eprintln!("ok-extract: tree answers: outfit {outfit} did not open: {e}");
        }
    }
    let payload = crate::tree_answers::end();
    if nocache.is_none() { std::env::remove_var("OK_NOCACHE"); }
    crate::tree_answers::with_answers(card, &payload?)
}

/// Root to open a card with: callers pass an empty root for self-contained
/// cards; an OK library card resolves to the configured base library
/// (`OK_BASE_LIBRARY`, a directory with `oklib.json`).
pub fn root_for_card(game: &Path, card: Option<&Path>) -> PathBuf {
    // An OK card opens from OK data only, whether or not a game directory is given.
    match (card, configured_library()) {
        (Some(card), Some(lib)) if is_library_card(card) => lib,
        _ => game.to_owned(),
    }
}

/// OK card: the original card (PNG and its card data) with a small `okLb`
/// chunk naming the library models it uses. A heavy embedded `okPv` pack is
/// dropped: the library holds every shared resource.
pub fn library_card(original: &[u8], payload: &[u8]) -> Result<Vec<u8>, String> {
    let (iend, snapshot) = crate::portable::png_snapshot(original)?;
    let mut out = Vec::with_capacity(original.len() + payload.len() + 12);
    let mut head = original[..iend].to_vec();
    if let Some(span) = snapshot { head.drain(span.start - 8..span.end + 4); }
    // An earlier okLb is replaced.
    let mut pos = 8usize;
    while pos + 12 <= head.len() {
        let len = u32::from_be_bytes(head[pos..pos + 4].try_into().unwrap()) as usize;
        if &head[pos + 4..pos + 8] == CARD_CHUNK || &head[pos + 4..pos + 8] == PACK_CHUNK { head.drain(pos..pos + 12 + len); continue; }
        pos += 12 + len;
    }
    out.extend_from_slice(&head);
    out.extend_from_slice(&(payload.len() as u32).to_be_bytes());
    let mut chunk = CARD_CHUNK.to_vec();
    chunk.extend_from_slice(payload);
    out.extend_from_slice(&chunk);
    out.extend_from_slice(&crc32fast::hash(&chunk).to_be_bytes());
    out.extend_from_slice(&original[iend..]);
    Ok(out)
}

pub fn activate(dir: &Path, mode: Mode) -> Result<Arc<Store>, String> {
    let _ = unity_asset_binary::oklib_hook::HOOK.set(vendor_hook);
    let store = Arc::new(Store::open(dir, mode)?);
    *STORE.write().unwrap() = Some(store.clone());
    ACTIVE.store(true, Ordering::Relaxed);
    Ok(store)
}
pub fn deactivate() {
    if let Some(store) = STORE.write().unwrap().take() { store.flush(); }
    ACTIVE.store(false, Ordering::Relaxed);
}

/// A record exists for this query (library lookups with fallback keys).
pub fn has_record<A: serde::Serialize + ?Sized>(id: Option<&Id>, method: &str, args: &A) -> bool {
    let (Some(store), Some(id)) = (active(), id) else { return false };
    let arg_bytes = bincode::serialize(args).unwrap_or_default();
    store.get_raw(id, method, &arg_bytes).is_some()
}
/// `memo` whose stored form differs from the returned value: `pack` makes
/// the stored form (e.g. textures as references), `unpack` restores it when
/// read back. Conversion returns the computed value as is.
pub fn memo_packed<A, R, F>(id: Option<&Id>, method: &str, args: &A, f: F, pack: impl FnOnce(&R) -> R, unpack: impl FnOnce(&mut R)) -> R
where A: serde::Serialize + ?Sized, R: serde::Serialize + serde::de::DeserializeOwned, F: FnOnce() -> R {
    let (Some(store), Some(id)) = (active(), id) else { return f(); };
    let arg_bytes = bincode::serialize(args).unwrap_or_default();
    let stored = if store.mode == Mode::Record && refreshing(method) { None } else { store.get_raw_src(id, method, &arg_bytes) };
    if let Some((raw, from_pack)) = stored {
        if let Ok(mut value) = crate::unity::with_texture_plans(|| bincode::deserialize::<R>(&raw)) {
            if store.mode == Mode::Replay { store.hits.fetch_add(1, Ordering::Relaxed); }
            let _ = from_pack;
            if !CONVERTING.load(Ordering::Relaxed) { unpack(&mut value); }
            return value;
        }
    }
    if store.mode == Mode::Replay {
        if let Some(card_pack) = store.recording_pack() {
            let value = f();
            // Stored packed like the library; restoring it once now records the queries the packed form
            // refers to (textures), so the pack can restore it without the card's zipmods.
            let mut stored = pack(&value);
            if let Ok(raw) = crate::unity::with_texture_plans(|| bincode::serialize(&stored)) { card_pack.put_raw(id, method, &arg_bytes, &raw); }
            unpack(&mut stored);
            return value;
        }
        if crate::okstrict::completing_model(id) {
            let value = f();
            let mut stored = pack(&value);
            if let Ok(raw) = crate::unity::with_texture_plans(|| bincode::serialize(&stored)) { crate::okstrict::keep_record(id, method, &arg_bytes, raw); }
            unpack(&mut stored);
            return value;
        }
        store.note_missing(format!("{method} {} {}", hex(id), describe_args(&arg_bytes)));
        return f();
    }
    let value = timed(method, f);
    let stored = timed("#pack", || pack(&value));
    if let Ok(raw) = crate::unity::with_texture_plans(|| bincode::serialize(&stored)) { timed("#store", || store.put_raw(id, method, &arg_bytes, &raw)); }
    value
}

/// Run `f` through the library. `id` is the content id of the Unity source;
/// `args` the query arguments. Replay misses run `f` on the model handle (no
/// Unity data) and are listed in missing.txt.
pub fn memo<A, R, F>(id: Option<&Id>, method: &str, args: &A, f: F) -> R
where A: serde::Serialize + ?Sized, R: serde::Serialize + serde::de::DeserializeOwned, F: FnOnce() -> R {
    let (Some(store), Some(id)) = (active(), id) else { return f(); };
    let arg_bytes = bincode::serialize(args).unwrap_or_default();
    let stored = if store.mode == Mode::Record && refreshing(method) { None } else { store.get_raw(id, method, &arg_bytes) };
    if let Some(raw) = stored {
        if let Ok(value) = crate::unity::with_texture_plans(|| bincode::deserialize::<R>(&raw)) {
            if store.mode == Mode::Replay { store.hits.fetch_add(1, Ordering::Relaxed); }
            return value;
        }
    }
    if store.mode == Mode::Replay {
        if let Some(card_pack) = store.recording_pack() {
            let value = f();
            if let Ok(raw) = crate::unity::with_texture_plans(|| bincode::serialize(&value)) { card_pack.put_raw(id, method, &arg_bytes, &raw); }
            return value;
        }
        if crate::okstrict::completing_model(id) {
            let value = f();
            if let Ok(raw) = crate::unity::with_texture_plans(|| bincode::serialize(&value)) { crate::okstrict::keep_record(id, method, &arg_bytes, raw); }
            return value;
        }
        store.note_missing(format!("{method} {} {}", hex(id), describe_args(&arg_bytes)));
        return f();
    }
    let value = timed(method, f);
    if let Ok(raw) = timed("#serialize+store", || crate::unity::with_texture_plans(|| bincode::serialize(&value))) { timed("#store", || store.put_raw(id, method, &arg_bytes, &raw)); }
    value
}

/// `OKLIB_REFRESH=query[,query...]`: while recording, these queries are
/// computed again and a changed result replaces the stored one (a fixed
/// query re-converted with `convert <lib> <threads> @list`).
fn refreshing(query: &str) -> bool {
    static LIST: std::sync::OnceLock<Vec<String>> = std::sync::OnceLock::new();
    LIST.get_or_init(|| std::env::var("OKLIB_REFRESH").unwrap_or_default().split(',')
        .map(|q| q.trim().to_owned()).filter(|q| !q.is_empty()).collect()).iter().any(|q| q == query)
}

fn describe_args(bytes: &[u8]) -> String {
    // Arguments are usually strings: show the printable part for the gap report.
    let text: String = bytes.iter().map(|&b| if (0x20..0x7f).contains(&b) { b as char } else { '.' }).collect();
    let text = text.trim_matches('.').to_owned();
    if text.len() > 120 { text[..120].to_owned() } else { text }
}

/// A replayed `memo_bytes` result read in place by `visit` (a large stored result is decompressed into a mapped
/// temporary file, not memory). None when not replaying, when a card pack is being recorded, or when the result is
/// not stored or `visit` fails: the caller then uses `memo_bytes`.
pub(crate) fn memo_bytes_visit<A: serde::Serialize + ?Sized, T>(bytes: &[u8], method: &str, args: &A, visit: impl FnOnce(&[u8]) -> Option<T>) -> Option<T> {
    if !ACTIVE.load(Ordering::Relaxed) { return None; }
    let store = active()?;
    if store.mode != Mode::Replay || store.recording_pack().is_some() { return None; }
    let id = content_id(bytes);
    let arg_bytes = bincode::serialize(args).ok()?;
    // (an OK card's own model: its pack only; one it lacks goes through memo_bytes, okstrict.rs)
    let own = crate::okstrict::active().is_some() && !crate::okstrict::library_model(&id);
    let library = if own { None } else { store.get_own_source(&id, method, &arg_bytes) };
    let raw = library.or_else(|| store.pack().and_then(|p| p.get_own_source(&id, method, &arg_bytes)))?;
    let value = visit(&raw)?;
    store.hits.fetch_add(1, Ordering::Relaxed);
    Some(value)
}

/// Memo for raw bundle/serialized bytes consumers (free functions).
pub fn memo_bytes<A, R, F>(bytes: &[u8], method: &str, args: &A, f: F) -> R
where A: serde::Serialize + ?Sized, R: serde::Serialize + serde::de::DeserializeOwned, F: FnOnce() -> R {
    if !ACTIVE.load(Ordering::Relaxed) { return f(); }
    let id = content_id(bytes);
    memo(Some(&id), method, args, f)
}

/// A session needed the contents of model `id`, which this checkout does not have.
pub fn note_absent_model(id: &Id, what: &str) {
    if let Some(store) = active() { store.note_missing(format!("absent-model {} {what}", hex(id))); }
}

/// A token reached a raw Unity loader that is not behind `memo`: a gap in the
/// recording surface. Reported once per call site label.
pub fn note_unmemoized(label: &str) {
    if let Some(store) = active() {
        let bt = std::backtrace::Backtrace::force_capture().to_string();
        let frames: Vec<&str> = bt.lines().filter(|l| l.contains("ok_extract::") && !l.contains("oklib::") && !l.contains("library_internals")).take(5).collect();
        store.note_missing(format!("UNMEMOIZED {label} {}", frames.join(" <- ").split_whitespace().collect::<Vec<_>>().join(" ")));
    }
}

pub fn hash16(bytes: &[u8]) -> Id {
    use sha2::Digest;
    let digest = sha2::Sha256::digest(bytes);
    let mut id = [0u8; 16];
    id.copy_from_slice(&digest[..16]);
    id
}

/// (private bytes, peak commit) of this process in MB; zeros where unknown.
pub(crate) fn process_memory_mb() -> (u64, u64) {
    #[cfg(windows)]
    {
        #[repr(C)]
        #[derive(Default)]
        struct Counters { cb: u32, faults: u32, peak_ws: usize, ws: usize, qpp: usize, qp: usize, qpnp: usize, qnp: usize, pagefile: usize, peak_pagefile: usize, private: usize }
        extern "system" {
            fn GetCurrentProcess() -> isize;
            fn K32GetProcessMemoryInfo(process: isize, counters: *mut Counters, size: u32) -> i32;
        }
        let mut c = Counters { cb: std::mem::size_of::<Counters>() as u32, ..Default::default() };
        if unsafe { K32GetProcessMemoryInfo(GetCurrentProcess(), &mut c, c.cb) } != 0 {
            return ((c.private >> 20) as u64, (c.peak_pagefile >> 20) as u64);
        }
    }
    (0, 0)
}

/// Highest private memory (MB) since the last `card_memory_begin`, sampled every 50 ms by a background thread.
static CARD_PEAK_MB: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
/// Starts measuring one card open: private memory now (MB); `card_memory_end` gives the open's peak.
pub fn card_memory_begin() -> u64 {
    static SAMPLER: std::sync::OnceLock<()> = std::sync::OnceLock::new();
    SAMPLER.get_or_init(|| {
        std::thread::spawn(|| loop {
            CARD_PEAK_MB.fetch_max(process_memory_mb().0, Ordering::Relaxed);
            std::thread::sleep(std::time::Duration::from_millis(50));
        });
    });
    let now = process_memory_mb().0;
    CARD_PEAK_MB.store(now, Ordering::Relaxed);
    now
}
/// (private memory now, peak since `card_memory_begin`) in MB.
pub fn card_memory_end() -> (u64, u64) {
    let now = process_memory_mb().0;
    (now, CARD_PEAK_MB.fetch_max(now, Ordering::Relaxed).max(now))
}
/// One line per card open: where it started, its peak and what it added at its peak, and what it left.
pub fn card_memory_line(phase: &str, card: &Path, start: u64) -> String {
    let (now, peak) = card_memory_end();
    format!("OKCHECK memory\t{phase}\t{}\tstart={start}MB\tcard_peak={peak}MB\tadded={}MB\tafter={now}MB", card.display(), peak.saturating_sub(start))
}

fn session_digest(game: &Path, card: &Path) -> Result<(String, usize, Vec<u8>), String> {
    session_digest_outfit(game, card, 0)
}
fn session_digest_outfit(game: &Path, card: &Path, outfit: i32) -> Result<(String, usize, Vec<u8>), String> {
    let session = crate::session::open(game, Some(card), outfit)?;
    let bytes = session.to_preview_bytes()?;
    Ok((hex(&hash16(&bytes)), session.parts.len(), bytes))
}

fn summarize(v: &serde_json::Value) -> String {
    let s = v.to_string();
    if s.len() > 100 { format!("{}...({} chars)", &s[..100], s.len()) } else { s }
}
pub(crate) fn diff_json_pub(a: &serde_json::Value, b: &serde_json::Value, path: String, out: &mut Vec<String>) { diff_json(a, b, path, out) }
fn diff_json(a: &serde_json::Value, b: &serde_json::Value, path: String, out: &mut Vec<String>) {
    use serde_json::Value;
    if a == b || out.len() >= 400 { return; }
    match (a, b) {
        (Value::Object(x), Value::Object(y)) => {
            for (k, va) in x {
                match y.get(k) { Some(vb) => diff_json(va, vb, format!("{path}.{k}"), out), None => out.push(format!("{path}.{k}: only in A")) }
            }
            for k in y.keys() { if !x.contains_key(k) { out.push(format!("{path}.{k}: only in B")); } }
        }
        (Value::Array(x), Value::Array(y)) => {
            let scalar = x.iter().chain(y.iter()).all(|v| v.is_number() || v.is_null());
            if scalar || x.len() != y.len() {
                let first = x.iter().zip(y.iter()).position(|(p, q)| p != q);
                out.push(format!("{path}: len {} vs {} first difference at {:?}", x.len(), y.len(), first));
                if !scalar { for (i, (p, q)) in x.iter().zip(y.iter()).enumerate().take(4) { diff_json(p, q, format!("{path}[{i}]"), out); } }
            } else {
                for (i, (p, q)) in x.iter().zip(y.iter()).enumerate() { diff_json(p, q, format!("{path}[{i}]"), out); }
            }
        }
        _ => out.push(format!("{path}: {} vs {}", summarize(a), summarize(b))),
    }
}

fn parity_file(lib: &Path) -> PathBuf { lib.join("parity.tsv") }
fn read_parity(lib: &Path) -> HashMap<String, String> {
    std::fs::read_to_string(parity_file(lib)).unwrap_or_default().lines()
        .filter_map(|l| l.split_once('\t')).map(|(a, b)| (a.to_owned(), b.to_owned())).collect()
}
fn card_key(card: &Path) -> String { card.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default() }

/// `ok-extract <game> --oklib <command> ...`
///   record <lib> <card>...  open cards on the installation, record queries
///   replay <lib> <card>...  open cards on <lib>/root only, compare with record
///   live <card>...          digest of a normal open (no library)
pub fn cli(game: &Path, args: &[String]) -> Result<(), String> {
    let command = args.first().map(String::as_str).unwrap_or("");
    let lib = args.get(1).map(PathBuf::from);
    match command {
        "record" | "replay" => {
            let lib = lib.ok_or("library directory required")?;
            let mode = if command == "record" { Mode::Record } else { Mode::Replay };
            // Replay: the library directory is the game root; nothing else is read.
            let root = if mode == Mode::Record { game.to_owned() } else { lib.clone() };
            let store = activate(&lib, mode)?;
            crate::gamefs::activate(if mode == Mode::Record { crate::gamefs::Mode::Record } else { crate::gamefs::Mode::Replay }, &root, &lib)?;
            let expected = read_parity(&lib);
            let mut failures = 0;
            for card in &args[2..] {
                let card = Path::new(card);
                let started = std::time::Instant::now();
                let (hits0, miss0, stored0) = (store.hits.load(Ordering::Relaxed), store.misses.load(Ordering::Relaxed), store.stored.load(Ordering::Relaxed));
                match session_digest(&root, card) {
                    Ok((digest, parts, bytes)) => {
                        let verdict = match (mode, expected.get(&card_key(card))) {
                            (Mode::Replay, Some(want)) if *want == digest => "MATCH",
                            (Mode::Replay, Some(_)) => { failures += 1; "DIFFERENT" }
                            (Mode::Replay, None) => "UNRECORDED",
                            (Mode::Record, _) => {
                                let mut all = read_parity(&lib);
                                all.insert(card_key(card), digest.clone());
                                let mut lines: Vec<_> = all.iter().map(|(k, v)| format!("{k}\t{v}")).collect();
                                lines.sort();
                                let _ = std::fs::write(parity_file(&lib), lines.join("\n") + "\n");
                                "RECORDED"
                            }
                        };
                        if verdict == "DIFFERENT" || std::env::var_os("OKLIB_KEEP").is_some() {
                            let _ = std::fs::write(lib.join(format!("{}.{command}.{digest}.okpreview", card_key(card))), &bytes);
                            let _ = std::fs::write(lib.join(format!("{}.{command}.okpreview", card_key(card))), &bytes);
                        }
                        println!("OKLIB {verdict} {} parts={parts} digest={digest} {:.1}s hits={} misses={} stored={}", card_key(card),
                            started.elapsed().as_secs_f64(), store.hits.load(Ordering::Relaxed) - hits0,
                            store.misses.load(Ordering::Relaxed) - miss0, store.stored.load(Ordering::Relaxed) - stored0);
                    }
                    Err(e) => { failures += 1; println!("OKLIB ERROR {} {e}", card_key(card)); }
                }
                store.flush();
                crate::gamefs::save(&lib)?;
            }
            let (files, dirs, zips) = crate::gamefs::stats();
            println!("OKLIB sources files={files} dirs={dirs} zips={zips}");
            crate::gamefs::deactivate();
            println!("OKLIB records={} conflicts={} missing_kinds={}", store.len(), store.conflicts.load(Ordering::Relaxed), store.missing_list().len());
            deactivate();
            if failures > 0 { return Err(format!("{failures} card(s) failed")); }
        }
        "probe" => {
            // probe <lib> record|replay: catalogs as the pipeline sees them.
            let lib = lib.ok_or("library directory required")?;
            let replay = args.get(2).map(String::as_str) == Some("replay");
            let root = if replay { lib.clone() } else { game.to_owned() };
            activate(&lib, if replay { Mode::Replay } else { Mode::Record })?;
            crate::gamefs::activate(if replay { crate::gamefs::Mode::Replay } else { crate::gamefs::Mode::Record }, &root, &lib)?;
            let vanilla = crate::list::load_characustom_dir(&root.join("abdata/list/characustom"));
            let mods = crate::list::load_mods_catalog(&root.join("mods"));
            let guids = crate::mods::find_guids(&root.join("mods"), None);
            let mut cats: BTreeMap<i32, usize> = BTreeMap::new();
            for (c, _) in vanilla.by_id.keys() { *cats.entry(*c).or_default() += 1; }
            println!("OKLIB PROBE vanilla={} mods={} guids={} categories={:?}", vanilla.by_id.len(), mods.by_id.len(), guids.len(), cats);
            if let (Some(c), Some(i)) = (args.get(3).and_then(|v| v.parse::<i32>().ok()), args.get(4).and_then(|v| v.parse::<i32>().ok())) {
                let e = mods.get(c, i);
                println!("OKLIB PROBE entry {c}/{i}: {:?} zip={:?}", e.map(|e| &e.name), e.and_then(|e| e.extra.get("__zip")));
            }
            deactivate();
            crate::gamefs::deactivate();
        }
        "convert" => {
            // convert <lib> [threads] [filter]
            let lib = lib.ok_or("library directory required")?;
            let threads = args.get(2).and_then(|v| v.parse().ok()).unwrap_or(8);
            crate::oklib_convert::convert(game, &lib, threads, args.get(3).map(String::as_str))?;
        }
        "card" => {
            // card <lib> <card> <out>: OK card = original card + okLb (models used).
            let lib = lib.ok_or("library directory required")?;
            let card = PathBuf::from(args.get(2).ok_or("card required")?);
            let out = PathBuf::from(args.get(3).ok_or("output required")?);
            crate::clothes_gpu::keep_results();
            let original = std::fs::read(&card).map_err(|e| e.to_string())?;
            ensure_library(&lib)?;
            let store = active().ok_or("library not active")?;
            store.forget_models();
            let (hits0, miss0) = (store.hits.load(Ordering::Relaxed), store.misses.load(Ordering::Relaxed));
            let started = std::time::Instant::now();
            let (digest, parts, bytes, pack_len) = remake_ok_card(&lib, game, &card)?;
            let models: Vec<String> = store.touched().iter().map(id_hex).collect();
            println!("OKCARD pack={pack_len} bytes");
            std::fs::write(&out, &bytes).map_err(|e| e.to_string())?;
            let ratio = bytes.len() as f64 / original.len() as f64;
            println!("OKCARD {} parts={parts} digest={digest} models={} {:.1}s hits={} misses={} original={} card={} ratio={ratio:.4}",
                card.display(), models.len(), started.elapsed().as_secs_f64(), store.hits.load(Ordering::Relaxed) - hits0,
                store.misses.load(Ordering::Relaxed) - miss0, original.len(), bytes.len());
            store.flush();
            let size_rule = check_ok_card_size(original.len() as u64, bytes.len() as u64, pack_len, card_zipmod_bytes(game, &card) + LAST_UNNAMED_ZIPMOD_BYTES.load(Ordering::Relaxed));
            println!("OKCARD size {}", size_rule.clone().unwrap_or_else(|e| e));
            let absent = LAST_ABSENT_ZIPMODS.lock().unwrap().clone();
            if !absent.is_empty() { println!("OKCARD absent zipmods {}", absent.join(" | ")); }
            size_rule?;
        }
        "cards" => {
            // cards <lib> <card list> <out dir>: `card` for every listed original card (convert in one pass, okstrict.rs).
            // Worker processes (OK_CARDS_WORKERS, default half the hardware threads, at most 16) each set up the
            // library once and convert cards one after another; every card starts from released caches. A worker
            // takes its cards one at a time on stdin (card list "-").
            let lib = lib.ok_or("library directory required")?;
            let list_arg = args.get(2).ok_or("card list required")?;
            let list = if list_arg == "-" { String::new() } else { std::fs::read_to_string(list_arg).map_err(|e| e.to_string())? };
            let out_dir = PathBuf::from(args.get(3).ok_or("output directory required")?);
            std::fs::create_dir_all(&out_dir).map_err(|e| e.to_string())?;
            ensure_library(&lib)?;
            let started = std::time::Instant::now();
            let (mut done, mut failed) = (0, 0);
            if std::env::var_os("OK_CARDS_IN_PROCESS").is_none() {
                let cards: Vec<String> = list.lines().map(str::trim).filter(|l| !l.is_empty()).map(str::to_owned).collect();
                let workers = std::env::var("OK_CARDS_WORKERS").ok().and_then(|v| v.parse().ok())
                    .unwrap_or_else(|| (std::thread::available_parallelism().map_or(8, |n| n.get()) / 2).clamp(1, 16)).clamp(1, cards.len().max(1));
                let exe = std::env::current_exe().map_err(|e| e.to_string())?;
                // The workers share the machine: each one's pools get its part of the hardware threads.
                let threads = (crate::cpu_threads() / workers).max(2);
                // Cards are handed out one at a time, each worker asking for the next when it is done: their costs differ
                // several times over, and shares fixed in advance left workers idle while one went through heavy cards.
                let queue = std::sync::Arc::new(Mutex::new(cards.iter().cloned().collect::<std::collections::VecDeque<String>>()));
                let runs: Vec<_> = (0..workers).map(|_| {
                    let (exe, game, lib, out_dir, queue) = (exe.clone(), game.to_owned(), lib.clone(), out_dir.clone(), queue.clone());
                    std::thread::spawn(move || -> Result<Vec<String>, String> {
                        use std::io::{BufRead, Write};
                        let mut child = std::process::Command::new(&exe).arg(&game).arg("--oklib").arg("cards").arg(&lib).arg("-").arg(&out_dir)
                            .env("OK_CARDS_IN_PROCESS", "1").env("OK_THREADS", threads.to_string()).env("RAYON_NUM_THREADS", threads.to_string())
                            .stdin(std::process::Stdio::piped()).stdout(std::process::Stdio::piped()).stderr(std::process::Stdio::inherit())
                            .spawn().map_err(|e| e.to_string())?;
                        let mut input = child.stdin.take().ok_or("worker stdin")?;
                        let mut output = std::io::BufReader::new(child.stdout.take().ok_or("worker stdout")?).lines();
                        let mut results = Vec::new();
                        loop {
                            let Some(card) = queue.lock().unwrap().pop_front() else { break };
                            let answer = writeln!(input, "{card}").and_then(|_| input.flush()).ok().and_then(|_| loop {
                                match output.next() {
                                    Some(Ok(l)) if l.starts_with("OKCARDS ok") || l.starts_with("OKCARDS error") => break Some(l),
                                    Some(Ok(_)) => continue,
                                    _ => break None,
                                }
                            });
                            match answer {
                                Some(line) => { println!("{line}"); results.push(line); }
                                None => { results.push(format!("OKCARDS error\t{card}\tthe worker ended ({:?})\t0.0s", child.try_wait())); break; }
                            }
                        }
                        drop(input);
                        let _ = child.wait();
                        Ok(results)
                    })
                }).collect();
                let mut seconds = 0.0;
                let mut lines = Vec::new();
                for run in runs { lines.extend(run.join().map_err(|_| "worker thread panicked")??); }
                // Cards no worker was left to take (all of them ended).
                lines.extend(queue.lock().unwrap().drain(..).map(|card| format!("OKCARDS error\t{card}\tno worker left\t0.0s")));
                for line in &lines {
                    if line.starts_with("OKCARDS ok") { done += 1 } else { failed += 1; if line.ends_with("\t0.0s") { println!("{line}"); } }
                    seconds += line.split('\t').find_map(|f| f.strip_suffix('s').and_then(|v| v.parse::<f64>().ok())).unwrap_or(0.0);
                }
                println!("OKCARDS finished {done} made, {failed} failed in {:.1}s with {workers} workers ({seconds:.1}s converting in all, {:.2}s a card)",
                    started.elapsed().as_secs_f64(), seconds / (done + failed).max(1) as f64);
                return Ok(());
            }
            // The process's one-time scans of the library's mods tree (GUIDs, MaterialEditor manifests, catalogues), all
            // at once before the first card: they used to land on whichever card first asked (2–3 s of that card).
            crate::prewarm(&lib)?;
            crate::clothes_gpu::keep_results();
            let given: Box<dyn Iterator<Item = String>> = if list_arg == "-" { Box::new(std::io::stdin().lines().map_while(Result::ok)) }
                else { Box::new(list.lines().map(str::to_owned).collect::<Vec<_>>().into_iter()) };
            for card in given.map(|l| l.trim().to_owned()).filter(|l| !l.is_empty()).map(PathBuf::from) {
                // Nothing an earlier card's open kept in this process may stand in for this card's reads: every one of
                // them must reach the recorder.
                crate::unity::release_bundle_caches();
                crate::clear_entry_memo();
                let t0 = std::time::Instant::now();
                crate::clothes_gpu::GPU_NANOS.store(0, Ordering::Relaxed);
                crate::clothes_gpu::GPU_CALLS.store(0, Ordering::Relaxed);
                let made = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| remake_ok_card(&lib, game, &card))).unwrap_or_else(|_| Err("panic".into()));
                match made {
                    Ok((digest, parts, bytes, pack_len)) => {
                        let out = out_dir.join(card.file_name().unwrap_or_default());
                        std::fs::write(&out, &bytes).map_err(|e| e.to_string())?;
                        done += 1;
                        let absent = LAST_ABSENT_ZIPMODS.lock().unwrap().join(" | ");
                        println!("OKCARDS ok\t{}\t{digest}\tparts={parts}\tpack={pack_len}\t{:.1}s\tabsent={absent}", card.display(), t0.elapsed().as_secs_f64());
                        if std::env::var_os("OK_PROFILE").is_some() { eprintln!("OKPROFILE gpu {:.2}s in {} calls", crate::clothes_gpu::GPU_NANOS.load(Ordering::Relaxed) as f64 / 1e9, crate::clothes_gpu::GPU_CALLS.load(Ordering::Relaxed)); }
                    }
                    Err(e) => { failed += 1; println!("OKCARDS error\t{}\t{}\t{:.1}s", card.display(), e.replace(['\t', '\n'], " "), t0.elapsed().as_secs_f64()); }
                }
                let _ = std::io::Write::flush(&mut std::io::stdout());
            }
            if let Some(store) = active() { store.flush(); }
            println!("OKCARDS finished {done} made, {failed} failed in {:.0}s", started.elapsed().as_secs_f64());
        }
        "okcard-check" => {
            // okcard-check <lib> <OK card dir> <original card list> [live]: acceptance of `cards` output in one process.
            //   closed: each OK card reopened from the library and its own pack only must give the digest it recorded;
            //   live (optional): the original card in game mode must give it too. One line per card:
            //   OKCHECK <verdict>\t<original>\t<recorded>\t<closed>\t<live>\tgaps=N\tgame_reads=N (accesses to the installation)
            let lib = lib.ok_or("library directory required")?;
            let dir = PathBuf::from(args.get(2).ok_or("OK card directory required")?);
            let list = std::fs::read_to_string(args.get(3).ok_or("card list required")?).map_err(|e| e.to_string())?;
            let cards: Vec<PathBuf> = list.lines().map(str::trim).filter(|l| !l.is_empty()).map(PathBuf::from).collect();
            let want_live = args[4..].iter().any(|a| a == "live");
            // warm: store each verified OK card in the game's open-card cache.
            let warm = args[4..].iter().any(|a| a == "warm");
            let started = std::time::Instant::now();
            let mut live: Vec<String> = vec!["-".into(); cards.len()];
            if want_live {
                deactivate();
                crate::gamefs::deactivate();
                for (i, card) in cards.iter().enumerate() {
                    let start = card_memory_begin();
                    live[i] = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| live_digest(game, card))).ok().and_then(Result::ok).map(|d| d.0).unwrap_or_else(|| "error".into());
                    crate::unity::release_bundle_caches();
                    eprintln!("{}", card_memory_line("live", card, start));
                }
            }
            ensure_library(&lib)?;
            crate::gamefs::set_fallback(Vec::new());
            // The original installation (the game argument, else OK_GAME_ROOT) must not be touched by a closed open.
            let installation = Some(game.to_owned()).filter(|g| g.is_dir()).or_else(|| std::env::var_os("OK_GAME_ROOT").map(PathBuf::from));
            let (mut pass, mut fail) = (0, 0);
            for (i, card) in cards.iter().enumerate() {
                let ok_card = dir.join(card.file_name().unwrap_or_default());
                let recorded = library_card_digest(&ok_card).unwrap_or_else(|| "-".into());
                if let Some(s) = active() { s.clear_missing(); }
                crate::gamefs::reset_fallback_used();
                crate::gamefs::watch_root(installation.as_deref());
                let start = card_memory_begin();
                let opened = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| session_digest(&lib, &ok_card))).ok().and_then(Result::ok);
                let closed = opened.as_ref().map(|d| d.0.clone()).unwrap_or_else(|| "error".into());
                let game_reads = crate::gamefs::watched_accesses() + usize::from(crate::gamefs::fallback_used());
                crate::gamefs::watch_root(None);
                let gap_list = active().map(|s| s.missing_list()).unwrap_or_default();
                let gaps = gap_list.len();
                if std::env::var_os("OKCHECK_GAPS").is_some() { for (what, n) in &gap_list { println!("OKCHECK gap\t{n}\t{what}"); } }
                let ok = recorded != "-" && closed == recorded && gaps == 0 && game_reads == 0 && (!want_live || live[i] == recorded);
                if ok { pass += 1; } else { fail += 1; }
                if ok && warm { if let (Some(key), Some(d)) = (session_cache_key(&lib, &ok_card, 0), opened.as_ref()) { session_cache_warm(&key, &d.2, ""); } }
                drop(opened);
                crate::unity::release_bundle_caches();
                println!("OKCHECK {}\t{}\t{recorded}\t{closed}\t{}\tgaps={gaps}\tgame_reads={game_reads}", if ok { "MATCH" } else { "DIFFERENT" }, card.display(), live[i]);
                eprintln!("{}", card_memory_line("closed", card, start));
            }
            println!("OKCHECK finished {pass} match, {fail} different in {:.0}s", started.elapsed().as_secs_f64());
        }
        "okcard-verify" => {
            // okcard-verify <lib> <card list>: for each original card: game-mode digest, the OK card made from
            //   the library plus its zipmods (with its pack), and that OK card reopened from OK data alone
            //   (no game, no zipmods). work/okcard_verify.tsv:
            //   card, verdict, live, OK card (closed), ratio, pack bytes, seconds, detail.
            use std::io::Write;
            let lib = lib.ok_or("library directory required")?;
            let list = std::fs::read_to_string(args.get(2).ok_or("card list required")?).map_err(|e| e.to_string())?;
            let work = lib.join("work");
            let cards_dir = work.join("okcards");
            std::fs::create_dir_all(&cards_dir).map_err(|e| e.to_string())?;
            let mut out = std::fs::OpenOptions::new().create(true).append(true).open(work.join("okcard_verify.tsv")).map_err(|e| e.to_string())?;
            for card in list.lines().map(str::trim).filter(|l| !l.is_empty()).map(PathBuf::from) {
                let t0 = std::time::Instant::now();
                let live = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| live_digest(game, &card))).unwrap_or_else(|_| Err("panic".into()));
                let t_live = t0.elapsed().as_secs_f64();
                let made = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| remake_ok_card(&lib, game, &card))).unwrap_or_else(|_| Err("panic".into()));
                deactivate();
                crate::gamefs::deactivate();
                let (ok_path, ratio, pack_len) = match &made {
                    Ok((_, _, bytes, pack)) => {
                        let path = cards_dir.join(card.file_name().unwrap_or_default());
                        let _ = std::fs::write(&path, bytes);
                        (Some(path), bytes.len() as f64 / std::fs::metadata(&card).map(|m| m.len()).unwrap_or(1).max(1) as f64, *pack)
                    }
                    Err(_) => (None, 0.0, 0),
                };
                let t_made = t0.elapsed().as_secs_f64();
                // Closed: library plus the card's pack only.
                let closed = match &ok_path {
                    Some(p) => std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| { ensure_library(&lib)?; session_digest(&lib, p) })).unwrap_or_else(|_| Err("panic".into())),
                    None => Err("no OK card".into()),
                };
                let gaps = active().map(|s| s.missing_list().len()).unwrap_or(0);
                eprintln!("OKCARDV TIME live={t_live:.1}s make={:.1}s closed={:.1}s", t_made - t_live, t0.elapsed().as_secs_f64() - t_made);
                deactivate();
                crate::gamefs::deactivate();
                let d = |r: &Result<(String, usize, Vec<u8>), String>| r.as_ref().map(|x| x.0.clone()).unwrap_or_else(|_| "-".into());
                let verdict = match (&live, &made, &closed) {
                    (Ok(a), Ok(b), Ok(c)) if a.0 == b.0 && b.0 == c.0 => "MATCH",
                    (Ok(a), Ok(b), Ok(_)) if a.0 == b.0 => "CLOSED_DIFFERENT",
                    (Ok(_), Ok(_), Ok(_)) => "DIFFERENT",
                    (Err(_), _, _) => "LIVE_ERROR",
                    (_, Err(_), _) => "MAKE_ERROR",
                    (_, _, Err(_)) => "CLOSED_ERROR",
                };
                let detail = match (&live, &made, &closed) {
                    (Err(e), _, _) | (_, Err(e), _) | (_, _, Err(e)) => e.replace(['\t', '\n'], " "),
                    _ => format!("gaps={gaps}"),
                };
                let original_len = std::fs::metadata(&card).map(|m| m.len()).unwrap_or(0);
                let size = match &made {
                    Ok((_, _, bytes, pack)) => check_ok_card_size(original_len, bytes.len() as u64, *pack, card_zipmod_bytes(game, &card) + LAST_UNNAMED_ZIPMOD_BYTES.load(Ordering::Relaxed)).unwrap_or_else(|e| format!("SIZE_FAIL {e}")),
                    Err(_) => String::new(),
                };
                let line = format!("{}\t{verdict}\t{}\t{}\t{ratio:.4}\t{pack_len}\t{:.1}\t{detail}\t{size}\n", card.display(), d(&live), d(&closed), t0.elapsed().as_secs_f64());
                print!("OKCARDV {line}");
                let _ = out.write_all(line.as_bytes());
            }
        }
        "reencode-pack" => {
            // reencode-pack <OK card> <out>: the same card with its okLc written again by this build (independent
            // frames, decoded in parallel on open); records, answers and identity unchanged.
            let bytes = std::fs::read(args.get(1).ok_or("card required")?).map_err(|e| e.to_string())?;
            let out = extend_card_pack(&bytes, crate::okstrict::Additions::default())?;
            std::fs::write(args.get(2).ok_or("output required")?, out).map_err(|e| e.to_string())?;
        }
        "pack-keys" => {
            // pack-keys <OK card>: what its okLc and okLt hold, one sorted line each (record keys with their value
            //   hashes, overlay files, archives and entries, answer keys): two conversions of a card are compared by it.
            let bytes = std::fs::read(args.get(1).ok_or("card required")?).map_err(|e| e.to_string())?;
            let mut lines = Vec::new();
            if let Some(chunk) = png_chunk(&bytes, PACK_CHUNK) {
                let raw = decode_card_pack_bytes(chunk)?;
                let pack: CardPack = bincode::deserialize(&raw).map_err(|e| e.to_string())?;
                for PackRecord(id, query, a, value) in &pack.records { lines.push(format!("record {} {query} {} {}", hex(id), hex(a), hex(&hash16(value)))); }
                for (k, e) in &pack.index.files { lines.push(format!("file {k} {}", hex(&hash16(&bincode::serialize(e).unwrap_or_default())))); }
                for (k, d) in &pack.index.dirs { lines.push(format!("dir {k} {}", d.len())); }
                for (k, z) in &pack.index.zips {
                    lines.push(format!("zip {k} names={}", z.names.len()));
                    for (i, e) in &z.entries { lines.push(format!("entry {k} {i} {}", hex(&hash16(&bincode::serialize(e).unwrap_or_default())))); }
                }
            }
            if let Some(table) = crate::tree_answers::parse(&bytes) {
                for ((kind, key), value) in table.iter() { lines.push(format!("answer {kind} {} {}", hex(key), hex(&hash16(value)))); }
            }
            lines.sort();
            for l in lines { println!("{l}"); }
        }
        "pack-diff" => {
            // pack-diff <lib> <card>: the original card from the library plus its zipmods, without and with the
            //   card pack being recorded; the sessions must be identical.
            let lib = lib.ok_or("library directory required")?;
            let card = PathBuf::from(args.get(2).ok_or("card required")?);
            let plain = with_zipmods(&lib, game, &card, || session_digest(&lib, &card))?;
            let work = lib.join("work").join("pack_diff");
            let recorded = with_zipmods(&lib, game, &card, || {
                begin_card_pack(&work)?;
                let r = session_digest(&lib, &card);
                end_card_pack()?;
                r
            })?;
            println!("plain {} parts={} / recording {} parts={}", plain.0, plain.1, recorded.0, recorded.1);
            let (a, b) = (crate::session::preview_json(&plain.2)?, crate::session::preview_json(&recorded.2)?);
            let parts = |v: &serde_json::Value| v["session"]["parts"].as_array().cloned().unwrap_or_default();
            let names = |v: &serde_json::Value| parts(v).iter().map(|p| p["name"].as_str().unwrap_or("").to_owned()).collect::<Vec<_>>();
            println!("only plain: {:?}", names(&a).into_iter().filter(|n| !names(&b).contains(n)).collect::<Vec<_>>());
            println!("only recording: {:?}", names(&b).into_iter().filter(|n| !names(&a).contains(n)).collect::<Vec<_>>());
            let mut out = Vec::new();
            diff_json(&a, &b, String::new(), &mut out);
            for line in out.iter().take(40) { println!("  {line}"); }
        }
        "kks-select" => {
            // kks-select <lib> <card list> <count>: original cards naming zipmods the library does not record
            //   (they exercise the zipmod fallback). Prints "<card>\t<zipmods outside the library>".
            let lib = lib.ok_or("library directory required")?;
            let list = std::fs::read_to_string(args.get(2).ok_or("card list required")?).map_err(|e| e.to_string())?;
            let count: usize = args.get(3).and_then(|v| v.parse().ok()).unwrap_or(10);
            ensure_library(&lib)?;
            let recorded = crate::gamefs::recorded_zip_names();
            let mut found = 0;
            for card in list.lines().map(str::trim).filter(|l| !l.is_empty()).map(PathBuf::from) {
                if found >= count { break; }
                let outside: Vec<String> = card_zipmods(game, &card).iter()
                    .filter_map(|z| z.file_name().map(|n| n.to_string_lossy().to_ascii_lowercase()))
                    .filter(|n| !recorded.contains(n)).collect();
                if outside.is_empty() { continue; }
                found += 1;
                println!("{}\t{}", card.display(), outside.join(" | "));
            }
        }
        "kks-verify" => {
            // kks-verify <lib> <card list>: original cards opened from the library plus their own zipmods
            //   (card directory, the game's mods/) against game mode (work/kks_verify.tsv).
            //   Columns: card, verdict, zipmod fallback used, library misses, seconds, detail.
            use std::io::Write;
            let lib = lib.ok_or("library directory required")?;
            let list = std::fs::read_to_string(args.get(2).ok_or("card list required")?).map_err(|e| e.to_string())?;
            let work = lib.join("work");
            std::fs::create_dir_all(&work).map_err(|e| e.to_string())?;
            let mut out = std::fs::OpenOptions::new().create(true).append(true).open(work.join("kks_verify.tsv")).map_err(|e| e.to_string())?;
            for card in list.lines().map(str::trim).filter(|l| !l.is_empty()).map(PathBuf::from) {
                let live = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| session_digest(game, &card))).unwrap_or_else(|_| Err("panic".into()));
                let t0 = std::time::Instant::now();
                let opened = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    let s = open_with_zipmods(&lib, game, &card, 0)?;
                    let used = crate::gamefs::fallback_used();
                    let misses = active().map(|st| st.misses.load(Ordering::Relaxed)).unwrap_or(0);
                    Ok::<_, String>((hex(&hash16(&s.to_preview_bytes()?)), used, misses))
                })).unwrap_or_else(|_| Err("panic".into()));
                let seconds = t0.elapsed().as_secs_f64();
                let (misses0, used) = match &opened { Ok((_, u, m)) => (*m, *u), Err(_) => (0, false) };
                deactivate();
                crate::gamefs::deactivate();
                let verdict = match (&live, &opened) {
                    (Ok((a, _, _)), Ok((b, _, _))) if a == b => "MATCH",
                    (Ok(_), Ok(_)) => "DIFFERENT",
                    (Err(_), _) => "LIVE_ERROR",
                    (_, Err(_)) => "LIBRARY_ERROR",
                };
                let detail = match (&live, &opened) { (Err(e), _) | (_, Err(e)) => e.replace(['\t', '\n'], " "), _ => String::new() };
                let line = format!("{}\t{verdict}\t{used}\t{misses0}\t{seconds:.1}\t{detail}\n", card.display());
                print!("OKKKS {line}");
                let _ = out.write_all(line.as_bytes());
            }
        }
        "needs" | "verify" => {
            // needs <lib> <card list> <shard> <shards>: replay cards on the library and
            //   list the Unity sources they still need (needs_<shard>.txt) and query gaps.
            // verify <lib> <card list> <shard> <shards>: game-mode digest vs library digest
            //   and the OK card size for every card (verify_<shard>.tsv).
            let lib = lib.ok_or("library directory required")?;
            let list = std::fs::read_to_string(args.get(2).ok_or("card list required")?).map_err(|e| e.to_string())?;
            let shard: usize = args.get(3).and_then(|v| v.parse().ok()).unwrap_or(0);
            let shards: usize = args.get(4).and_then(|v| v.parse().ok()).unwrap_or(1).max(1);
            let cards: Vec<PathBuf> = list.lines().map(str::trim).filter(|l| !l.is_empty()).enumerate()
                .filter(|(i, _)| i % shards == shard).map(|(_, l)| PathBuf::from(l)).collect();
            let work = lib.join("work");
            std::fs::create_dir_all(&work).map_err(|e| e.to_string())?;
            if command == "needs" {
                ensure_library(&lib)?;
                let store = active().ok_or("library not active")?;
                for (n, card) in cards.iter().enumerate() {
                    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| session_digest(&lib, card)));
                    if n % 10 == 0 { println!("OKNEEDS {n}/{} missing={}", cards.len(), store.missing_list().len()); }
                    if !matches!(r, Ok(Ok(_))) { println!("OKNEEDS error {}", card.display()); }
                }
                let mut sources = BTreeMap::new();
                let mut gaps = Vec::new();
                for (what, count) in store.missing_list() {
                    let source = what.strip_prefix("GAMEFS read ").or_else(|| what.strip_prefix("GAMEFS zip-entry "))
                        .filter(|s| s.ends_with(".unity3d") || s.starts_with("abdata/") && !s.contains('.'));
                    // A query on a model with no file: its source was never converted.
                    // Every converted source has its directories: missing them means not converted.
                    let basic = ["texture_directory2", "prefab_directory"].iter().any(|q| what.starts_with(&format!("{q} ")));
                    let unconverted = what.split_whitespace().nth(1).and_then(id_from_hex).filter(|id| basic || !model_path(&lib, id).is_file());
                    match (source, unconverted) {
                        (Some(s), _) => { *sources.entry(s.to_owned()).or_insert(0u32) += count; }
                        (None, Some(id)) => { for s in crate::gamefs::labels_for_model(&id) { *sources.entry(s).or_insert(0u32) += count; } }
                        (None, None) => gaps.push(format!("{count}\t{what}")),
                    }
                }
                std::fs::write(work.join(format!("needs_{shard}.txt")), sources.keys().cloned().collect::<Vec<_>>().join("\n") + "\n").map_err(|e| e.to_string())?;
                std::fs::write(work.join(format!("gaps_{shard}.txt")), gaps.join("\n") + "\n").map_err(|e| e.to_string())?;
                println!("OKNEEDS done cards={} sources={} gaps={}", cards.len(), sources.len(), gaps.len());
            } else {
                use std::io::Write;
                let mut out = std::fs::OpenOptions::new().create(true).append(true).open(work.join(format!("verify_{shard}.tsv"))).map_err(|e| e.to_string())?;
                for card in &cards {
                    let live = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| session_digest(game, card))).unwrap_or_else(|_| Err("panic".into()));
                    // Library mode in a separate activation: replay never sees the game.
                    ensure_library(&lib)?;
                    let store = active().ok_or("library not active")?;
                    store.forget_models();
                    let miss0 = store.misses.load(Ordering::Relaxed);
                    let t0 = std::time::Instant::now();
                    let replay = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| session_digest(&lib, card))).unwrap_or_else(|_| Err("panic".into()));
                    let seconds = t0.elapsed().as_secs_f64();
                    let misses = store.misses.load(Ordering::Relaxed) - miss0;
                    let models: Vec<String> = store.touched().iter().map(id_hex).collect();
                    deactivate();
                    crate::gamefs::deactivate();
                    let original = std::fs::read(card).unwrap_or_default();
                    let ok_card = match &replay { Ok((digest, _, _)) => library_card(&original, &serde_json::to_vec(&serde_json::json!({"format":"oklib-card","version":1,"digest":digest,"models":models})).unwrap()).ok(), Err(_) => None };
                    let ratio = ok_card.as_ref().map(|c| c.len() as f64 / original.len().max(1) as f64).unwrap_or(0.);
                    let verdict = match (&live, &replay) {
                        (Ok((a, _, _)), Ok((b, _, _))) if a == b => "MATCH",
                        (Ok(_), Ok(_)) => "DIFFERENT",
                        (Err(_), _) => "LIVE_ERROR",
                        (_, Err(_)) => "LIBRARY_ERROR",
                    };
                    let detail = match (&live, &replay) { (Err(e), _) | (_, Err(e)) => e.replace(['\t', '\n'], " "), _ => String::new() };
                    let line = format!("{}\t{verdict}\t{misses}\t{seconds:.1}\t{ratio:.4}\t{}\t{detail}\n", card.display(), models.len());
                    print!("OKVERIFY {line}");
                    let _ = out.write_all(line.as_bytes());
                }
            }
        }
        "kkex" => match args.get(2) {
            Some(plugin) => crate::okclothes::probe_plugin(&lib.ok_or("file required")?, plugin)?,
            None => crate::okclothes::probe(&lib.ok_or("file required")?)?,
        },
        "clothes-import" => {
            // clothes-import <coordinate card> <out.okclothes>
            let input = PathBuf::from(args.get(1).ok_or("coordinate card required")?);
            let out = PathBuf::from(args.get(2).ok_or("output required")?);
            let original = std::fs::read(&input).map_err(|e| e.to_string())?;
            let (clothes, thumb) = crate::okclothes::import_coordinate(&original)?;
            let bytes = clothes.to_bytes(&thumb)?;
            std::fs::write(&out, &bytes).map_err(|e| e.to_string())?;
            let ratio = bytes.len() as f64 / original.len() as f64;
            println!("OKCLOTHES import {} -> {} bytes={} original={} ratio={ratio:.4} dropped={:?}", input.display(), out.display(), bytes.len(), original.len(), clothes.dropped);
            if ratio > 1.10 { return Err(format!("okclothes is {:.1}% of the coordinate card (limit 110%)", ratio * 100.)); }
        }
        "clothes-export" => {
            // clothes-export <card> <outfit> <out.okclothes>
            let card = PathBuf::from(args.get(1).ok_or("card required")?);
            let outfit: usize = args.get(2).and_then(|v| v.parse().ok()).ok_or("outfit required")?;
            let out = PathBuf::from(args.get(3).ok_or("output required")?);
            let original = std::fs::read(&card).map_err(|e| e.to_string())?;
            let (clothes, thumb) = crate::okclothes::export_outfit(&original, outfit)?;
            let bytes = clothes.to_bytes(&thumb)?;
            std::fs::write(&out, &bytes).map_err(|e| e.to_string())?;
            println!("OKCLOTHES export {} outfit={outfit} -> {} bytes={}", card.display(), out.display(), bytes.len());
        }
        "clothes-wear" => {
            // clothes-wear <card> <okclothes> <outfit> <out card>
            let card = std::fs::read(args.get(1).ok_or("card required")?).map_err(|e| e.to_string())?;
            let clothes = crate::okclothes::OkClothes::from_bytes(&std::fs::read(args.get(2).ok_or("okclothes required")?).map_err(|e| e.to_string())?)?;
            let outfit: usize = args.get(3).and_then(|v| v.parse().ok()).ok_or("outfit required")?;
            let out = PathBuf::from(args.get(4).ok_or("output required")?);
            let (bytes, dropped) = crate::okclothes::wear(&card, &clothes, outfit)?;
            std::fs::write(&out, &bytes).map_err(|e| e.to_string())?;
            println!("OKCLOTHES wear outfit={outfit} -> {} dropped={dropped:?}", out.display());
        }
        "clothes-test" => {
            // clothes-test <work dir> <card> [outfit]: export -> wear back -> same session.
            let work = lib.ok_or("work directory required")?;
            std::fs::create_dir_all(&work).map_err(|e| e.to_string())?;
            let card = PathBuf::from(args.get(2).ok_or("card required")?);
            let outfit: usize = args.get(3).and_then(|v| v.parse().ok()).unwrap_or(0);
            let original = std::fs::read(&card).map_err(|e| e.to_string())?;
            let (clothes, thumb) = crate::okclothes::export_outfit(&original, outfit)?;
            let packed = clothes.to_bytes(&thumb)?;
            let clothes = crate::okclothes::OkClothes::from_bytes(&packed)?;
            let (worn, dropped) = crate::okclothes::wear(&original, &clothes, outfit)?;
            let worn_path = work.join(format!("{}.worn.png", card_key(&card)));
            std::fs::write(&worn_path, &worn).map_err(|e| e.to_string())?;
            let a = crate::session::open(game, Some(&card), outfit as i32)?.to_preview_bytes()?;
            let b = crate::session::open(game, Some(&worn_path), outfit as i32)?.to_preview_bytes()?;
            let verdict = if a == b { "MATCH" } else { "DIFFERENT" };
            if a != b { let _ = std::fs::write(work.join("a.okpreview"), &a); let _ = std::fs::write(work.join("b.okpreview"), &b); }
            println!("OKCLOTHES test {} outfit={outfit} {verdict} okclothes={} card={} dropped={dropped:?}", card.display(), packed.len(), original.len());
        }
        "seal" => {
            // seal <lib> <OK card>... [--out DIR]: make each OK card open from its own data and the shared resources
            // only (okstrict.rs): okLt answers, the third-party data it reads from the library copied into its okLc.
            // In place, or written to DIR. One card per process is the reliable check (strict-check).
            let lib = lib.ok_or("library directory required")?;
            let (mut cards, mut out_dir) = (Vec::new(), None);
            let mut rest = args[2..].iter();
            while let Some(a) = rest.next() {
                if a == "--out" { out_dir = Some(PathBuf::from(rest.next().ok_or("--out needs a directory")?)); } else { cards.push(PathBuf::from(a)); }
            }
            let mut failed = 0;
            for card in cards {
                let t = std::time::Instant::now();
                let made = std::fs::read(&card).map_err(|e| e.to_string()).and_then(|bytes| {
                    if !is_library_card(&card) { return Err("not an OK card".into()); }
                    let (out, summary) = seal_card(&lib, &bytes)?;
                    let target = match &out_dir { Some(d) => d.join(card.file_name().unwrap_or_default()), None => card.clone() };
                    let temp = target.with_extension("png.seal-tmp");
                    std::fs::write(&temp, &out).map_err(|e| e.to_string())?;
                    std::fs::rename(&temp, &target).map_err(|e| e.to_string())?;
                    Ok((bytes.len(), out.len(), summary))
                });
                match made {
                    Ok((before, after, summary)) => println!("OKSEAL ok\t{}\t{before} -> {after} bytes\t{summary}\t{:.1}s", card.display(), t.elapsed().as_secs_f64()),
                    Err(e) => { failed += 1; println!("OKSEAL error\t{}\t{e}", card.display()); }
                }
            }
            if failed > 0 { return Err(format!("{failed} card(s) failed")); }
        }
        "seal-step" => {
            // seal-step <lib> answers|complete <in> <out>: one step of seal_card, run in a process of its own.
            let lib = lib.ok_or("library directory required")?;
            let (step, input, output) = (args.get(2).ok_or("step required")?, args.get(3).ok_or("input required")?, args.get(4).ok_or("output required")?);
            match std::fs::read(input).map_err(|e| e.to_string()).and_then(|bytes| seal_step(&lib, step, &bytes)) {
                Ok((out, summary)) => {
                    std::fs::write(output, &out).map_err(|e| e.to_string())?;
                    println!("OKSEALSTEP {summary}");
                }
                Err(e) => { println!("OKSEALSTEP error {step}: {e}"); return Err(e); }
            }
        }
        "strict-check" => {
            // strict-check <lib> <OK card>...: every outfit opens from the card and the shared resources only.
            let lib = lib.ok_or("library directory required")?;
            let mut failed = 0;
            for card in args[2..].iter().map(PathBuf::from) {
                let t = std::time::Instant::now();
                match std::fs::read(&card).map_err(|e| e.to_string()).and_then(|bytes| open_outfits(&lib, &bytes, "strict")) {
                    Ok(()) => println!("OKSTRICT ok\t{}\t{:.1}s", card.display(), t.elapsed().as_secs_f64()),
                    Err(e) => { failed += 1; println!("OKSTRICT error\t{}\t{e}", card.display()); }
                }
            }
            if failed > 0 { return Err(format!("{failed} card(s) failed")); }
        }
        "crowd-check" => {
            // crowd-check <lib> <card>...: whether the hundred-girl room can use each card's outfit 0 (dressed, every drawn
            // material supported): OKCROWD lines (oklib/crowd_check.rs).
            crowd_check::cli(&lib.ok_or("library directory required")?, game, &args[2..])?;
        }
        "check" => {
            // check <lib>: every mesh record decodes with the current layout.
            let lib = lib.ok_or("library directory required")?;
            let (models, fixed, failures) = check_models(&lib)?;
            println!("OKCHECK models={models} mesh_records={fixed} failures={}", failures.len());
            for f in failures.iter().take(20) { println!("  {f}"); }
        }
        "repack" => {
            // repack <lib> [threads]: migrate legacy mesh records (texture_plan layout).
            let lib = lib.ok_or("library directory required")?;
            let threads = args.get(2).and_then(|v| v.parse().ok()).unwrap_or(8);
            let (models, fixed, failures) = repack(&lib, threads, |q, raw| crate::unity::migrate_mesh_record(q, raw))?;
            println!("OKREPACK models={models} fixed={fixed} failures={}", failures.len());
            for f in failures.iter().take(20) { println!("  {f}"); }
        }
        "models" => {
            // models <lib>: every OK model with its record counts by query.
            let lib = lib.ok_or("library directory required")?;
            let models = model_summary(&lib);
            let mut queries: BTreeMap<String, usize> = BTreeMap::new();
            let bytes: u64 = models.iter().map(|m| m.2).sum();
            for m in &models { for (q, n) in &m.3 { *queries.entry(q.clone()).or_default() += n; } }
            println!("OKLIB MODELS count={} bytes={} records={}", models.len(), bytes, models.iter().map(|m| m.1).sum::<usize>());
            for (q, n) in queries { println!("  {q:32} {n}"); }
        }
        "shared-ignore" => {
            // shared-ignore <lib> [shared mods list]: writes <lib>/models/.gitignore. A model is a shared resource
            //   (kept in version control) when one of its sources is the original game (not under mods/) or a
            //   listed shared mod (Tools/base_library_shared_mods.txt: "folder/", "zip|bundle" or "zip" lines);
            //   every other model (third-party mods, models no source names) stays out. See
            //   Docs/BASE_LIBRARY_SHARED_RESOURCES_PLAN.md.
            let lib = lib.ok_or("library directory required")?;
            let list = args.get(2).map(PathBuf::from).unwrap_or_else(|| {
                lib.parent().and_then(Path::parent).and_then(Path::parent).unwrap_or(&lib).join("Tools").join("base_library_shared_mods.txt")
            });
            let shared: Vec<String> = std::fs::read_to_string(&list).map_err(|e| format!("{}: {e}", list.display()))?
                .lines().map(|l| l.trim().replace('\\', "/").to_ascii_lowercase()).filter(|l| !l.is_empty() && !l.starts_with('#')).collect();
            // Bundles under a zipmod's abdata/list/ are read by every card's item catalog scan: shared as well.
            let is_shared = |label: &str| !label.starts_with("mods/") || label.contains("|abdata/list/") || shared.iter().any(|s| {
                if s.ends_with('/') { label.starts_with(s.as_str()) }
                else if s.contains('|') { label == s }
                else { label.strip_prefix(s.as_str()).is_some_and(|rest| rest.starts_with('|')) }
            });
            crate::gamefs::activate(crate::gamefs::Mode::Replay, &lib, &lib)?;
            let labels = crate::gamefs::model_labels();
            crate::gamefs::deactivate();
            let (mut kept, mut kept_bytes, mut out_bytes, mut unnamed) = (0usize, 0u64, 0u64, 0usize);
            let mut excluded: Vec<String> = Vec::new();
            let mut shards: Vec<_> = std::fs::read_dir(lib.join("models")).map_err(|e| e.to_string())?.flatten().filter(|e| e.path().is_dir()).collect();
            shards.sort_by_key(|e| e.file_name());
            for shard in shards {
                let mut files: Vec<_> = std::fs::read_dir(shard.path()).map_err(|e| e.to_string())?.flatten().collect();
                files.sort_by_key(|e| e.file_name());
                for f in files {
                    let name = f.file_name().to_string_lossy().into_owned();
                    let Some(id) = name.strip_suffix(".okmodel").and_then(id_from_hex) else { continue };
                    let len = f.metadata().map(|m| m.len()).unwrap_or(0);
                    let sources = labels.get(&id).map(Vec::as_slice).unwrap_or(&[]);
                    if sources.is_empty() { unnamed += 1; }
                    if sources.iter().any(|l| is_shared(l)) { kept += 1; kept_bytes += len; }
                    else {
                        out_bytes += len;
                        excluded.push(format!("/{}/{name}", shard.file_name().to_string_lossy()));
                        if std::env::var_os("OKLIB_SHARED_VERBOSE").is_some() { println!("OKLIB EXCLUDED {name}\t{}", sources.join(" ; ")); }
                    }
                }
            }
            let mut text = String::from("# Generated by `ok-extract <game> --oklib shared-ignore <lib>` from Tools/base_library_shared_mods.txt.\n\
                # Models only third-party mods (or no recorded source) use stay out of version control; the original game,\n\
                # the uncensor bodies and the shared mods stay in. Docs/BASE_LIBRARY_SHARED_RESOURCES_PLAN.md.\n");
            for e in &excluded { text.push_str(e); text.push('\n'); }
            std::fs::write(lib.join("models").join(".gitignore"), text).map_err(|e| e.to_string())?;
            // The library carries its list: an OK card's strict open (okstrict.rs) tells shared mods by it, also where
            // the project's Tools folder is not (a packaged game).
            std::fs::copy(&list, lib.join("shared_mods.txt")).map_err(|e| format!("{}: {e}", list.display()))?;
            println!("OKLIB SHARED kept={kept} ({:.2} GB) excluded={} ({:.2} GB) without_source={unnamed}",
                kept_bytes as f64 / 1e9, excluded.len(), out_bytes as f64 / 1e9);
        }
        "sources" => {
            // sources <lib>: what the observation index holds, by kind and extension.
            let lib = lib.ok_or("library directory required")?;
            crate::gamefs::activate(crate::gamefs::Mode::Replay, &lib, &lib)?;
            for (kind, ext, count, bytes) in crate::gamefs::summary() { println!("OKLIB SOURCES {kind:8} {ext:12} {count:7} {bytes:12}"); }
            crate::gamefs::deactivate();
        }
        "parts" => {
            // parts <preview>: index, name and texture names of each part.
            let v = crate::session::preview_json(&std::fs::read(args.get(1).ok_or("preview required")?).map_err(|e| e.to_string())?)?;
            for (i, p) in v["session"]["parts"].as_array().cloned().unwrap_or_default().iter().enumerate() {
                println!("{i}	{}	{}	{}", p["name"].as_str().unwrap_or(""), p["texture_name"].as_str().unwrap_or(""), p["role"].as_str().unwrap_or(""));
            }
        }
        "model" => {
            // model <lib> <id hex>: records of one model; directories and plans in full.
            let lib = lib.ok_or("library directory required")?;
            let id = args.get(2).and_then(|h| id_from_hex(h)).ok_or("model id required")?;
            let path = model_path(&lib, &id);
            let model = load_model(&path, false);
            let handle = File::open(&path).map_err(|e| e.to_string())?;
            let mut keys: Vec<_> = model.entries.keys().cloned().collect();
            keys.sort();
            for (q, a) in keys {
                let raw = read_loc(&handle, &model.entries[&(q.clone(), a.clone())]).unwrap_or_default();
                let detail = match q.as_str() {
                    "prefab_directory" => bincode::deserialize::<crate::unity::PrefabDirectory>(&raw).map(|d| format!("{d:?}")).unwrap_or_default(),
                    "texture_directory2" => crate::unity::with_texture_plans(|| bincode::deserialize::<crate::unity::TexDirectory>(&raw)).map(|d| format!("{d:?}")).unwrap_or_default(),
                    "meshes_for_maindata" => crate::unity::with_texture_plans(|| bincode::deserialize::<(Vec<crate::unity::ExtractedMesh>, Vec<(i64, Vec<String>)>)>(&raw))
                        .map(|(m, _)| m.iter().map(|m| format!("{} plan={:?} extra={:?}", m.name, m.texture_plan, m.shader_extra_textures.keys().collect::<Vec<_>>())).collect::<Vec<_>>().join(" ; ")).unwrap_or_default(),
                    _ => format!("{} bytes", raw.len()),
                };
                println!("{q}	{}	{}", describe_args(&a), if detail.len() > 1500 { &detail[..1500] } else { &detail });
            }
        }
        "probe-mesh" => {
            // probe-mesh <lib> <id hex> <main_data>: replayed meshes and their texture fields.
            let lib = lib.ok_or("library directory required")?;
            let id = args.get(2).and_then(|h| id_from_hex(h)).ok_or("model id required")?;
            ensure_library(&lib)?;
            let parsed = crate::unity::parse_bundle_cached(token(&id));
            let (meshes, _) = parsed.meshes_for_maindata(args.get(3).map(String::as_str).unwrap_or(""));
            for m in &meshes {
                println!("{} tex={:?} detail={:?} normal={:?} extra={:?}", m.name, m.texture_png.as_ref().map(|v| v.len()), m.detail_mask_png.as_ref().map(|v| v.len()),
                    m.normal_map_png.as_ref().map(|v| v.len()), m.shader_extra_textures.iter().map(|(k, v)| (k.clone(), v.len())).collect::<Vec<_>>());
            }
            println!("misses={:?}", active().map(|s| s.missing_list()));
        }
        "model-diff" => crate::okmodel_view::diff_cli(&lib.ok_or("library directory required")?, args.get(2).ok_or("model id required")?,
            args.get(3).ok_or("prefab required")?, Path::new(args.get(4).ok_or("card required")?))?,
        "model-texture" => {
            // model-texture <lib> <id> <texture> <out.png>
            let lib = lib.ok_or("library directory required")?;
            let id = id_from_hex_pub(args.get(2).ok_or("model id required")?).ok_or("bad model id")?;
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            let name = args.get(3).ok_or("texture name required")?;
            let png = view.texture_png(name).ok_or_else(|| format!("texture {name}: no image"))?;
            std::fs::write(args.get(4).ok_or("output required")?, &png).map_err(|e| e.to_string())?;
            println!("{name}: {} bytes", png.len());
        }
        "studio-catalog" => {
            // Rewrites studio_items.json from the game lists and bundles (as a full convert does), nothing else.
            let lib = lib.ok_or("library directory required")?;
            let mut studio = crate::studio_items::catalog(game);
            let unresolved = crate::studio_items::resolve_models(game, &mut studio);
            crate::studio_items::write(&lib, &studio)?;
            println!("OKCONVERT studio items={} with components={} unresolved={unresolved}", studio.items.len(), studio.items.iter().filter(|i| i.line_width.is_some()).count());
        }
        "studio-child-anims" => crate::studio_child_animation::convert(game, &lib.ok_or("library directory required")?)?,
        "studio-check" => crate::studio_items::check(&lib.ok_or("library directory required")?)?,
        "studio-images" => println!("OKCONVERT studio images copied={}", crate::studio_items::copy_user_images(game, &lib.ok_or("library directory required")?)?),
        "studio-sky" => println!("OKCONVERT studio sky patterns={}", crate::studio_sky::convert(game, &lib.ok_or("library directory required")?)?),
        "studio-environments" => println!("OKCONVERT studio map environments={}", crate::studio_sky::convert_map_environments(game, &lib.ok_or("library directory required")?)?),
        "studio-reflections" => println!("OKCONVERT studio map reflections={}", crate::studio_sky::convert_map_reflections(game,&lib.ok_or("library directory required")?)?),
        "studio-nodes" => crate::studio_items::record_hierarchies(game, &lib.ok_or("library directory required")?, args.get(2).and_then(|t| t.parse().ok()).unwrap_or(6))?,
        "studio-standard-textures" => crate::studio_items::record_standard_map_textures(game, &lib.ok_or("library directory required")?, args.get(2).map(String::as_str))?,
        "anim-convert" => {
            let card = std::path::PathBuf::from(args.get(2).ok_or("character card required (sampling skeleton)")?);
            let filter = args.get(3).map(String::as_str).filter(|f| !f.is_empty() && *f != "all");
            let threads = args.get(4).and_then(|t| t.parse().ok()).unwrap_or(6);
            let c = crate::studio_anims::convert(&lib.ok_or("library directory required")?, game, &card, filter, threads)?;
            println!("OKANIM catalogue {} animations, {} with files", c.anims.len(), c.anims.iter().filter(|a| !a.file.is_empty()).count());
        }
        "scene-stats" => {
            let s = crate::studio_scene::stats(&lib.ok_or("library directory required")?, std::path::Path::new(args.get(2).ok_or("scene folder required")?))?;
            let mut top: Vec<_> = s.fk_item_ids.iter().collect(); top.sort_by(|a, b| b.1.cmp(a.1));
            println!("SCENES fk_items={} distinct={} top={:?}", s.fk_items, top.len(), &top[..top.len().min(12)]);
            println!("SCENES main_cameras={}", s.main_cameras);
            println!("SCENES routes_moving={} routes_moved_start={}", s.routes_moving, s.routes_moved_start);
            println!("SCENES files={} parsed={} characters={} items={} vanilla_items={} mod_items={} lights={} folders={} routes={} cameras={} texts={}", s.files, s.parsed, s.characters, s.items, s.vanilla_items, s.mod_items, s.lights, s.folders, s.routes, s.cameras, s.texts);
            println!("SCENES maps {:?}", s.maps);
            for (f, e) in &s.failed { println!("SCENES FAILED {f}: {e}"); }
        }
        "model-view" => crate::okmodel_view::cli(&lib.ok_or("library directory required")?, args.get(2).ok_or("model id required")?, args.get(3).map(String::as_str))?,
        "diff" => {
            let a = std::fs::read(args.get(1).ok_or("two preview files")?).map_err(|e| e.to_string())?;
            let b = std::fs::read(args.get(2).ok_or("two preview files")?).map_err(|e| e.to_string())?;
            let (a, b) = (crate::session::preview_json(&a)?, crate::session::preview_json(&b)?);
            let mut out = Vec::new();
            diff_json(&a, &b, String::new(), &mut out);
            println!("OKLIB DIFF {} difference(s)", out.len());
            // Which parts differ, by name (differences inside one part repeat its index).
            let mut parts: BTreeMap<usize, usize> = BTreeMap::new();
            for line in &out { if let Some(i) = line.strip_prefix(".session.parts[").and_then(|r| r.split(']').next()).and_then(|n| n.parse().ok()) { *parts.entry(i).or_default() += 1; } }
            let name = |v: &serde_json::Value, i: usize| v["session"]["parts"][i]["name"].as_str().unwrap_or("?").to_owned();
            for (i, n) in &parts { println!("  part[{i}] {} / {}: {n} difference(s)", name(&a, *i), name(&b, *i)); }
            for line in out.iter().take(60) { println!("  {line}"); }
        }
        "live" => {
            for card in &args[1..] {
                let card = Path::new(card);
                let (digest, parts, bytes) = live_digest(game, card)?;
                if let Some(dir) = std::env::var_os("OKLIB_KEEP") { let _ = std::fs::write(Path::new(&dir).join(format!("{}.live.okpreview", card_key(card))), &bytes); }
                println!("OKLIB LIVE {} parts={parts} digest={digest}", card_key(card));
            }
        }
        _ => return Err("usage: --oklib convert|record|replay|live|diff|probe|models|sources ...".into()),
    }
    Ok(())
}

#[cfg(test)]
mod ok_card_edit_tests {
    use super::*;
    /// Editing an OK card (wearing another card's outfit) and saving it makes a clean OK card again: reopened from OK
    /// data alone it equals the edited card in game mode, and its recorded digest is that session.
    #[test]
    #[ignore = "reads the KKS installation, the base library and converted OK cards"]
    fn edited_ok_card_is_made_again_and_reopens_from_ok_data() {
        let game = &crate::test_support::game_path("");
        let lib = PathBuf::from("C:/portable/OpenKoikatsu/Content/IndependentResources/base_v1");
        let cards = Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG");
        if !game.is_dir() || !cards.join("C001.png").is_file() { return; }
        let work = std::env::temp_dir().join(format!("ok-card-edit-{}", std::process::id()));
        std::fs::create_dir_all(&work).unwrap();
        let clothes = work.join("outfit.okclothes");
        crate::okclothes::file::export(&cards.join("C002.png"), 0, &clothes).unwrap();
        let worn = work.join("worn.png");
        crate::okclothes::file::wear_file(&cards.join("C001.png"), &clothes, 0, &worn).unwrap();
        assert!(is_library_card(&worn), "wearing keeps the OK chunks (stale until made again)");
        // Game mode of the edited card data.
        let plain = work.join("plain.png");
        std::fs::write(&plain, strip_ok_chunks(&std::fs::read(&worn).unwrap()).unwrap()).unwrap();
        deactivate(); crate::gamefs::deactivate();
        let live = session_digest(game, &plain).unwrap().0;
        // Made again, then reopened from the library and its own pack only.
        let (digest, _, bytes, _) = remake_ok_card(&lib, game, &worn).unwrap();
        let ok = work.join("ok.png");
        std::fs::write(&ok, &bytes).unwrap();
        assert!(png_has_chunk(&bytes, CARD_CHUNK) && !png_has_chunk(&bytes, b"okPv"));
        ensure_library(&lib).unwrap();
        crate::gamefs::set_fallback(Vec::new());
        let closed = session_digest(&lib, &ok).unwrap().0;
        eprintln!("edited OK card live={live} made={digest} closed={closed}");
        assert_eq!(digest, live, "made from the library + zipmods = game mode");
        assert_eq!(closed, live, "reopened from OK data = game mode");
        assert_eq!(library_card_digest(&ok).as_deref(), Some(live.as_str()));
        let _ = std::fs::remove_dir_all(&work);
    }
}

#[cfg(test)]
mod card_identity_tests {
    use super::*;
    fn chunk(kind: &[u8; 4], data: &[u8]) -> Vec<u8> {
        let mut c = kind.to_vec(); c.extend_from_slice(data);
        let mut out = (data.len() as u32).to_be_bytes().to_vec(); out.extend_from_slice(&c); out.extend_from_slice(&crc32fast::hash(&c).to_be_bytes()); out
    }
    fn png(parts: &[Vec<u8>], tail: &[u8]) -> Vec<u8> {
        let mut out = b"\x89PNG\r\n\x1a\n".to_vec();
        for p in parts { out.extend_from_slice(p); }
        out.extend_from_slice(&chunk(b"IEND", b"")); out.extend_from_slice(tail); out
    }
    /// A remade card keeps the identity it had; okVo does not count; strip_ok_chunks drops okId.
    #[test]
    fn identity_survives_a_new_pack() {
        let old = png(&[chunk(b"IHDR", &[0; 13]), chunk(CARD_CHUNK, b"{}"), chunk(PACK_CHUNK, b"old")], b"card data");
        let id = card_identity(&old).unwrap();
        assert_eq!(id.len(), 64);
        let voiced = png(&[chunk(b"IHDR", &[0; 13]), chunk(CARD_CHUNK, b"{}"), chunk(PACK_CHUNK, b"old"), chunk(b"okVo", b"voice")], b"card data");
        assert_eq!(card_identity(&voiced).unwrap(), id, "a voice chunk does not change her identity");
        let remade = png(&[chunk(b"IHDR", &[0; 13]), chunk(CARD_CHUNK, b"{\"v\":2}"), chunk(PACK_CHUNK, b"new pack")], b"card data");
        assert_ne!(card_identity(&remade).unwrap(), id);
        let kept = keep_identity(&old, remade).unwrap();
        assert_eq!(card_identity(&kept).unwrap(), id);
        let again = keep_identity(&kept, png(&[chunk(b"IHDR", &[0; 13]), chunk(CARD_CHUNK, b"{\"v\":3}")], b"card data")).unwrap();
        assert_eq!(card_identity(&again).unwrap(), id, "made again from a remade card");
        assert!(png_chunk(&strip_ok_chunks(&kept).unwrap(), IDENTITY_CHUNK).is_none());
        let original = png(&[chunk(b"IHDR", &[0; 13])], b"card data");
        assert_eq!(keep_identity(&original, old.clone()).unwrap(), old, "a first conversion has no identity to keep");
    }
}

#[cfg(test)]
mod concurrent_open_tests {
    /// Pack-carrying OK cards opened at once (each with its own PackScope) build exactly what they build one at a time,
    /// and faster. OK_PHYS_CARDS: a folder of OK cards (default CrowdAPNG), OK_OPEN_CARDS cards (default 16).
    ///   cargo test --release --lib concurrent_card_opens -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn concurrent_card_opens() {
        std::env::set_var("OK_NOCACHE", "1");
        if std::env::var("OK_BUILTIN_RESOURCES").is_err() { std::env::set_var("OK_BUILTIN_RESOURCES", "E:/OpenKoikatsu/Content/IndependentResources/v1"); }
        if std::env::var("OK_BASE_LIBRARY").is_err() { std::env::set_var("OK_BASE_LIBRARY", "E:/OpenKoikatsu/Content/IndependentResources/base_v1"); }
        let dir = std::env::var("OK_PHYS_CARDS").unwrap_or_else(|_| "E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG".into());
        let count: usize = std::env::var("OK_OPEN_CARDS").ok().and_then(|v| v.parse().ok()).unwrap_or(16);
        let mut cards: Vec<_> = std::fs::read_dir(&dir).unwrap().filter_map(|e| e.ok()).map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|x| x == "png") && !p.file_name().unwrap().to_string_lossy().starts_with('.')).collect();
        cards.sort();
        cards.truncate(count);
        let open = |card: &std::path::Path| -> Vec<u8> {
            let (game, path) = (std::ffi::CString::new("").unwrap(), std::ffi::CString::new(card.to_string_lossy().as_bytes()).unwrap());
            let mut out: *mut crate::session::OkSession = std::ptr::null_mut();
            let mut err = vec![0i8; 2048];
            let status = unsafe { crate::ffi::session_open(game.as_ptr(), path.as_ptr(), 0, &mut out, err.as_mut_ptr(), err.len() as i32) };
            assert!(status == 0 && !out.is_null(), "{}: open failed", card.display());
            let s = unsafe { Box::from_raw(out) };
            s.to_preview_bytes().expect("preview bytes")
        };
        // Warm the process (catalogs, mods tree) once, then one at a time.
        let _ = open(&cards[0]);
        let t = std::time::Instant::now();
        let serial: Vec<Vec<u8>> = cards.iter().map(|c| open(c)).collect();
        let serial_s = t.elapsed().as_secs_f64();
        let t = std::time::Instant::now();
        let parallel: Vec<Vec<u8>> = std::thread::scope(|scope| {
            let handles: Vec<_> = cards.iter().map(|c| scope.spawn(move || open(c))).collect();
            handles.into_iter().map(|h| h.join().unwrap()).collect()
        });
        let parallel_s = t.elapsed().as_secs_f64();
        let mut differ = Vec::new();
        for ((c, a), b) in cards.iter().zip(&serial).zip(&parallel) { if a != b { differ.push(c.file_name().unwrap().to_string_lossy().to_string()); } }
        println!("{} cards: one at a time {serial_s:.1} s ({:.2} s each), all at once {parallel_s:.1} s; differing: {differ:?}", cards.len(), serial_s / cards.len() as f64);
        assert!(differ.is_empty(), "opens at once differ from one at a time: {differ:?}");
    }
}

#[cfg(test)]
mod pack_record_tests {
    /// A pack record's bincode bytes are those of the plain tuple it replaced (cards written before read the same).
    #[test]
    fn pack_records_keep_the_tuple_encoding() {
        let id: super::Id = [7u8; 16];
        let old = vec![(id, "query".to_string(), vec![1u8, 2, 3], (0..1000u32).map(|v| v as u8).collect::<Vec<u8>>())];
        let new: Vec<super::PackRecord> = old.iter().map(|(a, b, c, d)| super::PackRecord(*a, b.clone(), c.clone(), d.clone())).collect();
        let (old_bytes, new_bytes) = (bincode::serialize(&old).unwrap(), bincode::serialize(&new).unwrap());
        assert_eq!(old_bytes, new_bytes);
        let back: Vec<super::PackRecord> = bincode::deserialize(&old_bytes).unwrap();
        assert_eq!((back[0].2.clone(), back[0].3.clone()), (old[0].2.clone(), old[0].3.clone()));
    }
}

#[cfg(test)]
mod open_timing_tests {
    /// How long one OK card opens without the open cache, one after another in one process (the first also warms the
    /// process: catalogs, the mods tree). OK_OPEN_NAMES: CrowdAPNG card names (default C009,C001,C049,C020).
    ///   OK_PROFILE=1 cargo test --release --lib crowd_card_open_seconds -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn crowd_card_open_seconds() {
        std::env::set_var("OK_NOCACHE", "1");
        if std::env::var("OK_BUILTIN_RESOURCES").is_err() { std::env::set_var("OK_BUILTIN_RESOURCES", "E:/OpenKoikatsu/Content/IndependentResources/v1"); }
        if std::env::var("OK_BASE_LIBRARY").is_err() { std::env::set_var("OK_BASE_LIBRARY", "E:/OpenKoikatsu/Content/IndependentResources/base_v1"); }
        let names = std::env::var("OK_OPEN_NAMES").unwrap_or_else(|_| "C009,C001,C049,C020".into());
        for name in names.split(',') {
            let card = format!("E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/{name}.png");
            let start = std::time::Instant::now();
            let (game, path) = (std::ffi::CString::new("").unwrap(), std::ffi::CString::new(card).unwrap());
            let mut out: *mut crate::session::OkSession = std::ptr::null_mut();
            let mut err = vec![0i8; 2048];
            let status = unsafe { crate::ffi::session_open(game.as_ptr(), path.as_ptr(), 0, &mut out, err.as_mut_ptr(), err.len() as i32) };
            eprintln!("OPEN {name}: status {status} {:.2} s", start.elapsed().as_secs_f64());
            if !out.is_null() { drop(unsafe { Box::from_raw(out) }); }
        }
    }
}

#[cfg(test)]
mod open_dump_tests {
    /// Each named CrowdAPNG card opened without the open cache, its session's bytes (to_preview_bytes) written to
    /// OK_DUMP_DIR/<name>.bin: two builds' opens compared byte for byte.
    #[test]
    #[ignore]
    fn dump_card_opens() {
        std::env::set_var("OK_NOCACHE", "1");
        if std::env::var("OK_BUILTIN_RESOURCES").is_err() { std::env::set_var("OK_BUILTIN_RESOURCES", "E:/OpenKoikatsu/Content/IndependentResources/v1"); }
        if std::env::var("OK_BASE_LIBRARY").is_err() { std::env::set_var("OK_BASE_LIBRARY", "E:/OpenKoikatsu/Content/IndependentResources/base_v1"); }
        let dir = std::path::PathBuf::from(std::env::var("OK_DUMP_DIR").expect("OK_DUMP_DIR"));
        std::fs::create_dir_all(&dir).unwrap();
        for name in std::env::var("OK_OPEN_NAMES").unwrap_or_else(|_| "C009,C001,C049,C020".into()).split(',') {
            let card = format!("E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/{name}.png");
            let (game, path) = (std::ffi::CString::new("").unwrap(), std::ffi::CString::new(card).unwrap());
            let mut out: *mut crate::session::OkSession = std::ptr::null_mut();
            let mut err = vec![0i8; 2048];
            let status = unsafe { crate::ffi::session_open(game.as_ptr(), path.as_ptr(), 0, &mut out, err.as_mut_ptr(), err.len() as i32) };
            assert_eq!(status, 0, "{name}");
            let s = unsafe { Box::from_raw(out) };
            std::fs::write(dir.join(format!("{name}.bin")), s.to_preview_bytes().unwrap()).unwrap();
        }
    }
}
