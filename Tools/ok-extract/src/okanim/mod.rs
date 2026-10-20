//! OK animation: skeleton-agnostic animation data and its runtime player.
//!
//! An OK animation is not bound to any skeleton. Each track names a bone and stores, per frame, the rotation relative
//! to the *standard rest* of that bone (the rest it was authored on), plus that rest's local and world rotation. The
//! hips translation is stored normalised by the rest hips offset. Played on any OK skeleton (female, male, a card with
//! its body shape, a mod body, a future family revision), each delta is carried into the target's own rest through the
//! difference of the two rests, and the hips are scaled to the target's own rest offset. Bones the target lacks are
//! skipped; bones it has but the animation does not keep their rest.
//!
//! Groups (1..N roles) add semantic contacts ("role 1 right hand -> role 0 left shoulder, 3 cm out"); the player places
//! the roles and bends their limbs so the contacts meet on the actual bodies, whatever their sizes. Blend spaces (1D/2D)
//! mix animations or groups with phase sync before the contacts are solved.
//!
//! File layout: `OKANIM02` + u32 version + LZ4(bincode(OkAnim)).

pub mod solve;
pub mod convert;
pub mod ffi;
pub mod batch;
pub mod ops;
pub mod prof;
#[cfg(test)]
mod tests;

use crate::finalik::{qdot, qinv, qmul, qnorm, qrot, Quat, V3};
use std::collections::BTreeMap;
use std::sync::Arc;

pub const MAGIC: &[u8; 8] = b"OKANIM02";
pub const VERSION: u32 = 2;
const MAX_BYTES: usize = 256 * 1024 * 1024;

/// One animated bone: name, parent name and the standard rest it was authored on.
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Track {
    pub bone: String,
    pub parent: String,
    pub rest_local: Quat,
    pub rest_world: Quat,
}

/// A float curve (expressions, gaze, mouth...), one value per frame.
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Curve {
    pub name: String,
    pub values: Vec<f32>,
}

/// A semantic contact. Effector and target are bone names; the target is another role's bone, the ground (-1) or a
/// fixed point in the group space (-2). `offset` is in the target bone's frame, normalised by the target's hips rest
/// offset (so it scales with the body).
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Contact {
    pub role: u32,
    pub effector: String,
    pub target_role: i32,
    pub target_bone: String,
    pub offset: V3,
    pub start: f32,
    pub end: f32,
    /// Fade in/out time (s).
    pub blend: f32,
    pub weight: f32,
    pub priority: f32,
    /// Bend the effector's limb onto the target (hands, feet).
    pub ik: bool,
    /// Who adapts: 0 = the effector's role moves, 1 = the target's role moves, between = both.
    pub target_moves: f32,
    /// Offset over the contact's time (uniform samples from start to end), when the effector slides on the target:
    /// on bodies of the source's size the source motion is reproduced exactly. Empty: the constant `offset`.
    #[serde(default)]
    pub track: Vec<V3>,
}

