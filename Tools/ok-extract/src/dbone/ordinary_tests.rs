use super::*;

#[test]
fn collision_bounds_match_original_narrow_phase(){
    let mut seed=17u32;
    let mut random=||{seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);(seed>>8) as f32/16777216.};
    for inside in [false,true] {for direction in 0..3 {for scale in [0.,0.3,1.,8.] {for radius in [-2.,0.,2.] {
        let c=BodyCollider{transform:1,center:[-1.,0.4,2.],radius,height:15.,direction,inside};
        let w=trs([20.,-40.,80.],quat_norm([0.2,0.3,-0.4,0.8]),[-0.7,1.4,0.5]);let prepared=PreparedCollision::new(&c,&w,scale);
        for _ in 0..1024 {
            let mut actual=[random()*160.-60.,random()*160.-120.,random()*160.];let mut expected=actual;let particle_radius=random()*3.;
            prepared.collide(&mut actual,particle_radius);collide_particle_world(&mut expected,particle_radius,&c,&w,scale);
            assert_eq!(actual.map(f32::to_bits),expected.map(f32::to_bits));
        }
        for axis in 0..3 {for side in [prepared.low[axis],prepared.high[axis]] {for direction in [-1.,1.] {
            let mut actual=prepared.a;actual[axis]=side+direction*(prepared.scaled_radius+0.2);let mut expected=actual;
            prepared.collide(&mut actual,0.2);collide_particle_world(&mut expected,0.2,&c,&w,scale);
            assert_eq!(actual.map(f32::to_bits),expected.map(f32::to_bits));
        }}}
    }}}}
    // Invalid/non-finite endpoints retain the existing comparison behavior.
    let c=BodyCollider{transform:1,center:[0.;3],radius:1.,height:8.,direction:1,inside:false};
    for x in [f32::NAN,f32::INFINITY,1e30] {let mut w=ident();w[0][3]=x;let prepared=PreparedCollision::new(&c,&w,1.);
        let mut actual=[0.1,1.,2.];let mut expected=actual;prepared.collide(&mut actual,0.3);collide_particle_world(&mut expected,0.3,&c,&w,1.);
        assert_eq!(actual.map(f32::to_bits),expected.map(f32::to_bits));
    }
}

#[test]
#[ignore="release-only collision hot path benchmark"]
fn collision_bounds_benchmark(){
    let mut colliders:Vec<_>=(0..48).map(|i|{
        let c=BodyCollider{transform:1,center:[0.;3],radius:3.,height:12.,direction:1,inside:false};
        PreparedCollision::new(&c,&trs([(i%4) as f32*12.-18.,0.,i as f32*4.],[0.,0.,0.,1.],[1.;3]),1.)
    }).collect();
    for bounded in [false,true,false,true] {
        for c in &mut colliders{c.bounded=bounded;}
        let start=std::time::Instant::now();let mut sum=0.;
        for frame in 0..1200 {for i in 0..32 {
            let mut pos=[i as f32*3.-48.,frame as f32*0.001,130.+i as f32];
            for c in &colliders{c.collide(&mut pos,0.3);}
            sum+=std::hint::black_box(pos[0]+pos[1]+pos[2]);
        }}
        eprintln!("COLLISION BENCH bounded={bounded} ms={:.3} checksum={sum}",start.elapsed().as_secs_f64()*1000.);
    }
}

#[test]
fn shared_pose_allocation_reuse_matches_full_clone(){
    let tf=|id,parent|Tf{name:format!("n{id}"),parent,sibling_index:Some(id as u32),
        pos:[id as f32*0.001,0.1,0.],rot:[0.,0.,0.,1.],scale:[1.1,0.9,1.2]};
    let mut source=Skeleton{names:(1..=64).map(|id|format!("n{id}")).collect(),bone_ids:(1..=64).collect(),
        worlds:vec![],rest:vec![ident();64],smr_count:64,source_mesh:17,
        tfs:(1..=64).map(|id|(id,tf(id,if id==1{None}else{Some(id/2)}))).collect()};source.rebuild_worlds();
    let set=DBoneSet{bones:vec![DBone{root:2,component_transform:Some(1),force:[0.,-0.001,0.],..Default::default()}],..Default::default()};
    let mut actual=AttachedDynamicSet::new_unsettled(&set,&source,&ident()).unwrap();
    let mut bridge=tf(1000,Some(1));bridge.name="__ok_body_bridge/n4".into();actual.shared_component_tfs.insert(1000,bridge);
    let mut oracle=actual.clone();
    for frame in 0..120 {
        if frame==30{actual.skel.tfs.reserve(2048);oracle.skel.tfs.reserve(2048);}
        if frame==35{for state in [&mut actual,&mut oracle]{state.skel.tfs.insert(2000,tf(2000,Some(4)));}}
        if frame==75{actual.skel.tfs.remove(&2000);oracle.skel.tfs.remove(&2000);}
        for state in [&mut actual,&mut oracle]{state.skel.tfs.get_mut(&5).unwrap().parent=Some(if frame%2==0{2}else{3});}
        if frame==40{source.tfs.insert(65,tf(65,Some(4)));}if frame==80{source.tfs.remove(&65);}
        source.tfs.get_mut(&20).unwrap().parent=Some(if frame%2==0{4}else{5});
        source.tfs.get_mut(&1).unwrap().pos[0]=frame as f32*0.003;source.rebuild_worlds();
        actual.sync_shared_pose(&source);oracle.sync_shared_pose_impl(&source,false);
        assert_eq!(serde_json::to_value(&actual.skel).unwrap(),serde_json::to_value(&oracle.skel).unwrap());
        let dt=[0.,1./120.,1./30.][frame%3];actual.step_on_animated_pose(dt);oracle.step_impl_with_cache(dt,false,false);
        assert_eq!(actual.particle_snapshot(),oracle.particle_snapshot());assert_eq!(actual.skel.worlds,oracle.skel.worlds);
    }
}

