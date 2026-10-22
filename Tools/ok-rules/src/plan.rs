//! Plan §6: each heroine's day, written by rules (no LLM). The day plan (promises, his company, sleep, classes and
//! shifts, three meals, a jealous detour, free time), everyone's own morning and pace, errands, friends meeting, leaving
//! in time for fixed blocks, rain moving outdoor plans in, asking him to pick her up, and through the day the urgent
//! cut-ins (hungry, worn out, grimy) and what she orders when she eats alone. Also the weather and the holidays.
//! Blocks are game minutes of the day (0..1440+, a block may run past midnight); place "" is the street.
use crate::calendar::date;
use crate::campus::{self, Phase};
use crate::catalog::{self, Catalog};
use crate::effects::Effects;
use crate::places::PlacesTuning;
use crate::relation::Relation;
use crate::rng::RandomStream;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::BTreeMap;

#[derive(Serialize, Deserialize, Debug, Clone, Default, PartialEq)]
#[serde(default)]
pub struct Block {
    pub start: i32,
    pub end: i32,
    pub place: String,
    pub activity: String,
    pub kind: String,
    pub source: String,
    pub job_id: String,
    pub budget: i32,
}

fn block(start: i32, end: i32, place: &str, kind: &str, source: &str, activity: &str, budget: i32) -> Block {
    Block { start, end, place: place.into(), kind: kind.into(), source: source.into(), activity: activity.into(), budget, ..Default::default() }
}

/// Free time is filled around the fixed blocks; blocks never overlap.
fn put(day: &mut Vec<Block>, b: Block) -> bool {
    if day.iter().any(|x| b.start < x.end && x.start < b.end) {
        return false;
    }
    day.push(b);
    true
}

// ------------------------------------------------------------------------------------------------ weather and dates

/// Weather for a game day: deterministic per day, rainier in autumn; rain hours from a start hour for 2-6 hours.
pub fn raining_at(day: i32, minute: i32) -> bool {
    let mut rng = RandomStream::new(day.wrapping_mul(2654435).wrapping_add(17));
    let m = date(day, 12.0).month;
    let chance = if (9..=11).contains(&m) {
        0.3
    } else if (5..=8).contains(&m) {
        0.35
    } else {
        0.15
    };
    if rng.frand() >= chance {
        return false;
    }
    let from = rng.rand_range(8, 17) * 60;
    minute >= from && minute < from + rng.rand_range(2, 6) * 60
}

/// Holidays and school weeks (events.toml through events.rs).
pub fn holiday_on(day: i32) -> &'static str {
    crate::events::holiday_text(day)
}

// ------------------------------------------------------------------------------------------------ one heroine's day

pub struct Inputs<'a> {
    pub profile: &'a Value,
    pub weekday: i32,
    pub money_stress: f32,
    pub energy: f32,
    pub wealth: i64,
    pub promises: Vec<Block>,
    pub stalk_place: String,
    pub rainy: bool,
    pub exam_week: bool,
    pub company_place: String,
    pub company_role: String,
    pub classes: Vec<Block>,
    pub timetable: bool,
    pub seed: i32,
}

fn pstr<'a>(p: &'a Value, section: Option<&str>, field: &str) -> &'a str {
    match section {
        Some(s) => p.get(s).and_then(|x| x.get(field)),
        None => p.get(field),
    }
    .and_then(Value::as_str)
    .unwrap_or("")
}

