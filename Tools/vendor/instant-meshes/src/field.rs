//! Orientation and position fields with 2-, 4- and 6-fold symmetry.
//!
//! The smoother is the hierarchical Gauss–Seidel iteration from
//! Jakob et al., "Instant Field-Aligned Meshes", SIGGRAPH Asia 2015.
//! Within one graph-coloring phase only an independent set is updated, so
//! those vertices may be written in parallel.

use rayon::prelude::*;

use crate::hierarchy::Hierarchy;
use crate::math::{coordinate_system, modulo, signum, Vec2i, Vec3, INVALID, RCPOVERFLOW};

const SQRT_3_OVER_4: f32 = 0.866025403784439;

pub fn rotate180(q: Vec3) -> Vec3 {
    -q
}
pub fn rotate180_by(q: Vec3, _n: Vec3, amount: i32) -> Vec3 {
    if amount & 1 != 0 { -q } else { q }
}
pub fn rshift180(mut shift: Vec2i, amount: i32) -> Vec2i {
    if amount & 1 != 0 {
        shift = -shift;
    }
    shift
}
pub fn rotate90(q: Vec3, n: Vec3) -> Vec3 {
    n.cross(q)
}
pub fn rotate90_by(q: Vec3, n: Vec3, amount: i32) -> Vec3 {
    let turned = if amount & 1 != 0 { n.cross(q) } else { q };
    turned * if amount < 2 { 1.0 } else { -1.0 }
}
pub fn rshift90(mut shift: Vec2i, amount: i32) -> Vec2i {
    if amount & 1 != 0 {
        shift = Vec2i::new(-shift.y, shift.x);
    }
    if amount >= 2 {
        shift = -shift;
    }
    shift
}
pub fn rotate60(d: Vec3, n: Vec3) -> Vec3 {
    n.cross(d) * SQRT_3_OVER_4 + (d + n * n.dot(d)) * 0.5
}
pub fn rshift60(mut shift: Vec2i, amount: i32) -> Vec2i {
    for _ in 0..amount {
        shift = Vec2i::new(-shift.y, shift.x + shift.y);
    }
    shift
}
pub fn rotate60_by(d: Vec3, n: Vec3, amount: i32) -> Vec3 {
    match amount {
        0 => d,
        1 => rotate60(d, n),
        2 => -rotate60(d, -n),
        3 => -d,
        4 => -rotate60(d, n),
        5 => rotate60(d, -n),
        _ => d,
    }
}

pub fn rotate_vector_into_plane(mut q: Vec3, source_normal: Vec3, target_normal: Vec3) -> Vec3 {
    let cos_theta = source_normal.dot(target_normal);
    if cos_theta < 0.9999 {
        let axis = source_normal.cross(target_normal);
        let denom = axis.dot(axis);
        if denom > RCPOVERFLOW {
            q = q * cos_theta + axis.cross(q) + axis * (axis.dot(q) * (1.0 - cos_theta) / denom);
        }
    }
    q
}

fn middle_point(p0: Vec3, n0: Vec3, p1: Vec3, n1: Vec3) -> Vec3 {
    let n0p0 = n0.dot(p0);
    let n0p1 = n0.dot(p1);
    let n1p0 = n1.dot(p0);
    let n1p1 = n1.dot(p1);
    let n0n1 = n0.dot(n1);
    let denom = 1.0 / (1.0 - n0n1 * n0n1 + 1e-4);
    let lambda_0 = 2.0 * (n0p1 - n0p0 - n0n1 * (n1p0 - n1p1)) * denom;
    let lambda_1 = 2.0 * (n1p0 - n1p1 - n0n1 * (n0p1 - n0p0)) * denom;
    (p0 + p1) * 0.5 - (n0 * lambda_0 + n1 * lambda_1) * 0.25
}

pub fn compat_orientation_extrinsic_2(q0: Vec3, _n0: Vec3, q1: Vec3, _n1: Vec3) -> (Vec3, Vec3) {
    (q0, q1 * signum(q0.dot(q1)))
}
pub fn compat_orientation_extrinsic_4(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    let a = [q0, n0.cross(q0)];
    let b = [q1, n1.cross(q1)];
    let mut best_score = f32::NEG_INFINITY;
    let mut best_a = 0;
    let mut best_b = 0;
    for i in 0..2 {
        for j in 0..2 {
            let score = a[i].dot(b[j]).abs();
            if score > best_score {
                best_a = i;
                best_b = j;
                best_score = score;
            }
        }
    }
    let dp = a[best_a].dot(b[best_b]);
    (a[best_a], b[best_b] * signum(dp))
}
pub fn compat_orientation_extrinsic_6(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    let a = [rotate60(q0, -n0), q0, rotate60(q0, n0)];
    let b = [rotate60(q1, -n1), q1, rotate60(q1, n1)];
    let mut best_score = f32::NEG_INFINITY;
    let mut best_a = 0;
    let mut best_b = 0;
    for i in 0..3 {
        for j in 0..3 {
            let score = a[i].dot(b[j]).abs();
            if score > best_score {
                best_a = i;
                best_b = j;
                best_score = score;
            }
        }
    }
    let dp = a[best_a].dot(b[best_b]);
    (a[best_a], b[best_b] * signum(dp))
}

