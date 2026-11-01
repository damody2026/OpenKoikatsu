//! C ABI of the OK animation system (see include/ok_extract.h, "OK animation").
//!
//! Byte results follow the crate convention: call with `out = null, cap = 0` for the size, then again with a buffer.
//! The result of the last call is cached per thread so the second call does not redo the work.

use super::{convert, Contact, OkAnim, Player};
use crate::session::OkSession;
use std::cell::RefCell;
use std::ffi::{c_char, CStr};

thread_local! { static LAST: RefCell<Option<(u64, Vec<u8>)>> = const { RefCell::new(None) }; }

fn hash_args(parts: &[&[u8]]) -> u64 {
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    for p in parts { p.hash(&mut h); }
    h.finish()
}

unsafe fn cached(key: u64, out: *mut u8, cap: i32, make: impl FnOnce() -> Result<Vec<u8>, String>) -> i32 {
    let bytes = LAST.with(|l| l.borrow().as_ref().filter(|(k, _)| *k == key).map(|(_, b)| b.clone()));
    let bytes = match bytes { Some(b) => b, None => match make() {
        Ok(b) => { LAST.with(|l| *l.borrow_mut() = Some((key, b.clone()))); b }
        Err(e) => { set_error(&e); return -1; }
    } };
    crate::ffi::copy_resource(&bytes, out, cap)
}

thread_local! { static ERROR: RefCell<String> = const { RefCell::new(String::new()) }; }
fn set_error(e: &str) { ERROR.with(|x| *x.borrow_mut() = e.to_string()); }

unsafe fn str_arg(p: *const c_char) -> Option<String> { if p.is_null() { None } else { CStr::from_ptr(p).to_str().ok().map(str::to_string) } }
unsafe fn bytes_arg<'a>(p: *const u8, len: i32) -> Option<&'a [u8]> { (!p.is_null() && len > 0).then(|| std::slice::from_raw_parts(p, len as usize)) }

/// Last error message of an OK animation call on this thread.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_last_error(out: *mut c_char, cap: i32) -> i32 {
    let e = ERROR.with(|x| x.borrow().clone());
    if out.is_null() || cap <= 0 { return e.len() as i32 + 1; }
    let n = e.len().min(cap as usize - 1);
    std::ptr::copy_nonoverlapping(e.as_ptr(), out as *mut u8, n);
    *out.add(n) = 0;
    n as i32
}

/// KK clip (`key`, as selected on the session, with its parameters) -> OK animation bytes.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_from_clip(s: *mut OkSession, key: *const c_char, fps: f32, out: *mut u8, cap: i32) -> i32 {
    let (Some(s), Some(key)) = (s.as_mut(), str_arg(key)) else { set_error("invalid arguments"); return -1 };
    // Empty key: the clip the character is playing.
    let key = if key.is_empty() { s.anim_clip.clone() } else { key };
    if key.is_empty() { set_error("no clip selected"); return -1; }
    let k = hash_args(&[b"clip", key.as_bytes(), &fps.to_le_bytes(), &(s as *const OkSession as usize).to_le_bytes()]);
    cached(k, out, cap, || {
        let saved = (s.anim_clip.clone(), s.anim_time);
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| convert::from_session_clip(s, &key, fps)))
            .unwrap_or_else(|_| Err("conversion panicked".into()));
        s.anim_clip = saved.0; s.anim_time = saved.1;
        r?.to_bytes()
    })
}

/// Solves a VMD file into OK animation bytes on `s`'s shaped character skeleton (`s` may be null for standard KK rest).
/// `params_json` is optional JSON (`char_scale`, `obj_scale_y`, `playback_speed`, `delay`, `looping`, `high_heels_rot`, etc.).
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_from_vmd(
    s: *const OkSession,
    vmd_path: *const c_char,
    params_json: *const c_char,
    out: *mut u8,
    cap: i32,
) -> i32 {
    let Some(path) = str_arg(vmd_path).filter(|p| !p.trim().is_empty()) else {
        set_error("invalid vmd_path");
        return -1;
    };
    let pj = str_arg(params_json).unwrap_or_default();
    let sess_opt = s.as_ref();
    let mut shape_sig = Vec::new();
    if let Some(sess) = sess_opt {
        for &v in &sess.shape_body {
            shape_sig.extend_from_slice(&v.to_le_bytes());
        }
        shape_sig.extend_from_slice(&sess.nip_stand_rate.to_le_bytes());
        let shoes_vis = sess
            .parts
            .iter()
            .any(|p| p.role == "clothes_shoes" && p.render_visible);
        shape_sig.push(u8::from(shoes_vis));
        if let Some((name, main, guid)) = sess.active_shoe_meta.as_ref() {
            shape_sig.push(1);
            shape_sig.extend_from_slice(name.as_bytes());
            shape_sig.push(0);
            shape_sig.extend_from_slice(main.as_bytes());
            shape_sig.push(0);
            shape_sig.extend_from_slice(guid.as_bytes());
            shape_sig.push(0);
        } else if let Some(p) = sess.parts.iter().find(|p| p.role == "clothes_shoes") {
            shape_sig.push(2);
            shape_sig.extend_from_slice(p.name.as_bytes());
            shape_sig.push(0);
        } else {
            shape_sig.push(0);
        }
    }
    let k = hash_args(&[
        b"vmd",
        path.as_bytes(),
        pj.as_bytes(),
        &(s as usize).to_le_bytes(),
        &shape_sig,
    ]);
    cached(k, out, cap, || {
        let vmd_bytes = std::fs::read(&path).map_err(|e| format!("{path}: {e}"))?;
        let vmd = crate::vmd::VmdFile::parse(&vmd_bytes)?;
        let ini = crate::mmdd::CharaIniConfig::parse(crate::mmdd::DEFAULT_CHARA_INI)?;
        let (params, obj_scale_y) = crate::mmdd::MotionSolveParams::from_json_with_obj_scale(&pj)?;
        let stem = std::path::Path::new(&path)
            .file_stem()
            .and_then(|st| st.to_str())
            .unwrap_or("vmd");
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let rig = sess_opt.and_then(|sess| crate::mmdd::MmddCharacterRig::from_session(sess, obj_scale_y));
            crate::mmdd::solve_vmd_to_okanim_for_rig(&vmd, &ini, &params, stem, rig.as_ref())
        }))
        .unwrap_or_else(|_| Err("VMD conversion panicked".into()))?;
        r.to_bytes()
    })
}

