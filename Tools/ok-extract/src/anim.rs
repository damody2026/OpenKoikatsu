//! Sample Unity AnimationClip (CreateThumbnail: custom/custompose.unity3d Fmannequin_00).
//!
//! Generic clip data lives in m_MuscleClip.m_Clip as StreamedClip + DenseClip +
//! ConstantClip. Binding.path is an Avatar.m_TOS hash (not a CRC of the local
//! bone name). AssetStudio AnimationClipBindingConstant.FindBinding maps curve
//! index → GenericBinding by counting 4 curves per quaternion / 3 per pos/scale.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use std::collections::{BTreeSet, HashMap, HashSet};
use std::io::Read;
use std::path::{Component, Path, PathBuf};
use std::sync::{Arc, Mutex, OnceLock};

use unity_asset_binary::asset::parse_serialized_file;
use unity_asset_binary::bundle::load_bundle;
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;

use crate::pose::{euler_deg_to_quat, Skeleton, SkeletonSnapshot};

mod controller;
pub(crate) mod disk_cache;
mod shared;
pub(crate) use shared::{build_dense_clip, export_shared_clip, install_clip_for_root, install_shared_clip, is_shared_root, resource_tracks};
pub(crate) use controller::{layer_start_time_name,layer_start_phase_name};
pub(crate) mod crossfade;
pub(crate) mod yure;
#[cfg(test)] mod graph_tests;
#[cfg(test)] mod clock_tests;
#[cfg(test)] mod motion_time_scale_tests;
#[cfg(test)] mod additive_tests;
#[cfg(test)] mod studio_tests;

#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
struct Binding {
    path: u32,
    attribute: u32,
    type_id: i32,
}

fn as_f32(v: &UnityValue) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn as_u32(v: &UnityValue) -> u32 {
    v.as_i64().unwrap_or(0) as u32
}

fn as_i32(v: &UnityValue) -> i32 {
    v.as_i64().unwrap_or(0) as i32
}

fn curve_dim(b: &Binding) -> usize {
    // ClassIDType.Transform = 4. Missing typeID still treated as transform when
    // attribute is the kBindTransform* enum (1..4).
    let is_tf = b.type_id == 4 || b.type_id == 0 || (1..=4).contains(&b.attribute);
    if is_tf {
        match b.attribute {
            1 | 3 | 4 => 3,
            2 => 4,
            _ => 1,
        }
    } else {
        1
    }
}

fn parse_bindings(obj: &UnityObject) -> Vec<Binding> {
    let mut out = Vec::new();
    let Some(UnityValue::Object(bind)) = obj.class.properties().get("m_ClipBindingConstant") else {
        return out;
    };
    let Some(UnityValue::Array(arr)) = bind.get("genericBindings") else {
        return out;
    };
    for item in arr {
        let UnityValue::Object(o) = item else { continue };
        let type_id = o
            .get("typeID")
            .or_else(|| o.get("classID"))
            .or_else(|| o.get("typeId"))
            .map(as_i32)
            .unwrap_or(0);
        out.push(Binding {
            path: o.get("path").map(as_u32).unwrap_or(0),
            attribute: o.get("attribute").map(as_u32).unwrap_or(0),
            type_id,
        });
    }
    out
}

#[derive(Clone, Copy, serde::Serialize, serde::Deserialize)]
struct StreamedKey {
    time: f32,
    index: i32,
    /// Unity StreamedCurveKey.coeff[0..4]: cubic, quadratic, outSlope, value.
    coeff: [f32; 4],
}

fn parse_streamed_keys(obj: &UnityObject) -> (u32, Vec<StreamedKey>, f32) {
    let mut keys = Vec::new();
    let mut last_t = 0.0f32;
    let Some(sc) = clip_object(obj).and_then(|d| match d {
        UnityValue::Object(o) => o
            .get("m_StreamedClip")
            .or_else(|| o.get("m_streamedClip"))
            .cloned(),
        _ => None,
    }) else {
        return (0, keys, 0.0);
    };
    let UnityValue::Object(sc) = sc else {
        return (0, keys, 0.0);
    };
    let curve_count = sc.get("curveCount").map(as_u32).unwrap_or(0);
    let Some(UnityValue::Array(arr)) = sc.get("data") else {
        return (curve_count, keys, 0.0);
    };
    let bits: Vec<u32> = arr.iter().map(as_u32).collect();
    let mut i = 0usize;
    while i + 2 <= bits.len() {
        let time = f32::from_bits(bits[i]);
        let n = bits[i + 1] as usize;
        i += 2;
        if n > 0 && i + n * 5 > bits.len() {
            break;
        }
        let keep = time.is_finite() && time >= -0.0001;
        for _ in 0..n {
            let index = bits[i] as i32;
            let coeff = [1, 2, 3, 4].map(|k| f32::from_bits(bits[i + k]));
            if keep {
                keys.push(StreamedKey { time, index, coeff });
            }
            i += 5;
        }
        if keep && time > last_t {
            last_t = time;
        }
    }
    (curve_count, keys, last_t)
}

fn parse_streamed_until(obj: &UnityObject, max_t: f32) -> (u32, HashMap<i32, f32>, f32) {
    let (curve_count, keys, last_t) = parse_streamed_keys(obj);
    (curve_count, sample_streamed_keys(&keys, max_t), last_t)
}

fn sample_streamed_keys(keys: &[StreamedKey], t: f32) -> HashMap<i32, f32> {
    let mut previous = HashMap::<i32, StreamedKey>::new();
    let mut next = HashMap::<i32, StreamedKey>::new();
    for &key in keys {
        if key.time <= t {
            previous.insert(key.index, key);
        } else {
            next.entry(key.index).or_insert(key);
        }
    }
    let mut values = HashMap::new();
    for (&index, &prev) in &previous {
        let value = if let Some(&rhs) = next.get(&index) {
            let dt = (t - prev.time).clamp(0.0, (rhs.time - prev.time).max(0.0));
            let [a, b, slope, origin] = prev.coeff;
            if prev.coeff.iter().all(|x| x.is_finite()) {
                // AssetStudio StreamedCurveKey: coeff[2] is outSlope and
                // coeff[3] is value. coeff[0..2] are the cubic polynomial
                // for local seconds after this key. All-zero derivatives
                // represent a stepped key and naturally hold origin.
                ((a * dt + b) * dt + slope) * dt + origin
            } else {
                let alpha = (dt / (rhs.time - prev.time)).clamp(0.0, 1.0);
                origin * (1.0 - alpha) + rhs.coeff[3] * alpha
            }
        } else {
            prev.coeff[3]
        };
        values.insert(index, value);
    }
    for (index, key) in next {
        values.entry(index).or_insert(key.coeff[3]);
    }
    values
}

fn clip_object(obj: &UnityObject) -> Option<&UnityValue> {
    let UnityValue::Object(muscle) = obj.class.properties().get("m_MuscleClip")? else {
        return None;
    };
    let clip = muscle.get("m_Clip")?;
    match clip {
        UnityValue::Object(o) => Some(o.get("data").unwrap_or(clip)),
        _ => Some(clip),
    }
}

#[derive(Default, Clone, serde::Serialize, serde::Deserialize)]
struct DenseSamples {
    count: u32,
    frames: usize,
    rate: f32,
    begin: f32,
    samples: Vec<f32>,
}

fn parse_dense_raw(obj: &UnityObject) -> DenseSamples {
    let Some(UnityValue::Object(d)) = clip_object(obj) else {
        return DenseSamples::default();
    };
    let Some(UnityValue::Object(dc)) = d.get("m_DenseClip") else {
        return DenseSamples::default();
    };
    let count = dc
        .get("m_CurveCount")
        .or_else(|| dc.get("curveCount"))
        .map(as_u32)
        .unwrap_or(0);
    let samples: Vec<f32> = match dc.get("m_SampleArray").or_else(|| dc.get("sampleArray")) {
        Some(UnityValue::Array(a)) => a.iter().map(as_f32).collect(),
        _ => Vec::new(),
    };
    let available = if count > 0 { samples.len() / count as usize } else { 0 };
    let declared = dc.get("m_FrameCount").map(as_u32).unwrap_or(0) as usize;
    DenseSamples {
        count,
        frames: if declared > 0 { declared.min(available) } else { available },
        rate: dc.get("m_SampleRate").map(as_f32).unwrap_or(0.0),
        begin: dc.get("m_BeginTime").map(as_f32).unwrap_or(0.0),
        samples,
    }
}

fn dense_at(dense: &DenseSamples, t: f32, duration: f32) -> Vec<f32> {
    if dense.count == 0 || dense.samples.is_empty() || dense.frames == 0 {
        return Vec::new();
    }
    let frames = dense.frames;
    if frames <= 1 {
        return dense.samples[..dense.count as usize * frames].to_vec();
    }
    // Unity DenseClip stores its own sampling clock. The final dense frame can
    // be padding after m_StopTime; stretching all frames over the streamed
    // duration advances the motion by one frame near the end of the clip.
    let frame = if dense.rate.is_finite() && dense.rate > 0.0 && dense.begin.is_finite() {
        ((t - dense.begin) * dense.rate).clamp(0.0, (frames - 1) as f32)
    } else {
        ((t / duration.max(0.001)).clamp(0.0, 1.0)) * (frames - 1) as f32
    };
    let first = (frame.floor() as usize).min(frames - 1);
    let second = (first + 1).min(frames - 1);
    let blend = frame - first as f32;
    (0..dense.count as usize)
        .map(|i| dense.samples[first * dense.count as usize + i] * (1.0 - blend)
            + dense.samples[second * dense.count as usize + i] * blend)
        .collect()
}

fn parse_dense_at(obj: &UnityObject, t: f32, duration: f32) -> (u32, Vec<f32>) {
    let dense = parse_dense_raw(obj);
    (dense.count, dense_at(&dense, t, duration))
}

fn clip_timing(obj: &UnityObject, streamed_end: f32, dense: &DenseSamples) -> (f32, f32) {
    let muscle = obj.class.properties().get("m_MuscleClip");
    let start = muscle.and_then(|v| uv_get(v, "m_StartTime")).map(as_f32)
        .filter(|v| v.is_finite()).unwrap_or(0.0);
    let recorded_stop = muscle.and_then(|v| uv_get(v, "m_StopTime")).map(as_f32)
        .filter(|v| v.is_finite() && *v > start);
    let dense_end = if dense.frames > 1 && dense.rate.is_finite() && dense.rate > 0.0 {
        dense.begin + (dense.frames - 1) as f32 / dense.rate
    } else { start };
    let stop = recorded_stop.unwrap_or_else(|| {
        if streamed_end > start { streamed_end } else { dense_end }
    });
    (start, (stop - start).max(0.001))
}

fn parse_constant(obj: &UnityObject) -> Vec<f32> {
    let Some(UnityValue::Object(d)) = clip_object(obj) else {
        return Vec::new();
    };
    let Some(cc) = d.get("m_ConstantClip") else {
        return Vec::new();
    };
    let arr = match cc {
        UnityValue::Object(o) => o.get("data"),
        UnityValue::Array(_) => Some(cc),
        _ => None,
    };
    match arr {
        Some(UnityValue::Array(a)) => a.iter().map(as_f32).collect(),
        _ => Vec::new(),
    }
}

fn collect_tos(v: &UnityValue, out: &mut HashMap<u32, String>) {
    match v {
        UnityValue::Array(arr) => {
            if arr.len() == 2 {
                if let Some(path) = arr[1].as_str() {
                    if !path.is_empty() {
                        out.insert(as_u32(&arr[0]), path.to_string());
                    }
                    return;
                }
            }
            for item in arr {
                collect_tos(item, out);
            }
        }
        UnityValue::Object(o) => {
            let hash = o
                .get("first")
                .or_else(|| o.get("Key"))
                .or_else(|| o.get("key"))
                .or_else(|| o.get("0"));
            let path = o
                .get("second")
                .or_else(|| o.get("Value"))
                .or_else(|| o.get("value"))
                .or_else(|| o.get("1"));
            if let (Some(h), Some(p)) = (hash, path) {
                if let Some(ps) = p.as_str() {
                    if !ps.is_empty() {
                        out.insert(as_u32(h), ps.to_string());
                    }
                    return;
                }
            }
            for val in o.values() {
                collect_tos(val, out);
            }
        }
        _ => {}
    }
}

static TOS_CACHE: Mutex<Option<std::sync::Arc<HashMap<u32, String>>>> = Mutex::new(None);

pub fn ingest_avatar_tos(obj: &UnityObject) {
    let Some(tos) = obj.class.properties().get("m_TOS") else {
        return;
    };
    let mut map = HashMap::new();
    collect_tos(tos, &mut map);
    if map.is_empty() {
        return;
    }
    let n = obj.name().unwrap_or_default();
    if let Ok(mut guard) = TOS_CACHE.lock() {
        let slot = std::sync::Arc::make_mut(guard.get_or_insert_with(||std::sync::Arc::new(HashMap::new())));
        if n.contains("cf_body_00") {
            slot.extend(map);
        } else {
            for (k, v) in map {
                slot.entry(k).or_insert(v);
            }
        }
    }
}

fn load_avatar_tos_shared(game_root: &Path) -> std::sync::Arc<HashMap<u32, String>> {
    if let Some(tos)=shared::avatar_tos(game_root) { return tos; }
    // Kept per root: the disk cache has no memory of its own, so every sampled clip (each blend leaf, every frame)
    // read, decompressed and decoded the whole table again (0.9 ms a call on the one-on-one pair), and the new Arc
    // missed the binding and channel plan caches, which match it by pointer, so both plans were rebuilt each time.
    static MEMO: std::sync::Mutex<Option<HashMap<PathBuf, std::sync::Arc<HashMap<u32, String>>>>> = std::sync::Mutex::new(None);
    if let Some(tos)=MEMO.lock().ok().and_then(|m|m.as_ref().and_then(|m|m.get(game_root).cloned())) { return tos; }
    let tos=load_avatar_tos_uncached(game_root);
    if !tos.is_empty() { if let Ok(mut m)=MEMO.lock() { m.get_or_insert_with(HashMap::new).insert(game_root.to_path_buf(), tos.clone()); } }
    tos
}

fn load_avatar_tos_uncached(game_root: &Path) -> std::sync::Arc<HashMap<u32, String>> {
    // Decoded on disk (and in an OK base library root, which has no oo_base bundle).
    if let Some(tos)=disk_cache::load::<HashMap<u32, String>>(game_root, "tos", "avatar") { if !tos.is_empty() { return std::sync::Arc::new(tos); } }
    let tos=load_avatar_tos_bundle(game_root);
    if !tos.is_empty() { disk_cache::store(game_root, "tos", "avatar", &*tos); }
    tos
}

fn load_avatar_tos_bundle(game_root: &Path) -> std::sync::Arc<HashMap<u32, String>> {
    if let Ok(guard) = TOS_CACHE.lock() {
        if let Some(cached) = guard.as_ref() {
            if !cached.is_empty() {
                return cached.clone();
            }
        }
    }
    let mut out = std::sync::Arc::new(HashMap::new());
    let path = game_root.join("abdata/chara/oo_base.unity3d");
    let Ok(bundle) = load_bundle(&path) else {
        return out;
    };
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        for info in &serialized.objects {
            if info.type_id != 90 {
                continue;
            }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else {
                continue;
            };
            ingest_avatar_tos(&obj);
        }
    }
    if let Ok(guard) = TOS_CACHE.lock() {
        if let Some(cached) = guard.as_ref() {
            out = cached.clone();
        }
    }
    out
}

fn load_avatar_tos(game_root: &Path) -> HashMap<u32,String> {(*load_avatar_tos_shared(game_root)).clone()}

fn bone_name(path: &str) -> &str {
    path.rsplit('/').next().unwrap_or(path)
}

/// Unity `Animator.StringToHash` (CRC32 of the name).
pub(crate) fn unity_string_to_hash(s: &str) -> i32 {
    let mut c = 0xFFFF_FFFFu32;
    for b in s.bytes() {
        c ^= b as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 { (c >> 1) ^ 0xEDB8_8320 } else { c >> 1 };
        }
    }
    (c ^ 0xFFFF_FFFF) as i32
}

fn uv_get<'a>(v: &'a UnityValue, k: &str) -> Option<&'a UnityValue> {
    match v {
        UnityValue::Object(o) => o.get(k),
        _ => None,
    }
}

fn unwrap_data<'a>(v: &'a UnityValue) -> &'a UnityValue {
    match v {
        UnityValue::Object(o) => o.get("data").unwrap_or(v),
        _ => v,
    }
}

/// A generic (non-humanoid) AnimationClip at `t` seconds from its start: each binding (path CRC, attribute, class id)
/// with its values (Transform position, scale and Euler 3, rotation 4, anything else 1), and the clip's duration.
pub(crate) fn generic_clip_values(obj: &UnityObject, t: f32) -> (Vec<(u32, u32, i32, Vec<f32>)>, f32) {
    let (binds, values, _, _, _, duration) = parse_clip_obj_at(obj, t);
    let mut c = 0i32;
    let mut out = Vec::with_capacity(binds.len());
    for b in &binds {
        let n = curve_dim(b) as i32;
        out.push((b.path, b.attribute, b.type_id, (0..n).map(|k| curve_at(&values, c + k)).collect()));
        c += n;
    }
    (out, duration)
}

/// The clips an AnimatorController plays on entering (each state machine's m_DefaultState; a blend tree's first leaf),
/// as m_AnimationClips PPtrs.
pub(crate) fn controller_default_clips(obj: &UnityObject) -> Vec<UnityValue> {
    let props = obj.class.properties();
    let (Some(ctrl), Some(UnityValue::Array(clips))) = (props.get("m_Controller"), props.get("m_AnimationClips")) else { return Vec::new() };
    let Some(UnityValue::Array(sms)) = uv_get(ctrl, "m_StateMachineArray") else { return Vec::new() };
    let mut out = Vec::new();
    for sm in sms {
        let sm = unwrap_data(sm);
        let default = uv_get(sm, "m_DefaultState").and_then(|v| v.as_i64()).unwrap_or(0) as usize;
        let Some(UnityValue::Array(states)) = uv_get(sm, "m_StateConstantArray") else { continue };
        let Some(st) = states.get(default).map(unwrap_data) else { continue };
        let Some(UnityValue::Array(bts)) = uv_get(st, "m_BlendTreeConstantArray") else { continue };
        let Some(UnityValue::Array(nodes)) = bts.first().map(unwrap_data).and_then(|bt| uv_get(bt, "m_NodeArray")) else { continue };
        let clip = nodes.iter().map(unwrap_data).find_map(|n| uv_get(n, "m_ClipID").and_then(|v| v.as_i64()).filter(|c| *c >= 0));
        if let Some(c) = clip.and_then(|c| clips.get(c as usize)) { out.push(c.clone()); }
    }
    out
}

/// The clips an AnimatorController settles into once it has run a while: from each state machine's default state,
/// along transitions without conditions that leave on exit time (an empty start state waiting 2.3 s before the live
/// stage's lights come on), to the first state with a clip. m_AnimationClips PPtrs.
pub(crate) fn controller_steady_clips(obj: &UnityObject) -> Vec<UnityValue> {
    let props = obj.class.properties();
    let (Some(ctrl), Some(UnityValue::Array(clips))) = (props.get("m_Controller"), props.get("m_AnimationClips")) else { return Vec::new() };
    let Some(UnityValue::Array(sms)) = uv_get(ctrl, "m_StateMachineArray") else { return Vec::new() };
    let mut out = Vec::new();
    for sm in sms {
        let sm = unwrap_data(sm);
        let Some(UnityValue::Array(states)) = uv_get(sm, "m_StateConstantArray") else { continue };
        let mut state = uv_get(sm, "m_DefaultState").and_then(|v| v.as_i64()).unwrap_or(0) as usize;
        for _ in 0..states.len().max(1) {
            let Some(st) = states.get(state).map(unwrap_data) else { break };
            let clip = match uv_get(st, "m_BlendTreeConstantArray") {
                Some(UnityValue::Array(bts)) => bts.first().map(unwrap_data).and_then(|bt| uv_get(bt, "m_NodeArray")).and_then(|n| match n { UnityValue::Array(a) => Some(a), _ => None })
                    .and_then(|nodes| nodes.iter().map(unwrap_data).find_map(|n| uv_get(n, "m_ClipID").and_then(|v| v.as_i64()).filter(|c| *c >= 0))),
                _ => None,
            };
            if let Some(c) = clip.and_then(|c| clips.get(c as usize)) { out.push(c.clone()); break; }
            let next = match uv_get(st, "m_TransitionConstantArray") {
                Some(UnityValue::Array(ts)) => ts.iter().map(unwrap_data).find(|t| {
                    let unconditional = matches!(uv_get(t, "m_ConditionConstantArray"), Some(UnityValue::Array(c)) if c.is_empty());
                    unconditional && matches!(uv_get(t, "m_HasExitTime"), Some(UnityValue::Bool(true)))
                }).and_then(|t| uv_get(t, "m_DestinationState")).and_then(|v| v.as_i64()),
                _ => None,
            };
            match next { Some(n) if n >= 0 && (n as usize) < states.len() => state = n as usize, _ => break }
        }
    }
    out
}

