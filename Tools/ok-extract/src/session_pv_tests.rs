use super::*;
use std::ffi::CString;

#[test]
#[ignore="installed five cards, source Studio PV controls and shared external entry"]
fn studio_pv_session_policy_and_shared_entry() {
    let game=&crate::test_support::game_path("");std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");
    let desc=["studio/anime/00.unity3d","khh_f_base","studio/anime/00.unity3d","khh_f_28"];
    let source=format!("states:studio@{}",serde_json::to_vec(&desc).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>());
    let label=crate::anim::clip_names_for_source(game,&source).into_iter().find(|v|v=="khh_f_base / [Studio layer 79] Base Layer.Stop_Idle").unwrap();
    let key=CString::new(format!("{source}|{label}")).unwrap();let mut frames=0;let mut changed=0;let mut atomic=0;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        let mut s=open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();s.ensure_skel_export();
        assert!(s.pv_config.as_ref().is_some_and(|c|c.pairs.len()==4));
        assert_eq!(unsafe{crate::ffi::set_studio_animation_pv_profile(&mut s,key.as_ptr(),1,0.37,0.61,0,0.43,c"h/list/01.unity3d".as_ptr(),c"yure_khh_28_00".as_ptr(),12)},0);
        s.apply_anim_frame(0.,0.);let cfg=s.pv_config.clone().unwrap();
        for p in &cfg.pairs {for path in p {assert!(crate::pv::resolve(s.body_skel.as_ref().unwrap(),path).is_some(),"{card} unresolved actual prefab path {path}");}}
        let mut control=SessionBlob::from_session(&s).into_session();control.ensure_skel_export();
        assert_eq!(control.pv_mask,15);assert!(!control.pv_fk); // transient selection cannot leak into card cache
        control.anim_clip=s.anim_clip.clone();control.anim_scene_mode=4;
        let mut positive=0;let mut max=0f32;
        for (mask,fk) in [(15,false),(12,false),(12,true),(12,false),(15,false)] {
            assert_eq!(unsafe{crate::ffi::set_studio_pv(&mut s,mask,fk as i32)},0);
            assert_eq!(unsafe{crate::ffi::set_studio_pv(&mut control,mask,1)},0);
            for k in 0..4 {assert_eq!(unsafe{crate::ffi::studio_pv_flag(&mut s,k)},(!fk&&mask&(1<<k)!=0) as i32);}
            for frame in 0..12 {
                let t=frame as f32/60.;let mut input=Vec::new();
                for (k,p) in cfg.pairs.iter().enumerate() {
                    let id=crate::pv::resolve(s.body_shaped.as_ref().unwrap(),&p[0]).unwrap();let name=&s.body_shaped.as_ref().unwrap().tfs[&id].name;
                    let bone=s.skel_export.as_ref().unwrap().bones.iter().position(|b|b.name==*name&&!b.pose_locked).unwrap();
                    let mut value=crate::ffi::OkBodyPose{bone:bone as i32,position:[0.;3],rotation:[0.;4],scale:[0.;3]};
                    assert_eq!(unsafe{crate::ffi::external_body_pose_at(&mut s,bone as i32,&mut value)},0);
                    value.position[0]+=0.01*(k+1) as f32+t*0.013;value.position[1]-=0.023;value.scale=[0.91,1.17,0.83];input.push(value);
                }
                assert!(s.apply_external_body_frame(t,0.,&input));assert!(control.apply_external_body_frame(t,0.,&input));
                let a=s.body_skel.as_ref().unwrap();let b=control.body_skel.as_ref().unwrap();
                for p in &cfg.pairs {let id=crate::pv::resolve(a,&p[1]).unwrap();let other=crate::pv::resolve(b,&p[1]).unwrap();
                    let d=a.tfs[&id].pos.iter().zip(b.tfs[&other].pos).chain(a.tfs[&id].scale.iter().zip(b.tfs[&other].scale)).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max);
                    assert!(d.is_finite());max=max.max(d);positive+=(d>1e-4) as usize;
                    if fk {assert_eq!(d,0.);} }
                assert!(s.skel_export.as_ref().unwrap().bones.iter().all(|b|b.pose_pos.iter().chain(&b.pose_rot).chain(&b.pose_scl).all(|v|v.is_finite())));frames+=1;
            }
        }
        assert!(positive>0&&max>1e-3);changed+=positive;
        let prior=(s.pv_mask,s.pv_fk,s.anim_clip.clone());
        for bad in [-1,16] {assert_eq!(unsafe{crate::ffi::set_studio_pv(&mut s,bad,0)},1);assert_eq!((s.pv_mask,s.pv_fk,s.anim_clip.clone()),prior);atomic+=1;}
        assert_eq!(unsafe{crate::ffi::set_studio_animation_pv_profile(&mut s,key.as_ptr(),0,0.,0.,0,0.,c"".as_ptr(),c"".as_ptr(),0)},0);assert_eq!(s.pv_mask,15);
        eprintln!("PV Session card={card} frames=60 positive={positive} max={max} bindings=4 normal_reset=1 cache_reset=1");
    }
    eprintln!("PV Session accepted frames={frames} changed={changed} atomic={atomic}");
}
