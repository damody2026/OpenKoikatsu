#[test]
#[ignore = "real-card native palette regression; isolated process environment"]
fn crowd_march_native_palette() {
    for (key, value) in [("OK_GPU_SKIN", "0"), ("OK_SKELMESH", "0"), ("OK_NOPOSE", "1")] {
        std::env::set_var(key, value);
    }
    let game = &crate::test_support::game_path("");
    let card = std::env::var_os("OK_CROWD_NATIVE_CARD").map(std::path::PathBuf::from).unwrap_or_else(||game.join("UserData/chara/female/[Card] March_7th_keiyuu.png"));
    let mut session = crate::session::open(game, Some(&card), 0).expect("native card source");
    session.apply_anim_frame(0., 0.);
    session.skin_all();
    eprintln!("CROWD_RIG body={:?} head={:?} full_head={:?}",
        session.body_skel.as_ref().map(|s|(s.names.len(),s.worlds.len(),s.rest.len())),
        session.head_skel.as_ref().map(|s|(s.names.len(),s.worlds.len(),s.rest.len())),
        session.head_full_posed.as_ref().map(|s|(s.names.len(),s.worlds.len(),s.rest.len())));
    let mut failed = Vec::new();
    for (index, part) in session.parts.iter().enumerate() {
        let count = unsafe {crate::ffi::ok_part_skin_palette(&session as *const _ as *mut _, index as i32, std::ptr::null_mut(), std::ptr::null_mut(), 0)};
        if count > 0 || part.rigid_follow_bone.is_some() {continue;}
        let zeros:Vec<_>=part.weights.iter().enumerate().filter(|(_,w)|w.iter().sum::<f32>()<1e-5).map(|(i,_)|i).collect();
        let bad_weights:Vec<_>=part.weights.iter().enumerate().filter(|(_,w)|w.iter().any(|v|!v.is_finite()||*v<0.)).collect();
        let bad_binds:Vec<_>=part.bind_poses.iter().enumerate().filter(|(_,m)|m.iter().flatten().any(|v|!v.is_finite())).map(|(i,_)|i).collect();
        let head_worlds:Vec<_>=session.head_skel.as_ref().unwrap().worlds.iter().enumerate().filter(|(_,m)|m.iter().flatten().any(|v|!v.is_finite())).map(|(i,_)|i).collect();
        let extra=crate::head_onto_body(session.body_skel.as_ref(),session.head_skel.as_ref());
        eprintln!("CROWD_VALIDATION bad_weights={bad_weights:?} bad_binds={bad_binds:?} head_worlds={head_worlds:?} extra={extra:?}");
        eprintln!("CROWD_INVALID index={index} name={} role={} kind={} positions={} weights={} ids={} binds={} max_index={:?} zeros={:?} parent={:?}",
            part.name,part.role,part.skel_kind,part.rest_positions.len(),part.weights.len(),part.bone_idx.len(),part.bind_poses.len(),
            part.bone_idx.iter().flatten().max(),zeros,part.parent_bone);
        failed.push(part.name.clone());
    }
    assert!(failed.is_empty(),"native renderer palettes unavailable: {failed:?}");
}

// Street heroines are posed from an animation snapshot (to_animation_bytes -> from_animation_bytes, then a shared clip),
// the card reader from the session it opened. Same card, same frame: bone worlds and skinned vertices must match.
//   OK_HEAD_PROBE_CARD=<png> cargo test --release --lib head_snapshot_matches_session -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn head_snapshot_matches_session() {
    let card = std::env::var("OK_HEAD_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/fin_elysia.png".into());
    let card_c = std::ffi::CString::new(card.clone()).unwrap();let game = std::ffi::CString::new(std::env::var("OK_HEAD_PROBE_GAME").unwrap_or_default()).unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    let status = unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) };
    assert!(status == 0 && !raw.is_null(), "open failed: {}", unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }.to_string_lossy());
    let a = unsafe { &mut *raw };
    let bytes = a.to_animation_bytes().expect("animation snapshot");
    let mut b = crate::session::OkSession::from_animation_bytes(&bytes).expect("reopen snapshot");
    eprintln!("HEAD_PROBE game_root a={:?} b={:?} clip a={:?} b={:?} shape_face a={} b={} body_shaped a={} b={} head_shaped a={} b={}",
        a.game_root, b.game_root, a.anim_clip, b.anim_clip, a.shape_face.len(), b.shape_face.len(),
        a.body_shaped.is_some(), b.body_shaped.is_some(), a.head_shaped.is_some(), b.head_shaped.is_some());
    // Optional shared clip (the street's): OK_HEAD_PROBE_CLIP=<shared clip bytes file> installs it on both.
    if let Some(clip) = std::env::var_os("OK_HEAD_PROBE_CLIP") {
        let clip = std::fs::read(clip).expect("clip file");
        for s in [&mut *a, &mut b] { assert_eq!(unsafe { crate::ffi::ok_session_install_shared_clip(s as *mut _, clip.as_ptr(), clip.len() as i32) }, 0); }
        eprintln!("HEAD_PROBE clip installed: a.root={:?} b.root={:?}", a.game_root, b.game_root);
    }
    for s in [&mut *a, &mut b] { s.apply_anim_frame(0., 0.); s.skin_all(); }
    let names = ["cf_j_head", "cf_s_head", "p_cf_head_bone", "cf_J_N_FaceRoot", "cf_J_FaceRoot", "cf_J_FaceBase", "cf_J_FaceUp_ty", "cf_J_FaceLow_s", "cf_J_Head_s", "cf_J_FaceUp_tz"];
    let col = |m: &crate::pose::Mat4, i: usize| ((m[0][i]*m[0][i]+m[1][i]*m[1][i]+m[2][i]*m[2][i]) as f64).sqrt();
    for (label, sa, sb) in [("body", &a.body_skel, &b.body_skel), ("head", &a.head_skel, &b.head_skel), ("head_full", &a.head_full_posed, &b.head_full_posed)] {
        let (Some(sa), Some(sb)) = (sa.as_ref(), sb.as_ref()) else { eprintln!("HEAD_PROBE {label}: a={} b={}", sa.is_some(), sb.is_some()); continue; };
        let mut worst = (0f32, String::new());
        for (i, n) in sa.names.iter().enumerate() { let Some(j) = sb.names.iter().position(|m| m == n) else { continue }; if i >= sa.worlds.len() || j >= sb.worlds.len() { continue; }
            let d = (0..4).flat_map(|r| (0..4).map(move |c| (r, c))).map(|(r, c)| (sa.worlds[i][r][c] - sb.worlds[j][r][c]).abs()).fold(0f32, f32::max);
            if d > worst.0 { worst = (d, n.clone()); }
            if names.contains(&n.as_str()) { eprintln!("HEAD_PROBE {label} {n}: a_scale=({:.4},{:.4},{:.4}) b_scale=({:.4},{:.4},{:.4}) maxdiff={d:.6}", col(&sa.worlds[i],0), col(&sa.worlds[i],1), col(&sa.worlds[i],2), col(&sb.worlds[j],0), col(&sb.worlds[j],1), col(&sb.worlds[j],2)); }
        }
        eprintln!("HEAD_PROBE {label}: bones a={} b={} worst={:.6} at {}", sa.names.len(), sb.names.len(), worst.0, worst.1);
    }
    for (pa, pb) in a.parts.iter().zip(&b.parts) {
        if pa.positions.len() != pb.positions.len() || pa.positions.is_empty() { eprintln!("HEAD_PROBE part {} verts a={} b={}", pa.name, pa.positions.len(), pb.positions.len()); continue; }
        let bbox = |p: &[[f32; 3]]| { let mut lo = [f32::MAX; 3]; let mut hi = [f32::MIN; 3]; for v in p { for k in 0..3 { lo[k] = lo[k].min(v[k]); hi[k] = hi[k].max(v[k]); } } [hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]] };
        let d = pa.positions.iter().zip(&pb.positions).map(|(x, y)| ((x[0]-y[0]).powi(2)+(x[1]-y[1]).powi(2)+(x[2]-y[2]).powi(2)).sqrt()).fold(0f32, f32::max);
        let (ea, eb) = (bbox(&pa.positions), bbox(&pb.positions));
        eprintln!("HEAD_PROBE part {:<28} tris={} shader={} kind={} role={:<10} maxdist={:.5} extent_a=({:.4},{:.4},{:.4}) extent_b=({:.4},{:.4},{:.4})", pa.name, pa.indices.len()/3, pa.shader_kind, pa.skel_kind, pa.role, d, ea[0], ea[1], ea[2], eb[0], eb[1], eb[2]);
    }
}