/// Resolve the `AnimationClip` path_id that the `custom_pose` AnimatorController
/// plays for a state name. There can be multiple clips with the same name
/// (male/female); the controller state (by `m_NameID` CRC32) is authoritative.
fn controller_clip_path_id(ser: &unity_asset_binary::asset::SerializedFile, state: &str) -> Option<i64> {
    let want = unity_string_to_hash(state);
    let pptr = |v: &UnityValue| -> Option<i64> {
        match v {
            UnityValue::Object(o) => o
                .get("m_PathID")
                .or_else(|| o.get("m_PathId"))
                .and_then(|x| x.as_i64()),
            UnityValue::Integer(i) => Some(*i),
            _ => None,
        }
    };
    for info in ser.objects_of_type(91) {
        let Ok(obj) = UnityObject::from_serialized_file(ser, &info) else {
            continue;
        };
        let props = obj.class.properties();
        let Some(ctrl) = props.get("m_Controller") else {
            continue;
        };
        let clips: Vec<i64> = match props.get("m_AnimationClips") {
            Some(UnityValue::Array(a)) => a.iter().filter_map(&pptr).collect(),
            _ => continue,
        };
        let Some(UnityValue::Array(sms)) = uv_get(ctrl, "m_StateMachineArray") else {
            continue;
        };
        for sm in sms {
            let sm = unwrap_data(sm);
            let Some(UnityValue::Array(states)) = uv_get(sm, "m_StateConstantArray") else {
                continue;
            };
            for st in states {
                let st = unwrap_data(st);
                let name_id = uv_get(st, "m_NameID")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(0) as i32;
                if name_id != want {
                    continue;
                }
                // Blend-tree states reference the clip by index into m_AnimationClips.
                if let Some(UnityValue::Array(bts)) = uv_get(st, "m_BlendTreeConstantArray") {
                    if let Some(bt) = bts.first() {
                        let bt = unwrap_data(bt);
                        if let Some(UnityValue::Array(nodes)) = uv_get(bt, "m_NodeArray") {
                            for node in nodes {
                                let node = unwrap_data(node);
                                if let Some(cid) = uv_get(node, "m_ClipID").and_then(|v| v.as_i64())
                                {
                                    if cid >= 0 {
                                        if let Some(pid) = clips.get(cid as usize) {
                                            if *pid != 0 {
                                                return Some(*pid);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if let Some(pid) = uv_get(st, "m_Motion").and_then(pptr) {
                    if pid != 0 {
                        return Some(pid);
                    }
                }
            }
        }
    }
    None
}

fn parse_clip_obj_at(
    obj: &UnityObject,
    t: f32,
) -> (Vec<Binding>, HashMap<i32, f32>, u32, u32, usize, f32) {
    let binds = parse_bindings(obj);
    let dense_meta = parse_dense_raw(obj);
    let (_, _, streamed_end) = parse_streamed_keys(obj);
    let (start, duration) = clip_timing(obj, streamed_end, &dense_meta);
    let source_t = start + t;
    let (streamed_n, mut values, _) = parse_streamed_until(obj, source_t);
    let (dense_n, dense) = parse_dense_at(obj, source_t, duration);
    let constant = parse_constant(obj);
    for (i, v) in dense.iter().enumerate() {
        values.insert(streamed_n as i32 + i as i32, *v);
    }
    let base = streamed_n as i32 + dense_n as i32;
    for (i, v) in constant.iter().enumerate() {
        values.insert(base + i as i32, *v);
    }
    (binds, values, streamed_n, dense_n, constant.len(), duration)
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
struct CachedClip {
    binds: Vec<Binding>,
    keys: Vec<StreamedKey>,
    #[serde(skip)]
    curve_index: Option<Vec<(i32,Vec<usize>)>>,
    streamed_n: u32,
    dense: DenseSamples,
    constant: Vec<f32>,
    reference: Vec<f32>,
    start: f32,
    duration: f32,
    scene: ClipScene,
    state_timing: Option<controller::StateTiming>,
}

/// A decoded clip on disk in two entries: its curves (`clipdata`, the same for every controller and scene profile
/// that plays the clip, so an OK base library stores them once) and what the playing source adds (`cliphead`).
#[derive(serde::Serialize, serde::Deserialize)]
struct ClipHead { scene: ClipScene, state_timing: Option<controller::StateTiming> }
#[derive(serde::Serialize)]
struct ClipDataRef<'a> { binds: &'a Vec<Binding>, keys: &'a Vec<StreamedKey>, streamed_n: u32, dense: &'a DenseSamples, constant: &'a Vec<f32>, reference: &'a Vec<f32>, start: f32, duration: f32 }
#[derive(serde::Deserialize)]
struct ClipData { binds: Vec<Binding>, keys: Vec<StreamedKey>, streamed_n: u32, dense: DenseSamples, constant: Vec<f32>, reference: Vec<f32>, start: f32, duration: f32 }

fn load_disk_clip(root: &Path, key: &str) -> Option<CachedClip> {
    let head = disk_cache::load::<ClipHead>(root, "cliphead", key)?;
    let d = disk_cache::load::<ClipData>(root, "clipdata", key)?;
    Some(CachedClip { curve_index: streamed_curve_index(&d.keys), binds: d.binds, keys: d.keys, streamed_n: d.streamed_n, dense: d.dense,
        constant: d.constant, reference: d.reference, start: d.start, duration: d.duration, scene: head.scene, state_timing: head.state_timing })
}
fn store_disk_clip(root: &Path, key: &str, c: &CachedClip) {
    disk_cache::store(root, "clipdata", key, &ClipDataRef { binds: &c.binds, keys: &c.keys, streamed_n: c.streamed_n, dense: &c.dense,
        constant: &c.constant, reference: &c.reference, start: c.start, duration: c.duration });
    disk_cache::store(root, "cliphead", key, &ClipHead { scene: c.scene, state_timing: c.state_timing.clone() });
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
enum ClipScene {
    Maker,
    Normal,
    AdultOrEsthetic,
    Studio,
}

impl ClipScene {
    fn corrections(self) -> (bool, bool) {
        match self {
            Self::Maker => (true, true),
            Self::Normal | Self::Studio => (false, true),
            Self::AdultOrEsthetic => (false, false),
        }
    }
}

fn scene_for_source(source: &ClipBundleSource) -> ClipScene {
    if let ClipBundleSource::Layers{controller,..}=source {return scene_for_source(controller);}
    if let ClipBundleSource::Studio{base,studio_profile,..}=source {
        return if *studio_profile {ClipScene::Studio} else {scene_for_source(base)};
    }
    let key = source.key().replace('\\', "/").to_ascii_lowercase();
    if key.contains("/custom/custompose.unity3d") {
        ClipScene::Maker
    } else if key.contains("/h/anim/") || key.contains("!h/anim/")
        || key.contains("/esthetic/animator/") || key.contains("!esthetic/animator/") {
        ClipScene::AdultOrEsthetic
    } else if key.contains("/studio_k/anime/") || key.contains("!studio_k/anime/")
        || key.contains("/studio/anime/") || key.contains("!studio/anime/") {
        ClipScene::Studio
    } else {
        ClipScene::Normal
    }
}

fn studio_source_profile(source:&ClipBundleSource)->bool {
    match source {
        ClipBundleSource::Studio{studio_profile,..}=>*studio_profile,
        ClipBundleSource::Layers{controller,..}=>studio_source_profile(controller),
        _=>false,
    }
}

// Own immutable snapshots outside the catalogue lock. Paired actors and
// background preparation must not serialize their curve evaluation here.
static CLIP_CACHE: Mutex<Option<HashMap<String, Arc<CachedClip>>>> = Mutex::new(None);
/// Conversion into an OK base library: drop the decoded clips kept in memory (they are on disk by then).
pub(crate) fn release_decoded_clips(){ if let Ok(mut guard)=CLIP_CACHE.lock() { *guard=None; } }
fn cached_clip(key:&str)->Option<Arc<CachedClip>> {
    CLIP_CACHE.lock().ok()?.as_ref()?.get(key).cloned()
}

// Preserve file order, including duplicate times; malformed unordered inputs
// retain the original scan. This runtime-only index does not change OKCLIP01.
fn streamed_curve_index(keys:&[StreamedKey])->Option<Vec<(i32,Vec<usize>)>> {
    if keys.iter().any(|k|!k.time.is_finite()) || keys.windows(2).any(|k|k[0].time>k[1].time) {return None;}
    let mut curves=std::collections::BTreeMap::<i32,Vec<usize>>::new();
    for (i,key) in keys.iter().enumerate(){curves.entry(key.index).or_default().push(i);}
    Some(curves.into_iter().collect())
}
fn sample_indexed_keys(keys:&[StreamedKey],curves:&[(i32,Vec<usize>)],t:f32)->HashMap<i32,f32> {
    let mut values=HashMap::with_capacity(curves.len());
    for (index,offsets) in curves {
        let split=offsets.partition_point(|&i|keys[i].time<=t);
        let value=if split==0 {keys[offsets[0]].coeff[3]} else {
            let prev=keys[offsets[split-1]];
            if split==offsets.len(){prev.coeff[3]} else {
                let rhs=keys[offsets[split]];
                let dt=(t-prev.time).clamp(0.,(rhs.time-prev.time).max(0.));
                let [a,b,slope,origin]=prev.coeff;
                if prev.coeff.iter().all(|x|x.is_finite()){((a*dt+b)*dt+slope)*dt+origin}
                else {let alpha=(dt/(rhs.time-prev.time)).clamp(0.,1.);origin*(1.-alpha)+rhs.coeff[3]*alpha}
            }
        };
        values.insert(*index,value);
    }
    values
}

fn sample_cached(
    c: &CachedClip,
    t: f32,
) -> (Vec<Binding>, HashMap<i32, f32>, u32, u32, usize, f32) {
    let source_t = c.start + c.state_timing.map(|clock| clock.clip_time(t, c.duration)).unwrap_or(t);
    let mut values = c.curve_index.as_ref().map(|curves|sample_indexed_keys(&c.keys,curves,source_t))
        .unwrap_or_else(||sample_streamed_keys(&c.keys,source_t));
    let dense = dense_at(&c.dense, source_t, c.duration);
    for (i, v) in dense.iter().enumerate() {
        values.insert(c.streamed_n as i32 + i as i32, *v);
    }
    let base = c.streamed_n as i32 + c.dense.count as i32;
    for (i, v) in c.constant.iter().enumerate() {
        values.insert(base + i as i32, *v);
    }
    (
        c.binds.clone(),
        values,
        c.streamed_n,
        c.dense.count,
        c.constant.len(),
        c.state_timing.map(|clock| c.duration / clock.speed).unwrap_or(c.duration),
    )
}

fn clip_bundle_paths(game_root: &Path) -> Vec<std::path::PathBuf> {
    vec![
        game_root.join("abdata/custom/custompose.unity3d"),
        game_root.join("abdata/action/animator/00.unity3d"),
    ]
}

/// Other character animation bundles used by KKS, discovered without opening
/// their large Unity files. The gallery scans clips only after a source is
/// selected. The directories come from the game's motion loaders and Studio.
pub fn clip_sources(game_root: &Path) -> Vec<String> {
    const DIRS: &[&str] = &[
        "studio/anime", "studio_k/anime", "h/anim/female", "h/anim/male",
        "esthetic/animator", "adv/motion/controller/adv",
    ];
    let mut out = Vec::new();
    for dir in DIRS {
        let Ok(entries) = crate::gamefs::read_dir(game_root.join("abdata").join(dir)) else { continue };
        for entry in entries.flatten() {
            let path = entry.path();
            if !path.ok_is_file() || !path.extension().is_some_and(|x| x.eq_ignore_ascii_case("unity3d")) {
                continue;
            }
            if let Some(name) = path.file_name().and_then(|x| x.to_str()) {
                out.push(format!("{dir}/{name}"));
            }
        }
    }
    out.sort_unstable();
    // Explicit controller states are a separate catalogue from raw assets.
    // Lazy scanning is still performed only after selecting a source.
    let mut state_sources = vec!["custom/custompose.unity3d".to_string(),
        "action/animator/00.unity3d".to_string()];
    state_sources.extend(out.iter().cloned());
    out.extend(state_sources.into_iter().map(|s| format!("{}{s}", controller::SOURCE_PREFIX)));
    out
}

#[cfg(test)]
mod source_inventory_tests {
    use super::*;
    #[test]
    fn male_and_female_sources_keep_raw_and_controller_identity() {
        let nonce=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let root=std::env::temp_dir().join(format!("ok_anim_sources_{}_{}",std::process::id(),nonce));
        for role in ["male","female"] {
            let dir=root.join(format!("abdata/h/anim/{role}"));
            std::fs::create_dir_all(&dir).unwrap();
            for name in ["01_01_00.unity3d","07_01_00.UNITY3D","ignored.txt"] {std::fs::write(dir.join(name),b"").unwrap();}
        }
        let sources=clip_sources(&root);
        for role in ["male","female"] {for name in ["01_01_00.unity3d","07_01_00.UNITY3D"] {
            let path=format!("h/anim/{role}/{name}");
            assert_eq!(sources.iter().filter(|s|**s==path).count(),1);
            assert_eq!(sources.iter().filter(|s|**s==format!("states:{path}")).count(),1);
        }}
        assert!(!sources.iter().any(|s|s.contains("ignored")));
        for role in ["male","female"] {
            let dir=root.join(format!("abdata/h/anim/{role}"));
            for name in ["01_01_00.unity3d","07_01_00.UNITY3D","ignored.txt"] {std::fs::remove_file(dir.join(name)).unwrap();}
            std::fs::remove_dir(dir).unwrap();
        }
        for dir in ["abdata/h/anim","abdata/h","abdata",""] {std::fs::remove_dir(root.join(dir)).unwrap();}
    }
}

#[derive(Clone)]
enum ClipBundleSource {
    File(PathBuf),
    Zip(PathBuf, String),
    Studio { base: Box<ClipBundleSource>, owner: String,
        overlay: Option<(Box<ClipBundleSource>, String, String)>, studio_profile:bool },
    Layers {controller:Box<ClipBundleSource>,base_label:String,selected:Vec<String>},
}

impl ClipBundleSource {
    /// Installed resource root (with manifest.json) whose revision covers every file this source reads; None when
    /// the source mixes roots or lives in a folder without a manifest (then nothing is cached on disk).
    fn resource_root(&self)->Option<PathBuf> {
        match self {
            Self::File(path)|Self::Zip(path,_)=>disk_cache::root_of_file(path),
            Self::Studio{base,overlay,..}=>{
                let root=base.resource_root()?;
                match overlay {Some((source,_,_))=>(source.resource_root()?==root).then_some(root),None=>Some(root)}
            }
            Self::Layers{controller,..}=>controller.resource_root(),
        }
    }
    fn payload_key(&self)->String {
        // Controller identity affects the graph, but load() reads the same
        // immutable base payload. Share only file bytes across these owners.
        match self {
            Self::Studio{base,..}=>base.payload_key(),
            Self::Layers{controller,..}=>controller.payload_key(),
            _=>self.key(),
        }
    }
    /// Identity of cache entries on disk: `key` with files relative to their resource root (lowercase, '/'), so an OK
    /// base library converted from a root answers for it wherever either lives.
    fn disk_key(&self) -> String {
        match self {
            Self::File(path) => disk_cache::root_of_file(path)
                .and_then(|root| path.strip_prefix(&root).ok().map(|r| format!("root:{}", r.to_string_lossy().replace('\\', "/").to_ascii_lowercase())))
                .unwrap_or_else(|| self.key()),
            Self::Zip(..) => self.key(),
            Self::Studio{base,owner,overlay,studio_profile} => format!("{}:{}",if *studio_profile {"studio"} else {"controller"},serde_json::to_string(&(base.disk_key(),owner,overlay.as_ref().map(|(s,n,_)|(s.disk_key(),n)))).unwrap()),
            Self::Layers{controller,base_label,selected}=>format!("layers:{}",serde_json::to_string(&(controller.disk_key(),base_label,selected)).unwrap()),
        }
    }
    fn payload_disk_key(&self) -> String {
        match self {
            Self::Studio{base,..}=>base.payload_disk_key(),
            Self::Layers{controller,..}=>controller.payload_disk_key(),
            _=>self.disk_key(),
        }
    }
    fn key(&self) -> String {
        match self {
            Self::File(path) => path.to_string_lossy().into_owned(),
            Self::Zip(path, inner) => format!("{}!{inner}", path.display()),
            Self::Studio{base,owner,overlay,studio_profile} => format!("{}:{}",if *studio_profile {"studio"} else {"controller"},serde_json::to_string(&(base.key(),owner,overlay.as_ref().map(|(s,n,_)|(s.key(),n)))).unwrap()),
            Self::Layers{controller,base_label,selected}=>format!("layers:{}",serde_json::to_string(&(controller.key(),base_label,selected)).unwrap()),
        }
    }

    fn load(&self) -> Option<unity_asset_binary::bundle::AssetBundle> {
        match self {
            Self::File(path) => load_bundle(path).ok(),
            Self::Zip(path, inner) => {
                let bytes = exact_zip_bundle(path, inner)?;
                unity_asset_binary::bundle::load_bundle_from_memory(bytes).ok()
            }
            Self::Studio{base,..} => base.load(),
            Self::Layers{controller,..}=>controller.load(),
        }
    }
}

fn exact_zip_bundle(path: &Path, inner: &str) -> Option<Vec<u8>> {
    let mut archive = crate::gamefs::open_zip(path)?;
    let wanted = inner.to_ascii_lowercase();
    let suffix = format!("/{wanted}");
    let mut match_index = None;
    for index in 0..archive.len() {
        let Ok(entry) = archive.by_index(index) else { continue; };
        let name = entry.name().replace('\\', "/").to_ascii_lowercase();
        if name == wanted || name.ends_with(&suffix) {
            if match_index.is_some() { return None; }
            match_index = Some(index);
        }
    }
    let mut bytes = Vec::new();
    archive.by_index(match_index?).ok()?.read_to_end(&mut bytes).ok()?;
    (bytes.len() > 16).then_some(bytes)
}

fn safe_relative(source: &str, extension: &str) -> bool {
    let path = Path::new(source);
    !source.is_empty() && !source.contains('\\') && path.extension().is_some_and(|x| x.eq_ignore_ascii_case(extension))
        && path.components().all(|c| matches!(c, Component::Normal(_)))
}

// Loaded clips/controllers already have immutable source identity. Do not open
// Windows filesystem handles to canonicalize that same descriptor each frame,
// especially while another thread decompresses its bundle. Never cache failures.
thread_local! {static SOURCE_PATH_CACHE:std::cell::RefCell<Vec<(std::path::PathBuf,String,ClipBundleSource)>>=const {std::cell::RefCell::new(Vec::new())};}
fn source_path(game_root: &Path, source: &str) -> Option<ClipBundleSource> {
    let hit=SOURCE_PATH_CACHE.with(|cache|{let mut entries=cache.borrow_mut();let index=entries.iter().position(|(root,name,_)|root==game_root&&name==source)?;let entry=entries.remove(index);let result=entry.2.clone();entries.push(entry);Some(result)});
    if hit.is_some(){return hit;}
    let resolved=source_path_uncached(game_root,source)?;
    SOURCE_PATH_CACHE.with(|cache|{let mut entries=cache.borrow_mut();if entries.len()>=128{entries.remove(0);}entries.push((game_root.to_path_buf(),source.to_owned(),resolved.clone()));});
    Some(resolved)
}
fn source_path_uncached(game_root: &Path, source: &str) -> Option<ClipBundleSource> {
    if let Some(hex)=source.strip_prefix("layers@") {
        if hex.len()%2!=0||hex.len()>131072 {return None;}
        let bytes=(0..hex.len()).step_by(2).map(|i|u8::from_str_radix(hex.get(i..i+2)?,16).ok()).collect::<Option<Vec<_>>>()?;
        let (source,base_label,selected):(String,String,Vec<String>)=serde_json::from_slice(&bytes).ok()?;
        if base_label.is_empty()||selected.is_empty()||selected.len()>32||selected.iter().any(String::is_empty)
            ||!(source.starts_with("controller@")||source.starts_with("studio@")) {return None;}
        return Some(ClipBundleSource::Layers{controller:Box::new(source_path(game_root,&source)?),base_label,selected});
    }
    if let Some((hex,studio_profile))=source.strip_prefix("studio@").map(|h|(h,true))
        .or_else(||source.strip_prefix("controller@").map(|h|(h,false))) {
        if hex.len()%2!=0 || hex.len()>65536 {return None;}
        let bytes=(0..hex.len()).step_by(2).map(|i|u8::from_str_radix(hex.get(i..i+2)?,16).ok()).collect::<Option<Vec<_>>>()?;
        let [base,owner,over,over_owner]:[String;4]=serde_json::from_slice(&bytes).ok()?;
        let nested=|s:&str|s.starts_with("studio@")||s.starts_with("controller@")||s.starts_with("layers@");
        if owner.is_empty() || nested(&base) || nested(&over) || over.is_empty()!=over_owner.is_empty() {return None;}
        let overlay=if over.is_empty() {None} else {Some((Box::new(source_path(game_root,&over)?),over_owner,over))};
        return Some(ClipBundleSource::Studio{base:Box::new(source_path(game_root,&base)?),owner,overlay,studio_profile});
    }
    if let Some((zip_source, inner)) = source.split_once('!') {
        if !zip_source.starts_with("mods/") || !safe_relative(zip_source, "zipmod")
            || !safe_relative(inner, "unity3d") { return None; }
        let mods_root = game_root.join("mods").canonicalize().ok()?;
        let zip = game_root.join(zip_source).canonicalize().ok()?;
        return zip.starts_with(&mods_root).then(|| ClipBundleSource::Zip(zip, inner.to_string()));
    }
    let source = source.strip_prefix("abdata/").unwrap_or(source);
    if !safe_relative(source, "unity3d") { return None; }
    // An OK base library root has no bundles: its decoded entries answer for the path it was converted from.
    if disk_cache::is_library(game_root) { return Some(ClipBundleSource::File(game_root.join("abdata").join(source))); }
    let abdata = game_root.join("abdata").canonicalize().ok()?;
    let path = abdata.join(source).canonicalize().ok()?;
    path.starts_with(&abdata).then_some(ClipBundleSource::File(path))
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
struct ClipEntry {
    name: String,
    label: String,
    asset_file: String,
    path_id: i64,
    owners: Vec<String>,
    // A runtime Studio override keeps the base graph but may use a clip from
    // another bundle. Preserve that exact source with the leaf's PPtr identity.
    source: Option<String>,
    additive_reference_valid: bool,
}

fn reference_array_is_valid(value:Option<&UnityValue>,curve_count:usize)->bool {
    match value {
        None=>true,
        Some(UnityValue::Array(values))=>values.is_empty()||values.len()==curve_count&&values.iter().all(|v|v.as_f64().is_some_and(|n|n.is_finite()&&(n as f32).is_finite())),
        _=>false,
    }
}

fn local_clip_pointer(value: &UnityValue) -> Option<i64> {
    let UnityValue::Object(pointer) = value else { return None; };
    if pointer.get("m_FileID").and_then(|v| v.as_i64()).unwrap_or(0) != 0 { return None; }
    pointer.get("m_PathID").or_else(|| pointer.get("m_PathId"))
        .and_then(|v| v.as_i64()).filter(|id| *id != 0)
}

fn label_clip_entries(entries: &mut [ClipEntry]) {
    entries.sort_by(|a, b| a.name.cmp(&b.name)
        .then(a.asset_file.cmp(&b.asset_file)).then(a.path_id.cmp(&b.path_id)));
    let mut counts = HashMap::<String, usize>::new();
    for entry in entries.iter() { *counts.entry(entry.name.clone()).or_default() += 1; }
    // Reserve raw names as well, so a qualifier cannot shadow an authored name.
    let mut used: HashSet<String> = counts.keys().cloned().collect();
    for entry in entries.iter_mut() {
        entry.label = entry.name.clone();
        if counts[&entry.name] == 1 { continue; }
        let owner = if entry.owners.is_empty() { "版本".to_string() }
            else { entry.owners.join(", ") };
        let base = format!("{} [{}]", entry.name, owner);
        let mut label = base.clone();
        let mut variant = 1;
        while !used.insert(label.clone()) {
            variant += 1;
            label = format!("{base} ({variant})");
        }
        entry.label = label;
    }
    entries.sort_by(|a, b| a.label.cmp(&b.label));
}

fn entries_in_bundle(source: &ClipBundleSource) -> Vec<ClipEntry> {
    let Some(files)=preparation_files(source,None) else{return Vec::new();};
    let mut entries = Vec::new();
    for (file_name,serialized) in files.iter() {
        // Only additive layers need reference-pose validation. Enumerating
        // ordinary clip names must not decode every curve in a large bundle.
        let additive_ids=controller::additive_clip_ids(&serialized);
        let mut owners = HashMap::<i64, BTreeSet<String>>::new();
        for info in &serialized.objects {
            if info.type_id != 91 && info.type_id != 221 { continue; }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue; };
            let name = obj.name().unwrap_or_default();
            if name.is_empty() { continue; }
            let properties = obj.class.properties();
            if let Some(UnityValue::Array(clips)) = properties.get("m_AnimationClips") {
                for clip in clips {
                    if let Some(id) = local_clip_pointer(clip) {
                        owners.entry(id).or_default().insert(name.clone());
                    }
                }
            }
            if let Some(UnityValue::Array(clips)) = properties.get("m_Clips") {
                for clip in clips {
                    if let Some(id) = uv_get(clip, "m_OverrideClip").and_then(local_clip_pointer) {
                        owners.entry(id).or_default().insert(name.clone());
                    }
                }
            }
        }
        for info in &serialized.objects {
            if info.type_id != 74 { continue; }
            let name=unity_asset_binary::object::ObjectHandle::new(&serialized,info).peek_name().ok().flatten()
                .or_else(||UnityObject::from_serialized_file(&serialized,info).ok().map(|obj|obj.name().unwrap_or_default()));
            if let Some(name) = name {
                if !name.is_empty() {
                    let additive_reference_valid=!additive_ids.contains(&info.path_id)||clip_reference_is_valid(&serialized,info.path_id);
                    entries.push(ClipEntry { name, label: String::new(),
                        asset_file: file_name.clone(), path_id: info.path_id,
                        owners: owners.remove(&info.path_id).unwrap_or_default().into_iter().collect(),source:None,additive_reference_valid });
                }
            }
        }
    }
    label_clip_entries(&mut entries);
    entries
}

fn clip_reference_is_valid(serialized:&unity_asset_binary::asset::SerializedFile,id:i64)->bool {
    let Some(info)=serialized.find_object(id) else{return false;};
    let Ok(obj)=UnityObject::from_serialized_file(serialized,info) else{return false;};
    let reference=obj.class.properties().get("m_MuscleClip").and_then(|m|uv_get(m,"m_ValueArrayReferencePose"));
    reference_array_is_valid(reference,parse_bindings(&obj).iter().map(curve_dim).sum())
}

static CLIP_CATALOGUES: OnceLock<Mutex<HashMap<String, Vec<ClipEntry>>>> = OnceLock::new();

fn cached_bundle_entries(source: &ClipBundleSource) -> Vec<ClipEntry> {
    // The raw clip catalogue scans the immutable base payload, including all
    // controller owners. Controller/layer selection is applied by the graph
    // cache, so rebuilding this same table for each owner is redundant.
    let key = source.payload_key();
    let catalogues = CLIP_CATALOGUES.get_or_init(|| Mutex::new(HashMap::new()));
    if let Ok(guard) = catalogues.lock() {
        if let Some(entries) = guard.get(&key) { return entries.clone(); }
    }
    let root = source.resource_root();
    let disk = source.payload_disk_key();
    let entries = match root.as_deref().and_then(|root| disk_cache::load::<Vec<ClipEntry>>(root, "bundle", &disk)) {
        Some(entries) => entries,
        None => {
            let entries = entries_in_bundle(source);
            if let Some(root) = root.as_deref() { if !entries.is_empty() { disk_cache::store(root, "bundle", &disk, &entries); } }
            entries
        }
    };
    if let Ok(mut guard) = catalogues.lock() {
        guard.insert(key, entries.clone());
    }
    entries
}

// Cache only immutable labels, never actor/session state. Include the resolved
// descriptor and catalogue kind: raw clips and controller states can share a file.
static SOURCE_LABELS: OnceLock<Mutex<HashMap<String, Arc<Vec<String>>>>> = OnceLock::new();
fn source_labels(game_root: &Path, source: &str) -> Arc<Vec<String>> {
    let _scope=AnimationPreparationScope::new();
    let (states, descriptor) = source.strip_prefix(controller::SOURCE_PREFIX)
        .map(|s| (true, s)).unwrap_or((false, source));
    // Resolve the filesystem only on a miss. FFI indexed enumeration asks twice
    // per name; even canonicalizing the same path thousands of times is costly.
    let key = serde_json::to_string(&(game_root.to_string_lossy(), states, descriptor)).unwrap();
    let cache = SOURCE_LABELS.get_or_init(|| Mutex::new(HashMap::new()));
    if let Ok(guard) = cache.lock() {
        if let Some(labels) = guard.get(&key) { return labels.clone(); }
    }
    let Some(resolved) = source_path(game_root, descriptor) else { return Arc::new(Vec::new()); };
    // Do not hold the labels lock while resolving controller graphs recursively.
    let labels: Arc<Vec<String>> = Arc::new(if states {
        controller::entries(&resolved).iter().filter(|e| public_state_entry(e))
            .map(|e| e.label.clone()).collect()
    } else {
        cached_bundle_entries(&resolved).into_iter().map(|e| e.label).collect()
    });
    if let Ok(mut guard) = cache.lock() {
        return guard.entry(key).or_insert_with(|| labels.clone()).clone();
    }
    labels
}
pub fn clip_names_for_source(game_root: &Path, source: &str) -> Vec<String> {
    source_labels(game_root, source).as_ref().clone()
}

pub fn clip_count_for_source(game_root: &Path, source: &str) -> usize {
    source_labels(game_root, source).len()
}

// Warm every reachable blend-tree leaf, not just the default height/parameter
// sample. Otherwise another character shape can still parse a new clip on the
// game thread when the prepared controller is committed.
pub fn prepare_animation_clip(game:&Path,key:&str)->bool {
    // Reuse decoded bundle files across every leaf in this blend tree.
    // The bounded, thread-local cache is released after preparation.
    let _scope=AnimationPreparationScope::new();
    if !selectable_clip(game,key) {return false;}
    // Binding evaluation also needs Avatar TOS. A disk-restored session may
    // never have parsed oo_base; prepare this immutable metadata off-thread.
    let _=load_avatar_tos(game);
    if key.split_once('|').is_some_and(|(s,_)|s.starts_with(controller::SOURCE_PREFIX)) {
        let Some(leaves)=state_leaf_clips(game,key) else {return false;};
        for raw in leaves {if load_clip_at(game,&raw,0.).is_none(){return false;}}
    }
    let _=animation_float_parameters(game,key);let duration=clip_duration(game,key);
    duration.is_finite()&&duration>0.
}

/// The raw clips (`source|clip`) a controller state key plays: its motion's leaves (blend-tree children), its
/// synchronized layers' and, for a layer state, its base state's. None: not a state of the source.
pub(crate) fn state_leaf_clips(game:&Path,key:&str)->Option<HashSet<String>> {
    let (source,label)=key.split_once('|').and_then(|(s,l)|s.strip_prefix(controller::SOURCE_PREFIX).map(|s|(s,l)))?;
    let resolved=source_path(game,source)?;let entries=controller::entries(&resolved);
    let entry=entries.iter().find(|e|e.label==label)?;
    fn motion(m:&controller::MotionNode,source:&str,out:&mut HashSet<String>){match m {
        controller::MotionNode::Clip{clip,..}=>{out.insert(format!("{}|{}",clip.source.as_deref().unwrap_or(source),clip.label));},
        controller::MotionNode::OneD{children,..}|controller::MotionNode::Cartesian{children,..}=>for child in children {motion(child,source,out);}
    }}
    fn collect(e:&controller::StateEntry,source:&str,out:&mut HashSet<String>){
        if let Some(m)=&e.motion {motion(m,source,out);}else{out.insert(format!("{}|{}",e.clip.source.as_deref().unwrap_or(source),e.clip.label));}
        for child in &e.active_layers {collect(child,source,out);}
    }
    let mut keys=HashSet::new();collect(entry,source,&mut keys);
    if let Some(layer)=&entry.layer {let base=entries.iter().find(|e|e.label==layer.base_label)?;collect(base,source,&mut keys);}
    Some(keys)
}

/// Conversion into an OK base library: the raw clips a key plays (itself when it is not a controller state).
pub(crate) fn leaf_clips(game:&Path,key:&str)->Vec<String> {
    if let Some((source,label))=key.split_once('|').and_then(|(s,l)|s.strip_prefix(controller::SOURCE_PREFIX).map(|s|(s,l))) {
        let mut v:Vec<String>=state_leaf_clips(game,key).unwrap_or_default().into_iter().collect();
        // A single-clip state is also played under its own key (`load_clip_at` with the state's timing).
        let single=source_path(game,source).is_some_and(|resolved|controller::entries(&resolved).iter().find(|e|e.label==label).is_some_and(|e|
            e.active_layers.is_empty()&&e.layer.is_none()&&!matches!(e.motion,Some(controller::MotionNode::OneD{..}|controller::MotionNode::Cartesian{..}))));
        if single {v.push(key.to_owned());}
        v.sort();v
    } else {vec![key.to_owned()]}
}
/// Conversion into an OK base library: decodes (and stores) one raw clip and the avatar TOS its bindings need.
pub(crate) fn prepare_leaf_clip(game:&Path,raw:&str)->bool {
    let _scope=AnimationPreparationScope::new();
    let _=load_avatar_tos(game);
    load_clip_at(game,raw,0.).is_some()
}

pub fn clip_name_for_source_at(game_root: &Path, source: &str, index: usize) -> Option<String> {
    source_labels(game_root, source).get(index).cloned()
}

fn public_state_entry(entry:&controller::StateEntry)->bool {
    // A synchronized state cannot be an independent layer over authored Idle.
    // Expose it through the composed Studio graph, where the requested base
    // state and source motion set share the same state-machine clock.
    entry.studio_layer.is_some() || !entry.layer.as_ref().is_some_and(|l|l.synchronized)
}

/// HSceneProc enables param.lstIdLayer[0], after resetting the previous one.
/// Compose exactly that layer with the common base state and Animator clock.
pub(crate) fn native_pair_clip(game:&Path,key:&str,layer:i32)->Option<String>{
    if layer<=0{return selectable_clip(game,key).then(||key.to_owned());}
    let (source,label)=key.split_once('|')?;let descriptor=source.strip_prefix("states:")?;let resolved=source_path(game,descriptor)?;let graph=controller::entries(&resolved);
    let base=graph.iter().find(|e|e.label==label&&e.layer.is_none())?;
    let matches:Vec<_>=graph.iter().filter(|e|e.layer.as_ref().is_some_and(|l|l.index==layer as usize&&l.base_label==base.label)).collect();
    if matches.len()!=1{return None;}
    let json=serde_json::to_vec(&(descriptor,label,vec![matches[0].label.clone()])).ok()?;let hex:String=json.iter().map(|v|format!("{v:02X}")).collect();let source=format!("states:layers@{hex}");
    let names=clip_names_for_source(game,&source);if names.len()!=1{return None;}Some(format!("{source}|{}",names[0]))
}

pub fn selectable_clip(game_root: &Path, name: &str) -> bool {
    if let Some((source, clip)) = name.split_once('|') {
        source_labels(game_root, source).iter().any(|n| n == clip)
    } else {
        clip_names(game_root).iter().any(|n| n == name)
    }
}

/// Names of clips that the pose loader can actually resolve. Cache the catalogue
/// per game root: the animator bundle is large and the gallery may be reopened.
pub fn clip_names(game_root: &Path) -> Vec<String> {
    let mut names = Vec::new();
    for path in clip_bundle_paths(game_root) {
        // Preserve the default Maker/controller state aliases. Explicit source
        // catalogues below resolve exact clip assets, never a name guess.
        names.extend(cached_bundle_entries(&ClipBundleSource::File(path))
            .into_iter().map(|entry| entry.name));
    }
    names.sort_unstable();
    names.dedup();
    names
}

fn load_clip_at(
    game_root: &Path,
    want: &str,
    t: f32,
) -> Option<(Vec<Binding>, HashMap<i32, f32>, u32, u32, usize, f32)> {
    let cache_key = format!("{}\0{}", game_root.display(), want);
    if let Some(c)=cached_clip(&cache_key) {return Some(sample_cached(&c,t));}
    // Decoded clip from an earlier session: no bundle extraction or curve decoding.
    if let Some(c)=load_disk_clip(game_root,want) {
        let c=Arc::new(c);let sampled=sample_cached(&c,t);
        if let Ok(mut guard)=CLIP_CACHE.lock() {guard.get_or_insert_with(HashMap::new).insert(cache_key,c);}
        return Some(sampled);
    }
    let disk_key=want.to_owned();
    let (paths, want, exact, timing) = if let Some((source, clip)) = want.split_once('|') {
        if let Some(source) = source.strip_prefix(controller::SOURCE_PREFIX) {
            let resolved = source_path(game_root, source)?;
            let entries = controller::entries(&resolved);
            let entry = entries.iter().find(|entry| entry.label == clip)?;
            // A single clip with a speed parameter still has an unambiguous
            // default-clock raw sample. Blend trees and masked layers require
            // the parameter-aware pose evaluator instead.
            if !entry.active_layers.is_empty() || entry.layer.is_some() || matches!(entry.motion,
                Some(controller::MotionNode::OneD{..}|controller::MotionNode::Cartesian{..})) {return None;}
            let clip_source=entry.clip.source.as_ref().and_then(|s|source_path(game_root,s)).unwrap_or(resolved);
            let mut timing=entry.timing;
            if let Some(controller::MotionNode::Clip{cycle,duration_factor,..})=&entry.motion {
                timing.speed/=duration_factor;timing.cycle+=cycle;
            }
            (vec![clip_source], clip, Some(entry.clip.clone()), Some(timing))
        } else {
        let resolved = source_path(game_root, source)?;
        let entry = cached_bundle_entries(&resolved).into_iter()
            .find(|entry| entry.label == clip)?;
        (vec![resolved], clip, Some(entry), None)
        }
    } else {
        (clip_bundle_paths(game_root).into_iter().map(ClipBundleSource::File).collect(), want, None, None)
    };
    for source in paths {
        let Some(files)=preparation_files(&source,exact.as_ref().map(|entry|entry.asset_file.as_str())) else {continue;};
        for (file_name,serialized) in files.iter() {
            if exact.as_ref().is_some_and(|entry|entry.asset_file!=*file_name){continue;}
            let target_pid = exact.as_ref().map(|entry| entry.path_id)
                .or_else(|| controller_clip_path_id(&serialized, want));
            for info in &serialized.objects {
                if info.type_id != 74 {
                    continue;
                }
                if target_pid.is_some_and(|pid| pid != info.path_id) { continue; }
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else {
                    continue;
                };
                let name_match = obj.name().unwrap_or_default().eq_ignore_ascii_case(want);
                let pid_match = target_pid.map(|pid| pid == info.path_id).unwrap_or(false);
                if !(pid_match || (target_pid.is_none() && name_match)) {
                    continue;
                }
                let binds = parse_bindings(&obj);
                let (streamed_n, keys, dur) = parse_streamed_keys(&obj);
                let dense = parse_dense_raw(&obj);
                let constant = parse_constant(&obj);
                let reference=uv_get(&UnityValue::Object(obj.class.properties().clone()),"m_MuscleClip")
                    .and_then(|m|uv_get(m,"m_ValueArrayReferencePose"))
                    .and_then(|v|match v {UnityValue::Array(a)=>Some(a.iter().map(as_f32).collect()),_=>None}).unwrap_or_default();
                let (start, duration) = clip_timing(&obj, dur, &dense);
                let cached = CachedClip {
                    binds,
                    curve_index: streamed_curve_index(&keys),
                    keys,
                    streamed_n,
                    dense,
                    constant,
                    reference,
                    start,
                    duration,
                    scene: scene_for_source(&source),
                    state_timing: timing,
                };
                let sampled = sample_cached(&cached, t);
                store_disk_clip(game_root, &disk_key, &cached);
                if let Ok(mut guard) = CLIP_CACHE.lock() {
                    guard
                        .get_or_insert_with(HashMap::new)
                        .insert(cache_key, Arc::new(cached));
                }
                return Some(sampled);
            }
        }
    }
    None
}

type PreparationFiles=std::sync::Arc<Vec<(String,unity_asset_binary::asset::SerializedFile)>>;
#[derive(Default)]
struct PreparationCache {depth:usize,bytes:usize,loads:usize,entries:Vec<(String,PreparationFiles,usize)>}
thread_local! {static PREPARATION_FILES:std::cell::RefCell<PreparationCache>=std::cell::RefCell::new(PreparationCache::default());}
struct AnimationPreparationScope;
impl AnimationPreparationScope {
    fn new()->Self {begin_animation_preparation();Self}
}
impl Drop for AnimationPreparationScope {
    fn drop(&mut self){end_animation_preparation();}
}
pub fn prepare_animation_batch(game:&Path,keys:&[String])->Vec<bool> {
    let _scope=AnimationPreparationScope::new();
    keys.iter().map(|key|prepare_animation_clip(game,key)).collect()
}
pub(crate) fn begin_animation_preparation(){PREPARATION_FILES.with(|c|c.borrow_mut().depth+=1);}
pub(crate) fn end_animation_preparation(){PREPARATION_FILES.with(|c|{let mut c=c.borrow_mut();c.depth=c.depth.saturating_sub(1);if c.depth==0{c.entries.clear();c.bytes=0;c.loads=0;}});}
fn preparation_files(source:&ClipBundleSource,_file:Option<&str>)->Option<PreparationFiles>{
    // Batch workers may reuse the immutable serialized file for successive
    // clips. Lifetime is the explicit preparation scope, never playback or a
    // process-global bundle cache. Retained payload is bounded at 1 GiB / 4 keys.
    let key=source.payload_key();
    if let Some(files)=PREPARATION_FILES.with(|c|{let mut c=c.borrow_mut();if c.depth==0{return None;}let index=c.entries.iter().position(|(k,_,_)|k==&key)?;let entry=c.entries.remove(index);let files=entry.1.clone();c.entries.push(entry);Some(files)}){return Some(files);}
    PREPARATION_FILES.with(|c|{let mut c=c.borrow_mut();if c.depth>0{c.loads+=1;}});
    let bundle=source.load()?;let _=bundle.data_checked();let mut files=Vec::new();
    for entry in &bundle.files {if let Ok(bytes)=bundle.extract_file_data(entry){if let Ok(parsed)=parse_serialized_file(bytes){files.push((entry.name.clone(),parsed));}}}
    let files=std::sync::Arc::new(files);let bytes=files.iter().map(|(_,f)|f.data().len()).sum::<usize>();
    PREPARATION_FILES.with(|c|{let mut c=c.borrow_mut();const LIMIT:usize=1024*1024*1024;if c.depth==0||bytes>LIMIT||files.is_empty(){return;}while c.bytes+bytes>LIMIT||c.entries.len()>=4{let old=c.entries.remove(0);c.bytes-=old.2;}c.bytes+=bytes;c.entries.push((key,files.clone(),bytes));});
    Some(files)
}

fn load_clip(game_root: &Path, want: &str) -> Option<(Vec<Binding>, HashMap<i32, f32>, u32, u32, usize)> {
    load_clip_at(game_root, want, f32::MAX).map(|(b, v, s, d, c, _)| (b, v, s, d, c))
}

// Metadata queries avoid sampling curves and cloning bindings. Parameterized
// controller clocks still resolve their motion plan on every query.
fn loaded_clip_duration(game_root: &Path, want: &str) -> Option<f32> {
    let cache_key = format!("{}\0{}", game_root.display(), want);
    let cached = CLIP_CACHE.lock().ok().and_then(|guard| guard.as_ref()?.get(&cache_key)
        .map(|c| c.state_timing.map(|clock| c.duration / clock.speed).unwrap_or(c.duration)));
    cached.or_else(|| load_clip_at(game_root, want, f32::MAX).map(|c| c.5))
}

pub fn clip_duration(game_root: &Path, want: &str) -> f32 {
    clip_duration_parameters(game_root,want,&HashMap::new())
}

pub(crate) fn clip_duration_parameters(game_root:&Path,want:&str,parameters:&HashMap<String,f32>) -> f32 {
    if let Some((_,entry,leaves,duration))=motion_plan(game_root,want,parameters) {
        if let Some(layer)=entry.layer.as_ref().filter(|l|l.synchronized) {
            let (source,_)=want.split_once('|').unwrap();
            return clip_duration_parameters(game_root,&format!("{source}|{}",layer.base_label),parameters);
        }
        let _=leaves;return duration/entry.timing.speed;
    }
    loaded_clip_duration(game_root, want).unwrap_or(1.0)
}

/// Raw clip previews retain their existing looping behavior. Explicit states
/// use the controller's authored loop flag, including non-looping end holds.
pub fn clip_looping(game_root: &Path, want: &str) -> bool {
    if let Some((source,label))=want.split_once('|').and_then(|(s,l)|s.strip_prefix(controller::SOURCE_PREFIX).map(|s|(s,l))) {
        if let Some(source)=source_path(game_root,source) {
            if let Some(entry)=controller::entries(&source).iter().find(|e|e.label==label) {return entry.timing.looping;}
        }
    }
    let cache_key = format!("{}\0{}", game_root.display(), want);
    let _ = load_clip_at(game_root, want, 0.0);
    CLIP_CACHE.lock().ok().and_then(|guard| guard.as_ref()?.get(&cache_key)
        .map(|clip| clip.state_timing.map(|clock| clock.looping).unwrap_or(true))).unwrap_or(false)
}

/// The clip's source scene as a scene-mode code (1 Maker, 2 Normal, 3 H/esthetic, 4 Studio).
pub(crate) fn clip_scene_code(game_root: &Path, want: &str) -> i32 {
    match clip_scene(game_root, want) { ClipScene::Maker => 1, ClipScene::Normal => 2, ClipScene::AdultOrEsthetic => 3, ClipScene::Studio => 4 }
}

/// Scene flags written by ChaControl.LateUpdateForce after Animator sampling.
/// A bare name follows the actual bundle chosen by load_clip_at, including
/// controller state resolution; explicit mod sources retain their inner path.
pub fn clip_limb_corrections(game_root: &Path, want: &str) -> (bool, bool) {
    clip_scene(game_root, want).corrections()
}

fn clip_scene(game_root: &Path, want: &str) -> ClipScene {
    if let Some((source,_))=want.split_once('|') {
        if let Some(source)=source.strip_prefix(controller::SOURCE_PREFIX) {
            if let Some(source)=source_path(game_root,source) {return scene_for_source(&source);}
        }
    }
    let cache_key = format!("{}\0{}", game_root.display(), want);
    let cached_scene = || {
        CLIP_CACHE.lock().ok()?.as_ref()?.get(&cache_key).map(|clip| clip.scene)
    };
    if let Some(scene) = cached_scene() { return scene; }
    let _ = load_clip_at(game_root, want, 0.0);
    cached_scene().unwrap_or(ClipScene::Maker)
}

/// The same clip can be previewed in a different scene. 0 follows the clip's
/// source; 1/2/3/4 select Maker, normal gameplay, H/esthetic, or Studio.
fn clip_scene_mode(game_root: &Path, want: &str, mode: i32) -> ClipScene {
    match mode {
        1 => ClipScene::Maker,
        2 => ClipScene::Normal,
        3 => ClipScene::AdultOrEsthetic,
        4 => ClipScene::Studio,
        _ => clip_scene(game_root, want),
    }
}

pub fn clip_limb_corrections_mode(game_root: &Path, want: &str, mode: i32) -> (bool, bool) {
    clip_scene_mode(game_root, want, mode).corrections()
}

/// InitializeExpression is called by Studio.AddObjectFemale/Male, not the
/// gameplay/Maker character factory. An otherwise correct elbow formula tears
/// action clips if a component from a different scene is inserted unconditionally.
pub fn clip_expression_bone_mode(game_root: &Path, want: &str, mode: i32) -> bool {
    clip_scene_mode(game_root, want, mode) == ClipScene::Studio
}

#[cfg(test)]
mod catalogue_tests {
    #[test]
    fn raw_catalogue_is_shared_without_mixing_controller_graphs(){
        let root=Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/PairedAnimations");
        let base=ClipBundleSource::File(root.join("abdata/studio/anime/00.unity3d"));
        let first=ClipBundleSource::Studio{base:Box::new(base.clone()),owner:"pose_13".into(),overlay:None,studio_profile:false};
        let second=ClipBundleSource::Studio{base:Box::new(base.clone()),owner:"dance_19".into(),overlay:None,studio_profile:false};
        let signature=|entries:Vec<ClipEntry>|entries.into_iter().map(|e|(e.asset_file,e.path_id,e.name,e.label,e.owners)).collect::<Vec<_>>();
        let expected=signature(cached_bundle_entries(&base));assert!(!expected.is_empty());
        assert_eq!(expected,signature(cached_bundle_entries(&first)));assert_eq!(expected,signature(cached_bundle_entries(&second)));
        let cache=CLIP_CATALOGUES.get().unwrap().lock().unwrap();
        assert!(cache.contains_key(&base.payload_key()));assert!(!cache.contains_key(&first.key()));assert!(!cache.contains_key(&second.key()));drop(cache);
        for (source,owner) in [(&first,"pose_13 / "),(&second,"dance_19 / ")]{
            let graph=controller::entries(source);assert!(!graph.is_empty());assert!(graph.iter().all(|e|e.label.starts_with(owner)));
        }
    }
    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn preparation_scope_reuses_immutable_files_and_releases_nested_ownership(){
        let game=&crate::test_support::bundles_root();if !game.ok_exists(){return;}
        let source=source_path(game,"studio_k/anime/04.unity3d").unwrap();
        begin_animation_preparation();let first=preparation_files(&source,None).unwrap();assert!(!first.is_empty());
        let weak=Arc::downgrade(&first);begin_animation_preparation();let second=preparation_files(&source,None).unwrap();assert!(Arc::ptr_eq(&first,&second));
        end_animation_preparation();let third=preparation_files(&source,None).unwrap();assert!(Arc::ptr_eq(&first,&third));
        for owner in ["owner_a","owner_b"] {
            let controller=ClipBundleSource::Studio{base:Box::new(source.clone()),owner:owner.into(),overlay:None,studio_profile:false};
            assert_ne!(controller.key(),source.key());
            assert!(Arc::ptr_eq(&first,&preparation_files(&controller,None).unwrap()));
        }
        PREPARATION_FILES.with(|c|{let c=c.borrow();assert_eq!(c.loads,1);assert!(c.bytes<=1024*1024*1024&&c.entries.len()<=4);});
        end_animation_preparation();let cold=preparation_files(&source,None).unwrap();assert!(!Arc::ptr_eq(&first,&cold));
        assert_eq!(first.len(),cold.len());for ((a_name,a),(b_name,b)) in first.iter().zip(cold.iter()){assert_eq!(a_name,b_name);assert_eq!(a.data(),b.data());assert_eq!(a.objects.len(),b.objects.len());}
        PREPARATION_FILES.with(|c|{let c=c.borrow();assert!(c.depth==0&&c.bytes==0&&c.entries.is_empty());});
        drop(first);drop(second);drop(third);assert!(weak.upgrade().is_none());
        assert!(source_path(Path::new("C:/__missing_animation_root__"),"studio_k/anime/04.unity3d").is_none());
        assert!(source_path(game,"../studio_k/anime/04.unity3d").is_none());
    }
    #[test]
    fn indexed_source_labels_preserve_native_catalogue() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_exists() { return; }
        for source in ["states:studio/anime/00.unity3d", "studio/anime/00.unity3d"] {
            let start = std::time::Instant::now();
            let labels = super::source_labels(game, source);
            let cold = start.elapsed();
            let descriptor = source.strip_prefix("states:").unwrap_or(source);
            let resolved = super::source_path(game, descriptor).unwrap();
            let expected: Vec<String> = if source.starts_with("states:") {
                super::controller::entries(&resolved).iter().filter(|e| super::public_state_entry(e))
                    .map(|e| e.label.clone()).collect()
            } else { super::cached_bundle_entries(&resolved).into_iter().map(|e| e.label).collect() };
            assert!(!expected.is_empty());
            assert_eq!(*labels, expected);
            assert!(std::sync::Arc::ptr_eq(&labels, &super::source_labels(game, source)));
            let start = std::time::Instant::now();
            for _ in 0..3 {
                assert_eq!(super::clip_count_for_source(game, source), expected.len());
                for (i, name) in expected.iter().enumerate() {
                    assert_eq!(super::clip_name_for_source_at(game, source, i).as_ref(), Some(name));
                }
            }
            let indexed = start.elapsed();
            let start = std::time::Instant::now();
            if source.starts_with("states:") {
                for _ in 0..3 {for (i, name) in expected.iter().enumerate() {
                    let entry=super::controller::entries(&resolved).iter().filter(|e|super::public_state_entry(e)).nth(i).unwrap().label.clone();
                    assert_eq!(&entry,name);
                }}
            }
            let previous_scan=start.elapsed();
            assert_eq!(super::clip_name_for_source_at(game, source, expected.len()), None);
            assert_eq!(super::clip_name_for_source_at(game, source, usize::MAX), None);
            for name in &expected { assert!(super::selectable_clip(game, &format!("{source}|{name}"))); }
            eprintln!("catalogue perf source={source} labels={} cold_ms={:.3} indexed_3x_ms={:.3} previous_scan_3x_ms={:.3}", expected.len(), cold.as_secs_f64()*1000., indexed.as_secs_f64()*1000.,previous_scan.as_secs_f64()*1000.);
        }
        assert_eq!(super::clip_count_for_source(game, "states:../outside.unity3d"), 0);
        assert!(!super::selectable_clip(game, "states:studio/anime/00.unity3d|missing"));
    }
use super::*;

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn explicit_controller_state_keeps_controller_identity_and_overrides() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_is_dir() { return; }
        let source = "states:studio_k/anime/04.unity3d";
        let names = clip_names_for_source(game, source);
        let resolved=source_path(game,source.strip_prefix(controller::SOURCE_PREFIX).unwrap()).unwrap();
        assert_eq!(controller::entries(&resolved).iter().filter(|e|e.layer.is_none()).count(),66,
            "all previously supported base controller states must remain distinct");
        assert!(names.len()>=66,"explicit layers must not replace the original base states");
        for owner in ["pose_04", "gravure_00"] {
            let key = format!("{source}|{owner} / Base Layer.tachi_00");
            assert!(selectable_clip(game, &key), "{key}");
            assert!(load_clip_at(game, &key, 0.05).is_some());
        }
        assert!(!selectable_clip(game, &format!("{source}|tachi_00")));
        let over = "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle";
        assert!(selectable_clip(game, over));
        let state = load_clip_at(game, over, 0.25).expect("overridden state");
        let raw = load_clip_at(game, "action/animator/00.unity3d|Stand_map_33_00", 0.25 * 0.7)
            .expect("referenced override clip");
        assert!((state.5 - raw.5 / 0.7).abs() < 1e-5);
        assert_eq!(state.1.len(), raw.1.len());
        for (i, value) in &state.1 {
            assert!((value - raw.1[i]).abs() < 1e-5, "curve {i}");
        }
        let blend = "states:action/animator/00.unity3d|base / Base Layer.Twoshot.Twoshot_00";
        assert!(selectable_clip(game, blend), "the actual 1D state is selectable");
        assert!(load_clip_at(game, blend, 0.25).is_none(),
            "a multi-motion state must not silently sample only its first clip");
    }

    #[test]
    #[ignore="read-only portable rig topology cache oracle"]
    fn rig_binding_cache_matches_uncached_and_invalidates_topology() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();let session=crate::portable::load(Path::new(&path)).unwrap();let mut rig=session.body_skel.unwrap();
        for frame in 0..30 {
            for tf in rig.tfs.values_mut(){tf.pos[0]+=frame as f32*0.001;}
            let cached=cached_rig_bindings(&rig);let oracle=rig_binding_data_uncached(&rig);
            assert_eq!(cached.paths,oracle.paths);assert_eq!(cached.by_name,oracle.by_name);
            assert!(std::sync::Arc::ptr_eq(&cached,&cached_rig_bindings(&rig)));
        }
        for change in 0..3 {
            let before=cached_rig_bindings(&rig);
            let id=*rig.tfs.keys().find(|id|rig.tfs[id].parent.is_some()).unwrap();
            match change {0=>rig.tfs.get_mut(&id).unwrap().name.push_str("_changed"),1=>rig.tfs.get_mut(&id).unwrap().parent=None,_=>rig.bone_ids.reverse()};
            let after=cached_rig_bindings(&rig);let oracle=rig_binding_data_uncached(&rig);
            assert!(!std::sync::Arc::ptr_eq(&before,&after));assert_eq!(after.paths,oracle.paths);assert_eq!(after.by_name,oracle.by_name);
        }
        for i in 0..12 {let mut other=rig.clone();other.bone_ids.push(-100-i);cached_rig_bindings(&other);}
        RIG_BINDING_CACHE.with(|cache|assert!(cache.borrow().len()<=8));
    }

    #[test]
    #[ignore="real native curves and portable topology binding plan oracle"]
    fn prepared_binding_plan_matches_legacy_frames_and_invalidates() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let session=crate::portable::load(Path::new(&path)).unwrap();let baseline=session.body_skel.unwrap();
        let root=std::env::var("OK_TEST_GAME_ROOT").unwrap();let game=Path::new(&root);
        let key="studio/anime/00.unity3d|f_aruki_00_00_01";
        for frame in 0..240 {
            let (binds,values,s,d,c,_)=load_clip_at(game,key,frame as f32/120.).unwrap();
            let mut oracle=baseline.clone();let mut actual=baseline.clone();
            apply_binding_values_legacy(game,&mut oracle,key,binds.clone(),values.clone(),s,d,c);
            apply_binding_values(game,&mut actual,key,binds,values,s,d,c);
            assert_eq!(actual.worlds,oracle.worlds,"frame {frame}");
            for(id,tf)in &actual.tfs{let expected=&oracle.tfs[id];assert_eq!(tf.pos,expected.pos);assert_eq!(tf.rot,expected.rot);assert_eq!(tf.scale,expected.scale);}
            let mut direct=baseline.clone();apply_raw_clip_impl(game,&mut direct,key,frame as f32/120.,false);
            assert_eq!(direct.worlds,oracle.worlds,"direct channel frame {frame}");
            for(id,tf)in &direct.tfs{let expected=&oracle.tfs[id];assert_eq!(tf.pos,expected.pos);assert_eq!(tf.rot,expected.rot);assert_eq!(tf.scale,expected.scale);}
        }
        let (binds,_,_,_,_,_)=load_clip_at(game,key,0.).unwrap();let avatar=load_avatar_tos_shared(game);
        let rig=cached_rig_bindings(&baseline);let before=binding_plan(rig.clone(),avatar.clone(),&binds);
        assert!(Arc::ptr_eq(&before,&binding_plan(rig.clone(),avatar.clone(),&binds)));
        let mut changed=baseline.clone();let id=*changed.tfs.keys().next().unwrap();changed.tfs.get_mut(&id).unwrap().name.push_str("_changed");
        assert!(!Arc::ptr_eq(&before,&binding_plan(cached_rig_bindings(&changed),avatar.clone(),&binds)));
        let mut other_avatar=(*avatar).clone();other_avatar.insert(0,"changed".into());
        assert!(!Arc::ptr_eq(&before,&binding_plan(rig.clone(),Arc::new(other_avatar),&binds)));
        for i in 0..40 {let mut other=binds.clone();other.push(Binding{path:0,attribute:i,type_id:99});binding_plan(rig.clone(),avatar.clone(),&other);}
        BINDING_PLAN_CACHE.with(|cache|assert!(cache.borrow().len()<=32));
    }

    #[test]
    #[ignore="native controller single-leaf composition and exact baseline reset oracle"]
    fn prepared_motion_pose_matches_clone_composition_and_reset() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();let session=crate::portable::load(Path::new(&path)).unwrap();let baseline=session.body_skel.unwrap();
        let root=std::env::var("OK_TEST_GAME_ROOT").unwrap();let game=Path::new(&root);let parameters=HashMap::new();
        let key="states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01";assert!(prepare_animation_clip(game,key));
        let source="studio/anime/00.unity3d";let resolved=source_path(game,source).unwrap();
        let mut entry=controller::entries(&resolved).iter().find(|entry|key.ends_with(&entry.label)).unwrap().clone();
        // Vanilla fixed-clock states deliberately omit MotionNode. Build the
        // equivalent full-weight leaf to exercise parameter-aware composition.
        entry.motion=Some(controller::MotionNode::Clip{clip:entry.clip.clone(),cycle:0.,duration_factor:1.});
        let (source,entry,leaves,duration)=motion_plan_entry(game,source,entry,&parameters).unwrap();
        assert_eq!(leaves.len(),1);assert_eq!(leaves[0].1,1.);
        for frame in 0..240 {
            let mut expected=baseline.clone();let mut actual=baseline.clone();
            let time=if frame==239{f32::MAX}else{frame as f32/120.};
            assert!(sample_motion_pose_impl(game,&mut expected,&baseline,&source,&entry,leaves.clone(),duration,time,&parameters,0.,0.,false));
            assert!(sample_motion_pose_impl(game,&mut actual,&baseline,&source,&entry,leaves.clone(),duration,time,&parameters,0.,0.,true));
            assert_eq!(actual.worlds,expected.worlds,"frame {frame}");
            for(id,tf)in &actual.tfs{let old=&expected.tfs[id];assert_eq!(tf.pos,old.pos);assert_eq!(tf.rot,old.rot);assert_eq!(tf.scale,old.scale);}
        }
        let mut changed=baseline.clone();for tf in changed.tfs.values_mut(){tf.pos=[2.;3];tf.parent=None;tf.name.push_str("modified");}changed.bone_ids.reverse();changed.restore_from(&baseline);
        assert_eq!(serde_json::to_value(&changed).unwrap(),serde_json::to_value(&baseline).unwrap());
        let id=*changed.tfs.keys().next().unwrap();changed.tfs.remove(&id);changed.restore_from(&baseline);
        assert_eq!(serde_json::to_value(&changed).unwrap(),serde_json::to_value(&baseline).unwrap());
    }

    #[test]
    #[ignore="installed common Demo catalogue preparation regression"]
    fn demo_skipping_stones_preparation() {
        let root=std::env::var("OK_TEST_GAME_ROOT").unwrap();let game=Path::new(&root);
        let catalog:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(game.join("editor-demos.json")).unwrap()).unwrap();
        let row=catalog["demos"].as_array().unwrap().iter().find(|r|r["id"]==984).unwrap();
        let key=row["key"].as_str().unwrap();let source=key.split_once('|').unwrap().0;
        let start=std::time::Instant::now();
        eprintln!("demo regression enumerate start {key}");
        let names=clip_names_for_source(game,source);
        eprintln!("demo regression enumerate ms={} names={}",start.elapsed().as_millis(),names.len());
        assert!(names.iter().any(|name|key.ends_with(name)));
        assert!(prepare_animation_clip(game,key));
        eprintln!("demo regression prepared ms={}",start.elapsed().as_millis());
        assert!(clip_duration(game,key)>0.);
    }

    #[test]
    #[ignore="real concurrent preparation/sampling diagnostic via portable fixture and game root"]
    fn background_motion_preparation_keeps_sampling_responsive() {
        let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();let session=crate::portable::load(Path::new(&path)).unwrap();let baseline=session.body_skel.unwrap();
        let root=std::env::var("OK_TEST_GAME_ROOT").unwrap();let game=Path::new(&root);
        let walk="states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01";assert!(prepare_animation_clip(game,walk));
        let source="states:studio/anime/00.unity3d";let keys:Vec<_>=clip_names_for_source(game,source).into_iter().take(32).map(|name|format!("{source}|{name}")).collect();assert_eq!(keys.len(),32);
        let worker_root=root.clone();let worker=std::thread::spawn(move||{
            struct Scope;impl Drop for Scope{fn drop(&mut self){end_animation_preparation();}}
            begin_animation_preparation();let scope=Scope;let start=std::time::Instant::now();
            for key in keys {assert!(prepare_animation_clip(Path::new(&worker_root),&key));}
            PREPARATION_FILES.with(|c|{let c=c.borrow();eprintln!("OK animation preparation batch_ms={:.3} loads={} retained_bytes={} entries={}",start.elapsed().as_secs_f64()*1000.,c.loads,c.bytes,c.entries.len());assert!(c.bytes<=1024*1024*1024&&c.entries.len()<=4);});
            drop(scope);PREPARATION_FILES.with(|c|{let c=c.borrow();assert!(c.depth==0&&c.bytes==0&&c.entries.is_empty());});
        });
        let mut skel=baseline.clone();let parameters=HashMap::new();apply_clip_at_parameters(game,&mut skel,walk,0.,&parameters);
        let mut frames=0;let mut maximum=0f64;let mut total=0f64;
        while !worker.is_finished()||frames<240 {
            skel.restore_from(&baseline);let start=std::time::Instant::now();apply_clip_at_parameters(game,&mut skel,walk,frames as f32/120.,&parameters);let ms=start.elapsed().as_secs_f64()*1000.;maximum=maximum.max(ms);total+=ms;frames+=1;
            assert!(skel.worlds.iter().flatten().flatten().all(|v|v.is_finite()));
            std::thread::sleep(std::time::Duration::from_millis(4));
        }
        worker.join().unwrap();eprintln!("OK animation concurrent preparation frames={frames} mean_ms={:.3} maximum_ms={maximum:.3}",total/frames as f64);
        assert!(maximum<=300.,"prepared playback blocked for {maximum}ms");
    }

    #[test]
    fn binding_path_overlay_matches_owned_merge_and_preserves_snapshot() {
        let mut avatar=std::sync::Arc::new([(1,"authored/name".to_string()),(3,"authored/collision".into())].into_iter().collect::<HashMap<_,_>>());
        let snapshot=avatar.clone();
        let rig=[(1,Some(("rig/other".to_string(),1))),(2,Some(("rig/helper".into(),2))),(3,None),(4,None)].into_iter().collect();
        let overlay=BindingPaths{avatar:&snapshot,rig:&rig};
        let mut merged=(*snapshot).clone();for (hash,path) in &rig{if let Some((path,_))=path{merged.entry(*hash).or_insert_with(||path.clone());}}
        assert_eq!(overlay.len(),merged.len());for hash in 0..6{assert_eq!(overlay.get(&hash),merged.get(&hash));}
        std::sync::Arc::make_mut(&mut avatar).insert(1,"new/avatar".into());
        assert_eq!(snapshot.get(&1).unwrap(),"authored/name");assert_eq!(avatar.get(&1).unwrap(),"new/avatar");
    }

    #[test]
    fn indexed_streamed_sampling_matches_scan_at_boundaries_and_steps() {
        let mut keys=Vec::new();
        for frame in 0..20 {for curve in 0..17 {keys.push(StreamedKey{time:frame as f32*0.1,index:curve,
            coeff:if curve%3==0 {[f32::INFINITY,0.,0.,frame as f32]} else {[0.2,-0.3,0.7,(frame+curve) as f32]}});}}
        keys.insert(17,keys[0]); // Duplicate time: last previous and first next.
        let curves=streamed_curve_index(&keys).unwrap();
        for i in -5..230 {let t=i as f32*0.01;assert_eq!(sample_streamed_keys(&keys,t),sample_indexed_keys(&keys,&curves,t),"t={t}");}
        assert_eq!(sample_streamed_keys(&keys,f32::MAX),sample_indexed_keys(&keys,&curves,f32::MAX));
        let last=keys.len()-1;keys.swap(0,last);assert!(streamed_curve_index(&keys).is_none());
    }

    #[test]
    #[ignore="read-only actual walk clip sampling and timing oracle"]
    fn indexed_walk_sampling_matches_original_scan() {
        let root=PathBuf::from(std::env::var("OK_TEST_SHARED_ROOT").unwrap());
        let want="studio/anime/00.unity3d|f_aruki_00_00_01";
        load_clip_at(&root,want,0.).unwrap();
        let clip=CLIP_CACHE.lock().unwrap().as_ref().unwrap().get(&format!("{}\0{}",root.display(),want)).unwrap().clone();
        let curves=clip.curve_index.as_ref().unwrap();
        for i in 0..300 {let t=clip.start+clip.duration*i as f32/299.;assert_eq!(sample_streamed_keys(&clip.keys,t),sample_indexed_keys(&clip.keys,curves,t));}
        let begin=std::time::Instant::now();for i in 0..300{std::hint::black_box(sample_streamed_keys(&clip.keys,i as f32/30.));}let scan=begin.elapsed();
        let begin=std::time::Instant::now();for i in 0..300{std::hint::black_box(sample_indexed_keys(&clip.keys,curves,i as f32/30.));}
        eprintln!("walk sample scan_ms={:.6} indexed_ms={:.6} keys={} curves={}",scan.as_secs_f64()*1000./300.,begin.elapsed().as_secs_f64()*1000./300.,clip.keys.len(),curves.len());
    }

    #[test]
    fn duration_reads_loaded_metadata_without_resampling_curves() {
        let root=Path::new("duration-metadata-fixture-no-assets");
        for speed in [None,Some(0.5),Some(2.0)] {
            let want=format!("synthetic-duration-{speed:?}");
            // Invalid storage cannot be sampled; duration needs metadata only.
            let clip=CachedClip { binds:Vec::new(), keys:Vec::new(), curve_index:None, streamed_n:0,
                dense:DenseSamples { count:2, frames:4, rate:30., begin:0., samples:Vec::new() },
                constant:Vec::new(), reference:Vec::new(), start:0.25, duration:2.5,
                scene:ClipScene::Studio,
                state_timing:speed.map(|speed|controller::StateTiming{speed,cycle:0.25,looping:true}) };
            let key=format!("{}\0{}",root.display(),want);
            CLIP_CACHE.lock().unwrap().get_or_insert_with(HashMap::new).insert(key.clone(),Arc::new(clip));
            assert_eq!(clip_duration_parameters(root,&want,&HashMap::new()),2.5/speed.unwrap_or(1.));
            CLIP_CACHE.lock().unwrap().as_mut().unwrap().remove(&key);
        }
    }

    #[test]
    fn samples_between_streamed_and_dense_frames() {
        let clip = CachedClip {
            binds: Vec::new(), keys: vec![
                StreamedKey { time: 0.0, index: 0, coeff: [0.0, 0.0, 1.0, 0.0] },
                StreamedKey { time: 1.0, index: 0, coeff: [0.0, 0.0, 0.0, 1.0] },
            ],
            curve_index:None, streamed_n: 1, dense: DenseSamples {
                count: 1, frames: 2, rate: 1.0, begin: 0.0, samples: vec![0.0, 1.0],
            },
            constant: Vec::new(), reference:Vec::new(), start: 0.0, duration: 1.0, scene: ClipScene::Maker, state_timing: None,
        };
        let values = sample_cached(&clip, 0.25).1;
        assert!((values[&0] - 0.25).abs() < 1e-6);
        assert!((values[&1] - 0.25).abs() < 1e-6);
    }

    #[test]
    fn dense_sampling_uses_its_clock_and_ignores_padding_after_stop() {
        let dense = DenseSamples {
            count: 1, frames: 4, rate: 2.0, begin: 0.25,
            samples: vec![0.0, 1.0, 2.0, 99.0],
        };
        assert_eq!(dense_at(&dense, 0.25, 1.25), vec![0.0]);
        assert_eq!(dense_at(&dense, 0.75, 1.25), vec![1.0]);
        assert_eq!(dense_at(&dense, 1.0, 1.25), vec![1.5]);
        assert_eq!(dense_at(&dense, 1.25, 1.25), vec![2.0]);
    }

    #[test]
    fn game_sitting_clip_applies_authored_hand_scale() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_is_dir() { return; }
        use crate::pose::Tf;
        let mut tfs = HashMap::new();
        tfs.insert(1, Tf { sibling_index: None, name: "cf_j_root".into(), parent: None,
            pos: [0.0; 3], rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
        tfs.insert(2, Tf { sibling_index: None, name: "cf_j_hand_L".into(), parent: Some(1),
            pos: [0.0; 3], rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
        let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
            smr_count: 2, tfs: tfs.into(), bone_ids: vec![1, 2], source_mesh: 0 };
        apply_clip_at(game, &mut skel, "GSit_00", 0.25);
        let hand = &skel.tfs[&2];
        assert!((hand.scale[1] - 0.9).abs() < 0.001, "{:?}", hand.scale);
        assert!((hand.scale[2] - 0.9).abs() < 0.001, "{:?}", hand.scale);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn action_clip_animates_deformation_helpers_as_in_game_probe() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_is_dir() { return; }
        use crate::pose::Tf;
        let tfs = [(1, "cf_j_root"), (2, "cf_d_siri_L"), (3, "cf_s_kneeB_L")]
            .into_iter().map(|(id, name)| (id, Tf { sibling_index: None, name: name.into(),
                parent: (id != 1).then_some(1), pos: [0.0; 3],
                rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] })).collect();
        let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
            smr_count: 3, tfs, bone_ids: vec![1, 2, 3], source_mesh: 0 };
        apply_clip_at(game, &mut skel,
            "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle", 0.5);
        // Live CharaStudio probe at normalized 0.2625 with ExpressionBone
        // disabled: these channels are Animator output, not morph defaults.
        assert!((skel.tfs[&2].scale[0] - 1.01344323).abs() < 1e-5);
        for (actual, expected) in skel.tfs[&3].pos.iter().zip([0.0, -0.03418356, -0.08344822]) {
            assert!((actual - expected).abs() < 1e-5, "knee helper {actual} != {expected}");
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn expression_component_follows_scene_factory_and_source() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_is_dir() { return; }
        let action = "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle";
        for mode in 0..=3 { assert!(!clip_expression_bone_mode(game, action, mode)); }
        assert!(clip_expression_bone_mode(game, action, 4));
        let studio = "states:studio_k/anime/04.unity3d|pose_04 / Base Layer.tachi_00";
        assert!(clip_expression_bone_mode(game, studio, 0));
        assert!(!clip_expression_bone_mode(game, studio, 2));
        assert_eq!(scene_for_source(&ClipBundleSource::Zip(PathBuf::from("C:/mods/test.zipmod"),
            "studio_k/anime/example.unity3d".into())), ClipScene::Studio);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn game_scene_flags_override_clip_arm_hand_scale_after_sampling() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_is_dir() { return; }
        assert_eq!(clip_limb_corrections(game, "GSit_00"), (true, true));
        assert_eq!(clip_limb_corrections(game, "Stand_D_00"), (true, true));
        assert_eq!(clip_limb_corrections(game, "f_aruki_00_00_01"), (false, true));
        assert_eq!(clip_limb_corrections(game, "esthetic/animator/00.unity3d|L_aomuke [ke_f_00]"), (false, false));
        assert_eq!(scene_for_source(&ClipBundleSource::Zip(
            PathBuf::from("C:/mods/animation.zipmod"), "h/anim/female/example.unity3d".into(),
        )).corrections(), (false, false));
        assert_eq!(clip_limb_corrections_mode(game, "GSit_00", 3), (false, false));
        assert_eq!(clip_limb_corrections_mode(game, "f_aruki_00_00_01", 1), (true, true));
        let mut tfs = HashMap::new();
        for (id, name) in [(1, "cf_j_forearm01_L"), (2, "cf_j_hand_L")] {
            tfs.insert(id, crate::pose::Tf { sibling_index: None, name: name.into(), parent: None,
                pos: [0.0; 3], rot: [0.0, 0.0, 0.0, 1.0], scale: [0.92, 0.9, 0.9] });
        }
        let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
            smr_count: 2, tfs: tfs.into(), bone_ids: vec![1, 2], source_mesh: 0 };
        let (arm, hand) = clip_limb_corrections(game, "f_aruki_00_00_01");
        crate::pose::apply_scene_limb_size_corrections(&mut skel, arm, hand);
        assert_eq!(skel.tfs[&1].scale, [1.0, 1.0, 1.0]);
        assert_eq!(skel.tfs[&2].scale, [1.0, 0.9, 0.9]);
    }

    #[test]
    fn streamed_coefficients_preserve_cubic_and_stepped_segments() {
        let keys = [
            StreamedKey { time: 0.0, index: 0, coeff: [0.0, 1.0, 0.0, 0.0] },
            StreamedKey { time: 1.0, index: 0, coeff: [0.0, 0.0, 0.0, 1.0] },
            StreamedKey { time: 0.0, index: 1, coeff: [0.0, 0.0, 0.0, 2.0] },
            StreamedKey { time: 1.0, index: 1, coeff: [0.0, 0.0, 0.0, 3.0] },
        ];
        let sample = sample_streamed_keys(&keys, 0.25);
        assert!((sample[&0] - 0.0625).abs() < 1e-6);
        assert_eq!(sample[&1], 2.0);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn game_catalogue_resolves_distinct_motion_frames() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_exists() { return; }
        let names = clip_names(game);
        assert!(names.iter().any(|n| n == "f_aruki_00_00_01"));
        assert!(names.iter().any(|n| n == "Fmannequin_00"));
        let first = load_clip_at(game, "f_aruki_00_00_01", 0.0).expect("walking clip");
        let second = load_clip_at(game, "f_aruki_00_00_01", first.5 * 0.5).expect("walking frame");
        assert!(first.5 > 0.05);
        assert!(first.1.iter().any(|(i, a)| second.1.get(i).is_some_and(|b| (a-b).abs() > 1e-4)));
        let sit = load_clip_at(game, "GSit_00", 0.2).expect("sitting clip");
        assert!(!sit.0.is_empty());
        assert!(sit.5 > 0.05);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn studio_source_loads_by_bundle_and_clip_without_name_collision() {
        let game = &crate::test_support::bundles_root();
        if !game.ok_exists() { return; }
        assert_eq!(clip_name_for_source_at(game, "esthetic/animator/00.unity3d", 0),
            Some("L_aomuke [ke_f_00]".to_string()));
        let source = "studio_k/anime/04.unity3d";
        assert!(clip_sources(game).iter().any(|s| s == source));
        let names = clip_names_for_source(game, source);
        assert!(!names.is_empty(), "selected Studio bundle has no AnimationClips");
        eprintln!("Studio source {source}: {} clips, first={}", names.len(), names[0]);
        assert_eq!(clip_name_for_source_at(game, source, 0), Some(names[0].clone()));
        let selected = format!("{source}|{}", names[0]);
        assert!(selectable_clip(game, &selected));
        let clip = load_clip_at(game, &selected, 0.0).expect("Studio AnimationClip");
        assert!(!clip.0.is_empty());
        assert!(clip.5 > 0.0);
        assert!(!selectable_clip(game, "../outside.unity3d|clip"));
        assert!(source_path(game, "abdata/esthetic/animator/00.unity3d").is_some());
        assert!(source_path(game, "abdata/../esthetic/animator/00.unity3d").is_none());
    }

    #[test]
    fn zip_source_resolves_an_exact_internal_bundle() {
        let game = &crate::test_support::bundles_root();
        let zip = game.join(r"mods/Sideloader Modpack - Animations/Omega/[Omega] Free_H_Animations_Bundles v1.1.1.zipmod");
        if !zip.ok_is_file() { return; }
        let source = "mods/Sideloader Modpack - Animations/Omega/[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!h/anim/female/Armpitjob_F.unity3d";
        let resolved = source_path(game, source).expect("zipmod animation source path");
        assert!(matches!(resolved, ClipBundleSource::Zip(_, _)));
        assert!(resolved.load().is_some(), "exact bundle must parse");
        let names = clip_names_for_source(game, source);
        assert!(!names.is_empty(), "mod animation bundle must contain AnimationClips");
        let clip = format!("{source}|{}", names[0]);
        assert!(selectable_clip(game, &clip));
        assert!(load_clip_at(game, &clip, 0.0).is_some());
        assert!(source_path(game, "mods/../other.zipmod!chara/foo.unity3d").is_none());
    }

    #[test]
    fn studio_catalogue_keeps_every_same_name_animation_asset() {
        let game = &crate::test_support::bundles_root();
        let source = "studio_k/anime/04.unity3d";
        if !game.join("abdata").join(source).ok_is_file() { return; }
        let bundle = source_path(game, source).unwrap().load().unwrap();
        let mut asset_count = 0;
        for file in &bundle.files {
            let Ok(bytes) = bundle.extract_file_data(file) else { continue; };
            let Ok(serialized) = parse_serialized_file(bytes) else { continue; };
            asset_count += serialized.objects_of_type(74).len();
        }
        let names = clip_names_for_source(game, source);
        assert_eq!(names.len(), asset_count, "same-name assets were removed from the catalogue");
        let standing: Vec<_> = names.iter().filter(|name| name.starts_with("tachi_00")).collect();
        assert_eq!(standing.len(), 3, "three distinct standing clips must be selectable");
        assert!(!selectable_clip(game, &format!("{source}|tachi_00")),
            "ambiguous name must not silently select one clip");
        assert!(load_clip_at(game, &format!("{source}|tachi_00"), 0.35).is_none());
        let values: Vec<_> = standing.iter().map(|name| {
            let key = format!("{source}|{name}");
            assert!(selectable_clip(game, &key));
            eprintln!("distinct standing selector: {key}");
            load_clip_at(game, &key, 0.35).unwrap().1
        }).collect();
        assert!(values.iter().skip(1).any(|next| values[0].iter().any(|(i, first)|
            next.get(i).is_some_and(|value| (first-value).abs() > 1e-4))),
            "distinct selectors still load the first same-name clip");
    }

    #[test]
    #[ignore = "full installed-game and mod AnimationClip identity catalogue audit"]
    fn animation_inventory_preserves_all_exported_assets() {
        let game = &crate::test_support::bundles_root();
        let inventory = crate::gamefs::read_to_string("../../Saved/anim_export/animation_clips.tsv")
            .expect("exported animation inventory");
        let mut counts = std::collections::BTreeMap::<String, usize>::new();
        for line in inventory.lines().skip(1) {
            let source = line.split('\t').nth(1).expect("source column").replace('\\', "/");
            let source = source.strip_prefix(crate::test_support::game_prefix().as_str())
                .unwrap_or(&source).to_string();
            *counts.entry(source).or_default() += 1;
        }
        let mut total = 0;
        for (source, expected) in &counts {
            let labels = clip_names_for_source(game, source);
            assert_eq!(labels.len(), *expected, "lost clip identities in {source}");
            assert_eq!(labels.iter().collect::<HashSet<_>>().len(), labels.len(),
                "display labels collide in {source}");
            total += labels.len();
            eprintln!("asset catalogue: {source}: {} clips", labels.len());
        }
        eprintln!("asset catalogue verified: {} sources, {total} clips", counts.len());
        assert_eq!(total, inventory.lines().count() - 1);
    }

    #[test]
    fn mod_controller_state_resolves_its_own_clip() {
        let game = &crate::test_support::bundles_root();
        let source = "mods/Sideloader Modpack - Animations/Omega/[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!h/anim/female/Armpitjob_F.unity3d";
        if source_path(game, source).is_none() { return; }
        let states = format!("states:{source}");
        let names = clip_names_for_source(game, &states);
        assert!(names.iter().any(|n| n == "khh_f_94 / Base Layer.L_Idle"));
        let key = format!("{states}|khh_f_94 / Base Layer.L_Idle");
        let state = load_clip_at(game, &key, 0.25).expect("mod controller state");
        let raw = load_clip_at(game, &format!("{source}|L_Idle"), 0.25).expect("mod clip");
        assert_eq!(state.1, raw.1);
        assert_eq!(state.5, raw.5);
        assert_eq!(clip_limb_corrections(game, &key), (false, false));
        assert!(!selectable_clip(game, "states:../outside.unity3d|controller / state"));
    }

    #[test]
    #[ignore = "installed base/mod controller state coverage and identity audit"]
    fn controller_state_inventory() {
        let game = &crate::test_support::bundles_root();
        let inventory = crate::gamefs::read_to_string("../../Saved/anim_export/animation_sources.tsv")
            .expect("exported source inventory");
        let mut sources = 0;
        let mut total = 0;
        for row in inventory.lines().skip(1) {
            let columns: Vec<_> = row.split('\t').collect();
            if columns[3] == "0" && columns[4] == "0" { continue; }
            let normalized = columns[1].replace('\\', "/");
            let source = normalized.strip_prefix(crate::test_support::game_prefix().as_str()).unwrap_or(&normalized);
            let bundle = source_path(game, source).expect(source);
            let states = controller::entries(&bundle);
            assert_eq!(states.iter().map(|e| &e.label).collect::<HashSet<_>>().len(), states.len(),
                "state labels collide in {source}");
            let raw = cached_bundle_entries(&bundle);
            for entry in states.iter() {
                assert!(raw.iter().any(|clip| clip.path_id == entry.clip.path_id
                    && clip.asset_file == entry.clip.asset_file), "state clip missing in {source}");
                assert!(entry.timing.speed > 0.0 && entry.timing.speed.is_finite());
            }
            sources += 1;
            total += states.len();
            eprintln!("controller inventory: {source}: {} supported states", states.len());
        }
        assert!(sources > 50 && total > 10_000, "controller inventory unexpectedly empty");
        eprintln!("controller inventory verified: {sources} sources, {total} supported states");
    }

    #[test]
    #[ignore = "manual catalogue audit of larger game animation bundles"]
    fn print_additional_animation_sources() {
        let game = &crate::test_support::bundles_root();
        for source in ["studio/anime/01.unity3d", "h/anim/female/04_01_00.unity3d",
                       "adv/motion/controller/adv/00.unity3d", "esthetic/animator/00.unity3d"] {
            let names = clip_names_for_source(game, source);
            eprintln!("{source}: {} clips, first={:?}", names.len(), &names[..names.len().min(8)]);
        }
    }

    #[test]
    #[ignore = "manual check of Unity streamed cubic coefficient continuity"]
    fn audit_game_streamed_polynomial_endpoints() {
        let game = &crate::test_support::bundles_root();
        let name = "f_aruki_00_00_01";
        let _ = load_clip_at(game, name, 0.0).expect("game walking clip");
        let cache_key = format!("{}\0{name}", game.display());
        let guard = CLIP_CACHE.lock().unwrap();
        let clip = guard.as_ref().unwrap().get(&cache_key).unwrap();
        let mut last = HashMap::<i32, StreamedKey>::new();
        let mut errors = Vec::new();
        let mut midpoint_differences = Vec::new();
        for &key in &clip.keys {
            if let Some(prev) = last.insert(key.index, key) {
                let dt = key.time - prev.time;
                let [a, b, slope, origin] = prev.coeff;
                if dt > 0.0 && dt < 1.0 && prev.coeff.iter().all(|x| x.is_finite())
                    && !(a == 0.0 && b == 0.0 && slope == 0.0) {
                    let predicted = ((a * dt + b) * dt + slope) * dt + origin;
                    errors.push((predicted - key.coeff[3]).abs());
                    let half = dt * 0.5;
                    let cubic_middle = ((a * half + b) * half + slope) * half + origin;
                    midpoint_differences.push((cubic_middle - (origin + key.coeff[3]) * 0.5).abs());
                }
            }
        }
        errors.sort_by(|a, b| a.total_cmp(b));
        midpoint_differences.sort_by(|a, b| a.total_cmp(b));
        assert!(errors.len() > 100);
        eprintln!("streamed endpoints n={} median={} p95={} max={}",
            errors.len(), errors[errors.len()/2], errors[errors.len()*95/100], errors[errors.len()-1]);
        eprintln!("cubic-vs-linear midpoint median={} p95={} max={}",
            midpoint_differences[midpoint_differences.len()/2],
            midpoint_differences[midpoint_differences.len()*95/100],
            midpoint_differences[midpoint_differences.len()-1]);
    }

    #[test]
    #[ignore = "manual audit of dense timing fields in installed game bundles"]
    fn audit_game_dense_timing() {
        let game = &crate::test_support::bundles_root();
        for (source, targets) in [
            ("action/animator/00.unity3d", &["f_aruki_00_00_01", "GSit_00"][..]),
            ("adv/motion/controller/adv/00.unity3d", &["Stand_01_00"][..]),
            ("esthetic/animator/00.unity3d", &["L_aomuke"][..]),
        ] {
            let bundle = load_bundle(game.join("abdata").join(source)).unwrap();
            for file in &bundle.files {
                let data = bundle.extract_file_data(file).unwrap();
                let ser = parse_serialized_file(data).unwrap();
                for info in ser.objects_of_type(74) {
                    let obj = UnityObject::from_serialized_file(&ser, &info).unwrap();
                    let name = obj.name().unwrap_or_default();
                    if !targets.iter().any(|target| name.eq_ignore_ascii_case(target)) { continue; }
                    let (streamed_n, _, streamed_end) = parse_streamed_keys(&obj);
                    let dense = clip_object(&obj).and_then(|v| uv_get(v, "m_DenseClip"));
                    let get = |k: &str| dense.and_then(|v| uv_get(v, k));
                    let dense = parse_dense_raw(&obj);
                    let muscle = obj.class.properties().get("m_MuscleClip");
                    let (start, length) = clip_timing(&obj, streamed_end, &dense);
                    let end_frame = (start + length - dense.begin) * dense.rate;
                    let old_end_frame = (dense.frames.saturating_sub(1)) as f32;
                    eprintln!("{source}|{name}: streamed={streamed_n} end={streamed_end} dense={} samples={} frames={:?} rate={:?} begin={:?} start={:?} stop={:?} clip_stop={:?}",
                        dense.count, dense.samples.len(), get("m_FrameCount"), get("m_SampleRate"), get("m_BeginTime"),
                        muscle.and_then(|v| uv_get(v, "m_StartTime")), muscle.and_then(|v| uv_get(v, "m_StopTime")), obj.class.properties().get("m_StopTime"));
                    eprintln!("{source}|{name}: dense end frame source={end_frame:.5}, old={old_end_frame:.5}, delta={:.5}", old_end_frame-end_frame);
                }
            }
        }
    }

    #[test]
    #[ignore = "manual audit of transform binding attributes in real animation clips"]
    fn audit_game_transform_bindings() {
        let game = &crate::test_support::bundles_root();
        for clip in ["f_aruki_00_00_01", "GSit_00",
            "states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle",
            "adv/motion/controller/adv/00.unity3d|Stand_01_00",
            "esthetic/animator/00.unity3d|L_aomuke [ke_f_00]"] {
            let (binds, values, _, _, _, _) = load_clip_at(game, clip, 0.25).unwrap();
            let mut curve = 0i32;
            let mut counts = HashMap::<u32, usize>::new();
            let mut scales = Vec::new();
            let tos = load_avatar_tos(game);
            let mut named_scales = Vec::new();
            let mut other_names = BTreeSet::new();
            for b in binds {
                if let Some(path) = tos.get(&b.path) {
                    let name = bone_name(path);
                    if !name.to_ascii_lowercase().starts_with("cf_j_") { other_names.insert(name.to_string()); }
                }
                *counts.entry(b.attribute).or_default() += 1;
                if b.attribute == 3 {
                    let s = [curve_at(&values, curve), curve_at(&values, curve+1), curve_at(&values, curve+2)];
                    scales.push(s);
                    if s.iter().any(|x| (*x-1.0).abs() > 0.01) {
                        if let Some(path) = tos.get(&b.path) {
                            named_scales.push((path.clone(), s));
                        }
                    }
                }
                curve += curve_dim(&b) as i32;
            }
            eprintln!("{clip}: attrs={counts:?} scale_samples={:?}", &scales[..scales.len().min(6)]);
            eprintln!("{clip}: named_nonunit_scale_count={} examples={:?}", named_scales.len(), &named_scales[..named_scales.len().min(20)]);
            eprintln!("{clip}: non_cf_j_transform_names={other_names:?}");
        }
    }

    #[test]
    fn selected_head_prefab_supplies_mouth_patterns() {
        let path = &crate::test_support::game_path("/abdata/chara/bo_head_00.unity3d");
        let Ok(bytes) = crate::gamefs::read(path) else { return; };
        let parsed = crate::unity::parse_bundle_cached(bytes);
        let pairs = parsed.face_mouth_patterns("p_cf_head_00");
        assert_eq!(pairs.get("cf_o_face").and_then(|p| p.1.first()), Some(&(28, 29)));
        assert_eq!(pairs.get("cf_o_face").map(|p| p.1.len()), Some(43));
        assert_eq!(pairs.get("cf_o_face").map(|p| p.0), Some(0));
        assert!(pairs.contains_key("cf_o_noseline"));
        assert_eq!(pairs.len(), 5);
        assert!(pairs.values().all(|p| p.1.len() == 43));
    }

    #[test]
    fn installed_sexfaces_extends_source_mouth_without_missing_morphs() {
        let game = &crate::test_support::bundles_root();
        if !game.join("BepInEx/plugins/KKS_SexFaces.dll").ok_is_file() { return; }
        let Ok(bytes) = crate::gamefs::read(game.join("abdata/chara/bo_head_00.unity3d")) else { return; };
        let parsed = crate::unity::parse_bundle_cached(bytes);
        let patterns = parsed.face_mouth_patterns("p_cf_head_00");
        let (meshes, _) = parsed.meshes_for_maindata("p_cf_head_00");
        for (name, (target, source)) in patterns {
            let mesh = meshes.iter().find(|m| m.name.eq_ignore_ascii_case(&name)).unwrap();
            let mut part = crate::session::SessionPart::from_mesh("head", mesh, "opaque", false);
            part.mouth_patterns = source;
            let raw_x = mesh.positions.iter().map(|p| p[0]).collect::<Vec<_>>();
            crate::apply_sexfaces_mouth_extensions(&mut part, target, Some(&raw_x));
            assert_eq!(part.mouth_patterns.len(), 54, "{name}");
            for &(close, open) in &part.mouth_patterns {
                assert!(close >= 0 && (close as usize) < part.blend_shapes.len(), "{name} close {close}");
                assert!(open >= 0 && (open as usize) < part.blend_shapes.len(), "{name} open {open}");
            }
            assert!(part.blend_shapes[part.mouth_patterns[46].1 as usize]
                .0.to_string_lossy().starts_with("sexfaces.lopsided.left."));
        }
    }
}

fn norm_quat(mut q: [f32; 4]) -> [f32; 4] {
    if q[3].is_nan() {
        q[3] = 1.0;
    }
    let n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt();
    if n > 1e-8 {
        q[0] /= n;
        q[1] /= n;
        q[2] /= n;
        q[3] /= n;
    } else {
        q = [0.0, 0.0, 0.0, 1.0];
    }
    q
}

fn curve_at(values: &HashMap<i32, f32>, i: i32) -> f32 {
    values.get(&i).copied().unwrap_or(0.0)
}

fn smr_tree(skel: &Skeleton) -> std::collections::HashSet<i64> {
    let mut ids: std::collections::HashSet<i64> = skel.bone_ids.iter().copied().collect();
    let extra: Vec<i64> = skel.bone_ids.iter().copied().collect();
    for id in extra {
        let mut cur = id;
        for _ in 0..64 {
            let Some(p) = skel.tfs.get(&cur).and_then(|t| t.parent) else {
                break;
            };
            if p == 0 || p == cur || !ids.insert(p) {
                break;
            }
            cur = p;
        }
    }
    ids
}

/// Character maker: LoadAnimation(custom/custompose.unity3d, custom_pose);
/// AnimPlay(lstPose[0].State) — default female pose is `Stand_D_00`.
pub fn apply_mannequin_pose(game_root: &Path, skel: &mut Skeleton) {
    apply_mannequin_pose_at(game_root, skel, f32::MAX);
}

pub fn apply_mannequin_pose_at(game_root: &Path, skel: &mut Skeleton, t: f32) {
    let state = std::env::var("OK_POSE").unwrap_or_else(|_| "Stand_D_00".to_string());
    apply_clip_at(game_root, skel, &state, t);
}

pub fn apply_clip_at(game_root: &Path, skel: &mut Skeleton, state: &str, t: f32) {
    apply_clip_at_parameters(game_root,skel,state,t,&HashMap::new());
}

pub(crate) fn animation_float_parameters(game_root:&Path,want:&str) -> Vec<controller::FloatParameter> {
    let Some((source,label))=want.split_once('|') else {return Vec::new();};
    let Some(source)=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s)) else {return Vec::new();};
    controller::entries(&source).iter().find(|e|e.label==label).map(|e|e.parameters.clone()).unwrap_or_default()
}

pub(crate) fn animation_trigger_names(game_root:&Path,want:&str)->Vec<String> {
    let Some((source,label))=want.split_once('|') else {return Vec::new();};
    let Some(source)=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s)) else {return Vec::new();};
    controller::entries(&source).iter().find(|e|e.label==label).map(controller::trigger_names).unwrap_or_default()
}

