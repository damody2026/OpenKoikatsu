//! Modifiers and import cleanup on OK animations: trim a leading T-pose, find and cut a loop, close a loop's seam,
//! bake root travel out, smooth jitter, detect foot plants as ground contacts, renormalise. Each returns a new
//! animation; positions come from forward kinematics on a skeleton header (the family rest).

use super::convert::{rest_world_positions, LocalsHeader};
use super::{Contact, OkAnim};
use crate::finalik::{qdot, qinv, qmul, qnorm, qrot, qslerp, vadd, vlen, vsub, Quat, V3};
use std::collections::HashMap;

/// Angle (degrees) of a rotation, stable near zero.
fn angle(q: Quat) -> f32 { let v = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2]).sqrt(); 2.0 * v.atan2(q[3].abs()).to_degrees() }
fn diff(a: Quat, b: Quat) -> f32 { angle(qmul(qinv(a), b)) }

fn frame(anim: &OkAnim, f: usize) -> &[Quat] { let n = anim.tracks.len(); &anim.rot[f * n..(f + 1) * n] }

/// World positions (header space, metres) of every header bone at every frame.
pub fn world_positions(anim: &OkAnim, header: &LocalsHeader) -> Vec<HashMap<String, V3>> {
    world_poses(anim, header).into_iter().map(|f| f.into_iter().map(|(k, (p, _))| (k, p)).collect()).collect()
}

/// World positions and rotations of every header bone at every frame.
pub fn world_poses(anim: &OkAnim, header: &LocalsHeader) -> Vec<HashMap<String, (V3, Quat)>> {
    let index: HashMap<&str, usize> = header.bones.iter().enumerate().map(|(i, b)| (b.name.as_str(), i)).collect();
    // Parents before children.
    let depth = |mut i: usize| { let mut d = 0; while let Some(&p) = index.get(header.bones[i].parent.as_str()) { if p == i || d > 512 { break; } i = p; d += 1; } d };
    let mut order: Vec<usize> = (0..header.bones.len()).collect();
    order.sort_by_key(|&i| depth(i));
    let mut out = Vec::with_capacity(anim.frames as usize);
    let mut buf = vec![0.0f32; header.bones.len() * 4 + 3];
    for f in 0..anim.frames as usize {
        let pose = anim.sample(f as f32 / anim.fps);
        super::ffi::pose_onto(anim, &pose, header, &mut buf);
        let hips_pos = [buf[buf.len() - 3], buf[buf.len() - 2], buf[buf.len() - 1]];
        let mut rot: Vec<Quat> = vec![[0.0, 0.0, 0.0, 1.0]; header.bones.len()];
        let mut pos: Vec<V3> = vec![[0.0; 3]; header.bones.len()];
        for &i in &order {
            let b = &header.bones[i];
            let local = [buf[i * 4], buf[i * 4 + 1], buf[i * 4 + 2], buf[i * 4 + 3]];
            let offset = if b.name == anim.hips_bone { hips_pos } else { b.rest_pos };
            match index.get(b.parent.as_str()) {
                Some(&p) if p != i => { rot[i] = qnorm(qmul(rot[p], local)); pos[i] = vadd(pos[p], qrot(rot[p], offset)); }
                _ => { rot[i] = local; pos[i] = offset; }
            }
        }
        out.push(header.bones.iter().enumerate().map(|(i, b)| (b.name.clone(), (pos[i], rot[i]))).collect());
    }
    out
}

/// A copy of `anim` keeping frames `range` (curves and contacts follow; contacts outside are dropped or clipped).
pub fn keep_frames(anim: &OkAnim, range: std::ops::Range<usize>) -> OkAnim {
    let n = anim.tracks.len();
    let mut a = anim.clone();
    a.rot = anim.rot[range.start * n..range.end * n].to_vec();
    a.hips = anim.hips[range.clone()].to_vec();
    for c in a.curves.iter_mut() { c.values = c.values[range.clone()].to_vec(); }
    a.frames = range.len() as u32;
    let (t0, t1) = (range.start as f32 / anim.fps, (range.end - 1) as f32 / anim.fps);
    a.contacts = anim.contacts.iter().filter(|c| c.end > t0 && c.start < t1).map(|c| {
        let mut c = c.clone();
        c.start = (c.start - t0).max(0.0);
        c.end = (c.end - t0).min(t1 - t0);
        c
    }).collect();
    a
}

