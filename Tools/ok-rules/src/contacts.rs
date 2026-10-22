//! Whose number he has (STREET_SOCIAL_HEROINE_AGENT_PLAN 交換聯絡方式). That she gives it is the judge's reading
//! (gives_number); here it is recorded in the save's ext (contacts) and asked: the phone lists, calls and texts only
//! those, both ways. A friend or lover has it, and so has anyone he already wrote or called with (older saves).
use serde_json::{json, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

/// Request: ext, girl, stage, phone_history (they have messages or calls). Answer: contact.
fn has(r: &Value) -> Value {
    let saved = r.get("ext").and_then(|e| e.get("contacts")).and_then(|c| c.get(s(r, "girl"))).is_some();
    let known = !matches!(s(r, "stage"), "" | "stranger");
    let history = r.get("phone_history").and_then(Value::as_bool).unwrap_or(false);
    json!({"contact": saved || known || history})
}

/// She gave him her number. Request: ext, girl, day. Answer: ext, added (false when he had it), fact (for her memory).
fn add(r: &Value) -> Value {
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    let girl = s(r, "girl");
    if girl.is_empty() || ext.get("contacts").and_then(|c| c.get(girl)).is_some() {
        return json!({"ext": ext, "added": false, "fact": ""});
    }
    ext["contacts"][girl] = json!({"day": r.get("day").and_then(Value::as_i64).unwrap_or(0)});
    json!({"ext": ext, "added": true, "fact": crate::tr::t("contacts.gave_number")})
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
}
