//! The character's DynamicBone chains for the GPU rig's verlet pass (Shaders/GpuRig.usf PhysicsCS): the clothes set
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
}

#[derive(Clone, Debug)]
pub struct PhysCollider { pub at: Ref, pub center: [f32; 3], pub radius: f32, pub height: f32, pub direction: u32, pub inside: bool }

#[derive(Clone, Debug)]
pub struct PhysChain {
    pub root: usize, pub component: usize, pub ver01: bool, pub suppress_skip: bool, pub freeze_axis: u32, pub mirror: u32,
    pub gravity: [f32; 3], pub force: [f32; 3], pub local_gravity: [f32; 3], pub update_rate: f32,
    pub particles: Vec<PhysParticle>, pub colliders: Vec<PhysCollider>,
}

/// Which session set a node's actual world is read from (for the palette factorization).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SetRef { Clothes, Part(usize) }

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
    /// Per set, in stepping order: (clothes, node first, node count, chain first, chain count). Sets do not share nodes:
    /// the GPU steps each on its own thread, the clothes first (they move body bones the accessories hang on).
    pub ranges: Vec<(bool, usize, usize, usize, usize)>,
}

fn set_of(s: &crate::session::OkSession, set: SetRef) -> Option<&crate::dbone::AttachedDynamicSet> {
    match set {
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
                let mut node = PhysNode { parent: Parent::None, alias, pos, rot, scale: tf.scale, matrix: None, rot_offset: [0., 0., 0., 1.] };
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
                    inert: p.inert, radius: p.radius, add_angle_scale: p.add_angle_scale, turns_parent: p.turns_parent }).collect();
                let colliders = c.colliders.iter().filter_map(|col| Some(PhysCollider { at: at(col.transform, col.in_body)?, center: col.center,
                    radius: col.radius, height: col.height, direction: col.direction as u32, inside: col.inside })).collect();
                out.chains.push(PhysChain { root, component, ver01: c.ver01, suppress_skip: c.suppress_skip, freeze_axis: c.freeze_axis as u32,
                    mirror: c.mirror, gravity: c.gravity, force: c.force, local_gravity: c.local_gravity, update_rate: c.update_rate, particles, colliders });
            }
            out.ranges.push((set == SetRef::Clothes, node_first, out.nodes.len() - node_first, chain_first, out.chains.len() - chain_first));
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
}
