//! Unstructured multi-resolution graph used by the field smoother.

use crate::adjacency::{Adjacency, Link};
use crate::field::{compat_orientation_extrinsic, compat_position_extrinsic};
use crate::math::{coordinate_system, Pcg32, Vec3, INVALID, RCPOVERFLOW};
use crate::mesh::Faces;

pub const MAX_DEPTH: usize = 25;

#[derive(Clone, Debug)]
pub struct Level {
    pub v: Vec<Vec3>,
    pub n: Vec<Vec3>,
    pub a: Vec<f32>,
    pub q: Vec<Vec3>,
    pub o: Vec<Vec3>,
    pub cq: Vec<Vec3>,
    pub co: Vec<Vec3>,
    pub cqw: Vec<f32>,
    pub cow: Vec<f32>,
    pub adj: Adjacency,
    pub phases: Vec<Vec<u32>>,
}

#[derive(Clone, Debug)]
pub struct Hierarchy {
    pub faces: Faces,
    pub e2e: Vec<u32>,
    pub levels: Vec<Level>,
    /// `to_upper[level][coarse]` = two vertices of `level` that collapsed into `coarse`.
    pub to_upper: Vec<Vec<[u32; 2]>>,
    pub scale: f32,
    pub frozen_q: bool,
    pub frozen_o: bool,
    pub iterations_q: i32,
    pub iterations_o: i32,
    pub total_size: usize,
}

impl Hierarchy {
    pub fn new() -> Self {
        Self {
            faces: Faces::empty(),
            e2e: Vec::new(),
            levels: Vec::new(),
            to_upper: Vec::new(),
            scale: 0.0,
            frozen_q: false,
            frozen_o: false,
            iterations_q: -1,
            iterations_o: -1,
            total_size: 0,
        }
    }

    pub fn size(&self, level: usize) -> usize {
        self.levels[level].v.len()
    }

    pub fn clear_constraints(&mut self) {
        if self.levels.is_empty() {
            return;
        }
        crate::say!("Allocating memory for constraints ..");
        for level in &mut self.levels {
            let n = level.v.len();
            level.cq = vec![Vec3::ZERO; n];
            level.co = vec![Vec3::ZERO; n];
            level.cqw = vec![0.0; n];
            level.cow = vec![0.0; n];
        }
    }

    pub fn build(&mut self, deterministic: bool) {
        crate::say!("Processing level 0 ..");
        let phases = graph_coloring(&self.levels[0].adj, self.levels[0].v.len(), deterministic);
        self.levels[0].phases = phases;
        self.total_size = self.levels[0].v.len();

        crate::say!("Building multiresolution hierarchy ..");
        for _ in 0..MAX_DEPTH {
            let i = self.levels.len() - 1;
            let (v_p, n_p, a_p, to_upper, adj_p) = downsample(
                &self.levels[i].adj,
                &self.levels[i].v,
                &self.levels[i].n,
                &self.levels[i].a,
                deterministic,
            );
            if v_p.is_empty() {
                break;
            }
            let phases_p = graph_coloring(&adj_p, v_p.len(), deterministic);
            self.total_size += v_p.len();
            let n = v_p.len();
            self.to_upper.push(to_upper);
            self.levels.push(Level {
                v: v_p,
                n: n_p,
                a: a_p,
                q: Vec::new(),
                o: Vec::new(),
                cq: Vec::new(),
                co: Vec::new(),
                cqw: Vec::new(),
                cow: Vec::new(),
                adj: adj_p,
                phases: phases_p,
            });
            if n == 1 {
                break;
            }
        }
        self.iterations_q = -1;
        self.iterations_o = -1;
        self.frozen_o = false;
        self.frozen_q = false;
        crate::say!("Hierarchy has {} levels.", self.levels.len());
    }

    pub fn reset_solution(&mut self) {
        crate::say!("Setting to random solution ..");
        for level in &mut self.levels {
            level.q = init_random_tangent(&level.n);
            level.o = init_random_position(&level.v, &level.n, self.scale);
        }
        self.frozen_o = false;
        self.frozen_q = false;
        crate::say!("done.");
    }

