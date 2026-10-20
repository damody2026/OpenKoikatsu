//! Data-driven HitReaction.HitPointEffector core. No character-specific curves.
//! A caller supplies the original Unity fields and consumes offsets before IK.
use crate::dbone::{CurveKey,parse_curve,curve_value};
use crate::finalik::{V3,vadd,vlen};
use unity_asset_core::UnityValue;
use std::sync::Arc;
fn vscale(v:V3,s:f32)->V3{[v[0]*s,v[1]*s,v[2]*s]}

#[derive(Debug)]
struct Point {fade_time:f32,force:Vec<CurveKey>,up:Vec<CurveKey>,links:Vec<(usize,f32)>,length:f32}
#[derive(Debug)]
struct Group {id:i32,points:Vec<Point>}
#[derive(Debug)]
pub(crate) struct Definition {groups:Vec<Group>}
fn field<'a>(v:&'a UnityValue,name:&str)->Option<&'a UnityValue>{match v{UnityValue::Object(v)=>v.get(name),_=>None}}
fn number(v:Option<&UnityValue>)->Result<f32,String>{let x=v.and_then(UnityValue::as_f64).ok_or("missing reaction number")? as f32;if x.is_finite(){Ok(x)}else{Err("nonfinite reaction number".into())}}
fn array(v:Option<&UnityValue>)->Result<&[UnityValue],String>{v.and_then(UnityValue::as_array).map(Vec::as_slice).ok_or_else(||"missing reaction array".into())}
fn curve(v:Option<&UnityValue>)->Result<Vec<CurveKey>,String>{
 let keys=array(v.and_then(|v|field(v,"m_Curve")))?;
 if keys.len()>256{return Err("reaction curve too large".into());}
 for key in keys {
  number(field(key,"time"))?;number(field(key,"value"))?;
  for n in ["inSlope","outSlope"]{let x=field(key,n).and_then(UnityValue::as_f64).ok_or("missing reaction tangent")?;if x.is_nan(){return Err("NaN reaction tangent".into());}}
  for n in ["inWeight","outWeight"]{if field(key,n).is_some(){let w=number(field(key,n))?;if !(0.0..=1.0).contains(&w){return Err("reaction tangent weight out of range".into());}}}
  if let Some(mode)=field(key,"weightedMode"){if !mode.as_i64().is_some_and(|m|(0..=3).contains(&m)){return Err("invalid reaction weighted mode".into());}}
 }
 let out=parse_curve(v);
 if out.iter().any(|k|k.time<0.)||out.windows(2).any(|p|p[0].time>=p[1].time){return Err("reaction curve times must be distinct and nonnegative".into());}
 Ok(out)
}
impl Definition {
 pub fn parse(value:&UnityValue)->Result<Arc<Self>,String>{
  let rows=array(field(value,"effectorHit"))?;
  if rows.is_empty()||rows.len()>128{return Err("invalid reaction group count".into());}
  let mut groups=Vec::new();let mut ids=std::collections::HashSet::new();
  for row in rows {
   let id=field(row,"id").and_then(UnityValue::as_i64).and_then(|i|i32::try_from(i).ok()).ok_or("invalid reaction id")?;
   if !ids.insert(id){return Err("duplicate reaction id".into());}
   let items=array(field(row,"effectorHitPoints"))?;
   if items.is_empty()||items.len()>32{return Err("invalid reaction point count".into());}
   let mut points=Vec::new();
   for item in items {
    let fade_time=number(field(item,"crossFadeTime"))?;
    let force=curve(field(item,"offsetInForceDirection"))?;let up=curve(field(item,"offsetInUpDirection"))?;
    let length=force.last().map_or(0.,|k|k.time).max(up.last().map_or(0.,|k|k.time));
    let raw_links=array(field(item,"effectorLinks"))?;
    if raw_links.len()>64{return Err("too many reaction links".into());}
    let mut links=Vec::new();for link in raw_links {
     let id=field(link,"effector").and_then(UnityValue::as_i64).filter(|id|(0..9).contains(id)).ok_or("invalid reaction effector")? as usize;
     links.push((id,number(field(link,"weight"))?));
    }
    points.push(Point{fade_time,force,up,links,length});
   }
   groups.push(Group{id,points});
  }
  Ok(Arc::new(Self{groups}))
 }
}
#[derive(Default,Debug)]
struct LinkState {last:V3,current:V3}
#[derive(Debug)]
struct PointState {timer:f32,fade:f32,force:V3,links:Vec<LinkState>,active:bool}
pub(crate) struct Runtime {definition:Arc<Definition>,states:Vec<Vec<PointState>>}

