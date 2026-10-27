//! What the GPU needs to pose a character each frame from shared clips (Shaders/GpuRig.usf): her body skeleton (the
//! shaped baseline locals, ABMX, the limb-size corrections, the bones ExpressionBone reads and writes, PV copies), every
//! skin palette matrix as "one body bone's world x a constant" (found over a clip, physics off), and a shared clip as flat
//! channels (every bone component it writes, at 60 Hz, one copy for every character). Physics is not part of it.

use crate::pose::{euler_deg_to_quat, Mat4, Skeleton};
use std::collections::HashMap;

/// One ABMX bone modifier (BoneModifier.Apply on the bone's locals at that point of the frame).
#[derive(Clone, Debug)]
pub struct AbmOp { pub bone: usize, pub rot: [f32; 4], pub length: f32, pub pos: [f32; 3], pub scale: [f32; 3] }

/// The bones ExpressionBone and the limb corrections read or write (-1: the rig lacks it). `first`/`last`: the game's
/// two duplicate-name policies, as each branch uses them (pose.rs rig_name_index).
#[derive(Clone, Debug, Default)]
pub struct Named { pub first: HashMap<String, i32>, pub last: HashMap<String, i32> }
impl Named {
    pub fn f(&self, n: &str) -> i32 { *self.first.get(n).unwrap_or(&-1) }
    pub fn l(&self, n: &str) -> i32 { *self.last.get(n).unwrap_or(&-1) }
}

#[derive(Clone, Debug)]
pub struct FlatRig {
    pub names: Vec<String>, pub ids: Vec<i64>,
    /// Parent index, always before the bone (-1: a root; a parent outside the rig counts as the identity, as world_of).
    pub parent: Vec<i32>,
    /// The shaped baseline (body_shaped): every frame starts from it.
    pub pos: Vec<[f32; 3]>, pub rot: Vec<[f32; 4]>, pub scale: Vec<[f32; 3]>,
    pub female: bool,
    /// ShapeBodyInfoFemale.UpdateAlways: these channels come back from the baseline after the clip (bone, axis).
    pub always: Vec<(usize, usize)>,
    /// The limb-size correction targets (every bone of these names, as the session does).
    pub forearms: Vec<usize>, pub hands: Vec<usize>,
    pub pv: Vec<(usize, usize)>,
    pub abm: Vec<AbmOp>,
    pub n: Named,
}

/// A shared clip as flat channels: every bone component the clip writes, at `rate` Hz.
#[derive(Clone, Debug, Default)]
pub struct SharedClip {
    pub bones: Vec<String>,
    /// Bit 0..2 position x/y/z, 3 rotation, 4..6 scale x/y/z.
    pub mask: Vec<u8>,
    pub frames: usize, pub rate: f32, pub duration: f32,
    /// [channel][frame] position, rotation, scale.
    pub samples: Vec<Vec<([f32; 3], [f32; 4], [f32; 3])>>,
    pub limb_arm: bool, pub limb_hand: bool, pub expression: bool,
}



