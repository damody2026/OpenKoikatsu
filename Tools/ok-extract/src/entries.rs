//! List entries to meshes: the entry memo and its keys, garment-state rules, background opens, the entry prefetch and mesh extraction of hair, clothes and accessories.
use super::*;

thread_local! {
    /// The view the entry memos were filled in (extract_session).
    pub(crate) static ENTRY_EPOCH: std::cell::RefCell<String> = const { std::cell::RefCell::new(String::new()) };
}

pub(crate) fn clear_entry_memo() {
    ENTRY_MEMO.with(|m| m.borrow_mut().clear());
    ACCESSORY_SOURCE_MEMO.with(|m| m.borrow_mut().clear());
    VIEW_MEMO.with(|m| m.borrow_mut().clear());
}

thread_local! {
    /// Results of an open's stages that do not depend on the outfit or garment states (or that say so in their key),
    /// kept for the view's next opens like the entry memo: a conversion opens every outfit in six garment states, 42
    /// opens of one card that loaded the same base bundles and catalogue each time. Their reads were recorded by the
    /// view's first open into the same pack.
    static VIEW_MEMO: std::cell::RefCell<std::collections::HashMap<String, std::rc::Rc<dyn std::any::Any>>> =
        std::cell::RefCell::new(std::collections::HashMap::new());
}

thread_local! {
    /// This thread is in an open (extract_session_from_info), whose view VIEW_MEMO holds. Other threads (prefetch
    /// helpers, pool work) never clear theirs when the view changes, so they compute afresh.
    static IN_VIEW: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
}
/// Marks this thread as in an open for its duration (VIEW_MEMO answers only there).
pub(crate) struct ViewMemoScope(bool);
impl ViewMemoScope {
    pub(crate) fn begin() -> Self { ViewMemoScope(IN_VIEW.with(|v| v.replace(true))) }
}
impl Drop for ViewMemoScope {
    fn drop(&mut self) { IN_VIEW.with(|v| v.set(self.0)); }
}

thread_local! { static STATE_VARIANT: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
/// This thread's opens are a conversion's garment-state variants of an outfit already opened in its own states
/// (oklib::make_ok_card): only what those states read is needed of them, the session is not kept.
pub(crate) fn state_variant_open() -> bool { STATE_VARIANT.with(|v| v.get()) }
/// Marks this thread's opens as garment-state variants while alive.
pub(crate) struct StateVariantScope(bool);
impl StateVariantScope { pub(crate) fn begin() -> Self { StateVariantScope(STATE_VARIANT.with(|v| v.replace(true))) } }
impl Drop for StateVariantScope { fn drop(&mut self) { STATE_VARIANT.with(|v| v.set(self.0)); } }

/// A value `view_memo_put` kept for this view and key (None outside an open, or with OK_NO_VIEW_MEMO).
pub(crate) fn view_memo_get<T: Clone + 'static>(key: &str) -> Option<T> {
    if std::env::var_os("OK_NO_VIEW_MEMO").is_some() || !IN_VIEW.with(|v| v.get()) { return None; }
    VIEW_MEMO.with(|m| m.borrow().get(key).and_then(|v| v.downcast_ref::<T>().cloned()))
}
/// Keeps `value` for this view's later opens (no-op outside an open).
pub(crate) fn view_memo_put<T: 'static>(key: String, value: T) {
    if std::env::var_os("OK_NO_VIEW_MEMO").is_some() || !IN_VIEW.with(|v| v.get()) { return; }
    VIEW_MEMO.with(|m| m.borrow_mut().insert(key, std::rc::Rc::new(value)));
}

/// `make()` once per view and key (see VIEW_MEMO); a clone of it after. Outside an open: `make()`.
pub(crate) fn view_memo<T: Clone + 'static>(key: String, make: impl FnOnce() -> T) -> T {
    // OK_NO_VIEW_MEMO=1: every open computes it (to compare with the memo).
    static OFF: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    if *OFF.get_or_init(|| std::env::var_os("OK_NO_VIEW_MEMO").is_some()) || !IN_VIEW.with(|v| v.get()) { return make(); }
    if let Some(hit) = VIEW_MEMO.with(|m| m.borrow().get(&key).and_then(|v| v.downcast_ref::<T>().cloned())) { return hit; }
    let made = make();
    VIEW_MEMO.with(|m| m.borrow_mut().insert(key, std::rc::Rc::new(made.clone())));
    made
}

thread_local! {
    // The same accessory bundle is selected by mesh, colour and assignment
    // stages of one slot. Share one acquisition within an installed-source
    // epoch (cleared with the entry memo); bounded because bundles are large.
    pub(crate) static ACCESSORY_SOURCE_MEMO: std::cell::RefCell<Vec<((Option<PathBuf>,String,String),Option<std::sync::Arc<(crate::unity::SourceBytes,Option<PathBuf>)>>)>>
        = const { std::cell::RefCell::new(Vec::new()) };
}

