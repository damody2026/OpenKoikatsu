//! Local prototype snapshot. Internal schema, not the public PNG card format.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::{fs, io::{Read, Write}, path::Path, time::Instant};
use crate::session::OkSession;

pub(crate) const MAX_BYTES: u64 = 512 * 1024 * 1024;
const MAGIC: &[u8; 8] = b"OKPV0001";
const SHARED_MAGIC: &[u8; 8] = b"OKPV0002";
const HEADER: usize = 24;
// Bump whenever PreviewSnapshot or one of its serialized members changes.
const SCHEMA: u32 = 1;
const PNG: &[u8; 8] = b"\x89PNG\r\n\x1a\n";
const CHUNK: &[u8; 4] = b"okPv";

#[path = "portable_hair.rs"]
mod custom_hair;
pub use custom_hair::{inspect_hair_base, import_hair_card};

pub(crate) fn png_snapshot(bytes: &[u8]) -> Result<(usize, Option<std::ops::Range<usize>>), String> {
    if bytes.len()<8 || &bytes[..8]!=PNG {return Err("invalid PNG signature".into());}
    let mut pos=8usize;let mut snapshot=None;
    while pos.checked_add(12).is_some_and(|end|end<=bytes.len()) {
        let len=u32::from_be_bytes(bytes[pos..pos+4].try_into().unwrap()) as usize;
        let end=pos.checked_add(12).and_then(|p|p.checked_add(len)).ok_or("PNG length overflow")?;
        if end>bytes.len() {return Err("truncated PNG chunk".into());}
        let kind=&bytes[pos+4..pos+8];
        if kind==CHUNK {
            if snapshot.is_some() {return Err("duplicate portable PNG chunk".into());}
            if crc(&bytes[pos+4..end-4])!=u32::from_be_bytes(bytes[end-4..end].try_into().unwrap()) {return Err("portable PNG chunk CRC mismatch".into());}
            snapshot=Some(pos+8..end-4);
        }
        if kind==b"IEND" {
            if len!=0 {return Err("invalid IEND".into());}
            return Ok((pos,snapshot));
        }
        pos=end;
    }
    Err("missing PNG IEND".into())
}

pub(crate) fn crc(bytes: &[u8]) -> u32 {
    crc32fast::hash(bytes)
}

/// Replace only the portable resource chunk, retaining the exact card blocks.
/// Sources are already prepared and owned; saving never resolves game assets.
pub(crate) fn write_snapshot_card(raw:&[u8],payload:&[u8],output:&Path)->Result<(),String> {
    let(iend,old)=png_snapshot(raw)?;
    let length=raw.len().checked_add(payload.len()).and_then(|n|n.checked_add(12)).ok_or("snapshot card size overflow")?;
    if length as u64>MAX_BYTES{return Err("snapshot card exceeds limit".into());}
    let mut bytes=raw[..iend].to_vec();
    if let Some(old)=old{bytes.drain(old.start-8..old.end+4);}
    let mut chunk=CHUNK.to_vec();chunk.extend(payload);
    bytes.extend(u32::try_from(payload.len()).map_err(|_|"snapshot chunk size overflow")?.to_be_bytes());
    bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&raw[iend..]);
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())
}

#[test]
fn portable_crc_preserves_format_and_checksums() {
    fn reference(bytes:&[u8])->u32 {let mut v=!0u32;for b in bytes {v^=u32::from(*b);for _ in 0..8 {v=(v>>1)^(0xedb88320&(0u32.wrapping_sub(v&1)));}}!v}
    assert_eq!(crc(b"123456789"),0xcbf43926);
    let mut data=Vec::new();let mut seed=0x12345678u32;
    for i in 0..65537 {seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);data.push((seed>>24) as u8);
        if [0,1,7,15,255,4095,65536].contains(&i) {assert_eq!(crc(&data),reference(&data));}}
    assert_eq!(crc(&[]),reference(&[]));
}

pub(crate) fn decode_at(bytes: &[u8], path: Option<&Path>,outfit:i32) -> Result<OkSession, String> {
    if bytes.starts_with(crate::portable_outfits::MAGIC) {
        return crate::portable_outfits::load(bytes,path.ok_or("multi-outfit card needs its file path")?,outfit);
    }
    if outfit!=0{return Err("legacy portable card contains outfit 0 only".into());}
    if bytes.len() < HEADER || (&bytes[..8] != MAGIC && &bytes[..8] != SHARED_MAGIC) { return Err("invalid portable pack header".into()); }
    if u32::from_le_bytes(bytes[8..12].try_into().unwrap()) != SCHEMA { return Err("unsupported portable schema".into()); }
    let size = u64::from_le_bytes(bytes[12..20].try_into().unwrap());
    if size == 0 || size > MAX_BYTES { return Err("portable payload exceeds size limit".into()); }
    let expected = u32::from_le_bytes(bytes[20..24].try_into().unwrap());
    let raw = lz4_flex::block::decompress(&bytes[HEADER..], size as usize).map_err(|e| e.to_string())?;
    if raw.len() != size as usize || crc(&raw) != expected { return Err("portable payload checksum mismatch".into()); }
    if &bytes[..8]==SHARED_MAGIC {
        crate::portable_resources::load(&raw,path.ok_or("shared card needs its file path")?)
    } else {OkSession::from_preview_bytes(&raw)}
}

#[cfg(test)]
fn decode(bytes: &[u8]) -> Result<OkSession,String> {decode_at(bytes,None,0)}

pub fn load(path: &Path) -> Result<OkSession, String> {
    load_outfit(path,0)
}

/// APNG display follows editable Custom values rather than re-executing
/// source shape plugins. Incremental renderer candidates must use this exact
/// same normalization, while retaining their source extraction context.
/// Returns whether the shape was (re)applied and every renderer reskinned.
pub(crate) fn apply_editable_shape_parameters(session:&mut OkSession,card:&crate::card::CardInfo,outfit:i32)
    ->Result<bool,String>{
    if card.sex!=session.sex || card.shape_body.len()!=session.shape_body.len()
        || card.shape_face.len()!=session.shape_face.len()
        || card.shape_body.iter().chain(&card.shape_face).any(|v|!v.is_finite()) {
        return Err("PNG parameters do not match portable snapshot".into());
    }
    let active_abm:Vec<_>=card.abm.iter().cloned().map(|mut b|{let d=b.for_coord(outfit.max(0) as usize);b.coord=vec![d];b}).collect();
    let abm_changed=active_abm!=session.abm||session.bust_softness!=card.bust_softness||session.bust_weight!=card.bust_weight;
    session.bust_softness=card.bust_softness;session.bust_weight=card.bust_weight;
    if abm_changed {session.abm=active_abm;}
    if abm_changed || card.shape_body!=session.shape_body || card.shape_face!=session.shape_face {
        session.apply_shape(&card.shape_body,&card.shape_face);session.skin_all();
        return Ok(true);
    }
    Ok(false)
}
/// Display-shape inputs that determine the result of the function above.
pub(crate) fn editable_shape_key(card:&crate::card::CardInfo,outfit:i32)->Result<Vec<u8>,String>{
    let active_abm:Vec<_>=card.abm.iter().cloned().map(|mut b|{let d=b.for_coord(outfit.max(0) as usize);b.coord=vec![d];b}).collect();
    bincode::serialize(&(card.sex,&card.shape_body,&card.shape_face,active_abm,card.bust_softness,card.bust_weight)).map_err(|e|e.to_string())
}

pub fn load_outfit(path:&Path,outfit:i32)->Result<OkSession,String> {
    let profile=std::env::var_os("OK_PORTABLE_PROFILE").is_some();let started=Instant::now();
    let file = fs::File::open(path).map_err(|e| e.to_string())?;
    if file.metadata().map_err(|e| e.to_string())?.len() > MAX_BYTES + HEADER as u64 { return Err("portable file exceeds size limit".into()); }
    let mut bytes = Vec::new();
    file.take(MAX_BYTES + HEADER as u64 + 1).read_to_end(&mut bytes).map_err(|e| e.to_string())?;
    if bytes.len() as u64 > MAX_BYTES + HEADER as u64 { return Err("portable file exceeds size limit".into()); }
    if bytes.starts_with(PNG) {
        if profile{eprintln!("portable read {:.3}s",started.elapsed().as_secs_f64());}
        let (_,range)=png_snapshot(&bytes)?;
        let decoding=Instant::now();
        let mut session=decode_at(&bytes[range.ok_or("PNG has no portable snapshot")?],Some(path),outfit)?;
        if profile{eprintln!("portable decode {:.3}s",decoding.elapsed().as_secs_f64());}let parsing=Instant::now();
        // Use editable Custom values. Original plugin data is preserved in PNG,
        // but source runtime plugins (e.g. PushUp overrides) are not executed.
        let card=crate::card::parse_card_bytes_mode(&bytes,outfit,false)?;
        let requested=crate::unity::requested_clothes_states();
        if requested.iter().any(|state|*state!=0) {
            session=load_clothes_variant(path,&bytes,outfit,session.coordinate_count,requested)?;
        }
        restore_common_renderer_defaults(&mut session,&card,outfit,path)?;
        if profile{eprintln!("portable parameters {:.3}s",parsing.elapsed().as_secs_f64());}
        apply_editable_shape_parameters(&mut session,&card,outfit)?;
        crate::pregnancy::apply(&mut session,&card.pregnancy);
        session.refresh_portable_render();
        if profile{eprintln!("portable total {:.3}s",started.elapsed().as_secs_f64());}
        Ok(session)
    } else {
        let mut session=decode_at(&bytes,Some(path),outfit)?;
        session.refresh_portable_render();
        Ok(session)
    }
}

// Compatibility for APNGs prepared by the older sibling-enabling bug. Defaults
// are common prefab metadata, never a card/name exception or game-file lookup.
// Explicit card enable rows remain authoritative. Unknown/null materials still
// fail source-material validation rather than becoming a white placeholder.
fn restore_common_renderer_defaults(session:&mut OkSession,card:&crate::card::CardInfo,outfit:i32,path:&Path)->Result<(),String>{
    let Ok(root)=crate::portable_resources::root(path) else{return Ok(());};
    let file=root.join("renderer_defaults.json");if !file.ok_is_file(){return Ok(());}
    let bytes=crate::gamefs::read(file).map_err(|e|e.to_string())?;
    if bytes.len()>8*1024*1024{return Err("common renderer defaults exceed size limit".into());}
    let data:serde_json::Value=serde_json::from_slice(&bytes).map_err(|e|e.to_string())?;
    if data["schema"]!=1{return Err("common renderer default schema invalid".into());}
    for part in &mut session.parts {
        if part.source_material.is_some()||part.role!="accessory"{continue;}
        let Some(runtime)=part.runtime_accessory.as_ref() else{continue;};let source=runtime.source_mesh();
        let known_disabled=!source.renderer_ids.is_empty()&&source.renderer_ids.iter().all(|id|{
            let item=&data["renderers"][format!("{}:{id}",source.path_id)];
            item["enabled"]==false&&source.renderer_names.iter().any(|name|item["name"].as_str()==Some(name.to_ascii_lowercase().as_str()))
        });
        let enabled=card.me_renderer_on.iter().any(|(ot,coord,_,name)|*ot==2&&*coord==outfit&&source.renderer_names.iter().any(|n|n.eq_ignore_ascii_case(name)));
        if known_disabled&&!enabled {part.render_visible=false;}
    }
    Ok(())
}

/// Old APNGs contain only the worn prefab nodes. Never simulate half-undressing
/// by hiding arbitrary geometry. Prepare an exact native state snapshot once,
/// using its verified conversion receipt; subsequent loads need no game files.
pub(crate) fn clothing_resource_digest(png:&[u8])->Result<String,String> {
    use sha2::{Digest,Sha256};
    let (_,range)=png_snapshot(png)?;
    let range=range.ok_or("PNG has no portable snapshot")?;
    Ok(format!("{:x}",Sha256::digest(&png[range])))
}