// Same as above, then 3 s of the clip at 60 Hz on both: per part, the vertices in cf_j_head space (A: the opened
// session, as the card reader poses it; B: the animation snapshot the street's pose bank uses).
#[test]
#[ignore = "real-card diagnostic"]
fn head_snapshot_matches_session_over_time() {
    let card = std::env::var("OK_HEAD_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/fin_elysia.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let a = unsafe { &mut *raw };
    let mut b = crate::session::OkSession::from_animation_bytes(&a.to_animation_bytes().unwrap()).unwrap();
    let clip = std::fs::read(std::env::var("OK_HEAD_PROBE_CLIP").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/conversation_neutral.okanim".into())).unwrap();
    for s in [&mut *a, &mut b] { assert_eq!(unsafe { crate::ffi::ok_session_install_shared_clip(s as *mut _, clip.as_ptr(), clip.len() as i32) }, 0); }
    let head_local = |s: &crate::session::OkSession, p: &[f32; 3]| -> [f32; 3] {
        let sk = s.body_skel.as_ref().unwrap(); let i = sk.names.iter().position(|n| n == "cf_j_head").unwrap();
        let inv = crate::pose::mat_inv_pub(&sk.worlds[i]);
        let mut o = [0f32; 3]; for r in 0..3 { o[r] = inv[r][0]*p[0] + inv[r][1]*p[1] + inv[r][2]*p[2] + inv[r][3]; } o };
    for frame in 0..=180 {
        let t = frame as f32 / 60.;
        for s in [&mut *a, &mut b] { s.apply_anim_frame(t, if frame == 0 { 0. } else { 1. / 60. }); s.skin_all(); }
        if ![0, 1, 2, 10, 30, 60, 120, 180].contains(&frame) { continue; }
        for (pa, pb) in a.parts.iter().zip(&b.parts) {
            if !(pa.role == "head" && pa.name == "cf_O_face" || pa.role == "accessory" || pa.role.starts_with("hair")) || pa.positions.is_empty() { continue; }
            let (mut lo_a, mut hi_a, mut lo_b, mut hi_b) = ([f32::MAX; 3], [f32::MIN; 3], [f32::MAX; 3], [f32::MIN; 3]); let mut d = 0f32; let mut n = 0;
            for (x, y) in pa.positions.iter().zip(&pb.positions) {
                let (la, lb) = (head_local(a, x), head_local(&b, y));
                if la[1] < -0.05 { continue; } // only near/above the head joint (head space, metres)
                n += 1; d = d.max(((la[0]-lb[0]).powi(2)+(la[1]-lb[1]).powi(2)+(la[2]-lb[2]).powi(2)).sqrt());
                for k in 0..3 { lo_a[k] = lo_a[k].min(la[k]); hi_a[k] = hi_a[k].max(la[k]); lo_b[k] = lo_b[k].min(lb[k]); hi_b[k] = hi_b[k].max(lb[k]); }
            }
            if n == 0 { continue; }
            eprintln!("HEAD_TIME f={frame:3} {:<24} tris={:<5} n={:<5} maxdist={:.4} a_z=[{:.4},{:.4}] b_z=[{:.4},{:.4}] a_x=[{:.4},{:.4}] b_x=[{:.4},{:.4}] a_y=[{:.4},{:.4}] b_y=[{:.4},{:.4}]",
                pa.name, pa.indices.len()/3, n, d, lo_a[2], hi_a[2], lo_b[2], hi_b[2], lo_a[0], hi_a[0], lo_b[0], hi_b[0], lo_a[1], hi_a[1], lo_b[1], hi_b[1]);
        }
    }
}

// A card's T-pose against a game mesh dump (worldBounds per renderer, bones' world positions):
//   OK_DUMP_PROBE_CARD=<png> cargo test --release --lib card_tpose_extents -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn card_tpose_extents() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new(std::env::var("OK_DUMP_PROBE_GAME").unwrap_or_default()).unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    let status = unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) };
    assert!(status == 0 && !raw.is_null(), "open failed: {}", unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }.to_string_lossy());
    let s = unsafe { &mut *raw };
    s.apply_anim_frame(0., 0.); s.skin_all();
    for p in &s.parts {
        if p.positions.is_empty() { continue; }
        let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
        for v in &p.positions { for k in 0..3 { lo[k] = lo[k].min(v[k]); hi[k] = hi[k].max(v[k]); } }
        eprintln!("PART {:<32} role={:<10} kind={} size=({:.4},{:.4},{:.4}) center=({:.4},{:.4},{:.4})", p.name, p.role, p.skel_kind,
            hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2], (hi[0]+lo[0])/2., (hi[1]+lo[1])/2., (hi[2]+lo[2])/2.);
    }
    let col = |m: &crate::pose::Mat4, i: usize| ((m[0][i]*m[0][i]+m[1][i]*m[1][i]+m[2][i]*m[2][i]) as f64).sqrt();
    let filter = std::env::var("OK_DUMP_PROBE_BONES").unwrap_or_else(|_| "cf_s_head,cf_j_head,cf_J_FaceRoot,cf_J_FaceBase,cf_J_FaceUp_ty,cf_J_FaceUp_tz,cf_J_FaceLow_sx,cf_J_Chin_Base,cf_J_ChinLow,cf_J_Eye_rz_L,cf_J_Mayu_L,cf_J_N_FaceRoot,cf_J_Head_s,cf_J_FaceLow_tz".into());
    let wanted: Vec<&str> = filter.split(',').collect();
    for (label, sk) in [("body", &s.body_skel), ("head", &s.head_skel), ("head_full", &s.head_full_posed)] {
        let Some(sk) = sk.as_ref() else { continue };
        for (i, n) in sk.names.iter().enumerate() {
            if !wanted.contains(&n.as_str()) || i >= sk.worlds.len() { continue; }
            let m = &sk.worlds[i];
            eprintln!("BONE {label:<9} {n:<20} wp=({:.4},{:.4},{:.4}) wscale=({:.4},{:.4},{:.4})", m[0][3], m[1][3], m[2][3], col(m,0), col(m,1), col(m,2));
        }
    }
}

