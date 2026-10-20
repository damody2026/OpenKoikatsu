//! Plan §6.4-§6.7, §10, §13.3, §14: the street's social life. Appointments (waiting, arriving, being late, being stood
//! up, cancelling, the shop blacklist), drama modes and the foil, rumours and jealousy, two heroines meeting (gossip,
//! treats), being caught with someone else, heroines acting first, the Director's hidden needs, bias actions (coming
//! over, taking his hand, deferring to him), her own messages and whether she picks up. Unreal brings the facts (who
//! stands where, who he is with); every decision and every change is here.
use crate::bias;
use crate::catalog::profile_int;
use crate::effects::Effects;
use crate::relation::Relation;
use crate::rng::RandomStream;
use crate::traits::{profile_str, Traits};
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or("")
}
fn n(r: &Value, k: &str) -> f64 {
    r.get(k).and_then(Value::as_f64).unwrap_or(0.0)
}
fn b(r: &Value, k: &str) -> bool {
    r.get(k).and_then(Value::as_bool).unwrap_or(false)
}
fn rel_of(v: Option<&Value>) -> Result<Relation, String> {
    serde_json::from_value(v.cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))
}
fn profile(v: Option<&Value>) -> Value {
    v.cloned().filter(Value::is_object).unwrap_or_else(|| json!({}))
}

// ------------------------------------------------------------------------------------------------ drama modes

/// The global mode (player.json "drama_mode", default B), her override, then her orientation shifts it.
pub fn drama_mode(global: char, p: &Value) -> char {
    let o = profile_str(p, "drama_mode_override");
    let base = o.chars().next().filter(|c| ('A'..='C').contains(c)).unwrap_or(global);
    let mut mode = base as i32 - 'A' as i32;
    match profile_str(p, "orientation") {
        "Poly" | "Open" => mode -= 1,
        "Competitive" => mode += 1,
        _ => {}
    }
    char::from(b'A' + mode.clamp(0, 2) as u8)
}

/// The foil: the most competitive, most jealous heroine (120+ together); her gossip spreads harder.
pub fn foil<'a>(heroines: &'a [(String, Value)]) -> &'a str {
    let mut best = "";
    let mut score = -1;
    for (id, p) in heroines {
        let t = Traits::from_profile(p);
        let v = t.competitiveness + t.jealousy_prone;
        if v > score {
            score = v;
            best = id;
        }
    }
    if score >= 120 {
        best
    } else {
        ""
    }
}

fn drama_cast(r: &Value) -> Result<Value, String> {
    let global = s(r, "global_mode").chars().next().unwrap_or('B');
    let list: Vec<(String, Value)> = r.get("heroines").and_then(Value::as_array).into_iter().flatten().map(|h| (s(h, "id").to_string(), profile(h.get("profile")))).collect();
    let modes: Map<String, Value> = list.iter().map(|(id, p)| (id.clone(), json!(drama_mode(global, p).to_string()))).collect();
    Ok(json!({"modes": modes, "foil": foil(&list)}))
}

// ------------------------------------------------------------------------------------------------ jealousy and rumours

/// Hearing that he was with another heroine: jealousy toward her grows by mode, trait and how sure the news is.
pub fn jealousy_news(r: &mut Relation, t: &Traits, mode: char, is_foil: bool, about: &str, confidence: f32) {
    if mode == 'A' || r.id == about || (r.liking() < 25.0 && r.stage == "stranger") {
        return;
    }
    let gain = if mode == 'C' { 18.0 } else { 10.0 } * (0.5 + t.jealousy_prone as f32 / 100.0) * confidence * if is_foil { 1.5 } else { 1.0 };
    let toward = r.jealousy_toward.entry(about.to_string()).or_insert(0.0);
    *toward = (*toward + gain).min(100.0);
    r.jealousy = (r.jealousy + gain * 0.3).min(100.0);
}

/// Someone who may hear: her record, profile, mode and whether she is the foil.
#[derive(Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct Listener {
    id: String,
    name: String,
    actor: i32,
    relation: Option<Relation>,
    profile: Value,
    mode: String,
    foil: bool,
}

impl Listener {
    fn mode(&self) -> char {
        self.mode.chars().next().unwrap_or('B')
    }
    fn hear(&mut self, about: &str, confidence: f32, fx: &mut Effects) {
        let (t, mode, is_foil) = (Traits::from_profile(&self.profile), self.mode(), self.foil);
        let Some(rel) = self.relation.as_mut() else { return };
        let before = rel.clone();
        jealousy_news(rel, &t, mode, is_foil, about, confidence);
        fx.relation(rel, &before);
    }
}

fn jealousy_kind(r: &Value) -> Result<Value, String> {
    let mut l: Listener = serde_json::from_value(r.clone()).map_err(|e| format!("listener: {e}"))?;
    let mut fx = Effects::default();
    l.hear(s(r, "about"), n(r, "confidence") as f32, &mut fx);
    let before = rel_of(r.get("relation"))?;
    Ok(json!({"relation": l.relation.map(|x| x.patch(&before)).unwrap_or_default()}))
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(default)]
pub struct Rumor {
    pub id: i32,
    pub text: String,
    pub about: String,
    pub kind: String,
    pub heat: f32,
    pub confidence: f32,
    pub day: i32,
    pub known_by: Vec<String>,
    pub heard: BTreeMap<String, f32>,
}

impl Rumor {
    fn sure_of(&self, who: &str) -> f32 {
        *self.heard.get(who).unwrap_or(&self.confidence)
    }
}

/// A rumour about him (and maybe a heroine): known at once by the people involved and by heroines within 25 m, who saw
/// it. Request: about, about_mode, global_mode, kind, text, heat, secret, day, near [listener], rumors [{id, heat, day}].
fn rumor_make(r: &Value) -> Result<Value, String> {
    let about = s(r, "about");
    let global = s(r, "global_mode").chars().next().unwrap_or('B');
    if global == 'A' && !about.is_empty() && s(r, "about_mode") == "A" {
        return Ok(json!({"skip": true}));
    }
    let day = n(r, "day") as i32;
    let text = s(r, "text");
    let mut fx = Effects::default();
    let mut rumor = Rumor { kind: s(r, "kind").into(), text: text.into(), heat: n(r, "heat") as f32, confidence: 1.0, day, about: about.into(), ..Default::default() };
    if !about.is_empty() {
        rumor.known_by.push(about.into());
    }
    let mut near: Vec<Listener> = serde_json::from_value(r.get("near").cloned().unwrap_or(json!([]))).map_err(|e| format!("near: {e}"))?;
    let place = s(r, "place");
    for l in near.iter_mut().filter(|l| l.id != about) {
        if !rumor.known_by.contains(&l.id) {
            rumor.known_by.push(l.id.clone());
        }
        fx.observe(&l.id, "gossip", &crate::tr::m("social.saw_with_own_eyes", &[("text", crate::tr::a(&text))]), place, 0.5, "observation");
        if !about.is_empty() {
            l.hear(about, 1.0, &mut fx);
        }
    }
    if b(r, "secret") {
        rumor.heat *= 0.3;
    }
    // Old cold rumours fall away.
    let remove: Vec<i64> = r.get("rumors").and_then(Value::as_array).into_iter().flatten().filter(|x| n(x, "heat") < 5.0 || day - n(x, "day") as i32 > 30).map(|x| n(x, "id") as i64).collect();
    let keep_new = rumor.heat >= 5.0;
    Ok(json!({"skip": false, "rumor": if keep_new { json!(rumor) } else { Value::Null }, "remove": remove, "effects": fx.into_value()}))
}

// ------------------------------------------------------------------------------------------------ bond heat

/// Where two heroines' bond heat settles when they do not meet.
pub const BOND_HEAT_REST: f32 = 30.0;
/// The share of the way back to rest it drifts each day.
pub const BOND_HEAT_DRIFT: f32 = 0.04;

/// Warmth between two heroines grows less the warmer it already is (100 is never reached by meeting alone); cooling
/// (a negative gain) is not slowed.
pub fn bond_heat_add(heat: f32, gain: f32) -> f32 {
    let g = if gain > 0.0 { gain * (100.0 - heat).max(0.0) / 100.0 } else { gain };
    (heat + g).clamp(0.0, 100.0)
}

/// A day passes: every bond drifts a little back toward rest, so only friends who keep meeting stay close. Request:
/// bond_heat [[key, heat]]. Answer: bond_heat [[key, heat]] (only the changed).
fn bond_heat_daily(r: &Value) -> Result<Value, String> {
    let bonds: Vec<(String, f32)> = serde_json::from_value(r.get("bond_heat").cloned().unwrap_or(json!([]))).map_err(|e| format!("bond_heat: {e}"))?;
    let out: Vec<(String, f32)> = bonds.into_iter().map(|(k, h)| (k, h - (h - BOND_HEAT_REST) * BOND_HEAT_DRIFT)).collect();
    Ok(json!({"bond_heat": out}))
}

