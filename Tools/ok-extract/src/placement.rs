//! Meshes into place: writing and emitting parts, skinning, head onto body, attachment chains and accessory placement.
use super::*;

pub(crate) fn write_mesh(
    out_dir: &Path,
    file_stem: &str,
    mesh: &ExtractedMesh,
    tint: Option<[f32; 4]>,
) -> std::io::Result<(PathBuf, Option<String>)> {
    let safe: String = file_stem
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '_' { c } else { '_' })
        .collect();
    let path = out_dir.join(format!("{safe}.obj"));
    let obj = if mesh.obj.is_empty() {
        crate::unity::mesh_to_obj(&mesh.name, &mesh.positions, &mesh.uvs, &mesh.indices)
    } else {
        mesh.obj.clone()
    };
    std::fs::write(&path, obj)?;
    let tex = if let Some(png) = &mesh.texture_png {
        let bytes = tint
            .and_then(|c| crate::unity::tint_png(png, c))
            .unwrap_or_else(|| png.clone());
        let tp = format!("{safe}.png");
        std::fs::write(out_dir.join(&tp), &*crate::teximg::as_png(&bytes))?;
        Some(tp)
    } else {
        None
    };
    Ok((path, tex))
}

pub(crate) fn emit_part(
    out_dir: Option<&Path>,
    session_parts: &mut Vec<crate::session::SessionPart>,
    stem: &str,
    role: &str,
    mesh: &ExtractedMesh,
    blend: &str,
    unlit: bool,
) -> Result<(String, Option<String>), String> {let _spot=crate::profile::spot("emit_part");
    session_parts.push(crate::session::SessionPart::from_mesh(
        role, mesh, blend, unlit,
    ));
    if let Some(dir) = out_dir {
        let (p, tex) = write_mesh(dir, stem, mesh, None).map_err(|e| e.to_string())?;
        // Optional source-input audit. These are the exact PNG bytes handed
        // to the session shader path, beside the exported mesh/albedo. Keep
        // them out of normal exports to avoid duplicating large textures.
        if std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() == Ok("1")
            || (std::env::var("OK_DUMP_HAIR_INPUTS").as_deref() == Ok("1")
                && (mesh.hair_shader_hint || mesh.mat.as_ref().is_some_and(|m|
                    is_native_hair_shader(&m.shader))))
        {
            for (suffix, bytes) in [
                ("color-mask.png", mesh.color_mask_png.as_deref()),
                ("detail-mask.png", mesh.detail_mask_png.as_deref()),
                ("hair-gloss.png", mesh.hair_gloss_png.as_deref()),
            ] {
                if let Some(bytes) = bytes {
                    std::fs::write(p.with_extension(suffix), &*crate::teximg::as_png(bytes)).map_err(|e| e.to_string())?;
                }
            }
            if std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() == Ok("1") {
                let mut inputs = std::collections::BTreeMap::new();
                for (property,bytes) in [("_MainTex",&mesh.texture_png),("_ColorMask",&mesh.color_mask_png),
                    ("_DetailMask",&mesh.detail_mask_png),("_LineMask",&mesh.line_mask_png),
                    ("_AlphaMask",&mesh.alpha_mask_png),("_NormalMap",&mesh.normal_map_png),("_HairGloss",&mesh.hair_gloss_png)] {
                    if let Some(bytes)=bytes {let file=p.with_extension(format!("shader-{property}.png"));
                        std::fs::write(&file,&*crate::teximg::as_png(bytes)).map_err(|e|e.to_string())?;
                        inputs.insert(property.to_string(),file.file_name().unwrap().to_string_lossy().into_owned());}
                }
                // The session table is the exact set handed to native shaders,
                // including ME replacements. Null shader defaults stay absent.
                for (property, bytes) in &session_parts.last().unwrap().shader_textures {
                    if !property.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
                        return Err(format!("Unsafe shader input property: {property}"));
                    }
                    let file = p.with_extension(format!("shader-{property}.png"));
                    std::fs::write(&file, &*crate::teximg::as_png(bytes)).map_err(|e| e.to_string())?;
                    inputs.insert(property.clone(), file.file_name().unwrap().to_string_lossy().into_owned());
                }
                let audit = serde_json::json!({"mesh":mesh.name,"role":role,"material":mesh.mat,
                    "shader_inputs":inputs,"runtime_textures":mesh.runtime_textures,
                    "global_ramp":"toon-ramp.png","scope":"session PNG inputs, not GPU mip or final colour parity"});
                std::fs::write(p.with_extension("material-inputs.json"),serde_json::to_vec_pretty(&audit).map_err(|e|e.to_string())?)
                    .map_err(|e|e.to_string())?;
            }
        }
        let name = p
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_else(|| format!("{stem}.obj"));
        Ok((name, tex))
    } else {
        Ok((format!("{stem}.obj"), None))
    }
}

pub(crate) fn pick_path<'a>(
    meshes: &'a [ExtractedMesh],
    want: &str,
    pid: i64,
) -> Option<&'a ExtractedMesh> {
    meshes
        .iter()
        .find(|m| m.path_id == pid && m.name.eq_ignore_ascii_case(want))
        .or_else(|| meshes.iter().find(|m| m.path_id == pid))
}

