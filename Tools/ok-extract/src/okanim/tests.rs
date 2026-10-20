//! OK animation unit tests (no game data needed).

use super::*;

fn anim(frames: u32, angle: f32, looping: bool) -> OkAnim {
    let half = angle * 0.5;
    let rot = (0..frames).flat_map(|f| {
        let a = half * f as f32 / (frames - 1).max(1) as f32;
        [[0.0, 0.0, 0.0, 1.0], [a.sin(), 0.0, 0.0, a.cos()]]
    }).collect();
    OkAnim {
        family: "test".into(), fps: 10.0, frames, looping, scene: 0, hips_bone: "hips".into(), hips_rest: [0.0, 1.0, 0.0],
        hips_parent_rest_world: [0.0, 0.0, 0.0, 1.0],
        tracks: vec![
            Track { bone: "hips".into(), parent: String::new(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
            Track { bone: "arm".into(), parent: "hips".into(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
        ],
        rot, hips: vec![[0.0, 1.0, 0.0]; frames as usize], curves: vec![Curve { name: "c".into(), values: (0..frames).map(|f| f as f32).collect() }],
        contacts: Vec::new(), meta: Default::default(),
    }
}
fn angle_x(q: Quat) -> f32 { 2.0 * q[0].atan2(q[3]) }

#[test]
fn bytes_roundtrip_and_sample() {
    let a = anim(11, 1.0, false);
    let b = OkAnim::from_bytes(&a.to_bytes().unwrap()).unwrap();
    assert_eq!(b.frames, 11);
    assert!((b.duration() - 1.0).abs() < 1e-6);
    let p = b.sample(0.5);
    assert!((angle_x(p.rot[1]) - 0.5).abs() < 1e-3);
    assert!((p.curves[0].1 - 5.0).abs() < 1e-4);
    // Clamped past the end, looped when looping.
    assert!((angle_x(b.sample(5.0).rot[1]) - 1.0).abs() < 1e-3);
    let l = anim(11, 1.0, true);
    assert!((angle_x(l.sample(1.25).rot[1]) - 0.25).abs() < 1e-3);
    assert!(OkAnim::from_bytes(b"OKANIM02xxxx").is_err());
}

#[test]
fn carry_between_rests() {
    // A delta about X authored on an identity rest, carried onto a bone whose rest is turned 90 degrees about Y:
    // the world-space rotation must be the same, so in the target's frame it is a rotation about -Z.
    let d = [0.5f32.sin(), 0.0, 0.0, 0.5f32.cos()];
    let t = [0.0, std::f32::consts::FRAC_PI_4.sin(), 0.0, std::f32::consts::FRAC_PI_4.cos()];
    let c = carry_delta(d, [0.0, 0.0, 0.0, 1.0], t);
    let world_src = d;
    let world_dst = crate::finalik::qmul(crate::finalik::qmul(t, c), crate::finalik::qinv(t));
    for i in 0..4 { assert!((world_src[i] - world_dst[i]).abs() < 1e-5, "{world_src:?} {world_dst:?}"); }
}

fn player(spec: &str, anims: &[OkAnim]) -> Player {
    let blobs: Vec<Vec<u8>> = anims.iter().map(|a| a.to_bytes().unwrap()).collect();
    let refs: Vec<&[u8]> = blobs.iter().map(|b| b.as_slice()).collect();
    Player::new(spec, &refs).unwrap()
}

#[test]
fn blend_1d_2d() {
    let p = player(r#"{"type":"blend","axes":[{"name":"speed","min":0,"max":1}],"samples":[
        {"at":[0],"spec":{"type":"anim","anim":0}},{"at":[1],"spec":{"type":"anim","anim":1}}]}"#, &[anim(11, 0.0, true), anim(11, 1.0, true)]);
    let mut p = p;
    p.params = vec![0.5, 0.0];
    let e = p.eval(1.0 - 1e-4);
    assert!((angle_x(e.roles[0].pose.rot[1]) - 0.5).abs() < 2e-2, "{}", angle_x(e.roles[0].pose.rot[1]));
    let axes = vec![Axis { name: "x".into(), min: 0.0, max: 1.0 }, Axis { name: "y".into(), min: 0.0, max: 1.0 }];
    let s = |x: f32, y: f32| BlendSample { at: vec![x, y], spec: Box::new(Spec::Anim { anim: 0, speed: 1.0 }) };
    let samples = vec![s(0.0, 0.0), s(1.0, 0.0), s(0.0, 1.0), s(1.0, 1.0)];
    for p in [[0.25f32, 0.25], [0.9, 0.6], [0.5, 0.5], [2.0, -1.0]] {
        let w = blend_weights(&axes, &samples, &p);
        let sum: f32 = w.iter().map(|x| x.1).sum();
        assert!((sum - 1.0).abs() < 1e-4, "{p:?} {w:?}");
        // The weighted sample position reproduces the (clamped) parameter.
        let x: f32 = w.iter().map(|(i, w)| samples[*i].at[0] * w).sum();
        let y: f32 = w.iter().map(|(i, w)| samples[*i].at[1] * w).sum();
        assert!((x - p[0].clamp(0.0, 1.0)).abs() < 1e-3 && (y - p[1].clamp(0.0, 1.0)).abs() < 1e-3, "{p:?} {w:?}");
    }
}

#[test]
fn montage_sections_fade() {
    let p = player(r#"{"type":"sequence","sections":[
        {"name":"a","spec":{"type":"anim","anim":0},"start":0,"length":1},
        {"name":"b","spec":{"type":"anim","anim":1},"start":0.8,"length":1,"blend_in":0.2}]}"#, &[anim(11, 0.0, true), anim(11, 1.0, false)]);
    assert!((p.duration() - 1.8).abs() < 1e-5);
    assert!(angle_x(p.eval(0.5).roles[0].pose.rot[1]).abs() < 1e-4);
    // Mid fade: half of b's pose at local 0.1.
    let mid = angle_x(p.eval(0.9).roles[0].pose.rot[1]);
    assert!((mid - 0.05).abs() < 1e-2, "{mid}");
    assert!((angle_x(p.eval(1.8).roles[0].pose.rot[1]) - 1.0).abs() < 1e-3);
    assert!(Player::new(r#"{"type":"sequence","sections":[]}"#, &[]).is_err());
}

#[test]
fn group_contacts_follow_roles() {
    let mut a = anim(11, 0.0, true);
    a.contacts.push(Contact { role: 0, effector: "arm".into(), target_role: -1, target_bone: String::new(), offset: [0.0; 3], start: 0.0, end: 1.0,
        blend: 0.0, weight: 1.0, priority: 0.0, ik: true, target_moves: 0.0, track: Vec::new() });
    let p = player(r#"{"type":"group","roles":[{"spec":{"type":"anim","anim":0}},{"spec":{"type":"anim","anim":0}}],
        "contacts":[{"role":1,"effector":"arm","target_role":0,"target_bone":"hips","offset":[0,0,0],"start":0.2,"end":0.6,"blend":0.1,"weight":1,"priority":1,"ik":true,"target_moves":0.5}]}"#, &[a]);
    assert_eq!(p.role_count(), 2);
    let e = p.eval(0.4);
    // Each role keeps its own ground contact (role index rewritten), plus the group contact at full weight.
    assert_eq!(e.contacts.iter().filter(|(c, _)| c.target_role == -1).map(|(c, _)| c.role).collect::<Vec<_>>(), vec![0, 1]);
    assert!(e.contacts.iter().any(|(c, w)| c.role == 1 && c.target_role == 0 && (*w - 1.0).abs() < 1e-5));
    assert!(p.eval(0.25).contacts.iter().any(|(c, w)| c.target_role == 0 && (*w - 0.5).abs() < 1e-3));
    assert!(!p.eval(0.8).contacts.iter().any(|(c, _)| c.target_role == 0));
}

#[test]
fn rest_alignment_reproduces_source_pose() {
    use crate::finalik::{qrot, vnorm};
    // Family: T-pose (arm along +X). Source: A-pose (arm 45 degrees down), its bones carrying identity rests.
    let id = "[0,0,0,1]";
    let family = format!(r#"{{"fps":30,"frames":1,"hips":"hips","bones":[
        {{"name":"hips","rest_local":{id},"rest_world":{id},"rest_pos":[0,1,0]}},
        {{"name":"arm","parent":"hips","rest_local":{id},"rest_world":{id},"rest_pos":[0.2,0.5,0]}},
        {{"name":"hand","parent":"arm","rest_local":{id},"rest_world":{id},"rest_pos":[0.3,0,0]}}]}}"#);
    let source = |frames: u32| format!(r#"{{"fps":30,"frames":{frames},"hips":"hips","align_to":{family},"bones":[
        {{"name":"hips","rest_local":{id},"rest_world":{id},"rest_pos":[0,1,0]}},
        {{"name":"arm","parent":"hips","rest_local":{id},"rest_world":{id},"rest_pos":[0.2,0.5,0]}},
        {{"name":"hand","parent":"arm","rest_local":{id},"rest_world":{id},"rest_pos":[0.2121,-0.2121,0]}}]}}"#);
    // One frame at the source's own rest.
    let rot: Vec<f32> = [0.0, 0.0, 0.0, 1.0].repeat(3);
    let anim = convert::from_locals(&source(1), &rot, &[0.0, 1.0, 0.0]).unwrap();
    let bytes = anim.to_bytes().unwrap();
    let mut out = vec![0f32; 3 * 4 + 3];
    let n = unsafe { ffi::ok_okanim_sample_onto(bytes.as_ptr(), bytes.len() as i32, std::ffi::CString::new(family.clone()).unwrap().as_ptr(), 0.0, out.as_mut_ptr(), out.len() as i32) };
    assert_eq!(n, 15);
    // The family's arm (rest along +X) must now point 45 degrees down, like the source.
    let arm = [out[4], out[5], out[6], out[7]];
    let dir = vnorm(qrot(arm, [1.0, 0.0, 0.0]));
    assert!((dir[0] - 0.7071).abs() < 2e-3 && (dir[1] + 0.7071).abs() < 2e-3, "{dir:?}");
    // And the reverse: a family rest-pose animation played on the aligned A-pose skeleton lifts its arm to +X.
    let fam_anim = convert::from_locals(&family, &rot, &[0.0, 1.0, 0.0]).unwrap().to_bytes().unwrap();
    let n = unsafe { ffi::ok_okanim_sample_onto(fam_anim.as_ptr(), fam_anim.len() as i32, std::ffi::CString::new(source(1)).unwrap().as_ptr(), 0.0, out.as_mut_ptr(), out.len() as i32) };
    assert_eq!(n, 15);
    let arm = [out[4], out[5], out[6], out[7]];
    let dir = vnorm(qrot(arm, [0.7071, -0.7071, 0.0]));
    assert!((dir[0] - 1.0).abs() < 2e-3, "{dir:?}");
}

