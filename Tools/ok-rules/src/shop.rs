//! Plan §8: money between him and a heroine and what activities leave behind. Who pays at a checkout (her leaning and
//! the split, her shortfall fronted by him, her taste remembered), gifts, lending her money, an activity together ending
//! (its time, price, who pays, what it does to her and the memory), her errands, two friends meeting, his part-time job
//! (the manager's decision, the pay and rating), and the time alone in a private room. Unreal moves the money through the
//! books and plays the scenes.
use crate::catalog::{checkout_lean, gift_score, pick_meal, profile_int, taste_of, Catalog};
use crate::effects::Effects;
use crate::relation::Relation;
use crate::rng::RandomStream;
use serde::Deserialize;
use serde_json::{json, Map, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or("")
}
fn n(r: &Value, k: &str) -> f64 {
    r.get(k).and_then(Value::as_f64).unwrap_or(0.0)
}
fn b(r: &Value, k: &str) -> bool {
    r.get(k).and_then(Value::as_bool).unwrap_or(false)
}
fn rel_of(v: Option<&Value>) -> Result<Option<Relation>, String> {
    match v.filter(|v| v.is_object()) {
        Some(v) => serde_json::from_value(v.clone()).map(Some).map_err(|e| format!("relation: {e}")),
        None => Ok(None),
    }
}
fn profile(v: Option<&Value>) -> Value {
    v.cloned().filter(Value::is_object).unwrap_or_else(|| json!({}))
}

// ------------------------------------------------------------------------------------------------ checkout

