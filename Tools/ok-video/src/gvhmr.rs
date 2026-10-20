//! GVHMR inference (Docs/VIDEO_MOTION_NATIVE_SPEC.md §6): `NetworkEncoderRoPE` on the GPU,
//! then decode, world-frame assembly, static-joint post-processing and CCD IK on the CPU,
//! line for line with hmr4d (gvhmr_pipeline.py, postprocess.py, ccd_ik.py).

use crate::geom::*;
use candle_core::{DType, Device, Tensor, D};
use std::collections::HashMap;
use std::path::Path;

type R<T> = Result<T, String>;
fn e<E: std::fmt::Display>(x: E) -> String { x.to_string() }

// ---------------------------------------------------------------- inputs / outputs

pub struct Inputs {
    /// ViTPose keypoints per frame: 17 × (x, y, conf) in full-image pixels.
    pub kp2d: Vec<[[f32; 3]; 17]>,
    /// (cx, cy, size) per frame.
    pub bbx_xys: Vec<[f64; 3]>,
    /// Intrinsics: focal, cx, cy (same for all frames).
    pub focal: f64,
    pub cx: f64,
    pub cy: f64,
    /// cam_angvel per frame (6D of R_{t→t+1}); identity = [1,0,0,0,1,0].
    pub cam_angvel: Vec<[f64; 6]>,
    /// HMR2 features per frame (1024).
    pub imgseq: Vec<f32>,
    pub static_cam: bool,
}

#[derive(Clone, Default)]
pub struct SmplParams {
    pub global_orient: Vec<V3>,
    pub body_pose: Vec<[V3; 21]>,
    pub transl: Vec<V3>,
    pub betas: [f64; 10],
}

#[derive(Clone)]
pub struct Output {
    pub global: SmplParams,
    pub incam: SmplParams,
    pub static_conf_logits: Vec<[f64; 6]>,
}

// ---------------------------------------------------------------- SMPL-X skeleton (fk_v2)

pub const PARENTS: [i32; 22] = [-1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 12, 13, 14, 16, 17, 18, 19];
pub const STATIC_JOINTS: [usize; 6] = [7, 10, 8, 11, 20, 21];

pub struct Skeleton {
    pub j_template: [V3; 22],
    /// [joint][xyz][beta]
    pub j_shapedirs: Vec<[[f64; 10]; 3]>,
}

impl Skeleton {
    /// `SmplxLite.register_fast_skeleton_computing_buffers` from SMPLX_NEUTRAL.npz.
    pub fn load(npz: &Path) -> R<Self> {
        let a = crate::npy::read_npz(npz, &["J_regressor", "v_template", "shapedirs"])?;
        let jr = &a["J_regressor"];
        let vt = &a["v_template"];
        let sd = &a["shapedirs"];
        let nv = vt.shape[0];
        let nb = sd.shape[2];
        let mut j_template = [[0.; 3]; 22];
        let mut j_shapedirs = vec![[[0.; 10]; 3]; 22];
        for j in 0..22 {
            for v in 0..nv {
                let w = jr.data[j * nv + v];
                if w == 0. {
                    continue;
                }
                for c in 0..3 {
                    j_template[j][c] += w * vt.data[v * 3 + c];
                    for b in 0..10 {
                        j_shapedirs[j][c][b] += w * sd.data[(v * 3 + c) * nb + b];
                    }
                }
            }
        }
        Ok(Skeleton { j_template, j_shapedirs })
    }
    pub fn joints(&self, betas: &[f64; 10]) -> [V3; 22] {
        let mut out = self.j_template;
        for j in 0..22 {
            for c in 0..3 {
                for b in 0..10 {
                    out[j][c] += betas[b] * self.j_shapedirs[j][c][b];
                }
            }
        }
        out
    }
}

#[derive(Clone, Copy)]
pub struct Tf {
    pub r: M3,
    pub t: V3,
}
fn compose(a: &Tf, b: &Tf) -> Tf {
    // get_mat_BfromA: normalized_matrix(A @ B)
    Tf { r: normalized_matrix(&mul(&a.r, &b.r)), t: add(mv(&a.r, b.t), a.t) }
}
/// `matrix.forward_kinematics`: root kept as is, children normalized.
fn forward_kinematics(local: &[Tf], parent: &[i32]) -> Vec<Tf> {
    let mut world: Vec<Tf> = Vec::with_capacity(local.len());
    for i in 0..local.len() {
        world.push(if parent[i] < 0 { local[i] } else { compose(&world[parent[i] as usize], &local[i]) });
    }
    world
}

