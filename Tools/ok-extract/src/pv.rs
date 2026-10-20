//! Studio.Preparation's serialized PVCopy references and OCIChar gates.
use crate::pose::Skeleton;

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(crate) struct Config {pub pairs:Vec<[String;2]>}

/// Serialized paths are relative to the body prefab, rather than names from
/// PVCopy.Reset (an editor-only helper). Duplicate suffixes are not guessed.
pub(crate) fn resolve(sk:&Skeleton,path:&str)->Option<i64> {
    let wanted:Vec<_>=path.split('/').collect();let mut matches=Vec::new();
    for (&id,_) in &sk.tfs {
        let mut current=Some(id);let mut valid=true;
        for name in wanted.iter().rev() {
            let Some(tf)=current.and_then(|id|sk.tfs.get(&id)) else {valid=false;break;};
            if tf.name!=*name {valid=false;break;}current=tf.parent;
        }
        if valid {matches.push(id);}
    }
    if matches.len()==1 {Some(matches[0])} else {None}
}

impl Config {
    pub fn apply(&self,sk:&mut Skeleton,mask:u8,fk:bool)->usize {
        if fk {return 0;}
        let mut copied=0;
        // Follow serialized order. A source can refer to an earlier destination.
        for (index,pair) in self.pairs.iter().enumerate() {
            if index>=4||mask&(1<<index)==0 {continue;}
            let (Some(src),Some(dst))=(resolve(sk,&pair[0]),resolve(sk,&pair[1])) else {continue;};
            let source=sk.tfs[&src].clone();
            // Source setter order matters when a mod places PV below its target.
            sk.tfs.get_mut(&dst).unwrap().scale=source.scale;
            let position=crate::pose::mat_pos(&crate::pose::world_of_id(src,&sk.tfs));
            let rotation=crate::pose::world_rotation_of(src,&sk.tfs);
            let parent=sk.tfs[&dst].parent.filter(|id|sk.tfs.contains_key(id));
            let local=if let Some(parent)=parent {
                let Some(inv)=crate::pose::mat_inverse_checked(&crate::pose::world_of_id(parent,&sk.tfs)) else {continue;};
                let pr=crate::pose::world_rotation_of(parent,&sk.tfs);
                (crate::pose::mat_vec(&inv,position),crate::pose::quat_mul([-pr[0],-pr[1],-pr[2],pr[3]],rotation))
            } else {(position,rotation)};
            // CopyPosRotScl: localScale, world position, world rotation.
            let tf=sk.tfs.get_mut(&dst).unwrap();tf.scale=source.scale;tf.pos=local.0;tf.rot=local.1;copied+=1;
        }
        sk.rebuild_worlds();copied
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::{HashMap,HashSet};
    fn vec(v:&serde_json::Value)->[f32;3] {std::array::from_fn(|i|v[["x","y","z"][i]].as_f64().unwrap() as f32)}
    fn quat(v:&serde_json::Value)->[f32;4] {std::array::from_fn(|i|v[["x","y","z","w"][i]].as_f64().unwrap() as f32)}
    #[test]
    #[ignore="five installed cards, actual compiled Studio PVCopy callback fixtures"]
    fn native_pv_copy_actual_callback() {
        let config=crate::unity::parse_bundle_cached(crate::gamefs::read(crate::test_support::game_path("/abdata/studio/base/00.unity3d")).unwrap()).studio_pv_config("p_cf_body_bone").unwrap();
        let mut frames=0;let mut copies=0;let mut held=0;let mut positive=0;let mut max=0f32;
        for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
            let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
            let input:Vec<serde_json::Value>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Studio295/{stem}Studio295.json.pv.json")).unwrap()).unwrap();
            assert_eq!(input.len(),15);let mut cardmax=0f32;
            for f in &input {
                assert!(f["callback"].as_str().unwrap().contains("<Start>"));
                let mut needed=HashSet::new();
                for field in ["pv","bone"] {for p in f[field].as_array().unwrap() {
                    let mut path=p.as_str().unwrap();loop {needed.insert(path.to_string());let Some((parent,_))=path.rsplit_once('/') else {break;};path=parent;}
                }}
                let all:HashMap<_,_>=f["before"].as_array().unwrap().iter().map(|b|(b["path"].as_str().unwrap().to_owned(),b)).collect();
                let mut paths:Vec<_>=needed.into_iter().collect();paths.sort();
                let ids:HashMap<_,_>=paths.iter().enumerate().map(|(i,p)|(p.clone(),i as i64+1)).collect();
                let tfs=paths.iter().map(|p| {let b=all.get(p).unwrap_or_else(||panic!("missing ancestor {p}"));
                    (ids[p],crate::pose::Tf{name:b["name"].as_str().unwrap().into(),parent:p.rsplit_once('/').map(|(p,_)|ids[p]),sibling_index:None,
                        pos:vec(&b["position"]),rot:quat(&b["rotation"]),scale:vec(&b["scale"])})}).collect();
                let mut sk=Skeleton{names:Vec::new(),worlds:Vec::new(),rest:Vec::new(),smr_count:0,tfs,bone_ids:Vec::new(),source_mesh:0};
                let old=sk.clone();let flags:Vec<_>=f["enabled"].as_array().unwrap().iter().map(|v|v.as_bool().unwrap()).collect();
                let mask=flags.iter().enumerate().fold(0u8,|m,(i,v)|m|if *v {1<<i}else{0});
                assert_eq!(config.apply(&mut sk,mask,false),flags.iter().filter(|v|**v).count());
                for k in 0..4 {
                    assert!(f["pv"][k].as_str().unwrap().ends_with(&config.pairs[k][0]));
                    let id=resolve(&sk,&config.pairs[k][1]).unwrap();assert_eq!(ids[f["bone"][k].as_str().unwrap()],id);
                    let tf=&sk.tfs[&id];let expected=&f["after"][k];
                    let p=vec(&expected["position"]);let s=vec(&expected["scale"]);let q=quat(&expected["rotation"]);
                    for i in 0..3 {cardmax=cardmax.max((tf.pos[i]-p[i]).abs()).max((tf.scale[i]-s[i]).abs());}
                    cardmax=cardmax.max((0..4).map(|i|(tf.rot[i]-q[i]).abs()).fold(0f32,f32::max).min((0..4).map(|i|(tf.rot[i]+q[i]).abs()).fold(0f32,f32::max)));
                    let world=crate::pose::mat_pos(&crate::pose::world_of_id(id,&sk.tfs));let wp=vec(&expected["worldPosition"]);
                    for i in 0..3 {cardmax=cardmax.max((world[i]-wp[i]).abs());}
                    assert!(cardmax<2e-5,"{card} stage={} sample={} pair={k} error={cardmax}",f["stage"],f["sample"]);
                    if flags[k] {copies+=1;let before=&old.tfs[&id];assert!(before.pos.iter().zip(tf.pos).chain(before.scale.iter().zip(tf.scale)).any(|(a,b)|(a-b).abs()>1e-4));positive+=1;}
                    else {held+=1;assert_eq!(tf.pos,old.tfs[&id].pos);assert_eq!(tf.rot,old.tfs[&id].rot);assert_eq!(tf.scale,old.tfs[&id].scale);}
                }
                // Same engine-independent binding works with arbitrarily named mod bones.
                let mut renamed=old.clone();let mut custom=config.clone();
                let mut names=HashMap::new();for (&id,tf) in &old.tfs {names.insert(tf.name.clone(),format!("mod_node_{id}"));}
                for tf in renamed.tfs.values_mut() {tf.name=names[&tf.name].clone();}
                for pair in &mut custom.pairs {for path in pair { *path=path.split('/').map(|n|names[n].clone()).collect::<Vec<_>>().join("/");}}
                assert_eq!(custom.apply(&mut renamed,mask,false),flags.iter().filter(|v|**v).count());
                assert!(renamed.tfs.iter().all(|(id,t)|t.pos==sk.tfs[id].pos&&t.rot==sk.tfs[id].rot&&t.scale==sk.tfs[id].scale));
                frames+=1;
            }
            max=max.max(cardmax);eprintln!("PV native card={card} frames=15 max={cardmax}");
        }
        eprintln!("PV native accepted frames={frames} copied={copies} held={held} positive={positive} max={max}");
    }
}
