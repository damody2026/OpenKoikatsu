//! GVHMR SMPL-X world motion -> Koikatsu clip, with automatic repair.
//!
//! Coordinates: SMPL is right-handed Y-up facing +Z; Unity is left-handed.
//! Mirroring X maps one to the other and keeps the character's left on `_L`.
//! Each mapped bone gets W_b(t) = G_j(t) * S_b * W_b(rest); the clip stores
//! absolute locals. Repair (all statistics from the game's own clips):
//! collar scaling, even spine distribution, native joint-limit clamps,
//! temporal smoothing, proportion-aware wrist targets with hinge-elbow IK,
//! KK-leg-derived hips height, foot-contact locking with hinge-knee IK.

use super::math::*;
use super::model::Model;
use super::rig::{mapping, smplx_rest_joints, Align, Rig, SMPL_PARENTS};
use std::collections::HashMap;
use std::path::Path;

pub struct Motion {
    pub global_orient: Vec<V3>,
    pub body_pose: Vec<[V3; 21]>,
    pub transl: Vec<V3>,
    pub betas: Vec<f64>,
    pub fps: f64,
    /// Pair videos: per-frame closest SMPL-X surface distance to the partner (ok-video src/pairalign.rs).
    pub pair_gap: Option<Vec<f64>>,
}

impl Motion {
    pub fn load(path: &Path) -> Result<Motion, String> {
        let m = super::npy::read_npz(path, &["global_orient", "body_pose", "transl", "betas"])?;
        let fps = super::npy::read_npz(path, &["fps"]).ok().and_then(|f| f["fps"].data.first().copied()).unwrap_or(30.);
        let pair_gap = super::npy::read_npz(path, &["pair_gap"]).ok().map(|g| g["pair_gap"].data.clone());
        let n = m["global_orient"].len0();
        let v3 = |a: &super::npy::Array, i: usize| -> V3 { let r = a.row(i); [r[0], r[1], r[2]] };
        let betas_a = &m["betas"];
        let nb = betas_a.data.len() / betas_a.len0().max(1);
        let mut betas = vec![0.; nb];
        for i in 0..betas_a.len0() { for b in 0..nb { betas[b] += betas_a.row(i)[b] / betas_a.len0() as f64; } }
        Ok(Motion {
            global_orient: (0..n).map(|i| v3(&m["global_orient"], i)).collect(),
            body_pose: (0..n).map(|i| { let r = m["body_pose"].row(i); std::array::from_fn(|j| [r[3 * j], r[3 * j + 1], r[3 * j + 2]]) }).collect(),
            transl: (0..n).map(|i| v3(&m["transl"], i)).collect(),
            betas,
            fps,
            pair_gap,
        })
    }
}

#[derive(Clone, Debug)]
pub struct Params {
    pub fps: f64,
    pub fix: bool,
    pub arm_reach: f64,
    pub collar_scale: f64,
    pub smooth: f64,
    pub lock_h: f64,
    pub lock_v: f64,
    pub lock_blend: usize,
    pub extra_lift: Option<Vec<f64>>,
    /// Shared placement for a pair (the anchor's report "frame").
    pub frame: Option<Frame>,
    /// Pair contact fit: per-frame horizontal root offset (world, x/z).
    pub extra_offset: Option<Vec<[f64; 2]>>,
}

#[derive(Clone, Copy, Debug)]
pub struct Frame { pub yaw: f64, pub x0: f64, pub z0: f64, pub ground: f64, pub xz_scale: f64 }
impl Default for Params {
    fn default() -> Self {
        Params { fps: 30., fix: true, arm_reach: 1., collar_scale: 0.5, smooth: 1., lock_h: 0.04, lock_v: 0.35, lock_blend: 3, extra_lift: None, frame: None, extra_offset: None }
    }
}

/// Per-frame expectations for validation.
#[derive(Default)]
pub struct Reference {
    pub contact: [Vec<bool>; 2],
    pub hand_target: [Vec<V3>; 2],
    pub kk_clear: f64,
    pub torso: f64,
}