/// Leading frames at the source's rest (a T-pose before the motion, as mocap takes have) are removed.
pub fn trim_rest_start(anim: &OkAnim, threshold_deg: f32) -> OkAnim {
    let at_rest = |f: usize| frame(anim, f).iter().all(|&q| angle(q) < threshold_deg);
    let mut first = 0;
    while first + 2 < anim.frames as usize && at_rest(first) { first += 1; }
    // Nothing but rest: keep as is.
    if first == 0 || first + 2 >= anim.frames as usize { return anim.clone(); }
    keep_frames(anim, first..anim.frames as usize)
}

/// Pose distance between two frames: mean bone angle (degrees) plus hips travel in hips heights x 100.
fn pose_distance(anim: &OkAnim, a: usize, b: usize) -> f32 {
    let (fa, fb) = (frame(anim, a), frame(anim, b));
    let rot = fa.iter().zip(fb).map(|(&x, &y)| diff(x, y)).sum::<f32>() / fa.len().max(1) as f32;
    // Velocity too: the same pose moving the other way (half a swing later) is no loop point.
    let last = anim.frames as usize - 1;
    let vel = |f: usize, k: usize| { let n = anim.tracks.len(); qmul(anim.rot[(f + 1).min(last) * n + k], qinv(anim.rot[f.saturating_sub(1) * n + k])) };
    let speed = (0..fa.len()).map(|k| diff(vel(a, k), vel(b, k))).sum::<f32>() / fa.len().max(1) as f32;
    let ha = anim.hips.get(a).copied().unwrap_or([0.0; 3]);
    let hb = anim.hips.get(b).copied().unwrap_or([0.0; 3]);
    rot + speed + vlen([0.0, hb[1] - ha[1], 0.0]) * 100.0
}

/// The best loop: frames (start, end) at least `min_seconds` apart whose poses match best (end repeats start).
pub fn find_loop(anim: &OkAnim, min_seconds: f32) -> Option<(usize, usize, f32)> {
    let n = anim.frames as usize;
    let min_len = ((min_seconds * anim.fps).round() as usize).max(2);
    if n < min_len + 1 { return None; }
    let mut best: Option<(usize, usize, f32)> = None;
    // Start in the first third (the motion settles), end anywhere after the minimum length.
    for s in 0..=(n / 3).min(n - min_len - 1) {
        for e in (s + min_len)..n {
            let d = pose_distance(anim, s, e);
            if best.map_or(true, |b| d < b.2) { best = Some((s, e, d)); }
        }
    }
    best
}

/// Spreads the seam between the last and first frame over the clip, so a loop closes exactly (rotations and hips height).
pub fn close_loop(anim: &OkAnim) -> OkAnim {
    let n = anim.tracks.len();
    let frames = anim.frames as usize;
    if frames < 3 { return anim.clone(); }
    let mut a = anim.clone();
    for k in 0..n {
        let (first, last) = (anim.rot[k], anim.rot[(frames - 1) * n + k]);
        let last = if qdot(first, last) < 0.0 { [-last[0], -last[1], -last[2], -last[3]] } else { last };
        let seam = qnorm(qmul(first, qinv(last)));   // what the last frame lacks to equal the first
        for f in 0..frames {
            let u = f as f32 / (frames - 1) as f32;
            a.rot[f * n + k] = qnorm(qmul(qslerp([0.0, 0.0, 0.0, 1.0], seam, u), anim.rot[f * n + k]));
        }
    }
    if let (Some(h0), Some(h1)) = (anim.hips.first().copied(), anim.hips.last().copied()) {
        for f in 0..frames {
            let u = f as f32 / (frames - 1) as f32;
            a.hips[f][1] += (h0[1] - h1[1]) * u;   // height only: horizontal travel is root motion
        }
    }
    a.looping = true;
    a
}

