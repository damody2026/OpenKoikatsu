use super::*;
use crate::ffi::OkBodyPose;

#[test]
#[ignore = "loads five installed cards and compares two 180-frame Animator paths"]
fn external_animator_matches_native_cards() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    std::env::remove_var("OK_NODB");
    let mut samples=0usize;let mut attachments=0usize;let mut rejected=0usize;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        std::env::set_var("OK_UE_ANIM","1");
        let path=game.join("UserData/chara/female").join(format!("{card}.png"));
        let mut initial=open(game,Some(&path),0).unwrap();
        let body=initial.shape_body.clone();let face=initial.shape_face.clone();initial.apply_shape(&body,&face);
        let bytes=initial.to_cache_bytes().unwrap();
        let mut native=OkSession::from_cache_bytes(&bytes).unwrap();
        let mut external=OkSession::from_cache_bytes(&bytes).unwrap();
        for session in [&mut native,&mut external] {
            session.anim_clip="states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01".into();
            session.anim_scene_mode=4;session.ensure_skel_export();
        }
        std::env::remove_var("OK_UE_ANIM");
        let duration=native.anim_duration();let mut max_position=0f32;let mut max_rotation=0f32;let mut max_vertex=0f32;
        for frame in 0..180 {
            let t=(frame as f32/60.)%duration;
            let mut animator=external.body_shaped.as_ref().unwrap().clone();
            crate::anim::apply_clip_at(game,&mut animator,&external.anim_clip,t);
            let input:Vec<OkBodyPose>=external.skel_export.as_ref().unwrap().bones.iter().enumerate()
                .filter(|(_,b)|!b.pose_locked)
                .filter_map(|(index,b)|animator.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&b.name))
                    .map(|tf|OkBodyPose{bone:index as i32,position:tf.pos,rotation:tf.rot,scale:tf.scale})).collect();
            assert!(!input.is_empty());
            if frame==90 {
                let before=external.to_cache_bytes().unwrap();
                let mut bad=input.clone();bad[0].position[0]=f32::NAN;
                assert!(!external.apply_external_body_frame(t,1./60.,&bad));rejected+=1;
                bad=input.clone();bad.push(bad[0]);assert!(!external.apply_external_body_frame(t,1./60.,&bad));rejected+=1;
                bad=input.clone();bad[0].bone=-1;assert!(!external.apply_external_body_frame(t,1./60.,&bad));rejected+=1;
                bad=input.clone();bad[0].rotation=[0.;4];assert!(!external.apply_external_body_frame(t,1./60.,&bad));rejected+=1;
                bad=input.clone();bad[0].scale[1]=0.;assert!(!external.apply_external_body_frame(t,1./60.,&bad));rejected+=1;
                assert!(!external.apply_external_body_frame(t,-1.,&input));rejected+=1;
                assert_eq!(external.to_cache_bytes().unwrap(),before,"invalid frame changed cached pose");
            }
            native.apply_anim_frame(t,1./60.);
            assert!(external.apply_external_body_frame(t,1./60.,&input));
            for (id,a) in &native.body_skel.as_ref().unwrap().tfs {
                let b=&external.body_skel.as_ref().unwrap().tfs[id];
                for c in 0..3 {max_position=max_position.max((a.pos[c]-b.pos[c]).abs());assert!((a.scale[c]-b.scale[c]).abs()<2e-5,"scale {}",a.name);}
                let direct=(0..4).map(|c|(a.rot[c]-b.rot[c]).abs()).fold(0f32,f32::max);
                let opposite=(0..4).map(|c|(a.rot[c]+b.rot[c]).abs()).fold(0f32,f32::max);
                max_rotation=max_rotation.max(direct.min(opposite));samples+=1;
                assert!(max_position<2e-6&&max_rotation<2e-5,"frame {frame} bone {} pos {max_position} rot {max_rotation}",a.name);
            }
            for (a,b) in native.parts.iter().zip(&external.parts).filter(|(a,_)|a.runtime_accessory.is_some()) {
                assert_eq!(a.positions.len(),b.positions.len());
                for (p,q) in a.positions.iter().zip(&b.positions) {for c in 0..3 {max_vertex=max_vertex.max((p[c]-q[c]).abs());} attachments+=1;}
                assert!(max_vertex<2e-5,"attachment {} max {max_vertex}",a.name);
            }
        }
        eprintln!("ExternalAnimator card={card} frames=180 position_max_m={max_position:.9} rotation_max={max_rotation:.9} attachment_max_m={max_vertex:.9}");
    }
    eprintln!("ExternalAnimator total frames=900 bone_samples={samples} attachment_vertices={attachments} rejected={rejected}");
}