const CLOTHING_MAGIC:&[u8;8]=b"OKPV0004";
#[derive(serde::Serialize,serde::Deserialize)]
struct ClothingSnapshot {schema:u32,base_digest:String,snapshot:crate::portable_resources::SplitSnapshot}
fn decode_clothing(payload:&[u8],png:&[u8],path:&Path)->Result<OkSession,String> {
 if !payload.starts_with(CLOTHING_MAGIC){return decode_at(payload,None,0);}
 if payload.len()<HEADER||payload.len() as u64>MAX_BYTES+HEADER as u64||u32::from_le_bytes(payload[8..12].try_into().unwrap())!=SCHEMA{return Err("invalid clothing snapshot header".into());}
 let size=u64::from_le_bytes(payload[12..20].try_into().unwrap());if size==0||size>MAX_BYTES{return Err("clothing snapshot exceeds size limit".into());}
 let raw=lz4_flex::block::decompress(&payload[HEADER..],size as usize).map_err(|e|e.to_string())?;
 if crc(&raw)!=u32::from_le_bytes(payload[20..24].try_into().unwrap()){return Err("clothing snapshot checksum mismatch".into());}
 use bincode::Options;
 let state:ClothingSnapshot=bincode::DefaultOptions::new().with_fixint_encoding().with_limit(MAX_BYTES).reject_trailing_bytes().deserialize(&raw).map_err(|e|e.to_string())?;
 if state.schema!=1||state.base_digest!=clothing_resource_digest(png)?{return Err("clothing snapshot base resource mismatch".into());}
 let (_,range)=png_snapshot(png)?;let base=&png[range.ok_or("missing base snapshot")?];
 crate::portable_outfits::load_clothing(&state.snapshot,base,path)
}
fn encode_clothing(session:&OkSession,png:&[u8],path:&Path,outfit:i32)->Result<Vec<u8>,String> {
 let (_,range)=png_snapshot(png)?;let base=&png[range.ok_or("missing base snapshot")?];
 // Compatibility with legacy single-outfit packs; their prepared states stay
 // self-contained. Indexed cards can borrow immutable assets by content hash.
 let (magic,raw)=if base.starts_with(crate::portable_outfits::MAGIC){
  let embedded=crate::portable_outfits::embedded_ids(base)?;let root=crate::portable_resources::root(path)?;
  let card=crate::card::parse_card_bytes_mode(png,outfit,false)?;
  let mut references=Vec::new();let mut unique=std::collections::BTreeMap::new();
  let residue=session.split_preview_resources(&card,|part,field,kind,raw,_|{
   let id=crate::portable_resources::id(kind,&raw);
   if !embedded.contains(&id)&&!root.join("assets").join(format!("{id}.okasset")).ok_is_file(){unique.entry(id.clone()).or_insert(raw);}
   references.push(crate::portable_resources::ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
  })?;
  let state=ClothingSnapshot{schema:1,base_digest:clothing_resource_digest(png)?,snapshot:crate::portable_resources::SplitSnapshot{schema:1,residue,references,unique}};
  (CLOTHING_MAGIC,bincode::serialize(&state).map_err(|e|e.to_string())?)
 }else{(MAGIC,session.to_preview_bytes()?)};
 if raw.len() as u64>MAX_BYTES{return Err("clothing state snapshot exceeds size limit".into());}
 let mut payload=magic.to_vec();payload.extend(SCHEMA.to_le_bytes());payload.extend((raw.len() as u64).to_le_bytes());payload.extend(crc(&raw).to_le_bytes());payload.extend(lz4_flex::block::compress(&raw));Ok(payload)
}

/// Newly edited cards retain complete source metadata inside the PNG. Recreate
/// a native source beside the immutable state cache, so saving/moving a card
/// does not depend on the editor's temporary working files.
pub(crate) fn clothing_source(path:&Path,png:&[u8],metadata:&serde_json::Value)->Result<(std::path::PathBuf,std::path::PathBuf),String>{
 use sha2::{Digest,Sha256};
 let explicit=metadata["game_root"].as_str().map(std::path::PathBuf::from).filter(|p|p.join("abdata").ok_is_dir());
 if metadata["source_embedded"]==true {
  let (_,span)=png_snapshot(png)?;let span=span.ok_or("edited card snapshot missing")?;let mut raw=png.to_vec();raw.drain(span.start-8..span.end+4);
  let source=path.parent().ok_or("card directory missing")?.join(".okstates").join(clothing_resource_digest(png)?).join("source.png");
  if !source.ok_exists(){publish_clothing_cache(&source,&raw)?;}
  let root=explicit.ok_or("請設定可用的原始遊戲目錄，以準備新外觀的服裝狀態")?;return Ok((root,source));
 }
 let source=std::path::PathBuf::from(metadata["source"].as_str().ok_or("conversion receipt has no source")?);
 let bytes=crate::gamefs::read(&source).map_err(|_|"Requested clothing source is unavailable".to_string())?;
 if metadata["source_sha256"].as_str()!=Some(format!("{:x}",Sha256::digest(&bytes)).as_str()){return Err("Original card changed since conversion".into());}
 let root=explicit.or_else(||source.ancestors().find(|p|p.join("abdata").ok_is_dir()).map(Path::to_path_buf)).ok_or("original clothing resources unavailable")?;Ok((root,source))
}

fn load_clothes_variant(path:&Path,png:&[u8],outfit:i32,count:usize,states:[u8;9])->Result<OkSession,String> {
    use sha2::{Digest,Sha256};
    let digest=format!("{:x}",Sha256::digest(png));
    let key=states.iter().map(u8::to_string).collect::<Vec<_>>().join("");
    // Preview and editable Custom values are not immutable model resources.
    // Key by the validated embedded snapshot; apply current Custom after loading.
    let parent=path.parent().ok_or("portable card has no directory")?;
    let directory=parent.join(".okstates").join(clothing_resource_digest(png)?);
    let cache=directory.join(format!("outfit{outfit}_{key}.okpreview"));
    if cache.ok_is_file() {
        let mut session=decode_clothing(&crate::gamefs::read(&cache).map_err(|e|e.to_string())?,png,path)?;session.coordinate_count=count;
        return Ok(session);
    }
    // E290 used the full PNG hash. Read and migrate that layout before touching
    // source resources, so old prepared cards remain usable offline.
    let legacy=parent.join(".okstates").join(&digest).join(format!("outfit{outfit}_{key}.okpreview"));
    if legacy.ok_is_file() {
        let payload=crate::gamefs::read(&legacy).map_err(|e|e.to_string())?;
        let mut session=decode_at(&payload,None,0)?;session.coordinate_count=count;
        if let Err(error)=publish_clothing_cache(&cache,&payload){eprintln!("portable legacy clothing cache migration deferred: {error}");}
        return Ok(session);
    }
    let receipt=path.with_extension("verified.json");
    let metadata:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&receipt)
        .map_err(|_|"This APNG contains worn clothing only. Its requested clothing state has not been prepared; keep the verified conversion receipt and original source available for first preparation.".to_string())?)
        .map_err(|e|e.to_string())?;
    if metadata["apng_sha256"].as_str()!=Some(digest.as_str()) {return Err("APNG conversion receipt hash mismatch; clothing state preparation refused".into());}
    let (root,source)=clothing_source(path,png,&metadata)?;
    let mut session=crate::session::open(&root,Some(&source),outfit)?;
    session.refresh_portable_render();
    let payload=encode_clothing(&session,png,path,outfit)?;
    // Decode before publication, including all borrowed-resource hashes.
    let mut independent=decode_clothing(&payload,png,path)?;independent.coordinate_count=count;
    publish_clothing_cache(&cache,&payload)?;
    eprintln!("portable clothing state prepared: outfit={outfit} states={key} cache={}",cache.display());
    Ok(independent)
}

/// Unique temporary files avoid stale process IDs and concurrent preparation
/// collisions. An unsuccessful write is removed, never published as a state.
pub(crate) fn publish_clothing_cache(cache:&Path,payload:&[u8])->Result<(),String> {
    use std::sync::atomic::{AtomicU64,Ordering};
    static NEXT:AtomicU64=AtomicU64::new(0);
    let directory=cache.parent().ok_or("clothing cache has no directory")?;
    fs::create_dir_all(directory).map_err(|e|e.to_string())?;
    let mut pending=None;
    for _ in 0..32 {
        let temporary=directory.join(format!(".state_{}_{}.tmp",std::process::id(),NEXT.fetch_add(1,Ordering::Relaxed)));
        match fs::OpenOptions::new().write(true).create_new(true).open(&temporary) {
            Ok(file)=>{pending=Some((temporary,file));break;},
            Err(e) if e.kind()==std::io::ErrorKind::AlreadyExists=>continue,
            Err(e)=>return Err(e.to_string()),
        }
    }
    let (temporary,mut file)=pending.ok_or("Unable to allocate clothing cache temporary file")?;
    let written=file.write_all(payload).and_then(|_|file.sync_all());drop(file);
    let result=written.and_then(|_|fs::rename(&temporary,cache));
    // Another worker may have published the identical immutable state first.
    let result=match result {Err(_) if crate::gamefs::read(cache).ok().as_deref()==Some(payload)=>Ok(()),other=>other};
    let _=fs::remove_file(&temporary);
    result.map_err(|e|e.to_string())
}


/// Carry prepared variants when saving a card to another directory. Geometry
/// compatibility is defined by the embedded resource snapshot, not file name.
pub(crate) fn copy_clothing_states(source:&Path,output:&Path)->Result<usize,String> {
    use sha2::{Digest,Sha256};
    fn read(path:&Path)->Result<Vec<u8>,String>{let file=fs::File::open(path).map_err(|e|e.to_string())?;if file.metadata().map_err(|e|e.to_string())?.len()>MAX_BYTES+HEADER as u64{return Err("portable file exceeds size limit".into());}let mut bytes=Vec::new();file.take(MAX_BYTES+HEADER as u64+1).read_to_end(&mut bytes).map_err(|e|e.to_string())?;Ok(bytes)}
    let original=read(source)?;let (_,embedded)=png_snapshot(&original)?;if embedded.is_none(){return Ok(0);}
    let saved=read(output)?;let digest=clothing_resource_digest(&original)?;
    if clothing_resource_digest(&saved)?!=digest{return Err("Saved card model resources differ; clothing state copy refused".into());}
    let origin=source.parent().ok_or("source card has no directory")?.join(".okstates");let target=output.parent().ok_or("saved card has no directory")?.join(".okstates").join(&digest);
    let legacy=format!("{:x}",Sha256::digest(&original));let mut copied=std::collections::BTreeSet::new();
    for directory in [origin.join(&digest),origin.join(legacy)] {
        if !directory.ok_is_dir(){continue;}
        for entry in crate::gamefs::read_dir(&directory).map_err(|e|e.to_string())? {
            let entry=entry.map_err(|e|e.to_string())?;if !entry.file_type().map_err(|e|e.to_string())?.ok_is_file(){continue;}
            let name=entry.file_name();let Some(name)=name.to_str() else {continue;};
            if let Some(outfit)=name.strip_prefix("outfit").and_then(|n|n.strip_suffix("_rules.json")) {
                if outfit.parse::<usize>().ok().filter(|n|*n<crate::portable_outfits::MAX_OUTFITS).is_some() && !copied.contains(name) {
                    let payload=read(&entry.path())?;crate::clothing_rules::validate(&payload,&digest)?;
                    let destination=target.join(name);if entry.path()!=destination {publish_clothing_cache(&destination,&payload)?;}copied.insert(name.to_string());
                }
                continue;
            }
            let Some((outfit,states))=name.strip_prefix("outfit").and_then(|n|n.strip_suffix(".okpreview")).and_then(|n|n.split_once('_')) else {continue;};
            if outfit.parse::<usize>().ok().filter(|n|*n<crate::portable_outfits::MAX_OUTFITS).is_none()||states.len()!=9||!states.bytes().all(|b|(b'0'..=b'3').contains(&b))||copied.contains(name){continue;}
            let destination=target.join(name);if entry.path()!=destination {let payload=read(&entry.path())?;let _=decode_clothing(&payload,&original,source)?;publish_clothing_cache(&destination,&payload)?;}
            copied.insert(name.to_string());
        }
    }
    let receipt=source.with_extension("verified.json");
    if receipt.ok_is_file() && source!=output {
        let mut metadata:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&receipt).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
        let source_digest=format!("{:x}",Sha256::digest(&original));
        if metadata["apng_sha256"].as_str()!=Some(source_digest.as_str()){return Err("Prepared states copied, but source receipt hash mismatch; preparation provenance was not copied".into());}
        metadata["derived_from_apng_sha256"]=serde_json::json!(source_digest);
        metadata["resource_sha256"]=serde_json::json!(digest);
        metadata["apng_sha256"]=serde_json::json!(format!("{:x}",Sha256::digest(&saved)));
        publish_clothing_cache(&output.with_extension("verified.json"),&serde_json::to_vec_pretty(&metadata).map_err(|e|e.to_string())?)?;
    }
    Ok(copied.len())
}

