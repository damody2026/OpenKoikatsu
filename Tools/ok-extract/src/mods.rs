#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use zip::ZipArchive;
#[path="mods_migration_cache.rs"]
mod migration_cache;

/// One zipmod bundle read once for every thread (each used to read and keep its own copy). Owned bytes are
/// bounded (`zip_ab_limits`); mapped stored entries cost no private memory.
type ZipAbCache = (HashMap<(PathBuf, String), Option<Arc<(String, crate::unity::SourceBytes)>>>, usize);
static ZIP_AB_CACHE: std::sync::Mutex<Option<ZipAbCache>> = std::sync::Mutex::new(None);
/// Bundles being read (zipmod_one_unity3d_arc): a second asker waits for the first read.
static ZIP_AB_LOADING: std::sync::LazyLock<std::sync::Mutex<HashMap<(PathBuf, String), Arc<std::sync::Mutex<()>>>>> = std::sync::LazyLock::new(Default::default);
/// Past either limit bundles give way until the new one fits (zip_ab_cache_put). A conversion (a card pack being recorded) opens a card 42 times: at 256 MiB /
/// 128 entries it emptied twice a card (then the whole cache went), each time reading its zipmod bundles again (~0.3 s); it keeps 768 MiB / 1024
/// and empties the cache between cards. Other opens keep the smaller limits.
fn zip_ab_limits() -> (usize, usize) {
    if crate::oklib::recording_card_pack() { (768 << 20, 1024) } else { (256 << 20, 128) }
}

/// Between independent card opens (`unity::release_bundle_caches`): the zipmod bundle cache.
pub(crate) fn clear_zip_bundle_cache() {
    *ZIP_AB_CACHE.lock().unwrap_or_else(|e| e.into_inner()) = None;
}

fn guid_cache_path(mods_dir: &Path) -> PathBuf {
    mods_dir.join(".ok_guid_index.txt")
}

/// Native MigrateData gives StripAll precedence over individual/MigrateAll
/// rules, even when the old GUID is still installed. Preserve the saved numeric
/// fields and let normal compatibility resolution select the internal list.
fn strip_all_manifest(xml:&str)->Vec<String> {
    let Ok(doc)=roxmltree::Document::parse(xml) else{return vec![]};
    let root=doc.root_element();
    if root.tag_name().name()!="manifest"||root.attribute("schema-ver")!=Some("1"){return vec![];}
    let games:Vec<_>=root.children().filter(|n|n.has_tag_name("game"))
        .filter_map(|n|n.text()).map(str::trim).filter(|s|!s.is_empty()).collect();
    if !games.is_empty()&&!games.iter().any(|s|["koikatsu sunshine","koikatu sunshine","コイカツ sunshine"].iter().any(|g|s.eq_ignore_ascii_case(g))){return vec![];}
    root.children().filter(|n|n.has_tag_name("migrationInfo")).flat_map(|n|n.children())
        .filter(|n|n.has_tag_name("info")&&n.attribute("migrationType")==Some("StripAll"))
        .filter_map(|n|n.attribute("guidOld")).map(str::trim).filter(|s|!s.is_empty()).map(str::to_owned).collect()
}

fn read_manifest(reader:&mut impl Read)->std::io::Result<String> {
    let mut bytes=Vec::new();reader.read_to_end(&mut bytes)?;
    let invalid=||std::io::Error::new(std::io::ErrorKind::InvalidData,"invalid manifest encoding");
    if bytes.starts_with(&[0xff,0xfe])||bytes.starts_with(&[0xfe,0xff]) {
        let little=bytes[0]==0xff;let data=&bytes[2..];
        if data.len()%2!=0 {return Err(invalid());}
        let units:Vec<u16>=data.chunks_exact(2).map(|b|if little {u16::from_le_bytes([b[0],b[1]])}else{u16::from_be_bytes([b[0],b[1]])}).collect();
        String::from_utf16(&units).map_err(|_|invalid())
    }else{
        let data=bytes.strip_prefix(&[0xef,0xbb,0xbf]).unwrap_or(&bytes);
        String::from_utf8(data.to_vec()).map_err(|_|invalid())
    }
}

fn strip_all_enabled(game:&Path)->bool {
    let config=crate::gamefs::read_to_string(game.join("BepInEx/config/com.bepis.bepinex.sideloader.cfg")).unwrap_or_default();
    !config.lines().filter_map(|line|line.split_once('=')).any(|(k,v)|k.trim()=="Migration enabled"&&v.trim().eq_ignore_ascii_case("false"))
}

pub(crate) fn prepare_strip_all_migrations(game:&Path) {
    if strip_all_enabled(game){let _=strip_all_guids(&game.join("mods"));}
}

pub(crate) fn apply_strip_all_migrations(game:&Path,card:&mut crate::card::CardInfo) {
    if !strip_all_enabled(game){return;}
    if card.resolves.is_empty(){return;}
    // An OK card's answer (tree_answers.rs): which of its GUIDs a StripAll migration removes, without the tree.
    let mut question:Vec<&str>=card.resolves.iter().map(|r|r.mod_id.trim()).collect();
    question.sort_unstable();question.dedup();
    let question=question.join("\n");
    let stripped:HashSet<String>=match crate::tree_answers::lookup::<Vec<String>>("strip-all",question.as_bytes()) {
        Some(answer)=>answer.into_iter().collect(),
        None if crate::tree_answers::unanswered("strip-all",question.as_bytes())=>HashSet::new(),
        None=>{
            let all=crate::okstrict::scanning(||strip_all_guids(&game.join("mods")));
            let mut answer:Vec<String>=question.split('\n').filter(|g|all.contains(*g)).map(str::to_owned).collect();
            answer.sort();
            crate::tree_answers::record("strip-all",question.as_bytes(),&answer);
            answer.into_iter().collect()
        }
    };
    card.resolves.retain(|r| {
        let keep=!stripped.contains(r.mod_id.trim());
        if !keep {eprintln!("ok-extract: native StripAll guid={} property={} slot={}",r.mod_id,r.property,r.slot);}
        keep
    });
}

pub(crate) fn strip_all_guids(mods:&Path)->Arc<HashSet<String>> {
    // A real directory keeps the watched cache (it sees same-size rewrites a size/time signature can miss). The OK
    // base library's recorded tree cannot be canonicalized or watched, so that cache never published and every
    // open rescanned all zipmods (~0.25 s); its archives cannot change while the library is active, so key it by
    // the zip list signature (and what a mounted card pack adds under mods/) instead.
    if mods.canonicalize().is_ok() { return migration_cache::get(mods,read_strip_all_guids); }
    use std::sync::Mutex;
    static MEMO: Mutex<Vec<(String, Arc<HashSet<String>>)>> = Mutex::new(Vec::new());
    let key = format!("{}\n{:016x}\n{}", mods.display(), crate::list::zip_list_sig(mods), crate::oklib::mounted_pack_mods_identity());
    if let Some((_, hit)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _)| *k == key) { return hit.clone(); }
    if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: MEMO miss strip_all key={}", key.replace('\n', " | ")); }
    let value = Arc::new(read_strip_all_guids(mods).0);
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    memo.retain(|(k, _)| *k != key);
    if memo.len() >= 8 { memo.remove(0); }
    memo.push((key, value.clone()));
    value
}

fn read_strip_all_guids(mods:&Path)->(HashSet<String>,bool) {let _spot=crate::profile::spot("read_strip_all_guids");
    let mut stripped=HashSet::new();let mut complete=true;
    let paths=match collect_zipmods_checked(mods){Ok((paths,trackable))=>{complete&=trackable;paths},Err(_)=>{complete=false;Vec::new()}};
    for path in paths {
        let (guids,ok)=crate::gamefs::zip_memo("strip_all",&path,||zipmod_strip_all(&path));
        stripped.extend(guids);complete&=ok;
    }
    (stripped,complete)
}

/// One zipmod's StripAll GUIDs, and whether it could be read.
fn zipmod_strip_all(path:&Path)->(Vec<String>,bool) {
    let Some(mut zip) = crate::gamefs::open_zip(path) else{return (Vec::new(),false)};
    // Sideloader reads the root manifest, not arbitrary embedded manifests.
    let mut entry=match zip.by_name("manifest.xml") {Ok(entry)=>entry,Err(zip::result::ZipError::FileNotFound)=>return (Vec::new(),true),Err(_)=>return (Vec::new(),false)};
    match read_manifest(&mut entry) {
        Ok(xml)=>(strip_all_manifest(&xml),true),
        Err(error) if error.kind()==std::io::ErrorKind::InvalidData=>(Vec::new(),true),
        Err(_)=>(Vec::new(),false),
    }
}

#[cfg(test)]
mod migration_tests {
    use crate::gamefs::GamePath;
    #[test]fn manifest_bom_decoding_preserves_xml_rules() {
        let xml="<manifest schema-ver='1'><game>Koikatsu Sunshine</game><guid>mod.guid</guid><migrationInfo><info migrationType='StripAll' guidOld='old'/></migrationInfo></manifest>";
        for little in [true,false] {
            let mut bytes=if little {vec![0xff,0xfe]}else{vec![0xfe,0xff]};
            for unit in xml.encode_utf16(){bytes.extend(if little{unit.to_le_bytes()}else{unit.to_be_bytes()});}
            let decoded=super::read_manifest(&mut bytes.as_slice()).unwrap();assert_eq!(decoded,xml);
            assert_eq!(super::strip_all_manifest(&decoded),vec!["old"]);
            assert_eq!(super::extract_tag(&decoded,"guid").as_deref(),Some("mod.guid"));
        }
        let mut bytes=vec![0xef,0xbb,0xbf];bytes.extend(xml.as_bytes());
        assert_eq!(super::read_manifest(&mut bytes.as_slice()).unwrap(),xml);
        assert!(super::read_manifest(&mut [0xff,0xfe,1].as_slice()).is_err());
        assert!(super::read_manifest(&mut [0xff,0xfe,0,0xd8].as_slice()).is_err());
    }
    #[test]
    #[ignore="installed zipmod inventory timing"]
    fn installed_strip_all_inventory_profile() {
        let root=&crate::test_support::game_path("/mods");
        assert!(root.ok_is_dir());let start=std::time::Instant::now();
        let first=super::strip_all_guids(root);let cold=start.elapsed();
        let trackable=super::collect_zipmods_checked(root).unwrap().1;
        let runs=if trackable{20}else{2};
        let start=std::time::Instant::now();
        for _ in 0..runs {let next=super::strip_all_guids(root);assert_eq!(*first,*next);if cfg!(windows)&&trackable{assert!(std::sync::Arc::ptr_eq(&first,&next));}}
        eprintln!("StripAll installed guids={} cacheable={trackable} first_ms={:.3} watch_validated_mean_ms={:.3}",first.len(),cold.as_secs_f64()*1000.,start.elapsed().as_secs_f64()*1000./runs as f64);
    }
    #[test]fn strip_all_inventory_cache_invalidates_on_changes_and_root() {
        use std::io::Write;
        let unique=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let root=std::env::temp_dir().join(format!("ok-migration-{unique}"));std::fs::create_dir_all(&root).unwrap();
        let package=|path:&std::path::Path,guid:&str| {
            let mut zip=zip::ZipWriter::new(std::fs::File::create(path).unwrap());
            zip.start_file("manifest.xml",zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored)).unwrap();
            write!(zip,"<manifest schema-ver='1'><game>Koikatsu Sunshine</game><migrationInfo><info migrationType='StripAll' guidOld='{guid}'/></migrationInfo></manifest>").unwrap();zip.finish().unwrap();
        };
        let first=root.join("first.zipmod");package(&first,"old");
        let original=super::strip_all_guids(&root);assert!(original.contains("old"));
        let same=super::strip_all_guids(&root);assert_eq!(*original,*same);
        if cfg!(windows){assert!(std::sync::Arc::ptr_eq(&original,&same));}
        let threaded=root.clone();let reused=std::thread::spawn(move||super::strip_all_guids(&threaded)).join().unwrap();
        assert_eq!(*original,*reused);
        if cfg!(windows){assert!(std::sync::Arc::ptr_eq(&original,&reused),"new background workers share immutable migration results");}
        let size=crate::gamefs::metadata(&first).unwrap().len();package(&first,"new");
        assert_eq!(crate::gamefs::metadata(&first).unwrap().len(),size);
        let changed=super::strip_all_guids(&root);assert!(changed.contains("new")&&!changed.contains("old"));
        assert!(original.contains("old"),"existing callers retain immutable prior generation");
        let second=root.join("second.zipmod");package(&second,"another");
        assert!(super::strip_all_guids(&root).contains("another"));
        package(&first,"replacement_longer");let updated=super::strip_all_guids(&root);
        assert!(!updated.contains("old"));assert!(updated.contains("replacement_longer"));
        std::fs::remove_file(&second).unwrap();assert!(!super::strip_all_guids(&root).contains("another"));
        let other=root.join("other");std::fs::create_dir(&other).unwrap();assert!(super::strip_all_guids(&other).is_empty());
        assert!(super::strip_all_guids(&root).contains("replacement_longer"));
        std::fs::remove_file(first).unwrap();std::fs::remove_dir(other).unwrap();std::fs::remove_dir(root).unwrap();
    }
    #[test]fn strip_all_obeys_native_manifest_and_game_scope() {
        let xml=r#"<manifest schema-ver="1"><game> </game><game>Koikatsu Sunshine</game><migrationInfo><info migrationType='StripAll' guidOld='old'/><info migrationType="MigrateAll" guidOld="old"/><info migrationType="StripAll" guidOld="other"/></migrationInfo><!-- <info migrationType="StripAll" guidOld="comment"/> --></manifest>"#;
        assert_eq!(super::strip_all_manifest(xml),vec!["old","other"]);
        assert!(super::strip_all_manifest(&xml.replace("Koikatsu Sunshine","Koikatsu")).is_empty());
        assert!(super::strip_all_manifest(&xml.replace("schema-ver=\"1\"","schema-ver=\"2\"")).is_empty());
        assert!(super::strip_all_manifest("broken XML").is_empty());
    }
    #[cfg(windows)]
    #[test]
    fn linked_mod_sources_remain_fresh_without_publishing_root_only_cache(){
        use std::{io::Write,os::windows::process::CommandExt};
        let base=std::env::temp_dir().join(format!("ok-migration-junction-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let root=base.join("mods");let external=base.join("external");std::fs::create_dir_all(&root).unwrap();std::fs::create_dir_all(&external).unwrap();
        let linked=root.join("linked");
        let result=std::process::Command::new("cmd.exe").args(["/D","/C","mklink","/J"]).arg(&linked).arg(&external).creation_flags(0x08000000).output().unwrap();
        assert!(result.status.success(),"fixture junction failed: {}",String::from_utf8_lossy(&result.stderr));
        let package=|guid:&str|{
            let mut zip=zip::ZipWriter::new(std::fs::File::create(external.join("source.zipmod")).unwrap());
            zip.start_file("manifest.xml",zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored)).unwrap();
            write!(zip,"<manifest schema-ver='1'><migrationInfo><info migrationType='StripAll' guidOld='{guid}'/></migrationInfo></manifest>").unwrap();zip.finish().unwrap();
        };
        package("old");let(paths,trackable)=super::collect_zipmods_checked(&root).unwrap();
        assert_eq!(paths.len(),1);assert!(!trackable,"root watcher cannot cover external junction writes");
        let before=super::strip_all_guids(&root);assert!(before.contains("old"));
        let again=super::strip_all_guids(&root);assert_eq!(*before,*again);assert!(!std::sync::Arc::ptr_eq(&before,&again));
        package("new");let after=super::strip_all_guids(&root);assert!(after.contains("new")&&!after.contains("old"));assert!(before.contains("old"));
    }
}

