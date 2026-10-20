//! Bake a shared parent-first skeleton + rest-space verts (`v_ref`) for UE.
//!
//! Unity each SMR has its own bindPose; UE one `USkeleton` has one InvBind.
//! `v_ref = skin(v, restWorld, bind)` so GPU `posed * inv(rest) * v_ref`
//! matches `skin_mesh` (AssignedAnotherWeights keeps original binds).
//!
//! Head: `p_cf_head_bone.SetParent(cf_s_head, worldPositionStays:false)`
//! (`ChaControl.cs:8104`) → head-root local = I under `cf_s_head`.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use std::collections::{HashMap, HashSet};
use std::ffi::CString;

use crate::pose::{self, Mat4, Skeleton, Tf};
use crate::session::{OkSession, SessionPart};

const ID_POS: [f32; 3] = [0.0, 0.0, 0.0];
const ID_ROT: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
const ID_SCL: [f32; 3] = [1.0, 1.0, 1.0];

#[derive(Clone)]
pub struct ExportBone {
    pub name: String,
    pub name_c: CString,
    pub parent: i32,
    pub rest_pos: [f32; 3],
    pub rest_rot: [f32; 4],
    pub rest_scl: [f32; 3],
    pub pose_pos: [f32; 3],
    pub pose_rot: [f32; 4],
    pub pose_scl: [f32; 3],
    pub pose_locked: bool,
    pub layer: u8,
}

pub struct SkelExport {
    // Runtime reference-space morphs; never serialize these as mesh-local deltas.
    pub part_morphs: Vec<Vec<Vec<f32>>>,
    pub bones: Vec<ExportBone>,
    pub name_to_idx: HashMap<String, u16>,
    pub part_rest: Vec<Vec<[f32; 3]>>,
    pub part_ref_normals: Vec<Vec<[f32; 3]>>,
    pub part_ref_tangents: Vec<Vec<[f32; 4]>>,
    pub part_bone_idx: Vec<Vec<[u16; 4]>>,
}

/// Part-local candidates share the live global rig. Retained arrays are moved,
/// never rebaked, so publishing a slot preserves their allocations and state.
pub(crate) struct PartExports {
    pub morphs: Vec<Vec<Vec<f32>>>,
    pub rest: Vec<Vec<[f32; 3]>>,
    pub normals: Vec<Vec<[f32; 3]>>,
    pub tangents: Vec<Vec<[f32; 4]>>,
    pub bone_idx: Vec<Vec<[u16; 4]>>,
}
impl PartExports {
    pub(crate) fn valid_count(&self, count: usize) -> bool {
        self.morphs.len()==count && self.rest.len()==count && self.normals.len()==count
            && self.tangents.len()==count && self.bone_idx.len()==count
    }
}
impl SkelExport {
    /// Validate every array and the full monotonic retained mapping before
    /// touching live state. Rig bones and their animated transforms stay owned
    /// by this export; the candidate cannot introduce another global palette.
    pub(crate) fn replace_part_exports(&mut self, retained:&[Option<usize>], added:PartExports)
        ->Result<(),String> {
        self.validate_part_replacement(retained,&added)?;
        // Accessory-private palettes (skel_kind=4) are not global rig indices;
        // validation of those belongs to their owned AccessoryRuntime contract.
        fn merge<T>(old:&mut Vec<T>, retained:&[Option<usize>], added:Vec<T>) {
            let mut result=Vec::with_capacity(retained.iter().flatten().count()+added.len());
            for (value,index) in std::mem::take(old).into_iter().zip(retained) {
                if index.is_some(){result.push(value);}
            }
            result.extend(added);*old=result;
        }
        merge(&mut self.part_morphs,retained,added.morphs);
        merge(&mut self.part_rest,retained,added.rest);
        merge(&mut self.part_ref_normals,retained,added.normals);
        merge(&mut self.part_ref_tangents,retained,added.tangents);
        merge(&mut self.part_bone_idx,retained,added.bone_idx);
        Ok(())
    }
    /// Borrowing form of the publication checks, so an owned candidate can be
    /// rejected before any caller transfers ownership of its arrays.
    pub(crate) fn validate_part_replacement(&self, retained:&[Option<usize>], added:&PartExports)
        ->Result<(),String> {
        let count=retained.len();
        if self.part_morphs.len()!=count || self.part_rest.len()!=count
            || self.part_ref_normals.len()!=count || self.part_ref_tangents.len()!=count
            || self.part_bone_idx.len()!=count || !added.valid_count(added.rest.len()) {
            return Err("part export replacement array count mismatch".into());
        }
        let mut next=0;
        for index in retained.iter().flatten() {
            if *index!=next {return Err("part export replacement mapping is not contiguous".into());}
            next+=1;
        }
        Ok(())
    }
}

fn ident_trs() -> ([f32; 3], [f32; 4], [f32; 3]) {
    (ID_POS, ID_ROT, ID_SCL)
}

fn cname(s: &str) -> CString {
    CString::new(s).unwrap_or_default()
}

/// 1 = Animator `cf_j_*`, 2 = ShapeInfo `cf_s_*`/`cf_n_*`/`cf_d_*`/`cf_hit_*`.
pub fn bone_layer(name: &str) -> u8 {
    let n = name.to_ascii_lowercase();
    if n.starts_with("cf_j_") {
        1
    } else if n.starts_with("cf_s_")
        || n.starts_with("cf_n_")
        || n.starts_with("cf_d_")
        || n.starts_with("cf_hit_")
    {
        2
    } else {
        0
    }
}

fn key(s: &str) -> String {
    s.to_ascii_lowercase()
}

fn mat_vec(m: &Mat4, p: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
        m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
        m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3],
    ]
}

fn apply_mat(pos: &mut [[f32; 3]], m: &Mat4) {
    for p in pos {
        *p = mat_vec(m, *p);
    }
}

fn normalize_dir(p: [f32; 3]) -> [f32; 3] {
    let len = (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]).sqrt();
    if len > 1e-8 { [p[0] / len, p[1] / len, p[2] / len] } else { [0.0, 0.0, 1.0] }
}

fn mat_dir(m: &Mat4, p: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2],
        m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2],
        m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2],
    ]
}

// Apply the same rest-world * mesh-bind palette used for part_rest positions.
// Exporting the already posed normals alongside rest positions skins them twice
// in UE; exporting raw mesh-local normals omits this reference-space transform.
fn lbs_dirs(
    dirs: &[[f32; 3]], weights: &[[f32; 4]], bone_idx: &[[u16; 4]],
    worlds: &[Mat4], binds: &[Mat4],
) -> Vec<[f32; 3]> {
    let use_bind = worlds.len() == binds.len();
    dirs.iter().enumerate().map(|(i, &dir)| {
        let w = weights.get(i).copied().unwrap_or([1.0, 0.0, 0.0, 0.0]);
        let b = bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
        let mut acc = [0.0; 3];
        let mut sum = 0.0;
        for k in 0..4 {
            if w[k].abs() < 1e-6 || b[k] as usize >= worlds.len() { continue; }
            let bi = b[k] as usize;
            let palette = if use_bind { pose::mat_mul_pub(&worlds[bi], &binds[bi]) } else { worlds[bi] };
            let transformed = mat_dir(&palette, dir);
            for c in 0..3 { acc[c] += transformed[c] * w[k]; }
            sum += w[k];
        }
        normalize_dir(if sum > 1e-5 { [acc[0] / sum, acc[1] / sum, acc[2] / sum] } else { dir })
    }).collect()
}

