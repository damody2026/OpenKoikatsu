//! What the rules tell the heroine's model each turn and what they accept back (plan §4, §7, §10): the inner line and
//! leanings (bias), the needs she lets show, money stress, the validator for the model's relationship deltas and
//! emotion, the daily drift, and negotiating a pending choice (plan §7.1).
use crate::bias;
use crate::heroine::Ctx;
use crate::relation::Relation;
use serde_json::{json, Value};

/// Money stress 0..100 (plan §4.3): a week's spending by her style against her cash, urgent needs she cannot cover,
/// and debt. `secret`: the worst need is one she hides.
pub fn money_stress(r: &Relation, profile: &Value) -> (f32, bool) {
    if r.cash < 0 {
        return (0.0, false);
    }
    let Some(money) = profile.get("money").filter(|m| m.is_object()) else { return (0.0, false) };
    let weekly: i64 = match money.get("spending_style").and_then(Value::as_str).unwrap_or("") {
        "frugal" => crate::region::amount("cash", 200),
        "generous" => crate::region::amount("cash", 600),
        "lavish" => crate::region::amount("cash", 1000),
        _ => crate::region::amount("cash", 400),
    };
    let (mut worst, mut need, mut secret) = (0.0f32, 0i64, false);
    for n in money.get("money_needs").and_then(Value::as_array).into_iter().flatten() {
        let sev = n.get("severity").and_then(Value::as_f64).unwrap_or(0.0) as f32;
        if sev > worst {
            worst = sev;
            need = n.get("amount").and_then(Value::as_f64).unwrap_or(0.0) as i64;
            secret = n.get("secret").and_then(Value::as_bool).unwrap_or(false);
        }
    }
    let mut s = if weekly > 0 { (100.0 * (1.0 - r.cash as f32 / weekly as f32)).clamp(0.0, 100.0) } else { 0.0 };
    if r.cash < need {
        s = s.max(worst * 100.0);
    }
    // In debt (bills she could not pay): at least 50, more the bigger it is against a week's spending.
    let debt = if r.debt > 0 { (50.0 + 100.0 * r.debt as f32 / weekly as f32).clamp(0.0, 100.0) } else { 0.0 };
    (s.max(debt), secret)
}

/// Words for the prompt: only what she would let show.
pub fn needs_text(r: &Relation, money_stress: f32, show_money: bool) -> String {
    let mut p: Vec<&str> = Vec::new();
    if r.satiety < 25.0 {
        p.push(crate::tr::t("mind.hungry_mood"));
    } else if r.satiety < 45.0 {
        p.push(crate::tr::t("mind.feeling_hungry"));
    } else if r.satiety >= 80.0 {
        // Full, said as plainly as hungry is: with nothing about her stomach outside a menu, 三月七 an hour after her
        // lunch opened with "I'm hungry, let's eat" and two lines later "I just ate, I'm not hungry" (R6-16).
        p.push(crate::tr::t("mind.full_not_hungry"));
    }
    if r.energy < 20.0 {
        p.push(crate::tr::t("mind.very_tired"));
    } else if r.energy < 40.0 {
        p.push(crate::tr::t("mind.feeling_tired"));
    }
    if r.fun < 25.0 {
        p.push(crate::tr::t("mind.bored_go_out"));
    }
    if r.social < 25.0 {
        p.push(crate::tr::t("mind.feeling_lonely"));
    }
    if r.hygiene < 25.0 {
        p.push(crate::tr::t("mind.sweaty_keep_distance"));
    }
    if show_money && money_stress > 70.0 {
        p.push(crate::tr::t("mind.cash_tight_lately"));
    } else if show_money && money_stress > 40.0 {
        p.push(crate::tr::t("mind.short_on_money"));
    }
    let mut out = p.join(crate::tr::t("bank.list_separator"));
    // Who owes whom, plainly: her memories only say she once borrowed, and the model can turn that around.
    if r.owes_player > 0 {
        if !out.is_empty() {
            out.push('、');
        }
        out.push_str(&crate::tr::m("mind.you_owe_him", &[("amount", crate::tr::a(&crate::tr::money(r.owes_player)))]));
    }
    out
}

