//! Runtime preparation keeps geometry and full-card checkpoints off the
//! texture selection path. A candidate never changes live or saved state.
use super::*;
use crate::{portable_resources::SplitSnapshot,session::{OkSession,HeadTextureDelta}};
use std::{collections::{BTreeSet,HashMap},sync::Arc};

// Wall time remains the acceptance criterion. CPU accounting explains a
// stall without silently subtracting scheduling, I/O, or memory waits.
#[cfg(windows)]
fn source_thread_cpu_ms()->Option<f64>{
 #[link(name="kernel32")]
 extern "system" {
  fn GetCurrentThread()->*mut std::ffi::c_void;
  fn GetThreadTimes(thread:*mut std::ffi::c_void,creation:*mut u64,exit:*mut u64,kernel:*mut u64,user:*mut u64)->i32;
 }
 let(mut creation,mut exit,mut kernel,mut user)=(0u64,0u64,0u64,0u64);
 let valid=unsafe{GetThreadTimes(GetCurrentThread(),&mut creation,&mut exit,&mut kernel,&mut user)};
 (valid!=0).then_some((kernel as f64+user as f64)/10_000.)
}
#[cfg(not(windows))]
fn source_thread_cpu_ms()->Option<f64>{None}
fn cpu_delta(before:Option<f64>,after:Option<f64>)->Option<f64>{
 before.zip(after).map(|(before,after)|(after-before).max(0.))
}

