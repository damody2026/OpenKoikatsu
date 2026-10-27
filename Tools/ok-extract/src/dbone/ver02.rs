//! KKS DynamicBone_Ver02: ordered, reference-driven body chains.
//! These are not descendant-traversing DynamicBone hair components.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use super::*;

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct BodyDynamicSet { components: Vec<Component> }

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
struct Component {
    root: String, bones: Vec<String>, patterns: Vec<Pattern>, enabled: bool,
    kind: Option<usize>, update_rate: f32, reflect_speed: f32, max_steps: usize,
    force: [f32; 3], colliders: Vec<NamedCollider>,
}
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
struct NamedCollider { name: String, collider: BodyCollider }
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
struct Pattern { gravity: [f32; 3], end: [f32; 3], params: Vec<Parameter> }
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
struct Parameter {
    reference: Option<String>, rotate: bool, damping: f32, elasticity: f32,
    stiffness: f32, inert: f32, next_length: f32, radius: f32,
    move_limit: bool, min: [f32; 3], max: [f32; 3], keep_min: f32, keep_max: f32,
    crush: bool, crush_min: f32, crush_max: f32, crush_add_min: f32, crush_add_max: f32,
}
impl Default for Parameter {
    fn default() -> Self {
        Self { reference: None, rotate: false, damping: 0.0, elasticity: 0.0,
            stiffness: 0.0, inert: 0.0, next_length: 1.0, radius: 0.0,
            move_limit: false, min: [0.0; 3], max: [0.0; 3], keep_min: 0.0,
            keep_max: 0.0, crush: false, crush_min: 0.0, crush_max: 0.0,
            crush_add_min: 0.0, crush_add_max: 0.0 }
    }
}
fn field<'a>(v: &'a UnityValue, name: &str) -> Option<&'a UnityValue> {
    if let UnityValue::Object(o) = v { o.get(name) } else { None }
}
fn array(v: Option<&UnityValue>) -> &[UnityValue] {
    if let Some(UnityValue::Array(a)) = v { a } else { &[] }
}
fn number(v: &UnityValue, name: &str) -> f32 { field(v, name).map(as_f32).unwrap_or(0.0) }
fn vector(v: &UnityValue, name: &str) -> [f32; 3] { field(v, name).map(vec3).unwrap_or([0.0; 3]) }
fn flag(v: &UnityValue, name: &str) -> bool { field(v, name).map(|v| behaviour_enabled(Some(v))).unwrap_or(false) }
fn reference(v: Option<&UnityValue>, skel: &Skeleton) -> Option<String> {
    // References are local PPtrs in this file, never a name search in another bundle.
    let v = v?;
    if field(v, "m_FileID").and_then(|v| v.as_i64()).unwrap_or(0) != 0 { return None; }
    skel.tfs.get(&pptr(v)?).map(|tf| tf.name.clone())
}
pub fn parse(serialized: &SerializedFile, skel: &Skeleton) -> BodyDynamicSet {
    parse_resolved(serialized,skel,&crate::unity::references::Scripts::local(serialized))
}
pub(crate) fn parse_resolved(serialized: &SerializedFile, skel: &Skeleton, scripts:&crate::unity::references::Scripts) -> BodyDynamicSet {
    let behaviours: Vec<_> = serialized.objects.iter().filter(|o| o.type_id == 114)
        .filter_map(|o| Some((o.path_id, UnityObject::from_serialized_file(serialized, o).ok()?))).collect();
    let mut kinds = HashMap::new();
    for (_, obj) in &behaviours {
        let p = obj.class.properties();
        if scripts.class(p.get("m_Script")) == Some("ChaPreparationBodyBone") {
            for (kind, v) in array(p.get("_dynamicBones")).iter().enumerate() {
                if let Some(id) = pptr(v) { kinds.insert(id, kind); }
            }
        }
    }
    let colliders: HashMap<_, _> = parse_colliders_resolved(serialized, Some(skel),scripts).into_iter().collect();
    let mut components = Vec::new();
    for (id, obj) in behaviours {
        let p = obj.class.properties();
        if scripts.class(p.get("m_Script")) != Some("DynamicBone_Ver02") { continue; }
        let Some(root) = reference(p.get("Root"), skel) else { continue; };
        let bones: Option<Vec<_>> = array(p.get("Bones")).iter().map(|v| reference(Some(v), skel)).collect();
        let Some(bones) = bones.filter(|b| !b.is_empty()) else { continue; };
        let mut patterns = Vec::new();
        for v in array(p.get("Patterns")) {
            let mut params = Vec::new();
            let mut valid = true;
            for param in array(field(v, "Params")) {
                let r = reference(field(param, "RefTransform"), skel);
                if r.is_none() { valid = false; break; }
                params.push(Parameter { reference: r, rotate: flag(param, "IsRotationCalc"),
                    damping: number(param, "Damping").clamp(0.0, 1.0), elasticity: number(param, "Elasticity").clamp(0.0, 1.0),
                    stiffness: number(param, "Stiffness").clamp(0.0, 1.0), inert: number(param, "Inert").clamp(0.0, 1.0),
                    next_length: number(param, "NextBoneLength").max(0.0), radius: number(param, "CollisionRadius").max(0.0),
                    move_limit: flag(param, "IsMoveLimit"), min: vector(param, "MoveLimitMin"), max: vector(param, "MoveLimitMax"),
                    keep_min: number(param, "KeepLengthLimitMin"), keep_max: number(param, "KeepLengthLimitMax"),
                    crush: flag(param, "IsCrush"), crush_min: number(param, "CrushMoveAreaMin"), crush_max: number(param, "CrushMoveAreaMax"),
                    crush_add_min: number(param, "CrushAddXYMin"), crush_add_max: number(param, "CrushAddXYMax") });
            }
            if !valid || params.len() != bones.len() { break; }
            params.push(Parameter { damping: number(v, "EndOffsetDamping").clamp(0.0, 1.0),
                elasticity: number(v, "EndOffsetElasticity").clamp(0.0, 1.0),
                stiffness: number(v, "EndOffsetStiffness").clamp(0.0, 1.0),
                inert: number(v, "EndOffsetInert").clamp(0.0, 1.0), ..Parameter::default() });
            patterns.push(Pattern { gravity: vector(v, "Gravity"), end: vector(v, "EndOffset"), params });
        }
        if patterns.len() != array(p.get("Patterns")).len() || patterns.is_empty() {
            eprintln!("ok-extract: unsupported Ver02 references/parameter count on {root}"); continue;
        }
        components.push(Component { root, bones, patterns,
            enabled: behaviour_enabled(p.get("m_Enabled")), kind: kinds.get(&id).copied(),
            update_rate: p.get("UpdateRate").map(as_f32).unwrap_or(60.0).max(0.0),
            reflect_speed: p.get("ReflectSpeed").map(as_f32).unwrap_or(1.0),
            max_steps: p.get("HeavyLoopMaxCount").and_then(|v| v.as_i64()).unwrap_or(3).max(1) as usize,
            force: p.get("Force").map(vec3).unwrap_or([0.0; 3]),
            colliders: pptr_list(p.get("Colliders")).iter().filter_map(|id| {
                let c = colliders.get(id)?; Some(NamedCollider { name: skel.tfs.get(&c.transform)?.name.clone(), collider: c.clone() })
            }).collect() });
    }
    BodyDynamicSet { components }
}
impl BodyDynamicSet {
    /// Bones the enabled dynamics move (roots and chains): their pose comes from the simulation, not the animation.
    pub fn moved_bones(&self) -> Vec<String> {
        let mut out: Vec<String> = self.components.iter().filter(|c| c.enabled).flat_map(|c| std::iter::once(c.root.clone()).chain(c.bones.iter().cloned())).filter(|n| !n.is_empty()).collect();
        out.sort();
        out.dedup();
        out
    }

