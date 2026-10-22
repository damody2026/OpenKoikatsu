//! The story file (.okstory.json, schema 1): its data, validation and the inheritance along an act's lines.
use crate::math::{Quat, V3};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

pub const SCHEMA: u32 = 1;

/// Text in the story: one string for every language, a string per language, or a localization id with arguments.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum Text {
    Plain(String),
    Lang(BTreeMap<String, String>),
}

impl Default for Text {
    fn default() -> Self {
        Text::Plain(String::new())
    }
}

impl Text {
    /// The words in `lang` (its base language when the region variant is missing, then zh, ja, en, anything).
    pub fn get(&self, lang: &str) -> String {
        match self {
            Text::Plain(s) => s.clone(),
            Text::Lang(m) => {
                let base = lang.split(['-', '_']).next().unwrap_or(lang);
                for key in [lang, base, "zh", "ja", "en"] {
                    if let Some(s) = m.get(key) {
                        return s.clone();
                    }
                }
                m.values().next().cloned().unwrap_or_default()
            }
        }
    }
    pub fn is_empty(&self) -> bool {
        match self {
            Text::Plain(s) => s.is_empty(),
            Text::Lang(m) => m.values().all(|s| s.is_empty()),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct PortraitAdjust {
    /// Percent of the frame (-100..100).
    pub offset_x: f64,
    pub offset_y: f64,
    /// Zoom (0.25..3).
    pub scale: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct VoiceDef {
    /// ok-tts voice (a personality number, or "card": the card's); lines without a voice file are spoken with it.
    pub tts: Option<String>,
    pub pitch: Option<f64>,
}

/// A Studio animation (OK animation library, studio_anims.json): kind 0 Studio motion, 1 H motion.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct Motion {
    #[serde(default)]
    pub kind: i32,
    pub group: i32,
    pub category: i32,
    pub no: i32,
}

/// Written as {"group":..,"category":..,"no":..[,"kind":..]} or "group:category:no" / "kind:group:category:no".
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum MotionSpec {
    Code(String),
    Full(Motion),
}

impl MotionSpec {
    pub fn resolve(&self) -> Result<Motion, String> {
        match self {
            MotionSpec::Full(m) => Ok(*m),
            MotionSpec::Code(s) => {
                let bad = || format!("motion \"{s}\" is not group:category:no or kind:group:category:no");
                let parts: Vec<i32> =
                    s.split(':').map(|p| p.trim().parse::<i32>()).collect::<Result<_, _>>().map_err(|_| bad())?;
                match parts.as_slice() {
                    [g, c, n] => Ok(Motion { kind: 0, group: *g, category: *c, no: *n }),
                    [k, g, c, n] => Ok(Motion { kind: *k, group: *g, category: *c, no: *n }),
                    _ => Err(bad()),
                }
            }
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Offstage {
    pub motion: Option<MotionSpec>,
    pub face: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct CharDef {
    /// OK card or KKS card (absolute, or relative to the project).
    pub card: String,
    pub outfit: i32,
    pub name: Text,
    /// Name plate colour, #rrggbb.
    pub color: String,
    pub portrait: PortraitAdjust,
    pub voice: VoiceDef,
    /// The game's personality (expression table profile); absent: the one Unreal reports for the card.
    pub personality: Option<i32>,
    pub offstage: Offstage,
    /// The scene object already showing this character (absent: matched by card, else added).
    pub scene_object: Option<i32>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct ItemDef {
    pub name: Text,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ItemCount {
    pub item: String,
    #[serde(default = "one")]
    pub n: i64,
}

fn one() -> i64 {
    1
}

/// A camera: a fixed pose (rotation, or a point to look at), or a framing worked out from the cast ("shot").
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct CameraSpec {
    pub pos: Option<V3>,
    /// FQuat x, y, z, w.
    pub rot: Option<[f64; 4]>,
    pub look_at: Option<V3>,
    pub fov: Option<f64>,
    /// "speaker", "two", "wide", "studio" (the scene's active camera) or "studio:<object id>".
    pub shot: Option<String>,
    /// Shots: who to frame instead of the speaker.
    pub on: Option<String>,
    /// Smooth move from the previous camera (default: within an act).
    pub cut: Option<bool>,
}

impl CameraSpec {
    pub fn fixed_rotation(&self) -> Option<Quat> {
        if let Some(r) = self.rot {
            return Some(Quat::from_array(r));
        }
        match (self.pos, self.look_at) {
            (Some(p), Some(t)) => Some(Quat::look(crate::math::sub(t, p), [0.0, 0.0, 1.0])),
            _ => None,
        }
    }
}

/// One cast position: who stands there and how. On a line, only the fields given change.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct CastPatch {
    pub who: Option<String>,
    /// cm in the scene.
    pub pos: Option<V3>,
    /// Degrees about Z.
    pub yaw: Option<f64>,
    pub scale: Option<f64>,
    pub motion: Option<MotionSpec>,
    pub face: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Cond {
    /// Flag values the line needs (equal).
    pub flags: BTreeMap<String, i64>,
    /// Items it needs (at least).
    pub items: Vec<ItemCount>,
}

impl Cond {
    pub fn is_empty(&self) -> bool {
        self.flags.is_empty() && self.items.is_empty()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Choice {
    pub text: Text,
    /// An act id (its first line) or a line id.
    pub goto: String,
    pub need: Vec<ItemCount>,
    /// Take the needed items when chosen.
    pub consume: bool,
    pub give: Vec<ItemCount>,
    pub set: BTreeMap<String, i64>,
    /// Shown only when these hold.
    pub when: Cond,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Line {
    pub id: String,
    /// Character id; empty: narration.
    pub who: String,
    pub text: Text,
    /// Voice file (relative to the story file, or absolute).
    pub voice: Option<String>,
    pub se: Option<String>,
    /// The speaker's expression (name in the game's table, or an alias from faces.json).
    pub face: Option<String>,
    pub motion: Option<MotionSpec>,
    pub camera: Option<CameraSpec>,
    pub cast: BTreeMap<String, Option<CastPatch>>,
    /// "camera", "none", "auto" or a character id: where the speaker looks (default: the camera).
    pub look: Option<String>,
    pub choices: Vec<Choice>,
    pub give: Vec<ItemCount>,
    pub take: Vec<ItemCount>,
    pub set: BTreeMap<String, i64>,
    /// Played only when these hold (skipped otherwise).
    pub when: Cond,
    /// Where to go after this line (default: the next line).
    pub goto: Option<String>,
    /// Name shown instead of the character's ("？？？").
    pub name: Option<Text>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Act {
    pub id: String,
    pub title: Option<Text>,
    /// .okscene (relative to the story file, the project, or absolute).
    pub scene: String,
    pub camera: Option<CameraSpec>,
    pub cast: BTreeMap<String, CastPatch>,
    pub lines: Vec<Line>,
    /// The act after the last line (default: the next act; "" ends the story).
    pub next: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Story {
    pub schema: u32,
    pub title: Text,
    pub characters: BTreeMap<String, CharDef>,
    pub items: BTreeMap<String, ItemDef>,
    pub acts: Vec<Act>,
    /// Act the story starts with (default: the first).
    pub start: Option<String>,
}

/// Where a goto leads.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct At {
    pub act: usize,
    pub line: usize,
}

impl Story {
    pub fn parse(text: &str) -> Result<Story, String> {
        let story: Story = serde_json::from_str(text).map_err(|e| format!("story JSON: {e}"))?;
        let errors = story.validate();
        if !errors.is_empty() {
            return Err(errors.join("\n"));
        }
        Ok(story)
    }

    pub fn act_index(&self, id: &str) -> Option<usize> {
        self.acts.iter().position(|a| a.id == id)
    }

    /// An act id (its first line) or a line id.
    pub fn target(&self, id: &str) -> Option<At> {
        if let Some(a) = self.act_index(id) {
            return Some(At { act: a, line: 0 });
        }
        for (a, act) in self.acts.iter().enumerate() {
            if let Some(l) = act.lines.iter().position(|l| l.id == id) {
                return Some(At { act: a, line: l });
            }
        }
        None
    }

    pub fn start(&self) -> At {
        self.start.as_deref().and_then(|s| self.target(s)).unwrap_or(At { act: 0, line: 0 })
    }

    pub fn line(&self, at: At) -> Option<&Line> {
        self.acts.get(at.act).and_then(|a| a.lines.get(at.line))
    }

    /// Every error with the path of what is wrong (empty: valid).
    pub fn validate(&self) -> Vec<String> {
        let mut e = Vec::new();
        if self.schema != SCHEMA {
            e.push(format!("schema: {} (expected {SCHEMA})", self.schema));
        }
        if self.acts.is_empty() {
            e.push("acts: the story has no act".into());
        }
        let mut ids = BTreeSet::new();
        for (i, a) in self.acts.iter().enumerate() {
            if a.id.is_empty() {
                e.push(format!("acts[{i}].id: empty"));
            } else if !ids.insert(a.id.clone()) {
                e.push(format!("acts[{i}].id: \"{}\" is used twice", a.id));
            }
            for (j, l) in a.lines.iter().enumerate() {
                if l.id.is_empty() {
                    e.push(format!("acts[{i}].lines[{j}].id: empty"));
                } else if !ids.insert(l.id.clone()) {
                    e.push(format!("acts[{i}].lines[{j}].id: \"{}\" is used twice", l.id));
                }
            }
        }
        let who = |e: &mut Vec<String>, path: String, id: &str| {
            if !id.is_empty() && !self.characters.contains_key(id) {
                e.push(format!("{path}: unknown character \"{id}\""));
            }
        };
        let items = |e: &mut Vec<String>, path: String, list: &[ItemCount]| {
            for (k, c) in list.iter().enumerate() {
                if !self.items.contains_key(&c.item) {
                    e.push(format!("{path}[{k}].item: unknown item \"{}\"", c.item));
                }
                if c.n < 1 {
                    e.push(format!("{path}[{k}].n: must be at least 1"));
                }
            }
        };
        let goto = |e: &mut Vec<String>, path: String, id: &str| {
            if self.target(id).is_none() {
                e.push(format!("{path}: no act or line \"{id}\""));
            }
        };
        let motion = |e: &mut Vec<String>, path: String, m: &Option<MotionSpec>| {
            if let Some(m) = m {
                if let Err(err) = m.resolve() {
                    e.push(format!("{path}: {err}"));
                }
            }
        };
        let camera = |e: &mut Vec<String>, path: String, c: &Option<CameraSpec>| {
            let Some(c) = c else { return };
            if let Some(shot) = &c.shot {
                let ok = matches!(shot.as_str(), "speaker" | "two" | "wide" | "studio")
                    || shot.strip_prefix("studio:").is_some_and(|n| n.parse::<i32>().is_ok());
                if !ok {
                    e.push(format!("{path}.shot: \"{shot}\" is not speaker, two, wide, studio or studio:<id>"));
                }
            } else if c.pos.is_none() || c.fixed_rotation().is_none() {
                e.push(format!("{path}: a fixed camera needs pos and rot (or look_at), or use shot"));
            }
            if let Some(f) = c.fov {
                if !(5.0..=170.0).contains(&f) {
                    e.push(format!("{path}.fov: {f} is outside 5..170"));
                }
            }
            if let Some(on) = &c.on {
                if !self.characters.contains_key(on) {
                    e.push(format!("{path}.on: unknown character \"{on}\""));
                }
            }
        };
        for (id, c) in &self.characters {
            if c.card.is_empty() {
                e.push(format!("characters.{id}.card: empty"));
            }
            if !(0..=6).contains(&c.outfit) {
                e.push(format!("characters.{id}.outfit: {} is outside 0..6", c.outfit));
            }
            motion(&mut e, format!("characters.{id}.offstage.motion"), &c.offstage.motion);
        }
        if let Some(s) = &self.start {
            goto(&mut e, "start".into(), s);
        }
        for (i, a) in self.acts.iter().enumerate() {
            let p = format!("acts[{i}]");
            if a.scene.is_empty() {
                e.push(format!("{p}.scene: empty"));
            }
            camera(&mut e, format!("{p}.camera"), &a.camera);
            if let Some(n) = &a.next {
                if !n.is_empty() {
                    goto(&mut e, format!("{p}.next"), n);
                }
            }
            // The cast as the lines change it: nobody may stand at two positions.
            let mut cast: BTreeMap<String, String> = BTreeMap::new();
            for (slot, c) in &a.cast {
                match &c.who {
                    Some(w) => {
                        who(&mut e, format!("{p}.cast.{slot}.who"), w);
                        cast.insert(slot.clone(), w.clone());
                    }
                    None => e.push(format!("{p}.cast.{slot}.who: missing")),
                }
                motion(&mut e, format!("{p}.cast.{slot}.motion"), &c.motion);
            }
            let twice = |e: &mut Vec<String>, path: String, cast: &BTreeMap<String, String>| {
                let mut seen = BTreeSet::new();
                for w in cast.values() {
                    if !seen.insert(w) {
                        e.push(format!("{path}: \"{w}\" stands at two cast positions"));
                    }
                }
            };
            twice(&mut e, format!("{p}.cast"), &cast);
            for (j, l) in a.lines.iter().enumerate() {
                let lp = format!("{p}.lines[{j}]");
                who(&mut e, format!("{lp}.who"), &l.who);
                motion(&mut e, format!("{lp}.motion"), &l.motion);
                camera(&mut e, format!("{lp}.camera"), &l.camera);
                if l.who.is_empty() && (l.face.is_some() || l.motion.is_some()) {
                    e.push(format!("{lp}: face and motion need a speaker (who)"));
                }
                if let Some(look) = &l.look {
                    if !matches!(look.as_str(), "camera" | "none" | "auto") {
                        who(&mut e, format!("{lp}.look"), look);
                    }
                }
                for (slot, c) in &l.cast {
                    match c {
                        None => {
                            cast.remove(slot);
                        }
                        Some(c) => {
                            match (&c.who, cast.contains_key(slot)) {
                                (Some(w), _) => {
                                    who(&mut e, format!("{lp}.cast.{slot}.who"), w);
                                    cast.insert(slot.clone(), w.clone());
                                }
                                (None, false) => e.push(format!("{lp}.cast.{slot}.who: a new cast position needs who")),
                                (None, true) => {}
                            }
                            motion(&mut e, format!("{lp}.cast.{slot}.motion"), &c.motion);
                        }
                    }
                }
                twice(&mut e, format!("{lp}.cast"), &cast);
                items(&mut e, format!("{lp}.give"), &l.give);
                items(&mut e, format!("{lp}.take"), &l.take);
                items(&mut e, format!("{lp}.when.items"), &l.when.items);
                if let Some(g) = &l.goto {
                    goto(&mut e, format!("{lp}.goto"), g);
                }
                for (k, c) in l.choices.iter().enumerate() {
                    let cp = format!("{lp}.choices[{k}]");
                    goto(&mut e, format!("{cp}.goto"), &c.goto);
                    items(&mut e, format!("{cp}.need"), &c.need);
                    items(&mut e, format!("{cp}.give"), &c.give);
                    items(&mut e, format!("{cp}.when.items"), &c.when.items);
                    if c.text.is_empty() {
                        e.push(format!("{cp}.text: empty"));
                    }
                }
                if !l.choices.is_empty() && l.goto.is_some() {
                    e.push(format!("{lp}: a line with choices cannot also have goto"));
                }
            }
        }
        e
    }
}

/// One cast position as it stands at a line.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Placed {
    pub who: String,
    /// None: where the scene has the character.
    pub pos: Option<V3>,
    pub yaw: Option<f64>,
    pub scale: Option<f64>,
    pub motion: Option<Motion>,
    pub face: Option<String>,
}

fn apply(slot: Option<&Placed>, p: &CastPatch) -> Option<Placed> {
    let who = p.who.clone().or_else(|| slot.map(|s| s.who.clone()))?;
    let same = slot.is_some_and(|s| s.who == who);
    let base = if same { slot.cloned() } else { None };
    Some(Placed {
        who,
        pos: p.pos.or(base.as_ref().and_then(|b| b.pos)),
        yaw: p.yaw.or(base.as_ref().and_then(|b| b.yaw)),
        scale: p.scale.or(base.as_ref().and_then(|b| b.scale)),
        motion: p.motion.as_ref().and_then(|m| m.resolve().ok()).or(base.as_ref().and_then(|b| b.motion)),
        face: p.face.clone().or(base.and_then(|b| b.face)),
    })
}

/// The cast at a line of an act: the act's, changed by every line up to and including it (a speaker's face and
/// motion stay on them afterwards).
pub fn cast_at(act: &Act, line: usize) -> BTreeMap<String, Placed> {
    let mut cast: BTreeMap<String, Placed> = BTreeMap::new();
    for (slot, p) in &act.cast {
        if let Some(c) = apply(None, p) {
            cast.insert(slot.clone(), c);
        }
    }
    for l in act.lines.iter().take(line + 1) {
        for (slot, p) in &l.cast {
            match p {
                None => {
                    cast.remove(slot);
                }
                Some(p) => {
                    if let Some(c) = apply(cast.get(slot), p) {
                        cast.insert(slot.clone(), c);
                    }
                }
            }
        }
        if !l.who.is_empty() {
            if let Some(c) = cast.values_mut().find(|c| c.who == l.who) {
                if let Some(m) = l.motion.as_ref().and_then(|m| m.resolve().ok()) {
                    c.motion = Some(m);
                }
                if let Some(f) = &l.face {
                    c.face = Some(f.clone());
                }
            }
        }
    }
    cast
}

/// The camera a line is seen through: its own, else the nearest earlier line's in the act, else the act's.
pub fn camera_at(act: &Act, line: usize) -> Option<(usize, &CameraSpec)> {
    for j in (0..=line.min(act.lines.len().saturating_sub(1))).rev() {
        if let Some(c) = &act.lines[j].camera {
            return Some((j, c));
        }
    }
    act.camera.as_ref().map(|c| (usize::MAX, c))
}

#[cfg(test)]
mod tests {
    use super::*;

    pub fn sample() -> Story {
        Story::parse(include_str!("../tests/sample.okstory.json")).expect("sample story")
    }

    #[test]
    fn sample_is_valid() {
        let s = sample();
        assert_eq!(s.acts.len(), 3);
    }

    #[test]
    fn errors_carry_paths() {
        let mut s = sample();
        s.acts[0].lines[2].choices[0].goto = "nowhere".into();
        s.acts[0].lines[1].who = "nobody".into();
        s.acts[1].lines[0].id = s.acts[0].lines[0].id.clone();
        let e = s.validate();
        assert!(e.iter().any(|m| m == "acts[0].lines[2].choices[0].goto: no act or line \"nowhere\""), "{e:?}");
        assert!(e.iter().any(|m| m.starts_with("acts[0].lines[1].who: unknown character")), "{e:?}");
        assert!(e.iter().any(|m| m.starts_with("acts[1].lines[0].id:") && m.contains("twice")), "{e:?}");
    }

    #[test]
    fn same_character_twice_is_refused() {
        let mut s = sample();
        let slot = s.acts[0].cast.keys().next().unwrap().clone();
        let who = s.acts[0].cast[&slot].who.clone();
        s.acts[0].lines[0].cast.insert("extra".into(), Some(CastPatch { who, ..Default::default() }));
        assert!(s.validate().iter().any(|m| m.contains("stands at two cast positions")));
    }

    #[test]
    fn cast_and_camera_inherit() {
        let s = sample();
        let a = &s.acts[0];
        let c0 = cast_at(a, 0);
        assert!(c0.values().any(|c| c.who == "ayu"));
        // Line 3 brings rin in; the last line of the act sends her away again.
        let joined = cast_at(a, 3);
        assert!(joined.values().any(|c| c.who == "rin"));
        let left = cast_at(a, a.lines.len() - 1);
        assert!(!left.values().any(|c| c.who == "rin"));
        // ayu's face from line 0 stays.
        assert_eq!(cast_at(a, 1).values().find(|c| c.who == "ayu").unwrap().face.as_deref(), Some("smile"));
        // Line 1 has no camera: line 0's.
        assert_eq!(camera_at(a, 1).map(|(j, _)| j), Some(0));
    }

    #[test]
    fn motion_codes() {
        assert_eq!(MotionSpec::Code("1:2:3".into()).resolve().unwrap(), Motion { kind: 0, group: 1, category: 2, no: 3 });
        assert_eq!(MotionSpec::Code("1:0:2:3".into()).resolve().unwrap(), Motion { kind: 1, group: 0, category: 2, no: 3 });
        assert!(MotionSpec::Code("x".into()).resolve().is_err());
    }
}