// ------------------------------------------------------------------------------------------------ two heroines meet

/// Two heroines meet: their bond, their feelings for each other, gossip both ways (never about the listener herself; C
/// distorts, A stays quiet, the foil spreads everything), and maybe one treats the other at a food place. Request: a, b
/// (listeners), heat, rumors (full), place, food_place, overheard, seed.
fn encounter(r: &Value) -> Result<Value, String> {
    let mut a: Listener = serde_json::from_value(r.get("a").cloned().ok_or("missing_field: a")?).map_err(|e| format!("a: {e}"))?;
    let mut bb: Listener = serde_json::from_value(r.get("b").cloned().ok_or("missing_field: b")?).map_err(|e| format!("b: {e}"))?;
    let mut rumors: Vec<Rumor> = serde_json::from_value(r.get("rumors").cloned().unwrap_or(json!([]))).map_err(|e| format!("rumors: {e}"))?;
    let place = s(r, "place");
    let today = n(r, "day") as i32;
    let mut rng = RandomStream::new(n(r, "seed") as i32);
    let mut fx = Effects::default();
    let mut heat = n(r, "heat") as f32;
    if heat <= 0.0 {
        heat = 30.0;
    }
    let jt = |l: &Listener, other: &str| l.relation.as_ref().and_then(|x| x.jealousy_toward.get(other).copied()).unwrap_or(0.0);
    let rivals = jt(&a, &bb.id) > 40.0 || jt(&bb, &a.id) > 40.0;
    heat = bond_heat_add(heat, if rivals { -3.0 } else { 3.0 });
    fx.bond_event(&a.id, &bb.id, if rivals { "encounter_rivals" } else { "encounter" });
    let mut changed = Vec::new();
    let mut learned: Vec<(usize, String, f32)> = Vec::new();
    let mut later: Vec<(usize, String, f32)> = Vec::new();
    let people = [&a, &bb];
    for (k, rm) in rumors.iter_mut().enumerate() {
        for (teller, listener) in [(0usize, 1usize), (1, 0)] {
            let (t, l) = (people[teller], people[listener]);
            if !rm.known_by.contains(&t.id) || rm.known_by.contains(&l.id) || rm.about == l.id {
                continue;
            }
            let mode = t.mode();
            if mode == 'A' {
                continue;
            }
            // Her own secret dates travel less; the foil spreads everything.
            let own_secret = rm.about == t.id && rm.kind != "treat" && rm.kind != "wealth";
            let chance = (rm.heat / 100.0) * (heat / 100.0 + 0.3) * if mode == 'C' { 1.3 } else { 1.0 } * if own_secret { 0.3 } else { 1.0 } * if t.foil { 1.6 } else { 1.0 };
            if rng.frand() >= chance {
                continue;
            }
            rm.known_by.push(l.id.clone());
            // How sure she is: the teller's own certainty, less for being second-hand (the foil tells it as fact).
            let conf = if t.foil { rm.sure_of(&t.id) } else { rm.sure_of(&t.id) * if mode == 'C' { 0.7 } else { 0.85 } };
            rm.heard.insert(l.id.clone(), conf);
            if t.foil {
                rm.heat = (rm.heat + 10.0).min(100.0);
            }
            // When it happened goes with it: an old story retold is not news about today.
            learned.push((listener, crate::tr::m("social.heard_from", &[("name", crate::tr::a(&t.name)), ("days_ago", crate::tr::a(&(today - rm.day).max(0))), ("message", crate::tr::a(&rm.text))]), conf));
            if !rm.about.is_empty() {
                later.push((listener, rm.about.clone(), conf));
            }
            if !changed.contains(&k) {
                changed.push(k);
            }
        }
    }
    // Meeting at a food place, one of them treats the other (the more reciprocal one).
    if !place.is_empty() && b(r, "food_place") && !rivals && rng.frand() < 0.3 {
        let ra = profile_int(&a.profile, "money", "reciprocity", 50);
        let rb = profile_int(&bb.profile, "money", "reciprocity", 50);
        let (payer, guest) = if ra >= rb { (&a, &bb) } else { (&bb, &a) };
        fx.observe(&guest.id, "treat", &crate::tr::m("social.drink_treat_from", &[("name", crate::tr::a(&payer.name))]), place, 0.3, "observation");
        fx.observe(&payer.id, "treat", &crate::tr::m("social.i_treat_drink", &[("name", crate::tr::a(&guest.name))]), place, 0.25, "observation");
        heat = bond_heat_add(heat, 2.0);
    }
    for (who, text, conf) in &learned {
        let id = [&a.id, &bb.id][*who].clone();
        fx.observe(&id, "gossip", text, place, 0.45 * conf + 0.1, "observation");
        fx.mind_note(&id, text);
    }
    for (who, about, conf) in later {
        if who == 0 {
            a.hear(&about, conf, &mut fx);
        } else {
            bb.hear(&about, conf, &mut fx);
        }
    }
    if !b(r, "overheard") {
        // The place by its name in the player's language (the request carries the id: "girls_dorm").
        let named = crate::tr::lookup(&format!("ui.name.place.{place}"), "");
        let at = if place.is_empty() { crate::tr::t("social.on_street").to_string() } else if named.is_empty() { place.to_string() } else { named.to_string() };
        fx.observe(&a.id, "encounter", &crate::tr::m("social.met_and_chatted", &[("place", crate::tr::a(&at)), ("name", crate::tr::a(&bb.name))]), place, 0.2, "observation");
        fx.observe(&bb.id, "encounter", &crate::tr::m("social.met_and_chatted", &[("place", crate::tr::a(&at)), ("name", crate::tr::a(&a.name))]), place, 0.2, "observation");
    }
    let rumors: Vec<&Rumor> = changed.iter().map(|k| &rumors[*k]).collect();
    Ok(json!({"heat": heat, "rumors": rumors, "learned": learned.len(), "effects": fx.into_value()}))
}

/// Every ten game minutes: pairs in the same place (or within 4 m on the street) may meet, once a pair a day (60%);
/// in mode C a jealous heroine near him may catch him with another one. Request: seed, with (id or ""), pairs
/// [{a, b, a_name, b_name, a_mode, b_mode, a_busy, b_busy, a_sees, b_sees, together, near, met_today}].
fn encounters_tick(r: &Value) -> Result<Value, String> {
    let mut rng = RandomStream::new(n(r, "seed") as i32);
    let with = s(r, "with");
    let mut fx = Effects::default();
    let mut caught = Vec::new();
    let mut meet = Vec::new();
    for p in r.get("pairs").and_then(Value::as_array).into_iter().flatten() {
        if b(p, "met_today") {
            continue;
        }
        let mut met = false;
        for (seer, other) in [("a", "b"), ("b", "a")] {
            let (seer_id, other_id) = (s(p, seer), s(p, other));
            if with == other_id && !b(p, &format!("{seer}_busy")) && s(p, &format!("{seer}_mode")) == "C" && b(p, &format!("{seer}_sees")) {
                let other_name = s(p, &format!("{other}_name"));
                fx.recent(seer_id, &crate::tr::m("social.saw_him_with", &[("other_name", crate::tr::a(&other_name))]));
                fx.intent(seer_id, "seek_player");
                fx.observe(seer_id, "betrayal", &crate::tr::m("social.caught_with", &[("other_name", crate::tr::a(&other_name))]), s(r, "place"), 0.75, "observation");
                caught.push(json!({"seer": seer_id, "other": other_id}));
                met = true;
            }
        }
        if met || b(p, "a_busy") || b(p, "b_busy") || !b(p, "together") {
            if met {
                meet.push(json!({"a": p["a"], "b": p["b"], "met": false}));
            }
            continue;
        }
        if rng.frand() >= 0.6 {
            continue;
        }
        meet.push(json!({"a": p["a"], "b": p["b"], "met": true, "overheard": b(p, "near")}));
    }
    Ok(json!({"caught": caught, "meet": meet, "effects": fx.into_value()}))
}

// ------------------------------------------------------------------------------------------------ appointments