#[test]
fn subtree_world_invalidation_matches_full_clear_with_reparenting(){
    let tf=|id,parent|Tf{name:format!("node{id}"),parent,sibling_index:Some(id as u32),
        pos:[id as f32*0.13,-0.27,0.18],rot:[0.,0.,0.258819,0.9659258],scale:[1.4,0.7,-1.1]};
    let initial:HashMap<_,_>=[(1,tf(1,None)),(2,tf(2,Some(1))),(3,tf(3,Some(1))),
        (4,tf(4,Some(2))),(5,tf(5,Some(3))),(6,tf(6,Some(0))),(7,tf(7,Some(7)))].into_iter().collect();
    let mut optimized=initial.clone();let mut original=initial;let mut cache=HashMap::new();let mut oracle=HashMap::new();let mut rotations=HashMap::new();
    for iteration in 0..120 {
        // Rebuild from the current complete parent inventory, as the live
        // solver does after external attachment/implant configuration.
        let parent=if iteration%2==0 {2}else{3};
        for tfs in [&mut optimized,&mut original]{tfs.get_mut(&4).unwrap().parent=Some(parent);}
        cache.clear();oracle.clear();rotations.clear();let mut children:HashMap<i64,Vec<i64>>=HashMap::new();
        for (id,node) in &optimized{if let Some(parent)=node.parent.filter(|p|*p!=0&&*p!=*id){children.entry(parent).or_default().push(*id);}}
        for id in 1..=8{assert_eq!(world_of(id,&optimized,&mut cache),world_of(id,&original,&mut oracle));
            assert_eq!(scoped_world_rot(id,&optimized,&mut rotations).map(f32::to_bits),world_rot_of(id,&original).map(f32::to_bits));}
        for id in [2,4,1,3,5,6,7]{
            let pos=[iteration as f32*0.007,id as f32*0.04,-0.3];
            set_world_pos_with_children(&mut optimized,&mut cache,id,pos,Some(&children));set_world_pos(&mut original,&mut oracle,id,pos);
            for probe in 1..=7{assert_eq!(world_of(probe,&optimized,&mut cache),world_of(probe,&original,&mut oracle));}
            let angle=iteration as f32*0.003;let rotation=[angle.sin(),0.,0.,angle.cos()];
            set_world_rot_scoped(&mut optimized,&mut cache,id,rotation,Some(&children),Some(&mut rotations));set_world_rot(&mut original,&mut oracle,id,rotation);
            for probe in 1..=8{assert_eq!(world_of(probe,&optimized,&mut cache),world_of(probe,&original,&mut oracle));
                assert_eq!(scoped_world_rot(probe,&optimized,&mut rotations).map(f32::to_bits),world_rot_of(probe,&original).map(f32::to_bits));}
        }
    }
}

#[test]
#[ignore="release-only scoped rotation hot path benchmark"]
fn scoped_rotation_benchmark(){
    let mut skel=Skeleton{names:vec![],bone_ids:vec![],worlds:vec![],rest:vec![],smr_count:0,source_mesh:0,
        tfs:(1..=96).map(|id|(id,Tf{name:format!("n{id}"),parent:if id==1{None}else{Some(id-1)},sibling_index:None,
            pos:[0.001,0.025,0.],rot:[0.,0.,0.015,0.9998875],scale:[1.;3]})).collect()};skel.rebuild_worlds();
    let db=DBone{root:72,component_transform:Some(1),force:[0.,-0.001,0.],..Default::default()};
    let mut particles=Vec::new();let mut cache=HashMap::new();gather_dparticles(&skel.tfs,Some(db.root),-1,0.,&db,1,&mut particles,&mut cache);
    let children:HashMap<i64,Vec<i64>>=(1..96).map(|id|(id,vec![id+1])).collect();
    let counts:HashMap<i64,usize>=children.iter().map(|(id,ids)|(*id,ids.len())).collect();
    for cached in [false,true,false,true]{
        let mut actor=skel.clone();let mut particles=particles.clone();let mut rotations=HashMap::new();cache.clear();
        let start=std::time::Instant::now();
        for _ in 0..2000{cache.clear();rotations.clear();
            apply_particles_scoped(&mut actor,&mut particles,&mut cache,1,false,Some(&counts),Some(&children),if cached{Some(&mut rotations)}else{None});
        }
        eprintln!("ROTATION SCOPE BENCH cached={cached} ms={:.3} checksum={:?}",start.elapsed().as_secs_f64()*1000.,std::hint::black_box(actor.tfs[&96].rot));
    }
}

#[test]
fn lossy_scale_removes_rotated_ancestor_shear(){
    let tf=|parent,rot,scale|Tf{name:"scale_control".into(),parent,sibling_index:None,
        pos:[0.;3],rot,scale};
    let angle=std::f32::consts::PI/12.;
    let tfs=[(1,tf(None,[0.,0.,0.,1.],[2.,1.,1.])),
        (2,tf(Some(1),[0.,0.,angle.sin(),angle.cos()],[1.;3]))].into_iter().collect();
    let world=world_of(2,&tfs,&mut HashMap::new());
    let scale=transform_lossy_scale(2,&tfs,&world);
    for (actual,expected) in scale.into_iter().zip([1.75,1.25,1.]){assert!((actual-expected).abs()<3e-7);}
    assert!((vlen([world[0][0],world[1][0],world[2][0]])-scale[0]).abs()>0.05,
        "column length contains shear, native lossyScale projects out rotation");
}

#[test]
fn ordinary_scaled_cloth_history_lengths_and_collision_response(){
    let tf=|name:&str,parent,pos|Tf{name:name.into(),parent,sibling_index:None,pos,
        rot:[0.,0.,0.,1.],scale:[1.;3]};
    let mut animated=Skeleton{names:vec![],bone_ids:vec![],worlds:vec![],rest:vec![],smr_count:0,source_mesh:0,
        tfs:[(1,tf("height",None,[0.;3])),(2,tf("cloth_root",Some(1),[0.;3])),
            (3,tf("cloth_tip",Some(2),[0.,1.,0.]))].into_iter().collect()};
    let set=DBoneSet{bones:vec![DBone{root:2,component_transform:Some(1),update_rate:60.,
        damping:0.2,elasticity:0.1,stiffness:0.8,..Default::default()}],..Default::default()};
    let mut contact=AttachedDynamicSet::new_unsettled(&set,&animated,&ident()).unwrap();
    let untouched=contact.clone();let untouched_positions=untouched.audit_particle_positions();
    let mut clear=contact.clone();let mut observations=0;let mut min_contact=f32::MAX;
    for scale in [1f32,0.6,1.8,1.]{
        animated.tfs.get_mut(&1).unwrap().scale=[scale;3];animated.rebuild_worlds();
        let world=world_of(1,&animated.tfs,&mut HashMap::new());
        contact.chains[0].external_colliders=vec![(BodyCollider{transform:1,radius:0.35,height:0.,
            center:[0.1,0.8,0.],direction:1,inside:false},world,scale)];
        for _ in 0..60{
            for state in [&mut contact,&mut clear]{
                state.sync_shared_pose(&animated);state.step_on_animated_pose(1./60.);
                let particles=&state.chains[0].particles;
                assert_eq!(particles.len(),2);
                assert!(particles.iter().flat_map(|p|p.pos).all(f32::is_finite));
                let length=vlen(vsub(particles[1].pos,particles[0].pos));
                assert!((length-scale).abs()<2e-6,"current scaled chain length: {length} vs {scale}");
                observations+=1;
            }
            assert_eq!(untouched.audit_particle_positions(),untouched_positions);
        }
        let delta=vlen(vsub(contact.chains[0].particles[1].pos,clear.chains[0].particles[1].pos));
        min_contact=min_contact.min(delta/scale);
        assert!(delta>0.01*scale,"scaled collision response positive control {delta}");
    }
    eprintln!("scaled ordinary cloth: observations={observations} min_normalized_collision_delta_m={min_contact:.9} isolation=1");
}

