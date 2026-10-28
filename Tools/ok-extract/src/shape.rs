//! Bone morph: AnimationKeyInfo + cf_custombody/head + ShapeBody/Head Update.
//! Port of kks_algo.md / ShapeInfoBase.ChangeValue + ShapeBodyInfoFemale.Update
//! with typeBone == 0 (no shapecorrect offsets).

use std::collections::HashMap;
use std::path::Path;

use crate::list::text_assets_from_bundle;
use crate::pose::Skeleton;

pub(crate) mod hand;

const BODY_CAT: &str = include_str!("../data/cf_custombody.txt");
const HEAD_CAT: &str = include_str!("../data/cf_customhead.txt");

const DST_BODY: &[&str] = &[
    "cf_n_height", "cf_s_hand_L", "cf_s_hand_R", "cf_s_head", "cf_s_neck",
    "cf_s_spine03", "cf_s_shoulder02_L", "cf_s_shoulder02_R", "cf_s_arm01_L", "cf_s_arm01_R",
    "cf_s_arm02_L", "cf_s_arm02_R", "cf_s_arm03_L", "cf_s_arm03_R", "cf_s_forearm01_L",
    "cf_s_forearm01_R", "cf_s_forearm02_L", "cf_s_forearm02_R", "cf_s_wrist_L", "cf_s_wrist_R",
    "cf_s_spine02", "cf_s_spine01", "cf_s_waist01", "cf_s_waist02", "cf_s_siri_L",
    "cf_s_siri_R", "cf_s_thigh01_L", "cf_s_thigh01_R", "cf_s_thigh02_L", "cf_s_thigh02_R",
    "cf_s_thigh03_L", "cf_s_thigh03_R", "cf_s_leg01_L", "cf_s_leg01_R", "cf_s_leg02_L",
    "cf_s_leg02_R", "cf_s_leg03_L", "cf_s_leg03_R", "cf_d_kokan", "cf_s_bust00_L",
    "cf_d_bust01_L", "cf_d_bust02_L", "cf_d_bust03_L", "cf_s_bust01_L", "cf_s_bust02_L",
    "cf_s_bust03_L", "cf_hit_bust02_L", "cf_d_bnip01_L", "cf_s_bnip01_L", "cf_s_bnip025_L",
    "cf_s_bnip015_L", "cf_s_bnip02_L", "cf_s_bnipacc_L", "cf_s_bust00_R", "cf_d_bust01_R",
    "cf_d_bust02_R", "cf_d_bust03_R", "cf_s_bust01_R", "cf_s_bust02_R", "cf_s_bust03_R",
    "cf_hit_bust02_R", "cf_d_bnip01_R", "cf_s_bnip01_R", "cf_s_bnip025_R", "cf_s_bnip015_R",
    "cf_s_bnip02_R", "cf_s_bnipacc_R", "cf_hit_siri_L", "cf_hit_siri_R", "cf_hit_waist_L",
    "cf_hit_berry", "cf_hit_spine02_L", "cf_hit_shoulder_L", "cf_hit_shoulder_R", "cf_hit_arm_L",
    "cf_hit_arm_R", "cf_hit_spine01", "cf_d_sk_top", "cf_d_sk_00_00", "cf_d_sk_01_00",
    "cf_d_sk_02_00", "cf_d_sk_03_00", "cf_d_sk_04_00", "cf_d_sk_05_00", "cf_d_sk_06_00",
    "cf_d_sk_07_00",
];

const SRC_BODY: &[&str] = &[
    "cf_a_height", "cf_a_height_aid", "cf_a_head", "cf_a_neck", "cf_a_spine03",
    "cf_a_shoulder", "cf_a_shoulder_L_aid03", "cf_a_shoulder_R_aid03", "cf_a_arm_L_aid03",
    "cf_a_arm_R_aid03", "cf_a_arm02", "cf_a_arm03_blend01", "cf_a_arm03_blend02", "cf_a_farm01",
    "cf_a_farm02_blend01", "cf_a_farm02_blend03", "cf_a_farm03", "cf_a_spine02",
    "cf_a_spine02_aid_berry", "cf_a_spine01", "cf_a_berry", "cf_a_waist01", "cf_a_waist02",
    "cf_a_siri", "cf_a_thigh01_L", "cf_a_thigh01_L_aid", "cf_a_thigh01_R", "cf_a_thigh01_R_aid",
    "cf_a_thigh02_L_blend01", "cf_a_thigh02_L_blend03", "cf_a_thigh02_R_blend01",
    "cf_a_thigh02_R_blend03", "cf_a_thigh03_L", "cf_a_thigh03_R", "cf_a_leg01_L", "cf_a_leg01_R",
    "cf_a_leg02_L", "cf_a_leg02_R", "cf_a_leg03", "cf_a_dan", "cf_a_bust_ty",
    "cf_a_bust00_aid03_sz", "cf_a_bust00_aid02_sz", "cf_a_bust_L_ry", "cf_a_bust_rx",
    "cf_a_bust01_size", "cf_a_bust_L_tx", "cf_a_bust02_size", "cf_a_bust_tz", "cf_a_bust03_size",
    "cf_a_bust01_shape1", "cf_a_bust02_shape1", "cf_a_bust03_shape1", "cf_a_hit_bust_shape1",
    "cf_a_hit_bust_shape2", "cf_a_bnip01", "cf_a_bnip01_size", "cf_a_d_bnip01_size",
    "cf_a_bnip02_size", "cf_a_bnip015_size", "cf_a_bnip02", "cf_a_bnipacc_stand",
    "cf_a_bnipacc_size", "cf_a_bust_R_ry", "cf_a_bust_R_tx", "cf_a_hit_siri_shape1",
    "cf_a_hit_siri_shape2", "cf_a_hit_siri_shape3", "cf_a_hit_siri_shape4", "cf_a_hit_siri_shape5",
    "cf_a_hit_siri_shape6", "cf_a_hit_waist_shape1", "cf_a_hit_waist_shape2", "cf_a_hit_waist_shape3",
    "cf_a_hit_spinety_shape", "cf_a_hit_waist_shape4", "cf_a_hit_waist_shape5", "cf_a_hit_berry_shape",
    "cf_a_hit_berry_shape2", "cf_a_hit_berry_shape3", "cf_a_hit_spine02_shape1",
    "cf_a_hit_spine02_shape2", "cf_a_hit_spine02_shape3", "cf_a_hit_shoulder_shape1",
    "cf_a_hit_shoulder_shape2", "cf_a_hit_shoulder_shape3", "cf_a_hit_arm_shape2",
    "cf_a_hit_arm_shape3", "cf_a_hit_arm_shape4", "cf_a_hit_spine01_shape1", "cf_a_hit_spine01_shape2",
    "cf_a_sk_00_00", "cf_a_sk_00_01", "cf_a_sk_berry", "cf_a_sk_thigh01_sz", "cf_a_sk_01_00",
    "cf_a_sk_01_01", "cf_a_sk_thigh01_sx", "cf_a_sk_02_00", "cf_a_sk_02_01", "cf_a_sk_siri",
    "cf_a_sk_03_00", "cf_a_sk_03_01", "cf_a_sk_04_00", "cf_a_sk_04_01", "cf_a_sk_05_00",
    "cf_a_sk_05_01", "cf_a_sk_06_00", "cf_a_sk_06_01", "cf_a_sk_07_00", "cf_a_sk_07_01",
];

const DST_HEAD: &[&str] = &[
    "cf_J_FaceBase", "cf_J_FaceUp_tz", "cf_J_NoseBridge_ty", "cf_J_FaceUp_ty", "cf_J_FaceLow_tz",
    "cf_J_FaceLow_sx", "cf_J_CheekUp2_L", "cf_J_CheekUp2_R", "cf_J_ChinLow", "cf_J_CheekLow_s_L",
    "cf_J_CheekLow_s_R", "cf_J_Chin_Base", "cf_J_ChinTip_Base", "cf_J_Nose_tip", "cf_J_NoseBase_rx",
    "cf_J_NoseBridge_rx", "cf_J_CheekUpBase", "cf_J_CheekUp_s_L", "cf_J_CheekUp_s_R", "cf_J_Eye_tx_L",
    "cf_J_Eye_tx_R", "cf_J_megane_rx_ear", "cf_J_Eye_rz_L", "cf_J_Eye_rz_R", "cf_J_Eye_tz",
    "cf_J_Eye01_s_L", "cf_J_Eye01_s_R", "cf_J_Eye05_s_L", "cf_J_Eye05_s_R", "cf_J_Eye02_s_L",
    "cf_J_Eye02_s_R", "cf_J_Eye03_s_L", "cf_J_Eye03_s_R", "cf_J_Eye04_s_L", "cf_J_Eye04_s_R",
    "cf_J_Eye08_s_L", "cf_J_Eye08_s_R", "cf_J_Eye07_s_L", "cf_J_Eye07_s_R", "cf_J_Eye06_s_L",
    "cf_J_Eye06_s_R", "cf_J_Mayu_L", "cf_J_Mayu_R", "cf_J_MayuMid_s_L", "cf_J_MayuMid_s_R",
    "cf_J_MayuTip_s_L", "cf_J_MayuTip_s_R", "cf_J_EarBase_ry_L", "cf_J_EarBase_ry_R", "cf_J_EarUp_L",
    "cf_J_EarUp_R", "cf_J_EarLow_L", "cf_J_EarLow_R", "cf_J_MouthBase_ty", "cf_J_Mouth_L",
    "cf_J_Mouth_R", "cf_J_MouthBase_rx", "cf_J_Mouthup", "cf_J_MouthLow",
];

#[derive(Clone, Copy)]
struct Bone {
    pos: [f32; 3],
    rot: [f32; 3],
    scl: [f32; 3],
}

impl Default for Bone {
    fn default() -> Self {
        Self {
            pos: [0.0; 3],
            rot: [0.0; 3],
            scl: [1.0, 1.0, 1.0],
        }
    }
}

#[derive(Clone, Copy, Default)]
struct Dst {
    pos: [f32; 3],
    rot: [f32; 3],
    scl: [f32; 3],
    set_pos: [bool; 3],
    set_rot: bool,
    set_scl: [bool; 3],
}