pub(crate) struct Context {
 root:PathBuf, base:Arc<Vec<u8>>, resources:PathBuf, outfit:usize, revision:u64,
 raw:Arc<Vec<u8>>, snapshots:Arc<Vec<SplitSnapshot>>, materials:HashMap<String,Vec<u8>>,
 items:Arc<Vec<Item>>, vanilla:crate::list::ChaListCatalog, mods:crate::list::ChaListCatalog,
 guids:HashMap<String,PathBuf>, dirty:BTreeSet<i32>, dictionary_cache:std::cell::RefCell<crate::card::PngDictionaryCache>,
 geometry_template:std::cell::RefCell<Option<GeometryTemplate>>,
}
/// Display-shaped global state (no renderers) for exact-slot candidates.
/// Keyed by the complete parts-less globals and every display-shape input;
/// any difference rebuilds it through the ordinary shape policy.
struct GeometryTemplate { key:[u8;32], bytes:Vec<u8>, shaped:bool }
pub(crate) struct Prepared {
 pub base_revision:u64, pub target:String, pub item:String, pub category:i32,
 pub raw:Arc<Vec<u8>>, pub delta:HeadTextureDelta, pub timings_ms:[f64;4], snapshots:Arc<Vec<SplitSnapshot>>,
 pub preparation_thread:String, pub thread_cpu_timings_ms:[Option<f64>;4], pub material_resolves:usize, pub dictionary_cache_stats:[usize;2],
 pub card_parse_phases:Vec<(String,[f64;3])>,
 pub source_stages:super::timings::Stages,
}
pub(crate) struct Checkpoint {
 pub revision:u64, base:Arc<Vec<u8>>, resources:PathBuf,
 raw:Arc<Vec<u8>>, snapshots:Arc<Vec<SplitSnapshot>>,
}
/// An exact-slot geometry transaction owns both the renderer preparation
/// session and the complete deferred-save checkpoint. Nothing is published
/// during preparation, and a revision mismatch cannot mutate live state.
pub(crate) struct PreparedGeometry {
 pub base_revision:u64,pub target:String,pub item:String,pub slot:usize,
 /// Global rig only; per-part arrays live in `exports` so validation can
 /// borrow every publication input before ownership is transferred.
 pub source:OkSession,pub exports:crate::skel_export::PartExports,pub retained_indices:Vec<Option<usize>>,
 pub added_range:std::ops::Range<usize>,
 pub timings:std::collections::BTreeMap<&'static str,f64>,
 pub raw:Arc<Vec<u8>>,snapshots:Arc<Vec<SplitSnapshot>>,
}
impl Checkpoint {
 pub fn export(&self,output:&Path)->Result<J,String>{
  let(_,range)=crate::portable::png_snapshot(&self.base)?;
  let range=range.ok_or("checkpoint baseline has no portable snapshot")?;
  let replacements=self.snapshots.iter().cloned().enumerate().collect();
  let payload=crate::portable_outfits::replace_outfits(&self.base[range],replacements,&self.resources)?;
  crate::portable::write_snapshot_card(&self.raw,&payload,output)?;
  Ok(json!({"valid":true,"output":output,"revision":self.revision,"outfit_count":self.snapshots.len(),"source_reresolved":false}))
 }
}
impl Context {
 pub fn new(root:&Path,base:&Path,resources:&Path,cache:&Path,outfit:usize)->Result<Self,String>{
  let original=crate::gamefs::read(base).map_err(|e|e.to_string())?;
  let(_,range)=crate::portable::png_snapshot(&original)?;
  let range=range.ok_or("live appearance requires a portable snapshot")?;
  // This thread owns all later preparation: installed-source changes are
  // observed by notification rather than a full metadata walk per selection.
  crate::unity::references::watch_current_thread(root);
  let reader=crate::portable_outfits::SnapshotReader::new(&original[range],resources)?;
  let count=crate::card::parse_card_bytes_mode(&original,0,false)?.coordinate_count;
  if count==0||count>crate::portable_outfits::MAX_OUTFITS||outfit>=count{return Err("invalid live appearance outfit count/index".into());}
  let snapshots=(0..count).map(|index|reader.snapshot(index)).collect::<Result<Vec<_>,_>>()?;
  let mut materials=HashMap::new();
  for r in snapshots.iter().flat_map(|s|&s.references).filter(|r|r.field=="material_definition"){
   if materials.contains_key(&r.id){continue;}
   materials.insert(r.id.clone(),reader.resolve(r)?);
  }
  let mut blocks=blocks_from_card(&original)?;
  if let Some(metadata)=original_metadata(root,cache,&original)?{blocks.insert("KKEx".into(),metadata);}
  let raw=Arc::new(write_blocks(&original,blocks)?);
  let dictionary_started=std::time::Instant::now();let mut dictionary_cache=crate::card::PngDictionaryCache::default();
  crate::card::parse_card_bytes_cached(&raw,0,&mut dictionary_cache)?;
  eprintln!("APPEARANCE_DICTIONARY_READY retained_bytes={} initialization_ms={:.3}",dictionary_cache.retained_bytes(),dictionary_started.elapsed().as_secs_f64()*1000.);
  let context=Self{root:root.into(),base:Arc::new(original),resources:resources.into(),outfit,revision:0,raw,snapshots:Arc::new(snapshots),materials,items:catalog(root,cache)?,
   vanilla:crate::list::load_characustom_dir(&root.join("abdata/list/characustom")),
   mods:crate::list::load_mods_catalog(&root.join("mods")),
   guids:crate::mods::find_guids(&root.join("mods"),None),dirty:BTreeSet::new(),dictionary_cache:std::cell::RefCell::new(dictionary_cache),geometry_template:std::cell::RefCell::new(None)};
  context.prepare_shared_source_directories();Ok(context)
 }
 fn prepare_shared_source_directories(&self){
  // These are the installed shared texture families, not prepared character
  // variants or the first N choices. Index all vanilla families on the owned
  // source thread during context initialization, before enabling editing.
  // Pixel decoding/composition still belongs to the actual clicked selection.
  let started=std::time::Instant::now();let mut assets=BTreeSet::new();
  for entry in self.vanilla.by_id.values().filter(|e|matches!(e.category_no,406..=414)){
   for asset in [entry.main_ab.clone(),entry.info("MainTexAB"),entry.info("ColorMaskAB")]{
    if !asset.is_empty()&&asset!="0"{assets.insert(asset);}
   }
  }
  let mut loaded=0;
  for asset in &assets {
   if let Some(zip)=crate::mods::override_bundle_source(&self.root.join("mods"),asset){
    if let Some(bundle)=crate::unity::parse_bundle_zip_cached(&zip,asset){bundle.prepare_texture_directory();loaded+=1;}
   }
   if let Some(bundle)=crate::unity::parse_bundle_path_cached(&self.root.join("abdata").join(asset)){
    bundle.prepare_texture_directory();loaded+=1;
   }
  }
  // Face colour-space compatibility can consult the shared create texture,
  // including a negative name lookup. Prepare its directory, never its pixels.
  if let Some(bundle)=crate::unity::parse_bundle_path_cached(&self.root.join("abdata/chara/bo_head_00.unity3d")){
   bundle.prepare_texture_directory();loaded+=1;
  }
  eprintln!("APPEARANCE_SOURCE_READY families={} providers={} initialization_ms={:.3} character_prebakes=0",assets.len(),loaded,started.elapsed().as_secs_f64()*1000.);
 }
 pub fn prepare(&self,target:&str,key:&str)->Result<Prepared,String>{
  let t=targets().into_iter().find(|t|t.key==target).ok_or("unknown appearance target")?;
  if !matches!(t.category,406..=414){return Err("target requires geometry or composed skin preparation".into());}
  let item=if key.is_empty(){None}else{Some(self.items.iter().find(|i|i.key==key&&i.entry.category_no==t.category).ok_or("unknown appearance item")?)};
  if item.is_some_and(|i|!i.source.is_empty()&&i.guid.is_empty()){return Err("appearance source has no stable GUID".into());}
  let capture=super::timings::Capture::begin();
  let started=std::time::Instant::now();let cpu_start=source_thread_cpu_ms();
  let raw=Arc::new(patch(&self.raw,&t,item,self.outfit)?);
  let patched=started.elapsed().as_secs_f64()*1000.;let cpu_patched=source_thread_cpu_ms();
  // Face texture inputs are global and use coordinate-zero ME scope, matching
  // the full-card exporter. Each outfit retains its own renderer materials.
  let dictionary_before=self.dictionary_cache.borrow().stats();
  let card=crate::card::parse_card_bytes_cached(&raw,0,&mut self.dictionary_cache.borrow_mut())?;
  let dictionary_after=self.dictionary_cache.borrow().stats();
  let card_parse_phases=self.dictionary_cache.borrow().parse_phases.clone();
  let dictionary_cache_stats=[dictionary_after[0]-dictionary_before[0],dictionary_after[1]-dictionary_before[1]];
  let parsed=started.elapsed().as_secs_f64()*1000.;let cpu_parsed=source_thread_cpu_ms();
  let mut snapshots=self.snapshots.as_ref().clone();
  let mut bakes=HashMap::new();let mut material_resolves=0;
  for snapshot in &mut snapshots {
  let count=OkSession::patch_preview_head_texture_cached(snapshot,t.category,&self.root,&card,
   &self.vanilla,&self.mods,&self.guids,|r|{material_resolves+=1;self.materials.get(&r.id).cloned().ok_or("missing prepared material definition".into())},&mut bakes)?;
  if count.is_none(){return Err("source program requires a complete renderer replacement".into());}
  }
  let resolved=started.elapsed().as_secs_f64()*1000.;let cpu_resolved=source_thread_cpu_ms();
  let delta=OkSession::head_texture_snapshot_delta(&self.snapshots[self.outfit],&snapshots[self.outfit])?;
  let finished=started.elapsed().as_secs_f64()*1000.;let cpu_finished=source_thread_cpu_ms();
  Ok(Prepared{base_revision:self.revision,target:target.into(),item:key.into(),category:t.category,raw,delta,
   timings_ms:[patched,parsed-patched,resolved-parsed,finished-resolved],snapshots:Arc::new(snapshots),preparation_thread:format!("{:?}",std::thread::current().id()),
   material_resolves,dictionary_cache_stats,card_parse_phases,source_stages:capture.finish(),thread_cpu_timings_ms:[cpu_delta(cpu_start,cpu_patched),cpu_delta(cpu_patched,cpu_parsed),cpu_delta(cpu_parsed,cpu_resolved),cpu_delta(cpu_resolved,cpu_finished)]})
 }
 pub fn commit(&mut self,p:Prepared,live:&mut OkSession)->Result<(),String>{
  if p.base_revision!=self.revision{return Err("stale appearance candidate".into());}
  live.apply_head_texture_delta(&p.delta)?;
  self.raw=p.raw;self.snapshots=p.snapshots;self.dirty.insert(p.category);self.revision+=1;Ok(())
 }
 pub fn revision(&self)->u64{self.revision}
 pub fn raw(&self)->Arc<Vec<u8>>{self.raw.clone()}
 pub fn dirty_categories(&self)->Vec<i32>{self.dirty.iter().copied().collect()}
 pub fn checkpoint(&self)->Checkpoint{Checkpoint{revision:self.revision,base:self.base.clone(),
  resources:self.resources.clone(),raw:self.raw(),snapshots:self.snapshots.clone()}}
 pub fn prepared_checkpoint(&self,p:&Prepared)->Result<Checkpoint,String>{
  if p.base_revision!=self.revision{return Err("stale appearance candidate checkpoint".into());}
  Ok(Checkpoint{revision:self.revision+1,base:self.base.clone(),resources:self.resources.clone(),raw:p.raw.clone(),snapshots:p.snapshots.clone()})
 }
 pub fn prepare_geometry(&self,target:&str,key:&str)->Result<PreparedGeometry,String>{
  let t=targets().into_iter().find(|t|t.key==target).ok_or("unknown appearance target")?;
  if t.section!="accessory" || !(121..=130).contains(&t.category){return Err("geometry transaction requires an accessory target".into());}
  let slot=t.path.split('.').nth(1).and_then(|v|v.parse::<usize>().ok()).ok_or("invalid accessory slot")?;
  let item=if key.is_empty(){None}else{Some(self.items.iter().find(|i|i.key==key&&i.entry.category_no==t.category).ok_or("unknown appearance item")?)};
  if item.is_some_and(|i|!i.source.is_empty()&&i.guid.is_empty()){return Err("appearance source has no identity".into());}
  // Extract under exactly the established portable-export profile, scoped to
  // this source thread: the live renderer toggles the process environment,
  // and a global set_var here would race with it (and with dependency checks).
  let _profile=crate::profile::portable_export();let _clothes=crate::unity::request_clothes_states([0;9]);
  let capture=super::timings::Capture::begin();
  let raw=Arc::new(super::timings::measure("geometry.patch",||patch(&self.raw,&t,item,self.outfit))?);
  let mut card=super::timings::measure("geometry.card_parse",||crate::card::parse_card_bytes_cached(&raw,self.outfit as i32,&mut self.dictionary_cache.borrow_mut()))?;
  super::timings::measure("geometry.migrations",||crate::mods::apply_strip_all_migrations(&self.root,&mut card));
  let(_,range)=crate::portable::png_snapshot(&self.base)?;
  let reader=crate::portable_outfits::SnapshotReader::new(&self.base[range.ok_or("missing geometry baseline")?],&self.resources)?;
  let baseline=&self.snapshots[self.outfit];
  let resolve=|r:&crate::portable_resources::ResourceRef|baseline.unique.get(&r.id).cloned().map(Ok).unwrap_or_else(||reader.resolve(r));
  let prepared=crate::portable::prepare_accessory_snapshot(&self.root,&card,baseline,self.outfit,slot,resolve)?
   .ok_or("geometry transaction requires a runtime legacy provenance upgrade")?;
  let display_card=super::timings::measure("geometry.display_parse",||crate::card::parse_card_bytes_mode_cached(&raw,self.outfit as i32,false,Some(&mut self.dictionary_cache.borrow_mut())))?;
  let mut source=if self.geometry_template_enabled() {
   let(mut source,shaped)=self.shaped_globals(baseline,resolve,&display_card)?;
   source.parts=prepared.parts;
   // Replay exactly the renderer-dependent tail of the non-live shape policy
   // (accessory reset, zero-step solve, reskin) that the template could not
   // run before these renderers existed; global state is already shaped.
   if shaped{super::timings::measure("geometry.skin",||{source.reset_dynamic_accessories();source.update_dynamic_accessories(0.0);source.skin_all();});}
   source
  }else{
   let mut source=super::timings::measure("geometry.globals",||OkSession::preview_globals(baseline,resolve))?;
   source.parts=prepared.parts;
   super::timings::measure("geometry.shape_policy",||crate::portable::apply_editable_shape_parameters(&mut source,&display_card,self.outfit as i32))?;
   source
  };
  super::timings::measure("geometry.refresh",||source.refresh_portable_render());super::timings::measure("geometry.reference_export",||{source.ensure_skel_export();});
  let export=source.skel_export.as_mut().ok_or("geometry candidate has no prepared rig")?;
  let exports=crate::skel_export::PartExports{morphs:std::mem::take(&mut export.part_morphs),rest:std::mem::take(&mut export.part_rest),
   normals:std::mem::take(&mut export.part_ref_normals),tangents:std::mem::take(&mut export.part_ref_tangents),bone_idx:std::mem::take(&mut export.part_bone_idx)};
  let mut snapshots=super::timings::measure("geometry.snapshot_clone",||self.snapshots.as_ref().clone());snapshots[self.outfit]=prepared.replacement.snapshot;
  let mut timings=prepared.timings;
  for(name,stage)in capture.finish(){timings.insert(name,stage.total_ms);}
  Ok(PreparedGeometry{base_revision:self.revision,target:target.into(),item:key.into(),slot,source,exports,
   retained_indices:prepared.replacement.retained_indices,added_range:prepared.replacement.added_range,
   timings,raw,snapshots:Arc::new(snapshots)})
 }
 #[cfg(not(test))]
 fn geometry_template_enabled(&self)->bool{true}
 #[cfg(test)]
 fn geometry_template_enabled(&self)->bool{std::env::var_os("OK_TEST_GEOMETRY_UNCACHED").is_none()}
 /// Hit and miss both return a session decoded from the same template bytes,
 /// so repeated selections cannot depend on which one shaped the globals.
 fn shaped_globals(&self,baseline:&crate::portable_resources::SplitSnapshot,
  resolve:impl FnMut(&crate::portable_resources::ResourceRef)->Result<Vec<u8>,String>,display:&crate::card::CardInfo)
  ->Result<(OkSession,bool),String>{
  use sha2::Digest;
  let mut globals=super::timings::measure("geometry.globals",||OkSession::preview_globals(baseline,resolve))?;
  let key:[u8;32]=super::timings::measure("geometry.template_key",||->Result<[u8;32],String>{
   let mut hash=sha2::Sha256::new();hash.update(globals.to_preview_bytes()?);
   hash.update(crate::portable::editable_shape_key(display,self.outfit as i32)?);Ok(hash.finalize().into())
  })?;
  if let Some(template)=self.geometry_template.borrow().as_ref().filter(|t|t.key==key){
   let session=super::timings::measure("geometry.template_hit",||OkSession::from_preview_bytes(&template.bytes))?;
   return Ok((session,template.shaped));
  }
  let shaped=super::timings::measure("geometry.shape_policy",||crate::portable::apply_editable_shape_parameters(&mut globals,display,self.outfit as i32))?;
  let bytes=globals.to_preview_bytes()?;
  let session=OkSession::from_preview_bytes(&bytes)?;
  *self.geometry_template.borrow_mut()=Some(GeometryTemplate{key,bytes,shaped});
  Ok((session,shaped))
 }
 /// Complete borrowing validation, including the native publication map.
 /// Returns the dirty category so commit has no fallible step after it.
 pub fn validate_geometry(&self,p:&PreparedGeometry,live:&OkSession)->Result<i32,String>{
  if p.base_revision!=self.revision{return Err("stale geometry candidate".into());}
  if live.shape_body!=p.source.shape_body || live.shape_face!=p.source.shape_face {
   return Err("geometry candidate shape inputs changed".into());
  }
  if p.retained_indices.len()!=live.parts.len() || p.added_range.len()!=p.source.parts.len(){
   return Err("geometry candidate renderer layout changed".into());
  }
  let a=live.skel_export.as_ref().ok_or("geometry publication requires a prepared live rig")?;
  let b=p.source.skel_export.as_ref().ok_or("geometry candidate has no prepared rig")?;
  if a.name_to_idx!=b.name_to_idx || a.bones.len()!=b.bones.len()
   || a.bones.iter().zip(&b.bones).any(|(a,b)|a.name!=b.name || a.parent!=b.parent
    || a.rest_pos!=b.rest_pos || a.rest_rot!=b.rest_rot || a.rest_scl!=b.rest_scl || a.pose_locked!=b.pose_locked) {
   return Err("geometry candidate global rig differs from live rig".into());
  }
  live.validate_accessory_replacement(self.outfit,p.slot,&p.retained_indices,&p.source.parts,&p.exports)?;
  targets().into_iter().find(|t|t.key==p.target).map(|t|t.category).ok_or_else(||"geometry target disappeared".into())
 }
 pub fn geometry_checkpoint(&self,p:&PreparedGeometry)->Result<Checkpoint,String>{
  if p.base_revision!=self.revision{return Err("stale geometry checkpoint".into());}
  Ok(Checkpoint{revision:self.revision+1,base:self.base.clone(),resources:self.resources.clone(),raw:p.raw.clone(),snapshots:p.snapshots.clone()})
 }
 pub fn commit_geometry(&mut self,mut p:PreparedGeometry,live:&mut OkSession)->Result<(),String>{
  let category=self.validate_geometry(&p,live)?;
  live.replace_accessory_parts(self.outfit,p.slot,&p.retained_indices,std::mem::take(&mut p.source.parts),p.exports)?;
  self.raw=p.raw;self.snapshots=p.snapshots;
  self.dirty.insert(category);self.revision+=1;Ok(())
 }
}

