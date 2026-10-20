//! A heroine's traits (her profile's "traits" block, ok-mind profile.rs): each 0..100, rounded like Unreal did.
//! Requests carry her whole profile; rules read what they need from it.
use serde_json::Value;

#[derive(Debug, Clone, PartialEq)]
pub struct Traits {
    pub worship: i32,
    pub jealousy_prone: i32,
    pub possessiveness: i32,
    pub insecurity: i32,
    pub competitiveness: i32,
    pub self_esteem: i32,
    pub tsundere: i32,
    pub mask_axes: Vec<String>,
    pub shyness: i32,
    pub public_boldness: i32,
    pub charm_resistance: i32,
    pub money_resistance: i32,
    /// traits.difficulty when the profile has one (her stranger cap), else 0.
    pub difficulty: i32,
}

impl Default for Traits {
    fn default() -> Self {
        Traits {
            worship: 50,
            jealousy_prone: 50,
            possessiveness: 50,
            insecurity: 50,
            competitiveness: 50,
            self_esteem: 50,
            tsundere: 0,
            mask_axes: Vec::new(),
            shyness: 50,
            public_boldness: 50,
            charm_resistance: 50,
            money_resistance: 50,
            difficulty: 0,
        }
    }
}

/// Unreal's FMath::RoundToInt32 (half away from zero... RoundToInt is floor(x + 0.5)).
fn round(v: f64) -> i32 {
    (v + 0.5).floor() as i32
}

impl Traits {
    pub fn from_profile(profile: &Value) -> Traits {
        let mut t = Traits::default();
        let Some(traits) = profile.get("traits").filter(|v| v.is_object()) else { return t };
        let get = |name: &str, out: &mut i32| {
            if let Some(v) = traits.get(name).and_then(Value::as_f64).filter(|v| v.is_finite()) {
                *out = round(v).clamp(0, 100);
            }
        };
        get("worship_proneness", &mut t.worship);
        get("jealousy_proneness", &mut t.jealousy_prone);
        get("possessiveness", &mut t.possessiveness);
        get("insecurity", &mut t.insecurity);
        get("competitiveness", &mut t.competitiveness);
        get("self_esteem", &mut t.self_esteem);
        get("tsundere", &mut t.tsundere);
        get("shyness", &mut t.shyness);
        get("public_boldness", &mut t.public_boldness);
        get("charm_resistance", &mut t.charm_resistance);
        get("money_resistance", &mut t.money_resistance);
        if let Some(d) = traits.get("difficulty").and_then(Value::as_f64).filter(|v| v.is_finite()) {
            t.difficulty = d as i32;
        }
        if let Some(axes) = traits.get("mask_axes").and_then(Value::as_array) {
            t.mask_axes = axes.iter().filter_map(Value::as_str).map(String::from).collect();
        }
        t
    }
}

/// A string field of her profile (OKStreetProfileString without the nested section).
pub fn profile_str<'a>(profile: &'a Value, name: &str) -> &'a str {
    profile.get(name).and_then(Value::as_str).unwrap_or("")
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn reads_clamps_and_defaults() {
        let t = Traits::from_profile(&json!({"traits": {"shyness": 72.5, "tsundere": 140, "charm_resistance": -3, "mask_axes": ["affection"], "difficulty": 64}}));
        assert_eq!((t.shyness, t.tsundere, t.charm_resistance, t.worship, t.difficulty), (73, 100, 0, 50, 64));
        assert_eq!(t.mask_axes, vec!["affection".to_string()]);
        assert_eq!(Traits::from_profile(&json!({})), Traits::default());
    }
}
