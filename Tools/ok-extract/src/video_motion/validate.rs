//! Runtime validation: plays a clip on a real session (shape, helpers,
//! dynamics, skinning) and checks every frame against the source
//! expectations and native baselines.

use super::math::*;
use super::model::Model;
use super::retarget::{bend_deg, Reference};
use crate::session::OkSession;
use std::collections::BTreeMap;

pub const JOINTS: [&str; 25] = ["cf_j_root", "cf_j_hips", "cf_j_spine01", "cf_j_spine02", "cf_j_spine03", "cf_j_neck", "cf_j_head",
    "cf_j_shoulder_L", "cf_j_arm00_L", "cf_j_forearm01_L", "cf_j_hand_L", "cf_j_shoulder_R", "cf_j_arm00_R", "cf_j_forearm01_R", "cf_j_hand_R",
    "cf_j_thigh00_L", "cf_j_leg01_L", "cf_j_leg03_L", "cf_j_toes_L", "cf_j_thigh00_R", "cf_j_leg01_R", "cf_j_leg03_R", "cf_j_toes_R",
    "cf_j_waist01", "cf_j_waist02"];
const LIMBS: [&str; 9] = ["cf_j_hand_L", "cf_j_hand_R", "cf_j_leg03_L", "cf_j_leg03_R", "cf_j_head", "cf_j_forearm01_L",
    "cf_j_forearm01_R", "cf_j_leg01_L", "cf_j_leg01_R"];

pub struct Frame {
    pub t: f32,
    pub joints: Vec<V3>,
    /// region (JOINTS index) -> (p99, max, p01) edge length ratio (stretch,
    /// and compression: elbow/knee pinch shortens edges)
    pub stretch: BTreeMap<usize, (f32, f32, f32)>,
    pub mesh_min_y: f32,
}

pub struct Sampler {
    pub session: OkSession,
    body: Vec<usize>,
    rest: Vec<Vec<[f32; 3]>>,
}

impl Sampler {
    pub fn new(mut session: OkSession) -> Sampler {
        session.skin_all();
        let body: Vec<usize> = session.parts.iter().enumerate().filter(|(_, p)| p.name.contains("o_body")).map(|(i, _)| i).collect();
        let rest = body.iter().map(|&i| session.parts[i].positions.clone()).collect();
        Sampler { session, body, rest }
    }

    /// Installs `clip` (portable: shared root; game: keeps the game root) and
    /// samples every 1/fps step.
    pub fn sample(&mut self, clip: &[u8], fps: f32, portable: bool) -> Result<Vec<Frame>, String> {
        let s = &mut self.session;
        let status = unsafe {
            if portable { crate::ffi::ok_session_install_shared_clip(s, clip.as_ptr(), clip.len() as i32) }
            else { super::ok_session_install_video_clip(s, clip.as_ptr(), clip.len() as i32) }
        };
        if status != 0 { return Err("clip install failed".into()); }
        let duration = s.anim_duration();
        let dist = |a: [f32; 3], b: [f32; 3]| ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt();
        let n = (duration * fps).floor() as usize + 1;
        // Let dynamic bones (bust, hair, skirt) settle on the first pose, as
        // they would in game after the previous motion; a cold T-pose start
        // is a harness artifact.
        for _ in 0..(2. * fps) as usize { s.apply_anim_frame(0., 1. / fps); }
        let mut out = Vec::with_capacity(n);
        for f in 0..n {
            // The clip loops: sampling exactly at its duration wraps to frame 0.
            let t = (f as f32 / fps).min(duration - 1e-3);
            s.apply_anim_frame(t, 1. / fps);
            s.skin_all();
            let pos: Vec<[f32; 3]> = JOINTS.iter().map(|j| crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), j)
                .map(|m| [m[0][3], m[1][3], m[2][3]]).unwrap_or([f32::NAN; 3])).collect();
            let mut region: Vec<Vec<f32>> = vec![Vec::new(); JOINTS.len()];
            let mut min_y = f32::INFINITY;
            for (k, &pi) in self.body.iter().enumerate() {
                let p = &s.parts[pi];
                for v in &p.positions { min_y = min_y.min(v[1]); }
                for tri in p.indices.chunks(3) {
                    let (a, b) = (tri[0] as usize, tri[1] as usize);
                    let r0 = dist(self.rest[k][a], self.rest[k][b]);
                    if r0 < 1e-4 { continue; }
                    let r = dist(p.positions[a], p.positions[b]) / r0;
                    let mid = [(p.positions[a][0] + p.positions[b][0]) / 2., (p.positions[a][1] + p.positions[b][1]) / 2., (p.positions[a][2] + p.positions[b][2]) / 2.];
                    let near = (1..JOINTS.len()).min_by(|&x, &y| dist(pos[x], mid).total_cmp(&dist(pos[y], mid))).unwrap_or(1);
                    region[near].push(r);
                }
            }
            let stretch = region.iter_mut().enumerate().filter(|(_, r)| !r.is_empty()).map(|(i, r)| {
                r.sort_by(|a, b| a.total_cmp(b));
                (i, (r[((r.len() - 1) as f32 * 0.99) as usize], r[r.len() - 1], r[((r.len() - 1) as f32 * 0.01) as usize]))
            }).collect();
            out.push(Frame { t, joints: pos.iter().map(|p| [p[0] as f64, p[1] as f64, p[2] as f64]).collect(), stretch, mesh_min_y: min_y });
        }
        Ok(out)
    }
}

