//! DynamicBone.cs (kks_decomp) particle hang for hair / soft bones.
//! SetupParticles → UpdateParticles1 (gravity) → UpdateParticles2 (length) →
//! ApplyParticlesToTransforms.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use std::collections::HashMap;
use std::path::Path;

use unity_asset_binary::asset::{parse_serialized_file, SerializedFile};
use unity_asset_binary::bundle::load_bundle;
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;

use crate::pose::{mat_inv_pub, mat_mul_pub, Skeleton, Tf};

pub mod ver02;
pub mod game_colliders;
pub mod implant;
#[cfg(test)]
mod ordinary_tests;
#[cfg(test)]
mod accessory_native_tests;
#[cfg(test)]
mod body_rebind_tests;
#[cfg(test)]
mod dynamic_scale_native_tests;
#[cfg(test)]
mod collider_native_tests;

type Mat4 = crate::pose::Mat4;

fn as_f32(v: &UnityValue) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn pptr(v: &UnityValue) -> Option<i64> {
    crate::unity::references::PPtr::local(v)
}

fn behaviour_enabled(value: Option<&UnityValue>) -> bool {
    match value {
        Some(UnityValue::Bool(enabled)) => *enabled,
        Some(value) => value.as_i64() != Some(0),
        None => true,
    }
}

fn vec3(v: &UnityValue) -> [f32; 3] {
    match v {
        UnityValue::Object(o) => [
            o.get("x").map(as_f32).unwrap_or(0.0),
            o.get("y").map(as_f32).unwrap_or(0.0),
            o.get("z").map(as_f32).unwrap_or(0.0),
        ],
        UnityValue::Array(a) if a.len() >= 3 => [as_f32(&a[0]), as_f32(&a[1]), as_f32(&a[2])],
        _ => [0.0, 0.0, 0.0],
    }
}