/// Rest skeleton of a character's Animator layer as a target/source header JSON (UTF-8, not terminated).
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_session_rest(s: *mut OkSession, out: *mut u8, cap: i32) -> i32 {
    let Some(s) = s.as_ref() else { set_error("invalid arguments"); return -1 };
    match convert::session_rest_header(s) { Ok(j) => crate::ffi::copy_resource(j.as_bytes(), out, cap), Err(e) => { set_error(&e); -1 } }
}

/// Records a character's final pose frame by frame (OK Studio timeline -> .okanim): start, one `frame` per sampled
/// pose (after the caller posed the session), `finish` writes the OKANIM bytes (null `out` / 0 `cap`: the size), `free`.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_record_start(s: *mut OkSession) -> *mut convert::PoseRecorder {
    let Some(s) = s.as_ref() else { set_error("invalid arguments"); return std::ptr::null_mut() };
    match convert::PoseRecorder::start(s) { Ok(r) => Box::into_raw(Box::new(r)), Err(e) => { set_error(&e); std::ptr::null_mut() } }
}
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_record_frame(r: *mut convert::PoseRecorder, s: *mut OkSession) -> i32 {
    let (Some(r), Some(s)) = (r.as_mut(), s.as_ref()) else { set_error("invalid arguments"); return -1 };
    r.bytes = None;
    match r.push(s) { Ok(()) => r.frames as i32, Err(e) => { set_error(&e); -1 } }
}
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_record_finish(r: *mut convert::PoseRecorder, fps: f32, looping: i32, name: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let Some(r) = r.as_mut() else { set_error("invalid arguments"); return -1 };
    if r.bytes.is_none() {
        match r.finish(fps, looping != 0, &str_arg(name).unwrap_or_default()).and_then(|a| a.to_bytes()) { Ok(b) => r.bytes = Some(b), Err(e) => { set_error(&e); return -1; } }
    }
    crate::ffi::copy_resource(r.bytes.as_deref().unwrap_or_default(), out, cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_record_free(r: *mut convert::PoseRecorder) {
    if !r.is_null() { drop(Box::from_raw(r)); }
}

/// Bones the character's physics dynamics move (JSON array of names): validation reports them apart.
#[no_mangle]
pub unsafe extern "C" fn ok_session_dynamic_bones(s: *mut OkSession, out: *mut u8, cap: i32) -> i32 {
    let Some(s) = s.as_ref() else { set_error("invalid arguments"); return -1 };
    let j = serde_json::to_vec(&s.body_dynamics.moved_bones()).unwrap_or_default();
    crate::ffi::copy_resource(&j, out, cap)
}

/// Starts converting KK clips of a character on worker threads: items = JSON [{"key","file","source"}] (existing files
/// are skipped). The session is only read here; poll with `ok_okanim_batch_poll`, free with `ok_okanim_batch_free`.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_batch_start(s: *mut OkSession, items: *const c_char, fps: f32, threads: i32) -> *mut super::batch::Batch {
    let (Some(s), Some(j)) = (s.as_mut(), str_arg(items)) else { set_error("invalid arguments"); return std::ptr::null_mut() };
    let items: Vec<super::batch::Item> = match serde_json::from_str(&j) { Ok(v) => v, Err(e) => { set_error(&e.to_string()); return std::ptr::null_mut() } };
    let sampler = match convert::ClipSampler::of_session(s) { Ok(v) => v, Err(e) => { set_error(&e); return std::ptr::null_mut() } };
    Box::into_raw(Box::new(super::batch::Batch::start(sampler, items, if fps > 0.0 { fps } else { 30.0 }, threads.max(1) as usize)))
}

/// Batch progress JSON (entries from index `from`).
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_batch_poll(b: *const super::batch::Batch, from: i32, out: *mut u8, cap: i32) -> i32 {
    let Some(b) = b.as_ref() else { set_error("no batch"); return -1 };
    // Progress moves between the size query and the copy: the query takes a snapshot, the copy returns that snapshot.
    thread_local! { static SNAPSHOT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) }; }
    if out.is_null() { SNAPSHOT.with(|x| *x.borrow_mut() = serde_json::to_vec(&b.poll(from.max(0) as usize)).unwrap_or_default()); }
    SNAPSHOT.with(|x| crate::ffi::copy_resource(&x.borrow(), out, cap))
}

