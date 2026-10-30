//! Faithful Rust port of RootMotion.FinalIK's `FullBodyBipedIK` stack, used by
//! the Koikatsu maker pose (`MotionIK` + `FullBodyBipedIK`).
//!
//! Source of truth (decompiled, no guessing):
//!   `kks_decomp\Managed\KoikatsuSunshine_Data\Assembly-CSharp-firstpass\RootMotion.FinalIK\*.cs`
//!
//! Port order (incremental; each step verified against a mesh_dump):
//!   1. `IKSolverTrigonometric` (this file)  + `QuaTools` helpers.
//!   2. `IKSolverLimb` (bend goal / rotation offsets / twist).
//!   3. `IKConstraintBend`.
//!   4. `IKEffector`.
//!   5. `FBIKChain`, `IKMapping{Spine,Limb,Bone}`, `IKSolverFullBody`,
//!      `IKSolverFullBodyBiped`, `FullBodyBipedIK`.
//!   6. `MotionIK` binding (effector targets = `cf_pv_*` from `custom_pose`).
//!
//! Unity conventions are preserved exactly (left-handed, quaternion xyzw,
//! `Quaternion.LookRotation`/`FromToRotation`/`Lerp`/`Slerp`).

#![allow(dead_code)]

/// Unity `Vector3` (f32).
pub type V3 = [f32; 3];
/// Unity `Quaternion` as (x, y, z, w).
pub type Quat = [f32; 4];

pub const IDENTITY: Quat = [0.0, 0.0, 0.0, 1.0];

#[inline]
pub fn vsub(a: V3, b: V3) -> V3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}
#[inline]
pub fn vadd(a: V3, b: V3) -> V3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}
#[inline]
pub fn vmul(a: V3, s: f32) -> V3 {
    [a[0] * s, a[1] * s, a[2] * s]
}
#[inline]
fn vdiv(a:V3,s:f32)->V3{[a[0]/s,a[1]/s,a[2]/s]}
#[inline]
pub fn vdot(a: V3, b: V3) -> f32 {
    ((a[0] as f64)*(b[0] as f64)+(a[1] as f64)*(b[1] as f64)+(a[2] as f64)*(b[2] as f64))as f32
}
#[inline]
pub fn vcross(a: V3, b: V3) -> V3 {
    [
        ((a[1]as f64)*(b[2]as f64)-(a[2]as f64)*(b[1]as f64))as f32,
        ((a[2]as f64)*(b[0]as f64)-(a[0]as f64)*(b[2]as f64))as f32,
        ((a[0]as f64)*(b[1]as f64)-(a[1]as f64)*(b[0]as f64))as f32,
    ]
}
#[inline]
pub fn vlen(a: V3) -> f32 {
    ((a[0]as f64)*(a[0]as f64)+(a[1]as f64)*(a[1]as f64)+(a[2]as f64)*(a[2]as f64)).sqrt()as f32
}
#[inline]
pub fn vnorm(a: V3) -> V3 {
    let l = vlen(a);
    if l > 1e-9 {
        [a[0] / l, a[1] / l, a[2] / l]
    } else {
        [0.0, 0.0, 0.0]
    }
}
#[inline]
pub fn vlerp(a: V3, b: V3, t: f32) -> V3 {
    let t=t.clamp(0.,1.)as f64;
    std::array::from_fn(|i|(a[i]as f64+((b[i]as f64)-(a[i]as f64))*t)as f32)
}
// Managed Mono expressions retain precision until float stores/call boundaries.
// Native C++ icalls use float arithmetic: keep their normalization separate.
fn cpp_vdot(a:V3,b:V3)->f32{a[0]*b[0]+a[1]*b[1]+a[2]*b[2]}
fn cpp_vlen(a:V3)->f32{cpp_vdot(a,a).sqrt()}
fn cpp_vnorm(a:V3)->V3{let m=cpp_vlen(a);if m>1e-9{vdiv(a,m)}else{[0.;3]}}
fn cpp_vcross(a:V3,b:V3)->V3{[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]}
#[inline]
pub fn vis_zero(a: V3) -> bool {
    a[0] == 0.0 && a[1] == 0.0 && a[2] == 0.0
}

#[inline]
pub fn qmul(a: Quat, b: Quat) -> Quat {
    let a=a.map(|v|v as f64);let b=b.map(|v|v as f64);
    [a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]+a[1]*b[3]+a[2]*b[0]-a[0]*b[2],a[3]*b[2]+a[2]*b[3]+a[0]*b[1]-a[1]*b[0],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]].map(|v|v as f32)
}
fn cpp_qmul(a:Quat,b:Quat)->Quat{
    [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] + a[1] * b[3] + a[2] * b[0] - a[0] * b[2],
        a[3] * b[2] + a[2] * b[3] + a[0] * b[1] - a[1] * b[0],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ]
}
#[inline]
pub fn qinv(a: Quat) -> Quat {
    // Compiled Unity Quaternion.Inverse assumes a unit quaternion and uses
    // its conjugate (the native primitive oracle also covers nonunit input).
    [-a[0],-a[1],-a[2],a[3]]
}
#[inline]
pub fn qdot(a: Quat, b: Quat) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
}
#[inline]
pub fn qnorm(a: Quat) -> Quat {
    let m = qdot(a, a).sqrt();
    if m > 1e-9 {
        [a[0] / m, a[1] / m, a[2] / m, a[3] / m]
    } else {
        IDENTITY
    }
}
/// Rotate a vector by a quaternion (Unity `q * v`).
pub fn qrot(q: Quat, v: V3) -> V3 {
    // Mono Quaternion.operator *(Quaternion, Vector3): float locals, then
    // extended component expressions rounded at the Vector3 constructor.
    let x=q[0]*2.;let y=q[1]*2.;let z=q[2]*2.;
    let xx=q[0]*x;let yy=q[1]*y;let zz=q[2]*z;
    let xy=q[0]*y;let xz=q[0]*z;let yz=q[1]*z;
    let wx=q[3]*x;let wy=q[3]*y;let wz=q[3]*z;
    let(xx,yy,zz,xy,xz,yz,wx,wy,wz)=(xx as f64,yy as f64,zz as f64,xy as f64,xz as f64,yz as f64,wx as f64,wy as f64,wz as f64);
    let v=v.map(|v|v as f64);
    [((1.-(yy+zz))*v[0]+(xy-wz)*v[1]+(xz+wy)*v[2])as f32,
     ((xy+wz)*v[0]+(1.-(xx+zz))*v[1]+(yz-wx)*v[2])as f32,
     ((xz-wy)*v[0]+(yz+wx)*v[1]+(1.-(xx+yy))*v[2])as f32]
}

fn cpp_qrot(q:Quat,v:V3)->V3{
    let m=crate::pose::trs([0.;3],q,[1.;3]);
    std::array::from_fn(|a|(m[a][0]*v[0]+m[a][1]*v[1])+m[a][2]*v[2])
}/// Unity `Quaternion.AngleAxis(angleDeg, axis)`.
pub fn qangle_axis(angle_deg: f32, axis: V3) -> Quat {
    let a = cpp_vnorm(axis);
    let half = angle_deg.to_radians() * 0.5;
    let s = half.sin();
    [a[0] * s, a[1] * s, a[2] * s, half.cos()]
}

/// Unity `Quaternion.LookRotation(forward, upwards)`.
pub fn qlook_rotation(forward: V3, upwards: V3) -> Quat {
    let f = cpp_vnorm(forward);
    if cpp_vlen(f) < 1e-9 {
        return IDENTITY;
    }
    let right=cpp_vcross(upwards,f);
    if cpp_vlen(right)<1e-5{return qfrom_to_rotation([0.,0.,1.],f);}
    let r = cpp_vnorm(right);
    let u = cpp_vcross(f, r);
    let (m00, m01, m02) = (r[0], u[0], f[0]);
    let (m10, m11, m12) = (r[1], u[1], f[1]);
    let (m20, m21, m22) = (r[2], u[2], f[2]);
    let num8 = m00 + m11 + m22;
    if num8 > 0.0 {
        let num = ((num8 + 1.0).sqrt()) * 2.0;
        [
            (m21 - m12) / num,
            (m02 - m20) / num,
            (m10 - m01) / num,
            0.25 * num,
        ]
    } else if m00 > m11 && m00 > m22 {
        let num = ((1.0 + m00 - m11 - m22).sqrt()) * 2.0;
        [
            0.25 * num,
            (m01 + m10) / num,
            (m02 + m20) / num,
            (m21 - m12) / num,
        ]
    } else if m11 > m22 {
        let num = ((1.0 + m11 - m00 - m22).sqrt()) * 2.0;
        [
            (m01 + m10) / num,
            0.25 * num,
            (m12 + m21) / num,
            (m02 - m20) / num,
        ]
    } else {
        let num = ((1.0 + m22 - m00 - m11).sqrt()) * 2.0;
        [
            (m02 + m20) / num,
            (m12 + m21) / num,
            0.25 * num,
            (m10 - m01) / num,
        ]
    }
}

/// Unity `Quaternion.FromToRotation(from, to)`.
pub fn qfrom_to_rotation(from: V3, to: V3) -> Quat {
    let a = cpp_vnorm(from);
    let b = cpp_vnorm(to);
    let d = cpp_vdot(a, b);
    // Compiled Unity FromToRotation returns identity for this parallel range.
    if d >= 1.0 - 1e-6 {
        return IDENTITY;
    }
    if d <= -1.0 + 1e-7 {
        // Opposite: pick an arbitrary orthogonal axis.
        let mut axis = cpp_vcross([0.0, 1.0, 0.0], a);
        if cpp_vlen(axis) < 1e-6 {
            axis = cpp_vcross([1.0, 0.0, 0.0], a);
        }
        return qangle_axis(180.0, axis);
    }
    let c = cpp_vcross(a, b);
    let q = [c[0], c[1], c[2], 1.0 + d];
    qnorm(q)
}

/// Unity `Quaternion.Lerp(a, b, t)` (shortest path).
pub fn qlerp(a: Quat, b: Quat, t: f32) -> Quat {
    let t=t.clamp(0.,1.);let sign=if qdot(a,b)<0.{-1.}else{1.};
    let u=1.-t;
    qnorm([a[0]*u+b[0]*sign*t,a[1]*u+b[1]*sign*t,a[2]*u+b[2]*sign*t,a[3]*u+b[3]*sign*t])
}

/// Unity `Quaternion.Slerp(a, b, t)` (shortest path).
pub fn qslerp(a: Quat, b: Quat, t: f32) -> Quat {
    let t=t.clamp(0.,1.);
    let mut bb = b;
    let mut dot = qdot(a, b);
    if dot < 0.0 {
        bb = [-b[0], -b[1], -b[2], -b[3]];
        dot = -dot;
    }
    if dot > 0.999999 {
        return qnorm([
            a[0] + (bb[0] - a[0]) * t,
            a[1] + (bb[1] - a[1]) * t,
            a[2] + (bb[2] - a[2]) * t,
            a[3] + (bb[3] - a[3]) * t,
        ]);
    }
    let angle = dot.clamp(-1.0, 1.0).acos();
    let inv = 1.0 / angle.sin();
    let ta = ((1.0 - t) * angle).sin() * inv;
    let tb = (t * angle).sin() * inv;
    qnorm([
        a[0] * ta + bb[0] * tb,
        a[1] * ta + bb[1] * tb,
        a[2] * ta + bb[2] * tb,
        a[3] * ta + bb[3] * tb,
    ])
}

/// `QuaTools.RotationToLocalSpace(space, rotation)`
/// = `Inverse(Inverse(space) * rotation)`.
pub fn rotation_to_local_space(space: Quat, rotation: Quat) -> Quat {
    qinv(qmul(qinv(space), rotation))
}

/// `IKSolver.Point` / `Bone` state used by the trigonometric solver.
#[derive(Clone, Copy, Debug)]
pub struct TrigBone {
    pub pos: V3,
    pub rot: Quat,
    pub sqr_mag: f32,
    target_to_local_space: Quat,
    default_local_bend_normal: V3,
}

impl Default for TrigBone {
    fn default() -> Self {
        Self {
            pos: [0.0; 3],
            rot: IDENTITY,
            sqr_mag: 0.0,
            target_to_local_space: IDENTITY,
            default_local_bend_normal: [0.0; 3],
        }
    }
}

impl TrigBone {
    /// `IKSolverTrigonometric.TrigonometricBone.Initiate`.
    pub fn initiate(&mut self, child_pos: V3, bend_normal: V3) {
        let rotation = qlook_rotation(vsub(child_pos, self.pos), bend_normal);
        self.target_to_local_space = rotation_to_local_space(self.rot, rotation);
        self.default_local_bend_normal = qrot(qinv(self.rot), bend_normal);
    }

    /// `ITrigonometricBone.GetRotation`.
    pub fn get_rotation(&self, direction: V3, bend_normal: V3) -> Quat {
        qmul(qlook_rotation(direction, bend_normal), self.target_to_local_space)
    }

    /// `ITrigonometricBone.GetBendNormalFromCurrentRotation`.
    pub fn get_bend_normal_from_current_rotation(&self) -> V3 {
        qrot(self.rot, self.default_local_bend_normal)
    }
}

/// Port of `IKSolverTrigonometric` (the 3-bone limb solve).
#[derive(Clone, Debug)]
pub struct IKSolverTrigonometric {
    pub ik_position: V3,
    pub ik_position_weight: f32,
    pub ik_rotation: Quat,
    pub ik_rotation_weight: f32,
    pub bend_normal: V3,
    pub bone1: TrigBone,
    pub bone2: TrigBone,
    pub bone3: TrigBone,
    pub initiated: bool,
    direct_hierarchy: bool,
    /// Bone names this solver drives (world transforms live in `IkSkeleton`).
    pub chain: [Option<String>; 3],
}

impl Default for IKSolverTrigonometric {
    fn default() -> Self {
        Self {
            ik_position: [0.0; 3],
            ik_position_weight: 1.0,
            ik_rotation: IDENTITY,
            ik_rotation_weight: 1.0,
            bend_normal: [1.0, 0.0, 0.0], // Vector3.right
            bone1: TrigBone::default(),
            bone2: TrigBone::default(),
            bone3: TrigBone::default(),
            initiated: false,
            direct_hierarchy: true,
            chain: [None, None, None],
        }
    }
}

impl IKSolverTrigonometric {
    /// `IKSolver.Initiate` + `OnInitiate` (bone positions/rotations are read
    /// from the caller's skeleton state; the caller keeps them in sync).
    pub fn initiate(&mut self) {
        if vis_zero(self.bend_normal) {
            self.bend_normal = [1.0, 0.0, 0.0];
        }
        self.ik_position = self.bone3.pos;
        self.ik_rotation = self.bone3.rot;
        self.bone1.initiate(self.bone2.pos, self.bend_normal);
        self.bone2.initiate(self.bone3.pos, self.bend_normal);
        self.set_bend_plane_to_current();
        self.initiated = true;
    }

    /// `IKSolverTrigonometric.SetBendPlaneToCurrent`.
    pub fn set_bend_plane_to_current(&mut self) {
        if self.initiated {
            let v = vcross(
                vsub(self.bone2.pos, self.bone1.pos),
                vsub(self.bone3.pos, self.bone2.pos),
            );
            if !vis_zero(v) {
                self.bend_normal = v;
            }
        }
    }

    /// `IKSolverTrigonometric.OnUpdate` (without the virtual overrides that
    /// `IKSolverLimb` adds -- those land when `IKSolverLimb` is ported).
    pub fn update(&mut self, target_pos: V3, target_rot: Quat) {
        if !self.initiated {
            self.initiate();
        }
        self.ik_position = target_pos;
        self.ik_rotation = target_rot;
        let w = self.ik_position_weight.clamp(0.0, 1.0);
        let rw = self.ik_rotation_weight.clamp(0.0, 1.0);
        if w > 0.0 {
            if !self.direct_hierarchy {
                self.bone1.initiate(self.bone2.pos, self.bend_normal);
                self.bone2.initiate(self.bone3.pos, self.bend_normal);
            }
            self.bone1.sqr_mag = vdot(
                vsub(self.bone2.pos, self.bone1.pos),
                vsub(self.bone2.pos, self.bone1.pos),
            );
            self.bone2.sqr_mag = vdot(
                vsub(self.bone3.pos, self.bone2.pos),
                vsub(self.bone3.pos, self.bone2.pos),
            );
            let weight_ik_position = vlerp(self.bone3.pos, self.ik_position, w);
            let bend = vlerp(
                self.bone1.get_bend_normal_from_current_rotation(),
                self.bend_normal,
                w,
            );
            let mut dir = vlerp(
                vsub(self.bone2.pos, self.bone1.pos),
                self.get_bend_direction(weight_ik_position, bend),
                w,
            );
            if vis_zero(dir) {
                dir = vsub(self.bone2.pos, self.bone1.pos);
            }
            self.bone1.rot = self.bone1.get_rotation(dir, bend);
            self.bone2.rot = self
                .bone2
                .get_rotation(vsub(weight_ik_position, self.bone2.pos), self.bone2.get_bend_normal_from_current_rotation());
        }
        if rw > 0.0 {
            self.bone3.rot = qslerp(self.bone3.rot, self.ik_rotation, rw);
        }
    }

    /// Refresh `bone1..bone3` world pos/rot from the skeleton.
    pub(crate) fn read_chain(&mut self, sk: &mut IkSkeleton) -> bool {
        let n0 = self.chain[0].clone();
        let n1 = self.chain[1].clone();
        let n2 = self.chain[2].clone();
        let (Some(n0), Some(n1), Some(n2)) = (n0, n1, n2) else {
            return false;
        };
        let (Some(p0), Some(r0)) = (sk.world_pos(&n0), sk.world_rot(&n0)) else {
            return false;
        };
        let (Some(p1), Some(r1)) = (sk.world_pos(&n1), sk.world_rot(&n1)) else {
            return false;
        };
        let (Some(p2), Some(r2)) = (sk.world_pos(&n2), sk.world_rot(&n2)) else {
            return false;
        };
        self.bone1.pos = p0;
        self.bone1.rot = r0;
        self.bone2.pos = p1;
        self.bone2.rot = r1;
        self.bone3.pos = p2;
        self.bone3.rot = r2;
        true
    }

    /// `IKSolver.Initiate` reading the chain from a live skeleton.
    pub fn initiate_sk(&mut self, sk: &mut IkSkeleton) {
        if !self.read_chain(sk) {
            return;
        }
        self.initiate();
    }

    /// `IKSolverTrigonometric.OnUpdate` operating on a live skeleton: it writes
    /// `bone1`'s world rotation, **re-reads `bone2`** (Unity propagates the
    /// transform), then writes `bone2`/`bone3`.
    pub fn update_sk(&mut self, sk: &mut IkSkeleton, target_pos: V3, target_rot: Quat) {
        if !self.initiated {
            self.initiate_sk(sk);
        }
        if !self.read_chain(sk) {
            return;
        }
        self.ik_position = target_pos;
        self.ik_rotation = target_rot;
        let w = self.ik_position_weight.clamp(0.0, 1.0);
        let rw = self.ik_rotation_weight.clamp(0.0, 1.0);
        if w > 0.0 {
            if !self.direct_hierarchy {
                self.bone1.initiate(self.bone2.pos, self.bend_normal);
                self.bone2.initiate(self.bone3.pos, self.bend_normal);
            }
            self.bone1.sqr_mag =
                vdot(vsub(self.bone2.pos, self.bone1.pos), vsub(self.bone2.pos, self.bone1.pos));
            self.bone2.sqr_mag =
                vdot(vsub(self.bone3.pos, self.bone2.pos), vsub(self.bone3.pos, self.bone2.pos));
            let weight_ik_position = vlerp(self.bone3.pos, self.ik_position, w);
            let bend = vlerp(
                self.bone1.get_bend_normal_from_current_rotation(),
                self.bend_normal,
                w,
            );
            let mut dir = vlerp(
                vsub(self.bone2.pos, self.bone1.pos),
                self.get_bend_direction(weight_ik_position, bend),
                w,
            );
            if vis_zero(dir) {
                dir = vsub(self.bone2.pos, self.bone1.pos);
            }
            let q1 = self.bone1.get_rotation(dir, bend);
            self.bone1.rot = q1;
            sk.set_world_rot(&self.chain[0].clone().unwrap(), q1);
            // Unity moved bone2: refresh its world transform before solving it.
            if let (Some(p1), Some(r1)) = (
                sk.world_pos(&self.chain[1].clone().unwrap()),
                sk.world_rot(&self.chain[1].clone().unwrap()),
            ) {
                self.bone2.pos = p1;
                self.bone2.rot = r1;
            }
            let q2 = self.bone2.get_rotation(
                vsub(weight_ik_position, self.bone2.pos),
                self.bone2.get_bend_normal_from_current_rotation(),
            );
            self.bone2.rot = q2;
            sk.set_world_rot(&self.chain[1].clone().unwrap(), q2);
        }
        if rw > 0.0 {
            if let Some(r2) = sk.world_rot(&self.chain[2].clone().unwrap()) {
                self.bone3.rot = r2;
            }
            self.bone3.rot = qslerp(self.bone3.rot, self.ik_rotation, rw);
            sk.set_world_rot(&self.chain[2].clone().unwrap(), self.bone3.rot);
        }
    }

