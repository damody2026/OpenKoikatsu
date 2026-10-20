//! Rotation math matching pytorch3d / GVHMR helpers (Docs/VIDEO_MOTION_NATIVE_SPEC.md §6).
//! Matrices are row-major `[row][col]`, column vectors (M · v).

pub type V3 = [f64; 3];
pub type M3 = [[f64; 3]; 3];
/// wxyz quaternion (GVHMR / pytorch3d order).
pub type Q = [f64; 4];

pub const I3: M3 = [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]];

pub fn add(a: V3, b: V3) -> V3 { [a[0] + b[0], a[1] + b[1], a[2] + b[2]] }
pub fn sub(a: V3, b: V3) -> V3 { [a[0] - b[0], a[1] - b[1], a[2] - b[2]] }
pub fn scale(a: V3, s: f64) -> V3 { [a[0] * s, a[1] * s, a[2] * s] }
pub fn dot(a: V3, b: V3) -> f64 { a[0] * b[0] + a[1] * b[1] + a[2] * b[2] }
pub fn cross(a: V3, b: V3) -> V3 { [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]] }
pub fn norm(a: V3) -> f64 { dot(a, a).sqrt() }
/// `F.normalize` (eps 1e-12): zero stays zero.
pub fn normalize(a: V3) -> V3 { scale(a, 1. / norm(a).max(1e-12)) }

