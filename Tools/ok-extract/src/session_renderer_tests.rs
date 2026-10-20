#[allow(unused_imports)] use crate::gamefs::GamePath as _;
#[test]
#[ignore="read-only published p202 SkinPlus inputs; no shader or cache mutations"]
fn cached_skinplus_source_inputs() {
    let root=Path::new("C:/portable/OpenKoikatsu");
    let directory=root.join("Saved/Tests/Skin325/CachedInputs");std::fs::create_dir_all(&directory).unwrap();
    let comparison:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Logs/Texture324SessionComparison.json")).unwrap()).unwrap();
    let wanted:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Tests/Skin325/WantedNativeInputs.json")).unwrap()).unwrap();
    assert_eq!(comparison["passed"],true);
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let mut rows=Vec::new();let mut cards=0;
    for card in comparison["cards"].as_array().unwrap() {
        let key=card["keys"][1].as_str().unwrap();assert!(key.contains("|p202|"));
        assert!(key.ends_with("|OK_SHOES_TYPE="));
        let bytes:Vec<u8>=db.query_row("SELECT payload FROM session_blob WHERE card_key=?1",[key],|r|r.get(0)).unwrap();
        let blob:SessionBlob=bincode::deserialize(&bytes).unwrap();cards+=1;
        for (ordinal,part) in blob.parts.iter().enumerate() {
            let Some(material)=part.source_material.as_ref() else{continue;};
            if !wanted.as_array().unwrap().iter().any(|w|w["card"]==card["card"] && w["mesh"]==part.name
                && crate::card::me_mat_key(w["material"].as_str().unwrap())==crate::card::me_mat_key(&material.name)) {continue;}
            let mut textures=serde_json::Map::new();
            for (name,png) in [("_MainTex",&part.png),("_AlphaMask",&part.alpha_mask),
                ("_DetailMask",&part.detail_mask),("_LineMask",&part.line_mask),
                ("_NormalMap",&part.normal_map),("_NormalMask",&part.normal_mask),
                ("_NormalMapDetail",&part.normal_detail),("_overtex1",&part.overtex_png),
                ("_overtex2",&part.overtex2_png)] {
                if let Some(png)=png {
                    let path=directory.join(format!("{}_part{ordinal}{name}.png",card["card"].as_str().unwrap()));
                    std::fs::write(&path,png).unwrap();textures.insert(name.into(),serde_json::json!({"path":path,"bytes":png.len(),"transport":"legacy field"}));
                }
            }
            for (name,png) in &part.shader_textures {
                let path=directory.join(format!("{}_part{ordinal}_generic{name}.png",card["card"].as_str().unwrap()));
                std::fs::write(&path,png).unwrap();textures.insert(name.clone(),serde_json::json!({"path":path,"bytes":png.len(),"transport":"shader_textures"}));
            }
            rows.push(serde_json::json!({"card":card["card"],"cache_key":key,"part":ordinal,"role":part.role,"mesh":part.name,
                "shader_kind":part.shader_kind,"material":material,"textures":textures,"runtime_textures":part.runtime_textures,
                "uv":part.uvs,"uv1":part.uv2,"uv2":part.uv3,"uv3":part.uv4,"vertex_colors":part.vertex_colors,
                "legacy_display_values":{"overcolor1":part.overcolor,"overcolor2":part.overcolor2,"nipsize":part.nip_size,"nip_specular":part.nip_specular}}));
        }
    }
    assert_eq!(cards,34);assert_eq!(rows.len(),wanted.as_array().unwrap().len());assert!(!rows.is_empty());
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&serde_json::json!({"cache_cards":cards,"rows":rows,"scope":"exact published p202 cache inputs; not native parity"})).unwrap()).unwrap();
}

#[test]
#[ignore="installed newly added clothing counterexample cached material inputs"]
fn cached_clothing_counterexample_inputs() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let bytes:Vec<u8>=db.query_row("SELECT payload FROM session_blob WHERE card_key LIKE '%/兔田佩克拉.png|%' AND card_key LIKE '%|p199|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1",[],|r|r.get(0)).unwrap();
    let s:SessionBlob=bincode::deserialize(&bytes).unwrap();let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Hair322ClothingInputs");std::fs::create_dir_all(root).unwrap();let mut rows=Vec::new();
    for (i,p) in s.parts.iter().enumerate() {
        let png=root.join(format!("part{i}.png"));if let Some(bytes)=&p.png{std::fs::write(&png,bytes).unwrap();}
        rows.push(serde_json::json!({"part":i,"name":p.name,"role":p.role,"shader_kind":p.shader_kind,"visible":p.render_visible,"material":p.source_material,"png":png,"runtime_textures":p.runtime_textures,"blend":p.blend,"uvs":p.uvs,"uv2":p.uv2}));
    }
    std::fs::write(root.join("inputs.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="read-only complete p199/p200 capture caches; run after all34 capture"]
fn compare_footwear_visibility_session_caches() {
    // MeshMaterial uses HashMap: deserializing assigns randomized iteration
    // order. Canonicalize only map keys, preserving every serialized value bit.
    fn canonical_part(part:&mut PartBlob)->Vec<u8> {
        fn ordered_value(value:&mut serde_json::Value) {
            use serde_json::Value;
            match value {
                Value::Object(map)=>for (key,item) in map.iter_mut() {
                    if matches!(key.as_str(),"implanted_bones"|"removed_accessory_bones") {
                        item.as_array_mut().unwrap().sort_by_key(|v|v.as_i64().unwrap());
                    }
                    ordered_value(item);
                },
                Value::Array(items)=>for item in items {ordered_value(item);},
                Value::Number(number) if number.is_f64()=>{
                    let bits=number.as_f64().unwrap().to_bits();
                    *value=serde_json::json!({"__float64_bits":bits});
                },
                _=>{},
            }
        }
        let material=part.source_material.take();
        let runtime=part.runtime_accessory.take();
        let mut bytes=bincode::serialize(part).unwrap();
        if let Some(m)=&material {
            use std::collections::BTreeMap;
            bytes.extend(bincode::serialize(&(&m.name,&m.shader,m.shader_has_rim,m.render_queue,
                &m.keywords,m.colors.iter().collect::<BTreeMap<_,_>>(),
                m.floats.iter().collect::<BTreeMap<_,_>>(),&m.tex_slots,
                m.tex_wraps.iter().collect::<BTreeMap<_,_>>())).unwrap());
        }
        // Private skeletons/deferred colliders contain additional HashMaps,
        // and the two bone membership fields are HashSets, not ordered arrays.
        let mut runtime_value=serde_json::to_value(&runtime).unwrap();
        ordered_value(&mut runtime_value);
        bytes.extend(serde_json::to_vec(&runtime_value).unwrap());
        part.source_material=material;
        part.runtime_accessory=runtime;
        bytes
    }
    let root=Path::new("C:/portable/OpenKoikatsu");
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Logs/Hair323Inventory.json")).unwrap()).unwrap();
    let mut rows=Vec::new();
    for item in inventory.as_array().unwrap() {
        let name=item["card"].as_str().unwrap();let mut sessions=Vec::new();let mut keys=Vec::new();
        for pipeline in [199,200] {
            let (key,bytes):(String,Vec<u8>)=db.query_row("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE ?2 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' AND card_key NOT LIKE '%OK_SHOES_TYPE=0|%' AND card_key NOT LIKE '%OK_SHOES_TYPE=0' ORDER BY card_key DESC LIMIT 1",[format!("%/{name}|%"),format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
            keys.push(key);sessions.push(bincode::deserialize::<SessionBlob>(&bytes).unwrap());
        }
        let old_bytes:Vec<_>=sessions[0].parts.iter_mut().map(canonical_part).collect();
        let new_bytes:Vec<_>=sessions[1].parts.iter_mut().map(canonical_part).collect();
        let (old,new)=(&sessions[0],&sessions[1]);let mut cursor=0;let mut removed=Vec::new();
        for (part,bytes) in new.parts.iter().zip(&new_bytes) {
            while cursor<old.parts.len() && old_bytes[cursor]!=*bytes {
                assert_eq!(old.parts[cursor].role,"clothes_shoes","unexpected altered/removed part {name} {}",old.parts[cursor].name);
                removed.push(old.parts[cursor].name.clone());cursor+=1;
            }
            assert!(cursor<old.parts.len(),"new/changed part {name} {}",part.name);
            cursor+=1;
        }
        for part in &old.parts[cursor..] {assert_eq!(part.role,"clothes_shoes");removed.push(part.name.clone());}
        rows.push(serde_json::json!({"card":name,"keys":keys,"old_parts":old.parts.len(),"new_parts":new.parts.len(),"removed_inactive_shoes":removed,"retained_parts_byte_identical":new.parts.len()}));
    }
    assert_eq!(rows.len(),34);
    std::fs::write(root.join("Saved/Logs/Hair323FootwearSessionComparison.json"),serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"scope":"read-only cached payloads: retained fields exact after canonical unordered maps/bone membership sets; floating point bits preserved; only inactive shoe parts removed; not free-running physics parity","cards":rows})).unwrap()).unwrap();
}

#[test]
#[ignore="read-only p200/p202 integration; run after all requested captures"]
fn compare_material_editor_texture_transform_caches() {
    let root=Path::new("C:/portable/OpenKoikatsu");
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Logs/Hair323Inventory.json")).unwrap()).unwrap();
    let names:Vec<String>=std::env::var("OK_TEXTURE_ST_COMPARE_NAMES").ok().map(|s|serde_json::from_str(&s).unwrap())
        .unwrap_or_else(||inventory.as_array().unwrap().iter().map(|v|v["card"].as_str().unwrap().into()).collect());
    let mut rows=Vec::new();
    for name in names {
        let mut sessions=Vec::new();let mut keys=Vec::new();
        for pipeline in [200,202] {
            let (key,bytes):(String,Vec<u8>)=db.query_row("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE ?2 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' AND substr(card_key,-length('|OK_SHOES_TYPE='))='|OK_SHOES_TYPE=' ORDER BY card_key DESC LIMIT 1",[format!("%/{name}|%"),format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
            keys.push(key);sessions.push(bincode::deserialize::<SessionBlob>(&bytes).unwrap());
        }
        assert_eq!(keys[0].replace("|p200|","|p202|"),keys[1],"Comparison must use the same complete source/configuration identity");
        let (old,new)=(&sessions[0],&sessions[1]);assert_eq!(old.parts.len(),new.parts.len());
        let mut changes=Vec::new();
        for (a,b) in old.parts.iter().zip(&new.parts) {
            assert_eq!((&a.role,&a.name),(&b.role,&b.name));
            // Full binary image buffers and ordered geometry, including all UV
            // channels and palettes, must remain bit-identical.
            let fixed=|p:&PartBlob|bincode::serialize(&(
                (&p.positions,&p.rest_positions,&p.normals,&p.rest_normals,&p.rest_tangents,&p.indices,&p.weights,&p.bone_idx,&p.bind_poses),
                (&p.uvs,&p.uv2,&p.uv3,&p.uv4,&p.vertex_colors),
                (&p.png,&p.alpha_mask,&p.detail_mask,&p.line_mask,&p.hair_gloss,&p.normal_map,&p.normal_detail,&p.normal_mask),
                (&p.overtex_png,&p.overtex2_png,&p.shader_textures,&p.runtime_textures),
                (&p.blend,p.unlit,p.shader_kind,p.render_visible)
            )).unwrap();
            if fixed(a)!=fixed(b) {
                eprintln!("Mismatched cache keys: {}\n{}",keys[0],keys[1]);
                macro_rules! diagnose {($($field:ident),*)=>{$(
                    let before=bincode::serialize(&a.$field).unwrap();let after=bincode::serialize(&b.$field).unwrap();
                    if before!=after {eprintln!("DIFF {} old_len={} new_len={} first_byte_difference={:?}",
                        stringify!($field),before.len(),after.len(),before.iter().zip(&after).position(|(a,b)|a!=b));}
                )*};}
                diagnose!(positions,rest_positions,normals,rest_normals,rest_tangents,indices,weights,bone_idx,bind_poses,
                    uvs,uv2,uv3,uv4,vertex_colors,png,alpha_mask,detail_mask,line_mask,hair_gloss,normal_map,normal_detail,
                    normal_mask,overtex_png,overtex2_png,shader_textures,runtime_textures,blend,unlit,shader_kind,render_visible);
                panic!("altered texture/geometry {name} {}",b.name);
            }
            if let (Some(before),Some(after))=(&a.source_material,&b.source_material) {
                assert_eq!(before.name,after.name);assert_eq!(before.shader,after.shader);
                assert_eq!(before.colors,after.colors);assert_eq!(before.tex_slots,after.tex_slots);
                assert_eq!(before.keywords,after.keywords);assert_eq!(before.render_queue,after.render_queue);
                assert_eq!(before.tex_wraps,after.tex_wraps);assert_eq!(before.shader_has_rim,after.shader_has_rim);
                let mut changed=Vec::new();
                for key in before.floats.keys().chain(after.floats.keys()) {
                    if before.floats.get(key)!=after.floats.get(key) {
                        let lower=key.to_ascii_lowercase();
                        assert!(["_st_x","_st_y","_st_z","_st_w"].iter().any(|s|lower.ends_with(s)),"non-ST value change {name} {key}");
                        changed.push((key.clone(),after.floats.get(key).copied()));
                    }
                }
                changed.sort_by(|a,b|a.0.cmp(&b.0));changed.dedup();
                if !changed.is_empty(){
                    let mut st=serde_json::json!({"_MainTex":b.main_tex_st,"_AlphaMask":b.alpha_mask_st,"_NormalMap":b.normal_map_st,
                        "_NormalMapDetail":b.normal_detail_st,"_NormalMask":b.normal_mask_st,
                        "_DetailMask":b.detail_mask_st,"_LineMask":b.line_mask_st,"_HairGloss":b.hair_gloss_st});
                    if let Some(draw)=&b.item_draw {st.as_object_mut().unwrap().insert("_ColorMask".into(),serde_json::json!(draw.color_mask_st));}
                    let unsupported:Vec<_>=changed.iter().filter(|(key,value)|value.is_some() && !st.as_object().unwrap().keys()
                        .any(|prop|key.to_ascii_lowercase().starts_with(&(prop.to_ascii_lowercase()+"_st_"))))
                        .map(|(key,_)|key.clone()).collect();
                    changes.push(serde_json::json!({"name":b.name,"role":b.role,"material":after.name,
                        "properties":changed,"st":st,"uncovered_shader_st_components":unsupported}));
                }
                let expected=SessionPart::from_mesh(&b.role,&ExtractedMesh {name:b.name.clone(),mat:Some(after.clone()),..Default::default()},&b.blend,b.unlit);
                assert_eq!(b.main_tex_st,expected.main_tex_st);assert_eq!(b.alpha_mask_st,expected.alpha_mask_st);
                assert_eq!(b.normal_map_st,expected.normal_map_st);assert_eq!(b.normal_detail_st,expected.normal_detail_st);
                assert_eq!(b.normal_mask_st,expected.normal_mask_st);assert_eq!(b.detail_mask_st,expected.detail_mask_st);
                assert_eq!(b.line_mask_st,expected.line_mask_st);assert_eq!(b.hair_gloss_st,expected.hair_gloss_st);
                assert_eq!(b.item_draw.as_ref().map(|d|d.color_mask_st),expected.item_draw.as_ref().map(|d|d.color_mask_st));
                if name=="KK_380982.png" && b.role=="body" {assert_eq!(b.normal_map_st,[5.,5.,0.,0.]);assert_eq!(a.normal_map_st,[1.,1.,0.,0.]);}
            }else{assert!(a.source_material.is_none()&&b.source_material.is_none());}
        }
        eprintln!("ST comparison {name}: parts={} changed={}",new.parts.len(),changes.len());
        rows.push(serde_json::json!({"card":name,"keys":keys,"parts":new.parts.len(),"changes":changes}));
    }
    let output=std::env::var("OK_TEXTURE_ST_COMPARE_OUTPUT").unwrap_or_else(|_|"Saved/Logs/Texture324SessionComparison.json".into());
    std::fs::write(root.join(output),serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"scope":"all requested cached geometry/UV/images/palettes unchanged; material changes restricted to ST; stored ST matches source values; not complete SkinPlus/lighting parity","cards":rows})).unwrap()).unwrap();
}