pub(crate) fn entry_sql_key(game_root: &Path, zip: Option<&Path>, entry: &ListEntry) -> String {let _spot=crate::profile::spot("entry_sql_key");
    if entry.main_ab.is_empty() { return String::new(); }
    // The same in every open of a view (crate::view_memo): finding the entry's bundle again took ~0.5 ms, for every
    // item of every open (ACCESSORY_SOURCE_MEMO keeps only four bundles).
    // The entry prefetch helpers ask for it of every item they extract too (a miss reads the entry's bundle, 10–30 ms).
    pack_memo(format!("entry_sql_key|{}|{zip:?}|{}|{}", game_root.display(), entry.main_ab, entry.main_data),
        || entry_sql_key_view(game_root, zip, entry))
}

/// `make()` once per mounted card pack's tree (its identity and the fallback's generation) and key, for every thread
/// of its opens (the open's own, the entry prefetch helpers, pool work): a view_memo that the helpers share. Without a
/// pack (a game folder): view_memo.
pub(crate) fn pack_memo<T: Clone + Send + Sync + 'static>(key: String, make: impl FnOnce() -> T) -> T {
    let pack = crate::oklib::mounted_pack_identity();
    if pack.is_empty() || std::env::var_os("OK_NO_VIEW_MEMO").is_some() { return view_memo(key, make); }
    type Slot = std::sync::Arc<dyn std::any::Any + Send + Sync>;
    static SHARED: std::sync::LazyLock<std::sync::Mutex<std::collections::HashMap<String, Slot>>> = std::sync::LazyLock::new(Default::default);
    let shared_key = format!("{pack}|{}|{key}", crate::gamefs::fallback_generation());
    if let Some(hit) = SHARED.lock().unwrap_or_else(|e| e.into_inner()).get(&shared_key).and_then(|v| v.downcast_ref::<T>().cloned()) { return hit; }
    let made = make();
    let mut shared = SHARED.lock().unwrap_or_else(|e| e.into_inner());
    if shared.len() > 100_000 { shared.clear(); }
    shared.insert(shared_key, std::sync::Arc::new(made.clone()));
    made
}

fn entry_sql_key_view(game_root: &Path, zip: Option<&Path>, entry: &ListEntry) -> String {
    match accessory_bundle_source(game_root, zip, entry).and_then(|(_,source)|source) {
        Some(source) => crate::sqlcache::zip_key(&source, &entry.main_ab),
        None => crate::sqlcache::file_key(&game_root.join("abdata").join(&entry.main_ab)),
    }
}

pub(crate) fn entry_prefab_variant_key(category: i32, shake_enabled: bool, clothes_states: [u8; 9]) -> String {
    // Parsed clothes prefabs have already dropped inactive n_top/n_bot/etc.
    // nodes. Their cache identity must include the runtime DrawClothes state;
    // a card can request several states of the same bundle in one process.
    if (105..=112).contains(&category) || (200..=212).contains(&category) {
        let states = clothes_states.iter().map(u8::to_string).collect::<Vec<_>>().join("");
        format!("shake={shake_enabled}|clothes={states}")
    } else {
        format!("shake={shake_enabled}")
    }
}

/// `ChaControl.SetClothesState` (decompiled, lines 2121-2244): a missing
/// StateType has no dictionary and accepts the requested value unchanged.
/// Type 0 allows 0/1/3, type 1 allows 0/3, type 2 allows all four.
pub(crate) fn set_clothes_state(
    states: &mut [u8; 9], kind: usize, mut state: u8,
    state_type: Option<u8>, not_bot: bool, not_shorts: bool,
    shoe_types: [Option<u8>; 2],
) {
    let previous = states[kind];
    if let Some(ty @ 0..=2) = state_type {
        loop {
            let allowed = match ty { 0 => state != 2, 1 => state == 0 || state == 3, _ => true };
            if allowed { states[kind] = state; break; }
            state = (state + 1) % 4;
            if state == previous { break; }
        }
    } else {
        states[kind] = state;
    }
    let linked = match kind {
        0 if not_bot => Some(1), 1 if not_bot => Some(0),
        2 if not_shorts => Some(3), 3 if not_shorts => Some(2),
        _ => None,
    };
    if let Some(other) = linked {
        if states[kind] == 3 { states[other] = 3; }
        else if states[other] == 3 { states[other] = state; }
    }
    if kind == 7 || kind == 8 {
        let other = if kind == 7 { 8 } else { 7 };
        if state != 0 || shoe_types[other - 7].is_some_and(|ty| ty <= 2) {
            states[other] = state;
        }
    }
}

pub(crate) fn resolve_clothes_states(
    requested: [u8; 9], types: [Option<u8>; 9], not_bot: bool, not_shorts: bool,
) -> [u8; 9] {
    // UpdateClothesStateAll calls SetClothesState in enum order. The value
    // passed to linked slots is the post-cycle request, not the prior value.
    let mut states = [0u8; 9];
    for kind in 0..9 {
        set_clothes_state(&mut states, kind, requested[kind], types[kind],
            not_bot, not_shorts, [types[7], types[8]]);
    }
    states
}