pub(crate) fn skin_mesh(mesh: &mut ExtractedMesh, skel: &crate::pose::Skeleton) -> bool {
    skin_mesh_impl(mesh,skel,true)
}
pub(crate) fn skin_mesh_geometry(mesh:&mut ExtractedMesh,skel:&crate::pose::Skeleton)->bool {
    skin_mesh_impl(mesh,skel,false)
}
pub(crate) fn skin_mesh_impl(mesh:&mut ExtractedMesh,skel:&crate::pose::Skeleton,emit_obj:bool)->bool {
    let ln = mesh.name.to_ascii_lowercase();
    // Unity skins every renderer with its authored weights. Classifying by
    // asset name made mod hair such as `35_mesh` take the old repair/prune/
    // spike path and shifted its game-dump bounds by 16 mm. Use the same LBS
    // rule for all card meshes, regardless of name or source category.
    let sm = std::env::var("OK_SMOOTH")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(0);
    crate::pose::smooth_weights(mesh, sm);
    // Unity: pal = bone.localToWorld * mesh.bindPose. Never require
    // bind_poses.len() >= worlds.len() ??o_body_a is 100 binds / 101 SMR bones
    // and falling back to inv(rest) made pal = I (raw mesh, head/body split).
    let n = if !mesh.bind_poses.is_empty() {
        mesh.bind_poses.len().min(skel.worlds.len())
    } else {
        skel.worlds.len().min(skel.rest.len())
    };
    if n == 0 || mesh.weights.is_empty() {
        return false;
    }
    if mesh.rest_positions.len() != mesh.positions.len() {
        mesh.rest_positions = mesh.positions.clone();
    }
    let posed = skel.worlds[..n].to_vec();
    if std::env::var("OK_BONEDIAG").is_ok() && ln.starts_with("o_body_") {
        let mut out = String::new();
        for (i, w) in posed.iter().enumerate() {
            let nm = skel.names.get(i).cloned().unwrap_or_default();
            if nm.is_empty() {
                continue;
            }
            out.push_str(&format!("{nm} {:.6} {:.6} {:.6}\n", w[0][3], w[1][3], w[2][3]));
        }
        let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\our_bones.txt", out);
        let mut loc = String::new();
        for t in skel.tfs.values() {
            let par = t
                .parent
                .and_then(|p| skel.tfs.get(&p))
                .map(|p| p.name.clone())
                .unwrap_or_default();
            loc.push_str(&format!(
                "{} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {:.6} {}\n",
                t.name, t.pos[0], t.pos[1], t.pos[2], t.rot[0], t.rot[1], t.rot[2], t.rot[3],
                t.scale[0], t.scale[1], t.scale[2], par
            ));
        }
        let _ = std::fs::write(r"C:\Users\Damody\AppData\Local\Temp\opencode\our_bone_locals.txt", loc);
    }
    let binds = if mesh.bind_poses.len() >= n {
        mesh.bind_poses[..n].to_vec()
    } else {
        crate::pose::binds_from_rest(&skel.rest[..n])
    };
    if binds.len() != posed.len() {
        return false;
    }
    if std::env::var("OK_SKINDIAG").is_ok()
        && (ln.contains("cf_o_face") || ln.contains("hair_f_04_00"))
    {
        let s_b = crate::pose::world_scale_pub(&binds[0]);
        let s_p = crate::pose::world_scale_pub(&posed[0]);
        let sm = crate::pose::mat_mul_pub(&posed[0], &binds[0]);
        let s_m = crate::pose::world_scale_pub(&sm);
        let pp = crate::pose::mat_pos(&posed[0]);
        eprintln!(
            "SKINDIAG {} n={n} bind0s=({:.4},{:.4},{:.4}) p0=({:.4},{:.4},{:.4}) ps=({:.4},{:.4},{:.4}) sm=({:.4},{:.4},{:.4})",
            mesh.name,
            s_b[0], s_b[1], s_b[2],
            pp[0], pp[1], pp[2],
            s_p[0], s_p[1], s_p[2],
            s_m[0], s_m[1], s_m[2]
        );
    }
    let Some(mut pos) = (if std::env::var("OK_DQS").is_ok() {
        crate::pose::skin_positions_dqs(mesh, &posed, &binds)
    } else {
        crate::pose::skin_positions(mesh, &posed, &binds)
    }) else {
        return false;
    };
    if std::env::var("OK_SKINDIAG").is_ok() && ln.contains("hitomi") {
        let (r0, r1) = crate::pose::aabb(&mesh.rest_positions);
        let (s0, s1) = crate::pose::aabb(&pos);
        eprintln!(
            "SKINDIAG {} rest x[{:.4},{:.4}] y[{:.4},{:.4}] z[{:.4},{:.4}] -> skin x[{:.4},{:.4}] y[{:.4},{:.4}] z[{:.4},{:.4}]",
            mesh.name,
            r0[0], r1[0], r0[1], r1[1], r0[2], r1[2],
            s0[0], s1[0], s0[1], s1[1], s0[2], s1[2]
        );
        for i in 0..n {
            let m = crate::pose::mat_mul_pub(&posed[i], &binds[i]);
            let s = crate::pose::world_scale_pub(&m);
            if (s[0] - 1.0).abs() > 0.1 || (s[1] - 1.0).abs() > 0.1 || (s[2] - 1.0).abs() > 0.1 {
                eprintln!(
                    "SKINBONE {} [{}] {} prod_scale={:?}",
                    mesh.name,
                    i,
                    skel.names.get(i).map(|x| x.as_str()).unwrap_or("?"),
                    s
                );
            }
        }
    }
    // Region-limited dual-quaternion skinning: only the shoulder/arm vertices
    // use DQS, which removes the deltoid collapse without the knee bulging that
    // full DQS causes.
    if std::env::var("OK_DQSH").is_ok() {
        if let Some(dq) = crate::pose::skin_positions_dqs(mesh, &posed, &binds) {
            for i in 0..pos.len() {
                let b = mesh.bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
                let w = mesh.weights.get(i).copied().unwrap_or([0.0; 4]);
                let arm = (0..4).any(|k| {
                    w[k] > 0.15
                        && skel
                            .bone_ids
                            .get(b[k] as usize)
                            .and_then(|id| skel.tfs.get(id))
                            .map(|t| {
                                let n = &t.name;
                                n.contains("shoulder")
                                    || n.contains("arm")
                                    || n.contains("forearm")
                                    || n.contains("hand")
                                    || n.contains("wrist")
                            })
                            .unwrap_or(false)
                });
                if arm {
                    pos[i] = dq[i];
                }
            }
        }
    }
    // Laplacian relaxation used to be applied here but it carves a notch into
    // the pose+shape shoulder, so it is only kept behind an opt-in flag.
    if std::env::var("OK_RELAX").is_ok() {
        crate::pose::relax_positions(&mut pos, &mesh.indices, 1, 0.2);
    }
    if mesh.rest_normals.len() != mesh.normals.len() {
        mesh.rest_normals = mesh.normals.clone();
    }
    mesh.normals = mesh.rest_normals.clone();
    if let Some(nrm) = crate::pose::skin_normals(mesh, &posed, &binds) {
        mesh.normals = nrm;
    }
    if std::env::var("OK_DIAG").is_ok() && mesh.name == "o_body_a" {
        let mut band = vec![f32::MIN; 24];
        for p in &pos {
            let bx = ((p[0] + 0.24) / 0.02).floor() as i32;
            if (0..24).contains(&bx) && p[2] > -0.12 {
                let b = bx as usize;
                if p[1] > band[b] {
                    band[b] = p[1];
                }
            }
        }
        let prof: Vec<String> = band
            .iter()
            .enumerate()
            .map(|(i, y)| format!("{:.3}@{:.2}", y, -0.24 + i as f32 * 0.02))
            .collect();
        eprintln!("ok-extract: DIAG shoulder-top {}", prof.join(" "));
        let bnames: Vec<String> = skel
            .bone_ids
            .iter()
            .enumerate()
            .filter_map(|(i, id)| {
                skel.tfs.get(id).map(|t| format!("{i}:{}", t.name))
            })
            .collect();
        eprintln!("ok-extract: DIAG bones {}", bnames.join(" "));
        for (i, p) in pos.iter().enumerate() {
            if p[0].abs() > 0.20 || p[0].abs() < -0.20 {
                let mut parts = Vec::new();
                for k in 0..4 {
                    let bi = mesh.bone_idx[i][k] as usize;
                    let w = mesh.weights[i][k];
                    if w > 0.04 {
                        let nm = skel
                            .bone_ids
                            .get(bi)
                            .and_then(|id| skel.tfs.get(id))
                            .map(|t| t.name.clone())
                            .unwrap_or_else(|| "?".into());
                        parts.push(format!("{nm}:{w:.2}"));
                    }
                }
                eprintln!(
                    "ok-extract: DIAG spike v{i} ({:.3},{:.3},{:.3}) [{}]",
                    p[0],
                    p[1],
                    p[2],
                    parts.join(",")
                );
            }
        }
        for (i, p) in pos.iter().enumerate() {
            if p[1] > 0.85 && p[1] < 1.20 && p[2] > 0.05 {
                let mut parts = Vec::new();
                for k in 0..4 {
                    let bi = mesh.bone_idx[i][k] as usize;
                    let w = mesh.weights[i][k];
                    if w > 0.04 {
                        let nm = skel
                            .bone_ids
                            .get(bi)
                            .and_then(|id| skel.tfs.get(id))
                            .map(|t| t.name.clone())
                            .unwrap_or_else(|| "?".into());
                        parts.push(format!("{nm}:{w:.2}"));
                    }
                }
                eprintln!(
                    "ok-extract: DIAG breast v{i} ({:.3},{:.3},{:.3}) [{}]",
                    p[0],
                    p[1],
                    p[2],
                    parts.join(",")
                );
            }
        }
        let mut shown = 0;
        for (i, p) in pos.iter().enumerate() {
            let mut bw = 0.0f32;
            for k in 0..4 {
                let nm = skel
                    .bone_ids
                    .get(mesh.bone_idx[i][k] as usize)
                    .and_then(|id| skel.tfs.get(id))
                    .map(|t| t.name.clone())
                    .unwrap_or_default();
                if nm.contains("bust") {
                    bw = bw.max(mesh.weights[i][k]);
                }
            }
            if bw > 0.05 && shown < 25 {
                eprintln!(
                    "ok-extract: DIAG bustw v{i} ({:.3},{:.3},{:.3}) maxbust={:.2}",
                    p[0], p[1], p[2], bw
                );
                shown += 1;
            }
        }
    }
    if emit_obj || std::env::var("OK_SKINDIAG").is_ok() {
    let (mn, mx) = crate::pose::aabb(&pos);
    let mut max_bi = 0u16;
    for b in &mesh.bone_idx {
        for k in 0..4 {
            if (b[k] as usize) < 4096 {
                max_bi = max_bi.max(b[k]);
            }
        }
    }
    eprintln!(
        "ok-extract: skinned {} nv={} aabb x=[{:.3},{:.3}] y=[{:.3},{:.3}] z=[{:.3},{:.3}] bones={n} bind={} max_bi={max_bi}",
        mesh.name, mesh.positions.len(), mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], mesh.bind_poses.len()
    );
    }
    if emit_obj {crate::pose::rebuild_obj(mesh, pos);}else{mesh.positions=pos;}
    true
}

