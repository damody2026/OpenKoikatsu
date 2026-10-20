use super::*;
use std::ffi::CString;

fn vector(v:&serde_json::Value)->[f32;3] {std::array::from_fn(|i|v[["x","y","z"][i]].as_f64().unwrap() as f32)}
fn quaternion(v:&serde_json::Value)->[f32;4] {std::array::from_fn(|i|v[["x","y","z","w"][i]].as_f64().unwrap() as f32)}

#[test]
#[ignore="five installed cards: cold GPU load followed directly by source H and EyeLook"]
fn studio_yure_eye_look_diagnostic() {
    let game=&crate::test_support::game_path("");std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");
    let desc=["studio/anime/00.unity3d","khh_f_base","studio/anime/00.unity3d","khh_f_28"];
    let source=format!("states:studio@{}",serde_json::to_vec(&desc).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>());
    let label=crate::anim::clip_names_for_source(game,&source).into_iter().find(|v|v=="khh_f_base / [Studio layer 79] Base Layer.Stop_Idle").unwrap();
    let key=CString::new(format!("{source}|{label}")).unwrap();
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
    let mut s=open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
    s.ensure_skel_export(); // UE loads the GPU mesh before choosing the first Studio action.
    assert!(s.body_shaped.is_none()&&s.head_shaped.is_none(),"this control must exercise the actual cold cache load");
    assert_eq!(unsafe{crate::ffi::set_studio_animation_profile(&mut s,key.as_ptr(),1,0.,0.,0,0.,c"h/list/01.unity3d".as_ptr(),c"yure_khh_28_00".as_ptr())},0);
    assert!(s.body_shaped.is_some()&&s.head_shaped.is_some(),"first H selection must initialize both source baselines");
    s.apply_anim_frame(0.,0.);
    for step in 1..=24 {s.apply_anim_frame(0.4*step as f32/24.,0.4/24.);}
    assert!(s.body_skel.as_ref().unwrap().tfs.values().all(|t|t.pos.iter().chain(&t.rot).chain(&t.scale).all(|v|v.is_finite())),"H sampled physics produced nonfinite locals");
    let mut frozen=[0.;3];
    for i in 0..60 {let rates=s.step_eye_look([0.45,1.55,2.],1,1./60.,true).unwrap_or_else(||panic!("EyeLook missing basis at step {i}"));assert!(rates.iter().all(|v|v.is_finite()),"step {i}: {rates:?}");frozen=rates;}
    assert_eq!(s.step_eye_look([0.45,1.55,2.],1,0.,true).unwrap(),frozen);
    eprintln!("Yure cold H EyeLook card={card} rates={frozen:?} steps=60 paused_hold=1");
    }
}

