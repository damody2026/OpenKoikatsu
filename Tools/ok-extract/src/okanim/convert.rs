//! Sources -> OK animation.
//!
//! * KK: the Animator's own output (no body shape, corrections, IK or physics) sampled on a character, expressed
//!   against the unshaped rest (`body_rest`), so the result carries no trace of that character's body.
//! * Any skeleton (the Unreal exporter, retargeted motion): absolute local rotations of named bones plus that
//!   skeleton's rest, expressed the same way.
//! * Group contacts: detected from the characters' final (IK'd) poses, as semantic "bone -> partner bone" links.

use super::{Contact, OkAnim, Track};
use crate::finalik::{qdot, qinv, qmul, qnorm, qrot, vlen, vsub, Quat, V3};
use crate::pose::Skeleton;
use crate::session::OkSession;
use std::collections::{BTreeMap, HashMap};

pub const FAMILY: &str = "kk.cf_j";

/// The Animator layer of a KK skeleton: every `cf_j_*` bone, parents before children.
pub fn family_tracks(rest: &Skeleton) -> Vec<(i64, Track)> {
    let depth = |mut id: i64| { let mut d = 0; while let Some(p) = rest.tfs.get(&id).and_then(|t| t.parent) { if !rest.tfs.contains_key(&p) { break; } id = p; d += 1; } d };
    let mut ids: Vec<i64> = rest.tfs.iter().filter(|(_, t)| t.name.starts_with("cf_j_")).map(|(id, _)| *id).collect();
    ids.sort_by(|a, b| depth(*a).cmp(&depth(*b)).then_with(|| rest.tfs[a].name.cmp(&rest.tfs[b].name)));
    let mut seen = std::collections::HashSet::new();
    ids.into_iter().filter(|id| seen.insert(rest.tfs[id].name.clone())).map(|id| {
        let tf = &rest.tfs[&id];
        let parent = tf.parent.and_then(|p| rest.tfs.get(&p)).map(|p| p.name.clone()).unwrap_or_default();
        (id, Track { bone: tf.name.clone(), parent, rest_local: tf.rot, rest_world: crate::pose::world_rotation_of(id, &rest.tfs) })
    }).collect()
}

fn continuous(prev: Option<Quat>, q: Quat) -> Quat {
    match prev { Some(p) if qdot(p, q) < 0.0 => [-q[0], -q[1], -q[2], -q[3]], _ => q }
}

/// What converting a KK clip needs from a character, detached from the session (worker threads convert with it).
#[derive(Clone)]
pub struct ClipSampler {
    pub game: std::path::PathBuf,
    /// The pose every frame starts from (the character's shaped baseline), and the unshaped rest tracks are expressed against.
    pub base: Skeleton,
    pub rest: Skeleton,
    pub params: HashMap<String, f32>,
    pub clock: crate::anim::crossfade::Clock,
}

impl ClipSampler {
    pub fn of_session(s: &mut OkSession) -> Result<Self, String> {
        // The shaped baselines are made on demand (as a frame would).
        s.ok_begin_frame(s.anim_time);
        let base = s.body_shaped.clone().or_else(|| s.body_skel.clone()).ok_or("character has no skeleton")?;
        let rest = s.body_rest.clone().or_else(|| s.body_shaped.clone()).ok_or("character has no rest skeleton")?;
        if s.game_root.as_os_str().is_empty() { return Err("no game root".into()); }
        Ok(Self { game: s.game_root.clone(), base, rest, params: s.anim_parameters.clone(), clock: s.anim_clock })
    }
}

/// Samples the selected clip (`key`, with the session's parameters) frame by frame and records the Animator output.
pub fn from_session_clip(s: &mut OkSession, key: &str, fps: f32) -> Result<OkAnim, String> {
    if !(fps > 0.0) { return Err("fps must be positive".into()); }
    s.anim_clip = key.to_string();
    s.anim_time = 0.0;
    let duration = s.anim_duration();
    let sampler = ClipSampler::of_session(s)?;
    from_clip(&sampler, key, fps, Some(duration))
}