impl Sampler {
    /// Installs `clip` and returns, per frame, the skinned body vertices
    /// (every `stride`-th) and the hips world position.
    /// Torso vertices only (within 0.22 m of the hips-neck axis, from below the
    /// hips to the neck): arms wrapped around a partner are not body contact.
    pub fn sample_meshes(&mut self, clip: &[u8], fps: f32, portable: bool, stride: usize) -> Result<Vec<(Vec<[f32; 3]>, [f32; 3])>, String> {
        let s = &mut self.session;
        let status = unsafe {
            if portable { crate::ffi::ok_session_install_shared_clip(s, clip.as_ptr(), clip.len() as i32) }
            else { super::ok_session_install_video_clip(s, clip.as_ptr(), clip.len() as i32) }
        };
        if status != 0 { return Err("clip install failed".into()); }
        let duration = s.anim_duration();
        let n = (duration * fps).floor() as usize + 1;
        for _ in 0..(2. * fps) as usize { s.apply_anim_frame(0., 1. / fps); }
        let mut out = Vec::with_capacity(n);
        for f in 0..n {
            let t = (f as f32 / fps).min(duration - 1e-3);
            s.apply_anim_frame(t, 1. / fps);
            s.skin_all();
            let hips = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), "cf_j_hips").map(|m| [m[0][3], m[1][3], m[2][3]]).unwrap_or([0.; 3]);
            let neck = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), "cf_j_neck").map(|m| [m[0][3], m[1][3], m[2][3]]).unwrap_or(hips);
            let axis = [neck[0] - hips[0], neck[1] - hips[1], neck[2] - hips[2]];
            let len2 = (axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]).max(1e-6);
            let torso = |v: &[f32; 3]| {
                let d = [v[0] - hips[0], v[1] - hips[1], v[2] - hips[2]];
                let t = (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / len2;
                let c = [hips[0] + axis[0] * t, hips[1] + axis[1] * t, hips[2] + axis[2] * t];
                (-0.35..=1.0).contains(&t) && ((v[0] - c[0]).powi(2) + (v[1] - c[1]).powi(2) + (v[2] - c[2]).powi(2)) < 0.22 * 0.22
            };
            let verts = self.body.iter().flat_map(|&pi| s.parts[pi].positions.iter().step_by(stride.max(1)).copied()).filter(|v| torso(v)).collect();
            out.push((verts, hips));
        }
        Ok(out)
    }
}

