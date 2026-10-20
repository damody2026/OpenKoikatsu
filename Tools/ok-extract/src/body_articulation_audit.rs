//! Installed source rig audit; numerical output only.
use std::path::Path;
#[test]
#[ignore = "requires installed game; explicit numerical body rig audit"]
fn installed_body_articulation_sources() {
    let game=&crate::test_support::game_path("");
    let mut rows=Vec::new();
    for name in ["菲比v1.1.png","KK_226176.png","Alice_keiyuu.png"] {
        let path=game.join("UserData/chara/female").join(name);
        let card=crate::card::parse_card(&path).unwrap();
        let (source,bytes)=crate::mods::uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex).unwrap();
        let parsed=crate::unity::parse_bundle_ref(&bytes);
        parsed.configure_game_references(game);
        let (meshes,_) = parsed.meshes_for_maindata(&source.asset);
        let mesh=meshes.iter().find(|m|m.name.eq_ignore_ascii_case("o_body_a")).unwrap();
        let sk=mesh.smr_skel.as_ref().unwrap();
        let set=parsed.dynamic_bones_for_skeleton(sk);
        let mut target=sk.clone();
        let ids:std::collections::HashMap<_,_>=sk.tfs.keys().map(|id|(*id,*id-9_000_000)).collect();
        target.tfs=sk.tfs.iter().map(|(id,t)| {
            let mut t=t.clone();t.parent=t.parent.and_then(|p|ids.get(&p).copied());(ids[id],t)
        }).collect();
        target.bone_ids=sk.bone_ids.iter().map(|id|ids[id]).collect();target.rebuild_worlds();
        let rebound=set.retarget_body(sk,&target);
        assert_eq!(rebound.len(),set.len(),"all body components must survive source ID changes");
        let mut moved=0usize;
        if !rebound.is_empty() {
            let before=target.clone();
            let mut state=crate::dbone::AttachedDynamicSet::new_unsettled(&rebound,&target,&crate::pose::ident()).unwrap();
            for frame in 0..120 {
                target=before.clone();
                for t in target.tfs.values_mut().filter(|t|t.parent.is_none()) {
                    t.pos[0]+=0.04*(frame as f32*0.13).sin();
                }
                state.sync_shared_pose(&target);state.step_on_animated_pose(1.0/60.0);
                let animated=target.clone();state.copy_simulated_pose(&mut target);
                for (id,t) in &target.tfs {
                    assert!(t.pos.iter().chain(&t.rot).chain(&t.scale).all(|v|v.is_finite()));
                    if t.name.to_ascii_lowercase().contains("vagina") &&
                       (t.pos!=animated.tfs[id].pos || t.rot!=animated.tfs[id].rot) {moved+=1;}
                }
            }
            assert!(moved>0,"source secondary bones must produce actual output changes");
        }
        let mut names:Vec<_>=sk.tfs.values().filter(|t|t.name.to_ascii_lowercase().contains("vagina"))
            .map(|t|t.name.clone()).collect();names.sort();
        rows.push(serde_json::json!({"card":path,"body_guid":source.guid,"bundle":source.file,
            "vertices":mesh.positions.len(),"rig_transforms":sk.tfs.len(),
            "articulation_bones":names,"standard_dynamic_components":set.len(),
            "rebound_dynamic_components":rebound.len(),"secondary_bone_changes":moved,
            "morph_names":mesh.blend_shapes.iter().map(|(n,_)|n).collect::<Vec<_>>() }));
    }
    let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/BodyArticulation347");
    std::fs::create_dir_all(root).unwrap();
    std::fs::write(root.join("Sources.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    eprintln!("{}",serde_json::to_string(&rows).unwrap());
}

#[test]
#[ignore = "requires installed game; live session and cached body component regression"]
fn installed_body_components_survive_session_and_cache() {
    let game=&crate::test_support::game_path("");
    let card=game.join("UserData/chara/female/菲比v1.1.png");
    let mut session=crate::session::open(game,Some(&card),0).unwrap();
    assert!(session.clothes_dynamics.len()>=13,"replacement body components must reach live runtime");
    let bytes=session.to_cache_bytes().unwrap();
    let cached=crate::session::OkSession::from_cache_bytes(&bytes).unwrap();
    assert_eq!(cached.clothes_dynamics.len(),session.clothes_dynamics.len());
    session.apply_anim_time(0.0);
    for frame in 0..60 {session.apply_anim_time(frame as f32/60.0);}
    let sk=session.body_skel.as_ref().unwrap();
    assert!(sk.tfs.values().all(|t|t.pos.iter().chain(&t.rot).chain(&t.scale).all(|v|v.is_finite())));
    let output=serde_json::json!({"passed":true,"dynamic_components":session.clothes_dynamics.len(),
        "cached_dynamic_components":cached.clothes_dynamics.len(),"frames":61,"nonfinite":0,
        "scope":"live session component retention and cache; not paired-contact native parity"});
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/BodyArticulation347/LiveComponents.json",serde_json::to_vec_pretty(&output).unwrap()).unwrap();
}

#[test]
#[ignore = "requires installed game; live-palette morph rotation regression"]
fn body_morph_follows_live_palette() {
    let game=&crate::test_support::game_path("");
    let card=game.join("UserData/chara/female/KK_226176.png");
    let mut session=crate::session::open(game,Some(&card),0).unwrap();
    let part=session.parts.iter().find(|p|p.role=="body"&&!p.blend_shapes.is_empty()).unwrap();
    let before=crate::skel_export::body_morph_to_pose(&session,part,0).unwrap();
    let q=[0.,std::f32::consts::FRAC_1_SQRT_2,0.,std::f32::consts::FRAC_1_SQRT_2];
    for tf in session.body_skel.as_mut().unwrap().tfs.values_mut().filter(|t|t.parent.is_none()) {
        tf.rot=crate::pose::quat_mul(q,tf.rot);
    }
    session.body_skel.as_mut().unwrap().rebuild_worlds();
    let part=session.parts.iter().find(|p|p.role=="body"&&!p.blend_shapes.is_empty()).unwrap();
    let after=crate::skel_export::body_morph_to_pose(&session,part,0).unwrap();
    let rotation=crate::pose::trs([0.;3],q,[1.;3]);
    let mut maximum=0f32;let mut positive=0f32;
    for (a,b) in before.chunks_exact(3).zip(after.chunks_exact(3)) {
        let expected=crate::pose::mat_vec(&rotation,[a[0],a[1],a[2]]);
        for c in 0..3 {maximum=maximum.max((b[c]-expected[c]).abs());positive=positive.max(a[c].abs());}
    }
    assert!(positive>0.0001);assert!(maximum<1e-5,"live palette error {maximum}");
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/BodyArticulation347/PaletteControl.json",
        serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"max_error":maximum,"positive_displacement":positive})).unwrap()).unwrap();
}
