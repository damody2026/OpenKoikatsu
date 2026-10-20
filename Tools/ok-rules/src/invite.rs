//! Plan §12: his appeal against her resistance, whether she accepts an invitation (strangers can say no), the reason she
//! gives, asking again after a no, and §13.1 whether she comes over to him by herself.
use crate::relation::Relation;
use crate::rng::RandomStream;
use crate::traits::Traits;
use serde_json::{json, Value};

/// Kinds of invitation from light to sleeping over (difficulty grows down the list).
pub const KINDS: usize = 6;
/// Their names: message ids (rules_text.toml [invite_kind]), shown and told to the model in the player's language.
pub const NAMES: [&str; KINDS] = ["invite_kind.casual", "invite_kind.meal", "invite_kind.stroll_elsewhere", "invite_kind.outing", "invite_kind.private_room", "invite_kind.overnight"];
pub const DIFFICULTY: [f32; KINDS] = [5.0, 15.0, 12.0, 25.0, 55.0, 70.0];
pub const LIGHT: usize = 0;
pub const MEAL: usize = 1;
pub const VISIT: usize = 2;
pub const LEISURE: usize = 3;
pub const PRIVATE: usize = 4;
pub const SLEEP: usize = 5;

/// What kind of invitation an activity id is.
pub fn kind_of(action: &str) -> usize {
    if action.starts_with("sleep") {
        return SLEEP;
    }
    if matches!(action, "hotel_rest" | "inn_rest" | "rest" | "intimacy" | "restaurant_room" | "hotel_room6" | "lodge_lounge2") {
        return PRIVATE;
    }
    if matches!(action, "meal" | "cafeteria_meal" | "dept_food" | "dept_view" | "dessert" | "fruit") {
        return MEAL;
    }
    if action.starts_with("aquarium")
        || action == "onsen"
        || action == "pool"
        || action.contains("spa")
        || action.starts_with("dept_")
        || action == "pet_photo"
        || action == "hotel_bar"
        || action.starts_with("splash")
        || action.starts_with("watermelon")
    {
        return LEISURE;
    }
    if action.starts_with("go:") {
        return VISIT;
    }
    LIGHT
}

/// Appeal (plan §12.1): look and her idea of his wealth against her resistances, weighted by what she values.
pub fn appeal(look: f32, perceived_wealth: f32, t: &Traits, profile: &Value, admiration: f32) -> f32 {
    let material = profile.get("material").and_then(Value::as_str).unwrap_or("Mixed");
    let archetype = profile.get("archetype").and_then(Value::as_str).unwrap_or("");
    let mut w_look = match material {
        "Mercenary" => 0.3,
        "Romantic" => 0.8,
        _ => 0.5,
    };
    let mut w_money = match material {
        "Mercenary" => 0.7,
        "Romantic" => 0.2,
        _ => 0.5,
    };
    if archetype == "G" {
        w_money += 0.2;
    }
    if archetype == "S" && admiration > 60.0 {
        w_look *= 1.2;
        w_money *= 1.2;
    }
    let mut a = w_look * (look - t.charm_resistance as f32) + w_money * (perceived_wealth - t.money_resistance as f32);
    if admiration > 60.0 {
        a += 10.0;
    }
    a.clamp(-100.0, 100.0)
}

/// accept_score - difficulty (plan §12.2).
pub fn score(kind: usize, r: &Relation, appeal: f32, awkward: f32) -> f32 {
    let base = match r.stage.as_str() {
        "lover" => 70.0,
        "friend" => 45.0,
        _ => 20.0,
    };
    let mut s = base + r.liking() * 0.3 + r.trust * 0.15 + appeal * 0.5 - r.resentment * 0.4;
    if kind == MEAL && r.satiety < 45.0 {
        s += 15.0;
    }
    if kind == LEISURE && r.fun < 40.0 {
        s += 10.0;
    }
    if (kind == LEISURE || kind == VISIT) && r.energy < 30.0 {
        s -= 15.0;
    }
    if kind == SLEEP && r.energy < 25.0 {
        s += 5.0;
    }
    s -= awkward * 15.0;
    s - DIFFICULTY[kind] - 30.0
}

/// P(accept) = sigmoid(score / 10).
pub fn chance(score: f32) -> f32 {
    1.0 / (1.0 + (-score / 10.0).exp())
}

/// The reason she gives when she refuses (rules choose; the model says it).
pub fn refusal_reason(kind: usize, r: &Relation) -> &'static str {
    if r.resentment > 50.0 {
        return crate::tr::t("invite.still_angry_at_him");
    }
    if r.energy < 25.0 && kind != SLEEP {
        return crate::tr::t("invite.too_tired");
    }
    if r.stage == "stranger" {
        return if kind >= PRIVATE { crate::tr::t("invite.just_met_too_soon") } else { crate::tr::t("invite.not_close_enough") };
    }
    if kind >= PRIVATE {
        return crate::tr::t("invite.not_ready_yet");
    }
    crate::tr::t("invite.not_in_mood_now")
}

