//! Camera rotation for moving-camera videos (replaces GVHMR's SimpleVO, Docs/VIDEO_MOTION_NATIVE_SPEC.md §5).
//!
//! Same sampling and outputs as SimpleVO (every 8th half-resolution frame, accumulated
//! R_w2c, slerp in between, `compute_cam_angvel`). Features differ: Shi-Tomasi corners tracked
//! with pyramidal Lucas-Kanade (forward-backward checked) instead of SIFT + FLANN, and the
//! relative rotation comes from RANSAC on either a rotation-only model or an essential matrix
//! (normalised 8-point, cheirality-checked decomposition), whichever explains the matches —
//! GVHMR only uses the rotation.

use crate::geom::*;

pub struct Gray {
    pub w: usize,
    pub h: usize,
    pub px: Vec<f32>,
}

impl Gray {
    /// SimpleVO's cv2.COLOR_BGR2GRAY applied to RGB data (source behaviour).
    pub fn from_rgb(rgb: &[u8], w: usize, h: usize) -> Gray {
        let px = rgb.chunks_exact(3).map(|p| 0.114 * p[0] as f32 + 0.587 * p[1] as f32 + 0.299 * p[2] as f32).collect();
        Gray { w, h, px }
    }
    fn at(&self, x: f32, y: f32) -> f32 {
        let x = x.clamp(0., (self.w - 1) as f32);
        let y = y.clamp(0., (self.h - 1) as f32);
        let (x0, y0) = (x.floor() as usize, y.floor() as usize);
        let (x1, y1) = ((x0 + 1).min(self.w - 1), (y0 + 1).min(self.h - 1));
        let (ax, ay) = (x - x0 as f32, y - y0 as f32);
        let p = |xx: usize, yy: usize| self.px[yy * self.w + xx];
        (1. - ay) * ((1. - ax) * p(x0, y0) + ax * p(x1, y0)) + ay * ((1. - ax) * p(x0, y1) + ax * p(x1, y1))
    }
    fn half(&self) -> Gray {
        let (w, h) = ((self.w / 2).max(1), (self.h / 2).max(1));
        let mut px = vec![0f32; w * h];
        for y in 0..h {
            for x in 0..w {
                let (sx, sy) = ((2 * x).min(self.w - 1), (2 * y).min(self.h - 1));
                let (sx1, sy1) = ((sx + 1).min(self.w - 1), (sy + 1).min(self.h - 1));
                px[y * w + x] = 0.25 * (self.px[sy * self.w + sx] + self.px[sy * self.w + sx1] + self.px[sy1 * self.w + sx] + self.px[sy1 * self.w + sx1]);
            }
        }
        Gray { w, h, px }
    }
}

/// Shi-Tomasi corners: min eigenvalue of the 5×5 structure tensor, quality 1%, min distance 8.
fn corners(g: &Gray, max: usize) -> Vec<[f32; 2]> {
    let (w, h) = (g.w, g.h);
    let mut gx = vec![0f32; w * h];
    let mut gy = vec![0f32; w * h];
    for y in 1..h - 1 {
        for x in 1..w - 1 {
            let p = |dx: isize, dy: isize| g.px[((y as isize + dy) as usize) * w + (x as isize + dx) as usize];
            gx[y * w + x] = (p(1, -1) + 2. * p(1, 0) + p(1, 1)) - (p(-1, -1) + 2. * p(-1, 0) + p(-1, 1));
            gy[y * w + x] = (p(-1, 1) + 2. * p(0, 1) + p(1, 1)) - (p(-1, -1) + 2. * p(0, -1) + p(1, -1));
        }
    }
    let r = 2;
    let border = 12;
    let mut score = vec![0f32; w * h];
    let mut best = 0f32;
    for y in border..h.saturating_sub(border) {
        for x in border..w.saturating_sub(border) {
            let (mut a, mut b, mut c) = (0f32, 0f32, 0f32);
            for dy in 0..=2 * r {
                for dx in 0..=2 * r {
                    let i = (y + dy - r) * w + x + dx - r;
                    a += gx[i] * gx[i];
                    b += gx[i] * gy[i];
                    c += gy[i] * gy[i];
                }
            }
            let m = 0.5 * (a + c - ((a - c) * (a - c) + 4. * b * b).sqrt());
            score[y * w + x] = m;
            best = best.max(m);
        }
    }
    let mut cand: Vec<(f32, usize)> = score.iter().enumerate().filter(|(_, s)| **s > 0.01 * best).map(|(i, s)| (*s, i)).collect();
    cand.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap());
    let cell = 8usize;
    let (cw, ch) = (w / cell + 1, h / cell + 1);
    let mut taken = vec![false; cw * ch];
    let mut out = Vec::new();
    for (_, i) in cand {
        let (x, y) = (i % w, i / w);
        let c = (y / cell) * cw + x / cell;
        if taken[c] {
            continue;
        }
        taken[c] = true;
        out.push([x as f32, y as f32]);
        if out.len() >= max {
            break;
        }
    }
    out
}

