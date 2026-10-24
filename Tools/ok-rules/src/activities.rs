//! The activities together (rules/activities.toml): where each happens and how Unreal shows it, and the rules of who
//! may do it, in what weather, what it uses up and what it leaves. Unreal takes the table once (`activities_table`) and
//! only plays it; whether one may start now is `activity_check`, what is out of reach here for her words is
//! `activity_blocked`. Minutes and needs written in the table go into `activity_end` (shop.rs) through `table_end`.
use crate::catalog::Catalog;
use crate::locale::Texts;
use serde::Deserialize;
use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct Activity {
    pub id: String,
    /// The data's own name (Traditional Chinese); `name` of `table_in` is in the language (names.toml ui.name.activity).
    pub name: String,
    #[serde(default)]
    pub place: String,
    pub kind: String,
    #[serde(default)]
    pub code: i32,
    /// Anchor in the place's model; "a|b" takes the first one the model has (a new scene's anchor wins once it is there).
    #[serde(default)]
    pub anchor: String,
    /// Degrees added to the anchor's facing (the fountain's spot faces away from it for a photo with it behind them;
    /// feeding the fish turns them round to the water).
    #[serde(default)]
    pub turn: f32,
    #[serde(default)]
    pub female_clip: String,
    #[serde(default)]
    pub male_clip: String,
    /// Only these heroines (stable id or a name in any language); empty: anyone.
    #[serde(default)]
    pub who: Vec<String>,
    /// any | dry | rain
    #[serde(default)]
    pub weather: String,
    /// Item ids it needs (his bag or hers). Used up when it ends, unless `consume` is false (a sketchbook stays).
    #[serde(default)]
    pub needs: Vec<String>,
    #[serde(default = "yes")]
    pub consume: bool,
    /// What it leaves: photo | drawing | work_progress | book_progress.
    #[serde(default)]
    pub yields: Vec<String>,
    #[serde(default)]
    pub once_per_day: bool,
    /// Only on this festival (events.toml id): Halloween's night walk, the Christmas lights.
    #[serde(default)]
    pub event: String,
    /// What Unreal shows while it runs, beyond the poses ("explosion": Megumin's spell, far off).
    #[serde(default)]
    pub effect: String,
    #[serde(default)]
    pub minutes: i32,
    /// Her needs at the end (fun, energy, social, satiety, hygiene).
    #[serde(default)]
    pub feed: BTreeMap<String, f32>,
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields)]
pub struct ActivitiesTuning {
    #[serde(default)]
    pub activity: Vec<Activity>,
}

impl ActivitiesTuning {
    pub fn find(&self, id: &str) -> Option<&Activity> {
        self.activity.iter().find(|a| a.id == id)
    }

    /// The same table with the names in a language (the data's own where names.toml has none).
    pub fn named(&self, texts: &Texts, lang: &str) -> ActivitiesTuning {
        let mut out = self.clone();
        for a in &mut out.activity {
            if let Some(n) = texts.own(lang, &format!("ui.name.activity.{}", a.id)) {
                a.name = n.to_string();
            }
        }
        out
    }
}

fn yes() -> bool {
    true
}

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

fn list(r: &Value, k: &str) -> Vec<String> {
    r.get(k).and_then(Value::as_array).map(|a| a.iter().filter_map(|v| v.as_str().map(str::to_string)).collect()).unwrap_or_default()
}

