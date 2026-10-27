//! A heroine's relationship record as the rules see it: the scalar part of Unreal's FOKStreetRelation, under the same
//! names both sides use (OKStreetSocialRulesGlue.inl OKRulesRelationJson / OKRulesPatchRelation). Missing fields take
//! the C++ defaults. Rules change a copy; `patch` sends back only what changed.
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::BTreeMap;

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Relation {
    pub id: String,
    pub favor: f32,
    pub lewdness: f32,
    pub stage: String,
    pub talks_remaining: i32,
    pub day: i32,
    pub sex_desire: f32,
    pub topic_points: f32,
    // Axes 0..100, written only by rules.
    pub trust: f32,
    pub admiration: f32,
    pub attraction: f32,
    pub dependency: f32,
    pub mystique: f32,
    pub jealousy: f32,
    pub greed: f32,
    pub lifestyle_desire: f32,
    pub respect: f32,
    pub resentment: f32,
    pub jealousy_toward: BTreeMap<String, f32>,
    // Mood: last emotion, its intensity, the game minute it was set.
    pub emotion: String,
    pub emotion_intensity: f32,
    pub emotion_minute: i64,
    /// Her spirits (R3-269): -100 low .. 100 high, 0 her usual self, easing back to it (half every MOOD_HALF_MINUTES);
    /// what lifts or sinks it is an event's (her explosion spell coming off or fizzling). mood_minute: when it was set.
    pub mood_level: f32,
    pub mood_minute: i64,
    // Money: cash -1 until her profile is known.
    pub cash: i64,
    pub bank: i64,
    pub owes_player: i64,
    pub debt: i64,
    pub treat_balance: i32,
    // Needs 0..100 and the minute they were last updated.
    pub satiety: f32,
    pub energy: f32,
    pub fun: f32,
    pub social: f32,
    pub hygiene: f32,
    pub needs_minute: i64,
    pub perceived_wealth: f32,
    /// -1: not set (her personality decides).
    pub chastity: f32,
    /// 0: not set.
    pub stranger_cap: i32,
    pub flags: Vec<String>,
    // The talk core's record (sent only to the talk rules): lines talked, transactions committed, topics talked
    // about, and cooldowns (id -> the day it may be said again).
    pub talked: Vec<String>,
    pub committed: Vec<String>,
    pub topics: Vec<String>,
    pub cooldowns: BTreeMap<String, i32>,
}

impl Default for Relation {
    fn default() -> Self {
        Relation {
            id: String::new(),
            favor: 0.0,
            lewdness: 0.0,
            stage: "stranger".into(),
            talks_remaining: 0,
            day: 1,
            sex_desire: 0.0,
            topic_points: 0.0,
            trust: 20.0,
            admiration: 0.0,
            attraction: 10.0,
            dependency: 0.0,
            mystique: 0.0,
            jealousy: 0.0,
            greed: 0.0,
            lifestyle_desire: 0.0,
            respect: 20.0,
            resentment: 0.0,
            jealousy_toward: BTreeMap::new(),
            emotion: String::new(),
            emotion_intensity: 0.0,
            emotion_minute: 0,
            mood_level: 0.0,
            mood_minute: 0,
            cash: -1,
            bank: 0,
            owes_player: 0,
            debt: 0,
            treat_balance: 0,
            satiety: 80.0,
            energy: 90.0,
            fun: 60.0,
            social: 60.0,
            hygiene: 90.0,
            needs_minute: -1,
            perceived_wealth: 20.0,
            chastity: -1.0,
            stranger_cap: 0,
            flags: Vec::new(),
            talked: Vec::new(),
            committed: Vec::new(),
            topics: Vec::new(),
            cooldowns: BTreeMap::new(),
        }
    }
}

/// The axes by name, in the save's order.
pub const AXES: [&str; 10] = ["trust", "admiration", "attraction", "dependency", "mystique", "jealousy", "greed", "lifestyle_desire", "respect", "resentment"];

/// Favor caps by stage when her own is not known (rules.json stepup limits: stranger 75, friend 200 at least, lover 150).
pub const STRANGER_FAVOR_CAP: f32 = 75.0;
pub const FRIEND_FAVOR_CAP: f32 = 200.0;
pub const LOVER_FAVOR_CAP: f32 = 150.0;

/// How much of a favor gain counts while he already has `lovers` other lovers and she is not one of them: winning one
/// more is 1/lovers as easy, never below 1/5. Losses are not scaled; lovers themselves are not.
pub fn lover_gain_scale(stage: &str, lovers: i32) -> f32 {
    if stage == "lover" {
        1.0
    } else {
        (1.0 / lovers.max(1) as f32).max(0.2)
    }
}

/// Her liking for him on one 0..100 scale across the stages (stranger 0-35, friend 35-65, lover 65-100), read from her
/// favor inside the stage. Favor is the only stored feeling; rules that ask "how much does she like him" use this.
pub fn liking_band(stage: &str) -> (f32, f32) {
    match stage {
        "lover" => (65.0, 100.0),
        "friend" => (35.0, 65.0),
        _ => (0.0, 35.0),
    }
}

impl Relation {
    /// What she can pay with: her cash and her bank account (a debit card), 0 while her wallet is unknown.
    pub fn funds(&self) -> i64 {
        if self.cash < 0 {
            0
        } else {
            self.cash + self.bank.max(0)
        }
    }