/// A KK clip through a detached sampler (any thread): the Animator output frame by frame against the unshaped rest.
pub fn from_clip(sampler: &ClipSampler, key: &str, fps: f32, duration: Option<f32>) -> Result<OkAnim, String> {
    from_clip_with(sampler, key, fps, duration, &sampler.params)
}

/// The same with the Animator float parameters `params` in place of the sampler's (a blend tree sampled at one point).
pub fn from_clip_with(sampler: &ClipSampler, key: &str, fps: f32, duration: Option<f32>, params: &HashMap<String, f32>) -> Result<OkAnim, String> {
    if !(fps > 0.0) { return Err("fps must be positive".into()); }
    let duration = duration.unwrap_or_else(|| crate::anim::clip_duration_parameters(&sampler.game, key, params));
    if !(duration > 0.0) || !duration.is_finite() { return Err(format!("{key}: no duration")); }
    let rest = sampler.rest.clone();
    let mut skel = sampler.base.clone();
    let tracks = family_tracks(&rest);
    let hips_id = tracks.iter().find(|(_, t)| t.bone == "cf_j_hips").map(|(id, _)| *id).ok_or("no cf_j_hips")?;
    let hips_rest = rest.tfs[&hips_id].pos;
    let hips_len = vlen(hips_rest).max(1e-4);
    let hips_parent_rest_world = rest.tfs[&hips_id].parent.map(|p| crate::pose::world_rotation_of(p, &rest.tfs)).unwrap_or([0.0, 0.0, 0.0, 1.0]);
    let frames = ((duration * fps).round() as usize).max(1) + 1;
    let looping = crate::anim::clip_looping(&sampler.game, key);
    let mut rot = Vec::with_capacity(frames * tracks.len());
    let mut hips = Vec::with_capacity(frames);
    let mut last: Vec<Option<Quat>> = vec![None; tracks.len()];
    // KK IK targets (cf_pv_*): their positions in the hips' parent frame, in hips heights, as curves pv.<bone>.x|y|z.
    let hips_parent_name = tracks.iter().find(|(_, t)| t.bone == "cf_j_hips").map(|(_, t)| t.parent.clone()).unwrap_or_default();
    let mut pv_names: Vec<String> = skel.tfs.values().filter(|t| t.name.starts_with("cf_pv_")).map(|t| t.name.clone()).collect();
    pv_names.sort();
    pv_names.dedup();
    let mut pv_values: Vec<Vec<f32>> = vec![Vec::with_capacity(frames); pv_names.len() * 3];
    for f in 0..frames {
        // The last frame of a loop is its first: sample just before the wrap.
        let t = (f as f32 / fps).min(duration - if looping { 1e-4 } else { 0.0 });
        skel.restore_from(&sampler.base);
        crate::anim::crossfade::sample(&sampler.game, &mut skel, key, t, params, sampler.clock);
        skel.rebuild_worlds();
        let body = &skel;
        let ids: HashMap<&str, i64> = body.tfs.iter().map(|(id, t)| (t.name.as_str(), *id)).collect();
        for (k, (_, track)) in tracks.iter().enumerate() {
            let local = ids.get(track.bone.as_str()).and_then(|id| body.tfs.get(id)).map(|t| t.rot).unwrap_or(track.rest_local);
            let d = continuous(last[k], qnorm(qmul(qinv(track.rest_local), local)));
            last[k] = Some(d);
            rot.push(d);
        }
        let p = ids.get("cf_j_hips").and_then(|id| body.tfs.get(id)).map(|t| t.pos).unwrap_or(hips_rest);
        hips.push([p[0] / hips_len, p[1] / hips_len, p[2] / hips_len]);
        if !pv_names.is_empty() {
            let (frame_rot, frame_pos) = match ids.get(hips_parent_name.as_str()) {
                Some(&pid) => { let m = crate::pose::world_of_id(pid, &body.tfs); (crate::pose::world_rotation_of(pid, &body.tfs), [m[0][3], m[1][3], m[2][3]]) }
                None => ([0.0, 0.0, 0.0, 1.0], [0.0; 3]),
            };
            let inv = qinv(frame_rot);
            for (k, name) in pv_names.iter().enumerate() {
                let w = ids.get(name.as_str()).map(|&id| { let m = crate::pose::world_of_id(id, &body.tfs); [m[0][3], m[1][3], m[2][3]] }).unwrap_or(frame_pos);
                let l = qrot(inv, vsub(w, frame_pos));
                for c in 0..3 { pv_values[k * 3 + c].push(l[c] / hips_len); }
            }
        }
    }
    let curves: Vec<super::Curve> = pv_names.iter().enumerate().flat_map(|(k, n)| ["x", "y", "z"].into_iter().enumerate().map(move |(c, a)| (k * 3 + c, format!("pv.{n}.{a}"))))
        .map(|(i, name)| super::Curve { name, values: std::mem::take(&mut pv_values[i]) }).collect();
    let mut meta = BTreeMap::new();
    meta.insert("source".into(), "kk".into());
    meta.insert("source_key".into(), key.to_string());
    meta.insert("name".into(), key.rsplit(['|', '.', '/']).next().unwrap_or(key).to_string());
    let anim = OkAnim {
        family: FAMILY.into(), fps, frames: frames as u32, looping, scene: crate::anim::clip_scene_code(&sampler.game, key),
        hips_bone: "cf_j_hips".into(), hips_rest, hips_parent_rest_world,
        tracks: tracks.into_iter().map(|(_, t)| t).collect(), rot, hips, curves, contacts: Vec::new(), meta,
    };
    anim.validate()?;
    Ok(anim)
}