/// ChaReference: HeadParent=cf_s_head, HairParent=cf_J_FaceUp_ty.
/// Head bone lives on the body; face bones live on p_cf_head_bone which is
/// SetParent(cf_s_head, worldPositionStays: false).
pub(crate) fn head_onto_body(body: Option<&crate::pose::Skeleton>, head: Option<&crate::pose::Skeleton>) -> crate::pose::Mat4 {
    let parent = body
        .and_then(|b| b.world_tf("cf_s_head"))
        .unwrap_or_else(crate::pose::ident);
    let root = head
        .map(|h| h.root_world())
        .unwrap_or_else(crate::pose::ident);
    crate::pose::under_parent(&parent, &root)
}

pub(crate) fn is_head_attach(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.starts_with("cf_j_")
        || n.starts_with("a_n_hair")
        || n.starts_with("a_n_head")
        || n.starts_with("a_n_megane")
        || n.starts_with("a_n_ear")
        || n.starts_with("a_n_nose")
        || n.starts_with("a_n_mouth")
}

pub(crate) fn bone_world(
    body: Option<&crate::pose::Skeleton>,
    head: Option<&crate::pose::Skeleton>,
    name: &str,
) -> Option<crate::pose::Mat4> {
    if name.is_empty() || name.eq_ignore_ascii_case("none") || name == "0" {
        return None;
    }
    // ChaReference: HairParent / a_n_hair_* / a_n_head* live on the head prefab
    // (CreateReferenceInfo case 1). oo_base also contains p_cf_head_bone, so
    // looking up those names on the body skeleton would hit the unparented copy.
    if is_head_attach(name) {
        if let Some(h) = head {
            if let Some(m) = h.world_tf(name) {
                let attach = head_onto_body(body, head);
                return Some(crate::pose::mat_mul_pub(&attach, &m));
            }
        }
    }
    if let Some(b) = body {
        if let Some(m) = b.world_tf(name) {
            return Some(m);
        }
    }
    if let Some(h) = head {
        if let Some(m) = h.world_tf(name) {
            let attach = head_onto_body(body, head);
            return Some(crate::pose::mat_mul_pub(&attach, &m));
        }
    }
    None
}

