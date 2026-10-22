//! The catalogue (Content/IndependentResources/StreetSocialData/catalog.json): opening hours, what each place sells and
//! the jobs, and the money rules that read it: whether a place is open, her taste for an item, what she orders when
//! she eats out alone, who wants to pay a bill, and how a gift lands.
use crate::rng::RandomStream;
use serde_json::{json, Value};
use std::collections::BTreeMap;

#[derive(Debug, Clone, Default)]
pub struct Place {
    /// Hour ranges; "to" may pass 24 (overnight).
    pub open: Vec<(f64, f64)>,
    pub weekend_open: Vec<(f64, f64)>,
    /// Monday 0.
    pub closed_weekdays: Vec<i32>,
}

#[derive(Debug, Clone, Default)]
pub struct Item {
    pub id: String,
    pub place: String,
    pub name: String,
    pub category: String,
    /// size ("" for one size, or M/L/S) -> price
    pub price: BTreeMap<String, i32>,
    pub season: Vec<i32>,
    pub tags: Vec<String>,
    pub options: Vec<String>,
    pub effects: BTreeMap<String, f32>,
    pub giftable: bool,
    pub consumable: bool,
}

impl Item {
    pub fn price_of(&self, size: &str) -> i32 {
        self.price.get(size).copied().unwrap_or_else(|| self.price.values().copied().min().unwrap_or(0))
    }

    /// One size, else M, else the first.
    pub fn default_size(&self) -> String {
        if self.price.contains_key("") {
            String::new()
        } else if self.price.contains_key("M") {
            "M".into()
        } else {
            self.price.keys().next().cloned().unwrap_or_default()
        }
    }
}

/// A weekday in the data: a name ("mon" .. "sun"; which day is plain to whoever writes it) or a number (0 = Monday).
pub fn weekday_of(v: &Value) -> Option<i32> {
    if let Some(n) = v.as_i64() {
        return Some(n as i32);
    }
    ["mon", "tue", "wed", "thu", "fri", "sat", "sun"].iter().position(|d| v.as_str().is_some_and(|s| s.eq_ignore_ascii_case(d))).map(|k| k as i32)
}

#[derive(Debug, Clone, Default)]
pub struct Catalog {
    pub places: BTreeMap<String, Place>,
    pub items: Vec<Item>,
    /// Job id -> (place, wage).
    pub jobs: BTreeMap<String, (String, i32)>,
}

fn ranges(o: &Value, k: &str) -> Vec<(f64, f64)> {
    o.get(k).and_then(Value::as_array).map(|a| a.iter().filter_map(|r| r.as_array().filter(|r| r.len() == 2).map(|r| (r[0].as_f64().unwrap_or(0.0), r[1].as_f64().unwrap_or(0.0)))).collect()).unwrap_or_default()
}

fn strings(o: &Value, k: &str) -> Vec<String> {
    o.get(k).and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_str).map(String::from).collect()).unwrap_or_default()
}

