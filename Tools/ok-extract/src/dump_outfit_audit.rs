//! Opt-in export of actual runtime sessions for independent native dump audits.
#[test]
#[ignore="installed makeup bundle supplies positive source; no game process"]
fn native_face_draw_selected_null_does_not_borrow() {
    let game=&crate::test_support::game_path("");
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let entry=vanilla.by_id.values().find(|e|e.category_no==401 && !e.info("EyeshadowTex").is_empty() && e.info("EyeshadowTex")!="0").unwrap().clone();
    let mut selected=entry.clone();selected.main_tex=entry.info("EyeshadowTex");selected.extra.insert("EyeshadowTex".into(),"0".into());
    let mut mods=crate::list::ChaListCatalog::default();mods.by_id.insert((401,entry.id),selected);
    assert!(crate::load_catalog_display_texture(game,&vanilla,&Default::default(),401,entry.id,"EyeshadowTex").is_some(),"positive source must exist");
    assert!(crate::load_catalog_display_texture(game,&vanilla,&mods,401,entry.id,"EyeshadowTex").is_none(),"selected null must not borrow MainTex or vanilla row");
}
#[test]
#[ignore="installed card provides typed fields; explicit OK_AUDIT_CARD"]
fn native_face_draw_slots_precedence_and_cache() {
    let mut card=crate::card::parse_card(std::path::Path::new(&std::env::var("OK_AUDIT_CARD").unwrap())).unwrap();
    card.me_color.clear();card.me_tex.clear();card.hoho_aka_rate=2.;
    let mut mesh=crate::unity::ExtractedMesh {name:"cf_O_face".into(),
        mat:Some(crate::unity::MeshMaterial {name:"cf_m_face_00".into(),shader:"Koikano/main_skin".into(),
            colors:[("_overcolor2".into(),[0.18,0.42,0.77,0.91])].into(),..Default::default()}),..Default::default()};
    let game=std::path::Path::new("__no_game_config_345__");let catalog=crate::list::ChaListCatalog::default();
    for shader in ["Koikano/main_skin","Shader Forge/main_skin","xukmi/SkinPlus"] {
        mesh.mat.as_mut().unwrap().shader=shader.into();
        crate::apply_native_face_overlays(game,&catalog,&catalog,&card,0,&mut mesh);
        assert_eq!(mesh.mat.as_ref().unwrap().color("_overcolor2"),Some([0.18,0.42,0.77,0.2]));
    }
    let colors=[[0.1,0.2,0.3,0.4],[0.7,0.4,0.2,0.8],[0.6,0.3,0.9,0.5]];
    let png=crate::unity::solid_png([0.2,0.5,0.8,0.75],2,2).unwrap();
    for i in 1..=3 {
        card.me_color.insert((4,0,0,"cf_m_face_00".into(),format!("overcolor{i}")),colors[i-1]);
        card.me_tex.insert((4,0,0,"cf_m_face_00".into(),format!("overtex{i}")),std::sync::Arc::new(png.clone()));
    }
    card.hoho_aka_rate=0.;crate::apply_native_face_overlays(game,&catalog,&catalog,&card,0,&mut mesh);
    let part=crate::session::SessionPart::from_mesh("head",&mesh,"opaque",false);
    for i in 1..=3 {
        assert_eq!(part.shader_textures.get(&format!("_overtex{i}")),Some(&png));
        assert_eq!(part.source_material.as_ref().unwrap().color(&format!("_overcolor{i}")),Some(colors[i-1]));
    }
    assert!(crate::session::SessionPart::from_mesh("body",&mesh,"opaque",false).shader_textures.is_empty());
}
#[test]
#[ignore="requires installed male cards and clothing bundles"]
fn clothes_component_prefab_isolation() {
    let game=&crate::test_support::game_path("");
    let catalog=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    for relative in ["UserData/chara/male/KK_224305.png","DefaultData/chara/male/ill_male_01.png","DefaultData/chara/male/ill_male_02.png"] {
        let card=crate::card::parse_card(&game.join(relative)).unwrap();
        let entry=catalog.get(105,card.clothes_ids[0]).unwrap();
        let bytes=crate::accessory_bundle_bytes(game,None,entry).unwrap();
        let map=crate::unity::clothes_renderer_map(&bytes,&entry.main_data).expect("actual loaded prefab must resolve its component");
        assert!(!map.normal01.is_empty()||!map.alpha01.is_empty());
        assert!(crate::unity::clothes_renderer_map(&bytes,"__absent_prefab_341__").is_none(),"sibling component must not be borrowed");
        eprintln!("PREFAB341 {relative} prefab={} map={map:?}",entry.main_data);
    }
}
#[test]
#[ignore = "installed card supplies complete typed defaults; no game process"]
fn native_draw_settings_preserve_status_and_editor_precedence() {
    let card_path=std::path::PathBuf::from(std::env::var("OK_AUDIT_CARD").unwrap());
    let mut card=crate::card::parse_card(&card_path).unwrap();
    card.skin_gloss_power=0.2;card.nail_gloss_power=0.75;
    card.body_detail_power=0.6;card.cheek_gloss_power=0.4;card.lip_gloss_power=-1.;
    card.face_detail_power=0.;card.me_float.clear();
    let mut mesh=crate::unity::ExtractedMesh {mat:Some(crate::unity::MeshMaterial {
        name:"cf_m_body".into(),floats:[("_specularpower".into(),0.3)].into(),..Default::default()}),..Default::default()};
    for (rate,expected) in [(-1.,0.2),(0.25,0.4),(2.,1.)] {
        card.skin_tuya_rate=rate;crate::apply_native_skin_draw_settings(&mut mesh,&card,6,true);
        let mat=mesh.mat.as_ref().unwrap();assert!((mat.float("_SpecularPower").unwrap()-expected).abs()<1e-6);
        assert_eq!(mat.floats.keys().filter(|k|k.eq_ignore_ascii_case("_SpecularPower")).count(),1);
        assert_eq!(mat.float("_SpecularPowerNail"),Some(0.75));
    }
    card.me_float.insert((4,0,0,"cf_m_body".into(),"specularpower".into()),0.9);
    card.me_float.insert((4,0,0,"other".into(),"specularpowernail".into()),0.1);
    crate::apply_native_skin_draw_settings(&mut mesh,&card,6,true);
    assert_eq!(mesh.mat.as_ref().unwrap().float("_SpecularPower"),Some(0.9));
    assert_eq!(mesh.mat.as_ref().unwrap().float("_SpecularPowerNail"),Some(0.75));
    mesh.mat.as_mut().unwrap().name="cf_m_face_00".into();
    crate::apply_native_skin_draw_settings(&mut mesh,&card,6,false);
    assert_eq!(mesh.mat.as_ref().unwrap().float("_SpecularPower"),Some(0.4));
    assert_eq!(mesh.mat.as_ref().unwrap().float("_SpecularPowerNail"),Some(-1.));
}
#[test]
#[ignore = "requires installed game and explicit audit output; diagnostic export"]
fn export_runtime_outfits() {
    use std::path::PathBuf;
    let game = PathBuf::from(std::env::var("OK_AUDIT_GAME").unwrap());
    let card = PathBuf::from(std::env::var("OK_AUDIT_CARD").unwrap());
    let output = PathBuf::from(std::env::var("OK_AUDIT_OUTPUT").unwrap());
    for outfit in std::env::var("OK_AUDIT_OUTFITS").unwrap().split(',').map(|v|v.parse::<i32>().unwrap()) {
        let mut session = crate::session::open(&game, Some(&card), outfit).unwrap();
        session.apply_anim_time(0.0);
        let folder = output.join(format!("outfit{outfit}"));
        std::fs::create_dir_all(&folder).unwrap();
        let mut parts=Vec::new();
        for (index,p) in session.parts.iter().enumerate() {
            let textures=[("_MainTex",p.png.as_ref()),("_AlphaMask",p.alpha_mask.as_ref()),
                ("_DetailMask",p.detail_mask.as_ref()),("_NormalMap",p.normal_map.as_ref()),
                ("_NormalMapDetail",p.normal_detail.as_ref()),("_NormalMask",p.normal_mask.as_ref()),
                ("_HairGloss",p.hair_gloss.as_ref()),
                ("_overtex1",p.overtex_png.as_ref()),("_overtex2",p.overtex2_png.as_ref())];
            let mut files=serde_json::Map::new();
            for (slot,bytes) in textures {if let Some(bytes)=bytes {
                let name=format!("part{index}{slot}.png");std::fs::write(folder.join(&name),bytes).unwrap();
                files.insert(slot.into(),serde_json::json!(name));
            }}
            for (slot,bytes) in &p.shader_textures {let name=format!("part{index}_shader{slot}.png");
                std::fs::write(folder.join(&name),bytes).unwrap();files.insert(slot.clone(),serde_json::json!(name));}
            if let Some(draw)=&p.eye_draw {for (i,bytes) in draw.textures.iter().enumerate() {if let Some(bytes)=bytes {
                let name=format!("part{index}_eye{}.png",i+1);std::fs::write(folder.join(&name),bytes).unwrap();
                files.insert(format!("_overtex{}",i+1),serde_json::json!(name));
            }}}
            if let Some(draw)=&p.item_draw {if let Some(bytes)=&draw.color_mask {
                let name=format!("part{index}_ColorMask.png");std::fs::write(folder.join(&name),bytes).unwrap();
                files.insert("_ColorMask".into(),serde_json::json!(name));
            }}
            parts.push(serde_json::json!({"index":index,"role":p.role,"name":p.name,
                "shader_kind":p.shader_kind,"runtime_textures":p.runtime_textures,
                "visible":p.render_visible,"material":p.source_material,"positions":p.positions,
                "uv":p.uvs,"uv2":p.uv2,"uv3":p.uv3,"uv4":p.uv4,"indices":p.indices,
                "main_tex_st":p.main_tex_st,"alpha_mask_st":p.alpha_mask_st,
                "detail_mask_st":p.detail_mask_st,"normal_map_st":p.normal_map_st,
                "effective_values":{"_ShadowColor":p.shadow_color,"_LineColor":p.line_color,
                    "_overcolor1":p.overcolor,"_overcolor2":p.overcolor2,"_SpecularColor":p.specular_color,
                    "_SpecularPower":p.specular_power,"_SpecularPowerNail":p.specular_power_nail,
                    "_notusetexspecular":p.no_tex_specular,"_nipsize":p.nip_size,"_nip_specular":p.nip_specular,
                    "_DetailNormalMapScale":p.normal_detail_scale},
                "item_draw":p.item_draw.as_ref().map(|d|serde_json::json!({"colors":d.colors,
                    "color_count":d.color_count,"has_mask":d.color_mask.is_some(),"mask_st":d.color_mask_st})),
                "iris":p.iris_runtime.as_ref().map(|d|serde_json::json!({"settings":d.settings,"raw_uv":d.raw_uvs})),
                "eye_draw":p.eye_draw.as_ref().map(|d|serde_json::json!({"kind":d.kind,"colors":d.colors,
                    "color":d.color,"enabled":d.enabled,"offsets":d.offsets,"rotations":d.rotations,
                    "raw_uv":d.raw_uvs,"texcoord_st":d.texcoord_st,"main_texcoord_st":d.main_texcoord_st,
                    "sample_uv":d.sample_uvs})),
                "textures":files}));
        }
        std::fs::write(folder.join("runtime.json"),serde_json::to_vec(&serde_json::json!({
            "card":card,"outfit":outfit,"parts":parts})).unwrap()).unwrap();
        eprintln!("Runtime audit export outfit={outfit} parts={}",session.parts.len());
    }
}