/// Bakes the hips' net horizontal travel out of the motion (the character walks on the spot).
pub fn bake_in_place(anim: &OkAnim) -> OkAnim {
    let mut a = anim.clone();
    let c = anim.cycle_travel();
    let frames = anim.frames as usize;
    for (f, h) in a.hips.iter_mut().enumerate() {
        let u = if frames > 1 { f as f32 / (frames - 1) as f32 } else { 0.0 };
        h[0] -= c[0] * u;
        h[2] -= c[2] * u;
    }
    a.meta.remove("root_motion");
    a
}

/// Removes jitter: each frame becomes a weighted average of its neighbours within `radius` frames (triangle weights).
pub fn smooth(anim: &OkAnim, radius: usize) -> OkAnim {
    let n = anim.tracks.len();
    let frames = anim.frames as usize;
    if radius == 0 || frames < 3 { return anim.clone(); }
    let mut a = anim.clone();
    let at = |f: isize| -> usize {
        if anim.looping { f.rem_euclid(frames as isize - 1) as usize } else { f.clamp(0, frames as isize - 1) as usize }
    };
    for f in 0..frames {
        for k in 0..n {
            let base = anim.rot[f * n + k];
            let mut acc = [0.0f32; 4];
            for d in -(radius as isize)..=(radius as isize) {
                let w = (radius as f32 + 1.0) - d.unsigned_abs() as f32;
                let q = anim.rot[at(f as isize + d) * n + k];
                let q = if qdot(base, q) < 0.0 { [-q[0], -q[1], -q[2], -q[3]] } else { q };
                for c in 0..4 { acc[c] += q[c] * w; }
            }
            a.rot[f * n + k] = qnorm(acc);
        }
        let mut h = [0.0f32; 3];
        let mut ws = 0.0;
        for d in -(radius as isize)..=(radius as isize) {
            let w = (radius as f32 + 1.0) - d.unsigned_abs() as f32;
            let p = anim.hips[at(f as isize + d)];
            for c in 0..3 { h[c] += p[c] * w; }
            ws += w;
        }
        a.hips[f] = [h[0] / ws, h[1] / ws, h[2] / ws];
    }
    if anim.looping && frames > 1 { let n0 = a.rot[..n].to_vec(); a.rot[(frames - 1) * n..].copy_from_slice(&n0); a.hips[frames - 1] = a.hips[0]; }
    a
}

/// Normalised quaternions with continuous signs (no hemisphere flips between frames).
pub fn renormalize(anim: &OkAnim) -> OkAnim {
    let n = anim.tracks.len();
    let mut a = anim.clone();
    for f in 0..anim.frames as usize {
        for k in 0..n {
            let mut q = qnorm(a.rot[f * n + k]);
            if f > 0 && qdot(a.rot[(f - 1) * n + k], q) < 0.0 { q = [-q[0], -q[1], -q[2], -q[3]]; }
            a.rot[f * n + k] = q;
        }
    }
    a
}

