//! OK model viewer/editor backend: open one `.okmodel` of an OK base library,
//! list what it holds and read it back exactly as character generation does
//! (replay through the library, texture references restored).
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::sync::Arc;

use crate::oklib::{self, Id};
use crate::unity::{ExtractedMesh, ParsedBundle};

/// A lightmapped renderer instance (`lightmaps` entry of its hierarchy node): its lightmap UVs (Unity uv2, else uv,
/// times the renderer's scale-offset) go to `uv4` (free for Standard-family parts) and its material names the
/// lightmap (`_OKLightmapIndex`).
fn bake_lightmap(m: &mut ExtractedMesh, lightmaps: &crate::unity::PrefabLightmaps, node: i32) {
    let Some(&(_, index, st)) = lightmaps.renderers.iter().find(|(n, _, _)| *n == node) else { return; };
    if !m.uv4.is_empty() || lightmaps.textures.get(index as usize).is_none_or(|t| t.is_empty()) { return; }
    let source = if m.uv2.len() == m.positions.len() { &m.uv2 } else { &m.uvs };
    if source.len() != m.positions.len() { return; }
    m.uv4 = source.iter().map(|uv| [uv[0] * st[0] + st[2], uv[1] * st[1] + st[3]]).collect();
    if let Some(mat) = m.mat.as_mut() { mat.floats.insert("_OKLightmapIndex".into(), index as f32); }
}

/// The settled Animator material values of the renderer at `node` (`ParsedBundle::prefab_animated_materials`) written
/// into the part's material: a colour component (4..7 = r..a) or a float, matched by the property name hash.
fn apply_animated_materials(m: &mut ExtractedMesh, animated: &crate::unity::PrefabAnimatedMaterials, node: i32) {
    let Some(mat) = m.mat.as_mut() else { return };
    let hash = |name: &str| crate::anim::unity_string_to_hash(name) as u32 & 0x0FFF_FFFF;
    for &(_, prop, component, value) in animated.values.iter().filter(|v| v.0 == node) {
        if (4..=7).contains(&component) {
            if let Some((_, c)) = mat.colors.iter_mut().find(|(k, _)| hash(k) == prop) { c[(component - 4) as usize] = value; }
        } else if let Some((_, f)) = mat.floats.iter_mut().find(|(k, _)| hash(k) == prop) { *f = value; }
    }
}

/// Unity Standard's indirect specular source for each Standard-family part (meshes placed in prefab space, Unity
/// metres): the baked reflection probe whose box holds the part's centre (highest importance, then the smallest box),
/// else the scene's skybox reflection (probe node -1). The material names it (`_OKReflectionIndex`, with
/// `_OKReflectionMips` and `_OKReflectionIntensity`); each probe's atlas rides on the first part using it
/// (`_OKReflection`, as lightmaps do).
fn assign_reflection_probes(meshes: &mut [ExtractedMesh], probes: &crate::unity::PrefabReflectionProbes, worlds: &[crate::pose::Mat4]) {
    if probes.probes.is_empty() { return; }
    // Each probe's world box (Unity: the transform's position plus m_BoxOffset, axis-aligned m_BoxSize).
    let boxes: Vec<Option<([f32; 3], [f32; 3])>> = probes.probes.iter().map(|p| {
        let w = worlds.get(usize::try_from(p.node).ok()?)?;
        let c = crate::pose::mat_pos(w);
        let center = [c[0] + p.box_offset[0], c[1] + p.box_offset[1], c[2] + p.box_offset[2]];
        Some((center, p.box_size.map(|s| s * 0.5)))
    }).collect();
    let sky = probes.probes.iter().position(|p| p.node < 0);
    let mut carried = vec![false; probes.probes.len()];
    for m in meshes.iter_mut() {
        if !m.mat.as_ref().is_some_and(|mat| mat.shader.starts_with("Standard")) || m.positions.is_empty() { continue; }
        let n = m.positions.len() as f32;
        let c = m.positions.iter().fold([0f32; 3], |a, p| [a[0] + p[0] / n, a[1] + p[1] / n, a[2] + p[2] / n]);
        let inside = |(center, half): &([f32; 3], [f32; 3])| (0..3).all(|k| (c[k] - center[k]).abs() <= half[k]);
        let chosen = boxes.iter().enumerate().filter(|(_, b)| b.as_ref().is_some_and(inside))
            .max_by(|(a, ba), (b, bb)| {
                let vol = |b: &Option<([f32; 3], [f32; 3])>| b.map(|(_, h)| h[0] * h[1] * h[2]).unwrap_or(f32::MAX);
                probes.probes[*a].importance.cmp(&probes.probes[*b].importance).then(vol(bb).total_cmp(&vol(ba)))
            }).map(|(i, _)| i).or(sky);
        let Some(index) = chosen else { continue };
        let probe = &probes.probes[index];
        let Some(mat) = m.mat.as_mut() else { continue };
        mat.floats.insert("_OKReflectionIndex".into(), index as f32);
        mat.floats.insert("_OKReflectionDefault".into(), if probe.node<0 {1.} else {0.});
        mat.floats.insert("_OKReflectionNode".into(),probe.node as f32);
        mat.floats.insert("_OKReflectionMips".into(), probe.mips as f32);
        mat.floats.insert("_OKReflectionIntensity".into(), probe.intensity);
        if !carried[index] {
            m.shader_extra_textures.insert("_OKReflection".into(), probe.atlas.clone());
            carried[index] = true;
        }
    }
}

