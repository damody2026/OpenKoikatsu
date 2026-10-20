//! Low-poly companion model written beside an exported APNG card.
//!
//! Every visible renderer is welded across its UV seams and remeshed with
//! instant-meshes. Each output corner then takes its skin weights and normal
//! from the closest source triangle, and its UV from the closest triangle of a
//! single UV island, so a low triangle never stretches across a texture seam.
//!
//! The rig is the live `SkelExport`, the same one Unreal skins with: vertices
//! sit in its rest space, inverse binds are its rest worlds and joint nodes
//! carry its current (shaped) local pose. The low model therefore deforms with
//! exactly the palette the high one uses. Output is binary glTF 2.0.

use std::collections::HashMap;
use std::path::Path;

use crate::pose::{self, Mat4};
use crate::session::OkSession;

/// Clothes and accessories together (the `budget` argument / `--budget`).
pub const DEFAULT_TRIANGLE_BUDGET: usize = 30_000;
/// All hair together. Reported only: hair is left to the crowd room's own
/// edge-collapse reduction, which targets the same 30,000.
pub const HAIR_TRIANGLE_BUDGET: usize = 30_000;

/// Rule-based part class from the session's own renderer role and shader:
/// "face" (role head: face, eyes, lines, brows, teeth, tongue), "body"
/// (role body), "legwear" (clothes_socks, clothes_panst), "hair" (hair
/// shaders or hair-named), "outfit" (everything else: clothes_* and
/// accessories).
pub fn category(part: &Part) -> &'static str {
    let role = part.role.to_ascii_lowercase();
    if role == "head" { "face" }
    else if role.starts_with("clothes_socks") || role.starts_with("clothes_panst") { "legwear" }
    else if part.role.eq_ignore_ascii_case("body") { "body" }
    else if part.hair { "hair" }
    else { "outfit" }
}
/// Parts at or below this many triangles are copied unchanged.
const KEEP_BELOW: usize = 600;
/// Retries stop once a part would keep this fraction of its source.
const KEEP_RATIO: f32 = 0.7;
/// Surface deviation (p95, area samples) a remesh may have: the smaller of
/// this fraction of the part diagonal and an absolute cap in metres. Parts
/// that looked right measured 1-3 mm; C010's broken body 21 mm.
const MAX_SURFACE_P95_OF_DIAGONAL: f32 = 0.0075;
const MAX_SURFACE_P95: f32 = 0.005;
const MIN_PART_TRIANGLES: usize = 200;
/// Remeshed boundary length over the input's (plus one diagonal of slack).
/// Clean parts sit near 1.0; shredded ones (C064 coat tails, 7.5) far above.
const MAX_BOUNDARY_GROWTH: f32 = 1.75;

pub struct Bone {
    pub name: String,
    pub parent: i32,
    pub pose: ([f32; 3], [f32; 4], [f32; 3]),
    pub rest_world: Mat4,
    pub pose_world: Mat4,
}

pub struct Part {
    pub name: String,
    pub role: String,
    pub blend: String,
    /// Rest-space positions and normals of the shared rig.
    pub ref_pos: Vec<[f32; 3]>,
    pub ref_normal: Vec<[f32; 3]>,
    pub uvs: Vec<[f32; 2]>,
    pub indices: Vec<u32>,
    pub joints: Vec<[u16; 4]>,
    pub weights: Vec<[f32; 4]>,
    pub texture: Option<Vec<u8>>,
    /// Native clothes-over mask hiding skin under worn clothes:
    /// PNG, `_AlphaMask_ST`, and the `_alpha_a` / `_alpha_b` switches.
    pub alpha_mask: Option<(Vec<u8>, [f32; 4], [bool; 2])>,
    /// Bound to one bone instead of a source skin.
    pub rigid: bool,
    /// Hair strand cards (hair shaders or hair-named renderers).
    pub hair: bool,
    /// Koikatsu clothing sub-layer: `_g` overlay, `_kazari` trim, `_ura` lining.
    pub layer: bool,
}

/// Plain copy of everything the export needs. Owns no session state, so the
/// remesh can run on a worker thread while the viewer keeps ticking.
pub struct Source {
    pub bones: Vec<Bone>,
    pub parts: Vec<Part>,
    /// Parts not exported, with the reason.
    pub skipped: Vec<(String, String)>,
}

fn mat_vec(m: &Mat4, p: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
        m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
        m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3],
    ]
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] { [a[0] - b[0], a[1] - b[1], a[2] - b[2]] }
fn add(a: [f32; 3], b: [f32; 3]) -> [f32; 3] { [a[0] + b[0], a[1] + b[1], a[2] + b[2]] }
fn scale(a: [f32; 3], s: f32) -> [f32; 3] { [a[0] * s, a[1] * s, a[2] * s] }
fn dot(a: [f32; 3], b: [f32; 3]) -> f32 { a[0] * b[0] + a[1] * b[1] + a[2] * b[2] }
fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}
fn norm(a: [f32; 3]) -> f32 { dot(a, a).sqrt() }
fn normalize(a: [f32; 3]) -> [f32; 3] {
    let l = norm(a);
    if l > 1e-12 { scale(a, 1.0 / l) } else { [0.0, 1.0, 0.0] }
}

/// Parent-first worlds; `push_bone` only accepts parents already pushed.
fn worlds(bones: &[crate::skel_export::ExportBone], posed: bool) -> Vec<Mat4> {
    let mut out: Vec<Mat4> = Vec::with_capacity(bones.len());
    for b in bones {
        let local = if posed { pose::trs(b.pose_pos, b.pose_rot, b.pose_scl) } else { pose::trs(b.rest_pos, b.rest_rot, b.rest_scl) };
        let world = if b.parent >= 0 && (b.parent as usize) < out.len() {
            pose::mat_mul_pub(&out[b.parent as usize], &local)
        } else {
            local
        };
        out.push(world);
    }
    out
}

/// Linear blend skin of rest-space points with `pose * inv(rest)`, the
/// palette Unreal builds from the same export.
fn skin(points: &[[f32; 3]], joints: &[[u16; 4]], weights: &[[f32; 4]], palette: &[Mat4]) -> Vec<[f32; 3]> {
    points.iter().enumerate().map(|(i, &p)| {
        let mut acc = [0.0; 3];
        let mut sum = 0.0;
        for k in 0..4 {
            let w = weights[i][k];
            let j = joints[i][k] as usize;
            if w <= 0.0 || j >= palette.len() { continue; }
            acc = add(acc, scale(mat_vec(&palette[j], p), w));
            sum += w;
        }
        if sum > 1e-6 { scale(acc, 1.0 / sum) } else { p }
    }).collect()
}

fn normalized_weights(w: [f32; 4], j: [u16; 4], bones: usize) -> ([u16; 4], [f32; 4]) {
    let mut joints = [0u16; 4];
    let mut weights = [0f32; 4];
    let mut sum = 0.0;
    for k in 0..4 {
        if w[k] > 1e-6 && (j[k] as usize) < bones {
            joints[k] = j[k];
            weights[k] = w[k];
            sum += w[k];
        }
    }
    if sum <= 1e-6 { return ([0; 4], [1.0, 0.0, 0.0, 0.0]); }
    for v in &mut weights { *v /= sum; }
    (joints, weights)
}

/// Copy the live session. Builds the GPU rig export when it is missing.
pub fn capture(sess: &mut OkSession) -> Result<Source, String> {
    sess.ensure_skel_export();
    let ex = sess.skel_export.as_ref().ok_or("No skeleton export")?;
    if ex.bones.is_empty() { return Err("Character has no skeleton".into()); }
    let rest = worlds(&ex.bones, false);
    let posed = worlds(&ex.bones, true);
    let bones: Vec<Bone> = ex.bones.iter().enumerate().map(|(i, b)| Bone {
        name: b.name.clone(),
        parent: b.parent,
        pose: (b.pose_pos, b.pose_rot, b.pose_scl),
        rest_world: rest[i],
        pose_world: posed[i],
    }).collect();
    let shared = |name: &str| ex.name_to_idx.get(&name.to_ascii_lowercase()).copied();
    let palette: Vec<Mat4> = (0..bones.len())
        .map(|b| pose::mat_mul_pub(&posed[b], &pose::mat_inv_pub(&rest[b]))).collect();
    let mut parts = Vec::new();
    let mut skipped = Vec::new();
    let mut ancestor_bones = 0usize;
    for (i, p) in sess.parts.iter().enumerate() {
        let n = p.positions.len();
        if !p.render_visible { skipped.push((p.name.clone(), "hidden".into())); continue; }
        // Same set the card preview keeps out of sight (OKIsGenitalMesh).
        if matches!(p.name.to_ascii_lowercase().as_str(), "o_dankon" | "o_dan_f" | "cm_o_dan00" | "cm_o_dan_f" | "o_gomu") {
            skipped.push((p.name.clone(), "hidden in the card preview".into()));
            continue;
        }
        if n == 0 || p.indices.len() < 3 { skipped.push((p.name.clone(), "empty".into())); continue; }
        if p.indices.iter().any(|&v| v as usize >= n) { skipped.push((p.name.clone(), "index outside vertices".into())); continue; }
        let uvs = if p.uvs.len() == n { p.uvs.clone() } else { vec![[0.0; 2]; n] };
        let drawn_normals = if p.normals.len() == n { p.normals.clone() } else { vertex_normals(&p.positions, &p.indices) };
        let skinned = p.weights.len() == n && p.bone_idx.len() == n;
        // Body and head skins were already renamed onto the shared rig.
        let body_skin = ex.part_bone_idx.get(i).filter(|v| skinned && matches!(p.skel_kind, 1..=3) && v.len() == n);
        // Garment and accessory rigs reuse body bone names. Private bones
        // (hair strands, chains outside the body rig) follow their nearest
        // named ancestor, then the bone the rig hangs from.
        let private = p.runtime_accessory.as_ref().and_then(|r| r.private_rig()).filter(|_| skinned && body_skin.is_none())
            .map(|(rig, parent_key)| {
                let attach = shared(parent_key).unwrap_or(0);
                rig.bone_ids.iter().zip(&rig.names).map(|(&id, name)| {
                    if let Some(b) = shared(name) { return b; }
                    ancestor_bones += 1;
                    let mut cur = rig.tfs.get(&id).and_then(|t| t.parent);
                    for _ in 0..128 {
                        let Some(tf) = cur.and_then(|c| rig.tfs.get(&c)) else { break };
                        if let Some(b) = shared(&tf.name) { return b; }
                        if tf.parent == cur { break; }
                        cur = tf.parent;
                    }
                    attach
                }).collect::<Vec<u16>>()
            });
        let part = if let Some(idx) = body_skin {
            let (joints, weights): (Vec<_>, Vec<_>) = (0..n).map(|v| merge_duplicates(idx[v], p.weights[v], bones.len())).unzip();
            rebind(p, joints, weights, uvs, &palette, &drawn_normals, false)
        } else if let Some(map) = private {
            let (joints, weights): (Vec<_>, Vec<_>) = (0..n).map(|v| {
                let mut j = [0u16; 4];
                let mut w = p.weights[v];
                for k in 0..4 {
                    match map.get(p.bone_idx[v][k] as usize) { Some(&b) => j[k] = b, None => w[k] = 0.0 }
                }
                merge_duplicates(j, w, bones.len())
            }).unzip();
            rebind(p, joints, weights, uvs, &palette, &drawn_normals, false)
        } else {
            // Unskinned sockets and props follow one bone rigidly.
            let bone = p.rigid_follow_bone.as_deref().and_then(shared)
                .or_else(|| p.parent_bone.as_deref().and_then(shared)).unwrap_or(0);
            rebind(p, vec![[bone, 0, 0, 0]; n], vec![[1.0, 0.0, 0.0, 0.0]; n], uvs, &palette, &drawn_normals, true)
        };
        if part.ref_pos.iter().flatten().any(|v| !v.is_finite()) { skipped.push((p.name.clone(), "non-finite rest positions".into())); continue; }
        parts.push(part);
    }
    if ancestor_bones > 0 {
        skipped.push(("(private bones)".into(), format!("{ancestor_bones} private rig bones follow their nearest shared ancestor")));
    }
    if parts.is_empty() { return Err("No visible parts to export".into()); }
    // A garment with stacked sub-layers (`X`, `X_g`, `X_kazari`, `X_ura`) only
    // looks right while its layers coincide; keep the whole group at source.
    let layer_bases: std::collections::HashSet<String> = parts.iter().filter(|p| p.layer).filter_map(|p| {
        let n = p.name.to_ascii_lowercase();
        ["_g", "_ura"].iter().find_map(|s| n.strip_suffix(s).map(str::to_string))
            .or_else(|| n.find("_kazari").map(|i| n[..i].to_string()))
    }).collect();
    for part in &mut parts { if layer_bases.contains(&part.name.to_ascii_lowercase()) { part.layer = true; } }
    Ok(Source { bones, parts, skipped })
}