/// The player picked `option` with `player_insist`; she may push back once or twice when she leans elsewhere and
/// insists harder, then the stronger side wins. Returns (outcome, final option, round, her insistence).
pub fn negotiate(options: &[String], lean: &str, insist: f32, round: i32, option: &str, player_insist: f32) -> (&'static str, String, i32, f32) {
    if !options.iter().any(|o| o == option) {
        return ("rejected", String::new(), round, insist);
    }
    if lean.is_empty() || lean == option || option == "let_her_decide" {
        let fin = if option == "let_her_decide" && !lean.is_empty() { lean } else { option };
        return ("resolved", fin.into(), round, insist);
    }
    if round < 2 && insist > player_insist {
        // Each pushback costs her some insistence: she gives in eventually unless she means it.
        return ("pushback", String::new(), round + 1, insist * 0.85);
    }
    let fin = if insist > player_insist + 0.25 { lean } else { option };
    ("resolved", fin.into(), round, insist)
}

pub fn eval(kind: &str, request: &Value) -> Option<Result<Value, String>> {
    match kind {
        "mind_status" | "mind_bias" | "money_stress" | "admiration_stage" | "rel_delta" | "decay_daily" | "negotiate" => Some(handle(kind, request)),
        "favor_gain" => Some(Ok(favor_gain(request))),
        _ => None,
    }
}

/// A talk turn's favor change for her (request: stage, favor, limit, delta, lovers = his lovers other than her):
/// a gain scaled by lover_gain_scale, kept in 0..limit. Answer: favor (new), delta (what it moved).
pub fn favor_gain(r: &Value) -> Value {
    let n = |k: &str| r.get(k).and_then(Value::as_f64).unwrap_or(0.0) as f32;
    let stage = r.get("stage").and_then(Value::as_str).unwrap_or("stranger");
    let (favor, limit, mut delta) = (n("favor"), n("limit").max(1.0), n("delta"));
    if delta > 0.0 {
        delta *= crate::relation::lover_gain_scale(stage, n("lovers") as i32);
    }
    let after = (favor + delta).clamp(0.0, limit);
    json!({"favor": after, "delta": after - favor})
}

