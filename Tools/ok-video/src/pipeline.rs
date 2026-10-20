//! `ok-video import`: video → Saved/VideoMotions/<name>.okanim without Python
//! (replaces Tools/video_motion/import_video.py + run_gvhmr*.py + pair_align.py).
//!
//! Progress lines follow the editor panel's protocol: `[n/m]` stages, `[步驟]` / `[資訊]`,
//! `[錯誤]` causes; ok-extract's `round …` / `pair fit …` lines are passed through.

use crate::gvhmr::{self, Output};
use crate::models::Paths;
use crate::npy::{write_npz, Out};
use crate::{crop, pairalign, smplx, track, video, vit, vo, yolo};
use candle_core::Device;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

pub struct Options {
    pub video: PathBuf,
    pub name: String,
    pub out_dir: PathBuf,
    pub models: Paths,
    pub static_cam: bool,
    pub f_mm: Option<f64>,
    pub pair: bool,
    pub left_male: bool,
    pub game: Option<PathBuf>,
    pub card: Option<PathBuf>,
    pub ok_extract: PathBuf,
    pub data: PathBuf,
    pub arm_reach: f64,
    pub collar_scale: f64,
    pub smooth: f64,
    pub skip_gvhmr: bool,
}

fn say(s: &str) {
    println!("{s}");
    use std::io::Write;
    let _ = std::io::stdout().flush();
}

/// Runs `f` as a named step; a failure becomes `<step>失敗：<reason>`.
fn step<T>(name: &str, f: impl FnOnce() -> Result<T, String>) -> Result<T, String> {
    say(&format!("[步驟] {name}"));
    let t = Instant::now();
    let r = f().map_err(|e| format!("{name}失敗：{e}"));
    if r.is_ok() {
        say(&format!("[資訊] {name}完成（{:.1} 秒）", t.elapsed().as_secs_f64()));
    }
    r
}

/// One person's inputs to GVHMR.
struct Person {
    bbx: Vec<[f64; 3]>,
    kp: Vec<[[f32; 3]; 17]>,
    feat: Vec<f32>,
}

/// ViTPose keypoints + HMR2 features for each person's boxes, in one half-resolution pass.
fn features(dev: &Device, o: &Options, info: &video::Info, boxes: &[Vec<[f64; 3]>]) -> Result<Vec<Person>, String> {
    let pose = vit::Weights::load(&o.models.cached("vitpose.safetensors"), dev, "ViTPose", &["backbone.", "keypoint_head."])?;
    let hmr = vit::Weights::load(&o.models.cached("hmr2.safetensors"), dev, "HMR2", &["backbone.", "smpl_head.transformer."])?;
    let n = boxes[0].len();
    let mut people: Vec<Person> = boxes.iter().map(|b| Person { bbx: b.clone(), kp: Vec::with_capacity(n), feat: Vec::with_capacity(n * 1024) }).collect();
    let mut reader = video::Reader::open(&o.video, info, 0.5)?;
    let mut frame = Vec::new();
    let per = 3 * crop::H * crop::W;
    let batch = 16;
    let mut crops: Vec<Vec<f32>> = vec![Vec::new(); people.len()];
    let mut first = 0;
    let started = Instant::now();
    let mut flush = |crops: &mut Vec<Vec<f32>>, first: usize, people: &mut Vec<Person>| -> Result<(), String> {
        for (p, c) in crops.iter_mut().enumerate() {
            let b = c.len() / per;
            if b == 0 {
                continue;
            }
            let x = vit::crops_tensor(c, b, dev)?;
            let heat = pose.vitpose(&x)?;
            for i in 0..b {
                let bb = people[p].bbx[first + i];
                people[p].kp.push(vit::decode(&heat[i * 17 * 64 * 48..(i + 1) * 17 * 64 * 48], bb));
            }
            let f = hmr.hmr2(&x)?;
            people[p].feat.extend(f);
            c.clear();
        }
        Ok(())
    };
    for t in 0..n {
        if !reader.next(&mut frame)? {
            return Err(format!("半解析度解碼只得到 {t} 幀（全解析度 {n} 幀）"));
        }
        for (p, c) in crops.iter_mut().enumerate() {
            let [cx, cy, s] = people[p].bbx[t];
            let start = c.len();
            c.resize(start + per, 0.);
            crop::crop(&frame, reader.width, reader.height, cx, cy, s, &mut c[start..]);
        }
        if (t + 1) % batch == 0 || t + 1 == n {
            flush(&mut crops, first, &mut people)?;
            first = t + 1;
            say(&format!("[資訊] 2D 關節與影像特徵 {}/{} 幀（{:.0} 幀/秒）", t + 1, n, (t + 1) as f64 / started.elapsed().as_secs_f64()));
        }
    }
    Ok(people)
}

