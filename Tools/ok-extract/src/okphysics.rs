//! Per-component physics of an OK card (`okPh` PNG chunk, Docs/PER_COMPONENT_PHYSICS_PLAN.md).
//!
//! Every DynamicBone chain belongs to a component (hair part, accessory slot, the clothes, the body). A card may name
//! the host solver of any component; components it does not name use their default (`PhysicsComponent::default_solver`:
//! hair, breasts and hips the game's own DynamicBone, everything else PhysX). Original cards carry no `okPh`.
//!
//! ```json
//! {"format":"ok-physics","version":1,"parts":{"hair.front":{"solver":"dynbone"},"outfit0.accessory20":{"solver":"physx"}}}
//! ```
//! Keys: `hair.back|front|side|other`, an accessory's card property (`outfit0.accessory20`), `clothes` (every garment)
//! and `body` (breasts and hips). Unknown solver names (or ones this build lacks) fall back to the default.
use crate::px::{PhysicsComponent, Solver};
use std::collections::BTreeMap;

pub const CHUNK: &[u8; 4] = b"okPh";
const FORMAT: &str = "ok-physics";
const VERSION: u64 = 1;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CardPhysics {
    /// Component key -> solver name as written in the card.
    pub parts: BTreeMap<String, String>,
}

impl CardPhysics {
    pub fn parse(bytes: &[u8]) -> Result<CardPhysics, String> {
        let v: serde_json::Value = serde_json::from_slice(bytes).map_err(|e| format!("okPh: {e}"))?;
        if v.get("format").and_then(|f| f.as_str()) != Some(FORMAT) { return Err("okPh: not an ok-physics block".into()); }
        let version = v.get("version").and_then(|n| n.as_u64()).unwrap_or(0);
        if version == 0 || version > VERSION { return Err(format!("okPh: version {version} (supported {VERSION})")); }
        let mut parts = BTreeMap::new();
        if let Some(map) = v.get("parts").and_then(|p| p.as_object()) {
            for (key, part) in map {
                if let Some(solver) = part.get("solver").and_then(|s| s.as_str()) { parts.insert(key.clone(), solver.to_owned()); }
            }
        }
        Ok(CardPhysics { parts })
    }
    pub fn to_json(&self) -> Vec<u8> {
        let parts: serde_json::Map<String, serde_json::Value> =
            self.parts.iter().map(|(k, s)| (k.clone(), serde_json::json!({ "solver": s }))).collect();
        serde_json::to_vec(&serde_json::json!({ "format": FORMAT, "version": VERSION, "parts": parts })).unwrap_or_default()
    }
    /// The card's `okPh`; empty when it has none or it cannot be read (logged).
    pub fn read_card(bytes: &[u8]) -> CardPhysics {
        let Some(chunk) = crate::oklib::png_chunk(bytes, CHUNK) else { return CardPhysics::default() };
        CardPhysics::parse(chunk).unwrap_or_else(|e| { eprintln!("ok-extract: {e}"); CardPhysics::default() })
    }
    /// The card's own choice for a part (None: not named, or an unknown solver name).
    pub fn card_choice(&self, key: Option<&str>) -> Option<Solver> { key.and_then(|k| self.parts.get(k)).and_then(|n| Solver::from_name(n)) }
    /// The solver of one component: the card's choice, else the component default.
    pub fn solver(&self, key: Option<&str>, component: PhysicsComponent) -> Solver {
        let named = key.and_then(|k| self.parts.get(k));
        match named.map(|n| Solver::from_name(n)) {
            Some(Some(solver)) => solver,
            Some(None) => {
                eprintln!("ok-extract: okPh {}: unknown solver {:?}, using {}", key.unwrap_or(""), named.unwrap(), component.default_solver().name());
                component.default_solver()
            }
            None => component.default_solver(),
        }
    }
}

/// Card key of a session part (None: the part has no key of its own and uses its component default).
pub fn part_key(role: &str, source_property: Option<&str>) -> Option<String> {
    match role {
        "hair_b" => Some("hair.back".into()),
        "hair_f" => Some("hair.front".into()),
        "hair_s" => Some("hair.side".into()),
        "hair_o" => Some("hair.other".into()),
        r if r.starts_with("clothes") => Some(CLOTHES.into()),
        // The session records the exact card property (`outfit0.accessory1.ChaFileAccessory.PartsInfo.id`); the card key
        // is the slot (`outfit0.accessory1`).
        _ => source_property.filter(|p| !p.is_empty()).map(|p| {
            let mut parts = p.split('.');
            match (parts.next(), parts.next()) {
                (Some(outfit), Some(slot)) if outfit.starts_with("outfit") && slot.starts_with("accessory") => format!("{outfit}.{slot}"),
                _ => p.to_owned(),
            }
        }),
    }
}
pub const CLOTHES: &str = "clothes";
pub const BODY: &str = "body";