/// `AddClothesStateKind` adjusts state menus when the adjacent garment owns
/// the DEF node. Presence comes from that specific prefab's reference tree.
pub(crate) fn adjust_clothes_state_types(
    mut types: [Option<u8>; 9], refs: [bool; 4], not_bra: bool, not_shorts: bool,
) -> [Option<u8>; 9] {
    let mut top = types[0].unwrap_or(3);
    let mut bot = types[1].unwrap_or(3);
    if bot != 0 && refs[0] {
        if top == 0 { bot = 0; }
        else if top == 1 && bot == 2 { bot = 1; }
    }
    if top != 0 && refs[1] {
        if bot == 0 { top = 0; }
        else if bot == 1 && top == 2 { top = 1; }
    }
    types[0] = (top <= 2).then_some(top);
    types[1] = (bot <= 2).then_some(bot);
    let mut bra = if not_bra { 3 } else { types[2].unwrap_or(3) };
    let mut shorts = if not_bra && not_shorts { 3 } else { types[3].unwrap_or(3) };
    if shorts != 0 && shorts != 2 && refs[2] {
        if bra == 0 { shorts = 0; }
        else if bra == 1 && shorts == 3 { shorts = 1; }
    }
    if bra != 0 && bra != 2 && refs[3] {
        if shorts == 0 { bra = 0; }
        else if shorts == 1 && bra == 3 { bra = 1; }
    }
    types[2] = (bra <= 2).then_some(bra);
    types[3] = (shorts <= 2).then_some(shorts);
    types
}

pub(crate) fn selected_clothes_reference_flags(
    game_root: &Path, selected: &[SelectedClothes],
) -> [bool; 4] {
    std::array::from_fn(|kind| {
        let Some((entry, _, zip_path)) = selected[kind].as_ref() else { return false; };
        if is_dummy_part(&entry.main_data, Some(&entry.name)) { return false; }
        let zip = entry.extra.get("__zip").map(PathBuf::from).or_else(|| zip_path.clone());
        let Some(bytes) = accessory_bundle_bytes(game_root, zip.as_deref(), entry) else { return false; };
        let nodes = crate::unity::prefab_node_names(&bytes, &entry.main_data);
        let wanted = if kind == 0 || kind == 2 { "n_bot_a" } else { "n_top_a" };
        nodes.contains(wanted)
    })
}

