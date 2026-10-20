use super::*;
use serde::Deserialize;

#[derive(Deserialize)] struct V3{x:f32,y:f32,z:f32}
#[derive(Deserialize)] struct Q4{x:f32,y:f32,z:f32,w:f32}
#[derive(Deserialize)] struct Bone{name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)] struct Float{name:String,value:f32}
#[derive(Deserialize)] struct NativeClip{name:String,weight:f32,length:f32}
#[derive(Deserialize)] struct Frame {
    bundle:String,controller:String,state:String,layer:usize,elapsed:f32,length:f32,
    #[serde(rename="normalizedTime")] normalized_time:f32,
    speed:f32,#[serde(rename="speedMultiplier")] multiplier:f32,
    #[serde(rename="baseLength")]base_length:f32,
    #[serde(rename="baseNormalizedTime")]base_normalized_time:f32,
    parameters:Vec<Float>,clips:Vec<NativeClip>,#[serde(rename="baseClips")]base_clips:Vec<NativeClip>,bones:Vec<Bone>,
}

const CARDS:[&str;5]=["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"];
fn frames(card:&str)->Vec<Frame> {
    let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
    serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/MotionTimeScale296/{stem}MotionTimeScale296.json.motion-time-scale.json")).unwrap()).unwrap()
}
fn key(f:&Frame)->String {
    if f.layer==0 {return format!("states:{}|{} / {}",f.bundle,f.controller,f.state);}
    let payload=[f.bundle.as_str(),f.controller.as_str(),"",""];
    let source=serde_json::to_vec(&payload).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>();
    format!("states:controller@{source}|{} / [Controller layer {}] {}",f.controller,f.layer,f.state)
}
fn settings(f:&Frame)->HashMap<String,f32>{f.parameters.iter().map(|p|(p.name.clone(),p.value)).collect()}

#[test]
#[ignore="installed game native motion duration fixtures"]
fn native_motion_time_scale() {
    let game=&crate::test_support::game_path("");let tos=load_avatar_tos(game);
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut errors=[0f32;3];let mut count=0;let mut attributes=0;let mut scaled_clock_controls=0;let mut synced_controls=0;
    for card in CARDS {
        if std::env::var("OK_MOTION_MARCH_ONLY").is_ok()&&card!=CARDS[0] {continue;}
        let data=frames(card);assert_eq!(data.len(),60);
        let mut session=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
        session.apply_anim_frame(0.,0.);let baseline=session.body_shaped.as_ref().unwrap();
        for f in data {
            let key=key(&f);assert!(selectable_clip(game,&key),"source state missing {key}");
            assert!(!clip_expression_bone_mode(game,&key,0),"game graph must not insert Studio PV/ExpressionBone");
            assert_eq!(clip_limb_corrections_mode(game,&key,0),if f.layer==0 {(false,true)}else{(false,false)});
            let parameters=settings(&f);let (source,entry,leaves,_)=motion_plan(game,&key,&parameters).unwrap();
            assert!((entry.timing.speed-f.speed*f.multiplier).abs()<1e-6);
            let duration=clip_duration_parameters(game,&key,&parameters);
            assert!((duration-f.length).abs()<1e-4,"duration {} layer={} rust={duration} game={}",f.controller,f.layer,f.length);
            assert!((f.elapsed/duration-f.normalized_time).abs()<2e-5,"normalized clock");
            if f.layer>0 {
                assert!(entry.layer.as_ref().unwrap().synchronized);
                assert!((duration-f.base_length).abs()<1e-4);
                assert!((f.normalized_time-f.base_normalized_time).abs()<2e-5);synced_controls+=1;
            } else {
                let unscaled:f32=leaves.iter().map(|(_,w,_,len)|w*len).sum::<f32>()/entry.timing.speed;
                if parameters["Speed"]>0. {assert!((unscaled-duration).abs()>0.02);scaled_clock_controls+=1;}
            }
            let mut attrs=HashSet::new();let mut inspect=|leaves:&[(ClipEntry,f32,f32,f32)],native:&[NativeClip]| {
                let mut weights=HashMap::<String,f32>::new();
                for(c,w,_,len) in leaves {
                    *weights.entry(c.name.clone()).or_default()+=w;
                    if let Some(n)=native.iter().find(|n|n.name==c.name) {assert!((len-n.length).abs()<1e-5);}
                    for b in load_clip_at(game,&format!("{}|{}",c.source.as_deref().unwrap_or(&source),c.label),0.).unwrap().0 {
                        if matches!(b.attribute,1..=4) {if let Some(path)=tos.get(&b.path) {attrs.insert((bone_name(path).to_string(),b.attribute));}}
                    }
                }
                assert_eq!(weights.len(),native.len());for n in native {assert!((weights[&n.name]-n.weight).abs()<1e-5);}
            };
            inspect(&leaves,&f.clips);
            if let Some(l)=&entry.layer {inspect(&motion_plan(game,&format!("states:{source}|{}",l.base_label),&parameters).unwrap().2,&f.base_clips);}
            let mut pose=baseline.clone();apply_clip_at_parameters(game,&mut pose,&key,f.elapsed,&parameters);
            let mut frame_attributes=0;
            for b in &f.bones {
                let Some(a)=pose.tfs.values().find(|a|a.name==b.name) else {continue;};
                let p=[b.position.x,b.position.y,b.position.z];let s=[b.scale.x,b.scale.y,b.scale.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];
                if attrs.contains(&(b.name.clone(),1)) {for i in 0..3 {errors[0]=errors[0].max((a.pos[i]-p[i]).abs());}frame_attributes+=1;}
                if attrs.contains(&(b.name.clone(),2))||attrs.contains(&(b.name.clone(),4)) {
                    errors[1]=errors[1].max((0..4).map(|i|(a.rot[i]-q[i]).abs()).fold(0f32,f32::max).min((0..4).map(|i|(a.rot[i]+q[i]).abs()).fold(0f32,f32::max)));frame_attributes+=1;
                }
                if attrs.contains(&(b.name.clone(),3)) {for i in 0..3 {errors[2]=errors[2].max((a.scale[i]-s[i]).abs());}frame_attributes+=1;}
                assert!(errors.iter().all(|v|*v<2e-5),"native motion time scale {card} layer={} time={} bone={} errors={errors:?}",f.layer,f.elapsed,b.name);
            }
            assert!(frame_attributes>100,"actual native rig must have animated bindings");
            attributes+=frame_attributes;count+=1;
        }
        eprintln!("MotionTimeScale native card={card} frames=60 errors={errors:?}");
    }
    assert!(attributes>1000);assert!(scaled_clock_controls>=16&&synced_controls>=36);
    eprintln!("MotionTimeScale native accepted frames={count} attributes={attributes} scaled_clock_controls={scaled_clock_controls} synced_controls={synced_controls} max={errors:?}");
}

