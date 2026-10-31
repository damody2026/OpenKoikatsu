//! Belly bulge morphs for the H scene: where his penis presses out through her belly (or her back) the game swells
//! the skin there instead of letting it come through. Made once on opening a female body: a grid of round swells
//! "belly.bulge.<side><height><across>", on her front, back, left and right (f b l r), at eight heights from her upper thighs and just under her
//! crotch (her pubic mound and perineum) to her upper spine and three places across that side (front and back: her
//! middle and her groins 5.5 cm to each side; left and right: 5 cm before and behind her middle), each `BULGE_M` out
//! along the skin at weight 100. The scene asks `past` where his glans is against her skin now and drives
//! the one swell that moves that skin most (UOKHScenePanel::BulgeBelly). Skin on her breasts, arms, head and lower legs
//! stays.
use crate::pose::{mat_inv_pub, mat_mul_pub, Mat4};
use crate::session::{OkSession, SessionPart};
use std::collections::HashMap;

/// Where the swells are along her crotch-to-upper-spine line (fractions of it; below 0 is under her crotch: -0.4 and -0.2
/// her upper thighs, where his glans comes out when she lies curled on her side).
const HEIGHTS: [f32; 8] = [-0.4, -0.2, -0.05, 0.05, 0.15, 0.28, 0.42, 0.58];
/// Where across her front and back (metres from her middle), and across her sides (metres before her middle).
const ACROSS: [f32; 3] = [-0.055, 0.0, 0.055];
const ALONG_SIDE: [f32; 3] = [-0.05, 0.0, 0.05];
/// Her sides: front, back, left, right, with the way each faces (she faces +z, her left is +x).
const SIDES: [(char, [f32; 2]); 4] = [('f', [0., 1.]), ('b', [0., -1.]), ('l', [1., 0.]), ('r', [-1., 0.])];
/// How far the skin moves at weight 100 (Unity metres), and how wide a swell is (one standard deviation): a glans 8 cm
/// out of a thin girl's belly made a 4 cm wide one a pillar.
pub const BULGE_M: f32 = 0.14;
const ROUND_M: f32 = 0.055;
/// Bones whose skin never swells (part of a name): a straight clip leaves her low, at the groin, where the skin is on
/// her waist and thigh bones, so those are not limited. Her own hidden penis mesh (dan bones) is not her skin.
const STILL: [&str; 12] = ["bust", "arm", "hand", "head", "neck", "shoulder", "leg02", "leg03", "foot", "toe", "knee", "dan"];

fn point(m: &Mat4, p: [f32; 3]) -> [f32; 3] { std::array::from_fn(|i| m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3]) }
fn translation(m: &Mat4) -> [f32; 3] { [m[0][3], m[1][3], m[2][3]] }
fn is_bulge(n: &std::ffi::CString) -> bool { n.to_bytes().starts_with(b"belly.bulge.") }

/// The morph name on `side` (f b l r) at height `k` (0 lowest), place `across` (0..2 across that side).
pub fn name(side: char, k: usize, across: usize) -> String { format!("belly.bulge.{side}{k}{across}") }

/// The skin matrix of vertex `j` (palette blended by its weights) and how much of it is on bones that swell.
fn skin_of(part: &SessionPart, j: usize, worlds: &[Mat4], binds: &[Mat4], still: &[bool]) -> Option<(Mat4, f32)> {
    let (indices, weights) = (part.bone_idx.get(j)?, part.weights.get(j)?);
    let mut free = 1.0f32;
    let mut skin = [[0.0f32; 4]; 4];
    for (b, w) in indices.iter().zip(weights) {
        let b = *b as usize;if *w <= 0. || b >= worlds.len() { continue; }
        if still.get(b) == Some(&true) { free -= *w; }
        let m = mat_mul_pub(&worlds[b], &binds[b]);
        for r in 0..4 { for c in 0..4 { skin[r][c] += m[r][c] * w; } }
    }
    Some((skin, free))
}