pub fn plan_day(i: &Inputs, c: &Catalog) -> Vec<Block> {
    let mut day = Vec::new();
    let mut rng = RandomStream::new(i.seed);
    let home = Some(pstr(i.profile, None, "home_place_id")).filter(|h| !h.is_empty()).unwrap_or("girls_dorm").to_string();
    let primary = pstr(i.profile, Some("occupation"), "primary");
    let work = pstr(i.profile, Some("occupation"), "workplace_place_id");
    let job_id = pstr(i.profile, Some("occupation"), "job_id");
    let weekend = i.weekday >= 5;
    // 1. Promises first: they are what she planned the day around.
    for p in &i.promises {
        put(&mut day, p.clone());
    }
    // His company comes before classes and her old job: she changed career when she took the offer.
    if !i.company_place.is_empty() && !weekend {
        put(&mut day, block(600, 1080, &i.company_place, "work", "company", &i.company_role, 0));
    }
    // Night: sleep at home until 07:00, and from 23:00 on.
    put(&mut day, block(0, 420, &home, "sleep", "template", "", 0));
    put(&mut day, block(1380, 1440, &home, "sleep", "template", "", 0));
    // 2. Classes on weekdays for students; shifts at her workplace (more when money is tight).
    if i.timetable {
        for cl in &i.classes {
            put(&mut day, cl.clone());
        }
    } else if primary == "student" && !weekend {
        let place = if rng.frand() < 0.5 { "lecture" } else { "school" };
        put(&mut day, block(540, 720, place, "class", "schedule", "", 0));
        if rng.frand() < 0.6 {
            put(&mut day, block(810, 960, "lecture", "class", "schedule", "", 0));
        }
    }
    let shift_days = (if primary == "student" { 3 } else { 5 }) + if i.money_stress > 60.0 { 2 } else { 0 };
    // No shift on the day her workplace is closed (the library on Mondays: she "worked" there and asked to be picked up).
    let closed = c.places.get(work).is_some_and(|p| p.closed_weekdays.contains(&i.weekday));
    if !work.is_empty() && rng.frand() < shift_days as f32 / 7.0 && !closed {
        // A shift from the catalogue's hours when known, else an evening shift.
        let (mut s, mut e) = (1020, 1320);
        if let Some((from, to)) = c.places.get(work).and_then(|p| p.open.first()) {
            s = ((*from * 60.0) as i32).max(if primary == "student" && !weekend { 1020 } else { 600 });
            e = (s + 300).min((*to * 60.0) as i32);
        }
        let mut w = block(s, e, work, "work", "occupation", "", 0);
        w.job_id = job_id.into();
        put(&mut day, w);
    }
    // 3. Meals, cheaper when money is tight; when a class or shift sits on the usual hour, just before or after it.
    let budget = (if i.money_stress > 60.0 {
        crate::region::amount("goods", 13)
    } else if i.money_stress > 30.0 {
        crate::region::amount("goods", 25)
    } else if i.wealth >= crate::region::amount("cash", 30000) {
        crate::region::amount("goods", 90)
    } else {
        crate::region::amount("goods", 55)
    }) as i32;
    let lunch = if i.money_stress > 50.0 || (primary == "student" && !weekend) {
        "cafeteria".to_string()
    } else if rng.frand() < 0.5 {
        "restaurant".to_string()
    } else {
        "department".to_string()
    };
    let meal = |day: &mut Vec<Block>, starts: &[i32], len: i32, place: &str, b: i32| {
        for s in starts {
            if put(day, block(*s, s + len, place, "meal", "need", "", b)) {
                return;
            }
        }
    };
    // Breakfast is not the convenience store at 07:10 for everybody (seven heroines stood in it at once): at home, the
    // store, the cafeteria (students on weekdays) or the cafe, at a time of her own; the cafe opens at 08:00.
    let breakfast = if i.money_stress > 50.0 {
        home.clone()
    } else {
        let r = rng.frand();
        if r < 0.35 {
            home.clone()
        } else if r < 0.65 {
            "conveni".to_string()
        } else if primary == "student" && !weekend {
            "cafeteria".to_string()
        } else {
            "cafe".to_string()
        }
    };
    let morning: &[i32] = match (rng.frand() * 3.0) as i32 { 0 => &[420, 450, 480], 1 => &[450, 480, 510], _ => &[480, 510, 450] };
    let morning: Vec<i32> = morning.iter().copied().filter(|s| breakfast != "cafe" || *s >= 480).collect();
    meal(&mut day, &morning, 30, &breakfast, budget.min(crate::region::amount("goods", 17) as i32));
    meal(&mut day, &[720, 660, 780, 975], 60, &lunch, budget);
    // Dinner is not one restaurant for everybody (all ten heroines sat in it at 18:00): a student on a weekday often
    // eats at the cafeteria (open until 19:00), a small budget skips the restaurant, and the hour moves a little.
    let dinner = if i.money_stress > 50.0 {
        "conveni"
    } else {
        let r = rng.frand();
        let student_day = primary == "student" && !weekend;
        if student_day && r < 0.4 {
            "cafeteria"
        } else if budget < crate::region::amount("goods", 40) as i32 && r < 0.75 {
            if student_day { "cafeteria" } else { "department" }
        } else if r < 0.6 {
            "restaurant"
        } else if r < 0.85 {
            "department"
        } else {
            "cafe"
        }
    };
    let starts: &[i32] = match (rng.frand() * 3.0) as i32 { 0 => &[1050, 1080, 1020, 960, 1320], 1 => &[1080, 1110, 1020, 960, 1290], _ => &[1110, 1080, 1050, 960, 1290] };
    // The cafeteria closes at 19:00: only the starts that end by then.
    let starts: Vec<i32> = starts.iter().copied().filter(|s| dinner != "cafeteria" || s + 60 <= 1140).collect();
    meal(&mut day, &starts, 60, dinner, budget);
    // 4. Jealous: she turns up where he usually is.
    if !i.stalk_place.is_empty() {
        put(&mut day, block(1170, 1230, &i.stalk_place, "encounter", "jealousy", "", 0));
    }
    // 5. Free time: favourite places, or rest when tired, or home.
    let mut fav: Vec<String> = i.profile.get("favorite_places").and_then(Value::as_array).map(|a| a.iter().map(|v| v.as_str().unwrap_or("").to_string()).collect()).unwrap_or_default();
    if fav.is_empty() {
        fav = vec!["cafe".into(), "park".into(), "bookstore".into()];
    }
    if i.rainy {
        fav.retain(|p| p != "park" && p != "playground" && p != "shrine");
        if fav.is_empty() {
            fav.push("cafe".into());
        }
    }
    if i.exam_week && primary == "student" {
        fav = vec!["library".into()];
    }
    let mut start = 420;
    while start < 1410 {
        let end = start + 60;
        if day.iter().any(|x| start < x.end && x.start < end) {
            start += 60;
            continue;
        }
        if i.energy < 30.0 && rng.frand() < 0.6 {
            put(&mut day, block(start, end, &home, "rest", "need", "", 0));
            start += 60;
            continue;
        }
        let roll = rng.frand();
        if roll < 0.35 {
            put(&mut day, block(start, end, "", "leisure", "template", "walk", 0));
        } else if roll < 0.8 {
            let place = fav[rng.rand_range(0, fav.len() as i32 - 1) as usize].clone();
            put(&mut day, block(start, end, &place, "leisure", "favorite", "", if i.money_stress > 60.0 { 0 } else { crate::region::amount("goods", 150) as i32 }));
        } else {
            put(&mut day, block(start, end, &home, "rest", "template", "", 0));
        }
        start += 60;
    }
    day.sort_by_key(|b| b.start);
    day
}

// ------------------------------------------------------------------------------------------------ everyone's own day

fn fixed(b: &Block) -> bool {
    b.kind == "class" || b.kind == "work" || b.kind == "promise" || b.kind == "encounter" || (b.kind == "sleep" && b.start > 0)
}

/// Fixed blocks win; movable ones are trimmed round them, gaps go to the block before, slivers are dropped.
fn settle(day: &mut Vec<Block>, wake: i32) {
    day.sort_by_key(|b| b.start);
    let mut out: Vec<Block> = Vec::new();
    for mut b in day.drain(..) {
        let f = fixed(&b);
        if !f && !(b.kind == "sleep" && b.start == 0) {
            b.start = b.start.max(wake);
        }
        if let Some(prev) = out.last_mut() {
            if b.start < prev.end {
                // A fixed block wins over a movable one, and a meal over free time: the one before is cut short.
                let meal_over_free = b.kind == "meal" && prev.kind != "meal" && !fixed(prev) && b.start > prev.start;
                if (f && !fixed(prev)) || meal_over_free {
                    prev.end = b.start;
                    if prev.end - prev.start < 15 {
                        out.pop();
                    }
                } else {
                    b.start = prev.end;
                }
            }
        }
        if b.end - b.start >= 15 {
            out.push(b);
        }
    }
    // Close gaps: a movable block runs on until the next one begins.
    for k in 0..out.len().saturating_sub(1) {
        if out[k].end < out[k + 1].start && !fixed(&out[k]) {
            out[k].end = out[k + 1].start;
        }
    }
    *day = out;
}

/// Puts b into the day over free time only (no fixed block in the way); what it covers is cut round it.
fn insert(day: &mut Vec<Block>, b: &Block) -> bool {
    if day.iter().any(|x| x.start < b.end && b.start < x.end && (fixed(x) || x.kind == "sleep" || x.kind == "social")) {
        return false;
    }
    let mut out = Vec::new();
    for x in day.iter() {
        if !(x.start < b.end && b.start < x.end) {
            out.push(x.clone());
            continue;
        }
        if x.start < b.start {
            let mut head = x.clone();
            head.end = b.start;
            if head.end - head.start >= 15 {
                out.push(head);
            }
        }
        if x.end > b.end {
            let mut tail = x.clone();
            tail.start = b.end;
            if tail.end - tail.start >= 15 {
                out.push(tail);
            }
        }
    }
    out.push(b.clone());
    out.sort_by_key(|x| x.start);
    *day = out;
    true
}