/// Serial batch: prepare exact reader button states for every outfit. Original
/// APNG pixels stay unchanged; the variant borrows immutable base-card assets.
pub fn prepare_reader_library(library:&Path,report:&Path)->Result<serde_json::Value,String> {
 // This command owns its process. Do not persist complete one-shot source
 // sessions even when invoked directly instead of through the PS wrapper.
 std::env::set_var("OK_NOCACHE","1");
 for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
 let mut paths=crate::gamefs::read_dir(library).map_err(|e|e.to_string())?.filter_map(Result::ok).map(|e|e.path()).filter(|p|p.extension().is_some_and(|e|e.eq_ignore_ascii_case("png"))).collect::<Vec<_>>();paths.sort();if paths.is_empty(){return Err("empty APNG library".into());}
 let start=std::env::var("OK_READER_START").ok().map(|v|v.parse::<usize>()).transpose().map_err(|e|e.to_string())?.unwrap_or(0);
 let end=std::env::var("OK_READER_END").ok().map(|v|v.parse::<usize>()).transpose().map_err(|e|e.to_string())?.unwrap_or(paths.len());
 if start>=end||end>paths.len(){return Err("invalid library range".into());}let paths=paths[start..end].to_vec();
 let mut rows=Vec::new();let mut states=0;let mut private_parts=0;
 let geometry=|s:&OkSession|->Result<Vec<u8>,String>{bincode::serialize(&s.parts.iter().map(|p|(&p.role,&p.name,p.render_visible,&p.positions,&p.indices,&p.uvs,&p.alpha_mask)).collect::<Vec<_>>()).map_err(|e|e.to_string())};
 for (index,path) in paths.iter().enumerate(){
  let png=crate::gamefs::read(path).map_err(|e|e.to_string())?;let digest=clothing_resource_digest(&png)?;
  std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");let count=load(path)?.coordinate_count;
  for outfit in 0..count {
   std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");let mut worn=load_outfit(path,outfit as i32)?;let before=worn.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();worn.refresh_portable_render();let mut private=0;let mut hair=0;
   for (part,previous) in worn.parts.iter().zip(before){if !part.positions.iter().flatten().all(|v|v.is_finite()){return Err(format!("{} outfit{outfit}: nonfinite geometry",path.display()));}if part.runtime_accessory.is_some(){private+=1;if part.positions!=previous{return Err(format!("{} outfit{outfit}: unevaluated private rig",path.display()));}let name=part.name.to_ascii_lowercase();if part.role.contains("hair")||name.contains("hair")||name.contains("kami"){hair+=1;}}}private_parts+=private;
   let mut variants=Vec::new();
   for state in ["1,1,1,1,0,0,0,0,0","3,3,3,3,0,0,0,0,0"] {
    std::env::set_var("OK_CLOTHES_STATES",state);let session=load_outfit(path,outfit as i32)?;let payload=encode_clothing(&session,&png,path,outfit as i32)?;let decoded=decode_clothing(&payload,&png,path)?;
    if geometry(&session)?!=geometry(&decoded)?{return Err(format!("{} outfit{outfit}: compact clothing geometry mismatch",path.display()));}
    let cache=library.join(".okstates").join(&digest).join(format!("outfit{outfit}_{}.okpreview",state.replace(",","")));publish_clothing_cache(&cache,&payload)?;states+=1;variants.push(serde_json::json!({"state":state,"bytes":payload.len(),"roundtrip_geometry":true}));
   }
   rows.push(serde_json::json!({"card":path.file_name().unwrap().to_string_lossy(),"outfit":outfit,"private_parts":private,"named_hair_parts":hair,"finite":true,"evaluated_private_rig":true,"variants":variants}));
   eprintln!("Reader292 {}/{} {} outfit{outfit}: private={private} hair={hair} states={states}",index+1,paths.len(),path.display());
   publish_clothing_cache(report,&serde_json::to_vec_pretty(&serde_json::json!({"valid":false,"cards":paths.len(),"completed_outfits":rows.len(),"prepared_states":states,"private_parts":private_parts,"outfits":rows})).map_err(|e|e.to_string())?)?;
  }
 }
 std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
 let result=serde_json::json!({"valid":true,"cards":paths.len(),"completed_outfits":rows.len(),"prepared_states":states,"private_parts":private_parts,"outfits":rows});publish_clothing_cache(report,&serde_json::to_vec_pretty(&result).map_err(|e|e.to_string())?)?;Ok(result)
}

/// Rebuild a lean card: immutable resources belong to the application pack;
/// original KKEx texture dictionaries are not duplicated in the new card.
pub(crate) fn reuse_card_snapshot(raw:&[u8],base:&Path,output:&Path)->Result<Option<serde_json::Value>,String>{
    let original=crate::gamefs::read(base).map_err(|e|e.to_string())?;let(_,range)=png_snapshot(&original)?;
    let Some(range)=range else{return Ok(None)};let payload=&original[range];
    let(iend,existing)=png_snapshot(raw)?;if existing.is_some(){return Err("edited metadata already contains a snapshot".into());}
    let count=crate::card::parse_card_bytes_mode(raw,0,false)?.coordinate_count;
    let mut chunk=CHUNK.to_vec();chunk.extend(payload);
    let mut bytes=raw[..iend].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&raw[iend..]);
    if bytes.len() as u64>MAX_BYTES{return Err("edited PNG exceeds limit".into());}
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    Ok(Some(serde_json::json!({"outfit_count":count,"outfits_rebuilt":0,"unchanged_outfits_reused":count})))
}

/// Runtime geometry candidate: no APNG packing, file output, or publication.
/// Renderer removals come only from validated source-property provenance.
pub(crate) struct PreparedAccessorySnapshot {
    pub replacement:crate::session::PreviewPartReplacement,
    pub parts:Vec<crate::session::SessionPart>,
    pub timings:std::collections::BTreeMap<&'static str,f64>,
}
pub(crate) fn prepare_accessory_snapshot(game:&Path,info:&crate::card::CardInfo,
    snapshot:&crate::portable_resources::SplitSnapshot,index:usize,slot:usize,
    mut resolve:impl FnMut(&crate::portable_resources::ResourceRef)->Result<Vec<u8>,String>)
    ->Result<Option<PreparedAccessorySnapshot>,String>{
    use crate::{portable_resources::{SplitSnapshot,ResourceRef},session::OkSession};
    use std::collections::{BTreeMap,BTreeSet};
    use bincode::Options;
    if !snapshot.references.iter().any(|r|r.part.is_none()&&r.field=="accessory_context"){return Ok(None)}
    let mut stage=std::time::Instant::now();let mut timings=BTreeMap::new();
    macro_rules! checkpoint {($name:literal)=>{{timings.insert($name,stage.elapsed().as_secs_f64()*1000.);stage=std::time::Instant::now();}};}
    let mut source=OkSession::preview_globals(snapshot,|r|resolve(r))?;
    let context=source.accessory_context.as_deref().ok_or("missing accessory extraction context")?;
    if !context.matches(info,index as i32)?{
        eprintln!("ok-extract: single accessory fallback: extraction dependencies changed ({})",context.mismatch(info,index as i32));return Ok(None)
    }
    checkpoint!("restore_global_context");
    let accessory_parts=OkSession::preview_accessory_parts(&snapshot)?;
    let mut origins=BTreeMap::new();
    for r in snapshot.references.iter().filter(|r|r.field=="part_origin"){
        let Some(i)=r.part else{return Err("global part provenance is invalid".into())};
        let property:String=bincode::DefaultOptions::new().with_fixint_encoding().with_limit(MAX_BYTES)
            .reject_trailing_bytes().deserialize(&resolve(r)?).map_err(|e|e.to_string())?;
        if origins.insert(i,property).is_some(){return Err("duplicate part provenance".into());}
    }
    if accessory_parts.iter().any(|i|!origins.contains_key(i)){
        eprintln!("ok-extract: single accessory fallback: legacy renderer provenance");return Ok(None)
    }
    let removed:BTreeSet<_>=accessory_parts.into_iter().filter(|i|origins.get(i)
        .and_then(|p|crate::accessory_slot_from_property(p))==Some(slot)).collect();
    checkpoint!("read_provenance");
    source.parts=crate::accessory_extract::extract_slot(game,info,index as i32,slot,context)?;
    checkpoint!("extract_slot");
    let mut references=Vec::new();let mut unique=BTreeMap::new();
    let residue=source.split_preview_part_resources(info,|part,field,kind,raw,_|{
        if part.is_some(){let id=crate::portable_resources::id(kind,&raw);unique.insert(id.clone(),raw);
            references.push(ResourceRef{part,field:field.into(),kind:kind.into(),id});}Ok(())
    })?;
    checkpoint!("split_part_resources");
    let replacement=OkSession::prepare_preview_part_replacement(snapshot,&removed,SplitSnapshot{schema:1,residue,references,unique})?;
    checkpoint!("patch_parts");
    Ok(Some(PreparedAccessorySnapshot{replacement,parts:source.parts,timings}))
}

/// Patch exact source-identified accessory renderers. Missing legacy extraction
/// inputs request the existing full-outfit upgrade instead of guessing names.
pub(crate) fn shared_edited_accessory(game:&Path,card:&Path,base:&Path,resources:&Path,
    output:&Path,index:usize,slot:usize)->Result<Option<serde_json::Value>,String>{
    use crate::portable_outfits::SnapshotReader;
    use std::collections::BTreeMap;
    let mut stage=std::time::Instant::now();let mut timings=BTreeMap::new();
    macro_rules! checkpoint {($name:literal)=>{{timings.insert($name,stage.elapsed().as_secs_f64()*1000.);stage=std::time::Instant::now();}};}
    let original=crate::gamefs::read(base).map_err(|e|e.to_string())?;
    let(_,range)=png_snapshot(&original)?;let Some(range)=range else{return Ok(None)};
    let payload=&original[range];if !payload.starts_with(crate::portable_outfits::MAGIC){return Ok(None)}
    if output.ok_exists(){return Err("output already exists".into());}
    let reader=SnapshotReader::new(payload,resources)?;let snapshot=reader.snapshot(index)?;
    checkpoint!("base_read_directory");
    if !snapshot.references.iter().any(|r|r.part.is_none()&&r.field=="accessory_context"){return Ok(None)}
    let edited=crate::gamefs::read(card).map_err(|e|e.to_string())?;
    // Match the source extractor, including source shape plugins. Portable
    // display metadata deliberately uses a different parsing mode.
    let mut info=crate::card::parse_card_bytes(&edited,index as i32)?;
    let previous=crate::card::parse_card_bytes_mode(&original,index as i32,false)?;
    if previous.coordinate_count!=info.coordinate_count{return Err("edited outfit count changed unexpectedly".into());}
    crate::mods::apply_strip_all_migrations(game,&mut info);
    checkpoint!("card_parse_migrations");
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    let Some(candidate)=prepare_accessory_snapshot(game,&info,&snapshot,index,slot,|r|reader.resolve(r))? else{return Ok(None)};
    timings.extend(candidate.timings);
    let rebuilt=candidate.parts.len();let reused=candidate.replacement.added_range.start;
    let patched=candidate.replacement.snapshot;
    stage=std::time::Instant::now();
    let payload=crate::portable_outfits::replace_outfit(payload,patched,index,resources)?;
    checkpoint!("pack_outfit");
    let lean=lean_card(&edited)?;let(iend,_)=png_snapshot(&lean)?;
    let mut chunk=CHUNK.to_vec();chunk.extend(&payload);
    let mut bytes=lean[..iend].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());bytes.extend(&chunk);
    bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&lean[iend..]);
    if bytes.len() as u64>MAX_BYTES{return Err("edited PNG exceeds limit".into());}
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    timings.insert("write_png",stage.elapsed().as_secs_f64()*1000.);
    Ok(Some(serde_json::json!({"outfit_count":info.coordinate_count,"outfits_rebuilt":0,
        "unchanged_outfits_reused":info.coordinate_count.saturating_sub(1),"single_slot":slot,
        "parts_rebuilt":rebuilt,"parts_reused":reused,"single_slot_timings_ms":timings})))
}