pub(crate) fn animation_trigger_parameters(game_root:&Path,want:&str,parameters:&HashMap<String,f32>,name:&str,elapsed:f32)->Option<HashMap<String,f32>> {
    if !elapsed.is_finite()||elapsed<0. {return None;}
    let (source,label)=want.split_once('|')?;
    let source=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s))?;
    let entries=controller::entries(&source);let entry=entries.iter().find(|e|e.label==label)?;
    let layers=controller::trigger_layers(entry,name)?;
    let mut candidate=parameters.clone();
    for(index,phase) in layers {
        // A scheduled state has not entered yet. This immediate API cannot
        // preserve a separate future enable event while changing its clock.
        if parameters.get(&controller::layer_start_time_name(index)).copied().unwrap_or(0.)>elapsed {return None;}
        candidate.insert(controller::layer_start_time_name(index),elapsed);
        candidate.insert(controller::layer_start_phase_name(index),phase);
    }
    valid_animation_clock(game_root,want,&candidate).then_some(candidate)
}

pub(crate) fn studio_height_parameter(game_root:&Path,want:&str)->Option<String> {
    let (source,label)=want.split_once('|')?;
    let source=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s))?;
    let entries=controller::entries(&source);let entry=entries.iter().find(|e|e.label==label)?;
    ["height1","height"].iter().find(|name|entry.float_names.iter().any(|p|p==**name)).map(|p|p.to_string())
}

