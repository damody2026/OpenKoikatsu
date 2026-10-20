//! Private authored hair rig; uses the existing serialized native physics path.
use super::*;
use crate::{pose::{Tf,ident,mat_inv_pub},session::{SessionPart,AccessoryRuntime},unity::ExtractedMesh};
use std::collections::HashMap;

#[derive(Serialize,Deserialize)]
pub(super) struct Chain {
    pub name:String,pub points:Vec<[f32;3]>,
    pub damping:f32,pub elasticity:f32,pub stiffness:f32,pub radius:f32,
}
#[derive(Serialize,Deserialize)]
pub(super) struct SoftRig {
    pub chains:Vec<Chain>,pub weights:Vec<[f32;4]>,pub bone_idx:Vec<[u16;4]>,
}
impl SoftRig {
    pub(super) fn validate(&self,n:usize)->Result<(),String>{
        let count=1+self.chains.iter().map(|c|c.points.len()).sum::<usize>();
        if self.chains.is_empty()||self.chains.len()>32||count>256||self.weights.len()!=n||self.bone_idx.len()!=n{return Err("invalid soft hair rig size".into());}
        for c in &self.chains {
            if c.points.len()<3||c.points.len()>12||c.points.iter().flatten().any(|v|!v.is_finite())||
                [c.damping,c.elasticity,c.stiffness,c.radius].iter().any(|v|!v.is_finite()||*v<0.||*v>1.) ||
                c.points.windows(2).any(|p|p[0].iter().zip(p[1]).map(|(a,b)|(a-b).powi(2)).sum::<f32>()<1e-8){return Err("invalid soft hair chain".into());}
        }
        for(w,b)in self.weights.iter().zip(&self.bone_idx){
            if w.iter().any(|v|!v.is_finite()||*v<0.)||(w.iter().sum::<f32>()-1.).abs()>1e-4||w.iter().zip(b).any(|(w,b)|*w>0.&&*b as usize>=count){return Err("invalid soft hair skin weights".into());}
        }Ok(())
    }
}
pub(super) fn install(p:&mut SessionPart,rig:&SoftRig,head:&Mat4)->Result<(),String>{
    let mut tfs=HashMap::new();let mut ids=vec![1i64];let mut names:Vec<String>=vec!["TripoHairRoot".into()];let mut roots=Vec::new();
    tfs.insert(1,Tf{name:names[0].clone(),parent:None,sibling_index:Some(0),pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]});
    for(chain_index,c)in rig.chains.iter().enumerate(){
        let root=ids.len() as i64+1;roots.push((root,c.damping,c.elasticity,c.stiffness,c.radius));
        let mut parent=1;let mut previous=[0.;3];
        for(j,&point)in c.points.iter().enumerate(){
            let id=ids.len() as i64+1;let name=format!("TripoHair_{}_{j}",c.name);
            let pos=std::array::from_fn(|i|point[i]-previous[i]);
            tfs.insert(id,Tf{name:name.clone(),parent:Some(parent),sibling_index:Some(if j==0{chain_index as u32}else{0}),pos,rot:[0.,0.,0.,1.],scale:[1.;3]});
            ids.push(id);names.push(name);previous=point;parent=id;
        }
    }
    let mut sk=Skeleton{names,worlds:vec![ident();ids.len()],rest:Vec::new(),smr_count:ids.len(),tfs,bone_ids:ids,source_mesh:0};
    sk.rebuild_worlds();sk.rest=sk.worlds.clone();
    let binds=sk.rest.iter().map(mat_inv_pub).collect::<Vec<_>>();
    let source=ExtractedMesh{name:p.name.clone(),renderer_skinned:Some(true),positions:p.rest_positions.clone(),rest_positions:p.rest_positions.clone(),
        normals:p.rest_normals.clone(),rest_normals:p.rest_normals.clone(),tangents:p.rest_tangents.clone(),
        uvs:p.uvs.clone(),uv2:p.uv2.clone(),indices:p.indices.clone(),weights:rig.weights.clone(),bone_idx:rig.bone_idx.clone(),
        bind_poses:binds.clone(),bone_names:sk.names.clone(),smr_skel:Some(sk),deferred_dbones:Some(crate::dbone::DBoneSet::authored_hair(&roots)),..Default::default()};
    p.runtime_accessory=Some(AccessoryRuntime::from_head_local_hair(&source,head).ok_or("cannot create authored hair dynamics")?);
    p.weights=rig.weights.clone();p.bone_idx=rig.bone_idx.clone();p.bind_poses=binds;p.skel_kind=4;
    p.parent_bone=None;p.parent_c=CString::default();
    Ok(())
}

pub(super) fn verify_motion(s:&OkSession,name:&str)->Result<serde_json::Value,String>{
    let p=s.parts.iter().find(|p|p.name==name).ok_or("missing soft hair")?;
    let mut runtime=p.runtime_accessory.as_ref().ok_or("missing hair dynamics")?.clone();
    let base=s.body_skel.as_ref().ok_or("missing body rig")?;let mut body=base.clone();let h=head(base)?;
    let roots:Vec<_>=base.tfs.iter().filter(|(_,tf)|tf.parent.is_none()).map(|(id,tf)|(*id,tf.pos)).collect();
    if roots.is_empty(){return Err("body rig has no motion root".into());}
    let fixed:Vec<_>=p.weights.iter().zip(&p.bone_idx).enumerate().filter(|(_, (w,b))|b[0]==0&&w[0]>0.9999).map(|(i,_)|i).collect();
    if fixed.is_empty(){return Err("soft hair has no fixed scalp vertices".into());}
    let mut max_fixed=0f32;let mut max_soft=0f32;let mut min_y=f32::INFINITY;
    for frame in 0..180 {
        let x=if frame<120{0.025*(frame as f32*std::f32::consts::TAU/60.).sin()}else{0.};
        for(id,pos)in &roots{let tf=body.tfs.get_mut(id).unwrap();tf.pos=*pos;tf.pos[0]+=x;}
        body.rebuild_worlds();
        let(vertices,_)=runtime.update(Some(&body),s.head_skel.as_ref(),&Default::default(),Default::default(),1./60.).ok_or("soft hair solver failed")?;
        if vertices.iter().flatten().any(|x|!x.is_finite()){return Err("soft hair solver produced non-finite vertices".into());}
        for &i in &fixed {
            let rigid=crate::pose::mat_vec(&body.worlds[h],p.rest_positions[i]);
            max_fixed=max_fixed.max((0..3).map(|j|(vertices[i][j]-rigid[j]).abs()).fold(0.,f32::max));
        }
        for(i,v)in vertices.iter().enumerate().step_by(7){
            let rigid=crate::pose::mat_vec(&body.worlds[h],p.rest_positions[i]);
            max_soft=max_soft.max((0..3).map(|j|(v[j]-rigid[j]).powi(2)).sum::<f32>().sqrt());min_y=min_y.min(v[1]);
        }
    }
    if max_fixed>1e-4||max_soft<0.001||max_soft>0.5{return Err(format!("soft hair motion invalid: fixed={max_fixed}, soft={max_soft}"));}
    Ok(serde_json::json!({"frames":180,"fixed_scalp_vertices":fixed.len(),"fixed_scalp_max_error":max_fixed,"soft_max_displacement_from_rigid":max_soft,"min_vertex_y":min_y,"head_socket_translation_amplitude":0.025,"finite":true}))
}
