//! Source material replay for the native SkinPlus adapter. Not yet selected by
//! the renderer: sampling, vertex and lighting integration have separate gates.
use crate::{card::CardInfo, unity::MeshMaterial};
use std::collections::HashMap;

pub(crate) enum SkinDraw {
    Body { skin_tuya_rate: f32 },
    Face { hoho_aka_rate: f32 },
}

fn float(mat: &mut MeshMaterial, name: &str, value: f32) {
    mat.floats.retain(|k, _| !k.eq_ignore_ascii_case(name));
    mat.floats.insert(name.into(), value);
}
fn color(mat: &mut MeshMaterial, name: &str, value: [f32; 4]) {
    mat.colors.retain(|k, _| !k.eq_ignore_ascii_case(name));
    mat.colors.insert(name.into(), value);
}

/// Caller supplies the XML defaults resolved by the installed registration.
/// They are applied only on an actual SetShader, not every material update.
pub(crate) fn replay(
    source: &MeshMaterial, card: &CardInfo, draw: SkinDraw,
    xml_floats: &HashMap<String, f32>, xml_colors: &HashMap<String, [f32; 4]>,
) -> Option<MeshMaterial> {
    let identity = crate::card::me_mat_key(&source.name);
    let edit = card.me_shader.get(&(4, 0, 0, identity.clone()));
    let target = edit.map(|(name, _)| name.as_str()).unwrap_or(&source.shader);
    if !target.eq_ignore_ascii_case("xukmi/SkinPlus") { return None; }
    let schema: serde_json::Value = serde_json::from_str(include_str!(
        "../../../Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/SkinPlusBindings.json"
    )).expect("generated native SkinPlus schema");
    let props = schema["properties"].as_array().expect("native properties");
    let mut mat = source.clone();
    // ChaControl.ChangeSetting* precedes the MaterialEditor reload coroutine.
    match draw {
        SkinDraw::Body { skin_tuya_rate } => {
            float(&mut mat, "_DetailNormalMapScale", card.body_detail_power);
            float(&mut mat, "_SpecularPower", card.skin_gloss_power +
                (1. - card.skin_gloss_power) * skin_tuya_rate.clamp(0., 1.));
            float(&mut mat, "_SpecularPowerNail", card.nail_gloss_power);
            float(&mut mat, "_nipsize", card.areola_size);
            float(&mut mat, "_nip_specular", card.nip_gloss_power);
            float(&mut mat, "_linetexon", if card.body_draw_add_line {1.} else {0.});
            color(&mut mat, "_overcolor1", card.nip_color);
            color(&mut mat, "_overcolor2", card.underhair_color);
        },
        SkinDraw::Face { hoho_aka_rate } => {
            float(&mut mat, "_DetailNormalMapScale", card.face_detail_power);
            float(&mut mat, "_SpecularPower", card.cheek_gloss_power);
            float(&mut mat, "_SpecularPowerNail", card.lip_gloss_power);
            color(&mut mat, "_overcolor1", card.lip_color);
            color(&mut mat, "_overcolor3", card.eyeshadow_color);
            // Cheek tint is written to create-face Color4, not draw overcolor2.
            // ChangeHohoAka updates only draw alpha before ME overrides.
            let mut blush=mat.color("_overcolor2").unwrap_or(card.cheek_color);
            blush[3]=0.2*hoho_aka_rate.clamp(0.,1.);
            color(&mut mat,"_overcolor2",blush);
        },
    }
    mat.shader = target.into();
    mat.shader_has_rim = Some(props.iter().any(|p|p["m_Name"] == "_rimV"));
    // Unity changing shader preserves saved values, including hidden values.
    // Only properties absent from that state acquire ShaderLab defaults.
    let mut names = HashMap::new();
    for p in props {
        let name = p["m_Name"].as_str().unwrap();
        names.insert(name.to_ascii_lowercase(), name.to_string());
        let values: [f32; 4] = std::array::from_fn(|i|p[format!("m_DefValue[{i}]")].as_f64().unwrap() as f32);
        match p["m_Type"].as_i64().unwrap() {
            0 | 1 if !mat.colors.keys().any(|k|k.eq_ignore_ascii_case(name)) => color(&mut mat,name,values),
            2 | 3 if !mat.floats.keys().any(|k|k.eq_ignore_ascii_case(name)) => float(&mut mat,name,values[0]),
            4 => for (axis,value) in ["x","y","z","w"].into_iter().zip([1.,1.,0.,0.]) {
                let key=format!("{name}_ST_{axis}");
                if !mat.floats.keys().any(|k|k.eq_ignore_ascii_case(&key)) {float(&mut mat,&key,value);}
            },
            _ => {},
        }
    }
    let canonical = |property: &str| {
        let key=format!("_{}",property.trim_start_matches('_')).to_ascii_lowercase();
        names.get(&key).cloned()
    };
    if edit.is_some() {
        for (name,value) in xml_floats {if let Some(name)=canonical(name) {float(&mut mat,&name,*value);}}
        for (name,value) in xml_colors {if let Some(name)=canonical(name) {color(&mut mat,&name,*value);}}
        if let Some(queue)=edit.and_then(|(_,queue)|*queue) {mat.render_queue=queue;}
    }
    for ((object,coord,slot,material,property),value) in &card.me_float {
        if (*object,*coord,*slot)!=(4,0,0) || *material!=identity {continue;}
        if let Some(name)=canonical(property) {float(&mut mat,&name,*value);}
        else {
            // ST originates from independent nullable texture Scale/Offset.
            let key=format!("_{}",property.trim_start_matches('_'));
            if let Some((base,axis))=key.rsplit_once("_st_") {
                if matches!(axis,"x"|"y"|"z"|"w") {
                    if let Some(name)=canonical(base) {
                        if props.iter().any(|p|p["m_Name"]==name && p["m_Type"]==4) {
                            float(&mut mat,&format!("{name}_ST_{axis}"),*value);
                        }
                    }
                }
            }
        }
    }
    for ((object,coord,slot,material,property),value) in &card.me_color {
        if (*object,*coord,*slot)==(4,0,0) && *material==identity {
            if let Some(name)=canonical(property) {color(&mut mat,&name,*value);}
        }
    }
    Some(mat)
}