fn tuple<const N: usize>(s: &str) -> [f32; N] {
    s.trim_matches(|c| c == '(' || c == ')').split(',').map(|s| s.trim().parse().unwrap())
        .collect::<Vec<_>>().try_into().unwrap()
}
fn fixture(path: &str) -> (Skeleton, HashMap<String, i64>) {
    let rows: Vec<_> = crate::gamefs::read_to_string(path).unwrap().lines().map(|s| s.split('\t').map(str::to_owned).collect::<Vec<_>>()).collect();
    let ids: HashMap<_, _> = rows.iter().enumerate().map(|(i, r)| (r[2].clone(), i as i64 + 1)).collect();
    let tfs = rows.into_iter().map(|r| {
        let id = ids[&r[2]];
        let parent = r[2].rsplit_once('/').and_then(|(p, _)| ids.get(p).copied());
        (id, Tf { sibling_index: None, name: r[1].clone(), parent, pos: tuple(&r[3]), rot: tuple(&r[4]), scale: tuple(&r[5]) })
    }).collect();
    (Skeleton { names: Vec::new(), bone_ids: Vec::new(), worlds: Vec::new(), rest: Vec::new(), smr_count: 0, tfs, source_mesh: 0 }, ids)
}

#[test]
fn ordinary_shared_rig_keeps_post_update_animator_binding() {
    let tfs = [(1, Tf { sibling_index: None, name: "root".into(), parent: None, pos: [0.0; 3], rot: [0.0,0.0,0.0,1.0], scale: [1.0; 3] }),
        (2, Tf { sibling_index: None, name: "animated_tip".into(), parent: Some(1), pos: [0.0,1.0,0.0], rot: [0.0,0.0,0.0,1.0], scale: [1.0; 3] })].into_iter().collect();
    let source = Skeleton { names: vec![], bone_ids: vec![], worlds: vec![], rest: vec![], smr_count: 0, tfs, source_mesh: 0 };
    let set = DBoneSet { bones: vec![DBone { root: 1, stiffness: 1.0, update_rate: 60.0, ..Default::default() }], colliders: HashMap::new(), ..Default::default() };
    let mut state = AttachedDynamicSet::new_unsettled(&set, &source, &crate::pose::ident()).unwrap();
    let mut animated = source.clone(); animated.tfs.get_mut(&2).unwrap().pos = [0.5, 1.0, 0.0];
    state.sync_shared_pose(&animated); state.step_on_animated_pose(1.0/60.0); state.copy_simulated_pose(&mut animated);
    assert!(vlen(vsub(animated.tfs[&2].pos, [0.5,1.0,0.0])) < 1e-6, "LateUpdate must retain Animator curves after DynamicBone.Update restores the baseline");
}

fn clothes_fixture() -> (Skeleton, Skeleton, crate::unity::ExtractedMesh) {
    let tf = |name: &str, parent, x| Tf { sibling_index: None, name: name.into(), parent,
        pos: [x,0.0,0.0], rot: [0.0,0.0,0.0,1.0], scale: [1.0;3] };
    let mut source = Skeleton { names: vec![], bone_ids: vec![2,3,2], worlds: vec![], rest: vec![],
        smr_count: 3, source_mesh: 0, tfs: [
            (1, tf("prefab", None, 0.25)), (4, tf("cf_j_root", Some(1), 0.0)),
            (2, tf("borrowed", Some(4), 1.0)), (3, tf("private", Some(1), 2.0)),
            (5, tf("private_tip", Some(3), 0.0)),
            (6, tf("removed_private", Some(4), 0.0)),
        ].into_iter().collect() };
    source.tfs.get_mut(&5).unwrap().pos = [0.0,1.0,0.0]; source.rebuild_worlds(); source.rest = source.worlds.clone();
    let mut body = Skeleton { names: vec![], bone_ids: vec![200], worlds: vec![], rest: vec![],
        smr_count: 1, source_mesh: 0, tfs: [(200, tf("borrowed", None, 10.0))].into_iter().collect() };
    body.rebuild_worlds(); body.rest = body.worlds.clone();
    let mut distinct_bind = crate::pose::ident(); distinct_bind[0][3] = 0.1;
    let mesh = crate::unity::ExtractedMesh { name: "arbitrary_mod_mesh".into(),
        positions: vec![[0.0;3];3], rest_positions: vec![[0.0;3];3],
        normals: vec![[0.0,0.0,1.0];3], rest_normals: vec![[0.0,0.0,1.0];3],
        weights: vec![[1.0,0.0,0.0,0.0];3],
        bone_idx: vec![[0,0,0,0],[1,0,0,0],[2,0,0,0]],
        bone_names: source.names.clone(), bind_poses: vec![crate::pose::ident(),crate::pose::ident(),distinct_bind],
        smr_skel: Some(source.clone()), ..Default::default() };
    (source, body, mesh)
}

#[test]
fn clothing_private_roots_follow_source_copy_and_destroy_rules() {
    let (source, body, _) = clothes_fixture();
    let set = DBoneSet { bones: vec![
        DBone { root: 2, ..Default::default() }, // ordinary matching root -> shared
        DBone { root: 3, ..Default::default() }, // surviving missing root -> private
        DBone { root: 6, ..Default::default() }, // deleted cf_j_root subtree -> no live root
        DBone { root: 3, ver01: true, ..Default::default() },
    ], colliders: HashMap::new(), ..Default::default() };
    let shared = set.retarget_clothes(&source, &body, "ct_clothesBot", None);
    let private = set.private_clothes(&source, &body, true, "ct_clothesBot");
    assert_eq!(shared.len(), 1); assert_eq!(shared.first_root(), Some(200));
    assert_eq!(private.len(), 2); assert!(private.bones.iter().all(|db| db.root == 3));
    assert!(private.bones.iter().any(|db| db.ver01));
    // copyDynamicBone=false does not migrate a matching ordinary root that
    // survives outside the cloned, deleted cf_j_root subtree.
    let mut outside = source.clone(); outside.tfs.get_mut(&2).unwrap().parent = Some(1);
    assert_eq!(set.private_clothes(&outside, &body, false, "ct_bra").len(), 3);
}

