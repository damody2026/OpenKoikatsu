//! The director: plays a story. Each tick takes Unreal's facts and the queued input, advances every clock, and
//! returns the frame to present with the commands Unreal is to execute. Nothing here waits: what takes Unreal time
//! (a scene, a character, a voice) is a command whose result comes back through `ack`.
use crate::camera::{self, Rig, ShotContext};
use crate::io::*;
use crate::life::{self, Gaze, Life, Present};
use crate::math::{self, V3};
use crate::portrait::Portrait;
use crate::progress::{BacklogEntry, Progress};
use crate::rng::Rng;
use crate::script::{self, At, Line, Motion, Story};
use crate::text::{self, Typing};
use crate::tuning::{Expressions, Tuning, Words};
use ok_rules::icu::Arg;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub story: PathBuf,
    /// Content/IndependentResources/Story: tuning.json, faces.json, Localization/.
    pub data_dir: Option<PathBuf>,
    /// UserData/Story: settings.json and save/.
    pub save_dir: Option<PathBuf>,
    /// The game's expression table (StreetSocialData/RuntimeV2/expressions.json).
    pub expressions: Option<PathBuf>,
    /// Relative card, scene and voice paths not found next to the story are tried here.
    pub project_dir: Option<PathBuf>,
    pub lang: String,
    pub seed: u64,
    /// Minutes east of UTC, for the time shown on saves.
    pub utc_offset_minutes: i64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub text_speed: u32,
    pub auto_speed: u32,
    pub skip_unread: bool,
    pub voice_volume: f64,
    pub stop_voice: bool,
    pub parallax: f64,
    pub ui_opacity: f64,
}