pub struct Output {
    pub clip: Vec<u8>,
    pub frames: usize,
    pub duration: f64,
    pub reference: Reference,
    pub report: serde_json::Value,
}

// ------------------------------------------------------------------ pose state
pub struct Pose<'a> {
    pub rig: &'a Rig,
    pub world: HashMap<usize, Vec<M3>>,
    pub mapped: std::collections::HashSet<usize>,
    pub frames: usize,
    hips: usize,
    order: Vec<usize>,
}

impl<'a> Pose<'a> {
    fn refresh(&mut self) {
        for &i in &self.order {
            if self.mapped.contains(&i) { continue; }
            if let Some(p) = self.rig.parent(i) {
                if let Some(pw) = self.world.get(&p) {
                    let l = self.rig.local_rot[i];
                    let w: Vec<M3> = pw.iter().map(|m| mul(m, &l)).collect();
                    self.world.insert(i, w);
                }
            }
        }
    }
    /// World position relative to the hips joint.
    pub fn pos(&self, i: usize) -> Vec<V3> {
        let chain = self.rig.chain(i);
        let start = chain.iter().position(|&c| c == self.hips).map(|k| k + 1).unwrap_or(chain.len());
        let mut off = vec![[0.; 3]; self.frames];
        for &c in &chain[start..] {
            let p = self.rig.parent(c).unwrap();
            let o = self.rig.offset(c);
            for (f, v) in off.iter_mut().enumerate() { *v = add(*v, mulv(&self.world[&p][f], o)); }
        }
        off
    }
    pub fn local(&self, i: usize) -> Vec<M3> {
        match self.rig.parent(i) {
            Some(p) => self.world[&i].iter().zip(&self.world[&p]).map(|(w, pw)| tmul(pw, w)).collect(),
            None => self.world[&i].clone(),
        }
    }
    fn set_local(&mut self, i: usize, local: Vec<M3>) {
        let w = match self.rig.parent(i) { Some(p) => self.world[&p].iter().zip(&local).map(|(pw, l)| mul(pw, l)).collect(), None => local };
        self.world.insert(i, w);
        self.refresh();
    }
    fn rest_rel(&self, i: usize) -> Vec<V3> {
        let rest = self.rig.local_rot[i];
        self.local(i).iter().map(|l| log(&tmul(&rest, l))).collect()
    }
    fn set_rest_rel(&mut self, i: usize, rel: &[V3]) {
        let rest = self.rig.local_rot[i];
        self.set_local(i, rel.iter().map(|r| mul(&rest, &exp(*r))).collect());
    }
}

fn scale_joint(p: &mut Pose, name: &str, k: f64) {
    let i = p.rig.idx(name);
    let rel: Vec<V3> = p.rest_rel(i).into_iter().map(|r| scale(r, k)).collect();
    p.set_rest_rel(i, &rel);
}

/// Native per-axis box (0.5..99.5 percentile) on rest-relative rotation vectors.
fn clamp_joints(p: &mut Pose, names: &[&str], model: &Model, margin_deg: f64) -> Vec<bool> {
    let mut hit = vec![false; p.frames];
    let m = margin_deg.to_radians();
    for name in names {
        let Some(lim) = model.limits.get(*name) else { continue };
        let i = p.rig.idx(name);
        let mut rel = p.rest_rel(i);
        let mut changed = false;
        for (f, r) in rel.iter_mut().enumerate() {
            for c in 0..3 {
                let v = r[c].clamp(lim.lo[c] - m, lim.hi[c] + m);
                if (v - r[c]).abs() > 1e-6 { r[c] = v; hit[f] = true; changed = true; }
            }
        }
        if changed { p.set_rest_rel(i, &rel); }
    }
    hit
}

