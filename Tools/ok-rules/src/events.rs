//! Festivals and the term's big weeks (rules/events.toml, Docs/STREET_NEW_FEATURES_PLAN.md 4-1): which is on a day,
//! and which is coming (she hears of it some days ahead and decides herself whether to bring it up or ask him). The
//! table is installed with the other tuning, so plan.rs and the rest ask by day without carrying it.
use crate::calendar::{date, days_from_civil};
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::RwLock;

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct Event {
    pub id: String,
    #[serde(default)]
    pub year: i64,
    pub month: u32,
    pub day: u32,
    #[serde(default = "one")]
    pub days: i64,
    pub text: String,
    #[serde(default)]
    pub exam: bool,
    #[serde(default)]
    pub announce: i64,
}

fn one() -> i64 {
    1
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct EventsTuning {
    #[serde(default)]
    pub event: Vec<Event>,
}

static TABLE: RwLock<Option<EventsTuning>> = RwLock::new(None);
static BUILTIN: std::sync::LazyLock<EventsTuning> = std::sync::LazyLock::new(|| crate::tuning::Tuning::builtin().events);

pub fn install(t: &EventsTuning) {
    if let Ok(mut g) = TABLE.write() {
        *g = Some(t.clone());
    }
}

/// The game day an event starts in the year of this game day (or its own year), or None.
fn start_day(e: &Event, game_day: i32) -> Option<i64> {
    let d = date(game_day, 12.0);
    let year = if e.year > 0 { e.year } else { d.year };
    let epoch = days_from_civil(2010, 9, 1);
    let mut start = days_from_civil(year, e.month, e.day) - epoch;
    // An event every year already gone this year: next year's.
    if e.year == 0 && start + e.days <= game_day as i64 {
        start = days_from_civil(year + 1, e.month, e.day) - epoch;
    }
    Some(start)
}

/// The events on this day.
pub fn on(game_day: i32) -> Vec<Event> {
    let Ok(g) = TABLE.read() else { return vec![] };
    let t = g.as_ref().unwrap_or(&BUILTIN);
    t.event.iter().filter(|e| start_day(e, game_day).is_some_and(|s| (s..s + e.days).contains(&(game_day as i64)))).cloned().collect()
}

/// What the day is (the first event's words), "" for an ordinary day.
pub fn holiday_text(game_day: i32) -> &'static str {
    on(game_day).first().map(|e| crate::tr::lookup(&e.text, "")).unwrap_or("")
}

pub fn is_exam(game_day: i32) -> bool {
    on(game_day).iter().any(|e| e.exam)
}

/// For her words: what is on today and what is coming within its announce days. Request: day. Answer: text, today [ids].
fn facts(r: &Value) -> Value {
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    let Ok(g) = TABLE.read() else { return json!({"text": "", "today": []}) };
    let t = g.as_ref().unwrap_or(&BUILTIN);
    let mut parts = Vec::new();
    let mut today = Vec::new();
    for e in &t.event {
        let Some(s) = start_day(e, day) else { continue };
        let what = crate::tr::lookup(&e.text, "");
        if (s..s + e.days).contains(&(day as i64)) {
            today.push(json!(e.id));
            parts.push(crate::tr::m("events.today", &[("what", crate::tr::a(what))]));
        } else if s > day as i64 && s - (day as i64) <= e.announce {
            let d = date(s as i32, 12.0);
            parts.push(crate::tr::m("events.coming", &[("what", crate::tr::a(what)), ("date", crate::tr::a(&format!("{}/{}", d.month, d.day))), ("days", crate::tr::a(&(s - day as i64).to_string()))]));
        }
    }
    json!({"text": parts.join(crate::tr::t("items.fact_separator")), "today": today})
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "events_facts" => facts(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn festivals_by_day_and_coming_ones_told_ahead() {
        let t = Tuning::builtin();
        install(&t.events);
        assert!(!holiday_text(21).is_empty(), "2010-09-22 mid-autumn");
        assert_eq!(holiday_text(5), "");
        assert!(is_exam(69), "midterm week");
        // Halloween (10/31, day 60) is told from a week before.
        let f = facts(&json!({"day": 55}));
        assert!(!f["text"].as_str().unwrap().is_empty());
        assert_eq!(facts(&json!({"day": 40}))["text"], "");
        assert_eq!(facts(&json!({"day": 60}))["today"], json!(["halloween"]));
    }
}
