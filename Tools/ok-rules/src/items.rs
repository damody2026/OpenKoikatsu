//! Things that belong to someone (Docs/STREET_NEW_FEATURES_PLAN.md 0-2): what a heroine owns (bought herself, his
//! gift, a drawing she made, a saving goal reached), and what is lent between them. The state is part of the save's
//! `ext` document, which only the rules read; Unreal keeps it and sends it along. His own bag stays Unreal's
//! inventory: an answer says what to add to it or take from it (`his_add`, `his_remove`).
//!
//! Who gives or lends what is the AI's reading of the talk (judge fields she_gives, she_lends, she_returns, he_lends,
//! he_returns, each naming the thing as it was said); here it is only matched to the things they really hold.
use crate::catalog::Catalog;
use serde_json::{json, Value};

/// How long a loan is when nobody said (days).
const LOAN_DAYS: i64 = 14;

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

fn day(r: &Value) -> i64 {
    r.get("day").and_then(Value::as_i64).unwrap_or(0)
}

/// The items part of ext, with its three lists present.
fn state(r: &Value) -> Value {
    let mut st = r.get("ext").and_then(|e| e.get("items")).cloned().unwrap_or_else(|| json!({}));
    for (k, empty) in [("owned", json!({})), ("his_borrowed", json!([])), ("her_borrowed", json!([]))] {
        if st.get(k).is_none() {
            st[k] = empty;
        }
    }
    st
}

/// ext with its items part replaced (the rest of ext as it came).
fn with_state(r: &Value, st: Value) -> Value {
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    ext["items"] = st;
    ext
}

/// A thing's name in the language: the catalog's, else the name kept with it (her drawing), else its id.
fn name_of(c: &Catalog, item: &str, kept: &str) -> String {
    c.items.iter().find(|i| i.id == item).map(|i| i.name.clone()).filter(|n| !n.is_empty()).unwrap_or_else(|| if kept.is_empty() { item.to_string() } else { kept.to_string() })
}

/// Whether the words are about her drawing (the words for it in every language), whatever its title says.
fn says_drawing(said: &str) -> bool {
    let low = said.to_lowercase();
    crate::tr::t("items.drawing_words").split('|').any(|w| !w.trim().is_empty() && low.contains(&w.trim().to_lowercase()))
}

/// Which of these held things the words name: the id itself, else the name containing the words or the words the name
/// ("那本劍客小說" names 劍客小說《孤劍行》). None when nothing held matches.
fn find<'a>(c: &Catalog, held: &'a [Value], said: &str) -> Option<usize> {
    let said = said.trim();
    if said.is_empty() {
        return None;
    }
    if let Some(k) = held.iter().position(|h| s(h, "item") == said) {
        return Some(k);
    }
    let norm = |t: &str| t.chars().filter(|ch| !"《》「」\"' 　的那本這个個一".contains(*ch)).collect::<String>().to_lowercase();
    let want = norm(said);
    if want.is_empty() {
        return None;
    }
    let said_drawing = says_drawing(said);
    held.iter().position(|h| {
        let name = norm(&name_of(c, s(h, "item"), s(h, "name")));
        let drawing = s(h, "item").starts_with("drawing:") && said_drawing;
        // An umbrella is "the umbrella" in any script or language (折叠伞 / 雨傘 / umbrella); not in a drawing of one
        // ("the drawing of you under the umbrella" gave him her umbrella, R3-262).
        let umbrella = !said_drawing && is_umbrella(said) && is_umbrella(&format!("{}{}{name}", s(h, "item"), s(h, "name")));
        drawing || umbrella || (!name.is_empty() && (name.contains(&want) || want.contains(&name)))
    })
}

fn owned_mut<'a>(st: &'a mut Value, girl: &str) -> &'a mut Vec<Value> {
    if !st["owned"].get(girl).is_some_and(Value::is_array) {
        st["owned"][girl] = json!([]);
    }
    st["owned"][girl].as_array_mut().unwrap()
}

