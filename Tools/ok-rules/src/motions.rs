//! What the heroines' bodies do with the original game's animations (rules/motions.toml): the gesture she makes while
//! she talks (the model names it), the loop her schedule shows (by the block's kind and place, the same clip for the
//! whole block) and the two-shot poses for a photo. Unreal plays what comes back; which clip is this file's.
use crate::rng::RandomStream;
use serde::Deserialize;
use serde_json::{json, Value};

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct Gesture {
    pub key: String,
    #[serde(default)]
    pub stand: Vec<String>,
    #[serde(default)]
    pub chair: Vec<String>,
    #[serde(default = "default_seconds")]
    pub seconds: f32,
}
fn default_seconds() -> f32 {
    4.0
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct Life {
    pub kind: String,
    #[serde(default)]
    pub place: String,
    pub clips: Vec<String>,
    #[serde(default)]
    pub seated: bool,
    #[serde(default)]
    pub table: bool,
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct Photo {
    pub key: String,
    pub female: String,
    pub male: String,
    /// Only with a lover (the game's kiss two-shot).
    #[serde(default)]
    pub lover: bool,
}

/// One of her brows / eyes / mouth by name -> the source's face pattern. A mouth also says how far it opens while she
/// is silent (0 closed .. 1 wide: a gasp, not a smile); without it the named expression's own opening stays.
#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct FacePart {
    pub part: String,
    pub key: String,
    pub pattern: i32,
    #[serde(default)]
    pub open: Option<f32>,
}

/// Her mood -> the gestures that show it, for a line the model gave no gesture.
#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct Mood {
    pub emotions: Vec<String>,
    pub gestures: Vec<String>,
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct MotionsTuning {
    #[serde(default)]
    pub gesture: Vec<Gesture>,
    #[serde(default)]
    pub life: Vec<Life>,
    #[serde(default)]
    pub photo: Vec<Photo>,
    #[serde(default)]
    pub mood: Vec<Mood>,
    #[serde(default)]
    pub face: Vec<FacePart>,
}

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or("")
}
fn i(r: &Value, k: &str) -> i32 {
    r.get(k).and_then(Value::as_i64).unwrap_or(0) as i32
}
/// One of the clips, the same for the same seed.
fn pick<'a>(clips: &'a [String], seed: i32) -> Option<&'a String> {
    if clips.is_empty() {
        return None;
    }
    let mut rng = RandomStream::new(seed);
    Some(&clips[rng.rand_range(0, clips.len() as i32 - 1) as usize])
}

impl MotionsTuning {
    fn life_for(&self, kind: &str, place: &str) -> Option<&Life> {
        // Her place's own row first, then the kind's row without a place.
        self.life.iter().find(|l| l.kind == kind && !l.place.is_empty() && l.place == place).or_else(|| self.life.iter().find(|l| l.kind == kind && l.place.is_empty()))
    }

    /// Request: gesture (a key), posture (stand | chair), seed, emotion, speech_seconds. Answer: clip and seconds, or
    /// clip "" (no such key, or nothing for that posture: sitting she does not jump up to cheer). No gesture (or "none")
    /// takes one from her mood; the clip is held at least as long as she speaks, so the words never end in a still pose.
    fn gesture(&self, r: &Value) -> Value {
        let mut key = s(r, "gesture").trim().to_lowercase();
        if key.is_empty() || key == "none" || !self.gesture.iter().any(|g| g.key == key) {
            let emotion = s(r, "emotion").trim();
            let mood = self.mood.iter().find(|m| m.emotions.iter().any(|e| e.eq_ignore_ascii_case(emotion)));
            match mood.and_then(|m| pick(&m.gestures, i(r, "seed") ^ 0x5bd1)) {
                Some(g) => key = g.clone(),
                None if key.is_empty() || key == "none" => return json!({"clip": "", "why": "no_gesture"}),
                None => return json!({"clip": "", "why": "unknown_gesture"}),
            }
        }
        let Some(g) = self.gesture.iter().find(|g| g.key == key) else { return json!({"clip": "", "why": "unknown_gesture"}) };
        let clips = if s(r, "posture") == "chair" { &g.chair } else { &g.stand };
        let speech = r.get("speech_seconds").and_then(Value::as_f64).unwrap_or(0.0).clamp(0.0, 12.0);
        match pick(clips, i(r, "seed")) {
            Some(c) => json!({"clip": c, "seconds": g.seconds.max(speech as f32), "key": g.key}),
            None => json!({"clip": "", "why": "not_in_this_posture"}),
        }
    }