/// Keep the chest's world rotation; spread hips->chest evenly over the spine
/// (KK spine01 pivots 5mm above the hips, SMPL spine1 ~10cm).
fn distribute_spine(p: &mut Pose) {
    let idx: Vec<usize> = ["cf_j_spine01", "cf_j_spine02", "cf_j_spine03"].iter().map(|n| p.rig.idx(n)).collect();
    let top = p.world[&idx[2]].clone();
    let base = p.world[&p.rig.parent(idx[0]).unwrap()].clone();
    let rest = mul(&mul(&p.rig.local_rot[idx[0]], &p.rig.local_rot[idx[1]]), &p.rig.local_rot[idx[2]]);
    let step: Vec<M3> = (0..p.frames).map(|f| exp(scale(log(&tmul(&rest, &tmul(&base[f], &top[f]))), 1. / 3.))).collect();
    for &i in &idx {
        let r = p.rig.local_rot[i];
        p.set_local(i, step.iter().map(|s| mul(&r, s)).collect());
    }
    let below = p.world[&idx[1]].clone();
    p.set_local(idx[2], below.iter().zip(&top).map(|(b, t)| tmul(b, t)).collect());
}

fn smooth_world(p: &mut Pose, names: &[usize], sigma: f64) {
    let r = (3. * sigma).ceil() as isize;
    let w: Vec<f64> = (-r..=r).map(|o| (-0.5 * (o as f64 / sigma).powi(2)).exp()).collect();
    for &i in names {
        let wv = p.world[&i].clone();
        let n = wv.len() as isize;
        let out: Vec<M3> = (0..n).map(|f| {
            let mut acc = [0.; 3];
            let mut tot = 0.;
            for o in -r..=r {
                let g = f + o;
                if g < 0 || g >= n { continue; }
                let k = w[(o + r) as usize];
                acc = add(acc, scale(log(&tmul(&wv[f as usize], &wv[g as usize])), k));
                tot += k;
            }
            mul(&wv[f as usize], &exp(scale(acc, 1. / tot)))
        }).collect();
        p.world.insert(i, out);
    }
    p.refresh();
}

pub fn bend_deg(a: V3, b: V3, c: V3) -> f64 {
    let (u, v) = (sub(b, a), sub(c, b));
    (dot(u, v) / (norm(u) * norm(v))).clamp(-1., 1.).acos().to_degrees()
}

/// Hinge two-bone IK: KK knees bend only about +X, elbows about +Y (L) / -Y
/// (R) in native clips. The end bone keeps its world rotation; the bend plane
/// follows the source elbow/knee once the source bend exceeds the rest bend.
fn limb_ik(p: &mut Pose, root: &str, mid: &str, end: &str, target: &[V3], model: &Model, rest_bend: f64) {
    let (ia, ib, ic) = (p.rig.idx(root), p.rig.idx(mid), p.rig.idx(end));
    let (axis, sign) = match mid { m if m.starts_with("cf_j_leg01") => ([1., 0., 0.], 1.), "cf_j_forearm01_L" => ([0., 1., 0.], 1.), _ => ([0., 1., 0.], -1.) };
    let end_world = p.world[&ic].clone();
    let (a, b, c0) = (p.pos(ia), p.pos(ib), p.pos(ic));
    let (ob, oc) = (p.rig.offset(ib), p.rig.offset(ic));
    let (l1, l2) = (norm(ob), norm(oc));
    let max_bend = model.limits.get(mid).map(|l| l.angle99_5.to_radians()).unwrap_or(155f64.to_radians());
    let rest_b = p.rig.local_rot[ib];
    let mut wa = p.world[&ia].clone();
    let mut wb = vec![I3; p.frames];
    for f in 0..p.frames {
        let to = sub(target[f], a[f]);
        let d = norm(to).clamp((l1 - l2).abs().max(1e-3) + 1e-4, (l1 + l2) * 0.9999);
        let inner = ((l1 * l1 + l2 * l2 - d * d) / (2. * l1 * l2)).clamp(-1., 1.).acos();
        let bend = (std::f64::consts::PI - inner).clamp(0., max_bend);
        let local_b = mul(&rest_b, &exp(scale(axis, sign * bend)));
        let cpos = add(mulv(&wa[f], ob), mulv(&mul(&wa[f], &local_b), oc));
        let u = normalize(to);
        wa[f] = mul(&align(cpos, u), &wa[f]);
        let b1 = mulv(&wa[f], ob);
        let want = sub(b[f], a[f]);
        let p1 = sub(b1, scale(u, dot(b1, u)));
        let p2 = sub(want, scale(u, dot(want, u)));
        if norm(p1) > 1e-3 * l1 && norm(p2) > 1e-3 * l1 {
            let mut ang = dot(cross(p1, p2), u).atan2(dot(p1, p2));
            let src = bend_deg(a[f], b[f], c0[f]);
            ang *= ((src - rest_bend - 10.) / 20.).clamp(0., 1.);
            wa[f] = mul(&exp(scale(u, ang)), &wa[f]);
        }
        wb[f] = mul(&wa[f], &local_b);
    }
    p.world.insert(ia, wa);
    p.world.insert(ib, wb);
    p.world.insert(ic, end_world);
    p.refresh();
}