/// Sum influences that now point at the same shared bone.
fn merge_duplicates(j: [u16; 4], w: [f32; 4], bones: usize) -> ([u16; 4], [f32; 4]) {
    let mut joints = [0u16; 4];
    let mut weights = [0f32; 4];
    let mut used = 0;
    for k in 0..4 {
        if w[k] <= 1e-6 { continue; }
        if let Some(e) = (0..used).find(|&e| joints[e] == j[k]) { weights[e] += w[k]; continue; }
        joints[used] = j[k];
        weights[used] = w[k];
        used += 1;
    }
    normalized_weights(weights, joints, bones)
}

/// Rest-space copy of what the session draws now: each vertex goes through
/// the inverse of its own blended shared palette, so skinning it with that
/// palette returns exactly the drawn position. Normals use the transpose.
fn rebind(p: &crate::session::SessionPart, joints: Vec<[u16; 4]>, weights: Vec<[f32; 4]>, uvs: Vec<[f32; 2]>,
    palette: &[Mat4], drawn_normals: &[[f32; 3]], rigid: bool) -> Part {
    let mut ref_pos = Vec::with_capacity(p.positions.len());
    let mut ref_normal = Vec::with_capacity(p.positions.len());
    for v in 0..p.positions.len() {
        let mut blend = [[0.0f32; 4]; 4];
        for k in 0..4 {
            let w = weights[v][k];
            if w <= 0.0 { continue; }
            let m = &palette[joints[v][k] as usize];
            for r in 0..4 { for c in 0..4 { blend[r][c] += m[r][c] * w; } }
        }
        let inv = pose::mat_inv_pub(&blend);
        ref_pos.push(mat_vec(&inv, p.positions[v]));
        let nrm = drawn_normals[v];
        ref_normal.push(normalize([
            blend[0][0] * nrm[0] + blend[1][0] * nrm[1] + blend[2][0] * nrm[2],
            blend[0][1] * nrm[0] + blend[1][1] * nrm[1] + blend[2][1] * nrm[2],
            blend[0][2] * nrm[0] + blend[1][2] * nrm[1] + blend[2][2] * nrm[2],
        ]));
    }
    Part { name: p.name.clone(), role: p.role.clone(), blend: p.blend.clone(), ref_pos, ref_normal, uvs,
        indices: p.indices.clone(), joints, weights, texture: p.png.clone(), alpha_mask: clothes_over_mask(p), rigid,
        hair: matches!(p.shader_kind, 1 | 10) || p.name.to_ascii_lowercase().contains("hair") || p.role.to_ascii_lowercase().contains("hair"),
        layer: { let n = p.name.to_ascii_lowercase(); n.ends_with("_g") || n.contains("_kazari") || n.ends_with("_ura") } }
}

pub(crate) fn clothes_over_mask(p: &crate::session::SessionPart) -> Option<(Vec<u8>, [f32; 4], [bool; 2])> {
    let mask = p.alpha_mask.clone()?;
    let mat = p.source_material.as_ref()?;
    let f = |name: &str, default: f32| mat.float(name).unwrap_or(default);
    let st = [f("_AlphaMask_ST_x", 1.0), f("_AlphaMask_ST_y", 1.0), f("_AlphaMask_ST_z", 0.0), f("_AlphaMask_ST_w", 0.0)];
    Some((mask, st, [f("_alpha_a", 1.0) != 0.0, f("_alpha_b", 1.0) != 0.0]))
}

/// Main texture with the clothes-over mask folded into its alpha, the same
/// test the native renderer clips with. Both PNGs use top-origin rows.
pub(crate) fn bake_alpha_mask(main: &[u8], mask: &(Vec<u8>, [f32; 4], [bool; 2])) -> Option<Vec<u8>> {
    let (png, st, [use_r, use_g]) = mask;
    if !use_r && !use_g { return None; }
    let mut main = crate::teximg::load_from_memory(main).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = main.dimensions();
    let (mw, mh) = mask.dimensions();
    for y in 0..h {
        let v = 1.0 - (y as f32 + 0.5) / h as f32;
        let mv = v * st[1] + st[3];
        let my = (((1.0 - mv) * mh as f32) as i64).clamp(0, mh as i64 - 1) as u32;
        for x in 0..w {
            let mu = (x as f32 + 0.5) / w as f32 * st[0] + st[2];
            let mx = ((mu * mw as f32) as i64).clamp(0, mw as i64 - 1) as u32;
            let m = mask.get_pixel(mx, my);
            if (*use_r && m[0] < 128) || (*use_g && m[1] < 128) { main.get_pixel_mut(x, y)[3] = 0; }
        }
    }
    let mut out = Vec::new();
    image::DynamicImage::ImageRgba8(main).write_to(&mut std::io::Cursor::new(&mut out), image::ImageFormat::Png).ok()?;
    Some(out)
}

fn vertex_normals(pos: &[[f32; 3]], idx: &[u32]) -> Vec<[f32; 3]> {
    let mut n = vec![[0.0f32; 3]; pos.len()];
    for t in idx.chunks_exact(3) {
        let (a, b, c) = (t[0] as usize, t[1] as usize, t[2] as usize);
        let f = cross(sub(pos[b], pos[a]), sub(pos[c], pos[a]));
        for v in [a, b, c] { n[v] = add(n[v], f); }
    }
    n.into_iter().map(normalize).collect()
}

// ---------------------------------------------------------------- closest point

#[derive(Clone, Copy)]
struct Node { min: [f32; 3], max: [f32; 3], start: u32, count: u32, left: u32, right: u32 }

/// Triangle BVH answering closest-point queries with barycentrics.
struct TriTree<'a> { pos: &'a [[f32; 3]], idx: &'a [u32], order: Vec<u32>, nodes: Vec<Node> }

fn closest_on_triangle(p: [f32; 3], a: [f32; 3], b: [f32; 3], c: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    // Ericson, Real-Time Collision Detection 5.1.5.
    let ab = sub(b, a); let ac = sub(c, a); let ap = sub(p, a);
    let d1 = dot(ab, ap); let d2 = dot(ac, ap);
    if d1 <= 0.0 && d2 <= 0.0 { return (a, [1.0, 0.0, 0.0]); }
    let bp = sub(p, b); let d3 = dot(ab, bp); let d4 = dot(ac, bp);
    if d3 >= 0.0 && d4 <= d3 { return (b, [0.0, 1.0, 0.0]); }
    let vc = d1 * d4 - d3 * d2;
    if vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 {
        let v = d1 / (d1 - d3);
        return (add(a, scale(ab, v)), [1.0 - v, v, 0.0]);
    }
    let cp = sub(p, c); let d5 = dot(ab, cp); let d6 = dot(ac, cp);
    if d6 >= 0.0 && d5 <= d6 { return (c, [0.0, 0.0, 1.0]); }
    let vb = d5 * d2 - d1 * d6;
    if vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 {
        let w = d2 / (d2 - d6);
        return (add(a, scale(ac, w)), [1.0 - w, 0.0, w]);
    }
    let va = d3 * d6 - d5 * d4;
    if va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0 {
        let w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return (add(b, scale(sub(c, b), w)), [0.0, 1.0 - w, w]);
    }
    let denom = va + vb + vc;
    if denom.abs() < 1e-30 { return (a, [1.0, 0.0, 0.0]); }
    let v = vb / denom; let w = vc / denom;
    (add(a, add(scale(ab, v), scale(ac, w))), [1.0 - v - w, v, w])
}

impl<'a> TriTree<'a> {
    fn new(pos: &'a [[f32; 3]], idx: &'a [u32]) -> Self {
        let tris = idx.len() / 3;
        let centroid: Vec<[f32; 3]> = (0..tris).map(|t| {
            let (a, b, c) = (pos[idx[t * 3] as usize], pos[idx[t * 3 + 1] as usize], pos[idx[t * 3 + 2] as usize]);
            scale(add(a, add(b, c)), 1.0 / 3.0)
        }).collect();
        let mut tree = TriTree { pos, idx, order: (0..tris as u32).collect(), nodes: Vec::new() };
        if tris > 0 { tree.build(0, tris, &centroid); }
        tree
    }

    fn bounds(&self, start: usize, end: usize) -> ([f32; 3], [f32; 3]) {
        let mut lo = [f32::MAX; 3]; let mut hi = [f32::MIN; 3];
        for &t in &self.order[start..end] {
            for k in 0..3 {
                let p = self.pos[self.idx[t as usize * 3 + k] as usize];
                for a in 0..3 { lo[a] = lo[a].min(p[a]); hi[a] = hi[a].max(p[a]); }
            }
        }
        (lo, hi)
    }

    fn build(&mut self, start: usize, end: usize, centroid: &[[f32; 3]]) -> u32 {
        let (min, max) = self.bounds(start, end);
        let id = self.nodes.len() as u32;
        self.nodes.push(Node { min, max, start: start as u32, count: (end - start) as u32, left: 0, right: 0 });
        if end - start <= 4 { return id; }
        let ext = sub(max, min);
        let axis = if ext[0] >= ext[1] && ext[0] >= ext[2] { 0 } else if ext[1] >= ext[2] { 1 } else { 2 };
        let mid = (start + end) / 2;
        self.order[start..end].select_nth_unstable_by(mid - start, |a, b|
            centroid[*a as usize][axis].total_cmp(&centroid[*b as usize][axis]));
        let left = self.build(start, mid, centroid);
        let right = self.build(mid, end, centroid);
        self.nodes[id as usize].left = left;
        self.nodes[id as usize].right = right;
        self.nodes[id as usize].count = 0;
        id
    }

    fn box_dist2(n: &Node, p: [f32; 3]) -> f32 {
        let mut d = 0.0;
        for a in 0..3 {
            let v = if p[a] < n.min[a] { n.min[a] - p[a] } else if p[a] > n.max[a] { p[a] - n.max[a] } else { 0.0 };
            d += v * v;
        }
        d
    }

