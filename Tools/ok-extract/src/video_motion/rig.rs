//! Koikatsu rest rig (Tools/video_motion/data/rest_skeleton.json) and SMPL-X
//! rest joints.

use super::math::*;
use super::npy;
use std::collections::HashMap;
use std::path::Path;

pub struct Bone {
    pub name: String,
    pub parent: i32,
    pub path: String,
    pub rot: [f64; 4],
}

pub struct Rig {
    pub bones: Vec<Bone>,
    pub index: HashMap<String, usize>,
    pub local_rot: Vec<M3>,
    /// Rotation-only world chain (scale excluded).
    pub world_rot: Vec<M3>,
    /// Full world translation (scale applied through parents).
    pub world_pos: Vec<V3>,
    /// Full world linear part (rotation * scale), e.g. cf_n_height's card height.
    pub world_lin: Vec<M3>,
}

impl Rig {
    pub fn load(path: &Path) -> Result<Rig, String> {
        let v: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(path).map_err(|e| format!("{}: {e}", path.display()))?)
            .map_err(|e| e.to_string())?;
        let arr = v["bones"].as_array().ok_or("rest skeleton: bones")?;
        let f3 = |x: &serde_json::Value| -> [f64; 3] { std::array::from_fn(|i| x[i].as_f64().unwrap_or(0.)) };
        let mut bones = Vec::new();
        let (mut local_rot, mut world_rot, mut world_pos) = (Vec::new(), Vec::new(), Vec::new());
        let mut world_lin: Vec<M3> = Vec::new();
        for b in arr {
            let parent = b["parent"].as_i64().unwrap_or(-1) as i32;
            let rot: [f64; 4] = std::array::from_fn(|i| b["rot"][i].as_f64().unwrap_or(if i == 3 { 1. } else { 0. }));
            let (pos, scl) = (f3(&b["pos"]), f3(&b["scl"]));
            let r = from_quat(rot);
            let mut lin = r;
            for row in &mut lin { for c in 0..3 { row[c] *= scl[c]; } }
            let (wr, wl, wp) = if parent < 0 { (r, lin, pos) } else {
                let p = parent as usize;
                (mul(&world_rot[p], &r), mul(&world_lin[p], &lin), add(world_pos[p], mulv(&world_lin[p], pos)))
            };
            local_rot.push(r);
            world_rot.push(wr);
            world_lin.push(wl);
            world_pos.push(wp);
            bones.push(Bone { name: b["name"].as_str().unwrap_or("").to_string(), parent, path: b["path"].as_str().unwrap_or("").to_string(), rot });
        }
        let index = bones.iter().enumerate().map(|(i, b)| (b.name.clone(), i)).collect();
        Ok(Rig { bones, index, local_rot, world_rot, world_pos, world_lin })
    }
    pub fn idx(&self, name: &str) -> usize { self.index[name] }
    pub fn pos(&self, name: &str) -> V3 { self.world_pos[self.idx(name)] }
    pub fn parent(&self, i: usize) -> Option<usize> { (self.bones[i].parent >= 0).then(|| self.bones[i].parent as usize) }
    /// Root..bone indices.
    pub fn chain(&self, i: usize) -> Vec<usize> {
        let mut out = vec![i];
        let mut c = i;
        while let Some(p) = self.parent(c) { out.push(p); c = p; }
        out.reverse();
        out
    }
    /// Unity binding path relative to the Animator (`cf_j_root/...`); its CRC32
    /// equals the game's own clip binding hash.
    pub fn anim_path(&self, i: usize) -> String {
        let p = &self.bones[i].path;
        p.find("cf_j_root").map(|k| p[k..].to_string()).unwrap_or_else(|| p.clone())
    }
    /// World point -> local translation under `parent`, through its full
    /// rest transform including scale (cf_n_height carries the card height:
    /// ignoring it double-scales hips/PV targets on non-unit-height cards).
    pub fn to_local(&self, parent: usize, p: V3) -> V3 {
        let m = self.world_lin[parent];
        let det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        let inv = [[(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det, (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det],
                   [(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det],
                   [(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det, (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det]];
        mulv(&inv, sub(p, self.world_pos[parent]))
    }
    /// Rest offset of `child` in its parent's rotation frame (scale baked).
    pub fn offset(&self, child: usize) -> V3 {
        let p = self.parent(child).unwrap();
        mulv(&t(&self.world_rot[p]), sub(self.world_pos[child], self.world_pos[p]))
    }
}

/// SMPL-X rest joints for `betas` (first 22, L/R-symmetrised) and the lowest
/// template vertex height.
pub fn smplx_rest_joints(path: &Path, betas: &[f64]) -> Result<([V3; 22], f64), String> {
    let m = npy::read_npz(path, &["v_template", "shapedirs", "J_regressor"])?;
    let (vt, sd, jr) = (&m["v_template"], &m["shapedirs"], &m["J_regressor"]);
    let nv = vt.shape[0];
    let nb = sd.shape.get(2).copied().unwrap_or(0).min(betas.len());
    let mut v = vec![[0.; 3]; nv];
    let mut min_y = f64::INFINITY;
    for i in 0..nv {
        for c in 0..3 {
            let mut x = vt.data[i * 3 + c];
            let base = (i * 3 + c) * sd.shape[2];
            for b in 0..nb { x += sd.data[base + b] * betas[b]; }
            v[i][c] = x;
        }
        min_y = min_y.min(v[i][1]);
    }
    let mut j = [[0.; 3]; 22];
    for (k, jk) in j.iter_mut().enumerate() {
        let row = &jr.data[k * nv..(k + 1) * nv];
        for (i, w) in row.iter().enumerate() {
            if *w != 0. { for c in 0..3 { jk[c] += w * v[i][c]; } }
        }
    }
    // The v1.1 template is not left/right symmetric (rest elbows 22.7 vs 6.9
    // degrees); that bias would leak into every frame.
    for (a, b) in [(1, 2), (4, 5), (7, 8), (10, 11), (13, 14), (16, 17), (18, 19), (20, 21)] {
        let avg = [(j[a][0] - j[b][0]) / 2., (j[a][1] + j[b][1]) / 2., (j[a][2] + j[b][2]) / 2.];
        j[a] = avg;
        j[b] = [-avg[0], avg[1], avg[2]];
    }
    for i in [0, 3, 6, 9, 12, 15] { j[i][0] = 0.; }
    Ok((j, min_y))
}

pub const SMPL_PARENTS: [i32; 22] = [-1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 12, 13, 14, 16, 17, 18, 19];

/// (KK bone, SMPL joint, alignment): None torso / identity, Some(Ok(child, kk child)) direction-aligned,
/// Some(Err(other)) inherit `other`'s alignment.
pub enum Align { Identity, Direction(usize, &'static str), Inherit(&'static str) }

pub fn mapping() -> Vec<(&'static str, usize, Align)> {
    use Align::*;
    vec![
        ("cf_j_hips", 0, Identity), ("cf_j_spine01", 3, Identity), ("cf_j_spine02", 6, Identity),
        ("cf_j_spine03", 9, Identity), ("cf_j_neck", 12, Identity), ("cf_j_head", 15, Identity),
        ("cf_j_thigh00_L", 1, Direction(4, "cf_j_leg01_L")), ("cf_j_leg01_L", 4, Direction(7, "cf_j_leg03_L")),
        ("cf_j_leg03_L", 7, Inherit("cf_j_leg01_L")), ("cf_j_toes_L", 10, Inherit("cf_j_leg01_L")),
        ("cf_j_shoulder_L", 13, Identity), ("cf_j_arm00_L", 16, Direction(18, "cf_j_forearm01_L")),
        ("cf_j_forearm01_L", 18, Direction(20, "cf_j_hand_L")), ("cf_j_hand_L", 20, Inherit("cf_j_forearm01_L")),
        ("cf_j_thigh00_R", 2, Direction(5, "cf_j_leg01_R")), ("cf_j_leg01_R", 5, Direction(8, "cf_j_leg03_R")),
        ("cf_j_leg03_R", 8, Inherit("cf_j_leg01_R")), ("cf_j_toes_R", 11, Inherit("cf_j_leg01_R")),
        ("cf_j_shoulder_R", 14, Identity), ("cf_j_arm00_R", 17, Direction(19, "cf_j_forearm01_R")),
        ("cf_j_forearm01_R", 19, Direction(21, "cf_j_hand_R")), ("cf_j_hand_R", 21, Inherit("cf_j_forearm01_R")),
    ]
}
