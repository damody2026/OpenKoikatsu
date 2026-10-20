//! Rest skeleton for video-motion retargeting (Tools/video_motion).
//!
//! Dumps the freshly opened (T-pose) body rig in Unity local space so the
//! offline retargeter can turn SMPL-X joint rotations into absolute
//! `cf_j_*` local channels for an OKCLIP01 resource.

use std::path::Path;

use crate::session::OkSession;

pub mod math;
pub mod model;
mod npy;
pub mod retarget;
pub mod rig;
pub mod train;
pub mod validate;

#[derive(Clone)]
pub struct ConvertOptions<'a> {
    pub motion: &'a Path,
    pub out: &'a Path,
    pub skeleton: &'a Path,
    pub smplx: &'a Path,
    pub model: &'a Path,
    /// Card used for runtime validation (None: no validation / auto-fix loop).
    pub card: Option<&'a Path>,
    pub game: Option<&'a Path>,
    pub rounds: usize,
    pub params: retarget::Params,
}

/// Closed loop: retarget -> runtime validation -> adjust -> retarget. Keeps
/// the round with the fewest problem frames; writes `<out>` and
/// `<out stem>.validation.json`.
pub fn convert(o: &ConvertOptions) -> Result<serde_json::Value, String> {
    let t0 = std::time::Instant::now();
    let motion = retarget::Motion::load(o.motion)?;
    let rig = rig::Rig::load(o.skeleton)?;
    let model = model::Model::load(o.model)?;
    let mut params = o.params.clone();
    let mut sampler = match o.card {
        Some(card) => Some(validate::Sampler::new(match o.game {
            Some(g) => crate::session::open(g, Some(card), 0)?,
            None => crate::portable::load(card)?,
        })),
        None => None,
    };
    let mut rounds = Vec::new();
    let mut best: Option<(usize, retarget::Output, serde_json::Value)> = None;
    for round in 0..o.rounds.max(1) {
        let out = retarget::retarget(&motion, &rig, o.smplx, &model, &params)?;
        let Some(s) = sampler.as_mut() else {
            best = Some((0, out, serde_json::Value::Null));
            break;
        };
        let frames = s.sample(&out.clip, params.fps as f32, o.game.is_none())?;
        let rep = validate::check(&frames, Some(&out.reference), &model, params.fps, s.session.sex as i32);
        eprintln!("round {round}: {}/{} problem frames {:?}", rep.bad_frames(), rep.frames, rep.by_kind());
        rounds.push(serde_json::json!({"round": round, "bad_frames": rep.bad_frames(), "by_kind": rep.by_kind(),
            "params": format!("{:?}", retarget::Params { extra_lift: None, ..params.clone() })}));
        let bad = rep.bad_frames();
        let done = bad == 0;
        // Adjust for the next round.
        let mut changed = false;
        if !rep.floor_deficit.is_empty() {
            let mut lift = params.extra_lift.clone().unwrap_or_else(|| vec![0.; out.frames]);
            for (f, d) in &rep.floor_deficit { lift[*f] += d + 0.005; }
            params.extra_lift = Some(spread(&lift, 4));
            changed = true;
        }
        if rep.has("slide", "") { params.lock_h = (params.lock_h + 0.02).min(0.1); params.lock_v = (params.lock_v + 0.15).min(0.8); changed = true; }
        if rep.has("jitter", "leg") && params.lock_blend < 9 { params.lock_blend += 3; changed = true; }
        if (rep.has("jitter", "hand") || rep.has("jitter", "forearm") || rep.has("jitter", "head")) && params.smooth < 2.5 { params.smooth += 0.5; changed = true; }
        if rep.has("stretch", "shoulder") && params.collar_scale > 0.1 { params.collar_scale *= 0.5; changed = true; }
        if best.as_ref().is_none_or(|b| bad < b.0) { best = Some((bad, out, rep.to_json())); }
        if done || !changed { break; }
    }
    let (_, out, final_rep) = best.ok_or("no result")?;
    std::fs::write(o.out, &out.clip).map_err(|e| e.to_string())?;
    let summary = serde_json::json!({"frames": out.frames, "fps": params.fps, "duration_seconds": out.duration,
        "repair": out.report, "validation": final_rep, "rounds": rounds, "seconds": t0.elapsed().as_secs_f64()});
    std::fs::write(o.out.with_extension("validation.json"), serde_json::to_vec_pretty(&summary).unwrap()).map_err(|e| e.to_string())?;
    Ok(summary)
}