impl Dst {
    fn px(&mut self, v: f32) {
        self.pos[0] = v;
        self.set_pos[0] = true;
    }
    fn py(&mut self, v: f32) {
        self.pos[1] = v;
        self.set_pos[1] = true;
    }
    fn pz(&mut self, v: f32) {
        self.pos[2] = v;
        self.set_pos[2] = true;
    }
    fn rot(&mut self, x: f32, y: f32, z: f32) {
        self.rot = [x, y, z];
        self.set_rot = true;
    }
    fn sx(&mut self, v: f32) {
        self.scl[0] = v;
        self.set_scl[0] = true;
    }
    fn sy(&mut self, v: f32) {
        self.scl[1] = v;
        self.set_scl[1] = true;
    }
    fn sz(&mut self, v: f32) {
        self.scl[2] = v;
        self.set_scl[2] = true;
    }
    fn scl(&mut self, x: f32, y: f32, z: f32) {
        self.scl = [x, y, z];
        self.set_scl = [true, true, true];
    }
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
struct CatRow {
    category: i32,
    name: String,
    id: i32,
    use_pos: [bool; 3],
    use_rot: [bool; 3],
    use_scl: [bool; 3],
    get: [bool; 3],
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(crate) struct Keyframe {
    pos: [f32; 3],
    rot: [f32; 3],
    scl: [f32; 3],
}

fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

fn lerp_angle(a: f32, b: f32, t: f32) -> f32 {
    let mut delta = (b - a) % 360.0;
    if delta > 180.0 {
        delta -= 360.0;
    }
    if delta < -180.0 {
        delta += 360.0;
    }
    a + delta * t
}

fn read_7bit(data: &[u8], pos: &mut usize) -> Option<u32> {
    let mut result = 0u32;
    let mut shift = 0;
    for _ in 0..5 {
        let b = *data.get(*pos)?;
        *pos += 1;
        result |= ((b & 0x7f) as u32) << shift;
        if b & 0x80 == 0 {
            return Some(result);
        }
        shift += 7;
    }
    None
}

fn read_i32(data: &[u8], pos: &mut usize) -> Option<i32> {
    let b = data.get(*pos..*pos + 4)?.try_into().ok()?;
    *pos += 4;
    Some(i32::from_le_bytes(b))
}

fn read_f32(data: &[u8], pos: &mut usize) -> Option<f32> {
    let b = data.get(*pos..*pos + 4)?.try_into().ok()?;
    *pos += 4;
    Some(f32::from_le_bytes(b))
}

fn read_dotnet_string(data: &[u8], pos: &mut usize) -> Option<String> {
    let n = read_7bit(data, pos)? as usize;
    if *pos + n > data.len() {
        return None;
    }
    let s = String::from_utf8_lossy(&data[*pos..*pos + n]).into_owned();
    *pos += n;
    Some(s)
}

pub(crate) fn key_scl_range(bytes: &[u8], key: &str) -> (usize, f32, f32) {
    let m = load_anm_keys(bytes);
    match m.get(key) {
        Some(k) if !k.is_empty() => {
            let lo = k.iter().fold(f32::MAX, |a, x| a.min(x.scl[0]));
            let hi = k.iter().fold(f32::MIN, |a, x| a.max(x.scl[0]));
            (k.len(), lo, hi)
        }
        _ => (0, 0.0, 0.0),
    }
}

pub(crate) fn load_anm_keys(bytes: &[u8]) -> HashMap<String, Vec<Keyframe>> {
    let mut pos = 0usize;
    let mut map = HashMap::new();
    let Some(n) = read_i32(bytes, &mut pos) else {
        return map;
    };
    if n < 0 || n > 10_000 {
        return map;
    }
    for _ in 0..n {
        let Some(name) = read_dotnet_string(bytes, &mut pos) else {
            break;
        };
        let Some(k) = read_i32(bytes, &mut pos) else {
            break;
        };
        let mut keys = Vec::new();
        for _ in 0..k.max(0) {
            let _no = read_i32(bytes, &mut pos);
            let Some(px) = read_f32(bytes, &mut pos) else { break };
            let py = read_f32(bytes, &mut pos).unwrap_or(0.0);
            let pz = read_f32(bytes, &mut pos).unwrap_or(0.0);
            let rx = read_f32(bytes, &mut pos).unwrap_or(0.0);
            let ry = read_f32(bytes, &mut pos).unwrap_or(0.0);
            let rz = read_f32(bytes, &mut pos).unwrap_or(0.0);
            let sx = read_f32(bytes, &mut pos).unwrap_or(1.0);
            let sy = read_f32(bytes, &mut pos).unwrap_or(1.0);
            let sz = read_f32(bytes, &mut pos).unwrap_or(1.0);
            keys.push(Keyframe {
                pos: [px, py, pz],
                rot: [rx, ry, rz],
                scl: [sx, sy, sz],
            });
        }
        map.insert(name, keys);
    }
    map
}

fn slider_unlocker_extrap(a: [f32; 3], b: [f32; 3], rate: f32) -> [f32; 3] {
    [
        a[0] + (b[0] - a[0]) * rate,
        a[1] + (b[1] - a[1]) * rate,
        a[2] + (b[2] - a[2]) * rate,
    ]
}

/// SliderUnlocker.SliderMath.CalculateRotation (BepisPlugins 19.5).
fn slider_unlocker_rotation(keys: &[Keyframe], rate: f32) -> [f32; 3] {
    if keys.len() < 2 {
        return keys.first().map(|k| k.rot).unwrap_or([0.0; 3]);
    }
    let rot = keys[0].rot;
    let rot2 = keys[1].rot;
    let rot3 = keys[keys.len() - 1].rot;
    let val = [rot2[0] - rot[0], rot2[1] - rot[1], rot2[2] - rot[2]];
    let mut val2 = [rot3[0] - rot[0], rot3[1] - rot[1], rot3[2] - rot[2]];
    for i in 0..3 {
        let positive = val[i] >= 0.0;
        if val2[i] > 0.0 && !positive {
            val2[i] -= 360.0;
        } else if val2[i] < 0.0 && positive {
            val2[i] += 360.0;
        }
    }
    if rate < 0.0 {
        [
            rot[0] - val2[0] * rate.abs(),
            rot[1] - val2[1] * rate.abs(),
            rot[2] - val2[2] * rate.abs(),
        ]
    } else {
        let t = (rate - 1.0).abs();
        [
            rot3[0] + val2[0] * t,
            rot3[1] + val2[1] * t,
            rot3[2] + val2[2] * t,
        ]
    }
}

fn skip_unlocker_rotation(name: &str) -> bool {
    // SliderUnlocker.SliderMath.SafeCalculateRotation skip list.
    name.contains("cf_s_Mune")
        || name.contains("cf_s_Mouth")
        || name.contains("cf_s_LegLow")
        || name.contains("cf_s_MayuTip")
        || (name.contains("thigh") && name.contains("01"))
        || (name.starts_with("cf_a_bust") && name.ends_with("_size"))
}

fn get_info(keys: &[Keyframe], rate: f32, flag: [bool; 3], name: &str) -> Bone {
    let mut out = Bone::default();
    if keys.is_empty() {
        return out;
    }
    let last = keys.len() - 1;
    // SliderUnlocker Harmony postfix on AnimationKeyInfo.GetInfo: vanilla is
    // still used inside [0,1], but out-of-range rates are first+(last-first)*rate
    // (and a dedicated rotation wrap). CheckDataRange is NOP'd by the same plugin.
    if !(0.0..=1.0).contains(&rate) && keys.len() >= 2 {
        let first = &keys[0];
        let lastk = &keys[last];
        if flag[0] {
            out.pos = slider_unlocker_extrap(first.pos, lastk.pos, rate);
        }
        if flag[1] {
            out.rot = if skip_unlocker_rotation(name) {
                if rate <= 0.0 {
                    first.rot
                } else {
                    lastk.rot
                }
            } else {
                slider_unlocker_rotation(keys, rate)
            };
        }
        if flag[2] {
            out.scl = slider_unlocker_extrap(first.scl, lastk.scl, rate);
        }
        return out;
    }
    let sample = |rate: f32, pick: fn(&Keyframe) -> [f32; 3], ang: bool| -> [f32; 3] {
        if rate <= 0.0 {
            return pick(&keys[0]);
        }
        if rate >= 1.0 {
            return pick(&keys[last]);
        }
        let f = last as f32 * rate;
        let i = (f.floor() as usize).min(last.saturating_sub(1));
        let t = f - i as f32;
        let a = pick(&keys[i]);
        let b = pick(&keys[(i + 1).min(last)]);
        if ang {
            [lerp_angle(a[0], b[0], t), lerp_angle(a[1], b[1], t), lerp_angle(a[2], b[2], t)]
        } else {
            [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)]
        }
    };
    if flag[0] {
        out.pos = sample(rate, |k| k.pos, false);
    }
    if flag[1] {
        out.rot = sample(rate, |k| k.rot, true);
    }
    if flag[2] {
        out.scl = sample(rate, |k| k.scl, false);
    }
    out
}

fn parse_cat(text: &str, names: &[&str]) -> Vec<CatRow> {
    let mut id_of = HashMap::new();
    for (i, n) in names.iter().enumerate() {
        id_of.insert(*n, i as i32);
    }
    let mut rows = Vec::new();
    for line in text.lines() {
        let cols: Vec<&str> = line.split('\t').collect();
        if cols.len() < 11 {
            continue;
        }
        let Ok(cat) = cols[0].trim().parse::<i32>() else {
            continue;
        };
        let name = cols[1].trim().to_string();
        let Some(&id) = id_of.get(name.as_str()) else {
            continue;
        };
        let flag = |i: usize| cols[i].trim() != "0";
        let use_pos = [flag(2), flag(3), flag(4)];
        let use_rot = [flag(5), flag(6), flag(7)];
        let use_scl = [flag(8), flag(9), flag(10)];
        rows.push(CatRow {
            category: cat,
            name,
            id,
            use_pos,
            use_rot,
            use_scl,
            get: [
                use_pos.iter().any(|x| *x),
                use_rot.iter().any(|x| *x),
                use_scl.iter().any(|x| *x),
            ],
        });
    }
    rows
}

fn change_values(keys: &HashMap<String, Vec<Keyframe>>, rows: &[CatRow], values: &[f32], nsrc: usize) -> Vec<Bone> {
    let mut src = vec![Bone::default(); nsrc];
    for (cat, val) in values.iter().enumerate() {
        for row in rows.iter().filter(|r| r.category == cat as i32) {
            let Some(seq) = keys.get(&row.name) else {
                continue;
            };
            let info = get_info(seq, *val, row.get, &row.name);
            let b = &mut src[row.id as usize];
            for a in 0..3 {
                if row.use_pos[a] {
                    b.pos[a] = info.pos[a];
                }
                if row.use_rot[a] {
                    b.rot[a] = info.rot[a];
                }
                if row.use_scl[a] {
                    b.scl[a] = info.scl[a];
                }
            }
        }
    }
    src
}

fn s(src: &[Bone], i: usize) -> Bone {
    src.get(i).copied().unwrap_or_default()
}

fn nz(v: f32) -> f32 {
    if v.abs() < 1e-8 { 1.0 } else { v }
}

fn dst_mut(d: &mut HashMap<i32, Dst>, id: i32) -> &mut Dst {
    d.entry(id).or_default()
}

/// ShapeBodyInfoFemale.correctValue[32] from list/shapecorrect/shapecorrect.unity3d.
#[derive(Clone, Copy, Default, serde::Serialize, serde::Deserialize)]
pub struct CorrectInfo {
    pub pos: [f32; 3],
    pub rot: [f32; 3],
    pub scl: [f32; 3],
}

pub fn load_correct_info(game_root: &Path) -> Vec<CorrectInfo> {
    let path = game_root.join("abdata/list/shapecorrect/shapecorrect.unity3d");
    for (name, bytes) in text_assets_from_bundle(&path) {
        if !name.eq_ignore_ascii_case("shapecorrect") {
            continue;
        }
        let mut p = 0usize;
        let Some(n) = read_i32(&bytes, &mut p) else {
            continue;
        };
        if n < 0 || n > 64 {
            continue;
        }
        let mut out = Vec::with_capacity(n as usize);
        for _ in 0..n {
            let px = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let py = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let pz = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let rx = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let ry = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let rz = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let sx = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let sy = read_f32(&bytes, &mut p).unwrap_or(0.0);
            let sz = read_f32(&bytes, &mut p).unwrap_or(0.0);
            out.push(CorrectInfo {
                pos: [px, py, pz],
                rot: [rx, ry, rz],
                scl: [sx, sy, sz],
            });
        }
        return out;
    }
    Vec::new()
}

/// CorrectValue contribution: all zero for typeBone == 0 (vanilla bone layout).
fn cv(correct: &[CorrectInfo], type_bone: i32, i: usize) -> Bone {
    if type_bone == 0 {
        return Bone {
            pos: [0.0; 3],
            rot: [0.0; 3],
            scl: [0.0; 3],
        };
    }
    correct.get(i).map(|c| Bone {
        pos: c.pos,
        rot: c.rot,
        scl: c.scl,
    }).unwrap_or_default()
}

/// Component-wise world scale of a TRS matrix (lengths of the basis columns).
pub(crate) fn world_scale(m: &crate::pose::Mat4) -> [f32; 3] {
    let cx = (m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]).sqrt();
    let cy = (m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]).sqrt();
    let cz = (m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2]).sqrt();
    [cx, cy, cz]
}

