//! Lossless ExtendedSave editing for maker bone modifiers.
use crate::abmx::{AbmBone, AbmData};
use rmpv::Value;

#[derive(Default, serde::Serialize, serde::Deserialize)]
pub struct EditorMods {
    #[serde(default)] pub abm: Vec<AbmBone>,
    #[serde(default)] pub pregnancy: crate::pregnancy::Settings,
}

fn get<'a>(v: &'a Value, key: &str) -> Option<&'a Value> {
    v.as_map()?.iter().find(|(k,_)|k.as_str()==Some(key)).map(|(_,v)|v)
}
pub fn decode(bytes: &[u8]) -> Result<EditorMods, String> {
    if bytes.is_empty() { return Ok(EditorMods::default()); }
    let root = rmpv::decode::read_value(&mut &bytes[..]).map_err(|e|e.to_string())?;
    let mut mods = EditorMods::default();
    if let Some(plugin) = get(&root, "KKABMPlugin.ABMData") {
        if let Some(bytes) = plugin.as_array().and_then(|a|a.get(1)).and_then(|v|get(v,"boneData")).and_then(Value::as_slice) {
            mods.abm = crate::abmx::decode(bytes);
        }
    }
    if let Some(data)=get(&root,"KK_PregnancyPlus").and_then(|p|p.as_array()).and_then(|a|a.get(1)) {
        if let Some(v)=get(data,"GameplayEnabled").and_then(Value::as_bool){mods.pregnancy.enabled=v;}
        if let Some(entries)=data.as_map(){for(k,v)in entries{if let Some(key)=k.as_str(){if key.starts_with("inflation"){if let Some(n)=v.as_f64(){mods.pregnancy.values.insert(key.to_string(),n as f32);}}}}}
    }
    Ok(mods)
}
pub fn valid(mods: &EditorMods) -> bool {
    mods.pregnancy.values.len()<=32 && mods.pregnancy.values.iter().all(|(k,v)|k.starts_with("inflation")&&v.is_finite()&&v.abs()<=100.) && mods.abm.len() <= 8192 && mods.abm.iter().all(|b| !b.name.is_empty() && b.name.len()<=512 && !b.coord.is_empty() && b.coord.len()<=256 && b.coord.iter().all(|d|
        d.scale.iter().chain(d.pos.iter()).chain(d.rot.iter()).chain(std::iter::once(&d.length)).all(|v|v.is_finite()) && d.scale.iter().all(|v|*v>=0.) && d.length>=0.))
}
fn data_value(d: &AbmData) -> Value {
    let a = |v:[f32;3]|Value::Array(v.into_iter().map(Value::F32).collect());
    Value::Array(vec![a(d.scale),Value::F32(d.length),a(d.pos),a(d.rot)])
}
pub fn encode(bytes: &[u8], mods: &EditorMods) -> Result<Vec<u8>, String> {
    if !valid(mods) { return Err("invalid bone modifiers".into()); }
    let mut root = if bytes.is_empty(){Value::Map(vec![])}else{rmpv::decode::read_value(&mut &bytes[..]).map_err(|e|e.to_string())?};
    let entries=match &mut root {Value::Map(entries)=>entries,_=>return Err("ExtendedSave is not a map".into())};
    let bones=Value::Array(mods.abm.iter().filter(|b|b.coord.iter().any(|d|!d.is_empty())).map(|b|
        Value::Array(vec![b.name.clone().into(),Value::Array(b.coord.iter().map(data_value).collect()),b.location.into()])).collect());
    let mut packed=vec![];rmpv::encode::write_value(&mut packed,&bones).map_err(|e|e.to_string())?;
    let mut payload=vec![];rmpv::encode::write_value(&mut payload,&Value::from(packed.len() as u32)).map_err(|e|e.to_string())?;
    payload.extend(lz4_flex::block::compress(&packed));
    let mut binary=vec![];rmpv::encode::write_value(&mut binary,&Value::Ext(99,payload)).map_err(|e|e.to_string())?;
    let plugin=if let Some((_,old))=entries.iter_mut().find(|(k,_)|k.as_str()==Some("KKABMPlugin.ABMData")){old}else{
        entries.push(("KKABMPlugin.ABMData".into(),Value::Array(vec![2.into(),Value::Map(vec![])])));&mut entries.last_mut().unwrap().1};
    let Value::Array(array)=plugin else{return Err("invalid ABMX plugin data".into());};
    if array.first().and_then(Value::as_i64)!=Some(2){return Err("unsupported ABMX plugin version".into());}
    let Some(Value::Map(data))=array.get_mut(1) else{return Err("invalid ABMX plugin data".into());};
    if let Some((_,old))=data.iter_mut().find(|(k,_)|k.as_str()==Some("boneData")){*old=Value::Binary(binary);}else{data.push(("boneData".into(),Value::Binary(binary)));}
    let preg=if let Some((_,old))=entries.iter_mut().find(|(k,_)|k.as_str()==Some("KK_PregnancyPlus")){old}else{
        entries.push(("KK_PregnancyPlus".into(),Value::Array(vec![1.into(),Value::Map(vec![])])));&mut entries.last_mut().unwrap().1};
    let Value::Array(array)=preg else{return Err("invalid PregnancyPlus plugin data".into());};
    let Some(Value::Map(data))=array.get_mut(1) else{return Err("invalid PregnancyPlus plugin data".into());};
    let mut set=|key:&str,value:Value|{if let Some((_,old))=data.iter_mut().find(|(k,_)|k.as_str()==Some(key)){*old=value;}else{data.push((key.into(),value));}};
    set("GameplayEnabled",Value::Boolean(mods.pregnancy.enabled));
    set("pluginVersion",Value::String("7.8".into()));
    for key in ["inflationSize","inflationMultiplier","inflationRoundness","inflationMoveY","inflationMoveZ","inflationStretchX","inflationStretchY","inflationShiftY","inflationShiftZ","inflationTaperY","inflationTaperZ","inflationDrop","inflationClothOffset","inflationFatFold","inflationFatFoldHeight","inflationFatFoldGap"] {set(key,Value::F32(mods.pregnancy.get(key)));}
    let mut out=vec![];rmpv::encode::write_value(&mut out,&root).map_err(|e|e.to_string())?;Ok(out)
}

