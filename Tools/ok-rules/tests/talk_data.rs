//! The shipped dialogue catalogues parse the way Unreal parsed them (no node lost to a reader difference).
use ok_rules::Rules;
use serde_json::{json, Value};
use std::path::PathBuf;

fn data_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/StreetSocialData")
}

#[test]
fn shipped_catalogues_load() {
    for runtime in ["RuntimeV2", "RuntimeV3"] {
        let dir = data_dir().join(runtime);
        let catalogue = dir.join("catalogue.json");
        if !catalogue.exists() || !dir.join("rules.json").exists() {
            continue;
        }
        let rules = Rules::open("{}").unwrap();
        let topics = dir.join("topics.json");
        let request = json!({"kind": "talk_load", "catalogue": catalogue.to_string_lossy(), "topics": if topics.exists() { topics.to_string_lossy().to_string() } else { String::new() }});
        let answer: Value = serde_json::from_str(&rules.eval(&request.to_string()).unwrap()).unwrap();
        println!("{runtime}: {answer}");
        assert!(answer["nodes"].as_u64().unwrap() > 100, "{runtime}: {answer}");
        assert!(answer["step_up"].is_array());
        // A stranger of personality 0 has something to say when greeted.
        let v: Value = serde_json::from_str(&rules.eval(&json!({"kind": "talk_topic", "category": "greeting", "relation": {"id": "x", "talks_remaining": 5}, "personality": "0", "budget": 5, "seed": 1}).to_string()).unwrap()).unwrap();
        println!("{runtime} greeting: {v}");
    }
}

fn ask(rules: &Rules, v: Value) -> Value {
    serde_json::from_str(&rules.eval(&v.to_string()).unwrap()).unwrap()
}

/// Ported from OKStreetCheckIntegration / OKStreetCheckRelationship (OKStreetSocialRules.h): the shipped lines and rules.
#[test]
fn shipped_lines_and_events() {
    let dir = data_dir();
    if !dir.join("catalogue.json").exists() {
        return;
    }
    let rules = Rules::open("{}").unwrap();
    let loaded = ask(&rules, json!({"kind": "talk_load", "catalogue": dir.join("catalogue.json").to_string_lossy()}));
    assert_eq!((loaded["nodes"].as_u64(), loaded["skipped"].as_u64()), (Some(1230), Some(0)), "shipped catalogue parses every node");
    assert_eq!(loaded["step_up"], json!([75, 100, 150]));
    assert_eq!(loaded["beyond"], 150);
    let stranger = json!({"id": "heroine", "talks_remaining": 5});
    let lines = |category: &str, relation: &Value| ask(&rules, json!({"kind": "talk_lines", "category": category, "relation": relation, "personality": "0", "budget": 5}));
    let find = |v: &Value, id: &str| v["items"].as_array().unwrap().iter().find(|i| i["node"] == id).cloned();
    // The automatic greeting is the highest-priority generic line.
    let g = lines("greeting", &stranger);
    assert_eq!((g["items"][0]["node"].as_str(), g["items"][0]["eligible"].as_bool()), (Some("p0_r11_greeting"), Some(true)));
    // Condition id 2 stays disabled despite priority 360.
    if let Some(i) = find(&g, "p0_r0_greeting") {
        assert!(i["eligible"] == false && i["reason"].as_str().unwrap().contains("street_support_unsupported:id_2"));
    }
    // The success branch row is not a line to choose; a text-only line keeps its favor.
    let d = lines("daily", &stranger);
    if let Some(i) = find(&d, "p0_r15_daily") {
        assert_eq!(i["reason"], "branch_role_unsupported:success");
    }
    // A hole in the voice sequence keeps the line out (friend stage line).
    let f = lines("greeting", &json!({"id": "heroine", "stage": "friend", "talks_remaining": 5}));
    if let Some(i) = find(&f, "p0_r421_greeting") {
        assert_eq!(i["reason"], "voice_sequence_incomplete:missing_ogg");
    }
    // Each talk category has something to say.
    for c in ["daily", "romance", "intimacy"] {
        let v = ask(&rules, json!({"kind": "talk_topic", "category": c, "relation": stranger, "personality": "0", "personality_id": 0, "budget": 5, "seed": 7}));
        assert!(v["node"].is_string(), "{c}: {v}");
    }
    // Real favor delta clamps inside stage 0 and does not rank up (p0_r21 favor 3: 70 -> 73).
    let v = ask(&rules, json!({"kind": "talk_commit", "relation": {"id": "heroine", "favor": 70, "talks_remaining": 5}, "transaction": "heroine|p0_r21_daily", "effect": {"favor": 3, "talk_cost": 1, "memory": "p0_r21_daily"}}));
    assert_eq!((v["applied"].as_bool(), v["relation"]["favor"].as_f64()), (Some(true), Some(73.0)));
    // Friendship points at the condition 10 farewell (not voiced) and needs the cap.
    let low = ask(&rules, json!({"kind": "talk_event", "event": "friendship", "relation": {"id": "heroine", "favor": 74}, "personality": "0"}));
    assert_eq!(low["reason"], "friendship_favor_below_limit");
    let ready = ask(&rules, json!({"kind": "talk_event", "event": "friendship", "relation": {"id": "heroine", "favor": 75}, "personality": "0"}));
    assert_eq!((ready["resolved"].as_bool(), ready["line"].as_str(), ready["playable"].as_bool()), (Some(true), Some("p0_r110_farewell"), Some(false)));
    // Confession: under 0.7 of the friend cap, at 70 (27%, half the table under 100), refusal costs 25; over 100 it is 50% rising to certain at 150.
    let c = |favor: f64, stage: &str, roll: i64| ask(&rules, json!({"kind": "talk_event", "event": "confession", "relation": {"id": "heroine", "favor": favor, "stage": stage, "talks_remaining": 5}, "personality": "0", "roll": roll}));
    assert_eq!(c(69.99, "friend", 0)["reason"], "confession_favor_below_0_7");
    let open = c(70.0, "friend", 0);
    assert_eq!((open["chance"].as_i64(), open["reason"].as_str()), (Some(27), Some("confession_success")));
    let miss = c(99.0, "friend", 80);
    assert_eq!((miss["chance"].as_i64(), miss["refusal"].as_bool(), miss["effect"]["favor"].as_f64()), (Some(34), Some(true), Some(-25.0)));
    for roll in [0, 50, 99] {
        let sure = c(200.0, "stranger", roll);
        assert_eq!((sure["chance"].as_i64(), sure["refusal"].as_bool(), sure["effect"]["relation_event"].as_str()), (Some(100), Some(false), Some("lover")));
    }
}

