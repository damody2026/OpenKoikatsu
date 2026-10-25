//! The heroines' needs (0..100): they decay with game time, and what her day has her doing gives some back (rest and
//! sleep give energy, a meal fills her, a bath cleans her, fun and company lift her). Gentle on purpose: not a survival
//! game, needs colour her mood and plans and should rarely take over her day.
//!
//! Unreal owns the numbers (the save) and when each heroine was last updated; a request carries each heroine's needs,
//! the hours since then and her current activity, and the answer is her new needs.
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct NeedsTuning {
    pub tick_minutes: i64,
    pub max_hours: f32,
    /// After a jump of more than max_hours (he slept through to the next morning, skipped the day): she ate her
    /// meals off screen, so she is at least this full. Without it each skip took a few hours of decay and no meal.
    pub after_gap_satiety: f32,
    pub day_start_hour: i32,
    pub day_end_hour: i32,
    pub decay: Decay,
    pub gain: HashMap<String, Gain>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct Decay {
    pub satiety_day: f32,
    pub satiety_night: f32,
    pub energy_day: f32,
    pub energy_night: f32,
    pub fun: f32,
    pub social: f32,
    pub hygiene: f32,
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields, default)]
pub struct Gain {
    pub satiety: f32,
    pub energy: f32,
    pub hygiene: f32,
    pub hygiene_home: f32,
    pub fun: f32,
    pub social: f32,
}

#[derive(Serialize, Deserialize, Debug, Clone, Copy, PartialEq, Default)]
pub struct Needs {
    pub satiety: f32,
    pub energy: f32,
    pub fun: f32,
    pub social: f32,
    pub hygiene: f32,
}

/// One heroine's needs and what she is doing.
#[derive(Deserialize, Debug, Clone, Default)]
pub struct Heroine {
    pub id: String,
    /// Game minutes since her needs were last updated.
    pub minutes: f64,
    pub needs: Needs,
    /// Her life plan's current block kind (empty: nothing gives back).
    #[serde(default)]
    pub activity: String,
    /// The block is at her home (or the dorm).
    #[serde(default)]
    pub at_home: bool,
}

#[derive(Serialize, Debug, Clone, PartialEq)]
pub struct Updated {
    pub id: String,
    pub needs: Needs,
}

/// Unreal's FMath::Clamp: never panics.
fn clamp(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo {
        lo
    } else if x < hi {
        x
    } else {
        hi
    }
}

fn pct(x: f32) -> f32 {
    clamp(x, 0.0, 100.0)
}

impl NeedsTuning {
    pub fn day(&self, hour: i32) -> bool {
        hour >= self.day_start_hour && hour < self.day_end_hour
    }

    /// Decay by the hours passed (each need clamped), then what she is doing gives back (clamped again).
    pub fn advance(&self, n: Needs, hours: f32, day: bool, activity: &str, at_home: bool) -> Needs {
        let d = &self.decay;
        let mut n = Needs {
            satiety: pct(n.satiety - hours * if day { d.satiety_day } else { d.satiety_night }),
            energy: pct(n.energy - hours * if day { d.energy_day } else { d.energy_night }),
            fun: pct(n.fun - hours * d.fun),
            social: pct(n.social - hours * d.social),
            hygiene: pct(n.hygiene - hours * d.hygiene),
        };
        if let Some(g) = self.gain.get(activity).filter(|_| !activity.is_empty()) {
            let hygiene = if at_home { g.hygiene.max(g.hygiene_home) } else { g.hygiene };
            n.energy = pct(n.energy + g.energy * hours);
            n.satiety = pct(n.satiety + g.satiety * hours);
            n.hygiene = pct(n.hygiene + hygiene * hours);
            n.fun = pct(n.fun + g.fun * hours);
            n.social = pct(n.social + g.social * hours);
        }
        n
    }