/// The Studio H controls a clip's blend trees actually vary along: (control, parameter, sample values). The controls are
/// Studio's option 1 (height), option 2 (breast) and pattern (motion), as `studio_parameters` sets them; the values are
/// the tree's own thresholds inside the sliders' 0..1, with both ends, so blending between them reproduces the tree.
pub(crate) fn studio_blend_axes(game_root:&Path,want:&str)->Vec<(&'static str,String,f32,Vec<f32>)> {
    let Some((source,label))=want.split_once('|') else {return Vec::new();};
    let Some(source)=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s)) else {return Vec::new();};
    let entries=controller::entries(&source);
    let Some(entry)=entries.iter().find(|e|e.label==label) else {return Vec::new();};
    let mut trees:Vec<&controller::MotionNode>=entry.motion.iter().chain(entry.active_layers.iter().filter_map(|l|l.motion.as_ref())).collect();
    if let Some(base)=entry.layer.as_ref().and_then(|l|entries.iter().find(|e|e.label==l.base_label)) {trees.extend(base.motion.iter());}
    let mut axes=Vec::new();
    for (control,names) in [("option1",&["height1","height"][..]),("option2",&["Breast1","Breast","breast"][..]),("pattern",&["motion"][..])] {
        let Some(p)=names.iter().find_map(|n|entry.float_defaults.iter().find(|p|p.name==*n)) else {continue;};
        let mut at=Vec::new();
        for t in &trees {t.positions_of(p.id,&mut at);}
        if at.is_empty() {continue;}
        at.extend([0.,1.]);
        let mut at:Vec<f32>=at.into_iter().filter(|v|v.is_finite()).map(|v|v.clamp(0.,1.)).collect();
        at.sort_by(f32::total_cmp);at.dedup_by(|a,b|(*a-*b).abs()<1e-4);
        axes.push((control,p.name.clone(),p.default.clamp(0.,1.),at));
    }
    axes
}