/// Why this activity cannot start now, or None. Request fields: girl {id, names[]}, raining, his_items[], her_items[],
/// done_today[] (activity ids done with her today).
fn blocked(a: &Activity, c: &Catalog, r: &Value) -> Option<(String, String)> {
    if !a.who.is_empty() {
        let girl = r.get("girl").cloned().unwrap_or(Value::Null);
        let mut hers = list(&girl, "names");
        hers.push(s(&girl, "id").to_string());
        // "profile:a.b": her profile has that field filled (help_her_work: she has work of her own).
        let by_profile = |w: &str| {
            w.strip_prefix("profile:").is_some_and(|path| {
                let mut v = r.get("profile").cloned().unwrap_or(Value::Null);
                for k in path.split('.') {
                    v = v.get(k).cloned().unwrap_or(Value::Null);
                }
                v.as_str().is_some_and(|t| !t.is_empty())
            })
        };
        if !a.who.iter().any(|w| by_profile(w) || hers.iter().any(|h| !h.is_empty() && h == w)) {
            return Some(("who".into(), crate::tr::t("activity.only_some").into()));
        }
    }
    let raining = r.get("raining").and_then(Value::as_bool).unwrap_or(false);
    match a.weather.as_str() {
        "dry" if raining => return Some(("rain".into(), crate::tr::t("activity.raining").into())),
        "rain" if !raining => return Some(("dry".into(), crate::tr::t("activity.not_raining").into())),
        _ => {}
    }
    if !a.event.is_empty() {
        let day = r.get("day").and_then(Value::as_i64).unwrap_or(-1) as i32;
        if !crate::events::on(day).iter().any(|e| e.id == a.event) {
            return Some(("event".into(), crate::tr::m("activity.not_event_day", &[("what", crate::tr::a(crate::events::name_of(&a.event)))])));
        }
    }
    if a.once_per_day && list(r, "done_today").contains(&a.id) {
        return Some(("once".into(), crate::tr::t("activity.done_today").into()));
    }
    let (his, her) = (list(r, "his_items"), list(r, "her_items"));
    for need in &a.needs {
        if !his.contains(need) && !her.contains(need) {
            let item = c.items.iter().find(|i| &i.id == need);
            let name = item.map(|i| i.name.clone()).unwrap_or_else(|| need.clone());
            let place = item.map(|i| i.place.clone()).unwrap_or_default();
            return Some((
                "needs".into(),
                crate::tr::m("activity.needs_item", &[("item", crate::tr::a(&name)), ("place", crate::tr::a(&place_name(&place, r)))]),
            ));
        }
    }
    None
}

/// The shop's name as Unreal sent it (place_names {id: name}), else its id.
fn place_name(id: &str, r: &Value) -> String {
    r.get("place_names").and_then(|m| m.get(id)).and_then(Value::as_str).unwrap_or(id).to_string()
}

/// What it needs and neither of them has, when all of it is sold right here (`here`) or where the activity is done
/// (the fish food by the pond, asked for at the cafe) and he can pay (`player_cash`): he buys it. Else None.
fn buy_here(a: &Activity, c: &Catalog, r: &Value) -> Option<(Vec<Value>, i64)> {
    let (his, her, here) = (list(r, "his_items"), list(r, "her_items"), s(r, "here"));
    let mut buy = Vec::new();
    let mut total = 0i64;
    for need in a.needs.iter().filter(|n| !his.contains(n) && !her.contains(n)) {
        let item = c.items.iter().find(|i| &i.id == need).filter(|i| (!here.is_empty() && i.place == here) || (!a.place.is_empty() && i.place == a.place))?;
        let price = item.price_of(&item.default_size()) as i64;
        total += price;
        buy.push(json!({"item": item.id, "name": item.name, "price": price}));
    }
    // Not sent (the lists for her prompt): taken as affordable; the check before starting sends his cash.
    let cash = r.get("player_cash").and_then(Value::as_i64).unwrap_or(i64::MAX);
    (!buy.is_empty() && total <= cash).then_some((buy, total))
}

/// What still stops it when what it needs can be bought there (the lists for her prompt: feeding the fish is not
/// "impossible, no fish food" in the park, where it is sold).
fn blocked_unless_bought(a: &Activity, c: &Catalog, r: &Value) -> Option<(String, String)> {
    let b = blocked(a, c, r)?;
    if b.0 != "needs" {
        return Some(b);
    }
    let mut r2 = r.clone();
    if s(r, "here").is_empty() {
        r2["here"] = json!(s(r, "place"));
    }
    let Some((buy, _)) = buy_here(a, c, &r2) else { return Some(b) };
    let mut his = list(r, "his_items");
    his.extend(buy.iter().filter_map(|x| x["item"].as_str().map(String::from)));
    r2["his_items"] = json!(his);
    blocked(a, c, &r2)
}