    /// Every heroine whose clock moved: her new needs. `hour`: the game hour now (day or night decay).
    pub fn tick(&self, hour: i32, heroines: &[Heroine]) -> Vec<Updated> {
        let day = self.day(hour);
        heroines
            .iter()
            .filter_map(|h| {
                let hours = clamp((h.minutes / 60.0) as f32, 0.0, self.max_hours);
                (hours > 0.0).then(|| {
                    let mut needs = self.advance(h.needs, hours, day, &h.activity, h.at_home);
                    if (h.minutes / 60.0) as f32 > self.max_hours {
                        needs.satiety = needs.satiety.max(self.after_gap_satiety);
                    }
                    Updated { id: h.id.clone(), needs }
                })
            })
            .collect()
    }
}

fn field<T: serde::de::DeserializeOwned>(request: &Value, name: &str) -> Result<T, String> {
    let v = request.get(name).cloned().ok_or_else(|| format!("missing_field: {name}"))?;
    serde_json::from_value(v).map_err(|e| format!("{name}: {e}"))
}

/// None: not a needs request.
pub fn eval(t: &NeedsTuning, kind: &str, request: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "needs_defaults" => Ok(json!({"tick_minutes": t.tick_minutes})),
        "needs_tick" => (|| {
            let hour: i32 = field(request, "hour")?;
            let heroines: Vec<Heroine> = field(request, "heroines")?;
            Ok(json!({"heroines": t.tick(hour, &heroines)}))
        })(),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    fn tuning() -> NeedsTuning {
        Tuning::builtin().needs
    }

    fn full() -> Needs {
        Needs { satiety: 80.0, energy: 90.0, fun: 60.0, social: 60.0, hygiene: 90.0 }
    }

    fn close(a: Needs, b: Needs) -> bool {
        [(a.satiety, b.satiety), (a.energy, b.energy), (a.fun, b.fun), (a.social, b.social), (a.hygiene, b.hygiene)].iter().all(|(x, y)| (x - y).abs() < 1e-4)
    }

    #[test]
    fn awake_hour_decays_by_the_day_rates() {
        let n = tuning().advance(full(), 1.0, true, "", false);
        assert!(close(n, Needs { satiety: 75.5, energy: 88.0, fun: 58.5, social: 58.75, hygiene: 89.0 }), "{n:?}");
    }

    #[test]
    fn night_is_slower_for_hunger_and_tiredness() {
        let n = tuning().advance(full(), 2.0, false, "", false);
        assert!(close(n, Needs { satiety: 77.0, energy: 88.0, fun: 57.0, social: 57.5, hygiene: 88.0 }), "{n:?}");
    }

    #[test]
    fn day_is_seven_to_twenty_three() {
        let t = tuning();
        assert!(!t.day(6) && t.day(7) && t.day(22) && !t.day(23));
    }

    #[test]
    fn what_she_does_gives_back() {
        let t = tuning();
        let low = Needs { satiety: 20.0, energy: 20.0, fun: 20.0, social: 20.0, hygiene: 20.0 };
        let meal = t.advance(low, 1.0, true, "meal", false);
        assert!(close(meal, Needs { satiety: 40.5, energy: 20.0, fun: 18.5, social: 20.75, hygiene: 19.0 }), "{meal:?}");
        let rest_away = t.advance(low, 1.0, true, "rest", false);
        let rest_home = t.advance(low, 1.0, true, "rest", true);
        assert!((rest_away.hygiene - 19.0).abs() < 1e-4 && (rest_home.hygiene - 22.0).abs() < 1e-4);
        assert!((rest_home.energy - 32.0).abs() < 1e-4);
        let bath_home = t.advance(low, 0.5, true, "bath", true);
        assert!((bath_home.hygiene - 49.5).abs() < 1e-4, "a bath at home is still a bath: {bath_home:?}");
        let social = t.advance(low, 1.0, true, "social", false);
        assert!((social.fun - 24.5).abs() < 1e-4 && (social.social - 28.75).abs() < 1e-4);
        for kind in ["class", "work", "encounter"] {
            assert!(t.advance(low, 1.0, true, kind, false).social > 20.0, "{kind}");
        }
        assert!(close(t.advance(low, 1.0, true, "walk", false), t.advance(low, 1.0, true, "", false)), "unknown activity: decay only");
    }

    #[test]
    fn values_stay_in_range() {
        let t = tuning();
        let empty = t.advance(Needs::default(), 6.0, true, "", false);
        assert_eq!(empty, Needs::default());
        let max = Needs { satiety: 100.0, energy: 100.0, fun: 100.0, social: 100.0, hygiene: 100.0 };
        let n = t.advance(max, 0.25, true, "bath", true);
        assert_eq!(n.hygiene, 100.0);
    }

    #[test]
    fn tick_caps_hours_and_skips_unmoved_clocks() {
        let t = tuning();
        let hs = [
            Heroine { id: "a".into(), minutes: 15.0, needs: full(), ..Default::default() },
            Heroine { id: "b".into(), minutes: 0.0, needs: full(), ..Default::default() },
            Heroine { id: "c".into(), minutes: 60.0 * 48.0, needs: full(), ..Default::default() },
        ];
        let out = t.tick(12, &hs);
        assert_eq!(out.iter().map(|u| u.id.as_str()).collect::<Vec<_>>(), ["a", "c"]);
        assert!((out[0].needs.satiety - (80.0 - 4.5 * 0.25)).abs() < 1e-4);
        // Two days away cost at most six hours, and she ate on her own meanwhile (after_gap_satiety).
        assert!((out[1].needs.satiety - (80.0f32 - 4.5 * 6.0).max(t.after_gap_satiety)).abs() < 1e-4, "two days away cost at most six hours");
    }

    #[test]
    fn thirty_days_of_her_plan_stay_sane() {
        // Thirty game days in 15-minute ticks with a plain day (sleep, meals, class, leisure): nothing leaves 0..100 or
        // goes NaN, every day's meals lift her above 20 (it settles near 24) and the nights rest her. In the game what she
        // orders adds to the meal block, so this is the floor.
        let t = tuning();
        let mut n = full();
        for day in 0..30 {
            let (mut best_satiety, mut best_energy) = (0.0f32, 0.0f32);
            for q in 0..96 {
                let hour = q / 4;
                let activity = match hour {
                    0..=6 | 23 => "sleep",
                    7 | 12 | 18 => "meal",
                    8..=11 | 13..=16 => "class",
                    17 | 19 | 20 => "leisure",
                    _ => "rest",
                };
                n = t.advance(n, 0.25, t.day(hour), activity, hour < 7 || hour >= 21);
                for v in [n.satiety, n.energy, n.fun, n.social, n.hygiene] {
                    assert!(v.is_finite() && (0.0..=100.0).contains(&v), "{n:?}");
                }
                best_satiety = best_satiety.max(n.satiety);
                best_energy = best_energy.max(n.energy);
            }
            assert!(best_satiety > 20.0 && best_energy > 60.0, "day {day}: best satiety {best_satiety}, energy {best_energy}");
        }
    }

    #[test]
    fn json_requests() {
        let t = tuning();
        assert_eq!(eval(&t, "needs_defaults", &json!({})).unwrap().unwrap()["tick_minutes"], 15);
        let v = eval(&t, "needs_tick", &json!({"hour": 3, "heroines": [{"id": "a", "minutes": 60, "needs": {"satiety": 50, "energy": 50, "fun": 50, "social": 50, "hygiene": 50}, "activity": "sleep", "at_home": true}]})).unwrap().unwrap();
        assert_eq!(v["heroines"][0]["needs"]["energy"], 61.0);
        assert_eq!(v["heroines"][0]["needs"]["hygiene"], 52.0);
        assert!(eval(&t, "bonds_tick", &json!({})).is_none());
        assert!(eval(&t, "needs_tick", &json!({"heroines": []})).unwrap().is_err());
        // Skipped to the next morning (a day passed off screen): she ate on her own, not left near empty.
        let skipped = eval(&t, "needs_tick", &json!({"hour": 7, "heroines": [{"id": "a", "minutes": 1440, "needs": {"satiety": 20, "energy": 95, "fun": 50, "social": 50, "hygiene": 90}, "activity": "sleep", "at_home": true}]})).unwrap().unwrap();
        assert!(skipped["heroines"][0]["needs"]["satiety"].as_f64().unwrap() >= t.after_gap_satiety as f64);
    }
}
