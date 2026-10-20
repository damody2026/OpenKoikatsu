//! Independent installed solver oracle: native Transform graph and measured
//! particle coefficients. This does not substitute for asset/card parsing tests.
use super::*;

#[test]
#[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
fn native_shared_pose_reuse_matches_cloned_rig_history(){
    let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();let mut session=crate::portable::load(std::path::Path::new(&path)).unwrap();session.apply_anim_frame(0.,1./60.);
    let (_,state)=session.clothes_dynamic_runtime.as_ref().expect("fixture needs live coordinated clothes");
    let mut actual=state.clone();let mut oracle=state.clone();let mut frames=0;
    for frame in 0..120 {
        session.apply_anim_frame(frame as f32/60.,1./60.);let body=session.body_skel.as_ref().unwrap();
        actual.sync_shared_pose(body);oracle.sync_shared_pose_impl(body,false);
        assert_eq!(serde_json::to_value(&actual.skel).unwrap(),serde_json::to_value(&oracle.skel).unwrap());
        let dt=[0.,1./120.,1./60.,1./30.][frame%4];actual.step_on_animated_pose(dt);oracle.step_impl_with_cache(dt,false,false);
        assert_eq!(actual.particle_snapshot(),oracle.particle_snapshot());assert_eq!(actual.skel.worlds,oracle.skel.worlds);
        let mut display=body.clone();let mut expected=body.clone();actual.copy_simulated_pose(&mut display);oracle.copy_simulated_pose(&mut expected);
        assert_eq!(serde_json::to_value(display).unwrap(),serde_json::to_value(expected).unwrap());frames+=1;
    }
    eprintln!("native coordinated shared-pose exact frames={frames} chains={}",actual.chains.len());
}

#[test]
#[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
fn private_chain_world_scratch_matches_independent_maps() {
    let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
    let mut session=crate::portable::load(std::path::Path::new(&path)).unwrap();
    session.apply_anim_frame(0.,1./60.);
    let mut checked=0;let mut chains=0;
    for part in &session.parts {
        let Some(state)=part.runtime_accessory.as_ref().and_then(|runtime|runtime.dynamic_state_for_test()) else{continue;};
        let mut exact=state.clone();let mut oracle=state.clone();chains+=state.chains.len();
        for frame in 0..120 {
            let placement=trs([frame as f32*0.001,0.,0.],[0.,0.,0.,1.],[1.,1.,1.]);
            let dt=[0.,1./120.,1./60.,1./30.][frame%4];
            assert_eq!(exact.set_placement_for_step(&placement,dt>0.),oracle.set_placement(&placement));
            let restore=frame%2==0;
            if restore{exact.step(dt);}else{exact.step_on_animated_pose(dt);}oracle.step_impl_with_cache(dt,restore,false);
            assert_eq!(exact.particle_snapshot(),oracle.particle_snapshot());assert_eq!(exact.skel.worlds,oracle.skel.worlds);
        }
        assert!(exact.world_cache.is_empty());checked+=1;
    }
    assert!(checked>0&&chains>checked);eprintln!("private world scratch exact rigs={checked} chains={chains} frames=120");
}

fn tuple<const N:usize>(s:&str)->[f32;N] {
    let v:Vec<f32>=s.trim_matches(|c|c=='('||c==')').split(',').map(|x|x.trim().parse().unwrap()).collect();
    v.try_into().unwrap()
}
fn ids(s:&str)->Vec<i64> {if s.is_empty(){vec![]}else{s.split(',').map(|x|x.parse().unwrap()).collect()}}
fn fixture_directory()->std::path::PathBuf {std::env::var_os("OK_ACCESSORY_PHYSICS_FIXTURE").map(std::path::PathBuf::from).unwrap_or_else(||"C:/portable/OpenKoikatsu/Saved/Tests/Accessory318Native".into())}
fn quat_error(a:[f32;4],b:[f32;4])->f32 {
    let distance=|sign:f32|a.iter().zip(b).map(|(a,b)|(a-sign*b).abs()).fold(0.0f32,f32::max);
    distance(1.0).min(distance(-1.0))
}

