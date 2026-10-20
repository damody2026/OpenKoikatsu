use super::*;
use serde::Deserialize;
use std::ffi::CString;
#[derive(Deserialize)]struct V3{x:f32,y:f32,z:f32}
#[derive(Deserialize)]struct Q4{x:f32,y:f32,z:f32,w:f32}
#[derive(Deserialize)]struct Bone{name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)]#[serde(rename_all="camelCase")]struct Frame{bundle:String,controller:String,from_state:String,to_state:String,speed:f32,start:f32,elapsed:f32,blend:f32,offset:f32,from_length:f32,to_length:f32,to_normalized:f32,transition_normalized:f32,transitioning:bool,bones:Vec<Bone>}
const CARDS:[&str;5]=["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"];
fn frames(card:&str)->Vec<Frame>{let s:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/AnimatorCrossFade301/{s}AnimatorCrossFade301.json.crossfade.json")).unwrap()).unwrap()}
fn key(f:&Frame,state:&str)->String{format!("states:{}|{} / {}",f.bundle,f.controller,state)}
fn params(f:&Frame)->HashMap<String,f32>{HashMap::from([("MotionSpeed".into(),f.speed),("Speed".into(),0.37)])}
fn error(tf:&crate::pose::Tf,b:&Bone)->[f32;3]{let p=[b.position.x,b.position.y,b.position.z];let s=[b.scale.x,b.scale.y,b.scale.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];[(0..3).map(|i|(tf.pos[i]-p[i]).abs()).fold(0.,f32::max),(0..4).map(|i|(tf.rot[i]-q[i]).abs()).fold(0.,f32::max).min((0..4).map(|i|(tf.rot[i]+q[i]).abs()).fold(0.,f32::max)),(0..3).map(|i|(tf.scale[i]-s[i]).abs()).fold(0.,f32::max)]}
fn environment(){std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");}

#[test]#[ignore="five-card native CrossFade poses"]
fn native_crossfade(){
    environment();let game=&crate::test_support::game_path("");let mut count=0;let mut attrs=0;let mut maximum=[0f32;3];
    for card in CARDS{
        if std::env::var("OK_CROSSFADE_MARCH_ONLY").is_ok()&&card!=CARDS[0]{continue;}
        let mut s=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();s.apply_anim_frame(0.,0.);
        let baseline=s.body_shaped.as_ref().unwrap();let tos=load_avatar_tos(game);
        for f in frames(card){
            let from=key(&f,&f.from_state);let to=key(&f,&f.to_state);let p=params(&f);
            let fade=crossfade::CrossFade::new(game,&from,&to,&p,Default::default(),f.start,f.blend,f.offset).unwrap();
            assert!((fade.source_length-f.from_length).abs()<1e-4);assert!((crossfade::length(game,&to,&p).unwrap()-f.to_length).abs()<1e-4);
            assert!((fade.weight(f.elapsed)-f.transition_normalized).abs()<2e-5);
            assert!((f.offset+(f.elapsed-f.start)/f.to_length-f.to_normalized).abs()<2e-5);
            let mut authored:HashMap<String,HashSet<usize>>=HashMap::new();
            for (state,normalized) in [(&from,f.elapsed/f.from_length),(&to,f.to_normalized)] {
                let(_,entry,leaves,_)=motion_plan(game,state,&p).unwrap();
                for(clip,_,cycle,length)in leaves {
                    let phase=if entry.timing.looping{(normalized+entry.timing.cycle+cycle).rem_euclid(1.)}else{(normalized+entry.timing.cycle+cycle).clamp(0.,1.)};
                    let raw=format!("{}|{}",clip.source.as_deref().unwrap_or(&f.bundle),clip.label);
                    let(bindings,values,_,_,_,_)=load_clip_at(game,&raw,phase*length).unwrap();let mut curve=0;
                    for b in bindings {
                        // Serialized zero scales are not transform writes: the
                        // shared sampler retains the shaped/card baseline.
                        let valid_scale=b.attribute!=3||(0..3).all(|i|{let v=curve_at(&values,curve+i);v.is_finite()&&v.abs()>1e-6});
                        if (b.type_id==4||b.type_id==0)&&(1..=4).contains(&b.attribute)&&valid_scale {
                            if let Some(path)=tos.get(&b.path){authored.entry(bone_name(path).to_string()).or_default().insert(if b.attribute==4{1}else{b.attribute as usize-1});}
                        }
                        curve+=curve_dim(&b) as i32;
                    }
                }
            }
            let mut pose=baseline.clone();fade.sample(game,&mut pose,f.elapsed,&p);
            let by_name:HashMap<_,_>=pose.tfs.values().map(|tf|(tf.name.as_str(),tf)).collect();let mut matched=0;
            for b in f.bones.iter().filter(|b|authored.contains_key(&b.name)){let Some(tf)=by_name.get(b.name.as_str())else{continue;};let e=error(tf,b);for &i in &authored[&b.name]{maximum[i]=maximum[i].max(e[i]);assert!(e[i]<2e-5,"{card} {} t={} bone={} attribute={} error={e:?}",f.from_state,f.elapsed,b.name,i+1);attrs+=1;}matched+=1;}
            assert!(matched>=80);assert_eq!(matched,authored.keys().filter(|n|n.starts_with("cf_")&&by_name.contains_key(n.as_str())).count(),"every authored transform in the body Animator domain");count+=1;
        }
    }
    assert_eq!(count,if std::env::var("OK_CROSSFADE_MARCH_ONLY").is_ok(){15}else{75});eprintln!("CrossFade native accepted frames={count} attributes={attrs} max={maximum:?}");
}

#[test]#[ignore="source FFI CrossFade versus external pose, physics and accessories"]
fn crossfade_session(){
    use crate::ffi::OkBodyPose;environment();let game=&crate::test_support::game_path("");let mut count=0;let mut bones=0;let mut vertices=0;let mut max_bone=0f32;let mut max_vertex=0f32;let mut rejected=0;
    for card in CARDS{
        let path=game.join(format!("UserData/chara/female/{card}.png"));let mut actual=crate::session::open(game,Some(&path),0).unwrap();let mut expected=crate::session::open(game,Some(&path),0).unwrap();actual.ensure_skel_export();expected.ensure_skel_export();actual.apply_anim_frame(0.,0.);expected.apply_anim_frame(0.,0.);
        for f in frames(card){
            let from=key(&f,&f.from_state);let to=key(&f,&f.to_state);let p=params(&f);let fade=crossfade::CrossFade::new(game,&from,&to,&p,Default::default(),f.start,f.blend,f.offset).unwrap();
            for s in [&mut actual,&mut expected]{assert_eq!(unsafe{crate::ffi::set_anim_clip(s,CString::new(from.clone()).unwrap().as_ptr())},0);s.anim_scene_mode=2;for(n,v)in &p{assert!(s.set_anim_float(n,*v));}}
            let external=|s:&mut crate::session::OkSession,t:f32,dt:f32,initial:bool|{let mut raw=s.body_shaped.as_ref().unwrap().clone();if initial{apply_clip_at_parameters(game,&mut raw,&from,t,&p);}else{fade.sample(game,&mut raw,t,&p);}let poses:Vec<_>=s.skel_export.as_ref().unwrap().bones.iter().enumerate().filter(|(_,b)|!b.pose_locked).filter_map(|(i,b)|raw.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&b.name)).map(|tf|OkBodyPose{bone:i as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();assert!(s.apply_external_body_frame(t,dt,&poses));};
            external(&mut expected,f.start,0.,true);actual.apply_anim_frame(f.start,0.);
            let saved=actual.anim_parameters.clone();let clock=actual.anim_clock;let elapsed=actual.anim_time;
            for (target,duration,offset) in [(to.as_str(),-1.,0.),(to.as_str(),f32::NAN,0.),(to.as_str(),f32::INFINITY,0.),(to.as_str(),0.4,f32::NAN),("not_a_state",0.4,0.)]{assert!(!actual.crossfade(target,duration,offset));rejected+=1;assert_eq!(actual.anim_parameters,saved);assert_eq!(actual.anim_clock,clock);assert_eq!(actual.anim_clip,from);assert_eq!(actual.anim_time,elapsed);}
            assert_eq!(unsafe{crate::ffi::ok_animation_crossfade(&mut actual,CString::new(to.clone()).unwrap().as_ptr(),f.blend,f.offset)},0);assert_eq!(actual.anim_time,elapsed);
            if f.blend>0.{assert!(!actual.crossfade(&from,0.4,0.));assert!(!actual.set_anim_float("MotionSpeed",1.2));assert!(!actual.set_anim_trigger("Restart"));assert_eq!(actual.anim_parameters,saved);rejected+=3;}
            external(&mut expected,f.elapsed,1./60.,false);actual.apply_anim_frame(f.elapsed,1./60.);assert_eq!(actual.anim_crossfade.is_some(),f.transitioning);
            assert!((actual.anim_duration()-if f.transitioning{f.from_length}else{f.to_length}).abs()<1e-4);
            for(id,a)in &actual.body_skel.as_ref().unwrap().tfs{let b=&expected.body_skel.as_ref().unwrap().tfs[id];for i in 0..3{max_bone=max_bone.max((a.pos[i]-b.pos[i]).abs()).max((a.scale[i]-b.scale[i]).abs());}max_bone=max_bone.max((0..4).map(|i|(a.rot[i]-b.rot[i]).abs()).fold(0.,f32::max).min((0..4).map(|i|(a.rot[i]+b.rot[i]).abs()).fold(0.,f32::max)));bones+=1;}
            for(a,b)in actual.parts.iter().zip(&expected.parts).filter(|(a,_)|a.runtime_accessory.is_some()){assert_eq!(a.positions.len(),b.positions.len());for(v,w)in a.positions.iter().zip(&b.positions){for i in 0..3{max_vertex=max_vertex.max((v[i]-w[i]).abs());}vertices+=1;}}
            assert!(max_bone<2e-5&&max_vertex<2e-5);count+=1;
            if !f.transitioning{let time=actual.anim_time;assert!(actual.set_anim_float("MotionSpeed",1.2));assert_eq!(actual.anim_time,time);}
        }
    }
    assert_eq!(count,75);eprintln!("CrossFade Session accepted frames={count} bones={bones} attachment_vertices={vertices} bone_max={max_bone} attachment_max_m={max_vertex} rejected={rejected}");
}