/// `EnDecoder.fk_v2` for one frame: (joints, local TRS, world).
pub fn fk(skel: &Skeleton, go: V3, pose: &[V3; 21], betas: &[f64; 10], transl: V3) -> ([V3; 22], Vec<Tf>, Vec<Tf>) {
    let sk = skel.joints(betas);
    let mut local = Vec::with_capacity(22);
    for j in 0..22 {
        let rot = aa_to_matrix(if j == 0 { go } else { pose[j - 1] });
        let t = if j == 0 { add(sk[0], transl) } else { sub(sk[j], sk[PARENTS[j] as usize]) };
        local.push(Tf { r: normalized_matrix(&rot), t });
    }
    let world = forward_kinematics(&local, &PARENTS);
    let mut joints = [[0.; 3]; 22];
    for j in 0..22 {
        joints[j] = world[j].t;
    }
    (joints, local, world)
}

fn fk_seq(skel: &Skeleton, p: &SmplParams) -> Vec<[V3; 22]> {
    (0..p.transl.len()).map(|t| fk(skel, p.global_orient[t], &p.body_pose[t], &p.betas, p.transl[t]).0).collect()
}

// ---------------------------------------------------------------- network

const STATS_BODY_MEAN: [f64; 126] = [
    9.6969e-01, -5.9719e-02, -3.7700e-02, 5.8256e-02, 9.0800e-01, 1.0972e-01, 9.7636e-01, 4.3401e-02, 4.3110e-03, -4.3032e-02, 9.0261e-01,
    1.4478e-01, 9.9288e-01, 3.5673e-03, 1.6264e-02, -2.2260e-03, 9.3470e-01, -2.3495e-01, 9.7147e-01, 5.2553e-02, -9.3666e-02, -5.4550e-02,
    8.3321e-01, -2.4246e-01, 9.7971e-01, -3.8429e-02, 5.3575e-03, 1.5537e-02, 8.1449e-01, -3.0926e-01, 9.9532e-01, -9.4398e-03, -3.8328e-02,
    8.5141e-03, 9.8880e-01, 1.9976e-04, 9.5602e-01, -3.9528e-02, 2.0017e-01, 1.0363e-02, 9.5965e-01, 1.3770e-01, 9.6223e-01, -4.6278e-02,
    -1.5177e-01, 6.6705e-02, 9.5545e-01, 1.2519e-01, 9.9767e-01, -1.2616e-02, -2.5442e-04, 1.1661e-02, 9.9376e-01, -3.6222e-02, 9.9511e-01,
    -1.0583e-02, 1.2130e-02, 7.6461e-03, 9.9137e-01, 2.0029e-02, 9.9295e-01, 7.2917e-03, 4.9454e-03, -8.0286e-03, 9.9137e-01, 2.3707e-03,
    9.7698e-01, 1.9943e-02, 1.3808e-03, -2.2006e-02, 9.7375e-01, -6.7936e-02, 9.2804e-01, 2.5005e-01, -5.7167e-02, -2.4047e-01, 9.4246e-01,
    2.5863e-02, 9.2957e-01, -2.1329e-01, 1.1112e-01, 2.0741e-01, 9.4876e-01, 2.9901e-02, 9.7683e-01, -4.1210e-02, 2.3248e-03, 4.0967e-02,
    9.7365e-01, 5.7309e-03, 6.4513e-01, 6.1999e-01, -2.5469e-01, -6.2342e-01, 6.8177e-01, 3.5524e-02, 6.6192e-01, -5.9341e-01, 2.7136e-01,
    5.9269e-01, 6.8966e-01, 3.1309e-02, 6.8946e-01, -1.1676e-01, -4.9859e-01, 4.0969e-02, 9.3656e-01, -1.4875e-01, 6.2787e-01, 1.3793e-01,
    5.4289e-01, -9.1946e-02, 9.2868e-01, -1.1927e-01, 9.3012e-01, -8.3810e-02, -1.1951e-01, 9.7211e-02, 8.9118e-01, 5.9887e-02, 9.3033e-01,
    7.1047e-02, 7.5264e-02, -8.0679e-02, 8.8562e-01, 4.8960e-02,
];
const STATS_BODY_STD: [f64; 126] = [
    0.0612, 0.1390, 0.1779, 0.1415, 0.1826, 0.3268, 0.0440, 0.1382, 0.1542, 0.1348, 0.1930, 0.3272, 0.0132, 0.0801, 0.0855, 0.0729, 0.1255,
    0.2238, 0.0554, 0.1088, 0.1727, 0.0939, 0.3294, 0.3559, 0.0532, 0.1082, 0.1554, 0.0768, 0.3446, 0.3407, 0.0120, 0.0650, 0.0584, 0.0632,
    0.0198, 0.1335, 0.0631, 0.1250, 0.1574, 0.1047, 0.0730, 0.2091, 0.0759, 0.1241, 0.1667, 0.1112, 0.0831, 0.2185, 0.0060, 0.0441, 0.0502,
    0.0441, 0.0102, 0.0946, 0.0237, 0.0722, 0.0610, 0.0738, 0.0479, 0.0949, 0.0369, 0.0943, 0.0610, 0.0966, 0.0498, 0.0729, 0.0425, 0.1001,
    0.1824, 0.0972, 0.0408, 0.1887, 0.0594, 0.1842, 0.1884, 0.2020, 0.0457, 0.1018, 0.0640, 0.1990, 0.1854, 0.2133, 0.0467, 0.0910, 0.0392,
    0.1049, 0.1776, 0.1037, 0.0413, 0.1945, 0.1733, 0.2612, 0.1905, 0.2963, 0.1512, 0.1861, 0.1710, 0.2663, 0.1896, 0.3135, 0.1568, 0.2219,
    0.3976, 0.1594, 0.2810, 0.1855, 0.0845, 0.2398, 0.4398, 0.1629, 0.2685, 0.1990, 0.0998, 0.2556, 0.1137, 0.2837, 0.1419, 0.2761, 0.1678,
    0.2973, 0.1172, 0.3010, 0.1394, 0.2910, 0.1724, 0.3039,
];
const STATS_TAIL_MEAN: [f64; 25] = [
    0.2310, 0.1750, 0.2931, -0.1859, -1.1163, -1.1028, -0.2573, 0.3555, 0.3732, 0.2852, // betas
    -4.9862e-03, -8.7136e-04, -1.4187e-03, 1.4825e-02, -9.4419e-01, -5.1653e-02, // global_orient_c r6d
    3.6018e-04, -2.2327e-04, 2.2316e-03, -4.4879e-02, -9.7435e-01, 1.0021e-01, // global_orient_gv r6d
    -0.0002, -0.0006, 0.0069, // local_transl_vel
];
const STATS_TAIL_STD: [f64; 25] = [
    0.8831, 0.7965, 1.0899, 1.1788, 1.2128, 1.1081, 0.9780, 1.1434, 0.8498, 1.1462, //
    0.7048, 0.1713, 0.6884, 0.1548, 0.1546, 0.2403, //
    0.6070, 0.5355, 0.5873, 0.6285, 0.2336, 0.7675, //
    0.0064, 0.0070, 0.0138,
];
const PRED_CAM_STD: [f64; 3] = [0.1784, 0.0956, 0.0764];
const PRED_CAM_MEAN: [f64; 3] = [1.0606, -0.0027, 0.2702];
const ANGVEL_MEAN: [f64; 6] = [1., 0., 0., 0., 1., 0.];
const ANGVEL_STD: [f64; 6] = [0.001, 0.1, 0.1, 0.1, 0.001, 0.1];

