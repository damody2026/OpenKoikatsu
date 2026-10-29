//! Exact source inputs shared by whole-character and single-slot extraction.
use std::{collections::{HashMap,HashSet},path::{Path,PathBuf}};
use serde::{Serialize,Deserialize};
use crate::{card::CardInfo,list::ListEntry,pose::Skeleton,session::SessionPart,ManifestPart};

#[derive(Clone,Serialize,Deserialize)]
pub(crate) struct Context {
    pub schema:u32,
    pub dependencies:Vec<u8>,
    pub body:Option<Skeleton>,
    pub head:Option<Skeleton>,
    pub head_textures:Vec<(String,Vec<u8>)>,
    pub body_textures:Vec<(String,Vec<u8>)>,
    pub hair_gloss:Option<Vec<u8>>,
    pub hair_gloss_sampling:Option<[i32;5]>,
}
impl Context {
    pub fn dependencies(card:&CardInfo,outfit:i32)->Result<Vec<u8>,String>{
        // An absent clothes request and an explicit all-worn request have the
        // same source semantics. Compare effective states, not their spelling.
        let _states=crate::unity::request_clothes_states(crate::unity::requested_clothes_states());
        let mut resolves:Vec<_>=card.resolves.iter().filter(|r|r.category_no==100||r.category_no==433)
            .map(|r|(&r.mod_id,r.slot,r.local_slot,&r.property,r.category_no)).collect();resolves.sort();
        bincode::serialize(&(1u32,outfit,card.sex,card.type_bone,card.head_id,&card.body_guid,
            &card.shape_body,&card.shape_face,card.nip_stand_rate,&card.abm,card.hair_gloss_id,
            resolves,crate::sqlcache::env_sig())).map_err(|e|e.to_string())
    }
    pub fn matches(&self,card:&CardInfo,outfit:i32)->Result<bool,String>{
        Ok(self.schema==1&&self.dependencies==Self::dependencies(card,outfit)?)
    }
    /// Failure-path diagnostic: name the first dependency component that
    /// differs (bincode concatenates fields, so compare successive slices).
    pub fn mismatch(&self,card:&CardInfo,outfit:i32)->String{
        let _states=crate::unity::request_clothes_states(crate::unity::requested_clothes_states());
        let mut resolves:Vec<_>=card.resolves.iter().filter(|r|r.category_no==100||r.category_no==433)
            .map(|r|(&r.mod_id,r.slot,r.local_slot,&r.property,r.category_no)).collect();resolves.sort();
        let env=crate::sqlcache::env_sig();
        let parts:Vec<(&str,Vec<u8>)>=vec![("schema",bincode::serialize(&1u32).unwrap_or_default()),("outfit",bincode::serialize(&outfit).unwrap_or_default()),
            ("sex",bincode::serialize(&card.sex).unwrap_or_default()),("type_bone",bincode::serialize(&card.type_bone).unwrap_or_default()),
            ("head_id",bincode::serialize(&card.head_id).unwrap_or_default()),("body_guid",bincode::serialize(&card.body_guid).unwrap_or_default()),
            ("shape_body",bincode::serialize(&card.shape_body).unwrap_or_default()),("shape_face",bincode::serialize(&card.shape_face).unwrap_or_default()),
            ("nip_stand_rate",bincode::serialize(&card.nip_stand_rate).unwrap_or_default()),("abm",bincode::serialize(&card.abm).unwrap_or_default()),
            ("hair_gloss_id",bincode::serialize(&card.hair_gloss_id).unwrap_or_default()),("resolves",bincode::serialize(&resolves).unwrap_or_default()),
            ("env",bincode::serialize(&env).unwrap_or_default())];
        let mut offset=0;
        for(name,bytes)in &parts{
            let end=offset+bytes.len();
            if self.dependencies.get(offset..end)!=Some(bytes.as_slice()){
                if *name=="env"{
                    let stored=self.dependencies.get(offset..).and_then(|b|bincode::deserialize::<String>(b).ok()).unwrap_or_default();
                    return format!("env stored=[{stored}] current=[{env}]");
                }
                return format!("{name} at byte {offset}");
            }
            offset=end;
        }
        if offset!=self.dependencies.len(){return "trailing dependency bytes".into();}
        if self.schema!=1{return "schema".into();}
        "none".into()
    }
}