#[test]
#[ignore="five cards through actual FFI and external pose composition"]
fn motion_time_scale_session() {
    use crate::ffi::OkBodyPose;use std::ffi::CString;
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut count=0;let mut bones=0;let mut vertices=0;let mut max_bone=0f32;let mut max_vertex=0f32;
    for card in CARDS {
        let path=game.join(format!("UserData/chara/female/{card}.png"));
        let mut actual=crate::session::open(game,Some(&path),0).unwrap();let mut expected=crate::session::open(game,Some(&path),0).unwrap();
        actual.ensure_skel_export();expected.ensure_skel_export();
        actual.apply_anim_frame(0.,0.);expected.apply_anim_frame(0.,0.);
        for f in frames(card) {
            let key=key(&f);let parameters=settings(&f);
            for s in [&mut actual,&mut expected] {s.anim_scene_mode=0;assert_eq!(unsafe{crate::ffi::set_anim_clip(s,CString::new(key.clone()).unwrap().as_ptr())},0);}
            // External UE poses deliberately default to Studio in Auto because
            // they carry no source provenance. Supply the known native scene.
            expected.anim_scene_mode=if f.layer==0 {2}else{3};
            for (name,value) in &parameters {assert!(actual.set_anim_float(name,*value));}
            assert!((actual.anim_duration()-f.length).abs()<1e-4);
            let mut raw=expected.body_shaped.as_ref().unwrap().clone();apply_clip_at_parameters(game,&mut raw,&key,f.elapsed,&parameters);
            let input:Vec<_>=expected.skel_export.as_ref().unwrap().bones.iter().enumerate().filter(|(_,b)|!b.pose_locked)
                .filter_map(|(i,b)|raw.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&b.name)).map(|tf|OkBodyPose{bone:i as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();
            assert!(expected.apply_external_body_frame(f.elapsed,1./60.,&input));assert_eq!(unsafe{crate::ffi::apply_anim_frame(&mut actual,f.elapsed,1./60.)},0);
            for (id,a) in &actual.body_skel.as_ref().unwrap().tfs {
                let b=&expected.body_skel.as_ref().unwrap().tfs[id];
                for i in 0..3 {max_bone=max_bone.max((a.pos[i]-b.pos[i]).abs()).max((a.scale[i]-b.scale[i]).abs());}
                max_bone=max_bone.max((0..4).map(|i|(a.rot[i]-b.rot[i]).abs()).fold(0f32,f32::max).min((0..4).map(|i|(a.rot[i]+b.rot[i]).abs()).fold(0f32,f32::max)));bones+=1;
            }
            for (a,b) in actual.parts.iter().zip(&expected.parts).filter(|(a,_)|a.runtime_accessory.is_some()) {
                assert_eq!(a.positions.len(),b.positions.len());for(p,q) in a.positions.iter().zip(&b.positions) {for i in 0..3 {max_vertex=max_vertex.max((p[i]-q[i]).abs());}vertices+=1;}
            }
            assert!(max_bone<2e-5&&max_vertex<2e-5,"Session motion scale {card} bone={max_bone} attachment={max_vertex}");count+=1;
        }
        eprintln!("MotionTimeScale Session card={card} frames=60 bone_max={max_bone} attachment_max_m={max_vertex}");
    }
    assert_eq!(count,300);eprintln!("MotionTimeScale Session accepted frames={count} bones={bones} attachment_vertices={vertices} bone_max={max_bone} attachment_max_m={max_vertex}");
}
