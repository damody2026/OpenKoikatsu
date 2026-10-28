//! What crosses the C ABI: facts and input from Unreal, the frame and its commands back, command results.
use crate::math::{Quat, V3};
use crate::tuning::Layout;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// A world transform reported by Unreal.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Xf {
    pub pos: V3,
    /// FQuat x, y, z, w.
    pub rot: [f64; 4],
    pub scale: f64,
}

impl Default for Xf {
    fn default() -> Self {
        Xf { pos: [0.0; 3], rot: [0.0, 0.0, 0.0, 1.0], scale: 1.0 }
    }
}

impl Xf {
    pub fn q(&self) -> Quat {
        Quat::from_array(self.rot)
    }
}

/// One story character as Unreal shows it.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct ActorFacts {
    /// Built and posed (its bones are real).
    pub ready: bool,
    pub root: Xf,
    /// "head", "spine03", "spine02", "hips", "eyes" (between the eyes): world transforms.
    pub bones: BTreeMap<String, Xf>,
    /// The card's personality (expression profile).
    pub personality: Option<i32>,
    /// The animation itself closes the eyes / sets the gaze now.
    pub authored_blink: bool,
    pub authored_look: bool,
}

impl ActorFacts {
    pub fn bone(&self, name: &str) -> Option<&Xf> {
        self.bones.get(name)
    }
    /// The head, or a standing guess from the root.
    pub fn head(&self) -> V3 {
        self.bone("head").map(|b| b.pos).unwrap_or_else(|| crate::math::add(self.root.pos, [0.0, 0.0, 150.0 * self.root.scale]))
    }
    pub fn eyes(&self) -> V3 {
        self.bone("eyes").map(|b| b.pos).unwrap_or_else(|| crate::math::add(self.head(), [0.0, 0.0, 6.0 * self.root.scale]))
    }
    /// Horizontal facing (Studio characters face their local +Y).
    pub fn facing(&self) -> V3 {
        let f = self.root.q().rotate([0.0, 1.0, 0.0]);
        crate::math::normalize([f[0], f[1], 0.0])
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct VoiceFacts {
    /// The voice command playing (0: none).
    pub id: u64,
    pub playing: bool,
    /// 0..1 of the clip played (negative: unknown).
    pub progress: f64,
    /// The clip has ended.
    pub done: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Facts {
    /// Seconds since the previous tick.
    pub dt: f64,
    /// Pixels.
    pub viewport: [f64; 2],
    pub actors: BTreeMap<String, ActorFacts>,
    pub voice: VoiceFacts,
    /// The view camera Unreal shows (to look at it).
    pub view: Option<Xf>,
    /// Its horizontal field of view (degrees).
    pub view_fov: Option<f64>,
    /// Width over height of the picture that camera fills, when the story view (`viewport`) covers only part of it.
    pub view_aspect: Option<f64>,
    /// Results of the probes the last frame asked for.
    pub probes: Vec<ProbeResult>,
}

/// How far a probe could see from its start towards its end (cm; past the end: nothing in between).
#[derive(Debug, Clone, Copy, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct ProbeResult {
    pub id: u64,
    pub clear: f64,
}

/// A line of sight to measure: from a point towards another (the scene's characters do not block it).
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Probe {
    pub id: u64,
    pub from: V3,
    pub to: V3,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Input {
    /// Keys by Unreal name (Enter, SpaceBar, Escape, LeftControl, F5, W, ...).
    Key { key: String, #[serde(default = "yes")] down: bool },
    /// A click on the view (not on a button): left / right.
    Click { #[serde(default)] button: String },
    Wheel { delta: f64 },
    /// Pointer position, 0..1 over the view.
    Mouse { x: f64, y: f64 },
    /// Pointer movement in pixels (free camera).
    MouseDelta { dx: f64, dy: f64 },
    Choose { index: usize },
    /// Play from this line (an editor or tool jumping there; its effects are not applied).
    Jump { line: String },
    /// A button of the frame (its id), with a value for settings.
    Button { id: String, #[serde(default)] value: Option<serde_json::Value> },
}

fn yes() -> bool {
    true
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum Op {
    LoadScene { path: String },
    /// Show a story character (adding it to the scene when it is not there yet) or hide it.
    Place {
        who: String,
        card: String,
        outfit: i32,
        scene_object: Option<i32>,
        /// None: keep the scene's.
        pos: Option<V3>,
        yaw: Option<f64>,
        scale: Option<f64>,
        visible: bool,
    },
    Motion { who: String, kind: i32, group: i32, category: i32, no: i32, blend: f64 },
    Face { who: String, eyebrow: i32, eyes: i32, mouth: i32 },
    Blink { who: String },
    AutoBlink { who: String, on: bool },
    /// mode: "camera", "point" (world point) or "animation".
    Look { who: String, mode: String, point: Option<V3> },
    Voice { who: String, file: Option<String>, tts: Option<String>, text: String, pitch: f64, volume: f64 },
    StopVoice { who: String },
    Se { file: String, volume: f64 },
    /// A copy of the character only the portrait sees (speakers not on stage).
    SpawnOffstage { who: String, card: String, outfit: i32, pos: V3 },
    ReleaseOffstage { who: String },
    /// Hide the UI, capture the view at this size to this JPEG, show the UI again.
    Thumbnail { path: String, width: u32, height: u32 },
    End { reason: String },
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Command {
    pub id: u64,
    #[serde(flatten)]
    pub op: Op,
}

/// A camera Unreal found in a loaded scene.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SceneCamera {
    pub id: i32,
    pub pos: V3,
    pub rot: [f64; 4],
    pub fov: f64,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Ack {
    pub id: u64,
    pub ok: bool,
    pub error: Option<String>,
    /// voice: the clip's length (seconds).
    pub duration: Option<f64>,
    /// place: the card's personality.
    pub personality: Option<i32>,
    /// load_scene: its cameras and the active one.
    pub cameras: Vec<SceneCamera>,
    pub active_camera: Option<i32>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct CameraOut {
    pub pos: V3,
    pub rot: [f64; 4],
    pub fov: f64,
    pub moving: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Default)]
pub struct PortraitOut {
    pub visible: bool,
    pub who: String,
    pub alpha: f64,
    /// The capture camera: orthographic, this wide (cm).
    pub pos: V3,
    pub rot: [f64; 4],
    pub ortho_width: f64,
    /// x, y, width, height as fractions of the view (y from the top).
    pub rect: [f64; 4],
    pub fade_from: f64,
    /// Render target pixels; capture this frame.
    pub target: [u32; 2],
    pub capture: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Default)]
pub struct DialogOut {
    pub visible: bool,
    pub name: String,
    pub name_color: String,
    /// The whole line; the widget lays it out once so typing never moves characters between lines.
    pub text: String,
    pub text_visible: String,
    pub shown: usize,
    pub total: usize,
    /// typing, voice, click, choice, auto, skip, title, loading, ended.
    pub waiting: String,
    pub opacity: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ChoiceOut {
    pub text: String,
    pub enabled: bool,
    pub hint: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct TitleOut {
    pub text: String,
    /// Horizontal offset as a fraction of the view width (0: in place).
    pub x: f64,
    pub alpha: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ButtonOut {
    pub id: String,
    pub label: String,
    pub on: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Default)]
pub struct Frame {
    pub time: f64,
    /// play, menu, save, load, backlog, settings, ended.
    pub screen: String,
    pub loading: bool,
    pub hide_ui: bool,
    pub camera: Option<CameraOut>,
    pub portrait: PortraitOut,
    pub dialog: DialogOut,
    pub choices: Vec<ChoiceOut>,
    pub title: Option<TitleOut>,
    pub layout: Layout,
    /// The quick bar under the dialog box.
    pub buttons: Vec<ButtonOut>,
    /// What the open screen shows (slots, backlog, settings, menu entries).
    pub panel: serde_json::Value,
    pub toast: Option<String>,
    /// Free camera editing: its hint.
    pub camera_edit: Option<String>,
    /// Lines of sight to measure this frame (answered in the next facts).
    pub probes: Vec<Probe>,
    pub commands: Vec<Command>,
    pub log: Vec<String>,
}