pub(crate) struct Source {
    pub entry:ListEntry,pub zip:Option<PathBuf>,pub guid:Option<String>,
    pub category:i32,pub id:i32,pub property:String,pub display:String,
    pub missing_entry:bool,
}

/// Emit the original colour, MaterialEditor, N_move, runtime-rig and binding
/// semantics once. No composed skin texture or mesh-name heuristic is used.
/// `batch`: OK_FIRSTLOAD=A2 keeps the slot's meshes for the open's PushBatch.
pub(crate) fn emit<'a>(game:&'a Path,card:&'a CardInfo,outfit:i32,source:&Source,context:&'a Context,
    out:Option<&'a Path>,manifest:&mut Vec<ManifestPart>,parts:&mut Vec<SessionPart>,seen:&mut HashSet<String>,
    batch:Option<&mut crate::PushBatch<'a>>)->Result<(),String>{
    let role=crate::role_for_category(source.category,&source.property);
    if !role.starts_with("acc"){return Err("single-slot source is not an accessory".into());}
    let (mut meshes,rig)=crate::appearance::timings::measure("slot.meshes",||if source.missing_entry {
        let shake=crate::accessory_slot_from_property(&source.property)
            .and_then(|i|card.accessory.get(i)).is_none_or(|p|!p.no_shake);
        crate::meshes_for_entry_shake(game,source.zip.as_deref(),&source.entry,shake)
    }else{crate::meshes_for_accessory_entry(game,source.zip.as_deref(),&source.entry,card,outfit,&source.property)});
    // A slot whose list entry is missing (an OK card's own accessory) still takes the card's DynamicBoneEditor rows.
    if source.missing_entry {crate::apply_dynamic_bone_editor(&mut meshes,card,outfit,&source.property);}
    if std::env::var_os("OK_DBONE_DIAG").is_some() {
        eprintln!("ok-extract: DBONE slot {} missing_entry={} ab={} main={} zip={:?} meshes={:?}",source.property,source.missing_entry,source.entry.main_ab,source.entry.main_data,source.zip,
            meshes.iter().map(|m|(m.name.clone(),m.deferred_dbones.as_ref().map(|d|d.len()))).collect::<Vec<_>>());
    }
    // With an open's batch (OK_FIRSTLOAD=A2) the slot's colour bake and push run at its flush, the colour bakes of
    // every slot at once (a card's accessories coloured one after another took ~0.9 s of a first open). The push keeps
    // its place among the batch's calls; its duplicate keys carry the slot's card property, which no other call has.
    if batch.is_some() && crate::first_load_option("A2") {
        batch.unwrap().defer_accessory(Deferred{game,card,outfit,source:Source{entry:source.entry.clone(),zip:source.zip.clone(),guid:source.guid.clone(),
            category:source.category,id:source.id,property:source.property.clone(),display:source.display.clone(),missing_entry:source.missing_entry},
            context,out,meshes,rig,clothes_active:crate::unity::active_clothes_states(),clothes_requested:crate::unity::explicit_requested_clothes_states(),
            overrides:crate::profile::snapshot()});
        return Ok(());
    }
    color(game,card,outfit,source,context,&mut meshes);
    finish(game,card,outfit,source,context,out,manifest,parts,seen,batch,meshes,rig)
}