/// Direct catalog textures affect every outfit but do not change any model.
pub(crate) fn shared_edited_head_texture(game:&Path,card:&Path,base:&Path,resources:&Path,
    output:&Path,category:i32)->Result<Option<serde_json::Value>,String>{
    shared_edited_head_textures(game,card,base,resources,output,&[category])
}
pub(crate) fn shared_edited_head_textures(game:&Path,card:&Path,base:&Path,resources:&Path,
    output:&Path,categories:&[i32])->Result<Option<serde_json::Value>,String>{
    if categories.is_empty()||categories.iter().any(|c|!matches!(c,406..=414)){return Ok(None)}
    let categories=categories.iter().copied().collect::<std::collections::BTreeSet<_>>();
    let original=crate::gamefs::read(base).map_err(|e|e.to_string())?;
    let(_,range)=png_snapshot(&original)?;let Some(range)=range else{return Ok(None)};
    let original_payload=&original[range];if !original_payload.starts_with(crate::portable_outfits::MAGIC){return Ok(None)}
    if output.ok_exists(){return Err("output already exists".into());}
    let edited=crate::gamefs::read(card).map_err(|e|e.to_string())?;
    let info=crate::card::parse_card_bytes(&edited,0)?;
    let previous=crate::card::parse_card_bytes_mode(&original,0,false)?;
    if info.coordinate_count!=previous.coordinate_count{return Err("edited outfit count changed unexpectedly".into());}
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let guids=crate::mods::find_guids(&game.join("mods"),None);
    let reader=crate::portable_outfits::SnapshotReader::new(original_payload,resources)?;
    let mut replacements=Vec::new();let mut changed=0;
    for outfit in 0..info.coordinate_count{
        let mut snapshot=reader.snapshot(outfit)?;
        for &category in &categories{
            // Global face texture inputs use coordinate-zero head ME scope.
            // Per-outfit material definitions still come from its snapshot.
            let Some(count)=OkSession::patch_preview_head_texture(&mut snapshot,category,game,&info,&vanilla,&mods,&guids,|r|reader.resolve(r))?else{return Ok(None)};
            changed+=count;
        }
        replacements.push((outfit,snapshot));
    }
    let payload=crate::portable_outfits::replace_outfits(original_payload,replacements,resources)?;
    let lean=lean_card(&edited)?;let(iend,_)=png_snapshot(&lean)?;
    let mut chunk=CHUNK.to_vec();chunk.extend(&payload);
    let mut bytes=lean[..iend].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&lean[iend..]);
    if bytes.len() as u64>MAX_BYTES{return Err("edited PNG exceeds limit".into());}
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    Ok(Some(serde_json::json!({"outfit_count":info.coordinate_count,"outfits_rebuilt":0,
        "head_texture_outfits_updated":info.coordinate_count,"head_texture_parts_updated":changed,
        "unchanged_outfits_reused":0,"geometry_rebuilt":false})))
}

pub(crate) fn shared_edited_outfit(game:&Path,card:&Path,base:&Path,resources:&Path,output:&Path,index:usize)->Result<Option<serde_json::Value>,String> {
    let original=crate::gamefs::read(base).map_err(|e|e.to_string())?;
    let (_,range)=png_snapshot(&original)?;let Some(range)=range else{return Ok(None)};
    let payload=&original[range];if !payload.starts_with(crate::portable_outfits::MAGIC){return Ok(None)}
    if output.ok_exists(){return Err("output already exists".into());}
    let edited=crate::gamefs::read(card).map_err(|e|e.to_string())?;
    let info=crate::card::parse_card_bytes_mode(&edited,index as i32,false)?;
    let previous=crate::card::parse_card_bytes_mode(&original,index as i32,false)?;
    if previous.coordinate_count!=info.coordinate_count{return Err("edited outfit count changed unexpectedly".into());}
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    let source=crate::session::open(game,Some(card),index as i32)?;
    let (raw,_)=crate::portable_resources::split(&source,&info,resources,&format!("edited#outfit{index}"))?;
    let snapshot=bincode::deserialize(&raw).map_err(|e|e.to_string())?;
    let payload=crate::portable_outfits::replace_outfit(payload,snapshot,index,resources)?;
    let lean=lean_card(&edited)?;let(iend,_)=png_snapshot(&lean)?;
    let mut chunk=CHUNK.to_vec();chunk.extend(&payload);
    let mut bytes=lean[..iend].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&lean[iend..]);
    if bytes.len() as u64>MAX_BYTES{return Err("edited PNG exceeds limit".into());}
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    Ok(Some(serde_json::json!({"outfit_count":info.coordinate_count,"outfits_rebuilt":1,"unchanged_outfits_reused":info.coordinate_count.saturating_sub(1)})))
}

pub fn shared(game:&Path,card:&Path,resources:&Path,output:&Path)->Result<serde_json::Value,String> {
    if output.ok_exists() {return Err("output already exists".into());}
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"] {std::env::set_var(key,"1");}
    let start=Instant::now();
    let original=crate::gamefs::read(card).map_err(|e|e.to_string())?;
    let info=crate::card::parse_card_bytes_mode(&original,0,false)?;
    let count=info.coordinate_count.max(1);
    if count>crate::portable_outfits::MAX_OUTFITS{return Err("source card exceeds outfit limit".into());}
    let mut snapshots=Vec::new();let mut reports=Vec::new();
    for outfit in 0..count {
        eprintln!("shared outfit {}/{}",outfit+1,count);
        let source=crate::session::open(game,Some(card),outfit as i32)?;
        let info=crate::card::parse_card_bytes_mode(&original,outfit as i32,false)?;
        let (raw,mut report)=crate::portable_resources::split(&source,&info,resources,
            &format!("{}#outfit{outfit}",card.file_name().unwrap_or_default().to_string_lossy()))?;
        let snapshot= bincode::deserialize(&raw).map_err(|e|e.to_string())?;
        snapshots.push(snapshot);report["index"]=serde_json::json!(outfit);
        report["clothes_ids"]=serde_json::json!(info.clothes_ids);reports.push(report);
    }
    let (payload,mut report)=crate::portable_outfits::pack(snapshots,resources)?;
    report["outfits"]=serde_json::json!(reports);report["builtin_directory"]=serde_json::json!(resources);
    let lean=lean_card(&original)?;
    let (iend,_)=png_snapshot(&lean)?;
    let mut chunk=CHUNK.to_vec();chunk.extend(&payload);
    let mut bytes=lean[..iend].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());
    bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&lean[iend..]);
    if bytes.len() as u64>MAX_BYTES {return Err("shared PNG exceeds limit".into());}
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    report["png"]=serde_json::json!(output);report["png_bytes"]=serde_json::json!(bytes.len());
    report["original_bytes"]=serde_json::json!(original.len());report["lean_metadata_preview_bytes"]=serde_json::json!(lean.len());
    report["card_payload_bytes"]=serde_json::json!(payload.len());report["conversion_seconds"]=serde_json::json!(start.elapsed().as_secs_f64());
    Ok(report)
}

/// Keep editable parameters and every Coordinate, without legacy texture dictionaries or scripts.
fn lean_card(original:&[u8])->Result<Vec<u8>,String> {
    use std::io::Cursor;
    let (iend,existing)=png_snapshot(original)?;
    if existing.is_some() {return Err("shared export requires the original card".into());}
    let extra=original.get(iend+12..).ok_or("missing character payload")?;
    let mut cur=Cursor::new(extra);
    crate::card::read_i32_le(&mut cur)?;
    crate::card::read_dotnet_string(&mut cur)?;crate::card::read_dotnet_string(&mut cur)?;
    let face=crate::card::read_i32_le(&mut cur)?;
    if face<0 || (cur.position() as usize).checked_add(face as usize).is_none_or(|p|p>extra.len()) {return Err("invalid embedded face image".into());}
    cur.set_position(cur.position()+face as u64);
    let header_size=crate::card::read_i32_le(&mut cur)?;
    if header_size<0 {return Err("invalid block table size".into());}
    let position=cur.position() as usize;
    let header=extra.get(position..position+header_size as usize).ok_or("truncated block table")?;
    let mut table=rmpv::decode::read_value(&mut Cursor::new(header)).map_err(|e|e.to_string())?;
    cur.set_position((position+header_size as usize) as u64);
    crate::card::read_i64_le(&mut cur)?;let base=cur.position() as usize;
    let mut data=Vec::<u8>::new();let mut kept=0;
    let rmpv::Value::Map(table_map)=&mut table else {return Err("invalid block table".into());};
    let value=table_map.iter_mut().find(|(k,_)|k.as_str()==Some("lstInfo")).map(|(_,v)|v).ok_or("missing block list")?;
    let rmpv::Value::Array(list)=value else {return Err("invalid block list".into());};
    list.retain(|v|v.as_map().is_some_and(|m|m.iter().any(|(k,v)|k.as_str()==Some("name") &&
        matches!(v.as_str(),Some("Custom"|"Parameter"|"Coordinate")))));
    for entry in list {
        let rmpv::Value::Map(map)=entry else {return Err("invalid block entry".into());};
        let old_pos=map.iter().find(|(k,_)|k.as_str()==Some("pos")).and_then(|(_,v)|v.as_i64()).ok_or("missing block offset")?;
        let size=map.iter().find(|(k,_)|k.as_str()==Some("size")).and_then(|(_,v)|v.as_i64()).ok_or("missing block size")?;
        if old_pos<0 || size<0 {return Err("negative block offset".into());}
        let block=extra.get(base+old_pos as usize..base+old_pos as usize+size as usize).ok_or("truncated character block")?;
        let offset=data.len();data.extend(block);
        for (key,value) in map {if key.as_str()==Some("pos") {*value=rmpv::Value::from(offset as u64);}}
        kept+=1;
    }
    if kept<2 {return Err("shared card requires Custom and Parameter".into());}
    let mut header=Vec::new();rmpv::encode::write_value(&mut header,&table).map_err(|e|e.to_string())?;
    let mut output=original[..iend+12].to_vec();
    // Retain marker/version but discard the redundant internal face thumbnail.
    let mut prefix=Cursor::new(extra);crate::card::read_i32_le(&mut prefix)?;
    crate::card::read_dotnet_string(&mut prefix)?;crate::card::read_dotnet_string(&mut prefix)?;
    output.extend(&extra[..prefix.position() as usize]);output.extend(0i32.to_le_bytes());
    output.extend((header.len() as i32).to_le_bytes());output.extend(header);
    output.extend((data.len() as i64).to_le_bytes());output.extend(data);Ok(output)
}

/// Experimental single-file PNG: original preview and original KKS card blocks
/// remain intact. The private ancillary chunk holds schema-1 local assets.
pub fn embed(card: &Path, pack: &Path, output: &Path) -> Result<serde_json::Value,String> {
    let original=crate::gamefs::read(card).map_err(|e|e.to_string())?;
    let (iend,existing)=png_snapshot(&original)?;
    if existing.is_some() {return Err("card already contains portable assets".into());}
    load(pack)?;
    let payload=crate::gamefs::read(pack).map_err(|e|e.to_string())?;
    if original.len() as u64+payload.len() as u64+12>MAX_BYTES {return Err("combined PNG too large".into());}
    let mut chunk=Vec::from(CHUNK.as_slice());chunk.extend(&payload);let checksum=crc(&chunk);
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&original[..iend]).and_then(|_|file.write_all(&(payload.len() as u32).to_be_bytes()))
        .and_then(|_|file.write_all(&chunk)).and_then(|_|file.write_all(&checksum.to_be_bytes()))
        .and_then(|_|file.write_all(&original[iend..])).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    Ok(serde_json::json!({"png":output,"bytes":original.len()+payload.len()+12,"chunk":"okPv","schema":SCHEMA}))
}

