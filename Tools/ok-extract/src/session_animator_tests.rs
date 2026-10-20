use super::*;
use crate::ffi::OkBodyPose;
use serde::Deserialize;
use std::ffi::CString;

#[test]
#[ignore="five-card native Studio controls; complete Session propagation"]
fn studio_initialization_reaches_session_palette() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut count=0;let mut bones=0;let mut vertices=0;let mut invalid=0;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        let path=game.join(format!("UserData/chara/female/{card}.png"));let mut actual=open(game,Some(&path),0).unwrap();
        actual.apply_anim_frame(0.,0.);actual.ensure_skel_export();let mut expected=SessionBlob::from_session(&actual).into_session();
        expected.apply_anim_frame(0.,0.);expected.ensure_skel_export();
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
        let native:Vec<serde_json::Value>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Studio293/{stem}Studio293.json.studio.json")).unwrap()).unwrap();
        assert_eq!(native.len(),27);let mut max_bone=0f32;let mut max_vertex=0f32;
        for f in native {
            let data=[f["source"].as_str().unwrap(),f["owner"].as_str().unwrap(),f["overlaySource"].as_str().unwrap(),f["overlayOwner"].as_str().unwrap()];
            let source=format!("states:studio@{}",serde_json::to_vec(&data).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>());
            let prefix=format!("{} / [Studio layer {}] ",data[1],f["layer"].as_u64().unwrap());let state=f["state"].as_str().unwrap();
            let candidates:Vec<_>=crate::anim::clip_names_for_source(game,&source).into_iter().filter(|p|p.strip_prefix(&prefix).is_some_and(|p|p==state||p.ends_with(&format!(".{state}")))).collect();
            assert_eq!(candidates.len(),1);let key=format!("{source}|{}",candidates[0]);let ckey=CString::new(key.clone()).unwrap();
            let is_h=f["isH"].as_bool().unwrap() as i32;let is_motion=f["isMotion"].as_bool().unwrap() as i32;
            let height=f["height"].as_f64().unwrap() as f32;let breast=f["breast"].as_f64().unwrap() as f32;let motion=f["motion"].as_f64().unwrap() as f32;
            assert_eq!(unsafe{crate::ffi::set_studio_animation(&mut actual,ckey.as_ptr(),is_h,height,breast,is_motion,motion)},0);
            let saved=actual.anim_parameters.clone();let clip=actual.anim_clip.clone();let time=actual.anim_time;let scene=actual.anim_scene_mode;
            for bad in [f32::NAN,f32::INFINITY,f32::NEG_INFINITY] {
                assert_eq!(unsafe{crate::ffi::set_studio_animation(&mut actual,ckey.as_ptr(),is_h,bad,breast,is_motion,motion)},1);
                assert_eq!(actual.anim_parameters,saved);assert_eq!(actual.anim_clip,clip);assert_eq!(actual.anim_time,time);assert_eq!(actual.anim_scene_mode,scene);invalid+=1;
            }
            assert_eq!(unsafe{crate::ffi::set_anim_clip(&mut expected,ckey.as_ptr())},0);expected.anim_scene_mode=4;
            // Independent input values are taken from the actual OCIChar after
            // LoadAnime, not from the new initialization function.
            let parameters:std::collections::HashMap<String,f32>=f["parameters"].as_array().unwrap().iter()
                .map(|v|(v["name"].as_str().unwrap().into(),v["value"].as_f64().unwrap() as f32)).collect();
            let elapsed=f["elapsed"].as_f64().unwrap() as f32;let mut raw=expected.body_shaped.as_ref().unwrap().clone();
            crate::anim::apply_clip_at_parameters(game,&mut raw,&key,elapsed,&parameters);
            let animator_ids=crate::pose::subtree_ids_pub(crate::pose::animator_root_id(&raw).unwrap(),&raw.tfs);
            let input:Vec<OkBodyPose>=expected.skel_export.as_ref().unwrap().bones.iter().enumerate().filter(|(_,b)|!b.pose_locked)
                .filter_map(|(i,b)|animator_ids.iter().filter_map(|id|raw.tfs.get(id)).find(|tf|tf.name.eq_ignore_ascii_case(&b.name))
                    .map(|tf|OkBodyPose{bone:i as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();
            assert!(expected.apply_external_body_frame(elapsed,1./60.,&input));actual.apply_anim_frame(elapsed,1./60.);
            for(id,a) in &actual.body_skel.as_ref().unwrap().tfs {
                let b=&expected.body_skel.as_ref().unwrap().tfs[id];for i in 0..3{max_bone=max_bone.max((a.pos[i]-b.pos[i]).abs()).max((a.scale[i]-b.scale[i]).abs());}
                let same=(0..4).map(|i|(a.rot[i]-b.rot[i]).abs()).fold(0f32,f32::max);let opposite=(0..4).map(|i|(a.rot[i]+b.rot[i]).abs()).fold(0f32,f32::max);
                max_bone=max_bone.max(same.min(opposite));bones+=1;
            }
            for(a,b) in actual.parts.iter().zip(&expected.parts).filter(|(a,_)|a.runtime_accessory.is_some()) {
                assert_eq!(a.positions.len(),b.positions.len());for(p,q) in a.positions.iter().zip(&b.positions){for i in 0..3{max_vertex=max_vertex.max((p[i]-q[i]).abs());}vertices+=1;}
            }
            assert!(max_bone<2e-5&&max_vertex<2e-5,"Studio Session {card} {} layer={} elapsed={elapsed} {max_bone} {max_vertex}",data[1],f["layer"]);count+=1;
        }
        eprintln!("Studio Session card={card} frames=27 bone_max={max_bone} attachment_max={max_vertex}");
    }
    eprintln!("Studio Session accepted cards=5 frames={count} bones={bones} attachment_vertices={vertices} invalid_atomic={invalid}");
}

#[derive(Deserialize)]
struct Frame {
    state:String, parameter:String, value:f32, phase:f32, length:f32,
    #[serde(default)] parameter2:String, #[serde(default)] value2:f32,
    #[serde(default)] parameter3:String, #[serde(default)] value3:f32,
    #[serde(default,rename="baseParameter")] base_parameter:String,
    #[serde(default,rename="baseValue")] base_value:f32,
    #[serde(default)] controller:String,
}

#[derive(Deserialize)] struct ClockFrame {state:String,parameter:String,value:f32,elapsed:f32,#[serde(default)]controller:String,#[serde(default)]layer:usize}

#[test]
#[ignore="installed five-card native clock fixtures; full Session and accessories"]
fn animator_clock_reaches_session_palette() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut frames=0usize;let mut bones=0usize;let mut vertices=0usize;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        let path=game.join(format!("UserData/chara/female/{card}.png"));
        let mut actual=open(game,Some(&path),0).unwrap();actual.apply_anim_frame(0.,0.);actual.ensure_skel_export();
        let mut expected=SessionBlob::from_session(&actual).into_session();
        expected.apply_anim_frame(0.,0.);expected.ensure_skel_export();
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_' {c} else {'_'}).collect();
        let mut native:Vec<ClockFrame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Animator292/{stem}Animator292.json.animator-clock.json")).unwrap()).unwrap();
        assert_eq!(native.len(),24);let mut max_bone=0f32;let mut max_vertex=0f32;let mut invalid=0;
        let layer:Vec<ClockFrame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Animator292/{stem}Animator292Layer.json.animator-clock.json")).unwrap()).unwrap();
        assert_eq!(layer.len(),12);native.extend(layer);
        for f in native {
            let key=if f.layer==0 {format!("states:action/animator/00.unity3d|base / {}",f.state)} else {format!("states:studio/anime/00.unity3d|{} / {}",f.controller,f.state)};
            let ckey=CString::new(key.clone()).unwrap();
            for s in [&mut actual,&mut expected] {assert_eq!(unsafe{crate::ffi::set_anim_scene_mode(s,4)},0);assert_eq!(unsafe{crate::ffi::set_anim_clip(s,ckey.as_ptr())},0);}
            let available=crate::anim::animation_float_parameters(game,&key);
            let mut parameters:std::collections::HashMap<String,f32>=std::collections::HashMap::from([(f.parameter.clone(),f.value),("Speed".into(),0.37)]);
            if f.layer>0 {parameters.extend([("height".into(),0.37),("Breast".into(),0.61),("muneR_X".into(),0.31),("muneR_Y".into(),0.79)]);}
            for(name,value) in &parameters {if available.iter().any(|p|&p.name==name) {assert!(actual.set_anim_float(name,*value));}}
            let saved=actual.anim_parameters.clone();let time=actual.anim_time;let duration=actual.anim_duration();
            for bad in [0.,-1.,f32::NAN,f32::INFINITY,f32::from_bits(1)] {
                assert!(!actual.set_anim_float(&f.parameter,bad),"invalid clock must reject atomically {bad}");
                assert_eq!(actual.anim_parameters,saved);assert_eq!(actual.anim_time,time);assert_eq!(actual.anim_duration(),duration);invalid+=1;
            }
            let mut raw=expected.body_shaped.as_ref().unwrap().clone();
            crate::anim::apply_clip_at_parameters(game,&mut raw,&key,f.elapsed,&parameters);
            let input:Vec<OkBodyPose>=expected.skel_export.as_ref().unwrap().bones.iter().enumerate().filter(|(_,b)|!b.pose_locked)
                .filter_map(|(i,b)|raw.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&b.name))
                    .map(|tf|OkBodyPose{bone:i as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();
            assert!(expected.apply_external_body_frame(f.elapsed,1./60.,&input));actual.apply_anim_frame(f.elapsed,1./60.);
            for(id,a) in &actual.body_skel.as_ref().unwrap().tfs {
                let b=&expected.body_skel.as_ref().unwrap().tfs[id];
                for i in 0..3 {max_bone=max_bone.max((a.pos[i]-b.pos[i]).abs()).max((a.scale[i]-b.scale[i]).abs());}
                let direct=(0..4).map(|i|(a.rot[i]-b.rot[i]).abs()).fold(0f32,f32::max);
                let opposite=(0..4).map(|i|(a.rot[i]+b.rot[i]).abs()).fold(0f32,f32::max);max_bone=max_bone.max(direct.min(opposite));bones+=1;
            }
            for(a,b) in actual.parts.iter().zip(&expected.parts).filter(|(a,_)|a.runtime_accessory.is_some()) {
                assert_eq!(a.positions.len(),b.positions.len());
                for(p,q) in a.positions.iter().zip(&b.positions) {for i in 0..3 {max_vertex=max_vertex.max((p[i]-q[i]).abs());}vertices+=1;}
            }
            assert!(max_bone<2e-5&&max_vertex<2e-5,"clock Session propagation {card} {} {} {max_bone} {max_vertex}",f.state,f.value);frames+=1;
        }
        eprintln!("AnimatorClock Session card={card} frames=36 bone_max={max_bone} attachment_max={max_vertex} invalid_atomic={invalid}");
    }
    eprintln!("AnimatorClock Session accepted cards=5 frames={frames} bones={bones} attachment_vertices={vertices} invalid_atomic=900");
}

