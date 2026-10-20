use crate::{
    geometry::*,
    io::{Editor, atomic_json, id, owned_file, read_json, valid_id},
};
use anyhow::{Context, Result, bail, ensure};
use serde_json::{Value, json};
use std::{collections::HashSet, path::PathBuf, sync::Arc, time::Instant};
use tokio_util::sync::CancellationToken;

pub fn text_result(data: Value, images: Vec<Value>) -> Value {
    let mut content = vec![json!({"type":"text","text":data.to_string()})];
    content.extend(images);
    json!({"content":content,"isError":false})
}
fn number(v: &Value, key: &str, default: f64) -> f64 {
    v[key].as_f64().unwrap_or(default)
}
fn string<'a>(v: &'a Value, key: &str, default: &'a str) -> &'a str {
    v[key].as_str().unwrap_or(default)
}
fn masks_content(masks: &[Mask]) -> Result<Vec<Value>> {
    masks.iter().map(Mask::content).collect()
}
#[derive(Clone)]
pub struct Fitting {
    pub editor: Arc<dyn Editor>,
}
impl Fitting {
    pub fn root(&self) -> PathBuf {
        self.editor.root().join("fitting")
    }
    pub fn geometry(
        &self,
        session: &Value,
        cancel: &CancellationToken,
    ) -> Result<(Geometry, Value)> {
        let state = self
            .editor
            .rpc("geometry", &json!({"session":session}), cancel)?;
        let path = PathBuf::from(
            state["geometry"]
                .as_str()
                .context("Missing geometry path")?,
        );
        let owned = owned_file(self.editor.root(), "geometry", &path, 64_000_000)?;
        ensure!(
            path.extension().is_some_and(|s| s == "json"),
            "Invalid geometry extension"
        );
        let data = read_json(&owned, 64_000_000)?;
        let _ = std::fs::remove_file(&owned);
        Ok((Geometry::parse(data)?, state))
    }
    pub fn dataset(&self, identifier: &str) -> Result<(PathBuf, Value)> {
        valid_id(identifier)?;
        let folder = self.root().join(identifier);
        let reference = read_json(&folder.join("reference.json"), 8_000_000)?;
        Ok((folder, reference))
    }
    pub fn call(&self, action: &str, args: &Value, cancel: &CancellationToken) -> Result<Value> {
        let (result, masks) = match action {
            "reference" => {
                let (g, _) = self.geometry(&args["session"], cancel)?;
                let mut frame = if args["projection_frame"].is_object() {
                    serde_json::from_value(args["projection_frame"].clone())?
                } else {
                    Frame::new(
                        &g,
                        args["width"].as_u64().unwrap_or(256) as u32,
                        args["height"].as_u64().unwrap_or(384) as u32,
                    )?
                };
                if args["projection_frame"].is_null() {
                    frame.side = string(args, "side_view", "right").into();
                }
                frame.validate()?;
                let background = if let Some(a) = args["background"].as_array() {
                    Some([
                        a[0].as_u64().context("Invalid background")? as u8,
                        a[1].as_u64().context("Invalid background")? as u8,
                        a[2].as_u64().context("Invalid background")? as u8,
                    ])
                } else {
                    None
                };
                let mut masks = Vec::new();
                for view in VIEWS {
                    masks.push(read_mask(
                        &PathBuf::from(args[view].as_str().context("Reference path required")?),
                        string(args, "mode", "mask"),
                        background,
                        number(args, "tolerance", 24.),
                    )?);
                }
                let transforms = if args["projection_frame"].is_object() {
                    ensure!(
                        masks
                            .iter()
                            .all(|m| m.width == frame.width && m.height == frame.height),
                        "Known projection references must match the supplied frame dimensions"
                    );
                    json!([{"scale":1,"translation":[0,0],"known_projection":true},{"scale":1,"translation":[0,0],"known_projection":true},{"scale":1,"translation":[0,0],"known_projection":true}])
                } else {
                    let (aligned, t) =
                        align_masks(&masks, &frame, args["physical_height"].as_f64())?;
                    masks = aligned;
                    t
                };
                let identifier = id();
                let folder = self.root().join(&identifier);
                save_masks(&folder, &masks)?;
                let result = json!({"reference":identifier,"frame":frame,"alignment":transforms,"scale_known":args["physical_height"].is_number()||args["projection_frame"].is_object(),"note":"Absolute stature requires physical_height or a known projection. Pose must match; reference masks must exclude hair/clothing."});
                atomic_json(&folder.join("reference.json"), &result)?;
                (result, masks)
            }
            "masks" | "compare" | "sections" => {
                let (g, state) = self.geometry(&args["session"], cancel)?;
                if action == "sections" {
                    let heights: Vec<f64> = serde_json::from_value(args["heights"].clone())?;
                    let mut result = sections(&g, &heights);
                    result["session"] = state["session"].clone();
                    result["revision"] = state["revision"].clone();
                    (result, vec![])
                } else {
                    let (frame, folder) = if let Some(reference) = args["reference"].as_str() {
                        let (folder, data) = self.dataset(reference)?;
                        (
                            serde_json::from_value::<Frame>(data["frame"].clone())?,
                            Some(folder),
                        )
                    } else {
                        let mut frame = Frame::new(
                            &g,
                            args["width"].as_u64().unwrap_or(256) as u32,
                            args["height"].as_u64().unwrap_or(384) as u32,
                        )?;
                        frame.side = string(args, "side", "right").into();
                        (frame, None)
                    };
                    let masks = rasterize(&g, &frame)?;
                    let mut result = json!({"session":state["session"],"revision":state["revision"],"frame":frame,"views":VIEWS});
                    if action == "compare" {
                        let folder = folder.context("Reference required")?;
                        let targets = load_masks(&folder)?;
                        let scores = metrics(&masks, &targets)?;
                        for (k, v) in scores.as_object().unwrap() {
                            result[k] = v.clone();
                        }
                    }
                    (result, masks)
                }
            }
            "fit" | "sensitivity" => self.fit(args, cancel, action == "sensitivity")?,
            _ => bail!("Unknown fitting action"),
        };
        Ok(text_result(result, masks_content(&masks)?))
    }
    pub fn fit(
        &self,
        args: &Value,
        cancel: &CancellationToken,
        sensitivity: bool,
    ) -> Result<(Value, Vec<Mask>)> {
        let (folder, reference) =
            self.dataset(args["reference"].as_str().context("Reference required")?)?;
        let targets = load_masks(&folder)?;
        let frame: Frame = serde_json::from_value(reference["frame"].clone())?;
        frame.validate()?;
        let mut state = self.editor.rpc("state", &json!({}), cancel)?;
        ensure!(
            state["session"] == args["session"]
                && state["revision"] == args["revision"]
                && state["pending_properties"].as_u64() == Some(0),
            "Refresh session/revision and apply pending properties before fitting"
        );
        ensure!(
            state["pose_mode"].as_i64().unwrap_or(0) == 0,
            "Set a fixed T-pose with capture_configure before fitting or sensitivity measurement"
        );
        let keys: Vec<String> = if args["keys"].is_array() {
            serde_json::from_value(args["keys"].clone())?
        } else {
            (0..44).map(|i| format!("shape.body.{i}")).collect()
        };
        ensure!(
            !keys.is_empty() && keys.iter().collect::<HashSet<_>>().len() == keys.len(),
            "Duplicate or empty fitting parameter keys"
        );
        let source = state["card"].clone();
        let queried = self.editor.rpc(
            "get",
            &json!({"session":state["session"],"keys":keys}),
            cancel,
        )?;
        if queried["session"].is_string() {
            ensure!(
                queried["session"] == state["session"],
                "Character changed while loading fitting parameters"
            );
            state = queried.clone();
        }
        let parameters = queried["parameters"]
            .as_array()
            .context("Missing parameters")?;
        ensure!(parameters.len() == keys.len(), "Parameter count mismatch");
        let mut initial = Vec::new();
        let mut low = Vec::new();
        let mut high = Vec::new();
        for (p, key) in parameters.iter().zip(&keys) {
            ensure!(
                p["key"] == *key
                    && p["supported"] == true
                    && p["requires_apply"] != true
                    && p["value"].is_number()
                    && p["min"].is_number()
                    && p["max"].is_number(),
                "Fitting requires supported immediate numeric parameters with bounds: {key}"
            );
            let l = p["min"].as_f64().unwrap();
            let h = p["max"].as_f64().unwrap();
            ensure!(h > l, "Invalid parameter range: {key}");
            initial.push(p["value"].as_f64().unwrap());
            low.push(l);
            high.push(h);
        }
        let run_id = args["_run_id"]
            .as_str()
            .map(str::to_owned)
            .unwrap_or_else(id);
        valid_id(&run_id)?;
        let mut run = json!({"run":run_id,"reference":args["reference"],"keys":keys,"source_card":source,"initial":initial,"status":"running","evaluations":[],"algorithm":if sensitivity{"central_finite_difference_sensitivity"}else{"bounded_adaptive_coordinate_search"}});
        let mut best = initial.clone();
        let mut step: Vec<f64> = low
            .iter()
            .zip(&high)
            .map(|(l, h)| (h - l) * number(args, "step_fraction", 0.025))
            .collect();
        if let Some(resume) = args["resume"].as_str() {
            valid_id(resume)?;
            let prior = read_json(&self.root().join(format!("run-{resume}.json")), 64_000_000)?;
            ensure!(
                prior["reference"] == args["reference"]
                    && prior["keys"] == json!(keys)
                    && prior["source_card"] == source,
                "Resume requires matching reference, keys and character card"
            );
            best = serde_json::from_value(prior["best_values"].clone())?;
            step = serde_json::from_value(prior["steps"].clone())?;
            ensure!(
                best.len() == keys.len() && step.len() == keys.len(),
                "Invalid resume parameter count"
            );
            for i in 0..best.len() {
                ensure!(
                    best[i].is_finite() && step[i].is_finite() && step[i] >= 0.,
                    "Invalid resume values"
                );
                best[i] = best[i].clamp(low[i], high[i]);
            }
            run["resumed_from"] = resume.into();
        }
        let path = self.root().join(format!("run-{run_id}.json"));
        let mut search = Search {
            fitting: self,
            cancel,
            keys,
            expected: state,
            frame,
            targets,
            low,
            high,
            best,
            step,
            best_score: f64::INFINITY,
            best_masks: vec![],
            run,
            path,
            started: Instant::now(),
            seconds: number(args, "max_seconds", 1800.),
            budget: args["max_evaluations"].as_u64().unwrap_or(160) as usize,
            target: number(args, "target_loss", 0.002),
        };
        let outcome = search.execute(sensitivity);
        if let Err(error) = outcome {
            search.run["status"] = "interrupted".into();
            search.run["error"] = error.to_string().into();
            let recovery = (|| -> Result<()> {
                let token = CancellationToken::new();
                let state = self.editor.rpc("state", &json!({}), &token)?;
                if same_version(&state, &search.expected) {
                    let result = self.editor.rpc(
                        "set",
                        &edits(&state, &search.keys, &search.best),
                        &token,
                    )?;
                    search.run["best_restored"] = true.into();
                    search.run["revision"] = result["revision"].clone();
                } else {
                    search.run["best_restored"] = false.into();
                }
                Ok(())
            })();
            if let Err(e) = recovery {
                search.run["recovery_error"] = e.to_string().into();
            }
            search.checkpoint()?;
            bail!("{error}; checkpoint: {}", search.path.display());
        }
        let mut result = search.run.clone();
        result.as_object_mut().unwrap().remove("evaluations");
        result["report"] = search.path.display().to_string().into();
        Ok((result, search.best_masks))
    }
}
fn load_masks(folder: &std::path::Path) -> Result<Vec<Mask>> {
    VIEWS
        .iter()
        .map(|view| read_mask(&folder.join(format!("{view}.png")), "mask", None, 24.))
        .collect()
}
fn same_version(a: &Value, b: &Value) -> bool {
    a["session"] == b["session"] && a["revision"] == b["revision"]
}
fn edits(state: &Value, keys: &[String], values: &[f64]) -> Value {
    json!({"session":state["session"],"revision":state["revision"],"edits":keys.iter().zip(values).map(|(key,value)|json!({"key":key,"value":value})).collect::<Vec<_>>()})
}
struct Search<'a> {
    fitting: &'a Fitting,
    cancel: &'a CancellationToken,
    keys: Vec<String>,
    expected: Value,
    frame: Frame,
    targets: Vec<Mask>,
    low: Vec<f64>,
    high: Vec<f64>,
    best: Vec<f64>,
    step: Vec<f64>,
    best_score: f64,
    best_masks: Vec<Mask>,
    run: Value,
    path: PathBuf,
    started: Instant,
    seconds: f64,
    budget: usize,
    target: f64,
}
impl Search<'_> {
    fn count(&self) -> usize {
        self.run["evaluations"].as_array().unwrap().len()
    }
    fn exhausted(&self) -> bool {
        self.count() >= self.budget || self.started.elapsed().as_secs_f64() >= self.seconds
    }
    fn checkpoint(&mut self) -> Result<()> {
        self.run["best_loss"] = if self.best_score.is_finite() {
            json!(self.best_score)
        } else {
            Value::Null
        };
        self.run["best_values"] = json!(self.best);
        self.run["steps"] = json!(self.step);
        atomic_json(&self.path, &self.run)
    }
    fn evaluate(&mut self, values: &[f64]) -> Result<(f64, Option<Vec<Mask>>)> {
        ensure!(
            !self.cancel.is_cancelled(),
            "Fit cancelled; inspect saved best candidate and state before resuming"
        );
        let state = self.fitting.editor.rpc("state", &json!({}), self.cancel)?;
        ensure!(
            same_version(&state, &self.expected),
            "Character modified externally during fit; stopping without overwriting it"
        );
        self.expected =
            self.fitting
                .editor
                .rpc("set", &edits(&state, &self.keys, values), self.cancel)?;
        let (g, measured) = self
            .fitting
            .geometry(&self.expected["session"], self.cancel)?;
        ensure!(
            same_version(&measured, &self.expected),
            "Character modified during geometry measurement"
        );
        let masks = match rasterize(&g, &self.frame) {
            Ok(m) => m,
            Err(e)
                if self.count() > 0 && e.to_string().contains("exceeds fixed projection frame") =>
            {
                self.run["evaluations"]
                    .as_array_mut()
                    .unwrap()
                    .push(json!({"values":values,"loss":1000,"valid":false,"error":e.to_string()}));
                return Ok((1000., None));
            }
            Err(e) => return Err(e),
        };
        let mut score = metrics(&masks, &self.targets)?;
        let loss = score["loss"].as_f64().unwrap();
        score["values"] = json!(values);
        self.run["evaluations"].as_array_mut().unwrap().push(score);
        Ok((loss, Some(masks)))
    }
    fn execute(&mut self, sensitivity: bool) -> Result<()> {
        let initial = self.best.clone();
        let (score, masks) = self.evaluate(&initial)?;
        self.best_score = score;
        self.best_masks = masks.context("Missing initial silhouettes")?;
        self.run["initial_loss"] = score.into();
        self.checkpoint()?;
        if sensitivity {
            self.run["sensitivities"] = json!([]);
            for i in 0..self.keys.len() {
                if self.started.elapsed().as_secs_f64() >= self.seconds {
                    break;
                }
                let mut probes = Vec::new();
                for sign in [-1., 1.] {
                    let mut trial = self.best.clone();
                    trial[i] = (trial[i] + sign * self.step[i]).clamp(self.low[i], self.high[i]);
                    let (score, masks) = self.evaluate(&trial)?;
                    let changed = masks.as_ref().map(|m| {
                        m.iter()
                            .zip(&self.best_masks)
                            .map(|(a, b)| {
                                a.pixels
                                    .iter()
                                    .zip(&b.pixels)
                                    .filter(|(a, b)| a != b)
                                    .count() as f64
                                    / a.pixels.len() as f64
                            })
                            .sum::<f64>()
                            / 3.
                    });
                    probes.push(
                        json!({"value":trial[i],"loss":score,"silhouette_change_fraction":changed}),
                    );
                }
                let interval =
                    probes[1]["value"].as_f64().unwrap() - probes[0]["value"].as_f64().unwrap();
                let valid = probes
                    .iter()
                    .all(|p| p["silhouette_change_fraction"].is_number());
                let derivative = if valid && interval != 0. {
                    Some(
                        (probes[1]["loss"].as_f64().unwrap() - probes[0]["loss"].as_f64().unwrap())
                            / interval,
                    )
                } else {
                    None
                };
                self.run["sensitivities"].as_array_mut().unwrap().push(json!({"key":self.keys[i],"probes":probes,"valid":valid,"loss_derivative":derivative}));
                self.checkpoint()?;
            }
        } else {
            while !self.exhausted() && self.best_score > self.target {
                let mut improved = false;
                for i in 0..self.keys.len() {
                    if self.exhausted() {
                        break;
                    }
                    let base = self.best.clone();
                    for sign in [1., -1.] {
                        if self.exhausted() {
                            break;
                        }
                        let mut trial = base.clone();
                        trial[i] = (base[i] + sign * self.step[i]).clamp(self.low[i], self.high[i]);
                        if trial[i] == base[i] {
                            continue;
                        }
                        let (score, masks) = self.evaluate(&trial)?;
                        if score + 1e-9 < self.best_score {
                            self.best_score = score;
                            self.best = trial;
                            self.best_masks = masks.context("Missing best masks")?;
                            improved = true;
                            self.checkpoint()?;
                        }
                    }
                    if self.best_score <= self.target {
                        break;
                    }
                }
                if !improved {
                    for s in &mut self.step {
                        *s *= 0.5;
                    }
                    if self
                        .step
                        .iter()
                        .enumerate()
                        .map(|(i, s)| s / (self.high[i] - self.low[i]))
                        .fold(0., f64::max)
                        < 1e-4
                    {
                        break;
                    }
                }
                self.checkpoint()?;
            }
        }
        let state = self.fitting.editor.rpc("state", &json!({}), self.cancel)?;
        ensure!(
            same_version(&state, &self.expected),
            "Character modified before final fit commit"
        );
        let final_state =
            self.fitting
                .editor
                .rpc("set", &edits(&state, &self.keys, &self.best), self.cancel)?;
        let reason = if sensitivity
            && self.run["sensitivities"].as_array().unwrap().len() == self.keys.len()
        {
            "sensitivity_complete"
        } else if !sensitivity && self.best_score <= self.target {
            "target_loss"
        } else if self.started.elapsed().as_secs_f64() >= self.seconds {
            "time_budget"
        } else if self.count() >= self.budget {
            "evaluation_budget"
        } else {
            "minimum_step"
        };
        self.run["status"] = "completed".into();
        self.run["stop_reason"] = reason.into();
        self.run["session"] = final_state["session"].clone();
        self.run["revision"] = final_state["revision"].clone();
        self.run["evaluation_count"] = self.count().into();
        let run_id = self.run["run"].as_str().unwrap();
        save_masks(
            &self.fitting.root().join(format!("run-{run_id}")),
            &self.best_masks,
        )?;
        self.checkpoint()
    }
}