/// How long she waits for him: dependent and grateful types longer, proud ones shorter.
pub fn patience(p: &Value) -> i32 {
    match profile_str(p, "archetype") {
        "D" | "E" => 30,
        "H" => 10,
        _ => 20,
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct Appointment {
    id: i32,
    kind: String,
    heroine: String,
    place: String,
    label: String,
    start: i64,
    grace: i32,
    status: String,
    /// Facts: he is there now (inside, or within 10 m of the door); an entrust reminder that is settled.
    #[serde(skip_serializing)]
    there: bool,
    #[serde(skip_serializing)]
    entrust_done: bool,
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct Her {
    relation: Option<Relation>,
    profile: Value,
}

/// Every game minute. Request: now, day, talking_to / approaching / with (ids or ""), appointments, heroines {id: {relation,
/// profile}}. Answer: updates [{id, status}], remove [id], arrive [{id, heroine, late}], blacklist [{place, until}],
/// fired [{place}] (a shift not turned up for), notice,
/// effects.
/// Minutes a heroine keeps waiting past the grace before it is a no-show.
const DATE_WAIT: i64 = 60;
/// Favor a no-show costs, and the most being late costs (1 per 3 minutes late).
const STOOD_UP_FAVOR: f32 = 20.0;
const LATE_FAVOR_PER_MINUTE: f32 = 1.0 / 3.0;
/// Minutes past the grace a shift still takes him (late: paid from when he came); later it is a no-show.
const SHIFT_LATE_WAIT: i64 = 60;

fn appointments_tick(r: &Value) -> Result<Value, String> {
    let now = n(r, "now") as i64;
    let day = n(r, "day") as i32;
    let (talking, approaching, with) = (s(r, "talking_to"), s(r, "approaching"), s(r, "with"));
    // The heroine he walked there with (an outing to the place, or she was following him): they came together.
    let came_with = s(r, "came_with");
    let mut list: Vec<Appointment> = serde_json::from_value(r.get("appointments").cloned().unwrap_or(json!([]))).map_err(|e| format!("appointments: {e}"))?;
    let mut hers: BTreeMap<String, Her> = serde_json::from_value(r.get("heroines").cloned().unwrap_or(json!({}))).map_err(|e| format!("heroines: {e}"))?;
    let mut fx = Effects::default();
    let (mut updates, mut arrive, mut blacklist, mut fired) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
    let mut notice = String::new();
    for k in 0..list.len() {
        if list[k].status != "pending" {
            continue;
        }
        let a = list[k].clone();
        if a.kind == "entrust" {
            if a.entrust_done {
                list[k].status = "done".into();
                updates.push(json!({"id": a.id, "status": "done"}));
            }
            continue;
        }
        let girl = !a.heroine.is_empty() && hers.contains_key(&a.heroine);
        // A heroine waits for him: up to an hour past the grace he is late (favor by the minutes, at most what a no-show
        // costs), not a no-show —
        // also when he is there but held up by someone else walking up to him or talking with him.
        let busy = girl && talking != a.heroine && ((!approaching.is_empty() && approaching != a.heroine) || !talking.is_empty());
        let wait = if girl { DATE_WAIT } else if a.kind == "shift" { SHIFT_LATE_WAIT } else { 0 };
        if a.there && now >= a.start - 30 && now <= a.start + a.grace as i64 + wait {
            if busy {
                continue;
            }
            // Arriving together is not him keeping her waiting, however late the two of them are.
            let together = girl && !came_with.is_empty() && came_with == a.heroine;
            let late = now > a.start && !together;
            list[k].status = if late { "late" } else { "done" }.into();
            updates.push(json!({"id": a.id, "status": list[k].status}));
            if girl {
                if let Some(rel) = hers.get_mut(&a.heroine).and_then(|h| h.relation.as_mut()) {
                    if late {
                        let before = rel.clone();
                        rel.favor = (rel.favor - ((now - a.start) as f32 * LATE_FAVOR_PER_MINUTE).min(STOOD_UP_FAVOR)).max(0.0);
                        fx.relation(rel, &before);
                    }
                }
                fx.note_at(&a.heroine, "date", &if late { crate::tr::m("social.late_minutes", &[("minutes", crate::tr::a(&(now - a.start)))]) } else if together { crate::tr::t("social.came_together").into() } else { crate::tr::t("social.on_time").into() }, &a.place, if late { 0.5 } else { 0.6 });
                arrive.push(json!({"id": a.id, "heroine": a.heroine, "late": late}));
            }
            notice = format!("{}{}", if late { crate::tr::t("social.late_prefix") } else { crate::tr::t("social.arrived_on_time_prefix") }, a.label);
            continue;
        }
        if now <= a.start + a.grace as i64 + wait {
            continue;
        }
        // Stood up.
        list[k].status = "missed".into();
        updates.push(json!({"id": a.id, "status": "missed"}));
        if girl {
            let previous = list.iter().filter(|x| x.heroine == a.heroine && x.status == "missed").count() as i32;
            let her = hers.get_mut(&a.heroine).unwrap();
            let t = Traits::from_profile(&her.profile);
            if let Some(rel) = her.relation.as_mut() {
                let before = rel.clone();
                let scale = 1.5f32.powf((previous - 1).max(0) as f32);
                // Worship softens it, insecurity doubles it.
                let m = if rel.admiration > 60.0 { 0.5 } else { 1.0 } * if t.insecurity > 60 { 2.0 } else { 1.0 } * scale;
                rel.favor = (rel.favor - STOOD_UP_FAVOR).max(0.0);
                rel.trust = (rel.trust - 10.0 * m).max(0.0);
                rel.resentment = (rel.resentment + 10.0 * m).min(100.0);
                // He was with another heroine at the time: jealousy toward her.
                if !with.is_empty() && with != a.heroine {
                    let j = rel.jealousy_toward.entry(with.to_string()).or_insert(0.0);
                    *j = (*j + 20.0).min(100.0);
                    fx.relation(rel, &before);
                    fx.bond_event(&a.heroine, with, "stood_up");
                } else {
                    fx.relation(rel, &before);
                }
            }
            fx.note_at(&a.heroine, "stood_up", &crate::tr::m("social.stood_up_reason", &[("label", crate::tr::a(&a.label))]), &a.place, 0.85);
            fx.text(&a.heroine, &crate::tr::m("social.no_show_waited", &[("label", crate::tr::a(&a.label))]), "stood_up", 0.0);
        }
        if a.kind == "shift" {
            // Not turning up for a shift costs him the job, not the place: he can still stay at the hotel he worked at.
            fired.push(json!({"place": a.place}));
            notice = crate::tr::m("social.no_show_fired", &[("label", crate::tr::a(&a.label))]);
        } else if a.kind == "reservation" || a.kind == "pickup" {
            blacklist.push(json!({"place": a.place, "until": day + 7}));
            notice = crate::tr::m("social.no_show_blacklist", &[("label", crate::tr::a(&a.label))]);
        }
    }
    // Old finished entries are dropped after two days.
    let remove: Vec<i32> = list.iter().filter(|a| a.status != "pending" && now - a.start > 2 * 1440).map(|a| a.id).collect();
    Ok(json!({"updates": updates, "remove": remove, "arrive": arrive, "blacklist": blacklist, "fired": fired, "notice": notice, "effects": fx.into_value()}))
}

/// Cancelling early costs little; within two hours it counts as half a no-show. Request: id, relation, minutes_left.
fn appointment_cancel(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?;
    let before = rel.clone();
    let late = n(r, "minutes_left") < 120.0;
    rel.add_liking(if late { -3.0 } else { -1.0 });
    if late {
        rel.trust = (rel.trust - 2.0).max(0.0);
    }
    let mut fx = Effects::default();
    fx.relation(&rel, &before);
    fx.note_at(&rel.id, "promise", if late { crate::tr::t("social.late_cancel") } else { crate::tr::t("social.early_reschedule") }, "", if late { 0.5 } else { 0.3 });
    Ok(json!({"effects": fx.into_value()}))
}

// ------------------------------------------------------------------------------------------------ acting first

/// Three times a day (07-12, 12-18, 18-) each heroine weighs acting first; the strongest reason (40+) wins, one heroine a
/// slot. Request: minute, day, last_slot, rain, holiday, names {id: name}, heroines [{actor, id, relation, profile, busy,
/// near (within 30 m of him), birthday_tomorrow}]. Answer: slot (key), and when one acts: actor, id, score, why, intent.
/// He named the girl she is jealous of himself ("yes, I'm going out with 三月七"): she has heard it from him, so that
/// news no longer sends her to have it out with him (initiative's confront_j, at least the 61 it takes), until new
/// news raises her jealousy again. 胡桃 was told face to face and still came an hour later "to ask about the rumour".
/// Request: relation, said (his line), names {id: name}. Answer: effects (the relation's flag), named.
fn rival_named(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?;
    let before = rel.clone();
    let said = s(r, "said");
    let (j, rival) = bias::peak_jealousy(&rel);
    let names = r.get("names").cloned().unwrap_or(json!({}));
    let who = names.get(&rival).and_then(Value::as_str).unwrap_or("");
    let mut fx = Effects::default();
    let named = !rival.is_empty() && !who.is_empty() && said.contains(who);
    if named {
        let mark = j.max(61.0).round();
        let had = rel.flags.iter().find_map(|f| f.strip_prefix("confront_j:").and_then(|v| v.parse::<f32>().ok())).unwrap_or(0.0);
        if mark > had {
            rel.flags.retain(|f| !f.starts_with("confront_j:"));
            rel.flags.push(format!("confront_j:{mark}"));
            fx.relation(&rel, &before);
        }
    }
    Ok(json!({"effects": fx.into_value(), "named": named}))
}

fn initiative(r: &Value) -> Result<Value, String> {
    let minute = n(r, "minute") as i32;
    let hour = minute / 60;
    let slot = if hour >= 18 { 3 } else if hour >= 12 { 2 } else if hour >= 7 { 1 } else { 0 };
    let key = n(r, "day") as i32 * 4 + slot;
    if slot == 0 || key == n(r, "last_slot") as i32 {
        return Ok(json!({"slot": n(r, "last_slot")}));
    }
    let rain = b(r, "rain");
    let holiday = s(r, "holiday");
    let names = r.get("names").cloned().unwrap_or(json!({}));
    let mut best: Option<(f32, i32, String, String, String)> = None;
    // The heroine whose confrontation won, with her jealousy then: she has had it out with him once; only new news
    // (jealousy 8 higher) brings her back for it (every slot for days: "you heard about Arthuria, ask him" again and again).
    let mut confront: Option<(Relation, Relation)> = None;
    for h in r.get("heroines").and_then(Value::as_array).into_iter().flatten() {
        if b(h, "busy") {
            continue;
        }
        let rel = rel_of(h.get("relation"))?;
        let p = profile(h.get("profile"));
        let t = Traits::from_profile(&p);
        let (stress, _) = crate::mind::money_stress(&rel, &p);
        let (j, rival) = bias::peak_jealousy(&rel);
        let mut reasons: Vec<(f32, String, &str)> = Vec::new();
        if rel.satiety < 45.0 && rel.liking() > 40.0 {
            reasons.push((40.0 + rel.liking() * 0.3, crate::tr::t("social.invite_dinner").into(), "seek_player"));
        }
        // He told her his wallet is empty: nobody comes to borrow from him then (missing: unknown, so she may ask).
        let his_cash = r.get("player_cash").and_then(Value::as_f64).unwrap_or(f64::MAX);
        if stress > 70.0 && rel.trust >= 50.0 && profile_int(&p, "money", "treat_pride", 50) < 50 && his_cash >= crate::region::amount("cash", 100) as f64 {
            reasons.push((55.0 + stress * 0.2, crate::tr::t("social.ask_loan_tight").into(), "ask_help"));
        }
        if rel.treat_balance >= 2 && profile_int(&p, "money", "reciprocity", 50) >= 60 {
            reasons.push((45.0, crate::tr::t("social.return_treat").into(), "seek_player"));
        }
        let confronted = rel.flags.iter().find_map(|f| f.strip_prefix("confront_j:").and_then(|v| v.parse::<f32>().ok())).unwrap_or(0.0);
        let confront_why = (j >= 61.0 && !rival.is_empty() && j >= confronted + 8.0).then(|| {
            let who = names.get(&rival).and_then(Value::as_str).unwrap_or(&rival);
            crate::tr::m("social.hear_rumor_confront", &[("who", crate::tr::a(&who))])
        });
        if let Some(why) = &confront_why {
            reasons.push((50.0 + j * 0.3, why.clone(), "seek_player"));
        }
        if bias::admiration_stage(&rel, 0) >= 3 {
            reasons.push((40.0 + rel.admiration * 0.2, crate::tr::t("social.ask_his_advice").into(), "seek_player"));
        }
        if rain && rel.liking() > 35.0 && b(h, "near") {
            reasons.push((60.0, crate::tr::t("social.rain_share_umbrella").into(), "seek_player"));
        }
        if b(h, "birthday_tomorrow") && rel.liking() > 30.0 {
            reasons.push((45.0, crate::tr::t("social.tomorrow_birthday_hint").into(), "seek_player"));
        }
        if !holiday.is_empty() && !holiday.contains('考') && rel.liking() > 45.0 {
            reasons.push((50.0, crate::tr::m("social.invite_holiday", &[("holiday", crate::tr::a(&holiday))]), "seek_player"));
        }
        if profile_str(&p, "orientation") == "Recruiter" && rel.liking() > 60.0 {
            reasons.push((45.0, crate::tr::t("social.introduce_friend").into(), "seek_player"));
        }
        if rel.social < 30.0 && rel.liking() > 30.0 {
            reasons.push((35.0 + (100.0 - rel.social) * 0.2, crate::tr::t("social.lonely_chat").into(), "seek_player"));
        }
        for (score, why, intent) in reasons {
            if score * (0.8 + t.possessiveness as f32 / 500.0) > best.as_ref().map(|x| x.0).unwrap_or(0.0) {
                confront = (confront_why.as_ref() == Some(&why)).then(|| {
                    let mut after = rel.clone();
                    after.flags.retain(|f| !f.starts_with("confront_j:"));
                    after.flags.push(format!("confront_j:{}", j.round()));
                    (after, rel.clone())
                });
                best = Some((score, n(h, "actor") as i32, s(h, "id").to_string(), why, intent.to_string()));
            }
        }
    }
    let mut out = json!({"slot": key});
    if let Some((score, actor, id, why, intent)) = best.filter(|x| x.0 >= 40.0) {
        let mut fx = Effects::default();
        if let Some((after, before)) = &confront {
            fx.relation(after, before);
        }
        fx.recent(&id, &why);
        fx.intent(&id, &intent);
        fx.observe(&id, "initiative", &why, "", 0.3, "observation");
        out = json!({"slot": key, "actor": actor, "id": id, "score": score, "why": why, "intent": intent, "effects": fx.into_value()});
    }
    Ok(out)
}

/// Once a day the Director lets a hidden money need (severity 0.7+, secret) be heard about: her closest friend knows.
/// Request: day, heroines [{id, name, profile}], rumors [{about, kind}], bond_heat [[key, heat]]. Answer: rumors to add.
fn director(r: &Value) -> Result<Value, String> {
    let day = n(r, "day") as i32;
    let known: Vec<(String, String)> = r.get("rumors").and_then(Value::as_array).into_iter().flatten().map(|x| (s(x, "about").to_string(), s(x, "kind").to_string())).collect();
    let bonds: Vec<(String, f32)> = serde_json::from_value(r.get("bond_heat").cloned().unwrap_or(json!([]))).unwrap_or_default();
    let mut out: Vec<Rumor> = Vec::new();
    for h in r.get("heroines").and_then(Value::as_array).into_iter().flatten() {
        let id = s(h, "id");
        for need in h.get("profile").and_then(|p| p.get("money")).and_then(|m| m.get("money_needs")).and_then(Value::as_array).into_iter().flatten() {
            if n(need, "severity") < 0.7 || !b(need, "secret") {
                continue;
            }
            if known.iter().any(|(a, k)| a == id && k == "need") || out.iter().any(|x| x.about == id) {
                continue;
            }
            let mut friend = String::new();
            let mut heat = -1.0;
            for (key, v) in &bonds {
                if key.contains(id) && *v > heat {
                    heat = *v;
                    if let Some((l, rt)) = key.split_once('|') {
                        friend = if l == id { rt.into() } else { l.into() };
                    }
                }
            }
            let reason: String = s(need, "reason").chars().take(20).collect();
            out.push(Rumor { kind: "need".into(), about: id.into(), heat: 60.0, confidence: 1.0, day, text: crate::tr::m("social.heard_she_broke", &[("name", crate::tr::a(&(s(h, "name")))), ("reason", crate::tr::a(&reason))]), known_by: if friend.is_empty() { vec![] } else { vec![friend] }, ..Default::default() });
        }
    }
    Ok(json!({"rumors": out}))
}

// ------------------------------------------------------------------------------------------------ bias actions

/// Every 30 s: seeing him with another heroine nearby, she may come over (surveil / compete); walking with him with
/// another heroine close, she may take his hand (possessive display). Request: seed, with_name, near [{actor, surveil,
/// compete}], walker {actor, possessive, rival_near} or null. Answer: come [{actor, why}], hold_hands (actor or -1).
fn bias_actions(r: &Value) -> Result<Value, String> {
    let mut rng = RandomStream::new(n(r, "seed") as i32);
    let other = s(r, "with_name");
    let mut come = Vec::new();
    for h in r.get("near").and_then(Value::as_array).into_iter().flatten() {
        let (surveil, compete) = (n(h, "surveil") as f32, n(h, "compete") as f32);
        let pull = surveil.max(compete);
        if pull < 0.3 || rng.frand() >= pull * 0.3 {
            continue;
        }
        let why = if compete >= surveil { crate::tr::m("social.see_him_pull_back", &[("other", crate::tr::a(&other))]) } else { crate::tr::m("social.see_him_go_check", &[("other", crate::tr::a(&other))]) };
        come.push(json!({"actor": h["actor"], "why": why}));
    }
    let mut hold = -1;
    if let Some(w) = r.get("walker").filter(|w| w.is_object()) {
        let p = n(w, "possessive") as f32;
        if b(w, "rival_near") && p >= 0.4 && rng.frand() < p * 0.5 {
            hold = n(w, "actor") as i32;
        }
    }
    Ok(json!({"come": come, "hold_hands": hold}))
}

/// A refusal she would give may still turn into a yes ("if you really want to..."). Request: defer, seed, why.
fn bias_defer(r: &Value) -> Result<Value, String> {
    let defer = n(r, "defer") as f32;
    let yes = defer > 0.0 && RandomStream::new(n(r, "seed") as i32).frand() < defer * 0.5;
    Ok(json!({"yes": yes, "recent": if yes { crate::tr::m("social.reluctant_agreed", &[("why", crate::tr::a(&(s(r, "why"))))]) } else { String::new() }}))
}

/// Alone with him, a lover who trusts him lets the act drop.
pub fn mask_broken(r: &Relation, public: bool) -> bool {
    !public && r.stage == "lover" && r.trust >= 70.0
}

/// Her own messages: at most three a day, half an hour apart. Request: now, log [minutes]. Answer: allowed, log.
fn self_text(r: &Value) -> Result<Value, String> {
    let now = n(r, "now") as i64;
    let mut log: Vec<i64> = r.get("log").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_i64).filter(|m| m / 1440 == now / 1440).collect();
    let allowed = log.len() < 3 && log.last().map(|l| now - l >= 30).unwrap_or(true);
    if allowed {
        log.push(now);
    }
    Ok(json!({"allowed": allowed, "log": log}))
}

/// Whether she picks up: a lover nearly always, a friend sometimes, someone who barely knows him rarely; less in class,
/// at work or asleep, and not when she resents him. Request: relation, block (her plan block kind or "").
pub fn phone_answer(r: &Relation, block: &str) -> f32 {
    let mut p = match r.stage.as_str() {
        "lover" => 0.9,
        "friend" => 0.3 + (r.liking() - 40.0).max(0.0) / 200.0,
        _ => 0.08 + (r.liking() - 50.0).max(0.0) / 400.0,
    };
    if block == "class" || block == "work" {
        p *= 0.35;
    }
    if block == "sleep" {
        p *= 0.15;
    }
    if r.resentment > 60.0 {
        p *= 0.3;
    }
    p.clamp(0.0, 0.97)
}

/// End of her birthday: a day with him counts; forgotten costs 5 affection (when she cared, 30+).
fn birthday_end(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?;
    let before = rel.clone();
    let mut fx = Effects::default();
    if b(r, "together") {
        fx.observe(&rel.id, "birthday", crate::tr::t("social.birthday_spent_together"), "", 0.8, "observation");
    } else if rel.liking() > 30.0 {
        rel.add_liking(-5.0);
        fx.relation(&rel, &before);
        fx.observe(&rel.id, "birthday", crate::tr::t("social.he_forgot_birthday"), "", 0.75, "observation");
    }
    Ok(json!({"effects": fx.into_value()}))
}

/// Words that ask someone over: "Alter，妳也過來一起聊吧", "叫九十九一起來".

/// He calls a girl nearby by name and asks her over: she is invited, whatever the girl he is talking to thinks of it
/// (her reply is her feelings; the invitation is his). Names match by any part of two or more letters ("Alter" for
/// "Alter Arthuria", "狂死香" for "凶鳥 狂死香") or a given alias. Request: said, nearby [{name, aliases []}]. Answer:
/// invite [name].
/// The ways a girl can be called: her name's words, aliases, and for a Chinese name of three letters or more its given
/// and family parts (日代千鶴: 千鶴, 日代), all in one script.
fn called_by(girl: &Value) -> Vec<String> {
    let one = |t: &str| zhconv::zhconv(&t.to_lowercase(), zhconv::Variant::ZhCN);
    let name = s(girl, "name");
    let aliases = girl.get("aliases").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str);
    let parts: Vec<String> = name.split_whitespace().chain(aliases).chain(std::iter::once(name)).map(|p| one(p)).collect();
    let mut called_by: Vec<String> = parts.clone();
    for p in &parts {
        let chars: Vec<char> = p.chars().collect();
        if chars.len() >= 3 && !chars.iter().any(char::is_ascii) {
            for n in 2..chars.len() {
                called_by.push(chars[chars.len() - n..].iter().collect());
                called_by.push(chars[..n].iter().collect());
            }
        }
    }
    called_by.retain(|p| p.chars().count() >= 2);
    called_by
}

/// He addresses the girl he is talking to by another girl's name ("爱莉希雅，你也在这里吃饭啊" to Arthuria): the name
/// another girl is called by, at the start of his line as a call (in the first clause, little more than the name, or
/// right after a greeting), and not her own. Request: said, her {name, aliases}, others [{name, aliases}].
/// Answer: called (the other girl's name, "" when none).
fn misnamed(r: &Value) -> Result<Value, String> {
    let one = |t: &str| zhconv::zhconv(&t.to_lowercase(), zhconv::Variant::ZhCN);
    let said = one(s(r, "said"));
    let mine = r.get("her").map(called_by).unwrap_or_default();
    let clauses: Vec<&str> = said.split(['。', '！', '？', '!', '?', '，', ',', '；', ';', '…', '\n', '、']).map(str::trim).filter(|c| !c.is_empty()).collect();
    // The call: the first clause, or the second after a short greeting ("嗨，爱莉希雅").
    let calls: Vec<&str> = clauses.iter().take(2).enumerate().filter(|(i, _c)| *i == 0 || clauses[0].chars().count() <= 3).map(|(_, c)| *c).collect();
    for girl in r.get("others").and_then(Value::as_array).into_iter().flatten() {
        let theirs = called_by(girl);
        // A call is little more than the name ("爱莉希雅", "爱莉希雅呀", "嗨，过子"): "千鹤是朋友" talks about her.
        // "跟千鹤。" (with whom? with 千鹤) names her after a word that makes her someone talked about, not a call.
        let about = |c: &str| crate::tr::t("hear.name_about").split('|').map(str::trim).filter(|w| !w.is_empty()).any(|w| c.starts_with(w));
        let hit = calls.iter().any(|c| !about(c) && theirs.iter().any(|n| c.contains(n.as_str()) && c.chars().count() <= n.chars().count() + 1 && !mine.iter().any(|m| c.contains(m.as_str()))));
        if hit {
            return Ok(json!({"called": s(girl, "name")}));
        }
    }
    Ok(json!({"called": ""}))
}

/// She broke up with him in her own words just now. Request: relation. Answer: effects (her stage back to friend, or
/// stranger when her resentment is past 90; favor from zero; the breakup remembered).
fn breakup_said(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?;
    let mut fx = Effects::default();
    if rel.stage != "lover" {
        return Ok(json!({"effects": fx.into_value()}));
    }
    let before = rel.clone();
    rel.stage = if rel.resentment >= 90.0 { "stranger" } else { "friend" }.into();
    rel.favor = 0.0;
    fx.relation(&rel, &before);
    fx.note(&rel.id, "breakup", &crate::tr::m("day.breakup_reason_short", &[("reason", crate::tr::a(crate::tr::t("social.breakup_said_reason")))]), 0.95, true);
    fx.log(format!("Street stage: {} breaks up in her own words -> {}", rel.id, rel.stage));
    Ok(json!({"effects": fx.into_value()}))
}

/// She proposes something in her reply ("要不要一起喝杯咖啡？") that he has not asked for or agreed to in his line: it
/// waits for his answer instead of starting now. Request: his, her. Answer: open (true: wait for his yes).
fn her_proposal_open(r: &Value) -> Result<Value, String> {
    let one = |t: &str| zhconv::zhconv(&t.to_lowercase(), zhconv::Variant::ZhCN);
    let (his, her) = (one(s(r, "his")), one(s(r, "her")));
    let asks = crate::tr::heard(&her, "hear.propose");
    // His own offer counts as wanting it: "我送你回宿舍吧" is him proposing the walk (and "回宿舍" in it is not a goodbye).
    let he_wants = crate::tr::heard(&his, "hear.invite") || crate::tr::heard(&his, "hear.agree") || crate::tr::heard(&his, "hear.he_offer") || crate::tr::heard(&his, "hear.propose");
    // He was saying goodbye ("go to work, see you tomorrow") and she answers "walk with me first": whatever she
    // starts now is her idea; it waits for his yes (it set off for the park while he was leaving).
    let he_leaves = crate::tr::heard(&his, "hear.goodbye");
    Ok(json!({"open": (asks || he_leaves) && !he_wants}))
}

fn invite_named(r: &Value) -> Result<Value, String> {
    // Either script on either side (he types Simplified, her card name is Traditional): compared in one.
    let one = |t: &str| zhconv::zhconv(&t.to_lowercase(), zhconv::Variant::ZhCN);
    let said = one(s(r, "said"));
    if !crate::tr::heard(&said, "hear.invite") || crate::tr::heard(&said, "hear.refuse") {
        return Ok(json!({"invite": []}));
    }
    let mut invite = Vec::new();
    for girl in r.get("nearby").and_then(Value::as_array).into_iter().flatten() {
        let name = s(girl, "name");
        let aliases = girl.get("aliases").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str);
        let parts: Vec<String> = name.split_whitespace().chain(aliases).chain(std::iter::once(name)).map(|p| one(p)).collect();
        // A Chinese name without spaces is also called by its given or family part (日代千鶴: 千鶴, 日代).
        let mut called_by: Vec<String> = parts.clone();
        for p in &parts {
            let chars: Vec<char> = p.chars().collect();
            if chars.len() >= 3 && !chars.iter().any(char::is_ascii) {
                for n in 2..chars.len() {
                    called_by.push(chars[chars.len() - n..].iter().collect());
                    called_by.push(chars[..n].iter().collect());
                }
            }
        }
        // Called over: her name and the invitation in the same clause ("千鶴，妳也過來"), not just named somewhere in a
        // line that also asks someone else along ("我是來向日代道歉的……想跟妳一起走").
        // Or called just before it ("Alter，妳也過來"): the clause before is little more than her name.
        let clauses: Vec<&str> = said.split(['。', '！', '？', '!', '?', '，', ',', '；', ';', '…', '\n']).collect();
        let names = |clause: &str| called_by.iter().any(|part| part.chars().count() >= 2 && clause.contains(part.as_str()));
        let vocative = |clause: &str| {
            let c = clause.trim();
            called_by.iter().any(|part| part.chars().count() >= 2 && c.contains(part.as_str()) && c.chars().count() <= part.chars().count() + 2)
        };
        let called = clauses.iter().enumerate().any(|(i, clause)| {
            // Her name in the clause with only a soft "一起" is talk about her ("千鹤那边是约好一起看书"), not a call:
            // a call says come ("过来", "也来") or speaks to her ("你也……").
            let call = crate::tr::heard(clause, "hear.invite_strong") || crate::tr::heard(clause, "hear.you_words");
            crate::tr::heard(clause, "hear.invite") && ((names(clause) && call) || (i > 0 && vocative(clauses[i - 1])))
        });
        if called {
            invite.push(name.to_string());
        }
    }
    Ok(json!({"invite": invite}))
}

