use super::*;
use serde::Deserialize;

#[derive(Deserialize)] struct V3 { x:f32,y:f32,z:f32 }
#[derive(Deserialize)] struct Q4 { x:f32,y:f32,z:f32,w:f32 }
#[derive(Deserialize)] struct Bone {name:String,position:V3,rotation:Q4,scale:V3}
#[derive(Deserialize)] struct NativeClip {name:String,weight:f32}
#[derive(Deserialize)] struct Frame {state:String,parameter:String,#[serde(default)]parameter2:String,value:f32,#[serde(default)]value2:f32,phase:f32,length:f32,clips:Vec<NativeClip>,bones:Vec<Bone>,
    #[serde(default)]controller:String,#[serde(default)]parameter3:String,#[serde(default)]value3:f32,
    #[serde(default,rename="baseParameter")]base_parameter:String,#[serde(default,rename="baseValue")]base_value:f32,
    #[serde(default)]layer:usize}

#[test]
#[ignore="installed native Animator 1D blend control"]
fn native_animator_one_d() {
    let root=&crate::test_support::game_path("");
    let mut errors=[0f32;3];let mut samples=0;
    for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
        let(count,max)=verify_native_card(root,card,false);samples+=count;
        for i in 0..3 {errors[i]=errors[i].max(max[i]);}
    }
    eprintln!("Animator1D accepted cards=5 frames=300 transform_samples={samples} position_max_m={} rotation_max={} scale_max={}",errors[0],errors[1],errors[2]);
}

#[test]
#[ignore="installed native Cartesian and override-layer controls"]
fn native_animator_cartesian() {
    let root=&crate::test_support::game_path("");let mut errors=[0f32;3];let mut samples=0;
    let cards=if std::env::var("OK_NATIVE_2D_MARCH_ONLY").as_deref()==Ok("1") {vec!["[Card] March_7th_keiyuu"]}
        else {vec!["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"]};
    for card in &cards {let(count,max)=verify_native_card(root,card,true);samples+=count;for i in 0..3 {errors[i]=errors[i].max(max[i]);}}
    eprintln!("Animator2D accepted cards={} frames={} transform_samples={samples} position_max_m={} rotation_max={} scale_max={}",cards.len(),cards.len()*144,errors[0],errors[1],errors[2]);
}

