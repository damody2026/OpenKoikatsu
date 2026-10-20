//! Installed renderer-free ordinary DynamicBone with recorded per-frame inputs.
use super::*;
use serde_json::{Value,json};
fn vector<const N:usize>(v:&Value)->[f32;N]{
    let names=["x","y","z","w"];
    std::array::from_fn(|i|{let value=v[names[i]].as_f64().unwrap() as f32;assert!(value.is_finite());value})
}
fn skeleton(nodes:&Value)->Skeleton{
    let tfs=nodes.as_array().unwrap().iter().map(|n|{
        let id=n["id"].as_i64().unwrap();let parent=n["parent"].as_i64().unwrap();
        (id,Tf{name:n["name"].as_str().unwrap().into(),parent:(parent!=0).then_some(parent),sibling_index:Some(id as u32),
            pos:vector(&n["pos"]),rot:vector(&n["rot"]),scale:vector(&n["scale"])})
    }).collect();
    Skeleton{tfs,names:vec![],bone_ids:vec![],worlds:vec![],rest:vec![],smr_count:0,source_mesh:0}
}
fn quaternion_error(a:[f32;4],b:[f32;4])->f32{
    let delta=|sign:f32|a.iter().zip(b).map(|(a,b)|(a-sign*b).abs()).fold(0f32,f32::max);
    delta(1.).min(delta(-1.))
}
#[test]
#[ignore="installed renderer-free native DynamicBone scale/movement oracle"]
fn native_persistent_dynamic_scale(){
    let fixture=std::env::var("OK_DYNAMIC_SCALE_FIXTURE").expect("fresh native fixture required");
    let report=std::env::var("OK_DYNAMIC_SCALE_REPORT").expect("fresh report path required");
    let external=std::env::var("OK_DYNAMIC_COLLIDER_EXTERNAL").ok().as_deref()==Some("1");
    let dump:Value=serde_json::from_slice(&crate::gamefs::read(&fixture).unwrap()).unwrap();
    let sequences=dump["sequences"].as_array().unwrap();assert!(matches!(sequences.len(),5|9));
    let mut rows=Vec::new();let mut initial_max=0f32;let mut inputs_max=0f32;
    let mut particle_max=0f32;let mut previous_max=0f32;let mut world_max=0f32;let mut local_max=0f32;let mut rotation_max=0f32;
    for sequence in sequences{
        let name=sequence["name"].as_str().unwrap();let source=skeleton(&sequence["initial"]);assert_eq!(source.tfs.len(),6);
        let collision=sequence["collision"].as_bool().unwrap();
        let collider=BodyCollider{transform:6,center:[0.07,-0.35,0.04],radius:0.14,
            height:sequence["colliderHeight"].as_f64().unwrap() as f32,direction:1,inside:sequence["colliderInside"].as_bool().unwrap_or(false)};
        let set=DBoneSet{bones:vec![DBone{root:3,component_transform:Some(2),update_rate:60.,
            damping:0.2,elasticity:0.15,stiffness:0.1,inert:0.3,radius:0.012,
            gravity:[0.001,-0.005,0.0005],force:[0.003,-0.007,0.002],end_offset:[0.02,-0.12,0.01],
            freeze_axis:sequence["freeze"].as_u64().unwrap() as usize,
            collider_refs:if collision{vec![100]}else{vec![]},..Default::default()}],
            colliders:[(100,collider.clone())].into_iter().collect(),..Default::default()};
        let mut state=AttachedDynamicSet::new_unsettled(&set,&source,&ident()).unwrap();
        assert_eq!(state.chains.len(),1);assert_eq!(state.chains[0].particles.len(),4);
        for (p,n) in state.chains[0].particles.iter().zip(sequence["initialParticles"].as_array().unwrap()){
            assert_eq!(p.parent,n["parent"].as_i64().unwrap() as i32);
            initial_max=initial_max.max(vlen(vsub(p.pos,vector(&n["pos"]))));
        }
        let frames=sequence["frames"].as_array().unwrap();assert_eq!(frames.len(),180);
        for (index,frame) in frames.iter().enumerate(){
            assert_eq!(frame["frame"].as_u64().unwrap() as usize,index);
            assert_eq!(frame["input"].as_array().unwrap().len(),6);
            assert_eq!(frame["output"].as_array().unwrap().len(),6);
            assert_eq!(frame["particles"].as_array().unwrap().len(),4);
            let input=skeleton(&frame["input"]);state.sync_shared_pose(&input);
            if external {
                let native=BodyColliderSet{colliders:if collision{vec![(input.tfs[&6].name.clone(),collider.clone())]}else{vec![]}};
                state.configure_character_colliders(&input,&native,game_colliders::Flags::default());
                assert_eq!(state.chains[0].external_colliders.len(),usize::from(collision));
                assert!(state.chains[0].colliders.is_empty());
            }
            let component=world_of(2,&state.skel.tfs,&mut HashMap::new());
            let object_scale=transform_lossy_scale(2,&state.skel.tfs,&component)[0].abs();
            let scale_delta=frame["objectScale"].as_f64().map(|native|object_scale-native as f32);
            let mut input_rotation_max=0f32;
            for n in frame["input"].as_array().unwrap(){
                inputs_max=inputs_max.max(vlen(vsub(parts_pos(&state.skel.tfs,n["id"].as_i64().unwrap()),vector(&n["world"]))));
                if n.get("worldRot").is_some(){input_rotation_max=input_rotation_max.max(quaternion_error(world_rot_of(n["id"].as_i64().unwrap(),&state.skel.tfs),vector(&n["worldRot"])));}
            }
            state.step_on_animated_pose(frame["dt"].as_f64().unwrap() as f32);
            let mut pm=0f32;let mut prev=0f32;let mut wm=0f32;let mut lm=0f32;let mut rm=0f32;
            for (p,n) in state.chains[0].particles.iter().zip(frame["particles"].as_array().unwrap()){
                assert!(p.pos.into_iter().chain(p.prev).all(f32::is_finite));
                pm=pm.max(vlen(vsub(p.pos,vector(&n["pos"]))));prev=prev.max(vlen(vsub(p.prev,vector(&n["prev"]))));
            }
            for n in frame["output"].as_array().unwrap(){
                let id=n["id"].as_i64().unwrap();let actual=&state.skel.tfs[&id];
                assert!(actual.pos.into_iter().chain(actual.rot).chain(actual.scale).all(f32::is_finite));
                wm=wm.max(vlen(vsub(parts_pos(&state.skel.tfs,id),vector(&n["world"]))));
                lm=lm.max(vlen(vsub(actual.pos,vector(&n["pos"]))));rm=rm.max(quaternion_error(actual.rot,vector(&n["rot"])));
            }
            particle_max=particle_max.max(pm);previous_max=previous_max.max(prev);world_max=world_max.max(wm);local_max=local_max.max(lm);rotation_max=rotation_max.max(rm);
            rows.push(json!({"case":name,"frame":index,"particle_max_m":pm,"previous_max_m":prev,"world_max_m":wm,"local_max_m":lm,"quaternion_component_max":rm,
                "object_scale_delta":scale_delta,"input_rotation_max":input_rotation_max}));
        }
    }
    let free=&sequences[0]["frames"];let sphere=&sequences[1]["frames"];
    let collision_delta=(0..180).flat_map(|f|(1..4).map(move|p|vlen(vsub(vector(&free[f]["particles"][p]["pos"]),vector(&sphere[f]["particles"][p]["pos"]))))).fold(0f32,f32::max);
    let passed=[initial_max,inputs_max,particle_max,previous_max,world_max,local_max,rotation_max].into_iter().all(|e|e<2e-5)&&collision_delta>1e-3;
    std::fs::write(&report,serde_json::to_vec_pretty(&json!({"passed":passed,"external_character_colliders":external,"cases":sequences.len(),"frames":rows.len(),"particle_observations":rows.len()*4,
        "initial_max_m":initial_max,"input_world_max_m":inputs_max,"particle_max_m":particle_max,"previous_max_m":previous_max,
        "world_max_m":world_max,"local_max_m":local_max,"quaternion_component_max":rotation_max,"native_collision_delta_m":collision_delta,
        "tolerance":2e-5,"rows":rows,"clothing_intersections_verified":false,"scene_normality_verified":false})).unwrap()).unwrap();
    eprintln!("native DynamicBone frames={} external={external} particle={particle_max:.9} prev={previous_max:.9} world={world_max:.9} local={local_max:.9} quat={rotation_max:.9} input={inputs_max:.9} collision={collision_delta:.9}",rows.len());
    assert!(passed,"native particle/bone comparison failed; inspect preserved report");
}
