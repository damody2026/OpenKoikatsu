//! Books being read (Docs/STREET_NEW_FEATURES_PLAN.md 2-4): who reads which book (an item tagged `book`), how far, and
//! the ones both have finished, which is what they can talk about. Reading moves on when he reads alone (the bag's
//! "read" button) and when they read together (read_books, library_read...: activities that yield book_progress).
//! What they say about a book is the AI's; here are only the facts it is told. The state is in the save's `ext`.
use crate::catalog::Catalog;
use serde_json::{json, Value};

/// Minutes a book takes from start to end.
const BOOK_MINUTES: f64 = 360.0;
const PICTURE_BOOK_MINUTES: f64 = 40.0;

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or_default()
}

fn rows(r: &Value) -> Vec<Value> {
    r.get("ext").and_then(|e| e.get("reading")).and_then(Value::as_array).cloned().unwrap_or_default()
}

fn with_rows(r: &Value, rows: Vec<Value>) -> Value {
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    ext["reading"] = Value::Array(rows);
    ext
}

fn is_book(c: &Catalog, item: &str) -> bool {
    c.items.iter().any(|i| i.id == item && i.tags.iter().any(|t| t == "book"))
}

fn length(c: &Catalog, item: &str) -> f64 {
    if c.items.iter().any(|i| i.id == item && i.tags.iter().any(|t| t == "picturebook")) {
        PICTURE_BOOK_MINUTES
    } else {
        BOOK_MINUTES
    }
}

fn name(c: &Catalog, item: &str) -> String {
    c.items.iter().find(|i| i.id == item).map(|i| i.name.clone()).unwrap_or_else(|| item.to_string())
}

/// Which book this reader is on: the one in progress (newest started), else the first unread one they hold.
fn current(rows: &[Value], who: &str, held: &[String]) -> Option<String> {
    if let Some(r) = rows.iter().rev().find(|r| s(r, "who") == who && r.get("progress").and_then(Value::as_f64).unwrap_or(0.0) < 100.0 && held.iter().any(|h| h == s(r, "book"))) {
        return Some(s(r, "book").to_string());
    }
    held.iter().find(|h| !rows.iter().any(|r| s(r, "who") == who && s(r, "book") == h.as_str())).cloned()
}

/// Move a reader on by some minutes; returns (book, progress, finished now).
fn advance(c: &Catalog, rows: &mut Vec<Value>, who: &str, held: &[String], minutes: f64, day: i64) -> Option<(String, f64, bool)> {
    let held: Vec<String> = held.iter().filter(|h| is_book(c, h)).cloned().collect();
    let book = current(rows, who, &held)?;
    let at = match rows.iter().position(|r| s(r, "who") == who && s(r, "book") == book) {
        Some(k) => k,
        None => {
            rows.push(json!({"who": who, "book": book, "progress": 0.0, "since": day}));
            rows.len() - 1
        }
    };
    let before = rows[at]["progress"].as_f64().unwrap_or(0.0);
    let after = (before + minutes / length(c, &book) * 100.0).min(100.0);
    let after = if after > 99.99 { 100.0 } else { after };
    rows[at]["progress"] = json!(after);
    let done = before < 100.0 && after >= 100.0;
    if done {
        rows[at]["finished"] = json!(day);
    }
    Some((book, after.floor(), done))
}

/// Read for a while. Request: ext, day, minutes, readers [{who: "him" or her id, books: [item ids they hold]}].
/// Answer: ext, read [{who, book, name, progress, finished}].
fn read(c: &Catalog, r: &Value) -> Value {
    let mut rs = rows(r);
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(0);
    let minutes = r.get("minutes").and_then(Value::as_f64).unwrap_or(0.0).max(0.0);
    let mut out = Vec::new();
    for reader in r.get("readers").and_then(Value::as_array).into_iter().flatten() {
        let who = s(reader, "who");
        // Books given, else hers from the things she owns (items.rs owned).
        let mut held: Vec<String> = reader.get("books").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str).map(str::to_string).collect();
        if reader.get("books").is_none() {
            held = r.get("ext").and_then(|e| e.get("items")).and_then(|i| i.get("owned")).and_then(|o| o.get(who)).and_then(Value::as_array).into_iter().flatten().map(|e| s(e, "item").to_string()).collect();
        }
        if let Some((book, progress, finished)) = advance(c, &mut rs, who, &held, minutes, day) {
            out.push(json!({"who": who, "book": book, "name": name(c, &book), "progress": progress, "finished": finished}));
        }
    }
    let notice: Vec<String> = out
        .iter()
        .filter(|o| s(o, "who") == "him")
        .map(|o| {
            if o["finished"] == true {
                crate::tr::m("reading.he_finished", &[("book", crate::tr::a(s(o, "name")))])
            } else {
                crate::tr::m("reading.he_read", &[("book", crate::tr::a(s(o, "name"))), ("progress", crate::tr::a(&(o["progress"].as_f64().unwrap_or(0.0) as i64).to_string()))])
            }
        })
        .collect();
    json!({"ext": with_rows(r, rs), "read": out, "notice": notice.join(crate::tr::t("items.fact_separator"))})
}