/// Keep authored resources independent: a null slot is a ShaderLab default,
/// while a non-null resource that cannot be resolved is a transport failure.
/// PNG contains decoded source level zero, never a composited body/face mask.
#[derive(Clone, Debug)]
pub(crate) struct SkinTexture {
    pub name: String,
    pub png: Vec<u8>,
    pub sampling: crate::unity::TextureSampling,
}
#[derive(Clone, Debug)]
pub(crate) enum SkinTextureSlot {
    Default(String),
    Bound(SkinTexture),
    Unresolved(String),
}

pub(crate) fn authored_textures(
    material: &MeshMaterial,
    mut resolve: impl FnMut(&str)->Option<SkinTexture>,
) -> HashMap<String,SkinTextureSlot> {
    let schema:serde_json::Value=serde_json::from_str(include_str!(
        "../../../Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/SkinPlusBindings.json"
    )).expect("native SkinPlus schema");
    schema["properties"].as_array().unwrap().iter().filter(|p|p["m_Type"]==4).map(|p| {
        let property=p["m_Name"].as_str().unwrap().to_string();
        let slot=match material.slot(&property).filter(|n|!n.is_empty()) {
            Some(name)=>resolve(name).map(SkinTextureSlot::Bound)
                .unwrap_or_else(||SkinTextureSlot::Unresolved(name.into())),
            None=>SkinTextureSlot::Default(p["m_DefTexture"]["m_DefaultName"].as_str()
                .expect("native texture default").into()),
        };
        (property,slot)
    }).collect()
}

