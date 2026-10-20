use anyhow::{Result, bail, ensure};
use ok_character_mcp::{
    app::{App, validate},
    fitting::Fitting,
    geometry::*,
    io::*,
};
use serde_json::{Value, json};
use std::{
    path::{Path, PathBuf},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
};
use tokio_util::sync::CancellationToken;

fn box_geometry(w: f64, d: f64, h: f64, offset: f64) -> Geometry {
    let mut vertices = vec![];
    for z in [0., h] {
        for y in [-d / 2., d / 2.] {
            for x in [-w / 2., w / 2.] {
                vertices.push([x + offset, y, z]);
            }
        }
    }
    Geometry {
        meshes: vec![Mesh {
            role: "body".into(),
            vertices,
            triangles: vec![
                [0, 1, 3],
                [0, 3, 2],
                [4, 6, 7],
                [4, 7, 5],
                [0, 4, 5],
                [0, 5, 1],
                [2, 3, 7],
                [2, 7, 6],
                [0, 2, 6],
                [0, 6, 4],
                [1, 5, 7],
                [1, 7, 3],
            ],
        }],
    }
}
#[test]
fn silhouette_alignment_metrics_and_clipping() -> Result<()> {
    let g = box_geometry(40., 20., 159.37, 0.);
    let frame = Frame::new(&g, 255, 383)?;
    let masks = rasterize(&g, &frame)?;
    let (aligned, _) = align_masks(&masks, &frame, None)?;
    assert_eq!(masks, aligned);
    assert_eq!(metrics(&masks, &masks)?["loss"], 0.);
    assert!(
        metrics(
            &masks,
            &rasterize(&box_geometry(45., 22., 155., 0.), &frame)?
        )?["loss"]
            .as_f64()
            .unwrap()
            > 0.
    );
    assert!(rasterize(&box_geometry(400., 20., 160., 0.), &frame).is_err());
    Ok(())
}
#[test]
fn sections_measure_closed_loops_without_combining_limbs() {
    let mut g = box_geometry(40., 20., 160., 0.);
    g.meshes.extend(box_geometry(10., 10., 160., 80.).meshes);
    let result = sections(&g, &[80.]);
    let row = &result["sections"][0];
    assert_eq!(row["closed_loops"].as_array().unwrap().len(), 2);
    assert_eq!(row["valid"], true);
    assert!((row["closed_loops"][0]["perimeter"].as_f64().unwrap() - 120.).abs() < 1e-7);
    g.meshes[0].triangles.truncate(6);
    assert_eq!(sections(&g, &[80.])["sections"][0]["valid"], false);
}
#[test]
fn segmentation_modes_and_hole_preservation() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let path = temp.path().join("mask.png");
    let mut image = image::RgbaImage::from_pixel(64, 64, image::Rgba([255, 255, 255, 255]));
    for y in 15..45 {
        for x in 15..45 {
            image.put_pixel(x, y, image::Rgba([0, 0, 0, 255]));
        }
    }
    image.put_pixel(30, 30, image::Rgba([255, 255, 255, 255]));
    image.save(&path)?;
    let mask = read_mask(&path, "background", Some([255; 3]), 0.)?;
    assert_eq!(mask.pixels[30 * 64 + 30], 1);
    assert!(read_mask(&path, "alpha", None, 0.).is_err());
    image.put_pixel(20, 20, image::Rgba([20, 30, 40, 255]));
    image.save(&path)?;
    assert!(read_mask(&path, "mask", None, 0.).is_err());
    for (_, _, p) in image.enumerate_pixels_mut() {
        if p[0] == 255 {
            p[3] = 0;
        }
    }
    image.save(&path)?;
    assert!(read_mask(&path, "alpha", None, 0.).is_ok());
    Ok(())
}
#[test]
fn atomic_reports_under_concurrent_readers() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let path = temp.path().join("report.json");
    atomic_json(&path, &json!({"index":0,"payload":"x".repeat(4096)}))?;
    let done = Arc::new(AtomicBool::new(false));
    let done2 = done.clone();
    let path2 = path.clone();
    let reader = std::thread::spawn(move || {
        while !done2.load(Ordering::Acquire) {
            let data = read_json(&path2, 10000).unwrap();
            assert_eq!(data["payload"].as_str().unwrap().len(), 4096);
        }
    });
    for i in 0..100 {
        atomic_json(&path, &json!({"index":i,"payload":"x".repeat(4096)}))?;
    }
    done.store(true, Ordering::Release);
    reader.join().unwrap();
    Ok(())
}
#[test]
fn strict_schema_and_output_boundary() -> Result<()> {
    assert!(
        validate(
            &json!(null),
            &json!({"type":["number","string","boolean"]}),
            "value"
        )
        .is_err()
    );
    assert!(
        validate(
            &json!({"bad":1}),
            &json!({"type":["number","string","boolean"]}),
            "value"
        )
        .is_err()
    );
    assert!(
        validate(
            &json!(false),
            &json!({"type":["number","string","boolean"]}),
            "value"
        )
        .is_ok()
    );
    let temp = tempfile::tempdir()?;
    std::fs::create_dir(temp.path().join("images"))?;
    let file = temp.path().join("outside.png");
    std::fs::write(&file, b"png")?;
    assert!(owned_file(temp.path(), "images", &file, 100).is_err());
    assert!(valid_id("../../escape").is_err());
    assert!(validate(&json!(true), &json!({"type":"number"}), "value").is_err());
    assert!(
        validate(
            &json!({"extra":1}),
            &json!({"type":"object","additionalProperties":false}),
            "args"
        )
        .is_err()
    );
    assert!(
        validate(
            &json!(0),
            &json!({"type":"number","exclusiveMinimum":0}),
            "height"
        )
        .is_err()
    );
    Ok(())
}
#[derive(Default)]
struct State {
    values: [f64; 3],
    revision: u64,
    counter: usize,
    interfere: bool,
    cancel_after: Option<usize>,
}
struct Fake {
    root: PathBuf,
    state: Mutex<State>,
}
impl Fake {
    fn new(root: &Path) -> Self {
        std::fs::create_dir_all(root.join("geometry")).unwrap();
        std::fs::create_dir_all(root.join("fitting")).unwrap();
        Self {
            root: root.into(),
            state: Mutex::new(State {
                values: [0.5; 3],
                revision: 1,
                ..Default::default()
            }),
        }
    }
}
fn state(s: &State) -> Value {
    json!({"session":"test","revision":s.revision,"pending_properties":0,"pose_mode":0,"busy":false,"card":"generic.png"})
}
impl Editor for Fake {
    fn root(&self) -> &Path {
        &self.root
    }
    fn rpc(&self, action: &str, args: &Value, cancel: &CancellationToken) -> Result<Value> {
        ensure!(!cancel.is_cancelled(), "cancelled");
        let mut s = self.state.lock().unwrap();
        match action {
            "state" | "progress" => Ok(state(&s)),
            "action" | "camera" => {
                s.revision += 1;
                Ok(state(&s))
            }
            "save" => {
                let path = self.root.join("saved.png");
                std::fs::write(&path, b"saved test card")?;
                let mut result = state(&s);
                result["saved_card"] = path.display().to_string().into();
                Ok(result)
            }
            "get" => Ok(
                json!({"parameters":args["keys"].as_array().unwrap().iter().map(|key|{let i=key.as_str().unwrap().rsplit('.').next().unwrap().parse::<usize>().unwrap();json!({"key":key,"value":s.values[i],"supported":true,"requires_apply":false,"min":0,"max":1})}).collect::<Vec<_>>()}),
            ),
            "set" => {
                ensure!(
                    args["revision"].as_u64() == Some(s.revision),
                    "Stale revision"
                );
                for edit in args["edits"].as_array().unwrap() {
                    let i = edit["key"]
                        .as_str()
                        .unwrap()
                        .rsplit('.')
                        .next()
                        .unwrap()
                        .parse::<usize>()
                        .unwrap();
                    s.values[i] = edit["value"].as_f64().unwrap();
                }
                s.revision += 1;
                Ok(state(&s))
            }
            "geometry" => {
                s.counter += 1;
                let path = self.root.join(format!("geometry/{}.json", s.counter));
                atomic_json(
                    &path,
                    &serde_json::to_value(box_geometry(
                        20. + 40. * s.values[0],
                        10. + 20. * s.values[1],
                        100. + 80. * s.values[2],
                        0.,
                    ))?,
                )?;
                let mut result = state(&s);
                result["geometry"] = path.display().to_string().into();
                if s.cancel_after == Some(s.counter) {
                    cancel.cancel();
                }
                if s.interfere && s.counter == 2 {
                    s.values[0] = 0.99;
                    s.revision += 1;
                }
                Ok(result)
            }
            _ => bail!("Unsupported fake action {action}"),
        }
    }
}
fn prepare(fake: &Fake) -> Result<Value> {
    let g = box_geometry(48., 16., 152., 0.);
    let frame = Frame::new(&box_geometry(40., 20., 160., 0.), 256, 384)?;
    let reference = id();
    let folder = fake.root.join("fitting").join(&reference);
    save_masks(&folder, &rasterize(&g, &frame)?)?;
    atomic_json(&folder.join("reference.json"), &json!({"frame":frame}))?;
    Ok(
        json!({"session":"test","revision":1,"reference":reference,"keys":["shape.body.0","shape.body.1","shape.body.2"],"max_evaluations":160,"max_seconds":30,"step_fraction":0.1,"target_loss":0.002}),
    )
}
#[test]
fn generic_fitter_recovers_target_and_resumes() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let mut args = prepare(&fake)?;
    let fitting = Fitting {
        editor: fake.clone(),
    };
    let (result, _) = fitting.fit(&args, &CancellationToken::new(), false)?;
    assert!(result["best_loss"].as_f64().unwrap() < 0.003, "{result}");
    assert!(result["initial_loss"].as_f64().unwrap() > 0.03);
    args["resume"] = result["run"].clone();
    args["revision"] = fake.state.lock().unwrap().revision.into();
    let (resumed, _) = fitting.fit(&args, &CancellationToken::new(), false)?;
    assert_eq!(resumed["resumed_from"], result["run"]);
    Ok(())
}
#[test]
fn sensitivity_restores_initial_values() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let args = prepare(&fake)?;
    let (result, _) = Fitting {
        editor: fake.clone(),
    }
    .fit(&args, &CancellationToken::new(), true)?;
    assert_eq!(result["sensitivities"].as_array().unwrap().len(), 3);
    assert_eq!(fake.state.lock().unwrap().values, [0.5; 3]);
    Ok(())
}
#[test]
fn cancelled_fit_persists_best_and_recovers() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let mut args = prepare(&fake)?;
    let run = id();
    args["_run_id"] = run.clone().into();
    fake.state.lock().unwrap().cancel_after = Some(3);
    assert!(
        Fitting {
            editor: fake.clone()
        }
        .fit(&args, &CancellationToken::new(), false)
        .is_err()
    );
    let report = read_json(
        &temp.path().join(format!("fitting/run-{run}.json")),
        1000000,
    )?;
    assert_eq!(report["best_restored"], true);
    assert_eq!(
        json!(fake.state.lock().unwrap().values),
        report["best_values"]
    );
    Ok(())
}
#[test]
fn external_edits_are_never_overwritten() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let args = prepare(&fake)?;
    fake.state.lock().unwrap().interfere = true;
    let result = Fitting {
        editor: fake.clone(),
    }
    .fit(&args, &CancellationToken::new(), false);
    assert!(result.unwrap_err().to_string().contains("externally"));
    assert_eq!(fake.state.lock().unwrap().values[0], 0.99);
    Ok(())
}
#[test]
fn mailbox_rpc_cleanup_and_cancel() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let bridge = Bridge::new(temp.path().into(), 2.)?;
    let root = bridge.root.clone();
    let worker = std::thread::spawn(move || {
        loop {
            let paths: Vec<_> = std::fs::read_dir(root.join("requests"))
                .unwrap()
                .flatten()
                .filter(|p| p.path().extension().is_some_and(|s| s == "json"))
                .collect();
            if let Some(path) = paths.first() {
                let request = read_json(&path.path(), 100000).unwrap();
                assert_eq!(request["action"], "state");
                atomic_json(
                    &root.join("responses").join(path.file_name()),
                    &json!({"ok":true,"result":{"session":"test"}}),
                )
                .unwrap();
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(5));
        }
    });
    assert_eq!(
        bridge.rpc("state", &json!({}), &CancellationToken::new())?["session"],
        "test"
    );
    worker.join().unwrap();
    assert_eq!(std::fs::read_dir(bridge.root.join("requests"))?.count(), 0);
    let token = CancellationToken::new();
    token.cancel();
    assert!(bridge.rpc("set", &json!({}), &token).is_err());
    Ok(())
}
#[tokio::test]
async fn background_job_status_and_restart() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let args = prepare(&fake)?;
    let app = App::new(fake.clone())?;
    let result = app
        .invoke("body_fit_start", args, CancellationToken::new())
        .await?;
    let data: Value = serde_json::from_str(result["content"][0]["text"].as_str().unwrap())?;
    let request = json!({"job":data["job"]});
    loop {
        let status = app
            .invoke("body_fit_status", request.clone(), CancellationToken::new())
            .await?;
        let data: Value = serde_json::from_str(status["content"][0]["text"].as_str().unwrap())?;
        if data["status"] == "completed" {
            assert!(data["result"]["best_loss"].as_f64().unwrap() < 0.003);
            break;
        }
        ensure!(data["status"] != "failed", "{data}");
        tokio::time::sleep(std::time::Duration::from_millis(10)).await;
    }
    app.shutdown().await;
    let restarted = App::new(fake)?;
    let status = restarted
        .invoke("body_fit_status", request, CancellationToken::new())
        .await?;
    assert!(
        status["content"][0]["text"]
            .as_str()
            .unwrap()
            .contains("completed")
    );
    Ok(())
}

