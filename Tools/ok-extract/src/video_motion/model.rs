//! Offline-trained data (ok-extract x --export-video-model, train.rs ->
//! data/video_motion_model.json): helper-bone regressions, native joint
//! limits and validation baselines, all measured on the game's own clips.

use super::math::*;
use super::rig::Rig;
use std::collections::HashMap;
use std::path::Path;

#[derive(serde::Deserialize)]
pub struct Helper {
    pub bone: String,
    pub attr: u32,
    pub drivers: Vec<String>,
    pub w: Vec<Vec<f64>>,
    pub b: Vec<f64>,
}

#[derive(serde::Deserialize)]
pub struct Limit {
    pub lo: [f64; 3],
    pub hi: [f64; 3],
    pub angle99_5: f64,
}

#[derive(serde::Deserialize)]
pub struct Model {
    pub helpers: Vec<Helper>,
    pub limits: HashMap<String, Limit>,
    pub baseline: HashMap<String, f64>,
    /// Same statistics measured on a male card (male body mesh differs).
    #[serde(default)]
    pub baseline_male: HashMap<String, f64>,
}

impl Model {
    /// Validation baseline for the session's sex (0 male, 1 female).
    pub fn baseline_for(&self, sex: i32) -> &HashMap<String, f64> {
        if sex == 0 && !self.baseline_male.is_empty() { &self.baseline_male } else { &self.baseline }
    }
    pub fn load(path: &Path) -> Result<Model, String> {
        serde_json::from_slice(&crate::gamefs::read(path).map_err(|e| format!("{}: {e}", path.display()))?).map_err(|e| e.to_string())
    }
}

/// Helper/correction bone tracks from the mapped joints' absolute locals:
/// (bone, attribute, per-frame values). Rotations are returned as continuous
/// quaternions; positions/scales as vectors.
pub fn predict_helpers(model: &Model, rig: &Rig, locals: &HashMap<String, Vec<M3>>) -> Vec<(String, u32, Vec<Vec<f64>>)> {
    let mut rel: HashMap<&str, Vec<V3>> = HashMap::new();
    for (name, l) in locals {
        let rest = rig.local_rot[rig.idx(name)];
        rel.insert(name.as_str(), l.iter().map(|m| log(&tmul(&rest, m))).collect());
    }
    let mut out = Vec::new();
    for h in &model.helpers {
        if !rig.index.contains_key(&h.bone) || h.drivers.iter().any(|d| !rel.contains_key(d.as_str())) { continue; }
        let frames = rel[h.drivers[0].as_str()].len();
        let dim = h.b.len();
        let mut values: Vec<Vec<f64>> = (0..frames).map(|f| {
            let mut y = h.b.clone();
            for (k, d) in h.drivers.iter().enumerate() {
                let x = rel[d.as_str()][f];
                for c in 0..3 { for (o, yo) in y.iter_mut().enumerate().take(dim) { *yo += x[c] * h.w[3 * k + c][o]; } }
            }
            y
        }).collect();
        if h.attr == 2 {
            let rest = from_quat(rig.bones[rig.idx(&h.bone)].rot);
            let mut q: Vec<[f64; 4]> = values.iter().map(|y| to_quat(&mul(&rest, &exp([y[0], y[1], y[2]])))).collect();
            continuous(&mut q);
            values = q.iter().map(|x| x.to_vec()).collect();
        }
        out.push((h.bone.clone(), h.attr, values));
    }
    out
}