    pub fn propagate_constraints(&mut self, rosy: i32, posy: i32) {
        if self.levels.is_empty() {
            return;
        }
        crate::say!("Propagating constraints ..");
        let scale = self.scale;
        let inv_scale = 1.0 / scale;
        let n_levels = self.levels.len();
        for l in 0..n_levels - 1 {
            let n_coarse = self.levels[l + 1].v.len();
            let mut cq_next = vec![Vec3::ZERO; n_coarse];
            let mut co_next = vec![Vec3::ZERO; n_coarse];
            let mut cqw_next = vec![0.0; n_coarse];
            let mut cow_next = vec![0.0; n_coarse];
            for i in 0..n_coarse {
                let upper = self.to_upper[l][i];
                let mut cq = Vec3::ZERO;
                let mut co = Vec3::ZERO;
                let mut cqw = 0.0;
                let mut cow = 0.0;
                let has_cq0 = self.levels[l].cqw[upper[0] as usize] != 0.0;
                let has_cq1 = upper[1] != INVALID && self.levels[l].cqw[upper[1] as usize] != 0.0;
                let has_co0 = self.levels[l].cow[upper[0] as usize] != 0.0;
                let has_co1 = upper[1] != INVALID && self.levels[l].cow[upper[1] as usize] != 0.0;

                if has_cq0 && !has_cq1 {
                    cq = self.levels[l].cq[upper[0] as usize];
                    cqw = self.levels[l].cqw[upper[0] as usize];
                } else if has_cq1 && !has_cq0 {
                    cq = self.levels[l].cq[upper[1] as usize];
                    cqw = self.levels[l].cqw[upper[1] as usize];
                } else if has_cq1 && has_cq0 {
                    let (a, b) = compat_orientation_extrinsic(
                        rosy,
                        self.levels[l].cq[upper[0] as usize],
                        self.levels[l].n[upper[0] as usize],
                        self.levels[l].cq[upper[1] as usize],
                        self.levels[l].n[upper[1] as usize],
                    );
                    cq = a * self.levels[l].cqw[upper[0] as usize]
                        + b * self.levels[l].cqw[upper[1] as usize];
                    cqw = self.levels[l].cqw[upper[0] as usize]
                        + self.levels[l].cqw[upper[1] as usize];
                }
                if cq != Vec3::ZERO {
                    let n = self.levels[l + 1].n[i];
                    cq -= n * n.dot(cq);
                    if cq.squared_norm() > RCPOVERFLOW {
                        cq = cq.normalized();
                    }
                }

                if has_co0 && !has_co1 {
                    co = self.levels[l].co[upper[0] as usize];
                    cow = self.levels[l].cow[upper[0] as usize];
                } else if has_co1 && !has_co0 {
                    co = self.levels[l].co[upper[1] as usize];
                    cow = self.levels[l].cow[upper[1] as usize];
                } else if has_co1 && has_co0 {
                    let (a, b) = compat_position_extrinsic(
                        posy,
                        self.levels[l].v[upper[0] as usize],
                        self.levels[l].n[upper[0] as usize],
                        self.levels[l].cq[upper[0] as usize],
                        self.levels[l].co[upper[0] as usize],
                        self.levels[l].v[upper[1] as usize],
                        self.levels[l].n[upper[1] as usize],
                        self.levels[l].cq[upper[1] as usize],
                        self.levels[l].co[upper[1] as usize],
                        scale,
                        inv_scale,
                    );
                    cow = self.levels[l].cow[upper[0] as usize]
                        + self.levels[l].cow[upper[1] as usize];
                    co = (a * self.levels[l].cow[upper[0] as usize]
                        + b * self.levels[l].cow[upper[1] as usize])
                        / cow;
                }
                if co != Vec3::ZERO {
                    let n = self.levels[l + 1].n[i];
                    let v = self.levels[l + 1].v[i];
                    // Matches the original source, which projects with `cq` here.
                    co -= n * n.dot(cq - v);
                }
                if cqw > 0.0 {
                    cqw = 1.0;
                }
                if cow > 0.0 {
                    cow = 1.0;
                }
                cqw_next[i] = cqw;
                cow_next[i] = cow;
                cq_next[i] = cq;
                co_next[i] = co;
            }
            self.levels[l + 1].cq = cq_next;
            self.levels[l + 1].co = co_next;
            self.levels[l + 1].cqw = cqw_next;
            self.levels[l + 1].cow = cow_next;
        }
        crate::say!("done.");
    }
}

impl Default for Hierarchy {
    fn default() -> Self {
        Self::new()
    }
}

struct Entry {
    i: u32,
    j: u32,
    order: f32,
}

