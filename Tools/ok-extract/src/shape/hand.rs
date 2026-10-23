//! KKS ChaControl.UpdateAlwaysShapeHand / ShapeHandInfo: a late rotation
//! override of the authored 30 finger targets, not the legacy HandAnimeCtrl.
use super::*;

pub(crate) const TARGETS: [&str; 30] = [
    "cf_j_index01_L", "cf_j_index02_L", "cf_j_index03_L",
    "cf_j_little01_L", "cf_j_little02_L", "cf_j_little03_L",
    "cf_j_middle01_L", "cf_j_middle02_L", "cf_j_middle03_L",
    "cf_j_ring01_L", "cf_j_ring02_L", "cf_j_ring03_L",
    "cf_j_thumb01_L", "cf_j_thumb02_L", "cf_j_thumb03_L",
    "cf_j_index01_R", "cf_j_index02_R", "cf_j_index03_R",
    "cf_j_little01_R", "cf_j_little02_R", "cf_j_little03_R",
    "cf_j_middle01_R", "cf_j_middle02_R", "cf_j_middle03_R",
    "cf_j_ring01_R", "cf_j_ring02_R", "cf_j_ring03_R",
    "cf_j_thumb01_R", "cf_j_thumb02_R", "cf_j_thumb03_R",
];

#[derive(Default, Clone, serde::Serialize, serde::Deserialize)]
pub(crate) struct HandShapes {
    #[serde(serialize_with="crate::ordered::map")]
    keys: HashMap<String, Vec<Keyframe>>,
    rows: Vec<CatRow>,
    count: usize,
    enabled: [bool; 2],
    rotations: [[f32; 3]; 30],
}

// The integer stored with each frame is not its selection index. GetInfo uses
// the serialized List ordinal, and GetKeyCount uses the first serialized list.
fn read_keys(data: &[u8]) -> Option<(HashMap<String, Vec<Keyframe>>, usize)> {
    let mut pos = 0;
    let n = read_i32(data, &mut pos)?;
    if !(1..=10000).contains(&n) { return None; }
    let mut keys = HashMap::new();
    let mut first_count = 0;
    for list in 0..n {
        let name = read_dotnet_string(data, &mut pos)?;
        let count = read_i32(data, &mut pos)?;
        if !(0..=10000).contains(&count) { return None; }
        if list == 0 { first_count = count as usize; }
        let mut sequence = Vec::new();
        for _ in 0..count {
            read_i32(data, &mut pos)?;
            let mut xyz = [[0.0; 3]; 3];
            for vector in &mut xyz {
                for value in vector {
                    *value = read_f32(data, &mut pos)?;
                    if !value.is_finite() { return None; }
                }
            }
            sequence.push(Keyframe { pos: xyz[0], rot: xyz[1], scl: xyz[2] });
        }
        if keys.insert(name, sequence).is_some() { return None; }
    }
    if pos != data.len() { return None; }
    Some((keys, first_count))
}

fn angle_blend(a: f32, b: f32, blend: f32) -> f32 {
    // Mathf.LerpAngle uses Repeat then the shortest positive 180-degree tie,
    // and clamps t. Quaternion slerp or raw Euler lerp has different results.
    let delta = (b - a).rem_euclid(360.0);
    a + (if delta > 180.0 { delta - 360.0 } else { delta }) * blend.clamp(0.0, 1.0)
}

impl HandShapes {
    pub(crate) fn load(game: &Path) -> Self {
        let assets = match crate::mods::override_bundle(&game.join("mods"), "chara/oo_hand.unity3d") {
            Some(bytes) => crate::list::text_assets_from_bytes(&bytes),
            None => text_assets_from_bundle(&game.join("abdata/chara/oo_hand.unity3d")),
        };
        let key_data = assets.iter().find(|(name, _)| name == "cf_anmShapeHand");
        let category = assets.iter().find(|(name, _)| name == "cf_customhand");
        let Some((keys, count)) = key_data.and_then(|(_, data)| read_keys(data)) else { return Self::default(); };
        let Some(text) = category.and_then(|(_, data)| std::str::from_utf8(data).ok()) else { return Self::default(); };
        let rows = parse_cat(text, &TARGETS);
        if rows.is_empty() { return Self::default(); }
        let mut result = Self { keys, count, rows, ..Self::default() };
        // InitShapeHand calls SetShapeHandValue(i,0,0,0) for both sides.
        for side in 0..2 { result.set(side, false, 0, 0, 0.0); }
        result
    }