/// Records a character's final pose frame by frame (whatever drives it: animation, FK, IK, an OK Studio timeline) as
/// an OK animation, with the tracks of `from_clip` (deltas from the unshaped rest; hips in hips heights).
pub struct PoseRecorder {
    tracks: Vec<(i64, Track)>,
    hips_rest: V3,
    hips_len: f32,
    hips_parent_rest_world: Quat,
    last: Vec<Option<Quat>>,
    rot: Vec<Quat>,
    hips: Vec<V3>,
    pub frames: u32,
    pub bytes: Option<Vec<u8>>,
}

impl PoseRecorder {
    pub fn start(s: &OkSession) -> Result<Self, String> {
        let rest = s.body_rest.as_ref().or(s.body_shaped.as_ref()).ok_or("character has no rest skeleton")?;
        let tracks = family_tracks(rest);
        let hips_id = tracks.iter().find(|(_, t)| t.bone == "cf_j_hips").map(|(id, _)| *id).ok_or("no cf_j_hips")?;
        let hips_rest = rest.tfs[&hips_id].pos;
        let hips_parent_rest_world = rest.tfs[&hips_id].parent.map(|p| crate::pose::world_rotation_of(p, &rest.tfs)).unwrap_or([0.0, 0.0, 0.0, 1.0]);
        let n = tracks.len();
        Ok(Self { tracks, hips_rest, hips_len: vlen(hips_rest).max(1e-4), hips_parent_rest_world, last: vec![None; n], rot: Vec::new(), hips: Vec::new(), frames: 0, bytes: None })
    }

    /// The session's current body pose as the next frame.
    pub fn push(&mut self, s: &OkSession) -> Result<(), String> {
        let body = s.body_skel.as_ref().ok_or("character has no skeleton")?;
        let ids: HashMap<&str, i64> = body.tfs.iter().map(|(id, t)| (t.name.as_str(), *id)).collect();
        for (k, (_, track)) in self.tracks.iter().enumerate() {
            let local = ids.get(track.bone.as_str()).and_then(|id| body.tfs.get(id)).map(|t| t.rot).unwrap_or(track.rest_local);
            let d = continuous(self.last[k], qnorm(qmul(qinv(track.rest_local), local)));
            self.last[k] = Some(d);
            self.rot.push(d);
        }
        let p = ids.get("cf_j_hips").and_then(|id| body.tfs.get(id)).map(|t| t.pos).unwrap_or(self.hips_rest);
        self.hips.push([p[0] / self.hips_len, p[1] / self.hips_len, p[2] / self.hips_len]);
        self.frames += 1;
        Ok(())
    }