#[no_mangle]
pub unsafe extern "C" fn ok_okanim_batch_cancel(b: *const super::batch::Batch) { if let Some(b) = b.as_ref() { b.cancel(); } }

/// Cancels (if running), waits for the workers and frees the batch.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_batch_free(b: *mut super::batch::Batch) { if !b.is_null() { drop(Box::from_raw(b)); } }

/// A modifier on an animation (see okanim::ops::apply): op = {"op": "trim_rest_start" | "find_loop" | "auto_loop" |
/// "close_loop" | "in_place" | "smooth" | "renormalize" | "foot_contacts", params}; header = a skeleton header JSON
/// (positions for foot_contacts; may be null otherwise) -> the modified animation's bytes.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_modify(bytes: *const u8, len: i32, op: *const c_char, header: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let (Some(b), Some(op)) = (bytes_arg(bytes, len), str_arg(op)) else { set_error("invalid arguments"); return -1 };
    let h = str_arg(header).unwrap_or_default();
    let key = hash_args(&[b, op.as_bytes(), h.as_bytes()]);
    cached(key, out, cap, || {
        let anim = OkAnim::from_bytes(b)?;
        let op: serde_json::Value = serde_json::from_str(&op).map_err(|e| e.to_string())?;
        let header = if h.is_empty() { None } else { Some(target_header(&h).ok_or_else(|| "bad header".to_string())?) };
        super::ops::apply(&anim, &op, header.as_deref())?.to_bytes()
    })
}

/// Quality issues of an animation on a skeleton header (positions): JSON [{kind, bone, start, end, value}] (frames).
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_quality(bytes: *const u8, len: i32, header: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let (Some(b), Some(h)) = (bytes_arg(bytes, len), str_arg(header)) else { set_error("invalid arguments"); return -1 };
    let key = hash_args(&[b, h.as_bytes(), b"quality"]);
    cached(key, out, cap, || {
        let anim = OkAnim::from_bytes(b)?;
        let header = target_header(&h).ok_or_else(|| "bad header".to_string())?;
        serde_json::to_vec(&super::ops::quality(&anim, &header)).map_err(|e| e.to_string())
    })
}

/// Posture adaptation for every player (1 on, 0 off; see solve::posture). Returns the previous setting.
#[no_mangle]
pub extern "C" fn ok_okanim_set_posture(on: i32) -> i32 { super::solve::POSTURE.swap(on != 0, std::sync::atomic::Ordering::Relaxed) as i32 }

/// The walk cycle mixed into the legs of roles the placement moves fast (an OK animation; null clears it).
#[no_mangle]
pub unsafe extern "C" fn ok_player_set_walk(p: *mut Player, bytes: *const u8, len: i32) -> i32 {
    let Some(p) = p.as_mut() else { set_error("no player"); return -1 };
    match bytes_arg(bytes, len) {
        None => { p.walk = None; 0 }
        Some(b) => match OkAnim::from_bytes(b) { Ok(a) => { p.walk = Some(std::sync::Arc::new(a)); 0 } Err(e) => { set_error(&e); -1 } },
    }
}

/// Finishing passes of a group's characters in parallel (1) or one after the other (0). Returns the previous setting.
#[no_mangle]
pub extern "C" fn ok_okanim_set_parallel_finish(on: i32) -> i32 { super::solve::PARALLEL_FINISH.swap(on != 0, std::sync::atomic::Ordering::Relaxed) as i32 }

/// Accumulated OK playback phase timings: JSON {phase: [total_us, count]} since the last reset (reset != 0 resets).
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_profile(reset: i32, out: *mut u8, cap: i32) -> i32 {
    thread_local! { static SNAP: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) }; }
    if out.is_null() { SNAP.with(|x| *x.borrow_mut() = super::prof::snapshot(reset != 0).into_bytes()); }
    SNAP.with(|x| crate::ffi::copy_resource(&x.borrow(), out, cap))
}

