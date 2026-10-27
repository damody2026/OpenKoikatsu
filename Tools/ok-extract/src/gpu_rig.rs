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

#[cfg(test)]
mod foot_slip {
    /// How far a crowd girl's feet slide while she walks the room's way (OKCrowdRoom: straight at walk_speed, the walk
    /// clip at 1x): each foot bone's world position is her pose (the native solve, which the bank and the rig both match:
    /// `--gpu-rig-verify`) plus her travel, and while the bone is down (within 1 cm of its lowest) its horizontal speed is
    /// the slide. Also the speed that would zero it: the bone's own backward speed in her root space while down.
    ///   OK_RIG_CARDS=C000,C001 cargo test --release --lib gpu_rig_foot_slip -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn gpu_rig_foot_slip() {
        let cards = std::env::var("OK_RIG_CARDS").unwrap_or_else(|_| "C000,C001,C003,C004,C005,C006,C007,C008,C009,C010".into());
        let clip = std::fs::read("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim").unwrap();
        let pct = |v: &mut Vec<f32>, p: f32| -> f32 { if v.is_empty() { return f32::NAN; } v.sort_by(|a, b| a.total_cmp(b)); v[((v.len() - 1) as f32 * p).round() as usize] };
        for card in cards.split(',') {
            std::env::set_var("OK_RIG_CARD", format!("E:/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/{card}.png"));
            let Ok(opened) = std::panic::catch_unwind(|| super::tests::open_with_clip_pub()) else { println!("{card}: open failed"); continue };
            let mut s = Box::new(crate::session::OkSession::from_animation_bytes(&opened.to_animation_bytes().expect("snapshot")).expect("reopen"));
            let s = &mut *s;
            let Some(v) = super::walk::walk_speed(s, &clip) else { println!("{card}: no walk speed"); continue };
            let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
            let names = ["cf_j_foot_L", "cf_j_toes_L", "cf_j_foot_R", "cf_j_toes_R"];
            let ids: Vec<i64> = names.iter().map(|n| s.body_skel.as_ref().unwrap().tfs.iter().find(|(_, t)| t.name == *n).map(|(id, _)| *id).expect("foot")).collect();
            // Two loops at 60 Hz: root-space positions (x, y up, z forward).
            let frames = (d * 60.).round() as usize * 2;
            let mut track: Vec<Vec<[f32; 3]>> = Vec::new();
            for k in 0..=frames {
                s.sample_paired_frame((k as f32 / 60.) % d);
                let body = s.body_skel.as_ref().unwrap();
                track.push(ids.iter().map(|id| { let w = crate::pose::world_of_id(*id, &body.tfs); [w[0][3], w[1][3], w[2][3]] }).collect());
            }
            let mut line = format!("{card}: walk {:.1} cm/s, loop {d:.3} s;", v * 100.);
            let mut all = Vec::new(); let mut best = Vec::new();
            for (b, name) in names.iter().enumerate() {
                let low = track.iter().map(|t| t[b][1]).fold(f32::MAX, f32::min);
                let (mut slide, mut back) = (Vec::new(), Vec::new());
                for k in 0..frames {
                    let (a, c) = (track[k][b], track[k + 1][b]);
                    if a[1] > low + 0.01 || c[1] > low + 0.01 { continue; }
                    // A loop's wrap is not a step of the clip.
                    if ((k + 1) as f32 / 60.) % d < (k as f32 / 60.) % d { continue; }
                    let (dx, dz) = (c[0] - a[0], c[2] - a[2] + v / 60.);
                    slide.push((dx * dx + dz * dz).sqrt() * 60. * 100.);
                    back.push(-(c[2] - a[2]) * 60. * 100.);
                }
                all.extend(slide.iter().copied()); best.extend(back.iter().copied());
                line += &format!(" {name} {}f median {:.1} p90 {:.1} max {:.1} (own {:.1})", slide.len(), pct(&mut slide.clone(), 0.5), pct(&mut slide.clone(), 0.9), pct(&mut slide, 1.), pct(&mut back, 0.5));
            }
            // The same walk under the best constant speed, and under a speed that follows the clip: at each frame the
            // backward speed of the foot that is down (the lower contact point of the two sides), smoothed over 5 frames.
            let contact = |t: &Vec<[f32; 3]>, side: usize| if t[side * 2][1] <= t[side * 2 + 1][1] { side * 2 } else { side * 2 + 1 };
            let mut curve = vec![0f32; frames];
            for k in 0..frames {
                let (a, c) = (&track[k], &track[k + 1]);
                let side = if a[contact(a, 0)][1] <= a[contact(a, 1)][1] { 0 } else { 1 };
                let b = contact(a, side);
                curve[k] = -(c[b][2] - a[b][2]) * 60.;
            }
            let smooth: Vec<f32> = (0..frames).map(|k| { let mut s = 0.; for j in 0..5 { s += curve[(k + frames + j - 2) % frames]; } s / 5. }).collect();
            let opt = pct(&mut best.clone(), 0.5) / 100.;
            let shipped = super::walk::walk_curve(s, &clip).expect("walk curve").speed;
            let shipped: Vec<f32> = (0..frames).map(|k| shipped[k % shipped.len()]).collect();
            for (label, speed) in [("best constant", None), ("clip curve", Some(&smooth)), ("walk_curve", Some(&shipped))] {
                let (mut sl, mut sx, mut sz) = (Vec::new(), Vec::new(), Vec::new());
                for b in 0..names.len() {
                    let low = track.iter().map(|t| t[b][1]).fold(f32::MAX, f32::min);
                    for k in 0..frames {
                        let (a, c) = (track[k][b], track[k + 1][b]);
                        if a[1] > low + 0.01 || c[1] > low + 0.01 { continue; }
                        if ((k + 1) as f32 / 60.) % d < (k as f32 / 60.) % d { continue; }
                        let vk = speed.map_or(opt, |s| s[k]);
                        let (dx, dz) = (c[0] - a[0], c[2] - a[2] + vk / 60.);
                        sl.push((dx * dx + dz * dz).sqrt() * 6000.); sx.push(dx.abs() * 6000.); sz.push(dz.abs() * 6000.);
                    }
                }
                println!("  {card} {label} ({:.1} cm/s mean): slide median {:.1} p90 {:.1}; sideways median {:.1}, along median {:.1} cm/s", speed.map_or(opt, |s| s.iter().sum::<f32>() / s.len() as f32) * 100.,
                    pct(&mut sl.clone(), 0.5), pct(&mut sl, 0.9), pct(&mut sx, 0.5), pct(&mut sz, 0.5));
            }
            println!("{line}; all median {:.1} p90 {:.1} cm/s, speed that zeroes it {:.1} cm/s", pct(&mut all.clone(), 0.5), pct(&mut all, 0.9), pct(&mut best, 0.5));
        }
    }
    /// The same from a game's own record (ok.Crowd.WalkTrace 1 writes walk.tsv beside the room's report): each girl's
    /// frames that play one clip (no crossfade), her pose at the drawn clip time (the native solve; the bank and the rig
    /// match it) placed by her transform that frame. A foot bone is down within 1 cm of its lowest; its slide is its
    /// horizontal speed over at least 1/30 s with both ends down and the same kind of frame (walking: clip slot 1, else
    /// standing). Turning: her yaw changes over 5 deg/s. Older records name only her walk; then only walking frames count.
    ///   OK_WALK_TRACE=<report dir> cargo test --release --lib gpu_rig_foot_slip_trace -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn gpu_rig_foot_slip_trace() {
        use std::collections::HashMap;
        let dir = std::env::var("OK_WALK_TRACE").expect("OK_WALK_TRACE");
        let text = std::fs::read_to_string(format!("{dir}/walk.tsv")).expect("walk.tsv");
        let mut girls: HashMap<usize, (Vec<String>, String)> = HashMap::new();
        // time, clip slot, clip time, position, yaw
        let mut rows: HashMap<usize, Vec<(f32, usize, f32, [f32; 3], f32)>> = HashMap::new();
        for line in text.lines() {
            let f: Vec<&str> = line.split('\t').collect();
            if line.starts_with("# ") { continue; }
            if f[0] == "#" {
                let mut clips: Vec<String> = f[2].split(',').map(String::from).collect();
                if clips.len() == 1 { clips.insert(0, String::new()); }
                girls.insert(f[1].parse().unwrap(), (clips, f[3].to_string()));
                continue;
            }
            let n = |i: usize| f[i].parse::<f32>().unwrap();
            if n(6) != 0. { continue; }
            rows.entry(f[1].parse().unwrap()).or_default().push((n(0), f[2].parse().unwrap(), n(4), [n(7), n(8), n(9)], n(10)));
        }
        let pct = |v: &mut Vec<f32>, p: f32| -> f32 { if v.is_empty() { return f32::NAN; } v.sort_by(|a, b| a.total_cmp(b)); v[((v.len() - 1) as f32 * p).round() as usize] };
        let names = ["cf_j_foot_L", "cf_j_toes_L", "cf_j_foot_R", "cf_j_toes_R"];
        let mut keys: Vec<usize> = rows.keys().copied().collect(); keys.sort();
        // walking straight, walking and turning, standing and turning
        let mut everyone: [Vec<f32>; 3] = Default::default();
        for g in keys {
            let (clips, card) = &girls[&g];
            let path_of = |id: &str| if std::path::Path::new(id).is_file() { id.to_string() } else { format!("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/{id}") };
            let frames: Vec<_> = rows[&g].iter().copied().filter(|r| clips.get(r.1).is_some_and(|c| !c.is_empty() && std::path::Path::new(&path_of(c)).is_file())).collect();
            if frames.len() < 30 { continue; }
            std::env::set_var("OK_RIG_CARD", card);
            let Ok(opened) = std::panic::catch_unwind(|| super::tests::open_with_clip_pub()) else { println!("{card}: open failed"); continue };
            let mut s = Box::new(crate::session::OkSession::from_animation_bytes(&opened.to_animation_bytes().expect("snapshot")).expect("reopen"));
            let s = &mut *s;
            let ids: Vec<i64> = names.iter().map(|n| s.body_skel.as_ref().unwrap().tfs.iter().find(|(_, t)| t.name == *n).map(|(id, _)| *id).expect("foot")).collect();
            // Her root-space feet per frame, clip by clip.
            let mut local: Vec<Vec<[f32; 3]>> = vec![Vec::new(); frames.len()];
            let mut slots: Vec<usize> = frames.iter().map(|r| r.1).collect(); slots.sort(); slots.dedup();
            for slot in slots {
                let data = std::fs::read(path_of(&clips[slot])).unwrap();
                let (root, key) = crate::anim::install_shared_clip(&data).expect("install");
                s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
                let mut cache: HashMap<u32, Vec<[f32; 3]>> = HashMap::new();
                for (k, r) in frames.iter().enumerate() {
                    if r.1 != slot { continue; }
                    local[k] = cache.entry(r.2.to_bits()).or_insert_with(|| {
                        s.sample_paired_frame(r.2);
                        let body = s.body_skel.as_ref().unwrap();
                        ids.iter().map(|id| { let w = crate::pose::world_of_id(*id, &body.tfs); [w[0][3], w[1][3], w[2][3]] }).collect()
                    }).clone();
                }
            }
            // World (UE cm) per frame and bone: Unity root space (x, y up, z forward) -> UE (-x, z, y) x 100, turned by yaw.
            let world: Vec<Vec<[f32; 3]>> = frames.iter().zip(&local).map(|((_, _, _, p, yaw), l)| {
                let (sn, cs) = yaw.to_radians().sin_cos();
                l.iter().map(|u| { let (vx, vy, vz) = (-u[0] * 100., u[2] * 100., u[1] * 100.); [p[0] + vx * cs - vy * sn, p[1] + vx * sn + vy * cs, p[2] + vz] }).collect()
            }).collect();
            let walking = |k: usize| frames[k].1 == 1;
            let mut kinds: [Vec<f32>; 3] = Default::default();
            let (mut turning_seconds, mut moved, mut span) = (0f32, 0f32, 0f32);
            for b in 0..names.len() {
                let low = world.iter().map(|w| w[b][2]).fold(f32::MAX, f32::min);
                let mut j = 0;
                for i in 0..frames.len() {
                    if world[i][b][2] > low + 1. { continue; }
                    j = j.max(i + 1);
                    while j < frames.len() && frames[j].0 < frames[i].0 + 1. / 30. { j += 1; }
                    if j >= frames.len() || frames[j].0 > frames[i].0 + 0.1 { continue; }
                    if (i..=j).any(|k| world[k][b][2] > low + 1. || walking(k) != walking(i)) { continue; }
                    let (a, c) = (world[i][b], world[j][b]);
                    let dt = frames[j].0 - frames[i].0;
                    let v = ((c[0] - a[0]).powi(2) + (c[1] - a[1]).powi(2)).sqrt() / dt;
                    let turn = ((frames[j].4 - frames[i].4 + 540.) % 360. - 180.).abs() / dt;
                    match (walking(i), turn > 5.) { (true, false) => kinds[0].push(v), (true, true) => kinds[1].push(v), (false, true) => kinds[2].push(v), _ => {} }
                }
            }
            for k in 1..frames.len() {
                let dt = frames[k].0 - frames[k - 1].0;
                if dt >= 0.1 { continue; }
                if !walking(k) && ((frames[k].4 - frames[k - 1].4 + 540.) % 360. - 180.).abs() / dt > 5. { turning_seconds += dt; }
                if walking(k) && walking(k - 1) { moved += ((frames[k].3[0] - frames[k - 1].3[0]).powi(2) + (frames[k].3[1] - frames[k - 1].3[1]).powi(2)).sqrt(); span += dt; }
            }
            let line = |v: &mut Vec<f32>| format!("{} spans, median {:.1} p90 {:.1} max {:.1}", v.len(), pct(&mut v.clone(), 0.5), pct(&mut v.clone(), 0.9), pct(v, 1.));
            println!("girl {g} {}: walking {:.1} cm/s; walking straight {}; walking and turning {}; standing and turning ({turning_seconds:.1} s) {} cm/s",
                std::path::Path::new(card).file_name().unwrap().to_string_lossy(), moved / span.max(1e-3), line(&mut kinds[0].clone()), line(&mut kinds[1].clone()), line(&mut kinds[2].clone()));
            for (e, k) in everyone.iter_mut().zip(kinds) { e.extend(k); }
        }
        let [mut a, mut b, mut c] = everyone;
        println!("everyone: walking straight {} spans median {:.1} p90 {:.1}; walking and turning {} spans median {:.1} p90 {:.1}; standing and turning {} spans median {:.1} p90 {:.1} cm/s",
            a.len(), pct(&mut a.clone(), 0.5), pct(&mut a, 0.9), b.len(), pct(&mut b.clone(), 0.5), pct(&mut b, 0.9), c.len(), pct(&mut c.clone(), 0.5), pct(&mut c, 0.9));
    }

}