    pub(crate) fn describe(&self) -> String {
        self.components.iter().map(|c| format!("{} en={} rate={} kind={:?} bones={} grav0={:?}",
            c.root, c.enabled, c.update_rate, c.kind, c.bones.len(), c.patterns.first().map(|p| p.gravity))).collect::<Vec<_>>().join(" | ")
    }
    pub fn extend(&mut self, other: Self) { self.components.extend(other.components); }
    pub fn len(&self) -> usize { self.components.len() }
    pub fn runtime(&self, skel: &mut Skeleton, sex: i32, softness: f32, weight: f32, size: f32) -> BodyDynamicRuntime {
        self.runtime_impl(skel,sex,softness,weight,size,false,false)
    }
    pub(crate) fn breast_references(&self, skel: &mut Skeleton, sex: i32, softness: f32, weight: f32, size: f32) -> BodyDynamicRuntime {
        self.runtime_impl(skel,sex,softness,weight,size,true,true)
    }
    /// Refresh reference fields directly; particle history and authored chains
    /// remain owned by the live runtime. The fresh-runtime path stays an oracle.
    pub(crate) fn refresh_live_breast_references(&self, skel:&Skeleton, softness:f32, weight:f32, size:f32, runtime:&mut BodyDynamicRuntime) {
        if !runtime.chains.iter().any(|c|matches!(c.kind,Some(0|1))) {return;}
        let names=crate::pose::rig_name_index(skel,true);
        let valid:Vec<_>=self.components.iter().filter(|c|matches!(c.kind,Some(0|1))).filter_map(|c| {
            let root=*names.get(&c.root)?;
            let ids=c.bones.iter().map(|n|names.get(n).copied()).collect::<Option<Vec<_>>>()?;
            Some((c,root,ids))
        }).collect();
        let mut scratch=runtime.reference_scratch.take();
        if let Some(saved)=scratch.as_mut(){saved.restore_from(skel);}
        let mut previous_targets:Vec<i64>=Vec::new();let mut source_worlds=HashMap::new();
        for chain in &mut runtime.chains {
            if !matches!(chain.kind,Some(0|1)) {continue;}
            // A duplicate root/kind selects the first valid source, just like
            // refresh_breast_references finds the first fresh matching chain.
            let Some((c,_,ids))=valid.iter().find(|(c,root,_)|*root==chain.root&&c.kind==chain.kind) else {continue;};
            let mut pattern=c.patterns[0].clone();if pattern.params.len()!=chain.particles.len(){continue;}
            let rate=(softness*size+0.01).clamp(0.0,1.0);
            let tree=|v:[f32;3]|if rate<0.5 {v[0]+(v[1]-v[0])*rate*2.0}else{v[1]+(v[2]-v[1])*(rate-0.5)*2.0};
            for p in &mut pattern.params {p.damping=tree([0.2,0.1,0.1]);p.elasticity=tree([0.2,0.15,0.05]);p.stiffness=tree([1.0,0.1,0.01]);}
            pattern.gravity=[0.0,-0.005*weight.clamp(0.0,1.0)*size*softness*0.5,0.0];
            let mut reference_pose=scratch.take().unwrap_or_else(||skel.clone());
            for id in previous_targets.drain(..) {if let (Some(target),Some(source))=(reference_pose.tfs.get_mut(&id),skel.tfs.get(&id)){target.pos=source.pos;target.rot=source.rot;target.scale=source.scale;}}
            for (id,p) in ids.iter().zip(&pattern.params) {
                if let Some(source)=p.reference.as_ref().and_then(|n|names.get(n)).and_then(|id|skel.tfs.get(id)) {
                    if let Some(target)=reference_pose.tfs.get_mut(id){target.pos=source.pos;target.rot=source.rot;target.scale=source.scale;previous_targets.push(*id);}
                }
            }
            let mut reference_worlds=HashMap::new();
            for (i,(old,p)) in chain.particles.iter_mut().zip(&pattern.params).enumerate() {
                let id=ids.get(i).copied();let reference=p.reference.as_ref().and_then(|n|names.get(n)).copied();
                let initial=reference.and_then(|id|skel.tfs.get(&id)).cloned();
                let local=if i>0&&id.is_some() {
                    let previous_ref=pattern.params[i-1].reference.as_ref().and_then(|n|names.get(n)).copied().unwrap_or(ids[i-1]);
                    let inverse=mat_inv_pub(&world_of(previous_ref,&reference_pose.tfs,&mut reference_worlds));
                    let position=reference.map(|r|pos_of(&world_of(r,&reference_pose.tfs,&mut reference_worlds)))
                        .unwrap_or_else(||pos_of(&world_of(id.unwrap(),&skel.tfs,&mut source_worlds)));
                    mat_vec_local(&inverse,position)
                }else{[0.0;3]};
                let radius=old.params.radius;old.authored_radius=p.radius;old.reference=reference;old.initial=initial;old.local=local;old.params=p.clone();old.params.radius=radius;
            }
            chain.gravity=pattern.gravity;chain.end=pattern.end;scratch=Some(reference_pose);
        }
        runtime.reference_scratch=scratch;
    }
    #[cfg(test)]
    pub(crate) fn breast_references_oracle(&self, skel: &mut Skeleton, sex: i32, softness: f32, weight: f32, size: f32) -> BodyDynamicRuntime {
        self.runtime_impl(skel,sex,softness,weight,size,true,false)
    }
    fn runtime_impl(&self, skel: &mut Skeleton, sex: i32, softness: f32, weight: f32, size: f32,breast_only:bool,reuse_references:bool) -> BodyDynamicRuntime {
        let names: HashMap<_, _> = skel.tfs.iter().map(|(id, tf)| (tf.name.clone(), *id)).collect();
        let mut chains = Vec::new();
        let mut reference_scratch:Option<Skeleton>=None;
        let mut previous_reference_targets:Vec<i64>=Vec::new();
        let mut source_worlds=HashMap::new();
        let prepared_world=|id:i64,tfs:&HashMap<i64,Tf>,cache:&mut HashMap<i64,Mat4>| {
            if !reuse_references {cache.clear();}
            world_of(id,tfs,cache)
        };
        for c in &self.components {
            if breast_only&&!matches!(c.kind,Some(0|1)){continue;}
            if sex == 0 && matches!(c.kind, Some(2 | 3)) { continue; }
            let Some(&root) = names.get(&c.root) else { continue; };
            let Some(ids) = c.bones.iter().map(|n| names.get(n).copied()).collect::<Option<Vec<_>>>() else { continue; };
            // Awake always selects pattern 0; serialized PtnNo is not the initial pattern.
            let mut pattern = c.patterns[0].clone();
            if matches!(c.kind, Some(0 | 1)) {
                let rate = (softness * size + 0.01).clamp(0.0, 1.0);
                let tree = |v: [f32; 3]| if rate < 0.5 { v[0] + (v[1] - v[0]) * rate * 2.0 }
                    else { v[1] + (v[2] - v[1]) * (rate - 0.5) * 2.0 };
                for p in &mut pattern.params {
                    p.damping = tree([0.2, 0.1, 0.1]); p.elasticity = tree([0.2, 0.15, 0.05]);
                    p.stiffness = tree([1.0, 0.1, 0.01]);
                }
                pattern.gravity = [0.0, -0.005 * weight.clamp(0.0, 1.0) * size * softness * 0.5, 0.0];
            }
            let mut particles = Vec::new();
            let mut reference_pose = if reuse_references {reference_scratch.take().unwrap_or_else(||skel.clone())}else{skel.clone()};
            // Only these local transforms were changed by the previous chain.
            // All source/parent topology stays immutable during this refresh.
            for id in previous_reference_targets.drain(..) {
                if let (Some(target),Some(source))=(reference_pose.tfs.get_mut(&id),skel.tfs.get(&id)) {
                    target.pos=source.pos;target.rot=source.rot;target.scale=source.scale;
                }
            }
            for (id, p) in ids.iter().zip(&pattern.params) {
                if let Some(source) = p.reference.as_ref().and_then(|n| names.get(n)).and_then(|id| skel.tfs.get(id)) {
                    if let Some(target) = reference_pose.tfs.get_mut(id) {
                        target.pos = source.pos; target.rot = source.rot; target.scale = source.scale;
                        if reuse_references {previous_reference_targets.push(*id);}
                    }
                }
            }
            let mut reference_worlds=HashMap::new();
            for (i, p) in pattern.params.iter().enumerate() {
                let id = ids.get(i).copied();
                let reference = p.reference.as_ref().and_then(|n| names.get(n)).copied();
                let initial = reference.and_then(|id| skel.tfs.get(&id)).cloned();
                let pos = id.map(|id| pos_of(&prepared_world(id,&skel.tfs,&mut source_worlds))).unwrap_or_else(|| {
                    mat_vec_local(&prepared_world(*ids.last().unwrap(), &skel.tfs, &mut source_worlds), pattern.end)
                });
                let local = if i > 0 && id.is_some() {
                    let previous_ref = pattern.params[i - 1].reference.as_ref().and_then(|n| names.get(n)).copied().unwrap_or(ids[i - 1]);
                    mat_vec_local(&mat_inv_pub(&prepared_world(previous_ref, &reference_pose.tfs, &mut reference_worlds)),
                        reference.map(|r| pos_of(&prepared_world(r,&reference_pose.tfs,&mut reference_worlds))).unwrap_or(pos))
                } else { [0.0; 3] };
                particles.push(Node { id, reference, initial, local, pos, prev: pos, authored_radius:p.radius, params: p.clone() });
            }
            let colliders:Vec<_> = c.colliders.iter().filter_map(|nc| {
                let mut collider = nc.collider.clone(); collider.transform = *names.get(&nc.name)?; Some(collider)
            }).collect();
            chains.push(Chain { root, kind: c.kind, enabled:c.enabled, particles, end: pattern.end, gravity: pattern.gravity,
                force: c.force, update_rate: c.update_rate, reflect_speed: c.reflect_speed,
                max_steps: c.max_steps, elapsed: 0.0, previous: pos_of(&prepared_world(root,&skel.tfs,&mut source_worlds)), authored_colliders:colliders.len(), colliders, px_key: crate::px::PxKey::new() });
            if reuse_references {reference_scratch=Some(reference_pose);}
        }
        BodyDynamicRuntime { chains, suppress_skip_update: false, collider_flags: None, reference_scratch:None, px_batch: crate::px::PxKey::new() }
    }
}
#[derive(Clone)]
struct Node { id: Option<i64>, reference: Option<i64>, initial: Option<Tf>,
    local: [f32; 3], pos: [f32; 3], prev: [f32; 3], authored_radius:f32, params: Parameter }