pub(crate) fn studio_parameters(game_root:&Path,want:&str,height:f32,breast:f32,is_h:bool,
    options:[f32;2],motion:Option<f32>)->Option<HashMap<String,f32>> {
    if ![height,breast,options[0],options[1]].iter().all(|v|v.is_finite()) || motion.is_some_and(|v|!v.is_finite()) {return None;}
    let (source,label)=want.split_once('|')?;
    let source=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s))?;
    if !studio_source_profile(&source) {return None;}
    let entries=controller::entries(&source);let entry=entries.iter().find(|e|e.label==label&&e.studio_layer.is_some())?;
    let find=|names:&[&str]|names.iter().find(|n|entry.float_names.iter().any(|p|p==**n)).map(|n|n.to_string());
    let mut values=HashMap::new();
    // OCIChar.LoadAnime seeds the card values, then reapplies the saved options.
    // H option setters overwrite the seeds; ordinary Studio retains card height.
    if let Some(n)=find(&["height1","height"]) {values.insert(n,if is_h {options[0]} else {height});}
    if is_h {
        if let Some(n)=find(&["Breast1","Breast","breast"]) {values.insert(n,options[1]);}
        if let Some(value)=motion {if let Some(n)=find(&["motion"]) {values.insert(n,value);}}
    }
    if !valid_animation_clock(game_root,want,&values) {return None;}
    let (runtime_source,entry,_,_)=motion_plan(game_root,want,&values)?;
    if let Some(layer)=&entry.layer {
        motion_plan(game_root,&format!("states:{runtime_source}|{}",layer.base_label),&values)?;
    }
    let duration=clip_duration_parameters(game_root,want,&values);
    (duration.is_finite()&&duration>0.).then_some(values)
}