/// A renderer instance whose material samples a render-to-texture camera's RenderTexture (`PrefabRenderTextures::users`):
/// its material names the camera (`_OKRenderTexture`, index into the prefab's cameras) and carries it in prefab space
/// (Unity axes, metres): `_OKRTCamPos` (position, vertical fov), `_OKRTCamFwd` (+Z, near), `_OKRTCamUp` (+Y, far),
/// `_OKRTCamSize` (RenderTexture width, height, orthographic 0/1, orthographic size), `_OKRTCamClear` (background
/// colour, clear flags). A camera Unity never runs (disabled or inactive) is left out.
/// The times of day (a bit per SunLightInfo.Info.Type) whose `visibleList` holds this node or one above it; 0: shown
/// whatever the time.
fn sun_mask(sun: &crate::unity::PrefabSunLight, h: &crate::pose::PrefabHierarchy, node: i32) -> u32 {
    let mut mask = 0;
    for info in &sun.infos {
        let mut i = node;
        while i >= 0 {
            if info.visible.contains(&i) { mask |= 1 << info.kind.clamp(0, 7); break; }
            i = h.nodes.get(i as usize).map_or(-1, |n| n.parent);
        }
    }
    mask
}

/// A part shown only at some times of day (SunLightInfo.Set hides every list, then shows the current time's):
/// `_OKSunVisible` = sun_mask.
fn mark_sun_visible(m: &mut ExtractedMesh, sun: &crate::unity::PrefabSunLight, h: &crate::pose::PrefabHierarchy, node: i32) {
    let mask = sun_mask(sun, h, node);
    if mask != 0 { if let Some(mat) = m.mat.as_mut() { mat.floats.insert("_OKSunVisible".into(), mask as f32); } }
}

fn mark_render_texture(m: &mut ExtractedMesh, rt: &crate::unity::PrefabRenderTextures, node: i32, h: &crate::pose::PrefabHierarchy, worlds: &[crate::pose::Mat4]) {
    let Some(mat) = m.mat.as_mut() else { return; };
    let Some(&(_, _, _, c)) = rt.users.iter().find(|(n, name, _, _)| *n == node && *name == mat.name) else { return; };
    let Some(cam) = rt.cameras.get(c as usize).filter(|c| c.enabled && c.active) else { return; };
    let Some(w) = worlds.get(cam.node as usize) else { return; };
    let Some((_, rot, _)) = h.node_world(cam.node as usize) else { return; };
    let pos = crate::pose::mat_pos(w);
    // The view turns with the camera's rotation only (Unity ignores a camera's scale; the live stage's is zero).
    let mut d = [[0., 0., 1.], [0., 1., 0.]];
    crate::pose::apply_mat(&mut d, &crate::pose::trs([0.; 3], rot, [1.; 3]));
    let [fwd, up] = d;
    mat.floats.insert("_OKRenderTexture".into(), c as f32);
    mat.colors.insert("_OKRTCamPos".into(), [pos[0], pos[1], pos[2], cam.fov]);
    mat.colors.insert("_OKRTCamFwd".into(), [fwd[0], fwd[1], fwd[2], cam.near]);
    mat.colors.insert("_OKRTCamUp".into(), [up[0], up[1], up[2], cam.far]);
    mat.colors.insert("_OKRTCamSize".into(), [cam.width as f32, cam.height as f32, if cam.orthographic { 1. } else { 0. }, cam.ortho_size]);
    mat.colors.insert("_OKRTCamClear".into(), [cam.background[0], cam.background[1], cam.background[2], cam.clear_flags as f32]);
}

/// Run with the library active in replay mode; a library that was not
/// active before is deactivated again (the host may open game-mode cards).
pub fn with_library<R>(lib: &Path, f: impl FnOnce() -> Result<R, String>) -> Result<R, String> {
    // The library stays active afterwards, as for OK cards: activating loads its whole source index (~800 MB, about a
    // minute), and the model editor makes many calls (info, preview session, every texture). Deactivating after each
    // one reloaded the index every time and kept the editor busy for tens of minutes.
    let t = std::time::Instant::now();
    oklib::ensure_library(lib)?;
    if std::env::var_os("OK_PROFILE").is_some() { eprintln!("ok-extract: PROFILE okmodel ensure_library {:.1}ms", t.elapsed().as_secs_f64() * 1000.0); }
    f()
}

pub struct ModelView {
    pub lib: PathBuf,
    pub id: Id,
    parsed: Arc<ParsedBundle>,
    /// The model file's recorded (query, args) keys, read when first asked for (listings and prefab names): opening a
    /// view for a session does not need them, and reading the whole model file was most of an item's open (~100 ms of a
    /// ~160 ms cube; a Studio scene opens the same item a thousand times).
    records: std::sync::OnceLock<Vec<(String, Vec<u8>)>>,
}

/// `<lib>/models/xx/<id>.okmodel` (or the id hex) -> (library, id).
pub fn locate(path: &Path) -> Result<(PathBuf, Id), String> {
    let stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or_default();
    let id = oklib::id_from_hex_pub(stem).ok_or("not an .okmodel file name")?;
    let lib = path.parent().and_then(Path::parent).and_then(Path::parent).ok_or("model is not inside an OK base library")?;
    if !lib.join("oklib.json").is_file() { return Err(format!("{} is not an OK base library", lib.display())); }
    Ok((lib.to_owned(), id))
}

