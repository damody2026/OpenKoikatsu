//! SQLite cache for zipmod catalogs and already-parsed bundles / sessions.
//!
//! First launch seeds from the existing `.ok_list_cache_*.tsv` / guid / override
//! files when they are newer than every zipmod. Later launches read SQL.
//! Changed zipmods are the only ones reopened.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

use rusqlite::{params, Connection};

use crate::list::{catalog_from_zipmod, ChaListCatalog, ListEntry};
use crate::pose::Skeleton;
use crate::unity::ExtractedMesh;
mod inventory;

pub const CACHE_VER: i32 = 108;
const DEFAULT_CACHE_MAX_BYTES: u64 = 4 * 1024 * 1024 * 1024;

fn cache_budget() -> u64 {
    std::env::var("OK_CACHE_MAX_MB").ok().and_then(|v| v.parse::<u64>().ok())
        .map(|mb| mb.clamp(64, 16384) * 1024 * 1024).unwrap_or(DEFAULT_CACHE_MAX_BYTES)
}

fn configure_cache_budget(conn: &Connection, bytes: u64) -> rusqlite::Result<()> {
    let page_size: u64 = conn.pragma_query_value(None, "page_size", |row| row.get(0))?;
    conn.pragma_update(None, "max_page_count", (bytes / page_size.max(1)).max(256))?;
    conn.pragma_update(None, "wal_autocheckpoint", 1024)?;
    conn.pragma_update(None, "journal_size_limit", 64 * 1024 * 1024)?;
    Ok(())
}
// One eviction order covers both kinds of disposable acceleration blobs.
// Source catalogs are never eviction candidates. Legacy rows have age zero.
fn evict_blob(conn: &Connection, protected: (&str, &str, &str)) -> rusqlite::Result<bool> {
    let tracked: bool = conn.query_row("SELECT count(*) FROM sqlite_master WHERE name='blob_usage'", [], |r| r.get::<_, i64>(0)).unwrap_or(0) != 0;
    let candidates = "SELECT 'session' kind,card_key k1,'' k2,length(payload) bytes FROM session_blob
        UNION ALL SELECT 'prefab',bundle_key,prefab,length(payload) FROM bundle_prefab";
    let sql = if tracked { format!("SELECT c.kind,c.k1,c.k2 FROM ({candidates}) c LEFT JOIN blob_usage u
        ON u.kind=c.kind AND u.k1=c.k1 AND u.k2=c.k2
        WHERE NOT(c.kind=?1 AND c.k1=?2 AND c.k2=?3)
        ORDER BY coalesce(u.used,0),c.bytes DESC,c.kind,c.k1,c.k2 LIMIT 1") }
        else { format!("SELECT kind,k1,k2 FROM ({candidates}) WHERE NOT(kind=?1 AND k1=?2 AND k2=?3)
        ORDER BY bytes DESC,kind,k1,k2 LIMIT 1") };
    let candidate = conn.query_row(&sql, params![protected.0,protected.1,protected.2], |r|
        Ok((r.get::<_,String>(0)?,r.get::<_,String>(1)?,r.get::<_,String>(2)?)));
    let (kind,k1,k2) = match candidate { Ok(row)=>row, Err(rusqlite::Error::QueryReturnedNoRows)=>return Ok(false), Err(e)=>return Err(e) };
    // Commit reclamation separately: SQLITE_FULL rolls back its failed write.
    let tx=conn.unchecked_transaction()?;
    if kind=="session" { tx.execute("DELETE FROM session_blob WHERE card_key=?1", [&k1])?; }
    else { tx.execute("DELETE FROM bundle_prefab WHERE bundle_key=?1 AND prefab=?2",params![k1,k2])?; }
    if tracked { tx.execute("DELETE FROM blob_usage WHERE kind=?1 AND k1=?2 AND k2=?3",params![kind,k1,k2])?; }
    tx.commit()?;
    Ok(true)
}

fn reserve_blob_space(conn: &Connection, bytes: u64, protected: (&str,&str,&str)) -> rusqlite::Result<bool> {
    for _ in 0..4096 { // Finite even for a corrupt or externally busy cache.
        let size:u64=conn.pragma_query_value(None,"page_size",|r|r.get(0))?;
        let pages:u64=conn.pragma_query_value(None,"page_count",|r|r.get(0))?;
        let max:u64=conn.pragma_query_value(None,"max_page_count",|r|r.get(0))?;
        let free:u64=conn.pragma_query_value(None,"freelist_count",|r|r.get(0))?;
        if (max.saturating_sub(pages)+free)*size>=bytes { return Ok(true); }
        if !evict_blob(conn,protected)? { return Ok(false); }
    }
    Ok(false)
}

fn touch_blob(conn:&Connection, kind:&str,k1:&str,k2:&str) {
    let used=SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_micros().min(i64::MAX as u128) as i64;
    // A second process may evict a blob between reading it and this touch.
    // Never leave usage-only records for data that no longer exists.
    let sql=if kind=="session" { "INSERT INTO blob_usage SELECT ?1,?2,?3,?4
        WHERE EXISTS(SELECT 1 FROM session_blob WHERE card_key=?2)
        ON CONFLICT(kind,k1,k2) DO UPDATE SET used=excluded.used" }
        else { "INSERT INTO blob_usage SELECT ?1,?2,?3,?4
        WHERE EXISTS(SELECT 1 FROM bundle_prefab WHERE bundle_key=?2 AND prefab=?3)
        ON CONFLICT(kind,k1,k2) DO UPDATE SET used=excluded.used" };
    if let Err(error)=conn.execute(sql,params![kind,k1,k2,used]) {
        eprintln!("ok-extract: cache usage update failed: {error}");
    }
}

fn store_blob(conn:&Connection,kind:&str,k1:&str,k2:&str,bytes:&[u8]) -> rusqlite::Result<bool> {
    let size:u64=conn.pragma_query_value(None,"page_size",|r|r.get(0))?;
    let max:u64=conn.pragma_query_value(None,"max_page_count",|r|r.get(0))?;
    let budget=size*max;
    // Do not evict the working set for an entry that dominates the whole budget.
    if bytes.len() as u64>budget/2 { return Ok(false); }
    let headroom=(budget/16).min(1024*1024).max(65536);
    let required=bytes.len() as u64+headroom+((k1.len()+k2.len()) as u64)*2;
    if !reserve_blob_space(conn,required,(kind,k1,k2))? { return Ok(false); }
    for _ in 0..2 {
        let result=if kind=="session" { conn.execute("INSERT INTO session_blob VALUES(?1,?2)
            ON CONFLICT(card_key) DO UPDATE SET payload=excluded.payload",params![k1,bytes]) }
        else { conn.execute("INSERT INTO bundle_prefab VALUES(?1,?2,?3)
            ON CONFLICT(bundle_key,prefab) DO UPDATE SET payload=excluded.payload",params![k1,k2,bytes]) };
        match result {
            Ok(_)=>{ touch_blob(conn,kind,k1,k2);return Ok(true); }
            Err(e) if e.sqlite_error_code()==Some(rusqlite::ErrorCode::DiskFull)=>{
                if !evict_blob(conn,(kind,k1,k2))? { return Ok(false); }
            }
            Err(e)=>return Err(e),
        }
    }
    Ok(false)
}

// SessionPart material-family inference changes without changing decoded
// bundles or zip catalogs. Keep its key separate so those caches stay warm.
// 250 keeps the separate penis/scrotum renderers and bakes the installed
// UncensorSelector skin (its main texture, color mask, and the card's skin
// colors). Mosaic Penis / Mosaic Balls are not selected. CACHE_VER stays 108.
// 251 resolves BOM-encoded Sideloader manifests; old missing-mod sessions
// must not mask the corrected GUID lookup. Bundle bytes themselves are unchanged.
// 254 captures exact accessory extraction inputs and normalizes effective worn
// states in their dependency signature; prefab/catalog caches stay valid.
// 255 canonicalizes serialized/selected HairGloss rows with other shader maps.
// 256 loads hair parts with id 0 (vanilla back hair 0 was skipped as empty).
// 257 places hair under cf_J_FaceUp_ty at open (chain-less states are rebuilt on the attachment chain).
// Original-card opens no longer read or write session_blob; the stored-session diagnostics in
// session_renderer_tests still report this version with the rows they find.
#[allow(dead_code)]
pub const SESSION_PIPELINE_VER: i32 = 257;

struct ModsMem {
    sql_valid: bool,
    dir: PathBuf,
    sig: String,
    // Shared, not copied per lookup: a card open asks for these several times and each copy of the full
    // catalogue cost close to a second.
    catalog: Arc<ChaListCatalog>,
    guids: Arc<HashMap<String, PathBuf>>,
    overrides: Arc<HashMap<String, PathBuf>>,
}

static DB: Mutex<Option<(PathBuf, Connection)>> = Mutex::new(None);
static MODS: Mutex<Option<Arc<ModsMem>>> = Mutex::new(None);

pub fn disabled() -> bool {
    // Recording/replaying the OK base library must see every Unity query.
    std::env::var("OK_NOCACHE").is_ok() || (crate::oklib::active().is_some() && !library_ignored_here())
}
// Tests share one process: another test activating the OK base library must not switch off the cache under a test
// of the cache itself. Such a test opts out on its own thread.
#[cfg(test)]
thread_local! { pub(crate) static TEST_IGNORE_LIBRARY: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
#[cfg(test)]
fn library_ignored_here() -> bool { TEST_IGNORE_LIBRARY.with(|c| c.get()) }
#[cfg(not(test))]
fn library_ignored_here() -> bool { false }

fn game_root_of_mods(mods_dir: &Path) -> PathBuf {
    mods_dir
        .parent()
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| mods_dir.to_path_buf())
}

pub fn db_path(game_root: &Path) -> PathBuf {
    game_root.join(".ok_cache.sqlite")
}

fn with_db<R>(game_root: &Path, f: impl FnOnce(&mut Connection) -> R) -> R {
    let path = db_path(game_root);
    let mut slot = DB.lock().unwrap_or_else(|e| e.into_inner());
    let reopen = match slot.as_ref() {
        Some((p, _)) => p != &path,
        None => true,
    };
    if reopen {
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        let conn = Connection::open(&path).unwrap_or_else(|e| {
            panic!("ok-extract: sqlite open {}: {e}", path.display());
        });
        let _ = conn.pragma_update(None, "journal_mode", "WAL");
        let _ = conn.pragma_update(None, "synchronous", "NORMAL");
        let _ = conn.busy_timeout(std::time::Duration::from_secs(30));
        // Bound disk growth and reclaim only disposable blobs when space runs out.
        if let Err(error) = configure_cache_budget(&conn, cache_budget()) {
            eprintln!("ok-extract: cache budget configuration failed: {error}");
        }
        ensure_schema(&conn);
        *slot = Some((path, conn));
    }
    f(&mut slot.as_mut().unwrap().1)
}

fn ensure_schema(conn: &Connection) {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS zip_file (
            path TEXT PRIMARY KEY,
            len INTEGER NOT NULL,
            mtime INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS zip_guid (
            path TEXT NOT NULL,
            guid TEXT NOT NULL,
            PRIMARY KEY (path, guid)
        );
        CREATE TABLE IF NOT EXISTS list_entry (
            zip_path TEXT NOT NULL,
            category INTEGER NOT NULL,
            id INTEGER NOT NULL,
            name TEXT NOT NULL,
            main_ab TEXT NOT NULL,
            main_data TEXT NOT NULL,
            main_tex TEXT NOT NULL,
            parent TEXT NOT NULL,
            extra TEXT NOT NULL,
            PRIMARY KEY (zip_path, category, id)
        );
        CREATE TABLE IF NOT EXISTS override_ab (
            ab_key TEXT PRIMARY KEY,
            zip_path TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS bundle_prefab (
            bundle_key TEXT NOT NULL,
            prefab TEXT NOT NULL,
            payload BLOB NOT NULL,
            PRIMARY KEY (bundle_key, prefab)
        );
        CREATE TABLE IF NOT EXISTS session_blob (
            card_key TEXT PRIMARY KEY,
            payload BLOB NOT NULL
        );
        ",
    )
    .unwrap_or_else(|error| eprintln!("ok-extract: cache schema failed: {error}"));
    // Existing full databases need reusable pages before adding usage metadata.
    if let Err(error) = (|| -> rusqlite::Result<()> {
        if !reserve_blob_space(conn,65536,("","",""))? { return Ok(()); }
        conn.execute_batch("CREATE TABLE IF NOT EXISTS blob_usage (
            kind TEXT NOT NULL,k1 TEXT NOT NULL,k2 TEXT NOT NULL,used INTEGER NOT NULL,
            PRIMARY KEY(kind,k1,k2));
            CREATE TRIGGER IF NOT EXISTS session_usage_delete AFTER DELETE ON session_blob BEGIN
                DELETE FROM blob_usage WHERE kind='session' AND k1=old.card_key; END;
            CREATE TRIGGER IF NOT EXISTS prefab_usage_delete AFTER DELETE ON bundle_prefab BEGIN
                DELETE FROM blob_usage WHERE kind='prefab' AND k1=old.bundle_key AND k2=old.prefab; END;")?;
        Ok(())
    })() { eprintln!("ok-extract: cache eviction initialization failed: {error}"); }
}

fn meta_get(conn: &Connection, k: &str) -> Option<String> {
    conn.query_row("SELECT v FROM meta WHERE k=?1", [k], |r| r.get(0))
        .ok()
}

fn meta_set(conn: &Connection, k: &str, v: &str) {
    let _ = conn.execute(
        "INSERT INTO meta(k,v) VALUES(?1,?2) ON CONFLICT(k) DO UPDATE SET v=excluded.v",
        params![k, v],
    );
}

fn file_stamp(path: &Path) -> Option<(i64, i64)> {
    metadata_stamp(&crate::gamefs::metadata(path).ok()?)
}

fn metadata_stamp(m: &crate::gamefs::Metadata) -> Option<(i64, i64)> {
    let len = m.len() as i64;
    let mtime = m
        .modified()
        .ok()?
        .duration_since(UNIX_EPOCH)
        .ok()?
        .as_micros().try_into().ok()?;
    Some((len, mtime))
}

fn same_zip_stamp(old_len: i64, old_mt: i64, len: i64, mt: i64) -> bool {
    old_len == len && (old_mt == mt
        // Earlier cache versions stored whole seconds. Migrate matching rows
        // in place; unfamiliar timestamp formats are reopened conservatively.
        || (old_mt >= 0 && old_mt < 10_000_000_000 && old_mt == mt / 1_000_000))
}

fn zip_path_identity(path: &Path) -> String {
    let value=path.to_string_lossy();
    #[cfg(windows)]
    { value.replace('\\',"/") }
    #[cfg(not(windows))]
    { value.into_owned() }
}

fn stamp_zips(mods_dir: &Path) -> Vec<(PathBuf, i64, i64)> {
    stamp_zips_checked(mods_dir).0
}

fn stamp_zips_checked(mods_dir: &Path) -> (Vec<(PathBuf, i64, i64)>, bool) {
    // On Windows DirEntry metadata comes from directory enumeration. Avoid
    // reopening every entry for is_dir and every zip again for its stamp.
    // Follow links exactly as the previous path-based walker did.
    fn rec(dir:&Path,out:&mut Vec<(PathBuf,i64,i64)>,complete:&mut bool)->std::io::Result<()> {
        for entry in crate::gamefs::read_dir(dir)? {
            let entry=entry?;let path=entry.path();let kind=entry.file_type().ok();
            let metadata=entry.metadata();
            #[cfg(windows)]
            let linked={metadata.as_ref().map(|m|m.file_attributes()&0x400!=0).unwrap_or(true)};
            #[cfg(not(windows))]
            let linked=kind.as_ref().map(|k|k.is_symlink()).unwrap_or(true);
            let linked=linked||kind.is_none();
            if linked{*complete=false;}
            let is_dir=if linked {path.ok_is_dir()}else{kind.as_ref().unwrap().ok_is_dir()};
            if is_dir {rec(&path,out,complete)?;}
            else if path.extension().and_then(|e|e.to_str()).map(|e|e.eq_ignore_ascii_case("zipmod")).unwrap_or(false) {
                let metadata=if linked {crate::gamefs::metadata(&path)}else{metadata};
                if let Some((len,mt))=metadata.ok().and_then(|m|metadata_stamp(&m)) {out.push((path,len,mt));}else{*complete=false;}
            }
        }
        Ok(())
    }
    let mut out=Vec::new();let mut complete=true;
    // all_zipmods historically discards a partial traversal on an I/O error.
    if rec(mods_dir,&mut out,&mut complete).is_err() {out.clear();complete=false;}
    out.sort_by_key(|row| zip_path_identity(&row.0));
    (out,complete)
}

fn sig_of(stamps: &[(PathBuf, i64, i64)]) -> String {
    // Count + newest mtime misses an edit to any older zipmod. Keep a stable
    // digest of every sorted path/size/mtime so mod catalog, card sessions,
    // and export stamps share the same invalidation boundary.
    let mut hash = 0xcbf29ce484222325u64;
    let mut add = |bytes: &[u8]| {
        for &byte in bytes {
            hash ^= byte as u64;
            hash = hash.wrapping_mul(0x100000001b3);
        }
    };
    for (path, len, mt) in stamps {
        add(zip_path_identity(path).as_bytes());
        add(&[0]);
        add(&len.to_le_bytes());
        add(&mt.to_le_bytes());
    }
    format!("n={};hash={hash:016x};v={CACHE_VER}", stamps.len())
}

pub fn mods_inventory_sig(mods_dir: &Path) -> String {
    inventory::get(mods_dir).sig.clone()
}

fn newest_file(dir: &Path, pred: impl Fn(&Path) -> bool) -> Option<PathBuf> {
    let mut best: Option<(SystemTime, PathBuf)> = None;
    let rd = crate::gamefs::read_dir(dir).ok()?;
    for ent in rd.flatten() {
        let p = ent.path();
        if !pred(&p) {
            continue;
        }
        let mt = ent.metadata().and_then(|m| m.modified()).ok();
        if let Some(mt) = mt {
            if best.as_ref().map(|(t, _)| mt > *t).unwrap_or(true) {
                best = Some((mt, p));
            }
        }
    }
    best.map(|(_, p)| p)
}

fn seed_list_only(conn: &Connection, mods_dir: &Path, max_mt: i64) -> bool {
    let list = newest_file(mods_dir, |p| {
        p.file_name()
            .and_then(|n| n.to_str())
            .map(|n| n.starts_with(".ok_list_cache_v2_") && n.ends_with(".tsv"))
            .unwrap_or(false)
    });
    let Some(list) = list else {
        return false;
    };
    let list_mt = file_stamp(&list).map(|(_, mt)| mt).unwrap_or(0);
    if list_mt < max_mt {
        return false;
    }
    let Some(cat) = crate::list::catalog_from_cache_tsv(&list) else {
        return false;
    };
    eprintln!(
        "ok-extract: sql seed list entries={} from {}",
        cat.by_id.len(),
        list.display()
    );
    insert_catalog(conn, &cat)
}

fn try_seed(conn: &mut Connection, mods_dir: &Path, stamps: &[(PathBuf, i64, i64)]) -> bool {
    let max_mt = stamps.iter().map(|(_, _, mt)| *mt).max().unwrap_or(0);
    let list = newest_file(mods_dir, |p| {
        p.file_name()
            .and_then(|n| n.to_str())
            .map(|n| n.starts_with(".ok_list_cache_v2_") && n.ends_with(".tsv"))
            .unwrap_or(false)
    });
    let Some(list) = list else {
        return false;
    };
    let list_mt = file_stamp(&list).map(|(_, mt)| mt).unwrap_or(0);
    if list_mt < max_mt {
        return false;
    }
    let guid_path = mods_dir.join(".ok_guid_index.txt");
    let over_path = mods_dir.join(".ok_override_index.tsv");
    let guid_mt = file_stamp(&guid_path).map(|(_, mt)| mt).unwrap_or(0);
    if guid_mt < max_mt {
        return false;
    }
    let _ = over_path;
    let Some(cat) = crate::list::catalog_from_cache_tsv(&list) else {
        return false;
    };
    eprintln!(
        "ok-extract: sql seed catalog from {} entries={}",
        list.display(),
        cat.by_id.len()
    );
    let tx = match conn.transaction() {
        Ok(t) => t,
        Err(_) => return false,
    };
    if tx.execute("DELETE FROM zip_file", []).is_err()
        || tx.execute("DELETE FROM zip_guid", []).is_err()
        || tx.execute("DELETE FROM list_entry", []).is_err()
        || tx.execute("DELETE FROM override_ab", []).is_err()
    {
        return false;
    }
    for (path, len, mt) in stamps {
        if tx
            .execute(
                "INSERT INTO zip_file(path,len,mtime) VALUES(?1,?2,?3)",
                params![path.to_string_lossy().as_ref(), len, mt],
            )
            .is_err()
        {
            return false;
        }
    }
    if !insert_catalog(&tx, &cat) {
        return false;
    }
    if let Ok(text) = crate::gamefs::read_to_string(&guid_path) {
        for line in text.lines() {
            let Some((g, p)) = line.split_once('\t') else {
                continue;
            };
            if g == "__FULL__" || g.is_empty() {
                continue;
            }
            if tx.execute(
                "INSERT OR IGNORE INTO zip_guid(path,guid) VALUES(?1,?2)",
                params![p, g],
            ).is_err() { return false; }
        }
    }
    if let Ok(text) = crate::gamefs::read_to_string(&over_path) {
        for line in text.lines() {
            let Some((ab, z)) = line.split_once('\t') else {
                continue;
            };
            if tx.execute(
                "INSERT OR REPLACE INTO override_ab(ab_key,zip_path) VALUES(?1,?2)",
                params![ab, z],
            ).is_err() { return false; }
        }
    }
    tx.commit().is_ok()
}

fn insert_catalog(conn: &Connection, cat: &ChaListCatalog) -> bool {
    for e in cat.by_id.values() {
        let zip = e.extra.get("__zip").cloned().unwrap_or_default();
        let extra = serde_json::to_string(&e.extra).unwrap_or_else(|_| "{}".into());
        if conn
            .execute(
                "INSERT OR REPLACE INTO list_entry(zip_path,category,id,name,main_ab,main_data,main_tex,parent,extra)
                 VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)",
                params![
                    zip,
                    e.category_no,
                    e.id,
                    e.name,
                    e.main_ab,
                    e.main_data,
                    e.main_tex,
                    e.parent,
                    extra
                ],
            )
            .is_err()
        {
            return false;
        }
    }
    true
}

fn ingest_zip(conn: &Connection, path: &Path) -> rusqlite::Result<()> {
    let p = path.to_string_lossy().to_string();
    conn.execute("DELETE FROM list_entry WHERE zip_path=?1", [&p])?;
    conn.execute("DELETE FROM zip_guid WHERE path=?1", [&p])?;
    conn.execute("DELETE FROM override_ab WHERE zip_path=?1", [&p])?;
    let cat = catalog_from_zipmod(path);
    if !insert_catalog(conn, &cat) { return Err(rusqlite::Error::InvalidQuery); }
    for g in crate::mods::guids_from_zipmod(path) {
        conn.execute(
            "INSERT OR IGNORE INTO zip_guid(path,guid) VALUES(?1,?2)",
            params![p, g],
        )?;
    }
    for ab in crate::mods::zipmod_override_keys(path) {
        conn.execute(
            "INSERT OR REPLACE INTO override_ab(ab_key,zip_path) VALUES(?1,?2)",
            params![ab, p],
        )?;
    }
    Ok(())
}

fn incremental(conn: &mut Connection, stamps: &[(PathBuf, i64, i64)]) -> rusqlite::Result<()> {
    let mut known: HashMap<String, (i64, i64, String)> = HashMap::new();
    if let Ok(mut stmt) = conn.prepare("SELECT path,len,mtime FROM zip_file") {
        let rows = stmt.query_map([], |r| {
            Ok((r.get::<_, String>(0)?, r.get::<_, i64>(1)?, r.get::<_, i64>(2)?))
        });
        if let Ok(rows) = rows {
            for row in rows.flatten() {
                known.insert(zip_path_identity(Path::new(&row.0)), (row.1, row.2, row.0));
            }
        }
    }
    let current: HashSet<String> = stamps
        .iter()
        .map(|(p, _, _)| zip_path_identity(p))
        .collect();
    let removed: Vec<String> = known.iter().filter(|(k,_)| !current.contains(*k)).map(|(_,v)|v.2.clone()).collect();
    let mut changed = 0usize;
    let tx = conn.transaction()?;
    for path in &removed {
        tx.execute("DELETE FROM zip_file WHERE path=?1", [path])?;
        tx.execute("DELETE FROM list_entry WHERE zip_path=?1", [path])?;
        tx.execute("DELETE FROM zip_guid WHERE path=?1", [path])?;
        tx.execute("DELETE FROM override_ab WHERE zip_path=?1", [path])?;
    }
    for (path, len, mt) in stamps {
        let key = path.to_string_lossy().to_string();
        if let Some((old_len, old_mt, old_key)) = known.get(&zip_path_identity(path)) {
            if same_zip_stamp(*old_len, *old_mt, *len, *mt) {
                if *old_mt != *mt {
                    tx.execute("UPDATE zip_file SET mtime=?2 WHERE path=?1", params![old_key, mt])?;
                }
                continue;
            }
            // A real edit uses the caller's spelling; remove the old alias
            // and its dependent rows before ingesting the new contents.
            if old_key!=&key {
                for (table,column) in [("zip_file","path"),("list_entry","zip_path"),("zip_guid","path"),("override_ab","zip_path")] {
                    tx.execute(&format!("DELETE FROM {table} WHERE {column}=?1"),[old_key])?;
                }
            }
        }
        changed += 1;
        if changed == 1 || changed % 200 == 0 {
            eprintln!("ok-extract: sql zipmod refresh {changed} {}", path.display());
        }
        tx.execute(
            "INSERT INTO zip_file(path,len,mtime) VALUES(?1,?2,?3)
             ON CONFLICT(path) DO UPDATE SET len=excluded.len, mtime=excluded.mtime",
            params![key, len, mt],
        )?;
        ingest_zip(&tx, path)?;
    }
    if changed > 0 || !removed.is_empty() {
        eprintln!(
            "ok-extract: sql zipmod sync changed={changed} removed={}",
            removed.len()
        );
    }
    tx.commit()
}

fn load_mem(conn: &Connection, mods_dir: &Path, sig: String) -> ModsMem {
    let mut catalog = ChaListCatalog::default();
    if let Ok(mut stmt) = conn.prepare(
        "SELECT zip_path,category,id,name,main_ab,main_data,main_tex,parent,extra FROM list_entry",
    ) {
        let rows = stmt.query_map([], |r| {
            let extra: String = r.get(8)?;
            let extra: HashMap<String, String> =
                serde_json::from_str(&extra).unwrap_or_default();
            Ok(ListEntry {
                category_no: r.get(1)?,
                id: r.get(2)?,
                name: r.get(3)?,
                main_ab: r.get(4)?,
                main_data: r.get(5)?,
                main_tex: r.get(6)?,
                parent: r.get(7)?,
                extra,
            })
        });
        if let Ok(rows) = rows {
            for e in rows.flatten() {
                catalog.by_id.entry((e.category_no, e.id)).or_insert(e);
            }
        }
    }
    let mut guids = HashMap::new();
    if let Ok(mut stmt) = conn.prepare("SELECT guid,path FROM zip_guid") {
        let rows = stmt.query_map([], |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?)));
        if let Ok(rows) = rows {
            for (g, p) in rows.flatten() {
                guids.entry(g).or_insert_with(|| PathBuf::from(p));
            }
        }
    }
    let mut overrides = HashMap::new();
    if let Ok(mut stmt) = conn.prepare("SELECT ab_key,zip_path FROM override_ab") {
        let rows = stmt.query_map([], |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?)));
        if let Ok(rows) = rows {
            for (ab, z) in rows.flatten() {
                overrides.entry(ab).or_insert_with(|| PathBuf::from(z));
            }
        }
    }
    eprintln!(
        "ok-extract: sql mods entries={} guids={} overrides={}",
        catalog.by_id.len(),
        guids.len(),
        overrides.len()
    );
    ModsMem {
        sql_valid: true,
        dir: mods_dir.to_path_buf(),
        sig,
        catalog: Arc::new(catalog),
        guids: Arc::new(guids),
        overrides: Arc::new(overrides),
    }
}

fn ensure_mods(mods_dir: &Path) -> Arc<ModsMem> {
    let started = std::time::Instant::now();
    let snapshot = inventory::get(mods_dir);
    let stamps = &snapshot.stamps;
    let sig = snapshot.sig.clone();
    let scanned = started.elapsed();
    {
        let slot = MODS.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(mem) = slot.as_ref() {
            if mem.dir == mods_dir && mem.sig == sig {
                if std::env::var("OK_PAIR_PROFILE").ok().as_deref()==Some("1") {
                    eprintln!("OK mods lookup scan_ms={:.3} snapshot_ms={:.3} hit=1",scanned.as_secs_f64()*1000.,(started.elapsed()-scanned).as_secs_f64()*1000.);
                }
                return Arc::clone(mem);
            }
        }
    }
    let game_root = game_root_of_mods(mods_dir);
    let mem = with_db(&game_root, |conn| {
        let stored = meta_get(conn, "mods_sig");
        if stored.as_deref() != Some(sig.as_str()) {
            if stored.is_none() && try_seed(conn, mods_dir, &stamps) {
                eprintln!("ok-extract: sql mods seeded, sig={sig}");
            } else {
                if let Err(error) = incremental(conn, &stamps) {
                    // A full/read-only cache must never publish a partial index
                    // as authoritative. None from the public accessors preserves
                    // the existing TSV/zip scan fallback for every lookup kind.
                    eprintln!("ok-extract: SQL catalog refresh failed, using source indexes: {error}");
                    return ModsMem {sql_valid:false,dir:mods_dir.to_path_buf(),sig,
                        catalog:Default::default(),guids:Default::default(),overrides:Default::default()};
                }
            }
            meta_set(conn, "mods_sig", &sig);
        }
        let n: i64 = conn
            .query_row("SELECT COUNT(*) FROM list_entry", [], |r| r.get(0))
            .unwrap_or(0);
        if n == 0 {
            let max_mt = stamps.iter().map(|(_, _, mt)| *mt).max().unwrap_or(0);
            let _ = seed_list_only(conn, mods_dir, max_mt);
        }
        load_mem(conn, mods_dir, sig)
    });
    let mem=Arc::new(mem);
    *MODS.lock().unwrap_or_else(|e| e.into_inner()) = Some(Arc::clone(&mem));
    if std::env::var("OK_PAIR_PROFILE").ok().as_deref()==Some("1") {
        eprintln!("OK mods lookup scan_ms={:.3} snapshot_ms={:.3} hit=0",scanned.as_secs_f64()*1000.,(started.elapsed()-scanned).as_secs_f64()*1000.);
    }
    mem
}

pub fn mods_catalog(mods_dir: &Path) -> Option<ChaListCatalog> {
    if disabled() || !mods_dir.ok_is_dir() {
        return None;
    }
    let mem=ensure_mods(mods_dir);
    mem.sql_valid.then(|| (*mem.catalog).clone())
}
/// The SQL catalogue without copying it (None as mods_catalog).
pub fn mods_catalog_shared(mods_dir: &Path) -> Option<Arc<ChaListCatalog>> {
    if disabled() || !mods_dir.ok_is_dir() {
        return None;
    }
    let mem=ensure_mods(mods_dir);
    mem.sql_valid.then(|| Arc::clone(&mem.catalog))
}

pub fn guid_index(mods_dir: &Path) -> Option<HashMap<String, PathBuf>> {
    if disabled() || !mods_dir.ok_is_dir() {
        return None;
    }
    let mem=ensure_mods(mods_dir);
    mem.sql_valid.then(|| (*mem.guids).clone())
}
/// The SQL GUID index without copying it (None as guid_index).
pub fn guid_index_shared(mods_dir: &Path) -> Option<Arc<HashMap<String, PathBuf>>> {
    if disabled() || !mods_dir.ok_is_dir() {
        return None;
    }
    let mem=ensure_mods(mods_dir);
    mem.sql_valid.then(|| Arc::clone(&mem.guids))
}

pub fn override_index(mods_dir: &Path) -> Option<HashMap<String, PathBuf>> {
    if disabled() || !mods_dir.ok_is_dir() {
        return None;
    }
    let mem=ensure_mods(mods_dir);
    mem.sql_valid.then(|| (*mem.overrides).clone())
}

// Outer None preserves the TSV/scan fallback when SQL is unavailable or empty.
// Some(None) is a valid miss in a populated SQL index. Keep inventory validation
// on every lookup; sharing the snapshot never bypasses changed zipmods.
pub(crate) fn override_source(mods_dir: &Path, ab_key: &str) -> Option<Option<PathBuf>> {
    if disabled() || !mods_dir.ok_is_dir() { return None; }
    let mem=ensure_mods(mods_dir);
    if !mem.sql_valid || mem.overrides.is_empty() { return None; }
    Some(mem.overrides.get(ab_key).cloned())
}

pub fn file_key(path: &Path) -> String {let _spot=crate::profile::spot("file_key");
    match file_stamp(path) {
        Some((len, mt)) => format!("file:{}|{len}|{mt}|v{CACHE_VER}", path.display()),
        None => format!("file:{}|missing|v{CACHE_VER}", path.display()),
    }
}

pub fn zip_key(zip: &Path, ab: &str) -> String {
    match file_stamp(zip) {
        Some((len, mt)) => format!("zip:{}|{ab}|{len}|{mt}|v{CACHE_VER}", zip.display()),
        None => format!("zip:{}|{ab}|missing|v{CACHE_VER}", zip.display()),
    }
}

#[derive(serde::Serialize, serde::Deserialize)]
struct PrefabBlob {
    meshes: Vec<ExtractedMesh>,
    skel: Option<Skeleton>,
}

pub fn load_prefab(
    game_root: &Path,
    key: &str,
    prefab: &str,
) -> Option<(Vec<ExtractedMesh>, Option<Skeleton>)> {let _spot=crate::profile::spot("load_prefab");
    if disabled() || key.is_empty() || prefab.is_empty() {
        return None;
    }
    let blob: Option<Vec<u8>> = with_db(game_root, |conn| {
        let result=conn.query_row(
            "SELECT payload FROM bundle_prefab WHERE bundle_key=?1 AND prefab=?2",
            params![key, prefab],
            |r| r.get(0),
        )
        .ok();
        if result.is_some() { touch_blob(conn,"prefab",key,prefab); }
        result
    });
    let blob = blob?;
    bincode::deserialize::<PrefabBlob>(&blob)
        .ok()
        .map(|p| (p.meshes, p.skel))
}

pub fn store_prefab(
    game_root: &Path,
    key: &str,
    prefab: &str,
    meshes: &[ExtractedMesh],
    skel: Option<&Skeleton>,
) {
    if disabled() || key.is_empty() || prefab.is_empty() || meshes.is_empty() {
        return;
    }
    let blob = PrefabBlob {
        meshes: meshes.to_vec(),
        skel: skel.cloned(),
    };
    let Ok(bytes) = bincode::serialize(&blob) else {
        return;
    };
    with_db(game_root, |conn| {
        match store_blob(conn,"prefab",key,prefab,&bytes) {
            Ok(true)=>{}, Ok(false)=>eprintln!("ok-extract: prefab cache entry exceeds available budget"),
            Err(error)=>eprintln!("ok-extract: prefab cache write failed: {error}"),
        }
    });
}

fn env_sig_with(get: impl Fn(&str) -> String) -> String {
    const KEYS: &[&str] = &[
        "OK_GPU_SKIN",
        "OK_SKELMESH",
        "OK_POSE",
        "OK_POSE_SCENE",
        "OK_NOPOSE",
        "OK_UE_ANIM",
        "OK_FINALIK",
        "OK_FINALIK_HANDS_OFF",
        "OK_NOEXPR",
        "OK_EXPR_ALL",
        "OK_NOFBS",
        "OK_NODB",
        "OK_DB_MOVE",
        "OK_DBSTST",
        "OK_RUNTIME_ACCESSORY",
        "OK_OLDPHEAD",
        "OK_NOCOPYTRF",
        "OK_FACE_FIXES",
        "OK_FACE_HIPOLY",
        "OK_EYE_FLIP",
        "OK_FACE_RAW",
        "OK_FACE_OVERLAY",
        "OK_FACE_FLAT",
        "OK_FACE_CAVITY_FILL",
        "OK_FACE_DROP_CAVITY",
        "OK_FACE_SKIN",
        "OK_MOUTH_OPEN",
        "OK_MOUTH_PTN",
        "OK_SHOW_MOUTH",
        "OK_CLOTHES_MASK",
        "OK_CLOTH_NOFLIP",
        "OK_SMOOTH",
        "OK_DQS",
        "OK_DQSH",
        "OK_RELAX",
        "OK_HEAD_GPUSKIN",
        "OK_NO_ABM_HEAD",
        "OK_NOSHAPE",
        "OK_NOFIX",
        "OK_NOHEADDETAIL",
        "OK_BONE_HIPOLY",
        "OK_SRC_DIRECT",
        "OK_SKIP_DETAIL",
        "OK_SKIP_DST",
        "OK_SKIP_FACE",
        "OK_SKIP_SCLERA",
        "OK_FBS_DEFCL",
        "OK_MOLE",
        "OK_CLOTHES_STATES",
        "OK_SHOES_TYPE",
    ];
    KEYS.iter()
        .map(|k| format!("{k}={}", get(k)))
        .collect::<Vec<_>>()
        .join("|")
}

pub fn env_sig() -> String {
    env_sig_with(|key| {
        if key=="OK_CLOTHES_STATES" {
            if let Some(states)=crate::unity::explicit_requested_clothes_states(){return states.iter().map(u8::to_string).collect::<Vec<_>>().join(",");}
        }
        crate::profile::var(key).unwrap_or_default()
    })
}

#[cfg(test)]
#[test]
fn explicit_clothing_requests_separate_session_cache_keys(){
    let original=env_sig();
    let half_scope=crate::unity::request_clothes_states([1;9]);let half=env_sig();
    {let _off=crate::unity::request_clothes_states([3;9]);assert_ne!(env_sig(),half);assert!(env_sig().contains("OK_CLOTHES_STATES=3,3,3,3,3,3,3,3,3"));}
    assert_eq!(env_sig(),half);drop(half_scope);assert_eq!(env_sig(),original);
}

#[cfg(test)]
#[test]
fn session_key_separates_clothes_states() {
    let worn = env_sig_with(|_| String::new());
    let half = env_sig_with(|key| if key == "OK_CLOTHES_STATES" {
        "1,0,1,2,0,0,0,0,0".into()
    } else { String::new() });
    assert_ne!(worn, half);
    assert!(half.contains("OK_CLOTHES_STATES=1,0,1,2,0,0,0,0,0"));
}

#[cfg(test)]
#[test]
fn session_key_separates_footwear_mode() {
    let outdoor=env_sig_with(|_|String::new());
    let indoor=env_sig_with(|key|if key=="OK_SHOES_TYPE"{"0".into()}else{String::new()});
    assert_ne!(outdoor,indoor);
    assert!(indoor.contains("OK_SHOES_TYPE=0"));
}

#[cfg(test)]
#[test]
fn session_key_separates_pose_used_during_extraction() {
    let neutral = env_sig_with(|_| String::new());
    let walking = env_sig_with(|key| if key == "OK_POSE" {
        "f_aruki_00_00".into()
    } else { String::new() });
    assert_ne!(neutral, walking);
    assert!(walking.contains("OK_POSE=f_aruki_00_00"));
    let studio = env_sig_with(|key| if key == "OK_POSE_SCENE" { "4".into() } else { String::new() });
    assert_ne!(neutral, studio, "a baked Studio component pose must not reuse a Maker cache");
}

#[cfg(test)]
#[test]
fn mod_inventory_signature_tracks_non_newest_zip() {
    let original = vec![
        (PathBuf::from("a.zipmod"), 100, 10),
        (PathBuf::from("b.zipmod"), 200, 20),
    ];
    let mut edited = original.clone();
    edited[0].1 = 101;
    edited[0].2 = 11;
    assert_ne!(sig_of(&original), sig_of(&edited));
    edited[0] = original[0].clone();
    edited[0].0 = PathBuf::from("c.zipmod");
    assert_ne!(sig_of(&original), sig_of(&edited));
    let mut same_second = original.clone();
    same_second[0].2 = 1_800_000_000_123_456;
    let before = sig_of(&same_second);
    same_second[0].2 += 1;
    assert_ne!(before, sig_of(&same_second));
}

#[cfg(test)]
#[test]
fn legacy_seconds_zip_stamp_migrates_to_microseconds() {
    assert!(same_zip_stamp(100, 1_800_000_000, 100, 1_800_000_000_123_456));
    assert!(!same_zip_stamp(100, 1_800_000_000, 101, 1_800_000_000_123_456));
    assert!(!same_zip_stamp(100, 1_800_000_000_123_456, 100, 1_800_000_000_987_654));
}

#[cfg(all(test,windows))]
#[test]
fn zip_inventory_aliases_preserve_rows_without_reingestion() {
    let old=PathBuf::from(r"C:\owned\mods\sample.zipmod");
    let new=PathBuf::from("C:/owned/mods/sample.zipmod");
    assert_eq!(sig_of(&[(old.clone(),100,20)]),sig_of(&[(new.clone(),100,20)]));
    let mut conn=Connection::open_in_memory().unwrap();ensure_schema(&conn);
    conn.execute("INSERT INTO zip_file VALUES(?1,100,20)",[old.to_string_lossy().as_ref()]).unwrap();
    conn.execute("INSERT INTO zip_guid VALUES(?1,'sentinel')",[old.to_string_lossy().as_ref()]).unwrap();
    // Neither file exists. Re-ingestion would erase the sentinel GUID.
    incremental(&mut conn,&[(new.clone(),100,20)]).unwrap();
    assert_eq!(conn.query_row("SELECT guid FROM zip_guid",[],|r|r.get::<_,String>(0)).unwrap(),"sentinel");
    // A genuine size/mtime edit must invalidate even across path aliases.
    incremental(&mut conn,&[(new,101,21)]).unwrap();
    assert_eq!(conn.query_row("SELECT COUNT(*) FROM zip_guid",[],|r|r.get::<_,i64>(0)).unwrap(),0);
    assert_eq!(conn.query_row("SELECT len FROM zip_file",[],|r|r.get::<_,i64>(0)).unwrap(),101);
}

#[cfg(test)]
#[test]
fn failed_catalog_ingestion_preserves_committed_index() {
    use std::io::Write;
    let unique=SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let path=std::env::temp_dir().join(format!("ok-cache-rollback-{unique}.zipmod"));
    let mut zip=zip::ZipWriter::new(fs::File::create(&path).unwrap());
    zip.start_file("abdata/studio/base/00.unity3d",zip::write::SimpleFileOptions::default()).unwrap();
    zip.write_all(b"cache failure fixture").unwrap();zip.finish().unwrap();
    let mut conn=Connection::open_in_memory().unwrap();ensure_schema(&conn);
    conn.execute("INSERT INTO zip_file VALUES('previous',1,1)",[]).unwrap();
    conn.execute("INSERT INTO override_ab VALUES('abdata/previous.unity3d','previous')",[]).unwrap();
    conn.execute_batch("CREATE TRIGGER refuse_override BEFORE INSERT ON override_ab BEGIN SELECT RAISE(ABORT,'simulated full cache'); END;").unwrap();
    let result=incremental(&mut conn,&[(path.clone(),100,20)]);
    fs::remove_file(&path).unwrap();
    assert!(result.is_err());
    let stamps:Vec<String>=conn.prepare("SELECT path FROM zip_file").unwrap().query_map([],|r|r.get(0)).unwrap().map(Result::unwrap).collect();
    assert_eq!(stamps,vec!["previous"]);
    let previous:String=conn.query_row("SELECT zip_path FROM override_ab WHERE ab_key='abdata/previous.unity3d'",[],|r|r.get(0)).unwrap();
    assert_eq!(previous,"previous");
}

#[cfg(test)]
#[test]
fn cache_budget_rejects_growth_without_losing_existing_entries() {
    let conn = Connection::open_in_memory().unwrap();
    configure_cache_budget(&conn, 1024 * 1024).unwrap();
    ensure_schema(&conn);
    conn.execute("INSERT INTO session_blob VALUES('warm', ?1)", [vec![7u8; 65536]]).unwrap();
    let mut refused = false;
    for index in 0..20 {
        if conn.execute("INSERT INTO session_blob VALUES(?1, ?2)",
                        params![format!("large{index}"), vec![3u8; 262144]]).is_err() {
            refused = true;
            break;
        }
    }
    assert!(refused, "cache must refuse writes beyond its budget");
    let pages: u64 = conn.pragma_query_value(None, "page_count", |row| row.get(0)).unwrap();
    let page_size: u64 = conn.pragma_query_value(None, "page_size", |row| row.get(0)).unwrap();
    assert!(pages * page_size <= 1024 * 1024);
    let warm: Vec<u8> = conn.query_row("SELECT payload FROM session_blob WHERE card_key='warm'", [], |row| row.get(0)).unwrap();
    assert_eq!(warm, vec![7u8; 65536]);
}

#[cfg(test)]
#[test]
fn override_lookup_tracks_zip_changes_and_owned_indexes() {
    use std::io::Write;
    TEST_IGNORE_LIBRARY.with(|c| c.set(true));
    let unique=SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let root=PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(format!("../../Saved/Tests/AnimationPerf389/Fixture{unique}"));
    let mods=root.join("mods");fs::create_dir_all(mods.join("nested")).unwrap();
    fn package(path:&Path,key:&str,bytes:&[u8]) {
        let mut zip=zip::ZipWriter::new(fs::File::create(path).unwrap());
        zip.start_file(key,zip::write::SimpleFileOptions::default()).unwrap();
        zip.write_all(bytes).unwrap();zip.finish().unwrap();
    }
    assert_eq!(override_source(&mods,"abdata/studio/base/00.unity3d"),None);
    assert!(crate::mods::override_bundle(&mods,"studio/base/00.unity3d").is_none());
    let first=mods.join("nested/first.ZIPMOD");
    package(&first,"abdata/studio/base/00.unity3d",b"first bundle fixture payload");
    assert_eq!(crate::mods::override_bundle(&mods,"STUDIO\\BASE\\00.UNITY3D").unwrap(),b"first bundle fixture payload");
    let mut reference:Vec<_>=crate::mods::all_zipmods(&mods).into_iter().filter_map(|p|file_stamp(&p).map(|(len,mt)|(p,len,mt))).collect();
    reference.sort_by_key(|row|zip_path_identity(&row.0));
    assert_eq!(stamp_zips(&mods),reference);
    let original=ensure_mods(&mods);
    assert!(Arc::ptr_eq(&original,&ensure_mods(&mods)));
    assert_eq!(override_source(&mods,"abdata/missing.unity3d"),Some(None));
    let mut owned=override_index(&mods).unwrap();owned.clear();
    assert_eq!(override_source(&mods,"abdata/studio/base/00.unity3d"),Some(Some(first.clone())));
    package(&first,"abdata/studio/base/00.unity3d",b"modified and longer");
    assert_eq!(crate::mods::override_bundle(&mods,"studio/base/00.unity3d").unwrap(),b"modified and longer");
    assert!(!Arc::ptr_eq(&original,&ensure_mods(&mods)));
    fs::rename(&first,first.with_extension("retired")).unwrap();
    assert!(crate::mods::override_bundle(&mods,"studio/base/00.unity3d").is_none());
    let second=mods.join("replacement.zipmod");
    package(&second,"abdata/studio/base/00.unity3d",b"replacement bundle fixture payload");
    assert_eq!(crate::mods::override_bundle(&mods,"studio/base/00.unity3d").unwrap(),b"replacement bundle fixture payload");
    assert_eq!(original.overrides.get("abdata/studio/base/00.unity3d"),Some(&first));
    assert_eq!(override_source(&root.join("absent"),"abdata/studio/base/00.unity3d"),None);
    eprintln!("OK mods override fixture valid=1 empty_fallback=1 edits=1 removal=1 addition=1 owned_independent=1 snapshot_immutable=1");
}

#[cfg(test)]
#[test]
#[ignore="installed mod inventory timing"]
fn installed_target_lookup_profile() {
    let mods=&crate::test_support::game_path("/mods");
    let _=ensure_mods(mods);
    let start=std::time::Instant::now();
    let mut reference:Vec<_>=crate::mods::all_zipmods(mods).into_iter().filter_map(|p|file_stamp(&p).map(|(len,mt)|(p,len,mt))).collect();
    reference.sort_by_key(|row|zip_path_identity(&row.0));
    let reference_ms=start.elapsed().as_secs_f64()*1000.;
    let start=std::time::Instant::now();let current=stamp_zips(mods);
    let current_ms=start.elapsed().as_secs_f64()*1000.;assert_eq!(current,reference);
    eprintln!("OK mods inventory exact=1 zips={} reference_ms={reference_ms:.3} current_ms={current_ms:.3}",current.len());
    for _ in 0..3 {
        let start=std::time::Instant::now();let mem=ensure_mods(mods);
        let scanned=start.elapsed();
        let catalog=std::hint::black_box(mem.catalog.clone());
        let guids=std::hint::black_box(mem.guids.clone());
        let overrides=std::hint::black_box(mem.overrides.clone());
        let copied=start.elapsed()-scanned;
        let expected=overrides.get("abdata/studio/base/00.unity3d").cloned();
        let start=std::time::Instant::now();let actual=override_source(mods,"abdata/studio/base/00.unity3d").unwrap();
        assert_eq!(expected,actual);
        eprintln!("OK mods target profile valid=1 inventory_ms={:.3} original_copy_ms={:.3} target_ms={:.3} guids={} overrides={}",scanned.as_secs_f64()*1000.,copied.as_secs_f64()*1000.,start.elapsed().as_secs_f64()*1000.,guids.len(),overrides.len());
        std::hint::black_box(catalog);
    }
}

#[cfg(test)]
#[test]
#[ignore="isolated process with OK_NOCACHE=1"]
fn override_lookup_without_sql_preserves_scan_and_tsv() {
    use std::io::Write;
    assert!(disabled(),"run in an isolated OK_NOCACHE=1 process");
    let unique=SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let root=PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(format!("../../Saved/Tests/AnimationPerf389/NoSqlFixture{unique}"));
    let mods=root.join("mods");fs::create_dir_all(&mods).unwrap();
    let path=mods.join("sample.zipmod");
    let bytes=b"fallback bundle fixture payload";
    let mut zip=zip::ZipWriter::new(fs::File::create(&path).unwrap());
    zip.start_file("abdata/studio/base/00.unity3d",zip::write::SimpleFileOptions::default()).unwrap();
    zip.write_all(bytes).unwrap();zip.finish().unwrap();
    assert_eq!(override_source(&mods,"abdata/studio/base/00.unity3d"),None);
    assert_eq!(crate::mods::override_bundle(&mods,"studio/base/00.unity3d").unwrap(),bytes);
    let outside=root.join("outside.zipmod");fs::rename(&path,&outside).unwrap();
    fs::write(mods.join(".ok_override_index.tsv"),format!("abdata/studio/base/00.unity3d\t{}\n",outside.display())).unwrap();
    assert_eq!(crate::mods::override_bundle_with_source(&mods,"studio/base/00.unity3d").unwrap(),(outside,bytes.to_vec()));
    assert!(!root.join(".ok_cache.sqlite").ok_exists());
    eprintln!("OK mods no SQL fallback valid=1 scan=1 tsv=1");
}

#[cfg(test)]
#[test]
fn bounded_blob_cache_recovers_full_legacy_database_and_preserves_catalogs() {
    let conn=Connection::open_in_memory().unwrap();
    configure_cache_budget(&conn,1024*1024).unwrap();ensure_schema(&conn);
    conn.execute("INSERT INTO meta VALUES('fixture','catalog preserved')",[]).unwrap();
    conn.execute("INSERT INTO list_entry VALUES('mod',1,2,'name','ab','data','tex','parent','extra')",[]).unwrap();
    conn.execute_batch("DROP TRIGGER session_usage_delete; DROP TRIGGER prefab_usage_delete; DROP TABLE blob_usage;").unwrap();
    let mut full=false;
    for i in 0..100 {
        if conn.execute("INSERT INTO session_blob VALUES(?1,?2)",params![format!("legacy{i}"),vec![1u8;32768]]).is_err() {full=true;break;}
    }
    assert!(full,"fixture must reach actual SQLite capacity");
    ensure_schema(&conn); // Full old database must be able to migrate in place.
    assert!(store_blob(&conn,"prefab","bundle","hair",&vec![2;65536]).unwrap());
    assert!(store_blob(&conn,"session","hot","",&vec![3;65536]).unwrap());
    for i in 0..60 {
        touch_blob(&conn,"session","hot","");
        assert!(store_blob(&conn,"session",&format!("new{i}"),"",&vec![4;65536]).unwrap());
    }
    let hot:Vec<u8>=conn.query_row("SELECT payload FROM session_blob WHERE card_key='hot'",[],|r|r.get(0)).unwrap();
    assert_eq!(hot,vec![3;65536]);
    assert_eq!(conn.query_row("SELECT count(*) FROM session_blob WHERE card_key LIKE 'legacy%'",[],|r|r.get::<_,i64>(0)).unwrap(),0);
    assert_eq!(conn.query_row("SELECT count(*) FROM bundle_prefab",[],|r|r.get::<_,i64>(0)).unwrap(),0,"both blob kinds participate in eviction");
    touch_blob(&conn,"session","already_evicted","");
    assert_eq!(conn.query_row("SELECT count(*) FROM blob_usage WHERE k1='already_evicted'",[],|r|r.get::<_,i64>(0)).unwrap(),0);
    assert_eq!(meta_get(&conn,"fixture").as_deref(),Some("catalog preserved"));
    assert_eq!(conn.query_row("SELECT count(*) FROM list_entry",[],|r|r.get::<_,i64>(0)).unwrap(),1);
    let pages:u64=conn.pragma_query_value(None,"page_count",|r|r.get(0)).unwrap();
    let size:u64=conn.pragma_query_value(None,"page_size",|r|r.get(0)).unwrap();
    assert!(pages*size<=1024*1024);
    let count:i64=conn.query_row("SELECT count(*) FROM session_blob",[],|r|r.get(0)).unwrap();
    assert!(!store_blob(&conn,"session","oversized","",&vec![5;600000]).unwrap());
    assert_eq!(conn.query_row("SELECT count(*) FROM session_blob",[],|r|r.get::<_,i64>(0)).unwrap(),count,"oversized entries must not flush working set");
    conn.execute_batch("CREATE TRIGGER refuse_blob BEFORE INSERT ON session_blob BEGIN SELECT RAISE(ABORT,'fixture write rejection'); END;").unwrap();
    assert!(store_blob(&conn,"session","rejected","",&[9;1024]).is_err());
    assert_eq!(conn.query_row("SELECT count(*) FROM session_blob",[],|r|r.get::<_,i64>(0)).unwrap(),count,"non-capacity failure must not trigger eviction");
}