#[derive(Deserialize, Debug, Clone, Default)]
struct Heroine {
    actor: i32,
    id: String,
    #[serde(default)]
    name: String,
    #[serde(default)]
    profile: Value,
    relation: Relation,
    #[serde(default)]
    invest_total: i64,
    #[serde(default)]
    intent: String,
    #[serde(default)]
    academic: Option<Value>,
    /// Today's appointments with him: minute of the day and place.
    #[serde(default)]
    promises: Vec<Block>,
    #[serde(default)]
    company_place: String,
    #[serde(default)]
    company_role: String,
}

/// Campus: today's classes from her timetable (rolled for attendance) and her own study; true when she has a record.
fn campus_blocks(h: &Heroine, day: i32) -> (bool, Vec<Block>) {
    let Some(a) = h.academic.as_ref().filter(|a| !a.get("dept").and_then(Value::as_str).unwrap_or("").is_empty()) else { return (false, Vec::new()) };
    let mut out = Vec::new();
    if a.get("status").and_then(Value::as_str) != Some("enrolled") {
        return (true, out);
    }
    let civil = campus::civil_day(day);
    let (term, phase) = campus::term_at(civil);
    if phase == Phase::Break || a.get("term").and_then(Value::as_str) != Some(term.key.as_str()) {
        return (true, out);
    }
    let weekday = date(day, 12.0).weekday as i32;
    let mut rng = RandomStream::new(day.wrapping_mul(31).wrapping_add(h.actor.wrapping_mul(977)));
    let num = |s: &str, f: &str| h.profile.get(s).and_then(|x| x.get(f)).and_then(Value::as_f64).map(|v| v as i32).unwrap_or(50) as f32;
    let (competence, drive) = (num("occupation", "competence"), num("traits", "competitiveness"));
    let r = &h.relation;
    let low = r.emotion_intensity > 0.2 && (r.emotion == "低落" || r.emotion == "疲倦");
    let attend = (0.45 + competence / 400.0 + drive / 500.0 - if low { 0.2 } else { 0.0 }).clamp(0.3, 0.92);
    // Her own study: an evening at the library or at home, nearly every day before exams.
    let cram = phase != Phase::Classes || (civil >= term.midterm - 7 && civil < term.midterm) || civil >= term.fin - 7;
    if rng.frand() < (0.12 + drive / 400.0 + competence / 600.0 + if cram { 0.45 } else { 0.0 } - if low { 0.1 } else { 0.0 }).clamp(0.05, 0.95) {
        let home = Some(pstr(&h.profile, None, "home_place_id")).filter(|x| !x.is_empty()).unwrap_or("girls_dorm");
        let place = if (1..=5).contains(&weekday) && rng.frand() < 0.6 { "library" } else { home };
        out.push(block(1140, if cram { 1320 } else { 1260 }, place, "study", "timetable", "study", 0));
    }
    if phase != Phase::Classes || weekday >= 5 {
        return (true, out);
    }
    const SLOT_START: [i32; 2] = [540, 810];
    const SLOT_END: [i32; 2] = [720, 990];
    for cl in a.get("courses").and_then(Value::as_array).into_iter().flatten() {
        let n = |k: &str| cl.get(k).and_then(Value::as_i64).unwrap_or(0) as i32;
        if n("weekday") != weekday || rng.frand() > attend {
            continue;
        }
        let slot = n("slot").clamp(0, 1) as usize;
        let building = cl.get("building").and_then(Value::as_str).unwrap_or("");
        out.push(block(SLOT_START[slot], SLOT_END[slot], building, "class", "timetable", &format!("class{}_{}", n("floor"), n("room")), 0));
    }
    (true, out)
}