/// Absolute locals of any named skeleton (header JSON, see convert::LocalsHeader) -> OK animation bytes.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_from_locals(header: *const c_char, rotations: *const f32, rotation_count: i32,
    hips: *const f32, hips_count: i32, out: *mut u8, cap: i32) -> i32 {
    let Some(header) = str_arg(header) else { set_error("invalid header"); return -1 };
    if rotation_count < 0 || hips_count < 0 || (rotation_count > 0 && rotations.is_null()) || (hips_count > 0 && hips.is_null()) { set_error("invalid arrays"); return -1; }
    let rot = if rotation_count > 0 { std::slice::from_raw_parts(rotations, rotation_count as usize) } else { &[] };
    let hip = if hips_count > 0 { std::slice::from_raw_parts(hips, hips_count as usize) } else { &[] };
    let k = hash_args(&[b"locals", header.as_bytes(), bytemuck_f32(rot), bytemuck_f32(hip)]);
    cached(k, out, cap, || convert::from_locals(&header, rot, hip)?.to_bytes())
}
fn bytemuck_f32(v: &[f32]) -> &[u8] { unsafe { std::slice::from_raw_parts(v.as_ptr() as *const u8, std::mem::size_of_val(v)) } }

/// Information about an OK animation as JSON: fps, frames, duration, looping, scene, family, bones, curves, contacts, meta.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_info(bytes: *const u8, len: i32, out: *mut u8, cap: i32) -> i32 {
    let Some(b) = bytes_arg(bytes, len) else { set_error("no data"); return -1 };
    let k = hash_args(&[b"info", b]);
    cached(k, out, cap, || {
        let a = OkAnim::from_bytes(b)?;
        let v = serde_json::json!({
            "family": a.family, "fps": a.fps, "frames": a.frames, "duration": a.duration(), "looping": a.looping, "scene": a.scene,
            "hips_bone": a.hips_bone, "bones": a.tracks.iter().map(|t| &t.bone).collect::<Vec<_>>(),
            "curves": a.curves.iter().map(|c| &c.name).collect::<Vec<_>>(), "curve_values": a.curves, "contacts": a.contacts, "meta": a.meta,
            "tracks": a.tracks.iter().map(|t| serde_json::json!({"bone": t.bone, "parent": t.parent, "rest_local": t.rest_local, "rest_world": t.rest_world})).collect::<Vec<_>>(),
            "hips_rest": a.hips_rest, "hips_parent_rest_world": a.hips_parent_rest_world,
        });
        Ok(serde_json::to_vec(&v).map_err(|e| e.to_string())?)
    })
}

/// Dense motion of an OK animation: `frames * tracks * 4` rotation deltas (track order of the info), then `frames * 3`
/// normalised hips offsets. Returns the float count.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_get_tracks(bytes: *const u8, len: i32, out: *mut f32, cap: i32) -> i32 {
    let Some(b) = bytes_arg(bytes, len) else { set_error("no data"); return -1 };
    let a = match OkAnim::from_bytes(b) { Ok(a) => a, Err(e) => { set_error(&e); return -1; } };
    let frames = a.frames as usize;
    let need = a.rot.len() * 4 + frames * 3;
    if out.is_null() || cap < need as i32 { return need as i32; }
    let out = std::slice::from_raw_parts_mut(out, need);
    for (i, q) in a.rot.iter().enumerate() { out[i * 4..i * 4 + 4].copy_from_slice(q); }
    let base = a.rot.len() * 4;
    for f in 0..frames {
        let h = a.hips.get(f).copied().unwrap_or([0.0; 3]);
        out[base + f * 3..base + f * 3 + 3].copy_from_slice(&h);
    }
    need as i32
}

/// Replaces the dense motion (layout of ok_okanim_get_tracks; the frame count may change) -> new OK animation bytes.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_set_tracks(bytes: *const u8, len: i32, data: *const f32, count: i32, out: *mut u8, cap: i32) -> i32 {
    let Some(b) = bytes_arg(bytes, len) else { set_error("no data"); return -1 };
    if data.is_null() || count <= 0 { set_error("no motion"); return -1; }
    let d = std::slice::from_raw_parts(data, count as usize);
    let k = hash_args(&[b"tracks", b, std::slice::from_raw_parts(data as *const u8, count as usize * 4)]);
    cached(k, out, cap, || {
        let mut a = OkAnim::from_bytes(b)?;
        // The frame count follows the data (frames x (tracks x 4 + 3)): the motion may be lengthened or shortened.
        let tracks = a.tracks.len();
        let per = tracks * 4 + 3;
        if d.len() % per != 0 || d.len() < per { return Err(format!("motion size {} is not a whole number of frames of {} tracks", d.len(), tracks)); }
        let frames = d.len() / per;
        let n = frames * tracks;
        a.rot = (0..n).map(|i| crate::finalik::qnorm([d[i * 4], d[i * 4 + 1], d[i * 4 + 2], d[i * 4 + 3]])).collect();
        a.hips = (0..frames).map(|f| [d[n * 4 + f * 3], d[n * 4 + f * 3 + 1], d[n * 4 + f * 3 + 2]]).collect();
        // Curves keep their values per frame: held at the end when lengthened, cut when shortened.
        for c in a.curves.iter_mut() {
            let last = c.values.last().copied().unwrap_or(0.0);
            c.values.resize(frames, last);
        }
        a.frames = frames as u32;
        a.to_bytes()
    })
}