pub struct Network {
    w: HashMap<String, Tensor>,
    dev: Device,
}

impl Network {
    pub fn load(path: &Path, dev: &Device) -> R<Self> {
        let raw = candle_core::safetensors::load(path, dev).map_err(|x| format!("{}：{x}", path.display()))?;
        let w = raw
            .into_iter()
            .filter_map(|(k, v)| k.strip_prefix("pipeline.denoiser3d.").map(|s| (s.to_string(), v)))
            .map(|(k, v)| (k, v.to_dtype(DType::F32).unwrap()))
            .collect::<HashMap<_, _>>();
        if !w.contains_key("blocks.11.mlp.fc2.weight") {
            return Err(format!("{} 不是 GVHMR 權重（缺 pipeline.denoiser3d.blocks.11）", path.display()));
        }
        Ok(Network { w, dev: dev.clone() })
    }
    fn p(&self, k: &str) -> R<&Tensor> { self.w.get(k).ok_or_else(|| format!("GVHMR 權重缺少 {k}")) }
    fn linear(&self, x: &Tensor, name: &str) -> R<Tensor> {
        let w = self.p(&format!("{name}.weight"))?;
        let b = self.p(&format!("{name}.bias"))?;
        x.broadcast_matmul(&w.t().map_err(e)?).and_then(|y| y.broadcast_add(b)).map_err(e)
    }
    fn mlp(&self, x: &Tensor, name: &str, tanh_gelu: bool) -> R<Tensor> {
        let h = self.linear(x, &format!("{name}.fc1"))?;
        let h = if tanh_gelu { h.gelu() } else { h.gelu_erf() }.map_err(e)?;
        self.linear(&h, &format!("{name}.fc2"))
    }
    fn layer_norm(&self, x: &Tensor, name: &str, eps: f64) -> R<Tensor> {
        candle_nn::ops::layer_norm(x, self.p(&format!("{name}.weight"))?, self.p(&format!("{name}.bias"))?, eps as f32).map_err(e)
    }