/// The whole street's plans for a day. Request: day, now (game minute), haunt, bond_heat [[key, heat]...], doors
/// {place: [x, y]}, place_names, heroines.
pub fn plan_all(c: &Catalog, r: &Value) -> Result<Value, String> {
    let day = r.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
    let now = r.get("now").and_then(Value::as_i64).unwrap_or(0);
    let haunt = r.get("haunt").and_then(Value::as_str).unwrap_or("").to_string();
    let heroines: Vec<Heroine> = serde_json::from_value(r.get("heroines").cloned().unwrap_or(json!([]))).map_err(|e| format!("heroines: {e}"))?;
    let bonds: Vec<(String, f32)> = serde_json::from_value(r.get("bond_heat").cloned().unwrap_or(json!([]))).unwrap_or_default();
    let doors: BTreeMap<String, (f32, f32)> = serde_json::from_value(r.get("doors").cloned().unwrap_or(json!({}))).unwrap_or_default();
    let names: BTreeMap<String, String> = serde_json::from_value(r.get("place_names").cloned().unwrap_or(json!({}))).unwrap_or_default();
    let weekday = date(day, 8.0).weekday as i32;
    let exam_week = crate::events::is_exam(day);
    let mut fx = Effects::default();
    let mut plans: Vec<(i32, Vec<Block>)> = Vec::new();
    for h in &heroines {
        let (stress, _) = crate::mind::money_stress(&h.relation, &h.profile);
        let mut stress = stress;
        // Tomorrow's intent from last night's reflection.
        if h.intent == "more_shifts" || h.intent == "save_money" {
            stress = stress.max(70.0);
        }
        let mut stalk = String::new();
        if h.intent == "seek_player" && !haunt.is_empty() {
            stalk = haunt.clone();
        }
        if crate::bias::peak_jealousy(&h.relation).0 >= 61.0 {
            stalk = haunt.clone();
        }
        let promises = h.promises.iter().map(|p| block(p.start, p.start + 90, &p.place, "promise", "promise", "", 0)).collect();
        let (timetable, classes) = campus_blocks(h, day);
        let inputs = Inputs {
            profile: &h.profile,
            weekday,
            money_stress: stress,
            energy: h.relation.energy,
            wealth: h.relation.cash.max(0) + h.relation.bank + h.invest_total,
            promises,
            stalk_place: stalk,
            rainy: false,
            exam_week,
            company_place: h.company_place.clone(),
            company_role: h.company_role.clone(),
            classes,
            timetable,
            seed: day.wrapping_mul(7919).wrapping_add(h.actor.wrapping_mul(104729)),
        };
        let plan = plan_day(&inputs, c);
        fx.log(format!("Street plan day {day} actor {}: {}", h.actor, summary(&plan)));
        plans.push((h.actor, plan));
    }
    // Everyone's own morning: up between 06:30 and 09:00 (an hour before an early fixed block at the latest), breakfast
    // once up, free time slid by her own few minutes.
    let mut wakes: BTreeMap<i32, i32> = BTreeMap::new();
    for (actor, plan) in plans.iter_mut() {
        let mut rng = RandomStream::new(day.wrapping_mul(31337).wrapping_add(actor.wrapping_mul(977)));
        let mut wake = 390 + rng.rand_range(0, 150);
        for b in plan.iter() {
            if fixed(b) && b.start < 720 {
                wake = wake.min((b.start - 60).max(390));
            }
        }
        let slide = rng.rand_range(0, 45);
        for b in plan.iter_mut() {
            if b.kind == "sleep" && b.start == 0 {
                b.end = wake;
                continue;
            }
            if fixed(b) {
                continue;
            }
            if b.kind == "meal" && b.start < 540 {
                b.start = wake + rng.rand_range(10, 25);
                b.end = b.start + 30;
                continue;
            }
            let len = b.end - b.start;
            b.start += if b.kind == "meal" { rng.rand_range(-15, 20) } else { slide };
            b.end = b.start + len;
        }
        settle(plan, wake);
        wakes.insert(*actor, wake);
    }
    let actor_of = |id: &str| heroines.iter().find(|h| h.id == id).map(|h| h.actor);
    // The best friends (if any pair is close enough) share the morning: she wakes with her friend and goes along.
    let mut morning_pair = json!(null);
    {
        let mut best = 60.0f32;
        let mut key = String::new();
        for (k, v) in &bonds {
            if *v >= best {
                best = *v;
                key = k.clone();
            }
        }
        if let Some((ida, idb)) = key.split_once('|') {
            if let (Some(a), Some(b)) = (actor_of(ida), actor_of(idb)) {
                if plans.iter().any(|p| p.0 == a) && plans.iter().any(|p| p.0 == b) {
                    let from = wakes[&a];
                    let theirs: Vec<Block> = plans.iter().find(|p| p.0 == a).unwrap().1.iter().filter(|x| !fixed(x) && x.start >= from && x.start < 720 && x.kind != "sleep" && !(x.kind == "meal" && x.start >= 600)).map(|x| Block { end: x.end.min(720), ..x.clone() }).filter(|x| x.end - x.start >= 15).collect();
                    let mine = &mut plans.iter_mut().find(|p| p.0 == b).unwrap().1;
                    // Her own lunch stays hers; the rest of the morning follows her friend.
                    let lunch = |x: &Block| x.kind == "meal" && x.start >= 600;
                    mine.retain(|x| lunch(x) || !(!fixed(x) && x.start < 720 && x.end > from && !(x.kind == "sleep" && x.start == 0)));
                    for x in mine.iter_mut() {
                        if x.kind == "sleep" && x.start == 0 {
                            x.end = from;
                        }
                    }
                    mine.extend(theirs);
                    settle(mine, from);
                    morning_pair = json!([a, b]);
                    fx.log(format!("Street plan: actors {a} and {b} (bond {best:.0}) spend the morning together"));
                }
            }
        }
    }
    // Errands: now and then a free hour goes to a quick errand.
    let rainy = raining_at(day, 900);
    for (actor, plan) in plans.iter_mut() {
        let mut rng = RandomStream::new(day.wrapping_mul(4447).wrapping_add(actor.wrapping_mul(613)));
        if rng.frand() >= 0.45 {
            continue;
        }
        let free: Vec<usize> = plan.iter().enumerate().filter(|(_, x)| x.kind == "leisure" && x.start >= 600 && x.end <= 1200).map(|(k, _)| k).collect();
        if free.is_empty() {
            continue;
        }
        const SHOPS: [&str; 4] = ["conveni", "bookstore", "department", "conveni"];
        let k = free[rng.rand_range(0, free.len() as i32 - 1) as usize];
        let x = &mut plan[k];
        x.kind = "errand".into();
        x.place = SHOPS[rng.rand_range(0, 3) as usize].into();
        x.source = "errand".into();
        x.budget = crate::region::amount("goods", 60) as i32;
        x.end = x.end.min(x.start + 40);
    }
    // Friends meet: up to three pairs who get on (bond heat 40+) spend an hour together, if both are free then.
    {
        let mut list: Vec<(String, f32)> = bonds.iter().filter(|b| b.1 >= 40.0).cloned().collect();
        list.sort_by(|l, r| r.1.partial_cmp(&l.1).unwrap_or(std::cmp::Ordering::Equal));
        let mut rng = RandomStream::new(day.wrapping_mul(1999).wrapping_add(7));
        let mut busy: Vec<i32> = Vec::new();
        let mut met = 0;
        for (key, heat) in &list {
            if met >= 3 {
                break;
            }
            let Some((fa, fb)) = key.split_once('|') else { continue };
            let (Some(a), Some(b)) = (actor_of(fa), actor_of(fb)) else { continue };
            if !plans.iter().any(|p| p.0 == a) || !plans.iter().any(|p| p.0 == b) || busy.contains(&a) || busy.contains(&b) {
                continue;
            }
            const SPOTS: [&str; 4] = ["cafe", "restaurant", "department", "park"];
            let mut meet = block(0, 0, SPOTS[rng.rand_range(0, if rainy { 2 } else { 3 }) as usize], "social", "friends", "", crate::region::amount("goods", 80) as i32);
            for hour in [19, 15, 20, 16, 13] {
                meet.start = hour * 60 + rng.rand_range(0, 2) * 10;
                meet.end = meet.start + 60;
                let mut da = plans.iter().find(|p| p.0 == a).unwrap().1.clone();
                let mut db = plans.iter().find(|p| p.0 == b).unwrap().1.clone();
                if insert(&mut da, &meet) && insert(&mut db, &meet) {
                    plans.iter_mut().find(|p| p.0 == a).unwrap().1 = da;
                    plans.iter_mut().find(|p| p.0 == b).unwrap().1 = db;
                    busy.push(a);
                    busy.push(b);
                    met += 1;
                    fx.log(format!("Street plan: actors {a} and {b} meet at {} {:02}:{:02} (bond {heat:.0})", meet.place, meet.start / 60, meet.start % 60));
                    break;
                }
            }
        }
    }
    // Classes, shifts and promises: she sets off in time to be there 1-10 minutes early (2.75 m a game minute).
    for (actor, plan) in plans.iter_mut() {
        let mut rng = RandomStream::new(day.wrapping_mul(7717).wrapping_add(actor.wrapping_mul(131)));
        let mut early = Vec::new();
        for k in 1..plan.len() {
            let (f, before) = (&plan[k], &plan[k - 1]);
            if !(f.kind == "class" || f.kind == "work" || f.kind == "promise") || f.place.is_empty() || before.place == f.place {
                continue;
            }
            let metres = match (doors.get(&f.place), doors.get(&before.place).filter(|_| !before.place.is_empty())) {
                (Some(to), Some(from)) => ((to.0 - from.0).powi(2) + (to.1 - from.1).powi(2)).sqrt() / 100.0 * 1.3,
                _ => 22.0,
            };
            let walk = ((metres / (110.0 / 100.0 * 60.0 / 24.0)).ceil() as i32).clamp(3, 60);
            let leave = (f.start - walk - rng.rand_range(1, 10)).max(before.start + 10);
            if leave >= f.start {
                continue;
            }
            let mut go = f.clone();
            go.start = leave;
            go.end = f.start;
            go.source = "early".into();
            early.push(go);
        }
        for go in early {
            for x in plan.iter_mut() {
                if x.start < go.start && x.end > go.start && !fixed(x) {
                    x.end = go.start;
                }
            }
            plan.push(go);
        }
        plan.sort_by_key(|b| b.start);
    }
    // Outdoor free time in the rain moves indoors (or home).
    for (actor, plan) in plans.iter_mut() {
        let home = heroines.iter().find(|h| h.actor == *actor).map(|h| pstr(&h.profile, None, "home_place_id").to_string()).filter(|x| !x.is_empty()).unwrap_or_else(|| "girls_dorm".into());
        for b in plan.iter_mut() {
            if b.kind != "leisure" && b.kind != "social" && b.kind != "errand" {
                continue;
            }
            let outdoor = b.place.is_empty() || b.place == "park" || b.place == "playground" || b.place == "shrine";
            if !outdoor || !raining_at(day, b.start) {
                continue;
            }
            const INDOOR: [&str; 4] = ["cafe", "library", "bookstore", "department"];
            let go_home = b.kind == "leisure" && RandomStream::new(actor.wrapping_mul(97).wrapping_add(b.start)).frand() < 0.3;
            b.place = if go_home { home.clone() } else { INDOOR[RandomStream::new(actor.wrapping_mul(31).wrapping_add(b.start)).rand_range(0, 3) as usize].into() };
            if go_home {
                b.kind = "rest".into();
            }
            b.source = "rain".into();
        }
    }
    // Pickup: a friend or lover with class or work ending in the evening may ask him to come for her.
    let mut pickups = Vec::new();
    // His evening is one person's: two girls asking him to come for them both at 17:00 in different places (and a
    // meeting he already has at 17:30) left him a promise he could not keep. Within 90 minutes of a meeting he
    // already has, or of a pickup already asked for, nobody asks.
    let mut taken: Vec<i64> = r.get("his_meetings").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_i64).collect();
    let mut text_rng = RandomStream::new(r.get("seed").and_then(Value::as_i64).unwrap_or(0) as i32);
    for (actor, plan) in &plans {
        let Some(h) = heroines.iter().find(|h| h.actor == *actor) else { continue };
        let rel = &h.relation;
        if rel.stage == "stranger" || rel.liking() < 45.0 {
            continue;
        }
        let mut rng = RandomStream::new(day.wrapping_mul(911).wrapping_add(actor.wrapping_mul(53)));
        if rng.frand() >= if rel.stage == "lover" { 0.3 } else { 0.12 } {
            continue;
        }
        for b in plan {
            if !(b.kind == "class" || b.kind == "work") || b.end < 1020 || b.end > 1320 || b.place.is_empty() {
                continue;
            }
            // Class until five and her shift from five: nothing to come for her for ("五点到了。我先去打工").
            if plan.iter().any(|n| (n.kind == "class" || n.kind == "work") && n.start >= b.end && n.start < b.end + 30) {
                continue;
            }
            let Some(place_name) = names.get(&b.place) else { break };
            let start = day as i64 * 1440 + b.end as i64;
            if start <= now + 60 || taken.iter().any(|t| (t - start).abs() < 90) {
                break;
            }
            taken.push(start);
            pickups.push(json!({"actor": actor, "place": b.place, "start": start, "grace": 20, "label": crate::tr::m("plan.go_pick_up", &[("place", crate::tr::a(&place_name)), ("name", crate::tr::a(&h.name)), ("hour", crate::tr::a(&(b.end / 60))), ("minute", crate::tr::a(&(b.end % 60)))])}));
            let delay = text_rng.frand_range(20.0, 90.0);
            fx.text(&h.id, &crate::tr::m("plan.pickup_offer_end_time", &[("activity", crate::tr::a(&(if b.kind == "class" { crate::tr::t("plan.class_label") } else { crate::tr::t("plan.part_time_job") }))), ("hour", crate::tr::a(&(b.end / 60))), ("minute", crate::tr::a(&(b.end % 60))), ("place", crate::tr::a(&place_name))]), "pickup", delay);
            fx.log(format!("Street pickup: actor {actor} asks to be picked up at {} {:02}:{:02}", b.place, b.end / 60, b.end % 60));
            break;
        }
    }
    for (actor, plan) in &plans {
        fx.log(format!("Street plan staggered day {day} actor {actor}: {}", summary(plan)));
    }
    let plans: serde_json::Map<String, Value> = plans.into_iter().map(|(a, p)| (a.to_string(), json!(p))).collect();
    Ok(json!({"plans": plans, "morning_pair": morning_pair, "pickups": pickups, "effects": fx.into_value()}))
}