    pub(crate) fn count(&self) -> usize { self.count }

    pub(crate) fn set(&mut self, side: usize, enabled: bool, a: usize, b: usize, blend: f32) -> bool {
        if side >= 2 || !blend.is_finite() { return false; }
        if !enabled && self.count == 0 { self.enabled[side] = false; return true; }
        if a >= self.count || b >= self.count { return false; }
        // Validate before applying so a failed caller cannot half-change the
        // currently selected pose. Valid assets follow ChangeValue exactly.
        let rows: Vec<_> = self.rows.iter().filter(|row| row.category == side as i32).collect();
        if rows.is_empty() || rows.iter().any(|row| self.keys.get(&row.name)
            .is_none_or(|keys| a >= keys.len() || b >= keys.len())) { return false; }
        for row in rows {
            let keys = &self.keys[&row.name];
            for axis in 0..3 {
                if row.use_rot[axis] {
                    self.rotations[row.id as usize][axis] = if row.get[1] {
                        angle_blend(keys[a].rot[axis], keys[b].rot[axis], blend)
                    } else { 0.0 };
                }
            }
        }
        self.enabled[side] = enabled;
        true
    }

    /// `apply` with the 30 finger bones looked up once per skeleton (the key is the skeleton's bone list), local
    /// rotations only: the caller rebuilds the world matrices when this returns true.
    pub(crate) fn apply_cached_locals(&self, skel: &mut Skeleton, cache: &mut crate::session::BoneIdCache) -> bool {
        if self.enabled == [false, false] { return false; }
        use std::hash::{Hash, Hasher};
        let mut h = std::collections::hash_map::DefaultHasher::new();
        skel.bone_ids.hash(&mut h);
        let key = h.finish();
        if cache.key != key || cache.ids.len() != TARGETS.len() {
            let ids = crate::pose::animator_root_id(skel).map(|root| crate::pose::subtree_ids_pub(root, &skel.tfs))
                .unwrap_or_else(|| skel.bone_ids.clone());
            let mut by_name: HashMap<&str, i64> = HashMap::new();
            for id in ids { if let Some(tf) = skel.tfs.get(&id) { by_name.entry(tf.name.as_str()).or_insert(id); } }
            cache.ids = TARGETS.iter().map(|name| by_name.get(*name).copied()).collect();
            cache.key = key;
        }
        for (index, id) in cache.ids.iter().enumerate() {
            if !self.enabled[index / 15] { continue; }
            if let Some(tf) = id.and_then(|id| skel.tfs.get_mut(&id)) { tf.rot = crate::pose::euler_deg_to_quat(self.rotations[index]); }
        }
        true
    }