/// Mutable state belongs to the live character, never to a shared curve cache
/// or the portable APNG payload. The standalone solver is prepared once.
pub(crate) struct Interaction {
 pub reaction:Runtime,
 pub weight:f32,
 pub release_mask:u8,
 pub solver:crate::finalik::IKSolverFullBodyBiped,
}
impl Interaction {
 pub fn offsets(&mut self,dt:f32,root_rotation:[f32;4])->[V3;9]{
  if !self.reaction.is_playing(){self.release_mask=0;return [[0.;3];9];}
  self.reaction.advance(dt,self.weight,crate::finalik::qrot(root_rotation,[0.,1.,0.])).unwrap_or([[0.;3];9])
 }
 pub fn apply_to_solver(&self,solver:&mut crate::finalik::IKSolverFullBodyBiped,offsets:[V3;9]){
  for(e,o)in solver.effectors.iter_mut().zip(offsets){e.position_offset=vadd(e.position_offset,o);}
  let progress=self.reaction.release_progress();
  for (bit,effector) in [5,6,7,8].into_iter().enumerate(){if self.release_mask&(1<<bit)!=0{
   solver.effectors[effector].position_weight=progress;
   if bit>=2{solver.effectors[effector].rotation_weight=progress;}
  }}
 }
}
impl Runtime {
 fn release_progress(&self)->f32{
  let mut value=1.;
  for(group,states)in self.definition.groups.iter().zip(&self.states){
   if let Some((point,state))=group.points.iter().zip(states).find(|(_,s)|s.active){value=state.timer/point.length;}
  }
  value
 }
 pub fn clear(&mut self){
  for state in self.states.iter_mut().flatten(){
   state.timer=0.;state.fade=0.;state.force=[0.;3];state.active=false;
   for link in &mut state.links{*link=LinkState::default();}
  }
 }
 pub fn new(definition:Arc<Definition>)->Self{
  let states=definition.groups.iter().map(|g|g.points.iter().map(|p|PointState{timer:0.,fade:0.,force:[0.;3],links:p.links.iter().map(|_|LinkState::default()).collect(),active:false}).collect()).collect();
  Self{definition,states}
 }
 pub fn hit(&mut self,id:i32,forces:&[V3])->bool{
  let Some(i)=self.definition.groups.iter().position(|g|g.id==id)else{return false;};
  if forces.len()!=self.states[i].len()||forces.iter().any(|v|!v.iter().all(|f|f.is_finite())||!vlen(*v).is_finite()){return false;}
  for ((point,state),force) in self.definition.groups[i].points.iter().zip(&mut self.states[i]).zip(forces){
   if point.length<=0.{continue;}
   if state.timer<1.{state.fade=0.;}
   for link in &mut state.links{link.last=link.current;}
   state.timer=0.;state.force=*force;state.active=true;
  }
  true
 }
 pub fn advance(&mut self,dt:f32,weight:f32,root_up:V3)->Option<[V3;9]>{
  if !dt.is_finite()||dt<0.||!weight.is_finite()||!root_up.iter().all(|v|v.is_finite()){return None;}
  let mut out=[[0.;3];9];
  for (group,states) in self.definition.groups.iter().zip(&mut self.states){for (point,state) in group.points.iter().zip(states){
   if !state.active{continue;}
   state.timer=(state.timer+dt).clamp(0.,point.length);
   let speed=if point.fade_time>0.{1./point.fade_time}else{0.};
   state.fade=if speed>0.{(state.fade+dt*speed).clamp(0.,1.)}else{1.};
   // Empty AnimationCurve.Evaluate is zero; DBone parameter defaults differ.
   let eval=|keys:&[CurveKey]|if keys.is_empty(){0.}else{curve_value(keys,state.timer)};
   let offset=vscale(vadd(vscale(state.force,eval(&point.force)),vscale(vscale(root_up,vlen(state.force)),eval(&point.up))),weight);
   for ((id,w),link) in point.links.iter().zip(&mut state.links){
    let target=vscale(offset,*w);
    link.current=std::array::from_fn(|a|link.last[a]+(target[a]-link.last[a])*state.fade);
    out[*id]=vadd(out[*id],link.current);
   }
   // Apply the exact end sample once, then cease contributing next frame.
   state.active=state.timer<point.length;
  }}
  Some(out)
 }
 pub fn is_playing(&self)->bool{self.states.iter().flatten().any(|s|s.active)}
}

