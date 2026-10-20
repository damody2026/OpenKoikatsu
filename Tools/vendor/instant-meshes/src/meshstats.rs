use crate::dedge::{dedge_next_3, dedge_prev_3};
use crate::math::{Aabb, Vec3, INVALID};
use crate::mesh::Faces;

#[derive(Clone, Debug)]
pub struct MeshStats {
    pub aabb: Aabb,
    pub weighted_center: Vec3,
    pub average_edge_length: f64,
    pub maximum_edge_length: f64,
    pub surface_area: f64,
}

impl Default for MeshStats {
    fn default() -> Self {
        Self {
            aabb: Aabb::empty(),
            weighted_center: Vec3::ZERO,
            average_edge_length: 0.0,
            maximum_edge_length: 0.0,
            surface_area: 0.0,
        }
    }
}

pub fn compute_mesh_stats(faces: &Faces, vertices: &[Vec3]) -> MeshStats {
    let mut stats = MeshStats::default();
    if faces.cols() > 0 {
        crate::say!("Computing mesh statistics ..");
        for f in 0..faces.cols() {
            let v = [
                vertices[faces.get(0, f) as usize],
                vertices[faces.get(1, f) as usize],
                vertices[faces.get(2, f) as usize],
            ];
            let mut face_center = Vec3::ZERO;
            for i in 0..3 {
                let next = if i == 2 { 0 } else { i + 1 };
                let edge_length = (v[i] - v[next]).norm() as f64;
                stats.average_edge_length += edge_length;
                stats.maximum_edge_length = stats.maximum_edge_length.max(edge_length);
                stats.aabb.expand(v[i]);
                face_center += v[i];
            }
            face_center = face_center * (1.0 / 3.0);
            let face_area = 0.5 * (v[1] - v[0]).cross(v[2] - v[0]).norm() as f64;
            stats.surface_area += face_area;
            stats.weighted_center += face_center * face_area as f32;
        }
        stats.average_edge_length /= (faces.cols() * 3) as f64;
        if stats.surface_area > 0.0 {
            stats.weighted_center = stats.weighted_center / stats.surface_area as f32;
        }
    } else {
        crate::say!("Computing point cloud statistics ..");
        for v in vertices {
            stats.aabb.expand(*v);
            stats.weighted_center += *v;
        }
        if !vertices.is_empty() {
            stats.weighted_center = stats.weighted_center / vertices.len() as f32;
        }
    }
    crate::say!("done.");
    stats
}

pub fn compute_dual_vertex_areas(
    faces: &Faces,
    vertices: &[Vec3],
    v2e: &[u32],
    e2e: &[u32],
    non_manifold: &[bool],
) -> Vec<f32> {
    crate::say!("Computing dual vertex areas ..");
    let mut areas = vec![0.0; vertices.len()];
    for i in 0..vertices.len() {
        let mut edge = v2e[i];
        let stop = edge;
        if non_manifold[i] || edge == INVALID {
            continue;
        }
        let mut vertex_area = 0.0;
        loop {
            let ep = dedge_prev_3(edge);
            let en = dedge_next_3(edge);
            let v = vertices[faces.get((edge % 3) as usize, (edge / 3) as usize) as usize];
            let vn = vertices[faces.get((en % 3) as usize, (en / 3) as usize) as usize];
            let vp = vertices[faces.get((ep % 3) as usize, (ep / 3) as usize) as usize];
            let face_center = (v + vp + vn) * (1.0 / 3.0);
            let prev = (v + vp) * 0.5;
            let next = (v + vn) * 0.5;
            vertex_area += 0.5
                * ((v - prev).cross(v - face_center).norm()
                    + (v - next).cross(v - face_center).norm());
            let opp = e2e[edge as usize];
            if opp == INVALID {
                break;
            }
            edge = dedge_next_3(opp);
            if edge == stop {
                break;
            }
        }
        areas[i] = vertex_area;
    }
    crate::say!("done.");
    areas
}