    pub fn finish(&self, fps: f32, looping: bool, name: &str) -> Result<OkAnim, String> {
        if self.frames < 2 { return Err("record at least 2 frames".into()); }
        if !(fps > 0.0) { return Err("fps must be positive".into()); }
        let mut meta = BTreeMap::new();
        meta.insert("source".into(), "okstudio".into());
        meta.insert("name".into(), name.to_string());
        let anim = OkAnim {
            family: FAMILY.into(), fps, frames: self.frames, looping, scene: 0,
            hips_bone: "cf_j_hips".into(), hips_rest: self.hips_rest, hips_parent_rest_world: self.hips_parent_rest_world,
            tracks: self.tracks.iter().map(|(_, t)| t.clone()).collect(), rot: self.rot.clone(), hips: self.hips.clone(),
            curves: Vec::new(), contacts: Vec::new(), meta,
        };
        anim.validate()?;
        Ok(anim)
    }
}

/// The unshaped rest of a character's Animator layer as a `LocalsHeader` JSON: the target for `sample_onto` and the
/// source header for `from_locals`, from the same data `from_session_clip` expresses its tracks against.
pub fn session_rest_header(s: &OkSession) -> Result<String, String> {
    let rest = s.body_rest.as_ref().or(s.body_shaped.as_ref()).ok_or("character has no rest skeleton")?;
    let tracks = family_tracks(rest);
    let family: std::collections::HashSet<i64> = tracks.iter().map(|(id, _)| *id).collect();
    let bones: Vec<serde_json::Value> = tracks.into_iter().map(|(id, t)| {
        let m = crate::pose::world_of_id(id, &rest.tfs);
        let world_pos = [m[0][3], m[1][3], m[2][3]];
        // Nearest family bone above it in the real skeleton (its parent may be a helper bone outside the family).
        let mut anchor = String::new();
        let mut p = rest.tfs[&id].parent;
        let mut guard = 0;
        while let Some(pid) = p {
            if family.contains(&pid) { anchor = rest.tfs[&pid].name.clone(); break; }
            p = rest.tfs.get(&pid).and_then(|t| t.parent);
            guard += 1;
            if guard > 512 { break; }
        }
        serde_json::json!({ "name": t.bone, "parent": t.parent, "anchor": anchor, "rest_local": t.rest_local, "rest_world": t.rest_world,
            "rest_pos": rest.tfs[&id].pos, "world_pos": world_pos })
    }).collect();
    Ok(serde_json::json!({ "family": FAMILY, "fps": 30.0, "frames": 1, "hips": "cf_j_hips", "sex": s.sex, "bones": bones }).to_string())
}

/// Header of a source skeleton for `from_locals` (the Unreal exporter / retarget output), in Unity local space.
#[derive(serde::Deserialize)]
pub struct LocalsHeader {
    #[serde(default = "default_family")] pub family: String,
    pub fps: f32,
    pub frames: u32,
    #[serde(default)] pub looping: bool,
    #[serde(default)] pub scene: i32,
    pub bones: Vec<LocalsBone>,
    #[serde(default = "default_hips")] pub hips: String,
    #[serde(default)] pub name: String,
    #[serde(default)] pub meta: BTreeMap<String, String>,
    /// The family's standard rest (e.g. `session_rest_header`): when given, this skeleton's rest is first aligned to it
    /// (see `align_rest`), so an A-pose source or target plays a T-pose family animation correctly and vice versa.
    #[serde(default)] pub align_to: Option<Box<LocalsHeader>>,
}
#[derive(serde::Deserialize, Clone)]
pub struct LocalsBone {
    pub name: String,
    #[serde(default)] pub parent: String,
    pub rest_local: Quat,
    pub rest_world: Quat,
    #[serde(default)] pub rest_pos: V3,
    /// Rest position in the skeleton's space; derived from `rest_pos` along the parents when absent.
    #[serde(default)] pub world_pos: Option<V3>,
}

