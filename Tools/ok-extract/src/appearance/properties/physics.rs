//! Per-component physics solver rows (`kind: "choice"`, key `physics.<okPh key>`), saved in the card's `okPh` chunk
//! (crate::okphysics). Value "" = the component default.
use super::*;
use crate::okphysics::{CardPhysics, BODY, CLOTHES};
use crate::px::{PhysicsComponent, Solver};

const PREFIX: &str = "physics.";

fn solver_label(s: Solver) -> &'static str {
    match s {
        Solver::DynamicBone => "原作 DynamicBone＋碰撞",
        Solver::PhysX => "PhysX 5.11",
        Solver::Kawaii => "KawaiiPhysics",
        Solver::Chaos => "Chaos PBD",
        Solver::Jolt => "Jolt 5.6（開發版限定）",
        Solver::MuJoCo => "MuJoCo 3.15（開發版限定）",
    }
}

fn component(key: &str) -> PhysicsComponent {
    if key.starts_with("hair.") { PhysicsComponent::Hair }
    else if key == BODY { PhysicsComponent::Body }
    else if key == CLOTHES { PhysicsComponent::Clothes }
    else { PhysicsComponent::Accessory }
}

fn row(key: &str, label: &str, physics: &CardPhysics) -> J {
    let default = component(key).default_solver();
    // Accessory chains are classified one by one (head-worn = hair, tails, the rest), so the default depends on the chain.
    let default_label = if component(key) == PhysicsComponent::Accessory { format!("預設（頭上的用{}，尾巴與其他用{}）", solver_label(Solver::DynamicBone), solver_label(Solver::PhysX)) }
        else { format!("預設（{}）", solver_label(default)) };
    let mut options = vec![json!({"value": "", "label": default_label})];
    options.extend(Solver::ALL.into_iter().map(|s| json!({"value": s.name(), "label": solver_label(s)})));
    let value = physics.parts.get(key).and_then(|n| Solver::from_name(n)).map_or("", Solver::name);
    json!({"key": format!("{PREFIX}{key}"), "label": format!("物理／{label}"), "kind": "choice", "value": value, "options": options})
}

/// Accessory slots of `outfit` that hold an item (type 120 = empty slot).
fn accessory_slots(blocks: &mut BTreeMap<String, Vec<u8>>, outfit: usize) -> Vec<usize> {
    let mut slots = Vec::new();
    let _ = sections(blocks, outfit, |name, v| {
        if name != "accessory" { return Ok(()); }
        if let Ok(Value::Array(parts)) = at(v, "parts") {
            for (i, part) in parts.iter().enumerate() {
                let kind = part.as_map().and_then(|m| m.iter().find(|(k, _)| k.as_str() == Some("type"))).and_then(|(_, t)| t.as_i64());
                if kind.is_some_and(|t| t != 120) { slots.push(i); }
            }
        }
        Ok(())
    });
    slots
}

pub(super) fn catalog(original: &[u8], outfit: usize) -> Result<Vec<J>, String> {
    let physics = CardPhysics::read_card(original);
    let mut rows: Vec<J> = [("hair.back", "頭髮（後髮）"), ("hair.front", "頭髮（前髮）"), ("hair.side", "頭髮（側髮）"), ("hair.other", "頭髮（延伸）"),
        (BODY, "身體（胸部、臀部）"), (CLOTHES, "衣物（全部）")].iter().map(|(k, l)| row(k, l, &physics)).collect();
    let mut blocks = blocks_from_card(original)?;
    let mut slots = accessory_slots(&mut blocks, outfit);
    // Extra accessory slots (20+, MoreAccessories XML): the ones bound to a bone hold an item.
    let mut extra = Vec::new();
    super::plugins::catalog(&blocks, outfit, &mut extra)?;
    for r in &extra {
        let Some(key) = r["key"].as_str() else { continue };
        let Some(rest) = key.split_once(".xml.").map(|(_, rest)| rest) else { continue };
        if let Some((slot, "parentKey")) = rest.split_once('.') {
            if let (Ok(slot), Some(parent)) = (slot.parse::<usize>(), r["value"].as_str()) { if !parent.is_empty() && !slots.contains(&slot) { slots.push(slot); } }
        }
    }
    for slot in slots {
        rows.push(row(&format!("outfit{outfit}.accessory{slot}"), &format!("飾品 {}", slot + 1), &physics));
    }
    Ok(rows)
}