fn verify_native_card(root:&Path,card:&str,two_d:bool)->(usize,[f32;3]) {
    let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_' {c} else {'_'}).collect();
    let (stage,suffix,frames,kind)=if two_d {(290,"animator-2d",144,"Animator2D")} else {(289,"animator-blend",60,"Animator1D")};
    let native:Vec<Frame>=serde_json::from_slice(&crate::gamefs::read(format!("C:/portable/OpenKoikatsu/Saved/Tests/Animator{stage}/{stem}Animator{stage}.json.{suffix}.json")).unwrap()).unwrap();
    assert_eq!(native.len(),frames);
    let mut session=crate::session::open(root,Some(&root.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
    session.apply_anim_frame(0.,0.);
    let baseline=session.body_shaped.clone().unwrap();
    let tos=load_avatar_tos(root);
    let mut errors=[0f32;3];let mut samples=0;
    for f in native {
        assert!(f.length.is_finite()&&f.length>0.);
        let key=if two_d {format!("states:studio/anime/00.unity3d|{} / {}",f.controller,f.state)} else {format!("states:action/animator/00.unity3d|base / {}",f.state)};
        assert!(selectable_clip(root,&key),"{}",f.state);
        let mut parameters=HashMap::from([(f.parameter.clone(),f.value)]);
        if !f.parameter2.is_empty() {parameters.insert(f.parameter2.clone(),f.value2);}
        if !f.parameter3.is_empty() {parameters.insert(f.parameter3.clone(),f.value3);}
        if !f.base_parameter.is_empty() {parameters.insert(f.base_parameter.clone(),f.base_value);}
        let (source,entry,leaves,duration)=motion_plan(root,&key,&parameters).unwrap();
        let state_duration=clip_duration_parameters(root,&key,&parameters);
        assert!((f.length-state_duration).abs()<1e-4,"length {} native={} rust={} raw={}",f.state,f.length,state_duration,duration);
        let mut weights=HashMap::<String,f32>::new();let mut attrs=HashSet::new();
        for(clip,w,_,_) in leaves {
            *weights.entry(clip.name.clone()).or_default()+=w;
            let binds=load_clip_at(root,&format!("{source}|{}",clip.label),0.).unwrap().0;
            for b in binds {if matches!(b.attribute,1..=4) {if let Some(path)=tos.get(&b.path) {attrs.insert((bone_name(path).to_string(),b.attribute));}}}
        }
        let mut actual=HashMap::<String,f32>::new();for c in f.clips {*actual.entry(c.name).or_default()+=c.weight;}
        for name in weights.keys().chain(actual.keys()) {
            assert!((weights.get(name).copied().unwrap_or(0.)-actual.get(name).copied().unwrap_or(0.)).abs()<1e-5,"weight {} {name}",f.state);
        }
        if let Some(layer)=entry.layer {
            assert_eq!(layer.index,f.layer);
            let base=format!("states:{source}|{}",layer.base_label);
            let (_,_,leaves,_)=motion_plan(root,&base,&parameters).expect("layer base motion");
            for(clip,_,_,_) in leaves {
                let binds=load_clip_at(root,&format!("{source}|{}",clip.label),0.).unwrap().0;
                for b in binds {if matches!(b.attribute,1..=4) {if let Some(path)=tos.get(&b.path) {attrs.insert((bone_name(path).to_string(),b.attribute));}}}
            }
        }
        let mut pose=baseline.clone();apply_clip_at_parameters(root,&mut pose,&key,f.phase*state_duration,&parameters);
        if two_d&&f.phase==0.&&f.value==0.&&f.value2==0. {
            let e=motion_plan(root,&key,&parameters).unwrap().1;
            let layer=e.layer.as_ref().unwrap();let mut base=baseline.clone();
            apply_clip_at_parameters(root,&mut base,&format!("states:{source}|{}",layer.base_label),0.,&parameters);
            for name in ["cf_j_hand_L","cf_d_siri_R","cf_d_siri_L"] {
                let (&id,tf)=pose.tfs.iter().find(|(_,tf)|tf.name==name).unwrap();
                let rig_root=crate::pose::animator_root_id(&pose).unwrap();let mut current=id;let mut names=Vec::new();
                loop {let t=&pose.tfs[&current];names.push(t.name.clone());if current==rig_root {break;}current=t.parent.unwrap();}
                names.reverse();let path=names.join("/");let hash=unity_string_to_hash(&path) as u32;
                eprintln!("LayerDebug {name} path={path} hash={hash} mask={:?} base_pos={:?} base_rot={:?} pose_pos={:?} pose_rot={:?}",
                    layer.mask.iter().find(|(h,_)|*h==hash),base.tfs[&id].pos,base.tfs[&id].rot,tf.pos,tf.rot);
            }
        }
        let expected:HashMap<_,_>=f.bones.iter().map(|b|(&b.name,b)).collect();
        for(name,attr) in attrs {
            let Some(tf)=pose.tfs.values().find(|tf|tf.name==name) else {continue;};
            let Some(b)=expected.get(&name) else {continue;};
            let (a,b,index): (Vec<f32>,Vec<f32>,usize)=match attr {
                1=>(tf.pos.to_vec(),vec![b.position.x,b.position.y,b.position.z],0),
                3=>(tf.scale.to_vec(),vec![b.scale.x,b.scale.y,b.scale.z],2),
                _=>{let mut q=vec![b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w];if tf.rot.iter().zip(&q).map(|(a,b)|a*b).sum::<f32>()<0. {q.iter_mut().for_each(|v|*v=-*v);} (tf.rot.to_vec(),q,1)},
            };
            let error=a.iter().zip(&b).map(|(a,b)|(a-b).abs()).fold(0f32,f32::max);errors[index]=errors[index].max(error);samples+=1;
            assert!(error<2e-5,"{} parameter={} value={} phase={} bone={name} attr={attr} error={error} rust={a:?} native={b:?}",f.state,f.parameter,f.value,f.phase);
        }
    }
    eprintln!("{kind} card={card} frames={frames} transform_samples={samples} position_max_m={} rotation_max={} scale_max={}",errors[0],errors[1],errors[2]);
    assert!(samples>10000);
    (samples,errors)
}