#[cfg(test)] mod tests {
 use super::*;
 #[test] fn authored_shared_scene_definition_is_usable(){
  let source=include_bytes!("../../../Content/IndependentResources/BodyInteractions/hit-reaction-v1.json");
  let value:UnityValue=serde_json::from_slice(source).unwrap();
  let definition=Definition::parse(&value).unwrap();assert_eq!(definition.groups.len(),12);
  for group in &definition.groups{
   assert_eq!(group.points.len(),3);
   let mut runtime=Runtime::new(definition.clone());
   assert!(runtime.hit(group.id,&[[1.,0.,0.];3]));
   let mut moved=false;
   for _ in 0..240{
    let offsets=runtime.advance(1./120.,0.3,[0.,1.,0.]).unwrap();
    assert!(offsets.iter().flatten().all(|x|x.is_finite()));
    moved|=offsets.iter().flatten().any(|x|x.abs()>1e-5);
   }
   assert!(moved,"authored group {} has no response",group.id);assert!(!runtime.is_playing());
  }
 }
 fn definition()->Arc<Definition>{
  let key=|t:f32,v:f32|serde_json::json!({"time":t,"value":v,"inSlope":0.,"outSlope":0.});
  let curve=serde_json::json!({"m_Curve":[key(0.,0.),key(0.5,1.),key(1.,0.)]});
  let v=serde_json::json!({"effectorHit":[{"id":3,"effectorHitPoints":[{"crossFadeTime":0.1,"offsetInForceDirection":curve,"offsetInUpDirection":{"m_Curve":[]},"effectorLinks":[{"effector":5,"weight":0.5},{"effector":5,"weight":0.25}]}]}]});
  Definition::parse(&serde_json::from_value(v).unwrap()).unwrap()
 }
 #[test] fn offsets_match_original_curve_link_sum_and_finish(){
  let mut r=Runtime::new(definition());assert!(!r.is_playing());assert!(r.hit(3,&[[2.,0.,0.]]));
  let result=r.advance(0.5,0.4,[0.,1.,0.]).unwrap();
  assert_eq!(result[5],[0.6,0.,0.]);for i in [0,1,2,3,4,6,7,8]{assert_eq!(result[i],[0.;3]);}
  assert!(r.is_playing());assert_eq!(r.advance(0.5,1.,[0.,1.,0.]).unwrap(),[[0.;3];9]);assert!(!r.is_playing());
  assert_eq!(r.advance(0.1,1.,[0.,1.,0.]).unwrap(),[[0.;3];9]);
 }
 #[test] fn retrigger_crossfades_previous_links_and_invalid_input_is_atomic(){
  let mut r=Runtime::new(definition());r.hit(3,&[[1.,0.,0.]]);r.advance(0.5,1.,[0.,1.,0.]);
  assert!(!r.hit(3,&[]));assert!(!r.hit(3,&[[f32::NAN,0.,0.]]));assert!(!r.hit(99,&[[1.,0.,0.]]));
  r.hit(3,&[[0.,0.,1.]]);let out=r.advance(0.,1.,[0.,1.,0.]).unwrap();assert_eq!(out[5],[0.75,0.,0.]);
  let before=format!("{:?}",r.states);assert!(r.advance(f32::NAN,1.,[0.,1.,0.]).is_none());assert_eq!(before,format!("{:?}",r.states));
 }
 #[test] fn frame_rate_independent_clock_and_instance_isolation(){
  let d=definition();let mut a=Runtime::new(d.clone());let mut b=Runtime::new(d.clone());let mut idle=Runtime::new(d);
  a.hit(3,&[[1.,2.,3.]]);b.hit(3,&[[1.,2.,3.]]);let mut x=[[0.;3];9];let mut y=x;
  for _ in 0..30{x=a.advance(1./60.,1.,[0.,1.,0.]).unwrap();}for _ in 0..60{y=b.advance(1./120.,1.,[0.,1.,0.]).unwrap();}
  for i in 0..9{for j in 0..3{assert!((x[i][j]-y[i][j]).abs()<2e-5);}}
  assert_eq!(idle.advance(0.5,1.,[0.,1.,0.]).unwrap(),[[0.;3];9]);
 }
 #[test] fn malformed_definition_is_rejected(){
  for v in [serde_json::json!({}),serde_json::json!({"effectorHit":[]}),serde_json::json!({"effectorHit":[{"id":1,"effectorHitPoints":[]}]})]{assert!(Definition::parse(&serde_json::from_value(v).unwrap()).is_err());}
 }
 #[test] fn release_effector_progress_and_clear_follow_original_timer(){
  let mut r=Runtime::new(definition());assert_eq!(r.release_progress(),1.);
  r.hit(3,&[[1.,0.,0.]]);assert_eq!(r.release_progress(),0.);
  r.advance(0.25,1.,[0.,1.,0.]);assert_eq!(r.release_progress(),0.25);
  r.advance(0.75,1.,[0.,1.,0.]);assert_eq!(r.release_progress(),1.);
  r.hit(3,&[[1.,0.,0.]]);r.clear();assert!(!r.is_playing());assert_eq!(r.release_progress(),1.);
  assert_eq!(r.advance(0.,1.,[0.,1.,0.]).unwrap(),[[0.;3];9]);
 }
 #[test] fn actual_apng_reaction_moves_core_bones_and_clears_without_source_game(){
  use std::path::Path;
  use std::ffi::CString;
  let root=Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content");
  let card=root.join("CharacterCards/CrowdAPNG/C001.png");
  let mut session=crate::oklib::open_test_card(&card,0).unwrap();
  assert!(session.game_root.as_os_str().is_empty(),"APNG must not require installed game");
  let shared=CString::new(root.join("IndependentResources/PairedAnimations").to_str().unwrap()).unwrap();
  let definition=include_bytes!("../../../Content/IndependentResources/BodyInteractions/hit-reaction-v1.json");
  let mut error=[0i8;2048];
  let s=&mut session as *mut _;
  unsafe {assert_eq!(crate::ffi::ok_body_reaction_prepare(s,shared.as_ptr(),definition.as_ptr(),definition.len() as i32,error.as_mut_ptr(),error.len() as i32),0,"{}",std::ffi::CStr::from_ptr(error.as_ptr()).to_string_lossy());}
  let bones=["cf_j_root","cf_j_spine01","cf_j_spine03","cf_j_arm00_L","cf_j_forearm01_L","cf_j_hand_L"];
  session.refresh_static_interaction(0.);
  let read=|s:&crate::session::OkSession|bones.iter().map(|b|s.body_skel.as_ref().unwrap().world_tf(b).expect(b)).collect::<Vec<_>>();
  let baseline=read(&session);let shape=session.shape_body.clone();
  unsafe {assert_eq!(crate::ffi::ok_body_reaction_hit_with_release(s,0,[[0.,1.,0.];3].as_ptr().cast(),3,0.3,0),0);}
  let mut maximum=0f32;
  for _ in 0..120{
   session.refresh_static_interaction(1./120.);
   for (a,b) in read(&session).iter().zip(&baseline){for i in 0..3{maximum=maximum.max((a[i][3]-b[i][3]).abs());}}
  }
  assert!(maximum>1e-4,"real core bones must visibly react, delta={maximum}");
  unsafe {assert_eq!(crate::ffi::ok_body_reaction_clear(s),0);}
  session.refresh_static_interaction(0.);
  let reset=read(&session);
  for (a,b) in reset.iter().zip(&baseline){for i in 0..4{for j in 0..4{assert!((a[i][j]-b[i][j]).abs()<1e-5,"core pose failed to restore");}}}
  assert_eq!(session.shape_body,shape,"touch cannot modify permanent body parameters");
  assert!(!session.body_interaction.as_ref().unwrap().reaction.is_playing());
 }
}
