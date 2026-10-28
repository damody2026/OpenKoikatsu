//! Prefabs of Unity bundles: base bundles, meshes and skeletons from loaded bundles, the prefab graph, maindata mesh queries and banded BC decoding.
use super::*;

pub fn attach_with_hint(meshes: &mut [ExtractedMesh], textures: &[(String, Vec<u8>)], hint: &str) {
    attach_textures(meshes, textures, hint);
    for m in meshes {
        let Some(png) = m.texture_png.as_ref() else {
            continue;
        };
        if let Some(unpacked) = unpack_illusion_albedo(png) {
            m.texture_png = Some(unpacked);
        }
    }
}

pub(crate) fn meshes_and_textures_from_loaded_bundle(
    bundle: &unity_asset_binary::bundle::AssetBundle,
    ingest_tos: bool,
    filter_base: bool,
) -> (Vec<ExtractedMesh>, Vec<(String, Vec<u8>)>, Option<crate::pose::Skeleton>) {
    let ver = version();
    let mut files = Vec::new();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        files.push((file.name.clone(), bytes));
    }
    let mut textures = Vec::new();
    let mut meshes = Vec::new();
    let mut skel = None;
    let mut best = 0usize;
    for (_name, bytes) in &files {
        let Ok(serialized) = parse_serialized_file(bytes.clone()) else {
            continue;
        };
        textures.extend(textures_from_serialized(&serialized, &files, filter_base));
        if ingest_tos {
            for info in &serialized.objects {
                if info.type_id != 90 {
                    continue;
                }
                if let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) {
                    crate::anim::ingest_avatar_tos(&obj);
                }
            }
        }
        for info in serialized.objects_of_type(MESH_CLASS_ID) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else {
                continue;
            };
            let stream = mesh_stream_bytes(&object, &files);
            let raw_name = object.name().unwrap_or_default();
            let ln = raw_name.to_ascii_lowercase();
            let keep = ln == "o_body_a"
                || ln == "cf_o_face"
                || ln.contains("hitomi")
                || ln.contains("eyeline")
                || ln.contains("noseline")
                || ln.contains("tooth")
                || ln.contains("mayuge")
                || ln.contains("tang")
                || ln.contains("kami")
                || ln.contains("hair")
                || ln.starts_with("o_mnp")
                || !filter_base;
            if !keep {
                continue;
            }
            if let Some(mesh) = decoded_from_object(&object, &ver, stream.as_deref(), &serialized.unity_version) {
                let prefer = mesh.name == "o_body_a" || mesh.name == "cf_O_face";
                if prefer {
                    if let Some(s) = crate::pose::skeleton_from_serialized(&serialized, mesh.path_id) {
                        let score = s.bone_ids.len() + 10_000;
                        if score > best {
                            best = score;
                            skel = Some(s);
                        }
                    }
                }
                meshes.push(mesh);
            }
        }
    }
    (meshes, textures, skel)
}

pub(crate) fn meshes_from_loaded_bundle(bundle: &unity_asset_binary::bundle::AssetBundle) -> Vec<ExtractedMesh> {
    let (mut meshes, textures, _) = meshes_and_textures_from_loaded_bundle(bundle, false, false);
    attach_with_hint(&mut meshes, &textures, "");
    meshes
}

pub fn load_base_bundle(
    path: &Path,
) -> (
    Vec<ExtractedMesh>,
    Option<crate::pose::Skeleton>,
    Vec<(String, Vec<u8>)>,
) {
    let Ok(bytes) = crate::gamefs::read(path) else {
        return (Vec::new(), None, Vec::new());
    };
    // The texture hint only depends on the bundle file name.
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    load_base_bundle_from_memory(bytes, &name)
}

