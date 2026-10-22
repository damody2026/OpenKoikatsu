//! Editing a story file in place (the story editor and the MCP tools): the JSON is changed as JSON, keeping its key
//! order and every field this crate does not know, and written back only when the result is still a valid story.
use crate::script::Story;
use serde_json::{json, Map, Value};
use std::path::Path;

pub fn load(path: &Path) -> Result<Value, String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
    serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))
}

/// Validates and writes; nothing is written when the story would be invalid.
pub fn save(path: &Path, story: &Value) -> Result<(), String> {
    let text = serde_json::to_string_pretty(story).map_err(|e| e.to_string())? + "\n";
    Story::parse(&text)?;
    std::fs::write(path, text).map_err(|e| format!("{}: {e}", path.display()))
}

/// Errors of the file as it is (empty: valid).
pub fn validate(story: &Value) -> Vec<String> {
    match serde_json::from_value::<Story>(story.clone()) {
        Ok(s) => s.validate(),
        Err(e) => vec![e.to_string()],
    }
}

fn acts(story: &mut Value) -> Result<&mut Vec<Value>, String> {
    story.get_mut("acts").and_then(Value::as_array_mut).ok_or_else(|| "the story has no acts array".to_string())
}

/// (act index, line index) of a line id.
fn find_line(story: &Value, id: &str) -> Option<(usize, usize)> {
    story["acts"].as_array()?.iter().enumerate().find_map(|(a, act)| {
        act["lines"].as_array()?.iter().position(|l| l["id"] == id).map(|l| (a, l))
    })
}

fn find_act(story: &Value, id: &str) -> Option<usize> {
    story["acts"].as_array()?.iter().position(|a| a["id"] == id)
}

fn text_of(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        Value::Object(m) => m.get("zh").or_else(|| m.get("ja")).or_else(|| m.values().next()).and_then(Value::as_str).unwrap_or_default().to_string(),
        _ => String::new(),
    }
}

/// Acts and their lines in short: ids, speakers, the first words, choices.
pub fn outline(story: &Value) -> Value {
    let acts: Vec<Value> = story["acts"]
        .as_array()
        .map(|a| {
            a.iter()
                .map(|act| {
                    let lines: Vec<Value> = act["lines"]
                        .as_array()
                        .map(|l| {
                            l.iter()
                                .map(|line| {
                                    let choices: Vec<Value> = line["choices"]
                                        .as_array()
                                        .map(|c| c.iter().map(|c| json!({"text": text_of(&c["text"]), "goto": c["goto"]})).collect())
                                        .unwrap_or_default();
                                    let mut o = json!({"id": line["id"], "who": line.get("who").cloned().unwrap_or(json!("")), "text": text_of(&line["text"])});
                                    if !choices.is_empty() {
                                        o["choices"] = json!(choices);
                                    }
                                    if let Some(g) = line.get("goto") {
                                        o["goto"] = g.clone();
                                    }
                                    o
                                })
                                .collect()
                        })
                        .unwrap_or_default();
                    json!({"id": act["id"], "title": text_of(&act["title"]), "scene": act["scene"], "lines": lines})
                })
                .collect()
        })
        .unwrap_or_default();
    json!({"title": text_of(&story["title"]), "characters": story["characters"].as_object().map(|m| m.keys().cloned().collect::<Vec<_>>()).unwrap_or_default(), "acts": acts})
}

pub fn get_line(story: &Value, id: &str) -> Result<Value, String> {
    let (a, l) = find_line(story, id).ok_or_else(|| format!("no line {id}"))?;
    Ok(story["acts"][a]["lines"][l].clone())
}

/// Lays `fields` over a line (a null removes the field; "id" cannot change here).
pub fn set_line(story: &mut Value, id: &str, fields: &Map<String, Value>) -> Result<(), String> {
    let (a, l) = find_line(story, id).ok_or_else(|| format!("no line {id}"))?;
    let line = acts(story)?[a]["lines"][l].as_object_mut().ok_or("the line is not an object")?;
    for (k, v) in fields {
        if k == "id" {
            continue;
        }
        if v.is_null() {
            line.remove(k);
        } else {
            line.insert(k.clone(), v.clone());
        }
    }
    Ok(())
}

/// Inserts a line after the line `after`, or first in the act `after`.
pub fn add_line(story: &mut Value, after: &str, line: Value) -> Result<(), String> {
    let id = line["id"].as_str().filter(|s| !s.is_empty()).ok_or("the new line needs an id")?.to_string();
    if find_line(story, &id).is_some() || find_act(story, &id).is_some() {
        return Err(format!("{id} is already used"));
    }
    let (a, at) = match (find_line(story, after), find_act(story, after)) {
        (Some((a, l)), _) => (a, l + 1),
        (None, Some(a)) => (a, 0),
        _ => return Err(format!("no act or line {after}")),
    };
    let act = acts(story)?[a].as_object_mut().ok_or("the act is not an object")?;
    let lines = act.entry("lines").or_insert_with(|| json!([])).as_array_mut().ok_or("lines is not an array")?;
    lines.insert(at, line);
    Ok(())
}

