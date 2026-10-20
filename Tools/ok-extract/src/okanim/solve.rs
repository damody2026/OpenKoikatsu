//! Plays an evaluated OK frame onto real characters: writes each role's pose into its own skeleton (carried from the
//! standard rest to that skeleton's rest, hips scaled to its body), places the roles so their contacts meet, bends the
//! limbs onto the contact targets, keeps planted feet on the ground (stepping when they must move), then runs the
//! character's own finishing pass (body shape, expression bones, ABMX, hair/cloth physics).

use super::{carry_delta, carry_vector, Contact, Eval, Player};
use crate::finalik::{qangle_axis, qinv, qmul, qnorm, qrot, vadd, vcross, vdot, vlen, vnorm, vsub, Quat, V3};
use crate::pose::{Mat4, Skeleton};
use crate::session::OkSession;
use std::collections::HashMap;

/// Per-player memory across frames (planted feet, steps in progress).
#[derive(Default)]
pub struct State {
    feet: HashMap<(usize, String), Foot>,
    last_t: f32,
    /// Placement actually applied (it approaches its target at a walking pace), the distance each role still has to
    /// cover (hips heights), and each role's walk cycle time.
    applied_offsets: Vec<V3>,
    remaining: Vec<f32>,
    /// Largest partner contact error of the previous frame (m), negative before the first: posture runs only when
    /// contacts were left unmet.
    last_contact_max: f32,
    walk_time: Vec<f32>,
}

#[derive(Clone, Copy)]
struct Foot {
    planted: V3,
    /// Step in progress: from, to, elapsed, duration.
    step: Option<(V3, V3, f32, f32)>,
}

/// Per-frame result, for tests and the editor's validation panel.
#[derive(Debug, Default, Clone, serde::Serialize)]
pub struct Report {
    /// Remaining contact error (m) after placement and IK, worst and mean over the fully active partner contacts.
    pub contact_max: f32,
    pub contact_mean: f32,
    pub contacts: usize,
    /// Role placement offsets applied this frame (m).
    pub offsets: Vec<V3>,
    pub steps: usize,
    /// Posture adjustments made this frame (pelvis, spine, neck moves).
    pub posture: usize,
    /// Largest walk weight mixed into a role's legs this frame (0 = none).
    pub walk: f32,
    /// The contact with the largest remaining error ("role.effector->role.bone").
    pub worst: String,
    /// Limbs bent onto partner / body contacts this frame ("role.effector>end").
    pub ik: Vec<String>,
}

fn mat_pos(m: &Mat4) -> V3 { [m[0][3], m[1][3], m[2][3]] }
fn mat_rot(m: &Mat4) -> Quat { crate::finalik::mat_rot(m) }

thread_local! {
    /// Bone name -> id of the skeletons of the frame being solved (their bones do not change during `apply`).
    static IDS: std::cell::RefCell<Vec<(usize, usize, std::rc::Rc<HashMap<String, i64>>)>> = const { std::cell::RefCell::new(Vec::new()) };
}

fn ids_by_name(sk: &Skeleton) -> std::rc::Rc<HashMap<String, i64>> {
    let key = (&sk.tfs as *const _ as usize, sk.tfs.len());
    if let Some(m) = IDS.with(|c| c.borrow().iter().find(|e| (e.0, e.1) == key).map(|e| e.2.clone())) { return m; }
    let mut m = HashMap::new();
    for (id, tf) in &sk.tfs { m.entry(tf.name.clone()).or_insert(*id); }
    let m = std::rc::Rc::new(m);
    IDS.with(|c| c.borrow_mut().push((key.0, key.1, m.clone())));
    m
}

fn clear_ids() { IDS.with(|c| c.borrow_mut().clear()); }

/// Rotation-only world rotation of a bone (Unity Transform.rotation).
fn world_rot(sk: &Skeleton, id: i64) -> Quat { crate::pose::world_rotation_of(id, &sk.tfs) }

/// Sets a bone's world rotation by changing its local rotation.
fn set_world_rot(sk: &mut Skeleton, id: i64, world: Quat) {
    let parent = sk.tfs.get(&id).and_then(|t| t.parent).filter(|p| sk.tfs.contains_key(p));
    let pr = parent.map(|p| world_rot(sk, p)).unwrap_or([0.0, 0.0, 0.0, 1.0]);
    if let Some(tf) = sk.tfs.get_mut(&id) { tf.rot = qnorm(qmul(qinv(pr), world)); }
}

/// World point -> local position under the bone's parent (full parent matrix, scale included).
fn world_to_parent_local(sk: &Skeleton, id: i64, p: V3) -> Option<V3> {
    let parent = sk.tfs.get(&id)?.parent.filter(|p| sk.tfs.contains_key(p))?;
    let inv = crate::pose::mat_inverse_checked(&crate::pose::world_of_id(parent, &sk.tfs))?;
    Some(crate::pose::mat_vec(&inv, p))
}

/// Writes one role's pose into `body` (which holds the reset baseline), carrying each delta from the animation's
/// standard rest onto this skeleton's rest (`rest`, the shaped baseline). Returns the hips world height at rest.
thread_local! {
    /// World rotations of the bones of a rest skeleton (the shaped baseline does not change from frame to frame).
    static REST_WORLD: std::cell::RefCell<Vec<(usize, usize, std::rc::Rc<HashMap<i64, Quat>>)>> = const { std::cell::RefCell::new(Vec::new()) };
}