impl FlatRig {
    pub fn from_session(s: &crate::session::OkSession) -> Option<Self> {
        let base = s.body_shaped.as_ref()?;
        let body = s.body_skel.as_ref()?;
        // Parents first: a depth-first walk from the roots, children in id order (any order works; it is fixed).
        let mut children: HashMap<i64, Vec<i64>> = HashMap::new();
        let mut roots = Vec::new();
        for (id, tf) in &base.tfs {
            match tf.parent.filter(|p| *p != 0 && *p != *id && base.tfs.contains_key(p)) {
                Some(p) => children.entry(p).or_default().push(*id),
                None => roots.push(*id),
            }
        }
        roots.sort();
        for list in children.values_mut() { list.sort(); }
        let mut order = Vec::new();
        let mut stack: Vec<i64> = roots.into_iter().rev().collect();
        while let Some(id) = stack.pop() {
            order.push(id);
            if let Some(list) = children.get(&id) { for c in list.iter().rev() { stack.push(*c); } }
        }
        let index: HashMap<i64, usize> = order.iter().enumerate().map(|(i, id)| (*id, i)).collect();
        let mut r = FlatRig { names: vec![], ids: order.clone(), parent: vec![], pos: vec![], rot: vec![], scale: vec![], female: s.sex == 1,
            always: vec![], forearms: vec![], hands: vec![], pv: vec![], abm: vec![], n: Named::default() };
        for id in &order {
            let tf = &base.tfs[id];
            r.names.push(tf.name.clone());
            r.parent.push(tf.parent.filter(|p| *p != 0 && *p != *id).and_then(|p| index.get(&p)).map_or(-1, |i| *i as i32));
            r.pos.push(tf.pos); r.rot.push(tf.rot); r.scale.push(tf.scale);
        }
        let first = crate::pose::rig_name_index(body, false);
        let last = crate::pose::rig_name_index(body, true);
        for (name, id) in first.iter() { if let Some(i) = index.get(id) { r.n.first.insert(name.clone(), *i as i32); } }
        for (name, id) in last.iter() { if let Some(i) = index.get(id) { r.n.last.insert(name.clone(), *i as i32); } }
        let roots = crate::shape::tree_ids(base);
        for (id, tf) in &base.tfs {
            let axis = match tf.name.as_str() { "cf_d_kokan" => 2, "cf_d_shoulder_L" | "cf_d_shoulder_R" if std::env::var("OK_NOFIX").is_err() => 0, _ => continue };
            if roots.as_ref().is_some_and(|ids| !ids.contains(id)) { continue; }
            r.always.push((index[id], axis));
        }
        r.always.sort();
        for (i, name) in r.names.iter().enumerate() {
            match name.as_str() { "cf_j_forearm01_L" | "cf_j_forearm01_R" => r.forearms.push(i), "cf_j_hand_L" | "cf_j_hand_R" => r.hands.push(i), _ => {} }
        }
        if let Some(config) = &s.pv_config {
            if !(s.pv_fk || !s.user_fk.is_empty()) {
                for (k, pair) in config.pairs.iter().enumerate() {
                    if k >= 4 || s.pv_mask & (1 << k) == 0 { continue; }
                    if let (Some(a), Some(b)) = (crate::pv::resolve(body, &pair[0]), crate::pv::resolve(body, &pair[1])) {
                        if let (Some(a), Some(b)) = (index.get(&a), index.get(&b)) { r.pv.push((*a, *b)); }
                    }
                }
            }
        }
        for b in &s.abm {
            let d = b.for_coord(0);
            if d.is_empty() { continue; }
            let Some(id) = first.get(&b.name) else { continue };
            let Some(i) = index.get(id) else { continue };
            r.abm.push(AbmOp { bone: *i, rot: euler_deg_to_quat(d.rot), length: d.length, pos: d.pos, scale: d.scale });
        }
        Some(r)
    }
}

