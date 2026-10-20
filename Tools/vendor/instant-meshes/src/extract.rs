//! Extract a quad-dominant (or triangle) mesh from the integer grid of the fields.

use std::collections::{BTreeSet, HashMap, HashSet};

use crate::bvh::Bvh;
use crate::dedge::build_dedge;
use crate::dset::DisjointSets;
use crate::field::{compat_orientation_extrinsic, compat_position_index};
use crate::hierarchy::Hierarchy;
use crate::math::{coordinate_system, signum, smallest_eigenvector_sym, Vec2i, Vec3, INVALID};
use crate::mesh::Faces;
use crate::reorder::reorder_mesh;

#[derive(Clone, Copy, Debug)]
pub(crate) struct TaggedLink {
    id: u32,
    flag: u8,
}

impl TaggedLink {
    fn new(id: u32) -> Self {
        Self { id, flag: 0 }
    }
    fn used(self) -> bool {
        self.flag & 1 != 0
    }
    fn mark_used(&mut self) {
        self.flag |= 1;
    }
}

pub struct Extraction {
    pub positions: Vec<Vec3>,
    pub normals: Vec<Vec3>,
    pub face_normals: Vec<Vec3>,
    pub faces: Faces,
}

pub(crate) fn extract_graph(
    h: &Hierarchy,
    extrinsic: bool,
    rosy: i32,
    posy: i32,
    crease_in: &HashSet<u32>,
    deterministic: bool,
) -> (Vec<Vec<TaggedLink>>, Vec<Vec3>, Vec<Vec3>, HashSet<u32>) {
    let scale = h.scale;
    let inv_scale = 1.0 / scale;
    let level = &h.levels[0];
    let n = level.v.len();
    crate::say!("Step 1: Classifying edges ..");
    let dset = DisjointSets::new(n);
    let mut adj_new: Vec<Vec<TaggedLink>> = vec![Vec::new(); n];
    let mut collapse: Vec<(u32, u32, f32)> = Vec::new();

    for i in 0..n {
        let mut locked = i as u32;
        while !dset.try_lock(&mut locked) {}
        for link in level.adj.neighbors(i) {
            let j = link.id;
            if j < i as u32 {
                continue;
            }
            let (q_i, q_j) = if extrinsic {
                compat_orientation_extrinsic(rosy, level.q[i], level.n[i], level.q[j as usize], level.n[j as usize])
            } else {
                crate::field::compat_orientation(false, rosy, level.q[i], level.n[i], level.q[j as usize], level.n[j as usize])
            };
            let ((shift_i, shift_j), error) = compat_position_index(
                extrinsic,
                posy,
                level.v[i],
                level.n[i],
                q_i,
                level.o[i],
                level.v[j as usize],
                level.n[j as usize],
                q_j,
                level.o[j as usize],
                scale,
                inv_scale,
            );
            let diff = (shift_i - shift_j).abs();
            if diff.max_comp() > 1 || (diff == Vec2i::new(1, 1) && posy == 4) {
                continue;
            }
            if diff.sum_abs() == 0 {
                collapse.push((i as u32, j, error));
            } else {
                let mut locked_j = j;
                while !dset.try_lock(&mut locked_j) {}
                adj_new[i].push(TaggedLink::new(j));
                adj_new[j as usize].push(TaggedLink::new(i as u32));
                dset.unlock(locked_j);
            }
        }
        dset.unlock(locked);
    }

    if deterministic {
        collapse.sort_by(|a, b| a.2.partial_cmp(&b.2).unwrap_or(std::cmp::Ordering::Equal));
    } else {
        collapse.sort_unstable_by(|a, b| a.2.partial_cmp(&b.2).unwrap_or(std::cmp::Ordering::Equal));
    }
    crate::say!("Step 2: Collapsing {} edges ..", collapse.len());
    let mut n_conflicts = 0u32;
    let mut n_collapses = vec![0u16; n];
    for (mut a, mut b, _) in collapse {
        let mut ignore = false;
        loop {
            if a > b {
                std::mem::swap(&mut a, &mut b);
            }
            if !dset.try_lock(&mut a) {
                continue;
            }
            if !dset.try_lock(&mut b) {
                dset.unlock(a);
                if b == a {
                    ignore = true;
                    break;
                }
                continue;
            }
            break;
        }
        if ignore {
            continue;
        }
        let mut contained = false;
        for neighbor in &adj_new[a as usize] {
            if dset.find(neighbor.id) == b {
                contained = true;
                break;
            }
        }
        if contained {
            dset.unlock(a);
            dset.unlock(b);
            n_conflicts += 1;
            continue;
        }
        let mut temp = BTreeSet::new();
        for neighbor in &adj_new[a as usize] {
            temp.insert(dset.find(neighbor.id));
        }
        for neighbor in &adj_new[b as usize] {
            temp.insert(dset.find(neighbor.id));
        }
        let target = dset.unite_index_locked(a, b);
        let collapses = n_collapses[a as usize] + n_collapses[b as usize] + 1;
        adj_new[a as usize].clear();
        adj_new[b as usize].clear();
        adj_new[target as usize] = temp.into_iter().map(TaggedLink::new).collect();
        n_collapses[target as usize] = collapses;
        dset.unite_unlock(a, b);
    }
    crate::say!("done. (ignored {n_conflicts} conflicting edges)");

    crate::say!("Step 3: Assigning vertices ..");
    let mut n_vertices = 0u32;
    let mut vertex_map: HashMap<u32, u32> = HashMap::new();
    let mut avg_collapses = 0.0f32;
    let mut i = 0u32;
    while (i as usize) < adj_new.len() {
        if adj_new[i as usize].is_empty() {
            i += 1;
            continue;
        }
        if i != n_vertices {
            adj_new.swap(n_vertices as usize, i as usize);
            n_collapses.swap(n_vertices as usize, i as usize);
        }
        avg_collapses += n_collapses[n_vertices as usize] as f32;
        vertex_map.insert(i, n_vertices);
        n_vertices += 1;
        i += 1;
    }
    adj_new.truncate(n_vertices as usize);
    if n_vertices > 0 {
        avg_collapses /= n_vertices as f32;
    }
    for slot in &mut adj_new {
        let mut temp = BTreeSet::new();
        for k in slot.iter() {
            if let Some(&mapped) = vertex_map.get(&dset.find(k.id)) {
                temp.insert(mapped);
            }
        }
        *slot = temp.into_iter().map(TaggedLink::new).collect();
    }
    crate::say!("done. ({n_vertices} vertices)");

    crate::say!("Step 3a: Removing spurious vertices ..");
    let mut removed = 0u32;
    for i in 0..adj_new.len() {
        if n_collapses[i] as f32 > avg_collapses / 10.0 {
            continue;
        }
        let neighbors: Vec<u32> = adj_new[i].iter().map(|n| n.id).collect();
        for neighbor in neighbors {
            adj_new[neighbor as usize].retain(|v| v.id != i as u32);
        }
        adj_new[i].clear();
        removed += 1;
    }
    crate::say!("done. (removed {removed} vertices)");

    crate::say!("Step 4: Assigning positions to vertices ..");
    let mut positions = vec![Vec3::ZERO; n_vertices as usize];
    let mut normals = vec![Vec3::ZERO; n_vertices as usize];
    let mut crease_out = HashSet::new();
    for &c in crease_in {
        if let Some(&mapped) = vertex_map.get(&dset.find(c)) {
            crease_out.insert(mapped);
        }
    }
    let mut weight = vec![0.0f32; n_vertices as usize];
    let cow = &level.cow;
    for i in 0..level.v.len() {
        let Some(&j) = vertex_map.get(&dset.find(i as u32)) else {
            continue;
        };
        let w = (-(level.o[i] - level.v[i]).squared_norm() * inv_scale * inv_scale * 9.0).exp();
        if !cow.is_empty() && cow[i] != 0.0 {
            crease_out.insert(j);
        }
        positions[j as usize] += level.o[i] * w;
        normals[j as usize] += level.n[i] * w;
        weight[j as usize] += w;
    }
    for i in 0..n_vertices as usize {
        if weight[i] == 0.0 {
            crate::say!("Warning: vertex {i} did not receive any contributions!");
            continue;
        }
        positions[i] = positions[i] / weight[i];
        normals[i] = normals[i].normalized();
        if normals[i].squared_norm() == 0.0 {
            normals[i] = Vec3::X;
        }
    }
    crate::say!("done.");

    crate::say!("Step 5: Snapping and removing unnecessary edges ..");
    let mut changed = true;
    let mut n_removed = 0u32;
    let mut n_snapped = 0u32;
    // Snapping and edge removal can undo each other and oscillate forever on
    // some inputs (crowd card C064 jacket). Bound both loops.
    let mut outer_rounds = 0u32;
    while changed && outer_rounds < 16 {
        outer_rounds += 1;
        changed = false;
        let mut changed_inner = true;
        let mut inner_rounds = 0u32;
        while changed_inner && inner_rounds < 64 {
            inner_rounds += 1;
            changed_inner = false;
            let thresh = 0.3 * scale;
            let mut candidates: Vec<(f32, u32, u32, u32)> = Vec::new();
            for i_id in 0..adj_new.len() {
                let p_i = positions[i_id];
                let adj_i: Vec<u32> = adj_new[i_id].iter().map(|l| l.id).collect();
                for &j_id in &adj_i {
                    let p_j = positions[j_id as usize];
                    let adj_j: Vec<u32> = adj_new[j_id as usize].iter().map(|l| l.id).collect();
                    for &k_id in &adj_j {
                        if k_id == i_id as u32 {
                            continue;
                        }
                        let p_k = positions[k_id as usize];
                        let a = (p_j - p_k).norm();
                        let b = (p_i - p_j).norm();
                        let c = (p_i - p_k).norm();
                        if a > b.max(c) && a > 1e-12 {
                            let s = 0.5 * (a + b + c);
                            let area2 = s * (s - a) * (s - b) * (s - c);
                            if area2 > 0.0 {
                                let height = 2.0 * area2.sqrt() / a;
                                if height < thresh {
                                    candidates.push((height, i_id as u32, j_id, k_id));
                                }
                            }
                        }
                    }
                }
            }
            candidates.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));
            for (height0, i, j, k) in candidates {
                let edge1 = adj_new[i as usize].iter().any(|l| l.id == j);
                let edge2 = adj_new[j as usize].iter().any(|l| l.id == k);
                let edge3 = adj_new[k as usize].iter().any(|l| l.id == i);
                if !edge1 || !edge2 {
                    continue;
                }
                let p_i = positions[i as usize];
                let p_j = positions[j as usize];
                let p_k = positions[k as usize];
                let a = (p_j - p_k).norm();
                let b = (p_i - p_j).norm();
                let c = (p_i - p_k).norm();
                let s = 0.5 * (a + b + c);
                let area2 = s * (s - a) * (s - b) * (s - c);
                if area2 <= 0.0 || a < 1e-12 {
                    continue;
                }
                let height = 2.0 * area2.sqrt() / a;
                if (height - height0).abs() > 1e-5 {
                    continue;
                }
                if (p_i - p_j).norm() < thresh || (p_i - p_k).norm() < thresh {
                    let merge_id = if (p_i - p_j).norm() < thresh { j } else { k };
                    positions[i as usize] = (positions[i as usize] + positions[merge_id as usize]) * 0.5;
                    normals[i as usize] = (normals[i as usize] + normals[merge_id as usize]) * 0.5;
                    let mut updated = BTreeSet::new();
                    let neigh: Vec<u32> = adj_new[merge_id as usize].iter().map(|n| n.id).collect();
                    for n in neigh {
                        if n == i {
                            continue;
                        }
                        updated.insert(n);
                        for n2 in &mut adj_new[n as usize] {
                            if n2.id == merge_id {
                                n2.id = i;
                            }
                        }
                    }
                    for n in &adj_new[i as usize] {
                        updated.insert(n.id);
                    }
                    updated.remove(&i);
                    updated.remove(&merge_id);
                    adj_new[merge_id as usize].clear();
                    adj_new[i as usize].clear();
                    if crease_out.remove(&merge_id) {
                        crease_out.insert(i);
                    }
                    adj_new[i as usize] = updated.into_iter().map(TaggedLink::new).collect();
                } else {
                    positions[i as usize] = (p_j + p_k) * 0.5;
                    normals[i as usize] = (normals[j as usize] + normals[k as usize]).normalized();
                    if crease_out.contains(&j) && crease_out.contains(&k) {
                        crease_out.insert(i);
                    }
                    adj_new[j as usize].retain(|l| l.id != k);
                    adj_new[k as usize].retain(|l| l.id != j);
                    if !edge3 {
                        adj_new[i as usize].push(TaggedLink::new(k));
                        adj_new[k as usize].push(TaggedLink::new(i));
                    }
                }
                changed = true;
                changed_inner = true;
                n_snapped += 1;
            }
        }

        if posy == 4 {
            let mut candidates: Vec<(f32, u32, u32)> = Vec::new();
            for i in 0..adj_new.len() {
                let p_i = positions[i];
                let adj_i: Vec<u32> = adj_new[i].iter().map(|l| l.id).collect();
                for &j_id in &adj_i {
                    let p_j = positions[j_id as usize];
                    let mut n_tris = 0u32;
                    let mut length = 0.0;
                    for &k_id in &adj_i {
                        if k_id == j_id {
                            continue;
                        }
                        if !adj_new[j_id as usize].iter().any(|l| l.id == k_id) {
                            continue;
                        }
                        let p_k = positions[k_id as usize];
                        n_tris += 1;
                        length += (p_k - p_i).norm() + (p_k - p_j).norm();
                    }
                    if n_tris == 2 {
                        let exp_diag = length / 4.0 * std::f32::consts::SQRT_2;
                        let diag = (p_i - p_j).norm();
                        let score = ((diag - exp_diag) / diag.min(exp_diag)).abs();
                        candidates.push((score, i as u32, j_id));
                    }
                }
            }
            candidates.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));
            for (_, i_id, j_id) in candidates {
                let mut n_tris = 0u32;
                for link in &adj_new[i_id as usize] {
                    if adj_new[j_id as usize].iter().any(|l| l.id == link.id) {
                        n_tris += 1;
                    }
                }
                if n_tris == 2 {
                    adj_new[i_id as usize].retain(|l| l.id != j_id);
                    adj_new[j_id as usize].retain(|l| l.id != i_id);
                    changed = true;
                    n_removed += 1;
                }
            }
        }
    }
    crate::say!("done. (snapped {n_snapped} vertices, removed {n_removed} edges)");

    crate::say!("Step 6: Orienting edges ..");
    for i in 0..positions.len() {
        let (s, t) = coordinate_system(normals[i]);
        let p = positions[i];
        adj_new[i].sort_by(|j0, j1| {
            let v0 = positions[j0.id as usize] - p;
            let v1 = positions[j1.id as usize] - p;
            let a0 = t.dot(v0).atan2(s.dot(v0));
            let a1 = t.dot(v1).atan2(s.dot(v1));
            a1.partial_cmp(&a0).unwrap_or(std::cmp::Ordering::Equal)
        });
    }
    crate::say!("done.");
    (adj_new, positions, normals, crease_out)
}

