#[test]
fn bundle_owned_bytes_keep_allocation_and_shared_views_alive(){
    use unity_asset_binary::{shared_bytes::SharedBytes,data_view::DataView};
    let bytes:Vec<_>=(0..4096).map(|i|(i%251) as u8).collect();
    let expected=bytes.clone();let address=bytes.as_ptr() as usize;
    let shared=SharedBytes::from_vec(bytes);assert_eq!(shared.ptr_usize(),address);
    let view=DataView::from_shared_range(shared.clone(),123..3000).unwrap();
    drop(shared);assert_eq!(view.as_bytes(),&expected[123..3000]);
    let clone=view.clone();drop(view);assert_eq!(clone.as_bytes(),&expected[123..3000]);
    assert_eq!(clone.backing_shared().ptr_usize(),address);
    let arc=Arc::<[u8]>::from(expected.clone());let address=arc.as_ptr() as usize;
    let shared=SharedBytes::from_arc(arc);assert_eq!(shared.ptr_usize(),address);assert_eq!(shared.as_bytes(),expected);
}
#[test]
fn bundle_zip_cache_validates_archive_entry_and_parser_lifetime() {
    use std::io::Write;
    let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
    let directory=std::env::temp_dir().join(format!("ok-bundle-zip-{}-{stamp}",std::process::id()));
    std::fs::create_dir(&directory).unwrap();let path=directory.join("source.zipmod");
    let write=|marker:u8|{
        let mut zip=zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
        for name in ["abdata/chara/a.unity3d","abdata/chara/b.unity3d"] {
            zip.start_file(name,zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored)).unwrap();
            zip.write_all(&[marker;128]).unwrap();
        }
        zip.finish().unwrap();
    };
    write(1);let first=parse_bundle_zip_cached(&path,"chara/a.unity3d").unwrap();
    assert!(Arc::ptr_eq(&first,&parse_bundle_zip_cached(&directory.join(".").join("source.zipmod"),"CHARA\\A.UNITY3D").unwrap()));
    let other=parse_bundle_zip_cached(&path,"chara/b.unity3d").unwrap();assert!(!Arc::ptr_eq(&first,&other));
    assert!(parse_bundle_zip_cached(&path,"chara/missing.unity3d").is_none());
    let size=crate::gamefs::metadata(&path).unwrap().len();write(2);assert_eq!(crate::gamefs::metadata(&path).unwrap().len(),size);
    std::fs::File::options().write(true).open(&path).unwrap().set_modified(std::time::UNIX_EPOCH+std::time::Duration::from_secs((stamp/1_000_000_000) as u64+10)).unwrap();
    let changed=parse_bundle_zip_cached(&path,"chara/a.unity3d").unwrap();assert!(!Arc::ptr_eq(&first,&changed));
    BUNDLE_CACHE.with(|cache|cache.borrow_mut().clear());
    assert!(!Arc::ptr_eq(&changed,&parse_bundle_zip_cached(&path,"chara/a.unity3d").unwrap()));
    std::fs::remove_file(&path).unwrap();assert!(parse_bundle_zip_cached(&path,"chara/a.unity3d").is_none());
    clear_bundle_cache();assert!(BUNDLE_ZIP_INDEX.with(|index|index.borrow().is_empty()));std::fs::remove_dir(&directory).unwrap();
}
#[test]
fn bundle_path_cache_validates_file_and_parser_lifetime() {
    let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
    let directory=std::env::temp_dir().join(format!("ok-bundle-path-{}-{stamp}",std::process::id()));
    std::fs::create_dir(&directory).unwrap();
    let path=directory.join("source.unity3d");
    // Invalid/empty bundles are sufficient to test ownership and file
    // invalidation without copying installed assets into a temp fixture.
    std::fs::write(&path,b"first").unwrap();
    let first=parse_bundle_path_cached(&path).unwrap();
    assert!(Arc::ptr_eq(&first,&parse_bundle_path_cached(&path).unwrap()));
    let alias=directory.join(".").join("source.unity3d");
    assert!(Arc::ptr_eq(&first,&parse_bundle_path_cached(&alias).unwrap()));
    std::fs::write(&path,b"second version").unwrap();
    let changed=parse_bundle_path_cached(&path).unwrap();
    assert!(!Arc::ptr_eq(&first,&changed));
    assert!(Arc::ptr_eq(&changed,&parse_bundle_path_cached(&path).unwrap()));
    std::fs::write(&path,b"newest version").unwrap();
    std::fs::File::options().write(true).open(&path).unwrap().set_modified(
        std::time::UNIX_EPOCH+std::time::Duration::from_secs((stamp/1_000_000_000) as u64+10)).unwrap();
    let same_size_changed=parse_bundle_path_cached(&path).unwrap();
    assert!(!Arc::ptr_eq(&changed,&same_size_changed),"mtime must invalidate same-size replacements");
    BUNDLE_CACHE.with(|cache|cache.borrow_mut().clear());
    let reloaded=parse_bundle_path_cached(&path).unwrap();
    assert!(!Arc::ptr_eq(&same_size_changed,&reloaded),"path index must not retain evicted parser objects");
    let mut interior=vec![17;2048];std::fs::write(&path,&interior).unwrap();
    let original=parse_bundle_path_cached(&path).unwrap();
    let sampled_key=content_key(&interior);interior[600]=23;
    assert_eq!(sampled_key,content_key(&interior),"fixture must change outside the legacy sampled locations");
    std::fs::write(&path,&interior).unwrap();
    std::fs::File::options().write(true).open(&path).unwrap().set_modified(
        std::time::UNIX_EPOCH+std::time::Duration::from_secs((stamp/1_000_000_000) as u64+20)).unwrap();
    assert!(!Arc::ptr_eq(&original,&parse_bundle_path_cached(&path).unwrap()),"same sampled bytes must not reuse an older path parser");
    std::fs::remove_file(&path).unwrap();
    assert!(parse_bundle_path_cached(&path).is_none(),"deleted source must not be returned from cache");
    clear_bundle_cache();
    assert!(BUNDLE_PATH_INDEX.with(|index|index.borrow().is_empty()));
    std::fs::remove_dir(&directory).unwrap();
}
#[test]
fn texture_index_allows_nested_empty_serialized_reads() {
    let bundle = parse_bundle_uncached(Vec::new().into());
    bundle.with_serialized(|files| {
        assert!(files.is_empty());
        assert!(bundle.decode_tex("missing").is_none());
        assert!(bundle.texture_color_space("missing").is_none());
        assert!(bundle.texture_sampling_named("missing").is_none());
        bundle.ensure_tex_catalog();

    });
    assert!(bundle.tex_objects.borrow().as_ref().unwrap().is_empty());
}

#[test]
#[ignore = "requires installed original Texture2D bundles"]
fn texture_index_matches_linear_lookup_on_installed_sources() {
    let mut sources=Vec::new();
    for source in ["mt_eyebrow_00", "mt_eyeline_up_00", "mt_eye_white_00"] {
        let path=&*Box::leak(crate::test_support::game_path("/abdata/chara").join(format!("{source}.unity3d")).into_boxed_path());
        sources.push((source.to_string(),crate::gamefs::read(&path).expect("installed original bundle"),Some(path)));
    }
    let zip=crate::mods::all_zipmods(&crate::test_support::game_path("/mods")).into_iter()
        .find(|p|p.file_name().is_some_and(|n|n.to_string_lossy().contains("[moderchan]charamaker support MOD v1.1"))).expect("installed CMS mod");
    sources.push(("CMS_NT_00".into(),crate::mods::zipmod_bundle_named(&zip,"chara/_kero/CMS_NT_00.unity3d").expect("installed CMS bundle").to_vec(),None));
    for (source,bytes,path) in sources {
        // The oracle uses the eager container and full objects, independently
        // of ParsedBundle's directory, range extraction and header index.
        let old=load_bundle_unbounded(bytes.clone()).expect("eager oracle container");
        let old_files:Vec<_>=old.files.iter().map(|f|(f.name.clone(),old.extract_file_data(f).unwrap())).collect();
        let old_serialized:Vec<_>=old_files.iter().filter(|(name,_)|{
            let n=name.to_ascii_lowercase();!n.ends_with(".ress")&&!n.ends_with(".resource")
        }).filter_map(|(_,b)|parse_serialized_file(b.clone()).ok()).collect();
        let bundle=parse_bundle_uncached(bytes.clone().into());
        let path_bundle=path.as_ref().and_then(|p|parse_bundle_path_cached(p)).unwrap_or_else(||parse_bundle_cached(bytes.clone()));
        assert_eq!(bundle.files.len(),old_files.len(),"all directory entries including resource streams");
        for ((name,data),(expected_name,expected)) in bundle.files.iter().zip(&old_files) {
            assert_eq!(name,expected_name);
            let size=expected.len().min(4096) as u32;
            assert_eq!(data.read_range(0,size).as_deref(),Some(&expected[..size as usize]));
            let offset=expected.len().saturating_sub(size as usize) as u64;
            assert_eq!(data.read_range(offset,size).as_deref(),Some(&expected[offset as usize..]));
            assert!(data.read_range(expected.len() as u64,1).is_none());
            assert!(data.read_range(u64::MAX,1).is_none());
            assert_eq!(data.read_range(expected.len() as u64,0),Some(Vec::new()));
            // Exercise actual compressed block boundaries, not just the
            // first/last bytes. Compare against the eager independent file.
            let file=&data.file;let mut start=0u64;
            for block in &data.bundle.blocks {
                if start>=file.offset&&start<file.offset+file.size {
                    let relative=(start-file.offset).saturating_sub(7) as usize;
                    let length=(expected.len()-relative).min(31);
                    assert_eq!(data.read_range(relative as u64,length as u32).as_deref(),Some(&expected[relative..relative+length]),"cross-block range {source}");
                }
                start+=block.uncompressed_size as u64;
            }
        }
        bundle.ensure_tex_catalog();
        assert!(bundle.serialized.borrow().is_none(),"texture catalog must not initialize full geometry files");
        let expected_names:Vec<_>=old_serialized.iter().flat_map(|file|file.objects_of_type(28).into_iter().filter_map(|info|{
            let object=UnityObject::from_serialized_file(file,info).ok()?;
            let name=object.name().unwrap_or_default();if name.is_empty(){return None;}
            let props=object.class.properties();
            let w=props.get("m_Width").and_then(|v|v.as_i64()).unwrap_or(0) as u32;
            let h=props.get("m_Height").and_then(|v|v.as_i64()).unwrap_or(0) as u32;
            Some((name,w.saturating_mul(h.max(1))))
        })).collect();
        assert_eq!(*bundle.tex_names.borrow(),expected_names,"complete catalog order, dimensions and names {source}");
        assert!(bundle.tex_catalog_complete.get(),"installed texture directory must be completely parsed {source}");
        assert!(named_png_cached_fallback(&bundle,&["__missing_texture__"],||panic!("a proven absent name must not re-read the full bundle")).is_none());
        let names:Vec<_>=bundle.tex_names.borrow().iter().take(10).map(|(name,_)|name.clone()).collect();
        assert_eq!(names.len(),10,"need ten real textures in {source}");
        for name in names {
            // Independent old-order scan is the oracle, including duplicate
            // names, Unity's legacy wrap fallback and raw color-space fallback.
            let expected=(|| {
                let files=&old_serialized;
                for file in files {for info in file.objects_of_type(28) {
                    let Ok(object)=UnityObject::from_serialized_file(file,info) else {continue;};
                    if !object.name().unwrap_or_default().eq_ignore_ascii_case(&name) {continue;}
                    let props=object.class.properties();
                    let cs=props.get("m_ColorSpace").and_then(|v|v.as_i64()).map(|v|v as i32)
                        .or_else(||parse_tex_unity2019(object.raw_data()).map(|t|t.color_space));
                    let sampling=props.get("m_TextureSettings").map(|settings| {
                        let value=|key:&str|uv_obj(settings,key).and_then(|v|v.as_i64()).map(|v|v as i32);
                        let wrap=value("m_WrapMode").unwrap_or(0);
                        (value("m_FilterMode").unwrap_or(1),value("m_WrapU").unwrap_or(wrap),
                         value("m_WrapV").unwrap_or(wrap),props.get("m_ColorSpace").and_then(|v|v.as_i64()).unwrap_or(1)!=0,
                         props.get("m_MipCount").and_then(|v|v.as_i64()).unwrap_or(1) as i32)
                    });
                    return Some((png_from_texture(&object,&old_files,None).map(|(_,png)|png),cs,sampling));
                }} None
            })().expect("linear lookup");
            assert!(expected.0.is_some(),"oracle must produce real PNG {source}/{name}");
            if source=="CMS_NT_00" {
                let(_,png,cs)=named_png_with_color_space_from_zip(&zip,"chara/_kero/CMS_NT_00.unity3d",&[&name]).expect("zip-backed source");
                assert_eq!(Some(png),expected.0,"zip-backed PNG {name}");assert_eq!(cs,expected.1,"zip-backed color space {name}");
            }
            for query in [name.clone(),name.to_ascii_uppercase()] {
                assert_eq!(bundle.decode_tex(&query),expected.0,"PNG {source}/{query}");
                assert_eq!(path_bundle.exact_texture_png(&query),expected.0,"cached file PNG {source}/{query}");
                assert_eq!(path_bundle.texture_color_space(&query),expected.1,"cold exact color space {source}/{query}");
                assert_eq!(path_bundle.texture_sampling_named(&query).map(|s|(s.filter,s.wrap_u,s.wrap_v,s.srgb,s.mip_count)),expected.2,"cold exact sampling {source}/{query}");
                assert!(path_bundle.tex_objects.borrow().is_none(),"exact lookup must not build the complete texture catalog");
                assert_eq!(bundle.texture_color_space(&query),expected.1,"color space {source}/{query}");
                assert_eq!(bundle.texture_sampling_named(&query).map(|s|(s.filter,s.wrap_u,s.wrap_v,s.srgb,s.mip_count)),expected.2,"sampling {source}/{query}");
            }
        }
        assert!(bundle.serialized.borrow().is_none(),"exact texture decode must not initialize full geometry files");
        for (name,location) in bundle.tex_objects.borrow().as_ref().unwrap(){
            let actual=path_bundle.exact_texture_location(name).expect("every indexed original texture must resolve");
            assert_eq!((actual.file,actual.path_id,actual.color_space),(location.file,location.path_id,location.color_space),"complete original texture identity oracle {source}/{name}");
        }
        let first=&expected_names[0].0;
        for queries in [vec![first.clone()],vec![first.to_ascii_uppercase()],vec![String::new()],
            vec!["thumb".into()],vec!["__missing_texture__".into()],vec!["__missing_texture__".into(),first.clone()],
            vec![first.trim_end_matches(|c:char|c.is_ascii_digit()).to_string()]] {
            let refs:Vec<_>=queries.iter().map(String::as_str).collect();
            let legacy=named_png_from_bytes_legacy(bytes.clone(),&refs);
            let cold=parse_bundle_uncached(bytes.clone().into());
            assert_eq!(named_png_from_parsed(&cold,&refs),legacy,"cold named/fuzzy lookup must preserve ranking {source}/{queries:?}");
            for _ in 0..2 {
                let selected=if let Some(path)=&path {named_png_from_bundle(path,&refs)}
                    else {named_png_from_zip(&zip,"chara/_kero/CMS_NT_00.unity3d",&refs)};
                assert_eq!(selected,legacy,"path/zip cached lookup must preserve ranking {source}/{queries:?}");
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn installed_shader_declaration_distinguishes_rim_from_unsaved_material_float() {
    let path = &crate::test_support::game_path(r"\kks_decomp\plugins\KKS_Plugins\KKS_MaterialEditor\KK_Plugins.Resources.EC_Shaders.unity3d");
    if !path.ok_exists() { return; }
    let bundle = load_bundle_path_unbounded(path).expect("installed MaterialEditor shader bundle");
    let _ = bundle.data_checked();
    let mut found = std::collections::HashMap::new();
    for file in &bundle.files {
        let bytes = bundle.extract_file_data(file).expect("shader asset bytes");
        let serialized = parse_serialized_file(bytes).expect("serialized shader file");
        for info in serialized.objects_of_type(48) {
            let shader = UnityObject::from_serialized_file(&serialized, info).expect("Shader object");
            found.insert(shader_runtime_name(&shader), shader_declares_property(&shader, "_rimV"));
        }
    }
    assert_eq!(found.get("Shader Forge/main_hair"), Some(&Some(true)));
    assert_eq!(found.get("Shader Forge/main_emblem"), Some(&Some(false)));
    assert_eq!(found.get("Shader Forge/toon_glasses_lod0"), Some(&Some(false)));
    let parsed = parse_bundle_cached(crate::gamefs::read(path).expect("shader bundle bytes"));
    let hair = parsed.material_named("mat_main_hair").expect("hair material");
    assert_eq!(hair.shader, "Shader Forge/main_hair");
    assert_eq!(hair.shader_has_rim, Some(true));
    let glasses = parsed.material_named("mat_toon_glasses_lod0").expect("glasses material");
    assert_eq!(glasses.shader_has_rim, Some(false));
}
#[test]
fn declared_sparse_uv_channels_do_not_shift_or_guess_data() {
    use unity_asset_decode::mesh::{Mesh,VertexData,ChannelInfo};
    let mut mesh=Mesh::default();
    let mut channels=vec![ChannelInfo::default();8];
    // Missing UV1 must not make UV2 its replacement. UV3 can be float4;
    // values outside [0,1] and tiny values are both valid authored UVs.
    channels[4]=ChannelInfo{stream:0,offset:0,format:0,dimension:2};
    channels[6]=ChannelInfo{stream:0,offset:8,format:0,dimension:2};
    channels[7]=ChannelInfo{stream:0,offset:16,format:0,dimension:4};
    let mut data=Vec::new();
    for vertex in [[0.2f32,0.3,2.,-3.,0.0004,0.0003,99.,88.],
                   [0.4,0.5,4.,-5.,0.0005,0.0006,77.,66.]] {
        for value in vertex {data.extend_from_slice(&value.to_le_bytes());}
    }
    mesh.vertex_data=VertexData{vertex_count:2,channels,data_size:data};
    assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("5.6.2f1").unwrap()),3);
    assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("2017.4.1f1").unwrap()),3);
    assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("2018.1.0f1").unwrap()),4);
    assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("2019.4.9f1").unwrap()),4);
    assert!(decode_texcoord(&mesh,1,0).is_empty());
    assert_eq!(decode_texcoord(&mesh,2,0),vec![[2.,-3.],[4.,-5.]]);
    assert_eq!(decode_texcoord(&mesh,3,0),vec![[0.0004,0.0003],[0.0005,0.0006]]);
}