pub fn export(game: &Path, card: &Path, output: &Path) -> Result<serde_json::Value, String> {
    for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
    let start = Instant::now();
    let source = crate::session::open(game, Some(card), 0)?;
    let raw = source.to_preview_bytes()?;
    if raw.len() as u64 > MAX_BYTES { return Err("snapshot too large".into()); }
    let mut bytes = Vec::from(MAGIC.as_slice());
    bytes.extend(SCHEMA.to_le_bytes());
    bytes.extend((raw.len() as u64).to_le_bytes());
    bytes.extend(crc(&raw).to_le_bytes());
    bytes.extend(lz4_flex::block::compress(&raw));
    if bytes.len() as u64 > MAX_BYTES + HEADER as u64 { return Err("compressed snapshot too large".into()); }
    // Never replace an existing output or the source card.
    let mut file = fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e| e.to_string())?;
    file.write_all(&bytes).and_then(|_| file.sync_all()).map_err(|e| e.to_string())?;
    Ok(serde_json::json!({"pack":output,"bytes":bytes.len(),"raw_bytes":raw.len(),
        "parts":source.parts.len(),"conversion_seconds":start.elapsed().as_secs_f64(),"schema":SCHEMA}))
}

/// Uses only the pack path. This command does not accept or discover a game root.
pub fn verify(path: &Path) -> Result<serde_json::Value, String> {
    let count=load(path)?.coordinate_count;
    let mut outfits=Vec::new();
    for index in 0..count {let mut report=verify_outfit(path,index as i32)?;report["outfit"]=serde_json::json!(index);outfits.push(report);}
    let mut report=outfits.first().cloned().ok_or("portable card has no outfits")?;
    report["outfit_count"]=serde_json::json!(count);report["outfits"]=serde_json::json!(outfits);Ok(report)
}

/// Bake the editable PNG parameters once during conversion. This preserves
/// load_outfit's geometry while avoiding its shape/skin rebuild on every load.
/// Resource references and every outfit survive; no catalog writer is needed.
pub fn normalize(input:&Path,output:&Path)->Result<serde_json::Value,String>{
    normalize_with(input,output,|_,_|Ok(()))
}

/// Upgrade dynamic clothing masks from verified source metadata. Geometry,
/// main textures and all PNG/APNG chunks outside okPv remain byte-identical.
pub fn repair_clothing_masks(game:&Path,input:&Path,output:&Path)->Result<serde_json::Value,String>{
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let guid=crate::mods::find_guids(&game.join("mods"),None);
    repair_clothing_masks_with(game,input,output,&vanilla,&mods,&guid)
}

pub fn repair_clothing_mask_library(game:&Path,input:&Path,output:&Path)->Result<serde_json::Value,String>{
    use sha2::{Digest,Sha256};
    fs::create_dir_all(output).map_err(|e|e.to_string())?;
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods=crate::list::load_mods_catalog(&game.join("mods"));
    let guid=crate::mods::find_guids(&game.join("mods"),None);
    let mut cards=crate::gamefs::read_dir(input).map_err(|e|e.to_string())?.filter_map(Result::ok)
        .map(|p|p.path()).filter(|p|p.extension().is_some_and(|e|e=="png") && p.with_extension("verified.json").ok_is_file()).collect::<Vec<_>>();
    cards.sort();if cards.is_empty(){return Err("No verified APNG cards".into());}
    let mut rows=Vec::new();
    for (index,card) in cards.iter().enumerate(){
        eprintln!("Mask upgrade {}/{} {}",index+1,cards.len(),card.display());
        let target=output.join(card.file_name().ok_or("missing card name")?);
        let report_path=target.with_extension("mask-upgrade.json");
        let report=if target.ok_exists(){
            let report:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&report_path).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            if report["original_apng_sha256"]!=format!("{:x}",Sha256::digest(crate::gamefs::read(card).map_err(|e|e.to_string())?)) ||
                report["apng_sha256"]!=format!("{:x}",Sha256::digest(crate::gamefs::read(&target).map_err(|e|e.to_string())?)) {return Err("Mask upgrade resume hash mismatch".into());}
            report
        }else{repair_clothing_masks_with(game,card,&target,&vanilla,&mods,&guid)?};
        publish_clothing_cache(&report_path,&serde_json::to_vec_pretty(&report).map_err(|e|e.to_string())?)?;
        let mut receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(card.with_extension("verified.json")).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
        receipt["apng"]=serde_json::json!(target);receipt["apng_sha256"]=report["apng_sha256"].clone();
        receipt["mask_upgrade"]=serde_json::json!(report_path);receipt["mask_upgrade_original_sha256"]=report["original_apng_sha256"].clone();
        publish_clothing_cache(&target.with_extension("verified.json"),&serde_json::to_vec_pretty(&receipt).map_err(|e|e.to_string())?)?;
        rows.push(report);
        publish_clothing_cache(&output.join("mask-library.json"),&serde_json::to_vec_pretty(&serde_json::json!({"valid":false,"cards":cards.len(),"completed_cards":rows.len(),"rows":rows})).map_err(|e|e.to_string())?)?;
        crate::unity::clear_bundle_cache();
    }
    let result=serde_json::json!({"valid":true,"cards":cards.len(),"completed_cards":rows.len(),"rows":rows});
    publish_clothing_cache(&output.join("mask-library.json"),&serde_json::to_vec_pretty(&result).map_err(|e|e.to_string())?)?;Ok(result)
}

fn repair_clothing_masks_with(game:&Path,input:&Path,output:&Path,vanilla:&crate::list::ChaListCatalog,
    mods:&crate::list::ChaListCatalog,guid:&std::collections::HashMap<String,std::path::PathBuf>)->Result<serde_json::Value,String>{
    use sha2::{Digest,Sha256};
    let receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(input.with_extension("verified.json")).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
    let hash=|bytes:&[u8]|format!("{:x}",Sha256::digest(bytes));
    let input_bytes=crate::gamefs::read(input).map_err(|e|e.to_string())?;
    if receipt["apng_sha256"].as_str()!=Some(hash(&input_bytes).as_str()){return Err("APNG verification receipt mismatch".into());}
    let source=Path::new(receipt["source"].as_str().ok_or("missing verified source")?);
    let original=crate::gamefs::read(source).map_err(|e|e.to_string())?;
    if receipt["source_sha256"].as_str()!=Some(hash(&original).as_str()){return Err("source verification receipt mismatch".into());}
    let mut rows=Vec::new();
    let mut report=normalize_with(input,output,|index,session|{
        let info=crate::card::parse_card_bytes_mode(&original,index,false)?;
        let inner=crate::over_layer_mask_png(game,&info,&vanilla,&mods,&guid,index,"OverInnerMask");
        let bra=crate::over_layer_mask_png(game,&info,&vanilla,&mods,&guid,index,"OverBraMask");
        let selected=crate::select_card_clothes(&info,index,&vanilla,&mods,&guid);
        let bra_st=if selected.get(2).and_then(Option::as_ref).is_some_and(|(entry,_,_)|entry.info("Coordinate")=="2") {(2.,-1.)}else{(1.,0.)};
        let mut changed=0;
        for part in &mut session.parts {
            if !matches!(part.shader_kind,2|8|9|11){continue;}
            let is_inner=part.runtime_accessory.as_ref().is_some_and(|p|p.clothes_component()=="ct_top_parts_A");
            let mask=if part.role=="clothes_bra"{bra.as_deref()}else if is_inner{inner.as_deref()}else{None};
            let Some(mask)=mask else{continue;};
            let scale=if part.role=="clothes_bra"{bra_st.0}else{1.};
            let offset=if part.role=="clothes_bra"{bra_st.1}else{0.};
            let mut mesh=crate::unity::ExtractedMesh {mat:part.source_material.clone(),texture_png:part.png.clone(),..Default::default()};
            crate::apply_native_clothes_over_mask(&mut mesh,mask,scale,offset,[true,true]);
            let png=mesh.alpha_mask_png.clone().ok_or("invalid dynamic mask PNG")?;
            part.alpha_mask=Some(png.clone());part.shader_textures.insert("_AlphaMask".into(),png);
            part.runtime_textures.remove("_AlphaMask");part.alpha_mask_st=[1.,scale,0.,offset];
            part.source_material=crate::session::SessionPart::from_mesh(&part.role,&mesh,"masked",true).source_material;changed+=1;
        }
        rows.push(serde_json::json!({"outfit":index,"updated_parts":changed}));Ok(())
    })?;
    report["clothing_mask_upgrade"]=serde_json::json!(rows);
    report["source_sha256"]=receipt["source_sha256"].clone();
    report["original_apng_sha256"]=serde_json::json!(hash(&input_bytes));
    report["apng_sha256"]=serde_json::json!(hash(&crate::gamefs::read(output).map_err(|e|e.to_string())?));
    Ok(report)
}

/// Preserve already extracted half/full models while upgrading their dynamic
/// mask bindings and base digest. No mesh extraction or visibility workaround.
pub fn upgrade_prepared_clothing_masks(input:&Path,output:&Path)->Result<serde_json::Value,String>{
    use sha2::{Digest,Sha256};
    let mut cards=crate::gamefs::read_dir(output).map_err(|e|e.to_string())?.filter_map(Result::ok).map(|e|e.path())
        .filter(|p|p.extension().is_some_and(|e|e=="png")).collect::<Vec<_>>();cards.sort();
    let mut rows=Vec::new();
    let geometry=|session:&OkSession|bincode::serialize(&session.parts.iter().map(|p|
        (&p.role,&p.name,p.render_visible,&p.positions,&p.normals,&p.weights,&p.bone_idx,&p.bind_poses,&p.indices,&p.uvs,&p.png)).collect::<Vec<_>>()).map_err(|e|e.to_string());
    for card in cards {
        let old=input.join(card.file_name().ok_or("missing card filename")?);
        let before=crate::gamefs::read(&old).map_err(|e|e.to_string())?;let after=crate::gamefs::read(&card).map_err(|e|e.to_string())?;
        let report:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(card.with_extension("mask-upgrade.json")).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
        if report["original_apng_sha256"]!=format!("{:x}",Sha256::digest(&before)) || report["apng_sha256"]!=format!("{:x}",Sha256::digest(&after)){return Err("Prepared-state upgrade card hash mismatch".into());}
        let origin=input.join(".okstates").join(clothing_resource_digest(&before)?);
        if !origin.ok_is_dir(){continue;}
        let target=output.join(".okstates").join(clothing_resource_digest(&after)?);
        let mut donors=std::collections::HashMap::new();let mut count=0;
        for entry in crate::gamefs::read_dir(origin).map_err(|e|e.to_string())? {
            let entry=entry.map_err(|e|e.to_string())?;let name=entry.file_name();let Some(name)=name.to_str() else{continue;};
            let Some((outfit,states))=name.strip_prefix("outfit").and_then(|s|s.strip_suffix(".okpreview")).and_then(|s|s.split_once('_')) else{continue;};
            let outfit:i32=outfit.parse().map_err(|_|"Invalid cached outfit")?;
            if states.len()!=9 || !states.bytes().all(|c|(b'0'..=b'3').contains(&c)){return Err("Invalid cached clothing states".into());}
            if !donors.contains_key(&outfit){let (_,range)=png_snapshot(&after)?;donors.insert(outfit,decode_at(&after[range.ok_or("missing upgraded portable chunk")?],Some(&card),outfit)?);}
            let donor=&donors[&outfit];let flags=crate::clothes_alpha_state(states.as_bytes()[0]-b'0');
            let raw=crate::gamefs::read(entry.path()).map_err(|e|e.to_string())?;let mut session=decode_clothing(&raw,&before,&old)?;
            let immutable=geometry(&session)?;
            for part in &mut session.parts {
                if !matches!(part.shader_kind,2|8|9|11){continue;}
                let category=if part.role=="clothes_bra"{1}else if part.runtime_accessory.as_ref().is_some_and(|p|p.clothes_component()=="ct_top_parts_A"){2}else{continue;};
                let binding=donor.parts.iter().find(|p|p.alpha_mask.is_some() && if category==1{p.role=="clothes_bra"}else{p.runtime_accessory.as_ref().is_some_and(|p|p.clothes_component()=="ct_top_parts_A")});
                let Some(binding)=binding else{continue;};let png=binding.alpha_mask.clone().unwrap();
                part.alpha_mask=Some(png.clone());part.shader_textures.insert("_AlphaMask".into(),png);
                part.alpha_mask_st=binding.alpha_mask_st;part.runtime_textures.remove("_AlphaMask");
                if let Some(mat)=part.source_material.as_mut(){
                    for (i,key) in ["_AlphaMask_ST_x","_AlphaMask_ST_y","_AlphaMask_ST_z","_AlphaMask_ST_w"].iter().enumerate(){mat.floats.insert((*key).into(),binding.alpha_mask_st[i]);}
                    mat.floats.insert("_alpha_a".into(),if flags[0]{1.}else{0.});mat.floats.insert("_alpha_b".into(),if flags[1]{1.}else{0.});
                    // A conservative false keeps translucent native alpha
                    // correct for every mask channel/state combination.
                    if mat.shader.eq_ignore_ascii_case("Shader Forge/main_alpha"){mat.floats.insert("_OKNativeAlphaOpaque".into(),0.);}
                }
            }
            if immutable!=geometry(&session)?{return Err("Prepared-state upgrade changed model geometry".into());}
            let payload=encode_clothing(&session,&after,&card,outfit)?;
            if immutable!=geometry(&decode_clothing(&payload,&after,&card)?)?{return Err("Prepared-state upgraded roundtrip geometry mismatch".into());}
            publish_clothing_cache(&target.join(name),&payload)?;count+=1;
        }
        rows.push(serde_json::json!({"card":card.file_name(),"upgraded_states":count,"geometry_unchanged":true}));
        publish_clothing_cache(&output.join("mask-prepared-states.json"),&serde_json::to_vec_pretty(&serde_json::json!({"valid":false,"rows":rows})).map_err(|e|e.to_string())?)?;
    }
    let result=serde_json::json!({"valid":true,"rows":rows});publish_clothing_cache(&output.join("mask-prepared-states.json"),&serde_json::to_vec_pretty(&result).map_err(|e|e.to_string())?)?;Ok(result)
}

