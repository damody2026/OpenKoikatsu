//! Plan §15: her money in his hands (托付), his money invested for her (幫她翻身) and the four-way split, money that goes
//! overdue or is spent on another girl, a Mercenary heroine finding true love, the identity question, and offering her
//! a job in his company. Unreal keeps the records (FOKStreetEntrust) and replays the effects.
use crate::bias;
use crate::effects::Effects;
use crate::heroine::Ctx;
use crate::relation::Relation;
use crate::rng::RandomStream;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Entrust {
    pub id: i32,
    pub heroine: String,
    /// entrust | help
    pub kind: String,
    pub principal: i64,
    pub returned: i64,
    pub day: i32,
    pub due: i32,
    /// open | settled
    pub status: String,
    /// all | cut20 | half | principal | loss
    pub split: String,
    pub overdue: bool,
    pub jealous: bool,
}

impl Default for Entrust {
    fn default() -> Self {
        Entrust { id: 0, heroine: String::new(), kind: String::new(), principal: 0, returned: 0, day: 0, due: 0, status: "open".into(), split: String::new(), overdue: false, jealous: false }
    }
}

fn axis(r: &mut Relation, name: &str, by: f32) {
    r.add_axis(name, by);
}

/// Starting one: her money (she must trust him and have it) or his (he must have it). Request: kind, amount, days, day,
/// next_id, player_cash, has_open, name, relation, profile.
pub fn start(request: &Value) -> Result<Value, String> {
    let c = Ctx::from_request(request)?;
    let kind = request.get("kind").and_then(Value::as_str).unwrap_or("entrust");
    let mut amount = request.get("amount").and_then(Value::as_i64).unwrap_or(0);
    let days = request.get("days").and_then(Value::as_i64).unwrap_or(7) as i32;
    let day = request.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    let cash = request.get("player_cash").and_then(Value::as_i64).unwrap_or(0);
    let name = request.get("name").and_then(Value::as_str).unwrap_or("");
    if amount <= 0 {
        return Ok(json!({}));
    }
    if request.get("has_open").and_then(Value::as_bool).unwrap_or(false) {
        return Ok(json!({"refused": crate::tr::t("venture.unsettled_exists")}));
    }
    let mut r = c.rel.clone();
    let mut fx = Effects::default();
    if kind == "entrust" {
        if r.trust < 50.0 {
            return Ok(json!({"refused": crate::tr::t("venture.not_trusted_yet")}));
        }
        amount = amount.min(r.cash.max(0));
        if amount < crate::region::amount("cash", 200) {
            return Ok(json!({"refused": crate::tr::t("venture.she_lacks_cash")}));
        }
        r.cash -= amount;
        fx.cash(amount, "entrust", &crate::tr::m("venture.entrusted_money_owed", &[("name", crate::tr::a(&name))]));
    } else {
        if cash < amount {
            return Ok(json!({"refused": crate::tr::t("venture.wallet_short")}));
        }
        fx.cash(-amount, "entrust_share", &crate::tr::m("venture.turnaround_money_for", &[("name", crate::tr::a(&name))]));
    }
    let e = Entrust {
        id: request.get("next_id").and_then(Value::as_i64).unwrap_or(1) as i32,
        heroine: r.id.clone(),
        kind: kind.into(),
        principal: amount,
        day,
        due: day + days.clamp(3, 180),
        ..Default::default()
    };
    fx.relation(&r, &c.rel);
    let span = e.due - e.day;
    let mine = kind == "entrust";
    fx.note(&r.id, "entrust", &if mine { crate::tr::m("venture.hand_over_invest", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("days", crate::tr::a(&span))]) } else { crate::tr::m("venture.he_offers_turnaround", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("days", crate::tr::a(&span))]) }, 0.75, false);
    Ok(json!({
        "entrust": e,
        "effects": fx.into_value(),
        "message": if mine { crate::tr::m("venture.she_hands_money", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("days", crate::tr::a(&span))]) } else { crate::tr::m("venture.you_fund_turnaround", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("days", crate::tr::a(&span))]) },
        "appointment": {"start": e.due as i64 * 1440 + 20 * 60, "grace": 24 * 60, "label": if mine { crate::tr::t("venture.return_entrusted") } else { crate::tr::t("venture.turnaround_settlement") }},
    }))
}