fn rest_world_rotations(rest: &Skeleton) -> std::rc::Rc<HashMap<i64, Quat>> {
    // Keyed by the skeleton and its contents' fingerprint (a reshaped body gets new rotations).
    let fingerprint = rest.tfs.iter().fold(0u64, |h, (id, t)| h ^ (*id as u64).wrapping_mul(0x9E3779B97F4A7C15) ^ t.rot[0].to_bits() as u64 ^ ((t.rot[3].to_bits() as u64) << 21));
    let key = (&rest.tfs as *const _ as usize, fingerprint as usize);
    if let Some(m) = REST_WORLD.with(|c| c.borrow().iter().find(|e| (e.0, e.1) == key).map(|e| e.2.clone())) { return m; }
    let m: HashMap<i64, Quat> = rest.tfs.keys().map(|&id| (id, world_rot(rest, id))).collect();
    let m = std::rc::Rc::new(m);
    REST_WORLD.with(|c| { let mut c = c.borrow_mut(); if c.len() > 16 { c.remove(0); } c.push((key.0, key.1, m.clone())); });
    m
}

fn write_pose(body: &mut Skeleton, rest: &Skeleton, role: &super::RoleEval, aliases: &HashMap<String, String>) -> f32 {
    let ids = ids_by_name(body);
    let anim = &role.anim;
    let rest_world = rest_world_rotations(rest);
    for (k, track) in anim.tracks.iter().enumerate() {
        let Some(&id) = ids.get(&track.bone).or_else(|| aliases.get(&track.bone).and_then(|n| ids.get(n))) else { continue };
        let Some(rest_tf) = rest.tfs.get(&id) else { continue };
        let target_world = rest_world.get(&id).copied().unwrap_or_else(|| world_rot(rest, id));
        let d = carry_delta(role.pose.rot[k], track.rest_world, target_world);
        if let Some(tf) = body.tfs.get_mut(&id) { tf.rot = qnorm(qmul(rest_tf.rot, d)); }
    }
    let mut hips_height = 1.0;
    if let (Some(&hid), Some(rest_hips)) = (ids.get(&anim.hips_bone), ids.get(&anim.hips_bone).and_then(|id| rest.tfs.get(id))) {
        let len = vlen(rest_hips.pos).max(1e-4);
        let parent_world = rest_hips.parent.map(|p| world_rot(rest, p)).unwrap_or([0.0, 0.0, 0.0, 1.0]);
        let v = carry_vector(role.pose.hips, anim.hips_parent_rest_world, parent_world);
        if let Some(tf) = body.tfs.get_mut(&hid) { tf.pos = [v[0] * len, v[1] * len, v[2] * len]; }
        hips_height = mat_pos(&crate::pose::world_of_id(hid, &rest.tfs))[1].abs().max(0.1);
        // Role placement in the group space: offset (in hips heights) and yaw about the vertical axis.
        if role.yaw.abs() > 1e-4 || vlen(role.offset) > 1e-6 {
            let hips_world = crate::pose::world_of_id(hid, &body.tfs);
            let yaw = qangle_axis(role.yaw, [0.0, 1.0, 0.0]);
            let p = qrot(yaw, mat_pos(&hips_world));
            let p = vadd(p, [role.offset[0] * hips_height, role.offset[1] * hips_height, role.offset[2] * hips_height]);
            let r = qmul(yaw, mat_rot(&hips_world));
            if let Some(local) = world_to_parent_local(body, hid, p) { body.tfs.get_mut(&hid).unwrap().pos = local; }
            set_world_rot(body, hid, r);
        }
    }
    hips_height
}

/// Analytic two-bone IK: bends upper/mid so `end` reaches `target`, blended by `weight`. The bend plane is the current
/// one (the animation's elbow/knee direction is kept).
fn two_bone_ik(sk: &mut Skeleton, end: i64, target: V3, weight: f32) -> bool {
    let Some(mid) = sk.tfs.get(&end).and_then(|t| t.parent) else { return false };
    let Some(upper) = sk.tfs.get(&mid).and_then(|t| t.parent) else { return false };
    let pos = |sk: &Skeleton, id: i64| mat_pos(&crate::pose::world_of_id(id, &sk.tfs));
    let (a, b, c) = (pos(sk, upper), pos(sk, mid), pos(sk, end));
    let (lab, lcb) = (vlen(vsub(b, a)), vlen(vsub(c, b)));
    if lab < 1e-5 || lcb < 1e-5 { return false; }
    let w = weight.clamp(0.0, 1.0);
    let lat = vlen(vsub(target, a)).clamp(1e-4, (lab + lcb) * 0.9999);
    let angle = |u: V3, v: V3| vdot(vnorm(u), vnorm(v)).clamp(-1.0, 1.0).acos();
    let ac_ab0 = angle(vsub(c, a), vsub(b, a));
    let ba_bc0 = angle(vsub(a, b), vsub(c, b));
    let ac_ab1 = ((lcb * lcb - lab * lab - lat * lat) / (-2.0 * lab * lat)).clamp(-1.0, 1.0).acos();
    let ba_bc1 = ((lat * lat - lab * lab - lcb * lcb) / (-2.0 * lab * lcb)).clamp(-1.0, 1.0).acos();
    let mut axis = vcross(vsub(c, a), vsub(b, a));
    if vlen(axis) < 1e-6 { axis = vcross(vsub(target, a), [0.0, 1.0, 0.0]); }
    if vlen(axis) < 1e-6 { axis = [1.0, 0.0, 0.0]; }
    let axis = vnorm(axis);
    let rot = |deg: f32| qangle_axis(deg, axis);
    let r0 = rot((ac_ab1 - ac_ab0).to_degrees() * w);
    let r1 = rot((ba_bc1 - ba_bc0).to_degrees() * w);
    let (wu, wm) = (world_rot(sk, upper), world_rot(sk, mid));
    set_world_rot(sk, upper, qmul(r0, wu));
    set_world_rot(sk, mid, qmul(r1, qmul(r0, wm)));
    // Swing the whole limb so the (now correctly bent) end points at the target.
    let (a2, c2) = (pos(sk, upper), pos(sk, end));
    let (from, to) = (vsub(c2, a2), vsub(target, a2));
    if vlen(from) > 1e-6 && vlen(to) > 1e-6 {
        let q = crate::finalik::qfrom_to_rotation(from, to);
        let q = crate::finalik::qslerp([0.0, 0.0, 0.0, 1.0], q, w);
        let wu = world_rot(sk, upper);
        let wm = world_rot(sk, mid);
        set_world_rot(sk, upper, qmul(q, wu));
        set_world_rot(sk, mid, qmul(q, wm));
    }
    true
}