/// Two-person video: `female` is converted first (closed loop) and anchors
/// the shared frame; the male is placed so the two KK body surfaces keep the
/// source distance (pair_gap, SMPL-X): KK bodies are thinner than real ones,
/// so the source pelvis spacing would leave a gap. Per frame, the male root is
/// shifted along the horizontal female->male hips direction (bisection on the
/// closest vertex distance), smoothed, then the male closed loop runs.
pub fn convert_pair(female: &ConvertOptions, male: &ConvertOptions) -> Result<serde_json::Value, String> {
    let t0 = std::time::Instant::now();
    let fsum = convert(female)?;
    let fr = &fsum["repair"]["frame"];
    let frame = retarget::Frame { yaw: fr["yaw"].as_f64().ok_or("frame")?, x0: fr["x0"].as_f64().ok_or("frame")?, z0: fr["z0"].as_f64().ok_or("frame")?,
        ground: fr["ground"].as_f64().ok_or("frame")?, xz_scale: fr["xz_scale"].as_f64().ok_or("frame")? };
    let fps = female.params.fps;
    let open = |card: &Path, game: Option<&Path>| -> Result<validate::Sampler, String> {
        Ok(validate::Sampler::new(match game { Some(g) => crate::session::open(g, Some(card), 0)?, None => crate::portable::load(card)? }))
    };
    let fcard = female.card.ok_or("pair fit needs the female validation card")?;
    let mcard = male.card.ok_or("pair fit needs a male card")?;
    let fclip = crate::gamefs::read(female.out).map_err(|e| e.to_string())?;
    let fmesh = open(fcard, female.game)?.sample_meshes(&fclip, fps as f32, female.game.is_none(), 4)?;
    // Initial male placement in the shared frame.
    let motion = retarget::Motion::load(male.motion)?;
    let rig = rig::Rig::load(male.skeleton)?;
    let model = model::Model::load(male.model)?;
    let mut prm = male.params.clone();
    prm.frame = Some(frame);
    let first = retarget::retarget(&motion, &rig, male.smplx, &model, &prm)?;
    let mut msampler = open(mcard, male.game)?;
    let mmesh = msampler.sample_meshes(&first.clip, fps as f32, male.game.is_none(), 4)?;
    let n = fmesh.len().min(mmesh.len());
    let gaps = motion.pair_gap.clone().unwrap_or_default();
    let mut shift = vec![0f64; n];
    let mut dirs = vec![[0f64; 2]; n];
    let (mut before, mut after) = (Vec::new(), Vec::new());
    for f in 0..n {
        let (fv, fh) = (&fmesh[f].0, fmesh[f].1);
        let (mv, mh) = (&mmesh[f].0, mmesh[f].1);
        let d = [(mh[0] - fh[0]) as f64, (mh[2] - fh[2]) as f64];
        let len = (d[0] * d[0] + d[1] * d[1]).sqrt();
        let Some(&g) = gaps.get(f) else { continue };
        if len < 1e-3 || g > 0.05 { continue; }  // not in contact in the source: keep its spacing
        let d = [d[0] / len, d[1] / len];
        dirs[f] = d;
        let (fa, ma) = (fv, mv);
        if fa.is_empty() || ma.is_empty() { continue; }
        let dist = |s: f64| -> f64 {
            let (dx, dz) = ((d[0] * s) as f32, (d[1] * s) as f32);
            let mut best = f32::INFINITY;
            for a in fa.iter() { for b in ma.iter() {
                let e = (a[0] - b[0] - dx).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2] - dz).powi(2);
                if e < best { best = e; }
            } }
            best.sqrt() as f64
        };
        // Approach from apart in 1 cm steps and stop at first torso contact:
        // closest-vertex distance alone cannot tell touching from passing through.
        let want = g.max(0.005);
        before.push(dist(0.));
        let mut s = 0.25f64;
        while s > -0.5 && dist(s) > want { s -= 0.01; }
        if s < -0.5 { s = -0.5; }
        shift[f] = s;
        after.push(dist(s));
    }
    let shift = math::smooth_f(&shift, 3.);
    prm.extra_offset = Some((0..n).map(|f| [dirs[f][0] * shift[f], dirs[f][1] * shift[f]]).collect());
    let mut final_opts = ConvertOptions { params: prm, ..*male };
    final_opts.params.frame = Some(frame);
    let msum = convert(&final_opts)?;
    let med = |v: &mut Vec<f64>| { v.sort_by(|a, b| a.total_cmp(b)); v.get(v.len() / 2).copied().unwrap_or(f64::NAN) };
    let mut sorted_shift = shift.clone();
    let fit = serde_json::json!({"contact_frames": before.len(), "gap_before_median_m": med(&mut before), "gap_after_median_m": med(&mut after),
        "shift_median_m": med(&mut sorted_shift)});
    eprintln!("pair fit: {fit}");
    Ok(serde_json::json!({"female": fsum, "male": msum, "pair_fit": fit, "seconds": t0.elapsed().as_secs_f64()}))
}