#[cfg(test)]
mod street_hips {
    /// A street heroine's whole body sat 1.1 mm off between the bank and the rig (Play-20261028T041021, jeanne_alter,
    /// from cf_j_hips up). The bank solves frames with apply_anim_frame (dynamics, interaction); the rig follows the
    /// native-only sample (sample_paired_frame). Per clip: the largest gap of her hips between the two, and how far her
    /// hips move in 1/60 s (a frame's time offset).
    ///   OK_RIG_CARD=<card> cargo test --release --lib gpu_rig_street_hips -- --ignored --nocapture --test-threads=1
    #[test]
    #[ignore]
    fn gpu_rig_street_hips() {
        if std::env::var("OK_RIG_CARD").is_err() { std::env::set_var("OK_RIG_CARD", "E:/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne_alter.png"); }
        let opened = super::tests::open_with_clip_pub();
        let bytes = opened.to_animation_bytes().expect("snapshot");
        for clip in ["walk_01", "run_female", "conversation_wait_00", "conversation_wait_04", "conversation_wait_10", "library/lib_0_0_1"] {
            let mut s = Box::new(crate::session::OkSession::from_animation_bytes(&bytes).expect("reopen"));
            let s = &mut *s;
            let data = std::fs::read(format!("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/{clip}.okanim")).unwrap();
            let (root, key) = crate::anim::install_shared_clip(&data).expect("install");
            s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
            let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
            let hips = s.body_skel.as_ref().unwrap().tfs.iter().find(|(_, t)| t.name == "cf_j_hips").map(|(id, _)| *id).unwrap();
            let at = |s: &crate::session::OkSession| { let w = crate::pose::world_of_id(hips, &s.body_skel.as_ref().unwrap().tfs); [w[0][3], w[1][3], w[2][3]] };
            let frames = (d * 60.).ceil() as usize;
            let (mut gap, mut step, mut last) = (0f32, 0f32, None::<[f32; 3]>);
            for k in 0..=frames {
                let t = (k as f32 / 60.).min(d);
                s.apply_anim_frame(t, if k == 0 { 0. } else { 1. / 60. });
                let full = at(s);
                s.sample_paired_frame(t);
                let native = at(s);
                gap = gap.max((0..3).map(|i| (full[i] - native[i]).powi(2)).sum::<f32>().sqrt() * 100.);
                if let Some(l) = last { step = step.max((0..3).map(|i| (native[i] - l[i]).powi(2)).sum::<f32>().sqrt() * 100.); }
                last = Some(native);
            }
            println!("{clip}: {d:.3} s, hips full vs native max {gap:.4} cm, hips moves up to {step:.4} cm per 1/60 s");
        }
    }
}