/// SimpleVO replacement: every 8th half-resolution frame (plus the last), relative rotations,
/// slerp in between, cam_angvel. Failed pairs keep the camera still and are reported.
fn camera_rotation(o: &Options, info: &video::Info, n: usize) -> Result<Vec<[f64; 6]>, String> {
    let mut samples: Vec<usize> = (0..n).step_by(8).collect();
    if *samples.last().unwrap() != n - 1 {
        samples.push(n - 1);
    }
    let mut reader = video::Reader::open(&o.video, info, 0.5)?;
    let mut frame = Vec::new();
    let mut grays = Vec::with_capacity(samples.len());
    let mut next = 0;
    for t in 0..n {
        if !reader.next(&mut frame)? {
            break;
        }
        if next < samples.len() && samples[next] == t {
            grays.push(vo::Gray::from_rgb(&frame, reader.width, reader.height));
            next += 1;
        }
    }
    if grays.len() != samples.len() {
        return Err(format!("鏡頭估計只讀到 {} 個取樣幀（應有 {}）", grays.len(), samples.len()));
    }
    let (w, h) = (grays[0].w as f64, grays[0].h as f64);
    // SimpleVO's focal: 24 mm equivalent unless --f_mm (half-resolution pixels).
    let f = (w * w + h * h).sqrt() / (24f64 * 24. + 36. * 36.).sqrt() * o.f_mm.unwrap_or(24.);
    let mut deltas = vec![crate::geom::I3; samples.len()];
    let mut failed = 0;
    let mut total_deg = 0.;
    for k in 1..samples.len() {
        let (p0, p1) = vo::matches(&grays[k - 1], &grays[k]);
        match vo::relative_rotation(&p0, &p1, f, w / 2., h / 2., k as u64) {
            Some((r, _)) => {
                total_deg += crate::geom::norm(crate::geom::matrix_to_aa(&r)).to_degrees();
                deltas[k] = r;
            }
            None => failed += 1,
        }
    }
    say(&format!("[資訊] 鏡頭轉動：{} 組取樣，累計 {:.1}°，{} 組特徵不足視為不動", samples.len() - 1, total_deg, failed));
    Ok(vo::cam_angvel(&vo::interpolate(&samples, &deltas, n)))
}

fn intrinsics(info: &video::Info, f_mm: Option<f64>) -> (f64, f64, f64) {
    let (w, h) = (info.width as f64, info.height as f64);
    let diag = (w * w + h * h).sqrt();
    let f = match f_mm {
        Some(mm) => diag / (24f64 * 24. + 36. * 36.).sqrt() * mm,
        None => diag,
    };
    (f, w / 2., h / 2.)
}

