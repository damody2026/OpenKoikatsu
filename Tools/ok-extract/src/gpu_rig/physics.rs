//! The character's DynamicBone chains for the GPU rig's verlet pass (Shaders/GpuRig.usf PhysicsCS): her body's Ver02
//! chains (breasts, hips: dbone/ver02.rs, on body bones, each starting from its reference bone's local), the clothes set
//! (chains on her body bones, written back to them) and every accessory's own set (a private skeleton hung on a body
//! bone), as the session steps them (session.rs update_clothes_dynamics, update_dynamic_accessories_filtered; dbone.rs
//! AttachedDynamicSet::step_impl, the built-in solver: no PhysX).
//!
//! Every transform a chain reads or writes becomes a node after the rig's bones: an alias of a body bone (its local is
//! the animated one; the solved world goes back to the body bone), a private bone with its prefab local, or the top of a
//! private skeleton fixed to the body bone that carries it (found over the samples, as the palettes are).

use super::export::FlatRig;
use crate::pose::Mat4;
use std::collections::HashMap;

/// Where a node's parent world comes from.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Parent { None, Rig(usize), Node(usize) }

#[derive(Clone, Debug)]
pub struct PhysNode {
    pub parent: Parent,
    /// Alias of this rig bone: its local is the animated one and its solved world is written back to the bone.
    pub alias: Option<usize>,
    /// An alias whose every step starts from another bone's animated local (a Ver02 particle's reference bone).
    pub local_from: Option<usize>,
    /// Fixed local (private bones; a particle bone's prefab local, which every step starts from).
    pub pos: [f32; 3], pub rot: [f32; 4], pub scale: [f32; 3],
    /// The top of a private skeleton: world = parent world x matrix, Transform.rotation = parent rotation x rot_offset.
    pub matrix: Option<Mat4>, pub rot_offset: [f32; 4],
}

/// A transform as a chain refers to it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Ref { Rig(usize), Node(usize) }

#[derive(Clone, Debug)]
pub struct PhysParticle {
    pub node: Option<usize>, pub parent: i32,
    pub end_offset: [f32; 3], pub local_position: [f32; 3],
    pub damping: f32, pub elasticity: f32, pub stiffness: f32, pub inert: f32, pub radius: f32, pub add_angle_scale: f32,
    pub turns_parent: bool,
    /// A Ver02 particle (local_position is its rest offset from the parent).
    pub v02: Option<V02Particle>,
}
#[derive(Clone, Debug, Default)]
pub struct V02Particle {
    pub rotate: bool, pub next_length: f32, pub keep_min: f32, pub keep_max: f32,
    pub move_limit: bool, pub min: [f32; 3], pub max: [f32; 3],
    pub crush: bool, pub crush_min: f32, pub crush_max: f32, pub crush_add_min: f32, pub crush_add_max: f32, pub initial_scale: [f32; 3],
}

#[derive(Clone, Debug)]
pub struct PhysCollider { pub at: Ref, pub center: [f32; 3], pub radius: f32, pub height: f32, pub direction: u32, pub inside: bool }

#[derive(Clone, Debug)]
pub struct PhysChain {
    pub root: usize, pub component: usize, pub ver01: bool, pub suppress_skip: bool, pub freeze_axis: u32, pub mirror: u32,
    pub gravity: [f32; 3], pub force: [f32; 3], pub local_gravity: [f32; 3], pub update_rate: f32,
    pub particles: Vec<PhysParticle>, pub colliders: Vec<PhysCollider>,
    /// A Ver02 chain: (end offset, reflect speed, max steps).
    pub v02: Option<([f32; 3], f32, u32)>,
}

/// Which session set a node's actual world is read from (for the palette factorization).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SetRef { Body, Clothes, Part(usize) }

#[derive(Clone, Debug, Default)]
pub struct Physics {
    pub nodes: Vec<PhysNode>,
    /// Per node: the set and the set skeleton's id (the session's world of it, sample by sample).
    pub sources: Vec<(SetRef, i64)>,
    /// (set, id) -> node.
    pub index: HashMap<(SetRef, i64), usize>,
    pub chains: Vec<PhysChain>,
    /// Rig bones under an alias node that are not nodes: their worlds follow the solved aliases (rig order).
    pub follow: Vec<usize>,
    /// Private tops that no body bone carries exactly (> 0.1 mm over the samples).
    pub loose_tops: usize,
    /// The clothes set's nodes by bone name (a coordinated implant's palette follows the clothes bone of its key).
    pub clothes_by_name: HashMap<String, usize>,
    /// Per set, in stepping order: (kind: 2 body, 1 clothes, 0 accessory; node first, node count, chain first, chain
    /// count). Sets do not share nodes: the GPU steps each on its own thread, body then clothes (both move body bones)
    /// then the accessories hung on them.
    pub ranges: Vec<(u32, usize, usize, usize, usize)>,
}

