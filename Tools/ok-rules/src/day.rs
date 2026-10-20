//! A new game day for the heroines (NextDay): the day counters, the daily drift of her feelings, sleep putting her
//! energy and hygiene back, his perceived wealth fading toward ordinary, favor and feelings made one again, friends after
//! days of talking, a lover whose feelings faded breaking up, and her money (his allowance, her income and bills, debts,
//! paying him back). Unreal sends every relation with what it alone knows (her actor, how many days they talked, her
//! favor cap) and replays the effects.
use crate::bias;
use crate::calendar;
use crate::effects::Effects;
use crate::relation::Relation;
use crate::rng::RandomStream;
use crate::traits::Traits;
use serde::Deserialize;
use serde_json::{json, Map, Value};

/// One relation in the request: the record plus Unreal's facts about her.
#[derive(Deserialize, Debug, Clone)]
pub struct DayHeroine {
    pub relation: Relation,
    /// She is one of the scene's heroines (a stored relation of someone not in this scene is false).
    #[serde(default)]
    pub present: bool,
    /// Distinct game days they talked (face to face, messages, calls), from her history.
    #[serde(default)]
    pub talk_days: i32,
    /// Her favor cap at her stage (OKStreetFavorLimitFor; -1 when the step-up table has none).
    #[serde(default = "minus_one")]
    pub favor_limit: i32,
    #[serde(default)]
    pub name: String,
}

fn minus_one() -> i32 {
    -1
}

/// Entering a stage lifts her trust (and a lover's attraction) to what the stage means (OKStreetEnterStage).
pub fn enter_stage(r: &mut Relation) {
    let low = crate::relation::liking_band(&r.stage).0;
    r.trust = r.trust.max(low * 0.8);
    if r.stage == "lover" {
        r.attraction = r.attraction.max(low * 0.75);
    }
}

/// A bill: from her cash, then her bank account; only what neither covers becomes a debt (cash -1 stays "wallet
/// unknown").
pub fn charge_bill(r: &mut Relation, amount: i64) {
    if r.cash < 0 || amount <= 0 {
        return;
    }
    r.debt += r.pay(amount);
}

/// What she owes is paid first, from cash and savings alike: nobody keeps ¥11,000 in the bank while owing ¥3,700.
/// A little is kept to live on: the ¥500 she borrowed from him for food went straight to the tuition debt and left her
/// hungry with an empty wallet.
pub fn settle_debt(r: &mut Relation) {
    if r.cash < 0 || r.debt <= 0 || r.funds() <= 0 {
        return;
    }
    let keep = crate::region::amount("cash", 100) as i64;
    let paying = r.debt.min((r.funds() - keep).max(0));
    if paying <= 0 {
        return;
    }
    r.debt -= paying - r.pay(paying);
}

fn num(v: &Value, name: &str) -> f64 {
    v.get(name).and_then(Value::as_f64).unwrap_or(0.0)
}

/// Her income and bills for today from her profile's money section.
fn money_today(r: &mut Relation, money: &Value, today: &calendar::Date) {
    for i in money.get("income").and_then(Value::as_array).into_iter().flatten() {
        let payday = i.get("payday").and_then(Value::as_f64).unwrap_or(1.0) as i64;
        let weekly = i.get("cycle").and_then(Value::as_str) == Some("weekly");
        let due = if weekly { today.weekday as i64 == payday.rem_euclid(7) } else { today.day as i64 == payday };
        if due {
            r.cash += num(i, "amount") as i64;
        }
    }
    for c in money.get("fixed_costs").and_then(Value::as_array).into_iter().flatten() {
        let due = c.get("due_day").and_then(Value::as_f64).unwrap_or(1.0) as i64;
        if today.day as i64 == due {
            charge_bill(r, num(c, "amount") as i64);
        }
    }
    settle_debt(r);
}