/// Adds the targets to her body part once; true when added (the skeleton export is then rebuilt with them).
pub fn add(session: &mut OkSession) -> bool {
    // Hers only (ChaFileParameter.sex 1): his body has the same cf_ bones, cf_j_kokan too.
    if session.sex != 1 { return false; }
    let Some(rest) = crate::skel_export::bind_body(session) else { return false; };
    let at = |n: &str| rest.world_tf(n).map(|m| translation(&m));
    // Her crotch to her upper spine in the bind pose (she faces +z): the heights the swells are at.
    let (Some(kokan), Some(top)) = (at("cf_j_kokan").or_else(|| at("cf_J_Kokan")), at("cf_j_spine02")) else { return false; };
    let still: Vec<bool> = rest.names.iter().map(|n| { let n = n.to_ascii_lowercase(); STILL.iter().any(|s| n.contains(s)) }).collect();
    let mut added = false;
    for part in session.parts.iter_mut() {
        if part.role != "body" || part.skel_kind != 1 || part.runtime_accessory.is_some() { continue; }
        if part.blend_shapes.iter().any(|(n, _)| is_bulge(n)) { continue; }
        let (worlds, binds) = crate::skel_export::palettes(&rest, part);
        if worlds.is_empty() { continue; }
        // Each vertex in the bind pose with its skin matrix (to bring the moved point back into the mesh) and how free it is.
        struct Vert { j: usize, p: [f32; 3], n: [f32; 3], inv: Mat4, free: f32 }
        let mut verts = Vec::new();
        for j in 0..part.rest_positions.len() {
            let Some((skin, free)) = skin_of(part, j, &worlds, &binds, &still) else { continue; };
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
        let mut targets: Vec<(String, Vec<(u32, [f32; 3])>)> = Vec::new();
        for (side, way) in SIDES {
            for (k, h) in HEIGHTS.iter().enumerate() {
                for a in 0..3 {
                    // Front and back swells sit across her (x), side ones before and behind her middle (z, along the line up her).
                    let y = kokan[1] + (top[1] - kokan[1]) * h;
                    let (x, z) = (kokan[0] + ACROSS[a], kokan[2] + (top[2] - kokan[2]) * h + ALONG_SIDE[a]);
                    let mut deltas = Vec::new();
                    for v in &verts {
                        let Some((sum, free, count)) = weld.get(&key(v.p)) else { continue; };
                        let len = (sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]).sqrt();if len < 1e-6 { continue; }
                        let n = [sum[0] / len, sum[1] / len, sum[2] / len];let free = (free / count).min(1.);
                        // Facing out on that side; round across her (x) and along her (y).
                        let facing = n[0] * way[0] + n[2] * way[1];if facing < 0.15 { continue; }
                        let dx = (if way[0] == 0. { v.p[0] - x } else { v.p[2] - z }) / ROUND_M;let dy = (v.p[1] - y) / ROUND_M;
                        let fall = (-0.5 * (dx * dx + dy * dy)).exp() * facing.min(1.) * free;
                        if fall < 0.01 { continue; }
                        let moved = [v.p[0] + n[0] * BULGE_M * fall, v.p[1] + n[1] * BULGE_M * fall, v.p[2] + n[2] * BULGE_M * fall];
                        let local = point(&v.inv, moved);let base = part.rest_positions[v.j];
                        deltas.push((v.j as u32, [local[0] - base[0], local[1] - base[1], local[2] - base[2]]));
                    }
                    if !deltas.is_empty() { targets.push((name(side, k, a), deltas)); }
                }
            }
        }
        for (target, deltas) in targets {
            part.morph_idx.push(deltas.iter().map(|(i, _)| *i).collect());
            part.morph_dxyz.push(deltas.iter().flat_map(|(_, d)| *d).collect());
            part.blend_shapes.push((std::ffi::CString::new(target).unwrap(), deltas));
        }
        added = true;
    }
    if added { session.skel_export = None; }
    added
}