fn set_of(s: &crate::session::OkSession, set: SetRef) -> Option<&crate::dbone::AttachedDynamicSet> {
    match set {
        SetRef::Body => None,
        SetRef::Clothes => s.clothes_dynamic_runtime.as_ref().map(|(_, state)| state),
        SetRef::Part(p) => s.parts.get(p)?.runtime_accessory.as_ref()?.dynamic_state(),
    }
}

/// The sets in the order the session steps them: clothes, then the accessories in part order.
pub fn sets(s: &crate::session::OkSession) -> Vec<SetRef> {
    let mut out = Vec::new();
    if s.clothes_dynamic_runtime.is_some() { out.push(SetRef::Clothes); }
    for p in 0..s.parts.len() { if set_of(s, SetRef::Part(p)).is_some() { out.push(SetRef::Part(p)); } }
    out
}

/// The nodes' worlds at one sample (Unity space, as the rig's worlds).
pub fn node_worlds(physics: &Physics, sample: &HashMap<(SetRef, i64), Mat4>) -> Vec<Mat4> {
    physics.sources.iter().map(|key| sample.get(key).copied().unwrap_or_else(crate::pose::ident)).collect()
}

fn quat_mul(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
    [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
     a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]]
}
fn conj(q: [f32; 4]) -> [f32; 4] { [-q[0], -q[1], -q[2], q[3]] }
/// Unity Transform.rotation: the local rotations composed up the parents.
fn world_rot(id: i64, tfs: &HashMap<i64, crate::pose::Tf>) -> [f32; 4] {
    let mut chain = Vec::new();
    let mut node = Some(id);
    while let Some(n) = node {
        let Some(tf) = tfs.get(&n) else { break };
        chain.push(tf.rot);
        node = tf.parent.filter(|p| *p != 0 && *p != n);
        if chain.len() > 512 { break; }
    }
    chain.iter().rev().fold([0., 0., 0., 1.], |acc, r| quat_mul(acc, *r))
}