#[test]
fn clothing_shared_root_retains_component_transform_ancestry() {
    let (mut source, mut body, _) = clothes_fixture();
    let root = source.tfs.get_mut(&1).unwrap();
    root.rot = [0.0,0.0,0.70710677,0.70710677]; root.scale = [2.0,1.0,0.5];
    let set = DBoneSet { bones: vec![DBone { root: 2, component_transform: Some(1),
        end_offset: [0.1,0.0,0.0], ..Default::default() }], colliders: HashMap::new(), ..Default::default() };
    let mut parent = crate::pose::ident(); parent[1][3] = 3.0;
    let shared = set.retarget_clothes(&source, &body, "ct_top_parts_A", Some(&parent));
    let expected = mat_mul_pub(&parent, &world_of(1,&source.tfs,&mut HashMap::new()));
    let mut state = AttachedDynamicSet::new_unsettled(&shared,&body,&crate::pose::ident()).unwrap();
    assert_eq!(state.chains[0].db.component_name, "ct_top_parts_A");
    assert_ne!(state.chains[0].component, 200);
    let component = state.chains[0].component;
    for _ in 0..2 {
        state.sync_shared_pose(&body);
        let world = world_of(component,&state.skel.tfs,&mut HashMap::new());
        assert!(world.iter().flatten().zip(expected.iter().flatten()).all(|(a,b)| (a-b).abs()<1e-6));
        body.tfs.get_mut(&200).unwrap().pos[0] += 10.0;
    }
    let particles = &state.chains[0].particles;
    assert!(vlen(vsub(particles[1].pos, [10.0,0.1,0.0])) < 2e-6, "EndOffset reads component rotation, not the copied body root");
}

#[test]
fn clothing_mixed_weights_keep_private_slots_and_distinct_binds() {
    let (_, mut body, mesh) = clothes_fixture();
    let mut runtime = crate::session::AccessoryRuntime::from_clothes_source(&mesh, None, &body, true, "ct_clothesBot", None, false).unwrap();
    let flags = game_colliders::Flags::default(); let native = BodyColliderSet::default();
    let (a, _) = runtime.update(Some(&body), None, &native, flags, 0.0).unwrap();
    assert!(vlen(vsub(a[0], [10.0,0.0,0.0])) < 1e-6);
    assert!(vlen(vsub(a[1], [2.25,0.0,0.0])) < 1e-6);
    assert!(vlen(vsub(a[2], [10.1,0.0,0.0])) < 1e-6, "same shared bone must retain separate bind slots");
    let bytes = bincode::serialize(&runtime).unwrap();
    let mut restored: crate::session::AccessoryRuntime = bincode::deserialize(&bytes).unwrap();
    body.tfs.get_mut(&200).unwrap().pos[0] = 20.0; body.rebuild_worlds();
    let (b, _) = restored.update(Some(&body), None, &native, flags, 1.0/60.0).unwrap();
    assert!(vlen(vsub(b[0], [20.0,0.0,0.0])) < 1e-6);
    assert!(vlen(vsub(b[1], a[1])) < 1e-6, "unmatched slot must not collapse to its shared ancestor");
    assert!(vlen(vsub(b[2], [20.1,0.0,0.0])) < 2e-6);
    let mut parent = crate::pose::ident(); parent[0][3] = 4.0;
    let mut nested = crate::session::AccessoryRuntime::from_clothes_source(&mesh, None, &body, true, "ct_top_parts_A", Some(&parent), false).unwrap();
    let (c, _) = nested.update(Some(&body), None, &native, flags, 0.0).unwrap();
    assert!(vlen(vsub(c[1], [6.25,0.0,0.0])) < 1e-6);
    assert!(vlen(vsub(c[0], b[0])) < 1e-6, "subpart parent applies only to its private slots");
}

#[test]
fn clothing_private_physics_moves_only_the_private_weight_slot() {
    let (mut source, body, mut mesh) = clothes_fixture();
    source.bone_ids = vec![2,5,2]; source.rebuild_worlds(); source.rest = source.worlds.clone();
    mesh.bone_names = source.names.clone(); mesh.smr_skel = Some(source);
    let mut rigid = crate::session::AccessoryRuntime::from_clothes_source(&mesh, None, &body, true, "ct_clothesBot", None, false).unwrap();
    mesh.deferred_dbones = Some(DBoneSet { bones: vec![DBone { root: 3, component_transform: Some(1),
        damping: 0.2, elasticity: 0.1, stiffness: 0.1, update_rate: 60.0, force: [0.03,0.0,0.0],
        ..Default::default() }], colliders: HashMap::new(), ..Default::default() });
    let mut dynamic = crate::session::AccessoryRuntime::from_clothes_source(&mesh, None, &body, true, "ct_clothesBot", None, false).unwrap();
    let flags = game_colliders::Flags::default(); let native = BodyColliderSet::default();
    let (a, _) = rigid.update(Some(&body), None, &native, flags, 0.0).unwrap();
    let (b, _) = dynamic.update(Some(&body), None, &native, flags, 1.0/60.0).unwrap();
    assert!(vlen(vsub(a[1],b[1])) > 0.005, "private DynamicBone must actually move the weighted tip");
    assert!(vlen(vsub(a[0],b[0])) < 1e-6 && vlen(vsub(a[2],b[2])) < 1e-6);
}

#[test]
#[ignore = "requires local controlled AssignedAnotherWeights game fixture"]
fn clothing_game_mixed_slots_and_destroyed_bone_control() {
    game_clothing_slots(false);
}

#[test]
fn clothing_implant_same_name_precedes_body_and_survives_cache() {
    let (mut source,mut body,mut mesh)=clothes_fixture();
    source.tfs.get_mut(&3).unwrap().name="borrowed".into(); source.rebuild_worlds();
    mesh.bone_names=source.names.clone(); mesh.smr_skel=Some(source);
    mesh.bone_implants=vec![implant::Marker {component:1,source:Some(3),destination:Some(2),colliders:vec![]}];
    // AssignedWeights retains the renderer slots even without implanting.
    // The disabled control must follow the matching body Transform; enabling
    // implantation then gives the copied private bone precedence below.
    let mut plain=crate::session::AccessoryRuntime::from_clothes_source(&mesh,None,&body,true,"ct_clothesTop",None,false).unwrap();
    let (plain_vertices,_)=plain.update(Some(&body),None,&BodyColliderSet::default(),game_colliders::Flags::default(),0.0).unwrap();
    assert!((plain_vertices[1][0]-10.0).abs()<1e-6);
    let runtime=crate::session::AccessoryRuntime::from_clothes_source(&mesh,None,&body,true,"ct_clothesTop",None,true).unwrap();
    let mut restored:crate::session::AccessoryRuntime=bincode::deserialize(&bincode::serialize(&runtime).unwrap()).unwrap();
    for x in [10.0,20.0] {
        body.tfs.get_mut(&200).unwrap().pos[0]=x; body.rebuild_worlds();
        let (vertices,_)=restored.update(Some(&body),None,&BodyColliderSet::default(),game_colliders::Flags::default(),0.0).unwrap();
        assert!((vertices[0][0]-x).abs()<1e-6);
        assert!((vertices[1][0]-(x+2.0)).abs()<1e-6,"implanted reference must win even when its name matches a body bone");
    }
    let (_,body,mut mesh)=clothes_fixture();
    mesh.bone_implants=vec![implant::Marker {component:1,source:None,destination:Some(2),colliders:vec![]}];
    let mut invalid=crate::session::AccessoryRuntime::from_clothes_source(&mesh,None,&body,true,"ct_clothesTop",None,true).unwrap();
    let (vertices,_)=invalid.update(Some(&body),None,&BodyColliderSet::default(),game_colliders::Flags::default(),0.0).unwrap();
    assert_eq!(vertices[1],[0.0;3],"presence of an invalid marker still invokes the plugin's null-slot branch");
}