/// Same as `load_base_bundle` but for bytes already in memory (e.g. a zipmod
/// override of a vanilla assetbundle). `name` selects the texture hint.
pub fn load_base_bundle_from_memory(
    bytes: Vec<u8>,
    name: &str,
) -> (
    Vec<ExtractedMesh>,
    Option<crate::pose::Skeleton>,
    Vec<(String, Vec<u8>)>,
) {
    if crate::oklib::active().is_none() {
        let Ok(bundle) = load_bundle_unbounded(bytes) else { return (Vec::new(), None, Vec::new()); };
        return load_base_bundle_impl(&bundle, name);
    }
    // Every card reads the same base bundles (oo_base, bo_head): their meshes and textures, once decoded (the textures
    // re-derived from the bundle: ~0.25 s a card), are kept for the process, by content and name like the memo below.
    type Base = (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>, Vec<(String, Vec<u8>)>);
    static DECODED: std::sync::Mutex<Vec<(crate::oklib::Id, String, Arc<Base>)>> = std::sync::Mutex::new(Vec::new());
    let id = crate::oklib::content_id(&bytes);
    if let Some(hit) = DECODED.lock().unwrap_or_else(|e| e.into_inner()).iter().find(|(i, n, _)| *i == id && n == name).map(|e| e.2.clone()) {
        return (*hit).clone();
    }
    let parsed = parse_bundle_cached(bytes.clone());
    let (mut meshes, skel, mut textures) = crate::oklib::memo_bytes(&bytes.clone(), "base_bundle", name, || {
        let Ok(bundle) = load_bundle_unbounded(bytes) else {
            return (Vec::new(), None, Vec::new());
        };
        let (mut m, s, mut t) = load_base_bundle_impl(&bundle, name);
        parsed.strip_mesh_textures(&mut m); parsed.strip_named_textures(&mut t);
        (m, s, t)
    });
    parsed.restore_mesh_textures(&mut meshes); parsed.restore_named_textures(&mut textures);
    let result = (meshes, skel, textures);
    {
        let mut kept = DECODED.lock().unwrap_or_else(|e| e.into_inner());
        if kept.len() >= 4 { kept.remove(0); }
        kept.push((id, name.to_string(), Arc::new(result.clone())));
    }
    result
}

pub(crate) fn load_base_bundle_impl(
    bundle: &unity_asset_binary::bundle::AssetBundle,
    p: &str,
) -> (
    Vec<ExtractedMesh>,
    Option<crate::pose::Skeleton>,
    Vec<(String, Vec<u8>)>,
) {
    let _ = bundle.data_checked();
    let ingest = p.contains("oo_base");
    let filter = ingest || p.contains("bo_head");
    let hint = if ingest {
        "cf_body_00_t"
    } else if p.contains("bo_head") {
        "cf_face"
    } else {
        ""
    };
    let (mut meshes, textures, skel) =
        meshes_and_textures_from_loaded_bundle(bundle, ingest, filter);
    attach_with_hint(&mut meshes, &textures, hint);
    (meshes, skel, textures)
}

pub fn meshes_and_skeleton_from_path(path: &Path) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    let (meshes, skel, _) = load_base_bundle(path);
    (meshes, skel)
}

/// Skeleton (SMR bone list) that drives a specific mesh in a bundle. Used for
/// meshes like the mouth which have their own bone list inside the body bundle.
pub fn skeleton_for_mesh(path: &Path, mesh_name: &str) -> Option<crate::pose::Skeleton> {
    let bytes = crate::gamefs::read(path).ok()?;
    crate::oklib::memo_bytes(&bytes.clone(), "skeleton_for_mesh", mesh_name, || skeleton_for_mesh_unity(path, bytes, mesh_name))
}
pub(crate) fn skeleton_for_mesh_unity(path: &Path, bytes: Vec<u8>, mesh_name: &str) -> Option<crate::pose::Skeleton> {
    let parsed = parse_bundle_ref(&bytes);
    let hit = parsed.meshes.borrow().values().find_map(|m| {
        if m.name.eq_ignore_ascii_case(mesh_name) {
            Some(m.path_id)
        } else {
            None
        }
    });
    if let Some(pid) = hit {
        return parsed.skeleton(pid);
    }
    let _ = path;
    let Ok(bundle) = load_bundle_unbounded(bytes) else {
        return None;
    };
    skeleton_from_bundle(&bundle, mesh_name)
}

