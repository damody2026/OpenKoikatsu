//! Content-addressed application assets and card-only resource references.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::{collections::{BTreeMap, BTreeSet}, fs, io::Read, path::{Path, PathBuf}};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use bincode::Options;

const MAGIC: &[u8;8] = b"OKAS0001";
#[derive(Serialize, Deserialize, Clone)]
pub(crate) struct ResourceRef {
    pub part: Option<usize>, pub field: String, pub kind: String, pub id: String,
}
#[derive(Serialize, Deserialize, Clone)]
pub(crate) struct SplitSnapshot {
    pub schema: u32,
    pub residue: Vec<u8>,
    pub references: Vec<ResourceRef>,
    pub unique: BTreeMap<String, Vec<u8>>,
}
#[derive(Serialize, Deserialize, Default)]
struct Catalog {
    schema: u32,
    assets: BTreeMap<String, AssetInfo>,
    imports: BTreeMap<String, serde_json::Value>,
}
#[derive(Serialize, Deserialize)]
struct AssetInfo { kind: String, raw_bytes: usize, bytes: usize, labels: BTreeSet<String> }

pub(crate) fn id(kind: &str, raw: &[u8]) -> String {
    let mut hash = Sha256::new(); hash.update(kind.as_bytes()); hash.update([0]); hash.update(raw);
    format!("{:x}", hash.finalize())
}
fn valid_id(id: &str) -> bool {id.len()==64 && id.bytes().all(|b|b.is_ascii_digit() || (b'a'..=b'f').contains(&b))}
pub(crate) fn read_bounded(path: &Path) -> Result<Vec<u8>,String> {
    let file=fs::File::open(path).map_err(|e|format!("{}: {e}",path.display()))?;
    let limit=crate::portable::MAX_BYTES+64;
    if file.metadata().map_err(|e|e.to_string())?.len()>limit {return Err("resource exceeds size limit".into());}
    let mut bytes=Vec::new();file.take(limit+1).read_to_end(&mut bytes).map_err(|e|e.to_string())?;
    if bytes.len() as u64>limit {return Err("resource exceeds size limit".into());} Ok(bytes)
}
pub(crate) fn encode(raw: &[u8]) -> Vec<u8> {
    let mut out=MAGIC.to_vec();out.extend((raw.len() as u64).to_le_bytes());
    out.extend(lz4_flex::block::compress(raw));out
}
pub(crate) fn decode(bytes: &[u8], kind: &str, expected: &str) -> Result<Vec<u8>,String> {
    if bytes.len()<16 || &bytes[..8]!=MAGIC {return Err("invalid builtin resource header".into());}
    let size=u64::from_le_bytes(bytes[8..16].try_into().unwrap());
    if size==0 || size>crate::portable::MAX_BYTES {return Err("invalid builtin resource length".into());}
    let raw=lz4_flex::block::decompress(&bytes[16..],size as usize).map_err(|e|e.to_string())?;
    if id(kind,&raw)!=expected {return Err(format!("builtin resource checksum mismatch: {expected}"));} Ok(raw)
}

/// The application owns this directory; no game/mod directory is searched.
pub(crate) fn root(card: &Path) -> Result<PathBuf,String> {
    if let Some(path)=std::env::var_os("OK_BUILTIN_RESOURCES") {return Ok(PathBuf::from(path));}
    for ancestor in card.ancestors().skip(1) {
        let path=ancestor.join("Content/IndependentResources/v1");
        if path.join("catalog.json").ok_is_file() {return Ok(path);}
    }
    Err("builtin resource pack not found; install Content/IndependentResources/v1 or set OK_BUILTIN_RESOURCES".into())
}