    /// Returns (sample (L,151) normalized, pred_cam (L,3) raw, static logits (L,6)).
    pub fn forward(&self, obs: &[f32], cliffcam: &[f32], angvel: &[f32], imgseq: &[f32], len: usize) -> R<(Vec<f32>, Vec<f32>, Vec<f32>)> {
        let d = &self.dev;
        let l = len;
        // (a) 2D observations
        let obs = Tensor::from_slice(obs, (l, 17, 3), d).map_err(e)?;
        let conf = obs.narrow(2, 2, 1).map_err(e)?;
        let vis = conf.gt(0.5f64).map_err(e)?.to_dtype(DType::F32).map_err(e)?;
        let xy = obs.narrow(2, 0, 2).map_err(e)?.broadcast_mul(&vis).map_err(e)?;
        let f = self.linear(&xy, "learned_pos_linear")?; // (L,17,32)
        let pos = self.p("learned_pos_params")?.unsqueeze(0).map_err(e)?; // (1,17,32)
        let inv = vis.affine(-1., 1.).map_err(e)?;
        let f = (f.broadcast_mul(&vis).map_err(e)? + pos.broadcast_mul(&inv).map_err(e)?).map_err(e)?;
        let f = f.reshape((l, 17 * 32)).map_err(e)?;
        let mut x = self.mlp(&f, "embed_noisyobs", false)?;
        // (b) conditions
        let cc = Tensor::from_slice(cliffcam, (l, 3), d).map_err(e)?;
        let h = self.linear(&cc, "cliffcam_embedder.0")?.silu().map_err(e)?;
        x = (x + self.linear(&h, "cliffcam_embedder.3")?).map_err(e)?;
        let av = Tensor::from_slice(angvel, (l, 6), d).map_err(e)?;
        let h = self.linear(&av, "cam_angvel_embedder.0")?.silu().map_err(e)?;
        x = (x + self.linear(&h, "cam_angvel_embedder.3")?).map_err(e)?;
        let im = Tensor::from_slice(imgseq, (l, 1024), d).map_err(e)?;
        let h = self.layer_norm(&im, "imgseq_embedder.0", 1e-5)?;
        x = (x + self.linear(&h, "imgseq_embedder.1")?).map_err(e)?;
        // (c) sliding-window mask (L > 120)
        let mask = if l > 120 {
            let mut m = vec![f32::NEG_INFINITY; l * l];
            for i in 0..l {
                let mut lo = i.saturating_sub(60);
                let mut hi = (i + 60).min(l);
                hi = hi.max(120);
                lo = lo.min(l - 120);
                for j in lo..hi {
                    m[i * l + j] = 0.;
                }
            }
            Some(Tensor::from_vec(m, (l, l), d).map_err(e)?)
        } else {
            None
        };
        // RoPE tables (interleaved pairs, base 10000, head dim 64)
        let mut cos = vec![0f32; l * 32];
        let mut sin = vec![0f32; l * 32];
        for t in 0..l {
            for k in 0..32 {
                let f = t as f64 * 10000f64.powf(-2. * k as f64 / 64.);
                cos[t * 32 + k] = f.cos() as f32;
                sin[t * 32 + k] = f.sin() as f32;
            }
        }
        let cos = Tensor::from_vec(cos, (l, 32), d).map_err(e)?;
        let sin = Tensor::from_vec(sin, (l, 32), d).map_err(e)?;
        // (d) blocks
        for b in 0..12 {
            let n = format!("blocks.{b}");
            let h = self.layer_norm(&x, &format!("{n}.norm1"), 1e-6)?;
            let heads = |t: Tensor| -> R<Tensor> { t.reshape((1, l, 8, 64)).and_then(|t| t.transpose(1, 2)).and_then(|t| t.contiguous()).map_err(e) };
            let q = heads(self.linear(&h, &format!("{n}.attn.query"))?)?;
            let k = heads(self.linear(&h, &format!("{n}.attn.key"))?)?;
            let v = heads(self.linear(&h, &format!("{n}.attn.value"))?)?;
            let q = candle_nn::rotary_emb::rope_i(&q, &cos, &sin).map_err(e)?;
            let k = candle_nn::rotary_emb::rope_i(&k, &cos, &sin).map_err(e)?;
            let mut s = (q.matmul(&k.t().map_err(e)?).map_err(e)? / 8.).map_err(e)?;
            if let Some(m) = &mask {
                s = s.broadcast_add(m).map_err(e)?;
            }
            let a = candle_nn::ops::softmax_last_dim(&s).map_err(e)?.matmul(&v).map_err(e)?;
            let a = a.transpose(1, 2).and_then(|t| t.reshape((l, 512))).map_err(e)?;
            let o = self.linear(&a, &format!("{n}.attn.proj"))?;
            x = (x + o.broadcast_mul(&self.p(&format!("{n}.gate_msa"))?.reshape(512).map_err(e)?).map_err(e)?).map_err(e)?;
            let h = self.layer_norm(&x, &format!("{n}.norm2"), 1e-6)?;
            let m = self.mlp(&h, &format!("{n}.mlp"), true)?;
            x = (x + m.broadcast_mul(&self.p(&format!("{n}.gate_mlp"))?.reshape(512).map_err(e)?).map_err(e)?).map_err(e)?;
        }
        // (e) heads
        let sample = self.mlp(&x, "final_layer", false)?;
        let cam = self.mlp(&x, "pred_cam_head", false)?;
        let stat = self.mlp(&x, "static_conf_head", false)?;
        let v = |t: Tensor| -> R<Vec<f32>> { t.flatten_all().and_then(|t| t.to_vec1::<f32>()).map_err(e) };
        let _ = D::Minus1;
        Ok((v(sample)?, v(cam)?, v(stat)?))
    }
}

