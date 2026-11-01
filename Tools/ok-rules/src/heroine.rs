//! Every interaction with a heroine, his or hers, through the same steps: may it happen (gate) → does she agree
//! (consent) → how she takes it here (reaction) → values → her record and memory → rumour → what it looks like. The table
//! is acts.toml; Unreal runs the steps that need the scene (asking her, writing memories, animation) from the answer.
use crate::places::{self, PlacesTuning, Reaction};
use crate::relation::{stage_index, Relation};
use crate::traits::Traits;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

#[derive(Deserialize, Serialize, Debug, Clone, Default)]
#[serde(deny_unknown_fields, default)]
pub struct Act {
    pub id: String,
    pub cat: String,
    pub name: String,
    pub stage: i32,
    pub affection: f32,
    pub attraction: f32,
    pub trust: f32,
    pub wanted: String,
    pub face_to_face: bool,
    pub awkward: bool,
    pub consent: String,
    pub history: String,
    pub text: String,
    pub memory: f32,
    pub rumor: String,
    pub rumor_text: String,
    pub rumor_heat: f32,
    pub button: String,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct ActsTuning {
    pub act: Vec<Act>,
}

impl ActsTuning {
    pub fn find(&self, id: &str) -> Option<&Act> {
        self.act.iter().find(|a| a.id == id)
    }
}

/// One heroine as a request describes her: her relation record and her profile (traits read from it).
pub struct Ctx {
    pub rel: Relation,
    pub profile: Value,
    pub traits: Traits,
}

impl Ctx {
    pub fn from_request(request: &Value) -> Result<Ctx, String> {
        Self::from_parts(request.get("relation"), request.get("profile"))
    }

    pub fn from_parts(relation: Option<&Value>, profile: Option<&Value>) -> Result<Ctx, String> {
        let rel: Relation = serde_json::from_value(relation.cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))?;
        let profile = profile.cloned().filter(|p| p.is_object()).unwrap_or_else(|| json!({}));
        let traits = Traits::from_profile(&profile);
        Ok(Ctx { rel, profile, traits })
    }

    /// His appeal to her (plan §12.1), from his look.
    pub fn appeal(&self, look: f32) -> f32 {
        crate::invite::appeal(look, self.rel.perceived_wealth, &self.traits, &self.profile, self.rel.admiration)
    }
}

/// May it happen between them now (the relationship side)?
pub fn gate(r: &Relation, a: &Act) -> bool {
    r.stage_index() >= a.stage && r.liking() >= a.affection && r.attraction >= a.attraction && r.trust >= a.trust
}

/// What his attempt below the gate costs (a stranger more than a friend).
pub fn gate_penalty(r: &mut Relation) {
    let hit = if stage_index(&r.stage) == 0 { 6.0 } else { 3.0 };
    r.add_axis("respect", -hit);
    r.add_axis("resentment", hit);
    r.add_axis("trust", -hit * 0.5);
}

/// How she took an act in this place.
pub fn reaction_delta(r: &mut Relation, reaction: Reaction, intimate: bool) {
    match reaction {
        Reaction::Fine if intimate => r.add_axis("affection", 1.0),
        Reaction::Thrilled => r.add_axis("attraction", 2.0),
        Reaction::Protest => r.add_axis("respect", -1.0),
        Reaction::Refuse => {
            r.add_axis("respect", -3.0);
            r.add_axis("trust", -1.0);
        }
        _ => {}
    }
}

/// Chastity she starts with when nothing set it: shy and hard to charm is chaste, bold is less so.
pub fn default_chastity(t: &Traits) -> f32 {
    (30.0 + 0.3 * t.shyness as f32 + 0.3 * t.charm_resistance as f32 - 0.2 * t.public_boldness as f32).clamp(5.0, 95.0)
}

pub fn chastity(r: &Relation, t: &Traits) -> f32 {
    if r.chastity >= 0.0 {
        r.chastity
    } else {
        default_chastity(t)
    }
}