    /// (triangle, barycentrics, squared distance) of the closest accepted triangle.
    fn closest(&self, p: [f32; 3], accept: &dyn Fn(usize) -> bool) -> Option<(usize, [f32; 3], f32)> {
        if self.nodes.is_empty() { return None; }
        let mut best: Option<(usize, [f32; 3], f32)> = None;
        let mut stack = vec![0u32];
        while let Some(id) = stack.pop() {
            let n = self.nodes[id as usize];
            let bound = best.map_or(f32::MAX, |b| b.2);
            if Self::box_dist2(&n, p) > bound { continue; }
            if n.count > 0 {
                for &t in &self.order[n.start as usize..(n.start + n.count) as usize] {
                    let t = t as usize;
                    if !accept(t) { continue; }
                    let (a, b, c) = (self.pos[self.idx[t * 3] as usize], self.pos[self.idx[t * 3 + 1] as usize], self.pos[self.idx[t * 3 + 2] as usize]);
                    let (q, bary) = closest_on_triangle(p, a, b, c);
                    let d = dot(sub(q, p), sub(q, p));
                    if best.map_or(true, |b| d < b.2) { best = Some((t, bary, d)); }
                }
            } else {
                let (left, right) = (n.left as usize, n.right as usize);
                let (dl, dr) = (Self::box_dist2(&self.nodes[left], p), Self::box_dist2(&self.nodes[right], p));
                if dl < dr { stack.push(right as u32); stack.push(left as u32); } else { stack.push(left as u32); stack.push(right as u32); }
            }
        }
        best
    }
}

// ---------------------------------------------------------------- remesh

pub struct LowPart {
    pub name: String,
    pub source_triangles: usize,
    pub positions: Vec<[f32; 3]>,
    pub normals: Vec<[f32; 3]>,
    pub uvs: Vec<[f32; 2]>,
    pub joints: Vec<[u16; 4]>,
    pub weights: Vec<[f32; 4]>,
    pub indices: Vec<u32>,
    /// "remeshed", "kept", or "kept: <reason>".
    pub method: String,
    /// Source triangles copied in where the remesh left the surface bare.
    pub patched: usize,
    /// Per low vertex: source triangle and barycentrics for geometry and
    /// skin, then the UV-island triangle and barycentrics for texture
    /// coordinates. Empty for kept parts. Lets a runtime that owns its own
    /// source streams rebuild the same low mesh (`.lowpoly.okmap`).
    pub map: Vec<(u32, [f32; 3], u32, [f32; 3])>,
    /// Outward offset in metres along the source normal per low vertex, so
    /// no low triangle sinks under its source surface (layer order).
    pub offsets: Vec<f32>,
}

/// UV islands are the vertex-connected components of the unwelded source:
/// every UV seam already splits vertices there.
fn islands(n: usize, idx: &[u32]) -> Vec<u32> {
    let mut parent: Vec<u32> = (0..n as u32).collect();
    fn find(p: &mut [u32], mut x: u32) -> u32 {
        while p[x as usize] != x { p[x as usize] = p[p[x as usize] as usize]; x = p[x as usize]; }
        x
    }
    for t in idx.chunks_exact(3) {
        for k in 1..3 {
            let (a, b) = (find(&mut parent, t[0]), find(&mut parent, t[k]));
            if a != b { parent[a as usize] = b; }
        }
    }
    let tris = idx.len() / 3;
    (0..tris).map(|t| find(&mut parent, idx[t * 3])).collect()
}

fn weld(pos: &[[f32; 3]], idx: &[u32]) -> (Vec<[f32; 3]>, Vec<u32>) {
    let eps = (diagonal(pos) * 1e-6).max(1e-7);
    let mut map: HashMap<[i64; 3], u32> = HashMap::new();
    let mut out = Vec::new();
    // Only vertices this renderer draws; submeshes can share one buffer.
    let mut remap = vec![u32::MAX; pos.len()];
    for &v in idx {
        let v = v as usize;
        if remap[v] != u32::MAX { continue; }
        let p = pos[v];
        let key = [(p[0] / eps).round() as i64, (p[1] / eps).round() as i64, (p[2] / eps).round() as i64];
        remap[v] = *map.entry(key).or_insert_with(|| { out.push(p); out.len() as u32 - 1 });
    }
    // Double-sided garments repeat each triangle with reversed winding; the
    // welded copy would be non-manifold. Keep one side, glTF draws both.
    let mut seen = std::collections::HashSet::new();
    let mut faces = Vec::with_capacity(idx.len());
    for t in idx.chunks_exact(3) {
        let (a, b, c) = (remap[t[0] as usize], remap[t[1] as usize], remap[t[2] as usize]);
        if a == b || b == c || a == c { continue; }
        let mut key = [a, b, c];
        key.sort_unstable();
        if seen.insert(key) { faces.extend([a, b, c]); }
    }
    (out, faces)
}

fn run_instant_meshes(pos: &[[f32; 3]], idx: &[u32], triangles: usize) -> Result<(Vec<[f32; 3]>, Vec<u32>), String> {
    use instant_meshes::math::Vec3;
    use instant_meshes::mesh::{Faces, Surface};
    instant_meshes::set_verbose(std::env::var_os("OK_LOWPOLY_REMESH_LOG").is_some());
    let mut faces = Faces::new(3, idx.len() / 3);
    faces.idx.copy_from_slice(idx);
    let surface = Surface { faces, vertices: pos.iter().map(|p| Vec3::new(p[0], p[1], p[2])).collect(), normals: Vec::new() };
    // Settings chosen on four crowd cards (Docs/APNG_LOWPOLY.md): direct
    // triangle output (-r 6 -p 6), no Laplacian smoothing (-S 0, smoothing
    // shrinks thin open sheets), 30 degree creases (-c 30), boundary aligned
    // (-b) and deterministic (-d). Triangle mode targets faces directly.
    let options = instant_meshes::Options {
        rosy: 6,
        posy: 3,
        face_count: triangles.max(8) as i32,
        crease_angle: 30.0,
        smooth_iter: 0,
        align_to_boundaries: true,
        // Close interior holes of 7+ edges and pull boundaries back onto the
        // input boundary, so seams between parts meet (both default on).
        fill_holes: true,
        snap_boundary: true,
        deterministic: true,
        ..Default::default()
    };
    let mesh = std::panic::catch_unwind(|| instant_meshes::remesh(&surface, &options))
        .map_err(|_| "remesher panicked".to_string())??;
    let out_pos: Vec<[f32; 3]> = mesh.vertices.iter().map(|v| [v.x, v.y, v.z]).collect();
    let mut out_idx = Vec::new();
    let degree = mesh.faces.degree;
    for f in 0..mesh.faces.cols() {
        let face = mesh.faces.face(f);
        if face.iter().any(|&v| v as usize >= out_pos.len()) { continue; }
        // Quad output marks triangles by repeating an index; triangles pass through.
        let mut poly: Vec<u32> = Vec::with_capacity(degree);
        for &v in face { if !poly.contains(&v) { poly.push(v); } }
        for k in 1..poly.len().saturating_sub(1) {
            let (a, b, c) = (poly[0], poly[k], poly[k + 1]);
            if a != b && b != c && a != c { out_idx.extend([a, b, c]); }
        }
    }
    if out_idx.is_empty() { return Err("remesher produced no faces".into()); }
    Ok((out_pos, out_idx))
}

fn kept(part: &Part, reason: &str) -> LowPart {
    LowPart {
        name: part.name.clone(), source_triangles: part.indices.len() / 3, positions: part.ref_pos.clone(),
        normals: part.ref_normal.clone(), uvs: part.uvs.clone(), joints: part.joints.clone(),
        weights: part.weights.clone(), indices: part.indices.clone(),
        method: if reason.is_empty() { "kept".into() } else { format!("kept: {reason}") }, patched: 0, map: Vec::new(), offsets: Vec::new(),
    }
}

fn blend_weights(part: &Part, tri: usize, bary: [f32; 3], bones: usize) -> ([u16; 4], [f32; 4]) {
    let mut acc: Vec<(u16, f32)> = Vec::with_capacity(12);
    for c in 0..3 {
        let v = part.indices[tri * 3 + c] as usize;
        for k in 0..4 {
            let w = part.weights[v][k] * bary[c];
            if w <= 0.0 { continue; }
            let j = part.joints[v][k];
            match acc.iter_mut().find(|e| e.0 == j) { Some(e) => e.1 += w, None => acc.push((j, w)) }
        }
    }
    acc.sort_by(|a, b| b.1.total_cmp(&a.1).then(a.0.cmp(&b.0)));
    acc.truncate(4);
    let mut joints = [0u16; 4]; let mut weights = [0f32; 4];
    for (k, (j, w)) in acc.into_iter().enumerate() { joints[k] = j; weights[k] = w; }
    normalized_weights(weights, joints, bones)
}

fn interp3(v: &[[f32; 3]], idx: &[u32], tri: usize, b: [f32; 3]) -> [f32; 3] {
    let (p, q, r) = (v[idx[tri * 3] as usize], v[idx[tri * 3 + 1] as usize], v[idx[tri * 3 + 2] as usize]);
    add(scale(p, b[0]), add(scale(q, b[1]), scale(r, b[2])))
}

fn interp2(v: &[[f32; 2]], idx: &[u32], tri: usize, b: [f32; 3]) -> [f32; 2] {
    let (p, q, r) = (v[idx[tri * 3] as usize], v[idx[tri * 3 + 1] as usize], v[idx[tri * 3 + 2] as usize]);
    [p[0] * b[0] + q[0] * b[1] + r[0] * b[2], p[1] * b[0] + q[1] * b[1] + r[1] * b[2]]
}