/// Preserve the actual Transform ancestry for DynamicBone. A composed socket
/// matrix can contain shear and cannot be reduced to one local TRS.
pub(crate) fn attachment_transform_chain(body:Option<&crate::pose::Skeleton>,head:Option<&crate::pose::Skeleton>,name:&str,local:&crate::pose::Mat4)->Option<Vec<crate::pose::Tf>> {
    use crate::pose::{Tf,ident,mat_mul_pub,trs};
    let path=|skel:&crate::pose::Skeleton,key:&str|->Option<Vec<Tf>> {
        let mut id=skel.names.iter().position(|n|n.eq_ignore_ascii_case(key)).and_then(|i|skel.bone_ids.get(i).copied())
            .or_else(||skel.tfs.iter().filter(|(_,tf)|tf.name.eq_ignore_ascii_case(key)).map(|(id,_)|*id).min())?;
        let mut chain=Vec::new();let mut seen=std::collections::HashSet::new();
        loop {if !seen.insert(id){return None;}let tf=skel.tfs.get(&id)?;chain.push(tf.clone());
            match tf.parent.filter(|p|*p!=0) {Some(parent)=>id=parent,None=>break,}}
        chain.reverse();Some(chain)
    };
    let use_head=head.is_some_and(|h|h.world_tf(name).is_some())
        && (is_head_attach(name)||body.is_none_or(|b|b.world_tf(name).is_none()));
    let mut chain=if use_head {
        let mut prefix=body.and_then(|b|path(b,"cf_s_head")).unwrap_or_default();
        let mut suffix=path(head?,name)?;
        // Head SetParent(cf_s_head,false) replaces the authored root world.
        let root=suffix.first_mut()?;root.pos=[0.;3];root.rot=[0.,0.,0.,1.];root.scale=[1.;3];
        prefix.extend(suffix);prefix
    } else {path(body?,name)?};
    let mut extra=Tf {name:"__ok_attachment_local".into(),parent:None,sibling_index:None,pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
    crate::pose::replace_local_trs(&mut extra,local);chain.push(extra);
    let represented=chain.iter().fold(ident(),|m,tf|mat_mul_pub(&m,&trs(tf.pos,tf.rot,tf.scale)));
    let expected=mat_mul_pub(&bone_world(body,head,name)?,local);
    if represented.iter().flatten().zip(expected.iter().flatten()).any(|(a,b)|!a.is_finite()||(a-b).abs()>2e-5){return None;}
    Some(chain)
}

pub(crate) fn accessory_slot_from_property(property: &str) -> Option<usize> {
    let p = property.to_ascii_lowercase();
    let i = p.find("accessory")?;
    let rest = &p[i + 9..];
    let n: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    if n.is_empty() {
        None
    } else {
        n.parse().ok()
    }
}

pub(crate) fn acc_move_matrix(mv: crate::card::AccMove) -> crate::pose::Mat4 {
    let pos = [mv.pos[0] * 0.01, mv.pos[1] * 0.01, mv.pos[2] * 0.01];
    let scl = [
        mv.scale[0].clamp(0.01, 100.0),
        mv.scale[1].clamp(0.01, 100.0),
        mv.scale[2].clamp(0.01, 100.0),
    ];
    crate::pose::local_trs(pos, mv.rot, scl)
}

pub(crate) fn identity_move() -> crate::pose::Mat4 {
    crate::pose::local_trs([0.0; 3], [0.0; 3], [1.0, 1.0, 1.0])
}

/// `UpdateAccessoryMoveFromInfo`: HideHair==1 resets both N_move locals to identity.
pub(crate) fn acc_place_moves(a: &crate::card::AccPart, hide_hair: bool) -> [crate::pose::Mat4; 2] {
    if hide_hair {
        let id = identity_move();
        return [id, id];
    }
    [acc_move_matrix(a.add_move[0]), acc_move_matrix(a.add_move[1])]
}

pub(crate) fn hide_hair_list(entry: &ListEntry) -> bool {
    entry.info("HideHair") == "1"
}

thread_local! {
    pub(crate) static ACC_PLACE: std::cell::RefCell<Option<[crate::pose::Mat4; 2]>> =
        const { std::cell::RefCell::new(None) };
}

pub(crate) struct AccPlaceGuard;

impl Drop for AccPlaceGuard {
    fn drop(&mut self) {
        ACC_PLACE.with(|c| *c.borrow_mut() = None);
    }
}

pub(crate) fn begin_acc_place(moves: [crate::pose::Mat4; 2]) -> AccPlaceGuard {
    ACC_PLACE.with(|c| *c.borrow_mut() = Some(moves));
    AccPlaceGuard
}

pub(crate) fn acc_local_for(card: &CardInfo, property: &str) -> Option<crate::pose::Mat4> {
    let slot = accessory_slot_from_property(property)?;
    let a = card.accessory.get(slot)?;
    let m = acc_move_matrix(a.add_move[0]);
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: acc_local slot={slot} j0={:?} j1={:?}",
            a.add_move[0], a.add_move[1]
        );
    }
    Some(m)
}