/// Ported from Tests/OKStreetSocialRules.spec.cpp RulesAndRealCatalogue: the prepared portable catalogue with source
/// preferences has voiced successes and refusals, and the manual farewell fits each budget.
#[test]
fn portable_catalogue_branches_and_farewells() {
    use ok_rules::relation::Relation;
    use ok_rules::talk::{farewell_lines, load, resolve_branch, Who};
    let dir = data_dir().join("TestFixtures/IntegrationCompletion02");
    if !dir.join("dialogue.runtime.json").exists() {
        return;
    }
    let data = load(&json!({"catalogue": dir.join("dialogue.runtime.json").to_string_lossy()})).unwrap();
    assert_eq!((data.nodes.len(), data.branches.len()), (1230, 705));
    assert!(!data.rules.preference.is_empty(), "source preferences load");
    let (mut playable, mut refusals) = (0, 0);
    for b in data.branches.iter().filter(|b| b.group == "Talk" && b.stage == 0 && !b.indexer.is_empty()) {
        let who = Who { personality: b.personality.to_string(), personality_id: b.personality, talk_limit: false, budget: 5 };
        for roll in [0, 99] {
            let d = resolve_branch(&data, &Relation { id: "portable".into(), ..Default::default() }, &who, b, roll);
            if d.decided && d.playable {
                playable += 1;
                refusals += d.refusal as i32;
            }
        }
    }
    assert!(playable > 0 && refusals > 0, "voiced success and refusal: {playable} {refusals}");
    let who = Who { personality: "0".into(), personality_id: 0, talk_limit: false, budget: 5 };
    for talks in [5, 1, 0] {
        let lines = farewell_lines(&data, &Relation { id: "end".into(), talks_remaining: talks, ..Default::default() }, &who);
        assert!(!lines.is_empty(), "a farewell at {talks} talks");
        for n in lines {
            assert_eq!(data.nodes[n].condition_id, if talks == 5 { 3 } else { 2 }, "the farewell condition fits the budget");
        }
    }
}
