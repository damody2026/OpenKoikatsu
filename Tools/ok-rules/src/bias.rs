//! Plan §4.2a traits, §10 distortion and §10.4 tsundere: how her feelings move (the model's suggested deltas pass
//! through here), drift back each day, and colour how she reads things. The model only acts out what these produce;
//! the inner line goes into the prompt in fixed phrases.
use crate::relation::Relation;
use crate::traits::Traits;
use serde::Serialize;

pub const AXIS_STEP_PER_TURN: f32 = 5.0;

/// Emotions the model may set, and what they do to the mood.
pub const EMOTIONS: [&str; 12] = ["驚訝", "崇拜", "曖昧", "害羞", "生氣", "吃醋", "感動", "興奮", "低落", "疲倦", "嘲諷", "平常"];

/// How strongly a trait speeds up gains on an axis: 0.5x at trait 0, 1x at 50, 1.5x at 100 (plan §4.2a).
pub fn gain_scale(t: &Traits, axis: &str) -> f32 {
    let trait_value = match axis {
        "admiration" => t.worship,
        "jealousy" => t.jealousy_prone,
        "dependency" => t.insecurity,
        _ => 50,
    };
    0.5 + trait_value as f32 / 100.0
}

/// The model's rel_delta for one turn: unknown axes ignored, each at most +-5 before trait scaling, results kept in
/// 0..100. Returns what was applied ("trust+3") for the log and the notice.
pub fn apply_rel_delta(r: &mut Relation, delta: &serde_json::Map<String, serde_json::Value>, t: &Traits) -> Vec<String> {
    let mut applied = Vec::new();
    for (key, value) in delta {
        let Some(raw) = value.as_f64().filter(|v| v.is_finite() && *v != 0.0) else { continue };
        let scale = gain_scale(t, key);
        let Some(axis) = r.axis_mut(key) else { continue };
        let mut step = (raw as f32).clamp(-AXIS_STEP_PER_TURN, AXIS_STEP_PER_TURN);
        if step > 0.0 {
            step *= scale;
        }
        let before = *axis;
        *axis = (*axis + step).clamp(0.0, 100.0);
        if (*axis - before).abs() > 1e-8 {
            applied.push(format!("{key}{:+.0}", *axis - before));
        }
    }
    applied
}

/// The model's emotion for this turn, when it is one of ours ("平常" clears the mood).
pub fn set_emotion(r: &mut Relation, emotion: &str, now_minute: i64) {
    if EMOTIONS.contains(&emotion) {
        r.emotion = emotion.into();
        r.emotion_intensity = if emotion == "平常" { 0.0 } else { 0.8 };
        r.emotion_minute = now_minute;
    }
}

/// Once per game day: jealousy, resentment and mystique drift back toward their baselines; how fast depends on the
/// trait (a jealous heroine stays jealous longer). Trust and affection are anchored by memories.
pub fn decay_daily(r: &mut Relation, t: &Traits) {
    let jealousy_keep = 0.70 + 0.25 * t.jealousy_prone as f32 / 100.0;
    r.jealousy *= jealousy_keep;
    for v in r.jealousy_toward.values_mut() {
        *v *= jealousy_keep;
    }
    r.resentment *= 0.92;
    r.mystique *= 0.97;
    r.admiration *= 0.97 + 0.03 * t.worship as f32 / 100.0;
    // Her desire for him grows by itself each day, as fast as her own sex drive (libido 100: 15 a day); a girl who
    // does not care for him yet feels a third of it.
    let warm = r.stage == "lover" || r.attraction >= 30.0;
    r.sex_desire = (r.sex_desire + t.libido as f32 * 0.15 * if warm { 1.0 } else { 0.3 }).min(crate::relation::SEX_DESIRE_MAX);
}

/// 0 ordinary .. 5 deeply dependent. Feats are quotable wins she remembers.
pub fn admiration_stage(r: &Relation, feats: i32) -> i32 {
    let score = r.admiration * 0.7 + r.mystique * 0.3 + feats.min(10) as f32 * 2.0;
    ((score / 18.0).floor() as i32).clamp(0, 5)
}

/// The band of a 0–100 value in steps of 20 (0..4).
fn band(v: f32) -> usize {
    match v {
        v if v <= 20.0 => 0,
        v if v <= 40.0 => 1,
        v if v <= 60.0 => 2,
        v if v <= 80.0 => 3,
        _ => 4,
    }
}

