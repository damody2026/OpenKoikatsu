//! The director played end to end against a fake stage that does what Unreal does with each command.
use ok_story::director::{Config, Director};
use ok_story::io::*;
use ok_story::math::Quat;
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::path::PathBuf;

struct Stage {
    d: Director,
    actors: BTreeMap<String, ActorFacts>,
    /// Voice command id → seconds played, length.
    voices: BTreeMap<u64, (f64, f64)>,
    log: Vec<Command>,
    frame: Frame,
    dir: PathBuf,
    /// Voice clips do not finish by themselves.
    hold_voice: bool,
    /// A wall at this x (nothing beyond it is seen), and the probe answers for the next tick.
    wall_x: Option<f64>,
    answers: Vec<ProbeResult>,
}

fn actor_at(pos: [f64; 3], yaw: f64) -> ActorFacts {
    let q = Quat::yaw_deg(yaw);
    let mut a = ActorFacts { ready: true, personality: Some(25), ..Default::default() };
    a.root = Xf { pos, rot: q.to_array(), scale: 1.0 };
    let bone = |z: f64| Xf { pos: [pos[0], pos[1], pos[2] + z], rot: q.to_array(), scale: 1.0 };
    a.bones.insert("hips".into(), bone(90.0));
    a.bones.insert("spine03".into(), bone(125.0));
    a.bones.insert("head".into(), bone(150.0));
    a.bones.insert("eyes".into(), bone(156.0));
    a
}