pub(crate) fn resolve_acc_parent(card_key: &str, catalog_parent: &str) -> String {
    // ChaControl.ChangeAccessoryNoAsync (ChaControl.cs:10352):
    //   if (parentKey == "") parentKey = list.Parent;
    //   ChangeAccessoryParent(slot, parentKey);
    if std::env::var("OK_ACCPAR").is_ok() {
        eprintln!("ok-extract: ACCPAR card={card_key:?} list={catalog_parent:?}");
    }
    // The card stores the generic `a_n_head` for accessories whose list Parent is
    // the specific `a_n_headtop` (HEXA Hachikuji Headband). `OK_ACCLISTPARENT`
    // prefers the list value; the default uses the card (what ChaControl.cs:10352
    // does, and what the game's mesh dump shows).
    if std::env::var("OK_ACCLISTPARENT").is_ok()
        && card_key == "a_n_head"
        && catalog_parent == "a_n_headtop"
    {
        return catalog_parent.to_string();
    }
    if card_key.is_empty() {
        catalog_parent.to_string()
    } else {
        card_key.to_string()
    }
}

pub(crate) fn parent_for_part(
    card: &CardInfo,
    property: &str,
    entry_parent: &str,
    role: &str,
) -> Option<String> {
    if role.contains("hair") {
        return Some("cf_J_FaceUp_ty".into());
    }
    if role.contains("acc") {
        if let Some(slot) = accessory_slot_from_property(property) {
            if let Some(a) = card.accessory.get(slot) {
                if !a.parent_key.is_empty() {
                    return Some(resolve_acc_parent(&a.parent_key, entry_parent));
                }
            }
        }
        if !entry_parent.is_empty() && entry_parent != "0" && entry_parent != "null" {
            return Some(entry_parent.to_string());
        }
    }
    None
}

pub(crate) fn apply_parent(mesh: &mut ExtractedMesh, m: &crate::pose::Mat4) {
    apply_parent_geometry(mesh, m);
    crate::pose::rebuild_obj(mesh, mesh.positions.clone());
}

/// Live playback consumes positions/normals; OBJ text belongs to export only.
pub(crate) fn apply_parent_geometry(mesh: &mut ExtractedMesh, m: &crate::pose::Mat4) {
    crate::pose::apply_mat(&mut mesh.positions, m);
    for n in &mut mesh.normals {
        *n = crate::pose::rot_normal(m, *n);
    }
}

