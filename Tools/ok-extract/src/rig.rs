//! Rigs of the open: a part's mesh rigged to its skeleton, and a part remapped onto the body's bones.
use super::*;

/// `rig(mesh)` once per view and key (crate::view_memo), the rigged mesh after: rigging a face or a body skins every
/// vertex, the same in each garment state of an outfit. The key must determine the mesh before rigging and the
/// skeletons: callers give the card data's digest (CardInfo::source_digest), the outfit and the mesh. No key: rigged
/// afresh.
pub(crate) fn rig_memo(key: Option<String>, mesh: &mut ExtractedMesh, rig: impl FnOnce(&mut ExtractedMesh)) {
    let Some(key) = key else { return rig(mesh) };
    let input = std::mem::take(mesh);
    *mesh = crate::view_memo(format!("rig|{key}"), move || { let mut made = input; rig(&mut made); made });
}

pub(crate) fn rig_mesh(
    mesh: &mut ExtractedMesh,
    role: &str,
    body_skel: Option<&crate::pose::Skeleton>,
    head_skel: Option<&crate::pose::Skeleton>,
    parent_key: Option<&str>,
    part_skel: Option<&crate::pose::Skeleton>,
    part_local: Option<&crate::pose::Mat4>,
    authored_fbs: Option<&HeadFbsInput<'_>>,
    // The accessory's hierarchy moves (begin_acc_place), passed in so the rig is the same on any thread.
    acc_place: Option<[crate::pose::Mat4; 2]>,
) {let _spot=crate::profile::spot("rig_mesh");
    let r = role.to_ascii_lowercase();
    if r == "body" {
        mesh.skel_kind = 1;
        if let Some(sk) = body_skel {
            let _ = skin_mesh(mesh, sk);
        }
        return;
    }
    if r == "mouth" {
        mesh.skel_kind = 1;
        if let (Some(sk), Some(ps)) = (body_skel, part_skel) {
            remap_part_to_body(mesh, sk, ps);
            let _ = skin_mesh(mesh, sk);
        } else if let Some(sk) = body_skel {
            let _ = skin_mesh(mesh, sk);
        }
        return;
    }
    if r.contains("head") || r.contains("face") {
        mesh.skel_kind = 2;
        apply_default_fbs(mesh,authored_fbs);
        // Head detail meshes (eyeline / mayuge / noseline / hitomi ...) each ship
        // their own SMR bone order; remap to the face skeleton by name before
        // skinning, otherwise the wrong bones stretch them across the face.
        if let (Some(sk), Some(ps)) = (head_skel.or(body_skel), part_skel) {
            remap_part_to_body(mesh, sk, ps);
            let _ = skin_mesh(mesh, sk);
        } else if let Some(sk) = head_skel.or(body_skel) {
            let _ = skin_mesh(mesh, sk);
        }
        if std::env::var("OK_MAYUDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("mayuge") {
            let mut left = 0;
            let mut right = 0;
            for (i, p) in mesh.rest_positions.iter().enumerate() {
                if p[0] < 0.0 {
                    left += 1;
                } else {
                    right += 1;
                }
                let _ = i;
            }
            eprintln!(
                "ok-extract: MAYUW counts left(x<0)={left} right={right} weights={} bids={}",
                mesh.weights.len(),
                mesh.bone_idx.len()
            );
            for i in [0usize, 1, 80, 160, 240] {
                if i < mesh.weights.len() {
                    eprintln!(
                        "ok-extract: MAYUW   v{i} rest={:?} bidx={:?} w={:?}",
                        mesh.rest_positions.get(i),
                        mesh.bone_idx.get(i),
                        mesh.weights.get(i)
                    );
                }
            }
            if let Some(sk) = head_skel.or(body_skel) {
                for n in [
                    "cf_J_MayuTip_s_L",
                    "cf_J_MayuMid_s_L",
                    "cf_J_MayuTip_s_R",
                    "cf_J_MayuMid_s_R",
                ] {
                    if let Some(m) = sk.world_tf(n) {
                        let p = crate::pose::mat_pos(&m);
                        eprintln!(
                            "ok-extract: MAYUDIAG {} {} pos=({:.4},{:.4},{:.4}) col0=({:.4},{:.4},{:.4}) col1=({:.4},{:.4},{:.4})",
                            mesh.name, n,
                            p[0], p[1], p[2],
                            m[0][0], m[1][0], m[2][0],
                            m[0][1], m[1][1], m[2][1]
                        );
                    }
                }
            }
        }
        let delta = head_onto_body(body_skel, head_skel);
        if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1")
            && crate::profile::var("OK_HEAD_GPUSKIN").is_err()
        {
            if let Some(sk) = head_skel.or(body_skel) {
                transform_baked_mouth_deltas(mesh, sk, &delta);
            }
        }
        if std::env::var("OK_DIAG").is_ok() {
            let s = crate::pose::world_scale_pub(&delta);
            let rs = head_skel.map(|h| crate::pose::world_scale_pub(&h.root_world())).unwrap_or([0.0; 3]);
            let bs = body_skel
                .and_then(|b| b.world_tf("cf_s_head"))
                .map(|m| crate::pose::world_scale_pub(&m))
                .unwrap_or([0.0; 3]);
            eprintln!("ok-extract: head delta scale={s:?} headroot={rs:?} cf_s_head={bs:?} headroot_pos={:?} cf_s_head_pos={:?} delta_pos={:?}",
                head_skel.map(|h| crate::pose::mat_pos(&h.root_world())),
                body_skel.and_then(|b| b.world_tf("cf_s_head")).map(|m| crate::pose::mat_pos(&m)),
                crate::pose::mat_pos(&delta));
        }
        // `delta` already contains cf_s_head's per-axis scale. Multiplying
        // that scale into the skinned vertices again made the face match the
        // dumper's BakeMesh + renderer.TransformPoint values, which apply
        // renderer scale a second time. KKS's boneWorld * bindpose vertices
        // (the GPU skinning space) are larger; hair already uses that space.
        apply_parent(mesh, &delta);
        // `positions` are already in body space (head skeleton skin + cf_s_head).
        // The kind-2 GPU bake skins the head-local rest pose again and multiplies
        // `head_onto_body` on a shared skeleton that already contains that
        // attach, so the face renders a head-height above the hair. Keep the
        // baked verts, same as hair that failed bone remap.
        if std::env::var("OK_CHINDIAG").is_ok() && mesh.name.eq_ignore_ascii_case("cf_O_face") {
            let mut tot = 0usize;
            for (i, p) in mesh.positions.iter().enumerate() {
                if p[1] < 1.34 && p[1] > 1.28 && p[0].abs() > 0.03 && p[0].abs() < 0.05 && p[2] > -0.01
                {
                    tot += 1;
                    if tot <= 4 {
                        let bname = mesh.bone_idx.get(i).and_then(|bi| {
                            head_skel.and_then(|sk| sk.names.get(bi[3] as usize).map(|s| s.clone()))
                        });
                        eprintln!(
                            "CHINVERT i={i} p={p:?} bi={:?} w={:?} b3={bname:?}",
                            mesh.bone_idx.get(i),
                            mesh.weights.get(i)
                        );
                        if let Some(sk) = head_skel {
                            if let Some(bi) = mesh.bone_idx.get(i) {
                                for b in bi {
                                    let name = sk
                                        .names
                                        .get(*b as usize)
                                        .map(|s| s.as_str())
                                        .unwrap_or("?");
                                    eprintln!("  bone {b} {name}");
                                }
                            }
                        }
                    }
                }
            }
            eprintln!("CHINVERT side_n={tot}");
        }
        if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1")
            && crate::profile::var("OK_HEAD_GPUSKIN").is_err()
        {
            mesh.rest_positions = mesh.positions.clone();
            mesh.rest_normals = mesh.normals.clone();
            mesh.skel_kind = 0;
            mesh.weights.clear();
            mesh.bone_idx.clear();
            mesh.rigid_follow_bone = Some("cf_s_head".into());
        }
        if std::env::var("OK_DIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("hitomi") {
            let mut mn = [f32::MAX; 3];
            let mut mx = [f32::MIN; 3];
            for p in &mesh.positions {
                for k in 0..3 {
                    mn[k] = mn[k].min(p[k]);
                    mx[k] = mx[k].max(p[k]);
                }
            }
            eprintln!(
                "ok-extract: EYEAABB {} x[{:.3},{:.3}] y[{:.3},{:.3}] z[{:.3},{:.3}]",
                mesh.name, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]
            );
        }
        return;
    }
    // Legacy fallback for hair without an owned skinned rig. Normal hair uses
    // AccessoryRuntime::from_hair_source: copyWeights=0 and its private prefab
    // skeleton, attached below the live HairParent without bone-name remapping.
    if r.contains("hair") {
        let has_full_head = match (head_skel, part_skel) {
            (Some(hsk), Some(ps)) => ps
                .names
                .first()
                .map(|root| hsk.index_of(root).is_some())
                .unwrap_or(false),
            _ => false,
        };
        if crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1") {
            let owned = mesh.smr_skel.clone().or_else(|| part_skel.cloned());
            if mesh.name.contains("cf_hair_31") {
                let _ = std::fs::write(
                    r"C:\portable\OpenKoikatsu\Saved\hair_gpu.txt",
                    format!(
                        "env=1 head={} smr={} part={} bones={}\n",
                        head_skel.is_some(),
                        mesh.smr_skel.is_some(),
                        part_skel.is_some(),
                        owned.as_ref().map(|s| s.names.len()).unwrap_or(0)
                    ),
                );
            }
            if let (Some(hsk), Some(ps)) = (head_skel, owned.as_ref()) {
                let mapped = ps
                    .names
                    .iter()
                    .filter(|n| hsk.index_of(n).is_some())
                    .count();
                if mapped > 0 {
                    if mesh.rest_positions.len() != mesh.positions.len() {
                        mesh.rest_positions = mesh.positions.clone();
                    }
                    remap_part_to_body(mesh, hsk, ps);
                    mesh.skel_kind = 3;
                    eprintln!(
                        "ok-extract: hair {} gpu-skin head_full mapped={mapped}/{}",
                        mesh.name,
                        ps.names.len()
                    );
                    return;
                }
            }
        }
        if has_full_head {
            let hsk = head_skel.unwrap();
            let ps = part_skel.unwrap();
            remap_part_to_body(mesh, hsk, ps);
            if crate::profile::var("OK_SKELMESH").ok().as_deref() == Some("1") {
                let hung = skin_mesh(mesh, hsk);
                mesh.rest_positions = mesh.positions.clone();
                let delta = head_onto_body(body_skel, Some(hsk));
                apply_parent(mesh, &delta);
                eprintln!("ok-extract: hair {} pmc under head skinned={hung}", mesh.name);
                return;
            }
            let hung = skin_mesh(mesh, hsk);
            mesh.rest_positions = mesh.positions.clone();
            let delta = head_onto_body(body_skel, Some(hsk));
            apply_parent(mesh, &delta);
            eprintln!("ok-extract: hair {} full-head remap skinned={hung}", mesh.name);
            return;
        }
        // Fallback: skin with the hair prefab's own skeleton, then snap under
        // HairParent (cf_J_FaceUp_ty).
        let skel_owned = mesh.smr_skel.clone().or_else(|| part_skel.cloned());
        if let Some(mut ordered) = skel_owned {
            if !mesh.bone_names.is_empty() {
                ordered.set_bone_order(&mesh.bone_names);
            }
            let _ = skin_mesh(mesh, &ordered);
        }
        mesh.rest_positions = mesh.positions.clone();
        if let Some(m) = bone_world(body_skel, head_skel, "cf_J_FaceUp_ty") {
            // Hair prefabs skin against their own (scale 1) skeleton and the
            // game parents them under HairParent. `snap_under_parent` already
            // applies HairParent's world (including the head-shape scale), so an
            // extra world_scale(m) here double-counts it: the T-pose dump
            // (20260914_023857) shows the hair comes out ~0.78x too small with
            // it. `OK_ACCSCALE` re-enables the old behaviour for A/B only.
            if std::env::var("OK_ACCSCALE").is_ok() {
                let d = crate::pose::world_scale_pub(&m);
                if d[0] > 0.0 && d[1] > 0.0 && d[2] > 0.0 {
                    for p in &mut mesh.positions {
                        p[0] *= d[0];
                        p[1] *= d[1];
                        p[2] *= d[2];
                    }
                }
            }
            snap_under_parent(mesh, &m, part_skel, None);
        }
        if mesh.skel_kind != 3 {
            // Bone-name remap failed (head mods, accessory hair with its own
            // skeleton). The skin + snap_under_parent above already left world
            // verts. Collapsing those onto one socket (often a_n_headside)
            // stacks every strand on a single joint. Keep the baked verts.
            // This authored socket also belongs to CPU-source exports: a
            // MeshRenderer without weights still follows ChangeHair's parent.
            mesh.parent_bone = None;
            mesh.rigid_follow_bone = Some("cf_J_FaceUp_ty".into());
            eprintln!("ok-extract: hair {} world-bake (no head-bone remap)", mesh.name);
        }
        return;
    }
    // ChangeAccessoryParent: SetParent(RefObjKey from parentKey, false).
    // Vertices are baked once into body space (parent * both N_move locals).
    // parent_bone stays empty so the viewer does not attach again.
    if r.contains("acc") {
        let key = parent_key.unwrap_or("");
        let hierarchy_moves = acc_place
            .or_else(|| part_local.map(|l| [*l, identity_move()]));
        let moved_skel = hierarchy_moves.as_ref()
            .and_then(|locals| accessory_skeleton_with_moves(mesh, part_skel, locals));
        let moves_in_skeleton = moved_skel.is_some();
        let skel_owned = moved_skel.or_else(|| mesh.smr_skel.clone()).or_else(|| part_skel.cloned())
            .filter(|_|mesh.uses_skin_renderer());
        if let Some(mut ordered) = skel_owned {
            if !mesh.bone_names.is_empty() {
                ordered.set_bone_order(&mesh.bone_names);
            }
            if let (Some(set), Some(parent)) = (
                mesh.deferred_dbones.as_ref(), bone_world(body_skel, head_skel, key)
            ) {
                let locals = acc_place
                    .or_else(|| part_local.map(|l| [*l, identity_move()]));
                if let Some(locals) = locals {
                    let placement = if moves_in_skeleton { parent }
                        else { accessory_placement_matrix(mesh, &parent, part_skel, &locals) };
                    let local=crate::pose::mat_mul_pub(&crate::pose::mat_inv_pub(&parent),&placement);
                    let chain=attachment_transform_chain(body_skel,head_skel,key,&local);
                    let used = if let Some(chain)=chain.filter(|_|std::env::var("OK_DBSTST").is_err()&&std::env::var("OK_DB_MOVE").is_err()) {
                        crate::dbone::hang_set_attached_with_chain(set,&mut ordered,&chain)
                    } else {crate::dbone::hang_set_attached(set, &mut ordered, &placement)};
                    if used > 0 { eprintln!("ok-extract: acc {} attached DynamicBone components={used}", mesh.name); }
                }
            }
            if std::env::var("OK_ACCBONE").is_ok() && crate::pose::accdiag_matches(&mesh.name) {
                let tfs_names: Vec<String> = ordered
                    .tfs
                    .values()
                    .map(|t| t.name.clone())
                    .filter(|n| {
                        n.contains("joint")
                            || n.contains("nc")
                            || n.contains("N_move")
                            || n.contains("slot")
                            || n.contains("miko2")
                            || n.contains("ca_")
                    })
                    .collect();
                eprintln!(
                    "ok-extract: ACCBONE {} smr_bone_names={:?} skel_names={:?} tfs_names={:?}",
                    mesh.name, mesh.bone_names, ordered.names, tfs_names
                );
                for (i, n) in ordered.names.iter().enumerate() {
                    let id = ordered.bone_ids.get(i).copied();
                    let tf = id.and_then(|id| ordered.tfs.get(&id));
                    let w = ordered.worlds.get(i);
                    eprintln!(
                        "ok-extract: ACCBONE {} [{}] {} local_pos={:?} local_rot={:?} local_scl={:?} world={:?}",
                        mesh.name,
                        i,
                        n,
                        tf.map(|t| t.pos),
                        tf.map(|t| t.rot),
                        tf.map(|t| t.scale),
                        w.map(crate::pose::mat_pos)
                    );
                }
            }
            let hung = skin_mesh(mesh, &ordered);
            if crate::pose::accdiag_matches(&mesh.name) {
                let max_bi = mesh
                    .bone_idx
                    .iter()
                    .flat_map(|b| b.iter())
                    .copied()
                    .max()
                    .unwrap_or(0);
                eprintln!(
                    "ok-extract: ACCDIAG {} rest_p0={:?} bind0={:?} skinned_p0={:?} bone_names={} bind_poses={} max_bi={} smr_names={:?}",
                    mesh.name,
                    mesh.rest_positions.first(),
                    mesh.bind_poses.first().map(crate::pose::mat_pos),
                    mesh.positions.first(),
                    mesh.bone_names.len(),
                    mesh.bind_poses.len(),
                    max_bi,
                    mesh.bone_names,
                );
                eprintln!(
                    "ok-extract: ACCW {} bi0={:?} w0={:?} bi1={:?} w1={:?}",
                    mesh.name,
                    mesh.bone_idx.first(),
                    mesh.weights.first(),
                    mesh.bone_idx.get(1),
                    mesh.weights.get(1)
                );
                for (i, w) in ordered.worlds.iter().enumerate() {
                    eprintln!("ok-extract: ACCDIAG {} world{i}={:?}", mesh.name, w);
                }
            }
            eprintln!("ok-extract: acc {} skinned={hung}", mesh.name);
        }
        mesh.rest_positions = mesh.positions.clone();
        mesh.parent_bone = None;
        if let Some(parent) = bone_world(body_skel, head_skel, key) {
            let locals = acc_place.or_else(|| {
                part_local.map(|l| [*l, identity_move()])
            });
            if let Some(locals) = locals {
                if moves_in_skeleton { apply_parent(mesh, &parent); }
                else { snap_accessory(mesh, &parent, part_skel, &locals); }
            } else {
                snap_under_parent(mesh, &parent, part_skel, None);
            }
            mesh.parent_bone = None;
            mesh.rigid_follow_bone = Some(key.to_string());
            // Session.skin_all restores rest buffers before its animation
            // update. The attachment is already baked into world space here.
            // Keeping the pre-placement rest buffer undoes both card move and
            // the renderer transform on the first frame.
            mesh.rest_positions = mesh.positions.clone();
            mesh.rest_normals = mesh.normals.clone();
            eprintln!("ok-extract: acc {} body-bake parent {key}", mesh.name);
            if crate::pose::accdiag_matches(&mesh.name) {
                eprintln!(
                    "ok-extract: ACCDIAG {} parent={} P_pos={:?} final_p0={:?}",
                    mesh.name,
                    key,
                    crate::pose::mat_pos(&parent),
                    mesh.positions.first()
                );
            }
        } else {
            eprintln!("ok-extract: acc {} missing parent '{key}'", mesh.name);
        }
        return;
    }
    if r.starts_with("clothes") {
        mesh.skel_kind = 1;
        // A prefab's sub-meshes can ship different SMR bone lists (e.g. the
        // skirt `o_bot_skirt01_a` has 13 bones, `_b` has 11). Remap each mesh
        // with its OWN SMR skeleton, not the entry-level one, or the second
        // mesh is skinned to the wrong bones and stretches.
        let own = mesh.smr_skel.clone();
        let ps = own.as_ref().or(part_skel);
        if let (Some(sk), Some(ps)) = (body_skel, ps) {
            remap_part_to_body(mesh, sk, ps);
            let _ = skin_mesh(mesh, sk);
        } else if let Some(sk) = body_skel {
            let _ = skin_mesh(mesh, sk);
        }
    }
}