#[test]
#[ignore="installed source-only replay for ambiguous same-mesh accessory slots"]
fn trace_texture_transform_source_identities() {
    let root=Path::new("C:/portable/OpenKoikatsu");let game=&crate::test_support::game_path("");
    let names:Vec<String>=serde_json::from_str(&std::env::var("OK_TEXTURE_ST_IDENTITY_NAMES").unwrap()).unwrap();
    let dir=root.join("Saved/Tests/Texture324SourceIdentity");std::fs::create_dir_all(&dir).unwrap();
    let db=rusqlite::Connection::open_with_flags(game.join(".ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let comparison_path=root.join("Saved/Logs/Texture324SessionComparison.json");
    let comparison:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&comparison_path).unwrap()).unwrap();
    let mut cards=Vec::new();
    for name in names {
        let report=comparison["cards"].as_array().unwrap().iter().find(|c|c["card"]==name).unwrap();
        let mut sessions=Vec::new();
        for key in report["keys"].as_array().unwrap() {
            let bytes:Vec<u8>=db.query_row("SELECT payload FROM session_blob WHERE card_key=?1",[key.as_str().unwrap()],|r|r.get(0)).unwrap();
            sessions.push(bincode::deserialize::<SessionBlob>(&bytes).unwrap());
        }
        let manifest_path=dir.join(format!("{name}.manifest.json"));
        std::env::set_var("OK_SOURCE_PART_MANIFEST_OUTPUT",&manifest_path);
        let card_path=game.join("UserData/chara/female").join(&name);
        let fresh=crate::extract_session(game,Some(&card_path),0,None).unwrap();
        std::env::remove_var("OK_SOURCE_PART_MANIFEST_OUTPUT");
        let manifest:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&manifest_path).unwrap()).unwrap();
        let sources=manifest["parts"].as_array().unwrap();let cached=&sessions[1];
        assert_eq!(sources.len(),cached.parts.len());assert_eq!(fresh.parts.len(),cached.parts.len());
        let mut entries=Vec::new();let mut change_ordinal=0;
        for (index,((source,part),replay)) in sources.iter().zip(&cached.parts).zip(&fresh.parts).enumerate() {
            assert_eq!(source["mesh"].as_str(),Some(part.name.as_str()));assert_eq!(source["role"].as_str(),Some(part.role.as_str()));
            assert_eq!((&replay.name,&replay.role),(&part.name,&part.role));
            assert_eq!(serde_json::to_value(&replay.source_material).unwrap(),serde_json::to_value(&part.source_material).unwrap(),"source material replay mismatch {name} part{index}");
            let old=&sessions[0].parts[index];
            if old.source_material.as_ref().map(|m|&m.floats)!=part.source_material.as_ref().map(|m|&m.floats) {
                let changed=&report["changes"][change_ordinal];
                assert_eq!(changed["name"].as_str(),Some(part.name.as_str()));assert_eq!(changed["role"].as_str(),Some(part.role.as_str()));
                let slot=crate::me_slot(&part.role,source["property"].as_str()).unwrap();
                let field=if part.role=="accessory" {"accessory"} else if part.role.contains("hair") {"hair"} else if part.role.starts_with("clothes_") {"clothes"} else {part.role.as_str()};
                entries.push(serde_json::json!({"change_ordinal":change_ordinal,"part_index":index,"mesh":part.name,
                    "source_mesh":source["source_mesh"].as_str().unwrap_or(part.name.as_str()),
                    "material":part.source_material.as_ref().unwrap().name,"field":field,"slot":slot,"property":source["property"]}));
                change_ordinal+=1;
            }
        }
        assert_eq!(change_ordinal,report["changes"].as_array().unwrap().len());
        cards.push(serde_json::json!({"card":name,"manifest":manifest_path,"source_card":card_path,"parts":cached.parts.len(),"entries":entries}));
    }
    let output=std::env::var("OK_TEXTURE_ST_IDENTITY_OUTPUT").unwrap_or_else(|_|"Saved/Logs/Texture324SourceIdentityReplay.json".into());
    std::fs::write(root.join(output),serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"comparison":comparison_path,"scope":"source-only ordered manifest replay; each cached role/mesh/material matches fresh extraction; no native ST value used to choose slots","cards":cards})).unwrap()).unwrap();
}

#[test]
#[ignore="read-only installed p197/p199 complete legacy regression caches"]
fn compare_hair_source_session_caches() {
    let root=Path::new("C:/portable/OpenKoikatsu");
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Saved/Logs/DumpFourViewsInventory.json")).unwrap()).unwrap();
    let mut rows=Vec::new();
    for item in inventory.as_array().unwrap() {
        let name=item["card"].as_str().unwrap();let mut sessions=Vec::new();let mut keys=Vec::new();
        for pipeline in [197,199] {
            let (key,bytes):(String,Vec<u8>)=db.query_row("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE ?2 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1",[format!("%/{name}|%"),format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
            keys.push(key);sessions.push(bincode::deserialize::<SessionBlob>(&bytes).unwrap());
        }
        let (old,new)=(&sessions[0],&sessions[1]);assert_eq!(old.parts.len(),new.parts.len());let mut changes=Vec::new();let mut protected=0;
        for (a,b) in old.parts.iter().zip(&new.parts) {
            assert_eq!((&a.role,&a.name),(&b.role,&b.name));
            assert_eq!(a.positions,b.positions,"geometry {}",b.name);assert_eq!(a.normals,b.normals,"normals {}",b.name);
            assert_eq!(a.indices,b.indices);assert_eq!(a.uvs,b.uvs);assert_eq!(a.uv2,b.uv2);
            let changed=a.png!=b.png || a.shader_textures!=b.shader_textures || a.hair_gloss!=b.hair_gloss || a.shader_kind!=b.shader_kind || serde_json::to_value(&a.source_material).unwrap()!=serde_json::to_value(&b.source_material).unwrap();
            let native_hair=b.source_material.as_ref().is_some_and(|m|crate::is_native_hair_shader(&m.shader));
            if !native_hair {
                assert_eq!(a.alpha_mask,b.alpha_mask);assert_eq!(a.detail_mask,b.detail_mask);assert_eq!(a.line_mask,b.line_mask);
                assert_eq!(a.normal_map,b.normal_map);assert_eq!(a.normal_detail,b.normal_detail);assert_eq!(a.normal_mask,b.normal_mask);
                assert_eq!(a.overtex_png,b.overtex_png);assert_eq!(a.overtex2_png,b.overtex2_png);
                assert_eq!(a.runtime_textures,b.runtime_textures);assert_eq!(a.blend,b.blend);assert_eq!(a.unlit,b.unlit);
            }
            if changed {assert!(native_hair,"unexpected nonhair input change {name} {}",b.name);changes.push(b.name.clone());}
            if b.role=="head" {assert!(!changed,"head/brow/eye input changed {name} {}",b.name);protected+=1;}
        }
        rows.push(serde_json::json!({"card":name,"keys":keys,"parts":new.parts.len(),"protected_head_parts":protected,"changed_hair_inputs":changes}));
    }
    assert_eq!(rows.len(),17);
    std::fs::write(root.join("Saved/Logs/Hair322SessionComparison.json"),serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"scope":"17 legacy cards: identical geometry/normals/UV/indices; head inputs unchanged; changes restricted to native hair material inputs","cards":rows})).unwrap()).unwrap();
}

#[test]
#[ignore="strict read-only p196/p197 renderer-slot integration"]
fn compare_eyeline_slot_session_caches() {
    let game=&crate::test_support::game_path("");
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));let guids=crate::mods::find_guids(&game.join("mods"),None);
    let db=rusqlite::Connection::open_with_flags(game.join(".ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Logs/DumpFourViewsInventory.json").unwrap()).unwrap();
    let directory=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Brow321CachedSlots");std::fs::create_dir_all(directory).unwrap();
    let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Tests/Brow321SlotInputs/inputs.json").unwrap()).unwrap();
    let mut rows=Vec::new();let mut slot_rows=Vec::new();
    for item in inventory.as_array().unwrap() {
        let name=item["card"].as_str().unwrap();let card=crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        let mut sessions=Vec::new();let mut keys=Vec::new();
        for pipeline in [196,197] {
            let (key,bytes):(String,Vec<u8>)=db.query_row("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE ?2 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1",[format!("%/{name}|%"),format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
            keys.push(key);sessions.push(bincode::deserialize::<SessionBlob>(&bytes).unwrap());
        }
        let (old,new)=(&sessions[0],&sessions[1]);assert_eq!(old.parts.len(),new.parts.len());let mut changes=Vec::new();
        for (a,b) in old.parts.iter().zip(&new.parts) {
            assert_eq!((&a.role,&a.name),(&b.role,&b.name));assert_eq!(a.uvs,b.uvs);assert_eq!(a.indices,b.indices);
            assert_eq!(a.shader_textures,b.shader_textures);assert_eq!(a.overtex_png,b.overtex_png);assert_eq!(a.positions,b.positions);assert_eq!(a.normals,b.normals);
            if a.png!=b.png {
                assert!(b.name.to_ascii_lowercase().contains("eyeline") || b.name.to_ascii_lowercase().contains("mayuge"),"unexpected changed texture {}",b.name);
                let mut mesh=crate::unity::ExtractedMesh{name:b.name.clone(),mat:b.source_material.clone(),..Default::default()};
                crate::resolve_head_detail_texture(game,&vanilla,&mods,&card,&guids,0,&mut mesh,&[],&[]);
                assert_eq!(crate::teximg::load_from_memory(mesh.texture_png.as_ref().unwrap()).unwrap().to_rgba8(),crate::teximg::load_from_memory(b.png.as_ref().unwrap()).unwrap().to_rgba8(),"changed texture must derive from selected source and actual material slot");
                changes.push(b.name.clone());
            }
        }
        if let Some(source)=native.as_array().unwrap().iter().find(|r|r["card"].as_str()==Some(name)) {
            let mut source=source.clone();
            for part in source["parts"].as_array_mut().unwrap() {
                let p=new.parts.iter().find(|p|p.name==part["name"].as_str().unwrap()).unwrap();
                let path=directory.join(format!("{}.{}.png",Path::new(name).file_stem().unwrap().to_str().unwrap(),p.name));std::fs::write(&path,p.png.as_ref().unwrap()).unwrap();
                part["png"]=serde_json::json!(path);part["uvs"]=serde_json::json!(p.uvs);
                assert_eq!(p.source_material.as_ref().unwrap().name,part["material"].as_str().unwrap());
            }
            slot_rows.push(source);
        }
        if name=="KK_381623.png" {std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/EyeRim320Inputs/final321_cached_face.png",new.parts.iter().find(|p|p.name=="cf_O_face").unwrap().png.as_ref().unwrap()).unwrap();}
        rows.push(serde_json::json!({"card":name,"keys":keys,"changed_pngs":changes,"parts":new.parts.len()}));
    }
    assert_eq!(rows.len(),17);assert_eq!(slot_rows.len(),6);
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&slot_rows).unwrap()).unwrap();
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Brow321SessionComparison.json",serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"cases":rows,"scope":"all17 UV/index/shader textures/overlay/positions/normals unchanged; every changed main PNG re-resolved from source material and selected catalog"})).unwrap()).unwrap();
}

