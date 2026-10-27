//! Plan §9 "anything goes anywhere, but some places make it awkward": each place's norms and expected crowd, what an
//! act needs, how awkward it is there, and how she takes it. Tables in places.toml; Unreal caches the norms table for
//! its own displays (`places_table`).
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::BTreeMap;

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Norms {
    pub privacy: i32,
    pub quiet: f32,
    pub formality: f32,
    pub food: bool,
    pub staff: bool,
    pub label: String,
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ActNeeds {
    pub min_privacy: i32,
    pub max_crowd: f32,
    pub food: bool,
    pub loud: bool,
    pub intimate: bool,
    pub name: String,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct PlacesTuning {
    pub default: Norms,
    pub norms: BTreeMap<String, Norms>,
    pub act_default: ActNeeds,
    pub acts: BTreeMap<String, ActNeeds>,
}

/// Her reaction to an awkward act (EOKStreetAwkwardReaction order).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reaction {
    Fine = 0,
    Shy = 1,
    Protest = 2,
    Refuse = 3,
    Thrilled = 4,
}

impl PlacesTuning {
    pub fn norms(&self, place: &str) -> &Norms {
        self.norms.get(place).unwrap_or(&self.default)
    }

    pub fn act(&self, act: &str) -> &ActNeeds {
        self.acts.get(act).unwrap_or(&self.act_default)
    }
}

/// Expected crowd 0..1 by place and hour (the street has no background crowd, so it is an estimate).
pub fn crowd_at(place: &str, hours: f64) -> f32 {
    let lunch = (11.5..13.5).contains(&hours);
    let dinner = (17.5..20.0).contains(&hours);
    let night = hours >= 22.0 || hours < 6.0;
    if night {
        return if place == "conveni" { 0.2 } else { 0.05 };
    }
    match place {
        "cafeteria" | "restaurant" => {
            if lunch || dinner {
                0.9
            } else {
                0.3
            }
        }
        "department" | "drinkshop" => {
            if (14.0..21.0).contains(&hours) {
                0.7
            } else {
                0.4
            }
        }
        _ if place == "library" || place == "lecture" || place == "school" || place.starts_with("campus_") => {
            if (9.0..18.0).contains(&hours) {
                0.6
            } else {
                0.2
            }
        }
        "" => {
            if (8.0..21.0).contains(&hours) {
                0.4
            } else {
                0.1
            }
        }
        _ => 0.4,
    }
}

/// awkwardness = sum of unmet needs x crowd x (1 + formality), roughly 0..3 (plan §9.3).
pub fn awkwardness(place: &Norms, crowd: f32, act: &ActNeeds) -> f32 {
    let mut unmet = 0.0;
    if act.food && !place.food {
        unmet += 1.0;
    }
    if act.loud {
        unmet += place.quiet;
    }
    if place.privacy < act.min_privacy {
        unmet += 0.6 * (act.min_privacy - place.privacy) as f32;
    }
    // Eating where people eat (sharing a dish at a restaurant table) is what everyone there does: the crowd
    // does not make it awkward. Only an intimate one (feeding her) still draws eyes.
    let what_people_do_here = act.food && place.food && !act.intimate;
    if crowd > act.max_crowd && !what_people_do_here {
        unmet += (crowd - act.max_crowd) * 1.5;
    }
    if act.intimate {
        unmet += 0.3 * place.formality;
    }
    unmet * (0.3 + crowd) * (1.0 + place.formality)
}

/// Her reaction (plan §9.3) from her shyness and public boldness, and how close they are.
pub fn react(awkward: f32, shyness: i32, boldness: i32, stage: &str) -> Reaction {
    let ease = match stage {
        "lover" => 0.5,
        "friend" => 0.2,
        _ => 0.0,
    } + boldness as f32 / 200.0;
    let felt = awkward * (0.5 + shyness as f32 / 100.0) - ease;
    if felt <= 0.15 {
        Reaction::Fine
    } else if boldness >= 70 && felt < 0.8 {
        Reaction::Thrilled
    } else if felt < 0.5 {
        Reaction::Shy
    } else if felt < 1.0 {
        Reaction::Protest
    } else {
        Reaction::Refuse
    }
}

/// Where he wakes after skipping to the morning: where he is if there is a bed for him there (his dorm, a hotel, an
/// inn, the onsen), else his own dorm (not standing all night at the girls' dorm door). Request: place. Answer: place.
fn wake_place(place: &str) -> &'static str {
    const BEDS: [&str; 5] = ["boys_dorm", "hotel", "lodge", "minshuku", "onsen"];
    BEDS.iter().copied().find(|b| *b == place).unwrap_or("boys_dorm")
}

/// A heroine who was with him when the game was saved (walking with him, out with him, sitting or talking with him), on
/// load (R3-238): she is put back where she was, not where her plan says. Walking or out with him, she follows him again;
/// sitting or talking with him, she waits beside him a while, unless her block now is a fixed one (class, shift, promise,
/// sleep), which takes her as usual. She was up and with him: the end of a night's sleep block (the day replanned on
/// load ran her sleep to 08:13) is not bed, only the night is (R3-241). Request: mode ("follow" | "stay"), fixed,
/// kind (her block now), minute (of the day). Answer: keep, follow, wait_minutes.
const RESUME_WAIT_MINUTES: i64 = 40;
fn resume_with_him(mode: &str, fixed: bool, kind: &str, minute: i64) -> Value {
    let follow = mode == "follow";
    let night = !(5 * 60..21 * 60).contains(&minute);
    let fixed = fixed && !(kind == "sleep" && !night);
    let keep = follow || !fixed;
    json!({"keep": keep, "follow": follow && keep, "wait_minutes": if keep { RESUME_WAIT_MINUTES } else { 0 }})
}

