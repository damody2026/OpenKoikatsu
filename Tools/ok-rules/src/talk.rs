//! The original talk core (KKS dialogue rows): which voiced lines she may say now, the topic branches and their
//! outcomes, what a finished line writes into the relationship (favor under the stage cap, lewdness, what was talked
//! about, cooldowns, flags), the caps by stage (rules.json step-up), becoming friends, confessing, invitations, and the
//! collected topics (what an answer is worth, a dialogue dropping a topic, a daily pickup).
//!
//! The catalogues are game data the rules read themselves (`talk_load` names the files; reloaded with the tuning).
//! Unreal keeps its own copy of the lines for what it shows and plays (text, voices); every decision is here, and lines
//! are named by their id.
use crate::relation::Relation;
use crate::rng::RandomStream;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap};

// ------------------------------------------------------------------------------------------------ data

#[derive(Serialize, Deserialize, Debug, Clone, Default, PartialEq)]
#[serde(default)]
pub struct Effect {
    pub favor: f32,
    pub lewdness: f32,
    pub relation_event: String,
    pub reset_favor: bool,
    pub set_flags: Vec<String>,
    pub talk_cost: i32,
    pub memory: String,
    pub topic_id: i32,
    pub cooldown_days: i32,
}

impl Effect {
    fn new() -> Effect {
        Effect { topic_id: -1, ..Default::default() }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Choice {
    pub id: String,
    pub next: String,
    pub conditions: Vec<String>,
    pub effect: Effect,
}

#[derive(Debug, Clone, Default)]
pub struct Node {
    pub id: String,
    pub personality: i32,
    pub category: String,
    pub stage: i32,
    pub priority: f32,
    pub roles: Vec<String>,
    pub group: String,
    pub condition_id: i32,
    pub conditions: Vec<String>,
    pub voice_complete: bool,
    pub voice_reason: String,
    /// It has voice files to play (a complete, non-empty sequence).
    pub voiced: bool,
    pub effect: Effect,
    pub choices: Vec<Choice>,
    pub topic_id: i32,
    pub containers: Vec<i32>,
    pub command: i32,
    pub row: i32,
    pub part_index: i32,
    pub once: bool,
    /// A line of hers calls him by his name (a KKS [P...] token, filled with his name): not before she knows it.
    pub names_him: bool,
}

impl Node {
    fn single_role(&self, role: &str) -> bool {
        self.roles.len() == 1 && self.roles[0] == role
    }
    fn generic(&self) -> bool {
        self.single_role("generic")
    }
}

#[derive(Debug, Clone, Default)]
pub struct Member {
    pub row: i32,
    pub part_index: i32,
    pub node: Option<usize>,
    pub role: String,
}

#[derive(Debug, Clone, Default)]
pub struct Branch {
    pub id: String,
    pub category: String,
    pub group: String,
    pub kind: String,
    pub operator: String,
    pub personality: i32,
    pub stage: i32,
    pub command: i32,
    pub container: i32,
    pub condition_id: i32,
    pub controlling_row: i32,
    pub introduction: Option<usize>,
    pub matched_attribute_type: i32,
    pub axis: i32,
    pub threshold: i32,
    pub index_when_true: i32,
    pub index_when_false: i32,
    pub weights_matched: Vec<i32>,
    pub weights_unmatched: Vec<i32>,
    pub weights: Vec<i32>,
    pub indexer: Vec<Member>,
}

/// rules.json: favor caps by stage (the original step-up limits), her preference axes by personality, the confession
/// chance range.
#[derive(Debug, Clone)]
pub struct TalkRules {
    pub step_up: Option<[i32; 3]>,
    /// The fourth step-up limit (kept for the save's record).
    pub beyond: Option<i32>,
    pub preference: BTreeMap<i32, (i32, i32, i32)>,
    pub confession: (i32, i32),
}

impl Default for TalkRules {
    fn default() -> Self {
        TalkRules { step_up: None, beyond: None, preference: BTreeMap::new(), confession: (20, 70) }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Topic {
    pub no: i32,
    pub category: i32,
    pub rarity: i32,
    pub name: String,
}

/// The collected topics (topics.json): the topics, their dialogue drop chances and what answers are worth.
#[derive(Debug, Clone, Default)]
pub struct Topics {
    pub topics: Vec<Topic>,
    pub drops: BTreeMap<i32, i32>,
    pub talk_favor: Vec<i32>,
    pub listen_common: Vec<i32>,
    pub listen_rare: Vec<i32>,
    pub interest_favor: Vec<i32>,
    pub interest_lewd: Vec<i32>,
    pub talk_point: Vec<i32>,
    pub listen_point: Vec<i32>,
    pub interest_point: Vec<i32>,
}

impl Topics {
    fn find(&self, no: i32) -> Option<&Topic> {
        self.topics.iter().find(|t| t.no == no)
    }
}

#[derive(Debug, Clone, Default)]
pub struct TalkData {
    pub nodes: Vec<Node>,
    pub branches: Vec<Branch>,
    pub by_id: HashMap<String, usize>,
    pub by_personality: HashMap<i32, Vec<usize>>,
    pub rules: TalkRules,
    pub topics: Topics,
    pub warnings: Vec<String>,
    pub skipped: i32,
    pub paths: Value,
}

pub const MAX_CHOICES: usize = 8;
pub const FRIEND_FAVOR_CAP: i32 = 200;
pub const LEWDNESS_CAP: f32 = 100.0;
pub const CONFESS_ACCEPT_FAVOR: f32 = 50.0;
pub const CONFESS_REFUSE_FAVOR: f32 = -25.0;

fn approved_category(c: &str) -> bool {
    matches!(c, "invitation" | "daily" | "romance" | "intimacy" | "greeting" | "farewell")
}
pub fn talk_cost(category: &str) -> i32 {
    if matches!(category, "daily" | "romance" | "intimacy") {
        1
    } else {
        0
    }
}
fn approved_stage(s: &str) -> bool {
    matches!(s, "stranger" | "friend" | "lover")
}
fn stage_name(i: i32) -> &'static str {
    match i {
        0 => "stranger",
        1 => "friend",
        2 => "lover",
        _ => "",
    }
}
fn stage_index(s: &str) -> i32 {
    match s {
        "stranger" => 0,
        "friend" => 1,
        "lover" => 2,
        _ => -1,
    }
}

/// A JSON integer (no fraction, in i32 range).
fn jint(v: Option<&Value>) -> Option<i32> {
    let n = v?.as_f64()?;
    if !n.is_finite() || n != n.floor() || n < i32::MIN as f64 || n > i32::MAX as f64 {
        return None;
    }
    Some(n as i32)
}
fn canonical_int(s: &str) -> Option<i32> {
    let v: i32 = s.parse().ok()?;
    (v.to_string() == s).then_some(v)
}
fn strs(v: Option<&Value>) -> Vec<String> {
    v.and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str).filter(|s| !s.is_empty()).map(String::from).collect()
}

// ------------------------------------------------------------------------------------------------ loading

fn read_effect(o: &Value, e: &mut Effect) -> Result<(), String> {
    if let Some(v) = o.get("favor") {
        e.favor = v.as_f64().filter(|x| x.is_finite()).ok_or("bad_favor")? as f32;
    }
    if let Some(v) = o.get("lewdness") {
        e.lewdness = v.as_f64().filter(|x| x.is_finite()).ok_or("bad_lewdness")? as f32;
    }
    if let Some(v) = o.get("relation_event") {
        e.relation_event = v.as_str().ok_or("unsupported_relation_event")?.into();
        if !e.relation_event.is_empty() && !approved_stage(&e.relation_event) {
            return Err("unsupported_relation_event".into());
        }
    }
    if let Some(v) = o.get("reset_favor") {
        e.reset_favor = v.as_bool().ok_or("bad_reset_favor")?;
    }
    if let Some(v) = o.get("set_flags") {
        for f in v.as_array().ok_or("unsupported_flag_shape")? {
            e.set_flags.push(f.as_str().filter(|s| !s.is_empty()).ok_or("unsupported_flag_shape")?.into());
        }
    }
    Ok(())
}

/// The original condition object as the street reads it: id -1 (supported) speaks, id 0 never (except the
/// introduction fallback), any other id stays disabled. Returns the id.
fn condition_object(c: Option<&Value>, out: &mut Vec<String>) -> Result<i32, String> {
    let c = c.filter(|c| c.is_object()).ok_or("conditions_not_object")?;
    let id = jint(c.get("id")).ok_or("condition_id_invalid")?;
    let filter = c.get("game_speak_filter").and_then(Value::as_str).unwrap_or("");
    let support = c.get("street_support").and_then(Value::as_str).unwrap_or("");
    let fallback = c.get("context").and_then(Value::as_str) == Some("introduction") && c.get("rule").and_then(Value::as_str) == Some("fallback_shuffle_pool") && support == "supported";
    if (id == -1 && support == "supported") || (id == 0 && fallback) {
        out.push("speak=always".into());
    } else if id == 0 {
        out.push("speak=never".into());
    } else {
        out.push(format!("street_support=unsupported:id_{id}:{filter}"));
    }
    Ok(id)
}

/// The voice sequence is complete: every slot matched, indices 0..n without holes, each with a file.
fn voice_sequence(source: &Value, node: &Value) -> (bool, bool, String) {
    let Some(rows) = source.get("voice_sequence").and_then(Value::as_array) else { return (false, false, "voice_sequence_missing".into()) };
    if rows.is_empty() {
        return (true, false, "voice_sequence_empty".into());
    }
    let mut slots: Vec<(i32, String, bool)> = Vec::new();
    for r in rows {
        let Some(i) = jint(r.get("index")) else { return (false, false, "voice_sequence_incomplete:bad_index".into()) };
        let has = r.get("path").and_then(Value::as_str).map(|s| !s.is_empty()).unwrap_or(false) || r.get("zh_ogg").and_then(Value::as_str).map(|s| !s.is_empty()).unwrap_or(false);
        slots.push((i, r.get("status").and_then(Value::as_str).unwrap_or("").to_string(), has));
    }
    slots.sort_by_key(|s| s.0);
    if slots.iter().enumerate().any(|(k, s)| s.0 != k as i32) {
        return (false, false, "voice_sequence_incomplete:index_hole".into());
    }
    let refs: Vec<i32> = node.get("audio_slots").and_then(Value::as_array).into_iter().flatten().filter(|a| a.get("zh_ref").or(a.get("path")).and_then(Value::as_str).map(|s| !s.is_empty()).unwrap_or(false)).filter_map(|a| jint(a.get("index"))).collect();
    for (i, status, has) in &slots {
        if status != "matched" {
            return (false, false, format!("voice_sequence_incomplete:{}", if status.is_empty() { "missing_status" } else { status }));
        }
        if !has && !refs.contains(i) {
            return (false, false, "voice_sequence_incomplete:missing_path".into());
        }
    }
    (true, true, "voice_sequence_matched".into())
}

fn parse_catalogue(root: &Value, data: &mut TalkData) -> Result<(), String> {
    let nodes = root.get("nodes").and_then(Value::as_array).ok_or("catalogue_nodes_missing")?;
    for (k, o) in nodes.iter().enumerate() {
        let warn = |data: &mut TalkData, w: String| {
            data.skipped += 1;
            if data.warnings.len() < 32 {
                data.warnings.push(w);
            }
        };
        if !o.is_object() {
            warn(data, format!("node_{k}_not_object"));
            continue;
        }
        let id = o.get("id").and_then(Value::as_str).unwrap_or("").to_string();
        if id.is_empty() {
            warn(data, "missing_id".into());
            continue;
        }
        if data.by_id.contains_key(&id) {
            warn(data, format!("duplicate_id:{id}"));
            continue;
        }
        let Some(personality) = jint(o.get("personality")) else { warn(data, format!("personality_not_integer:{id}")); continue };
        let category = o.get("category").and_then(Value::as_str).unwrap_or("").to_string();
        if !approved_category(&category) {
            warn(data, format!("unsupported_category:{id}"));
            continue;
        }
        let Some(src) = o.get("source").filter(|s| s.is_object()) else { warn(data, format!("source_not_object:{id}")); continue };
        let Some(stage) = jint(src.get("stage")) else { warn(data, format!("stage_not_integer:{id}")); continue };
        let Some(priority) = jint(src.get("priority")) else { warn(data, format!("bad_priority:{id}")); continue };
        let mut n = Node { id: id.clone(), personality, category: category.clone(), stage, priority: priority as f32, topic_id: -1, command: -1, row: -1, ..Default::default() };
        n.roles = strs(src.get("roles"));
        n.names_him = o.to_string().contains("[P");
        let effects_defined = src.get("effects_defined").and_then(Value::as_bool).unwrap_or(false);
        if let Some(c) = jint(src.get("command")) {
            n.command = c;
        }
        if let Some(p) = jint(src.get("part_index")) {
            n.part_index = p;
        }
        if let Some(r) = jint(src.get("row")) {
            n.row = r;
        }
        n.group = src.get("group").and_then(Value::as_str).unwrap_or("").into();
        if let Some(t) = jint(src.get("topic")).filter(|t| *t >= 0) {
            n.topic_id = t;
        }
        n.containers = src.get("containers").and_then(Value::as_array).into_iter().flatten().filter_map(|c| jint(Some(c))).collect();
        match condition_object(o.get("conditions"), &mut n.conditions) {
            Ok(c) => n.condition_id = c,
            Err(e) => {
                warn(data, format!("{e}:{id}"));
                continue;
            }
        }
        let sn = stage_name(stage);
        n.conditions.push(if sn.is_empty() { format!("street_support=unsupported:stage_{stage}") } else { format!("relation={sn}") });
        let favor = match o.get("favor") {
            Some(v) => match jint(Some(v)) {
                Some(f) => f,
                None => {
                    warn(data, format!("bad_favor:{id}"));
                    continue;
                }
            },
            None => 0,
        };
        let lewd = match o.get("lewdness") {
            Some(v) => match jint(Some(v)) {
                Some(f) => f,
                None => {
                    warn(data, format!("bad_lewdness:{id}"));
                    continue;
                }
            },
            None => 0,
        };
        n.effect = Effect { favor: if effects_defined { favor as f32 } else { 0.0 }, lewdness: if effects_defined { lewd as f32 } else { 0.0 }, talk_cost: talk_cost(&category), memory: id.clone(), topic_id: n.topic_id, ..Effect::new() };
        if let Some(d) = jint(o.get("cooldown_days")).filter(|d| *d > 0) {
            n.effect.cooldown_days = d;
        }
        let (complete, voiced, reason) = voice_sequence(src, o);
        n.voice_complete = complete;
        n.voiced = voiced;
        n.voice_reason = reason;
        if let Some(cs) = o.get("choices") {
            let Some(cs) = cs.as_array() else { warn(data, format!("bad_choices:{id}")); continue };
            let mut ok = true;
            for c in cs {
                let cid = c.get("id").and_then(Value::as_str).unwrap_or("");
                if cid.is_empty() {
                    warn(data, format!("choice_missing_id:{id}"));
                    ok = false;
                    break;
                }
                let mut ch = Choice { id: cid.into(), next: c.get("next").and_then(Value::as_str).unwrap_or("").into(), effect: Effect::new(), ..Default::default() };
                if c.get("conditions").is_some() {
                    if let Err(e) = condition_object(c.get("conditions"), &mut ch.conditions) {
                        warn(data, format!("{e}:{id}"));
                        ok = false;
                        break;
                    }
                }
                if let Err(e) = read_effect(c, &mut ch.effect) {
                    warn(data, format!("{e}:{id}"));
                    ok = false;
                    break;
                }
                ch.effect.talk_cost = 0;
                ch.effect.memory = ch.id.clone();
                n.choices.push(ch);
            }
            if !ok {
                continue;
            }
        }
        let at = data.nodes.len();
        data.by_id.insert(id, at);
        data.by_personality.entry(personality).or_default().push(at);
        data.nodes.push(n);
    }
    // Branches: their outcomes are the rows of one personality with the role named.
    let find = |data: &TalkData, p: i32, row: i32, role: &str| data.nodes.iter().position(|n| n.personality == p && n.row == row && n.single_role(role));
    for o in root.get("branches").and_then(Value::as_array).into_iter().flatten() {
        let id = o.get("id").and_then(Value::as_str).unwrap_or("");
        let (Some(personality), Some(stage), Some(container)) = (jint(o.get("personality")), jint(o.get("stage")), jint(o.get("container"))) else { continue };
        if id.is_empty() {
            continue;
        }
        let mut b = Branch { id: id.into(), personality, stage, container, command: jint(o.get("command")).unwrap_or(-1), condition_id: -1, controlling_row: -1, matched_attribute_type: -1, axis: -1, index_when_true: -1, index_when_false: -1, ..Default::default() };
        b.category = o.get("category").and_then(Value::as_str).unwrap_or("").into();
        b.group = o.get("group").and_then(Value::as_str).unwrap_or("").into();
        if let Some(r) = jint(o.get("controlling_row")) {
            b.controlling_row = r;
        }
        if let Some(c) = o.get("controlling_condition").and_then(|c| jint(c.get("id"))) {
            b.condition_id = c;
        }
        if let Some(sel) = o.get("selection").filter(|s| s.is_object()) {
            b.kind = sel.get("kind").and_then(Value::as_str).unwrap_or("").into();
            b.matched_attribute_type = jint(sel.get("matched_attribute_type")).unwrap_or(-1);
            b.axis = jint(sel.get("axis")).unwrap_or(-1);
            b.operator = sel.get("operator").and_then(Value::as_str).unwrap_or("").into();
            b.threshold = jint(sel.get("threshold")).unwrap_or(0);
            b.index_when_true = jint(sel.get("index_when_true")).unwrap_or(-1);
            b.index_when_false = jint(sel.get("index_when_false")).unwrap_or(-1);
            let w = |k: &str| sel.get(k).and_then(Value::as_array).into_iter().flatten().filter_map(|v| jint(Some(v))).collect::<Vec<i32>>();
            b.weights_matched = w("weights_when_matched");
            b.weights_unmatched = w("weights_when_not_matched");
            b.weights = w("weights");
        }
        for m in o.get("indexer").and_then(Value::as_array).into_iter().flatten() {
            let Some(row) = jint(m.get("row")) else { continue };
            let role = m.get("role").and_then(Value::as_str).unwrap_or("").to_string();
            let node = find(data, personality, row, &role);
            b.indexer.push(Member { row, part_index: jint(m.get("part_index")).unwrap_or(0), node, role });
        }
        if b.controlling_row != -1 {
            b.introduction = find(data, personality, b.controlling_row, "introduction");
        }
        data.branches.push(b);
    }
    Ok(())
}

fn chance_range(v: &Value) -> Option<(i32, i32)> {
    if let Some(s) = v.as_str() {
        let parts: Vec<&str> = s.split(',').filter(|p| !p.is_empty()).collect();
        return match parts.as_slice() {
            [one] => canonical_int(one.trim()).map(|n| (n, n)),
            [a, b] => {
                let (a, b) = (canonical_int(a.trim())?, canonical_int(b.trim())?);
                (a <= b).then_some((a, b))
            }
            _ => None,
        };
    }
    if v.is_object() {
        let (a, b) = (jint(v.get("min"))?, jint(v.get("max"))?);
        return (a <= b).then_some((a, b));
    }
    jint(Some(v)).map(|n| (n, n))
}

fn parse_rules(root: &Value) -> Result<TalkRules, String> {
    let mut r = TalkRules::default();
    let limits = root.get("stepup").ok_or("stepup_missing")?.get("limits").filter(|l| l.is_object()).ok_or("stepup_limits_missing")?;
    let (Some(a), Some(b), Some(c)) = (jint(limits.get("0")), jint(limits.get("1")), jint(limits.get("2"))) else { return Err("stepup_limit_invalid".into()) };
    if a < 0 || b < 0 || c < 0 {
        return Err("stepup_limit_invalid".into());
    }
    r.step_up = Some([a, b, c]);
    r.beyond = jint(limits.get("3"));
    for key in ["preference", "preference_for_exported_personalities"] {
        for (k, v) in root.get(key).and_then(Value::as_object).into_iter().flatten() {
            let Some(p) = canonical_int(k) else { continue };
            let triple = if let Some(a) = v.as_array().filter(|a| a.len() == 3) {
                (jint(a.first()), jint(a.get(1)), jint(a.get(2)))
            } else {
                (jint(v.get("aggressive")), jint(v.get("diligence")), jint(v.get("kindness")))
            };
            if let (Some(a), Some(d), Some(k)) = triple {
                r.preference.insert(p, (a, d, k));
            }
        }
    }
    if let Some(ch) = root.get("communication_chance").filter(|c| c.is_object()) {
        if let Some(range) = ch.get("1:0").and_then(chance_range) {
            r.confession = range;
        }
        if let Some(range) = ch.get("1").and_then(|s| s.get("0")).and_then(chance_range) {
            r.confession = range;
        }
    }
    for row in root.get("communication_condition_probability").and_then(Value::as_array).into_iter().flatten() {
        if jint(row.get("stage")) == Some(1) && jint(row.get("condition")) == Some(0) {
            if let (Some(a), Some(b)) = (jint(row.get("min")), jint(row.get("max"))) {
                if a <= b {
                    r.confession = (a, b);
                }
            }
        }
    }
    Ok(r)
}

fn parse_topics(root: &Value) -> Result<Topics, String> {
    let mut t = Topics::default();
    for o in root.get("topics").and_then(Value::as_array).filter(|a| !a.is_empty()).ok_or("topics_missing")? {
        let (Some(no), Some(category), Some(rarity)) = (jint(o.get("No")), jint(o.get("Category")), jint(o.get("Rarity"))) else { return Err("topic_metadata_invalid".into()) };
        if no < 0 || !(0..=4).contains(&category) || !(0..=4).contains(&rarity) || t.find(no).is_some() {
            return Err("topic_metadata_invalid".into());
        }
        t.topics.push(Topic { no, category, rarity, name: o.get("Name").and_then(Value::as_str).ok_or("topic_metadata_invalid")?.into() });
    }
    for o in root.get("drops").and_then(Value::as_array).into_iter().flatten() {
        let (Some(no), Some(p)) = (jint(o.get("no")), jint(o.get("probability"))) else { return Err("topic_drop_invalid".into()) };
        if t.find(no).is_none() || !(0..=100).contains(&p) || t.drops.contains_key(&no) {
            return Err("topic_drop_invalid".into());
        }
        t.drops.insert(no, p);
    }
    for (kind, len) in [("talk_points", 5), ("listen_points", 3), ("interest", 5)] {
        let rows = root.get(kind).and_then(Value::as_array).filter(|a| a.len() == len).ok_or("topic_points_invalid")?;
        for o in rows {
            let a = jint(o.get(if kind == "listen_points" { "common" } else { "favor" })).ok_or("topic_points_invalid")?;
            let b = jint(o.get(match kind {
                "listen_points" => "rare",
                "interest" => "lewdness",
                _ => "point",
            }))
            .ok_or("topic_points_invalid")?;
            let point = jint(o.get("point")).ok_or("topic_points_invalid")?;
            match kind {
                "talk_points" => {
                    t.talk_favor.push(a);
                    t.talk_point.push(point);
                }
                "listen_points" => {
                    t.listen_common.push(a);
                    t.listen_rare.push(b);
                    t.listen_point.push(point);
                }
                _ => {
                    t.interest_favor.push(a);
                    t.interest_lewd.push(b);
                    t.interest_point.push(point);
                }
            }
        }
    }
    Ok(t)
}

fn read_json(path: &str) -> Result<Value, String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
    serde_json::from_str(text.trim_start_matches('\u{feff}')).map_err(|e| format!("{path}: {e}"))
}

/// Reads the catalogues. Request: catalogue (the dialogue catalogue; rules.json is beside it), topics (optional).
pub fn load(r: &Value) -> Result<TalkData, String> {
    let mut data = TalkData { paths: r.clone(), ..Default::default() };
    let cat = r.get("catalogue").and_then(Value::as_str).ok_or("missing_field: catalogue")?;
    parse_catalogue(&read_json(cat)?, &mut data)?;
    let dir = std::path::Path::new(cat).parent().map(|p| p.to_path_buf()).unwrap_or_default();
    data.rules = parse_rules(&read_json(&dir.join("rules.json").to_string_lossy())?).map_err(|e| format!("rules.json: {e}"))?;
    if let Some(t) = r.get("topics").and_then(Value::as_str).filter(|t| !t.is_empty()) {
        data.topics = parse_topics(&read_json(t)?).map_err(|e| format!("topics: {e}"))?;
    }
    Ok(data)
}

// ------------------------------------------------------------------------------------------------ conditions

fn compare(l: f32, op: &str, r: f32) -> bool {
    match op {
        ">=" => l >= r,
        "<=" => l <= r,
        ">" => l > r,
        "<" => l < r,
        "==" => (l - r).abs() <= 0.001,
        _ => false,
    }
}

fn parse_atom(raw: &str) -> Result<(String, String, String), String> {
    let text = raw.trim();
    let bad = || format!("unsupported_condition:{raw}");
    if text.is_empty() || text.contains(' ') {
        return Err(bad());
    }
    let cut = text.find(|c: char| !(c.is_alphanumeric() || c == '_')).unwrap_or(text.len());
    if cut == 0 {
        return Err(bad());
    }
    let (key, rest) = text.split_at(cut);
    let number = |s: &str| !s.is_empty() && s.chars().enumerate().all(|(k, c)| c.is_ascii_digit() || c == '.' || (k == 0 && (c == '+' || c == '-'))) && s.parse::<f64>().map(|v| v.is_finite()).unwrap_or(false);
    let ident = |s: &str| !s.is_empty() && s.chars().all(|c| c.is_alphanumeric() || matches!(c, '_' | '.' | ':' | '-'));
    match key {
        "favor" | "lewdness" | "talks" => {
            let op = ["<=", ">=", "==", ">", "<"].into_iter().find(|o| rest.starts_with(o)).ok_or_else(bad)?;
            let value = &rest[op.len()..];
            if !number(value) {
                return Err(bad());
            }
            Ok((key.into(), op.into(), value.into()))
        }
        "speak" if rest == "=always" || rest == "=never" => Ok((key.into(), "=".into(), rest[1..].into())),
        "street_support" if rest.starts_with('=') && rest.len() > 1 => Ok((key.into(), "=".into(), rest[1..].into())),
        "relation" | "talked" | "not_talked" | "flag" | "not_flag" | "personality" | "category" => {
            let value = rest.strip_prefix('=').ok_or_else(bad)?;
            if !ident(value) || (key == "relation" && !approved_stage(value)) || (key == "category" && !approved_category(value)) {
                return Err(bad());
            }
            Ok((key.into(), "=".into(), value.into()))
        }
        _ => Err(bad()),
    }
}

fn eval_atom(key: &str, op: &str, value: &str, r: &Relation, personality: &str, category: &str) -> Result<(), String> {
    let n: f32 = value.parse().unwrap_or(0.0);
    let ok = match key {
        "favor" => compare(r.favor, op, n),
        "lewdness" => compare(r.lewdness, op, n),
        "talks" => compare(r.talks_remaining as f32, op, n),
        "relation" => r.stage == value,
        "talked" => r.talked.iter().any(|t| t == value),
        "not_talked" => !r.talked.iter().any(|t| t == value),
        "flag" => r.has_flag(value),
        "not_flag" => !r.has_flag(value),
        "personality" => personality == value,
        "category" => category == value,
        "speak" => match value {
            "always" => true,
            "never" => return Err("speak_never".into()),
            _ => return Err("unsupported_condition:speak".into()),
        },
        "street_support" => return Err(if value.starts_with("unsupported") { format!("street_support_{value}") } else { format!("street_support_unsupported:{value}") }),
        _ => return Err(format!("unsupported_condition:{key}")),
    };
    if ok {
        Ok(())
    } else if matches!(key, "favor" | "lewdness" | "talks") {
        Err(format!("condition_failed:{key}{op}{value}"))
    } else {
        Err(format!("condition_failed:{key}={value}"))
    }
}

/// All conditions hold (Err: the first reason it does not).
pub fn eval_conditions(conditions: &[String], r: &Relation, personality: &str, category: &str) -> Result<(), String> {
    let atoms: Vec<_> = conditions.iter().map(|c| parse_atom(c)).collect::<Result<_, _>>()?;
    for (k, o, v) in atoms {
        eval_atom(&k, &o, &v, r, personality, category)?;
    }
    Ok(())
}

pub fn topic_token(id: i32) -> String {
    format!("topic:{id}")
}

pub fn topic_cooling(r: &Relation, id: &str) -> bool {
    !id.is_empty() && r.cooldowns.get(id).map(|until| r.day < *until).unwrap_or(false)
}

/// Facts about who is talking: her personality (string and number), the talk budget switch.
pub struct Who {
    pub personality: String,
    pub personality_id: i32,
    pub talk_limit: bool,
    pub budget: i32,
}

impl Who {
    fn from(r: &Value) -> Who {
        let p = r.get("personality").and_then(Value::as_str).unwrap_or("").to_string();
        let id = r.get("personality_id").and_then(Value::as_i64).map(|v| v as i32).or_else(|| canonical_int(&p)).unwrap_or(-1);
        Who { personality: p, personality_id: id, talk_limit: r.get("talk_limit").and_then(Value::as_bool).unwrap_or(false), budget: r.get("budget").and_then(Value::as_i64).unwrap_or(5) as i32 }
    }
}

fn line_eligible(n: &Node, r: &Relation, who: &Who) -> Result<(), String> {
    eval_conditions(&n.conditions, r, &who.personality, &n.category)?;
    if !n.generic() {
        return Err(format!("branch_role_unsupported:{}", n.roles.first().map(String::as_str).unwrap_or("missing")));
    }
    if !n.voice_complete {
        return Err(if n.voice_reason.is_empty() { "voice_sequence_incomplete".into() } else { n.voice_reason.clone() });
    }
    if n.once && r.talked.contains(&n.effect.memory) {
        return Err("once_already_talked".into());
    }
    if n.names_him && r.stage == "stranger" {
        return Err("names_him_before_she_knows_it".into());
    }
    if who.talk_limit && talk_cost(&n.category) > r.talks_remaining {
        return Err("talk_budget".into());
    }
    if topic_cooling(r, &n.id) || (n.topic_id >= 0 && topic_cooling(r, &topic_token(n.topic_id))) {
        return Err("topic_cooldown".into());
    }
    Ok(())
}

/// A line already said or cooling down may be said again once a category is used up (a new round).
fn line_eligible_new_round(n: &Node, r: &Relation, who: &Who) -> bool {
    eval_conditions(&n.conditions, r, &who.personality, &n.category).is_ok() && n.generic() && n.voice_complete && n.voiced && !(n.names_him && r.stage == "stranger") && !(who.talk_limit && talk_cost(&n.category) > r.talks_remaining)
}

// ------------------------------------------------------------------------------------------------ caps and commits

impl TalkData {
    /// The favor cap at a stage; a stranger's is her own (stranger_cap, 10..100) when she has one. None: no step-up rules.
    pub fn favor_limit(&self, r: &Relation, stage: &str) -> Option<i32> {
        let s = self.rules.step_up?;
        Some(match stage {
            "stranger" if r.stranger_cap > 0 => r.stranger_cap.clamp(10, 100),
            "stranger" => s[0],
            "friend" => s[1].max(FRIEND_FAVOR_CAP),
            "lover" => s[2],
            _ => return None,
        })
    }

