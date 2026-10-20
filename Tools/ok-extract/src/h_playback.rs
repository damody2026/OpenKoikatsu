//! Native AnimationInfoData pairing, without basename or card-name guesses.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::path::Path;
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;
#[cfg(test)]
#[path="paired_height_audit.rs"]
mod paired_height_audit;

fn field<'a>(value:&'a UnityValue,key:&str)->Option<&'a UnityValue> {
    match value {UnityValue::Object(o)=>o.get(key),_=>None}
}
fn text(value:Option<&UnityValue>)->String {value.and_then(|v|v.as_str()).unwrap_or("").into()}

#[derive(Clone,serde::Serialize,serde::Deserialize)]
pub(crate) struct Pair {
    pub id:i64,pub category:String,pub name:String,
    pub participants:i32,
    pub female_layer:i32,pub male_layer:i32,
    pub female_source:String,pub male_source:String,
    pub female_motion:String,pub male_motion:String,
}
fn role(row:&UnityValue,base:&str,param:&str)->Option<(String,String)> {
    let base=field(row,base)?;let param=field(field(row,param)?,"path")?;
    let bundle=text(field(base,"assetpath"));let owner=text(field(base,"file"));
    if bundle.is_empty()||owner.is_empty() {return None;}
    let overlay=text(field(param,"assetpath"));let replacement=text(field(param,"file"));
    // The existing controller source resolver replays the exact base and
    // AnimatorOverrideController, including cross-bundle identities.
    let payload=serde_json::to_vec(&[bundle,owner.clone(),overlay,replacement.clone()]).ok()?;
    let hex:String=payload.iter().map(|v|format!("{v:02X}")).collect();
    Some((format!("states:controller@{hex}"),if replacement.is_empty(){owner}else{replacement}))
}
fn active_layer(row:&UnityValue,param:&str)->i32{field(field(row,param).unwrap_or(row),"lstIdLayer").and_then(|v|v.as_array()).and_then(|v|v.first()).and_then(|v|v.as_i64()).unwrap_or(-1)as i32}
pub(crate) fn pairs(game:&Path)->Vec<Pair> {
    authored_poses(game,false)
}
pub(crate) fn interaction_poses(game:&Path)->Vec<Pair>{authored_poses(game,true)}
fn authored_poses(game:&Path,include_solo:bool)->Vec<Pair> {
    use std::sync::{Mutex,OnceLock};
    type Stamp=Vec<(std::path::PathBuf,u64,std::time::SystemTime,Option<std::time::SystemTime>)>;
    struct Catalogue {stamp:Stamp,rows:Vec<Pair>}
    static CACHE:OnceLock<Mutex<std::collections::HashMap<std::path::PathBuf,Catalogue>>>=OnceLock::new();
    // An OK base library root answers from its converted catalogue (no h/list bundles).
    if crate::anim::disk_cache::is_library(game) {
        let mut rows=crate::anim::disk_cache::load::<Vec<Pair>>(game,"pairs","h/list").unwrap_or_default();
        if !include_solo{rows.retain(|row|row.participants!=1);}
        return rows;
    }
    let Ok(paths)=catalogue_paths(game) else{return Vec::new();};
    let stamp=|paths:&[std::path::PathBuf]|->Option<Stamp>{paths.iter().map(|p|{
        let m=crate::gamefs::metadata(p).ok()?;Some((p.clone(),m.len(),m.modified().ok()?,m.created().ok()))
    }).collect()};
    let signature=stamp(&paths);let cache=CACHE.get_or_init(Default::default);
    let select=|mut rows:Vec<Pair>|{if !include_solo{rows.retain(|row|row.participants!=1);}rows};
    if let Some(signature)=signature.as_ref(){
        if let Ok(entries)=cache.lock(){if let Some(hit)=entries.get(game).filter(|c|&c.stamp==signature){return select(hit.rows.clone());}}
    }
    // Cache only metadata, never sessions, controllers or animation clocks.
    // Do not hold a global lock while reading/decompressing large bundles.
    let (rows,complete)=read_authored_poses(&paths);
    if complete {crate::anim::disk_cache::store(game,"pairs","h/list",&rows);}
    if complete {if let Some(signature)=signature {
        // Recheck the inventory too: a file may have been added during parsing.
        if catalogue_paths(game).ok().and_then(|p|stamp(&p)).as_ref()==Some(&signature){
            if let Ok(mut entries)=cache.lock(){
                if !entries.contains_key(game)&&entries.len()>=8{entries.clear();}
                entries.insert(game.to_path_buf(),Catalogue{stamp:signature,rows:rows.clone()});
            }
        }
    }}
    select(rows)
}
fn catalogue_paths(game:&Path)->std::io::Result<Vec<std::path::PathBuf>> {
    let mut paths=Vec::new();
    for entry in crate::gamefs::read_dir(game.join("abdata/h/list"))?{
        let path=entry?.path();if path.extension().is_some_and(|e|e.eq_ignore_ascii_case("unity3d")){paths.push(path);}
    }
    paths.sort();Ok(paths)
}
fn read_authored_poses(paths:&[std::path::PathBuf])->(Vec<Pair>,bool) {
    let mut records=std::collections::BTreeMap::new();let mut complete=true;
    for path in paths {
        let Ok(bytes)=crate::gamefs::read(path) else {complete=false;continue;};
        let parsed=crate::unity::parse_bundle_cached(bytes);
        parsed.with_serialized(|files|for file in files {for info in file.objects_of_type(114) {
            let Ok(obj)=UnityObject::from_serialized_file(file,&info) else {continue;};
            let Some(name)=obj.name().filter(|n|n.starts_with("AnimationInfo_")) else {continue;};
            let Some(rows)=obj.class.properties().get("param").and_then(|v|v.as_array()) else {continue;};
            for row in rows {
                let Some((female_source,female_motion))=role(row,"pathFemaleBase","paramFemale") else {continue;};
                let Some(id)=field(row,"id").and_then(|v|v.as_i64()) else {continue;};
                let participants=if name=="AnimationInfo_00"{1}else if name=="AnimationInfo_01"||name=="AnimationInfo_02" {2}else{3};
                let (male_source,male_motion)=if participants==1{(String::new(),String::new())}else{let Some(role)=role(row,"pathMaleBase","paramMale")else{continue;};role};
                records.insert((name.to_string(),id),Pair{id,category:name.clone(),name:text(field(row,"nameAnimation")),participants,female_layer:active_layer(row,"paramFemale"),male_layer:active_layer(row,"paramMale"),female_source,male_source,female_motion,male_motion});
            }
        }});
    }
    (records.into_values().collect(),complete)
}

#[cfg(test)]
mod catalogue_cache_tests {
    use super::*;
    struct Fixture(std::path::PathBuf);
    impl Fixture {
        fn new()->Self {
            let unique=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let root=std::env::temp_dir().join(format!("ok-pose-catalogue-{}-{unique}",std::process::id()));
            std::fs::create_dir(&root).unwrap();std::fs::create_dir_all(root.join("abdata/h/list")).unwrap();Self(root)
        }
        fn file(&self)->std::path::PathBuf{self.0.join("abdata/h/list/fixture.unity3d")}
    }
    impl Drop for Fixture {fn drop(&mut self){
        let _=std::fs::remove_file(self.file());
        for relative in ["abdata/h/list","abdata/h","abdata",""]{let _=std::fs::remove_dir(self.0.join(relative));}
    }}
    #[test]
    #[ignore="installed authored AnimationInfo bundles; metadata cache parity and file invalidation"]
    fn authored_catalogue_cache_tracks_source_files(){
        let root=std::env::var_os("OK_TEST_WARM_ROOT").map(std::path::PathBuf::from).unwrap_or_else(crate::test_support::game);
        let sources=catalogue_paths(&root).unwrap();
        let fixtures:Vec<_>=sources.iter().map(|p|{let(rows,complete)=read_authored_poses(std::slice::from_ref(p));assert!(complete);(p,serde_json::to_string(&rows).unwrap(),rows)}).collect();
        let primary=fixtures.iter().find(|(_,_,rows)|!rows.is_empty()).expect("Authored metadata fixture");
        let alternative=fixtures.iter().find(|(_,json,_)|json!=&primary.1).expect("Distinct valid source bundle");
        let fixture=Fixture::new();let other=Fixture::new();
        assert!(authored_poses(&fixture.0,true).is_empty());
        std::fs::copy(primary.0,fixture.file()).unwrap();
        let start=std::time::Instant::now();let cold=authored_poses(&fixture.0,true);let cold_time=start.elapsed();
        assert_eq!(serde_json::to_string(&cold).unwrap(),primary.1,"Added bundle invalidates cached empty catalogue");
        let start=std::time::Instant::now();let hot=authored_poses(&fixture.0,true);let hot_time=start.elapsed();
        assert_eq!(serde_json::to_string(&hot).unwrap(),primary.1,"Cache hit preserves every source field and ordering");
        let paired:Vec<_>=primary.2.iter().filter(|r|r.participants!=1).cloned().collect();
        assert_eq!(serde_json::to_string(&authored_poses(&fixture.0,false)).unwrap(),serde_json::to_string(&paired).unwrap(),"Solo and pair selectors cannot contaminate each other");
        assert!(authored_poses(&other.0,true).is_empty(),"Roots remain isolated");
        std::thread::scope(|scope|{
            for _ in 0..4 {scope.spawn(||assert_eq!(serde_json::to_string(&authored_poses(&fixture.0,true)).unwrap(),primary.1));}
        });
        std::fs::copy(alternative.0,fixture.file()).unwrap();
        assert_eq!(serde_json::to_string(&authored_poses(&fixture.0,true)).unwrap(),alternative.1,"Replacing a bundle invalidates metadata");
        std::fs::remove_file(fixture.file()).unwrap();assert!(authored_poses(&fixture.0,true).is_empty(),"Removing a bundle invalidates metadata");
        std::fs::copy(primary.0,fixture.file()).unwrap();assert_eq!(serde_json::to_string(&authored_poses(&fixture.0,true)).unwrap(),primary.1,"Re-added source is available again");
        eprintln!("authored catalogue exact rows={} cold_ms={:.3} hot_ms={:.3} add=1 replace=1 remove=1 roots=1 concurrent=1",cold.len(),cold_time.as_secs_f64()*1000.,hot_time.as_secs_f64()*1000.);
    }
}

// Validate all counts and reads before entering the legacy native-layout parser.
fn checked_motion(bytes:&[u8])->Result<crate::finalik::MotionIkData,String> {
    struct Reader<'a>{b:&'a [u8],p:usize}
    impl Reader<'_>{
        fn int(&mut self)->Result<i32,String>{let end=self.p.checked_add(4).ok_or("MotionIK overflow")?;let v=self.b.get(self.p..end).ok_or("truncated MotionIK")?;self.p=end;Ok(i32::from_le_bytes(v.try_into().unwrap()))}
        fn count(&mut self,max:i32)->Result<i32,String>{let n=self.int()?;if n<0||n>max {Err("invalid MotionIK count".into())}else{Ok(n)}}
        fn string(&mut self)->Result<(),String>{let mut n=0usize;let mut done=false;for shift in (0..35).step_by(7){let v=*self.b.get(self.p).ok_or("truncated MotionIK string")?;self.p+=1;n|=((v&127)as usize)<<shift;if v&128==0{done=true;break;}}if !done||n>65536{return Err("invalid MotionIK string length".into());}let end=self.p.checked_add(n).ok_or("MotionIK overflow")?;std::str::from_utf8(self.b.get(self.p..end).ok_or("truncated MotionIK string")?).map_err(|_|"invalid MotionIK UTF-8")?;self.p=end;Ok(())}
        fn floats(&mut self,n:usize)->Result<(),String>{for _ in 0..n{if !f32::from_bits(self.int()?as u32).is_finite(){return Err("non-finite MotionIK value".into());}}Ok(())}
    }
    if bytes.len()>2_000_000{return Err("oversized MotionIK".into());}
    let mut r=Reader{b:bytes,p:0};for _ in 0..r.count(1024)?{r.string()?;for _ in 0..4{r.int()?;r.string()?;r.floats(2)?;r.string()?;r.floats(1)?;}for _ in 0..r.count(256)?{r.int()?;r.int()?;for _ in 0..r.count(256)?{r.int()?;r.floats(12)?;}}}
    if r.p!=bytes.len(){return Err("trailing MotionIK data".into());}
    Ok(crate::finalik::read_motionik_bytes(bytes))
}
type Setup=(crate::finalik::PrefabIkConfig,crate::finalik::MotionIkData);
type SharedSetup=std::sync::Arc<Setup>;
pub(crate) struct PairIkRuntime {
    solver:crate::finalik::IKSolverFullBodyBiped,
}

