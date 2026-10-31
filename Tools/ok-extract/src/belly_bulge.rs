//! Belly bulge morphs for the H scene: where his penis presses out through her belly (or her back) the game swells
//! the skin there instead of letting it come through. Made once on opening a female body: eight targets
//! "belly.bulge.f0".."f3" (front) and "b0".."b3" (back), a round swell at four heights between her crotch and her
//! upper spine (0.1 to 0.7 of the way), `BULGE_M` out along the skin at weight 100. The scene blends the two nearest
//! to where his tip is (UOKHScenePanel::BulgeBelly). Skin on her breasts, arms, head and lower legs stays.
use crate::pose::{mat_inv_pub, mat_mul_pub, Mat4};
use crate::session::OkSession;
use std::collections::HashMap;

pub const HEIGHTS: usize = 4;
/// How far the skin moves at weight 100 (Unity metres).
pub const BULGE_M: f32 = 0.10;
/// Bones whose skin never swells (part of a name): a straight clip leaves her low, at the groin, where the skin is on
/// her waist and thigh bones, so those are not limited.
const STILL: [&str; 11] = ["bust", "arm", "hand", "head", "neck", "shoulder", "leg02", "leg03", "foot", "toe", "knee"];

fn point(m: &Mat4, p: [f32; 3]) -> [f32; 3] { std::array::from_fn(|i| m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3]) }
fn translation(m: &Mat4) -> [f32; 3] { [m[0][3], m[1][3], m[2][3]] }

/// The morph name of front (`back` false) or back height `k` (0 lowest).
pub fn name(back: bool, k: usize) -> String { format!("belly.bulge.{}{}", if back { 'b' } else { 'f' }, k) }

/// Adds the targets to her body part once; true when added (the skeleton export is then rebuilt with them).
pub fn add(session: &mut OkSession) -> bool {
    // Hers only (ChaFileParameter.sex 1): his body has the same cf_ bones, cf_j_kokan too.
    if session.sex != 1 { return false; }
    let Some(rest) = crate::skel_export::bind_body(session) else { return false; };
    let at = |n: &str| rest.world_tf(n).map(|m| translation(&m));
    // Her crotch to her upper spine in the bind pose (she faces +z): the heights the swell can be at.
    let (Some(kokan), Some(top)) = (at("cf_j_kokan").or_else(|| at("cf_J_Kokan")), at("cf_j_spine02")) else { return false; };
    let still: Vec<bool> = rest.names.iter().map(|n| { let n = n.to_ascii_lowercase(); STILL.iter().any(|s| n.contains(s)) }).collect();
    let mut added = false;
    for part in session.parts.iter_mut() {
        if part.role != "body" || part.skel_kind != 1 || part.runtime_accessory.is_some() { continue; }
        if part.blend_shapes.iter().any(|(n, _)| n.to_bytes().starts_with(b"belly.bulge.")) { continue; }
        let (worlds, binds) = crate::skel_export::palettes(&rest, part);
        if worlds.is_empty() { continue; }
        // Each vertex in the bind pose with its skin matrix (to bring the moved point back into the mesh) and how free it is.
        struct Vert { j: usize, p: [f32; 3], n: [f32; 3], inv: Mat4, free: f32 }
        let mut verts = Vec::new();
        for j in 0..part.rest_positions.len() {
            let (Some(indices), Some(weights)) = (part.bone_idx.get(j), part.weights.get(j)) else { continue; };
            let mut free = 1.0f32;
            let mut skin = [[0.0f32; 4]; 4];
            for (b, w) in indices.iter().zip(weights) {
                let b = *b as usize;if *w <= 0. || b >= worlds.len() { continue; }
                if still.get(b) == Some(&true) { free -= *w; }
                let m = mat_mul_pub(&worlds[b], &binds[b]);
                for r in 0..4 { for c in 0..4 { skin[r][c] += m[r][c] * w; } }
            }
            if free <= 0.01 { continue; }
            let p = point(&skin, part.rest_positions[j]);
            let o = point(&skin, [0.; 3]);
            let n = part.rest_normals.get(j).map(|v| { let v = point(&skin, *v); [v[0] - o[0], v[1] - o[1], v[2] - o[2]] }).unwrap_or([0., 0., 1.]);
            verts.push(Vert { j, p, n, inv: mat_inv_pub(&skin), free });
        }
        // Vertices split along UV / normal seams share one place: one normal and one freedom for all of them, or the swell
        // tears open along the seams.
        let key = |p: [f32; 3]| p.map(|v| (v * 5000.).round() as i64);
        let mut weld: HashMap<[i64; 3], ([f32; 3], f32, f32)> = HashMap::new();
        for v in &verts {
            let len = (v.n[0] * v.n[0] + v.n[1] * v.n[1] + v.n[2] * v.n[2]).sqrt();if len < 1e-6 { continue; }
            let e = weld.entry(key(v.p)).or_insert(([0.; 3], 0., 0.));
            for i in 0..3 { e.0[i] += v.n[i] / len; }
            e.1 += v.free;e.2 += 1.;
        }
        let mut targets: Vec<Vec<(u32, [f32; 3])>> = vec![Vec::new(); 2 * HEIGHTS];
        for v in &verts {
            let Some((sum, free, count)) = weld.get(&key(v.p)) else { continue; };
            let len = (sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]).sqrt();if len < 1e-6 { continue; }
            let n = [sum[0] / len, sum[1] / len, sum[2] / len];let free = (free / count).min(1.);
            let p = v.p;
            for back in [false, true] {
                // Facing out on that side; the swell is round across her (x) and along her (y).
                let facing = if back { -n[2] } else { n[2] };if facing < 0.15 { continue; }
                for k in 0..HEIGHTS {
                    let t = (k as f32 + 0.5) / HEIGHTS as f32;
                    let y = kokan[1] + (top[1] - kokan[1]) * (0.8 * t);
                    let dy = (p[1] - y) / 0.05;let dx = (p[0] - kokan[0]) / 0.055;
                    let fall = (-0.5 * (dx * dx + dy * dy)).exp() * facing.min(1.) * free;
                    if fall < 0.01 { continue; }
                    let moved = [p[0] + n[0] * BULGE_M * fall, p[1] + n[1] * BULGE_M * fall, p[2] + n[2] * BULGE_M * fall];
                    let local = point(&v.inv, moved);let base = part.rest_positions[v.j];
                    let d = [local[0] - base[0], local[1] - base[1], local[2] - base[2]];
                    targets[(back as usize) * HEIGHTS + k].push((v.j as u32, d));
                }
            }
        }
        for back in [false, true] {
            for k in 0..HEIGHTS {
                let deltas = std::mem::take(&mut targets[(back as usize) * HEIGHTS + k]);
                part.morph_idx.push(deltas.iter().map(|(i, _)| *i).collect());
                part.morph_dxyz.push(deltas.iter().flat_map(|(_, d)| *d).collect());
                part.blend_shapes.push((std::ffi::CString::new(name(back, k)).unwrap(), deltas));
            }
        }
        added = true;
    }
    if added { session.skel_export = None; }
    added
}