fn handle(kind: &str, request: &Value) -> Result<Value, String> {
    let flag = |n: &str| request.get(n).and_then(Value::as_bool).unwrap_or(false);
    let now = request.get("now").and_then(Value::as_i64).unwrap_or(0);
    if kind == "negotiate" {
        let options: Vec<String> = request.get("options").and_then(|v| serde_json::from_value(v.clone()).ok()).unwrap_or_default();
        let s = |n: &str| request.get(n).and_then(Value::as_str).unwrap_or("").to_string();
        let f = |n: &str| request.get(n).and_then(Value::as_f64).unwrap_or(0.0) as f32;
        let (outcome, fin, round, insist) = negotiate(&options, &s("lean"), f("insist"), request.get("round").and_then(Value::as_i64).unwrap_or(0) as i32, &s("option"), f("player_insist"));
        return Ok(json!({"outcome": outcome, "final": fin, "round": round, "insist": insist}));
    }
    let c = Ctx::from_request(request)?;
    Ok(match kind {
        "mind_bias" | "mind_status" => {
            // public: anywhere but alone with him; mask_broken: a trusting lover alone with him (Unreal knows where they are).
            let mood = bias::mood_now(&c.rel, now);
            let b = bias::compute(&c.rel, &c.traits, flag("public"), flag("mask_broken"), mood, bias::low_mood(&c.rel, mood));
            let rival_bands: serde_json::Map<String, Value> = c.rel.jealousy_toward.iter().map(|(k, v)| (k.clone(), json!(bias::jealousy_band(*v)))).collect();
            let mut out = json!({"bias": b, "admiration_stage": bias::admiration_stage(&c.rel, 0), "admiration_band": bias::admiration_band(c.rel.admiration), "jealousy_band": bias::jealousy_band(c.rel.jealousy), "rival_bands": rival_bands});
            if kind == "mind_status" {
                let (stress, secret) = money_stress(&c.rel, &c.profile);
                out["money_stress"] = json!(stress);
                out["needs_text"] = json!(needs_text(&c.rel, stress, !secret || c.rel.trust >= 60.0));
            }
            out
        }
        "admiration_stage" => {
            let feats = request.get("feats").and_then(Value::as_i64).unwrap_or(0) as i32;
            json!({"admiration_stage": bias::admiration_stage(&c.rel, feats)})
        }
        "money_stress" => {
            let (stress, secret) = money_stress(&c.rel, &c.profile);
            json!({"money_stress": stress, "secret": secret})
        }
        "rel_delta" => {
            // The model's rel_delta and emotion for this turn, through the validator.
            let mut r = c.rel.clone();
            let applied = match request.get("delta").and_then(Value::as_object) {
                Some(d) => bias::apply_rel_delta(&mut r, d, &c.traits),
                None => Vec::new(),
            };
            bias::set_emotion(&mut r, request.get("emotion").and_then(Value::as_str).unwrap_or(""), now);
            json!({"applied": applied, "relation": r.patch(&c.rel)})
        }
        _ => {
            // decay_daily
            let mut r = c.rel.clone();
            bias::decay_daily(&mut r, &c.traits);
            json!({"relation": r.patch(&c.rel)})
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn money_stress_from_style_needs_and_debt() {
        let r = Relation { cash: 100, ..Default::default() };
        let p = json!({"money": {"spending_style": "frugal", "money_needs": [{"severity": 0.9, "amount": 500, "secret": true}]}});
        let (s, secret) = money_stress(&r, &p);
        assert!((s - 90.0).abs() < 1e-4 && secret);
        assert_eq!(money_stress(&Relation { cash: -1, ..Default::default() }, &p).0, 0.0);
        let debt = Relation { cash: 1000, debt: 200, ..Default::default() };
        assert!((money_stress(&debt, &json!({"money": {}})).0 - 100.0).abs() < 1e-4, "50 + 100*200/400");
    }

    #[test]
    fn needs_words() {
        let r = Relation { satiety: 30.0, energy: 10.0, social: 10.0, ..Default::default() };
        assert_eq!(needs_text(&r, 80.0, true), "有點餓、非常累、有點寂寞、最近手頭很緊");
        // Four lovers already: a +4 for someone else counts 1; a loss is whole; a lover's own gain is whole.
        let g = |stage: &str, delta: f32| favor_gain(&json!({"stage": stage, "favor": 10, "limit": 100, "delta": delta, "lovers": 4}))["delta"].as_f64().unwrap();
        assert_eq!((g("friend", 4.0), g("friend", -4.0), g("lover", 4.0)), (1.0, -4.0, 4.0));
        assert_eq!(favor_gain(&json!({"stage": "friend", "favor": 10, "limit": 100, "delta": 10, "lovers": 9}))["delta"], 2.0, "never below 1/5");
        let owing = Relation { owes_player: 400, ..r.clone() };
        assert!(needs_text(&owing, 0.0, false).ends_with("你還欠他 ¥400（是他借給你的，不是他欠你）"));
        assert_eq!(needs_text(&Relation { satiety: 60.0, ..Default::default() }, 80.0, false), "");
        assert_eq!(needs_text(&Relation { satiety: 98.0, energy: 80.0, fun: 80.0, social: 80.0, hygiene: 80.0, ..Default::default() }, 0.0, false), "剛吃飽，不餓");
    }

    #[test]
    fn negotiation_rounds() {
        let o = vec!["she_pays".to_string(), "player_pays".to_string(), "split".to_string()];
        assert_eq!(negotiate(&o, "split", 0.9, 0, "player_pays", 0.5).0, "pushback");
        let (_, _, round, insist) = negotiate(&o, "split", 0.9, 0, "player_pays", 0.5);
        assert_eq!((round, insist), (1, 0.9 * 0.85));
        let (outcome, fin, _, _) = negotiate(&o, "split", 0.9, 2, "player_pays", 0.5);
        assert_eq!((outcome, fin.as_str()), ("resolved", "split"), "she insists harder by more than 0.25");
        assert_eq!(negotiate(&o, "split", 0.6, 2, "player_pays", 0.5).1, "player_pays");
        assert_eq!(negotiate(&o, "split", 0.6, 0, "let_her_decide", 0.5).1, "", "not an option");
        assert_eq!(negotiate(&o, "", 0.6, 0, "nope", 0.5).0, "rejected");
    }

    #[test]
    fn rel_delta_request() {
        let v = handle("rel_delta", &json!({"relation": {"id": "a"}, "profile": {}, "delta": {"trust": 3}, "emotion": "害羞", "now": 50})).unwrap();
        assert_eq!(v["relation"]["trust"], 23.0);
        assert_eq!(v["relation"]["emotion"], "害羞");
        assert_eq!(v["applied"][0], "trust+3");
    }
}