#[test]
#[ignore="installed source mesh attributes for fresh native controls"]
fn skinplus_source_declared_vertex_attributes() {
    let game=&crate::test_support::game_path("");
    let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_380982.png")).unwrap();
    let (body,bytes)=crate::mods::uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex).unwrap();
    let bundle=parse_bundle_cached(bytes);
    let (meshes,_)=bundle.meshes_for_maindata(&body.asset);
    let body=meshes.iter().find(|m|m.name=="o_body_a").unwrap();
    let vanilla=crate::load_characustom_dir(&game.join("abdata/list/characustom"));
    let index=crate::mods::find_guids(&game.join("mods"),None);
    let mut zip_lists=std::collections::HashMap::new();
    let entry=crate::resolve_face_entry(&vanilla,&card,&index,&mut zip_lists).unwrap();
    let head_bytes=crate::accessory_bundle_bytes(game,entry.extra.get("__zip").map(|s|Path::new(s)),&entry).unwrap();
    let head=parse_bundle_cached(head_bytes);
    eprintln!("SOURCE_HEAD id={} ab={} main={}",card.head_id,entry.main_ab,entry.info("MainData"));
    head.with_serialized(|files| {for file in files {for info in file.objects_of_type(MESH_CLASS_ID) {
        let object=UnityObject::from_serialized_file(file,info).unwrap();
        if object.name().unwrap_or_default()=="cf_O_face" {eprintln!("RAW_FACE_CHANNELS version={} channels={:?}",file.unity_version,object.class.properties().get("m_VertexData").and_then(|v|match v {unity_asset_core::UnityValue::Object(f)=>f.get("m_Channels"),_=>None}));}
    }}});
    let (meshes,_)=head.meshes_for_maindata(&entry.info("MainData"));
    let face=meshes.iter().find(|m|m.name=="cf_O_face").unwrap();
    let rows:Vec<_>=[body,face].into_iter().map(|m|serde_json::json!({"mesh":m.name,
        "uv0":m.uvs,"uv1":m.uv2,"uv2":m.uv3,"uv3":m.uv4,"colors32":m.vertex_colors})).collect();
    std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Skin328SourceAttributes.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    assert_eq!(body.uv4.len(),body.positions.len());
    assert!(body.uv4.iter().flatten().all(|v|v.is_finite()));
}

#[test]
fn mesh_float16_uv_streams_preserve_authored_coordinates() {
    use unity_asset_decode::mesh::{Mesh,VertexData,ChannelInfo};
    // Two packed UV sets, with the following stream aligned to 16 bytes.
    let mut mesh=Mesh::default();
    let mut channels=vec![ChannelInfo::default();6];
    channels[4]=ChannelInfo {stream:0,offset:0,format:1,dimension:2};
    channels[5]=ChannelInfo {stream:1,offset:0,format:1,dimension:2};
    let mut bytes=Vec::new();
    for pair in [[0x3400u16,0x3a00],[0x3c00,0xbc00],[0,0x3800]] {for value in pair {bytes.extend_from_slice(&value.to_le_bytes());}}
    bytes.resize(16,0);
    for pair in [[0x3800u16,0x3000],[0x3c00,0x4000],[0x3800,0x3800]] {for value in pair {bytes.extend_from_slice(&value.to_le_bytes());}}
    mesh.vertex_data=VertexData {vertex_count:3,channels,data_size:bytes};
    assert_eq!(super::decode_uvs(&mesh),vec![[0.25,0.75],[1.,-1.],[0.,0.5]]);
    assert_eq!(super::decode_texcoord(&mesh,1,0),vec![[0.5,0.125],[1.,2.],[0.5,0.5]]);
    assert_eq!(super::read_vertex_component(&[0,128],0,5),-1.);
    assert_eq!(super::read_vertex_component(&[128],0,3),-1.);
}
#[test]
fn unity_dxt1_preserves_transparent_selector_and_opaque_four_colors() {
    let transparent = [0,0,255,255,255,255,255,255];
    let image = super::decode_unity_dxt1(&transparent,4,4).unwrap();
    assert!(image.pixels().all(|p|p.0==[0,0,0,0]));
    // endpoint0 > endpoint1 retains selector 3 as an opaque interpolation.
    let opaque = [0,248,31,0,255,255,255,255];
    let image=super::decode_unity_dxt1(&opaque,4,4).unwrap();
    assert!(image.pixels().all(|p|p.0[3]==255 && p.0[0]>p.0[2])); // decoder BGRA
    assert!(super::decode_unity_dxt1(&opaque[..7],4,4).is_none());
}
#[test]
fn oversized_texture_payload_uses_declared_array_after_long_name() {
    use unity_asset_decode::texture::{Texture2D,TextureFormat};
    let name="長貼圖名".repeat(70);
    let image=vec![0x5a;1024];
    let mut raw=(name.len() as u32).to_le_bytes().to_vec();
    raw.extend_from_slice(name.as_bytes());while raw.len()%4!=0{raw.push(0);}
    raw.extend_from_slice(&[0;76]);
    raw.extend_from_slice(&(image.len() as u32).to_le_bytes());
    raw.extend_from_slice(&image);raw.extend_from_slice(&[0;12]);
    let mut tex=Texture2D::new(name,32,32,TextureFormat::DXT5);
    tex.image_data=vec![0;32];tex.image_data.extend_from_slice(&image);
    repair_inline_texture_payload(&raw,&mut tex,1024);
    assert_eq!(tex.image_data,image);
    assert_eq!(tex.data_size,1024);
    // A streamed object has no declared inline array to replace its data.
    repair_inline_texture_payload(&raw[..80],&mut tex,1024);
    assert_eq!(tex.image_data,image);
}

#[test]
#[ignore="installed mod DXT5 payload / independent UnityPy image fixture"]
fn native_mod_dxt5_texture_payload() {
    use unity_asset_decode::texture::TextureProcessor;
    let fixture=std::path::Path::new("C:/portable/OpenKoikatsu/Saved/Tests/SkinHsv307");
    let zip=&crate::test_support::game_path("/mods/KKSImportedMods/付费作者系列含mod/SamCosmyson/mod/[[OMEGA]] HS2 port to KK v1.8.1.zipmod");
    let bytes=crate::mods::zipmod_bundle_named(zip,"chara/HS2-KK.unity3d").unwrap();
    let parsed=parse_bundle_ref(&bytes);
    let count=parsed.with_serialized(|files| {
        let mut count=0;
        for file in files {
            for info in file.objects_of_type(28) {
                let object=UnityObject::from_serialized_file(file,info).unwrap();
                let name=object.name().unwrap_or_default();
                if !["cf_shoes_sister_t","cf_shoes_sister_mc"].contains(&name.as_str()){continue;}
                count+=1;
                let mut tex=TextureProcessor::new(version()).convert_object(&object).unwrap();
                let expected=crate::gamefs::read(fixture.join(format!("{name}.dxT5"))).unwrap();
                eprintln!("DXT5 source name={name} format={:?} dimensions={}x{} bytes={} expected={} first={:?}",tex.format,tex.width,tex.height,tex.image_data.len(),expected.len(),&tex.image_data[..20.min(tex.image_data.len())]);
                std::fs::write(fixture.join(format!("{name}.raw.bin")),object.raw_data()).unwrap();
                repair_inline_texture_payload(object.raw_data(),&mut tex,expected.len());
                assert!(tex.image_data==expected,"original compressed bytes must be exact");
                let png=png_from_texture(&object,&parsed.files,None).unwrap().1;
                std::fs::write(fixture.join(format!("{name}.Rust.png")),&png).unwrap();
                let actual=crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
                let mut reference=image::open(fixture.join(format!("{name}.UnityPy.png"))).unwrap().to_rgba8();
                image::imageops::flip_vertical_in_place(&mut reference);
                assert_eq!(actual.dimensions(),reference.dimensions());
                let max=actual.as_raw().iter().zip(reference.as_raw()).map(|(a,b)|a.abs_diff(*b)).max().unwrap();
                eprintln!("DXT5 decoded name={name} pixels={} RGBA max={max}",actual.width()*actual.height());
                assert!(max<=1,"DXT5 decoded image disagrees with independent decoder");
            }
        }
        count
    });
    assert_eq!(count,2);
}

#[test]
fn short_parsed_texture_recovers_declared_inline_mip_chain() {
    let min_bytes = 1024 * 1024;
    let image = vec![0x5a; min_bytes + min_bytes / 3];
    let mut raw = vec![0; 76];
    raw[28..32].copy_from_slice(&(image.len() as u32).to_le_bytes());
    raw.extend_from_slice(&(image.len() as u32).to_le_bytes());
    raw.extend_from_slice(&image);
    raw.extend_from_slice(&[0; 12]);
    assert_eq!(super::recover_inline_texture_data(&raw, min_bytes), Some(image));
    assert!(super::recover_inline_texture_data(&raw[..76], min_bytes).is_none());
}
use super::*;

