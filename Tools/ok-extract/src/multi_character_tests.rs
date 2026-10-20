//! Real-card simultaneous-session ownership regression; no persistent schema changes.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::path::{Path, PathBuf};
use sha2::{Digest, Sha256};
use crate::session::{self, OkSession};

fn fingerprint(session: &OkSession) -> String {
    let mut hash=Sha256::new();
    hash.update(session.to_cache_bytes().unwrap());
    // The cache deliberately resets animated pose; check live pose separately.
    hash.update(bincode::serialize(&session.body_skel).unwrap());
    hash.update(bincode::serialize(&session.head_skel).unwrap());
    format!("{:x}",hash.finalize())
}

#[test]
#[ignore="requires installed cards; isolated process owns its environment"]
fn real_card_multi_character_isolation() {
    let game=PathBuf::from(std::env::var("OK_MULTI_GAME").unwrap());
    let card=PathBuf::from(std::env::var("OK_MULTI_CARD").unwrap());
    let output=PathBuf::from(std::env::var("OK_MULTI_REPORT").unwrap());
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE"] {std::env::set_var(key,"1");}
    let mut requests:Vec<(PathBuf,i32)>=[0,1,2,3,5,6].into_iter().map(|outfit|(card.clone(),outfit)).collect();
    for relative in ["UserData/chara/male/KK_224305.png","DefaultData/chara/male/ill_male_01.png","DefaultData/chara/male/ill_male_02.png"] {
        requests.push((game.join(relative),0));
    }
    // Same card/outfit twice must also own independent mutable pose state.
    requests.push((card.clone(),0));
    assert!(requests.iter().all(|(path,_)|path.ok_is_file()));
    let mut checks=Vec::new();
    for reverse in [false,true] {
        let order:Vec<usize>=if reverse {(0..requests.len()).rev().collect()}else{(0..requests.len()).collect()};
        let mut live:Vec<(usize,OkSession)>=Vec::new();
        for &id in &order {
            let before:Vec<String>=live.iter().map(|(_,s)|fingerprint(s)).collect();
            let (path,outfit)=&requests[id];
            let session=session::open(&game,Some(path),*outfit).unwrap();
            assert!(!session.parts.is_empty());
            assert!(session.parts.iter().all(|p|p.positions.iter().flatten().all(|v|v.is_finite())));
            for ((_,other),expected) in live.iter().zip(&before) {assert_eq!(&fingerprint(other),expected,"Loading another card mutated an existing session");}
            live.push((id,session));
        }
        for target in 0..live.len() {
            let before:Vec<String>=live.iter().map(|(_,s)|fingerprint(s)).collect();
            let (_,session)=&mut live[target];
            let mut body=session.shape_body.clone();let mut face=session.shape_face.clone();
            body[0]=if body[0]<0.5 {0.9}else{0.1};face[0]=if face[0]<0.5 {0.8}else{0.2};
            session.apply_shape(&body,&face);session.apply_anim_time(0.31);
            assert_ne!(fingerprint(session),before[target],"Negative control: target must actually change");
            for (index,(_,other)) in live.iter().enumerate() {
                if index!=target {assert_eq!(fingerprint(other),before[index],"Another actor's shape/pose/material state changed");}
            }
        }
        let before:Vec<String>=live.iter().skip(1).map(|(_,s)|fingerprint(s)).collect();
        drop(live.remove(0));
        for ((_,other),expected) in live.iter().zip(before) {assert_eq!(fingerprint(other),expected,"Dropping a session changed a survivor");}
        let (path,outfit)=&requests[order[0]];
        let reopened=session::open(&game,Some(path),*outfit).unwrap();
        let original=crate::card::parse_card_outfit(path,*outfit).unwrap();
        assert_eq!(reopened.shape_body,original.shape_body);
        assert_eq!(reopened.shape_face,original.shape_face);
        checks.push(serde_json::json!({"reverse_order":reverse,"simultaneous_sessions":requests.len(),
            "load_isolation":true,"independent_shape_pose_mutations":requests.len(),
            "destroy_survivor_isolation":true,"reopen_original_shapes":true}));
    }
    let cards:Vec<_>=requests.iter().map(|(p,o)|serde_json::json!({"path":p,"outfit":o,
        "sha256":format!("{:x}",Sha256::digest(crate::gamefs::read(p).unwrap()))})).collect();
    std::fs::create_dir_all(output.parent().unwrap_or(Path::new("."))).unwrap();
    std::fs::write(&output,serde_json::to_vec_pretty(&serde_json::json!({"passed":true,
        "scope":"Mutable session ownership with ten live characters, six Phoebe outfits, three male cards, duplicate card, both load orders; excludes full native dump pixel parity",
        "cards":cards,"checks":checks})).unwrap()).unwrap();
}
