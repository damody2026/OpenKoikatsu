//! pair_align.py (Docs/VIDEO_MOTION_NATIVE_SPEC.md §7.2): fit each person's world → camera
//! rigid map from all frames, express the other person in the anchor's world, and measure
//! the per-frame body surface gap in the source video.

use crate::geom::*;
use crate::gvhmr::Output;
use crate::smplx::{self, Model};

/// pair_align.aa_to_mat (Rodrigues, θ floor 1e-12).
fn aa_to_mat(aa: V3) -> M3 {
    let th = norm(aa);
    let k = scale(aa, 1. / th.max(1e-12));
    let (x, y, z) = (k[0], k[1], k[2]);
    let (c, s) = (th.cos(), th.sin());
    let cc = 1. - c;
    [
        [c + x * x * cc, x * y * cc - z * s, x * z * cc + y * s],
        [y * x * cc + z * s, c + y * y * cc, y * z * cc - x * s],
        [z * x * cc - y * s, z * y * cc + x * s, c + z * z * cc],
    ]
}

/// pair_align.mat_to_aa.
fn mat_to_aa(m: &M3) -> V3 {
    let c = ((m[0][0] + m[1][1] + m[2][2] - 1.) / 2.).clamp(-1., 1.);
    let ang = c.acos();
    let v = [m[2][1] - m[1][2], m[0][2] - m[2][0], m[1][0] - m[0][1]];
    let s = ang.sin();
    if s > 1e-6 { scale(v, ang / (2. * s)) } else { scale(v, 0.5) }
}

fn det(m: &M3) -> f64 { dot(m[0], cross(m[1], m[2])) }

/// Symmetric 3×3 eigen decomposition (cyclic Jacobi): (values, vectors as columns).
fn eig_sym(a: &M3) -> ([f64; 3], M3) {
    let mut a = *a;
    let mut v = I3;
    for _ in 0..50 {
        let off = a[0][1].abs() + a[0][2].abs() + a[1][2].abs();
        if off < 1e-15 {
            break;
        }
        for (p, q) in [(0, 1), (0, 2), (1, 2)] {
            if a[p][q].abs() < 1e-300 {
                continue;
            }
            let theta = (a[q][q] - a[p][p]) / (2. * a[p][q]);
            let t = theta.signum() / (theta.abs() + (theta * theta + 1.).sqrt());
            let t = if theta == 0. { 1. } else { t };
            let c = 1. / (t * t + 1.).sqrt();
            let s = t * c;
            let mut r = I3;
            r[p][p] = c;
            r[q][q] = c;
            r[p][q] = s;
            r[q][p] = -s;
            a = mul(&tr(&r), &mul(&a, &r));
            v = mul(&v, &r);
        }
    }
    ([a[0][0], a[1][1], a[2][2]], v)
}

/// Rotation maximising tr(Rᵀ M): R = U·diag(1,1,det(U Vᵀ))·Vᵀ from M = U S Vᵀ.
fn kabsch(m: &M3) -> M3 {
    let (vals, vecs) = eig_sym(&mul(&tr(m), m));
    let mut order = [0usize, 1, 2];
    order.sort_by(|&a, &b| vals[b].partial_cmp(&vals[a]).unwrap());
    let vcol: Vec<V3> = order.iter().map(|&i| col(&vecs, i)).collect();
    let mut ucol: Vec<V3> = vcol.iter().map(|v| normalize(mv(m, *v))).collect();
    ucol[2] = cross(ucol[0], ucol[1]);
    if dot(ucol[2], mv(m, vcol[2])) < 0. {
        ucol[2] = scale(ucol[2], -1.);
    }
    let u: M3 = std::array::from_fn(|i| [ucol[0][i], ucol[1][i], ucol[2][i]]);
    let v: M3 = std::array::from_fn(|i| [vcol[0][i], vcol[1][i], vcol[2][i]]);
    let d = det(&mul(&u, &tr(&v))).signum();
    let dm = [[1., 0., 0.], [0., 1., 0.], [0., 0., d]];
    mul(&mul(&u, &dm), &tr(&v))
}