fn load_guid_cache(mods_dir: &Path) -> (HashMap<String, PathBuf>, bool) {
    let mut out = HashMap::new();
    let Ok(text) = crate::gamefs::read_to_string(guid_cache_path(mods_dir)) else {
        return (out, false);
    };
    let mut full = false;
    for line in text.lines() {
        let Some((g, p)) = line.split_once('\t') else { continue };
        if g == "__FULL__" {
            full = true;
            continue;
        }
        out.insert(g.to_string(), PathBuf::from(p));
    }
    (out, full)
}

fn save_guid_cache(mods_dir: &Path, map: &HashMap<String, PathBuf>, full: bool) {
    let mut s = String::new();
    if full {
        s.push_str("__FULL__\t1\n");
    }
    for (g, p) in map {
        s.push_str(g);
        s.push('\t');
        s.push_str(&p.to_string_lossy());
        s.push('\n');
    }
    let _ = fs::write(guid_cache_path(mods_dir), s);
}

/// find_guids, remembered for the process per mods tree (zip list, mounted card pack) and wanted set: a GUID that is
/// not in the on-disk cache otherwise makes every card open read every zipmod's manifest again (~0.5 s).
pub fn find_guids(mods_dir: &Path, wanted: Option<&std::collections::HashSet<String>>) -> HashMap<String, PathBuf> {
    let Some(w) = wanted else {
        if crate::tree_answers::unanswered("guids", b"(every guid)") { return HashMap::new(); }
        return crate::okstrict::scanning(|| find_guids_uncached(mods_dir, None));
    };
    let mut names: Vec<&String> = w.iter().collect();
    names.sort();
    // An OK card's answer (tree_answers.rs): which archive declares each GUID, without the tree's manifests.
    let question = names.iter().map(|s| s.as_str()).collect::<Vec<_>>().join("\n");
    if let Some(rows) = crate::tree_answers::lookup::<Vec<(String, String)>>("guids", question.as_bytes()) {
        return rows.into_iter().map(|(guid, zip)| (guid, crate::tree_answers::rebase(mods_dir, &zip))).collect();
    }
    if crate::tree_answers::unanswered("guids", question.as_bytes()) { return HashMap::new(); }
    let found = crate::okstrict::scanning(|| find_guids_wanted(mods_dir, w, &names));
    if crate::tree_answers::recording() {
        let rows: Option<Vec<(String, String)>> = found.iter().map(|(g, p)| crate::tree_answers::relative(p).map(|z| (g.clone(), z))).collect();
        if let Some(mut rows) = rows { rows.sort(); crate::tree_answers::record("guids", question.as_bytes(), &rows); }
    }
    found
}

fn find_guids_wanted(mods_dir: &Path, w: &std::collections::HashSet<String>, names: &[&String]) -> HashMap<String, PathBuf> {
    use std::sync::Mutex;
    let wanted = Some(w);
    let key = format!("{}\n{:016x}\n{}\n{}", mods_dir.display(), crate::list::zip_list_sig(mods_dir), crate::oklib::mounted_pack_mods_identity(),
        names.iter().map(|s| s.as_str()).collect::<Vec<_>>().join("\n"));
    static MEMO: Mutex<Vec<(String, HashMap<String, PathBuf>)>> = Mutex::new(Vec::new());
    use crate::memosnap::{RES,get,put};
    let memo_key = format!("guids\n{key}");
    if let Some((_, hit)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _)| *k == key) {
        // A remembered hit still has to exist (a zipmod removed while the process runs).
        if hit.values().all(|p| p.ok_is_file()) { put(RES, memo_key.as_bytes(), || bincode::serialize(hit).ok()); return hit.clone(); }
    }
    // Replayed from the card (OK_CARD_MEMO=res).
    let replayed = get(RES, memo_key.as_bytes()).and_then(|b| bincode::deserialize::<HashMap<String, PathBuf>>(&b).ok())
        .filter(|hit| hit.values().all(|p| p.ok_is_file()));
    let out = match replayed { Some(hit) => hit, None => find_guids_uncached(mods_dir, wanted) };
    put(RES, memo_key.as_bytes(), || bincode::serialize(&out).ok());
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    memo.retain(|(k, _)| *k != key);
    if memo.len() >= 64 { memo.remove(0); }
    memo.push((key, out.clone()));
    out
}

/// A real mods tree's zipmods in walk order and their signature, reused while a directory watch reports no change.
/// Walking and stating an installation's ~7000 zipmods cost ~0.35 s, and an original card's open did it several
/// times (GUID lookups, list and manifest memos). None for a library's recorded tree (cheap to list) or a tree a
/// root watch cannot cover (junctions); those are walked every time as before.
pub(crate) fn watched_tree_zips(mods_dir: &Path) -> Option<Arc<(Vec<PathBuf>, u64)>> {
    use std::sync::Mutex;
    use crate::appearance::catalog_watch::Watch;
    let root = mods_dir.canonicalize().ok()?;
    static MEMO: Mutex<Vec<(PathBuf, Watch, Arc<(Vec<PathBuf>, u64)>)>> = Mutex::new(Vec::new());
    {
        let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(i) = memo.iter().position(|(r, _, _)| *r == root) {
            let (_, watch, tree) = &mut memo[i];
            if watch.available() && !watch.changed() { return Some(tree.clone()); }
            memo.remove(i);
        }
    }
    // Watch first: a change during the walk is then reported on the next call.
    let watch = Watch::for_directory(&root);
    let (walk, trackable) = collect_zipmods_checked(mods_dir).ok()?;
    if !trackable || !watch.available() { return None; }
    let mut sorted = walk.clone();
    sorted.sort();
    let tree = Arc::new((walk, crate::list::catalog_sig(&sorted)));
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    if memo.len() >= 4 { memo.remove(0); }
    memo.push((root, watch, tree.clone()));
    Some(tree)
}

/// Every GUID a mods tree declares (lower case) -> the first zipmod in walk order declaring it, built once per tree
/// signature with the archives' manifests read in parallel (each archive's once per generation, `zip_memo`). An
/// original card's open asks for its few GUIDs; when one is not installed, the per-card walk read ~7000 manifests.
fn tree_guid_index(mods_dir: &Path) -> Option<Arc<HashMap<String, PathBuf>>> {let _spot=crate::profile::spot("view: tree_guid_index");
    use std::sync::Mutex;
    let watched = watched_tree_zips(mods_dir);
    let walk = match &watched { Some(tree) => tree.0.clone(), None => collect_zipmods(mods_dir).ok()? };
    let sig = watched.as_ref().map_or_else(|| crate::list::zip_list_sig(mods_dir), |tree| tree.1);
    let key = format!("{}\n{:016x}\n{}", mods_dir.display(), sig, crate::oklib::mounted_pack_mods_identity());
    static MEMO: Mutex<Vec<(String, Arc<HashMap<String, PathBuf>>)>> = Mutex::new(Vec::new());
    if let Some((_, hit)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _)| *k == key) { return Some(hit.clone()); }
    // The installation's own tree (no library, no card pack): the index of the previous process when the tree is
    // unchanged. Reading ~7000 manifests took ~11 s at the start of every process (each okcard-check, each
    // ok-card-warm worker).
    let installed = crate::oklib::active().is_none() && crate::oklib::mounted_pack_mods_identity().is_empty();
    if let Some(index) = installed.then(|| load_tree_guid_index(mods_dir, &key)).flatten() {
        let index = Arc::new(index);
        let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
        if memo.len() >= 4 { memo.remove(0); }
        memo.push((key, index.clone()));
        return Some(index);
    }
    // An archive the active library records cannot change: its GUIDs are kept by path, so a card pack's new view
    // only reads the archives it adds (looking each of ~7000 up through `zip_memo` again cost ~0.3 s per view).
    static RECORDED: Mutex<Option<HashMap<PathBuf, Vec<String>>>> = Mutex::new(None);
    let recorded = crate::gamefs::library_records_zips(&walk);
    let known: Vec<Option<Vec<String>>> = {
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        // The previous process's answers for this library (oklib::library_scan_load).
        if map.is_none() { *map = crate::oklib::library_scan_load::<HashMap<PathBuf, Vec<String>>>("zip-guids-v1"); }
        walk.iter().zip(&recorded).map(|(p, r)| if *r { map.as_ref().and_then(|m| m.get(p).cloned()) } else { None }).collect()
    };
    let fresh_recorded = recorded.iter().zip(&known).any(|(r, k)| *r && k.is_none());
    let guids: Vec<Vec<String>> = { use rayon::prelude::*; crate::oklib::pack_parallel(|| walk.par_iter().zip(known.into_par_iter()).map(|(p, k)| k.unwrap_or_else(|| guids_from_zipmod(p))).collect()) };
    {
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        let map = map.get_or_insert_with(HashMap::new);
        for ((p, r), g) in walk.iter().zip(&recorded).zip(&guids) { if *r { map.entry(p.clone()).or_insert_with(|| g.clone()); } }
        if fresh_recorded { crate::oklib::library_scan_save("zip-guids-v1", &*map); }
    }
    let mut index = HashMap::new();
    for (path, guids) in walk.iter().zip(guids) { for g in guids { index.entry(g.to_ascii_lowercase()).or_insert_with(|| path.clone()); } }
    if installed { save_tree_guid_index(mods_dir, &key, &index); }
    let index = Arc::new(index);
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    memo.retain(|(k, _)| *k != key);
    if memo.len() >= 4 { memo.remove(0); }
    memo.push((key, index.clone()));
    Some(index)
}

