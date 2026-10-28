//! An OK card opens from its own data and the base library's shared resources, nothing else (strict open).
//!
//! What the card reads of its third-party mods (their archives under `mods/`: listings, entries, the models of their
//! bundles) comes from its pack (`okLc`); what its open asks of the whole mods tree (merged item catalogue, GUID
//! lookups, uncensor manifests...) from its answers (`okLt`, tree_answers.rs). The base library serves the original
//! game and the shared mods (`shared_mods.txt`: uncensor bodies, global fixes, shadow ramps, MaterialEditor shaders)
//! and nothing else: the third-party mods it records (`sources.okidx` knows ~7300 archives; their models stay out of
//! version control, `models/.gitignore`) are never read, listed or scanned. Anything else the open would need is a
//! gap, and the open fails naming it: nothing falls back to the tree.
//!
//! Sealing (`seal`): an OK card made before this, or on a library holding third-party models (which then never went
//! into its pack), gets its answers recorded, then is opened in completing mode: whatever the strict open would miss
//! and the library has is copied into the card's pack. A strict open of the sealed card needs nothing else.
//!
//! Converting (`oklib::convert_card`): an original card becomes an OK card in one pass. Every outfit is opened once in
//! converting mode: the tree's answers are recorded (the scans behind them run in their own pool, `scanning`, where
//! nothing they read is the card's), the third-party data the library holds is copied (as when sealing) and what only
//! the zipmods hold is recorded from them (`oklib::begin_card_pack`). Whatever neither has is a gap.
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use crate::oklib::Id;

/// The strict open of one OK card (shared by the opens of its outfits while it is sealed).
#[derive(Default)]
pub struct Strict {
    gaps: Mutex<BTreeSet<String>>,
    /// Completing (sealing): what the library served in place of the card's own data, for the card's pack.
    adds: Option<Mutex<Additions>>,
    /// Converting an original card: the tree's answers are recorded meanwhile, and what the library lacks is read
    /// from the zipmods.
    converting: bool,
}

/// Library data a card being sealed reads of its third-party mods.
#[derive(Default)]
pub struct Additions {
    pub index: crate::gamefs::Index,
    /// (model, query, args) -> stored value.
    pub records: BTreeMap<(Id, String, Vec<u8>), Vec<u8>>,
}

impl Strict {
    pub fn completing() -> Arc<Strict> { Arc::new(Strict { gaps: Mutex::default(), adds: Some(Mutex::default()), converting: false }) }
    pub fn converting() -> Arc<Strict> { Arc::new(Strict { gaps: Mutex::default(), adds: Some(Mutex::default()), converting: true }) }
    pub fn gaps(&self) -> Vec<String> { self.gaps.lock().unwrap_or_else(|e| e.into_inner()).iter().cloned().collect() }
    pub fn take_additions(&self) -> Additions {
        self.adds.as_ref().map(|a| std::mem::take(&mut *a.lock().unwrap_or_else(|e| e.into_inner()))).unwrap_or_default()
    }
}

/// The card being sealed: the opens of its outfits share this (completing) strict open.
static SEALING: Mutex<Option<Arc<Strict>>> = Mutex::new(None);
pub fn begin_sealing() -> Arc<Strict> {
    let strict = Strict::completing();
    *SEALING.lock().unwrap_or_else(|e| e.into_inner()) = Some(strict.clone());
    strict
}
pub fn end_sealing() { *SEALING.lock().unwrap_or_else(|e| e.into_inner()) = None; }
/// The strict open for an OK card being mounted.
pub fn for_open() -> Arc<Strict> {
    SEALING.lock().unwrap_or_else(|e| e.into_inner()).clone().unwrap_or_default()
}

/// The strict open this thread works for (none while a tree scan runs, nor while answers are recorded for sealing:
/// those ask the tree on purpose).
pub fn active() -> Option<Arc<Strict>> {
    if SCANNING.with(|s| s.get()) { return None; }
    let strict = crate::oklib::pack_scope()?.strict.clone()?;
    if crate::tree_answers::recording() && !strict.converting { return None; }
    Some(strict)
}

/// This thread converts an original card (see the module notes).
pub fn converting() -> bool { active().is_some_and(|s| s.converting) }