#[cfg(test)]mod tests {
 use super::*;
 /// Native mirror of the UI accessory sweep: the first ten catalog items of
 /// OK_GEOMETRY_BENCH_TARGET, prepared and committed in order on one owner
 /// thread, printing stage timings. Diagnostic only; UI timing is acceptance.
 #[test]#[ignore="installed sources and an explicit OK_GEOMETRY_BENCH_CARD"]
 fn geometry_preparation_benchmark(){
  let project=Path::new("C:/portable/OpenKoikatsu");let root=&crate::test_support::game_path("");
  let card=std::path::PathBuf::from(std::env::var("OK_GEOMETRY_BENCH_CARD").unwrap());
  let target=std::env::var("OK_GEOMETRY_BENCH_TARGET").unwrap_or("accessory1_124".into());
  let resources=project.join("Content/IndependentResources/v1");std::env::set_var("OK_BUILTIN_RESOURCES",&resources);
  let mut context=Context::new(root,&card,&resources,&project.join("Saved/AppearanceEditor/cache"),0).unwrap();
  let mut live=crate::portable::load(&card).unwrap();live.ensure_skel_export();
  let category=targets().into_iter().find(|t|t.key==target).unwrap().category;
  let keys=context.items.iter().filter(|i|i.entry.category_no==category).take(10).map(|i|i.key.clone()).collect::<Vec<_>>();
  for(index,key)in keys.iter().enumerate(){
   let started=std::time::Instant::now();let p=context.prepare_geometry(&target,key).unwrap();let ms=started.elapsed().as_secs_f64()*1000.;
   let mut stages=p.timings.iter().filter(|(_,v)|**v>=2.).map(|(k,v)|format!("{k}={v:.1}")).collect::<Vec<_>>();stages.sort();
   eprintln!("BENCH item={index} prepare_ms={ms:.1} {}",stages.join(" "));
   context.commit_geometry(p,&mut live).unwrap();
  }
 }
 fn first_difference(a:&J,b:&J,path:&str)->Option<String>{
  if a==b{return None;}
  match(a,b){
   (J::Object(a),J::Object(b))=>{
    for key in a.keys().chain(b.keys()){
     let next=format!("{path}/{key}");
     match(a.get(key),b.get(key)){
      (Some(a),Some(b))=>if let Some(diff)=first_difference(a,b,&next){return Some(diff)},
      _=>return Some(format!("{next}: missing field")),
     }
    }
   },
   (J::Array(a),J::Array(b))=>{
    if a.len()!=b.len(){return Some(format!("{path}: lengths {} != {}",a.len(),b.len()));}
    for(index,(a,b))in a.iter().zip(b).enumerate(){if let Some(diff)=first_difference(a,b,&format!("{path}/{index}")){return Some(diff)}}
   },
   _=>return Some(format!("{path}: {a} != {b}")),
  }
  Some(format!("{path}: unequal values"))
 }
 #[test]#[ignore="installed runtime head preparation and live-state acceptance"]
 fn sequential_runtime_head_preparation(){
  let project=Path::new("C:/portable/OpenKoikatsu");let root=&crate::test_support::game_path("");
  let base=project.join("Content/CharacterCards/CrowdAPNG/C001.png");
  let mut live=crate::portable::load(&base).unwrap();
  let mut context=Context::new(root,&base,&project.join("Content/IndependentResources/v1"),&project.join("Saved/AppearanceEditor/cache"),0).unwrap();
  let originals=live.parts.iter().map(|p|(p.rest_positions.clone(),p.indices.clone())).collect::<Vec<_>>();
  live.anim_time=42.;let mut rows=Vec::new();
  for target in ["eyebrow","eyeline_up","eyeline_down","eye_white","eye_hi_up","eye_hi_down","nose","pupil0","gradation0","pupil1","gradation1"]{
   let cat=targets().into_iter().find(|t|t.key==target).unwrap().category;
   let keys=context.items.iter().filter(|i|i.entry.category_no==cat).take(10).map(|i|i.key.clone()).collect::<Vec<_>>();
   assert_eq!(keys.len(),10,"ten distinct real sources required for {target}");
   for key in keys{
    let before=std::time::Instant::now();let p=context.prepare(target,&key).unwrap();let ms=before.elapsed().as_secs_f64()*1000.;
    let delta_parts=p.delta.parts.len();let timing=p.timings_ms;let material_resolves=p.material_resolves;
    assert_eq!(p.dictionary_cache_stats[1],0,"head selections preserve the PNG dictionary and must reuse its owned assets");
    // Count source programs independently through canonical JSON, rather
    // than assuming every prepared renderer produces a changed delta. The
    // upper eyeline also prepares its separate kage renderer, which can retain
    // the same PNG and therefore be absent from the final changed-parts list.
    let card=crate::card::parse_card_bytes(&p.raw,0).unwrap();let mut programs=BTreeSet::new();
    for snapshot in context.snapshots.iter(){
     let value=OkSession::preview_residue_value(&snapshot.residue).unwrap();
     for(index,part)in value["session"]["parts"].as_array().unwrap().iter().enumerate(){
      if part["role"]!="head"{continue;}
      let name=part["name"].as_str().unwrap();
      if !crate::head_detail_source_spec(&card,&name.to_ascii_lowercase()).is_some_and(|(category,_,_,_)|
          category==cat||(category==408&&matches!(cat,409..=411))){continue;}
      let reference=snapshot.references.iter().find(|r|r.part==Some(index)&&r.field=="material_definition").unwrap();
      let parameters=if part["source_material"].is_null(){J::Null}else{json!({
       "colors":part["source_material"]["colors"],"floats":part["source_material"]["floats"]})};
      programs.insert(serde_json::to_string(&json!([cat,name,part["shader_kind"],!part["eye_draw"].is_null(),reference.id,parameters])).unwrap());
     }
    }
    assert_eq!(material_resolves,programs.len(),"material preparation must run once per distinct source program across all outfits");
    context.commit(p,&mut live).unwrap();
    for snapshot in context.snapshots.iter(){
     let needed:std::collections::BTreeSet<_>=snapshot.references.iter().map(|r|&r.id).collect();
     assert!(snapshot.unique.keys().all(|id|needed.contains(id)),"selected texture history leaked into the current outfit revision");
    }
    assert_eq!(live.anim_time,42.);
    for (p,(vertices,indices)) in live.parts.iter().zip(&originals){assert_eq!(&p.rest_positions,vertices);assert_eq!(&p.indices,indices);}
    let t=targets().into_iter().find(|t|t.key==target).unwrap();let item=context.items.iter().find(|i|i.key==key).unwrap();
    assert_eq!(blocks_from_card(&context.raw).unwrap(),blocks_from_card(&patch(&context.raw,&t,Some(item),0).unwrap()).unwrap());
    rows.push(json!({"target":target,"item":key,"prepare_ms":ms,"delta_parts":delta_parts,"material_resolves":material_resolves,"valid":true,
     "timings_ms":{"metadata_patch":timing[0],"card_parse":timing[1],"texture_resolve":timing[2],"delta":timing[3]}}));
   }
  }
  let stale=context.prepare("eyebrow",&context.items.iter().find(|i|i.entry.category_no==406).unwrap().key).unwrap();
  let next=context.prepare("eyebrow",&context.items.iter().find(|i|i.entry.category_no==406).unwrap().key).unwrap();
  context.commit(next,&mut live).unwrap();let revision=context.revision();let raw=context.raw();
  assert!(context.commit(stale,&mut live).is_err());assert_eq!(context.revision(),revision);assert_eq!(context.raw(),raw);
  let invalid=context.prepare("eyebrow",&context.items.iter().find(|i|i.entry.category_no==406).unwrap().key).unwrap();
  let shape=live.shape_face.clone();live.shape_face[0]+=0.01;
  assert!(context.commit(invalid,&mut live).is_err());assert_eq!(context.revision(),revision);assert_eq!(context.raw(),raw);
  live.shape_face=shape;
  assert!(context.prepare("head","not-a-source").is_err());assert_eq!(context.revision(),revision);
  let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
  let output=project.join(format!("Saved/Tests/AppearanceSelectors20261010/runtime-checkpoint-{stamp}.png"));
  let checkpoint=context.checkpoint();checkpoint.export(&output).unwrap();
  assert_eq!(checkpoint.revision,context.revision());
  let saved=crate::portable::load(&output).unwrap();
  for(a,b)in live.parts.iter().zip(&saved.parts){assert_eq!(a.name,b.name);assert_eq!(a.png,b.png,"checkpoint lost texture {}",a.name);
   assert_eq!(a.shader_textures,b.shader_textures,"checkpoint lost native texture {}",a.name);
   assert_eq!(a.eye_draw.as_ref().map(|e|&e.textures),b.eye_draw.as_ref().map(|e|&e.textures));}
  assert_eq!(blocks_from_card(&context.raw).unwrap(),blocks_from_card(&crate::gamefs::read(&output).unwrap()).unwrap());
  // Prove every outfit is identical to independent per-outfit parsing,
  // including coordinate-specific extended data and material inputs.
  let original=crate::gamefs::read(&base).unwrap();let(_,span)=crate::portable::png_snapshot(&original).unwrap();
  let baseline=crate::portable_outfits::SnapshotReader::new(&original[span.unwrap()],&context.resources).unwrap();
  let exported=crate::gamefs::read(&output).unwrap();let(_,span)=crate::portable::png_snapshot(&exported).unwrap();
  let exported_reader=crate::portable_outfits::SnapshotReader::new(&exported[span.unwrap()],&context.resources).unwrap();
  let info=crate::card::parse_card_bytes(&context.raw,0).unwrap();
  for outfit in 0..info.coordinate_count{
   let card=crate::card::parse_card_bytes(&context.raw,0).unwrap();
   let mut expected=baseline.snapshot(outfit).unwrap();
   for category in context.dirty_categories(){
    assert!(OkSession::patch_preview_head_texture(&mut expected,category,&context.root,&card,&context.vanilla,&context.mods,&context.guids,|r|baseline.resolve(r)).unwrap().is_some());
   }
   let actual=exported_reader.snapshot(outfit).unwrap();
   // Snapshot structs contain HashMaps: compare semantic fields, not random
   // hash iteration order in the bincode representation.
   let difference=first_difference(&OkSession::preview_residue_value(&expected.residue).unwrap(),&OkSession::preview_residue_value(&actual.residue).unwrap(),"");
   assert!(difference.is_none(),"outfit {outfit} changed renderer state: {difference:?}");
   let refs=|s:&SplitSnapshot|{let mut v=s.references.iter().map(|r|(r.part,r.field.clone(),r.kind.clone(),r.id.clone())).collect::<Vec<_>>();v.sort();v};
   assert_eq!(refs(&expected),refs(&actual),"outfit {outfit} changed texture/source references");
  }
  let report=project.join(format!("Saved/Tests/AppearanceSelectors20261010/runtime-head-preparation-{stamp}.json"));
  fs::write(&report,serde_json::to_vec_pretty(&json!({"valid":true,"renderer_verified":false,"stale_candidate_rejected":true,"checkpoint_verified":true,"checkpoint":output,"dirty_categories":context.dirty_categories(),"cases":rows})).unwrap()).unwrap();
  // A retained revision must survive subsequent editing and loss of its
  // originating context. It must not reopen the baseline path or game root.
  let next=context.prepare("eyebrow",&context.items.iter().find(|i|i.entry.category_no==406).unwrap().key).unwrap();
  context.commit(next,&mut live).unwrap();
  context.base=Arc::new(vec![0;16]);context.root=project.join("Saved/Tests/MissingCheckpointGameRoot");
  drop(context);
  let retained=output.with_extension("retained.png");checkpoint.export(&retained).unwrap();
  assert_eq!(crate::gamefs::read(&retained).unwrap(),crate::gamefs::read(&output).unwrap(),"retained checkpoint must not drift after another commit");
  assert!(checkpoint.export(&retained).is_err(),"must reject replacing an existing output");
  eprintln!("runtime head preparation report {}",report.display());
 }
}
