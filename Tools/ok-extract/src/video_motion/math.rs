//! Small f64 3x3 / vector helpers for the video retargeter.

pub type V3 = [f64; 3];
pub type M3 = [[f64; 3]; 3];

pub const I3: M3 = [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]];

pub fn add(a: V3, b: V3) -> V3 { [a[0] + b[0], a[1] + b[1], a[2] + b[2]] }
pub fn sub(a: V3, b: V3) -> V3 { [a[0] - b[0], a[1] - b[1], a[2] - b[2]] }
pub fn scale(a: V3, k: f64) -> V3 { [a[0] * k, a[1] * k, a[2] * k] }
pub fn dot(a: V3, b: V3) -> f64 { a[0] * b[0] + a[1] * b[1] + a[2] * b[2] }
pub fn cross(a: V3, b: V3) -> V3 { [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]] }
pub fn norm(a: V3) -> f64 { dot(a, a).sqrt() }
pub fn normalize(a: V3) -> V3 { let n = norm(a).max(1e-12); scale(a, 1. / n) }

pub fn mul(a: &M3, b: &M3) -> M3 {
    let mut m = [[0.; 3]; 3];
    for i in 0..3 { for j in 0..3 { m[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]; } }
    m
}
pub fn mulv(a: &M3, v: V3) -> V3 {
    [a[0][0] * v[0] + a[0][1] * v[1] + a[0][2] * v[2], a[1][0] * v[0] + a[1][1] * v[1] + a[1][2] * v[2], a[2][0] * v[0] + a[2][1] * v[1] + a[2][2] * v[2]]
}
pub fn t(a: &M3) -> M3 { [[a[0][0], a[1][0], a[2][0]], [a[0][1], a[1][1], a[2][1]], [a[0][2], a[1][2], a[2][2]]] }
/// a^T * b
pub fn tmul(a: &M3, b: &M3) -> M3 { mul(&t(a), b) }

/// Rotation vector -> matrix (Rodrigues).
pub fn exp(v: V3) -> M3 {
    let ang = norm(v);
    if ang < 1e-12 { return I3; }
    let k = scale(v, 1. / ang);
    let (s, c) = ang.sin_cos();
    let cc = 1. - c;
    [[c + k[0] * k[0] * cc, k[0] * k[1] * cc - k[2] * s, k[0] * k[2] * cc + k[1] * s],
     [k[1] * k[0] * cc + k[2] * s, c + k[1] * k[1] * cc, k[1] * k[2] * cc - k[0] * s],
     [k[2] * k[0] * cc - k[1] * s, k[2] * k[1] * cc + k[0] * s, c + k[2] * k[2] * cc]]
}

/// Matrix -> rotation vector, robust near 0 and 180 degrees.
pub fn log(m: &M3) -> V3 {
    let q = to_quat(m);
    let (x, y, z, w) = if q[3] < 0. { (-q[0], -q[1], -q[2], -q[3]) } else { (q[0], q[1], q[2], q[3]) };
    let s = (x * x + y * y + z * z).sqrt();
    if s < 1e-12 { return [2. * x, 2. * y, 2. * z]; }
    let ang = 2. * s.atan2(w);
    [x / s * ang, y / s * ang, z / s * ang]
}

/// Quaternion (x, y, z, w) -> matrix.
pub fn from_quat(q: [f64; 4]) -> M3 {
    let n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt().max(1e-12);
    let (x, y, z, w) = (q[0] / n, q[1] / n, q[2] / n, q[3] / n);
    [[1. - 2. * (y * y + z * z), 2. * (x * y - z * w), 2. * (x * z + y * w)],
     [2. * (x * y + z * w), 1. - 2. * (x * x + z * z), 2. * (y * z - x * w)],
     [2. * (x * z - y * w), 2. * (y * z + x * w), 1. - 2. * (x * x + y * y)]]
}