pub fn frames_to_jsonl(frames: &[Frame]) -> String {
    let mut s = String::new();
    for (f, fr) in frames.iter().enumerate() {
        let joints: serde_json::Map<String, serde_json::Value> = JOINTS.iter().zip(&fr.joints).map(|(j, p)| (j.to_string(), serde_json::json!(p))).collect();
        let stretch: serde_json::Map<String, serde_json::Value> = fr.stretch.iter().map(|(i, v)| (JOINTS[*i].to_string(), serde_json::json!([v.0, v.1, v.2]))).collect();
        s += &serde_json::json!({"frame": f, "t": fr.t, "joints": joints, "stretch": stretch, "mesh_min_y": fr.mesh_min_y}).to_string();
        s.push('\n');
    }
    s
}

#[derive(Default, Debug)]
pub struct Report {
    pub frames: usize,
    pub issues: BTreeMap<usize, Vec<(String, String)>>,
    pub floor_deficit: BTreeMap<usize, f64>,
}

impl Report {
    pub fn bad_frames(&self) -> usize { self.issues.len() }
    pub fn by_kind(&self) -> BTreeMap<String, usize> {
        let mut m = BTreeMap::new();
        for v in self.issues.values() {
            let mut kinds: Vec<&str> = v.iter().map(|(k, _)| k.as_str()).collect();
            kinds.dedup();
            for k in kinds { *m.entry(k.to_string()).or_insert(0) += 1; }
        }
        m
    }
    pub fn has(&self, kind: &str, needle: &str) -> bool {
        self.issues.values().flatten().any(|(k, d)| k == kind && d.contains(needle))
    }
    pub fn to_json(&self) -> serde_json::Value {
        serde_json::json!({"frames": self.frames, "bad_frames": self.bad_frames(), "by_kind": self.by_kind(),
            "issues": self.issues.iter().map(|(f, v)| (f.to_string(), v.iter().map(|(k, d)| format!("{k}: {d}")).collect::<Vec<_>>().into())).collect::<serde_json::Map<String, serde_json::Value>>()})
    }
}

fn kink(p: &[V3]) -> Vec<f64> {
    let mut k = vec![0.; p.len()];
    for f in 1..p.len().saturating_sub(1) { k[f] = norm(sub(p[f], scale(add(p[f + 1], p[f - 1]), 0.5))); }
    k
}

fn seg_dist(p: V3, a: V3, b: V3) -> f64 {
    let ab = sub(b, a);
    let t = (dot(sub(p, a), ab) / dot(ab, ab).max(1e-12)).clamp(0., 1.);
    norm(sub(p, add(a, scale(ab, t))))
}