pub(crate) fn valid_animation_clock(game_root:&Path,want:&str,parameters:&HashMap<String,f32>)->bool {
    let Some((source,label))=want.split_once('|') else {return true;};
    let Some(source)=source.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game_root,s)) else {return true;};
    let entries=controller::entries(&source);
    let Some(entry)=entries.iter().find(|e|e.label==label) else {return false;};
    let valid=|e:&controller::StateEntry| {let speed=e.parameterized_speed(parameters);speed.is_finite()&&speed>0.};
    let weight=parameters.get(controller::LAYER_WEIGHT_PARAMETER).copied().unwrap_or(1.);
    valid(entry)&&entry.active_layers.iter().all(|e| {
        let l=e.layer.as_ref().unwrap();
        valid(e)&&(0.0..=1.0).contains(&parameters.get(&controller::layer_weight_name(l.index)).copied().unwrap_or(1.))
            &&[controller::layer_start_time_name(l.index),controller::layer_start_phase_name(l.index)].iter()
                .all(|name|parameters.get(name).copied().unwrap_or(0.).is_finite())
    })
        &&entry.layer.as_ref().map(|layer|(0.0..=1.0).contains(&weight)&&entries.iter().find(|e|e.label==layer.base_label).is_some_and(valid)).unwrap_or(true)
}

fn motion_plan(game_root:&Path,want:&str,parameters:&HashMap<String,f32>) -> Option<(String,controller::StateEntry,Vec<(ClipEntry,f32,f32,f32)>,f32)> {
    let _trace=crate::native_profile::Scope::new(37);
    let (source,label)=want.split_once('|')?;
    let source=source.strip_prefix(controller::SOURCE_PREFIX)?;
    let resolved=source_path(game_root,source)?;
    let mut entry=controller::entries(&resolved).iter().find(|e|e.label==label)?.clone();
    motion_plan_entry(game_root,source,entry,parameters)
}

fn motion_plan_entry(game_root:&Path,source:&str,mut entry:controller::StateEntry,parameters:&HashMap<String,f32>)->Option<(String,controller::StateEntry,Vec<(ClipEntry,f32,f32,f32)>,f32)> {
    entry.timing.speed=entry.parameterized_speed(parameters);
    let motion=entry.motion.as_ref()?;
    let values=entry.parameters.iter().map(|p|(p.id,parameters.get(&p.name).copied().unwrap_or(p.default))).collect();
    let mut leaves=Vec::new();motion.leaves(&values,1.,&mut leaves);
    let mut duration=0.;let mut sampled=Vec::new();
    for(clip,weight,cycle,duration_factor) in leaves {
        let key=format!("{}|{}",clip.source.as_deref().unwrap_or(source),clip.label);
        let length=loaded_clip_duration(game_root,&key)?;
        if entry.layer.as_ref().is_some_and(|l|l.additive) { additive_reference_values(game_root,&key)?; }
        if !length.is_finite()||length<=0. {return None;}
        let effective_length=length*duration_factor;
        if !effective_length.is_finite()||effective_length<=0. {return None;}
        duration+=weight*effective_length;sampled.push((clip,weight,cycle,length));
    }
    if sampled.is_empty()||!duration.is_finite()||duration<=0. {return None;}
    Some((source.to_string(),entry,sampled,duration))
}

pub(crate) fn apply_clip_at_parameters(game_root:&Path,skel:&mut Skeleton,state:&str,t:f32,parameters:&HashMap<String,f32>) {
    let resolve_start=std::time::Instant::now();
    let Some((source,entry,leaves,duration))=motion_plan(game_root,state,parameters) else {
        if resolve_start.elapsed().as_millis()>10 {eprintln!("OK animation slow motion_plan_ms={:.3} state={state}",resolve_start.elapsed().as_secs_f64()*1000.);}
        apply_raw_elapsed_clip_at(game_root,skel,state,t);return;
    };
    let baseline=SkeletonSnapshot::new(skel);
    if !entry.active_layers.is_empty() {
        let mut base=entry.clone();base.active_layers.clear();
        apply_motion_pose(game_root,skel,&baseline,&baseline,&source,&base,leaves,duration,t,parameters,1.,0.,0.);
        for child in &entry.active_layers {
            let Some((_,child,leaves,duration))=motion_plan_entry(game_root,&source,child.clone(),parameters) else {return;};
            let layer=child.layer.as_ref().unwrap();
            let weight=parameters.get(&controller::layer_weight_name(layer.index)).copied().unwrap_or(1.);
            let start=if layer.synchronized {0.}else{parameters.get(&controller::layer_start_time_name(layer.index)).copied().unwrap_or(0.)};
            let phase=if layer.synchronized {0.}else{parameters.get(&controller::layer_start_phase_name(layer.index)).copied().unwrap_or(0.)};
            let accumulated=SkeletonSnapshot::new(skel);
            if t!=f32::MAX&&t<start {continue;}
            apply_motion_pose(game_root,skel,&baseline,&accumulated,&source,&child,leaves,duration,t,parameters,weight,start,phase);
        }
        return;
    }
    let mut base_pose=entry.layer.as_ref().map(|_|SkeletonSnapshot::new(&baseline));
    if let (Some(layer),Some(pose))=(&entry.layer,&mut base_pose) {
        apply_clip_at_parameters(game_root,pose,&format!("states:{source}|{}",layer.base_label),t,parameters);
    }
    let weight=parameters.get(controller::LAYER_WEIGHT_PARAMETER).copied().unwrap_or(1.);
    apply_motion_pose(game_root,skel,&baseline,base_pose.as_deref().unwrap_or(&baseline),&source,&entry,leaves,duration,t,parameters,weight,0.,0.);
}

fn apply_motion_pose(game_root:&Path,skel:&mut Skeleton,baseline:&Skeleton,base_pose:&Skeleton,source:&str,
    entry:&controller::StateEntry,leaves:Vec<(ClipEntry,f32,f32,f32)>,duration:f32,t:f32,parameters:&HashMap<String,f32>,layer_weight:f32,start_time:f32,start_phase:f32) {
    if sample_motion_pose(game_root,skel,baseline,source,entry,leaves,duration,t,parameters,start_time,start_phase) {
        compose_motion_layer(skel,base_pose,entry,layer_weight);
    }
}

fn sample_motion_pose(game_root:&Path,skel:&mut Skeleton,baseline:&Skeleton,source:&str,
    entry:&controller::StateEntry,leaves:Vec<(ClipEntry,f32,f32,f32)>,duration:f32,t:f32,parameters:&HashMap<String,f32>,start_time:f32,start_phase:f32)->bool {
    sample_motion_pose_impl(game_root,skel,baseline,source,entry,leaves,duration,t,parameters,start_time,start_phase,true)
}
fn sample_motion_pose_impl(game_root:&Path,skel:&mut Skeleton,baseline:&Skeleton,source:&str,
    entry:&controller::StateEntry,leaves:Vec<(ClipEntry,f32,f32,f32)>,duration:f32,t:f32,parameters:&HashMap<String,f32>,start_time:f32,start_phase:f32,prepared:bool)->bool {
    let (clock_speed,clock_duration,clock_cycle)=if let Some(layer)=entry.layer.as_ref().filter(|l|l.synchronized) {
        match motion_plan(game_root,&format!("states:{source}|{}",layer.base_label),parameters) {
            Some((_,base,_,duration))=>(base.timing.speed,duration,base.timing.cycle),
            None=>return false,
        }
    } else {(entry.timing.speed,duration,entry.timing.cycle)};
    let phase=layer_clock_phase(t,clock_speed,clock_duration,clock_cycle,entry.timing.looping,start_time,start_phase);
    // A single full-weight leaf still performs the same weighted arithmetic and
    // quaternion normalization as composition; only redundant rig clones vanish.
    if prepared&&leaves.len()==1&&leaves[0].1==1.&&!entry.layer.as_ref().is_some_and(|l|l.additive){
        let (clip,_,cycle,length)=&leaves[0];
        let p=if t==f32::MAX {1.}else if entry.timing.looping {(phase+cycle).rem_euclid(1.)}else{(phase+cycle).clamp(0.,1.)};
        let key=format!("{}|{}",clip.source.as_deref().unwrap_or(source),clip.label);
        skel.restore_from(baseline);apply_raw_clip_local(game_root,skel,&key,p*length,&cached_rig_bindings(baseline));
        for tf in skel.tfs.values_mut(){for i in 0..3{tf.pos[i]=0.+tf.pos[i]*1.;tf.scale[i]=0.+tf.scale[i]*1.;}tf.rot=norm_quat(tf.rot.map(|x|0.+x*1.*1.));}
        skel.rebuild_worlds();return true;
    }
    let leaves_trace=crate::native_profile::Scope::new(38);
    // Every leaf is a snapshot of `baseline` (same transforms, parents and names): its rig is matched once here.
    let rig=cached_rig_bindings(baseline);
    let mut poses=Vec::new();
    for(clip,weight,cycle,length) in leaves {
        let mut pose={let _trace=crate::native_profile::Scope::new(40);SkeletonSnapshot::new(baseline)};
        let p=if t==f32::MAX {1.} else if entry.timing.looping {(phase+cycle).rem_euclid(1.)} else {(phase+cycle).clamp(0.,1.)};
        let key=format!("{}|{}",clip.source.as_deref().unwrap_or(source),clip.label);
        {let _trace=crate::native_profile::Scope::new(41);apply_raw_clip_local(game_root,&mut pose,&key,p*length,&rig);}
        if entry.layer.as_ref().is_some_and(|l|l.additive) {
            let mut reference=SkeletonSnapshot::new(baseline);
            let Some((binds,values))=additive_reference_values(game_root,&key) else {return false;};
            apply_binding_values(game_root,&mut reference,&key,binds,values,0,0,0);
            // Generic clips without an authored reference use their first
            // sample. Compute each leaf's delta before blending the tree.
            for (&id,tf) in &mut pose.tfs {
                let r=&reference.tfs[&id];
                for i in 0..3 {tf.pos[i]-=r.pos[i];tf.scale[i]=if r.scale[i].abs()>1e-12 {tf.scale[i]/r.scale[i]}else{1.};}
                tf.rot=crate::pose::quat_mul([-r.rot[0],-r.rot[1],-r.rot[2],r.rot[3]],tf.rot);
            }
        }
        poses.push((pose,weight));
    }
    drop(leaves_trace);
    let _blend_trace=crate::native_profile::Scope::new(39);
    for(&id,tf) in &mut skel.tfs {
        let mut pos=[0.;3];let mut scale=[0.;3];let mut q=[0.;4];
        let reference=poses[0].0.tfs[&id].rot;
        for(pose,weight) in &poses {
            let child=&pose.tfs[&id];
            for i in 0..3 {pos[i]+=child.pos[i]*weight;scale[i]+=child.scale[i]*weight;}
            let sign=if reference.iter().zip(child.rot).map(|(a,b)|a*b).sum::<f32>()<0. {-1.} else {1.};
            for i in 0..4 {q[i]+=child.rot[i]*weight*sign;}
        }
        tf.pos=pos;tf.scale=scale;tf.rot=norm_quat(q);
    }
    skel.rebuild_worlds();true
}

// Masks depend on authored paths, not animated TRS. Preserve the original
// traversal and duplicate-name order; verify topology before every reuse.
struct LayerMaskPlan {
    order:Vec<(i64,Option<i64>,String)>,mask:Vec<(u32,f32)>,
    resolved:std::sync::Arc<HashMap<i64,f32>>,
}
thread_local!{static LAYER_MASK_PLANS:std::cell::RefCell<Vec<LayerMaskPlan>>=const{std::cell::RefCell::new(Vec::new())};}
fn resolved_layer_mask(skel:&Skeleton,mask:&[(u32,f32)])->std::sync::Arc<HashMap<i64,f32>> {
    LAYER_MASK_PLANS.with(|cache|{
        let mut cache=cache.borrow_mut();
        let hit=cache.iter().rposition(|p|p.mask==mask&&p.order.len()==skel.tfs.len()&&p.order.iter().zip(skel.tfs.iter()).all(|((id,parent,name),(actual,tf))|id==actual&&parent==&tf.parent&&name==&tf.name));
        if let Some(i)=hit {let plan=cache.remove(i);let result=plan.resolved.clone();cache.push(plan);return result;}
        let resolved=std::sync::Arc::new(resolve_layer_mask_uncached(skel,mask));
        if cache.len()>=16{cache.remove(0);}
        cache.push(LayerMaskPlan{order:skel.tfs.iter().map(|(id,tf)|(*id,tf.parent,tf.name.clone())).collect(),mask:mask.to_vec(),resolved:resolved.clone()});resolved
    })
}
fn resolve_layer_mask_uncached(skel:&Skeleton,mask:&[(u32,f32)])->HashMap<i64,f32> {
        let masks:HashMap<_,_>=mask.iter().copied().collect();
        let default=mask.iter().find(|(h,_)|*h==0).map(|(_,w)|*w).unwrap_or(1.);
        let mut resolved=HashMap::new();
        if let Some(root)=crate::pose::animator_root_id(skel) {
            let body=crate::pose::subtree_ids_pub(root,&skel.tfs);
            let mut children=HashMap::<i64,Vec<i64>>::new();
            for id in body {if let Some(parent)=skel.tfs[&id].parent {children.entry(parent).or_default().push(id);}}
            fn visit(id:i64,path:String,inherited:f32,skel:&Skeleton,children:&HashMap<i64,Vec<i64>>,mask:&HashMap<u32,f32>,resolved:&mut HashMap<i64,f32>) {
                let weight=mask.get(&(unity_string_to_hash(&path) as u32)).copied().unwrap_or(inherited);
                resolved.insert(id,weight);
                for &child in children.get(&id).into_iter().flatten() {
                    if !resolved.contains_key(&child) {
                        visit(child,format!("{path}/{}",skel.tfs[&child].name),weight,skel,children,mask,resolved);
                    }
                }
            }
            visit(root,skel.tfs[&root].name.clone(),default,skel,&children,&masks,&mut resolved);
        }
        resolved
}
fn compose_motion_layer(skel:&mut Skeleton,base_pose:&Skeleton,entry:&controller::StateEntry,layer_weight:f32) {
    if let Some(layer)=&entry.layer {
        let resolved=resolved_layer_mask(skel,&layer.mask);
        for (&id,tf) in &mut skel.tfs {
            let w=resolved.get(&id).copied().unwrap_or(0.)*layer_weight;let base=&base_pose.tfs[&id];
            if w==0. {tf.pos=base.pos;tf.scale=base.scale;tf.rot=base.rot;}
            else if layer.additive {
                for i in 0..3 {tf.pos[i]=base.pos[i]+tf.pos[i]*w;tf.scale[i]=base.scale[i]*(1.+(tf.scale[i]-1.)*w);}
                tf.rot=crate::pose::quat_mul(base.rot,weighted_additive_rotation(tf.rot,w));
            }
            else if w<1. {
                for i in 0..3 {tf.pos[i]=base.pos[i]*(1.-w)+tf.pos[i]*w;tf.scale[i]=base.scale[i]*(1.-w)+tf.scale[i]*w;}
                let sign=if base.rot.iter().zip(tf.rot).map(|(a,b)|a*b).sum::<f32>()<0. {-1.} else {1.};
                tf.rot=norm_quat(std::array::from_fn(|i|base.rot[i]*(1.-w)+tf.rot[i]*w*sign));
            }
        }
    }
    skel.rebuild_worlds();
}

fn layer_clock_phase(t:f32,speed:f32,duration:f32,cycle:f32,looping:bool,start:f32,initial:f32)->f32 {
    if t==f32::MAX {return 1.;}
    if start==0.&&initial==0. {
        let phase=t*speed/duration+cycle;
        return if looping {phase.rem_euclid(1.)}else{phase.clamp(0.,1.)};
    }
    // Finite f32 controls may overflow an intermediate f32 subtraction or
    // division. Reduce the normalized phase in f64 before converting back.
    let phase=(t as f64-start as f64)*speed as f64/duration as f64+initial as f64+cycle as f64;
    (if looping {phase.rem_euclid(1.)}else{phase.clamp(0.,1.)}) as f32
}

fn weighted_additive_rotation(mut q:[f32;4],weight:f32)->[f32;4] {
    // Unity's generic additive weight scales the imaginary components,
    // keeping the real component before normalization. Slerp(identity,delta)
    // and ordinary nlerp give measurably different fractional-weight poses.
    q=norm_quat(q);
    norm_quat([q[0]*weight,q[1]*weight,q[2]*weight,q[3]])
}

fn apply_raw_clip_at(game_root: &Path, skel: &mut Skeleton, state: &str, t: f32) {
    apply_raw_clip_impl(game_root,skel,state,t,false);
}

// Preview elapsed time stays monotonic for physics, triggers and diagnostics.
// Only raw preview sampling wraps; controller leaves already own their clock,
// including non-looping endpoints and independent synchronized/additive layers.
fn raw_preview_sample_time(c:&CachedClip,t:f32)->f32 {
    if c.state_timing.is_none()&&t.is_finite()&&t>=0.&&t!=f32::MAX&&c.duration.is_finite()&&c.duration>0. {
        t.rem_euclid(c.duration)
    }else{t}
}
#[cfg(test)]
#[test]
fn raw_preview_loops_without_restarting_controller_clocks() {
    let mut c=CachedClip{binds:vec![],keys:vec![],curve_index:None,streamed_n:0,
        dense:DenseSamples{count:1,frames:3,rate:2.,begin:0.25,samples:vec![0.,1.,0.]},
        constant:vec![],reference:vec![],start:0.25,duration:1.,scene:ClipScene::Studio,state_timing:None};
    for cycles in 0..12 {for phase in [0.,0.125,0.25,0.5,0.75,0.875]{
        let expected=sample_cached(&c,phase).1;
        let actual=sample_cached(&c,raw_preview_sample_time(&c,cycles as f32+phase)).1;
        assert_eq!(actual,expected,"raw preview stopped or drifted after {cycles} cycles at {phase}");
    }}
    assert_ne!(sample_cached(&c,raw_preview_sample_time(&c,3.125)).1,sample_cached(&c,raw_preview_sample_time(&c,3.5)).1);
    assert_eq!(raw_preview_sample_time(&c,f32::MAX),f32::MAX);
    c.state_timing=Some(controller::StateTiming{speed:1.7,cycle:0.2,looping:false});
    for time in [0.,1.,3.,17.,f32::MAX]{assert_eq!(raw_preview_sample_time(&c,time),time);}
    c.state_timing.as_mut().unwrap().looping=true;
    assert_eq!(raw_preview_sample_time(&c,17.),17.,"controller owns its normalized clock");
    c.state_timing=None;c.duration=0.;assert_eq!(raw_preview_sample_time(&c,17.),17.);
}
fn load_elapsed_clip_at(root:&Path,key:&str,t:f32)->Option<(Vec<Binding>,HashMap<i32,f32>,u32,u32,usize,f32)> {
    let cache_key=format!("{}\0{}",root.display(),key);
    if let Some(c)=cached_clip(&cache_key){return Some(sample_cached(&c,raw_preview_sample_time(&c,t)));}
    // Cold loads retain the existing validation/resolution path. Do not hold
    // the cache mutex across loading; resample its validated result afterward.
    let first=load_clip_at(root,key,t)?;
    Some(cached_clip(&cache_key).map(|c|sample_cached(&c,raw_preview_sample_time(&c,t))).unwrap_or(first))
}
fn apply_raw_elapsed_clip_at(game_root:&Path,skel:&mut Skeleton,state:&str,t:f32){
    apply_raw_clip_impl(game_root,skel,state,t,true);
}
fn apply_raw_clip_impl(game_root:&Path,skel:&mut Skeleton,state:&str,t:f32,elapsed:bool) {
    apply_raw_clip_worlds(game_root,skel,state,t,elapsed,true,None);
}
/// A blend tree's leaf: only the local TRS is blended (the tree rebuilds the worlds once at the end), so the leaf
/// skips its own world rebuild. Alice's one-on-one sampled four leaves a role every frame and rebuilt ~3000 worlds
/// for each (KK_NativeMotionLeaves 2.1 ms a role). OK_LEAF_WORLDS=1 keeps the old rebuild, to compare.
fn apply_raw_clip_local(game_root:&Path,skel:&mut Skeleton,state:&str,t:f32,rig:&Arc<RigBindingData>) {
    static KEEP:std::sync::OnceLock<bool>=std::sync::OnceLock::new();
    apply_raw_clip_worlds(game_root,skel,state,t,false,*KEEP.get_or_init(||std::env::var("OK_LEAF_WORLDS").ok().as_deref()==Some("1")),Some(rig));
}
fn apply_raw_clip_worlds(game_root:&Path,skel:&mut Skeleton,state:&str,t:f32,elapsed:bool,worlds:bool,rig:Option<&Arc<RigBindingData>>) {
    let start=std::time::Instant::now();
    if std::env::var_os("OK_ANIM_DEBUG").is_none() {
        let key=format!("{}\0{}",game_root.display(),state);
        if let Some(clip)=cached_clip(&key) {
            let time=if elapsed{raw_preview_sample_time(&clip,t)}else{t};
            if apply_cached_channels(game_root,skel,clip,time,worlds,rig){return;}
        }
    }
    let Some((binds, values, streamed_n, dense_n, const_n, _dur)) =
        (if elapsed {load_elapsed_clip_at(game_root,state,t)}else{load_clip_at(game_root,state,t)})
    else {
        eprintln!("ok-extract: pose clip {state} missing");
        return;
    };
    let loaded=start.elapsed();
    apply_binding_values(game_root,skel,state,binds,values,streamed_n,dense_n,const_n);
    if start.elapsed().as_millis()>10 {eprintln!("OK animation slow raw_load_ms={:.3} binding_ms={:.3} state={state}",loaded.as_secs_f64()*1000.,(start.elapsed()-loaded).as_secs_f64()*1000.);}
}

fn additive_reference_values(game_root:&Path,state:&str)->Option<(Vec<Binding>,HashMap<i32,f32>)> {
    let (binds,first,_,_,_,_)=load_clip_at(game_root,state,0.)?;
    let guard=CLIP_CACHE.lock().ok()?;
    let clip=guard.as_ref()?.get(&format!("{}\0{}",game_root.display(),state))?;
    if clip.reference.is_empty() {return Some((binds,first));}
    let count:usize=binds.iter().map(curve_dim).sum();
    if clip.reference.len()!=count||clip.reference.iter().any(|v|!v.is_finite()) {return None;}
    Some((binds,clip.reference.iter().enumerate().map(|(i,v)|(i as i32,*v)).collect()))
}

/// Exact generic Transform targets relative to the Animator GameObject.
/// Keep duplicate paths/hash collisions unresolved rather than choosing a node.
fn rig_binding_paths(skel:&Skeleton)->HashMap<u32,Option<(String,i64)>> {
    let mut paths=HashMap::new();
    let Some(root)=crate::pose::animator_root_id(skel) else {return paths};
    for id in crate::pose::subtree_ids_pub(root,&skel.tfs) {
        let mut names=Vec::new();let mut current=id;
        loop {
            let Some(node)=skel.tfs.get(&current) else {break};
            names.push(node.name.as_str());
            if current==root {
                names.reverse();let path=names.join("/");let hash=unity_string_to_hash(&path) as u32;
                paths.entry(hash).and_modify(|entry|*entry=None).or_insert(Some((path,id)));
                break;
            }
            let Some(parent)=node.parent else {break};current=parent;
            if names.len()>skel.tfs.len() {break;}
        }
    }
    paths
}