#[cfg(test)]
mod invite_tests {
    use serde_json::json;
    #[test]
    fn she_breaks_up_in_her_own_words() {
        let v = super::breakup_said(&json!({"relation": {"id": "a", "stage": "lover", "favor": 80, "resentment": 40}})).unwrap();
        let fx = v["effects"].as_array().unwrap();
        assert!(fx.iter().any(|e| e["op"] == "relation" && e["patch"]["stage"] == "friend") || fx.iter().any(|e| e["op"] == "relation" && e.to_string().contains("friend")), "{v}");
        assert!(fx.iter().any(|e| e["op"] == "note" && e["act"] == "breakup"));
        let none = super::breakup_said(&json!({"relation": {"id": "a", "stage": "friend"}})).unwrap();
        assert!(none["effects"].as_array().unwrap().is_empty(), "not his girlfriend: nothing to end");
    }

    #[test]
    fn her_proposal_waits_for_him() {
        let o = |his: &str, her: &str| super::her_proposal_open(&json!({"his": his, "her": her})).unwrap()["open"].as_bool().unwrap();
        assert!(o("漫畫看到哪裡了？", "剛看完第三卷。要不要一起喝杯咖啡，坐下來聊？"));
        assert!(!o("要不要一起去喝咖啡？", "好啊，走吧。"));
        assert!(!o("好啊", "要不要去公園走走？"));
        assert!(o("吃饱了。你去打工吧，加油，明早七点见。", "不急着走，先陪我走走。中央公园就在北边。"), "he was leaving");
        assert!(o("那我走了", "等等，陪我去便利店。"));
        assert!(!o("好，下次你请。雨小了，我送你回宿舍吧。", "送到楼下就行。别上楼，规矩你懂。走吧，趁雨小。"), "he offered to walk her home");
    }