fn give_to_her(st: &mut Value, girl: &str, item: &str, name: &str, from: &str, today: i64) {
    let mut e = json!({"item": item, "from": from, "day": today});
    if !name.is_empty() {
        e["name"] = json!(name);
    }
    owned_mut(st, girl).push(e);
}

/// A checkout's things: hers when she picked them for herself (her_own), his to carry otherwise; one bought for her
/// that can be given is given to her next (the gift rules). Request: ext, girl, day, lines [{item, for_her, her_own}].
/// Answer: ext, his_add [ids], give_her (id or "").
fn checkout(c: &Catalog, r: &Value) -> Value {
    let mut st = state(r);
    let girl = s(r, "girl");
    let mut his_add = Vec::new();
    let mut give_her = String::new();
    for l in r.get("lines").and_then(Value::as_array).into_iter().flatten() {
        let id = s(l, "item");
        let Some(item) = c.items.iter().find(|i| i.id == id) else { continue };
        if item.consumable {
            continue;
        }
        if l.get("her_own").and_then(Value::as_bool).unwrap_or(false) && !girl.is_empty() {
            give_to_her(&mut st, girl, id, "", "bought", day(r));
            continue;
        }
        his_add.push(json!(id));
        if l.get("for_her").and_then(Value::as_bool).unwrap_or(false) && item.giftable && !girl.is_empty() && give_her.is_empty() {
            give_her = id.to_string();
        }
    }
    json!({"ext": with_state(r, st), "his_add": his_add, "give_her": give_her})
}

/// She took his gift (gift_settle accepted it): it is hers now. Request: ext, girl, day, item. Answer: ext.
fn received(r: &Value) -> Value {
    let mut st = state(r);
    let girl = s(r, "girl");
    if !girl.is_empty() && !s(r, "item").is_empty() {
        let from = if s(r, "from").is_empty() { "gift" } else { s(r, "from") };
        give_to_her(&mut st, girl, s(r, "item"), s(r, "name"), from, day(r));
    }
    json!({"ext": with_state(r, st)})
}