#[test]
#[ignore="installed source renderer slot1 and native eyeline UV oracle inputs"]
fn native_eyeline_slot_inputs() {
    let game=&crate::test_support::game_path("");let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));let guids=crate::mods::find_guids(&game.join("mods"),None);
    let directory=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Brow321SlotInputs");std::fs::create_dir_all(directory).unwrap();let mut rows=Vec::new();
    for (i,(name,native)) in [
        ("KK_226176.png","Eyeline320KK226176Native/Card0.json"),
        ("KK_381623.png","Eyeline320Native/Card0.json"),
        ("弹丸论破 - 江之岛盾子2.png","Eyeline320Native/Card1.json"),
        ("Hayase_yuuka_keiyuu_V1_02 For KKS.png","Eyeline320Native/Card2.json"),
        ("KK_388205.png","Eyeline320Native/Card3.json"),
        ("KK_384283.png","Eyeline320Native/Card4.json"),
    ].iter().enumerate() {
        let card=crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        let entry=crate::resolve_face_entry(&vanilla,&card,&guids,&mut std::collections::HashMap::new());
        let bytes=if let Some(e)=entry.as_ref().filter(|e|!e.main_ab.is_empty()&&e.main_ab!="0"&&!e.main_ab.eq_ignore_ascii_case("chara/bo_head_00.unity3d")) {
            crate::accessory_bundle_bytes(game,e.extra.get("__zip").map(|s|Path::new(s)),e).unwrap()
        } else {crate::gamefs::read(game.join("abdata/chara/bo_head_00.unity3d")).unwrap().into()};
        let parsed=crate::unity::parse_bundle_cached(bytes);
        let main=entry.as_ref().map(|e|e.info("MainData")).filter(|s|!s.is_empty()&&s!="0").unwrap_or_else(||"p_cf_head_00".into());
        let (meshes,_)=parsed.meshes_for_maindata(&main);let stroke=crate::pick_named(&meshes,"cf_O_eyeline").unwrap();
        let actual_slot=parsed.renderer_material_slot(&stroke.renderer_ids,1).expect("native source slot1 required; fallback is not an oracle");
        let mut shadow=crate::eyeline_shadow_mesh(&parsed,stroke);assert_eq!(shadow.mat.as_ref().unwrap().name,actual_slot.name);
        let mut upper=stroke.clone();let lower=crate::pick_named(&meshes,"cf_O_eyeline_low").unwrap().clone();
        let mut parts=Vec::new();
        for (slot,mut mesh) in [(0,upper),(1,shadow),(0,lower)] {
            crate::resolve_head_detail_texture(game,&vanilla,&mods,&card,&guids,0,&mut mesh,&[],&[]);
            let path=directory.join(format!("Card{i}.{}.png",mesh.name));std::fs::write(&path,mesh.texture_png.as_ref().unwrap()).unwrap();
            parts.push(serde_json::json!({"name":mesh.name,"material_slot":slot,"material":mesh.mat.as_ref().unwrap().name,"png":path,"uvs":mesh.uvs}));
        }
        rows.push(serde_json::json!({"card":name,"native":format!("C:/portable/OpenKoikatsu/Saved/Tests/{native}"),"parts":parts}));
    }
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="read-only actual pipeline196 face overlay output"]
fn cached_legacy_face_texture_inputs() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let bytes:Vec<u8>=db.query_row("SELECT payload FROM session_blob WHERE card_key LIKE '%/KK_381623.png|%' AND card_key LIKE '%|p196|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1",[],|r|r.get(0)).unwrap();
    let s:SessionBlob=bincode::deserialize(&bytes).unwrap();let p=s.parts.iter().find(|p|p.name=="cf_O_face").unwrap();
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/EyeRim320Inputs/final_cached_face.png",p.png.as_ref().unwrap()).unwrap();
}

#[test]
#[ignore="strict installed p195/p196 legacy overlay cache comparison"]
fn compare_legacy_overlay_session_caches() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Logs/DumpFourViewsInventory.json").unwrap()).unwrap();
    let mut rows=Vec::new();let mut changed=0;
    for item in inventory.as_array().unwrap() {
        let name=item["card"].as_str().unwrap();let card=crate::card::parse_card(&&crate::test_support::game_path("/UserData/chara/female").join(name)).unwrap();
        let mut sessions=Vec::new();let mut keys=Vec::new();
        for pipeline in [195,196] {
            let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE ?2 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
            let (key,bytes):(String,Vec<u8>)=st.query_row([format!("%/{name}|%"),format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
            keys.push(key);sessions.push(bincode::deserialize::<SessionBlob>(&bytes).unwrap());
        }
        let (old,new)=(&sessions[0],&sessions[1]);assert_eq!(old.parts.len(),new.parts.len());let mut png_changes=Vec::new();let mut position_max=0f32;
        for (a,b) in old.parts.iter().zip(&new.parts) {
            assert_eq!((&a.role,&a.name),(&b.role,&b.name));assert_eq!(a.uvs,b.uvs);assert_eq!(a.indices,b.indices);assert_eq!(a.shader_textures,b.shader_textures);assert_eq!(a.overtex_png,b.overtex_png);
            if a.png!=b.png {
                assert!(b.name.eq_ignore_ascii_case("cf_O_face"),"unexpected texture {}",b.name);
                let overlay=crate::ksox_overlay_png(&card,0,2).expect("changed face requires saved source overlay");
                let expected=crate::composite_overlay(a.png.as_ref().unwrap(),&overlay).unwrap();
                assert_eq!(crate::teximg::load_from_memory(&expected).unwrap().to_rgba8(),crate::teximg::load_from_memory(b.png.as_ref().unwrap()).unwrap().to_rgba8(),"changed face must derive exactly from existing generic overlay kernel");
                png_changes.push(b.name.clone());changed+=1;
                std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/EyeRim320Inputs/final_cached_face.png",b.png.as_ref().unwrap()).unwrap();
            }
            assert_eq!(a.positions.len(),b.positions.len());for (a,b) in a.positions.iter().zip(&b.positions) {position_max=position_max.max(a.iter().zip(b).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt());}
        }
        rows.push(serde_json::json!({"card":name,"keys":keys,"parts":new.parts.len(),"png_changes":png_changes,"position_max_m":position_max}));
    }
    assert_eq!(rows.len(),17);assert_eq!(changed,1);
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/EyeRim320SessionComparison.json",serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"cases":rows,"scope":"exact UV/topology/shader/overtex preservation and source-derived face composite; geometry deltas separately reported"})).unwrap()).unwrap();
}

#[test]
#[ignore="installed legacy overlay source inventory"]
fn legacy_overlay_source_inventory() {
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Logs/DumpFourViewsInventory.json").unwrap()).unwrap();
    let mut rows=Vec::new();
    for item in inventory.as_array().unwrap() {
        let name=item["card"].as_str().unwrap();let card=crate::card::parse_card(&&crate::test_support::game_path("/UserData/chara/female").join(name)).unwrap();
        rows.push(serde_json::json!({"card":name,"overlays":card.ksox.iter().map(|(k,p)|(k,p.len())).collect::<Vec<_>>() }));
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/EyeRim320OverlayInventory.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

use super::*;

#[test]
#[ignore="installed native face texture input diagnostic"]
fn native_face_texture_inputs() {
    let game=&crate::test_support::game_path("");let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_381623.png")).unwrap();
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE '%/KK_381623.png|%' AND card_key LIKE '%|p195|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
    let (key,bytes):(String,Vec<u8>)=st.query_row([],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();let s:SessionBlob=bincode::deserialize(&bytes).unwrap();
    let directory=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/EyeRim320Inputs");std::fs::create_dir_all(directory).unwrap();let mut rows=Vec::new();
    for p in s.parts.iter().filter(|p|p.name=="cf_O_face"||p.name=="cf_Ohitomi_L"||p.name=="cf_Ohitomi_R") {
        std::fs::write(directory.join(format!("{}.png",p.name)),p.png.as_ref().unwrap()).unwrap();
        rows.push(serde_json::json!({"name":p.name,"uvs":p.uvs,"positions":p.positions,"material":p.source_material,"main_tex_st":p.main_tex_st}));
    }
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));let guids=crate::mods::find_guids(&game.join("mods"),None);
    let png=crate::compose_face_texture(game,&vanilla,&mods,&card,&[],&[],card.skin_color,card.skin_sub_color,0,&guids).unwrap();
    std::fs::write(directory.join("resolved_face.png"),png).unwrap();
    for ((coordinate,kind),png) in &card.ksox {std::fs::write(directory.join(format!("ksox_{coordinate}_{kind}.png")),png).unwrap();}
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&serde_json::json!({"paint_ids":card.paint_ids,"paint_colors":card.paint_colors,"paint_layouts":card.paint_layouts,"key":key,"parts":rows,"ksox":card.ksox.iter().map(|(k,v)|(k,v.len())).collect::<Vec<_>>(),"me":card.me_tex.iter().map(|(k,v)|(k,v.len())).collect::<Vec<_>>()})).unwrap()).unwrap();
}

