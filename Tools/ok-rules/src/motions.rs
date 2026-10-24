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

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct MotionsTuning {
    #[serde(default)]
    pub gesture: Vec<Gesture>,
    #[serde(default)]
    pub life: Vec<Life>,
    #[serde(default)]
    pub photo: Vec<Photo>,
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

    /// Request: gesture (a key), posture (stand | chair), seed. Answer: clip and seconds, or clip "" (no such key, or
    /// nothing for that posture: sitting she does not jump up to cheer).
    fn gesture(&self, r: &Value) -> Value {
        let key = s(r, "gesture").trim().to_lowercase();
        let Some(g) = self.gesture.iter().find(|g| g.key == key) else { return json!({"clip": "", "why": "unknown_gesture"}) };
        let clips = if s(r, "posture") == "chair" { &g.chair } else { &g.stand };
        match pick(clips, i(r, "seed")) {
            Some(c) => json!({"clip": c, "seconds": g.seconds, "key": g.key}),
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
