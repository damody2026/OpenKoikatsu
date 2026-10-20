//! Indexed multi-outfit container. Inflate only the selected snapshot's assets.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::{collections::{BTreeMap,BTreeSet},path::Path};
use serde::{Serialize,Deserialize};
use bincode::Options;
use crate::{portable_resources::{self,SplitSnapshot},session::OkSession};

pub(crate) const MAGIC:&[u8;8]=b"OKPV0003";
const HEADER:usize=32;
pub(crate) const MAX_OUTFITS:usize=64;
#[derive(Serialize,Deserialize)]
struct Outfit {index:usize,snapshot:SplitSnapshot}
#[derive(Serialize,Deserialize)]
struct Span {offset:u64,size:u64}
#[derive(Serialize,Deserialize)]
struct Directory {schema:u32,outfits:Vec<Outfit>,unique:BTreeMap<String,Span>}

pub(crate) fn pack(snapshots:Vec<SplitSnapshot>,root:&Path)->Result<(Vec<u8>,serde_json::Value),String> {
    if snapshots.is_empty()||snapshots.len()>MAX_OUTFITS{return Err("invalid outfit count".into());}
    let mut unique=BTreeMap::<String,Vec<u8>>::new();let mut outfits=Vec::new();
    for (index,mut snapshot) in snapshots.into_iter().enumerate() {
        for (id,raw) in std::mem::take(&mut snapshot.unique) {
            if let Some(existing)=unique.get(&id) {if existing!=&raw{return Err("conflicting unique resources".into());}}
            else {unique.insert(id,raw);}
        }
        outfits.push(Outfit{index,snapshot});
    }
    // An asset classified common in any outfit must not remain embedded.
    unique.retain(|id,_|!root.join("assets").join(format!("{id}.okasset")).ok_exists());
    let raw_bytes:usize=unique.values().map(Vec::len).sum();
    let mut data=Vec::new();let mut spans=BTreeMap::new();
    for (id,raw) in unique {
        let compressed=portable_resources::encode(&raw);
        spans.insert(id,Span{offset:data.len() as u64,size:compressed.len() as u64});data.extend(compressed);
        if data.len() as u64>crate::portable::MAX_BYTES{return Err("multi-outfit unique data exceeds limit".into());}
    }
    let count=outfits.len();let unique_count=spans.len();
    let directory=Directory{schema:1,outfits,unique:spans};
    let raw=bincode::serialize(&directory).map_err(|e|e.to_string())?;
    if raw.len() as u64>crate::portable::MAX_BYTES{return Err("outfit directory exceeds limit".into());}
    let compressed=lz4_flex::block::compress(&raw);
    let mut payload=MAGIC.to_vec();payload.extend(1u32.to_le_bytes());payload.extend((raw.len() as u64).to_le_bytes());
    payload.extend(crate::portable::crc(&raw).to_le_bytes());payload.extend((compressed.len() as u64).to_le_bytes());
    payload.extend(compressed);payload.extend(data);
    if payload.len() as u64>crate::portable::MAX_BYTES{return Err("multi-outfit payload exceeds limit".into());}
    let payload_bytes=payload.len();
    Ok((payload,serde_json::json!({"outfit_count":count,"unique_resources":unique_count,"unique_raw_bytes":raw_bytes,
        "directory_raw_bytes":raw.len(),"card_payload_bytes":payload_bytes,"format":"OKPV0003","unique_deduplication":"across all outfits"})))
}

