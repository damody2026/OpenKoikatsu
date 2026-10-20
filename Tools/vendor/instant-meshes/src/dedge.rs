//! Directed-edge topology for triangle and quad meshes.

use crate::math::{Vec3, INVALID};
use crate::mesh::Faces;

pub fn dedge_prev_3(e: u32) -> u32 {
    if e % 3 == 0 { e + 2 } else { e - 1 }
}
pub fn dedge_next_3(e: u32) -> u32 {
    if e % 3 == 2 { e - 2 } else { e + 1 }
}
pub fn dedge_prev(e: u32, deg: u32) -> u32 {
    if e % deg == 0 { e + (deg - 1) } else { e - 1 }
}
pub fn dedge_next(e: u32, deg: u32) -> u32 {
    if e % deg == deg - 1 { e - (deg - 1) } else { e + 1 }
}

pub struct Dedge {
    pub v2e: Vec<u32>,
    pub e2e: Vec<u32>,
    pub boundary: Vec<bool>,
    pub non_manifold: Vec<bool>,
}

pub fn build_dedge(faces: &Faces, n_vertices: usize, quiet: bool) -> Result<Dedge, String> {
    if !quiet {
        crate::say_raw!("Building a directed edge data structure .. ");
    }
    let deg = faces.degree as u32;
    let n_faces = faces.cols();
    let n_edges = n_faces * faces.degree;
    if n_vertices == 0 {
        return Err("Mesh has no vertices".into());
    }

    let mut outgoing: Vec<Vec<(u32, u32)>> = vec![Vec::new(); n_vertices];
    for f in 0..n_faces {
        for i in 0..faces.degree {
            let idx_cur = faces.get(i, f);
            let idx_next = faces.get((i + 1) % faces.degree, f);
            if idx_cur as usize >= n_vertices || idx_next as usize >= n_vertices {
                return Err("Mesh data contains an out-of-bounds vertex reference".into());
            }
            if idx_cur == idx_next {
                continue;
            }
            let edge_id = deg * f as u32 + i as u32;
            outgoing[idx_cur as usize].push((idx_next, edge_id));
        }
    }

    let mut non_manifold = vec![false; n_vertices];
    let mut e2e = vec![INVALID; n_edges];

    for f in 0..n_faces {
        for i in 0..faces.degree {
            let idx_cur = faces.get(i, f);
            let idx_next = faces.get((i + 1) % faces.degree, f);
            if idx_cur == idx_next {
                continue;
            }
            let edge_id_cur = deg * f as u32 + i as u32;
            let mut edge_id_opp = INVALID;
            for &(next, eid) in &outgoing[idx_next as usize] {
                if next == idx_cur {
                    if edge_id_opp == INVALID {
                        edge_id_opp = eid;
                    } else {
                        non_manifold[idx_cur as usize] = true;
                        non_manifold[idx_next as usize] = true;
                        edge_id_opp = INVALID;
                        break;
                    }
                }
            }
            if edge_id_opp != INVALID && edge_id_cur < edge_id_opp {
                e2e[edge_id_cur as usize] = edge_id_opp;
                e2e[edge_id_opp as usize] = edge_id_cur;
            }
        }
    }

    let mut v2e = vec![INVALID; n_vertices];
    let mut boundary = vec![false; n_vertices];
    let mut non_manifold_count = 0u32;
    let mut boundary_count = 0u32;
    let mut isolated_count = 0u32;

    for i in 0..n_vertices {
        if outgoing[i].is_empty() {
            isolated_count += 1;
            continue;
        }
        if non_manifold[i] {
            non_manifold_count += 1;
            continue;
        }
        let start = outgoing[i][0].1;
        let mut edge = start;
        let mut chosen = INVALID;
        loop {
            chosen = chosen.min(edge);
            let prev_edge = e2e[dedge_prev(edge, deg) as usize];
            if prev_edge == INVALID {
                chosen = edge;
                boundary[i] = true;
                boundary_count += 1;
                break;
            }
            edge = prev_edge;
            if edge == start {
                break;
            }
        }
        v2e[i] = chosen;
    }

    if !quiet {
        crate::say_raw!("done. (");
        if non_manifold_count > 0 {
            crate::say_raw!("{non_manifold_count} non-manifold vertices, ");
        }
        if boundary_count > 0 {
            crate::say_raw!("{boundary_count} boundary vertices, ");
        }
        if isolated_count > 0 {
            crate::say_raw!("{isolated_count} isolated vertices, ");
        }
        crate::say!(")");
    }

    let _ = Vec3::ZERO;
    Ok(Dedge {
        v2e,
        e2e,
        boundary,
        non_manifold,
    })
}
