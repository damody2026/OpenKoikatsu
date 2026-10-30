//! The one-on-one's lasting marks on her, after the original H scene (Heroine.HExperience, HSceneProc.EndProc and
//! CalcParameter, HSceneResult.OnClickEnd): how many times, her first kiss, her virginity front and back, and the five
//! area experiences (chest, crotch, anal, butt, nipples; index 0 is kissing) that make her 初めて, 不慣れ, 慣れ or 淫乱.
//! Kept per heroine in ext.hexp.<id>. Unreal counts what happened in the scene and asks; the level gates poses, raw
//! insertion and anal, and picks her voice bank.
use crate::effects::Effects;
use crate::relation::Relation;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

/// Area indices, as the original hAreaExps.
pub const KISS: usize = 0;
pub const CHEST: usize = 1;
pub const CROTCH: usize = 2;
pub const ANAL: usize = 3;
pub const BUTT: usize = 4;
pub const NIPPLE: usize = 5;
const AREA_KEYS: [&str; 6] = ["kiss", "chest", "crotch", "anal", "butt", "nipple"];

/// What she has been through (Heroine's H fields).
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct HExp {
    pub count: i32,
    pub kissed: bool,
    pub areas: [f32; 6],
    /// Added to the crotch and anal areas when judging (countKokanH, countAnalH).
    pub crotch_h: f32,
    pub anal_h: f32,
    pub service: f32,
    pub service_count: i32,
    pub raw_count: i32,
    pub virgin: bool,
    pub anal_virgin: bool,
    /// The game day the today_* fields are for (-1 none): what she has had today carries from one scene to the next, so
    /// leaving the room and coming back does not undo "that is enough for today".
    pub today_day: i64,
    pub today_orgasms: i32,
    pub today_service: i32,
    pub today_anal_refused: bool,
}

impl Default for HExp {
    fn default() -> Self {
        HExp { count: 0, kissed: false, areas: [0.0; 6], crotch_h: 0.0, anal_h: 0.0, service: 0.0, service_count: 0, raw_count: 0, virgin: true, anal_virgin: true, today_day: -1, today_orgasms: 0, today_service: 0, today_anal_refused: false }
    }
}

impl HExp {
    /// An area as the original judges it: crotch and anal count their intercourse too.
    pub fn area(&self, i: usize) -> f32 {
        let extra = match i {
            CROTCH => self.crotch_h,
            ANAL => self.anal_h,
            _ => 0.0,
        };
        (self.areas[i] + extra).min(100.0)
    }
    /// 0 初めて, 1 不慣れ, 2 慣れ, 3 淫乱 (Heroine.HExperience): lewdness 100 wins only once she has done it.
    pub fn level(&self, lewdness: f32) -> i32 {
        if self.count == 0 {
            0
        } else if lewdness >= 100.0 {
            3
        } else if (CHEST..=NIPPLE).any(|i| self.area(i) < 100.0) {
            1
        } else {
            2
        }
    }
}

fn of(ext: &Value, girl: &str) -> HExp {
    ext.get("hexp").and_then(|h| h.get(girl)).and_then(|v| serde_json::from_value(v.clone()).ok()).unwrap_or_default()
}

/// The original's diminishing ladder for each finish or climax in one scene: 15, 10, 5, then 1.
fn ladder(times: i64) -> f32 {
    (0..times.max(0)).map(|k| match k { 0 => 15.0, 1 => 10.0, 2 => 5.0, _ => 1.0 }).sum()
}

fn level_name(level: i32) -> &'static str {
    match level {
        0 => crate::tr::t("hexp.level_first"),
        1 => crate::tr::t("hexp.level_inexperienced"),
        2 => crate::tr::t("hexp.level_used"),
        _ => crate::tr::t("hexp.level_lewd"),
    }
}

