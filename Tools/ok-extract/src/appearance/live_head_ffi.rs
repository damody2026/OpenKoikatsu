//! Owned, additive C ABI for preparing texture candidates off the game thread.
//! Buffers are copied to caller storage; no borrowed texture pointer escapes.
//! The caller must join pending calls before freeing a handle, and access a
//! live OkSession exclusively on its owning thread during validate/commit.
use super::live_head::{Context,Prepared,PreparedGeometry,Checkpoint};
use crate::{ffi::{cstr_opt,copy_resource,write_err},session::OkSession};
use std::{ffi::c_char,path::{Path,PathBuf},ptr,sync::{Arc,Mutex,mpsc},thread};
enum SourceCommand{Prepare(String,String,mpsc::Sender<Result<Prepared,String>>),PrepareGeometry(String,String,mpsc::Sender<Result<PreparedGeometry,String>>),Stop}
pub struct AppearanceContext{
 value:Arc<Mutex<Context>>,commands:mpsc::Sender<SourceCommand>,worker:Option<thread::JoinHandle<()>>,
}
pub struct AppearanceCandidate(Mutex<Option<Prepared>>);
pub struct AppearanceGeometryCandidate(pub(super) Mutex<Option<PreparedGeometry>>);
pub struct AppearanceCheckpoint(Checkpoint);
fn guarded<T>(f:impl FnOnce()->Result<T,String>)->Result<T,String>{
 std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or_else(|_|Err("appearance transaction panicked".into()))
}
impl AppearanceContext{
 fn open(root:PathBuf,card:PathBuf,resources:PathBuf,cache:PathBuf,outfit:usize)->Result<Self,String>{
  let(commands,requests)=mpsc::channel();let(initialized,ready)=mpsc::channel();
  let worker=thread::Builder::new().name("ok-appearance-source".into()).spawn(move||{
   let context=match guarded(||Context::new(&root,&card,&resources,&cache,outfit)){
    Ok(context)=>Arc::new(Mutex::new(context)),Err(error)=>{let _=initialized.send(Err(error));return;}
   };
   if initialized.send(Ok(context.clone())).is_err(){return;}
   while let Ok(command)=requests.recv(){match command{
    SourceCommand::Stop=>break,
    SourceCommand::Prepare(target,item,reply)=>{
     // ParsedBundle caches contain RefCells and are intentionally local to a
     // thread. Arbitrary UE pool dispatch used to decode the same bundles on
     // different threads. Give each editing context a stable source owner;
     // never mark those parser caches Send/Sync or hold a global parser lock.
     let result=guarded(||context.lock().map_err(|_|"appearance context poisoned")?.prepare(&target,&item));
     let _=reply.send(result);
    }
    SourceCommand::PrepareGeometry(target,item,reply)=>{
     let result=guarded(||context.lock().map_err(|_|"appearance context poisoned")?.prepare_geometry(&target,&item));
     let _=reply.send(result);
    }
   }}
  }).map_err(|e|e.to_string())?;
  match ready.recv(){
   Ok(Ok(value))=>Ok(Self{value,commands,worker:Some(worker)}),
   result=>{let _=worker.join();Err(match result{Ok(Err(error))=>error,_=>"appearance source worker did not initialize".into()})}
  }
 }
 fn prepare(&self,target:String,item:String)->Result<Prepared,String>{
  let(reply,result)=mpsc::channel();self.commands.send(SourceCommand::Prepare(target,item,reply)).map_err(|_|"appearance source worker unavailable")?;
  result.recv().map_err(|_|"appearance source preparation interrupted")?
 }
 fn prepare_geometry(&self,target:String,item:String)->Result<PreparedGeometry,String>{
  let(reply,result)=mpsc::channel();self.commands.send(SourceCommand::PrepareGeometry(target,item,reply)).map_err(|_|"appearance source worker unavailable")?;
  result.recv().map_err(|_|"appearance geometry preparation interrupted")?
 }
}
impl Drop for AppearanceContext{
 fn drop(&mut self){let _=self.commands.send(SourceCommand::Stop);if let Some(worker)=self.worker.take(){let _=worker.join();}}
}
unsafe fn finish(result:Result<(),String>,err:*mut c_char,cap:i32)->i32{
 match result{Ok(())=>{write_err(err,cap,"");0},Err(e)=>{write_err(err,cap,&e);-1}}
}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_context_open(root:*const c_char,card:*const c_char,resources:*const c_char,cache:*const c_char,outfit:i32,out:*mut *mut AppearanceContext,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance context output".into()),err,cap);}
 *out=ptr::null_mut();
 finish(guarded(||{
  let(Some(root),Some(card),Some(resources),Some(cache))=(cstr_opt(root),cstr_opt(card),cstr_opt(resources),cstr_opt(cache))else{return Err("missing appearance context paths".into())};
  if outfit<0{return Err("invalid appearance outfit".into());}
  let context=AppearanceContext::open(root.into(),card.into(),resources.into(),cache.into(),outfit as usize)?;
  *out=Box::into_raw(Box::new(context));Ok(())
 }),err,cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_context_free(p:*mut AppearanceContext){if !p.is_null(){drop(Box::from_raw(p));}}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_candidate_free(p:*mut AppearanceCandidate){if !p.is_null(){drop(Box::from_raw(p));}}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_checkpoint_free(p:*mut AppearanceCheckpoint){if !p.is_null(){drop(Box::from_raw(p));}}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_prepare(context:*mut AppearanceContext,target:*const c_char,item:*const c_char,out:*mut *mut AppearanceCandidate,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance candidate output".into()),err,cap);}*out=ptr::null_mut();
 finish(guarded(||{
  let context=context.as_ref().ok_or("missing appearance context")?;
  let(Some(target),Some(item))=(cstr_opt(target),cstr_opt(item))else{return Err("missing appearance selection".into())};
  let p=context.prepare(target,item)?;
  *out=Box::into_raw(Box::new(AppearanceCandidate(Mutex::new(Some(p)))));Ok(())
 }),err,cap)
}
/// JSON describes only changed renderers, sampler settings and timings. Pixel
/// data is obtained separately so metadata never embeds multi-megabyte arrays.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_candidate_info(candidate:*const AppearanceCandidate,out:*mut u8,cap:i32)->i32{
 guarded(||{
  let p=candidate.as_ref().ok_or("missing appearance candidate")?.0.lock().map_err(|_|"appearance candidate poisoned")?;
  let p=p.as_ref().ok_or("appearance candidate already committed")?;
  let parts=p.delta.parts.iter().map(|d|serde_json::json!({"index":d.index,"name":d.name,"shader_kind":d.shader_kind,
   "main_bytes":d.png.len(),"runtime_textures":d.runtime_textures,"eye_textures":d.eye_textures.as_ref().map(|t|t.iter().map(|t|t.as_ref().map_or(0,Vec::len)).collect::<Vec<_>>())})).collect::<Vec<_>>();
  let bytes=serde_json::to_vec(&serde_json::json!({"base_revision":p.base_revision,"target":p.target,"item":p.item,"category":p.category,"timings_ms":p.timings_ms,"preparation_thread":p.preparation_thread,"thread_cpu_timings_ms":p.thread_cpu_timings_ms,"material_resolves":p.material_resolves,"dictionary_cache_stats":p.dictionary_cache_stats,"card_parse_phases":p.card_parse_phases,"source_stages":p.source_stages,"parts":parts})).map_err(|e|e.to_string())?;
  Ok(copy_resource(&bytes,out,cap))
 }).unwrap_or(-1)
}
/// Card blocks for the exact uncommitted candidate revision.
/// Extended-data blocks can contain large source textures; this is not a delta.
/// Callers must pin the matching checkpoint before saving portable resources.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_candidate_card(candidate:*const AppearanceCandidate,out:*mut u8,cap:i32)->i32{
 guarded(||{
  let p=candidate.as_ref().ok_or("missing appearance candidate")?.0.lock().map_err(|_|"appearance candidate poisoned")?;
  let p=p.as_ref().ok_or("appearance candidate already committed")?;
  Ok(copy_resource(&p.raw,out,cap))
 }).unwrap_or(-1)
}
/// slot 0=main, 1=upper highlight, 2=lower highlight; part is a session index.
/// Empty optional eye textures return zero. Invalid indices/slots return -1.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_candidate_texture(candidate:*const AppearanceCandidate,part:i32,slot:i32,out:*mut u8,cap:i32)->i32{
 guarded(||{
  if part<0||!(0..=2).contains(&slot){return Err("invalid appearance texture index".into());}
  let p=candidate.as_ref().ok_or("missing appearance candidate")?.0.lock().map_err(|_|"appearance candidate poisoned")?;
  let p=p.as_ref().ok_or("appearance candidate already committed")?;
  let d=p.delta.parts.iter().find(|d|d.index==part as usize).ok_or("unchanged appearance renderer")?;
  let bytes=if slot==0{d.png.as_slice()}else{d.eye_textures.as_ref().and_then(|t|t[(slot-1)as usize].as_deref()).unwrap_or(&[])};
  Ok(copy_resource(bytes,out,cap))
 }).unwrap_or(-1)
}
/// Validation is available before allocating UE textures. Commit revalidates
/// revision and renderer state, and consumes the candidate only on success.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_candidate_commit(context:*mut AppearanceContext,candidate:*mut AppearanceCandidate,live:*mut OkSession,apply:i32,err:*mut c_char,cap:i32)->i32{
 finish(guarded(||{
  if !(0..=1).contains(&apply){return Err("invalid appearance commit mode".into());}
  let mut context=context.as_ref().ok_or("missing appearance context")?.value.lock().map_err(|_|"appearance context poisoned")?;
  let mut candidate=candidate.as_ref().ok_or("missing appearance candidate")?.0.lock().map_err(|_|"appearance candidate poisoned")?;
  let p=candidate.as_ref().ok_or("appearance candidate already committed")?;
  let live=live.as_mut().ok_or("missing appearance live session")?;
  if context.revision()!=p.base_revision{return Err("stale appearance candidate".into());}
  live.validate_head_texture_delta(&p.delta)?;
  if apply==1{context.commit(candidate.take().unwrap(),live)?;}
  Ok(())
 }),err,cap)
}
/// Capture under the context lock; export runs on an independent owned handle
/// so later selections cannot be blocked by a whole-card checkpoint.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_checkpoint_capture(context:*mut AppearanceContext,out:*mut *mut AppearanceCheckpoint,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance checkpoint output".into()),err,cap);}*out=ptr::null_mut();
 finish(guarded(||{
  let context=context.as_ref().ok_or("missing appearance context")?.value.lock().map_err(|_|"appearance context poisoned")?;
  *out=Box::into_raw(Box::new(AppearanceCheckpoint(context.checkpoint())));Ok(())
 }),err,cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_checkpoint_revision(checkpoint:*const AppearanceCheckpoint)->u64{checkpoint.as_ref().map_or(u64::MAX,|p|p.0.revision)}
/// Pin a complete durable candidate before committing any live state.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_candidate_checkpoint(context:*mut AppearanceContext,candidate:*const AppearanceCandidate,out:*mut *mut AppearanceCheckpoint,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance checkpoint output".into()),err,cap);}*out=ptr::null_mut();
 finish(guarded(||{
  let context=context.as_ref().ok_or("missing appearance context")?.value.lock().map_err(|_|"appearance context poisoned")?;
  let candidate=candidate.as_ref().ok_or("missing appearance candidate")?.0.lock().map_err(|_|"appearance candidate poisoned")?;
  let p=candidate.as_ref().ok_or("appearance candidate already committed")?;
  *out=Box::into_raw(Box::new(AppearanceCheckpoint(context.prepared_checkpoint(p)?)));Ok(())
 }),err,cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_checkpoint_export(checkpoint:*const AppearanceCheckpoint,path:*const c_char,err:*mut c_char,cap:i32)->i32{
 finish(guarded(||{
  let checkpoint=checkpoint.as_ref().ok_or("missing appearance checkpoint")?;
  let path=cstr_opt(path).ok_or("missing appearance checkpoint path")?;
  checkpoint.0.export(Path::new(&path))?;Ok(())
 }),err,cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_geometry_free(p:*mut AppearanceGeometryCandidate){if !p.is_null(){drop(Box::from_raw(p));}}
/// Exact-slot accessory preparation on the context's stable source owner.
/// Nothing live or durable changes; the candidate owns its renderers.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_prepare_geometry(context:*mut AppearanceContext,target:*const c_char,item:*const c_char,out:*mut *mut AppearanceGeometryCandidate,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance geometry output".into()),err,cap);}*out=ptr::null_mut();
 finish(guarded(||{
  let context=context.as_ref().ok_or("missing appearance context")?;
  let(Some(target),Some(item))=(cstr_opt(target),cstr_opt(item))else{return Err("missing appearance selection".into())};
  let started=std::time::Instant::now();
  let mut p=context.prepare_geometry(target,item)?;
  p.timings.insert("ffi_prepare_total",started.elapsed().as_secs_f64()*1000.);
  *out=Box::into_raw(Box::new(AppearanceGeometryCandidate(Mutex::new(Some(p)))));Ok(())
 }),err,cap)
}
/// JSON identity, exact renderer remap and added renderer summaries.
/// UE uses `added_range.start` as the offset of new session indices.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_geometry_info(candidate:*const AppearanceGeometryCandidate,out:*mut u8,cap:i32)->i32{
 guarded(||{
  let p=candidate.as_ref().ok_or("missing appearance geometry candidate")?.0.lock().map_err(|_|"appearance geometry candidate poisoned")?;
  let p=p.as_ref().ok_or("appearance geometry candidate already committed")?;
  let parts=p.source.parts.iter().enumerate().map(|(i,d)|serde_json::json!({"index":p.added_range.start+i,"name":d.name,"role":d.role,
   "source_property":d.source_property,"vertices":d.rest_positions.len(),"indices":d.indices.len(),"shader_kind":d.shader_kind,"main_bytes":d.png.as_ref().map_or(0,Vec::len)})).collect::<Vec<_>>();
  let bytes=serde_json::to_vec(&serde_json::json!({"base_revision":p.base_revision,"target":p.target,"item":p.item,"slot":p.slot,
   "retained_indices":p.retained_indices,"added_range":[p.added_range.start,p.added_range.end],"timings_ms":p.timings,"parts":parts})).map_err(|e|e.to_string())?;
  Ok(copy_resource(&bytes,out,cap))
 }).unwrap_or(-1)
}
/// Card blocks for the exact uncommitted geometry revision.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_geometry_card(candidate:*const AppearanceGeometryCandidate,out:*mut u8,cap:i32)->i32{
 guarded(||{
  let p=candidate.as_ref().ok_or("missing appearance geometry candidate")?.0.lock().map_err(|_|"appearance geometry candidate poisoned")?;
  let p=p.as_ref().ok_or("appearance geometry candidate already committed")?;
  Ok(copy_resource(&p.raw,out,cap))
 }).unwrap_or(-1)
}
/// Independent owned session holding only the added renderers and the global
/// rig, for unregistered UE staging. It never aliases candidate storage, and
/// it is not a publication input: native commit consumes the original
/// candidate, whose renderers retain their exact card-property provenance.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_geometry_stage(candidate:*const AppearanceGeometryCandidate,out:*mut *mut OkSession,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance geometry stage output".into()),err,cap);}*out=ptr::null_mut();
 finish(guarded(||{
  let p=candidate.as_ref().ok_or("missing appearance geometry candidate")?.0.lock().map_err(|_|"appearance geometry candidate poisoned")?;
  let p=p.as_ref().ok_or("appearance geometry candidate already committed")?;
  let mut stage=OkSession::from_preview_bytes(&p.source.to_preview_bytes()?)?;
  stage.ensure_skel_export();
  *out=Box::into_raw(Box::new(stage));Ok(())
 }),err,cap)
}
/// Pin the complete durable next revision before any live publication.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_geometry_checkpoint(context:*mut AppearanceContext,candidate:*const AppearanceGeometryCandidate,out:*mut *mut AppearanceCheckpoint,err:*mut c_char,cap:i32)->i32{
 if out.is_null(){return finish(Err("missing appearance checkpoint output".into()),err,cap);}*out=ptr::null_mut();
 finish(guarded(||{
  let context=context.as_ref().ok_or("missing appearance context")?.value.lock().map_err(|_|"appearance context poisoned")?;
  let candidate=candidate.as_ref().ok_or("missing appearance geometry candidate")?.0.lock().map_err(|_|"appearance geometry candidate poisoned")?;
  let p=candidate.as_ref().ok_or("appearance geometry candidate already committed")?;
  *out=Box::into_raw(Box::new(AppearanceCheckpoint(context.geometry_checkpoint(p)?)));Ok(())
 }),err,cap)
}
/// apply=0 validates only. apply=1 performs the complete borrowing validation
/// first and transfers ownership only after it passes, so every foreseeable
/// rejection (stale, rig, shape, map, provenance) retains the candidate.
#[no_mangle]
pub unsafe extern "C" fn ok_appearance_geometry_commit(context:*mut AppearanceContext,candidate:*mut AppearanceGeometryCandidate,live:*mut OkSession,apply:i32,err:*mut c_char,cap:i32)->i32{
 finish(guarded(||{
  if !(0..=1).contains(&apply){return Err("invalid appearance commit mode".into());}
  let mut context=context.as_ref().ok_or("missing appearance context")?.value.lock().map_err(|_|"appearance context poisoned")?;
  let mut candidate=candidate.as_ref().ok_or("missing appearance geometry candidate")?.0.lock().map_err(|_|"appearance geometry candidate poisoned")?;
  let p=candidate.as_ref().ok_or("appearance geometry candidate already committed")?;
  let live=live.as_mut().ok_or("missing appearance live session")?;
  context.validate_geometry(p,live)?;
  if apply==1{context.commit_geometry(candidate.take().unwrap(),live)?;}
  Ok(())
 }),err,cap)
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn invalid_handles_and_buffers_are_rejected(){unsafe{
  let mut err=[0i8;128];let mut context=ptr::null_mut();let mut candidate=ptr::null_mut();let mut checkpoint=ptr::null_mut();
  assert_eq!(ok_appearance_context_open(ptr::null(),ptr::null(),ptr::null(),ptr::null(),-1,&mut context,err.as_mut_ptr(),128),-1);assert!(context.is_null());
  assert_eq!(ok_appearance_prepare(context,ptr::null(),ptr::null(),&mut candidate,err.as_mut_ptr(),128),-1);assert!(candidate.is_null());
  assert_eq!(ok_appearance_candidate_info(candidate,ptr::null_mut(),0),-1);
  assert_eq!(ok_appearance_candidate_texture(candidate,-1,0,ptr::null_mut(),0),-1);
  assert_eq!(ok_appearance_candidate_commit(context,candidate,ptr::null_mut(),1,err.as_mut_ptr(),128),-1);
  assert_eq!(ok_appearance_checkpoint_capture(context,&mut checkpoint,err.as_mut_ptr(),128),-1);assert!(checkpoint.is_null());
  assert_eq!(ok_appearance_candidate_checkpoint(context,candidate,&mut checkpoint,err.as_mut_ptr(),128),-1);assert!(checkpoint.is_null());
  assert_eq!(ok_appearance_candidate_card(candidate,ptr::null_mut(),0),-1);
  assert_eq!(ok_appearance_checkpoint_revision(checkpoint),u64::MAX);
  assert_eq!(ok_appearance_checkpoint_export(checkpoint,ptr::null(),err.as_mut_ptr(),128),-1);
  let mut geometry=ptr::null_mut();let mut stage=ptr::null_mut();
  assert_eq!(ok_appearance_prepare_geometry(context,ptr::null(),ptr::null(),&mut geometry,err.as_mut_ptr(),128),-1);assert!(geometry.is_null());
  assert_eq!(ok_appearance_prepare_geometry(context,ptr::null(),ptr::null(),ptr::null_mut(),err.as_mut_ptr(),128),-1);
  assert_eq!(ok_appearance_geometry_info(geometry,ptr::null_mut(),0),-1);
  assert_eq!(ok_appearance_geometry_card(geometry,ptr::null_mut(),0),-1);
  assert_eq!(ok_appearance_geometry_stage(geometry,&mut stage,err.as_mut_ptr(),128),-1);assert!(stage.is_null());
  assert_eq!(ok_appearance_geometry_stage(geometry,ptr::null_mut(),err.as_mut_ptr(),128),-1);
  assert_eq!(ok_appearance_geometry_checkpoint(context,geometry,&mut checkpoint,err.as_mut_ptr(),128),-1);assert!(checkpoint.is_null());
  assert_eq!(ok_appearance_geometry_commit(context,geometry,ptr::null_mut(),1,err.as_mut_ptr(),128),-1);
  assert_eq!(ok_appearance_geometry_commit(context,geometry,ptr::null_mut(),2,err.as_mut_ptr(),128),-1);
  ok_appearance_geometry_free(geometry);
  ok_appearance_context_free(context);ok_appearance_candidate_free(candidate);ok_appearance_checkpoint_free(checkpoint);
 }}
 #[test]#[ignore="installed APNG, source catalog and resource acceptance"]
 fn owned_candidate_and_checkpoint_lifecycle(){unsafe{
  use std::ffi::CString;
  let root=&crate::test_support::game_path("");let project=Path::new("C:/portable/OpenKoikatsu");
  let card=project.join("Content/CharacterCards/CrowdAPNG/C001.png");let resources=project.join("Content/IndependentResources/v1");let cache=project.join("Saved/AppearanceEditor/cache");
  let strings=[root.to_path_buf(),card.clone(),resources,cache.clone()].map(|p|CString::new(p.to_string_lossy().as_bytes()).unwrap());
  let mut err=[0i8;1024];let mut context=ptr::null_mut();
  assert_eq!(ok_appearance_context_open(strings[0].as_ptr(),strings[1].as_ptr(),strings[2].as_ptr(),strings[3].as_ptr(),0,&mut context,err.as_mut_ptr(),1024),0,"{}",std::ffi::CStr::from_ptr(err.as_ptr()).to_string_lossy());
  let items=super::super::catalog(root,&cache).unwrap();let item=items.iter().find(|i|i.entry.category_no==406).unwrap();
  let target=CString::new("eyebrow").unwrap();let item=CString::new(item.key.clone()).unwrap();
  let context_address=context as usize;
  let prepare=||{let mut candidate=ptr::null_mut();let mut err=[0i8;1024];assert_eq!(ok_appearance_prepare(context_address as *mut AppearanceContext,target.as_ptr(),item.as_ptr(),&mut candidate,err.as_mut_ptr(),1024),0);candidate as usize};
  let addresses=thread::scope(|scope|{let a=scope.spawn(&prepare);let b=scope.spawn(&prepare);[a.join().unwrap(),b.join().unwrap()]});
  let candidate=addresses[0]as *mut AppearanceCandidate;let stale=addresses[1]as *mut AppearanceCandidate;
  let count=ok_appearance_candidate_info(candidate,ptr::null_mut(),0);assert!(count>0);
  let mut bytes=vec![0u8;count as usize];assert_eq!(ok_appearance_candidate_info(candidate,bytes.as_mut_ptr(),count),count);
  let info:serde_json::Value=serde_json::from_slice(&bytes).unwrap();assert_eq!(info["base_revision"],0);
  let stale_count=ok_appearance_candidate_info(stale,ptr::null_mut(),0);assert!(stale_count>0);let mut stale_bytes=vec![0u8;stale_count as usize];assert_eq!(ok_appearance_candidate_info(stale,stale_bytes.as_mut_ptr(),stale_count),stale_count);
  let stale_info:serde_json::Value=serde_json::from_slice(&stale_bytes).unwrap();
  assert_eq!(info["preparation_thread"],stale_info["preparation_thread"],"different callers must reuse the same source owner and parser cache");
  assert_ne!(info["preparation_thread"].as_str().unwrap(),format!("{:?}",thread::current().id()));
  for part in info["parts"].as_array().unwrap(){
   let index=part["index"].as_i64().unwrap()as i32;
   let n=ok_appearance_candidate_texture(candidate,index,0,ptr::null_mut(),0);assert!(n>0);
   let mut small=[123u8;1];assert_eq!(ok_appearance_candidate_texture(candidate,index,0,small.as_mut_ptr(),1),-1);assert_eq!(small,[123]);
   let mut png=vec![0u8;n as usize];assert_eq!(ok_appearance_candidate_texture(candidate,index,0,png.as_mut_ptr(),n),n);assert!(image::load_from_memory(&png).is_ok());
  }
  let raw_count=ok_appearance_candidate_card(candidate,ptr::null_mut(),0);assert!(raw_count>0);
  let mut small=[123u8;1];assert_eq!(ok_appearance_candidate_card(candidate,small.as_mut_ptr(),1),-1);assert_eq!(small,[123]);
  let mut raw=vec![0u8;raw_count as usize];assert_eq!(ok_appearance_candidate_card(candidate,raw.as_mut_ptr(),raw_count),raw_count);
  assert!(crate::card::parse_card_bytes(&raw,0).is_ok());
  let mut live=crate::portable::load(&card).unwrap();live.anim_time=42.;
  let mut prepared_checkpoint=ptr::null_mut();
  assert_eq!(ok_appearance_candidate_checkpoint(context,candidate,&mut prepared_checkpoint,err.as_mut_ptr(),1024),0);
  assert_eq!(ok_appearance_checkpoint_revision(prepared_checkpoint),1);
  assert_eq!(ok_appearance_candidate_commit(context,candidate,&mut live,0,err.as_mut_ptr(),1024),0);
  assert!(ok_appearance_candidate_info(candidate,ptr::null_mut(),0)>0,"validation must not consume");
  assert_eq!(ok_appearance_candidate_commit(context,candidate,&mut live,1,err.as_mut_ptr(),1024),0);
  assert_eq!(live.anim_time,42.);assert_eq!(ok_appearance_candidate_info(candidate,ptr::null_mut(),0),-1);
  assert_eq!(ok_appearance_candidate_card(candidate,ptr::null_mut(),0),-1);
  let mut consumed_checkpoint=ptr::null_mut();
  assert_eq!(ok_appearance_candidate_checkpoint(context,candidate,&mut consumed_checkpoint,err.as_mut_ptr(),1024),-1);assert!(consumed_checkpoint.is_null());
  assert_eq!(ok_appearance_candidate_checkpoint(context,stale,&mut consumed_checkpoint,err.as_mut_ptr(),1024),-1);assert!(consumed_checkpoint.is_null());
  assert_eq!(ok_appearance_candidate_commit(context,stale,&mut live,1,err.as_mut_ptr(),1024),-1);
  assert!(ok_appearance_candidate_info(stale,ptr::null_mut(),0)>0,"failed commit must retain candidate");
  let mut checkpoint=ptr::null_mut();assert_eq!(ok_appearance_checkpoint_capture(context,&mut checkpoint,err.as_mut_ptr(),1024),0);assert_eq!(ok_appearance_checkpoint_revision(checkpoint),1);
  ok_appearance_candidate_free(candidate);ok_appearance_candidate_free(stale);ok_appearance_context_free(context);
  // The checkpoint owns all state and survives its originating context.
  let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
  let output=project.join(format!("Saved/Tests/AppearanceSelectors20261010/ffi-checkpoint-{stamp}.png"));let path=CString::new(output.to_string_lossy().as_bytes()).unwrap();
  assert_eq!(ok_appearance_checkpoint_export(checkpoint,path.as_ptr(),err.as_mut_ptr(),1024),0,"{}",std::ffi::CStr::from_ptr(err.as_ptr()).to_string_lossy());ok_appearance_checkpoint_free(checkpoint);
  let prepared_path=output.with_extension("prepared.png");let prepared_path_c=CString::new(prepared_path.to_string_lossy().as_bytes()).unwrap();
  assert_eq!(ok_appearance_checkpoint_export(prepared_checkpoint,prepared_path_c.as_ptr(),err.as_mut_ptr(),1024),0,"{}",std::ffi::CStr::from_ptr(err.as_ptr()).to_string_lossy());
  ok_appearance_checkpoint_free(prepared_checkpoint);
  assert_eq!(crate::gamefs::read(&prepared_path).unwrap(),crate::gamefs::read(&output).unwrap(),"precommit journal and committed checkpoint must match exactly");
  assert_eq!(super::super::blocks_from_card(&raw).unwrap(),super::super::blocks_from_card(&crate::gamefs::read(&output).unwrap()).unwrap(),"candidate metadata and pinned checkpoint must represent exactly the same revision");
  let saved=crate::portable::load(&output).unwrap();
  for(a,b)in live.parts.iter().zip(&saved.parts){assert_eq!(a.png,b.png);assert_eq!(a.shader_textures,b.shader_textures);}
 }}
}