fn decode<T: serde::de::DeserializeOwned>(raw: &[u8]) -> Option<T> {
    crate::unity::with_texture_plans(|| bincode::deserialize(raw).ok())
}

impl ModelView {
    pub fn open(lib: &Path, id: Id) -> Result<ModelView, String> {
        oklib::ensure_library(lib)?;
        if !oklib::model_path(lib, &id).is_file() { return Err(format!("{} not found", oklib::model_path(lib, &id).display())); }
        let parsed = crate::unity::parse_bundle_cached(oklib::token(&id));
        Ok(ModelView { lib: lib.to_owned(), id, parsed, records: std::sync::OnceLock::new() })
    }

    fn records(&self) -> &[(String, Vec<u8>)] {
        self.records.get_or_init(|| oklib::model_records(&self.lib, &self.id).unwrap_or_default())
    }

    fn args_of<T: serde::de::DeserializeOwned>(&self, query: &str) -> Vec<T> {
        self.records().iter().filter(|(q, _)| q == query).filter_map(|(_, a)| bincode::deserialize(a).ok()).collect()
    }

    /// Prefab names with recorded meshes (exact spellings the lists use).
    /// The first prefab with shown meshes (what a scene item shows by default), without listing the whole model.
    pub fn first_shown_prefab(&self) -> Option<String> {
        let names: BTreeSet<String> = self.args_of::<(String, bool)>("meshes_for_maindata").into_iter().map(|(n, _)| n).collect();
        names.into_iter().find(|n| !self.prefab_meshes(n, false).is_empty())
    }

    pub fn prefabs(&self) -> Vec<String> {
        let mut names: BTreeSet<String> = self.args_of::<(String, bool)>("meshes_for_maindata").into_iter().map(|(n, _)| n).collect();
        names.retain(|n| !self.prefab_meshes(n, false).is_empty() || !self.prefab_meshes(n, true).is_empty());
        names.into_iter().collect()
    }

    /// Recorded prefabs using a source shader, without retaining an entire
    /// bundle's geometry as `contents` does. Suitable for selective refreshes.
    pub fn prefabs_with_shader(&self, shader: &str) -> Vec<String> {
        let names: BTreeSet<String> = self.args_of::<(String, bool)>("meshes_for_maindata").into_iter().map(|(n, _)| n).collect();
        names.into_iter().filter(|name| self.prefab_meshes(name, true).iter()
            .any(|mesh| mesh.mat.as_ref().is_some_and(|mat| mat.shader == shader))).collect()
    }

    pub fn prefab_meshes(&self, prefab: &str, include_disabled: bool) -> Vec<ExtractedMesh> {
        if include_disabled { self.parsed.meshes_for_maindata_including_disabled(prefab).0 } else { self.parsed.meshes_for_maindata(prefab).0 }
    }

    pub fn texture_png(&self, name: &str) -> Option<Vec<u8>> { self.parsed.texture_png_named(name) }
    pub fn material(&self, name: &str) -> Option<crate::unity::MeshMaterial> { self.parsed.material_named(name) }

    /// Everything the model holds, for an editor's content tree.
    pub fn contents(&self) -> serde_json::Value {
        let mut queries: BTreeMap<&str, usize> = BTreeMap::new();
        for (q, _) in self.records() { *queries.entry(q.as_str()).or_default() += 1; }
        let tex = self.parsed.texture_directory();
        let textures: Vec<serde_json::Value> = tex.catalog.iter().map(|(n, px)| serde_json::json!({"name": n, "pixels": px})).collect();
        // Each prefab's meshes once (shown and all renderers): the listing used to rebuild them per material.
        let names: BTreeSet<String> = self.args_of::<(String, bool)>("meshes_for_maindata").into_iter().map(|(n, _)| n).collect();
        let mut by_prefab: Vec<(String, Vec<crate::ExtractedMesh>, Vec<crate::ExtractedMesh>)> = Vec::new();
        for name in names {
            let shown = self.prefab_meshes(&name, false);
            let all = self.prefab_meshes(&name, true);
            if !shown.is_empty() || !all.is_empty() { by_prefab.push((name, shown, all)); }
        }
        let mut material_names: BTreeSet<String> = self.args_of::<String>("material_named").into_iter().collect();
        let mut prefab_materials: BTreeMap<String, crate::unity::MeshMaterial> = BTreeMap::new();
        for (_, _, all) in &by_prefab { for m in all { if let Some(mat) = &m.mat { material_names.insert(mat.name.clone()); prefab_materials.entry(mat.name.clone()).or_insert_with(|| mat.clone()); } } }
        let materials: Vec<serde_json::Value> = material_names.iter().map(|n| {
            let m = self.material(n).or_else(|| prefab_materials.get(n).cloned());
            match m {
                Some(m) => serde_json::json!({"name": n, "shader": m.shader, "render_queue": m.render_queue, "keywords": m.keywords,
                    "colors": m.colors, "floats": m.floats, "textures": m.tex_slots.iter().map(|(k, v)| serde_json::json!([k, v])).collect::<Vec<_>>()}),
                None => serde_json::json!({"name": n}),
            }
        }).collect();
        let mut prefabs = Vec::new();
        for (name, shown, all) in &by_prefab {
            let name = name.clone();
            let meshes: Vec<serde_json::Value> = all.iter().map(|m| serde_json::json!({
                "name": m.name, "path_id": m.path_id, "vertices": m.positions.len(), "triangles": m.indices.len() / 3,
                "skinned": !m.bind_poses.is_empty(), "enabled": shown.iter().any(|s| s.path_id == m.path_id && s.name == m.name),
                "material": m.mat.as_ref().map(|x| x.name.clone()), "shader": m.mat.as_ref().map(|x| x.shader.clone()),
                "texture": m.texture_name,
            })).collect();
            let (role, categories) = self.prefab_role(&name);
            prefabs.push(serde_json::json!({"name": name, "role": role, "categories": categories, "meshes": meshes}));
        }
        let skeletons: Vec<String> = self.args_of::<String>("skeleton_for_prefab_name");
        serde_json::json!({
            "id": oklib::id_hex(&self.id),
            "sources": crate::gamefs::labels_for_model(&self.id),
            "prefabs": prefabs,
            "textures": textures,
            "materials": materials,
            "rigs": skeletons,
            "queries": queries,
            "edits": crate::okmodel_edit::path(&self.lib, &self.id).is_file(),
        })
    }
}