/// How hard she is to win over: her stranger favor cap, 10..100 (her profile's difficulty when it has one).
pub fn stranger_cap(t: &Traits) -> i32 {
    if t.difficulty > 0 {
        return t.difficulty.clamp(10, 100);
    }
    ((10.0 + 0.5 * t.charm_resistance as f32 + 0.4 * default_chastity(t) + 0.5).floor() as i32).clamp(10, 100)
}

/// Being alone with him when they are not lovers: his charm against her resistance (mostly chastity); a friend past
/// 100 favor very likely says yes. Lovers go by the invitation rule (chance < 0).
pub fn private_chance(r: &Relation, t: &Traits, charm: f32) -> (f32, f32, String) {
    if r.stage == "lover" {
        return (-1.0, 0.0, String::new());
    }
    let friend = r.stage == "friend";
    let resistance = (0.6 * chastity(r, t) + 0.4 * t.charm_resistance as f32 - if friend { 10.0 } else { 0.0 }).clamp(0.0, 100.0);
    let mut chance = 0.0f32;
    if charm > resistance {
        // Rising with how far his charm is past her resistance, not a jump to one in two the moment it is (a stranger
        // whose resistance he passed by a hair went to a locked room with him at favor 8).
        chance = ((charm - resistance) / 35.0).clamp(0.0, 0.95);
    }
    if friend && r.favor > 100.0 {
        chance = chance.max((0.7 + (r.favor - 100.0) / 100.0 * 0.25).clamp(0.7, 0.95));
    }
    let why = if chance <= 0.0 { crate::tr::m("heroine.not_easy_charm_resistance", &[("charm", crate::tr::a(&charm)), ("resistance", crate::tr::a(&resistance))]) } else { String::new() };
    (chance, resistance, why)
}

/// The words of her reaction, for her history.
pub fn how(reaction: i32) -> &'static str {
    [crate::tr::t("heroine.you_go_along_naturally"), crate::tr::t("heroine.you_shy_but_went_along"), crate::tr::t("heroine.you_whisper_protest"), crate::tr::t("heroine.you_refused"), crate::tr::t("heroine.you_find_it_thrilling")][reaction.clamp(0, 4) as usize]
}

fn format(text: &str, detail: &str) -> String {
    if text.is_empty() {
        detail.into()
    } else {
        text.replace("%s", detail)
    }
}

/// Her record (history, and a memory when the table says so) and the rumour, as Unreal writes them.
fn record_and_rumor(a: &Act, detail: &str, rumor_detail: &str, importance: f32, hers: bool, name: &str, out: &mut serde_json::Map<String, Value>) {
    let text = format(&a.text, detail);
    if !a.history.is_empty() && !text.is_empty() {
        out.insert("record".into(), json!({"history": a.history, "text": text, "memory": if importance >= 0.0 { importance } else { a.memory }, "hers": hers}));
    }
    if !a.rumor.is_empty() && a.rumor != "none" && !a.rumor_text.is_empty() {
        let text = a.rumor_text.replace("%n", name).replace("%s", rumor_detail);
        out.insert("rumor".into(), json!({"kind": if a.history.is_empty() { &a.id } else { &a.history }, "text": text, "heat": a.rumor_heat, "secret": a.rumor == "secret"}));
    }
}