fn write_motion(path: &Path, out: &Output, extra: Option<(&Person, (f64, f64, f64))>, pair_gap: Option<&[f64]>) -> Result<(), String> {
    let l = out.global.transl.len();
    let f = |v: &[[f64; 3]]| v.iter().flat_map(|a| a.iter().map(|x| *x as f32)).collect::<Vec<f32>>();
    let go = f(&out.global.global_orient);
    let tl = f(&out.global.transl);
    let bp: Vec<f32> = out.global.body_pose.iter().flat_map(|p| p.iter().flat_map(|a| a.iter().map(|x| *x as f32))).collect();
    let betas: Vec<f32> = (0..l).flat_map(|_| out.global.betas.iter().map(|x| *x as f32)).collect();
    let fps = [30f64];
    let mut arrays: Vec<(&str, Out)> = vec![
        ("global_orient", Out::F32(&go, vec![l, 3])),
        ("body_pose", Out::F32(&bp, vec![l, 63])),
        ("transl", Out::F32(&tl, vec![l, 3])),
        ("betas", Out::F32(&betas, vec![l, 10])),
        ("fps", Out::F64(&fps, vec![])),
    ];
    let (igo, itl, k, conf, gap);
    if let Some((person, (fo, cx, cy))) = extra {
        igo = f(&out.incam.global_orient);
        itl = f(&out.incam.transl);
        k = vec![fo as f32, 0., cx as f32, 0., fo as f32, cy as f32, 0., 0., 1.];
        conf = person.kp.iter().flat_map(|k| k.iter().map(|j| j[2])).collect::<Vec<f32>>();
        arrays.push(("incam_global_orient", Out::F32(&igo, vec![l, 3])));
        arrays.push(("incam_transl", Out::F32(&itl, vec![l, 3])));
        arrays.push(("K", Out::F32(&k, vec![3, 3])));
        arrays.push(("kp_conf", Out::F32(&conf, vec![l, 17])));
    }
    if let Some(g) = pair_gap {
        gap = g.iter().map(|v| *v as f32).collect::<Vec<f32>>();
        arrays.push(("pair_gap", Out::F32(&gap, vec![l])));
    }
    write_npz(path, &arrays)
}

/// Runs ok-extract (retarget + repair + validation, already Rust) and returns its summary JSON.
fn ok_extract(o: &Options, args: Vec<String>, what: &str) -> Result<serde_json::Value, String> {
    let out = Command::new(&o.ok_extract)
        .args(&args)
        .output()
        .map_err(|e| format!("無法啟動 ok-extract（{e}，系統錯誤 {}）：{}", e.raw_os_error().unwrap_or(0), o.ok_extract.display()))?;
    let stderr = String::from_utf8_lossy(&out.stderr);
    if !out.status.success() {
        let lines: Vec<&str> = stderr.lines().filter(|l| !l.trim().is_empty() && !l.starts_with("ok-extract:")).collect();
        let tail = lines.iter().rev().take(6).rev().cloned().collect::<Vec<_>>().join("；");
        return Err(format!("ok-extract {what}失敗（結束碼 {:?}）：{}", out.status.code(), if tail.is_empty() { "沒有輸出".into() } else { tail }));
    }
    for line in stderr.lines() {
        if line.starts_with("round") || line.starts_with("pair fit") {
            say(line);
        }
    }
    let stdout = String::from_utf8_lossy(&out.stdout);
    let last = stdout.trim().lines().last().unwrap_or("");
    serde_json::from_str(last).map_err(|e| format!("ok-extract 的輸出不是 JSON（{e}）：{last}"))
}

/// True when the PNG carries an `okPv` portable snapshot (validated without the game root).
pub fn portable_card(path: &Path) -> bool {
    let Ok(b) = std::fs::read(path) else { return false };
    if b.len() < 8 || b[..8] != [0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a] {
        return false;
    }
    let mut i = 8;
    while i + 8 <= b.len() {
        let n = u32::from_be_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]]) as usize;
        match &b[i + 4..i + 8] {
            b"okPv" => return true,
            b"IEND" => return false,
            _ => {}
        }
        i += 12 + n;
    }
    false
}

fn common_args(o: &Options) -> Vec<String> {
    vec![
        "--smplx".into(),
        o.models.smplx().display().to_string(),
        "--model".into(),
        o.data.join("video_motion_model.json").display().to_string(),
        "--arm-reach".into(),
        o.arm_reach.to_string(),
        "--collar-scale".into(),
        o.collar_scale.to_string(),
        "--smooth".into(),
        o.smooth.to_string(),
    ]
}

