//! Whose number he has, and who has his (STREET_SOCIAL_HEROINE_AGENT_PLAN 交換聯絡方式). That she gives hers or takes his
//! is the judge's reading (gives_number, takes_his_number); here it is recorded in the save's ext (contacts) and
//! asked. He calls and texts only those whose number he has; she writes or calls him only when she has his. A friend
//! or lover has both, and so has anyone he already wrote or called with (older saves).
use serde_json::{json, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

/// Request: ext, girl, stage, phone_history (they have messages or calls), way ("his": he reaches her, the default;
/// "hers": she reaches him). Answer: contact.
fn has(r: &Value) -> Value {
    let entry = r.get("ext").and_then(|e| e.get("contacts")).and_then(|c| c.get(s(r, "girl")));
    // An entry from before the two ways were kept ({day}) is both.
    let key = if s(r, "way") == "hers" { "his" } else { "hers" };
    let saved = entry.is_some_and(|e| e.get(key).and_then(Value::as_bool).unwrap_or(e.get("hers").is_none() && e.get("his").is_none()));
    let known = !matches!(s(r, "stage"), "" | "stranger");
    let history = r.get("phone_history").and_then(Value::as_bool).unwrap_or(false);
    json!({"contact": saved || known || history})
}

/// She gave him her number (what "hers") or took his (what "his"). Request: ext, girl, day, what. Answer: ext, added
/// (false when it was already so), fact (for her memory).
fn add(r: &Value) -> Value {
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    let girl = s(r, "girl");
    let what = if s(r, "what") == "his" { "his" } else { "hers" };
    if girl.is_empty() {
        return json!({"ext": ext, "added": false, "fact": ""});
    }
    let entry = ext.get("contacts").and_then(|c| c.get(girl)).cloned();
    let old_both = entry.as_ref().is_some_and(|e| e.get("hers").is_none() && e.get("his").is_none());
    if old_both || entry.as_ref().and_then(|e| e.get(what)).and_then(Value::as_bool).unwrap_or(false) {
        return json!({"ext": ext, "added": false, "fact": ""});
    }
    let mut e = entry.unwrap_or_else(|| json!({"day": r.get("day").and_then(Value::as_i64).unwrap_or(0)}));
    e[what] = json!(true);
    ext["contacts"][girl] = e;
    json!({"ext": ext, "added": true, "fact": crate::tr::t(if what == "his" { "contacts.took_his_number" } else { "contacts.gave_number" })})
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "phone_contact" => has(r),
        "contact_add" => add(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_stranger_is_not_in_his_phone_until_she_gives_her_number() {
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger"}))["contact"], false);
        assert_eq!(has(&json!({"girl": "a", "stage": "friend"}))["contact"], true);
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger", "phone_history": true}))["contact"], true);
        let a = add(&json!({"girl": "a", "day": 4}));
        assert_eq!(a["added"], true);
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger", "ext": a["ext"]}))["contact"], true);
        assert_eq!(add(&json!({"girl": "a", "ext": a["ext"]}))["added"], false, "given twice is once");
    }

    #[test]
    fn she_took_his_number_only_she_can_write() {
        let t = add(&json!({"girl": "a", "what": "his"}));
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger", "ext": t["ext"]}))["contact"], false, "he has not hers");
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger", "way": "hers", "ext": t["ext"]}))["contact"], true);
        let both = add(&json!({"girl": "a", "ext": t["ext"]}));
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger", "ext": both["ext"]}))["contact"], true);
        // Saved before the two ways were kept: both.
        assert_eq!(has(&json!({"girl": "a", "stage": "stranger", "way": "hers", "ext": {"contacts": {"a": {"day": 3}}}}))["contact"], true);
    }
}
