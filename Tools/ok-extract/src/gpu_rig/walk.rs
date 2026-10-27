//! How fast a girl's walk carries her (the crowd room moves her at it, so her feet do not slide): her own skeleton
//! played through an in-place walk clip at 60 Hz; while a foot carries her it is still on the ground, so relative to
//! her hips it moves back at her speed. Her legs' length (body shape) sets the stride, so every girl has her own speed.

/// Metres per second along her facing (Unity +Z), or None without both feet or a stance to read.
pub fn walk_speed(s: &mut crate::session::OkSession, clip: &[u8]) -> Option<f32> {
    let (root, key) = crate::anim::install_shared_clip(clip).ok()?;
    s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
    let duration = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
    if !(duration > 0.1) { return None; }
    let id = |s: &crate::session::OkSession, names: &[&str]| -> Option<i64> {
        let body = s.body_skel.as_ref()?;
        names.iter().find_map(|n| body.tfs.iter().find(|(_, tf)| tf.name == *n).map(|(id, _)| *id))
    };
    let feet = [id(s, &["cf_j_toes_L", "cf_j_foot_L"])?, id(s, &["cf_j_toes_R", "cf_j_foot_R"])?];
    let hips = id(s, &["cf_j_hips"])?;
    let frames = (duration * 60.).round().max(2.) as usize;
    // Per frame: each foot's height and its forward position relative to the hips.
    let mut track: Vec<[(f32, f32); 2]> = Vec::with_capacity(frames + 1);
    for k in 0..=frames {
        s.sample_paired_frame(duration * k as f32 / frames as f32);
        let body = s.body_skel.as_ref()?;
        let h = crate::pose::world_of_id(hips, &body.tfs);
        let mut at = [(0f32, 0f32); 2];
        for (f, foot) in feet.iter().enumerate() { let w = crate::pose::world_of_id(*foot, &body.tfs); at[f] = (w[1][3], w[2][3] - h[2][3]); }
        track.push(at);
    }
    s.sample_paired_frame(0.);
    // A foot carries her while it is the lower one and near its lowest: its backward speed then.
    let low: [f32; 2] = [0, 1].map(|f| track.iter().map(|t| t[f].0).fold(f32::MAX, f32::min));
    let dt = duration / frames as f32;
    let mut speeds: Vec<f32> = Vec::new();
    for k in 0..frames {
        let (a, b) = (track[k], track[k + 1]);
        let f = if a[0].0 <= a[1].0 { 0 } else { 1 };
        if a[f].0 > low[f] + 0.01 || b[f].0 > low[f] + 0.01 { continue; }
        speeds.push(-(b[f].1 - a[f].1) / dt);
    }
    if speeds.len() < 4 { return None; }
    speeds.sort_by(|x, y| x.total_cmp(y));
    let speed = speeds[speeds.len() / 2];
    (speed > 0.05 && speed < 5.).then_some(speed)
}

/// The walk speed of a session in metres per second (walk_speed), or a negative value when it cannot be read; the
/// session is left posed at the clip's start.
#[no_mangle]
pub unsafe extern "C" fn ok_session_walk_speed(s: *mut crate::session::OkSession, clip: *const u8, len: i32) -> f32 {
    let Some(s) = s.as_mut() else { return -1. };
    if clip.is_null() || len <= 0 { return -1.; }
    walk_speed(s, std::slice::from_raw_parts(clip, len as usize)).unwrap_or(-1.)
}

#[cfg(test)]
mod tests {
    /// Every crowd card's walk speed (OK_PHYS_CARDS folder, OK_PHYS_LIMIT cards):
    ///   cargo test --release --lib gpu_rig_walk_speeds -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn gpu_rig_walk_speeds() {
        let dir = std::env::var("OK_PHYS_CARDS").unwrap_or_else(|_| "E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG".into());
        let limit: usize = std::env::var("OK_PHYS_LIMIT").ok().and_then(|v| v.parse().ok()).unwrap_or(usize::MAX);
        let mut cards: Vec<_> = std::fs::read_dir(&dir).unwrap().filter_map(|e| e.ok()).map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|x| x == "png") && !p.file_name().unwrap().to_string_lossy().starts_with('.')).collect();
        cards.sort();
        let clip = std::fs::read("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim").unwrap();
        for card in cards.iter().take(limit) {
            std::env::set_var("OK_RIG_CARD", card);
            let Ok(opened) = std::panic::catch_unwind(|| crate::gpu_rig::tests::open_with_clip_pub()) else { continue };
            let s = crate::session::OkSession::from_animation_bytes(&opened.to_animation_bytes().expect("snapshot")).expect("reopen");
            let mut s = Box::new(s);
            println!("{}: {:?} m/s", card.file_name().unwrap().to_string_lossy(), super::walk_speed(&mut s, &clip));
        }
    }
}

