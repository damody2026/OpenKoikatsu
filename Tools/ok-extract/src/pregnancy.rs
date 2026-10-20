//! Maker mesh sculpting, following the installed PregnancyPlus 7.8 controller.
//! Work is performed when settings change; ordinary animation still uses GPU skin.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use crate::pose::{Mat4, mat_mul_pub, mat_inv_pub};
use crate::session::OkSession;
use std::collections::BTreeMap;

#[derive(Debug,Clone,PartialEq,serde::Serialize,serde::Deserialize)]
#[serde(default)]
pub struct Settings {
    #[serde(rename="GameplayEnabled")] pub enabled:bool,
    #[serde(flatten)] pub values:BTreeMap<String,f32>,
}
impl Default for Settings {fn default()->Self{Self{enabled:true,values:Default::default()}}}
impl Settings {pub fn get(&self,key:&str)->f32{self.values.get(key).copied().unwrap_or(0.)}}
#[derive(Clone,Default)]
pub struct Runtime {pub settings:Settings,pub original:Vec<(Vec<[f32;3]>,Vec<[f32;3]>)>,pub private:Vec<Option<(Vec<[f32;3]>,Vec<[f32;3]>)>>}
fn add(a:[f32;3],b:[f32;3])->[f32;3]{std::array::from_fn(|i|a[i]+b[i])}
fn sub(a:[f32;3],b:[f32;3])->[f32;3]{std::array::from_fn(|i|a[i]-b[i])}
fn mul(a:[f32;3],b:f32)->[f32;3]{a.map(|x|x*b)}
fn mag(a:[f32;3])->f32{a.iter().map(|x|x*x).sum::<f32>().sqrt()}
fn unit(a:[f32;3])->[f32;3]{let n=mag(a);if n>1e-8{mul(a,1./n)}else{[0.;3]}}
fn lerp(a:f32,b:f32,t:f32)->f32{a+(b-a)*t.clamp(0.,1.)}
fn mix(a:[f32;3],b:[f32;3],t:f32)->[f32;3]{std::array::from_fn(|i|lerp(a[i],b[i],t))}
fn slerp(a:[f32;3],b:[f32;3],t:f32)->[f32;3]{
 let t=t.clamp(0.,1.);let ua=unit(a);let ub=unit(b);let dot=ua.iter().zip(ub).map(|(a,b)|a*b).sum::<f32>().clamp(-1.,1.);
 let angle=dot.acos();if angle.sin().abs()<1e-5{return mix(a,b,t);}
 mul(add(mul(ua,((1.-t)*angle).sin()/angle.sin()),mul(ub,(t*angle).sin()/angle.sin())),lerp(mag(a),mag(b),t))
}
fn curve(keys:&[(f32,f32)],x:f32)->f32{
 if x<=keys[0].0{return keys[0].1;}for w in keys.windows(2){if x<=w[1].0{let t=(x-w[0].0)/(w[1].0-w[0].0);return lerp(w[0].1,w[1].1,t*t*(3.-2.*t));}}keys.last().unwrap().1
}
fn point(m:&Mat4,p:[f32;3])->[f32;3]{std::array::from_fn(|i|m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]+m[i][3])}