#[test]
#[ignore = "real-card diagnostic"]
fn card_head_attach_scale() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    s.apply_anim_frame(0., 0.); s.skin_all();
    let sc = crate::pose::world_scale_pub;
    let h = s.head_skel.as_ref().unwrap();
    let root = h.root_world();
    let root_name = h.bone_ids.iter().find_map(|id| h.tfs.get(id).filter(|t| t.parent.filter(|p| *p != 0 && *p != *id).is_none()).map(|t| t.name.clone()));
    eprintln!("ATTACH head root {:?} scale={:?} pos={:?}", root_name, sc(&root), crate::pose::mat_pos(&root));
    eprintln!("ATTACH cf_s_head body scale={:?}", sc(&s.body_skel.as_ref().unwrap().world_tf("cf_s_head").unwrap()));
    eprintln!("ATTACH delta scale={:?}", sc(&crate::head_onto_body(s.body_skel.as_ref(), s.head_skel.as_ref())));
    let mut ids: Vec<_> = h.tfs.iter().collect(); ids.sort_by_key(|(id, _)| **id);
    for (id, t) in ids { if t.scale.iter().any(|v| (v - 1.).abs() > 1e-3) || t.parent.filter(|p| *p != 0 && *p != *id).is_none() { eprintln!("TF {:<24} parent={:?} scale={:?}", t.name, t.parent.and_then(|p| h.tfs.get(&p)).map(|p| p.name.clone()), t.scale); } }
}

#[test]
#[ignore = "real-card diagnostic"]
fn card_face_bind() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    for p in s.parts.iter().filter(|p| p.name == "cf_O_face" || p.name == "cf_Ohitomi_L") {
        let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
        for v in &p.rest_positions { for k in 0..3 { lo[k] = lo[k].min(v[k]); hi[k] = hi[k].max(v[k]); } }
        eprintln!("BIND {} rest size=({:.4},{:.4},{:.4}) n={} binds={}", p.name, hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2], p.rest_positions.len(), p.bind_poses.len());
        for b in p.bind_poses.iter().take(3) { eprintln!("BIND   scale={:?} pos={:?}", crate::pose::world_scale_pub(b), crate::pose::mat_pos(b)); }
    }
}

// Per-vertex: the opened card's T-pose part against the game dump's world OBJ (same vertex order).
//   OK_DUMP_PROBE_OBJ=<dump obj> OK_DUMP_PROBE_PART=cf_O_face
#[test]
#[ignore = "real-card diagnostic"]
fn card_part_vs_dump_obj() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let obj = std::env::var("OK_DUMP_PROBE_OBJ").unwrap_or_else(|_| crate::test_support::game_path("/mesh_dump/Jeanne V1.11.png/outfit0/mesh_3_cf_O_face.obj").to_string_lossy().into_owned());
    let name = std::env::var("OK_DUMP_PROBE_PART").unwrap_or_else(|_| "cf_O_face".into());
    let game: Vec<[f32; 3]> = std::fs::read_to_string(&obj).unwrap().lines().filter_map(|l| l.strip_prefix("v ")).map(|l| { let v: Vec<f32> = l.split_whitespace().map(|x| x.parse().unwrap()).collect(); [v[0], v[1], v[2]] }).collect();
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    s.apply_anim_frame(0., 0.); s.skin_all();
    let p = s.parts.iter().find(|p| p.name == name).expect("part");
    eprintln!("VS {} ours={} game={}", name, p.positions.len(), game.len());
    // OBJ x may be mirrored (right-handed export): pick the sign that fits.
    let err_for = |sx: f32| p.positions.iter().zip(&game).map(|(a, g)| ((a[0]-sx*g[0]).powi(2)+(a[1]-g[1]).powi(2)+(a[2]-g[2]).powi(2)).sqrt()).collect::<Vec<f32>>();
    let (e1, e2) = (err_for(1.), err_for(-1.));
    let (errs, sx) = if e1.iter().sum::<f32>() <= e2.iter().sum::<f32>() { (e1, 1.) } else { (e2, -1.) };
    let mut sorted = errs.clone(); sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
    eprintln!("VS sign={sx} median={:.5} p90={:.5} max={:.5}", sorted[sorted.len()/2], sorted[sorted.len()*9/10], sorted[sorted.len()-1]);
    let sk = s.head_skel.as_ref().unwrap();
    let mut by_bone: std::collections::BTreeMap<String, (f32, usize)> = Default::default();
    for (i, e) in errs.iter().enumerate() {
        let (w, b) = (&p.weights[i], &p.bone_idx[i]);
        let k = (0..4).max_by(|x, y| w[*x].partial_cmp(&w[*y]).unwrap()).unwrap();
        let n = sk.names.get(b[k] as usize).cloned().unwrap_or_default();
        let ent = by_bone.entry(n).or_default(); ent.0 += e; ent.1 += 1;
    }
    for (n, (sum, c)) in &by_bone { eprintln!("VS bone {n:<24} verts={c:<5} mean_err={:.5}", sum / *c as f32); }
    let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
    for v in &game { for k in 0..3 { lo[k] = lo[k].min(v[k]); hi[k] = hi[k].max(v[k]); } }
    eprintln!("VS game obj size=({:.4},{:.4},{:.4})", hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
}