type FactoryKey=(std::path::PathBuf,[u8;32]);
/// Revision of an installed shared root (no mods/): its manifest.json. Checked every frame, so by file metadata (size,
/// modification time) straight from the file system: inside the OK base library (`base_v1/paired`) reading the file
/// through the gamefs replay layer cost ~15 ms per frame. A root that exists only in a replayed index keeps the content hash.
fn manifest_stamp(game:&Path)->Option<[u8;32]>{
    use sha2::{Digest,Sha256};
    if game.join("mods").ok_exists(){return None;}
    let manifest=game.join("manifest.json");
    if let Ok(meta)=std::fs::metadata(&manifest){
        let mut hash=Sha256::new();hash.update(meta.len().to_le_bytes());hash.update(format!("{:?}",meta.modified().ok()).as_bytes());
        return Some(hash.finalize().into());
    }
    crate::gamefs::read(&manifest).ok().map(|bytes|Sha256::digest(&bytes).into())
}
fn factory_key(game:&Path,bytes:&[u8])->FactoryKey {
    use sha2::{Digest,Sha256};(game.to_path_buf(),Sha256::digest(bytes).into())
}
fn factory_prefab(game:&Path)->Result<std::sync::Arc<crate::pose::Skeleton>,String> {
    use std::sync::{Arc,Mutex,OnceLock};use std::collections::HashMap;
    static CACHE:OnceLock<Mutex<HashMap<FactoryKey,Arc<crate::pose::Skeleton>>>>=OnceLock::new();
    static COMMON:OnceLock<Mutex<HashMap<FactoryKey,Arc<crate::pose::Skeleton>>>>=OnceLock::new();
    // Installed common packs are immutable until their SHA manifest changes.
    // Game roots retain full override/content validation below.
    let common_key=manifest_stamp(game).map(|stamp|(game.to_path_buf(),stamp));
    if let Some(key)=&common_key {if let Some(body)=COMMON.get_or_init(Default::default).lock().map_err(|_|"common prefab cache poisoned")?.get(key).cloned(){return Ok(body);}}
    // An OK base library root carries the converted skeleton (no studio/base bundle).
    if crate::anim::disk_cache::is_library(game) {
        let body=Arc::new(crate::anim::disk_cache::load::<crate::pose::Skeleton>(game,"prefab","studio/base/00:p_cf_body_bone").ok_or("native body-bone skeleton missing from the OK base library")?);
        if let Some(key)=common_key{let mut entries=COMMON.get_or_init(Default::default).lock().map_err(|_|"common prefab cache poisoned")?;if entries.len()>=8{entries.clear();}entries.insert(key,body.clone());}
        return Ok(body);
    }
    let start=std::time::Instant::now();
    // Resolve overrides and read current bytes on every first initialization /
    // explicit prepare. Full content identity preserves source invalidation.
    let override_bytes=crate::mods::override_bundle(&game.join("mods"),"studio/base/00.unity3d");
    let resolved=start.elapsed();
    let bytes=override_bytes.or_else(||crate::gamefs::read(game.join("abdata/studio/base/00.unity3d")).ok()).ok_or("native body-bone prefab missing")?;
    let loaded=start.elapsed();let key=factory_key(game,&bytes);let cache=CACHE.get_or_init(Default::default);
    let cached=cache.lock().map_err(|_|"paired prefab cache poisoned")?.get(&key).cloned();
    let hit=cached.is_some();let body=if let Some(body)=cached {body}else{
        let parsed=crate::unity::parse_bundle_fresh(bytes);parsed.configure_game_references(game);
        let body=Arc::new(parsed.skeleton_for_prefab_name("p_cf_body_bone").ok_or("native body-bone skeleton missing")?);
        crate::anim::disk_cache::store(game,"prefab","studio/base/00:p_cf_body_bone",&*body);
        let mut entries=cache.lock().map_err(|_|"paired prefab cache poisoned")?;
        if entries.len()>=8 {entries.clear();}entries.entry(key).or_insert_with(||body.clone()).clone()
    };
    if std::env::var("OK_PAIR_PROFILE").ok().as_deref()==Some("1") {eprintln!("OK paired prefab load_ms={:.3} resolve_ms={:.3} read_ms={:.3} prepare_ms={:.3} hit={}",loaded.as_secs_f64()*1000.,resolved.as_secs_f64()*1000.,(loaded-resolved).as_secs_f64()*1000.,(start.elapsed()-loaded).as_secs_f64()*1000.,i32::from(hit));}
    if let Some(key)=common_key{let mut entries=COMMON.get_or_init(Default::default).lock().map_err(|_|"common prefab cache poisoned")?;if entries.len()>=8{entries.clear();}entries.insert(key,body.clone());}
    Ok(body)
}
fn factory_runtime_from_prefab(session:&crate::session::OkSession,config:&crate::finalik::PrefabIkConfig,prefab:&crate::pose::Skeleton)->Result<PairIkRuntime,String>{
    let start=std::time::Instant::now();let mut body=prefab.clone();
    let height=session.body_shaped.as_ref().and_then(|s|s.tfs.values().find(|t|t.name=="cf_n_height")).ok_or("native height bone missing")?.scale;
    body.tfs.values_mut().find(|t|t.name=="cf_n_height").ok_or("prefab height bone missing")?.scale=height;body.rebuild_worlds();
    let mut sk=crate::finalik::IkSkeleton::new(&mut body);let mut solver=crate::finalik::IKSolverFullBodyBiped::from_prefab(config,&mut sk);solver.initiate(&mut sk);
    if std::env::var("OK_PAIR_PROFILE").ok().as_deref()==Some("1") {eprintln!("OK paired factory init_ms={:.3}",start.elapsed().as_secs_f64()*1000.);}
    Ok(PairIkRuntime{solver})
}
/// Conversion into an OK base library: decodes (and stores) the paired IK index and the body-bone skeleton once.
pub(crate) fn prepare_library(game:&Path)->Result<(usize,bool),String>{
    let index=setup_index(game)?;
    factory_prefab(game)?;
    Ok((index.1.len(),index.0.is_some()))
}
pub(crate) fn body_reaction_solver(session:&crate::session::OkSession,game:&Path)->Result<crate::finalik::IKSolverFullBodyBiped,String>{
    let index=setup_index(game)?;
    let config=index.0.as_ref().ok_or("native FullBodyBipedIK config missing")?;
    if config.effectors.len()!=9{return Err("incomplete native IK config".into());}
    let prefab=factory_prefab(game)?;
    Ok(factory_runtime_from_prefab(session,config,&prefab)?.solver)
}
#[cfg(test)]
fn factory_runtime(session:&crate::session::OkSession,config:&crate::finalik::PrefabIkConfig)->Result<PairIkRuntime,String>{
    factory_runtime_reference(session,config)
}
#[cfg(test)]
fn factory_runtime_reference(session:&crate::session::OkSession,config:&crate::finalik::PrefabIkConfig)->Result<PairIkRuntime,String>{
    let profile=std::env::var("OK_PAIR_PROFILE").ok().as_deref()==Some("1");let start=std::time::Instant::now();
    // SolverManager initializes once, before the Animator's first H sample.
    // Its joint rest pose comes from the body-bone prefab; cf_n_height has
    // already received the character's native height at that point.
    let path=session.game_root.join("abdata/studio/base/00.unity3d");
    let bytes=crate::mods::override_bundle(&session.game_root.join("mods"),"studio/base/00.unity3d").or_else(||crate::gamefs::read(&path).ok()).ok_or("native body-bone prefab missing")?;
    let loaded=start.elapsed();
    let parsed=crate::unity::parse_bundle_cached(bytes);parsed.configure_game_references(&session.game_root);
    let mut body=parsed.skeleton_for_prefab_name("p_cf_body_bone").ok_or("native body-bone skeleton missing")?;
    let parsed_time=start.elapsed();
    let height=session.body_shaped.as_ref().and_then(|s|s.tfs.values().find(|t|t.name=="cf_n_height")).ok_or("native height bone missing")?.scale;
    body.tfs.values_mut().find(|t|t.name=="cf_n_height").ok_or("prefab height bone missing")?.scale=height;body.rebuild_worlds();
    let mut sk=crate::finalik::IkSkeleton::new(&mut body);let mut solver=crate::finalik::IKSolverFullBodyBiped::from_prefab(config,&mut sk);solver.initiate(&mut sk);
    if profile {eprintln!("OK paired factory load_ms={:.3} parse_ms={:.3} init_ms={:.3}",loaded.as_secs_f64()*1000.,(parsed_time-loaded).as_secs_f64()*1000.,(start.elapsed()-parsed_time).as_secs_f64()*1000.);}
    Ok(PairIkRuntime{solver})
}

fn correct_frames(body:&mut crate::pose::Skeleton,config:&crate::finalik::PrefabIkConfig,active:&crate::finalik::MotionState,shapes:&[Vec<f32>;2],role:usize,after:bool)->Result<(),String>{
    for order in if after {vec![2]} else {vec![0,1]} {
        for frame in active.frames.iter().filter(|f|f.frame_no>=8&&config.frame_orders.get(f.frame_no as usize-8)==Some(&order)){
            let partner=if frame.edit_no==0{role}else{1-role};let(pos,ang)=crate::finalik::correct_shape_values(&shapes[partner],Some(frame));
            let name=config.frame_bones[frame.frame_no as usize-8].as_deref().ok_or("FrameCorrect bone binding missing")?;
            let tf=body.tfs.values_mut().find(|t|t.name==name).ok_or("FrameCorrect live bone missing")?;
            for axis in 0..3{tf.pos[axis]+=pos[axis];}tf.rot=crate::finalik::qmul(tf.rot,crate::pose::euler_deg_to_quat(ang));
        }
    }
    body.rebuild_worlds();Ok(())
}
fn index_motion(index:&mut std::collections::HashMap<String,Result<crate::finalik::MotionIkData,String>>,name:String,bytes:&[u8]){
    // The original sorted scan rejected any invalid matching asset, even if
    // a later bundle replaced it. Preserve that rule in the shared index.
    if !index.get(&name).is_some_and(Result::is_err){index.insert(name,checked_motion(bytes));}
}
type MotionIndex=(Option<crate::finalik::PrefabIkConfig>,std::collections::HashMap<String,Result<crate::finalik::MotionIkData,String>>);