struct RigBindingData {paths:HashMap<u32,Option<(String,i64)>>,by_name:HashMap<String,i64>}
struct RigBindingSignature {order:Vec<(i64,Option<i64>,String)>,bones:Vec<i64>}
impl RigBindingSignature {
    fn capture(skel:&Skeleton)->Self{Self{order:skel.tfs.iter().map(|(id,tf)|(*id,tf.parent,tf.name.clone())).collect(),bones:skel.bone_ids.clone()}}
    fn matches(&self,skel:&Skeleton)->bool{self.bones==skel.bone_ids&&self.order.len()==skel.tfs.len()&&self.order.iter().zip(skel.tfs.iter()).all(|((id,parent,name),(other,tf))|id==other&&*parent==tf.parent&&name==&tf.name)}
}
thread_local! {static RIG_BINDING_CACHE:std::cell::RefCell<Vec<(RigBindingSignature,std::sync::Arc<RigBindingData>)>>=const {std::cell::RefCell::new(Vec::new())};}
fn rig_binding_data_uncached(skel:&Skeleton)->RigBindingData {
    let paths=rig_binding_paths(skel);let mut by_name=HashMap::new();
    if let Some(root)=crate::pose::animator_root_id(skel){for id in crate::pose::subtree_ids_pub(root,&skel.tfs){if let Some(tf)=skel.tfs.get(&id){by_name.entry(tf.name.clone()).or_insert(id);}}}
    if by_name.is_empty(){for id in &skel.bone_ids{if let Some(tf)=skel.tfs.get(id){by_name.entry(tf.name.clone()).or_insert(*id);}}}
    RigBindingData{paths,by_name}
}
fn cached_rig_bindings(skel:&Skeleton)->std::sync::Arc<RigBindingData> {
    RIG_BINDING_CACHE.with(|cache|{
        let mut entries=cache.borrow_mut();
        if let Some(index)=entries.iter().rposition(|(signature,_)|signature.matches(skel)){
            let entry=entries.remove(index);let result=entry.1.clone();entries.push(entry);return result;
        }
        let result=std::sync::Arc::new(rig_binding_data_uncached(skel));
        // Bound retained topology data for galleries and multi-character use.
        if entries.len()>=8{entries.remove(0);}
        entries.push((RigBindingSignature::capture(skel),result.clone()));result
    })
}

// Overlay rig-only helper paths without copying the entire immutable Avatar
// table per sample. Authored Avatar entries still take precedence.
struct BindingPaths<'a> {avatar:&'a HashMap<u32,String>,rig:&'a HashMap<u32,Option<(String,i64)>>}
impl BindingPaths<'_> {
    fn get(&self,hash:&u32)->Option<&String>{self.avatar.get(hash).or_else(||self.rig.get(hash).and_then(|entry|entry.as_ref().map(|(path,_)|path)))}
    fn len(&self)->usize{self.avatar.len()+self.rig.iter().filter(|(hash,path)|path.is_some()&&!self.avatar.contains_key(hash)).count()}
}

#[derive(Clone)]
struct BindingStep {id:i64,attribute:u32,curve:i32}
struct BindingPlan {
    rig:Arc<RigBindingData>,avatar:Arc<HashMap<u32,String>>,bindings:Vec<Binding>,steps:Vec<BindingStep>,
}
thread_local! {static BINDING_PLAN_CACHE:std::cell::RefCell<Vec<Arc<BindingPlan>>>=const {std::cell::RefCell::new(Vec::new())};}
fn compile_binding_plan(rig:Arc<RigBindingData>,avatar:Arc<HashMap<u32,String>>,bindings:&[Binding])->BindingPlan {
    let tos=BindingPaths{avatar:&avatar,rig:&rig.paths};
    let mut curves=Vec::with_capacity(bindings.len());let mut offset=0;
    for b in bindings {curves.push(offset);offset+=curve_dim(b) as i32;}
    let mut order:Vec<usize>=(0..bindings.len()).collect();
    order.sort_by_key(|&i|tos.get(&bindings[i].path).map(|p|!p.starts_with("cf_01/")).unwrap_or(true));
    let mut done=HashSet::new();let mut steps=Vec::new();
    for i in order {
        let b=bindings[i];if b.path==0||(b.type_id!=4&&b.type_id!=0)||!(1..=4).contains(&b.attribute){continue;}
        let Some(path)=tos.get(&b.path) else {continue;};
        let id=match rig.paths.get(&b.path){Some(Some((_,id)))=>*id,Some(None)=>continue,None=>match rig.by_name.get(bone_name(path)){Some(id)=>*id,None=>continue}};
        if done.insert((id,b.attribute)){steps.push(BindingStep{id,attribute:b.attribute,curve:curves[i]});}
    }
    BindingPlan{rig,avatar,bindings:bindings.to_vec(),steps}
}
fn binding_plan(rig:Arc<RigBindingData>,avatar:Arc<HashMap<u32,String>>,bindings:&[Binding])->Arc<BindingPlan> {
    BINDING_PLAN_CACHE.with(|cache|{
        let mut entries=cache.borrow_mut();
        if let Some(i)=entries.iter().position(|p|Arc::ptr_eq(&p.rig,&rig)&&Arc::ptr_eq(&p.avatar,&avatar)&&p.bindings==bindings){let p=entries.remove(i);entries.push(p.clone());return p;}
        let plan=Arc::new(compile_binding_plan(rig,avatar,bindings));
        // Retain topology/avatar ownership to prevent pointer reuse; bound memory.
        if entries.len()>=32{entries.remove(0);}entries.push(plan.clone());plan
    })
}
fn execute_binding_plan(skel:&mut Skeleton,steps:&[BindingStep],values:&HashMap<i32,f32>) {
    for step in steps {
        let Some(tf)=skel.tfs.get_mut(&step.id) else {continue;};
        let c=[curve_at(values,step.curve),curve_at(values,step.curve+1),curve_at(values,step.curve+2),curve_at(values,step.curve+3)];
        match step.attribute {
            1=>tf.pos=[c[0],c[1],c[2]],
            2=>tf.rot=norm_quat(c),
            3=>{let scale=[c[0],c[1],c[2]];if scale.iter().all(|s|s.is_finite()&&s.abs()>1e-6){tf.scale=scale;}},
            4=>tf.rot=euler_deg_to_quat([c[0],c[1],c[2]]),
            _=>{},
        }
    }
    skel.rebuild_worlds();
}