#[test]
#[ignore="read-only installed eyelid draw texture provenance diagnostic"]
fn native_face_line_source_provenance() {
    let game=&crate::test_support::game_path("");let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));let guids=crate::mods::find_guids(&game.join("mods"),None);
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let directory=std::env::var("OK_FACE_LINE_INPUT_DIR").unwrap_or_else(|_|"C:/portable/OpenKoikatsu/Saved/Tests/Eyeline320Inputs".into());let directory=Path::new(&directory);std::fs::create_dir_all(directory).unwrap();let mut rows=Vec::new();
    let names=std::env::var("OK_FACE_LINE_CARD_NAMES").unwrap_or_else(|_|"KK_381623.png;弹丸论破 - 江之岛盾子2.png;Hayase_yuuka_keiyuu_V1_02 For KKS.png;KK_388205.png;KK_384283.png".into());
    for (i,name) in names.split(';').enumerate() {
        let card=crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE '%|p192|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
        let (key,bytes):(String,Vec<u8>)=st.query_row([format!("%/{name}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();let s:SessionBlob=bincode::deserialize(&bytes).unwrap();
        for p in s.parts.iter().filter(|p|p.name.to_ascii_lowercase().contains("eyeline")) {
            let original=directory.join(format!("Card{i}.{}.prior.png",p.name));std::fs::write(&original,p.png.as_ref().unwrap()).unwrap();
            let mut mesh=crate::unity::ExtractedMesh {name:p.name.clone(),mat:p.source_material.clone(),..Default::default()};
            if !p.name.ends_with("_kage") {crate::resolve_head_detail_texture(game,&vanilla,&mods,&card,&guids,0,&mut mesh,&[],&[]);} else {continue;}
            let resolved=directory.join(format!("Card{i}.{}.resolved.png",p.name));std::fs::write(&resolved,mesh.texture_png.as_ref().unwrap()).unwrap();
            rows.push(serde_json::json!({"card":name,"card_index":i,"key":key,"name":p.name,"uvs":p.uvs,"prior_png":original,"resolved_png":resolved,"main_tex_st":p.main_tex_st}));
        }
    }
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="installed catalog brow source provenance diagnostic"]
fn native_brow_source_provenance() {
    let game=&crate::test_support::game_path("");let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));let guids=crate::mods::find_guids(&game.join("mods"),None);
    let directory=std::env::var("OK_BROW_PROVENANCE_DIR").unwrap_or_else(|_|"C:/portable/OpenKoikatsu/Saved/Tests/Brow319Provenance".into());let directory=Path::new(&directory);std::fs::create_dir_all(directory).unwrap();
    let resolved=std::env::var("OK_BROW_RESOLVED_DIR").unwrap_or_else(|_|"C:/portable/OpenKoikatsu/Saved/Tests/Brow319ResolvedInputs".into());let resolved=Path::new(&resolved);std::fs::create_dir_all(resolved).unwrap();
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let mut rows=Vec::new();
    let mut resolved_rows=Vec::new();
    let names=std::env::var("OK_BROW_CARD_NAMES").unwrap_or_else(|_|"弹丸论破 - 江之岛盾子2.png;Hayase_yuuka_keiyuu_V1_02 For KKS.png;KK_381623.png;KK_388205.png".into());
    for (i,name) in names.split(';').enumerate() {
        let card=crate::card::parse_card(&game.join("UserData/chara/female").join(name)).unwrap();
        let entry=crate::face_detail_entry(&vanilla,&mods,&card,&guids,406,card.eyebrow_id).unwrap();
        let png=crate::png_from_list_entry(game,&entry,&[&entry.info("EyebrowTex")],&[],&[]).unwrap();
        std::fs::write(directory.join(format!("Card{i}.png")),png).unwrap();
        let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE '%|p192|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
        let (key,bytes):(String,Vec<u8>)=st.query_row([format!("%/{name}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();let s:SessionBlob=bincode::deserialize(&bytes).unwrap();
        let p=s.parts.iter().find(|p|p.name.to_ascii_lowercase().contains("mayuge")).unwrap();
        let mut mesh=crate::unity::ExtractedMesh {name:p.name.clone(),mat:p.source_material.clone(),..Default::default()};
        crate::resolve_head_detail_texture(game,&vanilla,&mods,&card,&guids,0,&mut mesh,&[],&[]);
        std::fs::write(resolved.join(format!("Card{i}.png")),mesh.texture_png.as_ref().unwrap()).unwrap();
        resolved_rows.push(serde_json::json!({"card":name,"key":key,"resolver_pipeline":crate::sqlcache::SESSION_PIPELINE_VER,"name":p.name,"uvs":p.uvs}));
        rows.push(serde_json::json!({"card":name,"entry":format!("{entry:?}"),"sideloader_png":crate::sideloader_png(entry.extra.get("__zip").map(String::as_str),game,&entry.main_ab,&[&entry.info("EyebrowTex")]).is_some(),"me_textures":card.me_tex.iter().filter(|(k,_)|k.0==4&&k.3.contains("mayu")).map(|(k,v)|(k,v.len())).collect::<Vec<_>>(),"me_shaders":card.me_shader.iter().filter(|(k,_)|k.0==4&&k.3.contains("mayu")).collect::<Vec<_>>() }));
    }
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    std::fs::write(resolved.join("inputs.json"),serde_json::to_vec_pretty(&resolved_rows).unwrap()).unwrap();
}

#[test]
#[ignore="read-only cached brow inputs for independent native sampling"]
fn cached_brow_sampling_inputs() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let pipeline=std::env::var("OK_BROW_PIPELINE").unwrap_or_else(|_|"192".into());
    let directory=std::env::var("OK_BROW_INPUT_DIR").unwrap_or_else(|_|"C:/portable/OpenKoikatsu/Saved/Tests/Brow319CachedInputs".into());
    let directory=Path::new(&directory);std::fs::create_dir_all(directory).unwrap();
    let mut rows=Vec::new();
    let names=std::env::var("OK_BROW_CARD_NAMES").unwrap_or_else(|_|"弹丸论破 - 江之岛盾子2.png;Hayase_yuuka_keiyuu_V1_02 For KKS.png;KK_381623.png;KK_388205.png".into());
    for (i,card) in names.split(';').enumerate() {
        let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE ?2 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
        let (key,bytes):(String,Vec<u8>)=st.query_row([format!("%/{card}|%"),format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
        let s:SessionBlob=bincode::deserialize(&bytes).unwrap();
        let p=s.parts.iter().find(|p|p.name.to_ascii_lowercase().contains("mayuge")).unwrap();
        std::fs::write(directory.join(format!("Card{i}.png")),p.png.as_ref().unwrap()).unwrap();
        rows.push(serde_json::json!({"card":card,"key":key,"name":p.name,"positions":p.positions,"uvs":p.uvs,"indices":p.indices,"main_tex_st":p.main_tex_st,"shader_kind":p.shader_kind}));
    }
    std::fs::write(directory.join("inputs.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="read-only complete p192/p195 socket and brow regression cache comparison"]
fn compare_socket_brow_session_caches() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let inventory:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Logs/DumpFourViewsInventory.json").unwrap()).unwrap();
    let mut rows=Vec::new();
    let signature=|key:&str|key.split('|').filter(|s|!matches!(*s,"p192"|"p195")
        && !["colliders=","uncensor=","materialeditor="].iter().any(|prefix|s.starts_with(prefix))).collect::<Vec<_>>().join("|");
    let mut old_st=db.prepare("SELECT card_key FROM session_blob WHERE card_key LIKE '%|p192|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%'").unwrap();
    let old_keys:Vec<String>=old_st.query_map([],|r|r.get(0)).unwrap().map(Result::unwrap).collect();
    for item in inventory.as_array().unwrap() {
        let card=item["card"].as_str().unwrap();
        let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE '%|p195|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
        let (key,bytes):(String,Vec<u8>)=st.query_row([format!("%/{card}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
        let old_key=old_keys.iter().filter(|old|signature(old)==signature(&key)).max().unwrap();
        let old_bytes:Vec<u8>=db.query_row("SELECT payload FROM session_blob WHERE card_key=?1",[old_key],|r|r.get(0)).unwrap();
        let old:SessionBlob=bincode::deserialize(&old_bytes).unwrap();let new:SessionBlob=bincode::deserialize(&bytes).unwrap();
        assert_eq!(old.parts.len(),new.parts.len());let mut changes=Vec::new();let mut chains=Vec::new();
        for (a,b) in old.parts.iter().zip(&new.parts) {
            assert_eq!((&a.role,&a.name),(&b.role,&b.name));assert_eq!(a.uvs,b.uvs);assert_eq!(a.indices,b.indices);assert_eq!(a.shader_textures,b.shader_textures);
            let reversed=if a.png!=b.png {
                assert!(b.name.to_ascii_lowercase().contains("mayuge"),"unexpected changed texture {}",b.name);
                let mut before=crate::teximg::load_from_memory(a.png.as_ref().unwrap()).unwrap().to_rgba8();image::imageops::flip_vertical_in_place(&mut before);
                assert_eq!(before,crate::teximg::load_from_memory(b.png.as_ref().unwrap()).unwrap().to_rgba8(),"direct brow pixels must only reverse rows");true
            } else {false};
            let error=|a:&[[f32;3]],b:&[[f32;3]]| {assert_eq!(a.len(),b.len());a.iter().zip(b).map(|(a,b)|a.iter().zip(b).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt()).fold(0f32,f32::max)};
            let position=error(&a.positions,&b.positions);let normal=error(&a.normals,&b.normals);
            if position>1e-6||normal>1e-6||reversed {changes.push(serde_json::json!({"role":b.role,"name":b.name,"position_max_m":position,"normal_max":normal,"brow_rows_reversed":reversed}));}
            if let Some(r)=&b.runtime_accessory {if !r.copy_weights_body&&r.source.deferred_dbones.as_ref().is_some_and(|d|!d.is_empty()) {
                let chain=crate::attachment_transform_chain(new.body_skel.as_ref(),new.head_skel.as_ref(),&r.parent_key,&r.local_placement);
                chains.push(serde_json::json!({"name":b.name,"parent":r.parent_key,"ancestor_count":chain.as_ref().map(Vec::len),"legacy_matrix_fallback":chain.is_none()}));
            }}
        }
        let signature_differences:Vec<_>=old_key.split('|').zip(key.split('|')).filter(|(a,b)|a!=b).collect();
        rows.push(serde_json::json!({"card":card,"key":key,"prior_key":old_key,"signature_differences":signature_differences,"parts":new.parts.len(),"changes":changes,"dynamic_attachments":chains}));
    }
    assert_eq!(rows.len(),17);
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Brow319SessionComparison.json",serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"cases":rows,"scope":"actual cached buffers without physics rehydration; all UV/topology/shader textures identical; only changed PNGs are exact brow row reversals; geometry/normal deltas and config mtime differences reported"})).unwrap()).unwrap();
}

#[test]
#[ignore="read-only installed cached accessory physics input diagnostic"]
fn cached_accessory_physics_inputs() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let mut rows=Vec::new();
    for (card,meshes) in [("弹丸论破 - 江之岛盾子2.png",vec!["o_tie_01","o_necktie_yure_long"]),
        ("Hayase_yuuka_keiyuu_V1_02 For KKS.png",vec!["cf_hair_f_22_00_InRight"])] {
        let mut st=db.prepare("SELECT payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE '%|p192|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
        let bytes:Vec<u8>=st.query_row([format!("%/{card}|%")],|r|r.get(0)).unwrap();let s:SessionBlob=bincode::deserialize(&bytes).unwrap();
        for name in meshes {
            let p=s.parts.iter().find(|p|p.name==name).unwrap();let r=p.runtime_accessory.as_ref().unwrap();
            let parent=crate::bone_world(s.body_skel.as_ref(),s.head_skel.as_ref(),&r.parent_key).unwrap();
            rows.push(serde_json::json!({"card":card,"mesh":name,"source_skeleton":r.source.smr_skel,"dynamics":r.source.deferred_dbones,"placement":crate::pose::mat_mul_pub(&parent,&r.local_placement),"attachment_chain":crate::attachment_transform_chain(s.body_skel.as_ref(),s.head_skel.as_ref(),&r.parent_key,&r.local_placement),"parent_key":r.parent_key}));
        }
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Accessory318CachedInputs.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="independent partial-hair native palette diagnostic"]
fn native_partial_hair_move_control() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/Tests/Neck317YuukaNative/Card0.json.mesh29.json").unwrap()).unwrap();
    let mut rows=Vec::new();
    for pipeline in [190,191,192] {
        let mut st=db.prepare("SELECT card_key,payload FROM session_blob WHERE card_key LIKE '%/Hayase_yuuka_keiyuu_V1_02 For KKS.png|%' AND card_key LIKE ?1 AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1").unwrap();
        let (key,bytes):(String,Vec<u8>)=st.query_row([format!("%|p{pipeline}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
        let s:SessionBlob=bincode::deserialize(&bytes).unwrap();
        let p=s.parts.iter().find(|p|p.name=="cf_hair_f_22_00_InRight").unwrap();let r=p.runtime_accessory.as_ref().unwrap();
        let parent=crate::bone_world(s.body_skel.as_ref(),s.head_skel.as_ref(),&r.parent_key).unwrap();
        let placement=crate::pose::mat_mul_pub(&parent,&r.local_placement);
        let skel=r.source.smr_skel.as_ref().unwrap();let static_positions=r.skin_palette(skel,&placement,None).unwrap().0;
        let error=|vertices:&[[f32;3]]| vertices.iter().zip(native["shaderWorldVertices"].as_array().unwrap()).map(|(a,b)| {
            (0..3).map(|c|(a[c]-b[["x","y","z"][c]].as_f64().unwrap() as f32).powi(2)).sum::<f32>().sqrt()
        }).fold(0f32,f32::max);
        let mut bones=Vec::new();
        for (i,world) in skel.worlds.iter().enumerate() {
            let placed=crate::pose::mat_mul_pub(&placement,world);let mut max=0f32;
            for row in 0..4 {for col in 0..4 {max=max.max((placed[row][col]-native["boneWorlds"][i][format!("m{row}{col}")].as_f64().unwrap() as f32).abs());}}
            bones.push(serde_json::json!({"name":skel.names[i],"world":placed,"native_matrix_max":max}));
        }
        if pipeline==192 {assert!(error(&static_positions)<2e-5,"split move alias must match all native static vertices");}
        rows.push(serde_json::json!({"pipeline":pipeline,"key":key,"static_max_m":error(&static_positions),"cached_max_m":error(&p.positions),
            "bones":bones,"tfs":skel.tfs,"moves":r.source.move_nodes,"dynamic":r.source.deferred_dbones}));
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Neck317PartialHairControlR2.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="read-only comparison of existing p190/p192 GPU session regression caches"]
fn compare_accessory_hierarchy_session_caches() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),
        rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let mut statement=db.prepare("SELECT card_key FROM session_blob WHERE card_key LIKE '%|p192|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%'").unwrap();
    let keys:Vec<String>=statement.query_map([],|row|row.get(0)).unwrap().map(Result::unwrap).collect();
    let mut prior_statement=db.prepare("SELECT card_key FROM session_blob WHERE card_key LIKE '%|p190|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%'").unwrap();
    let prior_keys:Vec<String>=prior_statement.query_map([],|row|row.get(0)).unwrap().map(Result::unwrap).collect();
    // Native Studio rewrites these config mtimes on startup. Keep them in the
    // report; compare the actual cached data without claiming identical config.
    let signature=|key:&str|key.split('|').filter(|s| !matches!(*s,"p190"|"p192")
        && !["colliders=","uncensor=","materialeditor="].iter().any(|prefix|s.starts_with(prefix)))
        .collect::<Vec<_>>().join("|");
    let mut rows=Vec::new();
    for key in keys {
        let Some(old_key)=prior_keys.iter().filter(|old|signature(old)==signature(&key)).max() else {continue;};
        let bytes=|key:&str|db.query_row("SELECT payload FROM session_blob WHERE card_key=?1",[key],|row|row.get::<_,Vec<u8>>(0));
        let Ok(old)=bytes(old_key) else {continue;};
        let old:SessionBlob=bincode::deserialize(&old).unwrap();
        let new:SessionBlob=bincode::deserialize(&bytes(&key).unwrap()).unwrap();
        assert_eq!(old.parts.len(),new.parts.len());
        let mut parts=Vec::new();
        for (a,b) in old.parts.iter().zip(&new.parts) {
            assert_eq!((&a.role,&a.name),(&b.role,&b.name));
            assert_eq!(a.uvs,b.uvs);assert_eq!(a.indices,b.indices);
            assert_eq!(a.png,b.png);assert_eq!(a.shader_textures,b.shader_textures);
            let error=|a:&[[f32;3]],b:&[[f32;3]]| {assert_eq!(a.len(),b.len());
                a.iter().zip(b).map(|(a,b)|a.iter().zip(b).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt()).fold(0f32,f32::max)};
            let position=error(&a.positions,&b.positions);let normal=error(&a.normals,&b.normals);
            if position>1e-6 || normal>1e-6 {parts.push(serde_json::json!({"role":b.role,"name":b.name,
                "position_max_m":position,"normal_max":normal,"private_palette":b.runtime_accessory.is_some()}));}
        }
        let signature_differences:Vec<_>=old_key.split('|').zip(key.split('|')).filter(|(a,b)|a!=b).collect();
        rows.push(serde_json::json!({"key":key,"prior_key":old_key,"signature_differences":signature_differences,
            "parts":new.parts.len(),"changes":parts}));
    }
    assert!(!rows.is_empty());
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Neck317SessionComparisonR2.json",
        serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"cases":rows,
            "scope":"cached geometry/normal deltas; identical UVs/indices/PNG/shader texture inputs; not live physics or final pixel parity"})).unwrap()).unwrap();
}

#[test]
#[ignore="installed card and independent native accessory hierarchy fixture"]
fn native_accessory_move_hierarchy() {
    let game=&crate::test_support::game_path("");
    let session=open(game,Some(&game.join("UserData/chara/female/弹丸论破 - 江之岛盾子2.png")),0).unwrap();
    let fixtures=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Neck317NativeR2");
    let mut rows=Vec::new();
    for filename in ["Card0.json.mesh35.json","Card0.json.mesh36.json"] {
        let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(fixtures.join(filename)).unwrap()).unwrap();
        let name=native["name"].as_str().unwrap();
        let part=session.parts.iter().find(|p|p.name==name).unwrap();
        let runtime=part.runtime_accessory.as_ref().unwrap();
        let expected=native["shaderWorldVertices"].as_array().unwrap();
        assert_eq!(part.positions.len(),expected.len());
        let mut fixed_count=0;let mut fixed_max=0f32;let mut full_max=0f32;
        for (i,(a,b)) in part.positions.iter().zip(expected).enumerate() {
            let e:[f32;3]=std::array::from_fn(|c|b[["x","y","z"][c]].as_f64().unwrap() as f32);
            let error=a.iter().zip(e).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt();full_max=full_max.max(error);
            let source=&runtime.source;
            if (0..4).filter(|c|source.weights[i][*c]>1e-6).all(|c|source.bone_idx[i][c]==0) {
                fixed_count+=1;fixed_max=fixed_max.max(error);
            }
        }
        assert!(fixed_count>=10,"independent root-only vertices required");
        assert!(fixed_max<2e-5,"{name} fixed branch max={fixed_max}m");
        let mut palette=runtime.source.smr_skel.as_ref().unwrap().clone();
        for (i,world) in palette.worlds.iter_mut().enumerate() {for r in 0..4 {for c in 0..4 {
            world[r][c]=native["boneWorlds"][i][format!("m{r}{c}")].as_f64().unwrap() as f32;
            assert!((runtime.source.bind_poses[i][r][c]-native["bindposes"][i][format!("m{r}{c}")].as_f64().unwrap() as f32).abs()<2e-6);
        }}}
        let vertices=runtime.skin_palette(&palette,&crate::pose::ident(),None).unwrap().0;
        let mut oracle_max=0f32;
        for (a,b) in vertices.iter().zip(expected) {
            let e:[f32;3]=std::array::from_fn(|c|b[["x","y","z"][c]].as_f64().unwrap() as f32);
            oracle_max=oracle_max.max(a.iter().zip(e).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt());
        }
        for (a,b) in runtime.source.rest_positions.iter().zip(native["vertices"].as_array().unwrap()) {
            for c in 0..3 {assert!((a[c]-b[["x","y","z"][c]].as_f64().unwrap() as f32).abs()<2e-6);}
        }
        assert!(oracle_max<2e-5,"{name} native palette LBS max={oracle_max}");
        rows.push(serde_json::json!({"mesh":name,"vertices":vertices.len(),"fixed_vertices":fixed_count,
            "fixed_max_m":fixed_max,"native_palette_lbs_max_m":oracle_max,"settled_physics_max_m":full_max}));
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Accessory318NativeHierarchyAudit.json",
        serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"rows":rows,
            "scope":"fixed skin branches, raw geometry/binds, native-palette LBS; settled physics difference recorded without parity claim"})).unwrap()).unwrap();
}

#[test]
#[ignore="installed game RT fixtures for residual clothes colour diagnosis"]
fn diagnose_native_clothes_maintex_inputs() {
    let game=&crate::test_support::game_path("");
    let cases=[
        ("KK_226176.png","Inugami Korone_0","mesh_37_o_top_pajama_a.materials.json","o_top_pajama_a"),
        ("KK_384914.png","九十九 過子_0","mesh_44_o_top_sailor01_01_a.materials.json","o_top_sailor01_01_a"),
    ];
    let mut rows=Vec::new();
    for (card,sub,material_name,mesh_name) in cases {
        let session=open(game,Some(&game.join("UserData/chara/female").join(card)),0).unwrap();
        let part=session.parts.iter().find(|p|p.render_visible &&
            (p.name==mesh_name || p.name.strip_prefix(mesh_name).is_some_and(|suffix|suffix.starts_with('_'))))
            .expect("visible source clothes part");
        let native_dir=game.join("mesh_dump").join(card).join(sub);
        let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(native_dir.join(material_name)).unwrap()).unwrap();
        let texture=dump["materials"][0]["properties"].as_array().unwrap().iter()
            .find(|v|v["name"]=="_MainTex").unwrap();
        let native=native_dir.join(texture["texture"]["exportedPng"].as_str().unwrap());
        let got=crate::teximg::load_from_memory(part.png.as_ref().expect("session main texture")).unwrap().to_rgba8();
        let expected=image::open(&native).unwrap().to_rgba8();
        assert_eq!(got.dimensions(),expected.dimensions());
        let mut sum=[0u64;4];let mut maximum=[0u8;4];let mut flip_sum=[0u64;4];
        for y in 0..got.height() {for x in 0..got.width() {
            let a=got.get_pixel(x,y);let b=expected.get_pixel(x,y);let f=expected.get_pixel(x,got.height()-1-y);
            for c in 0..4 {let d=a[c].abs_diff(b[c]);sum[c]+=d as u64;maximum[c]=maximum[c].max(d);flip_sum[c]+=a[c].abs_diff(f[c]) as u64;}
        }}
        let n=(got.width() as u64*got.height() as u64) as f64;
        let row=serde_json::json!({"card":card,"mesh":mesh_name,"shader":dump["materials"][0]["shader"],
            "native_maintex":native,"mean_rgba":sum.map(|v|v as f64/n),"max_rgba":maximum,
            "flip_y_mean_rgba":flip_sum.map(|v|v as f64/n)});
        eprintln!("native clothes input {row}");rows.push(row);
    }
    let output=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MainItem310ClothesInputs172.json");
    std::fs::write(output,serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="installed MainItemPlus card and independent game material dump"]
fn native_item_material_matches_game_dump() {
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");
    let game=&crate::test_support::game_path("");
    let native=game.join("mesh_dump/KK_398302.png/Minase アキコ_0/mesh_124_o_acs_widebrimmedhat_0.materials.json");
    let dump:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&native).unwrap()).unwrap();
    let session=open(game,Some(&game.join("UserData/chara/female/KK_398302.png")),0).unwrap();
    let parts:Vec<_>=session.parts.iter().filter(|p|p.name==dump["mesh"].as_str().unwrap()).collect();
    assert_eq!(parts.len(),1);let p=parts[0];assert_eq!(p.shader_kind,9);assert_eq!(p.blend,"masked");
    let mat=p.source_material.as_ref().unwrap();assert_eq!(mat.shader,dump["materials"][0]["shader"].as_str().unwrap());
    let declaration:serde_json::Value=serde_json::from_slice(&crate::gamefs::read("C:/portable/OpenKoikatsu/Saved/ShaderDxbcVanillaPlus310/MainItemPlus/parsed.json").unwrap()).unwrap();
    let all=dump["materials"][0]["properties"].as_array().unwrap();
    let mut rows=Vec::new();let mut maximum=0f64;
    for expected in all.iter().filter(|v|v.get("value").is_some()) {
        let name=expected["name"].as_str().unwrap();
        let defaults=declaration["m_PropInfo"]["m_Props"].as_array().unwrap().iter().find(|v|v["m_Name"]==name).unwrap();
        let fallback=std::array::from_fn::<_,4,_>(|i|defaults[format!("m_DefValue[{i}]")].as_f64().unwrap() as f32);
        let e=&expected["value"];
        let (actual,expected)=if e.is_object() {
            let keys=if e.get("r").is_some(){["r","g","b","a"]}else{["x","y","z","w"]};
            (mat.color(name).unwrap_or(fallback).to_vec(),keys.map(|k|e[k].as_f64().unwrap()).to_vec())
        } else {(vec![mat.float(name).unwrap_or(fallback[0])],vec![e.as_f64().unwrap()])};
        let error=actual.iter().zip(&expected).map(|(a,b)|(*a as f64-b).abs()).fold(0f64,f64::max);
        assert!(error<2e-6,"{name}: actual={actual:?} expected={expected:?} error={error}");maximum=maximum.max(error);
        rows.push(serde_json::json!({"property":name,"actual":actual,"expected":expected,"maximum":error,
            "source":if mat.color(name).is_some() || mat.float(name).is_some(){"material"}else{"installed shader declaration default"}}));
    }
    let main=crate::teximg::load_from_memory(p.shader_textures.get("_MainTex").unwrap()).unwrap().to_rgba8();
    assert!(main.pixels().all(|v|v.0==[255;4]),"native null MainTex means shader white, not CPU tint");
    assert!(p.shader_textures.contains_key("_ColorMask"));assert!(p.shader_textures.contains_key("_DetailMask"));
    let normal=dump["materials"][0]["properties"].as_array().unwrap().iter().find(|v|v["name"]=="_NormalMap").unwrap();
    assert_eq!(normal["texture"]["graphicsFormat"],"R8G8B8A8_SRGB");
    assert_eq!(p.runtime_textures.get("_NormalMap"),Some(&[1,0,0,1,1]));
    let native_dir=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MainItem310HatTexturesFull171");
    let mut native_texture_max=std::collections::BTreeMap::new();
    for property in ["_ColorMask","_DetailMask","_NormalMap"] {
        let native_rt=native_dir.join(format!("Card0.json.renderer124.mat0.{property}.png"));
        if !native_rt.ok_exists() {continue;}
        let got=crate::teximg::load_from_memory(p.shader_textures.get(property).unwrap()).unwrap().to_rgba8();
        let expected=image::open(&native_rt).unwrap().to_rgba8();
        assert_eq!(got.dimensions(),expected.dimensions());
        let mut maximum=0u8;let mut over_six=0usize;
        for (a,b) in got.pixels().zip(expected.pixels()) {
            for c in 0..4 {let error=a[c].abs_diff(b[c]);maximum=maximum.max(error);if error>6 {over_six+=1;}}
        }
        assert_eq!(over_six,0,"{property} native texels differ: max={maximum}");
        native_texture_max.insert(property,maximum);
    }
    assert_eq!(native_texture_max.len(),3,"compare all present native item texture inputs");
    let runtime=p.runtime_accessory.as_ref().expect("skinned accessory without DynamicBone still needs its own palette");
    assert_eq!(p.skel_kind,4);
    assert_eq!(p.rest_tangents.len(),p.positions.len());
    assert_eq!(runtime.source.tangents.len(),p.positions.len());
    let native_mesh_path=game.join("mesh_dump/KK_398302.png/Minase アキコ_0/mesh_124_o_acs_widebrimmedhat_0.json");
    let native_mesh:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&native_mesh_path).unwrap()).unwrap();
    let authored=native_mesh["mesh"]["tangents"].as_array().unwrap();
    assert_eq!(authored.len(),runtime.source.tangents.len());
    let mut raw_tangent_error=0f64;let mut transformed=0usize;
    for ((source,current),native) in runtime.source.tangents.iter().zip(&p.rest_tangents).zip(authored) {
        for (axis,key) in ["x","y","z","w"].iter().enumerate() {
            raw_tangent_error=raw_tangent_error.max((source[axis] as f64-native[key].as_f64().unwrap()).abs());
        }
        assert_eq!(source[3],current[3],"skinning must preserve tangent handedness");
        assert!(current.iter().all(|v|v.is_finite()));
        let len=(current[0]*current[0]+current[1]*current[1]+current[2]*current[2]).sqrt();
        assert!((len-1.).abs()<1e-3,"skinned tangent direction must be unit length");
        if (0..3).any(|axis|(source[axis]-current[axis]).abs()>1e-4) {transformed+=1;}
    }
    assert!(raw_tangent_error<2e-6,"source tangent must agree with independent game mesh dump");
    assert!(transformed>0,"native skinned renderer must expose posed rather than raw tangents");
    // The dump labels worldVertices as BakeMesh + renderer.TransformPoint and
    // bakeMeshSpaceGuess=unknown. That applies renderer scale twice for this
    // SMR; the independently recorded manualSkinnedWorldBounds is the valid
    // palette-space control for the displayed mesh.
    assert_eq!(native_mesh["bakeMeshSpaceGuess"],"unknown");
    let mut native_bounds_error=0f64;
    for (axis,key) in ["x","y","z"].iter().enumerate() {
        let min=p.positions.iter().map(|v|v[axis]).fold(f32::INFINITY,f32::min);
        let max=p.positions.iter().map(|v|v[axis]).fold(f32::NEG_INFINITY,f32::max);
        for (actual,kind) in [(min,"min"),(max,"max")] {
            native_bounds_error=native_bounds_error.max((actual as f64-native_mesh["manualSkinnedWorldBounds"][kind][key].as_f64().unwrap()).abs());
        }
    }
    let textures=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MainItemPlus310/MaterialTextures167");
    std::fs::create_dir_all(textures).unwrap();
    for (name,png) in &p.shader_textures {std::fs::write(textures.join(format!("{name}.png")),png).unwrap();}
    std::fs::write(textures.join("geometry.json"),serde_json::to_vec(&serde_json::json!({
        "positions":p.positions,"normals":p.normals,"rest_normals":p.rest_normals,"rest_tangents":p.rest_tangents,
        "runtime_textures":p.runtime_textures,"bind_poses":p.bind_poses,
        "native_raw_tangent_max":raw_tangent_error,"posed_tangents_changed":transformed,
        "native_manual_bounds_max_m":native_bounds_error,"skel_kind":p.skel_kind,
        "native_texture_max":native_texture_max
    })).unwrap()).unwrap();
    assert!(native_bounds_error<0.002,"independent game manual skin bounds max={native_bounds_error} m");
    let output=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/MainItemPlus310/NativeMaterial167.json");
    std::fs::create_dir_all(output.parent().unwrap()).unwrap();
    std::fs::write(output,serde_json::to_vec_pretty(&serde_json::json!({"passed":true,"native":native,"properties":rows,"maximum":maximum,"shader_kind":p.shader_kind,
        "scope":"Independent game material values and raw mask preservation, not visual parity"})).unwrap()).unwrap();
    assert!(rows.len()>=50,"compare the complete installed numeric property table");
    eprintln!("Native MainItemPlus material properties={} max={maximum} shader_kind=9 raw_white_main=true",rows.len());
}