/// The installed tree's GUID index kept between processes, valid for one tree key (its zips' paths, sizes and
/// modification times).
fn tree_guid_index_path(mods_dir: &Path) -> PathBuf { mods_dir.join(".ok_tree_guid_index.bin") }
const TREE_GUID_INDEX_VERSION: u32 = 1;
fn load_tree_guid_index(mods_dir: &Path, key: &str) -> Option<HashMap<String, PathBuf>> {
    let bytes = fs::read(tree_guid_index_path(mods_dir)).ok()?;
    let (version, stored, entries): (u32, String, Vec<(String, PathBuf)>) = bincode::deserialize(&bytes).ok()?;
    (version == TREE_GUID_INDEX_VERSION && stored == key).then(|| entries.into_iter().collect())
}
fn save_tree_guid_index(mods_dir: &Path, key: &str, index: &HashMap<String, PathBuf>) {
    let entries: Vec<(&String, &PathBuf)> = index.iter().collect();
    let Ok(bytes) = bincode::serialize(&(TREE_GUID_INDEX_VERSION, key, entries)) else { return };
    let path = tree_guid_index_path(mods_dir);
    let tmp = path.with_extension(format!("bin.{}.tmp", std::process::id()));
    if fs::write(&tmp, bytes).is_ok() && fs::rename(&tmp, &path).is_err() { let _ = fs::remove_file(&tmp); }
}

/// Builds the GUID index of a mods tree ahead of the first original card (reader start).
pub(crate) fn prewarm_guid_index(mods_dir: &Path) { let _ = tree_guid_index(mods_dir); }

fn find_guids_uncached(mods_dir: &Path, wanted: Option<&std::collections::HashSet<String>>) -> HashMap<String, PathBuf> {
    // A library session (no SQL cache): the tree's GUID index answers every wanted GUID at once.
    if let (Some(w), true) = (wanted, crate::sqlcache::disabled()) {
        if let Some(index) = tree_guid_index(mods_dir) {
            return w.iter().filter_map(|g| index.get(&g.to_ascii_lowercase()).map(|p| (g.clone(), p.clone()))).collect();
        }
    }
    // Shared index: copying all of it for the few GUIDs a card asks for cost ~0.5 s per open.
    if let Some(all) = crate::sqlcache::guid_index_shared(mods_dir) {
        let Some(w) = wanted else {
            return (*all).clone();
        };
        let mut out = HashMap::new();
        for g in w {
            if let Some(p) = all.get(g).cloned().or_else(|| {
                all.iter()
                    .find(|(k, _)| k.eq_ignore_ascii_case(g))
                    .map(|(_, p)| p.clone())
            }) {
                if p.ok_is_file() {
                    out.insert(g.clone(), p);
                }
            }
        }
        if w.iter().all(|g| out.contains_key(g)) {
            return out;
        }
        // SQL index is missing a wanted GUID (stale seed). Fall through and
        // scan zipmods, then the next sync will pick the new files up.
    }
    let (mut cache, cache_full) = load_guid_cache(mods_dir);
    let mut out = HashMap::new();
    // Never open unused zipmods. Resolve wanted GUIDs from cache + filename only.
    if let Some(w) = wanted {
        for g in w {
            if let Some(p) = cache.get(g).cloned().or_else(|| {
                cache
                    .iter()
                    .find(|(k, _)| k.eq_ignore_ascii_case(g))
                    .map(|(_, p)| p.clone())
            }) {
                if p.ok_is_file() {
                    out.insert(g.clone(), p);
                }
            }
        }
        if w.iter().all(|g| out.contains_key(g)) {
            return out;
        }
        let Ok(walk) = collect_zipmods(mods_dir) else {
            return out;
        };
        // Resolve the remaining GUIDs by reading each zipmod's manifest.xml and
        // matching the GUID exactly. (Fuzzy filename matching is unsafe: e.g.
        // "com.DeathWeasel.Panties" -> tail "panties" would match
        // "[Augh] Hungry Panties" and load the wrong mod.)
        for path in &walk {
            let guids = guids_from_zipmod(path);
            for g in &guids {
                let stale = cache.get(g).map(|p| !p.ok_is_file()).unwrap_or(false);
                if stale {
                    cache.insert(g.clone(), path.clone());
                } else {
                    cache.entry(g.clone()).or_insert_with(|| path.clone());
                }
            }
            for g in w {
                if out.contains_key(g) {
                    continue;
                }
                if guids.iter().any(|x| x.eq_ignore_ascii_case(g)) {
                    out.insert(g.clone(), path.clone());
                }
            }
        }
        save_guid_cache(mods_dir, &cache, cache_full);
        return out;
    }
    if cache_full {
        return cache;
    }
    let Ok(walk) = collect_zipmods(mods_dir) else {
        return out;
    };
    for path in walk {
        let Some(guid) = guid_from_zipmod(&path) else { continue };
        cache.entry(guid.clone()).or_insert(path.clone());
        out.entry(guid).or_insert(path);
    }
    save_guid_cache(mods_dir, &cache, true);
    out
}

pub fn all_zipmods(root: &Path) -> Vec<PathBuf> {
    collect_zipmods(root).unwrap_or_default()
}

fn override_cache_path(mods_dir: &Path) -> PathBuf {
    mods_dir.join(".ok_override_index.tsv")
}

/// Map of lowercased `abdata/...` assetbundle path -> zipmod that provides it.
/// Sideloader lets mods replace vanilla assetbundles, so the game loads the
/// modded file (e.g. `chara/bo_head_00.unity3d`) instead of the disk one.
pub fn override_index(mods_dir: &Path) -> HashMap<String, PathBuf> {
    // A library session's mods tree is the recorded one (plus the card's own zipmods): it cannot change while the
    // library is active, but neither SQL nor the on-disk cache applies to it, and rebuilding opened every recorded
    // zip (about 1.3 s) on each lookup — dozens per card.
    if mods_dir.parent().is_some_and(crate::gamefs::serves_from_library) {
        return crate::gamefs::library_memo("override_index", mods_dir, || std::sync::Arc::new(build_override_index(mods_dir))).as_ref().clone();
    }
    if let Some(m) = crate::sqlcache::override_index(mods_dir) {
        if !m.is_empty() {
            return m;
        }
    }
    let cache = override_cache_path(mods_dir);
    if let Ok(text) = crate::gamefs::read_to_string(&cache) {
        let mut m = HashMap::new();
        for line in text.lines() {
            if let Some((ab, z)) = line.split_once('\t') {
                m.insert(ab.to_string(), PathBuf::from(z));
            }
        }
        if !m.is_empty() {
            return m;
        }
    }
    let m = build_override_index(mods_dir);
    let mut s = String::new();
    for (k, v) in &m {
        s.push_str(k);
        s.push('\t');
        s.push_str(&v.to_string_lossy());
        s.push('\n');
    }
    let _ = fs::write(&cache, s);
    m
}

/// `abdata/...unity3d` path -> first zipmod (sorted walk) that ships it. Names only: no entry is read.
fn build_override_index(mods_dir: &Path) -> HashMap<String, PathBuf> {let _spot=crate::profile::spot("build_override_index");
    let mut m: HashMap<String, PathBuf> = HashMap::new();
    for zip in all_zipmods(mods_dir) {
        let names = crate::gamefs::zip_memo("names", &zip, || std::sync::Arc::new(crate::gamefs::open_zip(&zip).map(|z| z.file_names()).unwrap_or_default()));
        for name in names.iter() {
            let n = name.replace('\\', "/").to_ascii_lowercase();
            if n.ends_with(".unity3d") && n.starts_with("abdata/") {
                m.entry(n).or_insert_with(|| zip.clone());
            }
        }
    }
    m
}

/// Lowercased `abdata/...unity3d` paths this zipmod replaces.
pub(crate) fn zipmod_override_keys(zip: &Path) -> Vec<String> {let _spot=crate::profile::spot("zipmod_override_keys");
    crate::gamefs::zip_memo("override_keys", zip, || zipmod_override_keys_uncached(zip))
}
fn zipmod_override_keys_uncached(zip: &Path) -> Vec<String> {
    let mut out = Vec::new();
    let Some(mut za) = crate::gamefs::open_zip(zip) else {
        return out;
    };
    for i in 0..za.len() {
        let Ok(f) = za.by_index(i) else {
            continue;
        };
        let n = f.name().replace('\\', "/").to_ascii_lowercase();
        if n.ends_with(".unity3d") && n.starts_with("abdata/") {
            out.push(n);
        }
    }
    out
}

pub fn override_bundle(mods_dir: &Path, ab: &str) -> Option<Vec<u8>> {let _spot=crate::profile::spot("override_bundle");
    override_bundle_with_source(mods_dir,ab).map(|(_,bytes)|bytes)
}
pub(crate) fn override_bundle_with_source(mods_dir:&Path,ab:&str)->Option<(PathBuf,Vec<u8>)> {
    let zip=override_bundle_source(mods_dir,ab)?;
    let want = format!("abdata/{}", ab.replace('\\', "/")).to_ascii_lowercase();
    let fname = want.rsplit('/').next().unwrap_or(&want).to_string();
    collect_zipmod_bundle(&zip, &want, &fname).map(|bytes|(zip,bytes.to_vec()))
}
pub(crate) fn override_bundle_source(mods_dir:&Path,ab:&str)->Option<PathBuf> {
    let want = format!("abdata/{}", ab.replace('\\', "/")).to_ascii_lowercase();
    Some(match crate::sqlcache::override_source(mods_dir,&want) {
        Some(source)=>source?,
        None=>override_lookup(mods_dir,&want)?,
    })
}
/// One override_index entry without copying the index: every bundle a card reads asks once, and copying the whole
/// library index for each (~0.15 s) set a floor under every mesh load.
fn override_lookup(mods_dir:&Path,want:&str)->Option<PathBuf> {
    // An OK card's answer (tree_answers.rs): which archive overrides this bundle, without the tree's listings.
    if let Some(answer)=crate::tree_answers::lookup::<Option<String>>("override",want.as_bytes()) {
        return answer.map(|zip|crate::tree_answers::rebase(mods_dir,&zip));
    }
    if crate::tree_answers::unanswered("override",want.as_bytes()) { return None; }
    let found=crate::okstrict::scanning(||override_lookup_in_tree(mods_dir,want));
    if crate::tree_answers::recording() {
        match &found {
            None=>crate::tree_answers::record("override",want.as_bytes(),&None::<String>),
            Some(zip)=>if let Some(zip)=crate::tree_answers::relative(zip) { crate::tree_answers::record("override",want.as_bytes(),&Some(zip)); },
        }
    }
    found
}
fn override_lookup_in_tree(mods_dir:&Path,want:&str)->Option<PathBuf> {
    if mods_dir.parent().is_some_and(crate::gamefs::serves_from_library) {
        let index=crate::gamefs::library_memo("override_index", mods_dir, || std::sync::Arc::new(build_override_index(mods_dir)));
        return index.get(want).cloned();
    }
    override_index(mods_dir).get(want).cloned()
}

fn collect_zipmods(root: &Path) -> std::io::Result<Vec<PathBuf>> {
    collect_zipmods_checked(root).map(|(paths,_)|paths)
}

fn collect_zipmods_checked(root: &Path) -> std::io::Result<(Vec<PathBuf>,bool)> {
    let mut out = Vec::new();
    let mut trackable=true;
    fn rec(dir: &Path, out: &mut Vec<PathBuf>,trackable:&mut bool) -> std::io::Result<()> {
        for entry in crate::gamefs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            let metadata=entry.metadata().ok();
            let linked=metadata.as_ref().map(|m|{
                #[cfg(windows)]
                {m.file_attributes()&0x400!=0}
                #[cfg(not(windows))]
                {m.file_type().is_symlink()}
            }).unwrap_or(true);
            if linked{*trackable=false;}
            // Preserve the old traversal's link-following behavior. A root
            // notification cannot guarantee coverage of linked external data.
            let directory=if linked{path.ok_is_dir()}else{metadata.as_ref().is_some_and(|m|m.ok_is_dir())};
            if directory {
                rec(&path, out,trackable)?;
            } else if path
                .extension()
                .and_then(|e| e.to_str())
                .map(|e| e.eq_ignore_ascii_case("zipmod"))
                .unwrap_or(false)
            {
                out.push(path);
            }
        }
        Ok(())
    }
    rec(root, &mut out,&mut trackable)?;
    Ok((out,trackable))
}