/// Edits an OK animation's parameters (JSON with any of: looping, scene, fps, meta, contacts, curves) -> new bytes.
/// Bone data is unchanged.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_edit(bytes: *const u8, len: i32, patch: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let (Some(b), Some(patch)) = (bytes_arg(bytes, len), str_arg(patch)) else { set_error("invalid arguments"); return -1 };
    let k = hash_args(&[b"edit", b, patch.as_bytes()]);
    cached(k, out, cap, || {
        let mut a = OkAnim::from_bytes(b)?;
        let p: serde_json::Value = serde_json::from_str(&patch).map_err(|e| e.to_string())?;
        if let Some(v) = p.get("looping").and_then(|v| v.as_bool()) { a.looping = v; }
        if let Some(v) = p.get("scene").and_then(|v| v.as_i64()) { a.scene = v as i32; }
        if let Some(v) = p.get("fps").and_then(|v| v.as_f64()) { if v > 0.0 { a.fps = v as f32; } }
        if let Some(v) = p.get("meta") { a.meta = serde_json::from_value(v.clone()).map_err(|e| e.to_string())?; }
        if let Some(v) = p.get("contacts") { a.contacts = serde_json::from_value::<Vec<Contact>>(v.clone()).map_err(|e| e.to_string())?; }
        if let Some(v) = p.get("curves") { a.curves = serde_json::from_value(v.clone()).map_err(|e| e.to_string())?; }
        a.to_bytes()
    })
}

/// Samples an OK animation onto a target skeleton's rest (header JSON like ok_okanim_from_locals, `frames`/`fps`
/// ignored): writes `bones * 4` local rotations and the 3 hips local coordinates. Used by the Unreal animation graph
/// node and by "expand to AnimSequence". Returns the number of floats written.
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_sample_onto(bytes: *const u8, len: i32, target: *const c_char, t: f32, out: *mut f32, cap: i32) -> i32 {
    let (Some(b), Some(target)) = (bytes_arg(bytes, len), str_arg(target)) else { set_error("invalid arguments"); return -1 };
    thread_local! { static ANIM: RefCell<Option<(u64, std::sync::Arc<OkAnim>)>> = const { RefCell::new(None) }; }
    let key = hash_args(&[b]);
    let anim = ANIM.with(|c| c.borrow().as_ref().filter(|(k, _)| *k == key).map(|(_, a)| a.clone()));
    let anim = match anim { Some(a) => a, None => match OkAnim::from_bytes(b) {
        Ok(a) => { let a = std::sync::Arc::new(a); ANIM.with(|c| *c.borrow_mut() = Some((key, a.clone()))); a }
        Err(e) => { set_error(&e); return -1; }
    } };
    let Some(header) = target_header(&target) else { return -1 };
    let need = header.bones.len() * 4 + 3;
    if out.is_null() || cap < need as i32 { return need as i32; }
    pose_onto(&anim, &anim.sample(t), &header, std::slice::from_raw_parts_mut(out, need));
    need as i32
}

/// Rest world rotation of every bone of a target header after its alignment to the family (`bones * 4` floats):
/// what a pose sampled onto it is relative to (editors turn on-screen rotations into animation deltas with it).
#[no_mangle]
pub unsafe extern "C" fn ok_okanim_target_rest(target: *const c_char, out: *mut f32, cap: i32) -> i32 {
    let Some(target) = str_arg(target) else { set_error("invalid arguments"); return -1 };
    let Some(header) = target_header(&target) else { return -1 };
    let need = header.bones.len() * 4;
    if out.is_null() || cap < need as i32 { return need as i32; }
    let out = std::slice::from_raw_parts_mut(out, need);
    for (k, b) in header.bones.iter().enumerate() { out[k * 4..k * 4 + 4].copy_from_slice(&b.rest_world); }
    need as i32
}

/// Target skeleton header (parsed and aligned once per skeleton, cached on this thread).
fn target_header(target: &str) -> Option<std::sync::Arc<convert::LocalsHeader>> {
    thread_local! { static TARGET: RefCell<Option<(u64, std::sync::Arc<convert::LocalsHeader>)>> = const { RefCell::new(None) }; }
    let tkey = hash_args(&[target.as_bytes()]);
    if let Some(h) = TARGET.with(|c| c.borrow().as_ref().filter(|(k, _)| *k == tkey).map(|(_, h)| h.clone())) { return Some(h); }
    let mut h: convert::LocalsHeader = match serde_json::from_str(target) { Ok(h) => h, Err(e) => { set_error(&e.to_string()); return None; } };
    if let Some(f) = h.align_to.take() { convert::align_rest(&mut h, &f); }
    let h = std::sync::Arc::new(h);
    TARGET.with(|c| *c.borrow_mut() = Some((tkey, h.clone())));
    Some(h)
}