#[test]
#[ignore="requires cached asset inputs and native accessory SetupParticles coefficients"]
fn native_accessory_asset_particle_parameters() {
    let inputs:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Logs/Accessory318CachedInputs.json").unwrap()).unwrap();
    let mut reports=Vec::new();
    let mut physics_reports=Vec::new();
    for (input,card) in inputs.as_array().unwrap().iter().zip([0,0,1]) {
        let source:Skeleton=serde_json::from_value(input["source_skeleton"].clone()).unwrap();
        let set:DBoneSet=serde_json::from_value(input["dynamics"].clone()).unwrap();
        let chain:Vec<Tf>=serde_json::from_value(input["attachment_chain"].clone()).unwrap();
        let state=AttachedDynamicSet::new_unsettled_with_chain(&set,&source,&chain).unwrap();
        let text=crate::gamefs::read_to_string(fixture_directory().join(format!("Card{card}.json.accessory-physics.tsv"))).unwrap();
        let rows:Vec<Vec<&str>>=text.lines().map(|line|line.split('\t').collect()).collect();
        let names:HashMap<i64,&str>=rows.iter().filter(|r|r[0]=="node").map(|r|(r[1].parse().unwrap(),r[4])).collect();
        let native_mesh=crate::gamefs::read_dir(fixture_directory()).unwrap().filter_map(Result::ok)
            .filter(|e|e.file_name().to_string_lossy().starts_with(&format!("Card{card}.json.mesh")))
            .filter_map(|e|serde_json::from_slice::<serde_json::Value>(&crate::gamefs::read(e.path()).unwrap()).ok())
            .find(|n|n["name"]==input["mesh"]).unwrap();
        assert_eq!(source.smr_count,native_mesh["bones"].as_array().unwrap().len());
        let aliases:HashMap<i64,&str>=source.bone_ids.iter().take(source.smr_count).zip(native_mesh["bones"].as_array().unwrap()).map(|(id,b)|(*id,b["name"].as_str().unwrap())).collect();
        let native_tfs:HashMap<i64,Tf>=rows.iter().filter(|r|r[0]=="node").map(|r| {
            let parent:i64=r[2].parse().unwrap();(r[1].parse().unwrap(),Tf{name:r[4].into(),parent:(parent!=0).then_some(parent),sibling_index:Some(r[3].parse().unwrap()),pos:tuple(r[5]),rot:tuple(r[6]),scale:tuple(r[7])})
        }).collect();
        for chain in &state.chains {
            let source_name=&source.tfs[&chain.db.root].name;
            let live_name=aliases.get(&chain.db.root).copied().unwrap_or(source_name);
            let Some(db)=rows.iter().find(|r|r[0]=="db"&&names[&r[3].parse::<i64>().unwrap()]==live_name) else {continue;};
            let expected:Vec<_>=rows.iter().filter(|r|r[0]=="particle"&&r[1]=="installed"&&r[2]==db[1]).collect();
            assert_eq!(chain.particles.len(),expected.len());
            let mut coefficient_max=0f32;let mut initial_world_max=0f32;let mut local_init_max=0f32;let mut init_rotation_max=0f32;
            let mut coefficients=Vec::new();
            for (p,row) in chain.particles.iter().zip(expected) {
                let id:i64=row[4].parse().unwrap();
                // Runtime split prefabs rename non-SMR helper children too.
                // SMR slot aliases establish the root; verify remaining nodes
                // through native particle parent/order and their actual TRS.
                if let Some(alias)=p.id.and_then(|id|aliases.get(&id)) {assert_eq!(*alias,names[&id]);}
                assert_eq!(p.id.is_some(),id!=0);assert_eq!(p.parent,row[5].parse::<i32>().unwrap());
                let actual=[p.damping,p.elasticity,p.stiffness,p.inert,p.radius];
                let native:[f32;5]=std::array::from_fn(|i|row[11+i].parse().unwrap());
                let error=actual.iter().zip(native).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max);coefficient_max=coefficient_max.max(error);
                initial_world_max=initial_world_max.max(vlen(vsub(p.pos,tuple(row[6]))));local_init_max=local_init_max.max(vlen(vsub(p.init_local_pos,tuple(row[9]))));
                init_rotation_max=init_rotation_max.max(quat_error(p.init_local_rot,tuple(row[10])));
                coefficients.push(serde_json::json!({"actual":actual,"native":native,"max":error,"source_name":p.id.map(|id|source.tfs[&id].name.as_str()),"native_name":(id!=0).then(||names[&id]),"source_bone_length":p.bone_length,"native_bone_length":row[16].parse::<f32>().unwrap(),"actual_initial_world":p.pos,"native_initial_world":tuple::<3>(row[6]),"actual_init_rotation":p.init_local_rot,"native_init_rotation":tuple::<4>(row[10])}));
            }
            eprintln!("asset accessory mesh={} root={source_name} particles={} coefficients={coefficient_max:.9} initial_world={initial_world_max:.9}m init_local={local_init_max:.9}m",input["mesh"],chain.particles.len());
            reports.push(serde_json::json!({"mesh":input["mesh"],"root":source_name,"particles":chain.particles.len(),"coefficient_max":coefficient_max,"initial_world_max_m":initial_world_max,"init_local_max_m":local_init_max,"init_rotation_max":init_rotation_max,"coefficients":coefficients}));
        }
        for mode in ["installed","no_colliders"] {
            let mut state=AttachedDynamicSet::new_unsettled_with_chain(&set,&source,&chain).unwrap();
            let mut mappings=Vec::new();
            for (index,chain) in state.chains.iter_mut().enumerate() {
                let live_name=aliases.get(&chain.db.root).copied().unwrap_or(&source.tfs[&chain.db.root].name);
                let Some(db)=rows.iter().find(|r|r[0]=="db"&&names[&r[3].parse::<i64>().unwrap()]==live_name) else {continue;};
                let expected:Vec<_>=rows.iter().filter(|r|r[0]=="particle"&&r[1]=="installed"&&r[2]==db[1]).collect();
                for (p,row) in chain.particles.iter().zip(expected) {if let Some(id)=p.id {mappings.push((id,row[4].parse::<i64>().unwrap()));}}
                if mode=="installed" {for collider_id in ids(db[17]) {
                    let r=rows.iter().find(|r|r[0]=="collider"&&r[1].parse::<i64>().unwrap()==collider_id).unwrap();
                    let collider=BodyCollider{transform:r[2].parse().unwrap(),center:tuple(r[3]),radius:r[4].parse().unwrap(),height:r[5].parse().unwrap(),direction:r[6].parse().unwrap(),inside:r[7]=="True"};
                    let world=world_of(collider.transform,&native_tfs,&mut HashMap::new());
                    let scale_z=transform_lossy_scale(collider.transform,&native_tfs,&world)[2].abs();chain.external_colliders.push((collider,world,scale_z));
                }}
                // These are the actual installed world-space collider controls,
                // not a replacement for character collider policy validation.
                chain.colliders.clear();let _=index;
            }
            let mut previous=0;
            for frame in [0,1,60,240] {
                for _ in previous..frame {state.step(1./60.);}previous=frame;
                let mut local_max=0f32;let mut world_max=0f32;let mut rotation_max=0f32;
                for (source_id,native_id) in &mappings {
                    let row=rows.iter().find(|r|r[0]=="result"&&r[1]==mode&&r[2].parse::<usize>().unwrap()==frame&&r[3].parse::<i64>().unwrap()==*native_id).unwrap();
                    let tf=&state.skel.tfs[source_id];local_max=local_max.max(vlen(vsub(tf.pos,tuple(row[4]))));rotation_max=rotation_max.max(quat_error(tf.rot,tuple(row[5])));
                    world_max=world_max.max(vlen(vsub(parts_pos(&state.skel.tfs,*source_id),tuple(row[6]))));
                }
                eprintln!("asset physics mesh={} mode={mode} frame={frame} transforms={} local={local_max:.9} world={world_max:.9} rotation={rotation_max:.9}",input["mesh"],mappings.len());
                physics_reports.push(serde_json::json!({"mesh":input["mesh"],"mode":mode,"frame":frame,"transforms":mappings.len(),"local_max_m":local_max,"world_max_m":world_max,"quaternion_component_max":rotation_max}));
            }
        }
    }
    let passed=reports.iter().all(|r|r["coefficient_max"].as_f64().unwrap()<2e-5&&r["initial_world_max_m"].as_f64().unwrap()<2e-5&&r["init_local_max_m"].as_f64().unwrap()<2e-5&&r["init_rotation_max"].as_f64().unwrap()<2e-5);
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Accessory318AssetParametersAudit.json",serde_json::to_vec_pretty(&serde_json::json!({"passed":passed,"rows":reports,"scope":"actual decoded source graph/card placement and distribution coefficients versus installed SetupParticles; no simulation"})).unwrap()).unwrap();
    let physics_passed=physics_reports.iter().all(|r|r["local_max_m"].as_f64().unwrap()<2e-5&&r["world_max_m"].as_f64().unwrap()<2e-5&&r["quaternion_component_max"].as_f64().unwrap()<2e-5);
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Accessory318SourcePhysicsAudit.json",serde_json::to_vec_pretty(&serde_json::json!({"passed":physics_passed,"rows":physics_reports,"scope":"decoded asset/card curves and complete socket Transform ancestry; actual native world collider controls; native steps 0/1/60/240; collider policy selection and free-running screenshot timing not covered"})).unwrap()).unwrap();
    assert_eq!(reports.len(),5,"two tie chains and three selected partial hair chains; unmatched names require investigation");
    assert!(passed,"source initial graph or distribution coefficients differ; inspect preserved report");
    assert!(physics_passed,"source fixed-step physics differs; inspect saved report");
}

