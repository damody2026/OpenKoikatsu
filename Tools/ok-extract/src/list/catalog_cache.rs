//! Bounded immutable source catalogs. Change notifications, never a TTL,
//! decide whether parsed Unity list assets may be reused.
use super::ChaListCatalog;
use crate::appearance::catalog_watch::Watch;
use std::{collections::VecDeque,path::{Path,PathBuf},sync::Arc};

const CAPACITY:usize=4;
struct Entry {directory:PathBuf,watch:Watch,catalog:Arc<ChaListCatalog>}
#[derive(Default)]
pub(super) struct Cache {entries:VecDeque<Entry>}
impl Cache {
    pub(super) fn get(&mut self,directory:&Path,load:impl FnOnce(&Path)->(ChaListCatalog,bool))->Arc<ChaListCatalog>{
        let Ok(canonical)=directory.canonicalize() else{return Arc::new(load(directory).0)};
        if let Some(index)=self.entries.iter().position(|e|e.directory==canonical){
            let mut entry=self.entries.remove(index).unwrap();
            if entry.watch.available()&&!entry.watch.changed(){
                let result=entry.catalog.clone();self.entries.push_front(entry);return result;
            }
        }
        // Arm before reading. A change during parsing must prevent publication
        // of this generation. The caller still receives the fresh read, just
        // as before caching; its next request is guaranteed to parse again.
        let mut watch=Watch::for_directory(&canonical);
        let (catalog,complete)=load(&canonical);let catalog=Arc::new(catalog);
        if complete&&watch.available()&&!watch.changed(){
            self.entries.push_front(Entry{directory:canonical,watch,catalog:catalog.clone()});
            while self.entries.len()>CAPACITY{self.entries.pop_back();}
        }
        catalog
    }
}

#[cfg(all(test,windows))]
mod tests {
    use super::*;
    use std::{cell::Cell,time::{Duration,Instant,SystemTime,UNIX_EPOCH}};
    fn fixture()->PathBuf{
        let path=std::env::temp_dir().join(format!("ok-source-catalog-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        std::fs::create_dir_all(&path).unwrap();path
    }
    fn read(directory:&Path,calls:&Cell<usize>)->(ChaListCatalog,bool){
        calls.set(calls.get()+1);let mut result=ChaListCatalog::default();
        if let Ok(name)=crate::gamefs::read_to_string(directory.join("item.unity3d")){
            result.by_id.insert((121,1),super::super::ListEntry{name,..Default::default()});
        }(result,true)
    }
    #[test]
    fn shared_catalog_reuses_and_invalidates_same_size_writes_and_removal(){
        let root=fixture();let file=root.join("item.unity3d");std::fs::write(&file,"old").unwrap();
        let calls=Cell::new(0);let mut cache=Cache::default();
        let old=cache.get(&root,|p|read(p,&calls));let same=cache.get(&root,|p|read(p,&calls));
        assert!(Arc::ptr_eq(&old,&same));assert_eq!(calls.get(),1);
        std::fs::write(&file,"new").unwrap();
        let started=Instant::now();let new=loop{
            let result=cache.get(&root,|p|read(p,&calls));
            if result.get(121,1).unwrap().name=="new"{break result}
            assert!(started.elapsed()<Duration::from_secs(1));std::thread::sleep(Duration::from_millis(1));
        };
        assert!(!Arc::ptr_eq(&old,&new));assert_eq!(old.get(121,1).unwrap().name,"old");
        std::fs::rename(&file,root.join("renamed.unity3d")).unwrap();
        let started=Instant::now();loop{
            if cache.get(&root,|p|read(p,&calls)).by_id.is_empty(){break}
            assert!(started.elapsed()<Duration::from_secs(1));std::thread::sleep(Duration::from_millis(1));
        }
    }
    #[test]
    fn shared_catalog_has_bounded_roots_and_never_caches_unavailable_directories(){
        let mut cache=Cache::default();let calls=Cell::new(0);
        let roots:Vec<_>=(0..CAPACITY+1).map(|_|fixture()).collect();
        for root in &roots{cache.get(root,|p|read(p,&calls));}
        assert_eq!(cache.entries.len(),CAPACITY);
        assert!(!cache.entries.iter().any(|e|e.directory==roots[0].canonicalize().unwrap()));
        let count=calls.get();cache.get(&roots[0],|p|read(p,&calls));assert_eq!(calls.get(),count+1);
        let missing=roots[0].join("missing");let count=calls.get();
        cache.get(&missing,|p|read(p,&calls));cache.get(&missing,|p|read(p,&calls));assert_eq!(calls.get(),count+2);
        assert_eq!(cache.entries.len(),CAPACITY);
    }
    #[test]
    fn shared_catalog_does_not_publish_a_generation_changed_during_load(){
        let root=fixture();std::fs::write(root.join("item.unity3d"),"old").unwrap();
        let calls=Cell::new(0);let mut cache=Cache::default();
        let before=cache.get(&root,|p|{
            let value=read(p,&calls);std::fs::write(p.join("item.unity3d"),"new").unwrap();
            // Windows delivers notifications asynchronously; wait for the
            // kernel change event before returning our injected slow loader.
            std::thread::sleep(Duration::from_millis(25));value
        });
        assert_eq!(before.get(121,1).unwrap().name,"old");assert!(cache.entries.is_empty());
        let after=cache.get(&root,|p|read(p,&calls));assert_eq!(after.get(121,1).unwrap().name,"new");assert_eq!(calls.get(),2);
    }
    #[test]
    fn shared_catalog_never_publishes_partial_reads(){
        let root=fixture();let mut cache=Cache::default();let calls=Cell::new(0);
        let partial=cache.get(&root,|p|{let mut result=read(p,&calls);result.1=false;result});
        assert!(cache.entries.is_empty());
        let full=cache.get(&root,|p|read(p,&calls));assert!(!Arc::ptr_eq(&partial,&full));assert_eq!(calls.get(),2);
        let malformed=fixture();std::fs::write(malformed.join("broken.unity3d"),[1,2,3]).unwrap();
        let a=super::super::load_characustom_dir_shared(&malformed);
        let b=super::super::load_characustom_dir_shared(&malformed);
        assert!(a.by_id.is_empty()&&b.by_id.is_empty());assert!(!Arc::ptr_eq(&a,&b));
    }
    #[test]
    #[ignore="installed original Unity list assets"]
    fn shared_catalog_actual_unity_lists_preserve_exact_entries(){
        let root=&crate::test_support::game_path("/abdata/list/characustom");
        let(raw,complete)=super::super::load_characustom_dir_uncached(root);
        assert!(complete,"installed list assets must parse fully");assert!(!raw.by_id.is_empty());
        let a=super::super::load_characustom_dir_shared(root);
        let start=Instant::now();let b=super::super::load_characustom_dir_shared(root);let hit_ms=start.elapsed().as_secs_f64()*1000.;
        assert!(Arc::ptr_eq(&a,&b));
        let exact=|c:&ChaListCatalog|c.by_id.iter().map(|(k,v)|(*k,serde_json::to_value(v).unwrap())).collect::<std::collections::BTreeMap<_,_>>();
        assert_eq!(exact(&raw),exact(&a));
        eprintln!("OK shared source catalog entries={} warm_hit_ms={hit_ms:.3}",a.by_id.len());
    }
}