pub(crate) fn tree_ids(skel: &Skeleton) -> Option<std::collections::HashSet<i64>> {
    crate::pose::animator_tree(skel).map(|(_,tree)|tree.into_iter().collect())
}

fn apply_dst(skel: &mut Skeleton, names: &[&str], dst: &HashMap<i32, Dst>) {
    apply_dst_locals(skel,names,dst);
    skel.rebuild_worlds();
}
fn apply_dst_locals(skel: &mut Skeleton, names: &[&str], dst: &HashMap<i32, Dst>) -> Option<std::collections::HashSet<i64>> {
    let only = tree_ids(skel);
    let mut writes:HashMap<&str,Vec<&Dst>>=HashMap::new();
    for (id, d) in dst {
        if let Some(name)=names.get(*id as usize){writes.entry(name).or_default().push(d);}
    }
    // Match every transform with this name, including duplicates, and retain
    // write order. Resolve the allowed current tree once; no persistent rig
    // cache can hide a live reparenting or a shape resource replacement.
    for (id,tf) in &mut skel.tfs {
        if only.as_ref().is_some_and(|allow|!allow.contains(id)){continue;}
        if let Some(values)=writes.get(tf.name.as_str()){for d in values {
            for axis in 0..3 {if d.set_pos[axis]{tf.pos[axis]=d.pos[axis];}if d.set_scl[axis]{tf.scale[axis]=d.scl[axis];}}
            if d.set_rot{tf.rot=crate::pose::euler_deg_to_quat(d.rot);}
        }}
    }
    only
}