fn spread(lift: &[f64], radius: usize) -> Vec<f64> {
    let mut out = lift.to_vec();
    for o in 1..=radius {
        let w = 1. - o as f64 / (radius + 1) as f64;
        for f in 0..lift.len() {
            if f >= o { out[f] = out[f].max(lift[f - o] * w); }
            if f + o < lift.len() { out[f] = out[f].max(lift[f + o] * w); }
        }
    }
    out
}

/// Plays an OKCLIP01 resource on a game session without replacing its game
/// root (unlike `ok_session_install_shared_clip`, which portable sessions use).
#[no_mangle]
pub unsafe extern "C" fn ok_session_install_video_clip(s: *mut OkSession, bytes: *const u8, len: i32) -> i32 {
    if s.is_null() || bytes.is_null() || len <= 0 || len > 128 * 1024 * 1024 {
        return 1;
    }
    let s = &mut *s;
    let Ok(key) = crate::anim::install_clip_for_root(&s.game_root, std::slice::from_raw_parts(bytes, len as usize)) else {
        return 1;
    };
    s.anim_clip = key;
    s.anim_parameters.clear();
    s.anim_scene_mode = 0;
    s.anim_time = 0.;
    s.reset_anim_clock();
    s.pv_mask = 15;
    0
}

pub fn rest_skeleton_json(game: &Path, card: Option<&Path>) -> Result<serde_json::Value, String> {
    let mut sess = crate::session::open(game, card, 0)?;
    let export = sess.ensure_skel_export();
    let names: Vec<&str> = export.bones.iter().map(|b| b.name.as_str()).collect();
    let bones: Vec<serde_json::Value> = export
        .bones
        .iter()
        .map(|b| {
            let mut path = vec![b.name.as_str()];
            let mut p = b.parent;
            while p >= 0 {
                path.push(names[p as usize]);
                p = export.bones[p as usize].parent;
            }
            path.reverse();
            serde_json::json!({
                "name": b.name,
                "parent": b.parent,
                "path": path.join("/"),
                "pos": b.pose_pos,
                "rot": b.pose_rot,
                "scl": b.pose_scl,
            })
        })
        .collect();
    Ok(serde_json::json!({"space": "unity_local", "bones": bones}))
}

