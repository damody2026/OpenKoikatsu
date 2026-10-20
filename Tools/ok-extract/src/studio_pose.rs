//! KK Studio pose files (`UserData/Studio/pose/*.dat`, Studio.PauseCtrl): read and write.
//!
//! Layout (.NET BinaryWriter, little endian; strings are 7-bit length prefixed UTF-8):
//! "【pose】", version (101), sex, name, then FileInfo: animation group / category / no, normalizedTime (version >= 101),
//! enableIK, activeIK[5], IK target count + (key, ChangeAmount)…, enableFK, activeFK[7], FK bone count +
//! (bone no, ChangeAmount)…, expression[4]. ChangeAmount is position, rotation (local Euler degrees) and scale, 3 floats each.

use serde::{Deserialize, Serialize};

pub const IDENTIFYING_CODE: &str = "【pose】";
pub const VERSION: i32 = 101;

#[derive(Clone, Copy, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct ChangeAmount { pub pos: [f32; 3], pub rot: [f32; 3], pub scale: [f32; 3] }

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Pose {
    pub sex: i32,
    pub name: String,
    /// Studio animation (group, category, no) and its normalized time.
    pub anime: [i32; 3],
    pub normalized_time: f32,
    pub enable_ik: bool,
    pub active_ik: [bool; 5],
    pub ik: Vec<(i32, ChangeAmount)>,
    pub enable_fk: bool,
    /// FK parts: hair, neck, breast, body, right hand, left hand, skirt.
    pub active_fk: [bool; 7],
    /// Keyed by the Studio FK bone no (the Bone sheet of `studio/info`).
    pub fk: Vec<(i32, ChangeAmount)>,
    pub expression: [bool; 4],
}

impl Default for Pose {
    fn default() -> Self {
        Pose { sex: 1, name: String::new(), anime: [-1, -1, -1], normalized_time: 0., enable_ik: false, active_ik: [true; 5], ik: Vec::new(),
            enable_fk: false, active_fk: [false, true, false, true, false, false, false], fk: Vec::new(), expression: [true; 4] }
    }
}

/// FK part (index into `active_fk`) of a Bone sheet group, as Studio.FKCtrl assigns it.
pub fn fk_part(group: i32) -> usize {
    match group { 0..=4 => 3, 5 => 4, 6 => 5, 7..=9 => 0, 10 => 1, 11 | 12 => 2, 13 => 6, _ => 3 }
}

struct Reader<'a> { b: &'a [u8], p: usize }
impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], String> {
        let s = self.b.get(self.p..self.p + n).ok_or("truncated pose file")?;
        self.p += n;
        Ok(s)
    }
    fn i32(&mut self) -> Result<i32, String> { Ok(i32::from_le_bytes(self.take(4)?.try_into().unwrap())) }
    fn f32(&mut self) -> Result<f32, String> { Ok(f32::from_le_bytes(self.take(4)?.try_into().unwrap())) }
    fn bool(&mut self) -> Result<bool, String> { Ok(self.take(1)?[0] != 0) }
    fn v3(&mut self) -> Result<[f32; 3], String> { Ok([self.f32()?, self.f32()?, self.f32()?]) }
    fn string(&mut self) -> Result<String, String> {
        let (mut len, mut shift) = (0usize, 0);
        loop {
            let byte = self.take(1)?[0];
            len |= ((byte & 0x7f) as usize) << shift;
            if byte & 0x80 == 0 { break; }
            shift += 7;
            if shift > 28 { return Err("bad string length".into()); }
        }
        Ok(String::from_utf8_lossy(self.take(len)?).into_owned())
    }
    fn change(&mut self) -> Result<ChangeAmount, String> { Ok(ChangeAmount { pos: self.v3()?, rot: self.v3()?, scale: self.v3()? }) }
}

fn write_string(out: &mut Vec<u8>, s: &str) {
    let mut len = s.len();
    loop {
        let byte = (len & 0x7f) as u8;
        len >>= 7;
        if len == 0 { out.push(byte); break; }
        out.push(byte | 0x80);
    }
    out.extend_from_slice(s.as_bytes());
}