    #[test]
    fn whole_name_before_a_longer_one() {
        let places = vec![json!({"id": "cafeteria", "name": "學生餐廳", "dist": 100.0}), json!({"id": "restaurant", "name": "餐廳", "dist": 900.0})];
        assert_eq!(super::named_place_id("餐廳就在旁邊，走吧", &places).as_deref(), Some("restaurant"));
        assert_eq!(super::named_place_id("去學生餐廳吧", &places).as_deref(), Some("cafeteria"));
        let more = vec![json!({"id": "cafeteria", "name": "學生餐廳", "dist": 100.0}), json!({"id": "park", "name": "中央公園", "dist": 900.0})];
        assert_eq!(super::named_place_id("坐窗邊那桌。昨天的公園，你自己說。", &more), None, "yesterday's park is not where they go");
        assert_eq!(super::named_place_id("昨天去過公園了，今天去學生餐廳吧", &more).as_deref(), Some("cafeteria"));
    }

    #[test]
    fn misnamed_calls() {
        let r = |said: &str| super::misnamed(&json!({"said": said, "her": {"name": "Alter Arthuria", "aliases": ["Alter", "亞爾托莉雅"]}, "others": [{"name": "fin 愛莉希雅1.1", "aliases": ["愛莉希雅"]}, {"name": "九十九 過子", "aliases": ["九十九過子"]}]})).unwrap()["called"].as_str().unwrap().to_string();
        assert_eq!(r("爱莉希雅，你也在这里吃饭啊。"), "fin 愛莉希雅1.1");
        assert_eq!(r("嗨，过子！今天好吗"), "九十九 過子");
        assert_eq!(r("亚尔托莉雅，你好"), "");
        assert_eq!(r("我刚跟过子吃完饭，你呢？"), "", "talking about her, not calling");
        assert_eq!(r("过子是朋友，我们约好一起看书的。"), "", "her name as the subject is not a call");
        assert_eq!(r("跟过子。我不想对你说谎。"), "", "answering with whom is not a call");
        assert_eq!(r("是爱莉希雅，今晚本来就约好的。"), "", "answering who it was is not a call");
        assert_eq!(r("你好"), "");
    }