pub(crate) fn rest_world_positions(h: &LocalsHeader) -> HashMap<String, (Quat, V3)> {
    fn visit(i: usize, h: &LocalsHeader, index: &HashMap<&str, usize>, out: &mut HashMap<String, (Quat, V3)>, depth: usize) -> V3 {
        let b = &h.bones[i];
        if let Some(&(_, p)) = out.get(&b.name) { return p; }
        let p = match (b.world_pos, index.get(b.parent.as_str())) {
            (Some(w), _) => w,
            (None, Some(&pi)) if depth < 512 && pi != i => {
                let pp = visit(pi, h, index, out, depth + 1);
                crate::finalik::vadd(pp, qrot(h.bones[pi].rest_world, b.rest_pos))
            }
            _ => b.rest_pos,
        };
        out.insert(b.name.clone(), (b.rest_world, p));
        p
    }
    let index: HashMap<&str, usize> = h.bones.iter().enumerate().map(|(i, b)| (b.name.as_str(), i)).collect();
    let mut out: HashMap<String, (Quat, V3)> = HashMap::new();
    for i in 0..h.bones.len() { visit(i, h, &index, &mut out, 0); }
    out
}

/// Aligns a skeleton's rest to a family's standard rest, like an IK retargeter's auto-align: top-down, every bone is
/// swung so the direction to its chain child (the family child that best continues the parent->bone direction, e.g.
/// spine03 -> neck, hand -> middle01) matches the family's. Only directions change; each bone's twist stays its own
/// (the carry between rests absorbs twist exactly). Bones the family lacks keep their parent's swing.
pub fn align_rest(h: &mut LocalsHeader, family: &LocalsHeader) {
    use crate::finalik::{qfrom_to_rotation, vdot, vnorm};
    let fam = rest_world_positions(family);
    let src = rest_world_positions(h);
    let fam_children = |name: &str| family.bones.iter().filter(|b| b.parent == name).map(|b| b.name.clone()).collect::<Vec<_>>();
    let mut corr: HashMap<String, Quat> = HashMap::new();
    let mut aligned: HashMap<String, Quat> = HashMap::new();
    let depth = |i: usize| {
        let mut d = 0;
        let mut p = h.bones[i].parent.clone();
        while let Some(b) = h.bones.iter().find(|b| b.name == p) { d += 1; p = b.parent.clone(); if d > 512 { break; } }
        d
    };
    let depths: Vec<usize> = (0..h.bones.len()).map(depth).collect();
    let mut order: Vec<usize> = (0..h.bones.len()).collect();
    order.sort_by_key(|&i| depths[i]);
    for &i in &order {
        let b = h.bones[i].clone();
        let parent_corr = corr.get(&b.parent).copied().unwrap_or([0.0, 0.0, 0.0, 1.0]);
        let mut c = parent_corr;
        if let (Some(&(_, fp)), Some(&(_, sp))) = (fam.get(&b.name), src.get(&b.name)) {
            // Continuation direction: from the parent in the family rest, or up for a root.
            let along = fam.get(&b.parent).map(|&(_, pp)| vnorm(vsub(fp, pp))).filter(|v| vlen(*v) > 0.5).unwrap_or([0.0, 1.0, 0.0]);
            let child = fam_children(&b.name).into_iter()
                .filter(|c| src.contains_key(c))
                .filter_map(|c| { let d = vsub(fam[&c].1, fp); (vlen(d) > 1e-4).then(|| { let score = vdot(vnorm(d), along); (c, score) }) })
                .max_by(|a, b| a.1.total_cmp(&b.1)).map(|(c, _)| c);
            if let Some(ch) = child {
                let d_src = qrot(parent_corr, vsub(src[&ch].1, sp));
                let d_fam = vsub(fam[&ch].1, fp);
                if vlen(d_src) > 1e-5 && vlen(d_fam) > 1e-5 { c = qnorm(qmul(qfrom_to_rotation(d_src, d_fam), parent_corr)); }
            }
        }
        corr.insert(b.name.clone(), c);
        aligned.insert(b.name.clone(), qnorm(qmul(c, b.rest_world)));
    }
    for b in h.bones.iter_mut() {
        let w = aligned[&b.name];
        b.rest_local = match aligned.get(&b.parent) { Some(&pw) => qnorm(qmul(qinv(pw), w)), None => w };
        b.rest_world = w;
    }
}
fn default_family() -> String { FAMILY.into() }
fn default_hips() -> String { "cf_j_hips".into() }

