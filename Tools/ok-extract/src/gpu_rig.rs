//! GPU runtime rig (development): can a character's skin palettes be rebuilt each frame from her skeleton's world
//! matrices instead of being baked per clip? For each palette slot of each part, find the bone whose world W(t) makes
//! W(t)⁻¹·P(t) constant over a clip (then the slot is W(t)·C on the GPU), and report how far the rest are from it.
//!   OK_RIG_CARD=<OK card png> OK_RIG_CLIP=<shared .okanim> cargo test --release --lib gpu_rig -- --ignored --nocapture

pub(crate) mod export;
pub(crate) mod physics;
pub(crate) mod walk;

#[cfg(test)]
mod tests {
    use crate::pose::Mat4;
    use std::path::Path;

    fn mul(a: &Mat4, b: &Mat4) -> Mat4 { crate::pose::mat_mul_pub(a, b) }

    /// Inverse of an affine 4x4 (row-major, translation in column 3; the 3x3 part may shear).
    fn inverse(m: &Mat4) -> Option<Mat4> {
        let a = [[m[0][0] as f64, m[0][1] as f64, m[0][2] as f64], [m[1][0] as f64, m[1][1] as f64, m[1][2] as f64], [m[2][0] as f64, m[2][1] as f64, m[2][2] as f64]];
        let det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        if det.abs() < 1e-12 { return None; }
        let inv = [
            [(a[1][1] * a[2][2] - a[1][2] * a[2][1]) / det, (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / det, (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / det],
            [(a[1][2] * a[2][0] - a[1][0] * a[2][2]) / det, (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / det, (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / det],
            [(a[1][0] * a[2][1] - a[1][1] * a[2][0]) / det, (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / det, (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / det],
        ];
        let t = [m[0][3] as f64, m[1][3] as f64, m[2][3] as f64];
        let mut out = [[0f32; 4]; 4];
        for r in 0..3 {
            for c in 0..3 { out[r][c] = inv[r][c] as f32; }
            out[r][3] = -(inv[r][0] * t[0] + inv[r][1] * t[1] + inv[r][2] * t[2]) as f32;
        }
        out[3][3] = 1.;
        Some(out)
    }

    /// How far two affine matrices move points of a part (Unity units, about metres): the largest displacement of
    /// the origin and of points `reach` away along each axis.
    fn distance(a: &Mat4, b: &Mat4, reach: f32) -> f32 {
        let mut worst = 0f32;
        for p in [[0., 0., 0.], [reach, 0., 0.], [0., reach, 0.], [0., 0., reach]] {
            let mut d2 = 0f32;
            for r in 0..3 {
                let va = a[r][0] * p[0] + a[r][1] * p[1] + a[r][2] * p[2] + a[r][3];
                let vb = b[r][0] * p[0] + b[r][1] * p[1] + b[r][2] * p[2] + b[r][3];
                d2 += (va - vb) * (va - vb);
            }
            worst = worst.max(d2.sqrt());
        }
        worst
    }

    unsafe fn part_palette(s: &mut crate::session::OkSession, part: usize) -> Option<Vec<Mat4>> {
        let mut view = std::mem::zeroed::<crate::ffi::OkSkinPaletteView>();
        let session = s as *mut crate::session::OkSession;
        let count = crate::ffi::ok_part_skin_palette(session, part as i32, std::ptr::null_mut(), std::ptr::null_mut(), 0);
        if count <= 0 { return None; }
        let mut flat = vec![0f32; count as usize * 16];
        if crate::ffi::ok_part_skin_palette(session, part as i32, &mut view, flat.as_mut_ptr(), count) != count { return None; }
        Some(flat.chunks(16).map(|c| [[c[0], c[1], c[2], c[3]], [c[4], c[5], c[6], c[7]], [c[8], c[9], c[10], c[11]], [c[12], c[13], c[14], c[15]]]).collect())
    }

    /// The card and clip of OK_RIG_CARD / OK_RIG_CLIP opened as the game does, the clip installed.
    pub(super) fn open_with_clip_pub() -> Box<crate::session::OkSession> { open_with_clip() }
    fn open_with_clip() -> Box<crate::session::OkSession> {
        let card = std::env::var("OK_RIG_CARD").unwrap_or_else(|_| "E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png".into());
        let clip = std::env::var("OK_RIG_CLIP").unwrap_or_else(|_| "E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim".into());
        if std::env::var("OK_BUILTIN_RESOURCES").is_err() { std::env::set_var("OK_BUILTIN_RESOURCES", "E:/OpenKoikatsu/Content/IndependentResources/v1"); }
        if std::env::var("OK_BASE_LIBRARY").is_err() { std::env::set_var("OK_BASE_LIBRARY", "E:/OpenKoikatsu/Content/IndependentResources/base_v1"); }
        let mut s = unsafe {
            let (game, path) = (std::ffi::CString::new("").unwrap(), std::ffi::CString::new(card.clone()).unwrap());
            let mut out: *mut crate::session::OkSession = std::ptr::null_mut();
            let mut err = vec![0i8; 2048];
            let status = crate::ffi::session_open(game.as_ptr(), path.as_ptr(), 0, &mut out, err.as_mut_ptr(), err.len() as i32);
            assert!(status == 0 && !out.is_null(), "open card: {}", std::ffi::CStr::from_ptr(err.as_ptr()).to_string_lossy());
            Box::from_raw(out)
        };
        let bytes = std::fs::read(&clip).expect("clip");
        let (root, key) = crate::anim::install_shared_clip(&bytes).expect("install clip");
        s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.;
        s.reset_anim_clock(); s.pv_mask = 15; s.reset_dynamic_accessories();
        s
    }

    /// The GPU rig and shared clip exports of a card: sizes, and how many palette matrices follow no single bone.
    #[test]
    #[ignore]
    fn gpu_rig_export() {
        let mut s = open_with_clip();
        let s = &mut *s;
        let clip = std::fs::read(std::env::var("OK_RIG_CLIP").unwrap_or_else(|_| "E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim".into())).unwrap();
        let start = std::time::Instant::now();
        let (rig, loose) = super::export::export_rig(s, &clip).expect("rig");
        let rig_ms = start.elapsed().as_secs_f64() * 1000.;
        let start = std::time::Instant::now();
        let shared = super::export::export_clip(s, &clip, 60.).expect("clip");
        println!("rig {} bytes ({rig_ms:.0} ms, {loose} loose matrices), clip {} bytes ({:.0} ms)", rig.len(), shared.len(), start.elapsed().as_secs_f64() * 1000.);
        assert_eq!(&rig[..4], b"OKGR");
        assert_eq!(&shared[..4], b"OKGC");
    }

    #[test]
    #[ignore]
    fn palette_factorization() {
        let physics = std::env::var("OK_RIG_PHYSICS").is_ok();
        let mut s = open_with_clip();
        let s = &mut *s;
        let duration = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
        let frames = 24usize;
        let step = duration / frames as f32;
        // Per frame: every part's palette, and every candidate bone's world in palette space (body bones as they are,
        // head bones carried onto the body).
        let mut palettes: Vec<Vec<Option<Vec<Mat4>>>> = Vec::new();
        let mut candidates: Vec<Vec<Mat4>> = Vec::new();
        let mut names: Vec<String> = Vec::new();
        for k in 0..frames {
            let t = step * k as f32;
            if physics { s.apply_anim_frame(t, if k == 0 { 0. } else { step }); }
            else { s.sample_paired_frame(t); }
            let mut worlds = Vec::new();
            let mut here = Vec::new();
            if let Some(body) = s.body_skel.as_ref() {
                for (i, w) in body.worlds.iter().enumerate() { worlds.push(*w); here.push(format!("body:{}", body.names.get(i).cloned().unwrap_or_default())); }
            }
            if let Some(head) = s.head_skel.as_ref() {
                let onto = crate::head_onto_body(s.body_skel.as_ref(), Some(head));
                for (i, w) in head.worlds.iter().enumerate() { worlds.push(mul(&onto, w)); here.push(format!("head:{}", head.names.get(i).cloned().unwrap_or_default())); }
            }
            if k == 0 { names = here; }
            candidates.push(worlds);
            palettes.push((0..s.parts.len()).map(|p| unsafe { part_palette(s, p) }).collect());
        }
        // What one frame of the skeleton costs on the CPU (curves, shape, expression bones, ABMX; no physics, no skin).
        {let start = std::time::Instant::now(); let n = 240;
         for k in 0..n { s.sample_paired_frame(duration * k as f32 / n as f32); }
         println!("CPU skeleton frame: {:.3} ms (sample_paired_frame, {} bones body / {} head)", start.elapsed().as_secs_f64() * 1000. / n as f64,
             s.body_skel.as_ref().map_or(0, |b| b.tfs.len()), s.head_skel.as_ref().map_or(0, |h| h.tfs.len()));}
        let reach = 0.2f32;
        let mut total = 0usize; let mut exact = 0usize; let mut close = 0usize;
        let mut worst: Vec<(f32, String)> = Vec::new();
        for p in 0..s.parts.len() {
            let Some(first) = palettes[0][p].as_ref() else { continue };
            let label = s.parts[p].name.clone();
            let mut part_worst = 0f32;
            for slot in 0..first.len() {
                if palettes.iter().any(|f| f[p].as_ref().map_or(true, |m| m.len() != first.len())) { break; }
                total += 1;
                // The bone whose W(0)⁻¹·P(0), carried to every frame, stays closest to P(t).
                let mut best = (f32::MAX, 0usize);
                for (n, w0) in candidates[0].iter().enumerate() {
                    let Some(inv) = inverse(w0) else { continue };
                    let c = mul(&inv, &palettes[0][p].as_ref().unwrap()[slot]);
                    let mut err = 0f32;
                    for k in 1..frames {
                        err = err.max(distance(&mul(&candidates[k][n], &c), &palettes[k][p].as_ref().unwrap()[slot], reach));
                        if err >= best.0 { break; }
                    }
                    if err < best.0 { best = (err, n); }
                }
                if best.0 < 1e-4 { exact += 1; } else if best.0 < 2e-3 { close += 1; }
                if best.0 > part_worst { part_worst = best.0; }
                if best.0 >= 2e-3 { worst.push((best.0, format!("{label} slot {slot} -> {}", names.get(best.1).cloned().unwrap_or_default()))); }
            }
            println!("part {p:3} {label:40} slots {:4} worst {:.4}", first.len(), part_worst);
        }
        worst.sort_by(|a, b| b.0.total_cmp(&a.0));
        for (e, what) in worst.iter().take(25) { println!("  {e:.4}  {what}"); }
        println!("slots {total}: exact (<0.1 mm) {exact}, close (<2 mm) {close}, other {} (physics {physics})", total - exact - close);
    }
}


#[cfg(test)]
mod check_terms {
    /// The game's path: a card's animation snapshot reopened, its terms factored, then frames applied as the bank does
    /// (apply_anim_frame): how far W(t)·C is from P(t).
    #[test]
    #[ignore]
    fn gpu_rig_terms_game_path() {
        let card = super::tests::open_with_clip_pub();
        let bytes = card.to_animation_bytes().expect("snapshot");
        let mut s = Box::new(crate::session::OkSession::from_animation_bytes(&bytes).expect("reopen"));
        let s = &mut *s;
        let clip = std::fs::read("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim").unwrap();
        let _ = super::export::export_rig(s, &clip).expect("rig");
        let rig = super::export::FlatRig::from_session(s).unwrap();
        let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip).max(0.1);
        let terms = super::export::factor_palettes(s, &rig, &[0., d * 0.31, d * 0.62, d * 0.87]);
        // Other clips the room plays (standing poses that move the fingers), the terms kept.
        let mut worst = vec![0f32; terms.len()];
        for other in ["walk_01", "conversation_wait_00", "conversation_wait_10", "library/lib_0_0_5", "library/lib_17_4_2", "conversation_reserved"] {
            let bytes = std::fs::read(format!("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/{other}.okanim")).unwrap();
            let (root, key) = crate::anim::install_shared_clip(&bytes).expect("install");
            s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
            let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip).max(0.1);
            s.apply_anim_frame(0., 0.);
            let mut clip_worst = vec![0f32; terms.len()];
            // Played at 60 Hz, springs and all, as the bank solves it.
            let frames = (d * 60.).ceil() as usize;
            for k in 1..=frames {
                let t = (k as f32 / 60.).min(d);
                s.apply_anim_frame(t, 1. / 60.);
                let body = s.body_skel.as_ref().unwrap();
                let worlds: Vec<_> = rig.ids.iter().map(|id| crate::pose::world_of_id(*id, &body.tfs)).collect();
                for p in 0..terms.len() {
                    let Some(pal) = super::export::part_palette(s, p) else { continue };
                    for (j, term) in terms[p].iter().enumerate() {
                        if j >= pal.len() { continue; }
                        let w = if term.node < 0 { crate::pose::ident() } else { worlds[term.node as usize] };
                        let e = super::export::distance_pub(&crate::pose::mat_mul_pub(&w, &term.c), &pal[j]);
                        clip_worst[p] = clip_worst[p].max(e);
                    }
                }
            }
            println!("{other}: {}", clip_worst.iter().enumerate().filter(|(_, e)| **e > 1e-3).map(|(p, e)| format!("{p}:{e:.3}")).collect::<Vec<_>>().join(" "));
            for (w, e) in worst.iter_mut().zip(&clip_worst) { *w = w.max(*e); }
        }
        for (p, e) in worst.iter().enumerate() { if *e > 0.05 { println!("part {p} {} acc={} err={e:.3} terms={:?}", s.parts[p].name, s.parts[p].runtime_accessory.is_some(), terms[p].iter().take(4).map(|t| t.node).collect::<Vec<_>>()); } }
        println!("worst per part (m): {}", worst.iter().enumerate().map(|(p, e)| format!("{p}:{e:.4}")).collect::<Vec<_>>().join(" "));
    }
}

#[cfg(test)]
mod physics_export {
    /// The physics section of a card's rig: counts, and every node's world rebuilt from its anchor and locals the way the
    /// GPU builds it (rest pose: a particle's prefab local), against the session's at the first sample.
    #[test]
    #[ignore]
    fn gpu_rig_physics_export() {
        use super::physics::{Parent, Physics};
        let card = super::tests::open_with_clip_pub();
        let bytes = card.to_animation_bytes().expect("snapshot");
        let mut s = Box::new(crate::session::OkSession::from_animation_bytes(&bytes).expect("reopen"));
        let s = &mut *s;
        let clip = std::fs::read("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim").unwrap();
        let _ = super::export::export_rig(s, &clip).expect("rig");
        let rig = super::export::FlatRig::from_session(s).unwrap();
        let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip).max(0.1);
        let taken = super::export::take_samples(s, &rig, &[0., d * 0.31, d * 0.62, d * 0.87]);
        let ph = Physics::build(s, &rig, &taken);
        let aliases = ph.nodes.iter().filter(|n| n.alias.is_some()).count();
        let tops = ph.nodes.iter().filter(|n| n.matrix.is_some()).count();
        let colliders: usize = ph.chains.iter().map(|c| c.colliders.len()).sum();
        println!("sets {:?}", super::physics::sets(s));
        println!("ranges {:?}", ph.ranges);
        println!("nodes {} (aliases {aliases}, tops {tops}, loose tops {}), chains {}, particles {}, colliders {colliders}, follow {}",
            ph.nodes.len(), ph.loose_tops, ph.chains.len(), ph.particle_count(), ph.follow.len());
        // Rebuild every node at sample 0 and compare (particles are not at their prefab locals after the steps: skipped).
        let particles: std::collections::HashSet<usize> = ph.chains.iter().flat_map(|c| c.particles.iter().filter_map(|p| p.node)).collect();
        let mut built: Vec<crate::pose::Mat4> = Vec::new();
        let (mut worst, mut worst_at) = (0f32, 0usize);
        let actual = super::physics::node_worlds(&ph, &taken.sets[0]);
        for (k, n) in ph.nodes.iter().enumerate() {
            let parent = match n.parent { Parent::None => crate::pose::ident(), Parent::Rig(r) => taken.worlds[0][r], Parent::Node(p) => built[p] };
            let w = if let Some(a) = n.alias { taken.worlds[0][a] } else if let Some(m) = n.matrix { crate::pose::mat_mul_pub(&parent, &m) }
                else { crate::pose::mat_mul_pub(&parent, &crate::pose::trs(n.pos, n.rot, n.scale)) };
            // Continue from the session's actual world so one swung particle does not spoil its whole subtree.
            built.push(actual[k]);
            if particles.contains(&k) || n.alias.is_some() { continue; }
            let e = super::export::distance_pub(&w, &actual[k]);
            if e > worst { worst = e; worst_at = k; }
        }
        println!("static node rebuild: worst {worst:.6} at node {worst_at} {:?}", ph.sources.get(worst_at));
        let palettes = super::export::factor(s, &rig, &taken, Some(&ph));
        let loose: Vec<String> = palettes.iter().enumerate().filter_map(|(p, t)| { let n = t.iter().filter(|t| t.error > 1e-4).count(); (n > 0).then(|| format!("{p}:{n}/{}", t.len())) }).collect();
        let on_nodes = palettes.iter().flatten().filter(|t| t.node >= rig.ids.len() as i32).count();
        println!("palette terms on physics nodes {on_nodes}, loose {}", loose.join(" "));
    }
}

#[cfg(test)]
mod composition {
    /// The pose bank keeps each native bone's Unity local TRS and composes them like UE's FTransform (IndependentPose.usf
    /// NativeTRS: rotations multiply, scales multiply per axis, no shear); the native skeleton (and SkeletonCS) multiplies
    /// full matrices. Under a non-uniformly scaled bone that turns its child (the limb-size corrections' forearm 0.92 and
    /// hand 0.9, ABMX) the two differ. Per card and clip: the bones where they differ most (cm, Unity x 100).
    ///   OK_RIG_CARDS=C000,C004 cargo test --release --lib gpu_rig_trs_composition -- --ignored --nocapture
    #[test]
    #[ignore]
    fn gpu_rig_trs_composition() {
        use crate::pose::{quat_mul, Mat4, Tf};
        use std::collections::HashMap;
        fn rotate(q: [f32; 4], v: [f32; 3]) -> [f32; 3] {
            let t = [2. * (q[1] * v[2] - q[2] * v[1]), 2. * (q[2] * v[0] - q[0] * v[2]), 2. * (q[0] * v[1] - q[1] * v[0])];
            [v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]), v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]), v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0])]
        }
        fn trs_world(id: i64, tfs: &HashMap<i64, Tf>) -> Mat4 {
            let mut chain = vec![id];
            while let Some(p) = tfs.get(chain.last().unwrap()).and_then(|t| t.parent).filter(|p| *p != 0 && tfs.contains_key(p)) { chain.push(p); }
            let root = &tfs[chain.last().unwrap()];
            let (mut t, mut q, mut s) = (root.pos, root.rot, root.scale);
            for id in chain.iter().rev().skip(1) {
                let l = &tfs[id];
                if s.iter().chain(l.scale.iter()).any(|v| *v < 0.) { return crate::pose::world_of_id(*id, tfs); }
                let r = rotate(q, [s[0] * l.pos[0], s[1] * l.pos[1], s[2] * l.pos[2]]);
                t = [t[0] + r[0], t[1] + r[1], t[2] + r[2]];
                q = quat_mul(q, l.rot);
                s = [s[0] * l.scale[0], s[1] * l.scale[1], s[2] * l.scale[2]];
            }
            crate::pose::trs(t, q, s)
        }
        let cards = std::env::var("OK_RIG_CARDS").unwrap_or_else(|_| "C000,C001,C004".into());
        let clips = ["walk_01", "conversation_wait_00", "conversation_wait_10", "library/lib_0_0_5", "library/lib_17_4_2"];
        for card in cards.split(',') {
            std::env::set_var("OK_RIG_CARD", format!("E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/{card}.png"));
            let mut s = super::tests::open_with_clip_pub();
            let s = &mut *s;
            for clip in clips {
                let bytes = std::fs::read(format!("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/{clip}.okanim")).unwrap();
                let (root, key) = crate::anim::install_shared_clip(&bytes).expect("install");
                s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
                let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip).max(0.1);
                let mut worst: HashMap<String, (f32, f32)> = HashMap::new();
                for k in 0..8 {
                    s.sample_paired_frame(d * k as f32 / 8.);
                    let body = s.body_skel.as_ref().unwrap();
                    for (id, tf) in &body.tfs {
                        let full = crate::pose::world_of_id(*id, &body.tfs);
                        let comp = trs_world(*id, &body.tfs);
                        let pos = ((0..3).map(|r| (full[r][3] - comp[r][3]).powi(2)).sum::<f32>()).sqrt() * 100.;
                        let lin = (0..3).flat_map(|r| (0..3).map(move |c| (r, c))).map(|(r, c)| (full[r][c] - comp[r][c]).abs()).fold(0f32, f32::max);
                        let e = worst.entry(tf.name.clone()).or_default();
                        e.0 = e.0.max(pos); e.1 = e.1.max(lin);
                    }
                }
                let mut list: Vec<_> = worst.into_iter().filter(|(_, e)| e.0 > 0.01 || e.1 > 1e-3).collect();
                list.sort_by(|a, b| b.1.0.total_cmp(&a.1.0).then(b.1.1.total_cmp(&a.1.1)));
                println!("{card} {clip}: {} bones differ; {}", list.len(),
                    list.iter().take(8).map(|(n, e)| format!("{n} {:.3} cm / {:.4}", e.0, e.1)).collect::<Vec<_>>().join(", "));
            }
            let body = s.body_skel.as_ref().unwrap();
            let mut scaled: Vec<String> = body.tfs.values().filter(|t| (t.scale[0] - t.scale[1]).abs() > 1e-4 || (t.scale[1] - t.scale[2]).abs() > 1e-4)
                .filter(|t| t.name.contains("arm") || t.name.contains("hand") || t.name.contains("shoulder") || t.name.contains("height") || t.name.contains("root"))
                .map(|t| format!("{} {:?}", t.name, t.scale)).collect();
            scaled.sort();
            println!("{card} non-uniform arm-line scales: {}", scaled.join("; "));
        }
    }
}
