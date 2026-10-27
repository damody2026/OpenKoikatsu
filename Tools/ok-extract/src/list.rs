#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::collections::HashMap;
use std::io::Cursor;
use std::path::Path;

use rmpv::Value;
use unity_asset_binary::asset::parse_serialized_file;
use unity_asset_binary::bundle::load_bundle;
use unity_asset_decode::object::UnityObject;
use unity_asset_core::UnityValue;

const TEXT_ASSET: i32 = 49;

#[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
pub struct ListEntry {
    pub category_no: i32,
    pub id: i32,
    pub name: String,
    pub main_ab: String,
    pub main_data: String,
    pub main_tex: String,
    pub parent: String,
    #[serde(serialize_with="crate::ordered::map")]
    pub extra: HashMap<String, String>,
}

impl ListEntry {
    pub fn info(&self, key: &str) -> String {
        if key.eq_ignore_ascii_case("MainTex") && !self.main_tex.is_empty() {
            return self.main_tex.clone();
        }
        if key.eq_ignore_ascii_case("MainAB") && !self.main_ab.is_empty() {
            return self.main_ab.clone();
        }
        if key.eq_ignore_ascii_case("MainData") && !self.main_data.is_empty() {
            return self.main_data.clone();
        }
        if key.eq_ignore_ascii_case("Parent") && !self.parent.is_empty() {
            return self.parent.clone();
        }
        self.extra
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(key))
            .map(|(_, v)| v.clone())
            .unwrap_or_default()
    }
}

#[derive(Debug, Default, Clone)]
pub struct ChaListCatalog {
    /// (category_no, id) -> entry
    pub by_id: HashMap<(i32, i32), ListEntry>,
    /// OK_FIRSTLOAD=A3 layered view: `by_id` holds only what a mounted card pack's archives change, everything
    /// else is read from the catalogue the reader prewarmed (`load_mods_catalog_shared`).
    pub(crate) base: Option<std::sync::Arc<ChaListCatalog>>,
}

impl ChaListCatalog {
    pub fn get(&self, category_no: i32, id: i32) -> Option<&ListEntry> {
        self.by_id.get(&(category_no, id)).or_else(|| self.base.as_ref()?.by_id.get(&(category_no, id)))
    }

    /// Every entry once, the layered view included.
    pub fn entries(&self) -> impl Iterator<Item = &ListEntry> {
        self.by_id.values().chain(self.base.iter().flat_map(|base|
            base.by_id.iter().filter(|(k, _)| !self.by_id.contains_key(k)).map(|(_, e)| e)))
    }

    pub fn get_slot(&self, category_no: i32, slot: i32, local_slot: i32) -> Option<&ListEntry> {
        self.get(category_no, slot)
            .or_else(|| {
                if local_slot != 0 {
                    self.get(category_no, local_slot)
                } else {
                    None
                }
            })
            .or_else(|| {
                // Deterministic among several candidates (never HashMap order):
                // the slot id before the local id, then the lowest category.
                self.entries().filter(|e| {
                    e.id == slot || (local_slot != 0 && e.id == local_slot)
                }).min_by_key(|e| (e.id != slot, e.category_no, e.id))
            })
    }

    pub fn insert_all(&mut self, entries: Vec<ListEntry>) {
        for e in entries {
            self.by_id.entry((e.category_no, e.id)).or_insert(e);
        }
    }
}

pub fn catalog_from_zipmod(path: &Path) -> ChaListCatalog {
    let mut cat = ChaListCatalog::default();
    let zip = path.to_string_lossy().into_owned();
    for (name, bytes) in crate::mods::zipmod_list_files(path) {
        let mut entries = if name.to_ascii_lowercase().ends_with(".unity3d") {
            let mut es = Vec::new();
            for (_n, script) in text_assets_from_bytes(&bytes) {
                es.extend(load_entries_from_bytes(&_n, &script));
            }
            es
        } else {
            load_entries_from_bytes(&name, &bytes)
        };
        // Tag the source zipmod so texture lookup (`png_from_list_entry`) can
        // find mod textures even when the prefab material leaves _MainTex empty.
        for e in entries.iter_mut() {
            e.extra.insert("__zip".to_string(), zip.clone());
        }
        cat.insert_all(entries);
    }
    cat
}

pub(crate) fn text_assets_from_bytes(bundle_bytes: &[u8]) -> Vec<(String, Vec<u8>)> {
    crate::oklib::memo_bytes(bundle_bytes, "text_assets", &(), || text_assets_from_bytes_unity(bundle_bytes))
}
fn text_assets_from_bytes_unity(bundle_bytes: &[u8]) -> Vec<(String, Vec<u8>)> {
    let Ok(bundle) = crate::unity::load_bundle_unbounded(bundle_bytes.to_vec()) else {
        return Vec::new();
    };
    let _ = bundle.data_checked();
    let mut out = Vec::new();
    for file in &bundle.files {
        let Ok(file_bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(serialized) = parse_serialized_file(file_bytes) else {
            continue;
        };
        for info in serialized.objects_of_type(TEXT_ASSET) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else {
                continue;
            };
            if let Some(parsed) = parse_text_asset_raw(object.raw_data()) {
                if !parsed.1.is_empty() {
                    out.push(parsed);
                }
            }
        }
    }
    out
}

fn align4(n: usize) -> usize {
    (n + 3) & !3
}

fn read_u32_le(data: &[u8], pos: usize) -> Option<u32> {
    data.get(pos..pos + 4)?.try_into().ok().map(u32::from_le_bytes)
}