fn guid_from_zipmod(path: &Path) -> Option<String> {let _spot=crate::profile::spot("guid_from_zipmod");
    crate::gamefs::zip_memo("guid", path, || guid_from_zipmod_uncached(path))
}
fn guid_from_zipmod_uncached(path: &Path) -> Option<String> {
    let mut zip = crate::gamefs::open_zip(path)?;
    for i in 0..zip.len() {
        let mut f = zip.by_index(i).ok()?;
        let name = f.name().replace('\\', "/");
        if !name.to_ascii_lowercase().ends_with("manifest.xml") {
            continue;
        }
        let xml = read_manifest(&mut f).ok()?;
        return extract_tag(&xml, "guid");
    }
    None
}

/// Every `<guid>` declared by any manifest.xml inside the zipmod.
pub(crate) fn guids_from_zipmod(path: &Path) -> Vec<String> {let _spot=crate::profile::spot("guids_from_zipmod");
    zipmod_manifests(path).iter().filter_map(|xml| extract_tag(xml, "guid")).collect()
}
#[allow(dead_code)]
fn guids_from_zipmod_uncached(path: &Path) -> Vec<String> {
    let mut out = Vec::new();
    let Some(mut zip) = crate::gamefs::open_zip(path) else {
        return out;
    };
    for i in 0..zip.len() {
        let Ok(mut f) = zip.by_index(i) else {
            continue;
        };
        let name = f.name().replace('\\', "/");
        if !name.to_ascii_lowercase().ends_with("manifest.xml") {
            continue;
        }
        if let Ok(xml) = read_manifest(&mut f) {
            if let Some(g) = extract_tag(&xml, "guid") {
                out.push(g);
            }
        }
    }
    out
}

/// Find the zipmod declaring an UncensorSelector body `guid` and return the
/// raw `.unity3d` bytes of its `oo_base` body file.
pub fn uncensor_body_bytes(mods_dir: &Path, guid: &str) -> Option<(String, Vec<u8>)> {let _spot=crate::profile::spot("uncensor_body_bytes");
    let walk = collect_zipmods(mods_dir).ok()?;
    let tag = format!("<guid>{guid}</guid>");
    for path in walk {
        let Some(mut zip) = crate::gamefs::open_zip(&path) else { continue };
        let mut xml = String::new();
        let mut got = false;
        for i in 0..zip.len() {
            let Ok(mut f) = zip.by_index(i) else { continue };
            let name = f.name().replace('\\', "/");
            if !name.to_ascii_lowercase().ends_with("manifest.xml") {
                continue;
            }
            if let Ok(decoded) = read_manifest(&mut f) {
                xml=decoded;
                got = true;
            }
            break;
        }
        if !got {
            continue;
        }
        let Some(idx) = xml.find(&tag) else { continue };
        let Some(body_start) = xml[..idx].rfind("<body>") else {
            continue;
        };
        let Some(body_end) = xml[idx..].find("</body>").map(|x| idx + x) else {
            continue;
        };
        let block = &xml[body_start..body_end];
        let Some(rel) = extract_tag(block, "file") else {
            continue;
        };
        let want = format!("abdata/{}", rel.replace('\\', "/")).to_ascii_lowercase();
        let fname = want.rsplit('/').next().unwrap_or(&want).to_string();
        for i in 0..zip.len() {
            let Ok(mut f) = zip.by_index(i) else { continue };
            let name = f.name().replace('\\', "/");
            let n = name.to_ascii_lowercase();
            if n.ends_with(&want) || n.ends_with(&fname) {
                let mut bytes = Vec::new();
                if f.read_to_end(&mut bytes).is_ok() && bytes.len() > 16 {
                    return Some((name, bytes));
                }
            }
        }
    }
    None
}

#[derive(Clone, Debug)]
pub(crate) struct UncensorBodySource {
    pub guid: String,
    pub display_name: String,
    pub sex: i32,
    pub file: String,
    pub asset: String,
    pub mm_base: String,
    pub body_material: String,
    pub main_texture: String,
    pub color_mask: String,
    pub(crate) zipmod: PathBuf,
}

fn uncensor_body_descriptors(xml: &str, zipmod: &Path) -> Vec<UncensorBodySource> {
    let mut out = Vec::new();
    let mut remainder = xml;
    while let Some(start) = remainder.find("<body>") {
        remainder = &remainder[start + 6..];
        let Some(end) = remainder.find("</body>") else { break };
        let block = &remainder[..end];
        if let (Some(guid), Some(file)) = (extract_tag(block, "guid"), extract_tag(block, "file")) {
            let sex = if extract_tag(block,"sex").is_some_and(|s|s.eq_ignore_ascii_case("female")) {1} else {0};
            let asset = extract_tag(block,"asset").unwrap_or_else(||
                if sex==1 {"p_cf_body_00".into()} else {"p_cm_body_00".into()});
            out.push(UncensorBodySource {display_name:extract_tag(block,"displayName").unwrap_or_else(||guid.clone()),
                mm_base:extract_tag(block,"oo_base").and_then(|b|extract_tag(&b,"mm_base")).filter(|v|!v.trim().is_empty()).unwrap_or_else(||"chara/mm_base.unity3d".into()),
                body_material:extract_tag(block,"oo_base").and_then(|b|extract_tag(&b,"material")).filter(|v|!v.trim().is_empty()).unwrap_or_else(||if sex==1 {"cf_m_body".into()} else {"cm_m_body".into()}),
                main_texture:extract_tag(block,"oo_base").and_then(|b|extract_tag(&b,"mainTex")).filter(|v|!v.trim().is_empty()).unwrap_or_else(||"cf_body_00_t".into()),
                color_mask:extract_tag(block,"oo_base").and_then(|b|extract_tag(&b,"colorMask")).filter(|v|!v.trim().is_empty()).unwrap_or_else(||if sex==1 {"cf_body_00_mc".into()} else {"cm_body_00_mc".into()}),
                guid,sex,file,asset,zipmod:zipmod.to_path_buf()});
        }
        remainder = &remainder[end + 7..];
    }
    out
}

pub(crate) fn uncensor_draw_bundle(game:&Path,source:&UncensorBodySource)->Option<std::sync::Arc<crate::unity::ParsedBundle>> {
    let bytes=zipmod_bundle_named(&source.zipmod,&source.mm_base).map(crate::unity::SourceBytes::from)
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(&source.mm_base)).ok())?;
    Some(crate::unity::parse_bundle_cached(bytes))
}

pub(crate) fn uncensor_draw_material(game:&Path,source:&UncensorBodySource)->Option<crate::unity::MeshMaterial> {
    uncensor_draw_bundle(game,source)?.material_named(&source.body_material)
}

const UNCENSOR_CONFIG: &str = "BepInEx/config/com.deathweasel.bepinex.uncensorselector.cfg";

/// Where a library session looks for the UncensorSelector configuration, in order: the installation given
/// with the card, the card's directory, the project's `Config/UncensorSelector.cfg` (`OK_UNCENSOR_CONFIG`
/// overrides it). Neither the library's recorded copy (one machine's settings) nor anything else is used.
pub(crate) fn uncensor_config_candidates() -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Some(game) = crate::gamefs::fallback_installation() { out.push(game.join(UNCENSOR_CONFIG)); }
    if let Some(dir) = crate::gamefs::fallback_card_dir() {
        out.push(dir.join("com.deathweasel.bepinex.uncensorselector.cfg"));
        out.push(dir.join("UncensorSelector.cfg"));
    }
    if let Some(p) = std::env::var_os("OK_UNCENSOR_CONFIG") { out.push(PathBuf::from(p)); }
    // The library is <project>/Content/IndependentResources/base_v1. A packaged game reaches it through a junction
    // (<stage>/Content/IndependentResources -> the project's): three levels up from that path is the stage, which has
    // no Config, and the card fell back to the censored base body. The library's real location names the project.
    if let Some(dir) = crate::oklib::active_dir() {
        for lib in [Some(dir.clone()), std::fs::canonicalize(&dir).ok()].into_iter().flatten() {
            if let Some(project) = lib.parent().and_then(Path::parent).and_then(Path::parent) {
                let config = project.join("Config").join("UncensorSelector.cfg");
                if !out.contains(&config) { out.push(config); }
            }
        }
    }
    out
}

/// What a library session reads as the UncensorSelector configuration right now (the OK card's own, else the first
/// candidate that exists): where it came from (the card, a file) and its contents, or "none". An open-cache entry stores it and counts only while it
/// is the same, so a body chosen without the configuration (or with another one) is never reused.
pub(crate) fn uncensor_config_identity() -> String {
    use sha2::Digest;
    let path = crate::oklib::active_dir().unwrap_or_default().join(UNCENSOR_CONFIG);
    let (from, text) = match crate::gamefs::overlay_bytes(&path) {
        Some(bytes) => ("card".to_string(), bytes),
        None => match uncensor_config_candidates().into_iter().find_map(|p| std::fs::read(&p).ok().map(|t| (p, t))) {
            // By contents, not path: C, E and a packaged stage find the same file at different places.
            Some((_, t)) => ("file".to_string(), t),
            None => return "uncensor-config:none".into(),
        },
    };
    let mut text = text;
    text.retain(|&b| b != b'\r');
    format!("uncensor-config:{from}:{}", crate::oklib::hex(&sha2::Sha256::digest(&text)[..16]))
}

fn uncensor_config_text(game_root: &Path) -> Option<String> {let _spot=crate::profile::spot("uncensor_config_text");
    let path = game_root.join(UNCENSOR_CONFIG);
    if !crate::gamefs::serves_from_library(game_root) {
        // An installation (game mode): its own configuration.
        return crate::gamefs::read_to_string(path).ok();
    }
    // An OK card carries the configuration it was made with.
    if let Some(bytes) = crate::gamefs::overlay_bytes(&path) { return String::from_utf8(bytes).ok(); }
    // An original card: the configuration of where it is opened from; recorded into the card being made.
    let candidates = uncensor_config_candidates();
    let Some((from, text)) = candidates.iter().find_map(|p| std::fs::read_to_string(p).ok().map(|t| (p.clone(), t))) else {
        eprintln!("ok-extract: no UncensorSelector configuration (looked in {candidates:?})");
        return None;
    };
    eprintln!("ok-extract: UncensorSelector configuration from {}", from.display());
    crate::gamefs::record_overlay_bytes(&path, text.as_bytes());
    Some(text)
}

fn zipmod_identity(path: &Path) -> String {
    // `file_index` is still unstable. Hardlinks of one zipmod share this index,
    // so the pack and the game copy are scanned once.
    #[cfg(windows)]
    {
        #[repr(C)]
        struct ByHandleFileInformation {
            _attributes: u32,
            _creation_low: u32,
            _creation_high: u32,
            _access_low: u32,
            _access_high: u32,
            _write_low: u32,
            _write_high: u32,
            serial: u32,
            _size_high: u32,
            _size_low: u32,
            _links: u32,
            index_high: u32,
            index_low: u32,
        }
        extern "system" {
            fn CreateFileW(name: *const u16, access: u32, share: u32, security: *mut u8, disposition: u32, flags: u32, template: *mut u8) -> isize;
            fn GetFileInformationByHandle(handle: isize, info: *mut ByHandleFileInformation) -> i32;
            fn CloseHandle(handle: isize) -> i32;
        }
        use std::os::windows::ffi::OsStrExt;
        let mut wide: Vec<u16> = path.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
        unsafe {
            let handle = CreateFileW(wide.as_mut_ptr(), 0x8000_0000, 1 | 2 | 4, std::ptr::null_mut(), 3, 0x0200_0000, std::ptr::null_mut());
            if handle != -1 {
                let mut info = std::mem::zeroed::<ByHandleFileInformation>();
                let ok = GetFileInformationByHandle(handle, &mut info);
                CloseHandle(handle);
                if ok != 0 {
                    return format!("{}:{}:{}", info.serial, info.index_high, info.index_low);
                }
            }
        }
    }
    crate::gamefs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf()).to_string_lossy().into_owned()
}

/// The `mods` tree's zipmods (the OK base library's recorded tree for library sessions), each file once: hardlinked
/// copies share an identity.
fn uncensor_zipmod_paths(game_root: &Path) -> Vec<PathBuf> {
    let mut seen = HashSet::new();
    let mut paths = collect_zipmods(&game_root.join("mods")).unwrap_or_default();
    paths.retain(|path| seen.insert(zipmod_identity(path)));
    paths
}