/// What the AI read in their talk, done with the things they really hold. Request: ext, girl, day, his_items [ids],
/// and any of she_gives {item}, she_lends {item, days}, she_returns {item}, he_lends {item, days}, he_returns {item}.
/// Answer: ext, his_add, his_remove, done [{what, item, name}], refused [{what, said}].
fn judge(c: &Catalog, r: &Value) -> Value {
    let mut st = state(r);
    let girl = s(r, "girl").to_string();
    let today = day(r);
    let his_items: Vec<Value> = r.get("his_items").and_then(Value::as_array).cloned().unwrap_or_default().into_iter().map(|v| json!({"item": v})).collect();
    let (mut his_add, mut his_remove, mut done, mut refused) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
    let mut photos_after: Option<Value> = None;
    let said = |k: &str| r.get(k).map(|v| if v.is_string() { v.as_str().unwrap_or_default().to_string() } else { s(v, "item").to_string() }).unwrap_or_default();
    let days = |k: &str| r.get(k).and_then(|v| v.get("days")).and_then(Value::as_i64).filter(|d| *d > 0).unwrap_or(LOAN_DAYS);
    for what in ["she_gives", "she_lends_item", "she_returns_item", "he_lends_item", "he_returns_item"] {
        let words = said(what);
        if words.is_empty() || girl.is_empty() {
            continue;
        }
        let ok = match what {
            "she_gives" | "she_lends_item" => {
                // "Take my umbrella": the folding one she brought today (she_has_umbrella) becomes a thing she holds, so
                // it can go to him; after that she has no umbrella of her own today.
                if is_umbrella(&words) && !says_drawing(&words) && !owned_mut(&mut st, &girl).iter().any(|e| is_umbrella(&format!("{}{}", s(e, "item"), s(e, "name")))) && she_has_umbrella(&with_state(r, st.clone()), &girl) {
                    give_to_her(&mut st, &girl, "cv_umbrella", crate::tr::t("items.folding_umbrella"), "own", today);
                    st["umbrella_out"][girl.as_str()] = json!(today);
                }
                let hers = owned_mut(&mut st, &girl).clone();
                find(c, &hers, &words).map(|k| {
                    let e = owned_mut(&mut st, &girl).remove(k);
                    let item = s(&e, "item").to_string();
                    let name = name_of(c, &item, s(&e, "name"));
                    if what == "she_gives" {
                        his_add.push(json!(item));
                        // Her drawing: in his album from now on.
                        if let Some(id) = item.strip_prefix("drawing:").and_then(|n| n.parse::<i64>().ok()) {
                            let shared = crate::photos::share(&with_state(r, st.clone()), id, "him");
                            photos_after = Some(shared["photos"].clone());
                        }
                    } else {
                        st["his_borrowed"].as_array_mut().unwrap().push(json!({"item": item, "name": s(&e, "name"), "owner": girl, "day": today, "due": today + days(what)}));
                    }
                    (item, name)
                })
            }
            "he_returns_item" => {
                let mine: Vec<Value> = st["his_borrowed"].as_array().unwrap().iter().filter(|b| s(b, "owner") == girl).cloned().collect();
                find(c, &mine, &words).map(|k| {
                    let e = mine[k].clone();
                    let list = st["his_borrowed"].as_array_mut().unwrap();
                    if let Some(at) = list.iter().position(|b| b == &e) {
                        list.remove(at);
                    }
                    let item = s(&e, "item").to_string();
                    give_to_her(&mut st, &girl, &item, s(&e, "name"), "returned", today);
                    let name = name_of(c, &item, s(&e, "name"));
                    (item, name)
                })
            }
            "he_lends_item" => find(c, &his_items, &words).map(|k| {
                let item = s(&his_items[k], "item").to_string();
                his_remove.push(json!(item));
                st["her_borrowed"].as_array_mut().unwrap().push(json!({"item": item, "borrower": girl, "day": today, "due": today + days(what)}));
                let name = name_of(c, &item, "");
                (item, name)
            }),
            _ => {
                // she_returns: what she borrowed from him comes back to his bag.
                let hers: Vec<Value> = st["her_borrowed"].as_array().unwrap().iter().filter(|b| s(b, "borrower") == girl).cloned().collect();
                find(c, &hers, &words).map(|k| {
                    let e = hers[k].clone();
                    let list = st["her_borrowed"].as_array_mut().unwrap();
                    if let Some(at) = list.iter().position(|b| b == &e) {
                        list.remove(at);
                    }
                    let item = s(&e, "item").to_string();
                    his_add.push(json!(item));
                    let name = name_of(c, &item, "");
                    (item, name)
                })
            }
        };
        match ok {
            Some((item, name)) => done.push(json!({"what": what, "item": item, "name": name})),
            None => refused.push(json!({"what": what, "said": words})),
        }
    }
    let notice: Vec<String> = done
        .iter()
        .map(|d| {
            let key = match s(d, "what") {
                "she_gives" => "items.she_gives",
                "she_lends_item" => "items.she_lends",
                "she_returns_item" => "items.she_returns",
                "he_lends_item" => "items.he_lends",
                _ => "items.he_returns",
            };
            crate::tr::m(key, &[("item", crate::tr::a(s(d, "name")))])
        })
        .collect();
    let mut ext = with_state(r, st);
    if let Some(p) = photos_after {
        ext["photos"] = p;
    }
    json!({"ext": ext, "his_add": his_add, "his_remove": his_remove, "done": done, "refused": refused, "notice": notice.join(crate::tr::t("items.separator"))})
}

/// An umbrella, by id or by name in any language.
fn is_umbrella(t: &str) -> bool {
    t.to_lowercase().contains("umbrella") || t.contains('伞') || t.contains('傘')
}