/// Approach score (plan §13.1) for the 2-10 minute timer; above 60 she comes to him.
pub fn approach_score(r: &Relation, appeal: f32, distance_m: f32, recently_rejected: bool) -> f32 {
    let base = match r.stage.as_str() {
        "lover" => 45.0,
        "friend" => 25.0,
        _ => 5.0,
    };
    let (peak, _) = crate::bias::peak_jealousy(r);
    let mut s = base + r.liking() * 0.25 + appeal * 0.5 + (100.0 - r.social) * 0.25 + peak * 0.3 + r.dependency * 0.2;
    s -= distance_m.min(120.0) * 0.15;
    if recently_rejected {
        s -= 30.0;
    }
    s
}

/// His intent bonus on top of the approach score (her plan for today) and the withdraw bias.
pub fn intent_bonus(intent: &str) -> f32 {
    match intent {
        "seek_player" | "ask_help" => 15.0,
        "avoid_player" => -40.0,
        _ => 0.0,
    }
}

/// This turn's answers to every kind of invitation, rolled once so her words and the rules agree. `cooling[k]`:
/// she refused that kind less than ten minutes ago.
pub fn stance(r: &Relation, appeal: f32, awkward: f32, cooling: &[bool], seed: i32) -> (Vec<bool>, String) {
    let mut rng = RandomStream::new(seed);
    let mut accept = Vec::with_capacity(KINDS);
    let mut lines = Vec::with_capacity(KINDS);
    for k in 0..KINDS {
        let cool = cooling.get(k).copied().unwrap_or(false);
        let p = if cool { 0.0 } else { chance(score(k, r, appeal, awkward)) };
        let yes = rng.frand() < p;
        accept.push(yes);
        lines.push(if yes {
            crate::tr::m("invite.will_accept", &[("name", crate::tr::a(crate::tr::t(NAMES[k])))])
        } else {
            crate::tr::m("invite.will_refuse_with_reason", &[("name", crate::tr::a(crate::tr::t(NAMES[k]))), ("reason", crate::tr::a(&(if cool { crate::tr::t("invite.refused_just_now") } else { refusal_reason(k, r) })))])
        });
    }
    (accept, lines.join(crate::tr::t("academics.semicolon_sep")))
}

pub fn eval(places: &crate::places::PlacesTuning, kind: &str, request: &Value) -> Option<Result<Value, String>> {
    match kind {
        "invite_kind" | "appeal" | "invite_stance" | "invite_refused" | "approach_score" => Some(handle(places, kind, request)),
        _ => None,
    }
}