impl ModelView {
    /// The list categories that use this prefab of the model's sources, and the part role the game gives it
    /// (the library replays the game's list files: vanilla list, each source zipmod's list). A prefab no list
    /// names keeps the categories of its bundle; nothing listed falls back to the name rule.
    pub fn prefab_role(&self, prefab: &str) -> (String, Vec<i32>) {
        let (role, cats, _) = self.prefab_list_entry(prefab);
        (role, cats)
    }

    /// `prefab_role` plus the list entry naming the prefab (for the textures the game takes from the list).
    fn prefab_list_entry(&self, prefab: &str) -> (String, Vec<i32>, Option<crate::list::ListEntry>) {
        // The answer depends only on the library's lists and the model's labels: once per process (a Studio scene opens
        // the same item hundreds of times, and each scan of the whole list was ~17 ms).
        type Memo = std::sync::Mutex<std::collections::HashMap<(PathBuf, Id, String), (String, Vec<i32>, Option<crate::list::ListEntry>)>>;
        static MEMO: std::sync::OnceLock<Memo> = std::sync::OnceLock::new();
        let key = (self.lib.clone(), self.id, prefab.to_owned());
        if let Some(hit) = MEMO.get_or_init(Default::default).lock().unwrap().get(&key) { return hit.clone(); }
        let found = self.prefab_list_entry_uncached(prefab);
        MEMO.get_or_init(Default::default).lock().unwrap().insert(key, found.clone());
        found
    }

    fn prefab_list_entry_uncached(&self, prefab: &str) -> (String, Vec<i32>, Option<crate::list::ListEntry>) {
        let mut exact = BTreeSet::new();
        let mut bundle = BTreeSet::new();
        let mut entry: Option<crate::list::ListEntry> = None;
        for label in crate::gamefs::labels_for_model(&self.id) {
            let (zip, file) = match label.split_once('|') { Some((z, f)) => (Some(z.to_owned()), f.to_owned()), None => (None, label.clone()) };
            let file_name = file.rsplit('/').next().unwrap_or(&file).to_ascii_lowercase();
            let mut scan = |catalog: &crate::list::ChaListCatalog| {
                for e in catalog.by_id.values() {
                    let ab = e.main_ab.replace('\\', "/").to_ascii_lowercase();
                    if ab.rsplit('/').next() != Some(file_name.as_str()) || !crate::unity::is_mesh_category(e.category_no) { continue; }
                    bundle.insert(e.category_no);
                    if e.main_data.eq_ignore_ascii_case(prefab) || e.info("MainData").eq_ignore_ascii_case(prefab) {
                        exact.insert(e.category_no);
                        // Deterministic among several entries using the prefab: lowest (category, id).
                        if entry.as_ref().is_none_or(|x| (e.category_no, e.id) < (x.category_no, x.id)) { entry = Some(e.clone()); }
                    }
                }
            };
            if let Some(zip) = &zip { scan(&crate::list::catalog_from_zipmod(&self.lib.join(zip))); }
            scan(&crate::list::load_characustom_dir_shared(&self.lib.join("abdata/list/characustom")));
        }
        let cats: Vec<i32> = if exact.is_empty() { bundle.into_iter().collect() } else { exact.into_iter().collect() };
        let role = match cats.first() { Some(&c) => crate::unity::role_for_category(c, ""), None => crate::unity::role_for(prefab, "") };
        (role.to_owned(), cats, entry)
    }

    /// One prefab as a preview session (meshes at their authored geometry).
    pub fn prefab_session(&self, prefab: &str, include_disabled: bool) -> Result<crate::session::OkSession, String> {
        self.prefab_session_fk(prefab, include_disabled, &[])
    }