// GPU palette (ok_part_skin_palette, what the reader's GPU skinning draws) against the CPU skinned positions, per part.
#[test]
#[ignore = "real-card diagnostic"]
fn card_gpu_palette_vs_cpu() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    s.apply_anim_frame(0., 0.); s.skin_all();
    for (index, part) in s.parts.iter().enumerate() {
        let count = unsafe { crate::ffi::ok_part_skin_palette(raw, index as i32, std::ptr::null_mut(), std::ptr::null_mut(), 0) };
        if count <= 0 { eprintln!("GPU {:<28} kind={} no palette ({count})", part.name, part.skel_kind); continue; }
        let mut m = vec![0f32; count as usize * 16];
        let mut view = std::mem::MaybeUninit::<crate::ffi::OkSkinPaletteView>::uninit();
        assert_eq!(unsafe { crate::ffi::ok_part_skin_palette(raw, index as i32, view.as_mut_ptr(), m.as_mut_ptr(), count) }, count);
        let v = unsafe { view.assume_init() };
        let n = v.n_verts as usize;
        let pos = unsafe { std::slice::from_raw_parts(v.positions, n * 3) };
        let w = unsafe { std::slice::from_raw_parts(v.weights, n * 4) };
        let b = unsafe { std::slice::from_raw_parts(v.bone_idx, n * 4) };
        let mut worst = 0f32; let mut sum = 0f32;
        let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
        for i in 0..n {
            let mut o = [0f32; 3];
            let ws: f32 = (0..4).map(|k| w[i*4+k]).sum();
            let infl: Vec<(usize, f32)> = if ws.abs() < 1e-5 { vec![(count as usize - 1, 1.)] } else { (0..4).map(|k| (b[i*4+k] as usize, w[i*4+k])).collect() };
            for (bi, wk) in infl { if wk == 0. { continue; } let mm = &m[bi*16..bi*16+16];
                for r in 0..3 { o[r] += wk * (mm[r*4]*pos[i*3] + mm[r*4+1]*pos[i*3+1] + mm[r*4+2]*pos[i*3+2] + mm[r*4+3]); } }
            for k in 0..3 { lo[k] = lo[k].min(o[k]); hi[k] = hi[k].max(o[k]); }
            if let Some(c) = part.positions.get(i) { let d = ((o[0]-c[0]).powi(2)+(o[1]-c[1]).powi(2)+(o[2]-c[2]).powi(2)).sqrt(); worst = worst.max(d); sum += d; }
        }
        eprintln!("GPU {:<28} kind={} n={n} cpu_n={} mean={:.5} max={:.5} gpu_size=({:.4},{:.4},{:.4}) gpu_center=({:.4},{:.4},{:.4})", part.name, part.skel_kind, part.positions.len(), sum / n as f32, worst,
            hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2], (hi[0]+lo[0])/2., (hi[1]+lo[1])/2., (hi[2]+lo[2])/2.);
    }
}

#[test]
#[ignore = "real-card diagnostic"]
fn card_part_materials() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let out = std::env::var("OK_DUMP_PROBE_OUT").ok();
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &*raw };
    for p in &s.parts {
        let shader = p.source_material.as_ref().map(|m| m.shader.clone()).unwrap_or_default();
        eprintln!("MAT {:<28} role={:<12} shader={shader} textures={:?} item={:?}", p.name, p.role, p.shader_textures.keys().collect::<Vec<_>>(),
            p.item_draw.as_ref().map(|d| (d.color_count, d.colors, d.color_mask.as_ref().map(|m| m.len()), d.color_mask_st)));
        if let Some(dir) = &out { for (k, v) in &p.shader_textures { let _ = std::fs::write(format!("{dir}/{}{k}.png", p.name), v); }
            if let Some(m) = p.item_draw.as_ref().and_then(|d| d.color_mask.as_ref()) { let _ = std::fs::write(format!("{dir}/{}_item_colormask.png", p.name), m); } }
    }
}

// Scalp vs hair cap: for face vertices above the eyes, the nearest hair vertex, measured along the ray from the head
// centre (positive: the scalp is outside the hair surface there).
#[test]
#[ignore = "real-card diagnostic"]
fn card_scalp_vs_hair() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    s.apply_anim_frame(0., 0.); s.skin_all();
    let face = s.parts.iter().find(|p| p.name == "cf_O_face").unwrap();
    let hair: Vec<[f32; 3]> = s.parts.iter().filter(|p| p.role.starts_with("hair") || p.name.starts_with("hair")).flat_map(|p| p.positions.iter().copied()).collect();
    let centre = [0f32, 1.36, -0.01];
    let mut buckets = [0usize; 6]; let mut out = Vec::new();
    for v in &face.positions {
        if v[1] < 1.40 { continue; }
        let d = [v[0]-centre[0], v[1]-centre[1], v[2]-centre[2]]; let r = (d[0]*d[0]+d[1]*d[1]+d[2]*d[2]).sqrt(); let dir = [d[0]/r, d[1]/r, d[2]/r];
        // hair vertices within 6 mm of this ray: their radius along it
        let mut best: Option<f32> = None;
        for h in &hair { let e = [h[0]-centre[0], h[1]-centre[1], h[2]-centre[2]]; let t = e[0]*dir[0]+e[1]*dir[1]+e[2]*dir[2]; if t <= 0. { continue; }
            let off = ((e[0]-t*dir[0]).powi(2)+(e[1]-t*dir[1]).powi(2)+(e[2]-t*dir[2]).powi(2)).sqrt(); if off < 0.006 { best = Some(best.map_or(t, |b: f32| b.min(t))); } }
        let Some(inner) = best else { buckets[5] += 1; continue };
        let gap = inner - r; // >0: hair outside the scalp
        let k = if gap < 0. { 0 } else if gap < 0.0005 { 1 } else if gap < 0.001 { 2 } else if gap < 0.002 { 3 } else { 4 };
        buckets[k] += 1; if gap < 0.001 { out.push((gap, *v)); }
    }
    eprintln!("SCALP verts above y=1.40: poke_out={} gap<0.5mm={} <1mm={} <2mm={} >=2mm={} no_hair_on_ray={}", buckets[0], buckets[1], buckets[2], buckets[3], buckets[4], buckets[5]);
    out.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());
    for (g, v) in out.iter().take(12) { eprintln!("SCALP gap={:.5} at ({:.4},{:.4},{:.4})", g, v[0], v[1], v[2]); }
}