#[cfg(test)]
mod live_skin_geometry_tests {
    #[test]
    fn live_geometry_matches_export_without_generating_obj() {
        use super::*;
        let mut rotated=crate::pose::ident();
        rotated[0][0]=0.;rotated[0][1]=-1.;rotated[1][0]=1.;rotated[1][1]=0.;
        rotated[0][3]=2.;rotated[2][3]=-0.3;
        let skeleton=crate::pose::Skeleton {names:vec!["a".into(),"b".into()],
            worlds:vec![crate::pose::ident(),rotated],rest:vec![crate::pose::ident();2],
            smr_count:2,tfs:Default::default(),bone_ids:vec![1,2],source_mesh:0};
        let source=ExtractedMesh {name:"live_geometry_fixture".into(),
            positions:vec![[0.2,0.4,0.7],[1.,-0.5,0.3],[0.;3]],
            rest_positions:vec![[0.2,0.4,0.7],[1.,-0.5,0.3],[0.;3]],
            normals:vec![[0.,1.,0.];3],rest_normals:vec![[0.,1.,0.];3],
            weights:vec![[0.75,0.25,0.,0.],[0.,1.,0.,0.],[0.;4]],
            bone_idx:vec![[0,1,0,0];3],bind_poses:vec![crate::pose::ident();2],
            indices:vec![0,1,2],uvs:vec![[0.,0.];3],..Default::default()};
        let mut exported=source.clone();let mut live=source;
        assert!(skin_mesh(&mut exported,&skeleton));assert!(skin_mesh_geometry(&mut live,&skeleton));
        assert_eq!(live.positions,exported.positions);assert_eq!(live.normals,exported.normals);
        assert!(live.obj.is_empty());assert!(!exported.obj.is_empty());
        apply_parent(&mut exported,&rotated);apply_parent_geometry(&mut live,&rotated);
        assert_eq!(live.positions,exported.positions);assert_eq!(live.normals,exported.normals);
        assert!(live.obj.is_empty());
    }
}

/// `SetParent(parent, worldPositionStays: false)` then optional accessory
/// local TRS: world = parent * local * inv(prefab_root) * skinned_prefab.
pub(crate) fn snap_under_parent(
    mesh: &mut ExtractedMesh,
    parent_world: &crate::pose::Mat4,
    part_skel: Option<&crate::pose::Skeleton>,
    part_local: Option<&crate::pose::Mat4>,
) {
    // ChaControl.UpdateAccessoryMoveFromInfo replaces the prefab `N_move` node's
    // local TRS with the card's addMove, so
    //   meshWorld = parentWorld * N_moveParentWorld * addMove * inv(N_moveWorld)
    // (see `ChaControl.cs:2570` + `ChangeAccessoryParent`). `move_nodes` holds
    // (N_moveParentWorld, N_moveWorld) in prefab space. Without this term the
    // authored N_move transform is left in place, so accessories whose prefab
    // ships a non-identity N_move (scale/rotation) render offset/mirrored.
    let move_node = mesh.move_nodes.first().copied().flatten();
    let mut m = *parent_world;
    if let Some(l) = part_local {
        if let Some((n_parent, n_self)) = move_node {
            m = crate::pose::mat_mul_pub(&m, &n_parent);
            m = crate::pose::mat_mul_pub(&m, l);
            m = crate::pose::mat_mul_pub(&m, &crate::pose::mat_inv_pub(&n_self));
        }
    }
    // Non-skinned accessories (MeshFilter) do not get the mesh GameObject's
    // prefab transform from bind poses, so apply it here or they land at the
    // parent origin (an accessory hair arc becomes a halo above the head).
    if !mesh.uses_skin_renderer() {
        if let Some(p) = mesh.prefab_local.as_ref() {
            m = crate::pose::mat_mul_pub(&m, p);
        }
    }
    if std::env::var("OK_SNAPDIAG").is_ok() && !mesh.positions.is_empty() {
        eprintln!(
            "ok-extract: SNAPDIAG {} before p0={:?} parent={:?} local={:?} m={:?} move0={:?} move1={:?} prefab_local={:?}",
            mesh.name,
            mesh.positions[0],
            crate::pose::mat_pos(parent_world),
            part_local.map(|l| crate::pose::mat_pos(l)),
            crate::pose::mat_pos(&m),
            move_node.map(|(p, s)| (crate::pose::mat_pos(&p), crate::pose::mat_pos(&s))),
            mesh.move_nodes.get(1).copied().flatten().map(|(p, s)| (crate::pose::mat_pos(&p), crate::pose::mat_pos(&s))),
            mesh.prefab_local.as_ref().map(crate::pose::mat_pos)
        );
    }
    if let Some(ps) = part_skel.filter(|_|mesh.uses_skin_renderer()) {
        // With `move_nodes` the N_move inverse already removes the prefab
        // transform; otherwise subtract the skeleton root so we don't double the
        // prefab root transform.
        if move_node.is_none() {
            let root = ps.root_world();
            eprintln!(
                "ok-extract: snap {} root pos={:?} scl={:?}",
                mesh.name,
                crate::pose::mat_pos(&root),
                crate::pose::world_scale_pub(&root)
            );
            m = crate::pose::mat_mul_pub(&m, &crate::pose::mat_inv_pub(&root));
        }
    }
    apply_parent(mesh, &m);
    if std::env::var("OK_SNAPDIAG").is_ok() && !mesh.positions.is_empty() {
        eprintln!(
            "ok-extract: SNAPDIAG {} after p0={:?} m={:?}",
            mesh.name,
            mesh.positions[0],
            crate::pose::mat_pos(&m)
        );
    }
}

