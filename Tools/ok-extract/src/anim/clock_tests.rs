use super::*;
use serde::Deserialize;

#[derive(Deserialize)] struct V3 {x:f32,y:f32,z:f32}
#[derive(Deserialize)] struct Q4 {x:f32,y:f32,z:f32,w:f32}
#[derive(Deserialize)] struct Bone {name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)] struct NativeClip {name:String,weight:f32,length:f32}
#[derive(Deserialize)] struct Frame {
    state:String,parameter:String,value:f32,elapsed:f32,length:f32,
    #[serde(rename="normalizedTime")] normalized_time:f32,
    speed:f32,#[serde(rename="speedMultiplier")] multiplier:f32,
    clips:Vec<NativeClip>,bones:Vec<Bone>,
    #[serde(default)]controller:String,#[serde(default)]layer:usize,
}

#[test]
#[ignore="five installed game Animator clock fixtures"]
fn native_animator_parameterized_clock() {
    let game=&crate::test_support::game_path("");
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    std::env::remove_var("OK_UE_ANIM");std::env::remove_var("OK_NODB");
    let tos=load_avatar_tos(game);let mut count=0;let mut attrs_count=0;let mut errors=[0f32;3];let mut positives=0;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_' {c} else {'_'}).collect();
        let mut frames:Vec<Frame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Animator292/{stem}Animator292.json.animator-clock.json")).unwrap()).unwrap();
        assert_eq!(frames.len(),24);
        let layer_frames:Vec<Frame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Animator292/{stem}Animator292Layer.json.animator-clock.json")).unwrap()).unwrap();
        assert_eq!(layer_frames.len(),12);frames.extend(layer_frames);
        let mut session=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
        session.apply_anim_frame(0.,0.);let baseline=session.body_shaped.as_ref().unwrap();let mut card_positive=0;
        for f in frames {
            let key=if f.layer==0 {format!("states:action/animator/00.unity3d|base / {}",f.state)} else {format!("states:studio/anime/00.unity3d|{} / {}",f.controller,f.state)};
            let mut parameters=HashMap::from([(f.parameter.clone(),f.value),("Speed".into(),0.37)]);
            if f.layer>0 {parameters.extend([("height".into(),0.37),("Breast".into(),0.61),("muneR_X".into(),0.31),("muneR_Y".into(),0.79)]);}
            let (source,entry,leaves,_)=motion_plan(game,&key,&parameters).unwrap();
            assert!((entry.timing.speed-f.speed*f.multiplier).abs()<1e-6);
            assert!((f.multiplier-f.value).abs()<1e-6);
            let duration=clip_duration_parameters(game,&key,&parameters);
            assert!((duration-f.length).abs()<1e-4,"native duration {card} {} multiplier={} rust={duration} game={}",f.state,f.value,f.length);
            assert!((f.elapsed/duration-f.normalized_time).abs()<2e-5,"native clock progression");
            let mut attrs=HashSet::new();let mut weights=HashMap::<String,f32>::new();
            for(clip,weight,_,length) in leaves {
                *weights.entry(clip.name.clone()).or_default()+=weight;
                if let Some(native)=f.clips.iter().find(|c|c.name==clip.name) {assert!((length-native.length).abs()<1e-5);}
                for binding in load_clip_at(game,&format!("{source}|{}",clip.label),0.).unwrap().0 {
                    if matches!(binding.attribute,1..=4) {if let Some(path)=tos.get(&binding.path) {attrs.insert((bone_name(path).to_string(),binding.attribute));}}
                }
            }
            if let Some(layer)=&entry.layer {
                for(clip,_,_,_) in motion_plan(game,&format!("states:{source}|{}",layer.base_label),&parameters).unwrap().2 {
                    for binding in load_clip_at(game,&format!("{source}|{}",clip.label),0.).unwrap().0 {
                        if matches!(binding.attribute,1..=4) {if let Some(path)=tos.get(&binding.path) {attrs.insert((bone_name(path).to_string(),binding.attribute));}}
                    }
                }
            }
            for c in &f.clips {assert!((weights.get(&c.name).copied().unwrap_or(0.)-c.weight).abs()<1e-5);}
            let mut pose=baseline.clone();apply_clip_at_parameters(game,&mut pose,&key,f.elapsed,&parameters);
            let mut old_parameters=parameters.clone();old_parameters.remove(&f.parameter);
            let mut old=baseline.clone();apply_clip_at_parameters(game,&mut old,&key,f.elapsed,&old_parameters);
            if pose.tfs.iter().any(|(id,a)|a.pos.iter().zip(old.tfs[id].pos).chain(a.rot.iter().zip(old.tfs[id].rot)).any(|(a,b)|(a-b).abs()>1e-4)) {card_positive+=1;positives+=1;}
            for b in &f.bones {
                let Some(a)=pose.tfs.values().find(|a|a.name==b.name) else {continue;};
                let p=[b.position.x,b.position.y,b.position.z];let s=[b.scale.x,b.scale.y,b.scale.z];let q=[b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];
                if attrs.contains(&(b.name.clone(),1)) {for i in 0..3 {errors[0]=errors[0].max((a.pos[i]-p[i]).abs());}attrs_count+=1;}
                if attrs.contains(&(b.name.clone(),2))||attrs.contains(&(b.name.clone(),4)) {
                    let same=(0..4).map(|i|(a.rot[i]-q[i]).abs()).fold(0f32,f32::max);
                    let opposite=(0..4).map(|i|(a.rot[i]+q[i]).abs()).fold(0f32,f32::max);
                    errors[1]=errors[1].max(same.min(opposite));attrs_count+=1;
                }
                if attrs.contains(&(b.name.clone(),3)) {for i in 0..3 {errors[2]=errors[2].max((a.scale[i]-s[i]).abs());}attrs_count+=1;}
                assert!(errors.iter().all(|v|*v<2e-5),"native clock TRS {card} {} elapsed={} multiplier={} bone={} errors={errors:?}",f.state,f.elapsed,f.value,b.name);
            }
            count+=1;
        }
        assert!(card_positive>=18,"parameter must affect native clock poses");
        eprintln!("AnimatorClock native card={card} frames=36 positive={card_positive} errors={errors:?}");
    }
    assert!(attrs_count>10000);
    eprintln!("AnimatorClock native accepted cards=5 frames={count} transform_attributes={attrs_count} positive={positives} position_max={} rotation_max={} scale_max={}",errors[0],errors[1],errors[2]);
}
