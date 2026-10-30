#[test]
fn rigid_hair_socket_is_preserved_in_cpu_and_gpu_sources() {
    use super::*;
    struct RestoreEnv(Vec<(&'static str,Option<std::ffi::OsString>)>);
    impl Drop for RestoreEnv { fn drop(&mut self) { for (key,value) in &self.0 {
        if let Some(value)=value {std::env::set_var(key,value);} else {std::env::remove_var(key);}
    } } }
    let keys=["OK_GPU_SKIN","OK_SKELMESH","OK_ACCSCALE"];
    let _restore=RestoreEnv(keys.iter().map(|key|(*key,std::env::var_os(key))).collect());
    std::env::set_var("OK_SKELMESH","0");std::env::remove_var("OK_ACCSCALE");
    let parent=crate::pose::trs([0.2,1.5,-0.1],[0.,0.,0.,1.],[0.8,1.2,0.9]);
    let body=crate::pose::Skeleton {names:vec!["cf_J_FaceUp_ty".into()],worlds:vec![parent],rest:vec![parent],
        smr_count:1,tfs:Default::default(),bone_ids:vec![1],source_mesh:0};
    for mode in ["0","1"] {
        std::env::set_var("OK_GPU_SKIN",mode);
        let local=[0.03,0.04,0.05];
        let mut mesh=ExtractedMesh {name:"arbitrary_rigid_renderer".into(),renderer_skinned:Some(false),
            positions:vec![local],normals:vec![[0.,1.,0.]],..Default::default()};
        rig_mesh(&mut mesh,"hair-side",Some(&body),None,None,None,None,None, None);
        assert_eq!(mesh.rigid_follow_bone.as_deref(),Some("cf_J_FaceUp_ty"));
        assert!(mesh.parent_bone.is_none(),"world bake must not attach twice");
        assert!(mesh.weights.is_empty(),"do not invent skin weights for a MeshRenderer");
        assert_eq!(mesh.skel_kind,0);
        let expected=crate::pose::mat_vec(&parent,local);
        for axis in 0..3 {assert!((mesh.positions[0][axis]-expected[axis]).abs()<1e-6);}
    }
}

#[test]
#[ignore="installed card texture property diagnostics"]
fn stage332_saved_texture_properties() {
    std::env::set_var("OK_ME_TEX_DIAG","1");
    crate::card::parse_card(&crate::test_support::game_path("/UserData/chara/female/kks_守岸人_abmx修复.png")).unwrap();
}
#[test]
fn resolved_null_normal_rejects_stale_texture_without_losing_authored_binding() {
    use super::*;
    let mut mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial::default()),
        normal_map_png:Some(vec![1,2,3]),..Default::default()};
    mesh.runtime_textures.insert("_NormalMap".into(),[1,0,0,0,1]);
    clear_unbound_normal_map(&mut mesh);
    assert!(mesh.normal_map_png.is_none());
    assert!(!mesh.runtime_textures.contains_key("_NormalMap"));
    mesh.mat.as_mut().unwrap().tex_slots.push(("_NormalMap".into(),"authored".into()));
    mesh.normal_map_png=Some(vec![4,5,6]);
    mesh.runtime_textures.insert("_NormalMap".into(),[0,1,1,0,2]);
    clear_unbound_normal_map(&mut mesh);
    assert_eq!(mesh.normal_map_png,Some(vec![4,5,6]));
    assert_eq!(mesh.runtime_textures.get("_NormalMap"),Some(&[0,1,1,0,2]));
    // Unresolved material provenance cannot certify a null binding.
    mesh.mat=None;
    clear_unbound_normal_map(&mut mesh);
    assert_eq!(mesh.normal_map_png,Some(vec![4,5,6]));
}

#[test]
#[ignore="installed source/material-editor diagnostics for main_alpha clothes"]
fn stage333_alpha_source_diagnostics() {
    use super::*;
    let game=&crate::test_support::game_path("");
    for (key,value) in [("OK_GPU_SKIN","1"),("OK_SKELMESH","1"),("OK_NOPOSE","1"),("OK_UE_ANIM","1"),("OK_POSE","f_aruki_00_00"),("OK_ME_TEX_DIAG","1")] {std::env::set_var(key,value);}
    let card_path=game.join("UserData/chara/female/一拳超人战栗的龙卷.png");
    let card=crate::card::parse_card(&card_path).unwrap();
    let out=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/ForgeAlpha333/Source");
    std::fs::create_dir_all(out).unwrap();
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let guids=crate::mods::find_guids(&game.join("mods"),None);
    let choices=select_card_top_subparts(&card,0,2,&vanilla,&mods,&guids);
    for choice in choices.iter().flatten() {
        let entry=&choice.1;
        eprintln!("stage333 entry={entry:?}");
        for key in ["MainTex","ColorMaskTex"] {
            if let Some(png)=clothes_list_png(game,entry,key,&[],&[]) {std::fs::write(out.join(format!("source-{key}.png")),png).unwrap();}
        }
    }
    let session=crate::session::open(game,Some(&card_path),0).unwrap();
    let mut rows=Vec::new();
    for (i,p) in session.parts.iter().enumerate().filter(|(_,p)|p.role.starts_with("clothes")) {
        let Some(mat)=&p.source_material else{continue;};
        for (suffix,png) in [("main",&p.png),("normal",&p.normal_map),("detail",&p.detail_mask)] {
            if let Some(png)=png {std::fs::write(out.join(format!("part{i}.{suffix}.png")),png).unwrap();}
        }
        for (name,png) in &p.shader_textures {std::fs::write(out.join(format!("part{i}.shader{name}.png")),png).unwrap();}
        if p.name=="o_inner_a" {
            assert_eq!(p.shader_kind,11);
            let actual=crate::teximg::load_from_memory(p.png.as_ref().unwrap()).unwrap().into_rgba8();
            let native=image::open("C:/portable/OpenKoikatsu/Saved/Tests/Tatsumaki333Native/Card0.json.renderer30.mat0._MainTex.png").unwrap().to_rgba8();
            assert_eq!(actual.dimensions(),native.dimensions());
            let max=actual.as_raw().iter().zip(native.as_raw()).map(|(a,b)|a.abs_diff(*b)).max().unwrap();
            assert!(max<=1,"KCOX native albedo max byte error {max}");
            assert!(p.shader_textures.contains_key("_NormalMap"));
            assert!(p.shader_textures.contains_key("_Texture3"));
            eprintln!("stage333 native KCOX MainTex max_byte_error={max}");
        }
        rows.push(serde_json::json!({"part":i,"name":p.name,"role":p.role,"material":mat.name,"shader":mat.shader,"shader_kind":p.shader_kind,"floats":mat.floats,"colors":mat.colors,"texture_slots":mat.tex_slots,"runtime_textures":p.runtime_textures}));
    }
    std::fs::write(out.join("Materials.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    std::fs::write(out.join("TopColors.json"),serde_json::to_vec_pretty(&card.clothes_colors.first()).unwrap()).unwrap();
}

#[test]
#[ignore="installed pattern resolve provenance"]
fn stage334_pattern_resolve_provenance() {
    use super::*;
    let game=&crate::test_support::game_path("");
    let mut card=crate::card::parse_card(&game.join("UserData/chara/female/弹丸论破 - 七海千秋3.png")).unwrap();
    eprintln!("stage334 patterns={:?}",card.clothes_patterns);
    for r in card.resolves.iter().filter(|r|r.category_no==430){eprintln!("stage334 resolve={r:?}");}
    crate::mods::apply_strip_all_migrations(game,&mut card);
    assert!(!card.resolves.iter().any(|r|r.category_no==430));
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));let guids=crate::mods::find_guids(&game.join("mods"),None);
    let out=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Resolve334Source");std::fs::create_dir_all(out).unwrap();
    let entry=vanilla.get(105,card.clothes_ids[0]).unwrap();
    for label in ["original","shift","alpha","combined"] {
        let mut p=build_clothes_patterns(game,&vanilla,&mods,&card,&guids,0,0,&[],&[],0);
        assert!(p[0].gpu.is_some());assert!(!p[0].gpu.as_ref().unwrap().sampling.srgb);
        if label=="shift"||label=="combined"{p[0].tiling=[0.08,0.32];p[0].offset=[0.37,0.68];p[0].rotate=0.29;}
        if label=="alpha"||label=="combined"{p[0].color[3]=0.4;}
        for (key,mask) in [("MainTex","ColorMaskTex"),("MainTex02","ColorMask02Tex")] {
            let png=flip_runtime_blit_v(clothes_gpu_bake(game,entry,key,mask,card.clothes_colors[0],&p).unwrap());
            std::fs::write(out.join(format!("{key}.{label}.png")),png).unwrap();
        }
    }
}

#[test]fn pattern_resolve_requires_coordinate_part_and_channel() {
    let r=crate::card::ResolveInfo{mod_id:"mod".into(),slot:313,local_slot:100000001,property:"outfit2.ChaFileClothes.ClothesTopPattern3".into(),category_no:430,name:String::new()};
    assert!(pattern_resolve_matches(&r,2,0,3,313));assert!(pattern_resolve_matches(&r,2,0,3,100000001));
    for (outfit,part,channel,id) in [(0,0,3,313),(2,1,3,313),(2,0,0,313),(2,0,3,32)] {assert!(!pattern_resolve_matches(&r,outfit,part,channel,id));}
}

#[test]
#[ignore="installed source diagnostics for headscarf and pattern gap"]
fn stage332_source_diagnostics() {
    use super::*;
    let game=&crate::test_support::game_path("");
    for (key,value) in [("OK_GPU_SKIN","1"),("OK_SKELMESH","1"),("OK_NOPOSE","1"),("OK_UE_ANIM","1"),("OK_POSE","f_aruki_00_00")] {std::env::set_var(key,value);}
    let out=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Shore332Source");std::fs::create_dir_all(out).unwrap();
    let session=crate::session::open(game,Some(&game.join("UserData/chara/female/kks_守岸人_abmx修复.png")),0).unwrap();
    let mut rows=Vec::new();
    for (i,p) in session.parts.iter().enumerate() {
        let Some(mat)=&p.source_material else{continue;};
        if !mat.name.starts_with("Welt") && p.name!="o_acs_head_headband_00" {continue;}
        if let Some(png)=&p.png {std::fs::write(out.join(format!("part{i}.png")),png).unwrap();}
        rows.push(serde_json::json!({"part":i,"name":p.name,"role":p.role,"material":mat.name,"shader":mat.shader,"floats":mat.floats,"colors":mat.colors,"shader_kind":p.shader_kind,"blend":p.blend,"texture_slots":mat.tex_slots}));
    }
    std::fs::write(out.join("Materials.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    let card=crate::card::parse_card(&game.join("UserData/chara/female/弹丸论破 - 七海千秋3.png")).unwrap();
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::sqlcache::mods_catalog(&game.join("mods")).unwrap();let guids=crate::mods::find_guids(&game.join("mods"),None);
    let patterns=build_clothes_patterns(game,&vanilla,&mods,&card,&guids,0,0,&[],&[],0);
    let mut report=Vec::new();
    for (i,p) in patterns.iter().enumerate() {
        let id=card.clothes_patterns[0][i].pattern;
        let mut lists=std::collections::HashMap::new();
        let entry=if let Some(r)=card.resolves.iter().find(|r|r.category_no==430&&(r.slot==id||r.local_slot==id)) {lookup_entry(&vanilla,&mut lists,guid_path(&guids,&r.mod_id).map(|p|p.as_path()),&r.mod_id,430,r.slot,r.local_slot)}else{vanilla.get(430,id).or_else(||mods.get(430,id)).cloned()};
        report.push(serde_json::json!({"channel":i,"id":id,"png":p.png.is_some(),"gpu":p.gpu.is_some(),"entry":entry.map(|e|serde_json::json!({"name":e.name,"extra":e.extra}))}));
    }
    std::fs::write(out.join("PatternGap.json"),serde_json::to_vec_pretty(&report).unwrap()).unwrap();
}

#[test]
#[ignore="installed native ARGB32 clothes regression"]
fn native_argb_clothes_source() {
    use super::*;
    let game=&crate::test_support::game_path("");
    let zip=game.join("mods/Sideloader Modpack/moderchan/[moderchan]Nailstep Gloves v1.0.zipmod");
    let catalog=crate::list::catalog_from_zipmod(&zip);
    let entry=catalog.get(109,1883).unwrap();
    let raw=crate::mods::zipmod_bundle_named(&zip,"chara/_kero/co_NailGloves_NT_01.unity3d").unwrap();
    let parsed=crate::unity::parse_bundle_ref(&raw);
    let output=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Clothes331ArgbSource");std::fs::create_dir_all(output).unwrap();
    for name in ["cf_body_00_t","cf_body_00_mc","cf_body_00_md","cf_body_00_t_low","cf_body_00_mc_low"] {
        let gpu=parsed.texture_gpu_named(name).unwrap();
        assert_eq!(gpu.format,29);assert!(gpu.mips>=1);
        let png=parsed.texture_png_named(name).unwrap();
        let image=crate::teximg::load_from_memory(&png).unwrap().into_rgba8();
        // PNG decoder exports source rows, GPU input keeps original texels.
        let decoded=image.into_raw();
        assert_eq!(gpu.data[..decoded.len()],decoded);
    }
    let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_381623.png")).unwrap();
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    let patterns=build_clothes_patterns(game,&vanilla,&catalog,&card,&std::collections::HashMap::new(),4,0,&[],&[],0);
    let png=clothes_gpu_bake(game,entry,"MainTex","ColorMaskTex",card.clothes_colors[4],&patterns).unwrap();
    std::fs::write(output.join("MainTex.png"),flip_runtime_blit_v(png)).unwrap();
}

#[test]
#[ignore="installed hardware GPU partial resource controls"]
fn native_clothes_gpu_rejects_partial_inputs() {
    let b=crate::gamefs::read(crate::test_support::game_path("/abdata/chara/mm_base.unity3d")).unwrap();
    let shader=crate::unity::parse_bundle_ref(&b).shader_pixel_dxbc_named("Koikano/create_topN").unwrap();
    let textures=std::array::from_fn(|_|clothes_gpu::Texture::white());
    let mut cb=[[1.0;4];16];cb[2]=[1.0,1.0,0.0,0.0];cb[7]=cb[2];
    let positive=clothes_gpu::bake(&shader,&textures,&cb,2).unwrap();
    assert_eq!(crate::teximg::load_from_memory(&positive).unwrap().into_rgba8().get_pixel(0,0).0,[255;4]);
    let mut bad=textures.clone();bad[0].data.clear();assert!(clothes_gpu::bake(&shader,&bad,&cb,2).unwrap_err().contains("short mip payload"));
    let mut bad=textures.clone();bad[0].mips=17;assert!(clothes_gpu::bake(&shader,&bad,&cb,2).unwrap_err().contains("invalid texture input"));
    let mut bad=textures.clone();bad[4].width=0;assert!(clothes_gpu::bake(&shader,&bad,&cb,2).unwrap_err().contains("invalid bake arguments"));
    assert!(clothes_gpu::bake(b"broken DXBC",&textures,&cb,2).is_err());
}

#[test]
#[ignore="installed native clothes shader source"]
fn native_clothes_shader_source() {
    let game=&crate::test_support::game_path("");
    let bytes=crate::gamefs::read(game.join("abdata/chara/mm_base.unity3d")).unwrap();
    let parsed=crate::unity::parse_bundle_ref(&bytes);
    assert!(parsed.shader_pixel_dxbc_named("Koikano/create_topN").is_some());
}

#[test]
#[ignore="native male clothing source diagnostics"]
fn male_clothes_source_controls() {
    use super::*;
    let game=&crate::test_support::game_path("");
    let catalog=load_characustom_dir(&game.join("abdata/list/characustom"));
    let folder=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Clothes331Source");std::fs::create_dir_all(folder).unwrap();
    let mut rows=Vec::new();
    for (i,relative) in ["UserData/chara/male/KK_224305.png","DefaultData/chara/male/ill_male_01.png","DefaultData/chara/male/ill_male_02.png"].iter().enumerate() {
        assert_eq!(clothes_gpu::quality(game),Some(2),"installed profile must resolve before GPU source control");
        let card=crate::card::parse_card(&game.join(relative)).unwrap();
        let entry=catalog.get(105,card.clothes_ids[0]).unwrap();
        let patterns=build_clothes_patterns(game,&catalog,&catalog,&card,&std::collections::HashMap::new(),0,0,&[],&[],0);
        if i==1 {
            for label in ["shift","alpha","combined"] {
                let mut control=build_clothes_patterns(game,&catalog,&catalog,&card,&std::collections::HashMap::new(),0,0,&[],&[],0);
                if label!="alpha" {control[0].tiling=[0.08,0.32];control[0].offset=[0.37,0.68];control[0].rotate=0.29;}
                if label!="shift" {control[0].color[3]=0.4;}
                let (b,_) = bake_clothes_create_textures(game,entry,card.clothes_colors[0],&control,&[],&[]);
                std::fs::write(folder.join(format!("card1_control-{label}.png")),flip_runtime_blit_v(b[0].clone().unwrap())).unwrap();
            }
        }
        if i==1 {
            for label in ["point0","point1","bilinear0","disable","enable"] {
                let mut p=build_clothes_patterns(game,&catalog,&catalog,&card,&std::collections::HashMap::new(),0,0,&[],&[],0);
                p[0].tiling=[0.08,0.32];p[0].offset=[0.37,0.68];p[0].rotate=0.29;
                let tex=p[0].gpu.as_mut().unwrap();tex.sampling.filter=if label.starts_with("point"){0}else{1};
                tex.aniso=if label=="point0"||label=="bilinear0"{0}else{1};
                let quality=if label=="disable"{0}else if label=="enable"{1}else{2};
                let png=clothes_gpu_bake_with_quality(game,entry,"MainTex","ColorMaskTex",card.clothes_colors[0],&p,quality).unwrap();
                std::fs::write(folder.join(format!("card1_control-{label}.png")),flip_runtime_blit_v(png)).unwrap();
            }
        }
        let (baked,used_mask)=bake_clothes_create_textures(game,entry,card.clothes_colors[0],&patterns,&[],&[]);
        for (slot,png) in baked.iter().enumerate(){if let Some(png)=png {std::fs::write(folder.join(format!("card{i}_slot{slot}.png")),png).unwrap();}}
        let bytes=accessory_bundle_bytes(game,None,entry).unwrap();let parsed=crate::unity::parse_bundle_cached(bytes.clone());
        let mut meshes=parsed.meshes_for_maindata(&entry.main_data).0;let map=crate::unity::clothes_renderer_map(&bytes,&entry.main_data);
        let before:Vec<_>=meshes.iter().map(|m|serde_json::json!({"mesh":m.name,"renderer_ids":m.renderer_ids,"texture_bytes":m.texture_png.as_ref().map(|p|p.len()),"slot":map.as_ref().and_then(|mp|clothes_create_slot(&m.renderer_ids,mp))})).collect();
        apply_clothes_create_textures(&mut meshes,map.as_ref(),&baked);
        for m in &meshes {if let Some(png)=&m.texture_png {std::fs::write(folder.join(format!("card{i}_{}.png",m.name)),png).unwrap();}}
        rows.push(serde_json::json!({"card":relative,"id":entry.id,"main":entry.main_data,"ab":entry.main_ab,"MainTex":entry.info("MainTex"),"ColorMaskTex":entry.info("ColorMaskTex"),"used_mask":used_mask,"pattern_loaded":patterns.iter().map(|p|p.png.is_some()).collect::<Vec<_>>(),"meshes":before}));
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Clothes331Sources.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="installed male cards and native renderer captures"]
fn installed_male_card_source_controls() {
    let game=&crate::test_support::game_path("");
    let mut rows=Vec::new();
    let base=crate::unity::parse_bundle_cached(crate::gamefs::read(game.join("abdata/chara/oo_base.unity3d")).unwrap());
    let (meshes,_)=base.meshes_for_maindata("p_cm_body_00");
    let mesh=meshes.iter().find(|m|m.name=="o_body_a").unwrap();
    assert!(!mesh.positions.is_empty());
    let skeleton=mesh.smr_skel.as_ref().unwrap();
    assert_eq!(skeleton.names,mesh.bone_names,"body weights use this renderer bone order");
    assert_eq!(skeleton.source_mesh,mesh.path_id);
    let skin=crate::unity::parse_bundle_cached(crate::gamefs::read(game.join("abdata/chara/mm_base.unity3d")).unwrap());
    let material=skin.material_named("cm_m_body").unwrap();
    assert_eq!(material.slot("_DetailMask"),Some("cm_body_00_md"));
    for (index,relative) in ["UserData/chara/male/KK_224305.png","DefaultData/chara/male/ill_male_01.png","DefaultData/chara/male/ill_male_02.png"].iter().enumerate() {
        let path=game.join(relative);let card=crate::card::parse_card(&path).unwrap();assert_eq!(card.sex,0);
        let replacement=crate::mods::uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex);
        let (body,prefab,guid)=if let Some((selected,bytes))=replacement {
            assert_eq!(selected.sex,0);assert_eq!(selected.body_material,"cm_m_body");
            let parsed=crate::unity::parse_bundle_cached(bytes);
            let meshes=parsed.meshes_for_maindata(&selected.asset).0;
            (meshes.iter().find(|m|m.name=="o_body_a").unwrap().clone(),selected.asset,selected.guid)
        }else{(mesh.clone(),"p_cm_body_00".into(),"Default.Body.Male".into())};
        let native_path=std::path::Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Male330Native").join(format!("Card{index}.json.renderers.json"));
        let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&native_path).unwrap()).unwrap();
        let native_body=native.as_array().unwrap().iter().find(|r|r["path"].as_str().is_some_and(|p|p.ends_with("/o_body_a"))).unwrap();
        assert_eq!(body.positions.len(),native_body["vertexCount"].as_u64().unwrap() as usize);
        let names=native_body["bones"].as_array().unwrap().iter()
            .map(|n|n.as_str().unwrap().rsplit('/').next().unwrap().to_string()).collect::<Vec<_>>();
        assert_eq!(body.bone_names,names,"native male renderer order");
        rows.push(serde_json::json!({"card":path,"sex":card.sex,"head_id":card.head_id,"body_guid":guid,"prefab":prefab,"source_vertices":body.positions.len(),"bone_names":body.bone_names,"uv0":body.uvs,"uv1":body.uv2,"uv2":body.uv3,"uv3":body.uv4,"colors32":body.vertex_colors,"native_body":native_body,"native_dump":native_path,"draw_material":material.name,"detail_texture":material.slot("_DetailMask"),"top_patterns":card.clothes_patterns.first().map(|p|p.iter().map(|x|serde_json::json!({"id":x.pattern,"color":x.pattern_color,"tiling":x.tiling})).collect::<Vec<_>>())}));
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Male330SourceControls.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore = "actual selected head controllers across the five installed cards"]
fn selected_head_neutral_controllers_use_source_indices() {
    use super::*;
    use std::io::Write;
    let game=&crate::test_support::game_path("");
    let folder=Path::new("../../Saved/Tests/HeadFbs280");
    std::fs::create_dir_all(folder).unwrap();
    let mut audit=std::fs::File::create(folder.join("selected-patterns.tsv")).unwrap();
    writeln!(audit,"card\tprefab\tcontroller\ttarget\tmesh\tpattern\tclose\topen").unwrap();
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    let index=crate::mods::find_guids(&game.join("mods"),None);
    let mut zip_lists=std::collections::HashMap::new();
    for name in ["[Card] March_7th_keiyuu.png","Alice_keiyuu.png","Hayase_yuuka_keiyuu_V1_02 For KKS.png","KK_091207.png","KK_384283.png"] {
        let card=crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        assert_eq!(card.eyebrow_open_max,1.0,"{name}");
        let entry=resolve_face_entry(&vanilla,&card,&index,&mut zip_lists).unwrap();
        let bytes=accessory_bundle_bytes(game,entry.extra.get("__zip").map(|s|Path::new(s)),&entry).unwrap();
        std::fs::write(folder.join(format!("{}_selected_head.unity3d",sanitize(name))),&bytes).unwrap();
        let parsed=crate::unity::parse_bundle_cached(bytes);
        let main=entry.info("MainData");
        let meshes=parsed.meshes_for_maindata(&main).0;
        for controller in ["EyebrowCtrl","EyesCtrl","MouthCtrl"] {
            let targets=parsed.face_control_patterns(&main,controller);
            assert!(!targets.is_empty(),"{name}/{controller}");
            for (mesh_name,(target,patterns)) in &targets {
                let mesh=meshes.iter().find(|m|m.name.eq_ignore_ascii_case(mesh_name)).unwrap();
                let open=if controller=="EyesCtrl" {eye_controller_open_max(card.eyes_open_max)}
                    else if controller=="EyebrowCtrl" {card.eyebrow_open_max.clamp(0.,1.)} else {0.};
                let weights=controller_neutral_weights(mesh.blend_shapes.len(),patterns,open);
                assert_eq!(weights.iter().flatten().sum::<f32>(),100.,"{name}/{controller}/{mesh_name}");
                for (pattern,&(close,opened)) in patterns.iter().enumerate() {
                    for channel in [close,opened] {assert!(channel == -1 || (channel>=0 && (channel as usize)<mesh.blend_shapes.len()));}
                    writeln!(audit,"{name}\t{main}\t{controller}\t{target}\t{mesh_name}\t{pattern}\t{close}\t{opened}").unwrap();
                }
            }
            eprintln!("HEAD_FBS {name} {controller} targets={} source-index baseline passed",targets.len());
        }
    }
}

#[test]
#[ignore = "manual source material audit against installed cards"]
fn mouth_material_source_audit() {
    use std::io::Write;
    let game = &crate::test_support::game_path("");
    let bytes = crate::gamefs::read(game.join("abdata/chara/bo_head_00.unity3d")).unwrap();
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let (meshes, _) = parsed.meshes_for_maindata("p_cf_head_00");
    let folder = std::path::Path::new("../../Saved/Tests/MouthMaterial278");
    std::fs::create_dir_all(folder).unwrap();
    let mut audit = std::fs::File::create(folder.join("source-materials.tsv")).unwrap();
    for mesh in meshes.iter().filter(|m| ["o_tang", "cf_O_tooth", "cf_O_canine"].contains(&m.name.as_str())) {
        let mat = mesh.mat.as_ref().unwrap();
        writeln!(audit,"SOURCE\t{}\t{}\t{}\t{:?}",mesh.name,mat.name,mat.shader,mat.colors).unwrap();
        if let Some(png) = &mesh.texture_png { std::fs::write(folder.join(format!("{}_raw.png",mesh.name)),png).unwrap(); }
        if let Some(png) = &mesh.color_mask_png { std::fs::write(folder.join(format!("{}_mask.png",mesh.name)),png).unwrap(); }
        std::fs::write(folder.join(format!("{}_material.json",mesh.name)),serde_json::to_vec_pretty(mat).unwrap()).unwrap();
    }
    for name in ["[Card] March_7th_keiyuu.png", "Alice_keiyuu.png", "Hayase_yuuka_keiyuu_V1_02 For KKS.png", "KK_091207.png", "KK_384283.png"] {
        let card = crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        for (key,value) in card.me_color.iter().filter(|(key,_)| key.0==4 && (key.3.contains("tang") || key.3.contains("tooth"))) {
            writeln!(audit,"ME_COLOR\t{name}\t{key:?}\t{value:?}").unwrap();
        }
        for (key,value) in card.me_shader.iter().filter(|(key,_)| key.0==4 && (key.3.contains("tang") || key.3.contains("tooth"))) {
            writeln!(audit,"ME_SHADER\t{name}\t{key:?}\t{value:?}").unwrap();
        }
    }
}

use super::*;

#[test]
#[ignore = "manual real-card material override and source asset control"]
fn head_item_actual_cards_and_mod_material_identity() {
    let game = &crate::test_support::game_path("");
    let data = crate::gamefs::read(game.join("abdata/chara/bo_head_00.unity3d")).unwrap();
    let bundle = crate::unity::parse_bundle_cached(data);
    let source = bundle.meshes_for_maindata("p_cf_head_00").0.into_iter().find(|m| m.name=="o_tang").unwrap();
    let folder = Path::new("../../Saved/Tests/MouthMaterial278");
    let mut audit = std::fs::File::create(folder.join("resolved-materials.tsv")).unwrap();
    let vanilla = load_characustom_dir(&game.join("abdata/list/characustom"));
    let index = crate::mods::find_guids(&game.join("mods"), None);
    let mut zip_lists = std::collections::HashMap::new();
    use std::io::Write;
    for name in ["[Card] March_7th_keiyuu.png", "Alice_keiyuu.png", "Hayase_yuuka_keiyuu_V1_02 For KKS.png", "KK_091207.png", "KK_384283.png"] {
        let mut card = crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        let entry = resolve_face_entry(&vanilla, &card, &index, &mut zip_lists).unwrap();
        let source = if let Some(bytes) = accessory_bundle_bytes(game, entry.extra.get("__zip").map(|s|Path::new(s)), &entry) {
            std::fs::write(folder.join(format!("{}_selected_head.unity3d",sanitize(name))),&bytes).unwrap();
            let parsed = crate::unity::parse_bundle_cached(bytes);
            let main = entry.info("MainData");
            parsed.meshes_for_maindata(&main).0.into_iter()
                .find(|m| m.name=="o_tang").unwrap()
        } else { source.clone() };
        eprintln!("HEAD_ITEM_SOURCE card={name} ab={} zip={:?} shader={:?}",entry.main_ab,entry.extra.get("__zip"),source.mat.as_ref().unwrap().shader);
        let mut mesh = source.clone();
        mesh.name = "arbitrary_mod_mesh_name".into();
        let expected = me_material_color(Some(&card.me_color),0,"head",None,&mesh,"color")
            .unwrap_or_else(|| source.mat.as_ref().unwrap().color("_Color").unwrap());
        apply_me_shader(game,Some(&card.me_shader),0,"head",None,&mut mesh);
        apply_me_floats(&card.me_float,0,"head",None,&mut mesh);
        apply_head_draw_material_overrides(&card, &mut mesh);
        let part = crate::session::SessionPart::from_mesh("head",&mesh,"opaque",true);
        let draw = part.item_draw.unwrap();
        assert_eq!(draw.color_count,if source.mat.as_ref().unwrap().shader=="Shader Forge/main_item" {3}else{4});
        assert_eq!(draw.colors[0], expected, "{name}");
        assert_eq!(mesh.texture_png, source.texture_png, "MainTex must remain unbaked {name}");
        let png = crate::teximg::load_from_memory(draw.color_mask.as_ref().unwrap()).unwrap().into_rgba8();
        assert!(png.pixels().all(|p| p.0 == [255,0,0,255]));
        writeln!(audit,"CARD\t{name}\t{:?}\t{:?}",draw.colors[0],part.shadow_color).unwrap();
        // Synthetic mod edits verify material identity, independent ST,
        // all colours, texture replacement and coordinate-zero head scope.
        let key=(4,0,0,"cf_m_tang".into(),"maintex".into());
        card.me_tex.insert(key, std::sync::Arc::new(vec![9,8,7]));
        card.me_color.insert((4,0,0,"cf_m_tang".into(),"color4".into()),[0.1,0.2,0.3,0.4]);
        card.me_float.insert((4,0,0,"cf_m_tang".into(),"ColorMask_ST_x".into()),0.4);
        let mut custom=source.clone(); custom.name="different_mod_name".into();
        apply_me_shader(game,Some(&card.me_shader),6,"head",None,&mut custom);
        apply_me_floats(&card.me_float, 6,"head",None,&mut custom);
        apply_head_draw_material_overrides(&card,&mut custom);
        let draw=crate::session::SessionPart::from_mesh("head",&custom,"opaque",true).item_draw.unwrap();
        assert_eq!(custom.texture_png,Some(vec![9,8,7]));
        assert_eq!(draw.colors[3],[0.1,0.2,0.3,0.4]);
        assert_eq!(draw.color_mask_st[0],0.4);
    }
}

#[test]
fn clothes_state_type_cycles_like_cha_control() {
    let types = [Some(0), Some(1), Some(2), None, None, None, None, None, None];
    let states = resolve_clothes_states([2, 1, 2, 2, 0, 0, 0, 0, 0], types, false, false);
    assert_eq!(states[..4], [3, 3, 2, 2]);
}

#[test]
fn linked_clothes_and_shoes_follow_set_order() {
    let types = [Some(2); 9];
    // UpdateClothesStateAll visits top then bot. The bot's later 0
    // restores a one-piece top previously removed by top=3.
    let states = resolve_clothes_states([3, 0, 3, 0, 0, 0, 0, 3, 0], types, true, true);
    assert_eq!(states[0..4], [0, 0, 0, 0]);
    assert_eq!(states[7..9], [0, 0]);
    let removed = resolve_clothes_states([3, 3, 3, 3, 0, 0, 0, 3, 3], types, true, true);
    assert_eq!(removed[0..4], [3, 3, 3, 3]);
    assert_eq!(removed[7..9], [3, 3]);
}

#[test]
fn reference_nodes_adjust_adjacent_state_types() {
    let mut types = [None; 9];
    types[0] = Some(0);
    types[1] = Some(2);
    types[2] = Some(1);
    types[3] = Some(2);
    let adjusted = adjust_clothes_state_types(types, [true, false, true, false], false, false);
    assert_eq!(adjusted[0..4], [Some(0), Some(0), Some(1), Some(2)]);
    types[0] = Some(1);
    types[1] = Some(2);
    assert_eq!(adjust_clothes_state_types(types, [true, false, false, false], false, false)[1], Some(1));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn vanilla_one_piece_top_reference_is_scoped_to_its_prefab() {
    let game = &crate::test_support::game_path(r"");
    if !game.join("abdata/list/characustom").ok_is_dir() { return; }
    let catalog = load_characustom_dir(&game.join("abdata/list/characustom"));
    let mut candidates = 0;
    let mut found = false;
    for id in 0..500 {
        let Some(entry) = catalog.get(105, id) else { continue; };
        if entry.info("Coordinate").trim() != "2"
            || is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
        candidates += 1;
        if let Some(bytes) = accessory_bundle_bytes(game, None, entry) {
            if crate::unity::prefab_node_names(&bytes, &entry.main_data).contains("n_bot_a") {
                found = true;
                break;
            }
        }
    }
    assert!(candidates > 0, "expected vanilla one-piece tops to validate prefab traversal");
    assert!(found, "no one-piece top exposed ChaReference n_bot_a");
}

#[test]
fn resolved_clothes_state_scope_restores_previous_card() {
    let before = crate::unity::clothes_states();
    {
        let _scope = crate::unity::set_session_clothes_states([3; 9]);
        assert_eq!(crate::unity::clothes_states(), [3; 9]);
    }
    assert_eq!(crate::unity::clothes_states(), before);
}
use std::ptr;

#[test]
fn clothes_prefab_cache_variant_includes_runtime_state() {
    let worn = [0; 9];
    let mut half = worn;
    half[0] = 1;
    assert_ne!(entry_prefab_variant_key(105, true, worn),
        entry_prefab_variant_key(105, true, half));
    assert_ne!(entry_prefab_variant_key(200, true, worn),
        entry_prefab_variant_key(200, true, half));
    assert_eq!(entry_prefab_variant_key(122, true, worn),
        entry_prefab_variant_key(122, true, half));
    assert_ne!(entry_prefab_variant_key(105, true, worn),
        entry_prefab_variant_key(105, false, worn));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_hair_diffuse() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\DeathWeasel AKA Anon11\[DeathWeasel] Accessory Hair Back v1.6.5.zipmod"),
    );
    let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(&zip, "chara/deathweasel/acc_bo_hair_b.unity3d") else {
        eprintln!("no bundle");
        return;
    };
    let (meshes, _) = crate::unity::meshes_skel_for_maindata_ref(&bytes, "p_cf_hair_b_00_e");
    let Some(m) = meshes.iter().find(|m| m.name.contains("122")) else {
        eprintln!("meshes={}", meshes.len());
        for x in meshes.iter().take(8) {
            eprintln!("  {}", x.name);
        }
        return;
    };
    let mut umin = 9f32;
    let mut umax = -9f32;
    let mut vmin = 9f32;
    let mut vmax = -9f32;
    for uv in &m.uvs {
        umin = umin.min(uv[0]);
        umax = umax.max(uv[0]);
        vmin = vmin.min(uv[1]);
        vmax = vmax.max(uv[1]);
    }
    eprintln!(
        "HAIR {} uv=[{umin:.3},{umax:.3}]x[{vmin:.3},{vmax:.3}] mask={}",
        m.name,
        m.color_mask_png.is_some()
    );
    let c1 = [0.5518, 0.562555, 0.62, 1.0];
    let c2 = [0.4784, 0.4867, 0.52, 1.0];
    let c3 = [0.4549, 0.4745, 0.5347, 1.0];
    if let (Some(main), Some(mask)) = (m.texture_png.as_deref(), m.color_mask_png.as_deref()) {
        if let Some(t) = crate::unity::hair_get_diffuse(main, Some(mask), c1, c2, c3) {
            eprintln!("DIFFUSE mean={:?}", png_mean(&t));
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_alice_hair_tex() {
    let game = crate::test_support::game_path(r"");
    let cat = load_characustom_dir(&game.join("abdata").join("list").join("characustom"));
    let card = parse_card_outfit(
        &crate::test_support::game_path(r"\UserData\chara\female\Alice_keiyuu.png"),
        0,
    )
    .expect("card");
    let hair = &card.hair_parts[0];
    eprintln!(
        "ALICE base={:?} start={:?} end={:?} gloss={:?} outline={:?}",
        hair.color, hair.start_color, hair.end_color, hair.gloss_color, hair.outline_color
    );
    let wanted: std::collections::HashSet<String> = card.resolves.iter()
        .filter(|r| accessory_slot_from_property(&r.property).is_some_and(|s| s < 3))
        .map(|r| r.mod_id.clone()).collect();
    let guids = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
    let mod_cat = crate::list::load_mods_catalog(&game.join("mods"));
    let mut zip_lists = std::collections::HashMap::new();
    for slot in [0usize, 1, 2] {
        let a = &card.accessory[slot];
        let resolve = card.resolves.iter().find(|r|
            accessory_slot_from_property(&r.property) == Some(slot));
        let zip = resolve.and_then(|r| guid_path(&guids, &r.mod_id));
        let entry = resolve.and_then(|r| lookup_entry(
            &cat, &mut zip_lists, zip.map(|p| p.as_path()), &r.mod_id,
            r.category_no, r.slot, r.local_slot,
        )).or_else(|| mod_cat.get(a.type_id, a.id).cloned())
            .or_else(|| cat.get(a.type_id, a.id).cloned());
        let Some(entry) = entry else {
            eprintln!("slot {slot} no entry type={} id={}", a.type_id, a.id);
            continue;
        };
        let zip_fallback = if zip.is_none() && !entry.info("__zip").is_empty() {
            Some(PathBuf::from(entry.info("__zip")))
        } else { None };
        let zip_path = zip.map(|p| p.as_path()).or_else(|| zip_fallback.as_deref());
        eprintln!(
            "slot {slot} ab={} data={} name={}",
            entry.main_ab, entry.main_data, entry.name
        );
        let Some(bytes) = accessory_bundle_bytes(&game, zip_path, &entry) else {
            eprintln!("  no bytes");
            continue;
        };
        let use_hint = crate::unity::accessory_color_use(&bytes, &entry.main_data);
        if let Some(u) = &use_hint {
            eprintln!(
                "  use01={} hair={} custom_hair={} normal={} alpha={} rend_acc={}",
                u.use01,
                u.hair.len(),
                u.custom_hair.len(),
                u.normal.len(),
                u.alpha.len(),
                u.rend_accessory.len()
            );
        }
        let (mut meshes, _) =
            crate::unity::meshes_skel_for_maindata_ref(&bytes, &entry.main_data);
        for m in meshes.iter().take(6) {
            let shader = m.mat.as_ref().map(|mm| mm.shader.as_str()).unwrap_or("");
            let in_hair = use_hint
                .as_ref()
                .map(|u| ids_hit(&m.renderer_ids, &u.hair))
                .unwrap_or(false);
            let in_custom = use_hint
                .as_ref()
                .map(|u| ids_hit(&m.renderer_ids, &u.custom_hair))
                .unwrap_or(false);
            let in_normal = use_hint
                .as_ref()
                .map(|u| ids_hit(&m.renderer_ids, &u.normal))
                .unwrap_or(false);
            eprintln!(
                "  mesh {} shader={shader} main={:?} mask={} hair={in_hair} custom={in_custom} normal={in_normal} ids={:?}",
                m.name,
                m.texture_png.as_deref().and_then(png_mean),
                m.color_mask_png.is_some(),
                m.renderer_ids
            );
            if let Some(main) = m.texture_png.as_deref() {
                if let Some(t) = crate::unity::hair_get_diffuse(
                    main,
                    m.color_mask_png.as_deref(),
                    hair.color,
                    hair.start_color,
                    hair.end_color,
                ) {
                    eprintln!("    diffuse={:?}", png_mean(&t));
                }
            }
        }
        let before: Vec<(String, Option<[f32; 4]>)> = meshes.iter()
            .map(|m| (m.name.clone(), m.texture_png.as_deref().and_then(png_mean)))
            .collect();
        let property = resolve.map(|r| r.property.as_str()).unwrap_or("");
        color_accessory_meshes(&game, zip_path, &entry, &card,
            property, 0, &mut meshes, &[], &[], None);
        for (m, (_, mean)) in meshes.iter().zip(before) {
            eprintln!("  baked {} source={mean:?} result={:?}", m.name,
                m.texture_png.as_deref().and_then(png_mean));
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn alice_hair_gloss_uses_card_selected_mod_list_entry() {
    let game = &crate::test_support::game_path(r"");
    let path = game.join(r"UserData\chara\female\Alice_keiyuu.png");
    if !path.ok_is_file() { return; }
    let card = crate::card::parse_card_outfit(&path, 0).unwrap();
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods = crate::list::load_mods_catalog(&game.join("mods"));
    let wanted = card.resolves.iter().map(|r| r.mod_id.clone()).collect();
    let index = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
    let (png, sampling) = selected_hair_gloss_texture(game, &card, &vanilla, &mods, &index)
        .expect("ChaControl.LoadHairGlossMask texture");
    assert!(png.len() > 10_000);
    assert!(sampling.mip_count >= 1);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn android18_hair_gloss_retains_native_texture_sampling() {
    let game = &crate::test_support::game_path(r"");
    let path = game.join(r"UserData\chara\female\KK_381623.png");
    if !path.ok_is_file() { return; }
    let card = crate::card::parse_card_outfit(&path, 0).unwrap();
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods = crate::list::load_mods_catalog(&game.join("mods"));
    let wanted = card.resolves.iter().map(|r| r.mod_id.clone()).collect();
    let index = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
    let (png, sampling) = selected_hair_gloss_texture(game, &card, &vanilla, &mods, &index)
        .expect("ChaControl.LoadHairGlossMask texture");
    assert!(png.len() > 10_000);
    // HairGloss312Native181 sampled cf_hair_00_04_mh in Studio.
    assert_eq!(sampling.filter, 1);
    assert_eq!((sampling.wrap_u, sampling.wrap_v), (0, 0));
    assert!(sampling.srgb);
    assert_eq!(sampling.mip_count, 11);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn android18_hair_detail_mask_retains_native_texture_sampling() {
    let game = &crate::test_support::game_path(r"");
    let path = game.join(r"UserData\chara\female\KK_381623.png");
    if !path.ok_is_file() { return; }
    let card = crate::card::parse_card_outfit(&path, 0).unwrap();
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods = crate::list::load_mods_catalog(&game.join("mods"));
    let wanted = card.resolves.iter().map(|r| r.mod_id.clone()).collect();
    let index = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
    let hair_id = card.hair_ids[0];
    let resolved = card.resolves.iter().find(|r|
        (r.slot == hair_id || r.local_slot == hair_id) && r.category_no == 101);
    let (zip, entry) = if let Some(r) = resolved {
        let zip = guid_path(&index, &r.mod_id).expect("hair back mod");
        let catalog = crate::list::catalog_from_zipmod(zip);
        let entry = catalog.get_slot(101, r.slot, r.local_slot)
            .expect("hair back row").clone();
        (Some(zip.clone()), entry)
    } else {
        (None, mods.get(101, hair_id).or_else(|| vanilla.get(101, hair_id))
            .expect("hair back row").clone())
    };
    let (meshes, _) = meshes_for_entry_shake(game, zip.as_deref(), &entry,
        !card.hair_parts[0].no_shake);
    let back = meshes.iter().find(|m| m.name == "cf_hair_b_21_00")
        .expect("sampled native back hair");
    // The runtime Studio mesh dump is an independent oracle for shader
    // inputs. A tangent/normal/UV mismatch can masquerade as a hair-light
    // formula error even when the source textures are correct.
    let dump_path = game.join("mesh_dump/KK_381623.png/Android 18_0/mesh_26_cf_hair_b_21_00.json");
    if dump_path.ok_is_file() {
        let dump: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump_path).unwrap()).unwrap();
        for (name, actual, keys) in [
            ("normals", back.normals.iter().map(|v| v.to_vec()).collect::<Vec<_>>(), &["x", "y", "z"][..]),
            ("tangents", back.tangents.iter().map(|v| v.to_vec()).collect::<Vec<_>>(), &["x", "y", "z", "w"][..]),
            ("uv", back.uvs.iter().map(|v| v.to_vec()).collect::<Vec<_>>(), &["x", "y"][..]),
            ("uv2", back.uv2.iter().map(|v| v.to_vec()).collect::<Vec<_>>(), &["x", "y"][..]),
        ] {
            let source = dump["mesh"][name].as_array().expect("native mesh channel");
            assert_eq!(actual.len(), source.len(), "{name} count");
            for (vertex, (got, expected)) in actual.iter().zip(source).enumerate() {
                for (axis, key) in keys.iter().enumerate() {
                    let native = expected[*key].as_f64().unwrap() as f32;
                    assert!((got[axis] - native).abs() < 2e-5,
                        "{name}[{vertex}].{key}: {} vs {native}", got[axis]);
                }
            }
        }
    }
    assert!(back.detail_mask_png.is_some());
    // The Studio renderer snapshot for this exact material reports
    // mf_tex2_md: 1024² Linear, 11 mips, Bilinear, Repeat/Repeat.
    assert_eq!(back.runtime_textures.get("_DetailMask"), Some(&[1, 0, 0, 0, 11]));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn android18_front_hair_masks_keep_independent_color_spaces() {
    let game = &crate::test_support::game_path(r"");
    let path = game.join(r"UserData\chara\female\KK_381623.png");
    if !path.ok_is_file() { return; }
    let card = crate::card::parse_card_outfit(&path, 0).unwrap();
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods = crate::list::load_mods_catalog(&game.join("mods"));
    let wanted = card.resolves.iter().map(|r| r.mod_id.clone()).collect();
    let index = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
    let mut zip_lists = std::collections::HashMap::new();
    for (slot, mesh_name) in [(8usize, "cf_hair_f_111_05_g"),
                              (9usize, "cf_hair_f_113_04_k")] {
        let accessory = &card.accessory[slot];
        let resolve = card.resolves.iter().find(|r|
            accessory_slot_from_property(&r.property) == Some(slot));
        let zip = resolve.and_then(|r| guid_path(&index, &r.mod_id));
        let entry = resolve.and_then(|r| lookup_entry(
            &vanilla, &mut zip_lists, zip.map(|p| p.as_path()),
            &r.mod_id, r.category_no, r.slot, r.local_slot,
        )).or_else(|| mods.get(accessory.type_id, accessory.id).cloned())
            .or_else(|| vanilla.get(accessory.type_id, accessory.id).cloned())
            .expect("front hair accessory list row");
        let zip_fallback = if zip.is_none() && !entry.info("__zip").is_empty() {
            Some(PathBuf::from(entry.info("__zip")))
        } else { None };
        let zip_path = zip.map(|p| p.as_path()).or(zip_fallback.as_deref());
        let bytes = accessory_bundle_bytes(game, zip_path, &entry)
            .expect("front hair accessory bundle");
        let (meshes, _) = crate::unity::meshes_skel_for_maindata_ref(&bytes, &entry.main_data);
        let front = meshes.iter().find(|m| m.name == mesh_name).expect("front hair mesh");
        assert_eq!(front.runtime_textures.get("_ColorMask"), Some(&[1, 0, 0, 0, 12]));
        assert_eq!(front.runtime_textures.get("_DetailMask"), Some(&[1, 0, 0, 1, 12]));
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_face_skin_layout() {
    std::env::set_var("OK_DIAG", "1");
    std::env::set_var("OK_SKINCH", "1");
    std::env::set_var("OK_DUMP_V", "342,0,100");
    let zip = std::path::PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack - Exclusive KKS\i\[i] Modified headmod v2.2.zipmod"),
    );
    let bytes = crate::mods::zipmod_bundle_named(&zip, "chara/modified_headmod.unity3d").expect("b");
    let (meshes, _) = crate::unity::meshes_skel_for_maindata_ref(&bytes, "p_cf_head_00");
    let face = meshes.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")).expect("face");
    let mut low = 0;
    for w in &face.weights {
        if w.iter().sum::<f32>() < 0.5 {
            low += 1;
        }
    }
    eprintln!("FACEWEIGHTS n={} low={}", face.weights.len(), low);
    eprintln!("v342 {:?} {:?}", face.bone_idx.get(342), face.weights.get(342));
    eprintln!("v0 {:?} {:?}", face.bone_idx.get(0), face.weights.get(0));
    let van = crate::gamefs::read(crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d")).unwrap_or_default();
    if !van.is_empty() {
        let (ms, _) = crate::unity::meshes_skel_for_maindata_ref(&van, "p_cf_head_00");
        if let Some(f) = ms.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")) {
            let low = f.weights.iter().filter(|w| w.iter().sum::<f32>() < 0.5).count();
            eprintln!("VANILLA face n={} low={low}", f.weights.len());
        }
    }
    let body = crate::gamefs::read(crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d")).unwrap_or_default();
    if !body.is_empty() {
        let (ms, _) = crate::unity::meshes_skel_for_maindata_ref(&body, "p_cf_body_bone");
        if let Some(b) = ms.iter().find(|m| m.name.eq_ignore_ascii_case("o_body_a")) {
            let low = b.weights.iter().filter(|w| w.iter().sum::<f32>() < 0.5).count();
            eprintln!("BODY o_body_a n={} low={low}", b.weights.len());
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_chin_profile() {
    std::env::set_var("OK_NOCACHE", "1");
    std::env::set_var("OK_GPU_SKIN", "1");
    std::env::set_var("OK_NOPOSE", "1");
    std::env::set_var("OK_CHINDIAG", "1");
    let root = &crate::test_support::game_path(r"");
    let card = Path::new(crate::test_support::game_file(r"\UserData\chara\female\[Card] March_7th_keiyuu.png"),
    );
    let sess = extract_session(root, Some(card), 0, None).expect("session");
    let profile = |name: &str, pos: &[[f32; 3]]| {
        eprintln!("PROFILE {name} nv={}", pos.len());
        for i in -11..=11 {
            let x0 = i as f32 * 0.004;
            let mut ymin = f32::MAX;
            let mut n = 0;
            for p in pos {
                if (p[0] - x0).abs() < 0.002 && p[1] < 1.36 && p[1] > 1.20 && p[2] > -0.02 {
                    ymin = ymin.min(p[1]);
                    n += 1;
                }
            }
            if n > 0 {
                let mut zmin = 0.0f32;
                for p in pos {
                    if (p[0] - x0).abs() < 0.002 && (p[1] - ymin).abs() < 1e-4 && p[2] > -0.02 {
                        zmin = p[2];
                    }
                }
                eprintln!("  x={x0:.3} ymin={ymin:.4} z={zmin:.4} n={n}");
            }
        }
    };
    if let Some(sk) = sess.head_skel.as_ref() {
        for n in [
            "cf_J_ChinLow",
            "cf_J_CheekLow_s_L",
            "cf_J_FaceLow_sx",
            "cf_J_FaceBase",
            "cf_J_FaceUp_ty",
        ] {
            if let Some(w) = sk.world_tf(n) {
                eprintln!("OURS {n} {:?}", crate::pose::mat_pos(&w));
            }
        }
    }
    if let Some(face) = sess.parts.iter().find(|p| p.name.eq_ignore_ascii_case("cf_O_face")) {
        profile("face", &face.positions);
    }
    if let Some(body) = sess.parts.iter().find(|p| p.name.eq_ignore_ascii_case("o_body_a")) {
        profile("body", &body.positions);
        eprintln!("BODY TOP");
        for i in -11..=11 {
            let x0 = i as f32 * 0.004;
            let mut ymax = f32::MIN;
            let mut n = 0;
            for p in &body.positions {
                if (p[0] - x0).abs() < 0.002 && p[1] > 1.15 && p[1] < 1.45 && p[2] > -0.02 {
                    ymax = ymax.max(p[1]);
                    n += 1;
                }
            }
            if n > 0 {
                eprintln!("  x={x0:.3} ymax={ymax:.4} n={n}");
            }
        }
    }
}

fn mesh_max_edge(pos: &[[f32; 3]], idx: &[u32]) -> (f32, usize) {
    let n = pos.len();
    let mut max = 0f32;
    let mut nlong = 0usize;
    for tri in idx.chunks(3) {
        if tri.len() < 3 {
            continue;
        }
        let ids = [tri[0] as usize, tri[1] as usize, tri[2] as usize];
        if ids.iter().any(|&i| i >= n) {
            continue;
        }
        for k in 0..3 {
            let a = pos[ids[k]];
            let b = pos[ids[(k + 1) % 3]];
            let d = ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt();
            if d > max {
                max = d;
            }
            if d > 0.05 {
                nlong += 1;
            }
        }
    }
    (max, nlong)
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn face_skin_max_edge_stays_tight() {
    let game = &crate::test_support::game_path(r"");
    let head_path = game.join("abdata/chara/bo_head_00.unity3d");
    if !head_path.ok_is_file() {
        return;
    }
    std::env::set_var("OK_DIAG", "1");
    std::env::set_var("OK_SKINCH", "1");
    crate::unity::clear_bundle_cache();
    let bytes = crate::gamefs::read(&head_path).unwrap();
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let (meshes, mut skel) = parsed.meshes_for_maindata("p_cf_head_00");
    let mut face = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        .cloned()
        .expect("cf_O_face");
    if let Some(s) = parsed.skeleton(face.path_id) {
        skel = Some(s);
    }
    let rest = mesh_max_edge(&face.positions, &face.indices);
    let sk = skel.as_mut().expect("head skel");
    let nbones = sk.names.len() as u16;
    let mut oob = 0usize;
    let mut wsum_ok = 0usize;
    let mut wsum_low = 0usize;
    let mut max_bi = 0u16;
    for i in 0..face.positions.len() {
        let b = face.bone_idx.get(i).copied().unwrap_or([0; 4]);
        let w = face.weights.get(i).copied().unwrap_or([0.0; 4]);
        for k in 0..4 {
            max_bi = max_bi.max(b[k]);
            if w[k] > 0.02 && b[k] >= nbones {
                oob += 1;
                break;
            }
        }
        let s: f32 = w.iter().sum();
        if (0.9..1.1).contains(&s) {
            wsum_ok += 1;
        } else if s < 0.5 {
            wsum_low += 1;
        }
    }
    eprintln!(
        "FACE rest maxedge={:.4} nlong={} nv={} binds={} smr={} tfs={} max_bi={max_bi} oob={oob} wsum_ok={wsum_ok} wsum_low={wsum_low}",
        rest.0,
        rest.1,
        face.positions.len(),
        face.bind_poses.len(),
        sk.names.len(),
        sk.tfs.len()
    );
    let dst_names = [
        "cf_J_FaceBase", "cf_J_FaceUp_ty", "cf_J_CheekUp2_L", "cf_J_ChinTip_Base",
        "cf_J_Eye_rz_L", "cf_J_Eye05_s_L", "cf_J_Eye02_s_L", "cf_J_MouthBase_ty",
    ];
    for n in dst_names {
        let in_smr = sk.names.iter().any(|s| s == n);
        let in_tf = sk.tfs.values().any(|t| t.name == n);
        eprintln!("FACEBONE {n} smr={in_smr} tf={in_tf}");
    }

    let card_path =
        &crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
    let card = crate::card::parse_card(card_path).expect("card");
    let keys = crate::shape::load_head_keys(&head_path);
    crate::shape::apply_head_shape_keys(sk, &keys, &card.shape_face, [1.0; 3], 1.0, 0);
    crate::pose::assigned_weights_remap(sk);

    let before_ids = face.bone_idx.clone();
    std::env::set_var("OK_NOREP", "1");
    std::env::set_var("OK_NOREPAIR", "1");
    std::env::set_var("OK_NOPRUNE", "1");
    std::env::set_var("OK_NOFBS", "1");
    let mut face_clean = face.clone();
    let hung = crate::skin_mesh(&mut face_clean, sk);
    std::env::remove_var("OK_NOREP");
    std::env::remove_var("OK_NOREPAIR");
    std::env::remove_var("OK_NOPRUNE");
    std::env::remove_var("OK_NOFBS");
    let clean = mesh_max_edge(&face_clean.positions, &face_clean.indices);
    eprintln!("FACE skin-clean hung={hung} maxedge={:.4} nlong={}", clean.0, clean.1);

    let mut face_rep = face.clone();
    let hung2 = crate::skin_mesh(&mut face_rep, sk);
    let repaired = mesh_max_edge(&face_rep.positions, &face_rep.indices);
    eprintln!("FACE skin-repair hung={hung2} maxedge={:.4} nlong={}", repaired.0, repaired.1);

    // Where are the long edges?
    let pos = &face_clean.positions;
    let n = pos.len();
    let mut shown = 0usize;
    for tri in face_clean.indices.chunks(3) {
        if tri.len() < 3 {
            continue;
        }
        let ids = [tri[0] as usize, tri[1] as usize, tri[2] as usize];
        if ids.iter().any(|&i| i >= n) {
            continue;
        }
        for k in 0..3 {
            let a = pos[ids[k]];
            let b = pos[ids[(k + 1) % 3]];
            let d = ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt();
            if d > 0.05 && shown < 8 {
                let i = ids[k];
                eprintln!(
                    "LONG d={:.4} v{} pos=[{:.3},{:.3},{:.3}] bones={:?} w={:?} restbones={:?}",
                    d,
                    i,
                    a[0],
                    a[1],
                    a[2],
                    face_clean.bone_idx.get(i),
                    face_clean.weights.get(i),
                    before_ids.get(i)
                );
                shown += 1;
            }
        }
    }
    std::env::remove_var("OK_DIAG");
    std::env::remove_var("OK_SKINCH");
    assert!(
        clean.0 < 0.05,
        "shaped+skinned cf_O_face max edge {:.4} ({} edges>5cm); dump is 0.033",
        clean.0,
        clean.1
    );
    assert!(
        wsum_ok > 1000 && max_bi < 45,
        "face weights still look decoded wrong wsum_ok={wsum_ok} max_bi={max_bi}"
    );
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn iris_skins_to_hitomi_not_parent() {
    // StudioEditor RebindSkinning maps every head SMR onto the full
    // p_cf_head_bone tree. The iris is 1-bone cf_J_hitomi_tx_L, which is
    // not in the 45-bone cf_O_face SMR list. Without those extra names
    // the iris falls back to cf_J_Eye_rz_L and crumples.
    let game = &crate::test_support::game_path(r"");
    let head_path = game.join("abdata/chara/bo_head_00.unity3d");
    if !head_path.ok_is_file() {
        return;
    }
    crate::unity::clear_bundle_cache();
    let bytes = crate::gamefs::read(&head_path).unwrap();
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let (meshes, mut skel) = parsed.meshes_for_maindata("p_cf_head_00");
    let face = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        .expect("cf_O_face");
    if let Some(s) = parsed.skeleton(face.path_id) {
        skel = Some(s);
    }
    let sk = skel.as_mut().expect("head skel");
    let hitomi_tf = sk.tfs.values().any(|t| t.name == "cf_J_hitomi_tx_L");
    eprintln!(
        "IRIS before include smr={} tfs={} hitomi_tf={hitomi_tf} hitomi_smr={}",
        sk.names.len(),
        sk.tfs.len(),
        sk.index_of("cf_J_hitomi_tx_L").is_some()
    );
    sk.include_tree_bones();
    eprintln!(
        "IRIS after include smr={} names={} tfs={} hitomi_idx={:?}",
        sk.smr_count,
        sk.names.len(),
        sk.tfs.len(),
        sk.index_of("cf_J_hitomi_tx_L")
    );
    for n in [
        "a_n_head",
        "a_n_headtop",
        "a_n_megane",
        "cf_J_hitomi_tx_L",
        "cf_J_Nose_tip",
    ] {
        let in_n = sk.index_of(n).is_some();
        let tf = sk.tfs.values().find(|t| t.name == n);
        eprintln!(
            "IRIS attach {n} smr={in_n} tf={} pos={:?}",
            tf.is_some(),
            tf.map(|t| t.pos)
        );
    }
    assert!(
        sk.index_of("cf_J_hitomi_tx_L").is_some(),
        "cf_J_hitomi_tx_L must be in head_skel.names so iris can skin to it"
    );
    assert!(
        sk.index_of("cf_J_hitomi_tx_R").is_some(),
        "cf_J_hitomi_tx_R must be in head_skel.names"
    );

    let mut iris = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_Ohitomi_L02"))
        .cloned()
        .expect("cf_Ohitomi_L02");
    let iris_skel = parsed.skeleton(iris.path_id).expect("iris skel");
    assert_eq!(iris_skel.names.len(), 1, "iris SMR is 1 bone, got {:?}", iris_skel.names);
    assert_eq!(iris_skel.names[0], "cf_J_hitomi_tx_L");
    eprintln!(
        "IRIS mesh verts={} weights={} bone_idx={} binds={} first_bi={:?} first_w={:?}",
        iris.positions.len(),
        iris.weights.len(),
        iris.bone_idx.len(),
        iris.bind_poses.len(),
        iris.bone_idx.first(),
        iris.weights.first()
    );
    remap_part_to_body(&mut iris, sk, &iris_skel);
    let mapped = iris.bone_idx.first().map(|b| b[0]).unwrap_or(0);
    let mapped_name = sk.names.get(mapped as usize).cloned().unwrap_or_default();
    eprintln!("IRIS mapped bone {mapped} name={mapped_name}");
    assert_eq!(
        mapped_name, "cf_J_hitomi_tx_L",
        "iris must remap to hitomi, not a parent ({mapped_name})"
    );

    let sclera = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_Ohitomi_L"))
        .expect("cf_Ohitomi_L");
    let sclera_skel = parsed.skeleton(sclera.path_id).expect("sclera skel");
    let missing: Vec<&str> = sclera_skel
        .names
        .iter()
        .filter(|n| sk.index_of(n).is_none())
        .map(|s| s.as_str())
        .collect();
    eprintln!(
        "IRIS sclera bones={} missing_in_head={}",
        sclera_skel.names.len(),
        missing.len()
    );
    assert!(
        missing.is_empty(),
        "sclera SMR bones missing from unified head_skel: {missing:?}"
    );

    std::env::set_var("OK_NOREP", "1");
    std::env::set_var("OK_NOREPAIR", "1");
    std::env::set_var("OK_NOPRUNE", "1");
    std::env::set_var("OK_NOFBS", "1");
    let hung = crate::skin_mesh(&mut iris, sk);
    std::env::remove_var("OK_NOREP");
    std::env::remove_var("OK_NOREPAIR");
    std::env::remove_var("OK_NOPRUNE");
    std::env::remove_var("OK_NOFBS");
    let (max_edge, nlong) = mesh_max_edge(&iris.positions, &iris.indices);
    let (mn, mx) = crate::pose::aabb(&iris.positions);
    let ysz = mx[1] - mn[1];
    let xsz = mx[0] - mn[0];
    eprintln!(
        "IRIS skinned hung={hung} maxedge={max_edge:.4} nlong={nlong} aabb x[{:.3},{:.3}] y[{:.3},{:.3}] z[{:.3},{:.3}] ysz={ysz:.4} xsz={xsz:.4}",
        mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]
    );
    assert!(hung, "iris should skin");
    assert!(
        max_edge < 0.05,
        "iris crumpled: max edge {max_edge:.4} ({nlong} edges>5cm)"
    );
    assert!(
        ysz > 0.015 && ysz < 0.055,
        "iris y extent {ysz:.4} (dump world ~0.030); crumpled or scaled wrong"
    );

    // Production extract applies shape then assigned_weights_remap (which
    // rewrites rest to the posed worlds) before skinning with repair on.
    let card_path =
        &crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
    if let Ok(card) = crate::card::parse_card(card_path) {
        let keys = crate::shape::load_head_keys(&head_path);
        crate::shape::apply_head_shape_keys(sk, &keys, &card.shape_face, [1.0; 3], 1.0, 0);
        crate::pose::assigned_weights_remap(sk);
        sk.include_tree_bones();
    }
    for (want, max_e) in [
        ("cf_Ohitomi_L", 0.05f32),
        ("cf_Ohitomi_L02", 0.05),
        ("cf_O_eyeline", 0.08),
        ("cf_O_mayuge", 0.08),
    ] {
        let mut m = meshes
            .iter()
            .find(|x| x.name.eq_ignore_ascii_case(want))
            .cloned()
            .unwrap_or_else(|| panic!("{want}"));
        if let Some(ps) = parsed.skeleton(m.path_id) {
            remap_part_to_body(&mut m, sk, &ps);
        }
        let hung = crate::skin_mesh(&mut m, sk);
        let (e, nlong) = mesh_max_edge(&m.positions, &m.indices);
        let (mn, mx) = crate::pose::aabb(&m.positions);
        eprintln!(
            "IRIS {want} hung={hung} maxedge={e:.4} nlong={nlong} y[{:.3},{:.3}] z[{:.3},{:.3}]",
            mn[1], mx[1], mn[2], mx[2]
        );
        assert!(hung, "{want} should skin");
        assert!(
            e < max_e,
            "{want} crumpled after shape+repair: max edge {e:.4} ({nlong} edges>5cm)"
        );
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_face_override() {
    let vanilla = &crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    let vb = crate::gamefs::read(vanilla).unwrap_or_default();
    let (vmeshes, vskel, _) =
        crate::unity::load_base_bundle_from_memory(vb, "bo_head_00");
    for m in vmeshes.iter().filter(|m| {
        m.name.eq_ignore_ascii_case("cf_O_face")
            || m.name.to_ascii_lowercase().contains("hitomi")
    }) {
        if m.positions.is_empty() {
            continue;
        }
        let mut mn = [f32::MAX; 3];
        let mut mx = [f32::MIN; 3];
        for p in &m.positions {
            for k in 0..3 {
                mn[k] = mn[k].min(p[k]);
                mx[k] = mx[k].max(p[k]);
            }
        }
        eprintln!(
            "VANILLA mesh {} nv={} bind={} aabb x[{:.4},{:.4}] y[{:.4},{:.4}] z[{:.4},{:.4}] skel={}",
            m.name, m.positions.len(), m.bind_poses.len(),
            mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], vskel.is_some()
        );
    }
    let zip = Path::new(crate::test_support::game_file(r"\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod"),
    );
    let Some((name, bytes)) = crate::mods::zipmod_one_unity3d(zip, "bo_head_00") else {
        eprintln!("FACE_OVR no bundle");
        return;
    };
    eprintln!("FACE_OVR bundle {name} bytes={}", bytes.len());
    let (meshes, skel, texs) =
        crate::unity::load_base_bundle_from_memory(bytes.clone(), "bo_head_00");
    eprintln!(
        "FACE_OVR meshes={} skel={} texs={}",
        meshes.len(),
        skel.is_some(),
        texs.len()
    );
    for m in meshes.iter().filter(|m| {
        m.name.eq_ignore_ascii_case("cf_O_face")
            || m.name.to_ascii_lowercase().contains("hitomi")
    }) {
        let n = m.positions.len();
        if n == 0 {
            continue;
        }
        let mut mn = [f32::MAX; 3];
        let mut mx = [f32::MIN; 3];
        for p in &m.positions {
            for k in 0..3 {
                mn[k] = mn[k].min(p[k]);
                mx[k] = mx[k].max(p[k]);
            }
        }
        let uv0 = m.uvs.first().copied().unwrap_or_default();
        eprintln!(
            "FACE_OVR mesh {} nv={} bind={} aabb x[{:.4},{:.4}] y[{:.4},{:.4}] z[{:.4},{:.4}] uv0={uv0:?}",
            m.name,
            n,
            m.bind_poses.len(),
            mn[0],
            mx[0],
            mn[1],
            mx[1],
            mn[2],
            mx[2]
        );
    }
}

#[test]
fn ffi_null_game_root_fails() {
    let mut err = [0u8; 128];
    let rc = ok_extract_card(
        ptr::null(),
        ptr::null(),
        ptr::null(),
        err.as_mut_ptr() as *mut c_char,
        err.len() as c_int,
    );
    assert_eq!(rc, 1);
    let msg = unsafe { CStr::from_ptr(err.as_ptr() as *const c_char) };
    assert!(!msg.to_bytes().is_empty());
}

#[test]
fn ffi_missing_abdata_fails() {
    let game = CString::new("C:/portable/OpenKoikatsu/Saved/no_such_game").unwrap();
    let out = CString::new("C:/portable/OpenKoikatsu/Saved/Converted/_ffi_missing").unwrap();
    let mut err = [0u8; 256];
    let rc = ok_extract_card(
        game.as_ptr(),
        ptr::null(),
        out.as_ptr(),
        err.as_mut_ptr() as *mut c_char,
        err.len() as c_int,
    );
    assert_eq!(rc, 1);
    let msg = unsafe { CStr::from_ptr(err.as_ptr() as *const c_char) }
        .to_string_lossy()
        .into_owned();
    assert!(msg.contains("abdata"), "{msg}");
}

#[test]
fn body_create_shader_uses_green_for_sub_and_blue_for_nails() {
    let main = crate::unity::solid_png([1.0, 1.0, 1.0, 1.0], 3, 1).unwrap();
    let mut mask = image::RgbaImage::new(3, 1);
    mask.put_pixel(0, 0, image::Rgba([255, 0, 0, 255]));
    mask.put_pixel(1, 0, image::Rgba([0, 255, 0, 255]));
    mask.put_pixel(2, 0, image::Rgba([0, 0, 191, 255]));
    let mask = encode_png(&mask).unwrap();
    let out = crate::unity::apply_body_create_colors(
        &main, &mask,
        [0.98, 0.9342667, 0.90160006, 1.0],
        [0.80222213, 0.6018249, 0.528676, 1.0],
        [1.0, 0.875, 0.875, 1.0],
    ).unwrap();
    let img = crate::teximg::load_from_memory(&out).unwrap().into_rgba8();
    assert_eq!(img.get_pixel(0, 0).0, [250, 238, 230, 255]);
    assert_eq!(img.get_pixel(1, 0).0, [200, 143, 121, 255]);
    assert_eq!(img.get_pixel(2, 0).0, [250, 216, 209, 255]);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn diag_attach_spaces() {
    let game = crate::test_support::game_path(r"");
    let body_b = game.join("abdata/chara/oo_base.unity3d");
    let head_b = game.join("abdata/chara/bo_head_00.unity3d");
    let (_, body_skel, _) = crate::unity::load_base_bundle(&body_b);
    let (_, head_skel, _) = crate::unity::load_base_bundle(&head_b);
    let Some(body) = body_skel.as_ref() else { return };
    let Some(head) = head_skel.as_ref() else { return };
    let parent = body.world_tf("cf_s_head").map(|m| crate::pose::mat_pos(&m));
    let root = crate::pose::mat_pos(&head.root_world());
    eprintln!("DIAG attach cf_s_head={parent:?} head_root={root:?}");
    let attach = head_onto_body(Some(body), Some(head));
    eprintln!("DIAG attach delta_pos={:?}", crate::pose::mat_pos(&attach));
    for k in ["a_n_headside", "a_n_headtop", "cf_J_FaceUp_ty", "cf_J_FaceBase"] {
        let hw = head.world_tf(k).map(|m| crate::pose::mat_pos(&m));
        let bw = bone_world(Some(body), Some(head), k).map(|m| crate::pose::mat_pos(&m));
        eprintln!("DIAG attach {k} head_local={hw:?} body_world={bw:?}");
    }
}

#[test]
fn list_bundle() {
    let Ok(p) = std::env::var("OK_LIST_BUNDLE") else {
        return;
    };
    let bytes = crate::gamefs::read(&p).expect("read");
    let (meshes, texs, _) = crate::unity::meshes_skel_from_memory(bytes.clone());
    eprintln!("BNDLOAD meshes={} texs={} bytes={}", meshes.len(), texs.len(), bytes.len());
    if meshes.is_empty() && texs.is_empty() {
        match unity_asset_binary::bundle::load_bundle_from_memory(bytes) {
            Ok(_) => eprintln!("BNDLOAD load_bundle_from_memory ok"),
            Err(e) => eprintln!("BNDLOAD load error: {e}"),
        }
    }
    for m in &meshes {
        eprintln!(
            "BND {} verts={} tris={} tex={}",
            m.name,
            m.positions.len(),
            m.indices.len() / 3,
            m.texture_name
        );
    }
    let out = std::path::PathBuf::from(r"C:\Users\Damody\AppData\Local\Temp\opencode\bnd");
    let _ = std::fs::create_dir_all(&out);
    for (i, (n, b)) in texs.iter().enumerate() {
        let safe = super::sanitize(n);
        let f = out.join(format!("{i:02}_{safe}.png"));
        let _ = std::fs::write(&f, b);
        eprintln!("TEX {} -> {}", n, f.display());
    }
}

#[test]
fn probe_prefab_bones() {
    let Ok(p) = std::env::var("OK_PROBE_BUNDLE") else {
        return;
    };
    let prefab =
        std::env::var("OK_PROBE_PREFAB").unwrap_or_else(|_| "p_cf_head_bone".into());
    let bytes = crate::gamefs::read(&p).expect("read");
    let parsed = crate::unity::parse_bundle_cached(bytes);
    match parsed.skeleton_for_prefab_name(&prefab) {
        Some(sk) => {
            eprintln!("PREFAB {} bones={}", prefab, sk.names.len());
            for (i, n) in sk.names.iter().enumerate() {
                let scl = sk.worlds.get(i).map(crate::pose::world_scale_pub);
                let pos = sk.worlds.get(i).map(crate::pose::mat_pos);
                eprintln!("PREFABBONE {i:>3} {n:32} scl={scl:?} pos={pos:?}");
            }
        }
        None => eprintln!("PREFAB {prefab} NOT FOUND"),
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_kf_bones() {
    let game = crate::test_support::game_path(r"");
    let path = game.join("abdata/chara/oo_base.unity3d");
    let bytes = crate::gamefs::read(&path).expect("oo_base");
    let parsed = crate::unity::parse_bundle_cached(bytes);
    if let Some(sk) = parsed.skeleton_for_prefab_name("p_cf_body_bone") {
        eprintln!("KF prefab bones={} tfs={}", sk.names.len(), sk.tfs.len());
        for n in [
            "k_f_kosi02_01",
            "k_f_kosi02_00",
            "cf_s_waist02",
            "cf_pv_leg_L",
            "cf_pv_elbo_L",
            "cf_pv_root",
            "a_n_heel_R",
            "a_n_heel_L",
            "a_n_leg_R",
            "a_n_bust",
            "a_n_bust_f",
            "a_n_nip_R",
        ] {
            let id = sk.tfs.iter().find(|(_, t)| t.name == n).map(|(id, _)| *id);
            match id {
                Some(id) => {
                    let t = &sk.tfs[&id];
                    let parent = t
                        .parent
                        .and_then(|p| sk.tfs.get(&p))
                        .map(|x| x.name.clone());
                    eprintln!(
                        "KF {n} id={id} local={:?} rot={:?} parent={:?}",
                        t.pos, t.rot, parent
                    );
                }
                None => eprintln!("KF {n} NOT FOUND"),
            }
        }
    } else {
        eprintln!("KF p_cf_body_bone NOT FOUND");
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_kf_any() {
    let p = std::env::var("OK_PROBE_BUNDLE").unwrap_or_else(|_| {
        crate::test_support::game_path(r"abdata\chara\co_bot_01.unity3d").to_string_lossy().into_owned()
    });
    let bytes = crate::gamefs::read(&p).expect("bundle");
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let mut any = false;
    for prefab in [
        "p_cf_body_bone",
        "p_cf_body_00",
        "cf_bot_01",
        "",
    ] {
        if let Some(sk) = parsed.skeleton_full_named("k_f_kosi02_01") {
            any = true;
            eprintln!("KFANY bundle={p} prefab_hint={prefab} tfs={}", sk.tfs.len());
            for n in [
                "k_f_kosi02_01",
                "k_f_kosi02_00",
                "cf_s_waist02",
                "cf_j_waist02",
                "k_f_kosi00_00",
            ] {
                let mut hits: Vec<i64> = sk
                    .tfs
                    .iter()
                    .filter(|(_, t)| t.name == n)
                    .map(|(id, _)| *id)
                    .collect();
                hits.sort();
                if hits.is_empty() {
                    eprintln!("KFANY {n} absent");
                }
                for id in hits {
                    let t = &sk.tfs[&id];
                    let parent =
                        t.parent.and_then(|pp| sk.tfs.get(&pp)).map(|x| x.name.clone());
                    eprintln!(
                        "KFANY {n} id={id} local={:?} rot={:?} parent={parent:?}",
                        t.pos, t.rot
                    );
                }
            }
            break;
        }
    }
    if !any {
        eprintln!("KFANY no k_f_kosi02_01 in {p}");
    }
}

#[test]
fn probe_shape_keys() {
    let game = crate::test_support::game_path(r"");
    let keys =
        crate::shape::load_keys_named(&game, "chara/oo_base.unity3d", "cf_anmShapeBody");
    eprintln!("SHAPEKEYS total={}", keys.len());
    let mut names: Vec<&String> = keys.keys().collect();
    names.sort();
    eprintln!("SHAPEKEYS names={:?}", &names);
    for n in ["k_f_kosi02_01", "k_f_kosi02_00", "cf_s_waist02", "cf_pv_root"] {
        eprintln!("SHAPEKEY {n} present={}", keys.contains_key(n));
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_sac_body() {
    let game = crate::test_support::game_path(r"");
    let guid = std::env::var("OK_BODY_GUID").unwrap_or_else(|_| "SAC.innie".to_string());
    let Some((name, bytes)) = crate::mods::uncensor_body_bytes(&game.join("mods"), &guid)
    else {
        eprintln!("SAC body {guid} not found");
        return;
    };
    eprintln!("SAC {guid} <- {name} bytes={}", bytes.len());
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let mut found = false;
    for prefab in ["p_cf_body_bone", "p_cf_body_00"] {
        if let Some(sk) = parsed.skeleton_for_prefab_name(prefab) {
            found = true;
            eprintln!("SAC prefab {prefab} bones={} tfs={}", sk.names.len(), sk.tfs.len());
            for n in [
                "k_f_kosi02_01",
                "k_f_kosi02_00",
                "cf_s_waist02",
                "cf_pv_leg_L",
                "cf_pv_elbo_L",
                "cf_pv_root",
                "cf_j_thigh00_R",
                "cf_j_leg01_R",
            ] {
                let mut ids: Vec<i64> = sk
                    .tfs
                    .iter()
                    .filter(|(_, t)| t.name == n)
                    .map(|(id, _)| *id)
                    .collect();
                ids.sort();
                if ids.is_empty() {
                    eprintln!("SAC {n} NOT FOUND");
                }
                for id in ids {
                    let t = &sk.tfs[&id];
                    eprintln!("SAC {n} local={:?} rot={:?}", t.pos, t.rot);
                }
            }
        }
    }
    if !found {
        eprintln!("SAC no body-bone prefab in bundle");
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_kf_components() {
    use std::collections::HashMap;
    use unity_asset_core::UnityValue;
    let game = crate::test_support::game_path(r"");
    let path = game.join("abdata/chara/oo_base.unity3d");
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle(&path) else {
        return;
    };
    let _ = bundle.data_checked();
    let kf_ids: Vec<i64> = vec![
        -6375677369174745051,
        6673302577653484792,
        -3603181830018883228,
        195515472582297632,
    ];
    let pptr = |v: &UnityValue| -> Option<i64> {
        match v {
            UnityValue::Object(o) => o
                .get("m_PathID")
                .or_else(|| o.get("m_PathId"))
                .and_then(|x| x.as_i64()),
            UnityValue::Integer(i) => Some(*i),
            _ => None,
        }
    };
    fn has_id(v: &UnityValue, ids: &[i64]) -> bool {
        match v {
            UnityValue::Object(o) => {
                if let Some(p) = o.get("m_PathID").and_then(|x| x.as_i64()) {
                    if ids.contains(&p) {
                        return true;
                    }
                }
                o.values().any(|x| has_id(x, ids))
            }
            UnityValue::Array(a) => a.iter().any(|x| has_id(x, ids)),
            UnityValue::Integer(i) => ids.contains(i),
            _ => false,
        }
    }
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(fb) else {
            continue;
        };
        let mut go_name: HashMap<i64, String> = HashMap::new();
        let mut names: HashMap<i64, String> = HashMap::new();
        let mut scripts: HashMap<i64, String> = HashMap::new();
        for info in &ser.objects {
            if info.type_id == 1 {
                if let Ok(o) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info) {
                    go_name.insert(info.path_id, o.name().unwrap_or_default());
                }
            }
            if info.type_id == 115 {
                if let Ok(o) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info) {
                    scripts.insert(
                        info.path_id,
                        o.class
                            .properties()
                            .get("m_ClassName")
                            .and_then(|v| v.as_str())
                            .unwrap_or("")
                            .to_string(),
                    );
                }
            }
        }
        for info in &ser.objects {
            if info.type_id == 4 {
                if let Ok(o) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info) {
                    if let Some(g) = o.class.properties().get("m_GameObject").and_then(pptr) {
                        if let Some(n) = go_name.get(&g) {
                            names.insert(info.path_id, n.clone());
                        }
                    }
                }
            }
        }
        for info in ser.objects_of_type(114) {
            let Ok(o) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info) else {
                continue;
            };
            let props = o.class.properties();
            if !props.iter().any(|(_, v)| has_id(v, &kf_ids)) {
                continue;
            }
            let class = props
                .get("m_Script")
                .and_then(pptr)
                .and_then(|id| scripts.get(&id))
                .cloned()
                .unwrap_or_default();
            let keys: Vec<&String> = props
                .iter()
                .filter(|(_, v)| has_id(v, &kf_ids))
                .map(|(k, _)| k)
                .collect();
            eprintln!("KFDB class={class} keys={keys:?}");
            for key in ["m_Root", "Root", "m_RootBone"] {
                if let Some(id) = props.get(key).and_then(pptr) {
                    eprintln!("   {key}={:?}", names.get(&id));
                }
            }
            for key in ["m_Bones", "Bones"] {
                if let Some(UnityValue::Array(a)) = props.get(key) {
                    let mut v: Vec<String> = Vec::new();
                    for x in a.iter().filter_map(pptr) {
                        v.push(names.get(&x).cloned().unwrap_or_else(|| format!("?{x}")));
                    }
                    eprintln!("   {key}({}): {:?}", v.len(), &v[..v.len().min(40)]);
                }
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_card() {
    let p = std::env::var("OK_CARD")
        .unwrap_or_else(|_| crate::test_support::game_path(r"UserData\chara\female\KK_091207.png").to_string_lossy().into_owned());
    let card = crate::card::parse_card(std::path::Path::new(&p)).expect("card");
    eprintln!(
        "CARD head={} detail={} white={} brow={} eyeup={} eyedown={} nose={} pupil={:?} lip={} mole={} cheek={} paint={:?}",
        card.head_id, card.detail_id, card.white_id, card.eyebrow_id, card.eyeline_up_id, card.eyeline_down_id,
        card.nose_id, card.pupil_ids, card.lip_line_id, card.mole_id, card.cheek_id, card.paint_ids
    );
    eprintln!(
        "CARD makeup lip={} lip_color={:?} eyeshadow={} es_color={:?}",
        card.lip_id, card.lip_color, card.eyeshadow_id, card.eyeshadow_color
    );
    eprintln!("CARD abm bones={}", card.abm.len());
    for b in &card.abm {
        let d = b.for_coord(0);
        if !d.is_empty() {
            eprintln!(
                "ABM {} loc={} coords={} scale={:?} len={} pos={:?} rot={:?}",
                b.name, b.location, b.coord.len(), d.scale, d.length, d.pos, d.rot
            );
        }
    }
    eprintln!(
        "CARD eye_color={:?} white_base={:?} white_sub={:?} hl_color={:?} pupilWHXY=({:.3},{:.3},{:.3},{:.3})",
        card.eye_color, card.white_base_color, card.white_sub_color, card.eye_hi_color,
        card.pupil_width, card.pupil_height, card.pupil_x, card.pupil_y
    );
    for (i, h) in card.hair_parts.iter().enumerate() {
        eprintln!(
            "HAIR[{i}] id={} length={} pos={:?} rot={:?} scl={:?}",
            h.id, h.length, h.pos, h.rot, h.scl
        );
    }
    for (i, a) in card.accessory.iter().enumerate() {
        eprintln!(
            "ACC[{i}] type={} id={} parent={:?} pos={:?} rot={:?} scl={:?}",
            a.type_id, a.id, a.parent_key, a.add_pos, a.add_rot, a.add_scale
        );
    }
    eprintln!(
        "CARD clothes={:?} sub={:?} nacc={}",
        card.clothes_ids,
        card.sub_parts_id,
        card.accessory.len()
    );
    eprintln!(
        "CARD colors brow={:?} eyeline={:?} lip={:?} cheek={:?} paint={:?} skin={:?} skin_sub={:?}",
        card.eyebrow_color, card.eyeline_color, card.lip_line_color, card.cheek_color,
        card.paint_colors, card.skin_color, card.skin_sub_color
    );
    eprintln!(
        "CARD mole_id={} mole_layout={:?} mole_color={:?} paint_layouts={:?} hl_up={} hl_down={} pupils={:?}",
        card.mole_id,
        card.mole_layout,
        card.mole_color,
        card.paint_layouts,
        card.hl_up_id,
        card.hl_down_id,
        card.pupils
    );
    eprintln!("CARD clothes_colors={:?}", card.clothes_colors);
    for (i, h) in card.hair_parts.iter().enumerate() {
        eprintln!("HAIRCOL[{i}] {:?}", h.color);
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn extract_outfit3_for_compare() {
    let game = crate::test_support::game_path(r"");
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    let out = std::path::PathBuf::from(
        r"C:\portable\OpenKoikatsu\Saved\Converted\_user_check_o3",
    );
    let _ = std::fs::create_dir_all(&out);
    let r = crate::extract_card_outfit(&game, Some(&card), &out, 3);
    eprintln!("EXTRACTO3 {r:?}");
}

#[test]
fn probe_zipmod_lists() {
    let paths = [
        r"mods\Sideloader Modpack\[musisi]Hundosi_k.zipmod",
        r"mods\Sideloader Modpack\5896\[5896]Sailor Mini.zipmod",
    ];
    for ps in paths {
        let p = crate::test_support::game_path(ps);
        if !p.ok_is_file() {
            eprintln!("ZIPLIST missing {ps}");
            continue;
        }
        let files = crate::mods::zipmod_list_files(&p);
        eprintln!(
            "ZIPLIST {ps} files={:?}",
            files.iter().map(|(n, b)| format!("{n}:{}", b.len())).collect::<Vec<_>>()
        );
        for (n, b) in &files {
            let es = crate::list::load_entries_from_bytes(n, b);
            eprintln!("ZIPLIST parsed {n} -> {} entries", es.len());
            for e in es.iter().take(4) {
                eprintln!("  entry cat={} id={} data={}", e.category_no, e.id, e.main_data);
            }
        }
        let cat = crate::list::catalog_from_zipmod(&p);
        eprintln!(
            "ZIPLIST cat106/141={:?}",
            cat.get_slot(106, 141, 0).map(|e| (e.name.clone(), e.main_data.clone()))
        );
        eprintln!(
            "ZIPLIST cat108/9663={:?}",
            cat.get_slot(108, 9663, 0).map(|e| (e.name.clone(), e.main_data.clone()))
        );
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_card_outfits() {
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    for outfit in 0..4 {
        let c = match crate::card::parse_card_outfit(&card, outfit) {
            Ok(c) => c,
            Err(e) => {
                eprintln!("CARDOUT {outfit} err {e}");
                continue;
            }
        };
        eprintln!(
            "CARDOUT {outfit} clothes={:?} sub={:?}",
            c.clothes_ids, c.sub_parts_id
        );
        for r in &c.resolves {
            if r.category_no >= 105 && r.category_no <= 130
                || r.category_no >= 200 && r.category_no <= 212
            {
                eprintln!(
                    "  resolve prop='{}' cat={} slot={} local={} mod='{}'",
                    r.property, r.category_no, r.slot, r.local_slot, r.mod_id
                );
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_extract_outfits() {
    let game = crate::test_support::game_path(r"");
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    let outfits: Vec<i32> = std::env::var("OK_OUTFITS")
        .ok()
        .and_then(|s| {
            Some(
                s.split(',')
                    .filter_map(|x| x.trim().parse::<i32>().ok())
                    .collect(),
            )
        })
        .unwrap_or_else(|| vec![4, 5, 6]);
    for outfit in outfits {
        let out = std::path::PathBuf::from(format!(
            r"C:\portable\OpenKoikatsu\Saved\Converted\_user_check{outfit}"
        ));
        match crate::extract_card_outfit(&game, Some(&card), &out, outfit) {
            Ok(p) => eprintln!("EXTRACT outfit={outfit} -> {}", p.display()),
            Err(e) => eprintln!("EXTRACT outfit={outfit} err={e}"),
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_session_thigh() {
    let game = crate::test_support::game_path(r"");
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    for outfit in [0, 5, 6] {
        let Ok(mut s) = crate::session::open(&game, Some(&card), outfit) else {
            continue;
        };
        let body = s.shape_body.clone();
        let face = s.shape_face.clone();
        s.apply_shape(&body, &face);
        if let Some(sk) = s.body_skel.as_ref() {
            for n in ["cf_j_thigh00_R", "cf_j_leg03_R", "a_n_heel_R"] {
                eprintln!(
                    "SESSION outfit={outfit} {n} local={:?} rot={:?} world={:?}",
                    sk.tfs.get(&sk.tfs.iter().find(|(_, t)| t.name == n).map(|(id, _)| *id).unwrap_or(0)).map(|t| t.pos),
                    sk.tfs.get(&sk.tfs.iter().find(|(_, t)| t.name == n).map(|(id, _)| *id).unwrap_or(0)).map(|t| t.rot),
                    sk.world_tf(n).map(|m| crate::pose::mat_pos(&m)),
                );
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_acc_parents() {
    let game = crate::test_support::game_path(r"");
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    let outfits: Vec<i32> = std::env::var("OK_OUTFITS")
        .ok()
        .and_then(|s| {
            Some(
                s.split(',')
                    .filter_map(|x| x.trim().parse::<i32>().ok())
                    .collect(),
            )
        })
        .unwrap_or_else(|| vec![5, 6]);
    for outfit in outfits {
        eprintln!("ACCPROBE === outfit {outfit} ===");
        match crate::extract_session(&game, Some(&card), outfit, None) {
            Ok(s) => eprintln!("ACCPROBE outfit {outfit} parts={}", s.parts.len()),
            Err(e) => eprintln!("ACCPROBE outfit {outfit} err={e}"),
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_outfit_acc() {
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    for outfit in 0..8 {
        let Ok(ci) = crate::card::parse_card_outfit(&card, outfit) else {
            continue;
        };
        eprintln!(
            "ACCINFO outfit={outfit} coords={} nacc={}",
            ci.coordinate_count,
            ci.accessory.len()
        );
        for (i, a) in ci.accessory.iter().enumerate() {
            if a.id == 0 {
                continue;
            }
            let pk = if a.parent_key.is_empty() {
                "-".to_string()
            } else {
                a.parent_key.clone()
            };
            eprintln!(
                "  [{i:>2}] type={:<3} id={:<7} parent={:<14} col={:?} col2={:?} col3={:?} col4={:?}",
                a.type_id, a.id, pk, a.color, a.color2, a.color3, a.color4
            );
        }
        for r in ci
            .resolves
            .iter()
            .filter(|r| (120..=130).contains(&r.category_no))
        {
            eprintln!(
                "  RES cat={} slot={} local={} guid={} name={} prop={}",
                r.category_no, r.slot, r.local_slot, r.mod_id, r.name, r.property
            );
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_outfits() {
    let game = crate::test_support::game_path(r"");
    let card = std::path::PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    for outfit in 0..4 {
        let s = match crate::extract_session(&game, Some(&card), outfit, None) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("OUTFIT {outfit} error: {e}");
                continue;
            }
        };
        let mut names: Vec<String> = s
            .parts
            .iter()
            .map(|p| format!("{}:{}", p.role, p.name))
            .collect();
        names.sort();
        eprintln!("OUTFIT {outfit} parts={}", s.parts.len());
        for n in names {
            eprintln!("  {n}");
        }
    }
}

#[test]
fn probe_motionik() {
    let path = std::path::PathBuf::from(crate::test_support::game_path(r"\abdata\custom\custompose.unity3d"),
    );
    for (name, bytes) in crate::list::text_assets_from_bundle(&path) {
        if name != "custom_pose" {
            continue;
        }
        let mut p = 0usize;
        let rd_i32 = |b: &[u8], p: &mut usize| -> i32 {
            let v = i32::from_le_bytes([b[*p], b[*p + 1], b[*p + 2], b[*p + 3]]);
            *p += 4;
            v
        };
        let rd_f32 = |b: &[u8], p: &mut usize| -> f32 {
            let v = f32::from_le_bytes([b[*p], b[*p + 1], b[*p + 2], b[*p + 3]]);
            *p += 4;
            v
        };
        let rd_str = |b: &[u8], p: &mut usize| -> String {
            let mut len = 0usize;
            let mut shift = 0u32;
            loop {
                let byte = b[*p];
                *p += 1;
                len |= ((byte & 0x7f) as usize) << shift;
                if byte & 0x80 == 0 {
                    break;
                }
                shift += 7;
            }
            let s = String::from_utf8_lossy(&b[*p..*p + len]).to_string();
            *p += len;
            s
        };
        let nstates = rd_i32(&bytes, &mut p);
        eprintln!("MOTIONIK states={nstates}");
        for _ in 0..nstates {
            let sname = rd_str(&bytes, &mut p);
            let mut parts = Vec::new();
            for _ in 0..4 {
                let sex = rd_i32(&bytes, &mut p);
                let target = rd_str(&bytes, &mut p);
                let wp = rd_f32(&bytes, &mut p);
                let wa = rd_f32(&bytes, &mut p);
                let chein = rd_str(&bytes, &mut p);
                let cw = rd_f32(&bytes, &mut p);
                parts.push((sex, target, wp, wa, chein, cw));
            }
            let nframes = rd_i32(&bytes, &mut p);
            let mut frames = Vec::new();
            for _ in 0..nframes {
                let frame_no = rd_i32(&bytes, &mut p);
                let edit_no = rd_i32(&bytes, &mut p);
                let nshapes = rd_i32(&bytes, &mut p);
                let mut shapes = Vec::new();
                for _ in 0..nshapes {
                    let sn = rd_i32(&bytes, &mut p);
                    let mut small = [0f32; 6];
                    let mut large = [0f32; 6];
                    for k in 0..6 {
                        small[k] = rd_f32(&bytes, &mut p);
                    }
                    for k in 0..6 {
                        large[k] = rd_f32(&bytes, &mut p);
                    }
                    shapes.push((sn, small, large));
                }
                frames.push((frame_no, edit_no, shapes));
            }
            if sname == "Stand_D_00" || std::env::var("OK_MOTIONIK_ALL").is_ok() {
                eprintln!("MOTIONIK state={sname}");
                for (i, (sex, target, wp, wa, chein, cw)) in parts.iter().enumerate() {
                    eprintln!(
                        "MOTIONIK   part{i} sex={sex} target={target:?} weightPos={wp} weightAng={wa} chein={chein:?} bendWeight={cw}"
                    );
                }
                let used: Vec<_> = frames
                    .iter()
                    .filter(|(_, _, sh)| !sh.is_empty())
                    .collect();
                eprintln!("MOTIONIK   frames={} nonempty={}", frames.len(), used.len());
                if sname == "Stand_D_00" {
                    for (no, edit, shapes) in frames.iter() {
                        if shapes.is_empty() {
                            continue;
                        }
                        eprintln!("MOTIONIK   frame no={no} edit={edit} shapes={}", shapes.len());
                        for (sn, small, large) in shapes.iter() {
                            eprintln!(
                                "MOTIONIK     shape={sn} small={small:?} large={large:?}"
                            );
                        }
                    }
                }
            }
        }
    }
}

#[test]
fn probe_components() {
    let path = std::env::var("OK_PROBE_BUNDLE").unwrap_or_else(|_| {
        crate::test_support::game_path(r"abdata\chara\oo_base.unity3d").to_string_lossy().into_owned()
    });
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle(std::path::Path::new(&path))
    else {
        return;
    };
    let _ = bundle.data_checked();
    use std::collections::HashMap;
    let pptr = |v: &unity_asset_core::UnityValue| -> Option<i64> {
        match v {
            unity_asset_core::UnityValue::Object(o) => o
                .get("m_PathID")
                .or_else(|| o.get("pathID"))
                .and_then(|x| x.as_i64()),
            unity_asset_core::UnityValue::Integer(i) => Some(*i),
            _ => None,
        }
    };
    fn walk(
        v: &unity_asset_core::UnityValue,
        depth: usize,
        out: &mut String,
    ) {
        use unity_asset_core::UnityValue;
        if depth > 6 {
            out.push_str("...");
            return;
        }
        match v {
            UnityValue::Object(o) => {
                out.push('{');
                for (k, vv) in o.iter() {
                    out.push_str(k);
                    out.push(':');
                    walk(vv, depth + 1, out);
                    out.push(',');
                }
                out.push('}');
            }
            UnityValue::Array(a) => {
                out.push('[');
                for vv in a.iter().take(32) {
                    walk(vv, depth + 1, out);
                    out.push(',');
                }
                if a.len() > 32 {
                    out.push_str("...");
                }
                out.push(']');
            }
            other => out.push_str(&format!("{other:?}")),
        }
    }
    let filter: Vec<String> = std::env::var("OK_COMP_FILTER")
        .unwrap_or_else(|_| "FullBodyBipedIK,FrameCorrect,IKCorrect".to_string())
        .split(',')
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(fb) else {
            continue;
        };
        let mut scripts: HashMap<i64, String> = HashMap::new();
        for info in ser.objects_of_type(115) {
            if let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info) {
                let class = obj
                    .class
                    .properties()
                    .get("m_ClassName")
                    .or_else(|| obj.class.properties().get("m_Name"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                scripts.insert(info.path_id, class);
            }
        }
        if std::env::var("OK_COMP_DIAG").is_ok() {
            let mut names: Vec<String> = scripts.values().cloned().collect();
            names.sort();
            names.dedup();
            eprintln!(
                "COMPDIAG file={file:?} scripts={} known={}",
                ser.objects_of_type(115).len(),
                scripts.len()
            );
            eprintln!("COMPDIAG names={names:?}");
            eprintln!("COMPDIAG mono114={}", ser.objects_of_type(114).len());
        }
        for info in ser.objects_of_type(114) {
            let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info) else {
                continue;
            };
            let props = obj.class.properties();
            let class = props
                .get("m_Script")
                .and_then(|v| pptr(v))
                .and_then(|id| scripts.get(&id))
                .cloned()
                .unwrap_or_default();
            if !filter.iter().any(|f| class.contains(f.as_str())) {
                continue;
            }
            eprintln!(
                "COMPONENT class={class} obj={} pathid={}",
                obj.name().unwrap_or_default(),
                info.path_id
            );
            for (k, v) in props.iter() {
                let mut s = String::new();
                walk(v, 0, &mut s);
                eprintln!("  {k} = {s}");
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_shoe_textures() {
    let path = &crate::test_support::game_path(r"\abdata\chara\co_shoes_00.unity3d");
    if !path.ok_is_file() {
        eprintln!("SHOE no bundle");
        return;
    }
    let bytes = crate::gamefs::read(path).unwrap();
    for n in [
        "cf_shoes_summer",
        "cf_shoes_summer_low",
        "cf_shoes_summer_mc",
        "cf_shoes_summer_mc_low",
        "cf_shoes_summer_md",
    ] {
        match crate::unity::named_png_from_bytes(bytes.clone(), &[n]) {
            Some((name, png)) => {
                let out = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\shoe_{n}.png");
                let _ = std::fs::write(&out, &png);
                let mean = crate::teximg::load_from_memory(&png).ok().map(|i| {
                    let im = i.to_rgba8();
                    let n = im.pixels().len() as f64;
                    let s: (f64, f64, f64) = im.pixels().fold((0.0, 0.0, 0.0), |a, p| {
                        (a.0 + p.0[0] as f64, a.1 + p.0[1] as f64, a.2 + p.0[2] as f64)
                    });
                    (s.0 / n, s.1 / n, s.2 / n)
                });
                eprintln!("SHOE {n} -> {name} mean={mean:?}");
            }
            None => eprintln!("SHOE {n} NOT FOUND"),
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_pants_textures() {
    let zip = std::path::Path::new(crate::test_support::game_file(r"\mods\Sideloader Modpack\hayashi\[hayashi]Open Front Pants v1.0.zipmod"),
    );
    if !zip.ok_is_file() {
        eprintln!("PANTS no zip");
        return;
    }
    let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(zip, "chara/Hayashi/hayashi_pants.unity3d")
    else {
        eprintln!("PANTS no bundle");
        return;
    };
    for n in [
        "cf_bot_pantshot_t",
        "cf_bot_pantshot_t_low",
        "cf_bot_pantshot_mc",
        "cf_bot_pantshot_mc_low",
        "cf_bot_pantshot_md",
    ] {
        match crate::unity::named_png_from_bytes(bytes.clone(), &[n]) {
            Some((name, png)) => {
                let out = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\pants_{n}.png");
                let _ = std::fs::write(&out, &png);
                let mean = crate::teximg::load_from_memory(&png)
                    .ok()
                    .map(|i| {
                        let im = i.to_rgba8();
                        let n = im.pixels().len() as f64;
                        let s: (f64, f64, f64) = im.pixels().fold((0.0, 0.0, 0.0), |a, p| {
                            (a.0 + p.0[0] as f64, a.1 + p.0[1] as f64, a.2 + p.0[2] as f64)
                        });
                        (s.0 / n, s.1 / n, s.2 / n)
                    });
                eprintln!("PANTS {n} -> {name} mean={mean:?}");
            }
            None => eprintln!("PANTS {n} NOT FOUND"),
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn female_folder_face_tex() {
    let game = crate::test_support::game_path(r"");
    let dir = game.join(r"UserData\chara\female");
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mod_catalog = crate::list::load_mods_catalog(&game.join("mods"));
    let head_bytes = crate::gamefs::read(game.join("abdata/chara/bo_head_00.unity3d")).unwrap_or_default();
    let head_texs = crate::unity::parse_bundle_cached(head_bytes).textures_keep_base();
    let body_texs: Vec<(String, Vec<u8>)> = Vec::new();
    let mut cards = Vec::new();
    let mut wanted = std::collections::HashSet::new();
    for ent in crate::gamefs::read_dir(&dir).unwrap().flatten() {
        let path = ent.path();
        if !path.ok_is_file() {
            continue;
        }
        if !path
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.eq_ignore_ascii_case("png"))
            .unwrap_or(false)
        {
            continue;
        }
        let Ok(info) = crate::card::parse_card(&path) else {
            eprintln!("PARSE FAIL {}", path.display());
            continue;
        };
        for r in &info.resolves {
            if r.category_no == 100 {
                wanted.insert(r.mod_id.clone());
            }
        }
        cards.push((path, info));
    }
    cards.sort_by(|a, b| a.0.cmp(&b.0));
    let guids = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
    let mut zip_lists = std::collections::HashMap::new();
    let mut missing = 0;
    let mut black = 0;
    let mut ok = 0;
    let mut lines = Vec::new();
    for (path, info) in &cards {
        let entry = resolve_face_entry(&vanilla, info, &guids, &mut zip_lists);
        let png = compose_face_texture(
            &game,
            &vanilla,
            &mod_catalog,
            info,
            &head_texs,
            &body_texs,
            info.skin_color,
            info.skin_sub_color,
            0,
            &guids,
        );
        let mean = png.as_ref().and_then(|b| png_mean(b));
        let name = path.file_name().and_then(|s| s.to_str()).unwrap_or("?");
        let head_res = info.resolves.iter().find(|r| r.category_no == 100);
        let line = match (&png, mean) {
            (None, _) => {
                missing += 1;
                format!(
                    "MISSING {name} headId={} resolve={:?} entry={:?}",
                    info.head_id,
                    head_res.map(|r| (r.mod_id.as_str(), r.slot, r.local_slot, r.property.as_str())),
                    entry.as_ref().map(|e| (e.name.as_str(), e.main_ab.as_str(), e.main_tex.as_str()))
                )
            }
            (Some(b), Some(m)) if m[0] < 0.08 && m[1] < 0.08 && m[2] < 0.08 => {
                black += 1;
                format!(
                    "BLACK {name} headId={} bytes={} mean={m:?} entry={:?}",
                    info.head_id,
                    b.len(),
                    entry.as_ref().map(|e| e.name.as_str())
                )
            }
            (Some(b), Some(m)) => {
                ok += 1;
                format!(
                    "OK {name} headId={} bytes={} mean=({:.2},{:.2},{:.2}) entry={} ab={}",
                    info.head_id,
                    b.len(),
                    m[0],
                    m[1],
                    m[2],
                    entry.as_ref().map(|e| e.name.as_str()).unwrap_or("none"),
                    entry.as_ref().map(|e| e.main_ab.as_str()).unwrap_or("")
                )
            }
            _ => {
                missing += 1;
                format!("BADPNG {name}")
            }
        };
        if name.contains("KK_384283")
            || name.contains("KK_226176")
            || name.contains("KK_398022")
            || name.contains("KK_398303")
            || name.contains("KK_091207")
        {
            if let Some(b) = &png {
                let safe = name.replace(' ', "_");
                let _ = std::fs::write(
                    format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\face_{safe}"),
                    b,
                );
            }
        }
        lines.push(line);
    }
    let report = format!(
        "{}\nFACE SCAN n={} ok={} missing={} black={}\n",
        lines.join("\n"),
        cards.len(),
        ok,
        missing,
        black
    );
    let _ = std::fs::write(
        r"C:\Users\Damody\AppData\Local\Temp\opencode\female_face_scan.txt",
        &report,
    );
    eprintln!("{report}");
    assert!(cards.len() >= 64, "expected the top-level female folder");
    assert_eq!(missing + black, 0, "some female cards still have a black face");
}

#[test]
fn diag_face_catalog_and_tex() {
    let game = &crate::test_support::game_path(r"");
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mut ids: Vec<i32> = vanilla
        .by_id
        .keys()
        .filter(|(c, _)| *c == 100)
        .map(|(_, id)| *id)
        .collect();
    ids.sort_unstable();
    eprintln!("cat100 count={} ids={:?}", ids.len(), &ids[..ids.len().min(30)]);
    match vanilla.get(100, 1).or_else(|| vanilla.get(100, 0)) {
        Some(e) => eprintln!(
            "cat100 hit name={} main_tex={} mask={} ab={}",
            e.name,
            e.main_tex,
            e.info("ColorMaskTex"),
            e.main_ab
        ),
        None => eprintln!("cat100 id0/1 MISSING"),
    }
    let bytes = crate::gamefs::read(game.join("abdata/chara/bo_head_00.unity3d")).unwrap_or_default();
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let texs = parsed.textures_keep_base();
    let names: Vec<&str> = texs.iter().map(|(n, _)| n.as_str()).filter(|n| {
        let l = n.to_ascii_lowercase();
        l.contains("face") || l.contains("cf_t")
    }).collect();
    eprintln!("face-like texs {}", names.len());
    for n in names.iter().take(40) {
        eprintln!("  {n}");
    }
    for want in ["cf_face_00_t", "cf_face_00_t_low", "cf_m_face_00"] {
        let hit = crate::unity::pick_named_png(&texs, &[want]);
        eprintln!("pick {want} -> {}", hit.as_ref().map(|(n,b)| format!("{n} {}", b.len())).unwrap_or_else(|| "NONE".into()));
    }
}

#[test]
fn blend_alphablend_keyword_is_translucent() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_shorts_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        keywords: vec!["_ALPHABLEND_ON".into(), "_ALPHA_A_ON".into()],
        render_queue: 2350,
        ..Default::default()
    });
    let (b, _) = blend_for_mesh("clothes_shorts", &mesh);
    assert_eq!(b, "translucent");
}

#[test]
fn blend_clothes_without_alphablend_is_masked() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_inner_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        keywords: vec!["_ALPHA_A_ON".into(), "_ALPHA_B_ON".into()],
        render_queue: 2450,
        ..Default::default()
    });
    let (b, _) = blend_for_mesh("clothes_top", &mesh);
    assert_eq!(b, "masked");
}

#[test]
fn blend_render_queue_3000_is_translucent() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_shorts_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        render_queue: 3030,
        ..Default::default()
    });
    let (b, _) = blend_for_mesh("clothes_shorts", &mesh);
    assert_eq!(b, "translucent");
}

#[test]
fn blend_toon_clothes_shader_is_unlit() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_top_sailor06_01_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        shader: "Shader Forge/main_opaque".into(),
        render_queue: 2450,
        ..Default::default()
    });
    let (b, unlit) = blend_for_mesh("clothes_top", &mesh);
    assert_eq!(b, "masked");
    assert!(unlit);
}

#[test]
fn material_editor_normal_blit_sets_red_and_preserves_gba() {
    let mut source = image::RgbaImage::new(1, 2);
    source.put_pixel(0, 0, image::Rgba([32, 90, 120, 140]));
    source.put_pixel(0, 1, image::Rgba([80, 180, 200, 220]));
    let out = convert_runtime_normal_map(encode_png(&source).unwrap());
    let img = crate::teximg::load_from_memory(&out).unwrap().into_rgba8();
    assert_eq!(img.get_pixel(0, 0).0, [255, 180, 200, 220]);
    assert_eq!(img.get_pixel(0, 1).0, [255, 90, 120, 140]);
}

#[test]
fn native_material_editor_normal_rt_encodes_linear_source_rgb() {
    let source=encode_png(&image::RgbaImage::from_pixel(1,1,
        image::Rgba([32,190,192,191]))).unwrap();
    let converted=convert_runtime_normal_map(source);
    let stored=linear_texture_to_srgb_png(&converted).unwrap();
    let pixel=crate::teximg::load_from_memory(&stored).unwrap().into_rgba8();
    assert_eq!(pixel.get_pixel(0,0).0,[255,224,225,191]);
}

#[test]
fn native_material_editor_normal_srgb_source_is_not_encoded_twice() {
    let source=encode_png(&image::RgbaImage::from_pixel(1,1,image::Rgba([32,188,188,73]))).unwrap();
    let linear=convert_runtime_normal_map_sampled(source.clone(),true);
    assert_eq!(crate::teximg::load_from_memory(&linear).unwrap().into_rgba8().get_pixel(0,0).0,[255,128,128,73]);
    let encoded=linear_texture_to_srgb_png(&linear).unwrap();
    assert_eq!(crate::teximg::load_from_memory(&encoded).unwrap().into_rgba8().get_pixel(0,0).0,[255,188,188,73]);
    let wrong=linear_texture_to_srgb_png(&convert_runtime_normal_map_sampled(source,false)).unwrap();
    assert!(crate::teximg::load_from_memory(&wrong).unwrap().into_rgba8().get_pixel(0,0)[1]>210);
}

#[test]
#[ignore="requires explicit audit card; synthetic directional source/overlay control"]
fn body_under_rows_match_native_rt_coordinates() {
    let mut card=crate::card::parse_card(Path::new(&std::env::var("OK_AUDIT_CARD").unwrap())).unwrap();
    card.ksox.clear();card.sex=1;card.nail_color=[1.;4];
    let overlay=image::RgbaImage::from_raw(1,2,vec![73,88,84,255,120,40,180,255]).unwrap();
    card.ksox.insert((0,3),encode_png(&overlay).unwrap());
    let main=crate::unity::solid_png([1.;4],1,2).unwrap();
    let mask=crate::unity::solid_png([0.,0.,0.,1.],1,2).unwrap();
    let mut mesh=ExtractedMesh::default();
    compose_body_texture(&mut mesh,Some(&card),[1.;4],[1.;4],&[("cf_body_00_t".into(),main),("cf_body_00_mc".into(),mask)],0);
    let result=crate::teximg::load_from_memory(mesh.texture_png.as_ref().unwrap()).unwrap().into_rgba8();
    for (actual,expected) in result.pixels().zip(overlay.pixels()) {
        for c in 0..4 {assert!((actual[c] as i16-expected[c] as i16).abs()<=1,"BodyUnder must remain in native RT row coordinates after the body Blit");}
    }
}

#[test]
fn normal_override_keeps_texture_container_rt_rows() {
    let source=image::RgbaImage::from_raw(1,2,vec![255,188,128,73,255,80,220,191]).unwrap();
    let png=encode_png(&source).unwrap();
    let output=convert_runtime_normal_map_rows(png.clone(),true,false);
    let actual=crate::teximg::load_from_memory(&output).unwrap().into_rgba8();
    assert_eq!(actual.get_pixel(0,0).0,[255,128,55,73]);
    assert_eq!(actual.get_pixel(0,1).0,[255,20,183,191]);
    assert_ne!(output,convert_runtime_normal_map_rows(png,true,true));
}
#[test]
#[ignore="explicit installed audit card and output; authored body source diagnostic"]
fn dump_uncensor_body_authored_textures() {
    let game=PathBuf::from(std::env::var("OK_AUDIT_GAME").unwrap());
    let card=crate::card::parse_card(Path::new(&std::env::var("OK_AUDIT_CARD").unwrap())).unwrap();
    let (source,bytes)=crate::mods::uncensor_body_for_card(&game,card.body_guid.as_deref(),card.sex).unwrap();
    let parsed=crate::unity::parse_bundle_ref(&bytes);
    let output=PathBuf::from(std::env::var("OK_AUDIT_OUTPUT").unwrap());std::fs::create_dir_all(&output).unwrap();
    for (tag,name) in [("main",source.main_texture),("mask",source.color_mask)] {
        std::fs::write(output.join(format!("{tag}.png")),parsed.texture_png_named(&name).unwrap()).unwrap();
    }
}

#[test]
fn body_create_preserves_native_skin_alpha() {
    let main=crate::unity::solid_png([1.;4],1,1).unwrap();
    let mask=crate::unity::solid_png([0.;4],1,1).unwrap();
    let output=crate::unity::apply_body_create_colors(&main,&mask,[1.,1.,1.,236./255.],[1.;4],[1.;4]).unwrap();
    assert_eq!(crate::teximg::load_from_memory(&output).unwrap().into_rgba8().get_pixel(0,0)[3],236);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn body_base_normal_matches_game_render_texture() {
    let game = &crate::test_support::game_path(r"");
    let bundle = game.join("abdata/chara/mm_base.unity3d");
    let reference = game.join("mesh_dump/KK_091207.png/outfit0-tpose/Ushiwakamaru_0/tex_18_cf_m_body__NormalMap.png");
    if !bundle.ok_exists() || !reference.ok_exists() { return; }
    let (_, raw) = crate::unity::named_png_from_bundle(&bundle, &["cf_body_00_base_n"])
        .expect("body base normal");
    let converted = convert_runtime_normal_map(raw);
    let got = crate::teximg::load_from_memory(&converted).unwrap().into_rgba8();
    let expected = image::open(reference).unwrap().to_rgba8();
    assert_eq!(got.dimensions(), expected.dimensions());
    let mut error = [0u64; 4];
    for (a, b) in got.pixels().zip(expected.pixels()) {
        for c in 0..4 { error[c] += a[c].abs_diff(b[c]) as u64; }
    }
    let n = got.width() as u64 * got.height() as u64;
    assert!(error.iter().all(|e| *e < n), "normal map channel error: {error:?}");
}

#[test]
fn face_blit_uses_measured_unity_srgb_transfer() {
    // March's decoded cf_face_00_t mouth texel and the same texel read
    // back from the live Maker texMain RT on 2026-09-27.
    let input = encode_png(&image::RgbaImage::from_pixel(1, 1,
        image::Rgba([75, 28, 28, 255]))).unwrap();
    let output = linear_texture_to_srgb_png(&input).unwrap();
    let pixel = crate::teximg::load_from_memory(&output).unwrap().into_rgba8();
    assert_eq!(pixel.get_pixel(0, 0).0, [148, 93, 93, 255]);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn face_source_color_space_comes_from_matching_bundle_texture() {
    let game = &crate::test_support::game_path(r"");
    let bundle = game.join("abdata/chara/bo_head_00.unity3d");
    if !bundle.ok_exists() { return; }
    let (_, png) = crate::unity::named_png_from_bundle(&bundle, &["cf_face_00_t"])
        .expect("vanilla face source");
    assert_eq!(face_base_color_space(game, None, "cf_face_00_t", &png), Some(0));
    let (_, _, ramp_space) = crate::unity::named_png_with_color_space_from_bundle(
        &bundle, &["Ramp_StateLine60"]).expect("gamma-space texture in same bundle");
    assert_eq!(ramp_space, Some(1));
    let mut altered = png.clone();
    altered.push(0);
    assert_eq!(face_base_color_space(game, None, "cf_face_00_t", &altered), None);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn configured_minimalist_ramp_is_loaded_from_game_settings() {
    let game = &crate::test_support::game_path(r"");
    if !game.join("UserData/config/system.xml").ok_exists() { return; }
    let vanilla = load_characustom_dir(&game.join("abdata/list/characustom"));
    let ramp = configured_ramp_png(game, &vanilla).expect("configured mt_ramp");
    let image = crate::teximg::load_from_memory(&ramp).unwrap().into_rgba8();
    assert_eq!((image.width(), image.height()), (256, 4));
    assert_eq!(image.get_pixel(0, 2)[0], 0);
    assert_eq!(image.get_pixel(128, 2)[0], 255);
}

#[test]
fn material_editor_top_subpart_uses_top_clothes_slot() {
    assert_eq!(me_slot("clothes_top", Some("outfit0.clothesTopParts1")), Some(0));
    assert_eq!(me_slot("clothes_bot", Some("outfit0.clothes1")), Some(1));
    let game = &crate::test_support::game_path(r"");
    let card_path = game.join(r"UserData\chara\female\Hayase_yuuka_keiyuu_V1_02 For KKS.png");
    if !card_path.ok_exists() { return; }
    let card = crate::card::parse_card_outfit(&card_path, 0).unwrap();
    let mesh = crate::unity::ExtractedMesh {
        mat: Some(crate::unity::MeshMaterial {
            name: "cf_m_top_jacket01 (Instance) (Instance)".into(),
            ..Default::default()
        }),
        ..Default::default()
    };
    let png = me_maintex(Some(&card.me_tex), 0, "clothes_top", Some("outfit0.clothesTopParts1"), &mesh);
    let image = crate::teximg::load_from_memory(png.expect("top subpart MainTex override")).unwrap();
    assert_eq!((image.width(), image.height()), (4096, 4096));
    let shadow = me_shadow_color(Some(&card.me_color), 0, "clothes_top", Some("outfit0.clothesTopParts1"), &mesh).unwrap();
    assert!((shadow[0] - 0.9).abs() < 0.001);
}

#[test]
fn clothes_alpha_state_follows_change_alpha_mask() {
    let main = encode_png(&image::RgbaImage::from_pixel(1, 1, image::Rgba([80, 90, 100, 255]))).unwrap();
    let mask = encode_png(&image::RgbaImage::from_pixel(1, 1, image::Rgba([0, 128, 0, 255]))).unwrap();
    let alpha = |bytes: Vec<u8>| crate::teximg::load_from_memory(&bytes).unwrap().into_rgba8().get_pixel(0, 0)[3];
    let channels = |bytes: Vec<u8>| { let p = *crate::teximg::load_from_memory(&bytes).unwrap().into_rgba8().get_pixel(0, 0); [p[0], p[1]] };
    for (state, expected, expected_channels) in [(0, 0, [0, 128]), (1, 128, [255, 128]), (2, 128, [255, 128]), (3, 255, [255, 255])] {
        let flags = clothes_alpha_state(state);
        assert_eq!(channels(body_mask_for_alpha_state(&mask, flags).unwrap()), expected_channels);
        assert_eq!(alpha(apply_clothes_over_mask(&main, &mask, 1.0, 0.0, flags).unwrap()), expected);
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn invalid_card_preserves_previous_export() {
    let game = &crate::test_support::game_path(r"");
    if !game.join("abdata").ok_is_dir() { return; }
    let unique = format!("ok_invalid_card_{}_{}", std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos());
    let out = std::env::temp_dir().join(unique);
    std::fs::create_dir(&out).unwrap();
    let existing = out.join("previous.obj");
    std::fs::write(&existing, b"existing export").unwrap();
    let missing = out.join("missing-card.png");
    let result = extract_session(game, Some(&missing), 0, Some(&out));
    assert!(result.is_err(), "missing card must fail");
    assert_eq!(crate::gamefs::read(&existing).unwrap(), b"existing export");
    std::fs::remove_file(existing).unwrap();
    std::fs::remove_dir(out).unwrap();
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn material_editor_hair_alpha_mask_multiplies_main_alpha() {
    let card_path = &crate::test_support::game_path(r"\UserData\chara\female\KK_384283.png");
    if !card_path.ok_exists() { return; }
    let card = crate::card::parse_card_outfit(card_path, 0).unwrap();
    let mesh = crate::unity::ExtractedMesh {
        mat: Some(crate::unity::MeshMaterial {
            name: "cf_m_hair_f_20_00 (Instance)".into(),
            shader: "Shader Forge/main_hair_front".into(),
            ..Default::default()
        }),
        ..Default::default()
    };
    let mask = me_texture(Some(&card.me_tex), 0, "accessory", Some("accessory11"), &mesh, "alphamask")
        .expect("K384 ME alpha mask");
    let mask_image = crate::teximg::load_from_memory(mask).unwrap().into_rgba8();
    assert_eq!(mask_image.get_pixel(250, 400)[0], 0);
    assert_eq!(mask_image.get_pixel(100, 400)[0], 255);
    let main = crate::unity::solid_png([1.0, 1.0, 1.0, 1.0], 512, 512).unwrap();
    let masked = apply_hair_alpha_mask(&main, mask).unwrap();
    let image = crate::teximg::load_from_memory(&masked).unwrap().into_rgba8();
    assert_eq!(image.get_pixel(250, 400)[3], 0);
    assert_eq!(image.get_pixel(100, 400)[3], 255);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn material_editor_face_maintex_matches_draw_material() {
    let card_path = &crate::test_support::game_path(r"\UserData\chara\female\Alice_keiyuu.png");
    if !card_path.ok_exists() { return; }
    let card = crate::card::parse_card_outfit(card_path, 0).unwrap();
    let mesh = crate::unity::ExtractedMesh {
        name: "cf_O_face".into(),
        ..Default::default()
    };
    let png = me_maintex(Some(&card.me_tex), 0, "head", None, &mesh)
        .expect("cf_m_face_00 MainTex replacement");
    let image = crate::teximg::load_from_memory(png).unwrap();
    assert_eq!((image.width(), image.height()), (1024, 1024));
    assert!(me_maintex(Some(&card.me_tex), 0, "head", None,
        &crate::unity::ExtractedMesh { name: "cf_O_eyeline_low".into(), ..Default::default() })
        .is_none());
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn material_editor_face_detailmask_matches_runtime_draw_texture() {
    let card_path = &crate::test_support::game_path(r"\UserData\chara\female\KK_384283.png");
    let dump_path = &crate::test_support::game_path(r"\mesh_dump\KK_384283.png\outfit0-tpose\不知火 舞_0\tex_10_cf_m_face_00__DetailMask.png");
    if !card_path.ok_exists() || !dump_path.ok_exists() { return; }
    let card = crate::card::parse_card_outfit(card_path, 0).unwrap();
    let mesh = crate::unity::ExtractedMesh { name: "cf_O_face".into(), ..Default::default() };
    let png = me_texture(Some(&card.me_tex), 0, "head", None, &mesh, "detailmask")
        .expect("face draw material DetailMask override");
    let source = crate::teximg::load_from_memory(png).unwrap().into_rgba8();
    let runtime = image::open(dump_path).unwrap().to_rgba8();
    assert_eq!(source.dimensions(), runtime.dimensions());
    let mut differing = 0usize;
    for (a, b) in source.pixels().zip(runtime.pixels()) {
        if a[0] != b[0] || a[1] != b[1] { differing += 1; }
    }
    assert!(differing < 4000, "face mask red/green must match the game without flipping: {differing} pixels");
}

#[test]
fn unresolved_hair_and_studio_accessory_have_distinct_blends() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "accessory_mesh".into(),
        mat: Some(crate::unity::MeshMaterial {
            render_queue: 2450,
            ..Default::default()
        }),
        ..Default::default()
    };
    assert_eq!(blend_for_mesh("accessory", &mesh), ("translucent".into(), true));
    mesh.hair_shader_hint = true;
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
    mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_hair_front".into();
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
    mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_hair".into();
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
    mesh.mat.as_mut().unwrap().render_queue = 3000;
    mesh.mat.as_mut().unwrap().keywords = vec!["_ALPHABLEND_ON".into()];
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
    mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_hair_front".into();
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
    mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_item_studio_alpha".into();
    assert_eq!(blend_for_mesh("accessory", &mesh), ("translucent".into(), true));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn material_editor_runtime_shader_controls_accessory_blend() {
    let path = &crate::test_support::game_path(r"\UserData\chara\female\[Card] March_7th_keiyuu.png");
    if !path.ok_exists() { return; }
    let card = crate::card::parse_card_outfit(path, 0).expect("March card");
    let mut mesh = crate::unity::ExtractedMesh {
        mat: Some(crate::unity::MeshMaterial {
            name: "hair (Instance)".into(),
            shader_has_rim: Some(false),
            ..Default::default()
        }),
        ..Default::default()
    };
    apply_me_shader(&crate::test_support::game_path(r""), Some(&card.me_shader), 0, "accessory", Some("accessory2"), &mut mesh);
    let material = mesh.mat.as_ref().unwrap();
    assert_eq!(material.shader, "Shader Forge/main_hair_front");
    assert_eq!(material.shader_has_rim, Some(true));
    assert_eq!(material.render_queue, 2475);
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
    apply_me_shader(&crate::test_support::game_path(r""), Some(&card.me_shader), 0, "accessory", Some("accessory0"), &mut mesh);
    assert_eq!(mesh.mat.as_ref().unwrap().shader, "Shader Forge/main_hair");
    assert_eq!(blend_for_mesh("accessory", &mesh), ("masked".into(), true));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn accessory_colormask_mixed_source_rows() {
    let path = &crate::test_support::game_path("/UserData/chara/female/[Card] March_7th_keiyuu.png");
    let mut card = crate::card::parse_card_outfit(path, 0).expect("installed card fixture");
    card.me_copies.clear();
    card.me_shader.clear();
    card.me_color.clear();
    card.me_float.clear();
    card.hair_acc.clear();
    card.accessory[0].color = [1., 0., 0., 1.];
    card.accessory[0].color2 = [0., 1., 0., 1.];
    card.accessory[0].color3 = [0., 0., 1., 1.];
    // Independent UV control: upper texel is red-channel tinted, lower
    // texel green-channel tinted, regardless of which source owns either.
    let main = encode_png(&image::RgbaImage::from_raw(1, 2,
        vec![200, 100, 50, 255, 80, 160, 240, 255]).unwrap()).unwrap();
    let mask = encode_png(&image::RgbaImage::from_raw(1, 2,
        vec![255, 0, 0, 255, 0, 255, 0, 255]).unwrap()).unwrap();
    for main_from_me in [true, false] {
        for mask_from_me in [true, false] {
            card.me_tex.clear();
            for (key, from_me, bytes) in [("maintex", main_from_me, &main), ("colormask", mask_from_me, &mask)] {
                if from_me {
                    card.me_tex.insert((2, 0, 0, "fixture_material".into(), key.into()),
                        std::sync::Arc::new(bytes.clone()));
                }
            }
            let mut meshes = vec![crate::unity::ExtractedMesh {
                name: "fixture_mesh".into(),
                texture_png: Some(flip_png_v(&main).unwrap()),
                color_mask_png: Some(flip_png_v(&mask).unwrap()),
                mat: Some(crate::unity::MeshMaterial {
                    name: "fixture_material".into(),
                    shader: "Shader Forge/main_item".into(),
                    ..Default::default()
                }),
                ..Default::default()
            }];
            color_accessory_meshes(Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MixedTextureSource"),
                None, &ListEntry::default(), &card, "accessory0", 0, &mut meshes, &[], &[], None);
            let mut output = meshes[0].texture_png.clone().unwrap();
            // Match the render boundary's source orientation convention.
            if !main_from_me { output = flip_png_v(&output).unwrap(); }
            let actual = crate::teximg::load_from_memory(&output).unwrap().into_rgba8();
            // The recovered native fragment performs ColorMask mixing.
            // Preserve source rows/bytes and separate uniforms, rather
            // than requiring the obsolete CPU pre-tint (double coloring).
            assert_eq!(actual.as_raw(), crate::teximg::load_from_memory(&main).unwrap().into_rgba8().as_raw());
            let mut output_mask=meshes[0].color_mask_png.clone().unwrap();
            if !mask_from_me {output_mask=flip_png_v(&output_mask).unwrap();}
            assert_eq!(output_mask, mask, "mask provenance main_me={main_from_me} mask_me={mask_from_me}");
            for (name,expected) in [("_Color",card.accessory[0].color),("_Color2",card.accessory[0].color2),("_Color3",card.accessory[0].color3)] {
                assert_eq!(mat_color(&meshes[0],name),expected,"separate shader color {name}");
            }
        }
    }
}

#[test]
fn me_colormask_targets_only_its_material() {
    let mut map = crate::card::MeTexMap::new();
    map.insert(
        (2, 0, 5, "cf_m_hair_f_54".into(), "colormask".into()),
        std::sync::Arc::new(vec![1, 2, 3]),
    );
    let mut mesh = crate::unity::ExtractedMesh {
        mat: Some(crate::unity::MeshMaterial {
            name: "cf_m_hair_f_54 (Instance)".into(),
            ..Default::default()
        }),
        ..Default::default()
    };
    assert_eq!(
        me_texture(Some(&map), 0, "acc", Some("accessory5"), &mesh, "colormask"),
        Some(&[1, 2, 3][..])
    );
    mesh.mat.as_mut().unwrap().name = "other_material".into();
    assert!(me_texture(Some(&map), 0, "acc", Some("accessory5"), &mesh, "colormask").is_none());
}

#[test]
fn me_maintex_fallback_uses_renderer_hair_classification() {
    let mut map = crate::card::MeTexMap::new();
    map.insert(
        (2, 0, 5, "source_material".into(), "maintex".into()),
        std::sync::Arc::new(vec![4, 5, 6]),
    );
    let mut mesh = crate::unity::ExtractedMesh {
        name: "plain_mesh".into(),
        mat: Some(crate::unity::MeshMaterial::default()),
        ..Default::default()
    };
    assert!(me_maintex(Some(&map), 0, "acc", Some("accessory5"), &mesh).is_none());
    mesh.hair_shader_hint = true;
    assert_eq!(
        me_maintex(Some(&map), 0, "acc", Some("accessory5"), &mesh),
        Some(&[4, 5, 6][..])
    );
}

#[test]
fn blend_main_clothes_opaque_is_unlit() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_bra_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        shader: "Koikano/main_clothes_opaque".into(),
        keywords: vec!["_COLORMASKUSE_COLOROFF".into()],
        render_queue: 2350,
        ..Default::default()
    });
    let (_, unlit) = blend_for_mesh("clothes_bra", &mesh);
    assert!(unlit);
}

#[test]
fn blend_alphatest_keyword_is_masked() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_shorts_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        keywords: vec!["_ALPHATEST_ON".into()],
        render_queue: 2450,
        ..Default::default()
    });
    let (b, _) = blend_for_mesh("clothes_shorts", &mesh);
    assert_eq!(b, "masked");
}

#[test]
fn xukmi_plus_shaders_map_to_their_shader_forge_family() {
    assert_eq!(shader_family("xukmi/MainItemStudioAlphaPlus"), "shader forge/main_item_studio_alpha");
    assert_eq!(shader_family("xukmi/MainItemPlus"), "shader forge/main_item");
    assert_eq!(shader_family("Shader Forge/main_alpha"), "shader forge/main_alpha");
    let mesh = crate::unity::ExtractedMesh {
        name: "shadow".into(),
        mat: Some(crate::unity::MeshMaterial { shader: "xukmi/MainItemStudioAlphaPlus".into(), render_queue: 2500, ..Default::default() }),
        ..Default::default()
    };
    // A hair-shadow overlay in a hair mod: translucent like the shader it replaces, whatever the role.
    assert_eq!(blend_for_mesh("hair_front", &mesh).0, "translucent");
}

#[test]
fn blend_main_alpha_shader_is_translucent() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_shorts_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        shader: "Shader Forge/main_alpha".into(),
        render_queue: 2450,
        ..Default::default()
    });
    for role in ["clothes_shorts","accessory","head"] {
        for queue in [-1,2450,3000] {
            mesh.mat.as_mut().unwrap().render_queue=queue;
            assert_eq!(blend_for_mesh(role,&mesh),("translucent".into(),true),"{role} queue={queue}");
        }
    }
}

#[test]
fn blend_clothes_role_is_unlit_even_without_shader() {
    let mesh = crate::unity::ExtractedMesh {
        name: "o_top_sailor06_01_a".into(),
        ..Default::default()
    };
    let (_, unlit) = blend_for_mesh("clothes_top", &mesh);
    assert!(unlit);
}

#[test]
fn blend_main_skin_stays_lit() {
    let mut mesh = crate::unity::ExtractedMesh {
        name: "o_body_a".into(),
        ..Default::default()
    };
    mesh.mat = Some(crate::unity::MeshMaterial {
        shader: "Koikano/main_skin".into(),
        render_queue: 2350,
        ..Default::default()
    });
    let (_, unlit) = blend_for_mesh("body", &mesh);
    assert!(!unlit);
}

#[test]
fn clothes_tex_ab_falls_back_to_main_ab() {
    let mut e = crate::list::ListEntry {
        main_ab: "chara/co_top.unity3d".into(),
        ..Default::default()
    };
    e.extra.insert("MainTexAB".into(), "0".into());
    e.extra.insert("ColorMaskAB".into(), "chara/tex.unity3d".into());
    assert_eq!(clothes_tex_ab(&e, "MainTexAB"), "chara/co_top.unity3d");
    assert_eq!(clothes_tex_ab(&e, "ColorMaskAB"), "chara/tex.unity3d");
    assert_eq!(clothes_tex_ab(&e, "MainTex02AB"), "chara/co_top.unity3d");
}

fn png_mean_abs_diff(a: &[u8], b: &[u8]) -> Option<f64> {
    let ia = crate::teximg::load_from_memory(a).ok()?.into_rgba8();
    let ib = crate::teximg::load_from_memory(b).ok()?.into_rgba8();
    let (wa, ha) = ia.dimensions();
    let (wb, hb) = ib.dimensions();
    if wa == 0 || ha == 0 || wb == 0 || hb == 0 {
        return None;
    }
    let mut sum = 0.0f64;
    let mut n = 0.0f64;
    for y in 0..ha {
        for x in 0..wa {
            let pa = ia.get_pixel(x, y).0;
            if pa[3] < 8 {
                continue;
            }
            let bx = (x as u64 * wb as u64 / wa as u64) as u32;
            let by = (y as u64 * hb as u64 / ha as u64) as u32;
            let pb = ib.get_pixel(bx.min(wb - 1), by.min(hb - 1)).0;
            sum += (pa[0] as f64 - pb[0] as f64).abs()
                + (pa[1] as f64 - pb[1] as f64).abs()
                + (pa[2] as f64 - pb[2] as f64).abs();
            n += 3.0;
        }
    }
    if n == 0.0 {
        None
    } else {
        Some(sum / n)
    }
}

#[test]
fn clothes_create_slot_follows_rend_normal_groups() {
    let map = crate::unity::ClothesRendererMap {
        overlay_renderers: vec![],
        normal01: [10].into_iter().collect(),
        normal02: [20, 21].into_iter().collect(),
        normal03: [30].into_iter().collect(),
        alpha01: [40].into_iter().collect(),
        accessory: [50].into_iter().collect(),
    };
    assert_eq!(clothes_create_slot(&[10], &map), Some(0));
    assert_eq!(clothes_create_slot(&[20], &map), Some(1));
    assert_eq!(clothes_create_slot(&[21], &map), Some(1));
    assert_eq!(clothes_create_slot(&[30], &map), Some(2));
    assert_eq!(clothes_create_slot(&[40], &map), Some(0));
    // ChangeCustomClothes assigns create RT 0 to rendAccessory too.
    assert_eq!(clothes_create_slot(&[50], &map), Some(0));
    assert_eq!(clothes_create_slot(&[99], &map), None);
    assert_eq!(clothes_create_slot(&[20,30], &map), Some(2));
    assert_eq!(clothes_create_slot(&[20,30,50], &map), Some(0));

    let mut meshes = vec![
        ExtractedMesh { renderer_ids: vec![50], texture_png: Some(vec![9]), ..Default::default() },
        ExtractedMesh { renderer_ids: vec![50], submesh_index: 1, texture_png: Some(vec![8]), ..Default::default() },
        ExtractedMesh { renderer_ids: vec![99], texture_png: Some(vec![7]), ..Default::default() },
    ];
    apply_clothes_create_textures(&mut meshes, Some(&map), &[Some(vec![1, 2, 3]), None, None]);
    assert_eq!(meshes[0].texture_png.as_deref(), Some(&[1, 2, 3][..]));
    assert!(meshes[0].runtime_textures.contains_key("_MainTex"));
    assert_eq!(meshes[1].texture_png.as_deref(), Some(&[8][..]));
    assert_eq!(meshes[2].texture_png.as_deref(), Some(&[7][..]));
    let mut missing=vec![ExtractedMesh {renderer_ids:vec![20],texture_png:Some(vec![6]),..Default::default()}];
    apply_clothes_create_textures(&mut missing,Some(&map),&[Some(vec![1]),None,None]);
    assert_eq!(missing[0].texture_png.as_deref(),Some(&[6][..]));
    assert!(!missing[0].runtime_textures.contains_key("_MainTex"));
    missing[0].renderer_ids=vec![20,30];
    apply_clothes_create_textures(&mut missing,Some(&map),&[Some(vec![1]),Some(vec![2]),None]);
    assert_eq!(missing[0].texture_png.as_deref(),Some(&[2][..]));
    assert_eq!(assigned_clothes_slot(&missing[0].renderer_ids,&map,&[Some(vec![1]),Some(vec![2]),None]),Some(1));
    let before=missing[0].texture_png.clone();
    apply_clothes_create_textures(&mut missing,Some(&map),&[None,Some(vec![3]),Some(vec![4])]);
    assert_eq!(missing[0].texture_png,before);
    assert_eq!(assigned_clothes_slot(&missing[0].renderer_ids,&map,&[None,Some(vec![3]),Some(vec![4])]),None);
}

#[test]
#[ignore="manual snapshot comparison requires Saved/Converted/_user_check0..6 and native outfit0..6 fixtures"]
fn clothes_rt_matches_dump_all_outfits() {
    use std::collections::HashSet;
    let conv = std::path::Path::new(r"C:\portable\OpenKoikatsu\Saved\Converted");
    let dump_root = &crate::test_support::game_path(r"\mesh_dump");
    let mut n_ok = 0usize;
    let mut n_bad = 0usize;
    for outfit in 0..7i32 {
        let ours_dir = conv.join(format!("_user_check{outfit}"));
        let dump_dir = dump_root.join(format!("outfit{outfit}")).join("Ushiwakamaru_0");
        if !ours_dir.ok_is_dir() || !dump_dir.ok_is_dir() {
            eprintln!("RTDIFF skip outfit{outfit} missing dir");
            continue;
        }
        let ours: Vec<std::path::PathBuf> = crate::gamefs::read_dir(&ours_dir)
            .unwrap()
            .flatten()
            .map(|e| e.path())
            .filter(|p| {
                p.extension().and_then(|s| s.to_str()) == Some("png")
                    && p.file_name()
                        .and_then(|s| s.to_str())
                        .map(|n| n.starts_with("clothes_"))
                        .unwrap_or(false)
            })
            .collect();
        let mut seen_dump = HashSet::new();
        let Ok(rd) = crate::gamefs::read_dir(&dump_dir) else {
            continue;
        };
        for e in rd.flatten() {
            let p = e.path();
            if p.extension().and_then(|s| s.to_str()) != Some("json") {
                continue;
            }
            let name = p.file_name().and_then(|s| s.to_str()).unwrap_or("");
            if !name.ends_with(".materials.json") {
                continue;
            }
            let Ok(txt) = crate::gamefs::read_to_string(&p) else {
                continue;
            };
            let Ok(v) = serde_json::from_str::<serde_json::Value>(&txt) else {
                continue;
            };
            let mesh = v
                .get("mesh")
                .and_then(|x| x.as_str())
                .unwrap_or("");
            if mesh.is_empty() {
                continue;
            }
            // Dump includes hidden clothes-state meshes (`_b` half-off, `_c`
            // off). Worn state is `_a` (ChaReference S_*_DEF).
            if mesh.ends_with("_b") || mesh.ends_with("_c") {
                continue;
            }
            let Some(png_name) = v.get("materials").and_then(|m| m.as_array()).and_then(|arr| {
                arr.iter().find_map(|mat| {
                    mat.get("properties").and_then(|ps| ps.as_array()).and_then(|ps| {
                        ps.iter().find_map(|pr| {
                            if pr.get("name").and_then(|n| n.as_str()) != Some("_MainTex") {
                                return None;
                            }
                            pr.get("texture")
                                .and_then(|t| t.get("exportedPng"))
                                .and_then(|x| x.as_str())
                                .filter(|s| s.ends_with("__MainTex.png"))
                        })
                    })
                })
            }) else {
                continue;
            };
            if !seen_dump.insert(png_name.to_string()) {
                continue;
            }
            let needle = format!("_{mesh}_");
            let needle_end = format!("_{mesh}.");
            let cands: Vec<_> = ours
                .iter()
                .filter(|op| {
                    op.file_name()
                        .and_then(|s| s.to_str())
                        .map(|n| n.contains(&needle) || n.contains(&needle_end))
                        .unwrap_or(false)
                })
                .collect();
            if cands.is_empty() {
                continue;
            }
            let dump_png = dump_dir.join(png_name);
            let Ok(b) = crate::gamefs::read(&dump_png) else {
                continue;
            };
            let mut best: Option<(f64, String)> = None;
            for ours_png in cands {
                let Ok(a) = crate::gamefs::read(ours_png) else {
                    continue;
                };
                let Some(d) = png_mean_abs_diff(&a, &b) else {
                    continue;
                };
                let ours_n = ours_png
                    .file_name()
                    .and_then(|s| s.to_str())
                    .unwrap_or("")
                    .to_string();
                if best.as_ref().map(|(v, _)| d < *v).unwrap_or(true) {
                    best = Some((d, ours_n));
                }
            }
            let Some((d, ours_n)) = best else {
                continue;
            };
            eprintln!("RTDIFF o{outfit} {mesh} {ours_n} vs {png_name} mean={d}");
            if d < 25.0 {
                n_ok += 1;
            } else {
                n_bad += 1;
                eprintln!("RTDIFF BAD o{outfit} {mesh} mean={d}");
            }
        }
    }
    eprintln!("RTDIFF summary ok={n_ok} bad={n_bad}");
    assert!(n_ok > 0, "no clothes RT pairs compared");
    assert_eq!(n_bad, 0, "{n_bad} clothes RT pairs mean abs diff >= 25");
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_hexa_headband_component() {
    use std::collections::{HashMap, HashSet};
    let mods = crate::test_support::game_path(r"\mods");
    fn walk(d: &std::path::Path, out: &mut Option<std::path::PathBuf>) {
        if out.is_some() {
            return;
        }
        let Ok(rd) = crate::gamefs::read_dir(d) else {
            return;
        };
        for e in rd.flatten() {
            let p = e.path();
            if p.ok_is_dir() {
                walk(&p, out);
            } else if p
                .file_name()
                .and_then(|s| s.to_str())
                .map(|n| n.contains("AA2 HEXA Accessories pack") && n.ends_with(".zipmod"))
                .unwrap_or(false)
            {
                *out = Some(p);
            }
        }
    }
    let mut zip = None;
    walk(&mods, &mut zip);
    let Some(zip) = zip else {
        eprintln!("HEXAPROBE no zip");
        return;
    };
    eprintln!("HEXAPROBE zip={}", zip.display());
    let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(&zip, "chara/AA2HEXA_acc_head.unity3d")
    else {
        eprintln!("HEXAPROBE no bundle");
        return;
    };
    let tmp = std::env::temp_dir().join("ok_hexa_head.unity3d");
    let _ = std::fs::write(&tmp, &bytes);
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle(&tmp) else {
        eprintln!("HEXAPROBE load fail");
        return;
    };
    let _ = bundle.data_checked();
    let pptr = |v: &unity_asset_core::UnityValue| -> Option<i64> {
        match v {
            unity_asset_core::UnityValue::Object(o) => o
                .get("m_PathID")
                .or_else(|| o.get("pathID"))
                .and_then(|x| x.as_i64()),
            unity_asset_core::UnityValue::Integer(i) => Some(*i),
            _ => None,
        }
    };
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(fb) else {
            continue;
        };
        let mut scripts: HashMap<i64, String> = HashMap::new();
        for info in ser.objects_of_type(115) {
            if let Ok(obj) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
            {
                let class = obj
                    .class
                    .properties()
                    .get("m_ClassName")
                    .or_else(|| obj.class.properties().get("m_Name"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                scripts.insert(info.path_id, class);
            }
        }
        let mut go_name: HashMap<i64, String> = HashMap::new();
        let mut tf_go: HashMap<i64, i64> = HashMap::new();
        let mut tf_children: HashMap<i64, Vec<i64>> = HashMap::new();
        let mut smr_go: HashMap<i64, i64> = HashMap::new();
        let mut smr_mesh: HashMap<i64, i64> = HashMap::new();
        let mut mesh_name: HashMap<i64, String> = HashMap::new();
        for info in &ser.objects {
            let Ok(obj) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info)
            else {
                continue;
            };
            let props = obj.class.properties();
            match info.type_id {
                1 => {
                    go_name.insert(info.path_id, obj.name().unwrap_or_default());
                }
                4 => {
                    if let Some(g) = props.get("m_GameObject").and_then(|v| pptr(v)) {
                        tf_go.insert(info.path_id, g);
                    }
                    if let Some(unity_asset_core::UnityValue::Array(ch)) =
                        props.get("m_Children")
                    {
                        tf_children.insert(
                            info.path_id,
                            ch.iter().filter_map(|x| pptr(x)).collect(),
                        );
                    }
                }
                43 => {
                    mesh_name.insert(info.path_id, obj.name().unwrap_or_default());
                }
                137 | 33 => {
                    if let Some(g) = props.get("m_GameObject").and_then(|v| pptr(v)) {
                        smr_go.insert(info.path_id, g);
                    }
                    if let Some(m) = props.get("m_Mesh").and_then(|v| pptr(v)) {
                        smr_mesh.insert(info.path_id, m);
                    }
                }
                _ => {}
            }
        }
        let go_tf: HashMap<i64, i64> = tf_go.iter().map(|(tf, go)| (*go, *tf)).collect();
        let want = "p_acs_hexa_headbandmayoi";
        let want_trim = "acs_hexa_headbandmayoi";
        let roots: Vec<i64> = go_name
            .iter()
            .filter(|(_, n)| {
                let l = n.to_ascii_lowercase();
                l == want || l == want_trim
            })
            .map(|(id, _)| *id)
            .collect();
        let mut seen: HashSet<i64> = HashSet::new();
        let mut stack = roots.clone();
        while let Some(g) = stack.pop() {
            if !seen.insert(g) {
                continue;
            }
            if let Some(tf) = go_tf.get(&g) {
                if let Some(k) = tf_children.get(tf) {
                    for c in k {
                        if let Some(cg) = tf_go.get(c) {
                            stack.push(*cg);
                        }
                    }
                }
            }
        }
        eprintln!(
            "HEXAPROBE file={:?} gos={} roots={:?} subtree={}",
            file.name,
            go_name.len(),
            roots
                .iter()
                .filter_map(|r| go_name.get(r))
                .collect::<Vec<_>>(),
            seen.len()
        );
        for (comp, go) in &smr_go {
            if !seen.contains(go) {
                continue;
            }
            eprintln!(
                "HEXAPROBE renderer={comp} go={:?} mesh={:?} meshname={:?}",
                go_name.get(go),
                smr_mesh.get(comp),
                smr_mesh.get(comp).and_then(|m| mesh_name.get(m))
            );
        }
        for info in ser.objects_of_type(114) {
            let Ok(obj) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
            else {
                continue;
            };
            let props = obj.class.properties();
            let class = props
                .get("m_Script")
                .and_then(|v| pptr(v))
                .and_then(|id| scripts.get(&id))
                .cloned()
                .unwrap_or_default();
            if !class.contains("ChaAccessoryComponent") {
                continue;
            }
            let go = props.get("m_GameObject").and_then(|v| pptr(v));
            let ids = |k: &str| -> Vec<i64> {
                props
                    .get(k)
                    .and_then(|v| v.as_array())
                    .map(|a| a.iter().filter_map(|x| pptr(x)).collect())
                    .unwrap_or_default()
            };
            let names: Vec<String> = ids("rendNormal")
                .iter()
                .map(|i| go_name.get(i).cloned().unwrap_or_else(|| format!("ext:{i}")))
                .collect();
            eprintln!(
                "HEXAPROBE COMP go={:?} inSubtree={} normal={:?} names={:?} alpha={:?} hair={:?} use01={:?}",
                go.and_then(|g| go_name.get(&g)),
                go.map(|g| seen.contains(&g)).unwrap_or(false),
                ids("rendNormal"),
                names,
                ids("rendAlpha"),
                ids("rendHair"),
                props.get("useColor01").map(|v| format!("{v:?}")),
            );
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_prefab_ik() {
    let game = crate::test_support::game_path(r"");
    let Some(cfg) = crate::unity::parse_prefab_ik(&game) else {
        eprintln!("PREFABIK none");
        return;
    };
    eprintln!(
        "PREFABIK iter={} spineStiff={} pullV={} pullH={} rootNode={:?}",
        cfg.iterations,
        cfg.spine_stiffness,
        cfg.pull_body_vertical,
        cfg.pull_body_horizontal,
        cfg.root_node
    );
    eprintln!(
        "PREFABIK refs root={:?} pelvis={:?} lua={:?} lf={:?} lh={:?} lt={:?} spine={:?} head={:?}",
        cfg.references.root,
        cfg.references.pelvis,
        cfg.references.left_upper_arm,
        cfg.references.left_forearm,
        cfg.references.left_hand,
        cfg.references.left_thigh,
        cfg.references.spine,
        cfg.references.head
    );
    for (i, c) in cfg.chains.iter().enumerate() {
        eprintln!(
            "PREFABIK chain{i} pin={} pull={} push={} reach={} nodes={:?} children={:?} cc={}",
            c.pin,
            c.pull,
            c.push,
            c.reach,
            c.nodes,
            c.children,
            c.child_constraints.len()
        );
    }
    for (i, e) in cfg.effectors.iter().enumerate() {
        eprintln!(
            "PREFABIK eff{i} bone={:?} target={:?} posW={} rotW={} childBones={:?} plane={:?}/{:?}/{:?}",
            e.bone,
            e.target,
            e.position_weight,
            e.rotation_weight,
            e.child_bones,
            e.plane_bone1,
            e.plane_bone2,
            e.plane_bone3
        );
    }
    eprintln!(
        "PREFABIK spineBones={:?} lua={:?} rua={:?} lt={:?} rt={:?}",
        cfg.spine_bones,
        cfg.spine_left_upper_arm,
        cfg.spine_right_upper_arm,
        cfg.spine_left_thigh,
        cfg.spine_right_thigh
    );
    for lm in &cfg.limb_mappings {
        eprintln!(
            "PREFABIK limb {:?}/{:?}/{:?}/{:?} mrw={} w={}",
            lm.parent_bone, lm.bone1, lm.bone2, lm.bone3, lm.maintain_rotation_weight, lm.weight
        );
    }
    for (b, w) in &cfg.bone_mappings {
        eprintln!("PREFABIK boneMap {:?} mrw={}", b, w);
    }
    eprintln!("PREFABIK setup_ok={}", crate::finalik::load_setup(&game).is_some());
}

#[test]
fn probe_text_bundle() {        let Ok(p) = std::env::var("OK_PROBE_BUNDLE") else {
        return;
    };
    for (name, bytes) in crate::list::text_assets_from_bundle(std::path::Path::new(&p)) {
        let text = String::from_utf8_lossy(&bytes).replace('\r', "");
        eprintln!("TEXTBUNDLE {name} bytes={} text=\n{text}", bytes.len());
    }
}

#[test]
fn probe_entry() {        let cat: i32 = std::env::var("OK_PROBE_CAT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(100);
    let id: i32 = std::env::var("OK_PROBE_ID")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(0);
    let dir = crate::test_support::game_path(r"\abdata\list\characustom");
    let catalog = if std::env::var("OK_PROBE_MODS").is_ok() {
        crate::list::load_mods_catalog(&crate::test_support::game_path(r"\mods"))
    } else {
        crate::list::load_characustom_dir(&dir)
    };
    if let Some(e) = catalog.get(cat, id) {
        eprintln!(
            "ENTRY cat={} id={} name={} main_ab={} main_data={} main_tex={} extra={:?}",
            e.category_no, e.id, e.name, e.main_ab, e.main_data, e.main_tex, e.extra
        );
    } else {
        eprintln!("ENTRY cat={cat} id={id} not found");
    }
}

#[test]
fn probe_head() {
    let Ok(p) = std::env::var("OK_PROBE_BUNDLE") else {
        return;
    };
    let path = std::path::Path::new(&p);
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle(path) else {
        eprintln!("PROBE: load_bundle failed");
        return;
    };
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(bytes) else {
            continue;
        };
        for info in ser.objects_of_type(43) {
            let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
            else {
                continue;
            };
            let name = obj.name().unwrap_or_default();
            if !name.eq_ignore_ascii_case("cf_O_face") {
                continue;
            }
            let props = obj.class.properties();
            if let Some(vd) = props.get("m_VertexData") {
                if let unity_asset_core::UnityValue::Object(v) = vd {
                    if let Some(chans) = v.get("m_Channels") {
                        eprintln!("PROBE cf_O_face channels={chans:?}");
                    }
                }
            }
            if let Some(ms) = props.get("m_SubMeshes") {
                eprintln!("PROBE cf_O_face m_SubMeshes = {ms:?}");
            }
            if let Some(vc) = props.get("m_VertexCount") {
                eprintln!("PROBE cf_O_face m_VertexCount = {vc:?}");
            }
            let keys: Vec<&String> = props.keys().collect();
            eprintln!("PROBE cf_O_face keys = {keys:?}");
        }
        let mut mesh_names: std::collections::HashMap<i64, String> =
            std::collections::HashMap::new();
        for info in ser.objects_of_type(43) {
            if let Ok(o) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
            {
                mesh_names.insert(o.path_id(), o.name().unwrap_or_default());
            }
        }
        for tid in [137i32, 23i32] {
            for info in ser.objects_of_type(tid) {
                let Ok(obj) =
                    unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
                else {
                    continue;
                };
                let props = obj.class.properties();
                let matn = props
                    .get("m_Materials")
                    .and_then(|m| m.as_array())
                    .map(|a| a.len())
                    .unwrap_or(0);
                let mp = props
                    .get("m_Mesh")
                    .and_then(|v| match v {
                        unity_asset_core::UnityValue::Object(o) => o
                            .get("m_PathID")
                            .or_else(|| o.get("m_PathId"))
                            .and_then(|x| x.as_i64()),
                        unity_asset_core::UnityValue::Integer(i) => Some(*i),
                        _ => None,
                    })
                    .and_then(|id| mesh_names.get(&id).cloned())
                    .unwrap_or_default();
                if mp.eq_ignore_ascii_case("cf_O_face") || matn > 1 {
                    eprintln!("PROBE SMR mesh={mp} mats={matn}");
                }
            }
        }
    }
}

#[test]
fn probe_mesh_aabb() {
    let Ok(p) = std::env::var("OK_PROBE_BUNDLE") else {
        return;
    };
    let (meshes, skel, _tex) = crate::unity::load_base_bundle(std::path::Path::new(&p));
    for m in &meshes {
        if !m.name.eq_ignore_ascii_case("cf_O_face") {
            continue;
        }
        let mut mn = [f32::MAX; 3];
        let mut mx = [f32::MIN; 3];
        for v in &m.positions {
            for i in 0..3 {
                mn[i] = mn[i].min(v[i]);
                mx[i] = mx[i].max(v[i]);
            }
        }
        let uvhash: f64 = m
            .uvs
            .iter()
            .enumerate()
            .map(|(i, uv)| (uv[0] as f64 + 1.0) * (i as f64 + 1.0) + uv[1] as f64 * 0.5)
            .sum();
        eprintln!(
            "MESHAABB {} verts={} binds={} uv={} uvhash={:.4} aabb=({:.4},{:.4},{:.4})-({:.4},{:.4},{:.4})",
            m.name,
            m.positions.len(),
            m.bind_poses.len(),
            m.uvs.len(),
            uvhash,
            mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]
        );
    }
    if let Some(s) = &skel {
        eprintln!("MESHAABB skel bones={}", s.bone_ids.len());
    }
}


#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn skin_stream_alignment_keeps_all_four_influences() {
    let path = &crate::test_support::game_path(r"\abdata\chara\cpo_jacket_b_00.unity3d");
    let Ok(bytes) = crate::gamefs::read(path) else { return; };
    let (meshes, _, _) = crate::unity::meshes_skel_from_memory(bytes);
    let mesh = meshes.iter().find(|m| m.name == "o_top_jacket01_a" && m.positions.len() == 3599).unwrap();
    let want = [0.44304767, 0.36732113, 0.09527123, 0.09435997];
    for (a, b) in mesh.weights[0].iter().zip(want) { assert!((a - b).abs() < 1e-5); }
    assert_eq!(mesh.bone_idx[0], [2, 56, 136, 17]);
    assert_eq!(mesh.weights[3452], [1.0, 0.0, 0.0, 0.0]);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn material_alpha_mask_resolves_from_shared_bundle() {
    let game = &crate::test_support::game_path(r"");
    if !game.join("abdata/chara/mt_mask_bra_00.unity3d").ok_is_file() { return; }
    let mut meshes = vec![crate::unity::ExtractedMesh {
        mat: Some(crate::unity::MeshMaterial {
            tex_slots: vec![("_AlphaMask".into(), "cf_inner01_bra_mab".into())],
            ..Default::default()
        }),
        ..Default::default()
    }];
    resolve_material_alpha_masks(game, &mut meshes);
    let mask = meshes[0].alpha_mask_png.as_ref().expect("shared material mask");
    assert!(mask.len() > 100);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn vanilla_zero_subpart_supplies_bra_mask() {
    let game = &crate::test_support::game_path(r"");
    let path = game.join(r"UserData\chara\female\Hayase_yuuka_keiyuu_V1_02 For KKS.png");
    if !path.ok_is_file() { return; }
    let card = crate::card::parse_card_outfit(&path, 0).unwrap();
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mask = over_layer_mask_png(game, &card, &vanilla, &ChaListCatalog::default(), &std::collections::HashMap::new(), 0, "OverBraMask")
        .expect("jacket SubA id 0 must contribute its authored mask");
    let image = crate::teximg::load_from_memory(&mask).unwrap().into_rgba8();
    assert_eq!(image.dimensions(), (512, 256));
    let chest = image.get_pixel(185, 119).0;
    assert_eq!(chest[0], 0);
    assert_eq!(chest[1], 255);
}


fn probe_text_assets() {
    let Ok(p) = std::env::var("OK_PROBE_BUNDLE") else {
        return;
    };
    let assets = crate::list::text_assets_from_bundle(std::path::Path::new(&p));
    for (name, bytes) in &assets {
        let (n, lo, hi) = crate::shape::key_scl_range(bytes, "cf_J_FaceUp_ty");
        eprintln!(
            "TEXTASSET {name} len={} faceup n={n} scl=[{lo:.4},{hi:.4}]",
            bytes.len()
        );
    }
}

#[test]
fn probe_mat() {
    let Ok(p) = std::env::var("OK_PROBE_BUNDLE") else {
        return;
    };
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle(std::path::Path::new(&p)) else {
        eprintln!("PROBE: load_bundle failed");
        return;
    };
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(bytes) else {
            continue;
        };
        let mut tex_names: std::collections::HashMap<i64, String> =
            std::collections::HashMap::new();
        for info in ser.objects_of_type(28) {
            if let Ok(o) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
            {
                tex_names.insert(o.path_id(), o.name().unwrap_or_default());
            }
        }
        let pptr = |v: &unity_asset_core::UnityValue| -> Option<i64> {
            match v {
                unity_asset_core::UnityValue::Object(o) => o
                    .get("m_PathID")
                    .or_else(|| o.get("m_PathId"))
                    .and_then(|x| x.as_i64()),
                unity_asset_core::UnityValue::Integer(i) => Some(*i),
                _ => None,
            }
        };
        for info in ser.objects_of_type(21) {
            let Ok(obj) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, &info)
            else {
                continue;
            };
            let name = obj.name().unwrap_or_default();
            let props = obj.class.properties();
            eprintln!("MAT name={name}");
            if std::env::var("OK_MAT_DEBUG").is_ok() {
                let s = format!("{:?}", props.get("m_SavedProperties"));
                eprintln!("MATDBG {name} {}", &s[..s.len().min(600)]);
            }
            if let Some(unity_asset_core::UnityValue::Object(sp)) = props.get("m_SavedProperties") {
                if let Some(unity_asset_core::UnityValue::Array(envs)) = sp.get("m_TexEnvs") {
                    for e in envs {
                        let (key, val) = match e {
                            unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                                let k = pair[0].as_str().unwrap_or("?").to_string();
                                (k, pair[1].clone())
                            }
                            unity_asset_core::UnityValue::Object(eo) => (
                                eo.get("first").and_then(|v| v.as_str()).unwrap_or("?").to_string(),
                                eo.get("second").cloned().unwrap_or(unity_asset_core::UnityValue::Null),
                            ),
                            _ => continue,
                        };
                        let tex = val
                            .as_object()
                            .and_then(|so| so.get("m_Texture"))
                            .and_then(&pptr)
                            .and_then(|id| tex_names.get(&id).cloned())
                            .unwrap_or_default();
                        eprintln!("  TEXENV {key} = {tex}");
                    }
                }
                if let Some(unity_asset_core::UnityValue::Array(cs)) = sp.get("m_Floats") {
                    for c in cs {
                        if let unity_asset_core::UnityValue::Object(co) = c {
                            let key = co.get("first").and_then(|v| v.as_str()).unwrap_or("?");
                            let val = co.get("second").and_then(|v| v.as_f64());
                            eprintln!("  FLOAT {key} = {val:?}");
                        }
                    }
                }
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_smrs() {
    use std::collections::HashMap;
    let path =
        &crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    if !path.ok_is_file() {
        eprintln!("PROBESMRS path missing");
        return;
    }
    let Ok(bundle) = unity_asset_binary::bundle::load_bundle(path) else {
        eprintln!("PROBESMRS load failed");
        return;
    };
    let _ = bundle.data_checked();
    eprintln!("PROBESMRS nfiles={}", bundle.files.len());
    for file in &bundle.files {
        let _ = file;
        let Ok(bytes) = bundle.extract_file_data(file) else {
            eprintln!("PROBESMRS extract fail {}", file.name);
            continue;
        };
        let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(bytes) else {
            eprintln!("PROBESMRS parse fail {}", file.name);
            continue;
        };
        eprintln!("PROBESMRS file={} nobj={}", file.name, ser.objects.len());
        let mut go_names: HashMap<i64, String> = HashMap::new();
        for info in &ser.objects {
            if info.type_id == 1 {
                if let Ok(o) =
                    unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info)
                {
                    go_names.insert(info.path_id, o.name().unwrap_or_default());
                }
            }
        }
        let go_of = |v: Option<&unity_asset_core::UnityValue>| -> String {
            match v {
                Some(unity_asset_core::UnityValue::Object(m)) => m
                    .get("m_PathID")
                    .and_then(|x| x.as_i64())
                    .and_then(|id| go_names.get(&id).cloned())
                    .unwrap_or_default(),
                _ => String::new(),
            }
        };
        let pptr = |v: &unity_asset_core::UnityValue| -> Option<i64> {
            match v {
                unity_asset_core::UnityValue::Object(o) => o
                    .get("m_PathID")
                    .or_else(|| o.get("m_PathId"))
                    .and_then(|x| x.as_i64()),
                unity_asset_core::UnityValue::Integer(i) => Some(*i),
                _ => None,
            }
        };
        let mut mesh_names: HashMap<i64, String> = HashMap::new();
        let mut mesh_go: HashMap<i64, String> = HashMap::new();
        for info in &ser.objects {
            if info.type_id == 43 {
                if let Ok(o) =
                    unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info)
                {
                    mesh_names.insert(info.path_id, o.name().unwrap_or_default());
                    mesh_go.insert(info.path_id, go_of(o.class.properties().get("m_GameObject")));
                }
            }
        }
        for info in &ser.objects {
            if info.type_id != 137 {
                continue;
            }
            if let Ok(o) =
                unity_asset_decode::object::UnityObject::from_serialized_file(&ser, info)
            {
                let props = o.class.properties();
                let mesh = match props.get("m_Mesh") {
                    Some(unity_asset_core::UnityValue::Object(m)) => {
                        m.get("m_PathID").and_then(|v| v.as_i64())
                    }
                    _ => None,
                };
                let nb = match props.get("m_Bones") {
                    Some(unity_asset_core::UnityValue::Array(a)) => a.len(),
                    _ => 0,
                };
                let mn = mesh
                    .and_then(|id| mesh_names.get(&id).cloned())
                    .unwrap_or_default();
                let mgo = mesh
                    .and_then(|id| mesh_go.get(&id).cloned())
                    .unwrap_or_default();
                let sgo = go_of(props.get("m_GameObject"));
                if nb <= 6 || mn.contains("Ohitomi_L02") {
                    eprintln!("SMR mesh={mn} nbones={nb} smr_go={sgo} mesh_go={mgo}");
                }
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_maindata() {
    let path =
        &crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    if !path.ok_is_file() {
        return;
    }
    let bytes = crate::gamefs::read(path).unwrap();
    let md = std::env::var("OK_MAINDATA").unwrap_or_else(|_| "p_cf_head_00".into());
    let (meshes, skel) = crate::unity::meshes_skel_for_maindata(bytes, &md);
    eprintln!("MAINDATA {md} meshes={}", meshes.len());
    for m in &meshes {
        eprintln!(
            "  M {} path={} verts={} binds={}",
            m.name,
            m.path_id,
            m.positions.len(),
            m.bind_poses.len()
        );
    }
    if let Some(s) = skel {
        eprintln!("  skel n={} names={:?}", s.names.len(), s.names);
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn probe_head_bones() {
    let path =
        &crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    if !path.ok_is_file() {
        return;
    }
    for m in ["cf_O_face", "cf_Ohitomi_L", "cf_Ohitomi_L02", "cf_O_eyeline"] {
        match crate::unity::skeleton_for_mesh(path, m) {
            Some(sk) => {
                eprintln!("HEADBONE {m} n={} names={:?}", sk.names.len(), sk.names);
            }
            None => eprintln!("HEADBONE {m} none"),
        }
    }
}

#[test]
fn rgb_on_black_alpha_punches_black() {
    let mut img = image::RgbaImage::new(2, 1);
    img.put_pixel(0, 0, image::Rgba([0, 0, 0, 255]));
    img.put_pixel(1, 0, image::Rgba([80, 120, 200, 255]));
    let png = encode_png(&img).expect("png");
    let out = rgb_on_black_alpha(&png).expect("alpha");
    let got = crate::teximg::load_from_memory(&out).unwrap().into_rgba8();
    assert_eq!(got.get_pixel(0, 0).0[3], 0, "black should be transparent");
    assert!(
        got.get_pixel(1, 0).0[3] > 100,
        "iris colour should stay opaque, got {}",
        got.get_pixel(1, 0).0[3]
    );
    assert_eq!(got.get_pixel(1, 0).0[2], 200);
}

#[test]
fn overlay_overtex_applies_eyeshadow_tint() {
    let mut base = image::RgbaImage::new(8, 8);
    for p in base.pixels_mut() {
        *p = image::Rgba([220, 180, 170, 255]);
    }
    let mut over = image::RgbaImage::new(8, 8);
    for (x, y, p) in over.enumerate_pixels_mut() {
        *p = if x >= 4 {
            image::Rgba([255, 255, 255, 200])
        } else {
            image::Rgba([255, 255, 255, 0])
        };
        let _ = y;
    }
    let bp = encode_png(&base).unwrap();
    let op = encode_png(&over).unwrap();
    let out = crate::unity::overlay_overtex(&bp, &op, [0.529, 0.230, 0.230, 0.65]).unwrap();
    let got = crate::teximg::load_from_memory(&out).unwrap().into_rgba8();
    let keep = got.get_pixel(0, 0).0;
    let tint = got.get_pixel(7, 0).0;
    assert!((keep[0] as i32 - 220).abs() < 3);
    assert!(tint[0] < keep[0], "eyeshadow should darken R, {} vs {}", tint[0], keep[0]);
    assert!(tint[1] < keep[1]);
}

#[test]
fn eye_look_iris_uv_rest_pose() {
    // StudioEditor CalculateUvTransform with pupilWidth/Height=0.9, XY=0.5.
    let (s, o) = iris_uv_transform(0.9, 0.9, 0.5, 0.5, false);
    assert!(
        (s[0] - 1.0).abs() < 0.02 && (s[1] - 1.0).abs() < 0.02,
        "default scale should be ~1, got {s:?}"
    );
    assert!(
        (o[0] + 0.02).abs() < 0.005 && o[1].abs() < 0.005,
        "rest offset is motion - 0, got {o:?}"
    );
    let (sz, oz) = iris_uv_transform(0.0, 0.0, 0.5, 0.5, false);
    assert!(
        (sz[0] - 2.8).abs() < 0.02,
        "pupilWidth=0 zooms to 1+1.8=2.8, got {sz:?}"
    );
    assert!(
        (oz[0] + 1.0).abs() < 0.005,
        "Mathf.Lerp clamps scale 1.8: motion - center = -1.0, got {oz:?}"
    );
    // The right eye mirrors the pupil X term.
    let (_, or) = iris_uv_transform(0.9, 0.9, 0.5, 0.5, true);
    assert!(
        (or[0] - 0.02).abs() < 0.005,
        "right eye mirrors pupil X, got {or:?}"
    );
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn posed_head_parts_match_dump_bands() {
    let game = &crate::test_support::game_path(r"");
    let card = &crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
    if !game.join("abdata").ok_is_dir() {
        return;
    }
    let sess = extract_session(game, Some(card), 0, None).expect("session");
    let mut iris_l = None;
    let mut sclera_l = None;
    let mut face = None;
    let mut bangs_f20 = None;
    let mut bangs_f04 = None;
    let mut side_left = None;
    let mut side_right = None;
    let mut headband = None;
    let mut eyeline_alpha_mean = 0.0f32;
    for p in &sess.parts {
        let ln = p.name.to_ascii_lowercase();
        let (mn, mx) = crate::pose::aabb(&p.positions);
        eprintln!(
            "PART {} role={} nv={} x[{:.3},{:.3}] y[{:.3},{:.3}] z[{:.3},{:.3}]",
            p.name,
            p.role,
            p.positions.len(),
            mn[0],
            mx[0],
            mn[1],
            mx[1],
            mn[2],
            mx[2]
        );
        if ln.contains("ohitomi_l02") {
            iris_l = Some((mn, mx));
        } else if ln.contains("ohitomi_l") && !ln.contains("02") {
            sclera_l = Some((mn, mx));
        } else if ln == "cf_o_face" || (ln.contains("cf_o_face") && !ln.contains("hitomi")) {
            if face.is_none() {
                face = Some((mn, mx));
            }
        }
        if ln.contains("hair_f_20") {
            bangs_f20 = Some((mn, mx));
        }
        if ln.contains("hair_f_04") {
            bangs_f04 = Some((mn, mx));
        }
        if ln.contains("hair_f_113") {
            let mid = (mn[0] + mx[0]) * 0.5;
            if mid < 0.0 {
                side_left = Some((mn, mx));
            } else {
                side_right = Some((mn, mx));
            }
        }
        if ln.contains("hexa")
            || ln.contains("headband")
            || ln.contains("kamio")
            || ln.contains("kami_o")
            || ln.contains("kamiz")
        {
            headband = Some((p.name.clone(), mn, mx));
        }
        if ln.contains("eyeline") && !ln.contains("low") {
            if let Some(png) = p.png.as_ref() {
                if let Ok(img) = crate::teximg::load_from_memory(png) {
                    let rgba = img.to_rgba8();
                    let n = rgba.pixels().len().max(1) as f32;
                    eyeline_alpha_mean = rgba.pixels().map(|px| px.0[3] as f32).sum::<f32>()
                        / (n * 255.0);
                }
            }
        }
    }
    let _ = (
        iris_l.expect("iris L02"),
        sclera_l.expect("sclera L"),
        face.expect("face"),
        bangs_f20,
        bangs_f04,
        side_left,
        side_right,
        eyeline_alpha_mean,
    );
    if let Some((name, mn, mx)) = headband {
        eprintln!(
            "HEADBAND {name} y[{:.3},{:.3}] z[{:.3},{:.3}]",
            mn[1], mx[1], mn[2], mx[2]
        );
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn hexa_hachikuji_headband_prefab_has_meshes() {
    // StudioEditor LoadAsset(MainAB, MainData). AA2 HEXA packs many
    // prefabs in one bundle; we must pick the named one, not decode_all.
    let zip = Path::new(crate::test_support::game_file(r"\mods\Sideloader Modpack\Creamstar\[Creamstar]AA2 HEXA Accessories pack.zipmod"),
    );
    if !zip.ok_is_file() {
        return;
    }
    let Some((_, bytes)) =
        crate::mods::zipmod_one_unity3d(zip, "chara/AA2HEXA_acc_head.unity3d")
    else {
        panic!("HEXA acc_head.unity3d missing from zip");
    };
    let (meshes, _) =
        crate::unity::meshes_skel_for_maindata_ref(&bytes, "p_acs_hexa_headbandmayoi");
    for m in &meshes {
        let (mn, mx) = crate::pose::aabb(&m.positions);
        eprintln!(
            "HEXA {} nv={} x[{:.3},{:.3}] y[{:.3},{:.3}] z[{:.3},{:.3}]",
            m.name,
            m.positions.len(),
            mn[0],
            mx[0],
            mn[1],
            mx[1],
            mn[2],
            mx[2]
        );
    }
    assert!(
        !meshes.is_empty(),
        "Hachikuji headband prefab produced 0 meshes"
    );
}

#[test]
fn empty_parent_key_uses_list_parent() {
    // ChaControl: if (parentKey == "") parentKey = list.Parent.
    assert_eq!(resolve_acc_parent("", "a_n_headtop"), "a_n_headtop");
    assert_eq!(resolve_acc_parent("a_n_head", "a_n_headtop"), "a_n_head");
    assert_eq!(
        resolve_acc_parent("a_n_headside", "a_n_headtop"),
        "a_n_headside"
    );
}

#[test]
fn bake_eye_white_uses_r_as_coverage() {
    let mut img = image::RgbaImage::new(2, 1);
    img.put_pixel(0, 0, image::Rgba([0, 0, 0, 255]));
    img.put_pixel(1, 0, image::Rgba([255, 255, 255, 255]));
    let png = encode_png(&img).unwrap();
    let out = bake_eye_white(&png, [1.0, 1.0, 1.0, 1.0], [0.2, 0.2, 0.2, 1.0]).unwrap();
    let got = crate::teximg::load_from_memory(&out).unwrap().into_rgba8();
    // fragWhite returns alpha 1 everywhere (opaque quad masked by the face).
    assert_eq!(got.get_pixel(0, 0).0[3], 255, "black maps to opaque sub color");
    assert!(got.get_pixel(0, 0).0[0] < 80, "black maps to sub color");
    assert_eq!(got.get_pixel(1, 0).0[3], 255, "white is opaque");
    let p = got.get_pixel(1, 0).0;
    assert!(p[0] > 240 && p[1] > 240, "white maps to base color, got {p:?}");
}

#[test]
fn create_eyew_uses_linear_samples_and_color_alpha() {
    let png=encode_png(&image::RgbaImage::from_pixel(1,1,image::Rgba([128,255,0,0]))).unwrap();
    let base=[1.,0.5,0.25,0.8]; let sub=[0.1,0.2,0.3,0.2];
    for source_linear in [false,true] {
        let out=bake_eye_white_with_space(&png,base,sub,source_linear).unwrap();
        let actual=crate::teximg::load_from_memory(&out).unwrap().into_rgba8().get_pixel(0,0).0;
        // Independent piecewise IEC sRGB calculation of PS 0001's mad.
        let linear=|v:f64|if v<=0.04045 {v/12.92}else{((v+0.055)/1.055).powf(2.4)};
        let gamma=|v:f64|if v<=0.0031308 {v*12.92}else{1.055*v.powf(1./2.4)-0.055};
        let t=if source_linear {128./255.}else{linear(128./255.)};
        for c in 0..3 {
            let value=linear(sub[c] as f64)*(1.-t)+linear(base[c] as f64)*t;
            assert!((actual[c] as i16-(gamma(value)*255.).round() as i16).abs()<=1);
        }
        assert!((actual[3] as i16-((0.2+0.6*t)*255.).round() as i16).abs()<=1);
    }
}

#[test]
fn create_eyew_rt_matches_linear_bilinear_shader_and_original_sizes(){
    let source=image::RgbaImage::from_fn(7,5,|x,y|image::Rgba([
        ((x*37+y*59)%256) as u8,255,0,((x*53+y*19)%256) as u8]));
    let png=encode_png(&source).unwrap();
    let base=[0.83,0.51,0.27,0.72];let sub=[0.13,0.29,0.07,0.18];
    for size in [256,512]{for source_linear in [false,true]{for flip_source in [false,true]{
        let mut reference=source.clone();if flip_source{image::imageops::flip_vertical_in_place(&mut reference);}
        let actual=bake_eye_white_create_rt_sized(&png,base,sub,source_linear,size,flip_source).unwrap();
        let actual=crate::teximg::load_from_memory(&actual).unwrap().into_rgba8();
        assert_eq!(actual.dimensions(),(size,size));
        for(x,y,p)in actual.enumerate_pixels(){
            let shade=sample_rgba_clamped_space(&reference,(x as f32+0.5)/size as f32,(y as f32+0.5)/size as f32,!source_linear)[0];
            let mut expected=[0;4];
            for c in 0..3{expected[c]=to_unorm8(linear_channel_to_srgb(lerp_f(
                srgb_channel_to_linear(sub[c]),srgb_channel_to_linear(base[c]),shade)));}
            expected[3]=to_unorm8(lerp_f(sub[3],base[3],shade));
            assert_eq!(p.0,expected,"size={size} linear={source_linear} flip={flip_source} at {x},{y}");
        }
    }}}
}
#[test]
fn create_eyew_lookup_matches_scalar_for_every_red_byte() {
    let source = image::RgbaImage::from_fn(256, 4, |x, y|
        image::Rgba([x as u8, (255-x) as u8, (x/2) as u8, (y*85) as u8]));
    let png = encode_png(&source).unwrap();
    for (base, sub) in [([1.,0.5,0.25,0.8],[0.1,0.2,0.3,0.2]),
                        ([0.,0.04045,1.,0.],[1.,0.0031308,0.,1.]),
                        ([0.73,0.17,0.91,0.37],[0.28,0.64,0.03,0.93])] {
        for source_linear in [false, true] {
            let result = bake_eye_white_with_space(&png,base,sub,source_linear).unwrap();
            let result = crate::teximg::load_from_memory(&result).unwrap().into_rgba8();
            for (p, actual) in source.pixels().zip(result.pixels()) {
                let shade = if source_linear {p[0] as f32/255.} else {srgb_channel_to_linear(p[0] as f32/255.)};
                let mut expected = [0;4];
                for c in 0..3 {
                    expected[c] = to_unorm8(linear_channel_to_srgb(lerp_f(
                        srgb_channel_to_linear(sub[c]),srgb_channel_to_linear(base[c]),shade)));
                }
                expected[3] = to_unorm8(lerp_f(sub[3],base[3],shade));
                assert_eq!(actual.0,expected,"red={} linear={source_linear}",p[0]);
            }
        }
    }
}

#[test]
fn null_clothes_maintex_uses_shader_declared_white_default() {
    for shader in ["Shader Forge/main_opaque","Koikano/main_clothes_opaque"] {
        let mut mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {shader:shader.into(),..Default::default()}),..Default::default()};
        apply_clothes_default_main_texture(&mut mesh);
        let image=crate::teximg::load_from_memory(mesh.texture_png.as_ref().unwrap()).unwrap().into_rgba8();
        assert!(image.pixels().all(|p|p.0==[255;4]));
        let existing=vec![1,2,3];mesh.texture_png=Some(existing.clone());
        apply_clothes_default_main_texture(&mut mesh);assert_eq!(mesh.texture_png,Some(existing));
    }
    let mut unknown=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {shader:"custom/unknown".into(),..Default::default()}),..Default::default()};
    apply_clothes_default_main_texture(&mut unknown);assert!(unknown.texture_png.is_none());
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn koikano_accessory_hair_retains_mask_and_null_main_default() {
    let path=&crate::test_support::game_path("/UserData/chara/female/KK_380982.png");
    assert!(path.ok_is_file(),"installed fixture required");
    let mut card=crate::card::parse_card_outfit(path,0).unwrap();
    card.me_copies.clear();card.me_shader.clear();card.me_color.clear();card.me_float.clear();card.me_tex.clear();
    let mask=crate::unity::solid_png([0.0,1.0,0.0,1.0],2,2).unwrap();
    for shader in ["Koikano/hair_main_sun","Koikano/hair_main_sun_front"] {
        let source=crate::unity::solid_png([1.0,0.0,0.0,1.0],2,2).unwrap();
        let mut meshes=vec![ExtractedMesh {name:"fixture".into(),texture_png:Some(source),
            color_mask_png:Some(mask.clone()),mat:Some(crate::unity::MeshMaterial {
                shader:shader.into(),tex_slots:vec![("_MainTex".into(),String::new())],
                colors:[("_Color".into(),[0.3,0.2,0.1,1.0]),("_Color2".into(),[0.05,0.05,0.05,1.0])].into(),
                ..Default::default()}),..Default::default()}];
        color_accessory_meshes(Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MixedTextureSource"),None,
            &ListEntry::default(),&card,"accessory999",0,&mut meshes,&[],&[],None);
        assert_eq!(meshes[0].color_mask_png.as_ref(),Some(&mask));
        let image=crate::teximg::load_from_memory(meshes[0].texture_png.as_ref().unwrap()).unwrap().into_rgba8();
        assert!(image.pixels().all(|p|p.0==[255;4]),"null main must not use fallback or baked mask");
        // Without a component map, ChangeAccessoryColor writes the
        // selected accessory's default white colours before shader draw.
        assert_eq!(meshes[0].mat.as_ref().unwrap().color("_Color2"),Some([1.0;4]));
        let (blend,unlit)=blend_for_mesh("accessory",&meshes[0]);
        assert!(unlit);assert_eq!(blend,"masked");
        assert_eq!(crate::session::SessionPart::from_mesh("accessory",&meshes[0],&blend,unlit).shader_kind,10);
    }
}

#[test]
#[ignore="installed hair accessory renderer/component provenance"]
fn native_hair_accessory_component_provenance() {
    let game=&crate::test_support::game_path("");
    let card=crate::card::parse_card_outfit(&game.join("UserData/chara/female/KK_380982.png"),0).unwrap();
    let cat=load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let wanted=card.resolves.iter().map(|r|r.mod_id.clone()).collect();
    let guids=crate::mods::find_guids(&game.join("mods"),Some(&wanted));let mut lists=std::collections::HashMap::new();
    eprintln!("HAIR {:?}",card.hair_parts[0]);
    for slot in [21usize,22,23] {
        let a=&card.accessory[slot];
        let resolve=card.resolves.iter().find(|r|accessory_slot_from_property(&r.property)==Some(slot));
        let zip=resolve.and_then(|r|guid_path(&guids,&r.mod_id));
        let entry=resolve.and_then(|r|lookup_entry(&cat,&mut lists,zip.map(|p|p.as_path()),&r.mod_id,r.category_no,r.slot,r.local_slot))
            .or_else(||mods.get(a.type_id,a.id).cloned()).or_else(||cat.get(a.type_id,a.id).cloned()).unwrap();
        let fallback=entry.extra.get("__zip").map(PathBuf::from);
        let zip=zip.map(|p|p.as_path()).or(fallback.as_deref());
        let bytes=accessory_bundle_bytes(game,zip,&entry).unwrap();
        std::fs::create_dir_all("C:/portable/OpenKoikatsu/Saved/Tests/Hair322Sources").unwrap();
        std::fs::write(format!("C:/portable/OpenKoikatsu/Saved/Tests/Hair322Sources/slot{slot}.unity3d"),&bytes).unwrap();
        let hint=crate::unity::accessory_color_use(&bytes,&entry.main_data);
        let (mut meshes,_)=meshes_for_accessory_entry(game,zip,&entry,&card,0,&format!("outfit0.accessory{slot}"));
        eprintln!("SLOT {slot} prefab={} HAC={:?} hint={hint:?}",entry.main_data,card.hair_acc.get(&(0,slot as i32)));
        for m in &meshes {eprintln!("MESH {} ids={:?} shader={:?}",m.name,m.renderer_ids,m.mat.as_ref().map(|m|&m.shader));}
        color_accessory_meshes(game,zip,&entry,&card,&format!("outfit0.accessory{slot}"),0,&mut meshes,&[],&[],None);
        for m in &meshes {
            if slot==21 || slot==22 {
                assert_eq!(m.mat.as_ref().unwrap().color("_Color"),Some(a.color));
                assert_eq!(m.mat.as_ref().unwrap().color("_Color2"),Some(a.color2));
                assert_eq!(m.mat.as_ref().unwrap().color("_Color3"),Some(a.color3));
                assert!(!m.hair_gloss_png.is_some(),"absent HAC must not introduce gloss");
                let img=crate::teximg::load_from_memory(m.texture_png.as_ref().unwrap()).unwrap().into_rgba8();
                assert!(img.pixels().all(|p|p.0==[255;4]));
                let native=crate::gamefs::read_to_string("C:/portable/OpenKoikatsu/Saved/Tests/Hair322NativeR2/Card0.json.materials.tsv").unwrap();
                for (key,color) in [("_Color",a.color),("_Color2",a.color2),("_Color3",a.color3)] {
                    let line=native.lines().find(|l|l.starts_with("color\t") && l.contains(&format!("/ca_slot{slot}/")) && l.contains(&format!("\t{key}\t"))).unwrap();
                    let numbers:Vec<f32>=line.split('\t').last().unwrap().trim_start_matches("RGBA").trim_matches(['(',')']).split(',').map(|v|v.trim().parse().unwrap()).collect();
                    for (x,y) in color.iter().zip(&numbers) {assert!((x-y).abs()<0.000001,"native colour {key}: {x} vs {y}");}
                }
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn main_item_plus_me_mask_replaces_source_sampling() {
    let path = &crate::test_support::game_path("/UserData/chara/female/[Card] March_7th_keiyuu.png");
    if !path.ok_is_file() { return; }
    let mut card = crate::card::parse_card_outfit(path, 0).unwrap();
    card.me_copies.clear();
    card.me_shader.clear();
    card.me_color.clear();
    card.me_float.clear();
    let source = encode_png(&image::RgbaImage::from_raw(1, 1,
        vec![255, 0, 0, 255]).unwrap()).unwrap();
    let replacement = encode_png(&image::RgbaImage::from_raw(1, 1,
        vec![0, 255, 0, 255]).unwrap()).unwrap();
    card.me_tex.clear();
    card.me_tex.insert((2, 0, 0, "fixture_material".into(), "colormask".into()),
        std::sync::Arc::new(replacement.clone()));
    let mut meshes = vec![crate::unity::ExtractedMesh {
        name: "fixture_mesh".into(),
        color_mask_png: Some(source),
        runtime_textures: [("_ColorMask".into(), [1, 0, 0, 1, 11])].into(),
        mat: Some(crate::unity::MeshMaterial {
            name: "fixture_material".into(),
            shader: "xukmi/MainItemPlus".into(),
            ..Default::default()
        }),
        ..Default::default()
    }];
    color_accessory_meshes(Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MixedTextureSource"),
        None, &ListEntry::default(), &card, "accessory0", 0, &mut meshes, &[], &[], None);
    assert_eq!(meshes[0].color_mask_png.as_deref(), Some(replacement.as_slice()));
    // MaterialAPI preserves the replaced sampler's wrap, while the
    // TextureContainer RT is sRGB with automatic mip generation. A 1x1
    // replacement has one mip; dropping this metadata was the old path.
    assert_eq!(meshes[0].runtime_textures.get("_ColorMask"), Some(&[1,0,0,1,1]));
}

#[test]
#[ignore="installed Vanilla Plus and original card MaterialEditor shader override"]
fn native_item_head_shader_registration_matches_studio() {
    let game=&crate::test_support::game_path("");
    assert!(me_shader_registered(game,"xukmi/MainItemPlus"),"installed D3D11 block-table shader must be usable");
    let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_380982.png")).unwrap();
    let mut mesh=ExtractedMesh {name:"o_tang".into(),
        mat:Some(crate::unity::MeshMaterial {name:"cf_m_tang".into(),shader:"Shader Forge/main_item".into(),..Default::default()}),
        texture_png:crate::unity::solid_png([1.;4],1,1),
        color_mask_png:crate::unity::solid_png([0.,0.,0.,1.],1,1),..Default::default()};
    resolve_head_detail_texture(game,&ChaListCatalog::default(),&ChaListCatalog::default(),&card,
        &Default::default(),0,&mut mesh,&[],&[]);
    assert_eq!(mesh.mat.as_ref().unwrap().shader,"xukmi/MainItemPlus");
    let expected:[f32;4]=[1.,0.857142866,0.857142866,1.];
    assert_eq!(mesh.mat.as_ref().unwrap().color("_Color"),Some(expected));
    let (blend,unlit)=blend_for_mesh("head",&mesh);
    let part=crate::session::SessionPart::from_mesh("head",&mesh,&blend,unlit);
    assert_eq!(part.shader_kind,9);assert!(part.shader_textures.contains_key("_ColorMask"));
    // This real card replaces tongue MainTex in ME. Native draw must keep
    // those exact input bytes, rather than tinting it with the pink _Color.
    let replacement=me_maintex(Some(&card.me_tex),0,"head",None,&mesh).unwrap();
    assert_eq!(part.shader_textures.get("_MainTex").unwrap().as_slice(),replacement);
    assert_eq!(mesh.runtime_textures.get("_DetailMask"),Some(&[1,0,0,1,1]));
    assert_eq!(mesh.runtime_textures.get("_MainTex"),Some(&[1,0,0,1,1]));
}

#[test]
fn native_item_me_sampling_survives_all_draw_roles() {
    // TextureContainer creates the same sRGB/mipped RT for every property;
    // MaterialAPI.SetTexture copies the previous wrap, including prior RTs.
    // Exercise the real draw assembly rather than just the metadata setter.
    let png=crate::unity::solid_png([0.5,0.25,0.125,0.75],2,2).unwrap();
    let names=["_ColorMask","_DetailMask","_LineMask","_NormalMap",
        "_NormalMapDetail","_AnotherRamp","_EmissionMask","_ReflectionMapCap","_ReflectMapDetail","_Texture2","_Texture3","_liquidmask"];
    for (role,property) in [("accessory","outfit1.accessory7"),("clothes_top","outfit1.clothes0"),
        ("hair_f","ChaFileHair.parts[1]"),("body","")] {
        for wrap in [0,1,2] {
            let mut mesh=ExtractedMesh{name:"sampler_fixture".into(),positions:vec![[0.;3]],
                mat:Some(crate::unity::MeshMaterial{name:"fixture".into(),shader:"xukmi/MainItemPlus".into(),
                    // Previous runtime wrap is meaningful only when the
                    // material actually has a non-null texture binding.
                    tex_slots:vec![("_NormalMap".into(),"bound_normal".into())],
                    // Authored Clamp must lose to the currently bound RT wrap.
                    tex_wraps:names.iter().map(|p|(p.to_string(),1)).collect(),..Default::default()}),
                runtime_textures:names.iter().map(|p|(p.to_string(),[1,wrap,wrap,0,7])).collect(),
                ..Default::default()};
            let object=me_object_type(role).unwrap();let slot=me_slot(role,Some(property)).unwrap();
            let coord=if object==4 {0}else{1};
            let textures:crate::card::MeTexMap=names.iter().map(|p|
                ((object,coord,slot,"fixture".into(),p.trim_start_matches('_').to_ascii_lowercase()),
                std::sync::Arc::new(png.clone()))).collect();
            let mut textures=textures;
            textures.insert((object,coord,slot,"fixture".into(),"alphamask".into()),std::sync::Arc::new(png.clone()));
            // Accessory's preliminary colour pass replaces ColorMask too.
            if role=="accessory" {mesh.color_mask_png=Some(png.clone());mark_material_editor_texture(&mut mesh,"_ColorMask");}
            let mut manifest=Vec::new();let mut session=Vec::new();
            push_meshes(&mut manifest,&mut session,None,Path::new("missing-source-fixture"),role,vec![mesh],
                None,None,None,Some(property.into()),None,None,None,None,None,&mut Default::default(),
                None,None,None,Some(&textures),None,None,None,1,None,None, None, None).unwrap();
            assert_eq!(session.len(),1,"{role}");let part=&session[0];assert_eq!(part.shader_kind,9);
            assert!(!part.shader_textures.contains_key("_AlphaMask"),"MaterialAPI HasProperty rejects absent _AlphaMask");
            for name in names {
                if !native_item_has_texture_property(name) {
                    assert!(!part.shader_textures.contains_key(name),"HasProperty rejects {role} {name}");
                    assert_eq!(part.runtime_textures.get(name),Some(&[1,wrap,wrap,0,7]),"rejected property cannot replace sampler {name}");
                    continue;
                }
                assert_eq!(part.runtime_textures.get(name),Some(&[1,wrap,wrap,1,1]),"{role} {name}");
                assert_eq!(part.shader_textures.get(name),Some(&png),"ME PNG rows/bytes: {role} {name}");
            }
        }
    }
}

#[test]
fn material_editor_rim_remover_preserves_saved_hair_override() {
    assert_eq!(me_slot("hair_b", Some("ChaFileHair.parts[0]")), Some(0));
    assert_eq!(me_slot("hair_f", Some("ChaFileHair.parts[1]")), Some(1));
    assert_eq!(me_slot("hair_s", Some("ChaFileHair.parts[2]")), Some(2));
    assert_eq!(me_slot("hair_o", Some("ChaFileHair.parts[3]")), Some(3));
    let game = &crate::test_support::game_path(r"");
    if game.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg").ok_exists() {
        assert!(material_editor_removes_rim(game));
    }
    let source = crate::unity::ExtractedMesh {
        mat: Some(crate::unity::MeshMaterial {
            name: "cf_m_hair (Instance)".into(),
            shader: "Shader Forge/main_hair".into(),
            ..Default::default()
        }),
        ..Default::default()
    };
    assert!(material_has_rim_property(source.mat.as_ref().unwrap()));
    let opaque_clothes = crate::unity::MeshMaterial { shader: "Koikano/main_clothes_opaque".into(), ..Default::default() };
    assert!(!material_has_rim_property(&opaque_clothes));
    let glasses = crate::unity::MeshMaterial { shader: "Koikano/main_clothes_item_glasses".into(), ..Default::default() };
    assert!(material_has_rim_property(&glasses));
    let forge_glasses = crate::unity::MeshMaterial { shader: "Shader Forge/toon_glasses_lod0".into(), ..Default::default() };
    assert!(!material_has_rim_property(&forge_glasses));
    let emblem = crate::unity::MeshMaterial { shader: "Shader Forge/main_emblem".into(), ..Default::default() };
    assert!(!material_has_rim_property(&emblem));
    let mod_with_unsaved_rim = crate::unity::MeshMaterial {
        shader: "custom/mod_hair".into(), shader_has_rim: Some(true), ..Default::default()
    };
    assert!(material_has_rim_property(&mod_with_unsaved_rim));
    let stale_saved_float = crate::unity::MeshMaterial {
        shader: "custom/mod_item".into(), shader_has_rim: Some(false),
        floats: std::collections::HashMap::from([("_rimV".into(), 0.75)]),
        ..Default::default()
    };
    assert!(!material_has_rim_property(&stale_saved_float));
    let mut no_card_value = source.clone();
    apply_material_editor_remove_rim(true, None, 0, "hair_f", Some("ChaFileHair.parts[1]"), &mut no_card_value);
    assert_eq!(no_card_value.mat.as_ref().unwrap().float("_rimV"), Some(0.0));
    let mut explicit = source.clone();
    explicit.mat.as_mut().unwrap().floats.insert("_rimV".into(), 0.75);
    let mut overrides = crate::card::MeFloatMap::new();
    overrides.insert((3, 0, 1, "cf_m_hair".into(), "rimv".into()), 0.6);
    apply_me_floats(&overrides, 0, "hair_f", Some("ChaFileHair.parts[1]"), &mut explicit);
    assert_eq!(explicit.mat.as_ref().unwrap().float("_rimV"), Some(0.6));
    apply_material_editor_remove_rim(true, Some(&overrides), 0, "hair_f", Some("ChaFileHair.parts[1]"), &mut explicit);
    assert_eq!(explicit.mat.as_ref().unwrap().float("_rimV"), Some(0.6));
    let mut skin = source;
    apply_material_editor_remove_rim(true, None, 0, "head", None, &mut skin);
    assert_eq!(skin.mat.as_ref().unwrap().float("_rimV"), None);
}

#[test]
#[ignore="installed card as seed for synthetic list selection matrix"]
fn top_kind_controls_subparts_and_mask_replacement() {
    let mut card=crate::card::parse_card_outfit(&crate::test_support::game_path("/UserData/chara/female/KK_398302.png"),0).unwrap();
    card.resolves.clear();card.clothes_masks.clear();
    card.clothes_ids=vec![100];card.sub_parts_id=vec![0,0,0];
    let mut catalog=ChaListCatalog::default();
    for category in [105,200,201,202,210,211,212] {
        let id=if category==105 {100}else{0};
        catalog.by_id.insert((category,id),ListEntry {category_no:category,id,
            extra:std::collections::HashMap::from([("Kind".into(),"4".into()),("OverBodyMask".into(),format!("mask{category}"))]),
            ..Default::default()});
    }
    for kind in [0,1,2,3,4,5] {
        catalog.by_id.get_mut(&(105,100)).unwrap().extra.insert("Kind".into(),kind.to_string());
        let mods=ChaListCatalog::default();let index=std::collections::HashMap::new();
        let parts=select_card_top_subparts(&card,0,kind,&catalog,&mods,&index);
        let body=active_clothes_mask_entry(&card,&catalog,&mods,&index,0,"OverBodyMask").unwrap();
        let inner=active_clothes_mask_entry(&card,&catalog,&mods,&index,0,"OverInnerMask").unwrap();
        match kind {
            1 => {assert_eq!(body.category_no,200);assert_eq!(inner.category_no,201);assert!(parts.iter().all(Option::is_some));}
            2 => {assert_eq!(body.category_no,210);assert_eq!(inner.category_no,211);assert!(parts.iter().all(Option::is_some));}
            _ => {assert_eq!(body.category_no,105);assert_eq!(inner.category_no,105);assert!(parts.iter().all(Option::is_none));}
        }
    }
}

#[test]
#[ignore="four independent game material dumps verify active body mask identity"]
fn new_cards_body_mask_matches_game_material_dump() {
    let game=&crate::test_support::game_path("");
    let vanilla=load_characustom_dir(&game.join("abdata/list/characustom"));
    for name in ["KK_226176.png","KK_384914.png","KK_398302.png","弹丸论破 - 七海千秋3.png"] {
        let card=crate::card::parse_card_outfit(&game.join("UserData/chara/female").join(name),0).unwrap();
        let wanted=card.resolves.iter().map(|r|r.mod_id.clone()).collect();
        let index=crate::mods::find_guids(&game.join("mods"),Some(&wanted));
        let source=active_clothes_mask_entry(&card,&vanilla,&ChaListCatalog::default(),&index,0,"OverBodyMask").unwrap();
        let mut files=Vec::new();
        fn collect(dir:&Path,files:&mut Vec<std::path::PathBuf>) {
            for file in crate::gamefs::read_dir(dir).unwrap().flatten() {
                let path=file.path();if path.ok_is_dir() {collect(&path,files);} else if path.to_string_lossy().ends_with(".materials.json") {files.push(path);}
            }
        }
        collect(&game.join("mesh_dump").join(name),&mut files);
        let mut native=Vec::new();
        for file in files {
            let j:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(file).unwrap()).unwrap();
            for m in j["materials"].as_array().unwrap() {
                if !m["shader"].as_str().is_some_and(|s|s=="Koikano/main_skin") {continue;}
                for prop in m["properties"].as_array().unwrap() {
                    if prop["name"]=="_AlphaMask" {
                        if let Some(n)=prop["texture"]["textureName"].as_str().filter(|n|!n.is_empty()) {native.push(n.to_string());}
                    }
                }
            }
        }
        assert!(!native.is_empty(),"native body AlphaMask {name}");
        assert!(native.iter().any(|n|*n==source.info("OverBodyMask")),"{name} selected {} versus native {native:?}",source.info("OverBodyMask"));
        eprintln!("Native mask {name} category={} id={} texture={}",source.category_no,source.id,source.info("OverBodyMask"));
    }
}

#[test]
fn create_eye_matches_native_shader_constant_probe() {
    // Native Koikano/create_eye GPU probe: linear source .75/.5/.25,
    // linear mask .25, colours .5/.25/.75 and .1/.9/.4, blend .6.
    let input=encode_png(&image::RgbaImage::from_pixel(1,1,image::Rgba([to_unorm8(linear_channel_to_srgb(191.0/255.0)),to_unorm8(linear_channel_to_srgb(128.0/255.0)),to_unorm8(linear_channel_to_srgb(64.0/255.0)),204]))).unwrap();
    let grad=encode_png(&image::RgbaImage::from_pixel(1,1,image::Rgba([to_unorm8(linear_channel_to_srgb(64.0/255.0)),0,0,255]))).unwrap();
    let pupil=crate::card::PupilInfo {base_color:[0.5,0.25,0.75,1.0],sub_color:[0.1,0.9,0.4,1.0],grad_blend:0.6,..Default::default()};
    let output=bake_iris(&input,&pupil,Some(&grad),None,None,[1.0;4],[1.0;4],0.5,0.5).unwrap();
    let image=crate::teximg::load_from_memory(&output).unwrap().into_rgba8();let pixel=image.get_pixel(256,256).0;
    for (actual,expected) in pixel.iter().zip([85u8,185,42,204]) {assert!((*actual as i16-expected as i16).abs()<=2,"native shader {pixel:?}");}
}

#[test]
#[ignore="native face GPU buffer: OK_SKIN_CARD and OK_SKIN_NATIVE_GPU_JSON"]
fn native_face_overlay_colors_reach_c_abi() {
    let game=crate::test_support::game_path("");let card=std::path::PathBuf::from(std::env::var("OK_SKIN_CARD").unwrap());
    let native:serde_json::Value=serde_json::from_slice(&std::fs::read(std::env::var("OK_SKIN_NATIVE_GPU_JSON").unwrap()).unwrap()).unwrap();
    let cb=native["buffers"].as_array().unwrap().iter().find(|b|b["stage"]=="ps"&&b["slot"]==0).unwrap()["float4"].as_array().unwrap();
    let mut session=crate::session::open(&game,Some(&card),0).unwrap();
    let index=session.parts.iter().position(|p|p.role=="head"&&p.name=="cf_O_face").unwrap();
    let mut view=std::mem::MaybeUninit::<crate::ffi::OkMeshView>::uninit();
    assert_eq!(unsafe{crate::ffi::part_at(&mut session,index as i32,view.as_mut_ptr())},0);let view=unsafe{view.assume_init()};
    for (ours,row) in [(view.overcolor,7usize),(view.overcolor2,9usize)]{
        for c in 0..4{let value=if c<3{srgb_channel_to_linear(ours[c])}else{ours[c]};let expected=cb[row][c].as_f64().unwrap() as f32;
            assert!((value-expected).abs()<0.000001,"native face overlay {row}/{c}: {value} != {expected}");}
    }
}

#[test]
#[ignore="native hair material color: OK_HAIR_CARD, OK_HAIR_MESH, OK_HAIR_NATIVE_COLOR"]
fn resolved_hair_gloss_color_matches_native_material() {
    let game=crate::test_support::game_path("");
    let card=std::path::PathBuf::from(std::env::var("OK_HAIR_CARD").unwrap());
    let mesh=std::env::var("OK_HAIR_MESH").unwrap();
    let expected:Vec<f32>=serde_json::from_str(&std::env::var("OK_HAIR_NATIVE_COLOR").unwrap()).unwrap();
    assert_eq!(expected.len(),4);
    let session=crate::session::open(&game,Some(&card),0).unwrap();
    let part=session.parts.iter().find(|p|p.name==mesh).expect("native renderer absent");
    let color=part.source_material.as_ref().unwrap().colors.get("_GlossColor").expect("resolved gloss absent");
    for c in 0..4{assert!((color[c]-expected[c]).abs()<0.000001,"native gloss channel {c}: {} != {}",color[c],expected[c]);}
}

#[test]
#[ignore="native hair texture readback: OK_HAIR_CARD, OK_HAIR_MESH, OK_HAIR_NATIVE_GLOSS"]
fn selected_hair_gloss_matches_native_input() {
    let game=crate::test_support::game_path("");
    let card=std::path::PathBuf::from(std::env::var("OK_HAIR_CARD").expect("OK_HAIR_CARD"));
    let mesh=std::env::var("OK_HAIR_MESH").expect("OK_HAIR_MESH");
    let native=std::env::var("OK_HAIR_NATIVE_GLOSS").expect("OK_HAIR_NATIVE_GLOSS");
    let session=crate::session::open(&game,Some(&card),0).unwrap();
    let part=session.parts.iter().find(|p|p.name==mesh).expect("selected native renderer");
    let ours=crate::teximg::load_from_memory(part.hair_gloss.as_ref().expect("selected gloss texture")).unwrap().into_rgba8();
    let theirs=image::open(native).unwrap().to_rgba8();
    assert_eq!(ours.dimensions(),theirs.dimensions());
    let mut total=0u64;
    let mut maximum=0u8;
    for (a,b) in ours.pixels().zip(theirs.pixels()) {
        for c in 0..3 {let d=a[c].abs_diff(b[c]);total+=u64::from(d);maximum=maximum.max(d);}
    }
    let mae=total as f64/(ours.width() as f64*ours.height() as f64*3.);
    eprintln!("native hair gloss mesh={} RGB MAE={mae} max={maximum} UV0={} UV1={}",part.name,part.uvs.len(),part.uv2.len());
    assert!(mae<2. && maximum<=8,"native gloss input differs: MAE={mae} max={maximum}");
}

#[test]
#[ignore="native selected-card create_eye readback: OK_IRIS_CARD and OK_IRIS_NATIVE_PREFIX"]
fn selected_iris_create_rt_matches_native() {
    let game=crate::test_support::game_path("");
    let card=std::path::PathBuf::from(std::env::var("OK_IRIS_CARD").expect("OK_IRIS_CARD"));
    let prefix=std::env::var("OK_IRIS_NATIVE_PREFIX").expect("OK_IRIS_NATIVE_PREFIX");
    let session=crate::session::open(&game,Some(&card),0).unwrap();
    let mut rows=Vec::new();
    for part in session.parts.iter().filter(|p|p.eye_draw.as_ref().is_some_and(|d|d.kind==1)) {
        let side=usize::from(part.name.to_ascii_lowercase().contains("_r"));
        let native=image::open(format!("{prefix}.eye{side}.pure.png")).unwrap().to_rgba8();
        let actual=crate::teximg::load_from_memory(part.png.as_ref().expect("resolved iris RT")).unwrap().into_rgba8();
        assert_eq!(native.dimensions(),actual.dimensions());
        let mut error=0u64;
        let mut count=0u64;
        let mut coverage=0u64;
        for (n,a) in native.pixels().zip(actual.pixels()) {
            if (n[3]>=128)!=(a[3]>=128){
                coverage+=1;
            }
            if n[3]>=128 {
                for c in 0..3{
                    error+=n[c].abs_diff(a[c]) as u64;
                    count+=1;
                }
            }
        }
        let mae=error as f64/count.max(1) as f64;
        eprintln!("IRIS_NATIVE part={} rgb_mae_255={mae:.6} coverage_differences={coverage}",part.name);
        rows.push(serde_json::json!({"part":part.name,"rgb_mae_255":mae,"coverage_differences":coverage}));
        assert!(count>1000 && mae<2.0 && coverage<32,"selected iris RT differs from native: {} mae={mae} coverage={coverage}",part.name);
    }
    assert_eq!(rows.len(),2,"both actual iris renderers required");
    if let Ok(path)=std::env::var("OK_IRIS_REPORT"){
        std::fs::write(path,serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_face_maps() {
    let path =
        &crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    if !path.ok_is_file() {
        return;
    }
    for n in [
        "cf_face_00_md",
        "cf_face_00_md2",
        "cf_face_00_mc",
        "cf_face_00_ml",
        "cf_face_00_t",
    ] {
        let bytes = crate::gamefs::read(path).unwrap();
        match crate::unity::named_png_from_bytes(bytes, &[n]) {
            Some((name, png)) => {
                let out =
                    format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\{}.png", n);
                let _ = std::fs::write(&out, &png);
                eprintln!("DUMP {n} -> {name} {} bytes", png.len());
            }
            None => eprintln!("DUMP {n} NOT FOUND"),
        }
    }
}

#[test]
fn hash_check() {
    fn unity_hash(s: &str) -> u32 {
        let mut h: u32 = 0;
        for c in s.chars() {
            let v = c as u32;
            h = v.wrapping_add(h << 6).wrapping_add(h << 16).wrapping_sub(h);
        }
        h
    }
    for n in ["cf_j_bnip02_L", "cf_s_leg01_L", "cf_s_bust03_L"] {
        eprintln!("HASH {} = {}", n, unity_hash(n));
    }
}

#[test]
fn list_vanilla_body() {
    let p = &crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    if !p.ok_exists() {
        return;
    }
    let (meshes, _skel) = crate::unity::meshes_and_skeleton_from_path(p);
    for m in &meshes {
        if true {
            let mut mx = [f32::MIN; 3];
            for v in &m.positions {
                for i in 0..3 {
                    mx[i] = mx[i].max(v[i]);
                }
            }
            eprintln!(
                "VBODY {} verts={} binds={} zmax={:.2}",
                m.name,
                m.positions.len(),
                m.bind_poses.len(),
                mx[2]
            );
        }
    }
}

#[test]
fn load_sac_body() {
    let p = std::path::Path::new(
        r"C:\Users\Damody\AppData\Local\Temp\opencode\sac\oo_base_sacinnie.unity3d",
    );
    if !p.ok_exists() {
        return;
    }
    let (meshes, _tex, skel) = crate::unity::meshes_skel_from_memory(crate::gamefs::read(p).unwrap());
    for m in &meshes {
        let mut mn = [f32::MAX; 3];
        let mut mx = [f32::MIN; 3];
        for v in &m.positions {
            for i in 0..3 {
                mn[i] = mn[i].min(v[i]);
                mx[i] = mx[i].max(v[i]);
            }
        }
        eprintln!(
            "SAC mesh {} verts={} weights={} binds={} aabb=({:.2},{:.2},{:.2})-({:.2},{:.2},{:.2})",
            m.name,
            m.positions.len(),
            m.weights.len(),
            m.bind_poses.len(),
            mn[0],
            mn[1],
            mn[2],
            mx[0],
            mx[1],
            mx[2]
        );
    }
    if let Some(s) = &skel {
        eprintln!(
            "SAC skel bones={} first={:?}",
            s.bone_ids.len(),
            s.names.iter().take(10).collect::<Vec<_>>()
        );
    } else {
        eprintln!("SAC skel none");
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn real_card_body_keeps_texture_detail() {
    let game = crate::test_support::game_path(r"");
    let card = std::env::var("OK_CARD")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png")
        });
    if !game.join("abdata").ok_is_dir() || !card.ok_exists() {
        return;
    }
    let out = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_user_check");
    extract_card(&game, Some(&card), &out).expect("extract");
    let obj = crate::gamefs::read_to_string(out.join("body.obj")).expect("body.obj");
    let mut mn = [f32::MAX; 3];
    let mut mx = [f32::MIN; 3];
    for line in obj.lines() {
        if !line.starts_with("v ") {
            continue;
        }
        let mut it = line.split_whitespace().skip(1);
        let p = [
            it.next().and_then(|s| s.parse().ok()).unwrap_or(0.0),
            it.next().and_then(|s| s.parse().ok()).unwrap_or(0.0),
            it.next().and_then(|s| s.parse().ok()).unwrap_or(0.0),
        ];
        for i in 0..3 {
            mn[i] = mn[i].min(p[i]);
            mx[i] = mx[i].max(p[i]);
        }
    }
    let xspan = mx[0] - mn[0];
    eprintln!(
        "ok-extract: body aabb x=[{:.3},{:.3}] y=[{:.3},{:.3}] z=[{:.3},{:.3}] xspan={xspan:.3}",
        mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]
    );
    assert!(xspan > 0.35, "body collapsed x=[{:.2},{:.2}]", mn[0], mx[0]);
    assert!(
        mn[1] > -0.25,
        "feet should sit near ground y=[{:.2},{:.2}]",
        mn[1],
        mx[1]
    );
    assert!(
        mx[1] > 1.15,
        "body top should reach the neck y=[{:.2},{:.2}]",
        mn[1],
        mx[1]
    );
    assert!(mx[1] - mn[1] > 1.2, "body too short y=[{:.2},{:.2}]", mn[1], mx[1]);
    let zspan = mx[2] - mn[2];
    assert!(zspan < 0.7, "body exploded in z zspan={zspan:.3}");
    assert!(
        xspan < 1.55,
        "body too wide (exploded or leftover T-pose) xspan={xspan:.3}"
    );
    let png = crate::gamefs::read(out.join("body.png")).expect("body.png");
    assert!(
        png.len() > 20_000,
        "body.png too small (solid fallback?) {}",
        png.len()
    );
    let img = crate::teximg::load_from_memory(&png).expect("png").to_rgba8();
    let mut rsum = 0u64;
    let mut gsum = 0u64;
    let mut bsum = 0u64;
    let mut n = 0u64;
    let mut minl = 255u8;
    let mut maxl = 0u8;
    for p in img.pixels() {
        rsum += p[0] as u64;
        gsum += p[1] as u64;
        bsum += p[2] as u64;
        n += 1;
        let l = p[0].max(p[1]).max(p[2]);
        minl = minl.min(l);
        maxl = maxl.max(l);
    }
    assert!(n > 0, "empty body.png");
    let _mean = (rsum / n, gsum / n, bsum / n);
    assert!(
        maxl > minl + 6,
        "body.png still flat luma {minl}..{maxl}"
    );
    assert!(out.join("head.png").ok_exists(), "missing head.png");
    let has_hair = crate::gamefs::read_dir(&out)
        .unwrap()
        .flatten()
        .any(|e| e.file_name().to_string_lossy().contains("hair") || e.file_name().to_string_lossy().contains("kami"));
    assert!(has_hair, "no hair/kami mesh exported");
    let man = crate::gamefs::read_to_string(out.join("manifest.json")).expect("manifest");
    assert!(man.contains("\"role\": \"body\""));
    assert!(man.contains("\"role\": \"head\""));
    let t0 = std::time::Instant::now();
    extract_card(&game, Some(&card), &out).expect("cached extract");
    let ms = t0.elapsed().as_millis();
    eprintln!("ok-extract: stamp skip {ms}ms");
    assert!(ms < 4000, "cache miss, extract took {ms}ms");
}

fn aabb_of(pos: &[[f32; 3]]) -> String {
    let (mn, mx) = crate::pose::aabb(pos);
    format!(
        "x=[{:.3},{:.3}] y=[{:.3},{:.3}] z=[{:.3},{:.3}] span=({:.3},{:.3},{:.3})",
        mn[0], mx[0], mn[1], mx[1], mn[2], mx[2],
        mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]
    )
}

fn pal_stats(posed: &[crate::pose::Mat4], binds: &[crate::pose::Mat4]) -> String {
    let n = posed.len().min(binds.len());
    let mut max_off = 0.0f32;
    let mut max_rot = 0.0f32;
    let mut worst = 0usize;
    for i in 0..n {
        let pal = crate::pose::mat_mul_pub(&posed[i], &binds[i]);
        let off = pal[0][3].abs() + pal[1][3].abs() + pal[2][3].abs();
        let rot = (pal[0][0] - 1.0).abs()
            + (pal[1][1] - 1.0).abs()
            + (pal[2][2] - 1.0).abs()
            + pal[0][1].abs()
            + pal[1][0].abs();
        if off + rot > max_off + max_rot {
            max_off = off;
            max_rot = rot;
            worst = i;
        }
    }
    format!("n={n} worst={worst} trans_l1={max_off:.4} rot_l1={max_rot:.4}")
}

fn bind_vs_inv_rest(rest: &[crate::pose::Mat4], binds: &[crate::pose::Mat4]) -> String {
    let n = rest.len().min(binds.len());
    let mut max_d = 0.0f32;
    let mut max_dt = 0.0f32;
    for i in 0..n {
        let inv = crate::pose::mat_inv_pub(&rest[i]);
        let mut d = 0.0f32;
        let mut dt = 0.0f32;
        for r in 0..4 {
            for c in 0..4 {
                d += (inv[r][c] - binds[i][r][c]).abs();
                dt += (inv[r][c] - binds[i][c][r]).abs();
            }
        }
        max_d = max_d.max(d);
        max_dt = max_dt.max(dt);
    }
    format!("inv(rest)-bind maxL1={max_d:.4} inv(rest)-bind^T maxL1={max_dt:.4}")
}

fn parent_chain(skel: &crate::pose::Skeleton, mut id: i64) -> String {
    let mut parts = Vec::new();
    for _ in 0..24 {
        let Some(tf) = skel.tfs.get(&id) else { break };
        parts.push(format!("{}@({:.3},{:.3},{:.3})", tf.name, tf.pos[0], tf.pos[1], tf.pos[2]));
        let Some(p) = tf.parent else { break };
        if p == 0 || p == id {
            break;
        }
        id = p;
    }
    parts.join(" <- ")
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn mesh_pipeline_diag() {
    let game = crate::test_support::game_path(r"");
    let card = crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
    if !game.join("abdata").ok_is_dir() {
        return;
    }
    let body = game.join("abdata/chara/oo_base.unity3d");
    let (meshes, skel, _) = crate::unity::load_base_bundle(&body);
    let mesh = crate::unity::pick_named(&meshes, "o_body_a")
        .cloned()
        .expect("o_body_a");
    let mut skel = skel.expect("body skel");
    eprintln!(
        "DIAG mesh verts={} weights={} bind={} smr={} worlds={} rest={} tfs={}",
        mesh.positions.len(),
        mesh.weights.len(),
        mesh.bind_poses.len(),
        skel.smr_count,
        skel.worlds.len(),
        skel.rest.len(),
        skel.tfs.len()
    );
    eprintln!("DIAG raw {}", aabb_of(&mesh.positions));
    eprintln!("DIAG smr_names={:?}", skel.names);
    let mut max_bi = 0u16;
    let mut n_oob = 0usize;
    let mut n_foot_oob = 0usize;
    let mut n_foot = 0usize;
    for (i, b) in mesh.bone_idx.iter().enumerate() {
        let mut oob = false;
        for k in 0..4 {
            if mesh.weights[i][k].abs() < 1e-6 {
                continue;
            }
            max_bi = max_bi.max(b[k]);
            if (b[k] as usize) >= 100 {
                oob = true;
            }
        }
        if oob {
            n_oob += 1;
        }
        if mesh.positions[i][1] < -1.0 {
            n_foot += 1;
            if oob {
                n_foot_oob += 1;
            }
        }
    }
    let mut n_w1 = 0usize;
    let mut n_wlow = 0usize;
    let mut max_sum = 0.0f32;
    let mut min_sum = f32::MAX;
    for w in &mesh.weights {
        let s = w[0] + w[1] + w[2] + w[3];
        max_sum = max_sum.max(s);
        min_sum = min_sum.min(s);
        if s > 0.5 {
            n_w1 += 1;
        } else {
            n_wlow += 1;
        }
    }
    eprintln!(
        "DIAG max_bone_idx={max_bi} oob_verts={n_oob} foot={n_foot} foot_oob={n_foot_oob} idx0={:?} w0={:?} wsum_ok={n_w1} wsum_low={n_wlow} wsum=[{min_sum:.3},{max_sum:.3}]",
        mesh.bone_idx.first(),
        mesh.weights.first()
    );
    if let Some(i) = mesh.positions.iter().position(|p| p[1] < -1.0) {
        eprintln!("DIAG foot i={i} p={:?} w={:?} b={:?}", mesh.positions[i], mesh.weights[i], mesh.bone_idx[i]);
    }
    if let Some(i) = mesh.positions.iter().enumerate().max_by(|a, b| a.1[1].partial_cmp(&b.1[1]).unwrap()).map(|(i, _)| i) {
        eprintln!("DIAG top i={i} p={:?} w={:?} b={:?}", mesh.positions[i], mesh.weights[i], mesh.bone_idx[i]);
    }
    let nbp = mesh.bind_poses.len().min(skel.rest.len());
    if nbp > 0 {
        let bp = &mesh.bind_poses[0];
        let inv0 = crate::pose::mat_inv_pub(&skel.rest[0]);
        eprintln!(
            "DIAG bind0 t={:?} row0={:?} inv_rest0 t={:?} row0={:?}",
            crate::pose::mat_pos(bp),
            bp[0],
            crate::pose::mat_pos(&inv0),
            inv0[0]
        );
        eprintln!(
            "DIAG filebind[0..{nbp}] vs inv(rest[0..]) {}",
            bind_vs_inv_rest(&skel.rest[..nbp], &mesh.bind_poses[..nbp])
        );
        if skel.rest.len() > nbp {
            eprintln!(
                "DIAG filebind vs inv(rest[1..]) {}",
                bind_vs_inv_rest(&skel.rest[1..1 + nbp], &mesh.bind_poses[..nbp])
            );
        }
        if let Some(p) = crate::pose::skin_positions(
            &mesh,
            &skel.worlds[..nbp],
            &mesh.bind_poses[..nbp],
        ) {
            eprintln!("DIAG skin rest*filebind n={nbp} {}", aabb_of(&p));
        }
    }
    if let Some(hips) = skel.world("cf_j_hips").or_else(|| skel.world_tf("cf_j_hips")) {
        let hp = crate::pose::mat_pos(&hips);
        let mut p = mesh.positions.clone();
        for v in &mut p {
            v[0] += hp[0];
            v[1] += hp[1];
            v[2] += hp[2];
        }
        eprintln!("DIAG raw+hips {:?} {}", hp, aabb_of(&p));
        let nn = skel.worlds.len().min(skel.rest.len());
        if let Some(skinned) = crate::pose::skin_positions(
            &mesh,
            &skel.worlds[..nn],
            &crate::pose::binds_from_rest(&skel.rest[..nn]),
        ) {
            let mut q = skinned;
            crate::pose::apply_mat(&mut q, &hips);
            eprintln!("DIAG palI * hips_mat {}", aabb_of(&q));
        }
    }
    let n = skel.worlds.len().min(skel.rest.len());
    let file_binds = if mesh.bind_poses.len() >= n {
        mesh.bind_poses[..n].to_vec()
    } else {
        crate::pose::binds_from_rest(&skel.rest[..n])
    };
    eprintln!(
        "DIAG rest*file_bind {}",
        pal_stats(&skel.rest[..n], &file_binds)
    );
    eprintln!(
        "DIAG rest*inv(rest) {}",
        pal_stats(&skel.rest[..n], &crate::pose::binds_from_rest(&skel.rest[..n]))
    );
    eprintln!("DIAG {}", bind_vs_inv_rest(&skel.rest[..n], &file_binds));
    if let Some(p) = crate::pose::skin_positions(&mesh, &skel.worlds[..n], &file_binds) {
        eprintln!("DIAG skin rest+file {}", aabb_of(&p));
    }
    if let Some(p) = crate::pose::skin_positions(
        &mesh,
        &skel.worlds[..n],
        &crate::pose::binds_from_rest(&skel.rest[..n]),
    ) {
        eprintln!("DIAG skin rest+inv(rest) {}", aabb_of(&p));
    }

    let mut name_n: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    for tf in skel.tfs.values() {
        *name_n.entry(tf.name.clone()).or_insert(0) += 1;
    }
    let mut dups: Vec<(String, usize)> = name_n
        .iter()
        .filter(|(_, c)| **c > 1)
        .map(|(k, v)| (k.clone(), *v))
        .collect();
    dups.sort_by(|a, b| b.1.cmp(&a.1));
    eprintln!(
        "DIAG dup names top {:?}",
        dups.iter().take(12).collect::<Vec<_>>()
    );

    let roots: Vec<_> = skel
        .tfs
        .iter()
        .filter(|(_, t)| t.name == "cf_j_root")
        .map(|(id, t)| {
            format!(
                "id={id} pos={:?} scl={:?} tree={}",
                t.pos,
                t.scale,
                crate::pose::subtree_ids_pub(*id, &skel.tfs).len()
            )
        })
        .collect();
    eprintln!("DIAG cf_j_root {roots:?}");
    if let Some(aid) = crate::pose::animator_root_id(&skel) {
        eprintln!(
            "DIAG animator_root id={aid} name={} tree={}",
            skel.tfs.get(&aid).map(|t| t.name.as_str()).unwrap_or("?"),
            crate::pose::subtree_ids_pub(aid, &skel.tfs).len()
        );
    }
    for want in [
        "cf_n_height",
        "cf_s_head",
        "cf_s_spine01",
        "cf_j_arm00_L",
        "cf_s_shoulder02_L",
        "cf_j_root",
    ] {
        for (id, tf) in &skel.tfs {
            if tf.name == want {
                eprintln!("DIAG chain {want} id={id} {}", parent_chain(&skel, *id));
            }
        }
        if let Some(m) = skel.world(want).or_else(|| skel.world_tf(want)) {
            eprintln!("DIAG smr-or-tf {want} world_pos={:?}", crate::pose::mat_pos(&m));
        }
    }

    if let Some(aroot) = crate::pose::animator_root_id(&skel) {
        let mut dict: std::collections::HashMap<String, i64> = std::collections::HashMap::new();
        for id in crate::pose::subtree_ids_pub(aroot, &skel.tfs) {
            if let Some(tf) = skel.tfs.get(&id) {
                dict.entry(tf.name.clone()).or_insert(id);
            }
        }
        let mut ncmp = 0usize;
        let mut nmiss = 0usize;
        let mut nsame = 0usize;
        let mut max_d = 0.0f32;
        let mut worst = String::new();
        for (i, id) in skel.bone_ids.iter().enumerate() {
            let Some(tf) = skel.tfs.get(id) else { continue };
            let Some(&nid) = dict.get(&tf.name) else {
                nmiss += 1;
                continue;
            };
            if nid == *id {
                nsame += 1;
                continue;
            }
            ncmp += 1;
            let wm = crate::pose::mat_pos(
                &skel.rest.get(i).copied().unwrap_or_else(crate::pose::ident),
            );
            let wa = crate::pose::mat_pos(&crate::pose::world_of_id(nid, &skel.tfs));
            let d = (wm[0] - wa[0]).abs() + (wm[1] - wa[1]).abs() + (wm[2] - wa[2]).abs();
            if d > max_d {
                max_d = d;
                worst = format!("{} mesh={:?} anim={:?}", tf.name, wm, wa);
            }
        }
        eprintln!(
            "DIAG mesh-vs-anim ncmp={ncmp} nsame={nsame} nmiss={nmiss} max_pos_l1={max_d:.4} worst={worst}"
        );
        for want in ["cf_n_height", "cf_s_head", "cf_j_arm00_L", "cf_s_shoulder02_L"] {
            let mesh_id = skel
                .bone_ids
                .iter()
                .find(|id| skel.tfs.get(id).map(|t| t.name.as_str()) == Some(want))
                .copied();
            let anim_id = dict.get(want).copied();
            let mp = mesh_id.map(|id| crate::pose::mat_pos(&crate::pose::world_of_id(id, &skel.tfs)));
            let ap = anim_id.map(|id| crate::pose::mat_pos(&crate::pose::world_of_id(id, &skel.tfs)));
            eprintln!("DIAG pair {want} mesh_id={mesh_id:?} anim_id={anim_id:?} mesh_w={mp:?} anim_w={ap:?}");
        }
    }

    if card.ok_exists() {
        let c = crate::card::parse_card(&card).expect("card");
        let correct = crate::shape::load_correct_info(&game);
        crate::shape::apply_body_shape(
            &game,
            &mut skel,
            &c.shape_body,
            &correct,
            c.sex,
            c.type_bone,
            c.nip_stand_rate,
        );
        let n = skel.worlds.len().min(skel.rest.len());
        let file_binds = if mesh.bind_poses.len() >= n {
            mesh.bind_poses[..n].to_vec()
        } else {
            crate::pose::binds_from_rest(&skel.rest[..n])
        };
        eprintln!(
            "DIAG after-shape rest*file {}",
            pal_stats(&skel.rest[..n], &file_binds)
        );
        eprintln!(
            "DIAG after-shape world*file {}",
            pal_stats(&skel.worlds[..n], &file_binds)
        );
        if let Some(p) = crate::pose::skin_positions(&mesh, &skel.worlds[..n], &file_binds) {
            eprintln!("DIAG skin shape+file {}", aabb_of(&p));
        }
        if let Some(p) = crate::pose::skin_positions(
            &mesh,
            &skel.worlds[..n],
            &crate::pose::binds_from_rest(&skel.rest[..n]),
        ) {
            eprintln!("DIAG skin shape+inv(orig rest) {}", aabb_of(&p));
        }
        if let Some(p) = crate::pose::skin_positions(
            &mesh,
            &skel.worlds[..n],
            &crate::pose::binds_from_rest(&skel.worlds[..n]),
        ) {
            eprintln!("DIAG skin shape+inv(shaped worlds) {}", aabb_of(&p));
        }

        let mut sk2 = skel.clone();
        crate::pose::assigned_weights_remap(&mut sk2);
        let n2 = sk2.worlds.len().min(sk2.rest.len());
        let fb = if mesh.bind_poses.len() >= n2 {
            mesh.bind_poses[..n2].to_vec()
        } else {
            crate::pose::binds_from_rest(&sk2.rest[..n2])
        };
        eprintln!("DIAG after-AW world*file {}", pal_stats(&sk2.worlds[..n2], &fb));
        eprintln!(
            "DIAG after-AW world*inv(AW rest) {}",
            pal_stats(&sk2.worlds[..n2], &crate::pose::binds_from_rest(&sk2.rest[..n2]))
        );
        if let Some(p) = crate::pose::skin_positions(&mesh, &sk2.worlds[..n2], &fb) {
            eprintln!("DIAG skin AW+shape+file {}", aabb_of(&p));
        }
        if let Some(p) = crate::pose::skin_positions(
            &mesh,
            &sk2.worlds[..n2],
            &crate::pose::binds_from_rest(&sk2.rest[..n2]),
        ) {
            eprintln!("DIAG skin AW+shape+inv(AW rest) {}", aabb_of(&p));
        }

        crate::anim::apply_mannequin_pose(&game, &mut sk2);
        let n2 = sk2.worlds.len().min(sk2.rest.len());
        let fb = if mesh.bind_poses.len() >= n2 {
            mesh.bind_poses[..n2].to_vec()
        } else {
            crate::pose::binds_from_rest(&sk2.rest[..n2])
        };
        if let Some(m) = sk2.world("cf_j_arm00_L").or_else(|| sk2.world_tf("cf_j_arm00_L")) {
            eprintln!("DIAG posed arm00_L {:?}", crate::pose::mat_pos(&m));
        }
        if let Some(m) = sk2
            .world("cf_s_shoulder02_L")
            .or_else(|| sk2.world_tf("cf_s_shoulder02_L"))
        {
            eprintln!("DIAG posed shoulder02_L {:?}", crate::pose::mat_pos(&m));
        }
        if let Some(p) = crate::pose::skin_positions(&mesh, &sk2.worlds[..n2], &fb) {
            eprintln!("DIAG skin AW+clip+file {}", aabb_of(&p));
        }
        if let Some(p) = crate::pose::skin_positions(
            &mesh,
            &sk2.worlds[..n2],
            &crate::pose::binds_from_rest(&sk2.rest[..n2]),
        ) {
            eprintln!("DIAG skin AW+clip+inv(AW rest) {}", aabb_of(&p));
        }
        let nb = mesh.bind_poses.len().min(sk2.worlds.len());
        if let Some(p) = crate::pose::skin_positions(
            &mesh,
            &sk2.worlds[..nb],
            &mesh.bind_poses[..nb],
        ) {
            eprintln!("DIAG skin AW+clip+filebind n={nb} {}", aabb_of(&p));
        }
        let mut valid_max = 0u16;
        let mut n_valid = 0usize;
        let mut n_none = 0usize;
        for b in &mesh.bone_idx {
            let mut any = false;
            for k in 0..4 {
                if (b[k] as usize) < 200 {
                    valid_max = valid_max.max(b[k]);
                    any = true;
                }
            }
            if any {
                n_valid += 1;
            } else {
                n_none += 1;
            }
        }
        eprintln!("DIAG bone_idx valid_max={valid_max} verts_with_valid={n_valid} verts_none={n_none}");
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn kk_091207_extract_is_fast() {
    let game = crate::test_support::game_path(r"");
    let card =
        crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
    if !game.join("abdata").ok_is_dir() || !card.ok_is_file() {
        return;
    }
    crate::unity::clear_bundle_cache();
    std::env::set_var("OK_PROFILE", "1");
    let t0 = Instant::now();
    let sess = extract_session(&game, Some(&card), 0, None).expect("session KK_091207");
    let ms = t0.elapsed().as_millis();
    std::env::remove_var("OK_PROFILE");
    eprintln!(
        "ok-extract: KK_091207 session {ms}ms parts={}",
        sess.parts.len()
    );
    assert!(sess.parts.len() >= 10, "too few parts {}", sess.parts.len());
    assert!(
        ms < 20000,
        "KK_091207 session too slow: {ms}ms (budget 20000ms, was ~60000ms)"
    );
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn deathweasel_panties_slot1_is_tanga() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\DeathWeasel AKA Anon11\[DeathWeasel] Panties v1.3.2.zipmod"),
    );
    if !zip.ok_is_file() {
        return;
    }
    let cat = crate::list::catalog_from_zipmod(&zip);
    let e = cat.get(108, 1).expect("cat 108 id 1");
    assert!(
        e.name.to_ascii_lowercase().contains("tanga"),
        "slot1={}",
        e.name
    );
    let e9 = cat.get(108, 9).expect("cat 108 id 9");
    assert!(
        e9.name.to_ascii_lowercase().contains("lowrise"),
        "slot9={}",
        e9.name
    );
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn deathweasel_panties_guid_points_at_existing_zip() {
    let mods = crate::test_support::game_path(r"\mods");
    if !mods.ok_is_dir() {
        return;
    }
    let mut wanted = std::collections::HashSet::new();
    wanted.insert("com.DeathWeasel.Panties".into());
    let map = crate::mods::find_guids(&mods, Some(&wanted));
    let p = map
        .get("com.DeathWeasel.Panties")
        .expect("guid indexed");
    assert!(p.ok_is_file(), "stale guid path {}", p.display());
    let cat = crate::list::catalog_from_zipmod(p);
    let e = cat.get(108, 1).expect("tanga");
    assert!(e.name.to_ascii_lowercase().contains("tanga"), "{}", e.name);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn kkex_clothes_does_not_fallback_to_vanilla_same_slot() {
    let card = PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
    );
    if !card.ok_is_file() {
        return;
    }
    let c = crate::card::parse_card_outfit(&card, 2).expect("outfit2");
    let r = c
        .resolves
        .iter()
        .find(|r| r.property == "outfit2.ChaFileClothes.ClothesShorts")
        .expect("KKEx shorts");
    assert_eq!(r.mod_id, "com.DeathWeasel.Panties");
    assert_eq!(r.slot, 1);
    assert_eq!(c.clothes_ids.get(3).copied().unwrap_or(0), 1);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn find_guids_wanted_skips_unused_zips() {
    let mods = crate::test_support::game_path(r"\mods");
    if !mods.ok_is_dir() {
        return;
    }
    let mut wanted = std::collections::HashSet::new();
    wanted.insert("does.not.exist.guid.okextract".into());
    let t0 = std::time::Instant::now();
    let map = crate::mods::find_guids(&mods, Some(&wanted));
    let ms = t0.elapsed().as_millis();
    eprintln!("ok-extract: find_guids wanted empty {ms}ms n={}", map.len());
    assert!(ms < 8000, "walked unused zipmods {ms}ms");
    assert!(map.is_empty());
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn profile_shape_morph_speed() {
    let game = crate::test_support::game_path(r"");
    if !game.join("abdata").ok_is_dir() {
        return;
    }
    let card = std::env::var("OK_CARD")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png")
        });
    if !card.ok_is_file() {
        eprintln!("PROFILE skip: no card {}", card.display());
        return;
    }
    let c = crate::card::parse_card(&card).expect("card");
    eprintln!(
        "PROFILE card shape_body={} shape_face={}",
        c.shape_body.len(),
        c.shape_face.len()
    );

    let t = Instant::now();
    let (body_meshes, body_skel, _) =
        load_base_bundle(&game.join("abdata/chara/oo_base.unity3d"));
    eprintln!("PROFILE load_base_bundle body {:.1}ms meshes={}", t.elapsed().as_secs_f64() * 1000.0, body_meshes.len());
    let Some(mut body_skel) = body_skel else {
        panic!("no body skeleton");
    };
    let t = Instant::now();
    let (_head_meshes, head_skel, _) =
        load_base_bundle(&game.join("abdata/chara/bo_head_00.unity3d"));
    eprintln!("PROFILE load_base_bundle head {:.1}ms", t.elapsed().as_secs_f64() * 1000.0);
    let Some(mut head_skel) = head_skel else {
        panic!("no head skeleton");
    };

    let t = Instant::now();
    let correct = crate::shape::load_correct_info(&game);
    eprintln!("PROFILE load_correct_info {:.1}ms n={}", t.elapsed().as_secs_f64() * 1000.0, correct.len());

    let t = Instant::now();
    let body_keys = crate::shape::load_keys_named(&game, "chara/oo_base.unity3d", "cf_anmShapeBody");
    eprintln!(
        "PROFILE load_keys cf_anmShapeBody {:.1}ms keys={}",
        t.elapsed().as_secs_f64() * 1000.0,
        body_keys.len()
    );
    let t = Instant::now();
    let head_keys = crate::shape::load_head_keys(&game);
    eprintln!(
        "PROFILE load_head_keys {:.1}ms keys={}",
        t.elapsed().as_secs_f64() * 1000.0,
        head_keys.len()
    );

    let rest_body = body_skel.clone();
    let rest_head = head_skel.clone();
    let t = Instant::now();
    let n = 20usize;
    let mut last_scale = [1.0f32; 3];
    for _ in 0..n {
        body_skel = rest_body.clone();
        last_scale = crate::shape::apply_body_shape_keys(
            &mut body_skel,
            &body_keys,
            &c.shape_body,
            &correct,
            c.sex,
            c.type_bone,
            c.nip_stand_rate,
        );
    }
    let body_ms = t.elapsed().as_secs_f64() * 1000.0 / n as f64;
    eprintln!("PROFILE apply_body_shape_keys avg {body_ms:.3}ms (n={n})");

    let t = Instant::now();
    for _ in 0..n {
        head_skel = rest_head.clone();
        crate::shape::apply_head_shape_keys(
            &mut head_skel,
            &head_keys,
            &c.shape_face,
            last_scale,
            1.0 / (1.0 + correct.get(2).map(|cc| cc.scl[1]).unwrap_or(0.0)),
            c.type_bone,
        );
    }
    let head_ms = t.elapsed().as_secs_f64() * 1000.0 / n as f64;
    eprintln!("PROFILE apply_head_shape_keys avg {head_ms:.3}ms (n={n})");

    let t = Instant::now();
    for _ in 0..n {
        let mut sk = rest_body.clone();
        let _ = crate::shape::apply_body_shape(
            &game,
            &mut sk,
            &c.shape_body,
            &correct,
            c.sex,
            c.type_bone,
            c.nip_stand_rate,
        );
    }
    eprintln!(
        "PROFILE apply_body_shape (reload keys each time) avg {:.1}ms (n={n})",
        t.elapsed().as_secs_f64() * 1000.0 / n as f64
    );

    let mut mesh = body_meshes
        .iter()
        .filter(|m| m.name.eq_ignore_ascii_case("o_body_a") && !m.weights.is_empty())
        .max_by_key(|m| m.positions.len())
        .cloned()
        .expect("o_body_a");
    eprintln!(
        "PROFILE body mesh verts={} weights={}",
        mesh.positions.len(),
        mesh.weights.len()
    );
    std::env::set_var("OK_NOREPAIR", "1");
    std::env::set_var("OK_NOPRUNE", "1");
    let t = Instant::now();
    for _ in 0..n {
        let mut m = mesh.clone();
        let _ = skin_mesh(&mut m, &body_skel);
    }
    eprintln!(
        "PROFILE skin_mesh body (no repair/prune) avg {:.3}ms (n={n})",
        t.elapsed().as_secs_f64() * 1000.0 / n as f64
    );
    std::env::remove_var("OK_NOREPAIR");
    std::env::remove_var("OK_NOPRUNE");
    let t = Instant::now();
    {
        let mut m = mesh.clone();
        let _ = skin_mesh(&mut m, &body_skel);
    }
    eprintln!(
        "PROFILE skin_mesh body (repair+prune default) {:.1}ms verts={}",
        t.elapsed().as_secs_f64() * 1000.0,
        mesh.positions.len()
    );

    let out = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_profile_shape");
    let _ = std::fs::remove_dir_all(&out);
    std::env::set_var("OK_PROFILE", "1");
    let t = Instant::now();
    extract_card(&game, Some(&card), &out).expect("extract");
    eprintln!(
        "PROFILE extract_card wall {:.1}ms -> {}",
        t.elapsed().as_secs_f64() * 1000.0,
        out.display()
    );
    std::env::remove_var("OK_PROFILE");
    eprintln!(
        "PROFILE summary cached morph body+head {:.3}ms + cpu skin(no repair) ; full extract is dominated by IO/bundles/clothes",
        body_ms + head_ms
    );
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn chin_dump_vs_shape_abm() {
    use crate::pose::Skeleton;
    let game = &crate::test_support::game_path(r"");
    let card = crate::card::parse_card(Path::new(crate::test_support::game_file(r"\UserData\chara\female\KK_091207.png"),
    ))
    .expect("card");
    eprintln!(
        "CHIN shape_face len={} head_id={} [6..14]={:?}",
        card.shape_face.len(),
        card.head_id,
        card.shape_face.get(6..14)
    );
    for b in &card.abm {
        let n = b.name.to_ascii_lowercase();
        if n.contains("chin") || n.contains("cheek") || n.contains("facelow") || n.contains("neck")
        {
            let d = b.for_coord(0);
            eprintln!(
                "CHIN ABM {} loc={} scl={:?} len={} pos={:?} rot={:?}",
                b.name, b.location, d.scale, d.length, d.pos, d.rot
            );
        }
    }

    let vanilla = game.join("abdata/chara/bo_head_00.unity3d");
    let bytes = crate::gamefs::read(&vanilla).unwrap();
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let (meshes, mut skel) = parsed.meshes_for_maindata("p_cf_head_00");
    let face = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        .expect("face");
    eprintln!(
        "CHIN vanilla cf_O_face nv={} binds={} bs={}",
        face.positions.len(),
        face.bind_poses.len(),
        face.blend_shapes.len()
    );
    if let Some(s) = parsed.skeleton(face.path_id) {
        skel = Some(s);
    }
    let sk = skel.as_mut().expect("skel");
    sk.include_tree_bones();
    let names = [
        "cf_J_ChinTip_Base",
        "cf_J_Chin_Base",
        "cf_J_Chin_s",
        "cf_J_ChinLow",
        "cf_J_FaceLow_sx",
        "cf_J_FaceLow_tz",
        "cf_J_FaceBase",
    ];
    let print_bones = |tag: &str, sk: &Skeleton| {
        for n in names {
            if let Some(tf) = sk.tfs.values().find(|t| t.name.eq_ignore_ascii_case(n)) {
                let par = tf
                    .parent
                    .and_then(|p| sk.tfs.get(&p))
                    .map(|t| t.name.as_str())
                    .unwrap_or("-");
                eprintln!(
                    "CHIN {tag} {n} par={par} pos={:?} scl={:?}",
                    tf.pos, tf.scale
                );
            } else {
                eprintln!("CHIN {tag} {n} MISSING");
            }
        }
    };
    print_bones("rest", sk);
    let keys = crate::shape::load_head_keys(&vanilla);
    crate::shape::apply_head_shape_keys(sk, &keys, &card.shape_face, [1.0; 3], 1.0, 0);
    print_bones("shape", sk);
    let abm_names: Vec<String> = card.abm.iter().map(|b| b.name.clone()).collect();
    let base = crate::pose::capture_baseline(sk, &abm_names);
    crate::pose::apply_abm(sk, &card.abm, 0, &base);
    print_bones("shape+abm", sk);

    let zip = Path::new(crate::test_support::game_file(r"\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod"),
    );
    if let Some((name, bytes)) = crate::mods::zipmod_one_unity3d(zip, "bo_head_00") {
        let (ms, _, _) = crate::unity::load_base_bundle_from_memory(bytes, "bo_head_00");
        if let Some(m) = ms.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")) {
            eprintln!(
                "CHIN FaceFixes bundle={name} cf_O_face nv={} binds={}",
                m.positions.len(),
                m.bind_poses.len()
            );
        } else {
            eprintln!("CHIN FaceFixes bundle={name} no cf_O_face");
        }
    } else {
        eprintln!("CHIN FaceFixes zip missing");
    }
    eprintln!("CHIN dump nv=2695 ChinTip scl dump=(-3.2709775, 0, 1) posy=0.00156 posz=0.01076");
    eprintln!("CHIN dump kuti def_cl=0 def_op=100 (open 100); our ShotPreview default open 80");
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn brow_eyeline_catalog_vs_dump() {
    let game = &crate::test_support::game_path(r"");
    let card = crate::card::parse_card(Path::new(crate::test_support::game_file(r"\UserData\chara\female\KK_091207.png"),
    ))
    .expect("card");
    eprintln!(
        "BROW card eyebrow_id={} eyeline_up={} eyeline_down={} head={}",
        card.eyebrow_id, card.eyeline_up_id, card.eyeline_down_id, card.head_id
    );
    for r in &card.resolves {
        if (406..=413).contains(&r.category_no) {
            eprintln!(
                "BROW resolve cat={} slot={} local={} guid={} name={} prop={}",
                r.category_no, r.slot, r.local_slot, r.mod_id, r.name, r.property
            );
        }
    }
    let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mod_catalog = crate::list::load_mods_catalog(&game.join("mods"));
    for (tag, cat, id) in [
        ("mayuge", 406, card.eyebrow_id),
        ("eyeline_up", 412, card.eyeline_up_id),
        ("eyeline_down", 413, card.eyeline_down_id),
    ] {
        let m = mod_catalog.get(cat, id);
        let v = vanilla.get(cat, id);
        eprintln!(
            "BROW {tag} cat={cat} id={id} mod={:?} vanilla={:?}",
            m.map(|e| (e.main_ab.clone(), e.info("EyebrowTex"), e.info("EyelineUpTex"), e.info("EyelineDownTex"), e.info("EyelineShadowTex"), e.main_tex.clone(), e.extra.get("__zip").cloned())),
            v.map(|e| (e.main_ab.clone(), e.info("EyebrowTex"), e.info("EyelineUpTex"), e.info("EyelineDownTex"), e.main_tex.clone()))
        );
    }
    eprintln!("BROW dump mayuge=cw_t_mayuge_011_vl eyeline=eri+eri_kage low=motoko");
    // skip full extract here; see skirt_outfit4_lr_vs_dump
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn skirt_outfit4_lr_vs_dump() {
    let game = &crate::test_support::game_path(r"");
    let sess = extract_session(
        game,
        Some(Path::new(crate::test_support::game_file(r"\UserData\chara\female\KK_091207.png"),
        )),
        4,
        None,
    )
    .expect("sess");
    let skirt = sess
        .parts
        .iter()
        .find(|p| p.name.to_ascii_lowercase().contains("skirt09_a"))
        .expect("skirt");
    let mut xmin = 1e9f32;
    let mut xmax = -1e9f32;
    let mut ymin = 1e9f32;
    let mut ymax = -1e9f32;
    let mut ymin_l = 1e9f32;
    let mut ymin_r = 1e9f32;
    for p in &skirt.positions {
        xmin = xmin.min(p[0]);
        xmax = xmax.max(p[0]);
        ymin = ymin.min(p[1]);
        ymax = ymax.max(p[1]);
        if p[0] < 0.0 {
            ymin_l = ymin_l.min(p[1]);
        } else {
            ymin_r = ymin_r.min(p[1]);
        }
    }
    eprintln!(
        "SKIRT our nv={} aabb x=[{:.3},{:.3}] y=[{:.3},{:.3}] hemL={:.3} hemR={:.3} dY={:.3}",
        skirt.positions.len(),
        xmin,
        xmax,
        ymin,
        ymax,
        ymin_l,
        ymin_r,
        ymin_l - ymin_r
    );
    eprintln!("SKIRT dump aabb x=[-0.188,0.188] y=[0.761,0.949] hemL=0.762 hemR=0.761");
    if let Some(sk) = sess.body_skel.as_ref() {
        for n in [
            "cf_j_sk_00_00",
            "cf_j_sk_00_05",
            "cf_j_sk_02_05",
            "cf_j_sk_06_05",
            "cf_j_sk_01_05",
            "cf_j_sk_07_05",
            "cf_d_sk_00_00",
            "cf_d_sk_07_00",
            "cf_d_sk_02_00",
            "cf_d_sk_06_00",
        ] {
            if let Some(m) = sk.world_tf(n) {
                let p = crate::pose::mat_pos(&m);
                eprintln!("SKIRTBONE {n} world=[{:.4},{:.4},{:.4}]", p[0], p[1], p[2]);
            } else {
                eprintln!("SKIRTBONE {n} MISSING");
            }
        }
    }
    eprintln!("SKIRT dump sk_00_05 y=0.510 x=0; sk_02_05 x=0.309 y=0.536; sk_06_05 x=-0.309 y=0.536");
    assert!(
        sess.body_skel
            .as_ref()
            .and_then(|s| s.index_of("cf_j_sk_00_05"))
            .is_some(),
        "skirt bones must stay in body palette after SAC reorder"
    );
    assert!(
        (ymin_l - ymin_r).abs() < 0.02,
        "skirt hem L/R {:.3} vs {:.3}",
        ymin_l,
        ymin_r
    );
    assert!(
        (xmin.abs() - xmax.abs()).abs() < 0.03,
        "skirt x span L/R [{:.3},{:.3}] dump ±0.188",
        xmin,
        xmax
    );
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn march_skirt_vs_game_dump() {
    let game = &crate::test_support::game_path(r"");
    let card = game.join(r"UserData\chara\female\[Card] March_7th_keiyuu.png");
    if !card.ok_exists() { return; }
    let sess = extract_session(game, Some(&card), 0, None).expect("March session");
    let skirt = sess.parts.iter().find(|p| p.name == "cf_bot_d_OpleatedSKF").expect("March skirt");
    let (lo, hi) = part_aabb(skirt);
    let dump = game.join(r"mesh_dump\[Card] March_7th_keiyuu_outfit0_face.png\outfit0-tpose\March 7th_0\mesh_65_cf_bot_d_OpleatedSKF.json");
    if !dump.ok_exists() { return; }
    let data: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
    let bounds = &data["worldBounds"];
    let game_width = bounds["size"]["x"].as_f64().unwrap() as f32;
    let width = hi[0] - lo[0];
    assert_eq!(skirt.positions.len(), 924);
    assert!((width - game_width).abs() < 0.03, "skirt width {width:.4} vs game {game_width:.4}");
    let game_bone_x = data["bones"].as_array().unwrap().iter()
        .find(|b| b["transform"]["name"] == "cf_j_sk_02_05")
        .unwrap()["transform"]["worldPosition"]["x"].as_f64().unwrap() as f32;
    let bone_x = sess.body_skel.as_ref().unwrap().world_tf("cf_j_sk_02_05")
        .map(|m| crate::pose::mat_pos(&m)[0]).unwrap();
    assert!((bone_x - game_bone_x).abs() < 0.04,
        "skirt hem bone x {bone_x:.4} vs game {game_bone_x:.4}");
}

fn part_aabb(part: &crate::session::SessionPart) -> ([f32; 3], [f32; 3]) {
    let mut mn = [f32::MAX; 3];
    let mut mx = [f32::MIN; 3];
    for p in &part.positions {
        for k in 0..3 {
            mn[k] = mn[k].min(p[k]);
            mx[k] = mx[k].max(p[k]);
        }
    }
    (mn, mx)
}

fn dump_world_aabb(dir: &Path, needle: &str) -> Option<([f32; 3], [f32; 3])> {
    let rd = crate::gamefs::read_dir(dir).ok()?;
    for e in rd.flatten() {
        let p = e.path();
        let name = p.file_name()?.to_str()?;
        if !name.ends_with(".json") || name.ends_with(".materials.json") {
            continue;
        }
        if !name.to_ascii_lowercase().contains(&needle.to_ascii_lowercase()) {
            continue;
        }
        let v: serde_json::Value = serde_json::from_str(&crate::gamefs::read_to_string(&p).ok()?).ok()?;
        let b = v.get("worldBounds")?;
        let min = b.get("min")?;
        let max = b.get("max")?;
        return Some((
            [
                min.get("x")?.as_f64()? as f32,
                min.get("y")?.as_f64()? as f32,
                min.get("z")?.as_f64()? as f32,
            ],
            [
                max.get("x")?.as_f64()? as f32,
                max.get("y")?.as_f64()? as f32,
                max.get("z")?.as_f64()? as f32,
            ],
        ));
    }
    None
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn tpose_dump_body_hair_aabb() {
    std::env::set_var("OK_NOPOSE", "1");
    std::env::set_var("OK_SKELMESH", "1");
    let dump = &crate::test_support::game_path(r"\mesh_dump\tpose\Ushiwakamaru_0");
    let sess = extract_session(
        &crate::test_support::game_path(r""),
        Some(Path::new(crate::test_support::game_file(r"\UserData\chara\female\KK_091207.png"),
        )),
        0,
        None,
    )
    .expect("sess");
    let body = sess
        .parts
        .iter()
        .find(|p| p.role == "body" || p.name.to_ascii_lowercase().contains("o_body"))
        .expect("body");
    let (bmn, bmx) = part_aabb(body);
    let dump_body = dump_world_aabb(dump, "o_body_a").expect("dump body");
    eprintln!(
        "TPOSE body ours y=[{:.3},{:.3}] x=[{:.3},{:.3}] dump y=[{:.3},{:.3}] x=[{:.3},{:.3}]",
        bmn[1], bmx[1], bmn[0], bmx[0], dump_body.0[1], dump_body.1[1], dump_body.0[0], dump_body.1[0]
    );
    let mut hair_ok = 0usize;
    for part in &sess.parts {
        let n = part.name.to_ascii_lowercase();
        if !n.contains("hair") {
            continue;
        }
        if part
            .parent_bone
            .as_deref()
            .map(|p| p.starts_with("a_n_"))
            .unwrap_or(false)
        {
            continue;
        }
        let (mn, mx) = part_aabb(part);
        eprintln!(
            "TPOSE hair {} y=[{:.3},{:.3}] x=[{:.3},{:.3}]",
            part.name, mn[1], mx[1], mn[0], mx[0]
        );
        assert!(
            mn[1] > 0.9,
            "hair {} on ground y=[{:.3},{:.3}] dump hair ~1.33",
            part.name,
            mn[1],
            mx[1]
        );
        hair_ok += 1;
    }
    assert!(hair_ok > 0, "need hair parts");
    assert!(
        bmn[1] > -0.05 && bmn[1] < 0.08,
        "body feet y={:.3} dump 0.01",
        bmn[1]
    );
    assert!(
        bmx[1] > 1.15 && bmx[1] < 1.45,
        "body top y={:.3} dump 1.28",
        bmx[1]
    );
    let arm = bmx[0].max(-bmn[0]);
    assert!(
        arm > 0.45,
        "tpose arm span x=[{:.3},{:.3}] dump ±0.609",
        bmn[0],
        bmx[0]
    );
    if let Some((hmin, hmax)) = dump_world_aabb(dump, "cf_hair_f_104") {
        eprintln!(
            "TPOSE dump hair_f_104 y=[{:.3},{:.3}]",
            hmin[1], hmax[1]
        );
        assert!(hmin[1] > 1.2, "dump hair not on head?");
    }
}

#[test]
fn standalone_material_editor_normal_default_and_config_precedence() {
    let root = std::env::temp_dir().join(format!("ok-normal-policy-{}-{}", std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
    std::fs::create_dir_all(root.join("BepInEx/config")).unwrap();
    // Installed sources without a plugin/config retain their existing behavior.
    assert!(!material_editor_converts_normals(&root));
    std::fs::write(root.join("require_complete_mods"), "1").unwrap();
    assert!(material_editor_converts_normals(&root), "standalone source must use MaterialEditor's true default");
    let config = root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    std::fs::write(&config, "Convert Normal Maps = false\n").unwrap();
    assert!(!material_editor_converts_normals(&root), "explicit false must win");
    std::fs::write(&config, "Convert Normal Maps = true\n").unwrap();
    assert!(material_editor_converts_normals(&root));
    std::fs::write(&config, "# missing key uses the plugin default\n").unwrap();
    assert!(material_editor_converts_normals(&root));
    std::fs::remove_file(config).unwrap();
    std::fs::remove_file(root.join("require_complete_mods")).unwrap();
    std::fs::remove_dir(root.join("BepInEx/config")).unwrap();
    std::fs::remove_dir(root.join("BepInEx")).unwrap();
    std::fs::remove_dir(root).unwrap();
}

#[test]
fn standalone_material_editor_normal_draw_assembly() {
    let root = std::env::temp_dir().join(format!("ok-normal-draw-{}-{}", std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
    std::fs::create_dir_all(root.join("BepInEx/config")).unwrap();
    std::fs::write(root.join("require_complete_mods"), "1").unwrap();
    let config = root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    let png = encode_png(&image::RgbaImage::from_raw(1, 2,
        vec![32, 90, 120, 140, 80, 180, 200, 220]).unwrap()).unwrap();
    for (role, property) in [("accessory", "outfit0.accessory7"),
        ("clothes_top", "outfit0.clothes0"), ("hair_f", "ChaFileHair.parts[1]"), ("body", "")] {
        for overridden in [false, true] {
            let object = me_object_type(role).unwrap();
            let slot = me_slot(role, Some(property)).unwrap();
            let textures = crate::card::MeTexMap::from([((object, 0, slot,
                "fixture".into(), "normalmap".into()), std::sync::Arc::new(png.clone()))]);
            let draw = || {
                let mesh = ExtractedMesh {name: "normal_fixture".into(), positions: vec![[0.;3]],
                    normal_map_png: Some(png.clone()),
                    mat: Some(crate::unity::MeshMaterial {name: "fixture".into(),
                        shader: "xukmi/MainItemPlus".into(),
                        tex_slots: vec![("_NormalMap".into(), "bound_normal".into())],
                        tex_wraps: std::collections::HashMap::from([("_NormalMap".into(), 2)]),
                        ..Default::default()}), ..Default::default()};
                let mut manifest = Vec::new(); let mut session = Vec::new();
                push_meshes(&mut manifest, &mut session, None, &root, role, vec![mesh],
                    None, None, None, Some(property.into()), None, None, None, None, None,
                    &mut Default::default(), None, None, None, overridden.then_some(&textures),
                    None, None, None, 0, None, None, None, None).unwrap();
                assert_eq!(session.len(), 1);
                let part = &session[0];
                (part.shader_textures.get("_NormalMap").expect("normal draw binding").clone(),
                    part.runtime_textures.get("_NormalMap").copied())
            };
            let standalone = draw();
            assert_eq!(standalone.1, Some([1,2,2,1,1]), "converted RT preserves wrap, sRGB and mips: {role}");
            assert!(crate::teximg::load_from_memory(&standalone.0).unwrap().into_rgba8().pixels().all(|p| p[0] == 255));
            std::fs::write(&config, "Convert Normal Maps = true\n").unwrap();
            assert_eq!(standalone, draw(), "standalone must match configured plugin: {role}/{overridden}");
            std::fs::write(&config, "Convert Normal Maps = false\n").unwrap();
            assert_ne!(standalone.0, draw().0, "explicit false must retain unconverted source: {role}/{overridden}");
            std::fs::remove_file(&config).unwrap();
        }
    }
    std::fs::remove_file(root.join("require_complete_mods")).unwrap();
    std::fs::remove_dir(root.join("BepInEx/config")).unwrap();
    std::fs::remove_dir(root.join("BepInEx")).unwrap();
    std::fs::remove_dir(root).unwrap();
}

#[test]
#[ignore = "requires historical March 7th card and runtime normal-map dump"]
fn material_editor_normal_conversion_matches_runtime_dump() {
    let game = &crate::test_support::game_path(r"");
    let card = game.join(r"UserData\chara\female\[Card] March_7th_keiyuu.png");
    let dump = game.join(r"mesh_dump\[Card] March_7th_keiyuu_outfit0_face.png\outfit0-tpose\March 7th_0\tex_47_cf_m_bot_sitopleatesSKF_d (Instance) (Instance)__NormalMap.png");
    assert!(card.ok_is_file() && dump.ok_is_file(), "historical runtime comparison fixtures are missing");
    assert!(material_editor_converts_normals(game));
    let runtime = image::open(dump).unwrap().to_rgba8();
    let session = crate::session::open(game, Some(&card), 0).unwrap();
    let best = session.parts.iter()
        .filter(|p| p.role.starts_with("clothes"))
        .filter_map(|p| p.normal_map.as_deref())
        .filter_map(|png| crate::teximg::load_from_memory(png).ok())
        .map(|image| image.to_rgba8())
        .filter(|image| image.dimensions() == runtime.dimensions())
        .map(|image| image.pixels().zip(runtime.pixels())
            .map(|(a, b)| (0..4).map(|c| a[c].abs_diff(b[c]) as u64).sum::<u64>())
            .sum::<u64>() as f64 / (image.width() as f64 * image.height() as f64 * 4.0))
        .fold(f64::INFINITY, f64::min);
    eprintln!("clothing normal vs game runtime MAE={best:.3}/255");
    assert!(best < 1.0, "clothing normal vs game runtime MAE={best:.3}/255");
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn march_tpose_body_vertices_match_game_dump() {
    let game = &crate::test_support::game_path(r"");
    let card = game.join(r"UserData\chara\female\[Card] March_7th_keiyuu.png");
    let dump = game.join(r"mesh_dump\[Card] March_7th_keiyuu_outfit0_face.png\outfit0-tpose\March 7th_0\mesh_55_o_body_a.json");
    if !card.ok_exists() || !dump.ok_exists() { return; }
    let session = crate::session::open(game, Some(&card), 0).unwrap();
    let body = session.parts.iter().find(|p| p.name == "o_body_a").unwrap();
    let face = session.parts.iter().find(|p| p.name.eq_ignore_ascii_case("cf_O_face")).unwrap();
    assert!(face.normal_mask.as_ref().is_some_and(|png| png.len() > 16),
        "main_skin face _NormalMask did not reach the preview session");
    assert!((face.face_normal - 0.4).abs() < 0.001,
        "main_skin face _FaceNormalG differs from the game material: {}", face.face_normal);
    let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
    let verts = json["mesh"]["worldVertices"].as_array().unwrap();
    assert_eq!(body.positions.len(), verts.len());
    let mut sum = [0.0f64; 3];
    let mut max = [0.0f64; 3];
    for (ours, theirs) in body.positions.iter().zip(verts) {
        for (axis, key) in ["x", "y", "z"].iter().enumerate() {
            let delta = (ours[axis] as f64 - theirs[*key].as_f64().unwrap()).abs();
            sum[axis] += delta;
            max[axis] = max[axis].max(delta);
        }
    }
    let mean = sum.map(|s| s / verts.len() as f64);
    eprintln!("March T-pose body mean {mean:?} max {max:?} metres");
    let mut neck = (0usize, [0.0f64; 3], [0.0f64; 3]);
    for (ours, theirs) in body.positions.iter().zip(verts) {
        let y = theirs["y"].as_f64().unwrap();
        let x = theirs["x"].as_f64().unwrap();
        if (1.23..1.39).contains(&y) && x.abs() < 0.10 {
            neck.0 += 1;
            for (axis, key) in ["x", "y", "z"].iter().enumerate() {
                let d = (ours[axis] as f64 - theirs[*key].as_f64().unwrap()).abs();
                neck.1[axis] += d;
                neck.2[axis] = neck.2[axis].max(d);
            }
        }
    }
    eprintln!("March neck count={} mean={:?} max={:?} metres", neck.0,
        neck.1.map(|s| s / neck.0.max(1) as f64), neck.2);
    assert!(neck.0 > 100 && neck.2.iter().all(|d| *d < 0.0005),
        "neck seam vertices differ from game dump: {neck:?}");
    assert!(mean[0] < 0.0002 && max[0] < 0.002,
        "arm correction differs from game dump: mean={mean:?}, max={max:?}");
    assert!(mean[1] < 0.001 && mean[2] < 0.001,
        "body pose differs from game dump: mean={mean:?}");
}

/// Measures the T-pose crotch of C019 against the game dump and one other
/// crowd card. Prints only; it does not change shaping or materials.
#[test]
#[ignore = "diagnostic: C019 crotch versus game dump and another card"]
fn c019_crotch_gap_diagnostic() {
    fn inner_gap(pos: &[[f32; 3]]) -> (usize, f64, f64, f64) {
        let mut n = 0usize;
        let mut left = f64::NEG_INFINITY;
        let mut right = f64::INFINITY;
        for p in pos {
            let (x, y, z) = (p[0] as f64, p[1] as f64, p[2] as f64);
            if !(0.80..0.84).contains(&y) || z <= 0.0 || x.abs() >= 0.08 { continue; }
            n += 1;
            if x < 0.0 { left = left.max(x); }
            if x > 0.0 { right = right.min(x); }
        }
        let gap = if left.is_finite() && right.is_finite() { right - left } else { f64::NAN };
        (n, left, right, gap)
    }
    fn island(pos: &[[f32; 3]], uv: &[[f32; 2]]) -> String {
        let mut n = 0usize;
        let mut min = [f64::INFINITY; 3];
        let mut max = [f64::NEG_INFINITY; 3];
        for (p, t) in pos.iter().zip(uv) {
            if !(0.110..0.204).contains(&t[0]) || !(0.575..0.602).contains(&t[1]) { continue; }
            n += 1;
            for a in 0..3 {
                min[a] = min[a].min(p[a] as f64);
                max[a] = max[a].max(p[a] as f64);
            }
        }
        format!("genital_uv n={n} min={min:?} max={max:?}")
    }
    /// Largest empty gap across the front of the crotch, measured in the
    /// vulva UV island's own height. A centre vertex no longer hides a hole.
    fn crotch_opening(pos: &[[f32; 3]], uv: &[[f32; 2]]) -> (usize, f64, f64) {
        let mut ys = Vec::new();
        let mut zmin = f64::INFINITY;
        for (p, t) in pos.iter().zip(uv) {
            if !(0.110..0.204).contains(&t[0]) || !(0.575..0.602).contains(&t[1]) { continue; }
            ys.push(p[1] as f64);
            zmin = zmin.min(p[2] as f64);
        }
        let n = ys.len();
        if n < 30 { return (n, f64::NAN, f64::NAN); }
        let y_lo = ys.iter().copied().fold(f64::INFINITY, f64::min) - 0.08;
        let y_hi = ys.iter().copied().fold(f64::NEG_INFINITY, f64::max) + 0.015;
        let z_cut = zmin - 0.01;
        let mut worst = 0.0f64;
        let mut worst_y = 0.0f64;
        let slices = 10i32;
        for s in 0..slices {
            let a = y_lo + (y_hi - y_lo) * s as f64 / slices as f64;
            let b = a + (y_hi - y_lo) / slices as f64;
            let mut xs = Vec::new();
            for p in pos {
                let x = p[0] as f64;
                let y = p[1] as f64;
                let z = p[2] as f64;
                if y >= a && y < b && z > z_cut && x.abs() < 0.12 { xs.push(x); }
            }
            xs.sort_by(|u, v| u.partial_cmp(v).unwrap_or(std::cmp::Ordering::Equal));
            let mut prev = f64::NAN;
            let mut gap = 0.0f64;
            for x in xs {
                if prev.is_finite() { gap = gap.max(x - prev); }
                prev = x;
            }
            if gap > worst { worst = gap; worst_y = (a + b) * 0.5; }
        }
        (n, worst, worst_y)
    }
    fn boundary_in_box(pos: &[[f32; 3]], idx: &[u32], y0: f64, y1: f64) -> (usize, f64) {
        let mut edges: std::collections::HashMap<(u32, u32), u8> = std::collections::HashMap::new();
        for tri in idx.chunks_exact(3) {
            let touch = tri.iter().any(|&i| {
                let p = pos.get(i as usize).copied().unwrap_or([0.0; 3]);
                (y0..y1).contains(&(p[1] as f64)) && (p[0] as f64).abs() < 0.10 && (p[2] as f64) > -0.03
            });
            if !touch { continue; }
            for e in [(tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])] {
                let key = if e.0 < e.1 { (e.0, e.1) } else { (e.1, e.0) };
                let slot = edges.entry(key).or_insert(0);
                *slot = slot.saturating_add(1);
            }
        }
        let mut n = 0usize;
        let mut longest = 0.0f64;
        for ((a, b), c) in edges {
            if c != 1 { continue; }
            let (Some(pa), Some(pb)) = (pos.get(a as usize), pos.get(b as usize)) else { continue; };
            let my = (pa[1] + pb[1]) as f64 * 0.5;
            let mx = (pa[0] + pb[0]) as f64 * 0.5;
            let mz = (pa[2] + pb[2]) as f64 * 0.5;
            if !(y0..y1).contains(&my) || mx.abs() > 0.07 || mz < -0.02 { continue; }
            n += 1;
            let d = ((pa[0] - pb[0]).powi(2) + (pa[1] - pb[1]).powi(2) + (pa[2] - pb[2]).powi(2)).sqrt() as f64;
            longest = longest.max(d);
        }
        (n, longest)
    }
    fn bones(label: &str, skel: &crate::pose::Skeleton) {
        for name in ["cf_d_kokan", "cf_j_kokan", "cf_s_thigh01_L", "cf_s_thigh01_R",
            "cf_j_thigh00_L", "cf_j_thigh00_R", "cf_J_Vagina_root"] {
            match skel.tfs.values().find(|t| t.name == name) {
                Some(t) => {
                    let world = skel.world_tf(name).map(|m| crate::pose::mat_pos(&m));
                    eprintln!("{label} {name} local_pos={:?} local_rot={:?} local_scl={:?} world={world:?}",
                        t.pos, t.rot, t.scale);
                }
                None => eprintln!("{label} {name} MISSING"),
            }
        }
        let vagina = skel.tfs.values().filter(|t| t.name.contains("Vagina")).count();
        eprintln!("{label} vagina_bones={vagina} bones={}", skel.tfs.len());
    }
    fn report_session(label: &str, card: &std::path::Path) {
        let game = &crate::test_support::game_path(r"");
        let t = std::time::Instant::now();
        let session = crate::session::open(game, Some(card), 0).expect("session");
        eprintln!("{label} open {:.1}s sex={} verts_body_pending shape={}",
            t.elapsed().as_secs_f64(), session.sex, session.shape_body.len());
        let body = session.parts.iter().find(|p| p.name == "o_body_a")
            .expect("o_body_a");
        let (n, left, right, gap) = inner_gap(&body.positions);
        eprintln!("{label} body verts={} blend={} line_tex_on={} alpha_bytes={} blend_shapes={} gap_n={n} left_max={left:.6} right_min={right:.6} gap={gap:.6}",
            body.positions.len(), body.blend, body.line_tex_on,
            body.alpha_mask.as_ref().map_or(0, Vec::len), body.blend_shapes.len());
        eprintln!("{label} {}", island(&body.positions, &body.uvs));
        if let Some(png) = body.alpha_mask.as_ref() {
            if let Ok(img) = crate::teximg::load_from_memory(png) {
                let img = img.to_rgba8();
                let (w, h) = img.dimensions();
                let mut samples = 0u32;
                let mut dark_top = 0u32;
                let mut dark_bot = 0u32;
                let mut min_r = 255u8;
                for t in &body.uvs {
                    if !(0.110..0.204).contains(&t[0]) || !(0.575..0.602).contains(&t[1]) { continue; }
                    let x = ((t[0].clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
                    let y_top = ((t[1].clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
                    let y_bot = (((1.0 - t[1]).clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
                    let rt = img.get_pixel(x, y_top)[0];
                    let rb = img.get_pixel(x, y_bot)[0];
                    min_r = min_r.min(rt).min(rb);
                    if rt < 128 { dark_top += 1; }
                    if rb < 128 { dark_bot += 1; }
                    samples += 1;
                }
                eprintln!("{label} alpha {w}x{h} genital_samples={samples} dark_top={dark_top} dark_bot={dark_bot} min_r={min_r}");
            }
        }
        if let Some(skel) = session.body_skel.as_ref() { bones(label, skel); }
        for p in session.parts.iter().filter(|p| p.role.starts_with("clothes") || p.role == "body" || p.name.to_ascii_lowercase().contains("dankon")) {
            let mut min = [f32::INFINITY; 3];
            let mut max = [f32::NEG_INFINITY; 3];
            for v in &p.positions {
                for a in 0..3 {
                    min[a] = min[a].min(v[a]);
                    max[a] = max[a].max(v[a]);
                }
            }
            let (n, left, right, gap) = inner_gap(&p.positions);
            eprintln!("{label} part role={} name={} vis={} blend={} verts={} aabb=({min:?},{max:?}) crotch_n={n} gap={gap:.4}",
                p.role, p.name, p.render_visible, p.blend, p.positions.len());
        }
    }
    fn report_card(label: &str, card: &std::path::Path) {
        let info = crate::parse_card(card).expect("card");
        eprintln!("{label} CARD head={} sex={} penis={:?} clothes={:?} resolves={}",
            info.head_id, info.sex, info.penis_guid, info.clothes_ids, info.resolves.len());
        for r in info.resolves.iter().filter(|r| r.property.contains("ChaFileClothes") || r.property.contains("Head")) {
            eprintln!("{label} RESOLVE {} guid={} slot={} local={} cat={} name={}",
                r.property, r.mod_id, r.slot, r.local_slot, r.category_no, r.name);
        }
    }
    fn report_dump(path: &std::path::Path) {
        let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(path).unwrap()).unwrap();
        let verts = json["mesh"]["worldVertices"].as_array().unwrap();
        let uvs = json["mesh"]["uv"].as_array().unwrap();
        let pos: Vec<[f32; 3]> = verts.iter().map(|v| {
            ["x", "y", "z"].map(|k| v[k].as_f64().unwrap() as f32)
        }).collect();
        let uv: Vec<[f32; 2]> = uvs.iter().map(|v| {
            ["x", "y"].map(|k| v[k].as_f64().unwrap() as f32)
        }).collect();
        let (n, left, right, gap) = inner_gap(&pos);
        let (island_n, opening, opening_y) = crotch_opening(&pos, &uv);
        eprintln!("DUMP {} verts={} gap_n={n} left_max={left:.6} right_min={right:.6} gap={gap:.6} island_n={island_n} opening={opening:.4} at_y={opening_y:.3}",
            path.display(), pos.len());
        eprintln!("DUMP {}", island(&pos, &uv));
        if let Some(tris) = json["mesh"]["triangles"].as_array() {
            let idx: Vec<u32> = tris.iter().filter_map(|v| v.as_u64().map(|n| n as u32)).collect();
            let y_lo = pos.iter().zip(&uv).filter(|(_, t)| (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]))
                .map(|(p, _)| p[1] as f64).fold(f64::INFINITY, f64::min) - 0.08;
            let y_hi = pos.iter().zip(&uv).filter(|(_, t)| (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]))
                .map(|(p, _)| p[1] as f64).fold(f64::NEG_INFINITY, f64::max) + 0.015;
            let (bn, longest) = boundary_in_box(&pos, &idx, y_lo, y_hi);
            eprintln!("DUMP boundary_edges={bn} longest={longest:.4} y={y_lo:.3}..{y_hi:.3} tris={}", idx.len() / 3);
        }
    }
    let game = &crate::test_support::game_path(r"");
    let dump_root = game.join(r"mesh_dump\KK_384914.png");
    if let Ok(rd) = crate::gamefs::read_dir(&dump_root) {
        for ent in rd.flatten() {
            let mesh = ent.path().join("mesh_37_o_body_a.json");
            if mesh.ok_is_file() { report_dump(&mesh); }
            let skel_path = ent.path().join("skeleton.json");
            if skel_path.ok_is_file() {
                let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(&skel_path).unwrap()).unwrap();
                for t in json["transformTree"].as_array().into_iter().flatten() {
                    let name = t["name"].as_str().unwrap_or("");
                    if !matches!(name, "cf_d_kokan" | "cf_j_kokan" | "cf_s_thigh01_L" | "cf_s_thigh01_R"
                        | "cf_j_thigh00_L" | "cf_j_thigh00_R" | "cf_J_Vagina_root") { continue; }
                    eprintln!("DUMP {name} lp={} lr={} ls={}", t["localPosition"], t["localRotation"], t["localScale"]);
                }
            }
        }
    } else {
        eprintln!("DUMP missing {}", dump_root.display());
    }
    let c019 = std::path::Path::new(r"C:\portable\OpenKoikatsu\Content\CharacterCards\CrowdAPNG\C019.png");
    let c000 = std::path::Path::new(r"C:\portable\OpenKoikatsu\Content\CharacterCards\CrowdAPNG\C000.png");
    report_card("C019", c019);
    report_card("C000", c000);
    std::env::set_var("OK_PORTABLE_PROFILE", "1");
    let mut c019_shape: Option<Vec<f32>> = None;
    let mut c019_rest: Option<crate::pose::Skeleton> = None;
    let mut c019_posed: Option<crate::pose::Skeleton> = None;
    for (label, path) in [("C019P", c019), ("C000P", c000)] {
        let t = std::time::Instant::now();
        let mut session = crate::portable::load(path).expect("portable");
        eprintln!("{label} portable {:.1}s sex={} parts={}", t.elapsed().as_secs_f64(), session.sex, session.parts.len());
        let body = session.parts.iter().find(|p| p.name == "o_body_a").expect("body");
        let (n, left, right, gap) = inner_gap(&body.positions);
        let (island_n, opening, opening_y) = crotch_opening(&body.positions, &body.uvs);
        eprintln!("{label} body verts={} blend={} line={} outline={} line_w={:.4} gap_n={n} left={left:.6} right={right:.6} gap={gap:.6} island_n={island_n} opening={opening:.4} at_y={opening_y:.3}",
            body.positions.len(), body.blend, body.line_tex_on, body.source_outline_enabled, body.skin_line_width);
        eprintln!("{label} {}", island(&body.positions, &body.uvs));
        if island_n >= 30 {
            let y_lo = body.positions.iter().zip(&body.uvs).filter(|(_, t)| (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]))
                .map(|(p, _)| p[1] as f64).fold(f64::INFINITY, f64::min) - 0.08;
            let y_hi = body.positions.iter().zip(&body.uvs).filter(|(_, t)| (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]))
                .map(|(p, _)| p[1] as f64).fold(f64::NEG_INFINITY, f64::max) + 0.015;
            let (bn, longest) = boundary_in_box(&body.positions, &body.indices, y_lo, y_hi);
            eprintln!("{label} boundary_edges={bn} longest={longest:.4}");
        }
        if label == "C019P" {
            if let Some(img) = body.alpha_mask.as_ref().and_then(|p| crate::teximg::load_from_memory(p).ok()) {
                let img = img.to_rgba8();
                let (w, h) = img.dimensions();
                let mut samples = 0u32;
                let mut dark = 0u32;
                for uv in &body.uvs {
                    if !(0.05..0.45).contains(&uv[0]) || !(0.50..0.70).contains(&uv[1]) { continue; }
                    let x = ((uv[0].clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
                    let y = ((uv[1].clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
                    samples += 1;
                    if img.get_pixel(x, y)[0] < 128 { dark += 1; }
                }
                eprintln!("{label} wide alpha {w}x{h} samples={samples} dark={dark} line_bytes={}",
                    body.line_mask.as_ref().map_or(0, Vec::len));
            }
            let root = &crate::test_support::game_path(r"\mesh_dump\KK_384914.png");
            if let Ok(rd) = crate::gamefs::read_dir(root) {
                for ent in rd.flatten() {
                    let mesh = ent.path().join("mesh_37_o_body_a.json");
                    if !mesh.ok_is_file() { continue; }
                    let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(mesh).unwrap()).unwrap();
                    let verts = json["mesh"]["worldVertices"].as_array().unwrap();
                    if verts.len() != body.positions.len() { eprintln!("{label} dump vert mismatch"); continue; }
                    let mut sum = 0.0f64;
                    let mut max = 0.0f64;
                    let mut at = [0.0f64; 3];
                    for (ours, theirs) in body.positions.iter().zip(verts) {
                        let d = ["x", "y", "z"].iter().enumerate().map(|(i, k)| {
                            (ours[i] as f64 - theirs[*k].as_f64().unwrap()).powi(2)
                        }).sum::<f64>().sqrt();
                        sum += d;
                        if d > max {
                            max = d;
                            at = ["x", "y", "z"].map(|k| theirs[k].as_f64().unwrap());
                        }
                    }
                    let mean = sum / verts.len() as f64;
                    eprintln!("{label} vs dump index mean={mean:.6} m max={max:.6} m dump_at={at:?}");
                    let mut worst = Vec::new();
                    for (i, (ours, theirs)) in body.positions.iter().zip(verts).enumerate() {
                        let d = ["x", "y", "z"].iter().enumerate().map(|(a, k)| {
                            (ours[a] as f64 - theirs[*k].as_f64().unwrap()).powi(2)
                        }).sum::<f64>().sqrt();
                        if d > 0.01 { worst.push((d, i)); }
                    }
                    worst.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap());
                    let dump_uv = json["mesh"]["uv"].as_array().unwrap();
                    let dump_tri = json["mesh"]["triangles"].as_array().map(|t| t.len()).unwrap_or(0);
                    let same_idx = body.indices.len() == dump_tri && json["mesh"]["triangles"].as_array().unwrap().iter().zip(&body.indices).all(|(a, b)| a.as_u64() == Some(*b as u64));
                    eprintln!("{label} indices ours={} dump={dump_tri} same_order={same_idx} moved_over_1cm={}", body.indices.len(), worst.len());
                    for (d, i) in worst.iter().take(8) {
                        let du = &dump_uv[*i];
                        eprintln!("{label} moved {d:.4} m idx={i} ours={:?} uv={:?} dump_uv=({},{})",
                            body.positions[*i], body.uvs.get(*i), du["x"], du["y"]);
                    }
                }
            }
        }
        for p in &session.parts {
            let n = p.name.to_ascii_lowercase();
            if !(p.role.starts_with("clothes") || p.role == "body" || n.contains("dankon") || n.contains("dan")) { continue; }
            let mut min = [f32::INFINITY; 3];
            let mut max = [f32::NEG_INFINITY; 3];
            for v in &p.positions { for a in 0..3 { min[a] = min[a].min(v[a]); max[a] = max[a].max(v[a]); } }
            let (cn, _, _, cg) = inner_gap(&p.positions);
            eprintln!("{label} part role={} name={} vis={} blend={} verts={} crotch_n={cn} gap={cg:.4} z=[{:.3},{:.3}]",
                p.role, p.name, p.render_visible, p.blend, p.positions.len(), min[2], max[2]);
        }
        if label == "C019P" {
            if let Some(skel) = session.body_skel.as_ref() { bones(label, skel); }
            c019_shape = Some(session.shape_body.clone());
            c019_rest = session.body_rest.clone();
            c019_posed = session.body_skel.clone();
        }
            let body_i = session.parts.iter().position(|p| p.name == "o_body_a").unwrap();
            let skel_kind = session.parts[body_i].skel_kind;
            let rest_n = session.parts[body_i].rest_positions.len();
            let bind_n = session.parts[body_i].bind_poses.len();
            eprintln!("{label} skel_kind={skel_kind} rest_verts={rest_n} binds={bind_n}");
            let pref = session.ensure_skel_export().part_rest[body_i].clone();
            let root = &crate::test_support::game_path(r"\mesh_dump\KK_384914.png");
            if let Ok(rd) = crate::gamefs::read_dir(root) {
                for ent in rd.flatten() {
                    let mesh = ent.path().join("mesh_37_o_body_a.json");
                    if !mesh.ok_is_file() { continue; }
                    let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(&mesh).unwrap()).unwrap();
                    let local = json["mesh"]["vertices"].as_array().unwrap();
                    let world = json["mesh"]["worldVertices"].as_array().unwrap();
                    let delta = |tag: &str, ours: &[[f32; 3]], theirs: &[serde_json::Value]| {
                        if ours.len() != theirs.len() {
                            eprintln!("{label} {tag} count ours={} dump={}", ours.len(), theirs.len());
                            return;
                        }
                        let mut sum = 0.0f64;
                        let mut max = 0.0f64;
                        let mut max_i = 0usize;
                        let mut over = 0usize;
                        for (i, (o, t)) in ours.iter().zip(theirs).enumerate() {
                            let d = ["x", "y", "z"].iter().enumerate().map(|(a, k)| {
                                (o[a] as f64 - t[*k].as_f64().unwrap()).powi(2)
                            }).sum::<f64>().sqrt();
                            sum += d;
                            if d > max { max = d; max_i = i; }
                            if d > 0.01 { over += 1; }
                        }
                        let mean = sum / ours.len() as f64;
                        eprintln!("{label} {tag} mean={mean:.6} max={max:.6} idx={max_i} over_1cm={over} ours={:?} dump=({},{},{})",
                            ours[max_i], theirs[max_i]["x"], theirs[max_i]["y"], theirs[max_i]["z"]);
                    };
                    delta("rest_vs_local", &session.parts[body_i].rest_positions, local);
                    delta("gpu_rest_vs_world", &pref, world);
                    let binds = &session.parts[body_i].bind_poses;
                    let dump_binds = json["mesh"]["bindposes"].as_array().unwrap();
                    let mut bind_max = 0.0f64;
                    let mut bind_over = 0usize;
                    let n = binds.len().min(dump_binds.len());
                    for i in 0..n {
                        let arr = dump_binds[i].as_array().unwrap();
                        for r in 0..4 {
                            for c in 0..4 {
                                let d = (binds[i][r][c] as f64 - arr[r * 4 + c].as_f64().unwrap()).abs();
                                if d > 1e-4 { bind_over += 1; }
                                bind_max = bind_max.max(d);
                            }
                        }
                    }
                    eprintln!("{label} binds ours={} dump={} max_abs={bind_max:.6} elems_over_1e-4={bind_over}",
                        binds.len(), dump_binds.len());
                }
            }
        let body_shape = session.shape_body.clone();
        let face_shape = session.shape_face.clone();
        session.apply_shape(&body_shape, &face_shape);
        session.skin_all();
        let body = session.parts.iter().find(|p| p.name == "o_body_a").expect("body");
        let (n, left, right, gap) = inner_gap(&body.positions);
        let (_, opening, opening_y) = crotch_opening(&body.positions, &body.uvs);
        eprintln!("{label} after T-pose ApplyShape gap_n={n} left={left:.6} right={right:.6} gap={gap:.6} opening={opening:.4} at_y={opening_y:.3} {}",
            island(&body.positions, &body.uvs));
        if label == "C019P" {
            if let Some(skel) = session.body_skel.as_ref() { bones("C019P-shaped", skel); }
        }
    }
    std::env::remove_var("OK_PORTABLE_PROFILE");
    let src = game.join(r"UserData\chara\female\KK_384914.png");
    if src.ok_is_file() {
        let t = std::time::Instant::now();
        let fresh = crate::session::open(game, Some(&src), 0).expect("fresh");
        eprintln!("FRESH open {:.1}s", t.elapsed().as_secs_f64());
        let body = fresh.parts.iter().find(|p| p.name == "o_body_a").expect("fresh body");
        let root = game.join(r"mesh_dump\KK_384914.png");
        if let Ok(rd) = crate::gamefs::read_dir(root) {
            for ent in rd.flatten() {
                let mesh = ent.path().join("mesh_37_o_body_a.json");
                if !mesh.ok_is_file() { continue; }
                let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(mesh).unwrap()).unwrap();
                let world = json["mesh"]["worldVertices"].as_array().unwrap();
                let local = json["mesh"]["vertices"].as_array().unwrap();
                let delta = |tag: &str, ours: &[[f32; 3]], theirs: &[serde_json::Value]| {
                    if ours.len() != theirs.len() {
                        eprintln!("FRESH {tag} count ours={} dump={}", ours.len(), theirs.len());
                        return;
                    }
                    let mut sum = 0.0f64;
                    let mut max = 0.0f64;
                    let mut over = 0usize;
                    for (o, t) in ours.iter().zip(theirs) {
                        let d = ["x", "y", "z"].iter().enumerate().map(|(a, k)| {
                            (o[a] as f64 - t[*k].as_f64().unwrap()).powi(2)
                        }).sum::<f64>().sqrt();
                        sum += d;
                        if d > max { max = d; }
                        if d > 0.01 { over += 1; }
                    }
                    let mean = sum / ours.len() as f64;
                    eprintln!("FRESH {tag} mean={mean:.6} max={max:.6} over_1cm={over}");
                };
                delta("positions_vs_world", &body.positions, world);
                delta("rest_vs_local", &body.rest_positions, local);
                eprintln!("FRESH {}", island(&body.positions, &body.uvs));
            }
        }
        let bone_map = |skel: &crate::pose::Skeleton| {
            skel.tfs.values().map(|t| (t.name.clone(), (t.pos, t.rot, t.scale))).collect::<std::collections::BTreeMap<_, _>>()
        };
        let report = |tag: &str, a: &std::collections::BTreeMap<String, ([f32; 3], [f32; 4], [f32; 3])>, b: &std::collections::BTreeMap<String, ([f32; 3], [f32; 4], [f32; 3])>| {
            let mut rows = Vec::new();
            for (name, (ap, ar, asc)) in a {
                let Some((bp, br, bsc)) = b.get(name) else {
                    rows.push((1.0e9, name.clone(), "missing-in-b".to_string()));
                    continue;
                };
                let dp = (0..3).map(|i| (ap[i] - bp[i]).powi(2) as f64).sum::<f64>().sqrt();
                let ds = (0..3).map(|i| (asc[i] - bsc[i]).powi(2) as f64).sum::<f64>().sqrt();
                let dot = (0..4).map(|i| ar[i] * br[i]).sum::<f32>().abs().min(1.0);
                let dr = (1.0 - dot) as f64;
                if dp > 0.0005 || ds > 0.0005 || dr > 0.000001 || name.contains("Vagina") || name.contains("siri") || name.contains("kokan") {
                    rows.push((dp.max(ds).max(dr), name.clone(), format!("dp={dp:.5} drot={dr:.6} ds={ds:.5}")));
                }
            }
            for name in b.keys() {
                if !a.contains_key(name) { rows.push((1.0e9, name.clone(), "missing-in-a".to_string())); }
            }
            rows.sort_by(|x, y| y.0.partial_cmp(&x.0).unwrap_or(std::cmp::Ordering::Equal));
            eprintln!("{tag} compared={} flagged={}", a.len().min(b.len()), rows.len());
            for (_, name, text) in rows.iter().take(30) { eprintln!("{tag} {name} {text}"); }
        };
        if let (Some(rest), Some(fresh_rest)) = (c019_rest.as_ref(), fresh.body_rest.as_ref()) {
            report("REST C019-vs-fresh", &bone_map(rest), &bone_map(fresh_rest));
        }
        if let (Some(posed), Some(fresh_posed)) = (c019_posed.as_ref(), fresh.body_skel.as_ref()) {
            report("POSED C019-vs-fresh", &bone_map(posed), &bone_map(fresh_posed));
        }
        if let Some(saved) = c019_shape.as_ref() {
            let n = saved.len().min(fresh.shape_body.len());
            let mut differ = Vec::new();
            for i in 0..n {
                let d = (saved[i] - fresh.shape_body[i]).abs();
                if d > 0.0001 { differ.push((i, saved[i], fresh.shape_body[i])); }
            }
            eprintln!("SHAPE C019={} fresh={} differ={}", saved.len(), fresh.shape_body.len(), differ.len());
            for (i, a, b) in differ.iter().take(40) { eprintln!("SHAPE [{i}] card={a:.5} fresh={b:.5}"); }
        }
        let fresh_body = fresh.parts.iter().find(|p| p.name == "o_body_a").unwrap();
        let local_of = |skel: &crate::pose::Skeleton, name: &str| {
            skel.tfs.values().find(|t| t.name == name).map(|t| (t.pos, t.scale))
        };
        if let Some(rest) = fresh.body_rest.as_ref() {
            eprintln!("FRESH rest Vagina_B {:?}", local_of(rest, "cf_J_Vagina_B"));
        }
        if let Some(posed) = fresh.body_skel.as_ref() {
            eprintln!("FRESH posed Vagina_B {:?}", local_of(posed, "cf_J_Vagina_B"));
        }
        if let Ok(mut card) = crate::portable::load(c019) {
            let card_body = card.parts.iter().find(|p| p.name == "o_body_a").unwrap();
            let weight_diff = card_body.weights.iter().zip(&fresh_body.weights).filter(|(a, b)| a != b).count();
            let index_diff = card_body.bone_idx.iter().zip(&fresh_body.bone_idx).filter(|(a, b)| a != b).count();
            eprintln!("WEIGHTS c019={} fresh={} differ={} INDEX differ={}",
                card_body.weights.len(), fresh_body.weights.len(), weight_diff, index_diff);
            let shape = fresh.shape_body.clone();
            let face = fresh.shape_face.clone();
            card.apply_shape(&shape, &face);
            card.skin_all();
            let shaped = card.parts.iter().find(|p| p.name == "o_body_a").unwrap();
            if let Some(posed) = card.body_skel.as_ref() {
                eprintln!("C019 with fresh sliders Vagina_B {:?}", local_of(posed, "cf_J_Vagina_B"));
            }
            eprintln!("C019 with fresh sliders {}", island(&shaped.positions, &shaped.uvs));
        }
        let vagina_local = |skel: &crate::pose::Skeleton, name: &str| -> Option<[f32; 3]> {
            skel.tfs.values().find(|t| t.name == name).map(|t| t.pos)
        };
        let describe_abm = |tag: &str, abm: &[crate::abmx::AbmBone]| {
            let mut rows = Vec::new();
            for b in abm {
                let d = b.for_coord(0);
                if d.is_empty() { continue; }
                let interesting = b.name.contains("Vagina") || b.name.contains("kokan")
                    || b.name.contains("siri") || b.name.contains("thigh")
                    || d.pos.iter().any(|v| v.abs() > 0.001)
                    || (d.length - 1.0).abs() > 0.001;
                if interesting {
                    rows.push(format!("{} loc={} len={:.4} pos={:?} rot={:?} scl={:?}",
                        b.name, b.location, d.length, d.pos, d.rot, d.scale));
                }
            }
            eprintln!("{tag} abm_n={} interesting={}", abm.len(), rows.len());
            for row in rows.iter().take(60) { eprintln!("{tag} ABM {row}"); }
        };
        if let Ok(mut card) = crate::portable::load(c019) {
            std::env::set_var("OK_VER02_DIAG", "1");
            eprintln!("C019 game_root empty={} path={} dyn={}",
                card.game_root.as_os_str().is_empty(), card.game_root.display(), card.body_dynamics.describe());
            eprintln!("FRESH dyn={}", fresh.body_dynamics.describe());
            if let Some(skel) = card.body_skel.as_ref() {
                eprintln!("C019 clothes {}", card.clothes_dynamics.describe_named(skel));
                eprintln!("C019 colliders main={:?} studio={:?}",
                    card.collider_settings.resolve(false, card.collider_overrides),
                    card.collider_settings.resolve(true, card.collider_overrides));
            }
            if let Some(skel) = fresh.body_skel.as_ref() {
                eprintln!("FRESH clothes {}", fresh.clothes_dynamics.describe_named(skel));
                let studio = !fresh.game_root.as_os_str().is_empty()
                    && crate::anim::clip_expression_bone_mode(&fresh.game_root, &fresh.anim_clip, fresh.anim_scene_mode);
                eprintln!("FRESH colliders studio_mode={studio} flags={:?}",
                    fresh.collider_settings.resolve(studio, fresh.collider_overrides));
            }
            let shape_now = card.shape_body.clone();
            let face_now = card.shape_face.clone();
            card.apply_shape(&shape_now, &face_now);
            if let Some(posed) = card.body_skel.as_ref() {
                eprintln!("AFTER own-shape apply Vagina_B {:?}", vagina_local(posed, "cf_J_Vagina_B"));
            }
            std::env::remove_var("OK_VER02_DIAG");
            describe_abm("C019PNG", &card.abm);
            describe_abm("FRESHCARD", &fresh.abm);
            let bytes = crate::gamefs::read(c019).unwrap();
            let (_, range) = crate::portable::png_snapshot(&bytes).unwrap();
            let baked = crate::portable::decode_at(&bytes[range.unwrap()], Some(c019), 0).unwrap();
            describe_abm("BAKED", &baked.abm);
            if let (Some(rest), Some(posed)) = (baked.body_rest.as_ref(), baked.body_skel.as_ref()) {
                eprintln!("BAKED rest Vagina_B {:?} posed {:?}", vagina_local(rest, "cf_J_Vagina_B"), vagina_local(posed, "cf_J_Vagina_B"));
                eprintln!("BAKED dup Vagina_B rest={} posed={}",
                    rest.tfs.values().filter(|t| t.name == "cf_J_Vagina_B").count(),
                    posed.tfs.values().filter(|t| t.name == "cf_J_Vagina_B").count());
            }
            let baked_body = baked.parts.iter().find(|p| p.name == "o_body_a").unwrap();
            eprintln!("BAKED before reapply {}", island(&baked_body.positions, &baked_body.uvs));
            let apply_named = |skel: &mut crate::pose::Skeleton, abm: &[crate::abmx::AbmBone]| {
                let names: Vec<String> = abm.iter().map(|b| b.name.clone()).collect();
                let base = crate::pose::capture_baseline(skel, &names);
                crate::pose::apply_abm(skel, abm, 0, &base);
            };
            if let Some(rest) = card.body_rest.clone() {
                let mut shaped = rest.clone();
                crate::shape::apply_body_shape_keys_masked(&mut shaped, &card.body_keys, &card.shape_body,
                    &card.correct, card.sex, card.type_bone, card.nip_stand_rate, [[true; 9]; 2]);
                eprintln!("STEP shape-only Vagina_B {:?}", vagina_local(&shaped, "cf_J_Vagina_B"));
                apply_named(&mut shaped, &card.abm);
                eprintln!("STEP shape+png-abm Vagina_B {:?}", vagina_local(&shaped, "cf_J_Vagina_B"));
                let mut baked_abm = rest.clone();
                crate::shape::apply_body_shape_keys_masked(&mut baked_abm, &card.body_keys, &card.shape_body,
                    &card.correct, card.sex, card.type_bone, card.nip_stand_rate, [[true; 9]; 2]);
                apply_named(&mut baked_abm, &baked.abm);
                eprintln!("STEP shape+baked-abm Vagina_B {:?}", vagina_local(&baked_abm, "cf_J_Vagina_B"));
                let mut fresh_abm = rest;
                crate::shape::apply_body_shape_keys_masked(&mut fresh_abm, &card.body_keys, &fresh.shape_body,
                    &card.correct, card.sex, card.type_bone, card.nip_stand_rate, [[true; 9]; 2]);
                apply_named(&mut fresh_abm, &fresh.abm);
                eprintln!("STEP shape+fresh-abm Vagina_B {:?}", vagina_local(&fresh_abm, "cf_J_Vagina_B"));
            }
            card.abm.clear();
            let shape = card.shape_body.clone();
            let face = card.shape_face.clone();
            card.apply_shape(&shape, &face);
            card.skin_all();
            let cleared = card.parts.iter().find(|p| p.name == "o_body_a").unwrap();
            eprintln!("CLEARED abm Vagina_B {:?} {}",
                card.body_skel.as_ref().and_then(|s| vagina_local(s, "cf_J_Vagina_B")),
                island(&cleared.positions, &cleared.uvs));
            let mut trial = card.body_skel.clone().unwrap();
            let rest_skel = card.body_rest.clone().unwrap();
            let id_of = |s: &crate::pose::Skeleton| s.tfs.iter().find(|(_, t)| t.name == "cf_J_Vagina_B").map(|(i, _)| *i);
            let missing = rest_skel.tfs.keys().filter(|k| !trial.tfs.contains_key(k)).count();
            let extra = trial.tfs.keys().filter(|k| !rest_skel.tfs.contains_key(k)).count();
            eprintln!("ID rest={:?} posed={:?} len {}/{} missing_in_posed={missing} extra_in_posed={extra}",
                id_of(&rest_skel), id_of(&trial), rest_skel.tfs.len(), trial.tfs.len());
            trial.restore_from(&rest_skel);
            eprintln!("AFTER restore_from Vagina_B {:?}", vagina_local(&trial, "cf_J_Vagina_B"));
        }
        let crowd = std::path::Path::new(r"C:\portable\OpenKoikatsu\Content\CharacterCards\CrowdAPNG");
        fn island_edge(pos: &[[f32; 3]], uv: &[[f32; 2]], idx: &[u32]) -> f64 {
            let mut mark = vec![false; pos.len()];
            for (i, t) in uv.iter().enumerate() {
                if (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]) { mark[i] = true; }
            }
            let mut longest = 0.0f64;
            for tri in idx.chunks_exact(3) {
                if !tri.iter().any(|&i| mark.get(i as usize).copied().unwrap_or(false)) { continue; }
                for (a, b) in [(tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])] {
                    let (Some(pa), Some(pb)) = (pos.get(a as usize), pos.get(b as usize)) else { continue; };
                    let d = ((pa[0] - pb[0]).powi(2) + (pa[1] - pb[1]).powi(2) + (pa[2] - pb[2]).powi(2)).sqrt() as f64;
                    longest = longest.max(d);
                }
            }
            longest
        }
        let mut scanned = 0usize;
        let mut offset = 0usize;
        let mut torn = 0usize;
        if let Ok(rd) = crate::gamefs::read_dir(crowd) {
            let mut paths: Vec<_> = rd.flatten().map(|e| e.path()).filter(|p| p.extension().and_then(|x| x.to_str()) == Some("png")).collect();
            paths.sort();
            for path in paths {
                let Ok(meta) = crate::gamefs::metadata(&path) else { continue; };
                if meta.len() > 40_000_000 { continue; }
                let Ok(mut session) = crate::portable::load(&path) else { continue; };
                if session.body_rest.as_ref().and_then(|s| vagina_local(s, "cf_J_Vagina_B")).is_none() { continue; }
                let shape = session.shape_body.clone();
                let face = session.shape_face.clone();
                session.apply_shape(&shape, &face);
                session.skin_all();
                let Some(rest) = session.body_rest.as_ref() else { continue; };
                let Some(posed) = session.body_skel.as_ref() else { continue; };
                let (Some(rp), Some(pp)) = (vagina_local(rest, "cf_J_Vagina_B"), vagina_local(posed, "cf_J_Vagina_B")) else { continue; };
                scanned += 1;
                let dp = (0..3).map(|i| (rp[i] - pp[i]).powi(2)).sum::<f32>().sqrt();
                let body = session.parts.iter().find(|p| p.name == "o_body_a");
                let verts = body.map(|p| p.positions.len()).unwrap_or(0);
                let zmax = body.map(|p| {
                    p.positions.iter().zip(&p.uvs).filter(|(_, t)| (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]))
                        .map(|(v, _)| v[2]).fold(f32::NEG_INFINITY, f32::max)
                }).unwrap_or(f32::NAN);
                let edge = body.map(|p| island_edge(&p.positions, &p.uvs, &p.indices)).unwrap_or(0.0);
                let (bounds, longest) = body.map(|p| boundary_in_box(&p.positions, &p.indices, 0.70, 0.86)).unwrap_or((0, 0.0));
                let file = path.file_name().and_then(|s| s.to_str()).unwrap_or("?");
                if dp > 0.005 { offset += 1; }
                if edge > 0.04 || longest > 0.04 { torn += 1; }
                eprintln!("CROWD {file} verts={verts} vagina_b_dp={dp:.4} zmax={zmax:.4} island_edge={edge:.4} boundary={bounds}/{longest:.4}");
                if dp >= 0.002 {
                    eprintln!("FAIL {file} rest={rp:?} posed={pp:?} clothes {}", session.clothes_dynamics.describe_named(posed));
                }
                assert!(dp < 0.002, "{file} T-pose vagina still off the shaped bind by {dp:.4} m");
                if file == "C019.png" {
                    assert!((zmax - 0.03576325).abs() < 0.003, "C019 genital island zmax {zmax} != game dump 0.035763");
                }
            }
        }
        eprintln!("CROWD bp_with_vagina_b={scanned} offset_over_5mm={offset} torn={torn}");
        assert!(scanned >= 30, "expected the BP crowd, scanned {scanned}");
        assert_eq!(offset, 0, "T-pose still displaced {offset} vagina chains");
        let c000 = crowd.join("C000.png");
        if let Ok(mut other) = crate::portable::load(&c000) {
            assert!(other.body_rest.as_ref().and_then(|s| vagina_local(s, "cf_J_Vagina_B")).is_none(),
                "C000 has no BP vagina chain and must stay that way");
            let shape = other.shape_body.clone();
            let face = other.shape_face.clone();
            other.apply_shape(&shape, &face);
            other.skin_all();
            let body = other.parts.iter().find(|p| p.name == "o_body_a").unwrap();
            let zmax = body.positions.iter().zip(&body.uvs)
                .filter(|(_, t)| (0.110..0.204).contains(&t[0]) && (0.575..0.602).contains(&t[1]))
                .map(|(v, _)| v[2]).fold(f32::NEG_INFINITY, f32::max);
            eprintln!("C000 verts={} zmax={zmax:.4}", body.positions.len());
            assert_eq!(body.positions.len(), 9558, "C000 body topology changed");
            assert!((zmax - 0.0359).abs() < 0.002, "C000 genital island moved to {zmax}");
        }
        let c009 = std::path::Path::new(r"C:\portable\OpenKoikatsu\Content\CharacterCards\CrowdAPNG\C009.png");
        if let (Ok(other), Some(rest)) = (crate::portable::load(c009), c019_rest.as_ref()) {
            if let Some(other_rest) = other.body_rest.as_ref() {
                report("REST C019-vs-C009", &bone_map(rest), &bone_map(other_rest));
            }
            let body = other.parts.iter().find(|p| p.name == "o_body_a");
            let c019_body_pending = true;
            let _ = (body, c019_body_pending);
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn k091_tpose_body_shading_basis_matches_game_dump() {
    let game = &crate::test_support::game_path(r"");
    let card = game.join(r"UserData\chara\female\KK_091207.png");
    let dump = game.join(r"mesh_dump\KK_091207.png\outfit0-tpose\Ushiwakamaru_0\mesh_47_o_body_a.json");
    if !card.ok_exists() || !dump.ok_exists() { return; }
    let session = crate::session::open(game, Some(&card), 0).unwrap();
    let body = session.parts.iter().find(|p| p.name == "o_body_a").unwrap();
    let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
    let normals = json["mesh"]["normals"].as_array().unwrap();
    let tangents = json["mesh"]["tangents"].as_array().unwrap();
    // MeshDumper writes sharedMesh.normals/tangents (local, unposed),
    // while worldVertices are BakeMesh results.
    assert_eq!(body.rest_normals.len(), normals.len());
    assert_eq!(body.rest_tangents.len(), tangents.len());
    let angle = |a: [f32; 3], b: [f32; 3]| {
        let dot = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]).clamp(-1.0, 1.0);
        dot.acos().to_degrees() as f64
    };
    let mut nsum = 0.0;
    let mut tsum = 0.0;
    let mut nmax = 0.0f64;
    let mut tmax = 0.0f64;
    for i in 0..normals.len() {
        let read = |v: &serde_json::Value| ["x", "y", "z"].map(|k| v[k].as_f64().unwrap() as f32);
        let n = angle(body.rest_normals[i], read(&normals[i]));
        let t = angle(body.rest_tangents[i][0..3].try_into().unwrap(), read(&tangents[i]));
        nsum += n; tsum += t; nmax = nmax.max(n); tmax = tmax.max(t);
    }
    let normal_mean = nsum / normals.len() as f64;
    let tangent_mean = tsum / tangents.len() as f64;
    eprintln!("K091 body source basis normals mean={normal_mean:.3} max={nmax:.3}; tangents mean={tangent_mean:.3} max={tmax:.3} degrees");
    assert!(normal_mean < 0.1 && tangent_mean < 0.1 && tmax < 1.0);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn march_tpose_face_matches_manual_skin_without_renderer_scale_twice() {
    let game = &crate::test_support::game_path(r"");
    let shot_card = std::path::Path::new(r"C:\portable\OpenKoikatsu\Saved\ShotInputThree\[Card] March_7th_keiyuu.png");
    let card = if shot_card.ok_exists() {
        shot_card.to_path_buf()
    } else {
        game.join(r"UserData\chara\female\[Card] March_7th_keiyuu.png")
    };
    let dump = game.join(r"mesh_dump\[Card] March_7th_keiyuu_outfit0_face.png\outfit0-tpose\March 7th_0\mesh_34_cf_O_face.json");
    if !card.ok_exists() || !dump.ok_exists() { return; }
    let session = crate::session::open(game, Some(&card), 0).unwrap();
    let face = session.parts.iter().find(|p| p.name == "cf_O_face").unwrap();
    let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
    let verts = json["mesh"]["worldVertices"].as_array().unwrap();
    // MeshDumper.cs used BakeMesh then renderer.TransformPoint. BakeMesh
    // already has the world-sized skinned extents, so this double-scales
    // the face. Undo only the dumper's last renderer transform here;
    // compare against its boneWorld * bindpose manual-skinned bounds.
    let renderer = &json["renderer"];
    let pivot: [f64; 3] = ["x", "y", "z"].map(|axis| renderer["position"][axis].as_f64().unwrap());
    let scale: [f64; 3] = ["x", "y", "z"].map(|axis| renderer["lossyScale"][axis].as_f64().unwrap());
    assert_eq!(face.positions.len(), verts.len());
    let dump_uvs = json["mesh"]["uv"].as_array().unwrap();
    assert_eq!(face.uvs.len(), dump_uvs.len());
    let uv_mae: [f64; 2] = ["x", "y"].map(|key| {
        face.uvs.iter().zip(dump_uvs.iter())
            .map(|(uv, row)| (uv[if key == "x" { 0 } else { 1 }] as f64
                - row[key].as_f64().unwrap()).abs())
            .sum::<f64>() / dump_uvs.len() as f64
    });
    eprintln!("March face UV mean absolute error={uv_mae:?}");
    assert!(uv_mae.iter().all(|e| *e < 0.001),
        "face UVs differ from game dump: {uv_mae:?}");
    let mut sum = [0.0f64; 3];
    let mut max = [0.0f64; 3];
    let mut chin = (0usize, [0.0f64; 3], [0.0f64; 3]);
    let mut chin_signed = [0.0f64; 3];
    for (ours, theirs) in face.positions.iter().zip(verts) {
        for (axis, key) in ["x", "y", "z"].iter().enumerate() {
            let dumped = theirs[*key].as_f64().unwrap();
            let size_corrected = pivot[axis] + (dumped - pivot[axis]) / scale[axis];
            let delta = (ours[axis] as f64 - size_corrected).abs();
            sum[axis] += delta;
            max[axis] = max[axis].max(delta);
            if theirs["y"].as_f64().is_some_and(|y| y < 1.32) {
                chin.1[axis] += delta;
                chin.2[axis] = chin.2[axis].max(delta);
                chin_signed[axis] += ours[axis] as f64 - theirs[*key].as_f64().unwrap();
            }
        }
        if theirs["y"].as_f64().is_some_and(|y| y < 1.32) { chin.0 += 1; }
    }
    let mean = sum.map(|s| s / verts.len() as f64);
    eprintln!("March T-pose face mean {mean:?} max {max:?} metres");
    eprintln!("March chin vertices={} mean={:?} max={:?} metres", chin.0,
        chin.1.map(|s| s / chin.0.max(1) as f64), chin.2);
    eprintln!("March chin signed mean={:?} metres", chin_signed.map(|s| s / chin.0.max(1) as f64));
    assert!(mean[0] < 0.001 && mean[1] < 0.001 && mean[2] < 0.001,
        "face geometry differs from manual-skinned game head: mean={mean:?}, max={max:?}");
    let runtime_normals = std::path::Path::new(r"C:\portable\OpenKoikatsu\Saved\DebugShots\[Card] March_7th_keiyuuFaceNormals.tsv");
    if runtime_normals.ok_exists() {
        let rows = crate::gamefs::read_to_string(runtime_normals).unwrap();
        let mut angle_sum = 0.0f64;
        let mut angle_max = 0.0f64;
        let mut chin_sum = 0.0f64;
        let mut chin_count = 0usize;
        let mut count = 0usize;
        for row in rows.lines() {
            let values: Vec<f64> = row.split('\t').filter_map(|s| s.parse().ok()).collect();
            if values.len() != 7 { continue; }
            let i = values[0] as usize;
            let Some(ours) = face.normals.get(i) else { continue; };
            let dot = (0..3).map(|c| ours[c] as f64 * values[c + 4]).sum::<f64>().clamp(-1.0, 1.0);
            let angle = dot.acos().to_degrees();
            angle_sum += angle;
            angle_max = angle_max.max(angle);
            count += 1;
            if (1.27..1.32).contains(&values[2]) {
                chin_sum += angle;
                chin_count += 1;
            }
        }
        eprintln!("March face normal angle mean={} max={} chin={} count={count}",
            angle_sum / count.max(1) as f64, angle_max, chin_sum / chin_count.max(1) as f64);
        assert_eq!(count, face.normals.len());
        // Current Maker idle pose moves the face ~21.6 mm in Z from the
        // dump's T-pose. Its normals differ slightly even with correct
        // skinning, so compare within that measured pose envelope.
        assert!(angle_sum / (count as f64) < 2.0 && angle_max < 4.0,
            "face normals differ from live Maker skinning");
    }
}

#[test]
fn mod_head_face_extents_match_game_manual_skin() {
    let game = &crate::test_support::game_path(r"");
    let cases = [
        (r"UserData\chara\female\KK_384283.png",
         r"mesh_dump\KK_384283.png\outfit0-tpose\不知火 舞_0\mesh_21_cf_O_face.json"),
        (r"UserData\chara\female\Alice_keiyuu.png",
         r"mesh_dump\Hayase_yuuka_keiyuu_V1_02 For KKS_outfit0_face.png\Alice_0-tpose\mesh_41_cf_O_face.json"),
    ];
    for (card_rel, dump_rel) in cases {
        let card = game.join(card_rel);
        let dump = game.join(dump_rel);
        if !card.ok_exists() || !dump.ok_exists() { continue; }
        let session = crate::session::open(game, Some(&card), 0).unwrap();
        let face = session.parts.iter().find(|p| p.name == "cf_O_face").unwrap();
        let (min, max) = crate::pose::aabb(&face.positions);
        let json: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
        let expected = &json["manualSkinnedWorldBounds"]["size"];
        for (i, axis) in ["x", "y", "z"].iter().enumerate() {
            let size = (max[i] - min[i]) as f64;
            let game_size = expected[*axis].as_f64().unwrap();
            assert!((size - game_size).abs() < 0.002,
                "{card_rel} face {axis} size {size} vs game skin {game_size}");
        }
    }
}

#[test]
fn ibl_alpha_uses_pass_state_even_with_transparent_queue_and_nose_name() {
    let mut mesh = ExtractedMesh::default();
    mesh.name = "O_nosetama".into();
    mesh.mat = Some(crate::unity::MeshMaterial {
        shader: "IBL_Shader_alpha".into(),
        render_queue: 3030,
        floats: [("_Metallic".into(), 0.2), ("_Glossiness".into(), 0.6)].into(),
        ..Default::default()
    });
    let (blend, unlit) = blend_for_mesh("accessory", &mesh);
    assert_eq!(blend, "translucent");
    assert!(!unlit);
    let part = crate::session::SessionPart::from_mesh("accessory", &mesh, &blend, unlit);
    assert_eq!(part.shader_kind, 6);
    assert_eq!(part.metallic, 0.2);
    assert_eq!(part.smoothness, 0.6);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn material_editor_copy_replays_accessory_overlay() {
    let card = &crate::test_support::game_path(r"\UserData\chara\female\Alice_keiyuu.png");
    if !card.ok_exists() { return; }
    let info = crate::card::parse_card_outfit(card, 0).unwrap();
    let game = &crate::test_support::game_path(r"");
    assert!(me_shader_registered(game, "Shader Forge/main_item_studio_alpha"));
    assert!(me_shader_registered(game, "IBL_Shader_alpha"));
    assert_eq!(me_shader_registration(game, "IBL_Shader_alpha"), (true, Some(true)));
    assert_eq!(me_shader_registration(game, "Shader Forge/toon_glasses_lod0"), (true, Some(false)));
    assert!(!me_shader_registered(game, "LTZ/RGB_Disturbance"));
    let copy = info.me_copies.iter().find(|c| c.object_type == 2 && c.coordinate == 0 && c.slot == 24)
        .expect("Alice slot 24 has a Material Editor overlay");
    assert_eq!(copy.source, "mf_m_primmaterial");
    assert_eq!(copy.copy, "mf_m_primmaterial.mecopy1");
    let mut mesh = ExtractedMesh::default();
    mesh.mat = Some(crate::unity::MeshMaterial { name: "mf_m_primmaterial".into(), ..Default::default() });
    let mut meshes = vec![mesh];
    apply_me_material_copies(&mut meshes, &info.me_copies, 0, "accessory", Some("outfit0.accessory24.ChaFileAccessory.PartsInfo.id"));
    assert_eq!(meshes.len(), 2, "copies={:?}", info.me_copies.iter().filter(|c| c.object_type == 2 && c.coordinate == 0 && c.slot == 24).collect::<Vec<_>>());
    assert_eq!(crate::card::me_mat_key(&meshes[1].mat.as_ref().unwrap().name), copy.copy);
    assert!(me_maintex(Some(&info.me_tex), 0, "accessory", Some("outfit0.accessory24.ChaFileAccessory.PartsInfo.id"), &meshes[1]).is_some());
    let hair_copy = info.me_copies.iter().find(|c| c.object_type == 2 && c.coordinate == 0 && c.slot == 2).unwrap();
    let (shader, _) = info.me_shader.get(&(2, 0, 2, hair_copy.copy.clone())).unwrap();
    assert_eq!(shader, "Shader Forge/main_item_studio_alpha");
    let mut copied_hair = ExtractedMesh::default();
    copied_hair.hair_shader_hint = true;
    copied_hair.mat = Some(crate::unity::MeshMaterial {
        name: hair_copy.copy.clone(), shader: shader.clone(),
        keywords: vec!["_ALPHATEST_ON".into()], ..Default::default()
    });
    let (blend, unlit) = blend_for_mesh("accessory", &copied_hair);
    assert_eq!(blend, "translucent");
    assert!(unlit);
    assert_eq!(crate::session::SessionPart::from_mesh("accessory", &copied_hair, &blend, unlit).shader_kind, 4);
    assert_eq!(info.me_float.get(&(2, 0, 2, hair_copy.copy.clone(), "alpha".into())).copied(), Some(0.8));
}