    #[test]
    fn place_named_prefers_her_home() {
        let places = vec![json!({"id": "boys_dorm", "name": "校外男生宿舍", "dist": 100.0}), json!({"id": "girls_dorm", "name": "校外女生宿舍", "dist": 900.0})];
        assert_eq!(super::named_place_id("我送你回宿舍", &places).as_deref(), Some("boys_dorm"), "a tie: the nearer");
        assert_eq!(super::named_place_id_preferring("我送你回宿舍", &places, &["girls_dorm".to_string()]).as_deref(), Some("girls_dorm"), "her home wins the tie");
        assert_eq!(super::named_place_id_preferring("去男生宿舍拿东西", &places, &["girls_dorm".to_string()]).as_deref(), Some("boys_dorm"), "named in full beats the preference");
    }

    #[test]
    fn named_in_another_clause_is_not_called() {
        let nearby = json!([{"name": "Hishiro Chizuru", "aliases": ["日代", "千鶴"]}]);
        let r = super::invite_named(&json!({"said": "過子，等等。我是來向日代道歉的。想跟妳一起走回宿舍", "nearby": nearby})).unwrap();
        assert_eq!(r["invite"].as_array().unwrap().len(), 0);
        let r = super::invite_named(&json!({"said": "日代，妳也過來一起聊吧", "nearby": nearby})).unwrap();
        assert_eq!(r["invite"].as_array().unwrap().len(), 1);
        let r2 = super::invite_named(&json!({"said": "没有啦，千鹤那边是约好一起看书。我不想瞒你", "nearby": nearby})).unwrap();
        assert_eq!(r2["invite"], json!([]), "talking about her is not calling her over");
        let r = super::invite_named(&json!({"said": "千鹤，你也过来一起聊吧", "nearby": nearby})).unwrap();
        assert_eq!(r["invite"].as_array().unwrap().len(), 1, "Simplified words, Traditional alias");
    }
}

/// The place he names in what he said ("去公園散步" -> 中央公園): the whole name (without its note in brackets), else the
/// end of it (two letters or more: 公園, 咖啡廳), the nearest when several fit. Request: said, places [{id, name, dist}].
/// Answer: id ("" when none).
fn place_named(r: &Value) -> Result<Value, String> {
    let places: Vec<Value> = r.get("places").and_then(Value::as_array).cloned().unwrap_or_default();
    let prefer: Vec<String> = r.get("prefer").and_then(Value::as_array).into_iter().flatten().filter_map(|v| v.as_str().map(str::to_string)).collect();
    Ok(json!({"id": named_place_id_preferring(s(r, "said"), &places, &prefer).unwrap_or_default()}))
}

/// The id of the place named in what he said, among these places ({id, name, dist}); see place_named.
pub fn named_place_id(said: &str, places: &[Value]) -> Option<String> {
    named_place_id_preferring(said, places, &[])
}