    pub(crate) fn apply(&self, skel: &mut Skeleton) {
        if self.enabled == [false, false] { return; }
        // ShapeHandInfo is initialized under the actual body Animator tree.
        // Private mod rigs with identical joint names are not another target.
        let ids = crate::pose::animator_root_id(skel).map(|root| crate::pose::subtree_ids_pub(root, &skel.tfs))
            .unwrap_or_else(|| skel.bone_ids.clone());
        let mut by_name = HashMap::new();
        for id in ids { if let Some(tf) = skel.tfs.get(&id) { by_name.entry(tf.name.clone()).or_insert(id); } }
        for (index, name) in TARGETS.iter().enumerate() {
            if !self.enabled[index / 15] { continue; }
            if let Some(tf) = by_name.get(*name).and_then(|id| skel.tfs.get_mut(id)) {
                tf.rot = crate::pose::euler_deg_to_quat(self.rotations[index]);
            }
        }
        skel.rebuild_worlds();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[ignore = "requires actual CharaStudio ShapeHandInfo probe fixture"]
    fn all_hand_shapes_match_installed_game_rotations_and_preserve_other_channels() {
        use crate::pose::{Skeleton, Tf};
        let text = crate::gamefs::read_to_string("../../Saved/Tests/HandShapes275/GameHandShapes.json.hand-shapes.tsv").unwrap();
        let mut baseline = HashMap::new();
        let mut hand = HandShapes::load(&crate::test_support::active_library());
        let mut skel = Skeleton { names: vec![], worlds: vec![], rest: vec![], smr_count: 0,
            tfs: HashMap::new(), bone_ids: vec![], source_mesh: 0 };
        let mut cases = 0;
        let mut compared = 0;
        let mut max_error = 0.0f32;
        for line in text.lines() {
            let c: Vec<_> = line.split('\t').collect();
            match c[0] {
                "count" => assert_eq!(hand.count(), c[1].parse::<usize>().unwrap()),
                "baseline" | "bone" => {
                    assert_eq!(c.len(), 13);
                    let f: Vec<f32> = c[3..].iter().map(|s| s.parse().unwrap()).collect();
                    let tf = Tf { name: c[2].into(), parent: None, sibling_index: None,
                        pos: f[0..3].try_into().unwrap(), rot: f[3..7].try_into().unwrap(), scale: f[7..10].try_into().unwrap() };
                    if c[0] == "baseline" { baseline.insert(baseline.len() as i64, tf); }
                    else {
                        let actual = skel.tfs.values().find(|a| a.name == tf.name).unwrap();
                        assert_eq!(actual.pos, tf.pos, "position {}", tf.name);
                        assert_eq!(actual.scale, tf.scale, "scale {}", tf.name);
                        let err = [-1.0f32, 1.0].into_iter().map(|sign| (0..4)
                            .map(|i| (actual.rot[i] * sign - tf.rot[i]).abs()).fold(0.0f32, f32::max))
                            .fold(f32::INFINITY, f32::min);
                        assert!(err <= 2e-5, "case {} {} quaternion error {} actual {:?} native {:?}", c[1], tf.name, err, actual.rot, tf.rot);
                        max_error = max_error.max(err); compared += 1;
                    }
                },
                "case" => {
                    assert_eq!(baseline.len(), 34);
                    skel.tfs = baseline.clone(); skel.bone_ids = (0..34).collect();
                    assert!(hand.set(0, false, 0, 0, 0.0)); assert!(hand.set(1, false, 0, 0, 0.0));
                    assert!(hand.set(c[2].parse().unwrap(), c[3] == "1", c[4].parse().unwrap(), c[5].parse().unwrap(), c[6].parse().unwrap()));
                    hand.apply(&mut skel); cases += 1;
                },
                "cases" => assert_eq!(cases, c[1].parse::<usize>().unwrap()),
                _ => panic!("unknown native fixture row"),
            }
        }
        assert_eq!(cases, 2 * (hand.count() + 5)); assert_eq!(compared, cases * 34);
        eprintln!("KKS actual hand control: patterns={} cases={} bone_comparisons={} max_quaternion_error={:.9}", hand.count(), cases, compared, max_error);
    }

    #[test]
    fn hand_angles_follow_unity_repeat_clamp_and_positive_180_tie() {
        assert_eq!(angle_blend(350.0, 10.0, 0.5), 360.0);
        assert_eq!(angle_blend(10.0, 350.0, 0.5), 0.0);
        assert_eq!(angle_blend(0.0, -180.0, 0.5), 90.0);
        assert_eq!(angle_blend(0.0, 10.0, 2.0), 10.0);
        assert_eq!(angle_blend(0.0, 10.0, -1.0), 0.0);
    }

    #[test]
    fn native_hand_asset_has_valid_both_side_categories_and_rejects_bad_selection() {
        let mut hand = HandShapes::load(&crate::test_support::active_library());
        assert!(hand.count() > 20);
        for side in 0..2 {
            for pattern in 0..hand.count() { assert!(hand.set(side, true, pattern, 0, 0.0)); }
        }
        let old = hand.rotations;
        assert!(!hand.set(2, true, 1, 0, 0.0));
        assert!(!hand.set(0, true, hand.count(), 0, 0.0));
        assert!(!hand.set(0, true, 1, 0, f32::NAN));
        assert_eq!(old, hand.rotations);
        assert!(hand.set(0, false, 0, 0, 0.0));
        assert_eq!(hand.enabled, [false, true]);
        eprintln!("KKS hand shape patterns={} targets={}", hand.count(), hand.rows.len());
    }
}