impl Catalog {
    pub fn parse(root: &Value) -> Catalog {
        let mut c = Catalog::default();
        for (id, o) in root.get("places").and_then(Value::as_object).into_iter().flatten() {
            c.places.insert(
                id.clone(),
                Place {
                    open: ranges(o, "open"),
                    weekend_open: ranges(o, "weekend_open"),
                    closed_weekdays: o.get("closed_weekdays").and_then(Value::as_array).map(|a| a.iter().filter_map(weekday_of).collect()).unwrap_or_default(),
                },
            );
        }
        for o in root.get("items").and_then(Value::as_array).into_iter().flatten() {
            let s = |k: &str| o.get(k).and_then(Value::as_str).unwrap_or("").to_string();
            let item = Item {
                id: s("id"),
                place: s("place"),
                name: s("name"),
                category: s("category"),
                price: o.get("price").and_then(Value::as_object).map(|m| m.iter().filter_map(|(k, v)| v.as_f64().map(|p| (k.clone(), p as i32))).collect()).unwrap_or_default(),
                season: o.get("season").and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_i64).map(|m| m as i32).collect()).unwrap_or_default(),
                tags: strings(o, "tags"),
                options: strings(o, "options"),
                effects: o.get("effects").and_then(Value::as_object).map(|m| m.iter().filter_map(|(k, v)| v.as_f64().map(|e| (k.clone(), e as f32))).collect()).unwrap_or_default(),
                giftable: o.get("giftable").and_then(Value::as_bool).unwrap_or(false),
                consumable: o.get("consumable").and_then(Value::as_bool).unwrap_or(true),
            };
            if !item.id.is_empty() && !item.price.is_empty() {
                c.items.push(item);
            }
        }
        for o in root.get("jobs").and_then(Value::as_array).into_iter().flatten() {
            let id = o.get("id").and_then(Value::as_str).unwrap_or("").to_string();
            if !id.is_empty() {
                c.jobs.insert(id, (o.get("place").and_then(Value::as_str).unwrap_or("").to_string(), o.get("wage").and_then(Value::as_f64).unwrap_or(95.0) as i32));
            }
        }
        c
    }

    /// Places missing from the catalogue are always open.
    pub fn place_open(&self, place: &str, hours: f64, weekday: i32) -> bool {
        let Some(p) = self.places.get(place) else { return true };
        if p.closed_weekdays.contains(&weekday) {
            return false;
        }
        let r = if weekday >= 5 && !p.weekend_open.is_empty() { &p.weekend_open } else { &p.open };
        r.iter().any(|(from, to)| (hours >= *from && hours < *to) || (*to > 24.0 && hours < to - 24.0)) || r.is_empty()
    }

    /// Why it is closed, in words: the hours of that day (a weekend has its own: the cafeteria opens 11-14 on Saturday,
    /// not 7-19), or a day it does not open at all.
    pub fn closed_why(&self, place: &str, weekday: i32) -> String {
        let Some(p) = self.places.get(place) else { return crate::tr::t("catalog.closed_now").to_string() };
        if p.closed_weekdays.contains(&weekday) {
            // Its day off by name ("closed every Monday"): the job panel says it of tomorrow, where "closed today" was wrong.
            let day = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"].get(weekday as usize).copied().unwrap_or("mon");
            return crate::tr::m("catalog.closed_weekday", &[("weekday", crate::tr::a(crate::tr::lookup(&format!("series.{day}"), day)))]);
        }
        let weekend = weekday >= 5 && !p.weekend_open.is_empty();
        let hours = if weekend { &p.weekend_open } else { &p.open };
        match hours.first() {
            Some((from, to)) => crate::tr::m(if weekend { "catalog.closed_now_weekend_hours" } else { "catalog.closed_now_hours" }, &[("from", crate::tr::a(&fmt_g(*from))), ("to", crate::tr::a(&fmt_g(*to)))]),
            None => crate::tr::t("catalog.closed_now").to_string(),
        }
    }

    pub fn items_at(&self, place: &str, month: i32) -> Vec<&Item> {
        self.items.iter().filter(|i| i.place == place && (i.season.is_empty() || i.season.contains(&month))).collect()
    }

    pub fn find(&self, id: &str) -> Option<&Item> {
        self.items.iter().find(|i| i.id == id)
    }
}

/// %g for an hour like 10 or 21.5.
fn fmt_g(v: f64) -> String {
    if v.fract() == 0.0 {
        format!("{}", v as i64)
    } else {
        format!("{v}")
    }
}

/// How well an item matches her tastes: 2 exact (item and, for drinks, sweetness and ice), 1 liked, 0 neutral, -1
/// disliked or allergic.
pub fn taste_of(profile: &Value, item: &Item, sweetness: &str, ice: &str) -> i32 {
    let Some(tastes) = profile.get("tastes").filter(|t| t.is_object()) else { return 0 };
    let has = |field: &str| strings(tastes, field).iter().any(|s| *s == item.id || item.tags.contains(s) || (!s.is_empty() && item.name.contains(s.as_str())));
    if has("allergies") || has("dislikes") {
        return -1;
    }
    if !has("likes") {
        return 0;
    }
    if item.options.is_empty() {
        return 2;
    }
    let drink = tastes.get("drink").cloned().unwrap_or(Value::Null);
    let want_sweet = drink.get("sweetness").and_then(Value::as_str).unwrap_or("");
    let want_ice = drink.get("ice").and_then(Value::as_str).unwrap_or("");
    if (want_sweet.is_empty() || want_sweet == sweetness) && (want_ice.is_empty() || want_ice == ice) {
        2
    } else {
        1
    }
}

/// Her choice eating out alone: liked items first, within budget, at this place.
pub fn pick_meal<'a>(c: &'a Catalog, place: &str, month: i32, profile: &Value, budget: i32, rng: &mut RandomStream) -> Option<&'a Item> {
    let mut options = Vec::new();
    let mut liked = Vec::new();
    for i in c.items_at(place, month) {
        if !i.consumable || i.price_of(&i.default_size()) > budget.max(crate::region::amount("goods", 30) as i32) {
            continue;
        }
        let t = taste_of(profile, i, "", "");
        if t < 0 {
            continue;
        }
        options.push(i);
        if t > 0 {
            liked.push(i);
        }
    }
    let from = if liked.is_empty() { &options } else { &liked };
    if from.is_empty() {
        None
    } else {
        Some(from[rng.rand_range(0, from.len() as i32 - 1) as usize])
    }
}

pub fn profile_int(p: &Value, section: &str, field: &str, def: i32) -> i32 {
    p.get(section).and_then(|s| s.get(field)).and_then(Value::as_f64).map(|v| v as i32).unwrap_or(def)
}