/// World position of a contact's target on role `target_role` (or the ground / group-space point).
fn contact_target(c: &Contact, effector: V3, bodies: &[&Skeleton], heights: &[f32]) -> Option<V3> {
    match c.target_role {
        -1 => Some([effector[0], c.offset[1] * heights.get(c.role as usize).copied().unwrap_or(1.0), effector[2]]),
        -2 => Some(c.offset),
        r if r >= 0 => {
            let sk = bodies.get(r as usize)?;
            let ids = ids_by_name(sk);
            let id = *ids.get(&c.target_bone)?;
            let m = crate::pose::world_of_id(id, &sk.tfs);
            let h = heights.get(r as usize).copied().unwrap_or(1.0);
            Some(vadd(mat_pos(&m), qrot(world_rot(sk, id), [c.offset[0] * h, c.offset[1] * h, c.offset[2] * h])))
        }
        _ => None,
    }
}

fn effector_pos(sk: &Skeleton, bone: &str) -> Option<V3> {
    let ids = ids_by_name(sk);
    ids.get(bone).map(|&id| mat_pos(&crate::pose::world_of_id(id, &sk.tfs)))
}

/// Posture adaptation on/off (all players): bodies bend (crouch / tiptoe, lean, turn the head) for contacts the limbs
/// cannot reach after placement. Off = placement and limb IK only (for comparisons).
pub static POSTURE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(true);
/// Finishing passes of a group's characters on worker threads (off: one after the other).
pub static PARALLEL_FINISH: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// Rotates a bone in world space by `q` (about its own position), children follow.
fn turn_bone(sk: &mut Skeleton, id: i64, q: Quat) { let w = world_rot(sk, id); set_world_rot(sk, id, qmul(q, w)); }

/// The rotation turning `from` towards `to`, at most `max_deg`, scaled by `weight`.
fn limited_turn(from: V3, to: V3, max_deg: f32, weight: f32) -> Option<Quat> {
    if vlen(from) < 1e-6 || vlen(to) < 1e-6 { return None; }
    let q = crate::finalik::qfrom_to_rotation(from, to);
    let ang = 2.0 * q[3].abs().min(1.0).acos().to_degrees();
    if ang < 1e-3 { return None; }
    let k = (max_deg / ang).min(1.0) * weight.clamp(0.0, 1.0);
    Some(crate::finalik::qslerp([0.0, 0.0, 0.0, 1.0], q, k))
}

/// How far a role's partner contacts are from being met, beyond what its limbs can still reach (m, weighted): what
/// posture adjustments must lower.
fn posture_error(cross: &[&(Contact, f32)], sessions: &[&mut OkSession], heights: &[f32], role: usize) -> f32 {
    let bodies: Vec<&Skeleton> = sessions.iter().map(|s| s.body_skel.as_ref().unwrap()).collect();
    let mut sum = 0.0;
    for (c, w) in cross {
        let (a, b) = (c.role as usize, c.target_role as usize);
        if a != role && b != role { continue; }
        let Some(e) = effector_pos(bodies[a], &c.effector) else { continue };
        let Some(tp) = contact_target(c, e, &bodies, heights) else { continue };
        let body = bodies[a];
        let pos = |id: i64| mat_pos(&crate::pose::world_of_id(id, &body.tfs));
        let chain = if c.ik { carrying_limb(body, &c.effector) } else { None }
            .and_then(|(end, off)| { let mid = body.tfs.get(&end)?.parent?; let up = body.tfs.get(&mid)?.parent?; Some((end, mid, up, off)) });
        let gap = match chain {
            Some((end, mid, up, off)) => {
                let reach = 0.95 * (vlen(vsub(pos(mid), pos(up))) + vlen(vsub(pos(end), pos(mid))));
                (vlen(vsub(vsub(tp, off), pos(up))) - reach).max(0.0)
            }
            None => vlen(vsub(tp, e)),
        };
        sum += gap * w * c.priority.max(0.05);
    }
    sum
}