fn bake_part_dirs(
    part: &SessionPart, dirs: &[[f32; 3]], body_rest: Option<&Skeleton>,
    head_rest: Option<&Skeleton>, hair_rest: Option<&Skeleton>,
) -> Vec<[f32; 3]> {
    if dirs.len() != part.rest_positions.len() { return Vec::new(); }
    let skel = match part.skel_kind {
        1 => body_rest,
        2 => head_rest,
        3 => hair_rest,
        _ => None,
    };
    let Some(skel) = skel else { return dirs.iter().copied().map(normalize_dir).collect(); };
    let (worlds, binds) = palettes(skel, part);
    let mut result = if worlds.is_empty() || part.weights.is_empty() {
        dirs.iter().copied().map(normalize_dir).collect()
    } else {
        lbs_dirs(dirs, &part.weights, &part.bone_idx, &worlds, &binds)
    };
    if part.skel_kind == 2 || part.skel_kind == 3 {
        let delta = crate::head_onto_body(body_rest, Some(skel));
        for dir in &mut result { *dir = normalize_dir(mat_dir(&delta, *dir)); }
    }
    result
}

fn lbs(
    verts: &[[f32; 3]],
    weights: &[[f32; 4]],
    bone_idx: &[[u16; 4]],
    worlds: &[Mat4],
    binds: &[Mat4],
) -> Vec<[f32; 3]> {
    let n = verts.len();
    let mut out = vec![[0.0f32; 3]; n];
    let nb = worlds.len();
    let use_bind = binds.len() == worlds.len();
    for i in 0..n {
        let w = weights.get(i).copied().unwrap_or([1.0, 0.0, 0.0, 0.0]);
        let b = bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
        let p = verts[i];
        let mut acc = [0.0f32; 3];
        let mut ws = 0.0f32;
        for k in 0..4 {
            let wi = w[k];
            if wi.abs() < 1e-6 {
                continue;
            }
            let bi = b[k] as usize;
            if bi >= nb {
                continue;
            }
            let pal = if use_bind {
                pose::mat_mul_pub(&worlds[bi], &binds[bi])
            } else {
                worlds[bi]
            };
            let t = mat_vec(&pal, p);
            acc[0] += t[0] * wi;
            acc[1] += t[1] * wi;
            acc[2] += t[2] * wi;
            ws += wi;
        }
        if ws > 1e-5 {
            out[i] = [acc[0] / ws, acc[1] / ws, acc[2] / ws];
        } else {
            out[i] = p;
        }
    }
    out
}

pub fn deform_from_ref(
    v_ref: &[[f32; 3]],
    weights: &[[f32; 4]],
    bone_idx: &[[u16; 4]],
    posed: &[Mat4],
    rest: &[Mat4],
) -> Vec<[f32; 3]> {
    let n = posed.len().min(rest.len());
    let pal: Vec<Mat4> = (0..n)
        .map(|i| pose::mat_mul_pub(&posed[i], &pose::mat_inv_pub(&rest[i])))
        .collect();
    let ident = vec![pose::ident(); n];
    lbs(v_ref, weights, bone_idx, &pal, &ident)
}

pub(crate) fn palettes(skel: &Skeleton, part: &SessionPart) -> (Vec<Mat4>, Vec<Mat4>) {
    let n = if !part.bind_poses.is_empty() {
        part.bind_poses.len().min(skel.worlds.len())
    } else {
        skel.worlds.len().min(skel.rest.len())
    };
    if n == 0 {
        return (Vec::new(), Vec::new());
    }
    let worlds = skel.worlds[..n].to_vec();
    let binds = if part.bind_poses.len() >= n {
        part.bind_poses[..n].to_vec()
    } else {
        pose::binds_from_rest(&skel.rest[..n])
    };
    (worlds, binds)
}

fn true_root(id: i64, tfs: &HashMap<i64, Tf>) -> i64 {
    let mut cur = id;
    for _ in 0..64 {
        let Some(tf) = tfs.get(&cur) else {
            break;
        };
        match tf.parent {
            Some(p) if p != 0 && p != cur && tfs.contains_key(&p) => cur = p,
            _ => break,
        }
    }
    cur
}

// Export indices are part of the immutable GPU input. HashMap traversal must
// not give an identical card a different palette on its next load. Preserve
// authored sibling order, with a stable name/id fallback for legacy snapshots.
fn ordered_subtree(root:i64,tfs:&HashMap<i64,Tf>)->Vec<i64> {
    let mut children=HashMap::<i64,Vec<i64>>::new();
    for(id,tf)in tfs {if let Some(parent)=tf.parent{children.entry(parent).or_default().push(*id);}}
    for ids in children.values_mut(){ids.sort_by_cached_key(|id|{let tf=&tfs[id];(tf.sibling_index.unwrap_or(u32::MAX),key(&tf.name),*id)});}
    let mut seen=HashSet::new();let mut out=Vec::new();let mut stack=vec![root];
    while let Some(id)=stack.pop(){if !seen.insert(id){continue;}out.push(id);if let Some(ids)=children.get(&id){stack.extend(ids.iter().rev().copied());}}
    out
}
fn parent_first_ids(skel: &Skeleton) -> Vec<i64> {
    let anim = skel.tfs.iter().filter(|(_,t)|matches!(t.name.as_str(),"cf_j_root"|"cf_J_N_FaceRoot"|"cf_J_FaceRoot"))
        .max_by_key(|(id,_)|(ordered_subtree(**id,&skel.tfs).len(),std::cmp::Reverse(**id))).map(|(id,_)|*id).or_else(|| {
        skel.tfs
            .iter()
            .filter(|(id, t)| match t.parent {
                Some(p) if p != 0 && p != **id && skel.tfs.contains_key(&p) => false,
                _ => true,
            })
            .max_by_key(|(id, _)| (ordered_subtree(**id, &skel.tfs).len(),std::cmp::Reverse(**id)))
            .map(|(id, _)| *id)
    });
    let Some(anim) = anim else {
        let mut ids: Vec<i64> = skel.tfs.keys().copied().collect();
        ids.sort_unstable();
        return ids;
    };
    let is_face = skel
        .tfs
        .get(&anim)
        .map(|t| t.name.to_ascii_lowercase().contains("face"))
        .unwrap_or(false);
    // Head: whole `p_cf_head_bone` prefab (true root + subtree).
    // Body: ancestors of `cf_j_root` plus its subtree — not the bundle root,
    // which also contains an unparented head copy (ChaControl looks up
    // `a_n_*` on the head prefab, not that copy).
    if is_face {
        let top = true_root(anim, &skel.tfs);
        return ordered_subtree(top, &skel.tfs);
    }
    let mut anc = Vec::new();
    let mut cur = skel.tfs.get(&anim).and_then(|t| t.parent);
    for _ in 0..64 {
        let Some(p) = cur else {
            break;
        };
        if p == 0 || p == anim || !skel.tfs.contains_key(&p) {
            break;
        }
        anc.push(p);
        cur = skel.tfs.get(&p).and_then(|t| t.parent);
    }
    anc.reverse();
    anc.extend(ordered_subtree(anim, &skel.tfs));
    anc
}