/// A role pose (in `anim`'s track layout) carried onto a target skeleton: `bones * 4` locals, then the hips offset.
pub(crate) fn pose_onto(anim: &OkAnim, pose: &super::RolePose, header: &convert::LocalsHeader, out: &mut [f32]) {
    let index: std::collections::HashMap<&str, usize> = anim.tracks.iter().enumerate().map(|(i, t)| (t.bone.as_str(), i)).collect();
    for (k, b) in header.bones.iter().enumerate() {
        let local = match index.get(b.name.as_str()) {
            Some(&i) if i < pose.rot.len() => crate::finalik::qnorm(crate::finalik::qmul(b.rest_local, super::carry_delta(pose.rot[i], anim.tracks[i].rest_world, b.rest_world))),
            _ => b.rest_local,
        };
        out[k * 4..k * 4 + 4].copy_from_slice(&local);
    }
    let need = out.len();
    let hips = header.bones.iter().find(|b| b.name == anim.hips_bone);
    let parent_world = hips.and_then(|h| header.bones.iter().find(|b| b.name == h.parent)).map(|b| b.rest_world).unwrap_or([0.0, 0.0, 0.0, 1.0]);
    let len_t = hips.map(|h| crate::finalik::vlen(h.rest_pos)).unwrap_or(0.0);
    let v = super::carry_vector(pose.hips, anim.hips_parent_rest_world, parent_world);
    out[need - 3..].copy_from_slice(&[v[0] * len_t, v[1] * len_t, v[2] * len_t]);
}

/// One role of a player's spec (blend spaces, montages, group roles: no contact solving) at `t` on a target skeleton
/// header, like ok_okanim_sample_onto. For skeletons without an OK character session (Unreal meshes, the editor).
#[no_mangle]
pub unsafe extern "C" fn ok_player_sample_onto(p: *const Player, role: i32, target: *const c_char, t: f32, out: *mut f32, cap: i32) -> i32 {
    let (Some(p), Some(target)) = (p.as_ref(), str_arg(target)) else { set_error("invalid arguments"); return -1 };
    let Some(header) = target_header(&target) else { return -1 };
    let need = header.bones.len() * 4 + 3;
    if out.is_null() || cap < need as i32 { return need as i32; }
    if !t.is_finite() { set_error("invalid time"); return -1; }
    let e = p.eval(t);
    let Some(r) = e.roles.get(role.max(0) as usize) else { set_error("role out of range"); return -1 };
    pose_onto(&r.anim, &r.pose, &header, std::slice::from_raw_parts_mut(out, need));
    need as i32
}