/// An accessory slot kept by an open's PushBatch (`emit`): its meshes, and this thread's state when it was kept (the
/// garment states and profile switches, applied again on the worker that colours it).
pub(crate) struct Deferred<'a> {
    pub game:&'a Path,pub card:&'a CardInfo,pub outfit:i32,pub source:Source,pub context:&'a Context,pub out:Option<&'a Path>,
    pub meshes:Vec<crate::unity::ExtractedMesh>,pub rig:Option<Skeleton>,
    pub clothes_active:Option<[u8;9]>,pub clothes_requested:Option<[u8;9]>,pub overrides:Vec<(&'static str,Option<&'static str>)>,
}
impl<'a> Deferred<'a> {
    /// The colour bake, on any thread.
    pub(crate) fn color(&mut self) {
        let _profile=crate::profile::scope(&self.overrides);
        let _active=self.clothes_active.map(crate::unity::set_session_clothes_states);
        let _requested=self.clothes_requested.map(crate::unity::request_clothes_states);
        color(self.game,self.card,self.outfit,&self.source,self.context,&mut self.meshes);
    }
    /// The push, on the open's thread, into `batch`.
    pub(crate) fn finish(self,manifest:&mut Vec<ManifestPart>,parts:&mut Vec<SessionPart>,seen:&mut HashSet<String>,batch:&mut crate::PushBatch<'a>)->Result<(),String> {
        let _active=self.clothes_active.map(crate::unity::set_session_clothes_states);
        let _requested=self.clothes_requested.map(crate::unity::request_clothes_states);
        let Deferred{game,card,outfit,source,context,out,meshes,rig,..}=self;
        finish(game,card,outfit,&source,context,out,manifest,parts,seen,Some(batch),meshes,rig)
    }
}

fn color(game:&Path,card:&CardInfo,outfit:i32,source:&Source,context:&Context,meshes:&mut Vec<crate::unity::ExtractedMesh>) {
    crate::appearance::timings::measure("slot.color",||crate::color_accessory_meshes(game,source.zip.as_deref(),&source.entry,card,&source.property,outfit,
        meshes,&context.head_textures,&context.body_textures,context.hair_gloss.as_deref()));
}

#[allow(clippy::too_many_arguments)]
fn finish<'a>(game:&'a Path,card:&'a CardInfo,outfit:i32,source:&Source,context:&'a Context,
    out:Option<&'a Path>,manifest:&mut Vec<ManifestPart>,parts:&mut Vec<SessionPart>,seen:&mut HashSet<String>,
    batch:Option<&mut crate::PushBatch<'a>>,meshes:Vec<crate::unity::ExtractedMesh>,rig:Option<Skeleton>)->Result<(),String>{
    let role=crate::role_for_category(source.category,&source.property);
    let moves=crate::accessory_slot_from_property(&source.property).and_then(|i|card.accessory.get(i))
        .map(|a|crate::acc_place_moves(a,crate::hide_hair_list(&source.entry)))
        .unwrap_or([crate::identity_move(),crate::identity_move()]);
    let _place=crate::begin_acc_place(moves);
    let local=crate::acc_local_for(card,&source.property);
    let parent=if source.guid.is_none() {
        crate::accessory_slot_from_property(&source.property).and_then(|i|card.accessory.get(i))
            .map(|a|crate::resolve_acc_parent(&a.parent_key,&source.entry.parent))
    }else{crate::parent_for_part(card,&source.property,&source.entry.parent,role)};
    let assignment=crate::appearance::timings::measure("slot.assignment",||crate::accessory_assignment(game,source.zip.as_deref(),&source.entry));
    crate::appearance::timings::measure("slot.push_meshes",||crate::push_meshes(manifest,parts,out,game,role,meshes,Some(source.id),Some(source.category),
        source.guid.clone(),Some(source.property.clone()),Some(source.display.clone()),
        context.body.as_ref(),context.head.as_ref(),parent.as_deref(),None,seen,rig.as_ref(),local.as_ref(),
        Some(&card.me_renderer_off),Some(&card.me_tex),Some(&card.me_color),Some(&card.me_shader),
        Some(&card.me_float),outfit,None,assignment.as_ref(),context.hair_gloss_sampling,batch))?;
    Ok(())
}