impl SharedClip {
    /// The session's current shared clip as flat channels, sampled at `rate` through the session's own clip sampler.
    pub fn capture(s: &crate::session::OkSession, rate: f32) -> Option<Self> {
        let base = s.body_shaped.as_ref()?;
        let duration = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
        if !(duration > 0.) { return None; }
        // Which components the clip writes: start from a sentinel and see what changed.
        const SENTINEL: f32 = 12345.678;
        let mut probe: Skeleton = base.clone();
        for tf in probe.tfs.values_mut() { tf.pos = [SENTINEL; 3]; tf.rot = [SENTINEL; 4]; tf.scale = [SENTINEL; 3]; }
        crate::anim::crossfade::sample(&s.game_root, &mut probe, &s.anim_clip, 0., &s.anim_parameters, s.anim_clock);
        let mut written: Vec<(i64, String, u8)> = Vec::new();
        for (id, tf) in &probe.tfs {
            let mut m = 0u8;
            for k in 0..3 { if tf.pos[k] != SENTINEL { m |= 1 << k; } if tf.scale[k] != SENTINEL { m |= 16 << k; } }
            if tf.rot.iter().any(|v| *v != SENTINEL) { m |= 8; }
            if m != 0 { written.push((*id, tf.name.clone(), m)); }
        }
        written.sort_by(|a, b| a.1.cmp(&b.1).then(a.0.cmp(&b.0)));
        let frames = (duration * rate).ceil() as usize + 1;
        let (arm, hand) = crate::anim::clip_limb_corrections_mode(&s.game_root, &s.anim_clip, s.anim_scene_mode);
        let mut clip = SharedClip { bones: written.iter().map(|w| w.1.clone()).collect(), mask: written.iter().map(|w| w.2).collect(),
            frames, rate, duration, samples: vec![Vec::with_capacity(frames); written.len()],
            limb_arm: arm, limb_hand: hand, expression: crate::anim::clip_expression_bone_mode(&s.game_root, &s.anim_clip, s.anim_scene_mode) };
        let mut sk = base.clone();
        for k in 0..frames {
            sk.restore_from(base);
            crate::anim::crossfade::sample(&s.game_root, &mut sk, &s.anim_clip, (k as f32 / rate).min(duration), &s.anim_parameters, s.anim_clock);
            for (c, (id, _, _)) in written.iter().enumerate() {
                let tf = &sk.tfs[id];
                clip.samples[c].push((tf.pos, tf.rot, tf.scale));
            }
        }
        Some(clip)
    }

    /// Channel -> bone of a rig, by name (the rig's first bone of that name).
    pub fn map_to(&self, rig: &FlatRig) -> Vec<i32> {
        self.bones.iter().map(|name| rig.n.f(name)).collect()
    }
}

/// The bones the GPU's ExpressionBone and AcsNull read or write, in the order Shaders/GpuRig.usf indexes them. The first
/// list resolves by the first bone of each name (the arms, the leg positions, AcsNull), the second by the last (the
/// elbows, hands, leg twists, bust), as pose.rs does.
pub const NAMED_FIRST: [&str; 26] = [
    "cf_j_shoulder_L", "cf_j_shoulder_R", "cf_j_forearm01_L", "cf_j_forearm01_R", "cf_j_arm00_L", "cf_j_arm00_R",
    "cf_d_shoulder02_L", "cf_d_shoulder02_R", "cf_d_arm01_L", "cf_d_arm01_R", "cf_d_arm02_L", "cf_d_arm02_R", "cf_d_arm03_L", "cf_d_arm03_R",
    "cf_j_waist02", "cf_j_thigh00_L", "cf_j_thigh00_R", "cf_j_leg01_L", "cf_j_leg01_R", "cf_n_height",
    "cf_s_leg_L", "cf_s_leg_R", "cf_d_siri_L", "cf_d_siri_R", "cf_j_head", "a_n_neck",
];
pub const NAMED_LAST: [&str; 40] = [
    "cf_j_forearm01_L", "cf_j_forearm01_R", "cf_s_elbo_L", "cf_s_elbo_R", "cf_s_elboback_L", "cf_s_elboback_R",
    "cf_j_hand_L", "cf_j_hand_R", "cf_j_middle01_L", "cf_j_middle01_R", "cf_d_hand_L", "cf_d_hand_R", "cf_d_wrist_L", "cf_d_wrist_R",
    "cf_d_forearm02_L", "cf_d_forearm02_R", "cf_j_thigh00_L", "cf_j_thigh00_R",
    "cf_d_thigh01_L", "cf_d_thigh01_R", "cf_d_thigh02_L", "cf_d_thigh02_R", "cf_d_thigh03_L", "cf_d_thigh03_R",
    "cf_j_leg01_L", "cf_j_leg01_R", "cf_s_kneeB_L", "cf_s_kneeB_R", "cf_d_kneeF_L", "cf_d_kneeF_R",
    "cf_j_leg03_L", "cf_j_leg03_R", "cf_d_leg02_L", "cf_d_leg02_R", "cf_j_spine03", "cf_d_bust00",
    "cf_j_bust02_L", "cf_j_bust02_R", "cf_d_hit_bust_L", "cf_d_hit_bust_R",
];