/// The interaction itself. Request: act, hers, record_only, remote, consented, penalty_ready, detail, rumor_detail,
/// importance, place, place_name, hours, name, relation, profile.
fn interact(acts: &ActsTuning, places: &PlacesTuning, request: &Value) -> Result<Value, String> {
    let s = |n: &str| request.get(n).and_then(Value::as_str).unwrap_or("").to_string();
    let b = |n: &str| request.get(n).and_then(Value::as_bool).unwrap_or(false);
    let act_id = s("act");
    let Some(a) = acts.find(&act_id) else { return Ok(json!({"known": false})) };
    let mut out = serde_json::Map::new();
    out.insert("known".into(), json!(true));
    out.insert("cat".into(), json!(a.cat));
    let importance = request.get("importance").and_then(Value::as_f64).unwrap_or(-1.0) as f32;
    let (hers, name, detail) = (b("hers"), s("name"), s("detail"));
    let rumor_detail = Some(s("rumor_detail")).filter(|d| !d.is_empty()).unwrap_or_else(|| detail.clone());
    // Settled elsewhere: only the record and the rumour.
    if b("record_only") {
        record_and_rumor(a, &detail, &rumor_detail, importance, hers, &name, &mut out);
        out.insert("done".into(), json!(true));
        return Ok(Value::Object(out));
    }
    // 1. May it happen: over the phone a touch stays words; below the relationship it does not.
    if a.face_to_face && b("remote") {
        out.insert("done".into(), json!(true));
        out.insert("why".into(), json!("remote"));
        return Ok(Value::Object(out));
    }
    let c = Ctx::from_request(request)?;
    if !gate(&c.rel, a) {
        out.insert("refused".into(), json!(true));
        out.insert("why".into(), json!("gate"));
        if !hers {
            // His idea below the gate: she remembers it, and it costs respect and trust (once per 20 s).
            let place_name = Some(s("place_name")).filter(|p| !p.is_empty()).unwrap_or_else(|| crate::tr::t("heroine.on_the_street").into());
            let what = crate::tr::m("heroine.wants_but_not_there", &[("place", crate::tr::a(&place_name)), ("wanted", crate::tr::a(&(if a.wanted.is_empty() { &a.name } else { &a.wanted })))]);
            out.insert("gate".into(), json!({"what": what, "importance": if c.rel.stage_index() == 0 { 0.8 } else { 0.6 }, "stage": c.rel.stage_index()}));
            if b("penalty_ready") {
                let mut r = c.rel.clone();
                gate_penalty(&mut r);
                out.insert("relation".into(), Value::Object(r.patch(&c.rel)));
            }
        }
        return Ok(Value::Object(out));
    }
    // 2. Does she agree: his invitations go through her invitation rule (Unreal asks, then calls again).
    if !a.consent.is_empty() && !hers && !b("consented") {
        out.insert("consent".into(), json!(a.consent));
        return Ok(Value::Object(out));
    }
    // 3. How she takes it here: the place's norms and the crowd against her shyness; staff may speak up.
    let mut reaction = Reaction::Fine;
    let mut awkward = 0.0f32;
    let mut recent = String::new();
    if a.awkward {
        let place = s("place");
        let norms = places.norms(&place);
        let needs = places.act(&a.id);
        let crowd = places::crowd_at(&place, request.get("hours").and_then(Value::as_f64).unwrap_or(12.0));
        awkward = places::awkwardness(norms, crowd, needs);
        reaction = places::react(awkward, c.traits.shyness, c.traits.public_boldness, &c.rel.stage);
        // Her own idea is not refused by her: 安可 held out the pudding to feed him at the restaurant and was told "she
        // refused and pulls you away". Shy as she may be, she does what she offered.
        if hers && reaction == Reaction::Refuse {
            reaction = Reaction::Shy;
        }
        let words = [crate::tr::t("heroine.she_goes_along_naturally"), crate::tr::t("heroine.she_shy_but_went_along"), crate::tr::t("heroine.she_whispers_protest_ends"), crate::tr::t("heroine.she_refused_pulls_away"), crate::tr::t("heroine.she_finds_it_thrilling")];
        recent = crate::tr::m("heroine.together_reaction_line", &[("norm_label", crate::tr::a(&norms.label)), ("need_name", crate::tr::a(&needs.name)), ("reaction_text", crate::tr::a(&(words[reaction as usize])))]);
        // Staff speak up when the act breaks the place's rules (food where nobody eats, noise where it is quiet, more
        // intimacy than the place allows), not when it is only embarrassing in a crowd (a bite fed at a busy lunch).
        let breaks_rules = (needs.food && !norms.food) || (needs.loud && norms.quiet >= 0.5) || norms.privacy < needs.min_privacy;
        let staff = norms.staff && awkward >= 0.6 && breaks_rules;
        if staff {
            recent += if norms.quiet >= 0.7 { crate::tr::t("heroine.manager_warned_quiet") } else { crate::tr::t("heroine.clerk_warned_you") };
        }
        if crowd >= 0.35 && awkward >= 0.3 {
            recent += crate::tr::t("heroine.bystanders_watching");
        }
        // 4. Values: how she took it.
        let mut r = c.rel.clone();
        reaction_delta(&mut r, reaction, needs.intimate);
        out.insert("relation".into(), Value::Object(r.patch(&c.rel)));
        out.insert("recent".into(), json!(recent));
        out.insert("staff".into(), json!(staff));
        out.insert("awkward".into(), json!(awkward));
        out.insert("reaction".into(), json!(reaction as i32));
    }
    // 5. Her record: what happened and how she took it; an awkward scene is a memory of hers. 6. What others hear.
    let detail = if a.name.is_empty() { detail } else { crate::tr::m("heroine.named_reaction_label", &[("name", crate::tr::a(&a.name)), ("reaction", crate::tr::a(&(how(reaction as i32))))]) };
    record_and_rumor(a, &detail, &detail, importance, hers, &name, &mut out);
    if awkward >= 0.3 {
        out.insert("awkward_memory".into(), json!({"text": recent.chars().take(80).collect::<String>(), "importance": (0.3 + awkward * 0.2).clamp(0.3, 0.8)}));
    }
    // 7. What it looks like.
    out.insert("play_act".into(), json!(a.cat == "act"));
    out.insert("done".into(), json!(reaction != Reaction::Refuse));
    Ok(Value::Object(out))
}