/// Absolute local rotations (`frames x bones` quaternions, then `frames` hips positions) of a named skeleton -> OK.
pub fn from_locals(header_json: &str, rotations: &[f32], hips_positions: &[f32]) -> Result<OkAnim, String> {
    let mut h: LocalsHeader = serde_json::from_str(header_json).map_err(|e| format!("OK export header: {e}"))?;
    if let Some(f) = h.align_to.take() { align_rest(&mut h, &f); }
    let (n, f) = (h.bones.len(), h.frames as usize);
    if n == 0 || f == 0 { return Err("OK export: no bones or frames".into()); }
    if rotations.len() != n * f * 4 { return Err("OK export: rotation count mismatch".into()); }
    if !hips_positions.is_empty() && hips_positions.len() != f * 3 { return Err("OK export: hips count mismatch".into()); }
    let hips_bone = h.bones.iter().find(|b| b.name == h.hips).ok_or("OK export: hips bone missing")?;
    let hips_len = vlen(hips_bone.rest_pos).max(1e-4);
    let hips_parent_rest_world = h.bones.iter().find(|b| b.name == hips_bone.parent).map(|b| b.rest_world).unwrap_or([0.0, 0.0, 0.0, 1.0]);
    let mut rot = Vec::with_capacity(n * f);
    let mut last: Vec<Option<Quat>> = vec![None; n];
    for fr in 0..f {
        for (k, b) in h.bones.iter().enumerate() {
            let o = (fr * n + k) * 4;
            let local = qnorm([rotations[o], rotations[o + 1], rotations[o + 2], rotations[o + 3]]);
            let d = continuous(last[k], qnorm(qmul(qinv(b.rest_local), local)));
            last[k] = Some(d);
            rot.push(d);
        }
    }
    let hips = (0..f).map(|fr| if hips_positions.is_empty() { hips_bone.rest_pos.map(|v| v / hips_len) } else {
        [hips_positions[fr * 3] / hips_len, hips_positions[fr * 3 + 1] / hips_len, hips_positions[fr * 3 + 2] / hips_len] }).collect();
    let mut meta = h.meta;
    if !h.name.is_empty() { meta.insert("name".into(), h.name.clone()); }
    let anim = OkAnim {
        family: h.family, fps: h.fps, frames: h.frames, looping: h.looping, scene: h.scene,
        hips_bone: h.hips.clone(), hips_rest: hips_bone.rest_pos, hips_parent_rest_world,
        tracks: h.bones.iter().map(|b| Track { bone: b.name.clone(), parent: b.parent.clone(), rest_local: b.rest_local, rest_world: b.rest_world }).collect(),
        rot, hips, curves: Vec::new(), contacts: Vec::new(), meta,
    };
    anim.validate()?;
    Ok(anim)
}

/// Bones that act as contact effectors (with their surface radius, m at a 0.9 m hips height).
pub const EFFECTORS: [(&str, f32); 10] = [("cf_j_hand_L", 0.03), ("cf_j_hand_R", 0.03), ("cf_j_leg03_L", 0.04), ("cf_j_leg03_R", 0.04), ("cf_j_head", 0.09),
    ("cf_j_hips", 0.12), ("cf_j_spine03", 0.12), ("cf_j_toes_L", 0.03), ("cf_j_toes_R", 0.03), ("cf_j_kokan", 0.03)];