/// One skin palette matrix of a part: the body bone (-1: none, the identity) and C, with P(t) = W(t) * C.
#[derive(Clone, Debug)]
pub struct PaletteTerm { pub node: i32, pub c: Mat4, pub error: f32 }

/// How far two affine matrices move points of a part (Unity units): origin and points 0.2 away along each axis.
pub(crate) fn distance_pub(a: &Mat4, b: &Mat4) -> f32 { distance(a, b) }
fn distance(a: &Mat4, b: &Mat4) -> f32 {
    let mut worst = 0f32;
    for p in [[0f32, 0., 0.], [0.2, 0., 0.], [0., 0.2, 0.], [0., 0., 0.2]] {
        let mut d2 = 0f32;
        for r in 0..3 {
            let va = a[r][0] * p[0] + a[r][1] * p[1] + a[r][2] * p[2] + a[r][3];
            let vb = b[r][0] * p[0] + b[r][1] * p[1] + b[r][2] * p[2] + b[r][3];
            d2 += (va - vb) * (va - vb);
        }
        worst = worst.max(d2.sqrt());
    }
    worst
}

/// A part's skin palette as the session builds it now (ffi ok_part_skin_palette).
pub(crate) fn part_palette(s: &mut crate::session::OkSession, part: usize) -> Option<Vec<Mat4>> {
    unsafe {
        let session = s as *mut crate::session::OkSession;
        let count = crate::ffi::ok_part_skin_palette(session, part as i32, std::ptr::null_mut(), std::ptr::null_mut(), 0);
        if count <= 0 { return None; }
        let mut view = std::mem::zeroed::<crate::ffi::OkSkinPaletteView>();
        let mut flat = vec![0f32; count as usize * 16];
        if crate::ffi::ok_part_skin_palette(session, part as i32, &mut view, flat.as_mut_ptr(), count) != count { return None; }
        Some(flat.chunks(16).map(|c| [[c[0], c[1], c[2], c[3]], [c[4], c[5], c[6], c[7]], [c[8], c[9], c[10], c[11]], [c[12], c[13], c[14], c[15]]]).collect())
    }
}