    /// Request: kind, place, seed (her block: the same clip all through it). Answer: clip ("" none), seated, table.
    fn life(&self, r: &Value) -> Value {
        match self.life_for(s(r, "kind"), s(r, "place")).and_then(|l| pick(&l.clips, i(r, "seed")).map(|c| (l, c))) {
            Some((l, c)) => json!({"clip": c, "seated": l.seated, "table": l.table}),
            None => json!({"clip": "", "seated": false, "table": false}),
        }
    }

    /// Request: pose (a key, or ""), after (the pose they had: "another pose" goes to the next), seed. Answer: key,
    /// female, male.
    fn photo(&self, r: &Value) -> Value {
        if self.photo.is_empty() {
            return json!({"key": "", "female": "", "male": ""});
        }
        // A lover's poses only with a lover (request: lover).
        let lover = r.get("lover").and_then(Value::as_bool).unwrap_or(false);
        let poses: Vec<&Photo> = self.photo.iter().filter(|p| lover || !p.lover).collect();
        let n = poses.len();
        let index = if let Some(k) = poses.iter().position(|p| p.key == s(r, "pose")) {
            k
        } else if let Some(k) = poses.iter().position(|p| p.key == s(r, "after")) {
            (k + 1) % n
        } else {
            let mut rng = RandomStream::new(i(r, "seed"));
            rng.rand_range(0, n as i32 - 1) as usize
        };
        let p = poses[index];
        json!({"key": p.key, "female": p.female, "male": p.male})
    }

    /// Request: brow, eyes, mouth (names). Answer: brow, eyes, mouth patterns (-1: not given or no such name), and
    /// mouth_open (how far that mouth opens while she is silent; -1: the table does not say).
    fn face_parts(&self, r: &Value) -> Value {
        let find = |part: &str| {
            let key = s(r, part).trim().to_lowercase();
            self.face.iter().find(|f| f.part == part && f.key == key)
        };
        let pattern = |part: &str| find(part).map(|f| f.pattern).unwrap_or(-1);
        let open = find("mouth").and_then(|f| f.open).map(|o| o.clamp(0.0, 1.0)).unwrap_or(-1.0);
        json!({"brow": pattern("brow"), "eyes": pattern("eyes"), "mouth": pattern("mouth"), "mouth_open": open})
    }

    /// Every clip the table names and whether she sits for it (Unreal brings the chair for seated loops).
    fn table(&self) -> Value {
        let mut seated = Vec::new();
        let mut tabled = Vec::new();
        for l in &self.life {
            for c in &l.clips {
                if l.seated {
                    seated.push(c.clone());
                }
                if l.table {
                    tabled.push(c.clone());
                }
            }
        }
        for g in &self.gesture {
            seated.extend(g.chair.iter().cloned());
        }
        seated.sort();
        seated.dedup();
        tabled.sort();
        tabled.dedup();
        json!({"seated": seated, "table": tabled, "gestures": self.gesture.iter().map(|g| g.key.clone()).collect::<Vec<_>>(), "photo_poses": self.photo.iter().map(|p| p.key.clone()).collect::<Vec<_>>()})
    }
}

