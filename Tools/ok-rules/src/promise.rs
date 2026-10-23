//! A meeting promised in chat (the judge's reading of both lines: day, time, place). Whether it is a new meeting, the
//! same one said again (moved to the time or place said last) or none (no clear time, already past, the place closed
//! then) is decided here; Unreal books, moves and labels it. Moved from AddPromiseFromChat (STREET_NEW_FEATURES_PLAN 0-4).
use crate::calendar::{date, days_from_civil};
use crate::catalog::Catalog;
use serde_json::{json, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

fn i(r: &Value, k: &str) -> i64 {
    r.get(k).and_then(Value::as_i64).or_else(|| r.get(k).and_then(Value::as_f64).map(|f| f as i64)).unwrap_or(0)
}

/// "19:00" as minutes of the day; None when it is not a clear time.
fn minute_of(time: &str) -> Option<i64> {
    let (h, m) = time.split_once(':')?;
    let (h, m) = (h.trim(), m.trim());
    if h.is_empty() || m.is_empty() || !h.chars().all(|c| c.is_ascii_digit()) || !m.chars().all(|c| c.is_ascii_digit()) {
        return None;
    }
    Some(h.parse::<i64>().ok()?.clamp(0, 23) * 60 + m.parse::<i64>().ok()?.clamp(0, 59))
}

/// "+1" (days from today) or "2010-09-05" as a game day.
/// A weekday as said ("周三", "下周三", "星期日", "wed", "next wed"): the game day it falls on. "周三" is the next
/// Wednesday from today (today when it is one); "下周三" the Wednesday of next week (Monday-based weeks). Game day 0,
/// 2010-09-01, was a Wednesday. The model counting the days itself put "下周三" said on a Saturday on Tuesday.
fn weekday_day(day: &str, today: i64) -> Option<i64> {
    let d = day.trim().to_lowercase();
    let (next, rest) = ["下周", "下週", "下星期", "下礼拜", "下禮拜", "next "]
        .iter()
        .find_map(|p| d.strip_prefix(p).map(|r| (true, r.to_string())))
        .unwrap_or((false, d.clone()));
    let rest = ["星期", "周", "週", "礼拜", "禮拜"].iter().find_map(|p| rest.strip_prefix(p)).unwrap_or(&rest).trim().to_string();
    let names: [&[&str]; 7] = [&["一", "mon"], &["二", "tue"], &["三", "wed"], &["四", "thu"], &["五", "fri"], &["六", "sat"], &["日", "天", "sun"]];
    let target = names.iter().position(|ns| ns.iter().any(|n| rest == *n || (n.len() == 3 && rest.starts_with(n))))? as i64;
    let now = (today + 2).rem_euclid(7); // Monday 0
    Some(if next { today - now + 7 + target } else { today + (target - now).rem_euclid(7) })
}

fn game_day_of(day: &str, today: i64) -> i64 {
    if let Some(n) = day.strip_prefix('+') {
        return today + n.trim().parse::<i64>().unwrap_or(0).clamp(0, 60);
    }
    if let Some(d) = weekday_day(day, today) {
        return d;
    }
    let p: Vec<&str> = day.get(..10).unwrap_or(day).split('-').collect();
    if let [y, m, d] = p[..] {
        if let (Ok(y), Ok(m), Ok(d)) = (y.parse::<i64>(), m.parse::<u32>(), d.parse::<u32>()) {
            return days_from_civil(y, m, d) - days_from_civil(2010, 9, 1);
        }
    }
    today
}

/// Request: heroine, day, time, place, place_known, now (game minute), today (game day), pending [{id, heroine, kind,
/// place, start, label, booked_ago (real seconds since booked from chat, -1 unknown)}].
/// Answer: action ignore | book | move | keep | closed; reason; start; minute (of the day said); id (the meeting moved or
/// kept); place_changed; why (closed); clash (another girl's meeting near it, its label); replaces_id (with replaces: the
/// meeting with her this new one takes the place of, 0 for none).
fn promise(c: &Catalog, r: &Value) -> Value {
    let ignore = |reason: &str| json!({"action": "ignore", "reason": reason});
    if !r.get("place_known").and_then(Value::as_bool).unwrap_or(true) {
        return ignore("not_a_place");
    }
    // No clear time, no meeting (it used to become 19:00 by default, a time nobody said).
    let Some(minute) = minute_of(s(r, "time")) else { return ignore("no_time") };
    let today = i(r, "today");
    let game_day = game_day_of(s(r, "day"), today);
    // Before 04:00 belongs to the previous game day's night.
    let start = game_day * 1440 + minute + if minute < 240 { 1440 } else { 0 };
    if start <= i(r, "now") {
        return ignore("past");
    }
    let (her, place) = (s(r, "heroine"), s(r, "place"));
    let pending: Vec<&Value> = r.get("pending").and_then(Value::as_array).map(|a| a.iter().collect()).unwrap_or_default();
    // The same meeting booked twice from one exchange (her reply's promise and the judge's), one of them a day off
    // ("seven tonight" read as tomorrow): within a minute and a half, same place and hour a day apart is that meeting,
    // on the nearer day.
    for a in &pending {
        let ago = a.get("booked_ago").and_then(Value::as_f64).unwrap_or(-1.0) as i64;
        if s(a, "heroine") == her && s(a, "kind") == "date" && s(a, "place") == place && (i(a, "start") - start).abs() == 1440 && (0..90).contains(&ago) {
            // Unless the judge says this line moves it ("tomorrow I can't, the day after"): then the day said now.
            let replaces = r.get("replaces").and_then(Value::as_bool).unwrap_or(false);
            let at = if replaces { start } else { i(a, "start").min(start) };
            return json!({"action": if at != i(a, "start") { "move" } else { "keep" }, "reason": if replaces { "moved_a_day" } else { "same_exchange_day_apart" }, "id": i(a, "id"), "start": at, "minute": minute, "place_changed": false});
        }
    }
    // One meeting with her at a time: the same hour already has one, or the same place on the same day is the same
    // meeting said again at another time ("three, not two"); her own "pick me up at five" counts like a date.
    let day_of = |m: i64| (m - 240).div_euclid(1440);
    for a in &pending {
        let (kind, at) = (s(a, "kind"), i(a, "start"));
        if s(a, "heroine") != her || (kind != "date" && kind != "pickup") {
            continue;
        }
        if (at - start).abs() >= 60 && !(s(a, "place") == place && day_of(at) == day_of(start)) {
            continue;
        }
        // "Five o'clock, don't forget" said again without am/pm: twelve hours off is the same time; the daytime one of
        // the two (07:00-22:00) is meant, the one already booked when both are.
        let daytime = |m: i64| (7 * 60..22 * 60).contains(&m.rem_euclid(1440));
        let said = if (at - start).abs() != 720 || (daytime(start) && !daytime(at)) { start } else { at };
        let moved = at != said || s(a, "place") != place;
        return json!({"action": if moved { "move" } else { "keep" }, "reason": if said != start { "twelve_hours_off" } else { "said_again" },
                      "id": i(a, "id"), "start": said, "minute": minute, "place_changed": s(a, "place") != place});
    }
    // Booked for when the place is closed (breakfast at 8:00 at a shop opening at 10:00): not booked; she hears why.
    let d = date((start / 1440) as i32, 12.0);
    let hours = (start % 1440) as f64 / 60.0;
    if !c.place_open(place, hours, d.weekday as i32) {
        return json!({"action": "closed", "reason": "closed", "start": start, "minute": minute, "why": c.closed_why(place, d.weekday as i32)});
    }
    // Another girl already booked near that time: he is told (both stay booked; it is his to sort out).
    // "Not today, tomorrow" (the judge's replaces) when it is not the same meeting said again: her meeting at the same
    // place first, else the nearest in time (the soonest one was taken: the pickup tonight went for a walk moved to 4 pm).
    let replaces_id = if r.get("replaces").and_then(Value::as_bool).unwrap_or(false) {
        pending
            .iter()
            .filter(|a| s(a, "heroine") == her && matches!(s(a, "kind"), "date" | "pickup"))
            .min_by_key(|a| (s(a, "place") != place, (i(a, "start") - start).abs()))
            .map(|a| i(a, "id"))
            .unwrap_or(0)
    } else {
        0
    };
    let clash = pending.iter().find(|a| s(a, "heroine") != her && s(a, "kind") != "entrust" && (i(a, "start") - start).abs() < 90).map(|a| s(a, "label").to_string()).unwrap_or_default();
    json!({"action": "book", "reason": "new", "start": start, "minute": minute, "clash": clash, "replaces_id": replaces_id})
}

pub fn eval(c: &Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "promise_from_chat" => promise(c, r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::Place;

    fn cat() -> Catalog {
        let mut c = Catalog::default();
        c.places.insert("cafe".into(), Place { open: vec![(10.0, 22.0)], weekend_open: vec![], closed_weekdays: vec![], ..Default::default() });
        c
    }

    fn ask(extra: Value) -> Value {
        let mut r = json!({"heroine": "a", "day": "+1", "time": "19:00", "place": "park", "now": 10 * 1440 + 600, "today": 10, "pending": []});
        for (k, v) in extra.as_object().unwrap() {
            r[k] = v.clone();
        }
        promise(&cat(), &r)
    }

    #[test]
    fn new_meetings_and_the_ones_that_are_not() {
        let b = ask(json!({}));
        assert_eq!((b["action"].as_str(), b["start"].as_i64()), (Some("book"), Some(11 * 1440 + 19 * 60)));
        assert_eq!(ask(json!({"time": "晚上"}))["reason"], "no_time");
        assert_eq!(ask(json!({"day": "+0", "time": "09:00"}))["reason"], "past");
        assert_eq!(ask(json!({"place_known": false}))["reason"], "not_a_place");
        // 01:30 "tonight" is the night after the game day.
        assert_eq!(ask(json!({"day": "+0", "time": "01:30"}))["start"], 11 * 1440 + 90);
        assert_eq!(ask(json!({"day": "2010-09-12", "time": "12:00"}))["start"], 11 * 1440 + 720);
        // Game day 3 is Saturday 2010-09-04: "下周三" is 09-08 (day 7), "周三" too; on Monday day 5 "下周三" is day 14.
        assert_eq!((game_day_of("下周三", 3), game_day_of("周三", 3), game_day_of("next wed", 3)), (7, 7, 7));
        assert_eq!((game_day_of("下周三", 5), game_day_of("周三", 5), game_day_of("星期日", 5)), (14, 7, 11));
        assert_eq!(game_day_of("周三", 0), 0, "today when it is one");
        let closed = ask(json!({"place": "cafe", "time": "08:00"}));
        assert_eq!(closed["action"], "closed");
        assert!(!closed["why"].as_str().unwrap().is_empty());
    }

    #[test]
    fn said_again_is_the_same_meeting() {
        let at = 11 * 1440 + 19 * 60;
        let pend = |start: i64, place: &str, ago: f64| json!([{"id": 7, "heroine": "a", "kind": "date", "place": place, "start": start, "label": "x", "booked_ago": ago}]);
        // "Three, not two": same place, same day, another time.
        let m = ask(json!({"time": "15:00", "pending": pend(11 * 1440 + 14 * 60, "park", -1.0)}));
        assert_eq!((m["action"].as_str(), m["id"].as_i64(), m["start"].as_i64()), (Some("move"), Some(7), Some(11 * 1440 + 900)));
        // The same time, another place: it moves there.
        let p = ask(json!({"pending": pend(at, "cafe", -1.0)}));
        assert_eq!((p["action"].as_str(), p["place_changed"].as_bool()), (Some("move"), Some(true)));
        assert_eq!(ask(json!({"pending": pend(at, "park", -1.0)}))["action"], "keep");
        // 07:00 said again for a 19:00: twelve hours off, the daytime one stays.
        let k = ask(json!({"time": "07:00", "pending": pend(at, "park", -1.0)}));
        assert_eq!((k["action"].as_str(), k["start"].as_i64()), (Some("keep"), Some(at)));
        // Booked from the same exchange a day apart: the nearer day.
        let d = ask(json!({"day": "+0", "pending": pend(at, "park", 20.0)}));
        assert_eq!((d["action"].as_str(), d["start"].as_i64()), (Some("move"), Some(10 * 1440 + 19 * 60)));
        // "Tomorrow I can't, the day after" right after booking it (the judge's replaces): it moves to the later day.
        let later = ask(json!({"day": "+2", "replaces": true, "pending": pend(at, "park", 9.0)}));
        assert_eq!((later["action"].as_str(), later["start"].as_i64()), (Some("move"), Some(at + 1440)));
        // Long after (another day's talk), a day apart is another meeting.
        assert_eq!(ask(json!({"day": "+0", "pending": pend(at, "park", 500.0)}))["action"], "book");
    }

    #[test]
    fn replaces_the_meeting_it_is_about_not_the_soonest() {
        let pend = json!([{"id": 1, "heroine": "a", "kind": "pickup", "place": "cafe", "start": 10 * 1440 + 22 * 60},
                          {"id": 2, "heroine": "a", "kind": "date", "place": "park", "start": 10 * 1440 + 19 * 60 + 600}]);
        // Said again (same place, same day): moved, nothing else replaced.
        let m = ask(json!({"time": "16:00", "replaces": true, "pending": pend.clone()}));
        assert_eq!((m["action"].as_str(), m["id"].as_i64()), (Some("move"), Some(2)));
        // "Not tomorrow, the day after": a new day; the park meeting goes, not tonight's pickup.
        let d = ask(json!({"day": "+2", "replaces": true, "pending": pend.clone()}));
        assert_eq!((d["action"].as_str(), d["replaces_id"].as_i64()), (Some("book"), Some(2)));
        assert_eq!(ask(json!({"day": "+2", "pending": pend}))["replaces_id"], 0);
    }

    #[test]
    fn another_girl_near_the_same_time_is_a_clash() {
        let b = ask(json!({"pending": [{"id": 3, "heroine": "b", "kind": "date", "place": "cafe", "start": 11 * 1440 + 19 * 60 + 30, "label": "和刻晴見面"}]}));
        assert_eq!((b["action"].as_str(), b["clash"].as_str()), (Some("book"), Some("和刻晴見面")));
    }
}
