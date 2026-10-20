#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use super::*;
use crate::{portable,portable_outfits::{self,SnapshotReader},portable_resources::{self,ResourceRef,SplitSnapshot}};

#[track_caller]
fn assert_snapshot_equal(a:&SplitSnapshot,b:&SplitSnapshot){
    // HashMap iteration order is not a serialized-data contract.
    assert_eq!(a.schema,b.schema);
    assert_eq!(crate::session::OkSession::preview_residue_value(&a.residue).unwrap(),
        crate::session::OkSession::preview_residue_value(&b.residue).unwrap());
    let refs=|s:&SplitSnapshot|{
        let mut v=s.references.iter().map(|r|(r.part,r.field.clone(),r.kind.clone(),r.id.clone())).collect::<Vec<_>>();
        v.sort();v
    };
    assert_eq!(refs(a),refs(b));
}

#[test]
#[ignore="installed original assets and APNG; isolated exact single-slot integration"]
fn single_slot_card_patch_preserves_other_renderers_and_outfits(){
    let game=&crate::test_support::game_path("");
    let original=game.join("UserData/chara/female/19重绘爱莉希雅女仆v1.1.png");
    let library=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");
    let resources=Path::new("C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    std::env::set_var("OK_BUILTIN_RESOURCES",resources);
    let raw=crate::gamefs::read(&original).unwrap();let info=crate::card::parse_card_outfit(&original,0).unwrap();
    let path_source=crate::extract_session(game,Some(&original),0,None).unwrap();
    let source=crate::extract_session_from_info(game,None,0,None,Some(info.clone())).unwrap();
    // Filename-free runtime inputs must emit the same source renderers and
    // extraction context as the established path pipeline, including mods.
    assert_eq!(source.parts.len(),path_source.parts.len());
    for(a,b)in source.parts.iter().zip(&path_source.parts){
        assert_eq!((&a.name,&a.role,&a.source_property),(&b.name,&b.role,&b.source_property));
        assert_eq!(a.rest_positions,b.rest_positions);assert_eq!(a.positions,b.positions);
        assert_eq!(a.rest_normals,b.rest_normals);assert_eq!(a.rest_tangents,b.rest_tangents);
        assert_eq!(a.indices,b.indices);assert_eq!(a.weights,b.weights);assert_eq!(a.bone_idx,b.bone_idx);
        assert!(a.bind_poses==b.bind_poses,"fresh source bind poses differ for {}",a.name);assert_eq!(a.uvs,b.uvs);assert_eq!(a.uv2,b.uv2);
        assert_eq!(a.uv3,b.uv3);assert_eq!(a.uv4,b.uv4);assert_eq!(a.vertex_colors,b.vertex_colors);
        assert_eq!(a.png,b.png);assert_eq!(a.shader_textures,b.shader_textures);
        assert_eq!(a.runtime_textures,b.runtime_textures);
        assert_eq!(serde_json::to_value(&a.source_material).unwrap(),serde_json::to_value(&b.source_material).unwrap());
        assert_eq!(a.runtime_accessory.as_ref().map(|r|r.skin_contract().unwrap()),b.runtime_accessory.as_ref().map(|r|r.skin_contract().unwrap()));
    }
    let context=source.accessory_context.as_deref().expect("runtime metadata must own accessory context without a source filename");
    let path_context=path_source.accessory_context.as_deref().unwrap();
    assert_eq!(context.dependencies,path_context.dependencies);
    assert_eq!(serde_json::to_value(&context.body).unwrap(),serde_json::to_value(&path_context.body).unwrap());
    assert_eq!(serde_json::to_value(&context.head).unwrap(),serde_json::to_value(&path_context.head).unwrap());
    assert_eq!(context.body_textures,path_context.body_textures);assert_eq!(context.head_textures,path_context.head_textures);
    assert_eq!(context.hair_gloss,path_context.hair_gloss);assert_eq!(context.hair_gloss_sampling,path_context.hair_gloss_sampling);
    drop(path_source);
    // The same new-renderer preparation used for an incremental slot must
    // exactly match the corresponding arrays from a complete rig export.
    let exported=crate::skel_export::build(&source);
    for index in 0..source.parts.len() {
        let part=crate::skel_export::prepare_part_exports(&source,&source.parts[index..index+1],&exported.name_to_idx);
        assert!(part.rest[0]==exported.part_rest[index],"part-local reference positions differ: {}",source.parts[index].name);
        assert!(part.normals[0]==exported.part_ref_normals[index],"part-local normals differ: {}",source.parts[index].name);
        assert!(part.tangents[0]==exported.part_ref_tangents[index],"part-local tangents differ: {}",source.parts[index].name);
        assert!(part.bone_idx[0]==exported.part_bone_idx[index],"part-local indices differ: {}",source.parts[index].name);
        assert!(part.morphs[0]==exported.part_morphs[index],"part-local morphs differ: {}",source.parts[index].name);
    }
    drop(exported);
    let mut references=Vec::new();let mut unique=BTreeMap::new();
    let residue=source.split_preview_resources(&info,|part,field,kind,bytes,_|{
        let id=portable_resources::id(kind,&bytes);unique.insert(id.clone(),bytes);
        references.push(ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
    }).unwrap();
    let bytes=crate::gamefs::read(library).unwrap();let(_,range)=portable::png_snapshot(&bytes).unwrap();
    let payload=portable_outfits::replace_outfit(&bytes[range.unwrap()],SplitSnapshot{schema:1,residue,references,unique},0,resources).unwrap();
    let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
    let dir=Path::new("C:/portable/OpenKoikatsu/Saved/Tests").join(format!("EditorSingleSlotNativeR27-{stamp}"));fs::create_dir_all(&dir).unwrap();
    let base=dir.join("base.png");let(iend,_)=portable::png_snapshot(&raw).unwrap();
    let mut chunk=b"okPv".to_vec();chunk.extend(&payload);
    let mut base_bytes=raw[..iend].to_vec();base_bytes.extend((payload.len() as u32).to_be_bytes());
    base_bytes.extend(&chunk);base_bytes.extend(portable::crc(&chunk).to_be_bytes());base_bytes.extend(&raw[iend..]);
    fs::write(&base,&base_bytes).unwrap();
    let display_before=portable::load(&base).unwrap();
    let baseline=SnapshotReader::new(&payload,resources).unwrap();
    let original_snapshot=baseline.snapshot(0).unwrap();
    let t=targets().into_iter().find(|t|t.key=="accessory1_124").unwrap();
    let entry=crate::load_characustom_dir(&game.join("abdata/list/characustom")).get(124,14).unwrap().clone();
    let item=Item{key:"test-stock-necklace".into(),guid:String::new(),source:String::new(),entry};
    let changed=patch(&raw,&t,Some(&item),0).unwrap();
    let input=dir.join("changed.source.png");let output=dir.join("changed.png");
    let request=json!({"action":"apply","card":base,"resources":resources,"outfit":0,"target":t.key});
    // Runtime preparation must finish without creating or modifying a card.
    let mut changed_info=crate::card::parse_card_bytes(&changed,0).unwrap();
    crate::mods::apply_strip_all_migrations(game,&mut changed_info);
    let base_before=crate::gamefs::read(&base).unwrap();
    let prepared=portable::prepare_accessory_snapshot(game,&changed_info,&original_snapshot,0,1,|r|baseline.resolve(r)).unwrap().unwrap();
    assert!(!output.ok_exists());assert_eq!(crate::gamefs::read(&base).unwrap(),base_before);
    assert_eq!(prepared.replacement.retained_indices,(0..display_before.parts.len()).map(Some).collect::<Vec<_>>());
    assert_eq!(prepared.replacement.added_range.start,display_before.parts.len());
    assert_eq!(prepared.replacement.added_range.len(),prepared.parts.len());
    assert!(!prepared.parts.is_empty());
    // Exercise the editing context's owned candidate/checkpoint transaction,
    // not just the lower-level extractor. Preparation must have no publication
    // or card I/O, and stale candidates must fail after the next revision.
    let cache=Path::new("C:/portable/OpenKoikatsu/Saved/AppearanceEditor/cache");
    let key=catalog(game,cache).unwrap().iter().find(|i|i.entry.category_no==124&&i.entry.id==14&&i.source.is_empty()).unwrap().key.clone();
    let mut geometry_context=live_head::Context::new(game,&base,resources,cache,0).unwrap();
    let mut geometry_live=portable::load(&base).unwrap();geometry_live.ensure_skel_export();
    let candidate=geometry_context.prepare_geometry(&t.key,&key).unwrap();
    let stale=geometry_context.prepare_geometry(&t.key,&key).unwrap();
    // The shaped-globals template (first call: miss, second: hit) must equal
    // the uncached shape policy applied with the renderers present.
    std::env::set_var("OK_TEST_GEOMETRY_UNCACHED","1");
    let reference=geometry_context.prepare_geometry(&t.key,&key).unwrap();
    std::env::remove_var("OK_TEST_GEOMETRY_UNCACHED");
    assert!(stale.timings.contains_key("geometry.template_hit")&&!candidate.timings.contains_key("geometry.template_hit"));
    for(label,other)in [("hit",&stale),("uncached",&reference)]{
        assert_eq!(candidate.source.shape_body,other.source.shape_body);assert_eq!(candidate.source.shape_face,other.source.shape_face);
        assert_eq!(candidate.source.parts.len(),other.source.parts.len());
        for(a,b)in candidate.source.parts.iter().zip(&other.source.parts){
            let delta=|x:&[[f32;3]],y:&[[f32;3]]|x.iter().zip(y).map(|(p,q)|(0..3).map(|i|(p[i]-q[i]).abs()).fold(0f32,f32::max)).fold(0f32,f32::max);
            assert!(a.positions==b.positions&&a.normals==b.normals&&a.rest_positions==b.rest_positions,
                "template renderer geometry differs ({label}): {} positions={} normals={} rest={}",a.name,delta(&a.positions,&b.positions),delta(&a.normals,&b.normals),delta(&a.rest_positions,&b.rest_positions));
            assert!(a.weights==b.weights&&a.bone_idx==b.bone_idx&&a.bind_poses==b.bind_poses,"template renderer skin differs: {}",a.name);
        }
        assert!(candidate.exports.rest==other.exports.rest&&candidate.exports.normals==other.exports.normals&&candidate.exports.tangents==other.exports.tangents,"template reference export differs");
        assert!(candidate.exports.morphs==other.exports.morphs&&candidate.exports.bone_idx==other.exports.bone_idx,"template reference morphs differ");
        let(x,y)=(candidate.source.skel_export.as_ref().unwrap(),other.source.skel_export.as_ref().unwrap());
        assert!(x.name_to_idx==y.name_to_idx&&x.bones.len()==y.bones.len());
        for(a,b)in x.bones.iter().zip(&y.bones){assert!(a.rest_pos==b.rest_pos&&a.rest_rot==b.rest_rot&&a.rest_scl==b.rest_scl&&a.parent==b.parent,"template rig differs: {}",a.name);}
    }
    drop(reference);
    let checkpoint=geometry_context.geometry_checkpoint(&candidate).unwrap();
    let geometry_output=dir.join("geometry-checkpoint.png");
    assert!(!geometry_output.ok_exists());assert_eq!(geometry_context.revision(),0);
    assert_eq!(geometry_live.parts.len(),display_before.parts.len());
    geometry_context.validate_geometry(&candidate,&geometry_live).unwrap();
    geometry_context.commit_geometry(candidate,&mut geometry_live).unwrap();
    assert_eq!(geometry_context.revision(),1);
    let committed_count=geometry_live.parts.len();
    assert!(geometry_context.validate_geometry(&stale,&geometry_live).is_err());
    assert!(geometry_context.commit_geometry(stale,&mut geometry_live).is_err());
    assert_eq!(geometry_live.parts.len(),committed_count);assert_eq!(geometry_context.revision(),1);
    checkpoint.export(&geometry_output).unwrap();
    let checkpoint_bytes=crate::gamefs::read(&geometry_output).unwrap();
    let(_,checkpoint_range)=portable::png_snapshot(&checkpoint_bytes).unwrap();
    let checkpoint_reader=SnapshotReader::new(&checkpoint_bytes[checkpoint_range.unwrap()],resources).unwrap();
    assert_snapshot_equal(&prepared.replacement.snapshot,&checkpoint_reader.snapshot(0).unwrap());
    for index in 1..info.coordinate_count {assert_snapshot_equal(&baseline.snapshot(index).unwrap(),&checkpoint_reader.snapshot(index).unwrap());}
    let reset=geometry_context.prepare_geometry(&t.key,"").unwrap();
    geometry_context.commit_geometry(reset,&mut geometry_live).unwrap();
    assert_eq!(geometry_context.revision(),2);assert_eq!(geometry_live.parts.len(),display_before.parts.len());
    drop(geometry_live);drop(geometry_context);
    geometry_c_abi_lifecycle(game,&base,resources,cache,&t.key,&key,&prepared.replacement.snapshot,&dir);
    let mut native_live=portable::load(&base).unwrap();
    native_live.ensure_skel_export();native_live.anim_time=1.234;
    let positions_before=native_live.parts.iter().map(|p|p.positions.as_ptr()).collect::<Vec<_>>();
    let refs_before=native_live.skel_export.as_ref().unwrap().part_rest.iter().map(|p|p.as_ptr()).collect::<Vec<_>>();
    let mut invalid=prepared.replacement.retained_indices.clone();invalid[0]=None;
    let empty_exports=crate::skel_export::prepare_part_exports(&native_live,&[],&native_live.skel_export.as_ref().unwrap().name_to_idx);
    assert!(native_live.replace_accessory_parts(0,1,&invalid,Vec::new(),empty_exports).is_err());
    assert_eq!(native_live.parts.iter().map(|p|p.positions.as_ptr()).collect::<Vec<_>>(),positions_before);
    let new_exports=crate::skel_export::prepare_part_exports(&native_live,&prepared.parts,&native_live.skel_export.as_ref().unwrap().name_to_idx);
    native_live.replace_accessory_parts(0,1,&prepared.replacement.retained_indices,prepared.parts,new_exports).unwrap();
    assert_eq!(native_live.parts.len(),prepared.replacement.added_range.end);
    assert_eq!(native_live.anim_time,1.234);
    for(old,mapped)in prepared.replacement.retained_indices.iter().enumerate(){
        if let Some(index)=mapped {
            assert_eq!(native_live.parts[*index].positions.as_ptr(),positions_before[old]);
            assert_eq!(native_live.skel_export.as_ref().unwrap().part_rest[*index].as_ptr(),refs_before[old]);
        }
    }
    let rebuilt=crate::skel_export::build(&native_live);
    let published=native_live.skel_export.as_ref().unwrap();
    assert!(published.part_rest==rebuilt.part_rest,"published slot reference positions differ from complete export");
    assert!(published.part_bone_idx==rebuilt.part_bone_idx,"published slot indices differ from complete export");
    assert!(published.part_morphs==rebuilt.part_morphs,"published slot morphs differ from complete export");
    drop(rebuilt);drop(native_live);
    let started=std::time::Instant::now();
    export_edited(game,&request,&changed,&output).unwrap();
    let report:J=serde_json::from_slice(&crate::gamefs::read(output.with_extension("verified.json")).unwrap()).unwrap();
    assert_eq!(report["single_slot"],1,"must use exact single slot");
    for key in ["source_write","preview_build","metadata_hash_write","through_card_write"]{
        assert!(report["timings_ms"][key].as_f64().is_some_and(|v|v.is_finite()&&v>=0.),"invalid stage timing {key}");
    }
    for key in ["base_read_directory","card_parse_migrations","restore_global_context","read_provenance","extract_slot","split_part_resources","patch_parts","pack_outfit","write_png"]{
        assert!(report["single_slot_timings_ms"][key].as_f64().is_some_and(|v|v.is_finite()&&v>=0.),"invalid single-slot stage timing {key}");
    }
    let elapsed=started.elapsed().as_secs_f64()*1000.;assert_eq!(report["outfits_rebuilt"],0);
    assert!(report["parts_rebuilt"].as_u64().unwrap()>0);
    let updated=crate::gamefs::read(&output).unwrap();let(_,range)=portable::png_snapshot(&updated).unwrap();
    let updated_payload=&updated[range.unwrap()];let after=SnapshotReader::new(updated_payload,resources).unwrap();
    for i in 1..info.coordinate_count{assert_snapshot_equal(&baseline.snapshot(i).unwrap(),&after.snapshot(i).unwrap());}
    let after_snapshot=after.snapshot(0).unwrap();
    assert_snapshot_equal(&prepared.replacement.snapshot,&after_snapshot);
    for r in &prepared.replacement.snapshot.references {
        if let Some(raw)=prepared.replacement.snapshot.unique.get(&r.id){assert_eq!(raw,&after.resolve(r).unwrap());}
    }
    // No old renderer was removed when adding to an empty slot. Its exact
    // resource IDs and indices, including wig and body, must stay unchanged.
    for r in &original_snapshot.references{assert!(after_snapshot.references.iter().any(|a|a.part==r.part&&a.field==r.field&&a.kind==r.kind&&a.id==r.id));}
    let loaded=portable::load(&output).unwrap();assert!(loaded.parts.len()>display_before.parts.len());
    for(a,b)in display_before.parts.iter().zip(&loaded.parts){
        assert_eq!(a.name,b.name);assert_eq!(a.source_property,b.source_property);
        assert!(a.positions==b.positions,"display geometry changed for {}",a.name);
        assert!(a.weights==b.weights&&a.bone_idx==b.bone_idx,"display skin changed for {}",a.name);
        assert!(a.png==b.png,"display texture changed for {}",a.name);
    }
    assert_eq!(loaded.shape_body,display_before.shape_body);assert_eq!(loaded.shape_face,display_before.shape_face);
    let reset=patch(&changed,&t,None,0).unwrap();
    let reset_output=dir.join("reset.png");let request=json!({"action":"apply","card":output,"resources":resources,"outfit":0,"target":t.key});
    export_edited(game,&request,&reset,&reset_output).unwrap();
    let reset_report:J=serde_json::from_slice(&crate::gamefs::read(reset_output.with_extension("verified.json")).unwrap()).unwrap();
    assert_eq!(reset_report["single_slot"],1);
    assert_eq!(reset_report["parts_rebuilt"],0);assert_eq!(portable::load(&reset_output).unwrap().parts.len(),display_before.parts.len());
    // Old portable cards must upgrade through the existing full source path.
    assert!(portable::shared_edited_accessory(game,&input,library,resources,&dir.join("legacy.png"),0,1).unwrap().is_none());
    eprintln!("OK single-slot APNG add/reset/legacy exact=1 unchanged_outfits={} source_update_ms={elapsed:.3} report={}",info.coordinate_count-1,dir.display());
}

/// The owned C ABI used by UE: validation never consumes, foreseeable failures
/// retain the candidate, stage clones are independent and match the native
/// candidate, and the pinned checkpoint outlives its editing context.
fn geometry_c_abi_lifecycle(game:&Path,base:&Path,resources:&Path,cache:&Path,target:&str,key:&str,expected:&SplitSnapshot,dir:&Path){unsafe{
    use super::live_head_ffi::*;use std::{ffi::{CString,CStr},ptr};
    let c=|s:&str|CString::new(s).unwrap();let path=|p:&Path|c(&p.to_string_lossy());
    let(root,card,res,cache)=(path(game),path(base),path(resources),path(cache));
    let mut err=[0i8;2048];let message=|e:&[i8;2048]|CStr::from_ptr(e.as_ptr()).to_string_lossy().into_owned();
    let mut context=ptr::null_mut();
    assert_eq!(ok_appearance_context_open(root.as_ptr(),card.as_ptr(),res.as_ptr(),cache.as_ptr(),0,&mut context,err.as_mut_ptr(),2048),0,"{}",message(&err));
    let mut live=portable::load(base).unwrap();live.ensure_skel_export();live.anim_time=7.5;
    let original_count=live.parts.len();let positions_before=live.parts.iter().map(|p|p.positions.as_ptr()).collect::<Vec<_>>();
    let(target,key)=(c(target),c(key));
    let prepare=|err:&mut [i8;2048]|{let mut out=ptr::null_mut();
        assert_eq!(ok_appearance_prepare_geometry(context,target.as_ptr(),key.as_ptr(),&mut out,err.as_mut_ptr(),2048),0,"{}",message(err));assert!(!out.is_null());out};
    let candidate=prepare(&mut err);let stale=prepare(&mut err);
    let n=ok_appearance_geometry_info(candidate,ptr::null_mut(),0);assert!(n>0);
    let mut small=[7u8;1];assert_eq!(ok_appearance_geometry_info(candidate,small.as_mut_ptr(),1),-1);assert_eq!(small,[7]);
    let mut bytes=vec![0u8;n as usize];assert_eq!(ok_appearance_geometry_info(candidate,bytes.as_mut_ptr(),n),n);
    let info:J=serde_json::from_slice(&bytes).unwrap();
    assert_eq!(info["base_revision"],0);assert_eq!(info["slot"],1);
    assert_eq!(info["retained_indices"].as_array().unwrap().len(),original_count);
    assert_eq!(info["added_range"][0],original_count);
    let added=info["parts"].as_array().unwrap().len();assert!(added>0);
    assert_eq!(info["added_range"][1].as_u64().unwrap() as usize,original_count+added);
    assert!(info["timings_ms"]["ffi_prepare_total"].as_f64().is_some_and(|v|v.is_finite()&&v>=0.));
    let n=ok_appearance_geometry_card(candidate,ptr::null_mut(),0);assert!(n>0);
    let mut raw=vec![0u8;n as usize];assert_eq!(ok_appearance_geometry_card(candidate,raw.as_mut_ptr(),n),n);
    assert!(crate::card::parse_card_bytes(&raw,0).is_ok());
    // Stage: an independent owned session whose renderers and rig match the
    // exact candidate and whose rig matches live. Freeing it leaves the candidate.
    let mut stage=ptr::null_mut();
    assert_eq!(ok_appearance_geometry_stage(candidate,&mut stage,err.as_mut_ptr(),2048),0,"{}",message(&err));
    {
        let stage=&*stage;let guard=candidate.as_ref().unwrap().0.lock().unwrap();let p=guard.as_ref().unwrap();
        assert_eq!(stage.parts.len(),p.source.parts.len());
        for(a,b)in stage.parts.iter().zip(&p.source.parts){
            assert_eq!((&a.name,&a.role,a.shader_kind,a.render_visible),(&b.name,&b.role,b.shader_kind,b.render_visible));
            assert!(a.source_property.is_none(),"stage clone is not a publication input");
            assert!(a.rest_positions==b.rest_positions&&a.positions==b.positions&&a.rest_normals==b.rest_normals&&a.rest_tangents==b.rest_tangents,"stage geometry differs: {}",a.name);
            assert!(a.indices==b.indices&&a.weights==b.weights&&a.bone_idx==b.bone_idx&&a.uvs==b.uvs&&a.uv2==b.uv2,"stage topology/skin differs: {}",a.name);
            assert!(a.bind_poses==b.bind_poses,"stage bind poses differ: {}",a.name);
            assert!(a.png==b.png&&a.shader_textures==b.shader_textures&&a.runtime_textures==b.runtime_textures,"stage textures differ: {}",a.name);
            assert_eq!(serde_json::to_value(&a.source_material).unwrap(),serde_json::to_value(&b.source_material).unwrap(),"stage material differs: {}",a.name);
            assert_eq!(a.runtime_accessory.as_ref().map(|r|r.skin_contract().unwrap()),b.runtime_accessory.as_ref().map(|r|r.skin_contract().unwrap()));
            assert_ne!(a.positions.as_ptr(),b.positions.as_ptr());
        }
        let s=stage.skel_export.as_ref().unwrap();let l=live.skel_export.as_ref().unwrap();
        assert!(s.name_to_idx==l.name_to_idx&&s.bones.len()==l.bones.len(),"stage global rig differs from live");
        for(a,b)in s.bones.iter().zip(&l.bones){assert!(a.name==b.name&&a.parent==b.parent&&a.rest_pos==b.rest_pos&&a.rest_rot==b.rest_rot&&a.rest_scl==b.rest_scl,"stage bone differs: {}",a.name);}
        assert!(s.part_rest==p.exports.rest&&s.part_bone_idx==p.exports.bone_idx&&s.part_morphs==p.exports.morphs,"stage reference export differs from candidate");
        assert!(s.part_ref_normals==p.exports.normals&&s.part_ref_tangents==p.exports.tangents,"stage reference normals differ from candidate");
    }
    crate::ffi::session_free(stage);
    assert!(ok_appearance_geometry_info(candidate,ptr::null_mut(),0)>0);
    let mut checkpoint=ptr::null_mut();
    assert_eq!(ok_appearance_geometry_checkpoint(context,candidate,&mut checkpoint,err.as_mut_ptr(),2048),0,"{}",message(&err));
    assert_eq!(ok_appearance_checkpoint_revision(checkpoint),1);
    // Validation alone consumes nothing and publishes nothing.
    assert_eq!(ok_appearance_geometry_commit(context,candidate,&mut live,0,err.as_mut_ptr(),2048),0,"{}",message(&err));
    assert!(ok_appearance_geometry_info(candidate,ptr::null_mut(),0)>0);assert_eq!(live.parts.len(),original_count);
    // A foreseeable rejection retains the candidate and leaves live untouched.
    let shape=live.shape_face.clone();live.shape_face[0]+=0.01;
    assert_eq!(ok_appearance_geometry_commit(context,candidate,&mut live,1,err.as_mut_ptr(),2048),-1);
    assert!(ok_appearance_geometry_info(candidate,ptr::null_mut(),0)>0,"rejected commit must retain candidate");
    assert_eq!(live.parts.len(),original_count);live.shape_face=shape;
    assert_eq!(ok_appearance_geometry_commit(context,candidate,&mut live,1,err.as_mut_ptr(),2048),0,"{}",message(&err));
    assert_eq!(live.parts.len(),original_count+added);assert_eq!(live.anim_time,7.5);
    for(old,mapped)in info["retained_indices"].as_array().unwrap().iter().enumerate(){
        if let Some(index)=mapped.as_u64(){assert_eq!(live.parts[index as usize].positions.as_ptr(),positions_before[old]);}
    }
    assert_eq!(ok_appearance_geometry_info(candidate,ptr::null_mut(),0),-1);
    let mut consumed=ptr::null_mut();assert_eq!(ok_appearance_geometry_stage(candidate,&mut consumed,err.as_mut_ptr(),2048),-1);assert!(consumed.is_null());
    let mut stale_checkpoint=ptr::null_mut();
    assert_eq!(ok_appearance_geometry_checkpoint(context,stale,&mut stale_checkpoint,err.as_mut_ptr(),2048),-1);assert!(stale_checkpoint.is_null());
    assert_eq!(ok_appearance_geometry_commit(context,stale,&mut live,1,err.as_mut_ptr(),2048),-1);
    assert!(ok_appearance_geometry_info(stale,ptr::null_mut(),0)>0,"stale commit must retain candidate");
    assert_eq!(live.parts.len(),original_count+added);
    let mut current=ptr::null_mut();assert_eq!(ok_appearance_checkpoint_capture(context,&mut current,err.as_mut_ptr(),2048),0);
    assert_eq!(ok_appearance_checkpoint_revision(current),1);ok_appearance_checkpoint_free(current);
    ok_appearance_geometry_free(candidate);ok_appearance_geometry_free(stale);ok_appearance_context_free(context);
    let output=dir.join("ffi-geometry-checkpoint.png");let output_c=path(&output);
    assert_eq!(ok_appearance_checkpoint_export(checkpoint,output_c.as_ptr(),err.as_mut_ptr(),2048),0,"{}",message(&err));
    ok_appearance_checkpoint_free(checkpoint);
    let bytes=crate::gamefs::read(&output).unwrap();let(_,range)=portable::png_snapshot(&bytes).unwrap();
    assert_snapshot_equal(expected,&SnapshotReader::new(&bytes[range.unwrap()],resources).unwrap().snapshot(0).unwrap());
    eprintln!("OK geometry C ABI lifecycle added={added} retained={original_count}");
}}

/// Diagnostic: report the first differing field of two checkpoints' changed
/// accessory resources. Inputs are OK_DIFF_A / OK_DIFF_B portable PNG paths.
#[test]
#[ignore="diagnostic over two explicit checkpoint files"]
fn diagnose_checkpoint_resource_difference(){
    fn diff(a:&J,b:&J,path:&str,out:&mut Vec<String>){
        if a==b||out.len()>=20{return;}
        match(a,b){
            (J::Object(x),J::Object(y))=>{for k in x.keys().chain(y.keys()){match(x.get(k),y.get(k)){(Some(p),Some(q))=>diff(p,q,&format!("{path}/{k}"),out),_=>out.push(format!("{path}/{k}: missing"))}}}
            (J::Array(x),J::Array(y)) if x.len()==y.len()=>{for(i,(p,q))in x.iter().zip(y).enumerate(){diff(p,q,&format!("{path}/{i}"),out)}}
            _=>{let s=|v:&J|{let t=v.to_string();if t.len()>200{format!("{}...(len {})",&t[..200],t.len())}else{t}};out.push(format!("{path}: {} != {}",s(a),s(b)))}
        }
    }
    let resources=Path::new("C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
    let open=|key:&str|{let bytes=crate::gamefs::read(std::env::var(key).unwrap()).unwrap();let(_,r)=portable::png_snapshot(&bytes).unwrap();let payload=bytes[r.unwrap()].to_vec();payload};
    let(pa,pb)=(open("OK_DIFF_A"),open("OK_DIFF_B"));
    let(ra,rb)=(SnapshotReader::new(&pa,resources).unwrap(),SnapshotReader::new(&pb,resources).unwrap());
    let(sa,sb)=(ra.snapshot(0).unwrap(),rb.snapshot(0).unwrap());
    let read=|raw:&[u8]|->crate::unity::ExtractedMesh{bincode::deserialize(raw).unwrap()};
    for a in &sa.references{
        let Some(b)=sb.references.iter().find(|b|b.part==a.part&&b.field==a.field)else{eprintln!("missing in B {:?} {}",a.part,a.field);continue};
        if a.id==b.id{continue;}
        let(x,y)=(ra.resolve(a).unwrap(),rb.resolve(b).unwrap());
        eprintln!("DIFF part={:?} field={} kind={} len {} vs {}",a.part,a.field,a.kind,x.len(),y.len());
        let(jx,jy)=if a.field=="accessory_source"{(serde_json::to_value(read(&x)).unwrap(),serde_json::to_value(read(&y)).unwrap())}
            else if let(Ok(p),Ok(q))=(bincode::deserialize::<crate::unity::MeshMaterial>(&x),bincode::deserialize::<crate::unity::MeshMaterial>(&y)){(serde_json::to_value(p).unwrap(),serde_json::to_value(q).unwrap())}
            else{let first=x.iter().zip(&y).position(|(p,q)|p!=q);eprintln!("  raw first byte difference {first:?}");continue};
        let mut out=Vec::new();diff(&jx,&jy,"",&mut out);for line in out{eprintln!("  {line}");}
    }
}

/// Diagnostic for the legacy APNG provenance upgrade: does a filename-free
/// source extraction reproduce each stored accessory renderer byte-exactly?
#[test]
#[ignore="installed C001 APNG and original assets; legacy provenance parity"]
fn legacy_accessory_provenance_parity(){
    let game=&crate::test_support::game_path("");
    let card=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png");
    let resources=Path::new("C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    std::env::set_var("OK_BUILTIN_RESOURCES",resources);
    let outfit:usize=std::env::var("OK_PARITY_OUTFIT").ok().and_then(|v|v.parse().ok()).unwrap_or(0);
    let bytes=crate::gamefs::read(card).unwrap();let(_,range)=portable::png_snapshot(&bytes).unwrap();
    let reader=SnapshotReader::new(&bytes[range.unwrap()],resources).unwrap();let stored=reader.snapshot(outfit).unwrap();
    let stored_value=crate::session::OkSession::preview_residue_value(&stored.residue).unwrap();
    let stored_parts=stored_value["session"]["parts"].as_array().unwrap().clone();
    eprintln!("stored parts={} globals={:?}",stored_parts.len(),stored.references.iter().filter(|r|r.part.is_none()).map(|r|r.field.clone()).collect::<Vec<_>>());
    let mut info=crate::card::parse_card_bytes(&bytes,outfit as i32).unwrap();
    crate::mods::apply_strip_all_migrations(game,&mut info);
    let started=std::time::Instant::now();
    let source=crate::extract_session_from_info(game,None,outfit as i32,None,Some(info.clone())).unwrap();
    eprintln!("source extraction ms={:.1} parts={} context={}",started.elapsed().as_secs_f64()*1000.,source.parts.len(),source.accessory_context.is_some());
    let mut references=Vec::new();let mut unique=BTreeMap::new();
    let residue=source.split_preview_resources(&info,|part,field,kind,raw,_|{let id=portable_resources::id(kind,&raw);unique.insert(id.clone(),raw);
        references.push(ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())}).unwrap();
    let fresh=SplitSnapshot{schema:1,residue,references,unique};
    let fresh_value=crate::session::OkSession::preview_residue_value(&fresh.residue).unwrap();
    let fresh_parts=fresh_value["session"]["parts"].as_array().unwrap();
    let ids=|s:&SplitSnapshot,i:usize|{let mut v=s.references.iter().filter(|r|r.part==Some(i)&&r.field!="part_origin").map(|r|(r.field.clone(),r.id.clone())).collect::<Vec<_>>();v.sort();v};
    for(i,p)in stored_parts.iter().enumerate(){
        let role=p["role"].as_str().unwrap();let name=p["name"].as_str().unwrap();
        let candidates=fresh_parts.iter().enumerate().filter(|(_,f)|f["name"]==p["name"]&&f["role"]==p["role"]).map(|(j,_)|j).collect::<Vec<_>>();
        let exact=candidates.iter().filter(|j|ids(&fresh,**j)==ids(&stored,i)).count();
        let residue_equal=candidates.iter().filter(|j|fresh_parts[**j]==*p).count();
        if role.starts_with("acc")||exact==0{
            let origin=candidates.first().and_then(|j|source.parts[*j].source_property.clone());
            eprintln!("part {i} role={role} name={name} same_name={} exact_resources={exact} residue_equal={residue_equal} origin={origin:?}",candidates.len());
            if exact==0&&!candidates.is_empty(){
                let a=ids(&stored,i);let b=ids(&fresh,candidates[0]);
                for(x,y)in a.iter().zip(&b){if x!=y{eprintln!("   differs {} vs {}",x.0,y.0);}}
                if a.len()!=b.len(){eprintln!("   field counts {} vs {}",a.len(),b.len());}
            }
        }
    }
    eprintln!("fresh parts={}",fresh_parts.len());
}

/// Fixture for real-display geometry acceptance: a portable APNG carrying exact
/// accessory context/provenance under the editor's own extraction environment
/// (no test-only OK_* switches), written to OK_GEOMETRY_FIXTURE_OUT.
#[test]
#[ignore="installed original card; writes an editor-environment geometry fixture"]
fn editor_environment_geometry_fixture(){
    let game=&crate::test_support::game_path("");
    let original=game.join("UserData/chara/female/19重绘爱莉希雅女仆v1.1.png");
    let library=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");
    let resources=Path::new("C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
    let output=std::path::PathBuf::from(std::env::var("OK_GEOMETRY_FIXTURE_OUT").unwrap());
    assert!(!output.ok_exists(),"fixture output must be fresh");
    std::env::set_var("OK_BUILTIN_RESOURCES",resources);
    // Same thread-scoped profile and clothes request as the editor exporter.
    let _profile=crate::profile::portable_export();let _clothes=crate::unity::request_clothes_states([0;9]);
    let raw=crate::gamefs::read(&original).unwrap();let info=crate::card::parse_card_outfit(&original,0).unwrap();
    let source=crate::extract_session_from_info(game,None,0,None,Some(info.clone())).unwrap();
    assert!(source.accessory_context.is_some());
    assert!(source.parts.iter().filter(|p|p.role.starts_with("acc")).all(|p|p.source_property.is_some()));
    let mut references=Vec::new();let mut unique=BTreeMap::new();
    let residue=source.split_preview_resources(&info,|part,field,kind,bytes,_|{
        let id=portable_resources::id(kind,&bytes);unique.insert(id.clone(),bytes);
        references.push(ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
    }).unwrap();
    let bytes=crate::gamefs::read(library).unwrap();let(_,range)=portable::png_snapshot(&bytes).unwrap();
    let payload=portable_outfits::replace_outfit(&bytes[range.unwrap()],SplitSnapshot{schema:1,residue,references,unique},0,resources).unwrap();
    let(iend,_)=portable::png_snapshot(&raw).unwrap();
    let mut chunk=b"okPv".to_vec();chunk.extend(&payload);
    let mut base_bytes=raw[..iend].to_vec();base_bytes.extend((payload.len() as u32).to_be_bytes());
    base_bytes.extend(&chunk);base_bytes.extend(portable::crc(&chunk).to_be_bytes());base_bytes.extend(&raw[iend..]);
    fs::create_dir_all(output.parent().unwrap()).unwrap();fs::write(&output,&base_bytes).unwrap();
    let loaded=portable::load(&output).unwrap();assert_eq!(loaded.parts.len(),source.parts.len());
    eprintln!("OK editor geometry fixture parts={} output={}",loaded.parts.len(),output.display());
}

/// Cached-bundle accessory colour metadata must equal the original byte-based
/// parse for installed vanilla and mod accessories (sets compared sorted).
#[test]
#[ignore="installed vanilla and mod accessory bundles"]
fn cached_accessory_color_use_matches_uncached_parse(){
    let game=&crate::test_support::game_path("");
    let vanilla=crate::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let pick=|catalog:&crate::list::ChaListCatalog,limit:usize|{let mut v=catalog.by_id.values().filter(|e|(121..=130).contains(&e.category_no)).cloned().collect::<Vec<_>>();
        v.sort_by_key(|e|(e.category_no,e.id));let step=(v.len()/limit).max(1);v.into_iter().step_by(step).take(limit).collect::<Vec<_>>()};
    let sorted=|s:&std::collections::HashSet<i64>|{let mut v=s.iter().copied().collect::<Vec<_>>();v.sort();v};
    let mut compared=0;
    for entry in pick(&vanilla,40).into_iter().chain(pick(&mods,40)){
        let zip=entry.extra.get("__zip").map(Path::new);
        let Some((bytes,_))=crate::accessory_bundle_source(game,zip,&entry) else{continue};
        let old=crate::unity::accessory_color_use(&bytes,&entry.main_data);
        let new=crate::unity::parse_bundle_ref(&bytes).accessory_color_use(&entry.main_data);
        assert_eq!(old.is_some(),new.is_some(),"presence differs for {} {}",entry.category_no,entry.id);
        if let(Some(a),Some(b))=(old,new){
            assert_eq!((a.use01,a.use02,a.use03,a.has_hair_component,a.has_accessory_component),(b.use01,b.use02,b.use03,b.has_hair_component,b.has_accessory_component));
            for(x,y)in [(&a.normal,&b.normal),(&a.alpha,&b.alpha),(&a.hair,&b.hair),(&a.custom_hair,&b.custom_hair),(&a.rend_accessory,&b.rend_accessory)]{assert_eq!(sorted(x),sorted(y),"renderer set differs for {} {}",entry.category_no,entry.id);}
            assert_eq!(a.length_bones,b.length_bones);assert_eq!(a.add_length.to_bits(),b.add_length.to_bits());
        }
        compared+=1;
    }
    assert!(compared>=60,"too few installed accessory bundles compared: {compared}");
    eprintln!("OK cached accessory colour metadata compared={compared}");
}