/// Verify the real FFI -> Session -> helpers/physics -> followers path against
/// the native-validated raw evaluator submitted through the external Animator
/// entry. A getter, finite pose or raw evaluator alone cannot detect lost floats.
#[test]
#[ignore="five installed cards; native 1D and Cartesian frame controls"]
fn animator_parameters_reach_session_palette() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut frames=0usize;let mut bones=0usize;let mut vertices=0usize;let mut positive=0usize;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        let path=game.join(format!("UserData/chara/female/{card}.png"));
        let mut initial=open(game,Some(&path),0).unwrap();initial.apply_anim_frame(0.,0.);initial.ensure_skel_export();
        let bytes=initial.to_cache_bytes().unwrap();
        let mut actual=OkSession::from_cache_bytes(&bytes).unwrap();
        let mut expected=OkSession::from_cache_bytes(&bytes).unwrap();
        actual.ensure_skel_export();expected.ensure_skel_export();
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_' {c} else {'_'}).collect();
        let mut count=0usize;let mut changed=0usize;let mut max_bone=0f32;let mut max_vertex=0f32;
        for (stage,suffix,two_d) in [(289,"animator-blend",false),(290,"animator-2d",true)] {
            let data=crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Animator{stage}/{stem}Animator{stage}.json.{suffix}.json")).unwrap();
            let control:Vec<Frame>=serde_json::from_slice(&data).unwrap();
            assert_eq!(control.len(),if two_d {144} else {60});
            for f in control {
                let key=if two_d {format!("states:studio/anime/00.unity3d|{} / {}",f.controller,f.state)}
                    else {format!("states:action/animator/00.unity3d|base / {}",f.state)};
                for s in [&mut actual,&mut expected] {
                    s.anim_scene_mode=4;
                    assert_eq!(unsafe{crate::ffi::set_anim_clip(s,CString::new(key.clone()).unwrap().as_ptr())},0);
                }
                let mut parameters=HashMap::from([(f.parameter,f.value)]);
                for (name,value) in [(f.parameter2,f.value2),(f.parameter3,f.value3),(f.base_parameter,f.base_value)] {
                    if !name.is_empty() {parameters.insert(name,value);}
                }
                let available=crate::anim::animation_float_parameters(game,&key);
                for (name,value) in &parameters {
                    if available.iter().any(|p|&p.name==name) {
                        assert_eq!(unsafe{crate::ffi::set_anim_float(&mut actual,CString::new(name.as_str()).unwrap().as_ptr(),*value)},0);
                    }
                }
                let t=f.phase*f.length;
                assert!((actual.anim_duration()-f.length).abs()<1e-4);
                let mut raw=expected.body_shaped.as_ref().unwrap().clone();
                crate::anim::apply_clip_at_parameters(game,&mut raw,&key,t,&parameters);
                let mut defaults=expected.body_shaped.as_ref().unwrap().clone();
                crate::anim::apply_clip_at_parameters(game,&mut defaults,&key,t,&HashMap::new());
                if raw.tfs.iter().any(|(id,a)| {
                    let b=&defaults.tfs[id];
                    a.pos.iter().zip(b.pos).chain(a.rot.iter().zip(b.rot)).any(|(a,b)|(a-b).abs()>1e-4)
                }) {changed+=1;positive+=1;}
                let input:Vec<OkBodyPose>=expected.skel_export.as_ref().unwrap().bones.iter().enumerate()
                    .filter(|(_,b)|!b.pose_locked)
                    .filter_map(|(i,b)|raw.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&b.name))
                        .map(|tf|OkBodyPose{bone:i as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();
                assert!(expected.apply_external_body_frame(t,1./60.,&input));
                assert_eq!(unsafe{crate::ffi::apply_anim_frame(&mut actual,t,1./60.)},0);
                for (id,a) in &actual.body_skel.as_ref().unwrap().tfs {
                    let b=&expected.body_skel.as_ref().unwrap().tfs[id];
                    for c in 0..3 {max_bone=max_bone.max((a.pos[c]-b.pos[c]).abs()).max((a.scale[c]-b.scale[c]).abs());}
                    let direct=(0..4).map(|c|(a.rot[c]-b.rot[c]).abs()).fold(0f32,f32::max);
                    let opposite=(0..4).map(|c|(a.rot[c]+b.rot[c]).abs()).fold(0f32,f32::max);
                    max_bone=max_bone.max(direct.min(opposite));bones+=1;
                    assert!(max_bone<2e-5,"session dropped animation parameters card={card} state={} phase={} bone={} error={max_bone}",f.state,f.phase,a.name);
                }
                for (a,b) in actual.parts.iter().zip(&expected.parts).filter(|(a,_)|a.runtime_accessory.is_some()) {
                    assert_eq!(a.positions.len(),b.positions.len());
                    for(p,q) in a.positions.iter().zip(&b.positions) {
                        for c in 0..3 {max_vertex=max_vertex.max((p[c]-q[c]).abs());}vertices+=1;
                    }
                    assert!(max_vertex<2e-5,"parameter followers {} error={max_vertex}",a.name);
                }
                frames+=1;count+=1;
            }
        }
        assert!(changed>100,"missing positive parameter control: {card}");
        eprintln!("AnimatorSession card={card} frames={count} changed={changed} bone_max={max_bone:.9} attachment_max_m={max_vertex:.9}");
    }
    eprintln!("AnimatorSession accepted cards=5 frames={frames} bones={bones} attachment_vertices={vertices} positive_frames={positive}");
}

