//! The live presenter: galgame presentation for conversations whose words come from elsewhere (the street's rules,
//! its language model, recorded lines). The game says who speaks what (`line`), who is in the conversation
//! (`conversation`) and when it ends; this decides how it is shown: the typewriter (following the voice), the
//! speaker's portrait, and a shot per line chosen from the conversation's people with a clear view (probes).
use crate::camera::{self, Candidate, Rig, ShotContext};
use crate::director::ProbeJob;
use crate::io::*;
use crate::math::{self, Pose};
use crate::portrait::Portrait;
use crate::script::{CameraSpec, PortraitAdjust};
use crate::text::Typing;
use crate::tuning::Tuning;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct LiveConfig {
    /// Content/IndependentResources/Story (tuning.json).
    pub data_dir: Option<PathBuf>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum LiveInput {
    /// A conversation starts (or its people change): the player and the others, by the names the game uses.
    Conversation { player: String, people: Vec<String> },
    /// It ends: no dialog box, no portrait, the game's own camera again.
    End,
    /// One line on screen (replacing the last). `voiced`: a voice will play it (its progress comes in the facts).
    Line {
        who: String,
        #[serde(default)]
        name: String,
        #[serde(default)]
        color: String,
        text: String,
        #[serde(default)]
        voiced: bool,
    },
    /// The whole line at once (a click).
    Reveal,
    /// The pointer, 0..1 over the view (the shots' parallax).
    Mouse { x: f64, y: f64 },
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct LiveLine {
    pub id: u64,
    pub who: String,
    pub name: String,
    pub color: String,
    pub voiced: bool,
    pub age: f64,
    pub done_at: Option<f64>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct LiveState {
    pub time: f64,
    pub active: bool,
    pub player: String,
    pub people: Vec<String>,
    pub line: Option<LiveLine>,
    pub lines: u64,
    pub typing: Typing,
    pub rig: Rig,
    pub camera_set: bool,
    /// The line's shot has a pose. Until it has (the game had not sent the two of them yet when the first line came),
    /// no camera goes out and the game's own view stays: the first line of every conversation stood frozen where the
    /// street camera was when it began, metres from both of them.
    pub framed: bool,
    pub portrait: Portrait,
    pub probe: Option<ProbeJob>,
    pub next_id: u64,
    /// The shot of the line on screen (re-framed while its people move).
    pub shot: Option<CameraSpec>,
    pub reframe_for: f64,
    /// A first-person shot's way out while its probes are out: the usual shot of the line, taken when something stands
    /// between his eyes and her (a table lamp, a shelf) and one of its views is clear.
    pub pov_fallback: Option<CameraSpec>,
}

pub struct Live {
    pub tuning: Tuning,
    pub st: LiveState,
    inbox: Vec<LiveInput>,
    probes_out: Vec<Probe>,
    facts: Facts,
}

impl Live {
    pub fn open(cfg: LiveConfig) -> Result<Live, String> {
        let tuning = Tuning::load(cfg.data_dir.as_deref())?;
        Ok(Live { tuning, st: LiveState { next_id: 1, ..Default::default() }, inbox: Vec::new(), probes_out: Vec::new(), facts: Facts::default() })
    }

    pub fn input(&mut self, i: LiveInput) {
        self.inbox.push(i);
    }

    fn present(&self) -> Vec<String> {
        let mut p = self.st.people.clone();
        if !self.st.player.is_empty() {
            p.push(self.st.player.clone());
        }
        p
    }

    fn candidates(&self, spec: &CameraSpec, speaker: &str) -> Vec<Candidate> {
        let present = self.present();
        let ctx = ShotContext {
            speaker,
            present: present.iter().map(String::as_str).collect(),
            actors: &self.facts.actors,
            cameras: &[],
            active_camera: None,
            aspect: self.facts.view_aspect.filter(|a| *a > 0.0).unwrap_or(if self.facts.viewport[1] > 0.0 { self.facts.viewport[0] / self.facts.viewport[1] } else { 16.0 / 9.0 }),
        };
        let mut out = camera::candidates(&self.tuning.camera, spec, &ctx, &self.st.rig.current);
        // A close-up never looks through the other person: views passing near their head or chest go last.
        if spec.shot.as_deref() == Some("speaker") {
            let subject = spec.on.as_deref().unwrap_or(speaker);
            let clear = self.tuning.camera.over_clear;
            let blocked = |c: &Candidate| {
                present.iter().filter(|w| w.as_str() != subject).filter_map(|w| self.facts.actors.get(w)).any(|a| {
                    let k = a.root.scale.abs().max(0.1);
                    let chest = a.bone("spine03").map(|b| b.pos).unwrap_or_else(|| math::add(a.root.pos, [0.0, 0.0, 120.0 * k]));
                    [a.head(), chest].iter().any(|p| point_segment(*p, c.from, c.pose.pos) < clear * k)
                })
            };
            let (open, behind): (Vec<Candidate>, Vec<Candidate>) = out.into_iter().partition(|c| !blocked(c));
            out = open;
            out.extend(behind);
        }
        out
    }

    /// The shot for a line: through his eyes onto her (`pov`); or her close-up and a two shot in turn, over his
    /// shoulder onto her when he speaks.
    fn shot_for(&self, who: &str) -> CameraSpec {
        let partner = self.st.people.first().cloned().unwrap_or_default();
        if self.tuning.live.pov && !self.st.player.is_empty() && !partner.is_empty() {
            // A girl outside the conversation who joins in (a group talk) is looked at too while the game reports her.
            let on = if who == self.st.player || !self.facts.actors.contains_key(who) { partner } else { who.to_string() };
            return CameraSpec { shot: Some("pov".into()), on: Some(on), ..Default::default() };
        }
        self.usual_shot(who)
    }

    /// Her close-up and a two shot in turn, over his shoulder onto her when he speaks.
    fn usual_shot(&self, who: &str) -> CameraSpec {
        let partner = self.st.people.first().cloned().unwrap_or_default();
        if who == self.st.player && !partner.is_empty() {
            CameraSpec { shot: Some("over".into()), on: Some(partner), ..Default::default() }
        } else if self.st.lines % 2 == 1 && self.st.people.len() + usize::from(!self.st.player.is_empty()) >= 2 {
            CameraSpec { shot: Some("two".into()), on: Some(who.to_string()), ..Default::default() }
        } else {
            CameraSpec { shot: Some("speaker".into()), on: Some(who.to_string()), ..Default::default() }
        }
    }

    /// His eyes (from his head bone, a little ahead toward her and up) looking at her, between her head and chest.
    fn pov(&self, on: &str) -> Option<math::Pose> {
        let me = self.facts.actors.get(&self.st.player)?;
        let her = self.facts.actors.get(on)?;
        let k = me.root.scale.abs().max(0.1);
        let head = me.head();
        let face = her.head();
        let chest = her.bone("spine03").map(|b| b.pos).unwrap_or_else(|| math::add(her.root.pos, [0.0, 0.0, 120.0 * her.root.scale.abs().max(0.1)]));
        let aim = math::lerp3(face, chest, self.tuning.live.pov_aim.clamp(0.0, 1.0));
        let flat = math::normalize([face[0] - head[0], face[1] - head[1], 0.0]);
        let eye = math::add(math::add(head, math::scale(flat, self.tuning.live.pov_forward * k)), [0.0, 0.0, self.tuning.live.pov_up * k]);
        if !math::finite3(eye) || !math::finite3(aim) || math::len(math::sub(aim, eye)) < 1.0 {
            return None;
        }
        Some(math::Pose { pos: eye, rot: math::Quat::look(math::sub(aim, eye), [0.0, 0.0, 1.0]), fov: self.tuning.live.pov_fov })
    }

    /// Not framed yet: the move starts from where the game's view is now (it kept following them meanwhile), not from
    /// where it was when the conversation began.
    fn start_from_view(&mut self) {
        if self.st.framed {
            return;
        }
        if let Some(v) = &self.facts.view {
            self.st.rig.current = Pose { pos: v.pos, rot: v.q(), fov: self.facts.view_fov.unwrap_or(65.0) };
            self.st.rig.travel = None;
        }
    }

    fn frame_shot(&mut self, cut: bool) {
        let Some(spec) = self.st.shot.clone() else { return };
        self.st.pov_fallback = None;
        let speaker = self.st.line.as_ref().map(|l| l.who.clone()).unwrap_or_default();
        // First person: his eyes onto her. Something can stand between them (a table lamp, a shelf; they talk across
        // furniture): her face to his eyes is probed with the usual shot's views after it, and the first clear one is
        // taken (probe_answers), the usual shot then staying for the line.
        if spec.shot.as_deref() == Some("pov") {
            let partner = self.st.people.first().cloned().unwrap_or_default();
            let on = spec.on.clone().filter(|on| self.pov(on).is_some()).unwrap_or(partner);
            let Some(pose) = self.pov(&on) else { return };
            self.start_from_view();
            self.st.rig.go(&self.tuning.camera, pose, cut || !self.st.camera_set, 1.0);
            self.st.camera_set = true;
            self.st.framed = true;
            self.st.probe = None;
            let Some(face) = self.facts.actors.get(&on).map(|a| a.head()) else { return };
            let usual = self.usual_shot(if speaker.is_empty() { &on } else { &speaker });
            let mut candidates = vec![Candidate { pose, from: face }];
            candidates.extend(self.candidates(&usual, &speaker));
            self.st.pov_fallback = Some(usual);
            self.ask_probes(candidates, cut);
            return;
        }
        let candidates = self.candidates(&spec, &speaker);
        let Some(first) = candidates.first().copied() else { return };
        self.start_from_view();
        self.st.rig.go(&self.tuning.camera, first.pose, cut || !self.st.camera_set, 1.0);
        self.st.camera_set = true;
        self.st.framed = true;
        self.st.probe = None;
        self.ask_probes(candidates, cut);
    }

    /// Probes for every candidate of a shot (the first one is already on its way); probe_answers picks among them.
    fn ask_probes(&mut self, candidates: Vec<Candidate>, cut: bool) {
        if candidates.len() > 1 {
            let mut ids = Vec::new();
            for c in &candidates {
                let id = self.st.next_id;
                self.st.next_id += 1;
                // From a little way out: whoever stands face to face with the subject (the other half of the
                // conversation, drawn by proxies a capture cannot hide) does not count as a wall.
                let to = c.pose.pos;
                let span = math::len(math::sub(to, c.from));
                let skip = self.tuning.camera.probe_skip.min((span - self.tuning.camera.probe_margin * 2.0).max(0.0));
                let from = math::add(c.from, math::scale(math::normalize(math::sub(to, c.from)), skip));
                self.probes_out.push(Probe { id, from, to });
                ids.push(id);
            }
            self.st.probe = Some(ProbeJob { ids, candidates, cut, chosen: None, asked: self.st.time });
        }
    }

    fn probe_answers(&mut self) {
        let margin = self.tuning.camera.probe_margin;
        let Some(job) = self.st.probe.as_mut().filter(|j| j.chosen.is_none()) else { return };
        let clear: Vec<Option<f64>> = job.ids.iter().map(|id| self.facts.probes.iter().find(|p| p.id == *id).map(|p| p.clear)).collect();
        if clear.iter().any(Option::is_none) {
            if self.st.time - job.asked > 1.0 {
                job.chosen = Some(0);
            }
            return;
        }
        let skip = self.tuning.camera.probe_skip;
        let reach: Vec<(f64, f64)> = job
            .candidates
            .iter()
            .zip(&clear)
            .map(|(c, r)| {
                let span = math::len(math::sub(c.pose.pos, c.from));
                (r.unwrap_or(0.0), span - skip.min((span - margin * 2.0).max(0.0)))
            })
            .collect();
        let pick = reach.iter().position(|(c, d)| *c >= d - margin).unwrap_or_else(|| {
            (0..reach.len()).max_by(|a, b| (reach[*a].0 / reach[*a].1.max(1.0)).total_cmp(&(reach[*b].0 / reach[*b].1.max(1.0)))).unwrap_or(0)
        });
        job.chosen = Some(pick);
        let (mut pose, cut) = (job.candidates[pick].pose, job.cut);
        // A first-person view that is not clear: the usual shot of the line takes over, its own candidates and the
        // chosen one in the job (re-framing follows that shot from now on).
        let fallback = if pick != 0 { self.st.pov_fallback.take() } else { None };
        if fallback.is_some() {
            job.candidates.remove(0);
            job.ids.remove(0);
            job.chosen = Some(pick - 1);
        }
        self.st.pov_fallback = None;
        if let Some(usual) = fallback {
            self.st.shot = Some(usual);
        }
        // None of them has a clear view (a narrow restaurant floor): the best one pulled in along its line to where the
        // view is clear, as a spring arm does. Left where it was, the shot looked at the wall in front of them.
        let (clear, need) = reach[pick];
        let pulled = clear < need - margin;
        if pulled {
            let c = &job.candidates[pick];
            let span = math::len(math::sub(c.pose.pos, c.from));
            let dist = ((span - need) + clear - margin).max(self.tuning.camera.probe_margin * 3.0);
            pose.pos = math::add(c.from, math::scale(math::normalize(math::sub(c.pose.pos, c.from)), dist));
        }
        if pick != 0 || pulled {
            if self.st.rig.moving() {
                self.st.rig.retarget(pose);
            } else {
                self.st.rig.go(&self.tuning.camera, pose, cut, 1.0);
            }
        }
    }

    fn handle(&mut self, i: LiveInput) {
        match i {
            LiveInput::Conversation { player, people } => {
                let starting = !self.st.active;
                self.st.active = true;
                self.st.player = player;
                self.st.people = people;
                if starting {
                    // From the game's own view, so the first shot moves there smoothly.
                    if let Some(v) = &self.facts.view {
                        self.st.rig.current = Pose { pos: v.pos, rot: v.q(), fov: self.facts.view_fov.unwrap_or(65.0) };
                        self.st.camera_set = true;
                    }
                    self.st.lines = 0;
                }
            }
            LiveInput::End => {
                self.st.active = false;
                self.st.framed = false;
                self.st.line = None;
                self.st.shot = None;
                self.st.probe = None;
                self.st.camera_set = false;
                self.st.rig.travel = None;
                self.st.portrait.want("");
            }
            LiveInput::Line { who, name, color, text, voiced } => {
                let id = self.st.next_id;
                self.st.next_id += 1;
                self.st.typing = Typing::new(&text);
                self.st.line = Some(LiveLine { id, who: who.clone(), name, color, voiced, age: 0.0, done_at: None });
                let show_portrait = who != self.st.player || self.tuning.live.player_portrait;
                self.st.portrait.want(if show_portrait { &who } else { "" });
                if self.st.active {
                    self.st.shot = Some(self.shot_for(&who));
                    self.st.lines += 1;
                    self.st.reframe_for = 0.8;
                    self.frame_shot(false);
                }
            }
            LiveInput::Reveal => self.st.typing.reveal(),
            LiveInput::Mouse { x, y } => {
                self.st.rig.pointer = [(x * 2.0 - 1.0).clamp(-1.0, 1.0), (1.0 - y * 2.0).clamp(-1.0, 1.0)];
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
        self.probe_answers();
        // The typewriter: the voice's progress when it plays, else the reading speed.
        let voice = self.facts.voice.clone();
        let mut following = false;
        if let Some(line) = &mut self.st.line {
            line.age += dt;
            // The game reports the speaker's voice without ids (0) or by the line id.
            if line.voiced && (voice.id == 0 || voice.id == line.id) {
                if voice.done {
                    self.st.typing.reveal();
                } else if voice.progress >= 0.0 && voice.playing {
                    self.st.typing.follow(voice.progress);
                    following = true;
                }
            }
            if !following {
                self.st.typing.type_for(&self.tuning.text, dt, self.tuning.live.text_speed);
            }
            if self.st.typing.revealed && line.done_at.is_none() {
                line.done_at = Some(self.st.time);
            }
        }
        if self.st.active && !self.st.framed && self.st.shot.is_some() {
            self.frame_shot(false);
        }
        // A shot follows its people for a moment (they turn to face each other as the line starts).
        if self.st.reframe_for > 0.0 && self.st.active && self.st.framed && self.st.shot.as_ref().and_then(|s| s.shot.as_deref()) == Some("pov") {
            self.st.reframe_for -= dt;
            let spec = self.st.shot.clone().unwrap_or_default();
            let partner = self.st.people.first().cloned().unwrap_or_default();
            if let Some(pose) = spec.on.as_deref().and_then(|on| self.pov(on)).or_else(|| self.pov(&partner)) {
                self.st.rig.retarget(pose);
            }
        } else if self.st.reframe_for > 0.0 && self.st.active {
            self.st.reframe_for -= dt;
            if let Some(spec) = self.st.shot.clone() {
                let speaker = self.st.line.as_ref().map(|l| l.who.clone()).unwrap_or_default();
                let candidates = self.candidates(&spec, &speaker);
                if !candidates.is_empty() {
                    if !self.st.camera_set {
                        self.frame_shot(true);
                    } else {
                        let pick = self.st.probe.as_ref().and_then(|j| j.chosen).unwrap_or(0).min(candidates.len() - 1);
                        self.st.rig.retarget(candidates[pick].pose);
                    }
                }
            }
        }
        self.st.rig.tick(&self.tuning.camera, dt, false);
        self.st.portrait.tick(&self.tuning.portrait, dt);
        self.frame()
    }

    fn frame(&mut self) -> Frame {
        let mut f = Frame { time: self.st.time, screen: "play".into(), layout: self.tuning.layout.clone(), ..Default::default() };
        f.layout.dialog_opacity = self.tuning.layout.dialog_opacity;
        if self.st.active && self.st.camera_set && self.st.framed {
            let p = self.st.rig.shown(&self.tuning.camera, self.tuning.live.parallax);
            f.camera = Some(CameraOut { pos: p.pos, rot: p.rot.to_array(), fov: p.fov, moving: self.st.rig.moving() });
        }
        if let Some(line) = &self.st.line {
            f.dialog = DialogOut {
                visible: true,
                name: line.name.clone(),
                name_color: line.color.clone(),
                text_visible: self.st.typing.visible(),
                shown: self.st.typing.shown,
                total: self.st.typing.total,
                waiting: if self.st.typing.revealed { "live".into() } else { "typing".into() },
                opacity: self.tuning.layout.dialog_opacity,
            };
        }
        let adjust = PortraitAdjust::default();
        f.portrait = self.st.portrait.out(&self.tuning.portrait, &self.tuning.layout, &adjust, &self.facts.actors, self.facts.viewport);
        if self.st.line.is_none() {
            f.portrait.visible = false;
        }
        f.probes = std::mem::take(&mut self.probes_out);
        f
    }

    pub fn state(&self) -> serde_json::Value {
        serde_json::json!({
            "active": self.st.active,
            "player": self.st.player,
            "people": self.st.people,
            "who": self.st.line.as_ref().map(|l| l.who.clone()),
            "shown": self.st.typing.shown,
            "total": self.st.typing.total,
            "shot": self.st.shot.as_ref().and_then(|s| s.shot.clone()),
            "camera": self.st.rig.current,
            "camera_moving": self.st.rig.moving(),
            "portrait": self.st.portrait.shown,
        })
    }
}

/// Distance from `p` to the segment `a`–`b`.
fn point_segment(p: math::V3, a: math::V3, b: math::V3) -> f64 {
    let ab = math::sub(b, a);
    let t = (math::dot(math::sub(p, a), ab) / math::dot(ab, ab).max(1e-9)).clamp(0.0, 1.0);
    math::len(math::sub(p, math::add(a, math::scale(ab, t))))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::math::Quat;

    fn actor(pos: [f64; 3], yaw: f64) -> ActorFacts {
        let q = Quat::yaw_deg(yaw);
        let mut a = ActorFacts { ready: true, ..Default::default() };
        a.root = Xf { pos, rot: q.to_array(), scale: 1.0 };
        for (b, z) in [("hips", 90.0), ("spine03", 125.0), ("head", 150.0)] {
            a.bones.insert(b.into(), Xf { pos: [pos[0], pos[1], pos[2] + z], rot: q.to_array(), scale: 1.0 });
        }
        a
    }

    fn facts(dt: f64, progress: f64, id: u64) -> Facts {
        let mut f = Facts { dt, viewport: [1920.0, 1080.0], ..Default::default() };
        // He stands at x=0 facing +x (yaw -90), she at x=120 facing him.
        f.actors.insert("he".into(), actor([0.0, 0.0, 0.0], -90.0));
        f.actors.insert("her1".into(), actor([120.0, 0.0, 0.0], 90.0));
        f.view = Some(Xf { pos: [-300.0, 0.0, 170.0], ..Default::default() });
        if id > 0 {
            f.voice = VoiceFacts { id, playing: progress < 1.0, progress, done: progress >= 1.0 };
        }
        f
    }

    #[test]
    fn lines_shots_and_voice() {
        let mut l = Live::open(LiveConfig::default()).unwrap();
        l.tuning.live.pov = false;
        l.input(LiveInput::Conversation { player: "he".into(), people: vec!["her1".into()] });
        l.input(LiveInput::Line { who: "her1".into(), name: "凜".into(), color: "#ff8".into(), text: "你好，今天天氣很好呢。".into(), voiced: true });
        let f = l.tick(facts(0.016, -1.0, 0));
        let id = l.st.line.as_ref().unwrap().id;
        assert!(f.camera.is_some(), "the presenter owns the camera in a conversation");
        assert_eq!(f.dialog.name, "凜");
        assert_eq!(l.st.shot.as_ref().unwrap().shot.as_deref(), Some("speaker"));
        // Voice at half: half the words.
        let f = l.tick(facts(0.016, 0.5, id));
        assert_eq!(f.dialog.shown, (f.dialog.total as f64 * 0.5).ceil() as usize);
        let f = l.tick(facts(0.016, 1.0, id));
        assert_eq!(f.dialog.shown, f.dialog.total);
        // His line: over his shoulder onto her, the camera behind him (x < 0).
        l.input(LiveInput::Line { who: "he".into(), name: "阿明".into(), color: String::new(), text: "嗯。".into(), voiced: false });
        for _ in 0..300 {
            l.tick(facts(0.016, -1.0, 0));
        }
        assert_eq!(l.st.shot.as_ref().unwrap().shot.as_deref(), Some("over"));
        assert!(l.st.rig.current.pos[0] < 0.0, "{:?}", l.st.rig.current.pos);
        // The conversation ends: the game's camera again.
        l.input(LiveInput::End);
        let f = l.tick(facts(0.016, -1.0, 0));
        assert!(f.camera.is_none() && !f.dialog.visible && !f.portrait.visible);
    }

    #[test]
    fn first_shot_starts_from_the_game_view() {
        let mut l = Live::open(LiveConfig::default()).unwrap();
        l.input(LiveInput::Conversation { player: "he".into(), people: vec!["her1".into()] });
        l.input(LiveInput::Line { who: "her1".into(), name: String::new(), color: String::new(), text: "……".into(), voiced: false });
        let f = l.tick(facts(0.016, -1.0, 0));
        let cam = f.camera.unwrap();
        assert!(cam.moving, "a move from the game's view, not a cut");
        assert!(cam.pos[0] < -200.0, "{:?}", cam.pos);
    }

    #[test]
    fn first_person_through_his_eyes() {
        let mut l = Live::open(LiveConfig::default()).unwrap();
        assert!(l.tuning.live.pov, "first person is the default");
        l.input(LiveInput::Conversation { player: "he".into(), people: vec!["her1".into()] });
        // Her line and his: both seen from his eyes onto her.
        for who in ["her1", "he"] {
            l.input(LiveInput::Line { who: who.into(), name: String::new(), color: String::new(), text: "嗯。".into(), voiced: false });
            for _ in 0..300 {
                l.tick(facts(0.016, -1.0, 0));
            }
            assert_eq!(l.st.shot.as_ref().unwrap().shot.as_deref(), Some("pov"));
            assert_eq!(l.st.shot.as_ref().unwrap().on.as_deref(), Some("her1"));
            let p = l.st.rig.current.pos;
            // His head is at (0, 0, 150): the eyes a little behind its centre (away from her) and up.
            assert!((p[0] + 12.0).abs() < 1.0 && p[1].abs() < 1.0 && (p[2] - 156.0).abs() < 1.0, "{:?}", p);
            let f = l.st.rig.current.rot.forward();
            assert!(f[0] > 0.9, "looks at her: {:?}", f);
        }
    }

    #[test]
    fn first_person_turns_to_the_girl_who_joins_in() {
        let mut l = Live::open(LiveConfig::default()).unwrap();
        l.input(LiveInput::Conversation { player: "he".into(), people: vec!["her1".into()] });
        // A second girl to his left (+y) speaks in a group talk: the view turns to her.
        l.input(LiveInput::Line { who: "her2".into(), name: String::new(), color: String::new(), text: "我也來了！".into(), voiced: false });
        for _ in 0..300 {
            let mut f = facts(0.016, -1.0, 0);
            f.actors.insert("her2".into(), actor([0.0, 120.0, 0.0], 180.0));
            l.tick(f);
        }
        assert_eq!(l.st.shot.as_ref().unwrap().on.as_deref(), Some("her2"));
        let f = l.st.rig.current.rot.forward();
        assert!(f[1] > 0.9, "looks at her2: {:?}", f);
    }

    /// Something between his eyes and her (the first probe, from her face to his eyes, comes back short): the usual
    /// shot of the line takes over; with a clear view first person stays.
    #[test]
    fn first_person_gives_way_when_something_stands_between() {
        for blocked in [true, false] {
            let mut l = Live::open(LiveConfig::default()).unwrap();
            l.input(LiveInput::Conversation { player: "he".into(), people: vec!["her1".into()] });
            l.input(LiveInput::Line { who: "her1".into(), name: String::new(), color: String::new(), text: "嗯。".into(), voiced: false });
            let mut asked: Vec<crate::io::Probe> = Vec::new();
            for _ in 0..5 {
                asked.extend(l.tick(facts(0.016, -1.0, 0)).probes);
                if !asked.is_empty() { break; }
            }
            assert!(asked.len() > 1, "first person asks its own probe and the usual shot's: {}", asked.len());
            let answers: Vec<crate::io::ProbeResult> = asked.iter().enumerate().map(|(i, p)| {
                let span = crate::math::len(crate::math::sub(p.to, p.from));
                crate::io::ProbeResult { id: p.id, clear: if i == 0 && blocked { 5.0 } else { span + 100.0 } }
            }).collect();
            let mut f = facts(0.016, -1.0, 0);
            f.probes = answers;
            l.tick(f);
            for _ in 0..60 { l.tick(facts(0.016, -1.0, 0)); }
            let shot = l.st.shot.as_ref().unwrap().shot.clone();
            if blocked { assert_ne!(shot.as_deref(), Some("pov"), "a blocked first person gives way"); } else { assert_eq!(shot.as_deref(), Some("pov")); }
        }
    }
}