/// Carry skin, normal and UV from the source onto the remeshed surface.
fn transfer(part: &Part, low_pos: &[[f32; 3]], low_idx: &[u32], bones: usize) -> LowPart {
    let tree = TriTree::new(&part.ref_pos, &part.indices);
    let island = islands(part.ref_pos.len(), &part.indices);
    let any = |_: usize| true;
    // Position-space attributes are shared by every copy of a low vertex, so
    // splitting it for UVs can never open a crack under deformation.
    let base: Vec<(usize, [f32; 3])> = low_pos.iter()
        .map(|&p| tree.closest(p, &any).map(|(t, b, _)| (t, b)).unwrap_or((0, [1.0, 0.0, 0.0]))).collect();
    let mut out = LowPart {
        name: part.name.clone(), source_triangles: part.indices.len() / 3, positions: Vec::new(), normals: Vec::new(),
        uvs: Vec::new(), joints: Vec::new(), weights: Vec::new(), indices: Vec::with_capacity(low_idx.len()), method: "remeshed".into(),
        patched: 0, map: Vec::new(), offsets: Vec::new(),
    };
    // Orient each connected low component by area-weighted vote against the
    // source normals, then shade with the low mesh's own smooth normals: on
    // double-sided sources the closest normal may come from either layer.
    let mut low_idx = low_idx.to_vec();
    let component = islands(low_pos.len(), &low_idx);
    // Source winding against its own normals (Unity data is not guaranteed
    // to follow one convention); low triangles copy whichever the source uses.
    let convention = part.indices.chunks_exact(3).map(|t| {
        let (a, b, c) = (part.ref_pos[t[0] as usize], part.ref_pos[t[1] as usize], part.ref_pos[t[2] as usize]);
        let n = add(part.ref_normal[t[0] as usize], add(part.ref_normal[t[1] as usize], part.ref_normal[t[2] as usize]));
        dot(cross(sub(b, a), sub(c, a)), n)
    }).sum::<f32>().signum();
    let mut vote: HashMap<u32, f32> = HashMap::new();
    for (f, t) in low_idx.chunks_exact(3).enumerate() {
        let (a, b, c) = (low_pos[t[0] as usize], low_pos[t[1] as usize], low_pos[t[2] as usize]);
        let face = cross(sub(b, a), sub(c, a));
        let centre = scale(add(a, add(b, c)), 1.0 / 3.0);
        if let Some((s, bary, _)) = tree.closest(centre, &any) {
            *vote.entry(component[f]).or_default() += dot(face, interp3(&part.ref_normal, &part.indices, s, bary)).signum() * norm(face);
        }
    }
    for (f, t) in low_idx.chunks_exact_mut(3).enumerate() {
        if vote.get(&component[f]).copied().unwrap_or(0.0) * convention < 0.0 { t.swap(1, 2); }
    }
    let low_normals = vertex_normals(low_pos, &low_idx);
    let mut copies: HashMap<(u32, u32), u32> = HashMap::new();
    for t in low_idx.chunks_exact(3) {
        let centre = scale(add(low_pos[t[0] as usize], add(low_pos[t[1] as usize], low_pos[t[2] as usize])), 1.0 / 3.0);
        let home = tree.closest(centre, &any).map(|r| island[r.0]).unwrap_or(0);
        for &v in t {
            let key = (v, home);
            if let Some(&i) = copies.get(&key) { out.indices.push(i); continue; }
            let (tri, bary) = base[v as usize];
            let (uv_tri, uv_bary) = if island[tri] == home { (tri, bary) } else {
                tree.closest(low_pos[v as usize], &|s| island[s] == home).map(|(s, b, _)| (s, b)).unwrap_or((tri, bary))
            };
            let (joints, weights) = blend_weights(part, tri, bary, bones);
            out.positions.push(low_pos[v as usize]);
            out.normals.push(low_normals[v as usize]);
            out.uvs.push(interp2(&part.uvs, &part.indices, uv_tri, uv_bary));
            out.joints.push(joints);
            out.weights.push(weights);
            out.map.push((tri as u32, bary, uv_tri as u32, uv_bary));
            let i = out.positions.len() as u32 - 1;
            copies.insert(key, i);
            out.indices.push(i);
        }
    }
    out
}

/// Points spread evenly by area: stratified over the cumulative triangle
/// area, golden-ratio barycentrics. Vertex sets over-weight dense details
/// such as fingers; area samples weigh every square centimetre alike.
fn surface_samples(pos: &[[f32; 3]], idx: &[u32], count: usize) -> Vec<[f32; 3]> {
    let tris = idx.len() / 3;
    let corner = |t: usize, k: usize| pos[idx[t * 3 + k] as usize];
    let mut cumulative = Vec::with_capacity(tris);
    let mut total = 0.0f64;
    for t in 0..tris {
        total += 0.5 * norm(cross(sub(corner(t, 1), corner(t, 0)), sub(corner(t, 2), corner(t, 0)))) as f64;
        cumulative.push(total);
    }
    if tris == 0 || total <= 0.0 { return Vec::new(); }
    (0..count).map(|k| {
        let target = (k as f64 + 0.5) / count as f64 * total;
        let t = cumulative.partition_point(|&c| c < target).min(tris - 1);
        let (mut u, mut v) = ((k as f32 * 0.618_034).fract(), (k as f32 * 0.754_877_7).fract());
        if u + v > 1.0 { u = 1.0 - u; v = 1.0 - v; }
        add(corner(t, 0), add(scale(sub(corner(t, 1), corner(t, 0)), u), scale(sub(corner(t, 2), corner(t, 0)), v)))
    }).collect()
}

/// Symmetric surface deviation between the low part and its source, in the
/// space the points are given in: (p95, max) over area samples of both.
fn deviation(a_pos: &[[f32; 3]], a_idx: &[u32], b_pos: &[[f32; 3]], b_idx: &[u32]) -> (f32, f32) {
    let ta = TriTree::new(a_pos, a_idx);
    let tb = TriTree::new(b_pos, b_idx);
    let any = |_: usize| true;
    let count = |idx: &[u32]| (idx.len() / 3 * 2).clamp(2_000, 20_000);
    let mut d: Vec<f32> = surface_samples(b_pos, b_idx, count(b_idx)).into_iter().filter_map(|p| ta.closest(p, &any).map(|r| r.2.sqrt()))
        .chain(surface_samples(a_pos, a_idx, count(a_idx)).into_iter().filter_map(|p| tb.closest(p, &any).map(|r| r.2.sqrt()))).collect();
    if d.is_empty() { return (0.0, 0.0); }
    d.sort_by(|a, b| a.total_cmp(b));
    (d[(d.len() * 95 / 100).min(d.len() - 1)], *d.last().unwrap())
}

fn diagonal(pos: &[[f32; 3]]) -> f32 {
    let mut lo = [f32::MAX; 3]; let mut hi = [f32::MIN; 3];
    for p in pos { for a in 0..3 { lo[a] = lo[a].min(p[a]); hi[a] = hi[a].max(p[a]); } }
    norm(sub(hi, lo))
}

pub struct Report {
    pub json: serde_json::Value,
}

fn remesh_part(part: &Part, target: usize, bones: usize) -> LowPart {
    let started = std::time::Instant::now();
    let low = remesh_part_inner(part, target, bones);
    // OK_LOWPOLY_TIMING=1 lists every part, for finding slow inputs.
    if std::env::var_os("OK_LOWPOLY_TIMING").is_some() {
        eprintln!("lowpoly part {} triangles={} target={} -> {} {} {:.1}s", part.name, part.indices.len() / 3, target,
            low.indices.len() / 3, low.method, started.elapsed().as_secs_f32());
    }
    low
}

fn remesh_part_inner(part: &Part, target: usize, bones: usize) -> LowPart {
    let t = part.indices.len() / 3;
    if t <= KEEP_BELOW { return kept(part, ""); }
    // Thin trim strips dissolve, and overlay or lining layers show through or
    // smear once their surface moves; leave them to edge collapse as well.
    if part.layer { return kept(part, "layered garment (base with _g/_kazari/_ura), left to edge-collapse reduction"); }
    let (wpos, widx) = weld(&part.ref_pos, &part.indices);
    let diag = diagonal(&wpos);
    let mut attempt = target;
    let mut last_error = String::new();
    // Never accept a coarse result for budget's sake: double the target until
    // the part passes, up to KEEP_RATIO of its source, else keep the source.
    // (C010's 88k-triangle chokers left the body a 424-triangle share.)
    let limit = (diag * MAX_SURFACE_P95_OF_DIAGONAL).min(MAX_SURFACE_P95);
    loop {
        let trace = std::env::var_os("OK_LOWPOLY_TIMING").is_some();
        if trace { eprintln!("lowpoly stage {} remesh target={attempt}", part.name); }
        match run_instant_meshes(&wpos, &widx, attempt).map(|(p, i)| { if trace { eprintln!("lowpoly stage {} cleanup", part.name); } drop_off_surface(part, p, i) }) {
            Ok((p, i)) if !i.is_empty() && p.iter().flatten().all(|v| v.is_finite()) => {
                if trace { eprintln!("lowpoly stage {} transfer", part.name); }
                let mut low = transfer(part, &p, &i, bones);
                if trace { eprintln!("lowpoly stage {} patch", part.name); }
                patch_uncovered(part, &mut low);
                if trace { eprintln!("lowpoly stage {} settle", part.name); }
                settle(part, &mut low);
                let (p95, _) = deviation(&low.positions, &low.indices, &part.ref_pos, &part.indices);
                // A shredded remesh still hugs the surface, so distance alone
                // misses it; its boundary length explodes instead.
                let growth = boundary_growth(part, &low);
                if p95 <= limit && growth <= MAX_BOUNDARY_GROWTH && low.indices.len() < part.indices.len() { return low; }
                last_error = format!("surface p95 {p95:.4} (limit {limit:.4}), boundary growth {growth:.2} (limit {MAX_BOUNDARY_GROWTH}), {} of {} triangles",
                    low.indices.len() / 3, t);
            }
            Ok(_) => last_error = "no usable faces".into(),
            Err(e) => last_error = e,
        }
        let ceiling = (t as f32 * KEEP_RATIO) as usize;
        if attempt >= ceiling { break; }
        attempt = (attempt * 2).min(ceiling);
    }
    kept(part, &last_error)
}

/// Hole filling can bridge separate strands or sheets and leave faces, or
/// stray vertices, far off the source. Drop any low triangle whose corners or
/// centre lie more than two mean low edges from the source surface.
/// Total length of boundary edges, seam copies merged by position. A
/// shredded remesh multiplies it; a clean one keeps close to the source.
fn boundary_length(pos: &[[f32; 3]], idx: &[u32]) -> f32 {
    let eps = (diagonal(pos) * 1e-6).max(1e-7);
    let mut key: HashMap<[i64; 3], u32> = HashMap::new();
    let mut wpos = Vec::new();
    let remap: Vec<u32> = pos.iter().map(|p| *key.entry([(p[0] / eps).round() as i64, (p[1] / eps).round() as i64, (p[2] / eps).round() as i64])
        .or_insert_with(|| { wpos.push(*p); wpos.len() as u32 - 1 })).collect();
    let mut count: HashMap<(u32, u32), u32> = HashMap::new();
    for t in idx.chunks_exact(3) {
        for k in 0..3 {
            let (a, b) = (remap[t[k] as usize], remap[t[(k + 1) % 3] as usize]);
            if a != b { *count.entry((a.min(b), a.max(b))).or_default() += 1; }
        }
    }
    count.iter().filter(|(_, c)| **c == 1).map(|((a, b), _)| norm(sub(wpos[*a as usize], wpos[*b as usize]))).sum()
}

/// Low boundary length over the remesher input's (welded, one side of a
/// double-sided garment), plus one part diagonal of slack so tiny or closed
/// sources are not judged on a near-zero boundary.
fn boundary_growth(part: &Part, low: &LowPart) -> f32 {
    let (wpos, widx) = weld(&part.ref_pos, &part.indices);
    let unpatched = &low.indices[..low.indices.len() - low.patched * 3];
    (boundary_length(&low.positions, unpatched) + diagonal(&wpos)) / (boundary_length(&wpos, &widx) + diagonal(&wpos))
}

fn drop_off_surface(part: &Part, pos: Vec<[f32; 3]>, idx: Vec<u32>) -> (Vec<[f32; 3]>, Vec<u32>) {
    let tris = idx.len() / 3;
    if tris == 0 { return (pos, idx); }
    let edge = idx.chunks_exact(3).map(|t| (0..3).map(|k| norm(sub(pos[t[k] as usize], pos[t[(k + 1) % 3] as usize]))).sum::<f32>())
        .sum::<f32>() / (tris * 3) as f32;
    let tolerance = (edge * 2.0).powi(2);
    let tree = TriTree::new(&part.ref_pos, &part.indices);
    let near = |p: [f32; 3]| tree.closest(p, &|_| true).is_some_and(|r| r.2 <= tolerance);
    let vertex_ok: Vec<bool> = pos.iter().map(|&p| near(p)).collect();
    let kept: Vec<u32> = idx.chunks_exact(3).filter(|t| {
        t.iter().all(|&v| vertex_ok[v as usize])
            && near(scale(add(pos[t[0] as usize], add(pos[t[1] as usize], pos[t[2] as usize])), 1.0 / 3.0))
    }).flatten().copied().collect();
    (pos, kept)
}