pub fn check(frames: &[Frame], reference: Option<&Reference>, model: &Model, fps: f64, sex: i32) -> Report {
    let baseline = model.baseline_for(sex);
    let n = frames.len();
    let mut rep = Report { frames: n, ..Default::default() };
    let ji = |name: &str| JOINTS.iter().position(|j| *j == name).unwrap();
    let col = |name: &str| -> Vec<V3> { let i = ji(name); frames.iter().map(|f| f.joints[i]).collect() };
    let add_issue = |rep: &mut Report, f: usize, k: &str, d: String| rep.issues.entry(f).or_default().push((k.into(), d));
    let hips = col("cf_j_hips");
    let neck = col("cf_j_neck");
    let mut torso_card: Vec<f64> = (0..n).map(|f| neck[f][1] - hips[f][1]).collect();
    torso_card.sort_by(|a, b| a.total_cmp(b));
    let torso_card = torso_card[n / 2];

    for (f, fr) in frames.iter().enumerate() {
        if fr.mesh_min_y < -0.015 {
            add_issue(&mut rep, f, "floor", format!("mesh {:.1}cm below floor", -fr.mesh_min_y * 100.));
            rep.floor_deficit.insert(f, -fr.mesh_min_y as f64);
        }
    }
    let ankles = [col("cf_j_leg03_L"), col("cf_j_leg03_R")];
    if let Some(r) = reference {
        let clear = r.kk_clear;
        for f in 0..n.min(r.contact[0].len()) {
            let low = ankles[0][f][1].min(ankles[1][f][1]);
            if (r.contact[0][f] || r.contact[1][f]) && low > clear + 0.05 {
                add_issue(&mut rep, f, "floor", format!("floating {:.1}cm while planted", (low - clear) * 100.));
            }
        }
        for s in 0..2 {
            for f in 1..n.min(r.contact[s].len()) {
                if r.contact[s][f] && r.contact[s][f - 1] {
                    let v = ((ankles[s][f][0] - ankles[s][f - 1][0]).powi(2) + (ankles[s][f][2] - ankles[s][f - 1][2]).powi(2)).sqrt() * fps;
                    if v > 0.15 { add_issue(&mut rep, f, "slide", format!("{} ankle slides {v:.2} m/s", ["L", "R"][s])); }
                }
            }
            let hand = col(&format!("cf_j_hand_{}", ["L", "R"][s]));
            for f in 0..n {
                let d = seg_dist(hand[f], hips[f], neck[f]);
                if d < 0.06 { add_issue(&mut rep, f, "hand", format!("{} wrist inside torso ({:.1}cm from spine)", ["L", "R"][s], d * 100.)); }
                if let Some(want) = r.hand_target[s].get(f) {
                    let want = scale(*want, torso_card / r.torso);
                    let err = norm(sub(sub(hand[f], hips[f]), want));
                    if err > 0.08 { add_issue(&mut rep, f, "hand", format!("{} wrist {:.0}cm from target", ["L", "R"][s], err * 100.)); }
                }
            }
        }
    }
    // Elbow/knee skin compression grows with the bend: compare with native
    // frames bent as far (20-degree bins); no verdict without native evidence.
    let hinge = |name: &str| -> Option<(usize, usize, usize)> {
        let s = name.rsplit('_').next()?;
        match name {
            n if n.starts_with("cf_j_forearm01_") => Some((ji(&format!("cf_j_arm00_{s}")), ji(n), ji(&format!("cf_j_hand_{s}")))),
            n if n.starts_with("cf_j_leg01_") => Some((ji(&format!("cf_j_thigh00_{s}")), ji(n), ji(&format!("cf_j_leg03_{s}")))),
            _ => None,
        }
    };
    for (f, fr) in frames.iter().enumerate() {
        for (region, (p99, _, p01)) in &fr.stretch {
            let name = JOINTS[*region];
            let (lim, what) = match hinge(name) {
                Some((a, b, c)) => {
                    // Linear in the bend between native bin centres; none outside the native range.
                    let deg = bend_deg(fr.joints[a], fr.joints[b], fr.joints[c]);
                    let at = |bin: i64| baseline.get(&format!("compress:{name}@{bin}")).copied();
                    let lo = ((deg - 10.) / 20.).floor() as i64 * 20;
                    let t = ((deg - 10. - lo as f64) / 20.).clamp(0., 1.);
                    let lim = match (at(lo), at(lo + 20)) {
                        (Some(x), Some(y)) => Some(x + (y - x) * t),
                        (Some(x), None) if t < 0.5 => Some(x),
                        (None, Some(y)) if t >= 0.5 => Some(y),
                        _ => None,
                    };
                    (lim, format!(" at {deg:.0} deg bend"))
                }
                None => (baseline.get(&format!("compress:{name}")).copied(), String::new()),
            };
            // The native 1st percentile is noisy per bin; flag only clear
            // collapse (scale bugs measured 0.19 against 0.9).
            if let Some(lim) = lim.map(|l| l * 0.8) {
                if (*p01 as f64) < lim { add_issue(&mut rep, f, "pinch", format!("{name} p01 {p01:.2} < 0.8 x native {:.2}{what}", lim / 0.8)); }
            }
            if let Some(lim) = baseline.get(JOINTS[*region]) {
                if (*p99 as f64) > *lim { add_issue(&mut rep, f, "stretch", format!("{} p99 {p99:.2} > native {lim:.2}", JOINTS[*region])); }
            }
        }
    }
    for name in LIMBS {
        let Some(lim) = baseline.get(&format!("jitter:{name}")) else { continue };
        let p = col(name);
        let rel: Vec<V3> = (0..n).map(|f| sub(p[f], hips[f])).collect();
        for (f, k) in kink(&rel).into_iter().enumerate() {
            if k > *lim { add_issue(&mut rep, f, "jitter", format!("{name} kink {:.1}mm > native {:.1}mm", k * 1000., lim * 1000.)); }
        }
    }
    rep
}