/// Settling: `total` is what the money is worth now; the split decides her share of the profit. Request: entrust, total,
/// split, player_cash, name, relation, profile.
pub fn settle(request: &Value) -> Result<Value, String> {
    let c = Ctx::from_request(request)?;
    let mut e: Entrust = serde_json::from_value(request.get("entrust").cloned().ok_or("missing_field: entrust")?).map_err(|x| format!("entrust: {x}"))?;
    let total = request.get("total").and_then(Value::as_i64).unwrap_or(0).max(0);
    let split = request.get("split").and_then(Value::as_str).unwrap_or("all").to_string();
    let cash = request.get("player_cash").and_then(Value::as_i64).unwrap_or(0);
    let name = request.get("name").and_then(Value::as_str).unwrap_or("");
    let mut r = c.rel.clone();
    let profit = total - e.principal;
    let mut what = String::new();
    let to_her: i64;
    if e.kind == "entrust" {
        // Her own money: principal is hers in every case; the split is only of the profit.
        if profit <= 0 {
            to_her = total;
            what = if total >= e.principal { crate::tr::t("venture.principal_no_profit").into() } else { crate::tr::m("venture.partial_repay_loss", &[("repaid", crate::tr::a(&crate::tr::money(total))), ("loss", crate::tr::a(&crate::tr::money(e.principal - total)))]) };
            if total < e.principal {
                axis(&mut r, "trust", -10.0);
                axis(&mut r, "resentment", 5.0);
            } else {
                axis(&mut r, "trust", -1.0);
            }
        } else {
            let share = match split.as_str() {
                "all" | "principal" => 1.0,
                "cut20" => 0.8,
                _ => 0.5,
            };
            to_her = e.principal + (profit as f64 * share) as i64;
        }
    } else if profit <= 0 {
        // His money for her: nothing made.
        to_her = 0;
        what = crate::tr::t("venture.invest_no_profit").into();
        axis(&mut r, "trust", 1.0);
    } else {
        to_her = match split.as_str() {
            "all" => total,
            "cut20" => (profit as f64 * 0.8) as i64,
            "half" => (profit as f64 * 0.5) as i64,
            _ => profit,
        };
    }
    if cash < to_her {
        return Ok(json!({"refused": crate::tr::m("venture.need_cash_to_split", &[("amount", crate::tr::a(&crate::tr::money(to_her)))])}));
    }
    if profit > 0 {
        match split.as_str() {
            "all" => {
                axis(&mut r, "trust", 8.0);
                axis(&mut r, "admiration", 8.0);
                axis(&mut r, "affection", 4.0);
                what = crate::tr::t("venture.gave_all_profit").into();
            }
            "cut20" => {
                axis(&mut r, "respect", 6.0);
                axis(&mut r, "trust", 3.0);
                r.add_flag("business_partner");
                what = crate::tr::t("venture.took_20_percent").into();
            }
            "half" => {
                axis(&mut r, "dependency", 8.0);
                axis(&mut r, "trust", 2.0);
                what = crate::tr::t("venture.split_profit_fifty").into();
            }
            _ => {
                axis(&mut r, "mystique", 12.0);
                axis(&mut r, "trust", 4.0);
                what = crate::tr::t("venture.he_only_principal").into();
            }
        }
    }
    let mut fx = Effects::default();
    // Her own money coming back is a transfer; anything beyond it is his money given to her.
    let back = if e.kind == "entrust" { to_her.min(e.principal) } else { 0 };
    fx.cash(-back, "entrust", &crate::tr::m("venture.return_principal_to", &[("name", crate::tr::a(&name))]));
    fx.cash(-(to_her - back), "entrust_share", &crate::tr::m("venture.settlement_share", &[("name", crate::tr::a(&name))]));
    r.cash = r.cash.max(0) + to_her;
    e.returned = to_her;
    e.status = "settled".into();
    e.split = if profit > 0 { split } else { "loss".into() };
    if e.overdue {
        // Returning late still mends some of it.
        axis(&mut r, "resentment", -10.0);
        axis(&mut r, "trust", 3.0);
    }
    fx.relation(&r, &c.rel);
    fx.note(&r.id, "entrust", &crate::tr::m("venture.settlement_received", &[("kind", crate::tr::a(&(if e.kind == "entrust" { crate::tr::t("venture.entrusted_money_label") } else { crate::tr::t("venture.turnaround_money_label") }))), ("amount", crate::tr::a(&crate::tr::money(to_her))), ("detail", crate::tr::a(&what))]), 0.85, false);
    Ok(json!({"entrust": e, "effects": fx.into_value(), "message": crate::tr::m("venture.settlement_done", &[("amount", crate::tr::a(&crate::tr::money(to_her))), ("detail", crate::tr::a(&what))])}))
}