/// Pyramidal Lucas-Kanade (21×21 window, 4 levels): new positions or None.
fn track(p0: &[Gray], p1: &[Gray], pts: &[[f32; 2]]) -> Vec<Option<[f32; 2]>> {
    let levels = p0.len();
    let r = 10i32;
    pts.iter()
        .map(|&pt| {
            let mut d = [0f32; 2];
            for l in (0..levels).rev() {
                let s = (1u32 << l) as f32;
                let (a, b) = (&p0[l], &p1[l]);
                let (x, y) = (pt[0] / s, pt[1] / s);
                let mut gxx = 0f32;
                let mut gxy = 0f32;
                let mut gyy = 0f32;
                let mut grads = Vec::with_capacity(((2 * r + 1) * (2 * r + 1)) as usize);
                for dy in -r..=r {
                    for dx in -r..=r {
                        let (px, py) = (x + dx as f32, y + dy as f32);
                        let ix = 0.5 * (a.at(px + 1., py) - a.at(px - 1., py));
                        let iy = 0.5 * (a.at(px, py + 1.) - a.at(px, py - 1.));
                        gxx += ix * ix;
                        gxy += ix * iy;
                        gyy += iy * iy;
                        grads.push((ix, iy, a.at(px, py)));
                    }
                }
                let det = gxx * gyy - gxy * gxy;
                if det.abs() < 1e-6 {
                    return None;
                }
                for _ in 0..20 {
                    let (mut bx, mut by) = (0f32, 0f32);
                    let mut k = 0;
                    for dy in -r..=r {
                        for dx in -r..=r {
                            let (ix, iy, i0) = grads[k];
                            k += 1;
                            let e = i0 - b.at(x + dx as f32 + d[0], y + dy as f32 + d[1]);
                            bx += e * ix;
                            by += e * iy;
                        }
                    }
                    let ux = (gyy * bx - gxy * by) / det;
                    let uy = (gxx * by - gxy * bx) / det;
                    d[0] += ux;
                    d[1] += uy;
                    if ux * ux + uy * uy < 1e-4 {
                        break;
                    }
                }
                if l > 0 {
                    d = [d[0] * 2., d[1] * 2.];
                }
            }
            let q = [pt[0] + d[0], pt[1] + d[1]];
            let w = p0[0].w as f32;
            let h = p0[0].h as f32;
            if q[0] < 0. || q[1] < 0. || q[0] >= w || q[1] >= h { None } else { Some(q) }
        })
        .collect()
}

fn pyramid(g: Gray, levels: usize) -> Vec<Gray> {
    let mut out = vec![g];
    for _ in 1..levels {
        let next = out.last().unwrap().half();
        out.push(next);
    }
    out
}

/// Matched points between two frames (forward-backward checked).
pub fn matches(a: &Gray, b: &Gray) -> (Vec<[f32; 2]>, Vec<[f32; 2]>) {
    let pa = pyramid(Gray { w: a.w, h: a.h, px: a.px.clone() }, 4);
    let pb = pyramid(Gray { w: b.w, h: b.h, px: b.px.clone() }, 4);
    let pts = corners(a, 800);
    let fwd = track(&pa, &pb, &pts);
    let good: Vec<([f32; 2], [f32; 2])> = pts.iter().zip(&fwd).filter_map(|(p, q)| q.map(|q| (*p, q))).collect();
    let back = track(&pb, &pa, &good.iter().map(|g| g.1).collect::<Vec<_>>());
    let mut m0 = Vec::new();
    let mut m1 = Vec::new();
    for ((p, q), r) in good.iter().zip(back) {
        if let Some(r) = r {
            if (r[0] - p[0]).powi(2) + (r[1] - p[1]).powi(2) < 1. {
                m0.push(*p);
                m1.push(*q);
            }
        }
    }
    (m0, m1)
}