/// Same as `skeleton_for_mesh` for an in-memory bundle (zipmod override).
pub fn skeleton_for_mesh_memory(bytes: &[u8], mesh_name: &str) -> Option<crate::pose::Skeleton> {
    crate::oklib::memo_bytes(bytes, "skeleton_for_mesh_memory", mesh_name, || {
        let bundle = load_bundle_unbounded(bytes.to_vec()).ok()?;
        skeleton_from_bundle(&bundle, mesh_name)
    })
}

/// Skeleton (SMR bone list) for a specific mesh object path id. Unlike
/// `skeleton_for_mesh`, the path id is unique per prefab, so this picks the SMR
/// the active prefab actually uses (e.g. the 1-bone iris instead of a 54-bone
/// variant that happens to share the mesh name).
pub fn skeleton_for_mesh_pathid(path: &Path, mesh_path_id: i64) -> Option<crate::pose::Skeleton> {
    let bytes = crate::gamefs::read(path).ok()?;
    parse_bundle_ref(&bytes).skeleton(mesh_path_id)
}

pub(crate) fn skeleton_from_bundle(
    bundle: &unity_asset_binary::bundle::AssetBundle,
    mesh_name: &str,
) -> Option<crate::pose::Skeleton> {
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = parse_serialized_file(bytes) else {
            continue;
        };
        for info in ser.objects_of_type(MESH_CLASS_ID) {
            let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) else {
                continue;
            };
            if obj.name().unwrap_or_default().eq_ignore_ascii_case(mesh_name) {
                if let Some(s) = crate::pose::skeleton_from_serialized(&ser, obj.path_id()) {
                    return Some(s);
                }
            }
        }
    }
    None
}

pub fn meshes_and_textures_from_memory(bytes: Vec<u8>) -> (Vec<ExtractedMesh>, Vec<(String, Vec<u8>)>) {
    let (m, t, _) = meshes_skel_from_memory(bytes);
    (m, t)
}

pub fn meshes_skel_from_memory(
    bytes: Vec<u8>,
) -> (
    Vec<ExtractedMesh>,
    Vec<(String, Vec<u8>)>,
    Option<crate::pose::Skeleton>,
) {
    parse_bundle_ref(&bytes).decode_all()
}

pub(crate) fn meshes_from_bundle_bytes(bytes: Vec<u8>) -> Vec<ExtractedMesh> {
    let Ok(bundle) = load_bundle_unbounded(bytes) else {
        return Vec::new();
    };
    let _ = bundle.data_checked();
    meshes_from_loaded_bundle(&bundle)
}

pub fn meshes_from_bundle_path(path: &Path) -> Vec<ExtractedMesh> {
    let Ok(bundle) = load_bundle(path) else {
        return Vec::new();
    };
    let _ = bundle.data_checked();
    meshes_from_loaded_bundle(&bundle)
}

pub fn meshes_from_memory(bytes: Vec<u8>) -> Vec<ExtractedMesh> {
    meshes_from_bundle_bytes(bytes)
}

pub(crate) fn prefab_name_matches(go: &str, main_data: &str) -> bool {
    let g = go.to_ascii_lowercase();
    let m = main_data.to_ascii_lowercase();
    let want = m.trim_start_matches("p_");
    g == m || g == want
}

pub(crate) fn prefab_name_fuzzy(go: &str, main_data: &str) -> bool {
    let g = go.to_ascii_lowercase();
    let want = main_data.to_ascii_lowercase();
    let want = want.trim_start_matches("p_");
    let g_flat: String = g.chars().filter(|c| c.is_ascii_alphanumeric()).collect();
    if let Some(tok) = want.rsplit(|c: char| c == '_' || c == '/' || c == '\\').next() {
        // Long unique tokens only ("headbandmayoi"). Short ones ("shoes",
        // "00") match half the bundle.
        if tok.len() >= 10 && (g.contains(tok) || g_flat.contains(tok)) {
            return true;
        }
        let tok_flat: String = tok.chars().filter(|c| c.is_ascii_alphanumeric()).collect();
        tok_flat.len() >= 10 && g_flat.contains(&tok_flat)
    } else {
        false
    }
}