fn summary(plan: &[Block]) -> String {
    plan.iter().map(|x| format!("{:02}:{:02} {} {}", x.start / 60, x.start % 60, x.kind, if x.place.is_empty() { "street" } else { &x.place })).collect::<Vec<_>>().join(", ")
}

// ------------------------------------------------------------------------------------------------ through the day

/// A fixed block (class, work, a promise, the night) is only cut into when the need is really low.
fn fixed_block(b: Option<&Block>) -> bool {
    b.map(|b| b.kind == "class" || b.kind == "work" || b.kind == "promise" || b.kind == "sleep").unwrap_or(false)
}

#[derive(Deserialize, Debug, Clone)]
struct Tick {
    actor: i32,
    satiety: f32,
    energy: f32,
    hygiene: f32,
    #[serde(default)]
    block: Option<Block>,
    /// The block index she was in (-2 eating, -3 resting, -4 bathing when a cut-in held her).
    #[serde(default = "none")]
    previous: i32,
    #[serde(default)]
    bathing: bool,
    #[serde(default)]
    home: String,
}

fn none() -> i32 {
    -1
}

/// Every half second: urgent needs cut in before the plan (hungry eats until 50, worn out rests until 40, grimy
/// bathes until 80; a class, shift or promise only when it is really bad). Request: minute, cafeteria_open, heroines.
pub fn schedule_tick(r: &Value) -> Result<Value, String> {
    let minute = r.get("minute").and_then(Value::as_i64).unwrap_or(0) as i32;
    let cafeteria_open = r.get("cafeteria_open").and_then(Value::as_bool).unwrap_or(true);
    let ticks: Vec<Tick> = serde_json::from_value(r.get("heroines").cloned().unwrap_or(json!([]))).map_err(|e| format!("heroines: {e}"))?;
    let mut out = Vec::new();
    for t in ticks {
        let b = t.block.as_ref();
        let home = if t.home.is_empty() { "girls_dorm".to_string() } else { t.home.clone() };
        let mut urgent = Value::Null;
        let mut bathing = t.bathing;
        if t.satiety < if t.previous == -2 { 50.0 } else { 20.0 } && b.map(|b| b.kind != "meal").unwrap_or(true) && (!fixed_block(b) || t.satiety < 5.0) {
            urgent = json!({"index": -2, "block": block(minute, minute + 45, if cafeteria_open { "cafeteria" } else { "conveni" }, "meal", "", "", crate::region::amount("goods", 80) as i32)});
        } else if t.energy < if t.previous == -3 { 40.0 } else { 15.0 } && b.map(|b| b.kind != "sleep").unwrap_or(true) && (!fixed_block(b) || t.energy < 5.0) {
            urgent = json!({"index": -3, "block": block(minute, minute + 120, &home, "rest", "", "", 0)});
        } else {
            // Grimy (hygiene under 15) and free: home to wash first; clean again, or class, work or a promise begins.
            if bathing && (t.hygiene >= 80.0 || (fixed_block(b) && t.hygiene >= 5.0)) {
                bathing = false;
            } else {
                let free = b.map(|b| !(b.kind == "sleep" || b.kind == "class" || b.kind == "work" || b.kind == "promise")).unwrap_or(true);
                if bathing || (t.hygiene < 15.0 && free) {
                    urgent = json!({"index": -4, "block": block(minute, minute + 45, &home, "bath", "", "", 0), "starts": !bathing});
                    bathing = true;
                }
            }
        }
        out.push(json!({"actor": t.actor, "urgent": urgent, "bathing": bathing}));
    }
    Ok(json!({"heroines": out}))
}