fn with_zipmod_manifests(path: &Path, mut visit: impl FnMut(&str)) {let _spot=crate::profile::spot("with_zipmod_manifests");
    for xml in zipmod_manifests(path).iter() { visit(xml); }
}

/// Every manifest.xml inside the zipmod (decoded), in archive order; read once per archive generation.
fn zipmod_manifests(path: &Path) -> Arc<Vec<String>> {
    crate::gamefs::zip_memo("manifests", path, || {
        let mut out = Vec::new();
        if let Some(mut zip) = crate::gamefs::open_zip(path) {
            for index in 0..zip.len() {
                let Ok(mut entry) = zip.by_index(index) else { continue };
                if !entry.name().to_ascii_lowercase().ends_with("manifest.xml") { continue; }
                if let Ok(xml) = read_manifest(&mut entry) { out.push(xml); }
            }
        }
        Arc::new(out)
    })
}

fn uncensor_config_default(text:&str,sex:i32)->Option<String> {
    let wanted=if sex==0 {"Default Male Body"} else {"Default Female Body"};
    let mut section="";
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') && line.ends_with(']') {section=&line[1..line.len()-1];continue;}
        if section!="Config" || line.starts_with('#') {continue;}
        if let Some((key,value))=line.split_once('=') {
            if key.trim()==wanted {return Some(value.trim().to_string());}
        }
    }
    None
}

fn select_uncensor_body<'a>(sources:&'a [UncensorBodySource],explicit:Option<&str>,default:Option<&str>)->Option<&'a UncensorBodySource> {
    // UncensorSelectorController.BodyData: a valid saved GUID precedes the
    // configured display name; a missing/uninstalled GUID takes the default.
    explicit.filter(|s|!s.trim().is_empty()).and_then(|guid|sources.iter().find(|s|s.guid==guid))
        .or_else(||default.and_then(|name|sources.iter().find(|s|
            format!("[{}] {}",if s.sex==0 {"Male"} else {"Female"},s.display_name)==name)))
}

/// The installed UncensorSelector bodies and genitals, from every zipmod manifest of the mods tree. Remembered per
/// process for the tree (zip list signature, mounted card pack): scanning all manifests cost ~1 s on every card open.
/// Bodies: the first zipmod naming a GUID provides it. Genitals: one GUID is one candidate and, over the sorted
/// paths, the last one wins (what `select_uncensor_genital` used to find).
pub(crate) fn uncensor_sources(game_root: &Path) -> Arc<(Vec<UncensorBodySource>, Vec<UncensorGenitalSource>)> {
    // An OK card's answer (tree_answers.rs): the tree's UncensorSelector manifests, without reading its archives.
    if let Some(rows) = crate::tree_answers::lookup::<Vec<(String, Vec<String>)>>("uncensor", b"") {
        let mods = game_root.join("mods");
        let manifests: Vec<(PathBuf, Vec<String>)> = rows.into_iter().map(|(zip, xmls)| (crate::tree_answers::rebase(&mods, &zip), xmls)).collect();
        return Arc::new(uncensor_sources_of(&manifests));
    }
    if crate::tree_answers::unanswered("uncensor", b"") { return Arc::new((Vec::new(), Vec::new())); }
    let (out, manifests) = crate::okstrict::scanning(|| uncensor_sources_in_tree(game_root));
    if crate::tree_answers::recording() {
        let rows: Option<Vec<(String, Vec<String>)>> = manifests.iter().map(|(p, x)| crate::tree_answers::relative(p).map(|z| (z, x.clone()))).collect();
        if let Some(rows) = rows { crate::tree_answers::record("uncensor", b"", &rows); }
    }
    out
}

type UncensorSources = Arc<(Vec<UncensorBodySource>, Vec<UncensorGenitalSource>)>;
/// `uncensor_sources` from the tree, with the manifests it was built from (archives with UncensorSelector parts).
fn uncensor_sources_in_tree(game_root: &Path) -> (UncensorSources, Arc<Vec<(PathBuf, Vec<String>)>>) {
    use std::sync::Mutex;
    let key = format!("{}\n{:016x}\n{}", game_root.display(), crate::list::zip_list_sig(&game_root.join("mods")),
        crate::oklib::mounted_pack_mods_identity());
    static MEMO: Mutex<Vec<(String, UncensorSources, Arc<Vec<(PathBuf, Vec<String>)>>)>> = Mutex::new(Vec::new());
    if let Some((_, hit, manifests)) = MEMO.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(k, _, _)| *k == key) {
        if hit.0.iter().map(|s| &s.zipmod).chain(hit.1.iter().map(|s| &s.zipmod)).all(|p| p.ok_is_file()) { return (hit.clone(), manifests.clone()); }
    }
    if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: MEMO miss uncensor_sources key={}", key.replace('\n', " | ")); }
    let paths = uncensor_zipmod_paths(game_root);
    // Each archive's UncensorSelector manifests, read at once; an archive the active library records cannot change,
    // so its answer is kept by path, across processes too (oklib::library_scan_load): the first card of a process
    // read ~7000 archives for this (~0.5 s).
    static RECORDED: Mutex<Option<HashMap<PathBuf, Vec<String>>>> = Mutex::new(None);
    let recorded = crate::gamefs::library_records_zips(&paths);
    let known: Vec<Option<Vec<String>>> = {
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        if map.is_none() { *map = crate::oklib::library_scan_load::<HashMap<PathBuf, Vec<String>>>("uncensor-manifests-v1"); }
        paths.iter().zip(&recorded).map(|(p, r)| if *r { map.as_ref().and_then(|m| m.get(p).cloned()) } else { None }).collect()
    };
    let fresh_recorded = recorded.iter().zip(&known).any(|(r, k)| *r && k.is_none());
    let found: Vec<Vec<String>> = { use rayon::prelude::*; crate::oklib::pack_parallel(|| paths.par_iter().zip(known.into_par_iter()).map(|(path, k)| k.unwrap_or_else(|| {
        let mut xmls = Vec::new();
        with_zipmod_manifests(path, |xml| {
            let lower = xml.to_ascii_lowercase();
            if lower.contains("<body>") || lower.contains("<penis>") || lower.contains("<balls>") { xmls.push(xml.to_string()); }
        });
        xmls
    })).collect()) };
    {
        let mut map = RECORDED.lock().unwrap_or_else(|e| e.into_inner());
        let map = map.get_or_insert_with(HashMap::new);
        for ((p, r), xmls) in paths.iter().zip(&recorded).zip(&found) { if *r { map.entry(p.clone()).or_insert_with(|| xmls.clone()); } }
        if fresh_recorded { crate::oklib::library_scan_save("uncensor-manifests-v1", &*map); }
    }
    let manifests: Vec<(PathBuf, Vec<String>)> = paths.iter().cloned().zip(found).filter(|(_, x)| !x.is_empty()).collect();
    let out = Arc::new(uncensor_sources_of(&manifests));
    let manifests = Arc::new(manifests);
    let mut memo = MEMO.lock().unwrap_or_else(|e| e.into_inner());
    memo.retain(|(k, _, _)| *k != key);
    if memo.len() >= 8 { memo.remove(0); }
    memo.push((key, out.clone(), manifests.clone()));
    (out, manifests)
}

/// The installed bodies and genitals the manifests declare (archives in walk order).
fn uncensor_sources_of(manifests: &[(PathBuf, Vec<String>)]) -> (Vec<UncensorBodySource>, Vec<UncensorGenitalSource>) {
    let mut bodies: Vec<UncensorBodySource> = Vec::new();
    for (path, xmls) in manifests {
        for xml in xmls {
            for source in uncensor_body_descriptors(xml, path) {
                if bodies.iter().any(|have| have.guid == source.guid) { continue; }
                bodies.push(source);
            }
        }
    }
    let mut sorted: Vec<&(PathBuf, Vec<String>)> = manifests.iter().collect();
    sorted.sort_by(|a, b| a.0.cmp(&b.0));
    let mut genitals: Vec<UncensorGenitalSource> = Vec::new();
    for (path, xmls) in sorted {
        for xml in xmls {
            for source in uncensor_genital_descriptors(xml, path) {
                if let Some(index) = genitals.iter().position(|have| have.kind == source.kind && have.guid == source.guid) {
                    genitals[index] = source;
                } else {
                    genitals.push(source);
                }
            }
        }
    }
    (bodies, genitals)
}

pub(crate) fn uncensor_body_for_card(game_root:&Path,explicit:Option<&str>,sex:i32)->Option<(UncensorBodySource,Vec<u8>)> {let _spot=crate::profile::spot("uncensor_body_for_card");
    // A card saved on the censored body still follows the installed selector.
    // select_uncensor_body misses this guid and uses the configured body.
    if matches!(explicit,Some("Default.Body.Female"|"Default.Body.Male")) {
        eprintln!("ok-extract: card body guid is the mosaic default; using the installed uncensor");
    }
    let config=uncensor_config_text(game_root);
    let default=config.as_deref().and_then(|text|uncensor_config_default(text,sex));
    if explicit.is_none() && default.is_none() {
        eprintln!("ok-extract: base body kept: the card names no body and the configuration none (sex {sex}, configuration {})",
            if config.is_some() {"read"} else {"missing"});
        return None;
    }
    let installed=uncensor_sources(game_root);
    let sources=&installed.0;
    let Some(selected)=select_uncensor_body(sources,explicit,default.as_deref()).cloned() else {
        // Random needs the game's seeded choice and registration order. Do
        // not pretend the first installed mod or a captured mesh is that choice.
        if default.as_deref()==Some("Random") {eprintln!("ok-extract: UncensorSelector Random body choice is unresolved; keeping base mesh");}
        else {eprintln!("ok-extract: base body kept: card body {explicit:?} / configured {default:?} not among {} installed bodies", sources.len());}
        return None;
    };
    let want=format!("abdata/{}",selected.file.replace('\\',"/")).to_ascii_lowercase();
    let Some(bytes)=zipmod_entry_suffix(&selected.zipmod,&want) else {
        eprintln!("ok-extract: base body kept: {} ({}) has no {want}", selected.guid, selected.zipmod.display());
        return None;
    };
    Some((selected,(*bytes).clone()))
}

/// The bytes of the zipmod entry named `want` (lower case, `/`), or ending in `/want`; read once per archive
/// generation (the uncensor bundles every card opens cost ~0.2 s to inflate each time).
fn zipmod_entry_suffix(zipmod: &Path, want: &str) -> Option<Arc<Vec<u8>>> {
    crate::gamefs::zip_memo(&format!("entry_suffix {want}"), zipmod, || {
        let mut zip = crate::gamefs::open_zip(zipmod)?;
        for index in 0..zip.len() {
            let Ok(mut entry) = zip.by_index(index) else { continue };
            let name = entry.name().replace('\\', "/").to_ascii_lowercase();
            if name == want || name.ends_with(&format!("/{want}")) {
                let mut bytes = Vec::new();
                entry.read_to_end(&mut bytes).ok()?;
                return Some(Arc::new(bytes));
            }
        }
        None
    })
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum UncensorGenitalKind { Penis, Balls }

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct UncensorColorMatch {
    pub object: String,
    pub material: String,
    pub material_create: String,
    pub main_tex: String,
    pub color_mask: String,
}

#[derive(Clone, Debug)]
pub(crate) struct UncensorGenitalSource {
    pub kind: UncensorGenitalKind,
    pub guid: String,
    pub display_name: String,
    pub file: String,
    pub asset: String,
    pub allow_random: bool,
    pub color_matches: Vec<UncensorColorMatch>,
    pub zipmod: PathBuf,
}

pub(crate) fn mosaic_uncensor(guid: &str, display: &str) -> bool {
    let guid = guid.trim();
    let display = display.trim();
    guid.eq_ignore_ascii_case("Default.Penis")
        || guid.eq_ignore_ascii_case("Default.Balls")
        || display.eq_ignore_ascii_case("Mosaic Penis")
        || display.eq_ignore_ascii_case("Mosaic Balls")
        || display.to_ascii_lowercase().contains("mosaic")
}

fn uncensor_config_value(text: &str, key: &str) -> Option<String> {
    let mut section = "";
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') && line.ends_with(']') {
            section = &line[1..line.len() - 1];
            continue;
        }
        if !section.eq_ignore_ascii_case("Config") || line.starts_with('#') || line.is_empty() {
            continue;
        }
        if let Some((found, value)) = line.split_once('=') {
            if found.trim() == key {
                let value = value.trim();
                if !value.is_empty() { return Some(value.to_string()); }
            }
        }
    }
    None
}