fn parse_text_asset_raw(data: &[u8]) -> Option<(String, Vec<u8>)> {
    let mut pos = 0usize;
    let name_len = read_u32_le(data, pos)? as usize;
    pos += 4;
    if pos + name_len > data.len() {
        return None;
    }
    let name = String::from_utf8_lossy(&data[pos..pos + name_len]).into_owned();
    pos = align4(pos + name_len);
    let script_len = read_u32_le(data, pos)? as usize;
    pos += 4;
    if pos + script_len > data.len() {
        return None;
    }
    let script = data[pos..pos + script_len].to_vec();
    Some((name, script))
}

fn value_bytes(v: &UnityValue) -> Option<Vec<u8>> {
    match v {
        UnityValue::Bytes(b) => Some(b.clone()),
        UnityValue::String(s) => Some(s.as_bytes().to_vec()),
        UnityValue::Array(arr) => {
            let mut out = Vec::with_capacity(arr.len());
            for item in arr {
                match item {
                    UnityValue::Integer(n) => out.push(*n as u8),
                    UnityValue::String(s) => out.extend(s.as_bytes()),
                    _ => {}
                }
            }
            if out.is_empty() {
                None
            } else {
                Some(out)
            }
        }
        _ => None,
    }
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

fn column_index(keys: &[String], name: &str) -> Option<usize> {
    keys.iter().position(|k| k.eq_ignore_ascii_case(name))
}

fn row_str(row: &[Value], keys: &[String], name: &str) -> String {
    column_index(keys, name)
        .and_then(|i| row.get(i))
        .map(as_str)
        .unwrap_or_default()
}

fn row_i32(row: &[Value], keys: &[String], name: &str) -> i32 {
    column_index(keys, name)
        .and_then(|i| row.get(i))
        .map(as_i32)
        .unwrap_or(0)
}

pub fn parse_chalist(bytes: &[u8]) -> Option<(i32, Vec<ListEntry>)> {
    let val = rmpv::decode::read_value(&mut Cursor::new(bytes)).ok()?;
    let mark = map_get(&val, "mark").map(as_str).unwrap_or_default();
    if !mark.contains("ChaListData") {
        return None;
    }
    let category_no = map_get(&val, "categoryNo").map(as_i32).unwrap_or(0);
    let keys: Vec<String> = map_get(&val, "lstKey")
        .and_then(|v| v.as_array())
        .map(|a| a.iter().map(as_str).collect())
        .unwrap_or_default();
    let mut entries = Vec::new();
    if let Some(dict) = map_get(&val, "dictList").and_then(|v| v.as_map()) {
        for (k, row_v) in dict {
            let id = k.as_i64().unwrap_or(0) as i32;
            let row = match row_v.as_array() {
                Some(a) => a.clone(),
                None => continue,
            };
            let id = if id != 0 { id } else { row_i32(&row, &keys, "ID") };
            let mut extra = HashMap::new();
            for k in &keys {
                let v = row_str(&row, &keys, k);
                if !v.is_empty() {
                    extra.insert(k.clone(), v);
                }
            }
            entries.push(ListEntry {
                category_no,
                id,
                name: row_str(&row, &keys, "Name"),
                main_ab: row_str(&row, &keys, "MainAB"),
                main_data: row_str(&row, &keys, "MainData"),
                main_tex: row_str(&row, &keys, "MainTex"),
                parent: row_str(&row, &keys, "Parent"),
                extra,
            });
        }
    }
    Some((category_no, entries))
}

pub fn text_assets_from_bundle(path: &Path) -> Vec<(String, Vec<u8>)> {
    text_assets_from_bundle_status(path).0
}

fn text_assets_from_bundle_status(path:&Path)->(Vec<(String,Vec<u8>)>,bool){
    let Ok(bytes) = crate::gamefs::read(path) else { return (Vec::new(),false); };
    // Not recording a library: the bytes go to the loader as they are (no second copy).
    if !crate::oklib::memo_active() { return text_assets_from_bundle_status_unity(bytes); }
    crate::oklib::memo_bytes(&bytes, "text_assets_status", &(), || text_assets_from_bundle_status_unity(bytes.clone()))
}
/// OK base library conversion: the same recorded query from bytes.
pub(crate) fn text_assets_status_from_bytes(bytes: &[u8]) {
    let _ = crate::oklib::memo_bytes(bytes, "text_assets_status", &(), || text_assets_from_bundle_status_unity(bytes.to_vec()));
}
fn text_assets_from_bundle_status_unity(bytes:Vec<u8>)->(Vec<(String,Vec<u8>)>,bool){
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle_from_memory(bytes) else {
        return (Vec::new(),false);
    };
    let mut complete = bundle.data_checked().is_ok();
    // Files parsed in place in the decompressed bundle (copying each out held it twice).
    let shared = bundle.data_shared().ok();
    let mut out = Vec::new();
    for file in &bundle.files {
        let serialized = match &shared {
            Some(data) => usize::try_from(file.offset).ok().zip(file.offset.checked_add(file.size).and_then(|end| usize::try_from(end).ok()))
                .ok_or(()).and_then(|(start, end)| unity_asset_binary::asset::SerializedFileParser::from_shared_range(data.clone(), start..end).map_err(|_| ())),
            None => bundle.extract_file_data(file).map_err(|_| ()).and_then(|file_bytes| parse_serialized_file(file_bytes).map_err(|_| ())),
        };
        let Ok(serialized) = serialized else {
            complete=false;
            continue;
        };
        for info in serialized.objects_of_type(TEXT_ASSET) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else {
                complete=false;
                continue;
            };
            let props = object.class.properties();
            let from_tree = props.get("m_Name").and_then(|v| v.as_str()).map(|n| n.to_string())
                .and_then(|name| props.get("m_Script").and_then(value_bytes).map(|s| (name, s)));
            let parsed = from_tree.or_else(|| parse_text_asset_raw(object.raw_data()));
            let Some((name, script)) = parsed else {
                complete=false;
                continue;
            };
            if script.is_empty() {
                continue;
            }
            out.push((name, script));
        }
    }
    (out,complete)
}