    /// Transform tree, mesh nodes and lights of a prefab (recorded by `studio-nodes`; empty when not recorded).
    pub fn hierarchy(&self, prefab: &str) -> crate::pose::PrefabHierarchy { self.parsed.prefab_hierarchy(prefab) }
    /// Baked lightmaps of a prefab (recorded by `studio-nodes`).
    pub fn prefab_lightmaps(&self, prefab: &str) -> crate::unity::PrefabLightmaps { self.parsed.prefab_lightmaps(prefab) }
    pub fn prefab_renderer_settings(&self, prefab: &str) -> Vec<(i32,i32,i32,bool)> { self.parsed.prefab_renderer_settings(prefab) }
    /// A map's SunLightInfo (recorded by `studio-nodes`).
    pub fn parsed_sun_light(&self, prefab: &str) -> crate::unity::PrefabSunLight { self.parsed.prefab_sun_light(prefab) }

    /// Bones a prefab's shown skinned meshes follow: what Studio item FK can turn.
    pub fn fk_bones(&self, prefab: &str) -> Vec<String> {
        let mut out: Vec<String> = Vec::new();
        for m in self.prefab_meshes(prefab, false) {
            if m.weights.is_empty() { continue; }
            for n in &m.bone_names { if !out.contains(n) { out.push(n.clone()); } }
        }
        // Rigid meshes turn with the nodes above them (recorded hierarchy): every ancestor below the prefab root.
        let h = self.hierarchy(prefab);
        for (_, node) in &h.mesh_nodes {
            let mut i = *node;
            while i > 0 {
                let Some(n) = h.nodes.get(i as usize) else { break; };
                if !out.contains(&n.name) { out.push(n.name.clone()); }
                i = n.parent;
            }
        }
        out
    }

