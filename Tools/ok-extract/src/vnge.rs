//! VNGE (Visual Novel Game Engine) scene compatibility for OK Studio:
//! - Actor & Prop tag registration (`-actor:<alias>`, `-prop:<alias>`, `-propchild:<alias>`, `-propgrandpa:<alias>`, `:vnscenescript:`)
//! - `libjsoncoder` (`__tuple__`, `__Vector3__`, `__Vector2__`, `__Quaternion__`, `__Color__`) & Python literal parser
//! - Scene Save State (`vnge_sss`, `vnge_sssadv`, `vnge_actor`, `-scenesavestate:`)
//! - Keyframe Animation v1 & v2 (`vnge_kfa`, `vnge_kfa2`, `-keyframeclips:`) with cubic Bezier interpolation, FK, IK, morphs, and camera
//! - Visual Novel Scene Script (`vnge_vnss`, `vnge_sound`, `:vnscenescript:`)

use std::collections::{BTreeMap, BTreeSet};
use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::card::map_get;
use crate::studio_scene::{Object, Scene};

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeActorTag {
    pub alias: String,
    pub key: i32,
    pub sex: i32,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub color: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub title: String,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeRegistry {
    pub actors: BTreeMap<String, i32>,
    pub actor_tags: Vec<VngeActorTag>,
    pub props: BTreeMap<String, i32>,
    #[serde(skip)]
    pub prop_fk_bones: BTreeMap<i32, Vec<String>>,
    #[serde(skip)]
    pub all_char_keys: Vec<i32>,
    #[serde(skip)]
    pub all_object_keys: BTreeSet<i32>,
}

impl VngeRegistry {
    pub fn resolve_actor(&self, alias: &str) -> Option<i32> {
        if let Some(&k) = self.actors.get(alias) {
            return Some(k);
        }
        if let Ok(k) = alias.parse::<i32>() {
            if self.all_char_keys.contains(&k) {
                return Some(k);
            }
        }
        // MMDD autoRegistChara uses "act0", "act1", ... when untagged, or "f1", "f2", ...
        if let Some(idx) = alias.strip_prefix("act").and_then(|s| s.parse::<usize>().ok()) {
            if let Some(&k) = self.all_char_keys.get(idx) {
                return Some(k);
            }
        }
        None
    }

    pub fn resolve_prop(&self, alias: &str) -> Option<i32> {
        if let Some(&k) = self.props.get(alias) {
            return Some(k);
        }
        if let Ok(k) = alias.parse::<i32>() {
            if self.all_object_keys.contains(&k) {
                return Some(k);
            }
        }
        None
    }

    pub fn resolve_any(&self, alias: &str) -> Option<i32> {
        self.resolve_actor(alias).or_else(|| self.resolve_prop(alias))
    }
}

pub fn object_key(o: &Object) -> i32 {
    match o {
        Object::Character { key, .. }
        | Object::Item { key, .. }
        | Object::Light { key, .. }
        | Object::Folder { key, .. }
        | Object::Route { key, .. }
        | Object::Camera { key, .. }
        | Object::Text { key, .. } => *key,
    }
}

/// Scans the Studio scene object tree and registers all VNGE / MMDD actor and prop aliases.
pub fn register_actors_props(scene: &Scene) -> VngeRegistry {
    let mut reg = VngeRegistry::default();

    fn scan_char_children(
        char_key: i32,
        sex: i32,
        objs: &[Object],
        parent_key: Option<i32>,
        grandparent_key: Option<i32>,
        reg: &mut VngeRegistry,
    ) {
        for o in objs {
            reg.all_object_keys.insert(object_key(o));
            match o {
                Object::Folder { key, name, children, .. } => {
                    let trimmed = name.trim();
                    if let Some(rest) = trimmed.strip_prefix("-actor:") {
                        let parts: Vec<&str> = rest.split(':').collect();
                        let alias = parts.first().copied().unwrap_or("").trim().to_string();
                        if !alias.is_empty() {
                            let color = parts.get(1).copied().unwrap_or("").trim().to_string();
                            let title = parts.get(2).copied().unwrap_or("").trim().to_string();
                            reg.actors.insert(alias.clone(), char_key);
                            if !reg.actor_tags.iter().any(|t| t.alias == alias) {
                                reg.actor_tags.push(VngeActorTag { alias, key: char_key, sex, color, title });
                            }
                        }
                    } else if trimmed.starts_with(":vnscenescript:v") {
                        for child in children {
                            if let Object::Folder { name: cname, .. } = child {
                                let ct = cname.trim();
                                if ct.starts_with(":f") || ct.starts_with(":m") {
                                    let alias = ct[1..].trim().to_string();
                                    if !alias.is_empty() {
                                        reg.actors.insert(alias.clone(), char_key);
                                        if !reg.actor_tags.iter().any(|t| t.alias == alias) {
                                            reg.actor_tags.push(VngeActorTag {
                                                alias,
                                                key: char_key,
                                                sex,
                                                color: String::new(),
                                                title: String::new(),
                                            });
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // Also check prop tags inside character accessory hierarchies
                    scan_prop_folder(*key, trimmed, children, parent_key, grandparent_key, reg);
                    scan_char_children(char_key, sex, children, Some(*key), parent_key, reg);
                }
                Object::Item { key, fk, children, .. } => {
                    if !fk.is_empty() {
                        reg.prop_fk_bones.insert(*key, fk.iter().map(|(n, _)| n.clone()).collect());
                    }
                    scan_char_children(char_key, sex, children, Some(*key), parent_key, reg);
                }
                Object::Route { key, children, .. } => {
                    scan_char_children(char_key, sex, children, Some(*key), parent_key, reg);
                }
                Object::Character { key: ckey, sex: csex, children: cslots, .. } => {
                    reg.all_char_keys.push(*ckey);
                    for (_, slot_objs) in cslots {
                        scan_char_children(*ckey, *csex, slot_objs, Some(*ckey), parent_key, reg);
                    }
                }
                _ => {}
            }
        }
    }

    fn scan_prop_folder(
        folder_key: i32,
        trimmed: &str,
        children: &[Object],
        parent_key: Option<i32>,
        grandparent_key: Option<i32>,
        reg: &mut VngeRegistry,
    ) {
        if let Some(rest) = trimmed.strip_prefix("-prop:") {
            let alias = rest.split(':').next().unwrap_or("").trim().to_string();
            if !alias.is_empty() {
                // vngameengine.py uses children[0]; mmddirector.py autoRegistProp puts an empty "-prop:<alias>" folder inside the target prop.
                let target = children
                    .first()
                    .map(object_key)
                    .or(parent_key)
                    .unwrap_or(folder_key);
                reg.props.insert(alias, target);
            }
        } else if let Some(rest) = trimmed.strip_prefix("-propchild:") {
            let alias = rest.split(':').next().unwrap_or("").trim().to_string();
            if !alias.is_empty() {
                reg.props.insert(alias, parent_key.unwrap_or(folder_key));
            }
        } else if let Some(rest) = trimmed.strip_prefix("-propgrandpa:") {
            let alias = rest.split(':').next().unwrap_or("").trim().to_string();
            if !alias.is_empty() {
                reg.props.insert(alias, grandparent_key.or(parent_key).unwrap_or(folder_key));
            }
        } else if trimmed.starts_with(":vnscenescript:v") {
            for child in children {
                if let Object::Folder { name: cname, children: cchildren, .. } = child {
                    let ct = cname.trim();
                    if ct.starts_with(":p") {
                        let alias = ct[1..].trim().to_string();
                        if !alias.is_empty() {
                            if let Some(first) = cchildren.first() {
                                reg.props.insert(alias, object_key(first));
                            }
                        }
                    }
                }
            }
        }
    }

    fn walk_tree(
        objs: &[Object],
        parent_key: Option<i32>,
        grandparent_key: Option<i32>,
        reg: &mut VngeRegistry,
    ) {
        for o in objs {
            let k = object_key(o);
            reg.all_object_keys.insert(k);
            match o {
                Object::Character { key, sex, children, .. } => {
                    reg.all_char_keys.push(*key);
                    for (_, slot_objs) in children {
                        scan_char_children(*key, *sex, slot_objs, Some(*key), parent_key, reg);
                    }
                }
                Object::Item { key, fk, children, .. } => {
                    if !fk.is_empty() {
                        reg.prop_fk_bones.insert(*key, fk.iter().map(|(n, _)| n.clone()).collect());
                    }
                    walk_tree(children, Some(*key), parent_key, reg);
                }
                Object::Folder { key, name, children, .. } => {
                    scan_prop_folder(*key, name.trim(), children, parent_key, grandparent_key, reg);
                    walk_tree(children, Some(*key), parent_key, reg);
                }
                Object::Route { key, children, .. } => {
                    walk_tree(children, Some(*key), parent_key, reg);
                }
                _ => {}
            }
        }
    }

    walk_tree(&scene.objects, None, None, &mut reg);
    reg
}

/// Unwraps `libjsoncoder.SceneEncoder` special objects (`__tuple__`, `__Vector3__`, `__Vector2__`, `__Quaternion__`, `__Color__`).
pub fn unwrap_scene_decoder(v: Value) -> Value {
    match v {
        Value::Array(arr) => Value::Array(arr.into_iter().map(unwrap_scene_decoder).collect()),
        Value::Object(mut map) => {
            if map.get("__tuple__").and_then(Value::as_bool) == Some(true) {
                if let Some(Value::Array(items)) = map.remove("items") {
                    return Value::Array(items.into_iter().map(unwrap_scene_decoder).collect());
                }
            }
            if map.get("__Vector3__").and_then(Value::as_bool) == Some(true) {
                let x = map.get("x").and_then(Value::as_f64).unwrap_or(0.0);
                let y = map.get("y").and_then(Value::as_f64).unwrap_or(0.0);
                let z = map.get("z").and_then(Value::as_f64).unwrap_or(0.0);
                return serde_json::json!([x, y, z]);
            }
            if map.get("__Vector2__").and_then(Value::as_bool) == Some(true) {
                let x = map.get("x").and_then(Value::as_f64).unwrap_or(0.0);
                let y = map.get("y").and_then(Value::as_f64).unwrap_or(0.0);
                return serde_json::json!([x, y]);
            }
            if map.get("__Quaternion__").and_then(Value::as_bool) == Some(true) {
                let x = map.get("x").and_then(Value::as_f64).unwrap_or(0.0);
                let y = map.get("y").and_then(Value::as_f64).unwrap_or(0.0);
                let z = map.get("z").and_then(Value::as_f64).unwrap_or(0.0);
                let w = map.get("w").and_then(Value::as_f64).unwrap_or(1.0);
                return serde_json::json!([x, y, z, w]);
            }
            if map.get("__Color__").and_then(Value::as_bool) == Some(true) {
                let r = map.get("r").and_then(Value::as_f64).unwrap_or(0.0);
                let g = map.get("g").and_then(Value::as_f64).unwrap_or(0.0);
                let b = map.get("b").and_then(Value::as_f64).unwrap_or(0.0);
                let a = map.get("a").and_then(Value::as_f64).unwrap_or(1.0);
                return serde_json::json!([r, g, b, a]);
            }
            let mut out = serde_json::Map::with_capacity(map.len());
            for (k, val) in map {
                out.insert(k, unwrap_scene_decoder(val));
            }
            Value::Object(out)
        }
        other => other,
    }
}

/// Parses either a JSON string (with `SceneEncoder` extensions) or an IronPython / Python literal string (`repr`/`str`) into `serde_json::Value`.
pub fn parse_py_or_json(input: &str) -> Result<Value, String> {
    let trimmed = input.trim().trim_start_matches('\u{feff}');
    if trimmed.is_empty() {
        return Ok(Value::Null);
    }
    if let Ok(v) = serde_json::from_str::<Value>(trimmed) {
        return Ok(unwrap_scene_decoder(v));
    }
    let mut p = PyParser { s: trimmed.as_bytes(), pos: 0 };
    let v = p.parse_value()?;
    Ok(unwrap_scene_decoder(v))
}

struct PyParser<'a> {
    s: &'a [u8],
    pos: usize,
}

impl<'a> PyParser<'a> {
    fn skip_ws(&mut self) {
        while self.pos < self.s.len() {
            match self.s[self.pos] {
                b' ' | b'\t' | b'\r' | b'\n' => self.pos += 1,
                b'#' => {
                    while self.pos < self.s.len() && self.s[self.pos] != b'\n' {
                        self.pos += 1;
                    }
                }
                _ => break,
            }
        }
    }

    fn peek(&mut self) -> Option<u8> {
        self.skip_ws();
        self.s.get(self.pos).copied()
    }

    fn parse_value(&mut self) -> Result<Value, String> {
        let Some(c) = self.peek() else {
            return Err("unexpected EOF".into());
        };
        match c {
            b'{' => self.parse_dict_or_set(),
            b'[' => self.parse_list(b']'),
            b'(' => self.parse_list(b')'),
            b'\'' | b'"' => self.parse_string().map(Value::String),
            b'u' | b'U' | b'b' | b'B' | b'r' | b'R'
                if self.s.get(self.pos + 1).is_some_and(|& n| n == b'\'' || n == b'"') =>
            {
                self.pos += 1;
                self.parse_string().map(Value::String)
            }
            b'0'..=b'9' | b'-' | b'+' | b'.' => self.parse_number(),
            _ => self.parse_ident_or_constructor(),
        }
    }

    fn parse_dict_or_set(&mut self) -> Result<Value, String> {
        self.pos += 1; // '{'
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Ok(Value::Object(serde_json::Map::new()));
        }
        let first = self.parse_value()?;
        if self.peek() == Some(b':') {
            self.pos += 1;
            let first_val = self.parse_value()?;
            let mut map = serde_json::Map::new();
            map.insert(val_to_key(&first), first_val);
            while self.peek() == Some(b',') {
                self.pos += 1;
                if self.peek() == Some(b'}') {
                    break;
                }
                let k = self.parse_value()?;
                if self.peek() != Some(b':') {
                    return Err(format!("expected ':' in dict at {}", self.pos));
                }
                self.pos += 1;
                let v = self.parse_value()?;
                map.insert(val_to_key(&k), v);
            }
            if self.peek() == Some(b'}') {
                self.pos += 1;
            }
            Ok(Value::Object(map))
        } else {
            // Python set literal {a, b, c}
            let mut arr = vec![first];
            while self.peek() == Some(b',') {
                self.pos += 1;
                if self.peek() == Some(b'}') {
                    break;
                }
                arr.push(self.parse_value()?);
            }
            if self.peek() == Some(b'}') {
                self.pos += 1;
            }
            Ok(Value::Array(arr))
        }
    }

    fn parse_list(&mut self, close: u8) -> Result<Value, String> {
        self.pos += 1; // '[' or '('
        let mut arr = Vec::new();
        if self.peek() == Some(close) {
            self.pos += 1;
            return Ok(Value::Array(arr));
        }
        loop {
            if self.peek() == Some(close) {
                break;
            }
            arr.push(self.parse_value()?);
            if self.peek() == Some(b',') {
                self.pos += 1;
            } else {
                break;
            }
        }
        if self.peek() == Some(close) {
            self.pos += 1;
        }
        Ok(Value::Array(arr))
    }

    fn parse_string(&mut self) -> Result<String, String> {
        let quote = self.s[self.pos];
        self.pos += 1;
        let triple = self.s.get(self.pos..self.pos + 2) == Some(&[quote, quote]);
        if triple {
            self.pos += 2;
        }
        let mut out = String::new();
        while self.pos < self.s.len() {
            if triple {
                if self.s.get(self.pos..self.pos + 3) == Some(&[quote, quote, quote]) {
                    self.pos += 3;
                    return Ok(out);
                }
            } else if self.s[self.pos] == quote {
                self.pos += 1;
                return Ok(out);
            }
            let b = self.s[self.pos];
            if b == b'\\' {
                self.pos += 1;
                if self.pos >= self.s.len() {
                    break;
                }
                let esc = self.s[self.pos];
                self.pos += 1;
                match esc {
                    b'n' => out.push('\n'),
                    b'r' => out.push('\r'),
                    b't' => out.push('\t'),
                    b'\\' => out.push('\\'),
                    b'\'' => out.push('\''),
                    b'"' => out.push('"'),
                    b'x' => {
                        if let Some(hex) = self.s.get(self.pos..self.pos + 2).and_then(|h| std::str::from_utf8(h).ok()) {
                            if let Ok(v) = u8::from_str_radix(hex, 16) {
                                out.push(v as char);
                                self.pos += 2;
                            }
                        }
                    }
                    b'u' => {
                        if let Some(hex) = self.s.get(self.pos..self.pos + 4).and_then(|h| std::str::from_utf8(h).ok()) {
                            if let Ok(v) = u32::from_str_radix(hex, 16) {
                                if let Some(ch) = char::from_u32(v) {
                                    out.push(ch);
                                }
                                self.pos += 4;
                            }
                        }
                    }
                    other => {
                        out.push(other as char);
                    }
                }
            } else {
                // Decode UTF-8 multi-byte char
                let rest = &self.s[self.pos..];
                let s = std::str::from_utf8(rest).unwrap_or_else(|e| {
                    let valid = e.valid_up_to();
                    if valid > 0 {
                        std::str::from_utf8(&rest[..valid]).unwrap_or("")
                    } else {
                        ""
                    }
                });
                if let Some(ch) = s.chars().next() {
                    out.push(ch);
                    self.pos += ch.len_utf8();
                } else {
                    out.push(b as char);
                    self.pos += 1;
                }
            }
        }
        Ok(out)
    }

    fn parse_number(&mut self) -> Result<Value, String> {
        let start = self.pos;
        if self.s.get(self.pos).is_some_and(|&b| b == b'+' || b == b'-') {
            self.pos += 1;
        }
        let mut is_float = false;
        while self.pos < self.s.len() {
            match self.s[self.pos] {
                b'0'..=b'9' => self.pos += 1,
                b'.' | b'e' | b'E' => {
                    is_float = true;
                    self.pos += 1;
                    if self.s.get(self.pos).is_some_and(|&b| b == b'+' || b == b'-') {
                        self.pos += 1;
                    }
                }
                b'L' | b'l' => {
                    self.pos += 1;
                    break;
                }
                _ => break,
            }
        }
        let raw = std::str::from_utf8(&self.s[start..self.pos])
            .unwrap_or("0")
            .trim_end_matches(['L', 'l'])
            .trim_start_matches('+');
        if !is_float {
            if let Ok(i) = raw.parse::<i64>() {
                return Ok(Value::Number(i.into()));
            }
        }
        let f = raw.parse::<f64>().unwrap_or(0.0);
        Ok(serde_json::Number::from_f64(if f.is_finite() { f } else { 0.0 })
            .map(Value::Number)
            .unwrap_or(Value::Null))
    }

    fn parse_ident_or_constructor(&mut self) -> Result<Value, String> {
        let start = self.pos;
        while self.pos < self.s.len() {
            let b = self.s[self.pos];
            if b.is_ascii_alphanumeric() || b == b'_' || b == b'.' {
                self.pos += 1;
            } else {
                break;
            }
        }
        if self.pos == start {
            // Skip unexpected token safely
            self.pos += 1;
            return Ok(Value::Null);
        }
        let ident = std::str::from_utf8(&self.s[start..self.pos]).unwrap_or("");
        match ident {
            "True" | "true" => Ok(Value::Bool(true)),
            "False" | "false" => Ok(Value::Bool(false)),
            "None" | "null" | "nan" | "NaN" | "inf" | "-inf" => Ok(Value::Null),
            _ => {
                // Constructor call such as Vector3(x, y, z), Quaternion(x, y, z, w), Color(r, g, b, a), tuple([...])
                if self.peek() == Some(b'(') {
                    let args = self.parse_list(b')')?;
                    if ident == "tuple" || ident == "list" || ident == "set" {
                        if let Some(first) = args.as_array().and_then(|a| a.first()) {
                            return Ok(first.clone());
                        }
                    }
                    return Ok(args);
                }
                Ok(Value::String(ident.to_string()))
            }
        }
    }
}

fn val_to_key(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        Value::Number(n) => n.to_string(),
        Value::Bool(b) => b.to_string(),
        Value::Array(a) => {
            let inner: Vec<String> = a.iter().map(val_to_key).collect();
            format!("({})", inner.join(","))
        }
        _ => String::new(),
    }
}

/// Extracts a VNGE plugin payload from the scene's `KKEx` ExtendedSave block (`vngameengine.dataPNG_load`).
pub fn data_png_load(blocks: &rmpv::Value, plugin_id: &str) -> Option<(String, String, Option<Vec<String>>)> {
    let plugin = map_get(blocks, plugin_id)?;
    let data = plugin.as_array().and_then(|a| a.get(1)).unwrap_or(plugin);
    let version = map_get(data, "version")
        .and_then(|v| v.as_str().map(str::to_owned))
        .unwrap_or_else(|| "1.0".into());
    let main = map_get(data, "main")
        .and_then(|v| {
            v.as_str()
                .map(str::to_owned)
                .or_else(|| v.as_slice().map(|b| String::from_utf8_lossy(b).into_owned()))
        })
        .unwrap_or_default();
    let list = map_get(data, "list")
        .and_then(rmpv::Value::as_array)
        .map(|arr| {
            arr.iter()
                .filter_map(|v| {
                    v.as_str()
                        .map(str::to_owned)
                        .or_else(|| v.as_slice().map(|b| String::from_utf8_lossy(b).into_owned()))
                })
                .collect::<Vec<_>>()
        });
    if main.is_empty() && list.as_ref().is_none_or(Vec::is_empty) {
        return None;
    }
    Some((version, main, list))
}

/// VNGE cubic Bezier interpolation (`vnanime.py` / `vnanime2.py`: `curve = [x1, x2, y1, y2]`).
pub fn cubic_bezier_interpolate(x1: f32, x2: f32, y1: f32, y2: f32, progress: f32) -> f32 {
    if progress <= 0.0 {
        return 0.0;
    }
    if progress >= 1.0 {
        return 1.0;
    }
    if (x1 - y1).abs() < 1e-5 && (x2 - y2).abs() < 1e-5 {
        return progress;
    }
    let eval = |t: f32, p1: f32, p2: f32| -> f32 {
        let a = 1.0 - 3.0 * p2 + 3.0 * p1;
        let b = 3.0 * p2 - 6.0 * p1;
        let c = 3.0 * p1;
        ((a * t + b) * t + c) * t
    };
    let deriv = |t: f32, p1: f32, p2: f32| -> f32 {
        let a = 1.0 - 3.0 * p2 + 3.0 * p1;
        let b = 3.0 * p2 - 6.0 * p1;
        let c = 3.0 * p1;
        (3.0 * a * t + 2.0 * b) * t + c
    };
    let mut lo = 0.0f32;
    let mut hi = 1.0f32;
    let mut t = progress;
    for _ in 0..12 {
        let x = eval(t, x1, x2) - progress;
        if x.abs() < 1e-6 {
            break;
        }
        if x < 0.0 {
            lo = t;
        } else {
            hi = t;
        }
        let d = deriv(t, x1, x2);
        if d.abs() > 1e-5 {
            let next = t - x / d;
            if next > lo && next < hi {
                t = next;
                continue;
            }
        }
        t = 0.5 * (lo + hi);
    }
    eval(t, y1, y2)
}

/// Maps a VNGE IK target name (`ikUni["uni"]` or `ikUni["charastudio"]` or `ikUni["neo"]`) to KK Studio's IK target index `0..12`,
/// along with `(Effector, Part, bUseRotation)` matching `OKStudioImport.cpp`.
pub fn ik_target_mapping(name: &str) -> Option<(i32, i32, i32, bool)> {
    // Map[13] in OKStudioImport.cpp:
    // 0: hips (Effector 0, Part 0)
    // 1: arm_l_s (Effector 1, Part 4)
    // 2: arm_l_e (Effector 9, Part 4)
    // 3: arm_l_h (Effector 5, Part 4, rot)
    // 4: arm_r_s (Effector 2, Part 3)
    // 5: arm_r_e (Effector 10, Part 3)
    // 6: arm_r_h (Effector 6, Part 3, rot)
    // 7: leg_l_t (Effector 3, Part 2)
    // 8: leg_l_k (Effector 11, Part 2)
    // 9: leg_l_f (Effector 7, Part 2, rot)
    // 10: leg_r_t (Effector 4, Part 1)
    // 11: leg_r_k (Effector 12, Part 1)
    // 12: leg_r_f (Effector 8, Part 1, rot)
    let no = match name.trim() {
        "0" | "body" | "ik_torso" | "cf_j_hips" | "cf_t_hips" | "cf_J_Hips" | "cm_J_Hips" => 0,
        "1" | "arm_l_s" | "ik_left_arm" | "cf_j_arm00_L" | "cf_t_shoulder_L" | "cf_J_ArmUp00_L" | "cm_J_ArmUp00_L" => 1,
        "2" | "arm_l_e" | "ik_left_forearm" | "cf_j_forearm01_L" | "cf_t_elbo_L" | "cf_J_ArmLow01_L" | "cm_J_ArmLow01_L" => 2,
        "3" | "arm_l_h" | "ik_left_hand" | "cf_j_hand_L" | "cf_t_hand_L" | "cf_J_Hand_L" | "cm_J_Hand_L" => 3,
        "4" | "arm_r_s" | "ik_right_arm" | "cf_j_arm00_R" | "cf_t_shoulder_R" | "cf_J_ArmUp00_R" | "cm_J_ArmUp00_R" => 4,
        "5" | "arm_r_e" | "ik_right_forearm" | "cf_j_forearm01_R" | "cf_t_elbo_R" | "cf_J_ArmLow01_R" | "cm_J_ArmLow01_R" => 5,
        "6" | "arm_r_h" | "ik_right_hand" | "cf_j_hand_R" | "cf_t_hand_R" | "cf_J_Hand_R" | "cm_J_Hand_R" => 6,
        "7" | "leg_l_t" | "ik_left_thigh" | "cf_j_thigh00_L" | "cf_t_waist_L" | "cf_J_LegUp00_L" | "cm_J_LegUp00_L" => 7,
        "8" | "leg_l_k" | "ik_left_knee" | "cf_j_leg01_L" | "cf_t_knee_L" | "cf_J_LegLow01_L" | "cm_J_LegLow01_L" => 8,
        "9" | "leg_l_f" | "ik_left_foot" | "cf_j_leg03_L" | "cf_t_leg_L" | "cf_J_Foot01_L" | "cm_J_Foot01_L" => 9,
        "10" | "leg_r_t" | "ik_right_thigh" | "cf_j_thigh00_R" | "cf_t_waist_R" | "cf_J_LegUp00_R" | "cm_J_LegUp00_R" => 10,
        "11" | "leg_r_k" | "ik_right_knee" | "cf_j_leg01_R" | "cf_t_knee_R" | "cf_J_LegLow01_R" | "cm_J_LegLow01_R" => 11,
        "12" | "leg_r_f" | "ik_right_foot" | "cf_j_leg03_R" | "cf_t_leg_R" | "cf_J_Foot01_R" | "cm_J_Foot01_R" => 12,
        _ => return None,
    };
    const MAP: [(i32, i32); 13] = [
        (0, 0),
        (1, 4),
        (9, 4),
        (5, 4),
        (2, 3),
        (10, 3),
        (6, 3),
        (3, 2),
        (11, 2),
        (7, 2),
        (4, 1),
        (12, 1),
        (8, 1),
    ];
    let (eff, part) = MAP[no as usize];
    let use_rot = matches!(no, 3 | 6 | 9 | 12);
    Some((no, eff, part, use_rot))
}

pub fn as_f32(v: &Value) -> Option<f32> {
    v.as_f64()
        .map(|f| f as f32)
        .or_else(|| v.as_i64().map(|i| i as f32))
        .or_else(|| v.as_str().and_then(|s| s.trim().parse::<f32>().ok()))
}

pub fn as_i32(v: &Value) -> Option<i32> {
    v.as_i64()
        .map(|i| i as i32)
        .or_else(|| v.as_f64().map(|f| f as i32))
        .or_else(|| v.as_str().and_then(|s| s.trim().parse::<i32>().ok()))
}

pub fn as_bool(v: &Value) -> Option<bool> {
    v.as_bool()
        .or_else(|| v.as_i64().map(|i| i != 0))
        .or_else(|| {
            v.as_str().map(|s| {
                let s = s.trim();
                s.eq_ignore_ascii_case("true") || s == "1"
            })
        })
}

pub fn as_vec3(v: &Value) -> Option<[f32; 3]> {
    if let Some(arr) = v.as_array() {
        if arr.len() >= 3 {
            return Some([as_f32(&arr[0])?, as_f32(&arr[1])?, as_f32(&arr[2])?]);
        }
    } else if let Some(obj) = v.as_object() {
        return Some([
            obj.get("x").and_then(as_f32)?,
            obj.get("y").and_then(as_f32)?,
            obj.get("z").and_then(as_f32)?,
        ]);
    } else if let Some(s) = v.as_str() {
        if let Ok(parsed) = parse_py_or_json(s) {
            if !parsed.is_string() {
                return as_vec3(&parsed);
            }
        }
    }
    None
}

/// Converts a value that is either a 3-element Euler degrees `[rx, ry, rz]` or a 4-element Unity quaternion `[qx, qy, qz, qw]`
/// into Unity Euler degrees `[rx, ry, rz]`.
pub fn as_unity_euler(v: &Value) -> Option<[f32; 3]> {
    if let Some(arr) = v.as_array() {
        if arr.len() == 4 {
            let q = [
                as_f32(&arr[0])?,
                as_f32(&arr[1])?,
                as_f32(&arr[2])?,
                as_f32(&arr[3])?,
            ];
            return Some(crate::pose::quat_to_unity_euler(q));
        } else if arr.len() >= 3 {
            return Some([as_f32(&arr[0])?, as_f32(&arr[1])?, as_f32(&arr[2])?]);
        }
    } else if let Some(obj) = v.as_object() {
        if let (Some(x), Some(y), Some(z), Some(w)) = (
            obj.get("x").and_then(as_f32),
            obj.get("y").and_then(as_f32),
            obj.get("z").and_then(as_f32),
            obj.get("w").and_then(as_f32),
        ) {
            return Some(crate::pose::quat_to_unity_euler([x, y, z, w]));
        }
        if let (Some(x), Some(y), Some(z)) = (
            obj.get("x").and_then(as_f32),
            obj.get("y").and_then(as_f32),
            obj.get("z").and_then(as_f32),
        ) {
            return Some([x, y, z]);
        }
    } else if let Some(s) = v.as_str() {
        if let Ok(parsed) = parse_py_or_json(s) {
            if !parsed.is_string() {
                return as_unity_euler(&parsed);
            }
        }
    }
    None
}

pub fn as_unity_quat(v: &Value) -> Option<[f32; 4]> {
    if let Some(arr) = v.as_array() {
        if arr.len() == 4 {
            return Some(crate::finalik::qnorm([
                as_f32(&arr[0])?,
                as_f32(&arr[1])?,
                as_f32(&arr[2])?,
                as_f32(&arr[3])?,
            ]));
        } else if arr.len() == 3 {
            let e = [as_f32(&arr[0])?, as_f32(&arr[1])?, as_f32(&arr[2])?];
            return Some(crate::pose::euler_deg_to_quat(e));
        }
    } else if let Some(s) = v.as_str() {
        if let Ok(parsed) = parse_py_or_json(s) {
            if !parsed.is_string() {
                return as_unity_quat(&parsed);
            }
        }
    }
    None
}

pub fn slerp_quat(qa: [f32; 4], mut qb: [f32; 4], t: f32) -> [f32; 4] {
    let mut dot: f32 = (0..4).map(|i| qa[i] * qb[i]).sum();
    if dot < 0.0 {
        qb = [-qb[0], -qb[1], -qb[2], -qb[3]];
        dot = -dot;
    }
    if dot > 0.9995 {
        let l: [f32; 4] = std::array::from_fn(|i| qa[i] + (qb[i] - qa[i]) * t);
        let n = l.iter().map(|v| v * v).sum::<f32>().sqrt().max(1e-9);
        l.map(|v| v / n)
    } else {
        let th = dot.clamp(-1.0, 1.0).acos();
        let sin_th = th.sin().max(1e-6);
        let (sa, sb) = (((1.0 - t) * th).sin() / sin_th, (t * th).sin() / sin_th);
        std::array::from_fn(|i| qa[i] * sa + qb[i] * sb)
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeFkBone {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub no: Option<i32>,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub bone: String,
    pub rot: [f32; 3],
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeIkTarget {
    pub no: i32,
    pub effector: i32,
    pub part: i32,
    pub pos: [f32; 3],
    pub use_rot: bool,
    pub rot: [f32; 3],
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeCameraSlot {
    pub slot: usize,
    pub pos: [f32; 3],
    pub distance: [f32; 3],
    pub rotate: [f32; 3],
    pub fov: f32,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub who_say: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub what_say: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub add_param: String,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeObjectStatus {
    pub alias: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub key: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub visible: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pos: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rot: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub scale: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub anime: Option<[i32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub anime_speed: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub anime_time: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub kinematic: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub fk_active: Option<Vec<bool>>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub fk: Vec<VngeFkBone>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ik_active: Option<Vec<bool>>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub ik: Vec<VngeIkTarget>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyebrow_ptn: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyes_ptn: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyes_open: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mouth_ptn: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mouth_open: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hand_left: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hand_right: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyes_look: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub neck_look: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub outfit: Option<i32>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub clothes_state: Vec<u8>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub show_accessory: Vec<u8>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tears_lv: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub nip_stand: Option<f32>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub fluids: Vec<u8>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub light_intensity: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub light_enable: Option<bool>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeSceneState {
    pub id: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub who_say: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub what_say: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub add_param: String,
    pub actors: Vec<VngeObjectStatus>,
    pub props: Vec<VngeObjectStatus>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeSssData {
    pub version: String,
    pub cameras: Vec<VngeCameraSlot>,
    pub states: Vec<VngeSceneState>,
}

pub fn parse_fk_dict(
    v: &Value,
    is_prop: bool,
    prop_bones: Option<&[String]>,
) -> Vec<VngeFkBone> {
    let mut out = Vec::new();
    match v {
        Value::Object(map) => {
            for (k, val) in map {
                let Some(rot) = as_unity_euler(val) else { continue };
                if !is_prop {
                    if let Ok(no) = k.trim().parse::<i32>() {
                        out.push(VngeFkBone { no: Some(no), bone: String::new(), rot });
                    } else {
                        out.push(VngeFkBone { no: None, bone: k.trim().to_string(), rot });
                    }
                } else {
                    out.push(VngeFkBone { no: None, bone: k.trim().to_string(), rot });
                }
            }
        }
        Value::Array(arr) => {
            if is_prop {
                for (idx, val) in arr.iter().enumerate() {
                    let Some(rot) = as_unity_euler(val) else { continue };
                    let bone = prop_bones
                        .and_then(|b| b.get(idx))
                        .cloned()
                        .unwrap_or_default();
                    if !bone.is_empty() {
                        out.push(VngeFkBone { no: None, bone, rot });
                    }
                }
            } else {
                // fk_set2 / fk_set_quat in some versions can be [[active_tuple, dict], ...]
                for item in arr {
                    if let Some(pair) = item.as_array() {
                        if pair.len() == 2 {
                            out.extend(parse_fk_dict(&pair[1], is_prop, prop_bones));
                        }
                    }
                }
            }
        }
        _ => {}
    }
    out
}

pub fn parse_ik_entry(key: &str, val: &Value) -> Option<VngeIkTarget> {
    let (no, effector, part, use_rot) = ik_target_mapping(key)?;
    let arr = val.as_array()?;
    if arr.is_empty() {
        return None;
    }
    // Format A: ([px, py, pz],) or ([px, py, pz], [qx, qy, qz, qw] / [rx, ry, rz])
    if arr[0].is_array() || arr[0].is_object() {
        let pos = as_vec3(&arr[0])?;
        let rot = if arr.len() >= 2 {
            as_unity_euler(&arr[1]).unwrap_or([0.0; 3])
        } else {
            [0.0; 3]
        };
        return Some(VngeIkTarget { no, effector, part, pos, use_rot, rot });
    }
    // Format B: [px, py, pz] or [px, py, pz, rx, ry, rz]
    let pos = as_vec3(val)?;
    let rot = if arr.len() >= 6 {
        [
            as_f32(&arr[3]).unwrap_or(0.0),
            as_f32(&arr[4]).unwrap_or(0.0),
            as_f32(&arr[5]).unwrap_or(0.0),
        ]
    } else {
        [0.0; 3]
    };
    Some(VngeIkTarget { no, effector, part, pos, use_rot, rot })
}

pub fn parse_ik_dict(v: &Value) -> Vec<VngeIkTarget> {
    let mut out = Vec::new();
    match v {
        Value::Object(map) => {
            for (k, val) in map {
                if let Some(t) = parse_ik_entry(k, val) {
                    out.push(t);
                }
            }
        }
        Value::Array(arr) => {
            // [active_5_tuple, ik_dict]
            if arr.len() == 2 && arr[1].is_object() {
                return parse_ik_dict(&arr[1]);
            }
        }
        _ => {}
    }
    out
}

pub fn parse_object_status(
    alias: &str,
    map: &serde_json::Map<String, Value>,
    is_prop: bool,
    reg: &VngeRegistry,
) -> VngeObjectStatus {
    let key = if is_prop {
        reg.resolve_prop(alias)
    } else {
        reg.resolve_actor(alias)
    };
    let prop_bones = key.and_then(|k| reg.prop_fk_bones.get(&k).map(Vec::as_slice));
    let mut st = VngeObjectStatus {
        alias: alias.to_string(),
        key,
        ..Default::default()
    };
    if let Some(v) = map.get("visible").and_then(as_bool) {
        st.visible = Some(v);
    }
    if let Some(p) = map.get("move_to").and_then(as_vec3) {
        st.pos = Some(p);
    }
    if let Some(r) = map.get("rotate_to").and_then(as_unity_euler) {
        st.rot = Some(r);
    }
    if let Some(s) = map.get("scale_to").and_then(as_vec3) {
        st.scale = Some(s);
    }
    if let Some(anim) = map.get("anim").and_then(Value::as_array) {
        if anim.len() == 3 {
            if let (Some(g), Some(c), Some(n)) = (as_i32(&anim[0]), as_i32(&anim[1]), as_i32(&anim[2])) {
                st.anime = Some([g, c, n]);
            }
        } else if anim.len() >= 4 {
            if let (Some(g), Some(c), Some(n)) = (as_i32(&anim[1]), as_i32(&anim[2]), as_i32(&anim[3])) {
                st.anime = Some([g, c, n]);
            }
        }
    }
    if let Some(spd) = map.get("anim_spd").and_then(as_f32) {
        st.anime_speed = Some(spd);
    }
    if let Some(nt) = map.get("anim_ntime").and_then(as_f32) {
        st.anime_time = Some(nt);
    }
    if let Some(k) = map.get("kinematic").and_then(as_i32) {
        st.kinematic = Some(k);
    }
    if let Some(fka) = map.get("fk_active").and_then(Value::as_array) {
        st.fk_active = Some(fka.iter().map(|x| as_bool(x).unwrap_or(false)).collect());
    }
    for k in ["fk_set2", "fk_set_quat", "fk_set"] {
        if let Some(fk_val) = map.get(k) {
            let parsed = parse_fk_dict(fk_val, is_prop, prop_bones);
            if !parsed.is_empty() {
                st.fk = parsed;
                break;
            }
        }
    }
    if let Some(ika) = map.get("ik_active").and_then(Value::as_array) {
        st.ik_active = Some(ika.iter().map(|x| as_bool(x).unwrap_or(false)).collect());
    }
    for k in ["ik_set2", "ik_set_quat", "ik_set"] {
        if let Some(ik_val) = map.get(k) {
            let parsed = parse_ik_dict(ik_val);
            if !parsed.is_empty() {
                st.ik = parsed;
                break;
            }
        }
    }
    if let Some(v) = map.get("eyebrow").and_then(as_i32) {
        st.eyebrow_ptn = Some(v);
    }
    if let Some(v) = map.get("eyes").and_then(as_i32) {
        st.eyes_ptn = Some(v);
    }
    if let Some(v) = map.get("eyes_open").and_then(as_f32) {
        st.eyes_open = Some(v);
    }
    if let Some(v) = map.get("mouth").and_then(as_i32) {
        st.mouth_ptn = Some(v);
    }
    if let Some(v) = map.get("mouth_open").and_then(as_f32) {
        st.mouth_open = Some(v);
    }
    if let Some(hands) = map.get("hands").and_then(Value::as_array) {
        if let Some(l) = hands.first().and_then(as_i32) {
            st.hand_left = Some(l);
        }
        if let Some(r) = hands.get(1).and_then(as_i32) {
            st.hand_right = Some(r);
        }
    }
    if let Some(v) = map.get("look_at").and_then(as_i32) {
        st.eyes_look = Some(v);
    }
    if let Some(v) = map.get("face_to").and_then(as_i32) {
        st.neck_look = Some(v);
    }
    if let Some(v) = map.get("cloth_type").and_then(as_i32) {
        st.outfit = Some(v);
    }
    if let Some(arr) = map.get("cloth_all").and_then(Value::as_array) {
        st.clothes_state = arr.iter().map(|x| as_i32(x).unwrap_or(0).clamp(0, 3) as u8).collect();
    }
    if let Some(arr) = map.get("acc_all").and_then(Value::as_array) {
        st.show_accessory = arr.iter().map(|x| u8::from(as_bool(x).unwrap_or(true))).collect();
    }
    if let Some(v) = map.get("tear").and_then(as_i32) {
        st.tears_lv = Some(v);
    }
    if let Some(v) = map.get("nip_stand").and_then(as_f32) {
        st.nip_stand = Some(v);
    }
    if let Some(arr) = map.get("juice").and_then(Value::as_array) {
        st.fluids = arr.iter().map(|x| as_i32(x).unwrap_or(0).clamp(0, 2) as u8).collect();
    }
    if let Some(v) = map.get("intensity").or_else(|| map.get("light_intensity")).and_then(as_f32) {
        st.light_intensity = Some(v);
    }
    if let Some(v) = map.get("enable").or_else(|| map.get("light_enable")).and_then(as_bool) {
        st.light_enable = Some(v);
    }
    st
}

fn find_folder_by_prefix<'a>(objs: &'a [Object], prefix: &str) -> Option<&'a Object> {
    for o in objs {
        match o {
            Object::Folder { name, children, .. } => {
                if name.trim().starts_with(prefix) {
                    return Some(o);
                }
                if let Some(found) = find_folder_by_prefix(children, prefix) {
                    return Some(found);
                }
            }
            Object::Item { children, .. } | Object::Route { children, .. } => {
                if let Some(found) = find_folder_by_prefix(children, prefix) {
                    return Some(found);
                }
            }
            Object::Character { children, .. } => {
                for (_, c) in children {
                    if let Some(found) = find_folder_by_prefix(c, prefix) {
                        return Some(found);
                    }
                }
            }
            _ => {}
        }
    }
    None
}

/// Parses VNGE SceneSaveState (`vnge_sss` in `KKEx`, or legacy `-scenesavestate:` folder).
pub fn parse_sss(scene: &Scene, blocks: Option<&rmpv::Value>, reg: &VngeRegistry) -> Option<VngeSssData> {
    let (version, raw) = if let Some((ver, main, _)) = blocks.and_then(|b| data_png_load(b, "vnge_sss")) {
        (ver, main)
    } else if let Some(Object::Folder { children, .. }) = find_folder_by_prefix(&scene.objects, "-scenesavestate:") {
        let first_name = children.iter().find_map(|c| match c {
            Object::Folder { name, .. } => Some(name.clone()),
            _ => None,
        })?;
        ("1.0-folder".into(), first_name)
    } else {
        return None;
    };

    let parsed = parse_py_or_json(&raw).ok()?;
    let obj = parsed.as_object()?;
    let mut sss = VngeSssData {
        version,
        cameras: Vec::new(),
        states: Vec::new(),
    };

    if let Some(cams) = obj.get("cams").and_then(Value::as_array) {
        for (slot, c) in cams.iter().enumerate() {
            let Some(arr) = c.as_array() else { continue };
            if arr.len() < 4 {
                continue;
            }
            let (Some(pos), Some(distance), Some(rotate), Some(fov)) = (
                as_vec3(&arr[0]),
                as_vec3(&arr[1]),
                as_unity_euler(&arr[2]),
                as_f32(&arr[3]),
            ) else {
                continue;
            };
            let (mut who_say, mut what_say, mut add_param) = (String::new(), String::new(), String::new());
            if let Some( dlg) = arr.get(4).and_then(Value::as_array) {
                who_say = dlg.first().and_then(Value::as_str).unwrap_or("").to_string();
                what_say = dlg.get(1).and_then(Value::as_str).unwrap_or("").to_string();
                add_param = dlg.get(2).and_then(Value::as_str).unwrap_or("").to_string();
            }
            sss.cameras.push(VngeCameraSlot {
                slot,
                pos,
                distance,
                rotate,
                fov,
                who_say,
                what_say,
                add_param,
            });
        }
    }

    let mut state_keys: Vec<&String> = obj.keys().filter(|k| k.as_str() != "cams").collect();
    state_keys.sort_by(|a, b| {
        match (a.parse::<i64>(), b.parse::<i64>()) {
            (Ok(ia), Ok(ib)) => ia.cmp(&ib),
            _ => a.cmp(b),
        }
    });

    for k in state_keys {
        let Some(arr) = obj.get(k).and_then(Value::as_array) else { continue };
        if arr.len() < 2 {
            continue;
        }
        let mut state = VngeSceneState {
            id: k.clone(),
            who_say: arr.get(2).and_then(Value::as_str).unwrap_or("").to_string(),
            what_say: arr.get(3).and_then(Value::as_str).unwrap_or("").to_string(),
            add_param: arr.get(4).and_then(Value::as_str).unwrap_or("").to_string(),
            actors: Vec::new(),
            props: Vec::new(),
        };
        if let Some(actors_map) = arr[0].as_object() {
            for (alias, st_val) in actors_map {
                if let Some(st_obj) = st_val.as_object() {
                    state.actors.push(parse_object_status(alias, st_obj, false, reg));
                }
            }
        }
        if let Some(props_map) = arr[1].as_object() {
            for (alias, st_val) in props_map {
                if let Some(st_obj) = st_val.as_object() {
                    state.props.push(parse_object_status(alias, st_obj, true, reg));
                }
            }
        }
        sss.states.push(state);
    }

    if sss.cameras.is_empty() && sss.states.is_empty() {
        None
    } else {
        Some(sss)
    }
}

/// Evaluates VNGE KFA v1 (`vnge_kfa`) and v2 (`vnge_kfa2`) keyframe clips into timeline tracks.
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeKey {
    pub time: f32,
    pub ease: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub visible: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pos: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rot: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub scale: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cam_pos: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cam_dst: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cam_rot: Option<[f32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub value: Option<f32>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub fk: Vec<VngeFkBone>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub ik: Vec<VngeIkTarget>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyebrow_ptn: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyes_ptn: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mouth_ptn: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eyes_open: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mouth_open: Option<f32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hand_left: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hand_right: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub anime: Option<[i32; 3]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub anime_speed: Option<f32>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeTrack {
    pub target_id: String,
    pub target_type: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub key: Option<i32>,
    pub keys: Vec<VngeKey>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeClip {
    pub name: String,
    pub version: i32,
    pub frame_length: f32,
    pub frame_rate: f32,
    pub duration: f32,
    pub loop_mode: i32,
    pub speed: f32,
    pub autorun: bool,
    pub priority: i32,
    pub tracks: Vec<VngeTrack>,
}

#[derive(Clone, Debug)]
struct RawKf {
    frame: f32,
    value: Value,
    curve: Option<[f32; 4]>,
}

fn parse_curve(v: Option<&Value>) -> Option<[f32; 4]> {
    let arr = v?.as_array()?;
    if arr.len() >= 4 {
        Some([
            as_f32(&arr[0])?,
            as_f32(&arr[1])?,
            as_f32(&arr[2])?,
            as_f32(&arr[3])?,
        ])
    } else {
        None
    }
}

fn eval_kf_f32(kfs: &[RawKf], frame: f32, interp: &str) -> Option<f32> {
    if kfs.is_empty() {
        return None;
    }
    if frame <= kfs[0].frame {
        return as_f32(&kfs[0].value);
    }
    let last = kfs.last().unwrap();
    if frame >= last.frame {
        return as_f32(&last.value);
    }
    for w in kfs.windows(2) {
        if frame >= w[0].frame && frame <= w[1].frame {
            let a = as_f32(&w[0].value)?;
            let b = as_f32(&w[1].value)?;
            if interp == "D" || interp == "E" || (w[1].frame - w[0].frame).abs() < 1e-5 {
                return Some(a);
            }
            let p = ((frame - w[0].frame) / (w[1].frame - w[0].frame)).clamp(0.0, 1.0);
            let t = if let Some(c) = w[0].curve {
                cubic_bezier_interpolate(c[0], c[1], c[2], c[3], p)
            } else {
                p
            };
            return Some(a + (b - a) * t);
        }
    }
    as_f32(&last.value)
}

fn eval_kf_vec3(kfs: &[RawKf], frame: f32, interp: &str) -> Option<[f32; 3]> {
    if kfs.is_empty() {
        return None;
    }
    if frame <= kfs[0].frame {
        return as_vec3(&kfs[0].value);
    }
    let last = kfs.last().unwrap();
    if frame >= last.frame {
        return as_vec3(&last.value);
    }
    for w in kfs.windows(2) {
        if frame >= w[0].frame && frame <= w[1].frame {
            let a = as_vec3(&w[0].value)?;
            let b = as_vec3(&w[1].value)?;
            if interp == "D" || interp == "E" || (w[1].frame - w[0].frame).abs() < 1e-5 {
                return Some(a);
            }
            let p = ((frame - w[0].frame) / (w[1].frame - w[0].frame)).clamp(0.0, 1.0);
            let t = if let Some(c) = w[0].curve {
                cubic_bezier_interpolate(c[0], c[1], c[2], c[3], p)
            } else {
                p
            };
            return Some([
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t,
            ]);
        }
    }
    as_vec3(&last.value)
}

fn eval_kf_euler(kfs: &[RawKf], frame: f32, interp: &str) -> Option<[f32; 3]> {
    if kfs.is_empty() {
        return None;
    }
    if frame <= kfs[0].frame {
        return as_unity_euler(&kfs[0].value);
    }
    let last = kfs.last().unwrap();
    if frame >= last.frame {
        return as_unity_euler(&last.value);
    }
    for w in kfs.windows(2) {
        if frame >= w[0].frame && frame <= w[1].frame {
            if interp == "D" || interp == "E" || (w[1].frame - w[0].frame).abs() < 1e-5 {
                return as_unity_euler(&w[0].value);
            }
            let p = ((frame - w[0].frame) / (w[1].frame - w[0].frame)).clamp(0.0, 1.0);
            let t = if let Some(c) = w[0].curve {
                cubic_bezier_interpolate(c[0], c[1], c[2], c[3], p)
            } else {
                p
            };
            // If stored as Quaternion (len 4), slerp quaternion; if stored as Euler (len 3), slerp quaternion as well
            let qa = as_unity_quat(&w[0].value)?;
            let qb = as_unity_quat(&w[1].value)?;
            return Some(crate::pose::quat_to_unity_euler(slerp_quat(qa, qb, t)));
        }
    }
    as_unity_euler(&last.value)
}

fn eval_kf_step<'a>(kfs: &'a [RawKf], frame: f32) -> Option<&'a Value> {
    if kfs.is_empty() {
        return None;
    }
    let mut cur = &kfs[0].value;
    for kf in kfs {
        if kf.frame <= frame + 1e-4 {
            cur = &kf.value;
        } else {
            break;
        }
    }
    Some(cur)
}

fn parse_kf_list(data: &Value) -> Vec<RawKf> {
    let mut out = Vec::new();
    let Some(arr) = data.as_array() else { return out };
    for item in arr {
        let Some(tuple) = item.as_array() else { continue };
        if tuple.len() < 2 {
            continue;
        }
        let Some(frame) = as_f32(&tuple[0]) else { continue };
        let value = tuple[1].clone();
        let curve = parse_curve(tuple.get(2));
        out.push(RawKf { frame, value, curve });
    }
    out.sort_by(|a, b| a.frame.partial_cmp(&b.frame).unwrap_or(std::cmp::Ordering::Equal));
    out
}

fn parse_clip_v2(setting: &Value, reg: &VngeRegistry) -> Option<VngeClip> {
    let obj = setting.as_object()?;
    let name = obj.get("name").and_then(Value::as_str).unwrap_or("clip").to_string();
    let frame_length = obj.get("frameLength").and_then(as_f32).unwrap_or(300.0).max(1.0);
    let frame_rate = obj.get("frameRate").and_then(as_f32).unwrap_or(30.0).max(1.0);
    let speed = obj.get("speed").and_then(as_f32).unwrap_or(1.0).max(0.01);
    let loop_mode = obj.get("loop").and_then(as_i32).unwrap_or(0);
    let autorun = obj.get("autorun").and_then(as_bool).unwrap_or(false);
    let priority = obj.get("priority").and_then(as_i32).unwrap_or(100);
    let duration = frame_length / (frame_rate * speed);

    let mut clip = VngeClip {
        name,
        version: 2,
        frame_length,
        frame_rate,
        duration,
        loop_mode,
        speed,
        autorun,
        priority,
        tracks: Vec::new(),
    };

    let Some(kf_map) = obj.get("keyframes").and_then(Value::as_object) else {
        return Some(clip);
    };

    for (tid, ctrlset_val) in kf_map {
        let Some(ctrlset) = ctrlset_val.as_object() else { continue };
        if ctrlset.get("enable").and_then(as_bool) == Some(false) {
            continue;
        }
        let Some(controllers) = ctrlset.get("controllers").and_then(Value::as_object) else {
            continue;
        };
        let (target_type, resolved_key) = if tid == "cam" {
            ("cam".to_string(), None)
        } else if tid == "sys" {
            ("sys".to_string(), None)
        } else if let Some(k) = reg.resolve_actor(tid) {
            ("actor".to_string(), Some(k))
        } else if let Some(k) = reg.resolve_prop(tid) {
            ("prop".to_string(), Some(k))
        } else {
            ("unknown".to_string(), None)
        };

        let mut single_ctrls: BTreeMap<String, (String, Vec<RawKf>)> = BTreeMap::new();
        let mut multi_ctrls: BTreeMap<String, (String, BTreeMap<String, Vec<RawKf>>)> = BTreeMap::new();
        let mut frame_set: BTreeSet<i32> = BTreeSet::new();
        let mut has_custom_bezier = false;

        for (cname, cval) in controllers {
            let Some(cobj) = cval.as_object() else { continue };
            let interp = cobj
                .get("interpolateType")
                .and_then(Value::as_str)
                .unwrap_or("A")
                .to_string();
            let single = cobj.get("SingleSet").and_then(as_bool).unwrap_or(true);
            let Some(kf_data) = cobj.get("keyframeData").and_then(Value::as_array) else {
                continue;
            };
            if single {
                if let Some(first) = kf_data.first() {
                    let list = parse_kf_list(first);
                    for k in &list {
                        frame_set.insert(k.frame.round() as i32);
                        if k.curve.is_some() {
                            has_custom_bezier = true;
                        }
                    }
                    if !list.is_empty() {
                        single_ctrls.insert(cname.clone(), (interp, list));
                    }
                }
            } else {
                let Some(keys) = cobj.get("keyframeSetKeys").and_then(Value::as_array) else {
                    continue;
                };
                let mut sub_map = BTreeMap::new();
                for (sub_k, sub_d) in keys.iter().zip(kf_data.iter()) {
                    let sk = val_to_key(sub_k);
                    let list = parse_kf_list(sub_d);
                    for k in &list {
                        frame_set.insert(k.frame.round() as i32);
                        if k.curve.is_some() {
                            has_custom_bezier = true;
                        }
                    }
                    if !list.is_empty() {
                        sub_map.insert(sk, list);
                    }
                }
                if !sub_map.is_empty() {
                    multi_ctrls.insert(cname.clone(), (interp, sub_map));
                }
            }
        }

        if frame_set.is_empty() {
            continue;
        }

        // If any controller uses custom Bezier curves and spans multiple frames, add intermediate sample frames
        // so custom ease curves play accurately in linear/cubic timelines.
        if has_custom_bezier && frame_set.len() >= 2 {
            let keyframes_sorted: Vec<i32> = frame_set.iter().copied().collect();
            for w in keyframes_sorted.windows(2) {
                let span = w[1] - w[0];
                if span > 4 {
                    let step = (span / 4).max(2);
                    let mut f = w[0] + step;
                    while f < w[1] {
                        frame_set.insert(f);
                        f += step;
                    }
                }
            }
        }

        let mut keys = Vec::with_capacity(frame_set.len());
        for f_int in frame_set {
            let f = f_int as f32;
            let time = f / (frame_rate * speed);
            let mut key = VngeKey {
                time,
                ease: "Linear".into(),
                ..Default::default()
            };

            if target_type == "cam" {
                if let Some((interp, list)) = single_ctrls.get("cam_pos") {
                    key.cam_pos = eval_kf_vec3(list, f, interp);
                }
                if let Some((interp, list)) = single_ctrls.get("cam_dst") {
                    key.cam_dst = eval_kf_vec3(list, f, interp);
                }
                if let Some((interp, list)) = single_ctrls.get("cam_rot") {
                    key.cam_rot = eval_kf_euler(list, f, interp);
                }
                if let Some((interp, list)) = single_ctrls.get("cam_fov") {
                    key.value = eval_kf_f32(list, f, interp);
                }
            } else {
                if let Some((_, list)) = single_ctrls.get("visible") {
                    key.visible = eval_kf_step(list, f).and_then(as_bool);
                }
                if let Some((interp, list)) = single_ctrls.get("move_to") {
                    key.pos = eval_kf_vec3(list, f, interp);
                }
                if let Some((interp, list)) = single_ctrls.get("rotate_to") {
                    key.rot = eval_kf_euler(list, f, interp);
                }
                if let Some((interp, list)) = single_ctrls.get("scale_to") {
                    key.scale = eval_kf_vec3(list, f, interp);
                }
                if let Some((interp, list)) = single_ctrls.get("intensity").or_else(|| single_ctrls.get("light_intensity")) {
                    key.value = eval_kf_f32(list, f, interp);
                }
                if let Some((_, list)) = single_ctrls.get("eyebrow") {
                    key.eyebrow_ptn = eval_kf_step(list, f).and_then(as_i32);
                }
                if let Some((_, list)) = single_ctrls.get("eyes") {
                    key.eyes_ptn = eval_kf_step(list, f).and_then(as_i32);
                }
                if let Some((interp, list)) = single_ctrls.get("eyes_open") {
                    key.eyes_open = eval_kf_f32(list, f, interp);
                }
                if let Some((_, list)) = single_ctrls.get("mouth") {
                    key.mouth_ptn = eval_kf_step(list, f).and_then(as_i32);
                }
                if let Some((interp, list)) = single_ctrls.get("mouth_open") {
                    key.mouth_open = eval_kf_f32(list, f, interp);
                }
                if let Some((_, list)) = single_ctrls.get("hands") {
                    if let Some(arr) = eval_kf_step(list, f).and_then(Value::as_array) {
                        key.hand_left = arr.first().and_then(as_i32);
                        key.hand_right = arr.get(1).and_then(as_i32);
                    }
                }
                if let Some((_, list)) = single_ctrls.get("anim") {
                    if let Some(arr) = eval_kf_step(list, f).and_then(Value::as_array) {
                        if arr.len() == 3 {
                            if let (Some(g), Some(c), Some(n)) = (as_i32(&arr[0]), as_i32(&arr[1]), as_i32(&arr[2])) {
                                key.anime = Some([g, c, n]);
                            }
                        } else if arr.len() >= 4 {
                            if let (Some(g), Some(c), Some(n)) = (as_i32(&arr[1]), as_i32(&arr[2]), as_i32(&arr[3])) {
                                key.anime = Some([g, c, n]);
                            }
                        }
                    }
                }
                if let Some((interp, list)) = single_ctrls.get("anim_spd") {
                    key.anime_speed = eval_kf_f32(list, f, interp);
                }
                if let Some((interp, sub_map)) = multi_ctrls.get("fk_set2") {
                    for (bone_key, list) in sub_map {
                        if let Some(rot) = eval_kf_euler(list, f, interp) {
                            if let Ok(no) = bone_key.parse::<i32>() {
                                key.fk.push(VngeFkBone { no: Some(no), bone: String::new(), rot });
                            } else {
                                key.fk.push(VngeFkBone { no: None, bone: bone_key.clone(), rot });
                            }
                        }
                    }
                }
                if let Some((interp, sub_map)) = multi_ctrls.get("ik_set2") {
                    for (ik_key, list) in sub_map {
                        let Some((no, effector, part, use_rot)) = ik_target_mapping(ik_key) else {
                            continue;
                        };
                        if let Some((pos, rot)) = eval_kf_ik(list, f, interp) {
                            key.ik.push(VngeIkTarget {
                                no,
                                effector,
                                part,
                                pos,
                                use_rot,
                                rot,
                            });
                        }
                    }
                }
            }
            keys.push(key);
        }

        clip.tracks.push(VngeTrack {
            target_id: tid.clone(),
            target_type,
            key: resolved_key,
            keys,
        });
    }

    Some(clip)
}

fn eval_kf_ik(kfs: &[RawKf], frame: f32, interp: &str) -> Option<([f32; 3], [f32; 3])> {
    if kfs.is_empty() {
        return None;
    }
    let extract = |v: &Value| -> Option<([f32; 3], Option<[f32; 4]>)> {
        let arr = v.as_array()?;
        if arr.is_empty() {
            return None;
        }
        if arr[0].is_array() || arr[0].is_object() {
            let pos = as_vec3(&arr[0])?;
            let q = arr.get(1).and_then(as_unity_quat);
            Some((pos, q))
        } else {
            let pos = as_vec3(v)?;
            Some((pos, None))
        }
    };
    if frame <= kfs[0].frame {
        let (p, q) = extract(&kfs[0].value)?;
        return Some((p, q.map(crate::pose::quat_to_unity_euler).unwrap_or([0.0; 3])));
    }
    let last = kfs.last().unwrap();
    if frame >= last.frame {
        let (p, q) = extract(&last.value)?;
        return Some((p, q.map(crate::pose::quat_to_unity_euler).unwrap_or([0.0; 3])));
    }
    for w in kfs.windows(2) {
        if frame >= w[0].frame && frame <= w[1].frame {
            let (pa, qa) = extract(&w[0].value)?;
            let (pb, qb) = extract(&w[1].value)?;
            if interp == "D" || interp == "E" || (w[1].frame - w[0].frame).abs() < 1e-5 {
                return Some((pa, qa.map(crate::pose::quat_to_unity_euler).unwrap_or([0.0; 3])));
            }
            let p = ((frame - w[0].frame) / (w[1].frame - w[0].frame)).clamp(0.0, 1.0);
            let t = if let Some(c) = w[0].curve {
                cubic_bezier_interpolate(c[0], c[1], c[2], c[3], p)
            } else {
                p
            };
            let pos = [
                pa[0] + (pb[0] - pa[0]) * t,
                pa[1] + (pb[1] - pa[1]) * t,
                pa[2] + (pb[2] - pa[2]) * t,
            ];
            let rot = match (qa, qb) {
                (Some(a), Some(b)) => crate::pose::quat_to_unity_euler(slerp_quat(a, b, t)),
                (Some(a), None) => crate::pose::quat_to_unity_euler(a),
                _ => [0.0; 3],
            };
            return Some((pos, rot));
        }
    }
    let (p, q) = extract(&last.value)?;
    Some((p, q.map(crate::pose::quat_to_unity_euler).unwrap_or([0.0; 3])))
}

/// Parses a v1 `KeyFrameClip` (`vnanime.py` `exportClip`).
fn parse_clip_v1(setting: &Value, reg: &VngeRegistry) -> Option<VngeClip> {
    let obj = setting.as_object()?;
    let name = obj.get("name").and_then(Value::as_str).unwrap_or("clip").to_string();
    let frame_length = obj.get("frameLength").and_then(as_f32).unwrap_or(300.0).max(1.0);
    let frame_rate = obj.get("frameRate").and_then(as_f32).unwrap_or(30.0).max(1.0);
    let speed = obj.get("speed").and_then(as_f32).unwrap_or(1.0).max(0.01);
    let loop_mode = obj.get("loop").and_then(as_i32).unwrap_or(0);
    let autorun = obj.get("autorun").and_then(as_bool).unwrap_or(false);
    let priority = obj.get("priority").and_then(as_i32).unwrap_or(100);
    let duration = frame_length / (frame_rate * speed);

    let mut clip = VngeClip {
        name,
        version: 1,
        frame_length,
        frame_rate,
        duration,
        loop_mode,
        speed,
        autorun,
        priority,
        tracks: Vec::new(),
    };

    let Some(kf_map) = obj.get("keyframes").and_then(Value::as_object) else {
        return Some(clip);
    };

    for (tid, kf_list_val) in kf_map {
        let Some(kf_list) = kf_list_val.as_array() else { continue };
        let (target_type, resolved_key) = if tid == "cam" {
            ("cam".to_string(), None)
        } else if tid == "sys" {
            ("sys".to_string(), None)
        } else if let Some(k) = reg.resolve_actor(tid) {
            ("actor".to_string(), Some(k))
        } else if let Some(k) = reg.resolve_prop(tid) {
            ("prop".to_string(), Some(k))
        } else {
            ("unknown".to_string(), None)
        };
        let is_prop = target_type == "prop";
        let mut keys = Vec::new();
        for kf_entry in kf_list {
            // In v1 exportClip, each entry is either [frameNo, statusDict, curve] or {"frameNo": ..., "status": ..., "curve": ...}
            let (frame_opt, status_opt) = if let Some(arr) = kf_entry.as_array() {
                (arr.first().and_then(as_f32), arr.get(1).and_then(Value::as_object))
            } else if let Some(m) = kf_entry.as_object() {
                (
                    m.get("frameNo").or_else(|| m.get("frame")).and_then(as_f32),
                    m.get("status").and_then(Value::as_object),
                )
            } else {
                (None, None)
            };
            let (Some(f), Some(st_map)) = (frame_opt, status_opt) else { continue };
            let time = f / (frame_rate * speed);
            let mut key = VngeKey {
                time,
                ease: "Cubic".into(),
                ..Default::default()
            };
            if target_type == "cam" {
                if let Some(cam_arr) = st_map.get("camera").and_then(Value::as_array) {
                    if cam_arr.len() >= 4 {
                        key.cam_pos = as_vec3(&cam_arr[0]);
                        key.cam_dst = as_vec3(&cam_arr[1]);
                        key.cam_rot = as_unity_euler(&cam_arr[2]);
                        key.value = as_f32(&cam_arr[3]);
                    }
                }
            } else {
                let st = parse_object_status(tid, st_map, is_prop, reg);
                key.visible = st.visible;
                key.pos = st.pos;
                key.rot = st.rot;
                key.scale = st.scale;
                key.value = st.light_intensity;
                key.fk = st.fk;
                key.ik = st.ik;
                key.eyebrow_ptn = st.eyebrow_ptn;
                key.eyes_ptn = st.eyes_ptn;
                key.eyes_open = st.eyes_open;
                key.mouth_ptn = st.mouth_ptn;
                key.mouth_open = st.mouth_open;
                key.hand_left = st.hand_left;
                key.hand_right = st.hand_right;
                key.anime = st.anime;
                key.anime_speed = st.anime_speed;
            }
            keys.push(key);
        }
        keys.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap_or(std::cmp::Ordering::Equal));
        if !keys.is_empty() {
            clip.tracks.push(VngeTrack {
                target_id: tid.clone(),
                target_type,
                key: resolved_key,
                keys,
            });
        }
    }

    Some(clip)
}

/// Parses all VNGE KFA v1 and v2 clips from `KKEx` and/or `-keyframeclips:` folders.
pub fn parse_kfa(scene: &Scene, blocks: Option<&rmpv::Value>, reg: &VngeRegistry) -> Vec<VngeClip> {
    let mut clips = Vec::new();

    // 1. vnge_kfa2 in KKEx
    if let Some((_, main, list)) = blocks.and_then(|b| data_png_load(b, "vnge_kfa2")) {
        if let Some(items) = list {
            for item_str in items {
                if let Ok(val) = parse_py_or_json(&item_str) {
                    if let Some(c) = parse_clip_v2(&val, reg) {
                        clips.push(c);
                    }
                }
            }
        } else if !main.is_empty() {
            if let Ok(val) = parse_py_or_json(&main) {
                if let Some(arr) = val.as_array() {
                    for item in arr {
                        if let Some(c) = parse_clip_v2(item, reg) {
                            clips.push(c);
                        }
                    }
                } else if let Some(c) = parse_clip_v2(&val, reg) {
                    clips.push(c);
                }
            }
        }
    }

    // 2. vnge_kfa (v1) in KKEx
    if let Some((_, main, _)) = blocks.and_then(|b| data_png_load(b, "vnge_kfa")) {
        if let Ok(val) = parse_py_or_json(&main) {
            if let Some(arr) = val.as_array() {
                for item in arr {
                    if let Some(c) = parse_clip_v1(item, reg) {
                        clips.push(c);
                    }
                }
            } else if let Some(c) = parse_clip_v1(&val, reg) {
                clips.push(c);
            }
        }
    }

    // 3. Legacy "-keyframeclips" folder in scene tree
    if let Some(Object::Folder { children, .. }) = find_folder_by_prefix(&scene.objects, "-keyframeclips") {
        for child in children {
            if let Object::Folder { children: inner, .. } = child {
                if let Some(Object::Folder { name: raw, .. }) = inner.first() {
                    if let Ok(val) = parse_py_or_json(raw) {
                        if val.get("version").and_then(as_i32) == Some(2) {
                            if let Some(c) = parse_clip_v2(&val, reg) {
                                clips.push(c);
                            }
                        } else if let Some(c) = parse_clip_v1(&val, reg) {
                            clips.push(c);
                        }
                    }
                }
            }
        }
    }

    clips.sort_by_key(|c| c.priority);
    clips
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeScript {
    pub version: String,
    pub raw: Value,
}

/// Parses VNGE VNSS (`vnge_vnss` in `KKEx`, or `:vnscenescript:` folder in scene tree).
pub fn parse_vnss(scene: &Scene, blocks: Option<&rmpv::Value>) -> Option<VngeScript> {
    if let Some((ver, main, _)) = blocks.and_then(|b| data_png_load(b, "vnge_vnss")) {
        if let Ok(raw) = parse_py_or_json(&main) {
            return Some(VngeScript { version: ver, raw });
        }
    }
    if let Some(Object::Folder { name, children, .. }) = find_folder_by_prefix(&scene.objects, ":vnscenescript:") {
        let mut states = serde_json::Map::new();
        for c in children {
            if let Object::Folder { name: cname, children: cchildren, .. } = c {
                let ct = cname.trim();
                if ct == ":acode" || ct == ":acodepng" || ct.starts_with(":s") || ct == ":init" {
                    let lines: Vec<Value> = cchildren
                        .iter()
                        .filter_map(|x| match x {
                            Object::Folder { name: line, .. } => Some(Value::String(line.clone())),
                            _ => None,
                        })
                        .collect();
                    states.insert(ct.to_string(), Value::Array(lines));
                }
            }
        }
        if !states.is_empty() {
            return Some(VngeScript {
                version: name.trim().to_string(),
                raw: Value::Object(states),
            });
        }
    }
    None
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VngeSceneData {
    pub registry: VngeRegistry,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sss: Option<VngeSssData>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub clips: Vec<VngeClip>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub script: Option<VngeScript>,
}

impl VngeSceneData {
    pub fn is_empty(&self) -> bool {
        self.registry.actors.is_empty()
            && self.registry.props.is_empty()
            && self.sss.is_none()
            && self.clips.is_empty()
            && self.script.is_none()
    }
}

pub fn extract_vnge(scene: &Scene, bytes: &[u8]) -> VngeSceneData {
    let blocks = crate::studio_scene::scene_plugins(bytes);
    let registry = register_actors_props(scene);
    let sss = parse_sss(scene, blocks.as_ref(), &registry);
    let clips = parse_kfa(scene, blocks.as_ref(), &registry);
    let script = parse_vnss(scene, blocks.as_ref());
    VngeSceneData {
        registry,
        sss,
        clips,
        script,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_python_and_scene_encoder_literals() {
        let py = "{'0': ({'f1': {'move_to': (1.0, 2.5, -3.0), 'visible': True, 'fk_set': {12: (10.0, 20.0, 30.0)}}}, {}, 'Alice', 'Hello', None)}";
        let v = parse_py_or_json(py).unwrap();
        assert_eq!(v["0"][2].as_str(), Some("Alice"));
        assert_eq!(as_vec3(&v["0"][0]["f1"]["move_to"]), Some([1.0, 2.5, -3.0]));
        assert_eq!(as_bool(&v["0"][0]["f1"]["visible"]), Some(true));

        let json_enc = r#"{"__tuple__": true, "items": [{"__Vector3__": true, "x": 1.0, "y": 2.0, "z": 3.0}, {"__Quaternion__": true, "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}]}"#;
        let v2 = parse_py_or_json(json_enc).unwrap();
        assert_eq!(as_vec3(&v2[0]), Some([1.0, 2.0, 3.0]));
        assert_eq!(as_unity_quat(&v2[1]), Some([0.0, 0.0, 0.0, 1.0]));
    }

    #[test]
    fn cubic_bezier_matches_endpoints_and_midpoint() {
        assert!((cubic_bezier_interpolate(0.2, 0.8, 0.2, 0.8, 0.0) - 0.0).abs() < 1e-6);
        assert!((cubic_bezier_interpolate(0.2, 0.8, 0.2, 0.8, 1.0) - 1.0).abs() < 1e-6);
        assert!((cubic_bezier_interpolate(0.2, 0.8, 0.2, 0.8, 0.5) - 0.5).abs() < 1e-5);
        let ease_in = cubic_bezier_interpolate(0.42, 1.0, 0.0, 1.0, 0.5);
        assert!(ease_in > 0.1 && ease_in < 0.5, "ease_in={ease_in}");
    }
}