#[test]
#[ignore="installed Chiaki card and independent game rigid renderer world vertices"]
fn shared_mesh_rigid_accessory_matches_native_world_vertices() {
    let game = &crate::test_support::game_path("");
    let native = game.join("mesh_dump/弹丸论破 - 七海千秋3.png/七海 千秋2.0_0/mesh_54_Mask.json");
    let dump: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(&native).unwrap()).unwrap();
    let mut session = open(game, Some(&game.join("UserData/chara/female/弹丸论破 - 七海千秋3.png")), 0).unwrap();
    session.anim_clip.clear(); session.apply_anim_frame(0.,0.);
    let parts: Vec<_> = session.parts.iter().filter(|part| part.role == "accessory" && part.name == "wed_mik_MZ").collect();
    assert_eq!(parts.len(),1);
    let part = parts[0];
    let vertices = dump["mesh"]["worldVertices"].as_array().unwrap();
    assert_eq!(part.positions.len(),vertices.len());
    assert_eq!(part.rigid_follow_bone.as_deref(),Some("a_n_headside"));
    let mut maximum = 0f64; let mut total = 0f64;
    for (actual, expected) in part.positions.iter().zip(vertices) {
        for (axis,key) in ["x","y","z"].iter().enumerate() {
            let difference = (actual[axis] as f64 - expected[key].as_f64().unwrap()).abs();
            assert!(difference.is_finite());
            maximum = maximum.max(difference); total += difference;
        }
    }
    let report = serde_json::json!({"vertices":vertices.len(),"maximum_m":maximum,
        "mean_component_m":total/(vertices.len()*3) as f64,"native":native,
        "scope":"Rigid renderer world positions from independent game dump; not shader acceptance"});
    let output = Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NewCards309SharedMesh166.json");
    std::fs::write(output,serde_json::to_vec_pretty(&report).unwrap()).unwrap();
    eprintln!("Native shared mesh renderer {report}");
    assert!(maximum < 0.00001,"selected prefab renderer differs from native world positions: {report}");
}