#[test]
#[ignore="inspect source and published Cartesian response with installed March card"]
fn animator_cartesian_publisher_diagnostic() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut s=open(game,Some(&game.join("UserData/chara/female/[Card] March_7th_keiyuu.png")),0).unwrap();
    s.ensure_skel_export();s.anim_scene_mode=4;
    let key=CString::new("states:studio/anime/00.unity3d|khs3_f_base3 / 17:Te_siri_D_momu_L.Te_siri_D_momu_L").unwrap();
    assert_eq!(unsafe{crate::ffi::set_anim_clip(&mut s,key.as_ptr())},0);
    s.apply_anim_frame(0.4,0.);
    let before=s.body_skel.as_ref().unwrap().clone();
    let published:Vec<_>=s.skel_export.as_ref().unwrap().bones.iter().map(|b|(b.name.clone(),b.pose_rot)).collect();
    assert!(s.set_anim_float("siriL_X",0.31));s.apply_anim_frame(0.4,0.);
    for (id,a) in &s.body_skel.as_ref().unwrap().tfs {
        if a.name=="cf_j_siri_L" {
            let b=&before.tfs[id];
            eprintln!("CartesianPublisher id={id} parent={:?} before={:?} after={:?}",a.parent,b.rot,a.rot);
        }
    }
    for b in &s.skel_export.as_ref().unwrap().bones {
        if b.name=="cf_j_siri_L" {
            let previous=published.iter().find(|(n,_)|n==&b.name).unwrap().1;
            assert!(previous.iter().zip(b.pose_rot).all(|(a,b)|(a-b).abs()<2e-5),"physics reference priority changed");
            eprintln!("CartesianPublisher export name={} locked={} before={:?} after={:?}",b.name,b.pose_locked,published.iter().find(|(n,_)|n==&b.name).unwrap().1,b.pose_rot);
        }
    }
    std::env::set_var("OK_NODB","1");
    assert!(s.set_anim_float("siriL_X",0.5));s.apply_anim_frame(0.4,0.);
    let before=s.skel_export.as_ref().unwrap().bones.iter().find(|b|b.name=="cf_j_siri_L").unwrap().pose_rot;
    assert!(s.set_anim_float("siriL_X",0.31));s.apply_anim_frame(0.4,0.);
    let after=s.skel_export.as_ref().unwrap().bones.iter().find(|b|b.name=="cf_j_siri_L").unwrap().pose_rot;
    let difference=before.iter().zip(after).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max);
    assert!(difference>1e-4,"missing isolated Cartesian publication");
    std::env::remove_var("OK_NODB");
    eprintln!("CartesianPublisher verified physics_reference_priority=1 isolated_delta={difference:.9} physics_restored=1");
}