/// Posture strategies for partner contacts placement and the limbs leave unmet, the roles taking turns for a few
/// rounds: the pelvis lowers (crouch, to 15% of the hips height) or rises (tiptoe, 5%), each spine bone leans (to 15
/// degrees), the neck turns (to 25 degrees). Every adjustment is tried in small steps and kept only when it lowers the
/// role's contact error (`posture_error`), so posture never makes a role's contacts worse.
fn posture(eval: &Eval, sessions: &mut [&mut OkSession], heights: &[f32]) -> usize {
    let roles = eval.roles.len();
    let mut moves = 0;
    let cross: Vec<&(Contact, f32)> = eval.contacts.iter().filter(|(c, _)| c.target_role >= 0 && (c.target_role as usize) < roles && c.target_role as u32 != c.role).collect();
    if cross.is_empty() { return 0; }
    for _round in 0..3 {
        let before = moves;
        for r in 0..roles {
            let mut err = posture_error(&cross, sessions, heights, r);
            // Placement and the limbs usually leave less than this (1.6 cm on a 0.8 m hips height): nothing to do.
            if err <= 0.02 * heights[r] { continue; }
            let hips_bone = eval.roles[r].anim.hips_bone.clone();
            let h = heights[r];
            // The mean direction of the role's unmet contacts (where its parts should go).
            let dir = {
                let bodies: Vec<&Skeleton> = sessions.iter().map(|s| s.body_skel.as_ref().unwrap()).collect();
                let mut d = [0.0f32; 3];
                for (c, w) in &cross {
                    let (a, b) = (c.role as usize, c.target_role as usize);
                    if a != r && b != r { continue; }
                    let Some(e) = effector_pos(bodies[a], &c.effector) else { continue };
                    let Some(tp) = contact_target(c, e, &bodies, heights) else { continue };
                    let g = if a == r { vsub(tp, e) } else { vsub(e, tp) };
                    for i in 0..3 { d[i] += g[i] * w; }
                }
                d
            };
            // Pelvis: try steps down and up, keep the best that helps.
            {
                let body = sessions[r].body_skel.as_ref().unwrap();
                let ids = ids_by_name(body);
                if let Some(&hid) = ids.get(&hips_bone) {
                    let saved = body.tfs[&hid].pos;
                    let base = mat_pos(&crate::pose::world_of_id(hid, &body.tfs));
                    let mut best = (err, saved);
                    // Down (crouch) / up (tiptoe), and along the unmet contacts' mean direction (the hips sway towards them;
                    // the feet keep their own links).
                    let n = vlen(dir);
                    let along = if n > 1e-6 { [dir[0] / n, dir[1] / n, dir[2] / n] } else { [0.0; 3] };
                    let mut candidates: Vec<V3> = [-1.0f32, -2.0, -3.0, -5.0, -7.5, 1.0, 2.5].iter().map(|k| [0.0, k * 0.02 * h, 0.0]).collect();
                    if n > 1e-6 { for k in [0.01f32, 0.02, 0.04, 0.06, 0.08] { candidates.push([along[0] * k * h, along[1] * k * h, along[2] * k * h]); } }
                    for d in candidates {
                        let body = sessions[r].body_skel.as_mut().unwrap();
                        if let Some(local) = world_to_parent_local(body, hid, vadd(base, d)) { body.tfs.get_mut(&hid).unwrap().pos = local; }
                        let e = posture_error(&cross, sessions, heights, r);
                        if e < best.0 - 1e-5 { best = (e, sessions[r].body_skel.as_ref().unwrap().tfs[&hid].pos); }
                    }
                    sessions[r].body_skel.as_mut().unwrap().tfs.get_mut(&hid).unwrap().pos = best.1;
                    if best.0 < err - 1e-5 { err = best.0; moves += 1; }
                }
            }
            // Spine and neck: lean towards the contacts' remaining gap.
            if vlen(dir) < 1e-5 { continue; }
            for (bone, max_deg) in [("cf_j_spine01", 15.0f32), ("cf_j_spine02", 15.0), ("cf_j_spine03", 15.0), ("cf_j_neck", 25.0)] {
                let Some(&id) = ids_by_name(sessions[r].body_skel.as_ref().unwrap()).get(bone) else { continue };
                let saved = sessions[r].body_skel.as_ref().unwrap().tfs[&id].rot;
                let (pivot, reach_point) = {
                    let body = sessions[r].body_skel.as_ref().unwrap();
                    let pivot = mat_pos(&crate::pose::world_of_id(id, &body.tfs));
                    // The part that leans: a point above the pivot (towards the head).
                    let up = qrot(world_rot(body, id), [0.0, 0.3 * h, 0.0]);
                    (pivot, vadd(pivot, up))
                };
                let goal = vadd(reach_point, dir);
                let mut best = (err, saved);
                for step in [5.0f32, 10.0, max_deg] {
                    sessions[r].body_skel.as_mut().unwrap().tfs.get_mut(&id).unwrap().rot = saved;
                    if let Some(q) = limited_turn(vsub(reach_point, pivot), vsub(goal, pivot), step, 1.0) {
                        turn_bone(sessions[r].body_skel.as_mut().unwrap(), id, q);
                        let e = posture_error(&cross, sessions, heights, r);
                        if e < best.0 - 1e-5 { best = (e, sessions[r].body_skel.as_ref().unwrap().tfs[&id].rot); }
                    }
                }
                sessions[r].body_skel.as_mut().unwrap().tfs.get_mut(&id).unwrap().rot = best.1;
                if best.0 < err - 1e-5 { err = best.0; moves += 1; }
            }
        }
        // A round that changed nothing will not be followed by one that does.
        if moves == before { break; }
    }
    moves
}

