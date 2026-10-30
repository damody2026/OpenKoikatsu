//! In-memory extract session: rest mesh + keys + skeleton, no OBJ on disk.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use std::collections::HashMap;
use std::path::{Path, PathBuf};

// Experimental exporter is opt-in; the Unreal static library is unchanged.
#[cfg(feature = "three-export")]
#[path = "../../../three/exporter/src/export.rs"]
pub mod three_export;

#[cfg(test)]
#[path = "session_external_tests.rs"]
mod external_tests;
#[cfg(test)]
#[path = "session_animator_tests.rs"]
mod animator_tests;
#[cfg(test)]
#[path = "session_yure_tests.rs"]
mod yure_tests;
#[cfg(test)]
#[path = "session_pv_tests.rs"]
mod pv_tests;
#[cfg(test)]
#[path = "session_renderer_tests.rs"]
mod renderer_tests;

use crate::pose::Skeleton;
use crate::shape::{CorrectInfo, Keyframe};
use crate::unity::ExtractedMesh;

pub(crate) fn native_parallel_work<F,R>(work:F)->R where F:FnOnce()->R+Send,R:Send {
    // A card opening with its own pack: its own pool, whose threads read that pack (oklib::pack_parallel).
    if crate::oklib::pack_scope_here() { return crate::oklib::pack_parallel(work); }
    match native_pool(true) {Some(pool)=>pool.install(work),None=>work()}
}
/// Runs `f` once on every native worker thread (their per-thread caches), if the pool was started.
pub(crate) fn native_workers_broadcast(f:impl Fn()+Sync) {
    if let Some(pool)=native_pool(false) { pool.broadcast(|_|f()); }
}
fn native_pool(start:bool)->Option<&'static rayon::ThreadPool> {
    static POOL:std::sync::OnceLock<Option<rayon::ThreadPool>>=std::sync::OnceLock::new();
    if !start { return POOL.get()?.as_ref(); }
    POOL.get_or_init(||{
        // Bound native work alongside Unreal's task workers. Nested accessory
        // jobs and the two independent actors share this same pool.
        let workers=std::env::var("OK_NATIVE_WORKERS").ok().and_then(|v|v.parse::<usize>().ok())
            .unwrap_or_else(||std::num::NonZeroUsize::new(crate::cpu_threads()).map_or(2,|n|{
                // Keep the established small-machine limit; larger machines
                // can overlap both roles without consuming every UE worker.
                let available=n.get();available.div_ceil(2).max(4).min(available).min(8)
            }));
        if !(1..=64).contains(&workers){return None;}
        rayon::ThreadPoolBuilder::new().num_threads(workers).thread_name(|i|format!("OKNativeWorker{i}")).build().ok()
    }).as_ref()
}

/// Read global renderer controls once per synchronous accessory pass. This
/// remains a live frame snapshot, not a process-wide cache: audit profiling and
/// diagnostic/renderer toggles continue to take effect on the next frame.
#[derive(Clone, Copy)]
pub(crate) struct AccessoryFramePolicy { profiling:bool, rigid_clothes:bool, parity:bool, optional_skin:bool }
impl AccessoryFramePolicy {
    pub(crate) fn read()->Self {
        Self {
            profiling:std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1"),
            rigid_clothes:std::env::var("OK_NO_CLOTH_DYNAMICS").ok().as_deref()==Some("1"),
            parity:std::env::var("OK_ANIM_SKIN_PARITY").ok().as_deref()==Some("1"),
            optional_skin:std::env::var("OK_SMOOTH").ok().and_then(|v|v.parse::<usize>().ok()).unwrap_or(0)>0||
                ["OK_DQS","OK_DQSH","OK_RELAX","OK_DIAG","OK_SKINDIAG","OK_BONEDIAG"].iter().any(|key|std::env::var(key).is_ok()),
        }
    }
    pub(crate) fn supports_private_gpu(&self)->bool{!self.optional_skin}
}

/// Immutable, scoped to one solved body pose. Missing names are cached too;
/// private renderer fallback decisions still belong to effective_worlds.
struct AssignedWorldFrame { worlds:HashMap<String,Option<crate::pose::Mat4>> }
impl AssignedWorldFrame {
    fn prepare(body:Option<&Skeleton>,parts:&[SessionPart],coordinated:bool)->Option<Self> {
        // Exact oracle passes, but normal-RHI ABA did not improve frame time.
        if std::env::var("OK_ASSIGNED_WORLD_FRAME").ok().as_deref()!=Some("1"){return None;}
        let body=body?;
        let mut requests=std::collections::HashSet::new();
        for runtime in parts.iter().filter_map(|p|p.runtime_accessory.as_ref())
            .filter(|r|r.copy_weights_body&&r.coordinated==coordinated) {
            let private=runtime.state.as_ref().map(|s|&s.skel).or(runtime.source.smr_skel.as_ref())?;
            for (name,id) in private.names.iter().zip(&private.bone_ids) {
                if runtime.implanted_bones.contains(id) {
                    if runtime.coordinated {requests.insert(crate::dbone::implant::rig::key(&runtime.clothes_component,*id));}
                } else {requests.insert(name.clone());}
            }
        }
        if requests.is_empty(){return None;}
        let names=crate::pose::rig_name_index(body,false);
        let mut ancestors=HashMap::new();
        let worlds=requests.into_iter().map(|name|{
            let world=names.get(&name).map(|id|crate::pose::world_of(*id,&body.tfs,&mut ancestors));
            (name,world)
        }).collect();
        Some(Self{worlds})
    }
}

fn authored_rig_bytes(skel: &Skeleton, bridges: &[(i64,String)]) -> Result<Vec<u8>, bincode::Error> {
    let mut transforms: Vec<_> = skel.tfs.iter().map(|(id,tf)| {
        // Implant.sync overwrites these TRS values from the validated body rig.
        // Their names/parents and bridge assignments remain source contracts.
        let trs=if bridges.iter().any(|(bridge,_)|bridge==id){None}else{Some((tf.pos,tf.rot,tf.scale))};
        (*id,&tf.name,tf.parent,tf.sibling_index,trs)
    }).collect();
    transforms.sort_unstable_by_key(|tf| tf.0);
    // worlds are evaluated state; rest and authored hierarchy are source data.
    bincode::serialize(&(&skel.names, &skel.rest, skel.smr_count, &skel.bone_ids, transforms))
}

/// Prefab-space renderer plus its own DynamicBone chain. Static card data is
/// cached; particle history is deliberately rebuilt when a session opens.
// Rest geometry is immutable while published. Pose/physics changes may mutate
// the rig, but replacing/extracting geometry must invalidate this cache. Clones
// own a new revision: a token never identifies another session by its address.
#[derive(Default)]
struct PrivateSourceValidation(std::sync::Mutex<Option<(usize, u64, bool)>>,
    #[cfg(test)] std::sync::atomic::AtomicUsize);
impl Clone for PrivateSourceValidation {
    fn clone(&self) -> Self { Self::default() }
}
static PRIVATE_SOURCE_REVISION: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub(crate) struct AccessoryRuntime {
    source: ExtractedMesh,
    #[serde(skip)]
    source_validation: PrivateSourceValidation,
    parent_key: String,
    local_placement: crate::pose::Mat4,
    /// AssignedWeightsAndSetBounds replaces only exact matching SMR slots.
    /// The remaining slots skin against the surviving private prefab rig.
    copy_weights_body: bool,
    implants_active: bool,
    #[serde(serialize_with="crate::ordered::set")]
    implanted_bones: std::collections::HashSet<i64>,
    body_bridges: Vec<(i64,String)>,
    clothes_component: String,
    coordinated: bool,
    #[serde(serialize_with="crate::ordered::set")]
    removed_accessory_bones: std::collections::HashSet<i64>,
    #[serde(skip)]
    collider_flags: Option<crate::dbone::game_colliders::Flags>,
    #[serde(skip)]
    state: Option<crate::dbone::AttachedDynamicSet>,
}

/// An accessory runtime keeps only geometry and rig data: the SessionPart already owns every image and material, so
/// duplicating them in the cache wastes many MB.
fn drop_part_images(source: &mut ExtractedMesh) {
    source.obj.clear();
    source.mat = None;
    source.shader_extra_textures.clear();
    source.texture_png = None;
    source.color_mask_png = None;
    source.alpha_mask_png = None;
    source.detail_mask_png = None;
    source.line_mask_png = None;
    source.normal_map_png = None;
    source.normal_mask_png = None;
    source.hair_gloss_png = None;
}

impl AccessoryRuntime {
    /// Diagnostics: (deferred DynamicBone components on the source, live particles (pos, prev)) when simulated.
    #[cfg(test)]
    pub(crate) fn dynamic_probe(&self)->(usize,Option<Vec<([f32;3],[f32;3])>>){
        (self.source.deferred_dbones.as_ref().map_or(0,|d|d.len()),self.state.as_ref().map(|s|s.particle_snapshot()))
    }
    pub(crate) fn from_head_local_hair(raw:&ExtractedMesh,head_world:&crate::pose::Mat4)->Option<Self> {
        Self::from_placed_source(raw,raw.smr_skel.as_ref(),"cf_s_head",head_world,*head_world,true)
    }
    pub(crate) fn replace_source(&mut self, source: ExtractedMesh) {
        self.source = source;
        self.source_validation = PrivateSourceValidation::default();
    }
    /// O(vertices) only for a new source/palette layout. The source is borrowed
    /// for this call; callers must copy it before any subsequent session edit.
    pub(crate) fn private_source_revision(&self, palette_count: usize) -> Option<u64> {
        let mut cached = self.source_validation.0.lock().unwrap_or_else(|e| e.into_inner());
        if let Some((count, revision, valid)) = *cached {
            if count == palette_count { return valid.then_some(revision); }
        }
        let mesh = &self.source;
        #[cfg(test)]
        self.source_validation.1.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let n = mesh.rest_positions.len();
        let valid = n > 0 && n <= i32::MAX as usize && (2..=65535).contains(&palette_count)
            && mesh.weights.len() == n && mesh.bone_idx.len() == n
            && mesh.rest_positions.iter().flatten().all(|v| v.is_finite())
            && mesh.rest_normals.iter().flatten().all(|v| v.is_finite())
            && mesh.tangents.iter().flatten().all(|v| v.is_finite())
            && mesh.weights.iter().zip(&mesh.bone_idx).all(|(weights, bones)|
                weights.iter().all(|w| w.is_finite()) && weights.iter().zip(bones)
                    .all(|(w,b)| w.abs() < 1e-6 || (*b as usize) < palette_count - 1));
        let revision = PRIVATE_SOURCE_REVISION.fetch_update(std::sync::atomic::Ordering::Relaxed,
            std::sync::atomic::Ordering::Relaxed, |v| v.checked_add(1)).expect("source revision exhausted");
        *cached = Some((palette_count, revision, valid));
        valid.then_some(revision)
    }
    #[cfg(test)]
    pub(crate) fn dynamic_state_for_test(&self)->Option<&crate::dbone::AttachedDynamicSet>{self.state.as_ref()}
    pub(crate) fn source_mesh(&self) -> &ExtractedMesh { &self.source }
    // Coordinated implanted clothes use the solved shared clothes rig.
    pub(crate) fn editor_bind_source(&self,body:&Skeleton)->Option<(&ExtractedMesh,Vec<crate::pose::Mat4>)>{
        let mut policy=AccessoryFramePolicy::read();policy.rigid_clothes=true;
        let parent=self.private_skin_parent_key().and_then(|key|crate::bone_world(Some(body),None,key)).unwrap_or_else(crate::pose::ident);
        let(mesh,mut matrices,post)=self.export_skin_stages_for_parent(Some(body),policy,parent)?;
        for matrix in &mut matrices{*matrix=crate::pose::mat_mul_pub(&post,matrix);}Some((mesh,matrices))
    }
    pub(crate) fn skin_body<'a>(&self, body:Option<&'a Skeleton>, clothes:Option<&'a Skeleton>)->Option<&'a Skeleton> {
        if self.coordinated {clothes.or(body)} else {body}
    }
    pub(crate) fn gpu_stream_policy_supported(&self)->bool { !AccessoryFramePolicy::read().optional_skin }
    // Ownership concerns immutable streams/layout, not this frame's matrices.
    // Stage reads still validate current affine matrices before GPU publication.
    pub(crate) fn current_private_source_revision(&self)->Option<u64>{
        let policy=AccessoryFramePolicy::read();if policy.optional_skin{return None;}
        let rigid=self.copy_weights_body&&policy.rigid_clothes;
        if !rigid&&self.state.is_none()&&self.source.deferred_dbones.as_ref().is_some_and(|d|!d.is_empty()){return None;}
        let private=if let Some(state)=self.state.as_ref().filter(|_|!rigid){&state.skel}else{self.source.smr_skel.as_ref()?};
        let count=if !self.source.bind_poses.is_empty(){self.source.bind_poses.len().min(private.worlds.len())}else{private.worlds.len().min(private.rest.len())};
        if count==0{return None;}self.private_source_revision(count+1)
    }
    pub(crate) fn clothes_component(&self) -> &str { &self.clothes_component }
    /// Authored private rig in skin (`bone_idx`) order and the body bone it
    /// hangs from. Exporters remap it onto the shared skeleton by name.
    pub(crate) fn private_rig(&self) -> Option<(&Skeleton, &str)> {
        self.source.smr_skel.as_ref().map(|rig| (rig, self.parent_key.as_str()))
    }
    pub(crate) fn skin_contract(&self) -> Result<Vec<u8>, bincode::Error> {
        let mut implanted: Vec<_> = self.implanted_bones.iter().copied().collect();
        let mut removed: Vec<_> = self.removed_accessory_bones.iter().copied().collect();
        implanted.sort_unstable(); removed.sort_unstable();
        let rig=self.source.smr_skel.as_ref().map(|rig|authored_rig_bytes(rig,&self.body_bridges)).transpose()?;
        bincode::serialize(&(rig, &self.parent_key, &self.local_placement,
            self.copy_weights_body, self.implants_active, implanted, &self.body_bridges,
            &self.clothes_component, self.coordinated, removed))
    }
    pub(crate) fn from_source(
        raw: &ExtractedMesh,
        part_skel: Option<&Skeleton>,
        parent_key: &str,
        locals: &[crate::pose::Mat4; 2],
        parent_world: &crate::pose::Mat4,
    ) -> Option<Self> {
        // A SkinnedMeshRenderer still evaluates its private bone palette when
        // it has no DynamicBone component. Falling back to a baked rigid mesh
        // loses the authored tangent basis and stretches scaled accessories.
        if let Some(skel) = crate::accessory_skeleton_with_moves(raw, part_skel, locals) {
            let mut source = raw.clone(); source.smr_skel = Some(skel);
            Self::from_placed_source(&source, part_skel, parent_key, parent_world, *parent_world, true)
        } else {
            Self::from_placed_source(raw, part_skel, parent_key, parent_world,
                crate::accessory_placement_matrix(raw, parent_world, part_skel, locals), true)
        }
    }

    pub(crate) fn from_assigned_accessory(raw:&ExtractedMesh,part_skel:Option<&Skeleton>,parent_key:&str,
        locals:&[crate::pose::Mat4;2],parent_world:&crate::pose::Mat4,armature_root:i64)->Option<Self> {
        let mut runtime=Self::from_source(raw,part_skel,parent_key,locals,parent_world)?;
        let private=runtime.source.smr_skel.as_ref()?;
        if !private.tfs.contains_key(&armature_root) {return None;}
        runtime.removed_accessory_bones=crate::pose::subtree_ids_pub(armature_root,&private.tfs).into_iter().collect();
        runtime.copy_weights_body=true;
        // ChaAccessoryClothes destroys the private Armature after assignment.
        runtime.source.deferred_dbones=Some(Default::default());runtime.state=None;
        Some(runtime)
    }

    pub(crate) fn from_hair_source(raw: &ExtractedMesh, part_skel: Option<&Skeleton>, parent_world: &crate::pose::Mat4) -> Option<Self> {
        // ChangeHair uses copyWeights=0: SetParent(HairParent, false), keeping
        // the prefab's local root and private bone hierarchy, including mods.
        Self::from_placed_source(raw, part_skel, "cf_J_FaceUp_ty", parent_world, *parent_world, true)
    }

    pub(crate) fn from_clothes_source(raw: &ExtractedMesh, part_skel: Option<&Skeleton>, body: &Skeleton,
        copy_dynamic: bool, component_name: &str, parent_world: Option<&crate::pose::Mat4>, implant_enabled: bool) -> Option<Self> {
        if !raw.uses_skin_renderer() || raw.weights.is_empty() { return None; }
        let mut rig = raw.smr_skel.as_ref().or(part_skel)?.clone();
        crate::dbone::implant::rename_prefab_root(&mut rig,component_name);
        let implants_active = implant_enabled && !raw.bone_implants.is_empty();
        let (implanted_bones,body_bridges)=if implants_active {
            crate::dbone::implant::prepare(&mut rig,body,&raw.bone_implants,parent_world)
        } else {Default::default()};
        let skel = &rig;
        let mut owned = raw.deferred_dbones.clone().unwrap_or_default()
            .private_clothes(skel, body, copy_dynamic, component_name);
        if implants_active {owned.add_implanted_colliders(&raw.bone_implants,&implanted_bones);}
        // AssignedWeightsAndSetBounds matches body Transform names, including
        // helpers outside the body's SMR palette. Keep the clothes' authored
        // slots/binds even when there are no private dynamics. Flattening these
        // slots onto the nearest SMR ancestor loses helper offsets (skirt bones).
        // (without the images from_placed_source drops: it copies this source again)
        let mut source = raw.clone(); drop_part_images(&mut source); source.deferred_dbones = Some(owned); source.bone_names=rig.names.clone(); source.smr_skel=Some(rig);
        let identity = crate::pose::ident();
        let mut runtime = Self::from_placed_source(&source, part_skel, "", &identity,
            if implants_active {identity} else {parent_world.copied().unwrap_or(identity)}, true)?;
        runtime.copy_weights_body = true;
        runtime.implants_active=implants_active; runtime.implanted_bones=implanted_bones; runtime.body_bridges=body_bridges;
        runtime.clothes_component=component_name.into();
        Some(runtime)
    }

    pub(crate) fn coordinate_clothes(&mut self) {
        if self.copy_weights_body && self.implants_active {
            self.coordinated=true; self.state=None;
            self.source.deferred_dbones=Some(Default::default());
        }
    }

    fn from_placed_source(raw: &ExtractedMesh, part_skel: Option<&Skeleton>, parent_key: &str,
        parent_world: &crate::pose::Mat4, placement: crate::pose::Mat4, allow_rigid: bool) -> Option<Self> {
        if !raw.uses_skin_renderer() {return None;}
        let mut source = raw.clone();
        let mut skel = source.smr_skel.clone().or_else(|| part_skel.cloned())?;
        let dbones = source.deferred_dbones.clone().unwrap_or_default().for_skeleton(&skel);
        if (!allow_rigid && dbones.is_empty()) || source.weights.is_empty() { return None; }
        if !source.bone_names.is_empty() && skel.names != source.bone_names { skel.set_bone_order(&source.bone_names); }
        let local_placement = crate::pose::mat_mul_pub(
            &crate::pose::mat_inv_pub(parent_world), &placement);
        // Warm up only after the live socket ancestry and character colliders
        // have been supplied by update_render, not under an approximate TRS.
        let mut state = crate::dbone::AttachedDynamicSet::new_unsettled(&dbones, &skel, &placement);
        if !dbones.is_empty() && state.is_none() { return None; }
        if let Some(state) = state.as_mut() { state.classify_chains(parent_key, &source.name); }
        drop_part_images(&mut source);
        source.smr_skel = Some(skel);
        source.deferred_dbones = Some(dbones);
        source.rest_positions = source.positions.clone();
        source.rest_normals = source.normals.clone();
        Some(Self { source, source_validation: Default::default(), parent_key: parent_key.into(), local_placement, copy_weights_body: false,
            implants_active:false,implanted_bones:Default::default(),body_bridges:Vec::new(),
            clothes_component:String::new(),coordinated:false,removed_accessory_bones:Default::default(),
            state, collider_flags: None })
    }

    pub(crate) fn update(&mut self, body: Option<&Skeleton>, head: Option<&Skeleton>, native: &crate::dbone::BodyColliderSet,
        flags: crate::dbone::game_colliders::Flags, dt: f32)
        -> Option<(Vec<[f32; 3]>, Vec<[f32; 3]>)>
    {
        self.update_render(body,head,native,flags,dt).map(|(p,n,_)|(p,n))
    }

    pub(crate) fn update_render(&mut self, body: Option<&Skeleton>, head: Option<&Skeleton>, native: &crate::dbone::BodyColliderSet,
        flags: crate::dbone::game_colliders::Flags, dt: f32)
        -> Option<(Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<[f32;4]>)>
    {
        self.update_render_impl(body, head, native, flags, dt, true, None,AccessoryFramePolicy::read(),None)
    }

    fn update_palette_pose(&mut self, body: Option<&Skeleton>, head: Option<&Skeleton>, native: &crate::dbone::BodyColliderSet,
        flags: crate::dbone::game_colliders::Flags, dt: f32)
    {
        let _ = self.update_render_impl(body, head, native, flags, dt, false, None,AccessoryFramePolicy::read(),None);
    }

    fn update_render_impl(&mut self, body: Option<&Skeleton>, head: Option<&Skeleton>, native: &crate::dbone::BodyColliderSet,
        flags: crate::dbone::game_colliders::Flags, dt: f32, emit_geometry: bool, prepared:Option<&crate::dbone::CharacterColliderFrame>,policy:AccessoryFramePolicy,assigned:Option<&AssignedWorldFrame>)
        -> Option<(Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<[f32;4]>)>
    {
        // Clothes are parented to objTop, outside the animated cf_j_root.
        let profiling=policy.profiling;
        let phase_start=std::time::Instant::now();
        let sync_trace=crate::native_profile::Scope::new(8);
        let parent = if self.copy_weights_body && self.parent_key.is_empty() { crate::pose::ident() }
            else { crate::bone_world(body, head, &self.parent_key)? };
        let placement = crate::pose::mat_mul_pub(&parent, &self.local_placement);
        // KKS_Colliders updates ordinary collider references/freeze axis in
        // place; it does not restart all hair/skirt particle chains. The live
        // state below rebinds its collider set for the new scene every frame.
        self.collider_flags = Some(flags);
        if let Some(body)=body {
            if let Some(skel)=&mut self.source.smr_skel {crate::dbone::implant::sync(skel,body,&self.body_bridges);}
            if let Some(state)=&mut self.state {crate::dbone::implant::sync(&mut state.skel,body,&self.body_bridges);}
        }
        let dbones = self.source.deferred_dbones.as_ref()?;
        // Gameplay bake policy: retain authored private bindings and animated
        // body assignment, but do not integrate clothing spring particles.
        if self.copy_weights_body && policy.rigid_clothes {
            self.state=None;
            if !emit_geometry {return None;}
            return self.skin_palette_with_policy(self.source.smr_skel.as_ref()?, &placement, body,policy,assigned);
        }
        let synced=phase_start.elapsed();
        drop(sync_trace);
        if dbones.is_empty() {
            if !emit_geometry { return None; }
            return self.skin_palette_with_policy(self.source.smr_skel.as_ref()?, &placement, body,policy,assigned);
        }
        let physics_trace=crate::native_profile::Scope::new(9);
        let attachment_chain=if self.copy_weights_body {None}
            else {crate::attachment_transform_chain(body,head,&self.parent_key,&self.local_placement)};
        // A state built from a placement alone (from_placed_source: hair under cf_J_FaceUp_ty, head-local hair) has no
        // attachment ancestry; once the live chain is known it could never be bound (length mismatch), every update
        // returned no geometry and the hair stayed in its local frame at the origin (card reader T-pose). Rebuild it
        // on the chain, as a first update would.
        if let (Some(state),Some(chain))=(&self.state,&attachment_chain) {if state.attachment_len()!=chain.len(){self.state=None;}}
        if self.state.is_none() {
            let skel = self.source.smr_skel.as_ref()?;
            self.state = if let Some(chain)=&attachment_chain {
                crate::dbone::AttachedDynamicSet::new_unsettled_with_chain(dbones,skel,chain)
            } else {crate::dbone::AttachedDynamicSet::new_unsettled(dbones, skel, &placement)};
            if let Some(state) = &mut self.state {
                state.set_mesh_radii(&self.source);
                state.classify_chains(&self.parent_key, &self.source.name);
                if let Some(frame)=prepared {state.configure_prepared_character_colliders(frame);}
                else if let Some(body) = body { state.configure_character_colliders(body, native, flags); }
                for _ in 0..240 { state.step(1.0 / 60.0); }
                if let Ok(path) = std::env::var("OK_PX_CHAINS") {
                    use std::io::Write;
                    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) { let _ = write!(f, "accessory {}\n{}", self.parent_key, state.describe_chains()); }
                }
            }
        } else if let Some(state) = &mut self.state {
            // The solver reads current Tf locals and publishes worlds at its
            // end. Collider rebinding below also reads Tf directly. Preserve
            // immediate publication for queries without a valid following step.
            let step_follows=dt.is_finite()&&dt>0.;
            if let Some(chain)=&attachment_chain {if !state.set_attachment_chain_for_step(chain,step_follows){return None;}}
            else if !state.set_placement_for_step(&placement,step_follows) { return None; }
        }
        if let Some(state) = &mut self.state {
            if let Some(frame)=prepared {state.configure_prepared_character_colliders(frame);}
            else if let Some(body) = body { state.configure_character_colliders(body, native, flags); }
            if dt > 0.0 {let _trace=crate::native_profile::Scope::new(10);state.step(dt);}
        }
        drop(physics_trace);
        if profiling {eprintln!("OK animation attachment name={} sync_ms={:.3} physics_ms={:.3}",self.source.name,synced.as_secs_f64()*1000.,(phase_start.elapsed()-synced).as_secs_f64()*1000.);}
        // Shared assets are skinned by Unreal from export_skin_palette. Keep
        // identical private rig/particle state, without cloning textures and
        // baking CPU positions that the shared player never consumes.
        if !emit_geometry { return None; }
        let state = self.state.as_ref()?;
        let correction = state.placement_correction();
        self.skin_palette_with_policy(&state.skel, &correction, body,policy,assigned)
    }

    /// `update_render_impl` for a renderer of a prefab another renderer already solved this frame (same socket, same
    /// simulated bones): the game runs the prefab's DynamicBones once for all its renderers. Places the rig as a step
    /// would, takes the leader's solved bone locals and skins. None in the outer option: not followable (no settled
    /// state, changed attachment), solve on its own instead.
    fn update_render_follow(&mut self, body: Option<&Skeleton>, head: Option<&Skeleton>, flags: crate::dbone::game_colliders::Flags,
        emit_geometry: bool, ids:&[i64], locals:&[([f32;3],[f32;4])], policy:AccessoryFramePolicy, assigned:Option<&AssignedWorldFrame>)
        -> Option<Option<(Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<[f32;4]>)>>
    {
        if self.state.is_none() || self.copy_weights_body {return None;}
        let parent = crate::bone_world(body, head, &self.parent_key)?;
        let placement = crate::pose::mat_mul_pub(&parent, &self.local_placement);
        self.collider_flags = Some(flags);
        if let Some(body)=body {
            if let Some(skel)=&mut self.source.smr_skel {crate::dbone::implant::sync(skel,body,&self.body_bridges);}
            if let Some(state)=&mut self.state {crate::dbone::implant::sync(&mut state.skel,body,&self.body_bridges);}
        }
        let attachment_chain=crate::attachment_transform_chain(body,head,&self.parent_key,&self.local_placement);
        let state=self.state.as_mut()?;
        if let Some(chain)=&attachment_chain {if state.attachment_len()!=chain.len()||!state.set_attachment_chain_for_step(chain,true){return None;}}
        else if !state.set_placement_for_step(&placement,true) {return None;}
        state.take_particle_locals(ids,locals);
        if !emit_geometry {return Some(None);}
        let state=self.state.as_ref()?;
        let correction=state.placement_correction();
        Some(self.skin_palette_with_policy(&state.skel,&correction,body,policy,assigned))
    }

    // Both the native renderer and independent animation exporter must use
    // precisely the same AssignedWeights/implant/destroyed-slot decisions.
    fn effective_worlds<'a>(&self, private: &'a Skeleton, correction: &crate::pose::Mat4, body: Option<&Skeleton>,parity:bool,assigned:Option<&AssignedWorldFrame>,body_names:Option<&HashMap<String,i64>>) -> std::borrow::Cow<'a,[crate::pose::Mat4]> {
        if self.copy_weights_body {
            let mut names=None;
            let mut cache=std::collections::HashMap::new();
            let mut max_error=0.0f32;
            let mut body_world=|name:&str| {
                let body=body?;
                let prepared=assigned.and_then(|frame|frame.worlds.get(name));
                let world=if let Some(world)=prepared {(*world)?} else {
                    let id=if let Some(index)=body_names{*index.get(name)?}else{
                        let index=names.get_or_insert_with(||crate::pose::rig_name_index(body,false));*index.get(name)?
                    };crate::pose::world_of(id,&body.tfs,&mut cache)
                };
                if parity&&prepared.is_some(){
                    let index=crate::pose::rig_name_index(body,false);
                    let reference=crate::pose::world_of(*index.get(name)?,&body.tfs,&mut cache);
                    assert_eq!(world,reference,"assigned frame differs from exact legacy world: {name}");
                }
                if parity {if let Some(reference)=crate::dbone::implant::world_exact(body,name) {for (a,b) in world.iter().flatten().zip(reference.iter().flatten()) {max_error=max_error.max((a-b).abs());}}}Some(world)
            };
            // Keep the original SMR slot order and bind poses. Distinct slots
            // may reference one shared bone but carry different bind matrices.
            let mut worlds = private.worlds.clone();
            for ((name, id), world) in private.names.iter().zip(&private.bone_ids).zip(&mut worlds) {
                *world = if self.implanted_bones.contains(id) {
                    if self.coordinated {
                        body_world(&crate::dbone::implant::rig::key(&self.clothes_component,*id))
                            .unwrap_or_else(||crate::pose::mat_mul_pub(correction,world))
                    } else {crate::pose::mat_mul_pub(correction,world)}
                }
                    else {body_world(name)
                    .unwrap_or_else(|| if self.implants_active || self.removed_accessory_bones.contains(id) || crate::dbone::DBoneSet::removed_clothes_transform(private, *id) {
                        // Game BakeMesh: a destroyed SMR bone slot contributes
                        // identity before its bind pose, not its former Transform.
                        crate::pose::ident()
                    } else { crate::pose::mat_mul_pub(correction, world) })};
            }
            if parity {eprintln!("OK animation world parity valid={} max_abs={max_error:.9}",i32::from(max_error<=1e-6));}
            std::borrow::Cow::Owned(worlds)
        } else {
            std::borrow::Cow::Borrowed(&private.worlds)
        }
    }

    /// Its DynamicBone state (the GPU rig exports the chains; None: no simulated bones).
    pub(crate) fn dynamic_state(&self) -> Option<&crate::dbone::AttachedDynamicSet> { self.state.as_ref() }
    /// The body bone each palette matrix follows, by name (export_skin_palette order), for an accessory skinned onto the
    /// body (copy_weights_body); None for one with its own rig. A coordinated implanted bone follows the clothes set's
    /// bone of that key (true), as effective_worlds reads it; any other implanted bone's name is its private one.
    pub(crate) fn body_palette_names(&self) -> Option<Vec<(String,bool)>> {
        if !self.copy_weights_body {return None;}
        let rigid_clothes=AccessoryFramePolicy::read().rigid_clothes;
        let private=match self.state.as_ref().filter(|_|!rigid_clothes){Some(state)=>&state.skel,None=>self.source.smr_skel.as_ref()?};
        Some(private.names.iter().zip(&private.bone_ids).map(|(name,id)|
            if self.implanted_bones.contains(id)&&self.coordinated {(crate::dbone::implant::rig::key(&self.clothes_component,*id),true)} else {(name.clone(),false)}).collect())
    }

    pub(crate) fn export_skin_palette(&self, body:Option<&Skeleton>, head:Option<&Skeleton>) -> Option<(&ExtractedMesh,Vec<crate::pose::Mat4>)> {
        let (mesh,mut matrices,correction)=self.export_skin_stages(body,head)?;
        // Staged consumers always receive an explicit identity fallback. The
        // legacy flattened ABI retains its original conditional fallback/count.
        matrices.pop();
        if !self.copy_weights_body {for skin in &mut matrices {*skin=crate::pose::mat_mul_pub(&correction,skin);}}
        if mesh.weights.iter().any(|w|w.iter().sum::<f32>()<=1e-5){
            matrices.push(if self.copy_weights_body{crate::pose::ident()}else{correction});
        }
        Some((mesh,matrices))
    }

    /// Native stream order: skin/normalize, then placement/rot_normal. Keeping
    /// the stages separate preserves collapsed-direction fallback semantics.
    pub(crate) fn export_skin_stages(&self, body:Option<&Skeleton>, head:Option<&Skeleton>) -> Option<(&ExtractedMesh,Vec<crate::pose::Mat4>,crate::pose::Mat4)> {
        self.export_skin_stages_for_frame(body,head,AccessoryFramePolicy::read())
    }
    pub(crate) fn export_skin_stages_for_frame(&self, body:Option<&Skeleton>, head:Option<&Skeleton>,policy:AccessoryFramePolicy) -> Option<(&ExtractedMesh,Vec<crate::pose::Mat4>,crate::pose::Mat4)> {
        let parent=match self.private_skin_parent_key(){Some(key)=>crate::bone_world(body,head,key)?,None=>crate::pose::ident()};
        self.export_skin_stages_for_parent(body,policy,parent)
    }

    pub(crate) fn private_skin_parent_key(&self)->Option<&str> {
        if self.copy_weights_body && self.parent_key.is_empty(){None}else{Some(&self.parent_key)}
    }

    /// Parent matrices are immutable within one read-only palette batch. The
    /// caller still resolves missing parents through the original bone lookup.
    pub(crate) fn export_skin_stages_for_parent(&self,body:Option<&Skeleton>,policy:AccessoryFramePolicy,parent:crate::pose::Mat4) -> Option<(&ExtractedMesh,Vec<crate::pose::Mat4>,crate::pose::Mat4)> {
        self.export_skin_stages_for_parent_indexed(body,policy,parent,None)
    }
    pub(crate) fn export_skin_stages_for_parent_indexed(&self,body:Option<&Skeleton>,policy:AccessoryFramePolicy,parent:crate::pose::Mat4,body_names:Option<&HashMap<String,i64>>) -> Option<(&ExtractedMesh,Vec<crate::pose::Mat4>,crate::pose::Mat4)> {
        let rigid_clothes=self.copy_weights_body && policy.rigid_clothes;
        if !rigid_clothes && self.state.is_none() && self.source.deferred_dbones.as_ref().is_some_and(|d|!d.is_empty()){return None;}
        let placement=crate::pose::mat_mul_pub(&parent,&self.local_placement);
        let (private,correction)=if let Some(state)=self.state.as_ref().filter(|_|!rigid_clothes) {(&state.skel,state.placement_correction())}
            else{(self.source.smr_skel.as_ref()?,placement)};
        let palette=self.effective_worlds(private,&correction,body,policy.parity,None,body_names);
        let count=if !self.source.bind_poses.is_empty(){self.source.bind_poses.len().min(palette.len())}
            else{palette.len().min(private.rest.len())};
        if count==0{return None;}
        let binds=if self.source.bind_poses.len()>=count{std::borrow::Cow::Borrowed(&self.source.bind_poses[..count])}
            else{std::borrow::Cow::Owned(crate::pose::binds_from_rest(&private.rest[..count]))};
        let mut matrices=Vec::with_capacity(count+1);
        matrices.extend(palette[..count].iter().zip(binds.iter()).map(|(world,bind)|crate::pose::mat_mul_pub(world,bind)));
        matrices.push(crate::pose::ident());
        Some((&self.source,matrices,if self.copy_weights_body{crate::pose::ident()}else{correction}))
    }

    /// Geometry-only consumption of the pose already solved by Animator / LateUpdate.
    /// No collider preparation, attachment rebind, or particle state mutation.
    fn render_solved(&self, body:Option<&Skeleton>,head:Option<&Skeleton>,policy:AccessoryFramePolicy,assigned:Option<&AssignedWorldFrame>)
        -> Option<(Vec<[f32;3]>,Vec<[f32;3]>,Vec<[f32;4]>)> {
        let rigid_clothes=self.copy_weights_body && policy.rigid_clothes;
        // Match export_skin_palette: an unsupported/uninitialized spring rig
        // must not silently fall back to a different authored palette.
        if !rigid_clothes&&self.state.is_none()&&self.source.deferred_dbones.as_ref().is_some_and(|d|!d.is_empty()){return None;}
        let (private,correction)=if let Some(state)=self.state.as_ref().filter(|_|!rigid_clothes){(&state.skel,state.placement_correction())}
            else{
                let parent=if self.copy_weights_body&&self.parent_key.is_empty(){crate::pose::ident()}
                    else{crate::bone_world(body,head,&self.parent_key)?};
                (self.source.smr_skel.as_ref()?,crate::pose::mat_mul_pub(&parent,&self.local_placement))
            };
        self.skin_palette_with_policy(private,&correction,body,policy,assigned)
    }

    #[cfg(test)]
    fn skin_palette(&self,private:&Skeleton,correction:&crate::pose::Mat4,body:Option<&Skeleton>)
        -> Option<(Vec<[f32;3]>,Vec<[f32;3]>,Vec<[f32;4]>)> {
        self.skin_palette_with_policy(private,correction,body,AccessoryFramePolicy::read(),None)
    }

    fn skin_palette_with_policy(&self, private: &Skeleton, correction: &crate::pose::Mat4, body: Option<&Skeleton>,policy:AccessoryFramePolicy,assigned:Option<&AssignedWorldFrame>)
        -> Option<(Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<[f32;4]>)> {
        let profiling=policy.profiling;let start=std::time::Instant::now();
        let prep_trace=crate::native_profile::Scope::new(11);
        let worlds=self.effective_worlds(private,correction,body,policy.parity,assigned,None);
        let count=if !self.source.bind_poses.is_empty(){self.source.bind_poses.len().min(worlds.len())}else{worlds.len().min(private.rest.len())};
        if count==0||self.source.weights.is_empty(){return None;}
        let binds=if self.source.bind_poses.len()>=count{self.source.bind_poses[..count].to_vec()}else{crate::pose::binds_from_rest(&private.rest[..count])};
        let prepared=start.elapsed();
        drop(prep_trace);
        // Optional skinning policies retain their original mutable path. Normal
        // native LBS reads immutable source buffers without cloning the mesh/rig.
        let optional=policy.optional_skin;
        let skin_trace=crate::native_profile::Scope::new(12);
        let (mut positions,mut normals,mut tangents)=if optional {
          let mut mesh = ExtractedMesh {
            name:self.source.name.clone(),positions:self.source.positions.clone(),
            normals:self.source.normals.clone(),rest_positions:self.source.rest_positions.clone(),
            rest_normals:self.source.rest_normals.clone(),weights:self.source.weights.clone(),
            bone_idx:self.source.bone_idx.clone(),bind_poses:self.source.bind_poses.clone(),
            indices:self.source.indices.clone(),..Default::default()
        };
          let mut palette=private.clone();palette.worlds=worlds.to_vec();
          if !crate::skin_mesh_geometry(&mut mesh,&palette){return None;}
          let tangents=crate::pose::skin_tangents(&self.source,&worlds[..count],&binds);(mesh.positions,mesh.normals,tangents)
        } else {
          let rest=if self.source.rest_normals.len()==self.source.normals.len(){&self.source.rest_normals}else{&self.source.normals};
          crate::pose::skin_streams(&self.source,rest,&worlds[..count],&binds)?
        };
        let skinned=start.elapsed();
        drop(skin_trace);
        let _correction_trace=crate::native_profile::Scope::new(13);
        if policy.parity {
            let mut palette=private.clone();palette.worlds=worlds.to_vec();
            let mut oracle=self.source.clone();let valid=crate::skin_mesh_geometry(&mut oracle,&palette)&&oracle.positions==positions&&oracle.normals==normals;
            assert!(valid,"borrowed native skin buffers differ from legacy geometry");
            eprintln!("OK animation skin parity valid={} vertices={}",i32::from(valid),positions.len());
        }
        if !self.copy_weights_body {
            crate::pose::apply_mat(&mut positions, correction);
            for normal in &mut normals { *normal = crate::pose::rot_normal(correction, *normal); }
        }
        if !self.copy_weights_body {for t in &mut tangents {
            let d=crate::pose::rot_normal(correction,[t[0],t[1],t[2]]);t[..3].copy_from_slice(&d);
        }}
        if profiling {eprintln!("OK animation skin name={} prep_ms={:.3} skin_ms={:.3} rest_ms={:.3}",self.source.name,prepared.as_secs_f64()*1000.,(skinned-prepared).as_secs_f64()*1000.,(start.elapsed()-skinned).as_secs_f64()*1000.);}
        Some((positions,normals,tangents))
    }

    fn reset(&mut self) { self.state = None; }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct ItemDrawMaterial {
    pub color_count: u8,
    pub colors: [[f32; 4]; 4],
    pub color_mask: Option<Vec<u8>>,
    pub color_mask_st: [f32; 4],
}

fn validate_part_origin(property:&str)->Result<(),String>{
    if property.is_empty()||property.len()>4096||property.contains('\0') {return Err("invalid part origin".into());}
    Ok(())
}

#[derive(Clone)]
#[allow(dead_code)]
pub struct SessionPart {
    /// Exact card property which emitted this renderer; names and mesh hashes are not slot identities.
    pub(crate) source_property: Option<String>,
    pub runtime_textures: std::collections::BTreeMap<String, [i32; 5]>,
    /// Resolved material data for source-program renderers (not role/card heuristics).
    pub source_material: Option<crate::unity::MeshMaterial>,
    pub shader_textures: std::collections::BTreeMap<String, Vec<u8>>,
    pub role: String,
    pub name: String,
    /// Source GameObject visibility; inactive targets retain their FBS data.
    pub render_visible: bool,
    /// Draw order among parts drawing the very same surface (0 first), from `assign_draw_layers`; not stored in
    /// snapshots: recomputed whenever a session is built or its parts change.
    pub draw_layer: u16,
    /// Head main_clothes_item keeps draw colours separate from MainTex.
    pub item_draw: Option<ItemDrawMaterial>,
    pub(crate) eye_draw: Option<crate::eye_gaze::EyeDraw>,
    pub rest_positions: Vec<[f32; 3]>,
    pub positions: Vec<[f32; 3]>,
    pub rest_normals: Vec<[f32; 3]>,
    pub rest_tangents: Vec<[f32; 4]>,
    pub normals: Vec<[f32; 3]>,
    pub uvs: Vec<[f32; 2]>,
    pub uv2: Vec<[f32; 2]>,
    pub uv3: Vec<[f32; 2]>,
    pub uv4: Vec<[f32; 2]>,
    pub vertex_colors: Vec<[u8; 4]>,
    pub overtex_png: Option<Vec<u8>>,
    pub overcolor: [f32; 4],
    pub overtex2_png: Option<Vec<u8>>,
    pub overcolor2: [f32; 4],
    pub nip_size: f32,
    pub nip_specular: f32,
    pub nip_toggle: f32,
    pub tex1_mask: f32,
    pub texcoord2_st: [f32; 4],
    pub texcoord3_st: [f32; 4],
    pub shadow_color: [f32; 4],
    pub line_color: [f32; 4],
    pub source_outline_enabled: bool,
    pub shadow_extend: f32,
    pub shadow_extend_another: f32,
    pub main_tex_st: [f32; 4],
    pub alpha_mask_st: [f32; 4],
    pub normal_map_st: [f32; 4],
    pub normal_detail_st: [f32; 4],
    pub normal_mask_st: [f32; 4],
    pub detail_mask_st: [f32; 4],
    pub line_mask_st: [f32; 4],
    pub normal_detail_scale: f32,
    pub face_normal: f32,
    pub face_shadow: f32,
    /// Koikano/main_skin chooses its line/diffuse tail with this material toggle.
    pub line_tex_on: f32,
    /// Koikano/main_skin Outline scales its extrusion by this material property.
    pub skin_line_width: f32,
    pub hair_gloss_st: [f32; 4],
    pub spec_height: f32,
    pub specular_power: f32,
    pub specular_power_nail: f32,
    pub specular_color: [f32; 4],
    pub no_tex_specular: f32,
    pub metallic: f32,
    pub smoothness: f32,
    /// 1 main_hair, 2 main_opaque, 3 main_clothes, 4 studio primitive,
    /// 5 main_skin, 6 IBL_Shader_alpha, 7 head skin, 8 glasses,
    /// 9 native MainItemPlus, 10 Koikano hair_main_sun; 0 other.
    pub shader_kind: u8,
    pub indices: Vec<u32>,
    pub weights: Vec<[f32; 4]>,
    pub bone_idx: Vec<[u16; 4]>,
    pub bind_poses: Vec<crate::pose::Mat4>,
    pub png: Option<Vec<u8>>,
    pub alpha_mask: Option<Vec<u8>>,
    pub hair_gloss: Option<Vec<u8>>,
    pub detail_mask: Option<Vec<u8>>,
    pub line_mask: Option<Vec<u8>>,
    pub normal_map: Option<Vec<u8>>,
    pub normal_detail: Option<Vec<u8>>,
    pub normal_mask: Option<Vec<u8>>,
    pub blend: String,
    pub unlit: bool,
    pub parent_bone: Option<String>,
    pub rigid_follow_bone: Option<String>,
    pub(crate) runtime_accessory: Option<AccessoryRuntime>,
    pub mouth_patterns: Vec<(i32, i32)>,
    pub eye_patterns: Vec<(i32, i32)>,
    pub eyebrow_patterns: Vec<(i32, i32)>,
    /// Weights already baked into the draw mesh, in percent. Runtime FBS
    /// subtracts this baseline once before adding the desired controller state.
    pub face_baseline_weights: Vec<f32>,
    pub face_default_rates: [f32; 2],
    pub face_runtime_settings: [f32; 12],
    pub(crate) iris_runtime: Option<crate::eye_gaze::IrisRuntime>,
    /// 0 none, 1 body, 2 head
    pub skel_kind: u8,
    pub role_c: std::ffi::CString,
    pub name_c: std::ffi::CString,
    pub blend_c: std::ffi::CString,
    pub parent_c: std::ffi::CString,
    pub rigid_follow_c: std::ffi::CString,
    pub blend_shapes: Vec<(std::ffi::CString, Vec<(u32, [f32; 3])>)>,
    pub morph_idx: Vec<Vec<u32>>,
    pub morph_dxyz: Vec<Vec<f32>>,
}

/// The albedo a studio-alpha material draws: its pass multiplies the sampled alpha by `_alpha`. Accessories with the
/// Shader Forge shader already have it baked into their texture (`color_accessory_meshes`); hair and xukmi's
/// "…StudioAlphaPlus" copies did not, so a 0.6 hair-shadow overlay drew as solid dark patches on the forehead.
fn studio_alpha_texture(role: &str, mesh: &ExtractedMesh) -> Option<Vec<u8>> {
    let png = mesh.texture_png.as_ref()?;
    let Some(mat) = mesh.mat.as_ref() else { return Some(png.clone()) };
    let baked = role.contains("acc") && mat.shader.to_ascii_lowercase().contains("main_item_studio_alpha");
    let alpha = mat.float("_alpha").unwrap_or(1.0);
    if baked || alpha >= 1.0 || !crate::shader_family(&mat.shader).contains("main_item_studio_alpha") { return Some(png.clone()); }
    crate::unity::scale_png_alpha(png, alpha.max(0.0)).or_else(|| Some(png.clone()))
}

impl SessionPart {
    /// Native face overlays are already customized in source_material. Legacy
    /// body overlay fields retain neutral defaults on old face snapshots.
    /// Resolve at the view boundary so existing portable cards are corrected too.
    pub(crate) fn draw_overlay_color(&self, slot:usize)->[f32;4] {
        let (key,fallback)=if slot==0{("_overcolor1",self.overcolor)}else{("_overcolor2",self.overcolor2)};
        if self.role=="head" && matches!(self.shader_kind,5|7) {
            self.source_material.as_ref().and_then(|m|m.color(key)).unwrap_or(fallback)
        }else{fallback}
    }
    pub fn from_mesh(role: &str, mesh: &ExtractedMesh, blend: &str, unlit: bool) -> Self {
        let rest = if mesh.rest_positions.len() == mesh.positions.len() {
            mesh.rest_positions.clone()
        } else {
            mesh.positions.clone()
        };
        let rest_n = if mesh.rest_normals.len() == mesh.normals.len() {
            mesh.rest_normals.clone()
        } else {
            mesh.normals.clone()
        };
        let is_forge_opaque = mesh.mat.as_ref().is_some_and(|m|
            m.shader.to_ascii_lowercase().contains("main_opaque")
                || (m.shader.is_empty() && role.starts_with("clothes")
                    && m.slot("_Texture3").is_some()));
        let hair_shader_name = mesh.mat.as_ref().map(|m| m.shader.to_ascii_lowercase()).unwrap_or_default();
        let is_hair_shader = crate::is_native_hair_shader(&hair_shader_name) || mesh.hair_shader_hint;
        let is_hair_front = hair_shader_name.contains("main_hair_front") || hair_shader_name=="koikano/hair_main_sun_front";
        // Studio items (Shader Forge/main_item_studio and _alpha) mix their three colours, and the Studio patterns,
        // through the ColorMask like the game. The light effects (_add) have no ColorMask: texture times colour, added.
        let is_studio_item = hair_shader_name.contains("main_item_studio") && !hair_shader_name.contains("_add");
        Self {
            source_property: None,
            draw_layer: 0,
            runtime_textures: mesh.runtime_textures.clone(),
            source_material: {
                let mut material=mesh.mat.clone();
                if let Some(mat)=material.as_mut().filter(|m|m.shader.eq_ignore_ascii_case("Shader Forge/main_alpha")) {
                    // Proven alpha=1 permits the native SrcAlpha pass to use
                    // UE's depth-writing opaque path without changing blending.
                    // A pure function of the two textures and the two switches, decoded for every open of every
                    // outfit before (~0.2–0.3 s a card conversion): kept by content (png_transform_cache).
                    let (use_a,use_b)=(mat.float("_alpha_a").unwrap_or(1.)!=0.,mat.float("_alpha_b").unwrap_or(1.)!=0.);
                    let opaque=mesh.texture_png.as_ref().is_some_and(|main_png|{
                        let mut parameters=vec![use_a as u8,use_b as u8,mesh.alpha_mask_png.is_some() as u8];
                        parameters.extend_from_slice(mesh.alpha_mask_png.as_deref().unwrap_or_default());
                        crate::png_transform_cache::transform("native_alpha_opaque_v1",main_png,&parameters,||{
                            let main=crate::teximg::load_from_memory(main_png).ok();
                            let mask=mesh.alpha_mask_png.as_ref().and_then(|p|crate::teximg::load_from_memory(p).ok());
                            let opaque=(mesh.alpha_mask_png.is_none() || mask.is_some()) && main.is_some_and(|p|p.to_rgba8().pixels().all(|v|v[3]==255)) &&
                                mask.is_none_or(|p|p.to_rgba8().pixels().all(|v|(!use_a || v[0]==255) && (!use_b || v[1]==255)));
                            Some(vec![opaque as u8])
                        }).is_some_and(|v|v==[1])
                    });
                    mat.floats.insert("_OKNativeAlphaOpaque".into(),if opaque {1.}else{0.});
                }
                material
            },
            shader_textures: {
                let mut textures=mesh.shader_extra_textures.clone();
                if mesh.mat.as_ref().is_some_and(|m|matches!(m.shader.to_ascii_lowercase().as_str(),"xukmi/mainitemplus"|"shader forge/main_item"|"shader forge/main_alpha"|"shader forge/main_opaque")) {
                    for (name,png) in [("_MainTex",&mesh.texture_png),("_ColorMask",&mesh.color_mask_png),
                        ("_AlphaMask",&mesh.alpha_mask_png),("_DetailMask",&mesh.detail_mask_png),
                        ("_LineMask",&mesh.line_mask_png),("_NormalMap",&mesh.normal_map_png)] {
                        if let Some(png)=png {textures.insert(name.into(),png.clone());}
                    }
                    // Exact MainOpaque has a white ShaderLab MainTex default.
                    // Keep its native path selected even for a null binding.
                    if mesh.mat.as_ref().is_some_and(|m|m.shader.eq_ignore_ascii_case("Shader Forge/main_opaque"))
                        && !textures.contains_key("_MainTex") {
                        if let Some(png)=crate::unity::solid_png([1.;4],1,1) {textures.insert("_MainTex".into(),png);}
                    }
                } else if mesh.mat.as_ref().is_some_and(|m| matches!(m.shader.to_ascii_lowercase().as_str(),
                    "koikano/main_clothes_item_glasses" | "shader forge/toon_glasses_lod0")) {
                    // The exact draw consumes native texture / Color4 separately.
                    textures.retain(|name, _| matches!(name.as_str(), "_MainTex" | "_GlassRamp" | "_NormalMap"));
                } else if mesh.mat.as_ref().is_some_and(|m| m.shader == "Shader Forge/main_StandardMDK_studio") {
                    // MDK reads emission regardless of Standard's _EMISSION
                    // keyword, and its normal slot is _BumpMap.
                    textures.retain(|name, _| matches!(name.as_str(), "_BumpMap" | "_ParallaxMap" | "_MetallicGlossMap" | "_EmissionMap" | "_OKLightmap" | "_OKLightmapDirection" | "_OKReflection"));
                } else if is_hair_shader {
                    textures.retain(|name, _| name == "_AnotherRamp");
                } else if role == "head" && matches!(hair_shader_name.as_str(), "koikano/main_skin" | "shader forge/main_skin" | "xukmi/skinplus") {
                    textures.retain(|name, _| matches!(name.as_str(), "_overtex1" | "_overtex2" | "_overtex3"));
                } else {
                    // Unity Standard-family emission (keyword _EMISSION): the map is kept, the renderer adds _EmissionColor x map.
                    let emits=mesh.mat.as_ref().is_some_and(|m|m.keywords.iter().any(|k|k=="_EMISSION"));
                    // A baked lightmap (`_OKLightmap`, okmodel_view::bake_lightmap) is kept for any shader.
                    // An eye set's pupil expression (`_expression`, eye_set::expression_png) too.
                    // A Studio map stage effect (`_OKStageEffect`, unity::stage_effect_kind) keeps its colour and mask textures.
                    // A Standard material's metallic map (keyword _METALLICGLOSSMAP: metallic and smoothness of its lightmap
                    // and reflection terms) and its baked reflection probe atlas (`_OKReflection`,
                    // okmodel_view::assign_reflection_probes). A sky dome's cube atlas (`_OKSkyCube`, unity: Skybox/Cubemap).
                    let stage=mesh.mat.as_ref().is_some_and(|m|m.floats.contains_key("_OKStageEffect"));
                    let metal_map=mesh.mat.as_ref().is_some_and(|m|m.keywords.iter().any(|k|k=="_METALLICGLOSSMAP"));
                    let standard_normal=mesh.mat.as_ref().is_some_and(|m|m.shader.starts_with("Standard")&&m.keywords.iter().any(|k|k=="_NORMALMAP"));
                    textures.retain(|name,_|(standard_normal && name=="_BumpMap") || (emits && name=="_EmissionMap") || (metal_map && name=="_MetallicGlossMap") || name=="_OKLightmap" || name=="_OKLightmapDirection" || name=="_OKReflection" || name=="_OKSkyCube" || name=="_expression"
                        || (stage && matches!(name.as_str(),"_color"|"_mask"|"_Emission"|"_MaskFast"|"_MaskSlow"|"_MaskFull")));
                }
                textures
            },
            role: role.to_string(),
            name: mesh.name.clone(),
            render_visible: true,
            eye_draw: None,
            item_draw: (role == "head" && crate::head_uses_item_draw(mesh)
                || is_hair_shader || is_studio_item).then(|| ItemDrawMaterial {
                color_count: if mesh.mat.as_ref().is_some_and(|m| m.shader.eq_ignore_ascii_case("Shader Forge/main_item"))
                    || is_hair_shader || is_studio_item {3}else{4},
                colors: std::array::from_fn(|i| {
                    let key = if i == 0 { "_Color".into() } else { format!("_Color{}", i + 1) };
                    let default = if is_hair_shader && i == 1 { [0.7843137, 0.7843137, 0.7843137, 1.0] }
                        else if is_hair_shader && i == 2 { [0.5, 0.5, 0.5, 1.0] }
                        else { [1.0; 4] };
                    mesh.mat.as_ref().and_then(|m| m.color(&key)).unwrap_or(default)
                }),
                color_mask: mesh.color_mask_png.clone(),
                color_mask_st: {
                    let keys = ["_ColorMask_ST_x", "_ColorMask_ST_y", "_ColorMask_ST_z", "_ColorMask_ST_w"];
                    let defaults = [1.0, 1.0, 0.0, 0.0];
                    std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
                },
            }),
            rest_positions: rest,
            positions: mesh.positions.clone(),
            rest_normals: rest_n,
            rest_tangents: mesh.tangents.clone(),
            normals: mesh.normals.clone(),
            uvs: if mesh.mat.as_ref().is_some_and(|m| crate::shader_samples_flipped_v(&m.shader)) { mesh.uvs.iter().map(|uv| [uv[0], 1.0 - uv[1]]).collect() }
                else { mesh.uvs.clone() },
            uv2: mesh.uv2.clone(),
            uv3: mesh.uv3.clone(),
            uv4: mesh.uv4.clone(),
            vertex_colors: mesh.vertex_colors.clone(),
            overtex_png: None,
            overcolor: [1.0, 1.0, 1.0, 0.0],
            overtex2_png: None,
            overcolor2: [0.0, 0.0, 0.0, 0.0],
            nip_size: mesh.mat.as_ref().and_then(|m| m.float("_nipsize")).unwrap_or(0.5),
            nip_specular: mesh.mat.as_ref().and_then(|m| m.float("_nip_specular")).unwrap_or(0.5),
            nip_toggle: mesh.mat.as_ref().and_then(|m| m.float("_nip")).unwrap_or(0.0),
            tex1_mask: mesh.mat.as_ref().and_then(|m| m.float("_tex1mask")).unwrap_or(0.0),
            texcoord2_st: {
                let k = ["_texcoord2_ST_x", "_texcoord2_ST_y", "_texcoord2_ST_z", "_texcoord2_ST_w"];
                let d = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(k[i])).unwrap_or(d[i]))
            },
            texcoord3_st: {
                let k = ["_texcoord3_ST_x", "_texcoord3_ST_y", "_texcoord3_ST_z", "_texcoord3_ST_w"];
                let d = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(k[i])).unwrap_or(d[i]))
            },
            // Unity's Unlit/* and the CM3D2 ports of mod maps (lib::blend_for_mesh draws them unlit) have no toon shadow
            // side: white keeps the ramp from darkening them (CM3D2/Lighted's _ShadowColor is near black).
            shadow_color: if mesh.mat.as_ref().is_some_and(|m| { let s = m.shader.to_ascii_lowercase(); s.starts_with("unlit/") || s.starts_with("cm3d2/") }) { [1.0; 4] }
                else { mesh.mat.as_ref().and_then(|m| m.color("_ShadowColor")).unwrap_or([0.628, 0.628, 0.628, 1.0]) },
            line_color: mesh.mat.as_ref().and_then(|m| m.color("_LineColor")
                .or_else(|| {
                    // MaterialEditor.LoadShader exposes the replacement
                    // shader's property defaults even when the old material
                    // did not serialize that property. Both Forge hair shaders
                    // declare _LineColor=(0.5,0.5,0.5,1) in EC_Shaders.
                    m.shader.to_ascii_lowercase().starts_with("shader forge/main_hair")
                        .then_some([0.5, 0.5, 0.5, 1.0])
                }))
                .unwrap_or([0.0, 0.0, 0.0, 1.0]),
            shadow_extend: mesh.mat.as_ref().and_then(|m| m.float("_ShadowExtend"))
                .unwrap_or(if is_hair_shader { 0.5 } else { 1.0 }),
            source_outline_enabled: mesh.mat.as_ref().is_none_or(|m|
                if m.shader.eq_ignore_ascii_case("xukmi/MainItemPlus") {m.float("_OutlineOn").unwrap_or(1.)!=0.}
                else {m.keywords.iter().any(|k|k.eq_ignore_ascii_case("_OUTLINE_ON"))}),
            shadow_extend_another: mesh.mat.as_ref().and_then(|m| m.float("_ShadowExtendAnother")).unwrap_or(0.0),
            main_tex_st: {
                let keys = ["_MainTex_ST_x", "_MainTex_ST_y", "_MainTex_ST_z", "_MainTex_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            alpha_mask_st: {
                let keys = ["_AlphaMask_ST_x", "_AlphaMask_ST_y", "_AlphaMask_ST_z", "_AlphaMask_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            normal_map_st: {
                let keys = ["_NormalMap_ST_x", "_NormalMap_ST_y", "_NormalMap_ST_z", "_NormalMap_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            normal_detail_st: {
                let keys = ["_NormalMapDetail_ST_x", "_NormalMapDetail_ST_y", "_NormalMapDetail_ST_z", "_NormalMapDetail_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            normal_mask_st: {
                let keys = ["_NormalMask_ST_x", "_NormalMask_ST_y", "_NormalMask_ST_z", "_NormalMask_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            detail_mask_st: {
                let keys = ["_DetailMask_ST_x", "_DetailMask_ST_y", "_DetailMask_ST_z", "_DetailMask_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            line_mask_st: {
                let keys = ["_LineMask_ST_x", "_LineMask_ST_y", "_LineMask_ST_z", "_LineMask_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(keys[i])).unwrap_or(defaults[i]))
            },
            normal_detail_scale: mesh.mat.as_ref().and_then(|m| m.float("_DetailNormalMapScale")).unwrap_or(0.0),
            face_normal: mesh.mat.as_ref().and_then(|m| m.float("_FaceNormalG")).unwrap_or(0.0),
            face_shadow: mesh.mat.as_ref().and_then(|m| m.float("_FaceShadowG")).unwrap_or(0.0),
            line_tex_on: mesh.mat.as_ref().and_then(|m| m.float("_linetexon")).unwrap_or(1.0),
            skin_line_width: mesh.mat.as_ref().and_then(|m| m.float("_LineWidthS")).unwrap_or(1.0),
            hair_gloss_st: {
                let st = ["_HairGloss_ST_x", "_HairGloss_ST_y", "_HairGloss_ST_z", "_HairGloss_ST_w"];
                let defaults = [1.0, 1.0, 0.0, 0.0];
                std::array::from_fn(|i| mesh.mat.as_ref().and_then(|m| m.float(st[i])).unwrap_or(defaults[i]))
            },
            spec_height: mesh.mat.as_ref().and_then(|m| m.float("_SpeclarHeight"))
                .unwrap_or(if is_hair_front { 0.985 } else if is_hair_shader { 0.85 } else { 0.95 }),
            specular_power: mesh.mat.as_ref().and_then(|m| m.float("_SpecularPower")).unwrap_or(0.0),
            specular_power_nail: mesh.mat.as_ref().and_then(|m| m.float("_SpecularPowerNail")).unwrap_or(0.0),
            specular_color: mesh.mat.as_ref().and_then(|m| m.color("_SpecularColor"))
                .unwrap_or([1.0, 1.0, 1.0, 0.0]),
            no_tex_specular: mesh.mat.as_ref().and_then(|m| m.float("_notusetexspecular")).unwrap_or(0.0),
            metallic: mesh.mat.as_ref().and_then(|m| m.float("_Metallic")).unwrap_or(0.0),
            smoothness: mesh.mat.as_ref().and_then(|m| m.float("_Glossiness")).unwrap_or(0.5),
            shader_kind: {
                let shader = mesh.mat.as_ref().map(|m| m.shader.to_ascii_lowercase()).unwrap_or_default();
                let mat_name = mesh.mat.as_ref().map(|m| m.name.to_ascii_lowercase()).unwrap_or_default();
                if shader == "xukmi/mainitemplus" { 9 }
                // A resolved shader overrides renderer-role and inherited hair
                // hints. Forge MainItem has one Forward program for all roles.
                else if shader == "shader forge/main_item" { 8 }
                else if shader == "shader forge/main_alpha" { 11 }
                else if shader == "shader forge/main_opaque" { 2 }
                else if crate::shader_family(mesh.mat.as_ref().map_or("", |m| m.shader.as_str())).contains("main_item_studio") { 4 }
                else if shader == "ibl_shader_alpha" { 6 }
                else if matches!(shader.as_str(),"koikano/hair_main_sun"|"koikano/hair_main_sun_front") { 10 }
                else if crate::is_native_hair_shader(&shader) || mesh.hair_shader_hint { 1 }
                // The extracted Forge ForwardBase pass is Cull Off, while
                // Koikano/main_skin is Cull Back. Keep the shader families
                // distinct so the renderer can reproduce their pass state.
                // External shader PPtrs in head mods can leave the shader
                // name unresolved. Both families may retain Forge rim
                // properties, but Koikano's face also has _FaceNormalG.
                else if shader == "shader forge/main_skin"
                    || (shader.is_empty()
                        && role == "head" && mesh.name.eq_ignore_ascii_case("cf_O_face")
                        && mesh.mat.as_ref().is_some_and(|m|
                            m.float("_rimV").is_some() && m.float("_rimpower").is_some()
                            && m.float("_FaceNormalG").is_none())) { 7 }
                // The body shader may be an external PPtr in the base bundle,
                // leaving its resolved name empty; the body role still names
                // Koikano/main_skin's renderer in the KKS prefab.
                else if shader.contains("main_skin")
                    || (shader.is_empty() && role == "body")
                    || (role == "head" && mesh.name.eq_ignore_ascii_case("cf_O_face")
                        && mat_name.starts_with("cf_m_face_00")) { 5 }
                else if is_forge_opaque { 2 }
                else if role == "head" && crate::head_uses_item_draw(mesh) { 3 }
                else if shader.contains("main_clothes") || (shader.is_empty()
                    && role.starts_with("clothes")
                    && mesh.mat.as_ref().is_some_and(|m| m.slot("_Texture3").is_none())
                    && mesh.mat.as_ref().is_some_and(|m|
                        m.keywords.iter().any(|k| k.starts_with("_COLORMASKUSE_"))
                        || ((mesh.detail_mask_png.is_some() || mesh.normal_map_png.is_some())
                            && (m.slot("_DetailMask").is_some() || m.slot("_NormalMap").is_some())))) { 3 }
                else if mat_name.starts_with("mf_m_primmaterial") { 4 }
                else { 0 }
            },
            indices: mesh.indices.clone(),
            weights: mesh.weights.clone(),
            bone_idx: mesh.bone_idx.clone(),
            bind_poses: mesh.bind_poses.clone(),
            png: studio_alpha_texture(role, mesh),
            alpha_mask: mesh.alpha_mask_png.clone(),
            hair_gloss: mesh.hair_gloss_png.clone(),
            detail_mask: mesh.detail_mask_png.clone(),
            line_mask: mesh.line_mask_png.clone(),
            // Forge opaque now uses the Maker camera light; the three-card
            // render check passed with its UV-derived tangent normals.
            // Koikano clothes still use an incomplete light path, so retain
            // the authored-tangent gate there until that family is aligned.
            normal_map: if mesh.source_tangents == Some(false) && !is_forge_opaque {
                None
            } else {
                mesh.normal_map_png.clone()
            },
            normal_detail: None,
            normal_mask: mesh.normal_mask_png.clone(),
            blend: blend.to_string(),
            unlit,
            parent_bone: mesh.parent_bone.clone(),
            rigid_follow_bone: mesh.rigid_follow_bone.clone(),
            runtime_accessory: None,
            mouth_patterns: Vec::new(),
            eye_patterns: Vec::new(),
            eyebrow_patterns: Vec::new(),
            face_baseline_weights: Vec::new(),
            face_default_rates: [0.92, 1.0],
            face_runtime_settings: [30.,0.15,1.,1.,0.,0.,-0.1,-0.1,0.1,0.3,0.1,0.],
            iris_runtime: None,
            skel_kind: mesh.skel_kind,
            role_c: std::ffi::CString::new(role).unwrap_or_default(),
            name_c: std::ffi::CString::new(mesh.name.as_str()).unwrap_or_default(),
            blend_c: std::ffi::CString::new(blend).unwrap_or_default(),
            parent_c: std::ffi::CString::new(mesh.parent_bone.as_deref().unwrap_or(""))
                .unwrap_or_default(),
            rigid_follow_c: std::ffi::CString::new(mesh.rigid_follow_bone.as_deref().unwrap_or(""))
                .unwrap_or_default(),
            blend_shapes: {
                mesh.blend_shapes
                    .iter()
                    .map(|(n, d)| {
                        (
                            std::ffi::CString::new(n.as_str()).unwrap_or_default(),
                            d.clone(),
                        )
                    })
                    .collect()
            },
            morph_idx: mesh
                .blend_shapes
                .iter()
                .map(|(_, d)| d.iter().map(|(i, _)| *i).collect())
                .collect(),
            morph_dxyz: mesh
                .blend_shapes
                .iter()
                .map(|(_, d)| {
                    let mut f = Vec::with_capacity(d.len() * 3);
                    for (_, xyz) in d {
                        f.extend_from_slice(xyz);
                    }
                    f
                })
                .collect(),
        }
    }
}

pub struct OkSession {
    pub(crate) accessory_context:Option<std::sync::Arc<crate::accessory_extract::Context>>,
    pub editor_pregnancy:crate::pregnancy::Runtime,
    pub(crate) pv_config:Option<crate::pv::Config>,
    pub(crate) pv_mask:u8,
    pub(crate) pv_fk:bool,
    pub(crate) eye_look_config:Option<crate::eye_look::Config>,
    pub(crate) eye_look_runtime:Option<crate::eye_look::Runtime>,
    pub(crate) head_look:crate::head_look::HeadLook,
    /// PhysX secondary dynamics (OKPhysXSecondary) for this character (`ok_session_set_physx_secondary`).
    /// On for the gallery physics menu, one-to-one interaction and paired playback; crowd and street NPCs stay off
    /// and keep the built-in DynamicBone solver.
    pub(crate) physx_secondary:bool,
    /// Whether an Unreal animation drives this body (`ok_session_set_external_animation`); None follows OK_UE_ANIM.
    /// The process environment is shared: another actor loading (a clothing warm-up) set it and this character's
    /// game animation stopped sampling and fell back to the T-pose.
    pub(crate) external_animation:Option<bool>,
    /// Parts an owner draws on the GPU from their skin palettes (OK Studio): the frame's hair/cloth pass still solves
    /// them but does not skin their vertices on the CPU (`ok_session_set_scene_gpu_parts`, sorted).
    pub(crate) scene_gpu_parts:Vec<usize>,
    /// The card's per-component solvers (`okPh`); read from the card at open, not cached.
    pub(crate) physics:crate::okphysics::CardPhysics,
    pub(crate) body_interaction:Option<crate::hit_reaction::Interaction>,
    pub game_root: PathBuf,
    pub ramp_png: Option<Vec<u8>>,
    /// Effective Studio `_ambientshadowG.a`, computed by
    /// SystemButtonCtrl.SetAmbientShadow as 1 - sceneInfo.ambientShadowG.
    /// The field retains its FFI name for compatibility.
    pub shadow_depth: f32,
    /// Character.UpdateGlobalShader sets `_linewidthG` from GraphicData.lineWidth.
    pub line_width: f32,
    /// `_LineColorG.a = 1 - GraphicData.lineDepth` in ChaShader.
    pub line_depth: f32,
    /// ChaShader.ChangeShaderType sets _KanoVerShift from GraphicData.shaderType.
    pub kano_ver_shift: f32,
    pub body_rest: Option<Skeleton>,
    pub body_dynamics: crate::dbone::ver02::BodyDynamicSet,
    pub body_dynamic_runtime: Option<crate::dbone::ver02::BodyDynamicRuntime>,
    pub(crate) yure_policy: Option<crate::anim::yure::Policy>,
    pub(crate) body_colliders: crate::dbone::BodyColliderSet,
    pub(crate) clothes_dynamics: crate::dbone::DBoneSet,
    pub(crate) clothes_dynamic_runtime: Option<(crate::dbone::game_colliders::Flags, crate::dbone::AttachedDynamicSet)>,
    pub bust_softness: f32,
    pub bust_weight: f32,
    pub collider_settings: crate::dbone::game_colliders::Settings,
    pub collider_overrides: crate::dbone::game_colliders::CardOverrides,
    pub head_rest: Option<Skeleton>,
    pub body_skel: Option<Skeleton>,
    pub head_skel: Option<Skeleton>,
    pub body_keys: HashMap<String, Vec<Keyframe>>,
    pub head_keys: HashMap<String, Vec<Keyframe>>,
    pub(crate) hand_shapes: crate::shape::hand::HandShapes,
    /// Studio FK: absolute local rotations (Unity Euler degrees, as KK Studio stores them) that replace the animated
    /// rotation of these bones of the body Animator tree after sampling.
    pub(crate) user_fk: Vec<(String, [f32; 3])>,
    pub(crate) user_fk_ids: BoneIdCache,
    pub(crate) hand_shape_ids: BoneIdCache,
    /// Studio IK: FullBodyBipedIK (the game's body-bone setup) and its position targets (effector index 0 body, 1/2
    /// shoulders, 3/4 thighs, 5/6 hands, 7/8 feet; skeleton space). Solved after FK.
    pub(crate) studio_ik_solver: Option<crate::finalik::IKSolverFullBodyBiped>,
    pub(crate) studio_ik: Vec<(usize, [f32; 3])>,
    pub(crate) studio_ik_rotations: Vec<(usize, [f32;4])>,
    pub correct: Vec<CorrectInfo>,
    pub sex: i32,
    pub type_bone: i32,
    pub nip_stand_rate: f32,
    pub shape_body: Vec<f32>,
    pub shape_face: Vec<f32>,
    pub abm: Vec<crate::abmx::AbmBone>,
    pub parts: Vec<SessionPart>,
    /// Number of outfit coordinate slots in the card (>= 1).
    pub coordinate_count: usize,
    pub skel_export: Option<crate::skel_export::SkelExport>,
    pub anim_time: f32,
    /// Live solver state is per role; never persisted in a card/snapshot.
    pub(crate) paired_ik_runtime: Option<crate::h_playback::PairIkRuntime>,
    /// Runtime clip selection is per session and deliberately not serialized.
    pub anim_clip: String,
    pub(crate) anim_parameters: HashMap<String,f32>,
    pub(crate) anim_clock: crate::anim::crossfade::Clock,
    pub(crate) anim_crossfade: Option<crate::anim::crossfade::CrossFade>,
    pub(crate) anim_layer_crossfades: std::collections::BTreeMap<usize,crate::anim::crossfade::CrossFade>,
    /// 0 = source default; 1 = Maker; 2 = normal gameplay; 3 = H/esthetic; 4 = Studio.
    pub anim_scene_mode: i32,
    pub body_shaped: Option<Skeleton>,
    // Runtime-only allocation reuse; restored from the complete current shaped
    // skeleton before ABMX/reference evaluation. Never serialized or history.
    pub(crate) shape_reference_scratch: Option<Skeleton>,
    pub head_shaped: Option<Skeleton>,
    /// Full head tree (`cf_J_hairF_top` and the joints the 45-bone face SMR drops).
    /// Rest is unshaped; posed is after head shape. GPU hair binds against rest.
    pub head_full_rest: Option<Skeleton>,
    pub head_full_posed: Option<Skeleton>,
}

pub(crate) struct PreviewPartReplacement {
    pub snapshot:crate::portable_resources::SplitSnapshot,
    pub retained_indices:Vec<Option<usize>>,
    pub added_range:std::ops::Range<usize>,
}

#[derive(Clone)]
pub(crate) struct HeadTextureDeltaPart {
    pub index:usize, pub name:String, pub shader_kind:u8,
    pub png:Vec<u8>, pub runtime_textures:std::collections::BTreeMap<String,[i32;5]>,
    pub eye_textures:Option<[Option<Vec<u8>>;2]>,
}
pub(crate) struct HeadTextureDelta {
    pub part_count:usize, pub shape_body:Vec<f32>, pub shape_face:Vec<f32>,
    pub parts:Vec<HeadTextureDeltaPart>,
}

impl OkSession {
    /// Publish a validated exact-slot candidate without replacing retained
    /// parts or their private dynamics. UE must stage its matching components
    /// and validate this transaction before performing the paired publication.
    pub(crate) fn replace_accessory_parts(&mut self,outfit:usize,slot:usize,
        retained:&[Option<usize>],added:Vec<SessionPart>,exports:crate::skel_export::PartExports)
        ->Result<(),String> {
        let next=self.validate_accessory_replacement(outfit,slot,retained,&added,&exports)?;
        let export=self.skel_export.as_mut().ok_or("accessory publication requires a prepared live rig")?;
        export.replace_part_exports(retained,exports)?;
        let mut parts=Vec::with_capacity(next+added.len());
        for(part,mapped)in std::mem::take(&mut self.parts).into_iter().zip(retained) {
            if mapped.is_some(){parts.push(part);}
        }
        parts.extend(added);crate::assign_draw_layers(&mut parts);self.parts=parts;
        Ok(())
    }
    /// All publication checks without taking ownership, so a C ABI candidate
    /// survives every foreseeable rejection. Returns the retained part count.
    pub(crate) fn validate_accessory_replacement(&self,outfit:usize,slot:usize,
        retained:&[Option<usize>],added:&[SessionPart],exports:&crate::skel_export::PartExports)
        ->Result<usize,String> {
        if outfit>=self.coordinate_count || retained.len()!=self.parts.len()
            || !exports.valid_count(added.len()) {
            return Err("accessory publication counts/outfit mismatch".into());
        }
        let property=format!("outfit{outfit}.accessory{slot}");
        let mut next=0;
        for(part,mapped)in self.parts.iter().zip(retained) {
            if part.role=="accessory" && part.source_property.as_deref()
                .and_then(crate::accessory_slot_from_property).is_none() {
                return Err("accessory publication requires exact legacy provenance upgrade".into());
            }
            let remove=part.role=="accessory" && part.source_property.as_deref()==Some(property.as_str());
            if remove {
                if mapped.is_some(){return Err("accessory publication retained a replaced slot".into());}
            } else {
                if *mapped!=Some(next){return Err("accessory publication removed or reordered another renderer".into());}
                next+=1;
            }
        }
        if added.iter().any(|part|part.role!="accessory"
            || part.source_property.as_deref()!=Some(property.as_str())) {
            return Err("accessory publication contains another source property".into());
        }
        let export=self.skel_export.as_ref().ok_or("accessory publication requires a prepared live rig")?;
        // This validates all export arrays/map before changing either state.
        export.validate_part_replacement(retained,exports)?;
        Ok(next)
    }

    #[cfg(test)]
    pub(crate) fn preview_residue_value(bytes:&[u8])->Result<serde_json::Value,String>{
        use bincode::Options;
        let snapshot:PreviewSnapshot=bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize(bytes).map_err(|e|e.to_string())?;
        let mut value=serde_json::to_value(snapshot).map_err(|e|e.to_string())?;
        // These two typed fields are HashSets, so JSON represents their
        // membership as randomly ordered arrays. All actual ordered arrays
        // (bones, weights, vertices and UVs) must retain their exact order.
        for part in value["session"]["parts"].as_array_mut().ok_or("missing snapshot parts")?{
            if let Some(runtime)=part.get_mut("runtime_accessory").and_then(|v|v.as_object_mut()){
                for field in ["implanted_bones","removed_accessory_bones"]{
                    runtime.get_mut(field).and_then(|v|v.as_array_mut()).ok_or("missing accessory membership set")?
                        .sort_by_key(|v|v.as_i64().expect("serialized i64 membership"));
                }
            }
        }
        Ok(value)
    }
    fn head_main_alpha_compatible(material:Option<&crate::unity::MeshMaterial>,png:&[u8])->bool {
        // An opaque specialization may only retain its depth-writing blend
        // state when the replacement MainTex still proves alpha=1.
        if !material.is_some_and(|m|m.float("_OKNativeAlphaOpaque")==Some(1.)){return true;}
        crate::teximg::load_from_memory(png).ok().is_some_and(|p|p.to_rgba8().pixels().all(|v|v[3]==255))
    }
    pub(crate) fn head_texture_snapshot_delta(before:&crate::portable_resources::SplitSnapshot,
        after:&crate::portable_resources::SplitSnapshot)->Result<HeadTextureDelta,String>{
        use bincode::Options;
        let decode=|s:&crate::portable_resources::SplitSnapshot|bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize::<PreviewSnapshot>(&s.residue).map_err(|e|e.to_string());
        let old=decode(before)?;let new=decode(after)?;
        if old.session.parts.len()!=new.session.parts.len()||old.session.shape_body!=new.session.shape_body||old.session.shape_face!=new.session.shape_face{
            return Err("head texture preparation changed model layout".into());
        }
        let reference=|s:&crate::portable_resources::SplitSnapshot,index:usize,field:&str|
            s.references.iter().find(|r|r.part==Some(index)&&r.field==field).map(|r|r.id.clone());
        let texture=|index:usize,field:&str|->Result<Option<Vec<u8>>,String>{
            let Some(id)=reference(after,index,field)else{return Ok(None)};
            let bytes=after.unique.get(&id).or_else(||before.unique.get(&id)).ok_or("head patch texture is not prepared")?;
            bincode::deserialize(bytes).map(Some).map_err(|e|e.to_string())
        };
        let mut parts=Vec::new();
        for(index,(a,b))in old.session.parts.iter().zip(&new.session.parts).enumerate(){
            let same=["png","shader_textures:_MainTex","eye_texture_0","eye_texture_1"].iter().all(|f|reference(before,index,f)==reference(after,index,f));
            if same&&a.runtime_textures==b.runtime_textures{continue;}
            if a.role!="head"||a.name!=b.name||a.shader_kind!=b.shader_kind||!matches!(a.shader_kind,0|6|11)||a.item_draw.is_some(){
                return Err(format!("head patch requires a new source program: {}",a.name));
            }
            parts.push(HeadTextureDeltaPart{index,name:b.name.clone(),shader_kind:b.shader_kind,
                png:texture(index,"png")?.ok_or("head patch has no main texture")?,runtime_textures:b.runtime_textures.clone(),
                eye_textures:if b.eye_draw.is_some(){Some([texture(index,"eye_texture_0")?,texture(index,"eye_texture_1")?])}else{None}});
        }
        Ok(HeadTextureDelta{part_count:new.session.parts.len(),shape_body:new.session.shape_body,
            shape_face:new.session.shape_face,parts})
    }
    pub(crate) fn validate_head_texture_delta(&self,delta:&HeadTextureDelta)->Result<(),String>{
        if self.parts.len()!=delta.part_count||self.shape_body!=delta.shape_body||self.shape_face!=delta.shape_face{
            return Err("head patch belongs to a different character shape/layout".into());
        }
        let mut indices=std::collections::BTreeSet::new();
        for d in &delta.parts{
            let p=self.parts.get(d.index).ok_or("head patch index out of bounds")?;
            if !indices.insert(d.index)||p.role!="head"||p.name!=d.name||p.shader_kind!=d.shader_kind||
                p.eye_draw.is_some()!=d.eye_textures.is_some()||p.item_draw.is_some()||!matches!(p.shader_kind,0|6|11)||d.png.is_empty(){
                return Err(format!("head patch renderer mismatch: {}",d.name));
            }
            if p.shader_kind==11&&!Self::head_main_alpha_compatible(p.source_material.as_ref(),&d.png){
                return Err("head texture changes opaque MainAlpha specialization".into());
            }
        }
        Ok(())
    }
    pub(crate) fn apply_head_texture_delta(&mut self,delta:&HeadTextureDelta)->Result<(),String>{
        self.validate_head_texture_delta(delta)?;
        for d in &delta.parts{let p=&mut self.parts[d.index];p.png=Some(d.png.clone());p.runtime_textures=d.runtime_textures.clone();
            if p.shader_kind==11{p.shader_textures.insert("_MainTex".into(),d.png.clone());}
            if let(Some(eye),Some(textures))=(&mut p.eye_draw,&d.eye_textures){eye.textures=textures.clone();}}
        Ok(())
    }
    /// Texture-only Maker edits retain the live skeleton, morphs, animation
    /// clocks, solver state and GPU stream identities. Validate the complete
    /// source transaction before committing any replacement.
    pub(crate) fn head_texture_changes(&self,source:&Self)->Result<Vec<usize>,String>{
        if self.parts.len()!=source.parts.len()||self.shape_body!=source.shape_body||self.shape_face!=source.shape_face {
            return Err("appearance changed model layout or shape".into());
        }
        let mut changed=Vec::new();
        for(index,(a,b))in self.parts.iter().zip(&source.parts).enumerate(){
            if a.role!=b.role||a.name!=b.name||a.rest_positions!=b.rest_positions||a.indices!=b.indices||
                a.shader_kind!=b.shader_kind||a.blend!=b.blend||a.render_visible!=b.render_visible {
                return Err(format!("appearance changed geometry/material layout: {}",a.name));
            }
            if a.png==b.png&&a.runtime_textures==b.runtime_textures&&
                (a.shader_kind!=11||a.shader_textures.get("_MainTex")==b.shader_textures.get("_MainTex"))&&
                a.eye_draw.as_ref().map(|e|&e.textures)==b.eye_draw.as_ref().map(|e|&e.textures){continue;}
            if a.role!="head"||a.item_draw.is_some()||a.eye_draw.is_some()!=b.eye_draw.is_some()||!matches!(a.shader_kind,0|6|11){
                return Err(format!("appearance requires a full renderer replacement: {}",a.name));
            }
            if a.shader_kind==11&&(a.source_material.as_ref().and_then(|m|m.float("_OKNativeAlphaOpaque"))!=b.source_material.as_ref().and_then(|m|m.float("_OKNativeAlphaOpaque"))||
                !b.png.as_ref().is_some_and(|png|Self::head_main_alpha_compatible(a.source_material.as_ref(),png))){
                return Err("appearance changes opaque MainAlpha specialization".into());
            }
            changed.push(index);
        }
        Ok(changed)
    }
    pub(crate) fn copy_head_textures(&mut self,source:&Self,indices:&[usize]){
        for &index in indices {let a=&mut self.parts[index];let b=&source.parts[index];
            a.png=b.png.clone();a.runtime_textures=b.runtime_textures.clone();
            if a.shader_kind==11{if let Some(png)=&b.png{a.shader_textures.insert("_MainTex".into(),png.clone());}}
            if let(Some(a),Some(b))=(&mut a.eye_draw,&b.eye_draw){a.textures=b.textures.clone();}
        }
    }
    pub(crate) fn set_yure_policy(&mut self,policy:crate::anim::yure::Policy) {
        if let (Some(runtime),Some(body))=(&mut self.body_dynamic_runtime,&mut self.body_skel) {
            runtime.set_actives(body,[true;4]); // LoadExcel.ResetShape precedes Proc.
            runtime.set_actives(body,policy.active);
        }
        self.yure_policy=Some(policy);
        // The first Studio action can be H, before any Animator/shape frame.
        // A body-only baseline would make reset_animator_pose erase the head.
        if self.shaped_baselines_missing() {
            let body=self.shape_body.clone();let face=self.shape_face.clone();
            self.apply_shape(&body,&face);
            return;
        }
        let Some(mut shaped)=self.body_rest.clone() else {return;};
        crate::shape::apply_body_shape_keys_masked(&mut shaped,&self.body_keys,&self.shape_body,&self.correct,
            self.sex,self.type_bone,self.nip_stand_rate,policy.shapes);
        crate::pose::assigned_weights_remap(&mut shaped);
        self.body_shaped=Some(shaped.clone());
        if let Some(runtime)=&mut self.body_dynamic_runtime {
            let names:Vec<String>=self.abm.iter().map(|b|b.name.clone()).collect();
            let base=crate::pose::capture_baseline(&shaped,&names);
            crate::pose::apply_abm(&mut shaped,&self.abm,0,&base);
            let fresh=self.body_dynamics.runtime(&mut shaped,self.sex,self.bust_softness,self.bust_weight,
                self.shape_body.get(4).copied().unwrap_or(0.5));
            runtime.refresh_breast_references(fresh);
        }
    }
    pub(crate) fn step_eye_look(&mut self,target:[f32;3],pattern:usize,dt:f32,enabled:bool)->Option<[f32;3]> {
        let c=self.eye_look_config.as_ref()?;
        let basis=crate::eye_look::Basis::from_session(self,c)?;
        if self.eye_look_runtime.is_none() {self.eye_look_runtime=crate::eye_look::Runtime::from_session(self,c);}
        let runtime=self.eye_look_runtime.as_mut()?;
        if !runtime.step(c,&basis,target,pattern,dt,enabled,None) {return None;}
        // Only source-referenced auxiliary transforms rotate. They are siblings
        // of the actual iris bones in the selected rigs, not face overrides.
        if let Some(head)=self.head_skel.as_mut() {
            for (i,eye) in c.eyes.iter().enumerate() {
                if let Some(t)=head.tfs.values_mut().find(|t|t.name==eye.name) {t.rot=runtime.fixed[i];}
            }
            crate::pose::assigned_weights_remap(head);
        }
        Some(runtime.rates)
    }
    /// Clothes thickness per body capsule for the clothed collision layer; recomputed only when the worn meshes change.
    fn cloth_thickness(&self) -> (Vec<f32>, [f32; 3]) {
        use std::hash::{Hash, Hasher};
        // (outfit signature, result, calls left before a retry: garments not skinned yet measure as nothing).
        static CACHE: std::sync::OnceLock<std::sync::Mutex<HashMap<u64, (u64, (Vec<f32>, [f32; 3]), u32)>>> = std::sync::OnceLock::new();
        // Measured once per outfit in the current pose: capsules and meshes from the same live skeleton.
        let Some(skel) = self.body_skel.as_ref() else { return (Vec::new(), [-2.0, 0.0, 0.0]); };
        let is_skin = |p: &SessionPart| p.name.to_ascii_lowercase().starts_with("o_body");
        let excluded = |p: &SessionPart| { let n = p.name.to_ascii_lowercase();
            ["o_tang", "o_tooth", "o_mnpa", "o_mnpb", "o_mnpk", "o_dankon", "o_gomu", "o_shadowcaster"].iter().any(|x| n.starts_with(x)) };
        // On the body skeleton: GPU-skinned body/garments (skel_kind 1), and garments with their own dynamic rig that
        // follow the clothes (coordinated private rigs, CPU-skinned every frame). Hair/accessories are not clothes.
        // The skin (body mesh on the body skeleton) and the garments (role clothes_*: their own rigs, CPU-skinned every
        // frame). Hair and accessories are what rests on them, not part of them.
        let worn: Vec<&SessionPart> = self.parts.iter().filter(|p| p.render_visible && !excluded(p)
            // Shoes only cover the far end of the shin capsule: counting them would thicken the whole shin.
            && ((p.runtime_accessory.is_none() && p.skel_kind == 1 && is_skin(p)) || (p.role.starts_with("clothes") && p.role != "clothes_shoes"))).collect();
        let mut h = std::collections::hash_map::DefaultHasher::new();
        for p in &worn { p.role.hash(&mut h); p.name.hash(&mut h); p.rest_positions.len().hash(&mut h); p.positions.is_empty().hash(&mut h); }
        let signature = h.finish();
        let key = self as *const Self as u64;
        let cache = CACHE.get_or_init(|| std::sync::Mutex::new(HashMap::new()));
        if let Ok(mut c) = cache.lock() {
            if let Some((sig, t, retry)) = c.get_mut(&key) {
                if *sig == signature && *retry == 0 { return t.clone(); }
                if *sig == signature { *retry -= 1; if *retry > 0 { return t.clone(); } }
            }
        }
        let skinned: Vec<(bool, Vec<[f32; 3]>)> = worn.iter().map(|p| {
            if p.runtime_accessory.is_some() { return (true, p.positions.clone()); } // a garment rig: skinned this frame
            // Same palette as the renderer: bone world x the mesh's own bind pose.
            let (worlds, binds) = (&skel.worlds, &p.bind_poses);
            let positions = p.rest_positions.iter().enumerate().filter_map(|(i, v)| {
                let (w, b) = (p.weights.get(i)?, p.bone_idx.get(i)?);
                let mut out = [0.0f32; 3]; let mut total = 0.0;
                for k in 0..4 {
                    let bone = b[k] as usize;
                    if w[k] <= 0.0 || bone >= worlds.len() || bone >= binds.len() { continue; }
                    let q = crate::pose::mat_vec(&crate::pose::mat_mul_pub(&worlds[bone], &binds[bone]), *v);
                    for a in 0..3 { out[a] += q[a] * w[k]; }
                    total += w[k];
                }
                (total > 0.5).then(|| [out[0] / total, out[1] / total, out[2] / total])
            }).collect();
            (!is_skin(p), positions)
        }).collect();
        let parts: Vec<(bool, &[[f32; 3]])> = skinned.iter().map(|(c, v)| (*c, v.as_slice())).collect();
        let thickness = crate::dbone::cloth_layer_thickness(skel, &parts);
        // Garments worn but none of their vertices measured: their rigs have not been skinned yet; measure again later.
        let unready = thickness.1[2] < 1.0 && worn.iter().any(|p| p.role.starts_with("clothes"));
        if let Ok(mut c) = cache.lock() { c.insert(key, (signature, thickness.clone(), if unready { 30 } else { 0 })); }
        thickness
    }
    fn update_clothes_dynamics(&mut self, dt: f32) {
        if std::env::var("OK_NODB").is_ok() || std::env::var("OK_NO_CLOTH_DYNAMICS").ok().as_deref()==Some("1") || self.clothes_dynamics.is_empty() { return; }
        let studio = !self.game_root.as_os_str().is_empty() && crate::anim::clip_expression_bone_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode);
        let flags = self.collider_settings.resolve(studio, self.collider_overrides);
        let group = self as *const Self as u64;
        let physx = self.physx_secondary;
        let cloth = self.cloth_thickness();
        let Some(body) = &mut self.body_skel else { return; };
        let _group = crate::px::enter_group(group, physx);
        let _solver = crate::px::enter_solver(self.physics.solver(Some(crate::okphysics::CLOTHES), crate::px::PhysicsComponent::Clothes));
        crate::dbone::publish_body_proxy(group, body, &cloth);
        // KKS_Colliders edits the live component; it does not restart its particles.
        if let Some((old,_))=&mut self.clothes_dynamic_runtime {*old=flags;}
        if self.clothes_dynamic_runtime.is_none() {
            let mut setup = self.body_shaped.clone().unwrap_or_else(|| body.clone());
            let names: Vec<String> = self.abm.iter().map(|b| b.name.clone()).collect();
            let base = crate::pose::capture_baseline(&setup, &names);
            crate::pose::apply_abm(&mut setup, &self.abm, 0, &base);
            if let Some(mut state) = crate::dbone::AttachedDynamicSet::new_unsettled(&self.clothes_dynamics, &setup, &crate::pose::ident()) {
                state.mark_skin_chains();
                state.configure_shared_clothes_colliders(body, &self.body_colliders, flags);
                for _ in 0..240 {
                    state.sync_shared_pose(body); state.configure_shared_clothes_colliders(body, &self.body_colliders, flags);
                    state.step_on_animated_pose(1.0 / 60.0);
                }
                if let Ok(path) = std::env::var("OK_PX_CHAINS") {
                    use std::io::Write;
                    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) { let _ = write!(f, "session {group:x}\n{}", state.describe_chains()); }
                }
                self.clothes_dynamic_runtime = Some((flags, state));
            }
        }
        if let Some((_, state)) = &mut self.clothes_dynamic_runtime {
            use crate::okanim::prof;
            let t = std::time::Instant::now();
            state.sync_shared_pose(body);
            prof::add(22, t);
            let t = std::time::Instant::now();
            state.configure_shared_clothes_colliders(body, &self.body_colliders, flags);
            prof::add(23, t);
            let t = std::time::Instant::now();
            state.step_on_animated_pose(dt);
            prof::add(24, t);
            let t = std::time::Instant::now();
            state.copy_simulated_pose_filtered(body, dt == 0.0);
            prof::add(25, t);
        }
    }
    /// IllusionFixes skips Ver02's phantom iteration when a frame has no
    /// elapsed time. Portable playback clears game_root, so a DLL lookup
    /// cannot run; treating that as "plugin absent" wrote the warmed
    /// particle pose onto every body chain during static ApplyShape.
    /// T-pose is that rebuild. An empty root honors the skip. A real
    /// game directory still follows its own plugin, and dt>0 still simulates.
    fn ver02_honors_dynamic_bones_fix(&self) -> bool {
        if self.game_root.as_os_str().is_empty() { return true; }
        crate::dbone::dynamic_bones_fix_enabled(&self.game_root)
    }
    fn update_body_dynamics(&mut self, dt: f32) {
        if std::env::var("OK_NODB").is_ok() { return; }
        let studio = !self.game_root.as_os_str().is_empty() && crate::anim::clip_expression_bone_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode);
        let flags = self.collider_settings.resolve(studio, self.collider_overrides);
        if self.body_dynamic_runtime.as_ref().is_some_and(|r| r.collider_flags() != Some(flags)) {
            if let (Some(runtime),Some(shaped))=(&mut self.body_dynamic_runtime,&self.body_shaped) {
                runtime.configure_colliders(shaped,flags,self.shape_body.get(4).copied().unwrap_or(0.5));
            }
        }
        let honors_dynamic_bones_fix = self.ver02_honors_dynamic_bones_fix();
        let group = self as *const Self as u64;
        let physx = self.physx_secondary;
        let cloth = self.cloth_thickness();
        let Some(sk) = self.body_skel.as_mut() else { return; };
        if self.body_dynamics.len() == 0 { return; }
        let _group = crate::px::enter_group(group, physx);
        let _solver = crate::px::enter_solver(self.physics.solver(Some(crate::okphysics::BODY), crate::px::PhysicsComponent::Body));
        crate::dbone::publish_body_proxy(group, sk, &cloth);
        if self.body_dynamic_runtime.is_none() {
            let mut shaped = self.body_shaped.clone().unwrap_or_else(|| sk.clone());
            let names: Vec<String> = self.abm.iter().map(|b| b.name.clone()).collect();
            let base = crate::pose::capture_baseline(&shaped, &names);
            crate::pose::apply_abm(&mut shaped, &self.abm, 0, &base);
            let mut runtime = self.body_dynamics.runtime(&mut shaped, self.sex, self.bust_softness,
                self.bust_weight, self.shape_body.get(4).copied().unwrap_or(0.5));
            runtime.suppress_skip_update(honors_dynamic_bones_fix);
            runtime.configure_colliders(&shaped, flags, self.shape_body.get(4).copied().unwrap_or(0.5));
            if let Some(policy)=self.yure_policy {runtime.set_actives(&mut shaped,policy.active);}
            let animated = sk.clone();
            for _ in 0..240 {
                *sk = animated.clone(); runtime.step(sk, 1.0 / 60.0);
            }
            *sk = animated;
            self.body_dynamic_runtime = Some(runtime);
        }
        self.body_dynamic_runtime.as_mut().unwrap().step(sk, dt);
        crate::dbone::step_ragdoll(group, sk, dt);
    }

    pub(crate) fn reset_dynamic_accessories(&mut self) {
        for part in &mut self.parts {
            if let Some(runtime) = &mut part.runtime_accessory { runtime.reset(); }
        }
    }

    pub(crate) fn update_dynamic_accessories(&mut self, dt: f32) {
        self.update_dynamic_accessories_impl(dt, crate::anim::is_shared_root(&self.game_root));
    }

    fn update_dynamic_accessories_impl(&mut self, dt: f32, palette_only: bool) {
        self.update_dynamic_accessories_dispatch(dt,palette_only,true);
    }

    fn update_dynamic_accessories_dispatch(&mut self, dt:f32,palette_only:bool,parallel:bool) {
        self.update_dynamic_accessories_dispatch_observed(dt,palette_only,parallel,None);
    }

    fn update_dynamic_accessories_dispatch_observed(&mut self,dt:f32,palette_only:bool,parallel:bool,observer:Option<crate::ffi::PrivateSkinObserver>) {
        self.update_dynamic_accessories_filtered(dt,palette_only,parallel,observer,&[]);
    }
    fn update_dynamic_accessories_filtered(&mut self,dt:f32,palette_only:bool,parallel:bool,observer:Option<crate::ffi::PrivateSkinObserver>,excluded:&[usize]) {
        let t_prepare=std::time::Instant::now();
        let policy=AccessoryFramePolicy::read();
        let studio = !self.game_root.as_os_str().is_empty() && crate::anim::clip_expression_bone_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode);
        let flags = self.collider_settings.resolve(studio, self.collider_overrides);
        let group = self as *const Self as u64;
        let physx = self.physx_secondary;
        let body = self.body_skel.as_ref();
        if let Some(sk) = body { let _group = crate::px::enter_group(group, physx); crate::dbone::publish_body_proxy(group, sk, &self.cloth_thickness()); }
        let clothes_body=self.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel).or(body);
        let head = self.head_full_posed.as_ref().or(self.head_skel.as_ref());
        // Private rigs own independent particle histories. Prepare immutable
        // character colliders once, then solve each renderer on the pool.
        let needs_frame=|coordinated|self.parts.iter().any(|p|p.runtime_accessory.as_ref().is_some_and(|r|r.coordinated==coordinated&&r.source.deferred_dbones.as_ref().is_some_and(|d|!d.is_empty())));
        let frames_trace=crate::native_profile::Scope::new(44);
        let body_frame=body.filter(|_|needs_frame(false)).map(|sk|crate::dbone::CharacterColliderFrame::new(sk,&self.body_colliders,flags));
        let clothes_frame=clothes_body.filter(|_|needs_frame(true)).map(|sk|crate::dbone::CharacterColliderFrame::new(sk,&self.body_colliders,flags));
        let colliders=&self.body_colliders;
        // Body pose is immutable throughout the joined renderer pass. Palette-only
        // dynamics do not consume skin matrices and must not pay this preparation.
        let assigned_body=if palette_only{None}else{AssignedWorldFrame::prepare(body,&self.parts,false)};
        let assigned_clothes=if palette_only{None}else{AssignedWorldFrame::prepare(clothes_body,&self.parts,true)};
        drop(frames_trace);
        // One accessory drawn by several renderers simulates the same bones once per renderer. Each measured the
        // mesh thickness of its own renderer only; with different radii against the partner's body the copies of a
        // strand parted and crossed each other (bangs "fighting"). Every copy uses the largest thickness of the bone.
        {
            let _trace=crate::native_profile::Scope::new(45);
            let mut radii: HashMap<(String, String), f32> = HashMap::new();
            for runtime in self.parts.iter().filter_map(|p| p.runtime_accessory.as_ref()) {
                let Some(state) = runtime.state.as_ref() else { continue };
                for (bone, r) in state.mesh_radii() {
                    let slot = radii.entry((runtime.parent_key.clone(), bone)).or_insert(0.0);
                    *slot = slot.max(r);
                }
            }
            if !radii.is_empty() {
                for runtime in self.parts.iter_mut().filter_map(|p| p.runtime_accessory.as_mut()) {
                    let key = runtime.parent_key.clone();
                    let Some(state) = runtime.state.as_mut() else { continue };
                    let own: HashMap<String, f32> = radii.iter().filter(|((k, _), _)| *k == key).map(|((_, b), r)| (b.clone(), *r)).collect();
                    state.apply_mesh_radii(&own);
                }
            }
        }
        // Renderers of one prefab (same socket and placement, same simulated bones, same part) share one solve: the
        // first solves, the others take its bone locals (update_render_follow). OK_ACC_SHARED_SOLVE=0: each solves.
        let mut leader_of:Vec<Option<usize>>=vec![None;self.parts.len()];
        let mut particle_ids:HashMap<usize,Vec<i64>>=HashMap::new();
        if dt>0.0 && acc_shared_solve() {
            let _trace=crate::native_profile::Scope::new(46);
            let mut leaders:HashMap<(String,String,Option<String>,[u32;16],Vec<i64>),usize>=HashMap::new();
            for (index,part) in self.parts.iter().enumerate() {
                let Some(runtime)=&part.runtime_accessory else {continue;};
                let Some(state)=&runtime.state else {continue;};
                if runtime.copy_weights_body || runtime.coordinated {continue;}
                let ids=state.particle_ids();if ids.is_empty(){continue;}
                let placement:[u32;16]=std::array::from_fn(|i|runtime.local_placement[i/4][i%4].to_bits());
                match leaders.entry((runtime.parent_key.clone(),part.role.clone(),part.source_property.clone(),placement,ids.clone())) {
                    std::collections::hash_map::Entry::Occupied(leader)=>{leader_of[index]=Some(*leader.get());particle_ids.insert(index,ids);}
                    std::collections::hash_map::Entry::Vacant(slot)=>{slot.insert(index);particle_ids.insert(index,ids);}
                }
            }
        }
        let leader_of=&leader_of;
        let physics=&self.physics;
        crate::okanim::prof::add(18,t_prepare);
        let t_parts=std::time::Instant::now();
        let skinned=std::sync::atomic::AtomicU64::new(0);
        let update=|(index,part):(usize,&mut SessionPart)| {
            if leader_of[index].is_some() {return;}
            let Some(runtime) = &mut part.runtime_accessory else { return; };
            let _group = crate::px::enter_group(group, physx);
            // Hair and accessories are worn over the clothes: they collide with the clothed body surface. Garments
            // (skirts...) rest on the skin.
            let _outer = crate::px::enter_outer(!part.role.starts_with("clothes"));
            let key = crate::okphysics::part_key(&part.role, part.source_property.as_deref());
            let _solver = crate::px::enter_solver(physics.solver(key.as_deref(), crate::px::PhysicsComponent::from_role(&part.role)));
            // Accessory chains are classified one by one (hair, tail...); the card's choice for the part still wins.
            let _card = crate::px::enter_card_solver(physics.card_choice(key.as_deref()));
            let source=if runtime.coordinated {clothes_body}else{body};
            let frame=if runtime.coordinated {&clothes_frame}else{&body_frame};
            let assigned=if runtime.coordinated{assigned_clothes.as_ref()}else{assigned_body.as_ref()};
            let t_part=std::time::Instant::now();
            let rendered=runtime.update_render_impl(source,head,colliders,flags,dt,!palette_only&&excluded.binary_search(&index).is_err(),frame.as_ref(),policy,assigned);
            // OK_ACC_PART_PROFILE=<frames>: every that many frames of a part, its solve time (diagnostic).
            if let Some(every)=acc_part_profile_every() {
                static TIMES:std::sync::Mutex<Option<HashMap<(u64,usize),(u32,u64)>>>=std::sync::Mutex::new(None);
                let mut times=TIMES.lock().unwrap();let slot=times.get_or_insert_with(HashMap::new).entry((group,index)).or_insert((0,0));
                slot.0+=1;slot.1+=t_part.elapsed().as_micros() as u64;
                if slot.0%every==0 {eprintln!("OK acc part session={group:x} index={index} role={} name={} avg_us={}",part.role,part.name,slot.1/every as u64);slot.1=0;}
            }
            if let Some((positions, normals,tangents)) = rendered {
                skinned.fetch_add(1,std::sync::atomic::Ordering::Relaxed);
                part.positions = positions;
                part.normals = normals;
                part.rest_tangents=tangents;
                if let Some(observer)=observer{observer.emit(index,part);}
            }
        };
        if parallel&&self.parts.iter().filter(|p|p.runtime_accessory.is_some()).count()>=8 {
            use rayon::prelude::*;
            native_parallel_work(||self.parts.par_iter_mut().enumerate().for_each(update));
        } else {self.parts.iter_mut().enumerate().for_each(update);}
        if leader_of.iter().any(Option::is_some) {
            // The leaders' solved locals, then the followers; one that cannot follow solves on its own.
            let mut solved:HashMap<usize,Vec<([f32;3],[f32;4])>>=HashMap::new();
            for leader in leader_of.iter().flatten() {
                if solved.contains_key(leader) {continue;}
                let locals=self.parts[*leader].runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).and_then(|s|s.particle_locals(&particle_ids[leader]));
                if let Some(locals)=locals {solved.insert(*leader,locals);}
            }
            let follow=|(index,part):(usize,&mut SessionPart)| {
                let Some(leader)=leader_of[index] else {return;};
                let Some(runtime)=&mut part.runtime_accessory else {return;};
                let source=if runtime.coordinated {clothes_body}else{body};
                let assigned=if runtime.coordinated{assigned_clothes.as_ref()}else{assigned_body.as_ref()};
                let emit=!palette_only&&excluded.binary_search(&index).is_err();
                let followed=solved.get(&leader).and_then(|locals|runtime.update_render_follow(source,head,flags,emit,&particle_ids[&index],locals,policy,assigned));
                let rendered=match followed {
                    Some(rendered)=>rendered,
                    None=>{
                        let _group=crate::px::enter_group(group, physx);
                        let _outer=crate::px::enter_outer(!part.role.starts_with("clothes"));
                        let key=crate::okphysics::part_key(&part.role, part.source_property.as_deref());
                        let _solver=crate::px::enter_solver(physics.solver(key.as_deref(), crate::px::PhysicsComponent::from_role(&part.role)));
                        let _card=crate::px::enter_card_solver(physics.card_choice(key.as_deref()));
                        let frame=if runtime.coordinated {&clothes_frame}else{&body_frame};
                        runtime.update_render_impl(source,head,colliders,flags,dt,emit,frame.as_ref(),policy,assigned)
                    }
                };
                if let Some((positions,normals,tangents))=rendered {
                    skinned.fetch_add(1,std::sync::atomic::Ordering::Relaxed);
                    part.positions=positions;part.normals=normals;part.rest_tangents=tangents;
                    if let Some(observer)=observer{observer.emit(index,part);}
                }
            };
            if parallel&&leader_of.iter().filter(|l|l.is_some()).count()>=8 {
                use rayon::prelude::*;
                native_parallel_work(||self.parts.par_iter_mut().enumerate().for_each(follow));
            } else {self.parts.iter_mut().enumerate().for_each(follow);}
        }
        crate::okanim::prof::add(19,t_parts);
        crate::okanim::prof::add_us(20,skinned.load(std::sync::atomic::Ordering::Relaxed));
        crate::okanim::prof::add_us(21,palette_only as u64);
    }

    fn render_solved_accessories(&mut self) {self.render_solved_accessories_observed(None);}

    fn render_solved_accessories_observed(&mut self,observer:Option<crate::ffi::PrivateSkinObserver>) {
        self.render_solved_accessories_filtered(observer,&[]);
    }
    fn render_solved_accessories_filtered(&mut self,observer:Option<crate::ffi::PrivateSkinObserver>,excluded:&[usize]) {
        let policy=AccessoryFramePolicy::read();
        // Explicit queries immediately after a resource reset still initialize
        // the authored rig. Steady playback has already solved it at frame dt.
        let rigid_clothes=policy.rigid_clothes;
        if self.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()).any(|r|
            r.state.is_none()&&!(r.copy_weights_body&&rigid_clothes)&&r.source.deferred_dbones.as_ref().is_some_and(|d|!d.is_empty())) {
            self.update_dynamic_accessories_impl(0.,false);return;
        }
        let body=self.body_skel.as_ref();
        let clothes=self.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel).or(body);
        let head=self.head_full_posed.as_ref().or(self.head_skel.as_ref());
        let assigned_body=AssignedWorldFrame::prepare(body,&self.parts,false);
        let assigned_clothes=AssignedWorldFrame::prepare(clothes,&self.parts,true);
        let render=|(index,part):(usize,&mut SessionPart)| {
            if excluded.binary_search(&index).is_ok(){return;}
            let Some(runtime)=part.runtime_accessory.as_ref() else{return;};
            let assigned=if runtime.coordinated{assigned_clothes.as_ref()}else{assigned_body.as_ref()};
            if let Some((positions,normals,tangents))=runtime.render_solved(if runtime.coordinated{clothes}else{body},head,policy,assigned) {
                part.positions=positions;part.normals=normals;part.rest_tangents=tangents;
                if let Some(observer)=observer{observer.emit(index,part);}
            }
        };
        if self.parts.iter().filter(|p|p.runtime_accessory.is_some()).count()>=8 {
            use rayon::prelude::*;native_parallel_work(||self.parts.par_iter_mut().enumerate().for_each(render));
        } else {self.parts.iter_mut().enumerate().for_each(render);}
    }

    pub fn apply_shape(&mut self, body: &[f32], face: &[f32]) {self.apply_shape_impl(body,face,false,false,true);}

    /// Maker live parameter edits do not call ResetDynamicBoneAll.
    pub fn apply_shape_live(&mut self,body:&[f32],face:&[f32]) {self.apply_shape_impl(body,face,true,false,true);}

    pub fn prepare_shape_live(&mut self,body:&[f32],face:&[f32]) {self.apply_shape_impl(body,face,true,true,true);}
    #[cfg(test)]
    pub(crate) fn prepare_shape_live_oracle(&mut self,body:&[f32],face:&[f32]) {self.apply_shape_impl(body,face,true,true,false);}

    fn apply_shape_impl(&mut self, body: &[f32], face: &[f32],live:bool,defer_dynamics:bool,reuse_preparation:bool) {
        let same_face=live&&self.shape_face==face;
        let previous_head_scale=self.body_shaped.as_ref().and_then(|s|s.world_tf("cf_s_head")).map(|m|crate::shape::world_scale(&m));
        self.shape_body = body.to_vec();
        self.shape_face = face.to_vec();
        if !live {self.body_dynamic_runtime = None;self.clothes_dynamic_runtime = None;}
        let restore_trace=crate::native_profile::Scope::new(25);
        match (&mut self.body_skel,&self.body_rest) {(Some(current),Some(rest))=>current.restore_from(rest),(_,rest)=>self.body_skel=rest.clone()};
        drop(restore_trace);
        let body_trace=crate::native_profile::Scope::new(26);
        let mut head_scale = [1.0f32; 3];
        let external_animation=self.external_animation();
        if let Some(sk) = self.body_skel.as_mut() {
            if !self.shape_body.is_empty() && !self.body_keys.is_empty() {
                head_scale = crate::shape::apply_body_shape_keys_masked(
                    sk,
                    &self.body_keys,
                    &self.shape_body,
                    &self.correct,
                    self.sex,
                    self.type_bone,
                    self.nip_stand_rate,
                    self.yure_policy.map_or([[true;9];2],|p|p.shapes),
                );
            }
            #[cfg(test)]
            if reuse_preparation {crate::pose::assigned_weights_remap(sk);}else{crate::pose::assigned_weights_remap_oracle(sk);}
            #[cfg(not(test))]
            crate::pose::assigned_weights_remap(sk);
            if let Some(current)=self.body_shaped.as_mut(){current.restore_from(sk);}else{self.body_shaped=Some(sk.clone());}
            if !defer_dynamics {
            let mut locals = false;
            if !self.game_root.as_os_str().is_empty() && !external_animation {
                crate::anim::apply_clip_at_parameters(&self.game_root, sk, &self.anim_clip, self.anim_time,&self.anim_parameters);
                if self.sex == 1 {
                    crate::shape::apply_body_shape_always_locals(sk, self.body_shaped.as_ref().unwrap());
                    locals = true;
                    let (arm, hand) = crate::anim::clip_limb_corrections_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode);
                    crate::pose::apply_scene_limb_size_corrections_locals(sk, arm, hand);
                }
            }
            locals |= self.hand_shapes.apply_cached_locals(sk, &mut self.hand_shape_ids);
            locals |= apply_user_fk_locals(sk, &self.user_fk, &mut self.user_fk_ids);
            // Shape, limb sizes, hand shapes and FK change local transforms only: the worlds are published once.
            if locals { sk.rebuild_worlds(); }
            // OCIChar.ActiveKinematicMode disables Preparation.pvCopy for all
            // limbs while FK is enabled, even when only fingers are selected.
            if !self.game_root.as_os_str().is_empty() && crate::anim::clip_expression_bone_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode) {
                if let Some(config)=&self.pv_config {config.apply(sk,self.pv_mask,self.pv_fk || !self.user_fk.is_empty());}
                crate::pose::apply_expression_bone(sk);
            }
            let names: Vec<String> = self.abm.iter().map(|b| b.name.clone()).collect();
            let base = crate::pose::capture_baseline(sk, &names);
            crate::pose::apply_abm(sk, &self.abm, 0, &base);
            // Studio IK after the bone modifiers: a target is where the limb ends up.
            apply_studio_ik(sk, &mut self.studio_ik_solver, &self.studio_ik, &self.studio_ik_rotations);
            if !self.game_root.as_os_str().is_empty() && std::env::var("OK_FINALIK").is_ok() {
                crate::finalik::apply_final_ik(
                    &self.game_root,
                    &self.shape_body,
                    sk,
                    "Stand_D_00",
                    "p_cf_body_bone",
                );
            }
            // Static shape rebuilds also publish a pose; retain saved FIX locals without advancing a tracking clock.
            if self.head_look.fixed.is_some() { self.head_look.apply_preserving_fk(sk,0.,&self.user_fk); }
            }
        }
        // Live sliders do not mutate source keys, typeBone, correct or ABMX.
        drop(body_trace);
        let head_trace=crate::native_profile::Scope::new(27);
        // The head shape depends on face values and the body's parent scale.
        let reuse_head=same_face&&previous_head_scale==Some(head_scale)&&self.head_shaped.is_some()&&self.head_full_posed.is_some();
        // The deferred caller publishes only after Animator restores this same
        // head baseline and applies ABMX. Do not build a posed head which that
        // mandatory reset immediately overwrites. Changed baselines still run.
        if !(reuse_preparation&&defer_dynamics&&reuse_head) {
        let head_source=if reuse_head {&self.head_shaped}else{&self.head_rest};
        match (&mut self.head_skel,head_source) {(Some(current),Some(rest))=>current.restore_from(rest),(_,rest)=>self.head_skel=rest.clone()};
        if let Some(sk) = self.head_skel.as_mut() {
            if !reuse_head {
            if !self.shape_face.is_empty() && !self.head_keys.is_empty() {
                let head_correct =
                    1.0 / (1.0 + self.correct.get(2).map(|c| c.scl[1]).unwrap_or(0.0));
                crate::shape::apply_head_shape_keys(
                    sk,
                    &self.head_keys,
                    &self.shape_face,
                    head_scale,
                    head_correct,
                    self.type_bone,
                );
            }
            #[cfg(test)]
            if reuse_preparation {crate::pose::assigned_weights_remap(sk);}else{crate::pose::assigned_weights_remap_oracle(sk);}
            #[cfg(not(test))]
            crate::pose::assigned_weights_remap(sk);
            if let Some(current)=self.head_shaped.as_mut(){current.restore_from(sk);}else{self.head_shaped=Some(sk.clone());}
            }
            // ABM/ABMX head modifiers (e.g. `cf_J_ChinTip_Base`, cheek/neck
            // bones) are stored in the card and applied by the plugin to the
            // head skeleton too; without this the chin/cheeks keep the vanilla
            // shape.
            let abm_names: Vec<String> = self.abm.iter().map(|b| b.name.clone()).collect();
            let abm_base = crate::pose::capture_baseline(sk, &abm_names);
            crate::pose::apply_abm(sk, &self.abm, 0, &abm_base);
            sk.include_tree_bones();
        }
        }
        // Hair and head accessories use the full prefab tree, not the face
        drop(head_trace);
        let full_head_trace=crate::native_profile::Scope::new(28);
        // SMR tree. Rebuild it with the same current parent scale and sliders
        // used for the face; retaining the imported pose detaches the hair
        // when height/head-size/face values change.
        if !reuse_head {
        match (&mut self.head_full_posed,&self.head_full_rest) {(Some(current),Some(rest))=>current.restore_from(rest),(_,rest)=>self.head_full_posed=rest.clone()};
        if let Some(sk)=self.head_full_posed.as_mut() {
            if !self.shape_face.is_empty() && !self.head_keys.is_empty() {
                let head_correct=1.0/(1.0+self.correct.get(2).map(|c|c.scl[1]).unwrap_or(0.0));
                crate::shape::apply_head_shape_keys(sk,&self.head_keys,&self.shape_face,
                    head_scale,head_correct,self.type_bone);
            }
            let names:Vec<String>=self.abm.iter().map(|b|b.name.clone()).collect();
            let baseline=crate::pose::capture_baseline(sk,&names);
            crate::pose::apply_abm(sk,&self.abm,0,&baseline);
        }
        }
        drop(full_head_trace);
        let references_trace=crate::native_profile::Scope::new(29);
        if live {
            // Rebuild changed breast reference locals / parameters, retaining
            // the particles and hip chains as Ver02.ResetPosition does.
            if let (Some(runtime),Some(base))=(&mut self.body_dynamic_runtime,&self.body_shaped) {
                let mut shaped=if reuse_preparation {let mut scratch=self.shape_reference_scratch.take().unwrap_or_else(||base.clone());scratch.restore_from(base);scratch}else{base.clone()};
                let names:Vec<String>=self.abm.iter().map(|b|b.name.clone()).collect();
                let base=crate::pose::capture_baseline(&shaped,&names);crate::pose::apply_abm(&mut shaped,&self.abm,0,&base);
                if reuse_preparation {
                self.body_dynamics.refresh_live_breast_references(&shaped,self.bust_softness,self.bust_weight,self.shape_body.get(4).copied().unwrap_or(0.5),runtime);
                }else{
                #[cfg(test)]
                let fresh=self.body_dynamics.breast_references_oracle(&mut shaped,self.sex,self.bust_softness,self.bust_weight,self.shape_body.get(4).copied().unwrap_or(0.5));
                #[cfg(not(test))]
                let fresh=self.body_dynamics.breast_references(&mut shaped,self.sex,self.bust_softness,self.bust_weight,self.shape_body.get(4).copied().unwrap_or(0.5));
                runtime.refresh_breast_references(fresh);
                }
                let studio=crate::anim::clip_expression_bone_mode(&self.game_root,&self.anim_clip,self.anim_scene_mode);
                runtime.configure_colliders(&shaped,self.collider_settings.resolve(studio,self.collider_overrides),self.shape_body.get(4).copied().unwrap_or(0.5));
                if reuse_preparation {self.shape_reference_scratch=Some(shaped);}
            }
        }
        // Animator will publish the changed pose and physics once this frame.
        drop(references_trace);
        if defer_dynamics {return;}
        self.update_body_dynamics(0.0);
        self.update_clothes_dynamics(0.0);
        if let Some(mut ex) = self.skel_export.take() {
            crate::skel_export::refresh_pose(self, &mut ex);
            self.skel_export = Some(ex);
        }
        if !live {self.reset_dynamic_accessories();}
        self.update_dynamic_accessories(0.0);
    }

    pub fn apply_anim_time(&mut self, t: f32) {
        // Compatibility for timestamp-only callers. Playback must supply its
        // actual frame delta: a wrapped sample cannot reveal elapsed loops.
        let sample_time = t.max(0.0);
        let dt = if sample_time >= self.anim_time {
            sample_time - self.anim_time
        } else {
            (self.anim_duration() - self.anim_time + sample_time).max(0.0)
        };
        self.apply_anim_frame(sample_time, dt);
    }

    pub(crate) fn set_hand_shape(&mut self, side: usize, enabled: bool, a: usize, b: usize, blend: f32) -> bool {
        self.hand_shapes.set(side, enabled, a, b, blend)
    }

    /// Animator sampling and DynamicBone's Time.deltaTime are separate clocks.
    /// Keep particle history across loops, repeated samples and timeline seeks.
    fn shaped_baselines_missing(&self)->bool {
        (self.body_rest.is_some()&&self.body_shaped.is_none()) ||
        (self.head_rest.is_some()&&self.head_shaped.is_none())
    }

    pub fn apply_anim_frame(&mut self,t:f32,delta_seconds:f32){
        if self.game_root.as_os_str().is_empty(){return;}
        let start=std::time::Instant::now();
        self.sample_paired_frame(t);let sampled=start.elapsed();self.apply_body_interaction(delta_seconds);self.finish_paired_dynamics(delta_seconds);
        if std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1") {eprintln!("OK animation frame sample_ms={:.3} dynamics_ms={:.3}",sampled.as_secs_f64()*1000.,(start.elapsed()-sampled).as_secs_f64()*1000.);}
    }
    pub(crate) fn external_animation(&self)->bool {
        self.external_animation.unwrap_or_else(||crate::profile::var("OK_UE_ANIM").ok().as_deref()==Some("1"))
    }
    pub(crate) fn sample_paired_frame(&mut self, t: f32) {
        if self.game_root.as_os_str().is_empty() { return; }
        self.anim_time = t.max(0.0);
        if self.shaped_baselines_missing() {
            let body = self.shape_body.clone();
            let face = self.shape_face.clone();
            self.apply_shape(&body, &face);
            self.sample_paired_frame(t);
            return;
        }
        let profile=std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1");let start=std::time::Instant::now();
        {let _trace=crate::native_profile::Scope::new(15);self.reset_animator_pose();}let reset=start.elapsed();
        // OK_UE_ANIM is process-wide: another actor rebuilding its neutral pose sets
        // it, which must not freeze an installed video clip on this session.
        let sampled = crate::anim::is_shared_root(&self.game_root) || self.anim_clip.starts_with("video|") || !self.external_animation();
        if sampled {
            let _trace=crate::native_profile::Scope::new(16);
            if let Some(sk) = self.body_skel.as_mut() {
                if let Some(fade)=&self.anim_crossfade {fade.sample(&self.game_root,sk,self.anim_time,&self.anim_parameters);}
                else if !self.anim_layer_crossfades.is_empty() {crate::anim::crossfade::sample_layers(&self.game_root,sk,&self.anim_clip,self.anim_time,&self.anim_parameters,&self.anim_layer_crossfades);}
                else {crate::anim::crossfade::sample(&self.game_root,sk,&self.anim_clip,self.anim_time,&self.anim_parameters,self.anim_clock);}
            }
            if self.anim_crossfade.as_ref().is_some_and(|f|f.weight(self.anim_time)==1.) {self.anim_crossfade=None;}
            self.anim_layer_crossfades.retain(|_,f|f.weight(self.anim_time)<1.);
        }
        let sample=start.elapsed();{let _trace=crate::native_profile::Scope::new(17);self.finish_animator_transforms(sampled);}
        if profile {eprintln!("OK animation sample reset_ms={:.3} curves_ms={:.3} modifiers_ms={:.3}",reset.as_secs_f64()*1000.,(sample-reset).as_secs_f64()*1000.,(start.elapsed()-sample).as_secs_f64()*1000.);}
    }

    /// OK animation player: start a frame from the card's reset baseline (the shaped rest of every Animator bone).
    pub(crate) fn ok_begin_frame(&mut self, t: f32) {
        self.anim_time = t.max(0.0);
        if self.shaped_baselines_missing() { let (b, f) = (self.shape_body.clone(), self.shape_face.clone()); self.apply_shape(&b, &f); }
        self.reset_animator_pose();
    }
    /// OK animation player: the character's own finishing pass on the written pose, in the animation's source scene
    /// (limb size corrections, expression bones), then ABMX, interaction and hair/cloth physics.
    pub(crate) fn ok_finish_frame(&mut self, dt: f32, scene: i32) {
        let saved = self.anim_scene_mode;
        self.anim_scene_mode = if (1..=4).contains(&scene) { scene } else if saved != 0 { saved } else { 2 };
        self.finish_animator_pose(dt.max(0.0), true);
        self.anim_scene_mode = saved;
    }
    /// The Animator's own output at `t` for the selected clip (no shape, corrections, IK or physics): what the OK
    /// converter records.
    pub(crate) fn ok_sample_animator(&mut self, t: f32) -> bool {
        if self.game_root.as_os_str().is_empty() { return false; }
        self.ok_begin_frame(t);
        let Some(sk) = self.body_skel.as_mut() else { return false; };
        crate::anim::crossfade::sample(&self.game_root, sk, &self.anim_clip, self.anim_time, &self.anim_parameters, self.anim_clock);
        sk.rebuild_worlds();
        true
    }

    pub(crate) fn refresh_static_interaction(&mut self,dt:f32) {
        self.reset_animator_pose();
        self.finish_animator_pose(dt,false);
    }

    fn reset_animator_pose(&mut self) {
        // Portable packs saved without shaped baselines must not reset the
        // live skeleton to None: clothes skinned on it collapse to the origin.
        if self.shaped_baselines_missing() {
            let body=self.shape_body.clone();let face=self.shape_face.clone();
            self.apply_shape(&body,&face);
        }
        match (&mut self.body_skel,&self.body_shaped){(Some(current),Some(base))=>current.restore_from(base),(_,Some(base))=>self.body_skel=Some(base.clone()),_=>{}};
        match (&mut self.head_skel,&self.head_shaped){(Some(current),Some(base))=>current.restore_from(base),(_,Some(base))=>self.head_skel=Some(base.clone()),_=>{}};
        // head_shaped is captured BEFORE ABMX in apply_shape. The game applies
        // BoneController after animation every frame; resetting this tree must
        // restore the head modifiers too, including non-rendered EyeTargets.
        if let Some(head)=self.head_skel.as_mut() {
            let baseline=crate::pose::capture_abm_baseline(head,&self.abm);
            crate::pose::apply_abm(head,&self.abm,0,&baseline);
        }
    }

    fn finish_animator_pose(&mut self,delta_seconds:f32,sampled:bool){
        self.finish_animator_transforms(sampled);
        let t=std::time::Instant::now();
        self.apply_body_interaction(delta_seconds);
        crate::okanim::prof::add(17,t);
        self.finish_paired_dynamics(delta_seconds);
    }

    pub(crate) fn apply_body_interaction(&mut self,dt:f32){
        let (Some(interaction),Some(body))=(&mut self.body_interaction,&mut self.body_skel) else{return;};
        if !interaction.reaction.is_playing(){return;}
        let mut sk=crate::finalik::IkSkeleton::new_batched(body);
        let root=sk.world_rot("cf_j_root").unwrap_or(crate::finalik::IDENTITY);
        let offsets=interaction.offsets(dt,root);
        // A standalone reaction preserves the sampled animation pose. Paired
        // playback instead adds these offsets to its existing contact solver.
        let solver=&mut interaction.solver;
        for (effector,offset) in solver.effectors.iter_mut().zip(offsets){
            effector.position_weight=0.;effector.rotation_weight=0.;effector.position_offset=offset;
        }
        solver.update(&mut sk);
        // Batched IK changes local TRS without publishing world matrices.
        // Standalone playback must publish before physics and skin export,
        // exactly as the paired solver does after removing its proxies.
        sk.sk.rebuild_worlds();
    }

    fn finish_animator_transforms(&mut self, sampled: bool) {
        use crate::okanim::prof;
        if let Some(sk) = self.body_skel.as_mut() {
            let mut locals = false;
            let t = std::time::Instant::now();
            if sampled {
                if self.sex == 1 {
                    crate::shape::apply_body_shape_always_locals(sk, self.body_shaped.as_ref().unwrap());
                    locals = true;
                    let (arm, hand) = crate::anim::clip_limb_corrections_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode);
                    crate::pose::apply_scene_limb_size_corrections_locals(sk, arm, hand);
                }
            }
            prof::add(12, t);
            let t = std::time::Instant::now();
            locals |= self.hand_shapes.apply_cached_locals(sk, &mut self.hand_shape_ids);
            locals |= apply_user_fk_locals(sk, &self.user_fk, &mut self.user_fk_ids);
            // Shape, limb sizes, hand shapes and FK change local transforms only: the worlds are published once.
            if locals { sk.rebuild_worlds(); }
            prof::add(13, t);
            let t = std::time::Instant::now();
            if !self.game_root.as_os_str().is_empty() && crate::anim::clip_expression_bone_mode(&self.game_root, &self.anim_clip, self.anim_scene_mode) {
                if let Some(config)=&self.pv_config {config.apply(sk,self.pv_mask,self.pv_fk || !self.user_fk.is_empty());}
                crate::pose::apply_expression_bone(sk);
            }
            prof::add(14, t);
            let t = std::time::Instant::now();
            let base = crate::pose::capture_abm_baseline(sk,&self.abm);
            crate::pose::apply_abm(sk, &self.abm, 0, &base);
            prof::add(15, t);
            let t = std::time::Instant::now();
            // Studio IK after the bone modifiers: a target is where the limb ends up.
            apply_studio_ik(sk, &mut self.studio_ik_solver, &self.studio_ik, &self.studio_ik_rotations);
            prof::add(16, t);
        }
    }
    /// Builds the Studio IK solver from the shared resources (FullBodyBipedIK config and body-bone prefab).
    pub(crate) fn prepare_studio_ik(&mut self, root: &std::path::Path) -> Result<(), String> {
        if self.studio_ik_solver.is_none() {
            let mut solver = crate::h_playback::body_reaction_solver(self, root)?;
            // Studio posing places limbs exactly; the game prefab iteration count is tuned for animated contacts.
            solver.iterations = solver.iterations.max(12);
            self.studio_ik_solver = Some(solver);
        }
        Ok(())
    }
    /// Skeleton-space position of an IK effector's bone in the current pose.
    pub(crate) fn ik_effector_position(&self, effector: usize) -> Option<[f32; 3]> {
        let solver = self.studio_ik_solver.as_ref()?;
        // Bend goals sit at the limb's middle joint (elbow / knee).
        let bone = if effector >= STUDIO_BEND_FIRST { solver.bend_constraints.get(effector - STUDIO_BEND_FIRST + 1)?.bone2.clone()? } else { solver.effectors.get(effector)?.bone.clone()? };
        let sk = self.body_skel.as_ref()?;
        let id = body_bone_id(sk, &bone)?;
        let i = sk.tfs.get(&id).map(|t| t.name.clone())?;
        sk.world_tf(&i).map(|m| [m[0][3], m[1][3], m[2][3]])
    }
    /// Local rotation (Unity Euler degrees) of a body bone as currently posed (FK editing starts from it).
    pub(crate) fn body_bone_euler(&self, name: &str) -> Option<[f32; 3]> {
        let sk = self.body_skel.as_ref()?;
        let id = body_bone_id(sk, name)?;
        Some(crate::pose::quat_to_unity_euler(sk.tfs.get(&id)?.rot))
    }
    pub(crate) fn finish_paired_dynamics(&mut self,delta_seconds:f32){
        // Parts the owner draws on the GPU (scene_gpu_parts) are solved but not skinned on the CPU.
        let gpu=std::mem::take(&mut self.scene_gpu_parts);
        self.finish_paired_dynamics_filtered(delta_seconds,None,&gpu);
        self.scene_gpu_parts=gpu;
    }

    pub(crate) fn finish_paired_dynamics_observed(&mut self,delta_seconds:f32,observer:Option<crate::ffi::PrivateSkinObserver>){
        self.finish_paired_dynamics_filtered(delta_seconds,observer,&[]);
    }
    pub(crate) fn finish_paired_dynamics_filtered(&mut self,delta_seconds:f32,observer:Option<crate::ffi::PrivateSkinObserver>,excluded:&[usize]){
        let profiling=std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1");let start=std::time::Instant::now();
        {let _trace=crate::native_profile::Scope::new(4);
        if let Some(body)=self.body_skel.as_mut() { self.head_look.apply_preserving_fk(body,delta_seconds,&self.user_fk); crate::dbone::apply_head_tilt(body, crate::px::debug_head_tilt()); }
        self.update_body_dynamics(delta_seconds);}
        let body=start.elapsed();
        {let _trace=crate::native_profile::Scope::new(5);self.update_clothes_dynamics(delta_seconds);}
        let clothes=start.elapsed();
        {let _trace=crate::native_profile::Scope::new(6);if let Some(mut ex) = self.skel_export.take() {
            crate::skel_export::refresh_pose(self, &mut ex);
            self.skel_export = Some(ex);
        }}
        let exported=start.elapsed();
        {let _trace=crate::native_profile::Scope::new(7);self.update_dynamic_accessories_filtered(delta_seconds,crate::anim::is_shared_root(&self.game_root),true,observer,excluded);}
        crate::okanim::prof::add_us(7, body.as_micros() as u64);
        crate::okanim::prof::add_us(8, (clothes - body).as_micros() as u64);
        crate::okanim::prof::add_us(9, (exported - clothes).as_micros() as u64);
        crate::okanim::prof::add_us(10, (start.elapsed() - exported).as_micros() as u64);
        if profiling {eprintln!("OK animation dynamics body_ms={:.3} clothes_ms={:.3} export_ms={:.3} accessories_ms={:.3} rest_ms={:.3}",body.as_secs_f64()*1000.,(clothes-body).as_secs_f64()*1000.,(exported-clothes).as_secs_f64()*1000.,(start.elapsed()-exported).as_secs_f64()*1000.,(start.elapsed()-clothes).as_secs_f64()*1000.);}
    }

    /// External Animator locals, before ShapeBody.UpdateAlways and ABMX.
    /// Validate the entire frame before touching either pose or particle history.
    pub fn apply_external_body_frame(&mut self, t: f32, dt: f32, poses: &[crate::ffi::OkBodyPose]) -> bool {
        if !t.is_finite() || !dt.is_finite() || dt < 0.0 { return false; }
        let Some(shaped) = self.body_shaped.as_ref() else { return false; };
        let Some(export) = self.skel_export.as_ref() else { return false; };
        let mut resolved = Vec::with_capacity(poses.len());
        let mut seen = std::collections::HashSet::new();
        for p in poses {
            let Some(b) = export.bones.get(p.bone as usize) else { return false; };
            if b.pose_locked || !seen.insert(p.bone) { return false; }
            let Some((&id, _)) = shaped.tfs.iter().find(|(_,tf)|tf.name.eq_ignore_ascii_case(&b.name)) else { return false; };
            let q2: f32 = p.rotation.iter().map(|v|v*v).sum();
            if !p.position.iter().chain(&p.rotation).chain(&p.scale).all(|v|v.is_finite()) ||
                !q2.is_finite() || q2 < 1e-12 || p.scale.iter().any(|v|v.abs()<1e-8) { return false; }
            resolved.push((id,p,q2.sqrt()));
        }
        self.anim_time = t.max(0.0);
        self.reset_animator_pose();
        let body = self.body_skel.as_mut().unwrap();
        for (id,p,length) in resolved {
            let tf = body.tfs.get_mut(&id).unwrap();
            tf.pos = p.position;
            // Preserve source float quaternions that are already unit length.
            // Renormalizing them perturbs long collision-chain histories.
            tf.rot = if (length-1.).abs()<=1e-6 {p.rotation} else {p.rotation.map(|v|v/length)};
            tf.scale = p.scale;
        }
        // An external UE clip has no Unity source scene. Auto follows this
        // preview's Studio policy; explicit Maker/normal/H/Studio still wins.
        let selected_scene = self.anim_scene_mode;
        if selected_scene == 0 { self.anim_scene_mode = 4; }
        self.finish_animator_pose(dt, true);
        self.anim_scene_mode = selected_scene;
        true
    }

    pub fn anim_duration(&self) -> f32 {
        if let Some(fade)=self.anim_crossfade.as_ref().filter(|f|f.layer.is_none()) {return fade.source_length;}
        crate::anim::clip_duration_parameters(&self.game_root, &self.anim_clip,&self.anim_parameters)
    }

    pub(crate) fn set_anim_float(&mut self,name:&str,value:f32)->bool {
        if self.is_anim_transitioning()||!value.is_finite()||!(crate::anim::animation_float_parameters(&self.game_root,&self.anim_clip).iter().any(|p|p.name==name)||crate::anim::crossfade::declared_float(&self.game_root,&self.anim_clip,name)) {return false;}
        let previous=self.anim_duration();
        let mut candidate=self.anim_parameters.clone();
        candidate.insert(name.to_string(),value);
        if !crate::anim::valid_animation_clock(&self.game_root,&self.anim_clip,&candidate) {return false;}
        let current=crate::anim::clip_duration_parameters(&self.game_root,&self.anim_clip,&candidate);
        // A nonpositive/overflowing state clock is not a usable playback
        // duration. Reject it before mutating parameters or particle history.
        if !current.is_finite()||current<=0. {return false;}
        let mut clock=self.anim_clock;
        if clock!=crate::anim::crossfade::Clock::default() {
            let phase=((self.anim_time as f64-self.anim_clock.epoch as f64)/previous as f64+self.anim_clock.phase as f64) as f32;
            if !phase.is_finite(){return false;}
            clock=crate::anim::crossfade::Clock{epoch:self.anim_time,phase};
        }
        self.anim_parameters=candidate;self.anim_clock=clock;
        if clock==crate::anim::crossfade::Clock::default()&&previous>0. {self.anim_time*=current/previous;}
        true
    }

    pub(crate) fn crossfade(&mut self,to:&str,blend:f32,offset:f32)->bool {
        if self.is_anim_transitioning() {return false;}
        let Some(fade)=crate::anim::crossfade::CrossFade::new(&self.game_root,&self.anim_clip,to,&self.anim_parameters,self.anim_clock,self.anim_time,blend,offset) else{return false;};
        self.anim_clip=to.into();self.anim_clock=fade.to_clock;
        self.anim_crossfade=(fade.seconds>0.).then_some(fade);true
    }

    pub(crate) fn reset_anim_clock(&mut self) {self.anim_clock=Default::default();self.anim_crossfade=None;self.anim_layer_crossfades.clear();}
    pub(crate) fn is_anim_transitioning(&self)->bool {self.anim_crossfade.is_some()||!self.anim_layer_crossfades.is_empty()}
    pub(crate) fn anim_layer_transition(&self,index:usize)->Option<&crate::anim::crossfade::CrossFade> {
        if index==0 {self.anim_crossfade.as_ref()}else{self.anim_layer_crossfades.get(&index)}
    }

    pub(crate) fn crossfade_layer(&mut self,to:&str,index:usize,blend:f32,offset:f32)->bool {
        if self.anim_crossfade.is_some()||self.anim_clock!=Default::default()||self.anim_layer_crossfades.contains_key(&index) {return false;}
        let Some(fade)=crate::anim::crossfade::CrossFade::new_layer(&self.game_root,&self.anim_clip,to,index,&self.anim_parameters,self.anim_time,blend,offset) else{return false;};
        let mut parameters=self.anim_parameters.clone();
        parameters.insert(crate::anim::layer_start_time_name(index),fade.to_clock.epoch);
        parameters.insert(crate::anim::layer_start_phase_name(index),fade.to_clock.phase);
        if !crate::anim::valid_animation_clock(&self.game_root,to,&parameters) {return false;}
        self.anim_clip=to.into();self.anim_parameters=parameters;
        if fade.seconds>0. {self.anim_layer_crossfades.insert(index,fade);}true
    }

    pub(crate) fn set_anim_trigger(&mut self,name:&str)->bool {
        if self.is_anim_transitioning() {return false;}
        let Some(candidate)=crate::anim::animation_trigger_parameters(&self.game_root,&self.anim_clip,&self.anim_parameters,name,self.anim_time) else {return false;};
        let duration=crate::anim::clip_duration_parameters(&self.game_root,&self.anim_clip,&candidate);
        if !duration.is_finite()||duration<=0. {return false;}
        self.anim_parameters=candidate;
        // Re-entry consumes the event immediately. The caller samples at this
        // same elapsed time; base/sync clocks, layer weights and physics stay.
        true
    }

    pub fn skin_all(&mut self) {self.skin_all_impl(true);}
    pub(crate) fn skin_display_except(&mut self,excluded:&[usize]) {self.skin_all_impl_filtered(true,None,excluded);}
    pub(crate) fn query_private_geometry(&mut self,index:usize)->bool {
        let policy=AccessoryFramePolicy::read();
        let body=self.body_skel.as_ref();
        let clothes=self.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel).or(body);
        let head=self.head_full_posed.as_ref().or(self.head_skel.as_ref());
        let Some(part)=self.parts.get_mut(index) else{return false;};
        let Some(runtime)=part.runtime_accessory.as_ref() else{return false;};
        let Some((positions,normals,tangents))=runtime.render_solved(runtime.skin_body(body,clothes),head,policy,None) else{return false;};
        part.positions=positions;part.normals=normals;part.rest_tangents=tangents;true
    }
    pub(crate) fn query_private_geometries(&mut self,indices:&[usize])->bool {
        let mut unique=std::collections::HashSet::new();
        if indices.iter().any(|i|!unique.insert(*i)||self.parts.get(*i).and_then(|p|p.runtime_accessory.as_ref()).is_none()){return false;}
        let policy=AccessoryFramePolicy::read();let body=self.body_skel.as_ref();
        let clothes=self.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel).or(body);
        let head=self.head_full_posed.as_ref().or(self.head_skel.as_ref());
        let read=|index:&usize| {let runtime=self.parts[*index].runtime_accessory.as_ref()?;runtime.render_solved(runtime.skin_body(body,clothes),head,policy,None)};
        // All workers read the same solved pose. Publish only after every query
        // succeeds; this path never advances animation or particle histories.
        let results:Option<Vec<_>>=if indices.len()>=4 {use rayon::prelude::*;native_parallel_work(||indices.par_iter().map(read).collect())}else{indices.iter().map(read).collect()};
        let Some(results)=results else{return false;};
        for(index,(positions,normals,tangents))in indices.iter().zip(results){let part=&mut self.parts[*index];part.positions=positions;part.normals=normals;part.rest_tangents=tangents;}
        true
    }

    fn skin_all_impl(&mut self,reuse_buffers:bool) {self.skin_all_impl_observed(reuse_buffers,None);}

    pub(crate) fn skin_all_observed(&mut self,observer:Option<crate::ffi::PrivateSkinObserver>){self.skin_all_impl_observed(true,observer);}

    fn skin_all_impl_observed(&mut self,reuse_buffers:bool,observer:Option<crate::ffi::PrivateSkinObserver>) {
        self.skin_all_impl_filtered(reuse_buffers,observer,&[]);
    }
    fn skin_all_impl_filtered(&mut self,reuse_buffers:bool,observer:Option<crate::ffi::PrivateSkinObserver>,excluded:&[usize]) {
        let profile=std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1");
        let mut prep_ms=0.;let mut skin_ms=0.;let mut vertices=0usize;let mut count=0usize;
        // Explicit CPU geometry queries remain supported for shared sessions;
        // refresh render vertices from the already solved pose, without time
        // advancement. Ordinary shared playback only consumes the palette.
        if crate::anim::is_shared_root(&self.game_root) {
            self.render_solved_accessories_filtered(observer,excluded);
        }
        // Smoothing mutates weights/indices in the temporary mesh. Preserve
        // the authored inputs when this optional diagnostic mode is enabled.
        let reuse_weights=reuse_buffers && std::env::var("OK_SMOOTH").ok().and_then(|s|s.parse::<usize>().ok()).unwrap_or(0)==0;
        let skip_gpu = crate::profile::var("OK_SKELMESH").ok().as_deref() == Some("1");
        for part in &mut self.parts {
            if part.runtime_accessory.is_some() { continue; }
            if skip_gpu && (part.skel_kind == 1 || part.skel_kind == 2 || part.skel_kind == 3) {
                continue;
            }
            let begin=std::time::Instant::now();
            part.positions.clone_from(&part.rest_positions);
            part.normals.clone_from(&part.rest_normals);
            let mut mesh = ExtractedMesh {
                runtime_textures: Default::default(), texture_plan: Vec::new(),
                shader_extra_textures: Default::default(),
                name: part.name.clone(),
                obj: String::new(),
                path_id: 0,
                renderer_ids: Vec::new(),
                renderer_names: Vec::new(),
                renderer_skinned: None,
                mat: None,
                submesh_ranges: Vec::new(),
                submesh_index: 0,
                hair_shader_hint: false,
                texture_png: None,
                texture_name: String::new(),
                color_mask_png: None,
                alpha_mask_png: None,
                detail_mask_png: None,
                line_mask_png: None,
                normal_map_png: None,
                normal_mask_png: None,
                source_tangents: None,
                tangents: Vec::new(),
                hair_gloss_png: None,
                positions: if reuse_buffers {std::mem::take(&mut part.positions)} else {part.positions.clone()},
                normals: if reuse_buffers {std::mem::take(&mut part.normals)} else {part.normals.clone()},
                rest_normals: if reuse_buffers {std::mem::take(&mut part.rest_normals)} else {part.rest_normals.clone()},
                uvs: if reuse_buffers {Vec::new()} else {part.uvs.clone()},
                uv2: if reuse_buffers {Vec::new()} else {part.uv2.clone()},
                uv3: if reuse_buffers {Vec::new()} else {part.uv3.clone()},
                uv4: if reuse_buffers {Vec::new()} else {part.uv4.clone()},
                vertex_colors: if reuse_buffers {Vec::new()} else {part.vertex_colors.clone()},
                indices: if reuse_buffers {std::mem::take(&mut part.indices)} else {part.indices.clone()},
                weights: if reuse_weights {std::mem::take(&mut part.weights)} else {part.weights.clone()},
                bone_idx: if reuse_weights {std::mem::take(&mut part.bone_idx)} else {part.bone_idx.clone()},
                bind_poses: if reuse_buffers {std::mem::take(&mut part.bind_poses)} else {part.bind_poses.clone()},
                bone_names: Vec::new(),
                smr_skel: None,
                bone_implants: Vec::new(),
                rest_positions: if reuse_buffers {std::mem::take(&mut part.rest_positions)} else {part.rest_positions.clone()},
                parent_bone: part.parent_bone.clone(),
                rigid_follow_bone: part.rigid_follow_bone.clone(),
                skel_kind: part.skel_kind,
                blend_shapes: Vec::new(),
                prefab_local: None,
                move_nodes: [None, None],
                deferred_dbones: None,
            };
            let prepared=std::time::Instant::now();
            match part.skel_kind {
                1 => {
                    if let Some(sk) = self.body_skel.as_ref() {
                        let _ = crate::skin_mesh_geometry(&mut mesh, sk);
                    }
                }
                2 => {
                    if let Some(sk) = self.head_skel.as_ref() {
                        let _ = crate::skin_mesh_geometry(&mut mesh, sk);
                    }
                    let delta = crate::head_onto_body(self.body_skel.as_ref(), self.head_skel.as_ref());
                    crate::apply_parent_geometry(&mut mesh, &delta);
                }
                3 => {
                    // Full head/hair meshes use their own prefab skeleton.
                    // Leaving these at rest while the face follows the walk
                    // produces a false CPU/GPU comparison and detached hair.
                    if let Some(sk) = self.head_full_posed.as_ref() {
                        let _ = crate::skin_mesh_geometry(&mut mesh, sk);
                        let delta = crate::head_onto_body(self.body_skel.as_ref(), Some(sk));
                        crate::apply_parent_geometry(&mut mesh, &delta);
                    }
                }
                _ => {}
            }
            if let Some(bone) = part.parent_bone.as_deref() {
                if let Some(m) = crate::bone_world(self.body_skel.as_ref(), self.head_skel.as_ref(), bone)
                {
                    crate::apply_parent_geometry(&mut mesh, &m);
                }
            }
            if profile {prep_ms+=(prepared-begin).as_secs_f64()*1000.;skin_ms+=prepared.elapsed().as_secs_f64()*1000.;vertices+=mesh.positions.len();count+=1;}
            if reuse_buffers {
                part.rest_positions=mesh.rest_positions;
                part.rest_normals=mesh.rest_normals;
                part.indices=mesh.indices;
                part.bind_poses=mesh.bind_poses;
            }
            if reuse_weights {part.weights=mesh.weights;part.bone_idx=mesh.bone_idx;}
            part.positions = mesh.positions;
            part.normals = mesh.normals;
        }
        if profile {eprintln!("OK animation cpu_mesh parts={count} vertices={vertices} cloning_ms={prep_ms:.3} skin_ms={skin_ms:.3}");}
    }

    /// Portable captures may have been exported by a palette-only crowd host.
    /// Their private-rig vertices are prefab-local until evaluated for the CPU
    /// procedural display. Solve the captured pose without advancing time.
    pub(crate) fn refresh_portable_render(&mut self) {
        self.update_dynamic_accessories_impl(0.0, false);
    }

    /// Split immutable assets from character parameters without changing the game cache layout.
    pub(crate) fn split_preview_resources(&self, card: &crate::card::CardInfo,
        save: impl FnMut(Option<usize>, &str, &str, Vec<u8>, bool) -> Result<(),String>) -> Result<Vec<u8>,String> {
        self.split_preview_resources_selected(card,true,save)
    }

    /// A renderer patch retains the base globals. Do not clone or serialize
    /// global extraction inputs just to discard them in the resource callback.
    pub(crate) fn split_preview_part_resources(&self, card: &crate::card::CardInfo,
        save: impl FnMut(Option<usize>, &str, &str, Vec<u8>, bool) -> Result<(),String>) -> Result<Vec<u8>,String> {
        self.split_preview_resources_selected(card,false,save)
    }

    fn split_preview_resources_selected(&self, card: &crate::card::CardInfo, include_globals:bool,
        mut save: impl FnMut(Option<usize>, &str, &str, Vec<u8>, bool) -> Result<(),String>) -> Result<Vec<u8>,String> {
        fn take<T: serde::Serialize + Default>(value: &mut T) -> Result<Option<Vec<u8>>,String> {
            let raw=bincode::serialize(value).map_err(|e|e.to_string())?;
            let default=bincode::serialize(&T::default()).map_err(|e|e.to_string())?;
            *value=T::default(); Ok((raw!=default).then_some(raw))
        }
        let mut snapshot=PreviewSnapshot {session:SessionBlob::from_session_selected(self,include_globals),
            collider_settings:if include_globals {self.collider_settings} else {Default::default()},
            hand_shapes:if include_globals {self.hand_shapes.clone()} else {Default::default()}};
        snapshot.session.game_root=PathBuf::new();
        macro_rules! global {($field:ident) => {
            if let Some(raw)=take(&mut snapshot.session.$field)? {save(None,stringify!($field),stringify!($field),raw,true)?;}
        };}
        if include_globals {
        global!(pv_config);
        global!(eye_look_config);
        global!(ramp_png);
        global!(body_rest);
        global!(body_dynamics);
        global!(body_colliders);
        global!(clothes_dynamics);
        global!(head_rest);
        global!(body_keys);
        global!(head_keys);
        global!(correct);
        global!(head_full_rest);
        if let Some(context)=self.accessory_context.as_deref(){
            save(None,"accessory_context","accessory_context",bincode::serialize(context).map_err(|e|e.to_string())?,true)?;
        }
        save(None,"collider_settings","collider_settings",bincode::serialize(&snapshot.collider_settings).map_err(|e|e.to_string())?,true)?;
        snapshot.collider_settings=Default::default();
        save(None,"hand_shapes","hand_shapes",bincode::serialize(&snapshot.hand_shapes).map_err(|e|e.to_string())?,true)?;
        snapshot.hand_shapes=Default::default();
        }
        let custom:Vec<&[u8]>=card.me_tex.values().map(|v|v.as_slice())
            .chain(card.ksox.values().map(Vec::as_slice)).chain(card.clothes_masks.values().map(Vec::as_slice))
            .chain(card.clothes_overlays.values().map(|v|v.0.as_slice())).collect();
        for (i,p) in snapshot.session.parts.iter_mut().enumerate() {
            if let Some(property)=&self.parts[i].source_property {
                validate_part_origin(property)?;
                save(Some(i),"part_origin","part_origin",bincode::serialize(property).map_err(|e|e.to_string())?,true)?;
            }
            macro_rules! model {($field:ident) => {
                if let Some(raw)=take(&mut p.$field)? {save(Some(i),stringify!($field),stringify!($field),raw,true)?;}
            };}
            if let Some(raw)=take(&mut p.positions)? {
                let shared=self.parts[i].positions==self.parts[i].rest_positions;
                save(Some(i),"positions",if shared {"rest_positions"} else {"positions"},raw,shared)?;
            }
            if let Some(raw)=take(&mut p.normals)? {
                let shared=self.parts[i].normals==self.parts[i].rest_normals;
                save(Some(i),"normals",if shared {"rest_normals"} else {"normals"},raw,shared)?;
            }
            if let Some(runtime)=p.runtime_accessory.as_mut() {
                runtime.source_validation = PrivateSourceValidation::default();
                if let Some(raw)=take(&mut runtime.source)? {save(Some(i),"accessory_source","accessory_model",raw,true)?;}
            }
            model!(rest_positions);
            model!(rest_normals);
            model!(rest_tangents);
            model!(uvs);
            model!(uv2);
            model!(uv3);
            model!(uv4);
            model!(vertex_colors);
            model!(indices);
            model!(weights);
            model!(bone_idx);
            model!(bind_poses);
            model!(blend_names);
            model!(blend_deltas);
            model!(morph_idx);
            model!(morph_dxyz);
            if let Some(mat)=p.source_material.as_mut() {
                let mut definition=mat.clone();definition.colors.clear();definition.floats.clear();
                let colors=std::mem::take(&mut mat.colors);let floats=std::mem::take(&mut mat.floats);
                *mat=crate::unity::MeshMaterial::default();mat.colors=colors;mat.floats=floats;
                save(Some(i),"material_definition","material",bincode::serialize(&definition).map_err(|e|e.to_string())?,true)?;
            }
            let material=self.parts[i].source_material.as_ref().map(|m|crate::card::me_mat_key(&m.name)).unwrap_or_default();
            let is_unique=|slot:&str,bytes:&[u8]| {
                let overrides=card.me_tex.keys().any(|(_,_,_,mat,property)|
                    mat==&material && property.eq_ignore_ascii_case(slot));
                let generated=slot=="_MainTex" && p.runtime_textures.contains_key(slot) &&
                    (p.role=="body" || p.role=="head" || p.role.starts_with("clothes"));
                let mask=slot=="_AlphaMask" && !card.clothes_masks.is_empty() &&
                    (p.role=="body" || p.role.starts_with("clothes"));
                overrides || generated || mask || custom.iter().any(|source|*source==bytes)
            };
            macro_rules! texture {($field:ident,$slot:literal) => {
                if let Some(bytes)=p.$field.take() {
                    let shared=!is_unique($slot,&bytes);
                    save(Some(i),stringify!($field),"texture",bincode::serialize(&bytes).map_err(|e|e.to_string())?,shared)?;
                }
            };}
            texture!(png,"_MainTex");
            texture!(alpha_mask,"_AlphaMask");
            texture!(hair_gloss,"_HairGloss");
            texture!(detail_mask,"_DetailMask");
            texture!(line_mask,"_LineMask");
            texture!(normal_map,"_NormalMap");
            texture!(normal_detail,"_NormalMapDetail");
            texture!(normal_mask,"_NormalMask");
            texture!(overtex_png,"_overtex");
            texture!(overtex2_png,"_overtex2");
            for (slot,bytes) in std::mem::take(&mut p.shader_textures) {
                let shared=!is_unique(&slot,&bytes);
                save(Some(i),&format!("shader_textures:{slot}"),"texture",bincode::serialize(&bytes).map_err(|e|e.to_string())?,shared)?;
            }
            if let Some(eye)=p.eye_draw.as_mut() {
                if let Some(raw)=take(&mut eye.raw_uvs)? {save(Some(i),"eye_raw_uvs","eye_raw_uvs",raw,true)?;}
                for layer in 0..2 {if let Some(bytes)=eye.textures[layer].take() {
                    let slot=if layer==0 {"overtex1"} else {"overtex2"};
                    save(Some(i),&format!("eye_texture_{layer}"),"texture",bincode::serialize(&bytes).map_err(|e|e.to_string())?,!is_unique(slot,&bytes))?;
                }}
            }
            if let Some(iris)=p.iris_runtime.as_mut() {
                if let Some(raw)=take(&mut iris.raw_uvs)? {save(Some(i),"iris_raw_uvs","uvs",raw,true)?;}
            }
            if let Some(item)=p.item_draw.as_mut() {if let Some(bytes)=item.color_mask.take() {
                let shared=!is_unique("_ColorMask",&bytes);
                save(Some(i),"item_color_mask","texture",bincode::serialize(&bytes).map_err(|e|e.to_string())?,shared)?;
            }}
        }
        bincode::serialize(&snapshot).map_err(|e|e.to_string())
    }

    pub(crate) fn audit_preview_residue(residue:&[u8])->Result<(),String> {
        use bincode::Options;
        let snapshot:PreviewSnapshot=bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize(residue).map_err(|e|e.to_string())?;
        for p in snapshot.session.parts {
            if p.runtime_accessory.as_ref().is_some_and(|r|!r.source.positions.is_empty() || !r.source.indices.is_empty()) {
                return Err("card residue contains an accessory model".into());
            }
            if !p.positions.is_empty() || !p.normals.is_empty() || !p.rest_positions.is_empty() || !p.rest_normals.is_empty() || !p.rest_tangents.is_empty() || !p.uvs.is_empty() || !p.uv2.is_empty() || !p.uv3.is_empty() || !p.uv4.is_empty() || !p.vertex_colors.is_empty() || !p.indices.is_empty() || !p.weights.is_empty() || !p.bone_idx.is_empty() || !p.bind_poses.is_empty() || !p.blend_names.is_empty() || !p.blend_deltas.is_empty() || !p.morph_idx.is_empty() || !p.morph_dxyz.is_empty() || p.png.is_some() || p.alpha_mask.is_some() || p.hair_gloss.is_some() || p.detail_mask.is_some() || p.line_mask.is_some() || p.normal_map.is_some() || p.normal_detail.is_some() || p.normal_mask.is_some() || p.overtex_png.is_some() || p.overtex2_png.is_some() || !p.shader_textures.is_empty() {return Err("card residue contains embedded model or texture assets".into());}
            if p.source_material.as_ref().is_some_and(|m|!m.shader.is_empty() || !m.tex_slots.is_empty()) {
                return Err("card residue contains a material definition".into());
            }
            if p.eye_draw.as_ref().is_some_and(|eye|eye.textures.iter().any(Option::is_some) || eye.raw_uvs.iter().any(|uv|!uv.is_empty())) ||
                p.iris_runtime.as_ref().is_some_and(|iris|!iris.raw_uvs.is_empty()) {
                return Err("card residue contains shared eye assets".into());
            }
            if p.item_draw.as_ref().is_some_and(|m|m.color_mask.is_some()) {return Err("card residue contains a color mask".into());}
        }
        if snapshot.session.body_rest.is_some() || snapshot.session.head_rest.is_some() ||
            !snapshot.session.body_keys.is_empty() || !snapshot.session.head_keys.is_empty() || snapshot.session.ramp_png.is_some() {
            return Err("card residue contains shared rig/shape/ramp assets".into());
        }
        Ok(())
    }

    pub(crate) fn merge_preview_resources(residue:&[u8], references:&[crate::portable_resources::ResourceRef],
        mut resolve:impl FnMut(&crate::portable_resources::ResourceRef)->Result<Vec<u8>,String>) -> Result<Self,String> {
        use bincode::Options;
        fn read<T:serde::de::DeserializeOwned>(raw:&[u8])->Result<T,String> {
            bincode::DefaultOptions::new().with_fixint_encoding().with_limit(crate::portable::MAX_BYTES)
                .reject_trailing_bytes().deserialize(raw).map_err(|e|e.to_string())
        }
        let mut snapshot:PreviewSnapshot=read(residue)?;
        let mut origins=std::collections::BTreeMap::new();
        let mut accessory_context=None;
        if !snapshot.session.game_root.as_os_str().is_empty() {return Err("shared card has a game root".into());}
        for reference in references {
            let raw=resolve(reference)?;
            if let Some(i)=reference.part {
                let p=snapshot.session.parts.get_mut(i).ok_or("resource part index out of range")?;
                match reference.field.as_str() {
                    "part_origin" => {let property:String=read(&raw)?;validate_part_origin(&property)?;if origins.insert(i,property).is_some(){return Err("duplicate part origin".into());}},
                    "positions" => p.positions=read(&raw)?,
                    "normals" => p.normals=read(&raw)?,
                    "accessory_source" => p.runtime_accessory.as_mut().ok_or("accessory placement missing")?.replace_source(read(&raw)?),
                    "rest_positions" => p.rest_positions=read(&raw)?,
                    "rest_normals" => p.rest_normals=read(&raw)?,
                    "rest_tangents" => p.rest_tangents=read(&raw)?,
                    "uvs" => p.uvs=read(&raw)?,
                    "uv2" => p.uv2=read(&raw)?,
                    "uv3" => p.uv3=read(&raw)?,
                    "uv4" => p.uv4=read(&raw)?,
                    "vertex_colors" => p.vertex_colors=read(&raw)?,
                    "indices" => p.indices=read(&raw)?,
                    "weights" => p.weights=read(&raw)?,
                    "bone_idx" => p.bone_idx=read(&raw)?,
                    "bind_poses" => p.bind_poses=read(&raw)?,
                    "blend_names" => p.blend_names=read(&raw)?,
                    "blend_deltas" => p.blend_deltas=read(&raw)?,
                    "morph_idx" => p.morph_idx=read(&raw)?,
                    "morph_dxyz" => p.morph_dxyz=read(&raw)?,
                    "png" => p.png=Some(read(&raw)?),
                    "alpha_mask" => p.alpha_mask=Some(read(&raw)?),
                    "hair_gloss" => p.hair_gloss=Some(read(&raw)?),
                    "detail_mask" => p.detail_mask=Some(read(&raw)?),
                    "line_mask" => p.line_mask=Some(read(&raw)?),
                    "normal_map" => p.normal_map=Some(read(&raw)?),
                    "normal_detail" => p.normal_detail=Some(read(&raw)?),
                    "normal_mask" => p.normal_mask=Some(read(&raw)?),
                    "overtex_png" => p.overtex_png=Some(read(&raw)?),
                    "overtex2_png" => p.overtex2_png=Some(read(&raw)?),
                    "material_definition" => {
                        let mut definition:crate::unity::MeshMaterial=read(&raw)?;
                        let parameters=p.source_material.as_mut().ok_or("material parameters missing")?;
                        definition.colors=std::mem::take(&mut parameters.colors);definition.floats=std::mem::take(&mut parameters.floats);
                        *parameters=definition;
                    }
                    "eye_raw_uvs" => p.eye_draw.as_mut().ok_or("eye material missing")?.raw_uvs=read(&raw)?,
                    "eye_texture_0" => p.eye_draw.as_mut().ok_or("eye material missing")?.textures[0]=Some(read(&raw)?),
                    "eye_texture_1" => p.eye_draw.as_mut().ok_or("eye material missing")?.textures[1]=Some(read(&raw)?),
                    "iris_raw_uvs" => p.iris_runtime.as_mut().ok_or("iris parameters missing")?.raw_uvs=read(&raw)?,
                    "item_color_mask" => p.item_draw.as_mut().ok_or("item material missing")?.color_mask=Some(read(&raw)?),
                    field if field.starts_with("shader_textures:") => {
                        p.shader_textures.insert(field[16..].into(),read(&raw)?);
                    }
                    _ => return Err("unsupported part resource field".into()),
                }
            } else {match reference.field.as_str() {
                "accessory_context" => {
                    let context:crate::accessory_extract::Context=read(&raw)?;
                    if context.schema!=1{return Err("unsupported accessory context schema".into());}
                    accessory_context=Some(std::sync::Arc::new(context));
                },
                "pv_config" => snapshot.session.pv_config=read(&raw)?,
                "eye_look_config" => snapshot.session.eye_look_config=read(&raw)?,
                "ramp_png" => snapshot.session.ramp_png=read(&raw)?,
                "body_rest" => snapshot.session.body_rest=read(&raw)?,
                "body_dynamics" => snapshot.session.body_dynamics=read(&raw)?,
                "body_colliders" => snapshot.session.body_colliders=read(&raw)?,
                "clothes_dynamics" => snapshot.session.clothes_dynamics=read(&raw)?,
                "head_rest" => snapshot.session.head_rest=read(&raw)?,
                "body_keys" => snapshot.session.body_keys=read(&raw)?,
                "head_keys" => snapshot.session.head_keys=read(&raw)?,
                "correct" => snapshot.session.correct=read(&raw)?,
                "head_full_rest" => snapshot.session.head_full_rest=read(&raw)?,
                "collider_settings" => snapshot.collider_settings=read(&raw)?,
                "hand_shapes" => snapshot.hand_shapes=read(&raw)?,
                _ => return Err("unsupported global resource field".into()),
            }}
        }
        let mut session=snapshot.session.into_session_with_settings(snapshot.collider_settings,snapshot.hand_shapes);
        session.accessory_context=accessory_context;
        for (i,property) in origins {session.parts[i].source_property=Some(property);}
        session.anim_clip.clear();session.coordinate_count=1;Ok(session)
    }

    pub(crate) fn preview_accessory_parts(base:&crate::portable_resources::SplitSnapshot)
        ->Result<std::collections::BTreeSet<usize>,String>{
        use bincode::Options;
        let snapshot:PreviewSnapshot=bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize(&base.residue).map_err(|e|e.to_string())?;
        Ok(snapshot.session.parts.iter().enumerate().filter(|(_,p)|p.role.starts_with("acc")).map(|(i,_)|i).collect())
    }

    /// Replay direct head texture assignments without rebuilding unrelated meshes.
    /// Unsupported material programs retain the full source extraction path.
    pub(crate) fn patch_preview_head_texture(base:&mut crate::portable_resources::SplitSnapshot,
        category:i32, game:&Path, card:&crate::card::CardInfo,
        vanilla:&crate::list::ChaListCatalog, mods:&crate::list::ChaListCatalog,
        guids:&std::collections::HashMap<String,PathBuf>,
        mut resolve:impl FnMut(&crate::portable_resources::ResourceRef)->Result<Vec<u8>,String>)
        ->Result<Option<usize>,String>{
        Self::patch_preview_head_texture_cached(base,category,game,card,vanilla,mods,guids,&mut resolve,&mut std::collections::HashMap::new())
    }
    // Cache lifetime is exactly one card/category preparation. Source inputs
    // from different cards, revisions or game roots can never share a bake.
    pub(crate) fn patch_preview_head_texture_cached(base:&mut crate::portable_resources::SplitSnapshot,
        category:i32, game:&Path, card:&crate::card::CardInfo,
        vanilla:&crate::list::ChaListCatalog, mods:&crate::list::ChaListCatalog,
        guids:&std::collections::HashMap<String,PathBuf>,
        mut resolve:impl FnMut(&crate::portable_resources::ResourceRef)->Result<Vec<u8>,String>,
        cache:&mut std::collections::HashMap<Vec<u8>,(Option<crate::eye_gaze::EyeDraw>,crate::unity::ExtractedMesh)>)
        ->Result<Option<usize>,String>{
        use bincode::Options;
        let profile=std::env::var_os("OK_APPEARANCE_PROFILE").is_some();
        let profile_start=std::time::Instant::now();
        let mut snapshot:PreviewSnapshot=bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize(&base.residue).map_err(|e|e.to_string())?;
        if profile{eprintln!("APPEARANCE_PROFILE category={category} phase=residue_decode ms={:.3} bytes={}",profile_start.elapsed().as_secs_f64()*1000.,base.residue.len());}
        let mut changes=Vec::new();
        for(index,part)in snapshot.session.parts.iter_mut().enumerate(){
            if part.role!="head"{continue;}
            let name=part.name.to_ascii_lowercase();
            if !crate::head_detail_source_spec(card,&name).is_some_and(|(cat,_,_,_)|cat==category||(cat==408&&matches!(category,409..=411))){continue;}
            let Some(reference)=base.references.iter().find(|r|r.part==Some(index)&&r.field=="material_definition")else{return Ok(None)};
            // The immutable resource ID owns the source definition; residue
            // parameters own its overrides. Check the per-preparation cache
            // before resolving/deserializing the same material in every outfit.
            let parameters=part.source_material.as_ref().map(|material|(
                material.colors.iter().collect::<std::collections::BTreeMap<_,_>>(),
                material.floats.iter().collect::<std::collections::BTreeMap<_,_>>()));
            let key=bincode::serialize(&(category,&part.name,part.shader_kind,part.eye_draw.is_some(),
                &reference.id,parameters)).map_err(|e|e.to_string())?;
            let cached=cache.get(&key);
            let mut mesh=if let Some((_,prepared))=cached {prepared.clone()}else{
                let mut material:crate::unity::MeshMaterial=bincode::deserialize(&resolve(reference)?).map_err(|e|e.to_string())?;
                if let Some(values)=&part.source_material{material.colors=values.colors.clone();material.floats=values.floats.clone();}
                let mesh=crate::unity::ExtractedMesh{name:part.name.clone(),mat:Some(material),..Default::default()};
                if crate::head_uses_item_draw(&mesh)||mesh.mat.as_ref().is_some_and(|m|m.shader.eq_ignore_ascii_case("xukmi/MainItemPlus")){return Ok(None)}
                mesh
            };
            // Native iris highlights are independent draw textures. They must
            // change along with the create-eye RT; rebaking MainTex alone is
            // invisible on the native eye shader. Keep all gaze/UV state.
            let source_start=std::time::Instant::now();
            let eye=if let Some((eye,_))=cached {eye.clone()}
                else {crate::appearance::timings::measure("eye.resolve",||crate::source_eye_draw(game,vanilla,mods,card,&mesh,guids))};
            if profile{eprintln!("APPEARANCE_PROFILE category={category} part={} phase=eye_source ms={:.3}",part.name,source_start.elapsed().as_secs_f64()*1000.);}
            if part.eye_draw.is_some()!=eye.is_some(){return Ok(None)}
            let cache_miss=cached.is_none();
            let prepared_eye=eye.clone();
            if let Some(eye)=eye{
                for (slot,png) in eye.textures.into_iter().enumerate(){
                    let field=format!("eye_texture_{slot}");
                    base.references.retain(|r|!(r.part==Some(index)&&r.field==field));
                    if let Some(png)=png{
                        let raw=bincode::serialize(&png).map_err(|e|e.to_string())?;
                        let id=crate::portable_resources::id("texture",&raw);
                        base.references.push(crate::portable_resources::ResourceRef{part:Some(index),field,kind:"texture".into(),id:id.clone()});
                        base.unique.insert(id,raw);
                    }
                }
            }
            let detail_start=std::time::Instant::now();
            if cache_miss{
                crate::appearance::timings::measure("detail.resolve",||crate::resolve_head_detail_texture(game,vanilla,mods,card,guids,0,&mut mesh,&[],&[]));
                // Cache only complete successful preparation, never a missing
                // source or a partial source-program transition.
                if mesh.texture_png.is_some(){cache.insert(key,(prepared_eye,mesh.clone()));}
            }
            if profile{eprintln!("APPEARANCE_PROFILE category={category} part={} phase=detail_source ms={:.3}",part.name,detail_start.elapsed().as_secs_f64()*1000.);}
            let Some(png)=mesh.texture_png else{return Err(format!("Selected head texture could not be resolved: {} category={category}",part.name))};
            if part.shader_kind==11&&!Self::head_main_alpha_compatible(part.source_material.as_ref(),&png){return Ok(None)}
            // Catalog assignments replace only MainTex; preserve normal maps,
            // source material, UVs, bones, FBS and all other texture references.
            part.runtime_textures.extend(mesh.runtime_textures);
            let raw=bincode::serialize(&png).map_err(|e|e.to_string())?;
            let id=crate::portable_resources::id("texture",&raw);
            changes.push((index,id,raw));
        }
        if changes.is_empty(){return Ok(None)}
        let count=changes.len();
        for(index,id,raw)in changes{
            base.references.retain(|r|!(r.part==Some(index)&&r.field=="png"));
            base.references.push(crate::portable_resources::ResourceRef{part:Some(index),field:"png".into(),kind:"texture".into(),id:id.clone()});
            // MainAlpha samples its native input table, not the convenience
            // preview PNG. Both references must move in the same transaction.
            if snapshot.session.parts[index].shader_kind==11{
                base.references.retain(|r|!(r.part==Some(index)&&r.field=="shader_textures:_MainTex"));
                base.references.push(crate::portable_resources::ResourceRef{part:Some(index),field:"shader_textures:_MainTex".into(),kind:"texture".into(),id:id.clone()});
            }
            base.unique.insert(id,raw);
        }
        // Only referenced resources belong to this immutable outfit revision.
        // Otherwise every texture ever selected stays in subsequent clones
        // and checkpoints, so editing cost and saved APNG size grow forever.
        let needed:std::collections::BTreeSet<_>=base.references.iter().map(|r|r.id.clone()).collect();
        base.unique.retain(|id,_|needed.contains(id));
        let serialize_start=std::time::Instant::now();
        base.residue=bincode::serialize(&snapshot).map_err(|e|e.to_string())?;
        if profile{eprintln!("APPEARANCE_PROFILE category={category} phase=residue_encode ms={:.3} total_ms={:.3}",serialize_start.elapsed().as_secs_f64()*1000.,profile_start.elapsed().as_secs_f64()*1000.);}
        Ok(Some(count))
    }

    /// Restore only global extraction state; no unrelated mesh/texture is read.
    pub(crate) fn preview_globals(base:&crate::portable_resources::SplitSnapshot,
        mut resolve:impl FnMut(&crate::portable_resources::ResourceRef)->Result<Vec<u8>,String>)
        ->Result<Self,String>{
        use bincode::Options;
        Self::audit_preview_residue(&base.residue)?;
        let mut snapshot:PreviewSnapshot=bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes().deserialize(&base.residue).map_err(|e|e.to_string())?;
        snapshot.session.parts.clear();
        let refs:Vec<_>=base.references.iter().filter(|r|r.part.is_none()).cloned().collect();
        let mut fields=std::collections::BTreeSet::new();
        for r in &refs{if !fields.insert(&r.field){return Err("duplicate global resource".into());}}
        Self::merge_preview_resources(&bincode::serialize(&snapshot).map_err(|e|e.to_string())?,&refs,|r|resolve(r))
    }

    /// Replace renderer records and their resource references while retaining
    /// the exact original global parameters, rig and all untouched parts.
    /// The caller identifies removals by validated card-property provenance.
    pub(crate) fn replace_preview_parts(base:&crate::portable_resources::SplitSnapshot,
        removed:&std::collections::BTreeSet<usize>,added:crate::portable_resources::SplitSnapshot)
        ->Result<crate::portable_resources::SplitSnapshot,String> {
        Self::prepare_preview_part_replacement(base,removed,added).map(|prepared|prepared.snapshot)
    }

    /// The same validated mapping is consumed by native state and UE renderer
    /// publication. Removed indices have no successor; retained parts keep their
    /// order and newly prepared parts occupy the final contiguous range.
    pub(crate) fn prepare_preview_part_replacement(base:&crate::portable_resources::SplitSnapshot,
        removed:&std::collections::BTreeSet<usize>,added:crate::portable_resources::SplitSnapshot)
        ->Result<PreviewPartReplacement,String> {
        use bincode::Options;
        let read=|bytes:&[u8]|bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes()
            .deserialize::<PreviewSnapshot>(bytes).map_err(|e|e.to_string());
        if base.schema!=1||added.schema!=1{return Err("unsupported part patch schema".into());}
        let mut original=read(&base.residue)?;
        let replacement=read(&added.residue)?;
        for(snapshot,count)in [(base,original.session.parts.len()),(&added,replacement.session.parts.len())] {
            let mut targets=std::collections::BTreeSet::new();
            for r in &snapshot.references {
                if r.part.is_some_and(|i|i>=count){return Err("part patch input resource index out of range".into());}
                if !targets.insert((r.part,r.field.clone())){return Err("duplicate part patch input resource".into());}
            }
            Self::audit_preview_residue(&snapshot.residue)?;
        }
        let count=original.session.parts.len();
        if removed.iter().any(|i|*i>=count){return Err("removed part index out of range".into());}
        let mut remap=vec![None;count];let mut retained=Vec::new();
        for (i,part) in std::mem::take(&mut original.session.parts).into_iter().enumerate(){
            if !removed.contains(&i){remap[i]=Some(retained.len());retained.push(part);}
        }
        let offset=retained.len();let new_count=replacement.session.parts.len();
        retained.extend(replacement.session.parts);original.session.parts=retained;
        let mut references=Vec::new();
        for reference in &base.references {
            let mut reference=reference.clone();
            if let Some(i)=reference.part {
                let mapped=remap.get(i).ok_or("base resource part index out of range")?;
                let Some(index)=mapped else{continue};reference.part=Some(*index);
            }
            references.push(reference);
        }
        for mut reference in added.references {
            // All globals remain from the base. A slot edit must never silently
            // replace its skeleton, shape, dynamics or lighting with the delta.
            let Some(i)=reference.part else{continue};
            if i>=new_count{return Err("added resource part index out of range".into());}
            reference.part=Some(offset+i);references.push(reference);
        }
        let needed:std::collections::BTreeSet<_>=references.iter().map(|r|r.id.clone()).collect();
        let mut unique=base.unique.iter().filter(|(id,_)|needed.contains(*id))
            .map(|(id,raw)|(id.clone(),raw.clone())).collect::<std::collections::BTreeMap<_,_>>();
        for(id,raw)in added.unique {
            if !needed.contains(&id){continue;}
            if unique.get(&id).is_some_and(|existing|existing!=&raw){return Err("conflicting part patch resources".into());}
            unique.insert(id,raw);
        }
        let mut targets=std::collections::BTreeSet::new();
        for r in &references {if !targets.insert((r.part,r.field.clone())){return Err("duplicate part patch resource".into());}}
        let residue=bincode::serialize(&original).map_err(|e|e.to_string())?;
        Self::audit_preview_residue(&residue)?;
        Ok(PreviewPartReplacement{
            snapshot:crate::portable_resources::SplitSnapshot{schema:1,residue,references,unique},
            retained_indices:remap,added_range:offset..offset+new_count,
        })
    }

    pub(crate) fn to_preview_bytes(&self) -> Result<Vec<u8>, String> {
        bincode::serialize(&self.preview_snapshot()).map_err(|e| e.to_string())
    }
    /// What to_preview_bytes serializes, owned (serialized elsewhere).
    pub(crate) fn preview_snapshot(&self) -> PreviewSnapshot {
        let mut blob = SessionBlob::from_session(self);
        blob.game_root = PathBuf::new();
        PreviewSnapshot { session: blob, collider_settings: self.collider_settings, hand_shapes: self.hand_shapes.clone() }
    }

    /// Unreal already owns the renderer's textures. Its animation evaluator
    /// needs the original rig, geometry, morphs and dynamics, not a second set
    /// of albedo/normal/mask images. Keep the full portable/APNG exporter above.
    pub(crate) fn to_animation_bytes(&self) -> Result<Vec<u8>, String> {
        let mut blob = SessionBlob::from_session(self);
        blob.game_root = PathBuf::new();
        blob.ramp_png = None;
        for p in &mut blob.parts {
            p.shader_textures.clear();
            p.png = None;
            p.alpha_mask = None;
            p.hair_gloss = None;
            p.detail_mask = None;
            p.line_mask = None;
            p.normal_map = None;
            p.normal_detail = None;
            p.normal_mask = None;
            p.overtex_png = None;
            p.overtex2_png = None;
            // Eye/item controller state remains intact: those components may
            // consult their authored images while updating animation state.
        }
        let snapshot = PreviewSnapshot { session: blob, collider_settings: self.collider_settings,
            hand_shapes: self.hand_shapes.clone() };
        let raw = bincode::serialize(&snapshot).map_err(|e| e.to_string())?;
        if raw.len() as u64 > crate::portable::MAX_BYTES { return Err("animation snapshot too large".into()); }
        let mut out = b"OKAN0001".to_vec();
        out.extend((raw.len() as u64).to_le_bytes());
        out.extend(crate::portable::crc(&raw).to_le_bytes());
        out.extend(lz4_flex::block::compress(&raw));
        Ok(out)
    }

    pub(crate) fn from_animation_bytes(bytes: &[u8]) -> Result<Self, String> {
        if !bytes.starts_with(b"OKAN0001") { return Self::from_preview_bytes(bytes); }
        if bytes.len() < 20 { return Err("truncated animation header".into()); }
        let size = u64::from_le_bytes(bytes[8..16].try_into().unwrap());
        if size == 0 || size > crate::portable::MAX_BYTES { return Err("animation size limit".into()); }
        let raw = lz4_flex::block::decompress(&bytes[20..], size as usize).map_err(|e| e.to_string())?;
        if crate::portable::crc(&raw) != u32::from_le_bytes(bytes[16..20].try_into().unwrap()) {
            return Err("animation checksum mismatch".into());
        }
        Self::from_preview_bytes(&raw)
    }

    pub(crate) fn from_preview_bytes(bytes: &[u8]) -> Result<Self, String> {
        use bincode::Options;
        let snapshot: PreviewSnapshot = bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes()
            .deserialize(bytes).map_err(|e| e.to_string())?;
        if !snapshot.session.game_root.as_os_str().is_empty() {
            return Err("portable snapshot must not contain a game root".into());
        }
        let mut session = snapshot.session.into_session_with_settings(snapshot.collider_settings, snapshot.hand_shapes);
        session.anim_clip.clear();
        session.coordinate_count = 1;
        Ok(session)
    }

    pub fn to_cache_bytes(&self) -> Result<Vec<u8>, String> {
        let origins:Vec<_>=self.parts.iter().map(|p|p.source_property.clone()).collect();
        for property in origins.iter().flatten(){validate_part_origin(property)?;}
        let mut bytes=b"OKSC0003".to_vec();
        bytes.extend(bincode::serialize(&(SessionBlob::from_session(self),origins,self.accessory_context.as_deref())).map_err(|e|e.to_string())?);
        Ok(bytes)
    }

    pub fn from_cache_bytes(bytes: &[u8]) -> Result<Self, String> {
        use bincode::Options;
        let reader=bincode::DefaultOptions::new().with_fixint_encoding()
            .with_limit(crate::portable::MAX_BYTES).reject_trailing_bytes();
        let modern=bytes.strip_prefix(b"OKSC0003");
        if let Some(raw)=modern.or_else(||bytes.strip_prefix(b"OKSC0002")) {
            let (blob,origins,context):(SessionBlob,Vec<Option<String>>,Option<crate::accessory_extract::Context>)=
                if modern.is_some(){reader.deserialize(raw).map_err(|e|e.to_string())?}
                else{let (blob,origins)=reader.deserialize(raw).map_err(|e|e.to_string())?;(blob,origins,None)};
            if context.as_ref().is_some_and(|c|c.schema!=1){return Err("unsupported cached accessory context".into());}
            let mut session=blob.into_session();
            session.accessory_context=context.map(std::sync::Arc::new);
            if origins.len()!=session.parts.len(){return Err("part origin count mismatch".into());}
            for (part,property) in session.parts.iter_mut().zip(origins) {
                if let Some(value)=&property {validate_part_origin(value)?;}
                part.source_property=property;
            }
            Ok(session)
        }else{
            // Old session blobs keep their exact binary layout and remain readable.
            let blob:SessionBlob=reader.deserialize(bytes).map_err(|e|e.to_string())?;
            Ok(blob.into_session())
        }
    }

    pub fn ensure_skel_export(&mut self) -> &crate::skel_export::SkelExport {
        if self.skel_export.is_none() {
            self.skel_export = Some(crate::skel_export::build(self));

        }
        self.skel_export.as_ref().unwrap()
    }
}

#[derive(serde::Serialize, serde::Deserialize)]
struct PartBlob {
    runtime_textures: std::collections::BTreeMap<String, [i32; 5]>,
    source_material: Option<crate::unity::MeshMaterial>,
    shader_textures: std::collections::BTreeMap<String, Vec<u8>>,
    role: String,
    name: String,
    render_visible: bool,
    item_draw: Option<ItemDrawMaterial>,
    eye_draw: Option<crate::eye_gaze::EyeDraw>,
    rest_positions: Vec<[f32; 3]>,
    positions: Vec<[f32; 3]>,
    rest_normals: Vec<[f32; 3]>,
    rest_tangents: Vec<[f32; 4]>,
    normals: Vec<[f32; 3]>,
    uvs: Vec<[f32; 2]>,
    uv2: Vec<[f32; 2]>,
    uv3: Vec<[f32; 2]>,
    uv4: Vec<[f32; 2]>,
    vertex_colors: Vec<[u8; 4]>,
    overtex_png: Option<Vec<u8>>,
    overcolor: [f32; 4],
    overtex2_png: Option<Vec<u8>>,
    overcolor2: [f32; 4],
    nip_size: f32,
    nip_specular: f32,
    nip_toggle: f32,
    tex1_mask: f32,
    texcoord2_st: [f32; 4],
    texcoord3_st: [f32; 4],
    shadow_color: [f32; 4],
    line_color: [f32; 4],
    source_outline_enabled: bool,
    shadow_extend: f32,
    shadow_extend_another: f32,
    main_tex_st: [f32; 4],
    alpha_mask_st: [f32; 4],
    normal_map_st: [f32; 4],
    normal_detail_st: [f32; 4],
    normal_mask_st: [f32; 4],
    detail_mask_st: [f32; 4],
    line_mask_st: [f32; 4],
    normal_detail_scale: f32,
    face_normal: f32,
    face_shadow: f32,
    line_tex_on: f32,
    skin_line_width: f32,
    hair_gloss_st: [f32; 4],
    spec_height: f32,
    specular_power: f32,
    specular_power_nail: f32,
    specular_color: [f32; 4],
    no_tex_specular: f32,
    metallic: f32,
    smoothness: f32,
    shader_kind: u8,
    indices: Vec<u32>,
    weights: Vec<[f32; 4]>,
    bone_idx: Vec<[u16; 4]>,
    bind_poses: Vec<crate::pose::Mat4>,
    png: Option<Vec<u8>>,
    alpha_mask: Option<Vec<u8>>,
    hair_gloss: Option<Vec<u8>>,
    detail_mask: Option<Vec<u8>>,
    line_mask: Option<Vec<u8>>,
    normal_map: Option<Vec<u8>>,
    normal_detail: Option<Vec<u8>>,
    normal_mask: Option<Vec<u8>>,
    blend: String,
    unlit: bool,
    parent_bone: Option<String>,
    rigid_follow_bone: Option<String>,
    runtime_accessory: Option<AccessoryRuntime>,
    mouth_patterns: Vec<(i32, i32)>,
    eye_patterns: Vec<(i32, i32)>,
    eyebrow_patterns: Vec<(i32, i32)>,
    face_baseline_weights: Vec<f32>,
    face_default_rates: [f32; 2],
    face_runtime_settings: [f32; 12],
    iris_runtime: Option<crate::eye_gaze::IrisRuntime>,
    skel_kind: u8,
    blend_names: Vec<String>,
    blend_deltas: Vec<Vec<(u32, [f32; 3])>>,
    morph_idx: Vec<Vec<u32>>,
    morph_dxyz: Vec<Vec<f32>>,
}

/// Preview bytes as a JSON tree (OK base library parity reports).
pub(crate) fn preview_json(bytes: &[u8]) -> Result<serde_json::Value, String> {
    use bincode::Options;
    let snapshot: PreviewSnapshot = bincode::DefaultOptions::new().with_fixint_encoding()
        .with_limit(crate::portable::MAX_BYTES).deserialize(bytes).map_err(|e| e.to_string())?;
    serde_json::to_value(&snapshot).map_err(|e| e.to_string())
}

#[derive(serde::Serialize, serde::Deserialize)]
pub(crate) struct PreviewSnapshot {
    session: SessionBlob,
    collider_settings: crate::dbone::game_colliders::Settings,
    hand_shapes: crate::shape::hand::HandShapes,
}

/// A session holding only these parts at their authored rest geometry
/// (OK model editor preview: no character, skeleton or card).
pub(crate) fn preview_session(parts: &[SessionPart], game_root: &Path) -> OkSession {
    let none = game_root;
    let vanilla = crate::list::load_characustom_dir_shared(&game_root.join("abdata/list/characustom"));
    let ramp_png = crate::configured_ramp_png(game_root, &vanilla);
    SessionBlob {
        pv_config: None, eye_look_config: None, game_root: game_root.to_owned(), ramp_png,
        shadow_depth: crate::configured_shadow_depth(none), line_width: crate::configured_line_width(none),
        line_depth: crate::configured_line_depth(none), kano_ver_shift: crate::configured_kano_ver_shift(none),
        body_rest: None, body_dynamics: Default::default(), body_colliders: Default::default(), clothes_dynamics: Default::default(),
        bust_softness: 0.5, bust_weight: 0.5, collider_overrides: Default::default(),
        head_rest: None, body_skel: None, head_skel: None, body_keys: HashMap::new(), head_keys: HashMap::new(), correct: Vec::new(),
        sex: 1, type_bone: 0, nip_stand_rate: 0.0, shape_body: Vec::new(), shape_face: Vec::new(), abm: Vec::new(),
        parts: parts.iter().map(PartBlob::from_part).collect(), coordinate_count: 1, anim_time: 0.0,
        body_shaped: None, head_shaped: None, head_full_rest: None, head_full_posed: None,
    }.into_session_with_settings(Default::default(), Default::default())
}

#[derive(serde::Serialize, serde::Deserialize)]
struct SessionBlob {
    pv_config:Option<crate::pv::Config>,
    eye_look_config:Option<crate::eye_look::Config>,
    game_root: PathBuf,
    ramp_png: Option<Vec<u8>>,
    shadow_depth: f32,
    line_width: f32,
    line_depth: f32,
    kano_ver_shift: f32,
    body_rest: Option<Skeleton>,
    body_dynamics: crate::dbone::ver02::BodyDynamicSet,
    body_colliders: crate::dbone::BodyColliderSet,
    clothes_dynamics: crate::dbone::DBoneSet,
    bust_softness: f32,
    bust_weight: f32,
    collider_overrides: crate::dbone::game_colliders::CardOverrides,
    head_rest: Option<Skeleton>,
    body_skel: Option<Skeleton>,
    head_skel: Option<Skeleton>,
    #[serde(serialize_with="crate::ordered::map")]
    body_keys: HashMap<String, Vec<crate::shape::Keyframe>>,
    #[serde(serialize_with="crate::ordered::map")]
    head_keys: HashMap<String, Vec<crate::shape::Keyframe>>,
    correct: Vec<crate::shape::CorrectInfo>,
    sex: i32,
    type_bone: i32,
    nip_stand_rate: f32,
    shape_body: Vec<f32>,
    shape_face: Vec<f32>,
    abm: Vec<crate::abmx::AbmBone>,
    parts: Vec<PartBlob>,
    coordinate_count: usize,
    anim_time: f32,
    body_shaped: Option<Skeleton>,
    head_shaped: Option<Skeleton>,
    head_full_rest: Option<Skeleton>,
    head_full_posed: Option<Skeleton>,
}

impl SessionBlob {
    fn from_session(s: &OkSession) -> Self {
        Self::from_session_selected(s,true)
    }

    fn from_session_selected(s: &OkSession, include_globals:bool) -> Self {
        macro_rules! global {($field:ident) => {if include_globals {s.$field.clone()} else {Default::default()}};}
        let mut body_shaped=global!(body_shaped);
        // Yure is transient Studio state, like anim_clip/particle history.
        // A cache reload must start from the original card shape again.
        if include_globals && s.yure_policy.is_some_and(|p|p.shapes!=[[true;9];2]) {
            if let Some(mut neutral)=s.body_rest.clone() {
                crate::shape::apply_body_shape_keys(&mut neutral,&s.body_keys,&s.shape_body,&s.correct,s.sex,s.type_bone,s.nip_stand_rate);
                crate::pose::assigned_weights_remap(&mut neutral);body_shaped=Some(neutral);
            }
        }
        // A session that never ran apply_shape (e.g. the editor's appearance
        // rebuild) has no shaped baselines. Writing None makes every reader
        // reset its live skeleton to nothing; persist the card shape instead.
        let mut head_shaped=global!(head_shaped);
        if include_globals && (body_shaped.is_none()||head_shaped.is_none()) {
            let mut head_scale=[1.0f32;3];
            if let Some(mut body)=s.body_rest.clone() {
                if !s.shape_body.is_empty()&&!s.body_keys.is_empty() {
                    head_scale=crate::shape::apply_body_shape_keys(&mut body,&s.body_keys,&s.shape_body,&s.correct,s.sex,s.type_bone,s.nip_stand_rate);
                }
                crate::pose::assigned_weights_remap(&mut body);
                if body_shaped.is_none(){body_shaped=Some(body);}
            }
            if head_shaped.is_none() {if let Some(mut head)=s.head_rest.clone() {
                if !s.shape_face.is_empty()&&!s.head_keys.is_empty() {
                    let head_correct=1.0/(1.0+s.correct.get(2).map(|c|c.scl[1]).unwrap_or(0.0));
                    crate::shape::apply_head_shape_keys(&mut head,&s.head_keys,&s.shape_face,head_scale,head_correct,s.type_bone);
                }
                crate::pose::assigned_weights_remap(&mut head);head_shaped=Some(head);
            }}
        }
        Self {
            pv_config:global!(pv_config),
            eye_look_config:global!(eye_look_config),
            game_root: global!(game_root),
            ramp_png: global!(ramp_png),
            shadow_depth: s.shadow_depth,
            line_width: s.line_width,
            line_depth: s.line_depth,
            kano_ver_shift: s.kano_ver_shift,
            body_rest: global!(body_rest),
            body_dynamics: global!(body_dynamics),
            body_colliders: global!(body_colliders),
            clothes_dynamics: global!(clothes_dynamics),
            bust_softness: s.bust_softness,
            bust_weight: s.bust_weight,
            collider_overrides: s.collider_overrides,
            head_rest: global!(head_rest),
            body_skel: global!(body_skel),
            head_skel: global!(head_skel),
            body_keys: global!(body_keys),
            head_keys: global!(head_keys),
            correct: global!(correct),
            sex: s.sex,
            type_bone: s.type_bone,
            nip_stand_rate: s.nip_stand_rate,
            shape_body: s.shape_body.clone(),
            shape_face: s.shape_face.clone(),
            abm: global!(abm),
            parts: s.parts.iter().map(PartBlob::from_part).collect(),
            coordinate_count: s.coordinate_count,
            anim_time: s.anim_time,
            body_shaped,
            head_shaped,
            head_full_rest: global!(head_full_rest),
            head_full_posed: global!(head_full_posed),
        }
    }

    fn into_session(self) -> OkSession {
        let collider_settings = crate::dbone::game_colliders::Settings::load(&self.game_root);
        let hand_shapes = crate::shape::hand::HandShapes::load(&self.game_root);
        self.into_session_with_settings(collider_settings, hand_shapes)
    }

    fn into_session_with_settings(self, collider_settings: crate::dbone::game_colliders::Settings,
        hand_shapes: crate::shape::hand::HandShapes) -> OkSession {
        OkSession {
            accessory_context:None,
            editor_pregnancy:Default::default(),
            pv_config:self.pv_config,pv_mask:15,pv_fk:false,
            eye_look_config:self.eye_look_config,
            eye_look_runtime:None,
            head_look:crate::head_look::HeadLook::default(),
            physx_secondary:false,
            external_animation:None,
            scene_gpu_parts:Vec::new(),
            physics:Default::default(),
            body_interaction:None,
            game_root: self.game_root,
            ramp_png: self.ramp_png,
            shadow_depth: self.shadow_depth,
            line_width: self.line_width,
            line_depth: self.line_depth,
            kano_ver_shift: self.kano_ver_shift,
            body_rest: self.body_rest,
            body_dynamics: self.body_dynamics,
            body_dynamic_runtime: None,
            yure_policy:None,
            body_colliders: self.body_colliders,
            clothes_dynamics: self.clothes_dynamics,
            clothes_dynamic_runtime: None,
            bust_softness: self.bust_softness,
            bust_weight: self.bust_weight,
            collider_settings,
            collider_overrides: self.collider_overrides,
            head_rest: self.head_rest,
            body_skel: self.body_skel,
            head_skel: self.head_skel,
            body_keys: self.body_keys,
            head_keys: self.head_keys,
            hand_shapes,
            user_fk: Vec::new(),
            user_fk_ids: BoneIdCache::default(),
            hand_shape_ids: BoneIdCache::default(),
            studio_ik_solver: None,
            studio_ik: Vec::new(),
            studio_ik_rotations: Vec::new(),
            correct: self.correct,
            sex: self.sex,
            type_bone: self.type_bone,
            nip_stand_rate: self.nip_stand_rate,
            shape_body: self.shape_body,
            shape_face: self.shape_face,
            abm: self.abm,
            parts: { let mut parts: Vec<SessionPart> = self.parts.into_iter().map(PartBlob::into_part).collect(); crate::assign_draw_layers(&mut parts); parts },
            coordinate_count: self.coordinate_count,
            skel_export: None,
            anim_time: self.anim_time,
            paired_ik_runtime: None,
            anim_clip: std::env::var("OK_POSE").unwrap_or_else(|_| "Stand_D_00".to_string()),
            anim_parameters: HashMap::new(),
            anim_clock:Default::default(),anim_crossfade:None,anim_layer_crossfades:Default::default(),
            anim_scene_mode: 0,
            body_shaped: self.body_shaped,
            shape_reference_scratch: None,
            head_shaped: self.head_shaped,
            head_full_rest: self.head_full_rest,
            head_full_posed: self.head_full_posed,
        }
    }
}

impl PartBlob {
    fn from_part(p: &SessionPart) -> Self {
        let (blend_names, blend_deltas) = p
            .blend_shapes
            .iter()
            .map(|(n, d)| (n.to_string_lossy().into_owned(), d.clone()))
            .unzip();
        Self {
            role: p.role.clone(),
            runtime_textures: p.runtime_textures.clone(),
            source_material: p.source_material.clone(),
            shader_textures: p.shader_textures.clone(),
            name: p.name.clone(),
            render_visible: p.render_visible,
            item_draw: p.item_draw.clone(),
            eye_draw: p.eye_draw.clone(),
            rest_positions: p.rest_positions.clone(),
            positions: p.positions.clone(),
            rest_normals: p.rest_normals.clone(),
            rest_tangents: p.rest_tangents.clone(),
            normals: p.normals.clone(),
            uvs: p.uvs.clone(),
            uv2: p.uv2.clone(),
            uv3: p.uv3.clone(),
            uv4: p.uv4.clone(),
            vertex_colors: p.vertex_colors.clone(),
            overtex_png: p.overtex_png.clone(),
            overcolor: p.overcolor,
            overtex2_png: p.overtex2_png.clone(),
            overcolor2: p.overcolor2,
            nip_size: p.nip_size,
            nip_specular: p.nip_specular,
            nip_toggle: p.nip_toggle,
            tex1_mask: p.tex1_mask,
            texcoord2_st: p.texcoord2_st,
            texcoord3_st: p.texcoord3_st,
            shadow_color: p.shadow_color,
            line_color: p.line_color,
            source_outline_enabled: p.source_outline_enabled,
            shadow_extend: p.shadow_extend,
            shadow_extend_another: p.shadow_extend_another,
            main_tex_st: p.main_tex_st,
            alpha_mask_st: p.alpha_mask_st,
            normal_map_st: p.normal_map_st,
            normal_detail_st: p.normal_detail_st,
            normal_mask_st: p.normal_mask_st,
            detail_mask_st: p.detail_mask_st,
            line_mask_st: p.line_mask_st,
            normal_detail_scale: p.normal_detail_scale,
            face_normal: p.face_normal,
            face_shadow: p.face_shadow,
            line_tex_on: p.line_tex_on,
            skin_line_width: p.skin_line_width,
            hair_gloss_st: p.hair_gloss_st,
            spec_height: p.spec_height,
            specular_power: p.specular_power,
            specular_power_nail: p.specular_power_nail,
            specular_color: p.specular_color,
            no_tex_specular: p.no_tex_specular,
            metallic: p.metallic,
            smoothness: p.smoothness,
            shader_kind: p.shader_kind,
            indices: p.indices.clone(),
            weights: p.weights.clone(),
            bone_idx: p.bone_idx.clone(),
            bind_poses: p.bind_poses.clone(),
            png: p.png.clone(),
            alpha_mask: p.alpha_mask.clone(),
            hair_gloss: p.hair_gloss.clone(),
            detail_mask: p.detail_mask.clone(),
            line_mask: p.line_mask.clone(),
            normal_map: p.normal_map.clone(),
            normal_detail: p.normal_detail.clone(),
            normal_mask: p.normal_mask.clone(),
            blend: p.blend.clone(),
            unlit: p.unlit,
            parent_bone: p.parent_bone.clone(),
            rigid_follow_bone: p.rigid_follow_bone.clone(),
            runtime_accessory: p.runtime_accessory.clone(),
            mouth_patterns: p.mouth_patterns.clone(),
            eye_patterns: p.eye_patterns.clone(),
            eyebrow_patterns: p.eyebrow_patterns.clone(),
            face_baseline_weights: p.face_baseline_weights.clone(),
            face_default_rates: p.face_default_rates,
            face_runtime_settings: p.face_runtime_settings,
            iris_runtime: p.iris_runtime.clone(),
            skel_kind: p.skel_kind,
            blend_names,
            blend_deltas,
            morph_idx: p.morph_idx.clone(),
            morph_dxyz: p.morph_dxyz.clone(),
        }
    }

    fn into_part(self) -> SessionPart {
        let blend_shapes = self
            .blend_names
            .iter()
            .zip(self.blend_deltas.iter())
            .map(|(n, d)| (std::ffi::CString::new(n.as_str()).unwrap_or_default(), d.clone()))
            .collect();
        SessionPart {
            source_property: None,
            draw_layer: 0,
            runtime_textures: self.runtime_textures,
            source_material: self.source_material,
            shader_textures: self.shader_textures,
            role_c: std::ffi::CString::new(self.role.as_str()).unwrap_or_default(),
            name_c: std::ffi::CString::new(self.name.as_str()).unwrap_or_default(),
            blend_c: std::ffi::CString::new(self.blend.as_str()).unwrap_or_default(),
            parent_c: std::ffi::CString::new(self.parent_bone.as_deref().unwrap_or(""))
                .unwrap_or_default(),
            rigid_follow_c: std::ffi::CString::new(self.rigid_follow_bone.as_deref().unwrap_or(""))
                .unwrap_or_default(),
            role: self.role,
            name: self.name,
            render_visible: self.render_visible,
            item_draw: self.item_draw,
            eye_draw: self.eye_draw,
            rest_positions: self.rest_positions,
            positions: self.positions,
            rest_normals: self.rest_normals,
            rest_tangents: self.rest_tangents,
            normals: self.normals,
            uvs: self.uvs,
            uv2: self.uv2,
            uv3: self.uv3,
            uv4: self.uv4,
            vertex_colors: self.vertex_colors,
            overtex_png: self.overtex_png,
            overcolor: self.overcolor,
            overtex2_png: self.overtex2_png,
            overcolor2: self.overcolor2,
            nip_size: self.nip_size,
            nip_specular: self.nip_specular,
            nip_toggle: self.nip_toggle,
            tex1_mask: self.tex1_mask,
            texcoord2_st: self.texcoord2_st,
            texcoord3_st: self.texcoord3_st,
            shadow_color: self.shadow_color,
            line_color: self.line_color,
            source_outline_enabled: self.source_outline_enabled,
            shadow_extend: self.shadow_extend,
            shadow_extend_another: self.shadow_extend_another,
            main_tex_st: self.main_tex_st,
            alpha_mask_st: self.alpha_mask_st,
            normal_map_st: self.normal_map_st,
            normal_detail_st: self.normal_detail_st,
            normal_mask_st: self.normal_mask_st,
            detail_mask_st: self.detail_mask_st,
            line_mask_st: self.line_mask_st,
            normal_detail_scale: self.normal_detail_scale,
            face_normal: self.face_normal,
            face_shadow: self.face_shadow,
            line_tex_on: self.line_tex_on,
            skin_line_width: self.skin_line_width,
            hair_gloss_st: self.hair_gloss_st,
            spec_height: self.spec_height,
            specular_power: self.specular_power,
            specular_power_nail: self.specular_power_nail,
            specular_color: self.specular_color,
            no_tex_specular: self.no_tex_specular,
            metallic: self.metallic,
            smoothness: self.smoothness,
            shader_kind: self.shader_kind,
            indices: self.indices,
            weights: self.weights,
            bone_idx: self.bone_idx,
            bind_poses: self.bind_poses,
            png: self.png,
            alpha_mask: self.alpha_mask,
            hair_gloss: self.hair_gloss,
            detail_mask: self.detail_mask,
            line_mask: self.line_mask,
            normal_map: self.normal_map,
            normal_detail: self.normal_detail,
            normal_mask: self.normal_mask,
            blend: self.blend,
            unlit: self.unlit,
            parent_bone: self.parent_bone,
            rigid_follow_bone: self.rigid_follow_bone,
            runtime_accessory: self.runtime_accessory,
            mouth_patterns: self.mouth_patterns,
            eye_patterns: self.eye_patterns,
            eyebrow_patterns: self.eyebrow_patterns,
            face_baseline_weights: self.face_baseline_weights,
            face_default_rates: self.face_default_rates,
            face_runtime_settings: self.face_runtime_settings,
            iris_runtime: self.iris_runtime,
            skel_kind: self.skel_kind,
            blend_shapes,
            morph_idx: self.morph_idx,
            morph_dxyz: self.morph_dxyz,
        }
    }
}

pub fn open(game: &Path, card: Option<&Path>, outfit: i32) -> Result<OkSession, String> {
    let mut sess = open_unconfigured(game, card, outfit)?;
    if let Some(bytes) = card.and_then(|c| crate::card::card_file_bytes(c).ok()) { sess.physics = crate::okphysics::CardPhysics::read_card(&bytes); }
    Ok(sess)
}

fn open_unconfigured(game: &Path, card: Option<&Path>, outfit: i32) -> Result<OkSession, String> {
    // A game root that is an OK base library needs no installation. The
    // session it builds is a portable one: no game root afterwards, so every
    // later feature (animation, pairing) treats it like an okPv card.
    if crate::oklib::ensure_library(game)? {
        crate::gamefs::reset_fallback_used();
        // Recording a card pack must visit source models/queries, including an explicitly
        // selected nonzero outfit. A cached final session cannot record those dependencies.
        let t_key = std::time::Instant::now();
        // (the key hashes the whole card: only worked out when something is looked up by it)
        let keyed = crate::oklib::session_cache_enabled() || crate::cardbake::enabled() || crate::memosnap::replay_kinds() != 0;
        let key = if crate::oklib::recording_card_pack() || !keyed { None }
            else { card.and_then(|c| crate::oklib::session_cache_key(game, c, outfit)) };
        if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: PROFILE open cache key {:>8.1}ms", t_key.elapsed().as_secs_f64() * 1e3); }
        crate::oklib::LAST_FROM_CACHE.store(false, std::sync::atomic::Ordering::Relaxed);
        // A card that carries this open baked in (okBk, OK_CARD_BAKED=1) needs neither its pack nor the cache.
        if crate::cardbake::enabled() {
            if let (Some(c), Some(k)) = (card, key.as_ref()) {
                let t = std::time::Instant::now();
                if let Some(sess) = crate::cardbake::load(c, k).and_then(|b| OkSession::from_preview_bytes(&b).ok()) {
                    crate::oklib::LAST_FROM_CACHE.store(true, std::sync::atomic::Ordering::Relaxed);
                    if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: PROFILE baked open {:>8.1}ms", t.elapsed().as_secs_f64() * 1e3); }
                    return Ok(sess);
                }
            }
        }
        // An OK card carrying its own resources: mounted over the library while it opens.
        let t = std::time::Instant::now();
        let _pack = match card { Some(c) => crate::oklib::mount_card_pack(c)?, None => None };
        if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: PROFILE mount card pack {:>8.1}ms", t.elapsed().as_secs_f64() * 1e3); }
        // A background warm process (ok-card-warm) already building this entry finishes it sooner than a new build.
        for attempt in 0..2 {
            if let Some(bytes) = key.as_ref().and_then(|k| crate::oklib::session_cache_load(k)) {
                if let Ok(sess) = OkSession::from_preview_bytes(&bytes) {
                    crate::oklib::LAST_FROM_CACHE.store(true, std::sync::atomic::Ordering::Relaxed);
                    return Ok(sess);
                }
            }
            if attempt == 1 || !key.as_ref().is_some_and(|k| crate::oklib::wait_for_warming(k)) { break; }
        }
        // Claimed while building, so a warm process does not build the same entry meanwhile.
        let claim = key.as_ref().and_then(|k| crate::oklib::claim_warming(k));
        // Memo results recorded into the card (okMm, OK_CARD_MEMO=kinds) stand in for that work during this build.
        let kinds = crate::memosnap::replay_kinds();
        let _memo = if kinds == 0 { None } else {
            card.zip(key.as_ref()).and_then(|(c, k)| crate::cardbake::load_kind(c, crate::cardbake::MEMO_CHUNK, k))
                .and_then(|b| crate::memosnap::seed(&b, kinds))
        };
        let made = crate::extract_session(game, card, outfit, None);
        // An OK card's strict open (okstrict.rs) that needed what neither the card nor the shared resources hold fails.
        if let (Some(strict), Some(c)) = (_pack.as_ref().and_then(crate::oklib::PackMount::strict), card) {
            let gaps = strict.gaps();
            if !gaps.is_empty() { return Err(crate::okstrict::failure(c, &gaps)); }
        }
        let mut sess = made?;
        sess.game_root = std::path::PathBuf::new();
        log_shader_inputs(&sess);
        // The key covers the library, the card (and its pack) and the zipmods the fallback can serve.
        // An open that read installed zipmods is only valid while they are unchanged (checked on load).
        let fallback = if crate::gamefs::fallback_used() { crate::gamefs::fallback_identity() } else { String::new() };
        let t = std::time::Instant::now();
        // (OK_NOCACHE: nothing is written, so the session is not serialized for it either)
        if let Some(k) = key.filter(|_| crate::oklib::session_cache_enabled()) {
            if crate::oklib::defer_cache_store() && crate::oklib::session_cache_location().is_some() {
                // The entry is written on its own thread (the claim held until then); what it records about this open
                // is read here, where the card pack is mounted.
                let (inputs, preview) = (crate::oklib::session_cache_inputs(&fallback), sess.preview_snapshot());
                std::thread::spawn(move || { let _claim = claim; if let Ok(bytes) = bincode::serialize(&preview) { crate::oklib::session_cache_store_prepared(&k, &bytes, &inputs); } });
            } else { drop(claim); if let Ok(bytes) = sess.to_preview_bytes() { crate::oklib::session_cache_store(&k, &bytes, &fallback); } }
        }
        if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: PROFILE open cache store {:>8.1}ms", t.elapsed().as_secs_f64() * 1e3); }
        return Ok(sess);
    }
    // An original card on the installation is built every time: its SQL session cache never answered an open (the
    // key moved with every signature it covers) and storing an entry doubled the session in memory (a 257 MB
    // buffer for one card) and wrote tens of MB into the installation's .ok_cache.sqlite per open.
    let sess = crate::extract_session(game, card, outfit, None)?;
    log_shader_inputs(&sess);
    Ok(sess)
}

fn log_shader_inputs(sess: &OkSession) {
    if std::env::var_os("OK_SHADER_DIAG").is_none() { return; }
    let dump_dir = std::env::var_os("OK_SHADER_DIAG_DUMP").map(PathBuf::from);
    if let Some(dir) = dump_dir.as_ref() {
        let _ = std::fs::create_dir_all(dir);
        if let Some(png) = sess.ramp_png.as_ref() { let _ = std::fs::write(dir.join("toon_ramp.png"), png); }
    }
    for p in &sess.parts {
        eprintln!("ok-extract: SHADER_INPUT role={} name={} family={} albedo={} alpha={} detail={} line={} normal={} normal_detail={}",
            p.role, p.name, p.shader_kind,
            p.png.as_ref().map_or(0, Vec::len),
            p.alpha_mask.as_ref().map_or(0, Vec::len),
            p.detail_mask.as_ref().map_or(0, Vec::len),
            p.line_mask.as_ref().map_or(0, Vec::len),
            p.normal_map.as_ref().map_or(0, Vec::len),
            p.normal_detail.as_ref().map_or(0, Vec::len));
        if let Some(dir) = dump_dir.as_ref() {
            let stem: String = p.name.chars().map(|c| if c.is_ascii_alphanumeric() { c } else { '_' }).collect();
            for (suffix, png) in [("albedo", &p.png), ("alpha", &p.alpha_mask), ("detail", &p.detail_mask), ("line", &p.line_mask), ("normal", &p.normal_map), ("normal_detail", &p.normal_detail)] {
                if let Some(png) = png { let _ = std::fs::write(dir.join(format!("{stem}_{suffix}.png")), png); }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    #[ignore="prepared local APNG; verifies backwards-compatible slot provenance, run alone"]
    fn part_origins_survive_cache_and_split_resources_without_name_identity() {
        use super::*;
        let path=std::path::Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");
        let mut source=crate::portable::load(path).unwrap();
        assert!(source.parts.len()>2);
        // Identical renderer names must remain independently addressable.
        source.parts[0].name="duplicate_renderer".into();
        source.parts[1].name="duplicate_renderer".into();
        source.parts[0].source_property=Some("outfit0.accessory0".into());
        source.parts[1].source_property=Some("outfit0.accessory20".into());
        let bytes=source.to_cache_bytes().unwrap();assert!(bytes.starts_with(b"OKSC0003"));
        let cached=OkSession::from_cache_bytes(&bytes).unwrap();
        for (a,b) in source.parts.iter().zip(&cached.parts){assert_eq!(a.source_property,b.source_property);assert_eq!(a.positions,b.positions);}
        let legacy=bincode::serialize(&SessionBlob::from_session(&source)).unwrap();
        let old=OkSession::from_cache_bytes(&legacy).unwrap();
        assert!(old.parts.iter().all(|p|p.source_property.is_none()));
        assert_eq!(old.parts[0].positions,source.parts[0].positions);
        let mut trailing=bytes.clone();trailing.push(0);assert!(OkSession::from_cache_bytes(&trailing).is_err());
        let card=crate::card::parse_card(path).unwrap();
        let mut references=Vec::new();let mut resources=std::collections::BTreeMap::new();
        let residue=source.split_preview_resources(&card,|part,field,kind,raw,_|{
            let id=crate::portable_resources::id(kind,&raw);resources.insert(id.clone(),raw);
            references.push(crate::portable_resources::ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
        }).unwrap();
        let restored=OkSession::merge_preview_resources(&residue,&references,|r|Ok(resources[&r.id].clone())).unwrap();
        for (a,b) in source.parts.iter().zip(&restored.parts){assert_eq!(a.source_property,b.source_property);assert_eq!(a.positions,b.positions);assert_eq!(a.png,b.png);}
        // Patch duplicate names by index, retaining all original global resources.
        use crate::portable_resources::{ResourceRef,SplitSnapshot};
        let split=|session:&OkSession| {
            let mut references=Vec::new();let mut unique=std::collections::BTreeMap::new();
            let residue=session.split_preview_resources(&card,|part,field,kind,raw,_|{
                let id=crate::portable_resources::id(kind,&raw);unique.insert(id.clone(),raw);
                references.push(ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
            }).unwrap();SplitSnapshot{schema:1,residue,references,unique}
        };
        let mut delta=OkSession::from_cache_bytes(&bytes).unwrap();
        let first=delta.parts.remove(0);let second=delta.parts.remove(0);delta.parts=vec![second,first];
        delta.shape_body[0]=0.123;
        let base=split(&source);let removed=[0,1].into_iter().collect();
        let prepared=OkSession::prepare_preview_part_replacement(&base,&removed,split(&delta)).unwrap();
        assert_eq!(prepared.retained_indices.len(),source.parts.len());
        assert_eq!(&prepared.retained_indices[..2],&[None,None]);
        for (old,new) in prepared.retained_indices.iter().enumerate().skip(2){assert_eq!(*new,Some(old-2));}
        assert_eq!(prepared.added_range,(source.parts.len()-2)..source.parts.len());
        let patched=OkSession::replace_preview_parts(&base,&removed,split(&delta)).unwrap();
        assert_eq!(OkSession::preview_residue_value(&prepared.snapshot.residue).unwrap(),OkSession::preview_residue_value(&patched.residue).unwrap());
        assert_eq!(prepared.snapshot.unique,patched.unique);
        let patched_session=OkSession::merge_preview_resources(&patched.residue,&patched.references,|r|Ok(patched.unique[&r.id].clone())).unwrap();
        let expected=source.parts.iter().skip(2).chain(delta.parts.iter()).collect::<Vec<_>>();
        assert_eq!(patched_session.parts.len(),expected.len());
        for(a,b)in patched_session.parts.iter().zip(expected){
            assert_eq!(a.source_property,b.source_property);
            let left=PartBlob::from_part(a);let right=PartBlob::from_part(b);
            let l=serde_json::to_value(&left).unwrap();let r=serde_json::to_value(&right).unwrap();
            let differences=l.as_object().unwrap().iter().filter(|(key,value)|r.get(*key)!=Some(*value)).map(|(key,_)|key.as_str()).collect::<Vec<_>>();
            assert!(l==r,"part {} differing fields {:?}",a.name,differences);
        }
        let mut original_globals=SessionBlob::from_session(&restored);original_globals.parts.clear();
        let mut patched_globals=SessionBlob::from_session(&patched_session);patched_globals.parts.clear();
        assert!(serde_json::to_value(&original_globals).unwrap()==serde_json::to_value(&patched_globals).unwrap(),"global parameters changed");
        assert!(OkSession::replace_preview_parts(&base,&[source.parts.len()].into_iter().collect(),split(&delta)).is_err());
        let mut invalid_delta=split(&delta);invalid_delta.references[0].part=Some(99999);
        assert!(OkSession::replace_preview_parts(&base,&removed,invalid_delta).is_err());
        let mut invalid_delta=split(&delta);invalid_delta.references.push(invalid_delta.references[0].clone());
        assert!(OkSession::replace_preview_parts(&base,&removed,invalid_delta).is_err());
        delta.parts.clear();
        let prepared_empty=OkSession::prepare_preview_part_replacement(&base,&removed,split(&delta)).unwrap();
        assert!(prepared_empty.added_range.is_empty());
        assert_eq!(prepared_empty.retained_indices,prepared.retained_indices);
        let empty=OkSession::replace_preview_parts(&base,&removed,split(&delta)).unwrap();
        let empty=OkSession::merge_preview_resources(&empty.residue,&empty.references,|r|Ok(base.unique[&r.id].clone())).unwrap();
        assert_eq!(empty.parts.len(),source.parts.len()-2);
        let mut invalid=source;invalid.parts[0].source_property=Some("bad\0origin".into());
        assert!(invalid.to_cache_bytes().is_err());
        let mismatch=bincode::serialize(&(SessionBlob::from_session(&invalid),vec![None::<String>])).unwrap();
        let mut bad=b"OKSC0002".to_vec();bad.extend(mismatch);assert!(OkSession::from_cache_bytes(&bad).is_err());
    }

    #[test]
    #[ignore="installed March 7th gameplay clothing fixture; run alone"]
    fn gameplay_rigid_clothes_keep_body_motion_and_private_bindings() {
        struct Policy(Option<String>);
        impl Drop for Policy {fn drop(&mut self){match &self.0 {Some(v)=>std::env::set_var("OK_NO_CLOTH_DYNAMICS",v),None=>std::env::remove_var("OK_NO_CLOTH_DYNAMICS")}}}
        let _restore=Policy(std::env::var("OK_NO_CLOTH_DYNAMICS").ok());
        std::env::remove_var("OK_NO_CLOTH_DYNAMICS");
        let root=&crate::test_support::game_path("");
        let card=root.join("UserData/chara/female/[Card] March_7th_keiyuu.png");
        let mut session=open(root,Some(&card),0).unwrap();
        session.anim_clip="studio/anime/00.unity3d|f_aruki_00_00_01".into();
        let bindings:Vec<_>=session.parts.iter().filter_map(|p|p.runtime_accessory.as_ref().map(|a|a.skin_contract().unwrap())).collect();
        std::env::set_var("OK_NO_CLOTH_DYNAMICS","1");
        session.reset_dynamic_accessories();
        session.apply_anim_frame(0.,0.);
        let before=session.body_skel.as_ref().unwrap().clone();
        for frame in 1..=30 {session.apply_anim_frame(frame as f32/60.,1./60.);}
        let after=session.body_skel.as_ref().unwrap();
        let motion=before.worlds.iter().zip(&after.worlds).map(|(a,b)|a.iter().flatten().zip(b.iter().flatten()).map(|(x,y)|(x-y).abs()).fold(0f32,f32::max)).fold(0f32,f32::max);
        assert!(motion>0.001,"walking body must remain animated");
        let mut clothes=0;let mut hair=0;
        for runtime in session.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()) {
            if runtime.copy_weights_body {clothes+=1;assert!(runtime.state.is_none());assert!(runtime.export_skin_palette(session.body_skel.as_ref(),session.head_skel.as_ref()).is_some());}
            else if runtime.state.is_some(){hair+=1;}
        }
        assert!(clothes>0);assert!(hair>0,"non-clothing secondary dynamics must remain enabled");
        let current:Vec<_>=session.parts.iter().filter_map(|p|p.runtime_accessory.as_ref().map(|a|a.skin_contract().unwrap())).collect();
        assert_eq!(bindings,current,"cloth policy must preserve private rigs and assignment");
        assert!(session.clothes_dynamic_runtime.is_none());
        eprintln!("rigid clothes validated parts={clothes} other_dynamic_parts={hair} body_motion={motion}");
    }
    #[test]
    #[ignore="installed prepared collider exact oracle"]
    fn installed_prepared_colliders_match_legacy_binding_and_particles() {
        let game=&crate::test_support::game_path("");
        let card=game.join("UserData/chara/female/[Card] March_7th_keiyuu.png");
        let mut s=open(game,Some(&card),0).unwrap();
        s.anim_clip="states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01".into();s.apply_anim_frame(0.15,0.15);
        let mut checked=0;
        for runtime in s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()) {
            let Some(state)=runtime.state.as_ref()else{continue;};
            let body=if runtime.coordinated{s.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel).or(s.body_skel.as_ref())}else{s.body_skel.as_ref()}.unwrap();
            for flags in [crate::dbone::game_colliders::Flags::default(),
                crate::dbone::game_colliders::Flags{installed:true,breast:true,floor:true,skirt:true},
                crate::dbone::game_colliders::Flags{installed:true,breast:false,floor:true,skirt:false},
                crate::dbone::game_colliders::Flags{installed:true,breast:false,floor:false,skirt:true}] {
                state.verify_prepared_colliders(body,&s.body_colliders,flags);checked+=1;
            }
        }
        assert!(checked>0);eprintln!("OK prepared colliders exact cases={checked} matrices=exact order=exact particles=exact");
    }
    #[test]
    #[ignore="installed native animation particle lifetime regression"]
    fn installed_animation_switch_preserves_private_particles() {
        let game=&crate::test_support::game_path("");
        let card=game.join("UserData/chara/female/[Card] March_7th_keiyuu.png");
        let mut s=open(game,Some(&card),0).unwrap();
        let clip=std::ffi::CString::new("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01").unwrap();
        unsafe {assert_eq!(crate::ffi::set_anim_clip(&mut s,clip.as_ptr()),0);}
        s.apply_anim_frame(0.15,0.15);
        let states=|s:&OkSession|s.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|a|a.state.as_ref()).map(|state|state.skel.worlds.clone())).collect::<Vec<_>>();
        let before=states(&s);assert!(before.iter().any(Option::is_some));
        let mut checked=0;
        for runtime in s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()).filter(|r|r.state.is_some()) {
            let mut probe=runtime.clone();let particles=probe.state.as_ref().unwrap().particle_snapshot();
            let mut flags=probe.collider_flags.unwrap();flags.floor=!flags.floor;
            let body=if probe.coordinated {s.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel).or(s.body_skel.as_ref())}else{s.body_skel.as_ref()};
            probe.update_render(body,s.head_full_posed.as_ref().or(s.head_skel.as_ref()),&s.body_colliders,flags,0.0);
            assert_eq!(probe.state.as_ref().unwrap().particle_snapshot(),particles,"ordinary collider flags must not rerun 240 warmup steps");checked+=1;
        }
        assert!(checked>0);
        let invalid=std::ffi::CString::new("states:studio/anime/00.unity3d|missing").unwrap();
        unsafe {assert_ne!(crate::ffi::set_anim_clip(&mut s,invalid.as_ptr()),0);}
        assert_eq!(s.anim_time,0.15);assert_eq!(states(&s),before);
        unsafe {assert_eq!(crate::ffi::set_anim_clip(&mut s,clip.as_ptr()),0);}
        assert_eq!(s.anim_time,0.0);assert_eq!(states(&s),before,"Animator switch must preserve warm private rig/particles");
        let encoded=serde_json::to_vec(&["studio/anime/00.unity3d","walk_run","",""]).unwrap().iter().map(|b|format!("{b:02X}")).collect::<String>();
        let studio=std::ffi::CString::new(format!("states:studio@{encoded}|walk_run / [Studio layer 0] Base Layer.f_aruki_00_00_01")).unwrap();
        let particles=|s:&OkSession|s.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|a|a.state.as_ref()).map(|state|state.particle_snapshot())).collect::<Vec<_>>();
        let previous_particles=particles(&s);
        unsafe {assert_eq!(crate::ffi::set_studio_animation(&mut s,studio.as_ptr(),0,0.,0.,0,0.),0);}
        assert_eq!(states(&s),before,"Studio selection must preserve existing accessory rigs");
        assert_eq!(particles(&s),previous_particles,"Studio selection must preserve private particle history");
        let body=s.body_skel.as_ref().unwrap();
        let mut body_runtime=s.body_dynamic_runtime.as_ref().unwrap().clone();
        let body_before=body_runtime.particle_snapshot();
        for mode in [4,2,4,2] {
            let flags=s.collider_settings.resolve(mode==4,s.collider_overrides);
            body_runtime.configure_colliders(body,flags,s.shape_body.get(4).copied().unwrap_or(0.5));
            assert_eq!(body_runtime.particle_snapshot(),body_before,"body collider changes must preserve history and timers");
            if let Some((_,state))=&s.clothes_dynamic_runtime {
                let mut probe=state.clone();let before=probe.particle_snapshot();
                probe.configure_character_colliders(body,&s.body_colliders,flags);
                assert_eq!(probe.particle_snapshot(),before,"clothes collider changes must preserve history and timers");
            }
        }
        eprintln!("OK Studio body/clothes collider lifetime exact=1 switches=4");
        eprintln!("OK Studio private lifetime exact=1 rig=1 particles=1");
        s.reset_dynamic_accessories();assert!(states(&s).iter().all(Option::is_none),"explicit resource reset remains available");
    }
    use super::*;
    use std::path::PathBuf;

    #[test]
    #[ignore="explicit APNG via OK_TEST_LEGACY_PORTABLE; independent native stream oracle"]
    fn private_skin_stages_match_solved_geometry() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut session=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut checked=0usize;let mut vertices=0usize;let mut max_error=0f32;let mut missing_stream_parts=0usize;
        for dt in [0.,1./120.,0.017] {
            session.update_dynamic_accessories_impl(dt,false);
            for part in &session.parts {
                let Some(runtime)=&part.runtime_accessory else{continue;};
                let body=runtime.skin_body(session.body_skel.as_ref(),session.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel));let head=session.head_full_posed.as_ref().or(session.head_skel.as_ref());
                let Some((mesh,matrices,post))=runtime.export_skin_stages(body,head) else{continue;};
                let Some((positions,normals,tangents))=runtime.render_solved(body,head,AccessoryFramePolicy::read(),None) else{continue;};
                assert_eq!(mesh.positions,mesh.rest_positions,"staged source must be the same rest points used by native skinning");
                let rest_normals=if mesh.rest_normals.len()==mesh.normals.len(){&mesh.rest_normals}else{&mesh.normals};
                if rest_normals.len()!=positions.len()||mesh.tangents.len()!=positions.len(){missing_stream_parts+=1;}
                let normalize=|v:[f32;3]|{let l=(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]).sqrt();if l>1e-8{[v[0]/l,v[1]/l,v[2]/l]}else{[0.,0.,1.]}};
                for i in 0..positions.len() {
                    let point=mesh.rest_positions[i];let normal=rest_normals.get(i).copied().unwrap_or([0.;3]);let tangent=mesh.tangents.get(i).copied().unwrap_or([0.;4]);
                    let mut p=[0.;3];let mut n=[0.;3];let mut t=[0.;3];let mut total=0.;
                    for k in 0..4 {
                        let w=mesh.weights[i][k];let b=mesh.bone_idx[i][k] as usize;
                        if w.abs()<1e-6||b>=matrices.len()-1{continue;}
                        let m=&matrices[b];
                        for axis in 0..3 {
                            p[axis]+=(m[axis][0]*point[0]+m[axis][1]*point[1]+m[axis][2]*point[2]+m[axis][3])*w;
                            n[axis]+=(m[axis][0]*normal[0]+m[axis][1]*normal[1]+m[axis][2]*normal[2])*w;
                            t[axis]+=(m[axis][0]*tangent[0]+m[axis][1]*tangent[1]+m[axis][2]*tangent[2])*w;
                        }total+=w;
                    }
                    if total>1e-5 {for axis in 0..3{p[axis]/=total;n[axis]/=total;t[axis]/=total;}}
                    else {p=point;n=normal;t=[tangent[0],tangent[1],tangent[2]];}
                    n=normalize(n);t=normalize(t);
                    if !runtime.copy_weights_body {crate::pose::apply_mat(std::slice::from_mut(&mut p),&post);n=crate::pose::rot_normal(&post,n);t=crate::pose::rot_normal(&post,t);}
                    for axis in 0..3 {
                        let mut compare=vec![(p[axis],positions[i][axis])];
                        if let Some(normal)=normals.get(i){compare.push((n[axis],normal[axis]));}
                        if let Some(tangent)=tangents.get(i){compare.push((t[axis],tangent[axis]));}
                        for (a,b) in compare {assert!(a.is_finite()&&b.is_finite());max_error=max_error.max((a-b).abs());}
                    }
                    if let Some(actual)=tangents.get(i){assert_eq!(tangent[3],actual[3]);}vertices+=1;
                }checked+=1;
            }
        }
        assert!(checked>0&&vertices>10000,"actual APNG must cover private geometry");
        assert!(max_error<=1e-5,"staged native streams differ from independent renderer: {max_error}");
        // Exercise the exported count/copy ABI and failed-read atomicity too.
        let mut copied=0;let mut revision_reads=0;
        for index in 0..session.parts.len() {unsafe {
            let count=crate::ffi::ok_part_private_skin_stages(&mut session,index as i32,std::ptr::null_mut(),std::ptr::null_mut(),0,std::ptr::null_mut());
            if count<=0{continue;}
            let mut values=vec![9f32;count as usize*16];let mut post=[9f32;16];let mut view:crate::ffi::OkSkinPaletteView=std::mem::zeroed();view.n_verts=-73;
            assert_eq!(crate::ffi::ok_part_private_skin_stages(&mut session,index as i32,&mut view,values.as_mut_ptr(),count-1,post.as_mut_ptr()),-1);
            assert!(values.iter().all(|v|*v==9.)&&post.iter().all(|v|*v==9.)&&view.n_verts==-73);
            assert_eq!(crate::ffi::ok_part_private_skin_stages(&mut session,index as i32,&mut view,values.as_mut_ptr(),count,post.as_mut_ptr()),count);
            assert!(view.n_verts>0);assert_eq!(&values[values.len()-16..],crate::pose::ident().as_flattened());copied+=1;
            let revision=crate::ffi::ok_part_private_skin_source_revision(&mut session,index as i32);assert_ne!(revision,0);
            let runtime=session.parts[index].runtime_accessory.as_ref().unwrap();
            assert_eq!(runtime.current_private_source_revision(),Some(revision),"source ownership must equal staged layout without rebuilding pose");
            assert_eq!(runtime.source_validation.1.load(std::sync::atomic::Ordering::Relaxed),1);
            let expected_values=values.clone();let expected_post=post;
            for _ in 0..8 {
                assert_eq!(crate::ffi::ok_part_private_skin_stages_revision(&mut session,index as i32,revision,&mut view,values.as_mut_ptr(),count,post.as_mut_ptr()),count);
                assert_eq!(values,expected_values);assert_eq!(post,expected_post);revision_reads+=1;
            }
            assert_eq!(session.parts[index].runtime_accessory.as_ref().unwrap().source_validation.1.load(std::sync::atomic::Ordering::Relaxed),1,"warm reads must not rescan vertices");
            assert_eq!(crate::ffi::ok_part_private_skin_stages_revision(&mut session,index as i32,revision+1,&mut view,values.as_mut_ptr(),count,post.as_mut_ptr()),-1);
            assert_eq!(values,expected_values);assert_eq!(post,expected_post);
            let clone=session.parts[index].runtime_accessory.as_ref().unwrap().clone();
            assert_ne!(clone.private_source_revision(count as usize).unwrap(),revision);
            let runtime=session.parts[index].runtime_accessory.as_mut().unwrap();let replacement=runtime.source.clone();runtime.replace_source(replacement);
            assert_eq!(crate::ffi::ok_part_private_skin_stages_revision(&mut session,index as i32,revision,&mut view,values.as_mut_ptr(),count,post.as_mut_ptr()),-1);
            assert_eq!(values,expected_values);assert_eq!(post,expected_post);
        }}
        assert!(copied>0);eprintln!("private staged native oracle parts={checked} vertices={vertices} ffi_parts={copied} revision_reads={revision_reads} source_scans_per_layout=1 missing_stream_parts={missing_stream_parts} max_error={max_error}");
    }

    #[test]
    fn private_skin_source_revision_lifecycle() {
        let source=ExtractedMesh {rest_positions:vec![[1.,2.,3.]],rest_normals:vec![[0.,0.,1.]],
            tangents:vec![[1.,0.,0.,-1.]],weights:vec![[1.5,-0.5,0.,0.]],bone_idx:vec![[0,1,65535,65535]],..Default::default()};
        let mut runtime=AccessoryRuntime {source,source_validation:Default::default(),
            parent_key:String::new(),local_placement:crate::pose::ident(),copy_weights_body:false,
            implants_active:false,implanted_bones:Default::default(),body_bridges:Vec::new(),
            clothes_component:String::new(),coordinated:false,removed_accessory_bones:Default::default(),state:None,collider_flags:None};
        let revision=runtime.private_source_revision(3).unwrap();
        for _ in 0..20 {assert_eq!(runtime.private_source_revision(3),Some(revision));}
        assert_eq!(runtime.source_validation.1.load(std::sync::atomic::Ordering::Relaxed),1);
        // Pose placement changes do not modify rest streams or bone slots.
        runtime.local_placement[0][3]=7.;
        assert_eq!(runtime.private_source_revision(3),Some(revision));
        assert_ne!(runtime.clone().private_source_revision(3).unwrap(),revision);
        let restored:AccessoryRuntime=bincode::deserialize(&bincode::serialize(&runtime).unwrap()).unwrap();
        assert_ne!(restored.private_source_revision(3).unwrap(),revision);
        let expanded=runtime.private_source_revision(4).unwrap();assert_ne!(expanded,revision);
        assert_ne!(runtime.private_source_revision(3).unwrap(),revision,"returning layout cannot revive old token");
        let mut replacement=runtime.source.clone();replacement.rest_positions[0][0]=42.;runtime.replace_source(replacement);
        assert_ne!(runtime.private_source_revision(3).unwrap(),revision);
        let valid=runtime.source.clone();let mut broken=valid.clone();broken.weights[0][0]=f32::NAN;runtime.replace_source(broken);
        assert_eq!(runtime.private_source_revision(3),None);assert_eq!(runtime.private_source_revision(3),None);
        assert_eq!(runtime.source_validation.1.load(std::sync::atomic::Ordering::Relaxed),1,"invalid source is cached too");
        runtime.replace_source(valid.clone());assert_eq!(runtime.private_source_revision(2),None,"authored slot cannot address fallback");
        runtime.replace_source(valid.clone());runtime.source.rest_positions[0][0]=f32::INFINITY;
        assert_eq!(runtime.private_source_revision(3),None);
        runtime.replace_source(valid);assert!(runtime.private_source_revision(3).is_some());
    }

    #[test]
    #[ignore="explicit APNG via OK_TEST_LEGACY_PORTABLE; exact serial geometry and particle oracle"]
    fn private_geometry_batch_matches_serial_and_rejects_partial_queries() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();let mut session=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let indices=session.parts.iter().enumerate().filter_map(|(i,p)|p.runtime_accessory.as_ref().map(|_|i)).collect::<Vec<_>>();assert!(indices.len()>=4);
        for dt in [0.,1./120.,0.017] {
            session.update_dynamic_accessories_impl(dt,false);
            for index in &indices {assert!(session.query_private_geometry(*index));}
            let expected=indices.iter().map(|i|{let p=&session.parts[*i];(p.positions.clone(),p.normals.clone(),p.rest_tangents.clone())}).collect::<Vec<_>>();
            let history=session.parts.iter().filter_map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|s|s.particle_snapshot())).collect::<Vec<_>>();
            assert!(session.query_private_geometries(&indices));
            for(index,expected)in indices.iter().zip(&expected){let p=&session.parts[*index];assert_eq!((&p.positions,&p.normals,&p.rest_tangents),(&expected.0,&expected.1,&expected.2));}
            assert_eq!(history,session.parts.iter().filter_map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|s|s.particle_snapshot())).collect::<Vec<_>>());
            assert!(!session.query_private_geometries(&[indices[0],usize::MAX]));assert!(!session.query_private_geometries(&[indices[0],indices[0]]));
            for(index,expected)in indices.iter().zip(&expected){let p=&session.parts[*index];assert_eq!((&p.positions,&p.normals,&p.rest_tangents),(&expected.0,&expected.1,&expected.2));}
        }
        unsafe {assert_eq!(crate::ffi::ok_query_private_skin_geometry_batch(std::ptr::null_mut(),std::ptr::null(),0),1);assert_eq!(crate::ffi::ok_query_private_skin_geometry_batch(&mut session,std::ptr::null(),1),1);assert_eq!(crate::ffi::ok_query_private_skin_geometry_batch(&mut session,std::ptr::null(),0),0);}
        eprintln!("private batch exact positions/normals/tangents parts={} samples=3 particle_history_unchanged=1 invalid_atomic=1",indices.len());
    }

    #[test]
    #[ignore="read-only existing portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn legacy_portable_source_contract_remains_loadable() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let source=crate::gamefs::read(&path).unwrap();
        let session=crate::portable::load(std::path::Path::new(&path)).unwrap();
        assert!(!session.parts.is_empty());
        for part in &session.parts {
            if let Some(runtime)=&part.runtime_accessory {assert!(!runtime.skin_contract().unwrap().is_empty());}
        }
        assert_eq!(source,crate::gamefs::read(&path).unwrap());
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn pose_only_accessories_match_render_and_explicit_geometry() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut cpu=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut palette=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut checked=0;
        for dt in [0.,1./60.,0.017,0.025] {
            cpu.update_dynamic_accessories_impl(dt,false);
            palette.update_dynamic_accessories_impl(dt,true);
            for (a,b) in cpu.parts.iter().zip(&palette.parts) {
                if let (Some(a),Some(b))=(&a.runtime_accessory,&b.runtime_accessory) {
                    let left=a.export_skin_palette(cpu.body_skel.as_ref(),cpu.head_full_posed.as_ref().or(cpu.head_skel.as_ref()));
                    let right=b.export_skin_palette(palette.body_skel.as_ref(),palette.head_full_posed.as_ref().or(palette.head_skel.as_ref()));
                    if let (Some((_,a)),Some((_,b)))=(left,right) {assert_eq!(a,b);checked+=1;}
                }
            }
        }
        assert!(checked>0,"fixture must cover private palettes");
        // A caller can request CPU positions after pose-only stepping. This
        // must preserve the same solved particles, not advance dynamics again.
        palette.update_dynamic_accessories_impl(0.,false);
        let mut vertices=0;
        for (a,b) in cpu.parts.iter().zip(&palette.parts) {
            if a.runtime_accessory.is_some() {
                assert_eq!(a.positions,b.positions);assert_eq!(a.normals,b.normals);
                vertices+=a.positions.len();
            }
        }
        assert!(vertices>0);
        eprintln!("pose-only accessory parity palettes={checked} explicit_cpu_vertices={vertices}");
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn solved_accessory_render_matches_legacy_refresh_history() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut old=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut new=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut compared=0;
        for frame in 0..40 {
            let dt=[0.,1./60.,0.017,0.025][frame%4];
            for s in [&mut old,&mut new] {
                let body=s.body_skel.as_mut().unwrap();
                for tf in body.tfs.values_mut().filter(|tf|tf.parent.is_none()){tf.pos[0]=frame as f32*0.003;tf.rot=[0.,0.,(frame as f32*0.01).sin(),(frame as f32*0.01).cos()];}
                body.rebuild_worlds();s.update_dynamic_accessories_impl(dt,true);
            }
            old.update_dynamic_accessories_impl(0.,false);
            let before:Vec<_>=new.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|state|state.particle_snapshot())).collect();
            new.render_solved_accessories();new.render_solved_accessories();
            let after:Vec<_>=new.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|state|state.particle_snapshot())).collect();
            assert_eq!(before,after,"render consumption must not mutate particles");
            for (a,b) in old.parts.iter().zip(&new.parts) {
                if let (Some(ar),Some(br))=(&a.runtime_accessory,&b.runtime_accessory){
                    assert_eq!(a.positions,b.positions,"positions {} frame={frame}",a.name);
                    assert_eq!(a.normals,b.normals,"normals {} frame={frame}",a.name);
                    assert_eq!(a.rest_tangents,b.rest_tangents,"tangents {} frame={frame}",a.name);
                    assert_eq!(ar.state.as_ref().map(|s|s.particle_snapshot()),br.state.as_ref().map(|s|s.particle_snapshot()));
                    assert_eq!(ar.state.as_ref().map(|s|&s.skel.worlds),br.state.as_ref().map(|s|&s.skel.worlds));compared+=a.positions.len();
                }
            }
        }
        old.reset_dynamic_accessories();new.reset_dynamic_accessories();
        old.update_dynamic_accessories_impl(0.,false);new.render_solved_accessories();
        for (a,b) in old.parts.iter().zip(&new.parts) {if let (Some(ar),Some(br))=(&a.runtime_accessory,&b.runtime_accessory){
            assert_eq!(a.positions,b.positions,"reset geometry {}",a.name);
            assert_eq!(a.normals,b.normals);assert_eq!(a.rest_tangents,b.rest_tangents);
            assert_eq!(ar.state.as_ref().map(|s|s.particle_snapshot()),br.state.as_ref().map(|s|s.particle_snapshot()));
        }}
        assert!(compared>0);eprintln!("solved accessory exact geometry and particle history vertices={compared}");
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn live_shape_baselines_and_body_seams_match_full_shape() {
        std::env::set_var("OK_SKELMESH","0");
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut live=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut full=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut staged=crate::portable::load(std::path::Path::new(&path)).unwrap();
        staged.apply_shape(&staged.shape_body.clone(),&staged.shape_face.clone());
        live.apply_shape(&live.shape_body.clone(),&live.shape_face.clone());live.skin_all();
        let mut seams=Vec::new();
        for (part,p) in live.parts.iter().enumerate().filter(|(_,p)|p.skel_kind==1&&p.role=="body") {
            let mut points=HashMap::<[i32;3],usize>::new();
            for (i,pos) in p.rest_positions.iter().enumerate(){let key=pos.map(|v|(v*1e6).round() as i32);
                if let Some(j)=points.insert(key,i){if p.positions[i].iter().zip(p.positions[j]).all(|(a,b)|(a-b).abs()<1e-5){seams.push((part,i,j));}}
            }
        }
        assert!(!seams.is_empty(),"fixture must cover body UV seam duplicates");
        let original=live.shape_body.clone();let face=live.shape_face.clone();let mut max_seam=0f32;let mut worst_ms=0f64;
        for index in 0..original.len() {
            let mut body=original.clone();body[index]=if index%2==0 {0.3}else{0.7};
            let particles:Vec<_>=live.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|s|s.particle_snapshot())).collect();
            let begin=std::time::Instant::now();live.apply_shape_live(&body,&face);live.skin_all();worst_ms=worst_ms.max(begin.elapsed().as_secs_f64()*1000.);
            full.apply_shape(&body,&face);
            assert_eq!(live.body_shaped.as_ref().map(|s|&s.worlds),full.body_shaped.as_ref().map(|s|&s.worlds),"body shape {index}");
            assert_eq!(live.head_shaped.as_ref().map(|s|&s.worlds),full.head_shaped.as_ref().map(|s|&s.worlds),"head shape {index}");
            let after:Vec<_>=live.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|s|s.particle_snapshot())).collect();assert_eq!(particles,after,"live edit reset private particle history {index}");
            for (part,a,b) in &seams {let p=&live.parts[*part];let d=p.positions[*a].iter().zip(p.positions[*b]).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt();max_seam=max_seam.max(d);assert!(d<0.0005,"body seam split index={index} part={} vertices={a}/{b} distance_m={d}",p.name);}
            let staged_particles:Vec<_>=staged.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|s|s.particle_snapshot())).collect();
            staged.prepare_shape_live(&body,&face);
            assert_eq!(staged.body_shaped.as_ref().map(|s|&s.worlds),full.body_shaped.as_ref().map(|s|&s.worlds));
            assert_eq!(staged.head_shaped.as_ref().map(|s|&s.worlds),full.head_shaped.as_ref().map(|s|&s.worlds));
            assert_eq!(staged.head_full_posed.as_ref().map(|s|&s.worlds),full.head_full_posed.as_ref().map(|s|&s.worlds),"cached full head shape {index}");
            assert_eq!(staged_particles,staged.parts.iter().map(|p|p.runtime_accessory.as_ref().and_then(|r|r.state.as_ref()).map(|s|s.particle_snapshot())).collect::<Vec<_>>());
            let time=(index+1) as f32/60.;
            staged.apply_anim_frame(time,1./60.);staged.skin_all();
            for p in &staged.parts {assert!(p.positions.iter().flatten().chain(p.normals.iter().flatten()).all(|v|v.is_finite()),"nonfinite staged shape {index} {}",p.name);}
            for (part,a,b) in &seams {let p=&staged.parts[*part];let d=p.positions[*a].iter().zip(p.positions[*b]).map(|(a,b)|(a-b).powi(2)).sum::<f32>().sqrt();assert!(d<0.0005,"animated seam split index={index} part={} distance_m={d}",p.name);}
        }
        live.apply_shape_live(&original,&face);assert_eq!(live.shape_body,original);
        eprintln!("live shape parameters={} seam_pairs={} max_seam_m={max_seam:.9} native_update_and_skin_max_ms={worst_ms:.6}",original.len(),seams.len());
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn borrowed_skin_buffers_match_clone_oracle() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut oracle=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut reused=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut checked=0;
        for frame in 0..30 {
            for session in [&mut oracle,&mut reused] {
                if let Some(body)=&mut session.body_skel {
                    for tf in body.tfs.values_mut().filter(|tf|tf.parent.is_none()) {tf.pos[0]=frame as f32*0.003;tf.rot=[0.,0.,(frame as f32*0.01).sin(),(frame as f32*0.01).cos()];}
                    body.rebuild_worlds();
                }
            }
            oracle.skin_all_impl(false);reused.skin_all_impl(true);
            for (a,b) in oracle.parts.iter().zip(&reused.parts) {
                assert_eq!(a.positions,b.positions,"positions {} frame={frame}",a.name);
                assert_eq!(a.normals,b.normals,"normals {} frame={frame}",a.name);
                assert_eq!(a.rest_positions,b.rest_positions);assert_eq!(a.rest_normals,b.rest_normals);
                assert_eq!(a.indices,b.indices);assert_eq!(a.weights,b.weights);assert_eq!(a.bone_idx,b.bone_idx);assert_eq!(a.bind_poses,b.bind_poses);
                assert_eq!(a.uvs,b.uvs);assert_eq!(a.uv2,b.uv2);assert_eq!(a.uv3,b.uv3);assert_eq!(a.uv4,b.uv4);assert_eq!(a.vertex_colors,b.vertex_colors);
                if a.runtime_accessory.is_none(){checked+=a.positions.len();}
            }
        }
        assert!(checked>0);eprintln!("borrowed skin exact parity vertices={checked}");
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn parallel_private_dynamics_matches_serial_history() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut serial=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut parallel=crate::portable::load(std::path::Path::new(&path)).unwrap();
        assert!(serial.parts.iter().filter(|p|p.runtime_accessory.is_some()).count()>=8);
        for frame in 0..30 {
            for session in [&mut serial,&mut parallel] {
                if let Some(body)=&mut session.body_skel {
                    for tf in body.tfs.values_mut().filter(|tf|tf.parent.is_none()) {tf.pos[0]=frame as f32*0.001;}
                    body.rebuild_worlds();
                }
            }
            let dt=[1./120.,1./60.,1./30.][frame%3];
            serial.update_dynamic_accessories_dispatch(dt,false,false);
            parallel.update_dynamic_accessories_dispatch(dt,false,true);
            for (a,b) in serial.parts.iter().zip(&parallel.parts) {
                assert_eq!(a.positions,b.positions);assert_eq!(a.normals,b.normals);assert_eq!(a.rest_tangents,b.rest_tangents);
                if let (Some(a),Some(b))=(&a.runtime_accessory,&b.runtime_accessory) {
                    assert_eq!(a.state.as_ref().map(|s|s.particle_snapshot()),b.state.as_ref().map(|s|s.particle_snapshot()));
                }
            }
        }
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn assigned_world_frame_matches_legacy_and_refreshes() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut session=crate::portable::load(std::path::Path::new(&path)).unwrap();
        session.update_dynamic_accessories_dispatch(1./120.,false,true);
        let mut checked=0;let mut previous=None;
        for frame in 0..6 {
            let body=session.body_skel.as_mut().unwrap();
            for tf in body.tfs.values_mut().filter(|tf|tf.parent.is_none()) {
                tf.pos[0]=frame as f32*0.013;tf.rot=[0.,0.,(frame as f32*0.03).sin(),(frame as f32*0.03).cos()];
                tf.scale=[1.+frame as f32*0.01,0.9,1.1];
            }
            body.rebuild_worlds();
            // Exercise coordinated/missing implanted slots without changing physics.
            if frame==3 {for part in &mut session.parts {if let Some(r)=part.runtime_accessory.as_mut().filter(|r|r.copy_weights_body){
                r.coordinated=true;
                if let Some(id)=r.source.smr_skel.as_ref().and_then(|s|s.bone_ids.first()){r.implanted_bones.insert(*id);}
            }}}
            let body=session.body_skel.as_ref();
            let normal=AssignedWorldFrame::prepare(body,&session.parts,false);
            let coordinated=AssignedWorldFrame::prepare(body,&session.parts,true);
            let empty=AssignedWorldFrame{worlds:HashMap::new()};
            for r in session.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()).filter(|r|r.copy_weights_body) {
                let private=r.state.as_ref().map(|s|&s.skel).or(r.source.smr_skel.as_ref()).unwrap();
                let correction=crate::pose::ident();
                let cache=if r.coordinated{coordinated.as_ref()}else{normal.as_ref()};
                assert!(cache.is_some());
                let reference=r.effective_worlds(private,&correction,body,false,None,None);
                assert_eq!(r.effective_worlds(private,&correction,body,true,cache,None),reference);
                assert_eq!(r.effective_worlds(private,&correction,body,false,Some(&empty),None),reference,"uncached inventory fallback");
                let names=body.map(|body|crate::pose::rig_name_index(body,false));
                assert_eq!(r.effective_worlds(private,&correction,body,false,None,names.as_deref()),reference,"batch body name index must preserve live world evaluation");
                let policy=AccessoryFramePolicy::read();
                assert_eq!(r.skin_palette_with_policy(private,&correction,body,policy,cache),
                    r.skin_palette_with_policy(private,&correction,body,policy,None),"all geometry streams");
                checked+=reference.len();
            }
            let maps=(normal.map(|v|v.worlds),coordinated.map(|v|v.worlds));
            if let Some(old)=previous {assert_ne!(maps,old,"new pose reused stale worlds");}previous=Some(maps);
        }
        assert!(checked>0);eprintln!("assigned world frame exact matrices={checked}; changing pose/coordinated/missing/fallback PASS");
    }

    #[test]
    #[ignore="read-only portable fixture via OK_TEST_LEGACY_PORTABLE"]
    fn private_skin_observer_matches_legacy_and_is_scoped() {
        type Record=(i32,usize,Vec<f32>,Vec<f32>,Vec<f32>);
        unsafe extern "C" fn capture(context:*mut std::ffi::c_void,role:i32,index:i32,count:i32,p:*const f32,n:*const f32,t:*const f32){
            let records=&*(context as *const std::sync::Mutex<Vec<Record>>);
            records.lock().unwrap().push((role,index as usize,std::slice::from_raw_parts(p,count as usize*3).to_vec(),
                std::slice::from_raw_parts(n,count as usize*3).to_vec(),if t.is_null(){vec![]}else{std::slice::from_raw_parts(t,count as usize*4).to_vec()}));
        }
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut oracle=crate::portable::load(std::path::Path::new(&path)).unwrap();let mut observed=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let records=std::sync::Mutex::new(Vec::<Record>::new());
        let observer=crate::ffi::PrivateSkinObserver{callback:capture,context:&records as *const _ as usize,role:1};
        let mut checked=0;
        for frame in 0..4 {
            for s in [&mut oracle,&mut observed]{
                if let Some(body)=&mut s.body_skel{for tf in body.tfs.values_mut().filter(|tf|tf.parent.is_none()){tf.pos[0]=frame as f32*0.003;}body.rebuild_worlds();}
                s.update_dynamic_accessories_dispatch(1./120.,true,true);
            }
            oracle.render_solved_accessories();observed.render_solved_accessories_observed(Some(observer));
            let mut captured=records.lock().unwrap();assert!(!captured.is_empty());
            let mut seen=std::collections::HashSet::new();
            for (role,index,p,n,t) in captured.iter(){assert_eq!(*role,1);assert!(seen.insert(*index));let part=&observed.parts[*index];
                assert_eq!(*p,part.positions.iter().flatten().copied().collect::<Vec<_>>());
                assert_eq!(*n,part.normals.iter().flatten().copied().collect::<Vec<_>>());
                assert_eq!(*t,part.rest_tangents.iter().flatten().copied().collect::<Vec<_>>());checked+=part.positions.len();}
            for (a,b) in oracle.parts.iter().zip(&observed.parts){assert_eq!(a.positions,b.positions);assert_eq!(a.normals,b.normals);assert_eq!(a.rest_tangents,b.rest_tangents);}
            captured.clear();drop(captured);
            // Ordinary queries and clones must not retain the observer/context.
            observed.render_solved_accessories();
            let mut copied=OkSession::from_animation_bytes(&observed.to_animation_bytes().unwrap()).unwrap();copied.render_solved_accessories();
            assert!(records.lock().unwrap().is_empty());
        }
        // Non-shared sessions emit during dynamics; shared palette-only sessions
        // emit during render consumption. Neither branch may miss completion.
        oracle.update_dynamic_accessories_dispatch(1./120.,false,true);
        observed.update_dynamic_accessories_dispatch_observed(1./120.,false,true,Some(observer));
        let captured=records.lock().unwrap();assert!(!captured.is_empty());let mut seen=std::collections::HashSet::new();
        for (role,index,p,n,t) in captured.iter(){assert_eq!(*role,1);assert!(seen.insert(*index));let part=&observed.parts[*index];
            assert_eq!(*p,part.positions.iter().flatten().copied().collect::<Vec<_>>());assert_eq!(*n,part.normals.iter().flatten().copied().collect::<Vec<_>>());
            assert_eq!(*t,part.rest_tangents.iter().flatten().copied().collect::<Vec<_>>());checked+=part.positions.len();}
        for (a,b) in oracle.parts.iter().zip(&observed.parts){assert_eq!(a.positions,b.positions);assert_eq!(a.normals,b.normals);assert_eq!(a.rest_tangents,b.rest_tangents);}
        assert!(checked>0);eprintln!("private observer exact vertices={checked}, both completion branches and scoped queries/clones PASS");
    }

    #[test]
    #[ignore="explicit APNG; native resource renderer exclusion preserves full solver"]
    fn private_gpu_native_dynamics_exclusions_match_full_solver(){
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut oracle=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut gpu=crate::portable::load(std::path::Path::new(&path)).unwrap();
        for s in [&mut oracle,&mut gpu]{s.update_dynamic_accessories_dispatch(0.,false,true);}
        let excluded=gpu.parts.iter().enumerate().filter_map(|(i,p)|p.runtime_accessory.as_ref().filter(|r|r.current_private_source_revision().is_some()).map(|_|i)).filter(|i|i%2==0).collect::<Vec<_>>();
        assert!(!excluded.is_empty());let mut vertices=0;
        let particles=|s:&OkSession|s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref().and_then(|r|r.dynamic_state_for_test()).map(|d|d.particle_snapshot())).collect::<Vec<_>>();
        for frame in 0..6{
            let before=gpu.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();
            for s in [&mut oracle,&mut gpu]{if let Some(body)=&mut s.body_skel{for tf in body.tfs.values_mut().filter(|t|t.parent.is_none()){tf.pos[0]=frame as f32*0.003;}body.rebuild_worlds();}}
            oracle.update_dynamic_accessories_dispatch(1./120.,false,true);
            gpu.update_dynamic_accessories_filtered(1./120.,false,true,None,&excluded);
            assert_eq!(particles(&gpu),particles(&oracle),"GPU exclusions must retain identical 120Hz physics");
            for (i,p) in gpu.parts.iter().enumerate(){assert_eq!(&p.positions,if excluded.contains(&i){&before[i]}else{&oracle.parts[i].positions});}
            let history=particles(&gpu);
            for &i in &excluded{assert!(gpu.query_private_geometry(i));assert_eq!(gpu.parts[i].positions,oracle.parts[i].positions);assert_eq!(gpu.parts[i].normals,oracle.parts[i].normals);assert_eq!(gpu.parts[i].rest_tangents,oracle.parts[i].rest_tangents);vertices+=gpu.parts[i].positions.len();}
            assert_eq!(history,particles(&gpu));
        }
        assert!(vertices>10000);eprintln!("Native GPU excluded geometry vertices={vertices}; solver/particles/queries exact at120Hz");
    }
    #[test]
    #[ignore="read-only portable fixture; owns test-only implant oracle switch"]
    fn implant_empty_sync_matches_unconditional_rebuild_history() {
        use crate::dbone::implant::FORCE_SYNC_REBUILD;
        use std::sync::atomic::Ordering;
        struct Reset;impl Drop for Reset {fn drop(&mut self){FORCE_SYNC_REBUILD.store(false,Ordering::Relaxed);}}
        let _reset=Reset;
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let mut oracle=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let mut reused=crate::portable::load(std::path::Path::new(&path)).unwrap();
        let empty=reused.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()).filter(|r|r.body_bridges.is_empty()).count();
        assert!(empty>0,"fixture must exercise an unimplanted private rig");
        for frame in 0..60 {
            for session in [&mut oracle,&mut reused] {
                if let Some(body)=&mut session.body_skel {
                    for tf in body.tfs.values_mut().filter(|tf|tf.parent.is_none()) {tf.pos[0]=frame as f32*0.001;}
                    body.rebuild_worlds();
                }
            }
            let dt=[1./120.,1./60.,1./30.][frame%3];
            FORCE_SYNC_REBUILD.store(true,Ordering::Relaxed);oracle.update_dynamic_accessories_dispatch(dt,false,false);
            FORCE_SYNC_REBUILD.store(false,Ordering::Relaxed);reused.update_dynamic_accessories_dispatch(dt,false,false);
            for (a,b) in oracle.parts.iter().zip(&reused.parts) {
                assert_eq!(a.positions,b.positions,"positions {} frame={frame}",a.name);
                assert_eq!(a.normals,b.normals);assert_eq!(a.rest_tangents,b.rest_tangents);
                if let (Some(a),Some(b))=(&a.runtime_accessory,&b.runtime_accessory) {
                    assert_eq!(a.state.as_ref().map(|s|s.particle_snapshot()),b.state.as_ref().map(|s|s.particle_snapshot()));
                    assert_eq!(a.source.smr_skel.as_ref().map(|s|&s.worlds),b.source.smr_skel.as_ref().map(|s|&s.worlds));
                    assert_eq!(a.state.as_ref().map(|s|&s.skel.worlds),b.state.as_ref().map(|s|&s.skel.worlds));
                }
            }
        }
        eprintln!("empty implant rebuild oracle exact frames=60 rigs={empty}");
    }

    #[test]
    fn immutable_skin_contract_detects_private_rig_changes() {
        let tf=crate::pose::Tf{name:"root".into(),parent:None,sibling_index:Some(0),
            pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
        let rig=Skeleton{names:vec!["root".into()],worlds:vec![crate::pose::ident()],
            rest:vec![crate::pose::ident()],smr_count:1,
            tfs:[(2,tf.clone()),(1,tf)].into(),bone_ids:vec![2],source_mesh:0};
        let original=authored_rig_bytes(&rig,&[]).unwrap();
        let mut reordered=rig.clone();
        let mut entries:Vec<_>=rig.tfs.iter().map(|(id,tf)|(*id,tf.clone())).collect();
        entries.reverse();reordered.tfs=entries.into_iter().collect();
        assert_eq!(original,authored_rig_bytes(&reordered,&[]).unwrap());
        for field in 0..4 {
            let mut changed=rig.clone();let tf=changed.tfs.get_mut(&2).unwrap();
            match field {0=>tf.parent=Some(1),1=>tf.pos[0]=0.2,
                2=>tf.scale[0]=2.,_=>tf.name="changed".into()};
            assert_ne!(original,authored_rig_bytes(&changed,&[]).unwrap());
        }
        let runtime=AccessoryRuntime{source:ExtractedMesh{smr_skel:Some(rig),..Default::default()},source_validation:Default::default(),
            parent_key:"socket".into(),local_placement:crate::pose::ident(),
            copy_weights_body:false,implants_active:false,implanted_bones:[1,2].into(),
            body_bridges:vec![(2,"body-root".into())],clothes_component:String::new(),coordinated:false,
            removed_accessory_bones:[3,4].into(),state:None,collider_flags:None};
        let baseline=runtime.skin_contract().unwrap();
        let mut live=runtime.clone();
        live.source.smr_skel.as_mut().unwrap().tfs.get_mut(&2).unwrap().pos[0]=42.;
        live.source.smr_skel.as_mut().unwrap().worlds[0][0][3]=99.;
        live.implanted_bones=[2,1].into();live.removed_accessory_bones=[4,3].into();
        assert_eq!(baseline,live.skin_contract().unwrap(),"playback and set order are not source changes");
        let restored:AccessoryRuntime=bincode::deserialize(&bincode::serialize(&live).unwrap()).unwrap();
        assert_eq!(baseline,restored.skin_contract().unwrap());
        for field in 0..4 {
            let mut changed=runtime.clone();match field {0=>changed.parent_key="other".into(),
                1=>changed.local_placement[0][3]=0.2,2=>changed.copy_weights_body=true,
                _=>{changed.removed_accessory_bones.insert(5);}};
            assert_ne!(baseline,changed.skin_contract().unwrap());
        }
    }

    #[test]
    fn studio_mdk_keeps_native_slots_without_emission_keyword() {
        let mesh = ExtractedMesh {
            mat: Some(crate::unity::MeshMaterial {
                shader: "Shader Forge/main_StandardMDK_studio".into(),
                keywords: vec!["_NORMALMAP".into()],
                ..Default::default()
            }),
            shader_extra_textures: ["_BumpMap", "_ParallaxMap", "_MetallicGlossMap", "_EmissionMap"]
                .into_iter().map(|name| (name.into(), vec![1, 2, 3])).collect(),
            ..Default::default()
        };
        let part = SessionPart::from_mesh("part", &mesh, "masked", false);
        let restored = bincode::deserialize::<PartBlob>(
            &bincode::serialize(&PartBlob::from_part(&part)).unwrap()
        ).unwrap().into_part();
        assert_eq!(restored.shader_textures, mesh.shader_extra_textures);
    }

    #[test]
    fn forge_hair_another_ramp_survives_session_cache() {
        for (shader, hint) in [
            ("Shader Forge/main_hair", false),
            ("Shader Forge/main_hair_front", false),
            ("", true),
        ] {
            let mesh = ExtractedMesh {
                mat: Some(crate::unity::MeshMaterial { shader: shader.into(), ..Default::default() }),
                hair_shader_hint: hint,
                shader_extra_textures: [
                    ("_AnotherRamp".into(), vec![1, 2, 3]),
                    ("_EmissionMask".into(), vec![4, 5, 6]),
                ].into(),
                runtime_textures: [("_AnotherRamp".into(), [1, 1, 1, 0, 9])].into(),
                ..Default::default()
            };
            let part = SessionPart::from_mesh("hair_front", &mesh, "masked", true);
            let restored = bincode::deserialize::<PartBlob>(
                &bincode::serialize(&PartBlob::from_part(&part)).unwrap()
            ).unwrap().into_part();
            assert_eq!(restored.shader_textures.get("_AnotherRamp"), Some(&vec![1, 2, 3]));
            assert!(!restored.shader_textures.contains_key("_EmissionMask"));
            assert_eq!(restored.runtime_textures.get("_AnotherRamp"), Some(&[1, 1, 1, 0, 9]));
        }
    }

    #[test]
    fn forge_item_keeps_unbaked_inputs_across_session_cache() {
        for role in ["accessory","head","clothes_top"] {
            let mut mat=crate::unity::MeshMaterial {shader:"Shader Forge/main_item".into(),..Default::default()};
            mat.colors.insert("_ShadowColor".into(),[0.,0.,0.,1.]);
            mat.colors.insert("_Color2".into(),[0.2,0.6,0.9,1.]);
            let mesh=ExtractedMesh {mat:Some(mat),texture_png:Some(vec![1,2,3]),
                color_mask_png:Some(vec![4,5,6]),normal_map_png:Some(vec![7,8,9]),
                shader_extra_textures:[("_AnotherRamp".into(),vec![10,11,12])].into(),..Default::default()};
            let (blend,unlit)=crate::blend_for_mesh(role,&mesh);
            assert_eq!(blend,"masked");assert!(unlit);
            let part=SessionPart::from_mesh(role,&mesh,&blend,unlit);
            assert_eq!(part.shader_kind,8);
            let restored=bincode::deserialize::<PartBlob>(&bincode::serialize(&PartBlob::from_part(&part)).unwrap()).unwrap().into_part();
            for (name,bytes) in [("_MainTex",vec![1,2,3]),("_ColorMask",vec![4,5,6]),("_NormalMap",vec![7,8,9]),("_AnotherRamp",vec![10,11,12])] {
                assert_eq!(restored.shader_textures.get(name),Some(&bytes),"{role}/{name}");
            }
            assert_eq!(restored.source_material.as_ref().unwrap().color("_ShadowColor"),Some([0.,0.,0.,1.]));
        }
    }

    #[test]
    fn accessory_glasses_keep_unbaked_inputs_across_session_cache() {
        for shader in ["Koikano/main_clothes_item_glasses", "Shader Forge/toon_glasses_lod0"] {
            let native = crate::unity::solid_png([1.;4],1,1).unwrap();
            let baked = crate::unity::solid_png([0.,0.16,0.19,0.197],1,1).unwrap();
            let mesh = ExtractedMesh {
                mat:Some(crate::unity::MeshMaterial {shader:shader.into(),
                    colors:[("_Color4".into(),[0.,0.16,0.19,0.197])].into(),..Default::default()}),
                texture_png:Some(baked.clone()),
                shader_extra_textures:[("_MainTex".into(),native.clone()),("_GlassRamp".into(),vec![1,2,3])].into(),
                ..Default::default()};
            let part=SessionPart::from_mesh("accessory",&mesh,"translucent",true);
            let restored=bincode::deserialize::<PartBlob>(&bincode::serialize(&PartBlob::from_part(&part)).unwrap()).unwrap().into_part();
            assert_eq!(restored.shader_textures.get("_MainTex"),Some(&native));
            assert_eq!(restored.shader_textures.get("_GlassRamp"),Some(&vec![1,2,3]));
            assert_eq!(restored.png,Some(baked));
            assert_eq!(restored.source_material.unwrap().color("_Color4").unwrap()[3],0.197);
        }
    }

    #[test]
    fn forge_opaque_cache_preserves_authored_draw_inputs_for_all_roles() {
        for role in ["accessory","clothes_top","head"] {
            let mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {
                shader:"Shader Forge/main_opaque".into(),
                colors:[("_ShadowColor".into(),[0.,0.,0.,1.])].into(),..Default::default()}),
                texture_png:Some(vec![1,2]),detail_mask_png:Some(vec![3,4]),
                shader_extra_textures:[("_AnotherRamp".into(),vec![5,6]),("_Texture3".into(),vec![7,8])].into(),
                hair_shader_hint:true,..Default::default()};
            let p=SessionPart::from_mesh(role,&mesh,"masked",true);
            let restored=bincode::deserialize::<PartBlob>(&bincode::serialize(&PartBlob::from_part(&p)).unwrap()).unwrap().into_part();
            assert_eq!(restored.shader_kind,2);
            assert_eq!(restored.source_material.as_ref().unwrap().color("_ShadowColor"),Some([0.,0.,0.,1.]));
            for (key,png) in [("_MainTex",vec![1,2]),("_DetailMask",vec![3,4]),("_AnotherRamp",vec![5,6]),("_Texture3",vec![7,8])] {
                assert_eq!(restored.shader_textures.get(key),Some(&png));
            }
            let mut null=mesh;null.texture_png=None;
            let p=SessionPart::from_mesh(role,&null,"masked",true);
            let image=crate::teximg::load_from_memory(&p.shader_textures["_MainTex"]).unwrap().to_rgba8();
            assert_eq!(image.get_pixel(0,0).0,[255;4]);
        }
    }

    #[test]
    fn forge_alpha_cache_keeps_liquid_inputs_and_proven_opaque_alpha() {
        for (alpha,red,disable_red,expected) in [(1.,1.,false,1.),(0.3,1.,false,0.),(1.,0.,false,0.),(1.,0.,true,1.)] {
            let mut mat=crate::unity::MeshMaterial {shader:"Shader Forge/main_alpha".into(),..Default::default()};
            mat.floats.insert("_alpha_a".into(),if disable_red {0.}else{1.});
            let mesh=ExtractedMesh {mat:Some(mat),texture_png:crate::unity::solid_png([1.,1.,1.,alpha],2,2),
                alpha_mask_png:crate::unity::solid_png([red,1.,1.,1.],2,2),
                shader_extra_textures:[("_Texture3".into(),vec![7,8,9]),("_liquidmask".into(),vec![4,5,6])].into(),..Default::default()};
            let p=SessionPart::from_mesh("clothes_top",&mesh,"translucent",true);
            let restored=bincode::deserialize::<PartBlob>(&bincode::serialize(&PartBlob::from_part(&p)).unwrap()).unwrap().into_part();
            assert_eq!(restored.shader_kind,11);
            assert_eq!(restored.source_material.as_ref().unwrap().float("_OKNativeAlphaOpaque"),Some(expected));
            assert_eq!(restored.shader_textures.get("_Texture3"),Some(&vec![7,8,9]));
            assert_eq!(restored.shader_textures.get("_liquidmask"),Some(&vec![4,5,6]));
            assert_eq!(restored.shader_textures.get("_AlphaMask"),mesh.alpha_mask_png.as_ref());
        }
    }

    #[test]
    fn native_item_program_keeps_material_and_masks_across_cache() {
        for role in ["accessory","hair_front","clothes_top","head"] {
            for toggle in [None,Some(0.),Some(1.)] {
                let mut mat=crate::unity::MeshMaterial {shader:"xukmi/MainItemPlus".into(),render_queue:4000,
                    keywords:vec!["_ALPHAPREMULTIPLY_ON".into()],..Default::default()};
                mat.colors.insert("_Color2".into(),[0.2,0.8,1.0,0.7]);
                mat.colors.insert("_ShadowHSV".into(),[0.13,-0.2,0.1,0.]);
                mat.floats.insert("_Cutoff".into(),0.73);
                if let Some(toggle)=toggle {mat.floats.insert("_OutlineOn".into(),toggle);}
                let mesh=ExtractedMesh {mat:Some(mat),hair_shader_hint:true,
                    color_mask_png:Some(vec![1,2,3]),texture_png:Some(vec![4,5,6]),
                    shader_extra_textures:[("_EmissionMask".into(),vec![7,8,9])].into(),..Default::default()};
                let (blend,unlit)=crate::blend_for_mesh(role,&mesh);
                assert_eq!(blend,"masked");assert!(unlit);
                let part=SessionPart::from_mesh(role,&mesh,&blend,unlit);
                assert_eq!(part.shader_kind,9);assert_eq!(part.source_outline_enabled,toggle!=Some(0.));
                assert_eq!(part.png,mesh.texture_png,"the source albedo must not be tinted twice");
                let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
                let restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
                assert_eq!(restored.shader_textures.get("_ColorMask"),mesh.color_mask_png.as_ref());
                assert_eq!(restored.shader_textures.get("_EmissionMask"),Some(&vec![7,8,9]));
                assert_eq!(restored.source_material.as_ref().unwrap().color("_Color2"),Some([0.2,0.8,1.0,0.7]));
                assert_eq!(restored.source_material.as_ref().unwrap().float("_Cutoff"),Some(0.73));
            }
        }
    }

    #[test]
    fn source_outline_pass_flag_survives_cache() {
        for enabled in [false,true] {
            let mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {
                shader:"Koikano/main_clothes_opaque".into(),
                keywords:if enabled {vec!["_OUTLINE_ON".into()]}else{vec!["_ALPHA_A_ON".into()]},
                ..Default::default()}),..Default::default()};
            let part=SessionPart::from_mesh("clothes_top",&mesh,"masked",true);
            let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
            assert_eq!(bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part().source_outline_enabled,enabled);
        }
    }

    #[test]
    fn skin_outline_material_width_survives_cache() {
        let mut mat = crate::unity::MeshMaterial {shader:"Koikano/main_skin".into(),..Default::default()};
        for width in [0.0,0.37,1.0,1.8] {
            mat.floats.insert("_LineWidthS".into(),width);
            let mesh=ExtractedMesh {name:"arbitrary_mod_skin".into(),mat:Some(mat.clone()),..Default::default()};
            let part=SessionPart::from_mesh("head",&mesh,"opaque",true);
            assert_eq!(part.shader_kind,5);
            let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
            let restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
            assert_eq!(restored.skin_line_width,width);
        }
    }

    #[test]
    fn face_controller_metadata_survives_cache() {
        let mesh=ExtractedMesh {name:"renamed_mod_head".into(),..Default::default()};
        let mut part=SessionPart::from_mesh("head",&mesh,"opaque",true);
        part.eye_patterns=vec![(3,1),(-1,2),(7,7)];
        part.eyebrow_patterns=vec![(2,4),(4,-1)];
        part.face_baseline_weights=vec![0.,92.,61.,8.,0.,0.,0.,0.];
        part.face_default_rates=[0.92,0.61337];
        part.face_runtime_settings=[123.,0.27,0.,0.,0.2,0.3,0.4,0.5,0.13,0.24,0.35,1.];
        part.iris_runtime=Some(crate::eye_gaze::IrisRuntime {
            settings:[1.,-57.,93.,-77.,86.,-0.08,0.12,-0.06,0.09,0.0007,0.23,-0.18,1.8,-0.2],
            raw_uvs:vec![[-0.3,0.7],[1.4,1.2]],
        });
        let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
        let restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
        assert_eq!(restored.eye_patterns,part.eye_patterns);
        assert_eq!(restored.eyebrow_patterns,part.eyebrow_patterns);
        assert_eq!(restored.face_baseline_weights,part.face_baseline_weights);
        assert_eq!(restored.face_default_rates,part.face_default_rates);
        assert_eq!(restored.face_runtime_settings,part.face_runtime_settings);
        let source=part.iris_runtime.as_ref().unwrap();let cached=restored.iris_runtime.as_ref().unwrap();
        assert_eq!(cached.settings,source.settings);assert_eq!(cached.raw_uvs,source.raw_uvs);
        assert_eq!(cached.st(0.73,-0.67),source.st(0.73,-0.67));
    }

    #[test]
    fn eye_draw_material_cache_preserves_three_streams() {
        let mesh=ExtractedMesh {name:"mod_renamed_eye".into(),..Default::default()};
        let mut part=SessionPart::from_mesh("head",&mesh,"translucent",true);
        part.eye_draw=Some(crate::eye_gaze::EyeDraw {
            kind:2,textures:[Some(vec![9,8,7]),None],colors:[[0.2,0.3,0.4,0.7],[0.8,0.6,0.4,0.9]],
            offsets:[[0.12,-0.034],[-0.07,0.056]],rotations:[0.013,0.5,0.5],color:[1.;4],enabled:0.7,
            raw_uvs:[vec![[0.1,0.2]],vec![[0.7,0.9]]],texcoord_st:[[1.,1.,0.,0.];2],
            main_texcoord_st:[1.,1.,0.,0.],sample_uvs:vec![],
        });
        let mut source=part.eye_draw.as_ref().unwrap().clone();
        let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
        let restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
        let mut cached=restored.eye_draw.unwrap();
        let (mut a,mut b,mut c,mut d)=(vec![],vec![],vec![],vec![]);
        source.apply([1.7,0.8,-0.1,0.03],&[[0.3,0.8]],&mut a,&mut b);
        cached.apply([1.7,0.8,-0.1,0.03],&[[0.3,0.8]],&mut c,&mut d);
        assert_eq!(a,c);assert_eq!(b,d);assert_eq!(source.sample_uvs,cached.sample_uvs);
        assert_eq!(source.textures,cached.textures);assert_eq!(source.colors,cached.colors);assert_eq!(cached.kind,2);
    }

    #[test]
    fn runtime_texture_provenance_survives_both_caches() {
        let mut mesh = ExtractedMesh::default();
        mesh.runtime_textures.insert("_MainTex".into(), [1,1,1,1,1]);
        let decoded: ExtractedMesh = bincode::deserialize(&bincode::serialize(&mesh).unwrap()).unwrap();
        let part = SessionPart::from_mesh("arbitrary_mod_role", &decoded, "opaque", true);
        let restored = bincode::deserialize::<PartBlob>(&bincode::serialize(&PartBlob::from_part(&part)).unwrap()).unwrap().into_part();
        assert_eq!(restored.runtime_textures.get("_MainTex"), Some(&[1,1,1,1,1]));
        assert!(restored.runtime_textures.get("_NormalMap").is_none());
        assert!(SessionPart::from_mesh("body", &ExtractedMesh::default(), "opaque", true).runtime_textures.is_empty());
    }

    #[test]
    fn head_item_draw_cache_preserves_material_and_uvs() {
        let mut mat = crate::unity::MeshMaterial { shader: "Koikano/main_clothes_item".into(), ..Default::default() };
        mat.colors.insert("_Color".into(), [0.94, 0.75514644, 0.71794444, 1.0]);
        mat.colors.insert("_Color4".into(), [0.2, 0.4, 0.8, 0.3]);
        mat.floats.insert("_ColorMask_ST_x".into(), 0.37);
        mat.floats.insert("_ColorMask_ST_w".into(), -0.2);
        let mesh = ExtractedMesh { name: "mod_renamed_surface".into(), mat: Some(mat),
            texture_png: Some(vec![1,2,3]), color_mask_png: Some(vec![4,5,6]), ..Default::default() };
        let part = SessionPart::from_mesh("head", &mesh, "opaque", true);
        assert_eq!(part.shader_kind, 3);
        let bytes = bincode::serialize(&PartBlob::from_part(&part)).unwrap();
        let restored = bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
        let draw = restored.item_draw.unwrap();
        assert_eq!(draw.color_count,4);
        assert_eq!(draw.colors[0], [0.94, 0.75514644, 0.71794444, 1.0]);
        assert_eq!(draw.colors[1], [1.0;4]);
        assert_eq!(draw.colors[3], [0.2, 0.4, 0.8, 0.3]);
        assert_eq!(draw.color_mask_st, [0.37, 1.0, 0.0, -0.2]);
        assert_eq!(draw.color_mask, mesh.color_mask_png);
        assert_eq!(restored.png, mesh.texture_png);
        // Other roles already bake their create shader and must not tint twice.
        assert!(SessionPart::from_mesh("accessory", &mesh, "opaque", true).item_draw.is_none());
        let mut forge = mesh.clone();
        forge.mat.as_mut().unwrap().shader="Shader Forge/main_item".into();
        let part=SessionPart::from_mesh("head",&forge,"opaque",true);
        assert_eq!(part.shader_kind,8);
        let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
        let restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
        assert_eq!(restored.shader_textures.get("_ColorMask"),forge.color_mask_png.as_ref());
        assert_eq!(restored.source_material.as_ref().unwrap().float("_ColorMask_ST_x"),Some(0.37));
        assert_eq!(restored.source_material.as_ref().unwrap().float("_ColorMask_ST_w"),Some(-0.2));
        let draw=restored.item_draw.unwrap();
        assert_eq!(draw.color_count,3);
        assert_eq!(draw.colors[3],[0.2,0.4,0.8,0.3]);
        assert_eq!(draw.color_mask_st,[0.37,1.0,0.0,-0.2]);
    }

    #[test]
    fn hair_fragment_receives_full_source_mask_and_colors_after_cache() {
        let mut mat = crate::unity::MeshMaterial {
            shader: "Shader Forge/main_hair".into(), ..Default::default()
        };
        mat.colors.insert("_Color".into(), [0.99, 0.92, 0.66, 1.0]);
        let mesh = ExtractedMesh {
            mat: Some(mat), texture_png: Some(vec![1, 2, 3]),
            color_mask_png: Some(vec![4, 5, 6, 7]),
            runtime_textures: [("_ColorMask".into(), [1, 0, 0, 0, 11])].into(),
            ..Default::default()
        };
        for role in ["hair_b", "accessory"] {
            let part = SessionPart::from_mesh(role, &mesh, "opaque", true);
            assert_eq!(part.shader_kind, 1);
            let restored = bincode::deserialize::<PartBlob>(
                &bincode::serialize(&PartBlob::from_part(&part)).unwrap()
            ).unwrap().into_part();
            let draw = restored.item_draw.unwrap();
            assert_eq!(draw.color_count, 3);
            assert_eq!(draw.color_mask, mesh.color_mask_png);
            assert_eq!(draw.colors[0], [0.99, 0.92, 0.66, 1.0]);
            assert_eq!(draw.colors[1], [0.7843137, 0.7843137, 0.7843137, 1.0]);
            assert_eq!(draw.colors[2], [0.5, 0.5, 0.5, 1.0]);
            assert_eq!(restored.runtime_textures["_ColorMask"], [1, 0, 0, 0, 11]);
            assert_eq!(restored.png, mesh.texture_png);
            assert_eq!(restored.shadow_extend, 0.5);
            assert_eq!(restored.spec_height, 0.85);
        }
        for shader in ["Koikano/hair_main_sun", "Koikano/hair_main_sun_front"] {
            let mut sun = mesh.clone();
            sun.mat.as_mut().unwrap().shader = shader.into();
            sun.mat.as_mut().unwrap().keywords = vec!["_REVERSECOLOR01_ON".into()];
            sun.mat.as_mut().unwrap().floats.insert("_Color2onoff".into(),0.0);
            let part=SessionPart::from_mesh("accessory", &sun, "masked", true);
            assert_eq!(part.shader_kind,10);
            let restored=bincode::deserialize::<PartBlob>(&bincode::serialize(&PartBlob::from_part(&part)).unwrap()).unwrap().into_part();
            assert_eq!(restored.item_draw.as_ref().unwrap().color_mask,sun.color_mask_png);
            assert_eq!(restored.png,sun.texture_png);
            let source=restored.source_material.unwrap();
            assert_eq!(source.keywords,vec!["_REVERSECOLOR01_ON"]);
            assert_eq!(source.float("_Color2onoff"),Some(0.0));
        }
        let mut front = mesh.clone();
        front.mat.as_mut().unwrap().shader = "Shader Forge/main_hair_front".into();
        assert_eq!(SessionPart::from_mesh("hair_f", &front, "alpha", true).spec_height, 0.985);
    }

    #[test]
    fn source_visibility_cache_retains_inactive_mouth_targets() {
        let mesh = ExtractedMesh { name: "custom_canine_mesh".into(),
            positions: vec![[1.0, 2.0, 3.0]], ..Default::default() };
        let mut part = SessionPart::from_mesh("head", &mesh, "opaque", true);
        part.mouth_patterns = vec![(0, 1)];
        part.blend_shapes = vec![(std::ffi::CString::new("mod.closed").unwrap(), vec![(0, [0.0; 3])]),
            (std::ffi::CString::new("mod.open").unwrap(), vec![(0, [0.0, 0.1, 0.0])])];
        for visible in [false, true] {
            part.render_visible = visible;
            let bytes = bincode::serialize(&PartBlob::from_part(&part)).unwrap();
            let restored = bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
            assert_eq!(restored.render_visible, visible);
            assert_eq!(restored.positions, part.positions);
            assert_eq!(restored.mouth_patterns, part.mouth_patterns);
            assert_eq!(restored.blend_shapes, part.blend_shapes);
        }
    }

    #[test]
    fn skin_normal_detail_survives_part_cache() {
        let mesh = ExtractedMesh::default();
        let mut part = SessionPart::from_mesh("body", &mesh, "opaque", true);
        part.normal_detail = Some(vec![11, 22, 33, 44]);
        part.normal_detail_st = [0.8, 0.9, 0.1, 0.2];
        let restored = PartBlob::from_part(&part).into_part();
        assert_eq!(restored.normal_detail, part.normal_detail);
        assert_eq!(restored.normal_detail_st, part.normal_detail_st);
    }

    #[test]
    fn normal_texture_uses_forge_camera_light_or_authored_tangents() {
        let mut mesh = ExtractedMesh::default();
        mesh.normal_map_png = Some(vec![1, 2, 3]);
        mesh.source_tangents = Some(false);
        assert!(SessionPart::from_mesh("clothes_top", &mesh, "opaque", true).normal_map.is_none());
        mesh.mat = Some(crate::unity::MeshMaterial {
            shader: "Shader Forge/main_opaque".into(),
            ..Default::default()
        });
        assert_eq!(SessionPart::from_mesh("clothes_top", &mesh, "opaque", true).normal_map, Some(vec![1, 2, 3]));
        mesh.mat = None;
        mesh.source_tangents = Some(true);
        assert!(SessionPart::from_mesh("clothes_top", &mesh, "opaque", true).normal_map.is_some());
    }

    #[test]
    fn hair_shader_line_color_default_is_used_only_when_unserialized() {
        let mut mesh = ExtractedMesh {
            mat: Some(crate::unity::MeshMaterial {
                shader: "Shader Forge/main_hair".into(),
                ..Default::default()
            }),
            ..Default::default()
        };
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "masked", true).line_color,
            [0.5, 0.5, 0.5, 1.0]);
        mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_hair_front".into();
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "masked", true).line_color,
            [0.5, 0.5, 0.5, 1.0]);
        mesh.mat.as_mut().unwrap().colors.insert("_LineColor".into(), [0.6, 0.4, 0.3, 1.0]);
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "masked", true).line_color,
            [0.6, 0.4, 0.3, 1.0]);
        mesh.mat.as_mut().unwrap().colors.clear();
        mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_item_studio_alpha".into();
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "translucent", true).line_color,
            [0.0, 0.0, 0.0, 1.0]);
    }

    #[test]
    fn authored_hair_alpha_mask_and_uv_transform_reach_session() {
        let mut material = crate::unity::MeshMaterial {
            shader: "Shader Forge/main_hair".into(),
            ..Default::default()
        };
        material.floats.insert("_AlphaMask_ST_x".into(), 0.75);
        material.floats.insert("_AlphaMask_ST_w".into(), -0.125);
        let mesh = ExtractedMesh {
            mat: Some(material),
            alpha_mask_png: Some(vec![137, 80, 78, 71]),
            ..Default::default()
        };
        let part = SessionPart::from_mesh("accessory", &mesh, "masked", true);
        assert_eq!(part.alpha_mask.as_deref(), Some(&[137, 80, 78, 71][..]));
        assert_eq!(part.alpha_mask_st, [0.75, 1.0, 0.0, -0.125]);
    }

    #[test]
    fn shader_family_comes_from_material_even_for_accessories() {
        let mut mesh = ExtractedMesh::default();
        mesh.name = "44_mesh".into();
        mesh.mat = Some(crate::unity::MeshMaterial {
            shader: "Shader Forge/main_hair".into(),
            ..Default::default()
        });
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "masked", true).shader_kind, 1);
        mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_opaque".into();
        assert_eq!(SessionPart::from_mesh("clothes_bot", &mesh, "masked", true).shader_kind, 2);
        mesh.mat.as_mut().unwrap().shader.clear();
        mesh.mat.as_mut().unwrap().keywords = vec!["_COLORMASKUSE_COLOROFF".into()];
        assert_eq!(SessionPart::from_mesh("clothes_bot", &mesh, "opaque", true).shader_kind, 3);
        mesh.mat.as_mut().unwrap().tex_slots.push(("_Texture3".into(), "paint".into()));
        assert_eq!(SessionPart::from_mesh("clothes_top", &mesh, "masked", true).shader_kind, 2);
        mesh.mat.as_mut().unwrap().tex_slots.clear();
        mesh.mat.as_mut().unwrap().name = "mf_m_primmaterial".into();
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "translucent", true).shader_kind, 4);
        mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_item".into();
        assert_eq!(SessionPart::from_mesh("accessory", &mesh, "masked", true).shader_kind, 8);
        mesh.mat.as_mut().unwrap().shader = "Koikano/main_skin".into();
        assert_eq!(SessionPart::from_mesh("body", &mesh, "opaque", true).shader_kind, 5);
        mesh.mat.as_mut().unwrap().shader = "Shader Forge/main_skin".into();
        assert_eq!(SessionPart::from_mesh("head", &mesh, "opaque", true).shader_kind, 7);
        mesh.mat.as_mut().unwrap().shader.clear();
        mesh.name = "cf_O_face".into();
        mesh.mat.as_mut().unwrap().name = "cf_m_face_00".into();
        mesh.mat.as_mut().unwrap().floats.insert("_rimV".into(), 0.4);
        mesh.mat.as_mut().unwrap().floats.insert("_rimpower".into(), 0.1);
        assert_eq!(SessionPart::from_mesh("head", &mesh, "opaque", true).shader_kind, 7);
        mesh.mat.as_mut().unwrap().floats.insert("_FaceNormalG".into(), 0.4);
        assert_eq!(SessionPart::from_mesh("head", &mesh, "opaque", true).shader_kind, 5);
        mesh.mat.as_mut().unwrap().floats.remove("_FaceNormalG");
        assert_eq!(SessionPart::from_mesh("body", &mesh, "opaque", true).shader_kind, 5);
        mesh.mat.as_mut().unwrap().floats.clear();
        assert_eq!(SessionPart::from_mesh("body", &mesh, "opaque", true).shader_kind, 5);
        assert_eq!(SessionPart::from_mesh("head", &mesh, "opaque", true).shader_kind, 5);
        mesh.name = "cf_O_eyeline".into();
        assert_eq!(SessionPart::from_mesh("head", &mesh, "opaque", true).shader_kind, 0);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn accessory_prefab_renderer_order_and_skeleton_are_stable() {
        let Ok(index) = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mods\.ok_guid_index.txt")) else {
            return;
        };
        let Some(path) = index.lines().find_map(|line| line.strip_prefix("aa001\t")) else {
            return;
        };
        let bytes = crate::mods::zipmod_one_unity3d(std::path::Path::new(path), "chara/aa1_acc_hair_f.unity3d")
            .expect("hair bundle").1;
        let mut first_move = None;
        let mut first_order = None;
        let mut first_skel = None;
        for _ in 0..4 {
            crate::unity::clear_bundle_cache();
            let parsed = crate::unity::parse_bundle_cached(bytes.clone());
            let (meshes, skel) = parsed.meshes_for_maindata_including_disabled("f_08L");
            let mesh = meshes.iter().find(|m| m.name == "AS01_O_kami_05F_0.001").expect("hair mesh");
            let order: Vec<_> = meshes.iter().map(|m| (
                m.path_id,
                m.renderer_ids.clone(),
                m.renderer_names.clone(),
                m.mat.as_ref().map(|mat| (mat.name.clone(), mat.shader.clone(), mat.color("_LineColor"))),
            )).collect();
            let skel_id = skel.as_ref().map(|s| s.source_mesh).expect("skinned renderer");
            if let Some(prior) = first_move {
                assert_eq!(prior, mesh.move_nodes, "N_move transform changes across parse");
            } else {
                first_move = Some(mesh.move_nodes);
            }
            if let Some(prior) = &first_order {
                assert_eq!(prior, &order, "prefab mesh order changes across parse");
            } else {
                first_order = Some(order);
            }
            if let Some(prior) = first_skel {
                assert_eq!(prior, skel_id, "prefab skeleton changes across parse");
            } else {
                first_skel = Some(skel_id);
            }
        }
    }


    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn session_open_body_head_and_height_moves() {
        let game = crate::test_support::game_path(r"");
        if !game.join("abdata").ok_is_dir() {
            return;
        }
        let card =
            crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if !card.ok_is_file() {
            return;
        }
        let mut s = open(&game, Some(&card), 0).expect("session open");
        assert!(
            s.parts.iter().any(|p| p.role == "body"),
            "missing body part roles={:?}",
            s.parts.iter().map(|p| p.role.as_str()).collect::<Vec<_>>()
        );
        assert!(s.parts.iter().any(|p| p.role == "head"));
        let body = s.parts.iter().find(|p| p.role == "body").unwrap();
        assert!(
            body.positions.len() > 6000,
            "body verts {}",
            body.positions.len()
        );
        let y0 = body
            .positions
            .iter()
            .fold(f32::MIN, |m, p| m.max(p[1]));
        let mut body_shape = s.shape_body.clone();
        if body_shape.is_empty() {
            body_shape.resize(32, 0.5);
        }
        body_shape[0] = 1.0;
        let face = s.shape_face.clone();
        s.apply_shape(&body_shape, &face);
        s.skin_all();
        let body = s.parts.iter().find(|p| p.role == "body").unwrap();
        let y1 = body
            .positions
            .iter()
            .fold(f32::MIN, |m, p| m.max(p[1]));
        eprintln!("session height maxY {y0:.3} -> {y1:.3}");
        assert!(
            (y1 - y0).abs() > 0.01,
            "height slider did not move mesh y {y0} vs {y1}"
        );
    }

    #[test]
    #[ignore = "manual real-card check of scene-dependent Animator post-corrections"]
    fn game_scene_mode_rewrites_arm_and_hand_after_animation() {
        let game = crate::test_support::game_path(r"");
        let card = game.join(r"UserData\chara\female\[Card] March_7th_keiyuu.png");
        if !card.ok_is_file() { return; }
        let mut s = open(&game, Some(&card), 0).expect("session open");
        let body = s.shape_body.clone();
        let face = s.shape_face.clone();
        s.apply_shape(&body, &face);
        let scale = |s: &OkSession, name: &str| {
            s.body_skel.as_ref().unwrap().tfs.values()
                .find(|tf| tf.name == name).unwrap().scale
        };
        s.anim_clip = "GSit_00".into();
        s.anim_scene_mode = 0;
        s.apply_anim_time(0.25);
        let maker_arm = scale(&s, "cf_j_forearm01_L");
        let maker_hand = scale(&s, "cf_j_hand_L");
        s.anim_scene_mode = 3;
        s.apply_anim_time(0.25);
        let h_arm = scale(&s, "cf_j_forearm01_L");
        let h_hand = scale(&s, "cf_j_hand_L");
        assert!((maker_arm[0] / h_arm[0] - 0.92).abs() < 0.005,
            "maker_arm={maker_arm:?} h_arm={h_arm:?}");
        for axis in 1..3 {
            assert!((maker_hand[axis] / h_hand[axis] - 0.9).abs() < 0.005,
                "maker_hand={maker_hand:?} h_hand={h_hand:?}");
        }
        s.anim_clip = "f_aruki_00_00_01".into();
        s.anim_scene_mode = 0;
        s.apply_anim_time(0.25);
        let normal_arm = scale(&s, "cf_j_forearm01_L");
        let normal_hand = scale(&s, "cf_j_hand_L");
        assert!((normal_arm[0] / h_arm[0] - 1.0).abs() < 0.005,
            "normal_arm={normal_arm:?} h_arm={h_arm:?}");
        for axis in 1..3 {
            assert!((normal_hand[axis] / h_hand[axis] - 0.9).abs() < 0.005,
                "normal_hand={normal_hand:?} h_hand={h_hand:?}");
        }
    }

    #[test]
    #[ignore = "manual K091 necklace attachment audit against card and prefab"]
    fn k091_necklace_attachment_audit() {
        let game = crate::test_support::game_path(r"");
        let card = game.join(r"UserData\chara\female\KK_091207.png");
        if !card.ok_is_file() { return; }
        let info = crate::card::parse_card_outfit(&card, 0).expect("card parse");
        eprintln!("NECK CARD slot19={:?}", info.accessory.get(19).map(|p| (p.type_id, p.id, p.parent_key.as_str(), p.no_shake)));
        let mut s = open(&game, Some(&card), 0).expect("session open");
        let dynamic_neck_parts: Vec<&str> = s.parts.iter()
            .filter(|p| p.name.contains("necklace") || p.name.contains("neck_star"))
            .filter(|p| p.runtime_accessory.is_some())
            .map(|p| p.name.as_str()).collect();
        assert_eq!(dynamic_neck_parts.len(), 2, "both necklace renderers need runtime skinning");
        eprintln!("NECK RUNTIME {dynamic_neck_parts:?}");
        let baked_neutral: Vec<_> = s.parts.iter()
            .filter(|p| p.runtime_accessory.is_some())
            .map(|p| (p.name.clone(), p.positions.clone(),
                p.runtime_accessory.as_ref().unwrap().parent_key.clone(),
                crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(),
                    &p.runtime_accessory.as_ref().unwrap().parent_key).unwrap())).collect();
        s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
        for (name, baked, parent_key, anchor_before) in &baked_neutral {
            let current = &s.parts.iter().find(|p| &p.name == name).unwrap().positions;
            assert_eq!(baked.len(), current.len(), "{name}");
            let anchor_after = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), parent_key).unwrap();
            let anchor_delta = crate::pose::mat_mul_pub(&anchor_after,
                &crate::pose::mat_inv_pub(anchor_before));
            let max_delta = baked.iter().zip(current).map(|(a, b)| {
                ((a[0]-b[0]).powi(2) + (a[1]-b[1]).powi(2)
                    + (a[2]-b[2]).powi(2)).sqrt()
            }).fold(0.0f32, f32::max);
            let max_local = baked.iter().zip(current).map(|(a, b)| {
                let pa = [anchor_delta[0][0]*a[0]+anchor_delta[0][1]*a[1]+anchor_delta[0][2]*a[2]+anchor_delta[0][3],
                    anchor_delta[1][0]*a[0]+anchor_delta[1][1]*a[1]+anchor_delta[1][2]*a[2]+anchor_delta[1][3],
                    anchor_delta[2][0]*a[0]+anchor_delta[2][1]*a[1]+anchor_delta[2][2]*a[2]+anchor_delta[2][3]];
                ((pa[0]-b[0]).powi(2)+(pa[1]-b[1]).powi(2)+(pa[2]-b[2]).powi(2)).sqrt()
            }).fold(0.0f32, f32::max);
            eprintln!("NECK NEUTRAL {name} max_bake_delta_m={max_delta:.6} max_after_anchor_m={max_local:.6}");
            assert!(max_local < 0.01, "{name}: neutral local drift {max_local} m");
        }
        let neutral = s.parts.iter().find(|p| p.name == "on_star_necklace_base").unwrap()
            .positions.clone();
        let anchor0 = s.body_skel.as_ref().unwrap().world_tf("a_n_neck").unwrap();
        for t in [0.0f32, 0.25, 0.75] {
            if t > 0.0 {
                s.anim_clip = "f_aruki_00_00_01".into();
                let mut step = if t == 0.25 { 0.0 } else { 0.25 };
                while step < t {
                    step = (step + 1.0 / 60.0).min(t);
                    s.apply_anim_time(step);
                }
            }
            if let Some(sk) = &s.body_skel {
                for name in ["a_n_neck", "cf_j_head", "cf_j_neck", "cf_s_head", "cf_j_spine03"] {
                    let tf = sk.tfs.iter().find(|(_, tf)| tf.name == name);
                    eprintln!("NECK BONE t={t} {name} local={:?} parent={:?} world={:?}",
                        tf.map(|(_, tf)| (tf.pos, tf.rot, tf.scale)),
                        tf.and_then(|(_, tf)| tf.parent).and_then(|id| sk.tfs.get(&id)).map(|tf| tf.name.as_str()),
                        sk.world_tf(name).map(|m| crate::pose::mat_pos(&m)));
                }
            }
        }
        let moved = s.parts.iter().find(|p| p.name == "on_star_necklace_base").unwrap();
        let anchor1 = s.body_skel.as_ref().unwrap().world_tf("a_n_neck").unwrap();
        let delta = crate::pose::mat_mul_pub(&anchor1, &crate::pose::mat_inv_pub(&anchor0));
        let max_nonrigid = neutral.iter().zip(&moved.positions).map(|(before, after)| {
            let rigid = [
                delta[0][0]*before[0] + delta[0][1]*before[1] + delta[0][2]*before[2] + delta[0][3],
                delta[1][0]*before[0] + delta[1][1]*before[1] + delta[1][2]*before[2] + delta[1][3],
                delta[2][0]*before[0] + delta[2][1]*before[1] + delta[2][2]*before[2] + delta[2][3],
            ];
            ((rigid[0]-after[0]).powi(2) + (rigid[1]-after[1]).powi(2)
                + (rigid[2]-after[2]).powi(2)).sqrt()
        }).fold(0.0f32, f32::max);
        eprintln!("NECK RUNTIME max_nonrigid_m={max_nonrigid:.6}");
        assert!(max_nonrigid > 0.0001, "animated necklace must not collapse to rigid follow");
        for part in &s.parts {
            let name = part.name.to_ascii_lowercase();
            if !name.contains("neck") && !name.contains("star") { continue; }
            let mut weighted = std::collections::HashMap::<u16, usize>::new();
            for (ids, weights) in part.bone_idx.iter().zip(&part.weights) {
                for i in 0..4 {
                    if weights[i] > 0.001 { *weighted.entry(ids[i]).or_default() += 1; }
                }
            }
            eprintln!("NECK PART {} role={} kind={} parent={:?} follow={:?} verts={} weighted={:?} bindposes={}",
                part.name, part.role, part.skel_kind, part.parent_bone, part.rigid_follow_bone,
                part.positions.len(), weighted, part.bind_poses.len());
        }
    }

    #[test]
    #[ignore = "manual real-card frame clock regression; owns process environment"]
    fn runtime_hair_frame_clock_is_independent_of_clip_time() {
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM"] {
            std::env::set_var(key, "1");
        }
        let game = crate::test_support::game_path(r"");
        let card = game.join(r"UserData\chara\female\KK_384283.png");
        assert!(card.ok_is_file(), "real-card clock regression requires K384");
        let mut source = open(&game, Some(&card), 0).expect("session open");
        source.apply_shape(&source.shape_body.clone(), &source.shape_face.clone());
        let bytes = source.to_cache_bytes().unwrap();
        let mut sessions: Vec<_> = (0..5).map(|_| {
            let mut s = OkSession::from_cache_bytes(&bytes).unwrap();
            s.anim_time = 0.0;
            s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
            s
        }).collect();
        let head_id = source.body_shaped.as_ref().unwrap().tfs.iter()
            .find(|(_, tf)| tf.name == "cf_j_head").map(|(id, _)| *id)
            .expect("animated head joint");
        let head_x = source.body_shaped.as_ref().unwrap().tfs[&head_id].pos[0];
        let dt = 1.0 / 30.0;
        let mut max_clock_error = 0.0f32;
        let mut max_frozen_difference = 0.0f32;
        for frame in 1..=24 {
            // Apply identical external socket motion while Animator is held.
            // A 120 Hz clip loops four times per 30 Hz physical frame.
            for s in &mut sessions {
                let sk = s.body_shaped.as_mut().unwrap();
                sk.tfs.get_mut(&head_id).unwrap().pos[0] = head_x
                    + 0.04 * (frame as f32 * 0.3).sin();
                sk.rebuild_worlds();
            }
            // Reference uses the physical clock directly after sampling.
            sessions[0].apply_anim_time(0.0);
            sessions[0].update_dynamic_accessories(dt);
            // Repeated phase, multiple-loop aliasing, and backward seeking.
            for (i, sample) in [(1, 0.0), (2, (frame as f32 * dt) % (1.0 / 120.0)),
                (3, if frame % 2 == 0 { 0.1 } else { 0.4 })] {
                assert_eq!(unsafe { crate::ffi::apply_anim_frame(&mut sessions[i], sample, dt) }, 0);
            }
            sessions[4].apply_anim_time(0.0); // Deliberately frozen negative control.
            let mut dynamic_parts = 0;
            for (index, expected) in sessions[0].parts.iter().enumerate() {
                if expected.runtime_accessory.is_none() { continue; }
                dynamic_parts += 1;
                for i in 1..=4 {
                    let actual = &sessions[i].parts[index].positions;
                    assert_eq!(actual.len(), expected.positions.len());
                    let error = actual.iter().zip(&expected.positions).flat_map(|(a, b)|
                        (0..3).map(move |axis| (a[axis] - b[axis]).abs()))
                        .fold(0.0f32, f32::max);
                    if i == 4 { max_frozen_difference = max_frozen_difference.max(error); }
                    else { max_clock_error = max_clock_error.max(error); }
                }
            }
            assert!(dynamic_parts >= 9, "test must exercise K384 runtime hair");
        }
        eprintln!("frame clock: sample-dependent error={max_clock_error:.7} m, frozen control={max_frozen_difference:.7} m");
        assert!(max_frozen_difference > 0.001, "socket motion must excite hair physics");
        assert!(max_clock_error < 1e-6, "clip time changed the physical frame clock");
    }

    #[test]
    #[ignore = "manual K091 bent-elbow CPU/GPU palette diagnostic; owns environment"]
    fn k091_controller_elbow_skin_diagnostic() {
        std::env::set_var("OK_GPU_SKIN", "1");
        std::env::set_var("OK_NOPOSE", "1");
        std::env::set_var("OK_SKELMESH", "1");
        std::env::remove_var("OK_UE_ANIM");
        let game = crate::test_support::game_path("");
        let card = game.join("UserData/chara/female/KK_091207.png");
        let mut s = open(&game, Some(&card), 0).expect("session open");
        s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
        let _ = s.ensure_skel_export();
        s.anim_clip = "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle".into();
        s.apply_anim_frame(0.5, 0.0);
        std::env::remove_var("OK_SKELMESH");
        s.skin_all();
        let part_index = s.parts.iter().position(|p| p.role == "body").unwrap();
        let skel = s.body_skel.as_ref().unwrap();
        let mut bones = Vec::new();
        for (id, tf) in &skel.tfs {
            if tf.name.contains("forearm") || tf.name.contains("elbo") || tf.name.contains("arm00") {
                bones.push(serde_json::json!({ "id": id, "tf": tf,
                    "parent_name": tf.parent.and_then(|p| skel.tfs.get(&p)).map(|p| &p.name),
                    "world": crate::pose::world_of_id(*id, &skel.tfs) }));
            }
        }
        let ex = s.skel_export.as_ref().unwrap();
        let worlds = |posed: bool| {
            let tfs: HashMap<i64, crate::pose::Tf> = ex.bones.iter().enumerate().map(|(i, b)| {
                let (pos, rot, scale) = if posed { (b.pose_pos, b.pose_rot, b.pose_scl) }
                    else { (b.rest_pos, b.rest_rot, b.rest_scl) };
                (i as i64 + 1, crate::pose::Tf { sibling_index: None, name: b.name.clone(),
                    parent: (b.parent >= 0).then_some(b.parent as i64 + 1), pos, rot, scale })
            }).collect();
            (0..ex.bones.len()).map(|i| crate::pose::world_of_id(i as i64 + 1, &tfs)).collect::<Vec<_>>()
        };
        let body = &s.parts[part_index];
        let gpu = crate::skel_export::deform_from_ref(&ex.part_rest[part_index], &body.weights,
            &ex.part_bone_idx[part_index], &worlds(true), &worlds(false));
        let error = body.positions.iter().zip(&gpu).map(|(a, b)|
            ((a[0]-b[0]).powi(2)+(a[1]-b[1]).powi(2)+(a[2]-b[2]).powi(2)).sqrt())
            .fold(0.0f32, f32::max);
        eprintln!("K091 bent elbow CPU/GPU max difference {error:.7} m");
        let all_bones: Vec<_> = skel.tfs.iter().map(|(id, tf)| serde_json::json!({
            "id": id, "tf": tf, "parent_name": tf.parent.and_then(|p| skel.tfs.get(&p)).map(|p| &p.name)
        })).collect();
        let value = serde_json::json!({ "bones": bones, "all_bones": all_bones, "cpu": body.positions,
            "gpu": gpu, "triangles": body.indices, "max_cpu_gpu": error });
        std::fs::write("../../Saved/Logs/K091ControllerElbowScene.json", serde_json::to_vec(&value).unwrap()).unwrap();
        // Shared-palette reconstruction differs slightly from each SMR's
        // authored bind matrices. Keep a sub-millimetre spatial gate.
        assert!(error < 2e-4, "GPU reference palette differs from CPU skin: {error} m");
        let mut variants = serde_json::Map::new();
        for preset in ["none", "studio_default", "studio_full"] {
            let mut sk = s.body_shaped.clone().unwrap();
            crate::anim::apply_clip_at(&game, &mut sk, &s.anim_clip, 0.5);
            crate::shape::apply_body_shape_always(&mut sk, s.body_shaped.as_ref().unwrap());
            crate::pose::apply_scene_limb_size_corrections(&mut sk, false, true);
            if preset == "studio_default" {
                crate::pose::apply_expression_bone_bust(&mut sk);
                crate::pose::apply_expression_bone_acs_null(&mut sk);
            }
            if preset == "studio_full" { crate::pose::apply_expression_bone(&mut sk); }
            let names: Vec<String> = s.abm.iter().map(|b| b.name.clone()).collect();
            let base = crate::pose::capture_baseline(&sk, &names);
            crate::pose::apply_abm(&mut sk, &s.abm, 0, &base);
            s.body_skel = Some(sk);
            s.skin_all();
            let tfs = &s.body_skel.as_ref().unwrap().tfs;
            variants.insert(preset.to_string(), serde_json::json!({ "positions": s.parts[part_index].positions,
                "bones": tfs.values().collect::<Vec<_>>() }));
        }
        std::fs::write("../../Saved/Logs/K091ControllerElbowSceneVariants.json", serde_json::to_vec(&variants).unwrap()).unwrap();
    }

    #[test]
    #[ignore = "real KKS mod cards, body physics and cache; owns process environment"]
    fn runtime_body_ver02_mod_cards_and_cache() {
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE"] { std::env::set_var(key, "1"); }
        for key in ["OK_UE_ANIM", "OK_NODB", "OK_POSE"] { std::env::remove_var(key); }
        let game = crate::test_support::game_path("");
        for card_name in ["KK_091207.png", "KK_384283.png"] {
            let card = game.join("UserData/chara/female").join(card_name);
            let mut s = open(&game, Some(&card), 0).unwrap();
            assert_eq!(s.body_dynamics.len(), 4);
            if card_name == "KK_091207.png" {
                assert!((s.bust_softness - 0.85).abs() < 1e-6);
                assert_eq!(s.bust_weight, 0.0);
            }
            s.anim_clip = "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle".into();
            s.anim_time = 0.5;
            s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
            let _ = s.ensure_skel_export();
            for frame in 0..30 { s.apply_anim_frame(frame as f32 / 60.0, 1.0 / 60.0); }
            assert!(s.body_dynamic_runtime.is_some());
            let sk = s.body_skel.as_ref().unwrap();
            let ex = s.skel_export.as_ref().unwrap();
            for name in ["cf_j_siri_L", "cf_j_siri_R", "cf_j_bust02_L", "cf_j_bust02_R"] {
                let tf = sk.tfs.values().find(|tf| tf.name == name).unwrap();
                assert!(tf.pos.iter().chain(tf.rot.iter()).chain(tf.scale.iter()).all(|v| v.is_finite()));
                let exported = ex.bones.iter().find(|b| b.name == name).unwrap();
                for i in 0..3 { assert!((exported.pose_pos[i] - tf.pos[i]).abs() < 1e-6,
                    "GPU palette refreshed before body physics: {name}"); }
            }
            let blob = s.to_cache_bytes().unwrap();
            let mut restored = OkSession::from_cache_bytes(&blob).unwrap();
            assert_eq!(restored.body_dynamics.len(), 4);
            assert_eq!(restored.bust_softness, s.bust_softness);
            assert_eq!(restored.bust_weight, s.bust_weight);
            assert_eq!(restored.collider_overrides, s.collider_overrides);
            assert!(restored.body_dynamic_runtime.is_none(), "particle history is not a disk cache pose");
            restored.anim_clip = s.anim_clip.clone();
            restored.apply_shape(&restored.shape_body.clone(), &restored.shape_face.clone());
            restored.apply_anim_frame(0.5, 1.0 / 60.0);
            for mode in [4, 2] {
                restored.anim_scene_mode = mode;
                restored.apply_anim_frame(0.5, 1.0 / 60.0);
                assert_eq!(restored.body_dynamic_runtime.as_ref().unwrap().collider_flags(),
                    Some(restored.collider_settings.resolve(mode == 4, restored.collider_overrides)),
                    "scene changes must update collider policy after cache reload");
            }
            eprintln!("Ver02 mod card {card_name}: 4 components, softness={}, weight={}, live GPU palette and cache pass", s.bust_softness, s.bust_weight);
        }
    }

    #[test]
    #[ignore = "real native hair, mod cards and shared cloth physics; owns process environment"]
    fn ordinary_runtime_cards_and_cache() {
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE"] { std::env::set_var(key, "1"); }
        for key in ["OK_UE_ANIM", "OK_NODB", "OK_POSE"] { std::env::remove_var(key); }
        let game = crate::test_support::game_path("");
        let mut total_cloth = 0; let mut total_hair = 0;
        for name in ["[Card] March_7th_keiyuu.png", "Alice_keiyuu.png", "Hayase_yuuka_keiyuu_V1_02 For KKS.png", "KK_091207.png", "KK_384283.png"] {
            let mut s = open(&game, Some(&game.join("UserData/chara/female").join(name)), 0).unwrap();
            s.anim_scene_mode = 2;
            s.anim_clip = "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle".into();
            s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
            let _ = s.ensure_skel_export();
            for frame in 0..30 { s.apply_anim_frame(frame as f32 / 60.0, 1.0 / 60.0); }
            let hair = s.parts.iter().filter(|p| p.role.contains("hair") && !p.role.contains("acc") && p.runtime_accessory.is_some()).count();
            let mixed = s.parts.iter().filter(|p| p.role.starts_with("clothes")
                && p.runtime_accessory.as_ref().is_some_and(|r| r.copy_weights_body)).count();
            let cloth = s.clothes_dynamics.len(); total_hair += hair; total_cloth += cloth;
            for p in s.parts.iter().filter(|p| p.runtime_accessory.is_some()) {
                assert_eq!(p.skel_kind, 4); assert!(p.rigid_follow_bone.is_none());
                assert!(p.positions.iter().flatten().all(|x| x.is_finite()));
                let runtime = p.runtime_accessory.as_ref().unwrap();
                assert!(runtime.source.smr_skel.is_some(), "keep each prefab's private rig");
            }
            if cloth > 0 {
                let body = s.body_skel.as_ref().unwrap();
                let state = &s.clothes_dynamic_runtime.as_ref().unwrap().1;
                let mut copy = body.clone(); state.copy_simulated_pose(&mut copy);
                assert!(copy.tfs.iter().all(|(id, a)| body.tfs.get(id).is_some_and(|b| a.pos == b.pos && a.rot == b.rot)));
            }
            let mut cached = OkSession::from_cache_bytes(&s.to_cache_bytes().unwrap()).unwrap();
            assert_eq!(cached.clothes_dynamics.len(), cloth); assert!(cached.clothes_dynamic_runtime.is_none());
            assert!(cached.parts.iter().filter_map(|p| p.runtime_accessory.as_ref()).all(|r| r.state.is_none()));
            cached.anim_clip = s.anim_clip.clone();
            for mode in [4, 2] {
                cached.anim_scene_mode = mode;
                cached.apply_anim_frame(0.5, 1.0 / 60.0);
                let flags = cached.collider_settings.resolve(mode == 4, cached.collider_overrides);
                assert!(cached.parts.iter().filter_map(|p| p.runtime_accessory.as_ref()).all(|r| r.collider_flags == Some(flags)));
                if cloth > 0 { assert_eq!(cached.clothes_dynamic_runtime.as_ref().unwrap().0, flags); }
            }
            eprintln!("ordinary card {name}: private hair renderers={hair}, shared cloth chains={cloth}, mixed cloth renderers={mixed}, 30 frames/cache/scene pass");
        }
        assert!(total_hair > 0 && total_cloth > 0, "regression must cover both routes");
    }

    #[test]
    #[ignore = "five actual dump cards with all hand shapes, cache and GPU palettes; owns process environment"]
    fn all_dump_cards_hand_shapes_cache_and_palette() {
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE"] { std::env::set_var(key, "1"); }
        for key in ["OK_UE_ANIM", "OK_NODB", "OK_POSE"] { std::env::remove_var(key); }
        let game = crate::test_support::game_path("");
        for name in ["[Card] March_7th_keiyuu.png", "Alice_keiyuu.png", "Hayase_yuuka_keiyuu_V1_02 For KKS.png", "KK_091207.png", "KK_384283.png"] {
            let mut s = open(&game, Some(&game.join("UserData/chara/female").join(name)), 0).unwrap();
            assert_eq!(s.hand_shapes.count(), 24);
            s.anim_scene_mode = 4;
            s.anim_clip = "states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01".into();
            s.apply_anim_frame(0.4, 0.0);
            let _ = s.ensure_skel_export();
            let body = s.body_skel.as_ref().unwrap();
            let root = crate::pose::animator_root_id(body).unwrap();
            let ids = crate::pose::subtree_ids_pub(root, &body.tfs);
            let targets: Vec<_> = crate::shape::hand::TARGETS.iter().map(|name|
                *ids.iter().find(|id| body.tfs[id].name == *name).unwrap()).collect();
            let baseline: Vec<_> = targets.iter().map(|id| body.tfs[id].clone()).collect();
            for side in 0..2 {
                for pattern in 0..s.hand_shapes.count() {
                    assert!(s.set_hand_shape(side, true, pattern, 23, 0.3));
                    s.apply_anim_frame(0.4, 0.0);
                    let body = s.body_skel.as_ref().unwrap();
                    for (i, id) in targets.iter().enumerate() {
                        let tf = &body.tfs[id];
                        assert_eq!(tf.pos, baseline[i].pos); assert_eq!(tf.scale, baseline[i].scale);
                        if i / 15 != side { assert_eq!(tf.rot, baseline[i].rot, "opposite hand {name}"); }
                        assert!(tf.rot.iter().all(|v| v.is_finite()));
                        let bone = s.skel_export.as_ref().unwrap().bones.iter().find(|b| b.name == tf.name).unwrap();
                        assert!(bone.pose_rot.iter().zip(tf.rot).all(|(a,b)| (*a-b).abs() < 1e-6), "GPU hand palette {}", tf.name);
                    }
                    assert_eq!(s.anim_time, 0.4);
                }
                assert!(s.set_hand_shape(side, false, 0, 0, 0.0));
                s.apply_anim_frame(0.4, 0.0);
                for (i,id) in targets.iter().enumerate() { assert_eq!(s.body_skel.as_ref().unwrap().tfs[id].rot, baseline[i].rot); }
            }
            assert!(s.set_hand_shape(0, true, 1, 23, 0.3));
            let mut cached = OkSession::from_cache_bytes(&s.to_cache_bytes().unwrap()).unwrap();
            assert_eq!(cached.hand_shapes.count(), 24);
            cached.anim_clip = s.anim_clip.clone(); cached.anim_scene_mode = 4;
            cached.apply_anim_frame(0.4, 0.0);
            for (i,id) in targets.iter().enumerate() { assert_eq!(cached.body_skel.as_ref().unwrap().tfs[id].rot, baseline[i].rot, "cache does not persist user hand selection"); }
            assert!(cached.set_hand_shape(0, true, 1, 23, 0.3));
            s.apply_anim_frame(0.4, 0.0); cached.apply_anim_frame(0.4, 0.0);
            for id in &targets { assert_eq!(cached.body_skel.as_ref().unwrap().tfs[id].rot, s.body_skel.as_ref().unwrap().tfs[id].rot); }
            eprintln!("Hand shapes {name}: 48 selections/30 finger targets/GPU palette/disable/cache/clock pass");
        }
    }

    #[test]
    #[ignore = "native March FBS geometry diagnostic; owns process environment"]
    fn native_mouth_geometry_fixture() {
        use std::io::Write;
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM"] { std::env::set_var(key, "1"); }
        let game = crate::test_support::game_path("");
        let mut session = open(&game, Some(&game.join("UserData/chara/female/[Card] March_7th_keiyuu.png")), 0).unwrap();
        session.ensure_skel_export();
        let mut writer = std::io::BufWriter::new(std::fs::File::create("../../Saved/Tests/MouthShapes276/RustMouthMarch.tsv").unwrap());
        let attach = crate::head_onto_body(session.body_skel.as_ref(),session.head_skel.as_ref());
        if let Some(head) = session.head_skel.as_ref() {
            for (name, world) in head.names.iter().zip(&head.worlds) {
                let attached = crate::pose::mat_mul_pub(&attach,world);
                write!(writer,"bone_world\t{name}").unwrap();
                for row in attached { for v in row { write!(writer,"\t{v}").unwrap(); } }
                writeln!(writer).unwrap();
            }
        }
        for part in &session.parts {
            if part.mouth_patterns.is_empty() { continue; }
            writeln!(writer, "target\t{}\t{}", part.name, part.positions.len()).unwrap();
            for pattern in [0usize,8] { for percent in [0,80,61,100] {
                let mut vertices = part.positions.clone();
                let pair = part.mouth_patterns[pattern];
                for (channel, weight) in [(part.mouth_patterns[0].0, -1.0f32),
                    (pair.0, (100-percent) as f32/100.0), (pair.1,percent as f32/100.0)] {
                    if channel < 0 { continue; }
                    let indices = &part.morph_idx[channel as usize];
                    let deltas = &part.morph_dxyz[channel as usize];
                    for (i, &vertex) in indices.iter().enumerate() {
                        for axis in 0..3 { vertices[vertex as usize][axis] += deltas[i*3+axis]*weight; }
                    }
                }
                for (index, v) in vertices.iter().enumerate() {
                    writeln!(writer, "vertex\t{}\t{pattern}\t{percent}\t{index}\t{}\t{}\t{}", part.name, v[0],v[1],v[2]).unwrap();
                }
            }}
        }
    }

    #[test]
    #[ignore = "manual K384 dynamic hair attachment audit"]
    fn k384_runtime_hair_attachment_audit() {
        std::env::set_var("OK_GPU_SKIN", "1");
        std::env::set_var("OK_SKELMESH", "1");
        std::env::set_var("OK_NOPOSE", "1");
        std::env::set_var("OK_UE_ANIM", "1");
        let game = crate::test_support::game_path(r"");
        let card = game.join(r"UserData\chara\female\KK_384283.png");
        if !card.ok_is_file() { return; }
        let mut s = open(&game, Some(&card), 0).expect("session open");
        let baked: Vec<_> = s.parts.iter().enumerate().filter_map(|(index, p)| {
            let runtime = p.runtime_accessory.as_ref()?;
            let anchor = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(),
                &runtime.parent_key)?;
            Some((index, p.name.clone(), p.positions.clone(), runtime.parent_key.clone(), anchor))
        }).collect();
        assert!(!baked.is_empty(), "K384 should exercise runtime dynamic hair");
        for (index, part) in s.parts.iter().enumerate() {
            if part.role.contains("hair") || part.role.contains("acc")
                || part.name.eq_ignore_ascii_case("cf_O_face") {
                eprintln!("K384 PART idx={index} name={} role={} follow={:?} parent={:?} kind={} runtime={}",
                    part.name, part.role, part.rigid_follow_bone, part.parent_bone,
                    part.skel_kind, part.runtime_accessory.is_some());
            }
        }
        s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
        // The dump's OBJ/worldVertices are BakeMesh vertices transformed a
        // second time by renderer.localToWorld. Renderer.bounds is Unity's
        // actual rendered world AABB and the valid geometry reference.
        let dump_json = game.join(r"mesh_dump\KK_384283.png\outfit0-tpose\不知火 舞_0\mesh_18_cf_hair_ext_34_01.json");
        if let Ok(bytes) = crate::gamefs::read(&dump_json) {
            let dump: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
            let part = s.parts.iter().find(|p| p.name == "34_mesh")
                .expect("K384 mod hair renderer");
            assert_eq!(part.positions.len(), dump["mesh"]["vertexCount"].as_u64().unwrap() as usize);
            let (preview_min, preview_max) = crate::pose::aabb(&part.positions);
            for field in ["rendererWorldBounds", "manualSkinnedWorldBounds"] {
                let point = |bound: &str| -> [f32; 3] {
                    let p = &dump[field][bound];
                    ["x", "y", "z"].map(|axis| p[axis].as_f64().unwrap() as f32)
                };
                let (actual_min, actual_max) = (point("min"), point("max"));
                let max_bound_error = (0..3).flat_map(|axis| [
                    (preview_min[axis] - actual_min[axis]).abs(),
                    (preview_max[axis] - actual_max[axis]).abs(),
                ]).fold(0.0f32, f32::max);
                eprintln!("K384 {field} preview={preview_min:?}..{preview_max:?} game={actual_min:?}..{actual_max:?} max_error_m={max_bound_error:.6}");
                assert!(max_bound_error < 0.002, "K384 rendered hair bounds differ from {field}");
            }
        }
        for (index, name, before, key, anchor_before) in &baked {
            let after = &s.parts[*index].positions;
            let anchor_after = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), key).unwrap();
            let delta = crate::pose::mat_mul_pub(&anchor_after,
                &crate::pose::mat_inv_pub(anchor_before));
            let max_local = before.iter().zip(after).map(|(a, b)| {
                let p = [delta[0][0]*a[0]+delta[0][1]*a[1]+delta[0][2]*a[2]+delta[0][3],
                    delta[1][0]*a[0]+delta[1][1]*a[1]+delta[1][2]*a[2]+delta[1][3],
                    delta[2][0]*a[0]+delta[2][1]*a[1]+delta[2][2]*a[2]+delta[2][3]];
                ((p[0]-b[0]).powi(2)+(p[1]-b[1]).powi(2)+(p[2]-b[2]).powi(2)).sqrt()
            }).fold(0.0f32, f32::max);
            eprintln!("K384 HAIR NEUTRAL idx={index} {name} parent={key} max_local_m={max_local:.6}");
        }
        std::env::remove_var("OK_UE_ANIM");
        s.anim_clip = "f_aruki_00_00_01".into();
        s.anim_time = 0.0;
        s.reset_dynamic_accessories();
        s.apply_anim_time(0.0);
        for name in ["a_n_headside", "cf_s_head", "cf_J_FaceUp_ty"] {
            let body_pos = s.body_skel.as_ref().and_then(|sk| sk.world_tf(name))
                .map(|m| crate::pose::mat_pos(&m));
            let attached_pos = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), name)
                .map(|m| crate::pose::mat_pos(&m));
            eprintln!("K384 ANCHOR start {name} body={body_pos:?} attached={attached_pos:?}");
        }
        let walk_start: Vec<_> = baked.iter().map(|(index, _, _, key, _)| {
            (s.parts[*index].positions.clone(),
                crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), key).unwrap())
        }).collect();
        for frame in 1..=30 { s.apply_anim_time(frame as f32 / 60.0); }
        for name in ["a_n_headside", "cf_s_head", "cf_J_FaceUp_ty"] {
            let body_pos = s.body_skel.as_ref().and_then(|sk| sk.world_tf(name))
                .map(|m| crate::pose::mat_pos(&m));
            let attached_pos = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), name)
                .map(|m| crate::pose::mat_pos(&m));
            eprintln!("K384 ANCHOR walk {name} body={body_pos:?} attached={attached_pos:?}");
        }
        for ((index, name, _, key, _), (before, anchor_before)) in baked.iter().zip(&walk_start) {
            let part = &s.parts[*index];
            let anchor_after = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), key).unwrap();
            let delta = crate::pose::mat_mul_pub(&anchor_after,
                &crate::pose::mat_inv_pub(anchor_before));
            let max_nonrigid = before.iter().zip(&part.positions).map(|(a, b)| {
                let p = [delta[0][0]*a[0]+delta[0][1]*a[1]+delta[0][2]*a[2]+delta[0][3],
                    delta[1][0]*a[0]+delta[1][1]*a[1]+delta[1][2]*a[2]+delta[1][3],
                    delta[2][0]*a[0]+delta[2][1]*a[1]+delta[2][2]*a[2]+delta[2][3]];
                ((p[0]-b[0]).powi(2)+(p[1]-b[1]).powi(2)+(p[2]-b[2]).powi(2)).sqrt()
            }).fold(0.0f32, f32::max);
            let runtime = part.runtime_accessory.as_ref().unwrap();
            let root_bone = runtime.source.deferred_dbones.as_ref()
                .and_then(|set| set.first_root())
                .and_then(|db| runtime.source.smr_skel.as_ref()
                    .and_then(|sk| sk.tfs.get(&db)))
                .map(|tf| tf.name.clone()).unwrap_or_default();
            if !root_bone.is_empty() {
                let source_root = runtime.source.smr_skel.as_ref().unwrap()
                    .world_tf(&root_bone).unwrap();
                let state = runtime.state.as_ref().unwrap();
                let current_root = crate::pose::mat_mul_pub(&state.placement_correction(),
                    &state.skel.world_tf(&root_bone).unwrap());
                let placement = crate::pose::mat_mul_pub(&anchor_after, &runtime.local_placement);
                let expected_root = crate::pose::mat_mul_pub(&placement, &source_root);
                let anchor_error = (0..3).flat_map(|row| (0..4).map(move |col| (row, col)))
                    .map(|(row, col)| (expected_root[row][col]-current_root[row][col]).abs())
                    .fold(0.0f32, f32::max);
                let position_error = (0..3).map(|row|
                    (expected_root[row][3]-current_root[row][3]).abs())
                    .fold(0.0f32, f32::max);
                eprintln!("K384 HAIR ROOT idx={index} {name} position_error={position_error:.7} matrix_error={anchor_error:.7}");
                assert!(position_error < 0.0001, "{name}: DynamicBone root left its attachment");
            }
            let root_index = runtime.source.bone_names.iter()
                .position(|n| n.eq_ignore_ascii_case(&root_bone));
            let mut max_root = 0.0f32;
            let mut root_count = 0usize;
            if let Some(root_index) = root_index {
                for (v, (a, b)) in before.iter().zip(&part.positions).enumerate() {
                    let Some(weights) = runtime.source.weights.get(v) else { continue; };
                    let Some(ids) = runtime.source.bone_idx.get(v) else { continue; };
                    let root_weight: f32 = (0..4).filter(|&i| ids[i] as usize == root_index)
                        .map(|i| weights[i]).sum();
                    if root_weight < 0.99 { continue; }
                    let p = [delta[0][0]*a[0]+delta[0][1]*a[1]+delta[0][2]*a[2]+delta[0][3],
                        delta[1][0]*a[0]+delta[1][1]*a[1]+delta[1][2]*a[2]+delta[1][3],
                        delta[2][0]*a[0]+delta[2][1]*a[1]+delta[2][2]*a[2]+delta[2][3]];
                    max_root = max_root.max(((p[0]-b[0]).powi(2)+(p[1]-b[1]).powi(2)
                        +(p[2]-b[2]).powi(2)).sqrt());
                    root_count += 1;
                }
            }
            eprintln!("K384 HAIR WALK idx={index} {name} root={root_bone} root_verts={root_count} max_root_m={max_root:.6} max_nonrigid_m={max_nonrigid:.6}");
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn kk_091207_emits_named_face_not_eyeline() {
        let game = crate::test_support::game_path(r"");
        let card = PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png"),
        );
        if !card.ok_is_file() {
            return;
        }
        std::env::set_var("OK_NOCACHE", "1");
        let sess = crate::extract_session(&game, Some(&card), 0, None).expect("session");
        let face = sess
            .parts
            .iter()
            .find(|p| p.name.eq_ignore_ascii_case("cf_O_face"));
        let face = face.expect("cf_O_face missing");
        let n = face.png.as_ref().map(|b| b.len()).unwrap_or(0);
        eprintln!(
            "091207 face role={} png={} verts={} blend={} unlit={}",
            face.role,
            n,
            face.positions.len(),
            face.blend,
            face.unlit
        );
        assert!(face.positions.len() > 1000, "face mesh too small");
        assert!(n > 1000, "face has no texture");
        let eyeline = sess
            .parts
            .iter()
            .find(|p| p.name.eq_ignore_ascii_case("cf_O_eyeline"))
            .expect("eyeline");
        let en = eyeline.png.as_ref().map(|b| b.len()).unwrap_or(0);
        eprintln!(
            "091207 eyeline png={} verts={} blend={}",
            en,
            eyeline.positions.len(),
            eyeline.blend
        );
        assert!(
            en < 20_000,
            "eyeline has the face texture ({en} bytes)"
        );
        assert_ne!(eyeline.blend, "opaque", "eyeline must not be the face slot");
        let aabb = |p: &[[f32; 3]]| {
            let mut mn = [f32::MAX; 3];
            let mut mx = [f32::MIN; 3];
            for v in p {
                for k in 0..3 {
                    mn[k] = mn[k].min(v[k]);
                    mx[k] = mx[k].max(v[k]);
                }
            }
            (mn, mx)
        };
        for name in ["cf_O_face", "cf_Ohitomi_L", "cf_O_mayuge", "o_body_a"] {
            if let Some(p) = sess.parts.iter().find(|p| p.name.eq_ignore_ascii_case(name)) {
                let (mn, mx) = aabb(&p.positions);
                let (rn, rx) = aabb(&p.rest_positions);
                eprintln!(
                    "PLACE {name} kind={} pos y[{:.3},{:.3}] rest y[{:.3},{:.3}] pos0={:?} rest0={:?}",
                    p.skel_kind, mn[1], mx[1], rn[1], rx[1], p.positions.first(), p.rest_positions.first()
                );
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_march7_chinbones() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack - Exclusive KKS\i\[i] Modified headmod v2.2.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/modified_headmod.unity3d") else {
            eprintln!("no head bundle");
            return;
        };
        let (meshes, skel) = crate::unity::meshes_skel_for_maindata_ref(&bytes, "p_cf_head_00");
        let Some(face) = meshes.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")) else {
            eprintln!("no face");
            return;
        };
        eprintln!("face bones={}", face.bone_names.len());
        for n in ["cf_J_ChinLow", "cf_J_Chin_Base", "cf_J_ChinTip_Base", "cf_J_Chin_s", "cf_J_FaceLow_sx", "cf_J_FaceBase"] {
            let idx = face.bone_names.iter().position(|b| b == n);
            eprintln!("  mesh bone {n} idx={idx:?}");
        }
        if let Some(sk) = skel.as_ref() {
            for n in ["cf_J_ChinLow", "cf_J_Chin_Base", "cf_J_ChinTip_Base", "cf_J_Chin_s"] {
                eprintln!("  skel {n} = {}", sk.index_of(n).is_some());
            }
        }
        let mut wsum = [0.0f32; 6];
        let names = ["cf_J_ChinLow", "cf_J_Chin_Base", "cf_J_ChinTip_Base", "cf_J_Chin_s", "cf_J_FaceLow_sx", "cf_J_FaceBase"];
        let idxs: Vec<Option<usize>> = names.iter().map(|n| face.bone_names.iter().position(|b| b == n)).collect();
        for (bi, w) in face.bone_idx.iter().zip(face.weights.iter()) {
            for k in 0..4 {
                if let Some(slot) = idxs.iter().position(|ix| ix == &Some(bi[k] as usize)) {
                    wsum[slot] += w[k];
                }
            }
        }
        for (n, s) in names.iter().zip(wsum) {
            eprintln!("  weight {n} = {s:.1}");
        }
        let chin_s = face.bone_names.iter().position(|b| b == "cf_J_Chin_s");
        let mut low: Vec<(f32, [f32; 3], [u16; 4], [f32; 4])> = face
            .positions
            .iter()
            .zip(face.bone_idx.iter())
            .zip(face.weights.iter())
            .map(|((p, b), w)| (p[1], *p, *b, *w))
            .collect();
        low.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());
        for (y, p, b, w) in low.iter().take(5) {
            let bn = |i: u16| face.bone_names.get(i as usize).map(|s| s.as_str()).unwrap_or("?");
            eprintln!(
                "  low y={y:.4} p={p:?} bones={} {} {} {} w={w:?}",
                bn(b[0]), bn(b[1]), bn(b[2]), bn(b[3])
            );
        }
        if let Some(ci) = chin_s {
            let mut n = 0;
            let mut mn = [f32::MAX; 3];
            let mut mx = [f32::MIN; 3];
            for ((p, b), w) in face.positions.iter().zip(face.bone_idx.iter()).zip(face.weights.iter()) {
                let hit = (0..4).any(|k| b[k] as usize == ci && w[k] > 0.5);
                if !hit {
                    continue;
                }
                n += 1;
                for k in 0..3 {
                    mn[k] = mn[k].min(p[k]);
                    mx[k] = mx[k].max(p[k]);
                }
            }
            eprintln!("  chin_s bind verts={n} y[{:.4},{:.4}] z[{:.4},{:.4}]", mn[1], mx[1], mn[2], mx[2]);
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_march7_mouth() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack - Exclusive KKS\i\[i] Modified headmod v2.2.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/modified_headmod.unity3d") else {
            eprintln!("no head bundle");
            return;
        };
        let (meshes, _) = crate::unity::meshes_skel_for_maindata_ref(&bytes, "p_cf_head_00");
        let Some(face) = meshes.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")) else {
            eprintln!("no face meshes={}", meshes.len());
            return;
        };
        eprintln!("face shapes={} verts={}", face.blend_shapes.len(), face.positions.len());
        for i in 24..40 {
            if let Some((n, d)) = face.blend_shapes.get(i) {
                let mut mx = 0.0f32;
                for (_, p) in d {
                    mx = mx.max((p[0] * p[0] + p[1] * p[1] + p[2] * p[2]).sqrt());
                }
                eprintln!("  [{i}] {n} n={} max={mx:.4}", d.len());
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_march7_hairtex() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod\keiyuuK佬\mod\[Creamstar] COM3D2 Hair v5.5.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/com3d2_hair_f.unity3d") else {
            eprintln!("no bundle");
            return;
        };
        for want in ["Hair_F143", "HairF", "hair_f143", "cf_m_hair_f_143"] {
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes.clone(), &[want]) {
                eprintln!("named {want} -> {n} {}", png.len());
            }
        }
        let (meshes, _) = crate::unity::meshes_skel_for_maindata_ref(&bytes, "f143L");
        eprintln!("meshes={}", meshes.len());
        for m in meshes.iter().take(6) {
            let n = m.texture_png.as_ref().map(|b| b.len()).unwrap_or(0);
            eprintln!("mesh {} png={n} shader={}", m.name, m.mat.as_ref().map(|x| x.shader.as_str()).unwrap_or(""));
            if let Some(png) = &m.texture_png {
                let path = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\hf_{}.png", m.name.replace(['/', '\\', ' '], "_"));
                let _ = std::fs::write(&path, png);
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_march7_parts() {
        let game = crate::test_support::game_path(r"");
        let card = game.join(r"UserData\chara\female\[Card] March_7th_keiyuu.png");
        let info = crate::card::parse_card(&card).expect("card");
        eprintln!(
            "MARCH head={} white={} eye={:?} eye_c={:?} eye_sub={:?} eyeline_up={} eyeline_down={} hair={:?}",
            info.head_id, info.white_id, info.pupil_ids, info.eye_color, info.eye_sub_color, info.eyeline_up_id, info.eyeline_down_id, info.hair_ids
        );
        for (i, p) in info.hair_parts.iter().enumerate() {
            eprintln!("  hairpart {i} c={:?} start={:?} end={:?}", p.color, p.start_color, p.end_color);
        }
        for (i, a) in info.accessory.iter().enumerate() {
            if a.id == 0 {
                continue;
            }
            eprintln!(
                "  acc{i} id={} parent={} pos={:?} rot={:?} scl={:?}",
                a.id, a.parent_key, a.add_pos, a.add_rot, a.add_scale
            );
        }
        for b in info.abm.iter().filter(|b| {
            let n = b.name.to_ascii_lowercase();
            n.contains("chin") || n.contains("facelow") || n.contains("facebase")
        }) {
            let d = b.for_coord(0);
            eprintln!("  abm {} loc={} pos={:?} scl={:?} rot={:?}", b.name, b.location, d.pos, d.scale, d.rot);
        }
        for r in info.resolves.iter().filter(|r| {
            matches!(r.category_no, 100 | 101 | 102 | 121 | 122 | 123 | 407 | 408 | 412 | 413)
        }) {
            eprintln!(
                "  res cat={} slot={} local={} name={} guid={} prop={}",
                r.category_no, r.slot, r.local_slot, r.name, r.mod_id, r.property
            );
        }
        std::env::set_var("OK_NOCACHE", "1");
        std::env::set_var("OK_GPU_SKIN", "1");
        std::env::set_var("OK_SKELMESH", "1");
        std::env::set_var("OK_DIAG", "1");
        let sess = open(&game, Some(&card), 0).expect("session");
        let mut hair = 0;
        let mut acc = 0;
        let aabb = |pts: &[[f32; 3]]| {
            let mut mn = [f32::MAX; 3];
            let mut mx = [f32::MIN; 3];
            for p in pts {
                for k in 0..3 {
                    mn[k] = mn[k].min(p[k]);
                    mx[k] = mx[k].max(p[k]);
                }
            }
            (mn, mx)
        };
        for p in &sess.parts {
            let ln = p.name.to_ascii_lowercase();
            if ln == "cf_o_face" {
                let (mn, mx) = aabb(&p.positions);
                let tip = p.positions.iter().min_by(|a, b| a[1].partial_cmp(&b[1]).unwrap());
                eprintln!("FACE pos y[{:.3},{:.3}] x[{:.3},{:.3}] z[{:.3},{:.3}] kind={} tip={tip:?}", mn[1], mx[1], mn[0], mx[0], mn[2], mx[2], p.skel_kind);
                let mut obj = String::new();
                for v in &p.positions {
                    obj.push_str(&format!("v {} {} {}\n", v[0], v[1], v[2]));
                }
                let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\our_face.obj", obj);
            }
            if p.role.contains("hair") || ln.contains("hair") || ln.contains("kami") {
                hair += 1;
                let (mn, mx) = aabb(&p.positions);
                let (rn, rx) = aabb(&p.rest_positions);
                eprintln!(
                    "HAIR role={} name={} parent={:?} kind={} pos y[{:.3},{:.3}] x[{:.3},{:.3}] rest y[{:.3},{:.3}] n={}",
                    p.role, p.name, p.parent_bone, p.skel_kind, mn[1], mx[1], mn[0], mx[0], rn[1], rx[1], p.positions.len()
                );
            } else if p.role.contains("acc") {
                acc += 1;
                if acc <= 8 {
                    eprintln!("ACC role={} name={} parent={:?}", p.role, p.name, p.parent_bone);
                }
            }
        }
        eprintln!("counts hair={hair} acc={acc} parts={}", sess.parts.len());
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_emit_eye_388415() {
        let game = crate::test_support::game_path(r"");
        let card = game.join(r"UserData\chara\female\KK_388415.png");
        std::env::set_var("OK_NOCACHE", "1");
        let sess = open(&game, Some(&card), 0).expect("session");
        let dump = PathBuf::from(r"C:\Users\Damody\AppData\Local\Temp\opencode");
        for p in &sess.parts {
            let ln = p.name.to_ascii_lowercase();
            if ln.contains("eyeline") || ln.contains("hitomi") {
                if let Some(png) = &p.png {
                    let path = dump.join(format!("emit388_{}.png", p.name));
                    let _ = std::fs::write(&path, png);
                    eprintln!("EMIT {} bytes={} -> {}", p.name, png.len(), path.display());
                }
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_eye_uv_vs_tex() {
        fn band(png: &[u8]) -> String {
            let im = crate::teximg::load_from_memory(png).unwrap().to_rgba8();
            let (w, h) = im.dimensions();
            let mean = |y0: u32, y1: u32| {
                let mut s = [0u64; 4];
                let mut c = 0u64;
                for y in y0..y1 {
                    for x in (0..w).step_by(4) {
                        let p = im.get_pixel(x, y).0;
                        for k in 0..4 {
                            s[k] += p[k] as u64;
                        }
                        c += 1;
                    }
                }
                (
                    s[0] as f32 / c.max(1) as f32,
                    s[3] as f32 / c.max(1) as f32,
                )
            };
            let top = mean(0, h / 4);
            let bot = mean(h * 3 / 4, h);
            let max_a = |y0: u32, y1: u32| {
                let mut m = 0u8;
                for y in y0..y1 {
                    for x in (0..w).step_by(2) {
                        m = m.max(im.get_pixel(x, y).0[3]);
                    }
                }
                m
            };
            format!(
                "{w}x{h} topRGB/A={top:?} botRGB/A={bot:?} maxA top={} bot={}",
                max_a(0, h / 5),
                max_a(h * 4 / 5, h)
            )
        }
        let game = crate::test_support::game_path(r"");
        let eye = crate::gamefs::read(game.join(r"abdata\chara\mt_eye_00.unity3d")).unwrap();
        if let Some((n, png)) = crate::unity::named_png_from_bytes(eye, &["cw_t_hitomi_030"]) {
            eprintln!("TEX {n} {}", band(&png));
            let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\src_hitomi_030.png", &png);
        }
        let upper = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\DEF\[DEF] Upper Eyes v1.0.zipmod"),
        );
        if let Some(png) = crate::mods::zipmod_file_suffix(
            &upper,
            "abdata/sideloader/mt_eyeline_up_00/cw_t_eyeline_up_055.png",
        ) {
            eprintln!("TEX eyeline_up_055 {}", band(&png));
        }
        let indo = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\LeiseHG\[LeiseHG] Indomitable v1.0.zipmod"),
        );
        if let Some(bytes) = crate::mods::zipmod_bundle_named(&indo, "chara/indomitable_eyeline_up.unity3d") {
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes.clone(), &["indomitable_eyeline_up_1"]) {
                eprintln!("TEX _1 {n} {}", band(&png));
                let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\src_indomitable_1.png", &png);
            } else {
                eprintln!("TEX _1 missing");
            }
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes.clone(), &["indomitable_eyeline_up_low"]) {
                eprintln!("TEX LOW {n} {}", band(&png));
            } else {
                eprintln!("TEX LOW indomitable_eyeline_up_low missing");
            }
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes, &["indomitable_eyeline_up"]) {
                eprintln!("TEX {n} {}", band(&png));
                let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\src_indomitable_eyeline.png", &png);
            }
        }
        let gf = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod"),
        );
        if let Some(bytes) = crate::mods::zipmod_bundle_named(&gf, "chara/gf_eyeline_up.unity3d") {
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes, &["eri"]) {
                eprintln!("TEX {n} {}", band(&png));
                let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\src_eri.png", &png);
            } else {
                eprintln!("TEX eri missing in gf_eyeline_up");
            }
        } else {
            eprintln!("TEX gf_eyeline_up bundle missing");
        }
        let tolove = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\[Rem27]To Love-Ru Modpack.zipmod"),
        );
        if let Some(bytes) = crate::mods::zipmod_bundle_named(&tolove, "chara/tolove_eyeline_up.unity3d") {
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes, &["yami"]) {
                eprintln!("TEX {n} {} bytes={}", band(&png), png.len());
            }
        }
        if let Some(bytes) = crate::mods::zipmod_bundle_named(&gf, "chara/gf_eye.unity3d") {
            if let Some((n, png)) = crate::unity::named_png_from_bytes(bytes, &["korone_l"]) {
                eprintln!("TEX {n} {}", band(&png));
                let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\src_korone_l.png", &png);
            }
        }
        let headmod = crate::mods::zipmod_bundle_named(
            &crate::test_support::game_path(r"\mods\Sideloader Modpack - Exclusive KK KKS\minih\[minih] headmod v4.zipmod"),
            "chara/head_mod.unity3d",
        )
        .unwrap_or_default().to_vec();
        let fangs = crate::mods::zipmod_bundle_named(
            &crate::test_support::game_path(r"\mods\Sideloader Modpack - Exclusive KKS\DeathWeasel\[DeathWeasel] Fangs v1.2.zipmod"),
            "chara/deathweasel/head_with_fangs.unity3d",
        )
        .unwrap_or_default().to_vec();
        let heads: Vec<(&str, Vec<u8>, &str)> = vec![
            (
                "vanilla",
                crate::gamefs::read(game.join(r"abdata\chara\bo_head_00.unity3d")).unwrap_or_default(),
                "p_cf_head_00",
            ),
            ("headmod", headmod, "p_cf_head_00"),
            ("fangs", fangs, "p_cf_head_00"),
            (
                "modhead",
                crate::mods::zipmod_bundle_named(
                    &crate::test_support::game_path(r"\mods\Sideloader Modpack - Exclusive KKS\i\[i] Modified headmod v2.2.zipmod"),
                    "chara/modified_headmod.unity3d",
                )
                .unwrap_or_default().to_vec(),
                "p_cf_head_00",
            ),
        ];
        for (tag, bytes, main) in heads {
            if bytes.len() < 32 {
                eprintln!("UV {tag} bundle missing");
                continue;
            }
            let parsed = crate::unity::parse_bundle_cached(bytes);
            let (ms, _) = parsed.meshes_for_maindata(main);
            for want in ["cf_O_eyeline", "cf_Ohitomi_L", "cf_Ohitomi_L02"] {
                if let Some(m) = crate::unity::pick_named(&ms, want) {
                    let mut v0 = f32::MAX;
                    let mut v1 = f32::MIN;
                    let mut top_i = 0usize;
                    let mut top_y = f32::MIN;
                    for (i, p) in m.positions.iter().enumerate() {
                        if p[1] > top_y {
                            top_y = p[1];
                            top_i = i;
                        }
                    }
                    for uv in &m.uvs {
                        v0 = v0.min(uv[1]);
                        v1 = v1.max(uv[1]);
                    }
                    let top_uv = m.uvs.get(top_i).copied().unwrap_or([0.0, 0.0]);
                    eprintln!(
                        "UV {tag} {want} n={} v[{v0:.3},{v1:.3}] topVert y={top_y:.4} uv=({:.3},{:.3})",
                        m.uvs.len(),
                        top_uv[0],
                        top_uv[1]
                    );
                } else {
                    eprintln!("UV {tag} {want} missing");
                }
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_eye_orient() {
        let game = crate::test_support::game_path(r"");
        let bundle = game.join(r"abdata\chara\mt_eye_white_00.unity3d");
        for n in [
            "cw_t_sirome_kage_000",
            "cw_t_sirome_kage_001",
            "cw_t_sirome_kage_003",
            "cw_t_sirome_kage_004",
        ] {
            let bytes = crate::gamefs::read(&bundle).expect("bundle");
            if let Some((name, png)) = crate::unity::named_png_from_bytes(bytes, &[n]) {
                let out = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\{name}.png");
                let _ = std::fs::write(&out, &png);
                if let Ok(im) = crate::teximg::load_from_memory(&png) {
                    let im = im.to_rgba8();
                    let (w, h) = im.dimensions();
                    let band = |y0: u32, y1: u32| {
                        let mut s = 0u64;
                        let mut c = 0u64;
                        for y in y0..y1 {
                            for x in (0..w).step_by(4) {
                                s += im.get_pixel(x, y).0[0] as u64;
                                c += 1;
                            }
                        }
                        s as f32 / c.max(1) as f32
                    };
                    eprintln!(
                        "KAGE {name} {w}x{h} topR={:.0} botR={:.0}",
                        band(0, h / 4),
                        band(h * 3 / 4, h)
                    );
                }
            } else {
                eprintln!("KAGE {n} missing");
            }
        }
        let vanilla = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
        let mod_catalog = crate::list::load_mods_catalog(&game.join("mods"));
        let cards = [
            "KK_091207.png",
            "KK_384283.png",
            "KK_226176.png",
            "KK_388415.png",
        ];
        for name in cards {
            let path = game.join(r"UserData\chara\female").join(name);
            let info = crate::card::parse_card(&path).expect("card");
            eprintln!(
                "CARD {name} head={} white={} eyeline_up={} eyeline_down={} pupil={:?}",
                info.head_id, info.white_id, info.eyeline_up_id, info.eyeline_down_id, info.pupil_ids
            );
            for r in info.resolves.iter().filter(|r| {
                matches!(r.category_no, 100 | 407 | 408 | 412 | 413)
            }) {
                eprintln!(
                    "  resolve cat={} slot={} local={} prop={} guid={}",
                    r.category_no, r.slot, r.local_slot, r.property, r.mod_id
                );
            }
            let wanted: std::collections::HashSet<String> =
                info.resolves.iter().map(|r| r.mod_id.clone()).collect();
            let guids = crate::mods::find_guids(&game.join("mods"), Some(&wanted));
            for (cat, id, key) in [
                (407, info.white_id, "EyeWhiteTex"),
                (412, info.eyeline_up_id, "EyelineUpTex"),
                (408, info.pupil_ids[0], "EyeTex"),
            ] {
                let resolved = info.resolves.iter().find(|r| r.category_no == cat);
                let from_zip = resolved.and_then(|r| {
                    let zip = guids.get(&r.mod_id)?;
                    let catl = crate::list::catalog_from_zipmod(zip);
                    catl.get_slot(cat, r.slot, r.local_slot).cloned()
                });
                let e = from_zip
                    .or_else(|| mod_catalog.get(cat, id).cloned())
                    .or_else(|| vanilla.get(cat, id).cloned());
                match e {
                    Some(e) => eprintln!(
                        "  cat={cat} id={id} name={} ab={} {key}={} zip={}",
                        e.name,
                        e.main_ab,
                        e.info(key),
                        e.extra.get("__zip").map(|s| s.as_str()).unwrap_or("vanilla")
                    ),
                    None => eprintln!("  cat={cat} id={id} MISSING"),
                }
            }
        }
    }

    #[test]
    fn diag_upper_eyes_list() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\DEF\[DEF] Upper Eyes v1.0.zipmod"),
        );
        let cat = crate::list::catalog_from_zipmod(&zip);
        eprintln!("upper eyes entries={}", cat.by_id.len());
        if let Some(e) = cat.get(412, 9997) {
            eprintln!(
                "HIT ab={} up={} kage={} zip={:?}",
                e.main_ab,
                e.info("EyelineUpTex"),
                e.info("EyelineShadowTex"),
                e.extra.get("__zip")
            );
        } else {
            eprintln!("MISS 412/9997");
            for ((c, id), e) in cat.by_id.iter().take(8) {
                eprintln!("  cat={c} id={id} ab={} name={}", e.main_ab, e.name);
            }
        }
        let png = crate::mods::zipmod_file_suffix(
            &zip,
            "abdata/sideloader/mt_eyeline_up_00/cw_t_eyeline_up_055.png",
        );
        eprintln!("png={}", png.as_ref().map(|b| b.len()).unwrap_or(0));
        let mods = crate::test_support::game_path(r"\mods");
        let mut wanted = std::collections::HashSet::new();
        wanted.insert("8246asdf.UpperEyes".to_string());
        let idx = crate::mods::find_guids(&mods, Some(&wanted));
        eprintln!("find_guids={:?}", idx.get("8246asdf.UpperEyes"));
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_kk384283_face_tex() {
        let game = crate::test_support::game_path(r"");
        let card = PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_384283.png"),
        );
        if !card.ok_is_file() {
            return;
        }
        let info = crate::card::parse_card(&card).expect("card");
        eprintln!(
            "head_id={} skin={:?} skin_sub={:?} coords={} white_id={} white_base={:?} white_sub={:?} brow={} eyeline_up={} eyeline_down={} eye={:?} eye_sub={:?} lip={} lip_color={:?} nose={}",
            info.head_id, info.skin_color, info.skin_sub_color, info.coordinate_count,
            info.white_id, info.white_base_color, info.white_sub_color,
            info.eyebrow_id, info.eyeline_up_id, info.eyeline_down_id,
            info.eye_color, info.eye_sub_color, info.lip_id, info.lip_color, info.nose_id
        );
        for r in info.resolves.iter().filter(|r| (400..=415).contains(&r.category_no)) {
            eprintln!(
                "face-resolve cat={} slot={} local={} prop={} name={} guid={}",
                r.category_no, r.slot, r.local_slot, r.property, r.name, r.mod_id
            );
        }
        if let Some((n, png)) = crate::unity::named_png_from_bundle(
            &game.join(r"abdata\chara\mt_eye_white_00.unity3d"),
            &["cw_t_sirome_kage_003", "cw_t_sirome_kage_001"],
        ) {
            let path = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\{n}.png");
            let _ = std::fs::write(&path, &png);
            eprintln!("dumped eye white {n} bytes={} -> {path}", png.len());
        }
        for r in info.resolves.iter().filter(|r| r.category_no == 100 || r.property.to_ascii_lowercase().contains("head")) {
            eprintln!(
                "resolve cat={} slot={} local={} prop={} name={} guid={}",
                r.category_no, r.slot, r.local_slot, r.property, r.name, r.mod_id
            );
        }
        std::env::set_var("OK_FACE_RAW", "1");
        let sess = open(&game, Some(&card), 0).expect("session");
        let dump = PathBuf::from(r"C:\Users\Damody\AppData\Local\Temp\opencode");
        for p in &sess.parts {
            let ln = p.name.to_ascii_lowercase();
            if p.role == "head" || ln.contains("face") || ln.contains("hitomi") || ln.contains("eyeline") || ln.contains("mayuge") || ln.contains("nose") {
                if let Some(png) = &p.png {
                    let path = dump.join(format!("part_{}.png", p.name.replace(['/', '\\'], "_")));
                    let _ = std::fs::write(&path, png);
                    eprintln!(
                        "PART {} role={} blend={} unlit={} png={} uv={}",
                        p.name, p.role, p.blend, p.unlit, png.len(), p.uvs.len()
                    );
                } else {
                    eprintln!("PART {} role={} NO PNG blend={}", p.name, p.role, p.blend);
                }
            }
        }
        for p in &sess.parts {
            if p.name.eq_ignore_ascii_case("cf_Ohitomi_L") {
                if let Some(png) = &p.png {
                    let path = r"C:\Users\Damody\AppData\Local\Temp\opencode\baked_hitomi_L.png";
                    let _ = std::fs::write(path, png);
                    if let Ok(im) = crate::teximg::load_from_memory(png) {
                        let im = im.to_rgba8();
                        let p0 = im.get_pixel(im.width() / 2, im.height() / 2).0;
                        let p1 = im.get_pixel(im.width() / 2, im.height() / 5).0;
                        eprintln!("hitomi center={p0:?} top={p1:?} bytes={}", png.len());
                    }
                }
                if let Some(uv) = p.uvs.first() {
                    eprintln!("hitomi uv0={uv:?} n={}", p.uvs.len());
                }
            }
        }
        for p in &sess.parts {
            if p.name.to_ascii_lowercase().contains("face") || p.role == "head" && p.name.to_ascii_lowercase().contains("cf_o_face") {
                let n = p.png.as_ref().map(|b| b.len()).unwrap_or(0);
                let mean = p.png.as_ref().and_then(|b| {
                    crate::teximg::load_from_memory(b).ok().map(|im| {
                        let im = im.to_rgba8();
                        let (w, h) = im.dimensions();
                        let mut s = [0f64; 4];
                        let mut c = 0f64;
                        for px in im.pixels().step_by(16) {
                            for k in 0..4 {
                                s[k] += px.0[k] as f64;
                            }
                            c += 1.0;
                        }
                        (w, h, [s[0] / c, s[1] / c, s[2] / c, s[3] / c])
                    })
                });
                eprintln!(
                    "FACE part role={} name={} png={} mean={:?} uv={} uv4={} blend={} unlit={}",
                    p.role, p.name, n, mean, p.uvs.len(), p.uv4.len(), p.blend, p.unlit
                );
                assert!(n > 1000, "cf_O_face has no texture");
                if let Some((_, _, m)) = mean {
                    assert!(m[0] > 40.0 && m[1] > 30.0, "face texture is black {:?}", m);
                }
            }
        }
    }

    /// KK_384283 outfits 0-6 must open like the game dump: one face, Material
    /// Editor-disabled renderers omitted, worn meshes present. Accessory hair
    /// is parented to a_n_headside in the game; that is not a failure.
    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn kk_384283_outfits_0_through_6_load() {
        let game = crate::test_support::game_path(r"");
        let card = PathBuf::from(crate::test_support::game_path(r"\UserData\chara\female\KK_384283.png"),
        );
        let dump = crate::test_support::game_path(r"\mesh_dump\KK_384283.png");
        if !game.join("abdata").ok_is_dir() || !card.ok_is_file() {
            eprintln!("skip: game or card missing");
            return;
        }
        let mut failures = Vec::new();
        for outfit in 0..=6 {
            let started = std::time::Instant::now();
            let sess = match open(&game, Some(&card), outfit) {
                Ok(s) => s,
                Err(e) => {
                    failures.push(format!("outfit{outfit} open failed: {e}"));
                    continue;
                }
            };
            let ms = started.elapsed().as_secs_f64() * 1000.0;
            let body = sess.parts.iter().filter(|p| p.role == "body").count();
            let head = sess.parts.iter().filter(|p| p.role == "head").count();
            let mut head_names = std::collections::HashSet::new();
            let mut dup_head = Vec::new();
            let mut junk = Vec::new();
            for p in &sess.parts {
                let n = p.name.to_ascii_lowercase();
                if p.role == "head" && !head_names.insert(n.clone()) {
                    dup_head.push(p.name.clone());
                }
                if n.contains("highlight_")
                    || n.contains("height bar")
                    || n.contains("silhouette")
                    || n == "background"
                    || n == "o_dan_f"
                {
                    junk.push(format!("{}:{}", p.role, p.name));
                }
            }
            let (enabled, disabled) = dump_mesh_names(&dump.join(format!("outfit{outfit}")));
            let loaded: Vec<String> = sess
                .parts
                .iter()
                .map(|p| p.name.to_ascii_lowercase())
                .collect();
            let mut disabled_loaded = Vec::new();
            for d in &disabled {
                let dl = d.to_ascii_lowercase();
                if loaded.iter().any(|n| n == &dl)
                    && !enabled.iter().any(|e| e.eq_ignore_ascii_case(d))
                {
                    disabled_loaded.push(d.clone());
                }
            }
            let mut missing_worn = Vec::new();
            for e in &enabled {
                let el = e.to_ascii_lowercase();
                if !loaded.iter().any(|n| n == &el) {
                    missing_worn.push(e.clone());
                }
            }
            eprintln!(
                "outfit{outfit} parts={} body={body} head={head} dup_head={} junk={} disabled_loaded={} missing_worn={} {:.0}ms",
                sess.parts.len(),
                dup_head.len(),
                junk.len(),
                disabled_loaded.len(),
                missing_worn.len(),
                ms
            );
            if body == 0 {
                failures.push(format!("outfit{outfit} missing body"));
            }
            if head == 0 {
                failures.push(format!("outfit{outfit} missing head"));
            }
            if !dup_head.is_empty() {
                failures.push(format!("outfit{outfit} duplicate head {:?}", dup_head));
            }
            if !junk.is_empty() {
                failures.push(format!("outfit{outfit} studio junk {:?}", junk));
            }
            if !disabled_loaded.is_empty() {
                failures.push(format!(
                    "outfit{outfit} loaded disabled renderers {:?}",
                    disabled_loaded
                ));
            }
            if !missing_worn.is_empty() {
                failures.push(format!(
                    "outfit{outfit} missing worn dump meshes {:?}",
                    missing_worn
                ));
            }
        }
        assert!(failures.is_empty(), "{}", failures.join("\n"));
    }

    fn dump_mesh_names(outfit_dir: &std::path::Path) -> (Vec<String>, Vec<String>) {
        let mut enabled = Vec::new();
        let mut disabled = Vec::new();
        let Ok(rd) = crate::gamefs::read_dir(outfit_dir) else {
            return (enabled, disabled);
        };
        for ent in rd.flatten() {
            let path = ent.path();
            if path.ok_is_dir() {
                let dir_name = ent.file_name().to_string_lossy().to_string();
                if dir_name.starts_with("outfit") {
                    continue;
                }
                let (e, d) = dump_mesh_names(&path);
                enabled.extend(e);
                disabled.extend(d);
                continue;
            }
            let name = ent.file_name().to_string_lossy().to_string();
            if !name.starts_with("mesh_") || !name.ends_with(".json") || name.contains(".materials.")
            {
                continue;
            }
            let Ok(text) = crate::gamefs::read_to_string(&path) else {
                continue;
            };
            let head: String = text.chars().take(12000).collect();
            let mesh_name = head
                .split("\"mesh\":{\"name\":\"")
                .nth(1)
                .and_then(|s| s.split('"').next())
                .unwrap_or("")
                .to_string();
            if mesh_name.is_empty() {
                continue;
            }
            let go = head
                .split("\"gameObject\":\"")
                .nth(1)
                .and_then(|s| s.split('"').next())
                .unwrap_or("");
            let parent = head
                .split("\"parentPath\":\"")
                .nth(1)
                .and_then(|s| s.split('"').next())
                .unwrap_or("");
            let off_nodes = [
                "n_top_b", "n_bot_b", "n_bot_c", "n_panst_b", "n_socks_b", "n_uwt_b",
                "n_uwb_b", "n_uwb_c", "o_inner_b", "o_bra_b", "n_shoes_b",
            ];
            let off_state = parent.split('/').any(|seg| {
                let s = seg.to_ascii_lowercase();
                off_nodes.iter().any(|n| s == *n)
            });
            let ln = mesh_name.to_ascii_lowercase();
            let go_l = go.to_ascii_lowercase();
            let studio = ln.contains("highlight_")
                || ln.contains("height bar")
                || ln.contains("silhouette")
                || ln == "background"
                || go_l.contains("highlight_")
                || go_l.contains("height bar")
                || go_l.contains("silhouette")
                || go_l == "n_tang"
                || go_l == "n_tang_silhouette"
                || go_l == "background";
            // o_gomu / o_dankon are body parts the card preview hides on purpose.
            let hidden_body = ln == "o_gomu" || ln == "o_dankon" || ln == "o_dan_f";
            if head.contains("\"renderer\":{\"enabled\":false") {
                disabled.push(mesh_name);
            } else if head.contains("\"renderer\":{\"enabled\":true")
                && !off_state
                && !studio
                && !hidden_body
            {
                enabled.push(mesh_name);
            }
        }
        (enabled, disabled)
    }
}

#[cfg(test)]
#[test]
fn native_face_draw_three_slots_cache_roundtrip() {
    let mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {
        shader:"Koikano/main_skin".into(),colors:[("_overcolor3".into(),[0.2,0.4,0.6,0.8])].into(),..Default::default()}),
        shader_extra_textures:[("_overtex1".into(),vec![1,2]),("_overtex2".into(),vec![3,4]),("_overtex3".into(),vec![5,6])].into(),
        runtime_textures:[("_overtex3".into(),[1,1,1,0,11])].into(),..Default::default()};
    let part=SessionPart::from_mesh("head",&mesh,"opaque",false);
    let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
    let restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
    assert_eq!(restored.shader_textures,mesh.shader_extra_textures);
    assert_eq!(restored.runtime_textures,mesh.runtime_textures);
    assert_eq!(restored.source_material.unwrap().color("_overcolor3"),Some([0.2,0.4,0.6,0.8]));
    assert!(SessionPart::from_mesh("body",&mesh,"opaque",false).shader_textures.is_empty());
}

#[cfg(test)]
#[path="wardrobe_height_audit.rs"]
mod wardrobe_height_audit;

#[cfg(test)]
#[test]
#[ignore="installed original card and source assets; run in an isolated process"]
fn single_accessory_extraction_preserves_full_source_contract() {
    let game=&crate::test_support::game_path("");
    let card=game.join("UserData/chara/female/19重绘爱莉希雅女仆v1.1.png");
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    let full=crate::session::open(game,Some(&card),0).unwrap();
    let mut info=crate::card::parse_card_outfit(&card,0).unwrap();
    crate::mods::apply_strip_all_migrations(game,&mut info);
    let context=full.accessory_context.as_deref().expect("full source must capture exact extraction inputs");
    assert!(context.matches(&info,0).unwrap());
    {let _worn=crate::unity::request_clothes_states([0;9]);assert!(context.matches(&info,0).unwrap(),"default and explicit worn states are equivalent");}
    let equal=|a:&SessionPart,b:&SessionPart| {
        let a=serde_json::to_value(PartBlob::from_part(a)).unwrap();
        let b=serde_json::to_value(PartBlob::from_part(b)).unwrap();
        if a!=b {for (key,value) in a.as_object().unwrap(){if b.get(key)!=Some(value){eprintln!("single accessory contract differs: {key}");}}}
        assert!(a==b,"all geometry, source rigs, placement, materials and texture fields must agree");
    };
    let mut tested=0;
    for slot in [0usize,1,20] {
        let start=std::time::Instant::now();
        let delta=crate::accessory_extract::extract_slot(game,&info,0,slot,context).unwrap();
        let expected:Vec<_>=full.parts.iter().filter(|p|p.source_property.as_deref()
            .and_then(crate::accessory_slot_from_property)==Some(slot)).collect();
        assert_eq!(delta.len(),expected.len(),"single slot renderer count");
        for (a,b) in expected.iter().zip(&delta){assert_eq!(a.source_property,b.source_property);equal(a,b);}
        eprintln!("OK single accessory slot={slot} renderers={} elapsed_ms={:.3}",delta.len(),start.elapsed().as_secs_f64()*1000.);
        tested+=1;
    }
    let mut changed=info.clone();changed.shape_body[0]+=0.01;
    assert!(!context.matches(&changed,0).unwrap());
    assert!(crate::accessory_extract::extract_slot(game,&changed,0,1,context).is_err());
    // This wig has HideHair=1: the original game deliberately resets both
    // placement layers. Use a stock necklace to exercise editable movement.
    let mut ordinary=info.clone();ordinary.resolves.retain(|r|crate::accessory_slot_from_property(&r.property)!=Some(1));
    ordinary.accessory[1]=ordinary.accessory[0].clone();ordinary.accessory[1].type_id=124;
    ordinary.accessory[1].id=14;ordinary.accessory[1].parent_key.clear();
    let mut moved=ordinary.clone();moved.accessory[1].add_move[0].rot[1]+=45.;
    assert!(context.matches(&moved,0).unwrap(),"slot edits do not invalidate immutable source context");
    let moved=crate::accessory_extract::extract_slot(game,&moved,0,1,context).unwrap();
    assert!(!moved.is_empty());
    let original=crate::accessory_extract::extract_slot(game,&ordinary,0,1,context).unwrap();
    assert!(moved.iter().zip(&original).any(|(a,b)|serde_json::to_value(PartBlob::from_part(a)).unwrap()!=serde_json::to_value(PartBlob::from_part(b)).unwrap()));
    let cached=OkSession::from_cache_bytes(&full.to_cache_bytes().unwrap()).unwrap();
    assert_eq!(cached.accessory_context.as_ref().unwrap().dependencies,context.dependencies);
    let origins:Vec<_>=full.parts.iter().map(|p|p.source_property.clone()).collect();
    let mut legacy=b"OKSC0002".to_vec();legacy.extend(bincode::serialize(&(SessionBlob::from_session(&full),origins)).unwrap());
    let legacy=OkSession::from_cache_bytes(&legacy).unwrap();assert!(legacy.accessory_context.is_none());
    assert_eq!(legacy.parts.len(),full.parts.len());
    let mut raw=std::collections::BTreeMap::new();let mut refs=Vec::new();
    let residue=full.split_preview_resources(&info,|part,field,kind,bytes,_|{
        let id=crate::portable_resources::id(kind,&bytes);raw.insert(id.clone(),bytes);
        refs.push(crate::portable_resources::ResourceRef{part,field:field.into(),kind:kind.into(),id});Ok(())
    }).unwrap();
    let restored=OkSession::merge_preview_resources(&residue,&refs,|r|Ok(raw[&r.id].clone())).unwrap();
    let restored_context=restored.accessory_context.as_deref().unwrap();
    assert!(restored_context.matches(&info,0).unwrap());
    let after=crate::accessory_extract::extract_slot(game,&info,0,0,restored_context).unwrap();
    let before=crate::accessory_extract::extract_slot(game,&info,0,0,context).unwrap();
    for (a,b) in before.iter().zip(&after){equal(a,b);}
    eprintln!("OK single accessory contract cases={tested} exact=1 invalidation=1 cache_v3=1 cache_v2=1 portable_context=1");
}

#[cfg(test)]
#[test]
#[ignore="installed original card and assets; exact renderer-only serialization contract"]
fn part_resource_split_preserves_full_renderer_contract(){
    use bincode::Options;
    let game=&crate::test_support::game_path("");
    let card=game.join("UserData/chara/female/19重绘爱莉希雅女仆v1.1.png");
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    std::env::set_var("OK_BUILTIN_RESOURCES","C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
    let mut source=open(game,Some(&card),0).unwrap();let info=crate::card::parse_card_outfit(&card,0).unwrap();
    assert!(source.accessory_context.is_some()&&source.body_rest.is_some()&&!source.parts.is_empty());
    let mut full=Vec::new();let start=std::time::Instant::now();
    let full_residue=source.split_preview_resources(&info,|part,field,kind,bytes,shared|{
        if let Some(part)=part{full.push((part,field.to_owned(),kind.to_owned(),bytes,shared));}Ok(())
    }).unwrap();let full_ms=start.elapsed().as_secs_f64()*1000.;
    let mut delta=Vec::new();let start=std::time::Instant::now();
    let part_residue=source.split_preview_part_resources(&info,|part,field,kind,bytes,shared|{
        delta.push((part.expect("part scope must never serialize globals"),field.to_owned(),kind.to_owned(),bytes,shared));Ok(())
    }).unwrap();let part_ms=start.elapsed().as_secs_f64()*1000.;
    assert_eq!(full,delta,"every resource byte, role, kind and sharing flag must agree");
    let read=|bytes:&[u8]|bincode::DefaultOptions::new().with_fixint_encoding().with_limit(crate::portable::MAX_BYTES)
        .reject_trailing_bytes().deserialize::<PreviewSnapshot>(bytes).unwrap();
    let a=read(&full_residue);let b=read(&part_residue);
    assert_eq!(serde_json::to_value(&a.session.parts).unwrap(),serde_json::to_value(&b.session.parts).unwrap(),"all renderer metadata and dynamic source definitions exact");
    assert!(b.session.body_rest.is_none()&&b.session.head_rest.is_none()&&b.session.body_shaped.is_none()&&b.session.head_shaped.is_none());
    assert!(b.session.body_skel.is_none()&&b.session.head_skel.is_none()&&b.session.body_keys.is_empty()&&b.session.head_keys.is_empty());
    OkSession::audit_preview_residue(&part_residue).unwrap();
    assert!(source.accessory_context.is_some()&&source.body_rest.is_some(),"immutable source inputs retained");
    eprintln!("OK part resource contract parts={} resources={} full_ms={full_ms:.3} part_ms={part_ms:.3}",source.parts.len(),delta.len());
    source.parts.clear();let mut calls=0;
    let empty=source.split_preview_part_resources(&info,|_,_,_,_,_|{calls+=1;Ok(())}).unwrap();
    assert_eq!(calls,0);assert!(read(&empty).session.parts.is_empty());
}

/// A bone of the body Animator tree by name (private mod rigs with the same joint names are not FK targets).
fn body_bone_id(sk: &crate::pose::Skeleton, name: &str) -> Option<i64> {
    let ids = crate::pose::animator_root_id(sk).map(|root| crate::pose::subtree_ids_pub(root, &sk.tfs)).unwrap_or_else(|| sk.bone_ids.clone());
    ids.into_iter().find(|id| sk.tfs.get(id).is_some_and(|t| t.name.eq_ignore_ascii_case(name)))
}

/// Bone ids looked up by name on one skeleton (a Studio FK list, the hand shapes), found once instead of every frame.
#[derive(Default)]
pub(crate) struct BoneIdCache { pub(crate) key: u64, pub(crate) ids: Vec<Option<i64>> }

/// `apply_user_fk` with the bone lookup kept across frames (the key covers the FK names and the skeleton's bones), local
/// rotations only: the caller rebuilds the world matrices when this returns true.
pub(crate) fn apply_user_fk_locals(sk: &mut crate::pose::Skeleton, fk: &[(String, [f32; 3])], cache: &mut BoneIdCache) -> bool {
    if fk.is_empty() { return false; }
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    for (name, _) in fk { name.hash(&mut h); }
    sk.bone_ids.hash(&mut h);
    let key = h.finish();
    if cache.key != key || cache.ids.len() != fk.len() {
        let ids = crate::pose::animator_root_id(sk).map(|root| crate::pose::subtree_ids_pub(root, &sk.tfs)).unwrap_or_else(|| sk.bone_ids.clone());
        let mut by_name: HashMap<String, i64> = HashMap::with_capacity(ids.len());
        for id in ids { if let Some(t) = sk.tfs.get(&id) { by_name.entry(t.name.to_ascii_lowercase()).or_insert(id); } }
        cache.ids = fk.iter().map(|(name, _)| by_name.get(&name.to_ascii_lowercase()).copied()).collect();
        cache.key = key;
    }
    for ((_, euler), id) in fk.iter().zip(&cache.ids) {
        if let Some(tf) = id.and_then(|id| sk.tfs.get_mut(&id)) { tf.rot = crate::pose::euler_deg_to_quat(*euler); }
    }
    true
}

/// Studio FK: the listed bones take these local rotations (Unity Euler degrees) instead of the animated ones.
pub(crate) fn apply_user_fk(sk: &mut crate::pose::Skeleton, fk: &[(String, [f32; 3])]) {
    if fk.is_empty() { return; }
    // The bones by name as body_bone_id finds them (the first of the Animator subtree, any case), looked up once for
    // the whole list: a Studio FK pose names dozens of bones every frame, and walking the subtree for each took ~9 ms.
    let ids = crate::pose::animator_root_id(sk).map(|root| crate::pose::subtree_ids_pub(root, &sk.tfs)).unwrap_or_else(|| sk.bone_ids.clone());
    let mut by_name: HashMap<String, i64> = HashMap::with_capacity(ids.len());
    for id in ids { if let Some(t) = sk.tfs.get(&id) { by_name.entry(t.name.to_ascii_lowercase()).or_insert(id); } }
    for (name, euler) in fk {
        if let Some(id) = by_name.get(&name.to_ascii_lowercase()) { if let Some(tf) = sk.tfs.get_mut(id) { tf.rot = crate::pose::euler_deg_to_quat(*euler); } }
    }
    sk.rebuild_worlds();
}

#[cfg(test)]
mod user_fk_tests {
    #[test]
    fn unity_euler_round_trip() {
        for e in [[10.0f32, 20.0, 30.0], [-45.0, 80.0, 5.0], [0.0, -170.0, 60.0], [89.0, 0.0, -30.0]] {
            let q = crate::pose::euler_deg_to_quat(e);
            let back = crate::pose::euler_deg_to_quat(crate::pose::quat_to_unity_euler(q));
            let dot: f32 = (0..4).map(|i| q[i] * back[i]).sum();
            assert!(dot.abs() > 0.9999, "{e:?} -> {:?}", crate::pose::quat_to_unity_euler(q));
        }
    }
}

#[cfg(test)]
mod user_fk_card_tests {
    #[test]
    #[ignore = "OK base library + OK card: FK replaces the static pose rotation"]
    fn fk_on_ok_card() {
        let card = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C002.png");
        let mut s = crate::oklib::open_test_card(&card, 0).unwrap();
        s.refresh_static_interaction(0.);
        let before = s.body_bone_euler("cf_j_arm00_L");
        s.user_fk = vec![("cf_j_arm00_L".into(), [10., 25., -50.])];
        s.refresh_static_interaction(0.);
        let after = s.body_bone_euler("cf_j_arm00_L");
        let names: Vec<String> = s.body_skel.as_ref().map(|sk| sk.tfs.values().filter(|t| t.name.contains("arm00_L")).map(|t| t.name.clone()).collect()).unwrap_or_default();
        println!("before={before:?} after={after:?} arm tfs={names:?} root={:?}", s.body_skel.as_ref().and_then(crate::pose::animator_root_id));
        let a = after.unwrap();
        assert!((a[0] - 10.).abs() < 0.05 && (a[1] - 25.).abs() < 0.05 && (a[2] + 50.).abs() < 0.05);
    }
}

/// Studio IK: the listed effectors reach their positions (full weight); the others follow the solve.
/// Studio IK targets: 0..8 the FBBIK effectors, then the bend goals (left elbow, right elbow, left knee, right knee).
pub(crate) const STUDIO_BEND_FIRST: usize = 9;
pub(crate) const STUDIO_IK_TARGETS: usize = 13;

pub(crate) fn apply_studio_ik(sk: &mut crate::pose::Skeleton, solver: &mut Option<crate::finalik::IKSolverFullBodyBiped>, targets: &[(usize, [f32; 3])], rotations: &[(usize,[f32;4])]) {
    let Some(solver) = solver.as_mut() else { return; };
    if targets.is_empty() { return; }
    let mut ik = crate::finalik::IkSkeleton::new_batched(sk);
    // The game prefab binds effectors to its IK target objects; Studio IK gives the positions itself.
    for effector in solver.effectors.iter_mut() { effector.target = None; effector.position_weight = 0.; effector.rotation_weight = 0.; effector.position_offset = [0.; 3]; }
    // 9..12: elbow / knee targets bend the limbs (left arm, right arm, left leg, right leg = bend constraints 1..4).
    for bend in solver.bend_constraints.iter_mut() { bend.goal_position = None; bend.weight = 0.; }
    for (index, position) in targets {
        if *index >= STUDIO_BEND_FIRST {
            if let Some(bend) = solver.bend_constraints.get_mut(*index - STUDIO_BEND_FIRST + 1) { bend.goal_position = Some(*position); bend.weight = 1.; }
        } else if let Some(effector) = solver.effectors.get_mut(*index) { effector.position = *position; effector.position_weight = 1.; }
    }
    for (index, rotation) in rotations {
        if let Some(effector)=solver.effectors.get_mut(*index) { effector.rotation=*rotation; effector.rotation_weight=1.; }
    }
    solver.update(&mut ik);
    ik.sk.rebuild_worlds();
}

#[cfg(test)]
mod studio_ik_card_tests {
    #[test]
    #[ignore = "OK base library + OK card + shared resources: an IK target moves the hand there"]
    fn left_hand_reaches_target() {
        let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let card = manifest.join("../../Content/CharacterCards/CrowdAPNG/C002.png");
        let root = manifest.join("../../Content/IndependentResources/base_v1/paired");
        let mut s = crate::oklib::open_test_card(&card, 0).unwrap();
        s.refresh_static_interaction(0.);
        s.prepare_studio_ik(&root).unwrap();
        let hand = s.ik_effector_position(5).unwrap();
        let foot = s.ik_effector_position(8).unwrap();
        let target = [hand[0] + 0.10, hand[1] + 0.05, hand[2] - 0.08];
        s.studio_ik = vec![(5, target)];
        s.refresh_static_interaction(0.);
        let reached = s.ik_effector_position(5).unwrap();
        let foot_after = s.ik_effector_position(8).unwrap();
        let d = |a: [f32; 3], b: [f32; 3]| ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt();
        println!("hand {hand:?} -> {reached:?} target {target:?} miss {:.4} m; right foot moved {:.4} m", d(reached, target), d(foot, foot_after));
        assert!(d(reached, target) < 0.01);
        s.studio_ik.clear();
        s.refresh_static_interaction(0.);
        assert!(d(s.ik_effector_position(5).unwrap(), hand) < 0.001, "clearing IK restores the hand");
        // A bend goal (9: left elbow) turns the elbow towards it while the hand stays on its target.
        let elbow = s.ik_effector_position(9).unwrap();
        s.studio_ik = vec![(5, target)];
        s.refresh_static_interaction(0.);
        let free = s.ik_effector_position(9).unwrap();
        s.studio_ik = vec![(5, target), (9, [elbow[0], elbow[1] - 0.3, elbow[2] - 0.3])];
        s.refresh_static_interaction(0.);
        let bent = s.ik_effector_position(9).unwrap();
        let hand_bent = s.ik_effector_position(5).unwrap();
        println!("elbow {elbow:?}: free {free:?}, with bend goal {bent:?} (moved {:.4} m), hand miss {:.4} m", d(free, bent), d(hand_bent, target));
        assert!(d(free, bent) > 0.01 && d(hand_bent, target) < 0.01);
    }
}


#[cfg(test)]
mod hair_parent_chain_tests {
    /// Hair parented under cf_J_FaceUp_ty (ChaFileHair parts) is solved on the attachment chain at open: its vertices
    /// are at head height, not in the head-local frame at the origin (the card reader drew them at the feet).
    #[test]
    fn vanilla_hair_is_placed_on_the_head_at_open() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG");
        let mut checked = 0;
        for n in 0..30 {
            let card = dir.join(format!("C{n:03}.png"));
            if !card.is_file() { continue; }
            let Ok(s) = crate::oklib::open_test_card(&card, 0) else { continue };
            for p in s.parts.iter().filter(|p| p.role.starts_with("hair") && p.runtime_accessory.as_ref().is_some_and(|r| r.parent_key == "cf_J_FaceUp_ty")) {
                let top = p.positions.iter().map(|v| v[1]).fold(f32::MIN, f32::max);
                assert!(top > 1.0, "{} {}: hair top at {top} m", card.display(), p.name);
                checked += 1;
            }
            if checked > 0 { break; }
        }
        assert!(checked > 0, "no crowd card with ChaFileHair hair under cf_J_FaceUp_ty");
    }
}

#[cfg(test)]
mod studio_pose_rotation_tests {
    #[test]
    #[ignore = "OK base library and C002 card: wrist orientation and saved neck"]
    fn rotations_survive_pose_and_clear() {
        let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let mut s = crate::oklib::open_test_card(
            &manifest.join("../../Content/CharacterCards/CrowdAPNG/C002.png"),
            0,
        )
        .unwrap();
        s.refresh_static_interaction(0.);
        s.prepare_studio_ik(&manifest.join("../../Content/IndependentResources/base_v1/paired"))
            .unwrap();
        let target = s.ik_effector_position(5).unwrap();
        let q = crate::pose::euler_deg_to_quat([34., -55., 72.]);
        s.studio_ik = vec![(5, target)];
        s.studio_ik_rotations = vec![(5, q)];
        s.refresh_static_interaction(0.);
        let mut ik = crate::finalik::IkSkeleton::new(s.body_skel.as_mut().unwrap());
        let got = ik.world_rot("cf_j_hand_L").unwrap();
        let dot = |a: [f32; 4], b: [f32; 4]| (0..4).map(|i| a[i] * b[i]).sum::<f32>().abs();
        println!("wrist expected {q:?} actual {got:?}");
        assert!(dot(q, got) > 0.9999);
        let reached = s.ik_effector_position(5).unwrap();
        assert!(
            (0..3)
                .map(|i| (reached[i] - target[i]).powi(2))
                .sum::<f32>()
                .sqrt()
                < 0.01
        );
        let fixed = [
            crate::pose::euler_deg_to_quat([-15., 25., 8.]),
            crate::pose::euler_deg_to_quat([12., -18., -7.]),
        ];
        s.head_look.fixed = Some(fixed);
        s.refresh_static_interaction(0.);
        for (name, q) in ["cf_j_neck", "cf_j_head"].into_iter().zip(fixed) {
            assert!(
                dot(
                    q,
                    s.body_skel
                        .as_ref()
                        .unwrap()
                        .tfs
                        .values()
                        .find(|tf| tf.name == name)
                        .unwrap()
                        .rot
                ) > 0.99999
            );
        }
        for live in [false, true] {
            let mut body = s.shape_body.clone();
            let face = s.shape_face.clone();
            assert!(!body.is_empty());
            body[0] = if body[0] > 0.5 {
                body[0] - 0.02
            } else {
                body[0] + 0.02
            };
            if live {
                s.apply_shape_live(&body, &face);
            } else {
                s.apply_shape(&body, &face);
            }
            for (name, q) in ["cf_j_neck", "cf_j_head"].into_iter().zip(fixed) {
                assert!(
                    dot(
                        q,
                        s.body_skel
                            .as_ref()
                            .unwrap()
                            .tfs
                            .values()
                            .find(|tf| tf.name == name)
                            .unwrap()
                            .rot
                    ) > 0.99999,
                    "shape rebuild lost FIX for {name}, live={live}"
                );
            }
        }
        let before = s.studio_ik.clone();
        let s_ptr = &mut s as *mut _;
        let bad = [0.; 4];
        let enabled = [1u8];
        let index = [5i32];
        assert_eq!(
            unsafe {
                crate::ffi::ok_session_set_studio_ik_pose(
                    s_ptr,
                    index.as_ptr(),
                    target.as_ptr(),
                    bad.as_ptr(),
                    enabled.as_ptr(),
                    1,
                )
            },
            -1
        );
        assert_eq!(s.studio_ik, before);
        assert_eq!(s.studio_ik_rotations, vec![(5, q)]);
        assert_eq!(
            unsafe { crate::ffi::ok_session_set_studio_fixed_neck(s_ptr, std::ptr::null(), 0) },
            0
        );
        assert_eq!(
            unsafe {
                crate::ffi::ok_session_set_studio_ik(s_ptr, std::ptr::null(), std::ptr::null(), 0)
            },
            0
        );
        assert!(s.studio_ik_rotations.is_empty());
        s.refresh_static_interaction(0.);
        assert!(s.head_look.fixed.is_none());
        let head = s
            .body_skel
            .as_ref()
            .unwrap()
            .tfs
            .values()
            .find(|tf| tf.name == "cf_j_head")
            .unwrap()
            .rot;
        assert!(dot(head, fixed[1]) < 0.999);
    }
}

#[cfg(test)]
#[test]
#[ignore="base library: 3800 saved neck FK and source camera"]
fn scene_3800_neck_fk_overrides_saved_fix() {
    let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../");
    let lib=root.join("Content/IndependentResources/base_v1");
    let bytes=std::fs::read(lib.join("studio/samples/koikatu_cs0003800.png")).unwrap();
    let scene=crate::studio_scene::read(&bytes).unwrap();
    let camera=scene.camera.as_ref().unwrap();
    assert!((camera.fov-27.3473415).abs()<0.00001);
    assert!((camera.rotate[2]-55.250511).abs()<0.00001);
    let object=scene.objects.iter().find(|o|matches!(o,crate::studio_scene::Object::Character{key:37,..})).unwrap();
    let crate::studio_scene::Object::Character{fk,neck,active_fk,enable_fk,..}=object else{unreachable!()};
    assert!(*enable_fk&&active_fk[1]);
    let fixed:&[[f32;4]]= &neck.as_ref().unwrap().rotations;
    let path=root.join("Saved/OKStudio/3800-neck-source.png");std::fs::write(&path,crate::studio_scene::card_bytes(&bytes,object).unwrap()).unwrap();
    let mut s=open(&lib,Some(&path),0).unwrap();
    s.head_look.fixed=Some(fixed.try_into().unwrap());
    s.user_fk=[(1,"cf_j_head"),(2,"cf_j_neck")].into_iter().map(|(id,name)|
        (name.to_owned(),fk.iter().find(|(n,_)|*n==id).unwrap().1.rot)).collect();
    let expected=s.user_fk.clone();
    for mode in 0..3 {
        if mode==0{s.refresh_static_interaction(0.);}
        else {let mut values=s.shape_body.clone();values[0]=(values[0]+0.03).min(0.99);let face=s.shape_face.clone();if mode==1{s.apply_shape(&values,&face);}else{s.apply_shape_live(&values,&face);}}
        let sk=s.body_skel.as_ref().unwrap();
        for (name,euler) in &expected {
            let actual=sk.tfs.values().find(|t|t.name==*name).unwrap().rot;let q=crate::pose::euler_deg_to_quat(*euler);
            assert!(actual.iter().zip(q).map(|(a,b)|a*b).sum::<f32>().abs()>0.99999,"mode={mode} {name}");
            println!("3800 mode={mode} {name} expected={q:?} actual={actual:?}");
        }
    }
    s.user_fk.clear();s.refresh_static_interaction(0.);
    for (name,q) in ["cf_j_neck","cf_j_head"].into_iter().zip(fixed) {
        let actual=s.body_skel.as_ref().unwrap().tfs.values().find(|t|t.name==name).unwrap().rot;
        assert!(actual.iter().zip(q).map(|(a,b)|a*b).sum::<f32>().abs()>0.99999,"FIX resumes after clearing FK");
    }
}

#[cfg(test)]
#[test]
fn native_face_overlay_colors_survive_legacy_snapshot_transport() {
    let lip=[0.8864779,0.6661562,0.7498654,0.08616342];let cheek=[1.,0.6862745,0.6862745,0.12];
    for shader in ["Koikano/main_skin","Shader Forge/main_skin"] {
        let mesh=ExtractedMesh{name:"cf_O_face".into(),mat:Some(crate::unity::MeshMaterial{shader:shader.into(),
            colors:[("_overcolor1".into(),lip),("_overcolor2".into(),cheek)].into(),..Default::default()}),..Default::default()};
        let part=SessionPart::from_mesh("head",&mesh,"opaque",false);
        assert_eq!(part.overcolor,[1.,1.,1.,0.]);
        assert_eq!(part.draw_overlay_color(0),lip);assert_eq!(part.draw_overlay_color(1),cheek);
        let bytes=bincode::serialize(&PartBlob::from_part(&part)).unwrap();
        let mut restored=bincode::deserialize::<PartBlob>(&bytes).unwrap().into_part();
        assert_eq!(restored.draw_overlay_color(0),lip);assert_eq!(restored.draw_overlay_color(1),cheek);
        restored.source_material.as_mut().unwrap().colors.remove("_overcolor1");
        assert_eq!(restored.draw_overlay_color(0),restored.overcolor);
        let mut body=SessionPart::from_mesh("body",&mesh,"opaque",false);body.overcolor=[0.4,0.2,0.1,0.3];
        assert_eq!(body.draw_overlay_color(0),body.overcolor,"body nipple transport remains authoritative");
    }
}

/// OK_ACC_PART_PROFILE=<frames>: print each accessory part's average solve time every that many of its frames.
fn acc_part_profile_every() -> Option<u32> {
    static EVERY: std::sync::OnceLock<Option<u32>> = std::sync::OnceLock::new();
    *EVERY.get_or_init(|| std::env::var("OK_ACC_PART_PROFILE").ok().and_then(|v| v.parse().ok()).filter(|&n: &u32| n > 0))
}

/// OK_ACC_SHARED_SOLVE=0: every renderer of a multi-renderer prefab solves its DynamicBones on its own (diagnostic).
fn acc_shared_solve() -> bool {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ON.get_or_init(|| std::env::var("OK_ACC_SHARED_SOLVE").ok().as_deref() != Some("0"))
}