pub fn people(request: &Value) -> Result<Value, String> {
    let day = request.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
    let budget = request.get("talk_budget").and_then(Value::as_i64).unwrap_or(0) as i32;
    let mut rng = RandomStream::new(request.get("seed").and_then(Value::as_i64).unwrap_or(0) as i32);
    let heroines: Vec<DayHeroine> = serde_json::from_value(request.get("heroines").cloned().ok_or("missing_field: heroines")?).map_err(|e| format!("heroines: {e}"))?;
    let empty = Map::new();
    let profiles = request.get("profiles").and_then(Value::as_object).unwrap_or(&empty);
    let today = calendar::date(day, 8.0);
    let mut fx = Effects::default();
    // His allowance on the 5th (plan §4.7).
    if today.day == 5 {
        fx.cash(crate::region::fixed("allowance", 1700), "allowance", crate::tr::t("day.family_living_allowance"));
    }
    for h in &heroines {
        let before = h.relation.clone();
        let mut r = before.clone();
        let profile = profiles.get(&r.id).cloned().unwrap_or(Value::Null);
        let traits = Traits::from_profile(&profile);
        r.day += 1;
        r.talks_remaining = budget;
        bias::decay_daily(&mut r, &traits);
        // A night's sleep: rested and washed; how rich she thinks he is fades back toward ordinary.
        r.energy = r.energy.max(95.0);
        r.hygiene = r.hygiene.max(90.0);
        r.perceived_wealth += (20.0 - r.perceived_wealth) * 0.03;
        if h.present {
            // Favor is her feeling as it is: the night does not recompute it.
            let limit = if h.favor_limit < 0 { r.favor_cap() } else { h.favor_limit as f32 };
            if r.stage == "stranger" && r.trust >= 35.0 && r.favor >= limit * 0.8 && h.talk_days >= 3 {
                r.stage = "friend".into();
                enter_stage(&mut r);
                r.add_flag("talk_event:0");
                fx.observe(&r.id, "stage", crate::tr::t("day.days_chatting_friends"), "", 0.7, "observation");
                fx.recent(&r.id, crate::tr::t("day.casual_mention_friend"));
                fx.log(format!("Street stage: {} stranger -> friend (trust {:.0} favor {:.0}, {} days)", r.id, r.trust, r.favor, h.talk_days));
            } else if r.stage == "lover" && r.favor <= 0.0 && r.resentment > 50.0 && rng.frand() < (r.resentment - 50.0) / 50.0 * 0.5 + 0.05 {
                // A lover at the bottom of her favor and full of resentment leaves him; past 90 she wants nothing more to do with him.
                r.stage = if r.resentment >= 90.0 { "stranger" } else { "friend" }.into();
                r.favor = 0.0;
                let why = if r.resentment > 50.0 { crate::tr::t("day.built_up_resentment") } else { crate::tr::t("day.feelings_faded") };
                fx.note(&r.id, "breakup", &crate::tr::m("day.breakup_reason_short", &[("reason", crate::tr::a(&why))]), 0.95, true);
                fx.text(&r.id, &crate::tr::m("day.breakup_own_way", &[("reason", crate::tr::a(&why))]), "breakup", rng.frand_range(30.0, 120.0));
                fx.log(format!("Street stage: {} breaks up (favor 0, resentment {:.0}) -> {}", r.id, r.resentment, r.stage));
            }
        }
        // Her money (plan §4.7): income and bills from her profile, debts first, then paying him back with a margin.
        if let Some(money) = profile.get("money").filter(|m| m.is_object()) {
            if r.cash >= 0 {
                money_today(&mut r, money, &today);
                // Pocket money from her account for the day: ¥1,700 in the bank and "my wallet is empty again, you pay"
                // every day was nobody's real life. What she owes him comes out of savings too.
                let pocket = crate::region::amount("cash", 300) as i64;
                let want = pocket.max(if r.owes_player > 0 { r.owes_player + crate::region::amount("cash", 200) as i64 } else { 0 });
                if r.cash < want && r.bank > 0 {
                    let take = (want - r.cash).min(r.bank);
                    r.bank -= take;
                    r.cash += take;
                }
                if r.owes_player > 0 && r.cash >= r.owes_player + crate::region::amount("cash", 200) as i64 {
                    let name = if h.name.is_empty() { r.id.clone() } else { h.name.clone() };
                    let note = crate::tr::m("day.repay_name_debt", &[("name", crate::tr::a(&name))]);
                    fx.cash(r.owes_player, "lend", &note);
                    fx.post("private", "lent", -r.owes_player, "lend", &note);
                    r.cash -= r.owes_player;
                    r.owes_player = 0;
                    r.trust = (r.trust + 2.0).min(100.0);
                }
            }
        }
        fx.relation(&r, &before);
    }
    Ok(json!({"effects": fx.into_value(), "rumor_heat_factor": 0.85}))
}