pub fn delete_line(story: &mut Value, id: &str) -> Result<(), String> {
    let (a, l) = find_line(story, id).ok_or_else(|| format!("no line {id}"))?;
    acts(story)?[a]["lines"].as_array_mut().ok_or("lines is not an array")?.remove(l);
    Ok(())
}

/// Adds an act after the act `after` (or last).
pub fn add_act(story: &mut Value, after: Option<&str>, act: Value) -> Result<(), String> {
    let id = act["id"].as_str().filter(|s| !s.is_empty()).ok_or("the new act needs an id")?.to_string();
    if find_act(story, &id).is_some() || find_line(story, &id).is_some() {
        return Err(format!("{id} is already used"));
    }
    let at = match after {
        Some(a) => find_act(story, a).ok_or_else(|| format!("no act {a}"))? + 1,
        None => story["acts"].as_array().map(|a| a.len()).unwrap_or(0),
    };
    acts(story)?.insert(at, act);
    Ok(())
}

/// Lays `fields` over an act (its lines are edited line by line).
pub fn set_act(story: &mut Value, id: &str, fields: &Map<String, Value>) -> Result<(), String> {
    let a = find_act(story, id).ok_or_else(|| format!("no act {id}"))?;
    let act = acts(story)?[a].as_object_mut().ok_or("the act is not an object")?;
    for (k, v) in fields {
        if k == "id" || k == "lines" {
            continue;
        }
        if v.is_null() {
            act.remove(k);
        } else {
            act.insert(k.clone(), v.clone());
        }
    }
    Ok(())
}

/// A character entry (null removes it).
pub fn set_character(story: &mut Value, id: &str, def: Value) -> Result<(), String> {
    let chars = story.as_object_mut().ok_or("the story is not an object")?.entry("characters").or_insert_with(|| json!({}));
    let chars = chars.as_object_mut().ok_or("characters is not an object")?;
    if def.is_null() {
        chars.remove(id);
    } else {
        chars.insert(id.to_string(), def);
    }
    Ok(())
}

/// A new story with one act on a scene.
pub fn new_story(title: &str, scene: &str) -> Value {
    json!({
        "schema": 1,
        "title": {"zh": title},
        "characters": {},
        "items": {},
        "acts": [{"id": "act1", "title": {"zh": title}, "scene": scene, "camera": {"shot": "wide"}, "cast": {},
                  "lines": [{"id": "act1.1", "text": {"zh": "……"}}]}]
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Value {
        serde_json::from_str(include_str!("../tests/sample.okstory.json")).unwrap()
    }

    #[test]
    fn edits_keep_order_and_validity() {
        let mut s = sample();
        add_line(&mut s, "a1.l1", json!({"id": "a1.new", "who": "ayu", "text": {"zh": "新的一句"}})).unwrap();
        assert_eq!(s["acts"][0]["lines"][1]["id"], "a1.new");
        add_line(&mut s, "a2", json!({"id": "a2.first", "text": "旁白"})).unwrap();
        assert_eq!(s["acts"][1]["lines"][0]["id"], "a2.first");
        assert!(add_line(&mut s, "a1.l1", json!({"id": "a1.l2", "text": "x"})).is_err(), "ids stay unique");
        let mut f = Map::new();
        f.insert("face".into(), json!("shy"));
        f.insert("camera".into(), Value::Null);
        set_line(&mut s, "a1.l1", &f).unwrap();
        assert_eq!(get_line(&s, "a1.l1").unwrap()["face"], "shy");
        assert!(get_line(&s, "a1.l1").unwrap().get("camera").is_none());
        delete_line(&mut s, "a1.new").unwrap();
        assert!(get_line(&s, "a1.new").is_err());
        assert!(validate(&s).is_empty(), "{:?}", validate(&s));
        // Key order of an edited line is kept (id first).
        let text = serde_json::to_string(&s["acts"][0]["lines"][0]).unwrap();
        assert!(text.starts_with("{\"id\""), "{text}");
        let o = outline(&s);
        assert_eq!(o["acts"][0]["lines"][2]["choices"][0]["goto"], "a1.l4");
    }

    #[test]
    fn invalid_results_are_not_written() {
        let dir = std::env::temp_dir().join(format!("ok-story-edit-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("s.okstory.json");
        let mut s = sample();
        save(&file, &s).unwrap();
        let mut f = Map::new();
        f.insert("who".into(), json!("nobody"));
        set_line(&mut s, "a1.l1", &f).unwrap();
        assert!(save(&file, &s).is_err());
        assert_eq!(load(&file).unwrap(), sample(), "the file kept the valid story");
        let n = new_story("測試", "UserData/Studio/scene/x.okscene");
        assert!(validate(&n).is_empty());
        std::fs::remove_dir_all(&dir).ok();
    }
}