/// What she orders when a meal block starts and she pays for it (Request: place, month, budget, seed, relation, profile).
/// What a meal that costs nothing fills (half of a proper one).
const FRUGAL_MEAL_SATIETY: f32 = 25.0;

pub fn meal(c: &Catalog, r: &Value) -> Result<Value, String> {
    let ctx = crate::heroine::Ctx::from_request(r)?;
    let place = r.get("place").and_then(Value::as_str).unwrap_or("");
    let mut rng = RandomStream::new(r.get("seed").and_then(Value::as_i64).unwrap_or(0) as i32);
    // Nothing she can pay for (broke after the fees, or nothing here within her budget): a meal that costs nothing (the
    // bread she brought, instant noodles in the dorm), not none at all: she stayed at 0 satiety for days, no money.
    let frugal = |rel: &crate::relation::Relation| {
        let mut fed = rel.clone();
        fed.satiety = (fed.satiety + FRUGAL_MEAL_SATIETY).clamp(0.0, 100.0);
        Ok(json!({"item": "frugal_meal", "name": crate::tr::t("plan.frugal_meal"), "price": 0, "frugal": true, "relation": fed.patch(rel)}))
    };
    let Some(item) = catalog::pick_meal(c, place, r.get("month").and_then(Value::as_i64).unwrap_or(1) as i32, &ctx.profile, r.get("budget").and_then(Value::as_i64).unwrap_or(0) as i32, &mut rng) else {
        return frugal(&ctx.rel);
    };
    let price = item.price_of(&item.default_size());
    let mut rel = ctx.rel.clone();
    if rel.cash >= 0 && rel.cash < price as i64 {
        return frugal(&ctx.rel);
    }
    if rel.cash >= 0 {
        rel.cash -= price as i64;
    }
    for (k, v) in &item.effects {
        let f = match k.as_str() {
            "satiety" => &mut rel.satiety,
            "energy" => &mut rel.energy,
            "fun" => &mut rel.fun,
            _ => continue,
        };
        *f = (*f + v).clamp(0.0, 100.0);
    }
    Ok(json!({"item": item.id, "name": item.name, "price": price, "relation": rel.patch(&ctx.rel)}))
}

/// Several picks from one menu with one random stream (her meal and his at an activity). Request: place, month, budget,
/// seed, picks [profile or null, ...]. Answer: items [{id, name, price, effects} or null, ...].
pub fn meal_pick(c: &Catalog, r: &Value) -> Result<Value, String> {
    let place = r.get("place").and_then(Value::as_str).unwrap_or("");
    let month = r.get("month").and_then(Value::as_i64).unwrap_or(1) as i32;
    let budget = r.get("budget").and_then(Value::as_i64).unwrap_or(0) as i32;
    let mut rng = RandomStream::new(r.get("seed").and_then(Value::as_i64).unwrap_or(0) as i32);
    let items: Vec<Value> = r.get("picks").and_then(Value::as_array).into_iter().flatten().map(|profile| {
        catalog::pick_meal(c, place, month, profile, budget, &mut rng).map(|i| json!({"id": i.id, "name": i.name, "price": i.price_of(&i.default_size()), "effects": i.effects})).unwrap_or(Value::Null)
    }).collect();
    Ok(json!({"items": items}))
}

/// The place in her plan is closed: the nearest open place of the same kind, else home to rest.
pub fn open_alternative(c: &Catalog, places: &PlacesTuning, r: &Value) -> Result<Value, String> {
    let b: Block = serde_json::from_value(r.get("block").cloned().ok_or("missing_field: block")?).map_err(|e| format!("block: {e}"))?;
    let doors: BTreeMap<String, (f32, f32)> = serde_json::from_value(r.get("doors").cloned().unwrap_or(json!({}))).unwrap_or_default();
    let from: (f32, f32) = serde_json::from_value(r.get("from").cloned().unwrap_or(json!([0, 0]))).unwrap_or((0.0, 0.0));
    let hours = r.get("hours").and_then(Value::as_f64).unwrap_or(12.0);
    let weekday = r.get("weekday").and_then(Value::as_i64).unwrap_or(0) as i32;
    let label = &places.norms(&b.place).label;
    let mut best = f32::MAX;
    let mut alt = b.clone();
    for (id, door) in &doors {
        if *id == b.place || places.norms(id).label != *label || !c.place_open(id, hours, weekday) {
            continue;
        }
        let d = ((door.0 - from.0).powi(2) + (door.1 - from.1).powi(2)).sqrt();
        if d < best {
            best = d;
            alt.place = id.clone();
        }
    }
    if best == f32::MAX {
        alt.kind = "rest".into();
        let home = r.get("home").and_then(Value::as_str).unwrap_or("");
        alt.place = if home.is_empty() { "girls_dorm".into() } else { home.into() };
    }
    Ok(json!({"block": alt}))
}