pub fn eval(t: &PlacesTuning, kind: &str, request: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "wake_place" => Ok(json!({"place": wake_place(request.get("place").and_then(Value::as_str).unwrap_or(""))})),
        "resume_with_him" => Ok(resume_with_him(
            request.get("mode").and_then(Value::as_str).unwrap_or(""),
            request.get("fixed").and_then(Value::as_bool).unwrap_or(false),
            request.get("kind").and_then(Value::as_str).unwrap_or(""),
            request.get("minute").and_then(Value::as_i64).unwrap_or(12 * 60),
        )),
        "places_table" => Ok(json!({"default": t.default, "norms": t.norms})),
        "place_crowd" => {
            let place = request.get("place").and_then(Value::as_str).unwrap_or("");
            let hours = request.get("hours").and_then(Value::as_f64).unwrap_or(12.0);
            Ok(json!({"crowd": crowd_at(place, hours)}))
        }
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn who_was_with_him_stays_with_him_after_a_load() {
        let walk = resume_with_him("follow", false, "leisure", 900);
        assert_eq!((walk["keep"].as_bool(), walk["follow"].as_bool()), (Some(true), Some(true)));
        let walk_in_class = resume_with_him("follow", true, "class", 600);
        assert_eq!(walk_in_class["follow"].as_bool(), Some(true), "out with him, her plan waits");
        let sat = resume_with_him("stay", false, "leisure", 900);
        assert_eq!((sat["keep"].as_bool(), sat["follow"].as_bool(), sat["wait_minutes"].as_i64()), (Some(true), Some(false), Some(40)));
        let on_shift = resume_with_him("stay", true, "work", 900);
        assert_eq!((on_shift["keep"].as_bool(), on_shift["wait_minutes"].as_i64()), (Some(false), Some(0)));
        // Breakfast with him at 07:52, her replanned sleep still running to 08:13: she stays (R3-241); at 23:30 bed takes her.
        let breakfast = resume_with_him("stay", true, "sleep", 7 * 60 + 52);
        assert_eq!(breakfast["keep"].as_bool(), Some(true));
        let night = resume_with_him("stay", true, "sleep", 23 * 60 + 30);
        assert_eq!(night["keep"].as_bool(), Some(false));
    }

    #[test]
    fn crowd_by_hour() {
        assert_eq!(crowd_at("cafeteria", 12.0), 0.9);
        assert_eq!(crowd_at("cafeteria", 15.0), 0.3);
        assert_eq!(crowd_at("conveni", 23.0), 0.2);
        assert_eq!(crowd_at("park", 2.0), 0.05);
        assert_eq!(crowd_at("campus_t1", 10.0), 0.6);
        assert_eq!(crowd_at("", 22.0 - 0.01), 0.1);
        assert_eq!(crowd_at("drinkshop", 14.0), 0.7);
    }

    #[test]
    fn sharing_a_dish_in_a_full_cafeteria_is_fine() {
        let t = Tuning::builtin().places;
        let hall = t.norms("cafeteria");
        let crowd = crowd_at("cafeteria", 12.0);
        assert!(awkwardness(hall, crowd, t.act("share_food")) < 0.3, "everyone eats there");
        assert!(awkwardness(hall, crowd, t.act("feed")) > awkwardness(hall, crowd, t.act("share_food")), "feeding her still draws eyes");
    }

    #[test]
    fn singing_to_him_fits_a_quiet_park_evening_not_a_library_morning() {
        let t = Tuning::builtin().places;
        let sing = t.act("sing");
        assert_eq!(sing.name, "唱歌給他聽");
        let park = awkwardness(t.norms("park"), crowd_at("park", 20.0), sing);
        assert!(matches!(react(park, 50, 50, "friend"), Reaction::Fine | Reaction::Thrilled), "{park}");
        let library = awkwardness(t.norms("library"), crowd_at("library", 10.0), sing);
        assert!(!matches!(react(library, 50, 50, "lover"), Reaction::Fine | Reaction::Thrilled), "{library}");
    }
    #[test]
    fn kissing_in_the_library_is_awkward() {
        let t = Tuning::builtin().places;
        let lib = t.norms("library");
        let a = awkwardness(lib, crowd_at("library", 10.0), t.act("kiss"));
        // unmet: privacy 0.6, crowd (0.6-0.2)*1.5=0.6, formality 0.09 -> 1.29 * 0.9 * 1.3
        assert!((a - 1.29 * 0.9 * 1.3).abs() < 1e-5, "{a}");
        assert_eq!(react(a, 50, 50, "stranger"), Reaction::Refuse);
        assert_eq!(react(0.1, 50, 50, "stranger"), Reaction::Fine);
        assert_eq!(react(1.0, 50, 80, "friend"), Reaction::Thrilled);
        assert_eq!(t.norms("nowhere").label, "街上");
        assert_eq!(t.act("unknown").name, "互動");
    }
}