fn trs_of(tf: &Tf) -> ([f32; 3], [f32; 4], [f32; 3]) {
    (tf.pos, tf.rot, tf.scale)
}

fn lookup_tf<'a>(skel: &'a Skeleton, id: i64) -> Option<&'a Tf> {
    skel.tfs.get(&id)
}

fn map_parent(
    parent: Option<i64>,
    id_to_idx: &HashMap<i64, u16>,
    name_to_idx: &HashMap<String, u16>,
    tfs: &HashMap<i64, Tf>,
) -> i32 {
    let mut cur = parent;
    for _ in 0..64 {
        let Some(p) = cur else {
            return -1;
        };
        if p == 0 {
            return -1;
        }
        if let Some(&idx) = id_to_idx.get(&p) {
            return idx as i32;
        }
        if let Some(tf) = tfs.get(&p) {
            if let Some(&idx) = name_to_idx.get(&key(&tf.name)) {
                return idx as i32;
            }
        }
        cur = tfs.get(&p).and_then(|t| t.parent);
    }
    -1
}

fn push_bone(
    bones: &mut Vec<ExportBone>,
    name_to_idx: &mut HashMap<String, u16>,
    id_to_idx: &mut HashMap<i64, u16>,
    id: i64,
    name: String,
    parent: i32,
    rest: ([f32; 3], [f32; 4], [f32; 3]),
    pose: ([f32; 3], [f32; 4], [f32; 3]),
    pose_locked: bool,
) -> Option<u16> {
    let k = key(&name);
    if k.is_empty() {
        return None;
    }
    if name_to_idx.contains_key(&k) {
        if let Some(&idx) = name_to_idx.get(&k) {
            id_to_idx.insert(id, idx);
        }
        return None;
    }
    if parent >= 0 && (parent as usize) >= bones.len() {
        return None;
    }
    let idx = bones.len() as u16;
    name_to_idx.insert(k, idx);
    id_to_idx.insert(id, idx);
    let layer = bone_layer(&name);
    bones.push(ExportBone {
        name_c: cname(&name),
        name,
        parent,
        rest_pos: rest.0,
        rest_rot: rest.1,
        rest_scl: rest.2,
        pose_pos: pose.0,
        pose_rot: pose.1,
        pose_scl: pose.2,
        pose_locked,
        layer,
    });
    Some(idx)
}

fn add_tree(
    bones: &mut Vec<ExportBone>,
    name_to_idx: &mut HashMap<String, u16>,
    rest: &Skeleton,
    posed: &Skeleton,
    ids: &[i64],
    force_parent: Option<(i64, i32, bool)>,
) {
    let mut id_to_idx: HashMap<i64, u16> = HashMap::new();
    for id in ids {
        let Some(tf) = lookup_tf(rest, *id) else {
            continue;
        };
        let mut name = tf.name.clone();
        if name.is_empty() {
            name = format!("tf_{id}");
        }
        let (parent, use_ident) = match force_parent {
            Some((top, pidx, ident)) if *id == top => (pidx, ident),
            _ => (
                map_parent(tf.parent, &id_to_idx, name_to_idx, &rest.tfs),
                false,
            ),
        };
        let rest_trs = if use_ident {
            ident_trs()
        } else {
            trs_of(tf)
        };
        let pose_trs = if use_ident {
            ident_trs()
        } else {
            lookup_tf(posed, *id).map(trs_of).unwrap_or(rest_trs)
        };
        let _ = push_bone(
            bones,
            name_to_idx,
            &mut id_to_idx,
            *id,
            name,
            parent,
            rest_trs,
            pose_trs,
            use_ident,
        );
    }
}

/// Bind-time body tree: T-pose, no shape / clip / ABM.
/// Shape lives in posed locals (`body_skel`), matching Unity
/// `boneWorld * bindPose * v` with bindPose from the unshaped prefab.
pub fn bind_body(sess: &OkSession) -> Option<Skeleton> {
    let mut sk = sess.body_rest.clone()?;
    pose::assigned_weights_remap(&mut sk);
    sk.include_tree_bones();
    Some(sk)
}

pub fn bind_head(sess: &OkSession) -> Option<Skeleton> {
    let mut sk = sess.head_rest.clone()?;
    pose::assigned_weights_remap(&mut sk);
    sk.include_tree_bones();
    Some(sk)
}

fn remap_indices(old: &[[u16; 4]], names: &[String], name_to_idx: &HashMap<String, u16>) -> Vec<[u16; 4]> {
    old.iter()
        .map(|q| {
            let mut o = [0u16; 4];
            for k in 0..4 {
                o[k] = names
                    .get(q[k] as usize)
                    .and_then(|n| name_to_idx.get(&key(n)))
                    .copied()
                    .unwrap_or(0);
            }
            o
        })
        .collect()
}

fn bake_part(
    part: &SessionPart,
    body_rest: Option<&Skeleton>,
    body_posed: Option<&Skeleton>,
    head_rest: Option<&Skeleton>,
    head_posed: Option<&Skeleton>,
    hair_rest: Option<&Skeleton>,
    name_to_idx: &HashMap<String, u16>,
) -> (Vec<[f32; 3]>, Vec<[u16; 4]>) {
    match part.skel_kind {
        3 => {
            let Some(hrest) = hair_rest else {
                return (part.rest_positions.clone(), part.bone_idx.clone());
            };
            let (rw, binds) = palettes(hrest, part);
            if rw.is_empty() || part.weights.is_empty() {
                return (
                    part.rest_positions.clone(),
                    remap_indices(&part.bone_idx, &hrest.names, name_to_idx),
                );
            }
            let mut v_ref = lbs(&part.rest_positions, &part.weights, &part.bone_idx, &rw, &binds);
            let delta = crate::head_onto_body(body_rest, hair_rest);
            apply_mat(&mut v_ref, &delta);
            let idx = remap_indices(&part.bone_idx, &hrest.names, name_to_idx);
            (v_ref, idx)
        }
        1 => {
            let Some(rest) = body_rest else {
                return (part.rest_positions.clone(), part.bone_idx.clone());
            };
            let (rw, binds) = palettes(rest, part);
            if rw.is_empty() || part.weights.is_empty() {
                return (part.rest_positions.clone(), remap_indices(&part.bone_idx, &rest.names, name_to_idx));
            }
            let v_ref = lbs(&part.rest_positions, &part.weights, &part.bone_idx, &rw, &binds);
            let idx = remap_indices(&part.bone_idx, &rest.names, name_to_idx);
            let _ = body_posed;
            (v_ref, idx)
        }
        2 => {
            let Some(hrest) = head_rest else {
                return (part.rest_positions.clone(), part.bone_idx.clone());
            };
            let (rw, binds) = palettes(hrest, part);
            if rw.is_empty() || part.weights.is_empty() {
                return (part.rest_positions.clone(), remap_indices(&part.bone_idx, &hrest.names, name_to_idx));
            }
            let mut v_ref = lbs(&part.rest_positions, &part.weights, &part.bone_idx, &rw, &binds);
            let delta = crate::head_onto_body(body_rest, head_rest);
            apply_mat(&mut v_ref, &delta);
            let idx = remap_indices(&part.bone_idx, &hrest.names, name_to_idx);
            let _ = head_posed;
            (v_ref, idx)
        }
        _ => (part.rest_positions.clone(), part.bone_idx.clone()),
    }
}