/// KK paired state (both sessions with their pair clips selected, as the paired playback does) -> OK group package:
/// `OKGROUP1`, u32 spec-JSON length, spec JSON, u32 blob count, then (u32 length, OK animation bytes) per role.
/// Contacts are detected from the native paired result (MotionIK applied).
#[no_mangle]
pub unsafe extern "C" fn ok_okgroup_from_pair(female: *mut OkSession, male: *mut OkSession, state: *const c_char, female_motion: *const c_char,
    male_motion: *const c_char, fps: f32, out: *mut u8, cap: i32) -> i32 {
    if female.is_null() || male.is_null() || female == male { set_error("invalid sessions"); return -1; }
    let (Some(state_s), Some(fm), Some(mm)) = (str_arg(state), str_arg(female_motion), str_arg(male_motion)) else { set_error("invalid identity"); return -1 };
    let k = hash_args(&[b"pair", state_s.as_bytes(), fm.as_bytes(), mm.as_bytes(), &fps.to_le_bytes(), &(female as usize).to_le_bytes(), &(male as usize).to_le_bytes()]);
    cached(k, out, cap, || {
        let (f, m) = (&mut *female, &mut *male);
        let (fk, mk) = (f.anim_clip.clone(), m.anim_clip.clone());
        let fa = convert::from_session_clip(f, &fk, fps)?;
        let ma = convert::from_session_clip(m, &mk, fps)?;
        let frames = fa.frames.min(ma.frames) as usize;
        let heights = [convert::hips_height(f.body_shaped.as_ref().ok_or("no shaped body")?), convert::hips_height(m.body_shaped.as_ref().ok_or("no shaped body")?)];
        // Native MotionIK links of this state: limb -> (partner or own) bone, recorded per frame below.
        const LIMBS: [&str; 4] = ["cf_j_hand_L", "cf_j_hand_R", "cf_j_leg03_L", "cf_j_leg03_R"];
        let links = crate::h_playback::motion_links(&*female, &*male, &state_s, &fm, &mm).unwrap_or_default();
        // The family bone a target transform hangs from (targets are helper objects under the animated bones).
        let anchor_of = |sk: &crate::pose::Skeleton, name: &str| -> Option<String> {
            let mut id = sk.tfs.iter().find(|(_, t)| t.name == name).map(|(id, _)| *id)?;
            for _ in 0..64 {
                let t = &sk.tfs[&id];
                if t.name.starts_with("cf_j_") { return Some(t.name.clone()); }
                id = t.parent.filter(|p| sk.tfs.contains_key(p))?;
            }
            None
        };
        let link_targets: Vec<Option<String>> = links.iter().map(|(_, _, tr, target, _)| {
            let sk = if *tr == 0 { (&*female).body_skel.as_ref() } else { (&*male).body_skel.as_ref() };
            sk.and_then(|sk| anchor_of(sk, target))
        }).collect();
        let mut link_tracks: Vec<Vec<crate::finalik::V3>> = vec![Vec::with_capacity(frames); links.len()];
        let mut recorded = Vec::with_capacity(frames);
        let mut err = [0 as c_char; 512];
        let (cs, cfm, cmm) = (std::ffi::CString::new(state_s.clone()).unwrap(), std::ffi::CString::new(fm.clone()).unwrap(), std::ffi::CString::new(mm.clone()).unwrap());
        for fr in 0..frames {
            let t = fr as f32 / fps;
            if crate::ffi::ok_animation_pair_frame(female, male, t, 1.0 / fps, cs.as_ptr(), cfm.as_ptr(), cmm.as_ptr(), err.as_mut_ptr(), err.len() as i32) != 0 {
                return Err(format!("paired frame failed: {}", CStr::from_ptr(err.as_ptr()).to_string_lossy()));
            }
            let (f, m) = (&*female, &*male);
            let bodies = [f.body_skel.as_ref().unwrap(), m.body_skel.as_ref().unwrap()];
            for (k, (role, limb, tr, _, _)) in links.iter().enumerate() {
                let Some(bone) = &link_targets[k] else { continue };
                let world = |sk: &crate::pose::Skeleton, n: &str| sk.tfs.iter().find(|(_, t)| t.name == n).map(|(id, _)| (crate::pose::world_of_id(*id, &sk.tfs), crate::pose::world_rotation_of(*id, &sk.tfs)));
                if let (Some((e, _)), Some((t, r))) = (world(bodies[*role], LIMBS[*limb]), world(bodies[*tr], bone)) {
                    let d = [e[0][3] - t[0][3], e[1][3] - t[1][3], e[2][3] - t[2][3]];
                    link_tracks[k].push(crate::finalik::qrot(crate::finalik::qinv(r), d));
                }
            }
            recorded.push(vec![convert::contact_bones(bodies[0]), convert::contact_bones(bodies[1])]);
        }
        let mut contacts = convert::detect_contacts(&recorded, &heights, fps, 0.03, 0.2);
        // MotionIK links are the authored interaction: they replace what was detected for the same limb.
        for (k, (role, limb, tr, _, weight)) in links.iter().enumerate() {
            let (Some(bone), track) = (&link_targets[k], &link_tracks[k]) else { continue };
            if track.len() < 2 { continue; }
            let h = heights[*tr].max(0.1);
            let track: Vec<crate::finalik::V3> = track.iter().map(|v| [v[0] / h, v[1] / h, v[2] / h]).collect();
            let mut offset = [0.0f32; 3];
            for v in &track { for i in 0..3 { offset[i] += v[i] / track.len() as f32; } }
            contacts.retain(|c| !(c.role as usize == *role && c.effector == LIMBS[*limb] && c.target_role >= 0));
            contacts.push(super::Contact { role: *role as u32, effector: LIMBS[*limb].into(), target_role: *tr as i32, target_bone: bone.clone(), offset,
                start: 0.0, end: (frames - 1) as f32 / fps, blend: 0.0, weight: weight.clamp(0.0, 1.0), priority: 1.5, ik: true, target_moves: 0.5, track });
        }
        let spec = serde_json::json!({
            "type": "group",
            "roles": [ {"name": "female", "spec": {"type": "anim", "anim": 0}}, {"name": "male", "spec": {"type": "anim", "anim": 1}} ],
            "contacts": contacts,
        });
        let spec = serde_json::to_vec(&spec).map_err(|e| e.to_string())?;
        let blobs = [fa.to_bytes()?, ma.to_bytes()?];
        let mut outb = Vec::new();
        outb.extend_from_slice(b"OKGROUP1");
        outb.extend_from_slice(&(spec.len() as u32).to_le_bytes());
        outb.extend_from_slice(&spec);
        outb.extend_from_slice(&(blobs.len() as u32).to_le_bytes());
        for b in &blobs { outb.extend_from_slice(&(b.len() as u32).to_le_bytes()); outb.extend_from_slice(b); }
        Ok(outb)
    })
}

