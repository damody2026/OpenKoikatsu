use std::collections::HashSet;

use crate::dedge::dedge_next_3;
use crate::math::{fast_acos, Vec3, INVALID, RCPOVERFLOW};
use crate::mesh::Faces;

pub fn generate_smooth_normals(
    faces: &Faces,
    vertices: &[Vec3],
    v2e: &[u32],
    e2e: &[u32],
    non_manifold: &[bool],
) -> Vec<Vec3> {
    crate::say!("Computing vertex normals ..");
    let mut face_normals = vec![Vec3::X; faces.cols()];
    let mut bad = 0u32;
    for f in 0..faces.cols() {
        let v0 = vertices[faces.get(0, f) as usize];
        let v1 = vertices[faces.get(1, f) as usize];
        let v2 = vertices[faces.get(2, f) as usize];
        let n = (v1 - v0).cross(v2 - v0);
        let norm = n.norm();
        if norm < RCPOVERFLOW {
            bad += 1;
            face_normals[f] = Vec3::X;
        } else {
            face_normals[f] = n / norm;
        }
    }

    let mut normals = vec![Vec3::X; vertices.len()];
    for i in 0..vertices.len() {
        let mut edge = v2e[i];
        if non_manifold[i] || edge == INVALID {
            continue;
        }
        let stop = edge;
        let mut normal = Vec3::ZERO;
        loop {
            let idx = (edge % 3) as usize;
            let face = (edge / 3) as usize;
            let d0 = vertices[faces.get((idx + 1) % 3, face) as usize] - vertices[i];
            let d1 = vertices[faces.get((idx + 2) % 3, face) as usize] - vertices[i];
            let denom = (d0.squared_norm() * d1.squared_norm()).sqrt();
            if denom > RCPOVERFLOW {
                let mut cosine = d0.dot(d1) / denom;
                cosine = cosine.clamp(-1.0, 1.0);
                let angle = fast_acos(cosine);
                if angle.is_finite() {
                    normal += face_normals[face] * angle;
                }
            }
            let opp = e2e[edge as usize];
            if opp == INVALID {
                break;
            }
            edge = dedge_next_3(opp);
            if edge == stop {
                break;
            }
        }
        let norm = normal.norm();
        normals[i] = if norm > RCPOVERFLOW {
            normal / norm
        } else {
            Vec3::X
        };
    }
    if bad > 0 {
        crate::say!("done. ({bad} degenerate faces)");
    } else {
        crate::say!("done.");
    }
    normals
}

/// Split the one-ring at creases sharper than `angle_threshold` degrees and
/// record crease vertices. Unlike the interactive path, vertices are not duplicated.
pub fn generate_crease_normals(
    faces: &Faces,
    vertices: &[Vec3],
    v2e: &[u32],
    e2e: &[u32],
    boundary: &[bool],
    non_manifold: &[bool],
    angle_threshold: f32,
) -> (Vec<Vec3>, HashSet<u32>) {
    let dp_threshold = (angle_threshold * std::f32::consts::PI / 180.0).cos();
    crate::say!("Computing vertex & crease normals ..");
    let mut v2e = v2e.to_vec();
    let mut face_normals = vec![Vec3::X; faces.cols()];
    let mut bad = 0u32;
    for f in 0..faces.cols() {
        let v0 = vertices[faces.get(0, f) as usize];
        let v1 = vertices[faces.get(1, f) as usize];
        let v2 = vertices[faces.get(2, f) as usize];
        let n = (v1 - v0).cross(v2 - v0);
        let norm = n.norm();
        if norm < RCPOVERFLOW {
            bad += 1;
            face_normals[f] = Vec3::X;
        } else {
            face_normals[f] = n / norm;
        }
    }

    for i in 0..vertices.len() {
        let mut edge = v2e[i];
        let stop = edge;
        if non_manifold[i] || edge == INVALID {
            continue;
        }
        let mut crease_edge = INVALID;
        loop {
            let opp = e2e[edge as usize];
            if opp == INVALID {
                break;
            }
            let next_edge = dedge_next_3(opp);
            if face_normals[(edge / 3) as usize].dot(face_normals[(next_edge / 3) as usize])
                < dp_threshold
                && (crease_edge == INVALID || crease_edge < next_edge)
            {
                crease_edge = next_edge;
            }
            edge = next_edge;
            if edge == stop {
                break;
            }
        }
        if crease_edge != INVALID && !boundary[i] {
            v2e[i] = crease_edge;
        }
    }

    let mut normals = vec![Vec3::X; vertices.len()];
    let mut creases = HashSet::new();
    for i in 0..vertices.len() {
        let mut edge = v2e[i];
        if non_manifold[i] || edge == INVALID {
            continue;
        }
        let stop = edge;
        let mut normal = Vec3::ZERO;
        loop {
            let idx = (edge % 3) as usize;
            let face = (edge / 3) as usize;
            let d0 = vertices[faces.get((idx + 1) % 3, face) as usize] - vertices[i];
            let d1 = vertices[faces.get((idx + 2) % 3, face) as usize] - vertices[i];
            let denom = (d0.squared_norm() * d1.squared_norm()).sqrt();
            if denom > RCPOVERFLOW {
                let cosine = (d0.dot(d1) / denom).clamp(-1.0, 1.0);
                let angle = fast_acos(cosine);
                if angle.is_finite() {
                    normal += face_normals[face] * angle;
                }
            }
            let opp = e2e[edge as usize];
            if opp == INVALID {
                break;
            }
            let next_edge = dedge_next_3(opp);
            if face_normals[(edge / 3) as usize].dot(face_normals[(next_edge / 3) as usize])
                < dp_threshold
            {
                creases.insert(i as u32);
                break;
            }
            edge = next_edge;
            if edge == stop {
                break;
            }
        }
        let norm = normal.norm();
        normals[i] = if norm > RCPOVERFLOW {
            normal / norm
        } else {
            Vec3::X
        };
    }
    crate::say_raw!("done. (");
    if bad > 0 {
        crate::say_raw!("{bad} degenerate faces, ");
    }
    if !creases.is_empty() {
        crate::say_raw!("{} crease vertices, ", creases.len());
    }
    crate::say!(")");
    (normals, creases)
}