fn body_update(
    src: &[Bone],
    correct: &[CorrectInfo],
    head_size: f32,
    neck_size: f32,
    type_bone: i32,
) -> HashMap<i32, Dst> {
    let c = |i: usize| cv(correct, type_bone, i);
    let mut d: HashMap<i32, Dst> = HashMap::new();
    let v2 = s(src, 1).scl;
    let v1 = s(src, 0).scl;
    dst_mut(&mut d, 0).scl(v1[0], v1[1], v1[2]);
    dst_mut(&mut d, 1).scl(v2[0], v2[1], v2[2]);
    dst_mut(&mut d, 2).scl(v2[0], v2[1], v2[2]);
    {
        let h = s(src, 2);
        let q = c(2);
        let b = dst_mut(&mut d, 3);
        b.py(h.pos[1] + q.pos[1]);
        b.scl(
            h.scl[0] * v2[0] * head_size + q.scl[0],
            h.scl[1] * v2[1] * head_size + q.scl[1],
            h.scl[2] * v2[2] * head_size + q.scl[2],
        );
    }
    {
        let n = s(src, 3);
        let q = c(1);
        let b = dst_mut(&mut d, 4);
        b.py(q.pos[1]);
        b.pz(n.pos[2]);
        b.rot(q.rot[0], 0.0, 0.0);
        b.scl(
            n.scl[0] * v2[0] * neck_size + q.scl[0],
            1.0,
            n.scl[2] * v2[2] * neck_size + q.scl[2],
        );
    }
    {
        let sp = s(src, 4);
        let q = c(5);
        let b = dst_mut(&mut d, 5);
        b.pz(sp.pos[2] + q.pos[2]);
        b.rot(q.rot[0], 0.0, 0.0);
        b.scl(
            sp.scl[0] * v2[0] + q.scl[0],
            1.0,
            sp.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let sh = s(src, 5);
        let aid = s(src, 6);
        let q = c(14);
        let b = dst_mut(&mut d, 6);
        b.px(aid.pos[0] + sh.pos[0] + q.pos[0]);
        b.py(sh.pos[1] + q.pos[1]);
        b.pz(q.pos[2]);
        b.scl(
            sh.scl[0],
            sh.scl[1] * v2[1] + q.scl[1],
            sh.scl[2] * aid.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let sh = s(src, 5);
        let aid = s(src, 7);
        let q = c(15);
        let b = dst_mut(&mut d, 7);
        b.px(aid.pos[0] - sh.pos[0] + q.pos[0]);
        b.py(sh.pos[1] + q.pos[1]);
        b.pz(q.pos[2]);
        b.scl(
            sh.scl[0],
            sh.scl[1] * v2[1] + q.scl[1],
            sh.scl[2] * aid.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let sh = s(src, 5);
        let aid = s(src, 8);
        let q = c(16);
        let b = dst_mut(&mut d, 8);
        b.px(aid.pos[0]);
        b.py(sh.pos[1] + q.pos[1]);
        b.scl(
            1.0 + q.scl[0],
            sh.scl[1] * v2[1] + q.scl[1],
            sh.scl[2] * aid.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let sh = s(src, 5);
        let aid = s(src, 9);
        let q = c(17);
        let b = dst_mut(&mut d, 9);
        b.px(aid.pos[0]);
        b.py(sh.pos[1] + q.pos[1]);
        b.scl(
            1.0 + q.scl[0],
            sh.scl[1] * v2[1] + q.scl[1],
            sh.scl[2] * aid.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 10);
        let q = c(18);
        let b = dst_mut(&mut d, 10);
        b.py(a.pos[1] + q.pos[1]);
        b.pz(a.pos[2] + q.pos[2]);
        b.scl(1.0, a.scl[1] * v2[1] + q.scl[1], a.scl[2] * v2[2] + q.scl[2]);
        let q = c(19);
        let b = dst_mut(&mut d, 11);
        b.py(a.pos[1] + q.pos[1]);
        b.pz(a.pos[2] + q.pos[2]);
        b.scl(1.0, a.scl[1] * v2[1] + q.scl[1], a.scl[2] * v2[2] + q.scl[2]);
    }
    {
        let a = s(src, 11);
        let cc = s(src, 12);
        let q = c(20);
        let b = dst_mut(&mut d, 12);
        b.py(q.pos[1]);
        b.scl(
            1.0,
            a.scl[1] * cc.scl[1] * v2[1] + q.scl[1],
            a.scl[2] * cc.scl[2] * v2[2] + q.scl[2],
        );
        let q = c(21);
        let b = dst_mut(&mut d, 13);
        b.py(q.pos[1]);
        b.scl(
            1.0,
            a.scl[1] * cc.scl[1] * v2[1] + q.scl[1],
            a.scl[2] * cc.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 13);
        let q = c(22);
        let b = dst_mut(&mut d, 14);
        b.py(q.pos[1]);
        b.scl(1.0, a.scl[1] * v2[1], a.scl[2] * v2[2]);
        let q = c(23);
        let b = dst_mut(&mut d, 15);
        b.py(q.pos[1]);
        b.scl(1.0, a.scl[1] * v2[1], a.scl[2] * v2[2]);
    }
    {
        let a = s(src, 14);
        let cc = s(src, 15);
        let q = c(24);
        let b = dst_mut(&mut d, 16);
        b.rot(0.0, q.rot[1], q.rot[2]);
        b.scl(
            1.0,
            a.scl[1] * cc.scl[1] * v2[1] + q.scl[1],
            a.scl[2] * cc.scl[2] * v2[2] + q.scl[2],
        );
        let q = c(25);
        let b = dst_mut(&mut d, 17);
        b.rot(0.0, q.rot[1], q.rot[2]);
        b.scl(
            1.0,
            a.scl[1] * cc.scl[1] * v2[1] + q.scl[1],
            a.scl[2] * cc.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 16);
        let q = c(26);
        let b = dst_mut(&mut d, 18);
        b.scl(1.0 + q.scl[0], a.scl[1] * v2[1] + q.scl[1], a.scl[2] * v2[2]);
        let q = c(27);
        let b = dst_mut(&mut d, 19);
        b.scl(1.0 + q.scl[0], a.scl[1] * v2[1] + q.scl[1], a.scl[2] * v2[2]);
    }
    {
        let a = s(src, 17);
        let cc = s(src, 18);
        let q = c(4);
        let b = dst_mut(&mut d, 20);
        b.pz(a.pos[2] + cc.pos[2] + q.pos[2]);
        b.scl(
            a.scl[0] * v2[0] + q.scl[0],
            1.0,
            a.scl[2] * v2[2] * cc.scl[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 19);
        let cc = s(src, 20);
        let q = c(3);
        let b = dst_mut(&mut d, 21);
        b.py(a.pos[1] + cc.pos[1]);
        b.pz(a.pos[2] + cc.pos[2] + q.pos[2]);
        b.rot(cc.rot[0], 0.0, 0.0);
        b.scl(
            a.scl[0] * v2[0] * cc.scl[0] + q.scl[0],
            cc.scl[1],
            a.scl[2] * v2[2] * cc.scl[2] + q.scl[2],
        );
    }
    {
        let berry = s(src, 20);
        let w = s(src, 21);
        let b = dst_mut(&mut d, 22);
        b.py(berry.pos[1]);
        b.pz(w.pos[2] + berry.pos[2]);
        b.rot(berry.rot[0], 0.0, 0.0);
        b.scl(w.scl[0] * v2[0] * berry.scl[0], berry.scl[1], w.scl[2] * v2[2] * berry.scl[2]);
    }
    {
        let w = s(src, 22);
        let q = c(0);
        let b = dst_mut(&mut d, 23);
        b.pz(w.pos[2]);
        b.scl(w.scl[0] * v2[0] + q.scl[0], 1.0, w.scl[2] * v2[2]);
    }
    {
        let siri = s(src, 23);
        let w = s(src, 22);
        let berry = s(src, 21);
        for (id, ci) in [(24, 28usize), (25, 29)] {
            let q = c(ci);
            let b = dst_mut(&mut d, id);
            b.px(q.pos[0]);
            b.py(siri.pos[1]);
            b.pz(siri.pos[2] + w.pos[2] * 0.3);
            b.rot(siri.rot[0] + q.rot[0], 0.0, q.rot[2]);
            b.scl(
                siri.scl[0] + (-1.0 + w.scl[0]) * 0.5 + q.scl[0],
                siri.scl[1],
                siri.scl[2] + (-1.0 + w.scl[2]) * 0.5 + (-1.0 + berry.scl[2]) * 0.5,
            );
        }
    }
    {
        let t = s(src, 24);
        let aid = s(src, 25);
        let q = c(6);
        let b = dst_mut(&mut d, 26);
        b.px(t.pos[0] + aid.pos[0] + q.pos[0]);
        b.pz(t.pos[2] + q.pos[2]);
        b.rot(t.rot[0] + q.rot[0], q.rot[1], t.rot[2] + q.rot[2]);
        b.scl(
            t.scl[0] * aid.scl[0] * v2[0] + q.scl[0],
            1.0,
            t.scl[2] * aid.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let t = s(src, 26);
        let aid = s(src, 27);
        let q = c(7);
        let b = dst_mut(&mut d, 27);
        b.px(t.pos[0] + aid.pos[0] + q.pos[0]);
        b.pz(t.pos[2] + q.pos[2]);
        b.rot(t.rot[0] + q.rot[0], q.rot[1], t.rot[2] + q.rot[2]);
        b.scl(
            t.scl[0] * aid.scl[0] * v2[0] + q.scl[0],
            1.0,
            t.scl[2] * aid.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 28);
        let cc = s(src, 29);
        let q = c(8);
        let b = dst_mut(&mut d, 28);
        b.px(a.pos[0] + cc.pos[0] + q.pos[0]);
        b.pz(a.pos[2] + cc.pos[2]);
        b.scl(
            a.scl[0] * cc.scl[0] * v2[0] + q.scl[0],
            1.0,
            a.scl[2] * cc.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 30);
        let cc = s(src, 31);
        let q = c(9);
        let b = dst_mut(&mut d, 29);
        b.px(a.pos[0] + cc.pos[0] + q.pos[0]);
        b.pz(a.pos[2] + cc.pos[2]);
        b.scl(
            a.scl[0] * cc.scl[0] * v2[0] + q.scl[0],
            1.0,
            a.scl[2] * cc.scl[2] * v2[2] + q.scl[2],
        );
    }
    {
        let a = s(src, 32);
        let q = c(10);
        let b = dst_mut(&mut d, 30);
        b.px(a.pos[0]);
        b.pz(a.pos[2] + q.pos[2]);
        b.rot(a.rot[0], 0.0, a.rot[2]);
        b.scl(a.scl[0] * v2[0] + q.scl[0], 1.0, a.scl[2] * v2[2]);
    }
    {
        let a = s(src, 33);
        let q = c(11);
        let b = dst_mut(&mut d, 31);
        b.px(a.pos[0]);
        b.pz(a.pos[2] + q.pos[2]);
        b.rot(a.rot[0], 0.0, a.rot[2]);
        b.scl(a.scl[0] * v2[0] + q.scl[0], 1.0, a.scl[2] * v2[2]);
    }
    {
        let a = s(src, 34);
        let q = c(12);
        let b = dst_mut(&mut d, 32);
        b.px(a.pos[0]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0] * v2[0] + q.scl[0], 1.0, a.scl[2] * v2[2] + q.scl[2]);
    }
    {
        let a = s(src, 35);
        let q = c(13);
        let b = dst_mut(&mut d, 33);
        b.px(a.pos[0]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0] * v2[0] + q.scl[0], 1.0, a.scl[2] * v2[2] + q.scl[2]);
    }
    {
        let a = s(src, 36);
        let b = dst_mut(&mut d, 34);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0] * v2[0], 1.0, a.scl[2] * v2[2]);
    }
    {
        let a = s(src, 37);
        let b = dst_mut(&mut d, 35);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0] * v2[0], 1.0, a.scl[2] * v2[2]);
    }
    {
        let a = s(src, 38);
        dst_mut(&mut d, 36).scl(a.scl[0], 1.0, a.scl[2]);
        dst_mut(&mut d, 37).scl(a.scl[0], 1.0, a.scl[2]);
    }
    // bust L
    {
        let ty = s(src, 40);
        let a03 = s(src, 41);
        let a02 = s(src, 42);
        let lry = s(src, 43);
        let rx = s(src, 44);
        let b = dst_mut(&mut d, 39);
        b.py(ty.pos[1]);
        b.pz(a03.pos[2] + a02.pos[2] + lry.pos[2] + ty.pos[2] + rx.pos[2]);
        b.rot(rx.rot[0], 0.0, 0.0);
        let sz = s(src, 45);
        let tx = s(src, 46);
        let b = dst_mut(&mut d, 40);
        b.px(tx.pos[0]);
        b.py(sz.pos[1]);
        b.pz(sz.pos[2]);
        b.rot(sz.rot[0], lry.rot[1] + sz.rot[1], 0.0);
        b.scl(sz.scl[0], sz.scl[1], sz.scl[2]);
        let b02 = s(src, 47);
        let tz = s(src, 48);
        let b = dst_mut(&mut d, 41);
        b.py(b02.pos[1]);
        b.pz(b02.pos[2] + tz.pos[2]);
        b.rot(b02.rot[0], 0.0, 0.0);
        b.scl(b02.scl[0], b02.scl[1], b02.scl[2]);
        let b03 = s(src, 49);
        let b = dst_mut(&mut d, 42);
        b.py(b03.pos[1]);
        b.pz(b03.pos[2] + tz.pos[2] / 2.0);
        b.rot(b03.rot[0], 0.0, 0.0);
        b.scl(b03.scl[0], b03.scl[1], b03.scl[2]);
        let sh1 = s(src, 50);
        let b = dst_mut(&mut d, 43);
        b.px(sh1.pos[0]);
        b.py(sh1.pos[1]);
        b.pz(sh1.pos[2]);
        b.rot(sh1.rot[0], sh1.rot[1], 0.0);
        b.scl(sh1.scl[0], sh1.scl[1], tz.scl[2]);
        let sh2 = s(src, 51);
        let b = dst_mut(&mut d, 44);
        b.px(sh2.pos[0]);
        b.py(sh2.pos[1]);
        b.pz(sh2.pos[2]);
        b.rot(sh2.rot[0], sh2.rot[1], 0.0);
        b.scl(sh2.scl[0] * tz.scl[0], sh2.scl[1] * tz.scl[1], sh2.scl[2]);
        let sh3 = s(src, 52);
        let b = dst_mut(&mut d, 45);
        b.py(sh3.pos[1]);
        b.pz(sh3.pos[2]);
        b.rot(sh3.rot[0], 0.0, 0.0);
        b.scl(sh3.scl[0] * tz.scl[0], sh3.scl[1] * tz.scl[1], 1.0);
        let hit1 = s(src, 53);
        let hit2 = s(src, 54);
        let b = dst_mut(&mut d, 46);
        b.px(hit1.pos[0]);
        b.py(hit1.pos[1]);
        b.pz(hit1.pos[2] + hit2.pos[2]);
        b.scl(hit1.scl[0] * hit2.scl[0], hit1.scl[1] * hit2.scl[0], hit1.scl[2] * hit2.scl[0]);
        let nip = s(src, 55);
        let nipsz = s(src, 56);
        let dn = s(src, 57);
        let b = dst_mut(&mut d, 47);
        b.pz(nip.pos[2] + nipsz.pos[2]);
        b.scl(dn.scl[0], dn.scl[0], dn.scl[2]);
        let b = dst_mut(&mut d, 48);
        b.pz(-(nip.pos[2] - 0.01) * 1.2);
        b.scl(nip.scl[0] * nipsz.scl[0], nip.scl[1] * nipsz.scl[1], nip.scl[2] * nipsz.scl[2]);
        let n2 = s(src, 58);
        let b = dst_mut(&mut d, 49);
        b.scl(1.0 / nz(nip.scl[0]) * (n2.scl[0] * 1.2), 1.0 / nz(nip.scl[1]) * (n2.scl[1] * 1.2), 1.0 / nz(nip.scl[2]));
        let n15 = s(src, 59);
        let b = dst_mut(&mut d, 50);
        b.pz(0.0025 + n15.pos[2] - (nip.pos[2] - 0.01));
        b.scl(0.1 + n15.scl[1] * n15.scl[0], 0.1 + n15.scl[1] * n15.scl[0], 0.1 + n15.scl[2] * n15.scl[0]);
        let n02 = s(src, 60);
        let b = dst_mut(&mut d, 51);
        b.pz(0.004 + n02.pos[2] + nip.pos[0] + n2.pos[2]);
        b.scl(n02.scl[0] * n2.scl[0], n02.scl[1] * n2.scl[1], n02.scl[2] * n2.scl[2]);
        let accs = s(src, 61);
        let accsz = s(src, 62);
        let b = dst_mut(&mut d, 52);
        b.pz(accs.pos[2]);
        b.scl(
            accs.scl[0] / nz(n02.scl[0]) / nz(n2.scl[0]) * accsz.scl[0],
            accs.scl[1] / nz(n02.scl[1]) / nz(n2.scl[1]) * accsz.scl[1],
            accs.scl[2] / nz(n02.scl[2]) / nz(n2.scl[2]) * accsz.scl[2],
        );
    }
    // bust R (mirror of L with src 63/64)
    {
        let ty = s(src, 40);
        let a03 = s(src, 41);
        let a02 = s(src, 42);
        let lry = s(src, 43);
        let rx = s(src, 44);
        let rry = s(src, 63);
        let rtx = s(src, 64);
        let b = dst_mut(&mut d, 53);
        b.py(ty.pos[1]);
        b.pz(a03.pos[2] + a02.pos[2] + lry.pos[2] + ty.pos[2] + rx.pos[2]);
        b.rot(rx.rot[0], 0.0, 0.0);
        let sz = s(src, 45);
        let b = dst_mut(&mut d, 54);
        b.px(rtx.pos[0]);
        b.py(sz.pos[1]);
        b.pz(sz.pos[2]);
        b.rot(sz.rot[0], rry.rot[1] - sz.rot[1], 0.0);
        b.scl(sz.scl[0], sz.scl[1], sz.scl[2]);
        let b02 = s(src, 47);
        let tz = s(src, 48);
        let b = dst_mut(&mut d, 55);
        b.py(b02.pos[1]);
        b.pz(b02.pos[2] + tz.pos[2]);
        b.rot(b02.rot[0], 0.0, 0.0);
        b.scl(b02.scl[0], b02.scl[1], b02.scl[2]);
        let b03 = s(src, 49);
        let b = dst_mut(&mut d, 56);
        b.py(b03.pos[1]);
        b.pz(b03.pos[2] + tz.pos[2] / 2.0);
        b.rot(b03.rot[0], 0.0, 0.0);
        b.scl(b03.scl[0], b03.scl[1], b03.scl[2]);
        let sh1 = s(src, 50);
        let b = dst_mut(&mut d, 57);
        b.px(-sh1.pos[0]);
        b.py(sh1.pos[1]);
        b.pz(sh1.pos[2]);
        b.rot(sh1.rot[0], -sh1.rot[1], 0.0);
        b.scl(sh1.scl[0], sh1.scl[1], tz.scl[2]);
        let sh2 = s(src, 51);
        let b = dst_mut(&mut d, 58);
        b.px(-sh2.pos[0]);
        b.py(sh2.pos[1]);
        b.pz(sh2.pos[2]);
        b.rot(sh2.rot[0], -sh2.rot[1], 0.0);
        b.scl(sh2.scl[0] * tz.scl[0], sh2.scl[1] * tz.scl[1], sh2.scl[2]);
        let sh3 = s(src, 52);
        let b = dst_mut(&mut d, 59);
        b.py(sh3.pos[1]);
        b.pz(sh3.pos[2]);
        b.rot(sh3.rot[0], 0.0, 0.0);
        b.scl(sh3.scl[0] * tz.scl[0], sh3.scl[1] * tz.scl[1], 1.0);
        let hit1 = s(src, 53);
        let hit2 = s(src, 54);
        let b = dst_mut(&mut d, 60);
        b.px(-hit1.pos[0]);
        b.py(hit1.pos[1]);
        b.pz(hit1.pos[2] + hit2.pos[2]);
        b.scl(hit1.scl[0] * hit2.scl[0], hit1.scl[1] * hit2.scl[0], hit1.scl[2] * hit2.scl[0]);
        let nip = s(src, 55);
        let nipsz = s(src, 56);
        let dn = s(src, 57);
        let b = dst_mut(&mut d, 61);
        b.pz(nip.pos[2] + nipsz.pos[2]);
        b.scl(dn.scl[0], dn.scl[0], dn.scl[2]);
        let b = dst_mut(&mut d, 62);
        b.pz(-(nip.pos[2] - 0.01) * 1.2);
        b.scl(nip.scl[0] * nipsz.scl[0], nip.scl[1] * nipsz.scl[1], nip.scl[2] * nipsz.scl[2]);
        let n2 = s(src, 58);
        let b = dst_mut(&mut d, 63);
        b.scl(1.0 / nz(nip.scl[0]) * (n2.scl[0] * 1.2), 1.0 / nz(nip.scl[1]) * (n2.scl[1] * 1.2), 1.0 / nz(nip.scl[2]));
        let n15 = s(src, 59);
        let b = dst_mut(&mut d, 64);
        b.pz(0.0025 + n15.pos[2] - (nip.pos[2] - 0.01));
        b.scl(0.1 + n15.scl[1] * n15.scl[0], 0.1 + n15.scl[1] * n15.scl[0], 0.1 + n15.scl[2] * n15.scl[0]);
        let n02 = s(src, 60);
        let b = dst_mut(&mut d, 65);
        b.pz(0.004 + n02.pos[2] + nip.pos[0] + n2.pos[2]);
        b.scl(n02.scl[0] * n2.scl[0], n02.scl[1] * n2.scl[1], n02.scl[2] * n2.scl[2]);
        let accs = s(src, 61);
        let accsz = s(src, 62);
        let b = dst_mut(&mut d, 66);
        b.pz(accs.pos[2]);
        b.scl(
            accs.scl[0] / nz(n02.scl[0]) / nz(n2.scl[0]) * accsz.scl[0],
            accs.scl[1] / nz(n02.scl[1]) / nz(n2.scl[1]) * accsz.scl[1],
            accs.scl[2] / nz(n02.scl[2]) / nz(n2.scl[2]) * accsz.scl[2],
        );
    }
    // ETC: hit bones + skirt deformation (dst 67..85).
    let n83 = {
        let r = s(src, 94).rot[0];
        if r > 180.0 { r - 360.0 } else { r }
    };
    let n84 = {
        let r = s(src, 97).rot[0];
        if r > 180.0 { r - 360.0 } else { r }
    };
    {
        let a = s(src, 65);
        let b65 = s(src, 66);
        let c67 = s(src, 67);
        let d68 = s(src, 68);
        let e69 = s(src, 69);
        let f70 = s(src, 70);
        let sx = a.scl[0] * c67.scl[0] * d68.scl[0] * v2[0] * f70.scl[0];
        let sy = a.scl[1] * c67.scl[1] * d68.scl[1] * v2[1] * f70.scl[0];
        let sz = a.scl[2] * c67.scl[2] * d68.scl[2] * v2[2] * f70.scl[0];
        let py = c67.pos[1] + d68.pos[1] + f70.pos[1];
        let pz = a.pos[2] + c67.pos[2] + f70.pos[2];
        let b = dst_mut(&mut d, 67);
        b.px(a.pos[0] + b65.pos[0] + c67.pos[0] + d68.pos[0] + e69.pos[0] + f70.pos[0]);
        b.py(py);
        b.pz(pz);
        b.scl(sx, sy, sz);
        let b = dst_mut(&mut d, 68);
        b.px(-(a.pos[0] + b65.pos[0] + c67.pos[0] + d68.pos[0] + e69.pos[0] + f70.pos[0]));
        b.py(py);
        b.pz(pz);
        b.scl(sx, sy, sz);
    }
    {
        let a = s(src, 71);
        let b71 = s(src, 72);
        let c73 = s(src, 73);
        let d74 = s(src, 74);
        let e75 = s(src, 75);
        let f76 = s(src, 76);
        let b = dst_mut(&mut d, 69);
        b.py(a.pos[1] + b71.pos[1] + c73.pos[1] + d74.pos[1] + e75.pos[1] + f76.pos[1]);
        b.pz(b71.pos[2] + c73.pos[2]);
        b.scl(
            a.scl[0] * b71.scl[0] * v2[0] * c73.scl[0] * f76.scl[0] * e75.scl[0],
            a.scl[0] * b71.scl[0] * v2[1] * c73.scl[1] * f76.scl[1] * e75.scl[1],
            a.scl[0] * b71.scl[0] * v2[2] * c73.scl[2] * f76.scl[2] * e75.scl[2],
        );
    }
    {
        let a = s(src, 77);
        let b74 = s(src, 74);
        let c78 = s(src, 78);
        let d79 = s(src, 79);
        let b = dst_mut(&mut d, 70);
        b.px(a.pos[0]);
        b.py(a.pos[1] + b74.pos[1] * 2.0 + c78.pos[1] + d79.pos[1]);
        b.pz(a.pos[2] + c78.pos[2] + d79.pos[2]);
        b.scl(
            a.scl[0] * c78.scl[0] * v2[0] * d79.scl[0],
            a.scl[1] * c78.scl[1] * v2[1] * d79.scl[1],
            a.scl[2] * c78.scl[2] * v2[2] * d79.scl[2],
        );
    }
    {
        let a = s(src, 80);
        let b81 = s(src, 81);
        let c82 = s(src, 82);
        let b = dst_mut(&mut d, 71);
        b.py(a.pos[1] + b81.pos[1] + c82.pos[1]);
        b.pz(c82.pos[2]);
        b.scl(
            b81.scl[0] * c82.scl[0] * v2[0],
            b81.scl[1] * c82.scl[1] * v2[1],
            b81.scl[2] * c82.scl[2] * v2[2],
        );
    }
    {
        let a = s(src, 83);
        let b84 = s(src, 84);
        let c85 = s(src, 85);
        let px = a.pos[0] + b84.pos[0] + c85.pos[0];
        let py = a.pos[1] + b84.pos[1] + c85.pos[1];
        let pz = a.pos[2] + b84.pos[2];
        let sx = a.scl[0] * b84.scl[0] * c85.scl[0] * v2[0];
        let sy = a.scl[1] * b84.scl[1] * c85.scl[1] * v2[1];
        let sz = a.scl[2] * b84.scl[2] * c85.scl[2] * v2[2];
        let b = dst_mut(&mut d, 72);
        b.px(px);
        b.py(py);
        b.pz(pz);
        b.scl(sx, sy, sz);
        let b = dst_mut(&mut d, 73);
        b.px(-px);
        b.py(py);
        b.pz(pz);
        b.scl(sx, sy, sz);
    }
    {
        let a = s(src, 86);
        let b87 = s(src, 87);
        let c88 = s(src, 88);
        let sy = a.scl[1] * b87.scl[1] * c88.scl[1] * v2[0];
        let sz = a.scl[2] * b87.scl[2] * c88.scl[2] * v2[0];
        dst_mut(&mut d, 74).scl(1.0, sy, sz);
        dst_mut(&mut d, 75).scl(1.0, sy, sz);
    }
    {
        let a = s(src, 89);
        let b90 = s(src, 90);
        let b = dst_mut(&mut d, 76);
        b.pz(a.pos[2] + b90.pos[2]);
        b.scl(
            a.scl[0] * b90.scl[0] * v2[0],
            a.scl[1] * b90.scl[1] * v2[1],
            a.scl[2] * b90.scl[2] * v2[2],
        );
    }
    dst_mut(&mut d, 77).scl(v2[0], v2[1], v2[2]);
    {
        let a = s(src, 91);
        let b92 = s(src, 92);
        let c93 = s(src, 93);
        let b = dst_mut(&mut d, 78);
        b.px(a.pos[0] + b92.pos[0]);
        b.pz(a.pos[2] + b92.pos[2] + c93.pos[2]);
        b.rot(a.rot[0] + b92.rot[0] + b92.rot[2] + n83, a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 95);
        let b96 = s(src, 96);
        let c93 = s(src, 93);
        let b = dst_mut(&mut d, 79);
        b.px(a.pos[0] + b96.pos[0] - c93.pos[0] * 0.5);
        b.pz(a.pos[2] + b96.pos[2] + c93.pos[2]);
        b.rot(a.rot[0] + b96.rot[0] + b96.rot[2] + n83 * 0.6 + n84 * 0.6, a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 98);
        let b99 = s(src, 99);
        let c100 = s(src, 100);
        let c93 = s(src, 93);
        let b = dst_mut(&mut d, 80);
        b.px(a.pos[0] + b99.pos[0] + c100.pos[0] * 0.5 - c93.pos[0]);
        b.pz(a.pos[2] + b99.pos[2]);
        b.rot(a.rot[0] + b99.rot[0] + b99.rot[2] + n84, a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 101);
        let b102 = s(src, 102);
        let c100 = s(src, 100);
        let b = dst_mut(&mut d, 81);
        b.px(a.pos[0] + b102.pos[0] + c100.pos[0]);
        b.pz(a.pos[2] + b102.pos[2] + c100.pos[2]);
        b.rot(a.rot[0] + b102.rot[0] + b102.rot[2] + n83 * 0.6 + n84 * 0.6 + c100.rot[0], a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 103);
        let b104 = s(src, 104);
        let c100 = s(src, 100);
        let b = dst_mut(&mut d, 82);
        b.px(a.pos[0] + b104.pos[0]);
        b.pz(a.pos[2] + b104.pos[2] + c100.pos[2]);
        b.rot(a.rot[0] + b104.rot[0] + b104.rot[2] + n83 + c100.rot[0], a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 105);
        let b106 = s(src, 106);
        let c100 = s(src, 100);
        let b = dst_mut(&mut d, 83);
        b.px(a.pos[0] + b106.pos[0] - c100.pos[0]);
        b.pz(a.pos[2] + b106.pos[2] + c100.pos[2]);
        b.rot(a.rot[0] + b106.rot[0] + b106.rot[2] + n83 * 0.6 + n84 * 0.6 + c100.rot[0], a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 107);
        let b108 = s(src, 108);
        let c100 = s(src, 100);
        let c93 = s(src, 93);
        let b = dst_mut(&mut d, 84);
        b.px(a.pos[0] + b108.pos[0] - c100.pos[0] * 0.5 + c93.pos[0]);
        b.pz(a.pos[2] + b108.pos[2]);
        b.rot(a.rot[0] + b108.rot[0] + b108.rot[2] + n84, a.rot[1], a.rot[2]);
    }
    {
        let a = s(src, 109);
        let b110 = s(src, 110);
        let c93 = s(src, 93);
        let b = dst_mut(&mut d, 85);
        b.px(a.pos[0] + b110.pos[0] + c93.pos[0] * 0.5);
        b.pz(a.pos[2] + b110.pos[2] + c93.pos[2]);
        b.rot(a.rot[0] + b110.rot[0] + b110.rot[2] + n83 * 0.6 + n84 * 0.6, a.rot[1], a.rot[2]);
    }
    // UpdateAlways kokan
    dst_mut(&mut d, 38).pz(s(src, 39).pos[2]);
    d
}