/// Foot plants as ground contacts: a foot low (within `height_tol` hips heights of its lowest point) and still (under
/// `speed_tol` hips heights per second horizontally) for at least `min_time` seconds. Replaces earlier ground contacts.
pub fn detect_foot_contacts(anim: &OkAnim, header: &LocalsHeader, feet: &[&str], height_tol: f32, speed_tol: f32, min_time: f32) -> OkAnim {
    let positions = world_positions(anim, header);
    let hips_h = positions.first().and_then(|p| p.get(&anim.hips_bone)).map(|p| p[1].abs()).filter(|h| *h > 1e-3).unwrap_or(1.0);
    let mut a = anim.clone();
    a.contacts.retain(|c| !(c.target_role == -1 && feet.contains(&c.effector.as_str())));
    let dt = 1.0 / anim.fps;
    let tracks: Vec<Vec<V3>> = feet.iter().map(|foot| positions.iter().filter_map(|p| p.get(*foot).copied()).collect()).collect();
    let velocity = |track: &[V3], f: usize| -> [f32; 2] {
        let prev = track[f.saturating_sub(1)];
        let next = track[(f + 1).min(track.len() - 1)];
        let span = ((f + 1).min(track.len() - 1) - f.saturating_sub(1)).max(1) as f32 * dt;
        [(next[0] - prev[0]) / span / hips_h, (next[2] - prev[2]) / span / hips_h]
    };
    // The ground's own velocity: zero for motion that travels, the walking speed backwards for motion played in
    // place. Estimated as the median velocity of feet at their lowest (planted feet move with the ground).
    let ground = {
        let (mut xs, mut zs) = (Vec::new(), Vec::new());
        for track in tracks.iter().filter(|t| t.len() == positions.len() && t.len() >= 2) {
            let lowest = track.iter().map(|p| p[1]).fold(f32::MAX, f32::min);
            for f in 0..track.len() { if track[f][1] - lowest < height_tol * hips_h { let v = velocity(track, f); xs.push(v[0]); zs.push(v[1]); } }
        }
        let median = |mut v: Vec<f32>| { if v.is_empty() { return 0.0; } v.sort_by(|a, b| a.total_cmp(b)); v[v.len() / 2] };
        [median(xs), median(zs)]
    };
    for (foot, track) in feet.iter().zip(&tracks) {
        if track.len() != positions.len() || track.len() < 2 { continue; }
        let lowest = track.iter().map(|p| p[1]).fold(f32::MAX, f32::min);
        let planted: Vec<bool> = (0..track.len()).map(|f| {
            let v = velocity(track, f);
            let slip = ((v[0] - ground[0]).powi(2) + (v[1] - ground[1]).powi(2)).sqrt();
            track[f][1] - lowest < height_tol * hips_h && slip < speed_tol
        }).collect();
        let mut f = 0;
        while f < planted.len() {
            if !planted[f] { f += 1; continue; }
            let s = f;
            while f < planted.len() && planted[f] { f += 1; }
            let (start, end) = (s as f32 * dt, (f - 1) as f32 * dt);
            if end - start + 1e-4 < min_time { continue; }
            let height = track[s..f].iter().map(|p| p[1]).sum::<f32>() / (f - s) as f32 / hips_h;
            a.contacts.push(Contact {
                role: 0, effector: foot.to_string(), target_role: -1, target_bone: String::new(), offset: [0.0, height, 0.0], start, end,
                blend: (0.1f32).min((end - start) * 0.25), weight: 1.0, priority: 1.0, ik: true, target_moves: 0.0, track: Vec::new(),
            });
        }
    }
    a
}

/// The raw motion (no root stripping) at fractional frame `f`: rotations, hips, curve values.
fn raw_at(anim: &OkAnim, f: f32) -> (Vec<Quat>, V3, Vec<f32>) {
    let n = anim.tracks.len();
    let last = anim.frames as usize - 1;
    let f = f.clamp(0.0, last as f32);
    let i0 = (f.floor() as usize).min(last);
    let i1 = (i0 + 1).min(last);
    let a = f - i0 as f32;
    let rot = (0..n).map(|k| super::nlerp(anim.rot[i0 * n + k], anim.rot[i1 * n + k], a)).collect();
    let hips = super::lerp3(anim.hips[i0], anim.hips[i1], a);
    let curves = anim.curves.iter().map(|c| c.values[i0] + (c.values[i1] - c.values[i0]) * a).collect();
    (rot, hips, curves)
}