pub fn parse_csv_list(text: &str, fallback_category: i32) -> Vec<ListEntry> {
    // Mod list CSVs are often written with a UTF-8 BOM; strip it or the leading
    // category-number line fails to parse and every entry lands in category 0.
    let text = text.trim_start_matches('\u{feff}');
    let raw: Vec<&str> = text
        .lines()
        .map(|l| l.trim())
        .filter(|t| !t.is_empty() && !t.starts_with('#'))
        .collect();
    if raw.is_empty() {
        return Vec::new();
    }
    let mut category_no = fallback_category;
    let mut header_idx = 0usize;
    // The first line may be a bare category number, possibly padded with empty
    // columns (`106,,,,`). Detect it from the first comma-separated cell so both
    // `108` and `106,,,` are accepted; otherwise the header itself is row 0.
    let first_cell = raw[0].split(',').next().unwrap_or("").trim();
    if first_cell.parse::<i32>().is_ok() {
        category_no = first_cell.parse().unwrap_or(fallback_category);
        header_idx = raw
            .iter()
            .position(|l| l.contains("ID") && l.contains(','))
            .unwrap_or(0);
    }
    let Some(header) = raw.get(header_idx) else {
        return Vec::new();
    };
    let cols: Vec<String> = header.split(',').map(|s| s.trim().to_string()).collect();
    let idx = |name: &str| cols.iter().position(|c| c.eq_ignore_ascii_case(name));
    let Some(id_i) = idx("ID") else {
        return Vec::new();
    };
    let name_i = idx("Name");
    let ab_i = idx("MainAB");
    let data_i = idx("MainData");
    let tex_i = idx("MainTex");
    let cat_i = idx("Category")
        .or_else(|| idx("categoryNo"))
        .or_else(|| idx("CategoryNo"));
    let mut out = Vec::new();
    for line in raw.iter().skip(header_idx + 1) {
        if line.parse::<i32>().is_ok() && !line.contains(',') {
            continue;
        }
        if !line.contains(',') {
            continue;
        }
        let cells: Vec<&str> = line.split(',').map(|s| s.trim()).collect();
        if cells.get(id_i).map(|s| s.eq_ignore_ascii_case("ID")).unwrap_or(false) {
            continue;
        }
        let id: i32 = cells.get(id_i).and_then(|s| s.parse().ok()).unwrap_or(0);
        if id == 0 && cells.get(id_i).map(|s| *s != "0").unwrap_or(true) {
            continue;
        }
        let category_no = cat_i
            .and_then(|i| cells.get(i))
            .and_then(|s| s.parse().ok())
            .unwrap_or(category_no);
        let parent_i = idx("Parent");
        let mut extra = HashMap::new();
        for (i, col) in cols.iter().enumerate() {
            if let Some(v) = cells.get(i) {
                if !v.is_empty() {
                    extra.insert(col.clone(), (*v).to_string());
                }
            }
        }
        out.push(ListEntry {
            category_no,
            id,
            name: name_i.and_then(|i| cells.get(i)).unwrap_or(&"").to_string(),
            main_ab: ab_i.and_then(|i| cells.get(i)).unwrap_or(&"").to_string(),
            main_data: data_i.and_then(|i| cells.get(i)).unwrap_or(&"").to_string(),
            main_tex: tex_i.and_then(|i| cells.get(i)).unwrap_or(&"").to_string(),
            parent: parent_i.and_then(|i| cells.get(i)).unwrap_or(&"").to_string(),
            extra,
        });
    }
    out
}

pub fn load_entries_from_bytes(name: &str, bytes: &[u8]) -> Vec<ListEntry> {
    if name.to_ascii_lowercase().ends_with(".csv") {
        let text = String::from_utf8_lossy(bytes);
        return parse_csv_list(&text, 0);
    }
    if let Some((_cat, entries)) = parse_chalist(bytes) {
        return entries;
    }
    Vec::new()
}

mod catalog_cache;

pub(crate) fn load_characustom_dir_shared(dir:&Path)->std::sync::Arc<ChaListCatalog>{
    static CACHE:std::sync::OnceLock<std::sync::Mutex<catalog_cache::Cache>>=std::sync::OnceLock::new();
    match CACHE.get_or_init(Default::default).lock(){
        Ok(mut cache)=>cache.get(dir,load_characustom_dir_uncached),
        // A poisoned cache is never a reason to return stale or empty data.
        Err(_)=>std::sync::Arc::new(load_characustom_dir_uncached(dir).0),
    }
}

pub fn load_characustom_dir(dir: &Path) -> ChaListCatalog {
    (*load_characustom_dir_shared(dir)).clone()
}

fn load_characustom_dir_uncached(dir: &Path) -> (ChaListCatalog,bool) {
    let mut cat = ChaListCatalog::default();
    let Ok(rd) = crate::gamefs::read_dir(dir) else {
        return (cat,false);
    };
    let mut complete=true;
    for entry in rd {
        let Ok(entry)=entry else{complete=false;continue};
        let path = entry.path();
        if path
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.eq_ignore_ascii_case("unity3d"))
            .unwrap_or(false)
        {
            let (assets,ok)=text_assets_from_bundle_status(&path);complete&=ok;
            for (_name, bytes) in assets {
                if let Some((_cat, entries)) = parse_chalist(&bytes) {
                    for e in entries {
                        cat.by_id.entry((e.category_no, e.id)).or_insert(e);
                    }
                }else{complete=false;}
            }
        }
    }
    (cat,complete)
}