use std::path::PathBuf;
use unity_asset_core::UnityValue;

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn long_sleeve_mesh_does_not_invent_source_tangents() {
    let zip = crate::test_support::game_path(r"\mods\Sideloader Modpack\gyobobo\[gyobobo]Long Sleeve T-Shirt Set v1.1.zipmod");
    if !zip.ok_exists() { return; }
    let (_, bytes) = crate::mods::zipmod_one_unity3d(&zip, "chara/_mods/LongSleeveT.unity3d")
        .expect("LongSleeveT bundle");
    let meshes = meshes_from_memory(bytes);
    let inner = meshes.iter().find(|m| m.name == "o_inner_a").expect("o_inner_a");
    assert_eq!(inner.source_tangents, Some(false));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn pleated_skirt_keeps_authored_tangent_handedness() {
    let path = crate::test_support::game_path(r"\abdata\chara\co_bot_00.unity3d");
    if !path.ok_exists() { return; }
    let meshes = meshes_from_memory(crate::gamefs::read(path).expect("co_bot_00"));
    let skirt = meshes.iter().find(|m| m.name == "cf_bot_d_OpleatedSKF" && m.positions.len() == 924)
        .expect("924 vertex pleated skirt");
    assert_eq!(skirt.tangents.len(), skirt.positions.len());
    assert!(skirt.tangents.iter().all(|t| (t[3].abs() - 1.0).abs() < 0.01));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn sac_innie_body_keeps_game_authored_tangents() {
    let zip = crate::test_support::game_path(r"\mods\Sideloader Modpack - KKS_UncensorSelector\[SAC] Uncensor Selector Pack v0.9.2.zipmod");
    if !zip.ok_exists() { return; }
    let (_, bytes) = crate::mods::zipmod_one_unity3d(&zip, "chara/oo_base_sacinnie.unity3d")
        .expect("SAC innie body bundle");
    let meshes = meshes_from_memory(bytes);
    let body = meshes.iter().find(|m| m.name == "o_body_a" && m.positions.len() == 9048).unwrap();
    assert_eq!(body.source_tangents, Some(true));
    assert_eq!(body.tangents.len(), body.positions.len());
    let dump = crate::test_support::game_path(r"\mesh_dump\KK_091207.png\outfit0-tpose\Ushiwakamaru_0\mesh_47_o_body_a.json");
    if !dump.ok_exists() { return; }
    let game: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
    let theirs = game["mesh"]["tangents"].as_array().unwrap();
    assert_eq!(body.tangents.len(), theirs.len());
    let mut sum = 0.0f64;
    let mut max = 0.0f64;
    let mut handedness_mismatch = 0usize;
    for (ours, theirs) in body.tangents.iter().zip(theirs) {
        let game = ["x", "y", "z"].map(|k| theirs[k].as_f64().unwrap() as f32);
        let dot = (ours[0] * game[0] + ours[1] * game[1] + ours[2] * game[2]).clamp(-1.0, 1.0);
        let deg = dot.acos().to_degrees() as f64;
        sum += deg;
        max = max.max(deg);
        if ours[3].signum() != (theirs["w"].as_f64().unwrap() as f32).signum() { handedness_mismatch += 1; }
    }
    let mean = sum / body.tangents.len() as f64;
    eprintln!("SAC tangent vs game mean={mean:.4} max={max:.4} degrees; handedness_mismatch={handedness_mismatch}");
    assert!(mean < 0.2 && max < 1.0 && handedness_mismatch == 0);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn four_vertex_accessory_prefab_is_not_discarded() {
    let Some(idx) = crate::sqlcache::guid_index(&crate::test_support::game_path(r"\mods")) else { return };
    let Some(zip) = idx.get("yamadamod.accprim") else { return };
    let (_, bytes) = crate::mods::zipmod_one_unity3d(zip, "chara/ymd/mf_accprim.unity3d").expect("accprim bundle");
    let (meshes, _) = parse_bundle_ref(&bytes).meshes_for_maindata("p_koi_stu_kihon14c_00");
    assert_eq!(meshes.len(), 1);
    assert_eq!(meshes[0].name, "O_nosetama");
    assert_eq!(meshes[0].positions.len(), 4);
    assert_eq!(meshes[0].indices.len(), 6);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn jacket_emblem_renderer_is_available_to_runtime_loader() {
    let path = crate::test_support::game_path(r"\abdata\chara\cpo_jacket_b_00.unity3d");
    if !path.ok_exists() { return; }
    let bytes = crate::gamefs::read(path).expect("jacket bundle");
    let parsed = parse_bundle_ref(&bytes);
    let (active, _) = parsed.meshes_for_maindata("p_o_top_jacket01");
    let (all, _) = parsed.meshes_for_maindata_including_disabled("p_o_top_jacket01");
    eprintln!("jacket active={:?} all={:?}", active.iter().map(|m| (&m.name, &m.renderer_names)).collect::<Vec<_>>(), all.iter().map(|m| (&m.name, &m.renderer_names)).collect::<Vec<_>>());
    assert!(all.iter().any(|m| m.name == "o_emblem_01_a"));
}

#[test]
fn clothes_state_variants_follow_draw_clothes_slots() {
    let worn = [0u8; 9];
    assert_eq!(clothes_state_node_visible("n_top_a", &worn, Some(105)), Some(true));
    assert_eq!(clothes_state_node_visible("n_top_b", &worn, Some(105)), Some(false));
    let mut loose = worn;
    loose[0] = 1;
    loose[1] = 1;
    loose[2] = 1;
    loose[3] = 2;
    assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(105)), Some(true));
    assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(106)), Some(true));
    assert_eq!(clothes_state_node_visible("o_bra_b", &loose, Some(107)), Some(true));
    assert_eq!(clothes_state_node_visible("n_uwb_c", &loose, Some(108)), Some(true));
    assert_eq!(clothes_state_node_visible("n_uwb_a", &loose, Some(108)), Some(false));
    assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(107)), Some(true));
    loose[2] = 0;
    assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(107)), Some(false));
    assert_eq!(clothes_state_node_visible("n_bot_c", &loose, Some(108)), Some(true));
    // top lower panel follows bottoms; bottom upper panel follows top;
    // bra lower panel follows shorts, but subparts follow top alone.
    assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(105)), Some(true));
    assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(106)), Some(true));
    assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(107)), Some(true));
    assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(212)), Some(true));
    loose[1] = 0;
    assert_eq!(clothes_state_node_visible("n_bot_a", &loose, Some(105)), Some(true));
    assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(105)), Some(false));
    loose[4] = 1;
    loose[5] = 2;
    loose[6] = 1;
    assert!(!clothes_kind_visible(4, &loose, 1));
    assert!(!clothes_kind_visible(5, &loose, 1));
    assert!(!clothes_kind_visible(6, &loose, 1));
    assert!(clothes_kind_visible(0, &loose, 1));
}

#[test]
fn footwear_mode_is_exclusive_and_respects_removed_state() {
    let mut states=[0;9];
    assert!(clothes_kind_visible(7,&states,0));
    assert!(!clothes_kind_visible(8,&states,0));
    assert!(!clothes_kind_visible(7,&states,1));
    assert!(clothes_kind_visible(8,&states,1));
    assert!(!clothes_kind_visible(7,&states,2));
    assert!(!clothes_kind_visible(8,&states,2));
    states[8]=1;
    assert!(!clothes_kind_visible(8,&states,1));
    states[7]=3;
    assert!(!clothes_kind_visible(7,&states,0));
    assert!(clothes_kind_visible(0,&states,1));
}

