//! OK Studio character state (the card's ChaFileStatus as a scene overrides it): tears, blush, nipples, skin gloss,
//! fluids and the accessory slots shown. A session opened under `request` applies it to the card it parses and to
//! the parts it builds; without a request nothing changes (cards keep their own look, tears stay hidden).

use std::cell::RefCell;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct StatusOverride {
    /// 0 none, 1..3 the small / medium / large tears object.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tears_lv: Option<i32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hoho_aka_rate: Option<f32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub nip_stand_rate: Option<f32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub skin_tuya_rate: Option<f32>,
    /// Fluid level 0..2 per part (face, chest front, chest back, lower front, lower back).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub siru_lv: Option<[i32; 5]>,
    /// Per accessory slot (20), shown or not; a 21st value stands for every extra slot (MoreAccessories, 20 and up).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub show_accessory: Option<Vec<bool>>,
    /// Eyes pattern (cha_eyeset row) whose eye set changes the parts: gag eyes, hidden eye base or pupils
    /// (`eye_set::EyeSet::changes_parts`; a plain blend shape pattern is set on the live face instead).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub eyes_ptn: Option<i32>,
    /// KK_InvisibleBody's Studio switch: only clothes and accessories are drawn.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub invisible_body: Option<bool>,
}

impl StatusOverride {
    pub fn is_empty(&self) -> bool { *self == Self::default() }
}

thread_local! { static REQUESTED: RefCell<Option<StatusOverride>> = const { RefCell::new(None) }; }

/// Restores the previous request when dropped.
pub struct RequestScope(Option<StatusOverride>);
impl Drop for RequestScope {
    fn drop(&mut self) { let previous = self.0.take(); REQUESTED.with(|r| *r.borrow_mut() = previous); }
}

/// Sessions opened on this thread while the scope lives use the override.
pub fn request(o: StatusOverride) -> RequestScope {
    let value = (!o.is_empty()).then_some(o);
    RequestScope(REQUESTED.with(|r| r.replace(value)))
}

/// The override of the session being opened on this thread, if any.
pub fn current() -> Option<StatusOverride> { REQUESTED.with(|r| r.borrow().clone()) }

/// Session cache identity of the override (None without one, so plain opens keep their keys).
pub fn cache_key_bytes() -> Option<Vec<u8>> { current().and_then(|o| serde_json::to_vec(&o).ok()) }

/// The card as the scene shows it.
pub fn apply(info: &mut crate::card::CardInfo) {
    let Some(o) = current() else { return; };
    if let Some(v) = o.tears_lv { info.tears_lv = v.clamp(0, 3); }
    if let Some(v) = o.hoho_aka_rate { info.hoho_aka_rate = v.clamp(0., 1.); }
    if let Some(v) = o.nip_stand_rate { info.nip_stand_rate = v.clamp(0., 1.); }
    if let Some(v) = o.skin_tuya_rate { info.skin_tuya_rate = v.clamp(0., 1.); }
    if let Some(v) = o.siru_lv { info.siru_lv = v.map(|l| l.clamp(0, 2)); }
    if let Some(v) = &o.show_accessory { info.show_accessory = v.clone(); }
    if let Some(v) = o.invisible_body { info.invisible_body = v; }
}

/// Whether the session being opened has the accessory slot switched off.
pub fn accessory_hidden(slot: usize) -> bool {
    current().and_then(|o| o.show_accessory).is_some_and(|shown| {
        let index = if slot >= 20 && shown.len() > 20 { 20 } else { slot };
        shown.get(index) == Some(&false)
    })
}

/// Tears object size (1 small, 2 medium, 3 large) of a renderer name, if it is one.
pub fn tears_size(name: &str) -> Option<i32> {
    let n = name.to_ascii_lowercase();
    if n.contains("cf_o_namida_s") { Some(1) } else if n.contains("cf_o_namida_m") { Some(2) } else if n.contains("cf_o_namida_l") { Some(3) } else { None }
}