/// The named prefab's mesh asset path ids and, per mesh, the renderer component
/// path ids that draw it (a mesh can be shared by several renderers).
pub(crate) fn collect_prefab_mesh_map(
    serialized: &unity_asset_binary::asset::SerializedFile,
    main_data: &str,
) -> (Vec<i64>, std::collections::HashMap<i64, Vec<i64>>) {
    let g = scan_prefab_graph(serialized);
    let (ids, rends, _) = query_prefab_graph(&g, main_data, false);
    (ids, rends)
}

/// AssetBundle.LoadAsset uses m_Container's asset PPtr. A descendant may have
/// the same GameObject name as its prefab, so name/PathID order is insufficient.
pub(crate) fn prefab_asset_roots(serialized:&unity_asset_binary::asset::SerializedFile) -> HashMap<String,Vec<i64>> {
    use unity_asset_core::UnityValue;
    let mut result:HashMap<String,Vec<i64>>=HashMap::new();
    for info in serialized.objects_of_type(142) {
        let Ok(obj)=UnityObject::from_serialized_file(serialized,info) else {continue;};
        let Some(container)=obj.class.properties().get("m_Container") else {continue;};
        for row in uv_arr(container) {
            let pair=match row {
                UnityValue::Array(pair) if pair.len()==2 => Some((&pair[0],&pair[1])),
                UnityValue::Object(pair) => pair.get("first").zip(pair.get("second")),
                _=>None,
            };
            let Some((name,value))=pair else {continue;};
            let Some(name)=name.as_str() else {continue;};
            let normalized=name.replace('\\',"/").to_ascii_lowercase();
            let leaf=normalized.rsplit('/').next().unwrap_or(&normalized);
            let stem=leaf.strip_suffix(".prefab").unwrap_or(leaf);
            let Some(asset)=uv_obj(value,"asset") else {continue;};
            if uv_obj(asset,"m_FileID").and_then(|v|v.as_i64()).unwrap_or(0)!=0 {continue;}
            let Some(id)=uv_pptr(asset).filter(|id|*id!=0) else {continue;};
            if !serialized.objects.iter().any(|o|o.path_id==id && o.type_id==1) {continue;}
            let ids=result.entry(stem.to_string()).or_default();if !ids.contains(&id) {ids.push(id);}
        }
    }
    result
}

#[derive(Default)]
pub(crate) struct PrefabGraph {
    pub(crate) asset_roots: HashMap<String, Vec<i64>>,
    pub(crate) go_name: std::collections::HashMap<i64, String>,
    pub(crate) go_comps: std::collections::HashMap<i64, Vec<i64>>,
    pub(crate) tf_go: std::collections::HashMap<i64, i64>,
    pub(crate) tf_children: std::collections::HashMap<i64, Vec<i64>>,
    pub(crate) tf_parent: std::collections::HashMap<i64, i64>,
    pub(crate) smr_go: std::collections::HashMap<i64, i64>,
    pub(crate) smr_mesh: std::collections::HashMap<i64, i64>,
    pub(crate) go_tf: std::collections::HashMap<i64, i64>,
    pub(crate) go_mr: std::collections::HashMap<i64, i64>,
    /// Renderer component path id -> Behaviour.m_Enabled. Missing means enabled.
    /// The game does not draw disabled renderers; loading them stacks unused
    /// hair/clothing variants on top of the worn ones.
    pub(crate) comp_enabled: std::collections::HashMap<i64, bool>,
    /// Mesh path id -> `externals[FileID-1].path` for renderer meshes stored in
    /// another serialized file (a dependency bundle, CAB reference).
    pub(crate) ext_mesh: std::collections::HashMap<i64, String>,
}