/// Signature of a mods tree's zipmod list (paths, sizes, times), for per-process memos.
pub(crate) fn zip_list_sig(mods_dir: &Path) -> u64 {
    if let Some(tree) = crate::mods::watched_tree_zips(mods_dir) { return tree.1; }
    // A replaying library's recorded tree only changes with the mounted card pack: walking and stating its ~7000
    // archives for every memo key (several per open) cost ~0.1 s each time.
    if crate::oklib::active().is_some_and(|s| s.mode == crate::oklib::Mode::Replay) && mods_dir.canonicalize().is_err() {
        use std::sync::Mutex;
        static MEMO: Mutex<Vec<(std::path::PathBuf, String, u64)>> = Mutex::new(Vec::new());
        let pack = crate::oklib::mounted_pack_identity();
        if let Some((_, _, sig)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(d, p, _)| d == mods_dir && *p == pack) { return *sig; }
        let mut zips = crate::mods::all_zipmods(mods_dir);
        zips.sort();
        let sig = catalog_sig(&zips);
        let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
        if memo.len() >= 32 { memo.remove(0); }
        memo.push((mods_dir.to_path_buf(), pack, sig));
        return sig;
    }
    let mut zips = crate::mods::all_zipmods(mods_dir);
    zips.sort();
    catalog_sig(&zips)
}

pub(crate) fn catalog_sig(zips: &[std::path::PathBuf]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for p in zips {
        let s = p.to_string_lossy();
        for b in s.as_bytes() {
            h ^= *b as u64;
            h = h.wrapping_mul(0x100000001b3);
        }
        if let Ok(m) = crate::gamefs::metadata(p) {
            h ^= m.len();
            h = h.wrapping_mul(0x100000001b3);
            if let Ok(t) = m.modified() {
                if let Ok(d) = t.duration_since(std::time::UNIX_EPOCH) {
                    h ^= d.as_secs();
                    h = h.wrapping_mul(0x100000001b3);
                }
            }
        }
    }
    h
}

fn value_clean(s: &str) -> String {
    s.replace('|', "/").replace(['\n', '\r', '\t'], " ")
}

fn serialize_catalog(cat: &ChaListCatalog) -> String {
    let mut s = String::new();
    for ((c, id), e) in &cat.by_id {
        let extra: Vec<String> = e
            .extra
            .iter()
            .map(|(k, v)| format!("{}={}", value_clean(k), value_clean(v)))
            .collect();
        s.push_str(&format!(
            "{}|{}|{}|{}|{}|{}|{}|{}\n",
            c,
            id,
            value_clean(&e.name),
            value_clean(&e.main_ab),
            value_clean(&e.main_data),
            value_clean(&e.main_tex),
            value_clean(&e.parent),
            extra.join(";")
        ));
    }
    s
}

pub(crate) fn catalog_from_cache_tsv(path: &std::path::Path) -> Option<ChaListCatalog> {
    let text = crate::gamefs::read_to_string(path).ok()?;
    Some(parse_catalog(&text))
}

fn parse_catalog(text: &str) -> ChaListCatalog {
    let mut cat = ChaListCatalog::default();
    for line in text.lines() {
        let f: Vec<&str> = line.split('|').collect();
        if f.len() < 7 {
            continue;
        }
        let Ok(c) = f[0].parse::<i32>() else { continue };
        let Ok(id) = f[1].parse::<i32>() else { continue };
        let mut extra = HashMap::new();
        for kv in f[7].split(';') {
            if let Some((k, v)) = kv.split_once('=') {
                if !k.is_empty() {
                    extra.insert(k.to_string(), v.to_string());
                }
            }
        }
        cat.by_id.entry((c, id)).or_insert(ListEntry {
            category_no: c,
            id,
            name: f[2].to_string(),
            main_ab: f[3].to_string(),
            main_data: f[4].to_string(),
            main_tex: f[5].to_string(),
            parent: f[6].to_string(),
            extra,
        });
    }
    cat
}

/// All character-custom lists from every installed zipmod (cached on disk).
/// Needed to resolve mod-only ids (face textures, clothes, hair) that are not
/// referenced by the card's KKEx resolves.
/// load_mods_catalog without a copy per call: the SQL catalogue as it is held, or one merged catalogue per mods tree
/// (zip list, mounted card pack) kept for the process. A card open used to copy the full catalogue (~0.8 s).
pub fn load_mods_catalog_shared(mods_dir: &Path) -> std::sync::Arc<ChaListCatalog> {
    use std::sync::{Arc, Mutex};
    if let Some(cat) = crate::sqlcache::mods_catalog_shared(mods_dir) {
        return cat;
    }
    let mut zips = crate::mods::all_zipmods(mods_dir);
    zips.sort();
    let key = (mods_dir.to_path_buf(), catalog_sig(&zips), crate::oklib::mounted_pack_mods_identity());
    static MEMO: Mutex<Vec<((std::path::PathBuf, u64, String), Arc<Vec<std::path::PathBuf>>, Arc<ChaListCatalog>)>> = Mutex::new(Vec::new());
    use crate::memosnap::{RES,get,put};
    let memo_key = format!("catalog\n{:?}", key);
    let record = |cat: &ChaListCatalog| put(RES, memo_key.as_bytes(), || {
        let mut list: Vec<((i32, i32), &ListEntry)> = cat.entries().map(|e| ((e.category_no, e.id), e)).collect();
        list.sort_by_key(|e| e.0);
        bincode::serialize(&list).ok()
    });
    if let Some((_, _, hit)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _, _)| *k == key) {
        record(hit);
        return Arc::clone(hit);
    }
    // Replayed from the card (OK_CARD_MEMO=res).
    let replayed = get(RES, memo_key.as_bytes()).and_then(|b| bincode::deserialize::<Vec<((i32, i32), ListEntry)>>(&b).ok())
        .map(|list| ChaListCatalog { by_id: list.into_iter().collect(), base: None });
    let layered = if replayed.is_none() && crate::first_load_option("A3") && crate::oklib::active().is_some() {
        let memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
        memo.iter().rev().filter(|(k, _, c)| k.0 == key.0 && c.base.is_none())
            .find(|(_, base_zips, _)| base_zips.iter().all(|z| zips.binary_search(z).is_ok()))
            .map(|(_, base_zips, base)| (Arc::clone(base_zips), Arc::clone(base)))
    } else { None };
    let layered = layered.and_then(|(base_zips, base)| layered_catalog(&zips, &base_zips, base));
    let cat = Arc::new(replayed.or(layered).unwrap_or_else(|| view_catalog(mods_dir, &zips).unwrap_or_else(|| load_mods_catalog(mods_dir))));
    record(&cat);
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    if memo.len() >= 8 { memo.remove(0); }
    memo.push((key, Arc::new(zips), Arc::clone(&cat)));
    cat
}