pub(crate) fn source_for_slot(game:&Path,card:&CardInfo,outfit:i32,slot:usize)->Result<Option<Source>,String>{
    let a=card.accessory.get(slot).ok_or("accessory slot is out of range")?;
    let wanted=crate::outfit_prefix(outfit);
    let resolves:Vec<_>=card.resolves.iter().filter(|r|(121..=130).contains(&r.category_no)
        &&crate::accessory_slot_from_property(&r.property)==Some(slot)
        &&(!r.property.to_ascii_lowercase().contains("outfit")||r.property.to_ascii_lowercase().contains(&wanted))).collect();
    if resolves.len()>1{return Err("ambiguous accessory source provenance".into());}
    // Unused records have no asset dependency, but resolved mod records must
    // still follow the original precedence even when their raw type is zero.
    if resolves.is_empty()&&(a.type_id==120||!crate::is_mesh_category(a.type_id)){return Ok(None);}
    let vanilla=crate::list::load_characustom_dir_shared(&game.join("abdata/list/characustom"));
    if let Some(r)=resolves.first(){
        if crate::is_dummy_part("",Some(&r.name)){return Ok(None);}
        let wanted=HashSet::from([r.mod_id.clone()]);let guids=crate::mods::find_guids(&game.join("mods"),Some(&wanted));
        let zip=crate::guid_path(&guids,&r.mod_id).cloned();
        let mut lists=HashMap::new();
        let entry=crate::lookup_entry(&vanilla,&mut lists,zip.as_deref(),&r.mod_id,r.category_no,r.slot,r.local_slot);
        let missing_entry=entry.is_none();
        if missing_entry&&zip.is_none(){return Err(format!("accessory source missing: {}",r.mod_id));}
        let entry=entry.unwrap_or_else(||ListEntry{category_no:r.category_no,id:r.slot,name:r.name.clone(),
            main_ab:String::new(),main_data:String::new(),main_tex:String::new(),parent:String::new(),extra:Default::default()});
        return Ok(Some(Source{display:if r.name.is_empty(){entry.name.clone()}else{r.name.clone()},
            entry,zip,guid:Some(r.mod_id.clone()),category:r.category_no,id:r.slot,property:r.property.clone(),missing_entry}));
    }
    // Legacy/extra unused records also contain zero. Match the full loader's
    // mesh-category gate rather than treating these absent items as errors.
    if a.type_id==120||!crate::is_mesh_category(a.type_id){return Ok(None);}
    if !(121..=130).contains(&a.type_id){return Err("invalid accessory category".into());}
    let entry=vanilla.get(a.type_id,a.id).cloned().or_else(||{
        crate::list::load_mods_catalog(&game.join("mods")).get(a.type_id,a.id).cloned()
    })
        .ok_or("accessory item is absent from source catalog")?;
    if crate::is_dummy_part(&entry.main_data,Some(&entry.name)){return Ok(None);}
    Ok(Some(Source{zip:entry.extra.get("__zip").map(PathBuf::from),guid:None,category:a.type_id,id:a.id,
        property:format!("outfit{outfit}.accessory{slot}"),display:entry.name.clone(),entry,missing_entry:false}))
}

/// Build only one card slot. The caller supplies the exact immutable context
/// captured by the full pipeline and retains every unrelated renderer/global.
pub(crate) fn extract_slot(game:&Path,card:&CardInfo,outfit:i32,slot:usize,context:&Context)
    ->Result<Vec<SessionPart>,String>{
    if !context.matches(card,outfit)?{return Err("accessory extraction dependencies changed".into());}
    let mut parts=Vec::new();
    if let Some(source)=crate::appearance::timings::measure("slot.source",||source_for_slot(game,card,outfit,slot))? {
        crate::appearance::timings::measure("slot.references",||crate::unity::references::begin_load(game));
        emit(game,card,outfit,&source,context,None,&mut Vec::new(),&mut parts,&mut HashSet::new(),None)?;
    }
    Ok(parts)
}