/// The card with `physics` as its `okPh` chunk (replacing an earlier one; an empty block removes it).
pub fn write_card(card: &[u8], physics: &CardPhysics) -> Result<Vec<u8>, String> {
    if card.len() < 8 || &card[..8] != b"\x89PNG\r\n\x1a\n" { return Err("not a PNG card".into()); }
    let mut out = card[..8].to_vec();
    let mut pos = 8usize;
    while pos + 12 <= card.len() {
        let len = u32::from_be_bytes(card[pos..pos + 4].try_into().unwrap()) as usize;
        let end = pos.checked_add(12 + len).filter(|e| *e <= card.len()).ok_or("truncated PNG chunk")?;
        let kind = &card[pos + 4..pos + 8];
        if kind == b"IEND" {
            if !physics.parts.is_empty() {
                let payload = physics.to_json();
                out.extend_from_slice(&(payload.len() as u32).to_be_bytes());
                let mut chunk = CHUNK.to_vec();
                chunk.extend_from_slice(&payload);
                out.extend_from_slice(&chunk);
                out.extend_from_slice(&crc32fast::hash(&chunk).to_be_bytes());
            }
            out.extend_from_slice(&card[pos..]);
            return Ok(out);
        }
        if kind != CHUNK { out.extend_from_slice(&card[pos..end]); }
        pos = end;
    }
    Err("missing PNG IEND".into())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn png() -> Vec<u8> {
        let mut v = b"\x89PNG\r\n\x1a\n".to_vec();
        v.extend_from_slice(&0u32.to_be_bytes());
        v.extend_from_slice(b"IEND");
        v.extend_from_slice(&crc32fast::hash(b"IEND").to_be_bytes());
        v.extend_from_slice(b"KoiKatuChara...");
        v
    }

    #[test]
    fn defaults_hair_and_body_dynamicbone_rest_physx() {
        let none = CardPhysics::default();
        assert_eq!(none.solver(part_key("hair_f", None).as_deref(), PhysicsComponent::from_role("hair_f")), Solver::DynamicBone);
        assert_eq!(none.solver(Some(BODY), PhysicsComponent::Body), Solver::DynamicBone);
        assert_eq!(none.solver(Some(CLOTHES), PhysicsComponent::from_role("clothes_bot")), Solver::PhysX);
        assert_eq!(none.solver(Some("outfit0.accessory20"), PhysicsComponent::from_role("accessory")), Solver::PhysX);
    }

    #[test]
    fn card_choice_overrides_and_unknown_falls_back() {
        let mut p = CardPhysics::default();
        p.parts.insert("hair.front".into(), "physx".into());
        p.parts.insert("outfit0.accessory20".into(), "DynBone".into());
        p.parts.insert(CLOTHES.into(), "bullet".into());
        assert_eq!(p.solver(Some("hair.front"), PhysicsComponent::Hair), Solver::PhysX);
        assert_eq!(p.solver(Some("outfit0.accessory20"), PhysicsComponent::Accessory), Solver::DynamicBone);
        assert_eq!(p.solver(Some(CLOTHES), PhysicsComponent::Clothes), Solver::PhysX);
    }

    #[test]
    fn chunk_round_trips_and_keeps_trailing_card_data() {
        let mut p = CardPhysics::default();
        p.parts.insert("hair.back".into(), "kawaii".into());
        let card = write_card(&png(), &p).unwrap();
        assert!(card.ends_with(b"KoiKatuChara..."));
        assert_eq!(CardPhysics::read_card(&card), p);
        // Rewritten, not duplicated; an empty block removes it; the OK chunk strip keeps it.
        p.parts.insert("body".into(), "physx".into());
        let again = write_card(&card, &p).unwrap();
        assert_eq!(CardPhysics::read_card(&again), p);
        assert_eq!(again.windows(4).filter(|w| w == CHUNK).count(), 1);
        assert_eq!(CardPhysics::read_card(&crate::oklib::strip_ok_chunks(&again).unwrap()), p);
        assert_eq!(write_card(&again, &CardPhysics::default()).unwrap(), png());
    }

    #[test]
    fn rejects_other_formats_and_newer_versions() {
        assert!(CardPhysics::parse(br#"{"format":"oklib-card","version":1}"#).is_err());
        assert!(CardPhysics::parse(br#"{"format":"ok-physics","version":9,"parts":{}}"#).is_err());
        assert!(CardPhysics::parse(br#"{"format":"ok-physics","version":1,"parts":{"x":{"solver":"physx","weight":3}}}"#).is_ok());
    }
}

/// `ok-extract . --okphysics <card> [key=solver ...]`: prints the card's per-component solvers; `key=solver` sets one,
/// `key=` clears it (back to the component default). The card is rewritten in place.
pub fn cli(args: &[String]) -> Result<(), String> {
    let card = std::path::PathBuf::from(args.first().ok_or("usage: --okphysics <card> [key=solver ...]")?);
    let bytes = std::fs::read(&card).map_err(|e| format!("{}: {e}", card.display()))?;
    let mut physics = CardPhysics::read_card(&bytes);
    if args.len() > 1 {
        for set in &args[1..] {
            let (key, solver) = set.split_once('=').ok_or_else(|| format!("{set}: expected key=solver"))?;
            if solver.is_empty() { physics.parts.remove(key); continue; }
            let solver = Solver::from_name(solver).ok_or_else(|| format!("{solver}: unknown solver (physx, kawaii, chaos, jolt, mujoco, dynbone)"))?;
            physics.parts.insert(key.to_owned(), solver.name().to_owned());
        }
        std::fs::write(&card, write_card(&bytes, &physics)?).map_err(|e| format!("{}: {e}", card.display()))?;
    }
    for (key, solver) in &physics.parts { println!("{key}={solver}"); }
    Ok(())
}