#[derive(Deserialize, Debug, Clone)]
struct Rumor {
    #[serde(default)]
    kind: String,
    #[serde(default)]
    about: String,
    #[serde(default)]
    day: i32,
    #[serde(default)]
    known_by: Vec<String>,
}

#[derive(Deserialize, Debug, Clone)]
struct DailyHeroine {
    relation: Relation,
    #[serde(default)]
    profile: Value,
    /// One of the scene's heroines (the true-love and identity rules are for them).
    #[serde(default)]
    present: bool,
    /// Her actor index (seeds her true-love roll like before).
    #[serde(default)]
    actor: i32,
    #[serde(default)]
    met_today: bool,
}

/// The day's rules (from NextDay): overdue entrusted money, her money spent on someone else, 拜金轉真愛, the identity
/// question. Request: day, player_cash, entrusts, rumors, heroines.
pub fn daily(request: &Value) -> Result<Value, String> {
    let day = request.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
    let cash = request.get("player_cash").and_then(Value::as_i64).unwrap_or(0);
    let mut entrusts: Vec<Entrust> = serde_json::from_value(request.get("entrusts").cloned().unwrap_or(json!([]))).map_err(|e| format!("entrusts: {e}"))?;
    let rumors: Vec<Rumor> = serde_json::from_value(request.get("rumors").cloned().unwrap_or(json!([]))).map_err(|e| format!("rumors: {e}"))?;
    let heroines: Vec<DailyHeroine> = serde_json::from_value(request.get("heroines").cloned().unwrap_or(json!([]))).map_err(|e| format!("heroines: {e}"))?;
    let mut rels: Vec<Relation> = heroines.iter().map(|h| h.relation.clone()).collect();
    let mut fx = Effects::default();
    for e in entrusts.iter_mut().filter(|e| e.status == "open") {
        let Some(r) = rels.iter_mut().find(|r| r.id == e.heroine) else { continue };
        if e.kind == "entrust" && day > e.due && !e.overdue {
            // Overdue entrusted money: trust and affection fall hard, resentment rises; it can still be mended.
            e.overdue = true;
            r.trust = (r.trust - 20.0).max(0.0);
            r.add_liking(-15.0);
            r.resentment = (r.resentment + 20.0).min(100.0);
            fx.note(&r.id, "entrust", &crate::tr::m("venture.overdue_repay", &[("amount", crate::tr::a(&crate::tr::money(e.principal)))]), 0.9, false);
        }
        // Her money while he treats someone else (a treat or gift rumour about another heroine she has heard).
        if e.kind == "entrust" && !e.jealous && cash < e.principal {
            if let Some(rm) = rumors.iter().find(|rm| (rm.kind == "treat" || rm.kind == "gift") && rm.day >= e.day && rm.about != e.heroine && rm.known_by.contains(&e.heroine)) {
                e.jealous = true;
                let j = r.jealousy_toward.entry(rm.about.clone()).or_insert(0.0);
                *j = (*j + 25.0).min(100.0);
                r.jealousy = (r.jealousy + 15.0).min(100.0);
                r.trust = (r.trust - 8.0).max(0.0);
                fx.note(&r.id, "entrust", crate::tr::t("venture.money_spent_on_others"), 0.9, false);
            }
        }
    }
    for (h, r) in heroines.iter().zip(rels.iter_mut()).filter(|(h, _)| h.present) {
        // 拜金轉真愛: a Mercenary heroine whose wants are met, who saw him stay while he had nothing to give.
        if h.profile.get("material").and_then(Value::as_str) == Some("Mercenary") && !r.has_flag("true_love") {
            let mut poor: i32 = r.flags.iter().filter_map(|f| f.strip_prefix("poor_with_me:")).filter_map(|n| n.parse().ok()).last().unwrap_or(0);
            if h.met_today && r.perceived_wealth < 30.0 {
                r.flags.retain(|f| !f.starts_with("poor_with_me:"));
                poor += 1;
                r.flags.push(format!("poor_with_me:{poor}"));
            }
            let mut rng = RandomStream::new(day * 31 + h.actor * 7);
            if poor >= 3 && r.greed < 40.0 && r.liking() >= 60.0 && rng.frand() < 0.35 {
                r.flags.push("true_love".into());
                r.greed = (r.greed - 20.0).max(0.0);
                r.add_liking(10.0);
                fx.observe(&r.id, "true_love", crate::tr::t("venture.realized_not_for_money"), "", 0.95, "reflection");
            }
        }
        // The identity question: worship plus mystique make her ask who he really is (once).
        if bias::admiration_stage(r, 0) >= 5 && r.mystique >= 80.0 && !r.has_flag("asked_identity") {
            r.flags.push("asked_identity".into());
            fx.recent(&r.id, crate::tr::t("venture.ask_who_are_you"));
            fx.observe(&r.id, "identity", crate::tr::t("venture.suspect_foresight"), "", 0.85, "reflection");
        }
    }
    let mut out = Effects::default();
    for (h, r) in heroines.iter().zip(rels.iter()) {
        out.relation(r, &h.relation);
    }
    // Relation changes first (the notes describe what they already reflect), then the notes and memories.
    out.0.extend(fx.0);
    Ok(json!({"entrusts": entrusts, "effects": out.into_value()}))
}