impl Contact {
    /// This contact at time `t` of a cycle of `duration`: its weight and its offset at that time (track resolved).
    pub fn at(&self, t: f32, duration: f32, looping: bool) -> Option<(Contact, f32)> {
        let w = contact_weight(self, t, duration, looping)?;
        let mut c = self.clone();
        if !c.track.is_empty() {
            let tt = if looping && duration > 0.0 { t.rem_euclid(duration) } else { t };
            let span = (c.end - c.start).max(1e-6);
            let f = ((tt - c.start) / span).clamp(0.0, 1.0) * (c.track.len() - 1) as f32;
            let i0 = f.floor() as usize;
            let i1 = (i0 + 1).min(c.track.len() - 1);
            c.offset = lerp3(c.track[i0], c.track[i1], f - i0 as f32);
            c.track = Vec::new();
        }
        Some((c, w))
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct OkAnim {
    pub family: String,
    pub fps: f32,
    pub frames: u32,
    pub looping: bool,
    /// Source scene (limb corrections): 0 auto, 1 Maker, 2 Normal, 3 H/esthetic, 4 Studio.
    pub scene: i32,
    pub hips_bone: String,
    /// Rest hips local offset (the normalisation length) and the hips parent's rest world rotation.
    pub hips_rest: V3,
    pub hips_parent_rest_world: Quat,
    pub tracks: Vec<Track>,
    /// `frames * tracks.len()` rotation deltas relative to each track's rest.
    pub rot: Vec<Quat>,
    /// Per frame hips local offset divided by `|hips_rest|`.
    pub hips: Vec<V3>,
    pub curves: Vec<Curve>,
    pub contacts: Vec<Contact>,
    pub meta: BTreeMap<String, String>,
}

impl OkAnim {
    pub fn duration(&self) -> f32 { if self.frames > 1 { (self.frames - 1) as f32 / self.fps } else { 1.0 / self.fps.max(1.0) } }

    pub fn validate(&self) -> Result<(), String> {
        let n = self.tracks.len();
        if self.frames == 0 || !(self.fps > 0.0) || !self.fps.is_finite() { return Err("OK animation: bad frame count or fps".into()); }
        if self.rot.len() != self.frames as usize * n { return Err("OK animation: rotation count mismatch".into()); }
        if self.hips.len() != self.frames as usize && !self.hips.is_empty() { return Err("OK animation: hips count mismatch".into()); }
        if self.curves.iter().any(|c| c.values.len() != self.frames as usize) { return Err("OK animation: curve length mismatch".into()); }
        if self.rot.iter().flatten().chain(self.hips.iter().flatten()).any(|v| !v.is_finite()) { return Err("OK animation: non-finite values".into()); }
        Ok(())
    }

    pub fn to_bytes(&self) -> Result<Vec<u8>, String> {
        self.validate()?;
        let raw = bincode::serialize(self).map_err(|e| e.to_string())?;
        let mut out = Vec::with_capacity(raw.len() / 2 + 16);
        out.extend_from_slice(MAGIC);
        out.extend_from_slice(&VERSION.to_le_bytes());
        out.extend_from_slice(&lz4_flex::compress_prepend_size(&raw));
        if out.len() > MAX_BYTES { return Err("OK animation too large".into()); }
        Ok(out)
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<OkAnim, String> {
        if bytes.len() < 12 || &bytes[..8] != MAGIC { return Err("not an OK animation (OKANIM02)".into()); }
        if u32::from_le_bytes(bytes[8..12].try_into().unwrap()) != VERSION { return Err("unsupported OK animation version".into()); }
        let raw = lz4_flex::decompress_size_prepended(&bytes[12..]).map_err(|e| e.to_string())?;
        if raw.len() > MAX_BYTES { return Err("OK animation too large".into()); }
        let anim: OkAnim = bincode::deserialize(&raw).map_err(|e| e.to_string())?;
        anim.validate()?;
        Ok(anim)
    }

    /// Root motion handling from the meta "root_motion": "in_place" or "root_motion" take the hips' net horizontal
    /// travel over the clip out of the pose (the latter hands it to the character, see `root_drift`).
    pub fn strips_root(&self) -> bool { matches!(self.meta.get("root_motion").map(String::as_str), Some("in_place") | Some("root_motion")) }
    /// Net horizontal hips travel over one cycle (hips heights, hips-parent frame).
    pub fn cycle_travel(&self) -> V3 {
        match (self.hips.first(), self.hips.last()) { (Some(a), Some(b)) => [b[0] - a[0], 0.0, b[2] - a[2]], _ => [0.0; 3] }
    }
    /// Travel accumulated at unwrapped time `t` (whole cycles plus the linear part of the current one).
    pub fn root_drift(&self, t: f32) -> V3 {
        let d = self.duration().max(1e-6);
        let c = self.cycle_travel();
        let u = if self.looping { t / d } else { (t / d).clamp(0.0, 1.0) };
        [c[0] * u, 0.0, c[2] * u]
    }

    /// Track deltas and normalised hips at time `t` (seconds, looped or clamped).
    pub fn sample(&self, t: f32) -> RolePose {
        let n = self.tracks.len();
        let d = self.duration();
        let t = if self.looping && d > 0.0 { t.rem_euclid(d) } else { t.clamp(0.0, d) };
        let f = (t * self.fps).max(0.0);
        let i0 = (f.floor() as usize).min(self.frames as usize - 1);
        let i1 = (i0 + 1).min(self.frames as usize - 1);
        let a = f - i0 as f32;
        let rot = (0..n).map(|k| nlerp(self.rot[i0 * n + k], self.rot[i1 * n + k], a)).collect();
        let mut hips = if self.hips.is_empty() { [0.0; 3] } else { lerp3(self.hips[i0], self.hips[i1], a) };
        if self.strips_root() {
            // The linear travel of this cycle comes off: the pose stays over its start (in place).
            let c = self.cycle_travel();
            let u = if d > 0.0 { t / d } else { 0.0 };
            hips[0] -= c[0] * u;
            hips[2] -= c[2] * u;
        }
        let curves = self.curves.iter().map(|c| (c.name.clone(), c.values[i0] + (c.values[i1] - c.values[i0]) * a)).collect();
        RolePose { rot, hips, curves }
    }
}

/// One role's sampled pose in its animation's track layout.
#[derive(Clone, Debug, Default)]
pub struct RolePose {
    pub rot: Vec<Quat>,
    pub hips: V3,
    pub curves: Vec<(String, f32)>,
}

pub(crate) fn nlerp(a: Quat, b: Quat, t: f32) -> Quat {
    let b = if qdot(a, b) < 0.0 { [-b[0], -b[1], -b[2], -b[3]] } else { b };
    qnorm([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t])
}
pub(crate) fn lerp3(a: V3, b: V3, t: f32) -> V3 { [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t] }

/// Delta authored against `std_world` carried onto a bone whose rest world rotation is `target_world`.
pub(crate) fn carry_delta(delta: Quat, std_world: Quat, target_world: Quat) -> Quat {
    let c = qmul(qinv(target_world), std_world);
    if (c[3].abs() - 1.0).abs() < 1e-6 { return delta; }
    qnorm(qmul(qmul(c, delta), qinv(c)))
}
pub(crate) fn carry_vector(v: V3, std_world: Quat, target_world: Quat) -> V3 {
    let c = qmul(qinv(target_world), std_world);
    if (c[3].abs() - 1.0).abs() < 1e-6 { return v; }
    qrot(c, v)
}

// ------------------------------------------------------------------------------------------------ player specs

/// What to play: one animation, a group of roles, or a 1D/2D blend of either. Built from JSON plus a list of
/// animation blobs (`"anim": index`).
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
#[serde(tag = "type", rename_all = "lowercase")]
pub enum Spec {
    Anim { anim: usize, #[serde(default = "one")] speed: f32 },
    Group {
        roles: Vec<RoleSpec>,
        #[serde(default)] contacts: Vec<Contact>,
    },
    Blend {
        axes: Vec<Axis>,
        samples: Vec<BlendSample>,
        #[serde(default = "yes")] sync: bool,
    },
    /// Switching animations: `from` keeps playing from `from_time` while `to` starts at 0, fading over `duration`.
    Crossfade {
        from: Box<Spec>,
        to: Box<Spec>,
        #[serde(default)] from_time: f32,
        duration: f32,
    },
    /// A montage: sections laid on one timeline, each cross-fading in over the previous ones.
    Sequence {
        sections: Vec<Section>,
        #[serde(default)] looping: bool,
    },
}
fn one() -> f32 { 1.0 }
fn yes() -> bool { true }

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct RoleSpec {
    #[serde(default)] pub name: String,
    pub spec: Box<Spec>,
    /// Role root offset in the group space, normalised by the role's own hips rest length; and its yaw (degrees).
    #[serde(default)] pub offset: V3,
    #[serde(default)] pub yaw: f32,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Section {
    pub spec: Box<Spec>,
    /// Where the section begins on the montage timeline, how long it lasts there, and where in its own spec it starts.
    pub start: f32,
    pub length: f32,
    #[serde(default)] pub from: f32,
    #[serde(default = "one")] pub rate: f32,
    /// Cross-fade in over whatever plays before it (s).
    #[serde(default)] pub blend_in: f32,
    #[serde(default)] pub name: String,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Axis { pub name: String, pub min: f32, pub max: f32 }

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct BlendSample { pub at: Vec<f32>, pub spec: Box<Spec> }

/// One role of an evaluated frame: its track layout (from the dominant animation), pose and placement.
#[derive(Clone, Debug)]
pub struct RoleEval {
    pub anim: Arc<OkAnim>,
    pub pose: RolePose,
    pub offset: V3,
    pub yaw: f32,
}

#[derive(Clone, Debug, Default)]
pub struct Eval {
    pub roles: Vec<RoleEval>,
    /// Active contacts with their current weight (fade and blend applied).
    pub contacts: Vec<(Contact, f32)>,
}

pub struct Player {
    pub spec: Spec,
    pub anims: Vec<Arc<OkAnim>>,
    pub params: Vec<f32>,
    pub state: solve::State,
    /// Family bone name -> the character's own bone name, for bodies that name some bones differently.
    pub aliases: std::collections::HashMap<String, String>,
    /// A walk cycle mixed into the legs of roles the placement moves fast (see solve::walk).
    pub walk: Option<Arc<OkAnim>>,
}

impl Player {
    pub fn new(spec_json: &str, blobs: &[&[u8]]) -> Result<Player, String> {
        let spec: Spec = serde_json::from_str(spec_json).map_err(|e| format!("OK player spec: {e}"))?;
        let anims = blobs.iter().map(|b| OkAnim::from_bytes(b).map(Arc::new)).collect::<Result<Vec<_>, _>>()?;
        check_spec(&spec, anims.len())?;
        Ok(Player { spec, anims, params: vec![0.0; 2], state: solve::State::default(), aliases: Default::default(), walk: None })
    }
    pub fn duration(&self) -> f32 { duration(&self.spec, &self.anims, &self.params) }
    pub fn role_count(&self) -> usize { role_count(&self.spec) }
    /// Root travel of a role at unwrapped time `t` (its leading animation's, at that animation's own speed).
    pub fn root_drift(&self, role: usize, t: f32) -> V3 {
        let e = self.eval(t);
        let Some(r) = e.roles.get(role) else { return [0.0; 3] };
        if !matches!(r.anim.meta.get("root_motion").map(String::as_str), Some("root_motion")) { return [0.0; 3]; }
        let speed = match &self.spec { Spec::Anim { speed, .. } => *speed, _ => 1.0 };
        r.anim.root_drift(t * speed)
    }
    pub fn eval(&self, t: f32) -> Eval { eval(&self.spec, &self.anims, &self.params, t) }
}

fn check_spec(spec: &Spec, anims: usize) -> Result<(), String> {
    match spec {
        Spec::Anim { anim, .. } => if *anim >= anims { Err(format!("OK player: animation {anim} missing")) } else { Ok(()) },
        Spec::Group { roles, contacts } => {
            if roles.is_empty() { return Err("OK player: a group needs at least one role".into()); }
            for r in roles { check_spec(&r.spec, anims)?; if role_count(&r.spec) != 1 { return Err("OK player: a group role must be a single animation or a 1-role blend".into()); } }
            for c in contacts {
                if c.role as usize >= roles.len() || c.target_role >= roles.len() as i32 { return Err("OK player: contact role out of range".into()); }
            }
            Ok(())
        }
        Spec::Blend { axes, samples, .. } => {
            if axes.is_empty() || axes.len() > 2 { return Err("OK player: a blend space has 1 or 2 axes".into()); }
            if samples.is_empty() { return Err("OK player: a blend space needs samples".into()); }
            let n = role_count(&samples[0].spec);
            for s in samples {
                check_spec(&s.spec, anims)?;
                if s.at.len() != axes.len() { return Err("OK player: sample position does not match the axes".into()); }
                if role_count(&s.spec) != n { return Err("OK player: blend samples must have the same number of roles".into()); }
            }
            Ok(())
        }
        Spec::Crossfade { from, to, duration, .. } => {
            check_spec(from, anims)?;
            check_spec(to, anims)?;
            if role_count(from) != role_count(to) { return Err("OK player: a cross-fade needs the same number of roles".into()); }
            if !(*duration >= 0.0) { return Err("OK player: bad cross-fade time".into()); }
            Ok(())
        }
        Spec::Sequence { sections, .. } => {
            if sections.is_empty() { return Err("OK player: a montage needs sections".into()); }
            let n = role_count(&sections[0].spec);
            for s in sections {
                check_spec(&s.spec, anims)?;
                if !(s.length > 0.0) || !s.start.is_finite() || !s.from.is_finite() || !(s.rate > 0.0) { return Err(format!("OK player: montage section '{}' has a bad range", s.name)); }
                if role_count(&s.spec) != n { return Err("OK player: montage sections must have the same number of roles".into()); }
            }
            Ok(())
        }
    }
}

fn role_count(spec: &Spec) -> usize {
    match spec { Spec::Anim { .. } => 1, Spec::Group { roles, .. } => roles.len(), Spec::Blend { samples, .. } => role_count(&samples[0].spec),
        Spec::Sequence { sections, .. } => role_count(&sections[0].spec),
        Spec::Crossfade { to, .. } => role_count(to) }
}

fn duration(spec: &Spec, anims: &[Arc<OkAnim>], params: &[f32]) -> f32 {
    match spec {
        Spec::Anim { anim, speed } => anims[*anim].duration() / speed.max(1e-3),
        Spec::Group { roles, contacts } => roles.iter().map(|r| duration(&r.spec, anims, params)).fold(0.0, f32::max)
            .max(contacts.iter().map(|c| c.end).fold(0.0, f32::max)),
        Spec::Blend { axes, samples, .. } => blend_weights(axes, samples, params).iter().map(|(i, w)| w * duration(&samples[*i].spec, anims, params)).sum(),
        Spec::Sequence { sections, .. } => sections.iter().map(|s| s.start + s.length).fold(0.0, f32::max),
        Spec::Crossfade { to, .. } => duration(to, anims, params),
    }
}

/// Barycentric weights of the blend samples at `params` (1D: neighbouring pair; 2D: Delaunay triangle, or the nearest
/// edge/point outside the hull).
pub fn blend_weights(axes: &[Axis], samples: &[BlendSample], params: &[f32]) -> Vec<(usize, f32)> {
    let norm = |v: f32, a: &Axis| if a.max > a.min { ((v - a.min) / (a.max - a.min)).clamp(0.0, 1.0) } else { 0.0 };
    if samples.len() == 1 { return vec![(0, 1.0)]; }
    if axes.len() == 1 {
        let x = norm(params.first().copied().unwrap_or(0.0), &axes[0]);
        let mut order: Vec<usize> = (0..samples.len()).collect();
        order.sort_by(|&a, &b| norm(samples[a].at[0], &axes[0]).total_cmp(&norm(samples[b].at[0], &axes[0])));
        let xs: Vec<f32> = order.iter().map(|&i| norm(samples[i].at[0], &axes[0])).collect();
        if x <= xs[0] { return vec![(order[0], 1.0)]; }
        if x >= *xs.last().unwrap() { return vec![(*order.last().unwrap(), 1.0)]; }
        for k in 0..xs.len() - 1 {
            if x >= xs[k] && x <= xs[k + 1] {
                let span = (xs[k + 1] - xs[k]).max(1e-6);
                let a = (x - xs[k]) / span;
                return vec![(order[k], 1.0 - a), (order[k + 1], a)];
            }
        }
        return vec![(order[0], 1.0)];
    }
    let p = [norm(params.first().copied().unwrap_or(0.0), &axes[0]), norm(params.get(1).copied().unwrap_or(0.0), &axes[1])];
    let pts: Vec<[f32; 2]> = samples.iter().map(|s| [norm(s.at[0], &axes[0]), norm(s.at[1], &axes[1])]).collect();
    let tris = delaunay(&pts);
    for t in &tris {
        if let Some(w) = barycentric(p, pts[t[0]], pts[t[1]], pts[t[2]]) {
            return t.iter().zip(w).filter(|(_, w)| *w > 1e-6).map(|(i, w)| (*i, w)).collect();
        }
    }
    // Outside the hull (or collinear samples): the closest point on any triangle edge / sample pair.
    let mut best: Option<(f32, Vec<(usize, f32)>)> = None;
    let mut consider = |a: usize, b: usize| {
        let (pa, pb) = (pts[a], pts[b]);
        let ab = [pb[0] - pa[0], pb[1] - pa[1]];
        let l2 = ab[0] * ab[0] + ab[1] * ab[1];
        let s = if l2 > 1e-12 { (((p[0] - pa[0]) * ab[0] + (p[1] - pa[1]) * ab[1]) / l2).clamp(0.0, 1.0) } else { 0.0 };
        let q = [pa[0] + ab[0] * s, pa[1] + ab[1] * s];
        let d = (q[0] - p[0]).powi(2) + (q[1] - p[1]).powi(2);
        if best.as_ref().map_or(true, |(bd, _)| d < *bd) { best = Some((d, vec![(a, 1.0 - s), (b, s)])); }
    };
    for i in 0..pts.len() { for j in i + 1..pts.len() { consider(i, j); } }
    best.map(|(_, w)| w.into_iter().filter(|(_, w)| *w > 1e-6).collect()).unwrap_or_else(|| vec![(0, 1.0)])
}

fn barycentric(p: [f32; 2], a: [f32; 2], b: [f32; 2], c: [f32; 2]) -> Option<[f32; 3]> {
    let d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    if d.abs() < 1e-9 { return None; }
    let l1 = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / d;
    let l2 = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / d;
    let l3 = 1.0 - l1 - l2;
    (l1 >= -1e-5 && l2 >= -1e-5 && l3 >= -1e-5).then_some([l1.max(0.0), l2.max(0.0), l3.max(0.0)])
}

/// Bowyer-Watson triangulation of the (normalised) sample positions.
fn delaunay(pts: &[[f32; 2]]) -> Vec<[usize; 3]> {
    let n = pts.len();
    if n < 3 { return Vec::new(); }
    let mut all: Vec<[f32; 2]> = pts.to_vec();
    all.extend([[-10.0, -10.0], [10.0, -10.0], [0.0, 10.0]]);
    let mut tris: Vec<[usize; 3]> = vec![[n, n + 1, n + 2]];
    let circum = |t: &[usize; 3], all: &[[f32; 2]]| -> Option<([f32; 2], f32)> {
        let (a, b, c) = (all[t[0]], all[t[1]], all[t[2]]);
        let d = 2.0 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]));
        if d.abs() < 1e-12 { return None; }
        let (a2, b2, c2) = (a[0] * a[0] + a[1] * a[1], b[0] * b[0] + b[1] * b[1], c[0] * c[0] + c[1] * c[1]);
        let ux = (a2 * (b[1] - c[1]) + b2 * (c[1] - a[1]) + c2 * (a[1] - b[1])) / d;
        let uy = (a2 * (c[0] - b[0]) + b2 * (a[0] - c[0]) + c2 * (b[0] - a[0])) / d;
        Some(([ux, uy], (a[0] - ux).powi(2) + (a[1] - uy).powi(2)))
    };
    for i in 0..n {
        let p = all[i];
        let mut bad = Vec::new();
        for (k, t) in tris.iter().enumerate() {
            if let Some((c, r2)) = circum(t, &all) { if (p[0] - c[0]).powi(2) + (p[1] - c[1]).powi(2) <= r2 + 1e-9 { bad.push(k); } }
        }
        let mut edges: Vec<[usize; 2]> = Vec::new();
        for &k in &bad {
            let t = tris[k];
            for e in [[t[0], t[1]], [t[1], t[2]], [t[2], t[0]]] {
                if let Some(pos) = edges.iter().position(|x| (x[0] == e[1] && x[1] == e[0]) || (x[0] == e[0] && x[1] == e[1])) { edges.remove(pos); } else { edges.push(e); }
            }
        }
        for k in bad.into_iter().rev() { tris.remove(k); }
        for e in edges { tris.push([e[0], e[1], i]); }
    }
    tris.retain(|t| t.iter().all(|&v| v < n));
    tris
}

fn eval(spec: &Spec, anims: &[Arc<OkAnim>], params: &[f32], t: f32) -> Eval {
    match spec {
        Spec::Anim { anim, speed } => {
            let a = anims[*anim].clone();
            let pose = a.sample(t * speed);
            let contacts = a.contacts.iter().filter_map(|c| c.at(t * speed, a.duration(), a.looping)).collect();
            Eval { roles: vec![RoleEval { anim: a, pose, offset: [0.0; 3], yaw: 0.0 }], contacts }
        }
        Spec::Group { roles, contacts } => {
            let mut out = Eval::default();
            let total = duration(spec, anims, params);
            for (ri, r) in roles.iter().enumerate() {
                let mut e = eval(&r.spec, anims, params, t);
                let mut role = e.roles.remove(0);
                role.offset = r.offset;
                role.yaw = r.yaw;
                out.roles.push(role);
                // A role's own animation contacts (ground, objects) belong to that role.
                for (mut c, w) in e.contacts { c.role = ri as u32; if c.target_role >= 0 { c.target_role = ri as i32; } out.contacts.push((c, w)); }
            }
            let looping = out.roles.iter().all(|r| r.anim.looping);
            for c in contacts { if let Some(cw) = c.at(t, total, looping) { out.contacts.push(cw); } }
            out
        }
        Spec::Blend { axes, samples, sync } => {
            let weights = blend_weights(axes, samples, params);
            let total: f32 = weights.iter().map(|(i, w)| w * duration(&samples[*i].spec, anims, params)).sum();
            let phase = if total > 0.0 { t / total } else { 0.0 };
            let evals: Vec<(Eval, f32)> = weights.iter().map(|(i, w)| {
                let d = duration(&samples[*i].spec, anims, params);
                // Phase sync: every sample at the same fraction of its own cycle.
                let ts = if *sync { phase * d } else { t };
                (eval(&samples[*i].spec, anims, params, ts), *w)
            }).collect();
            blend_evals(evals)
        }
        Spec::Crossfade { from, to, from_time, duration } => {
            let w = if *duration > 0.0 { (t / duration).clamp(0.0, 1.0) } else { 1.0 };
            if w >= 1.0 { return eval(to, anims, params, t); }
            blend_evals(vec![(eval(from, anims, params, from_time + t), 1.0 - w), (eval(to, anims, params, t), w)])
        }
        Spec::Sequence { sections, looping } => {
            let total = duration(spec, anims, params);
            let tt = if *looping && total > 0.0 { t.rem_euclid(total) } else { t.clamp(0.0, total) };
            let mut order: Vec<usize> = (0..sections.len()).collect();
            order.sort_by(|&a, &b| sections[a].start.total_cmp(&sections[b].start));
            // Later sections fade in over the earlier ones: each active section takes its fade weight, the rest scale down.
            let mut weights: Vec<(usize, f32)> = Vec::new();
            for &i in &order {
                let s = &sections[i];
                if tt < s.start || tt > s.start + s.length { continue; }
                let w = if s.blend_in > 0.0 && !weights.is_empty() { ((tt - s.start) / s.blend_in).clamp(0.0, 1.0) } else { 1.0 };
                for x in weights.iter_mut() { x.1 *= 1.0 - w; }
                weights.push((i, w));
            }
            weights.retain(|(_, w)| *w > 1e-6);
            if weights.is_empty() {
                // Between sections: hold the last one that ended (or the first one before it starts).
                let i = order.iter().rev().copied().find(|&i| sections[i].start + sections[i].length <= tt).unwrap_or(order[0]);
                weights.push((i, 1.0));
            }
            let evals = weights.iter().map(|&(i, w)| {
                let s = &sections[i];
                let local = s.from + (tt.clamp(s.start, s.start + s.length) - s.start) * s.rate;
                (eval(&s.spec, anims, params, local), w)
            }).collect();
            blend_evals(evals)
        }
    }
}

fn contact_weight(c: &Contact, t: f32, duration: f32, looping: bool) -> Option<f32> {
    let t = if looping && duration > 0.0 { t.rem_euclid(duration) } else { t };
    if t < c.start || t > c.end { return None; }
    let fade = c.blend.max(0.0);
    let w = if fade > 0.0 { ((t - c.start) / fade).min((c.end - t) / fade).clamp(0.0, 1.0) } else { 1.0 };
    let w = w * c.weight.clamp(0.0, 1.0);
    (w > 0.0).then_some(w)
}

/// Weighted blend of evaluations with the same role count. Bones are matched by name against the heaviest sample's
/// track layout; contacts with the same endpoints are merged, the others fade with their sample's weight.
fn blend_evals(evals: Vec<(Eval, f32)>) -> Eval {
    let Some(lead) = evals.iter().max_by(|a, b| a.1.total_cmp(&b.1)).map(|(e, _)| e.clone()) else { return Eval::default() };
    let mut out = lead.clone();
    for (ri, role) in out.roles.iter_mut().enumerate() {
        let layout: Vec<&str> = role.anim.tracks.iter().map(|t| t.bone.as_str()).collect();
        let mut acc: Vec<[f32; 4]> = vec![[0.0; 4]; layout.len()];
        let (mut hips, mut offset, mut yaw) = ([0.0f32; 3], [0.0f32; 3], 0.0f32);
        let mut curves: BTreeMap<String, f32> = BTreeMap::new();
        for (e, w) in &evals {
            let Some(r) = e.roles.get(ri) else { continue };
            let index: std::collections::HashMap<&str, usize> = r.anim.tracks.iter().enumerate().map(|(i, t)| (t.bone.as_str(), i)).collect();
            for (k, bone) in layout.iter().enumerate() {
                let q = index.get(bone).map(|&i| r.pose.rot[i]).unwrap_or([0.0, 0.0, 0.0, 1.0]);
                let q = if qdot(acc[k], q) < 0.0 { [-q[0], -q[1], -q[2], -q[3]] } else { q };
                for c in 0..4 { acc[k][c] += q[c] * w; }
            }
            for c in 0..3 { hips[c] += r.pose.hips[c] * w; offset[c] += r.offset[c] * w; }
            yaw += r.yaw * w;
            for (n, v) in &r.pose.curves { *curves.entry(n.clone()).or_default() += v * w; }
        }
        role.pose.rot = acc.into_iter().map(qnorm).collect();
        role.pose.hips = hips;
        role.offset = offset;
        role.yaw = yaw;
        role.pose.curves = curves.into_iter().collect();
    }
    let key = |c: &Contact| format!("{}|{}|{}|{}", c.role, c.effector, c.target_role, c.target_bone);
    let mut merged: BTreeMap<String, (Contact, f32, [f32; 3], f32)> = BTreeMap::new();
    for (e, w) in &evals {
        for (c, cw) in &e.contacts {
            let entry = merged.entry(key(c)).or_insert_with(|| (c.clone(), 0.0, [0.0; 3], 0.0));
            entry.1 += cw * w;
            for i in 0..3 { entry.2[i] += c.offset[i] * w; }
            entry.3 += w;
        }
    }
    out.contacts = merged.into_values().map(|(mut c, w, off, ws)| { if ws > 0.0 { c.offset = off.map(|v| v / ws); } (c, w) }).collect();
    out
}

#[cfg(test)]
pub(crate) mod tests_support {
    use super::*;
    /// hips -> arm, 5 frames at 10 fps, identity rests.
    pub fn two_bone_anim() -> OkAnim {
        OkAnim {
            family: "test".into(), fps: 10.0, frames: 5, looping: false, scene: 0, hips_bone: "hips".into(), hips_rest: [0.0, 1.0, 0.0],
            hips_parent_rest_world: [0.0, 0.0, 0.0, 1.0],
            tracks: vec![
                Track { bone: "hips".into(), parent: String::new(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
                Track { bone: "arm".into(), parent: "hips".into(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
            ],
            rot: vec![[0.0, 0.0, 0.0, 1.0]; 10], hips: vec![[0.0, 1.0, 0.0]; 5], curves: vec![], contacts: vec![], meta: Default::default(),
        }
    }
}