pub fn compat_orientation_intrinsic_2(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    let q1 = rotate_vector_into_plane(q1, n1, n0);
    (q0, q1 * signum(q1.dot(q0)))
}
pub fn compat_orientation_intrinsic_4(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    let q1 = rotate_vector_into_plane(q1, n1, n0);
    let t1 = n0.cross(q1);
    let dp0 = q1.dot(q0);
    let dp1 = t1.dot(q0);
    if dp0.abs() > dp1.abs() {
        (q0, q1 * signum(dp0))
    } else {
        (q0, t1 * signum(dp1))
    }
}
pub fn compat_orientation_intrinsic_6(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    let q1 = rotate_vector_into_plane(q1, n1, n0);
    let t1 = [rotate60(q1, -n0), q1, rotate60(q1, n0)];
    let dp = [t1[0].dot(q0), t1[1].dot(q0), t1[2].dot(q0)];
    let abs_dp = [dp[0].abs(), dp[1].abs(), dp[2].abs()];
    let k = if abs_dp[0] >= abs_dp[1] && abs_dp[0] >= abs_dp[2] {
        0
    } else if abs_dp[1] >= abs_dp[2] {
        1
    } else {
        2
    };
    (q0, t1[k] * signum(dp[k]))
}

pub fn compat_orientation_extrinsic(rosy: i32, q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    match rosy {
        2 => compat_orientation_extrinsic_2(q0, n0, q1, n1),
        6 => compat_orientation_extrinsic_6(q0, n0, q1, n1),
        _ => compat_orientation_extrinsic_4(q0, n0, q1, n1),
    }
}