/// Her day with today's meetings with him as they stand now (one made, moved or called off after the day was planned:
/// "lunch at noon?" said at nine). Earlier promise blocks go; each meeting takes [start, start + 90) and the blocks it
/// overlaps are cut back or dropped. Request: blocks [Block], promises [{start, place}]. Answer: blocks.
fn plan_set_promises(r: &Value) -> Result<Value, String> {
    let mut day: Vec<Block> = serde_json::from_value(r.get("blocks").cloned().unwrap_or(json!([]))).map_err(|e| format!("blocks: {e}"))?;
    day.retain(|b| b.kind != "promise");
    for p in r.get("promises").and_then(Value::as_array).into_iter().flatten() {
        let start = p.get("start").and_then(Value::as_i64).unwrap_or(0) as i32;
        let end = start + 90;
        let place = p.get("place").and_then(Value::as_str).unwrap_or("");
        let mut kept = Vec::with_capacity(day.len() + 2);
        for b in day.drain(..) {
            if b.end <= start || b.start >= end {
                kept.push(b);
                continue;
            }
            // the part before the meeting, and the part after it, of what she had planned
            if b.start < start {
                kept.push(Block { end: start, ..b.clone() });
            }
            if b.end > end {
                kept.push(Block { start: end, ..b });
            }
        }
        kept.push(block(start, end, place, "promise", "promise", "", 0));
        kept.sort_by_key(|b| b.start);
        day = kept;
    }
    Ok(json!({"blocks": day}))
}