#[cfg(test)]
mod verify_time {
    /// Which clip time a verify dump's bank and rig rows show (run_crowd_room100.lua / street -GpuRigVerify write rig.bin,
    /// rig.txt, rig-play.txt, rig-actors.txt): for one actor's bone, the rig row minus the bank row (translation, UE cm)
    /// against her native pose at the dumped time moved by a fraction of a frame (a named bone's row is its component
    /// matrix times a fixed inverse bind, so a translation gap is its world gap when the turns agree).
    ///   OK_RIG_DUMP=<report dir> OK_RIG_ACTOR=1 OK_RIG_BONE=cf_j_hips cargo test --release --lib gpu_rig_verify_time -- --ignored --nocapture
    #[test]
    #[ignore]
    fn gpu_rig_verify_time() {
        let dir = std::env::var("OK_RIG_DUMP").expect("OK_RIG_DUMP");
        let actor: usize = std::env::var("OK_RIG_ACTOR").ok().and_then(|v| v.parse().ok()).unwrap_or(1);
        let bone = std::env::var("OK_RIG_BONE").unwrap_or_else(|_| "cf_j_hips".into());
        let names: Vec<String> = std::fs::read_to_string(format!("{dir}/rig.txt")).unwrap().lines().map(String::from).collect();
        let bin = std::fs::read(format!("{dir}/rig.bin")).unwrap();
        let rows = names.len();
        let row = names.iter().position(|l| { let f: Vec<&str> = l.split(' ').collect(); f[0] == actor.to_string() && f[1] == bone }).expect("row");
        let t3 = |half: usize| -> [f32; 3] { let at = |k: usize| f32::from_le_bytes(bin[(half * rows + row) * 48 + k * 4..][..4].try_into().unwrap()); [at(3), at(7), at(11)] };
        let (bank, rig) = (t3(0), t3(1));
        let gap = [rig[0] - bank[0], rig[1] - bank[1], rig[2] - bank[2]];
        let play: Vec<String> = std::fs::read_to_string(format!("{dir}/rig-play.txt")).unwrap().lines().map(String::from).collect();
        let p: Vec<f32> = play[actor].split(' ').map(|v| v.parse().unwrap()).collect();
        let actors = std::fs::read_to_string(format!("{dir}/rig-actors.txt")).unwrap();
        let line = actors.lines().find(|l| l.split('\t').next() == Some(&actor.to_string())).expect("actor line");
        let f: Vec<&str> = line.split('\t').collect();
        let clips: Vec<&str> = f[2].split(',').collect();
        let clip = clips[p[0] as usize];
        println!("actor {actor} {} clip slot {} {clip} time {:.6} (B {} {:.6}, blend {}): rig - bank {:?} cm", f[1], p[0], p[2], p[1], p[3], p[4], gap);
        std::env::set_var("OK_RIG_CARD", f[1]);
        let opened = super::tests::open_with_clip_pub();
        let mut s = Box::new(crate::session::OkSession::from_animation_bytes(&opened.to_animation_bytes().expect("snapshot")).expect("reopen"));
        let s = &mut *s;
        let data = std::fs::read(format!("E:/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/{clip}")).unwrap();
        let (root, key) = crate::anim::install_shared_clip(&data).expect("install");
        s.game_root = root; s.anim_clip = key; s.anim_parameters.clear(); s.anim_scene_mode = 0; s.anim_time = 0.; s.reset_anim_clock(); s.pv_mask = 15;
        let d = crate::anim::clip_duration(&s.game_root, &s.anim_clip);
        let id = s.body_skel.as_ref().unwrap().tfs.iter().find(|(_, t)| t.name == bone).map(|(id, _)| *id).unwrap();
        let mut at = |t: f32| { s.sample_paired_frame(t.clamp(0., d)); let w = crate::pose::world_of_id(id, &s.body_skel.as_ref().unwrap().tfs); [-w[0][3] * 100., w[2][3] * 100., w[1][3] * 100.] };
        let base = at(p[2]);
        println!("clip {d:.4} s ({:.3} frames at 60 Hz)", d * 60.);
        // The pose bank's keys (KKRuntimePoseBank.cpp: Frames = ceil(duration * 60) in floats, Step = duration / Frames,
        // key k at k * Step) and its linear blend at the dumped time, against the native pose there.
        let frames = (d * 60f32).ceil() as u32;
        let step = d / frames as f32;
        let t = p[2];
        let k = ((t / step).floor() as u32).min(frames - 1);
        let (ka, kb) = (at(step * k as f32), at(if k + 1 == frames { d } else { step * (k + 1) as f32 }));
        let w = (t - step * k as f32) / step;
        let bank_like: Vec<f32> = (0..3).map(|i| ka[i] + (kb[i] - ka[i]) * w).collect();
        let exact = at(t);
        let model: Vec<f32> = (0..3).map(|i| exact[i] - bank_like[i]).collect();
        let miss = ((model[0] - gap[0]).powi(2) + (model[1] - gap[1]).powi(2) + (model[2] - gap[2]).powi(2)).sqrt();
        println!("  bank keys: {frames} frames (60 Hz gives {}), step {step:.7} s; native - bank-like at t = {model:?}: off the gap by {miss:.4} cm",
            (d * 60.).round());
        for k in [-2i32, -1, 1, 2] {
            let o = at(p[2] + k as f32 / 120.);
            let dv = [o[0] - base[0], o[1] - base[1], o[2] - base[2]];
            let miss = ((dv[0] - gap[0]).powi(2) + (dv[1] - gap[1]).powi(2) + (dv[2] - gap[2]).powi(2)).sqrt();
            println!("  native(t {:+} / 120 s) - native(t) = {:?}: off the gap by {miss:.4} cm", k, dv);
        }
    }
}
