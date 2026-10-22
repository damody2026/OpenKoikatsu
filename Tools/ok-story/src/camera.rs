//! The view camera: each line's pose (fixed, a shot framed from the cast, or a scene camera), the smooth move between
//! lines, the parallax that follows the pointer, and the free camera that records a line's pose.
use crate::io::{ActorFacts, SceneCamera};
use crate::math::{self, Pose, Quat, V3};
use crate::script::CameraSpec;
use crate::tuning::CameraTuning;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Travel {
    pub from: Pose,
    pub to: Pose,
    pub seconds: f64,
    pub elapsed: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct Fly {
    /// The pose before editing (Escape returns to it).
    pub original: Pose,
    pub keys: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct Rig {
    pub current: Pose,
    pub travel: Option<Travel>,
    /// Pointer -1..1 and its smoothed follower.
    pub pointer: [f64; 2],
    pub parallax: [f64; 2],
    pub fly: Option<Fly>,
    /// A shot is reframed while the cast settles at the start of its line.
    pub reframe_for: f64,
    /// A shot whose people are not there yet: framed (with this cut) as soon as they are.
    pub waiting: Option<bool>,
}

/// Seconds a move takes (0: no move).
pub fn travel_seconds(t: &CameraTuning, a: &Pose, b: &Pose, speed: f64) -> f64 {
    let distance = math::len(math::sub(b.pos, a.pos));
    let angle = a.rot.angle_to(b.rot);
    let fov = (a.fov - b.fov).abs();
    if distance < t.still_cm && angle < t.still_angle && fov < t.still_fov {
        return 0.0;
    }
    let raw = distance / t.per_cm + angle / std::f64::consts::PI * t.per_turn + fov / t.per_fov;
    raw.clamp(t.min_seconds, t.max_seconds) / speed.clamp(t.speed_min, t.speed_max)
}

/// What a shot can see: the speaker, the cast on stage, the scene's cameras.
pub struct ShotContext<'a> {
    pub speaker: &'a str,
    pub present: Vec<&'a str>,
    pub actors: &'a BTreeMap<String, ActorFacts>,
    pub cameras: &'a [SceneCamera],
    pub active_camera: Option<i32>,
    /// View width over height.
    pub aspect: f64,
}

fn looking(pos: V3, at: V3, fov: f64) -> Pose {
    Pose { pos, rot: Quat::look(math::sub(at, pos), [0.0, 0.0, 1.0]), fov }
}

/// One place a shot could be taken from; `from` is the point it looks at (a probe from there to the camera tells
/// whether a wall stands between).
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Candidate {
    pub pose: Pose,
    pub from: V3,
}

/// The pose of a camera spec; None keeps the current view (a shot of nobody, a missing scene camera).
pub fn resolve(t: &CameraTuning, spec: &CameraSpec, ctx: &ShotContext, current: &Pose) -> Option<Pose> {
    candidates(t, spec, ctx, current).first().map(|c| c.pose)
}

/// The places a camera spec can be seen from, best first: a fixed pose is one place, a shot several around its
/// subject (turned further and closer in) so one without a wall in between can be picked.
pub fn candidates(t: &CameraTuning, spec: &CameraSpec, ctx: &ShotContext, current: &Pose) -> Vec<Candidate> {
    let around = |aim: V3, dir: V3, distance: f64, fov: f64, angles: &[f64], closer: &[f64]| -> Vec<Candidate> {
        let mut out = Vec::new();
        for (k, f) in closer.iter().enumerate() {
            for (i, a) in angles.iter().enumerate() {
                // Closer in only for the first few angles: the near ones are the fallbacks.
                if k > 0 && i >= 3 {
                    break;
                }
                let d = Quat::yaw_deg(*a).rotate(dir);
                out.push(Candidate { pose: looking(math::add(aim, math::scale(d, distance * f)), aim, fov), from: aim });
            }
        }
        out
    };
    match shot_pose(t, spec, ctx, current) {
        Some((aim, dir, distance, fov)) if spec.shot.as_deref().is_some_and(|s| !s.starts_with("studio")) => {
            around(aim, dir, distance, fov, &t.probe_angles, &t.probe_closer)
        }
        Some((aim, dir, distance, fov)) => vec![Candidate { pose: looking(math::add(aim, math::scale(dir, distance)), aim, fov), from: aim }],
        None => fixed(spec, ctx, current).map(|p| vec![Candidate { pose: p, from: p.pos }]).unwrap_or_default(),
    }
}

/// A fixed pose or a scene camera.
fn fixed(spec: &CameraSpec, ctx: &ShotContext, current: &Pose) -> Option<Pose> {
    let fov_or = |d: f64| spec.fov.unwrap_or(d);
    match spec.shot.as_deref() {
        None => {
            let rot = spec.fixed_rotation()?;
            Some(Pose { pos: spec.pos?, rot, fov: fov_or(current.fov) })
        }
        Some(studio) => {
            let id = match studio.strip_prefix("studio:") {
                Some(n) => n.parse::<i32>().ok()?,
                None if studio == "studio" => ctx.active_camera.or_else(|| ctx.cameras.first().map(|c| c.id))?,
                None => return None,
            };
            let c = ctx.cameras.iter().find(|c| c.id == id)?;
            Some(Pose { pos: c.pos, rot: Quat::from_array(c.rot), fov: fov_or(c.fov) })
        }
    }
}

/// Aim height and distance that show `top`..`bottom` in the part of the view above the dialog box (`wide_free` of its
/// height), no closer than `least`.
fn fit_vertical(t: &CameraTuning, aspect: f64, fov: f64, top: f64, bottom: f64, least: f64) -> (f64, f64) {
    let tan_v = (fov.to_radians() * 0.5).tan() / aspect.max(0.1);
    let half = (top - bottom).max(1.0) * 0.5 / t.wide_free;
    let distance = (half / tan_v).max(least);
    ((top + bottom) * 0.5 - distance * tan_v * (1.0 - t.wide_free), distance)
}

/// A shot as aim point, direction from it to the camera, distance and field of view.
fn shot_pose(t: &CameraTuning, spec: &CameraSpec, ctx: &ShotContext, _current: &Pose) -> Option<(V3, V3, f64, f64)> {
    let fov_or = |d: f64| spec.fov.unwrap_or(d);
    let shot = spec.shot.as_deref()?;
    // Only people on stage are framed (an offstage speaker's portrait copy stands far away).
    let actor = |who: &str| ctx.actors.get(who).filter(|a| (a.ready || !a.bones.is_empty()) && ctx.present.contains(&who));
    let subject = spec.on.as_deref().unwrap_or(ctx.speaker);
    // A two shot without its subject on stage frames the first two who are.
    let subject = if actor(subject).is_none() && shot == "two" { ctx.present.iter().copied().find(|w| actor(w).is_some()).unwrap_or(subject) } else { subject };
    match shot {
        "speaker" => {
            let a = actor(subject)?;
            let k = a.root.scale.abs().max(0.1);
            let fov = fov_or(t.speaker_fov);
            let dir = Quat::yaw_deg(t.speaker_side_deg).rotate(a.facing());
            let head = a.head();
            // Hair top to chest inside the part of the view above the dialog box, at least the tuned distance.
            let (aim, distance) = fit_vertical(t, ctx.aspect, fov, head[2] + t.frame_above * k, head[2] - t.frame_below * k, t.speaker_distance * k);
            Some(([head[0], head[1], aim], dir, distance, fov))
        }
        "two" => {
            let a = actor(subject)?;
            let other = ctx
                .present
                .iter()
                .filter(|w| **w != subject)
                .filter_map(|w| actor(w))
                .min_by(|x, y| {
                    let dx = math::len(math::sub(x.head(), a.head()));
                    let dy = math::len(math::sub(y.head(), a.head()));
                    dx.total_cmp(&dy)
                })?;
            let (ha, hb) = (a.head(), other.head());
            let mid = math::scale(math::add(ha, hb), 0.5);
            let across = math::sub(hb, ha);
            let mut side = math::normalize(math::cross([0.0, 0.0, 1.0], [across[0], across[1], 0.0]));
            let facing = math::add(a.facing(), other.facing());
            if math::dot(side, facing) < 0.0 {
                side = math::scale(side, -1.0);
            }
            let fov = fov_or(t.two_fov);
            let half = (fov.to_radians() * 0.5).tan();
            let width = math::len([across[0], across[1], 0.0]) / t.two_fill.max(0.05);
            let top = ha[2].max(hb[2]) + t.frame_above;
            let bottom = ha[2].min(hb[2]) - t.frame_below;
            let (aim_z, distance) = fit_vertical(t, ctx.aspect, fov, top, bottom, (width * 0.5 / half).max(120.0));
            Some(([mid[0], mid[1], aim_z], side, distance, fov))
        }
        "wide" => {
            let all: Vec<&ActorFacts> = ctx.present.iter().filter_map(|w| actor(w)).collect();
            if all.is_empty() {
                return None;
            }
            let (mut lo, mut hi) = ([f64::MAX; 3], [f64::MIN; 3]);
            let mut facing = [0.0; 3];
            for a in &all {
                for p in [a.root.pos, a.head()] {
                    for i in 0..3 {
                        lo[i] = lo[i].min(p[i]);
                        hi[i] = hi[i].max(p[i]);
                    }
                }
                facing = math::add(facing, a.facing());
            }
            let center = math::scale(math::add(lo, hi), 0.5);
            let fov = fov_or(t.wide_fov);
            let tan_h = (fov.to_radians() * 0.5).tan();
            let tan_v = tan_h / ctx.aspect.max(0.1);
            // Everyone fits across, and head to foot (hair included) in the part of the view above the dialog box.
            let half_w = (math::len([hi[0] - lo[0], hi[1] - lo[1], 0.0]) * 0.5).max(60.0) * t.wide_margin;
            let half_h = ((hi[2] - lo[2]) * 0.5 + 15.0) * t.wide_margin / t.wide_free;
            let distance = (half_w / tan_h).max(half_h / tan_v).max(200.0);
            let dir = math::normalize(if math::len(facing) < 1e-6 { [0.0, -1.0, 0.0] } else { facing });
            // Aimed below the middle, so the group stands in the free upper part of the view.
            let aim = math::add(center, [0.0, 0.0, 7.5 - distance * tan_v * (1.0 - t.wide_free)]);
            Some((aim, dir, distance, fov))
        }
        _ => None,
    }
}

impl Rig {
    /// Move to `to`: smoothly unless `cut`.
    pub fn go(&mut self, t: &CameraTuning, to: Pose, cut: bool, speed: f64) {
        self.fly = None;
        let seconds = if cut { 0.0 } else { travel_seconds(t, &self.current, &to, speed) };
        if seconds <= 0.0 {
            self.current = to;
            self.travel = None;
        } else {
            self.travel = Some(Travel { from: self.current, to, seconds, elapsed: 0.0 });
        }
    }

    /// A new end for the move under way (or the pose, when none is), its timing kept.
    pub fn retarget(&mut self, to: Pose) {
        match &mut self.travel {
            Some(tr) => tr.to = to,
            None => self.current = to,
        }
    }

    pub fn finish(&mut self) {
        if let Some(tr) = self.travel.take() {
            self.current = tr.to;
        }
    }

    pub fn moving(&self) -> bool {
        self.travel.is_some()
    }

    pub fn tick(&mut self, t: &CameraTuning, dt: f64, paused: bool) {
        if !paused {
            if let Some(tr) = &mut self.travel {
                tr.elapsed += dt;
                let u = math::smoothstep(tr.elapsed / tr.seconds);
                self.current = Pose {
                    pos: math::lerp3(tr.from.pos, tr.to.pos, u),
                    rot: tr.from.rot.slerp(tr.to.rot, u),
                    fov: math::lerp(tr.from.fov, tr.to.fov, u),
                };
                if tr.elapsed >= tr.seconds {
                    self.finish();
                }
            }
        }
        let k = 1.0 - (-dt.clamp(0.0, 0.1) * t.parallax_rate).exp();
        for i in 0..2 {
            self.parallax[i] += (self.pointer[i] - self.parallax[i]) * k;
        }
    }

    /// The pose to render: the current one with the parallax offset (never stored).
    pub fn shown(&self, t: &CameraTuning, strength: f64) -> Pose {
        let p = self.current;
        if strength <= 0.0 || self.fly.is_some() {
            return p;
        }
        let right = p.rot.rotate([0.0, 1.0, 0.0]);
        let up = p.rot.rotate([0.0, 0.0, 1.0]);
        let offset = math::add(
            math::scale(right, self.parallax[0] * t.parallax_cm[0] * strength),
            math::scale(up, self.parallax[1] * t.parallax_cm[1] * strength),
        );
        let turn = Quat::from_rotator(self.parallax[1] * t.parallax_deg[1] * strength, -self.parallax[0] * t.parallax_deg[0] * strength, 0.0);
        Pose { pos: math::add(p.pos, offset), rot: p.rot.times(turn).normalized(), fov: p.fov }
    }

    pub fn start_fly(&mut self) {
        self.finish();
        self.fly = Some(Fly { original: self.current, keys: Vec::new() });
    }

    pub fn fly_key(&mut self, key: &str, down: bool) {
        if let Some(f) = &mut self.fly {
            f.keys.retain(|k| k != key);
            if down {
                f.keys.push(key.to_string());
            }
        }
    }

    pub fn fly_turn(&mut self, t: &CameraTuning, dx: f64, dy: f64) {
        if self.fly.is_none() {
            return;
        }
        let f = self.current.rot.forward();
        let yaw = f[1].atan2(f[0]).to_degrees() + dx * t.fly_mouse;
        let pitch = (f[2].clamp(-1.0, 1.0).asin().to_degrees() - dy * t.fly_mouse).clamp(-88.0, 88.0);
        self.current.rot = Quat::from_rotator(pitch, yaw, 0.0);
    }

    pub fn fly_tick(&mut self, t: &CameraTuning, dt: f64) {
        let Some(f) = &self.fly else { return };
        let has = |k: &str| f.keys.iter().any(|x| x == k);
        let mut speed = t.fly_speed;
        if has("LeftShift") || has("RightShift") {
            speed *= t.fly_fast;
        }
        if has("LeftAlt") || has("RightAlt") {
            speed *= t.fly_slow;
        }
        let axis = |a: &str, b: &str| (has(a) as i32 - has(b) as i32) as f64;
        let local = [axis("W", "S"), axis("D", "A"), axis("E", "Q")];
        if local.iter().all(|v| *v == 0.0) {
            return;
        }
        let r = self.current.rot;
        let mv = math::add(math::add(math::scale(r.forward(), local[0]), math::scale(r.rotate([0.0, 1.0, 0.0]), local[1])), [0.0, 0.0, local[2]]);
        self.current.pos = math::add(self.current.pos, math::scale(math::normalize(mv), speed * dt.clamp(0.0, 0.1)));
    }

    /// Escape: back to where editing started.
    pub fn cancel_fly(&mut self) {
        if let Some(f) = self.fly.take() {
            self.current = f.original;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::io::Xf;

    fn actor(pos: V3, yaw: f64) -> ActorFacts {
        let mut a = ActorFacts { ready: true, ..Default::default() };
        a.root = Xf { pos, rot: Quat::yaw_deg(yaw).to_array(), scale: 1.0 };
        a.bones.insert("head".into(), Xf { pos: math::add(pos, [0.0, 0.0, 150.0]), ..Default::default() });
        a
    }

    #[test]
    fn travel_formula() {
        let t = CameraTuning::default();
        let a = Pose { pos: [0.0, 0.0, 0.0], rot: Quat::IDENTITY, fov: 40.0 };
        assert_eq!(travel_seconds(&t, &a, &a, 1.0), 0.0);
        // 400 cm: 1 s; plus a half turn: 0.35 s more; fov 20: 0.2 s more.
        let b = Pose { pos: [400.0, 0.0, 0.0], rot: Quat::yaw_deg(90.0), fov: 60.0 };
        let s = travel_seconds(&t, &a, &b, 1.0);
        assert!((s - (1.0 + 0.35 + 0.2)).abs() < 1e-9, "{s}");
        // Short moves take at least the minimum, long ones at most the maximum.
        let c = Pose { pos: [1.0, 0.0, 0.0], ..a };
        assert_eq!(travel_seconds(&t, &a, &c, 1.0), 0.35);
        let d = Pose { pos: [100000.0, 0.0, 0.0], ..a };
        assert_eq!(travel_seconds(&t, &a, &d, 1.0), 4.0);
        // Speed is clamped.
        assert!((travel_seconds(&t, &a, &b, 10.0) - s / 1.15).abs() < 1e-9);
    }

    #[test]
    fn travel_passes_between_and_finishes() {
        let t = CameraTuning::default();
        let mut rig = Rig::default();
        let a = Pose { pos: [0.0, 0.0, 0.0], rot: Quat::IDENTITY, fov: 40.0 };
        let b = Pose { pos: [800.0, 0.0, 0.0], rot: Quat::IDENTITY, fov: 40.0 };
        rig.current = a;
        rig.go(&t, b, false, 1.0);
        let total = rig.travel.as_ref().unwrap().seconds;
        rig.tick(&t, total / 2.0, false);
        assert!((rig.current.pos[0] - 400.0).abs() < 1e-6, "smoothstep middle {}", rig.current.pos[0]);
        rig.tick(&t, total, true);
        assert!(rig.moving(), "paused travel stays");
        rig.finish();
        assert_eq!(rig.current, b);
        rig.go(&t, a, true, 1.0);
        assert_eq!(rig.current, a);
    }

    #[test]
    fn speaker_shot_faces_the_speaker() {
        let t = CameraTuning::default();
        let mut actors = BTreeMap::new();
        actors.insert("ayu".to_string(), actor([0.0, 0.0, 0.0], 0.0));
        let ctx = ShotContext { speaker: "ayu", present: vec!["ayu"], actors: &actors, cameras: &[], active_camera: None, aspect: 16.0 / 9.0 };
        let spec = CameraSpec { shot: Some("speaker".into()), ..Default::default() };
        let p = resolve(&t, &spec, &ctx, &Pose::default()).unwrap();
        // Yaw 0: she faces +Y; the camera stands in front of her (+Y side) and looks back at her head.
        assert!(p.pos[1] > 100.0, "{:?}", p.pos);
        let f = p.rot.forward();
        assert!(f[1] < -0.9, "{f:?}");
        assert_eq!(p.fov, 30.0);
        // Nobody to frame: the view stays.
        let ctx = ShotContext { speaker: "", ..ctx };
        assert!(resolve(&t, &spec, &ctx, &Pose::default()).is_none());
    }

    #[test]
    fn two_shot_sees_both() {
        let t = CameraTuning::default();
        let mut actors = BTreeMap::new();
        actors.insert("a".to_string(), actor([-60.0, 0.0, 0.0], 0.0));
        actors.insert("b".to_string(), actor([60.0, 0.0, 0.0], 0.0));
        let ctx = ShotContext { speaker: "a", present: vec!["a", "b"], actors: &actors, cameras: &[], active_camera: None, aspect: 16.0 / 9.0 };
        let p = resolve(&t, &CameraSpec { shot: Some("two".into()), ..Default::default() }, &ctx, &Pose::default()).unwrap();
        assert!(p.pos[0].abs() < 1e-6 && p.pos[1] > 0.0);
        // Both heads inside the horizontal field of view.
        for h in [[-60.0, 0.0, 150.0], [60.0, 0.0, 150.0]] {
            let d = math::normalize(math::sub(h, p.pos));
            let ang = math::dot(d, p.rot.forward()).acos().to_degrees();
            assert!(ang < p.fov * 0.5, "{ang}");
        }
    }

    #[test]
    fn parallax_is_not_stored() {
        let t = CameraTuning::default();
        let mut rig = Rig::default();
        rig.pointer = [1.0, 1.0];
        for _ in 0..200 {
            rig.tick(&t, 0.016, false);
        }
        let shown = rig.shown(&t, 0.5);
        assert!(math::len(math::sub(shown.pos, rig.current.pos)) > 1.0);
        assert_eq!(rig.current, Pose::default());
    }
}

#[cfg(test)]
mod offstage_tests {
    use super::*;
    use crate::io::Xf;

    fn actor(pos: V3) -> ActorFacts {
        let mut a = ActorFacts { ready: true, ..Default::default() };
        a.root = Xf { pos, rot: Quat::IDENTITY.to_array(), scale: 1.0 };
        a.bones.insert("head".into(), Xf { pos: math::add(pos, [0.0, 0.0, 150.0]), ..Default::default() });
        a
    }

    #[test]
    fn offstage_speakers_are_not_framed() {
        let t = CameraTuning::default();
        let mut actors = BTreeMap::new();
        actors.insert("a".to_string(), actor([-60.0, 0.0, 0.0]));
        actors.insert("b".to_string(), actor([60.0, 0.0, 0.0]));
        // Her portrait copy far away.
        actors.insert("far".to_string(), actor([0.0, 50000.0, 0.0]));
        let ctx = ShotContext { speaker: "far", present: vec!["a", "b"], actors: &actors, cameras: &[], active_camera: None, aspect: 16.0 / 9.0 };
        let two = resolve(&t, &CameraSpec { shot: Some("two".into()), ..Default::default() }, &ctx, &Pose::default()).unwrap();
        assert!(math::len(two.pos) < 1000.0, "{:?}", two.pos);
        assert!(resolve(&t, &CameraSpec { shot: Some("speaker".into()), ..Default::default() }, &ctx, &Pose::default()).is_none());
    }
}