fn uncensor_excluded_guids(text: &str, key: &str) -> Vec<String> {
    let mut section = "";
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') && line.ends_with(']') {
            section = &line[1..line.len() - 1];
            continue;
        }
        if !section.eq_ignore_ascii_case("Random Excluded") || line.starts_with('#') || line.is_empty() {
            continue;
        }
        if let Some((found, value)) = line.split_once('=') {
            if found.trim() == key {
                return value.split([',', ';']).map(str::trim).filter(|s| !s.is_empty()).map(str::to_string).collect();
            }
        }
    }
    Vec::new()
}

fn genital_blocks(xml: &str, tag: &str) -> Vec<String> {
    let mut out = Vec::new();
    let open = format!("<{tag}>");
    let close = format!("</{tag}>");
    let lower = xml.to_ascii_lowercase();
    let mut from = 0;
    while let Some(at) = lower[from..].find(&open.to_ascii_lowercase()) {
        let start = from + at + open.len();
        let Some(end) = lower[start..].find(&close.to_ascii_lowercase()) else { break };
        out.push(xml[start..start + end].to_string());
        from = start + end + close.len();
    }
    out
}

fn color_matches_in(block: &str) -> Vec<UncensorColorMatch> {
    genital_blocks(block, "colorMatch").into_iter().filter_map(|part| {
        let object = extract_tag(&part, "object")?;
        let material = extract_tag(&part, "material")?;
        let material_create = extract_tag(&part, "materialCreate")?;
        let main_tex = extract_tag(&part, "mainTex")?;
        let color_mask = extract_tag(&part, "colorMask")?;
        if object.is_empty() || material.is_empty() || material_create.is_empty()
            || main_tex.is_empty() || color_mask.is_empty() {
            return None;
        }
        Some(UncensorColorMatch { object, material, material_create, main_tex, color_mask })
    }).collect()
}

fn uncensor_genital_descriptors(xml: &str, zipmod: &Path) -> Vec<UncensorGenitalSource> {
    let mut out = Vec::new();
    for (tag, kind) in [("penis", UncensorGenitalKind::Penis), ("balls", UncensorGenitalKind::Balls)] {
        for block in genital_blocks(xml, tag) {
            let Some(guid) = extract_tag(&block, "guid").filter(|s| !s.is_empty()) else { continue };
            let Some(file) = extract_tag(&block, "file").filter(|s| !s.is_empty()) else { continue };
            let Some(asset) = extract_tag(&block, "asset").filter(|s| !s.is_empty()) else { continue };
            let display_name = extract_tag(&block, "displayName").filter(|s| !s.is_empty()).unwrap_or_else(|| guid.clone());
            let allow = extract_tag(&block, "allowRandom").map(|s| {
                let s = s.to_ascii_lowercase();
                s != "false" && s != "0"
            }).unwrap_or(true);
            out.push(UncensorGenitalSource {
                kind, guid, display_name, file, asset, allow_random: allow,
                color_matches: color_matches_in(&block), zipmod: zipmod.to_path_buf(),
            });
        }
    }
    out
}

/// .NET Framework `System.Random`, the generator UncensorSelector seeds with
/// `birthDay + personality + (int)(voicePitch * 100)`.
fn dotnet_random_ints(seed: i32, maxima: &[i32]) -> Vec<i32> {
    const MBIG: i32 = i32::MAX;
    const MSEED: i32 = 161803398;
    let subtraction = if seed == i32::MIN { i32::MAX } else { seed.abs() };
    let mut seed_array = [0i32; 56];
    let mut mj: i32 = MSEED - subtraction;
    seed_array[55] = mj;
    let mut mk: i32 = 1;
    for i in 1..55 {
        let ii = (21 * i) % 55;
        seed_array[ii] = mk;
        let mut diff = mj as i64 - mk as i64;
        if diff < 0 { diff += MBIG as i64; }
        mk = diff as i32;
        mj = seed_array[ii];
    }
    for _ in 1..5 {
        for i in 1..56 {
            let mut value = seed_array[i] as i64 - seed_array[1 + (i + 30) % 55] as i64;
            if value < 0 { value += MBIG as i64; }
            seed_array[i] = value as i32;
        }
    }
    let mut inext = 0i32;
    let mut inextp = 21i32;
    let mut sample = || -> i32 {
        inext += 1;
        inextp += 1;
        if inext >= 56 { inext = 1; }
        if inextp >= 56 { inextp = 1; }
        let mut ret = seed_array[inext as usize] - seed_array[inextp as usize];
        if ret == MBIG { ret -= 1; }
        if ret < 0 { ret += MBIG; }
        seed_array[inext as usize] = ret;
        ret
    };
    maxima.iter().map(|max| (sample() as f64 * (1.0 / MBIG as f64) * *max as f64) as i32).collect()
}

fn select_uncensor_genital<'a>(
    sources: &'a [UncensorGenitalSource],
    kind: UncensorGenitalKind,
    explicit: Option<&str>,
    configured: Option<&str>,
    excluded: &[String],
    seed: i32,
) -> Option<&'a UncensorGenitalSource> {
    let same: Vec<&UncensorGenitalSource> = sources.iter().filter(|s| s.kind == kind).collect();
    let usable = |s: &UncensorGenitalSource| !mosaic_uncensor(&s.guid, &s.display_name);
    if let Some(guid) = explicit.map(str::trim).filter(|s| !s.is_empty() && !mosaic_uncensor(s, "")) {
        if let Some(found) = same.iter().rev().find(|s| s.guid == guid && usable(s)) {
            return Some(*found);
        }
    }
    if let Some(name) = configured.map(str::trim).filter(|s| !s.is_empty() && !s.eq_ignore_ascii_case("Random")) {
        if let Some(found) = same.iter().rev().find(|s| s.display_name.eq_ignore_ascii_case(name) && usable(s)) {
            return Some(*found);
        }
    }
    let mut pool: Vec<&UncensorGenitalSource> = same.into_iter().filter(|s| s.allow_random && usable(s)
        && !excluded.iter().any(|g| g.eq_ignore_ascii_case(&s.guid))).collect();
    if pool.is_empty() { return None; }
    pool.sort_by(|a, b| a.guid.cmp(&b.guid).then_with(|| a.zipmod.cmp(&b.zipmod)));
    let index = dotnet_random_ints(seed, &[pool.len() as i32])[0];
    pool.get(index as usize).copied()
}

fn genital_config_key(kind: UncensorGenitalKind, sex: i32) -> &'static str {
    match (kind, sex) {
        (UncensorGenitalKind::Penis, 0) => "Default Male Penis",
        (UncensorGenitalKind::Penis, _) => "Default Female Penis",
        (UncensorGenitalKind::Balls, 0) => "Default Male Balls",
        (UncensorGenitalKind::Balls, _) => "Default Female Balls",
    }
}

fn genital_exclude_key(kind: UncensorGenitalKind, sex: i32) -> &'static str {
    match (kind, sex) {
        (UncensorGenitalKind::Penis, 0) => "Male Penis",
        (UncensorGenitalKind::Penis, _) => "Female Penis",
        (UncensorGenitalKind::Balls, 0) => "Male Balls",
        (UncensorGenitalKind::Balls, _) => "Female Balls",
    }
}

pub(crate) fn uncensor_config_flag(text: &str, key: &str) -> Option<bool> {
    uncensor_config_value(text, key).map(|value| {
        let value = value.to_ascii_lowercase();
        value != "false" && value != "0"
    })
}

/// The installed penis or balls prefab. A saved mosaic GUID and the built-in
/// Mosaic Penis / Mosaic Balls defaults are never returned.
pub(crate) fn uncensor_genital_for_card(
    game_root: &Path,
    kind: UncensorGenitalKind,
    explicit: Option<&str>,
    sex: i32,
    seed: i32,
) -> Option<(UncensorGenitalSource, Vec<u8>)> {let _spot=crate::profile::spot("uncensor_genital_for_card");
    let config = uncensor_config_text(game_root);
    let configured = config.as_deref().and_then(|text| uncensor_config_value(text, genital_config_key(kind, sex)));
    let excluded = config.as_deref().map(|text| uncensor_excluded_guids(text, genital_exclude_key(kind, sex))).unwrap_or_default();
    if explicit.is_some_and(|g| mosaic_uncensor(g, "")) {
        eprintln!("ok-extract: card {:?} guid is the mosaic default; using the installed uncensor", kind);
    }
    let installed = uncensor_sources(game_root);
    let sources = &installed.1;
    let selected = select_uncensor_genital(sources, kind, explicit, configured.as_deref(), &excluded, seed)?.clone();
    if configured.as_deref().is_some_and(|name| name.eq_ignore_ascii_case("Random"))
        && explicit.map(str::trim).filter(|s| !s.is_empty() && !mosaic_uncensor(s, "")).is_none() {
        eprintln!("ok-extract: uncensor {:?} random guid={} display={} (candidates sorted by guid; the game registry order is not stored)",
            kind, selected.guid, selected.display_name);
    }
    let want = format!("abdata/{}", selected.file.replace('\\', "/")).to_ascii_lowercase();
    if let Some(bytes) = zipmod_entry_suffix(&selected.zipmod, &want) { return Some((selected, (*bytes).clone())); }
    eprintln!("ok-extract: uncensor {:?} bundle missing file={}", kind, selected.file);
    None
}

#[test]
fn uncensor_draw_source_uses_nested_metadata_and_sex_defaults() {
    let xml="<body><guid>a</guid><sex>female</sex><oo_base><file>chara/a.unity3d</file><mm_base>chara/custom.unity3d</mm_base><material>custom_draw</material></oo_base></body><body><guid>b</guid><sex>male</sex><oo_base><file>chara/b.unity3d</file><mm_base> </mm_base><material></material></oo_base></body>";
    let sources=uncensor_body_descriptors(xml,Path::new("test.zipmod"));
    assert_eq!(sources.len(),2);
    assert_eq!(sources[0].mm_base,"chara/custom.unity3d");
    assert_eq!(sources[0].body_material,"custom_draw");
    assert_eq!(sources[1].mm_base,"chara/mm_base.unity3d");
    assert_eq!(sources[1].body_material,"cm_m_body");
    assert_eq!(sources[1].color_mask,"cm_body_00_mc");
    let source=uncensor_body_descriptors("<body><guid>custom</guid><sex>female</sex><oo_base><file>chara/body.unity3d</file><mainTex>custom_skin</mainTex><colorMask>custom_mask</colorMask></oo_base></body>",Path::new("test.zipmod"));
    assert_eq!(source[0].main_texture,"custom_skin");
    assert_eq!(source[0].color_mask,"custom_mask");
}

#[test]
fn uncensor_saved_guid_precedes_configured_body_and_uses_exact_sex_key() {
    let sources=uncensor_body_descriptors("<body><guid>a</guid><displayName>Saved</displayName><sex>female</sex><oo_base><file>chara/a.unity3d</file></oo_base></body><body><guid>b</guid><displayName>Configured</displayName><sex>female</sex><oo_base><file>chara/b.unity3d</file><asset>custom_body</asset></oo_base></body>",Path::new("test.zipmod"));
    let config="[Config]\nDefault Male Body = Random\nDefault Female Body = [Female] Configured\nDefault Female Body In Maker = [Female] Saved\n[Random Excluded]\nDefault Female Body = Wrong";
    assert_eq!(uncensor_config_default(config,1).as_deref(),Some("[Female] Configured"));
    assert_eq!(select_uncensor_body(&sources,Some("a"),Some("[Female] Configured")).unwrap().guid,"a");
    assert_eq!(select_uncensor_body(&sources,None,Some("[Female] Configured")).unwrap().asset,"custom_body");
    assert_eq!(select_uncensor_body(&sources,Some("missing"),Some("[Female] Configured")).unwrap().guid,"b");
    assert_eq!(select_uncensor_body(&sources,Some("Default.Body.Female"),Some("[Female] Configured")).unwrap().guid,"b");
    assert!(select_uncensor_body(&sources,None,Some("Random")).is_none());
}