/// Partner body parts as segments (bone -> child, or a point when the child is empty) with their surface radius.
pub const TARGETS: [(&str, &str, f32); 22] = [
    ("cf_j_head", "", 0.10), ("cf_j_neck", "cf_j_head", 0.05),
    ("cf_j_spine03", "cf_j_neck", 0.12), ("cf_j_spine02", "cf_j_spine03", 0.13), ("cf_j_spine01", "cf_j_spine02", 0.12), ("cf_j_hips", "cf_j_spine01", 0.13),
    ("cf_j_kokan", "", 0.04),
    ("cf_j_shoulder_L", "cf_j_arm00_L", 0.05), ("cf_j_shoulder_R", "cf_j_arm00_R", 0.05),
    ("cf_j_arm00_L", "cf_j_forearm01_L", 0.045), ("cf_j_arm00_R", "cf_j_forearm01_R", 0.045),
    ("cf_j_forearm01_L", "cf_j_hand_L", 0.035), ("cf_j_forearm01_R", "cf_j_hand_R", 0.035),
    ("cf_j_hand_L", "", 0.035), ("cf_j_hand_R", "", 0.035),
    ("cf_j_thigh00_L", "cf_j_leg01_L", 0.08), ("cf_j_thigh00_R", "cf_j_leg01_R", 0.08),
    ("cf_j_leg01_L", "cf_j_leg03_L", 0.055), ("cf_j_leg01_R", "cf_j_leg03_R", 0.055),
    ("cf_j_leg03_L", "cf_j_toes_L", 0.04), ("cf_j_leg03_R", "cf_j_toes_R", 0.04),
    ("cf_j_waist01", "", 0.12),
];
const REFERENCE_HIPS: f32 = 0.9;

fn segment_distance(e: V3, a: V3, b: Option<V3>) -> f32 {
    let Some(b) = b else { return vlen(vsub(e, a)) };
    let ab = vsub(b, a);
    let l2 = crate::finalik::vdot(ab, ab);
    let s = if l2 > 1e-10 { (crate::finalik::vdot(vsub(e, a), ab) / l2).clamp(0.0, 1.0) } else { 0.0 };
    vlen(vsub(e, [a[0] + ab[0] * s, a[1] + ab[1] * s, a[2] + ab[2] * s]))
}