// Where the part's vertices differ from a (scale-corrected) dump OBJ: error by region above / below a height.
#[test]
#[ignore = "real-card diagnostic"]
fn card_part_error_regions() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let obj = std::env::var("OK_DUMP_PROBE_OBJ").unwrap();
    let name = std::env::var("OK_DUMP_PROBE_PART").unwrap();
    let game: Vec<[f32; 3]> = std::fs::read_to_string(&obj).unwrap().lines().filter_map(|l| l.strip_prefix("v ")).map(|l| { let v: Vec<f32> = l.split_whitespace().map(|x| x.parse().unwrap()).collect(); [v[0], v[1], v[2]] }).collect();
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    s.apply_anim_frame(0., 0.); s.skin_all();
    let p = s.parts.iter().find(|p| p.name == name).expect("part");
    // 2 cm cells of the game position: mean error and count.
    let mut cells: std::collections::BTreeMap<(i32, i32, i32), (f32, usize, f32)> = Default::default();
    for (a, g) in p.positions.iter().zip(&game) {
        let e = ((a[0]-g[0]).powi(2)+(a[1]-g[1]).powi(2)+(a[2]-g[2]).powi(2)).sqrt();
        let key = ((g[0]/0.04).floor() as i32, (g[1]/0.04).floor() as i32, (g[2]/0.04).floor() as i32);
        let c = cells.entry(key).or_default(); c.0 += e; c.1 += 1; c.2 = c.2.max(e);
    }
    for ((x, y, z), (sum, n, max)) in &cells { if *y as f32 * 0.04 >= 1.30 && sum / *n as f32 > 0.002 { eprintln!("CELL x={:+.2} y={:.2} z={:+.2} n={n:<5} mean={:.4} max={:.4}", *x as f32*0.04, *y as f32*0.04, *z as f32*0.04, sum / *n as f32, max); } }
    // The worst vertices above the eyes, with their skin bones.
    let sk_names = |i: usize| -> String { let w = &p.weights[i]; let b = &p.bone_idx[i]; (0..4).filter(|k| w[*k] > 0.05).map(|k| format!("{}:{:.2}", b[k], w[k])).collect::<Vec<_>>().join(" ") };
    let mut worst: Vec<(f32, usize)> = p.positions.iter().zip(&game).enumerate().filter(|(_, (_, g))| g[1] > 1.38).map(|(i, (a, g))| (((a[0]-g[0]).powi(2)+(a[1]-g[1]).powi(2)+(a[2]-g[2]).powi(2)).sqrt(), i)).collect();
    worst.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap());
    for (e, i) in worst.iter().take(10) { eprintln!("WORST err={:.4} ours=({:.4},{:.4},{:.4}) game=({:.4},{:.4},{:.4}) bones={}", e, p.positions[*i][0], p.positions[*i][1], p.positions[*i][2], game[*i][0], game[*i][1], game[*i][2], sk_names(*i)); }
}

#[test]
#[ignore = "real-card diagnostic"]
fn card_me_textures() {
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| crate::test_support::game_path("/UserData/chara/female/Jeanne V1.11.png").to_string_lossy().into_owned());
    let c = crate::parse_card(std::path::Path::new(&card)).expect("card");
    for (k, v) in &c.me_tex { eprintln!("METEX {k:?} bytes={}", v.len()); }
    for (k, v) in &c.me_float { eprintln!("MEFLOAT {k:?} = {v:?}"); }
    for (k, v) in &c.me_shader { eprintln!("MESHADER {k:?} = {v:?}"); }
}

#[test]
#[ignore = "real-card diagnostic"]
fn card_face_alpha_mask() {
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &*raw };
    for p in s.parts.iter().filter(|p| p.role == "head" || p.role == "body") {
        let m = p.source_material.as_ref();
        eprintln!("FACEMASK {:<20} role={} kind={} blend={:?} alpha_mask={:?} st={:?} alpha_a={:?} alpha_b={:?} cutoff={:?} shader={:?}", p.name, p.role, p.shader_kind, p.blend, p.alpha_mask.as_ref().map(|m| m.len()), p.alpha_mask_st,
            m.and_then(|m| m.float("_alpha_a")), m.and_then(|m| m.float("_alpha_b")), m.and_then(|m| m.float("_Cutoff")), m.map(|m| m.shader.clone()));
    }
}

// A part's bounds while the T-pose plays at 60 Hz (dynamic bones settling), against a game dump's
// manualSkinnedWorldBounds (OK_DUMP_PROBE_BOUNDS="sx,sy,sz,cx,cy,cz").
//   OK_DUMP_PROBE_PART=hair.002_0.001 cargo test --release --lib card_part_settle -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn card_part_settle() {
    std::env::set_var("OK_NOPOSE", "1");
    let card = std::env::var("OK_DUMP_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/jeanne.png".into());
    let name = std::env::var("OK_DUMP_PROBE_PART").unwrap_or_else(|_| "hair.002_0.001".into());
    let card_c = std::ffi::CString::new(card).unwrap();let gc = std::ffi::CString::new("").unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(gc.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let s = unsafe { &mut *raw };
    if let Ok(b) = std::env::var("OK_DUMP_PROBE_BOUNDS") { eprintln!("SETTLE game manual bounds {b}"); }
    for (what, n) in crate::oklib::active().map(|st| st.missing_list()).unwrap_or_default() { eprintln!("SETTLE library gap x{n}: {what}"); }
    let index = s.parts.iter().position(|p| p.name == name).expect("part");
    eprintln!("SETTLE part {} role={} kind={} runtime_accessory={} dbones={}", name, s.parts[index].role, s.parts[index].skel_kind,
        s.parts[index].runtime_accessory.is_some(), false);
    eprintln!("SETTLE game_root={:?} clip={:?}", s.game_root, s.anim_clip);
    for frame in 0..=180 {
        let dt = if frame == 0 { 0. } else { 1. / 60. };
        if std::env::var_os("OK_SETTLE_DIRECT").is_some() { s.finish_paired_dynamics(dt); } else { s.apply_anim_frame(frame as f32 / 60., dt); }
        s.skin_all();
        if frame % 30 != 0 { continue; }
        if let Some(r) = s.parts[index].runtime_accessory.as_ref() { let (components, particles) = r.dynamic_probe(); let (n, low) = particles.as_ref().map_or((0, f32::MAX), |v| (v.len(), v.iter().map(|q| q.0[1]).fold(f32::MAX, f32::min))); eprintln!("SETTLE f={frame:3} dynamic components={components} particles={n} lowest_particle_y={low:.4}"); }
        let p = &s.parts[index];
        let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
        for v in &p.positions { for k in 0..3 { lo[k] = lo[k].min(v[k]); hi[k] = hi[k].max(v[k]); } }
        eprintln!("SETTLE f={frame:3} size=({:.4},{:.4},{:.4}) center=({:.4},{:.4},{:.4}) min_y={:.4} min_z={:.4}", hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2],
            (hi[0]+lo[0])/2., (hi[1]+lo[1])/2., (hi[2]+lo[2])/2., lo[1], lo[2]);
    }
}