    fn by_personality(&self, p: i32) -> &[usize] {
        self.by_personality.get(&p).map(Vec::as_slice).unwrap_or(&[])
    }
}

/// A finished line (or event) written into the relationship. Err: why not (nothing changes).
pub fn commit(data: &TalkData, r: &mut Relation, transaction: &str, e: &Effect, talk_limit: bool) -> Result<(), (bool, String)> {
    if transaction.is_empty() {
        return Err((false, "missing_transaction".into()));
    }
    if !e.relation_event.is_empty() && !approved_stage(&e.relation_event) {
        return Err((false, "unsupported_relation_event".into()));
    }
    if r.committed.iter().any(|t| t == transaction) {
        return Err((true, "duplicate_transaction".into()));
    }
    if talk_limit && r.talks_remaining < e.talk_cost {
        return Err((false, "talk_budget".into()));
    }
    let favor_changes = e.favor.abs() > f32::EPSILON || e.reset_favor;
    let limit = data.favor_limit(r, if e.relation_event.is_empty() { &r.stage } else { &e.relation_event });
    if favor_changes && limit.is_none() {
        return Err((false, "stepup_limit_missing".into()));
    }
    if !e.relation_event.is_empty() {
        r.stage = e.relation_event.clone();
        crate::day::enter_stage(r);
    }
    if e.reset_favor {
        r.favor = 0.0;
    }
    if let Some(l) = limit {
        r.favor = (r.favor + e.favor).clamp(0.0, l as f32);
    }
    r.lewdness = (r.lewdness + e.lewdness).clamp(0.0, LEWDNESS_CAP);
    if talk_limit {
        r.talks_remaining -= e.talk_cost;
    }
    add_token(&mut r.talked, &e.memory);
    for f in &e.set_flags {
        add_token(&mut r.flags, f);
    }
    if e.topic_id >= 0 {
        add_token(&mut r.topics, &topic_token(e.topic_id));
    }
    if e.cooldown_days > 0 {
        let id = if e.topic_id >= 0 { topic_token(e.topic_id) } else { e.memory.clone() };
        if !id.is_empty() {
            r.cooldowns.insert(id, r.day + e.cooldown_days);
        }
    }
    add_token(&mut r.committed, transaction);
    Ok(())
}

fn add_token(list: &mut Vec<String>, token: &str) {
    if !token.is_empty() && !list.iter().any(|t| t == token) {
        list.push(token.into());
    }
    list.sort();
}

// ------------------------------------------------------------------------------------------------ choosing a line

/// Index by weights (0..99 roll); a weight of 100 always wins.
pub fn weight_index(weights: &[i32], roll: i32) -> Result<usize, String> {
    if weights.is_empty() {
        return Err("weights_missing".into());
    }
    if let Some(k) = weights.iter().position(|w| *w == 100) {
        return Ok(k);
    }
    if !(0..=99).contains(&roll) {
        return Err("roll_out_of_range".into());
    }
    if weights.iter().any(|w| *w < 0) {
        return Err("weights_invalid".into());
    }
    let sum: i32 = weights.iter().sum();
    if sum <= 0 {
        return Err("weights_invalid".into());
    }
    let mut cursor = roll as f64 * sum as f64 / 100.0;
    for (k, w) in weights.iter().enumerate() {
        cursor -= *w as f64;
        if cursor < 0.0 {
            return Ok(k);
        }
    }
    Ok(0)
}

#[derive(Serialize, Debug, Clone, Default)]
pub struct Decision {
    pub decided: bool,
    pub playable: bool,
    pub refusal: bool,
    pub node: Option<usize>,
    pub role: String,
    pub reason: String,
    pub effect: Effect,
}

fn finish(data: &TalkData, r: &Relation, who: &Who, b: &Branch, chosen: i32, d: &mut Decision) {
    let Some(node) = usize::try_from(chosen).ok().and_then(|c| b.indexer.get(c)).and_then(|m| m.node) else {
        d.reason = "branch_index_missing".into();
        return;
    };
    let n = &data.nodes[node];
    if n.generic() {
        d.reason = "generic_not_a_branch_outcome".into();
        return;
    }
    d.decided = true;
    d.node = Some(node);
    d.role = if n.roles.len() == 1 { n.roles[0].clone() } else { b.indexer[chosen as usize].role.clone() };
    d.refusal = d.role == "failure" || d.role == "no_interested";
    d.effect = n.effect.clone();
    d.playable = n.voice_complete && eval_conditions(&n.conditions, r, &who.personality, &n.category).is_ok();
}

/// A branch's outcome: by her preference axes, by weights, or by talk temper.
pub fn resolve_branch(data: &TalkData, r: &Relation, who: &Who, b: &Branch, roll: i32) -> Decision {
    let mut d = Decision::default();
    if stage_index(&r.stage) != b.stage {
        d.reason = "stage_mismatch".into();
        return d;
    }
    match b.kind.as_str() {
        "preference_compare" => {
            let Some((a, dl, k)) = data.rules.preference.get(&b.personality).copied() else {
                d.reason = if data.rules.preference.is_empty() { "preference_evidence_missing" } else { "preference_personality_missing" }.into();
                return d;
            };
            if !matches!(b.operator.as_str(), "<=" | ">=" | "<" | ">" | "==") || !(0..=2).contains(&b.axis) || b.index_when_true == -1 || b.index_when_false == -1 {
                d.reason = format!("branch_rule_unsupported:{}", b.condition_id);
                return d;
            }
            let v = [a, dl, k][b.axis as usize];
            let pass = match b.operator.as_str() {
                "<=" => v <= b.threshold,
                ">=" => v >= b.threshold,
                "<" => v < b.threshold,
                ">" => v > b.threshold,
                _ => v == b.threshold,
            };
            d.reason = format!("preference_axis{}{}{}", b.axis, b.operator, b.threshold);
            finish(data, r, who, b, if pass { b.index_when_true } else { b.index_when_false }, &mut d);
        }
        "attribute_weights" | "random_weights" => {
            // No attribute type is supplied on the street: the weights for "not matched".
            let w = if b.kind == "attribute_weights" { &b.weights_unmatched } else { &b.weights };
            match weight_index(w, roll) {
                Ok(k) => {
                    d.reason = format!("{}:{}", b.kind, w.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(","));
                    finish(data, r, who, b, k as i32, &mut d);
                }
                Err(e) => d.reason = e,
            }
        }
        "talk_temper" => d.reason = "talk_temper_absent".into(),
        _ => d.reason = if b.indexer.is_empty() { "not_a_branch".into() } else { format!("branch_rule_unsupported:{}", b.condition_id) },
    }
    d
}

/// The talk topic he chose (daily, romance, intimacy, greeting, farewell): a farewell row that fits the moment, else for
/// talk categories a topic branch (one not talked about today, else any as a new round), else the highest-priority line
/// she may say (never the one just said), else any line of the category again. Request: category, relation,
/// personality, personality_id, budget, talk_limit, previous_node, last_branch, seed. Answer: node (id) or why not,
/// branch, effect (for a branch), new_round.
pub fn topic(data: &TalkData, r: &Value) -> Result<Value, String> {
    let rel: Relation = serde_json::from_value(r.get("relation").cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))?;
    let who = Who::from(r);
    let category = r.get("category").and_then(Value::as_str).unwrap_or("");
    let previous = r.get("previous_node").and_then(Value::as_str).unwrap_or("");
    let last_branch = r.get("last_branch").and_then(Value::as_str).unwrap_or("");
    let mut rng = RandomStream::new(r.get("seed").and_then(Value::as_i64).unwrap_or(0) as i32);
    let pick = |rng: &mut RandomStream, list: &[usize]| list[rng.rand_range(0, list.len() as i32 - 1) as usize];
    if category == "farewell" {
        let list = farewell_lines(data, &rel, &who);
        if list.is_empty() {
            return Ok(json!({"message": crate::tr::t("talk.no_farewell_line")}));
        }
        return Ok(json!({"node": data.nodes[pick(&mut rng, &list)].id}));
    }
    let talk = matches!(category, "daily" | "romance" | "intimacy");
    let branch = |rng: &mut RandomStream, ignore_cooling: bool| -> Option<Value> {
        let mut pool = Vec::new();
        for (k, b) in data.branches.iter().enumerate() {
            if b.personality != who.personality_id || b.category != category || b.stage != stage_index(&rel.stage) || b.group != "Talk" || b.indexer.is_empty() {
                continue;
            }
            let complete = (ignore_cooling || !topic_cooling(&rel, &b.id))
                && b.indexer.iter().all(|m| m.node.map(|n| data.nodes[n].voice_complete && eval_conditions(&data.nodes[n].conditions, &rel, &who.personality, &data.nodes[n].category).is_ok()).unwrap_or(false));
            if complete {
                let d = resolve_branch(data, &rel, &who, b, 0);
                if d.decided && d.playable {
                    pool.push(k);
                }
            }
        }
        if pool.is_empty() {
            return None;
        }
        if pool.len() > 1 {
            pool.retain(|k| data.branches[*k].id != last_branch);
        }
        let b = &data.branches[pick(rng, &pool)];
        let roll = rng.rand_range(0, 99);
        let d = resolve_branch(data, &rel, &who, b, roll);
        if d.decided && d.playable {
            let mut e = d.effect.clone();
            e.memory = b.id.clone();
            e.cooldown_days = 1;
            return Some(json!({"node": data.nodes[d.node.unwrap()].id, "branch": b.id, "effect": e, "role": d.role, "roll": roll, "reason": d.reason, "new_round": ignore_cooling}));
        }
        Some(json!({"message": crate::tr::m("talk.reply_unplayable", &[("reason", crate::tr::a(&d.reason))])}))
    };
    if talk {
        if who.talk_limit && rel.talks_remaining < talk_cost(category) {
            return Ok(json!({"message": crate::tr::t("talk.talks_used_today")}));
        }
        if let Some(v) = branch(&mut rng, false) {
            return Ok(v);
        }
    }
    let (items, empty) = list_lines(data, &rel, &who, category);
    let mut eligible: Vec<usize> = Vec::new();
    let mut highest = f32::MIN;
    for (n, ok, _) in &items {
        let line = &data.nodes[*n];
        if !*ok || !line.voiced {
            continue;
        }
        if line.priority > highest {
            highest = line.priority;
            eligible.clear();
        }
        if line.priority == highest {
            eligible.push(*n);
        }
    }
    if eligible.len() > 1 {
        eligible.retain(|n| data.nodes[*n].id != previous);
    }
    if eligible.is_empty() && talk {
        if let Some(v) = branch(&mut rng, true) {
            return Ok(v);
        }
    }
    let mut new_round = false;
    if eligible.is_empty() {
        // Every line of this category was already said: a new round (budget and conditions still apply).
        eligible = data.by_personality(who.personality_id).iter().copied().filter(|n| data.nodes[*n].category == category && line_eligible_new_round(&data.nodes[*n], &rel, &who)).collect();
        if eligible.len() > 1 {
            eligible.retain(|n| data.nodes[*n].id != previous);
        }
        new_round = !eligible.is_empty();
    }
    if eligible.is_empty() {
        return Ok(json!({"message": crate::tr::m("talk.no_playable_reply", &[("empty", crate::tr::a(&empty))])}));
    }
    Ok(json!({"node": data.nodes[pick(&mut rng, &eligible)].id, "new_round": new_round}))
}