/// A renderer that only casts shadows (Renderer.m_CastShadows == ShadowsOnly): Unity never draws its colour. Studio
/// maps use such boxes over rooms (`p_koi_map14_shadow00`); drawn, they hide the room from a camera above it.
pub(crate) fn shadows_only(renderer: &UnityObject) -> bool {
    renderer.class.properties().get("m_CastShadows").and_then(|v| v.as_i64()) == Some(3)
}

pub(crate) fn scan_prefab_graph(serialized: &unity_asset_binary::asset::SerializedFile) -> PrefabGraph {
    use unity_asset_core::UnityValue;
    let mut go_name: std::collections::HashMap<i64, String> = std::collections::HashMap::new();
    let mut go_comps: std::collections::HashMap<i64, Vec<i64>> = std::collections::HashMap::new();
    let mut tf_go: std::collections::HashMap<i64, i64> = std::collections::HashMap::new();
    let mut tf_children: std::collections::HashMap<i64, Vec<i64>> = std::collections::HashMap::new();
    let mut tf_parent: std::collections::HashMap<i64, i64> = std::collections::HashMap::new();
    let mut smr_go: std::collections::HashMap<i64, i64> = std::collections::HashMap::new();
    let mut smr_mesh: std::collections::HashMap<i64, i64> = std::collections::HashMap::new();
    let mut mr_go: std::collections::HashMap<i64, i64> = std::collections::HashMap::new();
    let mut comp_enabled: std::collections::HashMap<i64, bool> = std::collections::HashMap::new();
    let mut ext_mesh: std::collections::HashMap<i64, String> = std::collections::HashMap::new();
    for info in &serialized.objects {
        // Only these types are read below: reading every object decompressed and parsed each texture and mesh too.
        if !matches!(info.type_id, 1 | 4 | 23 | 33 | 137) { continue; }
        let Ok(obj) = UnityObject::from_serialized_file(serialized, info) else {
            continue;
        };
        match info.type_id {
            1 => {
                go_name.insert(info.path_id, obj.name().unwrap_or_default());
                let mut comps = Vec::new();
                if let Some(UnityValue::Array(arr)) = obj.class.properties().get("m_Component") {
                    for item in arr {
                        match item {
                            UnityValue::Object(o) => {
                                for key in ["component", "second", "data", "m_Component"] {
                                    if let Some(pid) = o.get(key).and_then(pptr_path_id) {
                                        comps.push(pid);
                                    }
                                }
                                if let Some(pid) = pptr_path_id(item) {
                                    comps.push(pid);
                                }
                            }
                            _ => {
                                if let Some(pid) = pptr_path_id(item) {
                                    comps.push(pid);
                                }
                            }
                        }
                    }
                }
                go_comps.insert(info.path_id, comps);
            }
            4 => {
                if let Some(pid) = obj.class.properties().get("m_GameObject").and_then(pptr_path_id) {
                    tf_go.insert(info.path_id, pid);
                }
                if let Some(UnityValue::Array(ch)) = obj.class.properties().get("m_Children") {
                    let kids: Vec<i64> = ch.iter().filter_map(pptr_path_id).collect();
                    tf_children.insert(info.path_id, kids);
                }
                if let Some(pid) = obj.class.properties().get("m_Father").and_then(pptr_path_id) {
                    tf_parent.insert(info.path_id, pid);
                }
            }
            137 | 33 => {
                if let Some(g) = obj.class.properties().get("m_GameObject").and_then(pptr_path_id) {
                    smr_go.insert(info.path_id, g);
                }
                if let Some(m) = obj.class.properties().get("m_Mesh").and_then(pptr_path_id) {
                    smr_mesh.insert(info.path_id, m);
                }
                if let Some(p) = obj.class.properties().get("m_Mesh").and_then(references::PPtr::read).filter(|p| p.file > 0) {
                    match serialized.externals.get(p.file as usize - 1) {
                        Some(e) => { ext_mesh.insert(p.path, e.path.clone()); }
                        None => eprintln!("ok-extract: invalid external Mesh FileID={} PathID={}", p.file, p.path),
                    }
                }
                if info.type_id == 137 {
                    if let Some(v) = obj.class.properties().get("m_Enabled") {
                        comp_enabled.insert(info.path_id, uv_bool(v) && !shadows_only(&obj));
                    }
                }
            }
            23 => {
                if let Some(g) = obj.class.properties().get("m_GameObject").and_then(pptr_path_id) {
                    mr_go.insert(info.path_id, g);
                }
                if let Some(v) = obj.class.properties().get("m_Enabled") {
                    comp_enabled.insert(info.path_id, uv_bool(v) && !shadows_only(&obj));
                }
            }
            _ => {}
        }
    }
    let mut go_tf = std::collections::HashMap::new();
    for (tf, go) in &tf_go {
        go_tf.insert(*go, *tf);
    }
    let mut go_mr = std::collections::HashMap::new();
    for (comp, go) in &mr_go {
        go_mr.insert(*go, *comp);
    }
    PrefabGraph {
        asset_roots: prefab_asset_roots(serialized),
        go_name,
        go_comps,
        tf_go,
        tf_children,
        tf_parent,
        smr_go,
        smr_mesh,
        go_tf,
        go_mr,
        comp_enabled,
        ext_mesh,
    }
}