#[cfg(test)]
mod tests {
    #[test]
    fn override_is_scoped_and_applied() {
        let card = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C002.png");
        let mut info = crate::card::parse_card(&card).unwrap();
        assert!(super::current().is_none() && super::cache_key_bytes().is_none());
        {
            let _s = super::request(super::StatusOverride { tears_lv: Some(2), hoho_aka_rate: Some(0.75), show_accessory: Some(vec![false; 20]), ..Default::default() });
            super::apply(&mut info);
            assert!(super::cache_key_bytes().is_some());
        }
        assert!(super::current().is_none(), "the scope ends the override");
        assert_eq!((info.tears_lv, info.hoho_aka_rate, info.show_accessory.iter().filter(|s| **s).count()), (2, 0.75, 0));
        let _empty = super::request(super::StatusOverride::default());
        assert!(super::current().is_none(), "an empty override is no override");
        assert_eq!(super::tears_size("cf_O_namida_M"), Some(2));
    }

    #[test]
    #[ignore = "OK base library + OK card: a session opened with a character state shows its tears and hides accessories"]
    fn session_shows_the_state() {
        let card = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C002.png");
        let visible = |s: &crate::session::OkSession| -> (Vec<String>, usize, usize) {
            let tears: Vec<String> = s.parts.iter().filter(|p| p.render_visible && super::tears_size(&p.name).is_some()).map(|p| p.name.clone()).collect();
            let acc = s.parts.iter().filter(|p| p.role.to_ascii_lowercase().contains("acc")).count();
            let acc_shown = s.parts.iter().filter(|p| p.render_visible && p.role.to_ascii_lowercase().contains("acc")).count();
            (tears, acc, acc_shown)
        };
        let plain = crate::oklib::open_test_card(&card, 0).unwrap();
        let (plain_tears, acc, acc_shown) = visible(&plain);
        let state = {
            let _s = super::request(super::StatusOverride { tears_lv: Some(2), show_accessory: Some(vec![false; 21]), ..Default::default() });
            crate::oklib::open_test_card(&card, 0).unwrap()
        };
        let (tears, _, state_acc_shown) = visible(&state);
        println!("plain: tears {plain_tears:?}, accessories {acc_shown}/{acc}; state: tears {tears:?}, accessories {state_acc_shown}");
        for p in state.parts.iter().filter(|p| p.render_visible && p.role.to_ascii_lowercase().contains("acc")).take(6) { println!("  still shown: {} {} {:?}", p.role, p.name, p.source_property); }
        assert!(plain_tears.is_empty(), "without a state the tears stay hidden");
        assert!(tears.len() == 1 && super::tears_size(&tears[0]) == Some(2), "tears level 2 shows the medium tears");
        assert_eq!(state_acc_shown, 0, "every accessory slot switched off");
    }

    /// Eyes pattern 23 (the turning swirl): the shown gag eye part carries its eye set's moving values
    /// (eye_set::apply_gag_animation) in the material the drawing reads (ok_part_material_value).
    #[test]
    #[ignore = "OK base library + OK card: a session with eyes pattern 23 gives its gag eye the animation values"]
    fn gag_eye_part_carries_its_animation() {
        let card = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG/C002.png");
        let session = {
            let _s = super::request(super::StatusOverride { eyes_ptn: Some(23), ..Default::default() });
            crate::oklib::open_test_card(&card, 0).unwrap()
        };
        let gag: Vec<_> = session.parts.iter().filter(|p| p.render_visible && p.name.to_ascii_lowercase().contains("gag_eye")).collect();
        for p in &gag {
            let m = p.source_material.as_ref();
            println!("{} shader={:?} tile={:?} angle={:?}", p.name, m.map(|m| m.shader.clone()), m.and_then(|m| m.color("_TileAnimation")), m.and_then(|m| m.float("_AngleSpeed")));
        }
        assert_eq!(gag.len(), 1, "one gag eye shown");
        assert_eq!(gag[0].source_material.as_ref().and_then(|m| m.float("_AngleSpeed")), Some(-0.5));
    }
}
