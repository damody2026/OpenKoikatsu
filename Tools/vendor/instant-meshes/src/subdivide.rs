//! Longest-edge subdivision until every edge is shorter than `max_length`.

use std::cmp::Ordering;
use std::collections::BinaryHeap;

use crate::dedge::{dedge_next_3, dedge_prev_3, Dedge};
use crate::math::{Vec3, INVALID};
use crate::mesh::Faces;

#[derive(Clone, Copy)]
struct HeapEdge {
    len2: f32,
    id: u32,
}

impl PartialEq for HeapEdge {
    fn eq(&self, other: &Self) -> bool {
        self.len2 == other.len2 && self.id == other.id
    }
}
impl Eq for HeapEdge {}
impl PartialOrd for HeapEdge {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
impl Ord for HeapEdge {
    fn cmp(&self, other: &Self) -> Ordering {
        self.len2
            .partial_cmp(&other.len2)
            .unwrap_or(Ordering::Equal)
            .then(self.id.cmp(&other.id))
    }
}

pub fn subdivide(faces: &mut Faces, vertices: &mut Vec<Vec3>, dedge: &mut Dedge, max_length: f32) {
    crate::say!("Subdividing mesh ..");
    let max_len2 = max_length * max_length;
    let mut heap = BinaryHeap::new();
    for i in 0..dedge.e2e.len() {
        let v0 = faces.get(i % 3, i / 3);
        let v1 = faces.get((i + 1) % 3, i / 3);
        if dedge.non_manifold[v0 as usize] || dedge.non_manifold[v1 as usize] {
            continue;
        }
        let length = (vertices[v0 as usize] - vertices[v1 as usize]).squared_norm();
        if length > max_len2 {
            let other = dedge.e2e[i];
            if other == INVALID || other > i as u32 {
                heap.push(HeapEdge {
                    len2: length,
                    id: i as u32,
                });
            }
        }
    }

    let mut n_v = vertices.len();
    let mut n_f = faces.cols();
    let mut n_split = 0u32;

    let set_pair = |e2e: &mut Vec<u32>, a: usize, b: u32| {
        e2e[a] = b;
        if b != INVALID {
            e2e[b as usize] = a as u32;
        }
    };

    // Long sliver triangles against a short target edge can ask for billions
    // of splits (crowd card C033 requested a 16 GiB buffer). Stop at a bound
    // that still leaves a valid mesh; the field solve copes with longer edges.
    let max_vertices = (vertices.len() * 16).clamp(1 << 18, 1 << 20);
    while let Some(edge) = heap.pop() {
        if n_v >= max_vertices {
            crate::say!("Subdivision stopped at {n_v} vertices.");
            break;
        }
        let e0 = edge.id;
        if e0 as usize >= dedge.e2e.len() {
            continue;
        }
        let e1 = dedge.e2e[e0 as usize];
        let is_boundary = e1 == INVALID;
        let f0 = (e0 / 3) as usize;
        let f1 = if is_boundary { 0 } else { (e1 / 3) as usize };
        let v0 = faces.get((e0 % 3) as usize, f0);
        let v0p = faces.get(((e0 + 2) % 3) as usize, f0);
        let v1 = faces.get(((e0 + 1) % 3) as usize, f0);
        if (vertices[v0 as usize] - vertices[v1 as usize]).squared_norm() != edge.len2 {
            continue;
        }
        let v1p = if is_boundary {
            INVALID
        } else {
            faces.get(((e1 + 2) % 3) as usize, f1)
        };
        let vn = n_v;
        n_v += 1;
        n_split += 1;

        if vertices.len() < n_v {
            let cap = n_v.max(vertices.len() * 2);
            vertices.resize(cap, Vec3::ZERO);
            dedge.v2e.resize(cap, INVALID);
            dedge.boundary.resize(cap, false);
            dedge.non_manifold.resize(cap, false);
        }
        vertices[vn] = (vertices[v0 as usize] + vertices[v1 as usize]) * 0.5;
        dedge.non_manifold[vn] = false;
        dedge.boundary[vn] = is_boundary;

        let f2 = if is_boundary {
            INVALID as usize
        } else {
            let f = n_f;
            n_f += 1;
            f
        };
        let f3 = n_f;
        n_f += 1;

        if faces.cols() < n_f {
            faces.resize_cols(n_f.max(faces.cols() * 2));
            dedge.e2e.resize(faces.cols() * 3, INVALID);
        }

        faces.set(0, f0, vn as u32);
        faces.set(1, f0, v0p);
        faces.set(2, f0, v0);
        if !is_boundary {
            faces.set(0, f1, vn as u32);
            faces.set(1, f1, v0);
            faces.set(2, f1, v1p);
            faces.set(0, f2, vn as u32);
            faces.set(1, f2, v1p);
            faces.set(2, f2, v1);
        }
        faces.set(0, f3, vn as u32);
        faces.set(1, f3, v1);
        faces.set(2, f3, v0p);

        let e0p = dedge.e2e[dedge_prev_3(e0) as usize];
        let e0n = dedge.e2e[dedge_next_3(e0) as usize];
        set_pair(&mut dedge.e2e, 3 * f0, 3 * f3 as u32 + 2);
        set_pair(&mut dedge.e2e, 3 * f0 + 1, e0p);
        set_pair(&mut dedge.e2e, 3 * f3 + 1, e0n);
        if is_boundary {
            set_pair(&mut dedge.e2e, 3 * f0 + 2, INVALID);
            set_pair(&mut dedge.e2e, 3 * f3, INVALID);
        } else {
            let e1p = dedge.e2e[dedge_prev_3(e1) as usize];
            let e1n = dedge.e2e[dedge_next_3(e1) as usize];
            set_pair(&mut dedge.e2e, 3 * f0 + 2, 3 * f1 as u32);
            set_pair(&mut dedge.e2e, 3 * f1 + 1, e1n);
            set_pair(&mut dedge.e2e, 3 * f1 + 2, 3 * f2 as u32);
            set_pair(&mut dedge.e2e, 3 * f2 + 1, e1p);
            set_pair(&mut dedge.e2e, 3 * f2 + 2, 3 * f3 as u32);
        }

        dedge.v2e[v0 as usize] = 3 * f0 as u32 + 2;
        dedge.v2e[vn] = 3 * f0 as u32;
        dedge.v2e[v1 as usize] = 3 * f3 as u32 + 1;
        dedge.v2e[v0p as usize] = 3 * f0 as u32 + 1;
        if !is_boundary {
            dedge.v2e[v1p as usize] = 3 * f1 as u32 + 2;
        }

        let schedule = |heap: &mut BinaryHeap<HeapEdge>,
                        faces: &Faces,
                        vertices: &[Vec3],
                        f: usize| {
            for i in 0..3 {
                let a = faces.get(i, f);
                let b = faces.get((i + 1) % 3, f);
                let length = (vertices[a as usize] - vertices[b as usize]).squared_norm();
                if length > max_len2 {
                    heap.push(HeapEdge {
                        len2: length,
                        id: (f * 3 + i) as u32,
                    });
                }
            }
        };
        schedule(&mut heap, faces, vertices, f0);
        if !is_boundary {
            schedule(&mut heap, faces, vertices, f2);
            schedule(&mut heap, faces, vertices, f1);
        }
        schedule(&mut heap, faces, vertices, f3);
    }

    faces.resize_cols(n_f);
    vertices.truncate(n_v);
    dedge.v2e.truncate(n_v);
    dedge.boundary.truncate(n_v);
    dedge.non_manifold.truncate(n_v);
    dedge.e2e.truncate(n_f * 3);
    crate::say!("done. (split {n_split} edges, new V={n_v}, F={n_f})");
}