#[test]
fn attached_ancestry_preserves_shear_and_particle_history() {
    let tf=|name:&str,parent,pos,rot,scale|Tf{name:name.into(),parent,sibling_index:None,pos,rot,scale};
    let mut source=Skeleton{names:vec!["root".into(),"tip".into()],worlds:vec![],rest:vec![ident();2],smr_count:2,
        tfs:HashMap::from([(1,tf("root",None,[0.,1.,0.],[0.,0.,0.,1.],[1.;3])),(2,tf("tip",Some(1),[0.,1.,0.],[0.,0.,0.,1.],[1.;3]))]),bone_ids:vec![1,2],source_mesh:0};source.rebuild_worlds();
    let angle=std::f32::consts::FRAC_PI_8;
    let mut chain=vec![tf("scaled_socket",None,[0.;3],[0.,0.,0.,1.],[2.,1.,1.]),tf("rotated_socket",None,[0.;3],[0.,0.,angle.sin(),angle.cos()],[1.;3])];
    let set=DBoneSet{bones:vec![DBone{root:1,component_transform:Some(1),force:[0.,-0.001,0.],..Default::default()}],..Default::default()};
    let mut exact=AttachedDynamicSet::new_unsettled_with_chain(&set,&source,&chain).unwrap();
    let actual=exact.chains[0].particles[1].pos;let expected=[-2.*2f32.sqrt(),2f32.sqrt(),0.];
    assert!(vlen(vsub(actual,expected))<1e-6,"two real ancestors preserve the sheared world");
    let placement=mat_mul_pub(&trs(chain[0].pos,chain[0].rot,chain[0].scale),&trs(chain[1].pos,chain[1].rot,chain[1].scale));
    let approximate=AttachedDynamicSet::new_unsettled(&set,&source,&placement).unwrap();
    assert!(vlen(vsub(approximate.chains[0].particles[1].pos,expected))>0.1,"negative control: one TRS loses the socket shear");
    exact.step(1./60.);let history:Vec<_>=exact.chains[0].particles.iter().map(|p|(p.pos,p.prev)).collect();
    chain[0].pos=[0.2,0.,0.];assert!(exact.set_attachment_chain(&chain));
    assert_eq!(history,exact.chains[0].particles.iter().map(|p|(p.pos,p.prev)).collect::<Vec<_>>(),"animated socket updates must retain particle history");
    exact.step(1./60.);assert!((exact.chains[0].particles[0].pos[0]-(-2f32.sqrt()+0.2)).abs()<1e-6);
    assert_eq!(source.rest,vec![ident();2],"attachment ancestry must not rewrite mesh binds/rest");
    let mut oracle=exact.clone();let mut deferred=exact.clone();
    let names:Vec<_>=exact.attachment_ids.iter().map(|id|exact.skel.tfs[id].name.as_ptr()).collect();
    for frame in 0..60 {
        chain[0].pos[0]=frame as f32*0.003;chain[1].sibling_index=Some(frame);
        let mut parent=Some(oracle.parent_id);
        for (id,tf) in oracle.attachment_ids.iter().zip(&chain) {
            let mut tf=tf.clone();tf.parent=parent;oracle.skel.tfs.insert(*id,tf);parent=Some(*id);
        }
        oracle.skel.rebuild_worlds();assert!(exact.set_attachment_chain(&chain));
        assert!(deferred.set_attachment_chain_for_step(&chain,true));
        assert_eq!(exact.skel.worlds,oracle.skel.worlds);
        for ((id,name),reference) in exact.attachment_ids.iter().zip(&names).zip(&oracle.attachment_ids) {
            assert_eq!(exact.skel.tfs[id].name.as_ptr(),*name,"reuse attachment name allocation");
            assert_eq!(serde_json::to_value(&exact.skel.tfs[id]).unwrap(),serde_json::to_value(&oracle.skel.tfs[reference]).unwrap());
        }
        let dt=[1./120.,1./60.,1./30.][frame as usize%3];
        exact.step(dt);oracle.step_impl_with_cache(dt,true,false);assert_eq!(exact.particle_snapshot(),oracle.particle_snapshot());
        deferred.step(dt);assert_eq!(deferred.particle_snapshot(),oracle.particle_snapshot());assert_eq!(deferred.skel.worlds,oracle.skel.worlds);
        assert_eq!(exact.skel.worlds,oracle.skel.worlds);
    }
}