// ---------------------------------------------------------------- pipeline

/// `DemoPL.predict` + `Pipeline.forward(postproc=True)`.
pub fn predict(net: &Network, skel: &Skeleton, inp: &Inputs) -> R<Output> {
    let l = inp.kp2d.len();
    if l < 2 || inp.bbx_xys.len() != l || inp.cam_angvel.len() != l || inp.imgseq.len() != l * 1024 {
        return Err(format!("GVHMR 輸入長度不一致：kp2d {l}、bbx {}、angvel {}、features {}", inp.bbx_xys.len(), inp.cam_angvel.len(), inp.imgseq.len() / 1024));
    }
    // normalize_kp2d
    let mut obs = Vec::with_capacity(l * 51);
    for t in 0..l {
        let [cx, cy, s] = inp.bbx_xys[t];
        for j in 0..17 {
            let [x, y, c] = inp.kp2d[t][j];
            let (x, y) = (x as f64, y as f64);
            let outside = x < cx - s / 2. || x > cx + s / 2. || y < cy - s / 2. || y > cy + s / 2.;
            obs.push((2. * (x - cx) / s) as f32);
            obs.push((2. * (y - cy) / s) as f32);
            obs.push(if outside { 0. } else { c });
        }
    }
    let cliff: Vec<f32> = inp.bbx_xys.iter().flat_map(|b| [((b[0] - inp.cx) / inp.focal) as f32, ((b[1] - inp.cy) / inp.focal) as f32, (b[2] / inp.focal) as f32]).collect();
    let angvel: Vec<f32> = inp.cam_angvel.iter().flat_map(|a| (0..6).map(move |i| ((a[i] - ANGVEL_MEAN[i]) / ANGVEL_STD[i]) as f32)).collect();
    let (sample, cam, stat) = net.forward(&obs, &cliff, &angvel, &inp.imgseq, l)?;

    // avgbeta + decode
    let mut betas_n = [0f64; 10];
    for t in 0..l {
        for b in 0..10 {
            betas_n[b] += sample[t * 151 + 126 + b] as f64 / l as f64;
        }
    }
    let mut betas = [0f64; 10];
    for b in 0..10 {
        betas[b] = betas_n[b] * STATS_TAIL_STD[b] + STATS_TAIL_MEAN[b];
    }
    let mut body_pose = Vec::with_capacity(l);
    let mut go_c = Vec::with_capacity(l);
    let mut go_gv = Vec::with_capacity(l);
    let mut vel = Vec::with_capacity(l);
    for t in 0..l {
        let x = |i: usize| -> f64 {
            let v = sample[t * 151 + i] as f64;
            if i < 126 { v * STATS_BODY_STD[i] + STATS_BODY_MEAN[i] } else { v * STATS_TAIL_STD[i - 126] + STATS_TAIL_MEAN[i - 126] }
        };
        let mut pose = [[0.; 3]; 21];
        for j in 0..21 {
            let d: Vec<f64> = (0..6).map(|k| x(j * 6 + k)).collect();
            pose[j] = matrix_to_aa(&r6d_to_matrix(&d));
        }
        body_pose.push(pose);
        go_c.push(matrix_to_aa(&r6d_to_matrix(&(136..142).map(x).collect::<Vec<_>>())));
        go_gv.push(matrix_to_aa(&r6d_to_matrix(&(142..148).map(x).collect::<Vec<_>>())));
        vel.push([x(148), x(149), x(150)]);
    }
    // incam params
    let incam_transl: Vec<V3> = (0..l)
        .map(|t| {
            let s = (cam[t * 3] as f64 * PRED_CAM_STD[0] + PRED_CAM_MEAN[0]).max(0.25);
            let tx = cam[t * 3 + 1] as f64 * PRED_CAM_STD[1] + PRED_CAM_MEAN[1];
            let ty = cam[t * 3 + 2] as f64 * PRED_CAM_STD[2] + PRED_CAM_MEAN[2];
            let [bx, by, bs] = inp.bbx_xys[t];
            let sb = s * bs;
            [tx + 2. * (bx - inp.cx) / (sb + 1e-9), ty + 2. * (by - inp.cy) / (sb + 1e-9), 2. * inp.focal / (sb + 1e-9)]
        })
        .collect();
    let incam = SmplParams { global_orient: go_c.clone(), body_pose: body_pose.clone(), transl: incam_transl, betas };
    let (global_orient, transl) = world_params(&go_gv, &vel, &go_c, &inp.cam_angvel);
    let mut global = SmplParams { global_orient, body_pose, transl, betas };
    let logits: Vec<[f64; 6]> = (0..l).map(|t| std::array::from_fn(|k| stat[t * 6 + k] as f64)).collect();

    global.transl = if inp.static_cam { pp_static_joint_cam(skel, &global, &incam, &logits) } else { pp_static_joint(skel, &global, &logits) };
    let ik_pose = process_ik(skel, &global, &logits);
    global.body_pose = ik_pose.clone();
    let mut incam = incam;
    incam.body_pose = ik_pose;
    Ok(Output { global, incam, static_conf_logits: logits })
}