/// Every part's palette as (body bone, constant), found over `samples` times of the session's clip with physics off:
/// for each matrix, the body bone whose world carries the matrix from the first sample to the others best. A body-skinned
/// part's matrix j is its skeleton's bone j (ok_part_skin_palette): that bone is taken when it carries the matrix, since
/// bones that keep still relative to each other over the samples (a hand and its fingers over a walk) carry it equally
/// and the first of them is not the one another clip moves.
pub fn factor_palettes(s: &mut crate::session::OkSession, rig: &FlatRig, samples: &[f32]) -> Vec<Vec<PaletteTerm>> {
    let mut palettes: Vec<Vec<Option<Vec<Mat4>>>> = Vec::new();
    let mut worlds: Vec<Vec<Mat4>> = Vec::new();
    for &t in samples {
        // Accessories export their palettes only once the frame's dynamics ran (zero step: nothing moves).
        s.sample_paired_frame(t);
        s.finish_paired_dynamics(0.);
        let body = s.body_skel.as_ref().unwrap();
        worlds.push(rig.ids.iter().map(|id| crate::pose::world_of_id(*id, &body.tfs)).collect());
        palettes.push((0..s.parts.len()).map(|p| part_palette(s, p)).collect());
    }
    let inverses: Vec<Option<Mat4>> = worlds[0].iter().map(crate::pose::mat_inverse_checked).collect();
    let rig_index: HashMap<i64, usize> = rig.ids.iter().enumerate().map(|(n, id)| (*id, n)).collect();
    let body_ids = s.body_skel.as_ref().map(|b| b.bone_ids.clone()).unwrap_or_default();
    let carry = |n: usize, p: usize, j: usize, first: &Mat4, limit: f32| -> Option<(Mat4, f32)> {
        let inv = inverses[n].as_ref()?;
        let c = crate::pose::mat_mul_pub(inv, first);
        let mut err = 0f32;
        for k in 1..samples.len() {
            err = err.max(distance(&crate::pose::mat_mul_pub(&worlds[k][n], &c), &palettes[k][p].as_ref().unwrap()[j]));
            if err >= limit { break; }
        }
        Some((c, err))
    };
    let mut out = Vec::with_capacity(s.parts.len());
    for p in 0..s.parts.len() {
        let Some(first) = palettes[0][p].as_ref() else { out.push(Vec::new()); continue };
        if palettes.iter().any(|f| f[p].as_ref().map_or(true, |m| m.len() != first.len())) { out.push(Vec::new()); continue; }
        let mut terms = Vec::with_capacity(first.len());
        let own = s.parts[p].runtime_accessory.is_none() && s.parts[p].skel_kind == 1 && s.parts[p].parent_bone.is_none();
        // An accessory skinned onto the body: matrix j follows the body bone of its slot's name.
        let named: Option<Vec<Option<usize>>> = s.parts[p].runtime_accessory.as_ref().and_then(|r| r.body_palette_names()).map(|names| {
            let index = s.body_skel.as_ref().map(|b| crate::pose::rig_name_index(b, false));
            names.iter().map(|n| index.as_ref().and_then(|i| i.get(n)).and_then(|id| rig_index.get(id).copied())).collect()
        });
        for j in 0..first.len() {
            let structural = if own { body_ids.get(j).and_then(|id| rig_index.get(id)).copied() }
                else { named.as_ref().and_then(|n| n.get(j).copied().flatten()) };
            if let Some(n) = structural {
                if let Some((c, err)) = carry(n, p, j, &first[j], f32::INFINITY) {
                    if err < 1e-5 { terms.push(PaletteTerm { node: n as i32, c, error: err }); continue; }
                }
            }
            // A matrix that does not move (no bone, the identity world) first, then every body bone.
            let still = (1..samples.len()).map(|k| distance(&palettes[k][p].as_ref().unwrap()[j], &first[j])).fold(0f32, f32::max);
            let mut best = PaletteTerm { node: -1, c: first[j], error: still };
            for n in 0..inverses.len() {
                if best.error < 1e-6 { break; }
                let Some((c, err)) = carry(n, p, j, &first[j], best.error) else { continue };
                if err < best.error { best = PaletteTerm { node: n as i32, c, error: err }; }
            }
            terms.push(best);
        }
        out.push(terms);
    }
    out
}

fn push_u32(b: &mut Vec<u8>, v: u32) { b.extend_from_slice(&v.to_le_bytes()); }
fn push_i32(b: &mut Vec<u8>, v: i32) { b.extend_from_slice(&v.to_le_bytes()); }
fn push_f32s(b: &mut Vec<u8>, v: &[f32]) { for x in v { b.extend_from_slice(&x.to_le_bytes()); } }
fn push_name(b: &mut Vec<u8>, s: &str) { push_u32(b, s.len() as u32); b.extend_from_slice(s.as_bytes()); while b.len() % 4 != 0 { b.push(0); } }

impl FlatRig {
    pub fn named_first(&self) -> Vec<i32> { NAMED_FIRST.iter().map(|n| self.n.f(n)).collect() }
    pub fn named_last(&self) -> Vec<i32> { NAMED_LAST.iter().map(|n| self.n.l(n)).collect() }

