//! Every number of the presentation (Content/IndependentResources/Story/tuning.json overrides any of them), the
//! expression table, and the player's words (Localization/<lang>/story.toml).
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Layout {
    /// Fractions of the view.
    pub dialog_height: f64,
    pub text_left: f64,
    pub portrait_width: f64,
    pub portrait_height: f64,
    /// Where (from the top of the portrait, 0..1) its fade into the dialog box starts.
    pub portrait_fade_from: f64,
    pub dialog_opacity: f64,
}

impl Default for Layout {
    fn default() -> Self {
        Layout {
            dialog_height: 0.26,
            text_left: 0.275,
            portrait_width: 0.54,
            portrait_height: 0.68,
            portrait_fade_from: 0.5,
            dialog_opacity: 0.86,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct TextTuning {
    /// Characters per second at speed 1..10 (index speed-1).
    pub speeds: Vec<f64>,
    /// A pause after these, in character times.
    pub pause_after: String,
    pub pause_chars: f64,
    /// Auto mode waits this plus per character after a line is out.
    pub auto_base: f64,
    pub auto_per_char: f64,
    /// Seconds per line when skipping.
    pub skip_line: f64,
}

impl Default for TextTuning {
    fn default() -> Self {
        TextTuning {
            speeds: vec![8.0, 12.0, 17.0, 23.0, 30.0, 38.0, 48.0, 62.0, 85.0, 140.0],
            pause_after: "、。，．,.!?！？…―─～~♪「」『』（）".into(),
            pause_chars: 3.0,
            auto_base: 0.8,
            auto_per_char: 0.06,
            skip_line: 0.05,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct CameraTuning {
    /// Travel time = clamp(distance / per_cm + angle / π × per_turn + |Δfov| / per_fov, min, max) / clamp(speed, ..).
    pub per_cm: f64,
    pub per_turn: f64,
    pub per_fov: f64,
    pub min_seconds: f64,
    pub max_seconds: f64,
    pub speed_min: f64,
    pub speed_max: f64,
    /// Below these nothing moves (cm, radians, degrees).
    pub still_cm: f64,
    pub still_angle: f64,
    pub still_fov: f64,
    /// Shots.
    pub speaker_distance: f64,
    pub speaker_side_deg: f64,
    pub speaker_fov: f64,
    /// Close-ups and two shots show from this far above the head to this far below it (cm).
    pub frame_above: f64,
    pub frame_below: f64,
    /// Over-the-shoulder shots: turned this far off the line between the two (degrees), this far behind the speaker (cm).
    pub over_side_deg: f64,
    pub over_behind: f64,
    /// Sideways clearance (cm) the line of sight keeps from the near shoulder's person.
    pub over_clear: f64,
    pub two_fov: f64,
    /// The pair fills this much of the width.
    pub two_fill: f64,
    pub wide_fov: f64,
    pub wide_margin: f64,
    /// The part of the view height above the dialog box (where a wide shot puts everyone).
    pub wide_free: f64,
    /// Parallax: offset (cm) and turn (degrees) at full strength, smoothing rate.
    pub parallax_cm: [f64; 2],
    pub parallax_deg: [f64; 2],
    pub parallax_rate: f64,
    /// Free camera while editing: cm/s, mouse degrees per pixel, fast and slow factors.
    pub fly_speed: f64,
    pub fly_mouse: f64,
    pub fly_fast: f64,
    pub fly_slow: f64,
    /// Shots are tried from these turns about their subject (degrees, best first), then closer in by these factors;
    /// the first whose view is clear (a probe from the subject reaches within probe_margin cm of it) is taken.
    pub probe_angles: Vec<f64>,
    pub probe_closer: Vec<f64>,
    pub probe_margin: f64,
}

impl Default for CameraTuning {
    fn default() -> Self {
        CameraTuning {
            per_cm: 400.0,
            per_turn: 0.7,
            per_fov: 100.0,
            min_seconds: 0.35,
            max_seconds: 4.0,
            speed_min: 0.85,
            speed_max: 1.15,
            still_cm: 0.1,
            still_angle: 0.001,
            still_fov: 0.01,
            speaker_distance: 165.0,
            speaker_side_deg: 20.0,
            speaker_fov: 30.0,
            frame_above: 20.0,
            frame_below: 45.0,
            over_side_deg: 18.0,
            over_behind: 70.0,
            over_clear: 42.0,
            two_fov: 35.0,
            two_fill: 0.42,
            wide_fov: 40.0,
            wide_margin: 1.25,
            wide_free: 0.74,
            parallax_cm: [4.0, 2.5],
            parallax_deg: [1.2, 0.8],
            parallax_rate: 8.0,
            fly_speed: 300.0,
            fly_mouse: 0.15,
            fly_fast: 3.0,
            fly_slow: 0.25,
            probe_angles: vec![0.0, -40.0, 35.0, -75.0, 70.0, 110.0, -110.0, 160.0],
            probe_closer: vec![1.0, 0.6],
            probe_margin: 15.0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct PortraitTuning {
    /// Camera offset in the character's heading frame (cm): forward (along her facing), side (towards her right: the view
    /// sees her turned towards the name plate), up.
    pub forward: f64,
    pub side: f64,
    pub up: f64,
    /// Aim this far above the target point (cm).
    pub aim_up: f64,
    /// Orthographic width (cm) across the portrait at scale 1, and where in it (fractions from the left and the bottom)
    /// the head stands.
    pub width: f64,
    pub anchor: [f64; 2],
    pub fade_out: f64,
    pub fade_in: f64,
    /// Render target size and how often it is captured (every N frames).
    pub target: [u32; 2],
    pub every: u32,
    /// Where offstage speakers stand (out of every view).
    pub offstage_at: [f64; 3],
    /// Their motion when the story gives none (group:category:no; 0:0:1 is Studio's 立ち1).
    pub offstage_motion: String,
}

impl Default for PortraitTuning {
    fn default() -> Self {
        PortraitTuning {
            forward: 170.0,
            side: 85.0,
            up: 5.0,
            aim_up: 3.0,
            width: 120.0,
            anchor: [0.2, 0.3],
            fade_out: 0.10,
            fade_in: 0.15,
            target: [768, 1152],
            every: 1,
            offstage_at: [0.0, 50000.0, 0.0],
            offstage_motion: "0:0:1".into(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct LifeTuning {
    pub blink_every: [f64; 2],
    pub blink_retry: [f64; 2],
    pub blink_max: usize,
    /// How long one blink keeps the slot (seconds).
    pub blink_length: f64,
    pub glance_every: [f64; 2],
    pub glance_hold: [f64; 2],
    pub glance_yaw: [f64; 2],
    pub glance_pitch: [f64; 2],
    pub look_hold: [f64; 2],
    pub look_cooldown: [f64; 2],
    /// A character to look at must be within these (degrees off the facing, cm).
    pub look_cone: f64,
    pub look_range: f64,
    pub max_yaw: f64,
    pub max_pitch: f64,
    pub gaze_max: usize,
    pub gaze_stagger: f64,
    pub gaze_jitter: [f64; 2],
    /// Distance of the point a glance looks at (cm).
    pub glance_distance: f64,
}

impl Default for LifeTuning {
    fn default() -> Self {
        LifeTuning {
            blink_every: [6.0, 12.0],
            blink_retry: [0.2, 1.0],
            blink_max: 1,
            blink_length: 0.3,
            glance_every: [8.0, 16.0],
            glance_hold: [0.45, 1.2],
            glance_yaw: [3.0, 8.0],
            glance_pitch: [1.0, 4.0],
            look_hold: [1.0, 1.8],
            look_cooldown: [45.0, 75.0],
            look_cone: 70.0,
            look_range: 400.0,
            max_yaw: 13.0,
            max_pitch: 6.0,
            gaze_max: 2,
            gaze_stagger: 1.2,
            gaze_jitter: [0.0, 0.6],
            glance_distance: 300.0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct FlowTuning {
    /// Act title: enter, hold, exit (seconds); the enter curve (cubic-bezier control points).
    pub title_enter: f64,
    pub title_hold: f64,
    pub title_exit: f64,
    pub title_enter_curve: [f64; 4],
    pub title_exit_curve: [f64; 4],
    /// Motion change blend (seconds).
    pub motion_blend: f64,
    pub backlog_size: usize,
    pub save_slots: usize,
    pub thumbnail: [u32; 2],
}

impl Default for FlowTuning {
    fn default() -> Self {
        FlowTuning {
            title_enter: 0.46,
            title_hold: 0.80,
            title_exit: 0.34,
            title_enter_curve: [0.16, 1.18, 0.3, 1.0],
            title_exit_curve: [0.65, 0.0, 0.85, 0.35],
            motion_blend: 0.3,
            backlog_size: 200,
            save_slots: 12,
            thumbnail: [320, 180],
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct LiveTuning {
    /// The player's own lines show his portrait too.
    pub player_portrait: bool,
    /// Reading speed (1..10) of lines without a voice.
    pub text_speed: u32,
    /// Pointer parallax of the conversation shots (0 off).
    pub parallax: f64,
}

impl Default for LiveTuning {
    fn default() -> Self {
        LiveTuning { player_portrait: true, text_speed: 6, parallax: 0.15 }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(default)]
pub struct Tuning {
    /// The live presenter (conversations whose words come from the game).
    pub live: LiveTuning,
    pub layout: Layout,
    pub text: TextTuning,
    pub camera: CameraTuning,
    pub portrait: PortraitTuning,
    pub life: LifeTuning,
    pub flow: FlowTuning,
    /// Story face names → the game's expression names (faces.json is merged in).
    pub faces: BTreeMap<String, String>,
}

pub fn default_faces() -> BTreeMap<String, String> {
    [
        ("normal", "標準"),
        ("closed", "標準（目閉じ）"),
        ("smile", "笑顔"),
        ("smile_closed", "笑顔（目閉じ）"),
        ("laugh", "スマイルA"),
        ("happy", "嬉しい"),
        ("shy", "照れ顔"),
        ("blush", "恥ずかしくて真っ赤"),
        ("sad", "悲しい"),
        ("cry", "泣き顔"),
        ("tearful_smile", "涙目笑顔"),
        ("angry", "怒り１"),
        ("pout", "不満"),
        ("surprise", "驚き"),
        ("think", "思案"),
        ("worry", "困り顔"),
        ("serious", "真剣"),
        ("smug", "ドヤ顔"),
        ("wink", "ウインク(左閉じ)"),
        ("doki", "ドキドキ"),
        ("sleepy", "半目"),
        ("look_away", "思わず目を逸らす"),
    ]
    .into_iter()
    .map(|(a, b)| (a.to_string(), b.to_string()))
    .collect()
}

impl Tuning {
    /// Defaults with tuning.json and faces.json of `dir` laid over them (missing files: the defaults).
    pub fn load(dir: Option<&Path>) -> Result<Tuning, String> {
        let mut t = Tuning { faces: default_faces(), ..Default::default() };
        let Some(dir) = dir else { return Ok(t) };
        let file = dir.join("tuning.json");
        if file.is_file() {
            let text = std::fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
            let over: serde_json::Value = serde_json::from_str(&text).map_err(|e| format!("{}: {e}", file.display()))?;
            let mut base = serde_json::to_value(&t).map_err(|e| e.to_string())?;
            merge(&mut base, &over);
            t = serde_json::from_value(base).map_err(|e| format!("{}: {e}", file.display()))?;
        }
        let file = dir.join("faces.json");
        if file.is_file() {
            let text = std::fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
            let faces: BTreeMap<String, String> =
                serde_json::from_str(&text).map_err(|e| format!("{}: {e}", file.display()))?;
            t.faces.extend(faces);
        }
        Ok(t)
    }
}

fn merge(base: &mut serde_json::Value, over: &serde_json::Value) {
    match (base, over) {
        (serde_json::Value::Object(b), serde_json::Value::Object(o)) => {
            for (k, v) in o {
                match b.get_mut(k) {
                    Some(slot) => merge(slot, v),
                    None => {
                        b.insert(k.clone(), v.clone());
                    }
                }
            }
        }
        (b, o) => *b = o.clone(),
    }
}

/// One row of the game's expression table (StreetSocialData/RuntimeV2/expressions.json).
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
pub struct Expression {
    pub eyebrow: i32,
    pub eyes: i32,
    pub mouth: i32,
    /// The expression has its eyes shut or winks (no automatic blink over it).
    pub closed: bool,
}

#[derive(Debug, Clone, Default)]
pub struct Expressions {
    /// personality → name → expression.
    pub profiles: BTreeMap<i32, BTreeMap<String, Expression>>,
}

impl Expressions {
    pub fn load(path: Option<&Path>) -> Result<Expressions, String> {
        let Some(path) = path.filter(|p| p.is_file()) else { return Ok(Expressions::default()) };
        let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
        Self::parse(&text).map_err(|e| format!("{}: {e}", path.display()))
    }

    pub fn parse(text: &str) -> Result<Expressions, String> {
        let root: serde_json::Value = serde_json::from_str(text).map_err(|e| e.to_string())?;
        let profiles = root.get("profiles").and_then(|p| p.as_object()).ok_or("no profiles")?;
        let mut out = Expressions::default();
        for (key, table) in profiles {
            let Ok(personality) = key.parse::<i32>() else { continue };
            let Some(table) = table.as_object() else { continue };
            let mut rows = BTreeMap::new();
            for (name, row) in table {
                let v = &row["values"];
                let pattern = |k: &str| v[k]["pattern"].as_i64().map(|n| n as i32).unwrap_or(-1);
                let blink = v["blink"].as_bool().unwrap_or(true);
                rows.insert(
                    name.clone(),
                    Expression { eyebrow: pattern("brow"), eyes: pattern("eyes"), mouth: pattern("mouth"), closed: !blink },
                );
            }
            out.profiles.insert(personality, rows);
        }
        Ok(out)
    }

    /// `face` (a story name or the game's own name) for a personality; the closest profile when it has none.
    pub fn find(&self, faces: &BTreeMap<String, String>, personality: Option<i32>, face: &str) -> Option<Expression> {
        let name = faces.get(face).map(String::as_str).unwrap_or(face);
        let try_profile = |p: i32| self.profiles.get(&p).and_then(|t| t.get(name)).copied();
        if let Some(p) = personality {
            if let Some(e) = try_profile(p) {
                return Some(e);
            }
        }
        self.profiles.values().find_map(|t| t.get(name).copied())
    }
}

/// The player's words: Localization/<lang>/story.toml (`id = "ICU message"`), over the built-in ones.
#[derive(Debug, Clone, Default)]
pub struct Words {
    pub lang: String,
    table: BTreeMap<String, String>,
}

const BUILTIN_ZH: &[(&str, &str)] = &[
    ("ui.auto", "自動"),
    ("ui.skip", "快進"),
    ("ui.backlog", "記錄"),
    ("ui.save", "存檔"),
    ("ui.load", "讀檔"),
    ("ui.settings", "設定"),
    ("ui.hide", "隱藏"),
    ("ui.close", "關閉"),
    ("ui.quit", "結束"),
    ("ui.menu", "選單"),
    ("ui.outline", "劇本目錄"),
    ("ui.empty_slot", "（空）"),
    ("ui.slot", "第 {n} 格"),
    ("ui.quick", "快速存檔"),
    ("ui.auto_save", "自動存檔"),
    ("ui.missing", "缺少：{items}"),
    ("ui.item_have", "{name} × {n}（現有 {have}）"),
    ("ui.story_changed", "劇本已修改，從最接近的位置繼續"),
    ("ui.saved", "已存到{n}"),
    ("ui.loaded", "已讀取{n}"),
    ("ui.end", "（完）"),
    ("ui.loading", "載入中…"),
    ("ui.replay", "重播語音"),
    ("ui.camera_edit", "相機編輯：WASD/QE 移動，滑鼠轉向，Enter 確定，Esc 取消"),
    ("ui.camera_saved", "已把相機存到 {line}"),
    ("set.text_speed", "文字速度"),
    ("set.auto_speed", "自動速度"),
    ("set.skip_unread", "快進未讀"),
    ("set.voice_volume", "語音音量"),
    ("set.stop_voice", "前進時停止語音"),
    ("set.parallax", "空間感"),
    ("set.ui_opacity", "對話框透明度"),
    ("set.on", "開"),
    ("set.off", "關"),
];

impl Words {
    pub fn load(dir: Option<&Path>, lang: &str) -> Result<Words, String> {
        let mut table: BTreeMap<String, String> = BUILTIN_ZH.iter().map(|(a, b)| (a.to_string(), b.to_string())).collect();
        if let Some(dir) = dir {
            let base = lang.split(['-', '_']).next().unwrap_or(lang);
            for l in [base, lang] {
                let file = dir.join("Localization").join(l).join("story.toml");
                if file.is_file() {
                    let text = std::fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
                    for (line_no, line) in text.lines().enumerate() {
                        let line = line.trim();
                        if line.is_empty() || line.starts_with('#') || line.starts_with('[') {
                            continue;
                        }
                        let Some((k, v)) = line.split_once('=') else {
                            return Err(format!("{}:{}: expected id = \"text\"", file.display(), line_no + 1));
                        };
                        let v: String = serde_json::from_str(v.trim())
                            .map_err(|e| format!("{}:{}: {e}", file.display(), line_no + 1))?;
                        table.insert(k.trim().trim_matches('"').to_string(), v);
                    }
                }
            }
        }
        Ok(Words { lang: lang.to_string(), table })
    }

    pub fn t(&self, id: &str) -> String {
        self.table.get(id).cloned().unwrap_or_else(|| id.to_string())
    }

    pub fn m(&self, id: &str, args: &[(&str, ok_rules::icu::Arg)]) -> String {
        let pattern = self.t(id);
        let map: ok_rules::icu::Args = args.iter().map(|(k, v)| (*k, v.clone())).collect();
        ok_rules::icu::format(&pattern, &self.lang, &map)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn partial_tuning_overrides() {
        let dir = std::env::temp_dir().join(format!("ok-story-tuning-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("tuning.json"), r#"{"camera": {"per_cm": 200}, "faces": {"grin": "ドヤ顔"}}"#).unwrap();
        let t = Tuning::load(Some(&dir)).unwrap();
        assert_eq!(t.camera.per_cm, 200.0);
        assert_eq!(t.camera.per_turn, 0.7);
        assert_eq!(t.faces["grin"], "ドヤ顔");
        assert_eq!(t.faces["smile"], "笑顔");
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn expressions_by_alias_and_profile() {
        let e = Expressions::parse(
            r#"{"profiles":{"25":{"笑顔":{"values":{"brow":{"pattern":0},"eyes":{"pattern":2},"mouth":{"pattern":1},"blink":true}},
                 "笑顔（目閉じ）":{"values":{"brow":{"pattern":0},"eyes":{"pattern":1},"mouth":{"pattern":1},"blink":false}}}}}"#,
        )
        .unwrap();
        let faces = default_faces();
        let smile = e.find(&faces, Some(25), "smile").unwrap();
        assert_eq!((smile.eyes, smile.mouth, smile.closed), (2, 1, false));
        assert!(e.find(&faces, Some(3), "smile_closed").unwrap().closed);
        assert!(e.find(&faces, Some(25), "nope").is_none());
    }

    #[test]
    fn words_format_icu() {
        let w = Words::load(None, "zh").unwrap();
        assert_eq!(w.m("ui.slot", &[("n", ok_rules::icu::Arg::Int(3))]), "第 3 格");
    }
}

#[cfg(test)]
mod shipped {
    use super::*;

    #[test]
    fn shipped_words_cover_the_builtin_ids() {
        let dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/Story");
        if !dir.is_dir() {
            return;
        }
        for lang in ["zh", "ja", "en"] {
            let w = Words::load(Some(&dir), lang).unwrap();
            for (id, _) in BUILTIN_ZH {
                assert_ne!(w.t(id), *id, "{lang}: {id}");
            }
            if lang != "zh" {
                assert_ne!(w.t("ui.save"), "存檔", "{lang} file read");
            }
        }
    }
}
