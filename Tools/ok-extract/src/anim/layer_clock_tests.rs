use super::*;
use serde::Deserialize;

#[derive(Deserialize)] struct V3{x:f32,y:f32,z:f32}
#[derive(Deserialize)] struct Q4{x:f32,y:f32,z:f32,w:f32}
#[derive(Deserialize)] struct Bone{name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)] struct Float{name:String,value:f32}
#[derive(Deserialize)] struct Layer {layer:usize,state:String,weight:f32,length:f32,#[serde(rename="normalizedTime")]normalized_time:f32,#[serde(rename="startTime")]start_time:f32,#[serde(rename="startPhase")]start_phase:f32,started:bool}
#[derive(Deserialize)] struct Frame {
    bundle:String,controller:String,#[serde(rename="baseState")]base_state:String,
    elapsed:f32,#[serde(rename="baseLength")]base_length:f32,#[serde(rename="baseNormalizedTime")]base_normalized_time:f32,
    parameters:Vec<Float>,bones:Vec<Bone>,layers:Vec<Layer>,
}
const CARDS:[&str;5]=["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"];
fn frames(card:&str)->Vec<Frame> {
    let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
    serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/AnimatorLayerClock299/{stem}AnimatorLayerClock299.json.layer-clock.json")).unwrap()).unwrap()
}
fn key(f:&Frame)->String {key_order(f,false)}
fn key_order(f:&Frame,reverse:bool)->String {
    let hex=|value:&serde_json::Value|serde_json::to_vec(value).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>();
    let source=format!("controller@{}",hex(&serde_json::json!([f.bundle,f.controller,"",""])));
    let base=format!("{} / [Controller layer 0] {}",f.controller,f.base_state);
    let mut selected:Vec<_>=f.layers.iter().map(|l|format!("{} / [Controller layer {}] {}{}",f.controller,l.layer,f.base_state,if l.layer<73 {format!(" + {}",l.state)}else{String::new()})).collect();
    if reverse {selected.reverse();}
    format!("states:layers@{}|{base}",hex(&serde_json::json!([source,base,selected])))
}
fn settings(f:&Frame)->HashMap<String,f32> {
    let mut p:HashMap<_,_>=f.parameters.iter().map(|p|(p.name.clone(),p.value)).collect();
    for l in &f.layers {
        p.insert(controller::layer_weight_name(l.layer),l.weight);
        if l.layer<73 {p.insert(controller::layer_start_time_name(l.layer),l.start_time);p.insert(controller::layer_start_phase_name(l.layer),l.start_phase);}
    }p
}
fn error(tf:&crate::pose::Tf,b:&Bone)->[f32;3] {
    let p=[b.position.x,b.position.y,b.position.z];let s=[b.scale.x,b.scale.y,b.scale.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];
    [(0..3).map(|i|(tf.pos[i]-p[i]).abs()).fold(0f32,f32::max),
     (0..4).map(|i|(tf.rot[i]-q[i]).abs()).fold(0f32,f32::max).min((0..4).map(|i|(tf.rot[i]+q[i]).abs()).fold(0f32,f32::max)),
     (0..3).map(|i|(tf.scale[i]-s[i]).abs()).fold(0f32,f32::max)]
}