/// Her leaning at a checkout. Request: total, relation, profile, promise (she said this one is on her and it holds).
fn lean(r: &Value) -> Result<Value, String> {
    let rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let p = profile(r.get("profile"));
    let total = n(r, "total") as i32;
    let (mut lean, mut insist) = checkout_lean(&p, rel.cash, total, rel.treat_balance, &rel.stage);
    // She cannot pay what she would offer to: she does not offer.
    if rel.cash >= 0 && rel.cash < total as i64 && lean == "she_pays" {
        lean = "player_pays";
        insist = insist.min(0.3);
    }
    if b(r, "promise") && rel.cash >= total as i64 {
        lean = "she_pays";
        insist = 1.0;
    }
    Ok(json!({"lean": lean, "insist": insist}))
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct Line {
    item: String,
    size: String,
    sweetness: String,
    ice: String,
    for_her: bool,
    /// Another girl at the table ordered it (her id): hers to eat, his to pay.
    girl: String,
}

/// Paying the cart. Request: option (player_pays | split | she_pays), cart [line], service (%), player_cash, place
/// (id), place_name, her_id, relation and profile (when she is at the table), names {girl id: name}, as_girl (at a group
/// table each girl's dishes are settled on their own: the lines of this girl count as hers). Answer: total,
/// player_pays, her_pays, advanced, said, card (he must pay by card), effects, notice, tell_her.
fn checkout(c: &Catalog, r: &Value) -> Result<Value, String> {
    let option = s(r, "option");
    let cart: Vec<Line> = serde_json::from_value(r.get("cart").cloned().unwrap_or(json!([]))).map_err(|e| format!("cart: {e}"))?;
    let service = n(r, "service") as i32;
    let place = s(r, "place");
    let place_name = s(r, "place_name");
    let p = profile(r.get("profile"));
    let mut her = rel_of(r.get("relation"))?;
    let before = her.clone();
    let price = |l: &Line| c.find(&l.item).map(|i| i.price_of(&l.size)).unwrap_or(0);
    let subtotal: i32 = cart.iter().map(price).sum();
    let total = subtotal + subtotal * service / 100;
    let as_girl = s(r, "as_girl");
    let hers = |l: &Line| (l.for_her && l.girl.is_empty()) || (!as_girl.is_empty() && l.girl == as_girl);
    let mut her_share: i32 = cart.iter().filter(|l| hers(l)).map(price).sum();
    her_share += her_share * service / 100;
    // Who pays what; she meant to pay but cannot: he fronts it (hers to give back), and the line says so.
    let (mut player_pays, mut her_pays, mut advanced) = (total as i64, 0i64, 0i64);
    let mut said = String::new();
    if let Some(rel) = her.as_ref() {
        // She pays with cash or her card (bank account).
        let cash = rel.funds();
        let part = if option == "she_pays" { total as i64 } else if option == "split" { her_share as i64 } else { 0 };
        if part > 0 {
            if cash >= part {
                player_pays = total as i64 - part;
                her_pays = part;
            } else if rel.cash >= 0 {
                advanced = part;
                said = if option == "she_pays" { crate::tr::m("shop.she_short_you_covered", &[("amount", crate::tr::a(&crate::tr::money(advanced)))]) } else { crate::tr::m("shop.she_short_you_covered_her", &[("amount", crate::tr::a(&crate::tr::money(advanced)))]) };
            } else {
                said = crate::tr::t("shop.she_short_you_pay").into();
            }
        }
    }
    let card = n(r, "player_cash") < player_pays as f64;
    if card {
        said += crate::tr::t("shop.card_payment_label");
    }
    let mut fx = Effects::default();
    let mut remembered = Vec::new();
    let mut disliked = Vec::new();
    let names: Vec<String> = cart.iter().filter_map(|l| c.find(&l.item).map(|i| i.name.clone())).collect();
    // A novel and a pen from the bookshop were bought, not eaten ("he treated me to eating a novel").
    let ate = cart.iter().any(|l| c.find(&l.item).is_some_and(|i| i.consumable));
    if let Some(rel) = her.as_mut() {
        if rel.cash >= 0 {
            rel.pay(her_pays);
        }
        if advanced > 0 {
            rel.owes_player += advanced;
            fx.post("private", "lent", advanced, "lend", crate::tr::t("shop.checkout_advance_label"));
        }
        // Treat balance counts favours, not money: he covered her part (+1), or she covered his (-1).
        if advanced == 0 {
            if her_share > 0 && her_pays == 0 {
                rel.treat_balance += 1;
            } else if her_pays > her_share as i64 {
                rel.treat_balance -= 1;
            }
        }
        // She saw him spend.
        rel.perceived_wealth = (rel.perceived_wealth + (player_pays as f32 / crate::region::amount("goods", 64) as f32).min(10.0)).clamp(0.0, 100.0);
        // Her order against her tastes: exactly right is remembered.
        for l in cart.iter().filter(|l| hers(l)) {
            let Some(i) = c.find(&l.item) else { continue };
            match taste_of(&p, i, &l.sweetness, &l.ice) {
                2 => {
                    remembered.push(if i.options.is_empty() { i.name.clone() } else { format!("{}{}{}", l.sweetness, l.ice, i.name) });
                    rel.add_liking(3.0);
                    rel.trust = (rel.trust + 2.0).min(100.0);
                }
                -1 => disliked.push(i.name.clone()),
                _ => {}
            }
        }
        fx.relation(rel, before.as_ref().unwrap());
        // Only his own order, paid by him (or only hers, paid by her): nobody treated anybody.
        let his_share: i32 = cart.iter().filter(|l| !l.for_her && l.girl.is_empty()).map(price).sum();
        let who = if advanced > 0 {
            crate::tr::t("shop.he_advanced_me_note")
        } else if her_share == 0 && her_pays == 0 {
            crate::tr::t("shop.he_own_order")
        } else if his_share == 0 && player_pays == 0 {
            crate::tr::t("shop.i_own_order")
        } else if her_pays == 0 {
            crate::tr::t("shop.he_treats_me")
        } else if player_pays == 0 {
            crate::tr::t("shop.i_treat_him")
        } else {
            crate::tr::t("shop.we_split_separately")
        };
        fx.note_rumor(&rel.id, "treat", &crate::tr::m("shop.group_meal_recap", &[("who", crate::tr::a(&who)), ("place_name", crate::tr::a(&place_name)), ("dishes", crate::tr::a(&(names.join(crate::tr::t("bank.list_separator"))))), ("total", crate::tr::a(&crate::tr::money(total))), ("eat", crate::tr::a(if ate { "yes" } else { "no" }))]), place, -1.0, place_name);
        if !remembered.is_empty() {
            fx.note_at(&rel.id, "taste", &crate::tr::m("shop.he_remembers_likes", &[("likes", crate::tr::a(&(remembered.join(crate::tr::t("bank.list_separator")))))]), place, 0.7);
        }
        if !disliked.is_empty() {
            fx.note_at(&rel.id, "treat_disliked", &crate::tr::m("shop.ordered_disliked", &[("dishes", crate::tr::a(&(disliked.join(crate::tr::t("bank.list_separator")))))]), place, 0.4);
        }
        if total as i64 >= crate::region::amount("cash", 200) {
            fx.rumor("", "wealth", &crate::tr::m("shop.he_spent_at_once", &[("place_name", crate::tr::a(&place_name)), ("total", crate::tr::a(&crate::tr::money(total)))]), 60.0, false);
        }
    }
    // The others at the table remember being treated.
    let mut others: Vec<(String, Vec<String>)> = Vec::new();
    for l in cart.iter().filter(|l| !l.girl.is_empty()) {
        let Some(i) = c.find(&l.item) else { continue };
        match others.iter_mut().find(|o| o.0 == l.girl) {
            Some(o) => o.1.push(i.name.clone()),
            None => others.push((l.girl.clone(), vec![i.name.clone()])),
        }
    }
    for (girl, items) in others {
        fx.note_rumor(&girl, "treat", &crate::tr::m("shop.group_dinner_treat", &[("place_name", crate::tr::a(&place_name)), ("items", crate::tr::a(&(items.join(crate::tr::t("bank.list_separator")))))]), place, -1.0, place_name);
    }
    let mut notice = crate::tr::m("shop.checkout_summary", &[("total", crate::tr::a(&crate::tr::money(total))), ("player_pays", crate::tr::a(&player_pays)), ("her_pays", crate::tr::a(&her_pays)), ("said", crate::tr::a(&said))]);
    if !remembered.is_empty() {
        notice += crate::tr::t("shop.she_notices_taste");
    }
    let tell_her = if her.is_some() {
        // How she answers depends on who paid: thanks only when he paid for her.
        let how = if her_share == 0 && her_pays == 0 {
            crate::tr::t("shop.his_own_purchase_note")
        } else if player_pays == 0 && her_pays > i64::from(her_share) {
            crate::tr::t("shop.she_treated_him_note")
        } else if player_pays == 0 {
            crate::tr::t("shop.self_paid_no_treat_note")
        } else if her_pays == 0 && advanced == 0 {
            crate::tr::t("shop.he_treats_you_prompt")
        } else {
            crate::tr::t("shop.split_each_note")
        };
        crate::tr::m("shop.checkout_recap_note", &[("total", crate::tr::a(&crate::tr::money(total))), ("he_pays", crate::tr::a(&crate::tr::money(player_pays))), ("you_pay", crate::tr::a(&crate::tr::money(her_pays))), ("advance_note", crate::tr::a(&(if advanced > 0 { crate::tr::m("shop.you_short_he_advanced", &[("amount", crate::tr::a(&crate::tr::money(advanced)))]) } else { String::new() }))), ("how", crate::tr::a(&how))])
    } else {
        String::new()
    };
    Ok(json!({"total": total, "player_pays": player_pays, "her_pays": her_pays, "advanced": advanced, "card": card, "effects": fx.into_value(), "notice": notice, "tell_her": tell_her, "treat_kept": her_pays > 0 || advanced > 0}))
}

// ------------------------------------------------------------------------------------------------ gifts and loans

/// Offering a gift: how she leans. Request: item, relation, profile, birthday. Answer: score, lean, insist.
fn gift_offer(c: &Catalog, r: &Value) -> Result<Value, String> {
    let rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let item = c.find(s(r, "item")).ok_or("unknown item")?;
    // A gift on her birthday counts for more.
    let score = gift_score(&profile(r.get("profile")), item, item.price_of(&item.default_size()), &rel.stage, rel.greed) + if b(r, "birthday") { 0.5 } else { 0.0 };
    Ok(json!({"score": score, "lean": if score >= 0.0 { "accept" } else { "decline" }, "insist": score.abs().clamp(0.0, 1.0)}))
}

/// Her answer to a gift. Request: item, option, score, relation. Answer: accepted, effects.
fn gift_settle(c: &Catalog, r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let before = rel.clone();
    let item = c.find(s(r, "item")).ok_or("unknown item")?;
    let price = item.price_of(&item.default_size());
    let score = n(r, "score") as f32;
    let mut fx = Effects::default();
    let accepted = s(r, "option") == "accept";
    if accepted {
        rel.add_liking(2.0 + score.max(0.0) * 4.0);
        rel.attraction = (rel.attraction + 1.0).clamp(0.0, 100.0);
        rel.perceived_wealth = (rel.perceived_wealth + (price as f32 / crate::region::amount("goods", 85) as f32).min(15.0)).clamp(0.0, 100.0);
        if item.tags.iter().any(|t| t == "luxury") {
            rel.lifestyle_desire = (rel.lifestyle_desire + 3.0).clamp(0.0, 100.0);
        }
        fx.relation(&rel, &before);
        fx.note_rumor(&rel.id, "gift", &crate::tr::m("shop.he_gave_gift", &[("item_name", crate::tr::a(&item.name)), ("price", crate::tr::a(&crate::tr::money(price)))]), "", (0.5 + score * 0.3).clamp(0.3, 0.9), &item.name);
    } else {
        fx.note_at(&rel.id, "gift_declined", &crate::tr::m("shop.he_offered_gift_declined", &[("item_name", crate::tr::a(&item.name))]), "", 0.5);
    }
    Ok(json!({"accepted": accepted, "effects": fx.into_value(), "notice": if accepted { crate::tr::t("shop.she_accepted") } else { crate::tr::t("shop.she_declined") }}))
}

/// Offering to lend her money: she accepts when she needs it, pride may refuse. Request: amount, relation, profile.
fn loan_offer(r: &Value) -> Result<Value, String> {
    let rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let p = profile(r.get("profile"));
    let amount = n(r, "amount") as i64;
    let needs = p.get("money").and_then(|m| m.get("money_needs")).and_then(Value::as_array).map(|a| !a.is_empty()).unwrap_or(false);
    let needy = (rel.cash >= 0 && rel.cash < amount) || needs;
    let pride = profile_int(&p, "money", "treat_pride", 50);
    Ok(json!({"lean": if needy && rel.trust >= 30.0 { "accept" } else { "decline" }, "insist": if needy { 0.5 } else { pride as f32 / 100.0 }}))
}

/// She took the loan. Request: amount, relation, name. Answer: effects (his cash, the book, her wallet and feelings).
fn loan_settle(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let before = rel.clone();
    let amount = n(r, "amount") as i64;
    let name = s(r, "name");
    let mut fx = Effects::default();
    fx.cash(-amount, "lend", &crate::tr::m("shop.lend_money_to", &[("name", crate::tr::a(&name))]));
    fx.post("private", "lent", amount, "lend", &crate::tr::m("shop.lend_money_to", &[("name", crate::tr::a(&name))]));
    rel.cash = rel.cash.max(0) + amount;
    rel.owes_player += amount;
    rel.trust = (rel.trust + 2.0).min(100.0);
    rel.dependency = (rel.dependency + 2.0).min(100.0);
    fx.relation(&rel, &before);
    fx.note_at(&rel.id, "loan", &crate::tr::m("shop.borrowed_from_him", &[("amount", crate::tr::a(&crate::tr::money(amount)))]), "", 0.6);
    Ok(json!({"effects": fx.into_value()}))
}

/// He hands her money that is hers to keep (paying her back, covering what she paid for him). Request: amount,
/// relation, name. Answer: effects (his cash, her wallet, her note that it is not a loan).
fn give_money_settle(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let before = rel.clone();
    let amount = n(r, "amount") as i64;
    if amount <= 0 {
        return Err("amount must be positive".into());
    }
    let name = s(r, "name");
    let mut fx = Effects::default();
    fx.cash(-amount, "gift", &crate::tr::m("shop.give_money_to", &[("name", crate::tr::a(&name))]));
    rel.cash = rel.cash.max(0) + amount;
    fx.relation(&rel, &before);
    fx.note_at(&rel.id, "money", &crate::tr::m("shop.got_money_from_him", &[("amount", crate::tr::a(&crate::tr::money(amount)))]), "", 0.5);
    Ok(json!({"effects": fx.into_value()}))
}

// ------------------------------------------------------------------------------------------------ activities

/// Fixed prices of what is not on the menu (base yuan for both; region::price at use), how long it takes (game minutes), what it does to her.
const COSTS: [(&str, i32, i32, &str, f32); 12] = [
    ("onsen", 80, 60, "hygiene", 30.0),
    ("pool", 60, 45, "fun", 20.0),
    ("hotel_spa", 240, 90, "energy", 25.0),
    ("hotel_rest", 320, 120, "energy", 40.0),
    ("inn_rest", 240, 120, "energy", 40.0),
    ("restaurant_room", 160, 70, "satiety", 40.0),
    ("aquarium", 60, 40, "fun", 20.0),
    ("vending", 6, 10, "satiety", 5.0),
    ("splash", 0, 25, "fun", 20.0),
    ("watermelon", 20, 30, "fun", 20.0),
    ("pet_photo", 0, 15, "fun", 15.0),
    ("shrine_pray", 10, 15, "fun", 5.0),
];

/// How long an activity takes (game minutes). kind: pair | photo | solo | facility | "" (not in the table).
pub fn activity_minutes(id: &str, kind: &str, code: i32) -> i32 {
    if let Some(c) = COSTS.iter().find(|c| id.starts_with(c.0)) {
        return c.2;
    }
    match kind {
        "" => match id {
            "photo" => 10,
            "phone_together" => 20,
            _ => 30,
        },
        "photo" => 10,
        "solo" => 20,
        "facility" => 60,
        _ => match code {
            5 => 50,
            2 => 25,
            8 => 20,
            11 => 25,
            _ => 35,
        },
    }
}

/// An activity together ended: what it cost and who paid, what it did to her, the memory; less of everything for the
/// same thing again the same day. Request: id, kind, code, name, place, month, again, menu (a meal or drink at the
/// counter not paid yet), player_cash, relation, profile, name_her, seed. Answer: minutes, price, payer, feed {need:
/// amount}, effects, notice.
fn activity_end(c: &Catalog, r: &Value) -> Result<Value, String> {
    let id = s(r, "id");
    let (kind, code) = (s(r, "kind"), n(r, "code") as i32);
    let name = s(r, "name");
    let place = s(r, "place");
    let again = n(r, "again") as i32;
    let p = profile(r.get("profile"));
    let mut rng = RandomStream::new(n(r, "seed") as i32);
    // Cut short (the place closed on them): only the minutes that went by.
    let minutes = match r.get("spent").and_then(Value::as_f64) { Some(m) => m.max(0.0) as i32, None => activity_minutes(id, kind, code) };
    // Money: the menu for a meal or a drink (one for each), else a fixed fee.
    let mut price = 0;
    let mut bought = String::new();
    let mut feed: Map<String, Value> = Map::new();
    let add = |feed: &mut Map<String, Value>, k: &str, v: f32| {
        let now = feed.get(k).and_then(Value::as_f64).unwrap_or(0.0) as f32;
        feed.insert(k.into(), json!(now + v));
    };
    if b(r, "menu") {
        let month = n(r, "month") as i32;
        let mut pick_rng = RandomStream::new(n(r, "menu_seed") as i32);
        if let Some(i) = pick_meal(c, place, month, &p, crate::region::amount("goods", 80) as i32, &mut pick_rng) {
            price += i.price_of(&i.default_size());
            bought = i.name.clone();
            for (k, v) in &i.effects {
                add(&mut feed, k, *v);
            }
        }
        if let Some(i) = pick_meal(c, place, month, &Value::Null, crate::region::amount("goods", 80) as i32, &mut pick_rng) {
            price += i.price_of(&i.default_size());
        }
    }
    // An admission (the aquarium, the pool, the onsen) is paid once a day there: watching the tank and then the bench
    // in front of it charged the ticket twice. Request: admitted (already paid at this place today).
    let mut admission = false;
    for cst in COSTS.iter().filter(|cst| id.starts_with(cst.0)) {
        let ticket = matches!(cst.0, "aquarium" | "pool" | "onsen");
        if !(ticket && b(r, "admitted")) {
            price += crate::region::price(cst.1 as i64) as i32;
            admission |= ticket;
        }
        add(&mut feed, cst.3, cst.4);
    }
    if feed.is_empty() {
        if kind == "photo" {
            add(&mut feed, "fun", 8.0);
        } else {
            add(&mut feed, "social", 10.0);
        }
    }
    for v in feed.values_mut() {
        *v = json!(v.as_f64().unwrap_or(0.0) as f32 / (1 + again) as f32);
    }
    let mut fx = Effects::default();
    let mut payer = String::new();
    let mut treat_kept = false;
    if let Some(mut rel) = rel_of(r.get("relation"))? {
        let before = rel.clone();
        if price > 0 {
            let (lean, insist) = {
                let (mut l, mut i) = checkout_lean(&p, rel.cash, price, rel.treat_balance, &rel.stage);
                if rel.cash >= 0 && rel.cash < price as i64 && l == "she_pays" {
                    l = "player_pays";
                    i = i.min(0.3);
                }
                if b(r, "promise") && rel.cash >= price as i64 {
                    l = "she_pays";
                    i = 1.0;
                }
                // What they settled when they set off ("门票各付各的", "我请"): the AI's reading of it.
                if let Some(o) = agreed(r) {
                    if o != "player_pays" || n(r, "player_cash") >= price as f64 {
                        l = o;
                        i = 1.0;
                    }
                }
                (l, i)
            };
            let why = crate::tr::m("shop.activity_with_her", &[("name", crate::tr::a(&(s(r, "name_her")))), ("bought", crate::tr::a(&(if bought.is_empty() { String::new() } else { crate::tr::m("shop.colon_items", &[("bought", crate::tr::a(&bought))]) })))]);
            let his = n(r, "player_cash") as i64;
            let price64 = price as i64;
            if lean == "she_pays" && rng.frand() < insist && rel.cash >= price64 {
                rel.cash -= price64;
                rel.treat_balance -= 1;
                payer = crate::tr::t("shop.she_treats").into();
                treat_kept = true;
            } else if lean == "split" && rng.frand() < insist && rel.cash >= price64 / 2 && his >= price64 - price64 / 2 {
                rel.cash -= price64 / 2;
                fx.cash(-(price64 - price64 / 2), "treat", &crate::tr::m("shop.split_half_label", &[("why", crate::tr::a(&why))]));
                payer = crate::tr::t("shop.split_half").into();
            } else if his >= price64 {
                fx.cash(-price64, "treat", &crate::tr::m("shop.you_treat_label", &[("why", crate::tr::a(&why))]));
                rel.treat_balance += 1;
                payer = crate::tr::t("shop.you_treat").into();
            } else if rel.funds() >= price64 {
                rel.pay(price64);
                payer = crate::tr::t("shop.short_she_paid").into();
                rel.respect = (rel.respect - 2.0).max(0.0);
            } else {
                // Neither can pay it all: he pays what he has, she covers the rest on credit and thinks less of him.
                let mine = his.max(0);
                fx.cash(-mine, "treat", &crate::tr::m("shop.all_cash_label", &[("why", crate::tr::a(&why))]));
                rel.debt += price64 - mine;
                rel.respect = (rel.respect - 3.0).max(0.0);
                payer = crate::tr::t("shop.short_she_advanced").into();
            }
        }
        // Feelings: a little closer, less for the same thing again.
        rel.add_liking(2.0 / (1 + again) as f32);
        rel.social = (rel.social + 5.0 / (1 + again) as f32).min(100.0);
        fx.relation(&rel, &before);
        let mut what = crate::tr::m("shop.with_him", &[("name", crate::tr::a(&name))]);
        if !bought.is_empty() {
            what += &crate::tr::m("shop.you_ate_suffix", &[("bought", crate::tr::a(&bought))]);
        }
        if price > 0 {
            what += &crate::tr::m("shop.price_payer_paren", &[("price", crate::tr::a(&crate::tr::money(price))), ("payer", crate::tr::a(&payer))]);
        }
        fx.note_at(&rel.id, "activity", &what, place, (0.55 / (1 + again) as f32).clamp(0.15, 0.6));
    }
    // Said as long as it really went on: they sat 50 minutes, not the activity's 35 (elapsed: the minutes that went by).
    let shown = minutes.max(r.get("elapsed").and_then(Value::as_f64).unwrap_or(0.0).round() as i32);
    let notice = crate::tr::m("shop.date_ended_summary", &[("name", crate::tr::a(&name)), ("minutes", crate::tr::a(&shown)), ("payment_note", crate::tr::a(&(if price > 0 { crate::tr::m("shop.price_payer_suffix", &[("price", crate::tr::a(&crate::tr::money(price))), ("payer", crate::tr::a(&payer))]) } else { String::new() })))]);
    Ok(json!({"minutes": minutes, "price": price, "payer": payer, "bought": bought, "feed": feed, "effects": fx.into_value(), "notice": notice, "treat_kept": treat_kept, "admission": admission}))
}

/// Her errand buys something there within the budget and her wallet. Request: place, month, budget, cash, seed.
fn errand(c: &Catalog, r: &Value) -> Result<Value, String> {
    let cash = n(r, "cash") as i64;
    let budget = (n(r, "budget") as i32).max(crate::region::amount("goods", 20) as i32);
    let options: Vec<_> = c.items_at(s(r, "place"), n(r, "month") as i32).into_iter().filter(|i| {
        let p = i.price_of(&i.default_size());
        p <= budget && (cash < 0 || p as i64 <= cash)
    }).collect();
    if options.is_empty() {
        return Ok(json!({}));
    }
    let i = options[RandomStream::new(n(r, "seed") as i32).rand_range(0, options.len() as i32 - 1) as usize];
    Ok(json!({"item": i.id, "name": i.name, "price": i.price_of(&i.default_size())}))
}

/// Two friends met as planned: their bond (22 for a first meeting, else +2). Request: heat (or absent).
fn friends_met(r: &Value) -> Result<Value, String> {
    Ok(json!({"heat": match r.get("heat").and_then(Value::as_f64) { Some(h) => crate::social::bond_heat_add(h as f32, 2.0), None => 22.0 }}))
}

/// The manager (only a dialog box) decides on his application: his look and how he did here before; with nobody to
/// spare, no now and then. Request: look, rating, seed.
fn job_apply(r: &Value) -> Result<Value, String> {
    let rating = r.get("rating").and_then(Value::as_f64).unwrap_or(50.0) as f32;
    let chance = (0.2 + n(r, "look") as f32 / 125.0 + (rating - 50.0) / 200.0).clamp(0.1, 0.95);
    Ok(json!({"yes": RandomStream::new(n(r, "seed") as i32).frand() < chance}))
}

/// A shift ended. Request: wage, worked (minutes), early, chats, rating. Answer: pay, rating, fired.
fn shift_end(r: &Value) -> Result<Value, String> {
    let worked = n(r, "worked") as i64;
    let pay = n(r, "wage") as i64 * worked / 60;
    let rating = (n(r, "rating") as i32 + if b(r, "early") { -10 } else { 3 } - (n(r, "chats") as i32 - 3).max(0)).clamp(0, 100);
    Ok(json!({"pay": pay, "rating": rating, "fired": rating < 15}))
}

/// Back from the private room: she remembers it, closer. Request: relation, place.
fn intimacy_after(r: &Value) -> Result<Value, String> {
    let mut rel = rel_of(r.get("relation"))?.ok_or("missing_field: relation")?;
    let before = rel.clone();
    rel.add_liking(5.0);
    rel.attraction = (rel.attraction + 5.0).min(100.0);
    rel.dependency = (rel.dependency + 3.0).min(100.0);
    let mut fx = Effects::default();
    fx.note_at(&rel.id, "intimacy", crate::tr::t("shop.private_room_time"), s(r, "place"), 0.9);
    fx.relation(&rel, &before);
    Ok(json!({"effects": fx.into_value()}))
}

/// Who pays for what she ordered in conversation. Request: agreed (what the AI read they settled: "him" | "her" |
/// "split" | ""), promise (she promised to treat him), lean and insist (her own inclination), has_hers.
/// Answer: option (player_pays | split | she_pays | choice). Who offered what is the AI's reading, not words looked up.
pub fn chat_order_payer(r: &Value) -> Value {
    let lean = r.get("lean").and_then(Value::as_str).unwrap_or("player_pays");
    let insist = r.get("insist").and_then(Value::as_f64).unwrap_or(0.0);
    let promise = r.get("promise").and_then(Value::as_bool).unwrap_or(false);
    let option = match agreed(r) {
        Some(o) => o,
        None if promise => "she_pays",
        None if lean == "she_pays" && insist > 0.6 => "choice",
        None if lean == "split" || lean == "she_pays" => lean,
        // She expects him to pay for what she ordered, but nothing was settled: his to decide (the checkout choice).
        None if r.get("has_hers").and_then(Value::as_bool).unwrap_or(false) => "choice",
        None => "player_pays",
    };
    json!({"option": option})
}

/// What they settled about paying, as a checkout option: request "agreed" from the AI (her reply's payer, the judge's).
fn agreed(r: &Value) -> Option<&'static str> {
    match r.get("agreed").and_then(Value::as_str).unwrap_or("") {
        "him" => Some("player_pays"),
        "her" => Some("she_pays"),
        "split" => Some("split"),
        _ => None,
    }
}

