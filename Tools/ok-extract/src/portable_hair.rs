//! Import an authored head-local hair mesh into a new, multi-outfit card.
//! This first integration uses one head bone; it does not invent hair physics.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use super::*;
use crate::pose::{Mat4, Skeleton};
use serde::{Serialize,Deserialize};
use std::{collections::BTreeMap,ffi::CString};
#[path="portable_hair_soft.rs"]
mod soft;

#[derive(Serialize,Deserialize)]
struct HairMesh {
    positions:Vec<[f32;3]>, normals:Vec<[f32;3]>, tangents:Vec<[f32;4]>,
    uvs:Vec<[f32;2]>, indices:Vec<u32>, texture:String,
    #[serde(default)] normal:Option<String>,
    #[serde(default)] roughness:Option<String>,
    #[serde(default)] metallic:Option<String>,
    #[serde(default)] native_material_card:Option<String>,
    #[serde(default)] native_gloss_asset:Option<String>,
    #[serde(default)] gloss_uvs:Vec<[f32;2]>,
    #[serde(default)] soft_rig:Option<soft::SoftRig>,
    #[serde(default)] hide_source_accessory:bool,
    /// Named hair layer (e.g. back, front, side, bangs). Layers are added as
    /// separate parts cloned from the native hair donor, so several authored
    /// layers can coexist; re-importing the same layer replaces it.
    #[serde(default)] layer:Option<String>,
}
fn part_name(m:&HairMesh)->String {
    match &m.layer {Some(l)=>format!("Tripo_Hair_{l}_{}",m.indices.len()/3),None=>format!("Tripo_Golden_Hair_{}",m.indices.len()/3)}
}
fn is_custom(p:&crate::session::SessionPart)->bool {p.name.starts_with("Tripo_")}
fn head(s:&Skeleton)->Result<usize,String> {
    s.names.iter().position(|n|n.eq_ignore_ascii_case("cf_s_head"))
        .ok_or_else(||"body skeleton has no cf_s_head".into())
}
fn validate(m:&HairMesh)->Result<(),String> {
    let n=m.positions.len();
    if n==0 || n>1_000_000 || m.normals.len()!=n || m.tangents.len()!=n || m.uvs.len()!=n {
        return Err("hair vertex channels have inconsistent lengths".into());
    }
    if m.indices.is_empty() || m.indices.len()%3!=0 || m.indices.len()/3>50000 || m.indices.iter().any(|i|*i as usize>=n) {
        return Err("invalid hair indices or exceeds 50000 triangles".into());
    }
    if m.positions.iter().flatten().chain(m.normals.iter().flatten()).chain(m.tangents.iter().flatten()).chain(m.uvs.iter().flatten()).any(|v|!v.is_finite()) {
        return Err("non-finite hair vertex channel".into());
    }
    if !m.gloss_uvs.is_empty() && (m.gloss_uvs.len()!=n || m.gloss_uvs.iter().flatten().any(|v|!v.is_finite())){return Err("invalid gloss UV channel".into());}
    if let Some(rig)=&m.soft_rig {rig.validate(n)?;}
    if let Some(l)=&m.layer {if l.is_empty()||l.len()>32||!l.chars().all(|c|c.is_ascii_alphanumeric()||c=='_'){return Err("invalid hair layer name".into());}}
    Ok(())
}
pub fn inspect_hair_base(input:&Path,output:&Path)->Result<serde_json::Value,String> {
    if output.ok_exists(){return Err("inspection output already exists".into());}
    let s=load(input)?;let sk=s.body_skel.as_ref().ok_or("missing body skeleton")?;let h=head(sk)?;
    let parts:Vec<_>=s.parts.iter().map(|p|serde_json::json!({
        "role":p.role,"name":p.name,"visible":p.render_visible,"source_property":p.source_property,"parent_bone":p.parent_bone,"skel_kind":p.skel_kind,"positions":p.positions,"normals":p.normals,"uvs":p.uvs,"indices":p.indices
    })).collect();
    let report=serde_json::json!({"source":input,"outfits":s.coordinate_count,"head_bone":sk.names[h],"head_world":sk.worlds[h],"parts":parts});
    let mut f=fs::OpenOptions::new().create_new(true).write(true).open(output).map_err(|e|e.to_string())?;
    f.write_all(&serde_json::to_vec(&report).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
    Ok(serde_json::json!({"inspection":output,"outfits":s.coordinate_count,"parts":s.parts.len(),"head_bone":sk.names[h]}))
}
fn install(s:&mut OkSession,m:&HairMesh,texture:&[u8],pbr:Option<&[Vec<u8>;3]>)->Result<(),String> {
    let sk=s.body_skel.as_ref().ok_or("missing body skeleton")?;let h=head(sk)?;
    let head_world=sk.worlds[h];
    let bone_count=sk.names.len();if h>u16::MAX as usize{return Err("head bone index overflow".into());}
    let native_hair=|p:&crate::session::SessionPart|(p.role.starts_with("hair") || (p.role=="accessory" && p.name=="Hair")) && !is_custom(p);
    let donor=s.parts.iter().position(|p|native_hair(p)).ok_or("base has no hair material donor")?;
    if m.hide_source_accessory {
        if let Some(source)=s.parts[donor].source_property.clone() {
            for part in &mut s.parts {
                if part.role=="accessory" && part.name!="Hair" && part.source_property.as_ref()==Some(&source) {
                    part.render_visible=false;
                }
            }
        }
    }
    let slot=if let Some(layer)=&m.layer {
        // Keep other authored layers; replace an earlier import of this layer.
        let prefix=format!("Tripo_Hair_{layer}_");
        let existing=s.parts.iter().position(|p|p.name.starts_with(&prefix));
        let index=match existing {Some(i)=>i,None=>{let clone=s.parts[donor].clone();s.parts.push(clone);s.parts.len()-1}};
        if existing.is_some(){s.parts[index]=s.parts[donor].clone();}
        index
    } else {donor};
    for p in s.parts.iter_mut().filter(|p|native_hair(p)){p.render_visible=false;}
    if m.layer.is_none() {for p in s.parts.iter_mut().filter(|p|is_custom(p)){p.render_visible=false;}}
    let p=&mut s.parts[slot];
    p.role=match m.layer.as_deref(){Some(l)=>format!("hair_{l}"),None=>"hair_back".into()};p.role_c=CString::new(p.role.clone()).unwrap();
    p.name=part_name(m);p.name_c=CString::new(p.name.clone()).unwrap();p.render_visible=true;
    p.source_property=None;p.runtime_accessory=None;p.item_draw=None;
    let mut material=crate::unity::MeshMaterial::default();
    material.name="Tripo_Golden_Hair".into();material.shader="Shader Forge/main_item".into();material.render_queue=2000;
    material.colors.insert("_Color".into(),[1.;4]);material.colors.insert("_Color2".into(),[1.;4]);material.colors.insert("_Color3".into(),[1.;4]);
    material.colors.insert("_ShadowColor".into(),[0.75,0.68,0.55,1.]);
    for(k,v)in [("_MainTex_ST_x",1.),("_MainTex_ST_y",1.),("_MainTex_ST_z",0.),("_MainTex_ST_w",0.),("_Glossiness",0.35)] {material.floats.insert(k.into(),v);}
    p.source_material=Some(material);p.blend="opaque".into();p.blend_c=CString::new("opaque").unwrap();
    p.shader_textures.clear();p.runtime_textures.clear();p.shader_textures.insert("_MainTex".into(),texture.to_vec());
    // This native Forge pass runs the original gamma-space shader program.
    // Feed encoded Tripo albedo values without an extra sRGB decode.
    p.runtime_textures.insert("_MainTex".into(),[1,0,0,0,1]);p.shader_kind=8;p.unlit=true;
    p.rest_positions=m.positions.clone();p.positions=m.positions.clone();p.rest_normals=m.normals.clone();p.normals=m.normals.clone();p.rest_tangents=m.tangents.clone();
    p.uvs=m.uvs.clone();p.uv2.clear();p.uv3.clear();p.uv4.clear();p.vertex_colors=vec![[255;4];m.positions.len()];p.indices=m.indices.clone();
    p.weights=vec![[1.,0.,0.,0.];m.positions.len()];p.bone_idx=vec![[h as u16,0,0,0];m.positions.len()];
    let identity:Mat4=[[1.,0.,0.,0.],[0.,1.,0.,0.],[0.,0.,1.,0.],[0.,0.,0.,1.]];
    p.bind_poses=vec![identity;bone_count];p.skel_kind=1;p.parent_bone=None;p.rigid_follow_bone=None;p.parent_c=CString::default();p.rigid_follow_c=CString::default();
    p.png=Some(texture.to_vec());p.alpha_mask=None;p.hair_gloss=None;p.detail_mask=None;p.line_mask=None;p.normal_map=None;p.normal_detail=None;p.normal_mask=None;
    p.overtex_png=None;p.overtex2_png=None;p.overcolor=[1.;4];p.overcolor2=[1.;4];p.specular_color=[0.2,0.2,0.2,1.];p.metallic=0.;p.smoothness=0.35;
    p.main_tex_st=[1.,1.,0.,0.];p.source_outline_enabled=true;p.blend_shapes.clear();p.morph_idx.clear();p.morph_dxyz.clear();
    if let Some(maps)=pbr {
        p.source_material.as_mut().unwrap().shader="OpenKoikatsu/TripoPBR".into();
        p.shader_kind=0;p.unlit=false;p.source_outline_enabled=false;
        p.normal_map=Some(maps[0].clone());p.normal_map_st=[1.,1.,0.,0.];
        p.shader_textures.insert("_RoughnessMap".into(),maps[1].clone());p.shader_textures.insert("_MetallicMap".into(),maps[2].clone());
        p.runtime_textures.insert("_MainTex".into(),[1,0,0,1,1]);
        for slot in ["_NormalMap","_RoughnessMap","_MetallicMap"]{p.runtime_textures.insert(slot.into(),[1,0,0,0,1]);}
    }
    if let Some(path)=&m.native_material_card {
        if pbr.is_some(){return Err("native hair and PBR modes are mutually exclusive".into());}
        let donor=load(Path::new(path))?;
        let d=donor.parts.iter().find(|p|p.render_visible && p.shader_kind==1)
            .or_else(||donor.parts.iter().find(|p|p.render_visible && p.shader_kind==10))
            .ok_or("donor has no native hair shader")?;
        // Keep all serialized native hair ramps, gloss and material constants.
        // Replace only the mesh, base texture and attachment with the imported hair.
        let authored=p.clone(); *p=d.clone();
        p.role=authored.role;p.role_c=authored.role_c;p.name=authored.name;p.name_c=authored.name_c;
        p.source_property=None;p.runtime_accessory=None;p.item_draw=None;p.render_visible=true;
        p.rest_positions=authored.rest_positions;p.positions=authored.positions;
        p.rest_normals=authored.rest_normals;p.normals=authored.normals;p.rest_tangents=authored.rest_tangents;
        p.uvs=authored.uvs;p.uv2=if m.gloss_uvs.is_empty(){p.uvs.clone()}else{m.gloss_uvs.clone()};p.uv3.clear();p.uv4.clear();p.vertex_colors=authored.vertex_colors;
        p.indices=authored.indices;p.weights=authored.weights;p.bone_idx=authored.bone_idx;p.bind_poses=authored.bind_poses;
        p.skel_kind=1;p.parent_bone=None;p.rigid_follow_bone=None;p.parent_c=CString::default();p.rigid_follow_c=CString::default();
        p.blend_shapes.clear();p.morph_idx.clear();p.morph_dxyz.clear();
        p.png=Some(texture.to_vec());p.shader_textures.insert("_MainTex".into(),texture.to_vec());
        p.runtime_textures.insert("_MainTex".into(),[1,0,0,1,1]);p.main_tex_st=[1.,1.,0.,0.];
        p.alpha_mask=None;p.blend="opaque".into();p.blend_c=CString::new("opaque").unwrap();
        if let Some(mat)=p.source_material.as_mut(){mat.render_queue=2000;for key in ["_Color","_Color2","_Color3"]{mat.colors.insert(key.into(),[1.;4]);}}
        p.overcolor=[1.;4];p.overcolor2=[1.;4];
        if let Some(id)=&m.native_gloss_asset {
            let root=crate::portable_resources::root(Path::new(path))?;
            let raw=crate::portable_resources::decode(&crate::gamefs::read(root.join("assets").join(format!("{id}.okasset"))).map_err(|e|e.to_string())?,"texture",id)?;
            let gloss:Vec<u8>=bincode::deserialize(&raw).map_err(|e|e.to_string())?;
            crate::teximg::load_from_memory(&gloss).map_err(|e|e.to_string())?;
            p.hair_gloss=Some(gloss.clone());p.shader_textures.insert("_HairGloss".into(),gloss);
            p.runtime_textures.insert("_HairGloss".into(),[1,0,0,0,1]);p.hair_gloss_st=[1.,1.,0.,0.];
        }
    }
    if let Some(rig)=&m.soft_rig {soft::install(p,rig,&head_world)?;}
    s.skin_all();s.skel_export=None;
    Ok(())
}
fn non_hair_geometry(s:&OkSession,check_visibility:bool)->Result<Vec<u32>,String> {
    s.parts.iter().filter(|p|!p.role.starts_with("hair") && !(p.role=="accessory" && p.name=="Hair"))
        // Compare authored channels. Posed vertices are a derived cache which
        // skin_all legitimately refreshes, including inactive outfit parts.
        .map(|p|bincode::serialize(&(&p.role,&p.name,if check_visibility{p.render_visible}else{true},&p.rest_positions,&p.rest_normals,&p.uvs,&p.indices,&p.weights,&p.bone_idx,&p.bind_poses,&p.png)).map(|v|crc(&v)).map_err(|e|e.to_string())).collect()
}
fn check_head_follow(s:&mut OkSession,name:&str)->Result<f32,String> {
    let sk=s.body_skel.as_mut().ok_or("missing body skeleton")?;let h=head(sk)?;let old=sk.worlds[h];
    // Rotate 30 degrees around the head's local up axis, retaining translation.
    let a=30f32.to_radians();let r=[[a.cos(),0.,a.sin(),0.],[0.,1.,0.,0.],[-a.sin(),0.,a.cos(),0.],[0.,0.,0.,1.]];
    let posed=crate::pose::mat_mul_pub(&old,&r);sk.worlds[h]=posed;s.skin_all();
    let p=s.parts.iter().find(|p|p.name==name).ok_or("missing custom hair")?;
    let error=p.rest_positions.iter().zip(&p.positions).map(|(v,got)|{
        let expected=crate::pose::mat_vec(&posed,*v);(0..3).map(|i|(expected[i]-got[i]).abs()).fold(0f32,f32::max)
    }).fold(0f32,f32::max);
    if !error.is_finite() || error>1e-5{return Err(format!("head-follow mismatch: {error}"));}
    s.body_skel.as_mut().unwrap().worlds[h]=old;s.skin_all();Ok(error)
}
pub fn import_hair_card(input:&Path,mesh:&Path,output:&Path)->Result<serde_json::Value,String> {
    if output.ok_exists(){return Err("hair output already exists".into());}
    let m:HairMesh=serde_json::from_slice(&crate::gamefs::read(mesh).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;validate(&m)?;
    let texture_path=mesh.parent().unwrap_or(Path::new(".")).join(&m.texture);
    let texture=crate::gamefs::read(&texture_path).map_err(|e|e.to_string())?;
    crate::teximg::load_from_memory(&texture).map_err(|e|e.to_string())?;
    let pbr=match (&m.normal,&m.roughness,&m.metallic){
        (None,None,None)=>None,
        (Some(n),Some(r),Some(t))=>{
            let read=|name:&str|->Result<Vec<u8>,String>{let bytes=crate::gamefs::read(mesh.parent().unwrap_or(Path::new(".")).join(name)).map_err(|e|e.to_string())?;crate::teximg::load_from_memory(&bytes).map_err(|e|e.to_string())?;Ok(bytes)};
            Some([read(n)?,read(r)?,read(t)?])
        },
        _=>return Err("PBR requires normal, roughness and metallic maps together".into()),
    };
    let original=crate::gamefs::read(input).map_err(|e|e.to_string())?;let(_,range)=png_snapshot(&original)?;let range=range.ok_or("missing portable snapshot")?;
    let root=crate::portable_resources::root(input)?;let count=load(input)?.coordinate_count;let mut snapshots=Vec::new();let mut preserved=Vec::new();
    let mut soft_template:Option<crate::session::AccessoryRuntime>=None;
    for index in 0..count {
        let mut s=load_outfit(input,index as i32)?;
        preserved.push(non_hair_geometry(&s,!m.hide_source_accessory)?);
        // Transform all outfits through head-local coordinates, so their head
        // pose and the card's shape settings remain the source of placement.
        install(&mut s,&m,&texture,pbr.as_ref())?;
        // Cloning one authored source preserves HashMap order in bincode and
        // deduplicates identical private rigs across all outfit snapshots.
        if m.soft_rig.is_some(){
            let p=s.parts.iter_mut().find(|p|p.name==part_name(&m)).unwrap();
            if let Some(template)=&soft_template{p.runtime_accessory=Some(template.clone());}
            else {soft_template=p.runtime_accessory.clone();}
        }
        let info=crate::card::parse_card_bytes_mode(&original,index as i32,false)?;
        let mut references=Vec::new();let mut unique=BTreeMap::new();
        let residue=s.split_preview_resources(&info,|part,field,kind,raw,_|{
            let id=crate::portable_resources::id(kind,&raw);
            if !root.join("assets").join(format!("{id}.okasset")).ok_is_file(){unique.entry(id.clone()).or_insert(raw);}
            references.push(crate::portable_resources::ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
        })?;
        snapshots.push(crate::portable_resources::SplitSnapshot{schema:1,residue,references,unique});
    }
    let(payload,mut report)=crate::portable_outfits::pack(snapshots,&root)?;
    let mut chunk=CHUNK.to_vec();chunk.extend(&payload);
    let mut bytes=original[..range.start-8].to_vec();bytes.extend((payload.len() as u32).to_be_bytes());bytes.extend(&chunk);bytes.extend(crc(&chunk).to_be_bytes());bytes.extend(&original[range.end+4..]);
    if bytes.len() as u64>MAX_BYTES{return Err("hair card exceeds size limit".into());}
    let mut f=fs::OpenOptions::new().create_new(true).write(true).open(output).map_err(|e|e.to_string())?;f.write_all(&bytes).and_then(|_|f.sync_all()).map_err(|e|e.to_string())?;
    let mut head_error=0f32;
    for index in 0..count {
        let mut s=load_outfit(output,index as i32)?;
        if non_hair_geometry(&s,!m.hide_source_accessory)?!=preserved[index as usize]{return Err(format!("non-hair geometry changed in outfit {index}"));}
        let p=s.parts.iter().find(|p|p.name==part_name(&m)).ok_or("hair missing after reload")?;
        if p.indices!=m.indices || p.rest_positions!=m.positions || p.png.as_deref()!=Some(texture.as_slice()) {return Err("hair data changed on reload".into());}
        if let Some(rig)=&m.soft_rig {if p.weights!=rig.weights || p.bone_idx!=rig.bone_idx || p.runtime_accessory.is_none(){return Err("soft hair rig changed on reload".into());}}
        else if p.weights.iter().any(|w|*w!=[1.,0.,0.,0.]) {return Err("rigid hair weights changed on reload".into());}
        if let Some(maps)=&pbr {
            if p.normal_map.as_ref()!=Some(&maps[0]) || p.shader_textures.get("_RoughnessMap")!=Some(&maps[1]) || p.shader_textures.get("_MetallicMap")!=Some(&maps[2]) || p.unlit || p.source_material.as_ref().map(|x|x.shader.as_str())!=Some("OpenKoikatsu/TripoPBR") {return Err("PBR material changed on reload".into());}
        }
        if m.native_material_card.is_some(){
            if !matches!(p.shader_kind,1|10){return Err("native hair shader lost on reload".into());}
            report["native_hair_shader"]=serde_json::json!(p.source_material.as_ref().map(|x|x.shader.as_str()));
            report["native_hair_gloss_bytes"]=serde_json::json!(p.hair_gloss.as_ref().map_or(0,Vec::len));
            report["native_hair_ramp_slots"]=serde_json::json!(p.shader_textures.keys().collect::<Vec<_>>());
        }
        if m.soft_rig.is_some(){report["soft_hair_motion"]=soft::verify_motion(&s,&part_name(&m))?;}
        else {head_error=head_error.max(check_head_follow(&mut s,&part_name(&m))?);}
    }
    report["non_hair_geometry_preserved_all_outfits"]=serde_json::json!(true);
    report["source_accessory_decorations_hidden"]=serde_json::json!(m.hide_source_accessory);
    if m.soft_rig.is_none(){report["head_rotation_degrees_tested"]=serde_json::json!(30);report["head_follow_max_error"]=serde_json::json!(head_error);}
    report["pbr_maps_roundtrip"]=serde_json::json!(pbr.is_some());
    report["card"]=serde_json::json!(output);report["outfits"]=serde_json::json!(count);report["triangles"]=serde_json::json!(m.indices.len()/3);report["part"]=serde_json::json!(part_name(&m));report["custom_hair_parts"]=serde_json::json!(load_outfit(output,0)?.parts.iter().filter(|p|is_custom(p)&&p.render_visible).map(|p|p.name.clone()).collect::<Vec<_>>());report["attachment"]=serde_json::json!(if m.soft_rig.is_some(){"cf_s_head; authored private hair rig; native DynamicBone"}else{"cf_s_head; rigid head-local skinning; no hair-tail dynamics"});report["roundtrip"]=serde_json::json!(true);Ok(report)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn rejects_invalid_and_over_budget_hair(){
        let mut m=HairMesh{positions:vec![[0.;3];3],normals:vec![[0.,0.,1.];3],tangents:vec![[1.,0.,0.,1.];3],uvs:vec![[0.;2];3],indices:vec![0,1,2],texture:"x.png".into(),normal:None,roughness:None,metallic:None,native_material_card:None,native_gloss_asset:None,gloss_uvs:Vec::new(),soft_rig:None,hide_source_accessory:false,layer:None};
        assert!(validate(&m).is_ok());m.indices=vec![0,1,2].repeat(50001);assert!(validate(&m).is_err());m.indices=vec![0,1,3];assert!(validate(&m).is_err());m.indices=vec![0,1,2];m.positions[0][0]=f32::NAN;assert!(validate(&m).is_err());
    }
}