fn handle(places: &crate::places::PlacesTuning, kind: &str, request: &Value) -> Result<Value, String> {
    let num = |name: &str| request.get(name).and_then(Value::as_f64).unwrap_or(0.0) as f32;
    let text = |name: &str| request.get(name).and_then(Value::as_str).unwrap_or("").to_string();
    let flag = |name: &str| request.get(name).and_then(Value::as_bool).unwrap_or(false);
    if kind == "invite_kind" {
        let k = kind_of(&text("action"));
        return Ok(json!({"kind": k, "name": crate::tr::t(NAMES[k]), "private": k == PRIVATE}));
    }
    let c = crate::heroine::Ctx::from_request(request)?;
    let appeal = c.appeal(num("look"));
    Ok(match kind {
        "appeal" => json!({"appeal": appeal}),
        "invite_stance" => {
            let cooling: Vec<bool> = request.get("cooling").and_then(|v| serde_json::from_value(v.clone()).ok()).unwrap_or_default();
            let seed = request.get("seed").and_then(Value::as_i64).unwrap_or(0) as i32;
            // Where they are makes saying yes a little harder (a loud place feels wrong for a quiet library).
            let place = text("place");
            let crowd = crate::places::crowd_at(&place, request.get("hours").and_then(Value::as_f64).unwrap_or(12.0));
            let awkward = crate::places::awkwardness(places.norms(&place), crowd, places.act("loud")) * 0.3;
            let (accept, line) = stance(&c.rel, appeal, awkward, &cooling, seed);
            json!({"accept": accept, "text": line, "appeal": appeal})
        }
        "invite_refused" => {
            // A refused invitation (her rolled stance said no): her reason; when he keeps asking after a no within ten
            // minutes, the third time costs respect and she remembers being pestered.
            let k = kind_of(&text("action"));
            let why = refusal_reason(k, &c.rel);
            let count = request.get("refusals").and_then(Value::as_i64).unwrap_or(0) as i32 + if flag("again") { 1 } else { 0 };
            let mut notes = vec![json!({"act": "invite_refused", "text": crate::tr::m("invite.refused_his_invite", &[("activity", crate::tr::a(crate::tr::t(NAMES[k]))), ("reason", crate::tr::a(&why))]), "importance": 0.35})];
            let mut rel = c.rel.clone();
            let pestered = count >= 3;
            if pestered {
                rel.respect = (rel.respect - 5.0).max(0.0);
                rel.resentment = (rel.resentment + 5.0).min(100.0);
                notes.push(json!({"act": "pestered", "text": crate::tr::t("invite.he_keeps_pestering"), "importance": 0.6}));
            }
            json!({"kind": k, "why": why, "refusals": if pestered { 0 } else { count }, "notes": notes, "relation": rel.patch(&c.rel)})
        }
        _ => {
            // approach_score
            let score = approach_score(&c.rel, appeal, num("distance_m"), flag("rejected")) + intent_bonus(&text("intent")) - num("withdraw") * 40.0;
            // Charmed enough: she comes to ask him out, and will lead the way (plan §12.3).
            json!({"score": score, "appeal": appeal, "comes": score >= 60.0, "invites": appeal >= 50.0})
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kinds_of_actions() {
        assert_eq!(kind_of("sleep_hotel"), SLEEP);
        assert_eq!(kind_of("hotel_rest"), PRIVATE);
        assert_eq!(kind_of("cafeteria_meal"), MEAL);
        assert_eq!(kind_of("dept_food"), MEAL, "the meal list wins over dept_");
        assert_eq!(kind_of("dept_clothes"), LEISURE);
        assert_eq!(kind_of("go:park"), VISIT);
        assert_eq!(kind_of("drink"), LIGHT);
    }

    #[test]
    fn appeal_weights_by_material() {
        let t = Traits::default();
        let merc = serde_json::json!({"material": "Mercenary"});
        assert_eq!(appeal(50.0, 100.0, &t, &merc, 0.0), 35.0);
        assert_eq!(appeal(50.0, 100.0, &t, &serde_json::json!({}), 70.0), 35.0, "mixed 25 + admiration 10");
        assert_eq!(appeal(500.0, 500.0, &t, &serde_json::json!({}), 0.0), 100.0);
    }

    #[test]
    fn scores_reasons_and_stance() {
        let stranger = Relation::default();
        let s = score(PRIVATE, &stranger, 0.0, 0.0);
        assert!((s - (20.0 + 3.0 - 55.0 - 30.0)).abs() < 1e-4, "trust 20 counts 3; a stranger at favor 0 likes him 0");
        assert!(chance(s) < 0.01);
        assert_eq!(refusal_reason(PRIVATE, &stranger), "我們才剛認識，太快了");
        assert_eq!(refusal_reason(LIGHT, &Relation { resentment: 60.0, ..Default::default() }), "還在氣他");
        let (accept, text) = stance(&stranger, 0.0, 0.0, &[false, true], 7);
        assert_eq!(accept.len(), KINDS);
        assert!(!accept[1], "cooling never accepts");
        assert!(text.contains("剛剛才拒絕過"));
        assert_eq!(stance(&stranger, 0.0, 0.0, &[], 7), stance(&stranger, 0.0, 0.0, &[], 7), "same seed, same answers");
    }

    #[test]
    fn pestering_costs_respect_on_the_third_refusal() {
        let req = serde_json::json!({"relation": {"id": "a"}, "profile": {}, "action": "meal", "again": true, "refusals": 2});
        let v = eval(&crate::tuning::Tuning::builtin().places, "invite_refused", &req).unwrap().unwrap();
        assert_eq!(v["refusals"], 0);
        assert_eq!(v["notes"].as_array().unwrap().len(), 2);
        assert_eq!(v["relation"]["respect"], 15.0);
        let req = serde_json::json!({"relation": {"id": "a"}, "profile": {}, "action": "meal", "again": false, "refusals": 0});
        let v = eval(&crate::tuning::Tuning::builtin().places, "invite_refused", &req).unwrap().unwrap();
        assert_eq!((v["refusals"].as_i64(), v["notes"].as_array().unwrap().len()), (Some(0), 1));
    }

    #[test]
    fn approach() {
        let lover = Relation { stage: "lover".into(), social: 20.0, ..Default::default() }.with_liking(80.0);
        let s = approach_score(&lover, 20.0, 10.0, false);
        assert!((s - (45.0 + 20.0 + 10.0 + 20.0 - 1.5)).abs() < 1e-4);
        assert!(approach_score(&lover, 20.0, 1000.0, true) < s - 30.0);
    }
}