/// EndConversation rows of her personality and stage whose own predicate holds; the highest priority ones.
pub fn farewell_lines(data: &TalkData, r: &Relation, who: &Who) -> Vec<usize> {
    let limit = data.favor_limit(r, &r.stage).unwrap_or(-1);
    let mut best = f32::MIN;
    let mut out = Vec::new();
    for (k, n) in data.nodes.iter().enumerate() {
        if n.category != "farewell" || n.group != "EndConversation" || n.personality != who.personality_id || n.stage != stage_index(&r.stage) || !n.generic() || !n.voice_complete {
            continue;
        }
        let budget = who.budget as f32;
        let ok = match n.condition_id {
            -1 => r.talks_remaining <= 0,
            0 => r.talks_remaining <= 0 && limit > 0 && r.favor / limit as f32 >= 0.5,
            1 => r.talks_remaining <= 0 && r.lewdness >= 50.0,
            2 => who.budget > 0 && (r.talks_remaining as f32 / budget) < 0.5,
            3 => who.budget > 0 && (r.talks_remaining as f32 / budget) >= 0.5,
            _ => false,
        };
        if !ok {
            continue;
        }
        if n.priority > best {
            best = n.priority;
            out.clear();
        }
        if n.priority == best {
            out.push(k);
        }
    }
    out
}