/// Player for a spec (JSON, see okanim::Spec) over animation blobs. Null on error (see ok_okanim_last_error).
#[no_mangle]
pub unsafe extern "C" fn ok_player_create(spec: *const c_char, blobs: *const *const u8, lengths: *const i32, count: i32) -> *mut Player {
    let Some(spec) = str_arg(spec) else { set_error("invalid spec"); return std::ptr::null_mut() };
    if count < 0 || (count > 0 && (blobs.is_null() || lengths.is_null())) { set_error("invalid blobs"); return std::ptr::null_mut(); }
    let mut list = Vec::with_capacity(count as usize);
    for i in 0..count as usize {
        let (p, l) = (*blobs.add(i), *lengths.add(i));
        let Some(b) = bytes_arg(p, l) else { set_error("empty animation blob"); return std::ptr::null_mut() };
        list.push(b);
    }
    match Player::new(&spec, &list) { Ok(p) => Box::into_raw(Box::new(p)), Err(e) => { set_error(&e); std::ptr::null_mut() } }
}
/// Root travel of a role at unwrapped time `t` (hips heights, the role's hips-parent frame; zero unless the role's
/// animation has meta root_motion = "root_motion"). The character moves by the difference between two times.
#[no_mangle]
pub unsafe extern "C" fn ok_player_root_drift(p: *const Player, role: i32, t: f32, out: *mut f32) -> i32 {
    let Some(p) = p.as_ref() else { return -1 };
    if out.is_null() || !t.is_finite() { return -1; }
    let d = p.root_drift(role.max(0) as usize, t);
    std::ptr::copy_nonoverlapping(d.as_ptr(), out, 3);
    0
}
/// Bone aliases for the characters this player poses: JSON object {family bone: character bone}.
#[no_mangle]
pub unsafe extern "C" fn ok_player_set_aliases(p: *mut Player, json: *const c_char) -> i32 {
    let (Some(p), Some(j)) = (p.as_mut(), str_arg(json)) else { set_error("invalid arguments"); return -1 };
    match serde_json::from_str(&j) { Ok(m) => { p.aliases = m; 0 } Err(e) => { set_error(&e.to_string()); -1 } }
}
/// Curve values of a role at `t` (expressions, gaze, mouth...) as a JSON object {name: value}.
#[no_mangle]
pub unsafe extern "C" fn ok_player_curves(p: *const Player, role: i32, t: f32, out: *mut u8, cap: i32) -> i32 {
    let Some(p) = p.as_ref() else { set_error("no player"); return -1 };
    if !t.is_finite() { set_error("invalid time"); return -1; }
    let e = p.eval(t);
    let Some(r) = e.roles.get(role.max(0) as usize) else { return 0 };
    if r.pose.curves.is_empty() { return 0; }
    let map: std::collections::BTreeMap<&str, f32> = r.pose.curves.iter().map(|(n, v)| (n.as_str(), *v)).collect();
    let j = serde_json::to_vec(&map).unwrap_or_default();
    crate::ffi::copy_resource(&j, out, cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_player_free(p: *mut Player) { if !p.is_null() { drop(Box::from_raw(p)); } }
#[no_mangle]
pub unsafe extern "C" fn ok_player_set_param(p: *mut Player, index: i32, value: f32) -> i32 {
    let Some(p) = p.as_mut() else { return -1 };
    if !(0..8).contains(&index) || !value.is_finite() { return -1; }
    if p.params.len() <= index as usize { p.params.resize(index as usize + 1, 0.0); }
    p.params[index as usize] = value; 0
}
#[no_mangle]
pub unsafe extern "C" fn ok_player_duration(p: *const Player) -> f32 { p.as_ref().map(|p| p.duration()).unwrap_or(0.0) }
#[no_mangle]
pub unsafe extern "C" fn ok_player_roles(p: *const Player) -> i32 { p.as_ref().map(|p| p.role_count() as i32).unwrap_or(0) }
/// Plays the frame at `t` onto `count` characters (one per role, in role order). Writes a JSON report (contact
/// errors, offsets, steps) when `report` is given. 0 on success.
#[no_mangle]
pub unsafe extern "C" fn ok_player_apply(p: *mut Player, sessions: *const *mut OkSession, count: i32, t: f32, dt: f32, report: *mut u8, cap: i32) -> i32 {
    let Some(p) = p.as_mut() else { set_error("no player"); return -1 };
    if count <= 0 || sessions.is_null() || !t.is_finite() || !dt.is_finite() { set_error("invalid arguments"); return -1; }
    let ptrs = std::slice::from_raw_parts(sessions, count as usize);
    for (i, a) in ptrs.iter().enumerate() {
        if a.is_null() || ptrs[..i].contains(a) { set_error("null or repeated character"); return -1; }
    }
    let mut refs: Vec<&mut OkSession> = ptrs.iter().map(|s| &mut **s).collect();
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| super::solve::apply(p, &mut refs, t, dt)));
    match r {
        Ok(Ok(rep)) => {
            if !report.is_null() && cap > 0 {
                let j = serde_json::to_vec(&rep).unwrap_or_default();
                let n = j.len().min(cap as usize - 1);
                std::ptr::copy_nonoverlapping(j.as_ptr(), report, n);
                *report.add(n) = 0;
            }
            0
        }
        Ok(Err(e)) => { set_error(&e); -1 }
        Err(_) => { set_error("player panicked"); -1 }
    }
}