impl Physics {
    /// Built from the session as it stands (after the sampling frames), `rig_worlds[k]` / `set_worlds` taken at the same
    /// samples the palettes use: a private top hangs on the rig bone that carries it best over them.
    pub fn build(s: &crate::session::OkSession, rig: &FlatRig, taken: &super::export::Samples) -> Self {
        let mut out = Physics::default();
        let rig_index: HashMap<i64, usize> = rig.ids.iter().enumerate().map(|(n, id)| (*id, n)).collect();
        let body = s.body_skel.as_ref();
        let mut aliased: Vec<usize> = Vec::new();
        // Her body's Ver02 chains first (session finish_paired_dynamics: body, clothes, accessories).
        if let (Some(runtime), Some(body)) = (s.body_dynamic_runtime.as_ref(), body) {
            let chains = runtime.gpu_view();
            let node_first = out.nodes.len();
            let mut node_of: HashMap<i64, usize> = HashMap::new();
            let mut ids: Vec<i64> = Vec::new();
            for c in &chains { for id in std::iter::once(c.root).chain(c.particles.iter().filter_map(|p| p.id)) { if rig_index.contains_key(&id) && !ids.contains(&id) { ids.push(id); } } }
            ids.sort_by_key(|id| rig_index[id]);
            let reference: HashMap<i64, i64> = chains.iter().flat_map(|c| c.particles.iter().filter_map(|p| Some((p.id?, p.reference?)))).collect();
            for id in &ids {
                let r = rig_index[id];
                let parent = match rig.parent[r] { p if p < 0 => Parent::None, p => match node_of.get(&rig.ids[p as usize]) { Some(n) => Parent::Node(*n), None => Parent::Rig(p as usize) } };
                let tf = &body.tfs[id];
                out.index.insert((SetRef::Body, *id), out.nodes.len());
                node_of.insert(*id, out.nodes.len());
                out.nodes.push(PhysNode { parent, alias: Some(r), local_from: reference.get(id).and_then(|q| rig_index.get(q)).copied(),
                    pos: tf.pos, rot: tf.rot, scale: tf.scale, matrix: None, rot_offset: [0., 0., 0., 1.] });
                out.sources.push((SetRef::Body, *id));
                aliased.push(r);
            }
            let chain_first = out.chains.len();
            for c in chains {
                let Some(&root) = node_of.get(&c.root) else { continue };
                let particles: Vec<PhysParticle> = c.particles.iter().enumerate().map(|(i, p)| PhysParticle {
                    node: p.id.and_then(|id| node_of.get(&id).copied()), parent: i as i32 - 1, end_offset: c.end, local_position: p.local,
                    damping: p.damping, elasticity: p.elasticity, stiffness: p.stiffness, inert: p.inert, radius: p.radius, add_angle_scale: 1., turns_parent: false,
                    v02: Some(V02Particle { rotate: p.rotate, next_length: p.next_length, keep_min: p.keep_min, keep_max: p.keep_max, move_limit: p.move_limit,
                        min: p.min, max: p.max, crush: p.crush, crush_min: p.crush_min, crush_max: p.crush_max, crush_add_min: p.crush_add_min,
                        crush_add_max: p.crush_add_max, initial_scale: p.initial.as_ref().map(|t| t.scale).unwrap_or([1.; 3]) }) }).collect();
                if particles.iter().take(particles.len() - 1).any(|p| p.node.is_none()) { continue; }
                let colliders = c.colliders.iter().filter_map(|col| Some(PhysCollider { at: Ref::Rig(*rig_index.get(&col.transform)?), center: col.center,
                    radius: col.radius, height: col.height, direction: col.direction as u32, inside: col.inside })).collect();
                out.chains.push(PhysChain { root, component: root, ver01: false, suppress_skip: c.suppress_skip, freeze_axis: 0, mirror: 0,
                    gravity: c.gravity, force: c.force, local_gravity: [0.; 3], update_rate: c.update_rate, particles, colliders,
                    v02: Some((c.end, c.reflect_speed, c.max_steps as u32)) });
            }
            if out.chains.len() > chain_first { out.ranges.push((2, node_first, out.nodes.len() - node_first, chain_first, out.chains.len() - chain_first)); }
            else { out.nodes.truncate(node_first); out.sources.truncate(node_first); }
        }
        for set in sets(s) {
            let Some(state) = set_of(s, set) else { continue };
            let tfs = &state.skel.tfs;
            let chains = state.gpu_view();
            let (node_first, chain_first) = (out.nodes.len(), out.chains.len());
            // Ids of this set's skeleton the chains use, then their ancestors up to a body bone (clothes) or the top.
            let is_rig = |id: i64| set == SetRef::Clothes && rig_index.contains_key(&id);
            let mut needed: Vec<i64> = Vec::new();
            let mut want = |id: i64, needed: &mut Vec<i64>| if !needed.contains(&id) { needed.push(id); };
            for c in &chains {
                want(c.root, &mut needed); want(c.component, &mut needed);
                for p in &c.particles { if let Some(id) = p.id { want(id, &mut needed); } }
                for col in &c.colliders { if !col.in_body { want(col.transform, &mut needed); } }
            }
            // An accessory's palette matrix j is its skeleton's bone j: every one becomes a node, so the palettes follow the
            // solved bones (at the sampling frames nothing swings, and every bone of the skeleton moves with its top).
            if let SetRef::Part(_) = set { for id in &state.skel.bone_ids { if tfs.contains_key(id) { want(*id, &mut needed); } } }
            // The attachment ancestors take the body's or head's locals each frame: a private skeleton is anchored below them.
            let attached: Vec<i64> = state.attachment_ids().to_vec();
            let inside = |p: &i64| *p != 0 && tfs.contains_key(p) && !attached.contains(p);
            needed.retain(|id| !attached.contains(id));
            let mut k = 0;
            while k < needed.len() {
                let id = needed[k];
                k += 1;
                if is_rig(id) { continue; }
                if let Some(p) = tfs.get(&id).and_then(|tf| tf.parent).filter(|p| *p != id && inside(p)) {
                    if !needed.contains(&p) { needed.push(p); }
                }
            }
            // Parents first.
            let depth = |id: i64| { let mut d = 0; let mut n = id; while let Some(p) = tfs.get(&n).and_then(|t| t.parent).filter(|p| *p != n && inside(p)) { d += 1; n = p; if d > 512 { break; } } d };
            needed.sort_by_key(|id| (depth(*id), *id));
            // Particle prefab locals (each step starts from them).
            let mut init: HashMap<i64, ([f32; 3], [f32; 4])> = HashMap::new();
            for c in &chains { for p in &c.particles { if let Some(id) = p.id { init.entry(id).or_insert((p.init_local_pos, p.init_local_rot)); } } }
            let mut node_of: HashMap<i64, usize> = HashMap::new();
            for id in &needed {
                let tf = &tfs[id];
                let parent_id = tf.parent.filter(|p| *p != *id && inside(p));
                let alias = if is_rig(*id) { rig_index.get(id).copied() } else { None };
                let (pos, rot) = init.get(id).copied().unwrap_or((tf.pos, tf.rot));
                let mut node = PhysNode { parent: Parent::None, alias, local_from: None, pos, rot, scale: tf.scale, matrix: None, rot_offset: [0., 0., 0., 1.] };
                match parent_id {
                    Some(p) if node_of.contains_key(&p) => node.parent = Parent::Node(node_of[&p]),
                    Some(p) if set == SetRef::Clothes && rig_index.contains_key(&p) => node.parent = Parent::Rig(rig_index[&p]),
                    _ => {
                        // A private top: the body bone carrying it best over the samples, at a fixed offset.
                        let key = (set, *id);
                        let mut best: Option<(usize, f32, Mat4)> = None;
                        if let Some(first) = taken.sets.first().and_then(|w| w.get(&key)) {
                            for r in 0..rig.ids.len() {
                                let Some(inv) = crate::pose::mat_inverse_checked(&taken.worlds[0][r]) else { continue };
                                let m = crate::pose::mat_mul_pub(&inv, first);
                                let mut err = 0f32;
                                for (worlds, sets) in taken.worlds.iter().zip(&taken.sets).skip(1) {
                                    let Some(w) = sets.get(&key) else { continue };
                                    err = err.max(super::export::distance_pub(&crate::pose::mat_mul_pub(&worlds[r], &m), w));
                                    if best.as_ref().is_some_and(|b| err >= b.1) { break; }
                                }
                                if best.as_ref().map_or(true, |b| err < b.1) { best = Some((r, err, m)); }
                                if err < 1e-6 { break; }
                            }
                        }
                        if let Some((r, err, m)) = best {
                            if err > 1e-4 { out.loose_tops += 1; }
                            node.parent = Parent::Rig(r);
                            node.matrix = Some(m);
                            let rig_rot = body.map(|b| world_rot(rig.ids[r], &b.tfs)).unwrap_or([0., 0., 0., 1.]);
                            node.rot_offset = quat_mul(conj(rig_rot), world_rot(*id, tfs));
                        }
                    }
                }
                if let Some(a) = alias { aliased.push(a); }
                node_of.insert(*id, out.nodes.len());
                out.index.insert((set, *id), out.nodes.len());
                if set == SetRef::Clothes { out.clothes_by_name.entry(tf.name.clone()).or_insert(out.nodes.len()); }
                out.nodes.push(node);
                out.sources.push((set, *id));
            }
            let at = |id: i64, in_body: bool| -> Option<Ref> {
                if in_body { return body.and_then(|_| rig_index.get(&id)).map(|r| Ref::Rig(*r)); }
                node_of.get(&id).map(|n| Ref::Node(*n))
            };
            for c in chains {
                let (Some(&root), Some(&component)) = (node_of.get(&c.root), node_of.get(&c.component)) else { continue };
                let particles = c.particles.iter().map(|p| PhysParticle { node: p.id.and_then(|id| node_of.get(&id).copied()), parent: p.parent,
                    end_offset: p.end_offset, local_position: p.local_position, damping: p.damping, elasticity: p.elasticity, stiffness: p.stiffness,
                    inert: p.inert, radius: p.radius, add_angle_scale: p.add_angle_scale, turns_parent: p.turns_parent, v02: None }).collect();
                let colliders = c.colliders.iter().filter_map(|col| Some(PhysCollider { at: at(col.transform, col.in_body)?, center: col.center,
                    radius: col.radius, height: col.height, direction: col.direction as u32, inside: col.inside })).collect();
                out.chains.push(PhysChain { root, component, ver01: c.ver01, suppress_skip: c.suppress_skip, freeze_axis: c.freeze_axis as u32,
                    mirror: c.mirror, gravity: c.gravity, force: c.force, local_gravity: c.local_gravity, update_rate: c.update_rate, particles, colliders, v02: None });
            }
            out.ranges.push((u32::from(set == SetRef::Clothes), node_first, out.nodes.len() - node_first, chain_first, out.chains.len() - chain_first));
        }
        // Rig bones whose world hangs under a solved alias (and are not aliases themselves), in rig order.
        let mut under = vec![false; rig.ids.len()];
        for a in &aliased { under[*a] = true; }
        for b in 0..rig.ids.len() {
            if under[b] { continue; }
            let p = rig.parent[b];
            if p >= 0 && under[p as usize] { under[b] = true; out.follow.push(b); }
        }
        out
    }