/// Splits `edits` into the card-data edits and the physics edits (key without the `physics.` prefix, solver name).
pub(super) fn take_edits(edits: &[J]) -> (Vec<J>, Vec<(String, String)>) {
    let mut rest = Vec::new();
    let mut physics = Vec::new();
    for e in edits {
        match e["key"].as_str().and_then(|k| k.strip_prefix(PREFIX)) {
            Some(key) => physics.push((key.to_owned(), e["value"].as_str().unwrap_or("\u{1}").to_owned())),
            None => rest.push(e.clone()),
        }
    }
    (rest, physics)
}

/// Writes the physics edits (already split off by `take_edits`) into `card`'s `okPh`.
pub(super) fn apply(original: &[u8], card: &[u8], outfit: usize, edits: &[(String, String)]) -> Result<Vec<u8>, String> {
    let rows = catalog(original, outfit)?;
    let mut physics = CardPhysics::read_card(original);
    for (key, value) in edits {
        let full = format!("{PREFIX}{key}");
        let row = rows.iter().find(|r| r["key"] == full.as_str()).ok_or("找不到可編輯欄位")?;
        if !row["options"].as_array().is_some_and(|o| o.iter().any(|o| o["value"] == value.as_str())) { return Err("物理解算器名稱錯誤".into()); }
        if value.is_empty() { physics.parts.remove(key); } else { physics.parts.insert(key.clone(), value.clone()); }
    }
    crate::okphysics::write_card(card, &physics)
}

#[cfg(test)]
mod tests {
    use super::*;
    const CARD: &str = "C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png";

    #[test]
    fn rows_cover_hair_body_clothes_and_filled_accessory_slots() {
        let original = crate::gamefs::read(CARD).unwrap();
        let rows = catalog(&original, 0).unwrap();
        for key in ["physics.hair.front", "physics.body", "physics.clothes"] { assert!(rows.iter().any(|r| r["key"] == key), "missing {key}"); }
        let front = rows.iter().find(|r| r["key"] == "physics.hair.front").unwrap();
        assert_eq!(front["value"], "");
        assert!(front["options"][0]["label"].as_str().unwrap().contains("DynamicBone"));
        let clothes = rows.iter().find(|r| r["key"] == "physics.clothes").unwrap();
        assert!(clothes["options"][0]["label"].as_str().unwrap().contains("PhysX"));
        assert!(rows.iter().all(|r| !r["key"].as_str().unwrap().contains("accessory") || r["key"].as_str().unwrap().starts_with("physics.outfit0.accessory")));
    }

    #[test]
    fn edits_write_okph_and_default_removes_it() {
        let original = crate::gamefs::read(CARD).unwrap();
        let (rest, edits) = take_edits(&[json!({"key": "physics.hair.front", "value": "physx"}), json!({"key": "hair.parts.0.noShake", "value": true})]);
        assert_eq!(rest.len(), 1);
        let card = apply(&original, &original, 0, &edits).unwrap();
        assert_eq!(CardPhysics::read_card(&card).parts.get("hair.front").map(String::as_str), Some("physx"));
        assert_eq!(catalog(&card, 0).unwrap().iter().find(|r| r["key"] == "physics.hair.front").unwrap()["value"], "physx");
        let back = apply(&card, &card, 0, &[("hair.front".into(), String::new())]).unwrap();
        assert!(CardPhysics::read_card(&back).parts.is_empty());
        assert!(apply(&original, &original, 0, &[("hair.front".into(), "bullet".into())]).is_err());
        assert!(apply(&original, &original, 0, &[("outfit0.accessory99".into(), "physx".into())]).is_err());
    }
}