fn area_name(i: usize) -> &'static str {
    match i {
        KISS => crate::tr::t("hexp.area_kiss"),
        CHEST => crate::tr::t("hexp.area_chest"),
        CROTCH => crate::tr::t("hexp.area_crotch"),
        ANAL => crate::tr::t("hexp.area_anal"),
        BUTT => crate::tr::t("hexp.area_butt"),
        _ => crate::tr::t("hexp.area_nipple"),
    }
}

/// What the scene allows with her now. Request: girl, ext, relation. Answer: level (0..3) and its name, the five areas,
/// raw_ok (NamaInsertCheck without the calendar: 淫乱, or 慣れ with five raw times or as his lover), anal_chance
/// (hAreaExps[3]+countAnalH percent), kiss_ok (lover or already kissed), used (poses marked isExperience need 慣れ).
/// Her character moves each of these (the street has no calendar or H-scene favor of the original): a girl of low
/// chastity who is lewd lets him go raw sooner and is easier about anal, one who is drawn to him kisses before they
/// are lovers, one who resents him does not.
fn experience(r: &Value) -> Result<Value, String> {
    let girl = r.get("girl").and_then(Value::as_str).ok_or("missing_field: girl")?;
    let rel: Option<Relation> = r.get("relation").filter(|v| v.is_object()).map(|v| serde_json::from_value(v.clone())).transpose().map_err(|e| format!("relation: {e}"))?;
    let lewd = rel.as_ref().map_or(0.0, |r| r.lewdness);
    let lover = rel.as_ref().is_some_and(|r| r.stage == "lover");
    let h = of(r.get("ext").unwrap_or(&Value::Null), girl);
    let level = h.level(lewd);
    let t = crate::traits::Traits::from_profile(r.get("profile").unwrap_or(&Value::Null));
    let chaste = rel.as_ref().map_or_else(|| crate::heroine::default_chastity(&t), |r| crate::heroine::chastity(r, &t));
    let attraction = rel.as_ref().map_or(0.0, |r| r.attraction);
    let resentment = rel.as_ref().map_or(0.0, |r| r.resentment);
    let easy = lewd / 100.0 + (50.0 - chaste) / 100.0; // about -0.45 (chaste, innocent) .. 1.45 (lewd, loose)
    let kiss_ok = lover || h.kissed || (resentment < 30.0 && attraction + 30.0 * easy >= 45.0 + 0.3 * chaste);
    let raw_ok = level == 3 || (level == 2 && (h.raw_count >= 5 || lover)) || (level >= 1 && easy >= 0.9) || (lover && level >= 1 && t.worship >= 70);
    let anal_chance = (h.area(ANAL) / 100.0 + 0.25 * easy).clamp(0.0, 1.0);
    // How many times in one scene she serves him to the end (hand, mouth, breasts) before she has had enough: more
    // as she gets used to it, is lewd and loose, or is his lover; once if she resents him.
    let service_rounds = if resentment >= 50.0 { 1 } else { (1 + level + (1.5 * easy).round() as i32 + i32::from(lover)).clamp(1, 6) };
    // How many times she comes in one scene before she has had enough and wants it to stop (no more touching, moving,
    // entering her): the same things move it, a lover lasts longer.
    let climax_limit = if resentment >= 50.0 { 1 } else { (2 + level + (1.5 * easy).round() as i32 + 2 * i32::from(lover)).clamp(2, 10) };
    // Today so far (the request's game day): she starts this scene where the last one today left her.
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(-1);
    let today = day >= 0 && h.today_day == day;
    let (today_orgasms, today_service, anal_refused_today) = if today { (h.today_orgasms, h.today_service, h.today_anal_refused) } else { (0, 0, false) };
    let areas: serde_json::Map<String, Value> = AREA_KEYS.iter().enumerate().map(|(i, k)| (k.to_string(), json!(h.area(i)))).collect();
    Ok(json!({
        "level": level, "level_name": level_name(level), "areas": areas, "count": h.count, "virgin": h.virgin, "anal_virgin": h.anal_virgin,
        "raw_ok": raw_ok,
        "anal_chance": anal_chance,
        "service_rounds": service_rounds,
        "climax_limit": climax_limit,
        // She resents him (resentment 50 or more): no anal whatever her experience (the scene asks nothing more of her).
        "resentful": resentment >= 50.0,
        "today_orgasms": today_orgasms,
        "today_service": today_service,
        "anal_refused_today": anal_refused_today,
        "had_enough": today_orgasms >= climax_limit,
        "kiss_ok": kiss_ok,
        "chastity": chaste,
        "used": level >= 2,
    }))
}