pub fn build(sess: &OkSession) -> SkelExport {
    let body_rest = bind_body(sess);
    let head_rest = bind_head(sess);
    let body_posed = sess.body_skel.as_ref();
    let head_posed = sess.head_skel.as_ref();

    let mut bones = Vec::new();
    let mut name_to_idx: HashMap<String, u16> = HashMap::new();

    if let (Some(rest), Some(posed)) = (body_rest.as_ref(), body_posed) {
        let ids = parent_first_ids(rest);
        add_tree(&mut bones, &mut name_to_idx, rest, posed, &ids, None);
    }

    if let (Some(rest), Some(posed)) = (head_rest.as_ref(), head_posed) {
        let ids = parent_first_ids(rest);
        let top = ids.first().copied();
        let head_parent = name_to_idx.get("cf_s_head").copied().map(|i| i as i32).unwrap_or(-1);
        if let Some(top) = top {
            add_tree(
                &mut bones,
                &mut name_to_idx,
                rest,
                posed,
                &ids,
                Some((top, head_parent, true)),
            );
        }
    }

    let before_hair = bones.len();
    if let (Some(rest), Some(posed)) = (sess.head_full_rest.as_ref(), sess.head_full_posed.as_ref()) {
        merge_hair_bones(&mut bones, &mut name_to_idx, rest, posed);
    }
    eprintln!(
        "ok-extract: gpu skel bones={} hair_added={}",
        bones.len(),
        bones.len() - before_hair
    );

    let parts=bake_part_exports(sess,&sess.parts,&name_to_idx,body_rest.as_ref(),head_rest.as_ref());
    SkelExport {
        part_morphs:parts.morphs,bones,name_to_idx,part_rest:parts.rest,
        part_ref_normals:parts.normals,part_ref_tangents:parts.tangents,part_bone_idx:parts.bone_idx,
    }
}

/// Bake only the new renderers against the unchanged live global palette.
/// Full loads and incremental candidates use precisely the same math.
pub(crate) fn prepare_part_exports(sess:&OkSession,parts:&[SessionPart],
    name_to_idx:&HashMap<String,u16>)->PartExports {
    let body_rest=bind_body(sess);
    let head_rest=bind_head(sess);
    bake_part_exports(sess,parts,name_to_idx,body_rest.as_ref(),head_rest.as_ref())
}
fn bake_part_exports(sess:&OkSession,parts:&[SessionPart],name_to_idx:&HashMap<String,u16>,
    body_rest:Option<&Skeleton>,head_rest:Option<&Skeleton>)->PartExports {
    let body_posed=sess.body_skel.as_ref();
    let head_posed=sess.head_skel.as_ref();
    let mut part_rest = Vec::with_capacity(parts.len());
    let mut part_ref_normals = Vec::with_capacity(parts.len());
    let mut part_ref_tangents = Vec::with_capacity(parts.len());
    let mut part_bone_idx = Vec::with_capacity(parts.len());
    for part in parts {
        let (vr, idx) = bake_part(
            part,
            body_rest,
            body_posed,
            head_rest,
            head_posed,
            sess.head_full_rest.as_ref(),
            name_to_idx,
        );
        let normal_refs = bake_part_dirs(
            part, &part.rest_normals, body_rest, head_rest,
            sess.head_full_rest.as_ref(),
        );
        let tangent_refs = if part.rest_tangents.len() == part.rest_positions.len() {
            let tangent_xyz: Vec<[f32; 3]> = part.rest_tangents.iter()
                .map(|t| [t[0], t[1], t[2]]).collect();
            bake_part_dirs(
                part, &tangent_xyz, body_rest, head_rest,
                sess.head_full_rest.as_ref(),
            ).into_iter().zip(part.rest_tangents.iter())
                .map(|(t, source)| [t[0], t[1], t[2], source[3]])
                .collect()
        } else { Vec::new() };
        part_rest.push(vr);
        part_ref_normals.push(normal_refs);
        part_ref_tangents.push(tangent_refs);
        part_bone_idx.push(idx);
    }

    PartExports {
        morphs: parts.iter().map(|p| morphs_to_ref(sess, p)).collect(),
        rest:part_rest,normals:part_ref_normals,tangents:part_ref_tangents,bone_idx:part_bone_idx,
    }
}

fn merge_hair_bones(
    bones: &mut Vec<ExportBone>,
    name_to_idx: &mut HashMap<String, u16>,
    rest: &Skeleton,
    posed: &Skeleton,
) {
    let head_parent = name_to_idx
        .get("cf_s_head")
        .copied()
        .map(|i| i as i32)
        .unwrap_or(-1);
    let ids = parent_first_ids(rest);
    let mut id_to_idx: HashMap<i64, u16> = HashMap::new();
    for id in ids {
        let Some(tf) = lookup_tf(rest, id) else {
            continue;
        };
        let name = if tf.name.is_empty() {
            format!("tf_{id}")
        } else {
            tf.name.clone()
        };
        let mut parent = map_parent(tf.parent, &id_to_idx, name_to_idx, &rest.tfs);
        if parent < 0 && !name_to_idx.contains_key(&key(&name)) {
            parent = head_parent;
        }
        let rest_trs = trs_of(tf);
        let pose_trs = lookup_tf(posed, id).map(trs_of).unwrap_or(rest_trs);
        let _ = push_bone(
            bones,
            name_to_idx,
            &mut id_to_idx,
            id,
            name,
            parent,
            rest_trs,
            pose_trs,
            false,
        );
    }
}

fn ref_point(
    p: [f32; 3],
    w: [f32; 4],
    b: [u16; 4],
    worlds: &[Mat4],
    binds: &[Mat4],
    extra: Option<&Mat4>,
) -> [f32; 3] {
    let mut out = lbs(&[p], &[w], &[b], worlds, binds);
    if let Some(m) = extra {
        apply_mat(&mut out, m);
    }
    out[0]
}

