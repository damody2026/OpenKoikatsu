//! Recorded memo results of one open, carried in the card (`okMm`, cardbake.rs) and replayed when the card opens on
//! another machine or in a fresh process, so the open only does the work the memos do not cover.
//!
//! Each kind is a memo the open path already keys by content (or by everything its result depends on):
//!   tex   texture transforms (`png_transform_cache`) and texture decodes from texels (`png_from_payload`)
//!   res   the mods catalog view and the card's GUID index lookup (list resolution)
//!   mesh  extracted item meshes (`meshes_for_entry`)
//! `ok-card-bake --memo KINDS` records them (hits on in-process memos are recorded as well, so every outfit's set is
//! complete); `OK_CARD_MEMO=KINDS` (e.g. `tex,res,mesh`) replays them. The set is stored under the open-cache key, so
//! it is only used for exactly the open it was recorded for (same card, outfit, clothes states and library).
use std::collections::HashMap;
use std::sync::{Arc, Mutex, RwLock};

pub const TEX: u8 = 1;
pub const RES: u8 = 2;
pub const MESH: u8 = 4;

pub fn parse_kinds(text: &str) -> u8 {
    text.split(',').map(|k| match k.trim() { "tex" => TEX, "res" => RES, "mesh" => MESH, "all" => TEX | RES | MESH, _ => 0 }).fold(0, |a, b| a | b)
}

/// Kinds replayed from cards (`OK_CARD_MEMO`).
pub fn replay_kinds() -> u8 {
    static KINDS: std::sync::OnceLock<u8> = std::sync::OnceLock::new();
    *KINDS.get_or_init(|| std::env::var("OK_CARD_MEMO").map(|v| parse_kinds(&v)).unwrap_or(0))
}

type Table = HashMap<(u8, Vec<u8>), Arc<Vec<u8>>>;
static RECORD: Mutex<Option<(u8, Table)>> = Mutex::new(None);
static SEED: RwLock<Option<Arc<Table>>> = RwLock::new(None);

/// Whether a hook of this kind does anything now (recording it, or a seed is loaded).
pub fn active(kind: u8) -> bool {
    RECORD.lock().unwrap_or_else(|e| e.into_inner()).as_ref().is_some_and(|r| r.0 & kind != 0)
        || SEED.read().unwrap_or_else(|e| e.into_inner()).is_some()
}

/// A replayed result.
pub fn get(kind: u8, key: &[u8]) -> Option<Arc<Vec<u8>>> {
    let seed = SEED.read().unwrap_or_else(|e| e.into_inner());
    let hit = seed.as_ref()?.get(&(kind, key.to_vec())).cloned();
    // Counted in OK_PROFILE's SPOT lines.
    if hit.is_some() { let _spot = crate::profile::spot(match kind { TEX => "memo hit tex", RES => "memo hit res", _ => "memo hit mesh" }); }
    hit
}

/// Records a result (no-op unless recording this kind). `value` is only computed when recording.
pub fn put(kind: u8, key: &[u8], value: impl FnOnce() -> Option<Vec<u8>>) {
    let mut record = RECORD.lock().unwrap_or_else(|e| e.into_inner());
    let Some((kinds, table)) = record.as_mut() else { return };
    if *kinds & kind == 0 || table.contains_key(&(kind, key.to_vec())) { return; }
    drop(record);
    let Some(value) = value() else { return };
    if let Some((_, table)) = RECORD.lock().unwrap_or_else(|e| e.into_inner()).as_mut() {
        table.entry((kind, key.to_vec())).or_insert_with(|| Arc::new(value));
    }
}

/// Starts recording these kinds (ok-card-bake, around one open).
pub fn begin_record(kinds: u8) {
    *RECORD.lock().unwrap_or_else(|e| e.into_inner()) = Some((kinds, HashMap::new()));
}

/// Stops recording; the recorded set as bytes for the card.
pub fn end_record() -> Result<Vec<u8>, String> {
    let table = RECORD.lock().unwrap_or_else(|e| e.into_inner()).take().map(|r| r.1).unwrap_or_default();
    let mut list: Vec<(u8, Vec<u8>, Vec<u8>)> = table.into_iter().map(|((k, key), v)| (k, key, (*v).clone())).collect();
    list.sort_by(|a, b| (a.0, &a.1).cmp(&(b.0, &b.1)));
    bincode::serialize(&list).map_err(|e| e.to_string())
}

/// Replays a recorded set (kinds limited to `kinds`) until the guard drops.
pub fn seed(bytes: &[u8], kinds: u8) -> Option<SeedGuard> {
    let list: Vec<(u8, Vec<u8>, Vec<u8>)> = bincode::deserialize(bytes).ok()?;
    let table: Table = list.into_iter().filter(|e| e.0 & kinds != 0).map(|(k, key, v)| ((k, key), Arc::new(v))).collect();
    *SEED.write().unwrap_or_else(|e| e.into_inner()) = Some(Arc::new(table));
    Some(SeedGuard)
}
pub struct SeedGuard;
impl Drop for SeedGuard {
    fn drop(&mut self) { *SEED.write().unwrap_or_else(|e| e.into_inner()) = None; }
}

/// Count of recorded entries per kind and bytes (bake report).
pub fn describe(bytes: &[u8]) -> String {
    let list: Vec<(u8, Vec<u8>, Vec<u8>)> = bincode::deserialize(bytes).unwrap_or_default();
    let mut out = String::new();
    for (kind, name) in [(TEX, "tex"), (RES, "res"), (MESH, "mesh")] {
        let (n, b) = list.iter().filter(|e| e.0 == kind).fold((0, 0usize), |a, e| (a.0 + 1, a.1 + e.2.len()));
        if n > 0 { out += &format!(" {name}={n}/{:.1}MB", b as f64 / 1048576.0); }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn record_then_replay_only_selected_kinds() {
        begin_record(TEX | MESH);
        assert!(active(TEX));
        put(TEX, b"a", || Some(vec![1]));
        put(RES, b"b", || panic!("not recorded"));
        put(MESH, b"c", || Some(vec![3]));
        let bytes = end_record().unwrap();
        assert!(!active(TEX));
        let guard = seed(&bytes, TEX).unwrap();
        assert_eq!(get(TEX, b"a").as_deref(), Some(&vec![1]));
        assert!(get(MESH, b"c").is_none());
        drop(guard);
        assert!(get(TEX, b"a").is_none());
        assert_eq!(parse_kinds("tex, mesh"), TEX | MESH);
    }
}