pub(crate) fn meshes_for_entry(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {let _spot=crate::profile::spot("meshes_for_entry");
    meshes_for_entry_shake(game_root, zip, entry, true)
}

/// Foreground opens in progress (the reader opening the card the user asked for).
pub(crate) static FOREGROUND_OPENS: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
thread_local! { static BACKGROUND_OPEN: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
/// Marks this thread's opens as background work (the reader's clothing variant warm-up) that yields to foreground opens.
pub(crate) fn set_thread_background(on: bool) { BACKGROUND_OPEN.with(|b| b.set(on)); }
pub(crate) fn thread_is_background() -> bool { BACKGROUND_OPEN.with(|b| b.get()) }
/// Counts a foreground open while alive (no-op on a background thread).
pub(crate) struct ForegroundOpen(bool);
impl ForegroundOpen { pub(crate) fn begin() -> Self { let fg = !thread_is_background(); if fg { FOREGROUND_OPENS.fetch_add(1, std::sync::atomic::Ordering::SeqCst); } Self(fg) } }
impl Drop for ForegroundOpen { fn drop(&mut self) { if self.0 { FOREGROUND_OPENS.fetch_sub(1, std::sync::atomic::Ordering::SeqCst); } } }
/// A background open gives up at its next stage boundary while a foreground open waits: it holds the process-wide
/// card pack mount, and the card the user asked for must not queue behind a speculative build.
pub(crate) fn check_preempted() -> Result<(), String> {
    if thread_is_background() && FOREGROUND_OPENS.load(std::sync::atomic::Ordering::SeqCst) > 0 { Err("background open preempted by a foreground open".into()) } else { Ok(()) }
}

pub(crate) type EntryMeshes = (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>);
/// One prefetched item: filled by a helper thread, waited for by the extraction that asks for the same memo key.
pub(crate) type PrefetchSlot = std::sync::Arc<(std::sync::Mutex<Option<Option<EntryMeshes>>>, std::sync::Condvar)>;
// The prefetched items of the extraction running on this thread (cards opening at once each keep their own).
thread_local! { static ENTRY_PREFETCH: std::cell::RefCell<Option<std::collections::HashMap<String, PrefetchSlot>>> = const { std::cell::RefCell::new(None) }; }
thread_local! { static PREFETCH_WORKER: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }

/// (in-process memo key, disk cache key; empty when the entry has no source to key on) of an entry's meshes.
/// Garment states enter as the visibility of the state nodes the entry's prefab has (STATE_NODES), once an
/// extraction of it named them: a conversion opens every outfit in six garment states, and most items look the same
/// in all of them. Before that, as the states themselves.
pub(crate) fn entry_memo_key_for(game_root: &Path, zip: Option<&Path>, entry: &ListEntry, shake_enabled: bool) -> (String, String) {
    let base_sql_key = entry_sql_key(game_root, zip, entry);
    let (base, states) = entry_state_base(game_root, zip, entry, shake_enabled, &base_sql_key);
    let states = entry_state_sig(&base, entry.category_no, states);
    let sql_key = if base_sql_key.is_empty() { String::new() } else { format!("{base_sql_key}|shake={shake_enabled}|{states}|dynamicfix={}|references={}",
        crate::dbone::dynamic_bones_fix_enabled(game_root), crate::unity::references::current_signature(game_root)) };
    (format!("{base}|{states}"), sql_key)
}

/// Everything an entry's meshes depend on except the garment states (the key of its STATE_NODES), and the states.
fn entry_state_base(game_root: &Path, zip: Option<&Path>, entry: &ListEntry, shake_enabled: bool, base_sql_key: &str) -> (String, [u8; 9]) {
    (format!("{base_sql_key}|{}|{}|{}|{}|shake={shake_enabled}|dynamicfix={}|references={}|pack={}",
        zip.map(|p| p.to_string_lossy().into_owned()).unwrap_or_default(), entry.main_ab, entry.main_data, entry.category_no,
        crate::dbone::dynamic_bones_fix_enabled(game_root), crate::unity::references::current_signature(game_root),
        crate::oklib::mounted_pack_identity()), crate::unity::clothes_states())
}

/// State nodes of an entry's prefab (collect_state_nodes), by entry_state_base. Shared by the threads of an open (the
/// entry prefetch helpers learn them too); the base names the source and the mounted pack, so it holds across opens.
static STATE_NODES: std::sync::LazyLock<std::sync::Mutex<std::collections::HashMap<String, std::sync::Arc<Vec<(String, Option<i32>)>>>>> =
    std::sync::LazyLock::new(Default::default);

fn entry_state_sig(base: &str, category: i32, states: [u8; 9]) -> String {
    let nodes = STATE_NODES.lock().unwrap_or_else(|e| e.into_inner()).get(base).cloned();
    match nodes {
        Some(nodes) => format!("nodes={}", nodes.iter().map(|(name, cc)| match crate::unity::clothes_state_node_visible(name, &states, *cc) {
            Some(true) => '1', Some(false) => '0', None => '-' }).collect::<String>()),
        None => unlearned_state_sig(category, states),
    }
}

/// The states part of an entry's key before an extraction named the prefab's state nodes.
fn unlearned_state_sig(category: i32, states: [u8; 9]) -> String { format!("{}|{states:?}", entry_prefab_variant_key(category, true, states)) }

/// The entry's memo key now and as it was before its state nodes were learned: an entry prefetch keyed before a helper
/// learned them is still this extraction's (the same states).
pub(crate) fn entry_memo_keys(game_root: &Path, zip: Option<&Path>, entry: &ListEntry, shake_enabled: bool) -> (String, String) {
    let base_sql_key = entry_sql_key(game_root, zip, entry);
    let (base, states) = entry_state_base(game_root, zip, entry, shake_enabled, &base_sql_key);
    (format!("{base}|{}", entry_state_sig(&base, entry.category_no, states)), format!("{base}|{}", unlearned_state_sig(entry.category_no, states)))
}

/// Records the state nodes an extraction of the entry looked at (see entry_memo_key_for).
fn learn_state_nodes(game_root: &Path, zip: Option<&Path>, entry: &ListEntry, shake_enabled: bool, nodes: Vec<(String, Option<i32>)>) {
    let base_sql_key = entry_sql_key(game_root, zip, entry);
    if base_sql_key.is_empty() { return; }
    let (base, _) = entry_state_base(game_root, zip, entry, shake_enabled, &base_sql_key);
    let mut map = STATE_NODES.lock().unwrap_or_else(|e| e.into_inner());
    if map.len() > 200_000 { map.clear(); }
    map.entry(base).or_insert_with(|| std::sync::Arc::new(nodes));
}

/// Starts extracting the items a card's hair, accessory and clothing passes are about to ask for, each on a helper
/// thread with this thread's clothes states and switches. The passes stay sequential and in order; an item's
/// `meshes_for_entry_shake` then takes the helper's result (waiting if it is still running) when its memo key is the
/// one predicted, and extracts itself otherwise. A wrong prediction only costs the helper's work.
pub(crate) fn prefetch_entries(game_root: &Path, items: Vec<(Option<PathBuf>, ListEntry, bool)>) {
    clear_entry_prefetch();
    if items.is_empty() || std::env::var_os("OK_NO_ENTRY_PREFETCH").is_some() { return; }
    let states = crate::unity::clothes_states();
    let requested = crate::unity::requested_clothes_states();
    let switches = crate::profile::snapshot();
    let mut jobs = Vec::new();
    {
        ENTRY_PREFETCH.with(|cell| { let mut store = cell.borrow_mut();
        let store = store.get_or_insert_with(std::collections::HashMap::new);
        for (zip, entry, shake) in items {
            if entry.main_data.is_empty() || entry.main_data.eq_ignore_ascii_case("p_dummy") { continue; }
            let (key, _) = entry_memo_key_for(game_root, zip.as_deref(), &entry, shake);
            if store.contains_key(&key) || ENTRY_MEMO.with(|m| m.borrow().contains_key(&key)) { continue; }
            let slot: PrefetchSlot = Default::default();
            store.insert(key.clone(), slot.clone());
            jobs.push((key, slot, zip, entry, shake));
        }
        });
    }
    // One helper per source bundle, its items in order: parsed bundles are cached per thread, so items of one large
    // pack spread over the pool each decompressed and kept their own copy (one card reached 20+ GB that way).
    let mut groups: Vec<((Option<PathBuf>, String), Vec<_>)> = Vec::new();
    for job in jobs {
        let source = (job.2.clone(), job.3.main_ab.to_ascii_lowercase());
        match groups.iter_mut().find(|(s, _)| *s == source) { Some((_, g)) => g.push(job), None => groups.push((source, vec![job])) }
    }
    // A few helpers take the groups in order (the order the passes ask for them): one per group at once parsed every
    // source bundle of the card together. Each helper drops what it parsed once a group's items are handed over.
    // A group of more than two items goes to two helpers, half each: the bundle is parsed twice at most, and a card
    // whose clothes all sit in one pack no longer waits for one helper to decode them one after another.
    let groups: Vec<Vec<_>> = groups.into_iter().flat_map(|(_, mut g)| {
        if g.len() <= 2 { return vec![g]; }
        let rest = g.split_off(g.len().div_ceil(2));
        vec![g, rest]
    }).collect();
    let queue = std::sync::Arc::new(std::sync::Mutex::new(groups.into_iter().collect::<std::collections::VecDeque<_>>()));
    let helpers = queue.lock().unwrap_or_else(|e| e.into_inner()).len().min(PREFETCH_HELPERS);
    if profile_enabled() { let sizes: Vec<usize> = queue.lock().unwrap_or_else(|e| e.into_inner()).iter().map(|g| g.len()).collect(); eprintln!("ok-extract: PROFILE prefetch {} groups {sizes:?} items, {helpers} helpers", sizes.len()); }
    for _ in 0..helpers {
        let (game_root, switches, queue) = (game_root.to_path_buf(), switches.clone(), queue.clone());
        let pack = crate::oklib::current_pack_scope();
        rayon::spawn(move || {
            // The asking open's card pack (cards opening at once each read their own).
            let _pack = crate::oklib::enter_pack_scope(pack);
            let _switches = crate::profile::scope(&switches);
            let _requested = crate::unity::request_clothes_states(requested);
            let _states = crate::unity::set_session_clothes_states(states);
            loop {
                let Some(group) = queue.lock().unwrap_or_else(|e| e.into_inner()).pop_front() else { break };
                for (key, slot, zip, entry, shake) in group {
                    PREFETCH_WORKER.with(|w| w.set(true));
                    // Only a result made under the same key may stand in for the asking thread's own extraction.
                    let (mine, unlearned) = entry_memo_keys(&game_root, zip.as_deref(), &entry, shake);
                    let same = mine == key || unlearned == key;
                    if !same && profile_enabled() { eprintln!("ok-extract: PROFILE prefetch key differs\n  asked {key}\n  here  {mine}"); }
                    let t_item = std::time::Instant::now();
                    let result = same.then(|| std::panic::catch_unwind(std::panic::AssertUnwindSafe(||
                        meshes_for_entry_shake(&game_root, zip.as_deref(), &entry, shake))).ok()).flatten();
                    PREFETCH_WORKER.with(|w| w.set(false));
                    if profile_enabled() { eprintln!("ok-extract: PROFILE prefetch item {} {} {:.1}ms", entry.main_ab, entry.main_data, t_item.elapsed().as_secs_f64() * 1e3); }
                    *slot.0.lock().unwrap_or_else(|e| e.into_inner()) = Some(result);
                    slot.1.notify_all();
                }
                // Recording a card pack keeps every cache as it was (the pack records the queries that run).
                if !crate::oklib::recording_card_pack() { crate::unity::release_thread_bundle_cache(); }
            }
        });
    }
}

/// Prefetch helpers working at once on one card's items: as fast as one helper per group on the 10-card check
/// (every source bundle of a card parsed at once), with about a quarter less memory per card.
pub(crate) const PREFETCH_HELPERS: usize = 8;

pub(crate) struct PrefetchGuard;
impl Drop for PrefetchGuard { fn drop(&mut self) { clear_entry_prefetch(); } }

/// Drops prefetched items nobody asked for (end of an extraction), after their helpers finish: a helper must not
/// outlive the extraction's card pack mount (the next card's pack would answer its reads).
pub(crate) fn clear_entry_prefetch() {
    let slots = ENTRY_PREFETCH.with(|cell| cell.borrow_mut().take());
    for slot in slots.into_iter().flat_map(|s| s.into_values()) {
        let mut done = slot.0.lock().unwrap_or_else(|e| e.into_inner());
        while done.is_none() { done = slot.1.wait(done).unwrap_or_else(|e| e.into_inner()); }
    }
}

pub(crate) fn meshes_for_entry_shake(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    shake_enabled: bool,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    use crate::memosnap::{MESH,active,get,put};
    // Recorded into / replayed from the card (OK_CARD_MEMO=mesh), under the in-process memo's key.
    if !active(MESH) { return meshes_for_entry_shake_memo(game_root, zip, entry, shake_enabled); }
    let (memo_key, _) = entry_memo_key_for(game_root, zip, entry, shake_enabled);
    if let Some(bytes) = get(MESH, memo_key.as_bytes()) {
        if let Ok(hit) = bincode::deserialize::<EntryMeshes>(&bytes) { return hit; }
    }
    let result = meshes_for_entry_shake_memo(game_root, zip, entry, shake_enabled);
    put(MESH, memo_key.as_bytes(), || bincode::serialize(&result).ok());
    result
}

pub(crate) fn meshes_for_entry_shake_memo(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    shake_enabled: bool,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let (memo_key, _) = entry_memo_key_for(game_root, zip, entry, shake_enabled);
    if let Some(hit) = ENTRY_MEMO.with(|m| m.borrow().get(&memo_key).cloned()) {
        return hit;
    }
    if !PREFETCH_WORKER.with(|w| w.get()) {
        let slot = ENTRY_PREFETCH.with(|cell| cell.borrow_mut().as_mut().and_then(|s| s.remove(&memo_key)
            .or_else(|| s.remove(&entry_memo_keys(game_root, zip, entry, shake_enabled).1))));
        let _prefetch_spot = crate::profile::spot(if slot.is_some() { "prefetch hit (wait)" } else { "prefetch miss" });
        if slot.is_none() && profile_enabled() && !entry.main_data.is_empty() {
            let keys: Vec<String> = ENTRY_PREFETCH.with(|cell| cell.borrow().as_ref().map(|s| s.keys().cloned().collect()).unwrap_or_default());
            let near = keys.iter().find(|k| k.contains(&entry.main_data)).cloned().unwrap_or_default();
            eprintln!("ok-extract: prefetch miss {} {} zip={:?} shake={shake_enabled}\n  key ={memo_key}\n  near={near}", entry.main_ab, entry.main_data, zip);
        }
        if let Some(slot) = slot {
            let mut done = slot.0.lock().unwrap_or_else(|e| e.into_inner());
            while done.is_none() { done = slot.1.wait(done).unwrap_or_else(|e| e.into_inner()); }
            // Handed over as is: copying the meshes (texture bytes included, tens of MB) into this thread's memo
            // cost more than the extraction saved when an extraction asks for an item once. Within a view that is
            // opened again (a conversion opens every outfit, extract_session keeps the memo) it is asked again:
            // there the copy is kept, or every outfit extracted the prefetched items anew.
            if let Some(Some(hit)) = done.take() {
                if !crate::oklib::mounted_pack_identity().is_empty() {
                    // The helper's extraction named the prefab's state nodes: the other garment states ask under that key.
                    let learned = entry_memo_key_for(game_root, zip, entry, shake_enabled).0;
                    ENTRY_MEMO.with(|m| m.borrow_mut().insert(learned, hit.clone()));
                }
                return hit;
            }
        }
    }
    let (_, sql_key) = entry_memo_key_for(game_root, zip, entry, shake_enabled);
    if let Some(hit) = crate::sqlcache::load_prefab(game_root, &sql_key, &entry.main_data) {
        ENTRY_MEMO.with(|m| m.borrow_mut().insert(memo_key, hit.clone()));
        return hit;
    }
    let t0 = Instant::now();
    let (result, nodes) = crate::unity::collect_state_nodes(|| meshes_for_entry_inner(game_root, zip, entry, shake_enabled));
    learn_state_nodes(game_root, zip, entry, shake_enabled, nodes);
    profile_tick(
        &format!(
            "meshes_for_entry {} {} n={}",
            entry.main_ab,
            entry.main_data,
            result.0.len()
        ),
        t0,
    );
    if !result.0.is_empty() && !sql_key.is_empty() && !entry.main_data.is_empty() {
        crate::sqlcache::store_prefab(
            game_root,
            &sql_key,
            &entry.main_data,
            &result.0,
            result.1.as_ref(),
        );
        // A helper thread's memo would only grow (it hands its result over instead).
        if !PREFETCH_WORKER.with(|w| w.get()) {
            let learned = entry_memo_key_for(game_root, zip, entry, shake_enabled).0;
            ENTRY_MEMO.with(|m| m.borrow_mut().insert(learned, result.clone()));
        }
    }
    result
}

pub(crate) fn meshes_for_entry_inner(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    shake_enabled: bool,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    if entry.main_data.eq_ignore_ascii_case("p_dummy") {
        return (Vec::new(), None);
    }
    let zip_bytes = accessory_bundle_source(game_root, zip, entry)
        .and_then(|(bytes,source)|source.map(|source|(source,bytes)));
    if let Some((source_zip,b)) = zip_bytes {
        {
            let (mut meshes, mut skel) = if entry.main_data.is_empty() {
                (Vec::new(), None)
            } else {
                crate::unity::meshes_skel_for_maindata_ref_with_category(&b, &entry.main_data, entry.category_no)
            };
            // ChaControl.ChangeHairNoAsync: hiPoly runs ChangeShakeHair
            // (DynamicBone on). Only !hiPoly && kind==1 disables it.
            hang_part_dynamic_bones(game_root, Some(&source_zip), &b, &mut meshes, skel.as_mut(),
                matches!(entry.category_no, 101..=112) || role_for_category(entry.category_no, "").starts_with("clothes") || role_for_category(entry.category_no, "").contains("acc"), shake_enabled);
            let kept: Vec<_> = meshes
                .iter()
                .filter(|m| !is_dummy_part(&m.name, Some(&entry.name)))
                .cloned()
                .collect();
            if kept.is_empty() {
                eprintln!(
                    "ok-extract: zip bundle parsed but 0 meshes ab={} data={} raw={}",
                    entry.main_ab,
                    entry.main_data,
                    meshes.len()
                );
                for m in &meshes {
                    eprintln!(
                        "ok-extract:   drop {} dummy={}",
                        m.name,
                        is_dummy_part(&m.name, Some(&entry.name))
                    );
                }
            } else {
                return (kept, skel);
            }
        }
    }
    if entry.main_ab.is_empty() {
        return (Vec::new(), None);
    }
    let path = game_root.join("abdata").join(&entry.main_ab);
    let Ok(bytes) = crate::gamefs::read_source(&path) else {
        return (Vec::new(), None);
    };
    let (mut meshes, mut skel) = if !entry.main_data.is_empty() {
        crate::unity::meshes_skel_for_maindata_ref_with_category(&bytes, &entry.main_data, entry.category_no)
    } else {
        let (m, _, s) = crate::unity::parse_bundle_ref(&bytes).decode_all();
        (m, s)
    };
    hang_part_dynamic_bones(game_root, None, &bytes, &mut meshes, skel.as_mut(),
        matches!(entry.category_no, 101..=112) || role_for_category(entry.category_no, "").starts_with("clothes") || role_for_category(entry.category_no, "").contains("acc"), shake_enabled);
    let meshes: Vec<_> = meshes
        .into_iter()
        .filter(|m| !is_dummy_part(&m.name, Some(&entry.name)))
        .collect();
    if !meshes.is_empty() {
        return (meshes, skel);
    }
    (meshes_for_maindata(bytes, &entry.main_data), skel)
}

pub(crate) fn hang_part_dynamic_bones(
    game_root: &Path,
    source_zip:Option<&Path>,
    bytes: &crate::unity::SourceBytes,
    meshes: &mut [ExtractedMesh],
    skel: Option<&mut crate::pose::Skeleton>,
    defer_accessory: bool,
    shake_enabled: bool,
) {
    let parsed = crate::unity::parse_bundle_ref(bytes);
    parsed.configure_game_references_from_zip(game_root,source_zip);
    let source_set = |sk: &crate::pose::Skeleton| {
        let set = parsed.dynamic_bones_for_skeleton(sk).with_game_fixes(game_root);
        if shake_enabled { set } else { set.after_no_shake() }
    };
    for m in meshes.iter_mut() {
        if let Some(sk) = m.smr_skel.as_mut() {
            m.bone_implants = parsed.bone_implants_for_skeleton(sk);
            let set = source_set(sk);
            if !set.is_empty() {
                m.deferred_dbones = Some(set.clone());
                if !defer_accessory { crate::dbone::hang_set(&set, sk); }
            }
        }
    }
    if let Some(sk) = skel.filter(|_| !defer_accessory) {
        let set = source_set(sk);
        if !set.is_empty() { crate::dbone::hang_set(&set, sk); }
    }
}

pub(crate) fn is_dummy_part(mesh_name: &str, display: Option<&str>) -> bool {
    // ChaControl.ChangeHairNoAsync / ChangeAccessoryNoAsync skip only
    // ListInfo MainData == "p_dummy".
    let n = mesh_name.to_ascii_lowercase();
    let d = display.unwrap_or("").to_ascii_lowercase();
    n.contains("p_dummy") || d.contains("p_dummy")
}

pub(crate) const EXTRACT_CACHE_VER: &str = "v-sacbody-353-native-mirrored-lossy-scale";
/// `o_body_a` SMR bone order for the mod-body skin data (`m_Skin` indices).
pub(crate) const SAC_BODY_BONES: &str = include_str!("../data/sac_body_bones.txt");

pub(crate) fn stamp_text(game_root: &Path, card: &Path, outfit: i32) -> String {
    let meta = crate::gamefs::metadata(card).ok();
    let len = meta.as_ref().map(|m| m.len()).unwrap_or(0);
    let mt = meta
        .and_then(|m| m.modified().ok())
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|d| d.as_micros())
        .unwrap_or(0);
    let me_cfg = game_root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    let sideloader_stamp=crate::gamefs::metadata(game_root.join("BepInEx/config/com.bepis.bepinex.sideloader.cfg")).ok().and_then(|m|Some((m.len(),m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_micros())));
    let me_stamp = crate::gamefs::metadata(me_cfg).ok().and_then(|m| {
        let modified = m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_micros();
        Some((m.len(), modified))
    });
    let me_plugins: Vec<_> = ["BepInEx/plugins/KKS_Plugins/KKS_MaterialEditor.dll", "BepInEx/plugins/KKS_MaterialEditor.dll"]
        .iter().map(|path| crate::gamefs::metadata(game_root.join(path)).ok().and_then(|m| {
            let modified = m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_micros();
            Some((m.len(), modified))
        })).collect();
    format!(
        "{EXTRACT_CACHE_VER}|{len}|{mt}|{outfit}|{}|{}|mods={}|dynamicfix={}|implant={}|materialeditor={me_stamp:?}|materialeditorplugin={me_plugins:?}|sideloader={sideloader_stamp:?}|references={}",
        card.display(), crate::sqlcache::env_sig(),
        crate::sqlcache::mods_inventory_sig(&game_root.join("mods")), crate::dbone::dynamic_bones_fix_enabled(game_root),
        crate::dbone::implant::installed(game_root),crate::unity::references::begin_load(game_root)
    )
}

pub(crate) fn stamp_fresh(game_root: &Path, out_dir: &Path, card: Option<&Path>, outfit: i32) -> bool {
    let Some(card) = card else {
        return false;
    };
    let Ok(s) = crate::gamefs::read_to_string(out_dir.join(".ok_stamp")) else {
        return false;
    };
    s.trim() == stamp_text(game_root, card, outfit)
        && out_dir.join("body.obj").ok_is_file()
        && out_dir.join("manifest.json").ok_is_file()
}

pub(crate) fn png_from_ab(
    game_root: &Path,
    ab: &str,
    names: &[&str],
    cached: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    if let Some((_, b)) = pick_named_png(cached, names) {
        return Some(b);
    }
    if ab.is_empty() || ab == "0" {
        return None;
    }
    let path = game_root.join("abdata").join(ab);
    named_png_from_bundle(&path, names).map(|(_, b)| b)
}

/// Load a texture referenced by a list entry. Mod entries record their source
/// zipmod in `__zip`, so their assetbundles are read straight from the zipmod.
pub(crate) fn png_from_list_entry(
    game_root: &Path,
    entry: &ListEntry,
    names: &[&str],
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
) -> Option<Vec<u8>> {
    png_from_bundle(
        game_root,
        &entry.main_ab,
        entry.extra.get("__zip").map(|s| s.as_str()),
        names,
        head_texs,
        body_texs,
    )
}

/// `Character.CreateChara(..., hiPoly)`: preview and Studio use the high-poly
/// texture path. The game dump's bra/inner create RTs are 1024-wide, matching
/// the base texture rather than the 512-wide `_low` variant. ActionGame's
/// low-poly path remains available with `OK_FACE_HIPOLY=0`.
pub(crate) fn character_hi_poly() -> bool {
    std::env::var("OK_FACE_HIPOLY")
        .map(|v| v != "0")
        .unwrap_or(true)
}

pub(crate) fn meshes_for_accessory_entry(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    card: &CardInfo,
    outfit: i32,
    property: &str,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let (mut meshes, skel) = meshes_for_accessory_entry_prefab(game_root, zip, entry, card, outfit, property);
    apply_dynamic_bone_editor(&mut meshes, card, outfit, property);
    (meshes, skel)
}

/// DynamicBoneEditor sets the slot's component parameters on the instantiated accessory (after the prefab, per card
/// and outfit): applied to the slot's meshes, never inside the per-entry memo the prefab meshes come from. Every
/// accessory source path calls it (accessory_extract::emit too); setting the same values again is harmless.
pub(crate) fn apply_dynamic_bone_editor(meshes: &mut [ExtractedMesh], card: &CardInfo, outfit: i32, property: &str) {
    if let Some(slot) = accessory_slot_from_property(property) {
        let edits: Vec<&crate::dbone::DynamicBoneEdit> = card.dynamic_bone_edits.iter().filter(|e| e.coordinate == outfit && e.slot == slot as i32).collect();
        if !edits.is_empty() {
            for m in meshes.iter_mut() {
                if let (Some(set), Some(sk)) = (m.deferred_dbones.as_mut(), m.smr_skel.as_ref()) {
                    let changed = set.apply_editor(&edits, sk);
                    if changed > 0 && std::env::var_os("OK_DBONE_DIAG").is_some() { eprintln!("ok-extract: DynamicBoneEditor slot {slot} outfit {outfit} {}: {changed} component(s)", m.name); }
                }
            }
        }
    }
}

pub(crate) fn meshes_for_accessory_entry_prefab(
    game_root: &Path,
    zip: Option<&Path>,
    entry: &ListEntry,
    card: &CardInfo,
    outfit: i32,
    property: &str,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let slot = accessory_slot_from_property(property);
    let shake_enabled = slot.and_then(|s| card.accessory.get(s))
        .is_none_or(|part| !part.no_shake);
    let normal = meshes_for_entry_shake(game_root, zip, entry, shake_enabled);
    let Some(slot) = slot else { return normal };
    let has_renderer_overrides = card.me_renderer_on.iter().any(|(ot, coord, s, _)| {
        *ot == 2 && *coord == outfit && *s == slot as i32
    });
    if !has_renderer_overrides {
        return normal;
    }
    // Material Editor applies renderer visibility after the prefab is loaded.
    // Only explicit Enabled=1 may restore a default-disabled candidate.
    // An Enabled=0 row for another renderer must not enable every sibling.
    let Some(shared) = accessory_bundle_shared(game_root, zip, entry) else { return normal };
    let (bytes,source_zip)=(&shared.0,shared.1.clone());
    let parsed = crate::unity::parse_bundle_ref(bytes);
    let (mut meshes, mut skel) = parsed.meshes_for_maindata_including_disabled(&entry.main_data);
    if meshes.is_empty() {
        return normal;
    }
    meshes.retain(|m| normal.0.iter().any(|n|n.path_id==m.path_id&&n.renderer_ids.iter().any(|id|m.renderer_ids.contains(id))) ||
        m.renderer_names.iter().any(|name|card.me_renderer_on.contains(&(2,outfit,slot as i32,name.to_ascii_lowercase()))));
    hang_part_dynamic_bones(game_root, source_zip.as_deref(), &bytes, &mut meshes, skel.as_mut(), true, shake_enabled);
    meshes.retain(|m| !is_dummy_part(&m.name, Some(&entry.name)));
    (meshes, skel)
}