    /// `IKSolverTrigonometric.GetBendDirection`.
    pub(crate) fn get_bend_direction(&self, ik_position: V3, bend_normal: V3) -> V3 {
        let vector = vsub(ik_position, self.bone1.pos);
        if vis_zero(vector) {
            return [0.0; 3];
        }
        let sqr_magnitude = vdot(vector, vector);
        let len = sqr_magnitude.sqrt();
        let num2 = (sqr_magnitude + self.bone1.sqr_mag - self.bone2.sqr_mag) / 2.0 / len;
        let y = (self.bone1.sqr_mag - num2 * num2).max(0.0).sqrt();
        let upwards = vcross(vector, bend_normal);
        let rot = qlook_rotation(vector, upwards);
        qrot(rot, [0.0, y, num2])
    }
}

// ---------------------------------------------------------------------------
// Unity `Vector3` helpers used by the FinalIK stack (exact Unity semantics).
// ---------------------------------------------------------------------------

/// Unity `Vector3.Angle(from, to)` (degrees).
pub fn vangle(from: V3, to: V3) -> f32 {
    let denominator=(vdot(from,from)*vdot(to,to)).sqrt();
    if denominator<1e-15{return 0.;}
    let d = (vdot(from,to)/denominator).clamp(-1.0, 1.0);
    d.acos().to_degrees()
}

/// Unity `Vector3.Project(v, onNormal)`.
pub fn vproject(v: V3, on_normal: V3) -> V3 {
    let d = vdot(on_normal, on_normal);
    if d < 1e-12 {
        return [0.0; 3];
    }
    vdiv(vmul(on_normal,vdot(v,on_normal)),d)
}

/// Unity `Vector3.OrthoNormalize(ref normal, ref tangent)`.
pub fn vortho_normalize(normal: V3, tangent: V3) -> (V3, V3) {
    let n = cpp_vnorm(normal);
    let mut t = vsub(tangent, vmul(n, cpp_vdot(n, tangent)));
    t = cpp_vnorm(t);
    (n, t)
}

/// Unity `Vector3.Slerp(a, b, t)`.
pub fn vslerp(a: V3, b: V3, t: f32) -> V3 {
    let t = t.clamp(0.0, 1.0);
    let (ma, mb) = (cpp_vlen(a), cpp_vlen(b));
    if ma < 1e-9 || mb < 1e-9 {
        return vlerp(a, b, t);
    }
    let start = vmul(a, 1.0 / ma);
    let end = vmul(b, 1.0 / mb);
    let dot = cpp_vdot(start, end).clamp(-1.0, 1.0);
    let theta = dot.acos() * t;
    let rel = vsub(end, vmul(start, dot));
    let rel = if cpp_vlen(rel) < 1e-9 { [0.0; 3] } else { cpp_vnorm(rel) };
    let result = vadd(vmul(start, theta.cos()), vmul(rel, theta.sin()));
    vmul(result, ma + (mb - ma) * t)
}

/// `Interp.InOutQuintic` with b=0, c=1.
pub fn in_out_quintic(t: f32) -> f32 {
    let t2 = t * t;
    let t3 = t2 * t;
    6.0 * t3 * t2 - 15.0 * t2 * t2 + 10.0 * t3
}

/// `V3Tools.ClampDirection`.
pub fn clamp_direction(direction: V3, normal_direction: V3, clamp_weight: f32, clamp_smoothing: i32) -> (V3, bool) {
    if clamp_weight <= 0.0 {
        return (direction, false);
    }
    if clamp_weight >= 1.0 {
        return (normal_direction, true);
    }
    let num = vangle(normal_direction, direction);
    let num2 = 1.0 - num / 180.0;
    if num2 > clamp_weight {
        return (direction, false);
    }
    let num3 = if clamp_weight > 0.0 {
        (1.0 - (clamp_weight - num2) / (1.0 - num2)).clamp(0.0, 1.0)
    } else {
        1.0
    };
    let mut num4 = if clamp_weight > 0.0 {
        (num2 / clamp_weight).clamp(0.0, 1.0)
    } else {
        1.0
    };
    for _ in 0..clamp_smoothing {
        num4 = (num4 * std::f32::consts::PI * 0.5).sin();
    }
    (vslerp(normal_direction, direction, num4 * num3), true)
}

/// `V3Tools.ExtractVertical`.
pub fn extract_vertical(v: V3, vertical_axis: V3, weight: f32) -> V3 {
    if weight == 0.0 {
        return [0.0; 3];
    }
    vmul(vproject(v, vertical_axis), weight)
}

/// `V3Tools.ExtractHorizontal`.
pub fn extract_horizontal(v: V3, normal: V3, weight: f32) -> V3 {
    if weight == 0.0 {
        return [0.0; 3];
    }
    let (_, tangent) = vortho_normalize(normal, v);
    vmul(vproject(v, tangent), weight)
}

pub(crate) fn triangle_bend_components(magnitude:f32,sqr_mag1:f32,sqr_mag_dif:f32)->(f32,f32){
        // Mono retains expression precision until float stores/managed call boundaries.
        // 1,920 compiled GetDirToBendPoint intermediate pairs match these
        // exact boundaries; rounding each arithmetic operation to f32 does not.
        let magnitude=magnitude as f64;
        let num=((magnitude*magnitude+sqr_mag_dif as f64)/2./magnitude)as f32;
        let radicand=(sqr_mag1 as f64-(num as f64)*(num as f64))as f32;
        let y=(radicand.max(0.)as f64).sqrt()as f32;
        (y,num)
}

/// Unity `AvatarIKGoal`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AvatarIkGoal {
    LeftFoot,
    RightFoot,
    LeftHand,
    RightHand,
}

/// `IKSolverLimb.BendModifier`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BendModifier {
    Animation,
    Target,
    Parent,
    Arm,
    Goal,
}

/// `IKSolverLimb.AxisDirection`.
#[derive(Clone, Copy, Debug)]
pub struct AxisDirection {
    pub direction: V3,
    pub axis: V3,
    pub dot: f32,
}

impl Default for AxisDirection {
    fn default() -> Self {
        Self { direction: [0.0; 3], axis: [0.0; 3], dot: 0.0 }
    }
}

fn store_axis_directions() -> [AxisDirection; 4] {
    let dir = |d: V3| vnorm(d);
    [
        AxisDirection { direction: vnorm([0.0, 0.0, 0.0]), axis: vnorm([-1.0, 0.0, 0.0]), dot: 0.0 },
        AxisDirection {
            direction: dir([0.5, 0.0, -0.2]),
            axis: dir([-0.5, -1.0, 1.0]),
            dot: 0.0,
        },
        AxisDirection {
            direction: dir([-0.5, -1.0, -0.2]),
            axis: dir([0.0, 0.5, -1.0]),
            dot: 0.0,
        },
        AxisDirection {
            direction: dir([-0.5, -0.5, 1.0]),
            axis: dir([-1.0, -1.0, -1.0]),
            dot: 0.0,
        },
    ]
}

/// Port of `IKSolverLimb` (`IKSolverLimb.cs`), built on top of the already
/// ported `IKSolverTrigonometric`.
#[derive(Clone, Debug)]
pub struct IKSolverLimb {
    pub trig: IKSolverTrigonometric,
    pub goal: AvatarIkGoal,
    pub bend_modifier: BendModifier,
    pub maintain_rotation_weight: f32,
    pub bend_modifier_weight: f32,
    pub bend_goal: Option<String>,
    pub root: Option<String>,
    maintain_bend_for_1_frame: bool,
    maintain_rotation_for_1_frame: bool,
    default_root_rotation: Quat,
    parent_default_rotation: Quat,
    bone3_rotation_before_solve: Quat,
    maintain_rotation: Quat,
    bone3_default_rotation: Quat,
    _bend_normal: V3,
    animation_normal: V3,
    axis_left: [AxisDirection; 4],
    axis_right: [AxisDirection; 4],
}

impl Default for IKSolverLimb {
    fn default() -> Self {
        Self {
            trig: IKSolverTrigonometric::default(),
            goal: AvatarIkGoal::LeftFoot,
            bend_modifier: BendModifier::Animation,
            maintain_rotation_weight: 0.0,
            bend_modifier_weight: 1.0,
            bend_goal: None,
            root: None,
            maintain_bend_for_1_frame: false,
            maintain_rotation_for_1_frame: false,
            default_root_rotation: IDENTITY,
            parent_default_rotation: IDENTITY,
            bone3_rotation_before_solve: IDENTITY,
            maintain_rotation: IDENTITY,
            bone3_default_rotation: IDENTITY,
            _bend_normal: [0.0; 3],
            animation_normal: [0.0; 3],
            axis_left: store_axis_directions(),
            axis_right: store_axis_directions(),
        }
    }
}

impl IKSolverLimb {
    pub fn new(goal: AvatarIkGoal) -> Self {
        Self { goal, ..Default::default() }
    }

    fn axes(&self) -> &[AxisDirection; 4] {
        if self.goal == AvatarIkGoal::LeftHand {
            &self.axis_left
        } else {
            &self.axis_right
        }
    }
    fn axes_mut(&mut self) -> &mut [AxisDirection; 4] {
        if self.goal == AvatarIkGoal::LeftHand {
            &mut self.axis_left
        } else {
            &mut self.axis_right
        }
    }

    /// `IKSolverLimb.MaintainRotation`.
    pub fn maintain_rotation(&mut self) {
        if self.trig.initiated {
            self.maintain_rotation = self.trig.bone3.rot;
            self.maintain_rotation_for_1_frame = true;
        }
    }

    /// `IKSolverLimb.MaintainBend`.
    pub fn maintain_bend(&mut self) {
        if self.trig.initiated {
            self.animation_normal = self.trig.bone1.get_bend_normal_from_current_rotation();
            self.maintain_bend_for_1_frame = true;
        }
    }

    /// `IKSolver.Initiate` (`OnInitiate`) + `OnInitiateVirtual`.
    pub fn initiate_sk(&mut self, sk: &mut IkSkeleton) {
        if !self.trig.read_chain(sk) {
            return;
        }
        if vis_zero(self.trig.bend_normal) {
            self.trig.bend_normal = [1.0, 0.0, 0.0];
        }
        // OnInitiateVirtual.
        self.default_root_rotation = self
            .root
            .as_ref()
            .and_then(|n| sk.world_rot(n))
            .unwrap_or(IDENTITY);
        if let Some(chain0) = self.trig.chain[0].clone() {
            if let Some(parent) = sk.parent_name(&chain0) {
                if let Some(pr) = sk.world_rot(&parent) {
                    self.parent_default_rotation = qmul(qinv(self.default_root_rotation), pr);
                }
            }
        }
        self.bone3_default_rotation = self.trig.bone3.rot;
        let v = vcross(
            vsub(self.trig.bone2.pos, self.trig.bone1.pos),
            vsub(self.trig.bone3.pos, self.trig.bone2.pos),
        );
        if !vis_zero(v) {
            self.trig.bend_normal = v;
        }
        self.animation_normal = self.trig.bend_normal;
        self.axis_left = store_axis_directions();
        self.axis_right = store_axis_directions();

        // `IKSolverTrigonometric.OnInitiate`.
        self.trig.ik_position = self.trig.bone3.pos;
        self.trig.ik_rotation = self.trig.bone3.rot;
        self.trig.bone1.initiate(self.trig.bone2.pos, self.trig.bend_normal);
        self.trig.bone2.initiate(self.trig.bone3.pos, self.trig.bend_normal);
        self.trig.set_bend_plane_to_current();
        self.trig.direct_hierarchy = self.is_direct_hierarchy(sk);
        self.trig.initiated = true;
    }

    fn is_direct_hierarchy(&self, sk: &mut IkSkeleton) -> bool {
        let c0 = self.trig.chain[0].clone();
        let c1 = self.trig.chain[1].clone();
        let c2 = self.trig.chain[2].clone();
        let (Some(c0), Some(c1), Some(c2)) = (c0, c1, c2) else {
            return false;
        };
        sk.parent_name(&c2).as_deref() == Some(c1.as_str())
            && sk.parent_name(&c1).as_deref() == Some(c0.as_str())
    }

    fn on_update_virtual(&mut self, sk: &mut IkSkeleton) {
        let w = self.trig.ik_position_weight;
        if w > 0.0 {
            self.bend_modifier_weight = self.bend_modifier_weight.clamp(0.0, 1.0);
            self.maintain_rotation_weight = self.maintain_rotation_weight.clamp(0.0, 1.0);
            self._bend_normal = self.trig.bend_normal;
            self.trig.bend_normal = self.get_modified_bend_normal(sk);
        }
        if self.maintain_rotation_weight * w > 0.0 {
            self.bone3_rotation_before_solve = if self.maintain_rotation_for_1_frame {
                self.maintain_rotation
            } else {
                self.trig.bone3.rot
            };
            self.maintain_rotation_for_1_frame = false;
        }
    }

    fn on_post_solve_virtual(&mut self, sk: &mut IkSkeleton) {
        let w = self.trig.ik_position_weight;
        if w > 0.0 {
            self.trig.bend_normal = self._bend_normal;
        }
        if self.maintain_rotation_weight * w > 0.0 {
            let nw = self.maintain_rotation_weight * w;
            if let Some(c2) = self.trig.chain[2].clone() {
                if let Some(r2) = sk.world_rot(&c2) {
                    self.trig.bone3.rot = r2;
                }
                let nr = qslerp(self.trig.bone3.rot, self.bone3_rotation_before_solve, nw);
                self.trig.bone3.rot = nr;
                sk.set_world_rot(&c2, nr);
            }
        }
    }

    fn get_modified_bend_normal(&mut self, sk: &mut IkSkeleton) -> V3 {
        let num = self.bend_modifier_weight;
        if num <= 0.0 {
            return self.trig.bend_normal;
        }
        match self.bend_modifier {
            BendModifier::Animation => {
                if !self.maintain_bend_for_1_frame {
                    self.maintain_bend();
                }
                self.maintain_bend_for_1_frame = false;
                vlerp(self.trig.bend_normal, self.animation_normal, num)
            }
            BendModifier::Parent => {
                let Some(c0) = self.trig.chain[0].clone() else {
                    return self.trig.bend_normal;
                };
                let Some(parent) = sk.parent_name(&c0) else {
                    return self.trig.bend_normal;
                };
                let Some(pr) = sk.world_rot(&parent) else {
                    return self.trig.bend_normal;
                };
                let quaternion = qmul(pr, qinv(self.parent_default_rotation));
                let q = qmul(quaternion, qinv(self.default_root_rotation));
                qrot(qslerp(IDENTITY, q, num), self.trig.bend_normal)
            }
            BendModifier::Target => {
                let b = qmul(self.trig.ik_rotation, qinv(self.bone3_default_rotation));
                qrot(qslerp(IDENTITY, b, num), self.trig.bend_normal)
            }
            BendModifier::Arm => {
                let Some(c0) = self.trig.chain[0].clone() else {
                    return self.trig.bend_normal;
                };
                let Some(parent) = sk.parent_name(&c0) else {
                    return self.trig.bend_normal;
                };
                if self.goal == AvatarIkGoal::LeftFoot || self.goal == AvatarIkGoal::RightFoot {
                    return self.trig.bend_normal;
                }
                let Some(pr) = sk.world_rot(&parent) else {
                    return self.trig.bend_normal;
                };
                let q = qmul(pr, qinv(self.parent_default_rotation));
                let mut normalized = vnorm(vsub(self.trig.ik_position, self.trig.bone1.pos));
                normalized = qrot(qinv(q), normalized);
                if self.goal == AvatarIkGoal::LeftHand {
                    normalized[0] = -normalized[0];
                }
                let axes = self.axes_mut();
                for i in 1..4 {
                    axes[i].dot = vdot(axes[i].direction, normalized).clamp(0.0, 1.0);
                    axes[i].dot = in_out_quintic(axes[i].dot);
                }
                let mut vector2 = axes[0].axis;
                for j in 1..4 {
                    vector2 = vslerp(vector2, axes[j].axis, axes[j].dot);
                }
                if self.goal == AvatarIkGoal::LeftHand {
                    vector2[0] = -vector2[0];
                    vector2 = vmul(vector2, -1.0);
                }
                let vector3 = qrot(q, vector2);
                if num >= 1.0 {
                    vector3
                } else {
                    vlerp(self.trig.bend_normal, vector3, num)
                }
            }
            BendModifier::Goal => {
                let Some(goal) = self.bend_goal.clone() else {
                    return self.trig.bend_normal;
                };
                let Some(gp) = sk.world_pos(&goal) else {
                    return self.trig.bend_normal;
                };
                let v = vcross(
                    vsub(gp, self.trig.bone1.pos),
                    vsub(self.trig.ik_position, self.trig.bone1.pos),
                );
                if vis_zero(v) {
                    return self.trig.bend_normal;
                }
                if num >= 1.0 {
                    v
                } else {
                    vlerp(self.trig.bend_normal, v, num)
                }
            }
        }
    }

    /// `IKSolverTrigonometric.OnUpdate` with the `IKSolverLimb` virtual hooks.
    pub fn update_sk(&mut self, sk: &mut IkSkeleton, target_pos: V3, target_rot: Quat) {
        if !self.trig.initiated {
            self.initiate_sk(sk);
        }
        if !self.trig.read_chain(sk) {
            return;
        }
        self.trig.ik_position = target_pos;
        self.trig.ik_rotation = target_rot;
        let w = self.trig.ik_position_weight.clamp(0.0, 1.0);
        let rw = self.trig.ik_rotation_weight.clamp(0.0, 1.0);
        self.trig.ik_position_weight = w;
        self.trig.ik_rotation_weight = rw;
        self.on_update_virtual(sk);
        if w > 0.0 {
            if !self.trig.direct_hierarchy {
                self.trig.bone1.initiate(self.trig.bone2.pos, self.trig.bend_normal);
                self.trig.bone2.initiate(self.trig.bone3.pos, self.trig.bend_normal);
            }
            self.trig.bone1.sqr_mag = vdot(
                vsub(self.trig.bone2.pos, self.trig.bone1.pos),
                vsub(self.trig.bone2.pos, self.trig.bone1.pos),
            );
            self.trig.bone2.sqr_mag = vdot(
                vsub(self.trig.bone3.pos, self.trig.bone2.pos),
                vsub(self.trig.bone3.pos, self.trig.bone2.pos),
            );
            let weight_ik_position = vlerp(self.trig.bone3.pos, self.trig.ik_position, w);
            let bend = vlerp(
                self.trig.bone1.get_bend_normal_from_current_rotation(),
                self.trig.bend_normal,
                w,
            );
            let mut dir = vlerp(
                vsub(self.trig.bone2.pos, self.trig.bone1.pos),
                self.trig.get_bend_direction(weight_ik_position, bend),
                w,
            );
            if vis_zero(dir) {
                dir = vsub(self.trig.bone2.pos, self.trig.bone1.pos);
            }
            let q1 = self.trig.bone1.get_rotation(dir, bend);
            self.trig.bone1.rot = q1;
            if let Some(c0) = self.trig.chain[0].clone() {
                sk.set_world_rot(&c0, q1);
            }
            if let Some(c1) = self.trig.chain[1].clone() {
                let p1 = sk.world_pos(&c1);
                let r1 = sk.world_rot(&c1);
                if let (Some(p1), Some(r1)) = (p1, r1) {
                    self.trig.bone2.pos = p1;
                    self.trig.bone2.rot = r1;
                }
            }
            let q2 = self.trig.bone2.get_rotation(
                vsub(weight_ik_position, self.trig.bone2.pos),
                self.trig.bone2.get_bend_normal_from_current_rotation(),
            );
            self.trig.bone2.rot = q2;
            if let Some(c1) = self.trig.chain[1].clone() {
                sk.set_world_rot(&c1, q2);
            }
        }
        if rw > 0.0 {
            if let Some(c2) = self.trig.chain[2].clone() {
                if let Some(r2) = sk.world_rot(&c2) {
                    self.trig.bone3.rot = r2;
                }
                let nr = qslerp(self.trig.bone3.rot, self.trig.ik_rotation, rw);
                self.trig.bone3.rot = nr;
                sk.set_world_rot(&c2, nr);
            }
        }
        self.on_post_solve_virtual(sk);
    }
}

/// Build a Unity quaternion from a rotation matrix whose **columns** are the
/// rotated basis vectors (our `pose::Mat4` upper-left 3x3 is R*scale).
pub fn qfrom_mat3(c0: V3, c1: V3, c2: V3) -> Quat {
    let (m00, m10, m20) = (c0[0], c0[1], c0[2]);
    let (m01, m11, m21) = (c1[0], c1[1], c1[2]);
    let (m02, m12, m22) = (c2[0], c2[1], c2[2]);
    let trace = m00 + m11 + m22;
    if trace > 0.0 {
        let s = (trace + 1.0).sqrt() * 2.0;
        [
            (m21 - m12) / s,
            (m02 - m20) / s,
            (m10 - m01) / s,
            0.25 * s,
        ]
    } else if m00 > m11 && m00 > m22 {
        let s = (1.0 + m00 - m11 - m22).sqrt() * 2.0;
        [
            0.25 * s,
            (m01 + m10) / s,
            (m02 + m20) / s,
            (m21 - m12) / s,
        ]
    } else if m11 > m22 {
        let s = (1.0 + m11 - m00 - m22).sqrt() * 2.0;
        [
            (m01 + m10) / s,
            0.25 * s,
            (m12 + m21) / s,
            (m02 - m20) / s,
        ]
    } else {
        let s = (1.0 + m22 - m00 - m11).sqrt() * 2.0;
        [
            (m02 + m20) / s,
            (m12 + m21) / s,
            0.25 * s,
            (m10 - m01) / s,
        ]
    }
}