/// Rebuilds the motion from source frame positions (`map[i]` = fractional source frame of new frame i) at `fps`;
/// contact times go through `time` (old seconds -> new seconds).
fn remap(anim: &OkAnim, map: &[f32], fps: f32, time: impl Fn(f32) -> f32) -> OkAnim {
    let mut a = anim.clone();
    a.rot.clear();
    a.hips.clear();
    for c in a.curves.iter_mut() { c.values.clear(); }
    for &f in map {
        let (r, h, c) = raw_at(anim, f);
        a.rot.extend(r);
        a.hips.push(h);
        for (k, v) in c.into_iter().enumerate() { a.curves[k].values.push(v); }
    }
    a.frames = map.len() as u32;
    a.fps = fps;
    let end = (map.len().max(1) - 1) as f32 / fps;
    a.contacts = anim.contacts.iter().filter_map(|c| {
        let mut c = c.clone();
        c.start = time(c.start).clamp(0.0, end);
        c.end = time(c.end).clamp(0.0, end);
        (c.end > c.start).then_some(c)
    }).collect();
    a
}

/// Keeps [start, end] seconds.
pub fn trim(anim: &OkAnim, start: f32, end: f32) -> OkAnim {
    let (s, e) = ((start * anim.fps).round().max(0.0) as usize, ((end * anim.fps).round() as usize).min(anim.frames as usize - 1));
    if e <= s { return anim.clone(); }
    keep_frames(anim, s..e + 1)
}

/// The same motion sampled at another rate (the duration stays).
pub fn resample(anim: &OkAnim, fps: f32) -> OkAnim {
    let fps = fps.clamp(1.0, 240.0);
    let n = ((anim.duration() * fps).round() as usize).max(1) + 1;
    let map: Vec<f32> = (0..n).map(|i| i as f32 / fps * anim.fps).collect();
    remap(anim, &map, fps, |t| t)
}

/// Plays `factor` times faster (the frame rate stays, the frame count changes).
pub fn speed(anim: &OkAnim, factor: f32) -> OkAnim {
    let factor = factor.clamp(0.05, 20.0);
    let n = ((anim.duration() / factor * anim.fps).round() as usize).max(1) + 1;
    let map: Vec<f32> = (0..n).map(|i| i as f32 * factor).collect();
    remap(anim, &map, anim.fps, |t| t / factor)
}

/// `count` frames holding frame `at` inserted after it (a pause), or removed from `at` on (count < 0).
pub fn insert_frames(anim: &OkAnim, at: usize, count: i32) -> OkAnim {
    let frames = anim.frames as usize;
    let at = at.min(frames - 1);
    let dt = 1.0 / anim.fps;
    if count >= 0 {
        let c = count as usize;
        let map: Vec<f32> = (0..frames + c).map(|i| if i <= at { i as f32 } else if i <= at + c { at as f32 } else { (i - c) as f32 }).collect();
        let t_at = at as f32 * dt;
        remap(anim, &map, anim.fps, |t| if t > t_at { t + c as f32 * dt } else { t })
    } else {
        let c = ((-count) as usize).min(frames - 1 - at.min(frames - 1)).min(frames.saturating_sub(2));
        let map: Vec<f32> = (0..frames - c).map(|i| if i < at { i as f32 } else { (i + c) as f32 }).collect();
        let (t0, t1) = (at as f32 * dt, (at + c) as f32 * dt);
        remap(anim, &map, anim.fps, |t| if t >= t1 { t - c as f32 * dt } else if t > t0 { t0 } else { t })
    }
}

/// A quality problem over a frame range.
#[derive(serde::Serialize, Debug, Clone)]
pub struct Issue { pub kind: String, pub bone: String, pub start: usize, pub end: usize, pub value: f32 }