pub(crate) fn extract_faces(
    mut adj: Vec<Vec<TaggedLink>>,
    mut positions: Vec<Vec3>,
    mut normals: Vec<Vec3>,
    posy: i32,
    scale: f32,
    mut crease: HashSet<u32>,
    fill_holes: bool,
    pure_quad: bool,
    bvh: Option<&Bvh>,
    smooth_iterations: i32,
) -> Extraction {
    let mut n_f = 0usize;
    let mut n_v = positions.len();
    let n_v_old = positions.len();
    let degree = posy as usize;
    let mut faces = Faces::new(degree, if posy == 4 { positions.len() } else { positions.len() * 2 });
    let mut irregular_faces: Vec<Vec<u32>> = Vec::new();
    let mut stats: Vec<u32> = vec![0; 10];

    // A hole walk (target 0) that enters a loop not containing its start
    // vertex never ends: edges are only marked used after success. Holes of 7
    // or more edges are never filled, so 64 steps loses no filled face and
    // keeps repeated failing walks cheap (an edge-count cap was quadratic).
    let max_cycle: usize = 64;
    let extract_cycle = |adj: &mut [Vec<TaggedLink>],
                         cur0: u32,
                         cur_idx0: usize,
                         target: usize|
     -> Vec<(u32, u32)> {
        let mut cur = cur0;
        let mut cur_idx = cur_idx0;
        let initial = cur;
        let mut result = Vec::new();
        let mut success = false;
        loop {
            if adj[cur as usize][cur_idx].used() || (target > 0 && result.len() + 1 > target) || result.len() >= max_cycle {
                break;
            }
            result.push((cur, cur_idx as u32));
            let next = adj[cur as usize][cur_idx].id;
            let next_rank = adj[next as usize].len();
            let mut idx = None;
            for j in 0..next_rank {
                if adj[next as usize][j].id == cur {
                    idx = Some(j);
                    break;
                }
            }
            let Some(idx) = idx else { break };
            if next_rank == 1 {
                break;
            }
            cur = next;
            cur_idx = (idx + 1) % next_rank;
            if cur == initial {
                success = target == 0 || result.len() == target;
                break;
            }
        }
        if success {
            for (v, e) in &result {
                adj[*v as usize][*e as usize].mark_used();
            }
            result
        } else {
            Vec::new()
        }
    };

    crate::say!("Step 7: Extracting faces ..");
    let mut n_faces_found = 0u32;
    for deg in 3..=8 {
        let mut i = 0u32;
        while (i as usize) < n_v_old {
            let mut j = 0usize;
            while j < adj[i as usize].len() {
                let result = extract_cycle(&mut adj, i, j, deg);
                if result.is_empty() {
                    j += 1;
                    continue;
                }
                if stats.len() <= result.len() {
                    stats.resize(result.len() + 1, 0);
                }
                stats[result.len()] += 1;
                if let Some(irregular) = fill_face(
                    &mut faces,
                    &mut positions,
                    &mut normals,
                    &mut n_f,
                    &mut n_v,
                    posy,
                    result,
                ) {
                    if !irregular.is_empty() {
                        irregular_faces.push(irregular);
                    }
                }
                n_faces_found += 1;
                j += 1;
            }
            i += 1;
        }
    }
    crate::say!("done. ({n_faces_found} faces)");

    if fill_holes {
        crate::say!("Step 8: Filling holes ..");
        let mut n_holes = 0u32;
        for i in 0..n_v_old {
            for j in 0..adj[i].len() {
                if adj[i][j].used() {
                    continue;
                }
                let j_id = adj[i][j].id as usize;
                for k in 0..adj[j_id].len() {
                    if adj[j_id][k].id == i as u32 && adj[j_id][k].used() {
                        adj[i][j].flag |= 2;
                        adj[j_id][k].flag |= 2;
                        break;
                    }
                }
            }
        }
        for slot in adj.iter_mut().take(n_v_old) {
            slot.retain(|l| l.flag & 2 != 0);
        }
        for i in 0..n_v_old {
            let mut j = 0usize;
            while j < adj[i].len() {
                let result = extract_cycle(&mut adj, i as u32, j, 0);
                if result.is_empty() {
                    j += 1;
                    continue;
                }
                if result.len() >= 7 {
                    crate::say!("Not trying to fill a hole of degree {}", result.len());
                    j += 1;
                    continue;
                }
                if stats.len() <= result.len() {
                    stats.resize(result.len() + 1, 0);
                }
                stats[result.len()] += 1;
                if let Some(irregular) = fill_face(
                    &mut faces,
                    &mut positions,
                    &mut normals,
                    &mut n_f,
                    &mut n_v,
                    posy,
                    result,
                ) {
                    if !irregular.is_empty() {
                        irregular_faces.push(irregular);
                    }
                }
                n_holes += 1;
                j += 1;
            }
        }
        crate::say!("done. ({n_holes} holes)");
    }

    crate::say_raw!("Intermediate mesh statistics: ");
    let mut first = true;
    for (i, count) in stats.iter().enumerate() {
        if *count == 0 {
            continue;
        }
        if !first {
            crate::say_raw!(", ");
        }
        crate::say_raw!("degree {i}: {count} faces");
        first = false;
    }
    crate::say!();

    if posy == 4 && pure_quad {
        faces.resize_cols(n_f);
        positions.truncate(n_v);
        normals.truncate(n_v);
        crate::say!("Step 9: Regular subdivision into pure quad mesh ..");
        let dedge = build_dedge(&faces, positions.len(), true).expect("dedge on extracted mesh");
        let mut e2e_v = vec![INVALID; dedge.e2e.len()];
        let n_f_old = n_f;
        for i in 0..n_f_old {
            let face = [
                faces.get(0, i),
                faces.get(1, i),
                faces.get(2, i),
                faces.get(3, i),
            ];
            if face[2] == face[3] {
                continue;
            }
            let mut fc = Vec3::ZERO;
            let mut fc_n = Vec3::ZERO;
            for &v in &face {
                fc += positions[v as usize];
                fc_n += normals[v as usize];
            }
            let idx_fc = n_v;
            n_v += 1;
            let all_crease = face.iter().all(|v| crease.contains(v));
            if positions.len() < n_v {
                positions.resize(n_v.max(positions.len() * 2), Vec3::ZERO);
                normals.resize(positions.len(), Vec3::ZERO);
            }
            positions[idx_fc] = fc * 0.25;
            normals[idx_fc] = fc_n.normalized();
            if all_crease {
                crease.insert(idx_fc as u32);
            }
            let mut idx_ecs = [INVALID; 4];
            for j in 0..4 {
                let i0 = face[j];
                let i1 = face[(j + 1) % 4];
                if i0 == i1 {
                    continue;
                }
                let edge = (i * 4 + j) as u32;
                let edge_opp = dedge.e2e[edge as usize];
                let mut idx_ec = if edge_opp != INVALID {
                    e2e_v[edge_opp as usize]
                } else {
                    INVALID
                };
                if idx_ec == INVALID {
                    idx_ec = n_v as u32;
                    n_v += 1;
                    if positions.len() < n_v {
                        positions.resize(n_v.max(positions.len() * 2), Vec3::ZERO);
                        normals.resize(positions.len(), Vec3::ZERO);
                    }
                    positions[idx_ec as usize] =
                        (positions[i0 as usize] + positions[i1 as usize]) * 0.5;
                    normals[idx_ec as usize] =
                        (normals[i0 as usize] + normals[i1 as usize]).normalized();
                    if crease.contains(&i0) && crease.contains(&i1) {
                        crease.insert(idx_ec);
                    }
                    e2e_v[edge as usize] = idx_ec;
                }
                idx_ecs[j] = idx_ec;
            }
            let mut first_face = true;
            for j in 0..4 {
                let i1 = face[(j + 1) % 4];
                let i0 = face[j];
                if i0 == i1 {
                    continue;
                }
                let idx_ec0 = idx_ecs[j];
                let idx_ec1 = idx_ecs[(j + 1) % 4];
                if idx_ec0 == INVALID || idx_ec1 == INVALID {
                    continue;
                }
                let idx_f = if first_face {
                    first_face = false;
                    i
                } else {
                    let id = n_f;
                    n_f += 1;
                    faces.ensure_cols(n_f);
                    id
                };
                faces.set(0, idx_f, idx_ec0);
                faces.set(1, idx_f, i1);
                faces.set(2, idx_f, idx_ec1);
                faces.set(3, idx_f, idx_fc as u32);
            }
        }

        for f in &irregular_faces {
            if f.is_empty() {
                continue;
            }
            let idx_fc = faces.get(3, f[0] as usize);
            let mut ecs = Vec::new();
            for i in 0..f.len() {
                let edge = 4 * f[i];
                let edge_opp = if (edge as usize) < dedge.e2e.len() {
                    dedge.e2e[edge as usize]
                } else {
                    INVALID
                };
                let i0 = faces.get(0, f[i] as usize);
                let i1 = faces.get(1, f[i] as usize);
                let mut idx_ec = if edge_opp != INVALID {
                    e2e_v[edge_opp as usize]
                } else {
                    INVALID
                };
                if idx_ec == INVALID {
                    idx_ec = n_v as u32;
                    n_v += 1;
                    if positions.len() < n_v {
                        positions.resize(n_v.max(positions.len() * 2), Vec3::ZERO);
                        normals.resize(positions.len(), Vec3::ZERO);
                    }
                    positions[idx_ec as usize] =
                        (positions[i0 as usize] + positions[i1 as usize]) * 0.5;
                    normals[idx_ec as usize] =
                        (normals[i0 as usize] + normals[i1 as usize]).normalized();
                    if crease.contains(&i0) && crease.contains(&i1) {
                        crease.insert(idx_ec);
                    }
                    if (edge as usize) < e2e_v.len() {
                        e2e_v[edge as usize] = idx_ec;
                    }
                }
                ecs.push(i0);
                ecs.push(idx_ec);
            }
            for i in 0..f.len() {
                let fi = f[i] as usize;
                faces.set(0, fi, ecs[2 * i + 1]);
                faces.set(1, fi, ecs[(2 * i + 2) % ecs.len()]);
                faces.set(2, fi, ecs[(2 * i + 3) % ecs.len()]);
                faces.set(3, fi, idx_fc);
            }
        }
        crate::say!("done.");
    }

    faces.resize_cols(n_f);
    positions.truncate(n_v);
    normals.truncate(n_v);

    if smooth_iterations > 0 && n_f > 0 {
        crate::say!("Step 10: Running {smooth_iterations} smoothing & reprojection steps ..");
        let mut adj_v: Vec<BTreeSet<u32>> = vec![BTreeSet::new(); positions.len()];
        for f in 0..faces.cols() {
            if posy == 4 && faces.get(2, f) == faces.get(3, f) {
                let mut i0 = faces.get(0, f);
                let mut i1 = faces.get(1, f);
                if i0 < i1 {
                    std::mem::swap(&mut i0, &mut i1);
                }
                if i0 != i1 {
                    adj_v[i0 as usize].insert(i1);
                    adj_v[i1 as usize].insert(i0);
                }
            } else {
                for j in 0..faces.degree {
                    let mut i0 = faces.get(j, f);
                    let mut i1 = faces.get((j + 1) % faces.degree, f);
                    if i0 < i1 {
                        std::mem::swap(&mut i0, &mut i1);
                    }
                    if i0 != i1 {
                        adj_v[i0 as usize].insert(i1);
                        adj_v[i1 as usize].insert(i0);
                    }
                }
            }
        }
        for _ in 0..smooth_iterations {
            let mut o_prime = positions.clone();
            let mut n_prime = normals.clone();
            for i in 0..positions.len() {
                if adj_v[i].is_empty() || crease.contains(&(i as u32)) {
                    continue;
                }
                let mut centroid = Vec3::ZERO;
                let mut avg_normal = normals[i];
                for &j in &adj_v[i] {
                    centroid += positions[j as usize];
                    avg_normal += normals[j as usize];
                }
                centroid = centroid / adj_v[i].len() as f32;
                let mut cov = [[0.0f32; 3]; 3];
                for &j in &adj_v[i] {
                    let d = positions[j as usize] - centroid;
                    let c = [d.x, d.y, d.z];
                    for r in 0..3 {
                        for c_i in 0..3 {
                            cov[r][c_i] += c[r] * c[c_i];
                        }
                    }
                }
                let mut n = smallest_eigenvector_sym(cov);
                n = n * signum(avg_normal.dot(n));
                if let Some(bvh) = bvh {
                    if !bvh_is_empty(bvh) {
                        let ray1 = crate::math::Ray::new(centroid, n, 0.0, scale / 2.0);
                        let ray2 = crate::math::Ray::new(centroid, -n, 0.0, scale / 2.0);
                        let h1 = bvh.ray_intersect(ray1);
                        let h2 = bvh.ray_intersect(ray2);
                        let t1 = h1.map(|(_, t)| t).unwrap_or(f32::INFINITY);
                        let t2 = h2.map(|(_, t)| t).unwrap_or(f32::INFINITY);
                        if t1.min(t2) < scale * 0.5 {
                            centroid = if t1 < t2 { ray1.at(t1) } else { ray2.at(t2) };
                        }
                    }
                }
                o_prime[i] = centroid;
                n_prime[i] = if n.squared_norm() > 0.0 { n.normalized() } else { normals[i] };
            }
            positions = o_prime;
            normals = n_prime;
        }
        crate::say!("done.");
    }

    let mut face_normals = vec![Vec3::ZERO; faces.cols()];
    for i in 0..faces.cols() {
        let mut centroid = Vec3::ZERO;
        let mut avg_normal = Vec3::ZERO;
        for j in 0..faces.degree {
            let k = faces.get(j, i) as usize;
            if k >= positions.len() {
                continue;
            }
            centroid += positions[k];
            avg_normal += normals[k];
        }
        centroid = centroid / faces.degree as f32;
        let mut cov = [[0.0f32; 3]; 3];
        for j in 0..faces.degree {
            let k = faces.get(j, i) as usize;
            if k >= positions.len() {
                continue;
            }
            let d = positions[k] - centroid;
            let c = [d.x, d.y, d.z];
            for r in 0..3 {
                for c_i in 0..3 {
                    cov[r][c_i] += c[r] * c[c_i];
                }
            }
        }
        let mut n = smallest_eigenvector_sym(cov);
        n = n * signum(avg_normal.dot(n));
        face_normals[i] = n;
    }
    if posy == 4 && !pure_quad {
        for f in &irregular_faces {
            let mut centroid = Vec3::ZERO;
            let mut avg_normal = Vec3::ZERO;
            for &fi in f {
                let k = faces.get(0, fi as usize) as usize;
                centroid += positions[k];
                avg_normal += normals[k];
            }
            if f.is_empty() {
                continue;
            }
            centroid = centroid / f.len() as f32;
            let mut cov = [[0.0f32; 3]; 3];
            for &fi in f {
                let k = faces.get(0, fi as usize) as usize;
                let d = positions[k] - centroid;
                let c = [d.x, d.y, d.z];
                for r in 0..3 {
                    for c_i in 0..3 {
                        cov[r][c_i] += c[r] * c[c_i];
                    }
                }
            }
            let mut n = smallest_eigenvector_sym(cov);
            n = n * signum(avg_normal.dot(n));
            for &fi in f {
                face_normals[fi as usize] = n;
            }
        }
    }

    crate::say!("Step 12: Reordering mesh for efficient access ..");
    let mut v_vec = vec![positions, normals];
    let mut f_vec = vec![face_normals];
    reorder_mesh(&mut faces, &mut v_vec, &mut f_vec);
    crate::say!("done.");
    Extraction {
        positions: v_vec.remove(0),
        normals: v_vec.remove(0),
        face_normals: f_vec.remove(0),
        faces,
    }
}