#[test]
#[ignore="actual five-card native Animator controls"]
fn native_layer_clock() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut count=0;let mut comparisons=0;let mut max=[0f32;3];
    for card in CARDS {
        if std::env::var("OK_LAYER_CLOCK_MARCH_ONLY").is_ok()&&card!=CARDS[0] {continue;}
        let mut session=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
        session.apply_anim_frame(0.,0.);let baseline=session.body_shaped.as_ref().unwrap();
        let tos=load_avatar_tos(game);
        let data=frames(card);assert_eq!(data.len(),20);
        // Unsupported selections must never appear as a usable fallback state.
        let first=key(&data[0]);let (source,label)=first.split_once('|').unwrap();
        let h=source.strip_prefix("states:layers@").unwrap();
        let bytes:Vec<_>=(0..h.len()).step_by(2).map(|i|u8::from_str_radix(&h[i..i+2],16).unwrap()).collect();
        let descriptor:serde_json::Value=serde_json::from_slice(&bytes).unwrap();
        for invalid in 0..5 {
            let mut d=descriptor.clone();
            match invalid {
                0=>{d[2][1]=d[2][0].clone();},
                1=>{d[2][0]=serde_json::json!("unknown layer");},
                2=>{d[1]=serde_json::json!("unknown base");},
                3=>{d[2]=serde_json::json!([]);},
                _=>{d[0]=serde_json::json!(source.strip_prefix("states:").unwrap());},
            }
            let encoded=serde_json::to_vec(&d).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>();
            assert!(!selectable_clip(game,&format!("states:layers@{encoded}|{label}")));
        }
        for f in data {
            let key=key(&f);assert!(selectable_clip(game,&key),"missing source multi-layer {key}");
            assert!(!clip_expression_bone_mode(game,&key,0));assert_eq!(clip_limb_corrections_mode(game,&key,0),(false,false));
            let parameters=settings(&f);let (source,entry,leaves,_)=motion_plan(game,&key,&parameters).unwrap();
            assert_eq!(entry.active_layers.len(),f.layers.len());
            assert_eq!(entry.parameters.iter().map(|p|p.id).collect::<HashSet<_>>().len(),entry.parameters.len());
            for l in f.layers.iter().filter(|l|l.layer>=73) {
                assert!(!entry.parameters.iter().any(|p|p.name==controller::layer_start_time_name(l.layer)||p.name==controller::layer_start_phase_name(l.layer)));
            }
            assert!((clip_duration_parameters(game,&key,&parameters)-f.base_length).abs()<1e-4);
            assert!((f.elapsed/f.base_length-f.base_normalized_time).abs()<2e-5);
            let mut plans=leaves;
            for l in &f.layers {
                let child=entry.active_layers.iter().find(|e|e.layer.as_ref().unwrap().index==l.layer).unwrap();
                let (_,plan,child_leaves,duration)=motion_plan_entry(game,&source,child.clone(),&parameters).unwrap();
                if child.layer.as_ref().unwrap().synchronized {assert!((l.normalized_time-f.base_normalized_time).abs()<2e-5);}
                else {assert!((duration/plan.timing.speed-l.length).abs()<1e-4);if l.started {assert!(((f.elapsed-l.start_time)/l.length+l.start_phase-l.normalized_time).abs()<2e-5);}}
                plans.extend(child_leaves);
            }
            let mut attrs=HashSet::new();
            for (clip,_,_,_) in plans {
                let raw=format!("{}|{}",clip.source.as_deref().unwrap_or(&source),clip.label);
                for binding in load_clip_at(game,&raw,0.).unwrap().0 {
                    if (binding.type_id==4||binding.type_id==0)&&(1..=3).contains(&binding.attribute) {
                        if let Some(path)=tos.get(&binding.path) {attrs.insert(bone_name(path).to_string());}
                    }
                }
            }
            let mut pose=baseline.clone();apply_clip_at_parameters(game,&mut pose,&key,f.elapsed,&parameters);
            let by_name:HashMap<_,_>=pose.tfs.values().map(|tf|(tf.name.as_str(),tf)).collect();let mut matched=0;
            for b in f.bones.iter().filter(|b|attrs.contains(&b.name)) {
                let Some(tf)=by_name.get(b.name.as_str()) else {continue;};let err=error(tf,b);
                for i in 0..3 {max[i]=max[i].max(err[i]);}
                assert!(err.iter().all(|v|*v<2e-5),"native multi-layer {card} t={} bone={} err={err:?}",f.elapsed,b.name);
                comparisons+=3;matched+=1;
            }
            let source_cf=attrs.iter().filter(|n|n.starts_with("cf_")).count();
            assert!(source_cf>=80);assert_eq!(matched,source_cf,"every authored CF binding must be compared");let mut reversed=baseline.clone();apply_clip_at_parameters(game,&mut reversed,&key_order(&f,true),f.elapsed,&parameters);
            for(id,tf) in &pose.tfs {assert_eq!(tf.pos,reversed.tfs[id].pos);assert_eq!(tf.rot,reversed.tfs[id].rot);assert_eq!(tf.scale,reversed.tfs[id].scale);}count+=1;
        }
        eprintln!("LayerClock native card={card} frames=20 max={max:?}");
    }
    eprintln!("LayerClock native accepted frames={count} attributes={comparisons} max={max:?}");
}

