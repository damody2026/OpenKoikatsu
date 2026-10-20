//! Clean-up after extraction, not part of the original batch mode.
//!
//! The original fills only holes of fewer than 7 edges, and extracted
//! boundaries sit a little inside the input boundary, so parts that meet at a
//! seam (a dress top and its skirt) open a gap there. Both steps compare the
//! output with the input boundary: a loop that follows the input boundary is
//! a real opening (sleeve, collar, hem) and stays open.

use std::collections::HashMap;

use crate::math::{Vec3, INVALID};
use crate::mesh::Faces;

/// Input boundary edges as segments, welded by position so UV seams and
/// split normals are not mistaken for boundaries.
pub fn input_boundary(faces: &Faces, vertices: &[Vec3]) -> Vec<(Vec3, Vec3)> {
    let mut lo = Vec3::new(f32::MAX, f32::MAX, f32::MAX);
    let mut hi = Vec3::new(f32::MIN, f32::MIN, f32::MIN);
    for v in vertices {
        lo = lo.min(*v);
        hi = hi.max(*v);
    }
    let eps = ((hi - lo).norm() * 1e-6).max(1e-7);
    let mut key: HashMap<[i64; 3], u32> = HashMap::new();
    let mut welded = Vec::new();
    let remap: Vec<u32> = vertices
        .iter()
        .map(|v| {
            let k = [(v.x / eps).round() as i64, (v.y / eps).round() as i64, (v.z / eps).round() as i64];
            *key.entry(k).or_insert_with(|| {
                welded.push(*v);
                welded.len() as u32 - 1
            })
        })
        .collect();
    let mut count: HashMap<(u32, u32), u32> = HashMap::new();
    for f in 0..faces.cols() {
        let poly = polygon(faces, f);
        for k in 0..poly.len() {
            let (a, b) = (remap[poly[k] as usize], remap[poly[(k + 1) % poly.len()] as usize]);
            if a != b {
                *count.entry((a.min(b), a.max(b))).or_default() += 1;
            }
        }
    }
    count
        .into_iter()
        .filter(|(_, c)| *c == 1)
        .map(|((a, b), _)| (welded[a as usize], welded[b as usize]))
        .collect()
}

/// Distinct corners of a face; quad output marks triangles by repeating one.
fn polygon(faces: &Faces, f: usize) -> Vec<u32> {
    let mut poly: Vec<u32> = Vec::with_capacity(faces.degree);
    for &v in faces.face(f) {
        if v != INVALID && !poly.contains(&v) {
            poly.push(v);
        }
    }
    poly
}

fn mean_edge(faces: &Faces, vertices: &[Vec3]) -> f32 {
    let (mut sum, mut n) = (0.0f32, 0usize);
    for f in 0..faces.cols() {
        let poly = polygon(faces, f);
        for k in 0..poly.len() {
            sum += (vertices[poly[k] as usize] - vertices[poly[(k + 1) % poly.len()] as usize]).norm();
            n += 1;
        }
    }
    if n == 0 { 0.0 } else { sum / n as f32 }
}

fn closest_on_segments(p: Vec3, segments: &[(Vec3, Vec3)]) -> Option<(f32, Vec3)> {
    let mut best: Option<(f32, Vec3)> = None;
    for &(a, b) in segments {
        let ab = b - a;
        let t = ((p - a).dot(ab) / ab.squared_norm().max(1e-20)).clamp(0.0, 1.0);
        let q = a + ab * t;
        let d = (p - q).squared_norm();
        if best.map_or(true, |(bd, _)| d < bd) {
            best = Some((d, q));
        }
    }
    best
}

/// Directed edges used by exactly one face and not by its reverse.
fn boundary_half_edges(faces: &Faces) -> Vec<(u32, u32)> {
    let mut half: HashMap<(u32, u32), u32> = HashMap::new();
    for f in 0..faces.cols() {
        let poly = polygon(faces, f);
        if poly.len() < 3 {
            continue;
        }
        for k in 0..poly.len() {
            *half.entry((poly[k], poly[(k + 1) % poly.len()])).or_default() += 1;
        }
    }
    let mut edges: Vec<(u32, u32)> = half
        .iter()
        .filter(|(&(a, b), &c)| c == 1 && !half.contains_key(&(b, a)))
        .map(|(&e, _)| e)
        .collect();
    edges.sort_unstable();
    edges
}

