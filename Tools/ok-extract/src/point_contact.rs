//! Optional common action contact constraints, solved on a task-private rig.
//! Only rotations change; source translations, scales, skin weights and topology survive.
use crate::{pose::Skeleton,finalik::*};
use serde::Deserialize;
#[derive(Deserialize)]
pub(crate) struct Recipe {pub start:f32,pub peak:f32,pub end:f32,pub constraints:Vec<Contact>}
#[derive(Deserialize)]
pub(crate) struct Contact {pub effector:String,pub tip:[f32;3],pub target:[f32;3],#[serde(default)]pub target_bone:Option<String>,pub chain:Vec<Joint>}
#[derive(Deserialize)]
pub(crate) struct Joint {pub name:String,pub max_degrees:f32}
fn point(sk:&Skeleton,name:&str,offset:[f32;3])->[f32;3]{let id=*sk.tfs.iter().find(|(_,t)|t.name.eq_ignore_ascii_case(name)).unwrap().0;crate::pose::mat_vec(&crate::pose::world_of_id(id,&sk.tfs),offset)}
impl Recipe {
 pub fn parse(json:&[u8])->Option<Self>{
  if json.is_empty()||json.len()>16384{return None;}
  let r:Self=serde_json::from_slice(json).ok()?;
  if ![r.start,r.peak,r.end].iter().all(|v|v.is_finite())||r.start<0.||r.end>1.||r.start>=r.peak||r.peak>=r.end||r.constraints.is_empty()||r.constraints.len()>4{return None;}
  for c in &r.constraints {if c.effector.is_empty()||c.effector.len()>128||c.target_bone.as_ref().is_some_and(|b|b.is_empty()||b.len()>128)||!c.tip.iter().chain(&c.target).all(|v|v.is_finite()&&v.abs()<10.)||c.chain.is_empty()||c.chain.len()>12{return None;}for j in &c.chain{if j.name.is_empty()||j.name.len()>128||!j.max_degrees.is_finite()||j.max_degrees<=0.||j.max_degrees>150.{return None;}}}
  Some(r)
 }
 pub fn apply(&self,sk:&mut Skeleton,phase:f32)->bool {
  // Validate all named ancestors before touching the private pose, even at weight zero.
  for c in &self.constraints {
   let Some(e)=sk.tfs.values().find(|v|v.name.eq_ignore_ascii_case(&c.effector))else{return false;};
   let mut ancestors=Vec::new();let mut parent=e.parent;
   for _ in 0..128 {let Some(id)=parent else{break;};let Some(t)=sk.tfs.get(&id)else{break;};ancestors.push(t.name.to_ascii_lowercase());parent=t.parent.filter(|p|*p!=id&&*p!=0);}
   let mut seen=std::collections::HashSet::new();
   for j in &c.chain{let n=j.name.to_ascii_lowercase();if !ancestors.contains(&n)||!seen.insert(n){return false;}}
   if c.target_bone.as_ref().is_some_and(|b|!sk.tfs.values().any(|t|t.name.eq_ignore_ascii_case(b))){return false;}
  }
  if !phase.is_finite(){return false;}
  let u=if phase<self.peak{(phase-self.start)/(self.peak-self.start)}else{(self.end-phase)/(self.end-self.peak)}.clamp(0.,1.);
  let weight=u*u*(3.-2.*u);if weight==0.{return true;}
  for c in &self.constraints {
   let target=match &c.target_bone{Some(b)=>point(sk,b,c.target),None=>c.target};
   let originals:Vec<_>=c.chain.iter().map(|j|sk.tfs.iter().find(|(_,t)|t.name.eq_ignore_ascii_case(&j.name)).map(|(id,t)|(*id,t.rot)).unwrap()).collect();
   for _ in 0..96 {
    let tip=point(sk,&c.effector,c.tip);
    if vlen(vsub(tip,target))<0.0005{break;}
    for (j,(id,original)) in c.chain.iter().zip(&originals) {
     let tip=point(sk,&c.effector,c.tip);
     let mut ik=IkSkeleton::new_batched(sk);let pivot=ik.world_pos(&j.name).unwrap();
     let a=vsub(tip,pivot);let b=vsub(target,pivot);if vlen(a)<1e-7||vlen(b)<1e-7{continue;}
     let an=vnorm(a);let bn=vnorm(b);let axis=vcross(an,bn);let sine=vlen(axis);let cosine=vdot(an,bn).clamp(-1.,1.);
     let delta=if sine>1e-8{qangle_axis(sine.atan2(cosine).min(0.15).to_degrees(),axis)}else if cosine<0.{qslerp(IDENTITY,qfrom_to_rotation(a,b),0.15/std::f32::consts::PI)}else{IDENTITY};
     let rotation=ik.world_rot(&j.name).unwrap();ik.set_world_rot(&j.name,qmul(delta,rotation));
     let current=ik.sk.tfs[id].rot;let difference=qmul(qinv(*original),current);let angle=2.*difference[3].abs().min(1.).acos();let limit=j.max_degrees.to_radians();
     if angle>limit{ik.sk.tfs.get_mut(id).unwrap().rot=qslerp(*original,current,limit/angle);}
     // Live-chain reads avoid stale SMR caches while solving; publish once below.
    }
   }
   for (id,original) in originals {let t=sk.tfs.get_mut(&id).unwrap();t.rot=qslerp(original,t.rot,weight);}
   sk.rebuild_worlds();
  }
  true
 }
}
#[cfg(test)] mod tests {
 use super::*;
 fn rig()->Skeleton {
  let mut tfs=std::collections::HashMap::new();
  for (id,name,parent,pos) in [(1,"root",None,[0.,0.,0.]),(2,"arm",Some(1),[0.5,0.,0.]),(3,"hand",Some(2),[0.5,0.,0.])] {
   tfs.insert(id,crate::pose::Tf{sibling_index:None,name:name.into(),parent,pos,rot:IDENTITY,scale:[1.;3]});
  }
  let mut sk=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:0,tfs,bone_ids:vec![1,2,3],source_mesh:0};sk.rebuild_worlds();sk
 }
 #[test] fn contact_solves_reachable_target_and_preserves_local_geometry(){
  let recipe=Recipe::parse(br#"{"start":0.2,"peak":0.5,"end":0.8,"constraints":[{"effector":"hand","tip":[0.5,0,0],"target":[0.8,0.8,0],"chain":[{"name":"arm","max_degrees":120},{"name":"root","max_degrees":120}]}]}"#).unwrap();
  let mut sk=rig();let before=sk.clone();assert!(recipe.apply(&mut sk,0.1));assert_eq!(serde_json::to_vec(&before).unwrap(),serde_json::to_vec(&sk).unwrap());
  assert!(recipe.apply(&mut sk,0.5));let residual=vlen(vsub(point(&sk,"hand",[0.5,0.,0.]),[0.8,0.8,0.]));assert!(residual<0.002,"residual {residual}");
  for (id,t) in &sk.tfs{assert_eq!(t.pos,before.tfs[id].pos);assert_eq!(t.scale,before.tfs[id].scale);assert!(t.rot.iter().all(|v|v.is_finite()));}
  let mut invalid=recipe;invalid.constraints[0].chain[0].name="missing".into();let saved=serde_json::to_vec(&sk).unwrap();assert!(!invalid.apply(&mut sk,0.5));assert_eq!(saved,serde_json::to_vec(&sk).unwrap());
 }
 #[test] fn contact_recipe_rejects_nonfinite_and_unbounded_inputs(){
  for s in [r#"{"start":0.2,"peak":0.1,"end":0.9,"constraints":[]}"#,r#"{"start":0.2,"peak":0.5,"end":0.9,"constraints":[{"effector":"x","tip":[0,0,0],"target":[0,0,0],"chain":[{"name":"y","max_degrees":180}]}]}"#]{assert!(Recipe::parse(s.as_bytes()).is_none());}
 }
}
#[cfg(test)] mod card_diag {
 use crate::finalik::*;
 // StreetSocial places the melon at 0.8 m scaled by the striker's standing shoulder height / 1.253 m (clamped 0.8..1).
 #[test] #[ignore="reads Saved/OKCardTest OK cards and the base library"]
 fn ok_card_watermelon_reach(){
  std::env::set_var("OK_BASE_LIBRARY","C:/portable/OpenKoikatsu/Content/IndependentResources/base_v1");
  let runtime=std::path::Path::new("C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations");
  let clip=std::fs::read(runtime.join("watermelon_strike.okanim")).unwrap();let json=std::fs::read(runtime.join("watermelon.contact.json")).unwrap();
  for n in 1..=10{
   let p=std::path::PathBuf::from(format!("C:/portable/OpenKoikatsu/Saved/OKCardTest/OK{n:02}.png"));
   let mut s=crate::session::open(&crate::oklib::root_for_card(std::path::Path::new(""),Some(&p)),Some(&p),0).unwrap();
   let (root,key)=crate::anim::install_shared_clip(&clip).unwrap();s.game_root=root;s.anim_clip=key;s.anim_parameters.clear();s.anim_scene_mode=0;s.anim_time=0.;s.reset_anim_clock();s.pv_mask=15;
   s.sample_paired_frame(0.);let shoulder=super::point(s.body_skel.as_ref().unwrap(),"cf_j_shoulder_R",[0.;3])[1];
   let mut r=super::Recipe::parse(&json).unwrap();r.constraints[0].target[2]=(80.*(shoulder/1.253).clamp(0.8,1.)).round()/100.;
   s.sample_paired_frame(s.anim_duration()*r.peak);let sk=s.body_skel.as_mut().unwrap();assert!(r.apply(sk,r.peak));
   let tip=super::point(sk,"k_f_handR_00",r.constraints[0].tip);let error=vlen(vsub(tip,r.constraints[0].target));
   eprintln!("OK{n:02} shoulder={shoulder:.3} target={:?} error_m={error:.3}",r.constraints[0].target);assert!(error<0.07);
  }
 }
}