/// The bill's choice ran out with no button pressed: what they settled decides; nothing settled, nobody treats anybody
/// when she only expected him to pay (each pays their own).
pub fn checkout_timeout(r: &Value) -> Value {
    let lean = r.get("lean").and_then(Value::as_str).unwrap_or("");
    let option = agreed(r).unwrap_or(if lean == "player_pays" || lean.is_empty() { "split" } else { lean });
    json!({"option": option})
}

pub fn eval(c: &Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "checkout_timeout" => Ok(checkout_timeout(r)),
        "checkout_lean" => lean(r),
        "chat_order_payer" => Ok(chat_order_payer(r)),
        "checkout" => checkout(c, r),
        "gift_offer" => gift_offer(c, r),
        "gift_settle" => gift_settle(c, r),
        "loan_offer" => loan_offer(r),
        "loan_settle" => loan_settle(r),
        "give_money_settle" => give_money_settle(r),
        "activity_end" => activity_end(c, r),
        // How long it takes (game minutes): a solo activity (praying, watching the fish) ends on its own after this.
        "activity_minutes" => Ok(json!({"minutes": activity_minutes(s(r, "id"), s(r, "kind"), n(r, "code") as i32)})),
        "errand" => errand(c, r),
        "friends_met" => friends_met(r),
        "job_apply" => job_apply(r),
        "shift_end" => shift_end(r),
        "intimacy_after" => intimacy_after(r),
        _ => return None,
    })
}