#[derive(Clone)]
struct Chain { root: i64, kind: Option<usize>, enabled:bool, particles: Vec<Node>, end: [f32; 3], gravity: [f32; 3], force: [f32; 3],
    update_rate: f32, reflect_speed: f32, max_steps: usize, elapsed: f32, previous: [f32; 3], authored_colliders:usize, colliders: Vec<BodyCollider>, px_key: crate::px::PxKey }
#[derive(Clone)]
pub struct BodyDynamicRuntime { chains: Vec<Chain>, suppress_skip_update: bool, collider_flags: Option<super::game_colliders::Flags>, reference_scratch:Option<Skeleton>, px_batch: crate::px::PxKey }
impl BodyDynamicRuntime {
    pub(crate) fn set_actives(&mut self,skel:&mut Skeleton,active:[bool;4]) {
        for chain in &mut self.chains {
            let Some(next)=chain.kind.and_then(|kind|active.get(kind)).copied() else {continue;};
            if next==chain.enabled {continue;}
            if next { // OnEnable: reset particle history on the current pose, retain substep clock.
                chain.previous=parts_pos(&skel.tfs,chain.root);
                for i in 0..chain.particles.len() {
                    let position=chain.particles[i].id.map(|id|parts_pos(&skel.tfs,id)).unwrap_or_else(|| {
                        mat_vec_local(&world_of(chain.particles[i-1].id.unwrap(),&skel.tfs,&mut HashMap::new()),chain.end)
                    });
                    chain.particles[i].pos=position;chain.particles[i].prev=position;
                }
            } else {chain.init_transforms(skel);} // OnDisable restores live references once.
            chain.enabled=next;
        }
        skel.rebuild_worlds();
    }
    pub(crate) fn refresh_breast_references(&mut self,fresh:Self) {
        // ResetPosition rebuilds breast reference locals/parameters after shape
        // changes. It does not clear particle history or rebuild hip chains.
        for chain in &mut self.chains {
            if !matches!(chain.kind,Some(0|1)) {continue;}
            let Some(new)=fresh.chains.iter().find(|c|c.root==chain.root&&c.kind==chain.kind) else {continue;};
            if new.particles.len()!=chain.particles.len() {continue;}
            for (old,new) in chain.particles.iter_mut().zip(&new.particles) {
                let radius=old.params.radius; // Retain GameColliders' live radius override.
                old.authored_radius=new.authored_radius;old.reference=new.reference;old.initial=new.initial.clone();old.local=new.local;old.params=new.params.clone();old.params.radius=radius;
            }
            chain.gravity=new.gravity;chain.end=new.end;
        }
    }
    #[cfg(test)]
    pub(crate) fn particle_snapshot(&self)->String {format!("{:?}",self.chains.iter().map(|c|(c.elapsed,c.previous,c.enabled,c.particles.iter().map(|p|(p.pos,p.prev)).collect::<Vec<_>>())).collect::<Vec<_>>())}
    #[cfg(test)]
    pub(crate) fn reference_snapshot(&self)->String {format!("{:?}",self.chains.iter().map(|c|(c.root,c.kind,c.gravity,c.end,c.particles.iter().map(|p|(p.id,p.reference,&p.initial,p.local,p.authored_radius,&p.params)).collect::<Vec<_>>())).collect::<Vec<_>>())}
    pub fn suppress_skip_update(&mut self, value: bool) { self.suppress_skip_update = value; }
    pub fn collider_flags(&self) -> Option<super::game_colliders::Flags> { self.collider_flags }
    pub fn configure_colliders(&mut self, skel: &Skeleton, flags: super::game_colliders::Flags, size: f32) {
        self.collider_flags = Some(flags);
        let colliders = super::game_colliders::breast_colliders(skel, flags);
        for chain in &mut self.chains {
            if !matches!(chain.kind, Some(0 | 1)) { continue; }
            // UpdateBreastDB changes Params[0..1] for every pattern, not hips
            // or the final breast particle. Other authored colliders remain.
            chain.particles[0].params.radius = if flags.installed && flags.breast { 0.1 * size } else if flags.installed {0.0} else {chain.particles[0].authored_radius};
            chain.particles[1].params.radius = if flags.installed && flags.breast { 0.08 * size } else if flags.installed {0.0} else {chain.particles[1].authored_radius};
            // Replace only plugin colliders. Never accumulate them on scene switches.
            chain.colliders.truncate(chain.authored_colliders);
            if flags.installed {chain.colliders.extend(colliders.iter().cloned());}
        }
    }
    pub fn step(&mut self, skel: &mut Skeleton, dt: f32) {
        let dt = dt.max(0.0);
        if crate::px::batch_available() && crate::px::enabled() {
            // PhysX: all enabled chains prepared, solved in one batch (one scene, one simulate per substep), then applied.
            let mut plans = Vec::new();
            for (index, chain) in self.chains.iter_mut().enumerate() {
                if !chain.enabled { continue; }
                let (movement, steps, force, scale) = chain.prepare(skel, dt);
                let input = chain.px_input(skel, force, movement, scale, dt);
                plans.push((index, movement, steps, force, scale, input));
            }
            let solved = {
                let mut inputs: Vec<&mut crate::px::ChainInput> = plans.iter_mut().filter_map(|plan| plan.5.as_mut()).collect();
                !inputs.is_empty() && crate::px::solve_batch(&self.px_batch, &mut inputs)
            };
            for (index, movement, steps, force, scale, input) in plans {
                let chain = &mut self.chains[index];
                if let (true, Some(input)) = (solved, input.as_ref()) { for (p, out) in chain.particles.iter_mut().zip(&input.particles) { p.pos = out.pos; p.prev = out.prev; } }
                // PhysX owns the chain while it is on: never the built-in iterations (an unsolvable chain holds still).
                chain.finish(skel, movement, steps, force, scale, true, self.suppress_skip_update);
            }
        } else {
            let audit = crate::px::audit_enabled();
            let mut audits = Vec::new();
            for chain in &mut self.chains {
                if !chain.enabled { continue; }
                let (movement, steps, force, scale) = chain.prepare(skel, dt);
                chain.finish_audited(skel, movement, steps, force, scale, dt, false, self.suppress_skip_update, if audit { Some(&mut audits) } else { None });
            }
            if !audits.is_empty() { crate::px::audit_batch(&self.px_batch, &mut audits); }
        }
        skel.rebuild_worlds();
    }
}
impl Chain {
    /// PhysX replacement of the whole substep loop; see `crate::px`.
    fn px_solve(&mut self, skel: &Skeleton, force: [f32; 3], movement: [f32; 3], scale: f32, dt: f32) -> bool {
        let Some(mut input) = self.px_input(skel, force, movement, scale, dt) else { return false };
        if !crate::px::solve(&self.px_key, &mut input.particles, &input.colliders, input.gravity_step, input.dt, input.reference_step, input.velocity_scale) { return false; }
        for (p, out) in self.particles.iter_mut().zip(&input.particles) { p.pos = out.pos; p.prev = out.prev; }
        true
    }
    fn px_input(&self, skel: &Skeleton, force: [f32; 3], movement: [f32; 3], scale: f32, dt: f32) -> Option<crate::px::ChainInput> {
        if !dt.is_finite() || dt <= 0.0 || self.particles.len() < 2 { return None; }
        let mut input = Vec::with_capacity(self.particles.len());
        let mut cache = HashMap::new();
        for i in 0..self.particles.len() {
            let p = &self.particles[i];
            if i == 0 {
                let pos = p.id.map(|id| parts_pos(&skel.tfs, id)).unwrap_or(p.pos);
                input.push(crate::px::OkPxParticle { parent: -1, flags: crate::px::FLAG_ROOT | crate::px::FLAG_BODY, pos, prev: p.pos, ..Default::default() });
                continue;
            }
            let Some(parent_id) = self.particles[i - 1].id else { return None };
            let parent_w = world_of(parent_id, &skel.tfs, &mut cache);
            let rest_len = p.id.map(|id| vlen(vsub(pos_of(&parent_w), parts_pos(&skel.tfs, id))))
                .unwrap_or(vlen(self.end) * scale);
            let mut linear = parent_w;
            for row in 0..3 { linear[row][3] = 0.0; }
            let inertia = vscale(movement, p.params.inert);
            input.push(crate::px::OkPxParticle {
                parent: i as i32 - 1, flags: 0, radius: p.params.radius * scale, damping: p.params.damping,
                elasticity: p.params.elasticity, stiffness: p.params.stiffness, rest_len,
                min_fraction: p.params.keep_min, max_fraction: p.params.keep_max,
                pos: vadd(p.pos, inertia), prev: vadd(p.prev, inertia),
                goal_offset: mat_vec_local(&linear, if p.id.is_some() { p.local } else { self.end }), freeze_normal: [0.0; 3],
                body_radius: 0.0,
            });
        }
        let mut capsules = Vec::new();
        for collider in &self.colliders {
            let world = world_of(collider.transform, &skel.tfs, &mut cache);
            let scale_z = transform_lossy_scale(collider.transform, &skel.tfs, &world)[2].abs();
            capsules.extend(px_capsule(collider, &world, scale_z));
        }
        let reference_step = if self.update_rate > 0.0 { 1.0 / self.update_rate } else { 1.0 / 60.0 };
        Some(crate::px::ChainInput { key: self.px_key.id(), particles: input, colliders: capsules, gravity_step: force, dt, reference_step, velocity_scale: self.reflect_speed })
    }
    fn init_transforms(&self,skel:&mut Skeleton) {
        // Ver02 restores each chain in LateUpdate from live reference bones,
        // after Animator, shape UpdateAlways and plugin modifiers.
        for p in &self.particles {
            if let Some(id) = p.id {
                let source = p.reference.and_then(|r| skel.tfs.get(&r)).or(p.initial.as_ref()).cloned();
                if let (Some(source), Some(target)) = (source, skel.tfs.get_mut(&id)) {
                    target.pos = source.pos; target.rot = source.rot; target.scale = source.scale;
                }
            }
        }
    }
    fn step(&mut self, skel: &mut Skeleton, dt: f32, suppress_skip_update: bool) {
        let (movement, steps, force, scale) = self.prepare(skel, dt);
        let px_on = crate::px::enabled();
        if px_on { self.px_solve(skel, force, movement, scale, dt); }
        self.finish(skel, movement, steps, force, scale, px_on, suppress_skip_update);
    }
    fn prepare(&mut self, skel: &mut Skeleton, dt: f32) -> ([f32; 3], usize, [f32; 3], f32) {
        self.init_transforms(skel);
        let root_w = world_of(self.root, &skel.tfs, &mut HashMap::new());
        let scale = vlen([root_w[0][0], root_w[1][0], root_w[2][0]]);
        let movement = vsub(pos_of(&root_w), self.previous);
        self.previous = pos_of(&root_w);
        let steps = if self.update_rate > 0.0 {
            self.elapsed += dt; let period = 1.0 / self.update_rate; let mut count = 0;
            while self.elapsed >= period {
                self.elapsed -= period; count += 1;
                if count >= self.max_steps { self.elapsed = 0.0; break; }
            }
            count
        } else { 1 };
        let force = vscale(vadd(self.gravity, self.force), scale);
        (movement, steps, force, scale)
    }
    fn finish(&mut self, skel: &mut Skeleton, movement: [f32; 3], steps: usize, force: [f32; 3], scale: f32, px_done: bool, suppress_skip_update: bool) {
        self.finish_audited(skel, movement, steps, force, scale, 0.0, px_done, suppress_skip_update, None);
    }
    fn finish_audited(&mut self, skel: &mut Skeleton, movement: [f32; 3], steps: usize, force: [f32; 3], scale: f32, dt: f32, px_done: bool,
        suppress_skip_update: bool, audit: Option<&mut Vec<crate::px::ChainInput>>) {
        let mut movement = movement;
        let iterations = if px_done || (steps == 0 && suppress_skip_update) { 0 } else { steps.max(1) };
        for _ in 0..iterations {
            for i in 0..self.particles.len() {
                let p = &mut self.particles[i];
                if i == 0 {
                    p.prev = p.pos; p.pos = parts_pos(&skel.tfs, p.id.unwrap());
                } else {
                    let inert = vscale(movement, p.params.inert);
                    if steps > 0 {
                        let velocity = vscale(vsub(p.pos, p.prev), self.reflect_speed);
                        p.prev = vadd(p.pos, inert);
                        p.pos = vadd(p.pos, vadd(vscale(velocity, 1.0 - p.params.damping), vadd(force, inert)));
                    } else { p.prev = vadd(p.prev, inert); p.pos = vadd(p.pos, inert); }
                }
            }
            for i in 1..self.particles.len() {
                let parent = self.particles[i - 1].clone();
                let p = &mut self.particles[i];
                let parent_id = parent.id.unwrap();
                let parent_w = world_of(parent_id, &skel.tfs, &mut HashMap::new());
                let rest_len = p.id.map(|id| vlen(vsub(pos_of(&parent_w), parts_pos(&skel.tfs, id))))
                    .unwrap_or(vlen(self.end) * scale);
                let mut goal_matrix = parent_w;
                for axis in 0..3 { goal_matrix[axis][3] = parent.pos[axis]; }
                let goal = mat_vec_local(&goal_matrix, if p.id.is_some() { p.local } else { self.end });
                if steps > 0 { p.pos = vadd(p.pos, vscale(vsub(goal, p.pos), p.params.elasticity)); }
                if p.params.stiffness > 0.0 {
                    let diff = vsub(goal, p.pos); let distance = vlen(diff);
                    let limit = rest_len * (1.0 - p.params.stiffness) * 2.0;
                    if distance > limit { p.pos = vadd(p.pos, vscale(diff, (distance - limit) / distance)); }
                }
                if steps > 0 { for c in &self.colliders { collide_particle(&mut p.pos, p.params.radius * scale, c, skel); } }
                let diff = vsub(parent.pos, p.pos); let distance = vlen(diff);
                if distance > 0.0 {
                    let fraction = (distance - rest_len) / distance;
                    if fraction <= p.params.keep_min { p.pos = vadd(p.pos, vscale(diff, fraction - p.params.keep_min)); }
                    else if fraction >= p.params.keep_max { p.pos = vadd(p.pos, vscale(diff, fraction - p.params.keep_max)); }
                }
                if let Some(id) = p.id.filter(|_| p.params.move_limit) {
                    let mut matrix = world_of(id, &skel.tfs, &mut HashMap::new());
                    for axis in 0..3 { matrix[axis][3] = goal[axis]; }
                    let mut local = mat_vec_local(&mat_inv_pub(&matrix), p.pos);
                    for axis in 0..3 { local[axis] = local[axis].clamp(p.params.min[axis], p.params.max[axis]); }
                    p.pos = mat_vec_local(&matrix, local);
                }
            }
            movement = [0.0; 3];
        }
        if px_done {
            // The external solver (PhysX/Kawaii/Chaos) moved the particles; the card's hard limits still apply exactly as
            // in the built-in step: the keep range along the bone and the local move box (hip/crotch bones). Skipping them
            // let those bones leave their box and tore the meshes skinned across them (underwear split at the crotch).
            for i in 1..self.particles.len() {
                let parent = self.particles[i - 1].clone();
                let p = &mut self.particles[i];
                let parent_id = parent.id.unwrap();
                let parent_w = world_of(parent_id, &skel.tfs, &mut HashMap::new());
                let rest_len = p.id.map(|id| vlen(vsub(pos_of(&parent_w), parts_pos(&skel.tfs, id))))
                    .unwrap_or(vlen(self.end) * scale);
                let mut goal_matrix = parent_w;
                for axis in 0..3 { goal_matrix[axis][3] = parent.pos[axis]; }
                let goal = mat_vec_local(&goal_matrix, if p.id.is_some() { p.local } else { self.end });
                let diff = vsub(parent.pos, p.pos); let distance = vlen(diff);
                if distance > 0.0 {
                    let fraction = (distance - rest_len) / distance;
                    if fraction <= p.params.keep_min { p.pos = vadd(p.pos, vscale(diff, fraction - p.params.keep_min)); }
                    else if fraction >= p.params.keep_max { p.pos = vadd(p.pos, vscale(diff, fraction - p.params.keep_max)); }
                }
                if let Some(id) = p.id.filter(|_| p.params.move_limit) {
                    let mut matrix = world_of(id, &skel.tfs, &mut HashMap::new());
                    for axis in 0..3 { matrix[axis][3] = goal[axis]; }
                    let mut local = mat_vec_local(&mat_inv_pub(&matrix), p.pos);
                    for axis in 0..3 { local[axis] = local[axis].clamp(p.params.min[axis], p.params.max[axis]); }
                    let clamped = mat_vec_local(&matrix, local);
                    // Keep the velocity the solver gave it, relative to the clamped position.
                    p.prev = vadd(p.prev, vsub(clamped, p.pos));
                    p.pos = clamped;
                }
            }
        }
        if let Some(audits) = audit {
            // Same audit as PhysX: the built-in result, before the bones are rewritten.
            if let Some(input) = self.px_input(skel, force, [0.0; 3], scale, dt) { audits.push(input); }
        }
        let mut cache = HashMap::new();
        for i in 1..self.particles.len() {
            let parent = &self.particles[i - 1]; let p = &self.particles[i]; let pid = parent.id.unwrap();
            if parent.params.rotate {
                let direction = if p.id.is_some() { p.local } else { self.end };
                let rotation = world_rot_of(pid, &skel.tfs);
                let from = mul_dir(&trs([0.0; 3], rotation, [1.0; 3]), direction);
                let target = vsub(vsub(p.pos, vscale(from, 1.0 - parent.params.next_length)), parent.pos);
                set_world_rot(&mut skel.tfs, &mut cache, pid, quat_mul(quat_from_to(from, target), rotation));
            }
            if let Some(id) = p.id {
                if p.params.crush {
                    let local = mat_vec_local(&mat_inv_pub(&world_of(id, &skel.tfs, &mut cache)), p.pos);
                    let inverse_lerp = |a: f32, b: f32, value: f32| if a != b { ((value - a) / (b - a)).clamp(0.0, 1.0) } else { 0.0 };
                    let amount = if local[2] <= 0.0 { p.params.crush_add_min * (1.0 - inverse_lerp(p.params.crush_min, 0.0, local[2])) }
                        else { p.params.crush_add_max * inverse_lerp(0.0, p.params.crush_max, local[2]) };
                    if let Some(tf) = skel.tfs.get_mut(&id) {
                        let initial = p.initial.as_ref().map(|tf| tf.scale).unwrap_or([1.0; 3]);
                        tf.scale = [initial[0] + amount, initial[1] + amount, initial[2]];
                    }
                    cache.clear();
                }
                set_world_pos(&mut skel.tfs, &mut cache, id, p.pos);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn live_breast_reference_subset_matches_full_rebuild_history_and_pose() {
        let path=&crate::test_support::game_path("/abdata/chara/oo_base.unity3d");
        if !path.ok_exists(){return;}
        let parsed=crate::unity::parse_bundle_cached(crate::gamefs::read(path).unwrap());
        let mut source=parsed.skeleton_for_prefab_name("p_cf_body_bone").unwrap();let set=parsed.body_dynamics_for_skeleton(&source);
        let mut old=set.runtime(&mut source,1,0.85,0.4,0.75);let mut fast=old.clone();let mut direct=old.clone();
        assert!(old.chains.iter().any(|c|matches!(c.kind,Some(0|1))));
        for factor in [0.7f32,1.3,1.] {
            let mut shaped=source.clone();for tf in shaped.tfs.values_mut(){tf.pos=tf.pos.map(|v|v*factor);}shaped.rebuild_worlds();
            old.refresh_breast_references(set.runtime(&mut shaped,1,0.85,0.4,0.75));
            fast.refresh_breast_references(set.breast_references(&mut shaped,1,0.85,0.4,0.75));
            set.refresh_live_breast_references(&shaped,0.85,0.4,0.75,&mut direct);
            assert_eq!(old.reference_snapshot(),direct.reference_snapshot());
            let mut a=shaped.clone();let mut b=shaped.clone();
            let mut c=shaped.clone();
            for _ in 0..12 {old.step(&mut a,1./120.);fast.step(&mut b,1./120.);direct.step(&mut c,1./120.);assert_eq!(a.worlds,b.worlds);assert_eq!(a.worlds,c.worlds);assert_eq!(old.particle_snapshot(),fast.particle_snapshot());assert_eq!(old.particle_snapshot(),direct.particle_snapshot());}
        }
    }

    fn tuple<const N: usize>(s: &str) -> [f32; N] {
        let v: Vec<f32> = s.trim_matches(|c| c == '(' || c == ')').split(',')
            .map(|s| s.trim().parse().unwrap()).collect();
        v.try_into().unwrap()
    }
    fn apply_dump(skel: &mut Skeleton, path: &str) {
        for line in crate::gamefs::read_to_string(path).unwrap().lines() {
            let f: Vec<_> = line.split('\t').collect();
            if f.len() != 6 || f[0] != "all_transform" || !f[2].contains("/p_cf_body_bone/") && f[1] != "p_cf_body_bone" { continue; }
            if let Some(tf) = skel.tfs.values_mut().find(|tf| tf.name == f[1]) {
                tf.pos = tuple(f[3]); tf.rot = tuple(f[4]); tf.scale = tuple(f[5]);
            }
        }
        skel.rebuild_worlds();
    }

    #[test]
    fn collider_reconfiguration_preserves_history_without_duplicates() {
        let path=&crate::test_support::game_path("/abdata/chara/oo_base.unity3d");
        if !path.ok_exists(){return;}
        let parsed=crate::unity::parse_bundle_cached(crate::gamefs::read(path).unwrap());
        let mut sk=parsed.skeleton_for_prefab_name("p_cf_body_bone").unwrap();
        let set=parsed.body_dynamics_for_skeleton(&sk);let mut runtime=set.runtime(&mut sk,1,0.85,0.,0.75);
        let baseline=runtime.clone();let history=runtime.particle_snapshot();
        let flags=super::super::game_colliders::Flags{installed:true,breast:true,floor:true,skirt:true};
        runtime.configure_colliders(&sk,flags,0.75);
        let counts=runtime.chains.iter().map(|c|c.colliders.len()).collect::<Vec<_>>();
        for _ in 0..8 {runtime.configure_colliders(&sk,flags,0.75);assert_eq!(runtime.chains.iter().map(|c|c.colliders.len()).collect::<Vec<_>>(),counts);}
        runtime.configure_colliders(&sk,Default::default(),0.75);
        assert_eq!(runtime.particle_snapshot(),history);
        for (a,b) in runtime.chains.iter().zip(&baseline.chains) {
            assert_eq!(a.colliders.len(),b.colliders.len());
            assert_eq!(a.particles.iter().map(|p|p.params.radius).collect::<Vec<_>>(),b.particles.iter().map(|p|p.params.radius).collect::<Vec<_>>());
        }
        eprintln!("OK body colliders rebind exact=1 switches=10 no_duplicates=1 authored_restored=1");
    }

    #[test]
    #[ignore = "requires local KKS bundle and controlled game probe output"]
    fn ver02_game_frozen_body_chains() {
        let parsed = crate::unity::parse_bundle_cached(crate::gamefs::read(crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d")).unwrap());
        let mut skel = parsed.skeleton_for_prefab_name("p_cf_body_bone").unwrap();
        let set = parsed.body_dynamics_for_skeleton(&skel);
        assert_eq!(set.len(), 4);
        let log = std::env::var("OK_TEST_BODY_FIXTURE").unwrap_or_else(|_| "../../Saved/Logs/BodyVer02SetupGame.json".into());
        apply_dump(&mut skel, &format!("{log}.physics-baseline.tsv"));
        let baseline = skel.clone();
        let mut runtime = set.runtime(&mut skel, 1, 0.85, 0.0, 0.75);
        // Use live setup's captured LocalPosition/InitLocalScale to isolate
        // the numerical algorithm from game factory/ABMX initialization.
        let live = crate::gamefs::read_to_string(format!("{log}.physics-setup.tsv")).unwrap();
        runtime.suppress_skip_update(live.lines().any(|line| line.starts_with("skip_patch_owner\tDynamicBone_Ver02\t")));
        let generated = live.lines().any(|line| line.starts_with("collider\t") && line.contains("/KKS_Colliders_"));
        if generated {
            runtime.configure_colliders(&skel, super::super::game_colliders::Flags {
                installed: true, breast: true, floor: true, skirt: true }, 0.75);
        }
        let mut current = None;
        let mut dt = 1.0 / 60.0;
        let mut movement = 0.0;
        for line in live.lines() {
            let f: Vec<_> = line.split('\t').collect();
            if f[0] == "simulation_settings" { dt = f[1].parse().unwrap(); movement = f[2].parse().unwrap(); }
            else if f[0] == "component" {
                let bones: Vec<_> = live.lines().filter_map(|line| {
                    let v: Vec<_> = line.split('\t').collect();
                    (v.len() == 3 && v[0] == "bone" && v[1] == f[2]).then(|| v[2].rsplit('/').next().unwrap().to_string())
                }).collect();
                current = runtime.chains.iter().position(|c| c.particles.iter().filter_map(|p| p.id.map(|id| skel.tfs[&id].name.clone())).collect::<Vec<_>>() == bones);
                if let Some(i) = current {
                    runtime.chains[i].gravity = tuple(f[9]);
                    let live_colliders: Vec<_> = live.lines().filter(|line| line.starts_with(&format!("collider\t{}\t", f[2]))).collect();
                    assert_eq!(runtime.chains[i].colliders.len(), live_colliders.len());
                    for (c, row) in runtime.chains[i].colliders.iter().zip(live_colliders) {
                        let v: Vec<_> = row.split('\t').collect();
                        assert_eq!(c.radius, v[3].parse::<f32>().unwrap());
                        assert_eq!(c.height, v[4].parse::<f32>().unwrap());
                        assert_eq!(c.center, tuple::<3>(v[5]));
                        assert_eq!(c.direction, v[6].parse::<usize>().unwrap());
                        assert_eq!(c.inside, v[7] == "1");
                        assert_eq!(skel.tfs[&c.transform].name, v[2].split('/').rev().nth(1).unwrap());
                    }
                }
            } else if f[0] == "particle" {
                let Some(c) = current else { continue; };
                let i: usize = f[2].parse().unwrap(); let node = &mut runtime.chains[c].particles[i];
                match f[3] {
                    "LocalPosition" => node.local = tuple(f[4]),
                    "InitLocalScale" => if let Some(tf) = &mut node.initial { tf.scale = tuple(f[4]); },
                    "Damping" => node.params.damping = f[4].parse().unwrap(),
                    "Elasticity" => node.params.elasticity = f[4].parse().unwrap(),
                    "Stiffness" => node.params.stiffness = f[4].parse().unwrap(),
                    "Radius" => assert!((node.params.radius - f[4].parse::<f32>().unwrap()).abs() < 1e-6),
                    _ => {},
                }
            } else if f[0] == "component_state" {
                if let Some(c) = current { match f[2] {
                    "UpdateTime" => runtime.chains[c].elapsed = f[3].parse().unwrap(),
                    "ObjectPrevPosition" => runtime.chains[c].previous = tuple(f[3]),
                    _ => {},
                }}
            }
        }
        let mut expected = baseline.clone();
        apply_dump(&mut expected, &format!("{log}.bones.tsv"));
        let root = skel.tfs.iter().find(|(_, tf)| tf.name == "p_cf_body_bone").map(|(id, _)| *id).unwrap();
        let mut without_colliders = runtime.clone();
        for chain in &mut without_colliders.chains { chain.colliders.clear(); }
        let mut uncollided = baseline.clone();
        for frame in 0..240 {
            skel = baseline.clone();
            skel.tfs.get_mut(&root).unwrap().pos[0] += movement * (frame + 1) as f32;
            runtime.step(&mut skel, dt);
            if generated {
                uncollided = baseline.clone();
                uncollided.tfs.get_mut(&root).unwrap().pos[0] += movement * (frame + 1) as f32;
                without_colliders.step(&mut uncollided, dt);
            }
        }
        if generated {
            let displacement = runtime.chains.iter().flat_map(|c| c.particles.iter()).filter_map(|p| p.id)
                .map(|id| vlen(vsub(skel.tfs[&id].pos, uncollided.tfs[&id].pos))).fold(0.0f32, f32::max);
            eprintln!("Ver02 collider contact displacement relative to no collision: {displacement:.8} m");
            assert!(displacement > 1e-4, "contact fixture must actually exercise collisions");
        }
        let mut max_pos = 0.0f32;
        let mut max_rotation = 0.0f32;
        let mut max_scale = 0.0f32;
        for c in &runtime.chains { for p in &c.particles {
            let Some(id) = p.id else { continue; };
            let (a, b) = (&skel.tfs[&id], &expected.tfs[&id]);
            let pos = vlen(vsub(a.pos, b.pos));
            let rotation = vlen(vsub([a.rot[0], a.rot[1], a.rot[2]], [b.rot[0], b.rot[1], b.rot[2]]));
            max_pos = max_pos.max(pos); max_rotation = max_rotation.max(rotation);
            max_scale = max_scale.max(vlen(vsub(a.scale, b.scale)));
            eprintln!("Ver02 game {} local position {pos:.8} m, rotation {rotation:.8}", a.name);
        }}
        assert!(max_pos < 2e-6 && max_rotation < 2e-5 && max_scale < 2e-6,
            "Ver02 controlled game differs: pos={max_pos}, rot={max_rotation}, scale={max_scale}");
    }

    fn fixture() -> (Skeleton, BodyDynamicSet) {
        let tfs = [(1, "root", None, [0.0; 3]), (2, "tip", Some(1), [0.0, 1.0, 0.0]),
            (3, "tip_reference", Some(1), [0.0, 1.0, 0.0])].into_iter().map(|(id, name, parent, pos)|
            (id, Tf { sibling_index: None, name: name.into(), parent, pos, rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] })).collect();
        let skel = Skeleton { names: vec!["root".into(), "tip".into()], worlds: vec![], rest: vec![],
            smr_count: 2, tfs, bone_ids: vec![1, 2], source_mesh: 0 };
        let params = vec![Parameter { reference: Some("root".into()), ..Parameter::default() },
            Parameter { reference: Some("tip_reference".into()), elasticity: 0.2, ..Parameter::default() },
            Parameter::default()];
        let set = BodyDynamicSet { components: vec![Component { root: "root".into(), bones: vec!["root".into(), "tip".into()],
            patterns: vec![Pattern { gravity: [0.1, 0.0, 0.0], end: [0.0; 3], params }], enabled: true,
            kind: Some(2), update_rate: 60.0, reflect_speed: 1.0, max_steps: 3, force: [0.0; 3], colliders: vec![] }] };
        (skel, set)
    }

    #[test]
    fn ver02_source_one_frame_and_sex_branch() {
        let (mut sk, set) = fixture();
        assert!(set.runtime(&mut sk, 0, 0.5, 0.5, 0.5).chains.is_empty(), "male hips disabled by preparation kind");
        let mut runtime = set.runtime(&mut sk, 1, 0.5, 0.5, 0.5);
        sk.tfs.get_mut(&2).unwrap().pos = [5.0, 1.0, 0.0]; // stale animation must restore from RefTransform
        runtime.step(&mut sk, 1.0 / 60.0);
        let length = (0.08f32 * 0.08 + 1.0).sqrt();
        let expected = [0.08 / length, 1.0 / length, 0.0];
        assert!(vlen(vsub(sk.tfs[&2].pos, expected)) < 1e-6);
        assert!(sk.tfs[&2].pos[0] > 0.07, "Ver02 does not cancel gravity along a root local direction");
    }

    #[test]
    #[ignore="five installed cards; source OnEnable/OnDisable trace"]
    fn ver02_game_enable_transition_trace() {
        let parsed=crate::unity::parse_bundle_cached(crate::gamefs::read(crate::test_support::game_path("/abdata/chara/oo_base.unity3d")).unwrap());
        let mut global=0f32;let mut samples=0;let mut frames=0;
        for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
            if std::env::var("OK_TEST_YURE_MARCH").is_ok()&&card!="[Card] March_7th_keiyuu" {continue;}
            let stem:String=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_' {c}else{'_'}).collect();
            let prefix=format!("C:/portable/OpenKoikatsu/Saved/Tests/Studio294/{stem}Studio294.json");
            let mut sk=parsed.skeleton_for_prefab_name("p_cf_body_bone").unwrap();let set=parsed.body_dynamics_for_skeleton(&sk);
            apply_dump(&mut sk,&format!("{prefix}.yure-baseline.tsv"));let baseline=sk.clone();
            let live=crate::gamefs::read_to_string(format!("{prefix}.yure-setup.tsv")).unwrap();
            let settings:Vec<_>=live.lines().find(|l|l.starts_with("body_settings\t")).unwrap().split('\t').collect();
            let mut runtime=set.runtime(&mut sk,1,settings[2].parse().unwrap(),settings[3].parse().unwrap(),0.5);
            runtime.suppress_skip_update(live.lines().any(|l|l.starts_with("skip_patch_owner\tDynamicBone_Ver02\t")));
            let generated=live.lines().any(|l|l.starts_with("collider\t")&&l.contains("/KKS_Colliders_"));
            if generated {runtime.configure_colliders(&sk,super::super::game_colliders::Flags{installed:true,breast:true,floor:true,skirt:true},0.5);}
            let mut current=None;
            for line in live.lines() {
                let f:Vec<_>=line.split('\t').collect();
                if f[0]=="component" {
                    let bones:Vec<_>=live.lines().filter_map(|l|{let p:Vec<_>=l.split('\t').collect();(p.len()==3&&p[0]=="bone"&&p[1]==f[2]).then(||p[2].rsplit('/').next().unwrap())}).collect();
                    current=runtime.chains.iter().position(|c|c.particles.iter().filter_map(|p|p.id.map(|id|sk.tfs[&id].name.as_str())).collect::<Vec<_>>()==bones);
                    let c=&mut runtime.chains[current.expect("source chain identity")];c.gravity=tuple(f[9]);
                    let colliders:Vec<_>=live.lines().filter(|l|l.starts_with(&format!("collider\t{}\t",f[2]))).collect();
                    assert_eq!(c.colliders.len(),colliders.len());
                    for (c,line) in c.colliders.iter_mut().zip(colliders) {
                        let p:Vec<_>=line.split('\t').collect();c.radius=p[3].parse().unwrap();c.height=p[4].parse().unwrap();c.center=tuple(p[5]);
                        assert_eq!(c.direction,p[6].parse::<usize>().unwrap());assert_eq!(c.inside,p[7]=="1");
                        assert_eq!(sk.tfs[&c.transform].name,p[2].split('/').rev().nth(1).unwrap());
                    }
                } else if f[0]=="particle" {
                    let c=current.unwrap();let i:usize=f[2].parse().unwrap();let node=&mut runtime.chains[c].particles[i];
                    match f[3] {"LocalPosition"=>node.local=tuple(f[4]),"Position"=>node.pos=tuple(f[4]),"PrevPosition"=>node.prev=tuple(f[4]),
                        "InitLocalScale"=>if let Some(t)=&mut node.initial {t.scale=tuple(f[4]);},
                        "Damping"=>node.params.damping=f[4].parse().unwrap(),"Elasticity"=>node.params.elasticity=f[4].parse().unwrap(),
                        "Stiffness"=>node.params.stiffness=f[4].parse().unwrap(),"Radius"=>node.params.radius=f[4].parse().unwrap(),_=>{}}
                } else if f[0]=="component_state" {
                    let c=&mut runtime.chains[current.unwrap()];match f[2] {"UpdateTime"=>c.elapsed=f[3].parse().unwrap(),"ObjectPrevPosition"=>c.previous=tuple(f[3]),_=>{}}
                }
            }
            let native:Vec<serde_json::Value>=serde_json::from_slice(&crate::gamefs::read(format!("{prefix}.yure-physics.json")).unwrap()).unwrap();
            assert_eq!(native.len(),240);let root=sk.tfs.iter().find(|(_,t)|t.name=="p_cf_body_bone").map(|(id,_)|*id).unwrap();let mut max=0f32;
            for (i,f) in native.iter().enumerate() {
                assert_eq!(f["frame"].as_u64().unwrap(),i as u64);sk=baseline.clone();sk.tfs.get_mut(&root).unwrap().pos[0]+=0.001*(i+1) as f32;
                let active=std::array::from_fn(|k|f["active"][k].as_bool().unwrap());runtime.set_actives(&mut sk,active);runtime.step(&mut sk,1./60.);
                assert_eq!(f["bones"].as_array().unwrap().len(),10);
                for bone in f["bones"].as_array().unwrap() {
                    let name=bone["name"].as_str().unwrap();let t=sk.tfs.values().find(|t|t.name==name).unwrap();
                    for k in 0..3 {
                        let key=["x","y","z"][k];max=max.max((t.pos[k]-bone["position"][key].as_f64().unwrap() as f32).abs());
                        max=max.max((t.scale[k]-bone["scale"][key].as_f64().unwrap() as f32).abs());
                    }
                    let q:[f32;4]=std::array::from_fn(|k|bone["rotation"][["x","y","z","w"][k]].as_f64().unwrap() as f32);
                    let direct=(0..4).map(|k|(t.rot[k]-q[k]).abs()).fold(0f32,f32::max);let opposite=(0..4).map(|k|(t.rot[k]+q[k]).abs()).fold(0f32,f32::max);max=max.max(direct.min(opposite));
                    assert!(max<2e-5,"Ver02 enable {card} frame={i} bone={name} max={max} actual={:?}/{:?}",t.pos,t.rot);samples+=1;
                }
                frames+=1;
            }
            global=global.max(max);eprintln!("Ver02 enable card={card} frames=240 bones=2400 max={max}");
        }
        eprintln!("Ver02 enable accepted frames={frames} bones={samples} max={global}");
    }

    #[test]
    fn ver02_enable_transitions_keep_clock_and_disabled_animator_pose() {
        let (mut sk,set)=fixture();let mut runtime=set.runtime(&mut sk,1,0.5,0.5,0.5);
        runtime.step(&mut sk,0.025);
        let clock=runtime.chains[0].elapsed;
        runtime.set_actives(&mut sk,[true,true,false,true]);
        assert_eq!(sk.tfs[&2].pos,[0.,1.,0.],"OnDisable restores live reference once");
        sk.tfs.get_mut(&2).unwrap().pos=[3.,1.,0.];
        let history=runtime.chains[0].particles[1].pos;
        runtime.step(&mut sk,1.);
        assert_eq!(sk.tfs[&2].pos,[3.,1.,0.],"disabled chain must retain Animator locals");
        assert_eq!(runtime.chains[0].particles[1].pos,history);
        assert_eq!(runtime.chains[0].elapsed,clock);
        runtime.set_actives(&mut sk,[true;4]);
        assert_eq!(runtime.chains[0].particles[1].pos,[3.,1.,0.]);
        assert_eq!(runtime.chains[0].particles[1].prev,[3.,1.,0.]);
        assert_eq!(runtime.chains[0].elapsed,clock,"source OnEnable does not reset UpdateTime");
    }

    #[test]
    fn ver02_move_limit_crush_and_substep_clock() {
        let (mut sk, mut set) = fixture();
        let p = &mut set.components[0].patterns[0].params[1];
        p.move_limit = true; p.min = [0.0, -0.1, -0.1]; p.max = [0.03, 0.1, 0.1];
        p.keep_min = -1.0; p.keep_max = 1.0; p.crush = true;
        p.crush_min = -0.1; p.crush_add_min = 0.2; p.crush_max = 0.1; p.crush_add_max = -0.1;
        set.components[0].patterns[0].gravity = [0.1, 0.0, -0.0625];
        let mut runtime = set.runtime(&mut sk, 1, 0.5, 0.5, 0.5);
        let baseline = sk.clone();
        runtime.step(&mut sk, 1.0 / 60.0);
        assert!((sk.tfs[&2].pos[0] - 0.03).abs() < 1e-6);
        assert!((sk.tfs[&2].pos[2] + 0.05).abs() < 1e-6);
        assert!((sk.tfs[&2].scale[0] - 1.1).abs() < 1e-6);
        let mut direct = set.runtime(&mut baseline.clone(), 1, 0.5, 0.5, 0.5);
        let mut capped = set.runtime(&mut baseline.clone(), 1, 0.5, 0.5, 0.5);
        let mut a = baseline.clone(); let mut b = baseline.clone();
        direct.step(&mut a, 3.0 / 60.0); capped.step(&mut b, 1.0);
        assert!(vlen(vsub(a.tfs[&2].pos, b.tfs[&2].pos)) < 1e-6, "source HeavyLoopMaxCount caps elapsed time");
    }

    #[test]
    fn ver02_pattern_references_are_not_particle_parent_indices() {
        let (mut sk, mut set) = fixture();
        sk.tfs.insert(4, Tf { sibling_index: None, name: "tip_parent".into(), parent: Some(1), pos: [0.0, 0.3, 0.0],
            rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
        sk.tfs.get_mut(&2).unwrap().parent = Some(4);
        sk.tfs.get_mut(&3).unwrap().parent = Some(4);
        set.components[0].patterns[0].gravity = [0.0; 3];
        let mut runtime = set.runtime(&mut sk, 1, 0.5, 0.5, 0.5);
        assert!((runtime.chains[0].particles[1].local[1] - 1.3).abs() < 1e-6);
        runtime.step(&mut sk, 1.0 / 60.0);
        assert!((sk.tfs[&2].pos[1] - 1.0).abs() < 1e-6);
    }
}

/// One enabled Ver02 chain as the GPU rig simulates it (gpu_rig/physics.rs, Shaders/GpuRig.usf PhysicsCS): the particles
/// in order (each the previous one's child; the last an end point) and what Chain::step reads besides the transforms.
#[derive(Clone, Debug)]
pub(crate) struct Ver02ChainView {
    pub root: i64, pub end: [f32; 3], pub gravity: [f32; 3], pub force: [f32; 3],
    pub update_rate: f32, pub reflect_speed: f32, pub max_steps: usize, pub suppress_skip: bool,
    pub particles: Vec<Ver02ParticleView>,
    pub colliders: Vec<GpuColliderView>,
}
#[derive(Clone, Debug)]
pub(crate) struct Ver02ParticleView {
    pub id: Option<i64>, pub reference: Option<i64>, pub initial: Option<Tf>, pub local: [f32; 3],
    pub rotate: bool, pub damping: f32, pub elasticity: f32, pub stiffness: f32, pub inert: f32, pub next_length: f32, pub radius: f32,
    pub move_limit: bool, pub min: [f32; 3], pub max: [f32; 3], pub keep_min: f32, pub keep_max: f32,
    pub crush: bool, pub crush_min: f32, pub crush_max: f32, pub crush_add_min: f32, pub crush_add_max: f32,
}
impl BodyDynamicRuntime {
    /// The enabled chains as the GPU rig reads them (configured colliders and radii as the last step used them).
    pub(crate) fn gpu_view(&self) -> Vec<Ver02ChainView> {
        self.chains.iter().filter(|c| c.enabled && c.particles.len() >= 2).map(|c| Ver02ChainView {
            root: c.root, end: c.end, gravity: c.gravity, force: c.force, update_rate: c.update_rate, reflect_speed: c.reflect_speed,
            max_steps: c.max_steps, suppress_skip: self.suppress_skip_update,
            particles: c.particles.iter().map(|p| { let q = &p.params; Ver02ParticleView { id: p.id, reference: p.reference, initial: p.initial.clone(), local: p.local,
                rotate: q.rotate, damping: q.damping, elasticity: q.elasticity, stiffness: q.stiffness, inert: q.inert, next_length: q.next_length, radius: q.radius,
                move_limit: q.move_limit, min: q.min, max: q.max, keep_min: q.keep_min, keep_max: q.keep_max,
                crush: q.crush, crush_min: q.crush_min, crush_max: q.crush_max, crush_add_min: q.crush_add_min, crush_add_max: q.crush_add_max } }).collect(),
            colliders: c.colliders.iter().map(|col| GpuColliderView { transform: col.transform, in_body: true, center: col.center, radius: col.radius,
                height: col.height, direction: col.direction, inside: col.inside }).collect(),
        }).collect()
    }
}
