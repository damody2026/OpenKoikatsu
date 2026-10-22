//! Unreal's frame (cm; X forward, Y right, Z up; quaternions as FQuat x, y, z, w) so poses pass through unchanged.
use serde::{Deserialize, Serialize};

pub type V3 = [f64; 3];

pub fn add(a: V3, b: V3) -> V3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}
pub fn sub(a: V3, b: V3) -> V3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}
pub fn scale(a: V3, s: f64) -> V3 {
    [a[0] * s, a[1] * s, a[2] * s]
}
pub fn dot(a: V3, b: V3) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}
pub fn cross(a: V3, b: V3) -> V3 {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}
pub fn len(a: V3) -> f64 {
    dot(a, a).sqrt()
}
pub fn normalize(a: V3) -> V3 {
    let l = len(a);
    if l < 1e-12 {
        [1.0, 0.0, 0.0]
    } else {
        scale(a, 1.0 / l)
    }
}
pub fn lerp3(a: V3, b: V3, t: f64) -> V3 {
    add(a, scale(sub(b, a), t))
}
pub fn lerp(a: f64, b: f64, t: f64) -> f64 {
    a + (b - a) * t
}
pub fn smoothstep(t: f64) -> f64 {
    let t = t.clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}
pub fn finite3(a: V3) -> bool {
    a.iter().all(|v| v.is_finite())
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Quat {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub w: f64,
}

impl Default for Quat {
    fn default() -> Self {
        Quat::IDENTITY
    }
}

impl Quat {
    pub const IDENTITY: Quat = Quat { x: 0.0, y: 0.0, z: 0.0, w: 1.0 };

    pub fn from_array(a: [f64; 4]) -> Quat {
        Quat { x: a[0], y: a[1], z: a[2], w: a[3] }.normalized()
    }
    pub fn to_array(self) -> [f64; 4] {
        [self.x, self.y, self.z, self.w]
    }
    pub fn normalized(self) -> Quat {
        let l = (self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w).sqrt();
        if l.is_nan() || l <= 1e-12 || !l.is_finite() {
            return Quat::IDENTITY;
        }
        Quat { x: self.x / l, y: self.y / l, z: self.z / l, w: self.w / l }
    }
    pub fn axis_angle(axis: V3, radians: f64) -> Quat {
        let a = normalize(axis);
        let (s, c) = (radians * 0.5).sin_cos();
        Quat { x: a[0] * s, y: a[1] * s, z: a[2] * s, w: c }
    }
    /// About +Z (Unreal yaw), degrees.
    pub fn yaw_deg(deg: f64) -> Quat {
        Quat::axis_angle([0.0, 0.0, 1.0], deg.to_radians())
    }
    /// FRotator(pitch, yaw, roll) in degrees, as FRotator::Quaternion builds it.
    pub fn from_rotator(pitch: f64, yaw: f64, roll: f64) -> Quat {
        let (sp, cp) = (pitch.to_radians() * 0.5).sin_cos();
        let (sy, cy) = (yaw.to_radians() * 0.5).sin_cos();
        let (sr, cr) = (roll.to_radians() * 0.5).sin_cos();
        Quat {
            x: cr * sp * sy - sr * cp * cy,
            y: -cr * sp * cy - sr * cp * sy,
            z: cr * cp * sy - sr * sp * cy,
            w: cr * cp * cy + sr * sp * sy,
        }
    }
    /// The rotation whose X axis points along `forward` with Z as near `up` as it can be (a camera looking there).
    pub fn look(forward: V3, up: V3) -> Quat {
        let f = normalize(forward);
        let mut r = cross(up, f);
        if len(r) < 1e-6 {
            r = cross([0.0, 1.0, 0.0], f);
        }
        let r = normalize(r);
        let u = cross(f, r);
        // Columns X = f, Y = r, Z = u.
        let (m00, m01, m02) = (f[0], r[0], u[0]);
        let (m10, m11, m12) = (f[1], r[1], u[1]);
        let (m20, m21, m22) = (f[2], r[2], u[2]);
        let trace = m00 + m11 + m22;
        let q = if trace > 0.0 {
            let s = (trace + 1.0).sqrt() * 2.0;
            Quat { w: 0.25 * s, x: (m21 - m12) / s, y: (m02 - m20) / s, z: (m10 - m01) / s }
        } else if m00 > m11 && m00 > m22 {
            let s = (1.0 + m00 - m11 - m22).sqrt() * 2.0;
            Quat { w: (m21 - m12) / s, x: 0.25 * s, y: (m01 + m10) / s, z: (m02 + m20) / s }
        } else if m11 > m22 {
            let s = (1.0 + m11 - m00 - m22).sqrt() * 2.0;
            Quat { w: (m02 - m20) / s, x: (m01 + m10) / s, y: 0.25 * s, z: (m12 + m21) / s }
        } else {
            let s = (1.0 + m22 - m00 - m11).sqrt() * 2.0;
            Quat { w: (m10 - m01) / s, x: (m02 + m20) / s, y: (m12 + m21) / s, z: 0.25 * s }
        };
        q.normalized()
    }
    /// self × b (b applied first).
    pub fn times(self, b: Quat) -> Quat {
        let a = self;
        Quat {
            w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        }
    }
    pub fn inverse(self) -> Quat {
        Quat { x: -self.x, y: -self.y, z: -self.z, w: self.w }
    }
    pub fn rotate(self, v: V3) -> V3 {
        let q = [self.x, self.y, self.z];
        let t = scale(cross(q, v), 2.0);
        add(add(v, scale(t, self.w)), cross(q, t))
    }
    pub fn forward(self) -> V3 {
        self.rotate([1.0, 0.0, 0.0])
    }
    pub fn dot(self, b: Quat) -> f64 {
        self.x * b.x + self.y * b.y + self.z * b.z + self.w * b.w
    }
    /// Angle between two orientations, radians (0..π).
    pub fn angle_to(self, b: Quat) -> f64 {
        2.0 * self.dot(b).abs().min(1.0).acos()
    }
    pub fn slerp(self, b: Quat, t: f64) -> Quat {
        let mut d = self.dot(b);
        let mut b = b;
        if d < 0.0 {
            d = -d;
            b = Quat { x: -b.x, y: -b.y, z: -b.z, w: -b.w };
        }
        if d > 0.9995 {
            return Quat {
                x: lerp(self.x, b.x, t),
                y: lerp(self.y, b.y, t),
                z: lerp(self.z, b.z, t),
                w: lerp(self.w, b.w, t),
            }
            .normalized();
        }
        let th = d.acos();
        let s = th.sin();
        let (wa, wb) = (((1.0 - t) * th).sin() / s, (t * th).sin() / s);
        Quat { x: self.x * wa + b.x * wb, y: self.y * wa + b.y * wb, z: self.z * wa + b.z * wb, w: self.w * wa + b.w * wb }
    }
    /// Heading about Z (degrees) of the rotation's `axis` direction projected on the ground, if it has one.
    pub fn heading_of(self, axis: V3) -> Option<f64> {
        let f = self.rotate(axis);
        if f[0] * f[0] + f[1] * f[1] < 1e-8 {
            None
        } else {
            Some(f[1].atan2(f[0]).to_degrees())
        }
    }
}

/// A camera pose: position (cm), rotation, horizontal field of view (degrees).
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Pose {
    pub pos: V3,
    pub rot: Quat,
    pub fov: f64,
}

impl Default for Pose {
    fn default() -> Self {
        Pose { pos: [0.0, -300.0, 150.0], rot: Quat::yaw_deg(90.0), fov: 40.0 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(a: V3, b: V3) -> bool {
        (0..3).all(|i| (a[i] - b[i]).abs() < 1e-6)
    }

    #[test]
    fn look_points_x_along_forward() {
        let q = Quat::look([0.0, 1.0, 0.0], [0.0, 0.0, 1.0]);
        assert!(close(q.forward(), [0.0, 1.0, 0.0]));
        assert!(close(q.rotate([0.0, 0.0, 1.0]), [0.0, 0.0, 1.0]));
        let q = Quat::look([1.0, 1.0, -1.0], [0.0, 0.0, 1.0]);
        assert!(close(q.forward(), normalize([1.0, 1.0, -1.0])));
    }

    #[test]
    fn rotator_matches_yaw() {
        let a = Quat::from_rotator(0.0, 90.0, 0.0);
        assert!(close(a.forward(), [0.0, 1.0, 0.0]));
        assert!(a.angle_to(Quat::yaw_deg(90.0)) < 1e-9);
        let p = Quat::from_rotator(-30.0, 0.0, 0.0);
        assert!(p.forward()[2] < -0.49);
    }

    #[test]
    fn slerp_ends_and_middle() {
        let a = Quat::IDENTITY;
        let b = Quat::yaw_deg(90.0);
        assert!(a.slerp(b, 0.0).angle_to(a) < 1e-9);
        assert!(a.slerp(b, 1.0).angle_to(b) < 1e-9);
        assert!((a.slerp(b, 0.5).angle_to(Quat::yaw_deg(45.0))) < 1e-9);
    }
}