/// Move output boundary vertices within 1.5 output edges of the input
/// boundary onto it. Returns how many moved.
pub fn snap_boundary(faces: &Faces, vertices: &mut [Vec3], input: &[(Vec3, Vec3)]) -> usize {
    if input.is_empty() {
        return 0;
    }
    let tolerance = (mean_edge(faces, vertices) * 1.5).powi(2);
    let mut on_boundary: Vec<u32> = boundary_half_edges(faces).into_iter().flat_map(|(a, b)| [a, b]).collect();
    on_boundary.sort_unstable();
    on_boundary.dedup();
    let mut moved = 0;
    for v in on_boundary {
        if let Some((d, q)) = closest_on_segments(vertices[v as usize], input) {
            if d < tolerance {
                vertices[v as usize] = q;
                moved += 1;
            }
        }
    }
    moved
}

/// Fill every interior hole up to `max_degree` corners by ear clipping, best
/// angle first like the extractor's own filler, winding with its neighbours.
/// New faces are appended (quad output stores triangles as `a b c c`).
/// Returns (holes filled, faces added).
pub fn fill_holes(faces: &mut Faces, vertices: &[Vec3], input: &[(Vec3, Vec3)], max_degree: usize) -> (usize, usize) {
    let edge = mean_edge(faces, vertices);
    let near_input = |p: Vec3| closest_on_segments(p, input).map_or(false, |(d, _)| d < (edge * 1.5).powi(2));
    // Walk each hole against its faces: b -> a for boundary edge a -> b.
    let mut out: HashMap<u32, Vec<u32>> = HashMap::new();
    let boundary = boundary_half_edges(faces);
    for &(a, b) in &boundary {
        out.entry(b).or_default().push(a);
    }
    // Split into simple cycles; holes touching at one vertex separate.
    let mut cycles: Vec<Vec<u32>> = Vec::new();
    for &(_, start) in &boundary {
        if out.get(&start).map_or(true, |o| o.is_empty()) {
            continue;
        }
        let mut path = vec![start];
        while let Some(to) = out.get_mut(path.last().unwrap()).and_then(|o| o.pop()) {
            if let Some(k) = path.iter().position(|&v| v == to) {
                cycles.push(path.split_off(k));
                path.push(to);
                continue;
            }
            path.push(to);
            if path.len() > 4 * max_degree.max(8) {
                break;
            }
        }
    }
    let mut added: Vec<[u32; 3]> = Vec::new();
    let mut filled = 0;
    for ring in cycles {
        if ring.len() < 3 || ring.len() > max_degree {
            continue;
        }
        let pts: Vec<Vec3> = ring.iter().map(|&v| vertices[v as usize]).collect();
        if !input.is_empty() && pts.iter().filter(|&&p| near_input(p)).count() * 2 >= pts.len() {
            continue;
        }
        let centre = pts.iter().fold(Vec3::ZERO, |a, p| a + *p) / pts.len() as f32;
        let mut normal = Vec3::ZERO;
        for k in 0..pts.len() {
            normal += (pts[k] - centre).cross(pts[(k + 1) % pts.len()] - centre);
        }
        if normal.squared_norm() <= 1e-24 {
            continue;
        }
        let normal = normal.normalized();
        let radius = pts.iter().map(|p| (*p - centre).norm()).fold(0.0f32, f32::max);
        // A loop around a whole limb or a twisted rim is not a flat hole.
        if pts.iter().map(|p| (*p - centre).dot(normal).abs()).fold(0.0f32, f32::max) > radius * 0.5 {
            continue;
        }
        let mut ring = ring;
        while ring.len() > 3 {
            let n = ring.len();
            let mut best: Option<(f32, usize)> = None;
            for i in 0..n {
                let a = vertices[ring[(i + n - 1) % n] as usize];
                let b = vertices[ring[i] as usize];
                let c = vertices[ring[(i + 1) % n] as usize];
                if (b - a).cross(c - b).dot(normal) <= 0.0 {
                    continue;
                }
                let angle = (a - b).normalized().dot((c - b).normalized()).clamp(-1.0, 1.0).acos().to_degrees();
                let score = (angle - 60.0).abs();
                if best.map_or(true, |(s, _)| score < s) {
                    best = Some((score, i));
                }
            }
            let i = best.map_or(0, |(_, i)| i);
            added.push([ring[(i + n - 1) % n], ring[i], ring[(i + 1) % n]]);
            ring.remove(i);
        }
        added.push([ring[0], ring[1], ring[2]]);
        filled += 1;
    }
    let start = faces.cols();
    faces.resize_cols(start + added.len());
    for (k, t) in added.iter().enumerate() {
        for r in 0..faces.degree {
            faces.set(r, start + k, t[r.min(2)]);
        }
    }
    (filled, added.len())
}