#[derive(Clone,Copy)]
enum SampleChannel { Missing, Constant(usize), Dense(usize), Streamed(usize) }
struct ChannelPlan { clip:Arc<CachedClip>, binding:Arc<BindingPlan>, channels:Vec<[SampleChannel;4]> }
thread_local! {static CHANNEL_PLANS:std::cell::RefCell<Vec<Arc<ChannelPlan>>>=const{std::cell::RefCell::new(Vec::new())};}
fn channel_address(c:&CachedClip,index:i32)->SampleChannel {
    let constant_base=c.streamed_n as i32+c.dense.count as i32;
    if index>=constant_base && ((index-constant_base) as usize)<c.constant.len(){return SampleChannel::Constant((index-constant_base) as usize);}
    if c.dense.frames>0 && !c.dense.samples.is_empty() && index>=c.streamed_n as i32 && index<constant_base{return SampleChannel::Dense((index-c.streamed_n as i32) as usize);}
    match c.curve_index.as_ref().and_then(|curves|curves.binary_search_by_key(&index,|(id,_)|*id).ok()) {Some(i)=>SampleChannel::Streamed(i),None=>SampleChannel::Missing}
}
fn sample_channel(c:&CachedClip,channel:SampleChannel,time:f32,dense_frame:(usize,usize,f32))->f32 {
    match channel {
        SampleChannel::Missing=>0.,
        SampleChannel::Constant(i)=>c.constant[i],
        SampleChannel::Dense(i)=>{let(first,second,blend)=dense_frame;let count=c.dense.count as usize;
            if c.dense.frames<=1{c.dense.samples[i]}else{c.dense.samples[first*count+i]*(1.-blend)+c.dense.samples[second*count+i]*blend}},
        SampleChannel::Streamed(i)=>{let offsets=&c.curve_index.as_ref().unwrap()[i].1;let split=offsets.partition_point(|&i|c.keys[i].time<=time);
            if split==0{return c.keys[offsets[0]].coeff[3];}
            let prev=c.keys[offsets[split-1]];if split==offsets.len(){return prev.coeff[3];}
            let rhs=c.keys[offsets[split]];let dt=(time-prev.time).clamp(0.,(rhs.time-prev.time).max(0.));let[a,b,slope,origin]=prev.coeff;
            if prev.coeff.iter().all(|x|x.is_finite()){((a*dt+b)*dt+slope)*dt+origin}
            else{let alpha=(dt/(rhs.time-prev.time)).clamp(0.,1.);origin*(1.-alpha)+rhs.coeff[3]*alpha}
        }
    }
}
fn channel_dense_frame(c:&CachedClip,time:f32)->(usize,usize,f32){
    if c.dense.frames<=1{return(0,0,0.);}
    let frames=c.dense.frames;
    let frame=if c.dense.rate.is_finite()&&c.dense.rate>0.&&c.dense.begin.is_finite(){((time-c.dense.begin)*c.dense.rate).clamp(0.,(frames-1) as f32)}else{(time/c.duration.max(0.001)).clamp(0.,1.)*(frames-1) as f32};
    let first=(frame.floor() as usize).min(frames-1);(first,(first+1).min(frames-1),frame-first as f32)
}
#[cfg(test)]
#[test]
fn direct_channels_match_full_sampler_boundaries_and_overrides(){
    let keys=vec![StreamedKey{time:0.,index:-1,coeff:[0.,0.,0.,7.]},StreamedKey{time:0.,index:0,coeff:[0.2,-0.3,0.4,0.5]},StreamedKey{time:0.,index:2,coeff:[0.,0.,0.,99.]},StreamedKey{time:0.,index:5,coeff:[0.,0.,0.,98.]},StreamedKey{time:0.5,index:0,coeff:[0.,0.,0.,1.]},StreamedKey{time:0.5,index:0,coeff:[f32::INFINITY,0.,0.,2.]},StreamedKey{time:1.,index:0,coeff:[0.,0.,0.,3.]}];
    let mut c=CachedClip{binds:vec![],curve_index:streamed_curve_index(&keys),keys,streamed_n:2,dense:DenseSamples{count:3,frames:3,rate:2.,begin:0.25,samples:vec![0.,1.,2.,3.,4.,5.,6.,7.,8.]},constant:vec![42.,43.],reference:vec![],start:0.125,duration:1.25,scene:ClipScene::Studio,state_timing:None};
    for timing in [None,Some(controller::StateTiming{speed:1.7,cycle:0.2,looping:false}),Some(controller::StateTiming{speed:1.7,cycle:0.2,looping:true})]{c.state_timing=timing;
        for rate in [2.,0.] {c.dense.rate=rate;for t in [-2.,0.,0.125,0.25,0.375,0.5,0.875,1.,2.,17.,f32::MAX]{
            let full=sample_cached(&c,t).1;let time=c.start+c.state_timing.map(|clock|clock.clip_time(t,c.duration)).unwrap_or(t);let frame=channel_dense_frame(&c,time);
            for i in -2..10{assert_eq!(sample_channel(&c,channel_address(&c,i),time,frame).to_bits(),curve_at(&full,i).to_bits(),"t={t} channel={i}");}
        }}
    }
}
fn apply_cached_channels(root:&Path,skel:&mut Skeleton,clip:Arc<CachedClip>,t:f32,worlds:bool,rig:Option<&Arc<RigBindingData>>)->bool {
    // Unordered/malformed keys retain the legacy scan. Addresses own both the
    // decoded clip and verified rig/avatar plan, preventing pointer reuse.
    if clip.curve_index.is_none() || clip.dense.samples.len()<(clip.dense.count as usize).saturating_mul(clip.dense.frames){return false;}
    let lookup_trace=crate::native_profile::Scope::new(42);
    // The rig's binding paths: given by a blend tree for its leaves (copies of one baseline, same topology), else
    // found by matching this skeleton (every transform's id, parent and name: 0.9 ms a call on Alice's rig).
    let rig=rig.cloned().unwrap_or_else(||cached_rig_bindings(skel));
    let binding=binding_plan(rig,load_avatar_tos_shared(root),&clip.binds);
    let plan=CHANNEL_PLANS.with(|cache|{let mut cache=cache.borrow_mut();
        if let Some(i)=cache.iter().rposition(|p|Arc::ptr_eq(&p.clip,&clip)&&Arc::ptr_eq(&p.binding,&binding)){let p=cache.remove(i);cache.push(p.clone());return p;}
        let channels=binding.steps.iter().map(|s|std::array::from_fn(|i|channel_address(&clip,s.curve+i as i32))).collect();
        let p=Arc::new(ChannelPlan{clip,binding,channels});if cache.len()>=32{cache.remove(0);}cache.push(p.clone());p
    });
    let c=&plan.clip;let time=c.start+c.state_timing.map(|clock|clock.clip_time(t,c.duration)).unwrap_or(t);let dense_frame=channel_dense_frame(c,time);
    drop(lookup_trace);let _sample_trace=crate::native_profile::Scope::new(43);
    for(step,channels)in plan.binding.steps.iter().zip(&plan.channels){
        let Some(tf)=skel.tfs.get_mut(&step.id)else{continue;};let values=channels.map(|channel|sample_channel(c,channel,time,dense_frame));
        match step.attribute{1=>tf.pos=[values[0],values[1],values[2]],2=>tf.rot=norm_quat(values),3=>{let scale=[values[0],values[1],values[2]];if scale.iter().all(|s|s.is_finite()&&s.abs()>1e-6){tf.scale=scale;}},4=>tf.rot=euler_deg_to_quat([values[0],values[1],values[2]]),_=>{}}
    }
    if worlds{skel.rebuild_worlds();}true
}
fn apply_binding_values(game_root:&Path,skel:&mut Skeleton,state:&str,binds:Vec<Binding>,values:HashMap<i32,f32>,streamed_n:u32,dense_n:u32,const_n:usize) {
    if std::env::var_os("OK_ANIM_DEBUG").is_some(){apply_binding_values_legacy(game_root,skel,state,binds,values,streamed_n,dense_n,const_n);return;}
    let rig=cached_rig_bindings(skel);let avatar=load_avatar_tos_shared(game_root);
    let plan=binding_plan(rig,avatar,&binds);
    execute_binding_plan(skel,&plan.steps,&values);
}
fn apply_binding_values_legacy(game_root:&Path,skel:&mut Skeleton,state:&str,binds:Vec<Binding>,values:HashMap<i32,f32>,streamed_n:u32,dense_n:u32,const_n:usize) {
    let _ = &state;
    let avatar = load_avatar_tos_shared(game_root);
    // Only immutable names/hierarchy are cached. Local/world transforms and
    // source shape/scene modifiers are still evaluated for the current frame.
    let binding=cached_rig_bindings(skel);let rig_paths=&binding.paths;let by_name=&binding.by_name;
    let tos=BindingPaths{avatar:&avatar,rig:rig_paths};
    let mut applied = 0usize;
    let mut matched = 0usize;
    {
        let mut tos_cfj: std::collections::HashSet<String> = std::collections::HashSet::new();
        for b in &binds {
            if let Some(p) = tos.get(&b.path) {
                let n = bone_name(p).to_ascii_lowercase();
                if n.starts_with("cf_j_") {
                    tos_cfj.insert(n);
                }
            }
        }
        let by_lower: std::collections::HashSet<String> =
            by_name.keys().map(|k| k.to_ascii_lowercase()).collect();
        let in_by = tos_cfj.iter().filter(|n| by_lower.contains(*n)).count();
        let missing: Vec<&String> = tos_cfj.iter().filter(|n| !by_lower.contains(*n)).take(12).collect();
        eprintln!(
            "ok-extract: pose {state} cf_j_names_in_clip={} in_by_name={in_by} missing_sample={missing:?}",
            tos_cfj.len()
        );
    }
    let mut attr_hist: HashMap<u32, usize> = HashMap::new();
    let mut done: HashMap<(i64, u32), bool> = HashMap::new();
    let first_attrs: Vec<u32> = binds.iter().take(12).map(|b| b.attribute).collect();
    let first_names: Vec<String> = binds
        .iter()
        .take(12)
        .map(|b| {
            tos.get(&b.path)
                .map(|p| bone_name(p).to_string())
                .unwrap_or_else(|| format!("?{}", b.path))
        })
        .collect();
    let _ = (&first_attrs, &first_names);
    if std::env::var("OK_ANIM_DEBUG").is_ok() {
        let sample: Vec<String> = binds
            .iter()
            .filter_map(|b| tos.get(&b.path).map(|p| bone_name(p).to_string()))
            .take(30)
            .collect();
        eprintln!(
            "ok-extract: ANIMDBG tos={} by_name={} cf_j_in_tree={} sample={:?}",
            tos.len(),
            by_name.len(),
            by_name
                .keys()
                .filter(|k| k.to_ascii_lowercase().starts_with("cf_j_"))
                .count(),
            sample
        );
        for w in ["cf_j_arm00_L", "cf_j_shoulder00_L", "cf_j_hips", "cf_j_leg00_L"] {
            eprintln!(
                "ok-extract: ANIMDBG by_name has {w}: {}",
                by_name.contains_key(w)
            );
        }
        for w in ["cf_j_arm00_L", "cf_s_hand_L", "cf_s_shoulder02_L"] {
            if let Some(&start) = by_name.get(w) {
                let mut cur = start;
                let mut chain = Vec::new();
                for _ in 0..40 {
                    let Some(tf) = skel.tfs.get(&cur) else { break };
                    chain.push(tf.name.clone());
                    match tf.parent {
                        Some(p) if p != 0 && p != cur => cur = p,
                        _ => break,
                    }
                }
                eprintln!("ok-extract: ANIMDBG chain {w}: {chain:?}");
            }
            let mut found = Vec::new();
            for (id, tf) in &skel.tfs {
                if tf.name == w {
                    found.push(*id);
                }
            }
            eprintln!("ok-extract: ANIMDBG ids for {w}: {found:?}");
        }
        // SMR-remapped hierarchy actually used for skinning.
        for w in ["cf_s_hand_L", "cf_s_shoulder02_L"] {
            if let Some(rid) = skel
                .bone_ids
                .iter()
                .find(|id| skel.tfs.get(id).map(|t| t.name.as_str()) == Some(w))
                .copied()
            {
                let mut cur = rid;
                let mut chain = Vec::new();
                for _ in 0..40 {
                    let Some(tf) = skel.tfs.get(&cur) else { break };
                    chain.push(tf.name.clone());
                    match tf.parent {
                        Some(p) if p != 0 && p != cur => cur = p,
                        _ => break,
                    }
                }
                eprintln!("ok-extract: ANIMDBG SMRchain {w} id={rid}: {chain:?}");
            }
        }
        let tos_sample: Vec<(u32, String)> = binds
            .iter()
            .take(20)
            .filter_map(|b| tos.get(&b.path).map(|p| (b.path, p.clone())))
            .collect();
        eprintln!("ok-extract: ANIMDBG tos_paths={tos_sample:?}");
    }
    // Prefer cf_body_00Avatar paths (cf_01/ prefix) when the same bone appears twice.
    let mut order: Vec<usize> = (0..binds.len()).collect();
    order.sort_by_key(|&i| {
        let prefer = tos
            .get(&binds[i].path)
            .map(|p| !p.starts_with("cf_01/"))
            .unwrap_or(true);
        prefer as u8
    });
    let mut curve_of = vec![0i32; binds.len()];
    let mut curves_used = 0i32;
    {
        let mut c = 0i32;
        for (i, b) in binds.iter().enumerate() {
            curve_of[i] = c;
            c += curve_dim(b) as i32;
        }
        curves_used = c;
    }
    for i in order {
        let b = binds[i];
        *attr_hist.entry(b.attribute).or_insert(0) += 1;
        let comps = [
            curve_at(&values, curve_of[i]),
            curve_at(&values, curve_of[i] + 1),
            curve_at(&values, curve_of[i] + 2),
            curve_at(&values, curve_of[i] + 3),
        ];
        if b.path == 0 {
            continue;
        }
        let Some(path) = tos.get(&b.path) else {
            continue;
        };
        matched += 1;
        let name = bone_name(path).to_string();
        let nl = name.to_ascii_lowercase();
        if (b.type_id != 4 && b.type_id != 0) || !(1..=4).contains(&b.attribute) {
            continue;
        }
        if std::env::var("OK_ANIM_DEBUG").is_ok()
            && (nl.contains("arm00") || nl.contains("shoulder") || nl.contains("hips")
                || nl == "cf_j_neck" || nl == "cf_j_head")
        {
            eprintln!(
                "ok-extract: ANIMDBG bind {name} attr={} curve_of={} comps={:?} by_name={}",
                b.attribute,
                curve_of[i],
                comps,
                by_name.contains_key(&name)
            );
        }
        let id=match rig_paths.get(&b.path) {
            Some(Some((_,id)))=>*id,
            Some(None)=>continue,
            None=>match by_name.get(&name) {Some(id)=>*id,None=>continue},
        };
        if done.contains_key(&(id,b.attribute)) {continue;}
        let Some(tf) = skel.tfs.get_mut(&id) else { continue };
        let old_rot = tf.rot;
        match b.attribute {
            1 => {
                tf.pos = [comps[0], comps[1], comps[2]];
                applied += 1;
            }
            2 => {
                tf.rot = norm_quat([comps[0], comps[1], comps[2], comps[3]]);
                applied += 1;
            }
            3 => {
                let scale = [comps[0], comps[1], comps[2]];
                if scale.iter().all(|s| s.is_finite() && s.abs() > 1e-6) {
                    tf.scale = scale;
                    applied += 1;
                }
            }
            4 => {
                tf.rot = euler_deg_to_quat([comps[0], comps[1], comps[2]]);
                applied += 1;
            }
            _ => {}
        }
        if std::env::var("OK_ANIM_DEBUG").is_ok() && nl.contains("arm00_l") {
            eprintln!(
                "ok-extract: ANIMDBG SET {name} id={id} attr={} old={old_rot:?} new={:?}",
                b.attribute, tf.rot
            );
        }
        done.insert((id, b.attribute), true);
    }
    skel.rebuild_worlds();
    if std::env::var("OK_ANIM_DEBUG").is_ok() {
        for n in ["cf_j_arm00_L", "cf_j_hand_L", "cf_s_hand_L", "cf_s_shoulder02_L"] {
            if let Some(m) = skel.world_tf(n) {
                eprintln!(
                    "ok-extract: ANIMDBG world {n} pos={:?}",
                    [m[0][3], m[1][3], m[2][3]]
                );
            }
        }
    }
    eprintln!(
        "ok-extract: pose {} bindings={} streamed={} dense={} const={} tos={} matched={} applied={} attr={attr_hist:?} curves_used={}",
        state,
        binds.len(),
        streamed_n,
        dense_n,
        const_n,
        tos.len(),
        matched,
        applied,
        curves_used
    );
    if applied == 0 {
        let sample_bind: Vec<u32> = binds.iter().map(|b| b.path).take(8).collect();
        let sample_tos: Vec<(u32, String)> = sample_bind
            .iter()
            .filter_map(|p| tos.get(p).map(|s| (*p, s.clone())))
            .collect();
        eprintln!("ok-extract: bind paths={sample_bind:?} tos={sample_tos:?}");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::unity::meshes_and_skeleton_from_path;
    use std::path::PathBuf;

    #[test]
    fn resident_layer_masks_match_traversal_and_invalidate_topology() {
        use crate::pose::{Tf,ident};
        let mut skel=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:0,tfs:HashMap::new().into(),bone_ids:vec![],source_mesh:7};
        for(id,name,parent)in [(1,"cf_j_root",None),(2,"arm",Some(1)),(3,"hand",Some(2)),(4,"hand",Some(1)),(5,"detached",None)]{
            skel.tfs.insert(id,Tf{name:name.into(),parent,sibling_index:None,pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]});
            skel.names.push(name.into());skel.bone_ids.push(id);skel.worlds.push(ident());skel.rest.push(ident());
        }
        let mask=vec![(0,0.25),(unity_string_to_hash("cf_j_root/arm")as u32,0.75),(unity_string_to_hash("cf_j_root/arm/hand")as u32,0.)];
        let first=resolved_layer_mask(&skel,&mask);
        assert_eq!(*first,resolve_layer_mask_uncached(&skel,&mask));
        assert_eq!(first[&3],0.);assert!(!first.contains_key(&5));
        for _ in 0..32{skel.tfs.get_mut(&2).unwrap().pos[0]+=0.1;let next=resolved_layer_mask(&skel,&mask);assert!(std::sync::Arc::ptr_eq(&first,&next));assert_eq!(*next,resolve_layer_mask_uncached(&skel,&mask));}
        skel.tfs.get_mut(&3).unwrap().parent=Some(1);
        let reparented=resolved_layer_mask(&skel,&mask);assert!(!std::sync::Arc::ptr_eq(&first,&reparented));assert_eq!(*reparented,resolve_layer_mask_uncached(&skel,&mask));assert_eq!(reparented[&3],0.25);
        skel.tfs.get_mut(&2).unwrap().name="renamed".into();
        assert_eq!(*resolved_layer_mask(&skel,&mask),resolve_layer_mask_uncached(&skel,&mask));
        let mut changed=mask.clone();changed[0].1=0.5;
        assert_eq!(*resolved_layer_mask(&skel,&changed),resolve_layer_mask_uncached(&skel,&changed));
        for i in 0..24{let m=vec![(0,i as f32/24.)];assert_eq!(*resolved_layer_mask(&skel,&m),resolve_layer_mask_uncached(&skel,&m));}
        LAYER_MASK_PLANS.with(|cache|assert!(cache.borrow().len()<=16));
    }

    #[test]
    fn mannequin_clip_applies_to_body_skel() {
        let game = crate::test_support::game_path(r"");
        if !game.join("abdata").ok_is_dir() {
            return;
        }
        let body = game.join("abdata/chara/oo_base.unity3d");
        let (_, skel) = meshes_and_skeleton_from_path(&body);
        let mut skel = skel.expect("body skel");
        let source = skel.source_mesh;
        let rest_arm = skel
            .world("cf_j_arm00_L")
            .or_else(|| skel.world_tf("cf_j_arm00_L"))
            .expect("arm rest");
        let meshes = crate::unity::meshes_from_bundle_path(&body);
        if std::env::var("OK_ANIM_DEBUG").is_ok() {
            for m in &meshes {
                if m.name.eq_ignore_ascii_case("o_body_a") {
                    eprintln!(
                        "ok-extract: ANIMDBG variant path_id={} verts={} binds={}",
                        m.path_id,
                        m.positions.len(),
                        m.bind_poses.len()
                    );
                }
            }
            eprintln!(
                "ok-extract: ANIMDBG source={source} chosen path_id={} verts={} binds={}",
                meshes
                    .iter()
                    .find(|m| m.path_id == source)
                    .map(|m| m.path_id)
                    .unwrap_or(-1),
                meshes
                    .iter()
                    .find(|m| m.path_id == source)
                    .map(|m| m.positions.len())
                    .unwrap_or(0),
                meshes
                    .iter()
                    .find(|m| m.path_id == source)
                    .map(|m| m.bind_poses.len())
                    .unwrap_or(0),
            );
        }
        let Some(mut mesh) = meshes
            .iter()
            .find(|m| m.path_id == source)
            .cloned()
            .or_else(|| crate::unity::pick_named(&meshes, "o_body_a").cloned())
        else {
            panic!("no o_body_a");
        };
        crate::pose::assigned_weights_remap(&mut skel);
        apply_mannequin_pose(&game, &mut skel);
        let smr_arms: Vec<&str> = skel
            .names
            .iter()
            .filter(|n| n.contains("arm00") || n.contains("hand") || n.contains("shoulder"))
            .map(|s| s.as_str())
            .collect();
        eprintln!("SMR arm-like names={smr_arms:?}");
        let arm = skel
            .world("cf_j_arm00_L")
            .or_else(|| skel.world("cf_j_arm00_R"))
            .or_else(|| skel.world_tf("cf_j_arm00_L"))
            .expect("no arm bone");
        eprintln!("arm rest[0]={:?} posed[0]={:?}", rest_arm[0], arm[0]);
        let dx = (arm[0][3] - rest_arm[0][3]).abs();
        let drot = (arm[0][0] - rest_arm[0][0]).abs() + (arm[1][0] - rest_arm[1][0]).abs();
        if dx < 0.01 && drot < 0.02 {
            eprintln!("warning: SMR/world arm did not move, continuing to skin check");
        }
        crate::pose::repair_weights(&mut mesh, &skel);
        let n = if !mesh.bind_poses.is_empty() {
            mesh.bind_poses.len().min(skel.worlds.len())
        } else {
            skel.worlds.len().min(skel.rest.len())
        };
        let binds = if mesh.bind_poses.len() >= n {
            mesh.bind_poses[..n].to_vec()
        } else {
            crate::pose::binds_from_rest(&skel.rest[..n])
        };
        let pos = crate::pose::repair_posed_spikes(&mut mesh, &skel.worlds[..n], &binds).expect("skin");
        {
            let vd = |a: [f32; 3], b: [f32; 3]| ((a[0]-b[0]).powi(2)+(a[1]-b[1]).powi(2)+(a[2]-b[2]).powi(2)).sqrt();
            let mut maxe = 0f32;
            let mut worst = (0usize, 0usize);
            for tri in mesh.indices.chunks(3) {
                let (a,b,c)=(tri[0] as usize,tri[1] as usize,tri[2] as usize);
                for &(u,v) in &[(a,b),(b,c),(c,a)] {
                    if u>=pos.len()||v>=pos.len(){continue;}
                    let d=vd(pos[u],pos[v]);
                    if d>maxe { maxe=d; worst=(u,v); }
                }
            }
            let desc = |i: usize| -> String {
                let b=mesh.bone_idx[i]; let w=mesh.weights[i];
                (0..4).filter(|&k| w[k]>1e-4).map(|k| {
                    let bi=b[k] as usize;
                    format!("[{bi}]{}@w{:.2} restworld={:?}", skel.names.get(bi).cloned().unwrap_or_default(), w[k],
                        skel.rest.get(bi).map(|m|[m[0][3],m[1][3],m[2][3]]).unwrap_or([0.0;3]))
                }).collect::<Vec<_>>().join(",")
            };
            eprintln!("ok-extract: ANIMDBG maxedge={maxe:.3} v{} [{}] v{} [{}]", worst.0, desc(worst.0), worst.1, desc(worst.1));
        }
        let _ = &pos;
        let (mn, mx) = crate::pose::aabb(&pos);
        {
            let mut idxs: Vec<usize> = (0..mesh.positions.len()).collect();
            idxs.sort_by(|&a, &b| {
                mesh.positions[b][0]
                    .abs()
                    .partial_cmp(&mesh.positions[a][0].abs())
                    .unwrap()
            });
            for &i in idxs.iter().take(4) {
                let b = mesh.bone_idx[i];
                let w = mesh.weights[i];
                let names: Vec<String> = (0..4)
                    .filter(|&k| w[k] > 1e-4)
                    .map(|k| {
                        let bi = b[k] as usize;
                        let nm = skel.names.get(bi).cloned().unwrap_or_default();
                        let wp = skel
                            .worlds
                            .get(bi)
                            .map(|m| [m[0][3], m[1][3], m[2][3]])
                            .unwrap_or([0.0; 3]);
                        format!("[{bi}]{nm}@{wp:?}")
                    })
                    .collect();
                eprintln!(
                    "ok-extract: ANIMDBG vtx raw={:?} skinned={:?} w={:?} bones={names:?}",
                    mesh.positions[i], pos[i], w
                );
            }
            let mut zidx: Vec<usize> = (0..mesh.positions.len()).collect();
            zidx.sort_by(|&a, &b| pos[b][2].abs().partial_cmp(&pos[a][2].abs()).unwrap());
            for &i in zidx.iter().take(3) {
                let b = mesh.bone_idx[i];
                let w = mesh.weights[i];
                let names: Vec<String> = (0..4)
                    .filter(|&k| w[k] > 1e-4)
                    .map(|k| {
                        let bi = b[k] as usize;
                        let nm = skel.names.get(bi).cloned().unwrap_or_default();
                        let wp = skel
                            .worlds
                            .get(bi)
                            .map(|m| [m[0][3], m[1][3], m[2][3]])
                            .unwrap_or([0.0; 3]);
                        format!("[{bi}]{nm}@{wp:?}")
                    })
                    .collect();
                eprintln!(
                    "ok-extract: ANIMDBG zmax raw={:?} skinned={:?} w={:?} bones={names:?}",
                    mesh.positions[i], pos[i], w
                );
            }
            if let Some(rest_pos) =
                crate::pose::skin_positions(&mesh, &skel.rest[..n], &binds)
            {
                let mut bad = 0usize;
                let mut worst = (0.0f32, String::new());
                for i in 0..mesh.positions.len() {
                    let b = mesh.bone_idx[i];
                    let w = mesh.weights[i];
                    let mut best = 0.0f32;
                    let mut bi = 0usize;
                    for k in 0..4 {
                        if w[k] > best {
                            best = w[k];
                            bi = b[k] as usize;
                        }
                    }
                    let bp = skel
                        .rest
                        .get(bi)
                        .map(|m| [m[0][3], m[1][3], m[2][3]])
                        .unwrap_or([0.0; 3]);
                    let d = ((rest_pos[i][0] - bp[0]).powi(2)
                        + (rest_pos[i][1] - bp[1]).powi(2)
                        + (rest_pos[i][2] - bp[2]).powi(2))
                    .sqrt();
                    if d > 0.4 {
                        bad += 1;
                    }
                    if d > worst.0 {
                        worst = (
                            d,
                            format!(
                                "v{i} bone[{}]{} restpos={:?} rest_skin={:?}",
                                bi,
                                skel.names.get(bi).cloned().unwrap_or_default(),
                                bp,
                                rest_pos[i]
                            ),
                        );
                    }
                }
                eprintln!(
                    "ok-extract: ANIMDBG weight-outliers(>0.4)={bad} worst={:.3} {}",
                    worst.0, worst.1
                );
            }
        }
        if let Some(rp) = crate::pose::skin_positions(&mesh, &skel.rest[..n], &binds) {
            let (rmn, rmx) = crate::pose::aabb(&rp);
            eprintln!(
                "skinned rest aabb x=[{:.3},{:.3}] y=[{:.3},{:.3}] z=[{:.3},{:.3}]",
                rmn[0], rmx[0], rmn[1], rmx[1], rmn[2], rmx[2]
            );
        }
        eprintln!(
            "skinned mannequin aabb x=[{:.3},{:.3}] y=[{:.3},{:.3}] z=[{:.3},{:.3}] bind_poses={}",
            mn[0], mx[0], mn[1], mx[1], mn[2], mx[2],
            mesh.bind_poses.len()
        );
        crate::pose::rebuild_obj(&mut mesh, pos);
        let xspan = mx[0] - mn[0];
        let zspan = mx[2] - mn[2];
        eprintln!("skinned xspan={xspan:.3} zspan={zspan:.3}");
        assert!(xspan > 0.35, "body collapsed xspan={xspan:.3}");
        assert!(zspan < 0.85, "skinned mesh exploded zspan={zspan:.3}");
        assert!(mn[1] > -0.25, "feet should sit near ground y=[{:.3},{:.3}]", mn[1], mx[1]);
        assert!(
            (mn[0] + 0.744).abs() > 0.02 || (mx[0] - 0.744).abs() > 0.02 || zspan > 0.3,
            "mesh still rest T-pose x=[{:.3},{:.3}] zspan={zspan:.3}",
            mn[0],
            mx[0]
        );
        let _ = (dx, drot);
    }

    #[test]
    fn probe_custompose_controller() {
        use unity_asset_core::UnityValue;
        let game = crate::test_support::game_path(r"");
        let path = game.join("abdata/custom/custompose.unity3d");
        let Ok(bundle) = load_bundle(&path) else { return };
        let _ = bundle.data_checked();
        let pptr = |v: &UnityValue| -> Option<i64> {
            match v {
                UnityValue::Object(o) => o
                    .get("m_PathID")
                    .or_else(|| o.get("m_PathId"))
                    .and_then(|x| x.as_i64()),
                UnityValue::Integer(i) => Some(*i),
                _ => None,
            }
        };
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(ser) = parse_serialized_file(fb) else { continue };
            // clip path_id -> name
            let mut clip_name: HashMap<i64, String> = HashMap::new();
            for info in &ser.objects {
                if info.type_id == 74 {
                    if let Ok(o) = UnityObject::from_serialized_file(&ser, info) {
                        clip_name.insert(info.path_id, o.name().unwrap_or_default());
                    }
                }
            }
            for info in &ser.objects {
                let Ok(o) = UnityObject::from_serialized_file(&ser, info) else { continue };
                let nm = o.name().unwrap_or_default();
                if info.type_id == 91 || nm.eq_ignore_ascii_case("custom_pose") {
                    eprintln!("CTRL type={} name={nm} pid={}", info.type_id, info.path_id);
                    let props = o.class.properties();
                    for (k, _) in props.iter() {
                        eprintln!("CTRL   key={k}");
                    }
                    {
                        fn crc32(s: &str) -> u32 {
                            let mut c = 0xFFFF_FFFFu32;
                            for b in s.bytes() {
                                c ^= b as u32;
                                for _ in 0..8 {
                                    c = if c & 1 != 0 { (c >> 1) ^ 0xEDB8_8320 } else { c >> 1 };
                                }
                            }
                            c ^ 0xFFFF_FFFF
                        }
                        for s in ["Stand_D_00", "Base Layer.Stand_D_00"] {
                            eprintln!(
                                "CTRL crc32({s}) = {} ({})",
                                crc32(s),
                                crc32(s) as i32
                            );
                        }
                    }
                    if let Some(ctrl) = props.get("m_Controller") {
                        let obj_get = |v: &UnityValue, k: &str| -> Option<UnityValue> {
                            match v {
                                UnityValue::Object(o) => o.get(k).cloned(),
                                _ => None,
                            }
                        };
                        if let Some(UnityValue::Array(layers)) = obj_get(ctrl, "m_LayerArray") {
                            eprintln!("CTRL layers={}", layers.len());
                        }
                        if let Some(UnityValue::Array(sms)) = obj_get(ctrl, "m_StateMachineArray") {
                            eprintln!("CTRL stateMachines={}", sms.len());
                            for (si, sm) in sms.iter().enumerate() {
                                let sm: &UnityValue = match sm {
                                    UnityValue::Object(o) => {
                                        o.get("data").unwrap_or(sm)
                                    }
                                    _ => sm,
                                };
                                let keys: Vec<&str> = match sm {
                                    UnityValue::Object(o) => o.keys().map(|s| s.as_str()).collect(),
                                    _ => vec![],
                                };
                                eprintln!("CTRL sm{si} keys={keys:?}");
                                for stkey in [
                                    "m_StateArray",
                                    "m_States",
                                    "m_StateConstantArray",
                                    "m_ChildStateMachineArray",
                                ] {
                                    if let Some(UnityValue::Array(states)) = obj_get(sm, stkey) {
                                        eprintln!("CTRL sm{si}.{stkey} len={}", states.len());
                                        if let Some(first) = states.first() {
                                            let f: &UnityValue = match first {
                                                UnityValue::Object(o) => {
                                                    o.get("data").unwrap_or(first)
                                                }
                                                _ => first,
                                            };
                                            match f {
                                                UnityValue::Object(o) => {
                                                    let mut ks: Vec<String> = Vec::new();
                                                    for (kk, vv) in o.iter() {
                                                        let dv = match vv {
                                                            UnityValue::Object(oo) => format!(
                                                                "obj keys={:?}",
                                                                oo.keys().collect::<Vec<_>>()
                                                            ),
                                                            other => format!("{other:?}"),
                                                        };
                                                        ks.push(format!("{kk}={dv}"));
                                                    }
                                                    eprintln!(
                                                        "CTRL sm{si}.{stkey}[0] {}",
                                                        ks.join(", ")
                                                    );
                                                }
                                                other => {
                                                    eprintln!("CTRL sm{si}.{stkey}[0]={other:?}")
                                                }
                                            }
                                        }
                                        for st in states.iter() {
                                            let st: &UnityValue = match st {
                                                UnityValue::Object(o) => o.get("data").unwrap_or(st),
                                                _ => st,
                                            };
                                            let name = obj_get(st, "m_Name")
                                                .or_else(|| obj_get(st, "m_FullPath"))
                                                .and_then(|v| v.as_str().map(|s| s.to_string()))
                                                .unwrap_or_default();
                                            if name != "Stand_D_00" {
                                                continue;
                                            }
                                            if let Some(motion) = obj_get(st, "m_Motion") {
                                                let id = pptr(&motion);
                                                let cn = id
                                                    .and_then(|i| clip_name.get(&i))
                                                    .cloned()
                                                    .unwrap_or_default();
                                                eprintln!(
                                                    "CTRL state {name} motion_id={id:?} clip={cn}"
                                                );
                                            }
                                            let skeys: Vec<&str> = match st {
                                                UnityValue::Object(o) => {
                                                    o.keys().map(|s| s.as_str()).collect()
                                                }
                                                _ => vec![],
                                            };
                                            eprintln!("CTRL state {name} keys={skeys:?}");
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // Find PPtrs anywhere that resolve to clip names, with context.
                    fn walk(
                        v: &UnityValue,
                        clip_name: &HashMap<i64, String>,
                        pptr: &dyn Fn(&UnityValue) -> Option<i64>,
                        key: &str,
                        depth: usize,
                    ) {
                        if depth > 8 {
                            return;
                        }
                        match v {
                            UnityValue::Object(o) => {
                                for (kk, vv) in o.iter() {
                                    if let Some(id) = pptr(vv) {
                                        if let Some(n) = clip_name.get(&id) {
                                            if n.eq_ignore_ascii_case("Stand_D_00") {
                                                eprintln!("CTRL clipref {key}.{kk} -> {n} id={id}");
                                            }
                                        }
                                    }
                                    walk(vv, clip_name, pptr, &format!("{key}.{kk}"), depth + 1);
                                }
                            }
                            UnityValue::Array(a) => {
                                for (i, vv) in a.iter().enumerate() {
                                    walk(vv, clip_name, pptr, &format!("{key}[{i}]"), depth + 1);
                                }
                            }
                            _ => {}
                        }
                    }
                    for (k, v) in props.iter() {
                        walk(v, &clip_name, &pptr, k, 0);
                    }
                }
            }
        }
    }

    #[test]
    fn probe_stand_clip_structure() {
        let game = crate::test_support::game_path(r"");
        let path = game.join("abdata/custom/custompose.unity3d");
        let Ok(bundle) = load_bundle(&path) else { return };
        let _ = bundle.data_checked();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(ser) = parse_serialized_file(fb) else { continue };
            for info in &ser.objects {
                if info.type_id != 74 {
                    continue;
                }
                let Ok(obj) = UnityObject::from_serialized_file(&ser, info) else {
                    continue;
                };
                if !obj.name().unwrap_or_default().eq_ignore_ascii_case("Stand_D_00") {
                    continue;
                }
                eprintln!("CLIP file={:?} pid={}", file.name, info.path_id);
                let Some(UnityValue::Object(muscle)) =
                    obj.class.properties().get("m_MuscleClip")
                else {
                    continue;
                };
                let Some(clip) = muscle.get("m_Clip") else {
                    continue;
                };
                let clip = match clip {
                    UnityValue::Object(o) => o.get("data").unwrap_or(clip),
                    _ => clip,
                };
                let UnityValue::Object(clip) = clip else {
                    continue;
                };
                eprintln!("CLIP keys={:?}", clip.keys().collect::<Vec<_>>());
                {
                    let tos = load_avatar_tos(&game);
                    let binds = parse_bindings(&obj);
                    let mut bust = 0usize;
                    let mut cfj = 0usize;
                    let mut names: Vec<String> = Vec::new();
                    for b in &binds {
                        if b.path == 0 {
                            continue;
                        }
                        if let Some(p) = tos.get(&b.path) {
                            let n = bone_name(p);
                            if n.starts_with("cf_j_") {
                                cfj += 1;
                                if names.len() < 8 {
                                    names.push(n.to_string());
                                }
                            }
                            if n.contains("bust") || n.contains("siri") || n.contains("sk_") {
                                bust += 1;
                            }
                        }
                    }
                    eprintln!(
                        "CLIP pid={} binds={} cf_j_bindings={cfj} female-ish={bust} sample={names:?}",
                        info.path_id,
                        binds.len(),
                    );
                }
                for key in ["m_StreamedClip", "m_DenseClip", "m_ConstantClip"] {
                    if let Some(v) = clip.get(key) {
                        match v {
                            UnityValue::Object(o) => {
                                for (k, vv) in o.iter() {
                                    let desc = match vv {
                                        UnityValue::Array(a) => format!("Array len={}", a.len()),
                                        UnityValue::Object(_) => "Object".to_string(),
                                        other => format!("{other:?}"),
                                    };
                                    eprintln!("CLIP   {key}.{k} = {desc}");
                                }
                            }
                            other => eprintln!("CLIP   {key} = {other:?}"),
                        }
                    }
                }
                // Streamed frame times (first few) + hips-y (curve 1) over time.
                if let Some(UnityValue::Object(sc)) = clip.get("m_StreamedClip") {
                    if let Some(UnityValue::Array(arr)) = sc.get("data") {
                        let bits: Vec<u32> = arr.iter().map(as_u32).collect();
                        let mut i = 0usize;
                        let mut times = Vec::new();
                        let mut values: HashMap<i32, f32> = HashMap::new();
                        let mut samples: Vec<(f32, f32)> = Vec::new();
                        while i + 2 <= bits.len() {
                            let t = f32::from_bits(bits[i]);
                            let n = bits[i + 1] as usize;
                            i += 2;
                            if n > 0 && i + n * 5 > bits.len() {
                                break;
                            }
                            let finite = t.is_finite();
                            for _ in 0..n {
                                let index = bits[i] as i32;
                                let value = f32::from_bits(bits[i + 4]);
                                if finite {
                                    values.insert(index, value);
                                }
                                i += 5;
                            }
                            if finite {
                                if times.len() < 12 {
                                    times.push((t, n));
                                }
                                samples.push((t, values.get(&1).copied().unwrap_or(f32::NAN)));
                            }
                        }
                        eprintln!("CLIP streamed frames(first12)={times:?}");
                        eprintln!("CLIP hipsY over time (t,curve1):");
                        for (t, y) in &samples {
                            eprintln!("CLIP   t={t:.4} y={y:.6}");
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn probe_stand_hips_bindings() {
        let game = crate::test_support::game_path(r"");
        if !game.join("abdata").ok_is_dir() {
            return;
        }
        let Some((binds, values, streamed_n, dense_n, const_n)) =
            load_clip(&game, "Stand_D_00")
        else {
            eprintln!("HIPS clip missing");
            return;
        };
        let tos = load_avatar_tos(&game);
        let mut curve_of = vec![0i32; binds.len()];
        let mut c = 0i32;
        for (i, b) in binds.iter().enumerate() {
            curve_of[i] = c;
            c += curve_dim(b) as i32;
        }
        eprintln!(
            "HIPS clip binds={} streamed={streamed_n} dense={dense_n} const={const_n} curves={c} tos={}",
            binds.len(),
            tos.len()
        );
        for (i, b) in binds.iter().enumerate() {
            if b.path == 0 {
                continue;
            }
            let Some(path) = tos.get(&b.path) else { continue };
            let name = bone_name(path);
            if name == "cf_j_hips"
                || name == "cf_j_root"
                || name == "cf_j_spine01"
                || name == "cf_j_waist01"
                || name == "cf_n_height"
                || name.contains("hips")
                || name == "cf_j_thigh00_R"
                || name == "cf_j_leg01_R"
                || name == "cf_j_leg03_R"
                || name == "cf_j_foot_R"
                || name == "cf_j_toes_R"
                || name == "cf_pv_leg_L"
                || name == "cf_pv_root"
            {
                let comps = [
                    curve_at(&values, curve_of[i]),
                    curve_at(&values, curve_of[i] + 1),
                    curve_at(&values, curve_of[i] + 2),
                    curve_at(&values, curve_of[i] + 3),
                ];
                eprintln!(
                    "HIPS bind {name} attr={} type={} path={} curve_of={} dim={} comps={comps:?}",
                    b.attribute,
                    b.type_id,
                    path,
                    curve_of[i],
                    curve_dim(b)
                );
            }
        }
    }

    #[test]
    fn list_custompose_clip_names() {
        let game = crate::test_support::game_path(r"");
        let path = game.join("abdata/custom/custompose.unity3d");
        let Ok(bundle) = load_bundle(&path) else {
            return;
        };
        let _ = bundle.data_checked();
        let mut names = Vec::new();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else {
                continue;
            };
            let Ok(ser) = parse_serialized_file(fb) else {
                continue;
            };
            for info in &ser.objects {
                if info.type_id != 74 {
                    continue;
                }
                let Ok(obj) = UnityObject::from_serialized_file(&ser, info) else {
                    continue;
                };
                let n = obj.name().unwrap_or_default();
                if !n.is_empty() {
                    names.push(n);
                }
            }
        }
        names.sort();
        names.dedup();
        eprintln!("CLIPS n={} {}", names.len(), names.join(","));
        assert!(names.iter().any(|n| n.eq_ignore_ascii_case("Stand_D_00")));
    }
}

#[cfg(test)]
mod multilayer_tests;

#[cfg(test)]
mod layer_clock_tests;

#[cfg(test)]
mod trigger_tests;
#[cfg(test)]
mod crossfade_tests;
#[cfg(test)]
mod layer_fade_tests;

#[cfg(test)] mod parallel_fade_tests;

pub(crate) fn prepare_studio_yure(game:&Path,key:&str,source:&str,owner:&str)->bool { yure::load(game,key,source,owner).is_ok() }
