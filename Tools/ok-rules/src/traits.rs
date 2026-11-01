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
    /// traits.yandere (病嬌), or the card's own personality 病嬌 (profile personality 24).
    pub yandere: bool,
    /// traits.libido (her own sex drive): how fast her desire for him (relation sex_desire) grows. A profile made
    /// before it had one gets a fixed value from her name (20..80), the same every time. The profile's libido_gain
    /// (what she gained past ten climaxes a day, hexp.rs) is added on top.
    pub libido: i32,
    /// traits.vagina_fragile (陰部脆弱): she bleeds a little after a long session (hexp.rs discharge).
    pub vagina_fragile: bool,
    /// traits.hymen_regen (處女膜再生): her hymen grows back, so she bleeds as the first time each scene; traits.
    /// hymen_stretched: it is already stretched, no blood the first time. Both from her card (KK_SkinEffects), set
    /// on her profile by the street (OKStreetApplyHerExtras).
    pub hymen_regen: bool,
    pub hymen_stretched: bool,
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
            yandere: false,
            libido: 50,
            vagina_fragile: false,
            hymen_regen: false,
            hymen_stretched: false,
        }
    }
}

/// Unreal's FMath::RoundToInt32 (half away from zero... RoundToInt is floor(x + 0.5)).
fn round(v: f64) -> i32 {
    (v + 0.5).floor() as i32
}

impl Traits {
    pub fn from_profile(profile: &Value) -> Traits {
        let mut t = Traits::authored(profile);
        if let Some(g) = profile.get("libido_gain").and_then(Value::as_f64).filter(|v| v.is_finite()) {
            t.libido = (t.libido + round(g)).clamp(0, 100);
        }
        t
    }

    fn authored(profile: &Value) -> Traits {
        let mut t = Traits::default();
        // Koikatsu's personality 24 is 病嬌 (the card's own, when the request carries it).
        t.yandere = profile.get("personality").and_then(Value::as_i64) == Some(24);
        let name = profile_str(profile, "display_name");
        if !name.is_empty() {
            // FNV-1a of her name: stable across runs and builds.
            let hash = name.bytes().fold(0x811c9dc5u32, |h, b| (h ^ b as u32).wrapping_mul(0x01000193));
            t.libido = 20 + (hash % 61) as i32;
        }
        let Some(traits) = profile.get("traits").filter(|v| v.is_object()) else { return t };
        if traits.get("yandere").and_then(Value::as_bool).unwrap_or(false) { t.yandere = true; }
        let flag = |name: &str| traits.get(name).and_then(Value::as_bool).unwrap_or(false);
        t.vagina_fragile = flag("vagina_fragile");
        t.hymen_regen = flag("hymen_regen");
        t.hymen_stretched = flag("hymen_stretched");
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
        get("libido", &mut t.libido);
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
    #[test]
    fn libido_is_hers_and_fixed() {
        let a = super::Traits::from_profile(&serde_json::json!({"display_name": "惠惠"}));
        let b = super::Traits::from_profile(&serde_json::json!({"display_name": "惠惠", "traits": {"shyness": 70}}));
        assert_eq!(a.libido, b.libido, "the same girl, the same drive");
        assert!((20..=80).contains(&a.libido));
        let set = super::Traits::from_profile(&serde_json::json!({"display_name": "惠惠", "traits": {"libido": 95}}));
        assert_eq!(set.libido, 95, "a profile's own value wins");
        let gained = super::Traits::from_profile(&serde_json::json!({"traits": {"libido": 95}, "libido_gain": 3}));
        assert_eq!(gained.libido, 98, "what she gained adds to it");
        let capped = super::Traits::from_profile(&serde_json::json!({"traits": {"libido": 99}, "libido_gain": 7}));
        assert_eq!(capped.libido, 100);
    }

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
