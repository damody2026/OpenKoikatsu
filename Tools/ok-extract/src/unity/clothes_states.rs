//! Garment states of an open: the requested and the session's resolved states (thread scopes) and which garments and state nodes show.
use super::*;

thread_local! {
    pub(crate) static ACTIVE_CLOTHES_STATES: std::cell::Cell<Option<[u8; 9]>> = const { std::cell::Cell::new(None) };
    pub(crate) static REQUESTED_CLOTHES_STATES: std::cell::Cell<Option<[u8; 9]>> = const { std::cell::Cell::new(None) };
}

pub struct RequestedClothesScope(Option<[u8;9]>);
impl Drop for RequestedClothesScope {
    fn drop(&mut self){REQUESTED_CLOTHES_STATES.with(|cell|cell.set(self.0));}
}
pub fn request_clothes_states(states:[u8;9])->RequestedClothesScope {
    RequestedClothesScope(REQUESTED_CLOTHES_STATES.with(|cell|cell.replace(Some(states))))
}
pub fn explicit_requested_clothes_states()->Option<[u8;9]>{REQUESTED_CLOTHES_STATES.with(|cell|cell.get())}

/// A session owns its resolved `fileStatus.clothesState`. Keep it scoped so a
/// later card in the same process cannot inherit this card's state types.
pub struct ClothesStatesScope(Option<[u8; 9]>);

impl Drop for ClothesStatesScope {
    fn drop(&mut self) {
        ACTIVE_CLOTHES_STATES.with(|cell| cell.set(self.0));
    }
}

pub fn set_session_clothes_states(states: [u8; 9]) -> ClothesStatesScope {
    ClothesStatesScope(ACTIVE_CLOTHES_STATES.with(|cell| cell.replace(Some(states))))
}

/// Parse the requested runtime state; `set_session_clothes_states` applies the
/// card's StateType and linked-garment rules before prefab selection.
pub fn requested_clothes_states() -> [u8; 9] {
    if let Some(states)=REQUESTED_CLOTHES_STATES.with(|cell|cell.get()){return states;}
    let mut states = [0u8; 9];
    if let Ok(raw) = std::env::var("OK_CLOTHES_STATES") {
        for (i, value) in raw.split(',').take(9).enumerate() {
            if let Ok(state) = value.trim().parse::<u8>() {
                states[i] = state.min(3);
            }
        }
    }
    states
}

pub(crate) fn active_clothes_states() -> Option<[u8; 9]> { ACTIVE_CLOTHES_STATES.with(|cell| cell.get()) }
pub fn clothes_states() -> [u8; 9] {
    ACTIVE_CLOTHES_STATES.with(|cell| cell.get().unwrap_or_else(requested_clothes_states))
}

/// ChaFileStatus.MemberInit uses outdoor shoes (1). Studio card import does
/// not load saved Status; scene/runtime controls can explicitly select 0/1.
pub fn requested_shoes_type() -> u8 {
    std::env::var("OK_SHOES_TYPE").ok().and_then(|value| value.parse().ok()).unwrap_or(1)
}

/// Object-level visibility in ChaControl.DrawClothes, before switching the
/// DEF/NUGE child nodes. Gloves, socks and shoes have no loosened object;
/// pantyhose has only DEF and NUGE.
pub fn clothes_kind_visible(kind: usize, states: &[u8; 9], shoes_type: u8) -> bool {
    match kind {
        0..=3 => states[kind] != 3,
        4 | 6 => states[kind] == 0,
        7 => shoes_type == 0 && states[kind] == 0,
        8 => shoes_type == 1 && states[kind] == 0,
        5 => states[kind] <= 1,
        _ => false,
    }
}

pub(crate) fn clothes_state_node_visible(name: &str, states: &[u8; 9], clothes_category: Option<i32>) -> Option<bool> {
    let (prefix, suffix) = name.rsplit_once('_')?;
    let variant = match suffix.to_ascii_lowercase().as_str() {
        "a" => 0, "b" => 1, "c" => 2, _ => return None,
    };
    let prefix = prefix.to_ascii_lowercase();
    let kind = match prefix.as_str() {
        "n_top" | "o_inner" => 0,
        "n_bot" => 1,
        "n_uwt" | "o_bra" => 2,
        "n_uwb" => 3,
        "n_panst" => 5,
        "n_socks" => 6,
        "n_shoes_inner" => 7,
        "n_shoes_outer" | "n_shoes" => 8,
        _ => return None,
    };
    // Bool8.Any(n) in the decompiled DrawClothes is an AND. Main top/bottom
    // and bra variants each contain a region controlled by the adjacent
    // garment's state; top subparts instead use topPartsKey[top_state].
    let selected = match (clothes_category, prefix.as_str()) {
        (Some(200..=202 | 210..=212), _) => states[0] == variant,
        (Some(105), "n_top" | "o_inner") => states[0] == variant,
        (Some(105), "n_bot") => states[0] != 3 && match variant {
            0 => states[1] == 0, 1 => states[1] != 0, _ => false,
        },
        (Some(106), "n_bot") => states[1] == variant,
        (Some(106), "n_top") => states[1] != 2 && match variant {
            0 => states[0] == 0, 1 => states[0] != 0, _ => false,
        },
        (Some(107), "n_top" | "n_uwt" | "o_bra") => states[2] == variant,
        (Some(107), "n_bot") => states[2] != 3 && match variant {
            0 => states[3] == 0, 1 => states[3] != 0, _ => false,
        },
        (Some(108), "n_bot" | "n_uwb") => states[3] == variant,
        (Some(105..=112), _) => states[(clothes_category.unwrap() - 105) as usize] == variant,
        _ => states[kind] == variant,
    };
    Some(selected)
}