#[test]
#[ignore = "diagnostic: runtime palette slots per street heroine (pose key budget, R3-10)"]
fn heroine_palette_slots() {
    let dir = std::path::Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines");
    let only = std::env::var("PROBE_CARD").ok();
    for entry in std::fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if only.as_deref().is_some_and(|o| !path.to_string_lossy().contains(o)) { continue; }
        let s = crate::oklib::open_test_card(&path, 0).unwrap();
        let (mut bones, mut unweighted, mut signed) = (0usize, 0usize, 0usize);
        let mut worst: Vec<(usize, String)> = Vec::new();
        for p in &s.parts {
            if !p.render_visible { continue; }
            let mut set = std::collections::HashSet::new();
            let (mut u, mut g) = (0, 0);
            for (w, b) in p.weights.iter().zip(&p.bone_idx) {
                let sum: f32 = w.iter().sum();
                if w.iter().any(|x| *x <= -1e-6) { g += 1; } else if sum <= 1e-5 { u += 1; } else { for k in 0..4 { if w[k] >= 1e-6 { set.insert(b[k]); } } }
            }
            bones += set.len(); unweighted += u; signed += g;
            worst.push((set.len() + u + g, format!("{} bones={} unweighted={u} signed={g} verts={}", p.name, set.len(), p.weights.len())));
        }
        worst.sort_by(|a, b| b.0.cmp(&a.0));
        let slots = bones + unweighted + signed;
        eprintln!("PALETTE {} parts={} slots={slots} (bones={bones} unweighted={unweighted} signed={signed}) key MB per 10 s clip={:.0}", path.file_name().unwrap().to_string_lossy(), s.parts.len(), (slots * 3 + 200) as f64 * 601.0 * 3.0 * 16.0 / 1048576.0);
        for (_, w) in worst.iter().take(4) { eprintln!("   {w}"); }
    }
}