/// Body-space bake of `ChangeAccessoryParent` + `UpdateAccessoryMoveFromInfo`.
/// Both `N_move` and `N_move2` locals are replaced, then the mesh is left in
/// body space with `parent_bone` cleared (the viewer must not attach again).
pub(crate) fn snap_accessory(
    mesh: &mut ExtractedMesh,
    parent_world: &crate::pose::Mat4,
    part_skel: Option<&crate::pose::Skeleton>,
    locals: &[crate::pose::Mat4; 2],
) {
    let m = accessory_placement_matrix(mesh, parent_world, part_skel, locals);
    if crate::pose::accdiag_matches(&mesh.name) {
        eprintln!("ok-extract: ACCDIAG snap mesh={} attach_matrix={:?} locals={:?} move_nodes={:?} renderer_prefab={:?}",
            mesh.name, m, locals, mesh.move_nodes, mesh.prefab_local);
    }
    apply_parent(mesh, &m);
    mesh.parent_bone = None;
}

pub(crate) fn accessory_placement_matrix(
    mesh: &ExtractedMesh,
    parent_world: &crate::pose::Mat4,
    part_skel: Option<&crate::pose::Skeleton>,
    locals: &[crate::pose::Mat4; 2],
) -> crate::pose::Mat4 {
    let mut chain = crate::pose::ident();
    let mut any = false;
    for j in 0..2 {
        let Some((n_parent, n_self)) = mesh.move_nodes[j] else {
            continue;
        };
        let mut step = n_parent;
        step = crate::pose::mat_mul_pub(&step, &locals[j]);
        step = crate::pose::mat_mul_pub(&step, &crate::pose::mat_inv_pub(&n_self));
        chain = crate::pose::mat_mul_pub(&chain, &step);
        any = true;
    }
    let mut m = if any {
        crate::pose::mat_mul_pub(parent_world, &chain)
    } else {
        // UpdateAccessoryMoveFromInfo writes only existing objAcsMove[j]
        // objects. A prefab without N_move has no card move to apply.
        *parent_world
    };
    // A bundle may contain a Transform hierarchy even for a MeshFilter.
    // Such a rigid renderer has no skin palette to apply its prefab matrix.
    if !mesh.uses_skin_renderer() {
        if let Some(p) = mesh.prefab_local.as_ref() {
            m = crate::pose::mat_mul_pub(&m, p);
        }
    }
    if mesh.uses_skin_renderer() && part_skel.is_some() && !any {
        if let Some(ps) = part_skel {
            m = crate::pose::mat_mul_pub(&m, &crate::pose::mat_inv_pub(&ps.root_world()));
        }
    }
    // The skinned vertex is already in the head bone's world space. Applying
    // the renderer's transform scale here shrinks it a second time. BakeMesh
    // followed by TransformPoint is a diagnostic export, not the GPU skin path.
    m
}

/// UpdateAccessoryMoveFromInfo writes each Transform independently. A second
/// move node may affect only some SMR bones; one post-skin matrix cannot express
/// that hierarchy (the neck ring and hanging tie share one renderer).
pub(crate) fn accessory_skeleton_with_moves(mesh: &ExtractedMesh,
    part_skel: Option<&crate::pose::Skeleton>, locals: &[crate::pose::Mat4; 2])
    -> Option<crate::pose::Skeleton>
{
    if !mesh.uses_skin_renderer() || !mesh.move_nodes.iter().any(Option::is_some) { return None; }
    let mut skel = mesh.smr_skel.clone().or_else(|| part_skel.cloned())?;
    let mut replacements = Vec::new();
    for j in 0..2 {
        let Some((_, authored_world)) = mesh.move_nodes[j] else { continue; };
        let name = if j == 0 { "N_move" } else { "N_move2" };
        let id = skel.tfs.iter().filter(|(_, tf)|
            tf.name.eq_ignore_ascii_case("N_move") || tf.name.eq_ignore_ascii_case("N_move2"))
            .filter(|(id, _)| {
                let world = crate::pose::world_of_id(**id, &skel.tfs);
                world.iter().flatten().zip(authored_world.iter().flatten())
                    .all(|(a,b)| (a-b).abs() < 1e-5)
            }).min_by_key(|(id,tf)| {
                // Split accessory prefabs can alias their original N_move2 as
                // the instantiated item's first move node. Coincident authored
                // matrices do not establish identity: prefer an actual SMR bone
                // ancestor, then the conventional name for ordinary prefabs.
                let referenced = skel.bone_ids.iter().any(|bone| {
                    let mut current = Some(*bone);
                    for _ in 0..skel.tfs.len() {
                        let Some(node) = current else { break; };
                        if node == **id { return true; }
                        current = skel.tfs.get(&node).and_then(|tf| tf.parent);
                    }
                    false
                });
                (!referenced, !tf.name.eq_ignore_ascii_case(name), **id)
            }).map(|(id,_)| *id)?;
        replacements.push((id,j));
    }
    for (id,j) in replacements { crate::pose::replace_local_trs(skel.tfs.get_mut(&id)?, &locals[j]); }
    skel.rebuild_worlds();
    Some(skel)
}