#[test]
fn reference_prepare_known_projection_roundtrip() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let fitting = Fitting { editor: fake };
    let g = box_geometry(40., 20., 140., 0.);
    let frame = Frame::new(&g, 256, 384)?;
    let folder = temp.path().join("targets");
    save_masks(&folder, &rasterize(&g, &frame)?)?;
    let mut args = json!({"session":"test","projection_frame":frame});
    for view in VIEWS {
        args[view] = folder
            .join(format!("{view}.png"))
            .display()
            .to_string()
            .into();
    }
    let result = fitting.call("reference", &args, &CancellationToken::new())?;
    let reference: Value = serde_json::from_str(result["content"][0]["text"].as_str().unwrap())?;
    let result = fitting.call(
        "compare",
        &json!({"session":"test","reference":reference["reference"]}),
        &CancellationToken::new(),
    )?;
    let compared: Value = serde_json::from_str(result["content"][0]["text"].as_str().unwrap())?;
    assert_eq!(compared["loss"], 0.);
    assert_eq!(result["content"].as_array().unwrap().len(), 4);
    Ok(())
}
#[tokio::test]
async fn background_cancel_remains_queryable() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let args = prepare(&fake)?;
    let app = App::new(fake)?;
    let result = app
        .invoke("body_fit_start", args, CancellationToken::new())
        .await?;
    let data: Value = serde_json::from_str(result["content"][0]["text"].as_str().unwrap())?;
    let request = json!({"job":data["job"]});
    app.invoke("body_fit_cancel", request.clone(), CancellationToken::new())
        .await?;
    app.invoke("editor_progress", json!({}), CancellationToken::new())
        .await?;
    app.shutdown().await;
    let result = app
        .invoke("body_fit_status", request, CancellationToken::new())
        .await?;
    let data: Value = serde_json::from_str(result["content"][0]["text"].as_str().unwrap())?;
    assert_eq!(data["status"], "cancelled");
    Ok(())
}
#[test]
fn native_cli_fits_and_saves_without_python() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let args = prepare(&fake)?;
    let done = Arc::new(AtomicBool::new(false));
    let done2 = done.clone();
    let worker = fake.clone();
    let responder = std::thread::spawn(move || -> Result<()> {
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(20);
        while !done2.load(Ordering::Acquire) && std::time::Instant::now() < deadline {
            if let Ok(files) = std::fs::read_dir(worker.root.join("requests")) {
                for path in files.flatten() {
                    if path.path().extension().is_none_or(|p| p != "json") {
                        continue;
                    }
                    let request = read_json(&path.path(), 100000)?;
                    std::fs::remove_file(path.path())?;
                    let response = match worker.rpc(
                        request["action"].as_str().unwrap(),
                        &request,
                        &CancellationToken::new(),
                    ) {
                        Ok(v) => json!({"ok":true,"result":v}),
                        Err(e) => json!({"ok":false,"error":e.to_string()}),
                    };
                    atomic_json(
                        &worker.root.join("responses").join(path.file_name()),
                        &response,
                    )?;
                }
            }
            std::thread::sleep(std::time::Duration::from_millis(2));
        }
        Ok(())
    });
    let output = std::process::Command::new(env!("CARGO_BIN_EXE_ok-character-mcp"))
        .args([
            "fit",
            "--mailbox",
            temp.path().to_str().unwrap(),
            "--timeout",
            "2",
            "--reference",
            args["reference"].as_str().unwrap(),
            "--keys",
            "shape.body.0",
            "shape.body.1",
            "shape.body.2",
            "--step-fraction",
            "0.1",
            "--max-evaluations",
            "160",
        ])
        .output()?;
    done.store(true, Ordering::Release);
    responder.join().unwrap()?;
    ensure!(
        output.status.success(),
        "CLI failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let result: Value = serde_json::from_slice(&output.stdout)?;
    assert!(result["best_loss"].as_f64().unwrap() < 0.003);
    assert!(Path::new(result["saved_card"].as_str().unwrap()).is_file());
    Ok(())
}

#[test]
fn clipped_candidates_do_not_abort_search() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let fake = Arc::new(Fake::new(temp.path()));
    let mut args = prepare(&fake)?;
    args["step_fraction"] = 1.0.into();
    args["max_evaluations"] = 20.into();
    let (result, _) = Fitting {
        editor: fake.clone(),
    }
    .fit(&args, &CancellationToken::new(), false)?;
    let report = read_json(Path::new(result["report"].as_str().unwrap()), 1000000)?;
    assert!(
        report["evaluations"]
            .as_array()
            .unwrap()
            .iter()
            .any(|e| e["valid"] == false)
    );
    assert_eq!(result["status"], "completed");
    assert_eq!(
        json!(fake.state.lock().unwrap().values),
        result["best_values"]
    );
    Ok(())
}