/// Her lines of a category: the eligible first (priority, then id), then the blocked with why, at most eight.
fn list_lines(data: &TalkData, r: &Relation, who: &Who, category: &str) -> (Vec<(usize, bool, String)>, String) {
    if who.personality.is_empty() {
        return (vec![], "missing_personality".into());
    }
    let Some(bucket) = data.by_personality.get(&who.personality_id) else { return (vec![], "no_lines_for_personality".into()) };
    let (mut ok, mut blocked): (Vec<(usize, bool, String)>, Vec<(usize, bool, String)>) = (vec![], vec![]);
    for &k in bucket {
        let n = &data.nodes[k];
        if n.category != category {
            continue;
        }
        match line_eligible(n, r, who) {
            Ok(()) => ok.push((k, true, String::new())),
            Err(why) => blocked.push((k, false, why)),
        }
    }
    ok.sort_by(|a, b| {
        let (pa, pb) = (data.nodes[a.0].priority, data.nodes[b.0].priority);
        if (pa - pb).abs() > f32::EPSILON {
            pb.partial_cmp(&pa).unwrap()
        } else {
            data.nodes[a.0].id.cmp(&data.nodes[b.0].id)
        }
    });
    blocked.sort_by(|a, b| data.nodes[a.0].id.cmp(&data.nodes[b.0].id));
    let out: Vec<_> = ok.into_iter().chain(blocked).take(MAX_CHOICES).collect();
    let empty = if out.is_empty() { "no_lines_for_category".to_string() } else { String::new() };
    (out, empty)
}