/// May it start now: ok, or why not (reason: who | rain | dry | once | needs, and a fact for her in the language);
/// when ok, the items it uses up and from whose bag (his first). What it needs and is sold where they stand he buys
/// first (buy [{item, name, price}], effects: the cash).
fn check(t: &ActivitiesTuning, c: &Catalog, r: &Value) -> Value {
    let Some(a) = t.find(s(r, "id")) else {
        return json!({"ok": true, "known": false});
    };
    // At its end (at_end) nothing blocks any more: only what it used up is asked for. Before, the things are still in
    // the bag while they do it (she saw no sketchbook in his bag and thought he came empty-handed).
    let at_end = r.get("at_end").and_then(Value::as_bool).unwrap_or(false);
    if !at_end {
        if let Some((reason, why)) = blocked(a, c, r) {
            if reason == "needs" {
                if let Some((buy, total)) = buy_here(a, c, r) {
                    let mut fx = crate::effects::Effects::default();
                    let names: Vec<&str> = buy.iter().filter_map(|b| b["name"].as_str()).collect();
                    fx.cash(-total, "shopping", &names.join("、"));
                    // Bought: only the other conditions are left to look at.
                    let mut r2 = r.clone();
                    let mut his = list(r, "his_items");
                    his.extend(buy.iter().filter_map(|b| b["item"].as_str().map(String::from)));
                    r2["his_items"] = json!(his);
                    if let Some((reason, why)) = blocked(a, c, &r2) {
                        return json!({"ok": false, "known": true, "reason": reason, "why": why});
                    }
                    return json!({"ok": true, "known": true, "consume": [], "yields": a.yields, "buy": buy, "effects": fx.into_value()});
                }
            }
            return json!({"ok": false, "known": true, "reason": reason, "why": why});
        }
    }
    let his = list(r, "his_items");
    let used = if at_end && a.consume { a.needs.clone() } else { Vec::new() };
    let consume: Vec<Value> = used.iter().map(|n| json!({"item": n, "from": if his.contains(n) { "his" } else { "her" }})).collect();
    json!({"ok": true, "known": true, "consume": consume, "yields": a.yields})
}

/// What cannot be done right now among this place's activities and those done where they stand, for her prompt:
/// [{id, name, why}]. Only the ones that have a condition (who, weather, needs, once a day) can be listed.
fn blocked_here(t: &ActivitiesTuning, c: &Catalog, r: &Value) -> Value {
    let place = s(r, "place");
    let out: Vec<Value> = t
        .activity
        .iter()
        .filter(|a| a.place.is_empty() || a.place == place)
        .filter_map(|a| blocked_unless_bought(a, c, r).filter(|(reason, _)| reason != "who" && reason != "event").map(|(reason, why)| json!({"id": a.id, "name": a.name, "reason": reason, "why": why})))
        .collect();
    json!({"blocked": out})
}

/// The activities of the place they are in that can be done now ([{id, name}]): her choice and the judge's are
/// grounded on it (in the park, standing practice is the park's, not the playground's stretching).
fn here(t: &ActivitiesTuning, c: &Catalog, r: &Value) -> Value {
    let place = s(r, "place");
    if place.is_empty() {
        return json!({"here": []});
    }
    let out: Vec<Value> = t.activity.iter().filter(|a| a.place == place && blocked_unless_bought(a, c, r).is_none()).map(|a| json!({"id": a.id, "name": a.name})).collect();
    json!({"here": out})
}

fn table(t: &ActivitiesTuning, own: &ActivitiesTuning) -> Value {
    let rows: Vec<Value> = t
        .activity
        .iter()
        .map(|a| {
            let own_name = own.find(&a.id).map(|o| o.name.clone()).unwrap_or_else(|| a.name.clone());
            json!({"id": a.id, "name": a.name, "own_name": own_name, "place": a.place, "kind": a.kind, "code": a.code,
                   "anchor": a.anchor, "turn": a.turn, "female_clip": a.female_clip, "male_clip": a.male_clip, "effect": a.effect})
        })
        .collect();
    json!({"activities": rows})
}