/// Leg bones the walk cycle drives.
fn is_leg(bone: &str) -> bool { ["thigh", "leg0", "knee", "foot", "toes", "kosi"].iter().any(|k| bone.contains(k)) }

/// Walk mixing: a role still on its way to where the placement wants it (more than 0.05 hips heights to go) gets the
/// walk cycle's legs, by a weight reaching 1 at 0.2 hips heights to go; the cycle runs at the walking pace.
fn walk(player: &mut Player, eval: &Eval, sessions: &mut [&mut OkSession], heights: &[f32], dt: f32) -> Vec<f32> {
    let roles = eval.roles.len();
    let mut weights = vec![0.0f32; roles];
    let state = &mut player.state;
    if state.walk_time.len() != roles { state.walk_time = vec![0.0; roles]; }
    let Some(walk) = player.walk.clone() else { return weights };
    if dt <= 0.0 || state.remaining.len() != roles { return weights; }
    for r in 0..roles {
        let speed: f32 = 1.2;   // the placement's walking pace (hips heights per second)
        let w = ((state.remaining[r] - 0.05) / 0.15).clamp(0.0, 1.0);
        weights[r] = w;
        if w <= 1e-3 { continue; }
        state.walk_time[r] += dt * (speed / 0.8).clamp(0.5, 2.0);
        let pose = walk.sample(state.walk_time[r]);
        let (Some(body), Some(rest)) = (sessions[r].body_skel.as_mut(), sessions[r].body_shaped.as_ref()) else { continue };
        let ids = ids_by_name(body);
        for (k, track) in walk.tracks.iter().enumerate() {
            if !is_leg(&track.bone) { continue; }
            let Some(&id) = ids.get(&track.bone) else { continue };
            let Some(rest_tf) = rest.tfs.get(&id) else { continue };
            let d = carry_delta(pose.rot[k], track.rest_world, world_rot(rest, id));
            let target = qnorm(qmul(rest_tf.rot, d));
            if let Some(tf) = body.tfs.get_mut(&id) { tf.rot = super::nlerp(tf.rot, target, w); }
        }
    }
    weights
}

/// The limb that carries a contact's effector: the effector itself when it is a limb end, else the nearest limb end
/// above it (toes under the foot, fingers under the hand), with the effector's offset from that end (world, m).
fn carrying_limb(sk: &Skeleton, effector: &str) -> Option<(i64, V3)> {
    let ids = ids_by_name(sk);
    let &id = ids.get(effector)?;
    let pos = |id: i64| mat_pos(&crate::pose::world_of_id(id, &sk.tfs));
    // A limb end whose two segments have length (KK's cf_j_foot sits on the ankle: its "chain" is degenerate).
    let usable = |end: i64| -> bool {
        let Some(mid) = sk.tfs.get(&end).and_then(|t| t.parent) else { return false };
        let Some(up) = sk.tfs.get(&mid).and_then(|t| t.parent) else { return false };
        vlen(vsub(pos(end), pos(mid))) > 0.01 && vlen(vsub(pos(mid), pos(up))) > 0.01
    };
    // The topmost limb end within a few bones (toes and foot both bend the leg from the ankle), so every contact on
    // one limb names the same chain.
    let mut cur = id;
    let mut found = None;
    for _ in 0..5 {
        let Some(tf) = sk.tfs.get(&cur) else { break };
        if is_limb_end(&tf.name) && usable(cur) { found = Some(cur); }
        let Some(p) = tf.parent else { break };
        cur = p;
    }
    found.map(|end| (end, vsub(pos(id), pos(end))))
}

/// Limb end bones that can be bent onto a target (two parents above them).
fn is_limb_end(bone: &str) -> bool {
    let b = bone.to_ascii_lowercase();
    b.starts_with("cf_j_hand_") || b.starts_with("cf_j_leg03_") || b.starts_with("cf_j_foot_")
}