fn normalize_with<F>(input:&Path,output:&Path,mut update:F)->Result<serde_json::Value,String>
where F:FnMut(i32,&mut OkSession)->Result<(),String>{
    use sha2::{Digest,Sha256};
    fn geometry(session:&OkSession)->Result<String,String>{
        let mut hash=Sha256::new();
        for part in &session.parts {
            hash.update(bincode::serialize(&(&part.name,&part.positions,&part.normals,&part.weights,&part.bone_idx,&part.bind_poses,
                &part.indices,&part.uvs,&part.uv2,&part.uv3,&part.uv4,&part.vertex_colors,&part.png,&part.normal_map,&part.alpha_mask)).map_err(|e|e.to_string())?);
        }
        Ok(format!("{:x}",hash.finalize()))
    }
    if output.ok_exists(){return Err("normalization output already exists".into());}
    let original=crate::gamefs::read(input).map_err(|e|e.to_string())?;
    let (_,range)=png_snapshot(&original)?;let range=range.ok_or("missing portable chunk")?;
    let root=crate::portable_resources::root(input)?;
    let count=load(input)?.coordinate_count;let mut snapshots=Vec::new();let mut expected=Vec::new();
    for index in 0..count {
        let mut session=load_outfit(input,index as i32)?;
        let immutable=bincode::serialize(&session.parts.iter().map(|p|(&p.positions,&p.normals,&p.indices,&p.uvs,&p.png)).collect::<Vec<_>>()).map_err(|e|e.to_string())?;
        update(index as i32,&mut session)?;
        if immutable!=bincode::serialize(&session.parts.iter().map(|p|(&p.positions,&p.normals,&p.indices,&p.uvs,&p.png)).collect::<Vec<_>>()).map_err(|e|e.to_string())? {return Err("upgrade modified geometry or MainTex".into());}
        expected.push(geometry(&session)?);
        let info=crate::card::parse_card_bytes_mode(&original,index as i32,false)?;
        let mut references=Vec::new();let mut unique=std::collections::BTreeMap::new();
        let residue=session.split_preview_resources(&info,|part,field,kind,raw,_|{
            let id=crate::portable_resources::id(kind,&raw);
            if !root.join("assets").join(format!("{id}.okasset")).ok_exists(){unique.entry(id.clone()).or_insert(raw);}
            references.push(crate::portable_resources::ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
        })?;
        crate::session::OkSession::audit_preview_residue(&residue)?;
        snapshots.push(crate::portable_resources::SplitSnapshot{schema:1,residue,references,unique});
    }
    let (payload,mut report)=crate::portable_outfits::pack(snapshots,&root)?;
    let mut chunk=CHUNK.to_vec();chunk.extend(&payload);
    let mut bytes=original[..range.start-8].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());
    bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&original[range.end+4..]);
    if bytes.len() as u64>MAX_BYTES{return Err("normalized PNG exceeds limit".into());}
    let mut file=fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|e.to_string())?;
    file.write_all(&bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
    for (index,expected) in expected.iter().enumerate(){
        if &geometry(&load_outfit(output,index as i32)?)?!=expected{return Err(format!("normalization changed outfit {index} geometry"));}
    }
    report["geometry_sha256_by_outfit"]=serde_json::json!(expected);
    report["normalized_parameters"]=serde_json::json!(true);report["png"]=serde_json::json!(output);Ok(report)
}

fn verify_outfit(path:&Path,outfit:i32)->Result<serde_json::Value,String> {
    let start = Instant::now();
    let mut s = load_outfit(path,outfit)?;
    let load_seconds = start.elapsed().as_secs_f64();
    if !s.game_root.as_os_str().is_empty() || s.parts.is_empty() || s.body_keys.is_empty() { return Err("incomplete independent snapshot".into()); }
    let body = s.shape_body.clone(); let face = s.shape_face.clone();
    let mut checks = Vec::new();
    for index in [0usize, 4, 22] {
        if index >= body.len() { return Err("missing body channels".into()); }
        let mut bounds = Vec::new();
        let mut baseline = Vec::new();
        let mut max_vertex_delta = 0.0f32;
        for value in [0.0, 0.5, 1.0] {
            let mut values = body.clone(); values[index] = value;
            s.apply_shape(&values, &face);
            s.skin_all();
            if value == 0.0 {
                baseline = s.parts.iter().map(|p| p.positions.clone()).collect::<Vec<_>>();
            } else if value == 1.0 {
                for (p, original) in s.parts.iter().zip(&baseline) {
                    for (vertex, before) in p.positions.iter().zip(original) {
                        for axis in 0..3 { max_vertex_delta = max_vertex_delta.max((vertex[axis]-before[axis]).abs()); }
                    }
                }
            }
            let mut low = [f32::INFINITY;3]; let mut high = [f32::NEG_INFINITY;3];
            for p in &s.parts { for vertex in &p.positions { for axis in 0..3 {
                if !vertex[axis].is_finite() { return Err("nonfinite shaped vertex".into()); }
                low[axis] = low[axis].min(vertex[axis]); high[axis] = high[axis].max(vertex[axis]);
            } } }
            bounds.push((low,high));
        }
        if max_vertex_delta < 1e-6 { return Err(format!("body channel {index} did not change geometry")); }
        checks.push(serde_json::json!({"channel":index,"max_vertex_delta":max_vertex_delta,"bounds_at_0_half_1":bounds}));
    }
    Ok(serde_json::json!({"pack":path,"game_root":"","load_seconds":load_seconds,
        "initial_height":body.first(),"initial_bust":body.get(4),
        "parts":s.parts.len(),"body_channels":body.len(),"face_channels":face.len(),"shape_checks":checks}))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    #[ignore="installed APNG/shared resources; owns process environment"]
    fn runtime_apng_parameters_survive_all_outfit_loads(){
        use sha2::{Digest,Sha256};
        let project=Path::new("C:/portable/OpenKoikatsu");
        let path=std::env::var_os("OK_TEST_PREVIEW_PACK").map(std::path::PathBuf::from)
            .unwrap_or_else(||project.join("Content/CharacterCards/CrowdAPNG/C001.png"));
        std::env::set_var("OK_BUILTIN_RESOURCES",project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        let original=crate::gamefs::read(&path).unwrap();let hash=Sha256::digest(&original);
        let count=crate::card::parse_card_bytes_mode(&original,0,false).unwrap().coordinate_count;
        assert!(count>0);let mut checked=0;
        for index in 0..count {
            let expected=crate::card::parse_card_bytes_mode(&original,index as i32,false).unwrap();
            let mut live=load_outfit(&path,index as i32).unwrap();
            assert_eq!(live.shape_body,expected.shape_body);assert_eq!(live.shape_face,expected.shape_face);
            assert_eq!(live.bust_softness,expected.bust_softness);assert_eq!(live.bust_weight,expected.bust_weight);
            let before=live.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();
            live.refresh_portable_render();assert!(!live.parts.is_empty());
            for(part,old)in live.parts.iter().zip(before){
                assert!(part.positions.iter().flatten().chain(part.normals.iter().flatten()).all(|v|v.is_finite()),"nonfinite outfit={index} part={}",part.name);
                assert_eq!(part.indices.len()%3,0);assert!(part.indices.iter().all(|i|(*i as usize)<part.positions.len()));
                if part.runtime_accessory.is_some(){assert!(part.positions==old,"private rig changed on repeated refresh: outfit={index} part={}",part.name);}
            }
            live.ensure_skel_export();assert_eq!(live.skel_export.as_ref().unwrap().part_rest.len(),live.parts.len());checked+=1;
        }
        assert_eq!(checked,count);assert_eq!(Sha256::digest(crate::gamefs::read(&path).unwrap()),hash);
        eprintln!("APNG editable runtime all outfits passed={checked} source_unchanged=1");
    }
    #[test]
    #[ignore="requires OK_TEST_PORTABLE_BEFORE and OK_TEST_PORTABLE_NORMALIZED"]
    fn normalization_keeps_every_outfit_geometry_and_shapes(){
        let before=std::env::var("OK_TEST_PORTABLE_BEFORE").unwrap();let after=std::env::var("OK_TEST_PORTABLE_NORMALIZED").unwrap();
        let count=load(Path::new(&before)).unwrap().coordinate_count;
        assert_eq!(load(Path::new(&after)).unwrap().coordinate_count,count);
        for index in 0..count {
            let a=load_outfit(Path::new(&before),index as i32).unwrap();let b=load_outfit(Path::new(&after),index as i32).unwrap();
            assert_eq!(a.shape_body,b.shape_body);assert_eq!(a.shape_face,b.shape_face);assert_eq!(a.parts.len(),b.parts.len());
            for (a,b) in a.parts.iter().zip(&b.parts){
                assert_eq!(a.name,b.name);assert_eq!(a.positions,b.positions);assert_eq!(a.normals,b.normals);
                assert_eq!(a.weights,b.weights);assert_eq!(a.bone_idx,b.bone_idx);assert_eq!(a.bind_poses,b.bind_poses);
                assert_eq!(a.indices,b.indices);assert_eq!(a.uvs,b.uvs);assert_eq!(a.png,b.png);
                assert_eq!(a.shader_textures,b.shader_textures);assert_eq!(a.runtime_textures,b.runtime_textures);
            }
        }
    }
    #[test] fn rejects_bad_headers_and_lengths() {
        assert!(decode(&[]).is_err());
        let mut bytes = Vec::from(MAGIC.as_slice()); bytes.extend(SCHEMA.to_le_bytes());
        bytes.extend((MAX_BYTES+1).to_le_bytes()); bytes.extend(0u32.to_le_bytes());
        assert!(decode(&bytes).err().unwrap().contains("size limit"));
        bytes[12..20].copy_from_slice(&1u64.to_le_bytes());
        assert!(decode(&bytes).is_err());
    }
    #[test] fn detects_payload_corruption() {
        let raw = b"not a session";
        let mut bytes = Vec::from(MAGIC.as_slice()); bytes.extend(SCHEMA.to_le_bytes());
        bytes.extend((raw.len() as u64).to_le_bytes()); bytes.extend((crc(raw)^1).to_le_bytes());
        bytes.extend(lz4_flex::block::compress(raw));
        assert!(decode(&bytes).err().unwrap().contains("checksum"));
    }
    #[test] fn clothing_cache_tracks_snapshot_not_preview_or_parameters() {
        fn png(snapshot:&[u8],preview:&[u8],custom:&[u8])->Vec<u8>{
            let mut bytes=PNG.to_vec();
            for (kind,data) in [(b"IDAT",preview),(CHUNK,snapshot),(b"IEND",&[][..])] {
                bytes.extend((data.len() as u32).to_be_bytes());let mut chunk=kind.to_vec();chunk.extend(data);bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());
            }
            bytes.extend(custom);bytes
        }
        let baseline=clothing_resource_digest(&png(b"model-a",b"preview-a",b"height-a")).unwrap();
        assert_eq!(baseline,clothing_resource_digest(&png(b"model-a",b"preview-b",b"height-b")).unwrap());
        assert_ne!(baseline,clothing_resource_digest(&png(b"model-b",b"preview-a",b"height-a")).unwrap());
        let mut damaged=png(b"model-a",b"preview-a",b"height-a");let (_,range)=png_snapshot(&damaged).unwrap();damaged[range.unwrap().start]^=1;
        assert!(clothing_resource_digest(&damaged).is_err());
    }
    #[test] fn clothing_cache_publication_does_not_leave_temporary_files() {
        let directory=std::env::temp_dir().join(format!("ok-clothing-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let path=directory.join("outfit0_111100000.okpreview");
        publish_clothing_cache(&path,b"immutable state").unwrap();
        publish_clothing_cache(&path,b"immutable state").unwrap();
        assert_eq!(crate::gamefs::read(&path).unwrap(),b"immutable state");
        let bad=directory.join("existing-directory");fs::create_dir(&bad).unwrap();
        assert!(publish_clothing_cache(&bad,b"not publishable").is_err());
        assert_eq!(crate::gamefs::read_dir(&directory).unwrap().count(),2);
        fs::remove_file(path).unwrap();fs::remove_dir(bad).unwrap();fs::remove_dir(directory).unwrap();
    }
    #[test] fn png_requires_a_complete_datastream() {
        assert!(png_snapshot(PNG).is_err());
        let mut png=Vec::from(PNG.as_slice());png.extend(0u32.to_be_bytes());png.extend(b"IEND");png.extend(crc(b"IEND").to_be_bytes());
        assert_eq!(png_snapshot(&png).unwrap().0,8);
        png[8..12].copy_from_slice(&100u32.to_be_bytes());assert!(png_snapshot(&png).is_err());
    }
    #[test]
    #[ignore = "requires a local converted PNG via OK_TEST_PREVIEW_PACK"]
    fn converted_card_exports_height_scale_changes() {
        let path=std::env::var("OK_TEST_PREVIEW_PACK").expect("set OK_TEST_PREVIEW_PACK");
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM"] {std::env::set_var(key,"1");}
        let mut s=load(Path::new(&path)).unwrap();
        let face=s.shape_face.clone();let mut body=s.shape_body.clone();
        body[0]=0.;s.apply_shape(&body,&face);
        let before=s.ensure_skel_export().bones.iter().map(|b|(b.pose_pos,b.pose_scl)).collect::<Vec<_>>();
        body[0]=1.;s.apply_shape(&body,&face);
        let after=s.ensure_skel_export();
        assert!(after.bones.iter().zip(before).any(|(b,(position,scale))|
            (0..3).any(|axis|(b.pose_pos[axis]-position[axis]).abs()>1e-6 || (b.pose_scl[axis]-scale[axis]).abs()>1e-6)),
            "height must change exported bone position or scale");
    }
}

