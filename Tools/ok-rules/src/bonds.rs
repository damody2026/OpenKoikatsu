//! What the heroines feel for each other. Each has a favor toward each other heroine (directional) and a stage
//! between them: strangers (min .. her stranger cap), friends once both reach their caps, lovers only when both are
//! bisexual and both care a lot. It moves by rules only, never by a model call: the hours they spend in the same place
//! doing the same thing, their meetings, and jealousy over him. Below zero she will not do things together with that
//! one unless she is his girlfriend, then she comes along reluctantly.
//!
//! Unreal owns the numbers (the save); every request carries the heroines involved and the answer says what to write.
//! Numbers are f32 like the save, so the results match what the game stores.
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct BondTuning {
    pub start_favor: f32,
    pub min_favor: f32,
    pub friend_cap: f32,
    pub stranger_cap_default: f32,
    pub cap_slack: f32,
    pub lover_favor: f32,
    pub together: Together,
    pub events: HashMap<String, EventTuning>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct Together {
    pub tick_minutes: i64,
    pub max_hours: f32,
    pub max_distance: f32,
    pub max_height: f32,
    pub with_him_rate: f32,
    pub with_him_why: String,
    pub jealous_limit: f32,
    pub jealous_loss_scale: f32,
    pub rates: HashMap<String, f32>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct EventTuning {
    pub ab: f32,
    pub ba: f32,
    pub why: String,
}

#[derive(Serialize, Deserialize, Debug, Clone, Copy, PartialEq, Eq, Default)]
#[serde(rename_all = "lowercase")]
pub enum Stage {
    #[default]
    Stranger,
    Friend,
    Lover,
}

#[derive(Deserialize, Debug, Clone, Default)]
pub struct Toward {
    pub favor: f32,
    #[serde(default)]
    pub stage: Stage,
}

/// One heroine as Unreal sees her right now. Only the fields a request needs have to be filled.
#[derive(Deserialize, Debug, Clone, Default)]
pub struct Heroine {
    /// Her actor index in the scene; answers name heroines by it.
    pub g: i32,
    /// Stable id; `toward` and `jealousy` are keyed by the other heroines' ids.
    pub id: String,
    #[serde(default)]
    pub bisexual: bool,
    /// Her own cap while strangers; 0 or missing: `stranger_cap_default`.
    #[serde(default)]
    pub stranger_cap: f32,
    #[serde(default)]
    pub jealousy: HashMap<String, f32>,
    #[serde(default)]
    pub toward: HashMap<String, Toward>,
    /// Where she is; missing: not in the street (never "together").
    #[serde(default)]
    pub pos: Option<[f32; 3]>,
    /// Her life plan's current block: activity kind and place.
    #[serde(default)]
    pub activity: String,
    #[serde(default)]
    pub place: String,
    /// Following him (or talking with him).
    #[serde(default)]
    pub with_him: bool,
    /// His girlfriend.
    #[serde(default)]
    pub girlfriend: bool,
}

#[derive(Serialize, Debug, Clone, PartialEq)]
pub struct Change {
    pub a: i32,
    pub b: i32,
    pub favor_ab: f32,
    pub favor_ba: f32,
    pub stage: Stage,
    pub why: String,
}

#[derive(Serialize, Debug, Clone, PartialEq)]
pub struct Transition {
    pub a: i32,
    pub b: i32,
    pub from: Stage,
    pub to: Stage,
    pub why: String,
    pub favor_ab: f32,
    pub favor_ba: f32,
}

#[derive(Serialize, Debug, Default, PartialEq)]
pub struct Outcome {
    pub changes: Vec<Change>,
    pub transitions: Vec<Transition>,
}

/// Unreal's FMath::Clamp: never panics, even when the cap is below the floor.
fn clamp(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo {
        lo
    } else if x < hi {
        x
    } else {
        hi
    }
}

impl BondTuning {
    fn favor(&self, from: &Heroine, other: &Heroine) -> f32 {
        from.toward.get(&other.id).map(|t| t.favor).unwrap_or(self.start_favor)
    }

    fn cap(&self, h: &Heroine, stage: Stage) -> f32 {
        match stage {
            Stage::Stranger if h.stranger_cap > 0.0 => h.stranger_cap,
            Stage::Stranger => self.stranger_cap_default,
            _ => self.friend_cap,
        }
    }

    /// Both directions move (each by her own delta), then the stage between them follows: friends when both reach
    /// their stranger caps, lovers when both are friends at `lover_favor`+ and both bisexual, back to strangers when
    /// either falls below zero.
    pub fn change(&self, a: &Heroine, b: &Heroine, delta_ab: f32, delta_ba: f32, why: &str, out: &mut Outcome) {
        let stage = a.toward.get(&b.id).map(|t| t.stage).unwrap_or_default();
        let mut fa = clamp(self.favor(a, b) + delta_ab, self.min_favor, self.cap(a, stage));
        let mut fb = clamp(self.favor(b, a) + delta_ba, self.min_favor, self.cap(b, stage));
        let next = match stage {
            Stage::Stranger if fa >= self.cap(a, stage) - self.cap_slack && fb >= self.cap(b, stage) - self.cap_slack => Stage::Friend,
            Stage::Friend if fa >= self.lover_favor && fb >= self.lover_favor && a.bisexual && b.bisexual => Stage::Lover,
            Stage::Friend | Stage::Lover if fa < 0.0 || fb < 0.0 => Stage::Stranger,
            _ => stage,
        };
        if next == Stage::Stranger && stage != Stage::Stranger {
            fa = fa.min(self.cap(a, next));
            fb = fb.min(self.cap(b, next));
        }
        out.changes.push(Change { a: a.g, b: b.g, favor_ab: fa, favor_ba: fb, stage: next, why: why.into() });
        if next != stage {
            out.transitions.push(Transition { a: a.g, b: b.g, from: stage, to: next, why: why.into(), favor_ab: fa, favor_ba: fb });
        }
    }

    /// A single event between two of them (a meeting, jealousy over him, a game together).
    pub fn event(&self, name: &str, a: &Heroine, b: &Heroine) -> Result<Outcome, String> {
        let e = self.events.get(name).ok_or_else(|| format!("unknown_bond_event: {name}"))?;
        let mut out = Outcome::default();
        self.change(a, b, e.ab, e.ba, &e.why, &mut out);
        Ok(out)
    }

    /// Two heroines in the same place doing the same thing grow closer (per game hour: `together.rates` by activity;
    /// following him together `with_him_rate`). Jealousy over him eats into it, and strong jealousy turns it negative.
    pub fn tick(&self, minutes: f64, heroines: &[Heroine]) -> Outcome {
        let t = &self.together;
        let hours = clamp((minutes / 60.0) as f32, 0.0, t.max_hours);
        let mut out = Outcome::default();
        for (i, hi) in heroines.iter().enumerate() {
            for hj in &heroines[i + 1..] {
                let (Some(pi), Some(pj)) = (hi.pos, hj.pos) else { continue };
                let dist = ((pi[0] - pj[0]).powi(2) + (pi[1] - pj[1]).powi(2)).sqrt();
                if (pi[2] - pj[2]).abs() > t.max_height || dist > t.max_distance {
                    continue;
                }
                let (rate, why) = if hi.with_him && hj.with_him {
                    (t.with_him_rate, t.with_him_why.as_str())
                } else if !hi.activity.is_empty() && hi.activity == hj.activity && !hi.place.is_empty() && hi.place == hj.place {
                    (t.rates.get(&hi.activity).copied().unwrap_or(0.0), hi.activity.as_str())
                } else {
                    (0.0, "")
                };
                if rate <= 0.0 {
                    continue;
                }
                let gain = |from: &Heroine, other: &Heroine| {
                    let jealous = from.jealousy.get(&other.id).copied().unwrap_or(0.0);
                    if jealous > t.jealous_limit {
                        -rate * t.jealous_loss_scale * hours
                    } else {
                        rate * hours * (1.0 - jealous / 100.0)
                    }
                };
                self.change(hi, hj, gain(hi, hj), gain(hj, hi), why, &mut out);
            }
        }
        out
    }

    /// Whether `girl` will be in a group with `others`: 0 fine, 1 reluctant (she dislikes someone but is his
    /// girlfriend), 2 refuse. `who`: the one she dislikes most.
    pub fn mood(&self, girl: &Heroine, others: &[Heroine]) -> (i32, Option<i32>) {
        let mut worst: Option<&Heroine> = None;
        let mut low = 0.0f32;
        for other in others.iter().filter(|o| o.g != girl.g) {
            let f = self.favor(girl, other);
            if f < low {
                low = f;
                worst = Some(other);
            }
        }
        match worst {
            None => (0, None),
            Some(w) => (if girl.girlfriend { 1 } else { 2 }, Some(w.g)),
        }
    }
}

fn field<T: serde::de::DeserializeOwned>(request: &Value, name: &str) -> Result<T, String> {
    let v = request.get(name).cloned().ok_or_else(|| format!("missing_field: {name}"))?;
    serde_json::from_value(v).map_err(|e| format!("{name}: {e}"))
}

pub fn eval(t: &BondTuning, kind: &str, request: Value) -> Result<Value, String> {
    Ok(match kind {
        "bonds_defaults" => json!({"start_favor": t.start_favor, "tick_minutes": t.together.tick_minutes}),
        "bonds_tick" => {
            let minutes: f64 = field(&request, "minutes")?;
            let heroines: Vec<Heroine> = field(&request, "heroines")?;
            serde_json::to_value(t.tick(minutes, &heroines)).map_err(|e| e.to_string())?
        }
        "bond_event" => {
            let name: String = field(&request, "event")?;
            let (a, b): (Heroine, Heroine) = (field(&request, "a")?, field(&request, "b")?);
            serde_json::to_value(t.event(&name, &a, &b)?).map_err(|e| e.to_string())?
        }
        "bond_mood" => {
            let girl: Heroine = field(&request, "girl")?;
            let others: Vec<Heroine> = field(&request, "others")?;
            let (mood, who) = t.mood(&girl, &others);
            json!({"mood": mood, "who": who.unwrap_or(-1)})
        }
        _ => return Err(format!("unknown_kind: {kind}")),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    fn tuning() -> BondTuning {
        Tuning::builtin().bonds
    }

    fn girl(g: i32) -> Heroine {
        Heroine { g, id: format!("h{g}"), pos: Some([0.0, 0.0, 0.0]), ..Default::default() }
    }

    fn with(mut h: Heroine, other: &str, favor: f32, stage: Stage) -> Heroine {
        h.toward.insert(other.into(), Toward { favor, stage });
        h
    }

    fn doing(mut h: Heroine, activity: &str, place: &str) -> Heroine {
        h.activity = activity.into();
        h.place = place.into();
        h
    }

    #[test]
    fn unset_favor_starts_at_ten_and_moves_both_ways() {
        let t = tuning();
        let mut out = Outcome::default();
        t.change(&girl(1), &girl(2), 3.0, -4.0, "x", &mut out);
        assert_eq!(out.changes, vec![Change { a: 1, b: 2, favor_ab: 13.0, favor_ba: 6.0, stage: Stage::Stranger, why: "x".into() }]);
        assert!(out.transitions.is_empty());
    }

    #[test]
    fn strangers_clamp_to_their_own_caps_and_floor() {
        let t = tuning();
        let mut a = girl(1);
        a.stranger_cap = 40.0;
        let mut out = Outcome::default();
        t.change(&with(a, "h2", 39.0, Stage::Stranger), &with(girl(2), "h1", -99.0, Stage::Stranger), 50.0, -50.0, "x", &mut out);
        assert_eq!((out.changes[0].favor_ab, out.changes[0].favor_ba), (40.0, -100.0));
        // No cap set: the default (100).
        let mut out = Outcome::default();
        t.change(&with(girl(1), "h2", 99.0, Stage::Stranger), &girl(2), 50.0, 0.0, "x", &mut out);
        assert_eq!(out.changes[0].favor_ab, 100.0);
    }

    #[test]
    fn friends_when_both_reach_their_caps_within_slack() {
        let t = tuning();
        let mut out = Outcome::default();
        t.change(&with(girl(1), "h2", 99.0, Stage::Stranger), &with(girl(2), "h1", 99.0, Stage::Stranger), 0.6, 0.6, "class", &mut out);
        assert_eq!(out.changes[0].stage, Stage::Friend);
        assert_eq!(out.transitions.len(), 1);
        assert_eq!((out.transitions[0].from, out.transitions[0].to), (Stage::Stranger, Stage::Friend));
        // Only one of them there: still strangers.
        let mut out = Outcome::default();
        t.change(&with(girl(1), "h2", 99.0, Stage::Stranger), &with(girl(2), "h1", 90.0, Stage::Stranger), 1.0, 1.0, "class", &mut out);
        assert_eq!(out.changes[0].stage, Stage::Stranger);
    }

    #[test]
    fn friends_cap_at_two_hundred() {
        let t = tuning();
        let mut out = Outcome::default();
        t.change(&with(girl(1), "h2", 199.0, Stage::Friend), &with(girl(2), "h1", 150.0, Stage::Friend), 5.0, 5.0, "x", &mut out);
        assert_eq!(out.changes[0].favor_ab, 200.0);
    }

    #[test]
    fn lovers_need_both_bisexual_and_both_at_180() {
        let t = tuning();
        let bi = |g| Heroine { bisexual: true, ..girl(g) };
        let mut out = Outcome::default();
        t.change(&with(bi(1), "h2", 179.0, Stage::Friend), &with(bi(2), "h1", 185.0, Stage::Friend), 1.0, 0.0, "x", &mut out);
        assert_eq!(out.changes[0].stage, Stage::Lover);
        let mut out = Outcome::default();
        t.change(&with(bi(1), "h2", 190.0, Stage::Friend), &with(girl(2), "h1", 190.0, Stage::Friend), 1.0, 1.0, "x", &mut out);
        assert_eq!(out.changes[0].stage, Stage::Friend, "a straight heroine never becomes a lover");
        let mut out = Outcome::default();
        t.change(&with(bi(1), "h2", 190.0, Stage::Friend), &with(bi(2), "h1", 170.0, Stage::Friend), 1.0, 1.0, "x", &mut out);
        assert_eq!(out.changes[0].stage, Stage::Friend);
    }

    #[test]
    fn falling_below_zero_ends_it_and_caps_at_stranger() {
        let t = tuning();
        let mut out = Outcome::default();
        t.change(&with(girl(1), "h2", 3.0, Stage::Lover), &with(girl(2), "h1", 190.0, Stage::Lover), -5.0, 0.0, "fight", &mut out);
        let c = &out.changes[0];
        assert_eq!((c.stage, c.favor_ab, c.favor_ba), (Stage::Stranger, -2.0, 100.0));
        assert_eq!((out.transitions[0].from, out.transitions[0].to), (Stage::Lover, Stage::Stranger));
    }

    #[test]
    fn events_use_their_tuning() {
        let t = tuning();
        let out = t.event("stood_up", &girl(1), &girl(2)).unwrap();
        assert_eq!((out.changes[0].favor_ab, out.changes[0].favor_ba), (5.0, 10.0));
        assert_eq!(out.changes[0].why, "他放我鴿子時和她在一起");
        let out = t.event("encounter_rivals", &girl(1), &girl(2)).unwrap();
        assert_eq!((out.changes[0].favor_ab, out.changes[0].favor_ba), (8.0, 8.0));
        assert!(t.event("nope", &girl(1), &girl(2)).is_err());
    }

    #[test]
    fn same_place_same_activity_grows_by_rate() {
        let t = tuning();
        let hs = [doing(girl(1), "work", "cafe"), doing(girl(2), "work", "cafe")];
        let out = t.tick(60.0, &hs);
        assert_eq!(out.changes.len(), 1);
        assert_eq!((out.changes[0].favor_ab, out.changes[0].why.as_str()), (12.0, "work"));
        // Rates by activity, and a quarter hour is a quarter of it.
        for (kind, gain) in [("class", 1.5), ("study", 1.5), ("social", 2.0), ("meal", 1.0), ("leisure", 1.0), ("rest", 0.5), ("sleep", 0.3)] {
            let out = t.tick(60.0, &[doing(girl(1), kind, "p"), doing(girl(2), kind, "p")]);
            assert!((out.changes[0].favor_ab - (10.0 + gain)).abs() < 1e-5, "{kind}");
        }
        let out = t.tick(15.0, &hs);
        assert!((out.changes[0].favor_ab - 10.5).abs() < 1e-5);
        // More than an hour since the last tick still counts as one hour.
        let out = t.tick(600.0, &hs);
        assert_eq!(out.changes[0].favor_ab, 12.0);
    }

    #[test]
    fn together_needs_same_place_activity_and_distance() {
        let t = tuning();
        assert!(t.tick(60.0, &[doing(girl(1), "work", "cafe"), doing(girl(2), "work", "bar")]).changes.is_empty());
        assert!(t.tick(60.0, &[doing(girl(1), "work", "cafe"), doing(girl(2), "meal", "cafe")]).changes.is_empty());
        assert!(t.tick(60.0, &[doing(girl(1), "work", ""), doing(girl(2), "work", "")]).changes.is_empty());
        assert!(t.tick(60.0, &[doing(girl(1), "walk", "p"), doing(girl(2), "walk", "p")]).changes.is_empty(), "no rate, no change");
        let mut far = doing(girl(2), "work", "cafe");
        far.pos = Some([1501.0, 0.0, 0.0]);
        assert!(t.tick(60.0, &[doing(girl(1), "work", "cafe"), far]).changes.is_empty());
        let mut upstairs = doing(girl(2), "work", "cafe");
        upstairs.pos = Some([0.0, 0.0, 151.0]);
        assert!(t.tick(60.0, &[doing(girl(1), "work", "cafe"), upstairs]).changes.is_empty());
        let mut away = doing(girl(2), "work", "cafe");
        away.pos = None;
        assert!(t.tick(60.0, &[doing(girl(1), "work", "cafe"), away]).changes.is_empty());
    }

    #[test]
    fn following_him_together_counts_anywhere() {
        let t = tuning();
        let a = Heroine { with_him: true, ..doing(girl(1), "work", "cafe") };
        let b = Heroine { with_him: true, ..doing(girl(2), "class", "school") };
        let out = t.tick(60.0, &[a, b]);
        assert_eq!((out.changes[0].favor_ab, out.changes[0].why.as_str()), (11.0, "一起跟著他"));
    }

    #[test]
    fn jealousy_slows_it_and_strong_jealousy_reverses_it() {
        let t = tuning();
        let mut a = doing(girl(1), "work", "cafe");
        a.jealousy.insert("h2".into(), 50.0);
        let mut b = doing(girl(2), "work", "cafe");
        b.jealousy.insert("h1".into(), 61.0);
        let out = t.tick(60.0, &[a, b]);
        assert_eq!((out.changes[0].favor_ab, out.changes[0].favor_ba), (11.0, 9.0));
    }

    #[test]
    fn mood_refuses_dislike_unless_girlfriend() {
        let t = tuning();
        let others = [girl(2), girl(3), girl(4)];
        assert_eq!(t.mood(&girl(1), &others), (0, None));
        let sulky = with(with(girl(1), "h2", -5.0, Stage::Stranger), "h3", -20.0, Stage::Stranger);
        assert_eq!(t.mood(&sulky, &others), (2, Some(3)));
        let gf = Heroine { girlfriend: true, ..sulky.clone() };
        assert_eq!(t.mood(&gf, &others), (1, Some(3)));
        // Herself in the list does not count.
        assert_eq!(t.mood(&with(girl(1), "h1", -50.0, Stage::Stranger), &[girl(1)]), (0, None));
    }

    #[test]
    fn json_requests() {
        let t = tuning();
        let v = eval(&t, "bonds_defaults", json!({})).unwrap();
        assert_eq!(v["start_favor"], 10.0);
        assert_eq!(v["tick_minutes"], 15);
        let v = eval(&t, "bond_event", json!({"event": "encounter", "a": {"g": 1, "id": "a"}, "b": {"g": 2, "id": "b", "toward": {"a": {"favor": 30.0, "stage": "stranger"}}}})).unwrap();
        assert_eq!(v["changes"][0]["favor_ba"], 31.0);
        assert_eq!(v["changes"][0]["stage"], "stranger");
        let v = eval(&t, "bond_mood", json!({"girl": {"g": 1, "id": "a", "toward": {"b": {"favor": -1.0}}}, "others": [{"g": 2, "id": "b"}]})).unwrap();
        assert_eq!((v["mood"].as_i64(), v["who"].as_i64()), (Some(2), Some(2)));
        assert!(eval(&t, "bonds_tick", json!({"heroines": []})).is_err(), "minutes is required");
    }
}