#[cfg(test)]
mod tests {


    use super::*;
    use crate::tuning::Tuning;

    fn rel(v: Value) -> Value {
        let mut o = json!({"id": "a"});
        for (k, x) in v.as_object().unwrap() {
            o[k] = x.clone();
        }
        o
    }

    #[test]
    fn given_money_is_hers() {
        let v = give_money_settle(&json!({"amount": 7, "relation": rel(json!({"cash": 195})), "name": "惠惠"})).unwrap();
        let fx = v["effects"].as_array().unwrap();
        assert!(fx.iter().any(|e| e["op"] == "cash" && e["amount"].as_i64() == Some(-7)), "{fx:?}");
        assert!(!fx.iter().any(|e| e["op"] == "post"), "not a loan");
        assert!(give_money_settle(&json!({"amount": 0, "relation": rel(json!({}))})).is_err());
    }
    #[test]
    fn who_pays() {
        let c = Tuning::builtin().catalog;
        let item = c.items.iter().find(|i| i.consumable).unwrap().clone();
        let price = item.price_of(&item.default_size());
        let cart = json!([{"item": item.id, "for_her": true}, {"item": item.id}]);
        let v = checkout(&c, &json!({"option": "split", "cart": cart, "player_cash": 10000, "relation": rel(json!({"cash": 1000})), "profile": {}})).unwrap();
        assert_eq!((v["player_pays"].as_i64(), v["her_pays"].as_i64()), (Some(price as i64), Some(price as i64)));
        let v = checkout(&c, &json!({"option": "she_pays", "cart": cart, "player_cash": 10000, "relation": rel(json!({"cash": 1})), "profile": {}})).unwrap();
        assert_eq!(v["advanced"].as_i64(), Some(2 * price as i64), "he fronts it");
        assert!(v["effects"].as_array().unwrap().iter().any(|e| e["op"] == "post" && e["account"] == "lent"));
        let v = checkout(&c, &json!({"option": "player_pays", "cart": cart, "player_cash": 0, "relation": null})).unwrap();
        assert_eq!(v["card"], true);
        // At a group table another girl's dishes, settled as hers: split means she pays them.
        let group = json!([{"item": item.id, "girl": "g2"}]);
        let v = checkout(&c, &json!({"option": "split", "cart": group, "player_cash": 10000, "relation": rel(json!({"cash": 1000})), "profile": {}, "as_girl": "g2"})).unwrap();
        assert_eq!((v["player_pays"].as_i64(), v["her_pays"].as_i64()), (Some(0), Some(price as i64)));
        let v = checkout(&c, &json!({"option": "split", "cart": group, "player_cash": 10000, "relation": rel(json!({"cash": 1000})), "profile": {}})).unwrap();
        assert_eq!(v["player_pays"].as_i64(), Some(price as i64), "without as_girl another girl's dish is his to pay");
    }