fn foot_contacts(height: &[f64], horiz: &[[f64; 2]], fps: f64, h: f64, v: f64) -> Vec<bool> {
    let n = height.len();
    let mut c: Vec<bool> = (0..n).map(|f| {
        let speed = if f == 0 { 0. } else { ((horiz[f][0] - horiz[f - 1][0]).powi(2) + (horiz[f][1] - horiz[f - 1][1]).powi(2)).sqrt() * fps };
        height[f] < h && speed < v
    }).collect();
    let mut f = 0;
    while f < n {
        if !c[f] { f += 1; continue; }
        let s = f;
        while f < n && c[f] { f += 1; }
        if f - s < 3 { for x in &mut c[s..f] { *x = false; } }
    }
    c
}

fn lock_targets(ankle: &[V3], contact: &[bool], floor: f64, blend: usize) -> Vec<V3> {
    let n = ankle.len();
    let mut target = ankle.to_vec();
    let mut w = vec![0.; n];
    let mut k = 0;
    while k < n {
        if !contact[k] { k += 1; continue; }
        let s = k;
        while k < n && contact[k] { k += 1; }
        let cnt = (k - s) as f64;
        let pin = [ankle[s..k].iter().map(|a| a[0]).sum::<f64>() / cnt, floor, ankle[s..k].iter().map(|a| a[2]).sum::<f64>() / cnt];
        for f in s..k { target[f] = pin; w[f] = 1.; }
        for b in 1..=blend {
            let a = 1. - b as f64 / (blend + 1) as f64;
            for f in [s as isize - b as isize, (k - 1 + b) as isize] {
                if f >= 0 && (f as usize) < n && !contact[f as usize] && a > w[f as usize] {
                    let f = f as usize;
                    target[f] = add(scale(ankle[f], 1. - a), scale(pin, a));
                    w[f] = a;
                }
            }
        }
    }
    target
}

