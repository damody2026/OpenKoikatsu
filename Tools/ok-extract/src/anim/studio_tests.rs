use super::*;
use serde::Deserialize;

#[test]
fn generic_transform_paths_distinguish_duplicate_leaf_names() {
    use crate::pose::{Skeleton,Tf};
    let mut pose=Skeleton{names:Vec::new(),worlds:Vec::new(),rest:Vec::new(),smr_count:0,tfs:HashMap::new(),bone_ids:vec![1,2,3,4,5],source_mesh:0};
    for (id,name,parent) in [(1,"cf_j_root",None),(2,"left",Some(1)),(3,"tip",Some(2)),(4,"right",Some(1)),(5,"tip",Some(4))] {
        pose.tfs.insert(id,Tf{name:name.into(),parent,sibling_index:None,pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]});
    }
    let bindings=vec![Binding{path:unity_string_to_hash("cf_j_root/left/tip") as u32,attribute:1,type_id:4},
        Binding{path:unity_string_to_hash("cf_j_root/right/tip") as u32,attribute:1,type_id:4}];
    let values=HashMap::from([(0,1.),(1,2.),(2,3.),(3,4.),(4,5.),(5,6.)]);
    apply_binding_values(Path::new("missing-game-fixture"),&mut pose,"generic fixture",bindings.clone(),values.clone(),0,0,6);
    assert_eq!(pose.tfs[&3].pos,[1.,2.,3.]);assert_eq!(pose.tfs[&5].pos,[4.,5.,6.]);
    let mut duplicate=pose.tfs[&5].clone();duplicate.pos=[7.,8.,9.];pose.tfs.insert(6,duplicate);pose.bone_ids.push(6);
    pose.tfs.get_mut(&5).unwrap().pos=[10.,11.,12.];
    apply_binding_values(Path::new("missing-game-fixture"),&mut pose,"ambiguous fixture",bindings,values,0,0,6);
    assert_eq!(pose.tfs[&5].pos,[10.,11.,12.]);assert_eq!(pose.tfs[&6].pos,[7.,8.,9.]);
}

#[derive(Deserialize)] struct V3{x:f32,y:f32,z:f32}
#[derive(Deserialize)] struct Q4{x:f32,y:f32,z:f32,w:f32}
#[derive(Deserialize)] struct Bone{name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)] struct Float{name:String,value:f32}
#[derive(Deserialize)] struct NativeClip{name:String,weight:f32,length:f32}
#[derive(Deserialize)] struct Frame {
    source:String,owner:String,#[serde(rename="overlaySource")]over_source:String,#[serde(rename="overlayOwner")]over_owner:String,
    state:String,layer:usize,#[serde(rename="layerCount")]layer_count:usize,
    #[serde(rename="baseState")]base_state:String,#[serde(rename="layerState")]layer_state:String,
    #[serde(rename="isH")]is_h:bool,#[serde(rename="isMotion")]is_motion:bool,
    height:f32,breast:f32,motion:f32,elapsed:f32,length:f32,
    #[serde(rename="baseLength")]base_length:f32,parameters:Vec<Float>,clips:Vec<NativeClip>,
    #[serde(rename="baseClips")]base_clips:Vec<NativeClip>,bones:Vec<Bone>,
}

fn source_key(f:&Frame,overlay:bool)->String {
    let data=[f.source.as_str(),f.owner.as_str(),if overlay{f.over_source.as_str()}else{""},if overlay{f.over_owner.as_str()}else{""}];
    format!("studio@{}",serde_json::to_vec(&data).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>())
}
fn selected_key(game:&Path,f:&Frame,overlay:bool)->String {
    let source=source_key(f,overlay);let prefix=format!("{} / [Studio layer {}] ",f.owner,f.layer);
    let labels:Vec<_>=clip_names_for_source(game,&format!("states:{source}")).into_iter().filter(|s|
        s.strip_prefix(&prefix).is_some_and(|p|p==f.state||p.ends_with(&format!(".{}",f.state)))).collect();
    assert_eq!(labels.len(),1,"Studio graph identity {} {} layer={} overlay={overlay}",f.source,f.owner,f.layer);
    format!("states:{source}|{}",labels[0])
}