// ---------------------------------------------------------------- geometry

struct Rng(u64);
impl Rng {
    fn next(&mut self, n: usize) -> usize {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        (self.0 % n as u64) as usize
    }
}

/// Rotation R with b1 ≈ R·b0 (Kabsch on unit bearings).
fn kabsch_bearings(b0: &[V3], b1: &[V3], idx: &[usize]) -> M3 {
    let mut m = [[0.; 3]; 3];
    for &i in idx {
        for r in 0..3 {
            for c in 0..3 {
                m[r][c] += b1[i][r] * b0[i][c];
            }
        }
    }
    nearest_rotation(&m)
}

/// Rotation maximising tr(Rᵀ M) via symmetric eigen of MᵀM.
fn nearest_rotation(m: &M3) -> M3 {
    let (vals, vecs) = eig3(&mul(&tr(m), m));
    let mut order = [0usize, 1, 2];
    order.sort_by(|&a, &b| vals[b].partial_cmp(&vals[a]).unwrap());
    let v: Vec<V3> = order.iter().map(|&i| col(&vecs, i)).collect();
    let mut u: Vec<V3> = v.iter().map(|x| normalize(mv(m, *x))).collect();
    u[2] = cross(u[0], u[1]);
    if dot(u[2], mv(m, v[2])) < 0. {
        u[2] = scale(u[2], -1.);
    }
    let um: M3 = std::array::from_fn(|i| [u[0][i], u[1][i], u[2][i]]);
    let vm: M3 = std::array::from_fn(|i| [v[0][i], v[1][i], v[2][i]]);
    let det = |a: &M3| dot(a[0], cross(a[1], a[2]));
    let d = det(&mul(&um, &tr(&vm))).signum();
    mul(&mul(&um, &[[1., 0., 0.], [0., 1., 0.], [0., 0., d]]), &tr(&vm))
}

/// 3×3 symmetric eigen (Jacobi): values and eigenvectors as columns.
fn eig3(a: &M3) -> ([f64; 3], M3) {
    let (vals, vecs) = jacobi(&a.iter().flat_map(|r| r.iter().cloned()).collect::<Vec<_>>(), 3);
    let v: M3 = std::array::from_fn(|i| std::array::from_fn(|j| vecs[i * 3 + j]));
    ([vals[0], vals[1], vals[2]], v)
}

