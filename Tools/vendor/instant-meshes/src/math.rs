//! Vectors, bounding boxes, and the small numeric helpers used by the solver.

use std::ops::{Add, AddAssign, Div, Mul, Neg, Sub, SubAssign};

pub const INVALID: u32 = u32::MAX;
pub const RCPOVERFLOW: f32 = 2.93873587705571876e-39;

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0 };
    pub const X: Self = Self { x: 1.0, y: 0.0, z: 0.0 };

    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    pub fn dot(self, o: Self) -> f32 {
        self.x * o.x + self.y * o.y + self.z * o.z
    }

    pub fn cross(self, o: Self) -> Self {
        Self::new(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )
    }

    pub fn squared_norm(self) -> f32 {
        self.dot(self)
    }

    pub fn norm(self) -> f32 {
        self.squared_norm().sqrt()
    }

    pub fn normalized(self) -> Self {
        let n = self.norm();
        if n > RCPOVERFLOW {
            self / n
        } else {
            Self::ZERO
        }
    }

    pub fn min(self, o: Self) -> Self {
        Self::new(self.x.min(o.x), self.y.min(o.y), self.z.min(o.z))
    }

    pub fn max(self, o: Self) -> Self {
        Self::new(self.x.max(o.x), self.y.max(o.y), self.z.max(o.z))
    }

    pub fn abs(self) -> Self {
        Self::new(self.x.abs(), self.y.abs(), self.z.abs())
    }

    pub fn max_component(self) -> f32 {
        self.x.max(self.y).max(self.z)
    }

    pub fn comp(self, i: usize) -> f32 {
        match i {
            0 => self.x,
            1 => self.y,
            _ => self.z,
        }
    }

    pub fn set_comp(&mut self, i: usize, v: f32) {
        match i {
            0 => self.x = v,
            1 => self.y = v,
            _ => self.z = v,
        }
    }

    pub fn is_finite(self) -> bool {
        self.x.is_finite() && self.y.is_finite() && self.z.is_finite()
    }
}