fn bvh_is_empty(bvh: &Bvh) -> bool {
    // Triangles or points both count as a surface to project onto.
    let _ = bvh.disk_radius;
    false
}

fn fill_face(
    faces: &mut Faces,
    positions: &mut Vec<Vec3>,
    normals: &mut Vec<Vec3>,
    n_f: &mut usize,
    n_v: &mut usize,
    posy: i32,
    mut verts: Vec<(u32, u32)>,
) -> Option<Vec<u32>> {
    let mut irregular = Vec::new();
    while verts.len() > 2 {
        if verts.len() == posy as usize {
            let idx = *n_f;
            *n_f += 1;
            faces.ensure_cols(*n_f);
            for i in 0..posy as usize {
                faces.set(i, idx, verts[i].0);
            }
            break;
        } else if verts.len() > posy as usize + 1 || posy == 3 {
            let mut best_score = f32::INFINITY;
            let mut best_idx = 0usize;
            for i in 0..verts.len() {
                let mut score = 0.0;
                for k in 0..posy as usize {
                    let v0 = positions[verts[(i + k) % verts.len()].0 as usize];
                    let v1 = positions[verts[(i + k + 1) % verts.len()].0 as usize];
                    let v2 = positions[verts[(i + k + 2) % verts.len()].0 as usize];
                    let d0 = (v0 - v1).normalized();
                    let d1 = (v2 - v1).normalized();
                    let mut cosine = d0.dot(d1).clamp(-1.0, 1.0);
                    if !cosine.is_finite() {
                        cosine = 1.0;
                    }
                    let angle = cosine.acos() * 180.0 / std::f32::consts::PI;
                    score += (angle - if posy == 4 { 90.0 } else { 60.0 }).abs();
                }
                if score < best_score {
                    best_score = score;
                    best_idx = i;
                }
            }
            let idx = *n_f;
            *n_f += 1;
            faces.ensure_cols(*n_f);
            for i in 0..posy as usize {
                let slot = (best_idx + i) % verts.len();
                faces.set(i, idx, verts[slot].0);
                if i != 0 && i != posy as usize - 1 {
                    verts[slot].0 = INVALID;
                }
            }
            verts.retain(|v| v.0 != INVALID);
        } else {
            if posy != 4 {
                return None;
            }
            let mut centroid = Vec3::ZERO;
            let mut centroid_n = Vec3::ZERO;
            for v in &verts {
                centroid += positions[v.0 as usize];
                centroid_n += normals[v.0 as usize];
            }
            let idx_centroid = *n_v;
            *n_v += 1;
            if positions.len() < *n_v {
                positions.resize((*n_v).max(positions.len() * 2), Vec3::ZERO);
                normals.resize(positions.len(), Vec3::ZERO);
            }
            positions[idx_centroid] = centroid / verts.len() as f32;
            normals[idx_centroid] = centroid_n.normalized();
            for i in 0..verts.len() {
                let idx = *n_f;
                *n_f += 1;
                faces.ensure_cols(*n_f);
                faces.set(0, idx, verts[i].0);
                faces.set(1, idx, verts[(i + 1) % verts.len()].0);
                faces.set(2, idx, idx_centroid as u32);
                faces.set(3, idx, idx_centroid as u32);
                irregular.push(idx as u32);
            }
            break;
        }
    }
    Some(irregular)
}