fn holds_umbrella(list: &Value, who: (&str, &str)) -> bool {
    list.as_array().into_iter().flatten().any(|e| s(e, who.0) == who.1 && is_umbrella(&format!("{}{}", s(e, "item"), s(e, "name"))))
}

/// Whether she has an umbrella with her today: one of her things, one she borrowed from him, or (fixed for the day,
/// half the days) a folding one in her bag, unless she already gave or lent that one to him today. Her words and the
/// walk both go by this: she said "I have one" and they walked in the rain without any.
pub fn she_has_umbrella(r: &Value, girl: &str) -> bool {
    let st = state(r);
    let owned = st["owned"].get(girl).and_then(Value::as_array).into_iter().flatten().any(|e| is_umbrella(&format!("{}{}", s(e, "item"), s(e, "name"))));
    owned || holds_umbrella(&st["her_borrowed"], ("borrower", girl)) || st["umbrella_out"].get(girl).and_then(Value::as_i64) != Some(day(r)) && {
        let mut h: u64 = 1469598103934665603;
        for b in girl.bytes().chain(day(r).to_le_bytes()) {
            h = (h ^ b as u64).wrapping_mul(1099511628211);
        }
        h % 100 < 50
    }
}

/// Whether he has one of hers on loan (she lent him her umbrella): it covers him on the walk like one in his bag.
pub fn he_borrowed_umbrella(r: &Value) -> bool {
    state(r)["his_borrowed"].as_array().into_iter().flatten().any(|e| is_umbrella(&format!("{}{}", s(e, "item"), s(e, "name"))))
}

/// For her words: what she owns (latest first, a few), what he borrowed from her and since when, what she borrowed
/// from him. Request: ext, girl, day. Answer: text ("" when there is nothing).
fn facts(c: &Catalog, r: &Value) -> Value {
    let st = state(r);
    let girl = s(r, "girl");
    let today = day(r);
    let mut parts = Vec::new();
    let mut owned: Vec<String> = st["owned"].get(girl).and_then(Value::as_array).into_iter().flatten().rev().take(8).map(|e| name_of(c, s(e, "item"), s(e, "name"))).collect();
    // What she saved up for and bought (bank.rs saving goals leave the flag "goal:<what>" on her relation).
    for f in r.get("relation").and_then(|v| v.get("flags")).and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str) {
        if let Some(what) = f.strip_prefix("goal:").filter(|w| !w.is_empty() && !owned.iter().any(|o| o == w)) {
            owned.push(what.to_string());
        }
    }
    if !owned.is_empty() {
        parts.push(crate::tr::m("items.fact_owned", &[("items", crate::tr::a(&owned.join(crate::tr::t("items.separator"))))]));
    }
    // Raining (the request's raining): whether she brought an umbrella, so what she says matches the walk.
    if r.get("raining").and_then(Value::as_bool) == Some(true) {
        parts.push(crate::tr::t(if she_has_umbrella(r, girl) { "items.fact_her_umbrella" } else { "items.fact_no_umbrella" }).to_string());
    }
    for b in st["his_borrowed"].as_array().into_iter().flatten().filter(|b| s(b, "owner") == girl) {
        let due = b.get("due").and_then(Value::as_i64).unwrap_or(today);
        let key = if today > due { "items.fact_he_borrowed_late" } else { "items.fact_he_borrowed" };
        parts.push(crate::tr::m(
            key,
            &[
                ("item", crate::tr::a(&name_of(c, s(b, "item"), s(b, "name")))),
                ("days", crate::tr::a(&(today - b.get("day").and_then(Value::as_i64).unwrap_or(today)).to_string())),
                ("late", crate::tr::a(&(today - due).max(0).to_string())),
            ],
        ));
    }
    for b in st["her_borrowed"].as_array().into_iter().flatten().filter(|b| s(b, "borrower") == girl) {
        parts.push(crate::tr::m("items.fact_she_borrowed", &[("item", crate::tr::a(&name_of(c, s(b, "item"), "")))]));
    }
    json!({"text": parts.join(crate::tr::t("items.fact_separator"))})
}