pub fn apply(player: &mut Player, sessions: &mut [&mut OkSession], t: f32, dt: f32) -> Result<Report, String> {
    use super::prof;
    let t_apply = std::time::Instant::now();
    clear_ids();
    let mut eval: Eval = player.eval(t);
    // Per-animation adaptation settings (meta): hand / foot IK scale and foot locking.
    for (c, w) in eval.contacts.iter_mut() {
        let Some(role) = eval.roles.get(c.role as usize) else { continue };
        let meta = &role.anim.meta;
        let factor = |key: &str| meta.get(key).and_then(|v| v.parse::<f32>().ok()).unwrap_or(1.0).clamp(0.0, 1.0);
        let e = c.effector.to_ascii_lowercase();
        if e.starts_with("cf_j_hand") { *w *= factor("ik_hands"); }
        if e.starts_with("cf_j_leg03") || e.starts_with("cf_j_toes") || e.starts_with("cf_j_foot") {
            *w *= factor("ik_feet");
            if c.target_role == -1 && meta.get("foot_lock").map(String::as_str) == Some("0") { *w = 0.0; }
        }
    }
    eval.contacts.retain(|(_, w)| *w > 0.0);
    if sessions.len() < eval.roles.len() { return Err(format!("OK player needs {} characters, got {}", eval.roles.len(), sessions.len())); }
    let mut report = Report::default();
    // 1. Pose every role on its own skeleton.
    let t_phase = std::time::Instant::now();
    let mut heights = Vec::with_capacity(eval.roles.len());
    for (role, s) in eval.roles.iter().zip(sessions.iter_mut()) {
        s.ok_begin_frame(t);
        let (Some(body), Some(rest)) = (s.body_skel.as_mut(), s.body_shaped.as_ref()) else { return Err("character has no body skeleton".into()) };
        heights.push(write_pose(body, rest, role, &player.aliases));
    }
    prof::add(0, t_phase);
    let t_phase = std::time::Instant::now();
    let roles = eval.roles.len();
    // 2. Placement: move whole roles so their contacts meet (weighted by priority and by who adapts).
    let mut offsets = vec![[0.0f32; 3]; roles];
    let cross: Vec<&(Contact, f32)> = eval.contacts.iter().filter(|(c, _)| c.target_role >= 0 && c.target_role as u32 != c.role).collect();
    if roles > 1 && !cross.is_empty() {
        for _ in 0..4 {
            let bodies: Vec<&Skeleton> = sessions.iter().map(|s| s.body_skel.as_ref().unwrap()).collect();
            let mut sum = vec![[0.0f32; 3]; roles];
            let mut wsum = vec![0.0f32; roles];
            for (c, w) in &cross {
                let a = c.role as usize; let b = c.target_role as usize;
                let Some(e) = effector_pos(bodies[a], &c.effector) else { continue };
                let Some(tp) = contact_target(c, e, &bodies, &heights) else { continue };
                let r = vsub(vadd(tp, offsets[b]), vadd(e, offsets[a]));
                // Only what IK cannot cover moves the bodies: limb contacts keep a reach margin.
                let reach = if c.ik && is_limb_end(&c.effector) { 0.35 * heights[a] } else { 0.0 };
                let len = vlen(r);
                if len <= reach { continue; }
                let r = [r[0] * (len - reach) / len, r[1] * (len - reach) / len, r[2] * (len - reach) / len];
                let k = w * c.priority.max(0.05);
                let m = c.target_moves.clamp(0.0, 1.0);
                for i in 0..3 { sum[a][i] += r[i] * k * (1.0 - m); sum[b][i] -= r[i] * k * m; }
                wsum[a] += k * (1.0 - m); wsum[b] += k * m;
            }
            for r in 0..roles {
                if wsum[r] > 1e-6 {
                    for i in 0..3 { offsets[r][i] += 0.7 * sum[r][i] / wsum[r]; }
                    let l = vlen(offsets[r]); let cap = 0.6 * heights[r];
                    if l > cap { for i in 0..3 { offsets[r][i] *= cap / l; } }
                }
            }
        }
        // The bodies get there at a walking pace (1.2 hips heights a second) instead of jumping; a time jump or
        // the first frame places them at once.
        // A new group starts with the bodies where they stand and walks them in; a seek places them at once.
        // (Evaluating without time passing, dt = 0, or jumping in time places them at once.)
        if dt <= 0.0 { player.state.applied_offsets = offsets.clone(); }
        else if player.state.applied_offsets.len() != roles { player.state.applied_offsets = vec![[0.0; 3]; roles]; }
        else if (t - player.state.last_t - dt).abs() > 0.25 { player.state.applied_offsets = offsets.clone(); }
        player.state.remaining = vec![0.0; roles];
        for r in 0..roles {
            let applied = player.state.applied_offsets[r];
            let gap = vsub(offsets[r], applied);
            let len = vlen(gap);
            let step = 1.2 * heights[r] * dt.max(0.0);
            let next = if len <= step || len < 1e-6 { offsets[r] } else { vadd(applied, [gap[0] * step / len, gap[1] * step / len, gap[2] * step / len]) };
            player.state.remaining[r] = vlen(vsub(offsets[r], next)) / heights[r].max(0.1);
            player.state.applied_offsets[r] = next;
            offsets[r] = next;
        }
        for (r, s) in sessions.iter_mut().enumerate().take(roles) {
            if vlen(offsets[r]) < 1e-6 { continue; }
            let anim = &eval.roles[r].anim;
            let body = s.body_skel.as_mut().unwrap();
            let ids = ids_by_name(body);
            if let Some(&hid) = ids.get(&anim.hips_bone) {
                let p = vadd(mat_pos(&crate::pose::world_of_id(hid, &body.tfs)), offsets[r]);
                if let Some(local) = world_to_parent_local(body, hid, p) { body.tfs.get_mut(&hid).unwrap().pos = local; }
            }
        }
    }
    report.offsets = offsets.clone();
    prof::add(1, t_phase);
    // 2a. Walk: roles the placement moves fast walk there.
    let t_phase = std::time::Instant::now();
    let walking = walk(player, &eval, sessions, &heights, dt);
    prof::add(2, t_phase);
    report.walk = walking.iter().copied().fold(0.0, f32::max);
    // 2b. Posture: what placement and the limbs cannot cover, the bodies bend for.
    let t_phase = std::time::Instant::now();
    let min_height = heights.iter().copied().fold(f32::MAX, f32::min);
    let unmet_before = player.state.last_contact_max <= 0.0 || player.state.last_contact_max > 0.015 * min_height || dt <= 0.0;
    if roles > 1 && unmet_before && POSTURE.load(std::sync::atomic::Ordering::Relaxed) { report.posture = posture(&eval, sessions, &heights); }
    prof::add(3, t_phase);
    let t_phase = std::time::Instant::now();
    // 3. Limb IK onto the contact targets (partner bones), then planted feet (a limb on a partner stays there).
    let mut partner_limbs: Vec<(usize, String)> = Vec::new();
    // One contact per limb: the strongest (a partner contact before the body's own links, then priority x weight).
    let mut chosen: HashMap<(usize, i64), (usize, f32)> = HashMap::new();
    for (i, (c, w)) in eval.contacts.iter().enumerate() {
        if !c.ik || c.target_role == -1 || c.role as usize >= roles { continue; }
        let Some((end, _)) = carrying_limb(sessions[c.role as usize].body_skel.as_ref().unwrap(), &c.effector) else { continue };
        // A partner contact outranks the body's own links on the same limb (the partner may be another size).
        let score = c.priority.max(0.05) * w + if c.target_role as u32 != c.role { 100.0 } else { 0.0 };
        let e = chosen.entry((c.role as usize, end)).or_insert((i, score));
        if score > e.1 { *e = (i, score); }
    }
    let mut order: Vec<usize> = chosen.values().map(|v| v.0).collect();
    order.sort();
    // Two rounds: a limb's target may sit on a partner limb that is bent later in the first round.
    let rounds = 2;
    for (round, i) in (0..rounds).flat_map(|r| order.iter().map(move |&i| (r, i))) {
        let (c, w) = &eval.contacts[i];
        let a = c.role as usize;
        // An effector below the limb's end (toes, fingers) turns with the limb: a few passes settle it on the target.
        let mut ok = false;
        let mut end_name = String::from("?");
        for _pass in 0..3 {
            let solve = {
                let bodies: Vec<&Skeleton> = sessions.iter().map(|s| s.body_skel.as_ref().unwrap()).collect();
                let Some(e) = effector_pos(bodies[a], &c.effector) else { break };
                let Some((end, offset)) = carrying_limb(bodies[a], &c.effector) else { break };
                contact_target(c, e, &bodies, &heights).map(|t| (end, vsub(t, offset), vlen(offset) < 1e-4))
            };
            let Some((end, target, at_end)) = solve else { break };
            // The end keeps its world orientation (a foot stays flat, a hand keeps its angle), so what hangs below
            // it (toes, fingers) stays where the offset says.
            let body = sessions[a].body_skel.as_mut().unwrap();
            let end_rot = world_rot(body, end);
            ok = two_bone_ik(body, end, target, *w);
            if ok && !at_end { set_world_rot(body, end, end_rot); }
            end_name = sessions[a].body_skel.as_ref().unwrap().tfs.get(&end).map(|t| t.name.clone()).unwrap_or_default();
            if at_end || !ok { break; }
        }
        if end_name == "?" || round + 1 < rounds { continue; }
        report.ik.push(format!("{}.{}>{}{}", a, c.effector, end_name, if ok { "" } else { " failed" }));
        partner_limbs.push((a, end_name));
    }
    prof::add(4, t_phase);
    let t_phase = std::time::Instant::now();
    report.steps = feet(player, &eval, sessions, &heights, &walking, &partner_limbs, t, dt);
    prof::add(5, t_phase);
    // 4. Error report (after IK), then each character's own finishing pass.
    {
        let bodies: Vec<&Skeleton> = sessions.iter().map(|s| s.body_skel.as_ref().unwrap()).collect();
        let mut errs = Vec::new();
        // Fully active contacts only: while one fades in or out the IK is only partly applied by design.
        for (c, w) in &eval.contacts {
            // Partner contacts: a body's links to itself give way to them on purpose when the bodies differ in size.
            if c.target_role < 0 || c.target_role as u32 == c.role || c.role as usize >= roles || *w < 0.99 { continue; }
            let Some(e) = effector_pos(bodies[c.role as usize], &c.effector) else { continue };
            if let Some(tp) = contact_target(c, e, &bodies, &heights) {
                let d = vlen(vsub(tp, e));
                if errs.iter().all(|x| d > *x) { report.worst = format!("{}.{}->{}.{} ik={} {:.3}", c.role, c.effector, c.target_role, c.target_bone, c.ik, d); }
                errs.push(d);
            }
        }
        report.contacts = errs.len();
        report.contact_max = errs.iter().copied().fold(0.0, f32::max);
        report.contact_mean = if errs.is_empty() { 0.0 } else { errs.iter().sum::<f32>() / errs.len() as f32 };
        player.state.last_contact_max = report.contact_max.max(1e-6);
    }
    // Each character's own finishing pass (shape corrections, expression bones, ABMX, hair / cloth dynamics) is
    // independent of the others: in parallel when there are several.
    let t_phase = std::time::Instant::now();
    if PARALLEL_FINISH.load(std::sync::atomic::Ordering::Relaxed) && eval.roles.len() > 1 {
        std::thread::scope(|scope| {
            for (role, s) in eval.roles.iter().zip(sessions.iter_mut()) {
                let scene = role.anim.scene;
                scope.spawn(move || s.ok_finish_frame(dt, scene));
            }
        });
    } else {
        for (role, s) in eval.roles.iter().zip(sessions.iter_mut()) {
            s.ok_finish_frame(dt, role.anim.scene);
        }
    }
    prof::add(6, t_phase);
    prof::add(11, t_apply);
    clear_ids();
    player.state.last_t = t;
    Ok(report)
}