    /// "OKGR" v1, little-endian 4-byte words (KKRuntimeGpuRig reads it):
    /// header: magic, version, bones, flags (1 female), always, forearms, hands, pv, abm, named first, named last, parts;
    /// bones: parent (i32), pos 3, rot 4, scale 3; always: (bone, axis); forearms; hands; pv: (src, dst);
    /// abm: bone, rot 4, length, pos 3, scale 3; named first / last (i32, -1 none);
    /// parts: count, then per palette matrix node (i32, -1 identity), error, C rows 0..2 (Unity, row-major);
    /// bone names: length, utf-8 bytes, padded to 4.
    pub fn to_bytes(&self, palettes: &[Vec<PaletteTerm>]) -> Vec<u8> {
        let (nf, nl) = (self.named_first(), self.named_last());
        let mut b = Vec::new();
        for v in [u32::from_le_bytes(*b"OKGR"), 1, self.names.len() as u32, self.female as u32, self.always.len() as u32, self.forearms.len() as u32,
            self.hands.len() as u32, self.pv.len() as u32, self.abm.len() as u32, nf.len() as u32, nl.len() as u32, palettes.len() as u32] { push_u32(&mut b, v); }
        for i in 0..self.names.len() { push_i32(&mut b, self.parent[i]); push_f32s(&mut b, &self.pos[i]); push_f32s(&mut b, &self.rot[i]); push_f32s(&mut b, &self.scale[i]); }
        for (bone, axis) in &self.always { push_u32(&mut b, *bone as u32); push_u32(&mut b, *axis as u32); }
        for i in &self.forearms { push_u32(&mut b, *i as u32); }
        for i in &self.hands { push_u32(&mut b, *i as u32); }
        for (a, c) in &self.pv { push_u32(&mut b, *a as u32); push_u32(&mut b, *c as u32); }
        for op in &self.abm { push_u32(&mut b, op.bone as u32); push_f32s(&mut b, &op.rot); push_f32s(&mut b, &[op.length]); push_f32s(&mut b, &op.pos); push_f32s(&mut b, &op.scale); }
        for v in nf.iter().chain(nl.iter()) { push_i32(&mut b, *v); }
        for part in palettes {
            push_u32(&mut b, part.len() as u32);
            for t in part { push_i32(&mut b, t.node); push_f32s(&mut b, &[t.error]); for r in 0..3 { push_f32s(&mut b, &t.c[r]); } }
        }
        for name in &self.names { push_name(&mut b, name); }
        b
    }
}

impl SharedClip {
    /// "OKGC" v1, little-endian 4-byte words: magic, version, channels, frames, rate, duration, flags (1 limb arm,
    /// 2 limb hand, 4 ExpressionBone); per channel mask and bone name (length, utf-8, padded to 4); then samples,
    /// [channel][frame] position 3, rotation 4, scale 3.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut b = Vec::new();
        push_u32(&mut b, u32::from_le_bytes(*b"OKGC")); push_u32(&mut b, 1); push_u32(&mut b, self.bones.len() as u32); push_u32(&mut b, self.frames as u32);
        push_f32s(&mut b, &[self.rate, self.duration]);
        push_u32(&mut b, self.limb_arm as u32 | (self.limb_hand as u32) << 1 | (self.expression as u32) << 2);
        for (name, mask) in self.bones.iter().zip(&self.mask) { push_u32(&mut b, *mask as u32); push_name(&mut b, name); }
        for channel in &self.samples { for (p, q, s) in channel { push_f32s(&mut b, p); push_f32s(&mut b, q); push_f32s(&mut b, s); } }
        b
    }
}

fn install(s: &mut crate::session::OkSession, clip: &[u8]) -> Result<(), String> {
    let (root, key) = crate::anim::install_shared_clip(clip).map_err(|e| format!("clip: {e}"))?;
    s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
    s.sample_paired_frame(0.);
    Ok(())
}