pub(crate) fn query_prefab_graph(
    g: &PrefabGraph,
    main_data: &str,
    include_disabled: bool,
) -> (
    Vec<i64>,
    std::collections::HashMap<i64, Vec<i64>>,
    std::collections::HashMap<i64, Vec<String>>,
) {
    let want = main_data.trim_start_matches("p_").to_ascii_lowercase();
    if want.is_empty() {
        return (
            Vec::new(),
            std::collections::HashMap::new(),
            std::collections::HashMap::new(),
        );
    }
    let mut roots: Vec<i64> = g.asset_roots.get(&main_data.to_ascii_lowercase()).cloned()
        .unwrap_or_else(||g
        .go_name
        .iter()
        .filter(|(_, n)| prefab_name_matches(n, main_data))
        .map(|(id, _)| *id)
        .collect());
    if roots.is_empty() {
        roots = g
            .go_name
            .iter()
            .filter(|(_, n)| prefab_name_fuzzy(n, main_data))
            .map(|(id, _)| *id)
            .collect();
        if !roots.is_empty() {
            eprintln!(
                "ok-extract: prefab '{main_data}' fuzzy {:?}",
                roots
                    .iter()
                    .filter_map(|id| g.go_name.get(id).cloned())
                    .collect::<Vec<_>>()
            );
        }
    }
    if roots.is_empty() {
        let sample: Vec<String> = g.go_name.values().take(24).cloned().collect();
        eprintln!("ok-extract: prefab '{main_data}' not in bundle gos={sample:?}");
        return (
            Vec::new(),
            std::collections::HashMap::new(),
            std::collections::HashMap::new(),
        );
    }
    let m = main_data.to_ascii_lowercase();
    roots.sort_by(|a, b| {
        let rank = |id: &i64| -> u8 {
            let l = g
                .go_name
                .get(id)
                .map(|n| n.to_ascii_lowercase())
                .unwrap_or_default();
            if l == m {
                0
            } else if l == want {
                1
            } else if l.ends_with("_low") || l.ends_with("_hit") {
                3
            } else {
                2
            }
        };
        rank(a).cmp(&rank(b)).then(a.cmp(b))
    });
    let root = roots[0];
    let mut stack: Vec<i64> = if include_disabled {
        let exact: Vec<i64> = roots.iter().copied().filter(|id| {
            g.go_name.get(id).is_some_and(|name| name.eq_ignore_ascii_case(main_data))
        }).collect();
        if exact.is_empty() { vec![root] } else { exact }
    } else {
        vec![root]
    };
    let mut seen = std::collections::HashSet::new();
    let mut father_children: std::collections::HashMap<i64, Vec<i64>> = std::collections::HashMap::new();
    if include_disabled {
        for (child, father) in &g.tf_parent {
            father_children.entry(*father).or_default().push(*child);
        }
    }
    while let Some(go) = stack.pop() {
        if !seen.insert(go) {
            continue;
        }
        if let Some(tf) = g.go_tf.get(&go) {
            if let Some(kids) = g.tf_children.get(tf) {
                for k in kids {
                    if let Some(child_go) = g.tf_go.get(k) {
                        stack.push(*child_go);
                    }
                }
            }
            if let Some(kids) = father_children.get(tf) {
                for k in kids {
                    if let Some(child_go) = g.tf_go.get(k) {
                        stack.push(*child_go);
                    }
                }
            }
        }
    }
    fn add_mesh(
        mesh_ids: &mut Vec<i64>,
        mesh_renderers: &mut std::collections::HashMap<i64, Vec<i64>>,
        mesh_names: &mut std::collections::HashMap<i64, Vec<String>>,
        mid: i64,
        comp: i64,
        go_name: &str,
    ) {
        if !mesh_ids.contains(&mid) {
            mesh_ids.push(mid);
        }
        let list = mesh_renderers.entry(mid).or_default();
        if !list.contains(&comp) {
            list.push(comp);
        }
        if !go_name.is_empty() {
            let names = mesh_names.entry(mid).or_default();
            if !names.iter().any(|n| n.eq_ignore_ascii_case(go_name)) {
                names.push(go_name.to_string());
            }
        }
    }
    let mut mesh_ids = Vec::new();
    let mut mesh_renderers: std::collections::HashMap<i64, Vec<i64>> =
        std::collections::HashMap::new();
    let mut mesh_names: std::collections::HashMap<i64, Vec<String>> =
        std::collections::HashMap::new();
    let renderer_id = |comp: i64, go: i64| -> i64 { g.go_mr.get(&go).copied().unwrap_or(comp) };
    let renderer_on = |comp: i64, go: i64| -> bool {
        if include_disabled {
            return true;
        }
        if g.comp_enabled.get(&comp) == Some(&false) {
            return false;
        }
        if let Some(mr) = g.go_mr.get(&go) {
            if g.comp_enabled.get(mr) == Some(&false) {
                return false;
            }
        }
        true
    };
    for (comp, go) in &g.smr_go {
        if !seen.contains(go) {
            continue;
        }
        if !renderer_on(*comp, *go) {
            continue;
        }
        if let Some(mid) = g.smr_mesh.get(comp) {
            add_mesh(
                &mut mesh_ids,
                &mut mesh_renderers,
                &mut mesh_names,
                *mid,
                renderer_id(*comp, *go),
                g.go_name.get(go).map(|s| s.as_str()).unwrap_or(""),
            );
        }
    }
    if mesh_ids.is_empty() {
        for go in &seen {
            if let Some(comps) = g.go_comps.get(go) {
                for c in comps {
                    if let Some(mid) = g.smr_mesh.get(c) {
                        add_mesh(
                            &mut mesh_ids,
                            &mut mesh_renderers,
                            &mut mesh_names,
                            *mid,
                            g.go_mr.get(go).copied().unwrap_or(*c),
                            g.go_name.get(go).map(|s| s.as_str()).unwrap_or(""),
                        );
                    }
                }
            }
        }
    }
    if mesh_ids.is_empty() {
        eprintln!(
            "ok-extract: prefab '{main_data}' go={} tree={} smr={} comps={} meshes=0",
            g.go_name.get(&root).map(|s| s.as_str()).unwrap_or("?"),
            seen.len(),
            g.smr_go.len(),
            g.go_comps.values().map(|c| c.len()).sum::<usize>()
        );
    }
    // smr_go and the fallback `seen` set are hash tables. Keep renderer order
    // stable across parses; it also selects which SMR supplies the prefab
    // skeleton when a prefab has several meshes.
    mesh_ids.sort_unstable();
    for renderers in mesh_renderers.values_mut() {
        renderers.sort_unstable();
    }
    for names in mesh_names.values_mut() {
        names.sort_unstable();
    }
    (mesh_ids, mesh_renderers, mesh_names)
}