fn downsample(
    adj: &Adjacency,
    v: &[Vec3],
    n: &[Vec3],
    a: &[f32],
    deterministic: bool,
) -> (Vec<Vec3>, Vec<Vec3>, Vec<f32>, Vec<[u32; 2]>, Adjacency) {
    crate::say_raw!("  Collapsing .. ");
    let mut entries = Vec::with_capacity(adj.n_links());
    for i in 0..v.len() {
        for link in adj.neighbors(i) {
            let k = link.id as usize;
            let dp = n[i].dot(n[k]);
            let ratio = if a[i] > a[k] {
                if a[k] > 0.0 { a[i] / a[k] } else { f32::MAX }
            } else if a[i] > 0.0 {
                a[k] / a[i]
            } else {
                f32::MAX
            };
            entries.push(Entry {
                i: i as u32,
                j: k as u32,
                order: dp * ratio,
            });
        }
    }
    if deterministic {
        entries.sort_by(|a, b| b.order.partial_cmp(&a.order).unwrap_or(std::cmp::Ordering::Equal));
    } else {
        entries.sort_unstable_by(|a, b| {
            b.order.partial_cmp(&a.order).unwrap_or(std::cmp::Ordering::Equal)
        });
    }

    let mut merge_flag = vec![false; v.len()];
    let mut n_collapsed = 0usize;
    for i in 0..entries.len() {
        let e = &entries[i];
        if merge_flag[e.i as usize] || merge_flag[e.j as usize] {
            continue;
        }
        merge_flag[e.i as usize] = true;
        merge_flag[e.j as usize] = true;
        entries[n_collapsed] = Entry {
            i: e.i,
            j: e.j,
            order: e.order,
        };
        n_collapsed += 1;
    }
    let vertex_count = v.len() - n_collapsed;
    let mut v_p = vec![Vec3::ZERO; vertex_count];
    let mut n_p = vec![Vec3::X; vertex_count];
    let mut a_p = vec![0.0; vertex_count];
    let mut to_upper = vec![[INVALID, INVALID]; vertex_count];
    let mut to_lower = vec![INVALID; v.len()];

    for i in 0..n_collapsed {
        let e = &entries[i];
        let area1 = a[e.i as usize];
        let area2 = a[e.j as usize];
        let surface = area1 + area2;
        v_p[i] = if surface > RCPOVERFLOW {
            (v[e.i as usize] * area1 + v[e.j as usize] * area2) / surface
        } else {
            (v[e.i as usize] + v[e.j as usize]) * 0.5
        };
        let normal = n[e.i as usize] * area1 + n[e.j as usize] * area2;
        let norm = normal.norm();
        n_p[i] = if norm > RCPOVERFLOW { normal / norm } else { Vec3::X };
        a_p[i] = surface;
        to_upper[i] = [e.i, e.j];
        to_lower[e.i as usize] = i as u32;
        to_lower[e.j as usize] = i as u32;
    }

    let mut offset = n_collapsed;
    for i in 0..v.len() {
        if !merge_flag[i] {
            let idx = offset;
            offset += 1;
            v_p[idx] = v[i];
            n_p[idx] = n[i];
            a_p[idx] = a[i];
            to_upper[idx] = [i as u32, INVALID];
            to_lower[i] = idx as u32;
        }
    }

    let mut lists = vec![Vec::new(); vertex_count];
    for i in 0..vertex_count {
        let mut scratch = Vec::new();
        for k in 0..2 {
            let upper = to_upper[i][k];
            if upper == INVALID {
                continue;
            }
            for link in adj.neighbors(upper as usize) {
                scratch.push(Link::with_weight(to_lower[link.id as usize], link.weight));
            }
        }
        scratch.sort_by_key(|l| l.id);
        let mut id = INVALID;
        for link in scratch {
            if link.id == i as u32 {
                continue;
            }
            if id != link.id {
                lists[i].push(link);
                id = link.id;
            } else if let Some(last) = lists[i].last_mut() {
                last.weight += link.weight;
            }
        }
    }
    crate::say!("done. ({} -> {vertex_count} vertices)", v.len());
    (v_p, n_p, a_p, to_upper, Adjacency::from_lists(lists))
}

fn graph_coloring(adj: &Adjacency, size: usize, deterministic: bool) -> Vec<Vec<u32>> {
    crate::say_raw!("    Coloring .. ");
    let mut perm: Vec<u32> = (0..size as u32).collect();
    let mut rng = if deterministic {
        Pcg32::new()
    } else {
        Pcg32::seed(0x1234_5678_9abc_def0, 0x0fed_cba9_8765_4321)
    };
    rng.shuffle(&mut perm);

    let mut color = vec![-1i32; size];
    let mut possible: Vec<u8> = Vec::new();
    let mut size_per_color: Vec<u32> = Vec::new();
    let mut ncolors = 0i32;

    for i in 0..size {
        let ip = perm[i] as usize;
        for c in &mut possible {
            *c = 1;
        }
        for link in adj.neighbors(ip) {
            let c = color[link.id as usize];
            if c >= 0 {
                possible[c as usize] = 0;
            }
        }
        let mut chosen = -1i32;
        for (j, ok) in possible.iter().enumerate() {
            if *ok != 0 {
                chosen = j as i32;
                break;
            }
        }
        if chosen < 0 {
            chosen = ncolors;
            ncolors += 1;
            possible.resize(ncolors as usize, 1);
            size_per_color.push(0);
        }
        color[ip] = chosen;
        size_per_color[chosen as usize] += 1;
    }

    let mut phases = vec![Vec::new(); ncolors as usize];
    for (i, c) in color.iter().enumerate() {
        phases[*c as usize].push(i as u32);
    }
    crate::say!("done. ({} colors)", phases.len());
    phases
}

fn init_random_tangent(normals: &[Vec3]) -> Vec<Vec3> {
    let mut rng = Pcg32::new();
    normals
        .iter()
        .map(|n| {
            let (s, t) = coordinate_system(*n);
            let angle = rng.next_f32() * std::f32::consts::TAU;
            s * angle.cos() + t * angle.sin()
        })
        .collect()
}

fn init_random_position(positions: &[Vec3], normals: &[Vec3], scale: f32) -> Vec<Vec3> {
    let mut rng = Pcg32::new();
    positions
        .iter()
        .zip(normals)
        .map(|(p, n)| {
            let (s, t) = coordinate_system(*n);
            let x = rng.next_f32() * 2.0 - 1.0;
            let y = rng.next_f32() * 2.0 - 1.0;
            *p + (s * x + t * y) * scale
        })
        .collect()
}