#[cfg(test)]
#[test]
pub(crate) fn accessory_moves_preserve_independent_skin_branches() {
    use crate::pose::{Skeleton,Tf,ident,world_of_id,local_trs};
    let tf = |name: &str, parent| Tf {name:name.into(),parent,sibling_index:None,
        pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
    for nested in [false,true] {
        let mut tfs=std::collections::HashMap::from([
            (1,tf("root",None)),(2,tf("N_move",Some(1))),
            (3,tf("N_move2",Some(if nested {2}else{1}))),
            (4,tf("fixed",Some(2))),(5,tf("hanging",Some(3)))]);
        tfs.get_mut(&3).unwrap().pos=[0.,7.,0.];
        let mut skel=Skeleton {names:vec!["fixed".into(),"hanging".into()],worlds:vec![],
            rest:vec![ident();2],smr_count:2,tfs,bone_ids:vec![4,5],source_mesh:0};
        skel.rebuild_worlds();
        let mut mesh=ExtractedMesh {renderer_skinned:Some(true),smr_skel:Some(skel.clone()),
            positions:vec![[1.,0.,0.];3],normals:vec![[0.,1.,0.];3],
            weights:vec![[1.,0.,0.,0.],[1.,0.,0.,0.],[0.5,0.5,0.,0.]],
            bone_idx:vec![[0,0,0,0],[1,0,0,0],[0,1,0,0]],bind_poses:vec![ident();2],
            move_nodes:[Some((ident(),world_of_id(2,&skel.tfs))),
                Some((world_of_id(if nested{2}else{1},&skel.tfs),world_of_id(3,&skel.tfs)))],
            ..Default::default()};
        let locals=[local_trs([10.,0.,0.],[0.;3],[2.,1.,1.]),
            local_trs([0.,3.,0.],[0.,0.,90.],[1.;3])];
        let moved=accessory_skeleton_with_moves(&mesh,None,&locals).unwrap();
        assert!(skin_mesh(&mut mesh,&moved));
        let expected=if nested {[[12.,0.,0.],[10.,4.,0.],[11.,2.,0.]]}
            else {[[12.,0.,0.],[0.,4.,0.],[6.,2.,0.]]};
        for (a,b) in mesh.positions.iter().zip(expected) {for i in 0..3 {assert!((a[i]-b[i]).abs()<2e-6);}}
        assert_eq!(moved.rest,skel.rest,"card placement must not rewrite mesh bind/rest data");
    }
}

#[cfg(test)]
#[test]
pub(crate) fn accessory_split_move_alias_uses_actual_skin_ancestor() {
    use crate::pose::{Skeleton,Tf,ident,local_trs};
    let tf = |name:&str,parent| Tf {name:name.into(),parent,sibling_index:None,
        pos:[0.;3],rot:[0.,0.,0.,1.],scale:[1.;3]};
    let tfs=std::collections::HashMap::from([(1,tf("root",None)),
        (2,tf("N_move",Some(1))),(3,tf("N_move2",Some(1))),(4,tf("right",Some(3)))]);
    let mut skel=Skeleton {names:vec!["right".into()],worlds:vec![],rest:vec![ident()],
        smr_count:1,tfs,bone_ids:vec![4],source_mesh:0};skel.rebuild_worlds();
    let mesh=ExtractedMesh {renderer_skinned:Some(true),smr_skel:Some(skel),
        move_nodes:[Some((ident(),ident())),None],..Default::default()};
    let moved=accessory_skeleton_with_moves(&mesh,None,&[local_trs([3.,2.,1.],[0.;3],[1.;3]),ident()]).unwrap();
    assert_eq!(crate::pose::mat_pos(&moved.worlds[0]),[3.,2.,1.]);
    assert_eq!(moved.tfs[&2].pos,[0.;3],"an unrelated coincident node must remain authored");
}

#[test]
pub(crate) fn attachment_chain_preserves_body_shear_and_prefers_live_head() {
    use crate::pose::{Skeleton,Tf,ident,trs,mat_mul_pub};
    let tf=|name:&str,parent,pos,rot,scale|Tf{name:name.into(),parent,sibling_index:None,pos,rot,scale};
    let angle=std::f32::consts::FRAC_PI_8;
    let mut body=Skeleton{names:vec!["a_n_neck".into(),"cf_s_head".into()],worlds:vec![],rest:vec![ident();2],smr_count:2,bone_ids:vec![2,3],source_mesh:0,
        tfs:std::collections::HashMap::from([(1,tf("root",None,[0.;3],[0.,0.,0.,1.],[2.,1.,1.])),(2,tf("a_n_neck",Some(1),[0.,1.,0.],[0.,0.,angle.sin(),angle.cos()],[1.;3])),(3,tf("cf_s_head",Some(2),[0.,2.,0.],[0.,0.,0.,1.],[0.8,1.,1.2])),(4,tf("a_n_headside",Some(1),[99.;3],[0.,0.,0.,1.],[1.;3]))])};body.rebuild_worlds();
    let mut head=Skeleton{names:vec!["p_cf_head_bone".into(),"a_n_headside".into()],worlds:vec![],rest:vec![ident();2],smr_count:2,bone_ids:vec![100,101],source_mesh:0,
        tfs:std::collections::HashMap::from([(100,tf("p_cf_head_bone",None,[0.,10.,0.],[0.,0.,0.,1.],[1.;3])),(101,tf("a_n_headside",Some(100),[0.,0.5,0.],[0.,0.,angle.sin(),angle.cos()],[1.;3]))])};head.rebuild_worlds();
    for name in ["a_n_neck","a_n_headside"] {
        let chain=attachment_transform_chain(Some(&body),Some(&head),name,&ident()).unwrap();
        let actual=chain.iter().fold(ident(),|m,tf|mat_mul_pub(&m,&trs(tf.pos,tf.rot,tf.scale)));
        let expected=bone_world(Some(&body),Some(&head),name).unwrap();
        assert!(actual.iter().flatten().zip(expected.iter().flatten()).all(|(a,b)|(a-b).abs()<1e-6));
        assert!(actual[0][3].abs()<99.,"unparented body copy of head socket must not win");
    }
}