/// ChaControl.ChangeTexture always calls SetTexture, including null for id0
/// entries. A caller must distinguish a resolved null from a missing asset.
pub(crate) fn replay_overlay_textures(
    slots: &mut HashMap<String,SkinTextureSlot>, card:&CardInfo, body:bool,
    mut catalog:impl FnMut(i32,i32,&str)->Result<Option<SkinTexture>,String>,
)->Result<(),String> {
    let changes=if body {vec![("_overtex1",423,card.nip_id,"NipTex"),
        ("_overtex2",424,card.underhair_id,"UnderhairTex")]}
        else {vec![("_overtex1",403,card.lip_id,"LipTex"),
        ("_overtex3",401,card.eyeshadow_id,"EyeshadowTex")]};
    for (property,cat,id,key) in changes {
        let texture=catalog(cat,id,key)?;
        let default=match slots.get(property) {Some(SkinTextureSlot::Default(name))=>name.clone(),_=>"black".into()};
        slots.insert(property.into(),texture.map(SkinTextureSlot::Bound)
            .unwrap_or(SkinTextureSlot::Default(default)));
    }
    Ok(())
}

/// Transfer each bound slot directly; no channel repacking, alias or tint.
/// Missing resources fail before mutation. Defaults remain explicit above.
pub(crate) fn transport_textures(slots:&HashMap<String,SkinTextureSlot>,
    pngs:&mut std::collections::BTreeMap<String,Vec<u8>>,
    sampling:&mut std::collections::BTreeMap<String,[i32;5]>,
)->Result<(),String> {
    if let Some((property,SkinTextureSlot::Unresolved(name)))=slots.iter()
        .find(|(_,slot)|matches!(slot,SkinTextureSlot::Unresolved(_))) {
        return Err(format!("unresolved SkinPlus resource {property}={name}"));
    }
    for (property,slot) in slots {
        pngs.remove(property);sampling.remove(property);
        if let SkinTextureSlot::Bound(t)=slot {
            pngs.insert(property.clone(),t.png.clone());
            sampling.insert(property.clone(),[t.sampling.filter,t.sampling.wrap_u,t.sampling.wrap_v,
                i32::from(t.sampling.srgb),t.sampling.mip_count]);
        }
    }
    Ok(())
}