#[test]
fn contact_detection_uses_body_surfaces() {
    use std::collections::HashMap;
    let id = [0.0, 0.0, 0.0, 1.0];
    // Role 0 lies along X: thigh from (0,0.2,0) to knee (0.45,0.2,0). Role 1's hand rests on the middle of the thigh
    // surface (8 cm thigh radius + 3 cm hand), 34 cm from either joint origin.
    let mut frames = Vec::new();
    for _ in 0..30 {
        let mut r0 = HashMap::new();
        r0.insert("cf_j_thigh00_L".to_string(), ([0.0, 0.2, 0.0], id));
        r0.insert("cf_j_leg01_L".to_string(), ([0.45, 0.2, 0.0], id));
        let mut r1 = HashMap::new();
        r1.insert("cf_j_hand_R".to_string(), ([0.22, 0.31, 0.0], id));
        frames.push(vec![r0, r1]);
    }
    let c = convert::detect_contacts(&frames, &[0.9, 0.9], 30.0, 0.03, 0.2);
    let hand = c.iter().find(|c| c.role == 1 && c.effector == "cf_j_hand_R").expect("hand contact");
    assert_eq!(hand.target_role, 0);
    assert_eq!(hand.target_bone, "cf_j_thigh00_L");
    assert!(hand.ik && (hand.end - 1.0).abs() < 1e-4);
    // 20 cm away from the surface: no contact.
    for f in frames.iter_mut() { f[1].insert("cf_j_hand_R".to_string(), ([0.22, 0.51, 0.0], id)); }
    assert!(convert::detect_contacts(&frames, &[0.9, 0.9], 30.0, 0.03, 0.2).iter().all(|c| c.role != 1 || c.target_role != 0));
}