fn setup_index(game:&Path)->Result<std::sync::Arc<MotionIndex>,String>{
    use std::sync::{Mutex,OnceLock};use std::collections::HashMap;
    // Decode the immutable source index/config once per resource revision,
    // rather than re-reading oo_base for every motion/state selection.
    // Both roles consume one revision-checked immutable snapshot per frame.
    static CACHE:OnceLock<Mutex<HashMap<std::path::PathBuf,(u64,std::sync::Arc<MotionIndex>)>>>=OnceLock::new();
    // Installed shared resources are immutable within their manifest revision,
    // just like their resident controller/clip caches. Verify that revision
    // once, rather than directory/metadata IO for every asset on every frame.
    // Original game/mod roots retain the full per-file invalidation below.
    use std::hash::{Hash,Hasher};
    let common_stamp=manifest_stamp(game).map(|stamp|u64::from_le_bytes(stamp[..8].try_into().unwrap()));
    let cache=CACHE.get_or_init(Default::default);
    if let Some(stamp)=common_stamp {if let Some(hit)=cache.lock().map_err(|_|"paired asset cache poisoned")?.get(game).filter(|(s,_)|*s==stamp).map(|(_,v)|v.clone()){return Ok(hit);}}
    // An OK base library root: the FBBIK config and the MotionIK assets (in the original scan order) were converted.
    if crate::anim::disk_cache::is_library(game) {
        let (config,raw):(Option<crate::finalik::PrefabIkConfig>,Vec<(String,Vec<u8>)>)=crate::anim::disk_cache::load(game,"pairik","index")
            .ok_or("paired IK data missing from the OK base library")?;
        let mut motions=HashMap::new();
        for (name,bytes) in raw {index_motion(&mut motions,name,&bytes);}
        let index=std::sync::Arc::new((config,motions));
        cache.lock().map_err(|_|"paired asset cache poisoned")?.insert(game.to_path_buf(),(common_stamp.unwrap_or(0),index.clone()));
        return Ok(index);
    }
    let mut paths:Vec<_>=crate::gamefs::read_dir(game.join("abdata/h/list")).map_err(|e|e.to_string())?.flatten().map(|e|e.path()).filter(|p|p.extension().is_some_and(|s|s.eq_ignore_ascii_case("unity3d"))).collect();paths.sort();
    let mut stamp=std::collections::hash_map::DefaultHasher::new();
    for path in paths.iter().chain(std::iter::once(&game.join("abdata/chara/oo_base.unity3d"))){path.hash(&mut stamp);let m=crate::gamefs::metadata(path).map_err(|e|e.to_string())?;m.len().hash(&mut stamp);m.modified().ok().hash(&mut stamp);}
    let stamp=common_stamp.unwrap_or_else(||stamp.finish());
    let hit=cache.lock().map_err(|_|"paired asset cache poisoned")?.get(game).filter(|(s,_)|*s==stamp).map(|(_,v)|v.clone());
    let index=if let Some(index)=hit {index}else{
        let mut motions=HashMap::new();
        let mut raw=Vec::new();
        for path in paths{for(name,bytes)in crate::list::text_assets_from_bundle(&path){
            // Retain validation failures by name too; a malformed requested
            // motion must fail, rather than silently lose its contacts.
            index_motion(&mut motions,name.clone(),&bytes);
            raw.push((name,bytes));
        }}
        let config=crate::unity::parse_prefab_ik(game);
        crate::anim::disk_cache::store(game,"pairik","index",&(&config,&raw));
        let index=std::sync::Arc::new((config,motions));
        cache.lock().map_err(|_|"paired asset cache poisoned")?.insert(game.to_path_buf(),(stamp,index.clone()));index
    };
    Ok(index)
}

fn setup(game:&Path,motion:&str,state:&str)->Result<Option<Setup>,String>{
    let index=setup_index(game)?;
    build_setup_from_index(&index,motion,state)
}

fn build_setup_from_index(index:&MotionIndex,motion:&str,state:&str)->Result<Option<Setup>,String>{
    // Only the selected state participates in this solve. Keep the entire
    // source index immutable; cloning every other state's frame/shape arrays
    // four times per paired frame was unnecessary runtime allocation.
    let value=match index.1.get(motion){Some(Ok(data))=>{let config=index.0.as_ref().ok_or("native FullBodyBipedIK config missing")?;match data.state(state){Some(active)=>Some((config.clone(),crate::finalik::MotionIkData{states:vec![active.clone()]})),None=>None}},Some(Err(error))=>return Err(error.clone()),None=>None};
    if let Some((config,data))=&value{if let Some(active)=data.state(state){if config.effectors.len()!=9||config.references.root.is_none()||config.frame_orders.len()!=config.frame_bones.len()||config.frame_orders.iter().any(|v|!(0..=2).contains(v))||active.parts.iter().any(|p|!(0..=1).contains(&p.sex)){return Err("unsupported native IK binding".into());}if active.frames.iter().any(|f|f.frame_no<0||f.frame_no>=8&&f.frame_no as usize-8>=config.frame_bones.len()||!(0..=1).contains(&f.edit_no)){return Err("unsupported native FrameCorrect binding".into());}}else{return Ok(None);}}
    Ok(value)
}
// Selected setups are immutable. A solver clones only its mutable MotionIkData;
// neither config validation nor config/string allocation belongs in each frame.
#[cfg(test)]
thread_local!{static UNCACHED_SETUPS:std::cell::Cell<bool>=const{std::cell::Cell::new(false)};}
#[cfg(test)]
fn with_uncached_setups<T>(run:impl FnOnce()->T)->T{
    struct Reset(bool);
    impl Drop for Reset{fn drop(&mut self){UNCACHED_SETUPS.with(|flag|flag.set(self.0));}}
    let _reset=Reset(UNCACHED_SETUPS.with(|flag|flag.replace(true)));run()
}
fn setup_from_index(index:&std::sync::Arc<MotionIndex>,motion:&str,state:&str)->Result<Option<SharedSetup>,String>{
    #[cfg(test)]
    if UNCACHED_SETUPS.with(|flag|flag.get()){return build_setup_from_index(index,motion,state).map(|value|value.map(std::sync::Arc::new));}
    type Entry=(std::sync::Arc<MotionIndex>,String,String,Option<SharedSetup>);
    thread_local!{static SETUPS:std::cell::RefCell<Vec<Entry>>=const{std::cell::RefCell::new(Vec::new())};}
    SETUPS.with(|cache|{
        let mut cache=cache.borrow_mut();
        if let Some(i)=cache.iter().position(|(source,m,s,_)|std::sync::Arc::ptr_eq(source,index)&&m==motion&&s==state){
            let entry=cache.remove(i);let result=entry.3.clone();cache.push(entry);return Ok(result);
        }
        let value=build_setup_from_index(index,motion,state)?.map(std::sync::Arc::new);
        if cache.len()>=16{cache.remove(0);}
        cache.push((index.clone(),motion.to_owned(),state.to_owned(),value.clone()));Ok(value)
    })
}
pub(crate) fn prepare_pair(game:&Path,state:&str,female_motion:&str,male_motion:&str)->Result<(),String>{
    let index=setup_index(game)?;
    let female=setup_from_index(&index,female_motion,state)?;let male=setup_from_index(&index,male_motion,state)?;
    if female.is_some()||male.is_some(){factory_prefab(game)?;}Ok(())
}
pub(crate) fn set_native_pair_parameters(female:&mut crate::session::OkSession,male:&mut crate::session::OkSession){
    // HActionBase.SetDefaultAnimatorFloat always writes female, and writes
    // male as well; _isFemale1=false refers to the second female only.
    let height=female.shape_body.first().copied().unwrap_or(0.5);
    for role in [female,male]{role.anim_parameters.insert("height".into(),height);role.anim_parameters.entry("motion".into()).or_insert(0.);}
}
pub(crate) fn validate_pair(female:&crate::session::OkSession,male:&crate::session::OkSession,state:&str,fm:&str,mm:&str)->Result<(),String>{
    validated_pair_setups(female,male,state,fm,mm).map(|_|())
}
fn controller_motion(source:&str)->Result<std::sync::Arc<String>,String> {
    thread_local!{static IDENTITIES:std::cell::RefCell<Vec<(String,std::sync::Arc<String>)>>=const{std::cell::RefCell::new(Vec::new())};}
    IDENTITIES.with(|cache|{
        let mut cache=cache.borrow_mut();
        if let Some(i)=cache.iter().rposition(|(key,_)|key==source){let entry=cache.remove(i);let result=entry.1.clone();cache.push(entry);return Ok(result);}
        let descriptor;if let Some(hex)=source.strip_prefix("states:layers@"){let bytes=(0..hex.len()).step_by(2).map(|i|u8::from_str_radix(hex.get(i..i+2).ok_or("invalid layer source")?,16).map_err(|_|"invalid layer source")).collect::<Result<Vec<_>,_>>()?;let (nested,_,_):(String,String,Vec<String>)=serde_json::from_slice(&bytes).map_err(|_|"invalid paired layers")?;descriptor=format!("states:{nested}");}else{descriptor=source.to_owned();}
        let hex=descriptor.strip_prefix("states:controller@").ok_or("pair requires an exact native controller source")?;
        if hex.len()%2!=0||hex.len()>65536{return Err("invalid paired controller identity".into());}
        let bytes=(0..hex.len()).step_by(2).map(|i|u8::from_str_radix(&hex[i..i+2],16)).collect::<Result<Vec<_>,_>>().map_err(|_|"invalid paired controller identity")?;
        let [_base,owner,_overlay,replacement]:[String;4]=serde_json::from_slice(&bytes).map_err(|_|"invalid paired controller identity")?;
        let motion=std::sync::Arc::new(if replacement.is_empty(){owner}else{replacement});
        if cache.len()>=16{cache.remove(0);}cache.push((source.to_owned(),motion.clone()));Ok(motion)
    })
}
pub(crate) fn validated_pair_setups(female:&crate::session::OkSession,male:&crate::session::OkSession,state:&str,fm:&str,mm:&str)->Result<[Option<SharedSetup>;2],String>{
    if female.sex!=1||male.sex!=0||female.game_root!=male.game_root{return Err("pair requires female/male sessions from one game".into());}
    for (session,motion) in [(female,fm),(male,mm)]{
        let (source,label)=session.anim_clip.split_once('|').ok_or("paired controller not selected")?;
        if controller_motion(source)?.as_str()!=motion||!label.ends_with(&format!(" / [Controller layer 0] Base Layer.{state}")){return Err("paired state/MotionIK identity mismatch".into());}
    }
    // Portable neutral display may release the mutable sampled rig while
    // retaining its shaped baseline. Sampling restores that rig before IK.
    // A session without either representation is still invalid before mutation.
    if [female,male].iter().any(|s|s.body_skel.is_none()&&s.body_shaped.is_none()&&s.body_rest.is_none()){return Err("body skeleton missing".into());}
    if female.shape_body.iter().chain(&male.shape_body).any(|v|!v.is_finite()){return Err("non-finite paired body shape".into());}
    let index=setup_index(&female.game_root)?;
    Ok([setup_from_index(&index,fm,state)?,setup_from_index(&index,mm,state)?])
}