#[test]
#[ignore="installed card body GUID and configured UncensorSelector source"]
fn new_cards_uncensor_body_sources() {
    let game=&crate::test_support::game_path("");
    for name in ["KK_226176.png","KK_384914.png","KK_398302.png","弹丸论破 - 七海千秋3.png"] {
        let card=crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        let config=crate::gamefs::read_to_string(game.join("BepInEx/config/com.deathweasel.bepinex.uncensorselector.cfg")).unwrap();
        eprintln!("BODY SOURCE card={name} saved={:?} sex={} configured={:?}",card.body_guid,card.sex,uncensor_config_default(&config,card.sex));
        let (selected,bytes)=uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex).expect("installed body choice");
        eprintln!("BODY SELECTED {} {} bytes={}",selected.guid,selected.asset,bytes.len());
        assert_eq!(selected.guid,if name=="KK_226176.png" {"SAC.innie2"} else {"BP.profundis"});
        let bundle=crate::unity::parse_bundle_ref(&bytes);
        let (meshes,_)=bundle.meshes_for_maindata(&selected.asset);
        let body:Vec<_>=meshes.iter().filter(|m|m.name=="o_body_a").collect();
        eprintln!("BODY MESHES count={} body={:?}",meshes.len(),body.iter().map(|m|(m.positions.len(),m.weights.len(),m.bone_names.len())).collect::<Vec<_>>());
        assert!(body.iter().any(|m|m.positions.len()==if name=="KK_226176.png" {9080} else {9823} && !m.weights.is_empty()),"selected prefab must include native body geometry and skin weights");
        let native_root=game.join("mesh_dump").join(name);
        let native_path=crate::gamefs::read_dir(&native_root).unwrap().filter_map(Result::ok)
            .filter(|entry|entry.path().ok_is_dir()).flat_map(|entry|crate::gamefs::read_dir(entry.path()).unwrap().filter_map(Result::ok))
            .map(|entry|entry.path()).find(|path|path.file_name().unwrap().to_string_lossy().ends_with("_o_body_a.json")).unwrap();
        let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(native_path).unwrap()).unwrap();
        let mesh=body.iter().find(|m|m.positions.len()==native["mesh"]["vertexCount"].as_u64().unwrap() as usize).unwrap();
        let weights=native["mesh"]["boneWeights"].as_array().unwrap();
        assert_eq!(weights.len(),mesh.weights.len());
        let mut max_error=0f32;
        for (i,row) in weights.iter().enumerate() {for axis in 0..4 {
            let weight=row[format!("w{axis}")].as_f64().unwrap() as f32;
            max_error=max_error.max((weight-mesh.weights[i][axis]).abs());
            if weight>1e-6 {assert_eq!(row[format!("i{axis}")].as_u64().unwrap() as u16,mesh.bone_idx[i][axis],"native skin index vertex={i} axis={axis}");}
        }}
        assert!(max_error<1e-6,"native source skin weight error {max_error}");
        for (i,row) in native["bones"].as_array().unwrap().iter().enumerate() {
            assert_eq!(row["transform"]["name"].as_str().unwrap(),mesh.bone_names[i],"native SMR bone order index={i}");
        }
        eprintln!("BODY NATIVE weights={} max_error={max_error} bones={}",weights.len(),mesh.bone_names.len());
    }
}

#[test]
fn uncensor_genital_selects_named_penis_and_never_mosaic() {
    let xml = r#"<KK_UncensorSelector>
        <penis><guid>SoS</guid><displayName>SoS</displayName><file>chara/sos.unity3d</file><asset>sos_dick</asset>
            <colorMatch><object>o_dankon</object><material>cm_m_dankon</material><materialCreate>cm_m_dankon_create</materialCreate><mainTex>cm_dankon_00_t</mainTex><colorMask>cm_dankon_00_mc</colorMask></colorMatch>
        </penis>
        <penis><guid>animal42069.BP_Cartoon</guid><displayName>BP Cartoon</displayName><file>chara/bp_cartoon.unity3d</file><asset>cartoon_uncensor</asset><allowRandom>true</allowRandom>
            <colorMatch><object>o_dankon</object><material>cm_m_dankon</material><materialCreate>cm_m_dankon_create</materialCreate><mainTex>cm_dankon_00_t</mainTex><colorMask>cm_dankon_00_mc</colorMask></colorMatch>
        </penis>
        <penis><guid>Default.Penis</guid><displayName>Mosaic Penis</displayName><file>chara/oo_base.unity3d</file><asset>p_cm_body_00</asset></penis>
        <balls><guid>bp.balls.color_match</guid><displayName>BP Balls</displayName><file>chara/bp_balls.unity3d</file><asset>balls</asset>
            <colorMatch><object>o_dan_f</object><material>cm_m_dan_f</material><materialCreate>cm_m_dan_f_create</materialCreate><mainTex>cm_dan_f_00_t</mainTex><colorMask>cm_dan_f_00_mc</colorMask></colorMatch>
        </balls>
        <balls><guid>mosaic.balls</guid><displayName>Mosaic Balls</displayName><file>chara/oo_base.unity3d</file><asset>p_cm_body_00</asset></balls>
    </KK_UncensorSelector>"#;
    let sources = uncensor_genital_descriptors(xml, Path::new("pack.zipmod"));
    assert_eq!(sources.len(), 5);
    assert_eq!(sources[1].color_matches[0].main_tex, "cm_dankon_00_t");
    let config = "[Config]\nDefault Male Penis = BP Cartoon\nDefault Male Balls = Random\nDefault Female Penis = Mosaic Penis\n[Random Excluded]\nMale Penis = SoS\n";
    assert_eq!(uncensor_config_value(config, "Default Male Penis").as_deref(), Some("BP Cartoon"));
    let excluded = uncensor_excluded_guids(config, "Male Penis");
    let named = select_uncensor_genital(&sources, UncensorGenitalKind::Penis, None, Some("BP Cartoon"), &excluded, 72).unwrap();
    assert_eq!(named.guid, "animal42069.BP_Cartoon");
    let saved = select_uncensor_genital(&sources, UncensorGenitalKind::Penis, Some("SoS"), Some("BP Cartoon"), &excluded, 72).unwrap();
    assert_eq!(saved.guid, "SoS");
    let mosaic_card = select_uncensor_genital(&sources, UncensorGenitalKind::Penis, Some("Default.Penis"), Some("BP Cartoon"), &excluded, 72).unwrap();
    assert_eq!(mosaic_card.guid, "animal42069.BP_Cartoon");
    let female_mosaic_config = select_uncensor_genital(&sources, UncensorGenitalKind::Penis, None, Some("Mosaic Penis"), &excluded, 72).unwrap();
    assert_eq!(female_mosaic_config.guid, "animal42069.BP_Cartoon");
    let balls = select_uncensor_genital(&sources, UncensorGenitalKind::Balls, Some("mosaic.balls"), Some("Mosaic Balls"), &[], 1).unwrap();
    assert_eq!(balls.guid, "bp.balls.color_match");
    let only_mosaic: Vec<_> = sources.iter().filter(|s| mosaic_uncensor(&s.guid, &s.display_name)).cloned().collect();
    assert!(select_uncensor_genital(&only_mosaic, UncensorGenitalKind::Penis, None, Some("Mosaic Penis"), &[], 1).is_none());
}

#[test]
fn dotnet_random_matches_framework_system_random() {
    assert_eq!(dotnet_random_ints(42, &[10, 100]), vec![6, 14]);
    assert_eq!(dotnet_random_ints(1, &[5, 5]), vec![1, 0]);
    assert_eq!(dotnet_random_ints(72, &[8]), vec![2]);
}

#[test]
#[ignore = "installed KoikatuSunshine uncensor skin"]
fn installed_uncensor_penis_and_body_skin_are_not_black() {
    let game = &crate::test_support::game_path("");
    let config = crate::gamefs::read_to_string(game.join("BepInEx/config/com.deathweasel.bepinex.uncensorselector.cfg")).unwrap();
    let (penis, bytes) = uncensor_genital_for_card(game, UncensorGenitalKind::Penis, None, 0, 0).expect("male penis");
    assert!(!mosaic_uncensor(&penis.guid, &penis.display_name));
    if uncensor_config_value(&config, "Default Male Penis").as_deref() == Some("BP Cartoon") {
        assert_eq!(penis.guid, "animal42069.BP_Cartoon");
    }
    let bundle = crate::unity::parse_bundle_cached(bytes);
    let part = penis.color_matches.first().expect("penis color match");
    let main = bundle.texture_png_named(&part.main_tex).expect("penis main texture");
    let mask = bundle.texture_png_named(&part.color_mask).expect("penis color mask");
    let skin = [0.78f32, 0.56, 0.46, 1.0];
    let baked = crate::unity::apply_body_create_colors_sampled(&main, &mask, skin, skin, [1.0; 4], false).expect("penis bake");
    let image = crate::teximg::load_from_memory(&baked).unwrap().to_rgba8();
    let mut sum = 0u64;
    let mut lit = 0u64;
    for pixel in image.pixels() {
        sum += pixel[0] as u64 + pixel[1] as u64 + pixel[2] as u64;
        if pixel[0] > 40 || pixel[1] > 30 { lit += 1; }
    }
    let count = image.pixels().len() as u64;
    let mean = sum as f64 / (count as f64 * 3.0);
    let (meshes, _) = bundle.meshes_for_maindata(&penis.asset);
    let dick = meshes.iter().find(|m| m.name.eq_ignore_ascii_case("o_dankon")).expect("o_dankon");
    eprintln!("PENIS guid={} mean={mean:.1} lit={lit}/{count} verts={} weights={} shader={}",
        penis.guid, dick.positions.len(), dick.weights.len(),
        dick.mat.as_ref().map(|m| m.shader.as_str()).unwrap_or("none"));
    assert!(mean > 20.0, "penis skin mean {mean} is black");
    assert!(lit * 5 > count, "penis skin is mostly black");
    assert!(!dick.weights.is_empty(), "penis mesh is not skinned");
    let (balls, ball_bytes) = uncensor_genital_for_card(game, UncensorGenitalKind::Balls, None, 0, 72).expect("balls");
    assert!(!mosaic_uncensor(&balls.guid, &balls.display_name));
    let ball_bundle = crate::unity::parse_bundle_cached(ball_bytes);
    let ball_part = balls.color_matches.first().expect("balls color match");
    let ball_main = ball_bundle.texture_png_named(&ball_part.main_tex).expect("balls main");
    let ball_mask = ball_bundle.texture_png_named(&ball_part.color_mask).expect("balls mask");
    let ball_baked = crate::unity::apply_body_create_colors_sampled(&ball_main, &ball_mask, skin, skin, [1.0; 4], false).expect("balls bake");
    let ball_image = crate::teximg::load_from_memory(&ball_baked).unwrap().to_rgba8();
    let ball_mean = ball_image.pixels().map(|p| p[0] as u64 + p[1] as u64 + p[2] as u64).sum::<u64>() as f64
        / (ball_image.pixels().len() as f64 * 3.0);
    eprintln!("BALLS guid={} display={} mean={ball_mean:.1}", balls.guid, balls.display_name);
    assert!(ball_mean > 20.0, "balls skin mean {ball_mean} is black");
    let (body, body_bytes) = uncensor_body_for_card(game, None, 1).expect("female body");
    let body_bundle = crate::unity::parse_bundle_ref(&body_bytes);
    let body_main = body_bundle.texture_png_named(&body.main_texture).expect("vagina body main");
    let body_mask = body_bundle.texture_png_named(&body.color_mask).expect("vagina body mask");
    let vanilla = crate::unity::parse_bundle_cached(crate::gamefs::read(game.join("abdata/chara/oo_base.unity3d")).unwrap());
    let vanilla_main = vanilla.texture_png_named("cf_body_00_t");
    eprintln!("BODY guid={} main_bytes={} mask_bytes={} same_as_vanilla={}",
        body.guid, body_main.len(), body_mask.len(), vanilla_main.as_ref().is_some_and(|v| v == &body_main));
    let body_baked = crate::unity::apply_body_create_colors_sampled(&body_main, &body_mask, skin, skin, [1.0; 4], false).unwrap();
    let body_image = crate::teximg::load_from_memory(&body_baked).unwrap().to_rgba8();
    let body_mean = body_image.pixels().map(|p| p[0] as u64 + p[1] as u64 + p[2] as u64).sum::<u64>() as f64
        / (body_image.pixels().len() as f64 * 3.0);
    assert!(body_mean > 20.0, "vagina/body skin mean {body_mean} is black");
    let (body_meshes, _) = body_bundle.meshes_for_maindata(&body.asset);
    let body_mesh = body_meshes.iter().find(|m| m.name.eq_ignore_ascii_case("o_body_a")).expect("o_body_a");
    let mut u0 = 1f32;
    let mut u1 = 0f32;
    let mut v0 = 1f32;
    let mut v1 = 0f32;
    let mut crotch = 0usize;
    for (i, weights) in body_mesh.weights.iter().enumerate() {
        let influenced = (0..4).any(|axis| weights[axis] > 0.2 && body_mesh.bone_names.get(body_mesh.bone_idx[i][axis] as usize)
            .is_some_and(|name| {
                let n = name.to_ascii_lowercase();
                n.contains("kokan") || n.contains("vagina")
            }));
        if !influenced { continue; }
        crotch += 1;
        let uv = body_mesh.uvs[i];
        u0 = u0.min(uv[0]); u1 = u1.max(uv[0]); v0 = v0.min(uv[1]); v1 = v1.max(uv[1]);
    }
    assert!(crotch > 0, "uncensor body has no vagina-weighted vertices");
    let w = body_image.width() as f32;
    let h = body_image.height() as f32;
    let x0 = (u0.clamp(0.0, 1.0) * (w - 1.0)).floor() as u32;
    let x1 = (u1.clamp(0.0, 1.0) * (w - 1.0)).ceil().min(w - 1.0) as u32;
    let y_top = ((1.0 - v1.clamp(0.0, 1.0)) * (h - 1.0)).floor() as u32;
    let y_bot = ((1.0 - v0.clamp(0.0, 1.0)) * (h - 1.0)).ceil().min(h - 1.0) as u32;
    let crop = image::imageops::crop_imm(&body_image, x0, y_top.min(y_bot), x1.saturating_sub(x0).max(1), y_bot.saturating_sub(y_top).max(1)).to_image();
    let crotch_mean = crop.pixels().map(|p| p[0] as u64 + p[1] as u64 + p[2] as u64).sum::<u64>() as f64
        / (crop.pixels().len() as f64 * 3.0);
    let dark = crop.pixels().filter(|p| p[0] < 20 && p[1] < 20 && p[2] < 20).count();
    eprintln!("CROTCH verts={crotch} uv=({u0:.3},{v0:.3})-({u1:.3},{v1:.3}) crop={}x{} mean={crotch_mean:.1}",
        crop.width(), crop.height());
    assert!(crotch_mean > 80.0 && crotch_mean < 220.0, "vagina island mean {crotch_mean} is not the uncensor skin bake");
    assert!(dark * 20 < crop.pixels().len(), "vagina island is mostly black");
}