    /// Every set's bone worlds at the current sample, by (set, id): the private tops' anchors and the nodes' worlds for the
    /// palette factorization come from them.
    pub fn sample_sets(s: &crate::session::OkSession) -> HashMap<(SetRef, i64), Mat4> {
        let mut out = HashMap::new();
        for set in sets(s) {
            let Some(state) = set_of(s, set) else { continue };
            let mut cache = HashMap::new();
            for id in state.skel.tfs.keys() { out.insert((set, *id), crate::pose::world_of(*id, &state.skel.tfs, &mut cache)); }
        }
        out
    }

    pub fn particle_count(&self) -> usize { self.chains.iter().map(|c| c.particles.len()).sum() }

    /// The nodes a chain's step writes (GpuRig.usf StepChain / StepChainV02): every particle bone below the root (its
    /// position), and a parent particle's bone a child turns (DynamicBone turns a parent with one child; Ver02 a
    /// parent with the rotate flag).
    pub fn chain_writes(&self, c: &PhysChain) -> Vec<usize> {
        let mut out = Vec::new();
        for (i, p) in c.particles.iter().enumerate() {
            if p.parent < 0 { continue; }
            if let Some(n) = p.node { out.push(n); }
            let parent = &c.particles[p.parent as usize];
            let turns = if c.v02.is_some() { parent.v02.as_ref().is_some_and(|v| v.rotate) } else { p.turns_parent };
            if turns && i > 0 { if let Some(n) = parent.node { out.push(n); } }
        }
        out.sort_unstable(); out.dedup(); out
    }
    /// The nodes whose worlds a chain's step reads: its particles', root's, component's and colliders' bones, each with
    /// every node above it (a world is built from the parent's).
    pub fn chain_reads(&self, c: &PhysChain) -> Vec<usize> {
        let mut out = Vec::new();
        let mut up = |n: usize, out: &mut Vec<usize>| { let mut k = n; loop { out.push(k); match self.nodes[k].parent { Parent::Node(p) if out.len() < 8192 => k = p, _ => break } } };
        up(c.root, &mut out); up(c.component, &mut out);
        for p in &c.particles { if let Some(n) = p.node { up(n, &mut out); } }
        for col in &c.colliders { if let Ref::Node(n) = col.at { up(n, &mut out); } }
        out.sort_unstable(); out.dedup(); out
    }