/// Remap a part mesh (clothes / mouth) that ships its own SMR bone list onto the
/// body skeleton by bone name: rewrite vertex bone indices and re-index the part
/// bind poses into body-skeleton order, so the part keeps its own (vanilla) bind
/// instead of the mod body's rest.
pub(crate) fn remap_part_to_body(
    mesh: &mut ExtractedMesh,
    sk: &crate::pose::Skeleton,
    ps: &crate::pose::Skeleton,
) {
    // A part bone that is not itself in the target skeleton (e.g. the iris mesh
    // has only `cf_J_hitomi_tx_L`, a child of `cf_J_Eye_rz_L`) resolves to its
    // nearest ancestor that is. At rest those coincide, so the part is placed
    // correctly instead of being pinned to bone 0 (which crumples it).
    let resolve = |n: &str| -> Option<u16> {
        if let Some(i) = sk.index_of(n) {
            return Some(i as u16);
        }
        let start = ps
            .tfs
            .iter()
            .find(|(_, t)| t.name == n)
            .map(|(id, _)| *id)?;
        let mut cur = ps.tfs.get(&start).and_then(|t| t.parent);
        let mut guard = 0;
        while let Some(p) = cur {
            guard += 1;
            if guard > 64 {
                break;
            }
            let Some(t) = ps.tfs.get(&p) else { break };
            if let Some(i) = sk.index_of(&t.name) {
                return Some(i as u16);
            }
            cur = t.parent;
        }
        None
    };
    let map: Vec<u16> = ps.names.iter().map(|n| resolve(n).unwrap_or(0)).collect();
    if std::env::var("OK_RDIAG").is_ok() {
        eprintln!(
            "RDIAG {} target_bones={} part_bones={}",
            mesh.name,
            sk.names.len(),
            ps.names.len()
        );
        for (ci, n) in ps.names.iter().enumerate() {
            let r = resolve(n);
            let tn = r
                .and_then(|i| sk.names.get(i as usize))
                .map(|x| x.as_str())
                .unwrap_or("<none->0>");
            eprintln!("RDIAG   [{ci}] {n} -> {r:?} {tn}");
        }
    }
    if std::env::var("OK_MAYUDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("mayuge") {
        let dups: Vec<&str> = {
            let mut seen = std::collections::HashSet::new();
            sk.names
                .iter()
                .filter(|n| n.contains("Mayu") && !seen.insert(n.as_str()))
                .map(|s| s.as_str())
                .collect()
        };
        eprintln!(
            "ok-extract: MAYUBIND target_bones={} dup_mayu_names={:?} part_binds={}",
            sk.names.len(),
            dups,
            mesh.bind_poses.len()
        );
        for (ci, n) in ps.names.iter().enumerate() {
            let bi = map[ci];
            let bp = mesh
                .bind_poses
                .get(ci)
                .map(|m| (m[0][3], m[1][3], m[2][3]));
            let wn = sk.names.get(bi as usize).map(|s| s.as_str()).unwrap_or("?");
            eprintln!(
                "ok-extract: MAYUBIND   ci={ci} {n} -> bi={bi} {wn} bindT={bp:?}"
            );
        }
    }
    for q in mesh.bone_idx.iter_mut() {
        for b in q.iter_mut() {
            let i = *b as usize;
            if i < map.len() {
                *b = map[i];
            }
        }
    }
    if !mesh.bind_poses.is_empty() {
        let world_n = sk.worlds.len();
        let mut nb = crate::pose::binds_from_rest(&sk.rest);
        if nb.len() < world_n {
            let last = *nb.last().unwrap_or(&[[0.0f32; 4]; 4]);
            nb.resize(world_n, last);
        }
        if std::env::var("OK_NOPARTBIND").is_err() {
            for (ci, n) in ps.names.iter().enumerate() {
                if let Some(bi) = resolve(n) {
                    if ci < mesh.bind_poses.len() && (bi as usize) < nb.len() {
                        nb[bi as usize] = mesh.bind_poses[ci];
                    }
                }
            }
        }
        mesh.bind_poses = nb;
    } else {
        mesh.bind_poses.clear();
    }
}
