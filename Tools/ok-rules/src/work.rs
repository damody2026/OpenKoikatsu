//! Helping her with work of her own (Docs/STREET_NEW_FEATURES_PLAN.md 2-3): her profile's occupation.side_task (Hu Tao
//! handing out flyers for the parlour). How it went is counted here from the place's crowd and the weather; what she
//! thinks of it is the AI's, told the counts. Her progress over time is kept in the save's `ext`.
use crate::rng::RandomStream;
use serde_json::{json, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

/// They helped her for a while. Request: ext, girl, girl_name, task, place_name, minutes, crowd (0..1), raining, seed.
/// Answer: ext, handed (flyers or tasks done), stopped (people who stopped to look), progress (her total), fact.
fn end(r: &Value) -> Value {
    let girl = s(r, "girl");
    let minutes = r.get("minutes").and_then(Value::as_f64).unwrap_or(60.0).max(0.0);
    let crowd = r.get("crowd").and_then(Value::as_f64).unwrap_or(0.3).clamp(0.0, 1.0);
    let rain = r.get("raining").and_then(Value::as_bool).unwrap_or(false);
    let mut rng = RandomStream::new(r.get("seed").and_then(Value::as_i64).unwrap_or(1) as i32);
    // Two people, about one flyer a minute each where people pass; fewer take one in the rain.
    let per_minute = 2.0 * (0.25 + crowd) * if rain { 0.4 } else { 1.0 };
    let handed = (minutes * per_minute * (0.85 + 0.3 * rng.frand() as f64)).round() as i64;
    let stopped = ((handed as f64) * (0.08 + 0.1 * crowd)).round() as i64;
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    let before = ext.get("work").and_then(|w| w.get(girl)).and_then(Value::as_i64).unwrap_or(0);
    let progress = before + handed;
    ext["work"][girl] = json!(progress);
    let fact = crate::tr::m(
        "work.helped_fact",
        &[
            ("task", crate::tr::a(s(r, "task"))),
            ("place", crate::tr::a(s(r, "place_name"))),
            ("handed", crate::tr::a(&handed.to_string())),
            ("stopped", crate::tr::a(&stopped.to_string())),
            ("weather", crate::tr::a(if rain { crate::tr::t("work.in_rain") } else { "" })),
        ],
    );
    json!({"ext": ext, "handed": handed, "stopped": stopped, "progress": progress, "fact": fact})
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "help_work_end" => end(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rain_and_an_empty_street_hand_out_less_and_progress_adds_up() {
        let dry = end(&json!({"ext": {}, "girl": "h", "minutes": 60, "crowd": 0.8, "raining": false, "seed": 3}));
        let wet = end(&json!({"ext": {}, "girl": "h", "minutes": 60, "crowd": 0.8, "raining": true, "seed": 3}));
        assert!(wet["handed"].as_i64().unwrap() < dry["handed"].as_i64().unwrap() / 2);
        let again = end(&json!({"ext": dry["ext"], "girl": "h", "minutes": 60, "crowd": 0.1, "raining": false, "seed": 4}));
        assert_eq!(again["progress"].as_i64().unwrap(), dry["handed"].as_i64().unwrap() + again["handed"].as_i64().unwrap());
        assert!(!again["fact"].as_str().unwrap().is_empty());
    }
}