pub fn meshes_for_maindata(bytes: impl Into<SourceBytes>, main_data: &str) -> Vec<ExtractedMesh> {
    meshes_skel_for_maindata(bytes, main_data).0
}

/// LoadAsset(MainAB, MainData): the named prefab and every SMR under it.
pub fn meshes_skel_for_maindata(
    bytes: impl Into<SourceBytes>,
    main_data: &str,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    meshes_skel_for_maindata_ref(&bytes.into(), main_data)
}

pub fn meshes_skel_for_maindata_ref<B: BundleInput + ?Sized>(
    bytes: &B,
    main_data: &str,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    if main_data.is_empty() || main_data.eq_ignore_ascii_case("p_dummy") {
        return (Vec::new(), None);
    }
    parse_bundle_ref(bytes).meshes_for_maindata(main_data)
}

pub fn meshes_skel_for_maindata_ref_with_category<B: BundleInput + ?Sized>(
    bytes: &B,
    main_data: &str,
    category: i32,
) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
    if main_data.is_empty() || main_data.eq_ignore_ascii_case("p_dummy") {
        return (Vec::new(), None);
    }
    let clothes_category = match category {
        105..=112 | 200..=202 | 210..=212 => Some(category),
        _ => None,
    };
    parse_bundle_ref(bytes).meshes_for_maindata_with_clothes_kind(main_data, false, clothes_category)
}