impl Default for Settings {
    fn default() -> Self {
        Settings { text_speed: 5, auto_speed: 5, skip_unread: false, voice_volume: 1.0, stop_voice: true, parallax: 0.25, ui_opacity: 0.86 }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub enum Phase {
    #[default]
    Start,
    /// Waiting for the scene and the cast; then the act title (or not) and the line.
    Loading { since: f64, title: bool, effects: bool },
    Title { elapsed: f64, effects: bool },
    Line,
    Ended,
}

/// What Unreal was told to show for one story character.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Shown {
    pub visible: bool,
    pub offstage: bool,
    pub pos: Option<V3>,
    pub yaw: Option<f64>,
    pub scale: Option<f64>,
    pub motion: Option<Motion>,
    pub face: Option<String>,
    pub look: Option<(String, Option<V3>)>,
    pub blink_taken: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum Pending {
    Scene,
    Place(String),
    Offstage(String),
    Voice,
    Thumbnail,
    Other,
}

/// A shot's places waiting for the probe answers.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ProbeJob {
    pub ids: Vec<u64>,
    pub candidates: Vec<camera::Candidate>,
    pub cut: bool,
    pub chosen: Option<usize>,
    pub asked: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct VoiceState {
    pub id: u64,
    pub who: String,
    pub started: bool,
    pub finished: bool,
}

/// Everything that changes while a story plays (what export, import and the hot reload carry).
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct State {
    pub progress: Progress,
    pub phase: Phase,
    pub typing: Typing,
    pub line_age: f64,
    pub done_at: Option<f64>,
    pub was_read: bool,
    /// Visible choice → its index in the line.
    pub choice_map: Vec<usize>,
    pub auto: bool,
    pub skip: bool,
    pub skip_hold: bool,
    pub hide_ui: bool,
    pub screen: String,
    pub page: usize,
    pub rig: Rig,
    pub camera_set: bool,
    /// The next camera change cuts (a new act, a loaded save).
    pub cut: bool,
    pub portrait: Portrait,
    pub life: Life,
    pub rng: Option<Rng>,
    pub time: f64,
    pub next_id: u64,
    pub pending: BTreeMap<u64, Pending>,
    pub shown: BTreeMap<String, Shown>,
    pub scene: String,
    pub cameras: Vec<SceneCamera>,
    pub active_camera: Option<i32>,
    pub voice: Option<VoiceState>,
    pub toast: Option<(String, f64)>,
    pub personalities: BTreeMap<String, i32>,
    pub settings: Settings,
    pub end_sent: bool,
    pub probe: Option<ProbeJob>,
    pub auto_save: bool,
}

pub struct Director {
    pub cfg: Config,
    pub story: Story,
    pub story_hash: String,
    pub tuning: Tuning,
    pub expressions: Expressions,
    pub words: Words,
    pub st: State,
    inbox: Vec<Input>,
    out: Vec<Command>,
    probes_out: Vec<Probe>,
    log: Vec<String>,
    facts: Facts,
}

pub fn hash(bytes: &[u8]) -> String {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for b in bytes {
        h ^= *b as u64;
        h = h.wrapping_mul(0x0100_0000_01b3);
    }
    format!("{h:016x}")
}

/// cubic-bezier(x1, y1, x2, y2) at x = `p`.
pub fn bezier(c: [f64; 4], p: f64) -> f64 {
    let p = p.clamp(0.0, 1.0);
    let at = |a: f64, b: f64, t: f64| 3.0 * (1.0 - t) * (1.0 - t) * t * a + 3.0 * (1.0 - t) * t * t * b + t * t * t;
    let (mut lo, mut hi) = (0.0, 1.0);
    for _ in 0..60 {
        let mid = (lo + hi) * 0.5;
        if at(c[0], c[2], mid) < p {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    at(c[1], c[3], (lo + hi) * 0.5)
}

const SLOT_QUICK: &str = "quick";
const SLOT_AUTO: &str = "auto";

impl Director {
    pub fn open(cfg: Config) -> Result<Director, String> {
        let bytes = std::fs::read(&cfg.story).map_err(|e| format!("{}: {e}", cfg.story.display()))?;
        let story = Story::parse(&String::from_utf8_lossy(&bytes)).map_err(|e| format!("{}:\n{e}", cfg.story.display()))?;
        let tuning = Tuning::load(cfg.data_dir.as_deref())?;
        let expressions = Expressions::load(cfg.expressions.as_deref())?;
        let lang = if cfg.lang.is_empty() { "zh".to_string() } else { cfg.lang.clone() };
        let words = Words::load(cfg.data_dir.as_deref(), &lang)?;
        let mut st = State { screen: "play".into(), next_id: 1, ..Default::default() };
        st.rng = Some(Rng::new(cfg.seed));
        st.settings = Self::read_settings(&cfg);
        let mut d = Director { cfg, story, story_hash: hash(&bytes), tuning, expressions, words, st, inbox: Vec::new(), out: Vec::new(), probes_out: Vec::new(), log: Vec::new(), facts: Facts::default() };
        if let Some(read) = d.read_file().and_then(|f| std::fs::read_to_string(f).ok()).and_then(|t| serde_json::from_str::<std::collections::BTreeSet<String>>(&t).ok()) {
            d.st.progress.read = read;
        }
        Ok(d)
    }

    /// Story, tuning, table and words again from disk, the position kept where the story still has it.
    pub fn reload(&mut self) -> Result<(), String> {
        let bytes = std::fs::read(&self.cfg.story).map_err(|e| format!("{}: {e}", self.cfg.story.display()))?;
        let story = Story::parse(&String::from_utf8_lossy(&bytes))?;
        let tuning = Tuning::load(self.cfg.data_dir.as_deref())?;
        let expressions = Expressions::load(self.cfg.expressions.as_deref())?;
        let words = Words::load(self.cfg.data_dir.as_deref(), &self.words.lang)?;
        let line_id = self.story.line(self.st.progress.at()).map(|l| l.id.clone());
        self.story = story;
        self.story_hash = hash(&bytes);
        self.tuning = tuning;
        self.expressions = expressions;
        self.words = words;
        if let Some(at) = line_id.and_then(|id| self.story.target(&id)) {
            self.st.progress.act = at.act;
            self.st.progress.line = at.line;
        } else if self.story.line(self.st.progress.at()).is_none() {
            let at = self.story.start();
            self.st.progress.act = at.act;
            self.st.progress.line = at.line;
        }
        if self.st.phase == Phase::Line {
            // The line as it now reads, without its effects again.
            self.enter_line(false);
        }
        Ok(())
    }

    fn read_settings(cfg: &Config) -> Settings {
        cfg.save_dir
            .as_ref()
            .and_then(|d| std::fs::read_to_string(d.join("settings.json")).ok())
            .and_then(|t| serde_json::from_str(&t).ok())
            .unwrap_or_default()
    }

    fn write_settings(&mut self) {
        if let Some(d) = &self.cfg.save_dir {
            let _ = std::fs::create_dir_all(d);
            if let Err(e) = std::fs::write(d.join("settings.json"), serde_json::to_string_pretty(&self.st.settings).unwrap_or_default()) {
                self.log.push(format!("settings: {e}"));
            }
        }
    }

    fn cmd(&mut self, op: Op, pending: Option<Pending>) -> u64 {
        let id = self.st.next_id.max(1);
        self.st.next_id = id + 1;
        if let Some(p) = pending {
            self.st.pending.insert(id, p);
        }
        self.out.push(Command { id, op });
        id
    }

    pub fn input(&mut self, i: Input) {
        self.inbox.push(i);
    }

    // ---- paths ------------------------------------------------------------------------------------------------------

    /// A path as written in the story: next to the story file, else under the project, else as written.
    fn path(&self, p: &str) -> String {
        if p.is_empty() {
            return String::new();
        }
        let raw = Path::new(p);
        if raw.is_absolute() {
            return p.to_string();
        }
        if let Some(dir) = self.cfg.story.parent() {
            let c = dir.join(raw);
            if c.exists() {
                return c.to_string_lossy().replace('\\', "/");
            }
        }
        if let Some(dir) = &self.cfg.project_dir {
            let c = dir.join(raw);
            if c.exists() {
                return c.to_string_lossy().replace('\\', "/");
            }
        }
        p.to_string()
    }

    fn story_stem(&self) -> String {
        self.cfg.story.file_stem().map(|s| s.to_string_lossy().trim_end_matches(".okstory").to_string()).unwrap_or_else(|| "story".into())
    }

    fn slot_dir(&self, slot: &str) -> Option<PathBuf> {
        self.cfg.save_dir.as_ref().map(|d| d.join("save").join(self.story_stem()).join(slot))
    }

    /// Lines read in any playthrough (shared by every save of the story).
    fn read_file(&self) -> Option<PathBuf> {
        self.cfg.save_dir.as_ref().map(|d| d.join("save").join(self.story_stem()).join("read.json"))
    }

    fn write_read(&mut self) {
        let Some(f) = self.read_file() else { return };
        let _ = std::fs::create_dir_all(f.parent().unwrap());
        if let Err(e) = std::fs::write(&f, serde_json::to_string(&self.st.progress.read).unwrap_or_default()) {
            self.log.push(format!("read lines: {e}"));
        }
    }

    // ---- flow -------------------------------------------------------------------------------------------------------

    fn line(&self) -> Option<&Line> {
        self.story.line(self.st.progress.at())
    }

    fn skipping(&self) -> bool {
        self.st.skip || self.st.skip_hold
    }

    fn toast(&mut self, text: String) {
        self.st.toast = Some((text, self.st.time + 2.5));
    }

    /// Starts playing at `at`: loads its scene when it is another, places the cast, then the title and the line.
    fn begin(&mut self, at: At, title: bool, effects: bool) {
        self.st.progress.act = at.act;
        self.st.progress.line = at.line;
        self.st.cut = true;
        let Some(act) = self.story.acts.get(at.act) else { return self.end() };
        let scene = self.path(&act.scene);
        let title = title && at.line == 0 && act.title.as_ref().is_some_and(|t| !t.is_empty());
        // Speakers offstage in the last act are not needed any more.
        let offstage: Vec<String> = self.st.shown.iter().filter(|(_, s)| s.offstage).map(|(w, _)| w.clone()).collect();
        for who in offstage {
            self.cmd(Op::ReleaseOffstage { who: who.clone() }, None);
            self.st.shown.remove(&who);
        }
        if scene != self.st.scene {
            self.st.scene = scene.clone();
            self.st.shown.clear();
            self.st.cameras.clear();
            self.st.active_camera = None;
            self.cmd(Op::LoadScene { path: scene }, Some(Pending::Scene));
        }
        self.st.portrait.want("");
        self.st.phase = Phase::Loading { since: self.st.time, title, effects };
        self.sync_cast(None);
    }

    fn loading_done(&mut self) {
        let Phase::Loading { since, title, effects } = self.st.phase.clone() else { return };
        let waiting = self.st.pending.values().any(|p| matches!(p, Pending::Scene | Pending::Place(_)));
        // A character that never arrives does not hold the story forever.
        if waiting && self.st.time - since < 30.0 {
            return;
        }
        if waiting {
            self.log.push("loading: gave up waiting for the scene or the cast after 30 s".into());
            self.st.pending.retain(|_, p| !matches!(p, Pending::Scene | Pending::Place(_)));
        }
        // The act's camera (or the scene's) before the first line moves it.
        let act = &self.story.acts[self.st.progress.act];
        let mut candidates = act.camera.as_ref().map(|c| self.camera_candidates(c, "")).unwrap_or_default();
        if candidates.is_empty() {
            candidates = self.camera_candidates(&script::CameraSpec { shot: Some("studio".into()), ..Default::default() }, "");
        }
        if let Some(first) = candidates.first().copied() {
            self.st.rig.go(&self.tuning.camera, first.pose, true, 1.0);
            self.st.camera_set = true;
            self.probe(candidates, true);
        }
        // The act's auto save, once its first line is on screen (its thumbnail shows the scene, not the loading).
        self.st.auto_save = effects && self.st.progress.line == 0;
        if title {
            self.st.phase = Phase::Title { elapsed: 0.0, effects };
        } else {
            self.enter_line(effects);
        }
    }

    fn end(&mut self) {
        self.st.phase = Phase::Ended;
        self.st.progress.ended = true;
        self.write_read();
        self.st.portrait.want("");
        self.stop_voice();
    }

    fn end_of_act(&mut self) {
        let act = &self.story.acts[self.st.progress.act];
        let next = match act.next.as_deref() {
            Some("") => None,
            Some(id) => self.story.target(id),
            None => (self.st.progress.act + 1 < self.story.acts.len()).then_some(At { act: self.st.progress.act + 1, line: 0 }),
        };
        match next {
            Some(at) => self.go(at),
            None => self.end(),
        }
    }

    /// Continue at `at` in the story's flow (its effects apply; a new act shows its title).
    fn go(&mut self, at: At) {
        if at.act != self.st.progress.act {
            self.begin(at, true, true);
        } else {
            self.st.progress.line = at.line;
            self.enter_line(true);
        }
    }

    fn enter_line(&mut self, effects: bool) {
        // Lines whose condition fails are passed over.
        for _ in 0..10_000 {
            let Some(line) = self.line() else { return self.end_of_act() };
            if self.st.progress.holds(&line.when) {
                break;
            }
            self.st.progress.line += 1;
        }
        let Some(line) = self.line().cloned() else { return self.end_of_act() };
        if effects {
            self.st.progress.give(&line.give);
            self.st.progress.take(&line.take);
            self.st.progress.set(&line.set);
        }
        self.st.was_read = !self.st.progress.read.insert(line.id.clone());
        self.st.phase = Phase::Line;
        self.st.line_age = 0.0;
        self.st.done_at = None;
        self.st.choice_map.clear();
        self.stop_voice();
        self.sync_cast(Some(&line));
        self.line_camera(&line);
        self.preload_offstage();
        let words = line.text.get(&self.words.lang);
        self.st.typing = Typing::new(&words);
        if self.skipping() && (self.st.was_read || self.st.settings.skip_unread) {
            self.st.typing.reveal();
        }
        let ch = self.story.characters.get(&line.who).cloned();
        if let Some(c) = &ch {
            let quiet = self.skipping() && (self.st.was_read || self.st.settings.skip_unread);
            let file = line.voice.as_ref().map(|v| self.path(v));
            // "card": the voice of the card's own personality.
            let tts = if file.is_none() { c.voice.tts.clone() } else { None };
            let tts = if tts.as_deref() == Some("card") { self.personality(&line.who).map(|p| p.to_string()) } else { tts };
            if !quiet && (file.is_some() || tts.is_some()) && !words.is_empty() {
                let id = self.cmd(
                    Op::Voice {
                        who: line.who.clone(),
                        file,
                        tts,
                        text: words.clone(),
                        pitch: c.voice.pitch.unwrap_or(1.0),
                        volume: self.st.settings.voice_volume,
                    },
                    Some(Pending::Voice),
                );
                self.st.voice = Some(VoiceState { id, who: line.who.clone(), started: false, finished: false });
            }
        }
        if let Some(se) = &line.se {
            if !self.skipping() {
                let file = self.path(se);
                self.cmd(Op::Se { file, volume: self.st.settings.voice_volume }, None);
            }
        }
        self.st.portrait.want(if ch.is_some() { &line.who } else { "" });
        let (name, color) = self.speaker_name(&line);
        let entry = BacklogEntry {
            line: line.id.clone(),
            name,
            color,
            text: words,
            voice: line.voice.as_ref().map(|v| self.path(v)),
            tts: ch.as_ref().and_then(|c| c.voice.tts.clone()),
            who: line.who.clone(),
            chose: None,
        };
        if self.st.progress.backlog.back().map(|b| &b.line) != Some(&line.id) {
            self.st.progress.push_backlog(entry, self.tuning.flow.backlog_size);
        }
    }

    fn speaker_name(&self, line: &Line) -> (String, String) {
        let ch = self.story.characters.get(&line.who);
        let name = match (&line.name, ch) {
            (Some(n), _) => n.get(&self.words.lang),
            (None, Some(c)) => c.name.get(&self.words.lang),
            (None, None) => String::new(),
        };
        (name, ch.map(|c| c.color.clone()).unwrap_or_default())
    }

    fn stop_voice(&mut self) {
        if let Some(v) = self.st.voice.take() {
            if !v.finished {
                self.cmd(Op::StopVoice { who: v.who }, None);
            }
            self.st.pending.remove(&v.id);
        }
    }

    /// After the line: its goto, else the next line, else the act's end.
    fn advance(&mut self) {
        let Some(line) = self.line().cloned() else { return self.end_of_act() };
        if self.st.settings.stop_voice || self.skipping() {
            self.stop_voice();
        }
        self.st.rig.finish();
        if let Some(g) = line.goto.as_deref().and_then(|g| self.story.target(g)) {
            return self.go(g);
        }
        let next = At { act: self.st.progress.act, line: self.st.progress.line + 1 };
        if self.story.line(next).is_some() {
            self.go(next);
        } else {
            self.end_of_act();
        }
    }

    fn choose(&mut self, visible: usize) {
        if self.st.phase != Phase::Line || !self.st.typing.revealed {
            return;
        }
        let Some(&index) = self.st.choice_map.get(visible) else { return };
        let Some(line) = self.line().cloned() else { return };
        let Some(c) = line.choices.get(index) else { return };
        let state = self.st.progress.choice_state(&self.story, &self.words, c);
        if !state.enabled || !state.shown {
            return;
        }
        if c.consume {
            self.st.progress.take(&c.need);
        }
        self.st.progress.give(&c.give);
        self.st.progress.set(&c.set);
        self.st.progress.chosen.push((line.id.clone(), index));
        let label = c.text.get(&self.words.lang);
        if let Some(b) = self.st.progress.backlog.back_mut() {
            if b.line == line.id {
                b.chose = Some(label);
            }
        }
        self.stop_voice();
        self.st.rig.finish();
        if let Some(at) = self.story.target(&c.goto) {
            self.go(at);
        }
    }

    /// Left click, Enter, Space: show the whole line, then go on.
    fn click(&mut self) {
        if self.st.hide_ui {
            self.st.hide_ui = false;
            return;
        }
        match self.st.phase.clone() {
            Phase::Title { effects, .. } => self.enter_line(effects),
            Phase::Line => {
                if !self.st.typing.revealed {
                    self.st.typing.reveal();
                } else if self.line().is_some_and(|l| !l.choices.is_empty()) {
                } else {
                    self.advance();
                }
            }
            Phase::Ended => self.send_end("finished"),
            _ => {}
        }
    }

    fn send_end(&mut self, reason: &str) {
        if !self.st.end_sent {
            self.st.end_sent = true;
            let who: Vec<String> = self.st.shown.iter().filter(|(_, s)| s.blink_taken).map(|(w, _)| w.clone()).collect();
            for w in who {
                self.cmd(Op::AutoBlink { who: w.clone(), on: true }, None);
                self.cmd(Op::Look { who: w, mode: "camera".into(), point: None }, None);
            }
            self.cmd(Op::End { reason: reason.into() }, None);
        }
    }

    // ---- stage ------------------------------------------------------------------------------------------------------

    fn personality(&self, who: &str) -> Option<i32> {
        self.story.characters.get(who).and_then(|c| c.personality).or_else(|| self.st.personalities.get(who).copied())
    }

    fn face_op(&mut self, who: &str, face: &str) {
        let personality = self.personality(who);
        match self.expressions.find(&self.tuning.faces, personality, face) {
            Some(e) => {
                self.cmd(Op::Face { who: who.into(), eyebrow: e.eyebrow, eyes: e.eyes, mouth: e.mouth }, None);
            }
            None => self.log.push(format!("face \"{face}\" is not in the expression table")),
        }
    }

    pub fn face_closed(&self, who: &str) -> bool {
        let Some(face) = self.st.shown.get(who).and_then(|s| s.face.clone()) else { return false };
        self.expressions.find(&self.tuning.faces, self.personality(who), &face).is_some_and(|e| e.closed)
    }

    /// Tells Unreal what changed in the cast at the current line (and brings an offstage speaker for the portrait).
    fn sync_cast(&mut self, line: Option<&Line>) {
        let act = self.story.acts[self.st.progress.act].clone();
        let cast = if line.is_some() {
            script::cast_at(&act, self.st.progress.line)
        } else {
            // Before the first line: the act's own cast.
            let mut a = act.clone();
            a.lines.clear();
            script::cast_at(&a, 0)
        };
        let mut on_stage = Vec::new();
        for c in cast.values() {
            on_stage.push(c.who.clone());
            let def = self.story.characters.get(&c.who).cloned().unwrap_or_default();
            let old = self.st.shown.get(&c.who).cloned();
            let mut s = old.clone().unwrap_or_default();
            if s.offstage {
                self.cmd(Op::ReleaseOffstage { who: c.who.clone() }, None);
                s = Shown::default();
            }
            let moved = !s.visible || s.pos != c.pos || s.yaw != c.yaw || s.scale != c.scale;
            if moved {
                let pending = (!s.visible).then(|| Pending::Place(c.who.clone()));
                self.cmd(
                    Op::Place {
                        who: c.who.clone(),
                        card: self.path(&def.card),
                        outfit: def.outfit,
                        scene_object: def.scene_object,
                        pos: c.pos,
                        yaw: c.yaw,
                        scale: c.scale,
                        visible: true,
                    },
                    pending,
                );
                if !s.visible {
                    // A character placed again starts from the scene's own state.
                    s.motion = None;
                    s.face = None;
                    s.look = None;
                    s.blink_taken = false;
                }
                s.visible = true;
                s.pos = c.pos;
                s.yaw = c.yaw;
                s.scale = c.scale;
            }
            if !s.blink_taken {
                self.cmd(Op::AutoBlink { who: c.who.clone(), on: false }, None);
                s.blink_taken = true;
            }
            if let Some(m) = c.motion.filter(|m| Some(*m) != s.motion) {
                let blend = if self.st.cut { 0.0 } else { self.tuning.flow.motion_blend };
                self.cmd(Op::Motion { who: c.who.clone(), kind: m.kind, group: m.group, category: m.category, no: m.no, blend }, None);
                s.motion = Some(m);
            }
            if let Some(f) = c.face.clone().filter(|f| Some(f) != s.face.as_ref()) {
                self.face_op(&c.who, &f);
                s.face = Some(f);
            }
            self.st.shown.insert(c.who.clone(), s);
        }
        // Gone from the cast: hidden.
        let gone: Vec<String> = self.st.shown.iter().filter(|(w, s)| s.visible && !s.offstage && !on_stage.contains(w)).map(|(w, _)| w.clone()).collect();
        for who in gone {
            let def = self.story.characters.get(&who).cloned().unwrap_or_default();
            let s = self.st.shown.get_mut(&who).unwrap();
            s.visible = false;
            let (pos, yaw, scale) = (s.pos, s.yaw, s.scale);
            self.cmd(
                Op::Place { who, card: self.path(&def.card), outfit: def.outfit, scene_object: def.scene_object, pos, yaw, scale, visible: false },
                None,
            );
        }
        // A speaker who is not on stage: a copy for the portrait.
        let Some(line) = line else { return };
        if line.who.is_empty() || on_stage.contains(&line.who) {
            return;
        }
        let Some(def) = self.story.characters.get(&line.who).cloned() else { return };
        self.spawn_offstage(&line.who);
        let mut s = self.st.shown.get(&line.who).cloned().unwrap_or_default();
        // Her line's motion, else her offstage one, else the default standing pose (a copy never stands in its bind pose).
        let fallback = crate::script::MotionSpec::Code(self.tuning.portrait.offstage_motion.clone());
        let motion = line.motion.as_ref().or(def.offstage.motion.as_ref()).or(Some(&fallback)).and_then(|m| m.resolve().ok());
        if let Some(m) = motion.filter(|m| Some(*m) != s.motion) {
            self.cmd(Op::Motion { who: line.who.clone(), kind: m.kind, group: m.group, category: m.category, no: m.no, blend: 0.0 }, None);
            s.motion = Some(m);
        }
        if let Some(f) = line.face.clone().or(def.offstage.face.clone()).filter(|f| Some(f) != s.face.as_ref()) {
            self.face_op(&line.who, &f);
            s.face = Some(f);
        }
        self.st.shown.insert(line.who.clone(), s);
    }

    /// The portrait copy of a speaker not on stage (built once, kept until the act ends or she comes on stage).
    fn spawn_offstage(&mut self, who: &str) {
        let Some(def) = self.story.characters.get(who).cloned() else { return };
        if self.st.shown.get(who).is_some_and(|s| s.offstage) {
            return;
        }
        let index = self.st.shown.values().filter(|s| s.offstage).count() as f64;
        let at = math::add(self.tuning.portrait.offstage_at, [index * 300.0, 0.0, 0.0]);
        self.cmd(Op::SpawnOffstage { who: who.to_string(), card: self.path(&def.card), outfit: def.outfit, pos: at }, Some(Pending::Offstage(who.to_string())));
        self.cmd(Op::AutoBlink { who: who.to_string(), on: false }, None);
        self.st.shown.insert(who.to_string(), Shown { offstage: true, visible: true, pos: Some(at), scale: Some(1.0), blink_taken: true, ..Default::default() });
    }

    /// The next line's speaker, when she will speak from offstage, is built now (her portrait is ready in time).
    fn preload_offstage(&mut self) {
        let next = At { act: self.st.progress.act, line: self.st.progress.line + 1 };
        let Some(who) = self.story.line(next).map(|l| l.who.clone()).filter(|w| !w.is_empty()) else { return };
        let cast = script::cast_at(&self.story.acts[next.act], next.line);
        if !cast.values().any(|c| c.who == who) && !self.st.shown.get(&who).is_some_and(|s| s.visible && !s.offstage) {
            self.spawn_offstage(&who);
        }
    }

    fn present(&self) -> Vec<String> {
        self.st.shown.iter().filter(|(_, s)| s.visible && !s.offstage).map(|(w, _)| w.clone()).collect()
    }

    fn camera_candidates(&self, spec: &script::CameraSpec, speaker: &str) -> Vec<camera::Candidate> {
        let present = self.present();
        let ctx = ShotContext {
            speaker,
            present: present.iter().map(String::as_str).collect(),
            actors: &self.facts.actors,
            cameras: &self.st.cameras,
            active_camera: self.st.active_camera,
            aspect: if self.facts.viewport[1] > 0.0 { self.facts.viewport[0] / self.facts.viewport[1] } else { 16.0 / 9.0 },
        };
        camera::candidates(&self.tuning.camera, spec, &ctx, &self.st.rig.current)
    }

    fn line_camera(&mut self, line: &Line) {
        let act = &self.story.acts[self.st.progress.act];
        let Some((_, spec)) = script::camera_at(act, self.st.progress.line) else { return };
        let spec = spec.clone();
        let cut = self.st.cut || spec.cut == Some(true) || self.skipping() || !self.st.camera_set;
        self.st.rig.waiting = None;
        self.st.probe = None;
        let shot = spec.shot.as_deref().is_some_and(|s| !s.starts_with("studio"));
        let candidates = self.camera_candidates(&spec, &line.who);
        let Some(first) = candidates.first().copied() else {
            // The people of the shot are still being placed: framed when they are there.
            if shot {
                self.st.rig.waiting = Some(cut);
                self.st.rig.reframe_for = 2.0;
                self.st.cut = false;
            }
            return;
        };
        self.st.rig.go(&self.tuning.camera, first.pose, cut, 1.0);
        self.st.rig.reframe_for = if shot { 0.6 } else { 0.0 };
        self.st.camera_set = true;
        self.st.cut = false;
        self.probe(candidates, cut);
    }

    /// Asks Unreal which of a shot's places sees its subject without a wall in between.
    fn probe(&mut self, candidates: Vec<camera::Candidate>, cut: bool) {
        if candidates.len() < 2 {
            return;
        }
        let mut ids = Vec::new();
        for c in &candidates {
            let id = self.st.next_id.max(1);
            self.st.next_id = id + 1;
            self.probes_out.push(Probe { id, from: c.from, to: c.pose.pos });
            ids.push(id);
        }
        self.st.probe = Some(ProbeJob { ids, candidates, cut, chosen: None, asked: self.st.time });
    }

    /// The probe answers: the first place with a clear view (else the clearest), taken at once.
    fn probe_answers(&mut self) {
        let margin = self.tuning.camera.probe_margin;
        let Some(job) = self.st.probe.as_mut().filter(|j| j.chosen.is_none()) else { return };
        let clear: Vec<Option<f64>> = job.ids.iter().map(|id| self.facts.probes.iter().find(|p| p.id == *id).map(|p| p.clear)).collect();
        if clear.iter().any(Option::is_none) {
            // Unreal that cannot probe: the first place stays.
            if self.st.time - job.asked > 1.0 {
                job.chosen = Some(0);
            }
            return;
        }
        let reach: Vec<(f64, f64)> =
            job.candidates.iter().zip(&clear).map(|(c, r)| (r.unwrap_or(0.0), math::len(math::sub(c.pose.pos, c.from)))).collect();
        let pick = reach.iter().position(|(c, d)| *c >= d - margin).unwrap_or_else(|| {
            (0..reach.len()).max_by(|a, b| (reach[*a].0 / reach[*a].1.max(1.0)).total_cmp(&(reach[*b].0 / reach[*b].1.max(1.0)))).unwrap_or(0)
        });
        job.chosen = Some(pick);
        let (pose, cut) = (job.candidates[pick].pose, job.cut);
        if pick != 0 {
            if self.st.rig.moving() {
                self.st.rig.retarget(pose);
            } else {
                self.st.rig.go(&self.tuning.camera, pose, cut, 1.0);
            }
        }
    }

    /// A shot is framed again while the people it frames are still being built or posed.
    fn reframe(&mut self, dt: f64) {
        if self.st.rig.reframe_for <= 0.0 || self.st.phase != Phase::Line {
            return;
        }
        self.st.rig.reframe_for -= dt;
        let Some(line) = self.line().cloned() else { return };
        let act = &self.story.acts[self.st.progress.act];
        let Some((_, spec)) = script::camera_at(act, self.st.progress.line) else { return };
        let spec = spec.clone();
        let candidates = self.camera_candidates(&spec, &line.who);
        if candidates.is_empty() {
            return;
        }
        match self.st.rig.waiting.take() {
            Some(cut) => {
                let cut = cut || !self.st.camera_set;
                self.st.rig.go(&self.tuning.camera, candidates[0].pose, cut, 1.0);
                self.st.rig.reframe_for = 0.6;
                self.st.camera_set = true;
                self.probe(candidates, cut);
            }
            None => {
                let pick = self.st.probe.as_ref().and_then(|j| j.chosen).unwrap_or(0).min(candidates.len() - 1);
                self.st.rig.retarget(candidates[pick].pose);
            }
        }
    }

    // ---- life -------------------------------------------------------------------------------------------------------

    fn life(&mut self, dt: f64) {
        let speaker = self.line().map(|l| l.who.clone()).unwrap_or_default();
        let look = self.line().and_then(|l| l.look.clone());
        let present = self.present();
        let speaker_on = present.contains(&speaker);
        let closed: BTreeMap<String, bool> = present.iter().map(|w| (w.clone(), self.face_closed(w))).collect();
        let facts = std::mem::take(&mut self.facts);
        let people: Vec<Present> = present
            .iter()
            .map(|w| {
                let (base, fixed) = if *w == speaker {
                    match look.as_deref() {
                        None | Some("camera") => (Gaze::Camera, look.is_some()),
                        Some("auto") => (Gaze::Camera, false),
                        Some("none") => (Gaze::Animation, true),
                        Some(other) => (Gaze::At(other.to_string()), true),
                    }
                } else if speaker_on {
                    (Gaze::At(speaker.clone()), false)
                } else {
                    (Gaze::Camera, false)
                };
                Present { who: w.as_str(), base, fixed, closed: closed[w], facts: facts.actors.get(w) }
            })
            .collect();
        let view = facts.view.map(|v| v.pos).or(Some(self.st.rig.current.pos));
        let mut rng = self.st.rng.take().unwrap_or_else(|| Rng::new(self.cfg.seed));
        let d = self.st.life.tick(&self.tuning.life, &mut rng, dt, &people, view);
        self.st.rng = Some(rng);
        let mut ops = Vec::new();
        for w in &d.blinks {
            ops.push(Op::Blink { who: w.clone() });
        }
        for (w, g) in &d.gaze {
            let me = facts.actors.get(w).cloned().unwrap_or_default();
            let want: (String, Option<V3>) = match g {
                Gaze::Camera => ("camera".into(), None),
                Gaze::Animation => ("animation".into(), None),
                Gaze::At(_) | Gaze::Point(_) => ("point".into(), Some(life::target_point(g, &me, &people, view))),
            };
            let s = self.st.shown.get_mut(w).unwrap();
            let changed = match (&s.look, &want) {
                (Some((m, p)), (wm, wp)) if m == wm => match (p, wp) {
                    (Some(a), Some(b)) => math::len(math::sub(*a, *b)) > 2.0,
                    _ => false,
                },
                _ => true,
            };
            if changed {
                s.look = Some(want.clone());
                ops.push(Op::Look { who: w.clone(), mode: want.0, point: want.1 });
            }
        }
        drop(people);
        self.facts = facts;
        for op in ops {
            self.cmd(op, None);
        }
    }

    // ---- input ------------------------------------------------------------------------------------------------------

    fn handle(&mut self, i: Input) {
        if self.st.rig.fly.is_some() {
            match &i {
                Input::Key { key, down } => match key.as_str() {
                    "Escape" if *down => {
                        self.st.rig.cancel_fly();
                        return;
                    }
                    "Enter" if *down => {
                        self.store_camera();
                        return;
                    }
                    _ => {
                        self.st.rig.fly_key(key, *down);
                        return;
                    }
                },
                Input::MouseDelta { dx, dy } => {
                    self.st.rig.fly_turn(&self.tuning.camera, *dx, *dy);
                    return;
                }
                _ => return,
            }
        }
        match i {
            Input::Key { key, down } => {
                let ctrl = matches!(key.as_str(), "LeftControl" | "RightControl");
                if ctrl {
                    self.st.skip_hold = down && self.st.screen == "play";
                    return;
                }
                if !down {
                    return;
                }
                match key.as_str() {
                    "Enter" | "SpaceBar" | "Down" if self.st.screen == "play" => self.click(),
                    "Escape" => {
                        if self.st.screen == "play" {
                            self.open_screen("menu");
                        } else {
                            self.st.screen = "play".into();
                        }
                    }
                    "F5" => self.save_slot(SLOT_QUICK, true),
                    "F9" => self.load_slot(SLOT_QUICK),
                    "H" if self.st.screen == "play" => self.st.hide_ui = !self.st.hide_ui,
                    "A" if self.st.screen == "play" => self.st.auto = !self.st.auto,
                    "F8" if self.st.screen == "play" && self.st.phase == Phase::Line => self.st.rig.start_fly(),
                    "F7" => {
                        if self.st.screen == "outline" {
                            self.st.screen = "play".into();
                        } else {
                            self.open_screen("outline");
                        }
                    }
                    _ => {}
                }
            }
            Input::Click { button } => {
                if self.st.screen != "play" {
                    return;
                }
                if button == "right" {
                    self.st.hide_ui = !self.st.hide_ui;
                } else {
                    self.st.skip = false;
                    self.click();
                }
            }
            Input::Wheel { delta } => {
                if self.st.screen == "play" && delta > 0.0 {
                    self.open_screen("backlog");
                } else if self.st.screen == "play" && delta < 0.0 {
                    self.click();
                }
            }
            Input::Mouse { x, y } => {
                self.st.rig.pointer = [(x * 2.0 - 1.0).clamp(-1.0, 1.0), (1.0 - y * 2.0).clamp(-1.0, 1.0)];
            }
            Input::MouseDelta { .. } => {}
            Input::Choose { index } => self.choose(index),
            Input::Jump { line } => self.jump(&line),
            Input::Button { id, value } => self.button(&id, value),
        }
    }

    fn jump(&mut self, line: &str) {
        let Some(at) = self.story.target(line) else { return self.log.push(format!("jump: no act or line {line}")) };
        self.stop_voice();
        self.st.rig.finish();
        self.st.screen = "play".into();
        if at.act != self.st.progress.act || matches!(self.st.phase, Phase::Ended | Phase::Start) {
            self.st.progress.ended = false;
            self.begin(at, false, false);
        } else {
            self.st.progress.line = at.line;
            self.enter_line(false);
        }
    }

    fn open_screen(&mut self, s: &str) {
        self.st.screen = s.into();
        self.st.page = 0;
        self.st.skip = false;
        self.st.skip_hold = false;
    }

    fn button(&mut self, id: &str, value: Option<Value>) {
        let (head, arg) = id.split_once(':').unwrap_or((id, ""));
        match head {
            "auto" => self.st.auto = !self.st.auto,
            "skip" => self.st.skip = !self.st.skip,
            "hide" => self.st.hide_ui = true,
            "menu" | "backlog" | "settings" | "outline" => self.open_screen(head),
            "jump" => self.jump(arg),
            "save" | "load" => self.open_screen(head),
            "close" => self.st.screen = "play".into(),
            "page" => self.st.page = arg.parse().unwrap_or(0),
            "quick_save" => self.save_slot(SLOT_QUICK, true),
            "quick_load" => self.load_slot(SLOT_QUICK),
            "slot" => {
                if self.st.screen == "save" {
                    self.save_slot(arg, true);
                } else {
                    self.load_slot(arg);
                }
            }
            "replay" => {
                if let Some(b) = arg.parse::<usize>().ok().and_then(|i| self.st.progress.backlog.get(i)).cloned() {
                    if b.voice.is_some() || b.tts.is_some() {
                        let pitch = self.story.characters.get(&b.who).and_then(|c| c.voice.pitch).unwrap_or(1.0);
                        self.cmd(Op::Voice { who: b.who, file: b.voice, tts: b.tts, text: b.text, pitch, volume: self.st.settings.voice_volume }, None);
                    }
                }
            }
            "set" => self.set(arg, value),
            "quit" | "end" => self.send_end("quit"),
            "camera" => {
                if self.st.phase == Phase::Line {
                    self.st.screen = "play".into();
                    self.st.rig.start_fly();
                }
            }
            _ => self.log.push(format!("unknown button {id}")),
        }
    }

    fn set(&mut self, name: &str, value: Option<Value>) {
        let s = &mut self.st.settings;
        let num = value.as_ref().and_then(|v| v.as_f64());
        let flag = value.as_ref().and_then(|v| v.as_bool());
        match name {
            "text_speed" => s.text_speed = num.map(|n| n.round().clamp(1.0, 10.0) as u32).unwrap_or(s.text_speed),
            "auto_speed" => s.auto_speed = num.map(|n| n.round().clamp(1.0, 10.0) as u32).unwrap_or(s.auto_speed),
            "skip_unread" => s.skip_unread = flag.unwrap_or(!s.skip_unread),
            "voice_volume" => s.voice_volume = num.map(|n| n.clamp(0.0, 1.0)).unwrap_or(s.voice_volume),
            "stop_voice" => s.stop_voice = flag.unwrap_or(!s.stop_voice),
            "parallax" => s.parallax = num.map(|n| n.clamp(0.0, 1.0)).unwrap_or(s.parallax),
            "ui_opacity" => s.ui_opacity = num.map(|n| n.clamp(0.2, 1.0)).unwrap_or(s.ui_opacity),
            _ => return self.log.push(format!("unknown setting {name}")),
        }
        self.write_settings();
    }

    /// Enter while flying: the line gets this camera, written into the story file.
    fn store_camera(&mut self) {
        let pose = self.st.rig.current;
        self.st.rig.fly = None;
        let at = self.st.progress.at();
        let Some(line) = self.line().cloned() else { return };
        let camera = script::CameraSpec { pos: Some(pose.pos), rot: Some(pose.rot.to_array()), fov: Some(pose.fov), ..Default::default() };
        let result = (|| -> Result<(), String> {
            let text = std::fs::read_to_string(&self.cfg.story).map_err(|e| e.to_string())?;
            let mut v: Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
            let slot = v.pointer_mut(&format!("/acts/{}/lines/{}", at.act, at.line)).and_then(|l| l.as_object_mut()).ok_or("line not found")?;
            let round = |x: f64, k: f64| (x * k).round() / k;
            slot.insert(
                "camera".into(),
                json!({"pos": pose.pos.map(|v| round(v, 10.0)), "rot": pose.rot.to_array().map(|v| round(v, 1e6)), "fov": round(pose.fov, 100.0)}),
            );
            let out = serde_json::to_string_pretty(&v).map_err(|e| e.to_string())?;
            std::fs::write(&self.cfg.story, out + "\n").map_err(|e| e.to_string())?;
            Ok(())
        })();
        match result {
            Ok(()) => {
                if let Some(l) = self.story.acts.get_mut(at.act).and_then(|a| a.lines.get_mut(at.line)) {
                    l.camera = Some(camera);
                }
                if let Ok(bytes) = std::fs::read(&self.cfg.story) {
                    self.story_hash = hash(&bytes);
                }
                let msg = self.words.m("ui.camera_saved", &[("line", Arg::Str(line.id))]);
                self.toast(msg);
            }
            Err(e) => self.log.push(format!("camera not saved: {e}")),
        }
    }

    // ---- saves ------------------------------------------------------------------------------------------------------

    fn save_slot(&mut self, slot: &str, announce: bool) {
        let Some(dir) = self.slot_dir(slot) else { return self.log.push("save: no save directory".into()) };
        let Some(line) = self.line().cloned() else { return };
        let act = &self.story.acts[self.st.progress.act];
        let (name, _) = self.speaker_name(&line);
        let words = line.text.get(&self.words.lang);
        let summary: String = text::graphemes(&words).into_iter().take(40).collect();
        let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0);
        let data = json!({
            "schema": 1,
            "story": self.cfg.story.to_string_lossy(),
            "story_hash": self.story_hash,
            "act": act.id,
            "act_title": act.title.as_ref().map(|t| t.get(&self.words.lang)).unwrap_or_default(),
            "line": line.id,
            "name": name,
            "summary": summary,
            "saved": now,
            "progress": self.st.progress,
            "rng": self.st.rng,
            "portrait_references": self.st.portrait.references,
        });
        let result = std::fs::create_dir_all(&dir)
            .and_then(|_| std::fs::write(dir.join("state.json"), serde_json::to_string_pretty(&data).unwrap_or_default()));
        if let Err(e) = result {
            return self.log.push(format!("save {slot}: {e}"));
        }
        self.write_read();
        let thumb = dir.join("thumb.jpg").to_string_lossy().replace('\\', "/");
        let [width, height] = self.tuning.flow.thumbnail;
        self.cmd(Op::Thumbnail { path: thumb, width, height }, Some(Pending::Thumbnail));
        if announce {
            let label = self.slot_label(slot);
            let msg = self.words.m("ui.saved", &[("n", Arg::Str(label))]);
            self.toast(msg);
            self.st.screen = "play".into();
        }
    }

    fn slot_label(&self, slot: &str) -> String {
        match slot {
            SLOT_QUICK => self.words.t("ui.quick"),
            SLOT_AUTO => self.words.t("ui.auto_save"),
            n => self.words.m("ui.slot", &[("n", Arg::Str(n.trim_start_matches('0').to_string()))]),
        }
    }

    fn load_slot(&mut self, slot: &str) {
        let Some(dir) = self.slot_dir(slot) else { return };
        let Ok(text) = std::fs::read_to_string(dir.join("state.json")) else { return };
        let Ok(v) = serde_json::from_str::<Value>(&text) else { return self.log.push(format!("save {slot}: unreadable")) };
        let Ok(progress) = serde_json::from_value::<Progress>(v["progress"].clone()) else { return self.log.push(format!("save {slot}: no progress")) };
        if v["story_hash"].as_str() != Some(self.story_hash.as_str()) {
            let msg = self.words.t("ui.story_changed");
            self.toast(msg);
        }
        let at = v["line"]
            .as_str()
            .and_then(|l| self.story.target(l))
            .or_else(|| v["act"].as_str().and_then(|a| self.story.act_index(a)).map(|a| At { act: a, line: 0 }))
            .unwrap_or_else(|| self.story.start());
        self.stop_voice();
        let read = std::mem::take(&mut self.st.progress.read);
        self.st.progress = progress;
        self.st.progress.ended = false;
        if let Ok(rng) = serde_json::from_value::<Rng>(v["rng"].clone()) {
            self.st.rng = Some(rng);
        }
        if let Ok(refs) = serde_json::from_value(v["portrait_references"].clone()) {
            self.st.portrait.references = refs;
        }
        self.st.screen = "play".into();
        self.st.auto = false;
        self.st.skip = false;
        self.st.end_sent = false;
        // What has been read stays read, whichever save is loaded.
        self.st.progress.read.extend(read);
        let label = self.slot_label(slot);
        let msg = self.words.m("ui.loaded", &[("n", Arg::Str(label))]);
        self.toast(msg);
        self.begin(at, false, false);
    }

    fn slots(&self) -> Vec<Value> {
        let n = self.tuning.flow.save_slots.max(1);
        let mut names: Vec<String> = vec![SLOT_QUICK.into(), SLOT_AUTO.into()];
        names.extend((1..=n).map(|i| format!("{i:02}")));
        names
            .into_iter()
            .map(|slot| {
                let dir = self.slot_dir(&slot);
                let data = dir.as_ref().and_then(|d| std::fs::read_to_string(d.join("state.json")).ok()).and_then(|t| serde_json::from_str::<Value>(&t).ok());
                let thumb = dir.as_ref().map(|d| d.join("thumb.jpg")).filter(|p| p.is_file()).map(|p| p.to_string_lossy().replace('\\', "/"));
                let label = self.slot_label(&slot);
                match data {
                    Some(d) => json!({
                        "id": slot, "label": label, "empty": false, "thumb": thumb,
                        "when": self.when(d["saved"].as_i64().unwrap_or(0)),
                        "act": d["act_title"], "name": d["name"], "summary": d["summary"],
                    }),
                    None => json!({"id": slot, "label": label, "empty": true, "summary": self.words.t("ui.empty_slot")}),
                }
            })
            .collect()
    }

    /// yyyy-mm-dd hh:mm in the configured offset.
    fn when(&self, unix: i64) -> String {
        if unix <= 0 {
            return String::new();
        }
        let t = unix + self.cfg.utc_offset_minutes * 60;
        let days = t.div_euclid(86400);
        let secs = t.rem_euclid(86400);
        // Civil from days (H. Hinnant).
        let z = days + 719468;
        let era = z.div_euclid(146097);
        let doe = z - era * 146097;
        let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        let y = yoe + era * 400;
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        let mp = (5 * doy + 2) / 153;
        let d = doy - (153 * mp + 2) / 5 + 1;
        let m = if mp < 10 { mp + 3 } else { mp - 9 };
        let y = if m <= 2 { y + 1 } else { y };
        format!("{y:04}-{m:02}-{d:02} {:02}:{:02}", secs / 3600, secs % 3600 / 60)
    }

    // ---- tick -------------------------------------------------------------------------------------------------------

    pub fn ack(&mut self, a: Ack) -> bool {
        let Some(p) = self.st.pending.remove(&a.id) else { return false };
        if !a.ok {
            self.log.push(format!("command {} failed: {}", a.id, a.error.clone().unwrap_or_default()));
        }
        match p {
            Pending::Scene => {
                self.st.cameras = a.cameras;
                self.st.active_camera = a.active_camera;
            }
            Pending::Place(who) | Pending::Offstage(who) => {
                if let Some(n) = a.personality {
                    self.st.personalities.insert(who.clone(), n);
                    // A face asked for before the personality was known: again with the right profile.
                    if let Some(face) = self.st.shown.get(&who).and_then(|s| s.face.clone()) {
                        self.face_op(&who, &face);
                    }
                }
            }
            Pending::Voice => {
                if let Some(v) = &mut self.st.voice {
                    if v.id == a.id {
                        if a.ok {
                            v.started = true;
                        } else {
                            v.finished = true;
                        }
                    }
                }
            }
            Pending::Thumbnail | Pending::Other => {}
        }
        true
    }

    fn paused(&self) -> bool {
        self.st.screen != "play" || self.st.rig.fly.is_some()
    }

    fn line_tick(&mut self, dt: f64) {
        let Some(line) = self.line().cloned() else { return };
        self.st.line_age += dt;
        if self.st.auto_save && self.st.line_age > 1.0 {
            self.st.auto_save = false;
            self.save_slot(SLOT_AUTO, false);
        }
        let voice = self.facts.voice.clone();
        let mut following = false;
        if let Some(v) = &mut self.st.voice {
            if voice.id == v.id && v.id != 0 {
                if voice.done {
                    v.finished = true;
                }
                if voice.progress >= 0.0 && (voice.playing || voice.done) {
                    self.st.typing.follow(if voice.done { 1.0 } else { voice.progress });
                    following = true;
                }
            }
        }
        let voice_over = self.st.voice.as_ref().map(|v| v.finished).unwrap_or(true);
        if voice_over && self.st.voice.is_some() {
            self.st.typing.reveal();
        }
        if !following {
            self.st.typing.type_for(&self.tuning.text, dt, self.st.settings.text_speed);
        }
        let complete = self.st.typing.revealed && voice_over;
        if complete && self.st.done_at.is_none() {
            self.st.done_at = Some(self.st.time);
        }
        if !line.choices.is_empty() {
            if self.st.typing.revealed {
                self.st.skip = false;
                self.st.skip_hold = false;
            }
            return;
        }
        if self.skipping() {
            if !self.st.was_read && !self.st.settings.skip_unread {
                self.st.skip = false;
                return;
            }
            if self.st.line_age >= self.tuning.text.skip_line {
                self.advance();
            }
            return;
        }
        if self.st.auto {
            if let Some(done) = self.st.done_at {
                let factor = 1.6f64.powf((5.0 - self.st.settings.auto_speed as f64) / 4.0);
                let wait = (self.tuning.text.auto_base + self.st.typing.total as f64 * self.tuning.text.auto_per_char) * factor;
                if self.st.time - done >= wait {
                    self.advance();
                }
            }
        }
    }

    pub fn tick(&mut self, facts: Facts) -> Frame {
        let dt = if facts.dt.is_finite() { facts.dt.clamp(0.0, 0.25) } else { 0.0 };
        self.facts = facts;
        self.st.time += dt;
        for i in std::mem::take(&mut self.inbox) {
            self.handle(i);
        }
        if let Some((_, until)) = &self.st.toast {
            if *until <= self.st.time {
                self.st.toast = None;
            }
        }
        self.probe_answers();
        let paused = self.paused();
        match self.st.phase.clone() {
            Phase::Start => {
                let at = self.story.start();
                self.begin(at, true, true);
            }
            Phase::Loading { .. } => self.loading_done(),
            Phase::Title { elapsed, effects } => {
                if !paused {
                    let f = &self.tuning.flow;
                    let elapsed = elapsed + dt;
                    if elapsed >= f.title_enter + f.title_hold + f.title_exit || self.skipping() {
                        self.enter_line(effects);
                    } else {
                        self.st.phase = Phase::Title { elapsed, effects };
                    }
                }
            }
            Phase::Line => {
                if !paused {
                    self.line_tick(dt);
                }
            }
            Phase::Ended => {}
        }
        if self.st.rig.fly.is_some() {
            self.st.rig.fly_tick(&self.tuning.camera, dt);
        }
        self.reframe(dt);
        self.st.rig.tick(&self.tuning.camera, dt, paused);
        self.st.portrait.tick(&self.tuning.portrait, dt);
        if !paused && matches!(self.st.phase, Phase::Line | Phase::Title { .. }) {
            self.life(dt);
        }
        self.frame()
    }

    fn frame(&mut self) -> Frame {
        let mut f = Frame { time: self.st.time, screen: self.st.screen.clone(), layout: self.tuning.layout.clone(), ..Default::default() };
        f.layout.dialog_opacity = self.st.settings.ui_opacity;
        f.hide_ui = self.st.hide_ui;
        f.loading = matches!(self.st.phase, Phase::Loading { .. });
        if self.st.phase == Phase::Ended {
            f.screen = if self.st.screen == "play" { "ended".into() } else { self.st.screen.clone() };
        }
        if self.st.camera_set {
            let p = self.st.rig.shown(&self.tuning.camera, self.st.settings.parallax);
            f.camera = Some(CameraOut { pos: p.pos, rot: p.rot.to_array(), fov: p.fov, moving: self.st.rig.moving() });
        }
        let line = self.line().cloned();
        let ui = !self.st.hide_ui && self.st.rig.fly.is_none();
        if let (Phase::Line, Some(line)) = (&self.st.phase, &line) {
            let (name, color) = self.speaker_name(line);
            let waiting = if !self.st.typing.revealed {
                if self.st.voice.as_ref().is_some_and(|v| !v.finished) { "voice" } else { "typing" }
            } else if !line.choices.is_empty() {
                "choice"
            } else if self.skipping() {
                "skip"
            } else if self.st.voice.as_ref().is_some_and(|v| !v.finished) {
                "voice"
            } else if self.st.auto {
                "auto"
            } else {
                "click"
            };
            f.dialog = DialogOut {
                visible: ui,
                name,
                name_color: color,
                text: self.st.typing.full.clone(),
                text_visible: self.st.typing.visible(),
                shown: self.st.typing.shown,
                total: self.st.typing.total,
                waiting: waiting.into(),
                opacity: self.st.settings.ui_opacity,
            };
            if self.st.typing.revealed && !line.choices.is_empty() {
                self.st.choice_map.clear();
                for (i, c) in line.choices.iter().enumerate() {
                    let s = self.st.progress.choice_state(&self.story, &self.words, c);
                    if s.shown {
                        self.st.choice_map.push(i);
                        f.choices.push(ChoiceOut { text: c.text.get(&self.words.lang), enabled: s.enabled, hint: s.hint });
                    }
                }
            }
        } else {
            f.dialog.waiting = match self.st.phase {
                Phase::Loading { .. } => "loading",
                Phase::Title { .. } => "title",
                Phase::Ended => "ended",
                _ => "",
            }
            .into();
            if self.st.phase == Phase::Ended && ui {
                f.dialog.visible = true;
                f.dialog.text_visible = self.words.t("ui.end");
                f.dialog.text = f.dialog.text_visible.clone();
                f.dialog.shown = text::graphemes(&f.dialog.text_visible).len();
                f.dialog.total = f.dialog.shown;
                f.dialog.opacity = self.st.settings.ui_opacity;
            }
        }
        if let Phase::Title { elapsed, .. } = self.st.phase {
            let fl = &self.tuning.flow;
            let act = &self.story.acts[self.st.progress.act];
            let text = act.title.as_ref().map(|t| t.get(&self.words.lang)).unwrap_or_default();
            let (x, alpha) = if elapsed < fl.title_enter {
                let e = bezier(fl.title_enter_curve, elapsed / fl.title_enter.max(1e-3));
                (-1.25 * (1.0 - e), e.clamp(0.0, 1.0))
            } else if elapsed < fl.title_enter + fl.title_hold {
                (0.0, 1.0)
            } else {
                let e = bezier(fl.title_exit_curve, (elapsed - fl.title_enter - fl.title_hold) / fl.title_exit.max(1e-3));
                (-1.25 * e, (1.0 - e).clamp(0.0, 1.0))
            };
            f.title = Some(TitleOut { text, x, alpha });
        }
        let adjust = self.story.characters.get(&self.st.portrait.shown).map(|c| c.portrait.clone()).unwrap_or_default();
        f.portrait = self.st.portrait.out(&self.tuning.portrait, &self.tuning.layout, &adjust, &self.facts.actors, self.facts.viewport);
        if !(ui && self.st.phase == Phase::Line) {
            f.portrait.visible = false;
        }
        let w = &self.words;
        let b = |id: &str, label: &str, on: bool| ButtonOut { id: id.into(), label: w.t(label), on };
        f.buttons = vec![
            b("auto", "ui.auto", self.st.auto),
            b("skip", "ui.skip", self.skipping()),
            b("backlog", "ui.backlog", false),
            b("save", "ui.save", false),
            b("load", "ui.load", false),
            b("settings", "ui.settings", false),
            b("hide", "ui.hide", false),
        ];
        f.panel = self.panel();
        f.toast = self.st.toast.as_ref().map(|(t, _)| t.clone());
        if self.st.rig.fly.is_some() {
            f.camera_edit = Some(self.words.t("ui.camera_edit"));
        }
        f.probes = std::mem::take(&mut self.probes_out);
        f.commands = std::mem::take(&mut self.out);
        f.log = std::mem::take(&mut self.log);
        f
    }

    fn panel(&self) -> Value {
        let w = &self.words;
        match self.st.screen.as_str() {
            "menu" => json!({"items": [
                {"id": "save", "label": w.t("ui.save")}, {"id": "load", "label": w.t("ui.load")},
                {"id": "backlog", "label": w.t("ui.backlog")}, {"id": "outline", "label": w.t("ui.outline")}, {"id": "settings", "label": w.t("ui.settings")},
                {"id": "close", "label": w.t("ui.close")}, {"id": "quit", "label": w.t("ui.quit")},
            ]}),
            "save" | "load" => {
                let all = self.slots();
                let per = 6usize;
                let pages = all.len().div_ceil(per).max(1);
                let page = self.st.page.min(pages - 1);
                let slots: Vec<Value> = all.into_iter().skip(page * per).take(per).collect();
                json!({"mode": self.st.screen, "title": w.t(if self.st.screen == "save" { "ui.save" } else { "ui.load" }), "page": page, "pages": pages, "slots": slots, "close": w.t("ui.close")})
            }
            "outline" => {
                // Every line of the story; a click plays from it (an editor's jump, effects not applied).
                let current = self.line().map(|l| l.id.clone()).unwrap_or_default();
                let mut entries = Vec::new();
                for act in &self.story.acts {
                    let title = act.title.as_ref().map(|t| t.get(&w.lang)).unwrap_or_default();
                    entries.push(json!({"act": act.id, "title": title, "header": true}));
                    for l in &act.lines {
                        let name = self.story.characters.get(&l.who).map(|c| c.name.get(&w.lang)).unwrap_or_default();
                        let text: String = text::graphemes(&l.text.get(&w.lang)).into_iter().take(36).collect();
                        entries.push(json!({"id": l.id, "name": name, "text": text, "current": l.id == current,
                            "read": self.st.progress.read.contains(&l.id), "choices": l.choices.len()}));
                    }
                }
                json!({"title": w.t("ui.outline"), "close": w.t("ui.close"), "entries": entries})
            }
            "backlog" => {
                let entries: Vec<Value> = self
                    .st
                    .progress
                    .backlog
                    .iter()
                    .enumerate()
                    .map(|(i, b)| json!({"index": i, "name": b.name, "color": b.color, "text": b.text, "chose": b.chose, "voice": b.voice.is_some() || b.tts.is_some()}))
                    .collect();
                json!({"title": w.t("ui.backlog"), "entries": entries, "replay": w.t("ui.replay"), "close": w.t("ui.close")})
            }
            "settings" => {
                let s = &self.st.settings;
                json!({"title": w.t("ui.settings"), "close": w.t("ui.close"), "on": w.t("set.on"), "off": w.t("set.off"), "items": [
                    {"id": "text_speed", "label": w.t("set.text_speed"), "kind": "range", "value": s.text_speed, "min": 1, "max": 10, "step": 1},
                    {"id": "auto_speed", "label": w.t("set.auto_speed"), "kind": "range", "value": s.auto_speed, "min": 1, "max": 10, "step": 1},
                    {"id": "skip_unread", "label": w.t("set.skip_unread"), "kind": "toggle", "value": s.skip_unread},
                    {"id": "voice_volume", "label": w.t("set.voice_volume"), "kind": "range", "value": s.voice_volume, "min": 0, "max": 1, "step": 0.05},
                    {"id": "stop_voice", "label": w.t("set.stop_voice"), "kind": "toggle", "value": s.stop_voice},
                    {"id": "parallax", "label": w.t("set.parallax"), "kind": "range", "value": s.parallax, "min": 0, "max": 1, "step": 0.05},
                    {"id": "ui_opacity", "label": w.t("set.ui_opacity"), "kind": "range", "value": s.ui_opacity, "min": 0.2, "max": 1, "step": 0.05},
                ]})
            }
            _ => Value::Null,
        }
    }

    // ---- queries ----------------------------------------------------------------------------------------------------

    pub fn query(&self, q: &Value) -> Value {
        match q["q"].as_str().unwrap_or("state") {
            "saves" => json!({"slots": self.slots()}),
            "facts" => json!({"facts": self.facts, "camera": self.st.rig.current, "cameras": self.st.cameras, "shown": self.st.shown}),
            "backlog" => json!(self.st.progress.backlog),
            "validate" => {
                let errors = std::fs::read_to_string(&self.cfg.story)
                    .map_err(|e| vec![e.to_string()])
                    .and_then(|t| serde_json::from_str::<Story>(&t).map_err(|e| vec![e.to_string()]))
                    .map(|s| s.validate())
                    .unwrap_or_else(|e| e);
                json!({"errors": errors})
            }
            _ => {
                let line = self.line();
                let act = self.story.acts.get(self.st.progress.act);
                json!({
                    "phase": match self.st.phase { Phase::Start => "start", Phase::Loading{..} => "loading", Phase::Title{..} => "title", Phase::Line => "line", Phase::Ended => "ended" },
                    "screen": self.st.screen,
                    "act": act.map(|a| a.id.clone()),
                    "line": line.map(|l| l.id.clone()),
                    "who": line.map(|l| l.who.clone()),
                    "shown": self.st.typing.shown,
                    "total": self.st.typing.total,
                    "revealed": self.st.typing.revealed,
                    "camera_moving": self.st.rig.moving(),
                    "voice": self.st.voice,
                    "auto": self.st.auto,
                    "skip": self.skipping(),
                    "items": self.st.progress.items,
                    "flags": self.st.progress.flags,
                    "portrait": self.st.portrait.shown,
                    "pending": self.st.pending.len(),
                    "time": self.st.time,
                })
            }
        }
    }

    pub fn export(&self) -> Value {
        json!({"schema": 1, "story_hash": self.story_hash, "state": self.st})
    }

    pub fn import(&mut self, v: &Value) -> Result<(), String> {
        if v["schema"].as_i64() != Some(1) {
            return Err("unknown export schema".into());
        }
        let st: State = serde_json::from_value(v["state"].clone()).map_err(|e| e.to_string())?;
        self.st = st;
        if self.story.line(self.st.progress.at()).is_none() && !matches!(self.st.phase, Phase::Ended | Phase::Start) {
            let at = self.story.start();
            self.begin(at, false, false);
        }
        Ok(())
    }
}