/// Frame-by-frame checks: extreme joint turns, knees bending backwards, planted feet sliding, single-frame pops
/// (finger flips), hands inside the torso, feet under the ground. Consecutive frames of one problem merge into a range.
pub fn quality(anim: &OkAnim, header: &LocalsHeader) -> Vec<Issue> {
    let n = anim.tracks.len();
    let frames = anim.frames as usize;
    let mut raw: Vec<(String, String, usize, f32)> = Vec::new();
    let finger = |b: &str| ["thumb", "index", "middle", "ring", "little", "finger"].iter().any(|f| b.contains(f));
    // Rotations: extreme turns from the rest, and pops between consecutive frames (scaled to 30 fps).
    let pop_limit = (60.0 * 30.0 / anim.fps.max(1.0)).min(90.0);
    // Top bones (no animated parent: the character's root, turned freely) are not joints.
    let tracked: std::collections::HashSet<&str> = anim.tracks.iter().map(|t| t.bone.as_str()).collect();
    for f in 0..frames {
        for k in 0..n {
            let bone = &anim.tracks[k].bone;
            if *bone == anim.hips_bone || !tracked.contains(anim.tracks[k].parent.as_str()) { continue; }
            let q = anim.rot[f * n + k];
            let a = angle(q);
            // Elbows and knees fold to ~160 degrees in ordinary poses: only a near-reversal is extreme.
            if a > 170.0 { raw.push(("joint_extreme".into(), bone.clone(), f, a)); }
            if f > 0 {
                let d = diff(anim.rot[(f - 1) * n + k], q);
                if d > pop_limit { raw.push((if finger(bone) { "finger_flip" } else { "pop" }.into(), bone.clone(), f, d)); }
            }
        }
    }
    let poses = world_poses(anim, header);
    let pos: Vec<HashMap<String, V3>> = poses.iter().map(|f| f.iter().map(|(k, (p, _))| (k.clone(), *p)).collect()).collect();
    // Hips height of the standing rest (a lying pose's hips are near the floor).
    let rest = rest_world_positions(header);
    let h = rest.get(&anim.hips_bone).map(|(_, p)| p[1].abs()).filter(|h| *h > 1e-3).unwrap_or(1.0);
    let get = |f: usize, b: &str| pos[f].get(b).copied();
    for f in 0..frames {
        for side in ["L", "R"] {
            // Knee: bends towards the thigh's own front (its rest front, +Z, turned with the thigh).
            let thigh = format!("cf_j_thigh00_{side}");
            if let (Some(hip), Some(knee), Some(ankle), Some(&(_, thigh_rot)), Some((thigh_rest, _))) = (get(f, &thigh), get(f, &format!("cf_j_leg01_{side}")), get(f, &format!("cf_j_leg03_{side}")), poses[f].get(&thigh), rest.get(&thigh)) {
                let mid = [(hip[0] + ankle[0]) * 0.5, (hip[1] + ankle[1]) * 0.5, (hip[2] + ankle[2]) * 0.5];
                let bend = vsub(knee, mid);
                let front = qrot(thigh_rot, qrot(qinv(*thigh_rest), [0.0, 0.0, 1.0]));
                let dot = bend[0] * front[0] + bend[1] * front[1] + bend[2] * front[2];
                if vlen(bend) > 0.04 * h && dot < -0.02 * h { raw.push(("knee_backward".into(), format!("cf_j_leg01_{side}"), f, vlen(bend) / h)); }
            }
            // Feet under the ground (KK characters stand on y = 0).
            let foot = format!("cf_j_leg03_{side}");
            if let Some(p) = get(f, &foot) { if p[1] < -0.03 * h { raw.push(("below_ground".into(), foot.clone(), f, -p[1] / h)); } }
            // Hands inside the torso (spine01 -> neck segment, radius 0.06 hips heights).
            let hand = format!("cf_j_hand_{side}");
            if let (Some(p), Some(a), Some(b)) = (get(f, &hand), get(f, "cf_j_spine01"), get(f, "cf_j_neck")) {
                let ab = vsub(b, a);
                let ap = vsub(p, a);
                let t = ((ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / (ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]).max(1e-9)).clamp(0.0, 1.0);
                let c = [a[0] + ab[0] * t, a[1] + ab[1] * t, a[2] + ab[2] * t];
                let d = vlen(vsub(p, c)) / h;
                if d < 0.06 { raw.push(("hand_in_body".into(), hand.clone(), f, d)); }
            }
        }
    }
    // Planted feet sliding: a ground contact's foot moving horizontally more than 0.05 hips heights.
    for c in anim.contacts.iter().filter(|c| c.target_role == -1) {
        let (s, e) = (((c.start * anim.fps).round() as usize).min(frames - 1), ((c.end * anim.fps).round() as usize).min(frames - 1));
        let Some(p0) = get(s, &c.effector) else { continue };
        for f in s..=e {
            if let Some(p) = get(f, &c.effector) {
                let d = vlen([p[0] - p0[0], 0.0, p[2] - p0[2]]) / h;
                if d > 0.05 { raw.push(("foot_slide".into(), c.effector.clone(), f, d)); }
            }
        }
    }
    // Merge consecutive frames of one kind and bone.
    raw.sort_by(|a, b| (&a.0, &a.1, a.2).cmp(&(&b.0, &b.1, b.2)));
    let mut out: Vec<Issue> = Vec::new();
    for (kind, bone, f, v) in raw {
        match out.last_mut() {
            Some(i) if i.kind == kind && i.bone == bone && i.end + 1 >= f => { i.end = f; i.value = i.value.max(v); }
            _ => out.push(Issue { kind, bone, start: f, end: f, value: v }),
        }
    }
    out.sort_by_key(|i| i.start);
    out
}