#[test]
#[ignore="installed boot create texture against independent native draw albedo"]
fn native_mod_boot_clothes_albedo() {
    let game=&crate::test_support::game_path("");
    let output=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/SkinHsv307");
    let card=crate::card::parse_card(&game.join("UserData/chara/female/弹丸论破 - 江之岛盾子2.png")).unwrap();
    let zip=game.join("mods/KKSImportedMods/付费作者系列含mod/SamCosmyson/mod/[[OMEGA]] HS2 port to KK v1.8.1.zipmod");
    let catalog=crate::list::catalog_from_zipmod(&zip);
    let entry=catalog.get(112,15222).unwrap();
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let patterns=crate::build_clothes_patterns(game,&vanilla,&catalog,&card,&crate::mods::find_guids(&game.join("mods"),None),8,0,&[],&[],0);
    let (baked,used_mask)=crate::bake_clothes_create_textures(game,entry,card.clothes_colors[8],&patterns,&[],&[]);
    assert!(used_mask);
    let png=crate::flip_runtime_blit_v(baked[0].clone().unwrap());
    std::fs::write(output.join("BootAlbedoAfter.png"),&png).unwrap();
    let a=crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
    let b=image::open("C:/portable/OpenKoikatsu/Saved/Tests/SkinHsv307BootNative/Card0.json.renderer58.mat0._MainTex.png").unwrap().to_rgba8();
    assert_eq!(a.dimensions(),b.dimensions());
    let maximum=a.as_raw().iter().zip(b.as_raw()).map(|(x,y)|x.abs_diff(*y)).max().unwrap();
    let mean=a.as_raw().iter().zip(b.as_raw()).map(|(x,y)|x.abs_diff(*y) as f64).sum::<f64>()/a.as_raw().len() as f64;
    let report=serde_json::json!({"maximum":maximum,"mean":mean,"pixels":a.width()*a.height(),"scope":"Native game draw MainTex vs complete CPU clothes create bake; not final fragment"});
    std::fs::write(output.join("BootAlbedoAfter.json"),serde_json::to_vec_pretty(&report).unwrap()).unwrap();
    eprintln!("Native boot baked albedo {report}");
    assert!(maximum<=3,"native clothes create albedo must agree");
}

#[test]
#[ignore="installed partial Sideloader override with native white accessory inputs"]
fn native_partial_override_accessory_colors() {
    let game=&crate::test_support::game_path("");
    let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_346095.png")).unwrap();
    let catalog=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let native=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NewCards306CollarNative");
    for (slot,row) in [(24usize,46),(25,0),(26,1)] {
        let acc=&card.accessory[slot];let entry=catalog.get(acc.type_id,acc.id).unwrap();
        let property=format!("outfit0.accessory{slot}");
        let (patch_zip,patch)=crate::mods::override_bundle_with_source(&game.join("mods"),&entry.main_ab).unwrap();
        let patch_bundle=crate::unity::parse_bundle_ref(&patch);
        assert!(!patch_bundle.contains_prefab(&entry.main_data),"partial patch does not own this accessory");
        assert!(crate::unity::accessory_color_use(&patch,&entry.main_data).is_none(),"another prefab's component is not a fallback");
        let mut hair_entry=entry.clone();hair_entry.main_data="p_acs_ahoge01".into();
        let (_,hair_source)=crate::accessory_bundle_source(game,None,&hair_entry).unwrap();
        assert_eq!(hair_source.as_deref(),Some(patch_zip.as_path()),"a prefab actually present in the patch still uses it");
        let (bytes,source)=crate::accessory_bundle_source(game,None,entry).unwrap();
        assert!(source.is_none(),"absent patch asset must use vanilla");
        let hint=crate::unity::accessory_color_use(&bytes,&entry.main_data).unwrap();
        let (mut meshes,_)=crate::meshes_for_accessory_entry(game,None,entry,&card,0,&property);
        assert!(meshes.iter().all(|m|crate::ids_hit(&m.renderer_ids,&hint.normal)),"mesh and colour component identities must come from the same prefab");
        crate::color_accessory_meshes(game,None,entry,&card,&property,0,&mut meshes,&[],&[],None);
        let native_path=native.join(format!("Card0.json.renderer{row}.mat0._MainTex.png"));
        let input=image::open(&native_path).unwrap().to_rgba8();
        assert!(input.pixels().all(|p|p.0[..3]==[255;3]),"independent game input fixture must be white");
        for m in meshes {
            let png=m.texture_png.unwrap();let image=crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
            assert_eq!(image.dimensions(),input.dimensions());
            assert!(image.pixels().all(|p|p.0[..3]==[255;3]),"native colours are white; authored cyan/gray must not survive slot {slot}");
            eprintln!("native partial override slot={slot} RGB=white pixels={} renderer-group=normal",image.width()*image.height());
        }
    }
}

#[test]
#[ignore="native textured MeshRenderer UV channels"]
fn native_rigid_material_uv() {
    let game=&crate::test_support::game_path("");
    let s=open(game,Some(&game.join("UserData/chara/female/KK_384917.png")),0).unwrap();
    let part=s.parts.iter().find(|p|p.name=="irukard.001").unwrap();
    let source:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(game.join("mesh_dump/KK_384917.png/凶鳥 狂死華_0/mesh_16_o_weebninja.json")).unwrap()).unwrap();
    let uv=source["mesh"]["uv"].as_array().unwrap();
    assert_eq!(part.uvs.len(),uv.len(),"native MeshRenderer UV0 must reach the material");
    let max=part.uvs.iter().zip(uv).map(|(a,b)|((a[0]-b["x"].as_f64().unwrap() as f32).abs()).max((a[1]-b["y"].as_f64().unwrap() as f32).abs())).fold(0f32,f32::max);
    eprintln!("native MeshRenderer UV count={} max={max}",uv.len());
    assert!(max<2e-5,"native UV0 mismatch max={max}");
}

#[test]
#[ignore="native null-MainTex glasses with Color4 alpha"]
fn native_accessory_glass_material() {
    let game=&crate::test_support::game_path("");
    let s=open(game,Some(&game.join("UserData/chara/female/弹丸论破 - 江之岛盾子2.png")),0).unwrap();
    let part=s.parts.iter().find(|p|p.name=="o_glasses_typeF_lens").unwrap();
    assert_eq!(part.blend,"translucent","native source Forward pass is SrcAlpha/OneMinusSrcAlpha");
    let png=part.png.as_ref().expect("declared white default multiplied by Color4");
    let image=crate::teximg::load_from_memory(png).unwrap().to_rgba8();
    // SourceWorld Card2 material TSV records Color4=(0,.163623989,.19607836,.1254902).
    assert!(image.pixels().all(|p|p.0==[0,42,50,32]),"native base colour and alpha must both survive null MainTex");
}

#[test]
#[ignore="selected accessory prefab renderer and placement diagnosis"]
fn source_rigid_renderer_diagnostics() {
    let game=&crate::test_support::game_path("");
    let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_384915.png")).unwrap();
    let resolves:Vec<_>=card.resolves.iter().filter(|r|crate::accessory_slot_from_property(&r.property).is_some_and(|s|s==33||s==34)).collect();
    let wanted=resolves.iter().map(|r|r.mod_id.clone()).collect();let guids=crate::mods::find_guids(&game.join("mods"),Some(&wanted));
    for r in resolves {
        let zip=guids.get(&r.mod_id).unwrap();let catalog=crate::list::catalog_from_zipmod(zip);let entry=catalog.get_slot(r.category_no,r.slot,r.local_slot).unwrap();
        let bundle=crate::unity::parse_bundle_cached(crate::mods::zipmod_bundle_named(zip,&entry.main_ab).unwrap());
        let (meshes,skel)=bundle.meshes_for_maindata_including_disabled(&entry.main_data);
        eprintln!("SOURCE RIGID prop={} entry={:?} card={:?} skeleton={}",r.property,entry,card.accessory[crate::accessory_slot_from_property(&r.property).unwrap()],skel.is_some());
        for m in meshes {eprintln!("SOURCE RIGID mesh={} renderer={:?} names={:?} skinned={:?} weights={} binds={} prefab={:?} moves={:?}",m.name,m.renderer_ids,m.renderer_names,m.renderer_skinned,m.weights.len(),m.bind_poses.len(),m.prefab_local,m.move_nodes);}
    }
}