// ------------------------------------------------------------------------------------------------ events

#[derive(Serialize, Debug, Clone, Default)]
pub struct EventResult {
    pub resolved: bool,
    pub refusal: bool,
    pub chance: i32,
    pub roll: i32,
    pub reason: String,
    pub stage: String,
    pub favor: f32,
    pub line: String,
    pub playable: bool,
    pub effect: Effect,
    /// A very shy girl's answer (shy_confession): "" as usual, "onlookers" (refused, people around) or "runs_away"
    /// (she runs off and says yes by phone).
    pub shy: String,
}

/// Shyness from which she is "very shy" (0..100, her profile's traits.shyness).
pub const VERY_SHY: i32 = 70;
/// Expected crowd (places::crowd_at) from which a place counts as people around.
pub const SHY_CROWD: f32 = 0.5;

/// A very shy girl and a confession: with people around (another girl near, or a busy place at this hour) she cannot
/// say yes and refuses, without holding it against him unless she would have refused anyway; alone, a yes may come
/// out as running off and answering by phone ((shyness - 60) x 3 %, 75 % at 85).
pub fn shy_confession(res: &mut EventResult, shyness: i32, others_near: i32, place: &str, hours: f64, shy_roll: i32) {
    if !res.resolved || shyness < VERY_SHY {
        return;
    }
    let crowded = others_near > 0 || crate::places::crowd_at(place, hours) >= SHY_CROWD;
    if crowded {
        if !res.refusal {
            res.refusal = true;
            res.effect.relation_event.clear();
            res.effect.reset_favor = false;
            res.effect.favor = 0.0;
            res.effect.set_flags.clear();
        }
        res.reason = "confession_shy_onlookers".into();
        res.shy = "onlookers".into();
    } else if !res.refusal && shy_roll.rem_euclid(100) < ((shyness - 60) * 3).clamp(0, 100) {
        res.shy = "runs_away".into();
    }
}

/// What he is told when a friendship or confession does not happen, with the numbers that decide it.
pub fn event_message(d: &TalkData, r: &Relation, res: &EventResult) -> String {
    let cap = d.favor_limit(r, &r.stage).unwrap_or(0);
    let gate = d.rules.step_up.map(|s| (s[1].min(100) as f32 * 0.7).ceil() as i32).unwrap_or(70);
    use crate::tr::{a, m, t};
    let favor = r.favor.round() as i64;
    match res.reason.as_str() {
        "friendship_rank_up" | "confession_success" | "confession_failure" | "confession_shy_onlookers" => String::new(),
        "friendship_requires_stranger" if r.stage == "friend" => m("talk.already_friends_confess_at", &[("gate", a(&gate)), ("favor", a(&favor))]),
        "friendship_requires_stranger" => t("talk.already_lovers").into(),
        "friendship_favor_below_limit" => m("talk.friend_needs_favor", &[("favor", a(&favor)), ("cap", a(&cap))]),
        "confession_requires_friend" => {
            let to_friend = d.favor_limit(r, "stranger").unwrap_or(cap);
            m("talk.confess_needs_friend", &[("to_friend", a(&to_friend)), ("favor", a(&favor))])
        }
        "confession_favor_below_0_7" => m("talk.confess_needs_favor", &[("gate", a(&gate)), ("favor", a(&favor))]),
        "already_lover" => t("talk.already_lovers").into(),
        "talk_budget" => t("talk.budget_used").into(),
        _ => t("talk.not_enough_favor").into(),
    }
}