/// Blendshape deltas are mesh-local. GPU skin adds them to `v_ref`, so each
/// delta is transformed by the same linear map as `bake_part` (translation cancels).
/// CPU body morphs are applied after skinning. Transform original mesh-local
/// deltas by the live palette; otherwise rotating the pelvis moves the mesh
/// while its morph displacement remains in the old coordinate frame.
pub(crate) fn body_morph_to_pose(sess:&OkSession,part:&SessionPart,morph:usize)->Option<Vec<f32>> {
    let skel=sess.body_skel.as_ref()?;
    let (worlds,binds)=palettes(skel,part);
    if worlds.is_empty(){return None;}
    let deltas=&part.blend_shapes.get(morph)?.1;
    let mut result=Vec::with_capacity(deltas.len()*3);
    for (vertex,delta) in deltas {
        let i=*vertex as usize;let base=*part.rest_positions.get(i)?;
        let weights=*part.weights.get(i)?;let bones=*part.bone_idx.get(i)?;
        let a=ref_point(base,weights,bones,&worlds,&binds,None);
        let b=ref_point([base[0]+delta[0],base[1]+delta[1],base[2]+delta[2]],weights,bones,&worlds,&binds,None);
        result.extend((0..3).map(|c|b[c]-a[c]));
    }
    Some(result)
}
pub fn morphs_to_ref(sess: &OkSession, part: &SessionPart) -> Vec<Vec<f32>> {
    let body = bind_body(sess);
    let head = bind_head(sess);
    let (skel, extra) = match part.skel_kind {
        1 => (body.as_ref(), None),
        2 => {
            let m = crate::head_onto_body(body.as_ref(), head.as_ref());
            (head.as_ref(), Some(m))
        }
        3 => {
            let m = crate::head_onto_body(body.as_ref(), sess.head_full_rest.as_ref());
            (sess.head_full_rest.as_ref(), Some(m))
        }
        _ => return part.morph_dxyz.clone(),
    };
    let Some(skel) = skel else {
        return part.morph_dxyz.clone();
    };
    let (rw, binds) = palettes(skel, part);
    if rw.is_empty() {
        return part.morph_dxyz.clone();
    }
    let canonical: Vec<Vec<f32>> = part.blend_shapes.iter().map(|(_, d)| d.iter().flat_map(|(_, xyz)| *xyz).collect()).collect();
    canonical
        .iter()
        .enumerate()
        .map(|(mi, dxyz)| {
            let Some(indices) = part.morph_idx.get(mi) else {
                return dxyz.clone();
            };
            let mut out = dxyz.clone();
            for (k, &vi32) in indices.iter().enumerate() {
                let vi = vi32 as usize;
                let base = match part.rest_positions.get(vi) {
                    Some(p) => *p,
                    None => continue,
                };
                let o = k * 3;
                if o + 2 >= dxyz.len() {
                    continue;
                }
                let d = [dxyz[o], dxyz[o + 1], dxyz[o + 2]];
                let w = part.weights.get(vi).copied().unwrap_or([1.0, 0.0, 0.0, 0.0]);
                let b = part.bone_idx.get(vi).copied().unwrap_or([0, 0, 0, 0]);
                let a = ref_point(base, w, b, &rw, &binds, extra.as_ref());
                let c = ref_point(
                    [base[0] + d[0], base[1] + d[1], base[2] + d[2]],
                    w,
                    b,
                    &rw,
                    &binds,
                    extra.as_ref(),
                );
                out[o] = c[0] - a[0];
                out[o + 1] = c[1] - a[1];
                out[o + 2] = c[2] - a[2];
            }
            out
        })
        .collect()
}

fn tf_by_name<'a>(skel: &'a Skeleton, name: &str) -> Option<&'a Tf> {
    skel.tfs.values().find(|t| t.name.eq_ignore_ascii_case(name))
}

// Build per refresh: no stale TRS, IDs, or cross-character bindings. Iterating
// each source in the old order retains its first duplicate and source priority.
#[cfg(test)]
fn pose_name_index<'a>(sources: &[Option<&'a Skeleton>]) -> HashMap<String, &'a Tf> {
    let mut index = HashMap::new();
    for skel in sources.iter().flatten() {
        for tf in skel.tfs.values() {
            index.entry(key(&tf.name)).or_insert(tf);
        }
    }
    index
}

// Bindings contain inventory slots, never references into mutable skeletons.
// Duplicate names retain the first transform and original source priority.
struct PoseExportPlan {
    sources:Vec<Option<Vec<(i64,String)>>>,
    bones:Vec<(String,bool)>,
    writes:Vec<Vec<Vec<usize>>>,
}
impl PoseExportPlan {
    fn matches(&self,sources:&[Option<&Skeleton>],bones:&[ExportBone])->bool {
        self.bones.len()==bones.len()&&self.bones.iter().zip(bones).all(|((name,locked),bone)|name==&bone.name&&*locked==bone.pose_locked)
        &&self.sources.len()==sources.len()&&self.sources.iter().zip(sources).all(|(expected,actual)|match(expected,actual){
            (None,None)=>true,(Some(entries),Some(skeleton))=>entries.len()==skeleton.tfs.len()&&entries.iter().zip(&skeleton.tfs).all(|((id,name),(actual,tf))|id==actual&&name==&tf.name),_=>false})
    }
    fn compile(sources:&[Option<&Skeleton>],bones:&[ExportBone])->Self {
        let mut first=HashMap::new();
        let mut writes=Vec::with_capacity(sources.len());
        for (source,skeleton) in sources.iter().enumerate(){
            let mut slots=Vec::new();
            if let Some(skeleton)=skeleton {for (slot,tf) in skeleton.tfs.values().enumerate(){first.entry(key(&tf.name)).or_insert((source,slot));slots.push(Vec::new());}}
            writes.push(slots);
        }
        for (output,bone)in bones.iter().enumerate(){if !bone.pose_locked {if let Some(&(source,slot))=first.get(&key(&bone.name)){writes[source][slot].push(output);}}}
        Self {sources:sources.iter().map(|source|source.map(|skeleton|skeleton.tfs.iter().map(|(id,tf)|(*id,tf.name.clone())).collect())).collect(),
            bones:bones.iter().map(|bone|(bone.name.clone(),bone.pose_locked)).collect(),writes}
    }
    fn apply(&self,sources:&[Option<&Skeleton>],bones:&mut [ExportBone]) {
        for bone in bones.iter_mut().filter(|bone|bone.pose_locked){bone.pose_pos=ID_POS;bone.pose_rot=ID_ROT;bone.pose_scl=ID_SCL;}
        for (source,slots)in sources.iter().zip(&self.writes){if let Some(skeleton)=source {
            for (tf,outputs)in skeleton.tfs.values().zip(slots){for &output in outputs {let bone=&mut bones[output];bone.pose_pos=tf.pos;bone.pose_rot=tf.rot;bone.pose_scl=tf.scale;}}
        }}
    }
}
thread_local!{static POSE_EXPORT_PLANS:std::cell::RefCell<Vec<PoseExportPlan>>=const{std::cell::RefCell::new(Vec::new())};}

fn refresh_pose_sources(sources:&[Option<&Skeleton>],bones:&mut [ExportBone]) {
    POSE_EXPORT_PLANS.with(|plans|{
        let mut plans=plans.borrow_mut();
        let plan=if let Some(index)=plans.iter().position(|plan|plan.matches(sources,bones)){plans.remove(index)}else{PoseExportPlan::compile(sources,bones)};
        plan.apply(sources,bones);
        if bones.len()<=4096&&sources.iter().flatten().all(|source|source.tfs.len()<=4096){if plans.len()>=16{plans.remove(0);}plans.push(plan);}
    });
}

/// Update posed locals after shape/clip/ABM. Rest and `v_ref` stay put.
pub fn refresh_pose(sess: &OkSession, ex: &mut SkelExport) {
    let body = sess.body_skel.as_ref();
    let head = sess.head_skel.as_ref();
    let sources = [body, head, sess.head_full_posed.as_ref()];
    let parity = std::env::var("OK_ANIM_SKIN_PARITY").ok().as_deref() == Some("1");
    let expected=if parity{Some(ex.bones.iter().map(|bone|{
        if bone.pose_locked {ident_trs()}else{sources.iter().flatten().find_map(|s|tf_by_name(s,&bone.name)).map_or((bone.pose_pos,bone.pose_rot,bone.pose_scl),|tf|(tf.pos,tf.rot,tf.scale))}
    }).collect::<Vec<_>>())}else{None};
    refresh_pose_sources(&sources,&mut ex.bones);
    let mut valid = true;
    if let Some(expected)=expected {for (bone,value)in ex.bones.iter().zip(expected){valid&=(bone.pose_pos,bone.pose_rot,bone.pose_scl)==value;}}
    if parity { eprintln!("OK animation export parity valid={} bones={}", i32::from(valid), ex.bones.len()); }
}