#[test]
#[ignore="five cards source/FFI versus external pose and all accessory vertices"]
fn layer_clock_session() {
    use crate::ffi::OkBodyPose;use std::ffi::CString;
    let game=&crate::test_support::game_path("");std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut count=0;let mut bones=0;let mut vertices=0;let mut max_bone=0f32;let mut max_vertex=0f32;
    for card in CARDS {
        let path=game.join(format!("UserData/chara/female/{card}.png"));
        let mut actual=crate::session::open(game,Some(&path),0).unwrap();let mut expected=crate::session::open(game,Some(&path),0).unwrap();
        actual.ensure_skel_export();expected.ensure_skel_export();actual.apply_anim_frame(0.,0.);expected.apply_anim_frame(0.,0.);
        for f in frames(card) {
            let key=key(&f);let p=settings(&f);
            for s in [&mut actual,&mut expected] {s.anim_scene_mode=0;assert_eq!(unsafe{crate::ffi::set_anim_clip(s,CString::new(key.clone()).unwrap().as_ptr())},0);}
            expected.anim_scene_mode=3;
            for (name,value) in &p {assert!(actual.set_anim_float(name,*value));}
            let saved=actual.anim_parameters.clone();let time=actual.anim_time;
            for l in &f.layers {
                for name in [controller::layer_start_time_name(l.layer),controller::layer_start_phase_name(l.layer)] {
                    if l.layer>=73 {assert!(!actual.set_anim_float(&name,0.37));}
                    else {for invalid in [f32::NAN,f32::INFINITY,f32::NEG_INFINITY] {assert!(!actual.set_anim_float(&name,invalid));}}
                }
            }
            for l in &f.layers {for invalid in [-0.01,1.01,f32::NAN,f32::INFINITY] {assert!(!actual.set_anim_float(&controller::layer_weight_name(l.layer),invalid));}}
            assert_eq!(actual.anim_parameters,saved);assert_eq!(actual.anim_time,time);
            let mut raw=expected.body_shaped.as_ref().unwrap().clone();apply_clip_at_parameters(game,&mut raw,&key,f.elapsed,&p);
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
            assert!(max_bone<2e-5&&max_vertex<2e-5,"Session multi-layer {card} bone={max_bone} attachment={max_vertex}");count+=1;
        }
        eprintln!("LayerClock Session card={card} frames=20 bone_max={max_bone} attachment_max_m={max_vertex}");
    }
    assert_eq!(count,100);eprintln!("LayerClock Session accepted frames={count} bones={bones} attachment_vertices={vertices} bone_max={max_bone} attachment_max_m={max_vertex}");
}


#[test]
fn layer_clock_extremes_and_default_phase_are_finite() {
    assert_eq!(layer_clock_phase(0.61,0.5,1.3,0.1,true,0.,0.),(0.61f32*0.5/1.3+0.1).rem_euclid(1.));
    assert!((layer_clock_phase(0.61,0.5,1.3,0.,true,0.11,0.37)-(0.5f32*0.5/1.3+0.37)).abs()<1e-6);
    for looping in [true,false] {for start in [f32::MAX,-f32::MAX] {for phase in [f32::MAX,-f32::MAX] {
        let value=layer_clock_phase(1.,f32::MAX,f32::MIN_POSITIVE,0.,looping,start,phase);
        assert!(value.is_finite()&&(0.0..=1.0).contains(&value));
    }}}
}