/// Diagnostic: open a portable card, install `clip`, and print a few bone
/// world rotations over time (row 0 of the world matrix).
pub fn probe(card: &Path, clip: &Path, game: Option<&Path>) -> Result<(), String> {
    let mut s = match game {Some(game)=>crate::session::open(game, Some(card), 0)?, None=>crate::portable::load(card)?};
    let bytes = crate::gamefs::read(clip).map_err(|e| e.to_string())?;
    let status = unsafe { if game.is_some() {ok_session_install_video_clip(&mut s, bytes.as_ptr(), bytes.len() as i32)}
        else {crate::ffi::ok_session_install_shared_clip(&mut s, bytes.as_ptr(), bytes.len() as i32)} };
    eprintln!("install={status} root={} clip={}", s.game_root.display(), s.anim_clip);
    for t in [0.0f32, 1.0, 3.0] {
        s.apply_anim_frame(t, 1.0 / 30.0);
        for bone in ["cf_j_forearm01_L", "cf_j_hand_L", "cf_s_elbo_L", "cf_s_elboback_L", "cf_d_forearm02_L", "cf_j_leg03_L"] {
            let m = crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), bone);
            eprintln!("t={t} {bone} tx={:?}", m.map(|m| [m[0][3], m[1][3], m[2][3]]));
        }
    }
    Ok(())
}

/// Diagnostic: skin the body at T-pose and at clip times, report triangle-edge
/// stretch (posed/rest length) and the joint nearest to the worst edges.
pub fn stretch(card: &Path, clip: &Path, game: Option<&Path>) -> Result<(), String> {
    let mut s = match game {Some(game)=>crate::session::open(game, Some(card), 0)?, None=>crate::portable::load(card)?};
    let bytes = crate::gamefs::read(clip).map_err(|e| e.to_string())?;
    s.skin_all();
    let body: Vec<usize> = s.parts.iter().enumerate().filter(|(_, p)| p.name.contains("o_body")).map(|(i, _)| i).collect();
    let rest: Vec<Vec<[f32; 3]>> = body.iter().map(|&i| s.parts[i].positions.clone()).collect();
    let status = unsafe { if game.is_some() {ok_session_install_video_clip(&mut s, bytes.as_ptr(), bytes.len() as i32)}
        else {crate::ffi::ok_session_install_shared_clip(&mut s, bytes.as_ptr(), bytes.len() as i32)} };
    if status != 0 { return Err("install failed".into()); }
    let joints = ["cf_j_hips","cf_j_spine02","cf_j_neck","cf_j_shoulder_L","cf_j_shoulder_R","cf_j_arm00_L","cf_j_arm00_R","cf_j_forearm01_L","cf_j_forearm01_R",
        "cf_j_hand_L","cf_j_hand_R","cf_j_thigh00_L","cf_j_thigh00_R","cf_j_leg01_L","cf_j_leg01_R","cf_j_leg03_L","cf_j_leg03_R"];
    let dist = |a: [f32; 3], b: [f32; 3]| ((a[0]-b[0]).powi(2)+(a[1]-b[1]).powi(2)+(a[2]-b[2]).powi(2)).sqrt();
    for t in std::env::var("OK_STRETCH_TIMES").ok().map(|v| v.split(',').filter_map(|x| x.parse().ok()).collect::<Vec<f32>>()).unwrap_or(vec![0.5f32, 1.5, 3.0, 5.0, 7.0]) {
        s.apply_anim_frame(t, 1.0 / 30.0);
        s.skin_all();
        let jw: Vec<(&str, [f32; 3])> = joints.iter().filter_map(|j| crate::bone_world(s.body_skel.as_ref(), s.head_skel.as_ref(), j).map(|m| (*j, [m[0][3], m[1][3], m[2][3]]))).collect();
        let mut ratios = Vec::new();
        let mut per_joint: std::collections::BTreeMap<&str, (usize, f32)> = Default::default();
        for (k, &pi) in body.iter().enumerate() {
            let p = &s.parts[pi];
            for tri in p.indices.chunks(3) {
                for (a, b) in [(tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])] {
                    let (a, b) = (a as usize, b as usize);
                    let r0 = dist(rest[k][a], rest[k][b]);
                    if r0 < 1e-4 { continue; }
                    let r = dist(p.positions[a], p.positions[b]) / r0;
                    ratios.push(r);
                    if r > 1.25 {
                        let mid = [(p.positions[a][0]+p.positions[b][0])/2., (p.positions[a][1]+p.positions[b][1])/2., (p.positions[a][2]+p.positions[b][2])/2.];
                        let near = jw.iter().min_by(|x, y| dist(x.1, mid).total_cmp(&dist(y.1, mid))).map(|x| x.0).unwrap_or("?");
                        let e = per_joint.entry(near).or_insert((0, 0.)); e.0 += 1; e.1 = e.1.max(r);
                    }
                }
            }
        }
        ratios.sort_by(|a, b| a.total_cmp(b));
        let q = |f: f32| ratios.get(((ratios.len() as f32 - 1.) * f) as usize).copied().unwrap_or(0.);
        let w = |a: &str| jw.iter().find(|x| x.0 == a).map(|x| x.1).unwrap_or([0.; 3]);
        eprintln!("t={t} edges={} p50={:.3} p99={:.3} p999={:.3} max={:.3} len hips-neck={:.3} arm={:.3} leg={:.3} shoulders={:.3} stretched>1.25 by nearest joint: {:?}", ratios.len(), q(0.5), q(0.99), q(0.999), q(1.0),
            dist(w("cf_j_hips"), w("cf_j_neck")), dist(w("cf_j_arm00_L"), w("cf_j_hand_L")), dist(w("cf_j_thigh00_L"), w("cf_j_leg03_L")), dist(w("cf_j_arm00_L"), w("cf_j_arm00_R")), per_joint);
    }
    Ok(())
}