impl Add for Vec3 {
    type Output = Self;
    fn add(self, o: Self) -> Self {
        Self::new(self.x + o.x, self.y + o.y, self.z + o.z)
    }
}
impl AddAssign for Vec3 {
    fn add_assign(&mut self, o: Self) {
        *self = *self + o;
    }
}
impl Sub for Vec3 {
    type Output = Self;
    fn sub(self, o: Self) -> Self {
        Self::new(self.x - o.x, self.y - o.y, self.z - o.z)
    }
}
impl SubAssign for Vec3 {
    fn sub_assign(&mut self, o: Self) {
        *self = *self - o;
    }
}
impl Neg for Vec3 {
    type Output = Self;
    fn neg(self) -> Self {
        Self::new(-self.x, -self.y, -self.z)
    }
}
impl Mul<f32> for Vec3 {
    type Output = Self;
    fn mul(self, s: f32) -> Self {
        Self::new(self.x * s, self.y * s, self.z * s)
    }
}
impl Div<f32> for Vec3 {
    type Output = Self;
    fn div(self, s: f32) -> Self {
        Self::new(self.x / s, self.y / s, self.z / s)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct Vec2i {
    pub x: i32,
    pub y: i32,
}

impl Vec2i {
    pub const ZERO: Self = Self { x: 0, y: 0 };
    pub const fn new(x: i32, y: i32) -> Self {
        Self { x, y }
    }
    pub fn abs(self) -> Self {
        Self::new(self.x.abs(), self.y.abs())
    }
    pub fn max_comp(self) -> i32 {
        self.x.abs().max(self.y.abs())
    }
    pub fn sum_abs(self) -> i32 {
        self.x.abs() + self.y.abs()
    }
}
impl Add for Vec2i {
    type Output = Self;
    fn add(self, o: Self) -> Self {
        Self::new(self.x + o.x, self.y + o.y)
    }
}
impl Sub for Vec2i {
    type Output = Self;
    fn sub(self, o: Self) -> Self {
        Self::new(self.x - o.x, self.y - o.y)
    }
}
impl Neg for Vec2i {
    type Output = Self;
    fn neg(self) -> Self {
        Self::new(-self.x, -self.y)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Ray {
    pub o: Vec3,
    pub d: Vec3,
    pub mint: f32,
    pub maxt: f32,
}

impl Ray {
    pub fn new(o: Vec3, d: Vec3, mint: f32, maxt: f32) -> Self {
        Self { o, d, mint, maxt }
    }
    pub fn at(self, t: f32) -> Vec3 {
        self.o + self.d * t
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Aabb {
    pub min: Vec3,
    pub max: Vec3,
}

impl Aabb {
    pub fn empty() -> Self {
        Self {
            min: Vec3::new(f32::INFINITY, f32::INFINITY, f32::INFINITY),
            max: Vec3::new(f32::NEG_INFINITY, f32::NEG_INFINITY, f32::NEG_INFINITY),
        }
    }

    pub fn expand(&mut self, p: Vec3) {
        self.min = self.min.min(p);
        self.max = self.max.max(p);
    }

    pub fn merge(self, o: Self) -> Self {
        Self {
            min: self.min.min(o.min),
            max: self.max.max(o.max),
        }
    }

    pub fn largest_axis(self) -> usize {
        let e = self.max - self.min;
        if e.x >= e.y && e.x >= e.z {
            0
        } else if e.y >= e.z {
            1
        } else {
            2
        }
    }

    pub fn surface_area(self) -> f32 {
        let d = self.max - self.min;
        2.0 * (d.x * d.y + d.x * d.z + d.y * d.z)
    }

    pub fn ray_intersect(self, ray: &Ray) -> bool {
        let mut near_t = f32::NEG_INFINITY;
        let mut far_t = f32::INFINITY;
        for i in 0..3 {
            let origin = ray.o.comp(i);
            let min_v = self.min.comp(i);
            let max_v = self.max.comp(i);
            if ray.d.comp(i) == 0.0 {
                if origin < min_v || origin > max_v {
                    return false;
                }
            } else {
                let mut t1 = (min_v - origin) / ray.d.comp(i);
                let mut t2 = (max_v - origin) / ray.d.comp(i);
                if t1 > t2 {
                    std::mem::swap(&mut t1, &mut t2);
                }
                near_t = t1.max(near_t);
                far_t = t2.min(far_t);
                if !(near_t <= far_t) {
                    return false;
                }
            }
        }
        ray.mint <= far_t && near_t <= ray.maxt
    }

    pub fn squared_distance(self, p: Vec3) -> f32 {
        let mut result = 0.0;
        for i in 0..3 {
            let v = p.comp(i);
            let value = if v < self.min.comp(i) {
                self.min.comp(i) - v
            } else if v > self.max.comp(i) {
                v - self.max.comp(i)
            } else {
                0.0
            };
            result += value * value;
        }
        result
    }
}

impl Default for Aabb {
    fn default() -> Self {
        Self::empty()
    }
}

pub fn coordinate_system(a: Vec3) -> (Vec3, Vec3) {
    let c = if a.x.abs() > a.y.abs() {
        let inv = 1.0 / (a.x * a.x + a.z * a.z).sqrt();
        Vec3::new(a.z * inv, 0.0, -a.x * inv)
    } else {
        let inv = 1.0 / (a.y * a.y + a.z * a.z).sqrt();
        Vec3::new(0.0, a.z * inv, -a.y * inv)
    };
    let b = c.cross(a);
    (b, c)
}

pub fn signum(v: f32) -> f32 {
    if v < 0.0 || (v == 0.0 && v.is_sign_negative()) {
        -1.0
    } else {
        1.0
    }
}

pub fn modulo(a: i32, b: i32) -> i32 {
    let mut r = a % b;
    if r < 0 {
        r += b;
    }
    r
}

pub fn fast_acos(mut x: f32) -> f32 {
    let negate = if x < 0.0 { 1.0 } else { 0.0 };
    x = x.abs();
    let mut ret = -0.0187293_f32;
    ret = ret * x + 0.0742610;
    ret = ret * x - 0.2121144;
    ret = ret * x + 1.5707288;
    ret *= (1.0 - x).sqrt();
    ret -= 2.0 * negate * ret;
    negate * std::f32::consts::PI + ret
}

/// Eigenvector belonging to the smallest eigenvalue of a symmetric 3×3 matrix.
/// `a` is stored row-major and must be symmetric. Used as a stand-in for
/// Eigen's `jacobiSvd` on a covariance (smallest singular vector).
pub fn smallest_eigenvector_sym(mut a: [[f32; 3]; 3]) -> Vec3 {
    let mut v = [[1.0_f32, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];
    for _ in 0..24 {
        let mut p = 0;
        let mut q = 1;
        let mut max = a[0][1].abs();
        if a[0][2].abs() > max {
            max = a[0][2].abs();
            p = 0;
            q = 2;
        }
        if a[1][2].abs() > max {
            max = a[1][2].abs();
            p = 1;
            q = 2;
        }
        if max < 1e-12 {
            break;
        }
        let app = a[p][p];
        let aqq = a[q][q];
        let apq = a[p][q];
        let tau = (aqq - app) / (2.0 * apq);
        let t = if tau >= 0.0 {
            1.0 / (tau + (1.0 + tau * tau).sqrt())
        } else {
            -1.0 / (-tau + (1.0 + tau * tau).sqrt())
        };
        let c = 1.0 / (1.0 + t * t).sqrt();
        let s = t * c;
        a[p][p] = app - t * apq;
        a[q][q] = aqq + t * apq;
        a[p][q] = 0.0;
        a[q][p] = 0.0;
        for r in 0..3 {
            if r == p || r == q {
                continue;
            }
            let arp = a[r][p];
            let arq = a[r][q];
            a[r][p] = c * arp - s * arq;
            a[p][r] = a[r][p];
            a[r][q] = s * arp + c * arq;
            a[q][r] = a[r][q];
        }
        for r in 0..3 {
            let vip = v[r][p];
            let viq = v[r][q];
            v[r][p] = c * vip - s * viq;
            v[r][q] = s * vip + c * viq;
        }
    }
    let mut k = 0;
    if a[1][1] < a[k][k] {
        k = 1;
    }
    if a[2][2] < a[k][k] {
        k = 2;
    }
    let n = Vec3::new(v[0][k], v[1][k], v[2][k]);
    let nn = n.normalized();
    if nn.squared_norm() > 0.0 {
        nn
    } else {
        Vec3::X
    }
}

/// PCG32 with the same seeding as Wenzel Jakob's `pcg32.h`.
#[derive(Clone)]
pub struct Pcg32 {
    state: u64,
    inc: u64,
}

impl Pcg32 {
    pub fn new() -> Self {
        Self::seed(0x853c49e6748fea9b, 0xda3e39cb94b95bdb)
    }

    pub fn seed(initstate: u64, initseq: u64) -> Self {
        let mut g = Self {
            state: 0,
            inc: (initseq << 1) | 1,
        };
        g.next_u32();
        g.state = g.state.wrapping_add(initstate);
        g.next_u32();
        g
    }

    pub fn next_u32(&mut self) -> u32 {
        let old = self.state;
        self.state = old
            .wrapping_mul(6364136223846793005)
            .wrapping_add(self.inc);
        let xorshifted = (((old >> 18) ^ old) >> 27) as u32;
        let rot = (old >> 59) as u32;
        xorshifted.rotate_right(rot)
    }

    pub fn next_f32(&mut self) -> f32 {
        let u = (self.next_u32() >> 9) | 0x3f80_0000;
        f32::from_bits(u) - 1.0
    }

    pub fn next_bounded(&mut self, bound: u32) -> u32 {
        if bound == 0 {
            return 0;
        }
        let mut x = self.next_u32();
        let mut m = (x as u64).wrapping_mul(bound as u64);
        let mut lo = m as u32;
        if lo < bound {
            let thresh = bound.wrapping_neg() % bound;
            while lo < thresh {
                x = self.next_u32();
                m = (x as u64).wrapping_mul(bound as u64);
                lo = m as u32;
            }
        }
        (m >> 32) as u32
    }

    pub fn shuffle<T>(&mut self, data: &mut [T]) {
        if data.len() < 2 {
            return;
        }
        for i in (1..data.len()).rev() {
            let j = self.next_bounded(i as u32 + 1) as usize;
            data.swap(i, j);
        }
    }
}

impl Default for Pcg32 {
    fn default() -> Self {
        Self::new()
    }
}

pub fn time_string(mut time_ms: f64) -> String {
    if !time_ms.is_finite() {
        return "inf".to_string();
    }
    let mut suffix = "ms";
    if time_ms > 1000.0 {
        time_ms /= 1000.0;
        suffix = "s";
        if time_ms > 60.0 {
            time_ms /= 60.0;
            suffix = "m";
        }
    }
    format!("{time_ms:.1}{suffix}")
}