/// The mods (bone modifiers, pregnancy) and then the sliders, in the order the editor applied them one by one: the
/// bone modifiers rebuild the shape with the session's values, pregnancy, then the given sliders when they differ.
pub(crate) fn apply_then_shape(sess:&mut crate::session::OkSession,mods:&EditorMods,body:&[f32],face:&[f32])->i32{
    if sess.abm!=mods.abm{sess.abm=mods.abm.clone();let b=sess.shape_body.clone();let f=sess.shape_face.clone();sess.apply_shape(&b,&f);}
    let changed=crate::pregnancy::apply(sess,&mods.pregnancy);
    if sess.shape_body!=body||sess.shape_face!=face{sess.apply_shape(body,face);}
    if changed{2}else{0}
}
/// Same result as `apply_then_shape`, building the shape once when the pregnancy edit changes nothing (it reads the
/// shaped body, so with one to apply the order above is kept). A card's load: ~0.12 s less.
pub(crate) fn apply_with_shape(sess:&mut crate::session::OkSession,mods:&EditorMods,body:&[f32],face:&[f32])->i32{
    if !crate::pregnancy::is_noop(sess,&mods.pregnancy){return apply_then_shape(sess,mods,body,face);}
    let abm_changed=sess.abm!=mods.abm;if abm_changed{sess.abm=mods.abm.clone();}
    crate::pregnancy::apply(sess,&mods.pregnancy);
    if abm_changed||sess.shape_body!=body||sess.shape_face!=face{sess.apply_shape(body,face);}
    0
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn extended_save_preserves_other_plugins_and_coordinate_data() {
        let root=Value::Map(vec![("other.plugin".into(),Value::Array(vec![7.into(),Value::Map(vec![("secret".into(),Value::Binary(vec![0,1,255]))])])),
            ("KKABMPlugin.ABMData".into(),Value::Array(vec![2.into(),Value::Map(vec![("future".into(),Value::Binary(vec![2,3,255]))]),"extra".into()])),
            ("KK_PregnancyPlus".into(),Value::Array(vec![1.into(),Value::Map(vec![("meshBlendShape".into(),Value::Binary(vec![9,8,7]))])]))]);
        let mut bytes=vec![];rmpv::encode::write_value(&mut bytes,&root).unwrap();
        let mods=EditorMods{pregnancy:Default::default(),abm:vec![AbmBone{name:"cf_s_neck".into(),location:0,coord:vec![AbmData{scale:[1.2,1.3,1.4],length:1.5,pos:[0.01,0.02,0.03],rot:[10.,20.,30.]},AbmData::default()]}]};
        let encoded=encode(&bytes,&mods).unwrap();let decoded=decode(&encoded).unwrap();
        assert_eq!(decoded.abm,mods.abm);assert!(decoded.pregnancy.values.values().all(|v|*v==0.));
        let actual=rmpv::decode::read_value(&mut &encoded[..]).unwrap();assert_eq!(get(&root,"other.plugin"),get(&actual,"other.plugin"));
        let before=get(&root,"KKABMPlugin.ABMData").unwrap().as_array().unwrap();let after=get(&actual,"KKABMPlugin.ABMData").unwrap().as_array().unwrap();
        assert_eq!(get(&before[1],"future"),get(&after[1],"future"));assert_eq!(before[2],after[2]);
        assert_eq!(get(&get(&root,"KK_PregnancyPlus").unwrap().as_array().unwrap()[1],"meshBlendShape"),get(&get(&actual,"KK_PregnancyPlus").unwrap().as_array().unwrap()[1],"meshBlendShape"));
        let mut bad=mods;bad.abm[0].coord[0].scale[0]=f32::NAN;assert!(encode(&bytes,&bad).is_err());
    }
    #[test] fn future_plugin_version_is_not_silently_overwritten(){
        let root=Value::Map(vec![("KKABMPlugin.ABMData".into(),Value::Array(vec![99.into(),Value::Map(vec![])]))]);
        let mut bytes=vec![];rmpv::encode::write_value(&mut bytes,&root).unwrap();
        assert!(encode(&bytes,&EditorMods::default()).unwrap_err().contains("unsupported"));
    }
    #[test] fn native_bone_edit_survives_animation_and_physics_controls(){
        let mut s=crate::oklib::open_test_card(&std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C000.png"),0).unwrap();
        s.abm.retain(|b|b.name!="cf_s_shoulder02_L"&&b.name!="cf_s_shoulder02_R");
        let body=s.shape_body.clone();let face=s.shape_face.clone();s.apply_shape(&body,&face);
        let scale=|s:&crate::session::OkSession,name:&str|s.body_skel.as_ref().unwrap().tfs.values().find(|t|t.name==name).unwrap().scale;
        let left=scale(&s,"cf_s_shoulder02_L");let right=scale(&s,"cf_s_shoulder02_R");
        let mut mods=EditorMods{abm:s.abm.clone(),pregnancy:Default::default()};
        mods.abm.push(AbmBone{name:"cf_s_shoulder02_L".into(),location:1,coord:vec![AbmData{scale:[1.35;3],..Default::default()}]});
        let json=std::ffi::CString::new(serde_json::to_vec(&mods).unwrap()).unwrap();
        unsafe{assert_eq!(crate::ffi::ok_editor_apply_mods(&mut s,json.as_ptr(),0),0);}
        for i in 0..3{assert!((scale(&s,"cf_s_shoulder02_L")[i]-left[i]*1.35).abs()<1e-5);}
        assert_eq!(scale(&s,"cf_s_shoulder02_R"),right);
        s.apply_anim_frame(0.2,0.);
        assert!(s.body_skel.as_ref().unwrap().tfs.values().all(|t|t.scale.iter().chain(&t.pos).all(|v|v.is_finite())));
        unsafe{assert_eq!(crate::ffi::ok_editor_body_physics(&mut s,0.8,0.3),0);assert_eq!(crate::ffi::ok_editor_body_physics(&mut s,f32::NAN,0.9),-1);}
        assert_eq!(s.bust_softness,0.8);assert_eq!(s.bust_weight,0.3);
        assert_eq!(s.abm.last().unwrap().coord[0].scale,[1.35;3]);
    }

    #[test]
    #[ignore = "reads the OK base library and project cards"]
    fn mods_then_shape_once_equals_two_steps(){
        let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let lib=root.join("Content/IndependentResources/base_v1");
        for name in ["C030","C031","C033","C044"]{
            let card=root.join(format!("Content/CharacterCards/CrowdAPNG/{name}.png"));
            if !card.is_file(){continue;}
            let bytes=std::fs::read(&card).unwrap();
            let info=crate::card::parse_card(&card).unwrap();
            let blocks=crate::appearance::blocks_from_card(&bytes).unwrap();
            let sess=crate::session::open(&lib,Some(&card),0).unwrap();
            let preview=sess.to_preview_bytes().unwrap();
            // The card's sliders, and also a set that differs from the session's on every slider.
            let other:Vec<f32>=info.shape_body.iter().map(|v|v+0.25).collect();
            for (body,face) in [(info.shape_body.clone(),info.shape_face.clone()),(other,info.shape_face.clone())]{
                let mut a=crate::session::OkSession::from_preview_bytes(&preview).unwrap();
                let mut b=crate::session::OkSession::from_preview_bytes(&preview).unwrap();
                let mut m=decode(blocks.get("KKEx").map(Vec::as_slice).unwrap_or(&[])).unwrap();for x in &mut m.abm{let active=x.for_coord(0);x.coord=vec![active];}
                assert_eq!(apply_then_shape(&mut a,&m,&body,&face),apply_with_shape(&mut b,&m,&body,&face),"{name}");
                assert!(a.to_preview_bytes().unwrap()==b.to_preview_bytes().unwrap(),"{name}: one shape build differs from two (abm={})",m.abm.len());
                eprintln!("{name}: identical (abm={}, pregnancy_noop={})",m.abm.len(),crate::pregnancy::is_noop(&a,&m.pregnancy));
            }
        }
    }
}