pub fn max_err(a: &[[f32; 3]], b: &[[f32; 3]]) -> f32 {
    let n = a.len().min(b.len());
    let mut m = 0.0f32;
    for i in 0..n {
        let dx = a[i][0] - b[i][0];
        let dy = a[i][1] - b[i][1];
        let dz = a[i][2] - b[i][2];
        m = m.max((dx * dx + dy * dy + dz * dz).sqrt());
    }
    m
}

pub fn reconstruct_part(
    part: &SessionPart,
    rest: &Skeleton,
    posed: &Skeleton,
) -> Option<(Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<[f32; 3]>)> {
    if part.weights.is_empty() || part.rest_positions.is_empty() {
        return None;
    }
    let (rw, rb) = palettes(rest, part);
    let (pw, pb) = palettes(posed, part);
    if rw.is_empty() || pw.is_empty() {
        return None;
    }
    let v_ref = lbs(&part.rest_positions, &part.weights, &part.bone_idx, &rw, &rb);
    let v_direct = lbs(&part.rest_positions, &part.weights, &part.bone_idx, &pw, &pb);
    let v_recon = deform_from_ref(&v_ref, &part.weights, &part.bone_idx, &pw, &rw);
    Some((v_ref, v_direct, v_recon))
}

#[allow(dead_code)]
fn _unique_check(bones: &[ExportBone]) -> (bool, Vec<String>) {
    let mut seen = HashSet::new();
    let mut dups = Vec::new();
    for b in bones {
        let k = key(&b.name);
        if !seen.insert(k.clone()) {
            dups.push(b.name.clone());
        }
    }
    (dups.is_empty(), dups)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn slot_export_replacement_validates_before_mutation_and_preserves_retained_buffers() {
        fn candidate(values:&[f32])->PartExports {
            PartExports {morphs:values.iter().map(|v|vec![vec![*v]]).collect(),
                rest:values.iter().map(|v|vec![[*v;3]]).collect(),
                normals:values.iter().map(|v|vec![[*v;3]]).collect(),
                tangents:values.iter().map(|v|vec![[*v;4]]).collect(),
                bone_idx:values.iter().map(|v|vec![[*v as u16;4]]).collect()}
        }
        let initial=candidate(&[1.,2.,3.]);
        let mut export=SkelExport {bones:vec![],name_to_idx:[("live-rig".into(),7)].into_iter().collect(),
            part_morphs:initial.morphs,part_rest:initial.rest,part_ref_normals:initial.normals,
            part_ref_tangents:initial.tangents,part_bone_idx:initial.bone_idx};
        let first=export.part_rest[0].as_ptr();let last=export.part_rest[2].as_ptr();
        assert!(export.replace_part_exports(&[Some(0),None,Some(2)],candidate(&[4.])).is_err());
        assert_eq!(export.part_rest.iter().map(|v|v[0][0]).collect::<Vec<_>>(),vec![1.,2.,3.]);
        let mut invalid=candidate(&[4.]);invalid.morphs.clear();
        assert!(export.replace_part_exports(&[Some(0),None,Some(1)],invalid).is_err());
        assert_eq!(export.part_rest[0].as_ptr(),first);
        export.replace_part_exports(&[Some(0),None,Some(1)],candidate(&[4.,5.])).unwrap();
        assert_eq!(export.part_rest.iter().map(|v|v[0][0]).collect::<Vec<_>>(),vec![1.,3.,4.,5.]);
        assert_eq!(export.part_rest[0].as_ptr(),first);assert_eq!(export.part_rest[1].as_ptr(),last);
        assert_eq!(export.part_morphs[1][0][0],3.);assert_eq!(export.part_bone_idx[2][0],[4;4]);
        assert_eq!(export.part_ref_normals[3][0],[5.;3]);assert_eq!(export.part_ref_tangents[3][0],[5.;4]);
        assert_eq!(export.name_to_idx.get("live-rig"),Some(&7));
        export.replace_part_exports(&[None,None,None,None],candidate(&[])).unwrap();
        assert!(export.part_rest.is_empty());assert!(export.part_morphs.is_empty());
        assert_eq!(export.name_to_idx.get("live-rig"),Some(&7));
    }

    #[test]
    fn export_tree_order_is_stable_and_preserves_authored_siblings() {
        let tf=|name:&str,parent,sibling_index|Tf{name:name.into(),parent,sibling_index,pos:ID_POS,rot:ID_ROT,scale:ID_SCL};
        let rows=vec![(1,tf("cf_j_root",None,None)),(2,tf("z_authored_first",Some(1),Some(0))),
            (3,tf("a_authored_second",Some(1),Some(1))),(4,tf("legacy_b",Some(1),None)),
            (5,tf("legacy_a",Some(1),None)),(6,tf("grandchild",Some(2),None))];
        for rotation in 0..rows.len(){let mut rotated=rows.clone();rotated.rotate_left(rotation);
            let skel=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:0,tfs:rotated.into_iter().collect(),bone_ids:vec![],source_mesh:0};
            assert_eq!(parent_first_ids(&skel),vec![1,2,6,3,5,4]);
        }
    }

    #[test]
    #[ignore="installed portable card export/cache determinism; owns process environment"]
    fn real_card_export_order_survives_independent_hashmaps_and_cache() {
        std::env::set_var("OK_BUILTIN_RESOURCES","C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        let path=PathBuf::from("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");
        let source=crate::portable::load_outfit(&path,0).unwrap();let bytes=source.to_cache_bytes().unwrap();let expected=build(&source);
        let identity=|e:&SkelExport|e.bones.iter().map(|b|(b.name.clone(),b.parent,b.rest_pos,b.rest_rot,b.rest_scl,b.pose_pos,b.pose_rot,b.pose_scl,b.pose_locked)).collect::<Vec<_>>();
        for _ in 0..4 {let restored=OkSession::from_cache_bytes(&bytes).unwrap();let actual=build(&restored);
            assert!(identity(&expected)==identity(&actual),"bone contract changed with HashMap insertion order");
            assert!(expected.part_bone_idx==actual.part_bone_idx,"skin palette indices changed");
            assert!(expected.part_rest==actual.part_rest,"reference geometry changed");
            assert!(expected.part_ref_normals==actual.part_ref_normals,"reference normals changed");
            assert!(expected.part_ref_tangents==actual.part_ref_tangents,"reference tangents changed");
            assert!(expected.part_morphs==actual.part_morphs,"reference morphs changed");
        }
    }

    #[test]
    #[ignore="installed APNG morph roundtrip; owns process environment"]
    fn reader_gpu_morph_roundtrip_is_immutable() {
        std::env::set_var("OK_BUILTIN_RESOURCES","C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
        std::env::set_var("OK_GPU_SKIN","1");
        let path=PathBuf::from("C:/portable/OpenKoikatsu/output/CrowdAPNG/C002.png");
        let mut session=crate::portable::load_outfit(&path,0).unwrap();
        let before=session.parts.iter().map(|p|p.morph_dxyz.clone()).collect::<Vec<_>>();
        let first=session.ensure_skel_export().part_morphs.clone();
        assert_eq!(before,session.parts.iter().map(|p|p.morph_dxyz.clone()).collect::<Vec<_>>());
        session.skel_export=None;
        assert_eq!(first,session.ensure_skel_export().part_morphs);
        let bytes=session.to_cache_bytes().unwrap();
        let mut restored=OkSession::from_cache_bytes(&bytes).unwrap();
        assert_eq!(first,restored.ensure_skel_export().part_morphs);
        // Simulate a legacy card containing already converted GPU channels.
        for (p,m) in restored.parts.iter_mut().zip(&first) { p.morph_dxyz=m.clone(); }
        restored.skel_export=None;
        assert_eq!(first,restored.ensure_skel_export().part_morphs);
    }

    #[test]
    #[ignore="installed portable GPU palette oracle; owns process environment"]
    fn reader_export_palette_matches_cpu() {
        std::env::set_var("OK_BUILTIN_RESOURCES","C:/portable/OpenKoikatsu/Content/IndependentResources/v1");
        for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
        std::env::set_var("OK_CLOTHES_STATES","0,0,0,0,0,0,0,0,0");let mut rows=Vec::new();
        for (card,outfit) in [(0,0),(1,0),(2,0),(2,4),(50,3)] {
            let path=PathBuf::from(format!("C:/portable/OpenKoikatsu/output/CrowdAPNG/C{card:03}.png"));
            let mut session=crate::portable::load_outfit(&path,outfit).unwrap();session.ensure_skel_export();
            let body=session.shape_body.clone();let face=session.shape_face.clone();session.apply_shape(&body,&face);
            let ex=build(&session);let mut rw=Vec::new();let mut pw=Vec::new();
            for b in &ex.bones {let r=pose::trs(b.rest_pos,b.rest_rot,b.rest_scl);let p=pose::trs(b.pose_pos,b.pose_rot,b.pose_scl);rw.push(if b.parent<0{r}else{pose::mat_mul_pub(&rw[b.parent as usize],&r)});pw.push(if b.parent<0{p}else{pose::mat_mul_pub(&pw[b.parent as usize],&p)});}
            std::env::set_var("OK_SKELMESH","0");session.skin_all();std::env::set_var("OK_SKELMESH","1");
            for (i,p) in session.parts.iter().enumerate().filter(|(_,p)|matches!(p.skel_kind,1..=3)) {
                let gpu=deform_from_ref(&ex.part_rest[i],&p.weights,&ex.part_bone_idx[i],&pw,&rw);
                let error=max_err(&gpu,&p.positions);rows.push(serde_json::json!({"card":card,"outfit":outfit,"part":p.name,"kind":p.skel_kind,"max_error_m":error}));
            }
        }
        std::fs::write("C:/portable/OpenKoikatsu/Saved/Tests/Reader292ExportPalette.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
    }

    #[test]
    fn pose_index_preserves_source_priority_duplicates_and_live_values() {
        fn skeleton(names: &[&str]) -> Skeleton {
            Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(), smr_count: 0,
                bone_ids: Vec::new(), source_mesh: 0, tfs: names.iter().enumerate().map(|(i,n)|
                    (i as i64,Tf { name: (*n).into(), parent: None, sibling_index: None,
                        pos: [i as f32, 1.0, 2.0], rot: ID_ROT, scale: ID_SCL })).collect() }
        }
        let mut body = skeleton(&["Shared", "DUP", "dup", "Ä", "ä"]);
        let head = skeleton(&["SHARED", "HeadOnly"]);
        let full = skeleton(&["shared", "headonly", "FullOnly"]);
        for pass in 0..2 {
            let sources = [Some(&body), Some(&head), Some(&full)];
            let index = pose_name_index(&sources);
            for name in ["shared", "DuP", "HEADONLY", "fullonly", "missing", "Ä", "ä"] {
                let actual = index.get(&key(name)).copied();
                let expected = sources.iter().flatten().find_map(|s| tf_by_name(s,name));
                assert_eq!(actual.map(|t| t as *const Tf), expected.map(|t| t as *const Tf));
            }
            if pass == 1 { assert_eq!(index["shared"].pos, [9.0, 8.0, 7.0]); }
            drop(index);
            body.tfs.get_mut(&0).unwrap().pos = [9.0, 8.0, 7.0];
        }
        assert!(pose_name_index(&[None,None,None]).is_empty());
        let mut bones:Vec<_>=["shared","DuP","HEADONLY","fullonly","missing","Ä","ä"].iter().map(|name|ExportBone{
            name:(*name).into(),name_c:cname(name),parent:-1,rest_pos:ID_POS,rest_rot:ID_ROT,rest_scl:ID_SCL,
            pose_pos:[17.,18.,19.],pose_rot:ID_ROT,pose_scl:ID_SCL,pose_locked:false,layer:1}).collect();
        for pass in 0..8 {
            let sources=[if pass==5{None}else{Some(&body)},Some(&head),Some(&full)];
            let expected:Vec<_>=bones.iter().map(|bone|if bone.pose_locked{ident_trs()}else{
                sources.iter().flatten().find_map(|source|tf_by_name(source,&bone.name))
                    .map_or((bone.pose_pos,bone.pose_rot,bone.pose_scl),|tf|(tf.pos,tf.rot,tf.scale))}).collect();
            refresh_pose_sources(&sources,&mut bones);
            for (bone,value)in bones.iter().zip(expected){assert_eq!((bone.pose_pos,bone.pose_rot,bone.pose_scl),value,"pass={pass}");}
            match pass {
                0=>body.tfs.reserve(2048),
                1=>{body.tfs.get_mut(&0).unwrap().name="renamed".into();},
                2=>{body.tfs.insert(900,Tf{name:"shared".into(),parent:None,sibling_index:None,pos:[-7.,2.,3.],rot:ID_ROT,scale:ID_SCL});},
                3=>bones[0].pose_locked=true,
                4=>bones.swap(0,2),
                5=>{body.tfs.remove(&900);bones[2].pose_locked=false;},
                6=>bones[3].name="renamed".into(),_=>{}
            }
        }
    }

    #[test]
    #[ignore]
    fn inspect_sitting_head_chain() {
        let game = crate::test_support::game_path("");
        let card = game.join("UserData/chara/female/KK_384283.png");
        let mut sess = crate::session::open(&game, Some(&card), 0).unwrap();
        sess.anim_clip = "GSit_00".to_string();
        sess.apply_anim_time(0.2);
        let ex = build(&sess);
        for name in ["cf_j_root", "cf_j_hips", "cf_j_spine01", "cf_j_head", "cf_s_head", "p_cf_head_bone", "cf_j_n_faceroot", "cf_J_FaceRoot", "cf_n_height"] {
            if let Some(&idx) = ex.name_to_idx.get(&name.to_ascii_lowercase()) {
                let b = &ex.bones[idx as usize];
                let par = if b.parent >= 0 { ex.bones[b.parent as usize].name.as_str() } else { "ROOT" };
                eprintln!("SIT {name} parent={par} rest={:?} pose={:?} locked={}",b.rest_pos,b.pose_pos,b.pose_locked);
            }
        }
        for target in ["cf_j_n_faceroot", "cf_s_head"] {
            let mut idx = ex.name_to_idx.get(target).copied().unwrap() as i32;
            let mut chain = Vec::new();
            while idx >= 0 && chain.len() < 20 {
                let b = &ex.bones[idx as usize];
                chain.push(b.name.clone());
                idx = b.parent;
            }
            eprintln!("CHAIN {target}: {chain:?}");
        }
        for target in ["cf_j_hips", "cf_j_head", "cf_s_head"] {
            let sk = sess.body_skel.as_ref().unwrap();
            if let Some((&id, _)) = sk.tfs.iter().find(|(_, tf)| tf.name.eq_ignore_ascii_case(target)) {
                let world = crate::pose::world_of_id(id, &sk.tfs);
                eprintln!("BODYWORLD {target} {:?}",[world[0][3],world[1][3],world[2][3]]);
            }
        }
        for (part_i, part) in sess.parts.iter().enumerate() {
            if part.name.eq_ignore_ascii_case("cf_O_face") {
                let mut counts = HashMap::<String, usize>::new();
                for (vi, bs) in ex.part_bone_idx[part_i].iter().enumerate() {
                    let ws = part.weights.get(vi).copied().unwrap_or([0.0;4]);
                    for k in 0..4 {
                        if ws[k] > 0.05 {
                            let n = ex.bones[bs[k] as usize].name.clone();
                            *counts.entry(n).or_default() += 1;
                        }
                    }
                }
                let mut counts: Vec<_> = counts.into_iter().collect();
                counts.sort_by_key(|x| std::cmp::Reverse(x.1));
                eprintln!("FACEBONES {counts:?}");
            }
        }
    }

    fn open_card() -> Option<OkSession> {
        let game = crate::test_support::game_path(r"");
        if !game.join("abdata").ok_is_dir() {
            return None;
        }
        let card =
            crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if !card.ok_is_file() {
            return None;
        }
        crate::session::open(&game, Some(&card), 0).ok()
    }

    #[test]
    fn export_skel_parent_first_unique_names() {
        let Some(sess) = open_card() else {
            return;
        };
        let ex = build(&sess);
        assert!(
            ex.bones.len() > 80,
            "too few bones {}",
            ex.bones.len()
        );
        let (ok, dups) = _unique_check(&ex.bones);
        assert!(ok, "duplicate bone names {dups:?}");
        for (i, b) in ex.bones.iter().enumerate() {
            assert!(
                b.parent < i as i32,
                "parent {} >= child {} ({})",
                b.parent,
                i,
                b.name
            );
        }
        let head = ex.name_to_idx.get("cf_s_head").copied().expect("missing cf_s_head");
        let face_i = ex
            .bones
            .iter()
            .position(|b| {
                let n = b.name.to_ascii_lowercase();
                n == "cf_j_n_faceroot" || n == "cf_j_faceroot"
            })
            .expect("missing face root");
        let mut p = ex.bones[face_i].parent;
        let mut hit = false;
        for _ in 0..64 {
            if p == head as i32 {
                hit = true;
                break;
            }
            if p < 0 {
                break;
            }
            p = ex.bones[p as usize].parent;
        }
        assert!(
            hit,
            "face root {} not under cf_s_head (parent chain)",
            ex.bones[face_i].name
        );
        eprintln!(
            "ok-extract: skel_export bones={} first={:?}",
            ex.bones.len(),
            ex.bones.iter().take(8).map(|b| b.name.as_str()).collect::<Vec<_>>()
        );
        let height = ex
            .bones
            .iter()
            .find(|b| b.name.eq_ignore_ascii_case("cf_n_height"))
            .expect("cf_n_height");
        assert_eq!(height.layer, 2, "height is shape");
        let arm = ex
            .bones
            .iter()
            .find(|b| b.name.eq_ignore_ascii_case("cf_j_arm00_L"))
            .expect("cf_j_arm00_L");
        assert_eq!(arm.layer, 1, "arm is anim");
        assert!(
            ex.bones.iter().any(|b| b.name.to_ascii_lowercase().starts_with("a_n_")),
            "missing a_n_* slot bones"
        );
    }

    #[test]
    fn vref_reconstructs_body_face_clothes() {
        let Some(mut sess) = open_card() else {
            return;
        };
        sess.skin_all();
        let body_rest = bind_body(&sess).expect("body rest");
        let body_posed = sess.body_skel.clone().expect("body posed");
        let head_rest = bind_head(&sess).expect("head rest");
        let head_posed = sess.head_skel.clone().expect("head posed");

        let mut checked = 0usize;
        for part in &sess.parts {
            let want = if part.skel_kind == 1
                && (part.role == "body" || part.role.contains("clothes") || part.name.to_ascii_lowercase().contains("o_body"))
            {
                Some((&body_rest, &body_posed, false))
            } else if part.skel_kind == 2
                && part.name.to_ascii_lowercase().contains("cf_o_face")
            {
                Some((&head_rest, &head_posed, true))
            } else {
                None
            };
            let Some((rest, posed, is_head)) = want else {
                continue;
            };
            if part.role.contains("clothes") && checked > 2 {
                continue;
            }
            let Some((v_ref, mut v_direct, mut v_recon)) = reconstruct_part(part, rest, posed)
            else {
                continue;
            };
            if is_head {
                let d_rest = crate::head_onto_body(Some(&body_rest), Some(&head_rest));
                let d_posed = crate::head_onto_body(Some(&body_posed), Some(&head_posed));
                apply_mat(&mut v_direct, &d_posed);
                apply_mat(&mut v_recon, &d_posed);
                let _ = d_rest;
                let _ = v_ref;
            }
            let err = max_err(&v_direct, &v_recon);
            eprintln!(
                "ok-extract: vref {} role={} kind={} verts={} err={err:.6}",
                part.name,
                part.role,
                part.skel_kind,
                v_direct.len()
            );
            assert!(
                err < 1e-4,
                "{} reconstruct err {err} (limit 1e-4)",
                part.name
            );
            checked += 1;
        }
        assert!(checked >= 2, "checked {checked} parts, want body+face");
    }

    #[test]
    fn different_clips_change_leg_bones() {
        let Some(mut sess) = open_card() else {
            return;
        };
        let face = sess.shape_face.clone();
        let body = sess.shape_body.clone();
        let rot = |sess: &OkSession, name: &str| -> Option<[f32; 4]> {
            sess.body_skel.as_ref()?.tfs.values().find(|t| t.name.eq_ignore_ascii_case(name)).map(|t| t.rot)
        };
        sess.anim_clip = "Stand_D_00".into();
        sess.apply_shape(&body, &face);
        let stand = rot(&sess, "cf_j_thigh00_L");
        sess.anim_clip = "GSit_00".into();
        sess.apply_shape(&body, &face);
        let sit = rot(&sess, "cf_j_thigh00_L");
        sess.anim_clip = "Stand_00_00".into();
        sess.apply_shape(&body, &face);
        let s00 = rot(&sess, "cf_j_thigh00_L");
        eprintln!("thigh L rot stand={stand:?} sit={sit:?} stand00={s00:?}");
        assert_ne!(stand, sit, "GSit_00 should change thigh vs Stand_D_00");
    }
}