/// Places this quiet (places.toml quiet) keep loud acts out entirely (heroine_place_ease).
const QUIET_PLACE: f32 = 0.7;

pub fn eval(acts: &ActsTuning, places: &PlacesTuning, kind: &str, request: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "heroine_acts" => Ok(json!({"acts": acts.act.iter().map(|a| json!({"id": a.id, "cat": a.cat, "name": a.name, "stage": a.stage, "face_to_face": a.face_to_face, "button": a.button})).collect::<Vec<_>>()})),
        "heroine_act" => interact(acts, places, request),
        // request: relation, profile, place, hours, act (a places.toml act) -> {awkward, reaction}: how she would take
        // doing it here now, before she offers (her song: only where she would be at ease, reaction 0 or 4).
        "heroine_place_ease" => Ctx::from_request(request).map(|c| {
            let place = request.get("place").and_then(Value::as_str).unwrap_or("");
            let act = request.get("act").and_then(Value::as_str).unwrap_or("");
            let crowd = places::crowd_at(place, request.get("hours").and_then(Value::as_f64).unwrap_or(12.0));
            let awkward = places::awkwardness(places.norms(place), crowd, places.act(act));
            let reaction = places::react(awkward, c.traits.shyness, c.traits.public_boldness, &c.rel.stage);
            // A loud act where people keep quiet (library, lecture hall, bookstore, shrine: quiet 0.7 and up) is not
            // done there, however empty it is or however bold she is: 三月七 sang to him in the bookstore at three in the
            // afternoon (R6-13). Her song waits for another place.
            let hushed = places.act(act).loud && places.norms(place).quiet >= QUIET_PLACE;
            json!({"awkward": awkward, "reaction": reaction as i32, "at_ease": !hushed && matches!(reaction, Reaction::Fine | Reaction::Thrilled)})
        }),
        "heroine_chastity" => Ctx::from_request(request).map(|c| json!({"chastity": chastity(&c.rel, &c.traits)})),
        "heroine_stranger_cap" => Ctx::from_request(request).map(|c| json!({"stranger_cap": stranger_cap(&c.traits)})),
        "heroine_private" => Ctx::from_request(request).map(|c| {
            let charm = request.get("charm").and_then(Value::as_f64).unwrap_or(0.0) as f32;
            let (chance, resistance, why) = private_chance(&c.rel, &c.traits, charm);
            let roll = request.get("roll").and_then(Value::as_f64).map(|r| r as f32);
            json!({"chance": chance, "resistance": resistance, "why": why, "chastity": chastity(&c.rel, &c.traits), "charm_resistance": c.traits.charm_resistance, "yes": roll.map(|r| chance > 0.0 && r < chance)})
        }),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    fn req(extra: Value) -> Value {
        let mut base = json!({"relation": {"id": "a"}, "profile": {}, "name": "小美", "place": "", "hours": 12.0});
        base.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
        base
    }

    /// Her song waits out every quiet place, empty or not, lover or not; an evening park is fine (R6-13).
    #[test]
    fn no_song_where_people_keep_quiet() {
        let t = Tuning::builtin();
        let ease = |place: &str, hours: f64| {
            let r = req(json!({"relation": {"id": "a", "stage": "lover", "affection": 100}, "profile": {"traits": {"public_boldness": 90, "shyness": 10}}, "place": place, "hours": hours, "act": "sing"}));
            eval(&t.acts, &t.places, "heroine_place_ease", &r).unwrap().unwrap()["at_ease"].as_bool().unwrap()
        };
        for place in ["library", "lecture", "bookstore", "shrine"] {
            assert!(!ease(place, 15.0), "{place}");
        }
        assert!(ease("park", 20.0));
    }

    #[test]
    fn gates_and_penalties() {
        let t = Tuning::builtin();
        let stranger = Relation::default();
        assert!(!gate(&stranger, t.acts.find("hold_hands").unwrap()));
        let friend = Relation { stage: "friend".into(), ..Default::default() }.with_liking(40.0);
        assert!(gate(&friend, t.acts.find("hold_hands").unwrap()));
        assert!(!gate(&friend, t.acts.find("hug").unwrap()), "hug needs 55 affection and 40 attraction");
        let mut r = stranger.clone();
        gate_penalty(&mut r);
        assert_eq!((r.respect, r.resentment, r.trust), (14.0, 6.0, 17.0));
    }

    #[test]
    fn interact_gate_refusal_and_reaction() {
        let t = Tuning::builtin();
        let v = interact(&t.acts, &t.places, &req(json!({"act": "kiss", "penalty_ready": true, "place_name": "圖書館"}))).unwrap();
        assert_eq!((v["refused"].as_bool(), v["why"].as_str()), (Some(true), Some("gate")));
        assert_eq!(v["gate"]["what"], "他在圖書館想親你，但你們的關係還沒到那一步");
        assert_eq!(v["relation"]["respect"], 14.0);
        let hers = interact(&t.acts, &t.places, &req(json!({"act": "kiss", "hers": true}))).unwrap();
        assert!(hers.get("gate").is_none() && hers.get("relation").is_none(), "her idea below the gate is simply not done");
        let lovers = req(json!({"act": "kiss", "relation": {"id": "a", "stage": "lover"}, "place": "library", "hours": 10.0}));
        let v = interact(&t.acts, &t.places, &lovers).unwrap();
        assert_eq!(v["reaction"], 2, "lovers kissing in a busy library: she protests");
        assert_eq!(v["record"]["text"], "他和你接吻（你小聲抗議）");
        assert!(v["recent"].as_str().unwrap().contains("管理員過來提醒"));
        assert_eq!(v["play_act"], true);
        let mine = interact(&t.acts, &t.places, &req(json!({"act": "kiss", "hers": true, "relation": {"id": "a", "stage": "lover"}, "place": "library", "hours": 10.0, "traits": {"shyness": 1.0}}))).unwrap();
        assert_ne!(mine["reaction"], 3, "her own idea is not refused by her");
        assert!(v["awkward_memory"]["importance"].as_f64().unwrap() >= 0.3);
    }

    #[test]
    fn record_only_writes_record_and_rumor() {
        let t = Tuning::builtin();
        let v = interact(&t.acts, &t.places, &json!({"act": "gift", "record_only": true, "detail": "送了我花", "rumor_detail": "一束花", "name": "小美"})).unwrap();
        assert_eq!(v["record"]["text"], "送了我花");
        assert_eq!(v["record"]["memory"], -1.0);
        assert_eq!(v["rumor"]["text"], "他送了小美一束花");
        assert_eq!(v["rumor"]["secret"], false);
        assert_eq!(interact(&t.acts, &t.places, &json!({"act": "nope"})).unwrap()["known"], false);
    }

    #[test]
    fn chastity_cap_and_private() {
        let t = Traits { shyness: 80, charm_resistance: 70, public_boldness: 20, ..Default::default() };
        assert!((default_chastity(&t) - 71.0).abs() < 1e-4);
        assert_eq!(stranger_cap(&t), 73);
        assert_eq!(stranger_cap(&Traits { difficulty: 5, ..Default::default() }), 10);
        let r = Relation::default();
        let (chance, resistance, why) = private_chance(&r, &t, 50.0);
        assert!(chance == 0.0 && (resistance - 70.6).abs() < 1e-3 && !why.is_empty());
        let friend = Relation { stage: "friend".into(), favor: 200.0, ..Default::default() };
        assert!((private_chance(&friend, &t, 0.0).0 - 0.95).abs() < 1e-6);
        assert_eq!(private_chance(&Relation { stage: "lover".into(), ..Default::default() }, &t, 0.0).0, -1.0);
    }

    // Ported from Tests/OKStreetHeroine.spec.cpp (Gates, Values, ChastityAndConfession's heroine half).
    #[test]
    fn spec_gates_match_the_old_lover_gate_rows() {
        let t = Tuning::builtin();
        let rows = [("hold_hands", 1, 40.0, 0.0), ("head_pat", 1, 40.0, 0.0), ("feed", 1, 0.0, 0.0), ("share_food", 1, 0.0, 0.0), ("hug", 1, 55.0, 40.0), ("kiss", 2, 0.0, 0.0), ("lap_pillow", 2, 0.0, 0.0), ("lie_down", 2, 0.0, 0.0)];
        for (id, stage, aff, att) in rows {
            let a = t.acts.find(id).unwrap();
            for (si, s) in ["stranger", "friend", "lover"].iter().enumerate() {
                for af in [0.0, 39.0, 40.0, 55.0, 80.0] {
                    for at in [0.0, 39.0, 40.0] {
                        let r = Relation { stage: s.to_string(), attraction: at, trust: 0.0, ..Default::default() }.with_liking(af);
                        assert_eq!(gate(&r, a), si as i32 >= stage && r.liking() >= aff && at >= att, "{id} {s} {af} {at}");
                    }
                }
            }
        }
        let zero = Relation { attraction: 0.0, trust: 0.0, ..Default::default() }.with_liking(0.0);
        for free in ["eat", "alcohol", "loud", "phone_call"] {
            assert!(gate(&zero, t.acts.find(free).unwrap()), "{free}");
        }
        for id in ["eat", "feed", "share_food", "loud", "phone_call", "hold_hands", "head_pat", "hug", "kiss", "lap_pillow", "lie_down", "alcohol"] {
            let a = t.acts.find(id).unwrap();
            assert!(a.cat == "act" && a.face_to_face && a.awkward && !a.name.is_empty() && a.history == "act", "{id}");
        }
        assert_eq!(crate::invite::kind_of("intimacy"), crate::invite::PRIVATE);
        let mut ids = std::collections::HashSet::new();
        for a in &t.acts.act {
            assert!(ids.insert(a.id.clone()), "duplicate {}", a.id);
            assert!(a.rumor.is_empty() || !a.rumor_text.is_empty(), "{} rumour text", a.id);
        }
    }

    #[test]
    fn spec_reaction_values() {
        let mut r = Relation { stage: "friend".into(), attraction: 50.0, respect: 50.0, trust: 50.0, ..Default::default() }.with_liking(50.0);
        let near = |r: &Relation, v: f32| (r.liking() - v).abs() < 1e-3;
        reaction_delta(&mut r, Reaction::Fine, true);
        assert!(near(&r, 51.0));
        reaction_delta(&mut r, Reaction::Fine, false);
        assert!(near(&r, 51.0));
        reaction_delta(&mut r, Reaction::Shy, true);
        assert!(near(&r, 51.0) && r.respect == 50.0);
        reaction_delta(&mut r, Reaction::Thrilled, false);
        assert_eq!(r.attraction, 52.0);
        reaction_delta(&mut r, Reaction::Protest, false);
        assert_eq!(r.respect, 49.0);
        reaction_delta(&mut r, Reaction::Refuse, false);
        assert!(r.respect == 46.0 && r.trust == 49.0);
        let mut f = Relation { stage: "friend".into(), respect: 20.0, resentment: 0.0, trust: 20.0, ..Default::default() };
        gate_penalty(&mut f);
        assert!(f.respect == 17.0 && f.resentment == 3.0 && f.trust == 18.5);
        let mut z = Relation { respect: 1.0, trust: 0.0, ..Default::default() };
        reaction_delta(&mut z, Reaction::Refuse, false);
        assert!(z.respect == 0.0 && z.trust == 0.0);
    }

    #[test]
    fn spec_chastity_private_and_caps() {
        let t = Traits { charm_resistance: 50, shyness: 50, public_boldness: 50, ..Default::default() };
        let loose = Relation { chastity: 10.0, ..Default::default() };
        let (chance, resistance, _) = private_chance(&loose, &t, 60.0);
        assert!((resistance - 26.0).abs() < 1e-4 && chance >= 0.9);
        let chaste = Relation { chastity: 90.0, ..loose.clone() };
        let (chance, _, why) = private_chance(&chaste, &t, 60.0);
        assert!(chance == 0.0 && !why.is_empty());
        let (barely, _, _) = private_chance(&Relation { chastity: 40.0, ..Default::default() }, &t, 45.0);
        assert!(barely < 0.1, "charm a hair past her resistance is not one in two: {barely}");
        let friend = Relation { stage: "friend".into(), favor: 150.0, ..chaste.clone() };
        assert!(private_chance(&friend, &t, 0.0).0 >= 0.8);
        assert_eq!(private_chance(&Relation { favor: 90.0, ..friend }, &t, 0.0).0, 0.0);
        assert!((chastity(&Relation::default(), &t) - 50.0).abs() < 1e-4);
        assert_eq!(stranger_cap(&Traits { difficulty: 5, ..t.clone() }), 10);
        assert_eq!(stranger_cap(&Traits { difficulty: 80, ..t.clone() }), 80);
        assert_eq!(stranger_cap(&t), 55);
        assert_eq!(stranger_cap(&Traits { charm_resistance: 0, shyness: 0, public_boldness: 100, ..Default::default() }), 14);
    }

    /// Every id a record is written under (the rules' note_at, the scene's HeroineNote / Interact) is in acts.toml:
    /// Interact drops a record whose id it does not know ("unknown interaction explosion" lost 惠惠's failed spell).
    #[test]
    fn recorded_ids_are_acts() {
        let t = Tuning::builtin();
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        // The quoted id right after the call's first argument (a plain string in Rust, a TEXT literal in the scene).
        fn ids(text: &str, marker: &str, quote: &str, out: &mut Vec<String>) {
            for (at, _) in text.match_indices(marker) {
                let rest = &text[at + marker.len()..];
                let Some(comma) = rest.find(',') else { continue };
                if rest[..comma].contains(['(', ')', '"']) { continue; }
                let Some(id) = rest[comma + 1..].trim_start().strip_prefix(quote) else { continue };
                if let Some(end) = id.find('"') { out.push(id[..end].to_string()); }
            }
        }
        let mut found = Vec::new();
        for (dir, markers) in [("src", &[("note_at(", "\"")][..]), ("../../Source/OpenKoikatsu", &[("HeroineNote(", "TEXT(\""), ("Interact(", "TEXT(\"")][..])] {
            for entry in std::fs::read_dir(root.join(dir)).unwrap() {
                let path = entry.unwrap().path();
                if !matches!(path.extension().and_then(|e| e.to_str()), Some("rs" | "inl" | "cpp")) { continue; }
                let text = std::fs::read_to_string(&path).unwrap();
                for (marker, quote) in markers { ids(&text, marker, quote, &mut found); }
            }
        }
        assert!(found.len() > 30, "the scan found the call sites ({})", found.len());
        let missing: Vec<_> = found.iter().filter(|id| t.acts.find(id).is_none()).collect();
        assert!(missing.is_empty(), "recorded under ids acts.toml does not have: {missing:?}");
    }
}