fn write_json(path: &Path, v: &serde_json::Value) -> Result<(), String> {
    std::fs::write(path, serde_json::to_string_pretty(v).unwrap()).map_err(|e| format!("無法寫入 {}：{e}", path.display()))
}

pub fn run(o: &Options) -> Result<(), String> {
    if o.name.is_empty() || o.name.contains(['/', '\\', ':', '*', '?', '"', '<', '>', '|']) || o.name.contains("..") {
        return Err(format!("動作名稱不可空白，也不可包含 \\ / : * ? \" < > | 或 ..：{}", o.name));
    }
    let work = o.out_dir.join("work").join(&o.name);
    std::fs::create_dir_all(&work).map_err(|e| format!("無法建立工作目錄 {}：{e}", work.display()))?;
    let total = if o.pair { 3 } else { 2 };
    let params = serde_json::json!({"arm_reach": o.arm_reach, "collar_scale": o.collar_scale, "smooth": o.smooth, "f_mm": o.f_mm});
    let motion_single = work.join("motion.npz");
    let (female_npz, male_npz, aligned_npz) = (work.join("motion_female.npz"), work.join("motion_male.npz"), work.join("motion_male_aligned.npz"));
    let have = if o.pair { aligned_npz.is_file() && female_npz.is_file() } else { motion_single.is_file() };

    if !(o.skip_gvhmr && have) {
        say(&format!("[1/{total}] {}", if o.pair { "GVHMR：分配兩人並估計各自的 3D 姿勢" } else { "GVHMR：偵測人物與估計 3D 姿勢" }));
        step("準備模型", || o.models.prepare(&say))?;
        let info = step("讀取影片資訊", || video::probe(&o.video))?;
        say(&format!("[資訊] 影片 {}×{}，{:.2} fps，{} 幀", info.width, info.height, info.fps, info.frames));
        if (info.fps - 30.).abs() > 0.5 {
            say("[資訊] 影片不是 30 fps：和原本的流程一樣逐幀當作 30 fps 處理（動作速度會等比例改變）");
        }
        let dev = crate::device();
        let dets = step("偵測人物（YOLOv8x）", || {
            let net = yolo::Yolo::load(&o.models.cached("yolov8x.safetensors"), &dev)?;
            let mut reader = video::Reader::open(&o.video, &info, 1.)?;
            let mut frame = Vec::new();
            let mut all = Vec::new();
            let started = Instant::now();
            while reader.next(&mut frame)? {
                all.push(net.detect(&frame, reader.width, reader.height, if o.pair { 0.3 } else { 0.5 })?);
                if all.len() % 60 == 0 {
                    say(&format!("[資訊] 偵測人物 {}/{} 幀（{:.0} 幀/秒）", all.len(), info.frames, all.len() as f64 / started.elapsed().as_secs_f64()));
                }
            }
            reader.finish()?;
            Ok(all)
        })?;
        let n = dets.len();
        if n < 2 {
            return Err(format!("影片只有 {n} 幀，無法估計動作"));
        }
        // (role name, boxes): single = one main track; pair = male / female.
        let roles: Vec<(&str, Vec<[f64; 3]>)> = if o.pair {
            let p = step("分配兩人（左右）", || track::pair(&dets, info.width as usize))?;
            let (male, female, cm, cf) = if o.left_male { (p.left, p.right, p.cover_left, p.cover_right) } else { (p.right, p.left, p.cover_right, p.cover_left) };
            for (who, c) in [("男", cm), ("女", cf)] {
                say(&format!("[資訊] {who}方在 {:.0}% 的幀被偵測到，其餘內插", c * 100.));
                if c < 0.3 {
                    return Err(format!("{who}方只在 {:.0}% 的幀被偵測到，無法估計動作（兩人重疊太多，或不是兩個人的影片）", c * 100.));
                }
            }
            vec![("male", male.iter().map(track::bbx_xys).collect()), ("female", female.iter().map(track::bbx_xys).collect())]
        } else {
            let main = step("追蹤主要人物", || track::single(&dets, info.width as usize, info.height as usize))?;
            vec![("single", main.iter().map(track::bbx_xys).collect())]
        };
        let boxes: Vec<Vec<[f64; 3]>> = roles.iter().map(|r| r.1.clone()).collect();
        let people = step("2D 關節（ViTPose）與影像特徵（HMR2）", || features(&dev, o, &info, &boxes))?;
        let angvel = if o.static_cam { vec![[1., 0., 0., 0., 1., 0.]; n] } else { step("估計鏡頭轉動", || camera_rotation(o, &info, n))? };
        let (focal, cx, cy) = intrinsics(&info, o.f_mm);
        let outputs: Vec<Output> = step("推論 3D 動作（GVHMR）", || {
            let net = gvhmr::Network::load(&o.models.cached("gvhmr.safetensors"), &dev)?;
            let skel = gvhmr::Skeleton::load(&o.models.smplx())?;
            people
                .iter()
                .map(|p| {
                    gvhmr::predict(&net, &skel, &gvhmr::Inputs {
                        kp2d: p.kp.clone(),
                        bbx_xys: p.bbx.clone(),
                        focal,
                        cx,
                        cy,
                        cam_angvel: angvel.clone(),
                        imgseq: p.feat.clone(),
                        static_cam: o.static_cam,
                    })
                })
                .collect()
        })?;
        if o.pair {
            step("寫入 motion_*.npz", || {
                write_motion(&male_npz, &outputs[0], Some((&people[0], (focal, cx, cy))), None)?;
                write_motion(&female_npz, &outputs[1], Some((&people[1], (focal, cx, cy))), None)
            })?;
            say(&format!("[2/{total}] 對齊兩人到同一個世界座標"));
            let model = step("讀取 SMPL-X", || smplx::Model::load(&o.models.smplx()))?;
            let mut male = outputs[0].clone();
            let (gaps, fa, fo) = step("世界→相機擬合與身體表面距離", || Ok(pairalign::align(&outputs[1], &mut male, &model)))?;
            say(&format!("[資訊] 世界→相機擬合殘差：基準 {:.1}° / {:.1}cm，另一人 {:.1}° / {:.1}cm", fa.resid_deg, fa.resid_m * 100., fo.resid_deg, fo.resid_m * 100.));
            let mut g = gaps.clone();
            let med = pairalign::median(&mut g);
            say(&format!("[資訊] 原始影片中兩人身體表面最近距離：中位數 {:.1} cm，{:.0}% 的幀在 3 cm 內（視為貼身接觸）", med * 100., gaps.iter().filter(|v| **v < 0.03).count() as f64 / gaps.len() as f64 * 100.));
            write_motion(&aligned_npz, &male, Some((&people[0], (focal, cx, cy))), Some(&gaps))?;
        } else {
            step("寫入 motion.npz", || write_motion(&motion_single, &outputs[0], None, None))?;
        }
    }

    let skeleton = o.data.join("rest_skeleton.json");
    std::fs::create_dir_all(&o.out_dir).map_err(|e| e.to_string())?;
    if !o.pair {
        say(&format!("[2/{total}] 轉換到戀活骨架並自動修復"));
        let target = o.out_dir.join(format!("{}.okanim", o.name));
        let mut args = vec!["x".into(), "--video-clip".into(), motion_single.display().to_string(), target.display().to_string(), "--skeleton".into(), skeleton.display().to_string()];
        args.extend(common_args(o));
        if let Some(card) = &o.card {
            args.extend(["--card".into(), card.display().to_string()]);
            if !portable_card(card) {
                let game = o.game.as_ref().ok_or_else(|| format!("驗證卡 {} 是遊戲原版卡（沒有獨立資源快照），需要 --game 遊戲目錄", card.display()))?;
                args.extend(["--game".into(), game.display().to_string()]);
            }
        }
        let mut info = ok_extract(o, args, "轉換")?;
        info["name"] = o.name.clone().into();
        info["video"] = o.video.display().to_string().into();
        info["static_cam"] = o.static_cam.into();
        info["work"] = o.name.clone().into();
        info["params"] = params;
        info["converter"] = "ok-video".into();
        write_json(&o.out_dir.join(format!("{}.json", o.name)), &info)?;
        say(&format!("wrote {} ({} frames, {:.2}s, {} frames still flagged)", target.display(), info["frames"], info["duration_seconds"].as_f64().unwrap_or(0.), info["bad_frames"]));
        return Ok(());
    }
    say(&format!("[3/{total}] 轉換到戀活骨架、貼合兩人身體並自動修復"));
    let game = o.game.clone().ok_or("雙人影片需要 --game 指向含 UserData/chara/male/*.png 的遊戲目錄")?;
    let mut male_cards: Vec<PathBuf> = std::fs::read_dir(game.join("UserData/chara/male"))
        .map_err(|e| format!("找不到男性角色卡目錄 {}：{e}", game.join("UserData/chara/male").display()))?
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")))
        .collect();
    male_cards.sort();
    let mcard = male_cards.first().ok_or("找不到男性角色卡：遊戲目錄的 UserData/chara/male 沒有 PNG")?;
    let (fname, mname) = (format!("{}_女", o.name), format!("{}_男", o.name));
    let card = o.card.clone().ok_or("雙人轉換需要 --card（驗證用的女性卡）")?;
    let mut args: Vec<String> = vec![
        "x".into(),
        "--video-pair".into(),
        female_npz.display().to_string(),
        aligned_npz.display().to_string(),
        o.out_dir.join(format!("{fname}.okanim")).display().to_string(),
        o.out_dir.join(format!("{mname}.okanim")).display().to_string(),
        "--skeleton".into(),
        skeleton.display().to_string(),
        "--skeleton-male".into(),
        o.data.join("rest_skeleton_male.json").display().to_string(),
        "--card".into(),
        card.display().to_string(),
        "--male-card".into(),
        mcard.display().to_string(),
        "--game".into(),
        game.display().to_string(),
    ];
    if !portable_card(&card) {
        args.extend(["--female-game".into(), game.display().to_string()]);
    }
    args.extend(common_args(o));
    let summary = ok_extract(o, args, "雙人轉換")?;
    let fit = summary["pair_fit"].clone();
    say(&format!(
        "[資訊] 身體貼合：軀幹間距 {:.1} cm → {:.1} cm（男方平均移動 {:+.0} cm）",
        fit["gap_before_median_m"].as_f64().unwrap_or(0.) * 100.,
        fit["gap_after_median_m"].as_f64().unwrap_or(0.) * 100.,
        fit["shift_median_m"].as_f64().unwrap_or(0.) * 100.
    ));
    for (n, key) in [(&fname, "female"), (&mname, "male")] {
        let mut info = summary[key].clone();
        info["name"] = n.clone().into();
        info["video"] = o.video.display().to_string().into();
        info["static_cam"] = o.static_cam.into();
        info["work"] = o.name.clone().into();
        info["params"] = params.clone();
        info["pair"] = o.name.clone().into();
        info["converter"] = "ok-video".into();
        write_json(&o.out_dir.join(format!("{n}.json")), &info)?;
    }
    let pair = serde_json::json!({
        "name": o.name, "female": fname, "male": mname, "left": if o.left_male { "male" } else { "female" },
        "work": o.name, "video": o.video.display().to_string(), "frame": summary["female"]["frame"],
        "frames": summary["female"]["frames"], "duration_seconds": summary["female"]["duration_seconds"], "pair_fit": fit,
    });
    let pj = o.out_dir.join(format!("{}.pair.json", o.name));
    write_json(&pj, &pair)?;
    say(&format!("wrote {} (女 {}、男 {} frames still flagged)", pj.display(), summary["female"]["bad_frames"], summary["male"]["bad_frames"]));
    Ok(())
}