#[test]
#[ignore="installed cards and actual OCI.LoadAnime Yure controls"]
fn studio_yure_policy_and_shape_initialization() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let mut count=0;let mut samples=0;let mut negatives=0;let mut invalid=0;let mut global=0f32;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        if std::env::var("OK_TEST_YURE_MARCH").is_ok()&&card!="[Card] March_7th_keiyuu" {continue;}
        let mut s=open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
        s.apply_anim_frame(0.,0.);s.ensure_skel_export();let unmasked=s.body_shaped.clone().unwrap();
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_'{c}else{'_'}).collect();
        let frames:Vec<serde_json::Value>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Studio294/{stem}Studio294.json.yure.json")).unwrap()).unwrap();
        assert_eq!(frames.len(),9);let mut max=0f32;
        for f in frames {
            let data=[f["source"].as_str().unwrap(),f["owner"].as_str().unwrap(),f["overlaySource"].as_str().unwrap(),f["overlayOwner"].as_str().unwrap()];
            let source=format!("states:studio@{}",serde_json::to_vec(&data).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>());
            let prefix=format!("{} / [Studio layer {}] ",data[1],f["layer"].as_u64().unwrap());let state=f["state"].as_str().unwrap();
            let candidates:Vec<_>=crate::anim::clip_names_for_source(game,&source).into_iter().filter(|s|s.strip_prefix(&prefix).is_some_and(|p|p==state||p.ends_with(&format!(".{state}")))).collect();
            assert_eq!(candidates.len(),1);let key=CString::new(format!("{source}|{}",candidates[0])).unwrap();
            let ysource=CString::new(f["yureSource"].as_str().unwrap()).unwrap();let yowner=CString::new(f["yureOwner"].as_str().unwrap()).unwrap();
            assert_eq!(unsafe{crate::ffi::set_studio_animation_profile(&mut s,key.as_ptr(),f["isH"].as_bool().unwrap() as i32,0.37,0.61,f["isMotion"].as_bool().unwrap() as i32,0.43,ysource.as_ptr(),yowner.as_ptr())},0);
            let policy=s.yure_policy.unwrap_or_default();
            assert_eq!(policy.active,f["active"].as_array().unwrap().iter().map(|v|v.as_bool().unwrap()).collect::<Vec<_>>().as_slice(),"{card} {state}");
            assert_eq!(policy.shapes.iter().flatten().copied().collect::<Vec<_>>(),f["shapeEnabled"].as_array().unwrap().iter().map(|v|v.as_bool().unwrap()).collect::<Vec<_>>());
            let sk=s.body_shaped.as_ref().unwrap();let roots=crate::pose::subtree_ids_pub(crate::pose::animator_root_id(sk).unwrap(),&sk.tfs);
            for(name,pos,rot,scale) in crate::shape::breast_written_channels() {
                let native=f["shapeBones"].as_array().unwrap().iter().find(|b|b["name"].as_str()==Some(name)).unwrap();
                let id=roots.iter().copied().find(|id|sk.tfs[id].name==name).unwrap();let tf=&sk.tfs[&id];
                let np=vector(&native["position"]);let ns=vector(&native["scale"]);let nq=quaternion(&native["rotation"]);
                for i in 0..3 {if pos[i] {max=max.max((tf.pos[i]-np[i]).abs());samples+=1;}if scale[i] {max=max.max((tf.scale[i]-ns[i]).abs());samples+=1;}}
                if rot {let same=(0..4).map(|i|(tf.rot[i]-nq[i]).abs()).fold(0f32,f32::max);let opposite=(0..4).map(|i|(tf.rot[i]+nq[i]).abs()).fold(0f32,f32::max);max=max.max(same.min(opposite));samples+=1;}
                assert!(max<2e-5,"Yure shape {card} {state} bone={name} native={np:?}/{ns:?}/{nq:?} actual={:?}/{:?}/{:?} error={max}",tf.pos,tf.scale,tf.rot);
            }
            if !policy.shapes[0][0] {
                assert!(sk.tfs.iter().any(|(id,t)|t.name.starts_with("cf_s_bust")&&t.pos.iter().zip(unmasked.tfs[id].pos).chain(t.scale.iter().zip(unmasked.tfs[id].scale)).any(|(a,b)|(a-b).abs()>1e-4)),"mask must change card-shaped baseline");negatives+=1;
                if f["category"].as_i64()==Some(128)&&f["no"].as_i64()==Some(0) {
                    let restored=SessionBlob::from_session(&s).into_session();assert!(restored.yure_policy.is_none());
                    let canonical=restored.body_shaped.as_ref().unwrap();
                    assert!(canonical.tfs.iter().all(|(id,t)|t.pos==unmasked.tfs[id].pos&&t.rot==unmasked.tfs[id].rot&&t.scale==unmasked.tfs[id].scale),"runtime Yure mask must not leak into cached card baseline");
                }
            }
            let before=s.body_shaped.clone().unwrap();let prior=s.yure_policy;let oldkey=s.anim_clip.clone();
            assert_eq!(unsafe{crate::ffi::set_studio_animation_profile(&mut s,key.as_ptr(),1,0.37,0.61,0,0.43,c"../bad.unity3d".as_ptr(),c"asset".as_ptr())},1);
            assert_eq!(s.yure_policy,prior);assert_eq!(s.anim_clip,oldkey);assert!(s.body_shaped.as_ref().unwrap().tfs.iter().all(|(id,t)|t.pos==before.tfs[id].pos&&t.rot==before.tfs[id].rot&&t.scale==before.tfs[id].scale));invalid+=1;
            s.apply_anim_frame(0.23,1./60.);assert!(s.skel_export.as_ref().unwrap().bones.iter().all(|b|b.pose_pos.iter().chain(b.pose_rot.iter()).chain(b.pose_scl.iter()).all(|v|v.is_finite())));
            count+=1;
        }
        global=global.max(max);eprintln!("Yure Session card={card} loads=9 shape_max={max}");
    }
    eprintln!("Yure Session accepted loads={count} shape_attributes={samples} masked_negative={negatives} invalid_atomic={invalid} max={global}");
}