/// One one-on-one ended. Request: girl, ext, relation, place, counts {areas: {chest: seconds touched, ...}, kiss
/// (seconds), service_finishes, service (any service pose played), crotch {inserted, climaxes}, anal {inserted,
/// climaxes}, raw (inserted without a condom), her_orgasms, played (any pose at all)}. Answer: ext (ext.hexp.<girl>),
/// effects (the relation and her memories: the time itself, firsts, an area she got used to, a new level), level_before,
/// level_after, hexp.
fn session_end(r: &Value) -> Result<Value, String> {
    let girl = r.get("girl").and_then(Value::as_str).ok_or("missing_field: girl")?;
    let mut rel: Relation = serde_json::from_value(r.get("relation").cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))?;
    let place = r.get("place").and_then(Value::as_str).unwrap_or("");
    let mut ext = r.get("ext").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    let c = r.get("counts").cloned().unwrap_or_else(|| json!({}));
    let num = |v: &Value, k: &str| v.get(k).and_then(Value::as_f64).unwrap_or(0.0);
    let flag = |v: &Value, k: &str| v.get(k).and_then(Value::as_bool).unwrap_or(false);
    let before = rel.clone();
    let mut h = of(&ext, girl);
    let was = h.clone();
    let level_before = h.level(rel.lewdness);
    let mut fx = Effects::default();
    // Today's tally (whatever was played): a new game day starts it again.
    if let Some(day) = r.get("day").and_then(Value::as_i64).filter(|d| *d >= 0) {
        if h.today_day != day {
            h.today_day = day;
            h.today_orgasms = 0;
            h.today_service = 0;
            h.today_anal_refused = false;
        }
        h.today_orgasms += num(&c, "her_orgasms") as i32;
        h.today_service += num(&c, "service_finishes") as i32;
        h.today_anal_refused |= flag(&c, "anal_refused");
    }

    // Only what they did leaves a mark: opening the room and leaving at once is no intimate time.
    let played = flag(&c, "played");
    if played {
        fx.note_at(&rel.id, "intimacy", crate::tr::t("shop.private_room_time"), place, 0.9);
        rel.add_liking(5.0);
        rel.attraction = (rel.attraction + 5.0).min(100.0);
        rel.dependency = (rel.dependency + 3.0).min(100.0);
        // Touching: selectAreas*0.1 a scene in the original, where one scene caps at 100; here a second of touch is
        // worth half a point, so a long caress of one place is a big step and five scenes of it make her used to it.
        let areas = c.get("areas").cloned().unwrap_or_else(|| json!({}));
        for i in CHEST..=NIPPLE {
            let secs = num(&areas, AREA_KEYS[i]).clamp(0.0, 200.0) as f32;
            h.areas[i] = (h.areas[i] + secs * 0.5).min(100.0);
        }
        let kiss = num(&c, "kiss").max(num(&areas, "kiss")).clamp(0.0, 100.0) as f32;
        if kiss > 0.0 {
            h.areas[KISS] = (h.areas[KISS] + kiss).min(100.0);
            h.kissed = true;
        }
        let finishes = num(&c, "service_finishes") as i64;
        h.service = (h.service + ladder(finishes)).min(1000.0);
        if flag(&c, "service") || finishes > 0 {
            h.service_count = (h.service_count + 1).min(1000);
        }
        for (key, slot, virgin) in [("crotch", &mut h.crotch_h, &mut h.virgin), ("anal", &mut h.anal_h, &mut h.anal_virgin)] {
            let part = c.get(key).cloned().unwrap_or_else(|| json!({}));
            if flag(&part, "inserted") {
                *slot = (*slot + 5.0 + ladder(num(&part, "climaxes") as i64)).min(1000.0);
                *virgin = false;
            }
        }
        if flag(&c, "raw") {
            h.raw_count = (h.raw_count + 1).min(1000);
        }
        h.count = (h.count + 1).min(10000);
        // Lewdness: the original's result table adds a few points a scene, more when she came; anal play adds to it.
        let orgasms = num(&c, "her_orgasms").clamp(0.0, 3.0) as f32;
        let anal = if flag(c.get("anal").unwrap_or(&Value::Null), "inserted") { 2.0 } else { 0.0 };
        rel.lewdness = (rel.lewdness + 2.0 + 3.0 * orgasms + anal).min(crate::talk::LEWDNESS_CAP);
        // Every climax he gave her warms her to him.
        let climaxes = num(&c, "her_orgasms").clamp(0.0, 20.0) as f32;
        if climaxes > 0.0 {
            rel.add_liking(1.5 * climaxes);
        }
        // He asked again for no condom and she gave in: how she takes it is her chastity and her character (a proud
        // girl minds, one who worships him or is already lewd less so).
        if flag(&c, "raw_pressed") {
            let t = crate::traits::Traits::from_profile(r.get("profile").unwrap_or(&Value::Null));
            let chaste = crate::heroine::chastity(&rel, &t);
            let delta = ((40.0 - chaste) / 10.0 - (t.self_esteem as f32 - 50.0) / 25.0 + (t.worship as f32 - 50.0) / 25.0 + rel.lewdness / 50.0).clamp(-8.0, 4.0);
            rel.add_liking(delta);
            fx.note_at(&rel.id, "h_first", crate::tr::t(if delta < 0.0 { "hexp.raw_pressed_upset" } else { "hexp.raw_pressed_ok" }), place, 0.85);
        }
        // The whole time in one memory she can be asked about ("how many times did you come?").
        let int = |k: &str| crate::tr::a(&(num(&c, k) as i64).to_string());
        let poses = r.get("poses").and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_str).collect::<Vec<_>>().join("、")).unwrap_or_default();
        let summary = crate::tr::m("hexp.session_summary", &[("orgasms", int("her_orgasms")), ("finishes", int("his_finishes")), ("inside", int("his_inside")), ("outside", int("his_outside")), ("mouth", int("his_mouth")), ("swallowed", int("swallowed")), ("poses", crate::tr::a(&poses))]);
        fx.note_at(&rel.id, "h_level", &summary, place, 0.9);
    }

    // Her memories of the firsts and of what changed in her.
    let mut first = |fx: &mut Effects, id: &'static str, importance: f32| fx.note_at(&rel.id, "h_first", crate::tr::t(id), place, importance);
    if was.count == 0 && h.count > 0 {
        first(&mut fx, "hexp.first_time", 1.0);
    }
    if was.virgin && !h.virgin {
        first(&mut fx, "hexp.lost_virginity", 1.0);
    }
    if was.anal_virgin && !h.anal_virgin {
        first(&mut fx, "hexp.first_anal", 0.95);
    }
    if !was.kissed && h.kissed {
        first(&mut fx, "hexp.first_kiss", 0.95);
    }
    for i in CHEST..=NIPPLE {
        if was.area(i) < 100.0 && h.area(i) >= 100.0 {
            fx.note_at(&rel.id, "h_area", &crate::tr::m("hexp.area_used_to", &[("area", crate::tr::a(area_name(i)))]), place, 0.7);
        }
    }
    let level_after = h.level(rel.lewdness);
    if level_after != level_before && level_after > 0 {
        fx.note_at(&rel.id, "h_level", &crate::tr::m("hexp.level_changed", &[("level", crate::tr::a(level_name(level_after)))]), place, 0.85);
    }
    fx.relation(&rel, &before);
    if !ext.get("hexp").is_some_and(Value::is_object) {
        ext["hexp"] = json!({});
    }
    ext["hexp"][girl] = serde_json::to_value(&h).map_err(|e| e.to_string())?;
    Ok(json!({"ext": ext, "effects": fx.into_value(), "level_before": level_before, "level_after": level_after, "level_name": level_name(level_after), "hexp": h}))
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "h_experience" => experience(r),
        "h_session_end" => session_end(r),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(lewd: f32) -> Value {
        let mut r = Relation { id: "g".into(), ..Relation::default() };
        r.lewdness = lewd;
        r.stage = "friend".into();
        serde_json::to_value(r).unwrap()
    }

    #[test]
    fn first_session_takes_her_virginity_and_makes_her_inexperienced() {
        let out = session_end(&json!({"girl": "g", "relation": rel(0.0), "ext": {}, "counts": {"played": true, "crotch": {"inserted": true, "climaxes": 1}, "kiss": 12, "her_orgasms": 1}})).unwrap();
        assert_eq!(out["level_before"], 0);
        assert_eq!(out["level_after"], 1);
        let h = &out["ext"]["hexp"]["g"];
        assert_eq!(h["virgin"], false);
        assert_eq!(h["anal_virgin"], true);
        assert_eq!(h["crotch_h"], 20.0);
        assert_eq!(h["kissed"], true);
        let notes: Vec<&str> = out["effects"].as_array().unwrap().iter().filter_map(|e| e.get("act").and_then(Value::as_str)).collect();
        assert!(notes.iter().filter(|a| **a == "h_first").count() >= 3, "first time, virginity, kiss: {notes:?}");
    }

    #[test]
    fn all_five_areas_at_100_make_her_used_to_it_and_lewdness_100_lewd() {
        let ext = json!({"hexp": {"g": {"count": 3, "areas": [50, 99, 100, 100, 100, 100], "kissed": true}}});
        let secs = json!({"chest": 10});
        let out = session_end(&json!({"girl": "g", "relation": rel(10.0), "ext": ext, "counts": {"played": true, "areas": secs}})).unwrap();
        assert_eq!(out["level_before"], 1);
        assert_eq!(out["level_after"], 2);
        let h: HExp = serde_json::from_value(out["ext"]["hexp"]["g"].clone()).unwrap();
        assert_eq!(h.level(100.0), 3);
        assert_eq!(HExp::default().level(100.0), 0, "never done it: 初めて whatever her lewdness");
    }

    #[test]
    fn climaxes_warm_her_and_a_chaste_girl_minds_being_pressed_raw() {
        let base = session_end(&json!({"girl": "g", "relation": rel(0.0), "ext": {}, "counts": {"played": true}})).unwrap();
        let warm = session_end(&json!({"girl": "g", "relation": rel(0.0), "ext": {}, "counts": {"played": true, "her_orgasms": 3}})).unwrap();
        let favor = |o: &Value| o["effects"].as_array().unwrap().iter().find(|e| e["op"] == "relation").map(|e| e["set"]["favor"].as_f64().unwrap_or(0.0)).unwrap_or(0.0);
        assert!(favor(&warm) > favor(&base), "three climaxes: {} vs {}", favor(&warm), favor(&base));
        let mut chaste: Relation = serde_json::from_value(rel(0.0)).unwrap();
        chaste.chastity = 90.0;
        chaste.favor = 50.0;
        let pressed = session_end(&json!({"girl": "g", "relation": chaste, "ext": {}, "counts": {"played": true, "raw_pressed": true}})).unwrap();
        let mut easy: Relation = serde_json::from_value(rel(0.0)).unwrap();
        easy.chastity = 10.0;
        easy.favor = 50.0;
        let fine = session_end(&json!({"girl": "g", "relation": easy, "ext": {}, "counts": {"played": true, "raw_pressed": true}})).unwrap();
        assert!(favor(&pressed) < favor(&fine), "chaste {} vs easy {}", favor(&pressed), favor(&fine));
    }

    #[test]
    fn her_character_moves_kiss_raw_and_anal() {
        let mut shy: Relation = serde_json::from_value(rel(0.0)).unwrap();
        shy.chastity = 85.0;
        shy.attraction = 50.0;
        let mut loose: Relation = serde_json::from_value(rel(60.0)).unwrap();
        loose.chastity = 15.0;
        loose.attraction = 50.0;
        let ext = json!({"hexp": {"g": {"count": 1, "virgin": false}}});
        let a = experience(&json!({"girl": "g", "relation": shy, "ext": ext})).unwrap();
        let b = experience(&json!({"girl": "g", "relation": loose, "ext": ext})).unwrap();
        assert_eq!(a["kiss_ok"], false);
        assert_eq!(b["kiss_ok"], true);
        assert_eq!(a["raw_ok"], false);
        assert_eq!(b["raw_ok"], true);
        assert!(b["anal_chance"].as_f64().unwrap() > a["anal_chance"].as_f64().unwrap());
        assert!(b["service_rounds"].as_i64().unwrap() > a["service_rounds"].as_i64().unwrap());
        assert!(a["service_rounds"].as_i64().unwrap() >= 1);
        assert!(b["climax_limit"].as_i64().unwrap() > a["climax_limit"].as_i64().unwrap());
        assert!(a["climax_limit"].as_i64().unwrap() >= 2);
        let mut sore = loose.clone();
        sore.resentment = 60.0;
        let c = experience(&json!({"girl": "g", "relation": sore, "ext": ext})).unwrap();
        assert_eq!(c["resentful"], true);
        assert_eq!(c["climax_limit"], 1);
        assert_eq!(b["resentful"], false);
    }

    #[test]
    fn what_she_had_today_carries_to_the_next_scene_that_day() {
        let r = rel(0.0);
        let first = session_end(&json!({"girl": "g", "relation": r, "ext": {}, "day": 3, "counts": {"played": true, "her_orgasms": 3, "service_finishes": 1, "anal_refused": true}})).unwrap();
        let e = experience(&json!({"girl": "g", "relation": r, "ext": first["ext"], "day": 3})).unwrap();
        assert_eq!(e["today_orgasms"], 3);
        assert_eq!(e["today_service"], 1);
        assert_eq!(e["anal_refused_today"], true);
        assert_eq!(e["had_enough"], e["climax_limit"].as_i64().unwrap() <= 3);
        let again = session_end(&json!({"girl": "g", "relation": r, "ext": first["ext"], "day": 3, "counts": {"played": true, "her_orgasms": 2}})).unwrap();
        assert_eq!(experience(&json!({"girl": "g", "relation": r, "ext": again["ext"], "day": 3})).unwrap()["today_orgasms"], 5);
        let tomorrow = experience(&json!({"girl": "g", "relation": r, "ext": again["ext"], "day": 4})).unwrap();
        assert_eq!(tomorrow["today_orgasms"], 0);
        assert_eq!(tomorrow["anal_refused_today"], false);
    }

    #[test]
    fn nothing_played_changes_no_experience() {
        let out = session_end(&json!({"girl": "g", "relation": rel(0.0), "ext": {}, "counts": {}})).unwrap();
        assert_eq!(out["ext"]["hexp"]["g"]["count"], 0);
        assert_eq!(out["level_after"], 0);
    }

    #[test]
    fn raw_and_anal_follow_experience() {
        let e = experience(&json!({"girl": "g", "relation": rel(0.0), "ext": {"hexp": {"g": {"count": 1, "anal_h": 30, "areas": [0, 0, 0, 20, 0, 0]}}}})).unwrap();
        assert_eq!(e["raw_ok"], false);
        assert!((e["anal_chance"].as_f64().unwrap() - 0.5).abs() < 1e-6);
        assert_eq!(e["kiss_ok"], false);
        assert_eq!(ladder(5), 32.0);
    }
}