fn head_update(
    src: &[Bone],
    parent_scale: [f32; 3],
    head_correct: f32,
    type_bone: i32,
) -> HashMap<i32, Dst> {
    let mut d: HashMap<i32, Dst> = HashMap::new();
    // StudioEditor's `KoikatsuShapeApplicator.ApplyHead` copies the sampled
    // source straight onto each bone (only cf_J_FaceBase is normalised). Our
    // hand-ported `Update` formula diverges on the cheek/chin bones, so allow
    // switching to the direct copy for comparison.
    if std::env::var("OK_SRC_DIRECT").is_ok() {
        let num = if type_bone == 0 { 1.0 } else { head_correct };
        let px = nz(parent_scale[0]);
        let py = nz(parent_scale[1]);
        let pz = nz(parent_scale[2]);
        let n2 = py * num;
        let s0 = s(src, 0);
        {
            let b = dst_mut(&mut d, 0);
            b.px(s0.pos[0]);
            b.py(s0.pos[1]);
            b.pz(s0.pos[2]);
            b.scl(n2 / px + (s0.scl[0] - 1.0), n2 / py, n2 / pz);
        }
        for i in 1..src.len() {
            let a = src[i];
            let b = dst_mut(&mut d, i as i32);
            b.px(a.pos[0]);
            b.py(a.pos[1]);
            b.pz(a.pos[2]);
            b.rot(a.rot[0], a.rot[1], a.rot[2]);
            b.scl(a.scl[0], a.scl[1], a.scl[2]);
        }
        return d;
    }
    // ShapeHeadInfoFemale.Update dst0 (cf_J_FaceBase) normalises against the
    // parent's lossy scale so body head scaling is not compounded.
    {
        let num = if type_bone == 0 { 1.0 } else { head_correct };
        let px = nz(parent_scale[0]);
        let py = nz(parent_scale[1]);
        let pz = nz(parent_scale[2]);
        let n2 = py * num;
        let s0 = s(src, 0);
        dst_mut(&mut d, 0).scl(n2 / px + (s0.scl[0] - 1.0), n2 / py, n2 / pz);
    }
    dst_mut(&mut d, 1).pz(s(src, 1).pos[2]);
    {
        let a = s(src, 2);
        let b = dst_mut(&mut d, 2);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
    }
    {
        let a = s(src, 3);
        let b = dst_mut(&mut d, 3);
        b.py(a.pos[1]);
        b.scl(a.scl[0], a.scl[1], a.scl[2]);
    }
    dst_mut(&mut d, 4).pz(s(src, 4).pos[2]);
    dst_mut(&mut d, 5).sx(s(src, 5).scl[0]);
    dst_mut(&mut d, 5).sy(1.0);
    dst_mut(&mut d, 5).sz(1.0);
    for id in [6, 7] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.px(a.pos[0]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0], 1.0, 1.0);
    }
    {
        let a = s(src, 8);
        let b = dst_mut(&mut d, 8);
        b.py(a.pos[1]);
        b.scl(a.scl[0], a.scl[1], a.scl[2]);
    }
    for id in [9, 10] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.pz(a.pos[2]);
        b.sz(a.scl[2]);
    }
    {
        let a = s(src, 11);
        let b = dst_mut(&mut d, 11);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0], 1.0, 1.0);
    }
    {
        let a = s(src, 12);
        let b = dst_mut(&mut d, 12);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
        b.scl(a.scl[0], 1.0, 1.0);
    }
    dst_mut(&mut d, 13).pz(s(src, 13).pos[2]);
    {
        let a = s(src, 14);
        let b = dst_mut(&mut d, 14);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
    }
    dst_mut(&mut d, 15).pz(s(src, 15).pos[2]);
    dst_mut(&mut d, 16).py(s(src, 16).pos[1]);
    for id in [17, 18] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.px(a.pos[0]);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
    }
    for id in [19, 20] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.py(a.pos[1]);
        b.rot(0.0, 0.0, a.rot[2]);
    }
    {
        let a = s(src, 21);
        let b = dst_mut(&mut d, 21);
        b.py(a.pos[1]);
        b.rot(a.rot[0], 0.0, 0.0);
    }
    for id in [22, 23] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.px(a.pos[0]);
        b.scl(a.scl[0], a.scl[1], 1.0);
    }
    dst_mut(&mut d, 24).pz(s(src, 24).pos[2]);
    dst_mut(&mut d, 25).px(s(src, 25).pos[0]);
    dst_mut(&mut d, 26).px(s(src, 26).pos[0]);
    for id in 27..=40 {
        dst_mut(&mut d, id).py(s(src, id as usize).pos[1]);
    }
    for id in [41, 42] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.px(a.pos[0]);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
        b.rot(0.0, 0.0, a.rot[2]);
    }
    for id in 43..=46 {
        dst_mut(&mut d, id).rot(0.0, 0.0, s(src, id as usize).rot[2]);
    }
    for id in [47, 48] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.rot(0.0, a.rot[1], a.rot[2]);
        b.scl(a.scl[0], a.scl[1], a.scl[2]);
    }
    for id in [49, 50] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.px(a.pos[0]);
        b.py(a.pos[1]);
        b.scl(a.scl[0], a.scl[1], a.scl[2]);
    }
    dst_mut(&mut d, 51).py(s(src, 51).pos[1]);
    dst_mut(&mut d, 52).py(s(src, 52).pos[1]);
    {
        let a = s(src, 53);
        let b = dst_mut(&mut d, 53);
        b.py(a.pos[1]);
        b.pz(a.pos[2]);
    }
    for id in [54, 55] {
        let a = s(src, id);
        let b = dst_mut(&mut d, id as i32);
        b.px(a.pos[0]);
        b.py(a.pos[1]);
    }
    dst_mut(&mut d, 56).pz(s(src, 56).pos[2]);
    dst_mut(&mut d, 57).pz(s(src, 57).pos[2]);
    dst_mut(&mut d, 58).pz(s(src, 58).pos[2]);
    d
}