#[test]
fn clothing_implanted_component_and_collision_follow_live_body() {
    let (mut source,mut body,_)=clothes_fixture();
    source.tfs.get_mut(&6).unwrap().pos=[0.0,0.09,0.0];
    let markers=vec![implant::Marker {component:1,source:Some(6),destination:Some(2),colliders:vec![800]}];
    let mut set=DBoneSet {bones:vec![DBone {root:2,component_transform:Some(6),end_offset:[0.0,0.1,0.0],
        update_rate:60.0,force:[0.003,0.0,0.0],damping:0.2,elasticity:0.15,stiffness:0.05,
        ..Default::default()}],colliders:[(800,BodyCollider {transform:6,radius:0.07,height:0.0,center:[0.0;3],direction:1,inside:false})].into_iter().collect(), ..Default::default()};
    let (implanted,_)=implant::prepare(&mut source,&body,&markers,None);
    set.add_implanted_colliders(&markers,&implanted);
    assert_eq!(set.for_skeleton(&source).colliders.len(),1,"unreferenced authored collider must survive until Implantor appends it");
    let shared=set.retarget_clothes(&source,&body,"ct_clothesTop",None);
    let mut state=AttachedDynamicSet::new_unsettled(&shared,&body,&crate::pose::ident()).unwrap();
    let component=state.chains[0].component;
    for x in [10.0,20.0] {
        body.tfs.get_mut(&200).unwrap().pos[0]=x; body.rebuild_worlds();
        state.sync_shared_pose(&body); state.configure_character_colliders(&body,&BodyColliderSet::default(),game_colliders::Flags::default());
        assert!(vlen(vsub(pos_of(&world_of(component,&state.skel.tfs,&mut HashMap::new())),[x,0.09,0.0]))<1e-6);
        assert_eq!(state.chains[0].external_colliders.len(),1);
        assert!(vlen(vsub(pos_of(&state.chains[0].external_colliders[0].1),[x,0.09,0.0]))<1e-6);
    }
    let mut clear=state.clone();
    for chain in &mut clear.chains {chain.external_colliders.clear();}
    for _ in 0..120 {
        state.sync_shared_pose(&body); state.step_on_animated_pose(1.0/60.0);
        clear.sync_shared_pose(&body); clear.step_on_animated_pose(1.0/60.0);
    }
    let contact=vlen(vsub(state.chains[0].particles[1].pos,clear.chains[0].particles[1].pos));
    eprintln!("implanted shared collider contact displacement={contact:.8} m");
    assert!(contact>0.001,"verify collision response, not collider count alone");
}

#[test]
#[ignore = "requires controlled installed ModBoneImplantor game fixture"]
fn clothing_game_implanted_slots_control() { game_clothing_slots(true); }

#[test]
fn clothing_prefab_coordinator_gathers_implants_once_for_all_renderers() {
    let (mut source,mut body,mut mesh)=clothes_fixture();
    source.tfs.get_mut(&3).unwrap().pos=[0.0,0.2,0.0];
    source.tfs.get_mut(&5).unwrap().pos=[0.0,0.2,0.0];
    source.bone_ids[1]=5; source.rebuild_worlds();
    mesh.bone_names=source.names.clone(); mesh.smr_skel=Some(source.clone());
    let markers=vec![implant::Marker {component:1,source:Some(3),destination:Some(2),colliders:vec![800]}];
    mesh.bone_implants=markers.clone();
    let mut set=DBoneSet {bones:vec![
        DBone {root:2,component_transform:Some(1),force:[0.01,0.0,0.0],update_rate:60.0,damping:0.2,elasticity:0.1,stiffness:0.05,..Default::default()},
        DBone {root:3,component_transform:Some(1),force:[0.0,0.0,0.01],update_rate:60.0,damping:0.2,elasticity:0.1,stiffness:0.05,..Default::default()},
    ],colliders:[(800,BodyCollider {transform:3,radius:0.001,height:0.0,center:[0.0;3],direction:1,inside:false})].into_iter().collect(),..Default::default()};
    mesh.deferred_dbones=Some(set.clone());
    let mut first=crate::session::AccessoryRuntime::from_clothes_source(&mesh,None,&body,true,"ct_clothesTop",None,true).unwrap();
    let mut second=first.clone(); first.coordinate_clothes(); second.coordinate_clothes();
    implant::rename_prefab_root(&mut source,"ct_clothesTop");
    let (implanted,_)=implant::prepare(&mut source,&body,&markers,None);set.add_implanted_colliders(&markers,&implanted);
    let config=implant::rig::loaded(&set,&source,&body,"ct_clothesTop",true);
    let cached:DBoneSet=bincode::deserialize(&bincode::serialize(&config).unwrap()).unwrap();
    let mut state=AttachedDynamicSet::new_unsettled(&cached,&body,&crate::pose::ident()).unwrap();
    let planted=state.skel.tfs.iter().find(|(_,tf)|tf.name==implant::rig::key("ct_clothesTop",3)).unwrap().0.to_owned();
    let tip=state.skel.tfs.iter().find(|(_,tf)|tf.name==implant::rig::key("ct_clothesTop",5)).unwrap().0.to_owned();
    assert_eq!(state.skel.tfs[&planted].parent,Some(200),"body link must alias the actual body node");
    assert_eq!(state.skel.tfs[&tip].parent,Some(planted));
    assert!(state.chains[0].particles.iter().any(|particle|particle.id==Some(tip)),"shared root must gather the implanted descendants");
    assert_eq!(state.chains[1].particles[0].id,Some(planted),"private and shared chains must use the same Transform identity");
    state.configure_character_colliders(&body,&BodyColliderSet::default(),game_colliders::Flags::default());
    let early=pos_of(&world_of(planted,&state.skel.tfs,&mut HashMap::new()));
    state.sync_shared_pose(&body);state.step_on_animated_pose(1.0/60.0);
    let late=pos_of(&world_of(planted,&state.skel.tfs,&mut HashMap::new()));
    assert!(vlen(vsub(early,late))>1e-4);
    assert!(vlen(vsub(pos_of(&state.chains[1].external_colliders[0].1),late))<2e-6,
        "later component must read the collider after the earlier component rotated its ancestor");
    let before=pos_of(&world_of(tip,&state.skel.tfs,&mut HashMap::new()));
    for _ in 0..120 {state.sync_shared_pose(&body);state.step_on_animated_pose(1.0/60.0);}
    let after=pos_of(&world_of(tip,&state.skel.tfs,&mut HashMap::new()));
    assert!(vlen(vsub(before,after))>0.005,"implanted tip must actually receive physics");
    state.copy_simulated_pose(&mut body);
    let flags=game_colliders::Flags::default(); let native=BodyColliderSet::default();
    let a=first.update(Some(&state.skel),None,&native,flags,0.0).unwrap().0;
    let b=second.update(Some(&state.skel),None,&native,flags,0.5).unwrap().0;
    assert_eq!(a,b,"renderer updates must not independently advance the shared solver");
    assert!(vlen(vsub(a[1],after))<2e-6,"renderer must consume the coordinator's private bone world");
    let mut restored:crate::session::AccessoryRuntime=bincode::deserialize(&bincode::serialize(&first).unwrap()).unwrap();
    assert_eq!(restored.update(Some(&state.skel),None,&native,flags,1.0).unwrap().0,a);
    body.tfs.get_mut(&200).unwrap().pos[0]=20.0; body.rebuild_worlds();
    state.sync_shared_pose(&body);
    let expected=pos_of(&mat_mul_pub(&body.world_tf("borrowed").unwrap(),&trs([0.0,0.2,0.0],[0.0,0.0,0.0,1.0],[1.0;3])));
    assert!(vlen(vsub(pos_of(&world_of(planted,&state.skel.tfs,&mut HashMap::new())),expected))<2e-6,"cache overlay must follow both live body translation and rotation");
    let no_copy=implant::rig::loaded(&set,&source,&body,"ct_bra",false);
    assert_eq!(no_copy.len(),1,"copyDynamicBone=false must not revive the removed cloned matching root");
}

