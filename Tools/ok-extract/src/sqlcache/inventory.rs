//! Watch-validated immutable ZIP inventory; preserve the caller's path spelling.
use super::{sig_of,stamp_zips_checked};
use crate::appearance::catalog_watch::Watch;
use std::{collections::VecDeque,ffi::OsString,path::{Path,PathBuf},sync::{Arc,Mutex,OnceLock}};

const CAPACITY:usize=4;
pub(super) struct Snapshot {pub stamps:Vec<(PathBuf,i64,i64)>,pub sig:String}
#[derive(PartialEq)]
struct Key {requested:OsString,canonical:PathBuf}
struct Entry {key:Key,watch:Watch,value:Arc<Snapshot>}
#[derive(Default)]
struct Cache {entries:VecDeque<Entry>}
fn load(root:&Path)->(Snapshot,bool){
    let (stamps,complete)=stamp_zips_checked(root);let sig=sig_of(&stamps);
    (Snapshot{stamps,sig},complete)
}
fn cached(cache:&Mutex<Cache>,root:&Path,read:impl FnOnce(&Path)->(Snapshot,bool))->Arc<Snapshot>{
    let Ok(canonical)=root.canonicalize() else{return Arc::new(read(root).0)};
    // Path equality can normalize `.` and separators. The old signature hashes
    // entry.path() spelling, so retain the exact OsString independently.
    let key=Key{requested:root.as_os_str().to_os_string(),canonical};
    {
        let Ok(mut state)=cache.lock() else{return Arc::new(read(root).0)};
        if let Some(index)=state.entries.iter().position(|e|e.key==key){
            let mut entry=state.entries.remove(index).unwrap();
            if entry.watch.available()&&!entry.watch.changed(){
                let value=entry.value.clone();state.entries.push_front(entry);return value;
            }
        }
    }
    let mut watch=Watch::for_directory(&key.canonical);
    let (value,complete)=read(root);let value=Arc::new(value);
    if complete&&watch.available()&&!watch.changed(){
        if let Ok(mut state)=cache.lock(){if !watch.changed(){
            state.entries.retain(|e|e.key!=key);
            state.entries.push_front(Entry{key,watch,value:value.clone()});
            while state.entries.len()>CAPACITY{state.entries.pop_back();}
        }}
    }
    value
}
pub(super) fn get(root:&Path)->Arc<Snapshot>{
    static CACHE:OnceLock<Mutex<Cache>>=OnceLock::new();
    cached(CACHE.get_or_init(Default::default),root,load)
}

#[cfg(all(test,windows))]
mod tests {
    use super::*;
    use std::{cell::Cell,fs,time::{SystemTime,UNIX_EPOCH}};
    fn fixture()->PathBuf{
        let root=std::env::temp_dir().join(format!("ok-inventory-shared-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        fs::create_dir_all(&root).unwrap();root
    }
    #[test]
    fn shared_snapshot_tracks_add_rewrite_rename_remove_and_old_lifetime(){
        let root=fixture();let a=root.join("a.zipmod");fs::write(&a,b"first").unwrap();
        let first=get(&root);let other=root.clone();
        let worker=std::thread::spawn(move||get(&other)).join().unwrap();assert!(Arc::ptr_eq(&first,&worker));
        fs::write(&a,b"second-longer").unwrap();let edited=get(&root);assert_ne!(first.sig,edited.sig);assert_eq!(first.stamps[0].1,5);
        let nested=root.join("nested");fs::create_dir(&nested).unwrap();fs::write(nested.join("b.ZIPMOD"),b"b").unwrap();assert_eq!(get(&root).stamps.len(),2);
        fs::rename(&a,root.join("c.zipmod")).unwrap();let renamed=get(&root);assert_ne!(edited.sig,renamed.sig);
        fs::remove_file(root.join("c.zipmod")).unwrap();let removed=get(&root);assert_eq!(removed.stamps.len(),1);
        assert_eq!(removed.sig,sig_of(&super::super::stamp_zips(&root)));
        assert_eq!(first.stamps.len(),1);assert_eq!(first.stamps[0].0,a);
    }
    #[test]
    fn aliases_keep_exact_original_signature_spelling(){
        let root=fixture();fs::create_dir(root.join("inner")).unwrap();fs::write(root.join("a.zipmod"),b"a").unwrap();
        let alias=root.join("inner").join("..");assert_eq!(root.canonicalize().unwrap(),alias.canonicalize().unwrap());
        let direct=get(&root);let through=get(&alias);
        assert_eq!(direct.sig,sig_of(&super::super::stamp_zips(&root)));
        assert_eq!(through.sig,sig_of(&super::super::stamp_zips(&alias)));
        assert_ne!(direct.sig,through.sig);assert!(!Arc::ptr_eq(&direct,&through));
        assert!(Arc::ptr_eq(&direct,&get(&root)));assert!(Arc::ptr_eq(&through,&get(&alias)));
    }
    #[test]
    fn capacity_partial_change_during_load_missing_and_poison_fall_back(){
        let cache=Mutex::new(Cache::default());let roots:Vec<_>=(0..CAPACITY+1).map(|_|fixture()).collect();
        for root in &roots{cached(&cache,root,load);}assert_eq!(cache.lock().unwrap().entries.len(),CAPACITY);
        assert!(!cache.lock().unwrap().entries.iter().any(|e|e.key.canonical==roots[0].canonicalize().unwrap()));
        let partial=Mutex::new(Cache::default());let calls=Cell::new(0);
        for _ in 0..2{cached(&partial,&roots[0],|p|{calls.set(calls.get()+1);(load(p).0,false)});}assert_eq!(calls.get(),2);assert!(partial.lock().unwrap().entries.is_empty());
        cached(&partial,&roots[0],|p|{let result=load(p);fs::write(p.join("new.zipmod"),b"new").unwrap();result});assert!(partial.lock().unwrap().entries.is_empty());
        assert_eq!(cached(&partial,&roots[0],load).stamps.len(),1);
        let missing=roots[0].join("missing");assert!(cached(&partial,&missing,load).stamps.is_empty());fs::create_dir(&missing).unwrap();fs::write(missing.join("a.zipmod"),b"a").unwrap();assert_eq!(cached(&partial,&missing,load).stamps.len(),1);
        let poisoned=Arc::new(Mutex::new(Cache::default()));let other=poisoned.clone();assert!(std::thread::spawn(move||{let _lock=other.lock().unwrap();panic!("injected inventory cache failure");}).join().is_err());
        assert_eq!(cached(&poisoned,&roots[0],load).stamps,load(&roots[0]).0.stamps);
    }
    #[test]
    fn linked_external_sources_are_read_without_publishing_root_only_watch(){
        use std::os::windows::process::CommandExt;
        let base=fixture();let root=base.join("mods");let external=base.join("external");fs::create_dir(&root).unwrap();fs::create_dir(&external).unwrap();
        let result=std::process::Command::new("cmd.exe").args(["/D","/C","mklink","/J"]).arg(root.join("linked")).arg(&external).creation_flags(0x08000000).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
        fs::write(external.join("a.zipmod"),b"a").unwrap();let (reference,complete)=stamp_zips_checked(&root);assert!(!complete);assert_eq!(reference.len(),1);
        let before=get(&root);assert_eq!(before.stamps,reference);assert!(!Arc::ptr_eq(&before,&get(&root)));
        fs::write(external.join("b.zipmod"),b"b").unwrap();let after=get(&root);assert_eq!(after.stamps.len(),2);assert_eq!(before.stamps.len(),1);
        assert_eq!(after.sig,sig_of(&super::super::stamp_zips(&root)));
    }
}
