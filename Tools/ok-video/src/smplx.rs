//! SMPL-X forward (smplx 0.1.28 `lbs`, Docs/VIDEO_MOTION_NATIVE_SPEC.md §7.3) for a vertex
//! subset: neutral model, 10 betas, zero face/hand pose (flat_hand_mean), transl last.

use crate::geom::*;
use std::path::Path;

pub struct Model {
    pub v_template: Vec<V3>,
    /// [vertex][xyz][beta 0..10]
    shapedirs: Vec<[[f64; 10]; 3]>,
    /// (486, V·3)
    posedirs: Vec<f64>,
    /// (55, V)
    j_regressor: Vec<f64>,
    /// (V, 55)
    weights: Vec<f64>,
    pub parents: Vec<i32>,
    pub nv: usize,
}

impl Model {
    pub fn load(npz: &Path) -> Result<Self, String> {
        let a = crate::npy::read_npz(npz, &["v_template", "shapedirs", "posedirs", "J_regressor", "weights", "kintree_table"])?;
        let nv = a["v_template"].shape[0];
        let v_template = (0..nv).map(|v| std::array::from_fn(|c| a["v_template"].data[v * 3 + c])).collect();
        let sd = &a["shapedirs"];
        let nb = sd.shape[2];
        let shapedirs = (0..nv).map(|v| std::array::from_fn(|c| std::array::from_fn(|b| sd.data[(v * 3 + c) * nb + b]))).collect();
        // posedirs is stored (V, 3, 486); smplx reshapes to (486, V·3).
        let pd = &a["posedirs"];
        let np = pd.shape[2];
        let mut posedirs = vec![0.; np * nv * 3];
        for v in 0..nv {
            for c in 0..3 {
                for p in 0..np {
                    posedirs[p * nv * 3 + v * 3 + c] = pd.data[(v * 3 + c) * np + p];
                }
            }
        }
        let kt = &a["kintree_table"];
        let nj = kt.shape[1];
        let mut parents: Vec<i32> = (0..nj).map(|j| kt.data[j] as i32).collect();
        parents[0] = -1;
        Ok(Model { v_template, shapedirs, posedirs, j_regressor: a["J_regressor"].data.clone(), weights: a["weights"].data.clone(), parents, nv })
    }

    pub fn shaped(&self, betas: &[f64; 10]) -> Vec<V3> {
        (0..self.nv)
            .map(|v| std::array::from_fn(|c| self.v_template[v][c] + (0..10).map(|b| betas[b] * self.shapedirs[v][c][b]).sum::<f64>()))
            .collect()
    }

    pub fn joints(&self, shaped: &[V3]) -> Vec<V3> {
        (0..55)
            .map(|j| {
                let mut s = [0.; 3];
                for v in 0..self.nv {
                    let w = self.j_regressor[j * self.nv + v];
                    if w != 0. {
                        s = add(s, scale(shaped[v], w));
                    }
                }
                s
            })
            .collect()
    }

    /// Posed vertices `idx` for one frame (body pose 21 joints; the other 33 joints at rest).
    pub fn vertices(&self, shaped: &[V3], joints: &[V3], go: V3, body: &[V3; 21], transl: V3, idx: &[usize]) -> Vec<V3> {
        let nj = 55;
        let rot: Vec<M3> = (0..nj)
            .map(|j| if j == 0 { rodrigues(go) } else if j <= 21 { rodrigues(body[j - 1]) } else { rodrigues([0.; 3]) })
            .collect();
        // pose feature: (R[1:] - I) flattened
        let mut feat = Vec::with_capacity((nj - 1) * 9);
        for r in &rot[1..] {
            for i in 0..3 {
                for k in 0..3 {
                    feat.push(r[i][k] - if i == k { 1. } else { 0. });
                }
            }
        }
        // batch_rigid_transform
        let mut g_rot = vec![I3; nj];
        let mut g_t = vec![[0.; 3]; nj];
        for j in 0..nj {
            let rel = if self.parents[j] < 0 { joints[j] } else { sub(joints[j], joints[self.parents[j] as usize]) };
            if self.parents[j] < 0 {
                g_rot[j] = rot[j];
                g_t[j] = rel;
            } else {
                let p = self.parents[j] as usize;
                g_rot[j] = mul(&g_rot[p], &rot[j]);
                g_t[j] = add(mv(&g_rot[p], rel), g_t[p]);
            }
        }
        // A_j = G_j - [0 | G_j·J_j]
        let a_t: Vec<V3> = (0..nj).map(|j| sub(g_t[j], mv(&g_rot[j], joints[j]))).collect();
        idx.iter()
            .map(|&v| {
                let mut p = shaped[v];
                for (k, f) in feat.iter().enumerate() {
                    if *f != 0. {
                        for c in 0..3 {
                            p[c] += f * self.posedirs[k * self.nv * 3 + v * 3 + c];
                        }
                    }
                }
                let mut r = [[0.; 3]; 3];
                let mut t = [0.; 3];
                for j in 0..nj {
                    let w = self.weights[v * nj + j];
                    if w == 0. {
                        continue;
                    }
                    for i in 0..3 {
                        for k in 0..3 {
                            r[i][k] += w * g_rot[j][i][k];
                        }
                        t[i] += w * a_t[j][i];
                    }
                }
                add(add(mv(&r, p), t), transl)
            })
            .collect()
    }
}

/// smplx `batch_rodrigues` (θ = ‖aa + 1e-8‖).
pub fn rodrigues(aa: V3) -> M3 {
    let th = norm(add(aa, [1e-8; 3]));
    let k = scale(aa, 1. / th);
    let (s, c) = (th.sin(), th.cos());
    let kx = [[0., -k[2], k[1]], [k[2], 0., -k[0]], [-k[1], k[0], 0.]];
    let kk = mul(&kx, &kx);
    let mut r = I3;
    for i in 0..3 {
        for j in 0..3 {
            r[i][j] += s * kx[i][j] + (1. - c) * kk[i][j];
        }
    }
    r
}