/// Detects contacts in recorded group frames: for every role's effector, the partner body part whose surface is within
/// `reach` of the effector's surface (segment distance minus both radii, scaled by each body's size) for at least
/// `min_time`; and planted feet. Offsets are the effector's position in the target bone's frame, averaged over the run
/// and normalised by the target's hips height.
pub fn detect_contacts(frames: &[Vec<HashMap<String, (V3, Quat)>>], heights: &[f32], fps: f32, reach: f32, min_time: f32) -> Vec<Contact> {
    let mut out = Vec::new();
    if frames.is_empty() { return out; }
    let roles = frames[0].len();
    let scale = |r: usize| heights.get(r).copied().unwrap_or(REFERENCE_HIPS).max(0.1) / REFERENCE_HIPS;
    for a in 0..roles {
        for (eff, eff_r) in EFFECTORS {
            for b in 0..roles {
                if b == a { continue; }
                let mut run: Option<(usize, Vec<V3>, String)> = None;
                let flush = |run: &mut Option<(usize, Vec<V3>, String)>, end: usize, out: &mut Vec<Contact>| {
                    if let Some((start, offs, bone)) = run.take() {
                        let dur = (end - start) as f32 / fps;
                        if dur >= min_time && !offs.is_empty() {
                            let mut o = [0.0; 3];
                            for v in &offs { for i in 0..3 { o[i] += v[i] / offs.len() as f32; } }
                            let h = heights.get(b).copied().unwrap_or(1.0).max(0.1);
                            let limb = eff.starts_with("cf_j_hand") || eff.starts_with("cf_j_leg03") || eff.starts_with("cf_j_toes");
                            // The run's frames span [start, end): sample the track at the run's end time too.
                            let mut track: Vec<V3> = offs.iter().map(|v| v.map(|x| x / h)).collect();
                            if let Some(last) = track.last().copied() { track.push(last); }
                            out.push(Contact { role: a as u32, effector: eff.into(), target_role: b as i32, target_bone: bone, offset: o.map(|v| v / h),
                                start: start as f32 / fps, end: end as f32 / fps, blend: 0.15f32.min(dur * 0.25), weight: 1.0,
                                // Core contacts (hips, crotch, chest) define the interaction; hands and feet follow.
                                priority: if limb { 1.0 } else { 2.0 }, ik: limb, target_moves: 0.5, track });
                        }
                    }
                };
                for (f, frame) in frames.iter().enumerate() {
                    let Some(&(e, _)) = frame[a].get(eff) else { flush(&mut run, f, &mut out); continue };
                    let best = TARGETS.iter().filter_map(|(t, child, r)| {
                        let (p, q) = frame[b].get(*t)?;
                        let c = if child.is_empty() { None } else { frame[b].get(*child).map(|x| x.0) };
                        Some((*t, segment_distance(e, *p, c) - r * scale(b) - eff_r * scale(a), *p, *q))
                    }).min_by(|x, y| x.1.total_cmp(&y.1));
                    match best {
                        Some((t, d, p, r)) if d <= reach => {
                            let local = qrot(qinv(r), vsub(e, p));
                            match &mut run {
                                Some((_, offs, bone)) if bone == t => offs.push(local),
                                _ => { flush(&mut run, f, &mut out); run = Some((f, vec![local], t.to_string())); }
                            }
                        }
                        _ => flush(&mut run, f, &mut out),
                    }
                }
                flush(&mut run, frames.len(), &mut out);
            }
        }
        // Planted feet on the ground: low and slow for a while.
        for eff in ["cf_j_leg03_L", "cf_j_leg03_R"] {
            let mut start: Option<usize> = None;
            let mut height = 0.0;
            for f in 0..=frames.len() {
                let planted = f < frames.len() && f > 0 && frames[f][a].get(eff).zip(frames[f - 1][a].get(eff)).is_some_and(|((p, _), (q, _))| {
                    let speed = vlen(vsub(*p, *q)) * fps;
                    p[1] < 0.15 * heights.get(a).copied().unwrap_or(1.0) && speed < 0.25
                });
                if planted { if start.is_none() { start = Some(f); height = frames[f][a][eff].0[1]; } continue; }
                if let Some(s0) = start.take() {
                    let dur = (f - s0) as f32 / fps;
                    if dur >= min_time {
                        let h = heights.get(a).copied().unwrap_or(1.0).max(0.1);
                        out.push(Contact { role: a as u32, effector: eff.into(), target_role: -1, target_bone: String::new(), offset: [0.0, height / h, 0.0],
                            start: s0 as f32 / fps, end: f as f32 / fps, blend: 0.1f32.min(dur * 0.25), weight: 1.0, priority: 1.0, ik: true, target_moves: 0.0, track: Vec::new() });
                    }
                }
            }
        }
    }
    out
}

/// World (position, rotation) of the contact bones of a posed skeleton.
pub fn contact_bones(sk: &Skeleton) -> HashMap<String, (V3, Quat)> {
    let wanted = |n: &str| EFFECTORS.iter().any(|(e, _)| *e == n) || TARGETS.iter().any(|(t, c, _)| *t == n || *c == n);
    let mut out = HashMap::new();
    for (id, tf) in &sk.tfs {
        if wanted(&tf.name) && !out.contains_key(&tf.name) {
            let m = crate::pose::world_of_id(*id, &sk.tfs);
            out.insert(tf.name.clone(), ([m[0][3], m[1][3], m[2][3]], crate::pose::world_rotation_of(*id, &sk.tfs)));
        }
    }
    out
}

/// Hips world height of a skeleton's rest (the normalisation length of offsets).
pub fn hips_height(sk: &Skeleton) -> f32 {
    sk.tfs.iter().find(|(_, t)| t.name == "cf_j_hips").map(|(id, _)| crate::pose::world_of_id(*id, &sk.tfs)[1][3].abs().max(0.1)).unwrap_or(1.0)
}