    /// Whether a set's chains must be stepped one after another: one writes a bone another reads (the same bone, or
    /// one above it: its result moves the other's bones before that one reads them). Otherwise the GPU steps each chain
    /// on its own thread; every node is rebuilt from the solved locals afterwards either way.
    pub fn serial(&self, chain_first: usize, chain_count: usize) -> bool {
        let chains = &self.chains[chain_first..chain_first + chain_count];
        let writes: Vec<Vec<usize>> = chains.iter().map(|c| self.chain_writes(c)).collect();
        let reads: Vec<Vec<usize>> = chains.iter().map(|c| self.chain_reads(c)).collect();
        let meets = |a: &[usize], b: &[usize]| a.iter().any(|x| b.binary_search(x).is_ok());
        for i in 0..chains.len() { for j in i + 1..chains.len() {
            if meets(&writes[i], &reads[j]) || meets(&writes[j], &reads[i]) { return true; }
        } }
        false
    }
}

#[cfg(test)]
mod stats {
    /// What bounds the GPU physics pass (one thread per chain or per serial set, so the slowest thread): per card, the
    /// sets that must step their chains one after another (the old test: chains sharing any bone or line; the new one:
    /// a chain writing a bone another reads), the longest chain and its colliders, the dearest serial set, the deepest
    /// node. OK_PHYS_CARDS: a folder of OK cards (default CrowdAPNG), OK_PHYS_LIMIT cards at most.
    ///   cargo test --release --lib gpu_rig_physics_stats -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn gpu_rig_physics_stats() {
        use super::{Parent, Physics};
        let dir = std::env::var("OK_PHYS_CARDS").unwrap_or_else(|_| "E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG".into());
        let limit: usize = std::env::var("OK_PHYS_LIMIT").ok().and_then(|v| v.parse().ok()).unwrap_or(usize::MAX);
        let mut cards: Vec<_> = std::fs::read_dir(&dir).unwrap().filter_map(|e| e.ok()).map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|x| x == "png") && !p.file_name().unwrap().to_string_lossy().starts_with('.')).collect();
        cards.sort();
        let clip = std::fs::read("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim").unwrap();
        let mut worst_chain = (0usize, String::new());
        let mut worst_serial = (0usize, String::new());
        for card in cards.iter().take(limit) {
            std::env::set_var("OK_RIG_CARD", card);
            let opened = std::panic::catch_unwind(|| crate::gpu_rig::tests::open_with_clip_pub());
            let Ok(card_session) = opened else { println!("{}: open failed", card.display()); continue };
            let bytes = card_session.to_animation_bytes().expect("snapshot");
            let s = crate::session::OkSession::from_animation_bytes(&bytes).expect("reopen");
            let mut s = Box::new(s);
            let s = &mut *s;
            if crate::gpu_rig::export::export_rig(s, &clip).is_err() { println!("{}: no rig", card.display()); continue; }
            let rig = crate::gpu_rig::export::FlatRig::from_session(s).unwrap();
            let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip).max(0.1);
            let taken = crate::gpu_rig::export::take_samples(s, &rig, &[0., d * 0.31, d * 0.62, d * 0.87]);
            let ph = Physics::build(s, &rig, &taken);
            let name = card.file_name().unwrap().to_string_lossy().to_string();
            let cost = |c: &super::PhysChain| c.particles.len() * (1 + c.colliders.len());
            let depth = |n: usize| { let mut d = 0; let mut k = n; while let Parent::Node(p) = ph.nodes[k].parent { d += 1; k = p; } d };
            let (mut old_serial, mut new_serial, mut set_cost_max, mut levels_max) = (0, 0, 0usize, 0usize);
            for &(kind, nf, nc, cf, cc) in &ph.ranges {
                let old = {
                    let anc = |n: usize| { let mut out = vec![n]; let mut k = n; while let Parent::Node(p) = ph.nodes[k].parent { out.push(p); k = p; } out };
                    let touched: Vec<Vec<usize>> = ph.chains[cf..cf + cc].iter().map(|c| c.particles.iter().filter_map(|p| p.node).collect()).collect();
                    let lines: Vec<Vec<Vec<usize>>> = touched.iter().map(|t| t.iter().map(|n| anc(*n)).collect()).collect();
                    (0..cc).any(|i| (i + 1..cc).any(|j| touched[i].iter().any(|a| lines[j].iter().any(|l| l.contains(a))) || touched[j].iter().any(|b| lines[i].iter().any(|l| l.contains(b)))))
                };
                let new = ph.serial(cf, cc);
                old_serial += old as usize; new_serial += new as usize;
                let sc: usize = ph.chains[cf..cf + cc].iter().map(cost).sum();
                if new { set_cost_max = set_cost_max.max(sc); if sc > worst_serial.0 { worst_serial = (sc, format!("{name} kind {kind} chains {cc}")); } }
                levels_max = levels_max.max((nf..nf + nc).map(depth).max().unwrap_or(0) + 1);
            }
            let longest = ph.chains.iter().map(|c| c.particles.len()).max().unwrap_or(0);
            let most_colliders = ph.chains.iter().map(|c| c.colliders.len()).max().unwrap_or(0);
            let chain_cost = ph.chains.iter().map(cost).max().unwrap_or(0);
            if chain_cost > worst_chain.0 { worst_chain = (chain_cost, name.clone()); }
            println!("{name}: sets {} serial {old_serial}->{new_serial}, nodes {}, chains {}, particles {}, longest chain {longest}, most colliders {most_colliders}, dearest chain {chain_cost}, dearest serial set {set_cost_max}, levels {levels_max}, follow {}",
                ph.ranges.len(), ph.nodes.len(), ph.chains.len(), ph.particle_count(), ph.follow.len());
        }
        println!("dearest chain {} ({}), dearest serial set {} ({})", worst_chain.0, worst_chain.1, worst_serial.0, worst_serial.1);
    }
}
