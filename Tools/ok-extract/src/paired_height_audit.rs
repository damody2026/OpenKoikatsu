//! Numerical paired-skeleton stress audit. No rendered scene output.
use super::*;
use crate::finalik::{IkSkeleton,vlen,vsub};

#[test]
#[ignore="installed clothed paired skeleton height stress audit"]
fn installed_clothed_pair_height_stress() {
    let game=&crate::test_support::game_path("");
    let fp=game.join("UserData/chara/female/KK_226176.png");
    let mp=game.join("UserData/chara/male/KK_224305.png");
    let output=std::env::var("OK_PAIR_HEIGHT_AUDIT_OUT").unwrap_or("../../Saved/Tests/PairedHeight349".into());
    let out=Path::new(&output);
    std::fs::create_dir_all(out).unwrap();
    let mut female=crate::session::open(game,Some(&fp),0).unwrap();
    let mut male=crate::session::open(game,Some(&mp),0).unwrap();
    for session in [&female,&male] {
        assert!(session.parts.iter().any(|p|p.role.starts_with("clothes_")),"clothed source fixture required");
    }
    let fshape=female.shape_body.clone();let mshape=male.shape_body.clone();
    let mut categories=std::collections::BTreeMap::new();let mut selected=Vec::new();
    for pair in pairs(game).into_iter().filter(|p|p.participants==2) {
        let count=categories.entry(pair.category.clone()).or_insert(0);
        if *count>=2 {continue;}
        let active=crate::anim::clip_names_for_source(game,&pair.female_source).iter()
            .filter_map(|label|label.split_once(" / [Controller layer 0] Base Layer.").map(|(_,s)|s))
            .any(|state|setup(game,&pair.female_motion,state).unwrap().is_some_and(|(_,data)|
                data.state(state).is_some_and(|s|s.parts.iter().any(|p|p.weight_pos>0. && !p.target.is_empty()))));
        if *count<2 && active {selected.push(pair);*count+=1;}
    }
    let mut rows=Vec::new();let mut frames=0usize;let mut nonfinite=0usize;
    for (fh,mh,fs,ms,label) in [(0.5,0.5,1.,1.,"baseline"),(0.,1.,1.,1.,"native_min_max"),
        (1.,0.,1.,1.,"native_max_min"),(0.,1.,0.6,1.8,"stress_short_tall"),
        (1.,0.,1.8,0.6,"stress_tall_short")] {
        let mut fb=fshape.clone();let mut mb=mshape.clone();fb[0]=fh;mb[0]=mh;
        let ff=female.shape_face.clone();let mf=male.shape_face.clone();
        female.apply_shape(&fb,&ff);male.apply_shape(&mb,&mf);
        for (session,scale) in [(&mut female,fs),(&mut male,ms)] {
            if scale!=1. {
                let shaped=session.body_shaped.as_mut().unwrap();
                let height=shaped.tfs.values_mut().find(|t|t.name=="cf_n_height").unwrap();
                for value in &mut height.scale {*value*=scale;}
                shaped.rebuild_worlds();session.body_skel=Some(shaped.clone());
            }
            session.paired_ik_runtime=None;
        }
        let height_scale:Vec<_>=[&female,&male].iter().map(|s|
            s.body_shaped.as_ref().unwrap().tfs.values().find(|t|t.name=="cf_n_height").unwrap().scale).collect();
        for pair in &selected {
            let fl=crate::anim::clip_names_for_source(game,&pair.female_source);
            let ml=crate::anim::clip_names_for_source(game,&pair.male_source);
            let common:Vec<_>=fl.iter().filter_map(|l|l.split_once(" / [Controller layer 0] Base Layer.").map(|(_,s)|(l,s)))
                .filter(|(_,s)|!s.contains('.')).filter_map(|(f,s)|ml.iter().find(|m|m.ends_with(&format!(" / [Controller layer 0] Base Layer.{s}"))).map(|m|(f,m,s)))
                .filter(|(_,_,state)|setup(game,&pair.female_motion,state).unwrap().is_some_and(|(_,data)|
                    data.state(state).is_some_and(|s|s.parts.iter().any(|p|p.weight_pos>0. && !p.target.is_empty()))))
                .take(2).collect();
            assert!(!common.is_empty());
            for (fl,ml,state) in common {
                for (session,source,label) in [(&mut female,&pair.female_source,fl),(&mut male,&pair.male_source,ml)] {
                    let key=std::ffi::CString::new(format!("{source}|{label}")).unwrap();
                    unsafe {assert_eq!(crate::ffi::set_anim_clip(session,key.as_ptr()),0);}
                    session.anim_scene_mode=3;
                }
                let mut contacts=0usize;let mut residual=0f32;let mut displacement=0f32;
                let mut previous:Option<Vec<[f32;3]>>=None;
                for frame in 0..=8 {
                    let time=frame as f32/4.;
                    validate_pair(&female,&male,state,&pair.female_motion,&pair.male_motion).unwrap();
                    set_native_pair_parameters(&mut female,&mut male);
                    female.sample_paired_frame(time);male.sample_paired_frame(time);
                    contacts+=apply_contacts(&mut female,&mut male,state,&pair.female_motion,&pair.male_motion).unwrap();
                    let mut positions=Vec::new();
                    for session in [&mut female,&mut male] {
                        if let Some(runtime)=&session.paired_ik_runtime {
                            let mut body=session.body_skel.clone().unwrap();let mut sk=IkSkeleton::new(&mut body);
                            for e in &runtime.solver.effectors {
                                if e.position_weight>0. {
                                    if let Some(p)=e.bone.as_deref().and_then(|b|sk.world_pos(b)) {
                                        residual=residual.max(vlen(vsub(p,e.position)));
                                    }
                                }
                            }
                        }
                        session.finish_paired_dynamics(0.25);
                        let sk=session.body_skel.as_ref().unwrap();
                        nonfinite+=sk.tfs.values().filter(|t|t.pos.iter().chain(&t.rot).chain(&t.scale).any(|v|!v.is_finite())).count();
                        assert!(!sk.tfs.values().any(|t|t.name.starts_with("__ok_pair_")));
                        let mut bones:Vec<_>=sk.tfs.values().filter(|t|t.name.starts_with("cf_j_")).collect();bones.sort_by(|a,b|a.name.cmp(&b.name));
                        positions.extend(bones.into_iter().map(|t| {
                            let world=sk.world_tf(&t.name).unwrap();[world[0][3],world[1][3],world[2][3]]
                        }));
                    }
                    if let Some(last)=&previous {for (a,b) in positions.iter().zip(last) {displacement=displacement.max(vlen(vsub(*a,*b)));}}
                    previous=Some(positions);
                    assert_eq!(female.anim_time,male.anim_time);assert_eq!(female.anim_parameters["height"],male.anim_parameters["height"]);
                    frames+=1;
                }
                rows.push(serde_json::json!({"case":label,"height_parameters":[fh,mh],"height_node_scales":height_scale,
                    "category":pair.category,"pair_id":pair.id,"state":state,"frames":9,
                    "active_effectors":contacts,"maximum_effector_target_residual_m":residual,
                    "maximum_joint_step_m":displacement}));
                assert!(contacts>0,"selected case must exercise active IK targets");
                eprintln!("height case={label} pair={}:{} frames=9 contacts={contacts} residual_m={residual}",pair.category,pair.id);
            }
        }
    }
    let report=serde_json::json!({"frames":frames,"role_frames":frames*2,"nonfinite":nonfinite,"rows":rows,
        "cards":[fp,mp],"outfit":0,"clothed_sources":true,"numeric_only":true,
        "visual_clothing_intersection_verified":false,"native_extreme_height_oracle_verified":false,
        "stress_scale_scope":"diagnostic cf_n_height skeletal scaling; not certified native card/ABMX import", 
        "all_scenarios_normal":false});
    std::fs::write(out.join("report.json"),serde_json::to_vec_pretty(&report).unwrap()).unwrap();
    assert_eq!(nonfinite,0);assert!(frames>0);
}