pub fn mul(a: &M3, b: &M3) -> M3 {
    let mut m = [[0.; 3]; 3];
    for i in 0..3 {
        for j in 0..3 {
            m[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
    m
}
pub fn tr(a: &M3) -> M3 {
    let mut m = [[0.; 3]; 3];
    for i in 0..3 {
        for j in 0..3 {
            m[i][j] = a[j][i];
        }
    }
    m
}
pub fn mv(a: &M3, v: V3) -> V3 { [dot(a[0], v), dot(a[1], v), dot(a[2], v)] }
pub fn col(a: &M3, j: usize) -> V3 { [a[0][j], a[1][j], a[2][j]] }
pub fn inverse(a: &M3) -> M3 {
    let c = |r0: usize, c0: usize, r1: usize, c1: usize| a[r0][c0] * a[r1][c1] - a[r0][c1] * a[r1][c0];
    let det = a[0][0] * c(1, 1, 2, 2) - a[0][1] * c(1, 0, 2, 2) + a[0][2] * c(1, 0, 2, 1);
    let d = 1. / det;
    [
        [c(1, 1, 2, 2) * d, -c(0, 1, 2, 2) * d, c(0, 1, 1, 2) * d],
        [-c(1, 0, 2, 2) * d, c(0, 0, 2, 2) * d, -c(0, 0, 1, 2) * d],
        [c(1, 0, 2, 1) * d, -c(0, 0, 2, 1) * d, c(0, 0, 1, 1) * d],
    ]
}
/// `matrix.normalized_matrix`: each column divided by (‖column‖ + 1e-9).
pub fn normalized_matrix(a: &M3) -> M3 {
    let mut m = *a;
    for j in 0..3 {
        let n = norm(col(a, j)) + 1e-9;
        for i in 0..3 {
            m[i][j] /= n;
        }
    }
    m
}

/// pytorch3d `rotation_6d_to_matrix`: rows b1, b2, b3.
pub fn r6d_to_matrix(d: &[f64]) -> M3 {
    let a1 = [d[0], d[1], d[2]];
    let a2 = [d[3], d[4], d[5]];
    let b1 = normalize(a1);
    let b2 = normalize(sub(a2, scale(b1, dot(b1, a2))));
    let b3 = cross(b1, b2);
    [b1, b2, b3]
}
/// pytorch3d `matrix_to_rotation_6d`: first two rows.
pub fn matrix_to_r6d(m: &M3) -> [f64; 6] { [m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2]] }

fn sqrt_positive(x: f64) -> f64 { if x > 0. { x.sqrt() } else { 0. } }

/// pytorch3d `matrix_to_quaternion` (largest-candidate selection, w ≥ 0).
pub fn matrix_to_quat(m: &M3) -> Q {
    let (m00, m01, m02) = (m[0][0], m[0][1], m[0][2]);
    let (m10, m11, m12) = (m[1][0], m[1][1], m[1][2]);
    let (m20, m21, m22) = (m[2][0], m[2][1], m[2][2]);
    let q_abs = [
        sqrt_positive(1. + m00 + m11 + m22),
        sqrt_positive(1. + m00 - m11 - m22),
        sqrt_positive(1. - m00 + m11 - m22),
        sqrt_positive(1. - m00 - m11 + m22),
    ];
    let cand = [
        [q_abs[0] * q_abs[0], m21 - m12, m02 - m20, m10 - m01],
        [m21 - m12, q_abs[1] * q_abs[1], m10 + m01, m02 + m20],
        [m02 - m20, m10 + m01, q_abs[2] * q_abs[2], m12 + m21],
        [m10 - m01, m20 + m02, m21 + m12, q_abs[3] * q_abs[3]],
    ];
    let mut best = 0;
    for i in 1..4 {
        if q_abs[i] > q_abs[best] {
            best = i;
        }
    }
    let d = 2. * q_abs[best].max(0.1);
    let mut q = [cand[best][0] / d, cand[best][1] / d, cand[best][2] / d, cand[best][3] / d];
    if q[0] < 0. {
        q = [-q[0], -q[1], -q[2], -q[3]];
    }
    q
}
/// pytorch3d `quaternion_to_matrix` (two_s = 2/‖q‖²).
pub fn quat_to_matrix(q: Q) -> M3 {
    let (r, i, j, k) = (q[0], q[1], q[2], q[3]);
    let s = 2. / (r * r + i * i + j * j + k * k);
    [
        [1. - s * (j * j + k * k), s * (i * j - k * r), s * (i * k + j * r)],
        [s * (i * j + k * r), 1. - s * (i * i + k * k), s * (j * k - i * r)],
        [s * (i * k - j * r), s * (j * k + i * r), 1. - s * (i * i + j * j)],
    ]
}
/// sin(x)/x with the limit 1 at 0.
fn sinc_over(x: f64) -> f64 { if x.abs() < 1e-8 { 1. - x * x / 6. } else { x.sin() / x } }
pub fn quat_to_aa(q: Q) -> V3 {
    let v = [q[1], q[2], q[3]];
    let half = norm(v).atan2(q[0]);
    // aa = xyz / (0.5·sinc(half/π)) = xyz · 2·half/sin(half)
    scale(v, 2. / sinc_over(half))
}
pub fn aa_to_quat(aa: V3) -> Q {
    let th = norm(aa);
    let s = 0.5 * sinc_over(th * 0.5);
    [(th * 0.5).cos(), aa[0] * s, aa[1] * s, aa[2] * s]
}
pub fn aa_to_matrix(aa: V3) -> M3 { quat_to_matrix(aa_to_quat(aa)) }
pub fn matrix_to_aa(m: &M3) -> V3 { quat_to_aa(matrix_to_quat(m)) }

// ---- GVHMR quaternion helpers (hmr4d/utils/geo/quaternion.py), wxyz

pub fn qnormalize(q: Q) -> Q {
    let n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt().max(1e-8);
    [q[0] / n, q[1] / n, q[2] / n, q[3] / n]
}
pub fn qinv(q: Q) -> Q { [q[0], -q[1], -q[2], -q[3]] }
/// Hamilton product q·r.
pub fn qmul(q: Q, r: Q) -> Q {
    [
        r[0] * q[0] - r[1] * q[1] - r[2] * q[2] - r[3] * q[3],
        r[0] * q[1] + r[1] * q[0] - r[2] * q[3] + r[3] * q[2],
        r[0] * q[2] + r[1] * q[3] + r[2] * q[0] - r[3] * q[1],
        r[0] * q[3] - r[1] * q[2] + r[2] * q[1] + r[3] * q[0],
    ]
}
pub fn qrot(q: Q, v: V3) -> V3 {
    let u = [q[1], q[2], q[3]];
    let uv = cross(u, v);
    let uuv = cross(u, uv);
    add(v, scale(add(scale(uv, q[0]), uuv), 2.))
}
fn qpow(q0: Q, t: f64) -> Q {
    let q0 = qnormalize(q0);
    let mut th = q0[0].clamp(-1., 1.).acos();
    if th.abs() <= 1e-9 {
        th = 1e-9;
    }
    let s = th.sin();
    let v = [q0[1] / s, q0[2] / s, q0[3] / s];
    let a = t * th;
    [a.cos(), v[0] * a.sin(), v[1] * a.sin(), v[2] * a.sin()]
}
pub fn qslerp(q0: Q, q1: Q, t: f64) -> Q {
    let q0 = qnormalize(q0);
    let q1 = qnormalize(q1);
    qmul(qpow(qmul(q1, qinv(q0)), t), q0)
}
pub fn qbetween(v0: V3, v1: V3) -> Q {
    let mut v = cross(v0, v1);
    let w = (dot(v0, v0) * dot(v1, v1)).sqrt() + dot(v0, v1);
    if norm(v) == 0. && w.abs() <= 1e-4 {
        v = [0., 1., 0.];
    }
    qnormalize([w, v[0], v[1], v[2]])
}

/// GVHMR `gaussian_smooth`: scipy kernel (radius ⌊4σ+0.5⌋), replicate padding, over a series.
pub fn gaussian_smooth(x: &[f64], sigma: f64) -> Vec<f64> {
    let radius = (4. * sigma + 0.5) as i64;
    let mut k: Vec<f64> = (-radius..=radius).map(|i| (-0.5 * (i * i) as f64 / (sigma * sigma)).exp()).collect();
    let sum: f64 = k.iter().sum();
    k.iter_mut().for_each(|v| *v /= sum);
    let n = x.len() as i64;
    (0..n)
        .map(|t| {
            k.iter()
                .enumerate()
                .map(|(i, w)| w * x[(t + i as i64 - radius).clamp(0, n - 1) as usize])
                .sum()
        })
        .collect()
}