    /// `prefab_session` with item FK: the listed bones (name, local Unity Euler degrees) turned before the meshes are baked.
    pub fn prefab_session_fk(&self, prefab: &str, include_disabled: bool, fk: &[(String, [f32; 3])]) -> Result<crate::session::OkSession, String> {
        let profile = std::env::var_os("OK_PROFILE").is_some();
        let t = std::time::Instant::now();
        let mut meshes = self.prefab_meshes(prefab, include_disabled);
        // MDK uses Standard-style slot names, not the character shader's
        // _NormalMap. Older model memos keep these bindings but omit their PNGs.
        // Resolve through the library's named-texture records at session time.
        for mesh in &mut meshes {
            let Some(mat) = mesh.mat.as_ref().filter(|m| m.shader == "Shader Forge/main_StandardMDK_studio") else { continue };
            for property in ["_BumpMap", "_ParallaxMap", "_MetallicGlossMap", "_EmissionMap"] {
                let Some(name) = mat.slot(property) else { continue };
                if let Some(png) = self.parsed.texture_png_named(name) {
                    mesh.shader_extra_textures.insert(property.into(), png);
                    if let Some(s) = self.parsed.texture_sampling_named(name) {
                        mesh.runtime_textures.insert(property.into(), [s.filter, s.wrap_u, s.wrap_v, i32::from(s.srgb), s.mip_count]);
                    }
                }
            }
        }
        // Studio map times of day (SunLightInfo): what a time shows may be inactive in the prefab, so those meshes come in
        // too; every part under a time's list is marked with the times it shows at (mark_sun_visible).
        let sun = self.parsed.prefab_sun_light(prefab);
        let renderers = self.parsed.prefab_renderer_settings(prefab);
        let renderer_visibility = self.parsed.prefab_renderer_visibility(prefab);
        let has_time_parts = sun.infos.iter().any(|i| !i.visible.is_empty());
        let has_shadow_only = renderers.iter().any(|r| r.1 == 3 && r.3);
        if !include_disabled && (has_time_parts || has_shadow_only || renderer_visibility.iter().any(|(_, v)| *v)) {
            let h = self.hierarchy(prefab);
            let have: BTreeSet<(i64, usize)> = meshes.iter().map(|m| (m.path_id, m.submesh_index)).collect();
            for m in self.prefab_meshes(prefab, true) {
                if have.contains(&(m.path_id, m.submesh_index)) { continue; }
                if h.mesh_nodes.iter().any(|(id, n)| *id == m.path_id &&
                    (renderer_visibility.iter().any(|(node, v)| node == n && *v) || sun_mask(&sun, &h, *n) != 0 || renderers.iter().any(|r| r.0 == *n && r.1 == 3 && r.3))) { meshes.push(m); }
            }
        }
        if profile { eprintln!("ok-extract: PROFILE okmodel meshes {:.1}ms", t.elapsed().as_secs_f64() * 1000.0); }
        if meshes.is_empty() { return Err(format!("prefab {prefab} has no meshes")); }
        // Rendered with the role the game gives the part (shader paths depend on it) and the textures the
        // game builds from the list entry.
        let t = std::time::Instant::now();
        let (role, _, entry) = self.prefab_list_entry(prefab);
        if profile { eprintln!("ok-extract: PROFILE okmodel list entry {:.1}ms", t.elapsed().as_secs_f64() * 1000.0); }
        let t = std::time::Instant::now();
        if let Some(entry) = &entry { crate::preview_list_item(&self.lib, entry, prefab, &mut meshes); }
        if profile { eprintln!("ok-extract: PROFILE okmodel list item {:.1}ms", t.elapsed().as_secs_f64() * 1000.0); }
        let t = std::time::Instant::now();
        let mut meshes: Vec<ExtractedMesh> = meshes.iter().map(|m| posed_static_fk(m, fk)).collect();
        // Rigid meshes (MeshFilter) keep mesh-space vertices: place them at their GameObject in the prefab, through the
        // recorded hierarchy with the FK nodes turned (without a recorded hierarchy, at the recorded prefab matrix).
        let h = self.hierarchy(prefab);
        let worlds = h.worlds_with(&|i| fk.iter().find(|(n, _)| *n == h.nodes[i].name).map(|(_, e)| crate::pose::euler_deg_to_quat(*e)));
        let place = |m: &mut ExtractedMesh, w: &crate::pose::Mat4| {
            crate::pose::apply_mat(&mut m.positions, w);
            let mut r = *w;
            for row in &mut r { row[3] = 0.; }
            crate::pose::apply_mat(&mut m.normals, &r);
            for n in &mut m.normals { let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt(); if l > 1e-8 { *n = n.map(|v| v / l); } }
            m.rest_positions = m.positions.clone();
            m.rest_normals = m.normals.clone();
            m.prefab_local = None;
        };
        // A Mesh drawn by several GameObjects becomes one part per instance (the extra ones named `<mesh>#<n>`, as
        // parts are told apart by name).
        let lightmaps = self.parsed.prefab_lightmaps(prefab);
        let render_textures = self.parsed.prefab_render_textures(prefab);
        let animated = self.parsed.prefab_animated_materials(prefab);
        let mark_renderer = |m: &mut ExtractedMesh, node: i32| {
            if let (Some(mat), Some((_, casting, layer, enabled))) = (m.mat.as_mut(), renderers.iter().find(|r| r.0 == node)) {
                mat.floats.insert("_OKRendererShadowMode".into(), *casting as f32);
                mat.floats.insert("_OKRendererLayer".into(), *layer as f32);
                mat.floats.insert("_OKRendererEnabled".into(), u8::from(*enabled && renderer_visibility.iter().find(|(n, _)| *n == node).is_none_or(|(_, v)| *v)) as f32);
            }
        };
        let mut instances: Vec<ExtractedMesh> = Vec::new();
        for m in meshes.iter_mut().filter(|m| !m.uses_skin_renderer()) {
            let nodes: Vec<(i32, crate::pose::Mat4)> = h.mesh_nodes.iter().filter(|(id, _)| *id == m.path_id)
                .filter_map(|(_, n)| worlds.get(*n as usize).map(|w| (*n, *w))).collect();
            for (k, (node, w)) in nodes.iter().enumerate().skip(1) {
                let mut extra = m.clone();
                extra.name = format!("{}#{}", m.name, k + 1);
                place(&mut extra, w);
                bake_lightmap(&mut extra, &lightmaps, *node);
                mark_render_texture(&mut extra, &render_textures, *node, &h, &worlds);
                apply_animated_materials(&mut extra, &animated, *node);
                mark_sun_visible(&mut extra, &sun, &h, *node);
                mark_renderer(&mut extra, *node);
                instances.push(extra);
            }
            if let Some((node, _)) = nodes.first() { bake_lightmap(m, &lightmaps, *node); mark_render_texture(m, &render_textures, *node, &h, &worlds); apply_animated_materials(m, &animated, *node); mark_sun_visible(m, &sun, &h, *node); mark_renderer(m, *node); }
            let Some(w) = nodes.first().map(|(_, w)| *w).or(m.prefab_local) else { continue; };
            place(m, &w);
        }
        for m in meshes.iter_mut().filter(|m| m.uses_skin_renderer()) {
            if let Some((_, node)) = h.mesh_nodes.iter().find(|(id, _)| *id == m.path_id) { mark_renderer(m, *node); }
        }
        meshes.extend(instances);
        // The map's sun for each time of day (Unity Euler angle, colour, intensity) and its culling mask, on every part
        // (the renderer reads them from any part of the map).
        if !sun.infos.is_empty() {
            for mat in meshes.iter_mut().filter_map(|m| m.mat.as_mut()) {
                for i in &sun.infos {
                    mat.colors.insert(format!("_OKSun{}Color", i.kind), [i.color[0], i.color[1], i.color[2], i.intensity]);
                    mat.colors.insert(format!("_OKSun{}Angle", i.kind), [i.angle[0], i.angle[1], i.angle[2], 1.]);
                }
                mat.floats.insert("_OKSunCulling".into(), (sun.culling_mask & 0x00FF_FFFF) as f32);
                mat.floats.insert("_OKSunCullingLo".into(), (sun.culling_mask & 0xFFFF) as f32);
                mat.floats.insert("_OKSunCullingHi".into(), (sun.culling_mask >> 16) as f32);
                mat.floats.insert("_OKSunLightmapping".into(), sun.lightmapping as f32);
            }
        }
        // Each lightmap texture rides on the first part that uses it; the others name it by index (no copies).
        for (index, png) in lightmaps.textures.iter().enumerate() {
            if png.is_empty() { continue; }
            if let Some(m) = meshes.iter_mut().find(|m| m.mat.as_ref().and_then(|x| x.floats.get("_OKLightmapIndex")).is_some_and(|v| *v as usize == index)) {
                m.shader_extra_textures.insert("_OKLightmap".into(), png.clone());
            }
        }
        if !lightmaps.textures.is_empty() {
        for (index, png) in self.parsed.prefab_lightmap_directions(prefab).textures.iter().enumerate() {
            if png.is_empty() { continue; }
            if let Some(m) = meshes.iter_mut().find(|m| m.mat.as_ref().and_then(|x| x.floats.get("_OKLightmapIndex")).is_some_and(|v| *v as usize == index)) {
                m.shader_extra_textures.insert("_OKLightmapDirection".into(), png.clone());
            }
        }
        }
        assign_reflection_probes(&mut meshes, &self.parsed.prefab_reflection_probes(prefab), &worlds);
        let parts = crate::preview_session_parts(&self.lib, &role, meshes, entry.as_ref())?;
        if profile { eprintln!("ok-extract: PROFILE okmodel parts {:.1}ms", t.elapsed().as_secs_f64() * 1000.0); }
        if parts.is_empty() { return Err(format!("prefab {prefab} has no shown meshes")); }
        Ok(crate::session::preview_session(&parts, &self.lib))
    }
}

