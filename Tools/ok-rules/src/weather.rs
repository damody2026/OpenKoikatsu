//! Walking out in the rain (Docs/STREET_NEW_FEATURES_PLAN.md 2-5): whether they walk under an umbrella, one each or
//! the two of them under his, and what she is told when neither has one. Whether to go and buy one is hers to say.
use serde_json::{json, Value};

/// Request: raining, minute (of the day), his_items [ids], her_items [ids], stage (stranger | friend | lover).
/// Answer: style ("none" | "umbrella_single" | "umbrella_shared"), fact (for her words, "" when there is nothing to say).
fn walk_style(r: &Value) -> Value {
    let mut out = rain_style(r);
    // A walk late at night (walking her home after her shift): the street is empty but for the two of them.
    let minute = r.get("minute").and_then(Value::as_i64).unwrap_or(720);
    if !(5 * 60..21 * 60).contains(&minute) {
        // With others along (a group walking together) it is not "just the two of you".
        let company = r.get("company").and_then(Value::as_i64).unwrap_or(0);
        let night = crate::tr::t(if company > 0 { "weather.night_walk_group" } else { "weather.night_walk" });
        let fact = out["fact"].as_str().unwrap_or("").to_string();
        out["fact"] = json!(if fact.is_empty() { night.to_string() } else { format!("{fact}{}{night}", crate::tr::t("items.fact_separator")) });
    }
    out
}

fn rain_style(r: &Value) -> Value {
    let raining = r.get("raining").and_then(Value::as_bool).unwrap_or(false);
    if !raining {
        return json!({"style": "none", "fact": ""});
    }
    let has = |k: &str| r.get(k).and_then(Value::as_array).is_some_and(|a| a.iter().any(|v| v.as_str().is_some_and(|s| s.contains("umbrella"))));
    // Hers: one in her things, or the folding one she brought today (items.rs she_has_umbrella).
    let girl = r.get("girl").and_then(Value::as_str).unwrap_or("");
    // His: one in his bag, or hers that she lent him.
    let his = has("his_items") || crate::items::he_borrowed_umbrella(r);
    let hers = has("her_items") || (!girl.is_empty() && crate::items::she_has_umbrella(r, girl));
    let close = r.get("stage").and_then(Value::as_str) == Some("lover");
    if his && (!hers || close) {
        // One umbrella between them when they are that close (or his is the only one): under his.
        let shared = close || !hers;
        return json!({"style": if shared { "umbrella_shared" } else { "umbrella_single" }, "fact": crate::tr::t(if shared { "weather.under_his_umbrella" } else { "weather.each_umbrella" })});
    }
    if his && hers {
        return json!({"style": "umbrella_single", "fact": crate::tr::t("weather.each_umbrella")});
    }
    if hers {
        // Only hers: the two of them under it.
        return json!({"style": "umbrella_shared", "fact": crate::tr::t("weather.under_her_umbrella")});
    }
    json!({"style": "none", "fact": crate::tr::t("weather.no_umbrella")})
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "walk_style" => walk_style(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rain_umbrellas_and_closeness() {
        assert_eq!(walk_style(&json!({"raining": false, "his_items": ["cv_umbrella"]}))["style"], "none");
        assert_eq!(walk_style(&json!({"raining": true, "his_items": ["cv_umbrella"], "stage": "lover"}))["style"], "umbrella_shared");
        assert_eq!(walk_style(&json!({"raining": true, "his_items": ["cv_umbrella"], "her_items": ["cv_umbrella"], "stage": "friend"}))["style"], "umbrella_single");
        let none = walk_style(&json!({"raining": true, "stage": "friend"}));
        assert_eq!(none["style"], "none");
        assert!(!none["fact"].as_str().unwrap().is_empty());
        // Only she has one: the two of them under hers.
        assert_eq!(walk_style(&json!({"raining": true, "her_items": ["cv_umbrella"], "stage": "friend"}))["style"], "umbrella_shared");
        // Her folding umbrella is fixed for the day: asked twice, the same answer; on some days she has one, on some not.
        let day = |d: i64| walk_style(&json!({"raining": true, "girl": "apng-x", "day": d, "stage": "friend"}))["style"].clone();
        assert_eq!(day(3), day(3));
        assert!((0..20).any(|d| day(d) == "umbrella_shared") && (0..20).any(|d| day(d) == "none"));
    }

    #[test]
    fn late_at_night_she_is_told_the_street_is_empty() {
        assert_eq!(walk_style(&json!({"raining": false, "minute": 15 * 60}))["fact"], "");
        assert!(!walk_style(&json!({"raining": false, "minute": 22 * 60 + 10}))["fact"].as_str().unwrap().is_empty());
        // Others walking with them: not "just the two of you".
        let alone = walk_style(&json!({"raining": false, "minute": 22 * 60}))["fact"].clone();
        let group = walk_style(&json!({"raining": false, "minute": 22 * 60, "company": 1}))["fact"].clone();
        assert_eq!(alone, crate::tr::t("weather.night_walk"));
        assert_eq!(group, crate::tr::t("weather.night_walk_group"));
    }
}