pub(crate) fn bundle_texture(bundle: &crate::unity::ParsedBundle,name:&str)->Option<SkinTexture> {
    Some(SkinTexture{name:name.into(),png:bundle.texture_png_named(name)?,
        sampling:bundle.texture_sampling_named(name)?})
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn skin_texture_transport_rejects_missing_assets_without_partial_mutation() {
        let material=MeshMaterial{tex_slots:vec![("_Texture2".into(),"missing_liquid".into())],..Default::default()};
        let mut slots=authored_textures(&material,|_|None);
        assert!(matches!(slots.get("_Texture2"),Some(SkinTextureSlot::Unresolved(_))));
        assert!(matches!(slots.get("_Texture3"),Some(SkinTextureSlot::Default(n)) if n=="bump"));
        let mut pngs=std::collections::BTreeMap::from([("_Texture2".into(),vec![99])]);
        let mut samplers=std::collections::BTreeMap::from([("_Texture2".into(),[9;5])]);
        let before=(pngs.clone(),samplers.clone());
        assert!(transport_textures(&slots,&mut pngs,&mut samplers).is_err());
        assert_eq!((pngs.clone(),samplers.clone()),before);
        slots.insert("_Texture2".into(),SkinTextureSlot::Default("black".into()));
        slots.insert("_Texture3".into(),SkinTextureSlot::Bound(SkinTexture{name:"normal".into(),png:vec![7,8],
            sampling:crate::unity::TextureSampling{filter:2,wrap_u:1,wrap_v:0,srgb:false,mip_count:10}}));
        transport_textures(&slots,&mut pngs,&mut samplers).unwrap();
        assert!(!pngs.contains_key("_Texture2"));assert!(!samplers.contains_key("_Texture2"));
        assert_eq!(pngs.get("_Texture3"),Some(&vec![7,8]));assert_eq!(samplers.get("_Texture3"),Some(&[2,1,0,0,10]));
    }

    #[test]
    #[ignore="installed draw bundles and independently captured native bindings"]
    fn native_skinplus_authored_resource_transport() {
        use std::path::Path;
        let root=Path::new("C:/portable/OpenKoikatsu");let game=&crate::test_support::game_path("");
        let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_380982.png")).unwrap();
        let (selected,_)=crate::mods::uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex).unwrap();
        let body_bundle=crate::mods::uncensor_draw_bundle(game,&selected).unwrap();
        let body=body_bundle.material_named(&selected.body_material).unwrap();
        let vanilla=crate::load_characustom_dir(&game.join("abdata/list/characustom"));
        let index=crate::mods::find_guids(&game.join("mods"),None);let mut lists=HashMap::new();
        let entry=crate::resolve_face_entry(&vanilla,&card,&index,&mut lists).unwrap();
        let bytes=crate::accessory_bundle_bytes(game,entry.extra.get("__zip").map(|s|Path::new(s)),&entry).unwrap();
        let face_bundle=crate::unity::parse_bundle_cached(bytes);
        let meshes=face_bundle.meshes_for_maindata(&entry.info("MainData")).0;
        let face=meshes.iter().find(|m|m.name=="cf_O_face").unwrap().mat.as_ref().unwrap();
        let folder=root.join("Saved/Tests/Skin329Resources");std::fs::create_dir_all(&folder).unwrap();
        let wanted:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Tests/Skin325/WantedNativeInputs.json")).unwrap()).unwrap();
        let mut rows=Vec::new();
        for (role,material,bundle,mesh) in [("body",&body,&body_bundle,"o_body_a"),("face",face,&face_bundle,"cf_O_face")] {
            let mut resources=authored_textures(material,|name|bundle_texture(bundle,name));
            replay_overlay_textures(&mut resources,&card,role=="body",|cat,id,key|
                crate::skin_catalog_texture(game,&vanilla,&vanilla,&card,&index,cat,id,key)).unwrap();
            let mut pngs=std::collections::BTreeMap::new();let mut sampling=std::collections::BTreeMap::new();
            transport_textures(&resources,&mut pngs,&mut sampling).unwrap();
            let w=wanted.as_array().unwrap().iter().find(|w|w["mesh"]==mesh).unwrap();
            let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(w["dump"].as_str().unwrap()).unwrap()).unwrap();
            for (property,slot) in resources {
                let witness=native["materials"][0]["properties"].as_array().unwrap().iter().find(|p|p["name"]==property).unwrap();
                match slot {
                    SkinTextureSlot::Bound(texture)=>{
                        let path=folder.join(format!("{role}_{property}.png"));std::fs::write(&path,&texture.png).unwrap();
                        assert_eq!(pngs.get(&property),Some(&texture.png));
                        let image=crate::teximg::load_from_memory(&texture.png).unwrap();
                        rows.push(serde_json::json!({"role":role,"property":property,"state":"bound","name":texture.name,
                            "png":path,"width":image.width(),"height":image.height(),
                            "sampling":[texture.sampling.filter,texture.sampling.wrap_u,texture.sampling.wrap_v,i32::from(texture.sampling.srgb),texture.sampling.mip_count],
                            "native":witness["texture"],"dump":w["dump"]}));
                    },
                    SkinTextureSlot::Default(default)=>rows.push(serde_json::json!({"role":role,"property":property,"state":"default","default":default,"native":witness["texture"],"dump":w["dump"]})),
                    SkinTextureSlot::Unresolved(name)=>rows.push(serde_json::json!({"role":role,"property":property,"state":"unresolved","name":name,"native":witness["texture"],"dump":w["dump"]})),
                }
            }
        }
        rows.sort_by_key(|r|format!("{}{}",r["role"],r["property"]));
        std::fs::write(root.join("Saved/Logs/Skin329AuthoredResources.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
        assert_eq!(rows.len(),30);
        assert!(!rows.iter().any(|r|r["state"]=="unresolved"),"authored non-null resources must resolve independently");
    }

    #[test]
    #[ignore="installed card supplies realistic typed source inputs"]
    fn skinplus_replay_event_order_and_identity_controls() {
        let mut card=crate::card::parse_card(&crate::test_support::game_path("/UserData/chara/female/KK_380982.png")).unwrap();
        card.me_shader.clear(); card.me_float.clear(); card.me_color.clear();
        card.skin_gloss_power=0.2;
        let mut source=MeshMaterial{name:"test_draw".into(),shader:"original".into(),render_queue:2177,..Default::default()};
        source.colors.insert("_ShadowColor".into(),[0.1,0.2,0.3,0.4]);
        source.colors.insert("_overcolor2".into(),[0.3,0.4,0.5,0.6]);
        source.floats.insert("_NormalMap_ST_z".into(),0.125);
        let identity=crate::card::me_mat_key(&source.name);
        let draw=||SkinDraw::Body{skin_tuya_rate:0.5};
        assert!(replay(&source,&card,draw(),&HashMap::new(),&HashMap::new()).is_none());
        card.me_shader.insert((4,0,0,identity.clone()),("xukmi/SkinPlus".into(),None));
        let xml=HashMap::from([("SpecularPower".into(),0.7)]);
        let first=replay(&source,&card,draw(),&xml,&HashMap::new()).unwrap();
        assert_eq!(first.color("_ShadowColor"),source.color("_ShadowColor"));
        assert_eq!(first.float("_SpecularPower"),Some(0.7),"XML after ChaControl");
        assert_eq!(first.render_queue,2177,"preserved queue");
        assert_eq!(first.float("_NormalMap_ST_z"),Some(0.125));
        assert_eq!(first.float("_NormalMap_ST_x"),Some(1.));
        card.me_float.insert((4,0,0,identity.clone(),"specularpower".into()),0.9);
        card.me_float.insert((4,1,0,identity.clone(),"specularpower".into()),0.1);
        card.me_float.insert((4,0,1,identity.clone(),"specularpower".into()),0.1);
        card.me_float.insert((4,0,0,"other".into(),"specularpower".into()),0.1);
        card.me_float.insert((4,0,0,identity.clone(),"normalmap_st_x".into()),5.);
        card.me_float.insert((4,0,0,identity.clone(),"missingproperty".into()),99.);
        let second=replay(&source,&card,draw(),&xml,&HashMap::new()).unwrap();
        assert_eq!(second.float("_SpecularPower"),Some(0.9),"ME after XML; exact identity");
        assert_eq!(second.float("_NormalMap_ST_x"),Some(5.));
        assert_eq!(second.float("_NormalMap_ST_y"),Some(1.),"independent axis preservation");
        assert_eq!(second.float("_NormalMap_ST_z"),Some(0.125));
        assert!(second.float("_missingproperty").is_none(),"HasProperty gate");
        card.me_shader.clear(); card.me_float.clear();
        source.shader="xukmi/SkinPlus".into();
        let unchanged_shader=replay(&source,&card,draw(),&xml,&HashMap::new()).unwrap();
        assert!((unchanged_shader.float("_SpecularPower").unwrap()-0.6).abs()<1e-6,
            "native tuya interpolation; no SetShader means no XML defaults");
        let face=replay(&source,&card,SkinDraw::Face{hoho_aka_rate:0.5},&xml,&HashMap::new()).unwrap();
        assert_eq!(face.color("_overcolor2"),Some([0.3,0.4,0.5,0.1]),"blush changes alpha only");
        card.me_color.insert((4,0,0,identity,"overcolor2".into()),[0.9,0.8,0.7,0.6]);
        let face_me=replay(&source,&card,SkinDraw::Face{hoho_aka_rate:0.5},&xml,&HashMap::new()).unwrap();
        assert_eq!(face_me.color("_overcolor2"),Some([0.9,0.8,0.7,0.6]),"ME follows blush");
        assert_eq!(source.color("_overcolor2"),Some([0.3,0.4,0.5,0.6]),"source remains intact");
    }
    #[test]
    #[ignore="installed card and exact native material source replay"]
    fn native_skinplus_numeric_source_replay() {
        let root=std::path::Path::new("C:/portable/OpenKoikatsu");
        let inputs:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Tests/Skin325/CachedInputs/inputs.json")).unwrap()).unwrap();
        let wanted:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Tests/Skin325/WantedNativeInputs.json")).unwrap()).unwrap();
        let mut rows=Vec::new();
        for input in inputs["rows"].as_array().unwrap() {
            let card=crate::card::parse_card(&&crate::test_support::game_path("/UserData/chara/female").join(input["card"].as_str().unwrap())).unwrap();
            let mut source:MeshMaterial=serde_json::from_value(input["material"].clone()).unwrap();
            let mut source_choice=serde_json::Value::Null;
            if input["role"]=="body" {
                let game=&crate::test_support::game_path("");
                if let Some((selected,_))=crate::mods::uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex) {
                    source=crate::mods::uncensor_draw_material(game,&selected).expect("selected draw source bundle/material");
                    source_choice=serde_json::json!({"guid":selected.guid,"mm_base":selected.mm_base,"material":selected.body_material});
                }
            }
            let draw=if input["role"]=="body" {SkinDraw::Body{skin_tuya_rate:0.}} else {SkinDraw::Face{hoho_aka_rate:0.}};
            let result=replay(&source,&card,draw,&HashMap::new(),&HashMap::new()).expect("card source requests registered native family");
            let witness=wanted.as_array().unwrap().iter().find(|w|w["card"]==input["card"] && w["mesh"]==input["mesh"]).unwrap();
            let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(witness["dump"].as_str().unwrap()).unwrap()).unwrap();
            let material=&native["materials"][witness["slot"].as_u64().unwrap() as usize];
            let mut checks=Vec::new();
            for p in material["properties"].as_array().unwrap() {
                let name=p["name"].as_str().unwrap();
                if p["type"]=="Float" || p["type"]=="Range" {
                    checks.push(serde_json::json!({"property":name,"native":p["value"],"replay":result.float(name)}));
                } else if p["type"]=="Color" || p["type"]=="Vector" {
                    let axes=if p["value"].get("r").is_some(){["r","g","b","a"]}else{["x","y","z","w"]};
                    let value:Vec<_>=axes.iter().map(|a|p["value"][*a].clone()).collect();
                    checks.push(serde_json::json!({"property":name,"native":value,"replay":result.color(name)}));
                } else if p["type"]=="Texture" {
                    for (axis,group,component) in [("x","scale","x"),("y","scale","y"),
                        ("z","offset","x"),("w","offset","y")] {
                        let property=format!("{name}_ST_{axis}");
                        checks.push(serde_json::json!({"property":property,
                            "native":p["texture"][group][component],"replay":result.float(&property)}));
                    }
                }
            }
            let overrides:Vec<_>=card.me_color.iter().filter(|((_,_,_,name,_),_)|*name==crate::card::me_mat_key(&source.name)).collect();
            rows.push(serde_json::json!({"card":input["card"],"mesh":input["mesh"],"source":source,"source_choice":source_choice,"card_colors":overrides,"replay":result,"checks":checks}));
        }
        assert!(!rows.is_empty());
        std::fs::write(root.join("Saved/Logs/Skin326NumericReplay.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
        assert_eq!(rows.len(), 2, "both installed native face/body witnesses");
        for row in &rows {
            for check in row["checks"].as_array().unwrap() {
                let flatten = |value: &serde_json::Value| -> Vec<f64> {
                    match value {
                        serde_json::Value::Array(values) => values.iter().map(|v|v.as_f64().expect("numeric component")).collect(),
                        _ => vec![value.as_f64().expect("present numeric source property")],
                    }
                };
                let native=flatten(&check["native"]);
                let replay=flatten(&check["replay"]);
                assert_eq!(native.len(),replay.len());
                assert!(native.iter().zip(&replay).all(|(a,b)|(a-b).abs()<=1e-6),
                    "{} {}: {}",row["mesh"],check["property"],check);
            }
        }
    }
}