/// Training data for the helper-bone model: export raw authored clips (OKCLIP01)
/// from the game's Studio/custom-pose sources whose label matches `filter`.
pub fn export_training_clips(game: &Path, out: &Path, filter: &str, limit: usize) -> Result<usize, String> {
    std::fs::create_dir_all(out).map_err(|e| e.to_string())?;
    let mut written = 0;
    let mut sources = crate::anim::clip_sources(game);
    sources.push("custom/custompose.unity3d".into());
    for source in sources.iter().filter(|s| !s.starts_with("states:") && s.contains(filter)) {
        for clip in crate::anim::clip_names_for_source(game, source) {
            if written >= limit { return Ok(written); }
            let key = format!("{source}|{clip}");
            let Ok(bytes) = crate::anim::export_shared_clip(game, &key) else { continue };
            let name: String = key.chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' || c == '.' { c } else { '_' }).collect();
            std::fs::write(out.join(format!("{name}.okanim")), bytes).map_err(|e| e.to_string())?;
            written += 1;
        }
        eprintln!("source {source}: total {written}");
    }
    Ok(written)
}

/// Diagnostic: write the skinned body (o_body*) at clip time `t` as OBJ.
pub fn dump_pose_obj(card: &Path, clip: &Path, t: f32, out: &Path, game: Option<&Path>) -> Result<(), String> {
    let mut s = match game { Some(g) => crate::session::open(g, Some(card), 0)?, None => crate::portable::load(card)? };
    let bytes = crate::gamefs::read(clip).map_err(|e| e.to_string())?;
    let status = unsafe { if game.is_some() { ok_session_install_video_clip(&mut s, bytes.as_ptr(), bytes.len() as i32) }
        else { crate::ffi::ok_session_install_shared_clip(&mut s, bytes.as_ptr(), bytes.len() as i32) } };
    if status != 0 { return Err("install failed".into()); }
    for _ in 0..60 { s.apply_anim_frame(t, 1.0 / 30.0); }
    s.apply_anim_frame(t, 1.0 / 30.0);
    s.skin_all();
    let mut text = String::new();
    let mut base = 1;
    for p in s.parts.iter().filter(|p| p.name.contains("o_body")) {
        for v in &p.positions { text += &format!("v {} {} {}\n", v[0], v[1], v[2]); }
        for tri in p.indices.chunks(3) { text += &format!("f {} {} {}\n", tri[0] as usize + base, tri[1] as usize + base, tri[2] as usize + base); }
        base += p.positions.len();
    }
    std::fs::write(out, text).map_err(|e| e.to_string())
}

/// Validation dump (JSON lines) of any clip, e.g. native clips for baselines.
pub fn validate_dump(card: &Path, clip: &Path, game: Option<&Path>, fps: f32, out: &Path) -> Result<(), String> {
    let s = match game {Some(game)=>crate::session::open(game, Some(card), 0)?, None=>crate::portable::load(card)?};
    let bytes = crate::gamefs::read(clip).map_err(|e| e.to_string())?;
    let frames = validate::Sampler::new(s).sample(&bytes, fps, game.is_none())?;
    std::fs::write(out, validate::frames_to_jsonl(&frames)).map_err(|e| e.to_string())
}