// Street heroines in the crowd are posed by the animation snapshot (the pose bank), the card reader and the talk
// model by the opened session. Same clip, 3 s at 60 Hz: the bones and the parts (world, metres) must agree.
//   OK_BODY_PROBE_CARD=<png> OK_BODY_PROBE_CLIP=<okanim> cargo test --release --lib body_snapshot_matches_session_over_time -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn body_snapshot_matches_session_over_time() {
    let card = std::env::var("OK_BODY_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/raiden_shogun.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new(std::env::var("OK_BODY_PROBE_GAME").unwrap_or_default()).unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let a = unsafe { &mut *raw };
    let mut b = crate::session::OkSession::from_animation_bytes(&a.to_animation_bytes().unwrap()).unwrap();
    let clip = std::fs::read(std::env::var("OK_BODY_PROBE_CLIP").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim".into())).unwrap();
    for s in [&mut *a, &mut b] { assert_eq!(unsafe { crate::ffi::ok_session_install_shared_clip(s as *mut _, clip.as_ptr(), clip.len() as i32) }, 0); }
    for frame in 0..=180 {
        let t = frame as f32 / 60.;
        for s in [&mut *a, &mut b] { s.apply_anim_frame(t, if frame == 0 { 0. } else { 1. / 60. }); s.skin_all(); }
        if ![0, 1, 10, 60, 180].contains(&frame) { continue; }
        let (sa, sb) = (a.body_skel.as_ref().unwrap(), b.body_skel.as_ref().unwrap());
        let mut diffs: Vec<(f32, String)> = Vec::new();
        for (i, n) in sa.names.iter().enumerate() {
            let Some(j) = sb.names.iter().position(|m| m == n) else { continue };
            if i >= sa.worlds.len() || j >= sb.worlds.len() { continue; }
            let d = (0..4).flat_map(|r| (0..4).map(move |c| (r, c))).map(|(r, c)| (sa.worlds[i][r][c] - sb.worlds[j][r][c]).abs()).fold(0f32, f32::max);
            if d > 1e-4 { diffs.push((d, n.clone())); }
        }
        diffs.sort_by(|x, y| y.0.total_cmp(&x.0));
        eprintln!("BODY_TIME f={frame:3} bones differing={} top={:?}", diffs.len(), &diffs[..diffs.len().min(8)]);
        for (pa, pb) in a.parts.iter().zip(&b.parts) {
            if pa.positions.len() != pb.positions.len() || pa.positions.is_empty() { continue; }
            let d = pa.positions.iter().zip(&pb.positions).map(|(x, y)| ((x[0]-y[0]).powi(2)+(x[1]-y[1]).powi(2)+(x[2]-y[2]).powi(2)).sqrt()).fold(0f32, f32::max);
            if d > 1e-4 { eprintln!("BODY_TIME f={frame:3} part {:<32} role={:<14} maxdist={d:.4}", pa.name, pa.role); }
        }
    }
}

// The bust bones' world scale with the card's shape, then after a shared clip (the street's): a clip that keys the
// bust bones must not undo the card's breast size.
//   OK_BODY_PROBE_CARD=<png> OK_BODY_PROBE_CLIP=<okanim> cargo test --release --lib bust_shape_survives_shared_clip -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn bust_shape_survives_shared_clip() {
    let card = std::env::var("OK_BODY_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/raiden_shogun.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new(std::env::var("OK_BODY_PROBE_GAME").unwrap_or_default()).unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    assert_eq!(unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) }, 0);
    let a = unsafe { &mut *raw };
    let col = |m: &crate::pose::Mat4, i: usize| ((m[0][i]*m[0][i]+m[1][i]*m[1][i]+m[2][i]*m[2][i]) as f64).sqrt();
    let report = |label: &str, s: &crate::session::OkSession| {
        let sk = s.body_skel.as_ref().unwrap();
        for (i, n) in sk.names.iter().enumerate() {
            if !(n.contains("bust") || n.contains("bnip")) || !n.ends_with("_L") && !n.contains("bust00") { continue; }
            let m = &sk.worlds[i];
            eprintln!("BUST {label:<6} {n:<16} scale=({:.3},{:.3},{:.3}) pos=({:.4},{:.4},{:.4})", col(m,0), col(m,1), col(m,2), m[0][3], m[1][3], m[2][3]);
        }
        for p in s.parts.iter().filter(|p| p.role == "body" || p.role.starts_with("genital") || p.name.contains("body")) { eprintln!("BUST {label:<6} part {} role={} verts={} binds={} rest_front={:.4} h_rest={:016x} h_w={:016x} h_bind={:016x} h_idx={:016x}", p.name, p.role, p.rest_positions.len(), p.bind_poses.len(),
                p.rest_positions.iter().filter(|v| v[1] > 1.0 && v[1] < 1.4).map(|v| v[2]).fold(f32::MIN, f32::max),
                { let mut x = 0u64; for v in &p.rest_positions { for c in v { x = x.wrapping_mul(31).wrapping_add(c.to_bits() as u64); } } x },
                { let mut x = 0u64; for v in &p.weights { for c in v { x = x.wrapping_mul(31).wrapping_add(c.to_bits() as u64); } } x },
                { let mut x = 0u64; for m in &p.bind_poses { for r in m { for c in r { x = x.wrapping_mul(31).wrapping_add(c.to_bits() as u64); } } } x },
                { let mut x = 0u64; for v in &p.bone_idx { for c in v { x = x.wrapping_mul(31).wrapping_add(*c as u64); } } x }); }
        if let Some(body) = s.parts.iter().find(|p| p.role == "body") {
            let front = body.positions.iter().filter(|v| v[1] > 1.0 && v[1] < 1.4).map(|v| v[2]).fold(f32::MIN, f32::max);
            eprintln!("BUST {label:<6} body front z={front:.4}");
        }
    };
    a.apply_anim_frame(0., 0.); a.skin_all(); report("native", a);
    let clip = std::fs::read(std::env::var("OK_BODY_PROBE_CLIP").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/walk_01.okanim".into())).unwrap();
    assert_eq!(unsafe { crate::ffi::ok_session_install_shared_clip(a as *mut _, clip.as_ptr(), clip.len() as i32) }, 0);
    a.apply_anim_frame(0., 0.); a.skin_all(); report("shared", a);
}

// The open-cache key of a card (which <key>.okpreview in %LOCALAPPDATA%\OpenKoikatsu\oklib-cache it reads).
//   OK_BODY_PROBE_CARD=<png> cargo test --release --lib print_session_cache_key -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn print_session_cache_key() {
    let card = std::env::var("OK_BODY_PROBE_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/raiden_shogun.png".into());
    let lib = std::path::PathBuf::from("C:/portable/OpenKoikatsu/Content/IndependentResources/base_v1");
    for outfit in 0..3 { eprintln!("SESSION_KEY outfit={outfit} key={:?}", crate::oklib::session_cache_key(&lib, std::path::Path::new(&card), outfit)); }
}

// The street opens its cards on several threads at once (empty game root, the base library from OK_BASE_LIBRARY).
// Each card must open as it does alone: the same parts, the same body.
//   OK_BASE_LIBRARY=<base_v1> OK_NOCACHE=1 cargo test --release --lib parallel_opens_match_single -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn parallel_opens_match_single() {
    let dir = std::path::PathBuf::from("C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines");
    let cards: Vec<String> = ["jeanne_alter", "raiden_shogun", "march_7th", "keqing", "hu_tao", "hayase_yuuka", "jeanne", "alice", "megumin", "encore"]
        .iter().map(|n| dir.join(format!("{n}.png")).to_string_lossy().into_owned()).collect();
    let open = |card: &str| -> Vec<(String, usize)> {
        // OK_PROBE_FIRST_GAME: the first card opens with this game root (the street opens its galgame portrait and
        // high cards through FOkSession with the Root of DefaultGame.ini); every other open has an empty root.
        let root = if card == std::env::var("OK_PROBE_FIRST").unwrap_or_default() { std::env::var("OK_PROBE_FIRST_GAME").unwrap_or_default() } else { String::new() };
        let card_c = std::ffi::CString::new(card).unwrap(); let game = std::ffi::CString::new(root).unwrap();
        let mut raw: *mut crate::session::OkSession = std::ptr::null_mut(); let mut err = [0i8; 2048];
        let status = unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) };
        assert!(status == 0 && !raw.is_null(), "{card}: {}", unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }.to_string_lossy());
        let s = unsafe { Box::from_raw(raw) };
        s.parts.iter().map(|p| (p.name.clone(), p.rest_positions.len())).collect()
    };
    // The street opens the male card first (OK_PROBE_FIRST, e.g. output/StreetSocialAPNG/Male01.png).
    if let Ok(first) = std::env::var("OK_PROBE_FIRST") { let parts = open(&first); eprintln!("PARALLEL first {first}: {} parts", parts.len()); }
    let alone: Vec<_> = cards.iter().map(|c| open(c)).collect();
    for (i, card) in cards.iter().enumerate() { eprintln!("PARALLEL alone {card}: body {:?}", alone[i].iter().filter(|p| p.0.contains("body")).collect::<Vec<_>>()); }
    for round in 0..3 {
        let together: Vec<_> = std::thread::scope(|scope| {
            let handles: Vec<_> = cards.iter().map(|c| scope.spawn(move || open(c))).collect();
            handles.into_iter().map(|h| h.join().unwrap()).collect()
        });
        for (i, card) in cards.iter().enumerate() {
            if together[i] != alone[i] {
                let body = |v: &Vec<(String, usize)>| v.iter().filter(|p| p.0.contains("body")).cloned().collect::<Vec<_>>();
                eprintln!("PARALLEL round {round} {card}: parts alone={} together={} body alone={:?} together={:?}",
                    alone[i].len(), together[i].len(), body(&alone[i]), body(&together[i]));
            }
        }
    }
    eprintln!("PARALLEL done");
}

