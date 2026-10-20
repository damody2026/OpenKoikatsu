//! Neutral numerical garment controls using the runtime ABMX path, no images.
use super::*;
use crate::pose::{Mat4,Skeleton,Tf};

fn export_worlds(ex:&crate::skel_export::SkelExport,posed:bool)->Vec<Mat4>{
    let tfs=ex.bones.iter().enumerate().map(|(i,b)|{
        let (pos,rot,scale)=if posed{(b.pose_pos,b.pose_rot,b.pose_scl)}else{(b.rest_pos,b.rest_rot,b.rest_scl)};
        (i as i64+1,Tf{name:b.name.clone(),parent:(b.parent>=0).then_some(b.parent as i64+1),sibling_index:None,pos,rot,scale})
    }).collect();
    (0..ex.bones.len()).map(|i|crate::pose::world_of_id(i as i64+1,&tfs)).collect()
}
fn under(sk:&Skeleton,mut id:i64,ancestor:i64)->bool{
    for _ in 0..128{if id==ancestor{return true;}let Some(parent)=sk.tfs.get(&id).and_then(|t|t.parent)else{return false;};if parent==id{return false;}id=parent;}false
}
fn snapshot(s:&OkSession)->Vec<u8>{
    let mut transforms:Vec<_>=s.body_skel.as_ref().unwrap().tfs.iter().map(|(id,t)|(*id,t.pos,t.rot,t.scale)).collect();
    transforms.sort_by_key(|t|t.0);
    bincode::serialize(&(transforms,s.parts.iter().map(|p|&p.positions).collect::<Vec<_>>())).unwrap()
}
fn scale_modifier(original:&[crate::abmx::AbmBone],scale:f32)->Vec<crate::abmx::AbmBone>{
    let mut result=original.to_vec();
    if let Some(b)=result.iter_mut().find(|b|b.name=="cf_n_height"){
        for d in &mut b.coord{for value in &mut d.scale{*value*=scale;}}
    }else{result.push(crate::abmx::AbmBone{name:"cf_n_height".into(),location:0,
        coord:vec![crate::abmx::AbmData{scale:[scale;3],..Default::default()}]});}
    result
}
#[test]
#[ignore="installed neutral clothed secondary physics, owns environment"]
fn installed_neutral_wardrobe_physics(){
    for key in ["OK_NODB","OK_UE_ANIM","OK_FINALIK","OK_CPU_SKIN"]{std::env::remove_var(key);}
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE"]{std::env::set_var(key,"1");}
    let game=&crate::test_support::game_path("");
    let output=std::env::var("OK_WARDROBE_PHYSICS_OUT").expect("fresh report directory required");
    std::fs::create_dir_all(&output).unwrap();
    let mut rows=Vec::new();let mut total_shared=0;let mut total_particles=0;
    for (sex,name) in [(1,"female/KK_226176.png"),(0,"male/KK_224305.png"),
        (1,"female/Alice_keiyuu.png"),(1,"female/[Card] March_7th_keiyuu.png")]{
        let card=game.join("UserData/chara").join(name);
        let mut session=open(game,Some(&card),0).unwrap();
        let other=open(game,Some(&card),0).unwrap();let untouched=snapshot(&other);
        assert_eq!(session.sex,sex);
        session.anim_clip=if sex==1{"states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01"}
            else{"states:studio/anime/00.unity3d|male / Base Layer.mc_m_walk_00_00_01"}.into();
        session.anim_scene_mode=4;
        let original=session.abm.clone();let mut time=0f32;
        for scale in [1f32,0.6,1.8,1.]{
            session.abm=scale_modifier(&original,scale);
            let mut changed=0;let mut previous:Option<Vec<[f32;3]>>=None;
            let mut shared_particles=0;let mut private_particles=0;let mut nonfinite=0;
            for _ in 0..60{
                time+=1./60.;session.apply_anim_frame(time,1./60.);
                let shared=session.clothes_dynamic_runtime.as_ref().map(|(_,state)|state.audit_particle_positions()).unwrap_or_default();
                shared_particles=shared.len();
                let private:Vec<_>=session.parts.iter().filter(|p|p.role.starts_with("clothes_"))
                    .filter_map(|p|p.runtime_accessory.as_ref()).filter_map(|r|r.state.as_ref())
                    .flat_map(|state|state.audit_particle_positions()).collect();
                private_particles=private.len();
                let positions:Vec<_>=shared.into_iter().chain(private).collect();
                nonfinite+=positions.iter().flatten().filter(|x|!x.is_finite()).count();
                nonfinite+=session.parts.iter().filter(|p|p.role.starts_with("clothes_"))
                    .flat_map(|p|p.positions.iter().flatten()).filter(|x|!x.is_finite()).count();
                if previous.as_ref().is_some_and(|p|*p!=positions){changed+=1;}
                previous=Some(positions);
                assert_eq!(snapshot(&other),untouched,"second session isolation");
            }
            total_shared+=shared_particles;total_particles+=shared_particles+private_particles;
            rows.push(serde_json::json!({"card":name,"scale":scale,"frames":60,"shared_particles":shared_particles,
                "private_garment_particles":private_particles,"changed_frames":changed,"nonfinite":nonfinite,"isolation":true}));
            std::fs::write(Path::new(&output).join("partial.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
            assert_eq!(nonfinite,0);if shared_particles+private_particles>0{assert!(changed>0,"physics history must change");}
        }
    }
    assert!(total_shared>0&&total_particles>0,"positive cloth physics coverage required");
    std::fs::write(Path::new(&output).join("report.json"),serde_json::to_vec_pretty(&serde_json::json!({
        "frames":960,"rows":rows,"positive_shared_particle_observations":total_shared,
        "particle_end_of_phase_observations":total_particles,"physics_enabled":true,"neutral_walk_only":true,
        "clothing_intersections_verified":false,"native_particle_parity_verified":false})).unwrap()).unwrap();
}
#[test]
#[ignore="installed male/female neutral wardrobe height controls; owns environment"]
fn installed_neutral_wardrobe_height(){
    for (key,value) in [("OK_GPU_SKIN","1"),("OK_SKELMESH","1"),("OK_NOPOSE","1"),("OK_NODB","1")]{std::env::set_var(key,value);}
    std::env::remove_var("OK_FINALIK");std::env::remove_var("OK_CPU_SKIN");
    let game=&crate::test_support::game_path("");
    let output=std::env::var("OK_WARDROBE_HEIGHT_OUT").unwrap_or("../../Saved/Tests/WardrobeHeight351/R1".into());
    std::fs::create_dir_all(&output).unwrap();
    let mut rows=Vec::new();let mut errors=Vec::new();let mut frames=0;let mut isolation=true;
    for (sex,card_name) in [(1,"female/KK_226176.png"),(0,"male/KK_224305.png")]{
        let card=game.join("UserData/chara").join(card_name);
        let mut session=open(game,Some(&card),0).unwrap();
        let other=open(game,Some(&card),0).unwrap();let untouched=snapshot(&other);
        assert_eq!(session.sex,sex);
        let original_abm=session.abm.clone();let original_body=session.shape_body.clone();let face=session.shape_face.clone();
        let source="states:studio/anime/00.unity3d";
        let state=if sex==1{"f_aruki_00_00_01"}else{"mc_m_walk_00_00_01"};
        let labels=crate::anim::clip_names_for_source(game,source);
        std::fs::write(Path::new(&output).join(format!("walk-labels-{sex}.json")),serde_json::to_vec_pretty(&labels).unwrap()).unwrap();
        let controller=if sex==1{"walk_run / Base Layer."}else{"male / Base Layer."};
        let label=labels.iter().find(|l|l.starts_with(controller)&&l.ends_with(state)).expect("native neutral walk controller state");
        let key=std::ffi::CString::new(format!("{source}|{label}")).unwrap();
        unsafe{assert_eq!(crate::ffi::set_anim_clip(&mut session,key.as_ptr()),0);}
        session.anim_scene_mode=4;
        for native_height in [0f32,1f32]{
            let mut body=original_body.clone();body[0]=native_height;session.abm=original_abm.clone();session.apply_shape(&body,&face);
            session.ensure_skel_export();
            for (pose,time) in [("stand",0f32),("walk",0.4),("walk",0.8),("walk",1.2)]{
                if pose=="stand"{std::env::set_var("OK_UE_ANIM","1");}else{std::env::remove_var("OK_UE_ANIM");}
                let mut baseline:Vec<Vec<[f32;3]>>=Vec::new();let mut origin=[0.;3];let mut base_height_scale=[0.;3];
                for scale in [1f32,0.6,1.8]{
                    session.abm=scale_modifier(&original_abm,scale);
                    session.apply_anim_frame(time,0.);
                    std::env::remove_var("OK_SKELMESH");session.skin_all();std::env::set_var("OK_SKELMESH","1");
                    let sk=session.body_skel.as_ref().unwrap();let (&height_id,height)=sk.tfs.iter().find(|(_,t)|t.name=="cf_n_height").unwrap();
                    let matrix=crate::pose::world_of_id(height_id,&sk.tfs);let pivot=[matrix[0][3],matrix[1][3],matrix[2][3]];
                    if scale==1.{baseline=session.parts.iter().map(|p|p.positions.clone()).collect();origin=pivot;base_height_scale=height.scale;}
                    for a in 0..3{assert!((height.scale[a]-base_height_scale[a]*scale).abs()<1e-6,"ABMX height overwritten by sampling");}
                    let ex=session.skel_export.as_ref().unwrap();let posed=export_worlds(ex,true);let rest=export_worlds(ex,false);
                    let mut garment_parts=0;let mut vertices=0;let mut uniform_vertices=0;let mut max_gpu=0f32;let mut max_scale=0f32;let mut nonfinite=0;
                    let mut gpu_body_parts=0;let mut gpu_garment_parts=0;let mut private_parts=0;
                    let mut excluded=Vec::new();
                    for (index,p) in session.parts.iter().enumerate().filter(|(_,p)|p.role.starts_with("clothes_")||p.role=="body"){
                        let garment=p.role.starts_with("clothes_");
                        let runtime=p.runtime_accessory.as_ref().filter(|r|r.copy_weights_body);
                        let (weights,indices)=if let Some(r)=runtime{(&r.source_mesh().weights,&r.source_mesh().bone_idx)}else{(&p.weights,&p.bone_idx)};
                        if weights.is_empty()||(runtime.is_none()&&p.skel_kind!=1){excluded.push(serde_json::json!({"name":p.name,"role":p.role,"kind":p.skel_kind}));continue;}
                        if garment{garment_parts+=1;if runtime.is_some(){private_parts+=1;}}
                        let gpu=if runtime.is_none(){
                            if garment{gpu_garment_parts+=1;}else{gpu_body_parts+=1;}
                            Some(crate::skel_export::deform_from_ref(&ex.part_rest[index],&p.weights,&ex.part_bone_idx[index],&posed,&rest))
                        }else{None};
                        if let Some(gpu)=&gpu{assert_eq!(gpu.len(),p.positions.len());}
                        for (v,cpu) in p.positions.iter().enumerate(){
                            if garment{vertices+=1;}nonfinite+=cpu.iter().filter(|x|!x.is_finite()).count();
                            if let Some(gpu)=&gpu{nonfinite+=gpu[v].iter().filter(|x|!x.is_finite()).count();max_gpu=max_gpu.max(crate::finalik::vlen(crate::finalik::vsub(*cpu,gpu[v])));}
                            if !garment{continue;}
                            let vertex_weights=weights[v];let sum: f32=vertex_weights.iter().sum();
                            let all_under=(0..4).filter(|&a|vertex_weights[a]>0.).all(|a|{
                                let slot=indices[v][a] as usize;
                                let id=if let Some(r)=runtime{r.source_mesh().bone_names.get(slot).and_then(|name|sk.tfs.iter().find(|(_,t)|t.name==*name).map(|(&id,_)|id))}else{sk.bone_ids.get(slot).copied()};
                                id.is_some_and(|id|under(sk,id,height_id))
                            });
                            if all_under&&(sum-1.).abs()<1e-5{
                                uniform_vertices+=1;
                                let expected=std::array::from_fn(|a|origin[a]+scale*(baseline[index][v][a]-origin[a]));
                                max_scale=max_scale.max(crate::finalik::vlen(crate::finalik::vsub(*cpu,expected)));
                            }
                        }
                    }
                    std::fs::write(Path::new(&output).join("last-coverage.json"),serde_json::to_vec_pretty(&serde_json::json!({"garment_parts":garment_parts,"uniform_vertices":uniform_vertices,"gpu_body_parts":gpu_body_parts,"gpu_garment_parts":gpu_garment_parts,"private_parts":private_parts,"excluded":excluded})).unwrap()).unwrap();
                    assert!(garment_parts>0&&uniform_vertices>0&&gpu_body_parts>0,"positive garment scaling and body GPU controls required");
                    let passed=nonfinite==0&&max_gpu<2e-4&&max_scale<2e-5;
                    let row=serde_json::json!({"card":card_name,"sex":sex,"native_height":native_height,"pose":pose,"time":time,"abmx_scale":scale,
                        "height_scale":height.scale,"garment_parts":garment_parts,"private_parts":private_parts,"gpu_body_parts":gpu_body_parts,"gpu_garment_parts":gpu_garment_parts,"vertices":vertices,"uniform_vertices":uniform_vertices,
                        "excluded_parts":excluded,"max_cpu_gpu_m":max_gpu,"max_uniform_scale_m":max_scale,"nonfinite":nonfinite,"passed":passed});
                    if !passed{errors.push(row.clone());}rows.push(row);frames+=1;
                    std::fs::write(Path::new(&output).join("partial-rows.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
                }
            }
        }
        isolation&=untouched==snapshot(&other);
    }
    let report=serde_json::json!({"passed":errors.is_empty()&&isolation,"frames":frames,"instance_isolation":isolation,"rows":rows,"errors":errors,
        "scope":"neutral standing/walking, runtime ABMX uniform height modifiers, body CPU/GPU numerical comparison and body-matched garment vertices uniform scaling; private garment output retained; no scene images",
        "clothing_intersections_verified":false,"native_extreme_scale_fixture_verified":false,"private_palette_gpu_garments_verified":false});
    std::fs::write(Path::new(&output).join("report.json"),serde_json::to_vec_pretty(&report).unwrap()).unwrap();
    eprintln!("WARDROBE frames={frames} isolation={isolation} failures={}",errors.len());
    assert!(errors.is_empty()&&isolation,"Wardrobe height mismatch; report saved to {output}");
}