#[test]
fn material_property_lookup_prefers_exact_shader_name() {
    let mut mat = MeshMaterial::default();
    mat.colors.insert("_LineColor".into(), [0.5, 0.5, 0.5, 0.0]);
    mat.colors.insert("_linecolor".into(), [0.4, 0.4, 0.4, 0.25]);
    mat.floats.insert("_ShadowExtend".into(), 0.4);
    mat.floats.insert("_shadowextend".into(), 0.8);
    mat.tex_slots.push(("_linemask".into(), "lower".into()));
    mat.tex_slots.push(("_LineMask".into(), "exact".into()));
    assert_eq!(mat.color("_LineColor"), Some([0.5, 0.5, 0.5, 0.0]));
    assert_eq!(mat.float("_ShadowExtend"), Some(0.4));
    assert_eq!(mat.slot("_LineMask"), Some("exact"));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn mm_base_body_draw_material_is_resolved_independently_of_body_mesh() {
    let path = crate::test_support::game_path(r"\abdata\chara\mm_base.unity3d");
    if !path.ok_exists() { return; }
    let parsed = parse_bundle_cached(crate::gamefs::read(path).unwrap());
    let mat = parsed.material_named("cf_m_body").expect("body draw material");
    eprintln!("body draw shader={} shadow={:?} detail={:?} line={:?} normal={:?}",
        mat.shader, mat.float("_ShadowExtend"), mat.slot("_DetailMask"),
        mat.slot("_LineMask"), mat.slot("_NormalMap"));
    assert!(mat.name.eq_ignore_ascii_case("cf_m_body"));
    assert_eq!(mat.float("_ShadowExtend"), Some(0.66));
    assert_eq!(mat.float("_linetexon"), Some(1.0));
    // Independently checked against this asset's UnityPy m_SavedProperties;
    // these values are hidden in the authored shader but survive SetShader.
    assert_eq!(mat.color("_EmissionColor"), Some([0.,0.,0.,1.]));
    assert_eq!(mat.color("_LiquidTiling"), Some([0.,0.,3.5,3.5]));
    assert_eq!(mat.color("_ShadowColor"), Some([1.,0.8963008,0.83599997,1.]));
    let card = crate::test_support::game_path(r"\UserData\chara\female\[Card] March_7th_keiyuu.png");
    if card.ok_exists() {
        let info = crate::card::parse_card_outfit(&card, 0).expect("March card");
        assert!(!info.body_draw_add_line, "VisibleAddBodyLine should disable this card's body lines");
    }
    for slot in ["_DetailMask", "_LineMask", "_NormalMap"] {
        let tex = mat.slot(slot).and_then(|name| parsed.texture_png_named(name));
        assert!(tex.as_ref().is_some_and(|png| png.len() > 16), "{slot}");
    }
}

#[test]
fn material_editor_can_select_detached_prefab_renderer() {
    let mut graph = PrefabGraph::default();
    graph.go_name.insert(0, "f_11R".into());
    graph.go_name.insert(1, "f_11R".into());
    graph.go_name.insert(2, "AS01_O_kami_07F".into());
    graph.go_name.insert(3, "AS01_O_kami_07F_0".into());
    graph.go_tf.insert(0, 5);
    graph.go_tf.insert(1, 10);
    graph.go_tf.insert(2, 20);
    graph.go_tf.insert(3, 30);
    graph.tf_children.insert(10, vec![20]);
    graph.tf_go.insert(20, 2);
    graph.tf_go.insert(30, 3);
    graph.tf_parent.insert(30, 20);
    graph.smr_go.insert(100, 3);
    graph.smr_mesh.insert(100, 200);
    graph.comp_enabled.insert(100, false);
    assert!(query_prefab_graph(&graph, "f_11R", false).0.is_empty());
    let (meshes, _, names) = query_prefab_graph(&graph, "f_11R", true);
    assert_eq!(meshes, vec![200]);
    assert_eq!(names[&200], vec!["AS01_O_kami_07F_0"]);
}

#[test]
fn cached_mesh_without_obj_remains_selectable() {
    let source = vec![ExtractedMesh {
        name: "cf_O_face".into(),
        obj: "v 0 0 0\n".into(),
        positions: vec![[0.0, 0.0, 0.0]],
        indices: vec![0, 0, 0],
        ..Default::default()
    }];
    let cached: Vec<ExtractedMesh> = bincode::deserialize(&bincode::serialize(&source).unwrap()).unwrap();
    assert!(cached[0].obj.is_empty());
    assert_eq!(pick_named(&cached, "cf_O_face").unwrap().indices.len(), 3);
}

fn png_px(r: u8, g: u8, b: u8, a: u8) -> Vec<u8> {
    let img = image::RgbaImage::from_pixel(1, 1, image::Rgba([r, g, b, a]));
    let mut buf = Vec::new();
    image::DynamicImage::ImageRgba8(img)
        .write_to(&mut std::io::Cursor::new(&mut buf), image::ImageFormat::Png)
        .unwrap();
    buf
}

#[test]
fn hair_get_diffuse_mask_r_multiplies_color() {
    let main = png_px(128, 128, 128, 255);
    let mask = png_px(255, 0, 0, 255);
    let out = hair_get_diffuse(
        &main,
        Some(&mask),
        [1.0, 0.0, 0.0, 1.0],
        [0.0, 1.0, 0.0, 1.0],
        [0.0, 0.0, 1.0, 1.0],
    )
    .unwrap();
    let got = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
    let p = got.get_pixel(0, 0).0;
    assert!(p[0] > 100 && p[1] < 20 && p[2] < 20, "mask.r ??Color * main, got {p:?}");
}

#[test]
fn hair_get_diffuse_black_mask_keeps_maintex() {
    let main = png_px(128, 64, 32, 255);
    let mask = png_px(0, 0, 0, 255);
    let out = hair_get_diffuse(
        &main,
        Some(&mask),
        [0.2, 0.4, 0.8, 1.0],
        [1.0, 0.0, 0.0, 1.0],
        [0.0, 1.0, 0.0, 1.0],
    )
    .unwrap();
    let got = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
    let p = got.get_pixel(0, 0).0;
    assert_eq!(p, [128, 64, 32, 255], "black ColorMask ??GetDiffuse=1");
}

fn png_mean_rgb(png: &[u8]) -> (u32, u32, u32, u32) {
    let img = crate::teximg::load_from_memory(png).unwrap().to_rgba8();
    let mut r = 0u64;
    let mut g = 0u64;
    let mut b = 0u64;
    let mut n = 0u64;
    for p in img.pixels() {
        if p[3] < 8 {
            continue;
        }
        if p[0] < 8 && p[1] < 8 && p[2] < 8 {
            continue;
        }
        r += p[0] as u64;
        g += p[1] as u64;
        b += p[2] as u64;
        n += 1;
    }
    if n == 0 {
        return (0, 0, 0, 0);
    }
    (
        (r / n) as u32,
        (g / n) as u32,
        (b / n) as u32,
        n as u32,
    )
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn named_png_prefers_eri_over_kage() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod"),
    );
    if !zip.ok_is_file() {
        return;
    }
    let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(&zip, "chara/gf_eyeline_up.unity3d")
    else {
        panic!("gf_eyeline_up missing");
    };
    let (n, png) = named_png_from_bytes(bytes, &["eri"]).expect("eri");
    let ln = n.to_ascii_lowercase();
    assert!(
        !ln.contains("kage") && !ln.contains("shadow"),
        "EyelineUpTex must not bind the shadow copy, got {n}"
    );
    assert!(
        ln == "eri" || ln == "eri_low",
        "expected eri / eri_low, got {n}"
    );
    let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
    let a_max = img.pixels().map(|p| p[3]).max().unwrap_or(0);
    assert!(a_max > 200, "eri must have opaque stroke pixels");
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn nicoichi_front_hair_has_color_mask() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\yamadamod\[yamadamod]hairNicoichi rev4.zipmod"),
    );
    if !zip.ok_is_file() {
        return;
    }
    let Some((_, bytes)) =
        crate::mods::zipmod_one_unity3d(&zip, "chara/ymd/mf_hairNicoichi.unity3d")
    else {
        panic!("nicoichi unity3d missing");
    };
    let (meshes, _) = meshes_skel_for_maindata_ref(&bytes, "p_cf_hair_f_04kai");
    assert!(!meshes.is_empty(), "front hair prefab empty");
    assert!(meshes.iter().any(|m| m.uv2.len() == m.positions.len()
        && m.uv2.iter().any(|uv| uv[0].abs() > 0.001 || uv[1].abs() > 0.001)),
        "hair gloss requires the prefab's authored second UV channel");
    let with_mask = meshes.iter().filter(|m| m.color_mask_png.is_some()).count();
    for m in &meshes {
        let main = m
            .texture_png
            .as_ref()
            .map(|p| png_mean_rgb(p))
            .unwrap_or((0, 0, 0, 0));
        let mask = m
            .color_mask_png
            .as_ref()
            .map(|p| png_mean_rgb(p))
            .unwrap_or((0, 0, 0, 0));
        eprintln!(
            "HAIRTEX {} tex={} main_len={} mask_len={} main_rgb={:?} mask_rgb={:?}",
            m.name,
            m.texture_name,
            m.texture_png.as_ref().map(|p| p.len()).unwrap_or(0),
            m.color_mask_png.as_ref().map(|p| p.len()).unwrap_or(0),
            main,
            mask
        );
    }
    assert!(
        with_mask == meshes.len(),
        "every hair SMR must bind _ColorMask, got {with_mask}/{}",
        meshes.len()
    );
    for m in &meshes {
        let tn = m.texture_name.to_ascii_lowercase();
        assert!(
            !tn.ends_with("_mc") && !tn.ends_with("_md"),
            "{} MainTex must not be ColorMask/DetailMask {}",
            m.name,
            m.texture_name
        );
        let main = m.texture_png.as_ref().map(|p| png_mean_rgb(p)).unwrap();
        assert!(
            main.1 <= main.0.saturating_add(40) || main.0 >= 200,
            "{} MainTex must not be the green DetailMask, mean={main:?}",
            m.name
        );
        let tinted = hair_get_diffuse(
            m.texture_png.as_deref().unwrap(),
            m.color_mask_png.as_deref(),
            [0.40, 0.41, 0.43, 1.0],
            [0.40, 0.41, 0.43, 1.0],
            [0.40, 0.41, 0.43, 1.0],
        )
        .unwrap();
        let d = png_mean_rgb(&tinted);
        assert!(
            d.1 <= d.0.saturating_add(25) && d.1 <= d.2.saturating_add(25),
            "{} GetDiffuse must follow hair color, not MainTex chroma, got {d:?}",
            m.name
        );
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn second_prefab_from_same_bundle_hits_cache() {
    let path = crate::test_support::game_path(r"\abdata\chara\co_shoes_00.unity3d");
    if !path.ok_is_file() {
        return;
    }
    let bytes = crate::gamefs::read(&path).unwrap();
    clear_bundle_cache();
    let t0 = std::time::Instant::now();
    let (m1, _) = meshes_skel_for_maindata(bytes.clone(), "p_o_shoes_summer");
    let first = t0.elapsed();
    assert!(!m1.is_empty(), "shoes prefab empty");
    let cached = parsed_if_cached(&bytes).expect("first decode must populate this thread's bundle cache");
    let t1 = std::time::Instant::now();
    let (m2, _) = meshes_skel_for_maindata(bytes.clone(), "p_o_shoes_summer");
    let second = t1.elapsed();
    assert_eq!(m1.len(), m2.len());
    eprintln!(
        "ok-extract: bundle cache first={:.1}ms second={:.1}ms n={}",
        first.as_secs_f64() * 1000.0,
        second.as_secs_f64() * 1000.0,
        m1.len()
    );
    // Wall-clock thresholds are benchmarks, not cache correctness: other
    // installed fixture tests can saturate CPU and memory concurrently.
    assert!(Arc::ptr_eq(&cached, &parsed_if_cached(&bytes).expect("cached second decode")));
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn diag_head_uv() {
    let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    let bundle = load_bundle(&path).expect("bundle");
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
            let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
            if mesh.name != "cf_O_face" {
                continue;
            }
            let n = mesh.vertex_data.vertex_count as usize;
            let (strides, offs) = channel_layout(&mesh);
            eprintln!("DIAGHU face verts={n} strides={strides:?} offs={offs:?}");
            for (i, c) in mesh.vertex_data.channels.iter().enumerate() {
                eprintln!("DIAGHU ch{i} stream={} offset={} fmt={} dim={}", c.stream, c.offset, c.format, c.dimension);
            }
            let data = &mesh.vertex_data.data_size;
            for ci in [4usize, 5, 6, 7] {
                let Some(c) = mesh.vertex_data.channels.get(ci) else { continue };
                if c.dimension < 2 {
                    continue;
                }
                let stride = strides[c.stream as usize];
                let base = offs[c.stream as usize];
                let mut good = 0usize;
                let mut first = Vec::new();
                for i in 0..n {
                    let o = base + i * stride + c.offset as usize;
                    let u = read_f32(data, o);
                    let v = read_f32(data, o + 4);
                    if u.is_finite() && v.is_finite() && u.abs() < 3.0 && v.abs() < 3.0 {
                        good += 1;
                    }
                    if i < 3 {
                        first.push([u, v]);
                    }
                }
                eprintln!("DIAGHU ch{ci} good={good}/{n} first={first:?}");
            }
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn diag_body_uv() {
    let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    let bundle = load_bundle(&path).expect("bundle");
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
            let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
            if mesh.name != "o_body_a" || mesh.vertex_data.vertex_count != 6823 {
                continue;
            }
            let n = mesh.vertex_data.vertex_count as usize;
            let (strides, offs) = channel_layout(&mesh);
            eprintln!("DIAGUV strides={strides:?} offs={offs:?}");
            for (i, c) in mesh.vertex_data.channels.iter().enumerate() {
                eprintln!("DIAGUV ch{i} stream={} offset={} fmt={} dim={}", c.stream, c.offset, c.format, c.dimension);
            }
            let data = &mesh.vertex_data.data_size;
            let start = offs[1];
            let hex: String = data[start..(start + 60).min(data.len())]
                .iter().map(|b| format!("{:02x}", b)).collect();
            eprintln!("DIAGUV stream1 v0.. {hex}");
            let uvs = decode_uvs(&mesh);
            eprintln!("DIAGUV uvs first4={:?}", &uvs[..4]);
            let pos = decode_positions(&mesh);
            eprintln!("DIAGUV pos first2={:?}", &pos[..2]);
            let _ = n;
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn diag_eye_uv() {
    let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    let bundle = load_bundle(&path).expect("bundle");
    let mut printed = 0;
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
            let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
            if mesh.name != "cf_Ohitomi_L" {
                continue;
            }
            printed += 1;
            let n = mesh.vertex_data.vertex_count as usize;
            let (strides, offs) = channel_layout(&mesh);
            eprintln!("EYEUV strides={strides:?} offs={offs:?} n={n}");
            for (i, c) in mesh.vertex_data.channels.iter().enumerate() {
                eprintln!(
                    "EYEUV ch{i} stream={} offset={} fmt={} dim={}",
                    c.stream, c.offset, c.format, c.dimension
                );
            }
            let data = &mesh.vertex_data.data_size;
            for ci in [4usize, 5, 6, 7] {
                let Some(c) = mesh.vertex_data.channels.get(ci) else { continue };
                if c.dimension < 2 {
                    continue;
                }
                let stride = strides[c.stream as usize];
                let base = offs[c.stream as usize];
                if stride == 0 || base + n * stride > data.len() {
                    continue;
                }
                let off = c.offset as usize;
                let mut first = Vec::new();
                for i in 0..4 {
                    let o = base + i * stride + off;
                    first.push((read_f32(data, o), read_f32(data, o + 4)));
                }
                eprintln!("EYEUV ch{ci} first4={first:?}");
            }
        }
    }
    eprintln!("EYEUV variants printed={printed}");
}

fn dump_tex_stats(label: &str, png: &[u8], out_dir: &std::path::Path) {
    let img = crate::teximg::load_from_memory(png).unwrap().to_rgba8();
    let (w, h) = img.dimensions();
    let mut amin = 255u8;
    let mut amax = 0u8;
    let mut r = 0u64;
    let mut g = 0u64;
    let mut b = 0u64;
    let mut a = 0u64;
    let n = (w as u64 * h as u64).max(1);
    for p in img.pixels() {
        amin = amin.min(p[3]);
        amax = amax.max(p[3]);
        r += p[0] as u64;
        g += p[1] as u64;
        b += p[2] as u64;
        a += p[3] as u64;
    }
    eprintln!(
        "FACERAW {label} {w}x{h} rgb=({},{},{}) a_mean={} a_range=[{amin},{amax}]",
        r / n,
        g / n,
        b / n,
        a / n
    );
    let _ = std::fs::create_dir_all(out_dir);
    let _ = std::fs::write(out_dir.join(format!("{label}.png")), png);
}

#[test]
fn dump_card_face_raw_textures() {
    let out = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw");
    let zip_pairs: &[(&str, &str, &str, &str)] = &[
        (
            "mayuge",
            r"mods\Sideloader Modpack\moderchan\[moderchan]charamaker support MOD v1.1.zipmod",
            "chara/_kero/CMS_NT_00.unity3d",
            "cw_t_mayuge_011_vl",
        ),
        (
            "noseline",
            r"mods\Sideloader Modpack\moderchan\[moderchan]charamaker support MOD v1.1.zipmod",
            "chara/_kero/mt_nose_NT.unity3d",
            "cw_t_noseline_002",
        ),
        (
            "eyeline_up",
            r"mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod",
            "chara/gf_eyeline_up.unity3d",
            "eri",
        ),
        (
            "eyeline_down",
            r"mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod",
            "chara/gf_eyeline_down.unity3d",
            "motoko",
        ),
    ];
    for (label, zip, ab, name) in zip_pairs {
        let zp = crate::test_support::game_path(zip);
        if !zp.ok_is_file() {
            eprintln!("FACERAW missing zip {zip}");
            continue;
        }
        match crate::mods::zipmod_one_unity3d(&zp, ab) {
            Some((_, bytes)) => {
                let parsed = parse_bundle_ref(&bytes);
                parsed.ensure_tex_catalog();
                for (n, px) in parsed.tex_names.borrow().iter() {
                    if n.to_ascii_lowercase().contains(&name.to_ascii_lowercase())
                        || n.to_ascii_lowercase().contains("eri")
                        || n.to_ascii_lowercase().contains("motoko")
                        || n.to_ascii_lowercase().contains("mayuge")
                        || n.to_ascii_lowercase().contains("nose")
                    {
                        eprintln!("FACERAW catalog {label} {n} px={px}");
                    }
                }
                match named_png_from_bytes(bytes, &[name]) {
                    Some((n, b)) => dump_tex_stats(&format!("{label}_{n}"), &b, &out),
                    None => eprintln!("FACERAW fail tex {label} {name}"),
                }
            }
            None => eprintln!("FACERAW fail bundle {ab}"),
        }
    }
    for (label, path, name) in [
        (
            "sirome",
            r"abdata\chara\mt_eye_white_00.unity3d",
            "cw_t_sirome_kage_001",
        ),
        (
            "hitomi",
            r"abdata\chara\mt_eye_00.unity3d",
            "cw_t_hitomi_066",
        ),
    ] {
        match named_png_from_bundle(&crate::test_support::game_path(path), &[name]) {
            Some((n, b)) => dump_tex_stats(&format!("{label}_{n}"), &b, &out),
            None => eprintln!("FACERAW fail {label} {name}"),
        }
    }
}

#[test]
fn diag_eyebrow_decode() {
    let p = crate::test_support::game_path(r"\abdata\chara\mt_eyebrow_00.unity3d");
    match named_png_from_bundle(&p, &["cw_t_mayuge_010"]) {
        Some((n, b)) => eprintln!("DIAGEW named decoded {n} bytes={}", b.len()),
        None => eprintln!("DIAGEW named FAILED"),
    }
    match named_png_from_bundle(&p, &["cw_t_mayuge_200"]) {
        Some((n, b)) => eprintln!("DIAGEW named200 decoded {n} bytes={}", b.len()),
        None => eprintln!("DIAGEW named200 FAILED"),
    }
}

#[test]
fn dump_body_tex() {
    for p in [
        crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d"),
        crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d"),
    ] {
        let (_, _, texs) = load_base_bundle(&p);
        for (n, b) in texs {
            eprintln!("DIAGBT tex {n} bytes={}", b.len());
            let safe: String = n.chars().map(|c| if c.is_ascii_alphanumeric() || c=='.' || c=='_' {c} else {'_'}).collect();
            let out = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\br_{safe}");
            std::fs::write(&out, &b).ok();
        }
    }
}

fn uv_u32s(v: &unity_asset_core::UnityValue) -> Vec<u32> {
    match v {
        unity_asset_core::UnityValue::Array(a) => a.iter().flat_map(uv_u32s).collect(),
        unity_asset_core::UnityValue::Integer(i) => vec![*i as u32],
        unity_asset_core::UnityValue::Float(f) => vec![*f as u32],
        _ => Vec::new(),
    }
}

fn safe_stem(s: &str) -> String {
    let t: String = s
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '_' || c == '-' {
                c
            } else {
                '_'
            }
        })
        .collect();
    if t.is_empty() {
        "unnamed".into()
    } else {
        t
    }
}