pub fn eval(c: &Catalog, places: &PlacesTuning, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    let day = || r.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    Some(match kind {
        "plan_all" => plan_all(c, r),
        "schedule_tick" => schedule_tick(r),
        "plan_meal" => meal(c, r),
        "meal_pick" => meal_pick(c, r),
        "plan_alternative" => open_alternative(c, places, r),
        "plan_set_promises" => plan_set_promises(r),
        "weather" => Ok(json!({"raining": raining_at(day(), r.get("minute").and_then(Value::as_i64).unwrap_or(0) as i32)})),
        "holiday" => Ok(json!({"holiday": holiday_on(day())})),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn a_meeting_made_today_goes_into_her_day() {
        let day = json!([{"start": 660, "end": 780, "place": "cafe", "kind": "leisure"}, {"start": 780, "end": 840, "place": "department", "kind": "meal"}]);
        let out = plan_set_promises(&json!({"blocks": day, "promises": [{"start": 720, "place": "restaurant"}]})).unwrap();
        let b: Vec<Block> = serde_json::from_value(out["blocks"].clone()).unwrap();
        assert_eq!(b.iter().map(|x| (x.start, x.end, x.kind.as_str())).collect::<Vec<_>>(), vec![(660, 720, "leisure"), (720, 810, "promise"), (810, 840, "meal")]);
        // called off: the promise block goes again
        let out = plan_set_promises(&json!({"blocks": out["blocks"], "promises": []})).unwrap();
        assert!(out["blocks"].as_array().unwrap().iter().all(|x| x["kind"] != "promise"));
    }

    fn inputs(profile: &Value, weekday: i32, seed: i32) -> Inputs<'_> {
        Inputs { profile, weekday, money_stress: 0.0, energy: 90.0, wealth: 0, promises: vec![], stalk_place: String::new(), rainy: false, exam_week: false, company_place: String::new(), company_role: String::new(), classes: vec![], timetable: false, seed }
    }

    #[test]
    fn breakfast_is_not_one_shop_for_everybody() {
        let c = Tuning::builtin().catalog;
        let student = json!({"occupation": {"primary": "student"}});
        let mut places = std::collections::BTreeSet::new();
        let mut starts = std::collections::BTreeSet::new();
        for k in 0..10 {
            for weekday in [2, 5] {
                let plan = plan_day(&inputs(&student, weekday, 3 * 7919 + k * 104729), &c);
                let breakfast = plan.iter().filter(|x| x.kind == "meal" && x.start < 600).min_by_key(|x| x.start).expect("breakfast");
                assert!(breakfast.place != "cafe" || breakfast.start >= 480, "the cafe opens at 08:00: {breakfast:?}");
                places.insert(breakfast.place.clone());
                starts.insert(breakfast.start);
            }
        }
        assert!(places.len() >= 3, "ten heroines, one breakfast place: {places:?}");
        assert!(starts.len() >= 2, "ten heroines, one breakfast hour: {starts:?}");
    }
    #[test]
    fn dinner_is_not_one_restaurant_for_everybody() {
        let c = Tuning::builtin().catalog;
        let student = json!({"occupation": {"primary": "student"}});
        let mut places = std::collections::BTreeSet::new();
        let mut starts = std::collections::BTreeSet::new();
        for k in 0..10 {
            let plan = plan_day(&inputs(&student, 3, 4 * 7919 + k * 104729), &c);
            let dinner = plan.iter().filter(|x| x.kind == "meal" && x.start >= 960).min_by_key(|x| x.start).expect("dinner");
            assert!(dinner.place != "cafeteria" || dinner.end <= 1140, "the cafeteria closes at 19:00: {dinner:?}");
            places.insert(dinner.place.clone());
            starts.insert(dinner.start);
        }
        assert!(places.len() >= 2, "ten heroines, one dinner place: {places:?}");
        assert!(starts.len() >= 2, "ten heroines, one dinner hour: {starts:?}");
    }
    #[test]
    fn a_day_is_well_formed() {
        // Ported from Tests/OKStreetLongRun.spec.cpp: blocks run forward, stay inside the day, never overlap, she sleeps.
        let c = Tuning::builtin().catalog;
        for (k, p) in [json!({"occupation": {"primary": "student", "workplace_place_id": "drinkshop"}}), json!({"occupation": {"primary": "clerk", "workplace_place_id": "conveni"}}), json!({})].iter().enumerate() {
            for day in 0..30 {
                let plan = plan_day(&inputs(p, day % 7, day * 7919 + k as i32 * 104729), &c);
                assert!(!plan.is_empty());
                for (a, x) in plan.iter().enumerate() {
                    assert!(x.end > x.start && x.start >= 0 && x.end <= 24 * 60 + 12 * 60, "{x:?}");
                    for y in &plan[a + 1..] {
                        assert!(!(x.start < y.end && y.start < x.end), "overlap {x:?} {y:?}");
                    }
                }
                assert!(plan.iter().any(|x| x.kind == "sleep"));
                assert!(plan.iter().filter(|x| x.kind == "meal").count() >= 2, "she eats");
            }
        }
    }

    #[test]
    fn promises_and_company_come_first() {
        let c = Tuning::builtin().catalog;
        let p = json!({});
        let mut i = inputs(&p, 1, 5);
        i.promises = vec![block(720, 810, "cafe", "promise", "promise", "", 0)];
        i.company_place = "dept_office".into();
        let plan = plan_day(&i, &c);
        assert!(plan.iter().any(|x| x.kind == "promise" && x.start == 720));
        assert!(!plan.iter().any(|x| x.source == "company"), "blocks never overlap: the promise wins over the company day");
        i.promises.clear();
        let plan = plan_day(&i, &c);
        assert!(plan.iter().any(|x| x.kind == "work" && x.source == "company" && x.start == 600 && x.end == 1080));
    }

    #[test]
    fn weather_and_holidays() {
        let rainy_days = (0..365).filter(|d| (0..1440).step_by(60).any(|m| raining_at(*d, m))).count();
        assert!(rainy_days > 40 && rainy_days < 140, "{rainy_days}");
        assert_eq!(holiday_on(21), "中秋節（大家會烤肉、賞月）");
        assert!(holiday_on(69).contains("期中考"));
        assert_eq!(holiday_on(5), "");
    }

    #[test]
    fn urgent_cut_ins() {
        let v = schedule_tick(&json!({"minute": 600, "cafeteria_open": false, "heroines": [
            {"actor": 1, "satiety": 10, "energy": 90, "hygiene": 90},
            {"actor": 2, "satiety": 60, "energy": 10, "hygiene": 90, "home": "lodge"},
            {"actor": 3, "satiety": 60, "energy": 90, "hygiene": 10},
            {"actor": 4, "satiety": 10, "energy": 90, "hygiene": 90, "block": {"kind": "class", "start": 540, "end": 720}},
            {"actor": 5, "satiety": 45, "energy": 90, "hygiene": 90, "previous": -2}]})).unwrap();
        let h = v["heroines"].as_array().unwrap();
        assert_eq!((h[0]["urgent"]["index"].as_i64(), h[0]["urgent"]["block"]["place"].as_str()), (Some(-2), Some("conveni")));
        assert_eq!(h[1]["urgent"]["block"]["place"], "lodge");
        assert_eq!((h[2]["urgent"]["index"].as_i64(), h[2]["bathing"].as_bool()), (Some(-4), Some(true)));
        assert!(h[3]["urgent"].is_null(), "class: only when starving");
        assert_eq!(h[4]["urgent"]["index"], -2, "keeps eating until 50");
    }

    #[test]
    fn the_street_plans_a_day() {
        let c = Tuning::builtin().catalog;
        let heroine = |actor: i32, id: &str| json!({"actor": actor, "id": id, "name": id, "profile": {}, "relation": {"id": id, "stage": "lover", "affection": 80}});
        let v = plan_all(&c, &json!({"day": 3, "now": 0, "bond_heat": [["a|b", 70.0]], "doors": {"cafe": [0, 0], "lecture": [10000, 0]}, "place_names": {"lecture": "教室"},
            "heroines": [heroine(1, "a"), heroine(2, "b")]})).unwrap();
        assert_eq!(v["morning_pair"], json!([1, 2]));
        // Over many days with five lovers: never two pickups within 90 minutes, nor one on top of his own meeting.
        let five: Vec<Value> = (1..=5)
            .map(|k| json!({"actor": k, "id": format!("h{k}"), "name": format!("h{k}"), "relation": {"id": format!("h{k}"), "stage": "lover", "favor": 150},
                "profile": {"occupation": {"primary": "student", "role": "staff", "workplace_place_id": "lecture"}}}))
            .collect();
        let mut asked = 0;
        for day in 3..40 {
            let meet = day as i64 * 1440 + 18 * 60;
            let v = plan_all(&c, &json!({"day": day, "now": 0, "doors": {"cafe": [0, 0], "lecture": [10000, 0]}, "place_names": {"lecture": "教室"}, "heroines": five, "his_meetings": [meet]})).unwrap();
            let starts: Vec<i64> = v["pickups"].as_array().unwrap().iter().map(|p| p["start"].as_i64().unwrap()).collect();
            asked += starts.len();
            for (i, a) in starts.iter().enumerate() {
                assert!((a - meet).abs() >= 90, "day {day}: pickup at {a} on top of his meeting");
                assert!(starts[i + 1..].iter().all(|b| (a - b).abs() >= 90), "day {day}: {starts:?}");
            }
        }
        assert!(asked > 0, "the test saw pickups at all");
        // The library is closed on Mondays (days 12, 19, 26): nobody works a shift there.
        let lib: Vec<Value> = (1..=5)
            .map(|k| json!({"actor": k, "id": format!("l{k}"), "name": format!("l{k}"), "relation": {"id": format!("l{k}"), "stage": "lover", "favor": 150},
                "profile": {"occupation": {"primary": "staff", "workplace_place_id": "library"}}}))
            .collect();
        let mut worked_other_days = 0;
        for day in 12..27 {
            let v = plan_all(&c, &json!({"day": day, "now": 0, "doors": {"cafe": [0, 0]}, "place_names": {"library": "圖書館"}, "heroines": lib})).unwrap();
            for p in v["plans"].as_object().unwrap().values() {
                let at_library = p.as_array().unwrap().iter().any(|b| b["kind"] == "work" && b["place"] == "library");
                if (day - 12) % 7 == 0 {
                    assert!(!at_library, "day {day} is a Monday");
                } else if at_library {
                    worked_other_days += 1;
                }
            }
        }
        assert!(worked_other_days > 0);
        for p in v["plans"].as_object().unwrap().values() {
            assert!(p.as_array().unwrap().iter().any(|b| b["kind"] == "sleep"));
        }
    }

    #[test]
    fn a_shared_morning_keeps_her_lunch() {
        // Following her friend's morning must not cost her lunch (playtest 2026-10-13: lunch vanished).
        let c = Tuning::builtin().catalog;
        let heroine = |actor: i32, id: &str| json!({"actor": actor, "id": id, "name": id, "profile": {}, "relation": {"id": id}});
        for day in 0..60 {
            let v = plan_all(&c, &json!({"day": day, "bond_heat": [["a|b", 90.0]], "heroines": [heroine(1, "a"), heroine(2, "b")]})).unwrap();
            let b = v["plans"]["2"].as_array().unwrap();
            assert!(b.iter().any(|x| x["kind"] == "meal" && (600..=900).contains(&x["start"].as_i64().unwrap())), "day {day}: {b:?}");
            for w in b.windows(2) {
                assert!(w[0]["end"].as_i64() <= w[1]["start"].as_i64(), "day {day}: overlap {w:?}");
            }
        }
    }
    #[test]
    fn a_broke_heroine_still_eats_something() {
        let c = crate::tuning::Tuning::builtin().catalog;
        let r = json!({"relation": {"id": "a", "cash": 3, "satiety": 0.0}, "profile": {}, "place": "cafeteria", "budget": 40, "month": 9, "seed": 1});
        let v = meal(&c, &r).unwrap();
        assert_eq!(v["frugal"], true, "{v}");
        assert_eq!(v["price"], 0);
    }
}