/// Instant Meshes drops pieces thinner than its edge length (loose hair
/// strands, collar tips). Copy back every source triangle the low surface
/// does not reach, with its own UVs and skin, so nothing goes missing.
fn patch_uncovered(part: &Part, low: &mut LowPart) {
    let tris = low.indices.len() / 3;
    if tris == 0 { return; }
    let edge = low.indices.chunks_exact(3).map(|t| (0..3).map(|k| norm(sub(low.positions[t[k] as usize], low.positions[t[(k + 1) % 3] as usize]))).sum::<f32>())
        .sum::<f32>() / (tris * 3) as f32;
    let tolerance = (edge * 1.5).powi(2);
    let uncovered: Vec<(usize, &[u32])> = {
        let tree = TriTree::new(&low.positions, &low.indices);
        let far = |p: [f32; 3]| tree.closest(p, &|_| true).map_or(true, |r| r.2 > tolerance);
        part.indices.chunks_exact(3).enumerate().filter(|(_, t)| {
            let corners = [part.ref_pos[t[0] as usize], part.ref_pos[t[1] as usize], part.ref_pos[t[2] as usize]];
            far(scale(add(corners[0], add(corners[1], corners[2])), 1.0 / 3.0)) || corners.iter().any(|&c| far(c))
        }).collect()
    };
    let mut copied: HashMap<u32, u32> = HashMap::new();
    for (tri, t) in uncovered {
        for (corner, &v) in t.iter().enumerate() {
            let i = *copied.entry(v).or_insert_with(|| {
                let mut bary = [0.0; 3];
                bary[corner] = 1.0;
                low.map.push((tri as u32, bary, tri as u32, bary));
                let v = v as usize;
                low.positions.push(part.ref_pos[v]);
                low.normals.push(part.ref_normal[v]);
                low.uvs.push(part.uvs[v]);
                low.joints.push(part.joints[v]);
                low.weights.push(part.weights[v]);
                low.positions.len() as u32 - 1
            });
            low.indices.push(i);
        }
        low.patched += 1;
    }
}

/// Put every low vertex on the source surface (what the crowd room
/// reconstructs from the map), then lift it along the source normal until no
/// low triangle dips under the source. Chords of a curved sheet otherwise sit
/// a little inside it and let the next layer in (lining, body) show through.
fn settle(part: &Part, low: &mut LowPart) {
    let n = low.positions.len();
    if low.map.len() != n { return; }
    let snapped: Vec<[f32; 3]> = low.map.iter().map(|(t, b, _, _)| interp3(&part.ref_pos, &part.indices, *t as usize, *b)).collect();
    let normals: Vec<[f32; 3]> = low.map.iter().map(|(t, b, _, _)| normalize(interp3(&part.ref_normal, &part.indices, *t as usize, *b))).collect();
    let tree = TriTree::new(&part.ref_pos, &part.indices);
    let tris = low.indices.len() / 3;
    let edge = if tris == 0 { 0.0 } else { low.indices.chunks_exact(3).map(|t| (0..3).map(|k| norm(sub(snapped[t[k] as usize], snapped[t[(k + 1) % 3] as usize]))).sum::<f32>()).sum::<f32>() / (tris * 3) as f32 };
    let mut offsets = vec![0.0f32; n];
    for t in low.indices.chunks_exact(3) {
        let c = [snapped[t[0] as usize], snapped[t[1] as usize], snapped[t[2] as usize]];
        let samples = [scale(add(c[0], add(c[1], c[2])), 1.0 / 3.0), scale(add(c[0], c[1]), 0.5), scale(add(c[1], c[2]), 0.5), scale(add(c[2], c[0]), 0.5)];
        let mut need = 0.0f32;
        for q in samples {
            if let Some((s, b, _)) = tree.closest(q, &|_| true) {
                let surface = interp3(&part.ref_pos, &part.indices, s, b);
                let normal = normalize(interp3(&part.ref_normal, &part.indices, s, b));
                need = need.max(-dot(sub(q, surface), normal));
            }
        }
        for &v in t { offsets[v as usize] = offsets[v as usize].max(need); }
    }
    // UV-seam copies of one low vertex share its source point; give them one
    // offset, or the seam opens into a crack.
    let mut shared: HashMap<(u32, [u32; 3]), f32> = HashMap::new();
    let key = |v: usize| { let (t, b, _, _) = low.map[v]; (t, [b[0].to_bits(), b[1].to_bits(), b[2].to_bits()]) };
    for v in 0..n { let e = shared.entry(key(v)).or_insert(0.0); *e = e.max(offsets[v]); }
    for v in 0..n { offsets[v] = shared[&key(v)]; }
    // The body is the innermost layer: lifting it would push skin through
    // tight clothes (C010 socks), so it stays on its own surface.
    if category(part) == "body" { offsets.iter_mut().for_each(|o| *o = 0.0); }
    for (v, o) in offsets.iter_mut().enumerate() {
        *o = o.min(edge * 0.5);
        low.positions[v] = add(snapped[v], scale(normals[v], *o));
    }
    low.offsets = offsets;
}

/// Every bone turned 25 degrees about its own axis on top of the export pose.
/// Wrong or misplaced weights tear the low surface away from the source here,
/// where the export pose alone would hide them.
fn probe_palette(source: &Source) -> Vec<Mat4> {
    let mut worlds: Vec<Mat4> = Vec::with_capacity(source.bones.len());
    for (i, b) in source.bones.iter().enumerate() {
        let axis = normalize([((i * 7 + 1) % 5) as f32 - 2.0, ((i * 3 + 2) % 5) as f32 - 1.5, ((i * 11 + 3) % 5) as f32 - 2.0]);
        let half = 25f32.to_radians() * 0.5;
        let r = [axis[0] * half.sin(), axis[1] * half.sin(), axis[2] * half.sin(), half.cos()];
        let q = b.pose.1;
        // q * r: the extra turn happens in the bone's own frame.
        let rot = [
            q[3] * r[0] + q[0] * r[3] + q[1] * r[2] - q[2] * r[1],
            q[3] * r[1] - q[0] * r[2] + q[1] * r[3] + q[2] * r[0],
            q[3] * r[2] + q[0] * r[1] - q[1] * r[0] + q[2] * r[3],
            q[3] * r[3] - q[0] * r[0] - q[1] * r[1] - q[2] * r[2],
        ];
        let local = pose::trs(b.pose.0, rot, b.pose.2);
        let world = if b.parent >= 0 && (b.parent as usize) < worlds.len() { pose::mat_mul_pub(&worlds[b.parent as usize], &local) } else { local };
        worlds.push(world);
    }
    worlds.iter().zip(&source.bones).map(|(w, b)| pose::mat_mul_pub(w, &pose::mat_inv_pub(&b.rest_world))).collect()
}

/// Remesh every part to share `budget` triangles and check the result.
/// Remesh by class: face, body, socks and hair keep their source; clothes
/// and accessories share `budget` by triangle count. Every remesh still has
/// to pass the quality gates (else more triangles, else the source), so the
/// outfit class may end above its budget; the report says so.
pub fn build(source: &Source, budget: usize) -> (Vec<LowPart>, Report) {
    use rayon::prelude::*;
    let tris: Vec<usize> = source.parts.iter().map(|p| p.indices.len() / 3).collect();
    let bones = source.bones.len();
    let class: Vec<&'static str> = source.parts.iter().map(category).collect();
    // Share of a pooled class: parts kept anyway (tiny, layered) use their
    // budget first; the rest split what remains by triangle count.
    let share = |name: &str, limit: usize| {
        let fixed: usize = (0..tris.len()).filter(|&k| class[k] == name && (tris[k] <= KEEP_BELOW || source.parts[k].layer)).map(|k| tris[k]).sum();
        let flexible: usize = (0..tris.len()).filter(|&k| class[k] == name && tris[k] > KEEP_BELOW && !source.parts[k].layer).map(|k| tris[k]).sum();
        if fixed + flexible <= limit || flexible == 0 { 1.0 } else { (limit.saturating_sub(fixed) as f32 / flexible as f32).min(1.0) }
    };
    let outfit_share = share("outfit", budget);
    let lows: Vec<LowPart> = source.parts.par_iter().enumerate().map(|(k, part)| {
        let t = tris[k];
        let ratio = match class[k] {
            "face" => return kept(part, "face is not reduced"),
            // OK_LOWPOLY_REMESH_LEGWEAR=1 (experiment): remesh socks and
            // stockings to half, still under every quality gate.
            "legwear" if std::env::var_os("OK_LOWPOLY_REMESH_LEGWEAR").is_some() => 0.5,
            "legwear" => return kept(part, "socks and stockings are not reduced"),
            "body" => return kept(part, "body is not reduced"),
            // Field-aligned remeshing breaks strand cards even past the
            // gates; the crowd room's edge collapse reduces hair instead.
            "hair" => return kept(part, "hair is not remeshed; the crowd room's edge collapse reduces it"),
            _ => outfit_share,
        };
        if ratio >= 1.0 { return kept(part, &format!("{} within budget", class[k])); }
        remesh_part(part, ((t as f32 * ratio) as usize).max(MIN_PART_TRIANGLES), bones)
    }).collect();
    finish(source, budget, &tris, lows)
}

fn finish(source: &Source, budget: usize, tris: &[usize], lows: Vec<LowPart>) -> (Vec<LowPart>, Report) {
    use rayon::prelude::*;
    let bones = source.bones.len();
    let total = |lows: &[LowPart]| lows.iter().map(|l| l.indices.len() / 3).sum::<usize>();
    let palette: Vec<Mat4> = source.bones.iter()
        .map(|b| pose::mat_mul_pub(&b.pose_world, &pose::mat_inv_pub(&b.rest_world))).collect();
    let probe = probe_palette(source);
    let rows: Vec<serde_json::Value> = source.parts.par_iter().zip(&lows).map(|(part, low)| {
        let (rest_p95, rest_max) = deviation(&low.positions, &low.indices, &part.ref_pos, &part.indices);
        let compare = |palette: &[Mat4]| deviation(
            &skin(&low.positions, &low.joints, &low.weights, palette), &low.indices,
            &skin(&part.ref_pos, &part.joints, &part.weights, palette), &part.indices);
        let (pose_p95, pose_max) = compare(&palette);
        let (probe_p95, probe_max) = compare(&probe);
        serde_json::json!({
            "name": part.name, "role": part.role, "class": category(part), "rigid": part.rigid, "method": low.method,
            "source_triangles": part.indices.len() / 3, "low_triangles": low.indices.len() / 3, "low_vertices": low.positions.len(),
            "patched_source_triangles": low.patched,
            "boundary_length_ratio": boundary_growth(part, low),
            "diagonal": diagonal(&part.ref_pos),
            "rest_deviation_p95": rest_p95, "rest_deviation_max": rest_max,
            "pose_deviation_p95": pose_p95, "pose_deviation_max": pose_max,
            "probe_deviation_p95": probe_p95, "probe_deviation_max": probe_max,
        })
    }).collect();
    let total_low = total(&lows);
    let json = serde_json::json!({
        "format": "OpenKoikatsu low-poly glTF 1",
        "remesher": "instant-meshes (Jakob et al. 2015): -r 6 -p 6 -S 0 -c 30 -b -d",
        "triangle_budget": budget, "source_triangles": tris.iter().sum::<usize>(), "low_triangles": total_low,
        "classes": (["face", "body", "legwear", "hair", "outfit"].iter().map(|c| {
            let rows: Vec<(&Part, &LowPart)> = source.parts.iter().zip(&lows).filter(|(p, _)| category(p) == *c).collect();
            let low: usize = rows.iter().map(|(_, l)| l.indices.len() / 3).sum();
            let limit = match *c { "hair" => Some(HAIR_TRIANGLE_BUDGET), "outfit" => Some(budget), _ => None };
            (c.to_string(), serde_json::json!({"parts": rows.len(), "source_triangles": rows.iter().map(|(p, _)| p.indices.len() / 3).sum::<usize>(),
                "low_triangles": low, "budget": limit, "within_budget": limit.map_or(true, |b| low <= b)}))
        }).collect::<serde_json::Map<_, _>>()),
        "bones": bones, "parts": rows,
        "skipped": source.skipped.iter().map(|(n, r)| serde_json::json!({"name": n, "reason": r})).collect::<Vec<_>>(),
        "deviation": concat!("symmetric closest-surface distance between low and source part over drawn vertices, ",
            "metres in Unity space: rest = shared rest pose, pose = export pose, probe = every bone turned 25 degrees"),
    });
    (lows, Report { json })
}

