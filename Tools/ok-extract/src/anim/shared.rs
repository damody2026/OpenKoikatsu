//! Authored clip resources, independent of character geometry and game files.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use super::*;
use bincode::Options;
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;

const MAX_BYTES: u64 = 128 * 1024 * 1024;
#[derive(serde::Serialize, serde::Deserialize)]
struct Resource {
    magic: [u8; 8],
    version: u32,
    clip: CachedClip,
    tos: BTreeMap<u32, String>,
}
static AVATARS: Mutex<Option<HashMap<PathBuf, std::sync::Arc<HashMap<u32, String>>>>> = Mutex::new(None);
pub(crate) fn is_shared_root(root:&Path)->bool { AVATARS.lock().ok().is_some_and(|a|a.as_ref().is_some_and(|m|m.contains_key(root))) }
pub(super) fn avatar_tos(root: &Path) -> Option<std::sync::Arc<HashMap<u32, String>>> {
    AVATARS.lock().ok()?.as_ref()?.get(root).cloned()
}
fn validate(r: &Resource) -> Result<(), String> {
    let c=&r.clip;
    if r.magic!=*b"OKCLIP01" || r.version!=1 { return Err("unsupported shared animation resource".into()); }
    if c.binds.is_empty() || c.binds.len()>65536 || r.tos.is_empty() || r.tos.len()>65536
        || !c.start.is_finite() || !c.duration.is_finite() || c.duration<=0. || c.duration>86400.
        || c.state_timing.is_some() { return Err("invalid raw clip metadata".into()); }
    let curves=c.binds.iter().map(curve_dim).sum::<usize>();
    if c.streamed_n as usize+c.dense.count as usize+c.constant.len()!=curves
        || c.dense.frames.checked_mul(c.dense.count as usize).is_none_or(|n| n>c.dense.samples.len())
        || !c.dense.rate.is_finite() || !c.dense.begin.is_finite()
        || (c.dense.frames>1 && c.dense.rate<=0.)
        || c.dense.samples.iter().chain(&c.constant).chain(&c.reference).any(|v|!v.is_finite())
        || c.keys.iter().any(|k|!k.time.is_finite() || k.index<0 || k.index as u32>=c.streamed_n || !k.coeff[3].is_finite() || k.coeff.iter().any(|v|v.is_nan()))
        || c.keys.windows(2).any(|k|k[0].time>k[1].time)
        || r.tos.values().any(|s|s.is_empty() || s.len()>4096 || s.contains('\0')) {
        return Err("invalid shared animation curves".into());
    }
    // Infinite derivatives are Unity's stepped-key encoding; sample_cached
    // handles them. Their value must still be finite.
    Ok(())
}
pub(crate) fn export_shared_clip(root: &Path, key: &str) -> Result<Vec<u8>, String> {
    // A Studio controller state that plays one clip (studio_anims.json keys): that clip. Blend trees and layered
    // states are not one clip and stay refused.
    if key.starts_with(controller::SOURCE_PREFIX) {
        let leaves=super::state_leaf_clips(root,key).ok_or("unknown controller state")?;
        // The game's height variants of one motion (…_S, …_M, …_L, picked by her height): the middle one.
        let raw=if leaves.len()==1 {leaves.into_iter().next().unwrap()}
            else if let Some(m)=leaves.iter().find(|k|k.ends_with("_M")||k.contains("_M_")) {m.clone()}
            else {return Err(format!("controller state plays {} clips, not one",leaves.len()));};
        if raw.starts_with(controller::SOURCE_PREFIX) {return Err("controller state resolves to another controller".into());}
        return export_shared_clip(root,&raw);
    }
    if !key.contains('|') {
        return Err("shared export requires an explicitly qualified raw AnimationClip; controller graphs are not raw clips".into());
    }
    load_clip_at(root,key,0.).ok_or("cannot load authored AnimationClip")?;
    let clip=CLIP_CACHE.lock().map_err(|_|"clip cache unavailable")?.as_ref()
        .and_then(|m|m.get(&format!("{}\0{}",root.display(),key))).cloned().ok_or("clip not cached")?;
    let r=Resource {magic:*b"OKCLIP01",version:1,clip:(*clip).clone(),tos:load_avatar_tos(root).into_iter().collect()};
    validate(&r)?;
    let bytes=bincode::serialize(&r).map_err(|e|e.to_string())?;
    if bytes.len() as u64>MAX_BYTES {return Err("shared animation exceeds resource limit".into());}
    Ok(bytes)
}
pub(crate) fn install_shared_clip(bytes: &[u8]) -> Result<(PathBuf,String),String> {
    if bytes.is_empty() || bytes.len() as u64>MAX_BYTES {return Err("invalid shared resource size".into());}
    let root=PathBuf::from(format!("__ok_shared/{:x}",Sha256::digest(bytes)));
    let key="memory|authored".to_string();
    // Only validated immutable curves enter both caches. Runtime preparation
    // and live selection share them without decoding the resource again.
    if is_shared_root(&root) && CLIP_CACHE.lock().map_err(|_|"clip cache unavailable")?.as_ref()
        .is_some_and(|m|m.contains_key(&format!("{}\0{}",root.display(),key))) {return Ok((root,key));}
    let mut r:Resource=bincode::DefaultOptions::new().with_fixint_encoding().with_limit(MAX_BYTES)
        .reject_trailing_bytes().deserialize(bytes).map_err(|e|e.to_string())?;
    validate(&r)?;
    r.clip.curve_index=streamed_curve_index(&r.clip.keys);
    // Per-resource TOS avoids cross-card/source contamination of the original
    // process-global Avatar cache. No abdata lookup is necessary after install.
    AVATARS.lock().map_err(|_|"avatar cache unavailable")?.get_or_insert_with(HashMap::new)
        .insert(root.clone(),std::sync::Arc::new(r.tos.into_iter().collect()));
    CLIP_CACHE.lock().map_err(|_|"clip cache unavailable")?.get_or_insert_with(HashMap::new)
        .insert(format!("{}\0{}",root.display(),key),Arc::new(r.clip));
    Ok((root,key))
}
/// Installs authored curves under the session's own root instead of a
/// synthetic shared root, so game sessions keep resolving bundles (outfits,
/// later native clips) while playing an external clip. Bindings must use the
/// rig's real Unity paths (`cf_j_root/...`); the avatar/rig resolves them.
pub(crate) fn install_clip_for_root(root: &Path, bytes: &[u8]) -> Result<String,String> {
    if bytes.is_empty() || bytes.len() as u64>MAX_BYTES {return Err("invalid shared resource size".into());}
    let key=format!("video|{:x}",Sha256::digest(bytes));
    let cache_key=format!("{}\0{}",root.display(),key);
    if CLIP_CACHE.lock().map_err(|_|"clip cache unavailable")?.as_ref().is_some_and(|m|m.contains_key(&cache_key)) {return Ok(key);}
    let mut r:Resource=bincode::DefaultOptions::new().with_fixint_encoding().with_limit(MAX_BYTES)
        .reject_trailing_bytes().deserialize(bytes).map_err(|e|e.to_string())?;
    validate(&r)?;
    r.clip.curve_index=streamed_curve_index(&r.clip.keys);
    CLIP_CACHE.lock().map_err(|_|"clip cache unavailable")?.get_or_insert_with(HashMap::new)
        .insert(cache_key,Arc::new(r.clip));
    Ok(key)
}
/// Bone tracks of an OKCLIP01 resource for offline training (video_motion::train):
/// bone name → attribute → frames × values, sampled with the runtime sampler at
/// `fps` from the clip start. Euler channels (attribute 4, Unity ZXY degrees)
/// become quaternions (attribute 2, x y z w).
pub(crate) fn resource_tracks(bytes: &[u8], fps: f32) -> Result<BTreeMap<String, BTreeMap<u32, Vec<Vec<f32>>>>, String> {
    if bytes.is_empty() || bytes.len() as u64>MAX_BYTES {return Err("invalid shared resource size".into());}
    let mut r:Resource=bincode::DefaultOptions::new().with_fixint_encoding().with_limit(MAX_BYTES)
        .reject_trailing_bytes().deserialize(bytes).map_err(|e|e.to_string())?;
    validate(&r)?;
    r.clip.curve_index=streamed_curve_index(&r.clip.keys);
    let mut out:BTreeMap<String,BTreeMap<u32,Vec<Vec<f32>>>>=BTreeMap::new();
    let n=((r.clip.duration.max(1e-3))*fps).ceil().max(1.) as usize;
    for k in 0..n {
        let (binds,values,..)=sample_cached(&r.clip,k as f32/fps);
        let mut cur=0i32;
        for b in &binds {
            let dim=curve_dim(b);
            if let (Some(path),true)=(r.tos.get(&b.path),(1..=4).contains(&b.attribute)) {
                let name=path.rsplit('/').next().unwrap_or(path).to_string();
                let mut v:Vec<f32>=(0..dim as i32).map(|i|values.get(&(cur+i)).copied().unwrap_or(0.)).collect();
                let mut attr=b.attribute;
                if attr==4 && v.len()==3 {
                    // Unity applies Z, then X, then Y: q = qy·qx·qz
                    let h=|d:f32|(d.to_radians()/2.).sin_cos();
                    let ((sx,cx),(sy,cy),(sz,cz))=(h(v[0]),h(v[1]),h(v[2]));
                    v=vec![cy*sx*cz+sy*cx*sz, sy*cx*cz-cy*sx*sz, cy*cx*sz-sy*sx*cz, cy*cx*cz+sy*sx*sz];
                    attr=2;
                }
                out.entry(name).or_default().entry(attr).or_default().push(v);
            }
            cur+=dim as i32;
        }
    }
    Ok(out)
}
/// Dense authored clip from absolute Unity-local channels (video motion).
/// `binds`: (animator-relative path, attribute 1 pos / 2 quat / 3 scale);
/// `samples[frame * count + curve]` in binding order. Same validation as
/// installed resources.
pub(crate) fn build_dense_clip(binds:&[(String,u32)], frames:usize, fps:f32, samples:Vec<f32>) -> Result<Vec<u8>,String> {
    let mut tos=BTreeMap::new();let mut out=Vec::with_capacity(binds.len());
    for (path,attribute) in binds {
        let hash=super::unity_string_to_hash(path) as u32;
        if tos.get(&hash).is_some_and(|p:&String|p!=path){return Err(format!("path hash collision: {path}"));}
        tos.insert(hash,path.clone());out.push(Binding{path:hash,attribute:*attribute,type_id:4});
    }
    let count=out.iter().map(curve_dim).sum::<usize>();
    if frames==0 || samples.len()!=frames*count {return Err("dense sample count mismatch".into());}
    let duration=if frames>1 {(frames-1) as f32/fps} else {1./fps};
    let clip=CachedClip{binds:out,keys:vec![],curve_index:None,streamed_n:0,
        dense:DenseSamples{count:count as u32,frames,rate:fps,begin:0.,samples},constant:vec![],reference:vec![],
        // Studio, like native Studio clips: enables ExpressionBone (elbow,
        // forearm, wrist and knee correction bones computed at runtime) and
        // PVCopy, so the clip must key cf_pv_hand/leg (video_motion does).
        start:0.,duration,scene:ClipScene::Studio,state_timing:None};
    let r=Resource{magic:*b"OKCLIP01",version:1,clip,tos};
    validate(&r)?;
    let bytes=bincode::serialize(&r).map_err(|e|e.to_string())?;
    if bytes.len() as u64>MAX_BYTES {return Err("shared animation exceeds resource limit".into());}
    Ok(bytes)
}
#[cfg(test)] mod tests {
    use super::*;
    fn resource()->Resource {Resource {magic:*b"OKCLIP01",version:1,tos:[(42,"root/bone".into())].into_iter().collect(),clip:CachedClip {
        binds:vec![Binding {path:42,attribute:1,type_id:4}],keys:vec![],curve_index:None,streamed_n:0,
        dense:DenseSamples {count:3,frames:2,rate:1.,begin:0.,samples:vec![0.,1.,2.,3.,4.,5.]},
        constant:vec![],reference:vec![],start:0.,duration:1.,scene:ClipScene::Normal,state_timing:None}}}
    #[test] fn shared_clip_roundtrip_samples_without_game_files() {
        let r=resource();let before=sample_cached(&r.clip,0.35);
        let bytes=bincode::serialize(&r).unwrap();let (root,key)=install_shared_clip(&bytes).unwrap();
        assert!(!root.ok_exists());assert_eq!(load_clip_at(&root,&key,0.35).unwrap().1,before.1);
        assert_eq!(load_avatar_tos(&root).get(&42).unwrap(),"root/bone");
        assert_eq!(clip_duration(&root,&key),1.);assert_eq!(clip_limb_corrections(&root,&key),(false,true));
        assert_eq!(install_shared_clip(&bytes).unwrap(),(root,key));
    }
    #[test] fn immutable_clip_snapshots_sample_concurrently_during_replacement() {
        let key="immutable-clip-concurrency-fixture".to_string();
        let first=Arc::new(resource().clip);
        CLIP_CACHE.lock().unwrap().get_or_insert_with(HashMap::new).insert(key.clone(),first.clone());
        let snapshot=cached_clip(&key).unwrap();
        assert!(Arc::ptr_eq(&first,&snapshot));
        let expected=sample_cached(&snapshot,0.35).1;
        let mut replacement=(*first).clone();
        replacement.dense.samples.iter_mut().for_each(|v|*v+=10.);
        let replacement=Arc::new(replacement);
        // Readers retain an immutable owned generation while the catalogue is
        // replaced and held locked. Their numerical sampling needs no lock.
        let mut guard=CLIP_CACHE.lock().unwrap();
        guard.as_mut().unwrap().insert(key.clone(),replacement.clone());
        std::thread::scope(|scope| {
            let mut workers=Vec::new();
            for _ in 0..4 {let c=snapshot.clone();let oracle=expected.clone();
                workers.push(scope.spawn(move||for _ in 0..128 {assert_eq!(sample_cached(&c,0.35).1,oracle);}));}
            for worker in workers {worker.join().unwrap();}
        });
        drop(guard);
        assert_eq!(sample_cached(&snapshot,0.35).1,expected);
        assert_ne!(sample_cached(&cached_clip(&key).unwrap(),0.35).1,expected);
        CLIP_CACHE.lock().unwrap().as_mut().unwrap().remove(&key);
    }
    #[test] fn shared_clip_rejects_corrupt_data_before_cache_install() {
        let mut bytes=bincode::serialize(&resource()).unwrap();bytes.push(0);assert!(install_shared_clip(&bytes).is_err());
        let mut r=resource();r.clip.dense.samples[0]=f32::NAN;assert!(install_shared_clip(&bincode::serialize(&r).unwrap()).is_err());
        r=resource();r.clip.dense.frames=usize::MAX;assert!(install_shared_clip(&bincode::serialize(&r).unwrap()).is_err());
        r=resource();r.clip.dense=DenseSamples{count:0,frames:0,rate:0.,begin:0.,samples:vec![]};r.clip.streamed_n=3;
        r.clip.keys=vec![StreamedKey{time:0.,index:0,coeff:[f32::NAN,0.,0.,1.]}];
        assert!(install_shared_clip(&bincode::serialize(&r).unwrap()).is_err());
        r.clip.keys[0].coeff[0]=f32::INFINITY;assert!(validate(&r).is_ok(),"Unity stepped-key derivatives remain supported");
        r=resource();r.version=2;assert!(install_shared_clip(&bincode::serialize(&r).unwrap()).is_err());
    }
    #[test]
    #[ignore = "explicit source root; read-only authored curve oracle"]
    fn shared_clip_matches_original_source() {
        let root=PathBuf::from(std::env::var("OK_TEST_SHARED_ROOT").unwrap());
        for key in ["studio/anime/00.unity3d|f_aruki_00_00_01","studio/anime/00.unity3d|mc_m_walk_00_00_01"] {
            let bytes=export_shared_clip(&root,key).unwrap();
            let (detached,name)=install_shared_clip(&bytes).unwrap();
            assert!(!detached.ok_exists());
            assert_eq!(clip_duration(&root,key),clip_duration(&detached,&name));
            for time in [0.,0.013,0.5,1.01,2.,8.] {
                let a=load_clip_at(&root,key,time).unwrap();let b=load_clip_at(&detached,&name,time).unwrap();
                assert_eq!(a.1,b.1,"shared curve sample differs: {key} at {time}");
            }
        }
    }

    #[test]
    #[ignore = "read-only source card/curve trajectory diagnostic"]
    fn watermelon_source_trajectory() {
        let game=&crate::test_support::game_path("");
        std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
        std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
        let card=game.join("UserData/chara/female/Alice_keiyuu.png");
        let mut session=crate::session::open(game,Some(&card),0).unwrap();
        let body=session.shape_body.clone();let face=session.shape_face.clone();session.apply_shape(&body,&face);
        let base=session.body_shaped.as_ref().unwrap();let mut rows=Vec::new();
        for name in ["ks_f_tachi_04_M","ks_f_tachi_05_M"] {
            let key=format!("action/animator/00.unity3d|{name}");
            let bytes=export_shared_clip(game,&key).unwrap();let (root,shared)=install_shared_clip(&bytes).unwrap();
            let duration=clip_duration(game,&key);
            if name=="ks_f_tachi_04_M" {
                let json=crate::gamefs::read("../../Content/IndependentResources/RuntimeAnimations/watermelon.contact.json").unwrap();
                let recipe=crate::point_contact::Recipe::parse(&json).unwrap();
                let mut contact=base.clone();apply_clip_at(game,&mut contact,&key,duration*recipe.peak);
                assert!(recipe.apply(&mut contact,recipe.peak));
                let point=crate::pose::mat_vec(&contact.world_tf("k_f_handR_00").unwrap(),[0.,0.,0.5099748]);
                let error=crate::finalik::vlen(crate::finalik::vsub(point,recipe.constraints[0].target));
                eprintln!("Watermelon point contact error_m={error} point={point:?}");assert!(error<0.11);
            }
            for frame in 0..=(duration*30.).ceil() as usize {
                let t=(frame as f32/30.).min(duration-0.0001);
                let mut source=base.clone();let mut detached=base.clone();
                apply_clip_at(game,&mut source,&key,t);apply_clip_at(&root,&mut detached,&shared,t);
                for bone in ["k_f_handR_00","cf_j_hand_R","cf_j_spine03"] {
                    let a=source.world_tf(bone).unwrap();let b=detached.world_tf(bone).unwrap();
                    assert!(a.iter().flatten().zip(b.iter().flatten()).all(|(x,y)|(x-y).abs()<0.00001),"shared differs {name} {t} {bone}");
                    rows.push(serde_json::json!({"clip":name,"time":t,"bone":bone,"matrix":a}));
                }
            }
        }
        std::fs::write("../../Saved/StreetSocial/watermelon.source.trajectory.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }
}
