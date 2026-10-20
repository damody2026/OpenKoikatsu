//! Offline training on the game's own clips (replaces Tools/video_motion/{helper_model,
//! joint_limits,baseline,export_model,kkrig}.py). Outputs the data the runtime reads
//! (`Tools/video_motion/data/video_motion_model.json`, see model.rs).
//!
//!   ok-extract <game> --export-training-clips <dir> "" 1500           clips
//!   ok-extract x --train-video-helpers <clips> <rest_skeleton.json> <helper_model.json>
//!   ok-extract x --train-video-limits <clips> <rest_skeleton.json> <joint_limits.json>
//!   ok-extract x --validate-video-clip <card> <native.okanim> <dump.jsonl> [game]   per clip
//!   ok-extract x --train-video-baseline <out.json> <dump.jsonl ...>
//!   ok-extract x --export-video-model <data dir>                       → video_motion_model.json

use super::math::*;
use super::rig::{mapping, Rig};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};

const SAMPLE_FPS: f32 = 10.;
const SKIP_PREFIX: [&str; 7] = ["cf_j_thumb", "cf_j_index", "cf_j_middle", "cf_j_ring", "cf_j_little", "cf_j_root", "cf_n_height"];

type Tracks = BTreeMap<String, BTreeMap<u32, Vec<Vec<f32>>>>;

fn clips(dir: &Path) -> Result<Vec<PathBuf>, String> {
    let mut files: Vec<PathBuf> = std::fs::read_dir(dir)
        .map_err(|e| format!("{}: {e}", dir.display()))?
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().is_some_and(|x| x == "okanim"))
        .collect();
    files.sort();
    Ok(files)
}

fn read_tracks(path: &Path) -> Result<Tracks, String> {
    let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
    crate::anim::resource_tracks(&bytes, SAMPLE_FPS)
}

/// Rest-relative rotation vector of a local quaternion (x y z w): log(R_rest⁻¹ · R).
fn rel_rot(rest: &M3, q: &[f32]) -> V3 {
    log(&tmul(rest, &from_quat([q[0] as f64, q[1] as f64, q[2] as f64, q[3] as f64])))
}

/// numpy.percentile (linear interpolation).
fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() {
        return 0.;
    }
    let pos = p / 100. * (sorted.len() - 1) as f64;
    let (lo, hi) = (pos.floor() as usize, pos.ceil() as usize);
    sorted[lo] + (sorted[hi] - sorted[lo]) * (pos - lo as f64)
}
fn sorted(mut v: Vec<f64>) -> Vec<f64> {
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    v
}

// ---------------------------------------------------------------- helper bones

/// Mapped joints relevant to a helper: nearest mapped ancestor, its mapped parent and its mapped children.
fn drivers_for(rig: &Rig, name: &str, mapped: &[&str]) -> Vec<String> {
    let names = |i: usize| -> Vec<&str> {
        let c = rig.chain(i);
        c[..c.len() - 1].iter().map(|&j| rig.bones[j].name.as_str()).collect()
    };
    let anc: Vec<&str> = names(rig.idx(name)).into_iter().filter(|b| mapped.contains(b)).collect();
    let Some(&a) = anc.last() else { return Vec::new() };
    let mut out: Vec<String> = vec![a.to_string()];
    if anc.len() >= 2 {
        out.push(anc[anc.len() - 2].to_string());
    }
    for &m in mapped {
        let manc: Vec<&str> = names(rig.idx(m)).into_iter().filter(|b| mapped.contains(b)).collect();
        if manc.last() == Some(&a) {
            out.push(m.to_string());
        }
    }
    let mut seen = std::collections::HashSet::new();
    out.retain(|d| seen.insert(d.clone()));
    out
}