#[test]
#[ignore="installed shader exporter; requires explicit fresh OK_DXBC_ROOT and OK_DXBC_BUNDLE"]
fn dump_game_dxbc() {
    let root = PathBuf::from(std::env::var("OK_DXBC_ROOT").expect("Set a fresh OK_DXBC_ROOT for shader export"));
    assert!(!root.ok_exists(),"Preserve previous shader exports; use a fresh output directory");
    std::fs::create_dir_all(&root).unwrap();
    let bundles = [std::env::var("OK_DXBC_BUNDLE").expect("Set OK_DXBC_BUNDLE to a shader-containing bundle")];
    let mut index = String::from("bundle\tshader\tblob\tsize\tdxbc\n");
    let mut names = Vec::new();
    let mut nblob = 0usize;
    for bp in bundles {
        let path = PathBuf::from(bp);
        let Ok(bundle) = load_bundle_path_unbounded(&path) else {
            continue;
        };
        let _ = bundle.data_checked();
        let bname = path.file_stem().unwrap().to_string_lossy().into_owned();
        let bdir = root.join(&bname);
        std::fs::create_dir_all(&bdir).ok();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in serialized.objects_of_type(48) {
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else {
                    continue;
                };
                let props = obj.class.properties();
                let tag = {
                    let from_parsed = props.get("m_ParsedForm").and_then(|v| match v {
                        unity_asset_core::UnityValue::Object(o) => o
                            .get("m_Name")
                            .and_then(|x| x.as_str())
                            .map(|s| s.to_string()),
                        _ => None,
                    });
                    from_parsed
                        .or_else(|| {
                            props
                                .get("m_Name")
                                .and_then(|v| v.as_str())
                                .map(|s| s.to_string())
                        })
                        .unwrap_or_default()
                };
                if std::env::var_os("OK_SHADER_PROPS").is_some() {
                    let _ = std::fs::write(root.join(format!("{}_shader_props.txt", safe_stem(&tag))), format!("{:#?}", props));
                }
                names.push(tag.clone());
                let Some(unity_asset_core::UnityValue::Bytes(comp)) =
                    props.get("compressedBlob")
                else {
                    eprintln!("DXBC {tag} no compressedBlob");
                    continue;
                };
                let blocks = decompress_shader_platforms(comp,
                    &shader_blob_lengths(props.get("offsets")),
                    &shader_blob_lengths(props.get("compressedLengths")),
                    &shader_blob_lengths(props.get("decompressedLengths")));
                let raw: Vec<u8> = blocks.iter().flat_map(|block| block.iter().copied()).collect();
                let sdir = bdir.join(safe_stem(&tag));
                std::fs::create_dir_all(&sdir).ok();
                std::fs::write(sdir.join("decompressed.bin"), &raw).ok();
                let blobs = dxbc_extract(&raw);
                eprintln!(
                    "DXBC shader={tag} compressed={} platforms={} decompressed={} dxbc={}",
                    comp.len(),
                    blocks.len(),
                    raw.len(),
                    blobs.len()
                );
                for (i, blob) in blobs.iter().enumerate() {
                    nblob += 1;
                    let dxbc = sdir.join(format!("{i:04}.dxbc"));
                    std::fs::write(&dxbc, blob).ok();
                    index.push_str(&format!(
                        "{bname}\t{tag}\t{i}\t{}\t{}\n",
                        blob.len(),
                        dxbc.display()
                    ));
                }
            }
        }
    }
    std::fs::write(root.join("index.tsv"), &index).unwrap();
    std::fs::write(root.join("shaders.txt"), names.join("\n")).ok();
    eprintln!("DXBC wrote {nblob} blobs shaders={} -> {}", names.len(), root.display());
    assert!(nblob > 0, "no DXBC programs in selected shader bundle");
}


#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn ibl_alpha_shader_platform_blocks_are_separate() {
    let zip = crate::test_support::game_path(r"\mods\Sideloader Modpack - KK_MaterialEditor\[nashi]IBL Shader v1.1.zipmod");
    if !zip.ok_exists() { return; }
    let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(&zip, "chara/IBLShader_alpha.unity3d") else {
        panic!("IBL alpha shader bundle missing from installed mod");
    };
    let bundle = load_bundle_unbounded(bytes).unwrap();
    let _ = bundle.data_checked();
    let mut found = false;
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue; };
        let Ok(serialized) = parse_serialized_file(fb) else { continue; };
        for info in serialized.objects_of_type(48) {
            let obj = UnityObject::from_serialized_file(&serialized, info).unwrap();
            assert_eq!(shader_runtime_name(&obj), "IBL_Shader_alpha");
            assert_eq!(shader_declares_property(&obj, "_rimV"), Some(true));
            let props = obj.class.properties();
            let Some(unity_asset_core::UnityValue::Bytes(comp)) = props.get("compressedBlob") else { continue; };
            let blocks = decompress_shader_platforms(comp,
                &shader_blob_lengths(props.get("offsets")),
                &shader_blob_lengths(props.get("compressedLengths")),
                &shader_blob_lengths(props.get("decompressedLengths")));
            let blobs: Vec<Vec<u8>> = blocks.iter().flat_map(|raw| dxbc_extract(raw)).collect();
            assert_eq!(blocks.len(), 2, "one LZ4 block per Unity graphics API");
            assert!(blobs.len() >= 15, "full IBL shader includes pixel and vertex variants: {}", blobs.len());
            found = true;
        }
    }
    assert!(found, "IBL alpha Shader object not found");
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_me_hair_dxbc() {
    let path = crate::test_support::game_path(r"\kks_decomp\plugins\KKS_Plugins\KKS_MaterialEditor\KK_Plugins.Resources.EC_Shaders.unity3d");
    let bundle = load_bundle_path_unbounded(&path).expect("ME shader bundle");
    let _ = bundle.data_checked();
    let mut dumped = 0usize;
    let filter = std::env::var("OK_ME_SHADER_FILTER").unwrap_or_else(|_| "main_hair".into());
    let out_root = PathBuf::from(std::env::var("OK_ME_DXBC_ROOT")
        .unwrap_or_else(|_| r"C:\portable\OpenKoikatsu\Saved\ShaderDxbc\EC_Shaders".into()));
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        for info in serialized.objects_of_type(48) {
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let props = obj.class.properties();
            let tag = props.get("m_ParsedForm").and_then(|v| match v {
                unity_asset_core::UnityValue::Object(o) => o.get("m_Name").and_then(|x| x.as_str()),
                _ => None,
            }).unwrap_or("unknown");
            if !tag.contains(&filter) { continue }
            let comp = match props.get("compressedBlob") {
                Some(unity_asset_core::UnityValue::Bytes(comp)) => comp,
                _ => { eprintln!("ME {tag}: no compressedBlob"); continue; }
            };
            let blocks = decompress_shader_platforms(comp,
                &shader_blob_lengths(props.get("offsets")),
                &shader_blob_lengths(props.get("compressedLengths")),
                &shader_blob_lengths(props.get("decompressedLengths")));
            let raw: Vec<u8> = blocks.iter().flat_map(|block| block.iter().copied()).collect();
            let blobs = dxbc_extract(&raw);
            eprintln!("ME {tag}: comp={} platforms={} raw={} dxbc={}", comp.len(), blocks.len(), raw.len(), blobs.len());
            let dir = out_root.join(safe_stem(tag));
            std::fs::create_dir_all(&dir).unwrap();
            for (i, blob) in blobs.iter().enumerate() { std::fs::write(dir.join(format!("{i:04}.dxbc")), blob).unwrap(); }
            dumped += blobs.len();
        }
    }
    if filter == "main_hair" {
        assert!(dumped > 0, "ME main_hair shaders had no DXBC");
    }
}


fn dxbc_extract(data: &[u8]) -> Vec<Vec<u8>> {
    let mag = b"DXBC";
    let mut out = Vec::new();
    let mut i = 0;
    while i + 28 <= data.len() {
        if &data[i..i + 4] == mag {
            let size = u32::from_le_bytes(data[i + 24..i + 28].try_into().unwrap()) as usize;
            if size >= 32 && size <= 8 * 1024 * 1024 && i + size <= data.len() {
                out.push(data[i..i + size].to_vec());
                i += size;
                continue;
            }
        }
        i += 1;
    }
    out
}