// R3-265: a street heroine's loop as the pose bank bakes it (animation snapshot, shared clip, one loop first, 60 Hz):
// hair/accessory vertices in head space, and every part's palette translations (what the GPU keys record), counting
// the frames whose motion turns back on the previous one (the bank's "Runtime pose jitter"). OK_JITTER_DT0=1 samples
// without dynamics (what is left comes from the clip itself). Run with the reader's env (OK_GPU_SKIN=1 OK_HEAD_GPUSKIN=1
// OK_SKELMESH=1 OK_NOPOSE=1 OK_UE_ANIM=1) so the parts match the game's.
//   OK_JITTER_GAME=C:/portable/OpenKoikatsu/Content/IndependentResources/base_v1 [OK_JITTER_CARD=<png>] [OK_JITTER_CLIP=<okanim>]
//   cargo test --release --lib street_bake_jitter -- --ignored --nocapture
#[test]
#[ignore = "real-card diagnostic"]
fn street_bake_jitter() {
    let card = std::env::var("OK_JITTER_CARD").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/CharacterCards/StreetHeroines/march_7th.png".into());
    let card_c = std::ffi::CString::new(card).unwrap();let game = std::ffi::CString::new(std::env::var("OK_JITTER_GAME").unwrap_or_default()).unwrap();
    let mut raw: *mut crate::session::OkSession = std::ptr::null_mut();let mut err = [0i8; 2048];
    let status = unsafe { crate::ffi::session_open(game.as_ptr(), card_c.as_ptr(), 0, &mut raw, err.as_mut_ptr(), err.len() as i32) };
    assert!(status == 0 && !raw.is_null(), "open failed: {}", unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }.to_string_lossy());
    let a = unsafe { &mut *raw };
    let mut b = crate::session::OkSession::from_animation_bytes(&a.to_animation_bytes().unwrap()).unwrap();
    let clip_path = std::env::var("OK_JITTER_CLIP").unwrap_or_else(|_| "C:/portable/OpenKoikatsu/Content/IndependentResources/RuntimeAnimations/conversation_wait_04.okanim".into());
    let clip = std::fs::read(clip_path).unwrap();
    assert_eq!(unsafe { crate::ffi::ok_session_install_shared_clip(&mut b as *mut _, clip.as_ptr(), clip.len() as i32) }, 0);
    let frames = 240usize;
    let mut tracks: Vec<Vec<Vec<[f32; 3]>>> = Vec::new(); // part -> frame -> vertex
    let mut names = Vec::new();
    let mut palettes: Vec<Vec<Vec<[f32; 3]>>> = Vec::new();
    for frame in 0..frames * 2 {
        let t = (frame % frames) as f32 / 60.;
        b.apply_anim_frame(t, if frame == 0 || std::env::var("OK_JITTER_DT0").is_ok() { 0. } else { 1. / 60. }); b.skin_all();
        if frame < frames { continue; }
        let sk = b.body_skel.as_ref().unwrap(); let i = sk.names.iter().position(|n| n == "cf_j_head").unwrap();
        let inv = crate::pose::mat_inv_pub(&sk.worlds[i]);
        let parts: Vec<_> = b.parts.iter().filter(|p| (p.role == "accessory" || p.role.starts_with("hair")) && !p.positions.is_empty()).collect();
        if tracks.is_empty() { tracks = vec![Vec::new(); parts.len()]; names = parts.iter().map(|p| p.name.clone()).collect(); }
        for (k, p) in parts.iter().enumerate() {
            tracks[k].push(p.positions.iter().map(|x| { let mut o = [0f32; 3]; for r in 0..3 { o[r] = inv[r][0]*x[0] + inv[r][1]*x[1] + inv[r][2]*x[2] + inv[r][3]; } o }).collect());
        }
        // What the pose bank records: each part's palette translations (world), as the GPU keys get them.
        if palettes.is_empty() { palettes = vec![Vec::new(); b.parts.len()]; }
        for index in 0..b.parts.len() {
            let mut view: crate::ffi::OkSkinPaletteView = unsafe { std::mem::zeroed() };
            let mut m = vec![0f32; 16 * 4096];
            let n = unsafe { crate::ffi::ok_part_skin_palette(&mut b as *mut _, index as i32, &mut view, m.as_mut_ptr(), 4096) };
            palettes[index].push(if n > 0 { (0..(n as usize).min(4096)).map(|s| [m[s*16+3], m[s*16+7], m[s*16+11]]).collect() } else { Vec::new() });
        }
    }
    for (index, part) in palettes.iter().enumerate() {
        let slots = part.iter().map(|f| f.len()).min().unwrap_or(0);
        for s in 0..slots {
            let (mut rev, mut amp) = (0, 0f32);
            for f in 0..frames - 2 {
                let d0 = [part[f+1][s][0]-part[f][s][0], part[f+1][s][1]-part[f][s][1], part[f+1][s][2]-part[f][s][2]];
                let d1 = [part[f+2][s][0]-part[f+1][s][0], part[f+2][s][1]-part[f+1][s][1], part[f+2][s][2]-part[f+1][s][2]];
                let (l0, l1) = ((d0[0]*d0[0]+d0[1]*d0[1]+d0[2]*d0[2]).sqrt(), (d1[0]*d1[0]+d1[1]*d1[1]+d1[2]*d1[2]).sqrt());
                if l0 > 2e-5 && l1 > 2e-5 && d0[0]*d1[0]+d0[1]*d1[1]+d0[2]*d1[2] < -0.5*l0*l1 { rev += 1; amp = amp.max(l0.min(l1)); }
            }
            if rev * 4 > frames { let bone = if b.parts[index].skel_kind == 1 { b.body_skel.as_ref().and_then(|k| k.names.get(s).cloned()).unwrap_or_default() } else { String::new() }; eprintln!("PALETTE part {index} {} slot {s} {bone}: {rev} reversals, step {amp:.6} m", b.parts[index].name); }
        }
    }
    let sub = |x: [f32; 3], y: [f32; 3]| [x[0]-y[0], x[1]-y[1], x[2]-y[2]];
    let len = |x: [f32; 3]| (x[0]*x[0]+x[1]*x[1]+x[2]*x[2]).sqrt();
    for (k, part) in tracks.iter().enumerate() {
        let verts = part[0].len();
        let (mut worst, mut worst_amp, mut worst_rev_amp) = (0usize, 0f32, 0f32);
        for v in 0..verts {
            let mut rev = 0; let mut amp = 0f32; let mut rev_amp = 0f32;
            for f in 0..frames - 2 {
                let d0 = sub(part[f + 1][v], part[f][v]); let d1 = sub(part[f + 2][v], part[f + 1][v]);
                let (l0, l1) = (len(d0), len(d1)); amp = amp.max(l0);
                if l0 > 2e-5 && l1 > 2e-5 && d0[0]*d1[0]+d0[1]*d1[1]+d0[2]*d1[2] < -0.5 * l0 * l1 { rev += 1; rev_amp = rev_amp.max(l0.min(l1)); }
            }
            if rev > worst || (rev == worst && amp > worst_amp) { worst = rev; worst_amp = amp; worst_rev_amp = rev_amp; }
        }
        eprintln!("JITTER {:<32} verts={:<6} worst_reversals={:<4} of {} max_step={:.5} m reversing_step={:.6} m", names[k], verts, worst, frames, worst_amp, worst_rev_amp);
    }
}