fn directory(payload:&[u8])->Result<(Directory,&[u8]),String> {
    if payload.len()<HEADER||&payload[..8]!=MAGIC{return Err("invalid multi-outfit header".into());}
    if u32::from_le_bytes(payload[8..12].try_into().unwrap())!=1{return Err("unsupported multi-outfit schema".into());}
    let size=u64::from_le_bytes(payload[12..20].try_into().unwrap());
    let compressed=u64::from_le_bytes(payload[24..32].try_into().unwrap());
    if size==0||size>crate::portable::MAX_BYTES||compressed==0||compressed>crate::portable::MAX_BYTES{return Err("invalid outfit directory size".into());}
    let end=HEADER.checked_add(compressed as usize).filter(|n|*n<=payload.len()).ok_or("truncated outfit directory")?;
    let raw=lz4_flex::block::decompress(&payload[HEADER..end],size as usize).map_err(|e|e.to_string())?;
    if crate::portable::crc(&raw)!=u32::from_le_bytes(payload[20..24].try_into().unwrap()){return Err("outfit directory checksum mismatch".into());}
    let d:Directory=bincode::DefaultOptions::new().with_fixint_encoding().with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes()
        .deserialize(&raw).map_err(|e|e.to_string())?;
    if d.schema!=1||d.outfits.is_empty()||d.outfits.len()>MAX_OUTFITS{return Err("invalid outfit directory".into());}
    let data=&payload[end..];let mut refs=BTreeSet::new();
    for (index,outfit) in d.outfits.iter().enumerate() {
        if outfit.index!=index||!outfit.snapshot.unique.is_empty(){return Err("invalid outfit index or nested unique data".into());}
        refs.extend(outfit.snapshot.references.iter().map(|r|r.id.as_str()));
    }
    let mut next=0u64;
    for (id,span) in &d.unique {
        if id.len()!=64||!id.bytes().all(|b|b.is_ascii_digit()||(b'a'..=b'f').contains(&b))||!refs.contains(id.as_str())||
            span.offset!=next||span.size<16{return Err("invalid unique asset directory".into());}
        next=next.checked_add(span.size).filter(|n|*n<=data.len() as u64).ok_or("truncated unique asset")?;
    }
    if next!=data.len() as u64{return Err("unexpected multi-outfit trailing data".into());}
    Ok((d,data))
}

/// Inspect provenance and globals without inflating unrelated renderer assets.
pub(crate) struct SnapshotReader<'a> {directory:Directory,data:&'a[u8],root:std::path::PathBuf}
impl<'a> SnapshotReader<'a> {
    pub(crate) fn new(payload:&'a[u8],root:&Path)->Result<Self,String>{
        let(directory,data)=directory(payload)?;Ok(Self{directory,data,root:root.into()})
    }
    pub(crate) fn snapshot(&self,index:usize)->Result<SplitSnapshot,String>{
        self.directory.outfits.get(index).map(|o|o.snapshot.clone()).ok_or("portable outfit index out of range".into())
    }
    pub(crate) fn resolve(&self,r:&portable_resources::ResourceRef)->Result<Vec<u8>,String>{
        if r.id.len()!=64||!r.id.bytes().all(|b|b.is_ascii_digit()||(b'a'..=b'f').contains(&b)){
            return Err("invalid resource identity".into());
        }
        let bytes=if let Some(span)=self.directory.unique.get(&r.id){
            self.data[span.offset as usize..(span.offset+span.size) as usize].to_vec()
        }else{portable_resources::read_bounded(&self.root.join("assets").join(format!("{}.okasset",r.id)))?};
        portable_resources::decode(&bytes,&r.kind,&r.id)
    }
}

/// A coordinate edit leaves every other outfit's exact resource snapshot intact.
/// Reuse compressed immutable assets directly instead of rebuilding other outfits.
pub(crate) fn replace_outfit(payload:&[u8],replacement:SplitSnapshot,index:usize,root:&Path)->Result<Vec<u8>,String> {
    replace_outfits(payload,vec![(index,replacement)],root)
}

/// One transaction for global material edits: repack the shared unique asset
/// table once, rather than copying the whole character once per outfit.
pub(crate) fn replace_outfits(payload:&[u8],replacements:Vec<(usize,SplitSnapshot)>,root:&Path)->Result<Vec<u8>,String> {
    let (mut d,old_data)=directory(payload)?;
    let mut new_unique=BTreeMap::new();let mut indices=BTreeSet::new();
    for(index,mut replacement)in replacements{
        if !indices.insert(index){return Err("duplicate edited outfit index".into());}
        let target=d.outfits.get_mut(index).ok_or("edited outfit index out of range")?;
        for(id,raw)in std::mem::take(&mut replacement.unique){
            if let Some(previous)=new_unique.get(&id){if previous!=&raw{return Err("conflicting edited unique resource".into());}}
            else{new_unique.insert(id,raw);}
        }
        target.snapshot=replacement;
    }
    let refs:BTreeSet<_>=d.outfits.iter().flat_map(|o|o.snapshot.references.iter().map(|r|r.id.clone())).collect();
    let mut encoded=BTreeMap::<String,Vec<u8>>::new();
    for id in refs {
        if root.join("assets").join(format!("{id}.okasset")).ok_is_file(){continue;}
        if let Some(raw)=new_unique.get(&id){encoded.insert(id,portable_resources::encode(raw));}
        else if let Some(span)=d.unique.get(&id){encoded.insert(id,old_data[span.offset as usize..(span.offset+span.size) as usize].to_vec());}
        // References absent from the unique table remain application resources;
        // load_snapshot validates their content-addressed files when consumed.
    }
    d.unique.clear();let mut data=Vec::new();
    for(id,bytes)in encoded{d.unique.insert(id,Span{offset:data.len() as u64,size:bytes.len() as u64});data.extend(bytes);}
    let raw=bincode::serialize(&d).map_err(|e|e.to_string())?;
    if raw.len() as u64>crate::portable::MAX_BYTES{return Err("outfit directory exceeds limit".into());}
    let compressed=lz4_flex::block::compress(&raw);
    let mut out=MAGIC.to_vec();out.extend(1u32.to_le_bytes());out.extend((raw.len() as u64).to_le_bytes());out.extend(crate::portable::crc(&raw).to_le_bytes());out.extend((compressed.len() as u64).to_le_bytes());out.extend(compressed);out.extend(data);
    if out.len() as u64>crate::portable::MAX_BYTES{return Err("edited outfit payload exceeds limit".into());}
    directory(&out)?;Ok(out)
}