#[test]
#[ignore="historical installed Ushiwakamaru face dump and mod fixture required"]
fn eyeshadow_00f_alpha_at_dump_eye_uv() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
    );
    let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
    else {
        return;
    };
    let (_, png) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
    let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
    let (w, h) = img.dimensions();
    let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit4\Ushiwakamaru_0\mesh_22_cf_O_face.json"),
    )
    .unwrap();
    let uv_s = dump.split("\"uv\":[").nth(1).unwrap();
    let uv_s = uv_s.split(']').next().unwrap();
    let mut uvs = Vec::new();
    for cap in uv_s.split("},{") {
        let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
        let y = cap.split("\"y\":").nth(1).and_then(|s| s.split(|c| c == '}' || c == ',').next());
        if let (Some(x), Some(y)) = (x, y) {
            if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                uvs.push([u, v]);
            }
        }
    }
    let sample = |u: f32, v: f32| -> u8 {
        let x = ((u.clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
        let y = ((v.clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
        img.get_pixel(x, y).0[3]
    };
    let mut sum = [0u64; 4];
    let mut n = 0u64;
    for [u, v] in &uvs {
        if !((*v > 0.55 && *v < 0.82)
            && ((*u > 0.22 && *u < 0.43) || (*u > 0.57 && *u < 0.80)))
        {
            continue;
        }
        n += 1;
        sum[0] += sample(*u, *v) as u64;
        sum[1] += sample(*u, 1.0 - *v) as u64;
        sum[2] += sample(*u, *v * (h as f32 / w as f32)) as u64;
        sum[3] += sample(*u, 1.0 - *v * (h as f32 / w as f32)) as u64;
    }
    eprintln!(
        "ES00F {w}x{h} eye_n={n} meanA uv=({:.1}) 1-v=({:.1}) v*h/w=({:.1}) 1-v*h/w=({:.1})",
        sum[0] as f32 / n as f32,
        sum[1] as f32 / n as f32,
        sum[2] as f32 / n as f32,
        sum[3] as f32 / n as f32,
    );
    assert!(n > 10);
    assert!(
        sum[0] > sum[1],
        "00f blobs sit at PNG y=v*h; flip before overlaying the flipped face RT"
    );
    let uv4_s = dump.split("\"uv4\":[").nth(1).unwrap();
    let uv4_s = uv4_s.split(']').next().unwrap();
    let mut uv4s = Vec::new();
    for cap in uv4_s.split("},{") {
        let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
        let y = cap.split("\"y\":").nth(1).and_then(|s| s.split(|c| c == '}' || c == ',').next());
        if let (Some(x), Some(y)) = (x, y) {
            if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                uv4s.push([u, v]);
            }
        }
    }
    let fract = |t: f32| t - t.floor();
    let mut s4 = [0u64; 4];
    let mut mx = [0u8; 4];
    let mut n4 = 0u64;
    for ([u0, v0], [u4, v4]) in uvs.iter().zip(uv4s.iter()) {
        if !((*v0 > 0.55 && *v0 < 0.82)
            && ((*u0 > 0.22 && *u0 < 0.43) || (*u0 > 0.57 && *u0 < 0.80)))
        {
            continue;
        }
        n4 += 1;
        let a0 = sample(*u4, *v4);
        let a1 = sample(*u4, 1.0 - *v4);
        let a2 = sample(fract(*u4), fract(*v4));
        let a3 = sample(fract(*u4), 1.0 - fract(*v4));
        s4[0] += a0 as u64;
        s4[1] += a1 as u64;
        s4[2] += a2 as u64;
        s4[3] += a3 as u64;
        mx[0] = mx[0].max(a0);
        mx[1] = mx[1].max(a1);
        mx[2] = mx[2].max(a2);
        mx[3] = mx[3].max(a3);
    }
    eprintln!(
        "ES00F uv4 eye_n={n4} meanA clamp=({:.1}/{m0}) 1-v=({:.1}/{m1}) fract=({:.1}/{m2}) fract1-v=({:.1}/{m3})",
        s4[0] as f32 / n4 as f32,
        s4[1] as f32 / n4 as f32,
        s4[2] as f32 / n4 as f32,
        s4[3] as f32 / n4 as f32,
        m0 = mx[0],
        m1 = mx[1],
        m2 = mx[2],
        m3 = mx[3],
    );
    let _ = std::fs::write(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw\cw_t_eyeshadow_00f.png", &png);
}

#[test]
#[ignore="historical installed Ushiwakamaru face dump required"]
fn face_uv4_matches_dump_range() {
    let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    if !path.ok_is_file() {
        return;
    }
    let meshes = crate::unity::meshes_from_bundle_path(&path);
    let face = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        .expect("cf_O_face");
    let mut umin = f32::MAX;
    let mut umax = f32::MIN;
    let mut vmin = f32::MAX;
    let mut vmax = f32::MIN;
    for [u, v] in &face.uv4 {
        umin = umin.min(*u);
        umax = umax.max(*u);
        vmin = vmin.min(*v);
        vmax = vmax.max(*v);
    }
    eprintln!(
        "FACEUV4 n={} u=[{umin:.3},{umax:.3}] v=[{vmin:.3},{vmax:.3}]",
        face.uv4.len()
    );
    assert!(umin < 0.0 && umax > 1.2, "uv4 u {umin} {umax}");
    assert!(vmin < -1.0 && vmax > 1.5, "uv4 v {vmin} {vmax}");
    let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\mesh_24_cf_O_face.json"),
    )
    .unwrap();
    let parse = |key: &str| -> Vec<[f32; 2]> {
        let s = dump.split(&format!("\"{key}\":[")).nth(1).unwrap();
        let s = s.split(']').next().unwrap();
        let mut out = Vec::new();
        for cap in s.split("},{") {
            let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
            let y = cap
                .split("\"y\":")
                .nth(1)
                .and_then(|s| s.split(|c| c == '}' || c == ',').next());
            if let (Some(x), Some(y)) = (x, y) {
                if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                    out.push([u, v]);
                }
            }
        }
        out
    };
    let d0 = parse("uv");
    let d4 = parse("uv4");
    let n = face.uv4.len().min(d4.len()).min(face.uvs.len()).min(d0.len());
    let mut e0 = 0.0f32;
    let mut e4 = 0.0f32;
    let mut e4f = 0.0f32;
    for i in 0..n {
        e0 += (face.uvs[i][0] - d0[i][0]).abs() + (face.uvs[i][1] - d0[i][1]).abs();
        e4 += (face.uv4[i][0] - d4[i][0]).abs() + (face.uv4[i][1] - d4[i][1]).abs();
        e4f += (face.uv4[i][0] - d4[i][0]).abs() + (face.uv4[i][1] - (1.0 - d4[i][1])).abs();
    }
    eprintln!(
        "FACEUV vs dump n={n} meanAbs uv0={:.4} uv4={:.4} uv4_vs_1-v={:.4} our0={:?} dump0={:?} our4={:?} dump4={:?}",
        e0 / n as f32,
        e4 / n as f32,
        e4f / n as f32,
        face.uvs[0],
        d0[0],
        face.uv4[0],
        d4[0]
    );
    let z4 = face.uv4.iter().filter(|uv| uv[0] == 0.0 && uv[1] == 0.0).count();
    let mut e4m = 0.0f32;
    let mut e4mf = 0.0f32;
    let mut hits = 0u32;
    for i in 0..n.min(200) {
        let [u, v] = face.uvs[i];
        let mut best = f32::MAX;
        let mut bj = 0usize;
        for (j, d) in d0.iter().enumerate() {
            let e = (d[0] - u).abs() + (d[1] - v).abs();
            let f = (d[0] - u).abs() + (d[1] - (1.0 - v)).abs();
            let m = e.min(f);
            if m < best {
                best = m;
                bj = j;
            }
        }
        if best < 0.02 {
            hits += 1;
            e4m += (face.uv4[i][0] - d4[bj][0]).abs() + (face.uv4[i][1] - d4[bj][1]).abs();
            e4mf += (face.uv4[i][0] - d4[bj][0]).abs()
                + (face.uv4[i][1] - (1.0 - d4[bj][1])).abs();
        }
    }
    eprintln!(
        "FACEUV4 zeros={z4} matched_uv0={hits}/200 uv4_at_match={:.4} uv4_1-v={:.4}",
        e4m / hits.max(1) as f32,
        e4mf / hits.max(1) as f32
    );
}

#[test]
#[ignore="historical baked PNG and installed Ushiwakamaru face dump required"]
fn baked_face_has_eyeshadow_at_eye_uv() {
    let p = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_user_check\head.png");
    if !p.ok_is_file() {
        return;
    }
    let img = image::open(&p).unwrap().to_rgba8();
    let (w, h) = img.dimensions();
    let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit4\Ushiwakamaru_0\mesh_22_cf_O_face.json"),
    )
    .unwrap();
    let uv_s = dump.split("\"uv\":[").nth(1).unwrap();
    let uv_s = uv_s.split(']').next().unwrap();
    let mut eye = Vec::new();
    let mut cheek = Vec::new();
    for cap in uv_s.split("},{") {
        let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
        let y = cap.split("\"y\":").nth(1).and_then(|s| s.split(|c| c == '}' || c == ',').next());
        let (Some(x), Some(y)) = (x, y) else { continue };
        let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) else { continue };
        let is_eye = (v > 0.55 && v < 0.82)
            && ((u > 0.22 && u < 0.43) || (u > 0.57 && u < 0.80));
        let is_cheek = v > 0.35 && v < 0.50 && u > 0.40 && u < 0.60;
        if is_eye {
            eye.push([u, v]);
        }
        if is_cheek {
            cheek.push([u, v]);
        }
    }
    let sample = |u: f32, v: f32, flip: bool| -> [u32; 4] {
        let x = ((u.clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
        let yv = if flip { 1.0 - v } else { v };
        let y = ((yv.clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
        let p = img.get_pixel(x, y).0;
        [p[0] as u32, p[1] as u32, p[2] as u32, p[3] as u32]
    };
    let mean = |pts: &[[f32; 2]], flip: bool| -> [f32; 3] {
        let mut s = [0u64; 3];
        for [u, v] in pts {
            let p = sample(*u, *v, flip);
            s[0] += p[0] as u64;
            s[1] += p[1] as u64;
            s[2] += p[2] as u64;
        }
        let n = pts.len().max(1) as f32;
        [s[0] as f32 / n, s[1] as f32 / n, s[2] as f32 / n]
    };
    let e0 = mean(&eye, false);
    let e1 = mean(&eye, true);
    let c0 = mean(&cheek, false);
    let c1 = mean(&cheek, true);
    eprintln!(
        "FACE {w}x{h} eye_n={} cheek_n={} eye(v)={:?} eye(1-v)={:?} cheek(v)={:?} cheek(1-v)={:?}",
        eye.len(),
        cheek.len(),
        e0,
        e1,
        c0,
        c1
    );
    assert!(!eye.is_empty());
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_eyeshadow_00f_matches_dump_size() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
    );
    let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
    else {
        return;
    };
    let (n, b) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
    let img = crate::teximg::load_from_memory(&b).unwrap().to_rgba8();
    let (w, h) = img.dimensions();
    assert_eq!(n, "cw_t_eyeshadow_00f");
    assert_eq!((w, h), (512, 256), "dump _overtex3 is 512x256");
    let dir = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw");
    let _ = std::fs::create_dir_all(&dir);
    let _ = std::fs::write(dir.join("cw_t_eyeshadow_00f.png"), &b);
    let c = [0.529f32, 0.230, 0.230, 0.65];
    let mut tint = img.clone();
    for p in tint.pixels_mut() {
        p[0] = ((p[0] as f32) * c[0]).clamp(0.0, 255.0) as u8;
        p[1] = ((p[1] as f32) * c[1]).clamp(0.0, 255.0) as u8;
        p[2] = ((p[2] as f32) * c[2]).clamp(0.0, 255.0) as u8;
        p[3] = ((p[3] as f32) * c[3]).clamp(0.0, 255.0) as u8;
    }
    let mut tb = Vec::new();
    image::DynamicImage::ImageRgba8(tint)
        .write_to(&mut std::io::Cursor::new(&mut tb), image::ImageFormat::Png)
        .unwrap();
    let _ = std::fs::write(dir.join("cw_t_eyeshadow_00f_tinted.png"), &tb);
    let mut flip = img.clone();
    image::imageops::flip_vertical_in_place(&mut flip);
    let mut fb = Vec::new();
    image::DynamicImage::ImageRgba8(flip)
        .write_to(&mut std::io::Cursor::new(&mut fb), image::ImageFormat::Png)
        .unwrap();
    let _ = std::fs::write(dir.join("cw_t_eyeshadow_00f_flipv.png"), &fb);
    eprintln!("ES00F wrote {} raw/tinted/flipv", dir.display());
}

#[test]
#[ignore="historical installed Ushiwakamaru overlay dump required"]
fn dump_overlay_vc_matches_png_sample() {
    let json = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\mesh_24_cf_O_face.json"),
    )
    .unwrap();
    let png = crate::gamefs::read(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\tex_4_cf_m_face_00__overtex3.png"),
    )
    .unwrap();
    let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
    let (w, h) = img.dimensions();
    let parse2 = |key: &str| -> Vec<[f32; 2]> {
        let s = json.split(&format!("\"{key}\":[")).nth(1).unwrap();
        let s = s.split(']').next().unwrap();
        let mut out = Vec::new();
        for cap in s.split("},{") {
            let x = cap.split("\"x\":").nth(1).and_then(|t| t.split(',').next());
            let y = cap
                .split("\"y\":")
                .nth(1)
                .and_then(|t| t.split(|c| c == '}' || c == ',').next());
            if let (Some(x), Some(y)) = (x, y) {
                if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                    out.push([u, v]);
                }
            }
        }
        out
    };
    let uv4 = parse2("uv4");
    let col_s = json
        .split("\"overlayVertexColors\":")
        .nth(1)
        .unwrap()
        .split("\"colors\":[")
        .nth(1)
        .unwrap();
    let col_s = col_s.split(']').next().unwrap();
    let mut cols = Vec::new();
    for cap in col_s.split("},{") {
        let a = cap
            .split("\"a\":")
            .nth(1)
            .and_then(|t| t.split(|c| c == '}' || c == ',').next())
            .and_then(|t| t.parse::<f32>().ok());
        if let Some(a) = a {
            cols.push(a);
        }
    }
    let sample = |u: f32, v: f32, flip: bool| -> f32 {
        let uu = u.clamp(0.0, 1.0);
        let vv = if flip { 1.0 - v } else { v }.clamp(0.0, 1.0);
        let x = ((uu * (w - 1) as f32).round() as u32).min(w - 1);
        let y = ((vv * (h - 1) as f32).round() as u32).min(h - 1);
        img.get_pixel(x, y).0[3] as f32 / 255.0 * 0.65
    };
    let mut e0 = 0.0f32;
    let mut e1 = 0.0f32;
    let mut n = 0u32;
    let mut npos = 0u32;
    for i in 0..cols.len().min(uv4.len()) {
        let a = cols[i];
        if a <= 0.02 && sample(uv4[i][0], uv4[i][1], false) <= 0.02 && sample(uv4[i][0], uv4[i][1], true) <= 0.02
        {
            continue;
        }
        n += 1;
        if a > 0.02 {
            npos += 1;
        }
        e0 += (a - sample(uv4[i][0], uv4[i][1], false)).abs();
        e1 += (a - sample(uv4[i][0], uv4[i][1], true)).abs();
    }
    eprintln!(
        "OVC vs PNG n={n} dumpA>0.02={npos} meanAbs y=v={:.4} y=1-v={:.4} img={w}x{h}",
        e0 / n.max(1) as f32,
        e1 / n.max(1) as f32
    );
    assert!(npos > 50, "dump overlayVertexColors should stain lids");
    let m0 = e0 / n.max(1) as f32;
    let m1 = e1 / n.max(1) as f32;
    assert!(m1 < 0.02 && m1 < m0, "dump OVC is PNG y=1-v, got {m1} vs y=v {m0}");
    let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    if path.ok_is_file() {
        let meshes = crate::unity::meshes_from_bundle_path(&path);
        if let Some(face) = meshes.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")) {
            let d0 = parse2("uv");
            let mut e4 = 0.0f32;
            let mut hits = 0u32;
            let mut shown = 0u32;
            for i in 0..cols.len().min(d0.len()).min(uv4.len()) {
                if cols[i] <= 0.2 {
                    continue;
                }
                let [u, v] = d0[i];
                let mut best = f32::MAX;
                let mut bj = 0usize;
                for (j, ou) in face.uvs.iter().enumerate() {
                    let e = (ou[0] - u).abs() + (ou[1] - v).abs();
                    if e < best {
                        best = e;
                        bj = j;
                    }
                }
                if best < 0.005 {
                    hits += 1;
                    let de = (face.uv4[bj][0] - uv4[i][0]).abs() + (face.uv4[bj][1] - uv4[i][1]).abs();
                    e4 += de;
                    if shown < 6 {
                        shown += 1;
                        eprintln!(
                            "LID uv0=({:.3},{:.3}) dump4={:?} our4={:?} uv0err={:.4} uv4err={:.4}",
                            u, v, uv4[i], face.uv4[bj], best, de
                        );
                    }
                }
            }
            eprintln!(
                "LIDMATCH hits={hits} mean uv4 abs={:.4}",
                e4 / hits.max(1) as f32
            );
        }
    }
}

