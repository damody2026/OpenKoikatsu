//! Compressed adjacency of the mesh graph. Each link also carries the integer
//! rotation / translation variables used after the field is frozen.

use crate::dedge::{dedge_next_3, dedge_prev_3};
use crate::math::{Vec3, INVALID, RCPOVERFLOW};
use crate::mesh::Faces;

#[derive(Clone, Copy, Debug)]
pub struct Link {
    pub id: u32,
    pub weight: f32,
    pub rot: [u8; 2],
    pub tu: [i32; 2],
    pub tv: [i32; 2],
}

impl Link {
    pub fn new(id: u32) -> Self {
        Self {
            id,
            weight: 1.0,
            rot: [0, 0],
            tu: [0, 0],
            tv: [0, 0],
        }
    }

    pub fn with_weight(id: u32, weight: f32) -> Self {
        Self {
            weight,
            ..Self::new(id)
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Adjacency {
    pub offsets: Vec<u32>,
    pub links: Vec<Link>,
}

impl Adjacency {
    pub fn from_lists(lists: Vec<Vec<Link>>) -> Self {
        let mut offsets = Vec::with_capacity(lists.len() + 1);
        let mut links = Vec::new();
        offsets.push(0);
        for list in lists {
            links.extend(list);
            offsets.push(links.len() as u32);
        }
        Self { offsets, links }
    }

    pub fn vertex_count(&self) -> usize {
        self.offsets.len().saturating_sub(1)
    }

    pub fn range(&self, i: usize) -> std::ops::Range<usize> {
        self.offsets[i] as usize..self.offsets[i + 1] as usize
    }

    pub fn neighbors(&self, i: usize) -> &[Link] {
        &self.links[self.range(i)]
    }

    pub fn neighbors_mut(&mut self, i: usize) -> &mut [Link] {
        let r = self.range(i);
        &mut self.links[r]
    }

    pub fn n_links(&self) -> usize {
        self.links.len()
    }
}

pub fn generate_adjacency_uniform(
    faces: &Faces,
    v2e: &[u32],
    e2e: &[u32],
    non_manifold: &[bool],
) -> Adjacency {
    crate::say!("Generating adjacency matrix ..");
    let n = v2e.len();
    let mut lists = vec![Vec::new(); n];
    for i in 0..n {
        let mut edge = v2e[i];
        let stop = edge;
        if non_manifold[i] || edge == INVALID {
            continue;
        }
        let mut it = 0u32;
        loop {
            let base = edge % 3;
            let f = (edge / 3) as usize;
            let opp = e2e[edge as usize];
            if it == 0 {
                lists[i].push(Link::new(faces.get((base as usize + 2) % 3, f)));
            }
            if opp == INVALID {
                lists[i].push(Link::new(faces.get((base as usize + 1) % 3, f)));
                break;
            }
            let next = dedge_next_3(opp);
            if next != stop {
                lists[i].push(Link::new(faces.get((base as usize + 1) % 3, f)));
            }
            edge = next;
            it += 1;
            if edge == stop {
                break;
            }
        }
    }
    Adjacency::from_lists(lists)
}

pub fn generate_adjacency_cotan(
    faces: &Faces,
    vertices: &[Vec3],
    v2e: &[u32],
    e2e: &[u32],
    non_manifold: &[bool],
) -> Adjacency {
    crate::say!("Computing cotangent Laplacian ..");
    let n = v2e.len();
    let mut lists = vec![Vec::new(); n];
    for i in 0..n {
        let mut edge = v2e[i];
        let stop = edge;
        if non_manifold[i] || edge == INVALID {
            continue;
        }
        let mut it = 0u32;
        loop {
            let f = (edge / 3) as usize;
            let curr = (edge % 3) as usize;
            let next_i = (curr + 1) % 3;
            let prev_i = (curr + 2) % 3;
            if it == 0 {
                let p = vertices[faces.get(next_i, f) as usize];
                let d0 = vertices[faces.get(prev_i, f) as usize] - p;
                let d1 = vertices[faces.get(curr, f) as usize] - p;
                let mut cot = 0.0;
                let sin_alpha = d0.cross(d1).norm();
                if sin_alpha > RCPOVERFLOW {
                    cot = d0.dot(d1) / sin_alpha;
                }
                let opp = e2e[dedge_prev_3(edge) as usize];
                if opp != INVALID {
                    let o_f = (opp / 3) as usize;
                    let o_curr = (opp % 3) as usize;
                    let o_next = (o_curr + 1) % 3;
                    let o_prev = (o_curr + 2) % 3;
                    let p = vertices[faces.get(o_prev, o_f) as usize];
                    let d0 = vertices[faces.get(o_curr, o_f) as usize] - p;
                    let d1 = vertices[faces.get(o_next, o_f) as usize] - p;
                    let sin_alpha = d0.cross(d1).norm();
                    if sin_alpha > RCPOVERFLOW {
                        cot += d0.dot(d1) / sin_alpha;
                    }
                }
                lists[i].push(Link::with_weight(faces.get(prev_i, f), cot * 0.5));
            }
            let opp = e2e[edge as usize];
            if opp == INVALID {
                let p = vertices[faces.get(prev_i, f) as usize];
                let d0 = vertices[faces.get(curr, f) as usize] - p;
                let d1 = vertices[faces.get(next_i, f) as usize] - p;
                let mut cot = 0.0;
                let sin_alpha = d0.cross(d1).norm();
                if sin_alpha > RCPOVERFLOW {
                    cot = d0.dot(d1) / sin_alpha;
                }
                lists[i].push(Link::with_weight(faces.get(next_i, f), cot * 0.5));
                break;
            }
            let next = dedge_next_3(opp);
            if next != stop {
                let p = vertices[faces.get(prev_i, f) as usize];
                let d0 = vertices[faces.get(curr, f) as usize] - p;
                let d1 = vertices[faces.get(next_i, f) as usize] - p;
                let mut cot = 0.0;
                let sin_alpha = d0.cross(d1).norm();
                if sin_alpha > RCPOVERFLOW {
                    cot = d0.dot(d1) / sin_alpha;
                }
                let o_f = (opp / 3) as usize;
                let o_curr = (opp % 3) as usize;
                let o_next = (o_curr + 1) % 3;
                let o_prev = (o_curr + 2) % 3;
                let p = vertices[faces.get(o_prev, o_f) as usize];
                let d0 = vertices[faces.get(o_curr, o_f) as usize] - p;
                let d1 = vertices[faces.get(o_next, o_f) as usize] - p;
                let sin_alpha = d0.cross(d1).norm();
                if sin_alpha > RCPOVERFLOW {
                    cot += d0.dot(d1) / sin_alpha;
                }
                lists[i].push(Link::with_weight(faces.get(next_i, f), cot * 0.5));
            }
            edge = next;
            it += 1;
            if edge == stop {
                break;
            }
        }
    }
    Adjacency::from_lists(lists)
}