fn compat_orientation_extrinsic_index_2(q0: Vec3, _n0: Vec3, q1: Vec3, _n1: Vec3) -> (i32, i32) {
    (0, if q0.dot(q1) < 0.0 { 1 } else { 0 })
}
fn compat_orientation_extrinsic_index_4(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (i32, i32) {
    let a = [q0, n0.cross(q0)];
    let b = [q1, n1.cross(q1)];
    let mut best_score = f32::NEG_INFINITY;
    let mut best_a = 0;
    let mut best_b = 0;
    for i in 0..2 {
        for j in 0..2 {
            let score = a[i].dot(b[j]).abs();
            if score > best_score {
                best_a = i;
                best_b = j;
                best_score = score;
            }
        }
    }
    if a[best_a].dot(b[best_b]) < 0.0 {
        best_b += 2;
    }
    (best_a as i32, best_b as i32)
}
fn compat_orientation_extrinsic_index_6(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (i32, i32) {
    let a = [rotate60(q0, -n0), q0, rotate60(q0, n0)];
    let b = [rotate60(q1, -n1), q1, rotate60(q1, n1)];
    let mut best_score = f32::NEG_INFINITY;
    let mut best_a = 0;
    let mut best_b = 0;
    for i in 0..3 {
        for j in 0..3 {
            let score = a[i].dot(b[j]).abs();
            if score > best_score {
                best_a = i;
                best_b = j;
                best_score = score;
            }
        }
    }
    if a[best_a].dot(b[best_b]) < 0.0 {
        best_b += 3;
    }
    (best_a as i32, best_b as i32)
}
fn compat_orientation_intrinsic_index_2(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (i32, i32) {
    let q1 = rotate_vector_into_plane(q1, n1, n0);
    (0, if q1.dot(q0) > 0.0 { 0 } else { 1 })
}
fn compat_orientation_intrinsic_index_4(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (i32, i32) {
    let q1 = rotate_vector_into_plane(q1, n1, n0);
    let dp0 = q1.dot(q0);
    let dp1 = n0.cross(q1).dot(q0);
    if dp0.abs() > dp1.abs() {
        (0, if dp0 > 0.0 { 0 } else { 2 })
    } else {
        (0, if dp1 > 0.0 { 1 } else { 3 })
    }
}
fn compat_orientation_intrinsic_index_6(q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (i32, i32) {
    let q1 = rotate_vector_into_plane(q1, n1, n0);
    let t1 = [rotate60(q1, -n0), q1, rotate60(q1, n0)];
    let dp = [t1[0].dot(q0), t1[1].dot(q0), t1[2].dot(q0)];
    let abs_dp = [dp[0].abs(), dp[1].abs(), dp[2].abs()];
    if abs_dp[0] >= abs_dp[1] && abs_dp[0] >= abs_dp[2] {
        (0, if dp[0] > 0.0 { 5 } else { 2 })
    } else if abs_dp[1] >= abs_dp[0] && abs_dp[1] >= abs_dp[2] {
        (0, if dp[1] > 0.0 { 0 } else { 3 })
    } else {
        (0, if dp[2] > 0.0 { 1 } else { 4 })
    }
}

fn orientation_index(extrinsic: bool, rosy: i32, q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (i32, i32) {
    match (extrinsic, rosy) {
        (true, 2) => compat_orientation_extrinsic_index_2(q0, n0, q1, n1),
        (true, 6) => compat_orientation_extrinsic_index_6(q0, n0, q1, n1),
        (true, _) => compat_orientation_extrinsic_index_4(q0, n0, q1, n1),
        (false, 2) => compat_orientation_intrinsic_index_2(q0, n0, q1, n1),
        (false, 6) => compat_orientation_intrinsic_index_6(q0, n0, q1, n1),
        (false, _) => compat_orientation_intrinsic_index_4(q0, n0, q1, n1),
    }
}

fn position_floor_4(o: Vec3, q: Vec3, n: Vec3, p: Vec3, scale: f32, inv_scale: f32) -> Vec3 {
    let t = n.cross(q);
    let d = p - o;
    o + q * q.dot(d).mul_add(inv_scale, 0.0).floor() * scale
        + t * t.dot(d).mul_add(inv_scale, 0.0).floor() * scale
}
fn position_floor_index_4(o: Vec3, q: Vec3, n: Vec3, p: Vec3, inv_scale: f32) -> Vec2i {
    let t = n.cross(q);
    let d = p - o;
    Vec2i::new(
        (q.dot(d) * inv_scale).floor() as i32,
        (t.dot(d) * inv_scale).floor() as i32,
    )
}
fn position_round_4(o: Vec3, q: Vec3, n: Vec3, p: Vec3, scale: f32, inv_scale: f32) -> Vec3 {
    let t = n.cross(q);
    let d = p - o;
    o + q * (q.dot(d) * inv_scale).round() * scale + t * (t.dot(d) * inv_scale).round() * scale
}
fn position_round_index_4(o: Vec3, q: Vec3, n: Vec3, p: Vec3, inv_scale: f32) -> Vec2i {
    let t = n.cross(q);
    let d = p - o;
    Vec2i::new(
        (q.dot(d) * inv_scale).round() as i32,
        (t.dot(d) * inv_scale).round() as i32,
    )
}

fn position_round_3(o: Vec3, q: Vec3, n: Vec3, p: Vec3, scale: f32, inv_scale: f32) -> Vec3 {
    let t = rotate60(q, n);
    let d = p - o;
    let dpq = q.dot(d);
    let dpt = t.dot(d);
    let u = ((4.0 * dpq - 2.0 * dpt) * (1.0 / 3.0) * inv_scale).floor();
    let v = ((-2.0 * dpq + 4.0 * dpt) * (1.0 / 3.0) * inv_scale).floor();
    let mut best_cost = f32::INFINITY;
    let mut best_i = 0;
    for i in 0..4 {
        let ot = o + (q * (u + (i & 1) as f32) + t * (v + ((i & 2) >> 1) as f32)) * scale;
        let cost = (ot - p).squared_norm();
        if cost < best_cost {
            best_i = i;
            best_cost = cost;
        }
    }
    o + (q * (u + (best_i & 1) as f32) + t * (v + ((best_i & 2) >> 1) as f32)) * scale
}
fn position_round_index_3(o: Vec3, q: Vec3, n: Vec3, p: Vec3, scale: f32, inv_scale: f32) -> Vec2i {
    let t = rotate60(q, n);
    let d = p - o;
    let dpq = q.dot(d);
    let dpt = t.dot(d);
    let u = ((4.0 * dpq - 2.0 * dpt) * (1.0 / 3.0) * inv_scale).floor() as i32;
    let v = ((-2.0 * dpq + 4.0 * dpt) * (1.0 / 3.0) * inv_scale).floor() as i32;
    let mut best_cost = f32::INFINITY;
    let mut best_i = 0;
    for i in 0..4 {
        let ot = o + (q * (u + (i & 1)) as f32 + t * (v + ((i & 2) >> 1)) as f32) * scale;
        let cost = (ot - p).squared_norm();
        if cost < best_cost {
            best_i = i;
            best_cost = cost;
        }
    }
    Vec2i::new(u + (best_i & 1), v + ((best_i & 2) >> 1))
}
fn position_floor_3(o: Vec3, q: Vec3, n: Vec3, p: Vec3, scale: f32, inv_scale: f32) -> Vec3 {
    let t = rotate60(q, n);
    let d = p - o;
    let dpq = q.dot(d);
    let dpt = t.dot(d);
    let u = ((4.0 * dpq - 2.0 * dpt) * (1.0 / 3.0) * inv_scale).floor();
    let v = ((-2.0 * dpq + 4.0 * dpt) * (1.0 / 3.0) * inv_scale).floor();
    o + (q * u + t * v) * scale
}
fn position_floor_index_3(o: Vec3, q: Vec3, n: Vec3, p: Vec3, inv_scale: f32) -> Vec2i {
    let t = rotate60(q, n);
    let d = p - o;
    let dpq = q.dot(d);
    let dpt = t.dot(d);
    Vec2i::new(
        ((4.0 * dpq - 2.0 * dpt) * (1.0 / 3.0) * inv_scale).floor() as i32,
        ((-2.0 * dpq + 4.0 * dpt) * (1.0 / 3.0) * inv_scale).floor() as i32,
    )
}

fn transport_intrinsic(p0: Vec3, n0: Vec3, p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3) -> (Vec3, Vec3) {
    let cos_theta = n1.dot(n0);
    if cos_theta >= 0.9999 {
        return (q1, o1);
    }
    let axis = n1.cross(n0);
    let denom = axis.dot(axis);
    if denom <= RCPOVERFLOW {
        return (q1, o1);
    }
    let factor = (1.0 - cos_theta) / denom;
    let middle = middle_point(p0, n0, p1, n1);
    let o = o1 - middle;
    let q = q1 * cos_theta + axis.cross(q1) + axis * (axis.dot(q1) * factor);
    let o = o * cos_theta + axis.cross(o) + axis * (axis.dot(o) * factor) + middle;
    (q, o)
}

pub fn compat_position_extrinsic_4(
    p0: Vec3, n0: Vec3, q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> (Vec3, Vec3) {
    let t0 = n0.cross(q0);
    let t1 = n1.cross(q1);
    let middle = middle_point(p0, n0, p1, n1);
    let o0p = position_floor_4(o0, q0, n0, middle, scale, inv_scale);
    let o1p = position_floor_4(o1, q1, n1, middle, scale, inv_scale);
    let mut best_cost = f32::INFINITY;
    let mut best_i = 0;
    let mut best_j = 0;
    for i in 0..4 {
        let o0t = o0p + (q0 * (i & 1) as f32 + t0 * ((i & 2) >> 1) as f32) * scale;
        for j in 0..4 {
            let o1t = o1p + (q1 * (j & 1) as f32 + t1 * ((j & 2) >> 1) as f32) * scale;
            let cost = (o0t - o1t).squared_norm();
            if cost < best_cost {
                best_i = i;
                best_j = j;
                best_cost = cost;
            }
        }
    }
    (
        o0p + (q0 * (best_i & 1) as f32 + t0 * ((best_i & 2) >> 1) as f32) * scale,
        o1p + (q1 * (best_j & 1) as f32 + t1 * ((best_j & 2) >> 1) as f32) * scale,
    )
}

pub fn compat_position_extrinsic_3(
    p0: Vec3, n0: Vec3, q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> (Vec3, Vec3) {
    let middle = middle_point(p0, n0, p1, n1);
    let o0 = position_floor_3(o0, q0, n0, middle, scale, inv_scale);
    let o1 = position_floor_3(o1, q1, n1, middle, scale, inv_scale);
    let t0 = rotate60(q0, n0);
    let t1 = rotate60(q1, n1);
    let mut best_cost = f32::INFINITY;
    let mut best_i = 0;
    let mut best_j = 0;
    for i in 0..4 {
        let o0t = o0 + (q0 * (i & 1) as f32 + t0 * ((i & 2) >> 1) as f32) * scale;
        for j in 0..4 {
            let o1t = o1 + (q1 * (j & 1) as f32 + t1 * ((j & 2) >> 1) as f32) * scale;
            let cost = (o0t - o1t).squared_norm();
            if cost < best_cost {
                best_i = i;
                best_j = j;
                best_cost = cost;
            }
        }
    }
    (
        o0 + (q0 * (best_i & 1) as f32 + t0 * ((best_i & 2) >> 1) as f32) * scale,
        o1 + (q1 * (best_j & 1) as f32 + t1 * ((best_j & 2) >> 1) as f32) * scale,
    )
}

pub fn compat_position_extrinsic(
    posy: i32,
    p0: Vec3, n0: Vec3, q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> (Vec3, Vec3) {
    if posy == 3 {
        compat_position_extrinsic_3(p0, n0, q0, o0, p1, n1, q1, o1, scale, inv_scale)
    } else {
        compat_position_extrinsic_4(p0, n0, q0, o0, p1, n1, q1, o1, scale, inv_scale)
    }
}

fn compat_position_intrinsic_4(
    p0: Vec3, n0: Vec3, _q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> (Vec3, Vec3) {
    let (q1, o1) = transport_intrinsic(p0, n0, p1, n1, q1, o1);
    (o0, position_round_4(o1, q1, n0, o0, scale, inv_scale))
}
fn compat_position_intrinsic_3(
    p0: Vec3, n0: Vec3, _q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> (Vec3, Vec3) {
    let (q1, o1) = transport_intrinsic(p0, n0, p1, n1, q1, o1);
    (o0, position_round_3(o1, q1, n0, o0, scale, inv_scale))
}

fn compat_position_extrinsic_index_4(
    p0: Vec3, n0: Vec3, q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> ((Vec2i, Vec2i), f32) {
    let t0 = n0.cross(q0);
    let t1 = n1.cross(q1);
    let middle = middle_point(p0, n0, p1, n1);
    let o0p = position_floor_index_4(o0, q0, n0, middle, inv_scale);
    let o1p = position_floor_index_4(o1, q1, n1, middle, inv_scale);
    let mut best_cost = f32::INFINITY;
    let mut best_i = 0;
    let mut best_j = 0;
    for i in 0..4 {
        let o0t = o0 + (q0 * ((i & 1) + o0p.x) as f32 + t0 * (((i & 2) >> 1) + o0p.y) as f32) * scale;
        for j in 0..4 {
            let o1t = o1 + (q1 * ((j & 1) + o1p.x) as f32 + t1 * (((j & 2) >> 1) + o1p.y) as f32) * scale;
            let cost = (o0t - o1t).squared_norm();
            if cost < best_cost {
                best_i = i;
                best_j = j;
                best_cost = cost;
            }
        }
    }
    (
        (
            Vec2i::new((best_i & 1) + o0p.x, ((best_i & 2) >> 1) + o0p.y),
            Vec2i::new((best_j & 1) + o1p.x, ((best_j & 2) >> 1) + o1p.y),
        ),
        best_cost,
    )
}

fn compat_position_extrinsic_index_3(
    p0: Vec3, n0: Vec3, q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> ((Vec2i, Vec2i), f32) {
    let t0 = rotate60(q0, n0);
    let t1 = rotate60(q1, n1);
    let middle = middle_point(p0, n0, p1, n1);
    let o0i = position_floor_index_3(o0, q0, n0, middle, inv_scale);
    let o1i = position_floor_index_3(o1, q1, n1, middle, inv_scale);
    let mut best_cost = f32::INFINITY;
    let mut best_i = 0;
    let mut best_j = 0;
    for i in 0..4 {
        let o0t = o0 + (q0 * (o0i.x + (i & 1)) as f32 + t0 * (o0i.y + ((i & 2) >> 1)) as f32) * scale;
        for j in 0..4 {
            let o1t = o1 + (q1 * (o1i.x + (j & 1)) as f32 + t1 * (o1i.y + ((j & 2) >> 1)) as f32) * scale;
            let cost = (o0t - o1t).squared_norm();
            if cost < best_cost {
                best_i = i;
                best_j = j;
                best_cost = cost;
            }
        }
    }
    (
        (
            Vec2i::new(o0i.x + (best_i & 1), o0i.y + ((best_i & 2) >> 1)),
            Vec2i::new(o1i.x + (best_j & 1), o1i.y + ((best_j & 2) >> 1)),
        ),
        best_cost,
    )
}

fn compat_position_intrinsic_index_4(
    p0: Vec3, n0: Vec3, _q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> ((Vec2i, Vec2i), f32) {
    let (q1, o1) = transport_intrinsic(p0, n0, p1, n1, q1, o1);
    let rounded = position_round_4(o1, q1, n0, o0, scale, inv_scale);
    (
        (Vec2i::ZERO, position_round_index_4(o1, q1, n0, o0, inv_scale)),
        (o0 - rounded).squared_norm(),
    )
}
fn compat_position_intrinsic_index_3(
    p0: Vec3, n0: Vec3, _q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> ((Vec2i, Vec2i), f32) {
    let (q1, o1) = transport_intrinsic(p0, n0, p1, n1, q1, o1);
    let rounded = position_round_3(o1, q1, n0, o0, scale, inv_scale);
    (
        (Vec2i::ZERO, position_round_index_3(o1, q1, n0, o0, scale, inv_scale)),
        (o0 - rounded).squared_norm(),
    )
}

pub fn compat_position_index(
    extrinsic: bool,
    posy: i32,
    p0: Vec3, n0: Vec3, q0: Vec3, o0: Vec3,
    p1: Vec3, n1: Vec3, q1: Vec3, o1: Vec3,
    scale: f32, inv_scale: f32,
) -> ((Vec2i, Vec2i), f32) {
    match (extrinsic, posy) {
        (true, 3) => compat_position_extrinsic_index_3(p0, n0, q0, o0, p1, n1, q1, o1, scale, inv_scale),
        (true, _) => compat_position_extrinsic_index_4(p0, n0, q0, o0, p1, n1, q1, o1, scale, inv_scale),
        (false, 3) => compat_position_intrinsic_index_3(p0, n0, q0, o0, p1, n1, q1, o1, scale, inv_scale),
        (false, _) => compat_position_intrinsic_index_4(p0, n0, q0, o0, p1, n1, q1, o1, scale, inv_scale),
    }
}

pub fn compat_orientation(extrinsic: bool, rosy: i32, q0: Vec3, n0: Vec3, q1: Vec3, n1: Vec3) -> (Vec3, Vec3) {
    match (extrinsic, rosy) {
        (true, 2) => compat_orientation_extrinsic_2(q0, n0, q1, n1),
        (true, 6) => compat_orientation_extrinsic_6(q0, n0, q1, n1),
        (true, _) => compat_orientation_extrinsic_4(q0, n0, q1, n1),
        (false, 2) => compat_orientation_intrinsic_2(q0, n0, q1, n1),
        (false, 6) => compat_orientation_intrinsic_6(q0, n0, q1, n1),
        (false, _) => compat_orientation_intrinsic_4(q0, n0, q1, n1),
    }
}

fn rotate_by(rosy: i32, q: Vec3, n: Vec3, amount: i32) -> Vec3 {
    match rosy {
        2 => rotate180_by(q, n, amount),
        6 => rotate60_by(q, n, amount),
        _ => rotate90_by(q, n, amount),
    }
}

fn round_position(posy: i32, o: Vec3, q: Vec3, n: Vec3, p: Vec3, scale: f32, inv_scale: f32) -> Vec3 {
    if posy == 3 {
        position_round_3(o, q, n, p, scale, inv_scale)
    } else {
        position_round_4(o, q, n, p, scale, inv_scale)
    }
}

/// Raw pointer that may cross threads. Safe only because each coloring phase
/// writes a disjoint independent set, and neighbor reads do not alias those writes.
#[derive(Clone, Copy)]
struct Ptr<T>(*mut T);
unsafe impl<T> Send for Ptr<T> {}
unsafe impl<T> Sync for Ptr<T> {}

#[derive(Clone, Copy)]
struct CPtr<T>(*const T);
unsafe impl<T> Send for CPtr<T> {}
unsafe impl<T> Sync for CPtr<T> {}

impl<T: Copy> CPtr<T> {
    unsafe fn get(self, i: usize) -> T {
        *self.0.add(i)
    }
}
impl<T: Copy> Ptr<T> {
    unsafe fn get(self, i: usize) -> T {
        *self.0.add(i)
    }
    unsafe fn set(self, i: usize, value: T) {
        *self.0.add(i) = value;
    }
}
impl<T> CPtr<T> {
    unsafe fn get_ref<'a>(self, i: usize) -> &'a T {
        &*self.0.add(i)
    }
}

fn for_phase<F: Fn(u32) + Sync>(parallel: bool, phase: &[u32], f: F) {
    if parallel {
        phase.par_iter().for_each(|i| f(*i));
    } else {
        for &i in phase {
            f(i);
        }
    }
}

pub fn optimize_orientations(h: &mut Hierarchy, level: usize, extrinsic: bool, rosy: i32, parallel: bool) {
    let phases: Vec<Vec<u32>> = h.levels[level].phases.clone();
    let n_ptr = CPtr(h.levels[level].n.as_ptr());
    let q_ptr = Ptr(h.levels[level].q.as_mut_ptr());
    let cqw_empty = h.levels[level].cqw.is_empty();
    let cqw_ptr = CPtr(h.levels[level].cqw.as_ptr());
    let cq_ptr = CPtr(h.levels[level].cq.as_ptr());
    let adj_links = CPtr(h.levels[level].adj.links.as_ptr());
    let offsets = h.levels[level].adj.offsets.clone();
    let frozen = h.frozen_q;

    for phase in &phases {
        for_phase(parallel, phase, |i| {
            let i = i as usize;
            let n_i = unsafe { n_ptr.get(i) };
            if frozen {
                let mut sum = Vec3::ZERO;
                let mut weight_sum = 0.0;
                let start = offsets[i] as usize;
                let end = offsets[i + 1] as usize;
                for li in start..end {
                    let link = unsafe { adj_links.get_ref(li) };
                    if link.weight == 0.0 {
                        continue;
                    }
                    let j = link.id as usize;
                    let n_j = unsafe { n_ptr.get(j) };
                    let q_j = unsafe { q_ptr.get(j) };
                    let temp = rotate_by(rosy, q_j, n_j, link.rot[1] as i32);
                    sum += rotate_by(rosy, temp, -n_i, link.rot[0] as i32) * link.weight;
                    weight_sum += link.weight;
                }
                sum -= n_i * n_i.dot(sum);
                let norm = sum.norm();
                if norm > RCPOVERFLOW {
                    sum = sum / norm;
                }
                if weight_sum > 0.0 {
                    unsafe { q_ptr.set(i, sum) };
                }
            } else {
                let mut weight_sum = 0.0;
                let mut sum = unsafe { q_ptr.get(i) };
                let start = offsets[i] as usize;
                let end = offsets[i + 1] as usize;
                for li in start..end {
                    let link = unsafe { adj_links.get_ref(li) };
                    if link.weight == 0.0 {
                        continue;
                    }
                    let j = link.id as usize;
                    let n_j = unsafe { n_ptr.get(j) };
                    let q_j = unsafe { q_ptr.get(j) };
                    let value = compat_orientation(extrinsic, rosy, sum, n_i, q_j, n_j);
                    sum = value.0 * weight_sum + value.1 * link.weight;
                    sum -= n_i * n_i.dot(sum);
                    weight_sum += link.weight;
                    let norm = sum.norm();
                    if norm > RCPOVERFLOW {
                        sum = sum / norm;
                    }
                }
                if !cqw_empty {
                    let cw = unsafe { cqw_ptr.get(i) };
                    if cw != 0.0 {
                        let cq = unsafe { cq_ptr.get(i) };
                        let value = compat_orientation(extrinsic, rosy, sum, n_i, cq, n_i);
                        sum = value.0 * (1.0 - cw) + value.1 * cw;
                        sum -= n_i * n_i.dot(sum);
                        let norm = sum.norm();
                        if norm > RCPOVERFLOW {
                            sum = sum / norm;
                        }
                    }
                }
                if weight_sum > 0.0 {
                    unsafe { q_ptr.set(i, sum) };
                }
            }
        });
    }
}

pub fn optimize_positions(h: &mut Hierarchy, level: usize, extrinsic: bool, posy: i32, parallel: bool) {
    let phases: Vec<Vec<u32>> = h.levels[level].phases.clone();
    let scale = h.scale;
    let inv_scale = 1.0 / scale;
    let n_ptr = CPtr(h.levels[level].n.as_ptr());
    let v_ptr = CPtr(h.levels[level].v.as_ptr());
    let q_ptr = CPtr(h.levels[level].q.as_ptr());
    let o_ptr = Ptr(h.levels[level].o.as_mut_ptr());
    let cow_empty = h.levels[level].cow.is_empty();
    let cow_ptr = CPtr(h.levels[level].cow.as_ptr());
    let co_ptr = CPtr(h.levels[level].co.as_ptr());
    let cq_ptr = CPtr(h.levels[level].cq.as_ptr());
    let adj_links = CPtr(h.levels[level].adj.links.as_ptr());
    let offsets = h.levels[level].adj.offsets.clone();
    let frozen = h.frozen_o;

    for phase in &phases {
        for_phase(parallel, phase, |i| {
            let i = i as usize;
            let n_i = unsafe { n_ptr.get(i) };
            let v_i = unsafe { v_ptr.get(i) };
            let mut q_i = unsafe { q_ptr.get(i) };
            q_i = q_i.normalized();
            if frozen {
                let t_i = n_i.cross(q_i);
                let mut sum = Vec3::ZERO;
                let mut weight_sum = 0.0;
                let start = offsets[i] as usize;
                let end = offsets[i + 1] as usize;
                for li in start..end {
                    let link = unsafe { adj_links.get_ref(li) };
                    if link.weight == 0.0 {
                        continue;
                    }
                    let j = link.id as usize;
                    let n_j = unsafe { n_ptr.get(j) };
                    let q_j = unsafe { q_ptr.get(j) }.normalized();
                    let o_j = unsafe { o_ptr.get(j) };
                    let t_j = n_j.cross(q_j);
                    sum += o_j
                        + (q_j * link.tu[1] as f32
                            + t_j * link.tv[1] as f32
                            - q_i * link.tu[0] as f32
                            - t_i * link.tv[0] as f32)
                            * scale;
                    weight_sum += link.weight;
                }
                if weight_sum > 0.0 {
                    sum = sum / weight_sum;
                    sum -= n_i * n_i.dot(sum - v_i);
                    unsafe { o_ptr.set(i, sum) };
                }
            } else {
                let mut sum = unsafe { o_ptr.get(i) };
                let mut weight_sum = 0.0;
                let start = offsets[i] as usize;
                let end = offsets[i + 1] as usize;
                for li in start..end {
                    let link = unsafe { adj_links.get_ref(li) };
                    if link.weight == 0.0 {
                        continue;
                    }
                    let j = link.id as usize;
                    let n_j = unsafe { n_ptr.get(j) };
                    let v_j = unsafe { v_ptr.get(j) };
                    let q_j = unsafe { q_ptr.get(j) }.normalized();
                    let o_j = unsafe { o_ptr.get(j) };
                    let (a, b) = if extrinsic {
                        if posy == 3 {
                            compat_position_extrinsic_3(v_i, n_i, q_i, sum, v_j, n_j, q_j, o_j, scale, inv_scale)
                        } else {
                            compat_position_extrinsic_4(v_i, n_i, q_i, sum, v_j, n_j, q_j, o_j, scale, inv_scale)
                        }
                    } else if posy == 3 {
                        compat_position_intrinsic_3(v_i, n_i, q_i, sum, v_j, n_j, q_j, o_j, scale, inv_scale)
                    } else {
                        compat_position_intrinsic_4(v_i, n_i, q_i, sum, v_j, n_j, q_j, o_j, scale, inv_scale)
                    };
                    sum = a * weight_sum + b * link.weight;
                    weight_sum += link.weight;
                    if weight_sum > RCPOVERFLOW {
                        sum = sum / weight_sum;
                    }
                    sum -= n_i * n_i.dot(sum - v_i);
                }
                if !cow_empty {
                    let cw = unsafe { cow_ptr.get(i) };
                    if cw != 0.0 {
                        let co = unsafe { co_ptr.get(i) };
                        let cq = unsafe { cq_ptr.get(i) };
                        let mut d = co - sum;
                        d -= cq * cq.dot(d);
                        sum += d * cw;
                        sum -= n_i * n_i.dot(sum - v_i);
                    }
                }
                if weight_sum > 0.0 {
                    let rounded = round_position(posy, sum, q_i, n_i, v_i, scale, inv_scale);
                    unsafe { o_ptr.set(i, rounded) };
                }
            }
        });
    }
}

fn freeze_orientations(h: &mut Hierarchy, extrinsic: bool, rosy: i32) {
    if rosy != 4 {
        return;
    }
    let n = h.size(0);
    for i in 0..n {
        let q_i = h.levels[0].q[i].normalized();
        let n_i = h.levels[0].n[i];
        let range = h.levels[0].adj.range(i);
        for li in range {
            let j = h.levels[0].adj.links[li].id as usize;
            let q_j = h.levels[0].q[j].normalized();
            let n_j = h.levels[0].n[j];
            let (a, b) = orientation_index(extrinsic, rosy, q_i, n_i, q_j, n_j);
            h.levels[0].adj.links[li].rot = [a as u8, b as u8];
        }
    }
    h.frozen_q = true;
}

fn freeze_positions(h: &mut Hierarchy, extrinsic: bool, posy: i32) {
    if posy != 4 {
        return;
    }
    let scale = h.scale;
    let inv_scale = 1.0 / scale;
    let n = h.size(0);
    for i in 0..n {
        let n_i = h.levels[0].n[i];
        let v_i = h.levels[0].v[i];
        let o_i = h.levels[0].o[i];
        let q_i = h.levels[0].q[i].normalized();
        let range = h.levels[0].adj.range(i);
        for li in range {
            let j = h.levels[0].adj.links[li].id as usize;
            let n_j = h.levels[0].n[j];
            let v_j = h.levels[0].v[j];
            let o_j = h.levels[0].o[j];
            let q_j = h.levels[0].q[j].normalized();
            let ((a, b), _) = compat_position_index(
                extrinsic, posy, v_i, n_i, q_i, o_i, v_j, n_j, q_j, o_j, scale, inv_scale,
            );
            h.levels[0].adj.links[li].tu = [a.x, b.x];
            h.levels[0].adj.links[li].tv = [a.y, b.y];
        }
    }
    h.frozen_o = true;
}

fn prolong_orientation(h: &mut Hierarchy, coarse: usize) {
    let fine = coarse - 1;
    let src = h.levels[coarse].q.clone();
    let normals = h.levels[fine].n.clone();
    let upper = h.to_upper[fine].clone();
    for (j, q) in src.iter().enumerate() {
        for k in 0..2 {
            let dest = upper[j][k];
            if dest == INVALID {
                continue;
            }
            let n = normals[dest as usize];
            h.levels[fine].q[dest as usize] = *q - n * n.dot(*q);
        }
    }
}

fn prolong_position(h: &mut Hierarchy, coarse: usize) {
    let fine = coarse - 1;
    let src = h.levels[coarse].o.clone();
    let normals = h.levels[fine].n.clone();
    let verts = h.levels[fine].v.clone();
    let upper = h.to_upper[fine].clone();
    for (j, o) in src.iter().enumerate() {
        for k in 0..2 {
            let dest = upper[j][k];
            if dest == INVALID {
                continue;
            }
            let n = normals[dest as usize];
            let v = verts[dest as usize];
            let mut o = *o;
            o -= n * n.dot(o - v);
            h.levels[fine].o[dest as usize] = o;
        }
    }
}

pub fn optimize_orientations_hierarchical(h: &mut Hierarchy, extrinsic: bool, rosy: i32, parallel: bool) {
    const LEVEL_ITERS: i32 = 6;
    let mut level = h.levels.len() as i32 - 1;
    let mut iters = 0i32;
    h.frozen_q = false;
    while level >= 0 {
        optimize_orientations(h, level as usize, extrinsic, rosy, parallel);
        iters += 1;
        let last = iters >= LEVEL_ITERS;
        if level > 0 && last {
            prolong_orientation(h, level as usize);
        }
        if last {
            if level == 0 {
                freeze_orientations(h, extrinsic, rosy);
                h.iterations_q += 1;
                break;
            }
            level -= 1;
            iters = 0;
        }
    }
}

pub fn optimize_positions_hierarchical(h: &mut Hierarchy, extrinsic: bool, posy: i32, parallel: bool) {
    const LEVEL_ITERS: i32 = 6;
    let mut level = h.levels.len() as i32 - 1;
    let mut iters = 0i32;
    h.frozen_o = false;
    while level >= 0 {
        optimize_positions(h, level as usize, extrinsic, posy, parallel);
        iters += 1;
        let last = iters >= LEVEL_ITERS;
        if level > 0 && last {
            prolong_position(h, level as usize);
        }
        if last {
            if level == 0 {
                freeze_positions(h, extrinsic, posy);
                h.iterations_o += 1;
                break;
            }
            level -= 1;
            iters = 0;
            h.iterations_o += 1;
        }
    }
}

pub fn compute_orientation_singularities(h: &Hierarchy, extrinsic: bool, rosy: i32) -> Vec<(u32, u32)> {
    let mut sing = Vec::new();
    if h.faces.degree != 3 {
        return sing;
    }
    for f in 0..h.faces.cols() {
        let mut index = 0i32;
        for k in 0..3 {
            let i = h.faces.get(k, f) as usize;
            let j = h.faces.get(if k == 2 { 0 } else { k + 1 }, f) as usize;
            let (a, b) = orientation_index(
                extrinsic,
                rosy,
                h.levels[0].q[i],
                h.levels[0].n[i],
                h.levels[0].q[j],
                h.levels[0].n[j],
            );
            index += b - a;
        }
        index = modulo(index, rosy);
        if index == 1 || index == rosy - 1 {
            sing.push((f as u32, index as u32));
        }
    }
    sing
}

/// Reference used by boundary propagation; keeps the import live for coordinate frames.
#[allow(dead_code)]
fn _frame(n: Vec3) -> (Vec3, Vec3) {
    coordinate_system(n)
}