pub(crate) fn split(session: &crate::session::OkSession, card: &crate::card::CardInfo,
    root: &Path, source_name: &str) -> Result<(Vec<u8>,serde_json::Value),String> {
    fs::create_dir_all(root.join("assets")).map_err(|e|e.to_string())?;
    let catalog_path=root.join("catalog.json");
    let mut catalog:Catalog=if catalog_path.ok_exists() {
        serde_json::from_slice(&read_bounded(&catalog_path)?).map_err(|e|e.to_string())?
    } else {Catalog {schema:1,..Default::default()}};
    if catalog.schema!=1 {return Err("unsupported builtin catalog schema".into());}
    let mut references=Vec::new();let mut unique=BTreeMap::new();
    let residue=session.split_preview_resources(card,|part,field,kind,raw,shared| {
        if raw.len() as u64>crate::portable::MAX_BYTES {return Err("asset exceeds limit".into());}
        let hash=id(kind,&raw);
        let label=part.map(|i|format!("{}.{}.{}",session.parts[i].role,session.parts[i].name,field))
            .unwrap_or_else(||format!("global.{field}"));
        if shared {
            let path=root.join("assets").join(format!("{hash}.okasset"));
            let bytes=encode(&raw);
            if path.ok_exists() {decode(&read_bounded(&path)?,kind,&hash)?;} else {
                use std::io::Write;
                let mut file=fs::OpenOptions::new().write(true).create_new(true).open(&path).map_err(|e|e.to_string())?;
                file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
            }
            let info=catalog.assets.entry(hash.clone()).or_insert_with(||AssetInfo {
                kind:kind.into(),raw_bytes:raw.len(),bytes:bytes.len(),labels:BTreeSet::new()});
            info.labels.insert(label);
        } else {unique.entry(hash.clone()).or_insert(raw);}
        references.push(ResourceRef{part,field:field.into(),kind:kind.into(),id:hash}); Ok(())
    })?;
    crate::session::OkSession::audit_preview_residue(&residue)?;
    // A resource shared elsewhere must never also be embedded in a card.
    unique.retain(|id,_|!catalog.assets.contains_key(id));
    let builtin_refs=references.iter().filter(|r|!unique.contains_key(&r.id)).count();
    let unique_bytes:usize=unique.values().map(Vec::len).sum();
    let report=serde_json::json!({"builtin_directory":root,"builtin_references":builtin_refs,
        "unique_resources":unique.len(),"unique_raw_bytes":unique_bytes,"parameter_state_bytes":residue.len(),
        "catalog_assets":catalog.assets.len(),"catalog_bytes":catalog.assets.values().map(|a|a.bytes).sum::<usize>(),
        "body_guid":card.body_guid,"head_id":card.head_id,"parts":session.parts.len(),
        "mod_guids":card.resolves.iter().map(|r|r.mod_id.clone()).collect::<BTreeSet<_>>()});
    catalog.imports.insert(source_name.into(),report.clone());
    // Unique temp file and rename publish a complete catalog; assets are immutable.
    let temporary=root.join(format!("catalog.{}.tmp",std::process::id()));
    fs::write(&temporary,serde_json::to_vec_pretty(&catalog).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
    fs::rename(&temporary,&catalog_path).map_err(|e|e.to_string())?;
    let raw=bincode::serialize(&SplitSnapshot{schema:1,residue,references,unique}).map_err(|e|e.to_string())?;
    Ok((raw,report))
}

pub(crate) fn load(raw: &[u8], card_path: &Path) -> Result<crate::session::OkSession,String> {
    let snapshot:SplitSnapshot=bincode::DefaultOptions::new().with_fixint_encoding()
        .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize(raw).map_err(|e|e.to_string())?;
    load_snapshot(&snapshot,card_path,|r|Ok(snapshot.unique.get(&r.id).cloned()))
}

pub(crate) fn load_snapshot(snapshot:&SplitSnapshot,card_path:&Path,
    mut unique:impl FnMut(&ResourceRef)->Result<Option<Vec<u8>>,String>)->Result<crate::session::OkSession,String> {
    if snapshot.schema!=1 {return Err("unsupported shared card schema".into());}
    crate::session::OkSession::audit_preview_residue(&snapshot.residue)?;
    let root=root(card_path)?;
    let mut cache=BTreeMap::<String,Vec<u8>>::new();
    let mut targets=BTreeSet::new();
    for reference in &snapshot.references {
        if !valid_id(&reference.id) || !targets.insert((reference.part,reference.field.clone())) {
            return Err("invalid or duplicate resource reference".into());
        }
    }
    crate::session::OkSession::merge_preview_resources(&snapshot.residue,&snapshot.references,|r| {
        if let Some(raw)=cache.get(&r.id) {return Ok(raw.clone());}
        if let Some(raw)=unique(r)? {
            if id(&r.kind,&raw)!=r.id {return Err("unique resource checksum mismatch".into());}
            cache.insert(r.id.clone(),raw.clone());return Ok(raw);
        }
        let path=root.join("assets").join(format!("{}.okasset",r.id));
        let bytes=read_bounded(&path).map_err(|e|format!("missing builtin resource {}: {e}",r.id))?;
        let raw=decode(&bytes,&r.kind,&r.id)?;cache.insert(r.id.clone(),raw.clone());Ok(raw)
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn hashes_and_checksums_are_content_bound() {
        let raw=b"shared mesh";let hash=id("model",raw);
        assert!(valid_id(&hash)); assert_ne!(hash,id("texture",raw));
        assert_eq!(decode(&encode(raw),"model",&hash).unwrap(),raw);
        assert!(decode(&encode(b"changed"),"model",&hash).is_err());
        assert!(!valid_id("../assets"));
    }

    #[test]
    #[ignore = "requires OK_TEST_SHARED_CARD and OK_TEST_SOURCE_CARD"]
    fn local_card_restores_all_assets_without_common_duplicates() {
        let path=PathBuf::from(std::env::var("OK_TEST_SHARED_CARD").unwrap());
        let source_card=PathBuf::from(std::env::var("OK_TEST_SOURCE_CARD").unwrap());
        let game=PathBuf::from(std::env::var("OK_TEST_GAME_ROOT").unwrap());
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM"] {std::env::set_var(key,"1");}
        let source=crate::session::open(&game,Some(&source_card),0).unwrap();
        // Compare the same extraction before/after split, not two separately
        // simulated dynamic poses (which can settle at different float values).
        let target=crate::portable::load(&path).unwrap();
        assert!(target.game_root.as_os_str().is_empty());
        let temporary=std::env::temp_dir().join(format!("ok-shared-roundtrip-{}-{}",std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let info=crate::card::parse_card_bytes_mode(&crate::gamefs::read(&source_card).unwrap(),0,false).unwrap();
        let (roundtrip,_)=split(&source,&info,&temporary,"roundtrip").unwrap();
        let original_root=std::env::var_os("OK_BUILTIN_RESOURCES");
        std::env::set_var("OK_BUILTIN_RESOURCES",&temporary);
        let restored=load(&roundtrip,&path).unwrap();
        if let Some(old)=original_root {std::env::set_var("OK_BUILTIN_RESOURCES",old);}else{std::env::remove_var("OK_BUILTIN_RESOURCES");}
        assert!(restored.game_root.as_os_str().is_empty());
        assert_eq!(source.parts.len(),restored.parts.len());
        for (a,b) in source.parts.iter().zip(&restored.parts) {
            assert_eq!(a.name,b.name);assert_eq!(a.role,b.role);
            assert!(a.rest_positions==b.rest_positions,"rest positions {}",a.name);assert!(a.positions==b.positions,"posed positions {}",a.name);
            assert_eq!(a.rest_normals,b.rest_normals);assert_eq!(a.normals,b.normals);
            assert_eq!(a.uvs,b.uvs);assert_eq!(a.uv2,b.uv2);assert_eq!(a.uv3,b.uv3);assert_eq!(a.uv4,b.uv4);
            assert_eq!(a.indices,b.indices);assert_eq!(a.weights,b.weights);assert_eq!(a.bone_idx,b.bone_idx);
            assert_eq!(a.bind_poses,b.bind_poses);assert_eq!(a.vertex_colors,b.vertex_colors);
            assert_eq!(a.morph_idx,b.morph_idx);assert_eq!(a.morph_dxyz,b.morph_dxyz);
            assert_eq!(a.png,b.png);assert_eq!(a.alpha_mask,b.alpha_mask);assert_eq!(a.hair_gloss,b.hair_gloss);
            assert_eq!(a.detail_mask,b.detail_mask);assert_eq!(a.line_mask,b.line_mask);
            assert_eq!(a.normal_map,b.normal_map);assert_eq!(a.normal_detail,b.normal_detail);assert_eq!(a.normal_mask,b.normal_mask);
            assert_eq!(a.overtex_png,b.overtex_png);assert_eq!(a.overtex2_png,b.overtex2_png);
            assert_eq!(a.shader_textures,b.shader_textures);assert_eq!(a.runtime_textures,b.runtime_textures);
            assert_eq!(serde_json::to_value(&a.source_material).unwrap(),serde_json::to_value(&b.source_material).unwrap());
            assert_eq!(serde_json::to_value(&a.item_draw).unwrap(),serde_json::to_value(&b.item_draw).unwrap());
            assert_eq!(serde_json::to_value(&a.eye_draw).unwrap(),serde_json::to_value(&b.eye_draw).unwrap());
            assert_eq!(serde_json::to_value(&a.iris_runtime).unwrap(),serde_json::to_value(&b.iris_runtime).unwrap());
            assert_eq!(serde_json::to_value(&a.runtime_accessory).unwrap(),serde_json::to_value(&b.runtime_accessory).unwrap());
        }
        assert_eq!(serde_json::to_value(&source.body_rest).unwrap(),serde_json::to_value(&restored.body_rest).unwrap());
        assert_eq!(serde_json::to_value(&source.head_rest).unwrap(),serde_json::to_value(&restored.head_rest).unwrap());
        assert_eq!(serde_json::to_value(&source.body_keys).unwrap(),serde_json::to_value(&restored.body_keys).unwrap());
        assert_eq!(serde_json::to_value(&source.head_keys).unwrap(),serde_json::to_value(&restored.head_keys).unwrap());
        let bytes=crate::gamefs::read(&path).unwrap();let range=crate::portable::png_snapshot(&bytes).unwrap().1.unwrap();
        let payload=&bytes[range];let size=u64::from_le_bytes(payload[12..20].try_into().unwrap());
        let raw=lz4_flex::block::decompress(&payload[24..],size as usize).unwrap();
        let mut split:SplitSnapshot=bincode::deserialize(&raw).unwrap();
        crate::session::OkSession::audit_preview_residue(&split.residue).unwrap();
        let directory=root(&path).unwrap();
        for id in split.unique.keys() {assert!(!directory.join("assets").join(format!("{id}.okasset")).ok_exists(),"card duplicates builtin");}
        assert!(split.references.iter().any(|r|r.field=="rest_positions" && !split.unique.contains_key(&r.id)));
        assert!(split.references.iter().any(|r|r.field=="material_definition" && !split.unique.contains_key(&r.id)));
        assert!(split.references.iter().any(|r|r.field=="body_keys" && !split.unique.contains_key(&r.id)));
        split.references.push(split.references[0].clone());
        let invalid=bincode::serialize(&split).unwrap();
        assert!(load(&invalid,&path).err().unwrap().contains("duplicate resource reference"));
        split.references.pop();split.references[0].id="../traversal".into();
        assert!(load(&bincode::serialize(&split).unwrap(),&path).err().unwrap().contains("invalid or duplicate"));
        let old_root=std::env::var_os("OK_BUILTIN_RESOURCES");
        std::env::set_var("OK_BUILTIN_RESOURCES",std::env::temp_dir().join(format!("ok-missing-builtins-{}",std::process::id())));
        let error=crate::portable::load(&path).err().expect("missing pack must fail");
        if let Some(old)=old_root {std::env::set_var("OK_BUILTIN_RESOURCES",old);}else{std::env::remove_var("OK_BUILTIN_RESOURCES");}
        assert!(error.contains("missing builtin resource"),"{error}");
    }
}