/// Solves (AᵀA + reg)·X = AᵀY for small dense systems (Gaussian elimination, partial pivoting).
fn solve(mut a: Vec<Vec<f64>>, mut b: Vec<Vec<f64>>) -> Vec<Vec<f64>> {
    let n = a.len();
    let m = b[0].len();
    for c in 0..n {
        let p = (c..n).max_by(|&i, &j| a[i][c].abs().partial_cmp(&a[j][c].abs()).unwrap()).unwrap();
        a.swap(c, p);
        b.swap(c, p);
        let d = a[c][c];
        if d.abs() < 1e-300 {
            continue;
        }
        for r in 0..n {
            if r == c {
                continue;
            }
            let f = a[r][c] / d;
            if f == 0. {
                continue;
            }
            for k in c..n {
                a[r][k] -= f * a[c][k];
            }
            for k in 0..m {
                b[r][k] -= f * b[c][k];
            }
        }
    }
    (0..n).map(|r| (0..m).map(|k| if a[r][r].abs() < 1e-300 { 0. } else { b[r][k] / a[r][r] }).collect()).collect()
}

/// Ridge regression per helper track: rest-relative rotation vectors of nearby mapped
/// joints → the helper's local position / rotation vector / scale. Writes helper_model.json
/// (the `helpers` array of video_motion_model.json).
pub fn train_helpers(dir: &Path, skeleton: &Path, out: &Path) -> Result<String, String> {
    let rig = Rig::load(skeleton)?;
    let map = mapping();
    let mapped: Vec<&str> = map.iter().map(|m| m.0).collect();
    let rest = |n: &str| rig.local_rot[rig.idx(n)];
    let files = clips(dir)?;
    // features: per frame, 3 values per mapped joint
    let mut x: Vec<Vec<f64>> = Vec::new();
    let mut clip_of: Vec<usize> = Vec::new();
    let mut starts: Vec<usize> = Vec::new();
    // helper → attr → [(clip, frames × dim)]
    let mut targets: BTreeMap<String, BTreeMap<u32, Vec<(usize, Vec<Vec<f64>>)>>> = BTreeMap::new();
    let mut usable = 0usize;
    for f in &files {
        let tr = match read_tracks(f) {
            Ok(t) => t,
            Err(e) => {
                eprintln!("skip {}: {e}", f.display());
                continue;
            }
        };
        if !mapped.iter().all(|m| tr.get(*m).is_some_and(|a| a.contains_key(&2))) {
            continue;
        }
        let n = tr[mapped[0]][&2].len();
        if mapped.iter().any(|m| tr[*m][&2].len() != n) {
            continue;
        }
        starts.push(x.len());
        for fr in 0..n {
            let mut row = Vec::with_capacity(3 * mapped.len());
            for m in &mapped {
                row.extend(rel_rot(&rest(m), &tr[*m][&2][fr]));
            }
            x.push(row);
            clip_of.push(usable);
        }
        for (name, attrs) in &tr {
            if mapped.contains(&name.as_str()) || SKIP_PREFIX.iter().any(|p| name.starts_with(p)) || !rig.index.contains_key(name) {
                continue;
            }
            let path = &rig.bones[rig.idx(name)].path;
            if !path.contains("cf_j_root") || path.contains("cf_j_head") {
                continue;
            }
            let t = targets.entry(name.clone()).or_default();
            for (attr, v) in attrs {
                if v.len() != n {
                    continue;
                }
                let vals: Vec<Vec<f64>> = if *attr == 2 {
                    v.iter().map(|q| rel_rot(&rest(name), q).to_vec()).collect()
                } else {
                    v.iter().map(|r| r.iter().map(|x| *x as f64).collect()).collect()
                };
                t.entry(*attr).or_default().push((usable, vals));
            }
        }
        usable += 1;
    }
    if usable == 0 {
        return Err(format!("{} 沒有可用的 .okanim（需要 22 個主要關節都有旋轉）", dir.display()));
    }
    let col: HashMap<&str, usize> = mapped.iter().enumerate().map(|(i, m)| (*m, i)).collect();
    let mut helpers = Vec::new();
    let mut report = String::new();
    let (mut kinds_fit, mut kinds_mean, mut kinds_skip) = (0, 0, 0);
    for (name, attrs) in &targets {
        let drivers = drivers_for(&rig, name, &mapped);
        if drivers.is_empty() {
            continue;
        }
        let cols: Vec<usize> = drivers.iter().flat_map(|d| { let base = 3 * col[d.as_str()]; (0..3).map(move |c| base + c) }).collect();
        for (attr, chunks) in attrs {
            if (chunks.len() as f64) < 0.5 * usable as f64 {
                continue; // rarely animated: leave to rest / shape
            }
            let mut xs: Vec<Vec<f64>> = Vec::new();
            let mut ys: Vec<Vec<f64>> = Vec::new();
            let mut hold: Vec<bool> = Vec::new();
            for (c, v) in chunks {
                for (k, y) in v.iter().enumerate() {
                    let r = &x[starts[*c] + k];
                    xs.push(cols.iter().map(|&i| r[i]).collect());
                    ys.push(y.clone());
                    hold.push(clip_of[starts[*c] + k] % 10 == 0);
                }
            }
            let dim = ys[0].len();
            let nf = cols.len();
            let train_idx: Vec<usize> = (0..ys.len()).filter(|&i| !hold[i]).collect();
            let hold_idx: Vec<usize> = (0..ys.len()).filter(|&i| hold[i]).collect();
            let mean = |idx: &[usize]| -> Vec<f64> { (0..dim).map(|d| idx.iter().map(|&i| ys[i][d]).sum::<f64>() / idx.len().max(1) as f64).collect() };
            let all: Vec<usize> = (0..ys.len()).collect();
            let all_mean = mean(&all);
            let std_max = (0..dim).map(|d| (ys.iter().map(|y| (y[d] - all_mean[d]).powi(2)).sum::<f64>() / ys.len() as f64).sqrt()).fold(0., f64::max);
            let (mut w, mut b) = (vec![vec![0.; dim]; nf], all_mean.clone());
            if std_max >= 1e-5 {
                // [X 1] with ridge 1e-2 on the weights, not the bias
                let mut ata = vec![vec![0.; nf + 1]; nf + 1];
                let mut aty = vec![vec![0.; dim]; nf + 1];
                for &i in &train_idx {
                    let mut a = xs[i].clone();
                    a.push(1.);
                    for r in 0..=nf {
                        for c in 0..=nf {
                            ata[r][c] += a[r] * a[c];
                        }
                        for d in 0..dim {
                            aty[r][d] += a[r] * ys[i][d];
                        }
                    }
                }
                for r in 0..nf {
                    ata[r][r] += 1e-2;
                }
                let sol = solve(ata, aty);
                w = sol[..nf].to_vec();
                b = sol[nf].clone();
            }
            let pred = |i: usize| -> Vec<f64> { (0..dim).map(|d| b[d] + (0..nf).map(|k| xs[i][k] * w[k][d]).sum::<f64>()).collect() };
            let dist = |a: &[f64], c: &[f64]| a.iter().zip(c).map(|(x, y)| (x - y).powi(2)).sum::<f64>().sqrt();
            let train_mean = mean(&train_idx);
            let (err, base) = if hold_idx.is_empty() {
                (0., 0.)
            } else {
                (
                    hold_idx.iter().map(|&i| dist(&pred(i), &ys[i])).sum::<f64>() / hold_idx.len() as f64,
                    hold_idx.iter().map(|&i| dist(&ys[i], &train_mean)).sum::<f64>() / hold_idx.len() as f64,
                )
            };
            // Keep a regression only when it clearly beats the clip mean; nearly constant tracks are
            // written as their mean (as native clips do); unpredictable ones (physics-driven) are left alone.
            let kind = if err < 0.8 * base {
                kinds_fit += 1;
                "fit"
            } else if base < 0.01 {
                kinds_mean += 1;
                w = vec![vec![0.; dim]; nf];
                b = all_mean.clone();
                "mean"
            } else {
                kinds_skip += 1;
                "skip"
            };
            let unit = match attr { 2 => "rad", 1 => "m", _ => "" };
            report.push_str(&format!("{name:28} attr{attr} {kind:4} holdout err {err:.4}{unit}  (constant {base:.4})\n"));
            if kind != "skip" {
                helpers.push(json!({"bone": name, "attr": attr, "drivers": drivers, "w": w, "b": b}));
            }
        }
    }
    report.push_str(&format!("clips {usable}/{} frames {} helpers fit {kinds_fit} mean {kinds_mean} skip {kinds_skip}\n", files.len(), x.len()));
    std::fs::write(out, serde_json::to_string(&json!({"helpers": helpers})).unwrap()).map_err(|e| format!("{}: {e}", out.display()))?;
    report.push_str(&format!("wrote {} ({} tracks)\n", out.display(), helpers.len()));
    Ok(report)
}