pub fn read(bytes: &[u8]) -> Result<Pose, String> {
    let mut r = Reader { b: bytes, p: 0 };
    if r.string()? != IDENTIFYING_CODE { return Err("not a KK Studio pose file".into()); }
    let version = r.i32()?;
    let mut pose = Pose { sex: r.i32()?, name: r.string()?, ..Pose::default() };
    pose.anime = [r.i32()?, r.i32()?, r.i32()?];
    if version >= 101 { pose.normalized_time = r.f32()?; }
    pose.enable_ik = r.bool()?;
    for a in &mut pose.active_ik { *a = r.bool()?; }
    let n = r.i32()?;
    if !(0..=4096).contains(&n) { return Err(format!("bad IK target count {n}")); }
    for _ in 0..n { let key = r.i32()?; pose.ik.push((key, r.change()?)); }
    pose.enable_fk = r.bool()?;
    for a in &mut pose.active_fk { *a = r.bool()?; }
    let n = r.i32()?;
    if !(0..=4096).contains(&n) { return Err(format!("bad FK bone count {n}")); }
    for _ in 0..n { let key = r.i32()?; pose.fk.push((key, r.change()?)); }
    for e in &mut pose.expression { *e = r.bool()?; }
    Ok(pose)
}

pub fn write(pose: &Pose) -> Vec<u8> {
    let mut out = Vec::new();
    let i32w = |o: &mut Vec<u8>, v: i32| o.extend_from_slice(&v.to_le_bytes());
    let f32w = |o: &mut Vec<u8>, v: f32| o.extend_from_slice(&v.to_le_bytes());
    let change = |o: &mut Vec<u8>, c: &ChangeAmount| for v in c.pos.iter().chain(&c.rot).chain(&c.scale) { o.extend_from_slice(&v.to_le_bytes()); };
    write_string(&mut out, IDENTIFYING_CODE);
    i32w(&mut out, VERSION);
    i32w(&mut out, pose.sex);
    write_string(&mut out, &pose.name);
    for v in pose.anime { i32w(&mut out, v); }
    f32w(&mut out, pose.normalized_time);
    out.push(pose.enable_ik as u8);
    for a in pose.active_ik { out.push(a as u8); }
    i32w(&mut out, pose.ik.len() as i32);
    for (k, c) in &pose.ik { i32w(&mut out, *k); change(&mut out, c); }
    out.push(pose.enable_fk as u8);
    for a in pose.active_fk { out.push(a as u8); }
    i32w(&mut out, pose.fk.len() as i32);
    for (k, c) in &pose.fk { i32w(&mut out, *k); change(&mut out, c); }
    for e in pose.expression { out.push(e as u8); }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn round_trip() {
        let pose = Pose { name: "テスト".into(), anime: [0, 1, 2], normalized_time: 0.25, enable_fk: true, active_fk: [false, true, false, true, true, false, false],
            fk: vec![(7, ChangeAmount { pos: [0.; 3], rot: [10., 20., 30.], scale: [1.; 3] }), (300, ChangeAmount { rot: [-5., 0., 90.], scale: [1.; 3], ..Default::default() })],
            ik: vec![(0, ChangeAmount { pos: [0.1, 0.2, 0.3], scale: [1.; 3], ..Default::default() })], ..Pose::default() };
        let bytes = write(&pose);
        assert_eq!(&bytes[1..1 + "【pose】".len()], "【pose】".as_bytes());
        assert_eq!(read(&bytes).unwrap(), pose);
        assert!(read(&bytes[..bytes.len() - 2]).is_err());
        let mut long = Pose::default();
        long.name = "x".repeat(300);
        assert_eq!(read(&write(&long)).unwrap().name.len(), 300);
    }
}

#[cfg(test)]
mod euler_rotate_tests {
    #[test]
    fn local_axis_turns_compose() {
        // Turning about the local Y axis twice by 15 degrees equals one 30-degree turn; zero is the identity.
        let e = [20.0f32, -35.0, 10.0];
        let mut a = [0.0f32; 3];
        let mut b = [0.0f32; 3];
        let mut c = [0.0f32; 3];
        let mut z = [0.0f32; 3];
        unsafe {
            assert_eq!(crate::ffi::ok_unity_euler_rotate(e.as_ptr(), 1, 15.0, a.as_mut_ptr()), 0);
            crate::ffi::ok_unity_euler_rotate(a.as_ptr(), 1, 15.0, b.as_mut_ptr());
            crate::ffi::ok_unity_euler_rotate(e.as_ptr(), 1, 30.0, c.as_mut_ptr());
            crate::ffi::ok_unity_euler_rotate(e.as_ptr(), 2, 0.0, z.as_mut_ptr());
            assert_eq!(crate::ffi::ok_unity_euler_rotate(e.as_ptr(), 3, 1.0, z.as_mut_ptr()), -1);
        }
        let same = |p: [f32; 3], q: [f32; 3]| {
            let (x, y) = (crate::pose::euler_deg_to_quat(p), crate::pose::euler_deg_to_quat(q));
            (0..4).map(|i| x[i] * y[i]).sum::<f32>().abs() > 0.9999
        };
        assert!(same(b, c), "{b:?} vs {c:?}");
        assert!(!same(a, e));
    }
}