// ---------------------------------------------------------------- glTF

/// Unity is left-handed; glTF is right-handed. Mirror X.
fn gl_pos(p: [f32; 3]) -> [f32; 3] { [-p[0], p[1], p[2]] }
fn gl_rot(q: [f32; 4]) -> [f32; 4] { [q[0], -q[1], -q[2], q[3]] }
fn gl_mat(m: &Mat4) -> Mat4 {
    let mut o = *m;
    for r in 0..4 { for c in 0..4 { if (r == 0) != (c == 0) { o[r][c] = -m[r][c]; } } }
    o
}

struct Bin { data: Vec<u8>, views: Vec<serde_json::Value>, accessors: Vec<serde_json::Value> }

impl Bin {
    fn align(&mut self) { while self.data.len() % 4 != 0 { self.data.push(0); } }
    fn view(&mut self, bytes: &[u8], target: Option<u32>) -> usize {
        self.align();
        let offset = self.data.len();
        self.data.extend_from_slice(bytes);
        let mut v = serde_json::json!({"buffer": 0, "byteOffset": offset, "byteLength": bytes.len()});
        if let Some(t) = target { v["target"] = t.into(); }
        self.views.push(v);
        self.views.len() - 1
    }
    fn accessor(&mut self, bytes: &[u8], target: Option<u32>, component: u32, count: usize, kind: &str, extra: serde_json::Value) -> usize {
        let view = self.view(bytes, target);
        let mut a = serde_json::json!({"bufferView": view, "componentType": component, "count": count, "type": kind});
        if let serde_json::Value::Object(map) = extra { for (k, v) in map { a[k] = v; } }
        self.accessors.push(a);
        self.accessors.len() - 1
    }
}

fn f32_bytes(v: impl Iterator<Item = f32>) -> Vec<u8> { v.flat_map(|x| x.to_le_bytes()).collect() }

pub fn write_glb(path: &Path, source: &Source, lows: &[LowPart]) -> Result<(), String> {
    let mut bin = Bin { data: Vec::new(), views: Vec::new(), accessors: Vec::new() };
    let mut nodes: Vec<serde_json::Value> = source.bones.iter().map(|b| serde_json::json!({
        "name": b.name, "translation": gl_pos(b.pose.0), "rotation": gl_rot(b.pose.1), "scale": b.pose.2,
    })).collect();
    let mut roots = Vec::new();
    for (i, b) in source.bones.iter().enumerate() {
        if b.parent >= 0 && (b.parent as usize) < i {
            let children = nodes[b.parent as usize].as_object_mut().unwrap().entry("children").or_insert(serde_json::json!([]));
            children.as_array_mut().unwrap().push(i.into());
        } else {
            roots.push(i);
        }
    }
    let ibm = f32_bytes(source.bones.iter().flat_map(|b| {
        let m = gl_mat(&pose::mat_inv_pub(&b.rest_world));
        // glTF matrices are column-major.
        (0..16).map(move |k| m[k % 4][k / 4])
    }));
    let ibm_acc = bin.accessor(&ibm, None, 5126, source.bones.len(), "MAT4", serde_json::json!({}));
    let mut images = Vec::new();
    let mut textures = Vec::new();
    let mut materials = Vec::new();
    let mut meshes = Vec::new();
    for (part, low) in source.parts.iter().zip(lows) {
        if low.indices.is_empty() { continue; }
        let n = low.positions.len();
        let pos: Vec<[f32; 3]> = low.positions.iter().map(|&p| gl_pos(p)).collect();
        let mut lo = [f32::MAX; 3]; let mut hi = [f32::MIN; 3];
        for p in &pos { for a in 0..3 { lo[a] = lo[a].min(p[a]); hi[a] = hi[a].max(p[a]); } }
        let a_pos = bin.accessor(&f32_bytes(pos.iter().flatten().copied()), Some(34962), 5126, n, "VEC3", serde_json::json!({"min": lo, "max": hi}));
        let a_nrm = bin.accessor(&f32_bytes(low.normals.iter().map(|&v| gl_pos(normalize(v))).flatten()), Some(34962), 5126, n, "VEC3", serde_json::json!({}));
        let a_uv = bin.accessor(&f32_bytes(low.uvs.iter().flat_map(|uv| [uv[0], 1.0 - uv[1]])), Some(34962), 5126, n, "VEC2", serde_json::json!({}));
        let joints: Vec<u8> = low.joints.iter().flatten().flat_map(|j| j.to_le_bytes()).collect();
        let a_j = bin.accessor(&joints, Some(34962), 5123, n, "VEC4", serde_json::json!({}));
        let a_w = bin.accessor(&f32_bytes(low.weights.iter().flatten().copied()), Some(34962), 5126, n, "VEC4", serde_json::json!({}));
        // Mirroring reverses handedness; swap two corners to keep the front face.
        let idx: Vec<u8> = low.indices.chunks_exact(3).flat_map(|t| [t[0], t[2], t[1]]).flat_map(|i| i.to_le_bytes()).collect();
        let a_i = bin.accessor(&idx, Some(34963), 5125, low.indices.len(), "SCALAR", serde_json::json!({}));
        let mut material = serde_json::json!({
            "name": part.name, "doubleSided": true,
            "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0},
        });
        let masked = part.texture.as_ref().zip(part.alpha_mask.as_ref()).and_then(|(main, mask)| bake_alpha_mask(main, mask));
        if let Some(png) = masked.as_ref().or(part.texture.as_ref()).filter(|p| p.starts_with(b"\x89PNG")) {
            let view = bin.view(png, None);
            images.push(serde_json::json!({"bufferView": view, "mimeType": "image/png"}));
            textures.push(serde_json::json!({"source": images.len() - 1}));
            material["pbrMetallicRoughness"]["baseColorTexture"] = serde_json::json!({"index": textures.len() - 1});
        }
        match part.blend.as_str() {
            "masked" => { material["alphaMode"] = "MASK".into(); material["alphaCutoff"] = 0.5.into(); }
            _ if masked.is_some() => { material["alphaMode"] = "MASK".into(); material["alphaCutoff"] = 0.5.into(); }
            "translucent" | "transparent" | "alpha" => { material["alphaMode"] = "BLEND".into(); }
            _ => {}
        }
        materials.push(material);
        meshes.push(serde_json::json!({"name": part.name, "primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv, "JOINTS_0": a_j, "WEIGHTS_0": a_w},
            "indices": a_i, "material": materials.len() - 1,
        }]}));
        nodes.push(serde_json::json!({"name": part.name, "mesh": meshes.len() - 1, "skin": 0}));
        roots.push(nodes.len() - 1);
    }
    bin.align();
    let mut doc = serde_json::json!({
        "asset": {"version": "2.0", "generator": "OpenKoikatsu ok-extract lowpoly"},
        "scene": 0, "scenes": [{"nodes": roots}], "nodes": nodes, "meshes": meshes, "materials": materials,
        "skins": [{"joints": (0..source.bones.len()).collect::<Vec<_>>(), "inverseBindMatrices": ibm_acc}],
        "accessors": bin.accessors, "bufferViews": bin.views, "buffers": [{"byteLength": bin.data.len()}],
    });
    if !images.is_empty() { doc["images"] = images.into(); doc["textures"] = textures.into(); }
    let mut json = serde_json::to_vec(&doc).map_err(|e| e.to_string())?;
    while json.len() % 4 != 0 { json.push(b' '); }
    let total = 12 + 8 + json.len() + 8 + bin.data.len();
    let mut out = Vec::with_capacity(total);
    out.extend_from_slice(b"glTF");
    out.extend_from_slice(&2u32.to_le_bytes());
    out.extend_from_slice(&(total as u32).to_le_bytes());
    out.extend_from_slice(&(json.len() as u32).to_le_bytes());
    out.extend_from_slice(b"JSON");
    out.extend_from_slice(&json);
    out.extend_from_slice(&(bin.data.len() as u32).to_le_bytes());
    out.extend_from_slice(b"BIN\0");
    out.extend_from_slice(&bin.data);
    // Write beside the target first so a failure never leaves half a model.
    let temp = path.with_extension("glb.part");
    std::fs::write(&temp, &out).map_err(|e| format!("{}: {e}", temp.display()))?;
    std::fs::rename(&temp, path).map_err(|e| format!("{}: {e}", path.display()))
}

/// FNV-1a over the little-endian index buffer: identifies a renderer by its
/// topology, independent of part order or pose.
pub fn index_hash(indices: &[u32]) -> u64 {
    let mut h = 0xcbf2_9ce4_8422_2325u64;
    for b in indices.iter().flat_map(|i| i.to_le_bytes()) {
        h ^= b as u64;
        h = h.wrapping_mul(0x0100_0000_01b3);
    }
    h
}

/// Low topology for runtimes that rebuild vertices from their own source
/// streams (the crowd room keeps its exact palette skinning this way).
/// Little-endian: "OKLPMAP2", u32 parts; per part u32 source vertices,
/// u32 source indices, u64 `index_hash`, u32 low vertices, u32 low
/// triangles, then per vertex u32 tri, f32 b0, f32 b1, u32 uv tri, f32 b0,
/// f32 b1 (third weight = 1 - b0 - b1), f32 outward offset in metres along
/// the interpolated source normal, then low indices in source winding.
pub fn write_okmap(path: &Path, source: &Source, lows: &[LowPart]) -> Result<(), String> {
    let mapped: Vec<(&Part, &LowPart)> = source.parts.iter().zip(lows)
        .filter(|(_, l)| !l.map.is_empty() && l.map.len() == l.positions.len() && l.offsets.len() == l.positions.len()).collect();
    let mut out = Vec::new();
    out.extend_from_slice(b"OKLPMAP2");
    out.extend_from_slice(&(mapped.len() as u32).to_le_bytes());
    for (part, low) in mapped {
        for v in [part.ref_pos.len() as u32, part.indices.len() as u32] { out.extend_from_slice(&v.to_le_bytes()); }
        out.extend_from_slice(&index_hash(&part.indices).to_le_bytes());
        for v in [low.positions.len() as u32, (low.indices.len() / 3) as u32] { out.extend_from_slice(&v.to_le_bytes()); }
        for ((tri, b, uv_tri, ub), offset) in low.map.iter().zip(&low.offsets) {
            out.extend_from_slice(&tri.to_le_bytes());
            out.extend_from_slice(&b[0].to_le_bytes());
            out.extend_from_slice(&b[1].to_le_bytes());
            out.extend_from_slice(&uv_tri.to_le_bytes());
            out.extend_from_slice(&ub[0].to_le_bytes());
            out.extend_from_slice(&ub[1].to_le_bytes());
            out.extend_from_slice(&offset.to_le_bytes());
        }
        for i in &low.indices { out.extend_from_slice(&i.to_le_bytes()); }
    }
    let temp = path.with_extension("okmap.part");
    std::fs::write(&temp, &out).map_err(|e| format!("{}: {e}", temp.display()))?;
    std::fs::rename(&temp, path).map_err(|e| format!("{}: {e}", path.display()))
}