/// Extract the (unit) rotation of a `pose::Mat4` (columns normalised).
pub fn mat_rot(m: &crate::pose::Mat4) -> Quat {
    let c0 = [m[0][0], m[1][0], m[2][0]];
    let c1 = [m[0][1], m[1][1], m[2][1]];
    let c2 = [m[0][2], m[1][2], m[2][2]];
    qfrom_mat3(vnorm(c0), vnorm(c1), vnorm(c2))
}

/// Adapter that lets a FinalIK port read/write world transforms on our
/// `pose::Skeleton`. FinalIK interleaves "write bone rotation" with "read the
/// next bone's world position", so every mutation marks the world cache dirty
/// and every read flushes (`rebuild_worlds`).
pub struct IkSkeleton<'a> {
    pub sk: &'a mut crate::pose::Skeleton,
    dirty: bool,
    defer_worlds: bool,
    names:Option<std::collections::HashMap<String,i64>>,
}

impl<'a> IkSkeleton<'a> {
    pub fn new(sk: &'a mut crate::pose::Skeleton) -> Self {
        Self { sk, dirty: false, defer_worlds: false,names:None }
    }

    /// All solver getters read live locals directly. The paired caller
    /// publishes worlds once after solving and removing temporary proxies.
    /// Keep new()'s existing intermediate publication for other callers.
    pub(crate) fn new_batched(sk: &'a mut crate::pose::Skeleton) -> Self {
        // This crate-owned solve scope changes TRS only. Temporary targets are
        // inserted before construction and removed after it drops. Match the
        // legacy first HashMap entry for case-insensitive duplicate names.
        let mut names=std::collections::HashMap::with_capacity(sk.tfs.len());
        for(id,tf)in &sk.tfs{names.entry(tf.name.to_ascii_lowercase()).or_insert(*id);}
        Self { sk, dirty: false, defer_worlds: true,names:Some(names) }
    }

    fn flush(&mut self) {
        if self.dirty && !self.defer_worlds {
            self.sk.rebuild_worlds();
            self.dirty = false;
        }
    }

    pub fn has(&self, name: &str) -> bool {
        self.sk.index_of(name).is_some()
            || self
                .sk
                .tfs
                .values()
                .any(|t| t.name.eq_ignore_ascii_case(name))
    }

    fn id_of(&self, name: &str) -> Option<i64> {
        if let Some(names)=&self.names{return names.get(&name.to_ascii_lowercase()).copied();}
        self.sk
            .tfs
            .iter()
            .find(|(_, t)| t.name.eq_ignore_ascii_case(name))
            .map(|(id, _)| *id)
    }

    /// Name of a bone's parent (Unity `transform.parent.name`).
    pub fn parent_name(&self, name: &str) -> Option<String> {
        let id = self.id_of(name)?;
        let pid = self.sk.tfs.get(&id).and_then(|t| t.parent)?;
        if pid == 0 || pid == id {
            return None;
        }
        self.sk.tfs.get(&pid).map(|t| t.name.clone())
    }

    /// Unity `transform.position` setter.
    pub fn set_world_pos(&mut self, name: &str, world: V3) -> bool {
        self.flush();
        let Some(id) = self.id_of(name) else {
            return false;
        };
        let parent = self.sk.tfs.get(&id).and_then(|t| t.parent);
        let local = match parent.filter(|p| *p != id) {
            Some(pid) => {
                let mut chain=Vec::new();let mut cur=Some(pid);
                for _ in 0..128{let Some(id)=cur else{break;};let Some(tf)=self.sk.tfs.get(&id)else{break;};chain.push(tf);cur=tf.parent.filter(|p|*p!=0&&*p!=id);}
                let mut point=world;
                for tf in chain.into_iter().rev(){point=cpp_qrot(qinv(tf.rot),vsub(point,tf.pos));for a in 0..3{if tf.scale[a]==0.{return false;}point[a]/=tf.scale[a];}}
                point
            }
            None => world,
        };
        if let Some(t) = self.sk.tfs.get_mut(&id) {
            t.pos = local;
            self.dirty = true;
        }
        true
    }

    /// Unity `transform.position` getter.
    pub fn world_pos(&mut self, name: &str) -> Option<V3> {
        let mut cur=Some(self.id_of(name)?);let mut p=[0.;3];
        // Unity TransformPoint scales the input before rotation. Folding scale
        // into matrix columns changes float rounding and can flip the bend
        // plane of an almost straight limb (compiled native scale controls).
        for _ in 0..128{let Some(id)=cur else{break;};let Some(tf)=self.sk.tfs.get(&id)else{break;};
            for a in 0..3{p[a]*=tf.scale[a];}
            let m=crate::pose::trs(tf.pos,tf.rot,[1.;3]);
            p=std::array::from_fn(|a|((m[a][0]*p[0]+m[a][1]*p[1])+m[a][2]*p[2])+m[a][3]);cur=tf.parent.filter(|p|*p!=0&&*p!=id);
        }Some(p)
    }

    /// Read both components from one immutable chain without matrix rotation
    /// extraction. Preserve the legacy leaf-to-root operation ordering.
    pub(crate) fn world_transform(&mut self,name:&str)->Option<(V3,Quat)>{
        let mut cur=Some(self.id_of(name)?);let mut p=[0.;3];let mut rotation=IDENTITY;
        for _ in 0..128{let Some(id)=cur else{break;};let Some(tf)=self.sk.tfs.get(&id)else{break;};
            for a in 0..3{p[a]*=tf.scale[a];}
            let m=crate::pose::trs(tf.pos,tf.rot,[1.;3]);
            p=std::array::from_fn(|a|((m[a][0]*p[0]+m[a][1]*p[1])+m[a][2]*p[2])+m[a][3]);
            rotation=cpp_qmul(tf.rot,rotation);cur=tf.parent.filter(|p|*p!=0&&*p!=id);
        }Some((p,rotation))
    }

    /// Unity `transform.localPosition` getter.
    pub fn local_pos(&self, name: &str) -> Option<V3> {
        let id = self.id_of(name)?;
        self.sk.tfs.get(&id).map(|t| t.pos)
    }

    /// Unity `transform.localRotation` getter.
    pub fn local_rot(&self, name: &str) -> Option<Quat> {
        let id = self.id_of(name)?;
        self.sk.tfs.get(&id).map(|t| t.rot)
    }

    /// Unity `transform.localPosition` setter.
    pub fn set_local_pos(&mut self, name: &str, pos: V3) -> bool {
        let Some(id) = self.id_of(name) else {
            return false;
        };
        if let Some(t) = self.sk.tfs.get_mut(&id) {
            t.pos = pos;
            self.dirty = true;
        }
        true
    }

    fn world_rotation_of(&self,id:i64)->Quat{
        // Use the managed Quaternion multiplication order for Transform chains;
        // Unity composes from the leaf toward the root. The opposite grouping
        // and pose's equivalent y/z grouping round differently.
        let mut rotation=IDENTITY;let mut cur=Some(id);
        for _ in 0..128{let Some(id)=cur else{break;};let Some(tf)=self.sk.tfs.get(&id)else{break;};rotation=cpp_qmul(tf.rot,rotation);cur=tf.parent.filter(|p|*p!=0&&*p!=id);}
        rotation
    }    pub fn world_rot(&mut self, name: &str) -> Option<Quat> {
        // Unity Transform.rotation composes local quaternions; extracting
        // rotation from a world matrix is wrong under nonuniform ABMX scale.
        let id=self.id_of(name)?;
        Some(self.world_rotation_of(id))
    }

    /// Set a bone's **world** rotation (FinalIK writes `transform.rotation`).
    pub fn set_world_rot(&mut self, name: &str, world: Quat) -> bool {
        self.flush();
        let Some(id) = self.id_of(name) else {
            return false;
        };
        let parent = self.sk.tfs.get(&id).and_then(|t| t.parent);
        let local = match parent.filter(|p| *p != id) {
            Some(pid) => {
                let parent_rotation=self.world_rotation_of(pid);
                cpp_qmul(qinv(parent_rotation), world)
            }
            None => world,
        };
        if let Some(t) = self.sk.tfs.get_mut(&id) {
            t.rot = qnorm(local);
            self.dirty = true;
        }
        true
    }

    /// Set a bone's local rotation directly.
    pub fn set_local_rot(&mut self, name: &str, local: Quat) -> bool {
        let Some(id) = self.id_of(name) else {
            return false;
        };
        if let Some(t) = self.sk.tfs.get_mut(&id) {
            t.rot = local;
            self.dirty = true;
        }
        true
    }
}

// ---------------------------------------------------------------------------
// `IKSolver.Node` / `FBIKChain` / `IKConstraintBend` / `IKEffector`
// ---------------------------------------------------------------------------

#[cfg(test)]
mod batching_tests {
    use super::*;
    #[test]
    fn scoped_name_index_preserves_duplicate_and_case_semantics(){
        use crate::pose::{Skeleton,Tf};
        let mut original=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:0,bone_ids:vec![],source_mesh:0,tfs:Default::default()};
        for(id,name)in [(1,"Root"),(2,"arm"),(3,"ARM"),(4,"Arm"),(5,"Händ"),(6,"__ok_pair_target_0")]{original.tfs.insert(id,Tf{name:name.into(),parent:None,sibling_index:None,pos:[id as f32;3],rot:[0.,0.,0.,1.],scale:[1.;3]});}
        let mut batched=original.clone();
        let old=IkSkeleton::new(&mut original);let new=IkSkeleton::new_batched(&mut batched);
        for name in ["ROOT","arm","ARM","Arm","Händ","händ","HÄND","missing","__ok_pair_target_0"]{assert_eq!(old.id_of(name),new.id_of(name));}
    }
    #[test]
    fn batched_interleaved_transform_access_matches_legacy() {
        use crate::pose::{Skeleton,Tf};
        for scale in [[1.0;3],[-1.2,0.7,1.4],[0.0,1.0,1.0]] {
            let mut original=Skeleton {names:Vec::new(),worlds:Vec::new(),rest:Vec::new(),smr_count:0,bone_ids:vec![1,2,3],source_mesh:0,tfs:Default::default()};
            for (id,name,parent) in [(1,"Root",None),(2,"Arm",Some(1)),(3,"Hand",Some(2))] {
                original.tfs.insert(id,Tf{name:name.into(),parent,sibling_index:None,pos:[0.1,0.2,0.3],rot:[0.0,0.6,0.0,0.8],scale});
            }
            original.rebuild_worlds();let mut batched=original.clone();
            {
                let mut old=IkSkeleton::new(&mut original);let mut new=IkSkeleton::new_batched(&mut batched);
                for frame in 0..12 {
                    let q=crate::pose::euler_deg_to_quat([frame as f32,11.0,17.0]);
                    assert_eq!(old.set_local_rot("ROOT",q),new.set_local_rot("ROOT",q));
                    assert_eq!(old.set_world_pos("Arm",[0.4,frame as f32*0.01,0.2]),new.set_world_pos("Arm",[0.4,frame as f32*0.01,0.2]));
                    assert_eq!(old.set_world_rot("Hand",q),new.set_world_rot("Hand",q));
                    for name in ["ROOT","arm","Hand","missing"] {
                        assert_eq!(old.world_pos(name),new.world_pos(name));assert_eq!(old.world_rot(name),new.world_rot(name));
                        assert_eq!(old.local_pos(name),new.local_pos(name));assert_eq!(old.local_rot(name),new.local_rot(name));
                    }
                }
            }
            original.rebuild_worlds();batched.rebuild_worlds();assert_eq!(original.worlds,batched.worlds);
            for (id,t) in &original.tfs {let b=&batched.tfs[id];assert_eq!((t.pos,t.rot,t.scale),(b.pos,b.rot,b.scale));}
        }
    }
}

/// `IKSolverFullBody.Smoothing`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Smoothing {
    None,
    Exponential,
    Cubic,
}

/// `IKSolver.Node`.
#[derive(Clone, Debug)]
pub struct Node {
    pub name: String,
    pub weight: f32,
    pub solver_position: V3,
    pub solver_rotation: Quat,
    pub length: f32,
    pub effector_position_weight: f32,
    pub effector_rotation_weight: f32,
    pub offset: V3,
}

impl Default for Node {
    fn default() -> Self {
        Self {
            name: String::new(),
            weight: 1.0,
            solver_position: [0.0; 3],
            solver_rotation: IDENTITY,
            length: 0.0,
            effector_position_weight: 0.0,
            effector_rotation_weight: 0.0,
            offset: [0.0; 3],
        }
    }
}

/// `FBIKChain.ChildConstraint`.
#[derive(Clone, Debug)]
pub struct ChildConstraint {
    pub push_elasticity: f32,
    pub pull_elasticity: f32,
    pub bone1: String,
    pub bone2: String,
    pub nominal_distance: f32,
    pub is_rigid: bool,
    cross_fade: f32,
    inverse_cross_fade: f32,
    chain1_index: i32,
    chain2_index: i32,
}

impl ChildConstraint {
    pub fn new(bone1: &str, bone2: &str) -> Self {
        Self {
            push_elasticity: 0.0,
            pull_elasticity: 0.0,
            bone1: bone1.to_string(),
            bone2: bone2.to_string(),
            nominal_distance: 0.0,
            is_rigid: false,
            cross_fade: 0.5,
            inverse_cross_fade: 0.5,
            chain1_index: -1,
            chain2_index: -1,
        }
    }
    pub fn new_elastic(bone1: &str, bone2: &str, push: f32, pull: f32) -> Self {
        Self {
            push_elasticity: push,
            pull_elasticity: pull,
            ..Self::new(bone1, bone2)
        }
    }
}

/// `FBIKChain`.
#[derive(Clone, Debug)]
pub struct FBIKChain {
    pub pin: f32,
    pub pull: f32,
    pub push: f32,
    pub push_parent: f32,
    pub reach: f32,
    pub reach_smoothing: Smoothing,
    pub push_smoothing: Smoothing,
    pub nodes: Vec<Node>,
    pub children: Vec<usize>,
    pub root_length: f32,
    pub initiated: bool,
    pub length: f32,
    pub distance: f32,
    pub reach_force: f32,
    pub pull_parent_sum: f32,
    pub cross_fades: Vec<f32>,
    pub sqr_mag1: f32,
    pub sqr_mag2: f32,
    pub sqr_mag_dif: f32,
}

impl Default for FBIKChain {
    fn default() -> Self {
        Self {
            pin: 0.0,
            pull: 1.0,
            push: 0.0,
            push_parent: 0.0,
            reach: 0.1,
            reach_smoothing: Smoothing::Exponential,
            push_smoothing: Smoothing::Exponential,
            nodes: Vec::new(),
            children: Vec::new(),
            root_length: 0.0,
            initiated: false,
            length: 0.0,
            distance: 0.0,
            reach_force: 0.0,
            pull_parent_sum: 0.0,
            cross_fades: Vec::new(),
            sqr_mag1: 0.0,
            sqr_mag2: 0.0,
            sqr_mag_dif: 0.0,
        }
    }
}

impl FBIKChain {
    pub fn set_nodes(&mut self, names: &[&str]) {
        self.nodes = names
            .iter()
            .map(|n| Node { name: n.to_string(), ..Default::default() })
            .collect();
    }
    pub fn node_index(&self, name: &str) -> i32 {
        self.nodes
            .iter()
            .position(|n| n.name == name)
            .map(|i| i as i32)
            .unwrap_or(-1)
    }
}

/// `IKConstraintBend`.
#[derive(Clone, Debug)]
pub struct IKConstraintBend {
    pub bone1: Option<String>,
    pub bone2: Option<String>,
    pub bone3: Option<String>,
    pub bend_goal: Option<String>,
    /// A bend goal given as a position (skeleton space): Studio IK's elbow and knee targets.
    pub goal_position: Option<V3>,
    pub direction: V3,
    pub rotation_offset: Quat,
    pub weight: f32,
    pub default_local_direction: V3,
    pub default_child_direction: V3,
    pub clamp_f: f32,
    chain_index1: i32,
    node_index1: i32,
    chain_index2: i32,
    node_index2: i32,
    chain_index3: i32,
    node_index3: i32,
    pub initiated: bool,
    orientation: Option<(V3, V3)>,
}

impl Default for IKConstraintBend {
    fn default() -> Self {
        Self {
            bone1: None,
            bone2: None,
            bone3: None,
            bend_goal: None,
            goal_position: None,
            direction: [1.0, 0.0, 0.0],
            rotation_offset: IDENTITY,
            weight: 0.0,
            default_local_direction: [0.0; 3],
            default_child_direction: [0.0; 3],
            clamp_f: 0.505,
            chain_index1: -1,
            node_index1: -1,
            chain_index2: -1,
            node_index2: -1,
            chain_index3: -1,
            node_index3: -1,
            initiated: false,
            orientation: None,
        }
    }
}

impl IKConstraintBend {
    #[cfg(test)]
    pub(crate) fn initialization_directions(&self)->[V3;3]{[self.direction,self.default_local_direction,self.default_child_direction]}
    pub fn set_bones(&mut self, bone1: &str, bone2: &str, bone3: &str) {
        self.bone1 = Some(bone1.to_string());
        self.bone2 = Some(bone2.to_string());
        self.bone3 = Some(bone3.to_string());
    }

    pub fn set_limb_orientation(&mut self, upper: V3, lower: V3, last: V3) {
        self.orientation = Some((vnorm(upper), vnorm(last)));
        let _ = lower;
    }

    fn node_pos(chains: &[FBIKChain], ci: i32, ni: i32) -> V3 {
        if ci < 0 || ni < 0 {
            return [0.0; 3];
        }
        chains
            .get(ci as usize)
            .and_then(|c| c.nodes.get(ni as usize))
            .map(|n| n.solver_position)
            .unwrap_or([0.0; 3])
    }

    fn ortho_to_limb(chains: &[FBIKChain], ci1: i32, ni1: i32, ci3: i32, ni3: i32, tangent: V3) -> V3 {
        let normal = vsub(
            Self::node_pos(chains, ci3, ni3),
            Self::node_pos(chains, ci1, ni1),
        );
        vortho_normalize(normal, tangent).1
    }

    fn ortho_to_bone1(chains: &[FBIKChain], ci1: i32, ni1: i32, ci2: i32, ni2: i32, tangent: V3) -> V3 {
        let normal = vsub(
            Self::node_pos(chains, ci2, ni2),
            Self::node_pos(chains, ci1, ni1),
        );
        vortho_normalize(normal, tangent).1
    }

    pub fn initiate(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton) {
        let (b1, b2, b3) = match (&self.bone1, &self.bone2, &self.bone3) {
            (Some(a), Some(b), Some(c)) => (a.clone(), b.clone(), c.clone()),
            _ => return,
        };
        let (c1, n1) = chain_and_node(chains, &b1);
        let (c2, n2) = chain_and_node(chains, &b2);
        let (c3, n3) = chain_and_node(chains, &b3);
        self.chain_index1 = c1;
        self.node_index1 = n1;
        self.chain_index2 = c2;
        self.node_index2 = n2;
        self.chain_index3 = c3;
        self.node_index3 = n3;
        let (p1, p2, p3) = (
            sk.world_pos(&b1).unwrap_or([0.0; 3]),
            sk.world_pos(&b2).unwrap_or([0.0; 3]),
            sk.world_pos(&b3).unwrap_or([0.0; 3]),
        );
        let tangent = vsub(p2, p1);
        let tangent = Self::ortho_to_limb(chains, c1, n1, c3, n3, tangent);
        self.direction = Self::ortho_to_bone1(chains, c1, n1, c2, n2, tangent);
        let r1 = sk.world_rot(&b1).unwrap_or(IDENTITY);
        let r3 = sk.world_rot(&b3).unwrap_or(IDENTITY);
        match self.orientation {
            Some((upper, last)) => {
                self.default_local_direction = upper;
                self.default_child_direction = last;
            }
            None => {
                self.default_local_direction = qrot(qinv(r1), self.direction);
                let vector = vcross(vnorm(vsub(p3, p1)), self.direction);
                self.default_child_direction = qrot(qinv(r3), vector);
            }
        }
        self.initiated = true;
    }

    /// `IKConstraintBend.LimitBend`.
    pub fn limit_bend(&mut self, sk: &mut IkSkeleton, solver_weight: f32, position_weight: f32) {
        if !self.initiated {
            return;
        }
        let (Some(b1), Some(b2), Some(b3)) = (
            self.bone1.clone(),
            self.bone2.clone(),
            self.bone3.clone(),
        ) else {
            return;
        };
        let r1 = sk.world_rot(&b1).unwrap_or(IDENTITY);
        let vector = qrot(r1, vmul(self.default_local_direction, -1.0));
        let to = sk.world_pos(&b3).unwrap_or([0.0; 3]);
        let from = sk.world_pos(&b2).unwrap_or([0.0; 3]);
        let from_direction = vsub(to, from);
        let (to_direction, changed) =
            clamp_direction(from_direction, vector, self.clamp_f * solver_weight, 0);
        let rotation = sk.world_rot(&b3).unwrap_or(IDENTITY);
        if changed {
            let q = qfrom_to_rotation(from_direction, to_direction);
            let r2 = sk.world_rot(&b2).unwrap_or(IDENTITY);
            sk.set_world_rot(&b2, qmul(q, r2));
        }
        if position_weight > 0.0 {
            let normal = vsub(sk.world_pos(&b2).unwrap_or([0.0; 3]), sk.world_pos(&b1).unwrap_or([0.0; 3]));
            let tangent = vsub(sk.world_pos(&b3).unwrap_or([0.0; 3]), sk.world_pos(&b2).unwrap_or([0.0; 3]));
            let (_n, tangent) = vortho_normalize(normal, tangent);
            let q2 = qfrom_to_rotation(tangent, vector);
            let r2 = sk.world_rot(&b2).unwrap_or(IDENTITY);
            let blended = qlerp(r2, qmul(q2, r2), position_weight * solver_weight);
            sk.set_world_rot(&b2, blended);
        }
        if changed || position_weight > 0.0 {
            sk.set_world_rot(&b3, rotation);
        }
    }