/// His bag as the panel shows it: his own things (can be given) and what he borrowed (whose, until when).
/// Request: ext, his_items [ids], day, names {heroine id: name}. Answer: mine [{item, name}], borrowed [{item, name, owner, owner_name, due, late}].
fn view(c: &Catalog, r: &Value) -> Value {
    let st = state(r);
    let today = day(r);
    let names = r.get("names").cloned().unwrap_or(Value::Null);
    let mine: Vec<Value> = r.get("his_items").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str).map(|i| json!({"item": i, "name": name_of(c, i, "")})).collect();
    let borrowed: Vec<Value> = st["his_borrowed"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|b| {
            let due = b.get("due").and_then(Value::as_i64).unwrap_or(today);
            json!({"item": s(b, "item"), "name": name_of(c, s(b, "item"), s(b, "name")), "owner": s(b, "owner"),
                   "owner_name": names.get(s(b, "owner")).and_then(Value::as_str).unwrap_or(s(b, "owner")), "due": due, "late": today > due})
        })
        .collect();
    json!({"mine": mine, "borrowed": borrowed})
}

pub fn eval(c: &Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "items_checkout" => checkout(c, r),
        "items_received" => received(r),
        "items_judge" => judge(c, r),
        "items_facts" => facts(c, r),
        "items_view" => view(c, r),
        _ => return None,
    }))
}