#[test]
#[ignore="historical installed Ushiwakamaru UV4 and overlay dump required"]
fn overtex3_uv4_clamp_hits_eye_not_cheek() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
    );
    let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
    else {
        return;
    };
    let (_, png) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
    let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
    let (w, h) = img.dimensions();
    let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit4\Ushiwakamaru_0\mesh_22_cf_O_face.json"),
    )
    .unwrap();
    let parse = |key: &str| -> Vec<[f32; 2]> {
        let s = dump.split(&format!("\"{key}\":[")).nth(1).unwrap();
        let s = s.split(']').next().unwrap();
        let mut out = Vec::new();
        for cap in s.split("},{") {
            let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
            let y = cap
                .split("\"y\":")
                .nth(1)
                .and_then(|s| s.split(|c| c == '}' || c == ',').next());
            if let (Some(x), Some(y)) = (x, y) {
                if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                    out.push([u, v]);
                }
            }
        }
        out
    };
    let uvs = parse("uv");
    let uv4s = parse("uv4");
    let sample = |u: f32, v: f32, flip: bool| -> u8 {
        let uu = u.clamp(0.0, 0.999);
        let vv = if flip { 1.0 - v } else { v }.clamp(0.0, 0.999);
        let x = ((uu * w as f32) as u32).min(w - 1);
        let y = ((vv * h as f32) as u32).min(h - 1);
        img.get_pixel(x, y).0[3]
    };
    let mean = |pred: fn(f32, f32) -> bool, flip: bool| -> (f32, u8, usize) {
        let mut s = 0u64;
        let mut mx = 0u8;
        let mut n = 0usize;
        for ([u0, v0], [u4, v4]) in uvs.iter().zip(uv4s.iter()) {
            if !pred(*u0, *v0) {
                continue;
            }
            let a = sample(*u4, *v4, flip);
            s += a as u64;
            mx = mx.max(a);
            n += 1;
        }
        (if n == 0 { 0.0 } else { s as f32 / n as f32 }, mx, n)
    };
    let eye = |u: f32, v: f32| {
        v > 0.55 && v < 0.82 && ((u > 0.22 && u < 0.43) || (u > 0.57 && u < 0.80))
    };
    let cheek = |u: f32, v: f32| {
        v > 0.30 && v < 0.52 && ((u > 0.15 && u < 0.40) || (u > 0.60 && u < 0.85))
    };
    let jaw = |u: f32, v: f32| v > 0.12 && v < 0.32 && u > 0.25 && u < 0.75;
    let temple = |u: f32, v: f32| {
        v > 0.70 && v < 0.90 && ((u > 0.05 && u < 0.25) || (u > 0.75 && u < 0.95))
    };
    for (name, pred) in [
        ("eye", eye as fn(f32, f32) -> bool),
        ("cheek", cheek),
        ("jaw", jaw),
        ("temple", temple),
    ] {
        let (a0, m0, n) = mean(pred, false);
        let (a1, m1, _) = mean(pred, true);
        eprintln!("{name} n={n} clamp_y=v meanA={a0:.1}/{m0}  y=1-v meanA={a1:.1}/{m1}");
    }
    let (eye0, _, n_eye) = mean(eye, false);
    let (eye1, _, _) = mean(eye, true);
    let (cheek0, _, n_cheek) = mean(cheek, false);
    let (cheek1, _, _) = mean(cheek, true);
    let (jaw1, _, _) = mean(jaw, true);
    assert!(n_eye > 10 && n_cheek > 10);
    assert!(
        cheek0 > 5.0,
        "sanity: PNG y=v*h clamps cheek uv4 onto 00f blobs (got {cheek0})"
    );
    assert!(eye1 > 40.0, "lid alpha PNG y=1-v={eye1}");
    assert!(
        cheek1 < 5.0,
        "cheek must miss 00f with Unity V (PNG y=1-v), got {cheek1}"
    );
    assert!(jaw1 < 5.0, "jaw must miss 00f with Unity V, got {jaw1}");
    let _ = eye0;
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn overlay_mapped_stains_lids_not_cheeks() {
    let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
    );
    let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
    else {
        return;
    };
    let (_, over) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
    let base_path = PathBuf::from(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\tex_2_cf_m_face_00__MainTex.png"),
    );
    if !base_path.ok_is_file() {
        return;
    }
    let base = crate::gamefs::read(&base_path).unwrap();
    let head = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    let meshes = crate::unity::meshes_from_bundle_path(&head);
    let face = meshes
        .iter()
        .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        .expect("cf_O_face");
    let out = overlay_overtex_mapped(
        &base,
        &over,
        [0.529, 0.230, 0.230, 0.65],
        &face.uvs,
        &face.uv4,
        &face.indices,
    )
    .expect("overlay");
    let dst = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
    let src = crate::teximg::load_from_memory(&base).unwrap().to_rgba8();
    let (w, h) = dst.dimensions();
    let pix = |img: &image::RgbaImage, u: f32, v: f32| -> [i32; 3] {
        let x = ((u.clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
        let y = (((1.0 - v).clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
        let p = img.get_pixel(x, y).0;
        [p[0] as i32, p[1] as i32, p[2] as i32]
    };
    let mut eye_d = 0.0f32;
    let mut cheek_d = 0.0f32;
    let mut n_eye = 0u32;
    let mut n_cheek = 0u32;
    for [u, v] in &face.uvs {
        let is_eye = *v > 0.55
            && *v < 0.82
            && ((*u > 0.22 && *u < 0.43) || (*u > 0.57 && *u < 0.80));
        let is_cheek = *v > 0.30
            && *v < 0.52
            && ((*u > 0.15 && *u < 0.40) || (*u > 0.60 && *u < 0.85));
        if !is_eye && !is_cheek {
            continue;
        }
        let a = pix(&src, *u, *v);
        let b = pix(&dst, *u, *v);
        let d = (a[0] - b[0]).abs() + (a[1] - b[1]).abs() + (a[2] - b[2]).abs();
        if is_eye {
            eye_d += d as f32;
            n_eye += 1;
        }
        if is_cheek {
            cheek_d += d as f32;
            n_cheek += 1;
        }
    }
    eye_d /= n_eye.max(1) as f32;
    cheek_d /= n_cheek.max(1) as f32;
    eprintln!("OVERLAY eye_d={eye_d:.1} n={n_eye} cheek_d={cheek_d:.1} n={n_cheek}");
    assert!(n_eye > 10 && n_cheek > 10);
    assert!(eye_d > 20.0, "lids must pick up 00f, eye_d={eye_d}");
    assert!(
        cheek_d * 5.0 < eye_d,
        "cheeks must stay far below lids (Unity Clamp + V), eye_d={eye_d} cheek_d={cheek_d}"
    );
}

#[test]
#[ignore]
fn diag_hair_f_118_renderer_enabled() {
    let path = PathBuf::from(
        r"C:\Users\Damody\AppData\Local\Temp\opencode\hairf50\abdata\chara\deathweasel\acc_bo_hair_f_50.unity3d",
    );
    if !path.ok_is_file() {
        eprintln!("skip missing {}", path.display());
        return;
    }
    let bundle = load_bundle(&path).expect("bundle");
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        let mut go_name = std::collections::HashMap::new();
        let mut go_active = std::collections::HashMap::new();
        for info in &serialized.objects {
            if info.type_id != 1 {
                continue;
            }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            go_name.insert(info.path_id, obj.name().unwrap_or_default());
            if let Some(v) = obj.class.properties().get("m_IsActive") {
                go_active.insert(info.path_id, uv_bool(v));
            }
        }
        let mut scripts = std::collections::HashMap::new();
        for info in &serialized.objects {
            if info.type_id != 115 {
                continue;
            }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            scripts.insert(info.path_id, obj.name().unwrap_or_default());
        }
        for info in &serialized.objects {
            if info.type_id != 114 {
                continue;
            }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let props = obj.class.properties();
            let class = props
                .get("m_Script")
                .and_then(pptr_path_id)
                .and_then(|id| scripts.get(&id))
                .cloned()
                .unwrap_or_default();
            if class.is_empty() {
                continue;
            }
            let keys: Vec<String> = props.keys().cloned().collect();
            eprintln!("MB class={class} keys={keys:?}");
        }
        for info in &serialized.objects {
            if info.type_id != 137 && info.type_id != 23 {
                continue;
            }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let props = obj.class.properties();
            let go = props.get("m_GameObject").and_then(pptr_path_id);
            let name = go.and_then(|id| go_name.get(&id)).cloned().unwrap_or_default();
            if !name.to_ascii_lowercase().contains("hair_f_118") && !name.to_ascii_lowercase().contains("hairfront") {
                continue;
            }
            let en = props.get("m_Enabled").map(uv_bool);
            let active = go.and_then(|id| go_active.get(&id).copied());
            let keys: Vec<&str> = props.keys().map(|s| s.as_str()).collect();
            eprintln!(
                "SMR type={} go={name} enabled={en:?} go_active={active:?} keys={keys:?}",
                info.type_id
            );
        }
    }
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_body_skeleton_and_bindpose() {
    let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    let bundle = load_bundle(&path).expect("bundle");
    let _ = bundle.data_checked();
    let mut lines = Vec::new();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        for info in serialized.objects_of_type(1) {
            if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                let n = obj.name().unwrap_or_default();
                if n.contains("cf_j_arm") || n.contains("cf_j_head") || n.contains("cf_j_foot") || n.contains("N_") || n.contains("n_hair") || n == "cf_n_height" || n.contains("cf_j_hips") {
                    lines.push(format!("GO {} {}", info.path_id, n));
                }
            }
        }
        for info in serialized.objects_of_type(4) {
            if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                let keys: Vec<_> = obj.class.properties().keys().cloned().collect();
                if lines.len() < 5 {
                    lines.push(format!("TFkeys={keys:?}"));
                }
                break;
            }
        }
        for info in serialized.objects_of_type(137) {
            if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                let bones = obj.class.properties().get("m_Bones").map(|v| v.to_string().chars().take(300).collect::<String>());
                let root = obj.class.properties().get("m_RootBone").map(|v| v.to_string().chars().take(80).collect::<String>());
                lines.push(format!("SMR {} bones={:?} root={:?}", info.path_id, bones, root));
            }
        }
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
            let mesh = match parse_mesh(&object, &version()) {
                Ok(m) => m,
                Err(_) => continue,
            };
            if mesh.name != "o_body_a" {
                continue;
            }
            lines.push(format!(
                "mesh {} bind={} hashes={} rootHash={}",
                mesh.name,
                mesh.bind_pose.len(),
                mesh.bone_name_hashes.len(),
                mesh.root_bone_name_hash
            ));
            if let Some(bp) = object.class.properties().get("m_BindPose") {
                lines.push(format!("bindkind={} preview={}", 
                    match bp { UnityValue::Array(_) => "arr", UnityValue::Object(_) => "obj", _ => "other" },
                    bp.to_string().chars().take(400).collect::<String>()));
            }
            if let Some(h) = object.class.properties().get("m_BoneNameHashes") {
                lines.push(format!("hashes={}", h.to_string().chars().take(200).collect::<String>()));
            }
        }
    }
    std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\skeleton.txt", lines.join("\n")).ok();
    eprintln!("{}", lines.join("\n"));
    assert!(!lines.is_empty());
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_avatar_tos_and_clip_curves() {
    let mut lines = Vec::new();
    for (label, p) in [
        ("pose", crate::test_support::game_path(r"\abdata\custom\custompose.unity3d")),
        ("body", crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d")),
    ] {
        let Ok(bundle) = load_bundle(&p) else { continue };
        let _ = bundle.data_checked();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in &serialized.objects {
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                let n = obj.name().unwrap_or_default();
                // 90 Avatar, 95 Animator
                if info.type_id == 90 || info.type_id == 95 {
                    let keys: Vec<_> = obj.class.properties().keys().cloned().take(20).collect();
                    lines.push(format!("{label} type{} {} keys={keys:?}", info.type_id, n));
                    if let Some(tos) = obj.class.properties().get("m_TOS") {
                        let s = tos.to_string();
                        lines.push(format!("  TOS len={} {}", s.len(), s.chars().take(2500).collect::<String>()));
                    }
                    if let Some(av) = obj.class.properties().get("m_Avatar") {
                        lines.push(format!("  m_Avatar={}", av.to_string().chars().take(200).collect::<String>()));
                    }
                }
                if info.type_id == 74 && n.eq_ignore_ascii_case("Fmannequin_00") {
                    if let Some(UnityValue::Object(mc)) = obj.class.properties().get("m_MuscleClip") {
                        if let Some(clip) = mc.get("m_Clip") {
                            if let UnityValue::Object(co) = clip {
                                let inner = co.get("data").unwrap_or(clip);
                                if let UnityValue::Object(d) = inner {
                                    if let Some(UnityValue::Object(sc)) = d.get("m_StreamedClip") {
                                        let curve = sc.get("curveCount").map(|v| v.to_string()).unwrap_or_default();
                                        let n = if let Some(UnityValue::Array(a)) = sc.get("data") { a.len() } else { 0 };
                                        lines.push(format!("streamed curveCount={curve} data_n={n}"));
                                    }
                                    if let Some(UnityValue::Object(dc)) = d.get("m_DenseClip") {
                                        lines.push(format!("dense keys={:?} preview={}", dc.keys().cloned().collect::<Vec<_>>(), dc.get("m_SampleArray").map(|v| v.to_string().chars().take(200).collect::<String>()).unwrap_or_default()));
                                    }
                                    if let Some(cc) = d.get("m_ConstantClip") {
                                        lines.push(format!("const preview={}", cc.to_string().chars().take(400).collect::<String>()));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\avatar_tos.txt", lines.join("\n")).ok();
    eprintln!("{}", lines.join("\n"));
    assert!(!lines.is_empty());
}

#[test]
fn dump_custom_pose_clips() {
    let path = crate::test_support::game_path(r"\abdata\custom\custompose.unity3d");
    if !path.ok_exists() {
        return;
    }
    let Ok(bundle) = load_bundle(&path) else {
        return;
    };
    let _ = bundle.data_checked();
    let mut lines = Vec::new();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        lines.push(format!("file {} objects={}", file.name, serialized.objects.len()));
        for info in &serialized.objects {
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let n = obj.name().unwrap_or_default();
            if info.type_id == 91 {
                if let Some(tos) = obj.class.properties().get("m_TOS") {
                    let s = tos.to_string();
                    lines.push(format!("TOS type91 name={n} len={} preview={}", s.len(), s.chars().take(4000).collect::<String>()));
                }
            }
            if info.type_id == 74 && n.eq_ignore_ascii_case("Fmannequin_00") {
                let keys: Vec<_> = obj.class.properties().keys().cloned().take(40).collect();
                lines.push(format!("id={} type={} name={} keys={keys:?}", info.path_id, info.type_id, n));
                if info.type_id == 74 {
                    for k in ["m_Name", "m_SampleRate", "m_Legacy", "m_AnimationType"] {
                        if let Some(v) = obj.class.properties().get(k) {
                            lines.push(format!("  {k}={}", v.to_string().chars().take(80).collect::<String>()));
                        }
                    }
                    if let Some(bind) = obj.class.properties().get("m_ClipBindingConstant") {
                        if let unity_asset_core::UnityValue::Object(o) = bind {
                            if let Some(unity_asset_core::UnityValue::Array(g)) = o.get("genericBindings") {
                                lines.push(format!("  bindings={}", g.len()));
                                if let Some(unity_asset_core::UnityValue::Array(pp)) = o.get("pptrCurveMapping") {
                                    lines.push(format!("  pptrCurveMapping={}", pp.len()));
                                }
                            }
                        }
                    }
                    if let Some(mc) = obj.class.properties().get("m_MuscleClip") {
                        if let unity_asset_core::UnityValue::Object(o) = mc {
                            lines.push(format!("  muscle_keys={:?}", o.keys().cloned().collect::<Vec<_>>()));
                            if let Some(clip) = o.get("m_Clip") {
                                let s = clip.to_string();
                                lines.push(format!("  m_Clip preview={}", s.chars().take(2000).collect::<String>()));
                            }
                        }
                    }
                }
            }
        }
    }
    std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\custom_pose.txt", lines.join("\n")).ok();
    eprintln!("{}", lines.join("\n"));
    assert!(!lines.is_empty());
}

#[test]
fn body_vertex_stream_offsets_match_unity_buffer() {
    let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    if !path.ok_exists() { return; }
    let bundle = unity_asset_binary::bundle::load_bundle(&path).expect("bundle");
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(bytes) else { continue };
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
            let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
            if mesh.name != "o_body_a" || mesh.vertex_data.vertex_count != 6823 { continue }
            let (strides, offsets) = channel_layout(&mesh);
            assert_eq!(strides, [40, 28, 32]);
            assert_eq!(offsets, [0, 272928, 463984]);
            assert_eq!(offsets[2] + strides[2] * 6823, mesh.vertex_data.data_size.len());
            return;
        }
    }
    panic!("reference body mesh missing");
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn body_obj_has_nonzero_vertices() {
    let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    let meshes = meshes_from_bundle_path(&path);
    let body = pick_named(&meshes, "o_body_a").expect("o_body_a");
    let has = body.obj.lines().any(|l| {
        if !l.starts_with("v ") {
            return false;
        }
        l.split_whitespace().skip(1).any(|n| n.parse::<f32>().ok().map(|f| f.abs() > 0.001).unwrap_or(false))
    });
    assert!(has, "body vertices still zero: {}", body.obj.lines().take(8).collect::<Vec<_>>().join("\n"));
    assert!(body.obj.contains("f "), "no faces");
    assert!(
        body.texture_png.as_ref().map(|p| p.len()).unwrap_or(0) > 64,
        "body texture missing name={}",
        body.texture_name
    );
}

// Studio's 色影 (p_koi_stu_marukage00_00) quad: Unity reads mesh.uv as 0..1 (native probe); uv2 is its lightmap atlas.
#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn studio_marukage_quad_keeps_uv0() {
    let path = crate::test_support::game_path(r"\abdata\studio\00.unity3d");
    let meshes = meshes_from_bundle_path(&path);
    let quad = pick_named(&meshes, "o_koi_stu_marukage00_00").expect("marukage mesh");
    eprintln!("uvs={:?}\nuv2={:?}", quad.uvs, quad.uv2);
    assert!(quad.uvs.iter().all(|uv| (uv[0] == 0.0 || uv[0] == 1.0) && (uv[1] == 0.0 || uv[1] == 1.0)), "uv0 {:?}", quad.uvs);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn head_texture_unpacked_from_red_atlas() {
    let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
    let meshes = meshes_from_bundle_path(&path);
    let head = pick_named(&meshes, "cf_O_face").expect("cf_O_face");
    let png = head.texture_png.as_ref().expect("head texture");
    let img = crate::teximg::load_from_memory(png).expect("png").to_rgba8();
    let mut rsum = 0u64;
    let mut gsum = 0u64;
    let mut n = 0u64;
    for p in img.pixels() {
        if p[0] < 16 && p[1] < 16 && p[2] < 16 {
            continue;
        }
        rsum += p[0] as u64;
        gsum += p[1] as u64;
        n += 1;
    }
    assert!(n > 0, "head tex empty");
    assert!(
        gsum * 2 > rsum,
        "head still packed red r={} g={}",
        rsum / n,
        gsum / n
    );
}

#[test]
fn unpack_red_packed_atlas() {
    let mut img = image::RgbaImage::new(8, 8);
    for (x, y, p) in img.enumerate_pixels_mut() {
        *p = image::Rgba([160 + 80 * ((x + y) % 2) as u8, 0, 0, 255]);
    }
    // Flat red artwork is deliberately rejected by the existing packed
    // atlas detector; it must not be recoloured by this legacy helper.
    let flat = solid_png([200.0/255.0, 0.0, 0.0, 1.0], 8, 8).unwrap();
    assert!(unpack_illusion_albedo(&flat).is_none());
    let mut png = Vec::new();
    image::DynamicImage::ImageRgba8(img)
        .write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png)
        .unwrap();
    let out = unpack_illusion_albedo(&png).expect("unpack");
    let decoded = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
    let p = decoded.get_pixel(0, 0);
    assert!(p[1] > 100, "g={}", p[1]);
    assert!(p[2] > 80, "b={}", p[2]);
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_body_vertex_layout() {
    let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    let bundle = unity_asset_binary::bundle::load_bundle(&path).expect("bundle");
    let _ = bundle.data_checked();
    let mut lines = Vec::new();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(bytes) else { continue };
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
            let keys: Vec<_> = object.class.properties().keys().cloned().collect();
            let mesh = parse_mesh(&object, &version()).ok();
            let Some(mesh) = mesh else { continue };
            if mesh.name != "o_body_a" && mesh.name != "cf_O_face" {
                continue;
            }
            lines.push(format!(
                "name={} vtx={} idx={} ch={} data={} comp={} keys={:?}",
                mesh.name,
                mesh.vertex_data.vertex_count,
                mesh.index_buffer.len(),
                mesh.vertex_data.channels.len(),
                mesh.vertex_data.data_size.len(),
                mesh.mesh_compression,
                keys
            ));
            for (i, ch) in mesh.vertex_data.channels.iter().enumerate() {
                lines.push(format!(
                    "  ch{i} stream={} off={} fmt={} dim={}",
                    ch.stream, ch.offset, ch.format, ch.dimension
                ));
            }
            if let Some(UnityValue::Object(vd)) = object.class.properties().get("m_VertexData") {
                let vkeys: Vec<_> = vd.keys().cloned().collect();
                lines.push(format!("  vkeys={vkeys:?}"));
                if let Some(st) = vd.get("m_Streams") {
                    lines.push(format!("  streams={}", st.to_string().chars().take(400).collect::<String>()));
                }
            }
            if let Some(UnityValue::Object(comp)) = object.class.properties().get("m_CompressedMesh") {
                let nums: Vec<String> = comp
                    .iter()
                    .map(|(k, v)| match v {
                        UnityValue::Object(o) => {
                            let n = o.get("m_NumItems").and_then(|x| x.as_i64()).unwrap_or(0);
                            format!("{k}.n={n}")
                        }
                        _ => format!("{k}"),
                    })
                    .collect();
                lines.push(format!("  compressed {}", nums.join(" ")));
            }
            if let Some(shapes) = object.class.properties().get("m_Shapes") {
                lines.push(format!("  shapes={}", shapes.to_string().chars().take(400).collect::<String>()));
            }
            if mesh.name == "o_body_a" && mesh.vertex_data.vertex_count == 6823 {
                let data = &mesh.vertex_data.data_size;
                let n = mesh.vertex_data.vertex_count as usize;
                lines.push(format!("  nbytes={} per={}", data.len(), if n>0 { data.len()/n } else { 0 }));
                if let Some(vw) = object.class.properties().get("m_VariableBoneCountWeights") {
                    lines.push(format!("  varbones={}", vw.to_string().chars().take(300).collect::<String>()));
                }
                // guess stream2 at 68*n (40+28) and 72*n (40+32)
                let (strides, stream_off) = channel_layout(&mesh);
                lines.push(format!("  computed strides={strides:?} soff={stream_off:?}"));
                let s2 = stream_off.get(2).copied().unwrap_or(0);
                let st = strides.get(2).copied().unwrap_or(32);
                for vi in [0usize, 10, 100, 500, 2144, 3000, 5000, 6822] {
                    let off = s2 + vi * st;
                    if off + 32 > data.len() {
                        continue;
                    }
                    let b = &data[off..off + 32];
                    let mut idxs = Vec::new();
                    let mut ws = Vec::new();
                    for i in 0..8 {
                        let u = u32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap());
                        let f = f32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap());
                        if u < 128 {
                            idxs.push(u);
                        } else if f > 0.02 && f <= 1.02 {
                            ws.push(f);
                        }
                    }
                    let sum: f32 = ws.iter().sum();
                    lines.push(format!("  heur v{vi} idx={idxs:?} w={ws:?} sum={sum:.3}"));
                }
                for (label, base) in [
                    ("v0_s2", stream_off.get(2).copied().unwrap_or(0)),
                    ("v0_plus4", stream_off.get(2).copied().unwrap_or(0).saturating_add(4)),
                    ("v2144_s2", stream_off.get(2).copied().unwrap_or(0) + 2144 * strides.get(2).copied().unwrap_or(32)),
                ] {
                    if base + 32 <= data.len() {
                        let b = &data[base..base+32];
                        let f: Vec<f32> = (0..8).map(|i| f32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap())).collect();
                        let u: Vec<u32> = (0..8).map(|i| u32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap())).collect();
                        lines.push(format!("  {label} @{base} f32={f:?} u32={u:?}"));
                    }
                }
            }
        }
    }
    std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\mesh_layout.txt", lines.join("\n")).ok();
    eprintln!("{}", lines.join("\n"));
    let mut texn = 0usize;
    let mut file_bytes = Vec::new();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else { continue };
        file_bytes.push((file.name.clone(), bytes));
    }
    for (_name, bytes) in &file_bytes {
        let Ok(serialized) = parse_serialized_file(bytes.clone()) else { continue };
        texn += textures_from_serialized(&serialized, &file_bytes, false).len();
    }
    lines.push(format!("textures_decoded={texn}"));
    eprintln!("textures_decoded={texn}");
    assert!(!lines.is_empty());
}