pub fn sculpt(p:[f32;3],s:&Settings,cloth:f32)->[f32;3]{
 if !s.enabled||s.get("inflationSize")==0.{return p;}
 let r=0.13*(1.+s.get("inflationMultiplier"));if r<=1e-6{return p;}
 let base=[0.,0.97+0.155*0.97,0.];let center=add(base,[0.,s.get("inflationMoveY"),s.get("inflationMoveZ")]);
 let distance=mag(sub(p,center));if distance>=r+0.13/20.||distance<1e-7||p[2]< -0.045||p[1]<base[1]-0.13*1.5{return p;}
 let sphere=add(center,mul(unit(sub(p,center)),r+cloth));
 if distance>=mag(sub(sphere,center))||mag(sub(p,base))>mag(sub(sphere,base)){return p;}
 let mut v=sphere;let xy=mag([v[0]-center[0],v[1]-center[1],0.])/(0.26*5.);
 v[0]=lerp(v[0],p[0],xy);v[1]=lerp(v[1],p[1],xy);
 let shift=s.get("inflationShiftY");if shift!=0.{let amount=lerp(shift,shift/4.,((v[1]-center[1])/(distance*1.8)).abs());let shifted=add(v,[0.,amount,0.]);v=slerp(shifted,v,((v[0]-center[0]).abs()/(distance*3.)+0.1).abs());}
 v[2]+=lerp(0.,s.get("inflationShiftZ"),(v[2]-center[2])/(distance*2.));
 v[0]=center[0]+(v[0]-center[0])*(1.+s.get("inflationStretchX"));v[1]=center[1]+(v[1]-center[1])*(1.+s.get("inflationStretchY"));
 let edge=curve(&[(0.,0.),(0.25,0.001),(0.5,0.001),(0.75,0.2),(0.9,0.7),(1.,1.)],distance/r);
 let round=lerp(lerp(0.,s.get("inflationRoundness"),(v[2]-center[2]-0.045/2.)/r),0.,edge);
 v=add(v,mul(unit(sub(v,add(center,[0.,0.,r/3.]))),round));
 let y=v[1]-center[1];let x=v[0]-center[0];let taper=lerp(lerp(0.,s.get("inflationTaperY"),y.abs()/distance),0.,1.-x.abs()/distance)*x.signum()*y.signum();v[0]+=taper;
 let ty=lerp(0.,s.get("inflationTaperZ"),(v[1]-center[1]).abs()/distance)*(v[1]-center[1]).signum();
 let target=add(v,[0.,0.,ty]);v=mix(mix(v,target,(p[2]+0.045)/distance-distance/2.),target,(v[2]-center[2])/distance);
 let fat=s.get("inflationFatFold");if fat>0.{let height=s.get("inflationFatFoldHeight")*r;let d=(v[1]-(center[1]+height)).abs();
  if d<=r{v=slerp(p,v,d/r+fat-1.);let gap=s.get("inflationFatFoldGap");if gap!=0.{let y=v[1]-center[1]-height;let k=curve(&[(0.,0.),(0.1,0.15),(0.25,0.25),(0.5,0.7),(0.75,0.95),(1.,1.)],y.abs()/0.13);
   let side=lerp(0.,1.,((v[0]-center[0]).abs()-0.13/2.)/0.13);if k>1e-6{v[1]+=lerp(0.,lerp(lerp(y,0.,k),0.,side),(gap*0.66).abs()/k)*gap.signum();}}}
  if v[1]>center[1]+height{v=slerp(v,p,d/(r*1.5));}
 }
 v[1]-=r*lerp(0.,s.get("inflationDrop"),(v[2]-center[2])/(r*1.5));
 let sides=0.13/1.8+(r/2.5-0.13/1.8).max(0.)/3.;let z=p[2]+0.045;
 if z<=sides{v=mix(p,v,curve(&[(0.,0.),(0.25,0.15),(0.5,0.35),(0.7,0.7),(0.9,0.9),(1.,1.)],z/sides));}
 let top=base[1]+0.182;if p[1]>top{return p;}if p[1]>=top-0.091{v[2]=lerp(p[2],v[2],curve(&[(0.,0.),(0.25,0.1),(0.5,0.35),(0.75,0.9),(1.,1.)],(top-p[1])/0.091));}
 if mag([v[0],0.,v[2]])<mag([p[0],0.,p[2]]){v[0]=p[0];v[2]=p[2];}
 if distance>mag(sub(v,center))||mag(sub(p,base))>mag(sub(v,base)){return p;}
 if v[2]< -0.045||(p[2]>v[2]&&p[2]>center[2]){v[2]=p[2];}
 mix(p,v,s.get("inflationSize")/40.)
}