/// Ground contacts: a planted foot stays where it was planted; when the body has moved it too far away (placement,
/// root motion), it steps to its natural position over a short arc.
fn feet(player: &mut Player, eval: &Eval, sessions: &mut [&mut OkSession], heights: &[f32], walking: &[f32], partner_limbs: &[(usize, String)], t: f32, dt: f32) -> usize {
    let mut steps = 0;
    let jumped = (t - player.state.last_t).abs() > 0.5 || dt <= 0.0;
    let mut active: Vec<(usize, String)> = Vec::new();
    for (c, w) in &eval.contacts {
        if c.target_role != -1 || !is_limb_end(&c.effector) { continue; }
        let r = c.role as usize;
        if r >= sessions.len() { continue; }
        // A walking role's feet follow the walk cycle; a foot on a partner stays on the partner.
        if walking.get(r).copied().unwrap_or(0.0) > 0.3 { continue; }
        if partner_limbs.iter().any(|(pr, n)| *pr == r && *n == c.effector) { continue; }
        let key = (r, c.effector.clone());
        active.push(key.clone());
        let body = sessions[r].body_skel.as_mut().unwrap();
        let Some(natural) = effector_pos(body, &c.effector) else { continue };
        let ground = c.offset[1] * heights[r];
        let natural = [natural[0], natural[1].max(ground), natural[2]];
        let step_len = 0.3 * heights[r];
        let foot = player.state.feet.entry(key).or_insert(Foot { planted: natural, step: None });
        if jumped { *foot = Foot { planted: natural, step: None }; }
        let target = if let Some((from, to, el, dur)) = foot.step {
            let el = el + dt;
            let a = (el / dur).clamp(0.0, 1.0);
            let s = a * a * (3.0 - 2.0 * a);
            let lift = (a * std::f32::consts::PI).sin() * 0.06 * heights[r];
            let p = [from[0] + (to[0] - from[0]) * s, from[1] + (to[1] - from[1]) * s + lift, from[2] + (to[2] - from[2]) * s];
            if a >= 1.0 { foot.planted = to; foot.step = None; } else { foot.step = Some((from, natural, el, dur)); }
            p
        } else if vlen(vsub(foot.planted, natural)) > step_len {
            foot.step = Some((foot.planted, natural, 0.0, 0.25));
            steps += 1;
            foot.planted
        } else {
            foot.planted
        };
        if let Some(&id) = ids_by_name(body).get(&c.effector) { two_bone_ik(body, id, target, *w); }
    }
    player.state.feet.retain(|k, _| active.contains(k));
    steps
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pose::Tf;

    fn skel(arm: &str) -> Skeleton {
        let tf = |name: &str, parent: Option<i64>, pos: [f32; 3]| Tf { name: name.into(), parent, sibling_index: None, pos, rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] };
        let mut tfs = HashMap::new();
        tfs.insert(1, tf("hips", None, [0.0, 1.0, 0.0]));
        tfs.insert(2, tf(arm, Some(1), [0.3, 0.4, 0.0]));
        Skeleton { names: vec![], worlds: vec![], rest: vec![], smr_count: 0, tfs, bone_ids: vec![1, 2], source_mesh: 0 }
    }

    /// A body naming a family bone differently (a mod body's "mod_arm" for "arm") still gets that bone's motion
    /// through the alias table; without it the bone stays at rest.
    #[test]
    fn aliases_pose_renamed_bones() {
        let mut a = super::super::tests_support::two_bone_anim();
        let turn = [(0.25f32).sin(), 0.0, 0.0, (0.25f32).cos()];
        for f in 0..a.frames as usize { a.rot[f * 2 + 1] = turn; }
        let role = super::super::RoleEval { anim: std::sync::Arc::new(a), pose: Default::default(), offset: [0.0; 3], yaw: 0.0 };
        let pose = role.anim.sample(0.0);
        let role = super::super::RoleEval { pose, ..role };
        let rest = skel("mod_arm");
        let mut body = skel("mod_arm");
        write_pose(&mut body, &rest, &role, &HashMap::new());
        assert!(body.tfs[&2].rot[0].abs() < 1e-6, "no alias: the renamed bone keeps its rest");
        let aliases: HashMap<String, String> = [("arm".to_string(), "mod_arm".to_string())].into();
        write_pose(&mut body, &rest, &role, &aliases);
        assert!((body.tfs[&2].rot[0] - turn[0]).abs() < 1e-5, "{:?}", body.tfs[&2].rot);
    }
}