fn as_identity(m: M3) -> M3 { if norm(matrix_to_aa(&m)) < 1e-5 { I3 } else { m } }

/// `get_smpl_params_w_Rt_v2`.
fn world_params(go_gv: &[V3], vel: &[V3], go_c: &[V3], angvel: &[[f64; 6]]) -> (Vec<V3>, Vec<V3>) {
    let l = go_gv.len();
    let mut aa_x = Vec::with_capacity(l);
    let mut aa_y = Vec::with_capacity(l);
    let mut aa_z = Vec::with_capacity(l);
    let r_gv: Vec<M3> = go_gv.iter().map(|a| aa_to_matrix(*a)).collect();
    for t in 0..l {
        let r_t_to_tp1 = as_identity(r6d_to_matrix(&angvel[t]));
        let r_c = aa_to_matrix(go_c[t]);
        let r_c2gv = mul(&r_gv[t], &tr(&r_c));
        let view = col(&r_c2gv, 2);
        let r_next = mul(&r_c2gv, &tr(&r_t_to_tp1));
        let view_next = col(&r_next, 2);
        let v1 = normalize([view[0], 0., view[2]]);
        let v2 = normalize([view_next[0], 0., view_next[2]]);
        let ang = dot(v1, v2).clamp(-1., 1.).acos();
        let aa = scale(normalize(cross(v2, v1)), ang);
        aa_x.push(aa[0]);
        aa_y.push(aa[1]);
        aa_z.push(aa[2]);
    }
    let (sx, sy, sz) = (gaussian_smooth(&aa_x, 3.), gaussian_smooth(&aa_y, 3.), gaussian_smooth(&aa_z, 3.));
    let mut r_t_to_0 = vec![I3; l];
    for i in 1..l {
        let r_tp1_to_t = tr(&aa_to_matrix([sx[i], sy[i], sz[i]]));
        r_t_to_0[i] = mul(&r_t_to_0[i - 1], &r_tp1_to_t);
    }
    let global_orient: Vec<V3> = (0..l).map(|t| matrix_to_aa(&mul(&as_identity(r_t_to_0[t]), &r_gv[t]))).collect();
    // rollout_local_transl_vel
    let mut transl = vec![[0.; 3]; l];
    for t in 1..l {
        transl[t] = add(transl[t - 1], mv(&aa_to_matrix(global_orient[t - 1]), vel[t - 1]));
    }
    // any->ay: R = aa2mat([0,0,π])
    let r = aa_to_matrix([0., 0., std::f64::consts::PI]);
    let go = global_orient.iter().map(|a| matrix_to_aa(&mul(&r, &aa_to_matrix(*a)))).collect();
    let tl = transl.iter().map(|v| mv(&r, *v)).collect();
    (go, tl)
}