fn extract_tag(xml: &str, tag: &str) -> Option<String> {
    let open = format!("<{tag}>");
    let close = format!("</{tag}>");
    let lower = xml.to_ascii_lowercase();
    let o = lower.find(&open.to_ascii_lowercase())?;
    let start = o + open.len();
    let c = lower[start..].find(&close.to_ascii_lowercase())?;
    Some(xml[start..start + c].trim().to_string())
}

pub fn zipmod_unity3d_bytes(path: &Path) -> Vec<(String, Vec<u8>)> {
    zipmod_one_unity3d(path, "").into_iter().collect()
}

/// Read a single non-thumb .unity3d from a zipmod. Prefer `main_ab` when set.
pub fn zipmod_one_unity3d(path: &Path, main_ab: &str) -> Option<(String, Vec<u8>)> {
    zipmod_one_unity3d_arc(path, main_ab).map(|a| (a.0.clone(), a.1.to_vec()))
}

/// `zipmod_one_unity3d` without a copy of the bundle (a stored entry is mapped).
pub(crate) fn zipmod_one_unity3d_source(path: &Path, main_ab: &str) -> Option<(String, crate::unity::SourceBytes)> {
    zipmod_one_unity3d_arc(path, main_ab).map(|a| (a.0.clone(), a.1.clone()))
}

fn zipmod_one_unity3d_arc(path: &Path, main_ab: &str) -> Option<Arc<(String, crate::unity::SourceBytes)>> {
    let want = main_ab.replace('\\', "/").to_ascii_lowercase();
    let key = (path.to_path_buf(), want.clone());
    let cached = || ZIP_AB_CACHE.lock().unwrap_or_else(|e| e.into_inner()).as_ref().and_then(|c| c.0.get(&key).cloned());
    if let Some(hit) = cached() { return hit; }
    // One read per bundle: the open and its entry prefetch helpers ask for the same bundles at once, and each used to
    // read its own (a card's conversion read 859 MB of zipmod bundles for 38 bundles).
    let loading = ZIP_AB_LOADING.lock().unwrap_or_else(|e| e.into_inner()).entry(key.clone()).or_default().clone();
    let _one = loading.lock().unwrap_or_else(|e| e.into_inner());
    let found = match cached() {
        Some(hit) => hit,
        None => { let found = zipmod_one_unity3d_uncached(path, &want).map(Arc::new); zip_ab_cache_put(key.clone(), found) }
    };
    let mut loads = ZIP_AB_LOADING.lock().unwrap_or_else(|e| e.into_inner());
    if loads.get(&key).is_some_and(|l| Arc::ptr_eq(l, &loading)) && Arc::strong_count(&loading) <= 2 { loads.remove(&key); }
    found
}

/// Stores a zipmod bundle read for `key` unless another thread stored one meanwhile (that one is kept).
fn zip_ab_cache_put(key: (PathBuf, String), found: Option<Arc<(String, crate::unity::SourceBytes)>>) -> Option<Arc<(String, crate::unity::SourceBytes)>> {
    let mut cache = ZIP_AB_CACHE.lock().unwrap_or_else(|e| e.into_inner());
    let (m, owned) = cache.get_or_insert_with(Default::default);
    // Another thread may have read it meanwhile: keep the first.
    if let Some(hit) = m.get(&key) { return hit.clone(); }
    let size = found.as_ref().filter(|f| !f.1.is_mapped()).map_or(0, |f| f.1.len());
    let (owned_limit, entry_limit) = zip_ab_limits();
    // Past a limit, bundles give way until the new one fits (emptying the whole cache had the next open of the same
    // card read all of them again).
    while !m.is_empty() && (m.len() >= entry_limit || *owned + size > owned_limit) {
        let Some(old) = m.keys().next().cloned() else { break };
        if let Some(Some(gone)) = m.remove(&old) { if !gone.1.is_mapped() { *owned -= gone.1.len().min(*owned); } }
    }
    *owned += size;
    m.insert(key, found.clone());
    found
}

fn zipmod_one_unity3d_uncached(path: &Path, want: &str) -> Option<(String, crate::unity::SourceBytes)> {let _spot=crate::profile::spot("zipmod_one_unity3d_uncached");
    let mut zip = crate::gamefs::open_zip(path)?;
    let file_name = want.rsplit('/').next().unwrap_or(want).to_string();
    let mut names = Vec::with_capacity(zip.len());
    for i in 0..zip.len() {
        match zip.by_index(i) {
            Ok(f) => names.push(f.name().replace('\\', "/")),
            Err(_) => names.push(String::new()),
        }
    }
    let mut hit_idx: Option<usize> = None;
    let mut fallback_idx: Option<usize> = None;
    for (i, name) in names.iter().enumerate() {
        let n = name.to_ascii_lowercase();
        if !n.ends_with(".unity3d") || n.contains("/thumb/") {
            continue;
        }
        let hit = !want.is_empty()
            && (n.ends_with(want) || n.rsplit('/').next() == Some(file_name.as_str()));
        if hit {
            hit_idx = Some(i);
            break;
        }
        if fallback_idx.is_none() {
            fallback_idx = Some(i);
        }
    }
    let idx = hit_idx.or(fallback_idx)?;
    zip_entry_source(&mut zip, path, idx).map(|bytes| (names[idx].clone(), bytes))
}

/// A zipmod entry's bytes (more than 16): a large stored entry is mapped from the archive, others are read.
fn zip_entry_source(zip: &mut crate::gamefs::Zip, path: &Path, idx: usize) -> Option<crate::unity::SourceBytes> {
    if let Some((start, len)) = zip.stored_range(idx).filter(|(_, len)| *len >= crate::unity::MAP_AT_LEAST) {
        if let Some(mapped) = crate::unity::SourceBytes::map(path, start, len) { return Some(mapped); }
    }
    // Read whole into a buffer of its size: a recording logs it, and an entry the library holds is handed over without
    // a copy (grown by doubling, and copied once more into a buffer of its own, a large entry was allocated anew
    // several times over: C030 ~0.8 GB of fresh pages a card).
    let mut f = zip.by_index(idx).ok()?;
    let mut bytes = Vec::with_capacity(f.size() as usize);
    if f.read_to_end(&mut bytes).is_err() || bytes.len() <= 16 {
        return None;
    }
    Some(bytes.into())
}

pub fn zipmod_list_files(path: &Path) -> Vec<(String, Vec<u8>)> {
    zipmod_files(path, |name| {
        let n = name.to_ascii_lowercase();
        (n.ends_with(".unity3d") && n.contains("list") && n.contains("characustom"))
            || (n.ends_with(".csv") && (n.contains("list") || n.contains("characustom")))
    })
}

#[allow(dead_code)]
/// Sideloader `GetPng`: a loose png inside any zipmod, matched by path suffix
/// (`abdata/sideloader/mt_eyeline_up_00/cw_t_eyeline_up_055.png`).
pub fn zipmod_file_suffix(path: &Path, suffix: &str) -> Option<Vec<u8>> {let _spot=crate::profile::spot("zipmod_file_suffix");
    let want = suffix.replace('\\', "/").trim_start_matches('/').to_ascii_lowercase();
    if want.is_empty() {
        return None;
    }
    // Manifests are asked of every zipmod (MaterialEditor shader registrations): read each archive's once.
    if want == "manifest.xml" {
        return crate::gamefs::zip_memo("suffix_manifest", path, || zipmod_file_suffix_uncached(path, &want));
    }
    zipmod_file_suffix_uncached(path, &want)
}
fn zipmod_file_suffix_uncached(path: &Path, want: &str) -> Option<Vec<u8>> {
    let mut zip = crate::gamefs::open_zip(path)?;
    for i in 0..zip.len() {
        let Ok(mut f) = zip.by_index(i) else { continue };
        let name = f.name().replace('\\', "/").to_ascii_lowercase();
        if name.ends_with(&want) {
            let mut bytes = Vec::new();
            if f.read_to_end(&mut bytes).is_ok() && bytes.len() > 16 {
                return Some(bytes);
            }
        }
    }
    None
}

/// The zipmod's bundle named `main_ab`, shared with every other reader of it (a stored entry is mapped).
pub fn zipmod_bundle_named(path: &Path, main_ab: &str) -> Option<crate::unity::SourceBytes> {
    if main_ab.is_empty() {
        return None;
    }
    let want = main_ab.replace('\\', "/").to_ascii_lowercase();
    let file_name = want.rsplit('/').next().unwrap_or(&want).to_string();
    let key = (path.to_path_buf(), format!("named\n{want}"));
    if let Some(hit) = ZIP_AB_CACHE.lock().unwrap_or_else(|e| e.into_inner()).as_ref().and_then(|c| c.0.get(&key).cloned()) {
        return hit.map(|h| h.1.clone());
    }
    let found = collect_zipmod_bundle(path, &want, &file_name).map(|b| Arc::new((String::new(), b)));
    zip_ab_cache_put(key, found).map(|h| h.1.clone())
}

fn collect_zipmod_bundle(path: &Path, want: &str, file_name: &str) -> Option<crate::unity::SourceBytes> {let _spot=crate::profile::spot("collect_zipmod_bundle");
    let mut zip = crate::gamefs::open_zip(path)?;
    for i in 0..zip.len() {
        let Ok(mut f) = zip.by_index(i) else { continue };
        let name = f.name().replace('\\', "/");
        let n = name.to_ascii_lowercase();
        if !n.ends_with(".unity3d") || n.contains("/thumb/") {
            continue;
        }
        if n.ends_with(want) || n.rsplit('/').next() == Some(file_name) {
            if f.size() > 16 {
                drop(f);
                match zip_entry_source(&mut zip, path, i) { Some(bytes) => return Some(bytes), None => continue }
            }
            let mut bytes = Vec::new();
            if f.read_to_end(&mut bytes).is_ok() && bytes.len() > 16 {
                return Some(bytes.into());
            }
        }
    }
    None
}

fn zipmod_files(path: &Path, keep: impl Fn(&str) -> bool) -> Vec<(String, Vec<u8>)> {let _spot=crate::profile::spot("zipmod_files");
    let mut out = Vec::new();
    let Some(mut zip) = crate::gamefs::open_zip(path) else {
        return out;
    };
    for i in 0..zip.len() {
        let Ok(mut f) = zip.by_index(i) else {
            continue;
        };
        let name = f.name().replace('\\', "/");
        if !keep(&name) {
            continue;
        }
        let mut bytes = Vec::new();
        if f.read_to_end(&mut bytes).is_ok() && bytes.len() > 16 {
            out.push((name, bytes));
        }
    }
    out
}