    #[test]
    fn activities_and_shifts() {
        assert_eq!(activity_minutes("onsen_a", "facility", 0), 60);
        assert_eq!(activity_minutes("x", "pair", 5), 50);
        let c = Tuning::builtin().catalog;
        let v = activity_end(&c, &json!({"id": "onsen", "kind": "facility", "name": "泡湯", "again": 1, "player_cash": 1000, "relation": rel(json!({"cash": 0, "affection": 10})), "profile": {}, "seed": 1})).unwrap();
        assert_eq!((v["price"].as_i64(), v["payer"].as_str()), (Some(80), Some("你請客")));
        assert_eq!(v["feed"]["hygiene"], 15.0, "half again the same day");
        assert_eq!(v["admission"], true);
        // Watching the tank and then sitting on the bench before it: one ticket for the visit.
        let tank = activity_end(&c, &json!({"id": "aquarium_observe", "kind": "solo", "name": "看魚", "player_cash": 1000, "relation": rel(json!({"cash": 0})), "profile": {}, "seed": 1})).unwrap();
        assert_eq!((tank["price"].as_i64(), tank["admission"].as_bool()), (Some(60), Some(true)));
        let bench = activity_end(&c, &json!({"id": "aquarium_bench", "kind": "pair", "code": 11, "name": "長椅", "admitted": true, "player_cash": 1000, "relation": rel(json!({"cash": 0})), "profile": {}, "seed": 1})).unwrap();
        assert_eq!((bench["price"].as_i64(), bench["admission"].as_bool()), (Some(0), Some(false)), "{bench}");
        // They settled that he pays (the AI's reading): his, however rich she is and whatever the dice say.
        for seed in 1..20 {
            let v = activity_end(&c, &json!({"id": "onsen", "kind": "facility", "name": "泡湯", "player_cash": 1000, "agreed": "him",
                "relation": rel(json!({"cash": 5000, "affection": 10, "treat_balance": -3})), "profile": {}, "seed": seed})).unwrap();
            assert_eq!(v["payer"].as_str(), Some("你請客"), "seed {seed}");
        }
        // They settled to pay their own: halves, not his treat.
        for seed in 1..20 {
            let v = activity_end(&c, &json!({"id": "aquarium_observe", "kind": "solo", "name": "看魚", "player_cash": 1000, "agreed": "split",
                "relation": rel(json!({"cash": 2000, "treat_balance": 3})), "profile": {}, "seed": seed})).unwrap();
            assert_eq!(v["payer"].as_str(), Some("各付一半"), "seed {seed}: {v}");
        }
        let v = shift_end(&json!({"wage": 120, "worked": 90, "early": true, "chats": 5, "rating": 20})).unwrap();
        assert_eq!((v["pay"].as_i64(), v["rating"].as_i64(), v["fired"].as_bool()), (Some(180), Some(8), Some(true)));
    }
    #[test]
    fn who_pays_for_her_chat_order() {
        let o = |v: Value| chat_order_payer(&v)["option"].as_str().unwrap().to_string();
        assert_eq!(o(json!({"lean": "split", "insist": 0.4})), "split", "nothing settled: her own way");
        assert_eq!(o(json!({"lean": "split", "insist": 0.4, "agreed": "him"})), "player_pays");
        assert_eq!(o(json!({"lean": "she_pays", "insist": 0.9})), "choice");
        assert_eq!(o(json!({"lean": "player_pays", "agreed": "split"})), "split", "settled beats her wish that he pays");
        assert_eq!(o(json!({"lean": "player_pays", "agreed": "her"})), "she_pays");
        assert_eq!(o(json!({"lean": "player_pays", "insist": 0.4, "has_hers": true})), "choice", "her own order, nothing settled: he decides");
        assert_eq!(o(json!({"lean": "split", "promise": true})), "she_pays", "her promise");
        assert_eq!(o(json!({"lean": "split", "promise": true, "agreed": "him"})), "player_pays", "he insisted and she let him");
        assert_eq!(super::checkout_timeout(&json!({"lean": "player_pays"}))["option"], "split", "nothing settled");
        assert_eq!(super::checkout_timeout(&json!({"lean": "player_pays", "agreed": "him"}))["option"], "player_pays");
        assert_eq!(super::checkout_timeout(&json!({"lean": "she_pays"}))["option"], "she_pays");
    }

}