    /// `IKConstraintBend.GetDir`.
    pub fn get_dir(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton, solver_weight: f32) -> V3 {
        if !self.initiated {
            return [0.0; 3];
        }
        let num = self.weight * solver_weight;
        if let Some(gp) = self.goal_position.or_else(|| self.bend_goal.as_deref().and_then(|goal| sk.world_pos(goal))) {
            let vector = vsub(gp, Self::node_pos(chains, self.chain_index1, self.node_index1));
            if !vis_zero(vector) {
                self.direction = vector;
            }
        }
        if num >= 1.0 {
            return vnorm(self.direction);
        }
        let vector2 = vsub(
            Self::node_pos(chains, self.chain_index3, self.node_index3),
            Self::node_pos(chains, self.chain_index1, self.node_index1),
        );
        let (b1, b3) = match (&self.bone1, &self.bone3) {
            (Some(a), Some(b)) => (a.clone(), b.clone()),
            _ => return [0.0; 3],
        };
        let p1 = sk.world_pos(&b1).unwrap_or([0.0; 3]);
        let p3 = sk.world_pos(&b3).unwrap_or([0.0; 3]);
        let p2 = sk.world_pos(&self.bone2.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
        let mut vector3 = qrot(
            qfrom_to_rotation(vsub(p3, p1), vector2),
            vsub(p2, p1),
        );
        let (ci3, ni3) = (self.chain_index3, self.node_index3);
        if let Some(node) = chains
            .get(ci3 as usize)
            .and_then(|c| c.nodes.get(ni3 as usize))
        {
            if node.effector_rotation_weight > 0.0 {
                let b = vmul(
                    vcross(vector2, qrot(node.solver_rotation, self.default_child_direction)),
                    -1.0,
                );
                vector3 = vlerp(vector3, b, node.effector_rotation_weight);
            }
        }
        if self.rotation_offset != IDENTITY {
            vector3 = qrot(
                qmul(qfrom_to_rotation(qrot(self.rotation_offset, vector2), vector2), self.rotation_offset),
                vector3,
            );
        }
        if num <= 0.0 {
            return vector3;
        }
        vlerp(vector3, vnorm(self.direction), num)
    }
}

/// `IKEffector`.
#[derive(Clone, Debug)]
pub struct IKEffector {
    pub bone: Option<String>,
    pub target: Option<String>,
    pub position_weight: f32,
    pub rotation_weight: f32,
    pub position: V3,
    pub rotation: Quat,
    pub position_offset: V3,
    pub effect_child_nodes: bool,
    pub maintain_relative_position_weight: f32,
    pub child_bones: Vec<String>,
    pub plane_bone1: Option<String>,
    pub plane_bone2: Option<String>,
    pub plane_bone3: Option<String>,
    pub plane_rotation_offset: Quat,
    pos_w: f32,
    rot_w: f32,
    local_positions: Vec<V3>,
    use_plane_nodes: bool,
    animated_plane_rotation: Quat,
    animated_position: V3,
    first_update: bool,
    chain_index: i32,
    node_index: i32,
    plane1_chain_index: i32,
    plane1_node_index: i32,
    plane2_chain_index: i32,
    plane2_node_index: i32,
    plane3_chain_index: i32,
    plane3_node_index: i32,
    child_chain_indexes: Vec<i32>,
    child_node_indexes: Vec<i32>,
    pub is_end_effector: bool,
}

impl Default for IKEffector {
    fn default() -> Self {
        Self {
            bone: None,
            target: None,
            position_weight: 0.0,
            rotation_weight: 0.0,
            position: [0.0; 3],
            rotation: IDENTITY,
            position_offset: [0.0; 3],
            effect_child_nodes: true,
            maintain_relative_position_weight: 0.0,
            child_bones: Vec::new(),
            plane_bone1: None,
            plane_bone2: None,
            plane_bone3: None,
            plane_rotation_offset: IDENTITY,
            pos_w: 0.0,
            rot_w: 0.0,
            local_positions: Vec::new(),
            use_plane_nodes: false,
            animated_plane_rotation: IDENTITY,
            animated_position: [0.0; 3],
            first_update: false,
            chain_index: -1,
            node_index: -1,
            plane1_chain_index: -1,
            plane1_node_index: -1,
            plane2_chain_index: -1,
            plane2_node_index: -1,
            plane3_chain_index: -1,
            plane3_node_index: -1,
            child_chain_indexes: Vec::new(),
            child_node_indexes: Vec::new(),
            is_end_effector: false,
        }
    }
}

// ---------------------------------------------------------------------------
// `IKMapping` (BoneMap) / `IKMappingSpine` / `IKMappingLimb` / `IKMappingBone`
// ---------------------------------------------------------------------------

/// `IKMapping.BoneMap`.
#[derive(Clone, Debug)]
pub struct BoneMap {
    pub name: Option<String>,
    pub chain_index: i32,
    pub node_index: i32,
    pub default_local_position: V3,
    pub default_local_rotation: Quat,
    pub local_swing_axis: V3,
    pub local_twist_axis: V3,
    pub plane_position: V3,
    pub ik_position: V3,
    pub default_local_target_rotation: Quat,
    maintain_rotation: Quat,
    pub length: f32,
    pub animated_rotation: Quat,
    plane_bone1: Option<String>,
    plane_bone2: Option<String>,
    plane_bone3: Option<String>,
    plane1_chain_index: i32,
    plane1_node_index: i32,
    plane2_chain_index: i32,
    plane2_node_index: i32,
    plane3_chain_index: i32,
    plane3_node_index: i32,
}

impl Default for BoneMap {
    fn default() -> Self {
        Self {
            name: None,
            chain_index: -1,
            node_index: -1,
            default_local_position: [0.0; 3],
            default_local_rotation: IDENTITY,
            local_swing_axis: [0.0; 3],
            local_twist_axis: [0.0; 3],
            plane_position: [0.0; 3],
            ik_position: [0.0; 3],
            default_local_target_rotation: IDENTITY,
            maintain_rotation: IDENTITY,
            length: 0.0,
            animated_rotation: IDENTITY,
            plane_bone1: None,
            plane_bone2: None,
            plane_bone3: None,
            plane1_chain_index: -1,
            plane1_node_index: -1,
            plane2_chain_index: -1,
            plane2_node_index: -1,
            plane3_chain_index: -1,
            plane3_node_index: -1,
        }
    }
}

fn chain_and_node(chains: &[FBIKChain], name: &str) -> (i32, i32) {
    for (ci, c) in chains.iter().enumerate() {
        let ni = c.node_index(name);
        if ni >= 0 {
            return (ci as i32, ni);
        }
    }
    (-1, -1)
}

fn node_solver_pos(chains: &[FBIKChain], ci: i32, ni: i32) -> V3 {
    if ci < 0 || ni < 0 {
        return [0.0; 3];
    }
    chains
        .get(ci as usize)
        .and_then(|c| c.nodes.get(ni as usize))
        .map(|n| n.solver_position)
        .unwrap_or([0.0; 3])
}

impl BoneMap {
    pub fn initiate(&mut self, name: &str, chains: &[FBIKChain]) {
        self.name = Some(name.to_string());
        let (ci, ni) = chain_and_node(chains, name);
        self.chain_index = ci;
        self.node_index = ni;
    }

    pub fn store_default_local_state(&mut self, sk: &mut IkSkeleton) {
        if let Some(n) = &self.name {
            self.default_local_position = sk.local_pos(n).unwrap_or([0.0; 3]);
            self.default_local_rotation = sk.local_rot(n).unwrap_or(IDENTITY);
        }
    }

    pub fn fix_transform(&mut self, sk: &mut IkSkeleton, position: bool) {
        if let Some(n) = self.name.clone() {
            if position {
                sk.set_local_pos(&n, self.default_local_position);
            }
            sk.set_local_rot(&n, self.default_local_rotation);
        }
    }

    pub fn set_length(&mut self, next: &BoneMap, sk: &mut IkSkeleton) {
        let (Some(a), Some(b)) = (self.name.clone(), next.name.clone()) else {
            return;
        };
        let pa = sk.world_pos(&a).unwrap_or([0.0; 3]);
        let pb = sk.world_pos(&b).unwrap_or([0.0; 3]);
        self.length = vlen(vsub(pa, pb));
    }

    pub fn set_local_swing_axis(&mut self, swing_target: &BoneMap, sk: &mut IkSkeleton) {
        // C# `SetLocalSwingAxis(swingTarget)` = `SetLocalSwingAxis(swingTarget, this)`,
        // i.e. `Inverse(transform.rotation) * (target.position - this.position)`.
        let (Some(a), Some(b)) = (self.name.clone(), swing_target.name.clone()) else {
            return;
        };
        let pa = sk.world_pos(&a).unwrap_or([0.0; 3]);
        let pb = sk.world_pos(&b).unwrap_or([0.0; 3]);
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        self.local_swing_axis = qrot(qinv(r), vsub(pb, pa));
    }

    pub fn set_local_swing_axis_two(&mut self, bone1: &BoneMap, bone2: &BoneMap, sk: &mut IkSkeleton) {
        let (Some(a), Some(b1), Some(b2)) =
            (self.name.clone(), bone1.name.clone(), bone2.name.clone())
        else {
            return;
        };
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        let p1 = sk.world_pos(&b1).unwrap_or([0.0; 3]);
        let p2 = sk.world_pos(&b2).unwrap_or([0.0; 3]);
        self.local_swing_axis = qrot(qinv(r), vsub(p1, p2));
    }

    pub fn set_local_twist_axis(&mut self, twist_direction: V3, normal_direction: V3, sk: &mut IkSkeleton) {
        let Some(a) = self.name.clone() else { return };
        let (_n, twist) = vortho_normalize(normal_direction, twist_direction);
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        self.local_twist_axis = qrot(qinv(r), twist);
    }

    pub fn set_plane(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton, p1: &str, p2: &str, p3: &str) {
        self.plane_bone1 = Some(p1.to_string());
        self.plane_bone2 = Some(p2.to_string());
        self.plane_bone3 = Some(p3.to_string());
        let (c1, n1) = chain_and_node(chains, p1);
        let (c2, n2) = chain_and_node(chains, p2);
        let (c3, n3) = chain_and_node(chains, p3);
        self.plane1_chain_index = c1;
        self.plane1_node_index = n1;
        self.plane2_chain_index = c2;
        self.plane2_node_index = n2;
        self.plane3_chain_index = c3;
        self.plane3_node_index = n3;
        self.update_plane(chains, sk, true, true);
    }

    fn last_animated_target_rotation(&self, sk: &mut IkSkeleton) -> Quat {
        let (Some(p1), Some(p2), Some(p3)) = (
            self.plane_bone1.clone(),
            self.plane_bone2.clone(),
            self.plane_bone3.clone(),
        ) else {
            return IDENTITY;
        };
        let a = sk.world_pos(&p1).unwrap_or([0.0; 3]);
        let b = sk.world_pos(&p2).unwrap_or([0.0; 3]);
        let c = sk.world_pos(&p3).unwrap_or([0.0; 3]);
        if a == c {
            return IDENTITY;
        }
        qlook_rotation(vsub(b, a), vsub(c, a))
    }

    pub fn update_plane(&mut self, _chains: &[FBIKChain], sk: &mut IkSkeleton, rotation: bool, position: bool) {
        let rotation2 = self.last_animated_target_rotation(sk);
        let Some(a) = self.name.clone() else { return };
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        if rotation {
            self.default_local_target_rotation = rotation_to_local_space(r, rotation2);
        }
        if position {
            let Some(p1) = self.plane_bone1.clone() else { return };
            let pa = sk.world_pos(&a).unwrap_or([0.0; 3]);
            let pb = sk.world_pos(&p1).unwrap_or([0.0; 3]);
            self.plane_position = qrot(qinv(rotation2), vsub(pa, pb));
        }
    }

    pub fn set_ik_position(&mut self, sk: &mut IkSkeleton) {
        if let Some(a) = &self.name {
            self.ik_position = sk.world_pos(a).unwrap_or([0.0; 3]);
        }
    }

    pub fn maintain_rotation(&mut self, sk: &mut IkSkeleton) {
        if let Some(a) = &self.name {
            self.maintain_rotation = sk.world_rot(a).unwrap_or(IDENTITY);
        }
    }

    pub fn set_to_ik_position(&mut self, sk: &mut IkSkeleton) {
        if let Some(a) = self.name.clone() {
            sk.set_world_pos(&a, self.ik_position);
        }
    }

    pub fn fix_to_node(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton, weight: f32) {
        let Some(a) = self.name.clone() else { return };
        let target = node_solver_pos(chains, self.chain_index, self.node_index);
        let cur = sk.world_pos(&a).unwrap_or([0.0; 3]);
        let p = if weight >= 1.0 { target } else { vlerp(cur, target, weight) };
        sk.set_world_pos(&a, p);
    }

    fn get_target_rotation(&self, chains: &[FBIKChain]) -> Quat {
        let p1 = node_solver_pos(chains, self.plane1_chain_index, self.plane1_node_index);
        let p2 = node_solver_pos(chains, self.plane2_chain_index, self.plane2_node_index);
        let p3 = node_solver_pos(chains, self.plane3_chain_index, self.plane3_node_index);
        if p1 == p3 {
            return IDENTITY;
        }
        qlook_rotation(vsub(p2, p1), vsub(p3, p1))
    }

    pub fn get_plane_position(&self, chains: &[FBIKChain]) -> V3 {
        let p1 = node_solver_pos(chains, self.plane1_chain_index, self.plane1_node_index);
        vadd(p1, qrot(self.get_target_rotation(chains), self.plane_position))
    }

    pub fn position_to_plane(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton) {
        if let Some(a) = self.name.clone() {
            let p = self.get_plane_position(chains);
            sk.set_world_pos(&a, p);
        }
    }

    pub fn rotate_to_plane(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton, weight: f32) {
        let Some(a) = self.name.clone() else { return };
        let q = qmul(self.get_target_rotation(chains), self.default_local_target_rotation);
        let cur = sk.world_rot(&a).unwrap_or(IDENTITY);
        let r = if weight >= 1.0 { q } else { qlerp(cur, q, weight) };
        sk.set_world_rot(&a, r);
    }

    pub fn swing(&mut self, sk: &mut IkSkeleton, swing_target: V3, weight: f32) {
        let Some(a) = self.name.clone() else { return };
        let pos2 = sk.world_pos(&a).unwrap_or([0.0; 3]);
        self.swing_to(sk, swing_target, pos2, weight);
    }

    pub fn swing_to(&mut self, sk: &mut IkSkeleton, pos1: V3, pos2: V3, weight: f32) {
        let Some(a) = self.name.clone() else { return };
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        let q = qmul(
            qfrom_to_rotation(qrot(r, self.local_swing_axis), vsub(pos1, pos2)),
            r,
        );
        let r = if weight >= 1.0 { q } else { qlerp(r, q, weight) };
        sk.set_world_rot(&a, r);
    }

    pub fn twist(&mut self, sk: &mut IkSkeleton, twist_direction: V3, normal_direction: V3, weight: f32) {
        let Some(a) = self.name.clone() else { return };
        let (_n, twist) = vortho_normalize(normal_direction, twist_direction);
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        let q = qmul(qfrom_to_rotation(qrot(r, self.local_twist_axis), twist), r);
        let r = if weight >= 1.0 { q } else { qlerp(r, q, weight) };
        sk.set_world_rot(&a, r);
    }

    pub fn rotate_to_maintain(&mut self, sk: &mut IkSkeleton, weight: f32) {
        if weight <= 0.0 {
            return;
        }
        let Some(a) = self.name.clone() else { return };
        let r = sk.world_rot(&a).unwrap_or(IDENTITY);
        sk.set_world_rot(&a, qlerp(r, self.maintain_rotation, weight));
    }

    pub fn rotate_to_effector(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton, weight: f32) {
        if !self.is_node_bone() {
            return;
        }
        let node = chains
            .get(self.chain_index as usize)
            .and_then(|c| c.nodes.get(self.node_index as usize));
        let Some(node) = node else { return };
        let num = weight * node.effector_rotation_weight;
        if num <= 0.0 {
            return;
        }
        let Some(a) = self.name.clone() else { return };
        let target = node.solver_rotation;
        if num >= 1.0 {
            sk.set_world_rot(&a, target);
        } else {
            let r = sk.world_rot(&a).unwrap_or(IDENTITY);
            sk.set_world_rot(&a, qlerp(r, target, num));
        }
    }

    pub fn is_node_bone(&self) -> bool {
        self.node_index != -1
    }
}

/// `IKMappingSpine`.
#[derive(Clone, Debug)]
pub struct IKMappingSpine {
    pub spine_bones: Vec<String>,
    pub left_upper_arm_bone: Option<String>,
    pub right_upper_arm_bone: Option<String>,
    pub left_thigh_bone: Option<String>,
    pub right_thigh_bone: Option<String>,
    pub iterations: i32,
    pub twist_weight: f32,
    root_node_index: i32,
    spine: Vec<BoneMap>,
    left_upper_arm: BoneMap,
    right_upper_arm: BoneMap,
    left_thigh: BoneMap,
    right_thigh: BoneMap,
    use_fabrik: bool,
}

impl Default for IKMappingSpine {
    fn default() -> Self {
        Self {
            spine_bones: Vec::new(),
            left_upper_arm_bone: None,
            right_upper_arm_bone: None,
            left_thigh_bone: None,
            right_thigh_bone: None,
            iterations: 3,
            twist_weight: 1.0,
            root_node_index: -1,
            spine: Vec::new(),
            left_upper_arm: BoneMap::default(),
            right_upper_arm: BoneMap::default(),
            left_thigh: BoneMap::default(),
            right_thigh: BoneMap::default(),
            use_fabrik: false,
        }
    }
}

impl IKMappingSpine {
    pub fn set_bones(&mut self, spine: &[&str], lua: &str, rua: &str, lt: &str, rt: &str) {
        self.spine_bones = spine.iter().map(|s| s.to_string()).collect();
        self.left_upper_arm_bone = Some(lua.to_string());
        self.right_upper_arm_bone = Some(rua.to_string());
        self.left_thigh_bone = Some(lt.to_string());
        self.right_thigh_bone = Some(rt.to_string());
    }