#[test]
#[ignore="actual Studio Animator PV transforms; includes bindings absent from Avatar TOS"]
fn native_studio_pv_animation_transforms() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");
    let cards=if std::env::var("OK_TEST_STUDIO_MARCH").ok().as_deref()==Some("1") {vec!["[Card] March_7th_keiyuu"]}
        else{vec!["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"]};
    let mut samples=0;let mut errors=[0f32;3];
    for card in cards {
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
        let native:Vec<Frame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Studio293/{stem}Studio293.json.studio.json")).unwrap()).unwrap();
        let mut session=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
        session.apply_anim_frame(0.,0.);
        let baseline=session.body_shaped.as_ref().unwrap();let config=session.pv_config.as_ref().unwrap();
        for f in native {
            let key=selected_key(game,&f,true);
            let p=studio_parameters(game,&key,session.shape_body[0],session.shape_body[4],f.is_h,[f.height,f.breast],f.is_motion.then_some(f.motion)).unwrap();
            let mut pose=baseline.clone();apply_clip_at_parameters(game,&mut pose,&key,f.elapsed,&p);
            for pair in &config.pairs {
                let id=crate::pv::resolve(&pose,&pair[0]).unwrap();let a=&pose.tfs[&id];let b=f.bones.iter().find(|b|b.name==a.name).unwrap();
                let pos=[b.position.x,b.position.y,b.position.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];let scl=[b.scale.x,b.scale.y,b.scale.z];
                for i in 0..3 {errors[0]=errors[0].max((a.pos[i]-pos[i]).abs());errors[2]=errors[2].max((a.scale[i]-scl[i]).abs());}
                let same=(0..4).map(|i|(a.rot[i]-q[i]).abs()).fold(0f32,f32::max);
                let opposite=(0..4).map(|i|(a.rot[i]+q[i]).abs()).fold(0f32,f32::max);errors[1]=errors[1].max(same.min(opposite));
                assert!(errors.iter().all(|v|*v<2e-5),"PV Animator {card} {} time={} bone={} errors={errors:?} actual={:?} native={pos:?}",f.owner,f.elapsed,a.name,a.pos);
                samples+=1;
            }
        }
    }
    eprintln!("Native Studio PV Animator accepted samples={samples} errors={errors:?}");
}

#[test]
#[ignore="installed Studio controller; synchronized catalogue boundary"]
fn synchronized_states_require_studio_composition() {
    let game=&crate::test_support::game_path("");let source="states:studio/anime/00.unity3d";
    let labels=clip_names_for_source(game,source);let all=controller::entries(&source_path(game,source.strip_prefix("states:").unwrap()).unwrap());
    let hidden:Vec<_>=all.iter().filter(|e|e.layer.as_ref().is_some_and(|l|l.synchronized)).collect();assert!(!hidden.is_empty());
    for e in hidden {assert!(!labels.contains(&e.label));assert!(!selectable_clip(game,&format!("{source}|{}",e.label)));}
    for (i,label) in labels.iter().enumerate() {assert_eq!(clip_name_for_source_at(game,source,i).as_ref(),Some(label));}
    assert!(clip_name_for_source_at(game,source,labels.len()).is_none());
    let payload=["studio/anime/00.unity3d","walk_run","",""];
    let studio=format!("studio@{}",serde_json::to_vec(&payload).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>());
    let resolved=source_path(game,&studio).unwrap();assert_eq!(scene_for_source(&resolved),ClipScene::Studio);
    assert!(!clip_names_for_source(game,&format!("states:{studio}")).is_empty());
    eprintln!("Studio synchronized raw boundary labels={} index_identity=1 missing_raw_rejected=1",labels.len());
}