#[test]
#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
fn dump_go_and_renderers() {
    let path = crate::test_support::game_path(r"\mods\Sideloader Modpack\yamadamod\[yamadamod]hairNicoichi rev4.zipmod");
    if !path.ok_exists() {
        return;
    }
    let bytes = crate::mods::zipmod_bundle_named(
        &path,
        "chara/ymd/mf_hairNicoichi.unity3d",
    )
    .expect("bundle");
    let bundle = load_bundle_unbounded(bytes.to_vec()).expect("load");
    let _ = bundle.data_checked();
    let mut lines = Vec::new();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        let mut ids = std::collections::BTreeMap::<i32, usize>::new();
        for info in &serialized.objects {
            *ids.entry(info.type_id).or_insert(0) += 1;
        }
        lines.push(format!("class_ids={ids:?}"));
        for info in serialized.objects_of_type(1) {
            if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                let name = obj.name().unwrap_or_default();
                let keys: Vec<_> = obj.class.properties().keys().cloned().collect();
                lines.push(format!("GO path={} name={name} keys={keys:?}", info.path_id));
            }
        }
        for cid in [23i32, 137, 33] {
            for info in serialized.objects_of_type(cid) {
                if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                    let keys: Vec<_> = obj.class.properties().keys().cloned().collect();
                    lines.push(format!(
                        "R cid={cid} path={} keys={keys:?} mesh={:?}",
                        info.path_id,
                        obj.class.properties().get("m_Mesh").map(|v| v.to_string().chars().take(80).collect::<String>())
                    ));
                }
            }
        }
    }
    std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\go_dump.txt", lines.join("\n")).ok();
    eprintln!("{}", lines.join("\n"));
}