    /// She pays: cash first, then from the bank; returns what she could not cover (0: all paid).
    pub fn pay(&mut self, amount: i64) -> i64 {
        if self.cash < 0 || amount <= 0 {
            return amount.max(0);
        }
        let from_cash = self.cash.min(amount);
        self.cash -= from_cash;
        let from_bank = self.bank.max(0).min(amount - from_cash);
        self.bank -= from_bank;
        amount - from_cash - from_bank
    }

    pub fn stage_index(&self) -> i32 {
        stage_index(&self.stage)
    }

    pub fn axis_mut(&mut self, name: &str) -> Option<&mut f32> {
        Some(match name {
            "trust" => &mut self.trust,
            "admiration" => &mut self.admiration,
            "attraction" => &mut self.attraction,
            "dependency" => &mut self.dependency,
            "mystique" => &mut self.mystique,
            "jealousy" => &mut self.jealousy,
            "greed" => &mut self.greed,
            "lifestyle_desire" => &mut self.lifestyle_desire,
            "respect" => &mut self.respect,
            "resentment" => &mut self.resentment,
            _ => return None,
        })
    }

    pub fn axis(&self, name: &str) -> Option<f32> {
        if name == "affection" {
            return Some(self.liking());
        }
        self.clone().axis_mut(name).map(|v| *v)
    }

    /// Adds to an axis, kept in 0..100. "affection" (the old liking axis, still named in data) moves her favor.
    pub fn add_axis(&mut self, name: &str, by: f32) {
        if name == "affection" {
            self.add_liking(by);
        } else if let Some(v) = self.axis_mut(name) {
            *v = (*v + by).clamp(0.0, 100.0);
        }
    }

    /// Her favor cap at her stage (talk.rs favor_limit with the default table).
    pub fn favor_cap(&self) -> f32 {
        match self.stage.as_str() {
            "lover" => LOVER_FAVOR_CAP,
            "friend" => FRIEND_FAVOR_CAP,
            _ if self.stranger_cap > 0 => self.stranger_cap.clamp(10, 100) as f32,
            _ => STRANGER_FAVOR_CAP,
        }
    }

    /// How much she likes him, 0..100 (see liking_band).
    pub fn liking(&self) -> f32 {
        let (low, high) = liking_band(&self.stage);
        low + (high - low) * (self.favor / self.favor_cap().max(1.0)).clamp(0.0, 1.0)
    }

    /// Her favor set so that her liking reads `liking` (clamped to her stage's band).
    pub fn set_liking(&mut self, liking: f32) {
        let (low, high) = liking_band(&self.stage);
        self.favor = ((liking - low) / (high - low)).clamp(0.0, 1.0) * self.favor_cap();
    }

    /// Like `set_liking`, for building one.
    pub fn with_liking(mut self, liking: f32) -> Self {
        self.set_liking(liking);
        self
    }

    /// Liking up or down by `by` points of the 0..100 scale: her favor moves by the same share of her stage, kept in
    /// 0..cap (a stage is never left this way).
    pub fn add_liking(&mut self, by: f32) {
        let (low, high) = liking_band(&self.stage);
        let cap = self.favor_cap();
        self.favor = (self.favor + by / (high - low) * cap).clamp(0.0, cap);
    }

    pub fn has_flag(&self, flag: &str) -> bool {
        self.flags.iter().any(|f| f == flag)
    }

    pub fn add_flag(&mut self, flag: &str) {
        if !self.has_flag(flag) {
            self.flags.push(flag.into());
        }
    }

    /// Her spirits at game minute `now`: eased back towards 0 since they were set.
    pub fn mood_at(&self, now: i64) -> f32 {
        let minutes = (now - self.mood_minute).max(0) as f32;
        self.mood_level * 0.5f32.powf(minutes / MOOD_HALF_MINUTES)
    }

    /// Lifts (or sinks) her spirits by `by` at minute `now` (-100..100).
    pub fn add_mood(&mut self, by: f32, now: i64) {
        self.mood_level = (self.mood_at(now) + by).clamp(-100.0, 100.0);
        self.mood_minute = now;
    }

    /// The fields that differ from `before`, by name (what Unreal writes back).
    pub fn patch(&self, before: &Relation) -> Map<String, Value> {
        let (Value::Object(now), Value::Object(was)) = (serde_json::to_value(self).unwrap(), serde_json::to_value(before).unwrap()) else {
            return Map::new();
        };
        now.into_iter().filter(|(k, v)| was.get(k) != Some(v)).collect()
    }
}

/// Game minutes for her spirits to come halfway back to her usual self.
pub const MOOD_HALF_MINUTES: f32 = 360.0;

/// Her spirits in a word for the panel and her prompt (low, down, usual, good, high).
pub fn mood_band(level: f32) -> &'static str {
    if level <= -45.0 { "low" } else if level <= -15.0 { "down" } else if level < 15.0 { "usual" } else if level < 45.0 { "good" } else { "high" }
}

pub fn stage_index(stage: &str) -> i32 {
    match stage {
        "lover" => 2,
        "friend" => 1,
        _ => 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn defaults_match_unreal() {
        let r: Relation = serde_json::from_value(json!({"id": "a"})).unwrap();
        assert_eq!((r.stage.as_str(), r.trust, r.respect, r.cash, r.chastity, r.satiety), ("stranger", 20.0, 20.0, -1, -1.0, 80.0));
    }

    #[test]
    fn patch_lists_only_changes() {
        let before = Relation { id: "a".into(), ..Default::default() };
        let mut after = before.clone();
        after.add_axis("trust", 500.0);
        after.add_flag("met");
        let p = after.patch(&before);
        assert_eq!(p.len(), 2);
        assert_eq!(p["trust"], 100.0);
        assert_eq!(p["flags"], json!(["met"]));
    }
}