/// Becoming friends: a stranger whose favor reached her cap.
pub fn friendship(data: &TalkData, r: &Relation, who: &Who) -> EventResult {
    let mut res = EventResult { stage: r.stage.clone(), favor: r.favor, ..Default::default() };
    // The original rank-up row: farewell, stage 0, condition 10.
    let mut best: Option<usize> = None;
    for &k in data.by_personality(who.personality_id) {
        let n = &data.nodes[k];
        if n.category != "farewell" || n.stage != 0 || n.condition_id != 10 || !n.generic() {
            continue;
        }
        if best.map(|b| n.priority > data.nodes[b].priority || (n.priority == data.nodes[b].priority && n.id < data.nodes[b].id)).unwrap_or(true) {
            best = Some(k);
        }
    }
    if let Some(b) = best {
        let n = &data.nodes[b];
        res.line = n.id.clone();
        res.playable = n.voice_complete && eval_conditions(&n.conditions, r, &who.personality, &n.category).is_ok();
    }
    if data.rules.step_up.is_none() {
        res.reason = "stepup_limit_missing".into();
        return res;
    }
    if r.stage != "stranger" {
        res.reason = "friendship_requires_stranger".into();
        return res;
    }
    if r.favor + 0.001 < data.favor_limit(r, "stranger").unwrap_or(i32::MAX) as f32 {
        res.reason = "friendship_favor_below_limit".into();
        return res;
    }
    res.resolved = true;
    res.reason = "friendship_rank_up".into();
    res.effect = Effect { relation_event: "friend".into(), reset_favor: true, memory: "event:friendship".into(), set_flags: vec!["talk_event:0".into()], ..Effect::new() };
    res
}

/// Confessing: friends at 70% of the original friend cap (100) or anyone over 100 favor; the chance rises with favor
/// (over 100: 50%, then one point per favor point, certain from 150).
pub fn confession(data: &TalkData, r: &Relation, who: &Who, roll: i32) -> EventResult {
    let mut res = EventResult { stage: r.stage.clone(), favor: r.favor, roll, ..Default::default() };
    let guaranteed = r.favor > 100.0;
    if r.stage == "lover" {
        res.reason = "already_lover".into();
        return res;
    }
    if r.stage != "friend" && !guaranteed {
        res.reason = "confession_requires_friend".into();
        return res;
    }
    let Some(step) = data.rules.step_up.filter(|s| s[1] > 0) else {
        res.reason = "stepup_limit_missing".into();
        return res;
    };
    let base = step[1].min(100);
    if !(base > 0 && r.favor as f64 / base as f64 >= 0.7) {
        res.reason = "confession_favor_below_0_7".into();
        return res;
    }
    res.chance = if guaranteed {
        // Over 100 favor: an even chance, rising evenly to certain at 150.
        (50 + (r.favor - 100.0).floor() as i32).clamp(50, 100)
    } else {
        let (min, max) = (data.rules.confession.0 as f64, data.rules.confession.1 as f64);
        let v = if min != max { min + (max - min) * (r.favor as f64 / 100.0).clamp(0.0, 1.0) } else { min };
        // Not over 100 yet: half the table's chance (over 100 starts at 50%, so it never drops past 100).
        ((v * 0.5).trunc() as i32).clamp(0, 100)
    };
    if who.talk_limit && r.talks_remaining < 1 {
        res.reason = "talk_budget".into();
        return res;
    }
    let chosen = match weight_index(&[res.chance, 100 - res.chance], roll) {
        Ok(k) => k,
        Err(e) => {
            res.reason = e;
            return res;
        }
    };
    res.resolved = true;
    res.refusal = chosen != 0;
    res.effect = Effect { talk_cost: 1, memory: "event:confession".into(), ..Effect::new() };
    if !res.refusal {
        res.reason = "confession_success".into();
        res.effect.relation_event = "lover".into();
        res.effect.reset_favor = true;
        res.effect.favor = CONFESS_ACCEPT_FAVOR;
        if guaranteed && r.stage == "stranger" {
            res.effect.set_flags.push("talk_event:0".into());
        }
        res.effect.set_flags.extend(["talk_event:1".to_string(), "talk_event:2".to_string()]);
    } else {
        res.reason = "confession_failure".into();
        res.effect.favor = CONFESS_REFUSE_FAVOR;
    }
    res
}

// ------------------------------------------------------------------------------------------------ invitations, topics

pub fn invitation_token(command: i32) -> String {
    format!("invitation:{command}")
}

/// A meal (3) or a walk (4) invitation may be asked: budget (walk costs two), a meal once a day.
pub fn can_invite(r: &Relation, command: i32, talk_limit: bool) -> Result<(), String> {
    if command != 3 && command != 4 {
        return Err("unsupported_invitation".into());
    }
    if talk_limit && r.talks_remaining < if command == 4 { 2 } else { 1 } {
        return Err("talk_budget".into());
    }
    if command == 3 && topic_cooling(r, &invitation_token(command)) {
        return Err("invitation_already_attempted".into());
    }
    Ok(())
}

/// What an answer about a collected topic is worth: asked about one, the same topic or category (common or rare);
/// telling one, its rarity and her interests.
fn collected_effect(t: &Topics, answer: Option<i32>, asked: Option<i32>, interests: &[i32]) -> Effect {
    let mut e = Effect { talk_cost: 1, ..Effect::new() };
    let answer = answer.and_then(|a| t.find(a));
    if let Some(asked) = asked.and_then(|a| t.find(a)) {
        let m = match answer {
            Some(a) if a.no == asked.no => 0,
            Some(a) if a.category == asked.category => 1,
            _ => 2,
        };
        let common = answer.map(|a| a.rarity == 0).unwrap_or(false);
        e.favor = if common { t.listen_common.get(m).copied().unwrap_or(0) } else { t.listen_rare.get(if m == 1 { 2 } else { m }).copied().unwrap_or(0) } as f32;
        return e;
    }
    let Some(a) = answer else { return e };
    let Some(base) = t.talk_favor.get(a.rarity as usize).copied() else { return e };
    e.favor = if a.category == 4 { 0.0 } else { base as f32 };
    e.lewdness = if a.category == 4 { base as f32 } else { 0.0 };
    if interests.contains(&a.category) {
        if let (Some(f), Some(l)) = (t.interest_favor.get(a.category as usize), t.interest_lewd.get(a.category as usize)) {
            e.favor += *f as f32;
            e.lewdness += *l as f32;
        }
    }
    e
}

/// How an answer matches what she asked: 0 the same topic, 1 the same category, 2 anything else (or nothing).
fn topic_match(t: &Topics, answer: Option<i32>, asked: i32) -> usize {
    let (Some(a), Some(q)) = (answer.and_then(|a| t.find(a)), t.find(asked)) else { return 2 };
    if a.no == q.no {
        0
    } else if a.category == q.category {
        1
    } else {
        2
    }
}

/// Topic points an answer earns (her topic points), and the desire an erotic topic stirs.
fn topic_points(t: &Topics, answer: Option<i32>, asked: Option<i32>, interests: &[i32]) -> (i32, f32) {
    if let Some(q) = asked {
        return (t.listen_point.get(topic_match(t, answer, q)).copied().unwrap_or(0), 0.0);
    }
    let Some(a) = answer.and_then(|a| t.find(a)) else { return (0, 0.0) };
    let point = t.talk_point.get(a.rarity as usize).copied().unwrap_or(0) + if interests.contains(&a.category) { t.interest_point.get(a.category as usize).copied().unwrap_or(0) } else { 0 };
    (point, if a.category == 4 { 10.0 } else { 0.0 })
}

/// The topic a dialogue line carries: for a generic Talk row, the topic of the first (male) part of its sequence.
fn dialogue_topic(data: &TalkData, line: &Node) -> i32 {
    if !line.generic() || line.group != "Talk" {
        return line.topic_id;
    }
    for f in &data.nodes {
        if f.personality == line.personality && f.stage == line.stage && f.group == line.group && f.command == line.command && f.part_index == 0 && f.generic() && line.containers.iter().any(|c| f.containers.contains(c)) {
            return f.topic_id;
        }
    }
    line.topic_id
}

/// A finished dialogue line may drop its topic into his stock (romance and intimacy commands raise the chance).
fn topic_drop(t: &Topics, stock: i32, no: i32, command: i32, count: i32, roll: i32) -> bool {
    let Some(p) = t.drops.get(&no).copied().filter(|p| *p > 0) else { return false };
    if t.find(no).is_none() || stock >= 99 || !(0..=99).contains(&roll) {
        return false;
    }
    let chance = (p + if command == 1 || command == 2 { count.max(0) * 2 } else { 0 }).clamp(0, 100);
    roll < chance
}

// ------------------------------------------------------------------------------------------------ kinds

fn relation(r: &Value) -> Result<Relation, String> {
    serde_json::from_value(r.get("relation").cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))
}
fn effect_of(v: Option<&Value>) -> Result<Effect, String> {
    let mut e: Effect = serde_json::from_value(v.cloned().unwrap_or(json!({}))).map_err(|e| format!("effect: {e}"))?;
    if v.and_then(|v| v.get("topic_id")).is_none() {
        e.topic_id = -1;
    }
    Ok(e)
}