#[test]
fn contact_track_follows_the_source() {
    let c = Contact { role: 1, effector: "h".into(), target_role: 0, target_bone: "t".into(), offset: [0.5, 0.0, 0.0], start: 1.0, end: 2.0,
        blend: 0.0, weight: 1.0, priority: 1.0, ik: true, target_moves: 0.5, track: vec![[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [2.0, 0.0, 0.0]] };
    let (r, w) = c.at(1.25, 3.0, false).unwrap();
    assert!((w - 1.0).abs() < 1e-6 && (r.offset[0] - 0.5).abs() < 1e-5 && r.track.is_empty());
    assert!((c.at(2.0, 3.0, false).unwrap().0.offset[0] - 2.0).abs() < 1e-5);
    // Looping: 4.5 is 1.5 of the next cycle.
    assert!((c.at(4.5, 3.0, true).unwrap().0.offset[0] - 1.0).abs() < 1e-5);
    assert!(c.at(0.5, 3.0, false).is_none());
}

/// Diagnostic (run with --ignored after OKAnim.DumpUnreal and OKAnim.SaveFamily): an Unreal frame converted to OK and
/// played on the KK family rest must point every limb segment the way the source does.
#[test]
#[ignore]
fn unreal_dump_directions() {
    use crate::finalik::{qmul, qinv, qrot, vnorm, vsub, vdot};
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Saved/OKAnim/debug");
    let name = std::env::var("OK_DUMP").unwrap_or("Tutorial_Walk_Fwd".into());
    let header = crate::gamefs::read_to_string(root.join(format!("{name}_header.json"))).unwrap();
    let frame: serde_json::Value = serde_json::from_str(&crate::gamefs::read_to_string(root.join(format!("{name}_frame.json"))).unwrap()).unwrap();
    let family = crate::gamefs::read_to_string(root.join("../../../Plugins/OKAnimation/Resources/kk_cf_j_rest.json")).unwrap();
    let rot: Vec<f32> = frame["rot"].as_array().unwrap().iter().map(|v| v.as_f64().unwrap() as f32).collect();
    let hips: Vec<f32> = frame["hips"].as_array().unwrap().iter().map(|v| v.as_f64().unwrap() as f32).collect();
    let names: Vec<String> = frame["names"].as_array().unwrap().iter().map(|v| v.as_str().unwrap().to_string()).collect();
    let pos: Vec<[f32; 3]> = frame["pos"].as_array().unwrap().iter().map(|v| { let a = v.as_array().unwrap(); [a[0].as_f64().unwrap() as f32, a[1].as_f64().unwrap() as f32, a[2].as_f64().unwrap() as f32] }).collect();
    let anim = convert::from_locals(&header, &rot, &hips).unwrap();
    let bytes = anim.to_bytes().unwrap();
    let fam: serde_json::Value = serde_json::from_str(&family).unwrap();
    let fb = fam["bones"].as_array().unwrap();
    let mut out = vec![0f32; fb.len() * 4 + 3];
    let n = unsafe { ffi::ok_okanim_sample_onto(bytes.as_ptr(), bytes.len() as i32, std::ffi::CString::new(family.clone()).unwrap().as_ptr(), 0.0, out.as_mut_ptr(), out.len() as i32) };
    assert_eq!(n as usize, out.len());
    let q4 = |v: &serde_json::Value| { let a = v.as_array().unwrap(); [a[0].as_f64().unwrap() as f32, a[1].as_f64().unwrap() as f32, a[2].as_f64().unwrap() as f32, a[3].as_f64().unwrap() as f32] };
    let v3 = |v: &serde_json::Value| { let a = v.as_array().unwrap(); [a[0].as_f64().unwrap() as f32, a[1].as_f64().unwrap() as f32, a[2].as_f64().unwrap() as f32] };
    let idx: std::collections::HashMap<String, usize> = fb.iter().enumerate().map(|(i, b)| (b["name"].as_str().unwrap().to_string(), i)).collect();
    // World rotation by FK; a parent outside the family moves rigidly with the nearest family bone above it (by rest).
    let fallback = |p: &str| -> Option<&str> { if p.starts_with("cf_d_shoulder") { Some("cf_j_spine03") } else { None } };
    fn world(i: usize, fb: &[serde_json::Value], idx: &std::collections::HashMap<String, usize>, out: &[f32], fallback: &dyn Fn(&str) -> Option<&'static str>,
        q4: &dyn Fn(&serde_json::Value) -> [f32; 4]) -> [f32; 4] {
        let b = &fb[i];
        let local = [out[i * 4], out[i * 4 + 1], out[i * 4 + 2], out[i * 4 + 3]];
        let p = b["parent"].as_str().unwrap();
        if let Some(&pi) = idx.get(p) { return qmul(world(pi, fb, idx, out, fallback, q4), local); }
        if let Some(a) = fallback(p).and_then(|a| idx.get(a)) {
            // parent world at rest relative to the ancestor: Rw_anc^-1 * Rw_parent_rest, Rw_parent_rest = Rw_b * inv(rest_local_b).
            let anc_w = world(*a, fb, idx, out, fallback, q4);
            let parent_rest = qmul(q4(&b["rest_world"]), qinv(q4(&b["rest_local"])));
            return qmul(qmul(anc_w, qmul(qinv(q4(&fb[*a]["rest_world"])), parent_rest)), local);
        }
        let parent_rest = qmul(q4(&b["rest_world"]), qinv(q4(&b["rest_local"])));
        qmul(parent_rest, local)
    }
    let fbs: Vec<serde_json::Value> = fb.clone();
    let segs = [("cf_j_hips", "cf_j_spine01"), ("cf_j_spine01", "cf_j_spine02"), ("cf_j_spine02", "cf_j_spine03"), ("cf_j_spine03", "cf_j_neck"), ("cf_j_neck", "cf_j_head"),
        ("cf_j_shoulder_L", "cf_j_arm00_L"), ("cf_j_arm00_L", "cf_j_forearm01_L"), ("cf_j_forearm01_L", "cf_j_hand_L"),
        ("cf_j_shoulder_R", "cf_j_arm00_R"), ("cf_j_arm00_R", "cf_j_forearm01_R"), ("cf_j_forearm01_R", "cf_j_hand_R"),
        ("cf_j_thigh00_L", "cf_j_leg01_L"), ("cf_j_leg01_L", "cf_j_leg03_L"), ("cf_j_thigh00_R", "cf_j_leg01_R"), ("cf_j_leg01_R", "cf_j_leg03_R")];
    let mut worst = 0.0f32;
    for (a, c) in segs {
        let (Some(sa), Some(sc)) = (names.iter().position(|n| n == a), names.iter().position(|n| n == c)) else { continue };
        let src = vnorm(vsub(pos[sc], pos[sa]));
        let wa = world(idx[a], &fbs, &idx, &out, &fallback, &q4);
        let dst = vnorm(qrot(wa, v3(&fb[idx[c]]["rest_pos"])));
        let ang = vdot(src, dst).clamp(-1.0, 1.0).acos().to_degrees();
        worst = worst.max(ang);
        println!("{a:>18} -> {c:<18} src {:>6.3} {:>6.3} {:>6.3}  ok {:>6.3} {:>6.3} {:>6.3}  {ang:6.1} deg", src[0], src[1], src[2], dst[0], dst[1], dst[2]);
    }
    println!("worst {worst:.1} deg");
}

#[test]
fn dense_tracks_roundtrip() {
    let a = anim(5, 1.0, false);
    let b = a.to_bytes().unwrap();
    let n = unsafe { ffi::ok_okanim_get_tracks(b.as_ptr(), b.len() as i32, std::ptr::null_mut(), 0) };
    assert_eq!(n as usize, 5 * 2 * 4 + 5 * 3);
    let mut d = vec![0f32; n as usize];
    unsafe { ffi::ok_okanim_get_tracks(b.as_ptr(), b.len() as i32, d.as_mut_ptr(), n) };
    // Turn track 0 by 90 degrees about Y on every frame, lift the hips.
    for f in 0..5 { let o = f * 2 * 4; d[o..o + 4].copy_from_slice(&[0.0, 0.70710678, 0.0, 0.70710678]); }
    for f in 0..5 { d[40 + f * 3 + 1] = 2.0; }
    let need = unsafe { ffi::ok_okanim_set_tracks(b.as_ptr(), b.len() as i32, d.as_ptr(), n, std::ptr::null_mut(), 0) };
    let mut out = vec![0u8; need as usize];
    unsafe { ffi::ok_okanim_set_tracks(b.as_ptr(), b.len() as i32, d.as_ptr(), n, out.as_mut_ptr(), need) };
    let e = OkAnim::from_bytes(&out).unwrap();
    assert!((e.rot[0][1] - 0.70710678).abs() < 1e-5 && (e.hips[3][1] - 2.0).abs() < 1e-6);
    assert!((angle_x(e.rot[1 * 2 + 1]) - angle_x(a.rot[1 * 2 + 1])).abs() < 1e-6);
}

#[test]
fn dense_tracks_change_length() {
    let a = anim(5, 1.0, false);
    let b = a.to_bytes().unwrap();
    let n = unsafe { ffi::ok_okanim_get_tracks(b.as_ptr(), b.len() as i32, std::ptr::null_mut(), 0) } as usize;
    let mut d = vec![0f32; n];
    unsafe { ffi::ok_okanim_get_tracks(b.as_ptr(), b.len() as i32, d.as_mut_ptr(), n as i32) };
    // 5 frames -> 8: rotations then hips, the last frame repeated.
    let (rot, hips) = d.split_at(5 * 2 * 4);
    let mut longer: Vec<f32> = rot.to_vec();
    for _ in 0..3 { longer.extend_from_slice(&rot[4 * 2 * 4..]); }
    longer.extend_from_slice(hips);
    for _ in 0..3 { longer.extend_from_slice(&hips[4 * 3..]); }
    let need = unsafe { ffi::ok_okanim_set_tracks(b.as_ptr(), b.len() as i32, longer.as_ptr(), longer.len() as i32, std::ptr::null_mut(), 0) };
    let mut out = vec![0u8; need as usize];
    unsafe { ffi::ok_okanim_set_tracks(b.as_ptr(), b.len() as i32, longer.as_ptr(), longer.len() as i32, out.as_mut_ptr(), need) };
    let e = OkAnim::from_bytes(&out).unwrap();
    assert_eq!(e.frames, 8);
    assert_eq!(e.curves[0].values.len(), 8);
    assert!((e.curves[0].values[7] - 4.0).abs() < 1e-6);
    assert!((angle_x(e.rot[7 * 2 + 1]) - 1.0).abs() < 1e-4);
    // Not a whole number of frames: refused.
    let bad = &longer[..longer.len() - 1];
    assert!(unsafe { ffi::ok_okanim_set_tracks(b.as_ptr(), b.len() as i32, bad.as_ptr(), bad.len() as i32, std::ptr::null_mut(), 0) } < 0);
}

#[test]
fn crossfade_switches_smoothly() {
    let p = player(r#"{"type":"crossfade","from":{"type":"anim","anim":0},"to":{"type":"anim","anim":1},"from_time":0.5,"duration":0.2}"#,
        &[anim(11, 1.0, true), anim(11, 0.0, true)]);
    // Start: the old animation where it was (0.5 s -> 0.5 rad); end of the fade: the new one only.
    assert!((angle_x(p.eval(0.0).roles[0].pose.rot[1]) - 0.5).abs() < 1e-3);
    assert!(angle_x(p.eval(0.2).roles[0].pose.rot[1]).abs() < 1e-4);
    let mid = angle_x(p.eval(0.1).roles[0].pose.rot[1]);
    assert!(mid > 0.2 && mid < 0.4, "{mid}");
    assert!((p.duration() - 1.0).abs() < 1e-5);
}

#[test]
fn root_motion_in_place_and_drift() {
    let mut a = anim(11, 0.0, true);
    for (f, h) in a.hips.iter_mut().enumerate() { h[2] = f as f32 * 0.1; } // walks 1 hips height forward per cycle
    a.meta.insert("root_motion".into(), "root_motion".into());
    let b = a.to_bytes().unwrap();
    let p = Player::new(r#"{"type":"anim","anim":0}"#, &[&b]).unwrap();
    // The pose stays in place, the travel is accumulated over cycles.
    assert!(p.eval(0.5).roles[0].pose.hips[2].abs() < 1e-4);
    assert!((p.root_drift(0, 0.5)[2] - 0.5).abs() < 1e-4);
    assert!((p.root_drift(0, 2.5)[2] - 2.5).abs() < 1e-4);
    a.meta.insert("root_motion".into(), "in_place".into());
    let c = a.to_bytes().unwrap();
    let q = Player::new(r#"{"type":"anim","anim":0}"#, &[&c]).unwrap();
    assert!(q.root_drift(0, 2.5)[2].abs() < 1e-6 && q.eval(0.5).roles[0].pose.hips[2].abs() < 1e-4);
}

#[test]
fn aliases_reach_renamed_bones() {
    let mut p = player(r#"{"type":"anim","anim":0}"#, &[anim(5, 1.0, false)]);
    p.aliases.insert("arm".into(), "mod_arm".into());
    assert_eq!(p.aliases.get("arm").map(String::as_str), Some("mod_arm"));
    let j = r#"{"arm":"mod_arm2"}"#;
    let c = std::ffi::CString::new(j).unwrap();
    assert_eq!(unsafe { ffi::ok_player_set_aliases(&mut p, c.as_ptr()) }, 0);
    assert_eq!(p.aliases.get("arm").map(String::as_str), Some("mod_arm2"));
}

#[test]
fn curves_reach_the_ffi() {
    let p = player(r#"{"type":"anim","anim":0}"#, &[anim(5, 1.0, false)]);
    let mut buf = vec![0u8; 256];
    let n = unsafe { ffi::ok_player_curves(&p, 0, 0.0, buf.as_mut_ptr(), buf.len() as i32) };
    assert!(n > 0, "{n}");
    let m: std::collections::BTreeMap<String, f32> = serde_json::from_slice(&buf[..n as usize]).unwrap();
    assert!(m.contains_key("c"));
}

/// Prints a .okgroup / .okanim summary: `OK_INSPECT=<file> cargo test --release --lib inspect_ok_file -- --ignored --nocapture`.
#[test]
#[ignore]
fn inspect_ok_file() {
    let b = crate::gamefs::read(std::env::var("OK_INSPECT").unwrap()).unwrap();
    let mut anims = Vec::new();
    if b.starts_with(b"OKGROUP1") {
        let u = |o: usize| u32::from_le_bytes(b[o..o + 4].try_into().unwrap()) as usize;
        let n = u(8);
        println!("spec: {}", String::from_utf8_lossy(&b[12..12 + n]));
        let mut o = 12 + n;
        let count = u(o); o += 4;
        for _ in 0..count { let l = u(o); o += 4; anims.push(OkAnim::from_bytes(&b[o..o + l]).unwrap()); o += l; }
    } else { anims.push(OkAnim::from_bytes(&b).unwrap()); }
    for (i, a) in anims.iter().enumerate() {
        println!("role {i}: frames={} bones={} curves={:?}", a.frames, a.tracks.len(), a.curves.iter().map(|c| format!("{}={:.3}", c.name, c.values.first().copied().unwrap_or(0.0))).collect::<Vec<_>>());
        for c in &a.contacts { println!("  contact {:?}", c); }
    }
}

/// Cross-fade continuity on two real files: `OK_A=<a.okanim> OK_B=<b.okanim> cargo test --release --lib inspect_crossfade -- --ignored --nocapture`.
#[test]
#[ignore]
fn inspect_crossfade() {
    let a = crate::gamefs::read(std::env::var("OK_A").unwrap()).unwrap();
    let b = crate::gamefs::read(std::env::var("OK_B").unwrap()).unwrap();
    let from_time = 0.5f32;
    let spec = format!(r#"{{"type":"crossfade","from":{{"type":"anim","anim":0}},"to":{{"type":"anim","anim":1}},"from_time":{from_time},"duration":0.3}}"#);
    let p = Player::new(&spec, &[&a, &b]).unwrap();
    let only_a = Player::new(r#"{"type":"anim","anim":0}"#, &[&a]).unwrap();
    let angle = |x: [f32; 4], y: [f32; 4]| { let d = (x[0] * y[0] + x[1] * y[1] + x[2] * y[2] + x[3] * y[3]).abs().min(1.0); 2.0 * d.acos().to_degrees() };
    let step = |e0: &super::Eval, e1: &super::Eval| {
        let (r0, r1) = (&e0.roles[0], &e1.roles[0]);
        let mut worst = (0.0f32, String::new());
        for (i, t) in r0.anim.tracks.iter().enumerate() {
            if let Some(j) = r1.anim.tracks.iter().position(|u| u.bone == t.bone) {
                let g = angle(r0.pose.rot[i], r1.pose.rot[j]);
                if g > worst.0 { worst = (g, t.bone.clone()); }
            }
        }
        worst
    };
    let prev = only_a.eval(from_time - 1.0 / 30.0);
    let first = p.eval(0.0);
    println!("A(from_time) vs fade(0): {:?}", step(&only_a.eval(from_time), &first));
    println!("A(from_time-1/30) vs fade(0): {:?}", step(&prev, &first));
    let mut last = first;
    for f in 1..15 { let e = p.eval(f as f32 / 30.0); println!("frame {f}: {:?}", step(&last, &e)); last = e; }
}

/// The background batch converts exactly like the session path (needs the KK install): `cargo test --release --lib batch_matches_session -- --ignored --nocapture`.
#[test]
#[ignore]
fn batch_matches_session() {
    let game = std::path::PathBuf::from(std::env::var("OK_GAME").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
    let card = std::path::PathBuf::from(std::env::var("OK_CARD").unwrap());
    let mut s = crate::session::open(&game, Some(&card), 0).unwrap();
    s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
    let source = crate::anim::clip_sources(&game).into_iter().find(|x| !crate::anim::clip_names_for_source(&game, x).is_empty()).unwrap();
    let keys: Vec<String> = crate::anim::clip_names_for_source(&game, &source).into_iter().take(6).map(|l| format!("{source}|{l}")).collect();
    let dir = std::env::temp_dir().join("ok_batch_test");
    let _ = std::fs::remove_dir_all(&dir);
    let items: Vec<super::batch::Item> = keys.iter().enumerate().map(|(i, k)| super::batch::Item { key: k.clone(), file: dir.join(format!("{i}.okanim")).to_string_lossy().into(), source: source.clone() }).collect();
    let sampler = convert::ClipSampler::of_session(&mut s).unwrap();
    let t0 = std::time::Instant::now();
    let b = super::batch::Batch::start(sampler, items, 30.0, 4);
    loop { let p = b.poll(0); if !p["running"].as_bool().unwrap() { println!("{p}"); break; } std::thread::sleep(std::time::Duration::from_millis(50)); }
    println!("batch {:?}", t0.elapsed());
    let t1 = std::time::Instant::now();
    for (i, k) in keys.iter().enumerate() {
        let a = convert::from_session_clip(&mut s, k, 30.0).unwrap().to_bytes().unwrap();
        let b = crate::gamefs::read(dir.join(format!("{i}.okanim"))).unwrap();
        assert_eq!(OkAnim::from_bytes(&a).unwrap().rot, OkAnim::from_bytes(&b).unwrap().rot, "{k}");
    }
    println!("session {:?}", t1.elapsed());
}

/// Where converting H clips spends its time: `OK_CARD=<png> cargo test --release --lib profile_h_clips -- --ignored --nocapture`.
#[test]
#[ignore]
fn profile_h_clips() {
    let game = std::path::PathBuf::from(std::env::var("OK_GAME").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
    let card = std::path::PathBuf::from(std::env::var("OK_CARD").unwrap());
    let mut s = crate::session::open(&game, Some(&card), 0).unwrap();
    s.apply_shape(&s.shape_body.clone(), &s.shape_face.clone());
    let sampler = convert::ClipSampler::of_session(&mut s).unwrap();
    let sources: Vec<String> = crate::anim::clip_sources(&game).into_iter().filter(|x| x.starts_with("states:")).take(3).collect();
    println!("{} state sources", sources.len());
    for src in sources {
        crate::anim::begin_animation_preparation();
        let t = std::time::Instant::now();
        let labels = crate::anim::clip_names_for_source(&game, &src);
        println!("source {}.. labels={} in {:?}", &src[..src.len().min(60)], labels.len(), t.elapsed());
        for l in labels.iter().take(4) {
            let key = format!("{src}|{l}");
            let t = std::time::Instant::now();
            let d = crate::anim::clip_duration_parameters(&game, &key, &sampler.params);
            let t1 = t.elapsed();
            let lp = crate::anim::clip_looping(&game, &key);
            let t2 = t.elapsed();
            let sc = crate::anim::clip_scene_code(&game, &key);
            let t3 = t.elapsed();
            let mut sk = sampler.base.clone();
            crate::anim::crossfade::sample(&game, &mut sk, &key, 0.1, &sampler.params, sampler.clock);
            let t4 = t.elapsed();
            crate::anim::crossfade::sample(&game, &mut sk, &key, 0.2, &sampler.params, sampler.clock);
            let t5 = t.elapsed();
            let a = convert::from_clip(&sampler, &key, 30.0, None).map(|a| a.frames).unwrap_or(0);
            println!("  {l}: dur {d:.2} ({t1:?}) loop {lp} ({:?}) scene {sc} ({:?}) sample1 {:?} sample2 {:?} full frames={a} {:?}", t2 - t1, t3 - t2, t4 - t3, t5 - t4, t.elapsed() - t5);
        }
        crate::anim::end_animation_preparation();
    }
}

fn set_arm(a: &mut OkAnim, f: usize, radians: f32) { a.rot[f * 2 + 1] = [(radians * 0.5).sin(), 0.0, 0.0, (radians * 0.5).cos()]; }

#[test]
fn ops_trim_loop_smooth_in_place() {
    // Three rest frames (a T-pose), then motion.
    let mut a = anim(40, 0.0, false);
    for f in 3..40 { set_arm(&mut a, f, 0.5 + 0.4 * ((f - 3) as f32 * std::f32::consts::TAU / 10.0).sin()); }
    let t = ops::trim_rest_start(&a, 3.0);
    assert_eq!(t.frames, 37);
    assert!((angle_x(t.rot[1]) - 0.5).abs() < 1e-3);
    // A 10-frame period: the best loop is a multiple of it, matching almost exactly; auto_loop cuts and closes it.
    let (s, e, d) = ops::find_loop(&t, 0.5).unwrap();
    assert_eq!((e - s) % 10, 0, "{s}..{e}");
    assert!(d < 0.01, "{d}");
    let l = ops::apply(&t, &serde_json::json!({"op": "auto_loop", "min_seconds": 0.5}), None).unwrap();
    assert!(l.looping && (l.frames as usize - 1) % 10 == 0);
    assert!((angle_x(l.rot[1]) - angle_x(l.rot[(l.frames as usize - 1) * 2 + 1])).abs() < 1e-4);
    // close_loop: an open clip's last frame becomes its first.
    let c = ops::close_loop(&anim(11, 1.0, false));
    assert!((angle_x(c.rot[1]) - angle_x(c.rot[10 * 2 + 1])).abs() < 1e-4);
    // smooth: alternating +-5 degree jitter shrinks.
    let mut j = anim(21, 0.0, false);
    for f in 0..21 { set_arm(&mut j, f, if f % 2 == 0 { 0.087 } else { -0.087 }); }
    let sm = ops::smooth(&j, 2);
    assert!(angle_x(sm.rot[10 * 2 + 1]).abs() < 0.03, "{}", angle_x(sm.rot[10 * 2 + 1]));
    // in_place: the hips' travel goes, the height stays.
    let mut w = anim(11, 0.0, false);
    for (f, h) in w.hips.iter_mut().enumerate() { h[2] = f as f32 * 0.1; h[1] = 1.0 + 0.01 * f as f32; }
    let ip = ops::bake_in_place(&w);
    assert!(ip.hips.iter().all(|h| h[2].abs() < 1e-5) && (ip.hips[5][1] - 1.05).abs() < 1e-5);
}

#[test]
fn ops_foot_contacts() {
    // hips -> foot; the hips rise after frame 9, lifting the foot off the ground.
    let mut a = anim(20, 0.0, false);
    a.tracks[1].bone = "foot".into();
    for f in 10..20 { a.hips[f][1] = 1.3; }
    let header: convert::LocalsHeader = serde_json::from_value(serde_json::json!({
        "family": "test", "fps": 10.0, "frames": 1, "hips": "hips",
        "bones": [
            {"name": "hips", "parent": "", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [0, 1, 0]},
            {"name": "foot", "parent": "hips", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [0.1, -0.9, 0]}
        ]})).unwrap();
    let pos = ops::world_positions(&a, &header);
    assert!((pos[0]["foot"][1] - 0.1).abs() < 1e-4 && (pos[15]["foot"][1] - 0.4).abs() < 1e-4, "{:?} {:?}", pos[0]["foot"], pos[15]["foot"]);
    let c = ops::detect_foot_contacts(&a, &header, &["foot"], 0.04, 0.15, 0.1);
    assert_eq!(c.contacts.len(), 1, "{:?}", c.contacts);
    let k = &c.contacts[0];
    assert!(k.target_role == -1 && k.start == 0.0 && (k.end - 0.9).abs() < 1e-4 && (k.offset[1] - 0.1).abs() < 1e-3, "{k:?}");
}

#[test]
fn ops_time_tools() {
    let a = anim(11, 1.0, false);   // 1 s at 10 fps, arm 0 -> 1 rad, curve 0..10
    let t = ops::trim(&a, 0.2, 0.6);
    assert_eq!(t.frames, 5);
    assert!((angle_x(t.rot[1]) - 0.2).abs() < 1e-3 && (t.curves[0].values[0] - 2.0).abs() < 1e-4);
    let r = ops::resample(&a, 20.0);
    assert_eq!(r.frames, 21);
    assert!((r.duration() - 1.0).abs() < 1e-5 && (angle_x(r.rot[11 * 2 + 1]) - 0.55).abs() < 1e-3);
    let s = ops::speed(&a, 2.0);
    assert_eq!(s.frames, 6);
    assert!((angle_x(s.rot[5 * 2 + 1]) - 1.0).abs() < 1e-3);
    let mut c = a.clone();
    c.contacts.push(Contact { role: 0, effector: "arm".into(), target_role: -1, target_bone: String::new(), offset: [0.0; 3], start: 0.5, end: 0.8, blend: 0.0, weight: 1.0, priority: 1.0, ik: true, target_moves: 0.0, track: vec![] });
    let i = ops::insert_frames(&c, 3, 2);   // hold frame 3 for 2 more frames
    assert_eq!(i.frames, 13);
    assert!((angle_x(i.rot[5 * 2 + 1]) - 0.3).abs() < 1e-3 && (angle_x(i.rot[6 * 2 + 1]) - 0.4).abs() < 1e-3);
    assert!((i.contacts[0].start - 0.7).abs() < 1e-4);
    let d = ops::insert_frames(&i, 4, -2);
    assert_eq!(d.frames, 11);
    assert!(d.rot.iter().zip(&c.rot).all(|(x, y)| (x[0] - y[0]).abs() < 1e-5));
}

#[test]
fn ops_quality_detects_problems() {
    // hips -> foot; a planted contact while the hips (and foot) slide, a pop, a foot under the ground.
    let mut a = anim(20, 0.0, false);
    a.tracks[1].bone = "cf_j_leg03_L".into();
    a.tracks[1].parent = "hips".into();
    for f in 0..10 { a.hips[f][0] = f as f32 * 0.02; }
    a.rot[12 * 2 + 1] = [(1.2f32).sin(), 0.0, 0.0, (1.2f32).cos()];   // 137 degrees for one frame
    for f in 15..20 { a.hips[f][1] = 0.85; }
    a.contacts.push(Contact { role: 0, effector: "cf_j_leg03_L".into(), target_role: -1, target_bone: String::new(), offset: [0.0, 0.1, 0.0], start: 0.0, end: 0.9, blend: 0.0, weight: 1.0, priority: 1.0, ik: true, target_moves: 0.0, track: vec![] });
    let header: convert::LocalsHeader = serde_json::from_value(serde_json::json!({
        "family": "test", "fps": 10.0, "frames": 1, "hips": "hips",
        "bones": [
            {"name": "hips", "parent": "", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [0, 1, 0]},
            {"name": "cf_j_leg03_L", "parent": "hips", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [0.1, -0.9, 0]}
        ]})).unwrap();
    let issues = ops::quality(&a, &header);
    let kinds: Vec<&str> = issues.iter().map(|i| i.kind.as_str()).collect();
    assert!(kinds.contains(&"foot_slide") && kinds.contains(&"pop") && kinds.contains(&"below_ground"), "{issues:?}");
    let below = issues.iter().find(|i| i.kind == "below_ground").unwrap();
    assert_eq!((below.start, below.end), (15, 19));
}

#[test]
fn ops_foot_contacts_in_place() {
    // Two feet alternating: each low and sliding backwards (the ground under an in-place walk) for half the cycle.
    let frames = 40u32;
    let mut a = anim(frames, 0.0, true);
    a.tracks = vec![
        Track { bone: "hips".into(), parent: String::new(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
        Track { bone: "footL".into(), parent: "hips".into(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
        Track { bone: "footR".into(), parent: "hips".into(), rest_local: [0.0, 0.0, 0.0, 1.0], rest_world: [0.0, 0.0, 0.0, 1.0] },
    ];
    a.rot = vec![[0.0, 0.0, 0.0, 1.0]; frames as usize * 3];
    a.curves.clear();
    // Feet are children of the hips at fixed offsets; moving / lifting is done by moving the hips is not possible for
    // two feet independently, so the header places feet and the test checks the median-velocity estimate directly.
    let header: convert::LocalsHeader = serde_json::from_value(serde_json::json!({
        "family": "test", "fps": 10.0, "frames": 1, "hips": "hips",
        "bones": [
            {"name": "hips", "parent": "", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [0, 1, 0]},
            {"name": "footL", "parent": "hips", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [0.1, -0.95, 0]},
            {"name": "footR", "parent": "hips", "rest_local": [0,0,0,1], "rest_world": [0,0,0,1], "rest_pos": [-0.1, -0.95, 0]}
        ]})).unwrap();
    // The whole body glides backwards at 0.5 hips heights / s (in-place walk ground speed): both feet low and moving.
    for (f, h) in a.hips.iter_mut().enumerate() { h[2] = -0.05 * f as f32; }
    let c = ops::detect_foot_contacts(&a, &header, &["footL", "footR"], 0.04, 0.15, 0.1);
    assert_eq!(c.contacts.len(), 2, "{:?}", c.contacts);
}

/// Quality issues of one file on the family rest: `OK_INSPECT=<file> cargo test --release --lib inspect_quality -- --ignored --nocapture`.
#[test]
#[ignore]
fn inspect_quality() {
    let b = crate::gamefs::read(std::env::var("OK_INSPECT").unwrap()).unwrap();
    let a = OkAnim::from_bytes(&b).unwrap();
    let h = crate::gamefs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../../Plugins/OKAnimation/Resources/kk_cf_j_rest.json")).unwrap();
    let header: convert::LocalsHeader = serde_json::from_str(&h).unwrap();
    for i in ops::quality(&a, &header) { println!("{} {} {}-{} {:.2}", i.kind, i.bone, i.start, i.end, i.value); }
}