pub struct Fit {
    pub r: M3,
    pub t: V3,
    pub j0: V3,
    pub resid_deg: f64,
    pub resid_m: f64,
}

/// world_to_camera for one person.
pub fn world_to_camera(o: &Output, model: &Model) -> Fit {
    let shaped = model.shaped(&o.global.betas);
    let j0 = model.joints(&shaped)[0];
    let l = o.global.transl.len();
    let rg: Vec<M3> = o.global.global_orient.iter().map(|a| aa_to_mat(*a)).collect();
    let rc: Vec<M3> = o.incam.global_orient.iter().map(|a| aa_to_mat(*a)).collect();
    let pg: Vec<V3> = o.global.transl.iter().map(|t| add(*t, j0)).collect();
    let pc: Vec<V3> = o.incam.transl.iter().map(|t| add(*t, j0)).collect();
    let mut m = [[0.; 3]; 3];
    for i in 0..l {
        let p = mul(&rc[i], &tr(&rg[i]));
        for a in 0..3 {
            for b in 0..3 {
                m[a][b] += p[a][b];
            }
        }
    }
    let r = kabsch(&m);
    let mut t = [0.; 3];
    for i in 0..l {
        t = add(t, scale(sub(pc[i], mv(&r, pg[i])), 1. / l as f64));
    }
    let mut rot: Vec<f64> = (0..l).map(|i| norm(mat_to_aa(&mul(&tr(&mul(&r, &rg[i])), &rc[i]))).to_degrees()).collect();
    let mut pos: Vec<f64> = (0..l).map(|i| norm(sub(pc[i], add(mv(&r, pg[i]), t)))).collect();
    Fit { r, t, j0, resid_deg: median(&mut rot), resid_m: median(&mut pos) }
}

pub fn median(v: &mut [f64]) -> f64 {
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let n = v.len();
    if n == 0 { 0. } else if n % 2 == 1 { v[n / 2] } else { (v[n / 2 - 1] + v[n / 2]) / 2. }
}

/// Moves `other` into `anchor`'s world (global_orient, transl) and returns the per-frame surface gap.
pub fn align(anchor: &Output, other: &mut Output, model: &Model) -> (Vec<f64>, Fit, Fit) {
    let fa = world_to_camera(anchor, model);
    let fo = world_to_camera(other, model);
    let a = mul(&tr(&fa.r), &fo.r);
    let b = mv(&tr(&fa.r), sub(fo.t, fa.t));
    let l = other.global.transl.len();
    for i in 0..l {
        other.global.global_orient[i] = mat_to_aa(&mul(&a, &aa_to_mat(other.global.global_orient[i])));
        let pel = add(mv(&a, add(other.global.transl[i], fo.j0)), b);
        other.global.transl[i] = sub(pel, fo.j0);
    }
    let gaps = surface_gaps(anchor, other, model, 6);
    (gaps, fa, fo)
}

/// Per frame: minimum distance between the two bodies' vertices `[::stride]`.
pub fn surface_gaps(a: &Output, b: &Output, model: &Model, stride: usize) -> Vec<f64> {
    let idx: Vec<usize> = (0..model.nv).step_by(stride).collect();
    let (sa, sb) = (model.shaped(&a.global.betas), model.shaped(&b.global.betas));
    let (ja, jb) = (model.joints(&sa), model.joints(&sb));
    (0..a.global.transl.len())
        .map(|f| {
            let va = model.vertices(&sa, &ja, a.global.global_orient[f], &a.global.body_pose[f], a.global.transl[f], &idx);
            let vb = model.vertices(&sb, &jb, b.global.global_orient[f], &b.global.body_pose[f], b.global.transl[f], &idx);
            let mut best = f64::INFINITY;
            for p in &va {
                for q in &vb {
                    let d = (p[0] - q[0]).powi(2) + (p[1] - q[1]).powi(2) + (p[2] - q[2]).powi(2);
                    if d < best {
                        best = d;
                    }
                }
            }
            best.sqrt()
        })
        .collect()
}

#[allow(dead_code)]
fn _uses() { let _ = smplx::rodrigues; }