/// The native MotionIK links of a paired state: (role, limb 0..4 = hand L/R, foot L/R, target role, target transform,
/// position weight). Role 0 is the female. A part's "sex" 0 means the role's own body, 1 the partner's.
pub(crate) fn motion_links(female:&crate::session::OkSession,male:&crate::session::OkSession,state:&str,fm:&str,mm:&str)->Result<Vec<(usize,usize,usize,String,f32)>,String>{
    let setups=validated_pair_setups(female,male,state,fm,mm)?;
    let mut out=Vec::new();
    for (role,setup) in setups.iter().enumerate(){
        let Some(setup)=setup else {continue;};
        let (_,data)=setup.as_ref();
        let Some(active)=data.state(state) else {continue;};
        for (limb,part) in active.parts.iter().enumerate().take(4){
            if part.target.is_empty()||part.weight_pos<=0.0{continue;}
            let target_role=if part.sex==0{role}else{1-role};
            out.push((role,limb,target_role,part.target.clone(),part.weight_pos));
        }
    }
    Ok(out)
}
pub(crate) fn apply_contacts(female:&mut crate::session::OkSession,male:&mut crate::session::OkSession,state:&str,female_motion:&str,male_motion:&str)->Result<usize,String> {
    if female.sex!=1||male.sex!=0||female.game_root!=male.game_root {return Err("pair requires female/male sessions from one game".into());}
    let index=setup_index(&female.game_root)?;
    let setups=[setup_from_index(&index,female_motion,state)?,setup_from_index(&index,male_motion,state)?];
    apply_contacts_prepared(female,male,state,setups,true)
}
pub(crate) fn apply_contacts_prepared(female:&mut crate::session::OkSession,male:&mut crate::session::OkSession,state:&str,setups:[Option<SharedSetup>;2],publish_export:bool)->Result<usize,String>{
    apply_contacts_prepared_with_policy(female,male,state,setups,publish_export,false,0.)
}
pub(crate) fn apply_contacts_prepared_with_policy(female:&mut crate::session::OkSession,male:&mut crate::session::OkSession,state:&str,setups:[Option<SharedSetup>;2],publish_export:bool,parallel:bool,dt:f32)->Result<usize,String>{
    let shapes=[female.shape_body.clone(),male.shape_body.clone()];
    let needs_factory=(setups[0].is_some()&&female.paired_ik_runtime.is_none())||(setups[1].is_some()&&male.paired_ik_runtime.is_none());
    let prefab=if needs_factory{Some(factory_prefab(&female.game_root)?)}else{None};
    for (role,setup) in setups.iter().enumerate() {
        let Some(setup)=setup else {continue;};let (config,data)=setup.as_ref();let active=data.state(state).unwrap();
        let session=if role==0 {&mut *female}else{&mut *male};
        if session.paired_ik_runtime.is_none(){
            session.paired_ik_runtime=Some(factory_runtime_from_prefab(session,config,prefab.as_deref().ok_or("paired prefab not prepared")?)?);
        }
        let body=session.body_skel.as_mut().ok_or("body skeleton missing")?;
        let _trace=crate::native_profile::Scope::new(30);
        correct_frames(body,config,active,&shapes,role,false)?;
    }
    // Sync target processors run before either solver. Never read a peer after
    // its solve: both roles must observe the same corrected animation sample.
    type FrozenTransform=([f32;3],[f32;4]);
    let mut frozen_parts:[Vec<Option<FrozenTransform>>;2]=[vec![],vec![]];
    let mut frozen_bends:[Vec<Option<FrozenTransform>>;2]=[vec![],vec![]];
    let mut frozen_roots=[crate::finalik::IDENTITY;2];
    {
        let _trace=crate::native_profile::Scope::new(31);
        let sources=[female.body_skel.as_mut().ok_or("female skeleton missing")?,male.body_skel.as_mut().ok_or("male skeleton missing")?];
        #[cfg(test)] let mut snapshots=[sources[0].clone(),sources[1].clone()];
        let [female_source,male_source]=sources;
        let mut reads=[crate::finalik::IkSkeleton::new_batched(female_source),crate::finalik::IkSkeleton::new_batched(male_source)];
        for role in 0..2 {
            let Some(setup)=&setups[role] else {continue;};let (_,data)=setup.as_ref();let active=data.state(state).unwrap();
            for part in &active.parts {
                let partner=if part.sex==0{role}else{1-role};
                let value=reads[partner].world_transform(&part.target);
                #[cfg(test)] {let mut old=crate::finalik::IkSkeleton::new(&mut snapshots[partner]);assert_eq!(value,old.world_pos(&part.target).zip(old.world_rot(&part.target)),"frozen target must match original skeleton snapshot");}
                frozen_parts[role].push(value);
            }
            for bend in &active.bends {
                let value=reads[role].world_transform(&bend.chein);
                #[cfg(test)] {let mut old=crate::finalik::IkSkeleton::new(&mut snapshots[role]);assert_eq!(value,old.world_pos(&bend.chein).zip(old.world_rot(&bend.chein)),"frozen bend must match original skeleton snapshot");}
                frozen_bends[role].push(value);
            }
            frozen_roots[role]=if reads[role].sk.world_tf("cf_j_root").is_some(){reads[role].world_rot("cf_j_root").unwrap_or(crate::finalik::IDENTITY)}else{crate::finalik::IDENTITY};
        }
    }
    // All peer reads completed above. Each solver now consumes only its owned
    // session and immutable frozen targets/shapes; join before private dynamics.
    let solve=|role:usize,session:&mut crate::session::OkSession,setup:Option<SharedSetup>|->Result<usize,String>{
        let _trace=crate::native_profile::Scope::new(32);
        let mut count=0;
        let Some(setup)=setup else {session.apply_body_interaction(dt);return Ok(0);};
        let (config,source_data)=setup.as_ref();let mut data=source_data.clone();
        let active=data.states.iter_mut().find(|s|s.name==state).unwrap();let post_corrections=active.clone();let mut proxies=Vec::new();
        for (i,part) in active.parts.iter_mut().enumerate() {
            // SetPartners puts this role first, then the other role.
            if !(0..=1).contains(&part.sex) {return Err("unsupported MotionIK partner index".into());}
            let alias=format!("__ok_pair_target_{i}");
            if let Some((pos,rot))=frozen_parts[role][i] {proxies.push((alias.clone(),pos,rot));part.target=alias;} else {part.target.clear();}
        }
        for (i,bend) in active.bends.iter_mut().enumerate() {
            let alias=format!("__ok_pair_bend_{i}");
            if let Some((pos,rot))=frozen_bends[role][i] {proxies.push((alias.clone(),pos,rot));bend.chein=alias;} else {bend.chein.clear();}
        }
        for frame in &mut active.frames {
            if !(0..=1).contains(&frame.edit_no) {return Err("unsupported shape partner index".into());}
            let partner=if frame.edit_no==0 {role}else{1-role};
            let (pos,ang)=crate::finalik::correct_shape_values(&shapes[partner],Some(frame));
            frame.shapes=vec![crate::finalik::MotionShape{shape_no:0,small:Default::default(),large:crate::finalik::PosAng{pos,ang}}];
        }
        for (i,bend) in active.bends.iter().enumerate() {
            let (pos,ang)=crate::finalik::correct_shape_values(&[1.],active.frame(i as i32*2+1));
            if let Some((_,p,r))=proxies.iter_mut().find(|(name,_,_)|*name==bend.chein) {
                let body_rotation=frozen_roots[role];
                let offset=crate::finalik::qrot(body_rotation,pos);for axis in 0..3 {p[axis]+=offset[axis];}*r=crate::finalik::qmul(*r,crate::pose::euler_deg_to_quat(ang));
            }
        }
        let reaction_offsets=session.body_interaction.as_mut().filter(|i|i.reaction.is_playing()).map(|i|i.offsets(dt,frozen_roots[role]));
        let body=session.body_skel.as_mut().unwrap();
        let mut ids=Vec::new();let mut id=-1;
        for (name,pos,rot) in proxies {while body.tfs.contains_key(&id) {id-=1;}body.tfs.insert(id,crate::pose::Tf{name,parent:None,sibling_index:None,pos,rot,scale:[1.;3]});ids.push(id);id-=1;}
        body.rebuild_worlds();
        {
            let runtime=session.paired_ik_runtime.as_mut().ok_or("paired IK runtime missing")?;let solver=&mut runtime.solver;
            if solver.effectors.len()!=9||config.references.root.is_none() {return Err("incomplete native IK config".into());}
            let parity=cfg!(test)||std::env::var("OK_ANIM_SKIN_PARITY").ok().as_deref()==Some("1");
            let oracle=if parity {
                let mut reference=body.clone();let mut old=solver.clone();
                {let mut sk=crate::finalik::IkSkeleton::new(&mut reference);old.link_ik(&data,state,&[1.],"cf_j_root",&mut sk);
                if let (Some(offsets),Some(interaction))=(reaction_offsets,session.body_interaction.as_ref()){interaction.apply_to_solver(&mut old,offsets);}
                old.update(&mut sk);}
                reference.rebuild_worlds();Some(reference)
            }else{None};
            // Opt-in performance oracle; normal playback always batches.
            let legacy=std::env::var("OK_ANIM_IK_LEGACY").ok().as_deref()==Some("1");
            let mut sk=if legacy {crate::finalik::IkSkeleton::new(body)}else{crate::finalik::IkSkeleton::new_batched(body)};
            solver.link_ik(&data,state,&[1.],"cf_j_root",&mut sk);
            if let (Some(offsets),Some(interaction))=(reaction_offsets,session.body_interaction.as_ref()){interaction.apply_to_solver(solver,offsets);}
            count+=solver.effectors.iter().filter(|e|e.position_weight>0.).count();solver.update(&mut sk);
            if let Some(reference)=oracle {
                sk.sk.rebuild_worlds();
                let valid=sk.sk.worlds==reference.worlds&&sk.sk.names==reference.names
                    &&sk.sk.tfs.iter().all(|(id,t)|reference.tfs.get(id).is_some_and(|r|t.pos==r.pos&&t.rot==r.rot&&t.scale==r.scale));
                eprintln!("OK animation ik parity valid={} role={role} bones={} exact=1",i32::from(valid),sk.sk.tfs.len());
                #[cfg(test)] assert!(valid,"batched solver must preserve original local transforms and published worlds");
            }
        }
        for id in ids {body.tfs.remove(&id);}
        {let _trace=crate::native_profile::Scope::new(33);
        correct_frames(body,&config,&post_corrections,&shapes,role,true)?;}
        if body.tfs.values().any(|t|t.pos.iter().chain(t.rot.iter()).chain(t.scale.iter()).any(|v|!v.is_finite())) {return Err("non-finite paired IK pose".into());}
        if publish_export {if let Some(mut ex)=session.skel_export.take() {crate::skel_export::refresh_pose(session,&mut ex);session.skel_export=Some(ex);}}
        Ok(count)
    };
    let [female_setup,male_setup]=setups;
    let (female_result,male_result)=if parallel {
        crate::session::native_parallel_work(||rayon::join(||solve(0,female,female_setup),||solve(1,male,male_setup)))
    }else{(Ok(solve(0,female,female_setup)?),solve(1,male,male_setup))};
    Ok(female_result?+male_result?)
}