/// Symmetric n×n Jacobi eigen decomposition: (values, vectors as columns, row-major).
fn jacobi(a: &[f64], n: usize) -> (Vec<f64>, Vec<f64>) {
    let mut a = a.to_vec();
    let mut v = vec![0.; n * n];
    for i in 0..n {
        v[i * n + i] = 1.;
    }
    for _ in 0..100 {
        let mut off = 0.;
        for p in 0..n {
            for q in p + 1..n {
                off += a[p * n + q].abs();
            }
        }
        if off < 1e-14 {
            break;
        }
        for p in 0..n {
            for q in p + 1..n {
                let apq = a[p * n + q];
                if apq.abs() < 1e-300 {
                    continue;
                }
                let theta = (a[q * n + q] - a[p * n + p]) / (2. * apq);
                let t = if theta == 0. { 1. } else { theta.signum() / (theta.abs() + (theta * theta + 1.).sqrt()) };
                let c = 1. / (t * t + 1.).sqrt();
                let s = t * c;
                for k in 0..n {
                    let (akp, akq) = (a[k * n + p], a[k * n + q]);
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for k in 0..n {
                    let (apk, aqk) = (a[p * n + k], a[q * n + k]);
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                for k in 0..n {
                    let (vkp, vkq) = (v[k * n + p], v[k * n + q]);
                    v[k * n + p] = c * vkp - s * vkq;
                    v[k * n + q] = s * vkp + c * vkq;
                }
            }
        }
    }
    ((0..n).map(|i| a[i * n + i]).collect(), v)
}

/// Essential matrix from ≥ 8 normalised correspondences (x1ᵀ E x0 = 0), rank 2 enforced.
fn essential_8pt(x0: &[V3], x1: &[V3], idx: &[usize]) -> Option<M3> {
    let mut ata = vec![0.; 81];
    for &i in idx {
        let (a, b) = (x0[i], x1[i]);
        let row = [b[0] * a[0], b[0] * a[1], b[0], b[1] * a[0], b[1] * a[1], b[1], a[0], a[1], 1.];
        for r in 0..9 {
            for c in 0..9 {
                ata[r * 9 + c] += row[r] * row[c];
            }
        }
    }
    let (vals, vecs) = jacobi(&ata, 9);
    let k = (0..9).min_by(|&a, &b| vals[a].partial_cmp(&vals[b]).unwrap())?;
    let e: M3 = std::array::from_fn(|r| std::array::from_fn(|c| vecs[(r * 3 + c) * 9 + k]));
    // rank 2 with equal singular values: E = U diag(1,1,0) Vᵀ
    let (u, v) = svd3(&e);
    Some(mul(&mul(&u, &[[1., 0., 0.], [0., 1., 0.], [0., 0., 0.]]), &tr(&v)))
}

/// SVD of a 3×3 (U, V with det +1 not enforced), singular values descending.
fn svd3(m: &M3) -> (M3, M3) {
    let (vals, vecs) = eig3(&mul(&tr(m), m));
    let mut order = [0usize, 1, 2];
    order.sort_by(|&a, &b| vals[b].partial_cmp(&vals[a]).unwrap());
    let v: Vec<V3> = order.iter().map(|&i| col(&vecs, i)).collect();
    let mut u: Vec<V3> = v.iter().map(|x| normalize(mv(m, *x))).collect();
    u[2] = cross(u[0], u[1]);
    let um: M3 = std::array::from_fn(|i| [u[0][i], u[1][i], u[2][i]]);
    let vm: M3 = std::array::from_fn(|i| [v[0][i], v[1][i], v[2][i]]);
    (um, vm)
}

fn sampson(e: &M3, a: V3, b: V3) -> f64 {
    let ea = mv(e, a);
    let etb = mv(&tr(e), b);
    let num = dot(b, ea).powi(2);
    num / (ea[0] * ea[0] + ea[1] * ea[1] + etb[0] * etb[0] + etb[1] * etb[1]).max(1e-30)
}

/// Rotation of the four E decompositions with the most points in front of both cameras.
fn rotation_from_essential(e: &M3, x0: &[V3], x1: &[V3], inl: &[usize]) -> M3 {
    let (mut u, mut v) = svd3(e);
    let det = |a: &M3| dot(a[0], cross(a[1], a[2]));
    if det(&u) < 0. {
        for r in 0..3 {
            u[r][2] = -u[r][2];
        }
    }
    if det(&v) < 0. {
        for r in 0..3 {
            v[r][2] = -v[r][2];
        }
    }
    let w = [[0., -1., 0.], [1., 0., 0.], [0., 0., 1.]];
    let r1 = mul(&mul(&u, &w), &tr(&v));
    let r2 = mul(&mul(&u, &tr(&w)), &tr(&v));
    let t = col(&u, 2);
    let mut best = (0usize, I3);
    for r in [r1, r2] {
        for s in [1., -1.] {
            let tt = scale(t, s);
            // depth of each point: solve λ1 x1 = λ0 R x0 + t (least squares on the two rays)
            let front = inl
                .iter()
                .filter(|&&i| {
                    let a = mv(&r, x0[i]);
                    let b = x1[i];
                    let ab = dot(a, b);
                    let den = dot(a, a) * dot(b, b) - ab * ab;
                    if den.abs() < 1e-12 {
                        return false;
                    }
                    let l0 = (ab * dot(b, tt) - dot(b, b) * dot(a, tt)) / den;
                    let l1 = (dot(a, a) * dot(b, tt) - ab * dot(a, tt)) / den;
                    l0 > 0. && l1 > 0.
                })
                .count();
            if front > best.0 {
                best = (front, r);
            }
        }
    }
    best.1
}

/// Relative rotation cam1 → cam2 (X2 = R·X1) from matched pixels, or None when too few.
pub fn relative_rotation(p0: &[[f32; 2]], p1: &[[f32; 2]], f: f64, cx: f64, cy: f64, seed: u64) -> Option<(M3, usize)> {
    let n = p0.len();
    if n < 10 {
        return None;
    }
    let ray = |p: [f32; 2]| [(p[0] as f64 - cx) / f, (p[1] as f64 - cy) / f, 1.];
    let x0: Vec<V3> = p0.iter().map(|p| ray(*p)).collect();
    let x1: Vec<V3> = p1.iter().map(|p| ray(*p)).collect();
    let b0: Vec<V3> = x0.iter().map(|v| normalize(*v)).collect();
    let b1: Vec<V3> = x1.iter().map(|v| normalize(*v)).collect();
    let thr = 2. / f; // ~2 px
    let mut rng = Rng(seed.max(1) * 0x9E3779B97F4A7C15);
    // rotation-only model
    let mut best_r = (Vec::new(), I3);
    for _ in 0..300 {
        let (i, j) = (rng.next(n), rng.next(n));
        if i == j {
            continue;
        }
        let r = kabsch_bearings(&b0, &b1, &[i, j]);
        let inl: Vec<usize> = (0..n).filter(|&k| norm(sub(mv(&r, b0[k]), b1[k])) < thr).collect();
        if inl.len() > best_r.0.len() {
            best_r = (inl, r);
        }
    }
    let rot_inl = best_r.0.clone();
    let r_only = if rot_inl.len() >= 3 { kabsch_bearings(&b0, &b1, &rot_inl) } else { I3 };
    // essential model
    let mut best_e: (Vec<usize>, M3) = (Vec::new(), I3);
    for _ in 0..300 {
        let mut s: Vec<usize> = Vec::with_capacity(8);
        while s.len() < 8 {
            let k = rng.next(n);
            if !s.contains(&k) {
                s.push(k);
            }
        }
        let Some(e) = essential_8pt(&x0, &x1, &s) else { continue };
        let inl: Vec<usize> = (0..n).filter(|&k| sampson(&e, x0[k], x1[k]) < thr * thr).collect();
        if inl.len() > best_e.0.len() {
            best_e = (inl, e);
        }
    }
    // Translation evident: the essential model explains clearly more matches than a pure rotation.
    if best_e.0.len() >= 8 && best_e.0.len() as f64 > 1.15 * rot_inl.len() as f64 {
        let e = essential_8pt(&x0, &x1, &best_e.0).unwrap_or(best_e.1);
        return Some((rotation_from_essential(&e, &x0, &x1, &best_e.0), best_e.0.len()));
    }
    if rot_inl.len() < 10 {
        return None;
    }
    Some((r_only, rot_inl.len()))
}

/// SimpleVO's quaternion slerp (wxyz): flip on negative dot, lerp when nearly parallel.
fn slerp(a: Q, b: Q, t: f64) -> Q {
    let mut b = b;
    let mut d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if d < 0. {
        b = [-b[0], -b[1], -b[2], -b[3]];
        d = -d;
    }
    if d > 0.9995 {
        return qnormalize(std::array::from_fn(|k| a[k] + t * (b[k] - a[k])));
    }
    let th0 = d.acos();
    let s0 = (t * th0).cos() - d * (t * th0).sin() / th0.sin();
    let s1 = (t * th0).sin() / th0.sin();
    std::array::from_fn(|k| s0 * a[k] + s1 * b[k])
}

/// Per-frame R_w2c from sampled relative rotations (`deltas[k]` maps sample k-1 → k).
pub fn interpolate(samples: &[usize], deltas: &[M3], frames: usize) -> Vec<M3> {
    let mut at = vec![I3; samples.len()];
    for k in 1..samples.len() {
        at[k] = mul(&deltas[k], &at[k - 1]);
    }
    let mut out = vec![I3; frames];
    for k in 0..samples.len() {
        out[samples[k]] = at[k];
        if k + 1 < samples.len() {
            let (s0, s1) = (samples[k], samples[k + 1]);
            let (q0, q1) = (matrix_to_quat(&at[k]), matrix_to_quat(&at[k + 1]));
            for f in s0 + 1..s1 {
                out[f] = quat_to_matrix(slerp(q0, q1, (f - s0) as f64 / (s1 - s0) as f64));
            }
        }
    }
    out
}

/// `compute_cam_angvel`: 6D (first two rows) of R[t+1]·R[t]ᵀ, last frame repeated.
pub fn cam_angvel(r: &[M3]) -> Vec<[f64; 6]> {
    let n = r.len();
    let mut out: Vec<[f64; 6]> = (0..n.saturating_sub(1)).map(|t| matrix_to_r6d(&mul(&r[t + 1], &tr(&r[t])))).collect();
    let last = out.last().copied().unwrap_or([1., 0., 0., 0., 1., 0.]);
    out.push(last);
    out
}