/// For her words: what she is reading, what he is reading, and the books both have finished (to talk about).
/// Request: ext, girl. Answer: text.
fn facts(c: &Catalog, r: &Value) -> Value {
    let rs = rows(r);
    let girl = s(r, "girl");
    let mut parts = Vec::new();
    let reading = |who: &str| rs.iter().rev().find(|x| s(x, "who") == who && x["progress"].as_f64().unwrap_or(0.0) < 100.0).cloned();
    if let Some(x) = reading(girl) {
        parts.push(crate::tr::m("reading.fact_she_reads", &[("book", crate::tr::a(&name(c, s(&x, "book")))), ("progress", crate::tr::a(&(x["progress"].as_f64().unwrap_or(0.0) as i64).to_string()))]));
    }
    if let Some(x) = reading("him") {
        parts.push(crate::tr::m("reading.fact_he_reads", &[("book", crate::tr::a(&name(c, s(&x, "book")))), ("progress", crate::tr::a(&(x["progress"].as_f64().unwrap_or(0.0) as i64).to_string()))]));
    }
    let done = |who: &str| rs.iter().filter(|x| s(x, "who") == who && x["progress"].as_f64().unwrap_or(0.0) >= 100.0).map(|x| s(x, "book").to_string()).collect::<Vec<_>>();
    let (hers, his) = (done(girl), done("him"));
    let both: Vec<String> = hers.iter().filter(|b| his.contains(b)).map(|b| name(c, b)).collect();
    if !both.is_empty() {
        parts.push(crate::tr::m("reading.fact_both_finished", &[("books", crate::tr::a(&both.join(crate::tr::t("items.separator"))))]));
    }
    json!({"text": parts.join(crate::tr::t("items.fact_separator"))})
}

pub fn eval(c: &Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(Ok(match kind {
        "reading_read" => read(c, r),
        "reading_facts" => facts(c, r),
        _ => return None,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn reading_together_until_both_finish_gives_them_the_book_to_talk_about() {
        let c = Tuning::builtin().catalog;
        let mut ext = json!({"items": {"a": 1}});
        // Hu Tao and he each hold the same novel; three sittings of two hours finish it.
        for k in 0..3 {
            let v = read(&c, &json!({"ext": ext, "day": k, "minutes": 120, "readers": [
                {"who": "him", "books": ["bk_novel_travel", "bk_notebook"]},
                {"who": "h", "books": ["bk_novel_travel"]}
            ]}));
            ext = v["ext"].clone();
            if k == 2 {
                assert_eq!(v["read"][0]["finished"], true);
                assert!(!v["notice"].as_str().unwrap().is_empty());
            }
        }
        assert_eq!(ext["items"]["a"], 1, "the rest of ext is kept");
        let f = facts(&c, &json!({"ext": ext, "girl": "h"}));
        let t = f["text"].as_str().unwrap();
        assert!(t.contains(&name(&c, "bk_novel_travel")), "{t}");
    }

    #[test]
    fn a_notebook_is_not_read_and_a_picture_book_is_quick() {
        let c = Tuning::builtin().catalog;
        let v = read(&c, &json!({"ext": {}, "day": 1, "minutes": 60, "readers": [{"who": "him", "books": ["bk_notebook"]}]}));
        assert_eq!(v["read"], json!([]));
        let v = read(&c, &json!({"ext": {}, "day": 1, "minutes": 60, "readers": [{"who": "e", "books": ["bk_pb_sheep"]}]}));
        assert_eq!(v["read"][0]["finished"], true);
    }
}