pub fn eval(kind: &str, request: &Value) -> Option<Result<Value, String>> {
    match kind {
        "day_people" => Some(people(request)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(heroines: Value, profiles: Value, day: i32, seed: i32) -> Vec<Value> {
        let v = people(&json!({"day": day, "talk_budget": 5, "seed": seed, "heroines": heroines, "profiles": profiles})).unwrap();
        v["effects"].as_array().unwrap().clone()
    }

    fn set_of<'a>(fx: &'a [Value], id: &str) -> &'a Value {
        &fx.iter().find(|e| e["op"] == "relation" && e["id"] == id).unwrap()["set"]
    }

    #[test]
    fn counters_floors_and_drift() {
        let fx = run(json!([{"relation": {"id": "a", "energy": 10, "hygiene": 50, "perceived_wealth": 80, "day": 3}}]), json!({}), 1, 1);
        let s = set_of(&fx, "a");
        assert_eq!((s["day"].as_i64(), s["talks_remaining"].as_i64(), s["energy"].as_f64(), s["hygiene"].as_f64()), (Some(4), Some(5), Some(95.0), Some(90.0)));
        assert!((s["perceived_wealth"].as_f64().unwrap() - 78.2).abs() < 1e-4);
    }

    #[test]
    fn favor_stays_overnight_and_friends_after_three_days() {
        // Favor is kept as it is overnight; half her cap is not friends yet.
        let fx = run(json!([{"present": true, "talk_days": 3, "favor_limit": 100, "relation": {"id": "a", "favor": 50, "trust": 40}}]), json!({}), 1, 1);
        let s = set_of(&fx, "a");
        assert!(s.get("favor").is_none(), "favor unchanged: {s}");
        assert!(fx.iter().all(|e| e["op"] != "observe"), "favor 50/100 is not enough for friends");
        let fx = run(json!([{"present": true, "talk_days": 3, "favor_limit": 100, "relation": {"id": "a", "favor": 85, "trust": 40}}]), json!({}), 1, 1);
        let s = set_of(&fx, "a");
        assert_eq!(s["stage"], "friend");
        assert!(s["flags"].as_array().unwrap().iter().any(|f| f == "talk_event:0"));
        assert!(fx.iter().any(|e| e["op"] == "observe" && e["kind"] == "stage"));
    }

    #[test]
    fn faded_lover_may_break_up() {
        let lover = json!([{"present": true, "favor_limit": 150, "relation": {"id": "a", "stage": "lover", "favor": 0, "trust": 70, "resentment": 100}}]);
        let breakups = (0..200).filter(|seed| run(lover.clone(), json!({}), 1, *seed).iter().any(|e| e["op"] == "note" && e["act"] == "breakup")).count();
        assert!(breakups > 70 && breakups < 130, "about 55% at favor 0 and resentment 100: {breakups}");
        let fond = json!([{"present": true, "favor_limit": 150, "relation": {"id": "a", "stage": "lover", "favor": 1, "trust": 70, "resentment": 95}}]);
        assert!((0..200).all(|seed| run(fond.clone(), json!({}), 1, seed).iter().all(|e| e["act"] != "breakup")), "any favor left: no breakup");
        let fx = (0..200).map(|seed| run(lover.clone(), json!({}), 1, seed)).find(|fx| fx.iter().any(|e| e["op"] == "note")).unwrap();
        assert_eq!(set_of(&fx, "a")["stage"], "stranger");
        assert!(fx.iter().any(|e| e["op"] == "text" && e["text"].as_str().unwrap().contains("怨氣")));
    }

    #[test]
    fn money_allowance_income_bills_debt_and_repaying() {
        // Day 4 is 2010-09-05: his allowance; her payday is the 5th; rent due the 5th.
        let profiles = json!({"a": {"money": {"income": [{"amount": 1000, "payday": 5}], "fixed_costs": [{"amount": 1500, "due_day": 5}]}}});
        let fx = run(json!([{"relation": {"id": "a", "cash": 100}}]), profiles.clone(), 4, 1);
        assert_eq!(fx[0]["op"], "cash");
        assert_eq!(fx[0]["amount"], 1700);
        let s = set_of(&fx, "a");
        assert_eq!((s["cash"].as_i64(), s["debt"].as_i64()), (Some(0), Some(400)));
        let fx = run(json!([{"name": "小美", "relation": {"id": "a", "cash": 5000, "owes_player": 300}}]), json!({"a": {"money": {}}}), 1, 1);
        assert!(fx.iter().any(|e| e["op"] == "cash" && e["amount"] == 300 && e["note"] == "小美還錢"));
        assert!(fx.iter().any(|e| e["op"] == "post" && e["amount"] == -300));
        let s = set_of(&fx, "a");
        assert_eq!((s["cash"].as_i64(), s["owes_player"].as_i64(), s["trust"].as_f64()), (Some(4700), Some(0), Some(22.0)));
        // Weekly income on her weekday (Sunday 6 here: day 4).
        let weekly = json!({"a": {"money": {"income": [{"amount": 50, "payday": 6, "cycle": "weekly"}]}}});
        assert_eq!(set_of(&run(json!([{"relation": {"id": "a", "cash": 0}}]), weekly, 4, 1), "a")["cash"], 50);
        // An empty wallet with money in the bank: she takes out pocket money; what she owes him comes out of savings.
        let s = set_of(&run(json!([{"relation": {"id": "a", "cash": 0, "bank": 1700}}]), json!({"a": {"money": {}}}), 1, 1), "a").clone();
        assert!(s["cash"].as_i64().unwrap() >= 300 && s["bank"].as_i64().unwrap() <= 1400, "{s}");
        let fx = run(json!([{"name": "狂死香", "relation": {"id": "a", "cash": 5, "bank": 1702, "owes_player": 500}}]), json!({"a": {"money": {}}}), 1, 1);
        assert!(fx.iter().any(|e| e["op"] == "cash" && e["amount"] == 500), "{fx:?}");
        // A debt bigger than her money: she keeps a little to eat on (what he lent her for food is not all gone to it).
        let mut r = crate::relation::Relation { id: "k".into(), cash: 500, bank: 0, debt: 1313, ..Default::default() };
        super::settle_debt(&mut r);
        assert_eq!((r.cash, r.debt), (crate::region::amount("cash", 100) as i64, 1313 - 500 + crate::region::amount("cash", 100) as i64));
        // Savings pay the rent and what she owes: no debt while there is money in the bank.
        let s = set_of(&run(json!([{"relation": {"id": "a", "cash": 100, "bank": 5000, "debt": 700}}]), profiles, 4, 1), "a").clone();
        assert_eq!((s["cash"].as_i64(), s["bank"].as_i64(), s["debt"].as_i64()), (Some(300), Some(3600), Some(0)), "and then pocket money");
    }
}