/// Diagnostic: play a native clip (`root` + key, e.g. a portable common
/// controller state) and the same pose rebuilt as a video clip (mapped joints
/// + learned helpers, Studio scene, no PVCopy) on the same card; report the
/// bones and left-arm vertices that differ. `scene_mode` overrides the video
/// session's scene (0: clip's own, 1 Maker, 2 Normal, 4 Studio).
pub fn compare_native(card: &Path, root: &Path, key: &str, t: f32, skeleton: &Path, model: &Path, scene_mode: i32) -> Result<(), String> {
    let rig = rig::Rig::load(skeleton)?;
    let model = model::Model::load(model)?;
    let mut a = crate::portable::load(card)?;
    let (r, k) = (std::ffi::CString::new(root.to_string_lossy().as_ref()).unwrap(), std::ffi::CString::new(key).unwrap());
    if key.ends_with(".okanim") {
        let bytes = crate::gamefs::read(key).map_err(|e| e.to_string())?;
        if unsafe { crate::ffi::ok_session_install_shared_clip(&mut a, bytes.as_ptr(), bytes.len() as i32) } != 0 { return Err("native install failed".into()); }
    } else if unsafe { crate::ffi::ok_select_common_animation(&mut a, r.as_ptr(), k.as_ptr()) } != 0 { return Err("native select failed".into()); }
    for _ in 0..60 { a.apply_anim_frame(t, 1. / 30.); }
    a.skin_all();
    let locals = |s: &OkSession| -> std::collections::HashMap<String, crate::pose::Tf> {
        s.body_skel.as_ref().map(|k| k.tfs.values().map(|tf| (tf.name.clone(), tf.clone())).collect()).unwrap_or_default()
    };
    let la = locals(&a);
    // Rebuild as a video clip: hips position + mapped rotations + helpers.
    let map = rig::mapping();
    let f64q = |q: [f32; 4]| [q[0] as f64, q[1] as f64, q[2] as f64, q[3] as f64];
    let mut binds = vec![(rig.anim_path(rig.idx("cf_j_hips")), 1u32)];
    let hp = la["cf_j_hips"].pos;
    let mut row: Vec<f32> = hp.to_vec();
    let mut mats = std::collections::HashMap::new();
    for (name, _, _) in &map {
        let q = la[*name].rot;
        binds.push((rig.anim_path(rig.idx(name)), 2));
        row.extend(q);
        binds.push((rig.anim_path(rig.idx(name)), 3));
        row.extend([1f32, 1., 1.]);
        mats.insert(name.to_string(), vec![math::from_quat(f64q(q))]);
    }
    for pv in ["cf_pv_hand_L", "cf_pv_hand_R", "cf_pv_leg_L", "cf_pv_leg_R"] {
        let (Some(&i), Some(tf)) = (rig.index.get(pv), la.get(pv)) else { continue };
        for (attr, v) in [(1u32, tf.pos.to_vec()), (2, tf.rot.to_vec()), (3, tf.scale.to_vec())] { binds.push((rig.anim_path(i), attr)); row.extend(v); }
    }
    for (bone, attr, values) in model::predict_helpers(&model, &rig, &mats) {
        let Some(&i) = rig.index.get(&bone) else { continue };
        binds.push((rig.anim_path(i), attr));
        row.extend(values[0].iter().map(|v| *v as f32));
    }
    let mut samples = row.clone();
    samples.extend(row);
    let clip = crate::anim::build_dense_clip(&binds, 2, 30., samples)?;
    let mut b = crate::portable::load(card)?;
    if unsafe { crate::ffi::ok_session_install_shared_clip(&mut b, clip.as_ptr(), clip.len() as i32) } != 0 { return Err("video install failed".into()); }
    if scene_mode != 0 { b.anim_scene_mode = scene_mode; }
    for _ in 0..60 { b.apply_anim_frame(0., 1. / 30.); }
    b.skin_all();
    let lb = locals(&b);
    eprintln!("pv pairs {:?} mask native {} video {} fk {}", a.pv_config.as_ref().map(|c| c.pairs.clone()), a.pv_mask, b.pv_mask, a.pv_fk);
    for n in ["cf_pv_hand_L", "cf_j_hand_L"] { if let (Some(x), Some(y)) = (la.get(n), lb.get(n)) { eprintln!("{n}: native pos {:?} rot {:?} scl {:?} | video pos {:?} scl {:?}", x.pos, x.rot, x.scale, y.pos, y.scale); } }
    eprintln!("corrections native {:?} video {:?}; expression native {} video {}",
        crate::anim::clip_limb_corrections_mode(&a.game_root, &a.anim_clip, a.anim_scene_mode),
        crate::anim::clip_limb_corrections_mode(&b.game_root, &b.anim_clip, b.anim_scene_mode),
        crate::anim::clip_expression_bone_mode(&a.game_root, &a.anim_clip, a.anim_scene_mode),
        crate::anim::clip_expression_bone_mode(&b.game_root, &b.anim_clip, b.anim_scene_mode));
    // Bone differences under the left arm.
    let mut diffs = Vec::new();
    for (name, ta) in &la {
        let Some(tb) = lb.get(name) else { continue };
        let in_arm = rig.index.get(name).is_some_and(|&i| rig.chain(i).contains(&rig.idx("cf_j_shoulder_L")));
        if !in_arm || name.contains("index") || name.contains("middle") || name.contains("ring") || name.contains("little") || name.contains("thumb") { continue; }
        let dq = (ta.rot[0] * tb.rot[0] + ta.rot[1] * tb.rot[1] + ta.rot[2] * tb.rot[2] + ta.rot[3] * tb.rot[3]).abs().min(1.);
        let ang = 2. * dq.acos().to_degrees();
        let dp = ((ta.pos[0] - tb.pos[0]).powi(2) + (ta.pos[1] - tb.pos[1]).powi(2) + (ta.pos[2] - tb.pos[2]).powi(2)).sqrt();
        let ds = (0..3).map(|c| (ta.scale[c] - tb.scale[c]).abs()).fold(0f32, f32::max);
        if ang > 0.5 || dp > 0.002 || ds > 0.01 { diffs.push((ang, dp, ds, name.clone())); }
        if ds > 0.01 { eprintln!("scale {name}: native {:?} video {:?}", ta.scale, tb.scale); }
    }
    diffs.sort_by(|x, y| (y.0 + y.1 * 1000.).total_cmp(&(x.0 + x.1 * 1000.)));
    for (ang, dp, ds, name) in diffs.iter().take(20) { eprintln!("bone {name:24} rot {ang:6.2} deg  pos {:.1} mm  scale {ds:.3}", dp * 1000.); }
    // Vertex differences near the left elbow.
    let elbow = crate::bone_world(a.body_skel.as_ref(), a.head_skel.as_ref(), "cf_j_forearm01_L").map(|m| [m[0][3], m[1][3], m[2][3]]).unwrap_or([0.; 3]);
    let (mut n, mut sum, mut max) = (0, 0f32, 0f32);
    for (pa, pb) in a.parts.iter().zip(&b.parts) {
        if !pa.name.contains("o_body") { continue; }
        for (va, vb) in pa.positions.iter().zip(&pb.positions) {
            let near = ((va[0] - elbow[0]).powi(2) + (va[1] - elbow[1]).powi(2) + (va[2] - elbow[2]).powi(2)).sqrt() < 0.15;
            if !near { continue; }
            let d = ((va[0] - vb[0]).powi(2) + (va[1] - vb[1]).powi(2) + (va[2] - vb[2]).powi(2)).sqrt();
            n += 1; sum += d; max = max.max(d);
        }
    }
    eprintln!("left elbow verts {n}: mean {:.1} mm  max {:.1} mm  (native scene mode {}, video scene mode {})", sum / n.max(1) as f32 * 1000., max * 1000., a.anim_scene_mode, b.anim_scene_mode);
    Ok(())
}