/// Decode Unity DXT1 as BC1 RGBA, retaining the decoder's BGRA byte order.
/// BC1 (Unity DXT1 = BC1 with alpha), BC3 (DXT5) or BC7 texels decoded in parallel bands of block rows.
/// None when the payload is shorter than the format needs (the single-call paths then report it as before).
pub(crate) fn decode_bc_banded(data: &[u8], w: usize, h: usize, format: unity_asset_decode::texture::TextureFormat) -> Option<image::RgbaImage> {
    use rayon::prelude::*;
    use unity_asset_decode::texture::TextureFormat;
    let block_bytes = if format == TextureFormat::DXT1 { 8 } else { 16 };
    let (blocks_x, blocks_y) = ((w + 3) / 4, (h + 3) / 4);
    let row_bytes = blocks_x * block_bytes;
    if data.len() < row_bytes * blocks_y { return None; }
    let rows_per_band = blocks_y.div_ceil(16).max(1);
    let mut rgba = vec![0u8; w * h * 4];
    let ok = rgba.par_chunks_mut(rows_per_band * 4 * w * 4).enumerate().all(|(band, out)| {
        let y0 = band * rows_per_band * 4;
        let band_h = out.len() / (w * 4);
        let first_row = y0 / 4;
        let src = &data[first_row * row_bytes..(first_row + band_h.div_ceil(4)) * row_bytes];
        let mut texels = vec![0u32; w * band_h];
        let decoded = match format {
            TextureFormat::DXT1 => texture2ddecoder::decode_bc1a(src, w, band_h, &mut texels),
            TextureFormat::DXT5 => texture2ddecoder::decode_bc3(src, w, band_h, &mut texels),
            _ => texture2ddecoder::decode_bc7(src, w, band_h, &mut texels),
        };
        if decoded.is_err() { return false; }
        // One 4-byte store a texel (a slice copy here was a memcpy call per texel).
        for (p, o) in texels.iter().zip(out.chunks_exact_mut(4)) {
            let o: &mut [u8; 4] = o.try_into().unwrap();
            *o = [((p >> 16) & 0xFF) as u8, ((p >> 8) & 0xFF) as u8, (p & 0xFF) as u8, ((p >> 24) & 0xFF) as u8];
        }
        true
    });
    if !ok { return None; }
    image::RgbaImage::from_raw(w as u32, h as u32, rgba)
}

pub(crate) fn decode_unity_dxt1(data: &[u8], width: u32, height: u32) -> Option<image::RgbaImage> {
    let mut pixels=vec![0u32;(width as usize).checked_mul(height as usize)?];
    texture2ddecoder::decode_bc1a(data,width as usize,height as usize,&mut pixels).ok()?;
    image::RgbaImage::from_raw(width,height,pixels.into_iter().flat_map(u32::to_le_bytes).collect())
}
