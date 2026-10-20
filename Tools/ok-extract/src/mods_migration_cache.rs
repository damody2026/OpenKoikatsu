//! Immutable migration data shared across short-lived native request workers.
use crate::appearance::catalog_watch::Watch;
use std::{collections::{HashSet,VecDeque},path::{Path,PathBuf},sync::{Arc,Mutex,OnceLock}};

const CAPACITY:usize=4;
struct Entry {root:PathBuf,watch:Watch,value:Arc<HashSet<String>>}
#[derive(Default)]
struct Cache {entries:VecDeque<Entry>}

fn cached(cache:&Mutex<Cache>,root:&Path,load:impl FnOnce(&Path)->(HashSet<String>,bool))->Arc<HashSet<String>> {
    let Ok(root)=root.canonicalize() else{return Arc::new(load(root).0)};
    {
        let Ok(mut state)=cache.lock() else{return Arc::new(load(&root).0)};
        if let Some(index)=state.entries.iter().position(|e|e.root==root){
            let mut entry=state.entries.remove(index).unwrap();
            if entry.watch.available()&&!entry.watch.changed(){
                let value=entry.value.clone();state.entries.push_front(entry);return value;
            }
        }
    }
    // Never hold the shared lock while traversing or opening source packages.
    let mut watch=Watch::for_directory(&root);
    let (value,complete)=load(&root);let value=Arc::new(value);
    if complete&&watch.available()&&!watch.changed(){
        if let Ok(mut state)=cache.lock(){
            // Poll again after acquiring the publication lock. A concurrent
            // reader cannot publish a superseded generation over a change.
            if !watch.changed(){
                state.entries.retain(|e|e.root!=root);
                state.entries.push_front(Entry{root,watch,value:value.clone()});
                while state.entries.len()>CAPACITY{state.entries.pop_back();}
            }
        }
    }
    value
}

pub(super) fn get(root:&Path,load:impl FnOnce(&Path)->(HashSet<String>,bool))->Arc<HashSet<String>> {
    static CACHE:OnceLock<Mutex<Cache>>=OnceLock::new();
    cached(CACHE.get_or_init(Default::default),root,load)
}

#[cfg(all(test,windows))]
mod tests {
    use super::*;
    use std::{cell::Cell,fs,time::{SystemTime,UNIX_EPOCH}};
    fn fixture()->PathBuf{
        let root=std::env::temp_dir().join(format!("ok-migrations-shared-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        fs::create_dir_all(&root).unwrap();root
    }
    fn read(root:&Path)->(HashSet<String>,bool){
        (crate::gamefs::read_to_string(root.join("manifest")).ok().into_iter().collect(),true)
    }
    #[test]
    fn bounded_roots_and_unavailable_sources(){
        let cache=Mutex::new(Cache::default());let roots:Vec<_>=(0..CAPACITY+1).map(|_|fixture()).collect();
        for root in &roots{cached(&cache,root,read);}
        assert_eq!(cache.lock().unwrap().entries.len(),CAPACITY);
        assert!(!cache.lock().unwrap().entries.iter().any(|e|e.root==roots[0].canonicalize().unwrap()));
        let calls=Cell::new(0);let missing=roots[0].join("missing");
        for _ in 0..2{cached(&cache,&missing,|p|{calls.set(calls.get()+1);read(p)});}
        assert_eq!(calls.get(),2);
    }
    #[test]
    fn partial_reads_never_publish_and_recovery_can_cache(){
        let root=fixture();let cache=Mutex::new(Cache::default());let calls=Cell::new(0);
        for _ in 0..2{cached(&cache,&root,|p|{calls.set(calls.get()+1);(read(p).0,false)});}
        assert_eq!(calls.get(),2);assert!(cache.lock().unwrap().entries.is_empty());
        let valid=cached(&cache,&root,read);
        assert!(Arc::ptr_eq(&valid,&cached(&cache,&root,|_|panic!("complete unchanged generation must reuse"))));
    }
    #[test]
    fn change_during_load_cannot_publish(){
        let root=fixture();fs::write(root.join("manifest"),"old").unwrap();let cache=Mutex::new(Cache::default());
        let before=cached(&cache,&root,|p|{
            let value=read(p);fs::write(p.join("manifest"),"new").unwrap();value
        });
        assert!(before.contains("old"));assert!(cache.lock().unwrap().entries.is_empty());
        let after=cached(&cache,&root,read);assert!(after.contains("new"));
    }
    #[test]
    fn poisoned_cache_preserves_direct_reads(){
        let root=fixture();fs::write(root.join("manifest"),"source").unwrap();let cache=Arc::new(Mutex::new(Cache::default()));
        let other=cache.clone();assert!(std::thread::spawn(move||{let _lock=other.lock().unwrap();panic!("injected cache failure");}).join().is_err());
        assert!(cached(&cache,&root,read).contains("source"));
    }
}