// ------------------------------------------------------------------ main entry
pub fn retarget(motion: &Motion, rig: &Rig, smplx: &Path, model: &Model, prm: &Params) -> Result<Output, String> {
    let (j, v_min_y) = smplx_rest_joints(smplx, &motion.betas)?;
    // Resample to the output rate (nearest on rotations, linear on translation).
    let n_in = motion.global_orient.len();
    let idx: Vec<f64> = if (motion.fps - prm.fps).abs() > 1e-3 {
        (0..=(((n_in - 1) as f64) / motion.fps * prm.fps) as usize).map(|k| k as f64 * motion.fps / prm.fps).collect()
    } else { (0..n_in).map(|k| k as f64).collect() };
    let frames = idx.len();
    let near = |x: f64| (x.round() as usize).min(n_in - 1);

    // SMPL forward kinematics.
    let mut g = vec![[I3; 22]; frames];
    let mut pp = vec![[[0.; 3]; 22]; frames];
    for (f, &x) in idx.iter().enumerate() {
        let k = near(x);
        let (k0, k1) = (x.floor() as usize, (x.ceil() as usize).min(n_in - 1));
        let a = x - k0 as f64;
        let tr = add(scale(motion.transl[k0], 1. - a), scale(motion.transl[k1], a));
        for jn in 0..22 {
            let local = if jn == 0 { exp(motion.global_orient[k]) } else { exp(motion.body_pose[k][jn - 1]) };
            let par = SMPL_PARENTS[jn];
            if par < 0 { g[f][0] = local; pp[f][0] = add(j[0], tr); } else {
                let pr = par as usize;
                g[f][jn] = mul(&g[f][pr], &local);
                pp[f][jn] = add(pp[f][pr], mulv(&g[f][pr], sub(j[jn], j[pr])));
            }
        }
    }
    // Start at the origin facing +Z, grounded. A pair shares the anchor's
    // frame (prm.frame) so both people keep their relative placement.
    let ankle_clear = j[7][1].min(j[8][1]) - v_min_y;
    let (yaw, p0, ground) = match prm.frame {
        Some(fr) => (fr.yaw, [fr.x0, 0., fr.z0], fr.ground),
        None => {
            let fwd = mulv(&g[0][0], [0., 0., 1.]);
            let yaw = -fwd[0].atan2(fwd[2]);
            let p0 = mulv(&exp([0., yaw, 0.]), pp[0][0]);
            let mut lowest: Vec<f64> = (0..frames).map(|f| { let a = mulv(&exp([0., yaw, 0.]), pp[f][7])[1]; let b = mulv(&exp([0., yaw, 0.]), pp[f][8])[1]; a.min(b) }).collect();
            lowest.sort_by(|a, b| a.total_cmp(b));
            (yaw, p0, lowest[((frames - 1) as f64 * 0.05) as usize] - ankle_clear)
        }
    };
    let ry = exp([0., yaw, 0.]);
    for f in 0..frames { for jn in 0..22 { g[f][jn] = mul(&ry, &g[f][jn]); let q = mulv(&ry, pp[f][jn]); pp[f][jn] = [q[0] - p0[0], q[1], q[2] - p0[2]]; } }
    for f in 0..frames { for jn in 0..22 { pp[f][jn][1] -= ground; } }
    // SMPL -> Unity.
    let gu: Vec<[M3; 22]> = g.iter().map(|r| std::array::from_fn(|k| mirror_rot(&r[k]))).collect();
    let mut pu: Vec<[V3; 22]> = pp.iter().map(|r| std::array::from_fn(|k| mirror_pos(r[k]))).collect();
    let ju: [V3; 22] = std::array::from_fn(|k| mirror_pos(j[k]));

    let kk_leg = rig.pos("cf_j_hips")[1] - 0.5 * (rig.pos("cf_j_leg03_L")[1] + rig.pos("cf_j_leg03_R")[1]);
    let smpl_leg = ju[0][1] - 0.5 * (ju[7][1] + ju[8][1]);
    let scale_k = kk_leg / smpl_leg;

    // Alignments and initial world rotations.
    let map = mapping();
    let mut s_align: HashMap<&str, M3> = HashMap::new();
    for (name, jn, al) in &map {
        match al {
            Align::Identity => { s_align.insert(name, I3); }
            Align::Direction(child, kchild) => { s_align.insert(name, align(sub(rig.pos(kchild), rig.pos(name)), sub(ju[*child], ju[*jn]))); }
            Align::Inherit(_) => {}
        }
    }
    for (name, _, al) in &map { if let Align::Inherit(o) = al { let m = s_align[o]; s_align.insert(name, m); } }
    let mapped: HashMap<usize, usize> = map.iter().map(|(n, jn, _)| (rig.idx(n), *jn)).collect();
    let mut needed: Vec<usize> = mapped.keys().flat_map(|&i| rig.chain(i)).collect();
    needed.sort();
    needed.dedup();
    let mut world: HashMap<usize, Vec<M3>> = HashMap::new();
    for &i in &needed {
        let w: Vec<M3> = if let Some(&jn) = mapped.get(&i) {
            let sw = mul(&s_align[rig.bones[i].name.as_str()], &rig.world_rot[i]);
            gu.iter().map(|r| mul(&r[jn], &sw)).collect()
        } else if let Some(p) = rig.parent(i) {
            world[&p].iter().map(|m| mul(m, &rig.local_rot[i])).collect()
        } else { vec![rig.world_rot[i]; frames] };
        world.insert(i, w);
    }
    let mut pose = Pose { rig, world, mapped: mapped.keys().copied().collect(), frames, hips: rig.idx("cf_j_hips"), order: needed.clone() };

    // ---- repair: torso
    let mut report = serde_json::Map::new();
    let torso = ["cf_j_spine01", "cf_j_spine02", "cf_j_spine03", "cf_j_neck", "cf_j_head", "cf_j_shoulder_L", "cf_j_shoulder_R"];
    if prm.fix {
        if prm.collar_scale != 1. { for s in ["cf_j_shoulder_L", "cf_j_shoulder_R"] { scale_joint(&mut pose, s, prm.collar_scale); } }
        distribute_spine(&mut pose);
        let hit = clamp_joints(&mut pose, &torso, model, 3.);
        report.insert("clamped_torso_frames".into(), hit.iter().filter(|h| **h).count().into());
        if prm.smooth > 0. {
            let all: Vec<usize> = needed.iter().copied().filter(|i| pose.mapped.contains(i)).collect();
            smooth_world(&mut pose, &all, prm.smooth);
            for jn in 0..22 {
                let col: Vec<V3> = pu.iter().map(|r| r[jn]).collect();
                for (f, v) in smooth_v3(&col, prm.smooth).into_iter().enumerate() { pu[f][jn] = v; }
            }
        }
    }
    let p_src = |f: usize, jn: usize| mirror_pos(pu[f][jn]);  // back to SMPL space (grounded)

    // ---- arms: proportion-aware wrist targets, hinge elbows
    let torso_kk = rig.pos("cf_j_arm00_L")[1] - rig.pos("cf_j_thigh00_L")[1];
    let torso_smpl = ju[16][1] - ju[1][1];
    let mut hand_targets: [Vec<V3>; 2] = [vec![], vec![]];
    for (si, side) in ["L", "R"].iter().enumerate() {
        let (js, je, jw) = if si == 0 { (16, 18, 20) } else { (17, 19, 21) };
        let a = pose.pos(rig.idx(&format!("cf_j_arm00_{side}")));
        let kk_arm = norm(rig.offset(rig.idx(&format!("cf_j_forearm01_{side}")))) + norm(rig.offset(rig.idx(&format!("cf_j_hand_{side}"))));
        let smpl_arm = norm(sub(ju[je], ju[js])) + norm(sub(ju[jw], ju[je]));
        let k_angles = kk_arm / smpl_arm;
        let k = (1. - prm.arm_reach) * k_angles + prm.arm_reach * torso_kk / torso_smpl;
        hand_targets[si] = (0..frames).map(|f| {
            let mut v = scale(sub(pu[f][jw], pu[f][js]), k_angles);
            if prm.arm_reach > 0. && v[1] < 0. { v[1] *= k / k_angles; }
            add(a[f], v)
        }).collect();
        if prm.fix || prm.arm_reach > 0. {
            limb_ik(&mut pose, &format!("cf_j_arm00_{side}"), &format!("cf_j_forearm01_{side}"), &format!("cf_j_hand_{side}"),
                    &hand_targets[si].clone(), model, bend_deg(ju[js], ju[je], ju[jw]));
        }
    }

    // ---- hips position (KK legs decide the height) and feet
    let hips = rig.idx("cf_j_hips");
    let kk_clear = 0.5 * (rig.pos("cf_j_leg03_L")[1] + rig.pos("cf_j_leg03_R")[1]);
    // Horizontal placement scale; a pair uses the anchor's so their spacing stays consistent.
    let xz_scale = prm.frame.map(|fr| fr.xz_scale).unwrap_or(scale_k);
    let mut target: Vec<V3> = (0..frames).map(|f| [pu[f][0][0] * xz_scale, 0., pu[f][0][2] * xz_scale]).collect();
    // Pair contact fit, before the foot locks so planted feet move with it.
    if let Some(off) = &prm.extra_offset { for f in 0..frames { if let Some(o) = off.get(f) { target[f][0] += o[0]; target[f][2] += o[1]; } } }
    let mut hy = vec![f64::NEG_INFINITY; frames];
    for (si, side) in ["L", "R"].iter().enumerate() {
        let jn = if si == 0 { 7 } else { 8 };
        let ank = pose.pos(rig.idx(&format!("cf_j_leg03_{side}")));
        for f in 0..frames { hy[f] = hy[f].max(kk_clear + scale_k * (p_src(f, jn)[1] - ankle_clear) - ank[f][1]); }
    }
    if prm.fix && prm.smooth > 0. { hy = smooth_f(&hy, 2. * prm.smooth); }
    for f in 0..frames { target[f][1] = hy[f]; }
    let mut contacts: [Vec<bool>; 2] = [vec![false; frames], vec![false; frames]];
    if prm.fix {
        for (si, side) in ["L", "R"].iter().enumerate() {
            let jn = if si == 0 { 7 } else { 8 };
            let rel = pose.pos(rig.idx(&format!("cf_j_leg03_{side}")));
            let ankle: Vec<V3> = (0..frames).map(|f| add(target[f], rel[f])).collect();
            let height: Vec<f64> = (0..frames).map(|f| p_src(f, jn)[1] - ankle_clear).collect();
            let horiz: Vec<[f64; 2]> = (0..frames).map(|f| { let q = p_src(f, jn); [q[0], q[2]] }).collect();
            let c = foot_contacts(&height, &horiz, prm.fps, prm.lock_h, prm.lock_v);
            let mut want = if c.iter().any(|x| *x) { lock_targets(&ankle, &c, kk_clear, prm.lock_blend) } else { ankle.clone() };
            if prm.smooth > 0. {
                let sm = smooth_v3(&want, 1.5 * prm.smooth);
                for f in 0..frames { if !c[f] { want[f] = sm[f]; } }
            }
            let rel_t: Vec<V3> = (0..frames).map(|f| sub(want[f], target[f])).collect();
            let jl = if si == 0 { (1, 4, 7) } else { (2, 5, 8) };
            limb_ik(&mut pose, &format!("cf_j_thigh00_{side}"), &format!("cf_j_leg01_{side}"), &format!("cf_j_leg03_{side}"),
                    &rel_t, model, bend_deg(ju[jl.0], ju[jl.1], ju[jl.2]));
            contacts[si] = c;
        }
        report.insert("contact_frames".into(), serde_json::json!({"L": contacts[0].iter().filter(|x| **x).count(), "R": contacts[1].iter().filter(|x| **x).count()}));
        let ends = ["cf_j_hand_L", "cf_j_hand_R", "cf_j_leg03_L", "cf_j_leg03_R", "cf_j_toes_L", "cf_j_toes_R"];
        let hit = clamp_joints(&mut pose, &ends, model, 3.);
        report.insert("clamped_end_frames".into(), hit.iter().filter(|h| **h).count().into());
        let (al, ar) = (pose.pos(rig.idx("cf_j_leg03_L")), pose.pos(rig.idx("cf_j_leg03_R")));
        for f in 0..frames {
            let low = al[f][1].min(ar[f][1]) + target[f][1];
            target[f][1] += (kk_clear - 0.005 - low).max(0.);
        }
    }
    if let Some(lift) = &prm.extra_lift { for f in 0..frames { target[f][1] += lift.get(f).copied().unwrap_or(0.); } }


    // ---- tracks
    let hp = rig.parent(hips).unwrap();
    // World targets -> locals through the parent's full rest transform
    // (cf_n_height carries the card height scale).
    let hips_local: Vec<V3> = target.iter().map(|t| rig.to_local(hp, *t)).collect();
    let mut binds: Vec<(String, u32)> = vec![(rig.anim_path(hips), 1)];
    let mut cols: Vec<Vec<Vec<f64>>> = vec![hips_local.iter().map(|v| v.to_vec()).collect()];
    let mut locals: HashMap<String, Vec<M3>> = HashMap::new();
    for (name, _, _) in &map {
        let i = rig.idx(name);
        let l = pose.local(i);
        let mut q: Vec<[f64; 4]> = l.iter().map(to_quat).collect();
        continuous(&mut q);
        binds.push((rig.anim_path(i), 2));
        cols.push(q.iter().map(|x| x.to_vec()).collect());
        // Native clips key unit scale on every joint; without it the joints
        // keep the reset pose's scale (e.g. hands 1/0.9/0.9).
        binds.push((rig.anim_path(i), 3));
        cols.push(vec![vec![1., 1., 1.]; frames]);
        locals.insert(name.to_string(), l);
    }
    // Studio PVCopy targets, as native Studio clips key them: the game copies
    // cf_pv_hand/leg world position, rotation and scale onto the hands and
    // ankles (scale 1 also undoes the 0.9 hand correction, like native).
    // PVCopy takes their world transform, so positions go through cf_pv_root's
    // full rest transform (cf_n_height scale included).
    for (pv, end) in [("cf_pv_hand_L", "cf_j_hand_L"), ("cf_pv_hand_R", "cf_j_hand_R"), ("cf_pv_leg_L", "cf_j_leg03_L"), ("cf_pv_leg_R", "cf_j_leg03_R")] {
        let (Some(&ip), ie) = (rig.index.get(pv), rig.idx(end)) else { continue };
        let rel = pose.pos(ie);
        let pp = rig.parent(ip).unwrap();
        binds.push((rig.anim_path(ip), 1));
        cols.push((0..frames).map(|f| rig.to_local(pp, add(target[f], rel[f])).to_vec()).collect());
        let mut q: Vec<[f64; 4]> = pose.world[&ie].iter().map(to_quat).collect();
        continuous(&mut q);
        binds.push((rig.anim_path(ip), 2));
        cols.push(q.iter().map(|x| x.to_vec()).collect());
        binds.push((rig.anim_path(ip), 3));
        cols.push(vec![vec![1., 1., 1.]; frames]);
    }
    let mut helper_tracks = 0;
    for (bone, attr, values) in super::model::predict_helpers(model, rig, &locals) {
        let Some(&i) = rig.index.get(&bone) else { continue };
        binds.push((rig.anim_path(i), attr));
        cols.push(values);
        helper_tracks += 1;
    }
    let mut samples = Vec::with_capacity(frames * cols.iter().map(|c| c[0].len()).sum::<usize>());
    for f in 0..frames { for c in &cols { samples.extend(c[f].iter().map(|v| *v as f32)); } }
    let clip = crate::anim::build_dense_clip(&binds, frames, prm.fps as f32, samples)?;
    report.insert("helper_tracks".into(), helper_tracks.into());
    report.insert("scale".into(), scale_k.into());
    report.insert("frame".into(), serde_json::json!({"yaw": yaw, "x0": p0[0], "z0": p0[2], "ground": ground, "xz_scale": xz_scale}));
    Ok(Output {
        clip,
        frames,
        duration: (frames.max(2) - 1) as f64 / prm.fps,
        reference: Reference { contact: contacts, hand_target: hand_targets, kk_clear, torso: rig.pos("cf_j_neck")[1] - rig.pos("cf_j_hips")[1] },
        report: serde_json::Value::Object(report),
    })
}