/// The table's minutes and needs for activity_end / activity_minutes (shop.rs reads table_minutes, table_feed).
pub fn table_end(t: &ActivitiesTuning, request: &mut Value) {
    let Some(a) = t.find(s(request, "id")) else { return };
    if let Some(o) = request.as_object_mut() {
        if a.minutes > 0 {
            o.insert("table_minutes".into(), json!(a.minutes));
        }
        if !a.yields.is_empty() {
            o.insert("table_yields".into(), json!(a.yields));
        }
        if !a.feed.is_empty() {
            let feed: Map<String, Value> = a.feed.iter().map(|(k, v)| (k.clone(), json!(v))).collect();
            o.insert("table_feed".into(), Value::Object(feed));
        }
    }
}

pub fn eval(t: &ActivitiesTuning, own: &ActivitiesTuning, c: &Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "activities_table" => table(t, own),
        "activity_check" => check(t, c, r),
        "activity_blocked" => blocked_here(t, c, r),
        "activities_here" => here(t, c, r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    fn tuning() -> Tuning {
        Tuning::builtin()
    }

    #[test]
    fn table_has_every_activity_with_its_show() {
        let t = tuning();
        let v = table(&t.activities, &t.activities);
        let rows = v["activities"].as_array().unwrap();
        assert!(rows.len() >= 86);
        let seat = rows.iter().find(|r| r["id"] == "drink_seat").unwrap();
        assert_eq!(seat["kind"], "pair");
        assert_eq!(seat["code"], 1);
        assert_eq!(seat["female_clip"], "venue_drink_seated.okanim");
        let walk = rows.iter().find(|r| r["id"] == "walk").unwrap();
        assert_eq!(walk["place"], "");
    }

    #[test]
    fn what_it_needs_is_bought_where_it_is_sold() {
        let mut c = Catalog::default();
        c.items.push(crate::catalog::Item { id: "park_fishfood".into(), place: "park".into(), name: "魚飼料".into(), price: [(String::new(), 5)].into_iter().collect(), ..Default::default() });
        let mut t = ActivitiesTuning::default();
        t.activity.push(Activity { id: "park_feed_fish".into(), name: "餵魚".into(), kind: "solo".into(), needs: vec!["park_fishfood".into()], consume: true, ..Default::default() });
        let at_pond = check(&t, &c, &json!({"id": "park_feed_fish", "here": "park", "player_cash": 100}));
        assert_eq!((at_pond["ok"].as_bool(), at_pond["buy"][0]["price"].as_i64()), (Some(true), Some(5)));
        assert_eq!(check(&t, &c, &json!({"id": "park_feed_fish", "here": "cafe", "player_cash": 100}))["ok"], false, "not sold here");
        t.activity[0].place = "park".into();
        assert_eq!(check(&t, &c, &json!({"id": "park_feed_fish", "here": "cafe", "player_cash": 100}))["ok"], true, "sold where it is done");
        assert_eq!(blocked_here(&t, &c, &json!({"place": "park"}))["blocked"].as_array().map(Vec::len), Some(0), "not listed as impossible in the park");
        assert_eq!(check(&t, &c, &json!({"id": "park_feed_fish", "here": "park", "player_cash": 2}))["ok"], false, "cannot pay");
        assert!(check(&t, &c, &json!({"id": "park_feed_fish", "here": "park", "his_items": ["park_fishfood"]})).get("buy").is_none());
    }

    #[test]
    fn rain_needs_and_who_block_and_say_why() {
        let t = tuning();
        let mut a = Activity { id: "x_sketch".into(), name: "寫生".into(), kind: "pair".into(), weather: "dry".into(), needs: vec!["park_fishfood".into()], consume: true, ..Default::default() };
        a.once_per_day = true;
        let mut tab = ActivitiesTuning::default();
        tab.activity.push(a);
        tab.activity.push(Activity { id: "x_magic".into(), name: "魔法".into(), kind: "solo".into(), who: vec!["惠惠".into()], ..Default::default() });
        let c = &t.catalog;
        let rain = check(&tab, c, &json!({"id": "x_sketch", "raining": true, "his_items": ["park_fishfood"]}));
        assert_eq!(rain["ok"], false);
        assert_eq!(rain["reason"], "rain");
        let none = check(&tab, c, &json!({"id": "x_sketch", "raining": false}));
        assert_eq!(none["reason"], "needs");
        let hers = check(&tab, c, &json!({"id": "x_sketch", "her_items": ["park_fishfood"]}));
        assert_eq!(hers["ok"], true);
        assert_eq!(hers["consume"], json!([]), "nothing is used up before it ends");
        let end = check(&tab, c, &json!({"id": "x_sketch", "her_items": ["park_fishfood"], "raining": true, "at_end": true}));
        assert_eq!(end["consume"][0]["from"], "her");
        let again = check(&tab, c, &json!({"id": "x_sketch", "his_items": ["park_fishfood"], "done_today": ["x_sketch"]}));
        assert_eq!(again["reason"], "once");
        assert_eq!(check(&tab, c, &json!({"id": "x_magic", "girl": {"id": "apng-1", "names": ["Keqing", "刻晴"]}}))["ok"], false);
        assert_eq!(check(&tab, c, &json!({"id": "x_magic", "girl": {"id": "apng-2", "names": ["Megumin", "惠惠"]}}))["ok"], true);
        // Someone else's activity is not listed as "out of reach" for her: she would only hear about what she cannot do.
        let b = blocked_here(&tab, c, &json!({"place": "", "raining": true, "girl": {"names": ["刻晴"]}}));
        let ids: Vec<&str> = b["blocked"].as_array().unwrap().iter().map(|v| v["id"].as_str().unwrap()).collect();
        assert_eq!(ids, vec!["x_sketch"]);
        let park = tuning();
        let h = here(&park.activities, c, &json!({"place": "park", "raining": false}));
        let ids: Vec<&str> = h["here"].as_array().unwrap().iter().map(|v| v["id"].as_str().unwrap()).collect();
        assert!(ids.contains(&"park_exercise") && !ids.contains(&"field_stretch"));
        let wet = here(&park.activities, c, &json!({"place": "park", "raining": true}));
        assert!(!wet["here"].as_array().unwrap().iter().any(|v| v["id"] == "park_exercise"));
    }

    #[test]
    fn festival_activities_open_only_on_their_day() {
        let t = tuning();
        crate::events::install(&t.events);
        let c = &t.catalog;
        // 2010-10-31 is game day 60.
        let off = check(&t.activities, c, &json!({"id": "halloween_walk", "day": 40}));
        assert_eq!(off["reason"], "event");
        assert_eq!(check(&t.activities, c, &json!({"id": "halloween_walk", "day": 60}))["ok"], true);
        let quiet = blocked_here(&t.activities, c, &json!({"place": "", "day": 40}));
        assert!(!quiet["blocked"].as_array().unwrap().iter().any(|v| v["id"] == "halloween_walk"), "not told about it on other days");
        let park = |day: i64| here(&t.activities, c, &json!({"place": "park", "day": day, "raining": false}))["here"].as_array().unwrap().iter().any(|v| v["id"] == "new_years_eve_countdown");
        assert!(!park(100) && park(121), "2010-12-31 is game day 121");
    }

    #[test]
    fn table_minutes_and_feed_reach_activity_end() {
        let mut tab = ActivitiesTuning::default();
        let mut a = Activity { id: "x_jog".into(), name: "跑步".into(), kind: "walk".into(), minutes: 40, ..Default::default() };
        a.feed.insert("energy".into(), -20.0);
        tab.activity.push(a);
        let mut r = json!({"id": "x_jog"});
        table_end(&tab, &mut r);
        assert_eq!(r["table_minutes"], 40);
        assert_eq!(r["table_feed"]["energy"], -20.0);
    }
}