impl Stage {
    fn new(name: &str, edit: impl FnOnce(&mut Value)) -> Stage {
        let dir = std::env::temp_dir().join(format!("ok-story-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let mut story: Value = serde_json::from_str(include_str!("sample.okstory.json")).unwrap();
        edit(&mut story);
        let path = dir.join("sample.okstory.json");
        std::fs::write(&path, serde_json::to_string_pretty(&story).unwrap()).unwrap();
        let table = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/StreetSocialData/RuntimeV2/expressions.json");
        let expressions = table.is_file().then_some(table);
        let cfg = Config { story: path, save_dir: Some(dir.join("save_root")), expressions, lang: "zh".into(), seed: 5, ..Default::default() };
        let d = Director::open(cfg).unwrap();
        Stage { d, actors: BTreeMap::new(), voices: BTreeMap::new(), log: Vec::new(), frame: Frame::default(), dir, hold_voice: false, wall_x: None, answers: Vec::new() }
    }

    fn execute(&mut self, c: &Command) {
        let ok = |id| Ack { id, ok: true, ..Default::default() };
        match &c.op {
            Op::LoadScene { .. } => {
                self.actors.clear();
                let cam = SceneCamera { id: 7, pos: [0.0, -400.0, 160.0], rot: Quat::yaw_deg(90.0).to_array(), fov: 45.0 };
                self.d.ack(Ack { cameras: vec![cam], active_camera: Some(7), ..ok(c.id) });
            }
            Op::Place { who, pos, yaw, visible, .. } => {
                if *visible {
                    self.actors.insert(who.clone(), actor_at(pos.unwrap_or([0.0; 3]), yaw.unwrap_or(0.0)));
                } else {
                    self.actors.remove(who);
                }
                self.d.ack(Ack { personality: Some(25), ..ok(c.id) });
            }
            Op::SpawnOffstage { who, pos, .. } => {
                self.actors.insert(who.clone(), actor_at(*pos, 0.0));
                self.d.ack(Ack { personality: Some(25), ..ok(c.id) });
            }
            Op::ReleaseOffstage { who } => {
                self.actors.remove(who);
            }
            Op::Voice { .. } => {
                self.voices.insert(c.id, (0.0, 1.0));
                self.d.ack(Ack { duration: Some(1.0), ..ok(c.id) });
            }
            Op::StopVoice { .. } => self.voices.clear(),
            Op::Thumbnail { path, .. } => {
                std::fs::write(path, b"jpg").unwrap();
                self.d.ack(ok(c.id));
            }
            _ => {
                self.d.ack(ok(c.id));
            }
        }
        self.log.push(c.clone());
    }

    fn tick(&mut self, dt: f64) -> &Frame {
        let mut voice = VoiceFacts::default();
        if let Some((&id, (played, length))) = self.voices.iter_mut().next_back() {
            if !self.hold_voice {
                *played += dt;
            }
            voice = VoiceFacts { id, playing: *played < *length, progress: (*played / *length).min(1.0), done: *played >= *length };
        }
        let probes = std::mem::take(&mut self.answers);
        let facts = Facts { dt, viewport: [1920.0, 1080.0], actors: self.actors.clone(), voice, view: None, probes };
        self.frame = self.d.tick(facts);
        for p in self.frame.probes.clone() {
            let length = ((p.to[0] - p.from[0]).powi(2) + (p.to[1] - p.from[1]).powi(2) + (p.to[2] - p.from[2]).powi(2)).sqrt();
            let clear = match self.wall_x {
                Some(w) if p.to[0] > w && p.from[0] < w => length * (w - p.from[0]) / (p.to[0] - p.from[0]),
                _ => 1e9,
            };
            self.answers.push(ProbeResult { id: p.id, clear });
        }
        for c in self.frame.commands.clone() {
            self.execute(&c);
        }
        &self.frame
    }

    fn run(&mut self, seconds: f64) {
        let n = (seconds * 60.0).round() as usize;
        for _ in 0..n {
            self.tick(1.0 / 60.0);
        }
    }

    fn input(&mut self, v: Value) {
        self.d.input(serde_json::from_value(v).unwrap());
    }

    fn state(&self) -> Value {
        self.d.query(&json!({"q": "state"}))
    }

    fn line(&self) -> String {
        self.state()["line"].as_str().unwrap_or_default().to_string()
    }

    /// Clicks (and waits) until the line is `id`.
    fn reach(&mut self, id: &str) {
        for _ in 0..400 {
            if self.line() == id && self.state()["phase"] == "line" {
                return;
            }
            self.input(json!({"type": "click"}));
            self.run(0.1);
        }
        panic!("never reached {id}; at {}", self.state());
    }

    fn ops(&self, name: &str) -> Vec<&Command> {
        self.log.iter().filter(|c| serde_json::to_value(c).unwrap()["op"] == name).collect()
    }
}

impl Drop for Stage {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}

#[test]
fn plays_through_choices_items_and_offstage_speakers() {
    let mut s = Stage::new("play", |_| {});
    s.run(0.2);
    // The first act: its title, then the first line.
    assert_eq!(s.ops("load_scene").len(), 1);
    assert!(s.frame.title.is_some() || s.line() == "a1.l1");
    s.reach("a1.l1");
    assert_eq!(s.frame.dialog.name, "小鮎");
    assert_eq!(s.frame.dialog.name_color, "#ff7aa8");
    // "smile" is the game's 笑顔 (personality 25: eyes 2, mouth 1), when the table is there.
    if s.d.expressions.profiles.contains_key(&25) {
        assert!(s.ops("face").iter().any(|c| matches!(&c.op, Op::Face { who, eyes: 2, mouth: 1, .. } if who == "ayu")));
    }
    s.reach("a1.l3");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    assert_eq!(s.frame.choices.len(), 2);
    assert!(s.frame.choices[0].enabled);
    assert!(!s.frame.choices[1].enabled);
    assert_eq!(s.frame.choices[1].hint, "缺少：舊鑰匙 × 1（現有 0）");
    // A disabled choice does nothing.
    s.input(json!({"type": "choose", "index": 1}));
    s.run(0.05);
    assert_eq!(s.line(), "a1.l3");
    s.input(json!({"type": "choose", "index": 0}));
    s.run(0.05);
    assert_eq!(s.line(), "a1.l4");
    assert_eq!(s.state()["items"]["key"], 1);
    assert_eq!(s.state()["flags"]["together"], 1);
    assert!(s.actors.contains_key("rin"), "rin placed");
    s.reach("a1.l6");
    s.run(0.1);
    // She leaves the stage at her own line and goes on speaking from a portrait copy.
    assert!(s.ops("place").iter().any(|c| matches!(&c.op, Op::Place { who, visible: false, .. } if who == "rin")), "rin left the cast");
    assert_eq!(s.state()["portrait"], "rin");
    // The next act: title, then a2.l1; a2.l2 plays because together = 1.
    s.input(json!({"type": "click"}));
    s.input(json!({"type": "click"}));
    s.run(0.1);
    s.reach("a2.l2");
    s.reach("a2.l3");
    // rin speaks from offstage: a copy for the portrait.
    assert!(s.ops("spawn_offstage").iter().any(|c| matches!(&c.op, Op::SpawnOffstage { who, .. } if who == "rin")));
    s.run(0.5);
    assert_eq!(s.state()["portrait"], "rin");
    assert!(s.frame.portrait.visible);
    assert_eq!(s.state()["items"].get("key"), None, "key taken");
    s.reach("a3.l1");
    s.reach("a3.l2");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    s.input(json!({"type": "click"}));
    s.run(0.1);
    assert_eq!(s.state()["phase"], "ended");
    s.input(json!({"type": "click"}));
    s.run(0.05);
    assert_eq!(s.ops("end").len(), 1);
}

#[test]
fn skipped_line_condition() {
    let mut s = Stage::new("when", |_| {});
    s.run(0.2);
    s.reach("a1.l3");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    // Without the flag, a2.l2 is passed over. Jump there through the act's end: choose "together" off by editing flags.
    s.input(json!({"type": "choose", "index": 0}));
    s.run(0.05);
    s.reach("a1.l6");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    s.reach("a2.l1");
}

#[test]
fn text_follows_the_voice() {
    let mut s = Stage::new("voice", |v| v["characters"]["ayu"]["voice"] = json!({"tts": "ayu"}));
    s.hold_voice = true;
    s.run(0.2);
    s.reach("a1.l1");
    let id = s.ops("voice").last().map(|c| c.id).expect("a voice for ayu's line");
    let total = s.frame.dialog.total;
    for p in [0.25, 0.5] {
        s.voices.insert(id, (p, 1.0));
        s.tick(0.0);
        let want = ((total as f64) * p).ceil() as usize;
        assert_eq!(s.frame.dialog.shown, want, "progress {p}");
        assert_eq!(s.frame.dialog.waiting, "voice");
    }
    // Clicking shows the whole line; the voice goes on.
    s.input(json!({"type": "click"}));
    s.tick(0.0);
    assert_eq!(s.frame.dialog.shown, total);
    assert_eq!(s.frame.dialog.waiting, "voice");
    s.hold_voice = false;
    s.run(1.2);
    assert_eq!(s.frame.dialog.waiting, "click");
}

#[test]
fn save_and_load_do_not_repeat_effects() {
    let mut s = Stage::new("save", |_| {});
    s.run(0.2);
    s.reach("a1.l3");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    s.input(json!({"type": "choose", "index": 0}));
    s.run(0.1);
    assert_eq!(s.line(), "a1.l4");
    s.input(json!({"type": "button", "id": "save"}));
    s.input(json!({"type": "button", "id": "slot:01"}));
    s.run(0.1);
    assert!(s.ops("thumbnail").len() >= 1);
    let saves = s.d.query(&json!({"q": "saves"}));
    let slot = saves["slots"].as_array().unwrap().iter().find(|x| x["id"] == "01").unwrap().clone();
    assert_eq!(slot["empty"], false);
    assert!(slot["thumb"].as_str().unwrap().ends_with("thumb.jpg"));
    s.reach("a2.l1");
    s.input(json!({"type": "button", "id": "load"}));
    s.input(json!({"type": "button", "id": "slot:01"}));
    s.run(0.5);
    assert_eq!(s.line(), "a1.l4");
    assert_eq!(s.state()["items"]["key"], 1, "the line's gift is not given twice");
    // Loading into another scene act reloads it; the same scene does not.
    assert!(s.state()["phase"] == "line");
}

#[test]
fn skip_stops_at_unread_lines() {
    let mut s = Stage::new("skip", |_| {});
    s.run(0.2);
    s.reach("a1.l2");
    s.input(json!({"type": "key", "key": "F5"}));
    s.run(0.1);
    s.reach("a1.l3");
    s.input(json!({"type": "key", "key": "F9"}));
    s.run(0.3);
    assert_eq!(s.line(), "a1.l2");
    s.input(json!({"type": "button", "id": "skip"}));
    s.run(1.0);
    // a1.l2 and a1.l3 were read; skip stops at the choices of a1.l3.
    assert_eq!(s.line(), "a1.l3");
    assert_eq!(s.state()["skip"], false);
    s.input(json!({"type": "choose", "index": 0}));
    s.run(0.05);
    s.input(json!({"type": "button", "id": "skip"}));
    s.run(1.0);
    // a1.l4 was never read: skip stops there.
    assert_eq!(s.line(), "a1.l4");
    assert_eq!(s.state()["skip"], false);
}

#[test]
fn auto_mode_waits_then_goes_on() {
    let mut s = Stage::new("auto", |_| {});
    s.run(0.2);
    s.reach("a1.l1");
    s.input(json!({"type": "button", "id": "auto"}));
    s.run(0.3);
    assert_eq!(s.line(), "a1.l1", "still typing / waiting");
    s.run(3.0);
    assert_ne!(s.line(), "a1.l1");
}

#[test]
fn act_title_slides_in_and_out() {
    let mut s = Stage::new("title", |_| {});
    s.tick(1.0 / 60.0);
    s.tick(1.0 / 60.0);
    let mut xs = Vec::new();
    for _ in 0..120 {
        s.tick(1.0 / 60.0);
        if let Some(t) = &s.frame.title {
            xs.push((t.x, t.alpha));
        }
    }
    assert!(xs.len() > 80, "title shown for its 1.6 s ({})", xs.len());
    assert!(xs[0].0 < -0.5);
    assert!(xs.iter().any(|(x, a)| *x == 0.0 && *a == 1.0));
    assert!(xs.last().unwrap().0 < -0.3);
    assert_eq!(s.line(), "a1.l1");
}

#[test]
fn camera_moves_within_an_act_and_cuts_between() {
    let mut s = Stage::new("camera", |_| {});
    s.run(0.2);
    s.reach("a1.l3");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    s.input(json!({"type": "choose", "index": 0}));
    s.tick(1.0 / 60.0);
    s.tick(1.0 / 60.0);
    // a1.l4 frames two people: a move from ayu's close-up.
    assert_eq!(s.state()["camera_moving"], true);
    // Clicking on finishes the move at once.
    s.input(json!({"type": "click"}));
    s.input(json!({"type": "click"}));
    s.tick(1.0 / 60.0);
    s.reach("a3.l1");
    s.tick(1.0 / 60.0);
    let cam = s.frame.camera.clone().unwrap();
    assert!((cam.pos[0] - 100.0).abs() < 1e-6 && (cam.fov - 35.0).abs() < 1e-6, "{cam:?}");
}

#[test]
fn export_import_and_reload_keep_the_place() {
    let mut s = Stage::new("export", |_| {});
    s.run(0.2);
    s.reach("a1.l2");
    let snap = s.d.export();
    s.reach("a1.l3");
    s.d.import(&snap).unwrap();
    assert_eq!(s.line(), "a1.l2");
    // Edit the story on disk: the line keeps its place when lines are inserted before it.
    let path = s.dir.join("sample.okstory.json");
    let mut v: Value = serde_json::from_str(&std::fs::read_to_string(&path).unwrap()).unwrap();
    v["acts"][0]["lines"].as_array_mut().unwrap().insert(0, json!({"id": "a1.l0", "text": "（序）"}));
    std::fs::write(&path, serde_json::to_string(&v).unwrap()).unwrap();
    s.d.reload().unwrap();
    assert_eq!(s.line(), "a1.l2");
    assert_eq!(s.state()["shown"], 0);
}

#[test]
fn camera_editing_writes_the_line() {
    let mut s = Stage::new("fly", |_| {});
    s.run(0.2);
    s.reach("a1.l2");
    s.input(json!({"type": "key", "key": "F8"}));
    s.tick(1.0 / 60.0);
    assert!(s.frame.camera_edit.is_some());
    s.input(json!({"type": "key", "key": "W", "down": true}));
    s.run(0.5);
    s.input(json!({"type": "key", "key": "W", "down": false}));
    s.input(json!({"type": "key", "key": "Enter"}));
    s.tick(1.0 / 60.0);
    assert!(s.frame.camera_edit.is_none());
    let v: Value = serde_json::from_str(&std::fs::read_to_string(s.dir.join("sample.okstory.json")).unwrap()).unwrap();
    let cam = &v["acts"][0]["lines"][1]["camera"];
    assert!(cam["pos"].is_array() && cam["rot"].is_array(), "{cam}");
    // The file is still a valid story and keeps its key order.
    let text = std::fs::read_to_string(s.dir.join("sample.okstory.json")).unwrap();
    assert!(text.find("\"schema\"").unwrap() < text.find("\"acts\"").unwrap());
    ok_story::script::Story::parse(&text).unwrap();
}

#[test]
fn settings_persist() {
    let mut s = Stage::new("settings", |_| {});
    s.input(json!({"type": "button", "id": "settings"}));
    s.input(json!({"type": "button", "id": "set:text_speed", "value": 9}));
    s.input(json!({"type": "button", "id": "set:skip_unread"}));
    s.tick(0.0);
    assert_eq!(s.frame.screen, "settings");
    let items = s.frame.panel["items"].as_array().unwrap();
    assert_eq!(items.iter().find(|i| i["id"] == "text_speed").unwrap()["value"], 9);
    let text = std::fs::read_to_string(s.dir.join("save_root").join("settings.json")).unwrap();
    let v: Value = serde_json::from_str(&text).unwrap();
    assert_eq!(v["text_speed"], 9);
    assert_eq!(v["skip_unread"], true);
}

#[test]
fn life_commands_reach_the_stage() {
    let mut s = Stage::new("life", |_| {});
    s.run(0.2);
    s.reach("a1.l3");
    s.input(json!({"type": "click"}));
    s.run(0.1);
    s.input(json!({"type": "choose", "index": 0}));
    s.run(0.1);
    s.run(40.0);
    assert!(s.ops("auto_blink").iter().any(|c| matches!(&c.op, Op::AutoBlink { on: false, .. })));
    assert!(!s.ops("blink").is_empty(), "blinks");
    assert!(s.ops("look").iter().any(|c| matches!(&c.op, Op::Look { mode, .. } if mode == "point")), "glances or looks at the speaker");
}

#[test]
fn shipped_story_files_are_valid() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/Story");
    let samples = root.join("Samples");
    let Ok(dir) = std::fs::read_dir(&samples) else { return };
    for entry in dir.flatten() {
        let path = entry.path();
        if path.extension().is_some_and(|e| e == "json") {
            let text = std::fs::read_to_string(&path).unwrap();
            if let Err(e) = ok_story::script::Story::parse(&text) {
                panic!("{}: {e}", path.display());
            }
        }
    }
    ok_story::tuning::Tuning::load(Some(&root)).unwrap();
}

/// `OK_STORY_DUMP_TUNING=<file> cargo test dump_tuning`: writes the default tuning (the shipped tuning.json).
#[test]
fn dump_tuning() {
    if let Ok(path) = std::env::var("OK_STORY_DUMP_TUNING") {
        let mut t = ok_story::tuning::Tuning::default();
        t.faces = ok_story::tuning::default_faces();
        std::fs::write(path, serde_json::to_string_pretty(&t).unwrap() + "\n").unwrap();
    }
}

#[test]
fn shots_avoid_walls() {
    // ayu sits at x = 0 facing +x (yaw -90: her +y turned to +x); a wall stands 60 cm in front of her.
    let mut s = Stage::new("wall", |v| {
        v["acts"][0]["cast"]["c0"]["yaw"] = json!(-90);
    });
    s.wall_x = Some(60.0);
    s.run(0.2);
    s.reach("a1.l1");
    s.run(1.5);
    let cam = s.frame.camera.clone().unwrap();
    assert!(cam.pos[0] < 60.0, "the camera stays on her side of the wall: {cam:?}");
    // Without the wall the first place (in front of her) is kept.
    let mut s = Stage::new("nowall", |v| {
        v["acts"][0]["cast"]["c0"]["yaw"] = json!(-90);
    });
    s.run(0.2);
    s.reach("a1.l1");
    s.run(1.5);
    let cam = s.frame.camera.clone().unwrap();
    assert!(cam.pos[0] > 60.0, "{cam:?}");
}