#[cfg(test)]
mod tests {
    #[test]
    fn shared_setup_tracks_snapshot_state_and_mutation_isolation() {
        use std::sync::Arc;
        let mut config=crate::finalik::PrefabIkConfig::default();
        config.references.root=Some("root".into());
        config.effectors=vec![crate::finalik::PrefabEffector::default();9];
        let state=|name:&str|crate::finalik::MotionState{name:name.into(),..Default::default()};
        let data=crate::finalik::MotionIkData{states:vec![state("Idle"),state("Walk")]};
        let index=Arc::new((Some(config),std::collections::HashMap::from([("motion".into(),Ok(data))])));
        let first=super::setup_from_index(&index,"motion","Idle").unwrap().unwrap();
        let again=super::setup_from_index(&index,"motion","Idle").unwrap().unwrap();
        assert!(Arc::ptr_eq(&first,&again));
        let oracle=super::build_setup_from_index(&index,"motion","Idle").unwrap().unwrap();
        assert_eq!(format!("{:?}",first.as_ref()),format!("{oracle:?}"));
        let walk=super::setup_from_index(&index,"motion","Walk").unwrap().unwrap();
        assert!(!Arc::ptr_eq(&first,&walk));assert_eq!(walk.1.states[0].name,"Walk");
        let mut solver_data=first.1.clone();solver_data.states[0].name="solver-mutated".into();
        assert_eq!(again.1.states[0].name,"Idle");
        assert!(super::setup_from_index(&index,"motion","missing").unwrap().is_none());
        let replacement=Arc::new((*index).clone());
        let replaced=super::setup_from_index(&replacement,"motion","Idle").unwrap().unwrap();
        assert!(!Arc::ptr_eq(&first,&replaced));
        let mut bad=(*index).clone();bad.0.as_mut().unwrap().effectors.clear();
        assert!(super::setup_from_index(&Arc::new(bad),"motion","Idle").is_err());
        let mut invalid=(*index).clone();invalid.1.insert("motion".into(),Err("invalid motion bytes".into()));
        assert_eq!(super::setup_from_index(&Arc::new(invalid),"motion","Idle").unwrap_err(),"invalid motion bytes");
        // Eviction must not retain wrong state or a source with a reused address.
        for i in 0..20{assert!(super::setup_from_index(&index,"motion",&format!("absent{i}")).unwrap().is_none());}
        let rebuilt=super::setup_from_index(&index,"motion","Idle").unwrap().unwrap();
        assert_eq!(format!("{:?}",rebuilt.as_ref()),format!("{oracle:?}"));
    }
    #[test]
    fn controller_identity_cache_preserves_owner_override_and_failures() {
        let encode=|value:serde_json::Value|serde_json::to_vec(&value).unwrap().iter().map(|v|format!("{v:02X}")).collect::<String>();
        let source=format!("states:controller@{}",encode(serde_json::json!(["bundle","owner","overlay",""])));
        let first=super::controller_motion(&source).unwrap();assert_eq!(first.as_str(),"owner");
        assert!(std::sync::Arc::ptr_eq(&first,&super::controller_motion(&source).unwrap()));
        let replacement=format!("states:controller@{}",encode(serde_json::json!(["bundle","owner","overlay","replacement"])));
        assert_eq!(super::controller_motion(&replacement).unwrap().as_str(),"replacement");
        let layered=format!("states:layers@{}",encode(serde_json::json!([replacement.strip_prefix("states:").unwrap(),"base",["child"]])));
        assert_eq!(super::controller_motion(&layered).unwrap().as_str(),"replacement");
        for bad in ["", "states:controller@F", "states:controller@GG", "states:layers@F", "states:controller@5B5D"]{assert!(super::controller_motion(bad).is_err());}
        for i in 0..24{let source=format!("states:controller@{}",encode(serde_json::json!(["bundle",format!("owner{i}"),"",""])));assert_eq!(super::controller_motion(&source).unwrap().as_str(),format!("owner{i}"));}
        assert_eq!(super::controller_motion(&source).unwrap().as_str(),"owner");
    }
    use super::*;
    #[test]
    fn motion_index_preserves_errors_across_duplicate_assets(){
        let mut index=std::collections::HashMap::new();
        index_motion(&mut index,"motion".into(),&[0,0,0,0]);assert!(index["motion"].is_ok());
        index_motion(&mut index,"motion".into(),&[]);assert!(index["motion"].is_err());
        let error=index["motion"].as_ref().err().unwrap().clone();
        index_motion(&mut index,"motion".into(),&[0,0,0,0]);
        assert_eq!(index["motion"].as_ref().err().unwrap(),&error);
        index_motion(&mut index,"other".into(),&[0,0,0,0]);assert!(index["other"].is_ok());
    }
    #[test]
    fn prefab_full_content_identity_covers_unsampled_changes() {
        let a=vec![0u8;512];let mut b=a.clone();b[200]=1;
        assert_ne!(factory_key(Path::new("game_a"),&a),factory_key(Path::new("game_a"),&b));
        assert_ne!(factory_key(Path::new("game_a"),&a),factory_key(Path::new("game_b"),&a));
    }
    #[test]
    #[ignore="installed immutable factory prefab and height oracle"]
    fn installed_factory_prefab_matches_original_for_heights() {
        let game=&crate::test_support::game_path("");
        let mut session=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/female/[Card] March_7th_keiyuu.png")),0).unwrap();
        session.apply_shape(&session.shape_body.clone(),&session.shape_face.clone());
        let config=crate::unity::parse_prefab_ik(game).unwrap();
        let prefab=factory_prefab(game).unwrap();let again=factory_prefab(game).unwrap();assert!(std::sync::Arc::ptr_eq(&prefab,&again));
        let before=serde_json::to_vec(&*prefab).unwrap();
        for height in [[0.72;3],[1.02;3],[1.28;3]] {
            session.body_shaped.as_mut().unwrap().tfs.values_mut().find(|t|t.name=="cf_n_height").unwrap().scale=height;
            let mut old=factory_runtime_reference(&session,&config).unwrap();let mut new=factory_runtime_from_prefab(&session,&config,&prefab).unwrap();
            assert_eq!(old.solver.target_sync,new.solver.target_sync);old.solver.target_sync.clear();new.solver.target_sync.clear();
            assert_eq!(format!("{:?}",old.solver),format!("{:?}",new.solver),"all initial solver fields must match original prefab/height factory");
        }
        assert_eq!(before,serde_json::to_vec(&*prefab).unwrap());
        eprintln!("OK paired factory oracle exact=1 heights=3 template_immutable=1 same_content_hit=1");
    }
    fn oracle_skeleton(rows:&serde_json::Value)->crate::pose::Skeleton{
        let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];
        let q=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32,x["w"].as_f64().unwrap()as f32];
            let rows=rows.as_array().unwrap();let ids:std::collections::HashMap<_,_>=rows.iter().enumerate().map(|(i,r)|(r["path"].as_str().unwrap(),i as i64+1)).collect();
            let tfs=rows.iter().enumerate().map(|(i,r)|(i as i64+1,crate::pose::Tf{name:r["name"].as_str().unwrap().into(),parent:ids.get(r["parent"].as_str().unwrap()).copied(),sibling_index:None,pos:v(&r["pos"]),rot:q(&r["rot"]),scale:v(&r["scale"])})).collect();
            let mut s=crate::pose::Skeleton{names:rows.iter().map(|r|r["name"].as_str().unwrap().into()).collect(),worlds:vec![],rest:vec![],smr_count:rows.len(),tfs,bone_ids:(1..=rows.len()as i64).collect(),source_mesh:0};s.rebuild_worlds();s
    }
    #[test]
    #[ignore="compiled native complete paired correction/contact/solver adapter"]
    fn native_pair_adapter_matches(){
        use crate::finalik::*;
        let base=std::env::var("OK_H_ADAPTER_FIXTURE").unwrap_or("../../Saved/Tests/HPlayback335/NativeSolver336R4.json".into());
        let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(format!("{base}.pair-solver.json")).unwrap()).unwrap();
        let contracts:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(format!("{base}.pair-contract.json")).unwrap()).unwrap();
        let game=&crate::test_support::game_path("");let config=crate::unity::parse_prefab_ik(game).unwrap();
        for row in contracts["contracts"].as_array().unwrap(){for(i,frame)in row["frames"].as_array().unwrap().iter().enumerate(){assert_eq!(config.frame_orders[i],frame["order"].as_i64().unwrap()as i32);}}
        eprintln!("NATIVE FrameCorrect orders {:?}",config.frame_orders);
        let mut female=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/female/KK_226176.png")),0).unwrap();let mut male=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/male/KK_224305.png")),0).unwrap();female.skel_export=None;male.skel_export=None;
        let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];let q=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32,x["w"].as_f64().unwrap()as f32];
        female.apply_anim_time(0.);male.apply_anim_time(0.);
        let mut differences=Vec::new();let mut frames=0;let mut contacts=0;let mut max_pos=0f32;let mut max_rot=0f32;
        for sequence in dump["sequences"].as_array().unwrap(){
            if !dump["automaticInitialization"].as_bool().unwrap_or(false){
                for(role,session)in [(0,&mut female),(1,&mut male)]{let mut body=oracle_skeleton(&sequence["initial"][role]);let mut sk=IkSkeleton::new(&mut body);let mut solver=IKSolverFullBodyBiped::from_prefab(&config,&mut sk);solver.initiate(&mut sk);session.paired_ik_runtime=Some(PairIkRuntime{solver});}
            }
            let prefix=sequence["motion"].as_str().unwrap();
            for pair in sequence["frames"].as_array().unwrap().chunks_exact(2){
                female.body_skel=Some(oracle_skeleton(&pair[0]["sampled"]));male.body_skel=Some(oracle_skeleton(&pair[1]["sampled"]));female.anim_time=pair[0]["time"].as_f64().unwrap()as f32;male.anim_time=female.anim_time;
                contacts+=apply_contacts(&mut female,&mut male,sequence["state"].as_str().unwrap(),&format!("{prefix}_f_00"),&format!("{prefix}_m_00")).unwrap();
                for(role,session)in [(0,&mut female),(1,&mut male)]{let mut sk=IkSkeleton::new(session.body_skel.as_mut().unwrap());for bone in pair[role]["afterCorrection"].as_array().unwrap(){let name=bone["name"].as_str().unwrap();if !name.starts_with("cf_j_"){continue;}let pd=vlen(vsub(sk.world_pos(name).unwrap(),v(&bone["worldPos"])));let rd=1.-qdot(qnorm(sk.world_rot(name).unwrap()),qnorm(q(&bone["worldRot"]))).abs().min(1.);max_pos=max_pos.max(pd);max_rot=max_rot.max(rd);if pd>1e-4||rd>1e-5{differences.push(serde_json::json!({"motion":prefix,"role":role,"time":session.anim_time,"bone":name,"position_delta_m":pd,"quaternion_1_minus_abs_dot":rd}));}}frames+=1;}
            }
        }
        let result=serde_json::json!({"passed":differences.is_empty(),"automatic_initialization":dump["automaticInitialization"],"frames":frames,"contacts":contacts,"max_position_delta_m":max_pos,"max_quaternion_1_minus_abs_dot":max_rot,"differences":differences});let output=std::env::var("OK_H_ADAPTER_REPORT").unwrap_or("../../Saved/Tests/HPlayback335/Adapter336Audit.json".into());std::fs::write(&output,serde_json::to_vec_pretty(&result).unwrap()).unwrap();eprintln!("ADAPTER frames={frames} contacts={contacts} max_pos_m={max_pos} max_rot={max_rot} differences={}",differences.len());assert!(differences.is_empty(),"Native paired adapter mismatch; see {output}");
    }
    #[test]
    #[ignore="compiled persistent solver with identical native skeleton inputs"]
    fn native_persistent_solver_matches(){
        use crate::finalik::*;
        let fixture=std::env::var("OK_H_SOLVER_FIXTURE").unwrap_or("../../Saved/Tests/HPlayback335/NativeSolver336R4.json.pair-solver.json".into());
        let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(fixture).unwrap()).unwrap();
        let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];
        let q=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32,x["w"].as_f64().unwrap()as f32];
        let cfg=crate::unity::parse_prefab_ik(&crate::test_support::game_path("")).unwrap();let mut errors=Vec::new();let mut frames=0;let mut max_pos=0f32;let mut max_rot=0f32;
        let automatic=dump["automaticInitialization"].as_bool().unwrap_or(false);let mut trace=Vec::new();let mut read_trace=Vec::new();
        let mut solvers=Vec::new();
        for sequence in dump["sequences"].as_array().unwrap(){
            if solvers.is_empty()||!automatic{solvers=(0..2).map(|role|{let mut body=oracle_skeleton(&sequence["initial"][role]);let mut sk=IkSkeleton::new(&mut body);let mut solver=IKSolverFullBodyBiped::from_prefab(&cfg,&mut sk);solver.initiate(&mut sk);solver}).collect::<Vec<_>>();}
            for frame in sequence["frames"].as_array().unwrap(){
                let role=frame["role"].as_u64().unwrap()as usize;let solver=&mut solvers[role];let mut body=oracle_skeleton(&frame["input"]);
                for (i,t)in frame["targets"].as_array().unwrap().iter().enumerate(){solver.effectors[i].target=None;solver.effectors[i].position=v(&t["pos"]);solver.effectors[i].rotation=q(&t["rot"]);solver.effectors[i].position_weight=t["positionWeight"].as_f64().unwrap()as f32;solver.effectors[i].rotation_weight=t["rotationWeight"].as_f64().unwrap()as f32;}
                for(i,b)in frame["bends"].as_array().unwrap().iter().enumerate(){let name=format!("__oracle_bend_{i}");let id=-1-i as i64;body.tfs.insert(id,crate::pose::Tf{name:name.clone(),parent:None,sibling_index:None,pos:v(&b["pos"]),rot:IDENTITY,scale:[1.;3]});solver.bend_constraints[i+1].bend_goal=Some(name);solver.bend_constraints[i+1].weight=b["weight"].as_f64().unwrap()as f32;}
                body.rebuild_worlds();
                if let Some(chains)=frame["readChains"].as_array(){let mut rs=solver.clone();let mut rb=body.clone();let mut sk=IkSkeleton::new(&mut rb);rs.root_up=rs.root.as_ref().and_then(|n|sk.world_rot(n)).map(|q|qrot(q,[0.,1.,0.])).unwrap_or([0.,1.,0.]);rs.read_pose(&mut sk);
                    for(i,c)in chains.iter().enumerate(){for(j,n)in c["nodes"].as_array().unwrap().iter().enumerate(){let pd=vlen(vsub(rs.chains[i].nodes[j].solver_position,v(n)));let ld=rs.chains[i].length-c["length"].as_f64().unwrap()as f32;if pd>1e-7||ld.abs()>1e-7{let node=&rs.chains[i].nodes[j];read_trace.push(serde_json::json!({"motion":sequence["motion"],"state":sequence["state"],"role":role,"time":frame["time"],"chain":i,"node":j,"bone":node.name,"ours":node.solver_position,"native":v(n),"offset":node.offset,"position_delta_m":pd,"length_delta_m":ld}));}}}
                }
                let mut sk=IkSkeleton::new(&mut body);if frame["solverEnabled"].as_bool().unwrap_or(true){solver.update(&mut sk);}
                if let Some(chains)=frame["chains"].as_array(){for(i,chain)in chains.iter().enumerate(){for(j,node)in chain["nodes"].as_array().unwrap().iter().enumerate(){let pd=vlen(vsub(solver.chains[i].nodes[j].solver_position,v(node)));if pd>1e-6{trace.push(serde_json::json!({"motion":sequence["motion"],"state":sequence["state"],"role":role,"time":frame["time"],"chain":i,"node":j,"position_delta_m":pd,"length_delta_m":solver.chains[i].length-chain["length"].as_f64().unwrap()as f32}));}}}}
                for bone in frame["output"].as_array().unwrap(){let name=bone["name"].as_str().unwrap();if !name.starts_with("cf_j_"){continue;}let position=sk.world_pos(name).unwrap();let native_pos=v(&bone["worldPos"]);let pd=vlen(vsub(position,native_pos));let rotation=sk.world_rot(name).unwrap();let native_rot=q(&bone["worldRot"]);let rd=1.-qdot(qnorm(rotation),qnorm(native_rot)).abs().min(1.);max_pos=max_pos.max(pd);max_rot=max_rot.max(rd);if pd>1e-4||rd>1e-5{errors.push(serde_json::json!({"motion":sequence["motion"],"role":role,"time":frame["time"],"bone":name,"position_delta_m":pd,"quaternion_1_minus_abs_dot":rd}));}}
                frames+=1;
            }
        }
        let report=serde_json::json!({"passed":errors.is_empty(),"automatic_initialization":automatic,"frames":frames,"max_position_delta_m":max_pos,"max_quaternion_1_minus_abs_dot":max_rot,"differences":errors,"chain_trace":trace,"read_trace":read_trace});let output=std::env::var("OK_H_SOLVER_REPORT").unwrap_or("../../Saved/Tests/HPlayback335/Solver336Audit.json".into());std::fs::write(&output,serde_json::to_vec_pretty(&report).unwrap()).unwrap();eprintln!("SOLVER frames={frames} max_pos_m={max_pos} max_rot={max_rot} differences={}",errors.len());assert!(errors.is_empty(),"Native persistent solver mismatch; see {output}");
    }
    #[test]
    #[ignore="compiled native orthonormalization diagnostic"]
    fn native_ortho_primitives(){
        use crate::finalik::*;let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R11.json.pair-ortho-math.json").unwrap()).unwrap();let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];let mut rows=Vec::new();
        for(index,r)in dump.as_array().unwrap().iter().enumerate(){let (n,t)=vortho_normalize(v(&r["normal"]),v(&r["tangent"]));rows.push(serde_json::json!({"row":index,"normal_delta":vlen(vsub(n,v(&r["resultNormal"]))),"tangent_delta":vlen(vsub(t,v(&r["resultTangent"])))}));}
        std::fs::write("../../Saved/Tests/HPlayback337/OrthoVerified.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();assert!(rows.iter().all(|r|r["normal_delta"]==0.&&r["tangent_delta"]==0.));
    }    #[test]
    #[ignore="compiled native Transform rotation hierarchy diagnostic"]
    fn native_rotation_composition(){
        use crate::finalik::*;let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R7.json.pair-solver.json").unwrap()).unwrap();let q=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32,x["w"].as_f64().unwrap()as f32];let mut max=0f32;let mut sum=0f64;let mut count=0;
        for sequence in dump["sequences"].as_array().unwrap(){for frame in sequence["frames"].as_array().unwrap(){let mut body=oracle_skeleton(&frame["input"]);let mut sk=IkSkeleton::new(&mut body);for row in frame["input"].as_array().unwrap(){let ours=sk.world_rot(row["name"].as_str().unwrap()).unwrap();let native=q(&row["worldRot"]);let sign=if qdot(ours,native)<0.{-1.}else{1.};let delta=ours.iter().zip(native).map(|(a,b)|(a*sign-b).abs()).fold(0f32,f32::max);max=max.max(delta);sum+=delta as f64;count+=1;}}}
        std::fs::write("../../Saved/Tests/HPlayback337/RotationCompositionVerified.json",serde_json::to_vec_pretty(&serde_json::json!({"count":count,"max":max,"mean":sum/count as f64})).unwrap()).unwrap();assert!(max<=5e-7);
    }    #[test]
    #[ignore="compiled native triangle primitive diagnostic"]
    fn native_triangle_primitives(){
        use crate::finalik::*;let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R12.json.pair-triangle-math.json").unwrap()).unwrap();let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];let q=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32,x["w"].as_f64().unwrap()as f32];let mut rows=Vec::new();
        for(index,r)in dump.as_array().unwrap().iter().enumerate(){let d=v(&r["direction"]);let b=v(&r["bend"]);let mag=r["magnitude"].as_f64().unwrap()as f32;let sq=r["sqrMag1"].as_f64().unwrap()as f32;let diff=r["sqrMagDif"].as_f64().unwrap()as f32;let (y,z)=triangle_bend_components(mag,sq,diff);assert_eq!(z,r["z"].as_f64().unwrap()as f32);assert_eq!(y,r["y"].as_f64().unwrap()as f32);let look=qlook_rotation(d,b);let ours=qrot(look,[0.,y,z]);let expected=v(&r["result"]);let nl=q(&r["look"]);let native_look_result=qrot(nl,[0.,y,z]);rows.push(serde_json::json!({"row":index,"delta":vlen(vsub(ours,expected)),"native_look_delta":vlen(vsub(native_look_result,expected)),"look_component_delta":look.iter().zip(nl).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max),"ours":ours,"native":expected}));}
        std::fs::write("../../Saved/Tests/HPlayback337/TriangleVerified.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();assert!(rows.iter().all(|r|r["native_look_delta"]==0.&&r["look_component_delta"].as_f64().unwrap()<=1e-5));
    }    #[test]
    #[ignore="compiled native vector interpolation diagnostics"]
    fn native_vector_primitives(){
        use crate::finalik::*;let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R9.json.pair-vector-math.json").unwrap()).unwrap();let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];let mut rows=Vec::new();
        for(index,row)in dump.as_array().unwrap().iter().enumerate(){let from=v(&row["normal"]);let to=v(&row["from"]);let t=row["t"].as_f64().unwrap()as f32;let expected=v(&row["slerp"]);let ma=vlen(from);let mb=vlen(to);
            for mode in ["basis","sine_divide","sine_reciprocal","axis"]{let ours=if mode=="basis"{vslerp(from,to,t)}else if ma<1e-9||mb<1e-9{vlerp(from,to,t)}else{let a=if mode=="sine_reciprocal"{vmul(from,1./ma)}else{std::array::from_fn(|i|from[i]/ma)};let b=if mode=="sine_reciprocal"{vmul(to,1./mb)}else{std::array::from_fn(|i|to[i]/mb)};let dot=vdot(a,b).clamp(-1.,1.);let angle=dot.acos();let len=ma+(mb-ma)*t;
                if mode=="axis"{vmul(qrot(qangle_axis(angle.to_degrees()*t,vcross(a,b)),a),len)}else if angle.sin().abs()<1e-9{vlerp(from,to,t)}else{vmul(vadd(vmul(a,((1.-t)*angle).sin()/angle.sin()),vmul(b,(t*angle).sin()/angle.sin())),len)}
            };let delta=vlen(vsub(ours,expected));rows.push(serde_json::json!({"row":index,"mode":mode,"delta":delta,"ours":ours,"native":expected}));}
        }std::fs::write("../../Saved/Tests/HPlayback337/VectorDiagnosticCurrent.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }
    #[test]
    #[ignore="compiled natural bend initialization diagnostic"]
    fn native_bend_initialization(){
        use crate::finalik::*;let game=&crate::test_support::game_path("");let cfg=crate::unity::parse_prefab_ik(game).unwrap();
        let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R7.json.pair-solver.json").unwrap()).unwrap();
        let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];let mut rows=Vec::new();
        for role in 0..2{let initial=&dump["firstInitialization"][role];let mut body=oracle_skeleton(initial);
            let mut sk=IkSkeleton::new(&mut body);let mut solver=IKSolverFullBodyBiped::from_prefab(&cfg,&mut sk);solver.initiate(&mut sk);
            for name in ["cf_j_arm00_R","cf_j_forearm01_R","cf_j_hand_R"]{let ours=sk.world_pos(name).unwrap();let expected=v(&initial.as_array().unwrap().iter().find(|b|b["name"]==name).unwrap()["worldPos"]);rows.push(serde_json::json!({"role":role,"key":name,"ours":ours,"native":expected,"delta":vlen(vsub(ours,expected))}));}
            for(i,bend)in solver.bend_constraints.iter().enumerate().skip(1){let native=&dump["initialBends"][role][i];for(j,key)in ["direction","localDirection","childDirection"].into_iter().enumerate(){let ours=bend.initialization_directions()[j];let expected=v(&native[key]);rows.push(serde_json::json!({"role":role,"chain":i,"key":key,"ours":ours,"native":expected,"delta":vlen(vsub(ours,expected))}));}}
        }std::fs::write("../../Saved/Tests/HPlayback337/BendInitializationVerified.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
        assert!(rows.iter().all(|r|r["delta"].as_f64().unwrap()<=1e-7));
    }
    #[test]
    #[ignore="serialized native solver parameter diagnostics"]
    fn serialized_solver_parameters(){
        let game=&crate::test_support::game_path("");let parsed=crate::unity::parse_bundle_cached(crate::gamefs::read(game.join("abdata/chara/oo_base.unity3d")).unwrap());
        let mut out=String::new();parsed.with_serialized(|files|for file in files{for info in file.objects_of_type(114){let Ok(obj)=UnityObject::from_serialized_file(file,&info)else{continue;};let props=obj.class.properties();if props.contains_key("references")&&props.contains_key("solver"){out.push_str(&format!("{props:#?}\n"));}}});
        std::fs::write("../../Saved/Tests/HPlayback337/SerializedSolverParameters.txt",out).unwrap();
    }
    #[test]
    #[ignore="compiled Unity quaternion primitives diagnostic"]
    fn native_quaternion_primitives(){
        use crate::finalik::*;
        let rows:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R3.json.pair-math.json").unwrap()).unwrap();
        let v=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32];let q=|x:&serde_json::Value|[x["x"].as_f64().unwrap()as f32,x["y"].as_f64().unwrap()as f32,x["z"].as_f64().unwrap()as f32,x["w"].as_f64().unwrap()as f32];
        let result:Vec<_>=rows.as_array().unwrap().iter().map(|row|{let a=v(&row["from"]);let b=v(&row["to"]);let ft=qfrom_to_rotation(a,b);let look=qlook_rotation(a,b);let native=q(&row["fromTo"]);let nl=q(&row["look"]);serde_json::json!({"from":a,"to":b,"from_to":ft,"native_from_to":native,"look":look,"native_look":nl,"from_to_component_delta":ft.iter().zip(native).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max),"look_component_delta":look.iter().zip(nl).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max)})}).collect();
        std::fs::write("../../Saved/Tests/HPlayback337/QuaternionPrimitivesR1.json",serde_json::to_vec_pretty(&result).unwrap()).unwrap();
        assert!(result.iter().all(|r|r["from_to_component_delta"].as_f64().unwrap()<=1e-6&&r["look_component_delta"].as_f64().unwrap()<=1e-5),"compiled Unity quaternion primitive mismatch");
    }
    #[test]
    #[ignore="native automatic initialization prefab comparison"]
    fn native_initial_prefab_matches(){
        use crate::finalik::*;
        let game=&crate::test_support::game_path("");
        let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeAuto337R1.json.pair-solver.json").unwrap()).unwrap();
        let mut native=oracle_skeleton(&dump["sequences"][0]["initial"][0]);
        let mut results=Vec::new();
        for bundle in ["abdata/chara/oo_base.unity3d","abdata/studio/base/00.unity3d"]{
            let parsed=crate::unity::parse_bundle_cached(crate::gamefs::read(game.join(bundle)).unwrap());parsed.configure_game_references(game);
            let mut body=parsed.skeleton_for_prefab_name("p_cf_body_bone").unwrap();
            let height=native.tfs.values().find(|t|t.name=="cf_n_height").unwrap().scale;
            body.tfs.values_mut().find(|t|t.name=="cf_n_height").unwrap().scale=height;body.rebuild_worlds();
            let cfg=crate::unity::parse_prefab_ik(game).unwrap();
            let names:std::collections::BTreeSet<_>=cfg.chains.iter().flat_map(|c|c.nodes.iter()).chain(cfg.spine_bones.iter()).chain(cfg.bone_mappings.iter().filter_map(|(n,_)|n.as_ref())).chain(cfg.limb_mappings.iter().flat_map(|m|[m.parent_bone.as_ref(),m.bone1.as_ref(),m.bone2.as_ref(),m.bone3.as_ref()].into_iter().flatten())).cloned().collect();
            let mut source=IkSkeleton::new(&mut body);let mut oracle=IkSkeleton::new(&mut native);let mut differences=Vec::new();let mut max=0f32;
            for name in &names{
                let Some(pos)=source.world_pos(name)else{continue;};let native_pos=oracle.world_pos(name).unwrap();let pd=vlen(vsub(pos,native_pos));max=max.max(pd);if pd>1e-6{differences.push(serde_json::json!({"bone":name,"pos_delta_m":pd,"source":pos,"native":native_pos,"source_tf":source.sk.tfs.values().find(|t|t.name==*name),"native_tf":oracle.sk.tfs.values().find(|t|t.name==*name)}));}
            }
            results.push(serde_json::json!({"bundle":bundle,"max_position_delta_m":max,"differences":differences}));
        }
        std::fs::write("../../Saved/Tests/HPlayback337/InitialPrefabR1.json",serde_json::to_vec_pretty(&results).unwrap()).unwrap();
        assert!(results.iter().all(|r|r["differences"].as_array().unwrap().is_empty()),"solver initialization bones disagree with native factory");
        eprintln!("INITIAL PREFAB {results:?}");
    }
    #[test]
    #[ignore="native peer target availability in live paired rigs"]
    fn installed_native_contacts_have_live_targets(){
        let game=&crate::test_support::game_path("");let mut female=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/female/KK_226176.png")),0).unwrap();let mut male=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/male/KK_224305.png")),0).unwrap();
        let mut modes=std::collections::HashSet::new();
        for pair in pairs(game).into_iter().filter(|p|p.participants==2).filter(|p|modes.insert(p.category.clone())){
            for (session,source,layer)in [(&mut female,&pair.female_source,pair.female_layer),(&mut male,&pair.male_source,pair.male_layer)]{
                let label=crate::anim::clip_names_for_source(game,source).into_iter().find(|l|l.ends_with(" / [Controller layer 0] Base Layer.Idle")).unwrap();let key=crate::anim::native_pair_clip(game,&format!("{source}|{label}"),layer).unwrap();let key=std::ffi::CString::new(key).unwrap();unsafe{assert_eq!(crate::ffi::set_anim_clip(session,key.as_ptr()),0);}session.anim_scene_mode=3;session.sample_paired_frame(0.61);
            }
            let mut expected=0;
            for (role,motion)in [(0,&pair.female_motion),(1,&pair.male_motion)]{
                let Some((_,data))=setup(game,motion,"Idle").unwrap()else{panic!("native Idle contacts missing");};let active=data.state("Idle").unwrap();
                for part in &active.parts{if part.target.is_empty(){continue;}let partner=if part.sex==0{role}else{1-role};let body=if partner==0{female.body_skel.as_ref().unwrap()}else{male.body_skel.as_ref().unwrap()};assert!(body.tfs.values().any(|t|t.name==part.target),"native live target missing role={role} target={} category={}",part.target,pair.category);expected+=usize::from(part.weight_pos>0.);}
            }
            let contacts=apply_contacts(&mut female,&mut male,"Idle",&pair.female_motion,&pair.male_motion).unwrap();assert_eq!(contacts,expected,"missing active contacts {}",pair.category);eprintln!("LIVE CONTACTS {} expected={expected} active={contacts}",pair.category);
        }
        assert_eq!(modes.len(),2);
    }
    #[test]
    #[ignore="compiled native MotionIK contract oracle"]
    fn native_motionik_contract_matches(){
        let game=&crate::test_support::game_path("");let fixture:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("../../Saved/Tests/HPlayback335/NativeContractR3.json.pair-contract.json").unwrap()).unwrap();
        let vec3=|v:&serde_json::Value|[v["x"].as_f64().unwrap()as f32,v["y"].as_f64().unwrap()as f32,v["z"].as_f64().unwrap()as f32];
        let quat=|v:&serde_json::Value|[v["x"].as_f64().unwrap()as f32,v["y"].as_f64().unwrap()as f32,v["z"].as_f64().unwrap()as f32,v["w"].as_f64().unwrap()as f32];
        let compare=|a:&[f32],b:&[f32]|{assert_eq!(a.len(),b.len());let max=a.iter().zip(b).map(|(a,b)|(a-b).abs()).fold(0.,f32::max);assert!(max<=2e-6,"native shape delta {max} {a:?} {b:?}");max};
        let mut max=0f32;let mut targets=0;let mut frames=0;
        for row in fixture["contracts"].as_array().unwrap(){
            let motion=row["motion"].as_str().unwrap();let state=row["state"].as_str().unwrap();let role=row["sex"].as_i64().unwrap();let partners=row["partnerSex"].as_array().unwrap();assert_eq!(partners[0].as_i64().unwrap(),role);assert_eq!(partners[1].as_i64().unwrap(),1-role);
            let own:Vec<f32>=row["ownShape"].as_array().unwrap().iter().map(|v|v.as_f64().unwrap()as f32).collect();let peer:Vec<f32>=row["otherShape"].as_array().unwrap().iter().map(|v|v.as_f64().unwrap()as f32).collect();
            let Some((config,data))=setup(game,motion,state).unwrap()else{let config=crate::unity::parse_prefab_ik(game).unwrap();for t in row["targets"].as_array().unwrap(){assert_eq!(t["positionWeight"].as_f64().unwrap(),0.);assert_eq!(t["rotationWeight"].as_f64().unwrap(),0.);assert_eq!(t["bendWeight"].as_f64().unwrap(),0.);targets+=1;}for (i,f)in row["frames"].as_array().unwrap().iter().enumerate(){assert_eq!(config.frame_bones[i].as_deref().unwrap(),f["bone"].as_str().unwrap());assert_eq!(f["enabled"].as_bool(),Some(false));frames+=1;}continue;};
            let active=data.state(state).unwrap();
            for(index,target)in row["targets"].as_array().unwrap().iter().enumerate(){
                let p=&active.parts[index];let b=&active.bends[index];assert_eq!(p.target,target["bone"].as_str().unwrap(),"{motion} {state} target {index}");assert_eq!(b.chein,target["bendBone"].as_str().unwrap());
                for (expected,name)in [(if p.target.is_empty(){0.}else{p.weight_pos},"positionWeight"),(if p.target.is_empty(){0.}else{p.weight_ang},"rotationWeight"),(if b.chein.is_empty(){0.}else{b.weight},"bendWeight")]{max=max.max(compare(&[expected],&[target[name].as_f64().unwrap()as f32]));}
                for (no,pos,rot)in [(index as i32*2,"pos","rot"),(index as i32*2+1,"bendPos","bendRot")]{let frame=active.frame(no);let shape=if frame.is_some_and(|f|f.edit_no==1){&peer}else{&own};let (p,a)=crate::finalik::correct_shape_values(shape,frame);max=max.max(compare(&p,&vec3(&target[pos])));let q=crate::pose::euler_deg_to_quat(a);let native=quat(&target[rot]);let sign=if q.iter().zip(native).map(|(a,b)|a*b).sum::<f32>()<0.{-1.}else{1.};max=max.max(compare(&q,&native.map(|v|v*sign)));}targets+=1;
            }
            for (index,frame)in row["frames"].as_array().unwrap().iter().enumerate(){assert_eq!(config.frame_bones[index].as_deref().unwrap(),frame["bone"].as_str().unwrap());let f=active.frame(index as i32+8);assert_eq!(f.is_some(),frame["enabled"].as_bool().unwrap());let shape=if f.is_some_and(|f|f.edit_no==1){&peer}else{&own};let (p,a)=crate::finalik::correct_shape_values(shape,f);max=max.max(compare(&p,&vec3(&frame["pos"])));let q=crate::pose::euler_deg_to_quat(a);let native=quat(&frame["rot"]);let sign=if q.iter().zip(native).map(|(a,b)|a*b).sum::<f32>()<0.{-1.}else{1.};max=max.max(compare(&q,&native.map(|v|v*sign)));frames+=1;}
        }
        assert_eq!(targets,64);assert_eq!(frames,384);eprintln!("NATIVE MotionIK contract targets={targets} frame_bindings={frames} max_component_delta={max}");
    }
    #[test]
    #[ignore="installed pair state labels"]
    fn installed_pair_state_labels(){let game=&crate::test_support::game_path("");let p=pairs(game).into_iter().find(|p|p.participants==2).unwrap();for source in [&p.female_source,&p.male_source]{let names=crate::anim::clip_names_for_source(game,source);eprintln!("LAYERS female={} male={} LABELS {:?}",p.female_layer,p.male_layer,&names[..names.len().min(16)]);}}
    #[test]
    fn rejects_truncated_and_hostile_motion_data(){
        assert!(checked_motion(&[0,0,0,0]).is_ok());
        for n in 0..4{assert!(checked_motion(&[0,0,0,0][..n]).is_err());}
        assert!(checked_motion(&i32::MAX.to_le_bytes()).is_err());
        assert!(checked_motion(&(-1i32).to_le_bytes()).is_err());
        assert!(checked_motion(&[1,0,0,0,255,255,255,255,255]).is_err());
        assert!(checked_motion(&[0,0,0,0,1]).is_err());
    }
    #[test]
    #[ignore="installed paired sessions and contact smoke regression"]
    fn installed_pair_shared_clock_and_contacts(){
        let game=&crate::test_support::game_path("");
        let mut female=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/female/KK_226176.png")),0).unwrap();
        let mut male=crate::session::open(game,Some(&crate::test_support::game_path("/UserData/chara/male/KK_224305.png")),0).unwrap();
        let mut frames=0;let mut active_contacts=0;
        for pair in pairs(game).into_iter().filter(|p|p.participants==2).take(8){
            let f=crate::anim::clip_names_for_source(game,&pair.female_source);let m=crate::anim::clip_names_for_source(game,&pair.male_source);
            let common:Vec<_>=f.iter().filter_map(|label|label.split_once(" / [Controller layer 0] Base Layer.").map(|(_,state)|(label,state))).filter(|(_,state)|!state.contains('.')).filter_map(|(fl,state)|m.iter().find(|ml|ml.ends_with(&format!(" / [Controller layer 0] Base Layer.{state}"))).map(|ml|(fl,ml,state))).collect();
            assert!(!common.is_empty(),"pair {} {}",pair.category,pair.id);
            for (fl,ml,state) in common.into_iter().take(4){
                let fk=std::ffi::CString::new(format!("{}|{}",pair.female_source,fl)).unwrap();let mk=std::ffi::CString::new(format!("{}|{}",pair.male_source,ml)).unwrap();
                unsafe{assert_eq!(crate::ffi::set_anim_clip(&mut female,fk.as_ptr()),0);assert_eq!(crate::ffi::set_anim_clip(&mut male,mk.as_ptr()),0);}
                female.anim_scene_mode=3;male.anim_scene_mode=3;
                for time in [0.,0.25,0.61,1.25]{
                    validate_pair(&female,&male,state,&pair.female_motion,&pair.male_motion).unwrap();set_native_pair_parameters(&mut female,&mut male);assert_eq!(female.anim_parameters["height"],male.anim_parameters["height"]);female.sample_paired_frame(time);male.sample_paired_frame(time);
                    active_contacts+=apply_contacts(&mut female,&mut male,state,&pair.female_motion,&pair.male_motion).unwrap();
                    female.finish_paired_dynamics(1./60.);male.finish_paired_dynamics(1./60.);
                    assert_eq!(female.anim_time,male.anim_time);assert_eq!(female.anim_time,time);
                    for session in [&female,&male]{assert!(session.body_skel.as_ref().unwrap().tfs.values().all(|tf|tf.pos.iter().chain(tf.rot.iter()).all(|v|v.is_finite())));assert!(!session.body_skel.as_ref().unwrap().tfs.values().any(|tf|tf.name.starts_with("__ok_pair_")));}
                    frames+=1;
                }
            }
        }
        assert!(active_contacts>0);eprintln!("PAIRED frames={frames} active_contacts={active_contacts} clocks=equal finite=true");
    }
    #[test]
    #[ignore="explicit APNG cards and common paired resources"]
    fn portable_parallel_pair_matches_serial_physics() {
        portable_parallel_pair_oracle(false);
    }
    #[test]
    #[ignore="explicit APNG cards and common paired resources"]
    fn portable_deferred_shape_matches_full_prepare_and_particles() {
        portable_parallel_pair_oracle(true);
    }
    fn portable_parallel_pair_oracle(live_shape:bool) {
        let root=std::path::PathBuf::from(std::env::var("OK_TEST_PAIR_ROOT").unwrap());
        let load=|key:&str|crate::portable::load(Path::new(&std::env::var(key).unwrap())).unwrap();
        let mut serial_f=load("OK_TEST_PAIR_FEMALE");let mut serial_m=load("OK_TEST_PAIR_MALE");
        let mut parallel_f=crate::session::OkSession::from_animation_bytes(&serial_f.to_animation_bytes().unwrap()).unwrap();
        let mut parallel_m=crate::session::OkSession::from_animation_bytes(&serial_m.to_animation_bytes().unwrap()).unwrap();
        if live_shape {
            // Both preparations start with the same reconstructed runtime,
            // including accessory particle history, not APNG import versus snapshot.
            serial_f=crate::session::OkSession::from_animation_bytes(&serial_f.to_animation_bytes().unwrap()).unwrap();
            serial_m=crate::session::OkSession::from_animation_bytes(&serial_m.to_animation_bytes().unwrap()).unwrap();
        }
        let root_c=std::ffi::CString::new(root.to_str().unwrap()).unwrap();let mut frames=0;let mut measured=0;let mut serial_ms=0.;let mut parallel_ms=0.;
        for pair in pairs(&root).into_iter().filter(|p|p.participants==2).take(2) {
            let female=crate::anim::clip_names_for_source(&root,&pair.female_source);
            let male=crate::anim::clip_names_for_source(&root,&pair.male_source);
            let (fl,state,ml)=female.iter().filter_map(|f|f.split_once(" / [Controller layer 0] Base Layer.").map(|(_,s)|(f,s)))
                .find_map(|(f,s)|male.iter().find(|m|m.ends_with(&format!(" / [Controller layer 0] Base Layer.{s}"))).map(|m|(f,s,m))).unwrap();
            let fk=std::ffi::CString::new(format!("{}|{}",pair.female_source,fl)).unwrap();let mk=std::ffi::CString::new(format!("{}|{}",pair.male_source,ml)).unwrap();
            for (session,key) in [(&mut serial_f,&fk),(&mut parallel_f,&fk),(&mut serial_m,&mk),(&mut parallel_m,&mk)] {
                unsafe{assert_eq!(crate::ffi::ok_select_common_animation(session,root_c.as_ptr(),key.as_ptr()),0);}
                session.anim_scene_mode=3;
            }
            let state_c=std::ffi::CString::new(state).unwrap();let fm=std::ffi::CString::new(pair.female_motion.clone()).unwrap();let mm=std::ffi::CString::new(pair.male_motion.clone()).unwrap();
            // Match a freshly opened APNG neutral display: the immutable
            // shaped baseline exists, but no animation frame has run yet.
            for session in [&mut serial_f,&mut serial_m,&mut parallel_f,&mut parallel_m] {assert!(session.body_shaped.is_some()||session.body_rest.is_some());session.body_skel=None;}
            for i in 0..32 {
                if live_shape {for (oracle,optimized) in [(&mut serial_f,&mut parallel_f),(&mut serial_m,&mut parallel_m)] {
                    let mut body=optimized.shape_body.clone();let mut face=optimized.shape_face.clone();
                    let id=if i%8==0 {((i/8)*4).min(body.len().saturating_sub(1))}else{21};
                    if let Some(v)=body.get_mut(id){*v=0.5+0.3*(i as f32*0.31).sin();}
                    if i%11==0 {if let Some(v)=face.get_mut(0){*v=0.5+0.2*(i as f32*0.19).cos();}}
                    oracle.prepare_shape_live_oracle(&body,&face);optimized.prepare_shape_live(&body,&face);
                    assert_eq!(oracle.body_shaped.as_ref().map(|s|&s.worlds),optimized.body_shaped.as_ref().map(|s|&s.worlds));
                    assert_eq!(oracle.body_shaped.as_ref().map(|s|&s.rest),optimized.body_shaped.as_ref().map(|s|&s.rest));
                    assert_eq!(oracle.head_shaped.as_ref().map(|s|&s.worlds),optimized.head_shaped.as_ref().map(|s|&s.worlds));
                    assert_eq!(oracle.head_shaped.as_ref().map(|s|&s.rest),optimized.head_shaped.as_ref().map(|s|&s.rest));
                    assert_eq!(oracle.head_full_posed.as_ref().map(|s|&s.worlds),optimized.head_full_posed.as_ref().map(|s|&s.worlds));
                    assert_eq!(oracle.body_dynamic_runtime.as_ref().map(|s|s.reference_snapshot()),optimized.body_dynamic_runtime.as_ref().map(|s|s.reference_snapshot()));
                }}
                let time=i as f32/120.;let dt=1./120.;
                let begin=std::time::Instant::now();
                if live_shape {
                    // Isolate shape preparation: both sides use the production
                    // frozen-contact transaction and identical physics scheduling.
                    let mut error=[0i8;1024];with_uncached_setups(||unsafe{assert_eq!(crate::ffi::ok_animation_pair_frame(&mut serial_f,&mut serial_m,time,dt,state_c.as_ptr(),fm.as_ptr(),mm.as_ptr(),error.as_mut_ptr(),1024),0);});
                } else {
                    set_native_pair_parameters(&mut serial_f,&mut serial_m);serial_f.sample_paired_frame(time);serial_m.sample_paired_frame(time);
                    with_uncached_setups(||apply_contacts(&mut serial_f,&mut serial_m,state,&pair.female_motion,&pair.male_motion)).unwrap();serial_f.finish_paired_dynamics(dt);serial_m.finish_paired_dynamics(dt);
                }
                let serial_elapsed=begin.elapsed().as_secs_f64()*1000.;let begin=std::time::Instant::now();
                let mut error=[0i8;1024];unsafe{assert_eq!(crate::ffi::ok_animation_pair_frame(&mut parallel_f,&mut parallel_m,time,dt,state_c.as_ptr(),fm.as_ptr(),mm.as_ptr(),error.as_mut_ptr(),1024),0);}
                if i>=4{serial_ms+=serial_elapsed;parallel_ms+=begin.elapsed().as_secs_f64()*1000.;measured+=1;}
                for (a,b) in [(&serial_f,&parallel_f),(&serial_m,&parallel_m)] {
                    assert_eq!(a.anim_time,b.anim_time);
                    assert_eq!(a.head_skel.as_ref().map(|s|&s.worlds),b.head_skel.as_ref().map(|s|&s.worlds));
                    assert_eq!(a.body_dynamic_runtime.as_ref().map(|s|s.particle_snapshot()),b.body_dynamic_runtime.as_ref().map(|s|s.particle_snapshot()));
                    for (id,tf) in &a.body_skel.as_ref().unwrap().tfs {let other=&b.body_skel.as_ref().unwrap().tfs[id];assert_eq!(tf.pos,other.pos);assert_eq!(tf.rot,other.rot);assert_eq!(tf.scale,other.scale);}
                    for (part_index,(ap,bp)) in a.parts.iter().zip(&b.parts).enumerate() {
                        assert_eq!(ap.runtime_accessory.as_ref().and_then(|r|r.dynamic_state_for_test()).map(|s|s.particle_snapshot()),bp.runtime_accessory.as_ref().and_then(|r|r.dynamic_state_for_test()).map(|s|s.particle_snapshot()),"pair={} frame={i} part={part_index}",pair.id);
                    }
                }
                frames+=1;
            }
        }
        assert_eq!(frames,64);eprintln!("PARALLEL APNG frames={frames} exact_bones=true exact_private_particles=true serial_mean_ms={} parallel_mean_ms={}",serial_ms/measured as f64,parallel_ms/measured as f64);
    }
    #[test]
    #[ignore="installed native pairing provenance"]
    fn installed_pairs_have_exact_role_controllers() {
        let game=&crate::test_support::game_path("");let records=pairs(game);
        assert!(!records.is_empty());
        for pair in &records {
            assert!(!crate::anim::clip_names_for_source(game,&pair.female_source).is_empty(),"female {} {}",pair.category,pair.id);
            assert!(!crate::anim::clip_names_for_source(game,&pair.male_source).is_empty(),"male {} {}",pair.category,pair.id);
        }
        let out=Path::new("../../Saved/Tests/HPlayback335/NativePairs.json");
        std::fs::write(out,serde_json::to_vec_pretty(&records).unwrap()).unwrap();
        eprintln!("NATIVE PAIRS {}",records.len());
    }
    #[test]
    #[ignore="installed native shared factory config"]
    fn native_shared_factory_has_frame_correct_binding_order() {
        let config=crate::unity::parse_prefab_ik(&crate::test_support::game_path("")).unwrap();
        assert_eq!(config.frame_bones.len(),24);
        assert!(config.frame_bones.iter().all(Option::is_some));
        assert_eq!(config.frame_bones[0].as_deref(),Some("cf_j_hips"));
        eprintln!("native frame bones {:?}",config.frame_bones);
    }
}
