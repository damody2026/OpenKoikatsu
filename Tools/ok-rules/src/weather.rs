//! Walking out in the rain (Docs/STREET_NEW_FEATURES_PLAN.md 2-5): whether they walk under an umbrella, one each or
//! the two of them under his, and what she is told when neither has one. Whether to go and buy one is hers to say.
use serde_json::{json, Value};

/// Request: raining, his_items [ids], her_items [ids], stage (stranger | friend | lover). Answer: style
/// ("none" | "umbrella_single" | "umbrella_shared"), fact (for her words, "" when there is nothing to say).
fn walk_style(r: &Value) -> Value {
    let raining = r.get("raining").and_then(Value::as_bool).unwrap_or(false);
    if !raining {
        return json!({"style": "none", "fact": ""});
    }
    let has = |k: &str| r.get(k).and_then(Value::as_array).is_some_and(|a| a.iter().any(|v| v.as_str().is_some_and(|s| s.contains("umbrella"))));
    let (his, hers) = (has("his_items"), has("her_items"));
    let close = r.get("stage").and_then(Value::as_str) == Some("lover");
    if his && (!hers || close) {
        // One umbrella between them when they are that close (or hers is the only other one): under his.
        let shared = close || !hers;
        return json!({"style": if shared { "umbrella_shared" } else { "umbrella_single" }, "fact": crate::tr::t(if shared { "weather.under_his_umbrella" } else { "weather.each_umbrella" })});
    }
    if his || hers {
        return json!({"style": "umbrella_single", "fact": crate::tr::t("weather.each_umbrella")});
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
    }
}
