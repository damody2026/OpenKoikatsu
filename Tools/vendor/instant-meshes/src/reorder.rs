//! Breadth-first reordering of faces and vertices, matching `reorder.cpp`.

use std::collections::VecDeque;

use crate::dedge::build_dedge;
use crate::math::{Vec3, INVALID};
use crate::mesh::Faces;

pub fn reorder_mesh(faces: &mut Faces, vertices: &mut [Vec<Vec3>], face_attribs: &mut [Vec<Vec3>]) {
    let n_v = vertices[0].len();
    let dedge = match build_dedge(faces, n_v, true) {
        Ok(d) => d,
        Err(_) => return,
    };
    let mut pending = vec![true; faces.cols()];
    let mut remaining = faces.cols();
    let mut next_scan = 0usize;
    let mut queue = VecDeque::new();
    let mut v_map = vec![INVALID; n_v];
    let mut n_f = 0usize;
    let mut n_v_new = 0usize;
    let mut new_faces = Faces::new(faces.degree, faces.cols());
    let mut new_v: Vec<Vec<Vec3>> = vertices
        .iter()
        .map(|a| vec![Vec3::ZERO; a.len()])
        .collect();
    let mut new_f: Vec<Vec<Vec3>> = face_attribs
        .iter()
        .map(|a| vec![Vec3::ZERO; a.len()])
        .collect();

    while remaining > 0 {
        if queue.is_empty() {
            while next_scan < pending.len() && !pending[next_scan] {
                next_scan += 1;
            }
            pending[next_scan] = false;
            remaining -= 1;
            queue.push_back(next_scan as u32);
            next_scan += 1;
        }
        while let Some(f_old) = queue.pop_front() {
            let f_new = n_f;
            n_f += 1;
            for (j, attrib) in face_attribs.iter().enumerate() {
                new_f[j][f_new] = attrib[f_old as usize];
            }
            for i in 0..faces.degree {
                let v_old = faces.get(i, f_old as usize);
                let v_new = if v_map[v_old as usize] != INVALID {
                    v_map[v_old as usize]
                } else {
                    let id = n_v_new as u32;
                    n_v_new += 1;
                    v_map[v_old as usize] = id;
                    for (j, attrib) in vertices.iter().enumerate() {
                        new_v[j][id as usize] = attrib[v_old as usize];
                    }
                    id
                };
                new_faces.set(i, f_new, v_new);
                let edge_other = dedge.e2e[f_old as usize * faces.degree + i];
                if edge_other != INVALID {
                    let neighbor = edge_other / faces.degree as u32;
                    if pending[neighbor as usize] {
                        pending[neighbor as usize] = false;
                        remaining -= 1;
                        queue.push_back(neighbor);
                    }
                }
            }
        }
    }

    new_faces.resize_cols(n_f);
    *faces = new_faces;
    for (j, attrib) in vertices.iter_mut().enumerate() {
        new_v[j].truncate(n_v_new);
        attrib.clear();
        attrib.extend_from_slice(&new_v[j]);
    }
    for (j, attrib) in face_attribs.iter_mut().enumerate() {
        new_f[j].truncate(n_f);
        attrib.clear();
        attrib.extend_from_slice(&new_f[j]);
    }
}