pub(crate) fn load_keys_named(game_root: &Path, bundle: &str, asset: &str) -> HashMap<String, Vec<Keyframe>> {
    let path = game_root.join("abdata").join(bundle);
    for (name, bytes) in text_assets_from_bundle(&path) {
        if name.eq_ignore_ascii_case(asset) {
            return load_anm_keys(&bytes);
        }
    }
    HashMap::new()
}

fn facebase_parent_scale(skel: &Skeleton) -> [f32; 3] {
    let id = skel
        .tfs
        .iter()
        .find(|(_, t)| t.name.eq_ignore_ascii_case("cf_J_FaceBase"))
        .map(|(id, _)| *id);
    if let Some(id) = id {
        if let Some(p) = skel
            .tfs
            .get(&id)
            .and_then(|t| t.parent)
            .filter(|p| *p != 0 && *p != id)
        {
            return world_scale(&crate::pose::world_of_id(p, &skel.tfs));
        }
    }
    [1.0; 3]
}

/// Applies ShapeBodyInfoFemale. Returns the world scale of `cf_s_head`
/// (needed by ShapeHeadInfoFemale dst0).
pub fn apply_body_shape(
    game_root: &Path,
    skel: &mut Skeleton,
    shape_body: &[f32],
    correct: &[CorrectInfo],
    sex: i32,
    type_bone: i32,
    nip_stand_rate: f32,
) -> [f32; 3] {
    let keys = load_keys_named(game_root, "chara/oo_base.unity3d", "cf_anmShapeBody");
    apply_body_shape_keys(skel, &keys, shape_body, correct, sex, type_bone, nip_stand_rate)
}