fn recalculate_normals(points:&[[f32;3]],indices:&[u32],fallback:&[[f32;3]])->Vec<[f32;3]>{
 let mut normals=vec![[0.;3];points.len()];for tri in indices.chunks_exact(3){let[a,b,c]=[tri[0] as usize,tri[1] as usize,tri[2] as usize];if a>=points.len()||b>=points.len()||c>=points.len(){continue;}
 let u=sub(points[b],points[a]);let v=sub(points[c],points[a]);let n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];for i in[a,b,c]{normals[i]=add(normals[i],n);}}
 for(i,n)in normals.iter_mut().enumerate(){*n=if mag(*n)>1e-8{unit(*n)}else{fallback.get(i).copied().unwrap_or([0.,1.,0.])};}normals
}

/// `apply` would change no vertex: the same settings, or nothing applied yet and nothing to apply.
pub fn is_noop(session:&OkSession,settings:&Settings)->bool{
 let old=&session.editor_pregnancy.settings;
 (old.enabled==settings.enabled&&old.values.keys().chain(settings.values.keys()).all(|k|old.get(k)==settings.get(k)))
  ||(session.editor_pregnancy.original.is_empty()&&(!settings.enabled||settings.get("inflationSize")==0.))
}
pub fn apply(session:&mut OkSession,settings:&Settings)->bool{
 let old=&session.editor_pregnancy.settings;
 if old.enabled==settings.enabled&&old.values.keys().chain(settings.values.keys()).all(|k|old.get(k)==settings.get(k)){return false;}
 if session.editor_pregnancy.original.is_empty()&&(!settings.enabled||settings.get("inflationSize")==0.){session.editor_pregnancy.settings=settings.clone();return false;}
 if session.editor_pregnancy.original.is_empty(){session.editor_pregnancy.original=session.parts.iter().map(|p|(p.rest_positions.clone(),p.rest_normals.clone())).collect();}
 let Some(rest)=crate::skel_export::bind_body(session) else{return false;};
 let eligible:Vec<bool>=rest.names.iter().map(|n|["cf_s_spine02","cf_s_waist01","cf_s_waist02"].contains(&n.as_str())).collect();
 let excluded:Vec<bool>=rest.names.iter().map(|n|["cf_s_bust03_L","cf_s_bust03_R"].contains(&n.as_str())).collect();
 for (i,part) in session.parts.iter_mut().enumerate(){
  if part.role.starts_with("clothes")&&part.runtime_accessory.is_some(){
   let runtime=part.runtime_accessory.as_mut().unwrap();
   if let Some((mesh,matrices))=runtime.editor_bind_source(&rest){
    if session.editor_pregnancy.private.len()<=i {session.editor_pregnancy.private.resize(i+1,None);}
    let original=session.editor_pregnancy.private[i].get_or_insert_with(||(mesh.rest_positions.clone(),mesh.rest_normals.clone()));
    let mut changed=mesh.clone();changed.rest_positions.clone_from(&original.0);changed.rest_normals.clone_from(&original.1);
    if let Some(private)=&changed.smr_skel{
     for j in 0..changed.rest_positions.len(){
      let Some(indices)=changed.bone_idx.get(j) else{continue;};let Some(weights)=changed.weights.get(j) else{continue;};
      let allowed=indices.iter().zip(weights).any(|(b,w)|*w>0.&&private.names.get(*b as usize).is_some_and(|n|["cf_s_spine02","cf_s_waist01","cf_s_waist02"].contains(&n.as_str())));
      let excluded=indices.iter().zip(weights).any(|(b,w)|*w>0.&&private.names.get(*b as usize).is_some_and(|n|["cf_s_bust03_L","cf_s_bust03_R"].contains(&n.as_str())));
      if !allowed||excluded{continue;}
      let mut skin=[[0.;4];4];for(b,w)in indices.iter().zip(weights){if let Some(m)=matrices.get(*b as usize){for a in 0..4{for k in 0..4{skin[a][k]+=m[a][k]*w;}}}}
      let p=point(&skin,original.0[j]);let v=sculpt(p,settings,0.13/120.+0.13/100.*settings.get("inflationClothOffset"));
      if v!=p{changed.rest_positions[j]=point(&mat_inv_pub(&skin),v);}
     }
    }
    if changed.rest_positions!=original.0{changed.rest_normals=recalculate_normals(&changed.rest_positions,&changed.indices,&original.1);}
    changed.positions.clone_from(&changed.rest_positions);changed.normals.clone_from(&changed.rest_normals);
    // Both the native private GPU stream and inspection geometry use this source.
    if !settings.enabled||settings.get("inflationSize")==0.{part.rest_positions.clone_from(&session.editor_pregnancy.original[i].0);part.rest_normals.clone_from(&session.editor_pregnancy.original[i].1);}else{part.rest_positions.clone_from(&changed.rest_positions);part.rest_normals.clone_from(&changed.rest_normals);}
    runtime.replace_source(changed);
   }
   continue;
  }
  if part.skel_kind!=1||part.runtime_accessory.is_some(){continue;}
  let (source,normals)=&session.editor_pregnancy.original[i];part.rest_positions.clone_from(source);part.rest_normals.clone_from(normals);
  let (worlds,binds)=crate::skel_export::palettes(&rest,part);
  for j in 0..source.len(){let Some(indices)=part.bone_idx.get(j) else{continue;};let Some(weights)=part.weights.get(j) else{continue;};
   if !indices.iter().zip(weights).any(|(b,w)|*w>0.&&eligible.get(*b as usize)==Some(&true))||indices.iter().zip(weights).any(|(b,w)|*w>0.&&excluded.get(*b as usize)==Some(&true)){continue;}
   let mut skin=[[0.;4];4];for (b,w)in indices.iter().zip(weights){let b=*b as usize;if *w==0.||b>=worlds.len(){continue;}let m=mat_mul_pub(&worlds[b],&binds[b]);for a in 0..4{for k in 0..4{skin[a][k]+=m[a][k]*w;}}}
   let p=point(&skin,source[j]);let cloth=if part.role=="body"{0.}else{0.13/120.+0.13/100.*settings.get("inflationClothOffset")};
   let changed=sculpt(p,settings,cloth);if changed!=p{part.rest_positions[j]=point(&mat_inv_pub(&skin),changed);}
  }
  if part.rest_positions!=*source{
   let mut normal=vec![[0.;3];source.len()];for triangle in part.indices.chunks_exact(3){let [a,b,c]=[triangle[0] as usize,triangle[1] as usize,triangle[2] as usize];if a>=normal.len()||b>=normal.len()||c>=normal.len(){continue;}
    let u=sub(part.rest_positions[b],part.rest_positions[a]);let v=sub(part.rest_positions[c],part.rest_positions[a]);let cross=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];for k in[a,b,c]{normal[k]=add(normal[k],cross);}}
   for j in 0..normal.len(){if mag(normal[j])>1e-8{part.rest_normals[j]=unit(normal[j]);}}
  }
 }
 session.editor_pregnancy.settings=settings.clone();session.skel_export=Some(crate::skel_export::build(session));true
}