/// His job offer: her answer from her feelings, the pay against her income, the role and her pride and competence.
/// Request: role, pay, title_public, exclusive, company, name, relation, profile.
pub fn offer(request: &Value) -> Result<Value, String> {
    let c = Ctx::from_request(request)?;
    let role = request.get("role").and_then(Value::as_str).unwrap_or("manager");
    let pay = request.get("pay").and_then(Value::as_i64).unwrap_or(0);
    let public = request.get("title_public").and_then(Value::as_bool).unwrap_or(true);
    let exclusive = request.get("exclusive").and_then(Value::as_bool).unwrap_or(false);
    let company = request.get("company").and_then(Value::as_str).unwrap_or("");
    let occupation = c.profile.get("occupation").cloned().unwrap_or(Value::Null);
    let occ = |k: &str| occupation.get(k).and_then(Value::as_f64).map(|v| v as i64).unwrap_or(50);
    let income: i64 = c.profile.get("money").and_then(|m| m.get("income")).and_then(Value::as_array).map(|l| l.iter().filter_map(|i| i.get("amount").and_then(Value::as_f64)).map(|a| a as i64).sum()).unwrap_or(0);
    let (comp, pride) = (occ("competence"), occ("pride"));
    let ratio = pay as f32 / income.max(crate::region::amount("salary", 1300)) as f32;
    let r = &c.rel;
    let mut score = r.trust * 0.4 + r.liking() * 0.25 + r.admiration * 0.2 + (ratio - 1.0).clamp(-1.0, 2.0) * 20.0;
    if exclusive {
        score -= 10.0;
    }
    if public && role == "ceo" {
        score += 8.0;
    }
    if role == "secretary" && pride > 70 {
        score -= 12.0;
    }
    if role == "ceo" && comp < 40 {
        score -= 15.0;
    }
    if crate::mind::money_stress(r, &c.profile).0 > 60.0 {
        score += 10.0;
    }
    let accept = score >= 45.0;
    let role_name = match role {
        "ceo" => crate::tr::t("venture.ceo_title"),
        "secretary" => crate::tr::t("venture.secretary_title"),
        _ => crate::tr::t("venture.manager_title"),
    };
    let words = crate::tr::m("venture.job_offer", &[("company", crate::tr::a(&company)), ("role", crate::tr::a(&role_name)), ("salary", crate::tr::a(&crate::tr::money(pay))), ("public_note", crate::tr::a(&(if public { crate::tr::t("venture.public_title_suffix") } else { crate::tr::t("venture.not_public_suffix") }))), ("exclusive_note", crate::tr::a(&(if exclusive { crate::tr::t("venture.no_second_job_suffix") } else { "" })))]);
    Ok(json!({"accept": accept, "score": score, "offer": words, "role_name": role_name}))
}