/// The end of his penis now: the vertex of his penis mesh farthest from its root (his skeleton space, Unity metres).
/// Not J_dan109: the dan bones draw in as he goes in (17.8 cm at her entrance, 5 to 9 in a loop) while his penis is drawn
/// whole (19.6 cm), so that bone is mid-shaft then.
pub fn penis_tip(session: &OkSession) -> Option<[f32; 3]> {
    let skel = session.body_skel.as_ref()?;
    let part = session.parts.iter().find(|p| crate::head_body::is_penis_mesh(&p.name))?;
    let root = translation(&skel.world_tf("cm_J_dan100_00")?);
    let (worlds, binds) = crate::skel_export::palettes(skel, part);
    if worlds.is_empty() { return None; }
    let mut best: Option<(f32, [f32; 3])> = None;
    for j in 0..part.rest_positions.len() {
        let Some((skin, _)) = skin_of(part, j, &worlds, &binds, &[]) else { continue; };
        let p = point(&skin, part.rest_positions[j]);
        let d = (p[0] - root[0]).powi(2) + (p[1] - root[1]).powi(2) + (p[2] - root[2]).powi(2);
        if best.map_or(true, |(b, _)| d > b) { best = Some((d, p)); }
    }
    best.map(|(_, p)| p)
}

/// How near his glans a skin vertex counts as where it comes through.
const NEAR_M: f32 = 0.06;
/// How far `tip` (her skeleton space, Unity metres) is outside her skin now (negative: inside): of the skin vertices any
/// swell moves within `NEAR_M` of it, in her current pose without the swell, the one it is farthest out of along its normal
/// (none that near: the nearest); how far that vertex is; the swell that moves it most and how far it does at weight 100
/// (metres). Not simply the nearest: a glans 6 cm out of her belly is often nearest a vertex below where it came through,
/// and the swell picked there bulged under it.
pub fn past(session: &OkSession, tip: [f32; 3]) -> Option<(f32, f32, String, f32)> {
    let skel = session.body_skel.as_ref()?;
    let part = session.parts.iter().find(|p| p.blend_shapes.iter().any(|(n, _)| is_bulge(n)))?;
    let (worlds, binds) = crate::skel_export::palettes(skel, part);
    if worlds.is_empty() { return None; }
    let mut seen = std::collections::HashSet::new();
    let (mut nearest, mut most_out): (Option<(f32, f32, usize)>, Option<(f32, f32, usize)>) = (None, None);
    for (_, deltas) in part.blend_shapes.iter().filter(|(n, _)| is_bulge(n)) {
        for (j, _) in deltas {
            let j = *j as usize;
            if !seen.insert(j) { continue; }
            let Some((skin, _)) = skin_of(part, j, &worlds, &binds, &[]) else { continue; };
            let p = point(&skin, part.rest_positions[j]);
            let d = [tip[0] - p[0], tip[1] - p[1], tip[2] - p[2]];
            let dist = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if dist > NEAR_M * NEAR_M && nearest.is_some_and(|(b, _, _)| b <= dist) { continue; }
            let o = point(&skin, [0.; 3]);
            let Some(n) = part.rest_normals.get(j).map(|v| { let v = point(&skin, *v); [v[0] - o[0], v[1] - o[1], v[2] - o[2]] }) else { continue; };
            let len = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();if len < 1e-6 { continue; }
            let out = (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]) / len;
            if nearest.map_or(true, |(b, _, _)| dist < b) { nearest = Some((dist, out, j)); }
            if dist <= NEAR_M * NEAR_M && most_out.map_or(true, |(_, b, _)| out > b) { most_out = Some((dist, out, j)); }
        }
    }
    let (dist, out, j) = most_out.or(nearest)?;
    let mut most: Option<(&std::ffi::CString, f32)> = None;
    for (n, deltas) in part.blend_shapes.iter().filter(|(n, _)| is_bulge(n)) {
        let Some((_, d)) = deltas.iter().find(|(i, _)| *i as usize == j) else { continue; };
        let moves = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt();
        if most.map_or(true, |(_, m)| moves > m) { most = Some((n, moves)); }
    }
    let (n, moves) = most?;
    Some((out, dist.sqrt(), n.to_string_lossy().into_owned(), moves))
}