/// `model-diff <lib> <id> <prefab> <card>`: each preview part against the part of the same name in the
/// card's session (role, material, textures; geometry arrays summarised) — what the preview lacks.
pub fn diff_cli(lib: &Path, which: &str, prefab: &str, card: &Path) -> Result<(), String> {
    let id = oklib::id_from_hex_pub(which).or_else(|| locate(Path::new(which)).ok().map(|x| x.1)).ok_or("model id or path required")?;
    let view = ModelView::open(lib, id)?;
    let preview = crate::session::preview_json(&view.prefab_session(prefab, false)?.to_preview_bytes()?)?;
    let parts = |v: &serde_json::Value| v["session"]["parts"].as_array().cloned().unwrap_or_default();
    let brief = |v: &serde_json::Value| {
        let mut v = v.clone();
        if let Some(o) = v.as_object_mut() {
            for (_, x) in o.iter_mut() {
                if x.as_array().is_some_and(|a| a.len() > 16) { *x = serde_json::json!(format!("array len {}", x.as_array().unwrap().len())); }
            }
        }
        v
    };
    let a = parts(&preview);
    if card.as_os_str() == "-" {
        for p in &a { println!("{}", serde_json::to_string_pretty(&brief(p)).unwrap()); }
        return Ok(());
    }
    let session = crate::session::open(lib, Some(card), 0)?;
    let b = parts(&crate::session::preview_json(&session.to_preview_bytes()?)?);
    for p in &a {
        let name = p["name"].as_str().unwrap_or("");
        let Some(q) = b.iter().find(|q| q["name"].as_str() == Some(name)) else { println!("{name}: not in the card session"); continue };
        // Geometry and texel arrays are summarised by length (the preview is posed differently by design).
        let mut out = Vec::new();
        oklib::diff_json_pub(&brief(p), &brief(q), String::new(), &mut out);
        println!("{name}: preview role {} / card role {}, {} difference(s)", p["role"], q["role"], out.len());
        for line in out.iter().take(80) { println!("  {line}"); }
    }
    Ok(())
}

/// A model shown on its own has no character skeleton: skinned meshes are posed with the prefab's own
/// renderer skeleton (as the game does before parenting the part) and kept static; unskinned or
/// unposable meshes keep their mesh-space vertices.
/// `posed_static` after turning the skinned mesh's bones by item FK (the bone's local rotation becomes the Euler).
pub(crate) fn posed_static_fk(mesh: &ExtractedMesh, fk: &[(String, [f32; 3])]) -> ExtractedMesh {
    if fk.is_empty() || mesh.weights.is_empty() || mesh.smr_skel.is_none() { return posed_static(mesh); }
    let mut m = mesh.clone();
    if let Some(skel) = m.smr_skel.as_mut() {
        for tf in skel.tfs.values_mut() {
            if let Some((_, e)) = fk.iter().find(|(n, _)| *n == tf.name) { tf.rot = crate::pose::euler_deg_to_quat(*e); }
        }
        skel.rebuild_worlds();
    }
    posed_static(&m)
}

pub(crate) fn posed_static(mesh: &ExtractedMesh) -> ExtractedMesh {
    let mut m = mesh.clone();
    if !m.weights.is_empty() {
        if let Some(mut skel) = m.smr_skel.clone() {
            if !m.bone_names.is_empty() { skel.set_bone_order(&m.bone_names); }
            crate::skin_mesh_geometry(&mut m, &skel);
        }
    }
    m.rest_positions = m.positions.clone();
    m.rest_normals = m.normals.clone();
    m.weights.clear();
    m.bone_idx.clear();
    m.skel_kind = 0;
    m
}