// ---------------------------------------------------------------- joint limits

/// Per mapped joint: 0.5 / 99.5 percentile box of the rest-relative rotation vector and the
/// 99.5 percentile angle, over all native clips.
pub fn train_limits(dir: &Path, skeleton: &Path, out: &Path) -> Result<String, String> {
    let rig = Rig::load(skeleton)?;
    let names: Vec<&str> = mapping().iter().map(|m| m.0).collect();
    let mut vecs: BTreeMap<&str, Vec<V3>> = names.iter().map(|n| (*n, Vec::new())).collect();
    for f in clips(dir)? {
        let Ok(tr) = read_tracks(&f) else { continue };
        for n in &names {
            if let Some(q) = tr.get(*n).and_then(|a| a.get(&2)) {
                let rest = rig.local_rot[rig.idx(n)];
                vecs.get_mut(n).unwrap().extend(q.iter().map(|q| rel_rot(&rest, q)));
            }
        }
    }
    let mut limits = serde_json::Map::new();
    let mut report = String::new();
    for n in &names {
        let v = &vecs[n];
        if v.is_empty() {
            return Err(format!("{n} 在所有片段中都沒有旋轉資料"));
        }
        let axis = |c: usize| sorted(v.iter().map(|r| r[c]).collect());
        let (a0, a1, a2) = (axis(0), axis(1), axis(2));
        let lo = [percentile(&a0, 0.5), percentile(&a1, 0.5), percentile(&a2, 0.5)];
        let hi = [percentile(&a0, 99.5), percentile(&a1, 99.5), percentile(&a2, 99.5)];
        let ang = percentile(&sorted(v.iter().map(|r| norm(*r)).collect()), 99.5).to_degrees();
        report.push_str(&format!("{n:20} max {ang:5.1} deg  lo [{:.0} {:.0} {:.0}] hi [{:.0} {:.0} {:.0}]\n", lo[0].to_degrees(), lo[1].to_degrees(), lo[2].to_degrees(), hi[0].to_degrees(), hi[1].to_degrees(), hi[2].to_degrees()));
        limits.insert(n.to_string(), json!({"lo": lo, "hi": hi, "angle99_5": ang, "frames": v.len()}));
    }
    std::fs::write(out, serde_json::to_string_pretty(&Value::Object(limits)).unwrap()).map_err(|e| format!("{}: {e}", out.display()))?;
    report.push_str(&format!("wrote {}\n", out.display()));
    Ok(report)
}