#[test]
#[ignore="actual five-card OCIChar.LoadAnime fixtures"]
fn native_studio_load_graph_and_options() {
    let game=&crate::test_support::game_path("");let tos=load_avatar_tos(game);
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");
    let cards=if std::env::var("OK_TEST_STUDIO_MARCH").ok().as_deref()==Some("1") {vec!["[Card] March_7th_keiyuu"]}
        else{vec!["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"]};
    let mut frames=0;let mut attrs_count=0;let mut errors=[0f32;3];let mut negative=0;
    for card in &cards {
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
        let native:Vec<Frame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Studio293/{stem}Studio293.json.studio.json")).unwrap()).unwrap();
        assert_eq!(native.len(),27);
        let mut session=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
        session.apply_anim_frame(0.,0.);let baseline=session.body_shaped.as_ref().unwrap();
        for f in native {
            let key=selected_key(game,&f,true);
            let p=studio_parameters(game,&key,session.shape_body[0],session.shape_body[4],f.is_h,[f.height,f.breast],f.is_motion.then_some(f.motion)).unwrap();
            for (name,value) in &p {let n=f.parameters.iter().find(|n|&n.name==name).unwrap();assert!((n.value-value).abs()<1e-6,"OCIChar final option {name}");}
            let (source,entry,leaves,_)=motion_plan(game,&key,&p).unwrap();
            assert_eq!(entry.full_path_id.to_string(),f.layer_state);
            assert!((clip_duration_parameters(game,&key,&p)-f.length).abs()<1e-4,"Studio layer duration");
            if f.layer>0 {assert!(f.layer_count>f.layer);assert!(entry.layer.is_some());}
            let mut attrs=HashSet::new();
            let mut inspect=|leaves:Vec<(ClipEntry,f32,f32,f32)>,clips:&Vec<NativeClip>| {
                let mut weights=HashMap::<String,f32>::new();
                for(c,w,_,len) in leaves {
                    *weights.entry(c.name.clone()).or_default()+=w;
                    if let Some(n)=clips.iter().find(|n|n.name==c.name) {assert!((len-n.length).abs()<1e-5);}
                    for b in load_clip_at(game,&format!("{}|{}",c.source.as_deref().unwrap_or(&source),c.label),0.).unwrap().0 {
                        if (1..=4).contains(&b.attribute) {if let Some(path)=tos.get(&b.path) {attrs.insert((bone_name(path).to_string(),b.attribute));}}
                    }
                }
                for n in clips {assert!((weights.get(&n.name).copied().unwrap_or(0.)-n.weight).abs()<1e-5,"Studio native clip weight {}",n.name);}
            };
            inspect(leaves,&f.clips);
            if let Some(l)=&entry.layer {
                let bk=format!("states:{source}|{}",l.base_label);let (_,base,leaves,_)=motion_plan(game,&bk,&p).unwrap();
                assert!((clip_duration_parameters(game,&bk,&p)-f.base_length).abs()<1e-4);
                assert_eq!(base.label.split_once("] ").map(|(_,s)|unity_string_to_hash(s).to_string()).unwrap(),f.base_state);
                inspect(leaves,&f.base_clips);
                // Native's selected-layer hash must be the original base graph's
                // same short-name state, not the one-layer override controller.
                let resolved=source_path(game,&f.source).unwrap();
                let original=controller::entries(&resolved);let matching=original.iter().find(|e|
                    e.label.starts_with(&(f.owner.clone()+" / "))&&e.layer.as_ref().is_some_and(|l|l.index==f.layer)
                    &&e.full_path_id.to_string()==f.layer_state).unwrap();
                assert_eq!(matching.full_path_id.to_string(),f.layer_state);
            }
            let mut pose=baseline.clone();apply_clip_at_parameters(game,&mut pose,&key,f.elapsed,&p);
            let animator_ids=crate::pose::subtree_ids_pub(crate::pose::animator_root_id(&pose).unwrap(),&pose.tfs);
            if !f.over_owner.is_empty() {
                let old=selected_key(game,&f,false);let mut without=baseline.clone();apply_clip_at_parameters(game,&mut without,&old,f.elapsed,&p);
                if pose.tfs.iter().any(|(id,a)|a.pos.iter().zip(without.tfs[id].pos).chain(a.rot.iter().zip(without.tfs[id].rot)).any(|(a,b)|(a-b).abs()>1e-4)) {negative+=1;}
            }
            for b in &f.bones {
                let Some(a)=animator_ids.iter().filter_map(|id|pose.tfs.get(id)).find(|a|a.name==b.name) else{continue;};
                let pos=[b.position.x,b.position.y,b.position.z];let scale=[b.scale.x,b.scale.y,b.scale.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];
                if attrs.contains(&(b.name.clone(),1)) {for i in 0..3 {errors[0]=errors[0].max((a.pos[i]-pos[i]).abs());}attrs_count+=1;}
                if attrs.contains(&(b.name.clone(),2))||attrs.contains(&(b.name.clone(),4)) {
                    let same=(0..4).map(|i|(a.rot[i]-q[i]).abs()).fold(0f32,f32::max);
                    let opposite=(0..4).map(|i|(a.rot[i]+q[i]).abs()).fold(0f32,f32::max);errors[1]=errors[1].max(same.min(opposite));attrs_count+=1;
                }
                if attrs.contains(&(b.name.clone(),3)) {for i in 0..3 {errors[2]=errors[2].max((a.scale[i]-scale[i]).abs());}attrs_count+=1;}
                assert!(errors.iter().all(|v|*v<2e-5),"Studio native {card} {} layer={} elapsed={} bone={} errors={errors:?} actual_q={:?} native_q={q:?} actual_p={:?} native_p={pos:?}",f.owner,f.layer,f.elapsed,b.name,a.rot,a.pos);
            }
            frames+=1;
        }
        eprintln!("Studio native card={card} frames=27 errors={errors:?}");
    }
    assert!(negative>=cards.len()*3,"missing runtime override must change actual pose");
    eprintln!("Studio native accepted cards={} frames={frames} transform_attributes={attrs_count} override_negative={negative} errors={errors:?}",cards.len());
}