/// `model-view <lib> <id|path> [prefab]`: contents (or one prefab's meshes) as JSON.
pub fn cli(lib: &Path, which: &str, prefab: Option<&str>) -> Result<(), String> {
    let id = oklib::id_from_hex_pub(which).or_else(|| locate(Path::new(which)).ok().map(|x| x.1)).ok_or("model id or path required")?;
    let view = ModelView::open(lib, id)?;
    match prefab {
        Some(p) if p.starts_with("shader:") => {
            for name in view.prefabs_with_shader(&p[7..]) { println!("{name}"); }
        }
        None => println!("{}", serde_json::to_string_pretty(&view.contents()).unwrap()),
        Some(p) => for m in view.prefab_meshes(p, true) {
            let shown = posed_static(&m);
            let (lo, hi) = crate::pose::aabb(&shown.positions);
            println!("{}\tverts={} tris={} tex={} {:?} detail={} normal={} mat={:?} skel={} bounds=[{:.3},{:.3},{:.3}]..[{:.3},{:.3},{:.3}]", m.name, m.positions.len(), m.indices.len() / 3, m.texture_name,
                m.texture_png.as_ref().map(|v| v.len()), m.detail_mask_png.is_some(), m.normal_map_png.is_some(), m.mat.as_ref().map(|x| (&x.name, &x.shader)),
                m.smr_skel.as_ref().map(|k| k.names.len()).unwrap_or(0), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        },
    }
    Ok(())
}

#[cfg(test)]
mod studio_item_timing {
    #[test]
    #[ignore = "reads the OK base library and an OK card"]
    fn model_prefab_session_after_ok_card_is_fast() {
        let lib = std::path::PathBuf::from("C:/portable/OpenKoikatsu/Content/IndependentResources/base_v1");
        std::env::set_var("OK_BASE_LIBRARY", &lib);
        let card = std::path::Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C002.png");
        let t = std::time::Instant::now();
        let s = crate::session::open(&crate::oklib::root_for_card(std::path::Path::new(""), Some(card)), Some(card), 0).unwrap();
        eprintln!("TIMING card open {:.1}s parts={}", t.elapsed().as_secs_f64(), s.parts.len());
        let id = crate::oklib::id_from_hex_pub("447f3c180000000045cc17684f4b4331").unwrap();
        for round in 0..2 {
            let t = std::time::Instant::now();
            let n = super::with_library(&lib, || super::ModelView::open(&lib, id)?.prefab_session("p_cf_hair_b_01", false)).unwrap().parts.len();
            eprintln!("TIMING prefab session round {round} {:.1}s parts={n}", t.elapsed().as_secs_f64());
        }
    }
}

#[cfg(test)]
mod item_fk_tests {
    #[test]
    #[ignore = "OK base library: Studio item FK turns a skinned item's bone before baking"]
    fn item_fk_turns_the_mesh() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let id = crate::oklib::id_from_hex_pub("1496300600000000d8e3d4984f4b4331").unwrap();
        super::with_library(&lib, || {
            let view = super::ModelView::open(&lib, id)?;
            let prefab = "p_koi_syu_FK_00_00";
            let bones = view.fk_bones(prefab);
            println!("{} FK bones: {:?}", bones.len(), &bones[..bones.len().min(8)]);
            assert!(!bones.is_empty(), "the FK item's meshes follow bones");
            let positions = |fk: &[(String, [f32; 3])]| -> Vec<[f32; 3]> {
                view.prefab_meshes(prefab, false).iter().flat_map(|m| super::posed_static_fk(m, fk).positions).collect()
            };
            let rest = positions(&[]);
            let plain: Vec<[f32; 3]> = view.prefab_meshes(prefab, false).iter().flat_map(|m| super::posed_static(m).positions).collect();
            assert_eq!(rest, plain, "no FK bakes the prefab pose");
            let turned = positions(&[(bones[bones.len() / 2].clone(), [0., 0., 60.])]);
            let moved = rest.iter().zip(&turned).filter(|(a, b)| (0..3).any(|i| (a[i] - b[i]).abs() > 1e-4)).count();
            println!("turning {} by 60 deg moves {moved} of {} vertices", bones[bones.len() / 2], rest.len());
            assert!(moved > 0 && moved < rest.len());
            Ok::<(), String>(())
        }).unwrap();
    }
}

#[cfg(test)]
#[test]
#[ignore = "installed game and base library: settled map visibility source oracle"]
fn stage_1901_renderer_visibility_matches_source() {
    let lib = std::env::var_os("OK_TEST_LIBRARY").map(PathBuf::from).unwrap_or_else(|| Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1"));
    let source = crate::unity::parse_bundle_cached(std::fs::read(crate::test_support::game_path("abdata/studio/map/livestage_s.unity3d")).unwrap());
    let expected = source.prefab_renderer_visibility("Map");
    assert!(expected.iter().any(|(_, visible)| *visible));
    assert!(expected.iter().filter(|(_, visible)| !visible).count() >= 114);
    with_library(&lib, || {
        let model = ModelView::open(&lib, crate::oklib::id_from_hex_pub("6eb3180200000000fb5732f44f4b4331").unwrap())?;
        assert_eq!(model.parsed.prefab_renderer_visibility("Map"), expected);
        let session = model.prefab_session("Map", false)?;
        let beam_parts: Vec<_> = session.parts.iter().filter(|p| p.source_material.as_ref().is_some_and(|m| m.shader == "Custom/StandardDouble")).collect();
        let disabled_beams = beam_parts.iter().filter(|p| p.source_material.as_ref().unwrap().floats.get("_OKRendererEnabled") == Some(&0.)).count();
        assert!(disabled_beams >= 6, "inactive beam instances are hidden");
        assert!(disabled_beams < beam_parts.len(), "shared shader is not globally disabled");
        println!("beam material parts={} disabled={disabled_beams}", beam_parts.len());
        assert!(session.parts.iter().any(|p| p.source_material.as_ref().is_some_and(|m| m.floats.get("_OKRendererEnabled") == Some(&1.))));
        println!("visibility source MATCH instances={} white-beam-parts={}", expected.len(), beam_parts.len());
        Ok(())
    }).unwrap();
}