/// For other rules: give her a thing (a saving goal she bought, a drawing she made). ext in, ext out.
pub fn add_owned(ext: &Value, girl: &str, item: &str, name: &str, from: &str, today: i64) -> Value {
    let r = json!({"ext": ext});
    let mut st = state(&r);
    give_to_her(&mut st, girl, item, name, from, today);
    with_state(&r, st)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    fn cat() -> Catalog {
        Tuning::builtin().catalog
    }

    #[test]
    fn checkout_keeps_what_she_picked_as_hers() {
        let c = cat();
        let v = checkout(&c, &json!({"girl": "r", "day": 3, "lines": [
            {"item": "bk_novel_sword", "for_her": true, "her_own": true},
            {"item": "bk_postcard", "for_her": true, "her_own": false},
            {"item": "bk_notebook", "for_her": false},
            {"item": "cv_candy", "for_her": true}
        ]}));
        assert_eq!(v["his_add"], json!(["bk_postcard", "bk_notebook"]));
        assert_eq!(v["give_her"], "bk_postcard");
        assert_eq!(v["ext"]["items"]["owned"]["r"][0]["item"], "bk_novel_sword");
    }

    #[test]
    fn she_lends_him_her_folding_umbrella() {
        let c = cat();
        // A day she has the folding one with her.
        let d = (0..40).find(|d| she_has_umbrella(&json!({"day": d}), "g")).unwrap();
        let lent = judge(&c, &json!({"girl": "g", "day": d, "she_lends_item": {"item": "折叠伞", "days": 2}}));
        let ext = lent["ext"].clone();
        assert_eq!(lent["done"].as_array().unwrap().len(), 1, "{lent}");
        assert!(he_borrowed_umbrella(&json!({"ext": ext})));
        // She has none left today; tomorrow is another day.
        assert!(!she_has_umbrella(&json!({"ext": ext, "day": d}), "g"));
        // Walking in the rain: his borrowed umbrella covers them.
        let walk = crate::weather::eval("walk_style", &json!({"raining": true, "ext": ext, "girl": "g", "day": d, "stage": "lover"})).unwrap().unwrap();
        assert_eq!(walk["style"], "umbrella_shared");
    }

    #[test]
    fn she_lends_her_book_he_returns_it_and_late_is_told() {
        let c = cat();
        let ext = add_owned(&json!({"other": 1}), "r", "bk_novel_sword", "", "bought", 1);
        let lent = judge(&c, &json!({"ext": ext, "girl": "r", "day": 5, "she_lends_item": {"item": "那本劍客小說", "days": 7}}));
        assert_eq!(lent["done"][0]["item"], "bk_novel_sword");
        assert_eq!(lent["ext"]["other"], 1, "the rest of ext is kept");
        assert_eq!(lent["ext"]["items"]["owned"]["r"], json!([]));
        assert_eq!(lent["ext"]["items"]["his_borrowed"][0]["due"], 12);
        let ok = facts(&c, &json!({"ext": lent["ext"], "girl": "r", "day": 8}));
        assert!(!ok["text"].as_str().unwrap().is_empty());
        let late = facts(&c, &json!({"ext": lent["ext"], "girl": "r", "day": 15}))["text"].as_str().unwrap().to_string();
        assert_ne!(late, ok["text"].as_str().unwrap());
        let view = view(&c, &json!({"ext": lent["ext"], "his_items": ["bk_notebook"], "day": 15, "names": {"r": "雷電"}}));
        assert_eq!(view["borrowed"][0]["owner_name"], "雷電");
        assert_eq!(view["borrowed"][0]["late"], true);
        let back = judge(&c, &json!({"ext": lent["ext"], "girl": "r", "day": 9, "he_returns_item": "孤劍行"}));
        assert_eq!(back["done"][0]["what"], "he_returns_item");
        assert_eq!(back["ext"]["items"]["his_borrowed"], json!([]));
        assert_eq!(back["ext"]["items"]["owned"]["r"][0]["item"], "bk_novel_sword");
    }

    #[test]
    fn her_drawing_given_to_him_is_in_his_album() {
        let c = cat();
        let d = crate::photos::eval("drawing_made", &json!({"ext": {}, "girl": "a", "girl_name": "貞德", "day": 3, "minute": 900, "place": "park", "place_name": "中央公園", "file": "d.png"})).unwrap().unwrap();
        assert_eq!(d["item"], "drawing:1");
        let g = judge(&c, &json!({"ext": d["ext"], "girl": "a", "day": 3, "she_gives": {"item": "那張畫"}}));
        assert_eq!(g["his_add"], json!(["drawing:1"]));
        assert_eq!(g["ext"]["photos"][0]["shared_with"], json!(["him"]));
        assert!(!g["notice"].as_str().unwrap().contains("drawing:"), "said by its name");
        // The drawing of him under the umbrella, with her umbrella in her hands too: the drawing (R3-262).
        let mut ext = d["ext"].clone();
        ext["items"]["owned"]["a"].as_array_mut().unwrap().push(json!({"item": "cv_umbrella", "from": "own", "day": 3}));
        let u = judge(&c, &json!({"ext": ext, "girl": "a", "day": 3, "she_gives": {"item": "画着撑伞的你的那张画"}}));
        assert_eq!(u["his_add"], json!(["drawing:1"]), "{u}");
    }

    #[test]
    fn nothing_she_does_not_hold_is_given() {
        let c = cat();
        let v = judge(&c, &json!({"ext": {}, "girl": "r", "day": 1, "she_gives": {"item": "劍客小說"}}));
        assert_eq!(v["done"], json!([]));
        assert_eq!(v["refused"][0]["what"], "she_gives");
        assert_eq!(v["his_add"], json!([]));
    }

    #[test]
    fn he_lends_from_his_bag_and_she_brings_it_back() {
        let c = cat();
        let v = judge(&c, &json!({"ext": {}, "girl": "k", "day": 1, "his_items": ["cv_umbrella"], "he_lends_item": {"item": "雨傘"}}));
        assert_eq!(v["his_remove"], json!(["cv_umbrella"]));
        let back = judge(&c, &json!({"ext": v["ext"], "girl": "k", "day": 2, "she_returns_item": "傘"}));
        assert_eq!(back["his_add"], json!(["cv_umbrella"]));
        assert_eq!(back["ext"]["items"]["her_borrowed"], json!([]));
    }
}