/// OK_FIRSTLOAD=A3: a mounted card pack adds a few archives to the prewarmed view. Merging the whole view again
/// cloned every entry (~0.2 s on a card's first open); keep the prewarmed catalogue and hold only the entries an
/// added archive wins. The first archive in sorted path order still wins a list id (`view_catalog`).
fn layered_catalog(zips: &[std::path::PathBuf], base_zips: &[std::path::PathBuf], base: std::sync::Arc<ChaListCatalog>) -> Option<ChaListCatalog> {
    let t = std::time::Instant::now();
    let added: Vec<&std::path::PathBuf> = zips.iter().filter(|z| base_zips.binary_search(z).is_err()).collect();
    let parts: Vec<std::sync::Arc<ChaListCatalog>> = added.iter().map(|zip| tagged_zip_catalog(zip)).collect();
    let by_id = layer_entries(&base, added.iter().map(|z| z.as_path()).zip(parts.iter().map(|p| &**p)))?;
    if std::env::var_os("OK_PROFILE").is_some() {
        eprintln!("ok-extract: layered catalog added_zips={} entries={} ms={:.1}", added.len(), by_id.len(), t.elapsed().as_secs_f64() * 1000.0);
    }
    Some(ChaListCatalog { by_id, base: Some(base) })
}

/// The entries the added archives (in sorted path order, each with its own list) win over `base`, the merged view of
/// the other archives: an id `base` lacks, or one whose base winner's archive sorts after the added one. None when a
/// base entry has no archive tag to compare.
fn layer_entries<'a>(base: &ChaListCatalog, added: impl Iterator<Item = (&'a Path, &'a ChaListCatalog)>) -> Option<HashMap<(i32, i32), ListEntry>> {
    let mut by_id: HashMap<(i32, i32), ListEntry> = HashMap::new();
    for (zip, part) in added {
        for (k, e) in part.by_id.iter() {
            if by_id.contains_key(k) { continue; }
            match base.by_id.get(k) {
                Some(winner) => { if zip < Path::new(winner.extra.get("__zip")?) { by_id.insert(*k, e.clone()); } }
                None => { by_id.insert(*k, e.clone()); }
            }
        }
    }
    Some(by_id)
}

/// One zipmod's list entries, each tagged with its archive (`__zip`).
fn tagged_zip_catalog(p: &Path) -> std::sync::Arc<ChaListCatalog> {
    crate::gamefs::zip_memo("catalog_tagged", p, || {
        let mut part = catalog_from_zipmod(p);
        for e in part.by_id.values_mut() { e.extra.insert("__zip".to_string(), p.to_string_lossy().into_owned()); }
        std::sync::Arc::new(part)
    })
}

/// A library session's mods view (the recorded tree, as a mounted card pack lists it): every archive's tagged
/// entries are remembered (`tagged_zip_catalog`), so a view only merges them, cloning each winning entry once. The
/// first archive in sorted path order wins a list id, as in `load_mods_catalog`. Cloning every archive's whole list
/// first, as that does, cost ~0.5 s for each card whose pack changes the view. None outside a library session.
fn view_catalog(_mods_dir: &Path, zips: &[std::path::PathBuf]) -> Option<ChaListCatalog> {
    if crate::oklib::active().is_none() { return None; }
    let t = std::time::Instant::now();
    // Archives the replaying library records cannot change: their tagged lists are kept by path (one index
    // lookup for all of them instead of a `zip_memo` round per archive).
    use std::sync::Mutex;
    static RECORDED: Mutex<Option<HashMap<std::path::PathBuf, std::sync::Arc<ChaListCatalog>>>> = Mutex::new(None);
    let recorded = crate::gamefs::library_records_zips(zips);
    let known: Vec<Option<std::sync::Arc<ChaListCatalog>>> = {
        let map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        zips.iter().zip(&recorded).map(|(p, r)| if *r { map.as_ref().and_then(|m| m.get(p).cloned()) } else { None }).collect()
    };
    let parts: Vec<std::sync::Arc<ChaListCatalog>> = {
        use rayon::prelude::*;
        crate::oklib::pack_parallel(|| zips.par_iter().zip(known.into_par_iter()).map(|(p, k)| k.unwrap_or_else(|| tagged_zip_catalog(p))).collect())
    };
    {
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        let map = map.get_or_insert_with(HashMap::new);
        for ((p, r), part) in zips.iter().zip(&recorded).zip(&parts) { if *r { map.entry(p.clone()).or_insert_with(|| part.clone()); } }
    }
    let read = t.elapsed();
    let mut cat = ChaListCatalog::default();
    cat.by_id.reserve(parts.iter().map(|p| p.by_id.len()).max().unwrap_or(0) * 4);
    for part in &parts { for (k, e) in part.by_id.iter() { cat.by_id.entry(*k).or_insert_with(|| e.clone()); } }
    if std::env::var_os("OK_PROFILE").is_some() {
        eprintln!("ok-extract: view catalog zips={} entries={} parts_ms={:.1} merge_ms={:.1}", zips.len(), cat.by_id.len(),
            read.as_secs_f64() * 1000.0, (t.elapsed() - read).as_secs_f64() * 1000.0);
    }
    Some(cat)
}