#[test]
#[ignore="requires actual accessory fixed-step installed DynamicBone fixtures"]
fn native_accessory_fixed_step_solver() {
    let root=fixture_directory();
    let mut reports=Vec::new();
    for card in 0..2 {
        let text=crate::gamefs::read_to_string(root.join(format!("Card{card}.json.accessory-physics.tsv"))).unwrap();
        let rows:Vec<Vec<&str>>=text.lines().map(|line|line.split('\t').collect()).collect();
        let tfs=rows.iter().filter(|r|r[0]=="node").map(|r| {
            let parent:i64=r[2].parse().unwrap();
            (r[1].parse().unwrap(),Tf {name:r[4].into(),parent:(parent!=0).then_some(parent),sibling_index:Some(r[3].parse().unwrap()),pos:tuple(r[5]),rot:tuple(r[6]),scale:tuple(r[7])})
        }).collect();
        let source=Skeleton {names:vec![],worlds:vec![],rest:vec![],smr_count:0,tfs,bone_ids:vec![],source_mesh:0};
        let colliders:HashMap<i64,BodyCollider>=rows.iter().filter(|r|r[0]=="collider").map(|r| (r[1].parse().unwrap(),BodyCollider {
            transform:r[2].parse().unwrap(),center:tuple(r[3]),radius:r[4].parse().unwrap(),height:r[5].parse().unwrap(),direction:r[6].parse().unwrap(),inside:r[7]=="True"
        })).collect();
        let bones:Vec<_>=rows.iter().filter(|r|r[0]=="db").map(|r| {
            assert_eq!(r[18].parse::<f32>().unwrap(),1.0,"fixture requires full native weight");
            DBone {component_transform:Some(r[2].parse().unwrap()),root:r[3].parse().unwrap(),update_rate:r[4].parse().unwrap(),damping:r[5].parse().unwrap(),elasticity:r[6].parse().unwrap(),stiffness:r[7].parse().unwrap(),inert:r[8].parse().unwrap(),radius:r[9].parse().unwrap(),gravity:tuple(r[10]),force:tuple(r[11]),end_length:r[12].parse().unwrap(),end_offset:tuple(r[13]),freeze_axis:r[14].parse().unwrap(),exclusions:ids(r[15]),not_rolls:ids(r[16]),collider_refs:ids(r[17]),..Default::default()}
        }).collect();
        assert!(!bones.is_empty());
        for mode in ["installed","no_colliders"] {
            let mut set=DBoneSet {bones:bones.clone(),colliders:colliders.clone(),..Default::default()};
            if mode=="no_colliders" {for db in &mut set.bones {db.collider_refs.clear();}}
            let mut state=AttachedDynamicSet::new_unsettled(&set,&source,&ident()).unwrap();
            let mut particles_checked=0;
            for row in rows.iter().filter(|r|r[0]=="particle"&&r[1]==mode) {
                let p=&mut state.chains[row[2].parse::<usize>().unwrap()].particles[row[3].parse::<usize>().unwrap()];
                let id:i64=row[4].parse().unwrap();assert_eq!(p.id,(id!=0).then_some(id));assert_eq!(p.parent,row[5].parse::<i32>().unwrap());
                assert!(vlen(vsub(p.pos,tuple(row[6])))<2e-6,"initial particle world position");
                assert!(vlen(vsub(p.end_offset,tuple(row[8])))<2e-6,"virtual end offset");
                assert!(vlen(vsub(p.init_local_pos,tuple(row[9])))<2e-6);
                assert!(quat_error(p.init_local_rot,tuple(row[10]))<2e-6);
                // Isolate the solver using the installed SetupParticles output.
                // Native distribution curves are intentionally not approximated.
                p.damping=row[11].parse().unwrap();p.elasticity=row[12].parse().unwrap();p.stiffness=row[13].parse().unwrap();p.inert=row[14].parse().unwrap();p.radius=row[15].parse().unwrap();
                assert!((p.bone_length-row[16].parse::<f32>().unwrap()).abs()<2e-6);
                particles_checked+=1;
            }
            assert_eq!(particles_checked,state.chains.iter().map(|c|c.particles.len()).sum::<usize>());
            let mut previous=0;
            for frame in [0,1,60,240] {
                for _ in previous..frame {state.step(1.0/60.0);}previous=frame;
                let mut local_max=0f32;let mut world_max=0f32;let mut rotation_max=0f32;let mut count=0;
                for row in rows.iter().filter(|r|r[0]=="result"&&r[1]==mode&&r[2].parse::<usize>().unwrap()==frame) {
                    let id:i64=row[3].parse().unwrap();let tf=&state.skel.tfs[&id];
                    local_max=local_max.max(vlen(vsub(tf.pos,tuple(row[4]))));
                    rotation_max=rotation_max.max(quat_error(tf.rot,tuple(row[5])));
                    world_max=world_max.max(vlen(vsub(parts_pos(&state.skel.tfs,id),tuple(row[6]))));count+=1;
                }
                assert_eq!(count,source.tfs.len());
                eprintln!("native accessory card={card} mode={mode} frame={frame} particles={particles_checked} transforms={count} local={local_max:.9}m world={world_max:.9}m rotation={rotation_max:.9}");
                reports.push(serde_json::json!({"card":card,"mode":mode,"frame":frame,"particles":particles_checked,"transforms":count,"local_max_m":local_max,"world_max_m":world_max,"quaternion_component_max":rotation_max}));
            }
        }
    }
    let passed=reports.iter().all(|r|r["local_max_m"].as_f64().unwrap()<2e-5&&r["world_max_m"].as_f64().unwrap()<2e-5&&r["quaternion_component_max"].as_f64().unwrap()<2e-5);
    let output=std::env::var("OK_ACCESSORY_SOLVER_REPORT").unwrap_or_else(|_|"C:/portable/OpenKoikatsu/Saved/Logs/Accessory318SolverAudit.json".into());
    std::fs::write(output,serde_json::to_vec_pretty(&serde_json::json!({"passed":passed,"rows":reports,"scope":"fixed native Transform graph, installed particle coefficients and collider refs; isolated ordinary solver; asset/card parameter parsing and free-running screenshot timing not covered"})).unwrap()).unwrap();
    assert!(passed,"actual accessory fixed-step solver exceeded unchanged 2e-5 gates; see saved audit");
}