#[test]
#[ignore="actual MeshRenderer dump vertices and assigned clothes slots"]
fn native_rigid_and_clothes_geometry() {
    let game=&crate::test_support::game_path("");
    let mut s=open(game,Some(&game.join("UserData/chara/female/KK_384915.png")),0).unwrap();
    s.ensure_skel_export();s.anim_clip.clear();s.apply_anim_frame(0.,0.);
    let dump=game.join("mesh_dump/KK_384915.png/飴宮 怠美_0");
    let mut used=std::collections::HashSet::new();let mut metrics=Vec::new();
    for filename in ["mesh_2_OrganicShape04.json","mesh_30_OrganicShape19.json","mesh_31_OrganicShape19.json","mesh_32_OrganicShape19.json","mesh_33_OrganicShape19.json"] {
        let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(dump.join(filename)).unwrap()).unwrap();
        assert_eq!(native["rendererType"],"MeshRenderer");
        let name=native["mesh"]["name"].as_str().unwrap();
        let source=native["mesh"]["worldVertices"].as_array().unwrap();
        let error=|actual:&[[f32;3]]| {
            assert_eq!(actual.len(),source.len());let mut max=0f32;let mut sum=0f64;
            for (actual,native) in actual.iter().zip(source) {
                let d=(0..3).map(|i|(actual[i]-native[["x","y","z"][i]].as_f64().unwrap() as f32).powi(2)).sum::<f32>().sqrt();
                max=max.max(d);sum+=(d as f64).powi(2);
            }
            (max,(sum/source.len() as f64).sqrt())
        };
        let (index,part)=s.parts.iter().enumerate().filter(|(i,p)|p.name==name&&!used.contains(i)).min_by(|(_,a),(_,b)|error(&a.positions).1.total_cmp(&error(&b.positions).1)).unwrap();
        used.insert(index);let (max,rms)=error(&part.positions);
        eprintln!("native MeshRenderer {filename} vertices={} max={max} rms={rms}",source.len());
        assert!(max<2e-5,"native rigid {filename}: max={max} rms={rms}");
        assert_eq!(part.positions,part.rest_positions,"first frame must preserve placement");
        metrics.push(serde_json::json!({"fixture":filename,"vertices":source.len(),"max_m":max,"rms_m":rms}));
    }
    let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NewCards306SourceWorld");
    let mut body=s.body_skel.as_ref().unwrap().clone();
    let transforms=crate::gamefs::read_to_string(root.join("Card0.json.transforms.tsv")).unwrap();
    let parse=|s:&str|s.trim_matches(|c|c=='('||c==')').split(',').map(|s|s.trim().parse::<f32>().unwrap()).collect::<Vec<_>>();
    for line in transforms.lines() {
        let col:Vec<_>=line.split('\t').collect();if col.len()!=6||!col[2].contains("/p_cf_body_bone/cf_j_root")||col[2].contains("/ca_slot") {continue;}
        if let Some(tf)=body.tfs.values_mut().find(|tf|tf.name==col[1]) {tf.pos=parse(col[3]).try_into().unwrap();tf.rot=parse(col[4]).try_into().unwrap();tf.scale=parse(col[5]).try_into().unwrap();}
    }
    body.rebuild_worlds();
    for file in crate::gamefs::read_dir(root).unwrap().flatten().map(|f|f.path()).filter(|p|p.file_name().unwrap().to_string_lossy().starts_with("Card0.json.mesh")) {
        let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&file).unwrap()).unwrap();
        if native["name"]!="o_bot_skirt01_a" {continue;}
        let runtime=s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()).find(|r|r.source.name=="o_bot_skirt01_a").unwrap();
        assert_eq!(runtime.source.bind_poses.len(),148);assert_eq!(runtime.source.bone_names.len(),148);
        let vertices=runtime.skin_palette(runtime.source.smr_skel.as_ref().unwrap(),&crate::pose::ident(),Some(&body)).unwrap().0;
        let source=native["shaderWorldVertices"].as_array().unwrap();assert_eq!(vertices.len(),source.len());
        let max=vertices.iter().zip(source).map(|(a,b)|(0..3).map(|i|(a[i]-b[["x","y","z"][i]].as_f64().unwrap() as f32).powi(2)).sum::<f32>().sqrt()).fold(0f32,f32::max);
        assert!(max<2e-5,"native skirt LBS max={max}");eprintln!("native clothes slots=148 vertices={} max={max}",vertices.len());
        metrics.push(serde_json::json!({"fixture":file,"vertices":vertices.len(),"slots":148,"max_m":max}));
    }
    assert_eq!(metrics.len(),6);
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/NewCards306NativeRigidClothes.json",serde_json::to_vec_pretty(&metrics).unwrap()).unwrap();
}

#[test]
#[ignore="native accessory BakeMesh with actual Studio body transforms"]
fn native_assigned_accessory_vertices() {
    let game=&crate::test_support::game_path("");
    let mut s=open(game,Some(&game.join("UserData/chara/female/弹丸论破 - 江之岛盾子2.png")),0).unwrap();
    s.ensure_skel_export();
    let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NewCards306SourceWorld");
    let mut body=s.body_skel.as_ref().unwrap().clone();
    let transforms=crate::gamefs::read_to_string(root.join("Card2.json.transforms.tsv")).unwrap();
    let parse=|s:&str|s.trim_matches(|c|c=='('||c==')').split(',').map(|s|s.trim().parse::<f32>().unwrap()).collect::<Vec<_>>();
    let mut assigned=0;
    for line in transforms.lines() {
        let columns:Vec<_>=line.split('\t').collect();
        if columns.len()!=6 || !columns[2].contains("/p_cf_body_bone/cf_j_root") || columns[2].contains("/ca_slot") {continue;}
        if let Some(tf)=body.tfs.values_mut().find(|tf|tf.name==columns[1]) {
            tf.pos=parse(columns[3]).try_into().unwrap();tf.rot=parse(columns[4]).try_into().unwrap();tf.scale=parse(columns[5]).try_into().unwrap();assigned+=1;
        }
    }
    assert!(assigned>250);body.rebuild_worlds();
    let mut metrics=Vec::new();
    for file in ["Card2.json.mesh1.json","Card2.json.mesh3.json"] {
        let native:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join(file)).unwrap()).unwrap();
        let name=native["name"].as_str().unwrap();
        let side=if native["path"].as_str().unwrap().contains("/a_n_hand_L/") {"a_n_hand_L"}else{"a_n_hand_R"};
        let runtime=s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()).find(|r|r.source.name==name && r.parent_key==side).unwrap();
        let mut palette_error=0f32;let mut bind_error=0f32;
        for (i,bone) in native["bones"].as_array().unwrap().iter().enumerate() {
            let name=bone["name"].as_str().unwrap();let world=crate::dbone::implant::world_exact(&body,name).unwrap();
            for r in 0..4 {for c in 0..4 {let key=format!("m{r}{c}");palette_error=palette_error.max((world[r][c]-native["boneWorlds"][i][&key].as_f64().unwrap() as f32).abs());bind_error=bind_error.max((runtime.source.bind_poses[i][r][c]-native["bindposes"][i][&key].as_f64().unwrap() as f32).abs());}}
        }
        let mut raw_error=0f32;for (actual,native) in runtime.source.rest_positions.iter().zip(native["vertices"].as_array().unwrap()) {for c in 0..3 {raw_error=raw_error.max((actual[c]-native[["x","y","z"][c]].as_f64().unwrap() as f32).abs());}}
        eprintln!("accessory source side={side} body={palette_error} binds={bind_error} raw={raw_error}");
        assert!(palette_error<2e-5 && bind_error==0. && raw_error==0.);
        assert!(runtime.copy_weights_body && !runtime.removed_accessory_bones.is_empty());
        let vertices=runtime.skin_palette(runtime.source.smr_skel.as_ref().unwrap(),&crate::pose::ident(),Some(&body)).unwrap().0;
        let source=native["shaderWorldVertices"].as_array().unwrap();assert_eq!(vertices.len(),source.len());
        let mut max=0f32;let mut sum=0f64;
        for (actual,native) in vertices.iter().zip(source) {
            let expected:[f32;3]=std::array::from_fn(|i|native[["x","y","z"][i]].as_f64().unwrap() as f32);
            let error=actual.iter().zip(expected).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt();max=max.max(error);sum+=(error as f64).powi(2);
        }
        let rms=(sum/vertices.len() as f64).sqrt();
        assert!(max<2e-5,"native assigned accessory {name}: max={max} rms={rms}");
        eprintln!("native assigned accessory {name}: vertices={} max={max} rms={rms}",vertices.len());
        metrics.push(serde_json::json!({"mesh":name,"vertices":vertices.len(),"max_m":max,"rms_m":rms,"source_pose_local_count":assigned}));
    }
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/NewCards306NativeAccessoryVertices.json",serde_json::to_vec_pretty(&metrics).unwrap()).unwrap();
}

#[test]
#[ignore="installed accessory source texture diagnosis"]
fn installed_accessory_material_inputs() {
    let game=&crate::test_support::game_path("");let wanted=["cream002".to_string()].into_iter().collect();
    let guids=crate::mods::find_guids(&game.join("mods"),Some(&wanted));let zip=guids.get("cream002").unwrap();
    let catalog=crate::list::catalog_from_zipmod(zip);let entry=catalog.get(121,127).unwrap();
    let bytes=crate::mods::zipmod_bundle_named(zip,&entry.main_ab).unwrap();
    let (meshes,_)=crate::unity::meshes_skel_for_maindata_ref(&bytes,&entry.main_data);
    assert!(!meshes.is_empty());
    for mesh in &meshes {assert!(mesh.mat.is_some() && mesh.texture_png.is_some(),"selected prefab authored material/texture: {}",mesh.name);}
    for mesh in meshes.iter().filter(|mesh|mesh.name.contains("kamiZ")) {eprintln!("Source mesh={} png={:?} mat={:?}",mesh.name,mesh.texture_png.as_ref().map(Vec::len),mesh.mat);}
    let png=crate::unity::named_png_from_bytes(bytes,&["kuma_1024"]).expect("authored RGB24 MainTex");
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/NewCards306Overlays/BearSource.png",png.1).unwrap();
}

#[test]
#[ignore="native eye renderer fixtures; compares actual GPU create RT and KSOX output"]
fn native_eye_create_and_overlays() {
    let root=PathBuf::from(std::env::var("OK_EYE_NATIVE_ROOT").unwrap_or_else(|_|"C:/portable/OpenKoikatsu/Saved/Tests/NewCards306EyeNative2".into()));
    let sources:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join("Sources.json")).unwrap()).unwrap();
    let mut metrics=Vec::new();
    for (index,row) in sources.as_array().unwrap().iter().enumerate() {
        let card=crate::card::parse_card(Path::new(row["card"].as_str().unwrap())).unwrap();
        for side in 0..2 {
            let prefix=root.join(format!("Card{index}.json.eye{side}"));
            let read=|suffix:&str|crate::gamefs::read(format!("{}.{suffix}.png",prefix.display())).unwrap();
            let source_path=format!("{}.source.png",prefix.display());let has_source=Path::new(&source_path).ok_exists();
            let input=if has_source {crate::gamefs::read(source_path).unwrap()}else{read("input")};let gradient=read("gradation");
            let settings=crate::gamefs::read_to_string(root.join(format!("Card{index}.json.eye-create.tsv"))).unwrap();
            let linear=has_source && settings.lines().any(|line|line==format!("mainColorSpace\t{side}\tLinear"));
            let output=crate::bake_iris_with_space(&input,&card.pupils[side],Some(&gradient),None,None,[1.;4],[1.;4],0.5,0.5,linear).unwrap();
            let compare=|png:&[u8],native:&[u8]| {
                let a=crate::teximg::load_from_memory(png).unwrap().to_rgba8();let b=crate::teximg::load_from_memory(native).unwrap().to_rgba8();assert_eq!(a.dimensions(),b.dimensions());
                let mut sum=0.;let mut count=0.;let mut max=0u8;
                for (a,b) in a.pixels().zip(b.pixels()) {if b[3]>=250 {for c in 0..3 {let d=a[c].abs_diff(b[c]);sum+=d as f64;count+=1.;max=max.max(d);}}}
                (sum/count,max)
            };
            std::fs::write(format!("{}.rust.png",prefix.display()),&output).unwrap();
            eprintln!("native eye card{index} side{side} pupil={:?}",card.pupils[side]);
            let pure=compare(&output,&read("pure"));assert!(pure.0<0.6 && pure.1<=3,"native pure card{index} side{side} {pure:?}");
            let mut overlay_metric=None;
            if let Some(overlay)=crate::ksox_overlay_png(&card,0,if side==0 {8}else{10}) {
                let composite=crate::composite_overlay(&read("pure"),&overlay).unwrap();let original=read("original");
                let metric=compare(&composite,&original);assert!(metric.0<0.6,"native KSOX {metric:?}");
                let a=crate::teximg::load_from_memory(&composite).unwrap().to_rgba8();let b=crate::teximg::load_from_memory(&original).unwrap().to_rgba8();
                assert!(a.pixels().zip(b.pixels()).all(|(a,b)|a[3].abs_diff(b[3])<=1),"KSOX alpha must also be interpolated");overlay_metric=Some(metric);
            }
            metrics.push(serde_json::json!({"card":row["card"],"side":side,"pure_mean":pure.0,"pure_max":pure.1,"overlay":overlay_metric}));
        }
    }
    std::fs::write(root.join("EyePixels.json"),serde_json::to_vec_pretty(&metrics).unwrap()).unwrap();
}

