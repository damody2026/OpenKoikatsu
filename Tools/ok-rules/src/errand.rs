//! Her errands at the administration office (Docs/STREET_NEW_FEATURES_PLAN.md 3-1): she applies for a student loan for
//! the fees she owes, or pays them from her own money. That she does it is the AI's reading (judge her_errand); here
//! it is done to her money and recorded: her loan in the save's `ext` (her_loans), the change through the effects.
use crate::effects::Effects;
use crate::relation::Relation;
use serde_json::{json, Value};

/// The most a term's loan covers (yuan): the term's fees.
fn loan_cap() -> i64 {
    crate::campus::TUITION_FEE() + crate::campus::DORM_FEE()
}

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

/// Request: relation, kind (student_loan | pay_tuition), place (where they are), day, ext. Answer: ok, fact (for her
/// and the notice), effects, ext.
fn errand(r: &Value) -> Result<Value, String> {
    let mut rel: Relation = serde_json::from_value(r.get("relation").cloned().unwrap_or(Value::Null)).map_err(|e| format!("relation: {e}"))?;
    let before = rel.clone();
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    let refuse = |why: &str, ext: Value| Ok(json!({"ok": false, "fact": why, "effects": Effects::default().into_value(), "ext": ext}));
    if s(r, "place") != "campus_ad" {
        return refuse(crate::tr::t("errand.only_at_office"), ext);
    }
    let mut fx = Effects::default();
    let fact = match s(r, "kind") {
        "student_loan" => {
            if rel.debt <= 0 {
                return refuse(crate::tr::t("errand.nothing_owed"), ext);
            }
            let amount = rel.debt.min(loan_cap());
            rel.debt -= amount;
            let owed = ext.get("her_loans").and_then(|l| l.get(&rel.id)).and_then(|l| l.get("owed")).and_then(Value::as_i64).unwrap_or(0) + amount;
            ext["her_loans"][&rel.id] = json!({"owed": owed, "day": r.get("day").and_then(Value::as_i64).unwrap_or(0)});
            crate::tr::m("errand.loan_done", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("owed", crate::tr::a(&crate::tr::money(owed)))])
        }
        "pay_tuition" => {
            if rel.debt <= 0 {
                return refuse(crate::tr::t("errand.nothing_owed"), ext);
            }
            let pay = rel.debt.min(rel.funds().max(0));
            if pay <= 0 {
                return refuse(crate::tr::t("errand.no_money"), ext);
            }
            rel.pay(pay);
            rel.debt -= pay;
            if rel.debt > 0 {
                crate::tr::m("errand.paid_part", &[("paid", crate::tr::a(&crate::tr::money(pay))), ("left", crate::tr::a(&crate::tr::money(rel.debt)))])
            } else {
                crate::tr::m("errand.paid_all", &[("paid", crate::tr::a(&crate::tr::money(pay)))])
            }
        }
        _ => return refuse("", ext),
    };
    fx.relation(&rel, &before);
    fx.observe(&rel.id, "errand", &fact, "", 0.6, "observation");
    Ok(json!({"ok": true, "fact": fact, "effects": fx.into_value(), "ext": ext}))
}

/// For her words: the student loan she has, and, while she owes fees, that the office can lend them (hers to bring up,
/// or not). Request: ext, girl, relation. Answer: text.
fn facts(r: &Value) -> Value {
    let owed = r.get("ext").and_then(|e| e.get("her_loans")).and_then(|l| l.get(s(r, "girl"))).and_then(|l| l.get("owed")).and_then(Value::as_i64).unwrap_or(0);
    let debt = r.get("relation").and_then(|v| v.get("debt")).and_then(Value::as_i64).unwrap_or(0);
    let mut parts = Vec::new();
    if owed > 0 {
        parts.push(crate::tr::m("errand.fact_loan", &[("owed", crate::tr::a(&crate::tr::money(owed)))]));
    }
    if debt > 0 {
        parts.push(crate::tr::m("errand.fact_can_borrow", &[("debt", crate::tr::a(&crate::tr::money(debt)))]));
    }
    // Almost nothing in hand or in the bank: said plainly (with ¥0 she kept saying "this one is my treat", and he paid).
    let rel: Option<crate::relation::Relation> = r.get("relation").cloned().and_then(|v| serde_json::from_value(v).ok());
    if let Some(rel) = rel.filter(|x| x.cash >= 0) {
        if rel.funds() < crate::region::amount("cash", 30) {
            parts.push(crate::tr::m("errand.fact_short_of_money", &[("funds", crate::tr::a(&crate::tr::money(rel.funds())))]));
        }
    }
    json!({"text": parts.join(crate::tr::t("items.fact_separator"))})
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "her_errand" => errand(r),
        "errand_facts" => Ok(facts(r)),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(cash: i64, debt: i64) -> Value {
        let mut r = Relation::default();
        r.id = "a".into();
        r.cash = cash;
        r.debt = debt;
        serde_json::to_value(r).unwrap()
    }

    #[test]
    fn a_loan_clears_what_she_owes_and_is_remembered() {
        let v = errand(&json!({"relation": rel(74, 2600), "kind": "student_loan", "place": "campus_ad", "day": 21, "ext": {"items": {}}})).unwrap();
        assert_eq!(v["ok"], true);
        assert_eq!(v["ext"]["her_loans"]["a"]["owed"], 2600);
        assert!(v["ext"]["items"].is_object());
        assert!(!facts(&json!({"ext": v["ext"], "girl": "a"}))["text"].as_str().unwrap().is_empty());
    }

    #[test]
    fn only_at_the_office_and_only_when_owing() {
        assert_eq!(errand(&json!({"relation": rel(74, 2600), "kind": "student_loan", "place": "park"})).unwrap()["ok"], false);
        assert_eq!(errand(&json!({"relation": rel(74, 0), "kind": "student_loan", "place": "campus_ad"})).unwrap()["ok"], false);
        let paid = errand(&json!({"relation": rel(5000, 2600), "kind": "pay_tuition", "place": "campus_ad"})).unwrap();
        assert_eq!(paid["ok"], true);
    }

    #[test]
    fn she_hears_when_she_has_almost_no_money() {
        let poor = facts(&json!({"girl": "a", "relation": {"id": "a", "cash": 0}}));
        assert!(!poor["text"].as_str().unwrap().is_empty());
        assert_eq!(facts(&json!({"girl": "a", "relation": {"id": "a", "cash": 500}}))["text"], "");
        assert_eq!(facts(&json!({"girl": "a", "relation": {"id": "a", "cash": -1}}))["text"], "", "unknown wallet: nothing said");
    }

    #[test]
    fn she_hears_of_the_loan_while_she_owes_fees() {
        assert_eq!(facts(&json!({"girl": "a", "relation": {"debt": 0}}))["text"], "");
        assert!(!facts(&json!({"girl": "a", "relation": {"debt": 311}}))["text"].as_str().unwrap().is_empty());
    }
}
