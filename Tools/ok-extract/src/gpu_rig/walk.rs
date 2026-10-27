//! How fast a girl's walk carries her, frame by frame (the crowd room moves her at it, so her feet do not slide): her own
//! skeleton played through an in-place walk clip at 60 Hz. The foot that is down carries her: in her root space it moves
//! back at her speed. Her legs' length (body shape) sets the stride, so every girl has her own; and the speed changes
//! through the step (it is not one number: at a constant speed the planted foot still slides 15 cm/s, Docs/CROWD_ROOM100.md).
//! Where that foot is matters too: a girl who turns while she walks turns about it, not about her root.

/// One loop of her walk at 60 Hz (value k: from k/60 to (k+1)/60 s): her speed along her facing (Unity +Z, root space,
/// m/s, smoothed over 5 frames) and the planted foot's ball (cf_j_toes of the side whose ankle or toes is lowest; root
/// space x, z in metres) at k/60.
pub struct WalkCurve { pub speed: Vec<f32>, pub pivot: Vec<[f32; 2]> }

/// Her walk curve, or None without both feet.
pub fn walk_curve(s: &mut crate::session::OkSession, clip: &[u8]) -> Option<WalkCurve> {
    let (root, key) = crate::anim::install_shared_clip(clip).ok()?;
    s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
    let duration = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
    if !(duration > 0.1) { return None; }
    let id = |s: &crate::session::OkSession, name: &str| -> Option<i64> {
        s.body_skel.as_ref()?.tfs.iter().find(|(_, tf)| tf.name == name).map(|(id, _)| *id)
    };
    let points = [id(s, "cf_j_foot_L")?, id(s, "cf_j_toes_L")?, id(s, "cf_j_foot_R")?, id(s, "cf_j_toes_R")?];
    let frames = (duration * 60.).round().max(2.) as usize;
    // Per frame: each contact point's height, sideways and forward position in her root space (the body skeleton's worlds).
    let mut track: Vec<[[f32; 3]; 4]> = Vec::with_capacity(frames + 1);
    for k in 0..=frames {
        s.sample_paired_frame((k as f32 / 60.).min(duration));
        let body = s.body_skel.as_ref()?;
        let mut at = [[0f32; 3]; 4];
        for (p, point) in points.iter().enumerate() { let w = crate::pose::world_of_id(*point, &body.tfs); at[p] = [w[1][3], w[0][3], w[2][3]]; }
        track.push(at);
    }
    s.sample_paired_frame(0.);
    let lowest = |a: &[[f32; 3]; 4]| (0..4).min_by(|x, y| a[*x][0].total_cmp(&a[*y][0])).unwrap();
    let mut raw = Vec::with_capacity(frames);
    let mut pivot = Vec::with_capacity(frames);
    for k in 0..frames {
        let (a, b) = (track[k], track[k + 1]);
        let down = lowest(&a);
        let dt = ((k + 1) as f32 / 60.).min(duration) - k as f32 / 60.;
        raw.push(if dt > 1e-4 { -(b[down][2] - a[down][2]) / dt } else { 0. });
        let toes = a[down / 2 * 2 + 1];
        pivot.push([toes[1], toes[2]]);
    }
    let speed: Vec<f32> = (0..frames).map(|k| (0..5).map(|j| raw[(k + frames + j - 2) % frames]).sum::<f32>() / 5.).collect();
    let mean = speed.iter().sum::<f32>() / frames as f32;
    (mean > 0.05 && mean < 5.).then_some(WalkCurve { speed: speed.into_iter().map(|v| v.clamp(0., 5.)).collect(), pivot })
}

/// Her mean walk speed in metres per second (walk_curve's mean), or None.
pub fn walk_speed(s: &mut crate::session::OkSession, clip: &[u8]) -> Option<f32> {
    let curve = walk_curve(s, clip)?;
    Some(curve.speed.iter().sum::<f32>() / curve.speed.len() as f32)
}

/// The walk speed of a session in metres per second (walk_speed), or a negative value when it cannot be read; the
/// session is left posed at the clip's start.
#[no_mangle]
pub unsafe extern "C" fn ok_session_walk_speed(s: *mut crate::session::OkSession, clip: *const u8, len: i32) -> f32 {
    let Some(s) = s.as_mut() else { return -1. };
    if clip.is_null() || len <= 0 { return -1.; }
    walk_speed(s, std::slice::from_raw_parts(clip, len as usize)).unwrap_or(-1.)
}

/// Her walk curve into `out`, three floats a frame (speed m/s, planted ball x, z in root-space metres; at most `cap`
/// floats): the frame count, or -1 when it cannot be read or does not fit. The session is left posed at the clip's start.
#[no_mangle]
pub unsafe extern "C" fn ok_session_walk_curve(s: *mut crate::session::OkSession, clip: *const u8, len: i32, out: *mut f32, cap: i32) -> i32 {
    let Some(s) = s.as_mut() else { return -1 };
    if clip.is_null() || len <= 0 || out.is_null() || cap <= 0 { return -1; }
    let Some(curve) = walk_curve(s, std::slice::from_raw_parts(clip, len as usize)) else { return -1 };
    if curve.speed.len() * 3 > cap as usize { return -1; }
    let out = std::slice::from_raw_parts_mut(out, curve.speed.len() * 3);
    for (k, (v, p)) in curve.speed.iter().zip(&curve.pivot).enumerate() { out[k * 3] = *v; out[k * 3 + 1] = p[0]; out[k * 3 + 2] = p[1]; }
    curve.speed.len() as i32
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