fn game_clothing_slots(implant: bool) {
    let path=if implant {"../../Saved/Logs/MixedClothesImplantGame270.json.clothing.tsv"}
        else {"../../Saved/Logs/MixedClothesGame268.json.clothing.tsv"};
    let rows = crate::gamefs::read_to_string(path).unwrap();
    assert!(rows.contains(if implant {"removed_destroyed\tFalse"} else {"removed_destroyed\tTrue"}));
    let (mut source, mut body, mut mesh) = clothes_fixture();
    source.bone_ids.push(6); source.rebuild_worlds(); source.rest = source.worlds.clone();
    source.tfs.get_mut(&6).unwrap().pos = [3.0,0.0,0.0]; source.rebuild_worlds();
    mesh.positions.extend([[0.0;3],[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]);
    mesh.rest_positions = mesh.positions.clone(); mesh.normals.resize(7,[0.0,0.0,1.0]); mesh.rest_normals = mesh.normals.clone();
    mesh.weights.resize(7,[1.0,0.0,0.0,0.0]); mesh.bone_idx.resize(7,[3,0,0,0]);
    let mut null_bind = crate::pose::ident(); null_bind[0][3] = 0.2; mesh.bind_poses.push(null_bind);
    mesh.bone_names = source.names.clone(); mesh.smr_skel = Some(source);
    if implant {mesh.bone_implants.push(implant::Marker {component:1,source:Some(6),destination:Some(2),colliders:Vec::new()});}
    let mut runtime = crate::session::AccessoryRuntime::from_clothes_source(&mesh, None, &body, true, "ct_clothesTop", None, implant).unwrap();
    for sample in 0..2 {
        body.tfs.get_mut(&200).unwrap().pos[0] = if sample == 0 {10.0} else {20.0}; body.rebuild_worlds();
        let (vertices, _) = runtime.update(Some(&body), None, &BodyColliderSet::default(), game_colliders::Flags::default(), 0.0).unwrap();
        let expected: Vec<_> = rows.lines().filter_map(|row| {
            let f: Vec<_> = row.split('\t').collect();
            (f[0]=="vertex" && f[1]==sample.to_string()).then(|| tuple::<3>(f[3]))
        }).collect();
        assert_eq!(expected.len(), 7); let error = vertices.iter().zip(expected)
            .map(|(a,b)| vlen(vsub(*a,b))).fold(0.0f32,f32::max);
        eprintln!("game clothing sample {sample}: 7 slots/vertices, max error={error:.8} m");
        assert!(error < 2e-6);
    }
}

#[test]
#[ignore = "requires local controlled KKS ordinary collider fixture"]
fn ordinary_game_floor_skirt_control() {
    let base = "../../Saved/Logs/OrdinaryCollidersGame265v4.json";
    let (baseline, ids) = fixture(&format!("{base}.ordinary-baseline.tsv"));
    let (expected, result_ids) = fixture(&format!("{base}.ordinary-result.tsv"));
    let game = &crate::test_support::game_path("");
    let parsed = crate::unity::parse_bundle_cached(crate::gamefs::read(game.join("abdata/chara/oo_base.unity3d")).unwrap());
    let source = parsed.skeleton_for_prefab_name("p_cf_body_bone").unwrap();
    let native = parsed.body_colliders_for_skeleton(&source);
    assert_eq!(native.bound(&baseline).len(), 24);
    let flags = game_colliders::Flags { installed: true, breast: true, floor: true, skirt: true };
    let rows = crate::gamefs::read_to_string(format!("{base}.ordinary-setup.tsv")).unwrap();
    let mut set = DBoneSet::default();
    for row in rows.lines().filter(|s| s.starts_with("component\t")) {
        let f: Vec<_> = row.split('\t').collect();
        set.bones.push(DBone { root: ids[f[2]], component_transform: Some(ids[f[1]]),
            component_name: f[1].rsplit('/').next().unwrap().into(), update_rate: 60.0,
            damping: 0.2, elasticity: 0.15, stiffness: 0.05, inert: 0.3, radius: 0.012,
            gravity: [0.002, -0.01, 0.004], force: [0.003, -0.01, 0.004], ..Default::default() });
    }
    assert_eq!(set.len(), 2);
    let mut state = AttachedDynamicSet::new_unsettled(&set.with_game_fixes(game), &baseline, &crate::pose::ident()).unwrap();
    state.configure_character_colliders(&baseline, &native, flags);
    for chain in &state.chains {
        let live: Vec<_> = rows.lines().filter(|s| s.starts_with(&format!("collider\t{}\t", chain.db.component_name))).collect();
        assert_eq!(chain.external_colliders.len(), live.len());
        assert_eq!(live.len(), if chain.db.component_name == "ct_clothesBot" { 31 } else { 25 });
        assert_eq!(chain.db.freeze_axis, if chain.db.component_name == "ct_clothesBot" { 1 } else { 0 });
        for ((c, world, _), row) in chain.external_colliders.iter().zip(live) {
            let f: Vec<_> = row.split('\t').collect();
            assert_eq!(c.radius, f[3].parse::<f32>().unwrap()); assert_eq!(c.height, f[4].parse::<f32>().unwrap());
            assert_eq!(c.center, tuple::<3>(f[5])); assert_eq!(c.direction, f[6].parse::<usize>().unwrap());
            let game_world = world_of(ids[f[2]], &baseline.tfs, &mut HashMap::new());
            assert!(world.iter().flatten().zip(game_world.iter().flatten()).all(|(a,b)| (a-b).abs() < 1e-5));
        }
    }
    let mut clear = state.clone();
    for chain in &mut clear.chains { chain.external_colliders.clear(); }
    for _ in 0..240 {
        state.sync_shared_pose(&baseline); state.step(1.0 / 60.0);
        clear.sync_shared_pose(&baseline); clear.step(1.0 / 60.0);
    }
    let mut max_position = 0.0f32; let mut max_rotation = 0.0f32; let mut contact = 0.0f32;
    for chain in &state.chains {
        for particle in &chain.particles {
            let Some(id) = particle.id else { continue; };
            let name = &state.skel.tfs[&id].name;
            let path = ids.iter().find(|(_, value)| **value == id).unwrap().0;
            let a = &state.skel.tfs[&id]; let b = &expected.tfs[&result_ids[path]];
            let position = vlen(vsub(a.pos, b.pos));
            let rotation = a.rot.iter().zip(b.rot).map(|(x,y)| (x-y).abs()).fold(0.0f32, f32::max);
            max_position = max_position.max(position); max_rotation = max_rotation.max(rotation);
            // ApplyParticlesToTransforms rotates the parent. Local child
            // positions can stay unchanged even when world contact moves them.
            let contact_world = pos_of(&world_of(id, &state.skel.tfs, &mut HashMap::new()));
            let clear_world = pos_of(&world_of(id, &clear.skel.tfs, &mut HashMap::new()));
            contact = contact.max(vlen(vsub(contact_world, clear_world)));
            eprintln!("ordinary game {name}: local pos={position:.8} m, quaternion={rotation:.8}");
        }
    }
    eprintln!("ordinary contact displacement={contact:.8} m");
    assert!(contact > 0.001, "real contact, not collider count only");
    assert!(max_position < 2e-6 && max_rotation < 2e-5, "ordinary game: position={max_position}, rotation={max_rotation}");
}

#[test]
#[ignore = "requires the controlled installed game prefab coordinator fixture"]
fn clothing_game_prefab_coordinator_control() {
    let rows=crate::gamefs::read_to_string("../../Saved/Logs/ClothesCoordinatorGame271.json.clothing-physics.tsv").unwrap();
    let (mut source,body,mut mesh)=clothes_fixture();
    source.tfs.get_mut(&6).unwrap().pos=[0.0,0.2,0.0];
    source.tfs.insert(7,Tf { sibling_index: None,name:"implanted_tip".into(),parent:Some(6),pos:[0.0,0.2,0.0],rot:[0.0,0.0,0.0,1.0],scale:[1.0;3]});
    source.bone_ids.push(6); source.rebuild_worlds();
    mesh.positions.extend([[0.0;3],[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]);
    mesh.rest_positions=mesh.positions.clone(); mesh.normals.resize(7,[0.0,0.0,1.0]); mesh.rest_normals=mesh.normals.clone();
    mesh.weights.resize(7,[1.0,0.0,0.0,0.0]);mesh.bone_idx.resize(7,[3,0,0,0]);
    let mut bind=crate::pose::ident();bind[0][3]=0.2;mesh.bind_poses.push(bind);
    mesh.bone_names=source.names.clone();mesh.smr_skel=Some(source.clone());
    let markers=vec![implant::Marker {component:1,source:Some(6),destination:Some(2),colliders:vec![]}];mesh.bone_implants=markers.clone();
    let set=DBoneSet {bones:vec![
        DBone {root:2,component_transform:Some(1),force:[0.01,0.0,0.0],update_rate:60.0,damping:0.2,elasticity:0.1,stiffness:0.05,..Default::default()},
        DBone {root:6,component_transform:Some(1),force:[0.0,0.0,0.01],update_rate:60.0,damping:0.2,elasticity:0.1,stiffness:0.05,..Default::default()},
    ],..Default::default()};mesh.deferred_dbones=Some(set.clone());
    let mut runtime=crate::session::AccessoryRuntime::from_clothes_source(&mesh,None,&body,true,"ct_clothesTop",None,true).unwrap();runtime.coordinate_clothes();
    implant::rename_prefab_root(&mut source,"ct_clothesTop");implant::prepare(&mut source,&body,&markers,None);
    let config=implant::rig::loaded(&set,&source,&body,"ct_clothesTop",true);
    let mut state=AttachedDynamicSet::new_unsettled(&config,&body,&crate::pose::ident()).unwrap();
    for _ in 0..120 {state.sync_shared_pose(&body);state.step_on_animated_pose(1.0/60.0);}
    let mut max_position=0.0f32;let mut max_rotation=0.0f32;let mut checked=0;
    for row in rows.lines().filter(|row|row.starts_with("bone\t")) {
        let f:Vec<_>=row.split('\t').collect();
        let name=if f[1]=="borrowed" {"borrowed".into()} else {implant::rig::key("ct_clothesTop",if f[1]=="removed_private" {6}else{7})};
        let tf=state.skel.tfs.values().find(|tf|tf.name==name).unwrap();
        max_position=max_position.max(vlen(vsub(tf.pos,tuple::<3>(f[2]))));
        max_rotation=max_rotation.max(tf.rot.iter().zip(tuple::<4>(f[3])).map(|(a,b)|(a-b).abs()).fold(0.0f32,f32::max));checked+=1;
        eprintln!("coordinator {} Rust pos={:?} rot={:?}; game pos={} rot={}",f[1],tf.pos,tf.rot,f[2],f[3]);
    }
    let vertices=runtime.update(Some(&state.skel),None,&BodyColliderSet::default(),game_colliders::Flags::default(),0.0).unwrap().0;
    let expected:Vec<_>=rows.lines().filter(|row|row.starts_with("vertex\t")).map(|row|tuple::<3>(row.split('\t').nth(2).unwrap())).collect();
    assert_eq!(checked,3);assert_eq!(expected.len(),7);
    let vertex_error=vertices.iter().zip(expected).map(|(a,b)|vlen(vsub(*a,b))).fold(0.0f32,f32::max);
    eprintln!("game prefab coordinator: 3 bones, position={max_position:.8} m quaternion={max_rotation:.8}; 7 vertices={vertex_error:.8} m");
    assert!(max_position<2e-6 && max_rotation<2e-5 && vertex_error<2e-6);
}

#[test]
#[ignore = "requires installed Unity Quaternion.FromToRotation angle control"]
fn clothing_game_quaternion_control() {
    let rows=crate::gamefs::read_to_string("../../Saved/Logs/ClothesCoordinatorGame271v3.json.quaternion.tsv").unwrap();
    let mut checked=0;
    for row in rows.lines() {
        let f:Vec<_>=row.split('\t').collect();
        let actual=quat_from_to([0.0,1.0,0.0],tuple::<3>(f[1]));
        let expected=tuple::<4>(f[2]);
        let error=actual.iter().zip(expected).map(|(a,b)|(a-b).abs()).fold(0.0f32,f32::max);
        assert!(error<1e-7,"Unity FromToRotation angle {} error {error}",f[0]);checked+=1;
    }
    assert_eq!(checked,7);
}

#[test]
#[ignore = "requires installed-game branching/notRoll particle control"]
fn ordinary_game_source_sibling_and_notroll_control() {
    let text=crate::gamefs::read_to_string("../../Saved/Logs/BranchingGame272.json.branching.tsv").unwrap();
    let rows:Vec<Vec<_>>=text.lines().map(|line|line.split('\t').collect()).collect();
    let ids:HashMap<_,_>=[("root",1),("branch_z",90),("z_tip",91),("roll_a",80),("ra_tip",81),
        ("branch_a",20),("a_tip",21),("roll_b",10),("b_tip",11),("b_skip",12),("excluded",5),("excluded_tip",6)].into_iter().collect();
    for mode in ["plain","notroll","excluded"] {
        let tfs=rows.iter().filter(|r|r[0]=="baseline"&&r[1]==mode).map(|r| {
            (ids[r[2]],Tf {name:r[2].into(),parent:ids.get(r[3]).copied(),sibling_index:Some(r[4].parse().unwrap()),
                pos:tuple(r[5]),rot:tuple(r[6]),scale:tuple(r[7])})
        }).collect();
        let source=Skeleton {names:vec![],worlds:vec![],rest:vec![],smr_count:0,tfs,bone_ids:vec![],source_mesh:0};
        let set=DBoneSet {bones:vec![DBone {root:1,update_rate:60.0,damping:0.2,elasticity:0.1,stiffness:0.05,
            force:[0.007,-0.001,0.003],end_offset:[0.0,0.05,0.0],
            not_rolls:if mode=="plain" {vec![]}else{vec![80,10]},
            exclusions:if mode=="excluded" {vec![5,12]}else{vec![]},..Default::default()}],..Default::default()};
        let mut state=AttachedDynamicSet::new_unsettled(&set,&source,&crate::pose::ident()).unwrap();
        let actual:Vec<_>=state.chains[0].particles.iter().map(|p|
            (p.id.map(|id|state.skel.tfs[&id].name.clone()).unwrap_or_else(||"null".into()),p.parent)).collect();
        let expected:Vec<_>=rows.iter().filter(|r|r[0]=="particle"&&r[1]==mode)
            .map(|r|(r[3].to_owned(),r[4].parse::<i32>().unwrap())).collect();
        assert_eq!(actual,expected,"exact native particle order/parent indices for {mode}");
        for _ in 0..120 {state.step(1.0/60.0);}
        let mut max_position=0.0f32;let mut max_rotation=0.0f32;let mut count=0;
        for row in rows.iter().filter(|r|r[0]=="result"&&r[1]==mode) {
            let tf=&state.skel.tfs[&ids[row[2]]];
            max_position=max_position.max(vlen(vsub(tf.pos,tuple(row[3]))));
            max_rotation=max_rotation.max(tf.rot.iter().zip(tuple::<4>(row[4])).map(|(a,b)|(a-b).abs()).fold(0.0f32,f32::max));count+=1;
        }
        assert_eq!(count,12);
        eprintln!("branching game {mode}: particles={}, 12 transforms position={max_position:.8} m quaternion={max_rotation:.8}",actual.len());
        assert!(max_position<2e-6&&max_rotation<2e-5,"{mode}: position={max_position},rotation={max_rotation}");
    }
}

#[test]
fn clothing_source_sibling_order_survives_two_prefab_implants_and_cache() {
    let (mut source,mut body,_)=clothes_fixture();
    for (id,index) in [(300,0),(301,1)] {
        body.tfs.insert(id,Tf {name:format!("body_child_{index}"),parent:Some(200),sibling_index:Some(index),
            pos:[0.0,0.1,0.0],rot:[0.0,0.0,0.0,1.0],scale:[1.0;3]});
    }
    let markers=vec![implant::Marker {component:1,source:Some(6),destination:Some(2),colliders:vec![]},
        implant::Marker {component:1,source:Some(3),destination:Some(2),colliders:vec![]}];
    implant::prepare(&mut source,&body,&markers,None);
    let bridge=source.tfs[&6].parent.unwrap();
    assert_eq!(children_of(&source.tfs,bridge),vec![6,3],"SetParent appends in marker order, not path ID order");
    let before=source.tfs[&6].sibling_index;
    source.reparent(6,Some(bridge),100);assert_eq!(source.tfs[&6].sibling_index,before,"same parent is a no-op");
    let mut set=implant::rig::loaded(&DBoneSet::default(),&source,&body,"ct_clothesTop",true);
    let second=implant::rig::loaded(&DBoneSet::default(),&source,&body,"ct_clothesBot",true);
    set.extend(second);
    let config:DBoneSet=bincode::deserialize(&bincode::serialize(&set).unwrap()).unwrap();
    let mut expected=vec!["body_child_0".into(),"body_child_1".into()];
    for name in ["ct_clothesTop","ct_clothesBot"] {for id in [6,3] {expected.push(implant::rig::key(name,id));}}
    for _ in 0..8 {
        let mut resolved=config.clone();let mut skel=body.clone();implant::rig::instantiate(&mut resolved,&mut skel);
        let actual:Vec<_>=children_of(&skel.tfs,200).iter().map(|id|skel.tfs[id].name.clone()).collect();
        assert_eq!(actual,expected,"body children precede successive prefab implants even when private IDs are reallocated");
        let cached:Skeleton=bincode::deserialize(&bincode::serialize(&skel).unwrap()).unwrap();
        assert_eq!(children_of(&cached.tfs,200),children_of(&skel.tfs,200));
    }
}