/// Her admiration in a word, in the game's language (rules_text [bias]).
pub fn admiration_band(v: f32) -> &'static str {
    crate::tr::t(["bias.admiration_band_0", "bias.admiration_band_1", "bias.admiration_band_2", "bias.admiration_band_3", "bias.admiration_band_4"][band(v)])
}

/// Her jealousy in a word, in the game's language.
pub fn jealousy_band(v: f32) -> &'static str {
    crate::tr::t(["bias.jealousy_band_0", "bias.jealousy_band_1", "bias.jealousy_band_2", "bias.jealousy_band_3", "bias.jealousy_band_4"][band(v)])
}

/// The strongest jealousy she feels, and toward whom (empty: him).
pub fn peak_jealousy(r: &Relation) -> (f32, String) {
    let mut peak = r.jealousy;
    let mut rival = String::new();
    for (k, v) in &r.jealousy_toward {
        if *v > peak {
            peak = *v;
            rival = k.clone();
        }
    }
    (peak, rival)
}

/// Mood intensity now, after the 2-game-hour half-life.
pub fn mood_now(r: &Relation, now_minute: i64) -> f32 {
    if r.emotion.is_empty() {
        return 0.0;
    }
    let hours = (now_minute - r.emotion_minute).max(0) as f64 / 60.0;
    r.emotion_intensity * 0.5f64.powf(hours / 2.0) as f32
}

/// Low mood: a lingering sad or tired emotion (every distortion gets stronger).
pub fn low_mood(r: &Relation, mood: f32) -> bool {
    mood > 0.2 && (r.emotion == "低落" || r.emotion == "疲倦")
}

#[derive(Serialize, Debug, Clone, Default, PartialEq)]
pub struct Bias {
    pub suspicion: f32,
    pub idealize_player: f32,
    pub rival_negativity: f32,
    pub defer_to_player: f32,
    pub compete: f32,
    pub surveil: f32,
    pub possessive_display: f32,
    pub withdraw: f32,
    pub combos: Vec<String>,
    /// How much she likes him (relation liking), what a mask hides.
    pub liking: f32,
    pub shown_affection: f32,
    pub shown_admiration: f32,
    pub shown_jealousy: f32,
    pub masked: bool,
    pub inner_line: String,
    /// The mood the bias was computed with (for panels).
    pub mood: f32,
    pub peak_jealousy: f32,
    pub rival: String,
}

/// Tsundere mask factor: 1 in public, 0.6 alone, 0 when the mask breaks.
pub fn mask_factor(public: bool, mask_broken: bool) -> f32 {
    if mask_broken {
        0.0
    } else if public {
        1.0
    } else {
        0.6
    }
}

