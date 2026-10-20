use super::*;
use serde::Deserialize;
use std::{ffi::CString,collections::BTreeMap};
#[derive(Deserialize)]struct V3{x:f32,y:f32,z:f32}
#[derive(Deserialize)]struct Q4{x:f32,y:f32,z:f32,w:f32}
#[derive(Deserialize)]struct Bone{name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)]struct Float{name:String,value:f32}
#[derive(Deserialize)]#[serde(rename_all="camelCase")]struct Layer{layer:usize,state:String,weight:f32,start_time:f32,start_phase:f32}
#[derive(Deserialize)]#[serde(rename_all="camelCase")]struct Event{layer:usize,start:f32,blend:f32,offset:f32,source_length:f32,to_normalized:f32,transition_normalized:f32,transitioning:bool}
#[derive(Deserialize)]#[serde(rename_all="camelCase")]struct Frame{bundle:String,controller:String,base_state:String,elapsed:f32,base_length:f32,parameters:Vec<Float>,layers:Vec<Layer>,events:Vec<Event>,bones:Vec<Bone>}
const CARDS:[&str;5]=["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"];
fn frames(card:&str)->Vec<Frame>{let s:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/AnimatorParallelFade303/{s}AnimatorParallelFade303.json.parallel-fade.json")).unwrap()).unwrap()}
fn key(f:&Frame)->String {
    let hex=|v:&serde_json::Value|serde_json::to_vec(v).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>();
    let source=format!("controller@{}",hex(&serde_json::json!([f.bundle,f.controller,"",""])));
    let base=format!("{} / [Controller layer 0] {}",f.controller,f.base_state);
    let selected:Vec<_>=f.layers.iter().map(|l|format!("{} / [Controller layer {}] {}{}",f.controller,l.layer,f.base_state,if l.layer<73{format!(" + {}",l.state)}else{String::new()})).collect();
    format!("states:layers@{}|{base}",hex(&serde_json::json!([source,base,selected])))
}
fn params(f:&Frame)->HashMap<String,f32>{let mut p:HashMap<_,_>=f.parameters.iter().map(|p|(p.name.clone(),p.value)).collect();for l in &f.layers{p.insert(controller::layer_weight_name(l.layer),l.weight);if l.layer<73{p.insert(controller::layer_start_time_name(l.layer),l.start_time);p.insert(controller::layer_start_phase_name(l.layer),l.start_phase);}}p}
fn error(tf:&crate::pose::Tf,b:&Bone)->[f32;3]{let p=[b.position.x,b.position.y,b.position.z];let s=[b.scale.x,b.scale.y,b.scale.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];[(0..3).map(|i|(tf.pos[i]-p[i]).abs()).fold(0.,f32::max),(0..4).map(|i|(tf.rot[i]-q[i]).abs()).fold(0.,f32::max).min((0..4).map(|i|(tf.rot[i]+q[i]).abs()).fold(0.,f32::max)),(0..3).map(|i|(tf.scale[i]-s[i]).abs()).fold(0.,f32::max)]}
fn environment(){std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");}
fn selected(card:&str)->bool{std::env::var("OK_PARALLEL_FADE_MARCH_ONLY").is_err()||card==CARDS[0]}
fn total()->usize{if std::env::var("OK_PARALLEL_FADE_MARCH_ONLY").is_ok(){20}else{100}}

fn plan_fades(game:&Path,f:&Frame,k:&str,p:&mut HashMap<String,f32>)->BTreeMap<usize,crossfade::CrossFade>{
    let mut fades=BTreeMap::new();
    for e in &f.events{
        let fade=crossfade::CrossFade::new_layer(game,k,k,e.layer,p,e.start,e.blend,e.offset).unwrap();
        assert!((fade.source_length-e.source_length).abs()<1e-4);
        assert!((fade.weight(f.elapsed)-e.transition_normalized).abs()<2e-5);
        assert!((e.offset+(f.elapsed-e.start)/e.source_length-e.to_normalized).abs()<2e-5);
        p.insert(controller::layer_start_time_name(e.layer),e.start);p.insert(controller::layer_start_phase_name(e.layer),e.offset);
        fades.insert(e.layer,fade);
    }fades
}
#[test]#[ignore="five-card native parallel independent-layer CrossFade"]
fn native_parallel_fade(){
    environment();let game=&crate::test_support::game_path("");let mut count=0;let mut attrs=0;let mut maximum=[0f32;3];
    for card in CARDS.into_iter().filter(|c|selected(c)){
        let mut s=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();s.apply_anim_frame(0.,0.);
        let baseline=s.body_shaped.as_ref().unwrap();let tos=load_avatar_tos(game);
        for f in frames(card){
            let k=key(&f);let mut p=params(&f);let fades=plan_fades(game,&f,&k,&mut p);
            let(source,root,mut leaves,_)=motion_plan(game,&k,&p).unwrap();for child in &root.active_layers{leaves.extend(motion_plan_entry(game,&source,child.clone(),&p).unwrap().2);}
            let mut authored:HashMap<String,HashSet<usize>>=HashMap::new();
            for(c,_,_,_)in leaves{let(bindings,values,_,_,_,_)=load_clip_at(game,&format!("{}|{}",c.source.as_deref().unwrap_or(&source),c.label),0.).unwrap();let mut curve=0;for b in bindings{let valid=b.attribute!=3||(0..3).all(|i|{let v=curve_at(&values,curve+i);v.is_finite()&&v.abs()>1e-6});if(b.type_id==4||b.type_id==0)&&(1..=4).contains(&b.attribute)&&valid{if let Some(path)=tos.get(&b.path){authored.entry(bone_name(path).to_string()).or_default().insert(if b.attribute==4{1}else{b.attribute as usize-1});}}curve+=curve_dim(&b) as i32;}}
            let mut pose=baseline.clone();crossfade::sample_layers(game,&mut pose,&k,f.elapsed,&p,&fades);
            let by_name:HashMap<_,_>=pose.tfs.values().map(|t|(t.name.as_str(),t)).collect();let mut matched=0;
            for b in f.bones.iter().filter(|b|authored.contains_key(&b.name)){let Some(tf)=by_name.get(b.name.as_str())else{continue;};let e=error(tf,b);for &i in &authored[&b.name]{maximum[i]=maximum[i].max(e[i]);assert!(e[i]<2e-5,"{card} time={} bone={} attribute={} error={e:?}",f.elapsed,b.name,i+1);attrs+=1;}matched+=1;}
            assert!(matched>=80);assert_eq!(matched,authored.keys().filter(|n|n.starts_with("cf_")&&by_name.contains_key(n.as_str())).count());count+=1;
        }
    }assert_eq!(count,total());eprintln!("ParallelFade native accepted frames={count} attributes={attrs} max={maximum:?}");
}
fn external(s:&mut crate::session::OkSession,k:&str,p:&HashMap<String,f32>,fades:&BTreeMap<usize,crossfade::CrossFade>,t:f32,dt:f32){
    use crate::ffi::OkBodyPose;let mut raw=s.body_shaped.as_ref().unwrap().clone();
    if fades.is_empty(){apply_clip_at_parameters(&s.game_root,&mut raw,k,t,p);}else{crossfade::sample_layers(&s.game_root,&mut raw,k,t,p,fades);}
    let poses:Vec<_>=s.skel_export.as_ref().unwrap().bones.iter().enumerate().filter(|(_,b)|!b.pose_locked).filter_map(|(i,b)|raw.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&b.name)).map(|tf|OkBodyPose{bone:i as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();assert!(s.apply_external_body_frame(t,dt,&poses));
}
#[test]#[ignore="parallel layer FFI versus external pose, physics and accessories"]
fn parallel_fade_session(){
    environment();let game=&crate::test_support::game_path("");let mut count=0;let mut events=0;let mut bones=0;let mut vertices=0;let mut max_bone=0f32;let mut max_vertex=0f32;let mut rejected=0;let mut zero=0;
    for card in CARDS.into_iter().filter(|c|selected(c)){
        let path=game.join(format!("UserData/chara/female/{card}.png"));let mut actual=crate::session::open(game,Some(&path),0).unwrap();let mut expected=crate::session::open(game,Some(&path),0).unwrap();actual.ensure_skel_export();expected.ensure_skel_export();actual.apply_anim_frame(0.,0.);expected.apply_anim_frame(0.,0.);
        for f in frames(card){
            let k=key(&f);let mut p=params(&f);let mut fades=BTreeMap::new();
            for s in [&mut actual,&mut expected]{assert_eq!(unsafe{crate::ffi::set_anim_clip(s,CString::new(k.clone()).unwrap().as_ptr())},0);s.anim_scene_mode=3;for(n,v)in &p{assert!(s.set_anim_float(n,*v));}}
            for e in &f.events{
                actual.apply_anim_frame(e.start,0.);external(&mut expected,&k,&p,&fades,e.start,0.);
                let saved=actual.anim_parameters.clone();let saved_fades=actual.anim_layer_crossfades.clone();
                for(target,index,duration,offset)in[(k.as_str(),0,0.4,0.),(k.as_str(),999,0.4,0.),(k.as_str(),73,0.4,0.),("not_a_state",e.layer,0.4,0.),(k.as_str(),e.layer,-1.,0.),(k.as_str(),e.layer,0.4,f32::NAN),(k.as_str(),e.layer,f32::INFINITY,0.)]{assert!(!actual.crossfade_layer(target,index,duration,offset));rejected+=1;assert_eq!(actual.anim_parameters,saved);assert_eq!(actual.anim_layer_crossfades,saved_fades);assert_eq!(actual.anim_time,e.start);assert_eq!(actual.anim_clip,k);}
                let epoch=controller::layer_start_time_name(e.layer);actual.anim_parameters.insert(epoch.clone(),e.start+1.);let future=actual.anim_parameters.clone();assert!(!actual.crossfade_layer(&k,e.layer,0.4,0.67));assert_eq!(actual.anim_parameters,future);assert_eq!(actual.anim_layer_crossfades,saved_fades);rejected+=1;actual.anim_parameters=saved.clone();
                let fade=crossfade::CrossFade::new_layer(game,&k,&k,e.layer,&p,e.start,e.blend,e.offset).unwrap();
                assert_eq!(unsafe{crate::ffi::ok_animation_crossfade_layer(&mut actual,e.layer as i32,CString::new(k.clone()).unwrap().as_ptr(),e.blend,e.offset)},0);
                assert_eq!(actual.anim_time,e.start);for(i,old)in &saved_fades{assert_eq!(&actual.anim_layer_crossfades[i],old);}
                p.insert(epoch,e.start);p.insert(controller::layer_start_phase_name(e.layer),e.offset);assert_eq!(actual.anim_parameters,p);
                assert!((unsafe{crate::ffi::ok_animation_layer_transition_source_length(&mut actual,e.layer as i32)}-e.source_length).abs()<1e-4);
                assert_eq!(unsafe{crate::ffi::ok_animation_in_transition(&mut actual)},1);assert_eq!(unsafe{crate::ffi::ok_animation_layer_in_transition(&mut actual,e.layer as i32)},1);
                let global=unsafe{crate::ffi::ok_animation_transition_source_length(&mut actual)};if actual.anim_layer_crossfades.len()>1{assert_eq!(global,0.);}else{assert!((global-e.source_length).abs()<1e-4);}
                assert!(!actual.crossfade_layer(&k,e.layer,0.4,0.));assert!(!actual.crossfade_layer(&k,e.layer,0.,0.));assert!(!actual.crossfade(&k,0.4,0.));assert!(!actual.set_anim_float("height",0.5));assert!(!actual.set_anim_trigger("Restart"));rejected+=5;
                assert_eq!(actual.anim_parameters,p);fades.insert(e.layer,fade);events+=1;
            }
            external(&mut expected,&k,&p,&fades,f.elapsed,1./60.);actual.apply_anim_frame(f.elapsed,1./60.);
            for e in &f.events{assert_eq!(unsafe{crate::ffi::ok_animation_layer_in_transition(&mut actual,e.layer as i32)},e.transitioning as i32);}
            assert_eq!(actual.is_anim_transitioning(),f.events.iter().any(|e|e.transitioning));assert!((actual.anim_duration()-f.base_length).abs()<1e-4);
            for(id,a)in &actual.body_skel.as_ref().unwrap().tfs{let b=&expected.body_skel.as_ref().unwrap().tfs[id];for i in 0..3{max_bone=max_bone.max((a.pos[i]-b.pos[i]).abs()).max((a.scale[i]-b.scale[i]).abs());}max_bone=max_bone.max((0..4).map(|i|(a.rot[i]-b.rot[i]).abs()).fold(0.,f32::max).min((0..4).map(|i|(a.rot[i]+b.rot[i]).abs()).fold(0.,f32::max)));bones+=1;}
            for(a,b)in actual.parts.iter().zip(&expected.parts).filter(|(a,_)|a.runtime_accessory.is_some()){assert_eq!(a.positions.len(),b.positions.len());for(v,w)in a.positions.iter().zip(&b.positions){for i in 0..3{max_vertex=max_vertex.max((v[i]-w[i]).abs());}vertices+=1;}}
            assert!(max_bone<2e-5&&max_vertex<2e-5);count+=1;
            if !actual.is_anim_transitioning(){assert!(actual.set_anim_float("height",p["height"]));let mut anchored=actual.body_shaped.as_ref().unwrap().clone();let mut pure=anchored.clone();apply_clip_at_parameters(game,&mut anchored,&k,f.elapsed,&p);crossfade::sample_layers(game,&mut pure,&k,f.elapsed,&p,&fades);for(id,a)in &anchored.tfs{let b=&pure.tfs[id];assert!(a.pos.iter().zip(b.pos).all(|(a,b)|(a-b).abs()<2e-5));assert!(a.scale.iter().zip(b.scale).all(|(a,b)|(a-b).abs()<2e-5));assert!(a.rot.iter().zip(b.rot).all(|(a,b)|(a-b).abs()<2e-5));}}
        }
        // Instantaneous second layer cannot erase an active zero-weight first layer.
        let f=frames(card).remove(10);let k=key(&f);let p=params(&f);assert_eq!(p[&controller::layer_weight_name(7)],0.);
        assert_eq!(unsafe{crate::ffi::set_anim_clip(&mut actual,CString::new(k.clone()).unwrap().as_ptr())},0);for(n,v)in &p{assert!(actual.set_anim_float(n,*v));}
        actual.apply_anim_frame(0.61,0.);assert!(actual.crossfade_layer(&k,7,0.4,0.67));actual.apply_anim_frame(0.77,0.);let first=actual.anim_layer_crossfades[&7].clone();
        assert!(actual.crossfade_layer(&k,8,0.,0.43));assert_eq!(actual.anim_layer_crossfades.len(),1);assert_eq!(actual.anim_layer_crossfades[&7],first);assert_eq!(actual.anim_time,0.77);assert_eq!(actual.anim_parameters[&controller::layer_start_time_name(8)],0.77);assert_eq!(actual.anim_parameters[&controller::layer_start_phase_name(8)],0.43);
        assert_eq!(unsafe{crate::ffi::ok_animation_layer_in_transition(&mut actual,8)},0);assert_eq!(unsafe{crate::ffi::ok_animation_layer_transition_source_length(&mut actual,8)},0.);assert!(actual.is_anim_transitioning());zero+=1;
        assert_eq!(unsafe{crate::ffi::set_anim_clip(&mut actual,CString::new(k).unwrap().as_ptr())},0);assert!(!actual.is_anim_transitioning());assert!(actual.anim_layer_crossfades.is_empty());assert!(actual.set_anim_float("height",0.37));
        assert_eq!(unsafe{crate::ffi::ok_animation_layer_in_transition(&mut actual,-1)},0);assert_eq!(unsafe{crate::ffi::ok_animation_layer_transition_source_length(&mut actual,-1)},0.);
    }assert_eq!(count,total());assert_eq!(zero,if total()==100{5}else{1});eprintln!("ParallelFade Session accepted frames={count} events={events} bones={bones} attachment_vertices={vertices} bone_max={max_bone} attachment_max_m={max_vertex} rejected={rejected} zero_during_active={zero}");
}