/// `<card>.png` → `<card>.lowpoly.glb` and `<card>.lowpoly.json`.
pub fn companion_paths(card: &Path) -> (std::path::PathBuf, std::path::PathBuf) {
    (card.with_extension("lowpoly.glb"), card.with_extension("lowpoly.json"))
}

pub fn export(source: &Source, glb: &Path, report: &Path, budget: usize) -> Result<serde_json::Value, String> {
    let (lows, rep) = build(source, budget);
    write_glb(glb, source, &lows)?;
    write_okmap(&glb.with_extension("okmap"), source, &lows)?;
    let mut json = rep.json;
    json["glb"] = glb.display().to_string().into();
    std::fs::write(report, serde_json::to_vec_pretty(&json).map_err(|e| e.to_string())?)
        .map_err(|e| format!("{}: {e}", report.display()))?;
    Ok(json)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn closest_point_matches_brute_force() {
        let mut pos = Vec::new();
        let mut idx = Vec::new();
        for y in 0..20u32 { for x in 0..20u32 {
            let f = |x: u32, y: u32| [x as f32 * 0.1, y as f32 * 0.1, ((x * 7 + y * 3) % 5) as f32 * 0.02];
            let b = pos.len() as u32;
            pos.extend([f(x, y), f(x + 1, y), f(x, y + 1), f(x + 1, y + 1)]);
            idx.extend([b, b + 1, b + 2, b + 1, b + 3, b + 2]);
        }}
        let tree = TriTree::new(&pos, &idx);
        for k in 0..200 {
            let p = [(k * 37 % 230) as f32 * 0.01 - 0.1, (k * 53 % 210) as f32 * 0.01, (k % 7) as f32 * 0.05 - 0.1];
            let (_, _, d) = tree.closest(p, &|_| true).unwrap();
            let brute = (0..idx.len() / 3).map(|t| {
                let (q, _) = closest_on_triangle(p, pos[idx[t * 3] as usize], pos[idx[t * 3 + 1] as usize], pos[idx[t * 3 + 2] as usize]);
                dot(sub(q, p), sub(q, p))
            }).fold(f32::MAX, f32::min);
            assert!((d - brute).abs() < 1e-9, "{d} vs {brute}");
        }
    }

    #[test]
    fn weights_blend_and_stay_normalised() {
        let part = Part {
            name: "t".into(), role: "t".into(), blend: "opaque".into(),
            ref_pos: vec![[0.0; 3], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], ref_normal: vec![[0.0, 0.0, 1.0]; 3],
            uvs: vec![[0.0; 2]; 3], indices: vec![0, 1, 2],
            joints: vec![[1, 2, 0, 0], [2, 3, 0, 0], [4, 5, 6, 7]],
            weights: vec![[0.5, 0.5, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0], [0.4, 0.3, 0.2, 0.1]],
            texture: None, alpha_mask: None, rigid: false, hair: false, layer: false,
        };
        let (j, w) = blend_weights(&part, 0, [0.5, 0.5, 0.0], 10);
        assert_eq!(j[0], 2);
        assert!((w[0] - 0.75).abs() < 1e-6 && (w[1] - 0.25).abs() < 1e-6);
        let (_, w) = blend_weights(&part, 0, [0.2, 0.3, 0.5], 10);
        assert!((w.iter().sum::<f32>() - 1.0).abs() < 1e-6);
    }

    /// Real crowd card in the live-viewer environment. Writes the companion
    /// files to OK_LOWPOLY_OUT (a directory) and prints the report.
    #[test]
    #[ignore = "installed APNG/shared resources; owns process environment"]
    fn installed_card_lowpoly() {
        let project = Path::new("C:/portable/OpenKoikatsu");
        let card = std::env::var_os("OK_LOWPOLY_CARD").map(std::path::PathBuf::from)
            .unwrap_or_else(|| project.join("Content/CharacterCards/CrowdAPNG/C000.png"));
        let out = std::path::PathBuf::from(std::env::var("OK_LOWPOLY_OUT").expect("OK_LOWPOLY_OUT directory"));
        std::fs::create_dir_all(&out).unwrap();
        std::env::set_var("OK_BUILTIN_RESOURCES", project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
        std::env::set_var("OK_CLOTHES_STATES", "0,0,0,0,0,0,0,0,0");
        let mut session = crate::portable::load_outfit(&card, 0).unwrap();
        let source = capture(&mut session).unwrap();
        // Skinning the rebound rest positions with the shared rig must return
        // exactly what the session draws, for clothes and hair as well.
        let palette: Vec<Mat4> = source.bones.iter()
            .map(|b| pose::mat_mul_pub(&b.pose_world, &pose::mat_inv_pub(&b.rest_world))).collect();
        let drawn: Vec<&Vec<[f32; 3]>> = session.parts.iter()
            .filter(|p| p.render_visible && !p.positions.is_empty() && p.indices.len() >= 3)
            .filter(|p| !matches!(p.name.to_ascii_lowercase().as_str(), "o_dankon" | "o_dan_f" | "cm_o_dan00" | "cm_o_dan_f" | "o_gomu"))
            .map(|p| &p.positions).collect();
        for (part, drawn) in source.parts.iter().zip(drawn) {
            let posed = skin(&part.ref_pos, &part.joints, &part.weights, &palette);
            let err = drawn.iter().zip(&posed).map(|(a, b)| norm(sub(*a, *b))).fold(0.0f32, f32::max);
            eprintln!("rig {:40} rigid={} verts={:6} drawn_vs_rig_max={err:.2e}", part.name, part.rigid, part.ref_pos.len());
            assert!(err < 1e-4, "{} does not reproduce the drawn mesh: {err}", part.name);
        }
        let stem = card.file_stem().unwrap().to_string_lossy().to_string();
        let glb = out.join(format!("{stem}.lowpoly.glb"));
        let started = std::time::Instant::now();
        let budget = std::env::var("OK_LOWPOLY_BUDGET").ok().and_then(|b| b.parse().ok()).unwrap_or(DEFAULT_TRIANGLE_BUDGET);
        let report = export(&source, &glb, &out.join(format!("{stem}.lowpoly.json")), budget).unwrap();
        eprintln!("export took {:.1}s", started.elapsed().as_secs_f32());
        for row in report["parts"].as_array().unwrap() {
            eprintln!("{:40} {:28} {:6} -> {:6}  rest p95={:.5} pose p95={:.5} probe p95={:.5} max={:.5}",
                row["name"].as_str().unwrap(), row["method"].as_str().unwrap().chars().take(28).collect::<String>(),
                row["source_triangles"], row["low_triangles"], row["rest_deviation_p95"].as_f64().unwrap(),
                row["pose_deviation_p95"].as_f64().unwrap(), row["probe_deviation_p95"].as_f64().unwrap(), row["probe_deviation_max"].as_f64().unwrap());
        }
        eprintln!("total {} -> {} bones={} skipped={}", report["source_triangles"], report["low_triangles"], report["bones"], report["skipped"]);
        assert!(crate::gamefs::metadata(&glb).unwrap().len() > 1000);
        assert!(report["low_triangles"].as_u64().unwrap() < report["source_triangles"].as_u64().unwrap());
        // Wrongly carried weights tear the surface apart once bones turn.
        for row in report["parts"].as_array().unwrap().iter().filter(|r| r["method"] == "remeshed") {
            let f = |k: &str| row[k].as_f64().unwrap();
            assert!(f("probe_deviation_p95") <= 2.0 * f("rest_deviation_p95").max(f("pose_deviation_p95")) + 0.003,
                "{} weights diverge under the probe pose: {row}", row["name"]);
        }
    }
}

#[cfg(test)]
mod hole_audit {
    use super::*;
    /// Boundary loops of a mesh, as vertex index lists (welded by position).
    pub(super) fn boundary_loops(pos: &[[f32; 3]], idx: &[u32]) -> Vec<Vec<[f32; 3]>> {
        // Merge seam copies by position but keep every triangle: dropping
        // collapsed or repeated ones would report holes that are not there.
        let eps = (diagonal(pos) * 1e-6).max(1e-7);
        let mut key: HashMap<[i64; 3], u32> = HashMap::new();
        let mut wpos = Vec::new();
        let remap: Vec<u32> = pos.iter().map(|p| *key.entry([(p[0] / eps).round() as i64, (p[1] / eps).round() as i64, (p[2] / eps).round() as i64])
            .or_insert_with(|| { wpos.push(*p); wpos.len() as u32 - 1 })).collect();
        let widx: Vec<u32> = idx.iter().map(|&v| remap[v as usize]).collect();
        let mut count: HashMap<(u32, u32), i32> = HashMap::new();
        for t in widx.chunks_exact(3) { for k in 0..3 { let (a, b) = (t[k], t[(k + 1) % 3]); if a != b { *count.entry((a.min(b), a.max(b))).or_default() += 1; } } }
        let mut next: HashMap<u32, Vec<u32>> = HashMap::new();
        for (&(a, b), &c) in &count { if c == 1 { next.entry(a).or_default().push(b); next.entry(b).or_default().push(a); } }
        let mut seen = std::collections::HashSet::new();
        let mut loops = Vec::new();
        for &start in next.keys() {
            if seen.contains(&start) { continue; }
            let mut lp = Vec::new(); let mut stack = vec![start];
            while let Some(v) = stack.pop() { if !seen.insert(v) { continue; } lp.push(wpos[v as usize]); for &n in &next[&v] { stack.push(n); } }
            loops.push(lp);
        }
        loops
    }

    #[test]
    #[ignore = "installed APNG/shared resources; owns process environment"]
    fn installed_card_source_mesh() {
        let project = Path::new("C:/portable/OpenKoikatsu");
        let card = std::env::var_os("OK_LOWPOLY_CARD").map(std::path::PathBuf::from)
            .unwrap_or_else(|| project.join("Content/CharacterCards/CrowdAPNG/C000.png"));
        std::env::set_var("OK_BUILTIN_RESOURCES", project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
        std::env::set_var("OK_CLOTHES_STATES", "0,0,0,0,0,0,0,0,0");
        let mut session = crate::portable::load_outfit(&card, 0).unwrap();
        let source = capture(&mut session).unwrap();
        for part in &source.parts {
            // Welded by position: UV seams are not boundaries.
            let (wpos, widx) = weld(&part.ref_pos, &part.indices);
            let mut count: HashMap<(u32, u32), u32> = HashMap::new();
            for t in widx.chunks_exact(3) { for k in 0..3 { let (a, b) = (t[k], t[(k + 1) % 3]); *count.entry((a.min(b), a.max(b))).or_default() += 1; } }
            let boundary = count.values().filter(|&&c| c == 1).count();
            let nonmanifold = count.values().filter(|&&c| c > 2).count();
            let degenerate = part.indices.chunks_exact(3).filter(|t| {
                let (a, b, c) = (part.ref_pos[t[0] as usize], part.ref_pos[t[1] as usize], part.ref_pos[t[2] as usize]);
                norm(cross(sub(b, a), sub(c, a))) < 1e-12 }).count();
            let mut loops: Vec<usize> = boundary_loops(&part.ref_pos, &part.indices).iter().map(|l| l.len()).collect();
            loops.sort_unstable_by(|a, b| b.cmp(a));
            eprintln!("source {:30} tris={:6} welded_verts={:6} boundary_edges={:5} nonmanifold_edges={:4} degenerate={:4} loops={} sizes={:?}",
                part.name, part.indices.len() / 3, wpos.len(), boundary, nonmanifold, degenerate, loops.len(), &loops[..loops.len().min(12)]);
        }
    }

    /// Hair orientation audit: mirrored skin (negative palette determinant)
    /// and winding vs authored normals, per hair part (OK_LOWPOLY_CARD).
    #[test]
    #[ignore = "installed APNG/shared resources; owns process environment"]
    fn installed_card_hair_orientation() {
        let project = Path::new("C:/portable/OpenKoikatsu");
        let card = std::env::var_os("OK_LOWPOLY_CARD").map(std::path::PathBuf::from)
            .unwrap_or_else(|| project.join("Content/CharacterCards/CrowdAPNG/C024.png"));
        std::env::set_var("OK_BUILTIN_RESOURCES", project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
        std::env::set_var("OK_CLOTHES_STATES", "0,0,0,0,0,0,0,0,0");
        let mut session = crate::portable::load_outfit(&card, 0).unwrap();
        session.skin_all();
        let sp: *mut crate::session::OkSession = &mut session;
        for index in 0..session.parts.len() {
            let part = &session.parts[index];
            if !matches!(part.shader_kind, 1 | 10) || !part.render_visible { continue; }
            let (name, idx, pos, nrm) = (part.name.clone(), part.indices.clone(), part.positions.clone(), part.normals.clone());
            let count = unsafe { crate::ffi::ok_part_skin_palette(sp, index as i32, std::ptr::null_mut(), std::ptr::null_mut(), 0) };
            let mut negative = 0usize; let mut total = 0usize;
            if count > 0 {
                let mut view = std::mem::MaybeUninit::<crate::ffi::OkSkinPaletteView>::uninit();
                let mut m = vec![0f32; count as usize * 16];
                let got = unsafe { crate::ffi::ok_part_skin_palette(sp, index as i32, view.as_mut_ptr(), m.as_mut_ptr(), count) };
                if got == count {
                    let v = unsafe { view.assume_init() };
                    let w = unsafe { std::slice::from_raw_parts(v.weights, v.n_verts as usize * 4) };
                    let b = unsafe { std::slice::from_raw_parts(v.bone_idx, v.n_verts as usize * 4) };
                    for i in 0..v.n_verts as usize {
                        let mut blend = [[0f32; 3]; 3];
                        for k in 0..4 { let wt = w[i * 4 + k]; if wt.abs() < 1e-6 { continue; } let mm = &m[b[i * 4 + k] as usize * 16..][..16];
                            for r in 0..3 { for c in 0..3 { blend[r][c] += mm[r * 4 + c] * wt; } } }
                        let det = blend[0][0] * (blend[1][1] * blend[2][2] - blend[1][2] * blend[2][1]) - blend[0][1] * (blend[1][0] * blend[2][2] - blend[1][2] * blend[2][0]) + blend[0][2] * (blend[1][0] * blend[2][1] - blend[1][1] * blend[2][0]);
                        total += 1; if det < 0.0 { negative += 1; }
                    }
                }
            }
            let mut agree = 0usize; let mut tris = 0usize;
            if nrm.len() == pos.len() {
                for t in idx.chunks_exact(3) {
                    let (a, b2, c) = (pos[t[0] as usize], pos[t[1] as usize], pos[t[2] as usize]);
                    let face = cross(sub(b2, a), sub(c, a));
                    let n = add(nrm[t[0] as usize], add(nrm[t[1] as usize], nrm[t[2] as usize]));
                    tris += 1; if dot(face, n) > 0.0 { agree += 1; }
                }
            }
            eprintln!("hair {:28} kind={} mirrored_vertices={}/{} winding_agrees_with_normals={}/{}", name, session.parts[index].shader_kind, negative, total, agree, tris);
        }
    }

    /// Remesher output only, by topology (no welding): interior boundary
    /// loops that are not along the input boundary, with fill/snap on and off.
    #[test]
    #[ignore = "installed APNG/shared resources; owns process environment"]
    fn installed_card_remesher_holes() {
        let project = Path::new("C:/portable/OpenKoikatsu");
        let card = std::env::var_os("OK_LOWPOLY_CARD").map(std::path::PathBuf::from)
            .unwrap_or_else(|| project.join("Content/CharacterCards/CrowdAPNG/C000.png"));
        std::env::set_var("OK_BUILTIN_RESOURCES", project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
        std::env::set_var("OK_CLOTHES_STATES", "0,0,0,0,0,0,0,0,0");
        let mut session = crate::portable::load_outfit(&card, 0).unwrap();
        let source = capture(&mut session).unwrap();
        use instant_meshes::math::Vec3;
        use instant_meshes::mesh::{Faces, Surface};
        instant_meshes::set_verbose(false);
        for part in source.parts.iter().filter(|p| p.indices.len() / 3 > KEEP_BELOW && !p.hair) {
            let (wpos, widx) = weld(&part.ref_pos, &part.indices);
            let mut faces = Faces::new(3, widx.len() / 3);
            faces.idx.copy_from_slice(&widx);
            let surface = Surface { faces, vertices: wpos.iter().map(|p| Vec3::new(p[0], p[1], p[2])).collect(), normals: Vec::new() };
            let input = instant_meshes::postprocess::input_boundary(&surface.faces, &surface.vertices);
            let mut row = Vec::new();
            for fix in [false, true] {
                let options = instant_meshes::Options { rosy: 6, posy: 3, face_count: (part.indices.len() / 6) as i32, crease_angle: 30.0, smooth_iter: 0,
                    align_to_boundaries: true, deterministic: true, fill_holes: fix, snap_boundary: fix, ..Default::default() };
                let mesh = instant_meshes::remesh(&surface, &options).unwrap();
                // Boundary half-edges and their cycles, topology only.
                let mut half: HashMap<(u32, u32), u32> = HashMap::new();
                for f in 0..mesh.faces.cols() { let t = mesh.faces.face(f); for k in 0..3 { if t[k] != t[(k + 1) % 3] { *half.entry((t[k], t[(k + 1) % 3])).or_default() += 1; } } }
                let mut out: HashMap<u32, Vec<u32>> = HashMap::new();
                for (&(a, b), &c) in &half { if c == 1 && !half.contains_key(&(b, a)) { out.entry(b).or_default().push(a); } }
                let mut loops = Vec::new();
                let starts: Vec<u32> = out.keys().copied().collect();
                for start in starts {
                    while out.get(&start).map_or(false, |o| !o.is_empty()) {
                        let mut path = vec![start];
                        while let Some(to) = out.get_mut(path.last().unwrap()).and_then(|o| o.pop()) {
                            if let Some(k) = path.iter().position(|&v| v == to) { loops.push(path.split_off(k)); path.push(to); continue; }
                            path.push(to);
                        }
                    }
                }
                let edge = 0.5 * (part.indices.len() as f32).recip().sqrt();
                let _ = edge;
                // Interior: fewer than half its corners within 3 mm of the input boundary... use output edge.
                let mean = { let (mut s, mut n) = (0.0f32, 0); for f in 0..mesh.faces.cols() { let t = mesh.faces.face(f); for k in 0..3 { s += (mesh.vertices[t[k] as usize] - mesh.vertices[t[(k + 1) % 3] as usize]).norm(); n += 1; } } s / n as f32 };
                let near = |v: u32| { let p = mesh.vertices[v as usize]; input.iter().any(|(a, b)| { let ab = *b - *a; let t = ((p - *a).dot(ab) / ab.squared_norm().max(1e-20)).clamp(0.0, 1.0); (p - (*a + ab * t)).norm() < mean * 1.5 }) };
                let interior: Vec<usize> = loops.iter().filter(|l| l.iter().filter(|&&v| near(v)).count() * 2 < l.len()).map(|l| l.len()).collect();
                let gap = loops.iter().filter(|l| l.iter().filter(|&&v| near(v)).count() * 2 >= l.len()).flat_map(|l| l.iter())
                    .map(|&v| { let p = mesh.vertices[v as usize]; input.iter().map(|(a, b)| { let ab = *b - *a; let t = ((p - *a).dot(ab) / ab.squared_norm().max(1e-20)).clamp(0.0, 1.0); (p - (*a + ab * t)).norm() }).fold(f32::MAX, f32::min) })
                    .fold(0.0f32, f32::max);
                row.push(format!("{}: interior={} sizes={:?} boundary_gap_max={:.2}mm", if fix { "fixed" } else { "original" }, interior.len(), interior, gap * 1000.0));
            }
            eprintln!("remesher {:28} {}", part.name, row.join(" | "));
        }
    }

    #[test]
    #[ignore = "installed APNG/shared resources; owns process environment"]
    fn installed_card_holes() {
        let project = Path::new("C:/portable/OpenKoikatsu");
        let card = std::env::var_os("OK_LOWPOLY_CARD").map(std::path::PathBuf::from)
            .unwrap_or_else(|| project.join("Content/CharacterCards/CrowdAPNG/C000.png"));
        std::env::set_var("OK_BUILTIN_RESOURCES", project.join("Content/IndependentResources/v1"));
        for key in ["OK_GPU_SKIN", "OK_SKELMESH", "OK_NOPOSE", "OK_UE_ANIM", "OK_HEAD_GPUSKIN"] { std::env::set_var(key, "1"); }
        std::env::set_var("OK_CLOTHES_STATES", "0,0,0,0,0,0,0,0,0");
        let mut session = crate::portable::load_outfit(&card, 0).unwrap();
        let source = capture(&mut session).unwrap();
        let (lows, _) = build(&source, DEFAULT_TRIANGLE_BUDGET);
        for (part, low) in source.parts.iter().zip(&lows).filter(|(_, l)| l.method == "remeshed") {
            // A low loop is a real opening when it runs along a source boundary.
            let src_loops = boundary_loops(&part.ref_pos, &part.indices);
            let src_pts: Vec<[f32; 3]> = src_loops.iter().flatten().copied().collect();
            let diag = diagonal(&part.ref_pos);
            let near_src = |p: [f32; 3]| src_pts.iter().any(|q| norm(sub(p, *q)) < diag * 0.03);
            let unpatched = &low.indices[..low.indices.len() - low.patched * 3];
            let loops = boundary_loops(&low.positions, unpatched);
            let interior: Vec<usize> = loops.iter().filter(|l| l.iter().filter(|&&p| near_src(p)).count() * 2 < l.len()).map(|l| l.len()).collect();
            eprintln!("holes {:32} patched={} source_loops={} low_loops={} interior_holes={} sizes={:?}", part.name, low.patched, src_loops.len(), loops.len(), interior.len(), interior);
        }
    }
}