#[test]
#[ignore="installed-card source list identities"]
fn dump_card_texture_identities() {
    let cards:Vec<String>=serde_json::from_str(&std::env::var("OK_AUDIT_CARDS").unwrap()).unwrap();
    let output=PathBuf::from(std::env::var("OK_AUDIT_OUTPUT").unwrap());assert!(output.is_absolute());
    std::fs::create_dir_all(&output).unwrap();
    for (index,name) in cards.iter().enumerate() {
        let card=crate::card::parse_card(&crate::test_support::game_path("/UserData/chara/female").join(name).as_path()).unwrap();
        std::fs::write(output.join(format!("Card{index}.identities.txt")),format!("head={} nose={} pupils={:?}\naccessory={:#?}\nresolves={:#?}",card.head_id,card.nose_id,card.pupils,card.accessory,card.resolves)).unwrap();
        let mut overlays=Vec::new();for ((coordinate,kind),png) in &card.ksox {
            let path=output.join(format!("Card{index}.overlay{coordinate}_{kind}.png"));std::fs::write(&path,png).unwrap();overlays.push((*coordinate,*kind,path));
        }
        let textures:Vec<_>=card.me_tex.iter().map(|(key,png)|(format!("{key:?}"),png.len())).collect();
        std::fs::write(output.join(format!("Card{index}.overlays.json")),serde_json::to_vec_pretty(&serde_json::json!({"overlays":overlays,"me_textures":textures,"me_colors":format!("{:?}",card.me_color)})).unwrap()).unwrap();
    }
}

#[test]
#[ignore="installed-card renderer diagnostics; writes source data, not visual acceptance"]
fn dump_runtime_renderer_diagnostics() {
    let cards:Vec<String>=serde_json::from_str(&std::env::var("OK_AUDIT_CARDS").expect("JSON card filenames")).unwrap();
    let output=PathBuf::from(std::env::var("OK_AUDIT_OUTPUT").expect("absolute diagnostic output"));
    assert!(output.is_absolute());std::fs::create_dir_all(&output).unwrap();
    std::env::set_var("OK_GPU_SKIN","1");std::env::set_var("OK_SKELMESH","1");std::env::remove_var("OK_UE_ANIM");
    let game=&crate::test_support::game_path("");
    for (card_index,card) in cards.iter().enumerate() {
        assert_eq!(Path::new(card).file_name().unwrap().to_str().unwrap(),card);
        let mut s=open(game,Some(&game.join("UserData/chara/female").join(card)),0).unwrap();
        s.ensure_skel_export();s.anim_clip.clear();s.apply_anim_frame(0.,0.);
        if let Some(body)=&s.body_skel {
            let bones:Vec<_>=body.tfs.values().map(|tf|serde_json::json!({"name":tf.name,"position":tf.pos,"rotation":tf.rot,"scale":tf.scale,"world":body.world_tf(&tf.name)})).collect();
            std::fs::write(output.join(format!("Card{card_index}.body.json")),serde_json::to_vec(&bones).unwrap()).unwrap();
        }
        let mut rows=Vec::new();
        for (index,p) in s.parts.iter().enumerate() {
            let bounds=|vertices:&Vec<[f32;3]>| {
                let mut min=[f32::MAX;3];let mut max=[f32::MIN;3];
                for v in vertices {for i in 0..3 {min[i]=min[i].min(v[i]);max[i]=max[i].max(v[i]);}}
                [min,max]
            };
            let image=if let Some(png)=&p.png {let path=output.join(format!("Card{card_index}.part{index}.png"));std::fs::write(&path,png).unwrap();Some(path)}else{None};
            for (slot,png) in [("gloss",&p.hair_gloss),("detail",&p.detail_mask),("overtex",&p.overtex_png)] {
                if let Some(png)=png {std::fs::write(output.join(format!("Card{card_index}.part{index}.{slot}.png")),png).unwrap();}
            }
            let eye=p.eye_draw.as_ref().map(|draw|serde_json::json!({"kind":draw.kind,"main_st":draw.main_texcoord_st,"highlight_colors":draw.colors}));
            let private=p.runtime_accessory.as_ref().map(|a|serde_json::json!({"parent":a.parent_key,"placement":a.local_placement,
                "source_bounds":bounds(&a.source.positions),"bones":a.source.bone_names,"bind_poses":a.source.bind_poses,"copy_weights_body":a.copy_weights_body}));
            if std::env::var("OK_AUDIT_MESHES").ok().is_some_and(|names|names.split(';').any(|name|name==p.name)) {
                std::fs::write(output.join(format!("Card{card_index}.part{index}.vertices.json")),serde_json::to_vec(&p.positions).unwrap()).unwrap();
                std::fs::write(output.join(format!("Card{card_index}.part{index}.geometry.json")),serde_json::to_vec(&serde_json::json!({
                    "positions":p.positions,"normals":p.normals,"indices":p.indices,"uvs":p.uvs,
                    "rest_positions":p.rest_positions,"rest_normals":p.rest_normals,"tangents":p.rest_tangents,
                    "outline":p.source_outline_enabled,"width":s.line_width
                })).unwrap()).unwrap();
                if let Some(png)=&p.alpha_mask {std::fs::write(output.join(format!("Card{card_index}.part{index}.alpha.png")),png).unwrap();}
            }
            rows.push(serde_json::json!({"index":index,"role":p.role,"name":p.name,"visible":p.render_visible,"vertices":p.positions.len(),
                "bounds":bounds(&p.positions),"rest_bounds":bounds(&p.rest_positions),"parent":p.parent_bone,"rigid_follow":p.rigid_follow_bone,
                "blend":p.blend,"shader_kind":p.shader_kind,"main_st":p.main_tex_st,"image":image,"eye":eye,"runtime":private,
                "material":p.source_material,"outline":p.source_outline_enabled,"line_width":s.line_width,
                "overcolor":p.overcolor,"spec_height":p.spec_height,"skin_line_width":p.skin_line_width,
                "uv_counts":[p.uvs.len(),p.uv2.len(),p.uv3.len(),p.uv4.len()],
                "shader_texture_names":p.shader_textures.keys().collect::<Vec<_>>() }));
        }
        std::fs::write(output.join(format!("Card{card_index}.json")),serde_json::to_vec_pretty(&serde_json::json!({"card":card,"parts":rows})).unwrap()).unwrap();
        eprintln!("Renderer diagnostic card={card} parts={}",s.parts.len());
    }
}

#[test]
#[ignore="independent native KCOX body AlphaMask fixture"]
fn native_clothes_overlay_mask() {
    let game=&crate::test_support::game_path("");
    let card_path=game.join("UserData/chara/female/KK_346095.png");
    let card=crate::card::parse_card(&card_path).unwrap();
    assert_eq!(card.clothes_masks.len(),2);
    assert!(card.clothes_masks.contains_key(&(0,"BodyMask".into())));
    assert!(!card.clothes_masks.contains_key(&(1,"BodyMask".into())),"KCOX has no cross-coordinate fallback");
    let s=open(game,Some(&card_path),0).unwrap();
    let body=s.parts.iter().find(|p|p.role=="body").unwrap();
    let image=crate::teximg::load_from_memory(body.alpha_mask.as_ref().expect("KCOX mask")).unwrap().to_rgba8();
    let fixtures=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NewCards306BodyMaskNative");
    let native=crate::gamefs::read_dir(fixtures).unwrap().filter_map(Result::ok)
        .map(|e|e.path()).find(|p|p.file_name().unwrap().to_string_lossy().ends_with("._AlphaMask.png")).unwrap();
    let expected=image::open(native).unwrap().to_rgba8();
    assert_eq!(image.dimensions(),expected.dimensions());
    let max=image.pixels().zip(expected.pixels()).map(|(a,b)|
        (0..3).map(|i|a[i].abs_diff(b[i])).max().unwrap()).max().unwrap();
    let holes=image.pixels().zip(expected.pixels()).filter(|(a,b)|
        (a[0].min(a[1])<128)!=(b[0].min(b[1])<128)).count();
    eprintln!("native KCOX body mask pixels={} RGB max={max} coverage_mismatches={holes}",image.width()*image.height());
    assert_eq!(holes,0,"source clothes/body visibility must agree");
    assert!(max<=3,"native input pixel error max={max}");
}
#[test]
#[ignore="installed actual p199 hair inputs and additive keyword ABI"]
fn cached_native_hair_family_inputs() {
    let db=rusqlite::Connection::open_with_flags(crate::test_support::game_path("/.ok_cache.sqlite"),rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY).unwrap();
    let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Hair322CachedInputs");std::fs::create_dir_all(root).unwrap();let mut rows=Vec::new();
    for name in ["KK_380982.png","Komazoku Megumin (2).png"] {
        let (key,bytes):(String,Vec<u8>)=db.query_row("SELECT card_key,payload FROM session_blob WHERE card_key LIKE ?1 AND card_key LIKE '%|p199|%' AND card_key LIKE '%OK_GPU_SKIN=1|%' AND card_key LIKE '%OK_NOPOSE=1|%' ORDER BY card_key DESC LIMIT 1",[format!("%/{name}|%")],|r|Ok((r.get(0)?,r.get(1)?))).unwrap();
        let blob:SessionBlob=bincode::deserialize(&bytes).unwrap();let mut s=blob.into_session();
        for index in 0..s.parts.len() {
            if s.parts[index].shader_kind!=10 {continue;}
            let mat=s.parts[index].source_material.as_ref().unwrap().clone();
            for keyword in ["_REVERSECOLOR01_ON","_OLDHAIR_ON","_SPECLARSWITCH_ON"] {
                let keyword=std::ffi::CString::new(keyword.to_ascii_lowercase()).unwrap();
                let expected=i32::from(mat.keywords.iter().any(|v|v.eq_ignore_ascii_case(keyword.to_str().unwrap())));
                assert_eq!(unsafe{crate::ffi::ok_part_material_keyword(&mut s as *mut _,index as i32,keyword.as_ptr())},expected);
            }
            let p=&s.parts[index];let draw=p.item_draw.as_ref().unwrap();let mat=p.source_material.as_ref().unwrap();
            let prefix=format!("{}.part{index}",Path::new(name).file_stem().unwrap().to_str().unwrap());
            let mut textures=std::collections::BTreeMap::new();
            for (property,png) in [("_MainTex",&p.png),("_ColorMask",&draw.color_mask),("_DetailMask",&p.detail_mask),("_HairGloss",&p.hair_gloss)] {
                if let Some(png)=png {let path=root.join(format!("{prefix}.{property}.png"));std::fs::write(&path,png).unwrap();textures.insert(property,path);}
            }
            rows.push(serde_json::json!({"card":name,"cache_key":key,"index":index,"mesh":p.name,"material":mat.name,"shader":mat.shader,"colors":draw.colors,"floats":mat.floats,"keywords":mat.keywords,"uvs":p.uvs,"textures":textures,"runtime_textures":p.runtime_textures}));
        }
        let keyword=std::ffi::CString::new("_OLDHAIR_ON").unwrap();
        assert_eq!(unsafe{crate::ffi::ok_part_material_keyword(&mut s,-1,keyword.as_ptr())},-1);
        assert_eq!(unsafe{crate::ffi::ok_part_material_keyword(&mut s,0,std::ptr::null())},-1);
    }
    assert!(rows.len()>=10);
    std::fs::write(root.join("inputs.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}

#[test]
#[ignore="read-only C019 runtime versus CardReader appearance diagnostic"]
fn street_c019_appearance_diagnostic() {
    let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C019.png");
    let output=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/StreetAppearance01");
    std::fs::create_dir_all(output).unwrap();let mut s=crate::portable::load(path).unwrap();let mut rows=Vec::new();
    fn record(s:&OkSession,tag:&str)->serde_json::Value {
        let parts:Vec<_>=s.parts.iter().enumerate().map(|(i,p)|{
            let mut lo=[f32::INFINITY;3];let mut hi=[f32::NEG_INFINITY;3];
            for v in &p.positions {for a in 0..3 {lo[a]=lo[a].min(v[a]);hi[a]=hi[a].max(v[a]);}}
            serde_json::json!({"index":i,"name":p.name,"role":p.role,"min":lo,"max":hi,"kind":p.shader_kind,"material":p.source_material,"hair_gloss_bytes":p.hair_gloss.as_ref().map(|x|x.len()),"detail_bytes":p.detail_mask.as_ref().map(|x|x.len()),"png_bytes":p.png.as_ref().map(|x|x.len()),"uv2_count":p.uv2.len(),"runtime_textures":p.runtime_textures.keys().collect::<Vec<_>>()})
        }).collect();serde_json::json!({"tag":tag,"parts":parts,"shape_body":s.shape_body,"shape_face":s.shape_face,"shadow_depth":s.shadow_depth,"line_width":s.line_width,"line_depth":s.line_depth})
    }
    let game=&crate::test_support::game_path("");let source=game.join("UserData/chara/female/KK_384914.png");for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"] { std::env::set_var(key,"1"); } let fresh=crate::extract_session(game,Some(&source),0,None).unwrap();rows.push(record(&fresh,"source_original"));rows.push(record(&s,"apng_open"));s.apply_shape(&s.shape_body.clone(),&s.shape_face.clone());s.skin_all();rows.push(record(&s,"cardreader_shape"));
    let curves=crate::gamefs::read("C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/conversation_female.okanim").unwrap_or_else(|_|crate::gamefs::read("C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim").unwrap());
    unsafe{assert_eq!(crate::ffi::ok_session_install_shared_clip(&mut s,curves.as_ptr(),curves.len() as i32),0);}
    s.apply_anim_frame(0.,0.);s.skin_all();rows.push(record(&s,"shared_pose_shape"));let mut raw=crate::portable::load(path).unwrap();
    unsafe{assert_eq!(crate::ffi::ok_session_install_shared_clip(&mut raw,curves.as_ptr(),curves.len() as i32),0);}
    raw.apply_anim_frame(0.,0.);raw.skin_all();rows.push(record(&raw,"shared_pose_raw"));std::fs::write(output.join("native.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
}