#[cfg(test)]
#[test]
fn batch_outfit_replacement_matches_serial_and_rejects_duplicate_indices(){
    let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NonexistentOutfitResourceRoot");
    let snapshot=|value:u8|{let bytes=vec![value;100];let id=portable_resources::id("test",&bytes);SplitSnapshot{schema:1,residue:vec![value],references:vec![portable_resources::ResourceRef{part:Some(0),field:"png".into(),kind:"test".into(),id:id.clone()}],unique:BTreeMap::from([(id,bytes)])}};
    let(original,_)=pack(vec![snapshot(1),snapshot(2),snapshot(3)],root).unwrap();
    let serial=replace_outfit(&replace_outfit(&original,snapshot(4),0,root).unwrap(),snapshot(5),2,root).unwrap();
    let batch=replace_outfits(&original,vec![(0,snapshot(4)),(2,snapshot(5))],root).unwrap();
    assert_eq!(batch,serial);
    assert!(replace_outfits(&original,vec![(0,snapshot(4)),(0,snapshot(5))],root).is_err());
}

#[cfg(test)]
#[test]
fn replace_outfit_preserves_other_snapshots_and_prunes_only_unused_unique_assets(){
    let root=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/NonexistentOutfitResourceRoot");
    let snapshot=|value:u8|{let bytes=vec![value;100];let id=portable_resources::id("test",&bytes);SplitSnapshot{schema:1,residue:vec![value],references:vec![portable_resources::ResourceRef{part:Some(0),field:"png".into(),kind:"test".into(),id:id.clone()}],unique:BTreeMap::from([(id,bytes)])}};
    let(original,_)=pack(vec![snapshot(1),snapshot(2)],root).unwrap();let(before,_)=directory(&original).unwrap();
    let previous=bincode::serialize(&before.outfits[0].snapshot).unwrap();
    let changed=replace_outfit(&original,snapshot(3),1,root).unwrap();let(after,data)=directory(&changed).unwrap();
    assert_eq!(after.outfits.len(),2);assert_eq!(bincode::serialize(&after.outfits[0].snapshot).unwrap(),previous);
    for value in [1u8,3]{let id=portable_resources::id("test",&vec![value;100]);let span=&after.unique[&id];assert_eq!(portable_resources::decode(&data[span.offset as usize..(span.offset+span.size) as usize],"test",&id).unwrap(),vec![value;100]);}
    assert!(!after.unique.contains_key(&portable_resources::id("test",&vec![2;100])));
    assert!(replace_outfit(&original,snapshot(4),2,root).is_err());
    let mut damaged=original.clone();damaged[20]^=1;assert!(replace_outfit(&damaged,snapshot(4),1,root).is_err());
}

/// Expose immutable embedded resources to exact clothing-state snapshots.
pub(crate) fn embedded_ids(payload:&[u8])->Result<BTreeSet<String>,String> {
 let (d,_)=directory(payload)?;Ok(d.unique.keys().cloned().collect())
}
pub(crate) fn load_clothing(snapshot:&SplitSnapshot,payload:&[u8],path:&Path)->Result<OkSession,String> {
 let (d,data)=directory(payload)?;
 portable_resources::load_snapshot(snapshot,path,|r| {
  if let Some(raw)=snapshot.unique.get(&r.id){return Ok(Some(raw.clone()));}
  match d.unique.get(&r.id){Some(span)=>portable_resources::decode(&data[span.offset as usize..(span.offset+span.size) as usize],&r.kind,&r.id).map(Some),None=>Ok(None)}
 })
}