thread_local! { static SCANNING: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
/// A scan of the whole mods tree behind a question an OK card answers itself (tree_answers.rs). While converting it
/// runs in a pool of its own whose threads are marked: what it reads is the tree's, never copied into the card (and
/// a value it leaves in a memo is read again by the card's own open, which copies it). Elsewhere it just runs.
pub fn scanning<R: Send>(scan: impl FnOnce() -> R + Send) -> R {
    if !converting() { return scan(); }
    static POOL: std::sync::OnceLock<Option<rayon::ThreadPool>> = std::sync::OnceLock::new();
    let pool = POOL.get_or_init(|| rayon::ThreadPoolBuilder::new()
        .num_threads(std::thread::available_parallelism().map_or(4, |n| n.get()).clamp(2, 16)).thread_name(|i| format!("OKTreeScan{i}"))
        .start_handler(|_| SCANNING.with(|s| s.set(true))).build().ok());
    match pool { Some(pool) => pool.install(scan), None => { SCANNING.with(|s| s.set(true)); let r = scan(); SCANNING.with(|s| s.set(false)); r } }
}

/// A gap of the strict open on this thread (nothing when there is none).
pub fn gap(what: impl FnOnce() -> String) {
    let Some(s) = active() else { return };
    let what = what();
    // OK_STRICT_TRACE=<text>: where a gap containing it comes from.
    if std::env::var("OK_STRICT_TRACE").is_ok_and(|t| what.contains(&t)) { eprintln!("ok-extract: strict gap {what} at\n{}", std::backtrace::Backtrace::force_capture()); }
    s.gaps.lock().unwrap_or_else(|e| e.into_inner()).insert(what);
}

/// Copies library data into the card being sealed: true when this open is one (`add` ran).
pub fn complete(add: impl FnOnce(&mut Additions)) -> bool {
    let Some(s) = active() else { return false };
    let Some(adds) = s.adds.as_ref() else { return false };
    add(&mut adds.lock().unwrap_or_else(|e| e.into_inner()));
    true
}

/// This thread works for a card being sealed.
pub fn completing() -> bool { active().is_some_and(|s| s.adds.is_some()) }
/// A card being sealed asks a query of its own model (one only third-party mods use) that neither its pack nor the
/// library holds: every open used to compute it from what is stored; the value is kept in the card (`keep_record`).
pub fn completing_model(id: &Id) -> bool { completing() && !library_model(id) && shared().is_some_and(|s| s.present(id)) }
pub fn keep_record(id: &Id, query: &str, args: &[u8], raw: Vec<u8>) {
    complete(|a| { a.records.entry((*id, query.to_owned(), args.to_vec())).or_insert(raw); });
}

/// Under a strict open: the library may serve this game path (outside `mods/`, or a shared mod's).
pub fn library_path(key: &str) -> bool {
    if !(key == "mods" || key.starts_with("mods/")) { return true; }
    shared().is_some_and(|s| s.whole(key))
}
/// Under a strict open: the library may serve this entry of a mod archive.
pub fn library_entry(zip: &str, entry: &str) -> bool {
    let label = format!("{zip}|{}", entry.replace('\\', "/")).to_ascii_lowercase();
    shared().is_some_and(|s| s.entry(&label))
}
/// Under a strict open: the library may serve this archive's listing (a shared mod, or one a shared bundle is in).
pub fn library_zip(zip: &str) -> bool { shared().is_some_and(|s| s.zip(zip)) }
/// Under a strict open: the library may serve this model: it holds it, and not as one only third-party mods use.
pub fn library_model(id: &Id) -> bool { shared().is_some_and(|s| !s.excluded.contains(id) && s.present(id)) }

/// The library's shared mods (`shared_mods.txt`) and the models that stay out of version control (`models/.gitignore`,
/// both written by `--oklib shared-ignore`).
struct Shared { lines: Vec<String>, excluded: HashSet<Id>, lib: PathBuf, present: Mutex<HashMap<Id, bool>> }

impl Shared {
    fn load(lib: &Path) -> Result<Shared, String> {
        let list = [lib.join("shared_mods.txt"), lib.join("../../../Tools/base_library_shared_mods.txt")].into_iter()
            .find(|p| p.is_file()).ok_or_else(|| format!("{}: no shared_mods.txt (ok-extract --oklib shared-ignore)", lib.display()))?;
        let lines = std::fs::read_to_string(&list).map_err(|e| format!("{}: {e}", list.display()))?.lines()
            .map(|l| l.trim().replace('\\', "/").to_ascii_lowercase()).filter(|l| !l.is_empty() && !l.starts_with('#')).collect();
        let ignore = lib.join("models").join(".gitignore");
        let excluded = std::fs::read_to_string(&ignore).map_err(|e| format!("{}: {e}", ignore.display()))?.lines()
            .filter_map(|l| l.trim().rsplit('/').next()?.strip_suffix(".okmodel").and_then(crate::oklib::id_from_hex_pub)).collect();
        Ok(Shared { lines, excluded, lib: lib.to_owned(), present: Mutex::default() })
    }
    /// The library has model `id` (a model only a card's own zipmod has is not shared, whatever the ignore list).
    fn present(&self, id: &Id) -> bool {
        *self.present.lock().unwrap_or_else(|e| e.into_inner()).entry(*id).or_insert_with(|| crate::oklib::model_path(&self.lib, id).is_file())
    }
    /// The same rule as `shared-ignore`: original sources, list bundles of any archive, listed folders, archives and bundles.
    fn entry(&self, label: &str) -> bool {
        !label.starts_with("mods/") || label.contains("|abdata/list/") || self.lines.iter().any(|s| {
            if s.ends_with('/') { label.starts_with(s.as_str()) }
            else if s.contains('|') { label == s }
            else { label.strip_prefix(s.as_str()).is_some_and(|rest| rest.starts_with('|')) }
        })
    }
    /// A whole archive or file the library serves.
    fn whole(&self, key: &str) -> bool {
        self.lines.iter().any(|s| if s.ends_with('/') { key.starts_with(s.as_str()) } else { !s.contains('|') && key == s })
    }
    /// An archive whose listing the library serves.
    fn zip(&self, key: &str) -> bool {
        self.whole(key) || self.lines.iter().any(|s| s.split_once('|').is_some_and(|(zip, _)| zip == key))
    }
}

fn shared() -> Option<Arc<Shared>> {
    static LOADED: Mutex<Option<HashMap<PathBuf, Result<Arc<Shared>, String>>>> = Mutex::new(None);
    let lib = crate::oklib::active_dir()?;
    let found = LOADED.lock().unwrap_or_else(|e| e.into_inner()).get_or_insert_with(HashMap::new).entry(lib.clone())
        .or_insert_with(|| Shared::load(&lib).map(Arc::new)).clone();
    match found {
        Ok(s) => Some(s),
        Err(e) => { gap(|| e); None }
    }
}

/// The error of a strict open that had gaps.
pub fn failure(card: &Path, gaps: &[String]) -> String {
    let shown: Vec<&str> = gaps.iter().take(12).map(String::as_str).collect();
    format!("{}: the OK card lacks {} thing(s) its open needs (neither in its okLc/okLt nor shared resources of the base \
        library; ok-extract X:/NoGame --oklib seal <base library> <card> completes it): {}{}", card.display(), gaps.len(),
        shown.join("; "), if gaps.len() > shown.len() { "; ..." } else { "" })
}

#[cfg(test)]
mod tests {
    use super::*;
    fn shared() -> Shared {
        Shared { lines: vec!["mods/sideloader modpack - kks_uncensorselector/".into(), "mods/sideloader modpack/[dak]cgramp.zipmod".into(),
            "mods/sideloader modpack/tansen/[tansen] eyes skin v2.0.zipmod|abdata/chara/mt_eye_00.unity3d".into()], excluded: HashSet::new(),
            lib: PathBuf::new(), present: Mutex::default() }
    }
    #[test]
    fn shared_mods_rule() {
        let s = shared();
        // Original sources and list bundles of any archive.
        assert!(s.entry("abdata/chara/co_top_00.unity3d"));
        assert!(s.entry("mods/a/[x] top.zipmod|abdata/list/characustom/x.unity3d"));
        // Listed folder, archive, single bundle.
        assert!(s.entry("mods/sideloader modpack - kks_uncensorselector/body.zipmod|abdata/chara/x.unity3d"));
        assert!(s.entry("mods/sideloader modpack/[dak]cgramp.zipmod|abdata/ramp.unity3d"));
        assert!(s.entry("mods/sideloader modpack/tansen/[tansen] eyes skin v2.0.zipmod|abdata/chara/mt_eye_00.unity3d"));
        assert!(!s.entry("mods/sideloader modpack/tansen/[tansen] eyes skin v2.0.zipmod|abdata/chara/other.unity3d"));
        // A third-party archive.
        assert!(!s.entry("mods/a/[x] top.zipmod|abdata/chara/top.unity3d"));
        assert!(s.whole("mods/sideloader modpack/[dak]cgramp.zipmod") && !s.whole("mods/a/[x] top.zipmod"));
        // An archive with one shared bundle: its listing is served, its other entries are not.
        assert!(s.zip("mods/sideloader modpack/tansen/[tansen] eyes skin v2.0.zipmod"));
        assert!(!s.whole("mods/sideloader modpack/tansen/[tansen] eyes skin v2.0.zipmod"));
    }
}