/// Who she wants to pay a bill, and how hard she insists. treat_balance > 0: he has paid for her more than she for him.
/// cash < 0: her wallet is not known yet (treated as able to pay).
pub fn checkout_lean(profile: &Value, cash: i64, total: i32, treat_balance: i32, stage: &str) -> (&'static str, f32) {
    let pride = profile_int(profile, "money", "treat_pride", 50);
    let expects = profile_int(profile, "money", "expects_treat", 50);
    let reciprocity = profile_int(profile, "money", "reciprocity", 50);
    let share = (total + 1) / 2;
    if cash >= 0 && cash < share as i64 {
        return ("player_pays", 0.3 + expects as f32 / 250.0);
    }
    if treat_balance >= 2 && reciprocity >= 50 && (cash < 0 || cash >= total as i64) {
        return ("she_pays", (0.4 + reciprocity as f32 / 200.0 + treat_balance as f32 * 0.05).clamp(0.0, 0.95));
    }
    if pride >= 65 {
        return ("split", pride as f32 / 110.0);
    }
    if expects >= 60 || stage == "lover" {
        return ("player_pays", 0.3 + expects as f32 / 200.0);
    }
    ("split", 0.4)
}

/// A gift: taste match x price for the relationship. Above 0.5 she gladly accepts; below 0 she declines.
pub fn gift_score(profile: &Value, item: &Item, price: i32, stage: &str, greed: f32) -> f32 {
    let taste = taste_of(profile, item, "", "");
    let style = profile.get("tastes").and_then(|t| t.get("gift_style")).and_then(Value::as_str).unwrap_or("");
    let mut score = 0.3 + taste as f32 * 0.2;
    if (style == "cute" && item.tags.iter().any(|t| t == "cute")) || (style == "luxury" && item.tags.iter().any(|t| t == "luxury")) || (style == "practical" && item.category == "goods") {
        score += 0.3;
    }
    let comfortable = match stage {
        "lover" => crate::region::amount("cash", 4000) as i32,
        "friend" => crate::region::amount("cash", 600) as i32,
        _ => crate::region::amount("cash", 170) as i32,
    };
    let pride = profile_int(profile, "money", "treat_pride", 50);
    if price > comfortable {
        score -= (price - comfortable) as f32 / comfortable as f32 * (pride as f32 / 100.0) - greed / 200.0;
    }
    (score + greed / 400.0).clamp(-1.0, 1.5)
}

pub fn eval(c: &Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    let s = |k: &str| r.get(k).and_then(Value::as_str).unwrap_or("").to_string();
    Some(match kind {
        "place_open" => {
            let place = s("place");
            let open = c.place_open(&place, r.get("hours").and_then(Value::as_f64).unwrap_or(12.0), r.get("weekday").and_then(Value::as_i64).unwrap_or(0) as i32);
            let weekday = r.get("weekday").and_then(Value::as_i64).unwrap_or(0) as i32;
            Ok(json!({"open": open, "why": if open { String::new() } else { c.closed_why(&place, weekday) }}))
        }
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn hours_overnight_and_closed_days() {
        let mut c = Catalog::default();
        c.places.insert("bar".into(), Place { open: vec![(18.0, 26.0)], weekend_open: vec![], closed_weekdays: vec![0] });
        assert!(c.place_open("bar", 20.0, 2) && c.place_open("bar", 1.5, 2) && !c.place_open("bar", 3.0, 2));
        assert!(!c.place_open("bar", 20.0, 0), "closed on Mondays");
        assert!(c.place_open("nowhere", 3.0, 0));
        assert_eq!(c.closed_why("bar", 2), "那裡現在沒開（營業 18–26 點）", "zh-TW, the default language");
        assert_eq!(c.closed_why("bar", 0), "那裡每週一公休", "closed on Mondays, said of any day");
    }

    #[test]
    fn tastes_meals_and_bills() {
        let c = Tuning::builtin().catalog;
        assert!(!c.items.is_empty() && !c.places.is_empty(), "catalog.json parsed");
        let item = Item { id: "tea".into(), name: "綠茶".into(), options: vec!["sweetness".into()], ..Default::default() };
        let p = json!({"tastes": {"likes": ["tea"], "drink": {"sweetness": "half"}}});
        assert_eq!((taste_of(&p, &item, "half", ""), taste_of(&p, &item, "full", "")), (2, 1));
        assert_eq!(taste_of(&json!({"tastes": {"dislikes": ["綠"]}}), &item, "", ""), -1);
        assert_eq!(checkout_lean(&json!({}), 10, 100, 0, "friend").0, "player_pays");
        assert_eq!(checkout_lean(&json!({"money": {"reciprocity": 80}}), 500, 100, 3, "friend").0, "she_pays");
        assert_eq!(checkout_lean(&json!({"money": {"treat_pride": 80}}), 500, 100, 0, "friend").0, "split");
        let place = c.items.iter().find(|i| i.consumable).map(|i| i.place.clone()).unwrap();
        let mut rng = RandomStream::new(3);
        assert!(pick_meal(&c, &place, 9, &json!({}), 500, &mut rng).is_some());
        let gift = Item { id: "bag".into(), tags: vec!["luxury".into()], ..Default::default() };
        assert!(gift_score(&json!({"tastes": {"gift_style": "luxury"}}), &gift, 100, "friend", 0.0) > gift_score(&json!({}), &gift, 100, "friend", 0.0));
    }
}