pub(crate) fn load(payload:&[u8],path:&Path,index:i32)->Result<OkSession,String> {
    let (d,data)=directory(payload)?;
    let outfit=usize::try_from(index).ok().and_then(|n|d.outfits.get(n)).ok_or("portable outfit index out of range")?;
    let mut session=portable_resources::load_snapshot(&outfit.snapshot,path,|r| {
        match d.unique.get(&r.id) {
            Some(span)=>portable_resources::decode(&data[span.offset as usize..(span.offset+span.size) as usize],&r.kind,&r.id).map(Some),
            None=>Ok(None),
        }
    })?;
    session.coordinate_count=d.outfits.len();Ok(session)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]fn rejects_invalid_multi_headers_and_indexes() {
        assert!(directory(&[]).is_err());
        let root=std::env::temp_dir();
        assert!(pack(vec![],&root).is_err());
        let snapshot=||SplitSnapshot{schema:1,residue:vec![],references:vec![],unique:BTreeMap::new()};
        let (bytes,_)=pack(vec![snapshot(),snapshot()],&root).unwrap();
        assert_eq!(directory(&bytes).unwrap().0.outfits.len(),2);
        assert!(load(&bytes,&root,-1).err().unwrap().contains("out of range"));
        assert!(load(&bytes,&root,2).err().unwrap().contains("out of range"));
        let mut bad=bytes.clone();bad[20]^=1;assert!(directory(&bad).is_err());
        let mut bad=bytes;bad.pop();assert!(directory(&bad).is_err());
    }
    #[test]fn identical_unique_data_is_stored_once_across_outfits() {
        use sha2::{Digest,Sha256};
        let raw=b"unique outfit texture".to_vec();let mut hash=Sha256::new();hash.update(b"texture\0");hash.update(&raw);
        let id=format!("{:x}",hash.finalize());
        let snapshot=||SplitSnapshot{schema:1,residue:vec![],references:vec![portable_resources::ResourceRef{
            part:None,field:"test".into(),kind:"texture".into(),id:id.clone()}],unique:BTreeMap::from([(id.clone(),raw.clone())])};
        let (bytes,report)=pack(vec![snapshot(),snapshot()],&std::env::temp_dir()).unwrap();
        let (directory,data)=directory(&bytes).unwrap();assert_eq!(directory.unique.len(),1);assert_eq!(report["unique_resources"],1);
        assert_eq!(data.len(),portable_resources::encode(&raw).len());
        assert_eq!(portable_resources::decode(data,"texture",&id).unwrap(),raw);
    }

    #[test]
    #[ignore="requires a local multi-outfit card via OK_TEST_MULTI_OUTFIT_CARD"]
    fn local_shape_updates_full_hair_tree_without_accumulating() {
        let path=std::env::var("OK_TEST_MULTI_OUTFIT_CARD").unwrap();
        let mut session=crate::portable::load_outfit(Path::new(&path),4).unwrap();
        let face=session.shape_face.clone();let original=session.shape_body.clone();
        // This mod head lacks a separate full-hair prefab. Use its complete
        // accessory tree as a fixture for the same full-tree update contract.
        session.head_full_rest=session.head_rest.clone();session.apply_shape(&original,&face);
        let original_hair=session.head_full_posed.as_ref().unwrap().world_tf("cf_J_FaceUp_ty").unwrap();
        let mut changed=false;
        for height in [0.,1.,0.222,0.222,original[0]] {
            let mut body=original.clone();body[0]=height;
            let mut edited_face=face.clone();if height!=original[0]{edited_face[0]=height;}
            session.apply_shape(&body,&edited_face);
            let mut neutral=session.body_rest.clone().unwrap();
            let scale=crate::shape::apply_body_shape_keys(&mut neutral,&session.body_keys,&body,&session.correct,
                session.sex,session.type_bone,session.nip_stand_rate);
            let mut expected=session.head_full_rest.clone().unwrap();
            let correction=1./(1.+session.correct.get(2).map(|c|c.scl[1]).unwrap_or(0.));
            crate::shape::apply_head_shape_keys(&mut expected,&session.head_keys,&edited_face,scale,correction,session.type_bone);
            let names=session.abm.iter().map(|b|b.name.clone()).collect::<Vec<_>>();
            let baseline=crate::pose::capture_baseline(&expected,&names);crate::pose::apply_abm(&mut expected,&session.abm,0,&baseline);
            let actual=session.head_full_posed.as_ref().unwrap().world_tf("cf_J_FaceUp_ty").unwrap();
            assert_eq!(actual,expected.world_tf("cf_J_FaceUp_ty").unwrap());changed|=actual!=original_hair;
        }
        assert!(changed,"hair parent must follow face shape edits");
        assert_eq!(session.head_full_posed.as_ref().unwrap().world_tf("cf_J_FaceUp_ty").unwrap(),original_hair);
    }

    #[test]
    #[ignore="diagnostic head-weight extraction; requires original card/game and OK_HEAD_PROBE_OUTPUT"]
    fn local_export_head_skin_probe() {
        let original=std::env::var("OK_TEST_SOURCE_CARD").unwrap();let game=std::env::var("OK_TEST_GAME_ROOT").unwrap();
        let output=std::env::var("OK_HEAD_PROBE_OUTPUT").unwrap();
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"] {std::env::set_var(key,"1");}
        let session=crate::session::open(Path::new(&game),Some(Path::new(&original)),4).unwrap();
        let part=session.parts.iter().find(|p|p.name.eq_ignore_ascii_case("cf_O_face")).unwrap();
        assert_eq!(part.skel_kind,2);assert_eq!(part.weights.len(),part.rest_positions.len());
        let raw=session.to_preview_bytes().unwrap();let mut bytes=b"OKPV0001".to_vec();
        bytes.extend(1u32.to_le_bytes());bytes.extend((raw.len() as u64).to_le_bytes());bytes.extend(crate::portable::crc(&raw).to_le_bytes());
        bytes.extend(lz4_flex::block::compress(&raw));let pack=Path::new(&output).with_extension("okpreview");
        std::fs::write(&pack,bytes).unwrap();crate::portable::embed(Path::new(&original),&pack,Path::new(&output)).unwrap();
    }

    #[test]
    #[ignore="requires OK_TEST_MULTI_OUTFIT_CARD and original card/game"]
    fn local_every_outfit_restores_source_resources_and_global_shapes() {
        let card=std::env::var("OK_TEST_MULTI_OUTFIT_CARD").unwrap();
        let original=std::env::var("OK_TEST_SOURCE_CARD").unwrap();let game=std::env::var("OK_TEST_GAME_ROOT").unwrap();
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"] {std::env::set_var(key,"1");}
        let info=crate::card::parse_card(Path::new(&original)).unwrap();let count=info.coordinate_count.max(1);
        let mut signatures=BTreeSet::new();
        for index in 0..count {
            let source=crate::session::open(Path::new(&game),Some(Path::new(&original)),index as i32).unwrap();
            let mut loaded=crate::portable::load_outfit(Path::new(&card),index as i32).unwrap();
            assert!(loaded.game_root.as_os_str().is_empty());assert_eq!(loaded.coordinate_count,count);assert_eq!(source.parts.len(),loaded.parts.len());
            for (a,b) in source.parts.iter().zip(&loaded.parts) {
                assert_eq!(a.name,b.name);assert_eq!(a.role,b.role);assert_eq!(a.rest_positions,b.rest_positions);assert_eq!(a.indices,b.indices);
                assert_eq!(a.uvs,b.uvs);assert_eq!(a.weights,b.weights);assert_eq!(a.bone_idx,b.bone_idx);
                assert_eq!(a.png,b.png);assert_eq!(a.normal_map,b.normal_map);assert_eq!(a.alpha_mask,b.alpha_mask);
                assert_eq!(a.shader_textures,b.shader_textures);assert_eq!(a.runtime_textures,b.runtime_textures);
                assert_eq!(serde_json::to_value(&a.source_material).unwrap(),serde_json::to_value(&b.source_material).unwrap());
            }
            signatures.insert(loaded.parts.iter().map(|p|(p.name.clone(),p.indices.len())).collect::<Vec<_>>());
            let before=loaded.ensure_skel_export().bones.iter().map(|b|b.pose_scl).collect::<Vec<_>>();
            let mut body=loaded.shape_body.clone();let face=loaded.shape_face.clone();body[0]=1.;loaded.apply_shape(&body,&face);
            assert!(loaded.ensure_skel_export().bones.iter().zip(before).any(|(a,b)|a.pose_scl!=b));
            eprintln!("multi outfit {index}: {} parts restored; geometry/textures/materials/shape PASS",loaded.parts.len());
        }
        eprintln!("multi outfit source count={count}, distinct part layouts={}",signatures.len());
        assert!(crate::portable::load_outfit(Path::new(&card),count as i32).is_err());
    }
}