// ---------------------------------------------------------------- validation baselines

const LIMBS: [&str; 9] = ["cf_j_hand_L", "cf_j_hand_R", "cf_j_leg03_L", "cf_j_leg03_R", "cf_j_head", "cf_j_forearm01_L", "cf_j_forearm01_R", "cf_j_leg01_L", "cf_j_leg01_R"];
const BIN_DEG: f64 = 20.;
const MIN_BIN_SAMPLES: usize = 20;

fn hinges() -> Vec<(String, [String; 3])> {
    let mut h = Vec::new();
    for s in ["L", "R"] {
        h.push((format!("cf_j_forearm01_{s}"), [format!("cf_j_arm00_{s}"), format!("cf_j_forearm01_{s}"), format!("cf_j_hand_{s}")]));
    }
    for s in ["L", "R"] {
        h.push((format!("cf_j_leg01_{s}"), [format!("cf_j_thigh00_{s}"), format!("cf_j_leg01_{s}"), format!("cf_j_leg03_{s}")]));
    }
    h
}

fn v3(v: &Value) -> V3 { std::array::from_fn(|i| v[i].as_f64().unwrap_or(0.)) }

/// Allowed per-region stretch (native p99 ×1.15), compression (native p1 ÷1.15, also per
/// 20° bend bin of elbows / knees with ≥ 20 samples) and per-joint kink (native p99.9 ×1.5),
/// from `--validate-video-clip` dumps of native clips.
pub fn train_baseline(dumps: &[PathBuf], out: &Path) -> Result<String, String> {
    let mut vals: BTreeMap<String, Vec<f64>> = BTreeMap::new();
    let hinges = hinges();
    for p in dumps {
        let text = std::fs::read_to_string(p).map_err(|e| format!("{}: {e}", p.display()))?;
        let rows: Vec<Value> = text.lines().filter(|l| !l.trim().is_empty()).map(|l| serde_json::from_str(l).map_err(|e| format!("{}: {e}", p.display()))).collect::<Result<_, _>>()?;
        if rows.is_empty() {
            continue;
        }
        for r in &rows {
            let j = &r["joints"];
            let mut bends: HashMap<&str, f64> = HashMap::new();
            for (region, [a, b, c]) in &hinges {
                let u = sub(v3(&j[b.as_str()]), v3(&j[a.as_str()]));
                let w = sub(v3(&j[c.as_str()]), v3(&j[b.as_str()]));
                bends.insert(region.as_str(), (dot(u, w) / (norm(u) * norm(w))).clamp(-1., 1.).acos().to_degrees());
            }
            if let Some(stretch) = r["stretch"].as_object() {
                for (region, v) in stretch {
                    let Some(arr) = v.as_array() else { continue };
                    if let Some(x) = arr.first().and_then(Value::as_f64) {
                        vals.entry(region.clone()).or_default().push(x);
                    }
                    if arr.len() > 2 {
                        if let Some(c) = arr[2].as_f64() {
                            vals.entry(format!("compress:{region}")).or_default().push(c);
                            if let Some(bend) = bends.get(region.as_str()) {
                                let bin = ((bend / BIN_DEG).floor() * BIN_DEG) as i64;
                                vals.entry(format!("compress:{region}@{bin}")).or_default().push(c);
                            }
                        }
                    }
                }
            }
        }
        for name in LIMBS {
            let pos: Vec<V3> = rows.iter().map(|r| sub(v3(&r["joints"][name]), v3(&r["joints"]["cf_j_hips"]))).collect();
            let e = vals.entry(format!("jitter:{name}")).or_default();
            for i in 1..pos.len().saturating_sub(1) {
                e.push(norm(sub(pos[i], scale(add(pos[i + 1], pos[i - 1]), 0.5))));
            }
        }
    }
    let mut outm = serde_json::Map::new();
    for (k, v) in vals {
        if k.contains('@') && v.len() < MIN_BIN_SAMPLES {
            continue; // too little native evidence: no verdict
        }
        let s = sorted(v);
        let limit = if k.starts_with("jitter:") {
            percentile(&s, 99.9) * 1.5
        } else if k.starts_with("compress:") {
            percentile(&s, 1.) / 1.15
        } else {
            percentile(&s, 99.) * 1.15
        };
        outm.insert(k, json!(limit));
    }
    let n = outm.len();
    std::fs::write(out, serde_json::to_string_pretty(&Value::Object(outm)).unwrap()).map_err(|e| format!("{}: {e}", out.display()))?;
    Ok(format!("wrote {} ({n} keys from {} dumps)\n", out.display(), dumps.len()))
}

// ---------------------------------------------------------------- bundle

/// helper_model.json + joint_limits.json + stretch_baseline[_male].json → video_motion_model.json.
pub fn export_model(data: &Path) -> Result<String, String> {
    let read = |name: &str| -> Result<Value, String> {
        let p = data.join(name);
        serde_json::from_str(&std::fs::read_to_string(&p).map_err(|e| format!("{}: {e}", p.display()))?).map_err(|e| format!("{}: {e}", p.display()))
    };
    let helpers = read("helper_model.json")?["helpers"].clone();
    let mut model = json!({"version": 1, "helpers": helpers, "limits": read("joint_limits.json")?, "baseline": read("stretch_baseline.json")?});
    if data.join("stretch_baseline_male.json").is_file() {
        model["baseline_male"] = read("stretch_baseline_male.json")?;
    }
    let out = data.join("video_motion_model.json");
    std::fs::write(&out, serde_json::to_string(&model).unwrap()).map_err(|e| format!("{}: {e}", out.display()))?;
    // the runtime must accept it
    super::model::Model::load(&out)?;
    Ok(format!("wrote {} helpers={}\n", out.display(), model["helpers"].as_array().map_or(0, |a| a.len())))
}
