//! Meetings that repeat (Docs/STREET_NEW_FEATURES_PLAN.md 0-4): "every Wednesday at seven in the library". The judge
//! reads the repeat from what they said (promise.repeat {"weekly": "wed"}); here the series is kept (the save's ext)
//! and the next meeting of each is booked a week ahead, one at a time. Calling it off is this one or all of them
//! (judge cancel_scope; asked when it could not tell).
use serde_json::{json, Value};

const DAYS: [&str; 7] = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"];

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

fn list(r: &Value) -> Vec<Value> {
    r.get("ext").and_then(|e| e.get("series")).and_then(Value::as_array).cloned().unwrap_or_default()
}

fn with_list(r: &Value, l: Vec<Value>) -> Value {
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    ext["series"] = Value::Array(l);
    ext
}

/// The weekday of a game minute (Monday 0), as calendar.rs counts it.
fn weekday(minute: i64) -> i64 {
    crate::calendar::date((minute / 1440) as i32, ((minute % 1440) as f64) / 60.0).weekday as i64
}

/// A meeting just booked is the first of a series. Request: ext, heroine, start (game minute), place, label, repeat
/// ({"weekly": "wed"} or "weekly:wed"). Answer: ext, added (bool), weekday, label (what the list shows).
fn add(r: &Value) -> Value {
    let rep = r.get("repeat").cloned().unwrap_or(Value::Null);
    let day = rep.get("weekly").and_then(Value::as_str).map(str::to_string).or_else(|| rep.as_str().and_then(|t| t.strip_prefix("weekly:")).map(str::to_string)).unwrap_or_default();
    let start = r.get("start").and_then(Value::as_i64).unwrap_or(0);
    // The weekday said, else the one of the first meeting.
    let wd = DAYS.iter().position(|d| *d == day.to_lowercase()).map(|k| k as i64).unwrap_or_else(|| weekday(start));
    if day.is_empty() && !rep.is_object() && !rep.is_string() {
        return json!({"ext": r.get("ext").cloned().unwrap_or(json!({})), "added": false});
    }
    let mut l = list(r);
    let heroine = s(r, "heroine");
    let minute = start.rem_euclid(1440);
    l.retain(|x| !(s(x, "heroine") == heroine && x["weekday"].as_i64() == Some(wd) && x["minute"].as_i64() == Some(minute)));
    let id = l.iter().filter_map(|x| x["id"].as_i64()).max().unwrap_or(0) + 1;
    let label = crate::tr::m("series.weekly_label", &[("label", crate::tr::a(s(r, "label"))), ("weekday", crate::tr::a(crate::tr::t(DAY_KEYS[wd as usize])))]);
    l.push(json!({"id": id, "heroine": heroine, "weekday": wd, "minute": minute, "place": s(r, "place"), "label": label, "last": start}));
    json!({"ext": with_list(r, l), "added": true, "weekday": wd, "label": label})
}

const DAY_KEYS: [&str; 7] = ["series.mon", "series.tue", "series.wed", "series.thu", "series.fri", "series.sat", "series.sun"];

/// Each series' next meeting, when none of it is booked any more (the last one is past or done): the same weekday and
/// time a week after the last. Request: ext, now, pending [{heroine, start}]. Answer: ext, book [{series, heroine,
/// start, place, label}].
fn due(r: &Value) -> Value {
    let now = r.get("now").and_then(Value::as_i64).unwrap_or(0);
    let pending: Vec<(String, i64)> = r.get("pending").and_then(Value::as_array).into_iter().flatten().map(|p| (s(p, "heroine").to_string(), p["start"].as_i64().unwrap_or(0))).collect();
    let mut l = list(r);
    let mut book = Vec::new();
    for x in l.iter_mut() {
        let last = x["last"].as_i64().unwrap_or(0);
        let heroine = s(x, "heroine").to_string();
        if pending.iter().any(|(h, st)| *h == heroine && *st >= last && (st - last) % 1440 == 0 && *st >= now) {
            continue; // the next one is booked
        }
        if last > now {
            continue; // the first one is still to come (it is in pending normally)
        }
        let mut next = last + 7 * 1440;
        while next <= now {
            next += 7 * 1440;
        }
        x["last"] = json!(next);
        book.push(json!({"series": x["id"], "heroine": heroine, "start": next, "place": x["place"], "label": x["label"]}));
    }
    json!({"ext": with_list(r, l), "book": book})
}

/// Calling a meeting off. Request: ext, heroine, start (the one called off), scope ("this", "all" or "" when nobody
/// said). Answer: ext, ask (true when it is part of a series and the scope was not said: the game asks him), ended.
fn cancel(r: &Value) -> Value {
    let heroine = s(r, "heroine");
    let start = r.get("start").and_then(Value::as_i64).unwrap_or(0);
    let mut l = list(r);
    let of = l.iter().position(|x| s(x, "heroine") == heroine && x["weekday"].as_i64() == Some(weekday(start)) && x["minute"].as_i64() == Some(start.rem_euclid(1440)));
    let Some(k) = of else {
        return json!({"ext": with_list(r, l), "ask": false, "ended": false});
    };
    match s(r, "scope") {
        "all" => {
            l.remove(k);
            json!({"ext": with_list(r, l), "ask": false, "ended": true})
        }
        "this" => json!({"ext": with_list(r, l), "ask": false, "ended": false}),
        _ => json!({"ext": with_list(r, l), "ask": true, "ended": false}),
    }
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "series_add" => add(r),
        "series_due" => due(r),
        "series_cancel" => cancel(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_wednesday_books_the_next_one_after_each() {
        // Day 14 is 2010-09-15, a Wednesday; 19:00.
        let start = 14 * 1440 + 19 * 60;
        let a = add(&json!({"ext": {"items": {}}, "heroine": "k", "start": start, "place": "library", "label": "念書", "repeat": {"weekly": "wed"}}));
        assert_eq!(a["added"], true);
        assert_eq!(a["weekday"], 2);
        assert!(a["ext"]["items"].is_object(), "the rest of ext is kept");
        // Still booked: nothing new.
        let d = due(&json!({"ext": a["ext"], "now": start - 60, "pending": [{"heroine": "k", "start": start}]}));
        assert_eq!(d["book"], json!([]));
        // It is past: next Wednesday.
        let d = due(&json!({"ext": a["ext"], "now": start + 120, "pending": []}));
        assert_eq!(d["book"][0]["start"], start + 7 * 1440);
        // And not twice.
        let again = due(&json!({"ext": d["ext"], "now": start + 200, "pending": [{"heroine": "k", "start": start + 7 * 1440}]}));
        assert_eq!(again["book"], json!([]));
    }

    #[test]
    fn calling_off_asks_unless_said_and_all_ends_the_series() {
        let start = 14 * 1440 + 19 * 60;
        let a = add(&json!({"ext": {}, "heroine": "k", "start": start, "place": "library", "label": "念書", "repeat": "weekly:wed"}));
        assert_eq!(cancel(&json!({"ext": a["ext"], "heroine": "k", "start": start + 7 * 1440, "scope": ""}))["ask"], true);
        let all = cancel(&json!({"ext": a["ext"], "heroine": "k", "start": start, "scope": "all"}));
        assert_eq!(all["ended"], true);
        assert_eq!(all["ext"]["series"], json!([]));
        // A meeting not in a series: nothing to ask.
        assert_eq!(cancel(&json!({"ext": {}, "heroine": "k", "start": start, "scope": ""}))["ask"], false);
    }
}