fn pp_static_joint(skel: &Skeleton, g: &SmplParams, logits: &[[f64; 6]]) -> Vec<V3> {
    let l = g.transl.len();
    let j = fk_seq(skel, g);
    let mut d = Vec::with_capacity(l - 1);
    for t in 0..l - 1 {
        let lg = logits[t];
        let lab: Vec<bool> = lg.iter().map(|v| *v > 0.).collect();
        let w: Vec<f64> = {
            let z: Vec<f64> = (0..6).map(|k| lg[k] - if lab[k] { 0. } else { 1e6 }).collect();
            let m = z.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
            let ex: Vec<f64> = z.iter().map(|v| (v - m).exp()).collect();
            let s: f64 = ex.iter().sum();
            ex.iter().map(|v| v / s).collect()
        };
        let mut disp = [0.; 3];
        if lab.iter().any(|b| *b) {
            for (k, &jid) in STATIC_JOINTS.iter().enumerate() {
                disp = add(disp, scale(sub(j[t + 1][jid], j[t][jid]), w[k]));
            }
        }
        d.push(sub(sub(g.transl[t + 1], g.transl[t]), disp));
    }
    let mut post = vec![g.transl[0]; l];
    for t in 1..l {
        post[t] = add(post[t - 1], d[t - 1]);
    }
    let xs = gaussian_smooth(&post.iter().map(|p| p[0]).collect::<Vec<_>>(), 3.);
    let zs = gaussian_smooth(&post.iter().map(|p| p[2]).collect::<Vec<_>>(), 3.);
    for t in 0..l {
        post[t][0] = xs[t];
        post[t][2] = zs[t];
    }
    let mut ground = f64::INFINITY;
    for t in 0..l {
        for k in 0..22 {
            ground = ground.min(j[t][k][1] - g.transl[t][1] + post[t][1]);
        }
    }
    for p in &mut post {
        p[1] -= ground;
    }
    post
}

fn pp_static_joint_cam(skel: &Skeleton, g: &SmplParams, c: &SmplParams, logits: &[[f64; 6]]) -> Vec<V3> {
    let l = g.transl.len();
    let jw = fk_seq(skel, g);
    let mut c = c.clone();
    for k in 0..3 {
        let s = gaussian_smooth(&c.transl.iter().map(|p| p[k]).collect::<Vec<_>>(), 5.);
        for t in 0..l {
            c.transl[t][k] = s[t];
        }
    }
    let jc = fk_seq(skel, &c);
    let r_c2w = mul(&aa_to_matrix(g.global_orient[0]), &tr(&aa_to_matrix(c.global_orient[0])));
    let t_c2w = sub(jw[0][0], mv(&r_c2w, jc[0][0]));
    let jc_w: Vec<V3> = (0..l).map(|t| add(mv(&r_c2w, jc[t][0]), t_c2w)).collect();
    let mut transl = g.transl.clone();
    let mut post = jw.clone();
    for i in 1..l {
        let mut diff = sub(post[i][0], jc_w[i]);
        for k in 0..3 {
            if diff[k] > -0.25 && diff[k] < 0.25 {
                diff[k] = 0.;
            }
            diff[k] = diff[k].clamp(-0.02, 0.02);
        }
        for t in i..l {
            transl[t] = sub(transl[t], diff);
            for jn in 0..22 {
                post[t][jn] = sub(post[t][jn], diff);
            }
        }
    }
    let mut pred_disp = Vec::with_capacity(l - 1);
    for t in 0..l - 1 {
        let mut sum = [0.; 3];
        let mut n = 0.;
        for (k, &jid) in STATIC_JOINTS.iter().enumerate() {
            if 1. / (1. + (-logits[t][k]).exp()) > 0.8 {
                sum = add(sum, sub(post[t + 1][jid], post[t][jid]));
                n += 1.;
            }
        }
        let mut d = scale(sum, 1. / f64::max(n, 1.));
        d[1] = 0.;
        pred_disp.push(d);
    }
    for i in 1..l {
        let d = pred_disp[i - 1];
        for t in i..l {
            transl[t] = sub(transl[t], d);
            for jn in 0..22 {
                post[t][jn] = sub(post[t][jn], d);
            }
        }
    }
    let ground = post.iter().flat_map(|f| f.iter().map(|p| p[1])).fold(f64::INFINITY, f64::min);
    for p in &mut transl {
        p[1] -= ground;
    }
    transl
}