pub fn apply_body_shape_keys(
    skel: &mut Skeleton,
    keys: &HashMap<String, Vec<Keyframe>>,
    shape_body: &[f32],
    correct: &[CorrectInfo],
    sex: i32,
    type_bone: i32,
    nip_stand_rate: f32,
) -> [f32; 3] {
    apply_body_shape_keys_masked(skel,keys,shape_body,correct,sex,type_bone,nip_stand_rate,[[true;9];2])
}

pub(crate) fn apply_body_shape_keys_masked(
    skel:&mut Skeleton, keys:&HashMap<String,Vec<Keyframe>>,shape_body:&[f32],correct:&[CorrectInfo],
    sex:i32,type_bone:i32,nip_stand_rate:f32, enabled:[[bool;9];2],
) -> [f32;3] {
    if keys.is_empty() {
        eprintln!("ok-extract: cf_anmShapeBody missing");
        return [1.0; 3];
    }
    let mut values = shape_body.to_vec();
    if let Some(v) = values.get_mut(13) {
        *v = lerp(*v, 1.0, nip_stand_rate);
    }
    // hiPoly path only; low-poly [0.2,0.8] remap is not used by the viewer.
    let head_size = if sex == 0 { 0.91 } else { 1.0 };
    let neck_size = if sex == 0 { 0.91 } else { 1.0 };
    static ROWS:std::sync::OnceLock<Vec<CatRow>>=std::sync::OnceLock::new();
    let rows = ROWS.get_or_init(||parse_cat(BODY_CAT,SRC_BODY));
    let src = change_values(&keys, &rows, &values, SRC_BODY.len());
    if std::env::var("OK_DIAG").is_ok() {
        for i in [45usize, 47, 49, 41, 42] {
            let b = s(&src, i);
            eprintln!(
                "ok-extract: DIAG bustsrc[{i}] pos={:?} rot={:?} scl={:?}",
                b.pos, b.rot, b.scl
            );
        }
    }
    if std::env::var("OK_DIAG").is_ok() {
        for i in 0..9 {
            let b = s(&src, i);
            eprintln!(
                "ok-extract: DIAG src[{i}] pos=[{:.4},{:.4},{:.4}] scl=[{:.4},{:.4},{:.4}]",
                b.pos[0], b.pos[1], b.pos[2], b.scl[0], b.scl[1], b.scl[2]
            );
        }
    }
    let mut dst = body_update(&src, correct, head_size, neck_size, type_bone);
    // ChaControl.UpdateShapeBody first updates mask7 with the card, then
    // replaces shape IDs5..13 and updates only the selected breast (mask1/2).
    for side in 0..2 {
        if enabled[side].iter().all(|v|*v) {continue;}
        let mut masked=values.clone();
        for i in 0..9 {
            if !enabled[side][i] {if let Some(v)=masked.get_mut(5+i) {*v=if i==6 {0.0}else{0.5};}}
        }
        let source=change_values(keys,&rows,&masked,SRC_BODY.len());
        let breast=body_update(&source,correct,head_size,neck_size,type_bone);
        let range=if side==0 {39..=52}else{53..=66};
        for id in range {if let Some(value)=breast.get(&id) {dst.insert(id,*value);}}
    }
    if let Ok(s) = std::env::var("OK_SKIP_DST") {
        for part in s.split(',') {
            if let Ok(i) = part.trim().parse::<i32>() {
                dst.remove(&i);
            }
        }
    }
    let only = apply_dst_locals(skel, DST_BODY, &dst);
    if std::env::var("OK_NOFIX").is_err() {
    let (sh_l, sh_r) = if type_bone == 0 {
        (-0.01563369f32, 0.01560147f32)
    } else {
        (
            correct.get(30).map(|c| c.pos[0]).unwrap_or(0.0),
            correct.get(31).map(|c| c.pos[0]).unwrap_or(0.0),
        )
    };
    // UpdateAlways shoulder fix
    skel.apply_local_in(
        "cf_d_shoulder_L",
        [sh_l, 0.0, 0.0],
        [0.0; 3],
        [1.0; 3],
        [true, false, false],
        false,
        [false; 3],
        only.as_ref(),
    );
    skel.apply_local_in(
        "cf_d_shoulder_R",
        [sh_r, 0.0, 0.0],
        [0.0; 3],
        [1.0; 3],
        [true, false, false],
        false,
        [false; 3],
        only.as_ref(),
    );
    }
    skel.rebuild_worlds();
    let head_scale = skel
        .world_tf("cf_s_head")
        .map(|m| world_scale(&m))
        .unwrap_or([1.0; 3]);
    if std::env::var("OK_SHAPE_DIAGNOSTICS").ok().as_deref()==Some("1") {
    eprintln!("ok-extract: body bone morph keys={} src={} dst={} type_bone={type_bone} correct_len={}", keys.len(), src.len(), dst.len(), correct.len());
    for n in [
        "cf_s_bust01_L",
        "cf_s_bust02_L",
        "cf_s_bust03_L",
        "cf_d_bust01_L",
        "cf_d_bust02_L",
        "cf_d_bust03_L",
        "cf_s_bust00_L",
        "cf_s_head",
        "cf_n_height",
        "cf_s_shoulder02_L",
        "cf_s_shoulder02_R",
        "cf_s_spine03",
        "cf_s_arm01_L",
        "cf_d_shoulder_L",
        "cf_d_shoulder_R",
    ] {
        if let Some(tf) = skel.tfs.values().find(|t| t.name == n) {
            let par = tf
                .parent
                .and_then(|p| skel.tfs.get(&p))
                .map(|t| t.name.clone())
                .unwrap_or_default();
            eprintln!(
                "ok-extract: bone {} parent={} pos={:?} scl={:?}",
                n, par, tf.pos, tf.scale
            );
        } else {
            eprintln!("ok-extract: bone {n} missing from skeleton");
        }
    }
    }
    head_scale
}

/// ShapeBodyInfoFemale.UpdateAlways runs after Animator. Only these local
/// position channels are restored every frame; all other authored helpers
/// retain their clip values until their own component runs.
pub(crate) fn apply_body_shape_always(skel: &mut Skeleton, shaped: &Skeleton) {
    apply_body_shape_always_locals(skel, shaped);
    skel.rebuild_worlds();
}

/// `apply_body_shape_always` without publishing the world matrices (the caller rebuilds them once after more local edits).
pub(crate) fn apply_body_shape_always_locals(skel: &mut Skeleton, shaped: &Skeleton) {
    let roots = tree_ids(skel);
    let fix_shoulders = std::env::var("OK_NOFIX").is_err();
    for (id, tf) in &mut skel.tfs {
        // The name first: only the three bones named here need the tree membership test.
        let axis = match tf.name.as_str() {
            "cf_d_kokan" => 2,
            "cf_d_shoulder_L" | "cf_d_shoulder_R" if fix_shoulders => 0,
            _ => continue,
        };
        if roots.as_ref().is_some_and(|ids| !ids.contains(id)) { continue; }
        if let Some(source) = shaped.tfs.get(id) { tf.pos[axis] = source.pos[axis]; }
    }
}