    pub fn initiate(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton) {
        if self.iterations <= 0 {
            self.iterations = 3;
        }
        self.spine = vec![BoneMap::default(); self.spine_bones.len()];
        self.root_node_index = -1;
        for (i, name) in self.spine_bones.clone().iter().enumerate() {
            self.spine[i].initiate(name, chains);
            if self.spine[i].is_node_bone() {
                self.root_node_index = i as i32;
            }
        }
        self.left_upper_arm = BoneMap::default();
        self.right_upper_arm = BoneMap::default();
        self.left_thigh = BoneMap::default();
        self.right_thigh = BoneMap::default();
        self.left_upper_arm
            .initiate(self.left_upper_arm_bone.clone().unwrap_or_default().as_str(), chains);
        self.right_upper_arm
            .initiate(self.right_upper_arm_bone.clone().unwrap_or_default().as_str(), chains);
        self.left_thigh
            .initiate(self.left_thigh_bone.clone().unwrap_or_default().as_str(), chains);
        self.right_thigh
            .initiate(self.right_thigh_bone.clone().unwrap_or_default().as_str(), chains);
        for bm in self.spine.iter_mut() {
            bm.set_ik_position(sk);
        }
        if self.root_node_index < 0 {
            self.use_fabrik = true;
            return;
        }
        let root_name = self.spine[self.root_node_index as usize].name.clone().unwrap_or_default();
        let lt = self.left_thigh.name.clone().unwrap_or_default();
        let rt = self.right_thigh.name.clone().unwrap_or_default();
        self.spine[0].set_plane(chains, sk, &root_name, &lt, &rt);
        let n = self.spine.len();
        for k in 0..n - 1 {
            let next = self.spine[k + 1].clone();
            self.spine[k].set_length(&next, sk);
            self.spine[k].set_local_swing_axis(&next, sk);
            let lua = sk.world_pos(&self.left_upper_arm.name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
            let rua = sk.world_pos(&self.right_upper_arm.name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
            let p_next = sk.world_pos(&next.name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
            let p_cur = sk.world_pos(&self.spine[k].name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
            self.spine[k].set_local_twist_axis(vsub(lua, rua), vsub(p_next, p_cur), sk);
        }
        let lua = self.left_upper_arm.name.clone().unwrap_or_default();
        let rua = self.right_upper_arm.name.clone().unwrap_or_default();
        self.spine[n - 1].set_plane(chains, sk, &root_name, &lua, &rua);
        let lua_map = self.left_upper_arm.clone();
        let rua_map = self.right_upper_arm.clone();
        let last = self.spine.len() - 1;
        self.spine[last].set_local_swing_axis_two(&lua_map, &rua_map, sk);
        self.use_fabrik = self.spine.len() > 3 || self.root_node_index != 1;
    }

    pub fn read_pose(&mut self, sk: &mut IkSkeleton) {
        let n = self.spine.len();
        if n == 0 {
            return;
        }
        let plane_src = self.spine.clone();
        self.spine[0].update_plane(
            &[],
            sk,
            true,
            true,
        );
        for i in 0..n - 1 {
            let next = self.spine[i + 1].clone();
            self.spine[i].set_length(&next, sk);
            self.spine[i].set_local_swing_axis(&next, sk);
            let lua = sk
                .world_pos(&self.left_upper_arm.name.clone().unwrap_or_default())
                .unwrap_or([0.0; 3]);
            let rua = sk
                .world_pos(&self.right_upper_arm.name.clone().unwrap_or_default())
                .unwrap_or([0.0; 3]);
            let p_next = sk.world_pos(&next.name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
            let p_cur = sk.world_pos(&plane_src[i].name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
            self.spine[i].set_local_twist_axis(vsub(lua, rua), vsub(p_next, p_cur), sk);
        }
        self.spine[n - 1].update_plane(&[], sk, true, true);
        let lua_map = self.left_upper_arm.clone();
        let rua_map = self.right_upper_arm.clone();
        self.spine[n - 1].set_local_swing_axis_two(&lua_map, &rua_map, sk);
    }

    pub fn write_pose(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton) {
        let n = self.spine.len();
        if n == 0 || self.root_node_index < 0 {
            return;
        }
        let root_i = self.root_node_index as usize;
        let plane_position = self.spine[0].get_plane_position(chains);
        let solver_position = node_solver_pos(
            chains,
            self.spine[root_i].chain_index,
            self.spine[root_i].node_index,
        );
        let plane_position2 = self.spine[n - 1].get_plane_position(chains);
        if self.use_fabrik {
            let vector = vsub(
                solver_position,
                sk.world_pos(&self.spine[root_i].name.clone().unwrap_or_default())
                    .unwrap_or([0.0; 3]),
            );
            for i in 0..n {
                let p = sk.world_pos(&self.spine[i].name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
                self.spine[i].ik_position = vadd(p, vector);
            }
            for _ in 0..self.iterations {
                self.forward_reach(plane_position2);
                self.backward_reach(plane_position);
                self.spine[root_i].ik_position = solver_position;
            }
        } else {
            self.spine[0].ik_position = plane_position;
            self.spine[root_i].ik_position = solver_position;
        }
        self.spine[n - 1].ik_position = plane_position2;
        self.map_to_solver_positions(chains, sk);
    }

    fn forward_reach(&mut self, position: V3) {
        let n = self.spine.len();
        self.spine[n - 1].ik_position = position;
        for num in (0..n - 1).rev() {
            let next = self.spine[num + 1].ik_position;
            let cur = self.spine[num].ik_position;
            let len = self.spine[num].length;
            self.spine[num].ik_position = solve_fabrik_joint(cur, next, len);
        }
    }

    fn backward_reach(&mut self, position: V3) {
        let n = self.spine.len();
        self.spine[0].ik_position = position;
        for i in 1..n {
            let prev = self.spine[i - 1].ik_position;
            let cur = self.spine[i].ik_position;
            let len = self.spine[i - 1].length;
            self.spine[i].ik_position = solve_fabrik_joint(cur, prev, len);
        }
    }

    fn map_to_solver_positions(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton) {
        let n = self.spine.len();
        self.spine[0].set_to_ik_position(sk);
        self.spine[0].rotate_to_plane(chains, sk, 1.0);
        for i in 1..n - 1 {
            let next_pos = self.spine[i + 1].ik_position;
            self.spine[i].swing(sk, next_pos, 1.0);
            if self.twist_weight > 0.0 {
                let num = i as f32 / (n as f32 - 2.0);
                let lua = node_solver_pos(
                    chains,
                    self.left_upper_arm.chain_index,
                    self.left_upper_arm.node_index,
                );
                let rua = node_solver_pos(
                    chains,
                    self.right_upper_arm.chain_index,
                    self.right_upper_arm.node_index,
                );
                let cur = sk.world_pos(&self.spine[i].name.clone().unwrap_or_default()).unwrap_or([0.0; 3]);
                self.spine[i].twist(sk, vsub(lua, rua), vsub(next_pos, cur), num * self.twist_weight);
            }
        }
        self.spine[n - 1].set_to_ik_position(sk);
        self.spine[n - 1].rotate_to_plane(chains, sk, 1.0);
    }
}

fn solve_fabrik_joint(pos1: V3, pos2: V3, length: f32) -> V3 {
    vadd(pos2, vmul(vnorm(vsub(pos1, pos2)), length))
}

/// `IKMappingLimb`.
#[derive(Clone, Debug)]
pub struct IKMappingLimb {
    pub parent_bone: Option<String>,
    pub bone1: String,
    pub bone2: String,
    pub bone3: String,
    pub maintain_rotation_weight: f32,
    pub weight: f32,
    bone_map_parent: BoneMap,
    bone_map1: BoneMap,
    bone_map2: BoneMap,
    bone_map3: BoneMap,
}

impl Default for IKMappingLimb {
    fn default() -> Self {
        Self {
            parent_bone: None,
            bone1: String::new(),
            bone2: String::new(),
            bone3: String::new(),
            maintain_rotation_weight: 0.0,
            weight: 1.0,
            bone_map_parent: BoneMap::default(),
            bone_map1: BoneMap::default(),
            bone_map2: BoneMap::default(),
            bone_map3: BoneMap::default(),
        }
    }
}

impl IKMappingLimb {
    pub fn set_bones(&mut self, b1: &str, b2: &str, b3: &str, parent: Option<&str>) {
        self.bone1 = b1.to_string();
        self.bone2 = b2.to_string();
        self.bone3 = b3.to_string();
        self.parent_bone = parent.map(|s| s.to_string());
    }

    pub fn initiate(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton) {
        self.bone_map_parent = BoneMap::default();
        self.bone_map1 = BoneMap::default();
        self.bone_map2 = BoneMap::default();
        self.bone_map3 = BoneMap::default();
        if let Some(p) = self.parent_bone.clone() {
            self.bone_map_parent.initiate(&p, chains);
        }
        self.bone_map1.initiate(&self.bone1.clone(), chains);
        self.bone_map2.initiate(&self.bone2.clone(), chains);
        self.bone_map3.initiate(&self.bone3.clone(), chains);
        let (b1, b2, b3) = (
            self.bone1.clone(),
            self.bone2.clone(),
            self.bone3.clone(),
        );
        self.bone_map1.set_plane(chains, sk, &b1, &b2, &b3);
        self.bone_map2.set_plane(chains, sk, &b2, &b3, &b1);
        if self.parent_bone.is_some() {
            let m1 = self.bone_map1.clone();
            self.bone_map_parent.set_local_swing_axis(&m1, sk);
        }
    }

    pub fn read_pose(&mut self, sk: &mut IkSkeleton) {
        self.bone_map1.update_plane(&[], sk, true, true);
        self.bone_map2.update_plane(&[], sk, true, false);
        self.weight = self.weight.clamp(0.0, 1.0);
        self.bone_map3.maintain_rotation(sk);
    }

    pub fn write_pose(&mut self, chains: &[FBIKChain], sk: &mut IkSkeleton, full_body: bool, ik_weight: f32) {
        if self.weight <= 0.0 {
            return;
        }
        if full_body && self.parent_bone.is_some() {
            let p = node_solver_pos(chains, self.bone_map1.chain_index, self.bone_map1.node_index);
            self.bone_map_parent.swing(sk, p, self.weight);
        }
        self.bone_map1.rotate_to_plane(chains, sk, self.weight);
        self.bone_map2.rotate_to_plane(chains, sk, self.weight);
        self.bone_map3
            .rotate_to_maintain(sk, self.maintain_rotation_weight * self.weight * ik_weight);
        self.bone_map3.rotate_to_effector(chains, sk, self.weight);
    }
}

/// `IKMappingBone`.
#[derive(Clone, Debug)]
pub struct IKMappingBone {
    pub bone: Option<String>,
    pub maintain_rotation_weight: f32,
    bone_map: BoneMap,
}

impl Default for IKMappingBone {
    fn default() -> Self {
        Self {
            bone: None,
            maintain_rotation_weight: 1.0,
            bone_map: BoneMap::default(),
        }
    }
}

impl IKMappingBone {
    pub fn initiate(&mut self, chains: &[FBIKChain]) {
        self.bone_map = BoneMap::default();
        if let Some(b) = self.bone.clone() {
            self.bone_map.initiate(&b, chains);
        }
    }
    pub fn read_pose(&mut self, sk: &mut IkSkeleton) {
        self.bone_map.maintain_rotation(sk);
    }
    pub fn write_pose(&mut self, sk: &mut IkSkeleton, solver_weight: f32) {
        self.bone_map
            .rotate_to_maintain(sk, solver_weight * self.maintain_rotation_weight);
    }
}

// ---------------------------------------------------------------------------
// `BipedReferences` + `IKSolverFullBody` / `IKSolverFullBodyBiped`
// ---------------------------------------------------------------------------

/// `BipedReferences`.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct BipedReferences {
    pub root: Option<String>,
    pub pelvis: Option<String>,
    pub left_thigh: Option<String>,
    pub left_calf: Option<String>,
    pub left_foot: Option<String>,
    pub right_thigh: Option<String>,
    pub right_calf: Option<String>,
    pub right_foot: Option<String>,
    pub left_upper_arm: Option<String>,
    pub left_forearm: Option<String>,
    pub left_hand: Option<String>,
    pub right_upper_arm: Option<String>,
    pub right_forearm: Option<String>,
    pub right_hand: Option<String>,
    pub head: Option<String>,
    pub spine: Vec<String>,
    pub eyes: Vec<String>,
}

impl BipedReferences {
    pub fn is_filled(&self) -> bool {
        let s = |o: &Option<String>| o.as_deref().map(|x| !x.is_empty()).unwrap_or(false);
        s(&self.root)
            && s(&self.pelvis)
            && s(&self.left_thigh)
            && s(&self.left_calf)
            && s(&self.left_foot)
            && s(&self.right_thigh)
            && s(&self.right_calf)
            && s(&self.right_foot)
            && s(&self.left_upper_arm)
            && s(&self.left_forearm)
            && s(&self.left_hand)
            && s(&self.right_upper_arm)
            && s(&self.right_forearm)
            && s(&self.right_hand)
    }
}

/// `IKSolverFullBodyBiped.DetectRootNodeBone` (needs world positions).
pub fn detect_root_node_bone(refs: &BipedReferences, mut pos: impl FnMut(&str) -> V3) -> Option<String> {
    if refs.spine.len() < 1 {
        return None;
    }
    if refs.spine.len() == 1 {
        return Some(refs.spine[0].clone());
    }
    let lt = pos(refs.left_thigh.as_deref()?);
    let rt = pos(refs.right_thigh.as_deref()?);
    let vector = vlerp(lt, rt, 0.5);
    let lua = pos(refs.left_upper_arm.as_deref()?);
    let rua = pos(refs.right_upper_arm.as_deref()?);
    let on_normal = vsub(vlerp(lua, rua, 0.5), vector);
    let magnitude = vlen(on_normal);
    if refs.spine.len() < 2 {
        return Some(refs.spine[0].clone());
    }
    let mut num2 = 0usize;
    for i in 1..refs.spine.len() {
        let vector2 = vproject(vsub(pos(&refs.spine[i]), vector), on_normal);
        if vdot(vnorm(vector2), vnorm(on_normal)) > 0.0 && vlen(vector2) / magnitude < 0.5 {
            num2 = i;
        }
    }
    Some(refs.spine[num2].clone())
}

/// Full port of `IKSolverFullBodyBiped` (+ `IKSolverFullBody` + `FBIKChain`
/// solver methods + `IKEffector`). Operates on a live `IkSkeleton`.
#[derive(Clone, Debug)]
pub struct IKSolverFullBodyBiped {
    pub chains: Vec<FBIKChain>,
    pub bend_constraints: Vec<IKConstraintBend>,
    pub child_constraints: Vec<ChildConstraint>,
    pub effectors: Vec<IKEffector>,
    pub spine_mapping: IKMappingSpine,
    pub bone_mappings: Vec<IKMappingBone>,
    pub limb_mappings: Vec<IKMappingLimb>,
    pub iterations: i32,
    pub ik_position_weight: f32,
    pub spine_stiffness: f32,
    pub pull_body_vertical: f32,
    pub pull_body_horizontal: f32,
    pub root: Option<String>,
    pub root_node: Option<String>,
    pub root_up: V3,
    /// `Illusion.Component.Correct` `BaseProcess.Type.Sync` targets: the
    /// `cf_t_*` transform's world = assigned bone world + body rot * pos, and
    /// rotation = assigned bone rotation * rot.
    pub target_sync: std::collections::HashMap<String, (String, V3, Quat)>,
    pub body_bone_rot: Quat,
    pub initiated: bool,
    offset: V3,
}

impl Default for IKSolverFullBodyBiped {
    fn default() -> Self {
        Self {
            chains: Vec::new(),
            bend_constraints: Vec::new(),
            child_constraints: Vec::new(),
            effectors: Vec::new(),
            spine_mapping: IKMappingSpine::default(),
            bone_mappings: Vec::new(),
            limb_mappings: Vec::new(),
            iterations: 4,
            ik_position_weight: 1.0,
            spine_stiffness: 0.5,
            pull_body_vertical: 0.5,
            pull_body_horizontal: 0.0,
            root: None,
            root_node: None,
            root_up: [0.0, 1.0, 0.0],
            target_sync: std::collections::HashMap::new(),
            body_bone_rot: IDENTITY,
            initiated: false,
            offset: [0.0; 3],
        }
    }
}

impl IKSolverFullBodyBiped {
    /// `IKSolverFullBodyBiped.SetToReferences`.
    pub fn set_to_references(&mut self, refs: &BipedReferences, root_node: Option<String>, sk: &mut IkSkeleton) {
        self.root = refs.root.clone();
        let rn = match root_node {
            Some(r) => Some(r),
            None => detect_root_node_bone(refs, |n| sk.world_pos(n).unwrap_or([0.0; 3])),
        };
        let rn = match rn {
            Some(r) => r,
            None => return,
        };
        self.chains = vec![FBIKChain::default(); 5];
        self.bend_constraints = vec![IKConstraintBend::default(); 5];
        self.chains[0].pin = 0.0;
        self.chains[0].set_nodes(&[&rn]);
        self.chains[0].children = vec![1, 2, 3, 4];
        let r = |o: &Option<String>| o.clone().unwrap_or_default();
        self.chains[1].set_nodes(&[
            &r(&refs.left_upper_arm),
            &r(&refs.left_forearm),
            &r(&refs.left_hand),
        ]);
        self.chains[2].set_nodes(&[
            &r(&refs.right_upper_arm),
            &r(&refs.right_forearm),
            &r(&refs.right_hand),
        ]);
        self.chains[3].set_nodes(&[
            &r(&refs.left_thigh),
            &r(&refs.left_calf),
            &r(&refs.left_foot),
        ]);
        self.chains[4].set_nodes(&[
            &r(&refs.right_thigh),
            &r(&refs.right_calf),
            &r(&refs.right_foot),
        ]);
        self.effectors = vec![IKEffector::default(); 9];
        self.effectors[0].bone = Some(rn.clone());
        self.effectors[0].child_bones = vec![
            r(&refs.left_thigh),
            r(&refs.right_thigh),
        ];
        self.effectors[1].bone = refs.left_upper_arm.clone();
        self.effectors[2].bone = refs.right_upper_arm.clone();
        self.effectors[3].bone = refs.left_thigh.clone();
        self.effectors[4].bone = refs.right_thigh.clone();
        self.effectors[5].bone = refs.left_hand.clone();
        self.effectors[6].bone = refs.right_hand.clone();
        self.effectors[7].bone = refs.left_foot.clone();
        self.effectors[8].bone = refs.right_foot.clone();
        let set_plane = |e: &mut IKEffector, p1: &Option<String>, p2: &Option<String>, p3: &Option<String>| {
            e.plane_bone1 = p1.clone();
            e.plane_bone2 = p2.clone();
            e.plane_bone3 = p3.clone();
        };
        set_plane(
            &mut self.effectors[5],
            &refs.left_upper_arm,
            &refs.right_upper_arm,
            &Some(rn.clone()),
        );
        set_plane(
            &mut self.effectors[6],
            &refs.right_upper_arm,
            &refs.left_upper_arm,
            &Some(rn.clone()),
        );
        set_plane(
            &mut self.effectors[7],
            &refs.left_thigh,
            &refs.right_thigh,
            &Some(rn.clone()),
        );
        set_plane(
            &mut self.effectors[8],
            &refs.right_thigh,
            &refs.left_thigh,
            &Some(rn.clone()),
        );
        let lua = r(&refs.left_upper_arm);
        let rua = r(&refs.right_upper_arm);
        let lt = r(&refs.left_thigh);
        let rt = r(&refs.right_thigh);
        self.child_constraints = vec![
            ChildConstraint::new_elastic(&lua, &rt, 0.0, 1.0),
            ChildConstraint::new_elastic(&rua, &lt, 0.0, 1.0),
            ChildConstraint::new(&lua, &rua),
            ChildConstraint::new(&lt, &rt),
        ];
        let mut spine: Vec<String> = vec![r(&refs.pelvis)];
        spine.extend(refs.spine.iter().cloned());
        let spine_refs: Vec<&str> = spine.iter().map(|s| s.as_str()).collect();
        self.spine_mapping.iterations = 3;
        self.spine_mapping
            .set_bones(&spine_refs, &lua, &rua, &lt, &rt);
        match &refs.head {
            Some(h) if !h.is_empty() => {
                let mut bm = IKMappingBone::default();
                bm.maintain_rotation_weight = 0.0;
                bm.bone = Some(h.clone());
                self.bone_mappings = vec![bm];
            }
            _ => self.bone_mappings = Vec::new(),
        }
        self.limb_mappings = vec![IKMappingLimb::default(); 4];
        self.limb_mappings[2].maintain_rotation_weight = 1.0;
        self.limb_mappings[3].maintain_rotation_weight = 1.0;
        let lf = r(&refs.left_forearm);
        let lh = r(&refs.left_hand);
        let rf = r(&refs.right_forearm);
        let rh = r(&refs.right_hand);
        let lc = r(&refs.left_calf);
        let lfoot = r(&refs.left_foot);
        let rc = r(&refs.right_calf);
        let rfoot = r(&refs.right_foot);
        let left_clav = refs
            .left_upper_arm
            .as_ref()
            .and_then(|_| sk.parent_name(&lua));
        let right_clav = refs
            .right_upper_arm
            .as_ref()
            .and_then(|_| sk.parent_name(&rua));
        self.limb_mappings[0].set_bones(&lua, &lf, &lh, left_clav.as_deref());
        self.limb_mappings[1].set_bones(&rua, &rf, &rh, right_clav.as_deref());
        self.limb_mappings[2].set_bones(&lt, &lc, &lfoot, None);
        self.limb_mappings[3].set_bones(&rt, &rc, &rfoot, None);
    }

    /// `IKSolver.Initiate` + `IKSolverFullBody.OnInitiate`.
    pub fn initiate(&mut self, sk: &mut IkSkeleton) {
        for i in 0..self.chains.len() {
            self.chain_initiate(i, sk);
        }
        for i in 0..self.effectors.len() {
            self.effector_initiate(i, sk);
        }
        self.spine_mapping.initiate(&self.chains, sk);
        for i in 0..self.bone_mappings.len() {
            self.bone_mappings[i].initiate(&self.chains);
        }
        for i in 0..self.limb_mappings.len() {
            self.limb_mappings[i].initiate(&self.chains, sk);
        }
        self.initiated = true;
    }

    fn chain_initiate(&mut self, i: usize, sk: &mut IkSkeleton) {
        {
            let c = &mut self.chains[i];
            c.initiated = false;
            for n in c.nodes.iter_mut() {
                n.solver_position = sk.world_pos(&n.name).unwrap_or([0.0; 3]);
            }
        }
        self.calculate_bone_lengths(i, sk);
        if i == 0 {
            for k in 0..self.child_constraints.len() {
                let b1 = self.child_constraints[k].bone1.clone();
                let b2 = self.child_constraints[k].bone2.clone();
                let c1 = chain_and_node(&self.chains, &b1);
                let c2 = chain_and_node(&self.chains, &b2);
                self.child_constraints[k].chain1_index = c1.0;
                self.child_constraints[k].chain2_index = c2.0;
                self.child_constraint_pre_solve(k, sk);
            }
        }
        if self.chains[i].nodes.len() == 3 {
            let b1 = self.chains[i].nodes[0].name.clone();
            let b2 = self.chains[i].nodes[1].name.clone();
            let b3 = self.chains[i].nodes[2].name.clone();
            self.bend_constraints[i].set_bones(&b1, &b2, &b3);
            self.bend_constraints[i].initiate(&self.chains, sk);
        }
        let nch = self.chains[i].children.len();
        self.chains[i].cross_fades = vec![0.0; nch];
        self.chains[i].initiated = true;
    }

    fn calculate_bone_lengths(&mut self, i: usize, sk: &mut IkSkeleton) {
        let n = self.chains[i].nodes.len();
        let mut length = 0.0f32;
        for j in 0..n.saturating_sub(1) {
            let a = self.chains[i].nodes[j].name.clone();
            let b = self.chains[i].nodes[j + 1].name.clone();
            let pa = sk.world_pos(&a).unwrap_or([0.0; 3]);
            let pb = sk.world_pos(&b).unwrap_or([0.0; 3]);
            let d = vlen(vsub(pa, pb));
            self.chains[i].nodes[j].length = d;
            length += d;
            if d == 0.0 {
                return;
            }
        }
        self.chains[i].length = length;
        let children = self.chains[i].children.clone();
        let last = self.chains[i].nodes[n - 1].name.clone();
        let plast = sk.world_pos(&last).unwrap_or([0.0; 3]);
        for child in children {
            let c0 = self.chains[child].nodes[0].name.clone();
            let pc = sk.world_pos(&c0).unwrap_or([0.0; 3]);
            let rl = vlen(vsub(pc, plast));
            self.chains[child].root_length = rl;
            if rl == 0.0 {
                return;
            }
        }
        if n == 3 {
            let l0 = self.chains[i].nodes[0].length;
            let l1 = self.chains[i].nodes[1].length;
            self.chains[i].sqr_mag1 = l0 * l0;
            self.chains[i].sqr_mag2 = l1 * l1;
            self.chains[i].sqr_mag_dif = l0 * l0 - l1 * l1;
        }
    }

    fn effector_initiate(&mut self, e: usize, sk: &mut IkSkeleton) {
        let bone = self.effectors[e].bone.clone().unwrap_or_default();
        self.effectors[e].position = sk.world_pos(&bone).unwrap_or([0.0; 3]);
        self.effectors[e].rotation = sk.world_rot(&bone).unwrap_or(IDENTITY);
        self.effectors[e].animated_plane_rotation = IDENTITY;
        let (ci, ni) = chain_and_node(&self.chains, &bone);
        self.effectors[e].chain_index = ci;
        self.effectors[e].node_index = ni;
        let children = self.effectors[e].child_bones.clone();
        self.effectors[e].child_chain_indexes = vec![0; children.len()];
        self.effectors[e].child_node_indexes = vec![0; children.len()];
        for (k, cb) in children.iter().enumerate() {
            let (cc, cn) = chain_and_node(&self.chains, cb);
            self.effectors[e].child_chain_indexes[k] = cc;
            self.effectors[e].child_node_indexes[k] = cn;
        }
        self.effectors[e].local_positions = vec![[0.0; 3]; children.len()];
        self.effectors[e].use_plane_nodes = false;
        if let Some(p1) = self.effectors[e].plane_bone1.clone() {
            let (a, b) = chain_and_node(&self.chains, &p1);
            self.effectors[e].plane1_chain_index = a;
            self.effectors[e].plane1_node_index = b;
            if let Some(p2) = self.effectors[e].plane_bone2.clone() {
                let (a, b) = chain_and_node(&self.chains, &p2);
                self.effectors[e].plane2_chain_index = a;
                self.effectors[e].plane2_node_index = b;
                if let Some(p3) = self.effectors[e].plane_bone3.clone() {
                    let (a, b) = chain_and_node(&self.chains, &p3);
                    self.effectors[e].plane3_chain_index = a;
                    self.effectors[e].plane3_node_index = b;
                    self.effectors[e].use_plane_nodes = true;
                }
            }
            self.effectors[e].is_end_effector = true;
        } else {
            self.effectors[e].is_end_effector = false;
        }
    }

    fn chain_and_node(&self, name: &str) -> (i32, i32) {
        chain_and_node(&self.chains, name)
    }

    fn effector_for_node(&self, name: &str) -> Option<usize> {
        self.effectors
            .iter()
            .position(|e| e.bone.as_deref() == Some(name))
    }

    fn node_pos(&self, e: usize) -> V3 {
        node_solver_pos(
            &self.chains,
            self.effectors[e].chain_index,
            self.effectors[e].node_index,
        )
    }

    fn node_pos_mut(&mut self, e: usize) -> &mut V3 {
        let ci = self.effectors[e].chain_index as usize;
        let ni = self.effectors[e].node_index as usize;
        &mut self.chains[ci].nodes[ni].solver_position
    }

    // --- Main update flow ---------------------------------------------------

    /// `IKSolver.Update` + `IKSolverFullBody.OnUpdate`.
    pub fn update(&mut self, sk: &mut IkSkeleton) {
        self.root_up = self
            .root
            .as_ref()
            .and_then(|r| sk.world_rot(r))
            .map(|q| qrot(q, [0.0, 1.0, 0.0]))
            .unwrap_or([0.0, 1.0, 0.0]);
        if !self.initiated {
            self.initiate(sk);
        }
        if self.ik_position_weight <= 0.0 {
            for e in self.effectors.iter_mut() {
                e.position_offset = [0.0; 3];
            }
            return;
        }
        if self.chains.is_empty() {
            return;
        }
        self.ik_position_weight = self.ik_position_weight.clamp(0.0, 1.0);
        self.read_pose(sk);
        self.solve(sk);
        self.write_pose(sk);
        for e in self.effectors.iter_mut() {
            e.position_offset = [0.0; 3];
        }
    }

    /// `IKSolverFullBodyBiped.ReadPose`.
    pub fn read_pose(&mut self, sk: &mut IkSkeleton) {
        for e in 0..self.effectors.len() {
            self.effector_set_to_target(e, sk);
        }
        self.pull_body(sk);
        let push_elasticity = (1.0 - self.spine_stiffness).clamp(0.0, 1.0);
        if self.child_constraints.len() >= 2 {
            self.child_constraints[0].push_elasticity = push_elasticity;
            self.child_constraints[1].push_elasticity = push_elasticity;
        }
        // `IKSolverFullBody.ReadPose`.
        for i in 0..self.chains.len() {
            if self.bend_constraints[i].initiated {
                let node2 = self.chains[i].nodes[2].name.clone();
                let pw = self
                    .effector_for_node(&node2)
                    .map(|e| self.effectors[e].position_weight)
                    .unwrap_or(0.0);
                let w = self.ik_position_weight;
                self.bend_constraints[i].limit_bend(sk, w, pw);
            }
        }
        for e in 0..self.effectors.len() {
            self.effector_reset_offset(e);
        }
        for e in 0..self.effectors.len() {
            self.effector_on_pre_solve(e, sk);
        }
        let full_body = self.iterations > 0;
        for i in 0..self.chains.len() {
            self.chain_read_pose(i, sk, full_body);
        }
        if full_body {
            self.spine_mapping.read_pose(sk);
            for i in 0..self.bone_mappings.len() {
                self.bone_mappings[i].read_pose(sk);
            }
        }
        for i in 0..self.limb_mappings.len() {
            self.limb_mappings[i].read_pose(sk);
        }
    }

    fn effector_set_to_target(&mut self, e: usize, sk: &mut IkSkeleton) {
        let Some(target) = self.effectors[e].target.clone() else {
            return;
        };
        if let Some((bone, pos, rot)) = self.target_sync.get(&target).cloned() {
            let bp = sk.world_pos(&bone).unwrap_or([0.0; 3]);
            let br = sk.world_rot(&bone).unwrap_or(IDENTITY);
            self.effectors[e].position = vadd(bp, qrot(self.body_bone_rot, pos));
            self.effectors[e].rotation = qmul(br, rot);
            return;
        }
        if let Some(p) = sk.world_pos(&target) {
            self.effectors[e].position = p;
        }
        if let Some(r) = sk.world_rot(&target) {
            self.effectors[e].rotation = r;
        }
    }

    fn effector_reset_offset(&mut self, e: usize) {
        let ci=self.effectors[e].chain_index as usize;let ni=self.effectors[e].node_index as usize;
        self.chains[ci].nodes[ni].offset=[0.;3];
        let nchild = self.effectors[e].child_chain_indexes.len();
        for k in 0..nchild {
            let ci = self.effectors[e].child_chain_indexes[k] as usize;
            let ni = self.effectors[e].child_node_indexes[k] as usize;
            self.chains[ci].nodes[ni].offset = [0.0; 3];
        }
    }

    fn effector_on_pre_solve(&mut self, e: usize, sk: &mut IkSkeleton) {
        let iw = self.ik_position_weight;
        let pos_w = self.effectors[e].position_weight.clamp(0.0, 1.0) * iw;
        let rot_w = self.effectors[e].rotation_weight.clamp(0.0, 1.0) * iw;
        self.effectors[e].maintain_relative_position_weight =
            self.effectors[e].maintain_relative_position_weight.clamp(0.0, 1.0);
        self.effectors[e].pos_w = pos_w;
        self.effectors[e].rot_w = rot_w;
        let ci = self.effectors[e].chain_index as usize;
        let ni = self.effectors[e].node_index as usize;
        self.chains[ci].nodes[ni].effector_position_weight = pos_w;
        self.chains[ci].nodes[ni].effector_rotation_weight = rot_w;
        self.chains[ci].nodes[ni].solver_rotation = self.effectors[e].rotation;
        let po = self.effectors[e].position_offset;
        self.chains[ci].nodes[ni].offset = vadd(self.chains[ci].nodes[ni].offset, vmul(po, iw));
        let bone = self.effectors[e].bone.clone().unwrap_or_default();
        if self.effectors[e].effect_child_nodes && self.iterations > 0 {
            let children = self.effectors[e].child_bones.clone();
            for (k, cb) in children.iter().enumerate() {
                let cp = sk.world_pos(cb);
                let bp = sk.world_pos(&bone);
                if let (Some(cp), Some(bp)) = (cp, bp) {
                    self.effectors[e].local_positions[k] = vsub(cp, bp);
                }
                let (cci, cni) = self.chain_and_node(cb);
                if cci >= 0 {
                    self.chains[cci as usize].nodes[cni as usize].offset =
                        vadd(self.chains[cci as usize].nodes[cni as usize].offset, vmul(po, iw));
                }
            }
        }
        if self.effectors[e].use_plane_nodes && self.effectors[e].maintain_relative_position_weight > 0.0 {
            let p1 = self.effectors[e].plane_bone1.clone().unwrap_or_default();
            let p2 = self.effectors[e].plane_bone2.clone().unwrap_or_default();
            let p3 = self.effectors[e].plane_bone3.clone().unwrap_or_default();
            let a = sk.world_pos(&p1);
            let b = sk.world_pos(&p2);
            let c = sk.world_pos(&p3);
            if let (Some(a), Some(b), Some(c)) = (a, b, c) {
                self.effectors[e].animated_plane_rotation = qlook_rotation(vsub(b, a), vsub(c, a));
            }
        }
        self.effectors[e].first_update = true;
    }

    fn effector_update(&mut self, e: usize, sk: &mut IkSkeleton) {
        if self.effectors[e].first_update {
            let bone = self.effectors[e].bone.clone().unwrap_or_default();
            let bp = sk.world_pos(&bone).unwrap_or([0.0; 3]);
            let ci = self.effectors[e].chain_index as usize;
            let ni = self.effectors[e].node_index as usize;
            let off = self.chains[ci].nodes[ni].offset;
            self.effectors[e].animated_position = vadd(bp, off);
            self.effectors[e].first_update = false;
        }
        let (pos, plane_off) = self.effector_get_position(e, sk);
        let pw = self.effectors[e].pos_w;
        let target = self.effectors[e].position;
        let new = vlerp(pos, target, pw);
        *self.node_pos_mut(e) = new;
        self.effectors[e].plane_rotation_offset = plane_off;
        if self.effectors[e].effect_child_nodes {
            let nchild = self.effectors[e].child_chain_indexes.len();
            let base = self.node_pos(e);
            for k in 0..nchild {
                let ci = self.effectors[e].child_chain_indexes[k] as usize;
                let ni = self.effectors[e].child_node_indexes[k] as usize;
                let cur = self.chains[ci].nodes[ni].solver_position;
                let tgt = vadd(base, self.effectors[e].local_positions[k]);
                self.chains[ci].nodes[ni].solver_position = vlerp(cur, tgt, pw);
            }
        }
    }

    fn effector_plane_rotation(&self, e: usize) -> Quat {
        let p1 = node_solver_pos(
            &self.chains,
            self.effectors[e].plane1_chain_index,
            self.effectors[e].plane1_node_index,
        );
        let p2 = node_solver_pos(
            &self.chains,
            self.effectors[e].plane2_chain_index,
            self.effectors[e].plane2_node_index,
        );
        let p3 = node_solver_pos(
            &self.chains,
            self.effectors[e].plane3_chain_index,
            self.effectors[e].plane3_node_index,
        );
        let vector = vsub(p2, p1);
        if vis_zero(vector) {
            return IDENTITY;
        }
        qlook_rotation(vector, vsub(p3, p1))
    }

    fn effector_get_position(&self, e: usize, sk: &mut IkSkeleton) -> (V3, Quat) {
        if !self.effectors[e].is_end_effector {
            return (self.node_pos(e), IDENTITY);
        }
        if self.effectors[e].maintain_relative_position_weight <= 0.0 {
            return (self.effectors[e].animated_position, IDENTITY);
        }
        let bone = self.effectors[e].bone.clone().unwrap_or_default();
        let p1 = self.effectors[e].plane_bone1.clone().unwrap_or_default();
        let bp = sk.world_pos(&bone).unwrap_or([0.0; 3]);
        let pp1 = sk.world_pos(&p1).unwrap_or([0.0; 3]);
        let vector2 = vsub(bp, pp1);
        let mut plane_offset = qmul(self.effector_plane_rotation(e), qinv(self.effectors[e].animated_plane_rotation));
        let np1 = node_solver_pos(
            &self.chains,
            self.effectors[e].plane1_chain_index,
            self.effectors[e].plane1_node_index,
        );
        let vector = vadd(np1, qrot(plane_offset, vector2));
        plane_offset = qlerp(
            IDENTITY,
            plane_offset,
            self.effectors[e].maintain_relative_position_weight,
        );
        let ci = self.effectors[e].chain_index as usize;
        let ni = self.effectors[e].node_index as usize;
        let off = self.chains[ci].nodes[ni].offset;
        let res = vlerp(
            self.effectors[e].animated_position,
            vadd(vector, off),
            self.effectors[e].maintain_relative_position_weight,
        );
        (res, plane_offset)
    }

    fn pull_body(&mut self, sk: &mut IkSkeleton) {
        if self.iterations >= 1 && (self.pull_body_vertical != 0.0 || self.pull_body_horizontal != 0.0) {
            let body_offset = self.get_body_offset(sk);
            let off = vadd(
                extract_vertical(body_offset, self.root_up, self.pull_body_vertical),
                extract_horizontal(body_offset, self.root_up, self.pull_body_horizontal),
            );
            self.effectors[0].position_offset = vadd(self.effectors[0].position_offset, off);
        }
    }

    fn get_body_offset(&self, sk: &mut IkSkeleton) -> V3 {
        let mut vector = [0.0; 3];
        let lw = self.effectors[5].position_weight.clamp(0.0, 1.0);
        vector = vadd(vector, vmul(self.get_hand_body_pull(5, 1, [0.0; 3], sk), lw));
        let rw = self.effectors[6].position_weight.clamp(0.0, 1.0);
        vadd(vector, vmul(self.get_hand_body_pull(6, 2, vector, sk), rw))
    }

    fn get_hand_body_pull(&self, e: usize, arm: usize, offset: V3, sk: &mut IkSkeleton) -> V3 {
        let a0 = self.chains[arm].nodes[0].name.clone();
        let ap = sk.world_pos(&a0).unwrap_or([0.0; 3]);
        let vector = vsub(self.effectors[e].position, vadd(ap, offset));
        let num = self.chains[arm].nodes[0].length + self.chains[arm].nodes[1].length;
        let mag = vlen(vector);
        if mag < num {
            return [0.0; 3];
        }
        let num2 = mag - num;
        vmul(vdiv(vector,mag),num2)
    }

    fn chain_read_pose(&mut self, i: usize, sk: &mut IkSkeleton, full_body: bool) {
        if !self.chains[i].initiated {
            return;
        }
        let names: Vec<String> = self.chains[i].nodes.iter().map(|n| n.name.clone()).collect();
        for (k, name) in names.iter().enumerate() {
            let p = sk.world_pos(name).unwrap_or([0.0; 3]);
            self.chains[i].nodes[k].solver_position = vadd(p, self.chains[i].nodes[k].offset);
        }
        self.calculate_bone_lengths(i, sk);
        if !full_body {
            return;
        }
        if i == 0 {
            for k in 0..self.child_constraints.len() {
                self.child_constraint_pre_solve(k, sk);
            }
        }
        let nch = self.chains[i].children.len();
        if nch != 0 {
            let last = self.chains[i].nodes.len() - 1;
            let mut num = self.chains[i].nodes[last].effector_position_weight;
            for child in self.chains[i].children.clone() {
                num += self.chains[child].nodes[0].effector_position_weight * self.chains[child].pull;
            }
            num = num.max(1.0);
            for (k, child) in self.chains[i].children.clone().iter().enumerate() {
                let v = self.chains[*child].nodes[0].effector_position_weight
                    * self.chains[*child].pull
                    / num;
                self.chains[i].cross_fades[k] = v;
            }
        }
        let mut pull_parent_sum = 0.0;
        for child in self.chains[i].children.clone() {
            pull_parent_sum += self.chains[child].pull;
        }
        self.chains[i].pull_parent_sum = pull_parent_sum.max(1.0);
        let n = self.chains[i].nodes.len();
        self.chains[i].reach_force = if n == 3 {
            self.chains[i].reach * self.chains[i].nodes[2].effector_position_weight.clamp(0.0, 1.0)
        } else {
            0.0
        };
        if self.chains[i].push > 0.0 && n > 1 {
            let a = self.chains[i].nodes[0].name.clone();
            let b = self.chains[i].nodes[n - 1].name.clone();
            let pa = sk.world_pos(&a).unwrap_or([0.0; 3]);
            let pb = sk.world_pos(&b).unwrap_or([0.0; 3]);
            self.chains[i].distance = vlen(vsub(pa, pb));
        }
    }

    fn child_constraint_pre_solve(&mut self, k: usize, sk: &mut IkSkeleton) {
        let c1 = self.child_constraints[k].chain1_index as usize;
        let c2 = self.child_constraints[k].chain2_index as usize;
        let a = self.chains[c1].nodes[0].name.clone();
        let b = self.chains[c2].nodes[0].name.clone();
        let pa = sk.world_pos(&a).unwrap_or([0.0; 3]);
        let pb = sk.world_pos(&b).unwrap_or([0.0; 3]);
        self.child_constraints[k].nominal_distance = vlen(vsub(pa, pb));
        self.child_constraints[k].is_rigid =
            self.child_constraints[k].push_elasticity <= 0.0 && self.child_constraints[k].pull_elasticity <= 0.0;
        if self.child_constraints[k].is_rigid {
            let num = self.chains[c1].pull - self.chains[c2].pull;
            self.child_constraints[k].cross_fade = 1.0 - (0.5 + num * 0.5);
        } else {
            self.child_constraints[k].cross_fade = 0.5;
        }
        self.child_constraints[k].inverse_cross_fade = 1.0 - self.child_constraints[k].cross_fade;
    }

    fn child_constraint_solve(&mut self, k: usize) {
        if self.child_constraints[k].push_elasticity >= 1.0
            && self.child_constraints[k].pull_elasticity >= 1.0
        {
            return;
        }
        let c1 = self.child_constraints[k].chain1_index as usize;
        let c2 = self.child_constraints[k].chain2_index as usize;
        let p1 = self.chains[c1].nodes[0].solver_position;
        let p2 = self.chains[c2].nodes[0].solver_position;
        let vector = vsub(p2, p1);
        let mag = vlen(vector);
        let nominal = self.child_constraints[k].nominal_distance;
        if mag != nominal && mag != 0.0 {
            let mut num = 1.0;
            if !self.child_constraints[k].is_rigid {
                let num2 = if mag > nominal {
                    self.child_constraints[k].pull_elasticity
                } else {
                    self.child_constraints[k].push_elasticity
                };
                num = 1.0 - num2;
            }
            num = ((num as f64)*(1.0-(nominal as f64)/(mag as f64)))as f32;
            let vector2 = vmul(vector, num);
            let cf = self.child_constraints[k].cross_fade;
            let icf = self.child_constraints[k].inverse_cross_fade;
            self.chains[c1].nodes[0].solver_position =
                vadd(self.chains[c1].nodes[0].solver_position, vmul(vector2, cf));
            self.chains[c2].nodes[0].solver_position =
                vsub(self.chains[c2].nodes[0].solver_position, vmul(vector2, icf));
        }
    }

    fn solve_child_constraints(&mut self) {
        for k in 0..self.child_constraints.len() {
            self.child_constraint_solve(k);
        }
    }

    // --- Solve --------------------------------------------------------------

    fn chain_push(&mut self, i: usize) -> V3 {
        if !self.chains[i].initiated {
            return [0.0; 3];
        }
        let mut zero = [0.0; 3];
        for child in self.chains[i].children.clone() {
            let v = self.chain_push(child);
            zero = vadd(zero, vmul(v, self.chains[child].push_parent));
        }
        let last = self.chains[i].nodes.len() - 1;
        self.chains[i].nodes[last].solver_position =
            vadd(self.chains[i].nodes[last].solver_position, zero);
        if self.chains[i].nodes.len() < 2 {
            return [0.0; 3];
        }
        if self.chains[i].push <= 0.0 {
            return [0.0; 3];
        }
        let p0 = self.chains[i].nodes[0].solver_position;
        let p2 = self.chains[i].nodes[2].solver_position;
        let vector = vsub(p2, p0);
        let mag = vlen(vector);
        if mag == 0.0 {
            return [0.0; 3];
        }
        let mut num = (1.0-(mag as f64)/(self.chains[i].distance as f64))as f32;
        if num <= 0.0 {
            return [0.0; 3];
        }
        match self.chains[i].push_smoothing {
            Smoothing::Exponential => num *= num,
            Smoothing::Cubic => num = num * num * num,
            Smoothing::None => {}
        }
        let vector2 = vmul(vmul(vector, -num), self.chains[i].push);
        self.chains[i].nodes[0].solver_position =
            vadd(self.chains[i].nodes[0].solver_position, vector2);
        vector2
    }

    fn chain_reach(&mut self, i: usize) {
        if !self.chains[i].initiated {
            return;
        }
        for child in self.chains[i].children.clone() {
            self.chain_reach(child);
        }
        if self.chains[i].reach_force <= 0.0 {
            return;
        }
        let p0 = self.chains[i].nodes[0].solver_position;
        let p2 = self.chains[i].nodes[2].solver_position;
        let vector = vsub(p2, p0);
        if vis_zero(vector) {
            return;
        }
        let mag = vlen(vector);
        let vector2 = vmul(vdiv(vector,mag),self.chains[i].length);
        let mut num = (mag / self.chains[i].length)
            .clamp(1.0 - self.chains[i].reach_force, 1.0 + self.chains[i].reach_force)
            - 1.0;
        num = (num + self.chains[i].reach_force).clamp(-1.0, 1.0);
        match self.chains[i].reach_smoothing {
            Smoothing::Exponential => num *= num,
            Smoothing::Cubic => num = num * num * num,
            Smoothing::None => {}
        }
        let vector3 = vmul(vector2, num.clamp(0.0, mag));
        let epw = self.chains[i].nodes[0].effector_position_weight;
        self.chains[i].nodes[0].solver_position =
            vadd(self.chains[i].nodes[0].solver_position, vmul(vector3, 1.0 - epw));
        self.chains[i].nodes[2].solver_position =
            vadd(self.chains[i].nodes[2].solver_position, vector3);
    }

    fn chain_solve_trigonometric(&mut self, i: usize, sk: &mut IkSkeleton, calculate_bend_direction: bool) {
        if !self.chains[i].initiated {
            return;
        }
        for child in self.chains[i].children.clone() {
            self.chain_solve_trigonometric(child, sk, calculate_bend_direction);
        }
        if self.chains[i].nodes.len() == 3 {
            let p0 = self.chains[i].nodes[0].solver_position;
            let p2 = self.chains[i].nodes[2].solver_position;
            let vector = vsub(p2, p0);
            let mag = vlen(vector);
            if mag != 0.0 {
                let num = mag.clamp(0.0, self.chains[i].length * 0.99999);
                let direction = vmul(vdiv(vector,mag),num);
                let bend_direction = if calculate_bend_direction && self.bend_constraints[i].initiated {
                    let w = self.ik_position_weight;
                    self.bend_constraints[i].get_dir(&self.chains, sk, w)
                } else {
                    vsub(self.chains[i].nodes[1].solver_position, p0)
                };
                let dir = self.get_dir_to_bend_point(i, direction, bend_direction, num);
                self.chains[i].nodes[1].solver_position = vadd(p0, dir);
            }
        }
    }

    fn get_dir_to_bend_point(&self, i: usize, direction: V3, bend_direction: V3, direction_magnitude: f32) -> V3 {
        let (y,num)=triangle_bend_components(direction_magnitude,self.chains[i].sqr_mag1,self.chains[i].sqr_mag_dif);
        if vis_zero(direction) {
            return [0.0; 3];
        }
        qrot(qlook_rotation(direction, bend_direction), [0.0, y, num])
    }

    fn chain_stage1(&mut self, i: usize, sk: &mut IkSkeleton) {
        for child in self.chains[i].children.clone() {
            self.chain_stage1(child, sk);
        }
        if self.chains[i].children.is_empty() {
            let p = *self.chains[i].nodes.last().map(|n| &n.solver_position).unwrap();
            self.forward_reach(i, p);
            return;
        }
        let last = self.chains[i].nodes.len() - 1;
        let mut solver_position = self.chains[i].nodes[last].solver_position;
        if i==0{self.solve_child_constraints();}
        for child in self.chains[i].children.clone() {
            let mut vector = self.chains[child].nodes[0].solver_position;
            if self.chains[child].root_length > 0.0 {
                vector = solve_fabrik_joint(
                    self.chains[i].nodes[last].solver_position,
                    vector,
                    self.chains[child].root_length,
                );
            }
            if self.chains[i].pull_parent_sum > 0.0 {
                let f = self.chains[child].pull / self.chains[i].pull_parent_sum;
                solver_position = vadd(
                    solver_position,
                    vmul(vsub(vector, self.chains[i].nodes[last].solver_position), f),
                );
            }
        }
        let pin = self.chains[i].pin;
        let pr = self.chains[i].nodes[last].solver_position;
        self.forward_reach(i, vlerp(solver_position, pr, pin));
    }

    fn forward_reach(&mut self, i: usize, position: V3) {
        let n = self.chains[i].nodes.len();
        self.chains[i].nodes[n - 1].solver_position = position;
        for k in (0..n - 1).rev() {
            let a = self.chains[i].nodes[k].solver_position;
            let b = self.chains[i].nodes[k + 1].solver_position;
            let len = self.chains[i].nodes[k].length;
            self.chains[i].nodes[k].solver_position = solve_fabrik_joint(a, b, len);
        }
    }

    fn backward_reach(&mut self, i: usize, position: V3) {
        let mut position = position;
        if self.chains[i].root_length > 0.0 {
            position = solve_fabrik_joint(
                self.chains[i].nodes[0].solver_position,
                position,
                self.chains[i].root_length,
            );
        }
        self.chains[i].nodes[0].solver_position = position;
        let n = self.chains[i].nodes.len();
        for k in 1..n {
            let a = self.chains[i].nodes[k].solver_position;
            let b = self.chains[i].nodes[k - 1].solver_position;
            let len = self.chains[i].nodes[k - 1].length;
            self.chains[i].nodes[k].solver_position = solve_fabrik_joint(a, b, len);
        }
    }

    fn solve_constraint_systems(&mut self, i: usize) {
        if i==0{self.solve_child_constraints();}
        for (k, child) in self.chains[i].children.clone().iter().enumerate() {
            let cross = self.chains[i].cross_fades[k];
            let root_len = self.chains[*child].root_length;
            let last = self.chains[i].nodes.len() - 1;
            let p1 = self.chains[i].nodes[last].solver_position;
            let p2 = self.chains[*child].nodes[0].solver_position;
            let vector = vsub(p2, p1);
            let mag = vlen(vector);
            if root_len != mag && mag != 0.0 {
                let vector2 = vmul(vector,(1.0-(root_len as f64)/(mag as f64))as f32);
                self.chains[i].nodes[last].solver_position =
                    vadd(self.chains[i].nodes[last].solver_position, vmul(vector2, cross));
                self.chains[*child].nodes[0].solver_position =
                    vsub(self.chains[*child].nodes[0].solver_position, vmul(vector2, 1.0 - cross));
            }
        }
    }

    fn chain_stage2(&mut self, i: usize, position: V3) {
        self.backward_reach(i, position);
        let num = self.iterations.clamp(2, 4);
        // FullBodyBiped stores these constraints on chain[0], not its limbs.
        if i==0 && !self.child_constraints.is_empty() {
            for _ in 0..num {
                self.solve_constraint_systems(i);
            }
        }
        let last = self.chains[i].nodes.len() - 1;
        let p = self.chains[i].nodes[last].solver_position;
        for child in self.chains[i].children.clone() {
            self.chain_stage2(child, p);
        }
    }

    fn solve(&mut self, sk: &mut IkSkeleton) {
        if self.iterations > 0 {
            for _ in 0..self.iterations {
                for e in 0..self.effectors.len() {
                    if self.effectors[e].is_end_effector {
                        self.effector_update(e, sk);
                    }
                }
                self.chain_push(0);
                self.chain_reach(0);
                for e in 0..self.effectors.len() {
                    if !self.effectors[e].is_end_effector {
                        self.effector_update(e, sk);
                    }
                }
                self.chain_solve_trigonometric(0, sk, false);
                self.chain_stage1(0, sk);
                for e in 0..self.effectors.len() {
                    if !self.effectors[e].is_end_effector {
                        self.effector_update(e, sk);
                    }
                }
                let p = self.chains[0].nodes[0].solver_position;
                self.chain_stage2(0, p);
            }
        }
        for e in 0..self.effectors.len() {
            if self.effectors[e].is_end_effector {
                self.effector_update(e, sk);
            }
        }
        self.apply_bend_constraints(sk);
    }

    /// `IKSolverFullBodyBiped.ApplyBendConstraints` + base.
    fn apply_bend_constraints(&mut self, sk: &mut IkSkeleton) {
        if self.iterations > 0 {
            let lh = self.effector_plane_offset(5);
            let rh = self.effector_plane_offset(6);
            let lf = self.effector_plane_offset(7);
            let rf = self.effector_plane_offset(8);
            if self.bend_constraints.len() >= 5 {
                self.bend_constraints[1].rotation_offset = lh;
                self.bend_constraints[2].rotation_offset = rh;
                self.bend_constraints[3].rotation_offset = lf;
                self.bend_constraints[4].rotation_offset = rf;
            }
        } else {
            let bone = self.effectors[0].bone.clone().unwrap_or_default();
            let bp = sk.world_pos(&bone).unwrap_or([0.0; 3]);
            let e0 = &self.effectors[0];
            self.offset = vlerp(
                e0.position_offset,
                vsub(e0.position, vadd(bp, e0.position_offset)),
                e0.position_weight,
            );
            let off = self.offset;
            for i in 0..5 {
                if self.effectors[i].chain_index >= 0 {
                    let cur = *self.node_pos_mut(i);
                    *self.node_pos_mut(i) = vadd(cur, off);
                }
            }
        }
        self.chain_solve_trigonometric(0, sk, true);
    }

    fn effector_plane_offset(&self, e: usize) -> Quat {
        self.effectors[e].plane_rotation_offset
    }

    /// `IKSolverFullBody.WritePose`.
    pub fn write_pose(&mut self, sk: &mut IkSkeleton) {
        if self.ik_position_weight <= 0.0 {
            return;
        }
        if self.iterations == 0 {
            if let Some(b0) = self.spine_mapping.spine_bones.first().cloned() {
                let p = sk.world_pos(&b0).unwrap_or([0.0; 3]);
                sk.set_world_pos(&b0, vadd(p, self.offset));
            }
        }
        if self.iterations > 0 {
            self.spine_mapping.write_pose(&self.chains, sk);
            let w = self.ik_position_weight;
            for i in 0..self.bone_mappings.len() {
                self.bone_mappings[i].write_pose(sk, w);
            }
        }
        let fb = self.iterations > 0;
        let w = self.ik_position_weight;
        for i in 0..self.limb_mappings.len() {
            self.limb_mappings[i].write_pose(&self.chains, sk, fb, w);
        }
    }
}

// ---------------------------------------------------------------------------
// Prefab `FullBodyBipedIK` config (parsed from `oo_base.unity3d`) + MotionIK
// ---------------------------------------------------------------------------

/// Serialized `FBIKChain` (subset we need; solver state is recomputed).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct PrefabChain {
    pub pin: f32,
    pub pull: f32,
    pub push: f32,
    pub push_parent: f32,
    pub reach: f32,
    pub nodes: Vec<String>,
    pub children: Vec<usize>,
    pub child_constraints: Vec<(f32, f32, String, String)>,
}

/// Serialized `IKEffector` (subset).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct PrefabEffector {
    pub bone: Option<String>,
    pub target: Option<String>,
    pub position_weight: f32,
    pub rotation_weight: f32,
    pub effect_child_nodes: bool,
    pub maintain_relative_position_weight: f32,
    pub child_bones: Vec<String>,
    pub plane_bone1: Option<String>,
    pub plane_bone2: Option<String>,
    pub plane_bone3: Option<String>,
}

/// Serialized `IKMappingLimb` (subset).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct PrefabLimbMapping {
    pub parent_bone: Option<String>,
    pub bone1: Option<String>,
    pub bone2: Option<String>,
    pub bone3: Option<String>,
    pub maintain_rotation_weight: f32,
    pub weight: f32,
}

/// Everything needed to rebuild the game's `IKSolverFullBodyBiped` faithfully.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct PrefabIkConfig {
    pub references: BipedReferences,
    pub frame_bones: Vec<Option<String>>,
    /// BaseCorrect.Info.ProcOrderType: First=0, Second=1, Last=2.
    pub frame_orders: Vec<i32>,
    pub root_node: Option<String>,
    pub iterations: i32,
    pub spine_stiffness: f32,
    pub pull_body_vertical: f32,
    pub pull_body_horizontal: f32,
    pub chains: Vec<PrefabChain>,
    pub effectors: Vec<PrefabEffector>,
    pub spine_bones: Vec<String>,
    pub spine_left_upper_arm: Option<String>,
    pub spine_right_upper_arm: Option<String>,
    pub spine_left_thigh: Option<String>,
    pub spine_right_thigh: Option<String>,
    pub bone_mappings: Vec<(Option<String>, f32)>,
    pub limb_mappings: Vec<PrefabLimbMapping>,
}

impl IKSolverFullBodyBiped {
    /// `IKSolverFullBodyBiped.SetToReferences`-equivalent from serialized data.
    pub fn from_prefab(cfg: &PrefabIkConfig, _sk: &mut IkSkeleton) -> Self {
        let mut s = Self::default();
        s.iterations = if cfg.iterations > 0 { cfg.iterations } else { 4 };
        s.spine_stiffness = cfg.spine_stiffness;
        s.pull_body_vertical = cfg.pull_body_vertical;
        s.pull_body_horizontal = cfg.pull_body_horizontal;
        s.root = cfg.references.root.clone();
        s.root_node = cfg.root_node.clone();
        for pc in &cfg.chains {
            let mut ch = FBIKChain::default();
            ch.pin = pc.pin;
            ch.pull = pc.pull;
            ch.push = pc.push;
            ch.push_parent = pc.push_parent;
            ch.reach = pc.reach;
            let names: Vec<&str> = pc.nodes.iter().map(|x| x.as_str()).collect();
            ch.set_nodes(&names);
            ch.children = pc.children.clone();
            s.chains.push(ch);
        }
        s.bend_constraints = vec![IKConstraintBend::default(); cfg.chains.len()];
        if let Some(c0) = cfg.chains.first() {
            for (push, pull, b1, b2) in &c0.child_constraints {
                let mut cc = ChildConstraint::new(b1, b2);
                cc.push_elasticity = *push;
                cc.pull_elasticity = *pull;
                s.child_constraints.push(cc);
            }
        }
        s.effectors = cfg
            .effectors
            .iter()
            .map(|pe| {
                let mut e = IKEffector::default();
                e.bone = pe.bone.clone();
                e.target = pe.target.clone();
                e.position_weight = pe.position_weight;
                e.rotation_weight = pe.rotation_weight;
                e.effect_child_nodes = pe.effect_child_nodes;
                e.maintain_relative_position_weight = pe.maintain_relative_position_weight;
                e.child_bones = pe.child_bones.clone();
                e.plane_bone1 = pe.plane_bone1.clone();
                e.plane_bone2 = pe.plane_bone2.clone();
                e.plane_bone3 = pe.plane_bone3.clone();
                e
            })
            .collect();
        s.spine_mapping.iterations = 3;
        let spine_refs: Vec<&str> = cfg.spine_bones.iter().map(|x| x.as_str()).collect();
        s.spine_mapping.set_bones(
            &spine_refs,
            cfg.spine_left_upper_arm.as_deref().unwrap_or(""),
            cfg.spine_right_upper_arm.as_deref().unwrap_or(""),
            cfg.spine_left_thigh.as_deref().unwrap_or(""),
            cfg.spine_right_thigh.as_deref().unwrap_or(""),
        );
        s.bone_mappings = cfg
            .bone_mappings
            .iter()
            .map(|(b, w)| {
                let mut m = IKMappingBone::default();
                m.bone = b.clone();
                m.maintain_rotation_weight = *w;
                m
            })
            .collect();
        s.limb_mappings = cfg
            .limb_mappings
            .iter()
            .map(|lm| {
                let mut m = IKMappingLimb::default();
                m.set_bones(
                    lm.bone1.as_deref().unwrap_or(""),
                    lm.bone2.as_deref().unwrap_or(""),
                    lm.bone3.as_deref().unwrap_or(""),
                    lm.parent_bone.as_deref(),
                );
                m.maintain_rotation_weight = lm.maintain_rotation_weight;
                m.weight = lm.weight;
                m
            })
            .collect();
        s
    }

    /// `MotionIK.Calc` + `LinkIK` for the given state. `shape_body` is the
    /// card's body shape vector (`GetShapeBodyValue` index).
    pub fn link_ik(
        &mut self,
        data: &MotionIkData,
        state_name: &str,
        shape_body: &[f32],
        body_root: &str,
        sk: &mut IkSkeleton,
    ) {
        self.body_bone_rot = sk.world_rot(body_root).unwrap_or(IDENTITY);
        let state = data.state(state_name).cloned();
        let eidx = [5usize, 6, 7, 8];
        let bidx = [1usize, 2, 3, 4];
        let tnames = ["cf_t_hand_L", "cf_t_hand_R", "cf_t_leg_L", "cf_t_leg_R"];
        for index in 0..4 {
            let part = state
                .as_ref()
                .and_then(|s| s.parts.get(index).cloned())
                .unwrap_or_default();
            let e = eidx[index];
            let bone = part.target.clone();
            let hands_off = index < 2 && std::env::var("OK_FINALIK_HANDS_OFF").is_ok();
            if hands_off {
                self.effectors[e].position_weight = 0.0;
                self.effectors[e].rotation_weight = 0.0;
                self.target_sync.remove(&tnames[index].to_string());
            } else if !bone.is_empty() && sk.has(&bone) {
                let frame = state
                    .as_ref()
                    .and_then(|s| s.frame((index as i32) * 2));
                let (pos, ang) = correct_shape_values(shape_body, frame);
                self.target_sync.insert(
                    tnames[index].to_string(),
                    (bone.clone(), pos, crate::pose::euler_deg_to_quat(ang)),
                );
                self.effectors[e].target = Some(tnames[index].to_string());
                self.effectors[e].position_weight = part.weight_pos;
                self.effectors[e].rotation_weight = part.weight_ang;
            } else {
                self.effectors[e].position_weight = 0.0;
                self.effectors[e].rotation_weight = 0.0;
            }
            let bend = state
                .as_ref()
                .and_then(|s| s.bends.get(index).cloned())
                .unwrap_or_default();
            if self.bend_constraints.len() > bidx[index] {
                self.bend_constraints[bidx[index]].bend_goal = None;
                self.bend_constraints[bidx[index]].weight = 0.0;
                if !bend.chein.is_empty() && sk.has(&bend.chein) {
                    self.bend_constraints[bidx[index]].bend_goal = Some(bend.chein.clone());
                    self.bend_constraints[bidx[index]].weight = bend.weight;
                }
            }
        }
    }
}

// --- MotionIKData (`custom_pose`) ------------------------------------------

#[derive(Clone, Debug, Default)]
pub struct PosAng {
    pub pos: V3,
    pub ang: V3,
}
#[derive(Clone, Debug, Default)]
pub struct MotionShape {
    pub shape_no: i32,
    pub small: PosAng,
    pub large: PosAng,
}
#[derive(Clone, Debug, Default)]
pub struct MotionFrame {
    pub frame_no: i32,
    pub edit_no: i32,
    pub shapes: Vec<MotionShape>,
}
#[derive(Clone, Debug, Default)]
pub struct MotionPart2 {
    pub sex: i32,
    pub target: String,
    pub weight_pos: f32,
    pub weight_ang: f32,
}
#[derive(Clone, Debug, Default)]
pub struct MotionPart3 {
    pub chein: String,
    pub weight: f32,
}
#[derive(Clone, Debug, Default)]
pub struct MotionState {
    pub name: String,
    pub parts: Vec<MotionPart2>,
    pub bends: Vec<MotionPart3>,
    pub frames: Vec<MotionFrame>,
}
impl MotionState {
    pub fn frame(&self, no: i32) -> Option<&MotionFrame> {
        self.frames.iter().find(|f| f.frame_no == no)
    }
}
#[derive(Clone, Debug, Default)]
pub struct MotionIkData {
    pub states: Vec<MotionState>,
}
impl MotionIkData {
    pub fn state(&self, name: &str) -> Option<&MotionState> {
        self.states.iter().find(|s| s.name == name)
    }
}

/// Parse the `custom_pose` TextAsset (`MotionIKData.Read`).
pub fn read_motionik_bytes(bytes: &[u8]) -> MotionIkData {
    let mut p = 0usize;
    let rd_i32 = |b: &[u8], p: &mut usize| -> i32 {
        let v = i32::from_le_bytes([b[*p], b[*p + 1], b[*p + 2], b[*p + 3]]);
        *p += 4;
        v
    };
    let rd_f32 = |b: &[u8], p: &mut usize| -> f32 {
        let v = f32::from_le_bytes([b[*p], b[*p + 1], b[*p + 2], b[*p + 3]]);
        *p += 4;
        v
    };
    let rd_str = |b: &[u8], p: &mut usize| -> String {
        let mut len = 0usize;
        let mut shift = 0u32;
        loop {
            let byte = b[*p];
            *p += 1;
            len |= ((byte & 0x7f) as usize) << shift;
            if byte & 0x80 == 0 {
                break;
            }
            shift += 7;
        }
        let s = String::from_utf8_lossy(&b[*p..*p + len]).to_string();
        *p += len;
        s
    };
    let mut data = MotionIkData::default();
    if bytes.len() < 4 {
        return data;
    }
    let nstates = rd_i32(bytes, &mut p);
    for _ in 0..nstates.max(0) {
        let mut st = MotionState::default();
        st.name = rd_str(bytes, &mut p);
        st.parts = Vec::new();
        st.bends = Vec::new();
        for _ in 0..4 {
            let sex = rd_i32(bytes, &mut p);
            let target = rd_str(bytes, &mut p);
            let wp = rd_f32(bytes, &mut p);
            let wa = rd_f32(bytes, &mut p);
            let chein = rd_str(bytes, &mut p);
            let cw = rd_f32(bytes, &mut p);
            st.parts.push(MotionPart2 {
                sex,
                target,
                weight_pos: wp,
                weight_ang: wa,
            });
            st.bends.push(MotionPart3 { chein, weight: cw });
        }
        let nframes = rd_i32(bytes, &mut p);
        for _ in 0..nframes.max(0) {
            let frame_no = rd_i32(bytes, &mut p);
            let edit_no = rd_i32(bytes, &mut p);
            let nshapes = rd_i32(bytes, &mut p);
            let mut shapes = Vec::new();
            for _ in 0..nshapes.max(0) {
                let shape_no = rd_i32(bytes, &mut p);
                let mut small = PosAng::default();
                let mut large = PosAng::default();
                small.pos = [rd_f32(bytes, &mut p), rd_f32(bytes, &mut p), rd_f32(bytes, &mut p)];
                small.ang = [rd_f32(bytes, &mut p), rd_f32(bytes, &mut p), rd_f32(bytes, &mut p)];
                large.pos = [rd_f32(bytes, &mut p), rd_f32(bytes, &mut p), rd_f32(bytes, &mut p)];
                large.ang = [rd_f32(bytes, &mut p), rd_f32(bytes, &mut p), rd_f32(bytes, &mut p)];
                shapes.push(MotionShape { shape_no, small, large });
            }
            st.frames.push(MotionFrame {
                frame_no,
                edit_no,
                shapes,
            });
        }
        data.states.push(st);
    }
    data
}

fn inverse_lerp(a: f32, b: f32, v: f32) -> f32 {
    if (b - a).abs() < 1e-12 {
        0.0
    } else {
        ((v - a) / (b - a)).clamp(0.0, 1.0)
    }
}

fn lerp_f(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

/// Unity `Mathf.LerpAngle`.
fn lerp_angle(a: f32, b: f32, t: f32) -> f32 {
    let mut delta = (b - a).rem_euclid(360.0);
    if delta > 180.0 {
        delta -= 360.0;
    }
    a + delta * t
}

/// `MotionIK.GetShapeLerpPositionValue`.
pub fn get_shape_lerp_position_value(shape: f32, min: V3, max: V3) -> V3 {
    if shape < 0.5 {
        let t = inverse_lerp(0.0, 0.5, shape);
        [
            lerp_f(min[0], 0.0, t),
            lerp_f(min[1], 0.0, t),
            lerp_f(min[2], 0.0, t),
        ]
    } else {
        let t = inverse_lerp(0.5, 1.0, shape);
        [
            lerp_f(0.0, max[0], t),
            lerp_f(0.0, max[1], t),
            lerp_f(0.0, max[2], t),
        ]
    }
}

/// `MotionIK.GetShapeLerpAngleValue`.
pub fn get_shape_lerp_angle_value(shape: f32, min: V3, max: V3) -> V3 {
    if shape < 0.5 {
        let t = inverse_lerp(0.0, 0.5, shape);
        [
            lerp_angle(min[0], 0.0, t),
            lerp_angle(min[1], 0.0, t),
            lerp_angle(min[2], 0.0, t),
        ]
    } else {
        let t = inverse_lerp(0.5, 1.0, shape);
        [
            lerp_angle(0.0, max[0], t),
            lerp_angle(0.0, max[1], t),
            lerp_angle(0.0, max[2], t),
        ]
    }
}

pub(crate) fn correct_shape_values(shape_body: &[f32], frame: Option<&MotionFrame>) -> (V3, V3) {
    let mut pos = [0.0; 3];
    let mut ang = [0.0; 3];
    if let Some(f) = frame {
        for s in &f.shapes {
            let v = shape_body
                .get(s.shape_no.max(0) as usize)
                .copied()
                .unwrap_or(0.5);
            let p = get_shape_lerp_position_value(v, s.small.pos, s.large.pos);
            let a = get_shape_lerp_angle_value(v, s.small.ang, s.large.ang);
            for k in 0..3 {
                pos[k] += p[k];
                ang[k] += a[k];
            }
        }
    }
    (pos, ang)
}

/// Load the body `FullBodyBipedIK` config + `custom_pose` data (cached).
#[derive(Clone, Debug)]
pub struct FinalIkSetup {
    pub config: PrefabIkConfig,
    pub motion: MotionIkData,
}

static SETUP_CACHE: std::sync::OnceLock<
    std::sync::Mutex<Vec<(std::path::PathBuf, Option<FinalIkSetup>)>>,
> = std::sync::OnceLock::new();

pub fn load_setup(game_root: &std::path::Path) -> Option<FinalIkSetup> {
    let cache = SETUP_CACHE.get_or_init(|| std::sync::Mutex::new(Vec::new()));
    if let Ok(g) = cache.lock() {
        for (p, s) in g.iter() {
            if p == game_root {
                return s.clone();
            }
        }
    }
    let parsed = (|| {
        let config = crate::unity::parse_prefab_ik(game_root)?;
        let path = game_root.join("abdata/custom/custompose.unity3d");
        let mut motion = MotionIkData::default();
        for (name, bytes) in crate::list::text_assets_from_bundle(&path) {
            if name == "custom_pose" {
                motion = read_motionik_bytes(&bytes);
                break;
            }
        }
        if motion.states.is_empty() {
            return None;
        }
        Some(FinalIkSetup { config, motion })
    })();
    if let Ok(mut g) = cache.lock() {
        g.push((game_root.to_path_buf(), parsed.clone()));
    }
    parsed
}

/// Apply the maker `MotionIK` + `FullBodyBipedIK` solve to a posed body
/// skeleton. `body_root` is `p_cf_body_bone` (`objBodyBone`).
pub fn apply_final_ik(
    game_root: &std::path::Path,
    shape_body: &[f32],
    skel: &mut crate::pose::Skeleton,
    state_name: &str,
    body_root: &str,
) {
    let Some(setup) = load_setup(game_root) else {
        return;
    };
    let mut sk = IkSkeleton::new(skel);
    let diag = std::env::var("OK_FINALIK_DIAG").is_ok();
    if diag {
        for n in [
            "cf_j_root",
            "cf_j_hips",
            "cf_j_spine01",
            "cf_j_arm00_L",
            "cf_j_hand_L",
            "cf_j_leg03_L",
            "cf_j_thigh00_L",
            "cf_j_leg01_L",
            "cf_pv_leg_L",
            "cf_pv_elbo_L",
            "k_f_kosi02_01",
        ] {
            eprintln!(
                "FINALIK pre {n} has={} local={:?} parent={:?} pos={:?}",
                sk.has(n),
                sk.local_pos(n),
                sk.parent_name(n),
                sk.world_pos(n)
            );
        }
    }
    let mut solver = IKSolverFullBodyBiped::from_prefab(&setup.config, &mut sk);
    solver.link_ik(&setup.motion, state_name, shape_body, body_root, &mut sk);
    if diag {
        for (i, e) in solver.effectors.iter().enumerate() {
            eprintln!(
                "FINALIK eff{i} bone={:?} target={:?} posW={} rotW={}",
                e.bone, e.target, e.position_weight, e.rotation_weight
            );
        }
        for (k, v) in solver.target_sync.iter() {
            eprintln!("FINALIK sync {k} -> {v:?}");
        }
    }
    solver.update(&mut sk);
    if diag {
        for n in [
            "cf_j_hips",
            "cf_j_spine01",
            "cf_j_spine02",
            "cf_j_spine03",
            "cf_j_neck",
            "cf_j_shoulder_L",
            "cf_j_arm00_L",
            "cf_j_forearm01_L",
            "cf_j_hand_L",
            "cf_j_leg03_L",
        ] {
            eprintln!(
                "FINALIK post {n} pos={:?} rot={:?}",
                sk.world_pos(n),
                sk.world_rot(n)
            );
        }
        for (i, c) in solver.chains.iter().enumerate() {
            let nodes: Vec<(String, V3, f32)> = c
                .nodes
                .iter()
                .map(|n| (n.name.clone(), n.solver_position, n.length))
                .collect();
            eprintln!(
                "FINALIK solved chain{i} len={:.4} sqr1={:.4} sqr2={:.4} nodes={nodes:?}",
                c.length, c.sqr_mag1, c.sqr_mag2
            );
        }
        for (i, e) in solver.effectors.iter().enumerate() {
            let ci = e.chain_index as usize;
            let ni = e.node_index as usize;
            if ci < solver.chains.len() && ni < solver.chains[ci].nodes.len() {
                eprintln!(
                    "FINALIK nodeEff{i} solverPos={:?} target={:?}",
                    solver.chains[ci].nodes[ni].solver_position, e.position
                );
            }
        }
    }
}

#[cfg(test)]
mod tests {    #[test]
    fn limb_stage2_preserves_sibling_roots(){
        use super::*;
        let mut solver=IKSolverFullBodyBiped::default();
        for side in 0..3{let mut chain=FBIKChain::default();chain.set_nodes(&["root","mid","end"]);for(j,node)in chain.nodes.iter_mut().enumerate(){node.solver_position=[side as f32*3.,j as f32,0.];node.length=1.;}solver.chains.push(chain);}
        let mut constraint=ChildConstraint::new("arm","leg");constraint.chain1_index=1;constraint.chain2_index=2;constraint.nominal_distance=1.;constraint.is_rigid=true;solver.child_constraints.push(constraint);
        let before:Vec<_>=solver.chains[2].nodes.iter().map(|n|n.solver_position).collect();
        solver.chain_stage2(1,[3.,0.,0.]);
        assert_eq!(before,solver.chains[2].nodes.iter().map(|n|n.solver_position).collect::<Vec<_>>());
    }
    use super::*;

    #[test]
    fn combined_transform_matches_split_and_original_rotation_bits(){
        let names:Vec<_>=(0..132).map(|i|format!("bone{i}")).collect();
        let bones:Vec<_>=names.iter().enumerate().map(|(i,n)|(n.as_str(),if i==0{None}else{Some(i as i64)},[0.02*i as f32,-0.031,0.015])).collect();
        let mut skeleton=mk_skel(&bones);
        for frame in 0..6{
            for (id,tf) in &mut skeleton.tfs{
                tf.rot=crate::pose::euler_deg_to_quat([*id as f32*0.13,frame as f32*0.11,-1.3]);
                tf.scale=if *id%7==0{[-1.,0.,0.75]}else{[0.99,1.,1.01]};
            }
            skeleton.tfs.get_mut(&8).unwrap().name="Duplicate".into();
            skeleton.tfs.get_mut(&90).unwrap().name="DUPLICATE".into();
            skeleton.tfs.get_mut(&132).unwrap().parent=match frame{1=>Some(9999),2=>Some(132),3=>Some(1),_=>Some(131)};
            skeleton.tfs.get_mut(&1).unwrap().parent=if frame==4{Some(131)}else{None};
            for name in ["bone131","bone0","duplicate","BONE31","missing"]{
                let expected={let mut old=IkSkeleton::new(&mut skeleton);
                    let position=old.world_pos(name);
                    let rotation=old.id_of(name).map(|id|{
                        let mut chain=Vec::new();let mut cur=Some(id);
                        for _ in 0..128{let Some(id)=cur else{break;};let Some(tf)=old.sk.tfs.get(&id)else{break;};chain.push(tf.rot);cur=tf.parent.filter(|p|*p!=0&&*p!=id);}
                        chain.into_iter().fold(IDENTITY,|rotation,parent|cpp_qmul(parent,rotation))
                    });
                    let actual_rotation=old.world_rot(name);
                    assert_eq!(actual_rotation.map(|q|q.map(f32::to_bits)),rotation.map(|q|q.map(f32::to_bits)));
                    position.zip(rotation)
                };
                let actual=IkSkeleton::new_batched(&mut skeleton).world_transform(name);
                let bits=|value:Option<(V3,Quat)>|value.map(|(p,q)|(p.map(f32::to_bits),q.map(f32::to_bits)));
                assert_eq!(bits(actual),bits(expected),"frame={frame} name={name}");
            }
        }
    }

    #[test]
    fn look_rotation_identity() {
        // forward +z, up +y -> identity in Unity.
        let q = qlook_rotation([0.0, 0.0, 1.0], [0.0, 1.0, 0.0]);
        assert!((q[3] - 1.0).abs() < 1e-5, "q={q:?}");
    }

    #[test]
    fn rotation_to_local_space_matches_inverse_concat() {
        let a = qangle_axis(30.0, [0.0, 1.0, 0.0]);
        let b = qangle_axis(50.0, [0.0, 0.0, 1.0]);
        let r = rotation_to_local_space(a, b);
        // Inverse(Inverse(a)*b) == Inverse(b)*a
        let expect = qmul(qinv(b), a);
        for i in 0..4 {
            assert!((r[i] - expect[i]).abs() < 1e-5, "{r:?} vs {expect:?}");
        }
    }

    #[test]
    fn trigonometric_solve_runs_and_rotates() {
        // Arm of two 1.0 bones along +y; target at (0.5, 1.2, 0.4).
        // NOTE: positions are owned by the caller's hierarchy; this solver only
        // writes rotations (as in FinalIK, where Unity propagates the transform).
        let mut s = IKSolverTrigonometric::default();
        s.bone1.pos = [0.0, 0.0, 0.0];
        s.bone2.pos = [0.0, 1.0, 0.0];
        s.bone3.pos = [0.0, 2.0, 0.0];
        s.bone1.sqr_mag = 1.0;
        s.bone2.sqr_mag = 1.0;
        s.bend_normal = [0.0, 0.0, 1.0];
        s.initiate();
        let before = s.bone1.rot;
        s.update([0.5, 1.2, 0.4], IDENTITY);
        assert!(s.bone1.rot != before, "bone1 rotation should change");
        assert!(s.bone1.rot[3].is_finite() && s.bone2.rot[3].is_finite());
    }

    #[test]
    fn adapters_roundtrip_identity() {
        let q = mat_rot(&crate::pose::ident());
        assert!((q[3].abs() - 1.0) < 1e-5, "q={q:?}");
        let r = qfrom_mat3([1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]);
        assert!((r[3] - 1.0).abs() < 1e-6, "r={r:?}");
    }

    #[test]
    fn vector_helpers_match_unity_reference() {
        let p = vproject([2.0, 3.0, 4.0], [1.0, 0.0, 0.0]);
        assert!((p[0] - 2.0).abs() < 1e-6 && p[1].abs() < 1e-6 && p[2].abs() < 1e-6);
        assert!((vangle([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]) - 90.0).abs() < 1e-3);
        assert!((in_out_quintic(0.5) - 0.5).abs() < 1e-6);
        let ev = extract_vertical([1.0, 2.0, 3.0], [0.0, 1.0, 0.0], 0.5);
        assert!((ev[1] - 1.0).abs() < 1e-6 && ev[0].abs() < 1e-6);
        let (_, changed) = clamp_direction([1.0, 0.0, 0.0], [0.0, 1.0, 0.0], 0.0, 0);
        assert!(!changed);
        let (d, changed) = clamp_direction([1.0, 0.0, 0.0], [0.0, 1.0, 0.0], 1.0, 0);
        assert!(changed && (d[1] - 1.0).abs() < 1e-6);
    }

    fn mk_skel(bones: &[(&str, Option<i64>, [f32; 3])]) -> crate::pose::Skeleton {
        let mut tfs = std::collections::HashMap::new();
        let mut bone_ids = Vec::new();
        for (i, (name, parent, pos)) in bones.iter().enumerate() {
            let id = (i + 1) as i64;
            tfs.insert(
                id,
                crate::pose::Tf { sibling_index: None,
                    name: name.to_string(),
                    parent: *parent,
                    pos: *pos,
                    rot: [0.0, 0.0, 0.0, 1.0],
                    scale: [1.0, 1.0, 1.0],
                },
            );
            bone_ids.push(id);
        }
        let mut sk = crate::pose::Skeleton {
            names: Vec::new(),
            worlds: Vec::new(),
            rest: Vec::new(),
            smr_count: 0,
            tfs: tfs.into(),
            bone_ids,
            source_mesh: 0,
        };
        sk.rebuild_worlds();
        sk
    }

    fn biped_bones() -> Vec<(&'static str, Option<i64>, [f32; 3])> {
        // id 0 unused; ids 1.. are the bones in this order.
        let id = |i: i64| Some(i);
        vec![
            ("root", None, [0.0, 0.0, 0.0]),                         // 1
            ("hips", id(1), [0.0, 1.0, 0.0]),                        // 2
            ("spine01", id(2), [0.0, 0.1, 0.0]),                     // 3
            ("spine02", id(3), [0.0, 0.1, 0.0]),                     // 4
            ("spine03", id(4), [0.0, 0.1, 0.0]),                     // 5
            ("head", id(5), [0.0, 0.1, 0.0]),                        // 6
            ("l_arm", id(5), [-0.1, 0.05, 0.0]),                     // 7
            ("l_forearm", id(7), [-0.2, 0.0, 0.0]),                  // 8
            ("l_hand", id(8), [-0.2, 0.0, 0.0]),                     // 9
            ("r_arm", id(5), [0.1, 0.05, 0.0]),                      // 10
            ("r_forearm", id(10), [0.2, 0.0, 0.0]),                  // 11
            ("r_hand", id(11), [0.2, 0.0, 0.0]),                     // 12
            ("l_thigh", id(2), [-0.05, -0.1, 0.0]),                  // 13
            ("l_calf", id(13), [0.0, -0.3, 0.0]),                    // 14
            ("l_foot", id(14), [0.0, -0.3, 0.0]),                    // 15
            ("r_thigh", id(2), [0.05, -0.1, 0.0]),                   // 16
            ("r_calf", id(16), [0.0, -0.3, 0.0]),                    // 17
            ("r_foot", id(17), [0.0, -0.3, 0.0]),                    // 18
            ("t_lh", None, [-0.4, 1.2, 0.2]),                        // 19
            ("t_rh", None, [0.4, 1.2, 0.2]),                         // 20
            ("t_lf", None, [-0.05, 0.1, 0.0]),                       // 21
            ("t_rf", None, [0.05, 0.1, 0.0]),                        // 22
        ]
    }

    fn biped_refs() -> BipedReferences {
        BipedReferences {
            root: Some("root".into()),
            pelvis: Some("hips".into()),
            left_thigh: Some("l_thigh".into()),
            left_calf: Some("l_calf".into()),
            left_foot: Some("l_foot".into()),
            right_thigh: Some("r_thigh".into()),
            right_calf: Some("r_calf".into()),
            right_foot: Some("r_foot".into()),
            left_upper_arm: Some("l_arm".into()),
            left_forearm: Some("l_forearm".into()),
            left_hand: Some("l_hand".into()),
            right_upper_arm: Some("r_arm".into()),
            right_forearm: Some("r_forearm".into()),
            right_hand: Some("r_hand".into()),
            head: Some("head".into()),
            spine: vec!["spine01".into(), "spine02".into(), "spine03".into()],
            eyes: Vec::new(),
        }
    }

    #[test]
    fn limb_solver_runs_on_skeleton() {
        let bones = [
            ("root", None, [0.0, 0.0, 0.0]),
            ("b1", Some(1i64), [0.0, 0.0, 0.0]),
            ("b2", Some(2i64), [0.0, 1.0, 0.0]),
            ("b3", Some(3i64), [0.0, 2.0, 0.0]),
        ];
        let mut skel = mk_skel(&bones);
        let mut sk = IkSkeleton::new(&mut skel);
        let mut limb = IKSolverLimb::new(AvatarIkGoal::LeftHand);
        limb.trig.chain = [
            Some("b1".into()),
            Some("b2".into()),
            Some("b3".into()),
        ];
        limb.root = Some("root".into());
        limb.initiate_sk(&mut sk);
        limb.update_sk(&mut sk, [0.5, 1.2, 0.4], IDENTITY);
        let r1 = sk.world_rot("b1").unwrap();
        assert!(r1[3].is_finite() && r1[3].abs() <= 1.0 + 1e-4, "r1={r1:?}");
        let r2 = sk.world_rot("b2").unwrap();
        assert!(r2[3].is_finite());
    }

    #[test]
    fn full_body_biped_initiate_and_update() {
        let mut skel = mk_skel(&biped_bones());
        let mut sk = IkSkeleton::new(&mut skel);
        let refs = biped_refs();
        let mut solver = IKSolverFullBodyBiped::default();
        solver.set_to_references(&refs, Some("spine01".into()), &mut sk);
        solver.effectors[5].target = Some("t_lh".into());
        solver.effectors[5].position_weight = 1.0;
        solver.effectors[6].target = Some("t_rh".into());
        solver.effectors[6].position_weight = 1.0;
        solver.effectors[7].target = Some("t_lf".into());
        solver.effectors[7].position_weight = 1.0;
        solver.effectors[8].target = Some("t_rf".into());
        solver.effectors[8].position_weight = 1.0;
        solver.update(&mut sk);
        solver.update(&mut sk); // second frame (as the game runs per-frame)
        for b in ["spine01", "l_arm", "l_forearm", "l_hand", "l_thigh", "l_calf", "l_foot"] {
            let q = sk.world_rot(b).unwrap();
            assert!(q.iter().all(|x| x.is_finite()), "{b} rot={q:?}");
        }
        assert!(solver.initiated);
        assert_eq!(solver.chains.len(), 5);
        assert_eq!(solver.effectors.len(), 9);
    }
}
