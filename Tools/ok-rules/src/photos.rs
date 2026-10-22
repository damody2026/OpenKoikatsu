//! Photos (Docs/STREET_NEW_FEATURES_PLAN.md 0-3): every picture taken is a record of who took it, where, when, who is
//! in it and what of, and whose album it is in. The picture itself is a PNG Unreal saved; the records are part of the
//! save's `ext` (rules-only state). A heroine takes one when the AI says so (judge she_photo) and sends one when it
//! says so (she_sends_photo); here it is filed, sent, listed, and told to her.
use serde_json::{json, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

fn n(r: &Value, k: &str) -> i64 {
    r.get(k).and_then(Value::as_i64).unwrap_or(0)
}

fn list(r: &Value) -> Vec<Value> {
    r.get("ext").and_then(|e| e.get("photos")).and_then(Value::as_array).cloned().unwrap_or_default()
}

fn with_list(r: &Value, photos: Vec<Value>) -> Value {
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    ext["photos"] = Value::Array(photos);
    ext
}

/// A picture was saved. Request: ext, file, day, minute, place, taker ("him" or her id), people [ids in it], topic,
/// album (a name for a set taken together, e.g. a photo walk; may be ""). It goes into the taker's album.
/// Answer: ext, id.
fn taken(r: &Value) -> Value {
    let mut photos = list(r);
    let id = photos.iter().filter_map(|p| p.get("id").and_then(Value::as_i64)).max().unwrap_or(0) + 1;
    let taker = if s(r, "taker").is_empty() { "him" } else { s(r, "taker") };
    photos.push(json!({
        "id": id, "file": s(r, "file"), "day": n(r, "day"), "minute": n(r, "minute"), "place": s(r, "place"),
        "taker": taker, "people": r.get("people").cloned().unwrap_or_else(|| json!([])), "topic": s(r, "topic"),
        "album": s(r, "album"), "shared_with": []
    }));
    json!({"ext": with_list(r, photos), "id": id})
}

/// Which photo: an id, or "latest" (the sender's newest one).
fn pick(photos: &[Value], which: &Value, from: &str) -> Option<usize> {
    if let Some(id) = which.as_i64() {
        return photos.iter().position(|p| p.get("id").and_then(Value::as_i64) == Some(id));
    }
    let topic = which.as_str().unwrap_or("latest");
    let mine = |p: &&Value| s(p, "taker") == from;
    if topic == "latest" || topic.is_empty() {
        return photos.iter().enumerate().filter(|(_, p)| mine(p)).map(|(k, _)| k).last();
    }
    // A topic as she said it ("the sunset one"): her newest whose topic shares it, else her newest.
    photos
        .iter()
        .enumerate()
        .filter(|(_, p)| mine(p) && (s(p, "topic").contains(topic) || topic.contains(s(p, "topic")) && !s(p, "topic").is_empty()))
        .map(|(k, _)| k)
        .last()
        .or_else(|| photos.iter().enumerate().filter(|(_, p)| mine(p)).map(|(k, _)| k).last())
}

/// One sends a photo to the other (her to him on the phone, or him to her). Request: ext, which (id or "latest" or a
/// topic), from ("him" or her id), to. Answer: ext, ok, id, file, caption (where, when, who: for the message and for
/// her to answer).
fn send(r: &Value) -> Value {
    let mut photos = list(r);
    let (from, to) = (s(r, "from"), s(r, "to"));
    let Some(k) = pick(&photos, r.get("which").unwrap_or(&json!("latest")), from) else {
        return json!({"ok": false, "ext": with_list(r, photos)});
    };
    let p = &mut photos[k];
    if let Some(shared) = p.get_mut("shared_with").and_then(Value::as_array_mut) {
        if !shared.iter().any(|v| v == to) {
            shared.push(json!(to));
        }
    }
    let caption = caption(p, r);
    let (id, file) = (p["id"].clone(), p["file"].clone());
    json!({"ok": true, "ext": with_list(r, photos), "id": id, "file": file, "caption": caption})
}

/// "中央公園，9/21 20:32，雷電將軍與你：夕陽" — place and people by the names Unreal sent (place_names, names).
fn caption(p: &Value, r: &Value) -> String {
    let place = r.get("place_names").and_then(|m| m.get(s(p, "place"))).and_then(Value::as_str).unwrap_or(s(p, "place"));
    let names = r.get("names").cloned().unwrap_or(Value::Null);
    let people: Vec<String> = p
        .get("people")
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(Value::as_str)
        .map(|id| if id == "him" { crate::tr::t("photos.him").to_string() } else { names.get(id).and_then(Value::as_str).unwrap_or(id).to_string() })
        .collect();
    let minute = n(p, "minute");
    let place = if place.is_empty() { crate::tr::t("photos.street") } else { place };
    let day = crate::calendar::date(n(p, "day") as i32, (minute as f64) / 60.0);
    let mut parts = vec![place.to_string(), format!("{}/{} {}:{:02}", day.month, day.day, minute / 60, minute % 60)];
    if !people.is_empty() {
        parts.push(people.join(crate::tr::t("items.separator")));
    }
    if !s(p, "topic").is_empty() {
        parts.push(s(p, "topic").to_string());
    }
    parts.join(crate::tr::t("photos.caption_separator"))
}

/// An album: the photos someone took or was sent, newest first. Request: ext, owner ("him" or her id), names,
/// place_names. Answer: photos [{id, file, caption, taker, album, sent}].
fn view(r: &Value) -> Value {
    let owner = s(r, "owner");
    let rows: Vec<Value> = list(r)
        .iter()
        .rev()
        .filter(|p| s(p, "taker") == owner || p.get("shared_with").and_then(Value::as_array).is_some_and(|a| a.iter().any(|v| v == owner)))
        .map(|p| json!({"id": p["id"], "file": p["file"], "caption": caption(p, r), "taker": p["taker"], "album": p["album"], "sent": s(p, "taker") != owner}))
        .collect();
    json!({"photos": rows})
}

/// For her words: the photos she took lately (topics), and how many there are of the two of them.
/// Request: ext, girl. Answer: text.
fn facts(r: &Value) -> Value {
    let girl = s(r, "girl");
    let photos = list(r);
    let hers: Vec<String> = photos.iter().rev().filter(|p| s(p, "taker") == girl && !s(p, "topic").is_empty()).take(4).map(|p| s(p, "topic").to_string()).collect();
    let together = photos
        .iter()
        .filter(|p| {
            let people = p.get("people").and_then(Value::as_array).cloned().unwrap_or_default();
            people.iter().any(|v| v == girl) && (people.iter().any(|v| v == "him") || s(p, "taker") == "him")
        })
        .count();
    let mut parts = Vec::new();
    if !hers.is_empty() {
        parts.push(crate::tr::m("photos.fact_hers", &[("topics", crate::tr::a(&hers.join(crate::tr::t("items.separator"))))]));
    }
    if together > 0 {
        parts.push(crate::tr::m("photos.fact_together", &[("count", crate::tr::a(&together.to_string()))]));
    }
    json!({"text": parts.join(crate::tr::t("items.fact_separator"))})
}

/// She finished a drawing (activities yielding "drawing": sketching). Its picture is filed like a photo she took, in an
/// album of her drawings, and the drawing is a thing she owns ("drawing:<photo id>"), to keep or give him.
/// Request: ext, girl, girl_name, day, minute, place, place_name, file. Answer: ext, id, item.
fn drawing(r: &Value) -> Value {
    let girl = s(r, "girl");
    let topic = crate::tr::m("photos.drawing_topic", &[("place", crate::tr::a(s(r, "place_name")))]);
    let mut req = r.clone();
    req["taker"] = json!(girl);
    req["people"] = json!([]);
    req["topic"] = json!(topic);
    req["album"] = json!("drawings");
    let filed = taken(&req);
    let id = filed["id"].as_i64().unwrap_or(0);
    let item = format!("drawing:{id}");
    let name = crate::tr::m("photos.drawing_name", &[("name", crate::tr::a(s(r, "girl_name"))), ("place", crate::tr::a(s(r, "place_name")))]);
    let ext = crate::items::add_owned(&filed["ext"], girl, &item, &name, "drawn", r.get("day").and_then(Value::as_i64).unwrap_or(0));
    json!({"ext": ext, "id": id, "item": item})
}

/// A drawing given to him is in his album too (items.rs she_gives of "drawing:<id>").
pub fn share(ext: &Value, id: i64, with: &str) -> Value {
    let r = json!({"ext": ext});
    let mut photos = list(&r);
    if let Some(p) = photos.iter_mut().find(|p| p.get("id").and_then(Value::as_i64) == Some(id)) {
        if let Some(a) = p.get_mut("shared_with").and_then(Value::as_array_mut) {
            if !a.iter().any(|v| v == with) {
                a.push(json!(with));
            }
        }
    }
    with_list(&r, photos)
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "drawing_made" => drawing(r),
        "photo_taken" => taken(r),
        "photo_send" => send(r),
        "photos_view" => view(r),
        "photos_facts" => facts(r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn her_photo_is_filed_sent_and_listed_in_his_album() {
        let a = taken(&json!({"ext": {"items": {"x": 1}}, "file": "a.png", "day": 20, "minute": 1230, "place": "park", "taker": "m7", "people": ["him"], "topic": "夕陽"}));
        assert_eq!(a["id"], 1);
        assert_eq!(a["ext"]["items"]["x"], 1, "the rest of ext is kept");
        let b = taken(&json!({"ext": a["ext"], "file": "b.png", "day": 20, "minute": 1240, "place": "park", "taker": "him", "people": ["m7"], "topic": ""}));
        assert_eq!(b["id"], 2);
        let sent = send(&json!({"ext": b["ext"], "which": "latest", "from": "m7", "to": "him", "names": {"m7": "三月七"}, "place_names": {"park": "中央公園"}}));
        assert_eq!(sent["ok"], true);
        assert_eq!(sent["file"], "a.png");
        assert!(sent["caption"].as_str().unwrap().contains("中央公園"));
        let album = view(&json!({"ext": sent["ext"], "owner": "him"}));
        let files: Vec<&str> = album["photos"].as_array().unwrap().iter().map(|p| p["file"].as_str().unwrap()).collect();
        assert_eq!(files, vec!["b.png", "a.png"]);
        assert_eq!(album["photos"][1]["sent"], true);
        let f = facts(&json!({"ext": sent["ext"], "girl": "m7"}));
        let t = f["text"].as_str().unwrap();
        assert!(t.contains("夕陽"));
    }

    #[test]
    fn nothing_to_send_when_she_took_none() {
        let v = send(&json!({"ext": {}, "which": "latest", "from": "m7", "to": "him"}));
        assert_eq!(v["ok"], false);
    }
}