#[cfg(test)]
pub(crate) fn breast_written_channels()->Vec<(&'static str,[bool;3],bool,[bool;3])> {
    let dst=body_update(&[],&[],1.,1.,0);
    (39..=66).map(|i|{let d=dst[&i];(DST_BODY[i as usize],d.set_pos,d.set_rot,d.set_scl)}).collect()
}

pub(crate) fn load_head_keys(head_bundle: &Path) -> HashMap<String, Vec<Keyframe>> {
    let assets = text_assets_from_bundle(head_bundle);
    let mut keys = HashMap::new();
    for (name, bytes) in &assets {
        if name.to_ascii_lowercase().contains("anm") || name.to_ascii_lowercase().contains("shape") {
            eprintln!("ok-extract: head text asset {}", name);
            keys = load_anm_keys(bytes);
            if !keys.is_empty() {
                break;
            }
        }
    }
    if keys.is_empty() {
        for (name, bytes) in &assets {
            keys = load_anm_keys(bytes);
            if !keys.is_empty() {
                eprintln!("ok-extract: using head keys from {}", name);
                break;
            }
        }
    }
    keys
}

/// ChaControl.InitShapeFace passes the list row's ShapeAnime asset name.
/// A bundle may contain several shape TextAssets; taking the first one can
/// silently apply the wrong head curve set to a sideloader prefab.
pub(crate) fn load_head_keys_named(head_bundle: &Path, asset: &str) -> HashMap<String, Vec<Keyframe>> {let _spot=crate::profile::spot("load_head_keys_named");
    head_keys_named_in(text_assets_from_bundle(head_bundle), asset)
}

/// `load_head_keys_named` for a head bundle in memory (a sideloader head's).
pub(crate) fn load_head_keys_named_bytes(head_bundle: &[u8], asset: &str) -> HashMap<String, Vec<Keyframe>> {let _spot=crate::profile::spot("load_head_keys_named");
    head_keys_named_in(crate::list::text_assets_from_bundle_bytes(head_bundle), asset)
}

fn head_keys_named_in(assets: Vec<(String, Vec<u8>)>, asset: &str) -> HashMap<String, Vec<Keyframe>> {
    assets.into_iter().find(|(name, _)| name.eq_ignore_ascii_case(asset)).map(|(_, bytes)| load_anm_keys(&bytes)).unwrap_or_default()
}

pub fn apply_head_shape(
    head_bundle: &Path,
    skel: &mut Skeleton,
    shape_face: &[f32],
    parent_scale: [f32; 3],
    head_correct: f32,
    type_bone: i32,
) {
    let keys = load_head_keys(head_bundle);
    apply_head_shape_keys(skel, &keys, shape_face, parent_scale, head_correct, type_bone);
}

pub fn apply_head_shape_keys(
    skel: &mut Skeleton,
    keys: &HashMap<String, Vec<Keyframe>>,
    shape_face: &[f32],
    parent_scale: [f32; 3],
    head_correct: f32,
    type_bone: i32,
) {
    if keys.is_empty() {
        eprintln!("ok-extract: face shape keys missing");
        return;
    }
    let internal = facebase_parent_scale(skel);
    let combined = [
        parent_scale[0] * internal[0],
        parent_scale[1] * internal[1],
        parent_scale[2] * internal[2],
    ];
    static ROWS:std::sync::OnceLock<Vec<CatRow>>=std::sync::OnceLock::new();
    let rows = ROWS.get_or_init(||parse_cat(HEAD_CAT,DST_HEAD));
    let src = change_values(keys, &rows, shape_face, DST_HEAD.len());
    if std::env::var("OK_DIAG").is_ok() {
        for (id, b) in src.iter().enumerate() {
            eprintln!(
                "ok-extract: ALLSRC {id} {:.6} {:.6} {:.6} | {:.6} {:.6} {:.6}",
                b.pos[0], b.pos[1], b.pos[2], b.scl[0], b.scl[1], b.scl[2]
            );
        }
        for row in rows {
            if matches!(row.id, 3 | 11 | 15 | 27 | 29) {
                let val = shape_face.get(row.category as usize).copied().unwrap_or(-1.0);
                let ks = keys.get(&row.name).map(|k| k.len()).unwrap_or(0);
                let first = keys
                    .get(&row.name)
                    .and_then(|k| k.first())
                    .map(|k| format!("pos={:?} scl={:?}", k.pos, k.scl))
                    .unwrap_or_default();
                eprintln!(
                    "ok-extract: HEADROW id={} cat={} name={} val={:.4} nkeys={} get={:?} use_pos={:?} use_scl={:?} first={first}",
                    row.id, row.category, row.name, val, ks, row.get, row.use_pos, row.use_scl
                );
                if row.category == 8 || row.category == 24 || row.category == 40 || row.category == 11 {
                    if let Some(kk) = keys.get(&row.name) {
                        let s: Vec<String> = kk
                            .iter()
                            .map(|k| format!("{:.4}/{:.4}", k.pos[1], k.pos[2]))
                            .collect();
                        eprintln!("ok-extract: KEYCAT{} p1/p2 {:?}", row.category, s);
                    }
                }
                if row.category == 2 || row.category == 3 {
                    if let Some(kk) = keys.get(&row.name) {
                        let s: Vec<String> = kk
                            .iter()
                            .map(|k| format!("{:.3}/{:.3}", k.scl[0], k.pos[1]))
                            .collect();
                        eprintln!("ok-extract: KEYCAT{} sclx/posy {:?}", row.category, s);
                    }
                }
            }
        }
    }
    let dst = head_update(&src, combined, head_correct, type_bone);
    apply_dst(skel, DST_HEAD, &dst);
    if std::env::var("OK_SHAPE_DIAGNOSTICS").ok().as_deref()==Some("1") {eprintln!(
        "ok-extract: head bone morph keys={} dst={} parent_scale={:?}",
        keys.len(),
        dst.len(),
        combined
    );}
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn batch_shape_writes_match_scan_oracle_with_duplicate_names_and_reparenting() {
        use crate::pose::{Tf,ident};
        let mut skel=Skeleton {names:vec![],worlds:vec![],rest:vec![ident();5],smr_count:5,
            tfs:HashMap::new(),bone_ids:(1..=5).collect(),source_mesh:-812};
        for (id,name,parent) in [(1,"cf_j_root",None),(2,"leaf",Some(1)),(3,"leaf",Some(1)),(4,"leaf",None),(5,"other",Some(1))] {
            skel.tfs.insert(id,Tf{name:name.into(),parent,sibling_index:None,pos:[0.2,0.3,0.4],rot:[0.,0.,0.,1.],scale:[1.;3]});
        }
        let mut dst=HashMap::new();let mut a=Dst::default();a.py(-2.);a.rot(13.,-27.,92.);a.sx(0.7);dst.insert(0,a);
        let mut b=Dst::default();b.pz(9.);b.sy(1.6);dst.insert(1,b);
        let mut c=Dst::default();c.px(3.);dst.insert(2,c);
        let names=["leaf","leaf","other"];
        for parent in [None,Some(1),Some(5)] {
            skel.tfs.get_mut(&4).unwrap().parent=parent;
            let mut oracle=skel.clone();let only=tree_ids(&oracle);
            for(id,d)in &dst {oracle.apply_local_in(names[*id as usize],d.pos,d.rot,d.scl,d.set_pos,d.set_rot,d.set_scl,only.as_ref());}
            oracle.rebuild_worlds();apply_dst(&mut skel,&names,&dst);
            for(id,tf)in &skel.tfs {let expected=&oracle.tfs[id];assert_eq!((tf.pos,tf.rot,tf.scale),(expected.pos,expected.rot,expected.scale));}
            assert_eq!(skel.worlds,oracle.worlds);
        }
    }

    fn keys_scale(first: f32, last: f32) -> Vec<Keyframe> {
        vec![
            Keyframe {
                pos: [0.0; 3],
                rot: [0.0; 3],
                scl: [first, first, first],
            },
            Keyframe {
                pos: [0.0; 3],
                rot: [0.0; 3],
                scl: [last, last, last],
            },
        ]
    }

    fn keys_pos_y(first: f32, last: f32) -> Vec<Keyframe> {
        vec![
            Keyframe {
                pos: [0.0, first, 0.0],
                rot: [0.0; 3],
                scl: [1.0; 3],
            },
            Keyframe {
                pos: [0.0, last, 0.0],
                rot: [0.0; 3],
                scl: [1.0; 3],
            },
        ]
    }

    #[test]
    fn slider_unlocker_extrapolates_scale_below_zero() {
        // SliderUnlocker.SliderMath.CalculateScale: first + (last-first)*rate.
        // cf_J_FaceUp_ty keys are [0.92, 1.0]; card slider 3 = -1.0 → dump 0.84.
        let b = get_info(&keys_scale(0.92, 1.0), -1.0, [false, false, true], "cf_J_FaceUp_ty");
        assert!(
            (b.scl[0] - 0.84).abs() < 1e-5,
            "expected 0.84, got {}",
            b.scl[0]
        );
    }

    #[test]
    fn slider_unlocker_extrapolates_scale_above_one() {
        let b = get_info(&keys_scale(0.92, 1.0), 1.5, [false, false, true], "cf_J_FaceUp_ty");
        assert!(
            (b.scl[0] - 1.04).abs() < 1e-5,
            "expected 1.04, got {}",
            b.scl[0]
        );
    }

    #[test]
    fn slider_unlocker_extrapolates_position_below_zero() {
        let b = get_info(&keys_pos_y(0.005, 0.014), -1.0, [true, false, false], "cf_J_Eye05_s_L");
        let expect = 0.005 + (0.014 - 0.005) * -1.0;
        assert!(
            (b.pos[1] - expect).abs() < 1e-5,
            "expected {expect}, got {}",
            b.pos[1]
        );
    }

    #[test]
    fn in_range_scale_still_lerps() {
        let b = get_info(&keys_scale(0.92, 1.0), 0.5, [false, false, true], "cf_J_FaceUp_ty");
        assert!(
            (b.scl[0] - 0.96).abs() < 1e-5,
            "expected 0.96, got {}",
            b.scl[0]
        );
    }
}