fn ident() -> Mat4 {
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

fn trs(pos: [f32; 3], rot: [f32; 4], scale: [f32; 3]) -> Mat4 {
    let (x, y, z, w) = (rot[0], rot[1], rot[2], rot[3]);
    let (x2, y2, z2) = (x + x, y + y, z + z);
    let (xx, yy, zz) = (x * x2, y * y2, z * z2);
    let (xy, xz, yz) = (x * y2, x * z2, y * z2);
    let (wx, wy, wz) = (w * x2, w * y2, w * z2);
    let (sx, sy, sz) = (scale[0], scale[1], scale[2]);
    [
        [(1.0 - (yy + zz)) * sx, (xy - wz) * sy, (xz + wy) * sz, pos[0]],
        [(xy + wz) * sx, (1.0 - (xx + zz)) * sy, (yz - wx) * sz, pos[1]],
        [(xz - wy) * sx, (yz + wx) * sy, (1.0 - (xx + yy)) * sz, pos[2]],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

fn world_of(id: i64, tfs: &HashMap<i64, Tf>, cache: &mut HashMap<i64, Mat4>) -> Mat4 {
    if let Some(m) = cache.get(&id) {
        return *m;
    }
    let Some(tf) = tfs.get(&id) else {
        return ident();
    };
    let local = trs(tf.pos, tf.rot, tf.scale);
    let mut world = if let Some(p) = tf.parent {
        if p != 0 && p != id {
            mat_mul_pub(&world_of(p, tfs, cache), &local)
        } else {
            local
        }
    } else {
        local
    };
    // Preserve native scale-point then rotate/translate arithmetic for
    // Transform.position; retain the complete matrix basis for shear.
    let mut point = tf.pos;
    let mut parent = tf.parent.filter(|p| *p != 0 && *p != id);
    while let Some(parent_id) = parent {
        let Some(ancestor) = tfs.get(&parent_id) else { break };
        for axis in 0..3 { point[axis] *= ancestor.scale[axis]; }
        point = mat_vec_local(&trs(ancestor.pos, ancestor.rot, [1.0;3]), point);
        parent = ancestor.parent.filter(|p| *p != 0 && *p != parent_id);
    }
    for axis in 0..3 { world[axis][3] = point[axis]; }
    cache.insert(id, world);
    world
}

fn pos_of(m: &Mat4) -> [f32; 3] {
    [m[0][3], m[1][3], m[2][3]]
}

fn mul_dir(m: &Mat4, v: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
        m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
        m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
    ]
}

fn root_local_gravity(root_world: &Mat4, gravity: [f32; 3]) -> [f32; 3] {
    // DynamicBone.SetupParticles: Root.InverseTransformDirection(m_Gravity).
    let q = quat_conj(quat_from_mat(root_world));
    let rotation = trs([0.0; 3], q, [1.0; 3]);
    mul_dir(&rotation, gravity)
}

fn root_local_gravity_rotation(root_rotation: [f32; 4], gravity: [f32; 3]) -> [f32; 3] {
    mul_dir(&trs([0.0; 3], quat_conj(root_rotation), [1.0; 3]), gravity)
}

fn vadd(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn vsub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn vscale(a: [f32; 3], s: f32) -> [f32; 3] {
    [a[0] * s, a[1] * s, a[2] * s]
}

fn vdot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn vlen(a: [f32; 3]) -> f32 {
    vdot(a, a).sqrt()
}

fn vnorm(a: [f32; 3]) -> [f32; 3] {
    let n = vlen(a);
    if n > 1e-8 {
        vscale(a, 1.0 / n)
    } else {
        a
    }
}

fn vcross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn quat_mul(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
    [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ]
}

fn quat_conj(q: [f32; 4]) -> [f32; 4] {
    [-q[0], -q[1], -q[2], q[3]]
}

fn quat_norm(mut q: [f32; 4]) -> [f32; 4] {
    let n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt();
    if n > 1e-8 {
        q[0] /= n;
        q[1] /= n;
        q[2] /= n;
        q[3] /= n;
    }
    q
}

fn world_rot_of(id: i64, tfs: &HashMap<i64, Tf>) -> [f32; 4] {
    let Some(tf) = tfs.get(&id) else { return [0.0, 0.0, 0.0, 1.0] };
    let local = quat_norm(tf.rot);
    match tf.parent.filter(|parent| *parent != 0 && *parent != id) {
        Some(parent) => quat_norm(quat_mul(world_rot_of(parent, tfs), local)),
        None => local,
    }
}

// Only valid while parent links are fixed and every rotation write invalidates
// this node and its cached descendants. No state survives a solver step.
fn scoped_world_rot(id:i64,tfs:&HashMap<i64,Tf>,cache:&mut HashMap<i64,[f32;4]>)->[f32;4]{
    if let Some(rotation)=cache.get(&id){return *rotation;}
    let Some(tf)=tfs.get(&id)else{return [0.,0.,0.,1.]};
    let local=quat_norm(tf.rot);
    let rotation=match tf.parent.filter(|parent|*parent!=0&&*parent!=id){
        Some(parent)=>quat_norm(quat_mul(scoped_world_rot(parent,tfs,cache),local)),
        None=>local,
    };
    cache.insert(id,rotation);rotation
}

// Unity reflects descendant rotation axes through negative ancestor scales
// before removing rotation from lossyScale. Keep the signed scale state;
// extracting a quaternion from the sheared world basis loses this information.
fn lossy_scale_rotation(id: i64, tfs: &HashMap<i64, Tf>) -> ([f32;4], [f32;3]) {
    let Some(tf)=tfs.get(&id) else{return ([0.,0.,0.,1.],[1.;3])};
    let (parent,sign)=tf.parent.filter(|p|*p!=0&&*p!=id)
        .map(|p|lossy_scale_rotation(p,tfs)).unwrap_or(([0.,0.,0.,1.],[1.;3]));
    let mut local=quat_norm(tf.rot);
    local[0]*=sign[1]*sign[2];local[1]*=sign[0]*sign[2];local[2]*=sign[0]*sign[1];
    let rotation=if tf.parent.is_some_and(|p|p!=0&&p!=id){quat_norm(quat_mul(parent,local))}else{local};
    (rotation,std::array::from_fn(|axis|sign[axis]*if tf.scale[axis]<0.{-1.}else{1.}))
}

type LossyRotation=([f32;4],[f32;3]);
fn scoped_lossy_rotation(id:i64,tfs:&HashMap<i64,Tf>,cache:&mut HashMap<i64,LossyRotation>)->LossyRotation {
    if let Some(value)=cache.get(&id){return *value;}
    let Some(tf)=tfs.get(&id)else{return ([0.,0.,0.,1.],[1.;3]);};
    let (parent,sign)=tf.parent.filter(|p|*p!=0&&*p!=id)
        .map(|p|scoped_lossy_rotation(p,tfs,cache)).unwrap_or(([0.,0.,0.,1.],[1.;3]));
    let mut local=quat_norm(tf.rot);
    local[0]*=sign[1]*sign[2];local[1]*=sign[0]*sign[2];local[2]*=sign[0]*sign[1];
    let rotation=if tf.parent.is_some_and(|p|p!=0&&p!=id){quat_norm(quat_mul(parent,local))}else{local};
    let value=(rotation,std::array::from_fn(|axis|sign[axis]*if tf.scale[axis]<0.{-1.}else{1.}));
    cache.insert(id,value);value
}

fn scoped_lossy_scale(id:i64,tfs:&HashMap<i64,Tf>,world:&Mat4,cache:&mut HashMap<i64,LossyRotation>)->[f32;3] {
    let rotation=trs([0.;3],scoped_lossy_rotation(id,tfs,cache).0,[1.;3]);
    let value=std::array::from_fn(|axis|vdot([world[0][axis],world[1][axis],world[2][axis]],
        [rotation[0][axis],rotation[1][axis],rotation[2][axis]]));
    #[cfg(test)]
    assert_eq!(value.map(f32::to_bits),transform_lossy_scale(id,tfs,world).map(f32::to_bits));
    value
}

/// Unity Transform.lossyScale is the diagonal after removing the composed
/// Transform.rotation. Column lengths also include shear and overestimate
/// scale beneath rotated, non-uniform ancestors (native scale control353).
fn transform_lossy_scale(id: i64, tfs: &HashMap<i64, Tf>, world: &Mat4) -> [f32; 3] {
    #[cfg(test)]
    if std::env::var("OK_DYNAMIC_SCALE_COLUMN_CONTROL").as_deref() == Ok("1") {
        return std::array::from_fn(|axis| vlen([world[0][axis],world[1][axis],world[2][axis]]));
    }
    let orientation=lossy_scale_rotation(id,tfs).0;
    #[cfg(test)]
    let orientation=if std::env::var("OK_COLLIDER_IGNORE_REFLECTION").as_deref()==Ok("1"){
        world_rot_of(id,tfs)
    }else{orientation};
    let rotation = trs([0.0; 3], orientation, [1.0; 3]);
    std::array::from_fn(|axis| vdot(
        [world[0][axis], world[1][axis], world[2][axis]],
        [rotation[0][axis], rotation[1][axis], rotation[2][axis]]))
}

fn quat_from_mat(m: &Mat4) -> [f32; 4] {
    // Transform.rotation excludes lossyScale. DynamicBone frequently runs
    // beneath a scaled head socket, so extract orientation from unit axes.
    let mut r = *m;
    for axis in 0..3 {
        let len = vlen([m[0][axis], m[1][axis], m[2][axis]]).max(1e-8);
        for row in 0..3 { r[row][axis] /= len; }
    }
    let m = &r;
    let m00 = m[0][0];
    let m11 = m[1][1];
    let m22 = m[2][2];
    let trace = m00 + m11 + m22;
    let q = if trace > 0.0 {
        let s = (trace + 1.0).sqrt() * 2.0;
        [
            (m[2][1] - m[1][2]) / s,
            (m[0][2] - m[2][0]) / s,
            (m[1][0] - m[0][1]) / s,
            0.25 * s,
        ]
    } else if m00 > m11 && m00 > m22 {
        let s = (1.0 + m00 - m11 - m22).sqrt() * 2.0;
        [
            0.25 * s,
            (m[0][1] + m[1][0]) / s,
            (m[0][2] + m[2][0]) / s,
            (m[2][1] - m[1][2]) / s,
        ]
    } else if m11 > m22 {
        let s = (1.0 + m11 - m00 - m22).sqrt() * 2.0;
        [
            (m[0][1] + m[1][0]) / s,
            0.25 * s,
            (m[1][2] + m[2][1]) / s,
            (m[0][2] - m[2][0]) / s,
        ]
    } else {
        let s = (1.0 + m22 - m00 - m11).sqrt() * 2.0;
        [
            (m[0][2] + m[2][0]) / s,
            (m[1][2] + m[2][1]) / s,
            0.25 * s,
            (m[1][0] - m[0][1]) / s,
        ]
    };
    quat_norm(q)
}

fn quat_from_to(from: [f32; 3], to: [f32; 3]) -> [f32; 4] {
    let f = vnorm(from);
    let t = vnorm(to);
    let d = vdot(f, t);
    // Actual Unity control: 0.001 rad is identity, 0.0015 rad is retained.
    // The former 0.9999 cutoff was too wide; no cutoff was also incorrect.
    if d > 0.999999 { return [0.0,0.0,0.0,1.0]; }
    if d < -0.999999 {
        let mut axis = vcross(f, [1.0, 0.0, 0.0]);
        if vlen(axis) < 1e-6 {
            axis = vcross(f, [0.0, 1.0, 0.0]);
        }
        axis = vnorm(axis);
        return [axis[0], axis[1], axis[2], 0.0];
    }
    let c = vcross(f, t);
    quat_norm([c[0], c[1], c[2], 1.0 + d])
}

#[test]
fn dynamic_bone_from_to_preserves_small_rotations() {
    let angle = 0.00485f32;
    let q = quat_from_to([0.0, 0.0, 1.0], [0.0, -angle.sin(), angle.cos()]);
    let target = mul_dir(&trs([0.0; 3], q, [1.0; 3]), [0.0, 0.0, 1.0]);
    assert!(vlen(vsub(target, [0.0, -angle.sin(), angle.cos()])) < 1e-7);
    assert!((q[0] - (angle * 0.5).sin()).abs() < 1e-7);
}

#[test]
fn dynamic_bone_fix_skips_only_ordinary_idle_particle_update() {
    let skel = Skeleton { names: vec!["root".into(), "tip".into()], worlds: vec![], rest: vec![],
        smr_count: 2, bone_ids: vec![1, 2], source_mesh: 0,
        tfs: [(1, Tf { sibling_index: None, name: "root".into(), parent: None, pos: [0.0; 3],
            rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] }),
            (2, Tf { sibling_index: None, name: "tip".into(), parent: Some(1), pos: [0.0, 1.0, 0.0],
            rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] })].into_iter().collect() };
    let mut source = DBoneSet { bones: vec![DBone { root: 1, update_rate: 60.0,
        inert: 1.0, ..Default::default() }], colliders: HashMap::new(), ..Default::default() };
    let mut vanilla = AttachedDynamicSet::new(&source, &skel, &ident()).unwrap();
    source.bones[0].suppress_skip_update = true;
    let mut patched = AttachedDynamicSet::new(&source, &skel, &ident()).unwrap();
    let before = parts_pos(&patched.skel.tfs, 2);
    let placement = trs([0.1, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0; 3]);
    vanilla.set_placement(&placement); patched.set_placement(&placement);
    vanilla.step(0.0); patched.step(0.0);
    assert!(vlen(vsub(parts_pos(&patched.skel.tfs, 2), before)) < 1e-6,
        "patched SkipUpdateParticles must leave particle world positions unchanged");
    assert!((parts_pos(&vanilla.skel.tfs, 2)[0] - before[0] - 0.1).abs() < 1e-6,
        "vanilla idle step still follows full inertia");
    source.bones.push(DBone { root: 1, ver01: true, ..Default::default() });
    let configured = source.with_game_fixes(&crate::test_support::game_path(""));
    assert!(!configured.bones[1].suppress_skip_update, "the installed patch does not target Ver01");
}

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
struct DBone {
    root: i64,
    #[serde(default)]
    ver01: bool,
    #[serde(default)]
    suppress_skip_update: bool,
    /// Ver01 reads an explicit ordered m_Nodes chain rather than traversing
    /// every descendant of m_Root. Empty means the ordinary DynamicBone path.
    #[serde(default)]
    ver01_nodes: Vec<(i64, bool)>,
    /// Transform of the MonoBehaviour GameObject. DynamicBone reads
    /// `Transform.lossyScale.x` from this, which may differ from m_Root.
    #[serde(default)]
    component_transform: Option<i64>,
    /// MonoBehaviour.name after ChaControl renames the instantiated prefab.
    component_name: String,
    /// A copied clothing component stays on its prefab GameObject while its
    /// root moves to the body rig. Keep its actual transform ancestry.
    component_ancestry: Vec<Tf>,
    implanted_collider_refs: Vec<i64>,
    implanted_collider_ancestry: Vec<(BodyCollider, Vec<Tf>)>,
    overlay_bindings: Option<implant::rig::Bindings>,
    source_root_name: Option<String>,
    /// DynamicBone.FreezeAxis: 0 none, 1 X, 2 Y, 3 Z.
    freeze_axis: usize,
    gravity: [f32; 3],
    force: [f32; 3],
    damping: f32,
    elasticity: f32,
    stiffness: f32,
    inert: f32,
    exclusions: Vec<i64>,
    not_rolls: Vec<i64>,
    update_rate: f32,
    end_length: f32,
    end_offset: [f32; 3],
    radius: f32,
    collider_refs: Vec<i64>,
    damping_curve: Vec<CurveKey>,
    elasticity_curve: Vec<CurveKey>,
    stiffness_curve: Vec<CurveKey>,
    inert_curve: Vec<CurveKey>,
    radius_curve: Vec<CurveKey>,
    #[serde(default = "default_add_angle_scale")]
    add_angle_scale: f32,
    #[serde(default)]
    add_angle_curve: Vec<CurveKey>,
}

/// One DynamicBoneEditor row (card `AccessoryDynamicBoneData`): the accessory slot's DynamicBone whose m_Root is
/// named `bone`, in outfit `coordinate`. Each Some replaces that m_ parameter, as the plugin sets it on the component.
#[derive(Clone, Debug, Default)]
pub struct DynamicBoneEdit {
    pub coordinate: i32,
    pub slot: i32,
    pub bone: String,
    pub freeze_axis: Option<usize>,
    pub weight: Option<f32>,
    pub damping: Option<f32>,
    pub elasticity: Option<f32>,
    pub stiffness: Option<f32>,
    pub inertia: Option<f32>,
    pub radius: Option<f32>,
}

fn default_add_angle_scale() -> f32 { 1.0 }

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(crate) struct CurveKey {
    pub(crate) time: f32,
    pub(crate) value: f32,
    pub(crate) in_slope: f32,
    pub(crate) out_slope: f32,
    pub(crate) in_weight: f32,
    pub(crate) out_weight: f32,
    pub(crate) weighted_mode: i64,
}

pub(crate) fn parse_curve(value: Option<&UnityValue>) -> Vec<CurveKey> {
    let Some(UnityValue::Object(curve)) = value else { return Vec::new() };
    let Some(UnityValue::Array(keys)) = curve.get("m_Curve") else { return Vec::new() };
    let mut out: Vec<_> = keys.iter().filter_map(|key| {
        let UnityValue::Object(k) = key else { return None };
        let f = |name: &str| k.get(name).map(as_f32).unwrap_or(0.0);
        Some(CurveKey {
            time: f("time"), value: f("value"),
            in_slope: f("inSlope"), out_slope: f("outSlope"),
            in_weight: k.get("inWeight").map(as_f32).unwrap_or(1.0 / 3.0),
            out_weight: k.get("outWeight").map(as_f32).unwrap_or(1.0 / 3.0),
            weighted_mode: k.get("weightedMode").and_then(|v| v.as_i64()).unwrap_or(0),
        })
    }).collect();
    out.sort_by(|a, b| a.time.total_cmp(&b.time));
    out
}

pub(crate) fn curve_value(keys: &[CurveKey], time: f32) -> f32 {
    let Some(first) = keys.first() else { return 1.0 };
    if time <= first.time { return first.value; }
    let last = keys.last().unwrap();
    if time >= last.time { return last.value; }
    let right = keys.partition_point(|k| k.time < time);
    let (a, b) = (&keys[right - 1], &keys[right]);
    let dt = b.time - a.time;
    if dt <= 1e-8 { return b.value; }
    if !a.out_slope.is_finite() { return a.value; }
    if !b.in_slope.is_finite() { return b.value; }
    let weighted_out = a.weighted_mode & 2 != 0;
    let weighted_in = b.weighted_mode & 1 != 0;
    let wa = if weighted_out { a.out_weight.clamp(0.0, 1.0) } else { 1.0 / 3.0 };
    let wb = if weighted_in { b.in_weight.clamp(0.0, 1.0) } else { 1.0 / 3.0 };
    let bezier = |p0: f32, p1: f32, p2: f32, p3: f32, u: f32| {
        let v = 1.0 - u;
        v * v * v * p0 + 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u * p3
    };
    let target = ((time - a.time) / dt).clamp(0.0, 1.0);
    let u = if weighted_out || weighted_in {
        let (mut lo, mut hi) = (0.0, 1.0);
        for _ in 0..18 {
            let mid = (lo + hi) * 0.5;
            if bezier(0.0, wa, 1.0 - wb, 1.0, mid) < target { lo = mid; } else { hi = mid; }
        }
        (lo + hi) * 0.5
    } else { target };
    bezier(a.value, a.value + a.out_slope * wa * dt,
        b.value - b.in_slope * wb * dt, b.value, u)
}

#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
struct BodyCollider {
    transform: i64,
    center: [f32; 3],
    radius: f32,
    height: f32,
    direction: usize,
    inside: bool,
}

fn pptr_list(v: Option<&UnityValue>) -> Vec<i64> {
    match v {
        Some(UnityValue::Array(a)) => a.iter().filter_map(pptr).filter(|id| *id != 0).collect(),
        _ => Vec::new(),
    }
}

fn parse_dbones(serialized: &SerializedFile) -> Vec<DBone> {
    parse_dbones_resolved(serialized,&crate::unity::references::Scripts::local(serialized))
}
fn parse_dbones_resolved(serialized: &SerializedFile, scripts:&crate::unity::references::Scripts) -> Vec<DBone> {
    let mut go_to_tf = HashMap::new();
    for info in serialized.objects.iter().filter(|o|o.type_id==4) {
        let Ok(obj)=UnityObject::from_serialized_file(serialized,info) else {continue;};
        if let Some(go)=obj.class.properties().get("m_GameObject").and_then(pptr) {go_to_tf.insert(go,info.path_id);}
    }
    let mut out = Vec::new();
    for info in &serialized.objects {
        if info.type_id != 114 {
            continue;
        }
        let Ok(obj) = UnityObject::from_serialized_file(serialized, info) else {
            continue;
        };
        let props = obj.class.properties();
        let class = scripts.class(props.get("m_Script")).unwrap_or("");
        let is_db = class.eq_ignore_ascii_case("DynamicBone")
            || class.eq_ignore_ascii_case("DynamicBone_Ver01");
        if !is_db {
            continue;
        }
        // Unity does not run disabled MonoBehaviour components.
        if !behaviour_enabled(props.get("m_Enabled")) {
            continue;
        }
        let ver01 = class.eq_ignore_ascii_case("DynamicBone_Ver01");
        let ver01_nodes: Vec<(i64, bool)> = if ver01 {
            match props.get("m_Nodes") {
                Some(UnityValue::Array(nodes)) => nodes.iter().filter_map(|node| {
                    let UnityValue::Object(fields) = node else { return None };
                    let id = fields.get("Transform").or_else(|| fields.get("m_Transform"))
                        .and_then(pptr).filter(|id| *id != 0)?;
                    Some((id, behaviour_enabled(fields.get("RotationCalc")
                        .or_else(|| fields.get("m_RotationCalc")))))
                }).collect(),
                _ => Vec::new(),
            }
        } else { Vec::new() };
        let Some(root) = props.get("m_Root").and_then(pptr).filter(|id| *id != 0)
            .or_else(|| ver01_nodes.first().map(|(id, _)| *id)) else {
            continue;
        };
        let gravity = props
            .get("m_Gravity")
            .map(vec3)
            .unwrap_or([0.0, 0.0, 0.0]);
        out.push(DBone {
            root,
            ver01,
            suppress_skip_update: false,
            ver01_nodes,
            component_transform: props.get("m_GameObject").and_then(pptr)
                .and_then(|go| go_to_tf.get(&go).copied()),
            component_name: String::new(),
            component_ancestry: Vec::new(),
            implanted_collider_refs: Vec::new(),
            implanted_collider_ancestry: Vec::new(),
            overlay_bindings: None,
            source_root_name: None,
            freeze_axis: props.get("m_FreezeAxis").and_then(|v| v.as_i64())
                .unwrap_or(0).clamp(0, 3) as usize,
            gravity,
            force: props.get("m_Force").map(vec3).unwrap_or([0.0, 0.0, 0.0]),
            damping: props.get("m_Damping").map(as_f32).unwrap_or(0.1),
            elasticity: props.get("m_Elasticity").map(as_f32).unwrap_or(0.1),
            stiffness: props.get("m_Stiffness").map(as_f32).unwrap_or(0.1),
            inert: props.get("m_Inert").map(as_f32).unwrap_or(0.0),
            exclusions: pptr_list(props.get("m_Exclusions")),
            not_rolls: pptr_list(props.get("m_notRolls")),
            update_rate: props.get("m_UpdateRate").map(as_f32).unwrap_or(60.0),
            end_length: props.get("m_EndLength").map(as_f32).unwrap_or(0.0),
            end_offset: props
                .get("m_EndOffset")
                .map(vec3)
                .unwrap_or([0.0, 0.0, 0.0]),
            radius: props.get("m_Radius").map(as_f32).unwrap_or(0.0).max(0.0),
            collider_refs: pptr_list(props.get("m_Colliders")),
            damping_curve: parse_curve(props.get("m_DampingDistrib")),
            elasticity_curve: parse_curve(props.get("m_ElasticityDistrib")),
            stiffness_curve: parse_curve(props.get("m_StiffnessDistrib")),
            inert_curve: parse_curve(props.get("m_InertDistrib")),
            radius_curve: parse_curve(props.get("m_RadiusDistrib")),
            add_angle_scale: props.get("m_AddAngleScale").map(as_f32).unwrap_or(1.0),
            add_angle_curve: parse_curve(props.get("m_AddAngleScaleDistrib")),
        });
    }
    out
}

fn parse_colliders(serialized: &SerializedFile, body: Option<&Skeleton>) -> Vec<(i64, BodyCollider)> {
    parse_colliders_resolved(serialized,body,&crate::unity::references::Scripts::local(serialized))
}
fn parse_colliders_resolved(serialized: &SerializedFile, body: Option<&Skeleton>, scripts:&crate::unity::references::Scripts) -> Vec<(i64, BodyCollider)> {
    // Only DynamicBoneCollider behaviours are colliders: a file without that script has none (OK_DBDIAG: counted).
    if !scripts.has_class(|class| class.eq_ignore_ascii_case("DynamicBoneCollider")) && std::env::var_os("OK_DBDIAG").is_none() { return Vec::new(); }
    let mut go_to_tf = HashMap::new();
    for info in serialized.objects.iter().filter(|o|o.type_id==4) {
        let Ok(obj)=UnityObject::from_serialized_file(serialized,info) else {continue;};
        if let Some(go)=obj.class.properties().get("m_GameObject").and_then(pptr) {go_to_tf.insert(go,info.path_id);}
    }
    let mut out = Vec::new();
    let mut class_hits = 0;
    let mut transform_hits = 0;
    for info in &serialized.objects {
        if info.type_id != 114 { continue; }
        let Ok(obj) = UnityObject::from_serialized_file(serialized, info) else { continue };
        let props = obj.class.properties();
        let class = scripts.class(props.get("m_Script")).unwrap_or("");
        if !class.eq_ignore_ascii_case("DynamicBoneCollider") { continue; }
        class_hits += 1;
        if !behaviour_enabled(props.get("m_Enabled")) { continue; }
        let Some(transform) = props.get("m_GameObject").and_then(pptr)
            .and_then(|go| go_to_tf.get(&go).copied()) else { continue };
        transform_hits += 1;
        if body.is_some_and(|b| !b.tfs.contains_key(&transform)) { continue; }
        out.push((info.path_id, BodyCollider {
            transform,
            center: props.get("m_Center").map(vec3).unwrap_or([0.0; 3]),
            radius: props.get("m_Radius").map(as_f32).unwrap_or(0.0).max(0.0),
            height: props.get("m_Height").map(as_f32).unwrap_or(0.0).max(0.0),
            direction: props.get("m_Direction").and_then(|v| v.as_i64()).unwrap_or(1).clamp(0, 2) as usize,
            inside: props.get("m_Bound").and_then(|v| v.as_i64()) == Some(1),
        }));
    }
    if std::env::var("OK_DBDIAG").is_ok() {
        eprintln!("ok-extract: collider probe transforms={} class_hits={} transform_hits={} body_hits={}",
            go_to_tf.len(), class_hits, transform_hits, out.len());
    }
    out
}

fn parse_body_colliders(serialized: &SerializedFile, body: &Skeleton) -> Vec<BodyCollider> {
    parse_colliders(serialized, Some(body)).into_iter().map(|(_, c)| c).collect()
}

#[cfg(test)]
#[test]
fn clothing_colliders_rebind_to_body_bone_tree() {
    let path = &crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
    let (.., Some(mut body), _) = crate::unity::load_base_bundle(path) else { return };
    let bytes = crate::gamefs::read(path).unwrap();
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let bone = parsed.skeleton_for_prefab_name("p_cf_body_bone").expect("body bone prefab");
    let added = body.merge_missing_tree_transforms(&bone);
    assert!(added > 0, "body mesh tree omits collider nodes");
    let mut total = 0;
    for (_, fb) in &parsed.files {
        let Ok(serialized) = crate::unity::parse_shared_serialized(fb.clone()) else { continue };
        let colliders = parse_body_colliders(&serialized, &bone);
        total += colliders.len();
        assert!(colliders.iter().all(|c| {
            let name = &bone.tfs.get(&c.transform).unwrap().name;
            body.tfs.values().any(|tf| tf.name == *name)
        }), "every collider must have a body transform");
    }
    assert_eq!(total, 24);
}

#[cfg(test)]
#[test]
#[ignore = "manual inventory of game hair bundles"]
fn dynamic_bone_real_variant_inventory() {
    let base = &crate::test_support::game_path(r"\abdata\chara");
    let names = ["mm_base.unity3d", "ao_hair_30.unity3d", "ao_hair_35.unity3d",
        "ao_hair_40.unity3d", "ao_hair_64.unity3d", "ao_hair_80.unity3d",
        "ao_hair_88.unity3d", "bo_hair_b_30.unity3d",
        "bo_hair_f_30.unity3d", "bo_hair_f_40.unity3d",
        "bo_hair_o_00.unity3d", "bo_hair_o_40.unity3d"];
    let mut total = [0usize; 2];
    for name in names {
        let path = base.join(name);
        if !path.ok_is_file() { continue; }
        let parsed = crate::unity::parse_bundle_cached(crate::gamefs::read(&path).unwrap());
        let mut counts = [0usize; 2];
        for (_, bytes) in &parsed.files {
            let Ok(serialized) = crate::unity::parse_shared_serialized(bytes.clone()) else { continue };
            for db in parse_dbones(&serialized) {
                counts[db.ver01 as usize] += 1;
            }
        }
        eprintln!("DB VARIANT {name} normal={} ver01={}", counts[0], counts[1]);
        total[0] += counts[0];
        total[1] += counts[1];
    }
    eprintln!("DB VARIANT TOTAL normal={} ver01={}", total[0], total[1]);
    assert!(total[0] + total[1] > 0);
}

#[cfg(test)]
#[test]
fn dynamic_bone_curve_matches_hermite_keys() {
    let a = CurveKey { time: 0.0, value: 3.0, in_slope: 0.0, out_slope: 0.0,
        in_weight: 1.0 / 3.0, out_weight: 1.0 / 3.0, weighted_mode: 0 };
    let b = CurveKey { time: 1.0, value: 0.0, in_slope: 0.0, out_slope: 0.0,
        in_weight: 1.0 / 3.0, out_weight: 1.0 / 3.0, weighted_mode: 0 };
    let keys = [a, b];
    assert!((curve_value(&keys, 0.25) - 2.53125).abs() < 1e-5);
    assert!((curve_value(&keys, 0.5) - 1.5).abs() < 1e-5);
    assert_eq!(curve_value(&keys, 2.0), 0.0);
}

#[cfg(test)]
#[test]
fn dynamic_bone_gravity_ignores_root_translation() {
    let identity = [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ];
    let mut translated = identity;
    translated[0][3] = -0.31;
    translated[1][3] = 1.42;
    translated[2][3] = 0.09;
    let gravity = [0.0, -0.005, 0.0];
    assert_eq!(root_local_gravity(&identity, gravity), root_local_gravity(&translated, gravity));
    let scaled = trs([0.13, 1.42, -0.09], [0.0, 0.0, 0.0, 1.0], [0.78; 3]);
    assert_eq!(root_local_gravity(&identity, gravity), root_local_gravity(&scaled, gravity));
    assert!((quat_from_mat(&scaled)[3] - 1.0).abs() < 1e-6);
}

#[cfg(test)]
#[test]
fn dynamic_bone_mirror_uses_component_then_parent_rotation_axes() {
    let mut tfs = HashMap::new();
    tfs.insert(1, Tf { sibling_index: None, name: "parent".into(), parent: None, pos: [0.0; 3],
        rot: [0.0, 0.0, 0.0, 1.0], scale: [-1.0, -1.0, 1.0] });
    tfs.insert(2, Tf { sibling_index: None, name: "component".into(), parent: Some(1), pos: [0.0; 3],
        rot: [0.0, 0.0, std::f32::consts::FRAC_1_SQRT_2,
            std::f32::consts::FRAC_1_SQRT_2], scale: [-1.0, 1.0, 1.0] });
    let axes = mirrored_direction_axes(&tfs, 2);
    assert_eq!(axes.len(), 2);
    // The component's negative X wins over its parent's negative X; the
    // parent's negative Y supplies the other mirror axis.
    assert!(vlen(vsub(axes[0], [0.0, 1.0, 0.0])) < 1e-5);
    assert!(vlen(vsub(axes[1], [0.0, 1.0, 0.0])) < 1e-5);
    tfs.get_mut(&2).unwrap().scale[0] = 1.0;
    let inherited = mirrored_direction_axes(&tfs, 2);
    assert!(vlen(vsub(inherited[0], [1.0, 0.0, 0.0])) < 1e-5);
}

#[cfg(test)]
#[test]
fn dynamic_bone_end_offset_uses_component_direction() {
    let q = std::f32::consts::FRAC_1_SQRT_2;
    let mut tfs = HashMap::new();
    tfs.insert(1, Tf { sibling_index: None, name: "component".into(), parent: None, pos: [0.0; 3],
        rot: [0.0, 0.0, q, q], scale: [2.0; 3] });
    tfs.insert(2, Tf { sibling_index: None, name: "root".into(), parent: Some(1), pos: [0.0; 3],
        rot: [0.0, 0.0, -q, q], scale: [1.0; 3] });
    tfs.insert(3, Tf { sibling_index: None, name: "leaf".into(), parent: Some(2), pos: [0.0, 0.5, 0.0],
        rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
    let db = DBone { root: 2, component_transform: Some(1),
        end_offset: [1.0, 0.0, 0.0], ..Default::default() };
    let mut particles = Vec::new();
    let mut cache = HashMap::new();
    gather_dparticles(&tfs, Some(db.root), -1, 0.0, &db, 1, &mut particles, &mut cache);
    assert_eq!(particles.len(), 3);
    let tip = &particles[2];
    assert!(tip.id.is_none());
    // TransformDirection ignores the component's scale, and its +90° Z
    // rotation sends local +X to world +Y even though m_Root faces +X.
    assert!(vlen(vsub(vsub(tip.pos, particles[1].pos), [0.0, 1.0, 0.0])) < 1e-5);
}

#[cfg(test)]
#[test]
fn dynamic_bone_end_length_without_parent_uses_local_x() {
    let q = std::f32::consts::FRAC_1_SQRT_2;
    let mut tfs = HashMap::new();
    tfs.insert(1, Tf { sibling_index: None, name: "root".into(), parent: None, pos: [2.0, 3.0, 4.0],
        rot: [0.0, 0.0, q, q], scale: [2.0, 2.0, 2.0] });
    let db = DBone { root: 1, end_length: 0.3, ..Default::default() };
    let mut particles = Vec::new();
    let mut cache = HashMap::new();
    gather_dparticles(&tfs, Some(1), -1, 0.0, &db, 1, &mut particles, &mut cache);
    assert_eq!(particles.len(), 2);
    assert_eq!(particles[1].end_offset, [0.3, 0.0, 0.0]);
    assert!(vlen(vsub(particles[1].pos, [2.0, 3.6, 4.0])) < 1e-5);
}

#[cfg(test)]
#[test]
fn dynamic_bone_ver01_uses_ordered_nodes_and_rotation_flag() {
    let mut tfs = HashMap::new();
    tfs.insert(1, Tf { sibling_index: None, name: "root".into(), parent: None, pos: [0.0; 3],
        rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
    tfs.insert(2, Tf { sibling_index: None, name: "unlisted_child".into(), parent: Some(1), pos: [0.0, 0.4, 0.0],
        rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
    tfs.insert(3, Tf { sibling_index: None, name: "listed_link".into(), parent: Some(1), pos: [1.0, 0.0, 0.0],
        rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
    let db = DBone { root: 1, ver01: true, ver01_nodes: vec![(1, false), (3, true)],
        add_angle_scale: 1.0, ..Default::default() };
    let mut particles = gather_ver01_particles(&tfs, &db);
    assert_eq!(particles.len(), 2);
    assert_eq!(particles[1].id, Some(3));
    assert!(!particles[0].rotation_calc);
    particles[1].pos = [0.0, 1.0, 0.0];
    let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 0, tfs, bone_ids: Vec::new(), source_mesh: 0 };
    let mut cache = HashMap::new();
    apply_particles(&mut skel, &mut particles, &mut cache, 1, true);
    assert_eq!(skel.tfs[&1].rot, [0.0, 0.0, 0.0, 1.0]);
    particles[0].rotation_calc = true;
    particles[1].add_angle_scale = 0.5;
    apply_particles(&mut skel, &mut particles, &mut cache, 1, true);
    let expected = (std::f32::consts::PI / 8.0).sin();
    assert!((skel.tfs[&1].rot[2] - expected).abs() < 1e-5);
}

#[cfg(test)]
#[test]
fn dynamic_bone_ver01_remains_active_when_card_disables_ordinary_shake() {
    let set = DBoneSet { bones: vec![
        DBone { root: 1, ..Default::default() },
        DBone { root: 2, ver01: true, ver01_nodes: vec![(2, true)], ..Default::default() },
    ], colliders: HashMap::new(), ..Default::default() };
    let active = set.after_no_shake();
    assert_eq!(active.bones.len(), 1);
    assert_eq!(active.bones[0].root, 2);
}

#[cfg(test)]
#[test]
fn dynamic_bone_shared_chain_preserves_earlier_late_update() {
    let mut tfs = HashMap::new();
    for (id, parent, pos) in [
        (1, None, [0.0, 0.0, 0.0]),
        (2, Some(1), [0.0, 1.0, 0.0]),
        (3, Some(2), [0.0, 1.0, 0.0]),
        (4, Some(1), [1.0, 0.0, 0.0]),
    ] {
        tfs.insert(id, Tf { sibling_index: None, name: format!("bone_{id}"), parent, pos,
            rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
    }
    let mut source = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 0, tfs, bone_ids: Vec::new(), source_mesh: 0 };
    source.rebuild_worlds();
    let set = DBoneSet { bones: vec![
        DBone { root: 1, force: [0.02, 0.0, 0.0], damping: 0.2,
            elasticity: 0.1, stiffness: 0.1, update_rate: 60.0, ..Default::default() },
        DBone { root: 2, force: [-0.02, 0.0, 0.0], damping: 0.2, elasticity: 0.1,
            stiffness: 0.1, update_rate: 0.1, ..Default::default() },
    ], colliders: HashMap::new(), ..Default::default() };
    let placement = trs([0.0; 3], [0.0, 0.0, 0.0, 1.0], [1.0; 3]);
    let mut state = AttachedDynamicSet::new(&set, &source, &placement).unwrap();
    let mut baked = source.clone();
    assert_eq!(hang_set_attached(&set, &mut baked, &placement), 2);
    for id in [1, 2, 3, 4] {
        let from_runtime = &state.skel.tfs[&id];
        let from_bake = &baked.tfs[&id];
        assert!(vlen(vsub(from_runtime.pos, from_bake.pos)) < 1e-5,
            "neutral bake and runtime disagree on bone {id}: {:?} vs {:?}",
            from_runtime.pos, from_bake.pos);
        let dot: f32 = from_runtime.rot.iter().zip(from_bake.rot.iter())
            .map(|(a, b)| a * b).sum::<f32>().abs();
        assert!(1.0 - dot < 1e-5,
            "neutral bake and runtime rotation disagree on bone {id}: {:?} vs {:?}",
            from_runtime.rot, from_bake.rot);
    }
    state.step(1.0 / 60.0);
    let first_chain_tip = state.chains[0].particles.iter().find(|p| p.id == Some(2)).unwrap().pos;
    let shared_bone = parts_pos(&state.skel.tfs, 2);
    assert!(vlen(vsub(first_chain_tip, [0.0, 1.0, 0.0])) > 0.005);
    assert!(vlen(vsub(first_chain_tip, shared_bone)) < 1e-5,
        "later Update reset an earlier chain's LateUpdate result: {first_chain_tip:?} vs {shared_bone:?}");
}

fn collide_particle(pos: &mut [f32; 3], radius: f32, collider: &BodyCollider, body: &Skeleton) {
    let mut cache = HashMap::new();
    let world = world_of(collider.transform, &body.tfs, &mut cache);
    let scale_z = transform_lossy_scale(collider.transform, &body.tfs, &world)[2].abs();
    collide_particle_world(pos, radius, collider, &world, scale_z);
}

fn collide_particle_world(pos: &mut [f32; 3], radius: f32, collider: &BodyCollider, world: &Mat4, scale_z: f32) {
    let reach = collider.radius * scale_z + radius;
    let half = (collider.height - collider.radius) * 0.5;
    let mut lo = collider.center;
    let mut hi = collider.center;
    if half > 0.0 {
        lo[collider.direction] -= half;
        hi[collider.direction] += half;
    }
    let a = mat_vec_local(world, lo);
    let b = mat_vec_local(world, hi);
    let ab = vsub(b, a);
    let mut diff = vsub(*pos, a);
    let projection = vdot(diff, ab);
    let length_squared = vdot(ab, ab);
    let interior = half > 0.0 && projection > 0.0 && projection < length_squared;
    let center = if half <= 0.0 || projection <= 0.0 { a }
        else if projection >= length_squared { diff = vsub(*pos, b); b }
        else {
            diff = vsub(diff, vscale(ab, projection / length_squared));
            a // unused for the source capsule interior branch
        };
    let distance_squared = vdot(diff, diff);
    // Preserve DynamicBoneCollider's squared-distance predicates and its
    // separate capsule-interior update. Replacing the interior expression by
    // nearestPoint + normalizedDirection * radius changes f32 contact input.
    if distance_squared > 0.0 && if collider.inside { distance_squared > reach * reach }
        else { distance_squared < reach * reach } {
        let distance = distance_squared.sqrt();
        *pos = if interior { vadd(*pos, vscale(diff, (reach - distance) / distance)) }
            else { vadd(center, vscale(diff, reach / distance)) };
    }
}

// Transform endpoints are constant during UpdateParticles2: the solver only
// writes Transform locals later, in ApplyParticles. Rebuild for every substep
// after the caller has refreshed body/implant collider transforms.
struct PreparedCollision {
    a: [f32;3], b: [f32;3], ab: [f32;3], half: f32,
    length_squared: f32, scaled_radius: f32, inside: bool,
    low:[f32;3],high:[f32;3],slack:f32,bounded:bool,
}
impl PreparedCollision {
    fn new(collider:&BodyCollider,world:&Mat4,scale_z:f32)->Self {
        let half=(collider.height-collider.radius)*0.5;
        let mut lo=collider.center;let mut hi=collider.center;
        if half>0.0 {lo[collider.direction]-=half;hi[collider.direction]+=half;}
        let a=mat_vec_local(world,lo);let b=mat_vec_local(world,hi);let ab=vsub(b,a);
        let low=std::array::from_fn(|i|a[i].min(b[i]));let high=std::array::from_fn(|i|a[i].max(b[i]));
        // Include rounding of subtraction, dot products and expanded bounds.
        // Non-finite data follows the original narrow phase unchanged.
        let magnitude=a.iter().chain(&b).fold(1.0f32,|m,x|m.max(x.abs()));
        Self {a,b,ab,half,length_squared:vdot(ab,ab),scaled_radius:collider.radius*scale_z,inside:collider.inside,
            low,high,slack:magnitude*f32::EPSILON*16.,bounded:a.iter().chain(&b).all(|x|x.is_finite())}
    }
    fn collide(&self,pos:&mut [f32;3],radius:f32) {
        let reach=self.scaled_radius+radius;
        // Outside a capsule's conservatively expanded AABB cannot collide.
        // Inside colliders intentionally push particles from outside inward.
        if !self.inside&&self.bounded&&reach.is_finite() {
            let padding=reach.abs()+reach.abs()*f32::EPSILON*16.+self.slack;
            for i in 0..3 {if pos[i]<self.low[i]-padding||pos[i]>self.high[i]+padding{return;}}
        }
        let mut diff=vsub(*pos,self.a);let projection=vdot(diff,self.ab);
        let interior=self.half>0.0&&projection>0.0&&projection<self.length_squared;
        let center=if self.half<=0.0||projection<=0.0 {self.a}
            else if projection>=self.length_squared {diff=vsub(*pos,self.b);self.b}
            else {diff=vsub(diff,vscale(self.ab,projection/self.length_squared));self.a};
        let distance_squared=vdot(diff,diff);
        if distance_squared>0.0&&if self.inside {distance_squared>reach*reach}else{distance_squared<reach*reach} {
            let distance=distance_squared.sqrt();
            *pos=if interior {vadd(*pos,vscale(diff,(reach-distance)/distance))}
                else {vadd(center,vscale(diff,reach/distance))};
        }
    }
}

struct Particle {
    id: Option<i64>,
    parent: i32,
    pos: [f32; 3],
    prev: [f32; 3],
    init_local_pos: [f32; 3],
    damping: f32,
    elasticity: f32,
    stiffness: f32,
    inert: f32,
    rest_len: f32,
}

fn children_of(tfs: &HashMap<i64, Tf>, parent: i64) -> Vec<i64> {
    let mut ids: Vec<i64> = tfs
        .iter()
        .filter(|(_, t)| t.parent == Some(parent))
        .map(|(id, _)| *id)
        .collect();
    ids.sort_by_key(|id|(tfs[id].sibling_index.unwrap_or(u32::MAX),*id));
    ids
}

/// DynamicBone.AppendParticles visits ordinary children first, then expands
/// only the last non-excluded notRoll child at this parent (the game's index).
fn particle_children(tfs:&HashMap<i64,Tf>,parent:i64,db:&DBone)->Vec<i64> {
    let mut result=Vec::new();let mut last_not_roll=None;
    for child in children_of(tfs,parent) {
        if db.exclusions.contains(&child) {continue;}
        if db.not_rolls.contains(&child) {last_not_roll=Some(child);}
        else {result.push(child);}
    }
    if let Some(child)=last_not_roll {
        result.extend(children_of(tfs,child).into_iter()
            .filter(|id|!db.exclusions.contains(id)&&!db.not_rolls.contains(id)));
    }
    result
}

fn append_particles(
    tfs: &HashMap<i64, Tf>,
    b: Option<i64>,
    parent_index: i32,
    db: &DBone,
    particles: &mut Vec<Particle>,
    cache: &mut HashMap<i64, Mat4>,
) {
    let world = b
        .map(|id| pos_of(&world_of(id, tfs, cache)))
        .unwrap_or([0.0; 3]);
    let local = b
        .and_then(|id| tfs.get(&id))
        .map(|t| t.pos)
        .unwrap_or([0.0; 3]);
    let rest_len = if parent_index >= 0 {
        vlen(vsub(particles[parent_index as usize].pos, world))
    } else {
        0.0
    };
    particles.push(Particle {
        id: b,
        parent: parent_index,
        pos: world,
        prev: world,
        init_local_pos: local,
        damping: db.damping.clamp(0.0, 1.0),
        elasticity: db.elasticity.clamp(0.0, 1.0),
        stiffness: db.stiffness.clamp(0.0, 1.0),
        inert: db.inert.clamp(0.0, 1.0),
        rest_len,
    });
    let count = particles.len() as i32 - 1;
    let Some(bid) = b else {
        return;
    };
    for child in particle_children(tfs,bid,db) {
        append_particles(tfs, Some(child), count, db, particles, cache);
    }
}

fn set_world_pos(tfs: &mut HashMap<i64, Tf>, cache: &mut HashMap<i64, Mat4>, id: i64, pos: [f32; 3]) {
    set_world_pos_with_children(tfs,cache,id,pos,None);
}

fn invalidate_descendants<T>(cache:&mut HashMap<i64,T>,id:i64,children:&HashMap<i64,Vec<i64>>) {
    // Recursive world reads cache their complete ancestor chain. If this
    // node is absent, none of its dependent descendants can be cached.
    if cache.remove(&id).is_none(){return;}
    if let Some(ids)=children.get(&id){for child in ids{invalidate_descendants(cache,*child,children);}}
}

fn set_world_pos_with_children(tfs:&mut HashMap<i64,Tf>,cache:&mut HashMap<i64,Mat4>,id:i64,pos:[f32;3],children:Option<&HashMap<i64,Vec<i64>>>) {
    let parent = tfs.get(&id).and_then(|t| t.parent).filter(|p| *p != 0 && *p != id);
    let local = if let Some(p) = parent {
        let pw = world_of(p, tfs, cache);
        let inv = crate::pose::mat_inv_basis(&pw);
        #[cfg(test)]
        {let reference=mat_inv_pub(&pw);for r in 0..4 {for c in 0..3 {assert_eq!(inv[r][c].to_bits(),reference[r][c].to_bits());}}}
        // Keep small local offsets when an overlapping chain writes a world
        // position far from the origin; preserve the complete inverse basis.
        mul_dir(&inv,vsub(pos,pos_of(&pw)))
    } else {
        pos
    };
    if let Some(tf) = tfs.get_mut(&id) {
        tf.pos = local;
    }
    if let Some(children)=children{invalidate_descendants(cache,id,children);}else{cache.clear();}
}

fn set_world_rot(tfs: &mut HashMap<i64, Tf>, cache: &mut HashMap<i64, Mat4>, id: i64, wrot: [f32; 4]) {
    set_world_rot_with_children(tfs,cache,id,wrot,None);
}

fn set_world_rot_with_children(tfs:&mut HashMap<i64,Tf>,cache:&mut HashMap<i64,Mat4>,id:i64,wrot:[f32;4],children:Option<&HashMap<i64,Vec<i64>>>) {
    set_world_rot_scoped(tfs,cache,id,wrot,children,None);
}

fn set_world_rot_scoped(tfs:&mut HashMap<i64,Tf>,cache:&mut HashMap<i64,Mat4>,id:i64,wrot:[f32;4],children:Option<&HashMap<i64,Vec<i64>>>,mut rotations:Option<&mut HashMap<i64,[f32;4]>>) {
    let parent = tfs.get(&id).and_then(|t| t.parent).filter(|p| *p != 0 && *p != id);
    let local = if let Some(p) = parent {
        // Unity Transform.rotation composes quaternions along the parent
        // chain; extracting it from a scaled/sheared world matrix changes the
        // direction of later DynamicBone links.
        let pq = match rotations.as_deref_mut(){Some(rotations)=>scoped_world_rot(p,tfs,rotations),None=>world_rot_of(p,tfs)};
        quat_mul(quat_conj(pq), wrot)
    } else {
        wrot
    };
    if let Some(tf) = tfs.get_mut(&id) {
        tf.rot = quat_norm(local);
    }
    if let Some(children)=children{invalidate_descendants(cache,id,children);}else{cache.clear();}
    if let Some(rotations)=rotations{if let Some(children)=children{invalidate_descendants(rotations,id,children);}else{rotations.clear();}}
}

/// Faithful port of `DynamicBone.UpdateDynamicBones` + `UpdateParticles1/2` +
/// `ApplyParticlesToTransforms`. `object_move_seed` is `m_ObjectMove` for the
/// first frame (the accessory root's world move caused by the body pose change);
/// subsequent frames use zero, matching `m_ObjectMove = Transform.position -
/// m_ObjectPrevPosition` on a settled character.
#[derive(Clone, Debug)]
struct DParticle {
    id: Option<i64>,
    parent: i32,
    pos: [f32; 3],
    prev: [f32; 3],
    end_offset: [f32; 3],
    init_local_pos: [f32; 3],
    init_local_rot: [f32; 4],
    damping: f32,
    elasticity: f32,
    stiffness: f32,
    inert: f32,
    bone_length: f32,
    radius: f32,
    rotation_calc: bool,
    local_position: [f32; 3],
    add_angle_scale: f32,
}

fn gather_dparticles(
    tfs: &HashMap<i64, Tf>,
    b: Option<i64>,
    parent_index: i32,
    bone_length: f32,
    db: &DBone,
    component: i64,
    particles: &mut Vec<DParticle>,
    cache: &mut HashMap<i64, Mat4>,
) {
    let mut p = DParticle {
        id: b,
        parent: parent_index,
        pos: [0.0; 3],
        prev: [0.0; 3],
        end_offset: [0.0; 3],
        init_local_pos: [0.0; 3],
        init_local_rot: [0.0, 0.0, 0.0, 1.0],
        damping: db.damping.clamp(0.0, 1.0),
        elasticity: db.elasticity.clamp(0.0, 1.0),
        stiffness: db.stiffness.clamp(0.0, 1.0),
        inert: db.inert.clamp(0.0, 1.0),
        bone_length,
        radius: db.radius,
        rotation_calc: true,
        local_position: [0.0; 3],
        add_angle_scale: 1.0,
    };
    if let Some(id) = b {
        let world = pos_of(&world_of(id, tfs, cache));
        p.pos = world;
        p.prev = world;
        if let Some(t) = tfs.get(&id) {
            p.init_local_pos = t.pos;
            p.init_local_rot = t.rot;
        }
    } else {
        let pid = parent_index as usize;
        let Some(pid_id) = particles[pid].id else {
            return;
        };
        let pw = world_of(pid_id, tfs, cache);
        let parent_pos = pos_of(&pw);
        p.end_offset = if db.end_length > 0.0 {
            let gp = tfs
                .get(&pid_id)
                .and_then(|t| t.parent)
                .filter(|q| *q != 0 && *q != pid_id)
                .and_then(|g| {
                    let mut c = HashMap::new();
                    Some(pos_of(&world_of(g, tfs, &mut c)))
                });
            if let Some(gp) = gp {
                let world_point = [
                    parent_pos[0] * 2.0 - gp[0],
                    parent_pos[1] * 2.0 - gp[1],
                    parent_pos[2] * 2.0 - gp[2],
                ];
                let inv = mat_inv_pub(&pw);
                vscale(mat_vec_local(&inv, world_point), db.end_length)
            } else {
                // DynamicBone.AppendParticles uses local +X directly when
                // the terminal transform has no parent.
                [db.end_length, 0.0, 0.0]
            }
        } else {
            // AppendParticles uses Transform.TransformDirection(m_EndOffset),
            // where Transform is the component GameObject, not m_Root.
            let dir = mul_dir(&trs([0.0; 3], world_rot_of(component, tfs), [1.0; 3]), db.end_offset);
            let point = vadd(dir, parent_pos);
            let inv = mat_inv_pub(&pw);
            mat_vec_local(&inv, point)
        };
        p.pos = mat_vec_local(&pw, p.end_offset);
        p.prev = p.pos;
    }
    if parent_index >= 0 {
        let parent_pos = particles[parent_index as usize].pos;
        p.bone_length = bone_length + vlen(vsub(parent_pos, p.pos));
    }
    let count = particles.len() as i32;
    let next_len = p.bone_length;
    particles.push(p);
    let Some(id) = b else { return };
    for child in particle_children(tfs,id,db) {
        gather_dparticles(tfs, Some(child), count, next_len, db, component, particles, cache);
    }
    if children_of(tfs, id).is_empty() && (db.end_length > 0.0 || !vis_zero3(db.end_offset)) {
        gather_dparticles(tfs, None, count, next_len, db, component, particles, cache);
    }
}

/// DynamicBone_Ver01.SetupParticles follows m_Nodes in serialized order. Its
/// links need not be direct Transform parent/child pairs.
fn gather_ver01_particles(tfs: &HashMap<i64, Tf>, db: &DBone) -> Vec<DParticle> {
    let mut particles: Vec<DParticle> = Vec::new();
    let mut cache = HashMap::new();
    for &(id, rotation_calc) in &db.ver01_nodes {
        if !tfs.contains_key(&id) { continue; }
        let pos = parts_pos(tfs, id);
        let tf = &tfs[&id];
        let parent = particles.len() as i32 - 1;
        let (bone_length, local_position) = if parent >= 0 {
            let prev = &particles[parent as usize];
            let parent_world = world_of(prev.id.unwrap(), tfs, &mut cache);
            (prev.bone_length + vlen(vsub(prev.pos, pos)),
                mat_vec_local(&mat_inv_pub(&parent_world), pos))
        } else { (0.0, tf.pos) };
        particles.push(DParticle {
            id: Some(id), parent, pos, prev: pos, end_offset: [0.0; 3],
            init_local_pos: tf.pos, init_local_rot: tf.rot,
            damping: db.damping, elasticity: db.elasticity,
            stiffness: db.stiffness, inert: db.inert, radius: db.radius,
            bone_length, rotation_calc, local_position,
            add_angle_scale: db.add_angle_scale,
        });
    }
    if let Some(last) = particles.last() {
        if db.end_length > 0.0 || !vis_zero3(db.end_offset) {
            let parent = particles.len() - 1;
            let id = last.id.unwrap();
            let pw = world_of(id, tfs, &mut cache);
            let end_offset = if db.end_length > 0.0 {
                let gp = tfs.get(&id).and_then(|tf| tf.parent)
                    .filter(|p| *p != 0 && *p != id)
                    .map(|p| parts_pos(tfs, p));
                if let Some(gp) = gp {
                    let here = pos_of(&pw);
                    let point = vsub(vscale(here, 2.0), gp);
                    vscale(mat_vec_local(&mat_inv_pub(&pw), point), db.end_length)
                } else { [db.end_length, 0.0, 0.0] }
            } else { db.end_offset };
            let pos = mat_vec_local(&pw, end_offset);
            particles.push(DParticle {
                id: None, parent: parent as i32, pos, prev: pos, end_offset,
                init_local_pos: [0.0; 3], init_local_rot: [0.0, 0.0, 0.0, 1.0],
                damping: db.damping, elasticity: db.elasticity,
                stiffness: db.stiffness, inert: db.inert, radius: db.radius,
                bone_length: particles[parent].bone_length + vlen(vsub(particles[parent].pos, pos)),
                rotation_calc: true, local_position: end_offset,
                add_angle_scale: db.add_angle_scale,
            });
        }
    }
    particles
}

fn initialize_dparticle_parameters(db: &DBone, particles: &mut [DParticle]) {
    let total_length = particles.iter().map(|p| p.bone_length).fold(0.0f32, f32::max);
    let count = particles.len();
    for (index, particle) in particles.iter_mut().enumerate() {
        let t = if db.ver01 {
            if count > 1 { index as f32 / (count - 1) as f32 } else { 0.0 }
        } else if total_length > 0.0 {
            particle.bone_length / total_length
        } else { 0.0 };
        // Ordinary DynamicBone evaluates distributions only when the chain
        // has length; Ver01 always evaluates them by particle index.
        if db.ver01 || total_length > 0.0 {
            particle.damping *= curve_value(&db.damping_curve, t);
            particle.elasticity *= curve_value(&db.elasticity_curve, t);
            particle.stiffness *= curve_value(&db.stiffness_curve, t);
            particle.inert *= curve_value(&db.inert_curve, t);
            particle.radius *= curve_value(&db.radius_curve, t);
            if db.ver01 {
                particle.add_angle_scale *= curve_value(&db.add_angle_curve, t);
            }
        }
        particle.damping = particle.damping.clamp(0.0, 1.0);
        particle.elasticity = particle.elasticity.clamp(0.0, 1.0);
        particle.stiffness = particle.stiffness.clamp(0.0, 1.0);
        particle.inert = particle.inert.clamp(0.0, 1.0);
        particle.radius = particle.radius.max(0.0);
        particle.add_angle_scale = particle.add_angle_scale.max(0.0);
    }
}

fn parts_pos(tfs: &HashMap<i64, Tf>, id: i64) -> [f32; 3] {
    pos_of(&world_of(id, tfs, &mut HashMap::new()))
}

fn cached_parts_pos(tfs:&HashMap<i64,Tf>,id:i64,cache:&mut HashMap<i64,Mat4>)->[f32;3] {
    let value=pos_of(&world_of(id,tfs,cache));
    #[cfg(test)]
    assert_eq!(value,parts_pos(tfs,id),"root query must observe the current transform tree");
    value
}

fn mat_vec_local(m: &Mat4, p: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
        m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
        m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3],
    ]
}

fn vis_zero3(v: [f32; 3]) -> bool {
    v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0
}

fn simulate_dynamic(
    skel: &mut Skeleton,
    db: &DBone,
    colliders: &[BodyCollider],
    object_scale_override: Option<f32>,
    object_move_seed: [f32; 3],
    frames: u32,
    dt: f32,
) -> usize {
    if !skel.tfs.contains_key(&db.root) {
        return 0;
    }
    let mut cache = HashMap::new();
    let mut particles = Vec::new();
    let component = db.component_transform.filter(|id| skel.tfs.contains_key(id))
        .unwrap_or(db.root);
    if db.ver01 {
        particles = gather_ver01_particles(&skel.tfs, db);
    } else {
        gather_dparticles(&skel.tfs, Some(db.root), -1, 0.0, db, component, &mut particles, &mut cache);
    }
    if particles.len() < 2 {
        return 0;
    }
    initialize_dparticle_parameters(db, &mut particles);
    // UpdateDynamicBones reads Abs(Transform.lossyScale.x) from the component
    // GameObject. Its m_Root can be a scaled child elsewhere in the prefab.
    let component_w0 = world_of(component, &skel.tfs, &mut cache);
    let object_scale = object_scale_override.unwrap_or_else(||
        transform_lossy_scale(component, &skel.tfs, &component_w0)[0].abs()).max(1e-6);
    // DynamicBone.SetupParticles calls Root.InverseTransformDirection here.
    // Treating gravity as a point adds the root translation to the force and
    // makes identical chains swing differently depending on prefab placement.
    let local_gravity = root_local_gravity_rotation(world_rot_of(db.root, &skel.tfs), db.gravity);

    let mut object_move = object_move_seed;
    let px_key = crate::px::PxKey::new();
    let mut time = 0.0f32;
    let mut cache = HashMap::new();
    for _ in 0..frames {
        // DynamicBone.Update calls InitTransforms before LateUpdate on every
        // frame. Particle positions persist, but authored local transforms do
        // not: carrying the prior ApplyParticles rotation into the next frame
        // makes long chains progressively drift away from the game's pose.
        for particle in &particles {
            if let Some(id) = particle.id {
                if let Some(tf) = skel.tfs.get_mut(&id) {
                    tf.pos = particle.init_local_pos;
                    tf.rot = particle.init_local_rot;
                }
            }
        }
        cache.clear();
        // UpdateParticles1 gravity of this frame.
        let normalized = vnorm(db.gravity);
        let lhs = mul_dir(&trs([0.0; 3], world_rot_of(db.root, &skel.tfs), [1.0; 3]), local_gravity);
        let subtract = vscale(normalized, vdot(lhs, normalized).max(0.0));
        let source_gravity = if db.ver01 { db.gravity } else { vsub(db.gravity, subtract) };
        let gravity = vscale(vadd(source_gravity, db.force), object_scale);

        let mut num = 1i32;
        if db.update_rate > 0.0 {
            let step = 1.0 / db.update_rate;
            time += dt;
            num = 0;
            while time >= step {
                time -= step;
                num += 1;
                if num >= 3 {
                    time = 0.0;
                    break;
                }
            }
        }
        let px_on = crate::px::enabled();
        if px_on { px_solve_ordinary(skel, &mut particles, &mut cache, colliders, &[],
            &px_key, object_scale, gravity, object_move, db.update_rate, dt, db.ver01, db.freeze_axis); }
        if px_on {
            object_move = [0.0; 3];
        } else if num > 0 {
            // Weight/hardness of the one-to-one interaction (identity elsewhere), as the host solvers apply them.
            let tuning = crate::px::verlet_tuning();
            let gravity = vadd(gravity, tuning.gravity_step(if db.update_rate > 0.0 { 1.0 / db.update_rate } else { dt }));
            for _ in 0..num {
                // --- UpdateParticles1 ---
                for i in 0..particles.len() {
                    if particles[i].parent < 0 {
                        if let Some(id) = particles[i].id {
                            particles[i].prev = particles[i].pos;
                            particles[i].pos = cached_parts_pos(&skel.tfs, id,&mut cache);
                        }
                        continue;
                    }
                    let vel = vsub(particles[i].pos, particles[i].prev);
                    let v3 = vscale(object_move, particles[i].inert);
                    particles[i].prev = vadd(particles[i].pos, v3);
                    particles[i].pos = vadd(
                        vadd(
                            particles[i].pos,
                            vscale(vel, 1.0 - particles[i].damping * tuning.damp),
                        ),
                        vadd(gravity, v3),
                    );
                }
                update_particles2(skel, &mut particles, &mut cache, colliders, &[], object_scale,
                    db.freeze_axis, db.ver01, tuning);
                object_move = [0.0; 3];
            }
        } else if !db.suppress_skip_update {
            skip_update_particles(skel, &mut particles, object_move, &mut cache, object_scale, db.ver01);
        }
        apply_particles(skel, &mut particles, &mut cache, component, db.ver01);
    }
    particles.len()
}

/// Self-collision proxy of a character: one capsule per limb/torso segment, sized
/// from the live bone lengths (shape sliders scale them). Radii stay below the real
/// skin so rest-pose clothing is never inside a proxy.
/// Body capsules (self-collision proxies) of a skeleton: arms, legs, hands, torso, neck, pelvis, head, shoulders, upper
/// back, and a floor sphere last.
pub(crate) fn body_proxy_capsules(skel: &Skeleton) -> Vec<crate::px::OkPxCapsule> {
    let mut cache = HashMap::new();
    let mut by_name: HashMap<&str, i64> = HashMap::new();
    for (id, tf) in &skel.tfs { by_name.entry(tf.name.as_str()).or_insert(*id); }
    let mut pos = |name: &str| by_name.get(name).map(|id| pos_of(&world_of(*id, &skel.tfs, &mut cache)));
    let mut out: Vec<crate::px::OkPxCapsule> = Vec::new();
    let mut seg = |out: &mut Vec<crate::px::OkPxCapsule>, a: Option<[f32; 3]>, b: Option<[f32; 3]>, ratio: f32| {
        if let (Some(a), Some(b)) = (a, b) {
            let len = vlen(vsub(b, a));
            if len > 1e-4 { out.push(crate::px::OkPxCapsule { a, b, radius: len * ratio }); }
        }
    };
    for side in ["L", "R"] {
        let n = |base: &str| format!("{base}_{side}");
        seg(&mut out, pos(&n("cf_j_arm00")), pos(&n("cf_j_forearm01")), 0.13);
        seg(&mut out, pos(&n("cf_j_forearm01")), pos(&n("cf_j_hand")), 0.11);
        seg(&mut out, pos(&n("cf_j_thigh00")), pos(&n("cf_j_leg01")), 0.15);
        seg(&mut out, pos(&n("cf_j_leg01")), pos(&n("cf_j_leg03")), 0.10);
        if let Some(h) = pos(&n("cf_j_hand")) { out.push(crate::px::OkPxCapsule { a: h, b: h, radius: 0.03 }); }
    }
    seg(&mut out, pos("cf_j_spine01"), pos("cf_j_spine03"), 0.40);
    seg(&mut out, pos("cf_j_neck"), pos("cf_j_head"), 0.45);
    if let (Some(l), Some(r)) = (pos("cf_j_thigh00_L"), pos("cf_j_thigh00_R")) {
        seg(&mut out, Some(l), Some(r), 0.5);
    }
    if let (Some(neck), Some(head)) = (pos("cf_j_neck"), pos("cf_j_head")) {
        let up = vnorm(vsub(head, neck));
        out.push(crate::px::OkPxCapsule { a: vadd(head, vscale(up, 0.04)), b: vadd(head, vscale(up, 0.13)), radius: 0.075 });
    }
    // Shoulders and the upper back/chest: where long hair lands when the head is thrown back.
    seg(&mut out, pos("cf_j_spine03"), pos("cf_j_arm00_L"), 0.32);
    seg(&mut out, pos("cf_j_spine03"), pos("cf_j_arm00_R"), 0.32);
    seg(&mut out, pos("cf_j_spine03"), pos("cf_j_neck"), 0.85);
    // The ground under the character (very long hair reaches it): a huge sphere touching the root height.
    if let Some(root) = pos("cf_j_root") {
        let center = vsub(root, [0.0, 100.0, 0.0]);
        out.push(crate::px::OkPxCapsule { a: center, b: center, radius: 100.0 });
    }
    out
}

/// Publishes the body capsules, then (after a negative-radius separator) the same capsules grown by the clothes worn
/// over each of them: hair and accessories rest on the clothes, clothes on the skin.
pub(crate) fn publish_body_proxy(group: u64, skel: &Skeleton, cloth: &(Vec<f32>, [f32; 3])) {
    let (cloth, diag) = (&cloth.0, cloth.1);
    if group == 0 || crate::px::solver().is_none() { return; }
    let mut out = body_proxy_capsules(skel);
    // Separator (negative radius); its `a` carries the clothes-layer diagnostics (skin median offset, skin and clothes
    // vertex counts) for the host log.
    let skin = out.clone();
    out.push(crate::px::OkPxCapsule { a: diag, b: [0.0; 3], radius: -1.0 });
    if cloth.len() == skin.len() {
        out.extend(skin.iter().zip(cloth).map(|(c, t)| crate::px::OkPxCapsule { a: c.a, b: c.b, radius: c.radius + t.max(0.0) }));
    }
    crate::px::publish_body(group, &out);
}

/// How far the worn clothes stand out of each body capsule, from the clothes meshes: per capsule, the
/// 80th percentile of the clothes vertices that lie 0..8 cm outside it (tight garments, not a loose hem). Capsules with
/// few such vertices (bare skin) get 0. Empty when the meshes and the rest skeleton do not line up.
pub(crate) fn cloth_layer_thickness(rest: &Skeleton, parts: &[(bool, &[[f32; 3]])]) -> (Vec<f32>, [f32; 3]) {
    let caps = body_proxy_capsules(rest);
    let nearest = |v: [f32; 3]| -> Option<(usize, f32)> {
        let mut best: Option<(usize, f32)> = None;
        for (k, c) in caps.iter().enumerate() {
            if c.radius > 10.0 { continue; } // floor
            let ab = vsub(c.b, c.a); let l2 = vdot(ab, ab);
            let t = if l2 > 1e-12 { (vdot(vsub(v, c.a), ab) / l2).clamp(0.0, 1.0) } else { 0.0 };
            let off = vlen(vsub(v, vadd(c.a, vscale(ab, t)))) - c.radius;
            if best.map_or(true, |(_, o)| off < o) { best = Some((k, off)); }
        }
        best
    };
    let mut skin = Vec::new();
    let mut buckets: Vec<Vec<f32>> = vec![Vec::new(); caps.len()];
    for (clothes, positions) in parts {
        for v in positions.iter().step_by(2) {
            if !v.iter().all(|x| x.is_finite()) { continue; }
            let Some((k, off)) = nearest(*v) else { continue };
            if *clothes { if off > 0.0 && off < 0.08 { buckets[k].push(off); } } else { skin.push(off.abs()); }
        }
    }
    // The body mesh must sit on the capsules (within a few cm); otherwise the spaces differ and nothing is grown.
    let clothes_count = buckets.iter().map(|b| b.len()).sum::<usize>() as f32;
    skin.sort_by(|a, b| a.total_cmp(b));
    let median = skin.get(skin.len() / 2).copied().unwrap_or(-1.0);
    let diag = [median, skin.len() as f32, clothes_count];
    if skin.len() < 100 || median > 0.05 { return (Vec::new(), diag); }
    (buckets.into_iter().map(|mut b| {
        if b.len() < 30 { return 0.0; }
        b.sort_by(|x, y| x.total_cmp(y));
        b[(b.len() * 4) / 5]
    }).collect(), diag)
}

/// (bone, tip bone, radius / length, fallback tip direction length). Parent-first order.
const RAGDOLL_BONES: &[(&str, &str, f32)] = &[
    ("cf_j_hips", "cf_j_spine01", 0.55),
    ("cf_j_spine01", "cf_j_spine03", 0.42),
    ("cf_j_spine03", "cf_j_neck", 0.42),
    ("cf_j_head", "", 0.0),
    ("cf_j_arm00_L", "cf_j_forearm01_L", 0.13),
    ("cf_j_forearm01_L", "cf_j_hand_L", 0.11),
    ("cf_j_hand_L", "", 0.0),
    ("cf_j_arm00_R", "cf_j_forearm01_R", 0.13),
    ("cf_j_forearm01_R", "cf_j_hand_R", 0.11),
    ("cf_j_hand_R", "", 0.0),
    ("cf_j_thigh00_L", "cf_j_leg01_L", 0.15),
    ("cf_j_leg01_L", "cf_j_leg03_L", 0.10),
    ("cf_j_leg03_L", "cf_j_toes_L", 0.30),
    ("cf_j_thigh00_R", "cf_j_leg01_R", 0.15),
    ("cf_j_leg01_R", "cf_j_leg03_R", 0.10),
    ("cf_j_leg03_R", "cf_j_toes_R", 0.30),
];

/// Rigid frame of a bone from its world matrix. Unity's composed `Transform.rotation` ignores the
/// reflections of negative-scale ancestors (mirrored L/R rigs), so it does not describe the geometry;
/// the matrix does. A reflected basis gets its Z axis flipped to make a proper rotation.
fn bone_frame(world: &Mat4) -> [f32; 4] {
    let mut m = *world;
    for c in 0..3 {
        let l = vlen([m[0][c], m[1][c], m[2][c]]).max(1e-9);
        for r in 0..3 { m[r][c] /= l; }
    }
    for r in 0..3 { m[r][3] = 0.0; }
    let (c0, c1, c2) = ([m[0][0], m[1][0], m[2][0]], [m[0][1], m[1][1], m[2][1]], [m[0][2], m[1][2], m[2][2]]);
    if vdot(c0, vcross(c1, c2)) < 0.0 { for r in 0..3 { m[r][2] = -m[r][2]; } }
    quat_norm(quat_from_mat(&m))
}

/// Rotates a bone's world matrix by `delta` (world space, about the bone origin) and stores the
/// result as its local rotation; optionally moves the origin to `origin` (hips only).
fn rotate_bone_world(tfs: &mut HashMap<i64, Tf>, cache: &mut HashMap<i64, Mat4>, id: i64, delta: [f32; 4], origin: Option<[f32; 3]>) {
    let world = world_of(id, tfs, cache);
    let rotation = trs([0.0; 3], delta, [1.0; 3]);
    let mut moved = mat_mul_pub(&rotation, &world);
    for r in 0..3 { moved[r][3] = origin.map_or(world[r][3], |o| o[r]); }
    let parent_world = tfs.get(&id).and_then(|t| t.parent).filter(|p| *p != 0 && *p != id).map(|p| world_of(p, tfs, cache)).unwrap_or_else(ident);
    let local = mat_mul_pub(&mat_inv_pub(&parent_world), &moved);
    let Some(tf) = tfs.get_mut(&id) else { return };
    let mut basis = local;
    for c in 0..3 {
        let s = if tf.scale[c].abs() > 1e-9 { tf.scale[c] } else { 1.0 };
        for r in 0..3 { basis[r][c] /= s; }
    }
    tf.rot = quat_norm(quat_from_mat(&basis));
    if origin.is_some() { tf.pos = [local[0][3], local[1][3], local[2][3]]; }
    cache.clear();
}

/// Throws the head back by `degrees` (split over neck and head) about the body's left-right axis, on top of the
/// animated pose. Used by the clipping audit; generic over rigs (only standard body bone names are read).
/// Turns the head (neck 30 %, head 70 %) towards a world target in a frame built from the body, not from the head
/// bone's local axes (they differ between rigs, and a pitch about the local X could end up as a roll). `state` keeps
/// the smoothed (yaw, pitch) in degrees. Limits: yaw +-65, pitch up to +70 (head thrown back) and down to -45.
pub(crate) fn head_look_world(skel: &mut Skeleton, target: [f32; 3], state: &mut [f32; 2], dt: f32) -> Option<(f32, f32)> {
    let by_name: HashMap<String, i64> = skel.tfs.iter().map(|(id, tf)| (tf.name.clone(), *id)).collect();
    let (Some(&neck), Some(&head), Some(&spine), Some(&left), Some(&right)) = (by_name.get("cf_j_neck"), by_name.get("cf_j_head"),
        by_name.get("cf_j_spine03"), by_name.get("cf_j_arm00_L"), by_name.get("cf_j_arm00_R")) else { return None };
    if !target.iter().all(|v| v.is_finite()) { return None; }
    let mut cache = HashMap::new();
    let p = |id: i64, cache: &mut HashMap<i64, Mat4>, skel: &Skeleton| pos_of(&world_of(id, &skel.tfs, cache));
    let head_pos = p(head, &mut cache, skel);
    // The target comes in the skeleton's published world space (Skeleton::world_tf, which prefers the stored worlds:
    // they may include the pair placement); bones here are composed from their locals. Carry the target through the
    // head so both are in the same space (this flipped the heroine's look while the partner's was right).
    let target = match skel.world_tf("cf_j_head") {
        Some(published) => {
            let local = crate::pose::mat_vec(&crate::pose::mat_inv_pub(&published), target);
            crate::pose::mat_vec(&world_of(head, &skel.tfs, &mut cache), local)
        }
        None => target,
    };
    if !target.iter().all(|v| v.is_finite()) { return None; }
    let up = vnorm(vsub(head_pos, p(spine, &mut cache, skel)));
    let lateral0 = vsub(p(right, &mut cache, skel), p(left, &mut cache, skel));
    // Face direction in the animated pose: from between the ears to the nose tip (both always on the face, far
    // apart), flattened against the ear line.
    let face_of = |skel: &Skeleton, cache: &mut HashMap<i64, Mat4>| -> Option<[f32; 3]> {
        // The face looks along the head bone's local +Z on every Koikatsu rig (the face bones live in a separate
        // skeleton, so nose/ears are not available here).
        let f = vnorm(mul_dir(&world_of(head, &skel.tfs, cache), [0.0, 0.0, 1.0]));
        (f.iter().all(|v| v.is_finite()) && vdot(f, f) > 0.5).then_some(f)
    };
    let face_now = face_of(skel, &mut cache);
    // Body forward across the shoulders; its sign from the face (a neck cannot turn the face more than 90 degrees from
    // the chest). The bust bones are not in front of the spine in every pose: they flipped the heroine's frame, so she
    // measured the camera as "behind" and turned away.
    let mut forward = vnorm(vcross(lateral0, up));
    if let Some(f) = face_now { if vdot(f, forward) < 0.0 { forward = vscale(forward, -1.0); } }
    let side = vnorm(vcross(up, forward));
    let dir = vsub(target, head_pos);
    let (f, l, u) = (vdot(dir, forward), vdot(dir, side), vdot(dir, up));
    if (f * f + l * l + u * u).sqrt() < 0.05 { return None; }
    let yaw = l.atan2(f).to_degrees().clamp(-65.0, 65.0);
    let pitch = u.atan2((f * f + l * l).sqrt()).to_degrees().clamp(-45.0, 70.0);
    let alpha = if dt == 0.0 { 1.0 } else { 1.0 - (-18.0 * dt.max(0.0)).exp() };
    state[0] += (yaw - state[0]) * alpha;
    state[1] += (pitch - state[1]) * alpha;
    // Desired face direction (smoothed, limited) in the body frame.
    let (yr, pr) = (state[0].to_radians(), state[1].to_radians());
    let desired = vnorm([
        (forward[0] * yr.cos() + side[0] * yr.sin()) * pr.cos() + up[0] * pr.sin(),
        (forward[1] * yr.cos() + side[1] * yr.sin()) * pr.cos() + up[1] * pr.sin(),
        (forward[2] * yr.cos() + side[2] * yr.sin()) * pr.cos() + up[2] * pr.sin()]);
    // Where the face actually points in the animated pose: across the eyes and up the neck. The animation may already
    // turn or tilt the head; only the remaining (shortest) rotation is added, so the face ends on the target.
    let face = face_now.unwrap_or(forward);
    // Shortest rotation face -> desired, shared by neck (30 %) and head (70 %).
    let c = vcross(face, desired);
    let d = vdot(face, desired).clamp(-1.0, 1.0);
    let arc = if d < -0.9999 { quat_norm([up[0], up[1], up[2], 0.0]) } else { quat_norm([c[0], c[1], c[2], 1.0 + d]) };
    let angle = 2.0 * arc[3].clamp(-1.0, 1.0).acos();
    let axis_len = (arc[0] * arc[0] + arc[1] * arc[1] + arc[2] * arc[2]).sqrt();
    if axis_len > 1e-6 && angle.is_finite() {
        let axis = [arc[0] / axis_len, arc[1] / axis_len, arc[2] / axis_len];
        for (bone, share) in [(neck, 0.3f32), (head, 0.7f32)] {
            let h = angle * share * 0.5;
            let delta = quat_norm([axis[0] * h.sin(), axis[1] * h.sin(), axis[2] * h.sin(), h.cos()]);
            rotate_bone_world(&mut skel.tfs, &mut cache, bone, delta, None);
        }
    }
    skel.rebuild_worlds();
    // Check: how far the face still is from the (limited) desired direction, and how far that is from the target.
    let mut after = HashMap::new();
    let residual = face_of(skel, &mut after).map(|f| vdot(f, desired).clamp(-1.0, 1.0).acos().to_degrees()).unwrap_or(-1.0);
    let excess = vdot(desired, vnorm(dir)).clamp(-1.0, 1.0).acos().to_degrees();
    Some((residual, excess))
}

pub(crate) fn apply_head_tilt(skel: &mut Skeleton, degrees: f32) {
    if !(degrees.abs() > 0.01) { return; }
    let by_name: HashMap<String, i64> = skel.tfs.iter().map(|(id, tf)| (tf.name.clone(), *id)).collect();
    let (Some(&neck), Some(&head), Some(&spine), Some(&left), Some(&right)) = (by_name.get("cf_j_neck"), by_name.get("cf_j_head"),
        by_name.get("cf_j_spine03"), by_name.get("cf_j_arm00_L"), by_name.get("cf_j_arm00_R")) else { return };
    let mut cache = HashMap::new();
    let p = |id: i64, cache: &mut HashMap<i64, Mat4>, skel: &Skeleton| pos_of(&world_of(id, &skel.tfs, cache));
    let up = vnorm(vsub(p(head, &mut cache, skel), p(spine, &mut cache, skel)));
    let lateral = vsub(p(right, &mut cache, skel), p(left, &mut cache, skel));
    // Forward is where the chest points: the bust bones sit in front of the spine on every body.
    let forward_hint = by_name.get("cf_d_bust00").or(by_name.get("cf_j_bust01_L")).map(|id| vsub(p(*id, &mut cache, skel), p(spine, &mut cache, skel)));
    let mut forward = vnorm(vcross(lateral, up));
    if let Some(hint) = forward_hint { if vdot(hint, forward) < 0.0 { forward = vscale(forward, -1.0); } }
    // Rotating `up` about forward x up by +angle moves it towards -forward (backwards).
    let axis = vnorm(vcross(forward, up));
    let half = degrees.to_radians() * 0.25; // quaternion half-angle of half the tilt
    let delta = quat_norm([axis[0] * half.sin(), axis[1] * half.sin(), axis[2] * half.sin(), half.cos()]);
    rotate_bone_world(&mut skel.tfs, &mut cache, neck, delta, None);
    rotate_bone_world(&mut skel.tfs, &mut cache, head, delta, None);
    skel.rebuild_worlds();
}

/// What the ragdoll wrote last frame: (written local rot, pose before the write, written local pos).
/// If the animation did not overwrite the bone since, the pre-write pose is the animation again.
static RAGDOLL_WRITES: std::sync::Mutex<Option<HashMap<(u64, i64), ([f32; 4], [f32; 4], [f32; 3], [f32; 3])>>> = std::sync::Mutex::new(None);

/// Full-body PhysX ragdoll. The animated pose goes in as drive targets, the simulated pose
/// of every body part is written back before skinning. No-op unless the host enabled it.
pub(crate) fn step_ragdoll(group: u64, skel: &mut Skeleton, dt: f32) {
    if group == 0 { return; }
    // The skeleton is not re-sampled every frame: give back last frame's animated pose first, so the
    // ragdoll tracks the animation and not its own output.
    {
        let mut guard = RAGDOLL_WRITES.lock().unwrap();
        if let Some(map) = guard.as_mut() {
            let mut restored = false;
            map.retain(|(g, id), (written_rot, original_rot, written_pos, original_pos)| {
                if *g != group { return true; }
                if let Some(tf) = skel.tfs.get_mut(id) {
                    if tf.rot == *written_rot { tf.rot = *original_rot; restored = true; }
                    if tf.pos == *written_pos { tf.pos = *original_pos; restored = true; }
                }
                false
            });
            if restored { skel.rebuild_worlds(); }
        }
    }
    if !crate::px::ragdoll_enabled(group) { return; }
    let by_name: HashMap<String, i64> = skel.tfs.iter().map(|(id, tf)| (tf.name.clone(), *id)).collect();
    let mut cache = HashMap::new();
    let mut bones: Vec<crate::px::OkPxBone> = Vec::new();
    let mut ids: Vec<i64> = Vec::new();
    for (name, tip_name, ratio) in RAGDOLL_BONES {
        let Some(&id) = by_name.get(*name) else { continue };
        let world = world_of(id, &skel.tfs, &mut cache);
        let pos = pos_of(&world);
        let rot = bone_frame(&world);
        // Nearest ancestor that is itself a ragdoll part.
        let mut parent = -1i32;
        let mut cursor = skel.tfs.get(&id).and_then(|t| t.parent);
        while let Some(p) = cursor {
            if let Some(index) = ids.iter().position(|x| *x == p) { parent = index as i32; break; }
            cursor = skel.tfs.get(&p).and_then(|t| t.parent);
        }
        let tip = if tip_name.is_empty() {
            // Heads and hands: continue the direction of the previous segment.
            let from = if parent >= 0 { bones[parent as usize].pos } else { pos };
            let dir = vnorm(vsub(pos, from));
            let length = if *name == "cf_j_head" { 0.18 } else { 0.08 };
            vadd(pos, vscale(dir, length))
        } else if let Some(&tip_id) = by_name.get(*tip_name) {
            pos_of(&world_of(tip_id, &skel.tfs, &mut cache))
        } else { vadd(pos, [0.0, -0.1, 0.0]) };
        let length = vlen(vsub(tip, pos));
        let radius = if *name == "cf_j_head" { 0.085 } else if *name == "cf_j_hand_L" || *name == "cf_j_hand_R" { 0.035 }
            else { (length * ratio).max(0.02) };
        bones.push(crate::px::OkPxBone { parent, radius, pos, rot, tip });
        ids.push(id);
    }
    if bones.len() < 4 || !crate::px::ragdoll_step(group, &mut bones, dt) { return; }
    let mut cache = HashMap::new();
    let mut written = false;
    for (index, (bone, id)) in bones.iter().zip(&ids).enumerate() {
        // Joints keep the bone lengths, so only the hips move; every other part just rotates.
        // Parents were rotated first: compare with the child's frame as it is now.
        let sent = bone_frame(&world_of(*id, &skel.tfs, &mut cache));
        let alignment = (sent[0] * bone.rot[0] + sent[1] * bone.rot[1] + sent[2] * bone.rot[2] + sent[3] * bone.rot[3]).abs();
        let moved = index == 0 && vlen(vsub(pos_of(&world_of(*id, &skel.tfs, &mut cache)), bone.pos)) > 1e-6;
        if alignment < 1.0 - 1e-7 || moved {
            let delta = quat_norm(quat_mul(bone.rot, quat_conj(sent)));
            let (before_rot, before_pos) = skel.tfs.get(id).map(|t| (t.rot, t.pos)).unwrap_or(([0.0, 0.0, 0.0, 1.0], [0.0; 3]));
            rotate_bone_world(&mut skel.tfs, &mut cache, *id, delta, if index == 0 { Some(bone.pos) } else { None });
            if let Some(tf) = skel.tfs.get(id) {
                RAGDOLL_WRITES.lock().unwrap().get_or_insert_with(HashMap::new)
                    .insert((group, *id), (tf.rot, before_rot, tf.pos, before_pos));
            }
            written = true;
        }
    }
    if written { skel.rebuild_worlds(); }
}

pub(crate) fn px_capsule(collider: &BodyCollider, world: &Mat4, scale_z: f32) -> Option<crate::px::OkPxCapsule> {
    if collider.inside { return None; }
    let half = (collider.height - collider.radius) * 0.5;
    let mut lo = collider.center;
    let mut hi = collider.center;
    if half > 0.0 {
        lo[collider.direction] -= half;
        hi[collider.direction] += half;
    }
    Some(crate::px::OkPxCapsule { a: mat_vec_local(world, lo), b: mat_vec_local(world, hi), radius: collider.radius * scale_z })
}

/// PhysX replacement of UpdateParticles1/2 for one ordinary DynamicBone chain.
/// Returns false (nothing touched) when the host solver declines.
fn px_solve_ordinary(
    skel: &Skeleton, particles: &mut [DParticle], cache: &mut HashMap<i64, Mat4>,
    colliders: &[BodyCollider], external: &[(BodyCollider, Mat4, f32)], key: &crate::px::PxKey,
    object_scale: f32, gravity: [f32; 3], object_move: [f32; 3], update_rate: f32, dt: f32, ver01: bool, freeze_axis: usize,
) -> bool {
    let Some(mut input) = px_input_ordinary(skel, particles, cache, colliders, external, key, object_scale, gravity, object_move,
        update_rate, dt, ver01, freeze_axis) else { return false };
    if !crate::px::solve(key, &mut input.particles, &input.colliders, input.gravity_step, input.dt, input.reference_step, 1.0) { return false; }
    for (p, out) in particles.iter_mut().zip(&input.particles) { p.pos = out.pos; p.prev = out.prev; }
    true
}

/// The PhysX request of one ordinary chain (UpdateParticles1/2 replacement); None when it cannot be expressed.
fn px_input_ordinary(
    skel: &Skeleton, particles: &[DParticle], cache: &mut HashMap<i64, Mat4>,
    colliders: &[BodyCollider], external: &[(BodyCollider, Mat4, f32)], key: &crate::px::PxKey,
    object_scale: f32, gravity: [f32; 3], object_move: [f32; 3], update_rate: f32, dt: f32, ver01: bool, freeze_axis: usize,
) -> Option<crate::px::ChainInput> {
    if !dt.is_finite() || dt <= 0.0 || particles.len() < 2 { return None; }
    let mut input = Vec::with_capacity(particles.len());
    for i in 0..particles.len() {
        let p = &particles[i];
        if p.parent < 0 {
            let pos = p.id.map(|id| cached_parts_pos(&skel.tfs, id, cache)).unwrap_or(p.pos);
            input.push(crate::px::OkPxParticle { parent: -1, flags: crate::px::FLAG_ROOT, pos, prev: p.pos, ..Default::default() });
            continue;
        }
        let pidx = p.parent as usize;
        let Some(pid) = particles[pidx].id else { return None };
        let pw = world_of(pid, &skel.tfs, cache);
        let parent_transform_pos = pos_of(&pw);
        let self_transform_pos = p.id.map(|id| pos_of(&world_of(id, &skel.tfs, cache)));
        let rest_len = if p.id.is_none() {
            if ver01 { vlen(p.end_offset) * object_scale } else { vlen(mul_dir(&pw, p.end_offset)) }
        } else { vlen(vsub(parent_transform_pos, self_transform_pos.unwrap_or([0.0; 3]))) };
        let local = if ver01 { p.local_position }
            else { p.id.and_then(|id| skel.tfs.get(&id)).map(|t| t.pos).unwrap_or(p.end_offset) };
        let mut linear = pw;
        for row in 0..3 { linear[row][3] = 0.0; }
        let inertia = vscale(object_move, p.inert);
        // DynamicBone.UpdateParticles2 projects onto the plane through the simulated parent
        // whose normal is Transform.right/up/forward of the (unscaled) parent rotation.
        let freeze_normal = if !ver01 && freeze_axis > 0 {
            let rotation = trs([0.0; 3], world_rot_of(pid, &skel.tfs), [1.0; 3]);
            let axis = freeze_axis - 1;
            [rotation[0][axis], rotation[1][axis], rotation[2][axis]]
        } else { [0.0; 3] };
        input.push(crate::px::OkPxParticle {
            parent: p.parent, flags: 0, radius: p.radius * object_scale, damping: p.damping,
            elasticity: p.elasticity, stiffness: p.stiffness, rest_len,
            pos: vadd(p.pos, inertia), prev: vadd(p.prev, inertia),
            goal_offset: mat_vec_local(&linear, local), freeze_normal, ..Default::default()
        });
    }
    let mut capsules = Vec::new();
    for collider in colliders {
        let mut local_cache = HashMap::new();
        let world = world_of(collider.transform, &skel.tfs, &mut local_cache);
        let scale_z = transform_lossy_scale(collider.transform, &skel.tfs, &world)[2].abs();
        capsules.extend(px_capsule(collider, &world, scale_z));
    }
    for (collider, world, scale_z) in external { capsules.extend(px_capsule(collider, world, *scale_z)); }
    let reference_step = if update_rate > 0.0 { 1.0 / update_rate } else { 1.0 / 60.0 };
    Some(crate::px::ChainInput { key: key.id(), particles: input, colliders: capsules, gravity_step: gravity, dt, reference_step, velocity_scale: 1.0 })
}

fn update_particles2(
    skel: &mut Skeleton,
    particles: &mut [DParticle],
    cache: &mut HashMap<i64, Mat4>,
    colliders: &[BodyCollider],
    external_colliders: &[(BodyCollider, Mat4, f32)],
    object_scale: f32,
    freeze_axis: usize,
    ver01: bool,
    tuning: crate::px::VerletTuning,
) {
    let prepared:Vec<_>=external_colliders.iter().map(|(c,w,s)|PreparedCollision::new(c,w,*s)).collect();
    for i in 1..particles.len() {
        let pidx = particles[i].parent as usize;
        let Some(pid) = particles[pidx].id else { continue };
        let pw = world_of(pid, &skel.tfs, cache);
        let parent_transform_pos = pos_of(&pw);
        let self_transform_pos = particles[i].id.map(|id| pos_of(&world_of(id, &skel.tfs, cache)));
        let num = if particles[i].id.is_none() {
            if ver01 { vlen(particles[i].end_offset) * object_scale }
            else { vlen(mul_dir(&pw, particles[i].end_offset)) }
        } else {
            vlen(vsub(parent_transform_pos, self_transform_pos.unwrap_or([0.0; 3])))
        };
        let stiff = particles[i].stiffness;
        let num2 = 1.0 + (stiff - 1.0) * 1.0; // Lerp(1, stiffness, weight=1)
        let elastic = particles[i].elasticity * tuning.pull;
        if num2 > 0.0 || elastic > 0.0 {
            let mut l2w = pw;
            let ppos = particles[pidx].pos;
            l2w[0][3] = ppos[0];
            l2w[1][3] = ppos[1];
            l2w[2][3] = ppos[2];
            let local = if ver01 { particles[i].local_position }
                else { particles[i].id.and_then(|id| skel.tfs.get(&id))
                    .map(|t| t.pos).unwrap_or(particles[i].end_offset) };
            let vector = mat_vec_local(&l2w, local);
            let mut pos = particles[i].pos;
            pos = vadd(pos, vscale(vsub(vector, pos), elastic));
            if num2 > 0.0 {
                let d = vsub(vector, pos);
                let mag = vlen(d);
                let lim = num * (1.0 - num2) * 2.0 * tuning.range;
                if mag > lim && mag > 1e-8 {
                    pos = vadd(pos, vscale(d, (mag - lim) / mag));
                }
            }
            particles[i].pos = pos;
        }
        for collider in colliders {
            collide_particle(&mut particles[i].pos, particles[i].radius * object_scale, collider, skel);
        }
        for (index, collider) in prepared.iter().enumerate() {
            #[cfg(test)]
            let expected={let mut pos=particles[i].pos;let (c,w,s)=&external_colliders[index];
                collide_particle_world(&mut pos,particles[i].radius*object_scale,c,w,*s);pos};
            #[cfg(not(test))]
            let _=index;
            collider.collide(&mut particles[i].pos,particles[i].radius*object_scale);
            #[cfg(test)]
            assert_eq!(particles[i].pos,expected,"prepared contact must match original per-particle transform math");
        }
        if !ver01 && freeze_axis > 0 {
            // DynamicBone.UpdateParticles2 projects onto the plane through
            // the simulated parent, normal to Transform.right/up/forward.
            let axis = freeze_axis - 1;
            // Transform.right/up/forward use rotation, not the scaled
            // localToWorld matrix used above for positions and bone length.
            let rotation = trs([0.0; 3], world_rot_of(pid, &skel.tfs), [1.0; 3]);
            let normal = [rotation[0][axis], rotation[1][axis], rotation[2][axis]];
            let distance = vdot(vsub(particles[i].pos, particles[pidx].pos), normal);
            particles[i].pos = vsub(particles[i].pos, vscale(normal, distance));
        }
        let parent_pos = particles[pidx].pos;
        let d = vsub(parent_pos, particles[i].pos);
        let mag = vlen(d);
        if mag > 0.0 {
            particles[i].pos = vadd(particles[i].pos, vscale(d, (mag - num) / mag));
        }
    }
}

fn skip_update_particles(
    skel: &mut Skeleton,
    particles: &mut [DParticle],
    object_move: [f32; 3],
    cache: &mut HashMap<i64, Mat4>,
    object_scale: f32,
    ver01: bool,
) {
    for i in 0..particles.len() {
        if particles[i].parent < 0 {
            if let Some(id) = particles[i].id {
                particles[i].prev = particles[i].pos;
                particles[i].pos = cached_parts_pos(&skel.tfs, id,cache);
            }
            continue;
        }
        let move_here = if ver01 { vscale(object_move, particles[i].inert) }
            else { object_move };
        particles[i].prev = vadd(particles[i].prev, move_here);
        particles[i].pos = vadd(particles[i].pos, move_here);
        let pidx = particles[i].parent as usize;
        let Some(pid) = particles[pidx].id else { continue };
        let pw = world_of(pid, &skel.tfs, cache);
        let parent_transform_pos = pos_of(&pw);
        let self_transform_pos = particles[i].id.map(|id| pos_of(&world_of(id, &skel.tfs, cache)));
        let num = if particles[i].id.is_none() {
            if ver01 { vlen(particles[i].end_offset) * object_scale }
            else { vlen(mul_dir(&pw, particles[i].end_offset)) }
        } else {
            vlen(vsub(parent_transform_pos, self_transform_pos.unwrap_or([0.0; 3])))
        };
        let num2 = particles[i].stiffness; // Lerp(1, stiff, weight=1)
        if num2 > 0.0 {
            let mut l2w = pw;
            let ppos = particles[pidx].pos;
            l2w[0][3] = ppos[0];
            l2w[1][3] = ppos[1];
            l2w[2][3] = ppos[2];
            let local = particles[i]
                .id
                .and_then(|id| skel.tfs.get(&id))
                .map(|t| t.pos)
                .unwrap_or(particles[i].end_offset);
            let vector = mat_vec_local(&l2w, local);
            let d = vsub(vector, particles[i].pos);
            let mag = vlen(d);
            let lim = num * (1.0 - num2) * 2.0;
            if mag > lim && mag > 1e-8 {
                particles[i].pos = vadd(particles[i].pos, vscale(d, (mag - lim) / mag));
            }
        }
        let parent_pos = particles[pidx].pos;
        let d = vsub(parent_pos, particles[i].pos);
        let mag = vlen(d);
        if mag > 0.0 {
            particles[i].pos = vadd(particles[i].pos, vscale(d, (mag - num) / mag));
        }
    }
}

fn mirrored_direction_axes(tfs: &HashMap<i64, Tf>, component: i64) -> Vec<[f32; 3]> {
    // DynamicBone.ApplyParticlesToTransforms mirrors the target vector for a
    // negative local scale on the component or its immediate parent. Unity's
    // Transform.right/up/forward are rotation-only world axes.
    let mut axes = Vec::new();
    let component_tf = tfs.get(&component);
    let parent_tf = component_tf.and_then(|tf| tf.parent)
        .and_then(|id| tfs.get(&id).map(|tf| (id, tf)));
    for axis in 0..3 {
        let source = if component_tf.is_some_and(|tf| tf.scale[axis] < 0.0) {
            Some(component)
        } else if parent_tf.is_some_and(|(_, tf)| tf.scale[axis] < 0.0) {
            parent_tf.map(|(id, _)| id)
        } else {
            None
        };
        if let Some(id) = source {
            let rotation = trs([0.0; 3], world_rot_of(id, tfs), [1.0; 3]);
            axes.push([rotation[0][axis], rotation[1][axis], rotation[2][axis]]);
        }
    }
    axes
}

fn apply_particles(
    skel: &mut Skeleton,
    particles: &mut [DParticle],
    cache: &mut HashMap<i64, Mat4>,
    component: i64,
    ver01: bool,
) {
    apply_particles_with_children(skel,particles,cache,component,ver01,None,None);
}

fn apply_particles_with_children(
    skel:&mut Skeleton,particles:&mut [DParticle],cache:&mut HashMap<i64,Mat4>,
    component:i64,ver01:bool,child_counts:Option<&HashMap<i64,usize>>,children:Option<&HashMap<i64,Vec<i64>>>,
) {
    apply_particles_scoped(skel,particles,cache,component,ver01,child_counts,children,None);
}

fn apply_particles_scoped(
    skel:&mut Skeleton,particles:&mut [DParticle],cache:&mut HashMap<i64,Mat4>,
    component:i64,ver01:bool,child_counts:Option<&HashMap<i64,usize>>,children:Option<&HashMap<i64,Vec<i64>>>,
    mut rotations:Option<&mut HashMap<i64,[f32;4]>>,
) {
    #[cfg(test)]
    let oracle=if rotations.is_some()&&std::env::var_os("OK_TEST_ROTATION_SCOPE_PARITY").is_some(){
        let mut reference=skel.clone();let mut particles=particles.to_vec();let mut worlds=cache.clone();
        apply_particles_scoped(&mut reference,&mut particles,&mut worlds,component,ver01,child_counts,children,None);
        Some((reference,particles))
    }else{None};
    let mirrored_axes = mirrored_direction_axes(&skel.tfs, component);
    for i in 1..particles.len() {
        let pidx = particles[i].parent as usize;
        let Some(pid) = particles[pidx].id else { continue };
        if (ver01 && particles[pidx].rotation_calc)
            || (!ver01 && child_counts.map(|counts|counts.get(&pid).copied().unwrap_or(0))
                .unwrap_or_else(||children_of(&skel.tfs,pid).len()) <= 1) {
            let direction = if particles[i].id.is_none() {
                particles[i].end_offset
            } else if ver01 {
                particles[i].local_position
            } else {
                particles[i]
                    .id
                    .and_then(|id| skel.tfs.get(&id))
                    .map(|t| t.pos)
                    .unwrap_or([0.0; 3])
            };
            let parent_rotation = match rotations.as_deref_mut(){Some(rotations)=>scoped_world_rot(pid,&skel.tfs,rotations),None=>world_rot_of(pid,&skel.tfs)};
            let from = mul_dir(&trs([0.0; 3], parent_rotation, [1.0; 3]), direction);
            let mut vector = vsub(particles[i].pos, particles[pidx].pos);
            if !ver01 {
                for axis in &mirrored_axes {
                    vector = vsub(vector, vscale(*axis, 2.0 * vdot(vector, *axis)));
                }
            }
            let mut q = quat_from_to(from, vector);
            if ver01 && (particles[i].add_angle_scale - 1.0).abs() > 1e-6 {
                let half = q[3].clamp(-1.0, 1.0).acos();
                let sin_half = half.sin();
                if sin_half > 1e-7 {
                    let scaled = (half * particles[i].add_angle_scale).sin() / sin_half;
                    q = quat_norm([q[0] * scaled, q[1] * scaled, q[2] * scaled,
                        (half * particles[i].add_angle_scale).cos()]);
                }
            }
            let wrot = quat_mul(q, parent_rotation);
            set_world_rot_scoped(&mut skel.tfs, cache, pid, wrot,children,rotations.as_deref_mut());
        }
        if let Some(id) = particles[i].id {
            set_world_pos_with_children(&mut skel.tfs, cache, id, particles[i].pos,children);
        }
    }
    #[cfg(test)]
    if let Some((reference,expected))=oracle{
        for (id,tf) in &skel.tfs{let old=&reference.tfs[id];
            assert_eq!(tf.pos.map(f32::to_bits),old.pos.map(f32::to_bits));
            assert_eq!(tf.rot.map(f32::to_bits),old.rot.map(f32::to_bits));
            assert_eq!(tf.scale.map(f32::to_bits),old.scale.map(f32::to_bits));
        }
        for (actual,old) in particles.iter().zip(expected){
            assert_eq!(actual.pos.map(f32::to_bits),old.pos.map(f32::to_bits));
            assert_eq!(actual.prev.map(f32::to_bits),old.prev.map(f32::to_bits));
        }
    }
}

fn simulate(skel: &mut Skeleton, db: &DBone, steps: u32) -> usize {
    if !skel.tfs.contains_key(&db.root) {
        return 0;
    }
    let mut cache = HashMap::new();
    let mut particles = Vec::new();
    append_particles(&skel.tfs, Some(db.root), -1, db, &mut particles, &mut cache);
    if particles.len() < 2 {
        return 0;
    }
    let root_w = world_of(db.root, &skel.tfs, &mut cache);
    // DynamicBone.SetupParticles: m_LocalGravity = Root.InverseTransformDirection(m_Gravity);
    // m_ObjectScale = Transform.lossyScale.x.
    let root_scale = transform_lossy_scale(db.root, &skel.tfs, &root_w)[0].abs().max(1e-6);
    let local_gravity = root_local_gravity(&root_w, db.gravity);
    // UpdateParticles1: gravity -= normalized * Max(Dot(Root.TransformDirection(m_LocalGravity), normalized), 0);
    // gravity = (gravity + m_Force) * m_ObjectScale;
    let gn = vnorm(db.gravity);
    let lhs = mul_dir(&root_w, local_gravity);
    let grav = if vlen(db.gravity) > 1e-8 {
        vsub(db.gravity, vscale(gn, vdot(lhs, gn).max(0.0)))
    } else {
        db.gravity
    };
    let gravity = vscale(vadd(grav, db.force), root_scale);
    for _ in 0..steps {
        for i in 0..particles.len() {
            if particles[i].parent < 0 {
                let id = particles[i].id.unwrap_or(db.root);
                let p = pos_of(&world_of(id, &skel.tfs, &mut cache));
                particles[i].prev = particles[i].pos;
                particles[i].pos = p;
                continue;
            }
            let vel = vsub(particles[i].pos, particles[i].prev);
            particles[i].prev = particles[i].pos;
            particles[i].pos = vadd(
                particles[i].pos,
                vadd(vscale(vel, 1.0 - particles[i].damping), gravity),
            );
        }
        for i in 1..particles.len() {
            let pidx = particles[i].parent as usize;
            let parent_pos = particles[pidx].pos;
            let rest = particles[i].rest_len.max(1e-5);
            let stiff = particles[i].stiffness;
            let elas = particles[i].elasticity;
            if elas > 0.0 || stiff > 0.0 {
                let rest_world = if particles[i].id.is_some() {
                    let pid = particles[pidx].id.unwrap_or(db.root);
                    let mut pw = world_of(pid, &skel.tfs, &mut cache);
                    pw[0][3] = parent_pos[0];
                    pw[1][3] = parent_pos[1];
                    pw[2][3] = parent_pos[2];
                    let lp = particles[i].init_local_pos;
                    [
                        pw[0][0] * lp[0] + pw[0][1] * lp[1] + pw[0][2] * lp[2] + pw[0][3],
                        pw[1][0] * lp[0] + pw[1][1] * lp[1] + pw[1][2] * lp[2] + pw[1][3],
                        pw[2][0] * lp[0] + pw[2][1] * lp[1] + pw[2][2] * lp[2] + pw[2][3],
                    ]
                } else {
                    vadd(parent_pos, [0.0, -rest, 0.0])
                };
                let delta = vsub(rest_world, particles[i].pos);
                particles[i].pos = vadd(particles[i].pos, vscale(delta, elas));
                if stiff > 0.0 {
                    let delta = vsub(rest_world, particles[i].pos);
                    let mag = vlen(delta);
                    let lim = rest * (1.0 - stiff) * 2.0;
                    if mag > lim && mag > 1e-8 {
                        particles[i].pos = vadd(particles[i].pos, vscale(delta, (mag - lim) / mag));
                    }
                }
            }
            let delta = vsub(parent_pos, particles[i].pos);
            let mag = vlen(delta);
            if mag > 1e-8 {
                particles[i].pos = vadd(particles[i].pos, vscale(delta, (mag - rest) / mag));
            }
        }
    }
    for i in 1..particles.len() {
        let pidx = particles[i].parent as usize;
        let Some(pid) = particles[pidx].id else {
            continue;
        };
        let child_count = children_of(&skel.tfs, pid).len();
        if child_count <= 1 {
            let parent_pos = particles[pidx].pos;
            let vector = vsub(particles[i].pos, parent_pos);
            let direction = particles[i]
                .id
                .and_then(|id| skel.tfs.get(&id))
                .map(|t| t.pos)
                .unwrap_or(particles[i].init_local_pos);
            let parent_rotation = world_rot_of(pid, &skel.tfs);
            let from = mul_dir(&trs([0.0; 3], parent_rotation, [1.0; 3]), direction);
            let q = quat_from_to(from, vector);
            let wrot = quat_mul(q, parent_rotation);
            set_world_rot(&mut skel.tfs, &mut cache, pid, wrot);
        }
        if let Some(id) = particles[i].id {
            set_world_pos(&mut skel.tfs, &mut cache, id, particles[i].pos);
        }
    }
    particles.len()
}

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub(crate) struct DBoneSet {
    bones: Vec<DBone>,
    #[serde(serialize_with="crate::ordered::map")]
    colliders: HashMap<i64, BodyCollider>,
    rigs: Vec<implant::rig::Rig>,
}

impl DBoneSet {
    /// Authored hair assets use the same native solver as extracted hair.
    pub(crate) fn authored_hair(chains:&[(i64,f32,f32,f32,f32)])->Self {
        Self {bones:chains.iter().map(|&(root,damping,elasticity,stiffness,radius)|DBone {
            root,component_transform:Some(root),component_name:"TripoHair".into(),
            damping,elasticity,stiffness,radius,inert:0.35,update_rate:60.,
            gravity:[0.,-0.01,0.],add_angle_scale:1.,..Default::default()
        }).collect(),..Default::default()}
    }
}

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub(crate) struct BodyColliderSet { colliders: Vec<(String, BodyCollider)> }
impl BodyColliderSet {
    #[cfg(test)]
    pub(crate) fn count(&self)->usize {self.colliders.len()}
    pub(crate) fn extend_serialized(&mut self, file: &SerializedFile, source: &Skeleton, scripts:&crate::unity::references::Scripts) {
        let colliders = parse_colliders_resolved(file, Some(source),scripts);
        let children: HashMap<_, _> = file.objects.iter().filter(|o| o.type_id == 4).filter_map(|o| {
            let obj = UnityObject::from_serialized_file(file, o).ok()?;
            Some((o.path_id, pptr_list(obj.class.properties().get("m_Children"))))
        }).collect();
        fn walk(id: i64, children: &HashMap<i64, Vec<i64>>, order: &mut Vec<i64>) {
            order.push(id);
            if let Some(ids) = children.get(&id) { for child in ids { walk(*child, children, order); } }
        }
        let mut roots: Vec<_> = source.tfs.iter().filter(|(_, tf)| tf.parent.is_none() || tf.parent == Some(0)
            || tf.parent.is_some_and(|p| !source.tfs.contains_key(&p))).map(|(id, _)| *id).collect();
        roots.sort_unstable();
        let mut order = Vec::new(); for root in roots { walk(root, &children, &mut order); }
        // GetComponentsInChildren follows Transform.m_Children, not the
        // serialized object table or numeric PPtr order. Collision order matters.
        for id in order { for (_, c) in colliders.iter().filter(|(_, c)| c.transform == id) {
            if let Some(tf) = source.tfs.get(&id) { self.colliders.push((tf.name.clone(), c.clone())); }
        }}
    }
    fn bound(&self, target: &Skeleton) -> Vec<BodyCollider> {
        let mut names=HashMap::new();
        for (id,tf) in &target.tfs {names.entry(tf.name.as_str()).or_insert(*id);}
        self.colliders.iter().filter_map(|(name, c)| {
            let mut c = c.clone();
            c.transform = *names.get(name.as_str())?;
            Some(c)
        }).collect()
    }
}

type ColliderPose=(BodyCollider,Mat4,f32);
fn collider_pose(c:BodyCollider,skel:&Skeleton)->ColliderPose {
    collider_pose_cached(c,skel,&mut HashMap::new())
}
/// `collider_pose` sharing the world matrices of one skeleton between colliders posed in the same frame (the same
/// products as computing each one alone; many colliders hang under the same few bones).
fn collider_pose_cached(c:BodyCollider,skel:&Skeleton,worlds:&mut HashMap<i64,Mat4>)->ColliderPose {
    let world=world_of(c.transform,&skel.tfs,worlds);
    let scale_z=transform_lossy_scale(c.transform,&skel.tfs,&world)[2].abs();
    (c,world,scale_z)
}

// Immutable and local to one refresh. Body and coordinated-clothes rigs get
// separate frames; no particle state, private rig or character is shared.
pub(crate) struct CharacterColliderFrame {
    native:Vec<ColliderPose>,ordinary:[Vec<ColliderPose>;2],flags:game_colliders::Flags,
}
impl CharacterColliderFrame {
    pub(crate) fn new(body:&Skeleton,native:&BodyColliderSet,flags:game_colliders::Flags)->Self {
        let mut worlds=HashMap::new();
        let native=native.bound(body).into_iter().map(|c|collider_pose_cached(c,body,&mut worlds)).collect();
        let ordinary=std::array::from_fn(|i|game_colliders::ordinary_colliders(body,flags,i==1).into_iter().map(|c|collider_pose_cached(c,body,&mut worlds)).collect());
        Self {native,ordinary,flags}
    }
}

#[cfg(test)]
mod collider_frame_tests {
    use super::*;
    #[test]
    fn prepared_contacts_match_original_sphere_capsule_and_boundaries() {
        let mut checked=0;
        for inside in [false,true] {for direction in 0..3 {for height in [0.0,0.2,0.7] {
            for scale in [[1.0;3],[-1.2,0.7,1.4],[0.0,1.0,0.0]] {
                let c=BodyCollider{transform:1,center:[0.1,-0.2,0.3],radius:0.2,height,direction,inside};
                let world=trs([0.1,0.3,-0.4],[0.0,0.6,0.0,0.8],scale);
                let scale_z=scale[2].abs();let prepared=PreparedCollision::new(&c,&world,scale_z);
                for radius in [0.0,0.03,0.2] {for x in -5..=5 {for y in -5..=5 {for z in -5..=5 {
                    let mut old=[x as f32*0.1,y as f32*0.1,z as f32*0.1];let mut new=old;
                    collide_particle_world(&mut old,radius,&c,&world,scale_z);prepared.collide(&mut new,radius);
                    assert_eq!(new,old);checked+=1;
                }}}}
                for pos in [prepared.a,prepared.b,vadd(prepared.a,[0.2*scale_z,0.0,0.0])] {
                    let mut old=pos;let mut new=pos;
                    collide_particle_world(&mut old,0.0,&c,&world,scale_z);prepared.collide(&mut new,0.0);assert_eq!(new,old);
                }
            }
        }}}
        eprintln!("OK prepared contacts exact cases={checked}");
    }
    #[test]
    fn prepared_binding_preserves_duplicates_case_missing_and_pose() {
        let mut body=Skeleton {names:Vec::new(),worlds:Vec::new(),rest:Vec::new(),smr_count:0,bone_ids:Vec::new(),source_mesh:0,tfs:HashMap::new()};
        for (id,name) in [(1,"Root"),(2,"Root"),(3,"cf_j_root")] {
            body.tfs.insert(id,Tf{name:name.into(),parent:None,sibling_index:None,pos:[id as f32,0.3,0.4],rot:[0.0,0.0,0.0,1.0],scale:[-1.2,0.7,1.4]});
        }
        let collider=|id|BodyCollider{transform:id,center:[0.1,0.2,0.3],radius:0.2,height:0.7,direction:1,inside:false};
        let native=BodyColliderSet{colliders:vec![("Root".into(),collider(99)),("Root".into(),collider(98)),("ROOT".into(),collider(97)),("missing".into(),collider(96))]};
        let flags=game_colliders::Flags{installed:true,floor:true,breast:false,skirt:true};
        for pass in 0..2 {
            let expected:Vec<_>=native.colliders.iter().filter_map(|(name,c)|{let mut c=c.clone();c.transform=body.tfs.iter().find(|(_,tf)|tf.name==*name).map(|(id,_)|*id)?;
                let world=world_of(c.transform,&body.tfs,&mut HashMap::new());let scale=transform_lossy_scale(c.transform,&body.tfs,&world)[2].abs();Some((c,world,scale))}).collect();
            let actual=CharacterColliderFrame::new(&body,&native,flags);
            assert_eq!(actual.native,expected);assert_eq!(actual.native.len(),2);
            assert_eq!(actual.ordinary[0].len(),1);assert_eq!(actual.ordinary[1].len(),1);
            if pass==1 {assert_eq!(actual.native[0].1[0][3],42.0);}
            for tf in body.tfs.values_mut(){tf.pos[0]=42.0;}
        }
    }
}

impl DBoneSet {
    pub(crate) fn extend(&mut self, other: Self) { self.bones.extend(other.bones); self.colliders.extend(other.colliders); self.rigs.extend(other.rigs); }

    /// Body replacement components survive UncensorSelector.RebindSkinning.
    /// Resolve their local PPtrs by Transform name, preserving their authored
    /// colliders and both ordinary DynamicBone versions. Clothes-copy rules
    /// remove components/colliders and must not be used for the body itself.
    pub(crate) fn retarget_body(&self, source:&Skeleton, body:&Skeleton) -> Self {
        let names:HashMap<_,_>=body.tfs.iter().map(|(id,t)| (t.name.as_str(),*id)).collect();
        let remap=|id|source.tfs.get(&id).and_then(|t|names.get(t.name.as_str()).copied());
        let mut collider_ids=HashMap::new();
        let mut colliders=HashMap::new();
        let mut original:Vec<_>=self.colliders.iter().collect();original.sort_by_key(|(id,_)|**id);
        for (index,(id,collider)) in original.into_iter().enumerate() {
            if let Some(transform)=remap(collider.transform) {
                let key=i64::MIN+index as i64;
                let mut collider=collider.clone();collider.transform=transform;
                collider_ids.insert(*id,key);colliders.insert(key,collider);
            }
        }
        let bones=self.bones.iter().filter_map(|original| {
            let mut db=original.clone();db.root=remap(db.root)?;
            db.component_transform=db.component_transform.and_then(remap);
            db.exclusions=db.exclusions.iter().filter_map(|id|remap(*id)).collect();
            db.not_rolls=db.not_rolls.iter().filter_map(|id|remap(*id)).collect();
            db.ver01_nodes=db.ver01_nodes.iter().filter_map(|(id,rotation_calc)|
                remap(*id).map(|id|(id,*rotation_calc))).collect();
            db.collider_refs=db.collider_refs.iter().filter_map(|id|collider_ids.get(id).copied()).collect();
            db.implanted_collider_refs=db.implanted_collider_refs.iter().filter_map(|id|collider_ids.get(id).copied()).collect();
            // Shared-character configuration clears the clothes-copy collider
            // list. Preserve replacement-body references in the existing
            // per-chain collider channel, which follows live body transforms.
            // Ver01 bypasses shared-character collider configuration and reads
            // its authored collider list directly during particle updates.
            if !db.ver01 { db.implanted_collider_refs.extend(db.collider_refs.drain(..)); }
            Some(db)
        }).collect();
        Self{bones,colliders,rigs:Vec::new()}
    }
    /// AssignedWeightsAndSetBounds destroys the cloned cf_j_root subtree.
    /// An unmatched reference outside it remains a live prefab Transform.
    pub(crate) fn removed_clothes_transform(source: &Skeleton, mut id: i64) -> bool {
        for _ in 0..source.tfs.len() {
            let Some(tf) = source.tfs.get(&id) else { return false; };
            if tf.name == "cf_j_root" { return true; }
            let Some(parent) = tf.parent else { return false; }; id = parent;
        }
        false
    }
    pub(crate) fn private_clothes(&self, source: &Skeleton, body: &Skeleton,
        copy_dynamic: bool, component_name: &str) -> Self {
        let names: std::collections::HashSet<_> = body.tfs.values().map(|tf| tf.name.as_str()).collect();
        let bones = self.bones.iter().filter(|db| {
            !db.component_transform.is_some_and(|id| Self::removed_clothes_transform(source, id))
                && !Self::removed_clothes_transform(source, db.root)
                && (db.ver01 || !copy_dynamic || source.tfs.get(&db.root)
                    .is_some_and(|tf| !names.contains(tf.name.as_str())))
        }).map(|db| {
            let mut db = db.clone();
            if !db.ver01 && copy_dynamic {
                // ChaControl already redirected these matching references to
                // body nodes outside the surviving private particle subtree.
                let private_reference = |id:&i64| source.tfs.get(id).is_none_or(|tf|!names.contains(tf.name.as_str()));
                db.exclusions.retain(private_reference); db.not_rolls.retain(private_reference);
            }
            // Only the instantiated prefab root is renamed by ChaControl.
            db.component_name = db.component_transform.and_then(|id| source.tfs.get(&id))
                .filter(|tf| tf.parent.is_some_and(|id| id != 0 && source.tfs.contains_key(&id)))
                .map(|tf| tf.name.clone()).unwrap_or_else(|| component_name.into());
            db
        }).collect();
        Self { bones, colliders: self.colliders.clone(), rigs:self.rigs.clone() }
    }
    pub(crate) fn add_implanted_colliders(&mut self, markers:&[implant::Marker], implanted:&std::collections::HashSet<i64>) {
        let mut ids=Vec::new();
        for marker in markers {for id in &marker.colliders {
            if self.colliders.get(id).is_some_and(|c|implanted.contains(&c.transform)) && !ids.contains(id) {ids.push(*id);}
        }}
        for db in &mut self.bones {if !db.ver01 {db.implanted_collider_refs=ids.clone();}}
    }
    pub(crate) fn retarget_clothes(&self, source: &Skeleton, body: &Skeleton, component_name: &str,
        parent_world: Option<&Mat4>) -> Self {
        let names: HashMap<_, _> = body.tfs.iter().map(|(id, tf)| (tf.name.as_str(), *id)).collect();
        let remap = |id: i64| source.tfs.get(&id).and_then(|tf| names.get(tf.name.as_str()).copied());
        let component = names.get("p_cf_body_bone").copied();
        let bones = self.bones.iter().filter_map(|db| {
            if db.ver01 || db.component_transform.is_some_and(|id| Self::removed_clothes_transform(source, id)) {
                return None; // only surviving ordinary MonoBehaviours are copied.
            }
            let mut db = db.clone(); db.root = remap(db.root)?;
            db.exclusions = db.exclusions.iter().filter_map(|id| remap(*id)).collect();
            db.not_rolls = db.not_rolls.iter().filter_map(|id| remap(*id)).collect();
            db.component_name = db.component_transform.and_then(|id| source.tfs.get(&id))
                .filter(|tf| tf.parent.is_some_and(|id| id != 0 && source.tfs.contains_key(&id)))
                .map(|tf| tf.name.clone()).unwrap_or_else(|| component_name.into());
            let mut component_id = db.component_transform;
            while let Some(id) = component_id {
                let Some(tf) = source.tfs.get(&id) else { break; };
                db.component_ancestry.push(tf.clone()); component_id = tf.parent;
                if db.component_ancestry.len() >= source.tfs.len() { break; }
            }
            db.component_ancestry.reverse();
            if let Some(parent) = parent_world.and_then(attached_parent_tf) {
                db.component_ancestry.insert(0, parent);
            }
            for collider in db.implanted_collider_refs.iter().filter_map(|id|self.colliders.get(id)) {
                let mut ancestry=Vec::new(); let mut id=Some(collider.transform);
                while let Some(current)=id {
                    let Some(tf)=source.tfs.get(&current) else {break;};
                    ancestry.push(tf.clone()); id=tf.parent;
                    if ancestry.len()>=source.tfs.len() {break;}
                }
                ancestry.reverse();
                db.implanted_collider_ancestry.push((collider.clone(),ancestry));
            }
            db.implanted_collider_refs.clear();
            db.component_transform = component;
            db.collider_refs.clear(); // native body colliders are rebound at runtime.
            Some(db)
        }).collect();
        Self { bones, colliders: HashMap::new(), rigs:Vec::new() }
    }
    pub(crate) fn with_game_fixes(mut self, game_root: &Path) -> Self {
        let disabled = dynamic_bones_fix_enabled(game_root);
        for db in &mut self.bones { db.suppress_skip_update = disabled && !db.ver01; }
        self
    }
    pub(crate) fn is_empty(&self) -> bool { self.bones.is_empty() }
    pub(crate) fn len(&self) -> usize { self.bones.len() }
    /// Roots that can rewrite a genital bone. Used to compare a portable
    /// snapshot with a fresh extract; not a card-name filter.
    pub(crate) fn describe_named(&self, skel: &Skeleton) -> String {
        let mut suppress_off = 0usize;
        let mut lines = Vec::new();
        for db in &self.bones {
            if !db.suppress_skip_update { suppress_off += 1; }
            let root = skel.tfs.get(&db.root).map(|t| t.name.as_str()).unwrap_or("missing");
            let folded = root.to_ascii_lowercase();
            let comp = db.component_name.to_ascii_lowercase();
            if folded.contains("vagina") || folded.contains("kokan") || comp.contains("vagina") || comp.contains("kokan") {
                lines.push(format!(
                    "root={root} comp={} v1={} skipfix={} rate={} grav={:?} force={:?} d={} e={} s={} inert={}",
                    db.component_name, db.ver01, db.suppress_skip_update, db.update_rate,
                    db.gravity, db.force, db.damping, db.elasticity, db.stiffness, db.inert));
            }
        }
        format!("n={} suppress_off={suppress_off} genital_roots={} [{}]", self.bones.len(), lines.len(), lines.join(" | "))
    }
    /// ChaControl.ChangeShakeHair/Accessory toggles DynamicBone components
    /// only. A mod's DynamicBone_Ver01 retains its serialized enabled state.
    pub(crate) fn after_no_shake(&self) -> Self {
        let bones: Vec<_> = self.bones.iter().filter(|db| db.ver01).cloned().collect();
        let ids: std::collections::HashSet<i64> = bones.iter()
            .flat_map(|db| db.collider_refs.iter().chain(&db.implanted_collider_refs).copied()).collect();
        let colliders = self.colliders.iter()
            .filter(|(id, _)| ids.contains(id)).map(|(id, c)| (*id, c.clone())).collect();
        Self { bones, colliders, rigs:self.rigs.clone() }
    }
    #[cfg(test)]
    pub(crate) fn first_root(&self) -> Option<i64> { self.bones.first().map(|db| db.root) }

    /// DynamicBoneEditor rows of one accessory slot: each component whose m_Root (named in `skel`) matches a row's
    /// BoneName takes that row's parameters. Returns how many components changed.
    pub(crate) fn apply_editor(&mut self, edits: &[&DynamicBoneEdit], skel: &Skeleton) -> usize {
        let mut changed = 0;
        for db in &mut self.bones {
            let Some(name) = skel.tfs.get(&db.root).map(|t| t.name.clone()).or_else(|| db.source_root_name.clone()) else { continue };
            let Some(edit) = edits.iter().rev().find(|e| e.bone == name) else { continue };
            if let Some(v) = edit.freeze_axis { db.freeze_axis = v; }
            if let Some(v) = edit.damping { db.damping = v; }
            if let Some(v) = edit.elasticity { db.elasticity = v; }
            if let Some(v) = edit.stiffness { db.stiffness = v; }
            if let Some(v) = edit.inertia { db.inert = v; }
            if let Some(v) = edit.radius { db.radius = v; }
            // m_Weight: UpdateParticles2 keeps the shape with Lerp(1, m_Stiffness * curve(t), weight)
            // = (1 - w) + w * m_Stiffness * curve(t). That is linear in the curve, so it is folded into the stiffness
            // and its distribution (Hermite values and tangents scale/offset exactly) instead of a new field: DBone is
            // bincode-serialized in OK library records, where any layout change makes every record unreadable.
            if let Some(w) = edit.weight.map(|w| w.clamp(0.0, 1.0)).filter(|w| *w < 1.0) {
                let scale = w * db.stiffness;
                if db.stiffness_curve.is_empty() { db.stiffness = (1.0 - w) + scale; }
                else {
                    for key in &mut db.stiffness_curve { key.value = (1.0 - w) + scale * key.value; key.in_slope *= scale; key.out_slope *= scale; }
                    db.stiffness = 1.0;
                }
            }
            changed += 1;
        }
        changed
    }
    /// OK_DBONE_DIAG: every component of this set, its root and whether `skel` (the renderer's rig) contains it.
    pub(crate) fn diag_against(&self, skel: &Skeleton, label: &str) {
        for db in &self.bones {
            let root = skel.tfs.get(&db.root).map(|t| t.name.clone()).or_else(|| db.source_root_name.clone()).unwrap_or_default();
            let path: Vec<&str> = db.component_ancestry.iter().map(|t| t.name.as_str()).collect();
            eprintln!("ok-extract: DBONE {label} component={} path={} root={} ({}) in_rig={} overlay={} gravity={:?} force={:?} stiffness={} elasticity={} damping={}",
                db.component_name, path.join("/"), db.root, root, skel.tfs.contains_key(&db.root), db.overlay_bindings.is_some(), db.gravity, db.force, db.stiffness, db.elasticity, db.damping);
        }
    }
    pub(crate) fn extend_for_skeleton(&mut self, other: &Self, skel: &Skeleton) {
        let filtered = other.for_skeleton(skel);
        self.bones.extend(filtered.bones);
        self.colliders.extend(filtered.colliders); self.rigs.extend(filtered.rigs);
    }

    pub(crate) fn for_skeleton(&self, skel: &Skeleton) -> Self {
        let bones: Vec<_> = self.bones.iter()
            .filter(|db| db.overlay_bindings.is_some() || skel.tfs.contains_key(&db.root)).cloned().collect();
        let colliders = self.colliders.iter()
            // Implantor appends colliders which were not authored in m_Colliders.
            .filter(|(_, c)| skel.tfs.contains_key(&c.transform))
            .map(|(id, c)| (*id, c.clone())).collect();
        Self { bones, colliders, rigs:self.rigs.clone() }
    }
}

/// Persistent DynamicBone particles for an accessory attached to a moving
/// character socket. The prefabricated skeleton remains beneath a synthetic
/// parent so `UpdateParticles1/2` sees the same world-space movement that
/// Unity's `SetParent(..., false)` gives its component.
#[derive(Clone, Debug)]
pub(crate) struct AttachedDynamicSet {
    pub skel: Skeleton,
    parent_id: i64,
    exact_placement: Mat4,
    chains: Vec<AttachedChain>,
    shared_component_tfs: HashMap<i64, Tf>,
    body_space: bool,
    attachment_ids: Vec<i64>,
    // Scratch only: never part of particle history or a serialized resource.
    world_cache: HashMap<i64,Mat4>,
    rotation_cache: HashMap<i64,[f32;4]>,
    lossy_rotation_cache:HashMap<i64,LossyRotation>,
    child_counts: HashMap<i64,usize>,
    child_ids: HashMap<i64,Vec<i64>>,
    child_topology:Vec<(i64,Option<i64>)>,
    /// PhysX scene of this set: all its chains are solved in one batch.
    px_batch: crate::px::PxKey,
    /// The character's clothes set (`clothes_dynamics`): its replacement-body chains move skin no garment follows.
    /// Hair and accessory sets also have chains without a clothes name; they are never skin.
    skin_chains: bool,
}

#[derive(Clone, Debug)]
struct AttachedChain {
    db: DBone,
    authored_freeze_axis: usize,
    particles: Vec<DParticle>,
    component: i64,
    local_gravity: [f32; 3],
    prev_component_pos: [f32; 3],
    time: f32,
    colliders: Vec<BodyCollider>,
    // Radius uses abs(Transform.lossyScale.z), which cannot be recovered from
    // a sheared world matrix alone. Preserve it from the collider's own rig.
    external_colliders: Vec<(BodyCollider, Mat4, f32)>,
    implanted_colliders: Vec<BodyCollider>,
    implanted_collider_offset: usize,
    px_key: crate::px::PxKey,
    /// Per particle: how thick the mesh drawn around the bone that ends at it is (m, world). Empty = unknown.
    body_radius: Vec<f32>,
    /// What this chain is (hair, tail, ordinary accessory), set by `classify_chains`; None = the part's own component.
    physics: Option<crate::px::PhysicsComponent>,
}

fn attached_parent_tf(placement: &Mat4) -> Option<Tf> {
    let mut scale = [0.0f32; 3];
    let mut rotation = *placement;
    for axis in 0..3 {
        scale[axis] = vlen([placement[0][axis], placement[1][axis], placement[2][axis]]);
        if scale[axis] < 1e-7 { return None; }
        for row in 0..3 { rotation[row][axis] /= scale[axis]; }
    }
    Some(Tf { sibling_index: None, name: "__ok_accessory_placement".into(), parent: None,
        pos: pos_of(placement), rot: quat_from_mat(&rotation), scale })
}

impl AttachedDynamicSet {
    #[cfg(test)]
    pub(crate) fn particle_snapshot(&self)->Vec<([f32;3],[f32;3])>{
        self.chains.iter().flat_map(|c|c.particles.iter().map(|p|(p.pos,p.prev))).collect()
    }
    #[cfg(test)]
    pub(crate) fn verify_prepared_colliders(&self,body:&Skeleton,native:&BodyColliderSet,flags:game_colliders::Flags) {
        let mut old=self.clone();let mut new=self.clone();
        old.configure_character_colliders_reference(body,native,flags);
        new.configure_prepared_character_colliders(&CharacterColliderFrame::new(body,native,flags));
        for (a,b) in old.chains.iter().zip(&new.chains) {
            assert_eq!(a.external_colliders,b.external_colliders,"collider order/world/lossyScale must remain exact");
            assert_eq!(a.implanted_collider_offset,b.implanted_collider_offset);
            assert_eq!(a.db.freeze_axis,b.db.freeze_axis);
        }
        old.step(1.0/60.0);new.step(1.0/60.0);
        assert_eq!(old.particle_snapshot(),new.particle_snapshot(),"collisions must retain exact particle positions/history");
    }
    #[cfg(test)]
    pub(crate) fn audit_particle_positions(&self) -> Vec<[f32; 3]> {
        self.chains.iter().flat_map(|chain| chain.particles.iter().map(|p|p.pos)).collect()
    }

    pub(crate) fn new_unsettled_with_chain(set:&DBoneSet,source:&Skeleton,chain:&[Tf])->Option<Self> {
        if chain.is_empty(){return None;}
        let mut source=source.clone();
        let roots:Vec<_>=source.tfs.iter().filter(|(_,tf)|tf.parent.is_none()||tf.parent==Some(0)).map(|(id,_)|*id).collect();
        let mut ids=Vec::new();let mut parent=None;
        for tf in chain {
            let id=(i64::MIN..).find(|id|!source.tfs.contains_key(id))?;
            let mut tf=tf.clone();tf.parent=parent;source.tfs.insert(id,tf);ids.push(id);parent=Some(id);
        }
        for id in roots {source.tfs.get_mut(&id)?.parent=parent;}
        source.rebuild_worlds();
        let mut state=Self::new_unsettled(set,&source,&ident())?;state.attachment_ids=ids;Some(state)
    }

    pub(crate) fn set_attachment_chain(&mut self,chain:&[Tf])->bool {
        self.set_attachment_chain_for_step(chain,false)
    }
    /// The ancestors copied in each frame from the body or head (set_attachment_chain).
    pub(crate) fn attachment_ids(&self)->&[i64]{&self.attachment_ids}
    /// Number of attachment ancestors this state was built on (0: placed without a chain).
    pub(crate) fn attachment_len(&self)->usize{self.attachment_ids.len()}
    /// The bones this set's chains move (sorted): renderers of one prefab carry the same set.
    pub(crate) fn particle_ids(&self)->Vec<i64>{
        let mut ids:Vec<i64>=self.chains.iter().flat_map(|chain|chain.particles.iter().filter_map(|p|p.id)).collect();
        ids.sort_unstable();ids.dedup();ids
    }
    /// The local position and rotation a step left on these bones.
    pub(crate) fn particle_locals(&self,ids:&[i64])->Option<Vec<([f32;3],[f32;4])>>{
        ids.iter().map(|id|self.skel.tfs.get(id).map(|tf|(tf.pos,tf.rot))).collect()
    }
    /// Instead of a step: another set's solved locals for the same bones (a step writes only these), then the worlds.
    pub(crate) fn take_particle_locals(&mut self,ids:&[i64],locals:&[([f32;3],[f32;4])]){
        for (id,(pos,rot)) in ids.iter().zip(locals) {if let Some(tf)=self.skel.tfs.get_mut(id){tf.pos=*pos;tf.rot=*rot;}}
        self.skel.rebuild_worlds();
    }
    pub(crate) fn set_attachment_chain_for_step(&mut self,chain:&[Tf],step_follows:bool)->bool {
        if chain.len()!=self.attachment_ids.len()||chain.is_empty(){return false;}
        let mut parent=Some(self.parent_id);
        for (id,tf) in self.attachment_ids.iter().zip(chain) {
            if let Some(target)=self.skel.tfs.get_mut(id) {
                target.name.clone_from(&tf.name);target.parent=parent;
                target.sibling_index=tf.sibling_index;target.pos=tf.pos;
                target.rot=tf.rot;target.scale=tf.scale;
            } else {
                let mut tf=tf.clone();tf.parent=parent;self.skel.tfs.insert(*id,tf);
            }
            parent=Some(*id);
        }
        if !step_follows{self.skel.rebuild_worlds();}true
    }

    pub(crate) fn new(set: &DBoneSet, source: &Skeleton, placement: &Mat4) -> Option<Self> {
        let mut state = Self::new_unsettled(set, source, placement)?;
        for _ in 0..240 { state.step(1.0 / 60.0); }
        Some(state)
    }

    pub(crate) fn new_unsettled(set: &DBoneSet, source: &Skeleton, placement: &Mat4) -> Option<Self> {
        if set.is_empty() { return None; }
        let mut skel = source.clone();
        let mut resolved=set.clone();
        let mut shared_component_tfs=implant::rig::instantiate(&mut resolved,&mut skel);
        let set=&resolved;
        let parent_id = (i64::MIN..).find(|id| !skel.tfs.contains_key(id))?;
        let parent_tf = attached_parent_tf(placement)?;
        let root_ids: Vec<i64> = skel.tfs.iter()
            .filter(|(_, tf)| tf.parent.is_none() || tf.parent == Some(0))
            .map(|(id, _)| *id).collect();
        skel.tfs.insert(parent_id, parent_tf);
        for id in root_ids {
            skel.tfs.get_mut(&id).unwrap().parent = Some(parent_id);
        }
        skel.rebuild_worlds();
        let mut chains = Vec::new();
        for (id,tf) in &mut shared_component_tfs { *tf=skel.tfs[id].clone(); }
        for db in &set.bones {
            if !skel.tfs.contains_key(&db.root) { continue; }
            let mut component = db.component_transform.filter(|id| skel.tfs.contains_key(id))
                .unwrap_or(db.root);
            let mut ancestor = parent_id;
            for tf in &db.component_ancestry {
                let id = (i64::MIN..).find(|id| !skel.tfs.contains_key(id))?;
                let mut tf = tf.clone(); tf.parent = Some(ancestor);
                skel.tfs.insert(id, tf.clone()); shared_component_tfs.insert(id, tf);
                ancestor = id; component = id;
            }
            let mut particles = Vec::new();
            let mut cache = HashMap::new();
            if db.ver01 {
                particles = gather_ver01_particles(&skel.tfs, db);
            } else {
                gather_dparticles(&skel.tfs, Some(db.root), -1, 0.0, db, component,
                    &mut particles, &mut cache);
            }
            if particles.len() < 2 { continue; }
            initialize_dparticle_parameters(db, &mut particles);
            let component_pos = parts_pos(&skel.tfs, component);
            let colliders = db.collider_refs.iter().filter_map(|id| set.colliders.get(id))
                .filter(|c| skel.tfs.contains_key(&c.transform)).cloned().collect();
            let mut implanted_colliders:Vec<_> = db.implanted_collider_refs.iter().filter_map(|id|set.colliders.get(id))
                .filter(|c|skel.tfs.contains_key(&c.transform)).cloned().collect();
            for (collider,ancestry) in &db.implanted_collider_ancestry {
                let mut ancestor=parent_id;
                for tf in ancestry {
                    let id=(i64::MIN..).find(|id|!skel.tfs.contains_key(id))?;
                    let mut tf=tf.clone(); tf.parent=Some(ancestor);
                    skel.tfs.insert(id,tf.clone()); shared_component_tfs.insert(id,tf); ancestor=id;
                }
                let mut collider=collider.clone(); collider.transform=ancestor; implanted_colliders.push(collider);
            }
            chains.push(AttachedChain { db: db.clone(), authored_freeze_axis: db.freeze_axis, particles, component,
                local_gravity: root_local_gravity_rotation(world_rot_of(db.root, &skel.tfs), db.gravity),
                prev_component_pos: component_pos, time: 0.0, colliders, external_colliders: Vec::new(), implanted_colliders,implanted_collider_offset:0,px_key:crate::px::PxKey::new(),body_radius:Vec::new(),physics:None });
        }
        if chains.is_empty() { return None; }
        Some(Self { skel, parent_id, exact_placement: *placement, chains, shared_component_tfs,body_space:!set.rigs.is_empty(),attachment_ids:Vec::new(),world_cache:HashMap::new(),rotation_cache:HashMap::new(),lossy_rotation_cache:HashMap::new(),child_counts:HashMap::new(),child_ids:HashMap::new(),child_topology:Vec::new(),px_batch:crate::px::PxKey::new(),skin_chains:false })
    }

    pub(crate) fn configure_character_colliders(&mut self, body: &Skeleton, native: &BodyColliderSet, flags: game_colliders::Flags) {
        let frame=CharacterColliderFrame::new(body,native,flags);
        self.configure_prepared_character_colliders(&frame);
    }

    /// Shared clothes runtime also carries replacement-body DynamicBones
    /// (`retarget_body`: no clothes component name, ancestry, or implant
    /// overlay). LoadCharaFbxData assigns every character collider, plus the
    /// KKS skirt/floor set, only to copied clothing. Giving that set to the
    /// body's own chains shoves `cf_J_Vagina_*` several centimetres in a
    /// static T-pose. Those chains keep their authored `m_Colliders` only.
    /// Clothing copies and accessory callers are unchanged.
    pub(crate) fn configure_shared_clothes_colliders(&mut self, body: &Skeleton, native: &BodyColliderSet, flags: game_colliders::Flags) {
        let frame=CharacterColliderFrame::new(body,native,flags);
        let flags=frame.flags;
        let mut worlds=HashMap::new();
        for chain in &mut self.chains {
            if chain.db.ver01 {continue;}
            let body_component=chain.db.component_name.is_empty()
                && chain.db.component_ancestry.is_empty()
                && chain.db.overlay_bindings.is_none();
            if body_component {
                chain.colliders.clear();
                chain.implanted_collider_offset=0;
                chain.external_colliders=chain.implanted_colliders.iter().cloned().map(|c|collider_pose_cached(c,&self.skel,&mut worlds)).collect();
                chain.db.freeze_axis=chain.authored_freeze_axis;
                continue;
            }
            chain.colliders.clear();
            let skirt=chain.db.component_name=="ct_clothesBot";
            chain.external_colliders.clone_from(&frame.native);
            chain.implanted_collider_offset=chain.external_colliders.len();
            chain.external_colliders.extend(chain.implanted_colliders.iter().cloned().map(|c|collider_pose_cached(c,&self.skel,&mut worlds)));
            chain.external_colliders.extend(frame.ordinary[usize::from(skirt)].iter().cloned());
            chain.db.freeze_axis=if flags.installed&&skirt
                &&chain.db.source_root_name.as_deref().or_else(||self.skel.tfs.get(&chain.db.root).map(|tf|tf.name.as_str()))
                    .is_some_and(|name|name.split('_').nth(3).and_then(|s|s.parse::<i32>().ok())==Some(0)) {
                usize::from(flags.skirt)
            }else{chain.authored_freeze_axis};
        }
    }

    pub(crate) fn configure_prepared_character_colliders(&mut self,frame:&CharacterColliderFrame) {
        let flags=frame.flags;
        let mut worlds=HashMap::new();
        for chain in &mut self.chains {
            if chain.db.ver01 {continue;}
            chain.colliders.clear();
            let skirt=chain.db.component_name=="ct_clothesBot";
            chain.external_colliders.clone_from(&frame.native);
            chain.implanted_collider_offset=chain.external_colliders.len();
            chain.external_colliders.extend(chain.implanted_colliders.iter().cloned().map(|c|collider_pose_cached(c,&self.skel,&mut worlds)));
            chain.external_colliders.extend(frame.ordinary[usize::from(skirt)].iter().cloned());
            chain.db.freeze_axis=if flags.installed&&skirt
                &&chain.db.source_root_name.as_deref().or_else(||self.skel.tfs.get(&chain.db.root).map(|tf|tf.name.as_str()))
                    .is_some_and(|name|name.split('_').nth(3).and_then(|s|s.parse::<i32>().ok())==Some(0)) {
                usize::from(flags.skirt)
            }else{chain.authored_freeze_axis};
        }
    }

    #[cfg(test)]
    fn configure_character_colliders_reference(&mut self, body: &Skeleton, native: &BodyColliderSet, flags: game_colliders::Flags) {
        let native:Vec<_>=native.colliders.iter().filter_map(|(name,c)|{
            let mut c=c.clone();c.transform=body.tfs.iter().find(|(_,tf)|tf.name==*name).map(|(id,_)|*id)?;Some(c)
        }).collect();
        for chain in &mut self.chains {
            if chain.db.ver01 { continue; } // ChaControl and KKS_Colliders target ordinary DynamicBone only.
            chain.colliders.clear(); // LoadCharaFbxData clears authored ordinary collider refs.
            let skirt = chain.db.component_name == "ct_clothesBot";
            chain.external_colliders = native.iter().cloned().map(|c| {
                let world = world_of(c.transform, &body.tfs, &mut HashMap::new());
                let scale_z=transform_lossy_scale(c.transform,&body.tfs,&world)[2].abs(); (c, world, scale_z)
            }).collect();
            chain.implanted_collider_offset=chain.external_colliders.len();
            chain.external_colliders.extend(chain.implanted_colliders.iter().cloned().map(|c| {
                let world=world_of(c.transform,&self.skel.tfs,&mut HashMap::new());
                let scale_z=transform_lossy_scale(c.transform,&self.skel.tfs,&world)[2].abs();(c,world,scale_z)
            }));
            chain.external_colliders.extend(game_colliders::ordinary_colliders(body, flags, skirt).into_iter().map(|c| {
                let world=world_of(c.transform,&body.tfs,&mut HashMap::new());
                let scale_z=transform_lossy_scale(c.transform,&body.tfs,&world)[2].abs();(c,world,scale_z)
            }));
            chain.db.freeze_axis = if flags.installed && skirt
                && chain.db.source_root_name.as_deref().or_else(||self.skel.tfs.get(&chain.db.root).map(|tf|tf.name.as_str()))
                    .is_some_and(|name| name.split('_').nth(3).and_then(|s| s.parse::<i32>().ok()) == Some(0)) {
                usize::from(flags.skirt)
            } else { chain.authored_freeze_axis };
        }
    }

    /// Keep particle history while replacing the animated, shaped shared rig.
    pub(crate) fn sync_shared_pose(&mut self, animated: &Skeleton) {
        self.sync_shared_pose_impl(animated,true);
    }
    fn sync_shared_pose_impl(&mut self,animated:&Skeleton,reuse:bool) {
        let parent = self.skel.tfs[&self.parent_id].clone();
        if reuse {
            self.skel.tfs.remove(&self.parent_id);
            for id in self.shared_component_tfs.keys(){self.skel.tfs.remove(id);}
            // Existing restore_from verifies the complete source inventory
            // before reusing allocations; added/removed transforms still clone.
            self.skel.restore_from(animated);
        }else{self.skel=animated.clone();}
        self.skel.tfs.insert(self.parent_id, parent);
        self.skel.tfs.extend(self.shared_component_tfs.clone());
        // The animated bones by name (the first of a duplicated name, as a search finds it), built only when a bridge needs one.
        let mut live_by_name:Option<HashMap<&str,&Tf>>=None;
        for tf in self.skel.tfs.values_mut() {
            if let Some(name)=tf.name.strip_prefix("__ok_body_bridge/") {
                let by_name=live_by_name.get_or_insert_with(||{let mut m=HashMap::with_capacity(animated.tfs.len());for live in animated.tfs.values(){m.entry(live.name.as_str()).or_insert(live);}m});
                if let Some(live)=by_name.get(name) {
                    tf.pos=live.pos; tf.rot=live.rot; tf.scale=live.scale;
                }
            }
        }
        for (id, tf) in &mut self.skel.tfs {
            if *id != self.parent_id && (tf.parent.is_none() || tf.parent == Some(0)) { tf.parent = Some(self.parent_id); }
        }
    }

    /// Diagnostic: one line per chain (component, body-component flag, particle bones).
    pub(crate) fn describe_chains(&self) -> String {
        let name = |id: Option<i64>| id.and_then(|id| self.skel.tfs.get(&id)).map(|tf| tf.name.clone()).unwrap_or_else(|| "-".into());
        self.chains.iter().map(|chain| {
            let body = chain.db.component_name.is_empty() && chain.db.component_ancestry.is_empty() && chain.db.overlay_bindings.is_none();
            let inside = chain.colliders.iter().filter(|c| c.inside).count() + chain.external_colliders.iter().filter(|c| c.0.inside).count();
            format!("physics={:?} component={:?} body={body} ver01={} gravity={:?} colliders={} inside={inside} freeze={} bones={} radius={} mesh={} e={} s={}\n", chain.physics, chain.db.component_name, chain.db.ver01, chain.db.gravity,
                chain.colliders.len() + chain.external_colliders.len(), chain.db.freeze_axis,
                chain.particles.iter().map(|p| name(p.id)).collect::<Vec<_>>().join(","),
                chain.particles.iter().map(|p| format!("{:.3}", p.radius)).collect::<Vec<_>>().join(","),
                chain.body_radius.iter().map(|r| format!("{r:.3}")).collect::<Vec<_>>().join(","),
                chain.particles.iter().map(|p| format!("{:.2}", p.elasticity)).collect::<Vec<_>>().join(","),
                chain.particles.iter().map(|p| format!("{:.2}", p.stiffness)).collect::<Vec<_>>().join(","))
        }).collect()
    }

    /// Measures how thick the mesh is around each simulated bone (the median distance of the vertices skinned mostly to a
    /// bone from the segment to its child particle), in world units. Authored DynamicBone radii are often 0 (a dragon
    /// tail of several centimetres collides as a line); the host meets other characters' bodies at this thickness.
    pub(crate) fn set_mesh_radii(&mut self, mesh: &crate::unity::ExtractedMesh) {
        let vertices = if mesh.rest_positions.len() == mesh.weights.len() { &mesh.rest_positions } else { &mesh.positions };
        if vertices.len() != mesh.weights.len() || mesh.bone_idx.len() != mesh.weights.len() || mesh.bind_poses.len() != mesh.bone_names.len() { return; }
        // Vertices by their dominant bone (mesh bone index).
        let mut by_bone: HashMap<usize, Vec<[f32; 3]>> = HashMap::new();
        for ((v, w), b) in vertices.iter().zip(&mesh.weights).zip(&mesh.bone_idx) {
            let k = (0..4).max_by(|&x, &y| w[x].total_cmp(&w[y])).unwrap_or(0);
            if w[k] > 0.0 { by_bone.entry(b[k] as usize).or_default().push(*v); }
        }
        let index_of: HashMap<&str, usize> = mesh.bone_names.iter().enumerate().map(|(i, n)| (n.as_str(), i)).collect();
        // Bone origin in mesh space: the inverse bind pose's translation.
        let origin = |b: usize| pos_of(&crate::pose::mat_inv_pub(&mesh.bind_poses[b]));
        let mut cache = HashMap::new();
        for chain in &mut self.chains {
            let mut radii = vec![0.0f32; chain.particles.len()];
            for i in 0..chain.particles.len() {
                let parent = chain.particles[i].parent;
                if parent < 0 { continue; }
                let (Some(pid), Some(id)) = (chain.particles[parent as usize].id, chain.particles[i].id) else {
                    radii[i] = radii[parent as usize]; // end particle: the last bone's thickness
                    continue;
                };
                let (Some(pname), Some(name)) = (self.skel.tfs.get(&pid).map(|t| t.name.as_str()), self.skel.tfs.get(&id).map(|t| t.name.as_str())) else { continue };
                let (Some(&pb), Some(&b)) = (index_of.get(pname), index_of.get(name)) else { continue };
                let Some(around) = by_bone.get(&pb) else { continue };
                let (a, c) = (origin(pb), origin(b));
                let ac = vsub(c, a);
                let l2 = vdot(ac, ac);
                let mut d: Vec<f32> = around.iter().map(|v| {
                    let t = if l2 > 1e-12 { (vdot(vsub(*v, a), ac) / l2).clamp(0.0, 1.0) } else { 0.0 };
                    vlen(vsub(*v, vadd(a, vscale(ac, t))))
                }).collect();
                if d.is_empty() { continue; }
                d.sort_by(|x, y| x.total_cmp(y));
                // Mesh space -> world: the scale of the parent bone's skin matrix at rest.
                let skin = crate::pose::mat_mul_pub(&world_of(pid, &self.skel.tfs, &mut cache), &mesh.bind_poses[pb]);
                let scale = vlen([skin[0][0], skin[1][0], skin[2][0]]);
                radii[i] = (d[d.len() / 2] * scale).min(0.15);
            }
            chain.body_radius = radii;
        }
    }

    /// The mesh thickness per simulated bone name (end particles: "<parent>/end"), for `unify_mesh_radii`.
    pub(crate) fn mesh_radii(&self) -> Vec<(String, f32)> {
        let mut out = Vec::new();
        for chain in &self.chains {
            for (i, r) in chain.body_radius.iter().enumerate() {
                if let Some(name) = self.particle_name(chain, i) { out.push((name, *r)); }
            }
        }
        out
    }
    /// Gives every particle the thickness `radii` holds for its bone (the largest over all renderers of the accessory).
    pub(crate) fn apply_mesh_radii(&mut self, radii: &HashMap<String, f32>) {
        for c in 0..self.chains.len() {
            let n = self.chains[c].particles.len();
            if self.chains[c].body_radius.len() != n { self.chains[c].body_radius = vec![0.0; n]; }
            for i in 0..n {
                if let Some(r) = self.particle_name(&self.chains[c], i).and_then(|name| radii.get(&name)) { self.chains[c].body_radius[i] = *r; }
            }
        }
    }
    fn particle_name(&self, chain: &AttachedChain, i: usize) -> Option<String> {
        let name = |id: i64| self.skel.tfs.get(&id).map(|tf| tf.name.clone());
        let p = chain.particles.get(i)?;
        match p.id {
            Some(id) => name(id),
            None => chain.particles.get(usize::try_from(p.parent).ok()?)?.id.and_then(name).map(|n| n + "/end"),
        }
    }

    /// Marks this set as the character's clothes set (see `skin_chains`).
    pub(crate) fn mark_skin_chains(&mut self) { self.skin_chains = true; }

    /// Classifies every chain of an accessory (attached at `attach`, mesh `mesh`): hair, tail or ordinary accessory
    /// (`PhysicsComponent::of_accessory_chain`). One accessory can hold both, e.g. a costume with hair strands and a tail.
    pub(crate) fn classify_chains(&mut self, attach: &str, mesh: &str) {
        for chain in &mut self.chains {
            let bones: Vec<&str> = chain.particles.iter().filter_map(|p| p.id.and_then(|id| self.skel.tfs.get(&id)).map(|tf| tf.name.as_str())).collect();
            chain.physics = Some(crate::px::PhysicsComponent::of_accessory_chain(attach, mesh, &bones));
        }
    }
    /// Component of each chain (diagnostics, tests).
    pub(crate) fn chain_components(&self) -> Vec<Option<crate::px::PhysicsComponent>> { self.chains.iter().map(|c| c.physics).collect() }

    pub(crate) fn copy_simulated_pose(&self, target: &mut Skeleton) {
        self.copy_simulated_pose_filtered(target, false);
    }

    /// `hold_body_components` keeps replacement-body chains (empty clothes
    /// name, no ancestry, no implant overlay) on the pose just synced in.
    /// A static T-pose has no elapsed motion; publishing the warmed particles
    /// still tucks `cf_J_Vagina_*` on some rigs. Clothing copies still publish
    /// their settle. A later frame with dt>0 copies every chain again.
    pub(crate) fn copy_simulated_pose_filtered(&self, target: &mut Skeleton, hold_body_components: bool) {
        for chain in &self.chains {
            if hold_body_components && chain.db.component_name.is_empty()
                && chain.db.component_ancestry.is_empty() && chain.db.overlay_bindings.is_none() {
                continue;
            }
            for particle in &chain.particles {
                if let Some(id) = particle.id {
                    if let (Some(a), Some(b)) = (self.skel.tfs.get(&id), target.tfs.get_mut(&id)) {
                        b.pos = a.pos; b.rot = a.rot;
                    }
                }
            }
        }
        target.rebuild_worlds();
    }

    pub(crate) fn set_placement(&mut self, placement: &Mat4) -> bool {
        self.set_placement_for_step(placement,false)
    }
    pub(crate) fn set_placement_for_step(&mut self, placement:&Mat4,step_follows:bool)->bool {
        if !self.attachment_ids.is_empty(){return false;}
        let Some(tf) = attached_parent_tf(placement) else { return false; };
        self.exact_placement = *placement;
        self.skel.tfs.insert(self.parent_id, tf);
        if !step_follows{self.skel.rebuild_worlds();}
        true
    }

    /// A Transform cannot store shear caused by non-uniform ancestors. Apply
    /// the full authored socket matrix after local DynamicBone simulation.
    pub(crate) fn placement_correction(&self) -> Mat4 {
        let Some(parent) = self.skel.tfs.get(&self.parent_id) else { return self.exact_placement; };
        let approximated = trs(parent.pos, parent.rot, parent.scale);
        crate::pose::mat_mul_pub(&self.exact_placement,
            &crate::pose::mat_inv_pub(&approximated))
    }

    pub(crate) fn step(&mut self, dt: f32) {
        self.step_impl(dt, true);
    }

    pub(crate) fn step_on_animated_pose(&mut self, dt: f32) {
        self.step_impl(dt, false);
    }

    fn step_impl(&mut self, dt: f32, restore: bool) {
        self.step_impl_with_cache(dt,restore,true);
    }
    fn step_impl_with_cache(&mut self, dt: f32, restore: bool,reuse:bool) {
        if !dt.is_finite() || dt < 0.0 { return; }
        // Unity runs Update on every DynamicBone component before any
        // LateUpdate. Restoring one chain immediately before simulating it
        // lets a later overlapping chain erase the earlier chain's result.
        if restore { for chain in &self.chains {
            for particle in &chain.particles {
                if let Some(id) = particle.id {
                    if let Some(tf) = self.skel.tfs.get_mut(&id) {
                        tf.pos = particle.init_local_pos;
                        tf.rot = particle.init_local_rot;
                    }
                }
            }
        }}
        let mut cache=std::mem::take(&mut self.world_cache);
        // Animation/restoration changes arbitrary transforms before this step.
        // Within the step only Apply writes transforms, and both setters
        // invalidate the written node and all cached dependent descendants.
        // Retain unchanged ancestors through particle phases and later chains.
        cache.clear();
        let mut rotations=std::mem::take(&mut self.rotation_cache);
        // Rotation writes invalidate their descendants across overlapping chains.
        // Position writes leave this rotation-only cache valid. Start every
        // solver step empty, after animation/restoration changed the local pose.
        rotations.clear();
        let mut lossy_rotations=std::mem::take(&mut self.lossy_rotation_cache);
        lossy_rotations.clear();
        // Transform parent links are immutable during this solver step. Count
        // once instead of scanning and sorting the complete rig per particle.
        // Validate IDs, iteration order and every parent each step. Local TRS
        // changes do not rebuild immutable child lists; live topology changes do.
        let topology_trace=crate::native_profile::Scope::new(18);
        if reuse && (self.child_topology.len()!=self.skel.tfs.len() || !self.child_topology.iter().zip(&self.skel.tfs).all(|((id,parent),(actual,tf))|id==actual&&*parent==tf.parent)){
            self.child_counts.clear();for children in self.child_ids.values_mut(){children.clear();}
            self.child_topology.clear();
            for(id,tf)in &self.skel.tfs{
                self.child_topology.push((*id,tf.parent));
                if let Some(parent)=tf.parent{*self.child_counts.entry(parent).or_insert(0)+=1;if parent!=0&&parent!=*id{self.child_ids.entry(parent).or_default().push(*id);}}
            }
        }
        drop(topology_trace);
        if crate::px::batch_available() && crate::px::enabled() {
            // PhysX: every chain of the set is prepared from the same pose, solved in one batch (one PhysX scene,
            // one simulate per substep), then applied in the original order.
            struct Plan { object_move: [f32; 3], object_scale: f32, gravity: [f32; 3], num: i32, input: Option<crate::px::ChainInput> }
            let mut plans: Vec<Plan> = Vec::with_capacity(self.chains.len());
            for chain in &mut self.chains {
                let db = &chain.db;
                // The independent oracle recreates the original map per chain.
                // Reuse keeps only matrices surviving exact write invalidation.
                if !reuse {cache=HashMap::new();}
                // The previous component may have rotated an actual body/implant
                // ancestor. Unity reads collider transforms during each LateUpdate.
                let collider_trace=crate::native_profile::Scope::new(19);
                for (index,(collider,world,scale_z)) in chain.external_colliders.iter_mut().enumerate() {
                    let implanted=index>=chain.implanted_collider_offset
                        && index<chain.implanted_collider_offset+chain.implanted_colliders.len();
                    if (self.body_space || implanted) && self.skel.tfs.contains_key(&collider.transform) {
                        *world=world_of(collider.transform,&self.skel.tfs,&mut cache);
                        *scale_z=if reuse{scoped_lossy_scale(collider.transform,&self.skel.tfs,world,&mut lossy_rotations)}else{transform_lossy_scale(collider.transform,&self.skel.tfs,world)}[2].abs();
                    }
                }
                drop(collider_trace);
                let forces_trace=crate::native_profile::Scope::new(20);
                let component_world = world_of(chain.component, &self.skel.tfs, &mut cache);
                let component_pos = pos_of(&component_world);
                let object_move = vsub(component_pos, chain.prev_component_pos);
                chain.prev_component_pos = component_pos;
                let object_scale = if reuse{scoped_lossy_scale(chain.component,&self.skel.tfs,&component_world,&mut lossy_rotations)}else{transform_lossy_scale(chain.component,&self.skel.tfs,&component_world)}[0].abs().max(1e-6);
                let normalized = vnorm(db.gravity);
                let root_rotation=if reuse {scoped_world_rot(db.root,&self.skel.tfs,&mut rotations)}else{world_rot_of(db.root,&self.skel.tfs)};
                let root_direction = mul_dir(&trs([0.0; 3],
                    root_rotation, [1.0; 3]), chain.local_gravity);
                let subtract = vscale(normalized,
                    vdot(root_direction, normalized).max(0.0));
                let source_gravity = if db.ver01 { db.gravity } else { vsub(db.gravity, subtract) };
                let gravity = vscale(vadd(source_gravity, db.force), object_scale);
                let mut num = 1i32;
                if db.update_rate > 0.0 {
                    let step = 1.0 / db.update_rate;
                    chain.time += dt;
                    num = 0;
                    while chain.time >= step {
                        chain.time -= step;
                        num += 1;
                        if num >= 3 { chain.time = 0.0; break; }
                    }
                }
                drop(forces_trace);
                let mut input = px_input_ordinary(&self.skel, &chain.particles, &mut cache, &chain.colliders, &chain.external_colliders,
                    &chain.px_key, object_scale, gravity, object_move, db.update_rate, dt, db.ver01, db.freeze_axis);
                // Replacement-body chains (empty clothes name, no ancestry, no implant overlay) move skin no garment follows.
                let body_component = self.skin_chains && db.component_name.is_empty() && db.component_ancestry.is_empty() && db.overlay_bindings.is_none();
                if let (true, Some(root)) = (body_component, input.as_mut().and_then(|input| input.particles.first_mut())) {
                    root.flags |= crate::px::FLAG_BODY | crate::px::FLAG_SKIN;
                }
                if let Some(input) = input.as_mut() {
                    for (p, r) in input.particles.iter_mut().zip(&chain.body_radius) { p.body_radius = *r; }
                    // A classified chain asks for its own solver (the card's choice for the part, else its component default).
                    if let (Some(component), Some(_)) = (chain.physics, input.particles.first()) {
                        let solver = crate::px::card_solver().unwrap_or(component.default_solver());
                        input.particles[0].flags = (input.particles[0].flags & !crate::px::FLAG_SOLVER_MASK) | crate::px::solver_flags(solver);
                        if component == crate::px::PhysicsComponent::Tail { input.particles[0].flags |= crate::px::FLAG_TAIL; }
                    }
                }
                plans.push(Plan { object_move, object_scale, gravity, num, input });
            }
            let solved = {
                // Chains PhysX cannot express (a single particle, dt 0) keep the built-in solver; the rest is one batch.
                let mut inputs: Vec<&mut crate::px::ChainInput> = plans.iter_mut().filter_map(|plan| plan.input.as_mut()).collect();
                !inputs.is_empty() && crate::px::solve_batch(&self.px_batch, &mut inputs)
            };
            for (chain, plan) in self.chains.iter_mut().zip(plans) {
                let db = &chain.db;
                let Plan { input, .. } = plan;
                // PhysX owns every chain while it is on: no built-in fallback. A chain it cannot express (a single
                // particle, no time step) keeps its particles where they are.
                if let (true, Some(input)) = (solved, input.as_ref()) {
                    for (p, out) in chain.particles.iter_mut().zip(&input.particles) { p.pos = out.pos; p.prev = out.prev; }
                }
                if !reuse {cache.clear();}
                let apply_trace=crate::native_profile::Scope::new(22);
                apply_particles_scoped(&mut self.skel, &mut chain.particles, &mut cache, chain.component, db.ver01,
                    if reuse {Some(&self.child_counts)}else{None},if reuse {Some(&self.child_ids)}else{None},
                    if reuse {Some(&mut rotations)}else{None});
                drop(apply_trace);
                // Apply only writes particle transforms. Retain all other ancestor
                // orientations for the next chain; invalidate touched subtrees even
                // when this particular particle only changed its position.
                if reuse {for particle in &chain.particles {if let Some(id)=particle.id{invalidate_descendants(&mut lossy_rotations,id,&self.child_ids);}}}
            }
        } else {
            let audit = crate::px::audit_enabled();
            let mut audits: Vec<crate::px::ChainInput> = Vec::new();
            for chain in &mut self.chains {
                let db = &chain.db;
                // The independent oracle recreates the original map per chain.
                // Reuse keeps only matrices surviving exact write invalidation.
                if !reuse {cache=HashMap::new();}
                // The previous component may have rotated an actual body/implant
                // ancestor. Unity reads collider transforms during each LateUpdate.
                let collider_trace=crate::native_profile::Scope::new(19);
                for (index,(collider,world,scale_z)) in chain.external_colliders.iter_mut().enumerate() {
                    let implanted=index>=chain.implanted_collider_offset
                        && index<chain.implanted_collider_offset+chain.implanted_colliders.len();
                    if (self.body_space || implanted) && self.skel.tfs.contains_key(&collider.transform) {
                        *world=world_of(collider.transform,&self.skel.tfs,&mut cache);
                        *scale_z=if reuse{scoped_lossy_scale(collider.transform,&self.skel.tfs,world,&mut lossy_rotations)}else{transform_lossy_scale(collider.transform,&self.skel.tfs,world)}[2].abs();
                    }
                }
                drop(collider_trace);
                let forces_trace=crate::native_profile::Scope::new(20);
                let component_world = world_of(chain.component, &self.skel.tfs, &mut cache);
                let component_pos = pos_of(&component_world);
                let object_move = vsub(component_pos, chain.prev_component_pos);
                chain.prev_component_pos = component_pos;
                let object_scale = if reuse{scoped_lossy_scale(chain.component,&self.skel.tfs,&component_world,&mut lossy_rotations)}else{transform_lossy_scale(chain.component,&self.skel.tfs,&component_world)}[0].abs().max(1e-6);
                let normalized = vnorm(db.gravity);
                let root_rotation=if reuse {scoped_world_rot(db.root,&self.skel.tfs,&mut rotations)}else{world_rot_of(db.root,&self.skel.tfs)};
                let root_direction = mul_dir(&trs([0.0; 3],
                    root_rotation, [1.0; 3]), chain.local_gravity);
                let subtract = vscale(normalized,
                    vdot(root_direction, normalized).max(0.0));
                let source_gravity = if db.ver01 { db.gravity } else { vsub(db.gravity, subtract) };
                let gravity = vscale(vadd(source_gravity, db.force), object_scale);
                let mut num = 1i32;
                if db.update_rate > 0.0 {
                    let step = 1.0 / db.update_rate;
                    chain.time += dt;
                    num = 0;
                    while chain.time >= step {
                        chain.time -= step;
                        num += 1;
                        if num >= 3 { chain.time = 0.0; break; }
                    }
                }
                drop(forces_trace);
                let particles_trace=crate::native_profile::Scope::new(21);
                let px_done = crate::px::solver().is_some() && px_solve_ordinary(&self.skel, &mut chain.particles, &mut cache,
                    &chain.colliders, &chain.external_colliders, &chain.px_key, object_scale, gravity, object_move,
                    db.update_rate, dt, db.ver01, db.freeze_axis);
                // Weight/hardness of the one-to-one interaction (identity elsewhere), as the host solvers apply them.
                let tuning = crate::px::verlet_tuning();
                let gravity = vadd(gravity, tuning.gravity_step(if db.update_rate > 0.0 { 1.0 / db.update_rate } else { dt }));
                if px_done {
                } else if num > 0 {
                    for substep in 0..num {
                        let move_this_step = if substep == 0 { object_move } else { [0.0; 3] };
                        for particle in &mut chain.particles {
                            if particle.parent < 0 {
                                if let Some(id) = particle.id {
                                    particle.prev = particle.pos;
                                    particle.pos = if reuse{cached_parts_pos(&self.skel.tfs,id,&mut cache)}else{parts_pos(&self.skel.tfs,id)};
                                }
                                continue;
                            }
                            let velocity = vsub(particle.pos, particle.prev);
                            let inertial_move = vscale(move_this_step, particle.inert);
                            particle.prev = vadd(particle.pos, inertial_move);
                            particle.pos = vadd(vadd(particle.pos,
                                vscale(velocity, 1.0 - particle.damping * tuning.damp)),
                                vadd(gravity, inertial_move));
                        }
                        if !reuse {cache.clear();}
                        update_particles2(&mut self.skel, &mut chain.particles,
                            &mut cache, &chain.colliders, &chain.external_colliders, object_scale, db.freeze_axis, db.ver01, tuning);
                    }
                } else if !db.suppress_skip_update {
                    if !reuse {cache.clear();}
                    skip_update_particles(&mut self.skel, &mut chain.particles, object_move,
                        &mut cache, object_scale, db.ver01);
                }
                drop(particles_trace);
                if audit {
                    // Same audit as PhysX: the built-in result, before Apply rewrites the bones.
                    if let Some(input) = px_input_ordinary(&self.skel, &chain.particles, &mut cache, &chain.colliders, &chain.external_colliders,
                        &chain.px_key, object_scale, gravity, [0.0; 3], db.update_rate, dt, db.ver01, db.freeze_axis) { audits.push(input); }
                }
                if !reuse {cache.clear();}
                let apply_trace=crate::native_profile::Scope::new(22);
                apply_particles_scoped(&mut self.skel, &mut chain.particles, &mut cache, chain.component, db.ver01,
                    if reuse {Some(&self.child_counts)}else{None},if reuse {Some(&self.child_ids)}else{None},
                    if reuse {Some(&mut rotations)}else{None});
                drop(apply_trace);
                // Apply only writes particle transforms. Retain all other ancestor
                // orientations for the next chain; invalidate touched subtrees even
                // when this particular particle only changed its position.
                if reuse {for particle in &chain.particles {if let Some(id)=particle.id{invalidate_descendants(&mut lossy_rotations,id,&self.child_ids);}}}
            }
            if !audits.is_empty() { crate::px::audit_batch(&self.px_batch, &mut audits); }
        }
        let worlds_trace=crate::native_profile::Scope::new(23);
        self.skel.rebuild_worlds();
        drop(worlds_trace);
        cache.clear();self.world_cache=cache;
        rotations.clear();self.rotation_cache=rotations;
        lossy_rotations.clear();self.lossy_rotation_cache=lossy_rotations;
    }
}

/// One DynamicBone chain as the GPU runtime rig simulates it (gpu_rig/export.rs, Shaders/GpuRig.usf PhysicsCS): the
/// particles in solver order and what UpdateDynamicBones reads besides the transforms (ids are the set's skeleton's).
#[derive(Clone, Debug)]
pub(crate) struct GpuChainView {
    pub root: i64, pub component: i64, pub ver01: bool, pub suppress_skip: bool,
    pub gravity: [f32; 3], pub force: [f32; 3], pub local_gravity: [f32; 3], pub freeze_axis: usize, pub update_rate: f32,
    /// ApplyParticlesToTransforms mirrors along these world axes of (bit 0..2 set) the component, (bit 4..6) its parent.
    pub mirror: u32,
    pub particles: Vec<GpuParticleView>,
    pub colliders: Vec<GpuColliderView>,
}
#[derive(Clone, Debug)]
pub(crate) struct GpuParticleView {
    pub id: Option<i64>, pub parent: i32,
    pub init_local_pos: [f32; 3], pub init_local_rot: [f32; 4], pub end_offset: [f32; 3], pub local_position: [f32; 3],
    pub damping: f32, pub elasticity: f32, pub stiffness: f32, pub inert: f32, pub radius: f32, pub add_angle_scale: f32,
    /// Apply turns this particle's parent bone toward it (ordinary: the parent has at most one child; Ver01: rotation_calc).
    pub turns_parent: bool,
}
/// `in_body`: the transform is the character's body bone (CharacterColliderFrame), else a bone of the set's skeleton.
#[derive(Clone, Debug)]
pub(crate) struct GpuColliderView {
    pub transform: i64, pub in_body: bool, pub center: [f32; 3], pub radius: f32, pub height: f32, pub direction: usize, pub inside: bool,
}
impl AttachedDynamicSet {
    /// The chains as the GPU rig reads them (configured colliders and freeze axes as the last step used them).
    pub(crate) fn gpu_view(&self) -> Vec<GpuChainView> {
        let mut children: HashMap<i64, usize> = HashMap::new();
        for (id, tf) in &self.skel.tfs { if let Some(p) = tf.parent.filter(|p| *p != 0 && *p != *id) { *children.entry(p).or_insert(0) += 1; } }
        self.chains.iter().map(|chain| {
            let db = &chain.db;
            let component_tf = self.skel.tfs.get(&chain.component);
            let parent_tf = component_tf.and_then(|tf| tf.parent).and_then(|id| self.skel.tfs.get(&id));
            let mut mirror = 0u32;
            for axis in 0..3 {
                if component_tf.is_some_and(|tf| tf.scale[axis] < 0.0) { mirror |= 1 << axis; }
                else if parent_tf.is_some_and(|tf| tf.scale[axis] < 0.0) { mirror |= 16 << axis; }
            }
            let particles = chain.particles.iter().map(|p| {
                let turns_parent = p.parent >= 0 && {
                    let parent = &chain.particles[p.parent as usize];
                    if db.ver01 { parent.rotation_calc } else { parent.id.is_some_and(|id| children.get(&id).copied().unwrap_or(0) <= 1) }
                };
                GpuParticleView { id: p.id, parent: p.parent, init_local_pos: p.init_local_pos, init_local_rot: p.init_local_rot,
                    end_offset: p.end_offset, local_position: p.local_position, damping: p.damping, elasticity: p.elasticity,
                    stiffness: p.stiffness, inert: p.inert, radius: p.radius, add_angle_scale: p.add_angle_scale, turns_parent }
            }).collect();
            let view = |c: &BodyCollider, in_body: bool| GpuColliderView { transform: c.transform, in_body, center: c.center, radius: c.radius,
                height: c.height, direction: c.direction, inside: c.inside };
            let mut colliders: Vec<GpuColliderView> = chain.colliders.iter().map(|c| view(c, false)).collect();
            for (index, (c, _, _)) in chain.external_colliders.iter().enumerate() {
                let implanted = index >= chain.implanted_collider_offset && index < chain.implanted_collider_offset + chain.implanted_colliders.len();
                colliders.push(view(c, !(self.body_space || implanted) || !self.skel.tfs.contains_key(&c.transform)));
            }
            GpuChainView { root: db.root, component: chain.component, ver01: db.ver01, suppress_skip: db.suppress_skip_update,
                gravity: db.gravity, force: db.force, local_gravity: chain.local_gravity, freeze_axis: db.freeze_axis,
                update_rate: db.update_rate, mirror, particles, colliders }
        }).collect()
    }
}

/// IllusionFixes DynamicBonesFix.Hooks patches ordinary and Ver02 skip,
/// but deliberately leaves Ver01's different algorithm alone.
pub(crate) fn dynamic_bones_fix_enabled(game_root: &Path) -> bool {
    ["BepInEx/plugins/IllusionFixes/KKS_Fix_DynamicBones.dll", "BepInEx/plugins/KKS_Fix_DynamicBones.dll"]
        .iter().any(|path| game_root.join(path).ok_is_file())
}

pub(crate) fn dbone_set(serialized: &SerializedFile) -> DBoneSet {
    dbone_set_resolved(serialized,&crate::unity::references::Scripts::local(serialized))
}
pub(crate) fn dbone_set_resolved(serialized: &SerializedFile,scripts:&crate::unity::references::Scripts) -> DBoneSet {
    DBoneSet {
        bones: parse_dbones_resolved(serialized,scripts),
        colliders: parse_colliders_resolved(serialized, None,scripts).into_iter().collect(), rigs:Vec::new(),
    }
}

pub(crate) fn hang_set(set: &DBoneSet, skel: &mut Skeleton) -> usize {
    if std::env::var("OK_DBDIAG").is_ok() {
        for db in set.bones.iter().filter(|db| skel.tfs.contains_key(&db.root)) {
            eprintln!(
                "ok-extract: DBDIAG cached root={} grav={:?} force={:?} damping={} elas={} stiff={} inert={} rate={} end_len={} end_offset={:?} radius={} freeze={} collider_refs={} excl={} notroll={}",
                skel.tfs.get(&db.root).map(|t| t.name.as_str()).unwrap_or("?"),
                db.gravity, db.force, db.damping, db.elasticity, db.stiffness,
                db.inert, db.update_rate, db.end_length, db.end_offset,
                db.radius, db.freeze_axis, db.collider_refs.len(), db.exclusions.len(), db.not_rolls.len(),
            );
        }
    }
    hang_bones_with_colliders(&set.bones, skel, &[], None, Some(&set.colliders))
}

/// Accessory DynamicBone runs after ChangeAccessoryParent and N_move have
/// placed the prefab in character space. Simulate under a temporary parent,
/// then return to prefab space for the existing skin-and-snap export path.
pub(crate) fn hang_set_attached(set: &DBoneSet, skel: &mut Skeleton, placement: &Mat4) -> usize {
    if set.is_empty() { return 0; }
    if std::env::var("OK_NODB").is_ok() { return 0; }
    // All components start from the same prefab pose in Unity. The normal
    // preview bake must use the same concurrent warmup as the persistent
    // runtime state; settling one component for 240 frames before creating
    // the next changes the latter's particle rest positions on shared bones.
    if std::env::var("OK_DBSTST").is_err() && std::env::var("OK_DB_MOVE").is_err() {
        if let Some(state) = AttachedDynamicSet::new(set, skel, placement) {
            let used = state.chains.len();
            let particles: usize = state.chains.iter().map(|chain| chain.particles.len()).sum();
            for (id, tf) in &mut skel.tfs {
                if let Some(baked) = state.skel.tfs.get(id) {
                    tf.pos = baked.pos;
                    tf.rot = baked.rot;
                }
            }
            skel.rebuild_worlds();
            eprintln!("ok-extract: DynamicBone components={used} particles={particles}");
            return used;
        }
        return 0;
    }
    // Preserve the older single-component bake for explicit A/B diagnostics.
    let mut scale = [0.0f32; 3];
    let mut rotation = *placement;
    for axis in 0..3 {
        scale[axis] = vlen([placement[0][axis], placement[1][axis], placement[2][axis]]);
        if scale[axis] < 1e-7 { return 0; }
        for row in 0..3 { rotation[row][axis] /= scale[axis]; }
    }
    // Unity Transform.lossyScale may include shear from ancestors. Keep the
    // column lengths and orientation; this is exact for uniform-scale sockets.
    let parent_id = (i64::MIN..).find(|id| !skel.tfs.contains_key(id)).unwrap();
    let original: Vec<_> = skel.tfs.iter().filter(|(_, tf)| tf.parent.is_none() || tf.parent == Some(0))
        .map(|(id, tf)| (*id, tf.parent)).collect();
    skel.tfs.insert(parent_id, Tf { sibling_index: None,
        name: "__ok_accessory_placement".to_string(), parent: None,
        pos: pos_of(placement), rot: quat_from_mat(&rotation), scale,
    });
    for (id, _) in &original { skel.tfs.get_mut(id).unwrap().parent = Some(parent_id); }
    skel.rebuild_worlds();
    let used = hang_set(set, skel);
    for (id, parent) in original { skel.tfs.get_mut(&id).unwrap().parent = parent; }
    skel.tfs.remove(&parent_id);
    skel.rebuild_worlds();
    used
}

pub(crate) fn hang_set_attached_with_chain(set:&DBoneSet,skel:&mut Skeleton,chain:&[Tf])->usize {
    if set.is_empty()||std::env::var("OK_NODB").is_ok(){return 0;}
    let Some(mut state)=AttachedDynamicSet::new_unsettled_with_chain(set,skel,chain) else {return 0;};
    for _ in 0..240 {state.step(1./60.);}
    for (id,tf) in &mut skel.tfs {if let Some(baked)=state.skel.tfs.get(id){tf.pos=baked.pos;tf.rot=baked.rot;}}
    skel.rebuild_worlds();state.chains.len()
}

pub(crate) fn hang_serialized(serialized: &SerializedFile, skel: &mut Skeleton) -> usize {
    if std::env::var("OK_NODB").is_ok() {
        return 0;
    }
    let set = dbone_set(serialized);
    let bones = &set.bones;
    if std::env::var("OK_DBDIAG").is_ok() {
        let in_tfs = bones.iter().filter(|db| skel.tfs.contains_key(&db.root)).count();
        eprintln!(
            "ok-extract: DBDIAG tfs={} dbones={} matched_roots={in_tfs}",
            skel.tfs.len(),
            bones.len()
        );
        for db in bones.iter().filter(|db| skel.tfs.contains_key(&db.root)) {
            eprintln!(
                "ok-extract: DBDIAG root={} grav={:?} force={:?} damping={} elas={} stiff={} inert={} excl={} notroll={}",
                skel.tfs.get(&db.root).map(|t| t.name.as_str()).unwrap_or("?"),
                db.gravity,
                db.force,
                db.damping,
                db.elasticity,
                db.stiffness,
                db.inert,
                db.exclusions.len(),
                db.not_rolls.len()
            );
        }
    }
    hang_set(&set, skel)
}

fn hang_bones(bones: &[DBone], skel: &mut Skeleton) -> usize {
    hang_bones_with_colliders(bones, skel, &[], None, None)
}

fn hang_bones_with_colliders(bones: &[DBone], skel: &mut Skeleton, colliders: &[BodyCollider], object_scale: Option<f32>, prefab_colliders: Option<&HashMap<i64, BodyCollider>>) -> usize {
    if std::env::var("OK_NODB").is_ok() {
        return 0;
    }
    let mut used = 0usize;
    let mut particles = 0usize;
    // The bundle holds one DynamicBone per prefab (this pack has 271); only the
    // components whose `m_Root` is a transform of the current skeleton belong to
    // this part. Matching by root (not truncating) keeps the accessory's own
    // chain from being dropped.
    let object_move: [f32; 3] = std::env::var("OK_DB_MOVE")
        .ok()
        .and_then(|s| {
            let p: Vec<f32> = s.split(',').filter_map(|x| x.trim().parse().ok()).collect();
            if p.len() == 3 {
                Some([p[0], p[1], p[2]])
            } else {
                None
            }
        })
        .unwrap_or([0.0, 0.0, 0.0]);
    // Faithful `DynamicBone.UpdateDynamicBones` (per-frame particles + apply) is
    // the default; `OK_DBSTST` keeps the old single static settle for A/B.
    let dynamic = std::env::var("OK_DBSTST").is_err();
    for db in bones {
        if !skel.tfs.contains_key(&db.root) {
            continue;
        }
        let authored_colliders: Vec<BodyCollider> = prefab_colliders.map(|map| {
            db.collider_refs.iter().filter_map(|id| map.get(id))
                .filter(|c| skel.tfs.contains_key(&c.transform)).cloned().collect()
        }).unwrap_or_default();
        if std::env::var("OK_DBDIAG").is_ok() && !db.collider_refs.is_empty() {
            eprintln!("ok-extract: DBDIAG active root={} collider_refs={} matched={}",
                skel.tfs.get(&db.root).map(|t| t.name.as_str()).unwrap_or("?"),
                db.collider_refs.len(), authored_colliders.len());
        }
        let active_colliders = if prefab_colliders.is_some() { &authored_colliders } else { colliders };
        let n = if dynamic {
            simulate_dynamic(skel, db, active_colliders, object_scale, object_move, 240, 1.0 / 60.0)
        } else {
            simulate(skel, db, 240)
        };
        if n > 0 {
            used += 1;
            particles += n;
        }
    }
    if used > 0 {
        skel.rebuild_worlds();
        eprintln!("ok-extract: DynamicBone components={used} particles={particles}");
    }
    used
}

fn hang_from_files(files: &[(String, Vec<u8>)], skel: &mut Skeleton) -> usize {
    let mut n = 0usize;
    for (_, fb) in files {
        let Ok(serialized) = parse_serialized_file(fb.clone()) else {
            continue;
        };
        n += hang_serialized(&serialized, skel);
    }
    n
}

pub fn hang_from_bytes(bytes: &[u8], skel: &mut Skeleton) -> usize {
    if let Some(parsed) = crate::unity::parsed_if_cached(bytes) {
        return parsed.with_serialized(|files| files.iter().map(|file|hang_serialized(file,skel)).sum());
    }
    let Ok(bundle) = crate::unity::load_bundle_unbounded(bytes.to_vec()) else {
        return 0;
    };
    let _ = bundle.data_checked();
    let mut files = Vec::new();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else {
            continue;
        };
        files.push((file.name.clone(), fb));
    }
    hang_from_files(&files, skel)
}

/// ChaControl.LoadCharaFbxDataNoAsync(copyDynamicBone: true) replaces a
/// clothing prefab's DynamicBone roots, exclusions, and notRolls with the
/// matching transforms in aaWeightsBody.dictBone. Simulate those components on
/// the character skeleton, which is also the SMR's assigned skinning palette.
pub fn hang_clothes_on_body(bytes: &[u8], body_bytes: &[u8], part: &Skeleton, body: &mut Skeleton) -> usize {
    if std::env::var("OK_NODB").is_ok() {
        return 0;
    }
    // A SQL prefab hit bypasses mesh parsing; load the bundle here if its
    // DynamicBone components have not yet entered the in-process parse cache.
    let parsed = crate::unity::parsed_if_cached(bytes)
        .unwrap_or_else(|| crate::unity::parse_bundle_cached(bytes.to_vec()));
    let body_ids: HashMap<&str, i64> = body.tfs.iter()
        .map(|(id, tf)| (tf.name.as_str(), *id)).collect();
    let remap = |id: i64| -> Option<i64> {
        part.tfs.get(&id).and_then(|tf| body_ids.get(tf.name.as_str()).copied())
    };
    let mut mapped = Vec::new();
    for (_, fb) in &parsed.files {
        let Ok(serialized) = crate::unity::parse_shared_serialized(fb.clone()) else { continue };
        for mut db in parse_dbones(&serialized) {
            // Other prefabs may share the same bundle. Restrict the component
            // to the loaded clothing prefab before mapping names to the body.
            if !part.tfs.contains_key(&db.root) { continue; }
            let Some(root) = remap(db.root) else { continue };
            db.root = root;
            db.exclusions = db.exclusions.into_iter().filter_map(&remap).collect();
            db.not_rolls = db.not_rolls.into_iter().filter_map(&remap).collect();
            mapped.push(db);
        }
    }
    let parsed_body = crate::unity::parsed_if_cached(body_bytes)
        .unwrap_or_else(|| crate::unity::parse_bundle_cached(body_bytes.to_vec()));
    let colliders = parsed_body.skeleton_for_prefab_name("p_cf_body_bone")
        .map(|source| {
            parsed_body.files.iter().filter_map(|(_, fb)| crate::unity::parse_shared_serialized(fb.clone()).ok())
                .flat_map(|s| parse_body_colliders(&s, &source))
                .filter_map(|mut c| {
                    let name = &source.tfs.get(&c.transform)?.name;
                    c.transform = *body_ids.get(name.as_str())?;
                    Some(c)
                }).collect::<Vec<_>>()
        }).unwrap_or_default();
    if std::env::var("OK_DBDIAG").is_ok() {
        eprintln!("ok-extract: cloth DB mapped={} body_colliders={}", mapped.len(), colliders.len());
    }
    hang_bones_with_colliders(&mapped, body, &colliders, Some(1.0), None)
}

pub fn hang_from_path(path: &Path, skel: &mut Skeleton) -> usize {
    let Ok(bundle) = load_bundle(path) else {
        return 0;
    };
    let _ = bundle.data_checked();
    let mut n = 0usize;
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue };
        let Ok(serialized) = parse_serialized_file(fb) else { continue };
        n += hang_serialized(&serialized, skel);
    }
    n
}
