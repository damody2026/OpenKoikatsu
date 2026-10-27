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

/// "After class" on game day `day`: the later end of his and her classes that weekday from their timetables. A school
/// weekday with no class of theirs (an exam week has none: R3-205, "明天放学后" in the midterm week) is the usual end of
/// the school day, the last period. None at weekends, in the break, or when neither of them studies (-1).
fn class_end_on(r: &Value, day: i64) -> i64 {
    let civil = crate::campus::civil_day(day as i32);
    let weekday = crate::campus::weekday(civil);
    let courses: Vec<&Value> = ["his_courses", "her_courses"].iter().flat_map(|k| r.get(*k).and_then(Value::as_array).map(|a| a.as_slice()).unwrap_or(&[])).collect();
    let phase = crate::campus::term_at(civil).1;
    if courses.is_empty() || weekday >= 5 || phase == crate::campus::Phase::Break {
        return -1;
    }
    let last = crate::academics::SLOT_END[1] as i64;
    if phase != crate::campus::Phase::Classes {
        return last;
    }
    courses.iter().filter(|c| i(c, "weekday") == weekday).map(|c| crate::academics::SLOT_END[i(c, "slot").clamp(0, 1) as usize] as i64).max().unwrap_or(last)
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
    // "After class" / "after work" with no clock time: when the later of the two of them is through that day. Today:
    // C++ gives the ends of today's classes and shifts after now. Another day (R3-205: "明天放学后" was read with
    // today's timetable, nothing left at 17:40, no time): the timetables (his_courses / her_courses [{weekday, slot}])
    // on that day's weekday while classes run, and his shifts [{start, minutes}] on that day.
    let today = i(r, "today");
    let game_day = game_day_of(s(r, "day"), today);
    let after = |kind: &str| -> Option<i64> {
        let end = if game_day == today {
            i(r, &format!("his_{kind}_end")).max(i(r, &format!("her_{kind}_end")))
        } else if kind == "class" {
            class_end_on(r, game_day)
        } else {
            let list = r.get("his_shifts").and_then(Value::as_array).map(|a| a.as_slice()).unwrap_or(&[]);
            list.iter().filter(|x| i(x, "start").div_euclid(1440) == game_day).map(|x| i(x, "start") % 1440 + i(x, "minutes").max(60)).max().unwrap_or(-1)
        };
        (end > 0).then_some(end)
    };
    let minute = match s(r, "time") {
        "after_class" => after("class"),
        "after_work" => after("work"),
        t => minute_of(t),
    };
    let Some(minute) = minute else { return ignore("no_time") };
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
    // Meeting at its door a little before it opens ("09:30 at the bookstore door, we read once it opens at 10") is a
    // meeting: they wait there for it (R3-253).
    let d = date((start / 1440) as i32, 12.0);
    let hours = (start % 1440) as f64 / 60.0;
    let opens_soon = (1..=4).any(|k| c.place_open(place, hours + k as f64 * 0.25, d.weekday as i32));
    if !c.place_open(place, hours, d.weekday as i32) && !opens_soon {
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
    // Another girl's meeting near it somewhere else: he cannot be at both. At the same place he can (Keqing asked to join
    // the study evening he had with Yuuka at the cafe was warned as a clash, R3-208); how they take it is theirs.
    let clash = pending
        .iter()
        .find(|a| s(a, "heroine") != her && s(a, "kind") != "entrust" && (i(a, "start") - start).abs() < 90 && s(a, "place") != place)
        .map(|a| s(a, "label").to_string())
        .unwrap_or_default();
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
        // "After class": the later end of his and her classes today; none left today, no time.
        let after = ask(json!({"day": "+0", "time": "after_class", "his_class_end": 990, "her_class_end": 720}));
        assert_eq!((after["action"].as_str(), after["minute"].as_i64()), (Some("book"), Some(990)));
        assert_eq!(ask(json!({"day": "+0", "time": "after_class", "his_class_end": -1, "her_class_end": -1}))["reason"], "no_time");
        // Another day (R3-205): from the timetables. Day 10 is Saturday 2010-09-11; day 12 is Monday, day 11 Sunday.
        let monday = json!({"time": "after_class", "his_class_end": -1, "her_courses": [{"weekday": 0, "slot": 1}], "his_courses": [{"weekday": 0, "slot": 0}, {"weekday": 2, "slot": 1}]});
        let mut m = monday.clone();
        m["day"] = json!("+2");
        assert_eq!(ask(m)["start"], 12 * 1440 + 990);
        let mut sun = monday.clone();
        sun["day"] = json!("+1");
        assert_eq!(ask(sun)["reason"], "no_time", "no classes on Sunday");
        // Tuesday (day 13): neither has a class that day, the school day ends with the last period; no timetable at all,
        // no time.
        let mut tue = monday.clone();
        tue["day"] = json!("+3");
        assert_eq!(ask(tue)["start"], 13 * 1440 + 990);
        assert_eq!(ask(json!({"day": "+2", "time": "after_class"}))["reason"], "no_time");
        // The midterm week (2010-11-08, day 68): no classes, still after school.
        let mid = ask(json!({"day": "+1", "today": 67, "now": 67 * 1440 + 1060, "time": "after_class", "her_courses": [{"weekday": 0, "slot": 0}]}));
        assert_eq!(mid["start"], 68 * 1440 + 990, "{mid}");
        let shift = ask(json!({"time": "after_work", "his_shifts": [{"start": 11 * 1440 + 1020, "minutes": 240}]}));
        assert_eq!(shift["start"], 11 * 1440 + 1260);
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
        // At the bookstore door half an hour before it opens (10:00): a meeting, they wait for it there (R3-253).
        let door = ask(json!({"place": "bookstore", "time": "09:30"}));
        assert_ne!(door["action"], "closed", "{door}");
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
        // The same place: both meetings can be kept (R3-208).
        let same = ask(json!({"place": "cafe", "pending": [{"id": 3, "heroine": "b", "kind": "date", "place": "cafe", "start": 11 * 1440 + 19 * 60, "label": "和優香複習"}]}));
        assert_eq!((same["action"].as_str(), same["clash"].as_str()), (Some("book"), Some("")));
    }
}