/// As named_place_id; between names matched equally well ("回宿舍": the girls' and the boys' dorm), one of `prefer` (her
/// home when he walks her back) wins before the nearer one.
pub fn named_place_id_preferring(said: &str, places: &[Value], prefer: &[String]) -> Option<String> {
    // (letters matched, the whole name, preferred, distance, id): "餐厅" names the place called just that before the end
    // of a longer name ("学生餐厅"), however near that one is.
    let mut best: Option<(usize, bool, bool, f64, String)> = None;
    // A place in a clause about the past ("昨天的公园，你自己说": what happened there yesterday) is not where they go.
    let past = crate::tr::t("hear.past_ref");
    let said: String = said
        .split_inclusive(['。', '！', '？', '!', '?', '，', ',', '；', ';', '\n'])
        .filter(|c| !past.split('|').map(str::trim).filter(|w| !w.is_empty()).any(|w| c.contains(w)))
        .collect();
    let said = said.as_str();
    for p in places {
        let name = s(p, "name");
        let core: String = name.split(['（', '(']).next().unwrap_or("").chars().filter(|c| !c.is_whitespace()).collect();
        let chars: Vec<char> = core.chars().collect();
        let matched = (2..=chars.len()).rev().map(|n| chars[chars.len() - n..].iter().collect::<String>()).find(|end| said.contains(end.as_str())).map(|end| end.chars().count());
        let Some(m) = matched else { continue };
        let whole = m == chars.len();
        let d = n(p, "dist");
        let id = s(p, "id").to_string();
        let pref = prefer.contains(&id);
        let better = |(bm, bw, bp, bd, _): &(usize, bool, bool, f64, String)| {
            m > *bm || (m == *bm && (whole && !*bw || (whole == *bw && (pref && !*bp || (pref == *bp && d < *bd)))))
        };
        if best.as_ref().map_or(true, better) {
            best = Some((m, whole, pref, d, id));
        }
    }
    best.map(|b| b.4)
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "place_named" => place_named(r),
        "invite_named" => invite_named(r),
        "misnamed" => misnamed(r),
        "her_proposal_open" => her_proposal_open(r),
        // She ends it in her own words ("we're done"): no longer his girlfriend (the nightly dice only knew favor 0).
        "breakup_said" => breakup_said(r),
        // He lends only when he offered it ("借你五百", "拿去用"); "I only have about a thousand" is not an offer.
        "lend_offered" => {
            let one = |t: &str| zhconv::zhconv(&t.to_lowercase(), zhconv::Variant::ZhCN);
            Ok(json!({"offered": crate::tr::heard(&one(s(r, "his")), "hear.lend_offer")}))
        }
        // The talk ends only on a goodbye somebody said ("明天见", "我先走了"); "that's enough practice for today" is not one.
        "farewell_heard" => {
            let one = |t: &str| zhconv::zhconv(&t.to_lowercase(), zhconv::Variant::ZhCN);
            Ok(json!({"heard": crate::tr::heard(&one(s(r, "his")), "hear.goodbye") || crate::tr::heard(&one(s(r, "her")), "hear.goodbye")}))
        }
        "drama_cast" => drama_cast(r),
        "bond_heat_daily" => bond_heat_daily(r),
        "jealousy_news" => jealousy_kind(r),
        "rumor_make" => rumor_make(r),
        "encounter" => encounter(r),
        "encounters_tick" => encounters_tick(r),
        "appointment_patience" => Ok(json!({"minutes": patience(&profile(r.get("profile")))})),
        "appointments_tick" => appointments_tick(r),
        "appointment_cancel" => appointment_cancel(r),
        "initiative" => initiative(r),
        "rival_named" => rival_named(r),
        "director" => director(r),
        "bias_actions" => bias_actions(r),
        "bias_defer" => bias_defer(r),
        "mask_broken" => rel_of(r.get("relation")).map(|x| json!({"broken": mask_broken(&x, b(r, "public"))})),
        "self_text" => self_text(r),
        "phone_answer" => rel_of(r.get("relation")).map(|x| json!({"chance": phone_answer(&x, s(r, "block"))})),
        "birthday_end" => birthday_end(r),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(v: Value) -> Value {
        let mut o = json!({"id": "a"});
        for (k, x) in v.as_object().unwrap() {
            o[k] = x.clone();
        }
        o
    }

    #[test]
    fn modes_and_the_foil() {
        assert_eq!(drama_mode('B', &json!({"orientation": "Poly"})), 'A');
        assert_eq!(drama_mode('B', &json!({"drama_mode_override": "C", "orientation": "Competitive"})), 'C');
        let list = vec![("a".to_string(), json!({"traits": {"competitiveness": 70, "jealousy_proneness": 60}})), ("b".to_string(), json!({}))];
        assert_eq!(foil(&list), "a");
    }

    #[test]
    fn bonds_settle_by_how_often_they_meet() {
        // Playtest 2026-10-13: every pair sat at 97-100. Meeting every day now settles well below 100, and a bond left
        // alone drifts back toward 30.
        let mut heat = 30.0;
        for _ in 0..200 {
            heat = bond_heat_add(heat, 3.0);
            heat = bond_heat_add(heat, 2.0);
            heat -= (heat - BOND_HEAT_REST) * BOND_HEAT_DRIFT;
        }
        assert!(heat > 60.0 && heat < 90.0, "daily friends settle at {heat}");
        let mut alone = 100.0;
        for _ in 0..60 {
            alone -= (alone - BOND_HEAT_REST) * BOND_HEAT_DRIFT;
        }
        assert!(alone < 45.0, "{alone}");
        assert_eq!(bond_heat_add(50.0, -3.0), 47.0, "cooling is not slowed");
    }

    #[test]
    fn jealousy_from_news() {
        let mut r: Relation = serde_json::from_value(rel(json!({"stage": "friend", "affection": 50}))).unwrap();
        let t = Traits::from_profile(&json!({"traits": {"jealousy_proneness": 50}}));
        jealousy_news(&mut r, &t, 'C', false, "b", 1.0);
        assert_eq!(r.jealousy_toward["b"], 18.0);
        jealousy_news(&mut r, &t, 'A', false, "b", 1.0);
        assert_eq!(r.jealousy_toward["b"], 18.0, "mode A stays out of it");
    }

    #[test]
    fn handing_money_over_is_a_lend_offer() {
        let offered = |his: &str| eval("lend_offered", &json!({"his": his})).unwrap().unwrap()["offered"].clone();
        assert_eq!(offered("给，三百块，你拿着。不急着还。"), true);
        assert_eq!(offered("借你三百"), true);
        assert_eq!(offered("我只有一千块左右"), false);
    }

    #[test]
    fn stood_up_and_kept() {
        let her = |favor: f32| json!({"relation": rel(json!({"stage": "friend", "favor": favor, "trust": 50, "admiration": 0})), "profile": {}});
        let v = appointments_tick(&json!({"now": 1000 + DATE_WAIT, "day": 0, "with": "c",
            "appointments": [{"id": 1, "kind": "date", "heroine": "a", "place": "cafe", "start": 900, "grace": 20, "status": "pending"},
                             {"id": 2, "kind": "reservation", "heroine": "", "place": "restaurant", "start": 900, "grace": 15, "status": "pending"},
                             {"id": 3, "kind": "date", "heroine": "b", "place": "park", "start": 995, "grace": 20, "status": "pending", "there": true}],
            "heroines": {"a": her(60.0), "b": her(60.0)}})).unwrap();
        assert_eq!(v["updates"], json!([{"id": 1, "status": "missed"}, {"id": 2, "status": "missed"}, {"id": 3, "status": "late"}]));
        assert_eq!(v["blacklist"][0]["until"], 7);
        // A shift: an hour late still counts (late); past that he loses the job, and the place does not ban him.
        let shift = |now: i64| appointments_tick(&json!({"now": now, "day": 0, "appointments": [{"id": 5, "kind": "shift", "heroine": "", "place": "hotel", "start": 720, "grace": 10, "status": "pending", "there": true}], "heroines": {}})).unwrap();
        assert_eq!(shift(720 + 10 + 30)["updates"], json!([{"id": 5, "status": "late"}]));
        let away = appointments_tick(&json!({"now": 720 + 10 + 61, "day": 0, "appointments": [{"id": 5, "kind": "shift", "heroine": "", "place": "hotel", "start": 720, "grace": 10, "status": "pending"}], "heroines": {}})).unwrap();
        assert_eq!((away["updates"].clone(), away["fired"].clone(), away["blacklist"].clone()), (json!([{"id": 5, "status": "missed"}]), json!([{"place": "hotel"}]), json!([])));
        assert_eq!(v["arrive"][0]["heroine"], "b");
        let fx = v["effects"].as_array().unwrap();
        assert!(fx.iter().any(|e| e["op"] == "bond_event" && e["b"] == "c"));
        // A no-show costs 20 favor; 65 minutes late costs 1 per 3 minutes, at most the same 20.
        assert_eq!(fx.iter().filter(|e| e["op"] == "relation" && e["set"]["favor"] == 40.0).count(), 2, "{fx:?}");
        // Busy with someone else: she waits.
        let v = appointments_tick(&json!({"now": 990, "talking_to": "c", "appointments": [{"id": 3, "kind": "date", "heroine": "b", "start": 995, "grace": 20, "status": "pending", "there": true}], "heroines": {"b": her(60.0)}})).unwrap();
        // Still held up after the grace, but he is there: she keeps waiting; free again within the hour, he is late.
        let held = appointments_tick(&json!({"now": 1040, "talking_to": "c", "appointments": [{"id": 4, "kind": "date", "heroine": "b", "start": 995, "grace": 20, "status": "pending", "there": true}], "heroines": {"b": her(60.0)}})).unwrap();
        assert!(held["updates"].as_array().unwrap().is_empty(), "{held}");
        let freed = appointments_tick(&json!({"now": 1050, "appointments": [{"id": 4, "kind": "date", "heroine": "b", "start": 995, "grace": 20, "status": "pending", "there": true}], "heroines": {"b": her(60.0)}})).unwrap();
        assert_eq!(freed["updates"][0]["status"], "late");
        let gone = appointments_tick(&json!({"now": 1100, "talking_to": "c", "appointments": [{"id": 5, "kind": "date", "heroine": "b", "start": 995, "grace": 20, "status": "pending", "there": false}], "heroines": {"b": her(60.0)}})).unwrap();
        assert_eq!(gone["updates"][0]["status"], "missed", "not there at all is still a no-show");
        assert_eq!(v["updates"], json!([]));
    }

    #[test]
    fn arriving_together_is_not_late() {
        // Walked there with her from lunch, eight minutes past the hour: done, no favor lost; alone it would be late.
        let her = |favor: f32| json!({"relation": rel(json!({"stage": "friend", "favor": favor, "trust": 50, "admiration": 0})), "profile": {}});
        let appt = json!([{"id": 6, "kind": "date", "heroine": "b", "start": 995, "grace": 20, "status": "pending", "there": true}]);
        let together = appointments_tick(&json!({"now": 1003, "talking_to": "b", "came_with": "b", "appointments": appt, "heroines": {"b": her(60.0)}})).unwrap();
        assert_eq!(together["updates"][0]["status"], "done", "{together}");
        assert_eq!(together["arrive"][0]["late"], false);
        let alone = appointments_tick(&json!({"now": 1003, "talking_to": "b", "appointments": appt, "heroines": {"b": her(60.0)}})).unwrap();
        assert_eq!(alone["updates"][0]["status"], "late");
        let other = appointments_tick(&json!({"now": 1003, "came_with": "c", "appointments": appt, "heroines": {"b": her(60.0)}})).unwrap();
        assert_eq!(other["updates"][0]["status"], "late", "came with someone else");
    }

    #[test]
    fn gossip_spreads_between_friends() {
        let l = |id: &str| json!({"id": id, "name": id, "relation": rel(json!({"id": id, "stage": "friend", "affection": 50})), "profile": {}, "mode": "B"});
        let mut spread = 0;
        for seed in 0..200 {
            let v = encounter(&json!({"a": l("a"), "b": l("b"), "heat": 20, "seed": seed, "rumors": [{"id": 1, "text": "x", "about": "c", "kind": "date", "heat": 90, "confidence": 1, "known_by": ["a"]}]})).unwrap();
            if v["learned"] == 1 {
                spread += 1;
                assert!((v["rumors"][0]["heard"]["b"].as_f64().unwrap() - 0.85).abs() < 1e-6);
                assert!(v["effects"].as_array().unwrap().iter().any(|e| e["op"] == "relation" && e["id"] == "b"), "she is jealous of c now");
            }
        }
        assert!(spread > 60 && spread < 140, "about half: {spread}");
    }

    #[test]
    fn acting_first() {
        let v = initiative(&json!({"minute": 13 * 60, "day": 3, "last_slot": 0, "holiday": "聖誕節", "heroines": [{"actor": 1, "id": "a", "relation": rel(json!({"stage": "friend", "favor": 200})), "profile": {}}]})).unwrap();
        assert_eq!((v["slot"].as_i64(), v["actor"].as_i64()), (Some(14), Some(1)));
        assert!(v["why"].as_str().unwrap().contains("聖誕節"));
        let again = initiative(&json!({"minute": 13 * 60 + 30, "day": 3, "last_slot": 14, "heroines": []})).unwrap();
        assert!(again.get("actor").is_none());
        // Jealous of b: she comes to have it out with him once; the same jealousy does not bring her back every slot.
        let jealous = rel(json!({"stage": "lover", "favor": 120, "jealousy": 60, "jealousy_toward": {"b": 70}}));
        let h = |r: Value| json!({"minute": 13 * 60, "day": 4, "last_slot": 0, "names": {"b": "B"}, "heroines": [{"actor": 1, "id": "a", "relation": r, "profile": {}}]});
        let first = initiative(&h(jealous.clone())).unwrap();
        assert!(first["why"].as_str().unwrap_or("").contains('B'), "{first}");
        let patch = first["effects"].as_array().unwrap().iter().find(|e| e["op"] == "relation").cloned().unwrap();
        let flags = patch.to_string();
        assert!(flags.contains("confront_j:70"), "{flags}");
        let mut later = jealous.clone();
        later["flags"] = json!(["confront_j:70"]);
        let second = initiative(&h(later.clone())).unwrap();
        assert!(!second["why"].as_str().unwrap_or("").contains('B'), "{second}");
                later["jealousy_toward"] = json!({"b": 80});
        let news = initiative(&h(later)).unwrap();
        assert!(news["why"].as_str().unwrap_or("").contains('B'), "new news: {news}");
    }

    #[test]
    fn told_face_to_face_is_not_a_rumour_to_ask_about() {
        let jealous = rel(json!({"stage": "friend", "favor": 30, "jealousy": 60, "jealousy_toward": {"b": 70}}));
        let told = rival_named(&json!({"relation": jealous.clone(), "said": "是，我跟B在交往了，你还是我很珍惜的朋友", "names": {"b": "B"}})).unwrap();
        assert_eq!(told["named"], true);
        let patch = told["effects"].as_array().unwrap().iter().find(|e| e["op"] == "relation").cloned().unwrap().to_string();
        assert!(patch.contains("confront_j:70"), "{patch}");
        let other = rival_named(&json!({"relation": jealous.clone(), "said": "今天天气真好", "names": {"b": "B"}})).unwrap();
        assert_eq!(other["named"], false);
        let mut after = jealous.clone();
        after["flags"] = json!(["confront_j:70"]);
        let v = initiative(&json!({"minute": 13 * 60, "day": 4, "last_slot": 0, "names": {"b": "B"}, "heroines": [{"actor": 1, "id": "a", "relation": after, "profile": {}}]})).unwrap();
        assert!(!v["why"].as_str().unwrap_or("").contains('B'), "{v}");
    }

    #[test]
    fn her_own_texts_and_calls() {
        let mut log = json!([]);
        let mut sent = 0;
        for m in (600..700).step_by(10) {
            let v = self_text(&json!({"now": m, "log": log})).unwrap();
            if v["allowed"] == true {
                sent += 1;
            }
            log = v["log"].clone();
        }
        assert_eq!(sent, 3);
        let r: Relation = serde_json::from_value(rel(json!({"stage": "lover"}))).unwrap();
        assert!((phone_answer(&r, "sleep") - 0.135).abs() < 1e-6);
    }

    #[test]
    fn calling_a_girl_over_by_name() {
        let nearby = json!([{"name": "Alter Arthuria"}, {"name": "凶鳥 狂死香"}, {"name": "Hishiro Chizuru", "aliases": ["日代千鶴"]}]);
        let ask = |said: &str| invite_named(&json!({"said": said, "nearby": nearby})).unwrap()["invite"].clone();
        assert_eq!(ask("Alter，妳也過來一起聊吧"), json!(["Alter Arthuria"]));
        assert_eq!(ask("alter 一起來"), json!(["Alter Arthuria"]), "any case");
        assert_eq!(ask("狂死香和千鶴也來吧"), json!(["凶鳥 狂死香", "Hishiro Chizuru"]));
        assert_eq!(ask("Alter 今天好可愛"), json!([]), "a name without asking her over");
        assert_eq!(ask("不要叫 Alter 過來"), json!([]));
    }

    #[test]
    fn the_place_he_names() {
        let places = json!([{"id": "park", "name": "中央公園", "dist": 900}, {"id": "cafe", "name": "咖啡廳", "dist": 300}, {"id": "toilet_a", "name": "公廁（大學講堂旁）", "dist": 100}, {"id": "park2", "name": "河濱公園", "dist": 2000}]);
        let named = |said: &str| place_named(&json!({"said": said, "places": places})).unwrap()["id"].clone();
        assert_eq!(named("大家一起去公園散步吧"), "park", "the nearer park");
        assert_eq!(named("去河濱公園走走"), "park2", "the whole name beats the end of a nearer one");
        assert_eq!(named("去咖啡廳坐坐"), "cafe");
        assert_eq!(named("我們到處走走"), "");
    }
}