#[cfg(test)]
mod reader_regressions {
    use super::*;
    #[test]
    #[ignore="installed source/APNG material contract audit; owns process environment"]
    fn reader_material_contract_oracle() {
        let selection=std::env::var("OK_MATERIAL_CONTRACT_SELECTION").expect("selection JSON required");
        let output=std::env::var("OK_MATERIAL_CONTRACT_OUTPUT").expect("output JSON required");
        let cards:Vec<String>=serde_json::from_slice(&crate::gamefs::read(selection).unwrap()).unwrap();
        assert!(!cards.is_empty()&&cards.len()<=100);
        std::env::set_var("OK_NOCACHE","1");
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        let mut rows=Vec::new();
        for card in cards {
            let path=Path::new(&card);
            let receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(path.with_extension("verified.json")).unwrap()).unwrap();
            let source=Path::new(receipt["source"].as_str().unwrap());
            let native=crate::session::open(&crate::test_support::game_path(""),Some(source),0).unwrap();
            let portable=load_outfit(path,0).unwrap();
            let describe=|session:&OkSession|session.parts.iter().map(|p|serde_json::json!({"name":p.name,"role":p.role,"visible":p.render_visible,"shader_kind":p.shader_kind,"unlit":p.unlit,"blend":p.blend,"material":p.source_material})).collect::<Vec<_>>();
            rows.push(serde_json::json!({"apng":card,"source":source,"native":describe(&native),"portable":describe(&portable)}));
            crate::unity::clear_bundle_cache();
        }
        fs::write(output,serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }
    #[test]
    #[ignore="installed clothing texture diagnostic"]
    fn reader_waist_material_oracle() {
        std::env::set_var("OK_BUILTIN_RESOURCES","C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        let mut session=load_outfit(Path::new("C:/portable/OpenKoikatsu/output/CrowdAPNG/C001.png"),0).unwrap();
        let body=session.shape_body.clone();let face=session.shape_face.clone();session.apply_shape(&body,&face);session.skin_all();session.refresh_portable_render();
        let out=Path::new("C:/portable/OpenKoikatsu/Saved/Tests/Reader292WaistMaterials");fs::create_dir_all(out).unwrap();
        let mut rows=Vec::new();
        for (i,p) in session.parts.iter().enumerate().filter(|(_,p)|p.role.contains("cloth")) {
            for (name,png) in [("main",p.png.as_ref()),("alpha",p.alpha_mask.as_ref()),("detail",p.detail_mask.as_ref()),("overlay",p.overtex_png.as_ref())] {
                if let Some(png)=png {fs::write(out.join(format!("{i}_{name}.png")),png).unwrap();}
            }
            rows.push(serde_json::json!({"index":i,"name":p.name,"role":p.role,"shader":p.shader_kind,"blend":p.blend,"material":p.source_material,"positions":p.positions,"raw":p.rest_positions,"kind":p.skel_kind,"private":p.runtime_accessory}));
        }
        fs::write(out.join("materials.json"),serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }

    #[test]
    #[ignore="source renderer visibility oracle; owns process environment"]
    fn reader_source_visibility_oracle() {
        let project=Path::new("C:/portable/OpenKoikatsu");let game=&crate::test_support::game_path("");
        std::env::set_var("OK_NOCACHE","1");std::env::set_var("OK_BUILTIN_RESOURCES",project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");let mut rows=Vec::new();
        for (index,outfit) in [(2,0),(2,4),(2,6),(50,3)] {
            let path=project.join(format!("output/CrowdAPNG/C{index:03}.png"));
            let receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(path.with_extension("verified.json")).unwrap()).unwrap();
            let source=Path::new(receipt["source"].as_str().unwrap());
            let native=crate::session::open(game,Some(source),outfit).unwrap();let portable=load_outfit(&path,outfit).unwrap();
            let summarize=|session:&OkSession|session.parts.iter().map(|p|serde_json::json!({"role":p.role,"name":p.name,"visible":p.render_visible,"triangles":p.indices.len()/3,"shader":p.shader_kind,"private":p.runtime_accessory.is_some()})).collect::<Vec<_>>();
            let before=summarize(&native);let after=summarize(&portable);
            rows.push(serde_json::json!({"card":index,"outfit":outfit,"native":before,"portable":after}));
            publish_clothing_cache(&project.join("Saved/Tests/Reader292SourceVisibility.json"),&serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
            crate::unity::clear_bundle_cache();
        }
    }
    #[test]
    #[ignore="portable material/geometry diagnostic; owns process environment"]
    fn reader_face_material_oracle() {
        use sha2::{Digest,Sha256};
        std::env::set_var("OK_BUILTIN_RESOURCES","C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        let mut rows=Vec::new();
        for outfit in [0,4,6] {
            let mut session=load_outfit(Path::new("C:/portable/OpenKoikatsu/output/CrowdAPNG/C002.png"),outfit).unwrap();
            session.ensure_skel_export();
            let body=session.shape_body.clone();let face=session.shape_face.clone();session.apply_shape(&body,&face);
            let bones=session.ensure_skel_export().bones.iter().map(|b|serde_json::json!({"name":b.name,"parent":b.parent,"rest_pos":b.rest_pos,"rest_rot":b.rest_rot,"rest_scl":b.rest_scl,"pose_pos":b.pose_pos,"pose_rot":b.pose_rot,"pose_scl":b.pose_scl})).collect::<Vec<_>>();
            std::env::set_var("OK_SKELMESH","0");session.skin_all();std::env::set_var("OK_SKELMESH","1");
            let parts=session.parts.iter().filter(|p|p.role=="head"||p.name.contains("nosetama")).map(|p|{
                let mut min=[f32::INFINITY;3];let mut max=[f32::NEG_INFINITY;3];
                for v in &p.positions {for i in 0..3 {min[i]=min[i].min(v[i]);max[i]=max[i].max(v[i]);}}
                let texture=p.png.as_ref().map(|raw|{let im=crate::teximg::load_from_memory(raw).unwrap().into_rgba8();let alpha=im.pixels().map(|v|v[3] as u64).sum::<u64>() as f64/(im.width() as f64*im.height() as f64*255.);serde_json::json!({"sha":format!("{:x}",Sha256::digest(raw)),"alpha_mean":alpha,"width":im.width(),"height":im.height()})});
                serde_json::json!({"name":p.name,"role":p.role,"min":min,"max":max,"blend":p.blend,"material":p.source_material,"texture":texture,"item":p.item_draw,"visible":p.render_visible})
            }).collect::<Vec<_>>();
            rows.push(serde_json::json!({"outfit":outfit,"parts":parts,"bones":bones}));
        }
        fs::write("C:/portable/OpenKoikatsu/Saved/Tests/Reader292FaceMaterials.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }
    #[test]
    #[ignore="all real library outfits; owns process environment"]
    fn reader_all_outfits_integrity() {
        use sha2::{Digest,Sha256};
        let project=Path::new("C:/portable/OpenKoikatsu");
        std::env::set_var("OK_BUILTIN_RESOURCES",project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        let mut rows=Vec::new();let mut textures=std::collections::BTreeSet::new();
        let report=project.join("Saved/Tests/Reader292AllOutfitsIntegrity.json");
        for index in 0..100 {
            let path=project.join(format!("output/CrowdAPNG/C{index:03}.png"));
            let png=crate::gamefs::read(&path).unwrap();let hash=format!("{:x}",Sha256::digest(&png));
            let receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(path.with_extension("verified.json")).unwrap()).unwrap();
            assert_eq!(receipt["apng_sha256"].as_str(),Some(hash.as_str()),"Original card changed: {}",path.display());
            let count=load(&path).unwrap().coordinate_count;
            for outfit in 0..count {
                let mut session=load_outfit(&path,outfit as i32).unwrap();
                let before=session.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();
                session.refresh_portable_render();let mut private=0;let mut triangles=0;
                for (part,old) in session.parts.iter().zip(before) {
                    assert!(part.positions.iter().flatten().chain(part.normals.iter().flatten()).all(|v|v.is_finite()),"C{index:03} outfit{outfit} {}",part.name);
                    assert_eq!(part.indices.len()%3,0);assert!(part.indices.iter().all(|i|(*i as usize)<part.positions.len()));
                    if part.runtime_accessory.is_some(){private+=1;assert_eq!(part.positions,old,"Private rig not evaluated: C{index:03} outfit{outfit} {}",part.name);}
                    triangles+=part.indices.len()/3;
                    for raw in [&part.png,&part.alpha_mask,&part.hair_gloss,&part.detail_mask,&part.line_mask,&part.normal_map,&part.normal_detail,&part.normal_mask,&part.overtex_png,&part.overtex2_png].into_iter().flatten(){
                        let id=format!("{:x}",Sha256::digest(raw));if textures.insert(id){let image=crate::teximg::load_from_memory(raw).unwrap();assert!(image.width()>0&&image.height()>0);}
                    }
                }
                rows.push(serde_json::json!({"card":index,"outfit":outfit,"parts":session.parts.len(),"private_rigs":private,"triangles":triangles,"finite":true,"private_evaluated":true}));
            }
            publish_clothing_cache(&report,&serde_json::to_vec_pretty(&serde_json::json!({"valid":false,"completed_cards":index+1,"outfits":rows,"decoded_unique_textures":textures.len(),"png_unchanged":true})).unwrap()).unwrap();
            eprintln!("Reader all-outfit integrity C{index:03}: outfits={count} cumulative={} unique_textures={}",rows.len(),textures.len());
        }
        publish_clothing_cache(&report,&serde_json::to_vec_pretty(&serde_json::json!({"valid":true,"cards":100,"completed_outfits":rows.len(),"outfits":rows,"decoded_unique_textures":textures.len(),"png_unchanged":true})).unwrap()).unwrap();
    }

    #[test]
    #[ignore="prepared real card resources"]
    fn compact_clothing_checks_base_and_corruption() {
      let project=std::path::PathBuf::from("C:/portable/OpenKoikatsu");std::env::set_var("OK_BUILTIN_RESOURCES",project.join("Content/IndependentResources/v1"));
      let path=project.join("output/CrowdAPNG/C000.png");let png=crate::gamefs::read(&path).unwrap();
      let cache=path.parent().unwrap().join(".okstates").join(clothing_resource_digest(&png).unwrap()).join("outfit0_111100000.okpreview");
      let payload=crate::gamefs::read(cache).unwrap();assert!(payload.starts_with(CLOTHING_MAGIC));assert!(decode_clothing(&payload,&png,&path).is_ok());
      let other=crate::gamefs::read(project.join("output/CrowdAPNG/C001.png")).unwrap();assert!(decode_clothing(&payload,&other,&path).err().unwrap().contains("base resource mismatch"));
      let mut damaged=payload.clone();damaged[20]^=1;assert!(decode_clothing(&damaged,&png,&path).err().unwrap().contains("checksum mismatch"));
      assert!(decode_clothing(&payload[..23],&png,&path).is_err());
    }

    #[test]
    #[ignore="real multi-outfit library; owns process environment"]
    fn clothing_states_survive_preview_edits_and_outfit_changes() {
        use sha2::{Digest,Sha256};
        let project=std::path::PathBuf::from("C:/portable/OpenKoikatsu");
        let library=project.join("output/CrowdAPNG");
        let offline=project.join("Saved/Tests/Reader291Offline");fs::create_dir_all(&offline).unwrap();
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        std::env::set_var("OK_BUILTIN_RESOURCES",project.join("Content/IndependentResources/v1"));
        let mut outfits=Vec::new();let mut cases=Vec::new();
        for index in [0,1] {
            std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
            let path=library.join(format!("C{index:03}.png"));let png=crate::gamefs::read(&path).unwrap();
            let metadata:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(path.with_extension("verified.json")).unwrap()).unwrap();
            let source=std::path::PathBuf::from(metadata["source"].as_str().unwrap());let root=source.ancestors().find(|p|p.join("abdata").ok_is_dir()).unwrap();
            let count=load(&path).unwrap().coordinate_count;
            for outfit in 0..count {
                let mut session=load_outfit(&path,outfit as i32).unwrap();let before=session.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();session.refresh_portable_render();
                for (part,before) in session.parts.iter().zip(before){assert!(part.positions.iter().flatten().all(|p|p.is_finite()));if part.runtime_accessory.is_some(){assert_eq!(part.positions,before);}}
                outfits.push(serde_json::json!({"card":index,"outfit":outfit,"parts":session.parts.len()}));
            }
            for outfit in [0,1] {
                for state in ["1,1,1,1,0,0,0,0,0","3,3,3,3,0,0,0,0,0","0,3,1,2,0,0,0,0,0"] {
                    std::env::set_var("OK_CLOTHES_STATES",state);
                    let portable=load_outfit(&path,outfit).unwrap();let native=crate::session::open(root,Some(&source),outfit).unwrap();
                    let signature=|s:&OkSession|s.parts.iter().filter(|p|p.role.starts_with("clothes")||p.role=="body").map(|p|(p.role.clone(),p.name.clone(),p.render_visible,p.indices.clone(),p.uvs.clone(),p.alpha_mask.clone())).collect::<Vec<_>>();
                    assert_eq!(signature(&portable),signature(&native),"C{index:03} outfit{outfit} {state}");
                    let digest=clothing_resource_digest(&png).unwrap();let name=format!("outfit{outfit}_{}.okpreview",state.replace(",",""));let target=offline.join(".okstates").join(&digest);fs::create_dir_all(&target).unwrap();
                    fs::copy(library.join(".okstates").join(&digest).join(&name),target.join(&name)).unwrap();
                    // A new preview ancillary chunk changes the full card hash but
                    // leaves embedded model resources and editable card valid.
                    let (iend,_)=png_snapshot(&png).unwrap();let mut edited=png[..iend].to_vec();let mut chunk=b"tEXt".to_vec();chunk.extend(b"Reader291\0updated preview metadata");edited.extend(((chunk.len()-4) as u32).to_be_bytes());edited.extend(&chunk);edited.extend(crc(&chunk).to_be_bytes());edited.extend(&png[iend..]);
                    assert_ne!(Sha256::digest(&edited),Sha256::digest(&png));assert_eq!(clothing_resource_digest(&edited).unwrap(),digest);
                    let copy=offline.join(format!("C{index:03}_edited.png"));fs::write(&copy,&edited).unwrap();
                    assert!(copy_clothing_states(&path,&copy).unwrap()>0);
                    let derived:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(copy.with_extension("verified.json")).unwrap()).unwrap();assert_eq!(derived["resource_sha256"],digest);assert_eq!(derived["apng_sha256"],format!("{:x}",Sha256::digest(&edited)));
                    // Remove only this test's newly generated provenance: the
                    // prepared state must still load without any source lookup.
                    fs::remove_file(copy.with_extension("verified.json")).unwrap();
                    let mut reloaded=load_outfit(&copy,outfit).unwrap();assert_eq!(signature(&reloaded),signature(&portable));assert_eq!(reloaded.coordinate_count,count);
                    let original=reloaded.shape_body.clone();let face=reloaded.shape_face.clone();let before=reloaded.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();let mut body=original.clone();body[0]=if body[0]<0.5{1.0}else{0.0};reloaded.apply_shape(&body,&face);reloaded.skin_all();reloaded.refresh_portable_render();assert!(reloaded.parts.iter().zip(&before).any(|(p,b)|p.positions!=*b));
                    reloaded.apply_shape(&original,&face);reloaded.skin_all();reloaded.refresh_portable_render();assert!(reloaded.parts.iter().flat_map(|p|p.positions.iter().flatten()).all(|v|v.is_finite()));
                    cases.push(serde_json::json!({"card":index,"outfit":outfit,"state":state,"native_oracle":true,"edited_offline":true,"shape_control":true}));eprintln!("Reader291 C{index:03} outfit{outfit} {state}: native, edited offline, shape pass");
                }
            }
        }
        fs::write(project.join("Saved/Tests/Reader291Library.json"),serde_json::to_vec_pretty(&serde_json::json!({"valid":true,"worn_outfits":outfits,"cases":cases})).unwrap()).unwrap();
    }

    #[test]
    #[ignore = "real prepared 100-card library; owns process environment"]
    fn prepared_library_hair_and_clothing_states() {
        let project=std::path::PathBuf::from("C:/portable/OpenKoikatsu");
        let library=project.join("output/CrowdAPNG");
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"] {std::env::set_var(key,"1");}
        std::env::set_var("OK_BUILTIN_RESOURCES",project.join("Content/IndependentResources/v1"));
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        let mut records=Vec::new();
        for index in 0..100 {
            let path=library.join(format!("C{index:03}.png"));
            let mut session=load(&path).unwrap();
            let initial=session.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();
            session.refresh_portable_render();
            let mut count=0;let mut hair_count=0;let mut error=0f32;let mut min_y=f32::INFINITY;let mut max_y=f32::NEG_INFINITY;
            for (part,previous) in session.parts.iter().zip(&initial) {
                let name=part.name.to_ascii_lowercase();
                if part.runtime_accessory.is_none(){continue;}
                let hair=part.role.contains("hair")||name.contains("hair")||name.contains("kami");
                if hair {hair_count+=1;}
                count+=1;
                assert!(part.positions.iter().flatten().all(|v|v.is_finite()));
                for (a,b) in part.positions.iter().zip(previous) {for axis in 0..3 {error=error.max((a[axis]-b[axis]).abs());}if hair {min_y=min_y.min(a[1]);max_y=max_y.max(a[1]);}}
            }
            assert!(error<1e-5,"C{index:03} initial display differs from evaluated private rig: {error}");
            if index<2 {assert!(count>0,"User-reported cards must exercise private-rig geometry, including accessory roles");}
            if index==1 {assert!(hair_count>0 && min_y>0.4,"March hair must be above the floor: count={hair_count} min_y={min_y}");}
            records.push(serde_json::json!({"card":index,"private_parts":count,"private_hair_parts":hair_count,"repeat_pose_error_m":error,"hair_min_y_m":if hair_count>0 {Some(min_y)}else{None},"hair_max_y_m":if hair_count>0 {Some(max_y)}else{None}}));
        }
        let offline=project.join("Saved/Tests/Reader290Offline");fs::create_dir_all(&offline).unwrap();
        for index in [0,1] {
            let path=library.join(format!("C{index:03}.png"));
            let receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(path.with_extension("verified.json")).unwrap()).unwrap();
            let original=std::path::PathBuf::from(receipt["source"].as_str().unwrap());
            let root=original.ancestors().find(|a|a.join("abdata").ok_is_dir()).unwrap();
            let worn=load(&path).unwrap();
            let mut previous=worn.parts.iter().filter(|p|p.role.starts_with("clothes")).map(|p|(p.name.clone(),p.indices.len())).collect::<Vec<_>>();
            for state in ["1,1,1,1,0,0,0,0,0","3,3,3,3,0,0,0,0,0","0,3,1,2,0,0,0,0,0"] {
                std::env::set_var("OK_CLOTHES_STATES",state);
                let portable=load(&path).unwrap();
                assert!(portable.game_root.as_os_str().is_empty());
                let native=crate::session::open(root,Some(&original),0).unwrap();
                assert_eq!(portable.coordinate_count,worn.coordinate_count);
                let signature=|s:&OkSession|s.parts.iter().filter(|p|p.role.starts_with("clothes")||p.role=="body").map(|p|(p.role.clone(),p.name.clone(),p.render_visible,p.indices.clone(),p.uvs.clone(),p.alpha_mask.clone())).collect::<Vec<_>>();
                assert_eq!(signature(&portable),signature(&native),"state matches native prefab filtering and body mask");
                let actual=portable.parts.iter().filter(|p|p.role.starts_with("clothes")).map(|p|(p.name.clone(),p.indices.len())).collect::<Vec<_>>();
                if state.starts_with("3,"){assert_ne!(actual,previous,"Full removal changes actual clothing geometry");}
                previous=actual;
                // Move only the APNG and this immutable state cache to a folder
                // without its receipt/original source: loading must stay offline.
                let png=crate::gamefs::read(&path).unwrap();let digest=clothing_resource_digest(&png).unwrap();
                let name=format!("outfit0_{}.okpreview",state.replace(",",""));
                let target=offline.join(".okstates").join(&digest);fs::create_dir_all(&target).unwrap();
                fs::copy(library.join(".okstates").join(&digest).join(&name),target.join(&name)).unwrap();
                let copy=offline.join(format!("C{index:03}.png"));fs::write(&copy,&png).unwrap();
                assert!(!copy.with_extension("verified.json").ok_exists());
                assert_eq!(signature(&load(&copy).unwrap()),signature(&portable));
                eprintln!("clothing C{index:03} {state}: native oracle + offline cache pass");
            }
            std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");
        }
        fs::write(project.join("Saved/Tests/Reader290Library.json"),serde_json::to_vec_pretty(&serde_json::json!({"valid":true,"cards":records,"clothing_cards":2,"state_cases":6,"offline_cases":6})).unwrap()).unwrap();
    }
}