/// `process_ik`: returns the IK-corrected body pose.
fn process_ik(skel: &Skeleton, g: &SmplParams, logits: &[[f64; 6]]) -> Vec<[V3; 21]> {
    let l = g.transl.len();
    let conf: Vec<[f64; 6]> = logits.iter().map(|lg| std::array::from_fn(|k| 1. / (1. + (-lg[k]).exp()))).collect();
    let mut frames: Vec<([V3; 22], Vec<Tf>)> = (0..l)
        .map(|t| {
            let (j, local, _) = fk(skel, g.global_orient[t], &g.body_pose[t], &g.betas, g.transl[t]);
            (j, local)
        })
        .collect();
    let mut target: Vec<[V3; 22]> = frames.iter().map(|f| f.0).collect();
    for i in 1..l {
        for (k, &jid) in STATIC_JOINTS.iter().enumerate() {
            let c = conf[i - 1][k];
            target[i][jid] = add(scale(target[i - 1][jid], c), scale(frames[i].0[jid], 1. - c));
        }
    }
    let chains: [(&[usize; 5], &[usize], &[usize]); 4] = [
        (&[0, 1, 4, 7, 10], &[3, 4], &[7, 10]),
        (&[0, 2, 5, 8, 11], &[3, 4], &[8, 11]),
        (&[9, 13, 16, 18, 20], &[4], &[20]),
        (&[9, 14, 17, 19, 21], &[4], &[21]),
    ];
    let mut out = Vec::with_capacity(l);
    for t in 0..l {
        let local = &mut frames[t].1;
        for (chain, tind, tjoints) in chains.iter() {
            let targets: Vec<V3> = tjoints.iter().map(|&j| target[t][j]).collect();
            ccd_ik(local, chain, tind, &targets);
        }
        let mut pose = [[0.; 3]; 21];
        for j in 1..22 {
            pose[j - 1] = matrix_to_aa(&local[j].r);
        }
        out.push(pose);
    }
    out
}

/// `CCD_IK(max_iter=2, pos_weight=1, rot_weight=0)` on one frame; writes chain rotations back.
fn ccd_ik(local: &mut [Tf], chain: &[usize; 5], target_ind: &[usize], target_pos: &[V3]) {
    let world_full = forward_kinematics(local, &PARENTS);
    let mut cl: Vec<Tf> = chain.iter().map(|&j| local[j]).collect();
    cl[0] = world_full[chain[0]];
    let parent = [-1, 0, 1, 2, 3];
    let n = 5;
    let mut gw = forward_kinematics(&cl, &parent);
    for _ in 0..2 {
        for i in 1..n - 1 {
            let pos = gw[i].t;
            let quat = matrix_to_quat(&gw[i].r);
            let mut xs = [0.; 3];
            let mut ys = [0.; 3];
            let mut count = 0.;
            for (ti, &j) in target_ind.iter().enumerate() {
                if i >= j {
                    continue;
                }
                let end = gw[j].t;
                let solved = qslerp(quat, qmul(qbetween(sub(end, pos), sub(target_pos[ti], pos)), quat), (i + 1) as f64 / n as f64);
                xs = add(xs, qrot(solved, [1., 0., 0.]));
                ys = add(ys, qrot(solved, [0., 1., 0.]));
                count += 1.;
            }
            if count > 0. {
                let nz = |v: V3| scale(v, 1. / norm(v).max(1e-9));
                let x = nz(scale(xs, 1. / count));
                let y = nz(scale(ys, 1. / count));
                let z = cross(x, y);
                let solved = [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]];
                let parent_rot = gw[parent[i] as usize].r;
                cl[i].r = normalized_matrix(&mul(&normalized_matrix(&inverse(&parent_rot)), &solved));
                gw = forward_kinematics(&cl, &parent);
            }
        }
    }
    for k in 1..5 {
        local[chain[k]].r = cl[k].r;
    }
}