/// Firing her: trust falls, resentment rises.
pub fn fire(request: &Value) -> Result<Value, String> {
    let c = Ctx::from_request(request)?;
    let mut r = c.rel.clone();
    r.trust = (r.trust - 10.0).max(0.0);
    r.resentment = (r.resentment + 10.0).min(100.0);
    let mut fx = Effects::default();
    fx.relation(&r, &c.rel);
    fx.note(&r.id, "company_job", crate::tr::t("venture.fired_me"), 0.85, false);
    Ok(json!({"effects": fx.into_value()}))
}

pub fn eval(kind: &str, request: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "entrust_start" => start(request),
        "entrust_settle" => settle(request),
        "venture_daily" => daily(request),
        "job_offer" => offer(request),
        "job_fire" => fire(request),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(extra: Value) -> Value {
        let mut r = json!({"id": "a", "cash": 1000, "trust": 60});
        r.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
        r
    }

    #[test]
    fn starting_needs_trust_and_money() {
        let v = start(&json!({"kind": "entrust", "amount": 5000, "days": 10, "day": 3, "next_id": 7, "player_cash": 0, "name": "小美", "relation": rel(json!({}))})).unwrap();
        assert_eq!(v["entrust"]["principal"], 1000, "only what she has");
        assert_eq!((v["entrust"]["id"].as_i64(), v["entrust"]["due"].as_i64()), (Some(7), Some(13)));
        assert_eq!(v["appointment"]["start"], 13 * 1440 + 20 * 60);
        assert!(v["effects"].as_array().unwrap().iter().any(|e| e["op"] == "cash" && e["amount"] == 1000));
        let low = start(&json!({"kind": "entrust", "amount": 500, "relation": rel(json!({"trust": 30}))})).unwrap();
        assert!(low["refused"].as_str().unwrap().contains("信任"));
        let open = start(&json!({"kind": "help", "amount": 500, "has_open": true, "relation": rel(json!({}))})).unwrap();
        assert!(open["refused"].is_string());
    }

    #[test]
    fn four_way_split() {
        let e = json!({"id": 1, "heroine": "a", "kind": "entrust", "principal": 1000, "status": "open"});
        let v = settle(&json!({"entrust": e, "total": 2000, "split": "cut20", "player_cash": 5000, "name": "小美", "relation": rel(json!({}))})).unwrap();
        assert_eq!(v["entrust"]["returned"], 1800);
        let fx = v["effects"].as_array().unwrap();
        assert!(fx.iter().any(|x| x["op"] == "cash" && x["amount"] == -1000 && x["category"] == "entrust"));
        assert!(fx.iter().any(|x| x["op"] == "cash" && x["amount"] == -800 && x["category"] == "entrust_share"));
        let set = &fx.iter().find(|x| x["op"] == "relation").unwrap()["set"];
        assert!(set["flags"].as_array().unwrap().iter().any(|f| f == "business_partner"));
        let loss = settle(&json!({"entrust": e, "total": 600, "split": "all", "player_cash": 5000, "relation": rel(json!({}))})).unwrap();
        assert_eq!(loss["entrust"]["split"], "loss");
        let help = json!({"id": 2, "heroine": "a", "kind": "help", "principal": 1000, "status": "open"});
        assert_eq!(settle(&json!({"entrust": help, "total": 3000, "split": "all", "player_cash": 5000, "relation": rel(json!({}))})).unwrap()["entrust"]["returned"], 3000);
        assert!(settle(&json!({"entrust": help, "total": 3000, "split": "all", "player_cash": 10, "relation": rel(json!({}))})).unwrap()["refused"].is_string());
    }

    #[test]
    fn overdue_and_jealous_money() {
        let req = json!({"day": 20, "player_cash": 0,
            "entrusts": [{"id": 1, "heroine": "a", "kind": "entrust", "principal": 1000, "day": 5, "due": 15, "status": "open"}],
            "rumors": [{"kind": "gift", "about": "b", "day": 6, "known_by": ["a"]}],
            "heroines": [{"relation": rel(json!({"stage": "friend", "favor": 100})), "present": true, "actor": 1}]});
        let v = daily(&req).unwrap();
        assert_eq!((v["entrusts"][0]["overdue"].as_bool(), v["entrusts"][0]["jealous"].as_bool()), (Some(true), Some(true)));
        let set = &v["effects"][0]["set"];
        // Liking 50 -> 35: a friend at half her favor drops to the bottom of it.
        assert_eq!((set["trust"].as_f64(), set["favor"].as_f64(), set["jealousy_toward"]["b"].as_f64()), (Some(32.0), Some(0.0), Some(25.0)));
    }

    #[test]
    fn true_love_counts_poor_days_and_identity() {
        let h = |flags: Value, met: bool| json!({"relation": rel(json!({"flags": flags, "perceived_wealth": 10, "greed": 20, "stage": "lover", "favor": 22})), "profile": {"material": "Mercenary"}, "present": true, "actor": 1, "met_today": met});
        let v = daily(&json!({"day": 1, "heroines": [h(json!(["poor_with_me:1"]), true)]})).unwrap();
        assert!(v["effects"][0]["set"]["flags"].as_array().unwrap().iter().any(|f| f == "poor_with_me:2"));
        let loved = (0..60).filter(|d| daily(&json!({"day": d, "heroines": [h(json!(["poor_with_me:3"]), false)]})).unwrap()["effects"].as_array().unwrap().iter().any(|e| e["kind"] == "true_love")).count();
        assert!(loved > 8 && loved < 35, "about 35%: {loved}");
        let idol = json!({"relation": rel(json!({"admiration": 100, "mystique": 90})), "present": true});
        let v = daily(&json!({"day": 1, "heroines": [idol]})).unwrap();
        assert!(v["effects"].as_array().unwrap().iter().any(|e| e["kind"] == "identity"));
    }

    #[test]
    fn job_offers() {
        let base = |role: &str, pay: i64, extra: Value| {
            let mut r = json!({"role": role, "pay": pay, "title_public": true, "exclusive": false, "company": "星火", "relation": rel(extra), "profile": {"occupation": {"competence": 30, "pride": 80}, "money": {"income": [{"amount": 1300}]}}});
            r["relation"]["stage"] = json!("friend");
            r["relation"]["favor"] = json!(167);
            r
        };
        let v = offer(&base("manager", 2600, json!({}))).unwrap();
        assert!(v["accept"].as_bool().unwrap(), "{v}");
        assert!(!offer(&base("secretary", 1300, json!({"trust": 20}))).unwrap()["accept"].as_bool().unwrap());
        assert!(offer(&base("ceo", 2600, json!({}))).unwrap()["offer"].as_str().unwrap().contains("執行長"));
    }
}