/// The GPU rig of a session, using a shared clip only to tell each palette matrix's bone (the session is left posed by
/// it). Also answers how many matrices follow no single bone exactly (> 0.1 mm).
pub fn export_rig(s: &mut crate::session::OkSession, clip: &[u8]) -> Result<(Vec<u8>, usize), String> {
    install(s, clip)?;
    let rig = FlatRig::from_session(s).ok_or("no shaped body skeleton")?;
    let duration = crate::anim::clip_duration(&s.game_root, &s.anim_clip).max(0.1);
    let palettes = factor_palettes(s, &rig, &[0., duration * 0.31, duration * 0.62, duration * 0.87]);
    let loose = palettes.iter().flatten().filter(|t| t.error > 1e-4).count();
    Ok((rig.to_bytes(&palettes), loose))
}

/// A shared clip as flat channels, sampled through a session (any character: the channels are absolute locals).
pub fn export_clip(s: &mut crate::session::OkSession, clip: &[u8], rate: f32) -> Result<Vec<u8>, String> {
    install(s, clip)?;
    Ok(SharedClip::capture(s, rate).ok_or("clip has no duration or no shaped body")?.to_bytes())
}

// The C entries (include/ok_extract.h). Both follow the size-then-copy protocol of the other resource exports: a null
// `out` with `cap` 0 answers the size; the result is kept for the copy call on the same thread, so it is built once.
thread_local! {static LAST: std::cell::RefCell<Option<(usize, u8, u64, Vec<u8>)>> = const { std::cell::RefCell::new(None) };}

fn clip_key(bytes: &[u8]) -> u64 {
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    bytes.hash(&mut h);
    h.finish()
}

unsafe fn export_entry(s: *mut crate::session::OkSession, clip: *const u8, len: i32, out: *mut u8, cap: i32, kind: u8) -> i32 {
    if s.is_null() || clip.is_null() || len <= 0 || len > 128 * 1024 * 1024 { return -1; }
    let bytes = std::slice::from_raw_parts(clip, len as usize);
    let key = (s as usize, kind, clip_key(bytes));
    let cached = LAST.with(|l| l.borrow().as_ref().filter(|c| (c.0, c.1, c.2) == key).map(|c| c.3.clone()));
    let result = match cached {
        Some(b) => b,
        None => {
            let session = &mut *s;
            let session = std::panic::AssertUnwindSafe(session);
            let built = crate::run_no_panic(move || { let session = session; match kind { 0 => export_rig(session.0, bytes).map(|r| r.0), _ => export_clip(session.0, bytes, 60.) } });
            match built { Ok(b) => b, Err(e) => { crate::ffi::ffi_log(1, &format!("ok-extract: GPU rig export: {e}")); return -1; } }
        }
    };
    let n = crate::ffi::copy_resource(&result, out, cap);
    LAST.with(|l| *l.borrow_mut() = if out.is_null() { Some((key.0, key.1, key.2, result)) } else { None });
    n
}

/// The GPU rig of a session ("OKGR", see FlatRig::to_bytes); `clip` is a shared clip used to tell each palette matrix's
/// bone (installed into the session, which is left posed by it).
#[no_mangle]
pub unsafe extern "C" fn ok_session_gpu_rig(s: *mut crate::session::OkSession, clip: *const u8, len: i32, out: *mut u8, cap: i32) -> i32 {
    export_entry(s, clip, len, out, cap, 0)
}

/// A shared clip as GPU channels ("OKGC", see SharedClip::to_bytes), sampled at 60 Hz through the session.
#[no_mangle]
pub unsafe extern "C" fn ok_session_gpu_clip(s: *mut crate::session::OkSession, clip: *const u8, len: i32, out: *mut u8, cap: i32) -> i32 {
    export_entry(s, clip, len, out, cap, 1)
}