/// `{"op": ..., params}` on an animation; `header` (positions) is needed by foot_contacts.
pub fn apply(anim: &OkAnim, op: &serde_json::Value, header: Option<&LocalsHeader>) -> Result<OkAnim, String> {
    let name = op.get("op").and_then(|v| v.as_str()).ok_or("op missing")?;
    let num = |k: &str, d: f64| op.get(k).and_then(|v| v.as_f64()).unwrap_or(d) as f32;
    let out = match name {
        "trim_rest_start" => trim_rest_start(anim, num("threshold", 3.0)),
        "find_loop" | "auto_loop" => {
            let (s, e, d) = find_loop(anim, num("min_seconds", 0.5)).ok_or("clip too short for a loop")?;
            let mut a = if name == "auto_loop" { close_loop(&keep_frames(anim, s..e + 1)) } else { anim.clone() };
            a.meta.insert("loop_start".into(), format!("{}", s as f32 / anim.fps));
            a.meta.insert("loop_end".into(), format!("{}", e as f32 / anim.fps));
            a.meta.insert("loop_error".into(), format!("{d:.3}"));
            a
        }
        "close_loop" => close_loop(anim),
        "trim" => trim(anim, num("start", 0.0), num("end", anim.duration() as f64)),
        "resample" => resample(anim, num("fps", 30.0)),
        "speed" => speed(anim, num("factor", 1.0)),
        "insert_frames" => insert_frames(anim, num("at", 0.0).max(0.0) as usize, num("count", 1.0) as i32),
        "in_place" => bake_in_place(anim),
        "smooth" => smooth(anim, num("radius", 2.0).max(0.0) as usize),
        "renormalize" => renormalize(anim),
        "foot_contacts" => {
            let h = header.ok_or("foot_contacts needs a skeleton header")?;
            let feet: Vec<String> = op.get("feet").and_then(|v| v.as_array()).map(|a| a.iter().filter_map(|x| x.as_str().map(str::to_string)).collect())
                .unwrap_or_else(|| vec!["cf_j_leg03_L".into(), "cf_j_leg03_R".into()]);
            let feet: Vec<&str> = feet.iter().map(String::as_str).collect();
            detect_foot_contacts(anim, h, &feet, num("height", 0.04), num("speed", 0.15), num("min_time", 0.1))
        }
        _ => return Err(format!("unknown op {name}")),
    };
    out.validate()?;
    Ok(out)
}