pub fn eval(data: Option<&TalkData>, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    let need = || data.ok_or_else(|| "talk_not_loaded".to_string());
    Some((|| -> Result<Value, String> {
        match kind {
            "favor_limit" => {
                let rel = relation(r)?;
                let stage = r.get("stage").and_then(Value::as_str).map(String::from).unwrap_or_else(|| rel.stage.clone());
                Ok(json!({"limit": need()?.favor_limit(&rel, &stage).unwrap_or(-1)}))
            }
            "talk_topic" => topic(need()?, r),
            "talk_lines" => {
                let d = need()?;
                let (items, empty) = list_lines(d, &relation(r)?, &Who::from(r), r.get("category").and_then(Value::as_str).unwrap_or(""));
                Ok(json!({"items": items.iter().map(|(n, ok, why)| json!({"node": d.nodes[*n].id, "eligible": ok, "reason": why})).collect::<Vec<_>>(), "empty": empty}))
            }
            "talk_commit" => {
                let d = need()?;
                let before = relation(r)?;
                let mut rel = before.clone();
                let e = effect_of(r.get("effect"))?;
                let talk_limit = r.get("talk_limit").and_then(Value::as_bool).unwrap_or(false);
                match commit(d, &mut rel, r.get("transaction").and_then(Value::as_str).unwrap_or(""), &e, talk_limit) {
                    Ok(()) => Ok(json!({"applied": true, "relation": rel.patch(&before)})),
                    Err((dup, why)) => Ok(json!({"applied": false, "duplicate": dup, "reason": why})),
                }
            }
            "talk_event" => {
                let d = need()?;
                let rel = relation(r)?;
                let who = Who::from(r);
                let res = match r.get("event").and_then(Value::as_str) {
                    Some("friendship") => friendship(d, &rel, &who),
                    Some("confession") => {
                        let roll = r.get("roll").and_then(Value::as_i64).unwrap_or(0) as i32;
                        let mut res = confession(d, &rel, &who, roll);
                        let shyness = r.get("profile").map(|p| crate::traits::Traits::from_profile(p).shyness).unwrap_or(0);
                        let shy_roll = r.get("shy_roll").and_then(Value::as_i64).map(|v| v as i32).unwrap_or((roll * 37 + 11) % 100);
                        let others = r.get("others_near").and_then(Value::as_i64).unwrap_or(0) as i32;
                        shy_confession(&mut res, shyness, others, r.get("place").and_then(Value::as_str).unwrap_or(""), r.get("hours").and_then(Value::as_f64).unwrap_or(12.0), shy_roll);
                        res
                    }
                    other => return Err(format!("unknown_event: {other:?}")),
                };
                let mut out = json!(res);
                out["message"] = json!(event_message(d, &rel, &res));
                Ok(out)
            }
            "auto_friendship" => {
                // A stranger whose favor reached her cap is his friend now (whatever raised it).
                let d = need()?;
                let before = relation(r)?;
                let Some(cap) = d.favor_limit(&before, &before.stage).filter(|c| *c > 0) else { return Ok(json!({"friend": false})) };
                if before.stage != "stranger" || before.stranger_cap <= 0 || before.favor + 0.001 < cap as f32 {
                    return Ok(json!({"friend": false}));
                }
                let mut rel = before.clone();
                rel.stage = "friend".into();
                crate::day::enter_stage(&mut rel);
                rel.favor = 0.0;
                rel.add_flag("talk_event:0");
                Ok(json!({"friend": true, "cap": cap, "relation": rel.patch(&before)}))
            }
            "free_talk_favor" => {
                // The model's favor change in a free talk, kept under her stage's cap.
                let d = need()?;
                let before = relation(r)?;
                let mut rel = before.clone();
                let limit = d.favor_limit(&rel, &rel.stage).map(|l| l as f32).unwrap_or(150.0);
                let delta = r.get("delta").and_then(Value::as_f64).unwrap_or(0.0) as f32;
                if delta != 0.0 {
                    rel.favor = (rel.favor + delta).clamp(0.0, limit.min(1000.0));
                }
                Ok(json!({"before": before.favor, "after": rel.favor, "limit": limit, "relation": rel.patch(&before)}))
            }
            "favor_clamp" => {
                // Loading: every favor within her stage's cap.
                let d = need()?;
                let mut out = Vec::new();
                for v in r.get("relations").and_then(Value::as_array).into_iter().flatten() {
                    let Ok(before) = serde_json::from_value::<Relation>(v.clone()) else { continue };
                    let Some(l) = d.favor_limit(&before, &before.stage) else { continue };
                    let mut rel = before.clone();
                    rel.favor = rel.favor.clamp(0.0, l as f32);
                    if rel.favor != before.favor {
                        out.push(json!({"id": rel.id, "favor": rel.favor}));
                    }
                }
                Ok(json!({"changes": out}))
            }
            "topic_cooling" => Ok(json!({"cooling": topic_cooling(&relation(r)?, r.get("id").and_then(Value::as_str).unwrap_or(""))})),
            "invite_check" => {
                let rel = relation(r)?;
                let command = r.get("command").and_then(Value::as_i64).unwrap_or(0) as i32;
                Ok(match can_invite(&rel, command, r.get("talk_limit").and_then(Value::as_bool).unwrap_or(false)) {
                    Ok(()) => json!({"ok": true, "effect": Effect { talk_cost: 1, memory: invitation_token(command), cooldown_days: if command == 3 { 1 } else { 0 }, ..Effect::new() }}),
                    Err(why) => json!({"ok": false, "reason": why}),
                })
            }
            "topic_effect" => {
                let d = need()?;
                let n = |k: &str| r.get(k).and_then(Value::as_i64).map(|v| v as i32);
                let interests: Vec<i32> = r.get("interests").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_i64).map(|v| v as i32).collect();
                let (answer, asked) = (n("answer"), n("asked"));
                let mut e = collected_effect(&d.topics, answer, asked, &interests);
                e.topic_id = if asked.is_some() { -1 } else { answer.unwrap_or(-1) };
                e.memory = match asked {
                    Some(q) => format!("listen:{q}"),
                    None => format!("offer:{}", answer.unwrap_or(-1)),
                };
                let (point, desire) = topic_points(&d.topics, answer, asked, &interests);
                Ok(json!({"effect": e, "match": asked.map(|q| topic_match(&d.topics, answer, q)), "point": point, "sex_desire": desire}))
            }
            "topic_drop" => {
                // After a dialogue line: its topic, and whether it drops into his stock.
                let d = need()?;
                let line = d.by_id.get(r.get("node").and_then(Value::as_str).unwrap_or("")).map(|k| &d.nodes[*k]).ok_or("unknown node")?;
                let no = dialogue_topic(d, line);
                let stock = r.get("stock").and_then(|s| s.get(no.to_string())).and_then(Value::as_i64).unwrap_or(0) as i32;
                let count = r.get("count").and_then(Value::as_i64).unwrap_or(0) as i32;
                let roll = r.get("roll").and_then(Value::as_i64).unwrap_or(99) as i32;
                Ok(json!({"topic": no, "command": line.command, "dropped": no >= 0 && topic_drop(&d.topics, stock, no, line.command, count, roll)}))
            }
            "topic_collect" => {
                // A street pickup: once a topic a day, at most 99 held.
                let no = r.get("no").and_then(Value::as_i64).unwrap_or(-1);
                let (stock, claimed, day) = (r.get("stock").and_then(Value::as_i64).unwrap_or(0), r.get("claimed").and_then(Value::as_i64), r.get("day").and_then(Value::as_i64).unwrap_or(1));
                Ok(json!({"ok": no >= 0 && claimed != Some(day) && stock < 99}))
            }
            "reflect_adjust" => {
                // Her night's reflection nudges her axes, at most 3 each way.
                let before = relation(r)?;
                let mut rel = before.clone();
                let mut applied = Vec::new();
                for (k, v) in r.get("adjust").and_then(Value::as_object).into_iter().flatten() {
                    let Some(x) = v.as_f64().filter(|x| x.is_finite()) else { continue };
                    if let Some(axis) = rel.axis_mut(k) {
                        let by = (x as f32).clamp(-3.0, 3.0);
                        *axis = (*axis + by).clamp(0.0, 100.0);
                        applied.push(format!("{k}{by:+.0}"));
                    }
                }
                Ok(json!({"relation": rel.patch(&before), "applied": applied.join(" ")}))
            }
            _ => Err("__not_mine".into()),
        }
    })())
    .filter(|v| !matches!(v, Err(e) if e == "__not_mine"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(v: Value) -> Relation {
        let mut o = json!({"id": "a"});
        for (k, x) in v.as_object().unwrap() {
            o[k] = x.clone();
        }
        serde_json::from_value(o).unwrap()
    }

    fn fixture() -> TalkData {
        // Ported from OKStreetFixtureCatalogue: a stranger greeting, a friend daily line, a never-speaking farewell,
        // an end-conversation farewell and a preference branch.
        let line = |id: &str, cat: &str, stage: i32, priority: i32, cond: i32, group: &str, roles: Value, row: i32| {
            json!({"id": id, "personality": 0, "category": cat, "text_zh": id, "favor": 3, "conditions": {"id": cond, "street_support": "supported"},
                "source": {"stage": stage, "priority": priority, "roles": roles, "effects_defined": true, "group": group, "row": row, "containers": [7],
                    "voice_sequence": [{"index": 0, "status": "matched", "path": format!("{id}.ogg")}]}})
        };
        let root = json!({"nodes": [
            line("hello", "greeting", 0, 5, -1, "", json!(["generic"]), 1),
            line("chat", "daily", 1, 3, -1, "", json!(["generic"]), 2),
            line("never", "farewell", 0, 1, 0, "", json!(["generic"]), 3),
            line("bye", "farewell", 0, 2, -1, "EndConversation", json!(["generic"]), 4),
            line("intro", "daily", 0, 1, 7, "Talk", json!(["introduction"]), 10),
            line("yes", "daily", 0, 1, -1, "Talk", json!(["success"]), 11),
            line("no", "daily", 0, 1, -1, "Talk", json!(["failure"]), 12)],
            "branches": [{"id": "b1", "personality": 0, "stage": 0, "container": 7, "category": "daily", "group": "Talk", "controlling_row": 10, "controlling_condition": {"id": 7},
                "selection": {"kind": "preference_compare", "axis": 0, "operator": "<=", "threshold": 3, "index_when_true": 1, "index_when_false": 0},
                "indexer": [{"row": 11, "role": "success"}, {"row": 12, "role": "failure"}]}]});
        let mut d = TalkData::default();
        parse_catalogue(&root, &mut d).unwrap();
        d.rules = parse_rules(&json!({"stepup": {"limits": {"0": 75, "1": 100, "2": 150, "3": 150}}, "preference": {"0": [5, 0, 0]}})).unwrap();
        d
    }

    fn who() -> Who {
        Who { personality: "0".into(), personality_id: 0, talk_limit: false, budget: 5 }
    }

    #[test]
    fn conditions() {
        // Ported from OKStreetCheckConditions.
        let stranger = rel(json!({}));
        let friend = rel(json!({"stage": "friend", "talks_remaining": 5}));
        assert!(eval_conditions(&["speak=always".into(), "relation=stranger".into()], &stranger, "0", "daily").is_ok());
        assert!(eval_conditions(&["speak=always".into(), "relation=stranger".into()], &friend, "0", "daily").unwrap_err().contains("relation="));
        assert_eq!(eval_conditions(&["speak=never".into()], &stranger, "0", "farewell").unwrap_err(), "speak_never");
        assert_eq!(eval_conditions(&["street_support=unsupported:id_2:original_default_true_not_accepted".into()], &stranger, "0", "greeting").unwrap_err(), "street_support_unsupported:id_2:original_default_true_not_accepted");
        assert!(eval_conditions(&["anger<10".into()], &stranger, "0", "daily").unwrap_err().starts_with("unsupported_condition"));
        assert!(eval_conditions(&["favor>=10".into(), "talks>0".into()], &rel(json!({"favor": 12, "talks_remaining": 1})), "0", "daily").is_ok());
    }

    #[test]
    fn lines_and_topics() {
        let d = fixture();
        assert!(d.warnings.is_empty(), "{:?}", d.warnings);
        let (items, _) = list_lines(&d, &rel(json!({})), &who(), "greeting");
        assert_eq!((items.len(), items[0].1), (1, true));
        let v = topic(&d, &json!({"category": "greeting", "relation": {"id": "a"}, "personality": "0", "seed": 1})).unwrap();
        assert_eq!(v["node"], "hello");
        // Farewell: the end-conversation row (condition -1) once the talks are used up.
        let v = topic(&d, &json!({"category": "farewell", "relation": {"id": "a", "talks_remaining": 0}, "personality": "0", "budget": 5})).unwrap();
        assert_eq!(v["node"], "bye");
        // The preference branch: aggressive 5 > 3, so the false index (success).
        let v = topic(&d, &json!({"category": "daily", "relation": {"id": "a"}, "personality": "0", "seed": 3})).unwrap();
        assert_eq!((v["node"].as_str(), v["branch"].as_str(), v["effect"]["cooldown_days"].as_i64()), (Some("yes"), Some("b1"), Some(1)));
    }

    #[test]
    fn commits_and_caps() {
        // Ported from OKStreetCheckCommit.
        let d = fixture();
        let mut r = rel(json!({"favor": 70}));
        let e = Effect { favor: 10.0, memory: "hello".into(), cooldown_days: 1, ..Effect::new() };
        commit(&d, &mut r, "t1", &e, false).unwrap();
        assert_eq!(r.favor, 75.0, "the stranger cap");
        assert!(r.talked.contains(&"hello".to_string()) && r.cooldowns["hello"] == 2);
        assert_eq!(commit(&d, &mut r, "t1", &e, false).unwrap_err(), (true, "duplicate_transaction".to_string()));
        r.stranger_cap = 40;
        assert_eq!(d.favor_limit(&r, "stranger"), Some(40));
        assert_eq!(d.favor_limit(&r, "friend"), Some(200));
        let mut empty = TalkData::default();
        empty.rules.step_up = None;
        assert_eq!(commit(&empty, &mut rel(json!({})), "t", &e, false).unwrap_err().1, "stepup_limit_missing");
    }

    #[test]
    fn friends_and_confessions() {
        let d = fixture();
        let f = friendship(&d, &rel(json!({"favor": 75})), &who());
        assert!(f.resolved && f.effect.relation_event == "friend");
        assert!(!friendship(&d, &rel(json!({"favor": 74})), &who()).resolved);
        let c = confession(&d, &rel(json!({"stage": "friend", "favor": 60})), &who(), 0);
        assert_eq!(c.reason, "confession_favor_below_0_7");
        let c = confession(&d, &rel(json!({"stage": "friend", "favor": 80})), &who(), 0);
        assert!(c.resolved && !c.refusal && c.chance == 30 && c.effect.favor == 50.0);
        assert_eq!(confession(&d, &rel(json!({"stage": "friend", "favor": 101})), &who(), 0).chance, 51);
        assert_eq!(confession(&d, &rel(json!({"stage": "friend", "favor": 125})), &who(), 0).chance, 75);
        assert_eq!(confession(&d, &rel(json!({"stage": "friend", "favor": 150})), &who(), 0).chance, 100);
        for roll in 0..100 {
            let c = confession(&d, &rel(json!({"favor": 200})), &who(), roll);
            assert!(c.resolved && !c.refusal && c.chance == 100, "200 favor is certain");
        }
        assert_eq!(confession(&d, &rel(json!({"stage": "lover"})), &who(), 0).reason, "already_lover");
    }

    #[test]
    fn shy_confessions() {
        let d = fixture();
        let yes = || confession(&d, &rel(json!({"stage": "friend", "favor": 150})), &who(), 0);
        // Not very shy: as usual.
        let mut c = yes();
        super::shy_confession(&mut c, 50, 2, "cafeteria", 12.0, 0);
        assert!(!c.refusal && c.shy.is_empty());
        // Another girl near, or a full cafeteria at lunch: refused, nothing lost.
        let mut c = yes();
        super::shy_confession(&mut c, 85, 1, "park", 15.0, 0);
        assert!(c.refusal && c.shy == "onlookers" && c.effect.favor == 0.0 && c.effect.relation_event.is_empty());
        let mut c = yes();
        super::shy_confession(&mut c, 85, 0, "cafeteria", 12.0, 0);
        assert_eq!(c.shy, "onlookers");
        // A refusal anyway keeps its cost.
        let mut no = confession(&d, &rel(json!({"stage": "friend", "favor": 101})), &who(), 99);
        assert!(no.refusal);
        super::shy_confession(&mut no, 85, 1, "park", 15.0, 0);
        assert!(no.shy == "onlookers" && no.effect.favor == super::CONFESS_REFUSE_FAVOR);
        // Alone on a quiet street at night: yes, often by running off (75 % at 85).
        let mut c = yes();
        super::shy_confession(&mut c, 85, 0, "", 22.5, 74);
        assert!(!c.refusal && c.shy == "runs_away" && c.effect.relation_event == "lover");
        let mut c = yes();
        super::shy_confession(&mut c, 85, 0, "", 22.5, 75);
        assert!(!c.refusal && c.shy.is_empty());
    }

    #[test]
    fn invitations_and_topics() {
        let r = rel(json!({"talks_remaining": 1}));
        assert!(can_invite(&r, 3, true).is_ok());
        assert_eq!(can_invite(&r, 4, true).unwrap_err(), "talk_budget");
        let mut cooled = r.clone();
        cooled.cooldowns.insert(invitation_token(3), 2);
        assert_eq!(can_invite(&cooled, 3, false).unwrap_err(), "invitation_already_attempted");
        let t = parse_topics(&json!({"topics": [{"No": 0, "Category": 0, "Rarity": 0, "Name": "a"}, {"No": 1, "Category": 0, "Rarity": 1, "Name": "b"}, {"No": 2, "Category": 4, "Rarity": 0, "Name": "c"}],
            "drops": [{"no": 0, "probability": 70, "states": []}],
            "talk_points": [{"favor": 10, "point": 1}, {"favor": 20, "point": 1}, {"favor": 30, "point": 1}, {"favor": 40, "point": 1}, {"favor": 50, "point": 1}],
            "listen_points": [{"common": 20, "rare": 30, "point": 1}, {"common": 5, "rare": 10, "point": 1}, {"common": 0, "rare": 0, "point": 1}],
            "interest": [{"favor": 1, "lewdness": 0, "point": 1}, {"favor": 1, "lewdness": 0, "point": 1}, {"favor": 1, "lewdness": 0, "point": 1}, {"favor": 1, "lewdness": 0, "point": 1}, {"favor": 0, "lewdness": 5, "point": 1}]})).unwrap();
        assert_eq!(collected_effect(&t, Some(0), Some(0), &[]).favor, 20.0);
        assert_eq!(collected_effect(&t, Some(1), Some(0), &[]).favor, 0.0, "a rare same-category answer uses rare[2]");
        assert_eq!(collected_effect(&t, None, Some(0), &[]).favor, 0.0);
        let e = collected_effect(&t, Some(2), None, &[4]);
        assert_eq!((e.favor, e.lewdness), (0.0, 15.0));
        assert!(topic_drop(&t, 0, 0, 0, 1, 69) && !topic_drop(&t, 0, 0, 0, 1, 70) && !topic_drop(&t, 99, 0, 2, 99, 0));
    }
}