pub fn load_mods_catalog(mods_dir: &Path) -> ChaListCatalog {
    if let Some(cat) = crate::sqlcache::mods_catalog(mods_dir) {
        return cat;
    }
    let mut zips = crate::mods::all_zipmods(mods_dir);
    zips.sort();
    let sig = catalog_sig(&zips);
    let cache = mods_dir.join(format!(".ok_list_cache_v2_{sig:016x}.tsv"));
    if let Ok(text) = crate::gamefs::read_to_string(&cache) {
        return parse_catalog(&text);
    }
    let mut cat = ChaListCatalog::default();
    for p in &zips {
        // Library sessions parse each recorded archive's lists once (a card's own zipmods every time).
        let mut part = (*crate::gamefs::library_zip_memo("catalog", p, || std::sync::Arc::new(catalog_from_zipmod(p)))).clone();
        for e in part.by_id.values_mut() {
            e.extra
                .insert("__zip".to_string(), p.to_string_lossy().into_owned());
        }
        for (k, e) in part.by_id {
            cat.by_id.entry(k).or_insert(e);
        }
    }
    let _ = std::fs::write(&cache, serialize_catalog(&cat));
    cat
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    /// OK_FIRSTLOAD=A3: the prewarmed view plus `layer_entries` answers every lookup (get, get_slot's fallbacks) as
    /// the full merge of all archives does (first archive in sorted path order wins), whatever archives the card adds.
    #[test]
    fn layered_view_equals_full_merge() {
        let mut seed = 0x9e3779b97f4a7c15u64;
        let mut next = |n: u64| { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; seed % n };
        for round in 0..200 {
            let count = 2 + next(8) as usize;
            let mut zips: Vec<PathBuf> = (0..count).map(|i| PathBuf::from(format!("C:/mods/{}/{:02}.zipmod", ["a", "b", "c"][next(3) as usize], i))).collect();
            zips.sort(); zips.dedup();
            let parts: Vec<ChaListCatalog> = zips.iter().map(|zip| {
                let mut part = ChaListCatalog::default();
                for _ in 0..next(12) {
                    let (c, id) = (100 + next(3) as i32, next(10) as i32);
                    let mut e = ListEntry { category_no: c, id, name: format!("{}-{c}-{id}", zip.display()), ..Default::default() };
                    e.extra.insert("__zip".into(), zip.to_string_lossy().into_owned());
                    part.by_id.entry((c, id)).or_insert(e);
                }
                part
            }).collect();
            let merge = |pick: &dyn Fn(usize) -> bool| {
                let mut cat = ChaListCatalog::default();
                for (i, part) in parts.iter().enumerate() { if pick(i) { for (k, e) in &part.by_id { cat.by_id.entry(*k).or_insert_with(|| e.clone()); } } }
                cat
            };
            let added: Vec<usize> = (0..zips.len()).filter(|_| next(3) == 0).collect();
            let full = merge(&|_| true);
            let base = std::sync::Arc::new(merge(&|i| !added.contains(&i)));
            let layer = layer_entries(&base, added.iter().map(|&i| (zips[i].as_path(), &parts[i]))).expect("tagged base");
            let view = ChaListCatalog { by_id: layer, base: Some(base) };
            for c in 99..104 { for id in -1..11 {
                assert_eq!(view.get(c, id).map(|e| &e.name), full.get(c, id).map(|e| &e.name), "round {round} get {c} {id}");
                for local in [0, 3] {
                    assert_eq!(view.get_slot(c, id, local).map(|e| &e.name), full.get_slot(c, id, local).map(|e| &e.name), "round {round} slot {c} {id} {local}");
                }
            } }
            assert_eq!(view.entries().count(), full.by_id.len(), "round {round} entries");
        }
    }

    #[test]
    fn dump_cus_pose() {
        let p = crate::test_support::game_path(r"\abdata\custom\customscenelist\00.unity3d");
        for (name, bytes) in text_assets_from_bundle(&p) {
            let s = String::from_utf8_lossy(&bytes);
            eprintln!(
                "ok-extract: asset name={name} len={} text_preview={}",
                bytes.len(),
                s.chars().take(300).collect::<String>().replace('\n', "|")
            );
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_face_categories() {
        let dir = crate::test_support::game_path(r"\abdata\list\characustom");
        if !dir.ok_is_dir() {
            return;
        }
        let cat = load_characustom_dir(&dir);
        let mut counts = std::collections::BTreeMap::<i32, usize>::new();
        for ((c, _), _) in &cat.by_id {
            *counts.entry(*c).or_insert(0) += 1;
        }
        eprintln!("ok-extract: characustom total={} categories={:?}", cat.by_id.len(), counts);
        for c in [406, 407, 408, 409, 412, 413, 414, 415] {
            let e = cat.by_id.values().find(|e| e.category_no == c);
            eprintln!(
                "ok-extract: cat {c} present={} sample={:?}",
                e.is_some(),
                e.map(|e| (e.id, e.main_ab.clone(), e.main_tex.clone()))
            );
            if let Some(e) = e {
                eprintln!("ok-extract: cat {c} extra={:?}", e.extra);
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_face_tex_names() {
        let p = crate::test_support::game_path(r"\abdata\chara\mt_eyebrow_00.unity3d");
        let bytes = crate::gamefs::read(&p).expect("read eyebrow ab");
        let bundle = unity_asset_binary::bundle::load_bundle_from_memory(bytes).expect("bundle");
        let _ = bundle.data_checked();
        let mut names = Vec::new();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(fb) else { continue };
            for info in ser.objects_of_type(28) {
                if let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info) {
                    names.push(obj.name().unwrap_or_default());
                }
            }
        }
        eprintln!("ok-extract: eyebrow TEX count={} names={:?}", names.len(), names);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_chalist_sample() {
        let path = crate::test_support::game_path(r"\abdata\list\characustom\00.unity3d");
        assert!(path.ok_exists(), "missing {path:?}");
        let bundle = unity_asset_binary::bundle::load_bundle(&path).expect("load bundle");
        let _ = bundle.data_checked();
        let mut class_ids = std::collections::BTreeMap::<i32, usize>::new();
        let mut names = Vec::new();
        for file in &bundle.files {
            names.push(file.name.clone());
            let Ok(file_bytes) = bundle.extract_file_data(file) else {
                continue;
            };
            let Ok(serialized) = parse_serialized_file(file_bytes) else {
                continue;
            };
            for info in &serialized.objects {
                *class_ids.entry(info.type_id).or_insert(0) += 1;
            }
        }
        eprintln!("files={names:?} class_ids={class_ids:?}");
        let assets = text_assets_from_bundle(&path);
        assert!(
            !assets.is_empty(),
            "no text assets files={names:?} class_ids={class_ids:?}"
        );
        let mut reports = Vec::new();
        for (name, bytes) in assets.iter().take(8) {
            let parsed = parse_chalist(bytes);
            reports.push(format!(
                "{name} bytes={} parsed={}",
                bytes.len(),
                parsed
                    .as_ref()
                    .map(|(c, e)| format!("cat={c} n={}", e.len()))
                    .unwrap_or_else(|| "no".into())
            ));
            if parsed.is_none() {
                let preview: String = bytes.iter().take(80).map(|b| {
                    if b.is_ascii_graphic() || *b == b' ' { *b as char } else { '.' }
                }).collect();
                reports.push(format!("  preview={preview}"));
                if let Ok(v) = rmpv::decode::read_value(&mut Cursor::new(bytes.as_slice())) {
                    reports.push(format!("  rmpv={}", v.to_string().chars().take(240).collect::<String>()));
                }
            }
        }
        std::fs::create_dir_all(r"C:\portable\OpenKoikatsu\Saved\spike").ok();
        std::fs::write(
            r"C:\portable\OpenKoikatsu\Saved\spike\chalist_dump.txt",
            reports.join("\n"),
        )
        .ok();
        eprintln!("{}", reports.join("\n"));
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_all_categories_and_card() {
        let dir = crate::test_support::game_path(r"\abdata\list\characustom");
        let cat = load_characustom_dir(&dir);
        let mut by_cat: std::collections::BTreeMap<i32, Vec<String>> = std::collections::BTreeMap::new();
        for ((c, id), e) in &cat.by_id {
            if id == &0 || id == &1 {
                by_cat.entry(*c).or_default().push(format!("id={id} {} ab={} data={}", e.name, e.main_ab, e.main_data));
            }
        }
        let mut lines = vec![format!("entries={}", cat.by_id.len())];
        for (c, rows) in &by_cat {
            lines.push(format!("cat {c}: {}", rows.join(" | ")));
        }
        let card_path = crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if card_path.ok_exists() {
            let card = crate::card::parse_card(&card_path).expect("card");
            lines.push(format!(
                "card head={} hair={:?} clothes={:?} shapeF={} shapeB={} acc={} uar={}",
                card.head_id,
                card.hair_ids,
                card.clothes_ids,
                card.shape_face.len(),
                card.shape_body.len(),
                card.accessory.len(),
                card.resolves.len()
            ));
            for r in card.resolves.iter().take(40) {
                lines.push(format!(
                    "  uar cat={} slot={} local={} prop={} guid={} name={}",
                    r.category_no, r.slot, r.local_slot, r.property, r.mod_id, r.name
                ));
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\chalist_cats.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
        assert!(!cat.by_id.is_empty());
    }

    #[test]
    fn parse_sideloader_csv_category_then_path() {
        let text = "210\n0\nAssets/Illusion/assetbundle/chara/list/characustom/00/cpo_jacket_a_00.bytes\nID,Kind,Possess,Name,MainManifest,MainAB,MainData,MainTex\n127,0,1,Wedding Dress(Inner),abdata,chara/inner_wedding_dress.unity3d,p_o_top_inner06,cf_top_inner06_t\n";
        let entries = parse_csv_list(text, 0);
        assert_eq!(entries.len(), 1, "{entries:?}");
        assert_eq!(entries[0].category_no, 210);
        assert_eq!(entries[0].id, 127);
        assert_eq!(entries[0].main_data, "p_o_top_inner06");
        assert_eq!(entries[0].main_tex, "cf_top_inner06_t");
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn zipmod_csv_slot_508() {
        let path = crate::test_support::game_path(r"\mods\Sideloader Modpack\yamadamod\[yamadamod]nohairB rev8.zipmod");
        if !path.ok_exists() {
            return;
        }
        let cat = catalog_from_zipmod(&path);
        let e = cat.get(101, 508);
        assert!(e.is_some(), "keys={:?}", cat.by_id.keys().take(20).collect::<Vec<_>>());
        let e = e.unwrap();
        eprintln!("entry ab={} data={}", e.main_ab, e.main_data);
        let bytes = crate::mods::zipmod_bundle_named(&path, &e.main_ab).expect("bundle");
        let meshes = crate::unity::meshes_from_memory(bytes.to_vec());
        eprintln!("meshes={:?}", meshes.iter().map(|m| m.name.clone()).collect::<Vec<_>>());
        assert!(crate::unity::pick_maindata(&meshes, &e.main_data).is_some() || !meshes.is_empty());
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn zipmod_wedding_inner_slot_127() {
        let path = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\Quokka\[Quokka]Inner Wedding Dress 1.0.zipmod"),
        );
        if !path.ok_exists() {
            return;
        }
        let cat = catalog_from_zipmod(&path);
        let e = cat
            .get_slot(210, 127, 100014396)
            .expect("wedding inner 210/127");
        assert_eq!(e.main_data, "p_o_top_inner06");
        let game = crate::test_support::game_path(r"");
        let (meshes, _) = crate::meshes_for_entry(&game, Some(&path), e);
        assert!(
            !meshes.is_empty(),
            "no meshes for {} / {}",
            e.main_ab,
            e.main_data
        );
        // ChaControl assigns create RTs after loading the mesh. A null
        // prefab MainTex is valid; test the actual catalog create recipe too.
        let (created,_)=crate::bake_clothes_create_textures(&game,e,[[1.0;4];4],&[],&[],&[]);
        let png=meshes.iter().find_map(|m|m.texture_png.as_ref()).or(created[0].as_ref())
            .expect("wedding inner needs static MainTex or a valid catalog create RT");
        let image=crate::teximg::load_from_memory(png).expect("clothing albedo must decode");
        assert!(image.width()>0 && image.height()>0);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_card_blocks() {
        use crate::card::{find_iend_end, lst_blocks, read_dotnet_string, read_i32_le, read_i64_le};
        use std::io::{Seek, SeekFrom};
        let card_path = crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        let data = crate::gamefs::read(&card_path).expect("read");
        let iend = find_iend_end(&data).expect("iend");
        let extra = &data[iend..];
        let mut cur = std::io::Cursor::new(extra);
        let _ = read_i32_le(&mut cur).unwrap();
        let marker = read_dotnet_string(&mut cur).unwrap();
        let version = read_dotnet_string(&mut cur).unwrap();
        let face_len = read_i32_le(&mut cur).unwrap();
        cur.seek(SeekFrom::Current(face_len as i64)).unwrap();
        let header_len = read_i32_le(&mut cur).unwrap() as usize;
        let pos = cur.position() as usize;
        let header_bytes = &extra[pos..pos + header_len];
        cur.set_position((pos + header_len) as u64);
        let header = rmpv::decode::read_value(&mut std::io::Cursor::new(header_bytes)).unwrap();
        let _total = read_i64_le(&mut cur).unwrap();
        let base = cur.position() as usize;
        let mut lines = vec![format!("marker={marker} version={version} header_len={header_len} base={base}")];
        for (name, bpos, bsize) in lst_blocks(&header) {
            let start = base + bpos as usize;
            let end = start + bsize as usize;
            let slice = &extra[start..end.min(extra.len())];
            let decoded = rmpv::decode::read_value(&mut std::io::Cursor::new(slice));
            let kind = match &decoded {
                Ok(v) if v.is_map() => format!("map n={}", v.as_map().map(|m| m.len()).unwrap_or(0)),
                Ok(v) if v.is_array() => format!("array n={}", v.as_array().map(|a| a.len()).unwrap_or(0)),
                Ok(v) => format!("other {v}"),
                Err(e) => format!("err {e}"),
            };
            let keys = if let Ok(v) = &decoded {
                if let Some(m) = v.as_map() {
                    m.iter()
                        .take(12)
                        .map(|(k, _)| k.as_str().unwrap_or("?").to_string())
                        .collect::<Vec<_>>()
                        .join(",")
                } else {
                    String::new()
                }
            } else {
                String::new()
            };
            if name == "Custom" {
                match crate::card::read_msgpack(slice) {
                    Some(v) => {
                        let keys = v.as_map().map(|m| {
                            m.iter().map(|(k, val)| format!("{k}:{}", if val.is_map() {"map"} else if val.is_array() {"arr"} else {"v"})).collect::<Vec<_>>().join(",")
                        }).unwrap_or_default();
                        lines.push(format!("  custom_decoded map={} keys={keys}", v.is_map()));
                    }
                    None => lines.push("  custom_decoded=None".into()),
                }
            }
            let hex: String = slice.iter().take(24).map(|b| format!("{b:02x}")).collect::<Vec<_>>().join(" ");
            lines.push(format!("block {name} pos={bpos} size={bsize} {kind} keys={keys} hex={hex}"));
            if name == "Coordinate" {
                if let Ok(v) = &decoded {
                    if let Some(arr) = v.as_array() {
                        if let Some(first) = arr.first() {
                            let inner = if first.is_map() {
                                Some(first.clone())
                            } else {
                                first.as_slice().and_then(crate::card::read_msgpack)
                            };
                            if let Some(slot) = inner {
                                let ck = slot.as_map().map(|m| {
                                    m.iter().map(|(k, val)| format!("{k}:{}", if val.is_map() {"map"} else if val.is_array() {"arr"} else {"v"})).collect::<Vec<_>>().join(",")
                                }).unwrap_or_default();
                                lines.push(format!("  coord0 keys={ck}"));
                                if let Some(m) = slot.as_map() {
                                    if let Some((_, arr)) = m.iter().find(|(k, _)| k.as_str() == Some("subPartsId")) {
                                        lines.push(format!("  subPartsId={}", arr.to_string().chars().take(200).collect::<String>()));
                                    }
                                }
                            } else {
                                lines.push(format!("  coord0 unwrap failed bin_len={}", first.as_slice().map(|s| s.len()).unwrap_or(0)));
                            }
                        }
                    }
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\card_blocks.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
    }
}