#[cfg(test)] mod tests {
 use super::*;
 #[test] fn every_control_changes_real_mesh_geometry_without_accumulation(){
  let path=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C000.png");
  let mut session=crate::oklib::open_test_card(&path,0).unwrap();
  let mut base=Settings::default();base.values.insert("inflationSize".into(),30.);base.values.insert("inflationMultiplier".into(),0.25);base.values.insert("inflationFatFold".into(),0.5);
  assert!(apply(&mut session,&base));
  let original=session.parts.iter().map(|p|p.rest_positions.clone()).collect::<Vec<_>>();
  for (key,value) in [("inflationSize",40.),("inflationMultiplier",0.6),("inflationRoundness",0.05),("inflationMoveY",0.05),("inflationMoveZ",0.03),("inflationStretchX",0.2),("inflationStretchY",0.15),("inflationShiftY",0.1),("inflationShiftZ",0.06),("inflationTaperY",0.06),("inflationTaperZ",0.06),("inflationDrop",0.7),("inflationClothOffset",8.),("inflationFatFold",1.5),("inflationFatFoldHeight",0.4),("inflationFatFoldGap",0.8)]{
   let mut changed=base.clone();changed.values.insert(key.into(),value);assert!(apply(&mut session,&changed));
   assert!(session.parts.iter().zip(&original).any(|(p,o)|p.rest_positions!=*o),"control {key} has no geometry effect");
   assert!(session.parts.iter().all(|p|p.rest_positions.iter().flatten().chain(p.rest_normals.iter().flatten()).all(|v|v.is_finite())),"control {key} creates invalid geometry");
   assert!(apply(&mut session,&base));assert_eq!(original,session.parts.iter().map(|p|p.rest_positions.clone()).collect::<Vec<_>>(),"control {key} accumulates deformation");
  }
 }
 #[test] fn portable_belly_updates_gpu_source_and_restores_exactly(){
  let path=&std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C000.png");
  assert!(path.ok_is_file(),"native fixture missing");
  let mut session=crate::oklib::open_test_card(path,0).unwrap();
  let original:Vec<_>=session.parts.iter().map(|p|(p.rest_positions.clone(),p.rest_normals.clone())).collect();
  let mut settings=Settings::default();settings.values.insert("inflationSize".into(),40.);settings.values.insert("inflationMultiplier".into(),0.25);
  assert!(apply(&mut session,&settings));
  let rest=crate::skel_export::bind_body(&session).unwrap();
  let changes:Vec<_>=session.parts.iter().enumerate().filter_map(|(i,p)|{
   let count=if let Some(runtime)=&p.runtime_accessory{let(mesh,_)=runtime.editor_bind_source(&rest)?;let o=session.editor_pregnancy.private.get(i)?.as_ref()?;mesh.rest_positions.iter().zip(&o.0).filter(|(a,b)|a!=b).count()}
      else{p.rest_positions.iter().zip(&original[i].0).filter(|(a,b)|a!=b).count()};
   (count>0).then(||serde_json::json!({"role":p.role,"name":p.name,"vertices":count}))
  }).collect();
  assert!(changes.iter().any(|v|v["role"]=="body"),"body belly must deform: {changes:?}");
  assert!(changes.iter().any(|v|v["role"].as_str().is_some_and(|r|r.starts_with("clothes"))),"wearing clothes must follow belly: {changes:?}");
  assert!(session.parts.iter().all(|p|p.rest_positions.iter().flatten().chain(p.rest_normals.iter().flatten()).all(|v|v.is_finite())));
  let delta=session.parts.iter().map(|p|p.rest_positions.clone()).collect::<Vec<_>>();
  session.apply_anim_frame(0.2,0.);assert_eq!(delta,session.parts.iter().map(|p|p.rest_positions.clone()).collect::<Vec<_>>());
  settings.enabled=false;assert!(apply(&mut session,&settings));
  for(p,o)in session.parts.iter().zip(&original){assert_eq!(p.rest_positions,o.0);assert_eq!(p.rest_normals,o.1);}
  settings.enabled=true;assert!(apply(&mut session,&settings));
  assert_eq!(delta,session.parts.iter().map(|p|p.rest_positions.clone()).collect::<Vec<_>>());
  apply(&mut session,&Settings::default());
  for(p,o)in session.parts.iter().zip(&original){assert_eq!(p.rest_positions,o.0);assert_eq!(p.rest_normals,o.1);}
  std::fs::create_dir_all("C:/portable/OpenKoikatsu/Saved/Tests/EditorMods").unwrap();
  std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/EditorMods/belly.json",serde_json::to_vec_pretty(&serde_json::json!({"valid":true,"changes":changes,"exact_restore":true,"animation_preserves_source":true})).unwrap()).unwrap();
 }
}