pub fn compute(r: &Relation, t: &Traits, public: bool, mask_broken: bool, mood: f32, low_mood: bool) -> Bias {
    let mut b = Bias { mood, ..Default::default() };
    let (j, rival) = peak_jealousy(r);
    let a = r.admiration;
    let amp = if low_mood { 1.3 } else { 1.0 };
    let over = |v: f32, from: f32| ((v - from) / (100.0 - from)).clamp(0.0, 1.0);
    b.idealize_player = (over(a, 40.0) * amp).min(1.0);
    b.defer_to_player = ((over(a, 40.0) * 0.8 + r.dependency / 100.0 * 0.4) * amp).min(1.0);
    b.suspicion = (over(j, 20.0) * (0.5 + t.jealousy_prone as f32 / 200.0) * amp).min(1.0);
    b.rival_negativity = if rival.is_empty() { 0.0 } else { (over(j, 40.0) * amp).min(1.0) };
    b.surveil = (over(j, 60.0) * (0.5 + t.possessiveness as f32 / 200.0) * amp).min(1.0);
    b.possessive_display = (over(j, 60.0) * t.possessiveness as f32 / 100.0 * amp).min(1.0);
    b.compete = (over(j, 40.0) * t.competitiveness as f32 / 100.0 * amp).min(1.0);
    b.withdraw = if j > 40.0 && t.self_esteem < 40 { (over(j, 40.0) * (1.0 - t.self_esteem as f32 / 100.0) * amp).min(1.0) } else { 0.0 };
    let mut combo = |on: bool, name: &str| {
        if on {
            b.combos.push(name.into());
        }
    };
    combo(a > 60.0 && j > 60.0, "devoted_possessive");
    combo(a > 60.0 && r.liking() < 30.0, "fan");
    combo(j > 60.0 && r.greed > 60.0, "jealous_greedy");
    combo(j > 40.0 && t.self_esteem < 40, "self_doubting");
    combo(j > 40.0 && t.competitiveness > 60, "rival_competitor");
    combo(a > 60.0 && r.resentment > 60.0, "disillusioned");
    combo(r.dependency > 60.0 && t.insecurity > 60, "clingy");
    combo(r.mystique > 60.0 && a < 40.0, "curious");
    // Tsundere: words say the opposite, actions follow the truth.
    b.liking = r.liking();
    b.shown_affection = r.liking();
    b.shown_admiration = a;
    b.shown_jealousy = j;
    let k = t.tsundere as f32 / 100.0 * mask_factor(public, mask_broken);
    let mask = |axis: &str, truth: f32, shown: &mut f32, masked: &mut bool| {
        if k > 0.0 && t.mask_axes.iter().any(|m| m == axis) && truth > 50.0 {
            *shown = truth + ((100.0 - truth) - truth) * k;
            *masked = true;
        }
    };
    let mut masked = false;
    let (mut sa, mut sad, mut sj) = (b.shown_affection, b.shown_admiration, b.shown_jealousy);
    mask("affection", r.liking(), &mut sa, &mut masked);
    mask("admiration", a, &mut sad, &mut masked);
    mask("jealousy", j, &mut sj, &mut masked);
    b.shown_affection = sa;
    b.shown_admiration = sad;
    b.shown_jealousy = sj;
    b.masked = masked;
    // The inner line: bands and combos in fixed phrases, in the game's language (rules_text [bias]).
    use crate::tr::{a as arg, m};
    let mut parts = vec![
        m("bias.admiration_line", &[("band", arg(admiration_band(a))), ("value", arg(&a))]),
        m("bias.jealousy_line", &[("band", arg(jealousy_band(j))), ("value", arg(&j)), ("rival", arg(if rival.is_empty() { "no" } else { "yes" }))]),
    ];
    let admiring = [(80.0, "bias.admiration_80"), (60.0, "bias.admiration_60"), (40.0, "bias.admiration_40")];
    if let Some((_, id)) = admiring.iter().find(|(over, _)| a > *over) {
        parts.push(crate::tr::t(id).into());
    }
    let jealous = [(80.0, "bias.jealousy_80"), (60.0, "bias.jealousy_60"), (40.0, "bias.jealousy_40"), (20.0, "bias.jealousy_20")];
    if let Some((_, id)) = jealous.iter().find(|(over, _)| j > *over) {
        parts.push(crate::tr::t(id).into());
    }
    for c in &b.combos {
        let id = match c.as_str() {
            "devoted_possessive" => "bias.combo_devoted_possessive",
            "fan" => "bias.combo_fan",
            "jealous_greedy" => "bias.combo_jealous_greedy",
            "self_doubting" => "bias.combo_self_doubting",
            "rival_competitor" => "bias.combo_rival_competitor",
            "disillusioned" => "bias.combo_disillusioned",
            "clingy" => "bias.combo_clingy",
            "curious" => "bias.combo_curious",
            _ => continue,
        };
        parts.push(crate::tr::t(id).into());
    }
    if b.masked {
        parts.push(m("bias.masked", &[("liking", arg(&r.liking())), ("public", arg(if public { "yes" } else { "no" })), ("shown", arg(&b.shown_affection))]));
    }
    if mood > 0.2 && !r.emotion.is_empty() {
        parts.push(m("bias.mood", &[("emotion", arg(r.emotion.as_str()))]));
    }
    b.inner_line = parts.join(crate::tr::t("bias.separator"));
    b.peak_jealousy = j;
    b.rival = rival;
    b
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn rel() -> Relation {
        Relation { id: "a".into(), ..Default::default() }
    }

    #[test]
    fn rel_delta_caps_scales_and_clamps() {
        let mut r = rel();
        let t = Traits { worship: 100, ..Default::default() };
        let d = json!({"admiration": 9, "trust": -2, "unknown": 5, "respect": 0, "affection": "x"});
        let applied = apply_rel_delta(&mut r, d.as_object().unwrap(), &t);
        assert_eq!(r.admiration, 7.5, "+5 capped, x1.5 for a worshipper");
        assert_eq!(r.trust, 18.0);
        assert_eq!(applied, vec!["admiration+8".to_string(), "trust-2".to_string()]);
        r.resentment = 0.0;
        assert!(apply_rel_delta(&mut r, json!({"resentment": -3}).as_object().unwrap(), &t).is_empty(), "already 0");
    }

    #[test]
    fn emotions_are_whitelisted() {
        let mut r = rel();
        set_emotion(&mut r, "暴怒", 100);
        assert!(r.emotion.is_empty());
        set_emotion(&mut r, "吃醋", 100);
        assert_eq!((r.emotion.as_str(), r.emotion_intensity, r.emotion_minute), ("吃醋", 0.8, 100));
        assert!((mood_now(&r, 220) - 0.4).abs() < 1e-6, "two hours: half");
        set_emotion(&mut r, "平常", 300);
        assert_eq!(r.emotion_intensity, 0.0);
    }

    #[test]
    fn daily_decay_depends_on_traits() {
        let mut r = Relation { jealousy: 100.0, resentment: 50.0, mystique: 100.0, admiration: 100.0, ..rel() };
        r.jealousy_toward.insert("b".into(), 80.0);
        decay_daily(&mut r, &Traits { jealousy_prone: 100, worship: 0, ..Default::default() });
        assert!((r.jealousy - 95.0).abs() < 1e-4 && (r.jealousy_toward["b"] - 76.0).abs() < 1e-4);
        assert!((r.resentment - 46.0).abs() < 1e-4 && (r.mystique - 97.0).abs() < 1e-4 && (r.admiration - 97.0).abs() < 1e-4);
    }

    #[test]
    fn her_desire_grows_daily_with_her_libido() {
        let mut keen = Relation { attraction: 50.0, ..rel() };
        let mut cool = keen.clone();
        decay_daily(&mut keen, &Traits { libido: 100, ..Default::default() });
        decay_daily(&mut cool, &Traits { libido: 20, ..Default::default() });
        assert!((keen.sex_desire - 15.0).abs() < 1e-4 && (cool.sex_desire - 3.0).abs() < 1e-4, "{} {}", keen.sex_desire, cool.sex_desire);
        let mut stranger = Relation { attraction: 0.0, ..rel() };
        decay_daily(&mut stranger, &Traits { libido: 100, ..Default::default() });
        assert!((stranger.sex_desire - 4.5).abs() < 1e-4, "not warm to him: a third");
    }

    #[test]
    fn stage_and_bands() {
        let r = Relation { admiration: 90.0, mystique: 60.0, ..rel() };
        assert_eq!(admiration_stage(&r, 0), 4);
        assert_eq!(admiration_stage(&r, 10), 5);
        assert_eq!((admiration_band(20.0), admiration_band(61.0), jealousy_band(100.0)), ("普通", "崇拜", "失控"));
    }

    #[test]
    fn bias_combos_mask_and_line() {
        let mut r = Relation { admiration: 70.0, stage: "lover".into(), ..rel() }.with_liking(80.0);
        r.jealousy_toward.insert("rival".into(), 70.0);
        let t = Traits { tsundere: 100, mask_axes: vec!["affection".into()], possessiveness: 100, ..Default::default() };
        let b = compute(&r, &t, true, false, 0.0, false);
        assert!(b.combos.contains(&"devoted_possessive".to_string()));
        assert!(b.masked);
        assert!((b.shown_affection - 20.0).abs() < 1e-4, "80 shown as 20 in public");
        assert!(b.rival_negativity > 0.0 && b.surveil > 0.0);
        assert!(b.inner_line.contains("對象是另一個女生") && b.inner_line.contains("嘴硬"));
        let en = {
            let _s = crate::tr::scope("en", "US");
            compute(&r, &t, true, false, 0.0, false).inner_line
        };
        assert!(en.contains("toward another girl") && en.contains("in front of others") && !en.contains('；'), "{en}");
        let alone = compute(&r, &t, false, true, 0.0, false);
        assert!(!alone.masked, "mask broken");
    }
}