pub fn eval(t: &MotionsTuning, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "motion_gesture" => t.gesture(r),
        "motion_life" => t.life(r),
        "motion_photo" => t.photo(r),
        "motion_table" => t.table(),
        "face_parts" => t.face_parts(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn gestures_life_and_photo_poses_come_from_the_table() {
        let t = Tuning::builtin();
        let m = &t.motions;
        let wave = m.gesture(&json!({"gesture": "Wave", "posture": "stand", "seed": 3}));
        assert!(wave["clip"].as_str().unwrap().ends_with(".okanim"), "{wave}");
        // sitting: no cheering, but a stretch on the chair
        assert_eq!(m.gesture(&json!({"gesture": "cheer", "posture": "chair", "seed": 1}))["clip"], "");
        assert_eq!(m.gesture(&json!({"gesture": "stretch", "posture": "chair", "seed": 1}))["clip"], "lib_0_6_9.okanim");
        assert_eq!(m.gesture(&json!({"gesture": "no_such", "seed": 1}))["why"], "unknown_gesture");
        // no gesture from the model: one from her mood, held while she speaks
        let shy = m.gesture(&json!({"gesture": "none", "emotion": "害羞", "posture": "stand", "seed": 4, "speech_seconds": 9.0}));
        assert!(["shy", "hide_face"].contains(&shy["key"].as_str().unwrap()), "{shy}");
        assert_eq!(shy["seconds"].as_f64(), Some(9.0));
        assert_eq!(m.gesture(&json!({"emotion": "平常", "seed": 2}))["key"], "talk");
        assert_eq!(m.gesture(&json!({"gesture": "none", "seed": 2}))["why"], "no_gesture");
        // her face by parts
        let f = m.face_parts(&json!({"brow": "Angry", "eyes": "glare", "mouth": "nope"}));
        assert_eq!((f["brow"].as_i64(), f["eyes"].as_i64(), f["mouth"].as_i64()), (Some(9), Some(9), Some(-1)));
        assert_eq!(f["mouth_open"].as_f64(), Some(-1.0));
        // each mouth opens as far as it is meant to while she is silent: a gasp wide, a smile barely, closed shut
        let open = |mouth: &str| m.face_parts(&json!({"mouth": mouth}))["mouth_open"].as_f64().unwrap();
        assert!(open("gasp") > open("laugh") && open("laugh") > open("smile") && open("smile") > open("closed"));
        assert_eq!(open("closed"), 0.0);
        // the same block shows the same loop; the place's own row wins
        let a = m.life(&json!({"kind": "leisure", "place": "cafe", "seed": 77}));
        assert_eq!(a, m.life(&json!({"kind": "leisure", "place": "cafe", "seed": 77})));
        assert_eq!(a["seated"], true);
        let shop = m.life(&json!({"kind": "work", "place": "department", "seed": 5}));
        assert!(["lib_17_4_9.okanim", "lib_17_4_10.okanim", "venue_shopping.okanim"].contains(&shop["clip"].as_str().unwrap()));
        assert_eq!(m.life(&json!({"kind": "work", "place": "nowhere", "seed": 5}))["seated"], false);
        assert_eq!(m.life(&json!({"kind": "sleep", "seed": 5}))["clip"], "facility_sleep.okanim");
        // "another pose": the next one, round
        let first = m.photo(&json!({"pose": "twoshot_9"}));
        assert_eq!(first["female"], "photo_female_08.okanim");
        assert_eq!(m.photo(&json!({"after": "twoshot_9"}))["key"], "twoshot_1");
        // the kiss two-shot: next after the ninth with a lover, never without one (asked for by name either)
        assert_eq!(m.photo(&json!({"after": "twoshot_9", "lover": true}))["key"], "kiss");
        assert_ne!(m.photo(&json!({"pose": "kiss"}))["key"], "kiss");
        for seed in 0..40 {
            assert_ne!(m.photo(&json!({"seed": seed}))["key"], "kiss");
        }
        assert!(m.table()["seated"].as_array().unwrap().iter().any(|c| c == "facility_eat.okanim"));
    }
}
