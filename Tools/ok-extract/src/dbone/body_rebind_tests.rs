use super::*;

fn rig(base:i64)->Skeleton {
    let mut sk=Skeleton {names:vec!["root".into(),"tip".into(),"aux".into()], worlds:vec![],rest:vec![],
        smr_count:3,bone_ids:vec![base,base+1,base+2],source_mesh:0,tfs:HashMap::new()};
    for (offset,name,parent,pos) in [(0,"root",None,[0.;3]),(1,"tip",Some(base),[0.,1.,0.]),(2,"aux",Some(base),[1.,0.,0.])] {
        sk.tfs.insert(base+offset,Tf{name:name.into(),parent,pos,rot:[0.,0.,0.,1.],scale:[1.;3],sibling_index:None});
    }
    sk.rebuild_worlds();sk
}

#[test]
fn body_rebind_preserves_both_particle_versions_and_colliders() {
    let source=rig(10);let target=rig(110);
    for ver01 in [false,true] {
        let original=DBoneSet{bones:vec![DBone{root:10,component_transform:Some(10),ver01,
            ver01_nodes:if ver01{vec![(10,true),(11,false)]}else{vec![]},
            collider_refs:vec![70],exclusions:vec![12],not_rolls:vec![12],update_rate:60.,
            ..Default::default()}],colliders:[(70,BodyCollider{transform:10,center:[0.;3],
                radius:0.2,height:0.,direction:1,inside:false})].into_iter().collect(),rigs:vec![]};
        let rebound=original.retarget_body(&source,&target);
        assert_eq!(rebound.bones[0].root,110);
        assert_eq!(rebound.bones[0].not_rolls,vec![112]);
        assert_eq!(rebound.bones[0].exclusions,vec![112]);
        if ver01 {assert_eq!(rebound.bones[0].ver01_nodes,vec![(110,true),(111,false)]);}
        let mut runtime=AttachedDynamicSet::new_unsettled(&rebound,&target,&ident())
            .expect("both versions must retain an active chain after all Transform IDs change");
        assert_eq!(runtime.chains.len(),1);
        let particles:Vec<_>=runtime.chains[0].particles.iter().filter_map(|p|p.id).collect();
        assert_eq!(particles,vec![110,111]);
        runtime.configure_character_colliders(&target,&BodyColliderSet::default(),game_colliders::Flags::default());
        let chain=&runtime.chains[0];
        let colliders:Vec<_>=chain.colliders.iter().chain(chain.external_colliders.iter().map(|(c,_,_)|c)).collect();
        assert_eq!(colliders.len(),1,"authored collider must survive shared configuration for each version");
        assert_eq!(colliders[0].transform,110);
        assert_eq!(colliders[0].radius,0.2);
        let cached:DBoneSet=bincode::deserialize(&bincode::serialize(&rebound).unwrap()).unwrap();
        let other=AttachedDynamicSet::new_unsettled(&cached,&target,&ident()).unwrap();
        let snapshot=|state:&AttachedDynamicSet| {
            let mut transforms:Vec<_>=state.skel.tfs.iter().map(|(id,t)|(*id,t.pos,t.rot,t.scale)).collect();
            transforms.sort_by_key(|t|t.0);
            let particles:Vec<_>=state.chains.iter().flat_map(|c|c.particles.iter().map(|p|(p.pos,p.prev))).collect();
            (transforms,particles)
        };
        let before=snapshot(&other);
        let mut animated=target.clone();animated.tfs.get_mut(&110).unwrap().pos[0]=0.1;
        animated.rebuild_worlds();runtime.sync_shared_pose(&animated);
        runtime.step_on_animated_pose(1./60.);
        assert_eq!(before,snapshot(&other),"updating a body must not mutate another cached instance");
        assert!(runtime.skel.tfs.values().all(|t|t.pos.iter().chain(&t.rot).all(|v|v.is_finite())));
        assert!(AttachedDynamicSet::new_unsettled(&cached,&target,&ident()).is_some());
    }
}