/// Matrix -> quaternion (x, y, z, w).
pub fn to_quat(m: &M3) -> [f64; 4] {
    let tr = m[0][0] + m[1][1] + m[2][2];
    let q = if tr > 0. {
        let s = (tr + 1.).sqrt() * 2.;
        [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s]
    } else if m[0][0] >= m[1][1] && m[0][0] >= m[2][2] {
        let s = (1. + m[0][0] - m[1][1] - m[2][2]).max(1e-12).sqrt() * 2.;
        [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s]
    } else if m[1][1] >= m[2][2] {
        let s = (1. + m[1][1] - m[0][0] - m[2][2]).max(1e-12).sqrt() * 2.;
        [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s]
    } else {
        let s = (1. + m[2][2] - m[0][0] - m[1][1]).max(1e-12).sqrt() * 2.;
        [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s]
    };
    let n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt();
    [q[0] / n, q[1] / n, q[2] / n, q[3] / n]
}

/// Minimal rotation taking direction a onto direction b.
pub fn align(a: V3, b: V3) -> M3 {
    let (a, b) = (normalize(a), normalize(b));
    let v = cross(a, b);
    let c = dot(a, b);
    if c < -0.999999 {
        let mut axis = cross(a, [1., 0., 0.]);
        if norm(axis) < 1e-6 { axis = cross(a, [0., 1., 0.]); }
        return exp(scale(normalize(axis), std::f64::consts::PI));
    }
    let vx = [[0., -v[2], v[1]], [v[2], 0., -v[0]], [-v[1], v[0], 0.]];
    let vx2 = mul(&vx, &vx);
    let k = 1. / (1. + c);
    let mut m = I3;
    for i in 0..3 { for j in 0..3 { m[i][j] += vx[i][j] + vx2[i][j] * k; } }
    m
}

/// Mirror X (SMPL right-handed <-> Unity left-handed): M R M.
pub fn mirror_rot(r: &M3) -> M3 {
    let mut m = *r;
    m[0][1] = -m[0][1]; m[0][2] = -m[0][2]; m[1][0] = -m[1][0]; m[2][0] = -m[2][0];
    m
}
pub fn mirror_pos(p: V3) -> V3 { [-p[0], p[1], p[2]] }

/// Quaternion sign continuity over time.
pub fn continuous(q: &mut [[f64; 4]]) {
    for f in 1..q.len() {
        let d = q[f][0] * q[f - 1][0] + q[f][1] * q[f - 1][1] + q[f][2] * q[f - 1][2] + q[f][3] * q[f - 1][3];
        if d < 0. { for c in 0..4 { q[f][c] = -q[f][c]; } }
    }
}

/// Gaussian smoothing over time with edge renormalisation.
pub fn smooth<T: Copy>(x: &[T], sigma: f64, get: impl Fn(&T) -> Vec<f64>, set: impl Fn(&[f64]) -> T) -> Vec<T> {
    if sigma <= 0. || x.len() < 2 { return x.to_vec(); }
    let r = (3. * sigma).ceil() as isize;
    let w: Vec<f64> = (-r..=r).map(|o| (-0.5 * (o as f64 / sigma).powi(2)).exp()).collect();
    let vals: Vec<Vec<f64>> = x.iter().map(&get).collect();
    let n = vals[0].len();
    (0..x.len() as isize).map(|f| {
        let mut acc = vec![0.; n];
        let mut tot = 0.;
        for o in -r..=r {
            let g = f + o;
            if g < 0 || g >= x.len() as isize { continue; }
            let k = w[(o + r) as usize];
            for c in 0..n { acc[c] += k * vals[g as usize][c]; }
            tot += k;
        }
        for c in &mut acc { *c /= tot; }
        set(&acc)
    }).collect()
}
pub fn smooth_v3(x: &[V3], sigma: f64) -> Vec<V3> { smooth(x, sigma, |v| v.to_vec(), |a| [a[0], a[1], a[2]]) }
pub fn smooth_f(x: &[f64], sigma: f64) -> Vec<f64> { smooth(x, sigma, |v| vec![*v], |a| a[0]) }
