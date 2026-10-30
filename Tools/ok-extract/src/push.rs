//! Parts into the session: the push context and batch (rig, texture transforms, emit in parallel), push_meshes and list entry lookups.
use super::*;

/// One push_meshes call kept for a PushBatch: what its rig, texture transforms and emit read (OK_FIRSTLOAD=A2).
pub(crate) struct PushCtx<'a, 's> {
    out_dir: Option<&'a Path>,
    game_root: &'a Path,
    role: String,
    slot: Option<i32>,
    category_no: Option<i32>,
    guid: Option<String>,
    property: Option<String>,
    name: Option<String>,
    // Borrowed for an immediate call; a kept call owns them, since a later pass can change the body (clothes).
    body_skel: Option<std::borrow::Cow<'s, crate::pose::Skeleton>>,
    head_skel: Option<std::borrow::Cow<'s, crate::pose::Skeleton>>,
    parent_key: Option<String>,
    hair_color: Option<[[f32; 4]; 3]>,
    part_skel: Option<crate::pose::Skeleton>,
    part_local: Option<crate::pose::Mat4>,
    me_tex: Option<&'a crate::card::MeTexMap>,
    me_color: Option<&'a crate::card::MeColorMap>,
    outfit: i32,
    alpha_mask: Option<(Vec<u8>, f32, f32)>,
    accessory_assignment: Option<crate::unity::AccessoryBodyBinding>,
    convert_normals: bool,
    // This thread's state when the call was made: the accessory moves (begin_acc_place), the clothes states and the
    // profile switches, re-applied on the worker that runs the item.
    acc_place: Option<[crate::pose::Mat4; 2]>,
    clothes_active: Option<[u8; 9]>,
    clothes_requested: Option<[u8; 9]>,
    overrides: Vec<(&'static str, Option<&'static str>)>,
    // The hair gloss texture's sampling for the emitted parts that carry one (accessory_extract).
    hair_gloss_sampling: Option<[i32; 5]>,
}
pub(crate) type PushWork = (usize, ExtractedMesh, bool, bool);
pub(crate) type PushDone = (usize, ExtractedMesh, Option<crate::session::AccessoryRuntime>);

/// OK_FIRSTLOAD=A2: push_meshes calls of one card open, kept until `flush`. A call has only a mesh or two, so
/// running its own meshes in parallel gained nothing; the batch runs every kept mesh's rig and texture transforms
/// (PNG decode/re-encode, flips, tints, masks, normal conversion) at once, then emits them in call order, which keeps
/// the output identical. Nothing between the calls reads the emitted parts.
#[derive(Default)]
pub(crate) struct PushBatch<'a> {
    groups: Vec<(PushCtx<'a, 'static>, Vec<PushWork>)>,
    /// Accessory slots whose colour bake and push wait for the flush (accessory_extract::emit), each with the number of
    /// groups kept before it (its place among the calls).
    slots: Vec<(usize, crate::accessory_extract::Deferred<'a>)>,
}
impl<'a> PushBatch<'a> {
    pub(crate) fn defer_accessory(&mut self, slot: crate::accessory_extract::Deferred<'a>) {
        self.slots.push((self.groups.len(), slot));
    }
    pub(crate) fn flush(&mut self, parts: &mut Vec<ManifestPart>, session_parts: &mut Vec<crate::session::SessionPart>,
        seen: &mut std::collections::HashSet<String>) -> Result<(), String> {
        use rayon::prelude::*;
        // The kept accessory slots: their colour bakes at once, then their pushes in their places, in call order.
        let mut slots = std::mem::take(&mut self.slots);
        if !slots.is_empty() {
            crate::session::native_parallel_work(|| slots.par_iter_mut().for_each(|(_, slot)| slot.color()));
            let kept = std::mem::take(&mut self.groups);
            let mut kept = kept.into_iter();
            let mut taken = 0;
            for (at, slot) in slots {
                while taken < at { if let Some(g) = kept.next() { self.groups.push(g); } taken += 1; }
                slot.finish(parts, session_parts, seen, self)?;
            }
            self.groups.extend(kept);
        }
        let groups = std::mem::take(&mut self.groups);
        let (ctxs, works): (Vec<PushCtx<'a, 'static>>, Vec<Vec<PushWork>>) = groups.into_iter().unzip();
        let items: Vec<(usize, PushWork)> = works.into_iter().enumerate().flat_map(|(g, w)| w.into_iter().map(move |item| (g, item))).collect();
        // An accessory item's transform depends only on the mesh and the call's inputs: the same item in the view's next
        // opens (a conversion opens every outfit in six garment states, the accessories mostly the same each time) takes
        // the transformed mesh kept by content (~1500 transforms a card, ~5 s, before).

        let ctx_keys: Vec<Option<[u64; 2]>> = ctxs.iter().map(|ctx| ctx.memo_fingerprint()).collect();
        let keys: Vec<Option<String>> = crate::session::native_parallel_work(|| items.par_iter().map(|(g, item)| ctx_keys[*g].map(|c| {
            let mut h = StreamHash::default();
            {
                let mut w = std::io::BufWriter::with_capacity(1 << 16, &mut h);
                // (obj and texture_plan are not serialized with the mesh)
                let _ = bincode::serialize_into(&mut w, &(c, item.2, item.3, &item.1, &item.1.obj, &item.1.texture_plan));
                let _ = std::io::Write::flush(&mut w);
            }
            let [a, b] = h.finish();
            format!("push_acc|{a:016x}{b:016x}")
        })).collect());
        let mut done: Vec<Option<(usize, PushDone)>> = Vec::with_capacity(items.len());
        let mut todo = Vec::new();
        for (n, ((g, item), key)) in items.into_iter().zip(keys).enumerate() {
            match key.as_deref().and_then(crate::view_memo_get::<(ExtractedMesh, Option<crate::session::AccessoryRuntime>)>) {
                Some((mesh, runtime)) => done.push(Some((g, (item.0, mesh, runtime)))),
                None => { done.push(None); todo.push((n, g, item, key)); }
            }
        }
        if profile_enabled() { eprintln!("ok-extract: PUSHMEMO hits={} misses={} keyed_misses={}", done.iter().filter(|d| d.is_some()).count(), todo.len(), todo.iter().filter(|t| t.3.is_some()).count()); }
        let ran: Vec<(usize, usize, Option<String>, PushDone)> = crate::session::native_parallel_work(||
            todo.into_par_iter().map(|(n, g, item, key)| (n, g, key, ctxs[g].run_on_worker(item))).collect());
        for (n, g, key, d) in ran {
            if let Some(key) = key { crate::view_memo_put(key, (d.1.clone(), d.2.clone())); }
            done[n] = Some((g, d));
        }
        let mut by_group: Vec<Vec<PushDone>> = (0..ctxs.len()).map(|_| Vec::new()).collect();
        for (g, d) in done.into_iter().flatten() { by_group[g].push(d); }
        for (ctx, done) in ctxs.iter().zip(by_group) { ctx.emit(done, parts, session_parts)?; }
        Ok(())
    }
}

/// A streamed 96-bit content hash (CRC-32 and a four-lane multiply-rotate mix, with the length) for memo keys of
/// large values: bincode writes the value straight into it.
#[derive(Default)]
pub(crate) struct StreamHash { crc: crc32fast::Hasher, lanes: [u64; 4], buf: [u8; 32], n: usize, total: u64 }
impl StreamHash {
    fn block(&mut self, b: &[u8; 32]) {
        for (i, lane) in self.lanes.iter_mut().enumerate() {
            let w = u64::from_le_bytes(b[i * 8..i * 8 + 8].try_into().unwrap());
            *lane = (*lane ^ w).wrapping_add(0x9E37_79B9_7F4A_7C15 ^ i as u64).wrapping_mul(0xFF51_AFD7_ED55_8CCD).rotate_left(29);
        }
    }
    pub(crate) fn finish(mut self) -> [u64; 2] {
        let mut last = [0u8; 32];
        last[..self.n].copy_from_slice(&self.buf[..self.n]);
        last[24..].copy_from_slice(&(self.total ^ ((self.n as u64) << 56)).to_le_bytes());
        self.block(&last);
        let mut h = self.lanes[0];
        for lane in &self.lanes[1..] { h = (h ^ lane.rotate_left(17)).wrapping_mul(0xC4CE_B9FE_1A85_EC53); }
        [h, ((self.crc.finalize() as u64) << 32) | (self.total & 0xFFFF_FFFF)]
    }
}
impl std::io::Write for StreamHash {
    fn write(&mut self, mut data: &[u8]) -> std::io::Result<usize> {
        let len = data.len();
        self.crc.update(data);
        self.total += len as u64;
        if self.n > 0 {
            let take = (32 - self.n).min(data.len());
            self.buf[self.n..self.n + take].copy_from_slice(&data[..take]);
            self.n += take; data = &data[take..];
            if self.n < 32 { return Ok(len); }
            let b = self.buf; self.block(&b); self.n = 0;
        }
        let mut chunks = data.chunks_exact(32);
        for c in &mut chunks { self.block(c.try_into().unwrap()); }
        let rest = chunks.remainder();
        self.buf[..rest.len()].copy_from_slice(rest); self.n = rest.len();
        Ok(len)
    }
    fn flush(&mut self) -> std::io::Result<()> { Ok(()) }
}

impl<'a, 's> PushCtx<'a, 's> {
    /// Hash of everything an accessory item's transform reads besides the mesh (None: not kept, see flush). The garment
    /// states are not in it: an accessory's transform reads them only for an outer garment's alpha mask, which an
    /// accessory call does not carry. Nor are the outfit and the card property: the transform reads them only to find
    /// the slot's MaterialEditor rows, which are in it (the same hair in every outfit transforms once).
    fn memo_fingerprint(&self) -> Option<[u64; 2]> {
        if !self.role.contains("acc") || self.alpha_mask.is_some() || self.out_dir.is_some() { return None; }
        // The MaterialEditor rows me_texture / me_maintex and the colour pass read: (object type, coordinate, slot).
        let (ot, slot) = (me_object_type(&self.role), me_slot(&self.role, self.property.as_deref()));
        let coord = if ot == Some(4) { 0 } else { self.outfit };
        let matches = |&(t, c, s, _, _): &(i32, i32, i32, String, String)| Some(t) == ot && c == coord && Some(s) == slot;
        let mut tex: Vec<_> = self.me_tex.into_iter().flatten().filter(|(k, _)| matches(k))
            .map(|(k, v)| (&k.3, &k.4, serde_bytes::Bytes::new(v.as_slice()))).collect();
        tex.sort_by(|a, b| (a.0, a.1).cmp(&(b.0, b.1)));
        let mut color: Vec<_> = self.me_color.into_iter().flatten().filter(|(k, _)| matches(k)).map(|(k, v)| (&k.3, &k.4, *v)).collect();
        color.sort_by(|a, b| (a.0, a.1).cmp(&(b.0, b.1)));
        let mut h = StreamHash::default();
        let mut w = std::io::BufWriter::with_capacity(1 << 16, &mut h);
        bincode::serialize_into(&mut w, &((self.game_root.to_string_lossy(), &self.role, self.category_no, &self.guid, ot, slot,
            self.me_tex.is_some(), self.me_color.is_some(), tex, color, self.body_skel.as_deref(), self.head_skel.as_deref()),
            (&self.parent_key, self.hair_color, &self.part_skel, self.part_local,
            &self.accessory_assignment, self.convert_normals, &self.acc_place, &self.overrides, self.hair_gloss_sampling))).ok()?;
        std::io::Write::flush(&mut w).ok()?;
        drop(w);
        Some(h.finish())
    }
    fn run_on_worker(&self, item: PushWork) -> PushDone {
        let _profile = crate::profile::scope(&self.overrides);
        let _active = self.clothes_active.map(crate::unity::set_session_clothes_states);
        let _requested = self.clothes_requested.map(crate::unity::request_clothes_states);
        let (t, name) = (std::time::Instant::now(), item.1.name.clone());
        let done = self.transform(item);
        if std::env::var_os("OK_A2_TRACE").is_some() { eprintln!("ok-extract: A2 item {} {} {:.1}ms thread={:?}", self.role, name, t.elapsed().as_secs_f64()*1000., std::thread::current().id()); }
        done
    }
    /// The runtime rig and texture transforms of one mesh.
    fn transform(&self, (i, mut mesh, source_program, native_hair_color): PushWork) -> PushDone {
        let (role, property, game_root, outfit) = (self.role.as_str(), &self.property, self.game_root, self.outfit);
        let (body_skel, head_skel, part_skel, part_local) = (self.body_skel.as_deref(), self.head_skel.as_deref(), self.part_skel.as_ref(), self.part_local.as_ref());
        let (parent_key, hair_color, me_tex, me_color) = (self.parent_key.as_deref(), self.hair_color, self.me_tex, self.me_color);
        let (accessory_assignment, acc_place, convert_normals) = (self.accessory_assignment.as_ref(), &self.acc_place, self.convert_normals);
        let alpha_mask = self.alpha_mask.as_ref().map(|(mask, scale, offset)| (mask.as_slice(), *scale, *offset));
            let mut runtime_accessory = if role.contains("acc")
                && std::env::var("OK_RUNTIME_ACCESSORY").ok().as_deref() != Some("0") {
                let key = parent_key.unwrap_or("");
                let parent = bone_world(body_skel, head_skel, key);
                let locals = acc_place.clone()
                    .or_else(|| part_local.map(|l| [*l, identity_move()]));
                match (parent, locals) {
                    (Some(parent), Some(locals)) => {
                        if let Some(binding)=accessory_assignment.filter(|binding|ids_hit(&mesh.renderer_ids,&binding.renderers)) {
                            crate::session::AccessoryRuntime::from_assigned_accessory(&mesh,part_skel,key,&locals,&parent,binding.armature_root)
                        } else {crate::session::AccessoryRuntime::from_source(&mesh,part_skel,key,&locals,&parent)}
                    },
                    _ => None,
                }
            } else if role.contains("hair") {
                bone_world(body_skel, head_skel, "cf_J_FaceUp_ty").and_then(|parent|
                    crate::session::AccessoryRuntime::from_hair_source(&mesh, part_skel, &parent))
            } else if role.starts_with("clothes") {
                let (copy_dynamic, component) = clothes_load_settings(property.as_deref().unwrap_or(""));
                body_skel.and_then(|body| crate::session::AccessoryRuntime::from_clothes_source(
                    &mesh, part_skel, body, copy_dynamic, component, part_local, crate::dbone::implant::installed(game_root)))
            } else { None };
            if role.starts_with("clothes") {if let Some(runtime)=&mut runtime_accessory {runtime.coordinate_clothes();}}
            if let Some(runtime) = &mut runtime_accessory {
                let flags = crate::dbone::game_colliders::Flags::default();
                if let Some((positions, normals,tangents)) = runtime.update_render(body_skel, head_skel, &crate::dbone::BodyColliderSet::default(), flags, 0.0) {
                    mesh.positions = positions; mesh.normals = normals;mesh.tangents=tangents;
                }
            } else {
                rig_mesh(&mut mesh, role, body_skel, head_skel, parent_key, part_skel, part_local, None, acc_place.clone());
            }
            if std::env::var("OK_MATDIAG").is_ok() {
                if let Some(mm) = mesh.mat.as_ref() {
                    let slots: Vec<&str> = mm.tex_slots.iter().map(|(k, _)| k.as_str()).collect();
                    eprintln!(
                        "ok-extract: MAT {} role={} shader={} q={} kw={} colors={:?} slots={:?}",
                        mesh.name,
                        role,
                        mm.shader,
                        mm.render_queue,
                        mm.keywords.join(","),
                        mm.colors,
                        slots
                    );
                }
            }
            // Accessory colour is baked in `color_accessory_meshes`. A second
            // name-contains-hair tint here would multiply ColorMatch / ME again.
            let hair_like = role.contains("hair") && !role.contains("acc");
            if hair_like && std::env::var("OK_HAIR_DUMP").is_ok() {
                let stem: String = mesh
                    .name
                    .chars()
                    .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
                    .collect();
                if let Some(t) = mesh.texture_png.as_deref() {
                    let _ = std::fs::write(
                        format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\hd_main_{stem}.png"),
                        t,
                    );
                }
                if let Some(t) = mesh.color_mask_png.as_deref() {
                    let _ = std::fs::write(
                        format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\hd_mask_{stem}.png"),
                        t,
                    );
                }
            }
            if source_program || native_hair_color {
                // Both shader families sample the original full-resolution mask.
                if hair_like {
                    if let Some(cols) = hair_color {
                        for (key, color) in ["_Color", "_Color2", "_Color3"].iter().zip(cols.iter()) {
                            set_material_color(&mut mesh, key, *color);
                        }
                    }
                    if mesh.texture_png.is_none() {
                        mesh.texture_png = crate::unity::solid_png([1.0; 4], 8, 8);
                    }
                }
            } else if hair_like && std::env::var("OK_HAIR_RAW").is_ok() {
                // diagnostic: keep the raw hair texture (skip the colour-mask tint)
            } else if hair_like {
                if let Some(cols) = hair_color {
                    let white = crate::unity::solid_png([1.0, 1.0, 1.0, 1.0], 8, 8);
                    let png = mesh.texture_png.as_deref().or(white.as_deref());
                    if let Some(png) = png {
                        if let Some(tinted) = crate::unity::hair_get_diffuse(
                            png,
                            mesh.color_mask_png.as_deref(),
                            cols[0],
                            cols[1],
                            cols[2],
                        ) {
                            mesh.texture_png = Some(tinted);
                        }
                    } else {
                        mesh.texture_png = crate::unity::solid_png(cols[0], 64, 64);
                    }
                }
            } else if role.starts_with("clothes") {
                if let Some(cols) = hair_color {
                    if let Some(png) = mesh.texture_png.as_ref() {
                        if let Some(tinted) = crate::unity::tint_png(png, cols[0]) {
                            mesh.texture_png = Some(tinted);
                        }
                    }
                }
            }
            // Accessories already swapped ME MainTex inside `color_accessory_meshes`
            // and baked ColorMask on top. Replacing again would drop that bake.
            if role.contains("acc") {
                // already baked
            } else if let Some(png) = me_maintex(me_tex, outfit, role, property.as_deref(), &mesh) {
                eprintln!(
                    "ok-extract: ME MainTex role={role} name={} mat={} bytes={}",
                    mesh.name,
                    mesh.mat.as_ref().map(|m| m.name.as_str()).unwrap_or(""),
                    png.len()
                );
                mesh.texture_png = Some(png.to_vec());
                mark_material_editor_main_texture(&mut mesh);
            }
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "detailmask") {
                mesh.detail_mask_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_DetailMask");}
                else {mesh.runtime_textures.remove("_DetailMask");}
            }
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "linemask") {
                mesh.line_mask_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_LineMask");}
            }
            // A resolved material's null NormalMap is authoritative. A decoded
            // normal retained from an earlier renderer/material must not survive
            // a null binding. A later ME assignment still replaces it below.
            clear_unbound_normal_map(&mut mesh);
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "normalmap") {
                mesh.normal_map_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_NormalMap");}
            }
            if let Some(png) = me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "alphamask") {
                mesh.alpha_mask_png = Some(png.to_vec());
                if source_program {mark_material_editor_texture(&mut mesh,"_AlphaMask");}
            }
            // MaterialEditor replaces _NormalMap with a blitted RenderTexture after
            // it applies texture overrides. Its normal_convert fragment writes
            // (1, source.g, source.b, source.a), not just a vertically flipped
            // source texture. Match the texture actually sampled by the game.
            if convert_normals {
                if let Some(png) = mesh.normal_map_png.take() {
                    let overridden=me_texture(me_tex,outfit,role,property.as_deref(),&mesh,"normalmap").is_some();
                    let srgb=overridden || mesh.runtime_textures.get("_OKSourceNormalMap").is_some_and(|s|s[3]==1);
                    let converted=convert_runtime_normal_map_rows(png,srgb,!overridden);
                    // MaterialEditor blits a linear normal source into a sRGB
                    // RenderTexture. Its stored RGB is sRGB encoded, then the
                    // native shader samples it through a sRGB sampler. Keep the
                    // legacy normal representation unchanged.
                    mesh.normal_map_png=Some(if source_program {
                        mark_native_runtime_normal_texture(&mut mesh,"_NormalMap");
                        linear_texture_to_srgb_png(&converted).unwrap_or(converted)
                    } else {converted});
                }
            }
            if role.contains("acc") {
                // Static Unity Texture2D rows are bottom-up. Accessory colour
                // baking preserves those rows; card ME PNGs are already top-down.
                // Normalize at the rendering boundary, before paired alpha masks,
                // because UE sends (u,1-v) for the native mesh UVs.
                if me_maintex(me_tex, outfit, role, property.as_deref(), &mesh).is_none() {
                    if let Some(png) = mesh.texture_png.take() {
                        mesh.texture_png = Some(flip_png_v(&png).unwrap_or(png));
                    }
                }
                let flip: [bool; 4] = ["detailmask", "linemask", "alphamask", "normalmap"]
                    .map(|key| me_texture(me_tex, outfit, role, property.as_deref(), &mesh, key).is_none());
                for (needs_flip, data) in [
                    // ParsedBundle already calls orient_raw_mask for these
                    // authored Texture2D slots. The legacy accessory bake expects
                    // its old orientation; the native shader samples the source
                    // UV and must not reverse the rows for a second time.
                    (flip[0] && !source_program, &mut mesh.detail_mask_png),
                    (flip[1] && !source_program, &mut mesh.line_mask_png),
                    (flip[2] && !source_program, &mut mesh.alpha_mask_png),
                    (flip[3] && !convert_normals, &mut mesh.normal_map_png),
                    (true, &mut mesh.hair_gloss_png),
                ] {
                    if needs_flip {
                        if let Some(png) = data.take() {
                            *data = Some(flip_png_v(&png).unwrap_or(png));
                        }
                    }
                }
            }
            if source_program || native_hair_color {
                if let Some(overrides)=me_color {
                    if let (Some(ot),Some(s),Some(mat))=(me_object_type(role),me_slot(role,property.as_deref()),mesh.mat.as_mut()) {
                        let material=crate::card::me_mat_key(&mat.name);
                        for ((t,coord,slot,name,prop),value) in overrides {
                            if *t==ot && *coord==(if ot==4 {0}else{outfit}) && *slot==s && *name==material {
                                let requested=if prop.starts_with('_'){prop.clone()}else{format!("_{prop}")};
                                // MeColorMap intentionally normalizes names. Replace
                                // the authored canonical key so exact-case lookup
                                // cannot keep the pre-ME value instead.
                                let key=mat.colors.keys().filter(|k|k.eq_ignore_ascii_case(&requested)).min().cloned().unwrap_or(requested);
                                mat.colors.insert(key,*value);
                            }
                        }
                    }
                }
                let me_color_mask = me_texture(me_tex,outfit,role,property.as_deref(),&mesh,"colormask");
                if native_hair_color || source_program {
                    if let Some(png) = me_color_mask {
                        mesh.color_mask_png = Some(png.to_vec());
                        // SetTexture changes the source to a runtime texture.
                        if source_program {mark_material_editor_texture(&mut mesh,"_ColorMask");}
                        else {mesh.runtime_textures.remove("_ColorMask");}
                    }
                }
                if let Some(png)=mesh.color_mask_png.take() {
                    mesh.color_mask_png=Some(if me_color_mask.is_some() {
                        png
                    } else {flip_png_v(&png).unwrap_or(png)});
                }
            }
            if source_program {
                for name in ["_NormalMapDetail","_AnotherRamp","_EmissionMask","_ReflectionMapCap","_ReflectMapDetail","_Texture2","_Texture3","_liquidmask"] {
                    let property_name=name.trim_start_matches('_').to_ascii_lowercase();
                    let override_png=me_texture(me_tex,outfit,role,property.as_deref(),&mesh,&property_name).map(|png|png.to_vec());
                    let is_override=override_png.is_some();
                    let data=override_png.or_else(||mesh.shader_extra_textures.remove(name));
                    if let Some(data)=data {
                        if is_override {mark_material_editor_texture(&mut mesh,name);}
                        let data=if name=="_NormalMapDetail" && convert_normals {
                            let srgb=is_override || mesh.runtime_textures.get("_OKSourceNormalMapDetail").is_some_and(|s|s[3]==1);
                            mark_native_runtime_normal_texture(&mut mesh,name);
                            let converted=convert_runtime_normal_map_rows(data,srgb,!is_override);
                            linear_texture_to_srgb_png(&converted).unwrap_or(converted)
                        }else if is_override {data}else{flip_png_v(&data).unwrap_or(data)};
                        mesh.shader_extra_textures.insert(name.into(),data);
                    }
                }
                if mesh.texture_png.is_none() {mesh.texture_png=crate::unity::solid_png([1.;4],8,8);}
            } else if mesh.mat.as_ref().is_some_and(|mat| matches!(mat.shader.as_str(), "Shader Forge/main_StandardMDK_studio" | "Standard" | "Standard (Specular setup)" | "Legacy Shaders/Particles/Additive")) {
                // Like MainTex above, every MDK slot follows the native mesh's
                // (u, 1-v) rendering UV convention. Keep their rows aligned.
                // Studio props have role "part", so the accessory-only MainTex
                // normalization above did not run for them.
                if !role.contains("acc") && me_maintex(me_tex, outfit, role, property.as_deref(), &mesh).is_none() {
                    if let Some(png) = mesh.texture_png.take() {
                        mesh.texture_png = Some(flip_png_v(&png).unwrap_or(png));
                    }
                }
                for name in ["_BumpMap", "_ParallaxMap", "_MetallicGlossMap", "_EmissionMap"] {
                    if let Some(png) = mesh.shader_extra_textures.remove(name) {
                        mesh.shader_extra_textures.insert(name.into(), flip_png_v(&png).unwrap_or(png));
                    }
                }
            } else if !role.contains("acc") && mesh.mat.as_ref().is_some_and(|mat| { let family = shader_family(&mat.shader); family.contains("main_item_studio") && !family.contains("_add") }) {
                // Studio items (role "part") drawn by main_item_studio: like the MDK slots above and the accessory MainTex,
                // the main texture and colour mask follow the native mesh's (u, 1-v) UV convention (the parasol's canopy
                // sampled its wood finial and ColorMask's black half before; native: the coloured canopy). Not the light
                // effects (main_item_studio_add): their rows were already right (0001's sword light sampled the texture's
                // filled lower half, a yellow wash, once flipped here; native: the streaked upper half, a rainbow).
                if me_maintex(me_tex, outfit, role, property.as_deref(), &mesh).is_none() {
                    if let Some(png) = mesh.texture_png.take() { mesh.texture_png = Some(flip_png_v(&png).unwrap_or(png)); }
                }
                if me_texture(me_tex, outfit, role, property.as_deref(), &mesh, "colormask").is_none() {
                    if let Some(png) = mesh.color_mask_png.take() { mesh.color_mask_png = Some(flip_png_v(&png).unwrap_or(png)); }
                }
            } else if mesh.hair_shader_hint || mesh.mat.as_ref().is_some_and(|mat| is_native_hair_shader(&mat.shader)) {
                // Forge hair samples _AnotherRamp with N dot view. Preserve a
                // MaterialEditor replacement and the original texture's sampling
                // provenance independently of the global _RampG. Match the
                // session's hair_shader_hint fallback for unresolved Shader PPtrs.
                let override_png = me_texture(me_tex,outfit,role,property.as_deref(),&mesh,"anotherramp")
                    .map(|png|png.to_vec());
                let replaced = override_png.is_some();
                let data = override_png.or_else(||mesh.shader_extra_textures.remove("_AnotherRamp"));
                if let Some(data) = data {
                    if replaced { mesh.runtime_textures.remove("_AnotherRamp"); }
                    let data = if replaced { data } else { flip_png_v(&data).unwrap_or(data) };
                    mesh.shader_extra_textures.insert("_AnotherRamp".into(), data);
                }
            }
            // Baked atlases use the same (u, 1-v) UV convention as main textures.
            // Reflection atlases are constructed in sampling order, not source Texture2D order.
            for name in ["_OKLightmap", "_OKLightmapDirection"] {
                if let Some(png) = mesh.shader_extra_textures.remove(name) {
                    mesh.shader_extra_textures.insert(name.into(), flip_png_v(&png).unwrap_or(png));
                }
            }
            let material_mask = if role.starts_with("clothes") {
                mesh.alpha_mask_png.as_deref().map(|mask| (mask, 1.0, 0.0))
            } else { None };
            if source_program {
                // ChaControl assigns the equipped outer layer's mask to the
                // inner/bra renderer. Native shaders must receive that binding;
                // skipping CPU alpha baking must not drop the dynamic mask.
                if let Some((mask, v_scale, v_offset)) = alpha_mask {
                    apply_native_clothes_over_mask(&mut mesh, mask, v_scale, v_offset,
                        clothes_alpha_state(crate::unity::clothes_states()[0]));
                }
            } else if let (Some((mask, v_scale, v_offset)), Some(png)) = (alpha_mask.or(material_mask), mesh.texture_png.as_deref()) {
                let dynamic_alpha = role == "clothes_bra"
                    || property.as_deref().is_some_and(|p| p.ends_with("clothesTopParts0"));
                let flags = if dynamic_alpha {
                    clothes_alpha_state(crate::unity::clothes_states()[0])
                } else {
                    [true, true]
                };
                mesh.texture_png = apply_clothes_over_mask(png, mask, v_scale, v_offset, flags);
            } else if mesh.mat.as_ref().is_some_and(|mat| is_native_hair_shader(&mat.shader)) {
                if let (Some(mask), Some(png)) = (mesh.alpha_mask_png.as_deref(), mesh.texture_png.as_deref()) {
                    mesh.texture_png = apply_hair_alpha_mask(png, mask);
                }
            }
        (i, mesh, runtime_accessory)
    }
    /// The transformed meshes as parts, in order.
    fn emit(&self, done: Vec<PushDone>, parts: &mut Vec<ManifestPart>, session_parts: &mut Vec<crate::session::SessionPart>) -> Result<(), String> {
        let (role, property, slot, outfit, out_dir, me_color) = (self.role.as_str(), &self.property, self.slot, self.outfit, self.out_dir, self.me_color);
        let (guid, category_no, name) = (&self.guid, self.category_no, &self.name);
        for (i, mut mesh, runtime_accessory) in done {
        // Include the card property in the stem: two accessory slots can share
        // the same item id (slot 14/15 = the two Hachikuji headband halves), so
        // without it both write to the same OBJ and the first is overwritten.
        let stem = match property.as_deref() {
            Some(p) if !p.is_empty() => format!(
                "{}_{}_{}_{}_{}_sub{}",
                role,
                slot.unwrap_or(0),
                i,
                sanitize(&mesh.name),
                sanitize(p),
                mesh.submesh_index,
            ),
            _ => format!("{}_{}_{}_{}_sub{}", role, slot.unwrap_or(0), i, sanitize(&mesh.name), mesh.submesh_index),
        };
        if role.starts_with("clothes") { apply_clothes_default_main_texture(&mut mesh); }
        let (blend, unlit) = blend_for_mesh(role, &mesh);
        let (pth, tex) = emit_part(out_dir, session_parts, &stem, role, &mesh, &blend, unlit)?;
        if let Some(part)=session_parts.last_mut(){part.source_property=property.clone();}
        if let (Some(runtime), Some(part)) = (runtime_accessory, session_parts.last_mut()) {
            part.runtime_accessory = Some(runtime);
            part.rigid_follow_bone = None;
            part.rigid_follow_c = std::ffi::CString::new("").unwrap();
            // 4 = accessory skinned by its own persistent DynamicBone rig.
            part.skel_kind = 4;
        }
        if let Some(color) = me_shadow_color(me_color, outfit, role, property.as_deref(), &mesh) {
            if let Some(part) = session_parts.last_mut() {
                part.shadow_color = color;
            }
        }
        if let Some(color) = me_material_color(me_color, outfit, role, property.as_deref(), &mesh, "linecolor") {
            if let Some(part) = session_parts.last_mut() {
                part.line_color = color;
            }
        }
        parts.push(ManifestPart {
            #[cfg(test)]
            source_mesh: Some(mesh.name.clone()),
            role: role.into(),
            path: pth,
            guid: guid.clone(),
            mesh: mesh.name.clone(),
            slot,
            property: property.clone(),
            category_no,
            name: name.clone(),
            texture: tex,
            blend,
            unlit,
        });
            if let (Some(sampling), Some(part)) = (self.hair_gloss_sampling, session_parts.last_mut()) {
                if part.hair_gloss.is_some() { part.runtime_textures.insert("_HairGloss".into(), sampling); }
            }
        }
        Ok(())
    }
}

pub(crate) fn push_meshes<'a>(
    parts: &mut Vec<ManifestPart>,
    session_parts: &mut Vec<crate::session::SessionPart>,
    out_dir: Option<&'a Path>,
    game_root: &'a Path,
    role: &str,
    meshes: Vec<ExtractedMesh>,
    slot: Option<i32>,
    category_no: Option<i32>,
    guid: Option<String>,
    property: Option<String>,
    name: Option<String>,
    body_skel: Option<&crate::pose::Skeleton>,
    head_skel: Option<&crate::pose::Skeleton>,
    parent_key: Option<&str>,
    hair_color: Option<[[f32; 4]; 3]>,
    seen_meshes: &mut std::collections::HashSet<String>,
    part_skel: Option<&crate::pose::Skeleton>,
    part_local: Option<&crate::pose::Mat4>,
    me_off: Option<&std::collections::HashSet<(i32, i32, i32, String)>>,
    me_tex: Option<&'a crate::card::MeTexMap>,
    me_color: Option<&'a crate::card::MeColorMap>,
    me_shader: Option<&crate::card::MeShaderMap>,
    me_float: Option<&crate::card::MeFloatMap>,
    outfit: i32,
    alpha_mask: Option<(&[u8], f32, f32)>,
    accessory_assignment: Option<&crate::unity::AccessoryBodyBinding>,
    hair_gloss_sampling: Option<[i32; 5]>,
    batch: Option<&mut PushBatch<'a>>,
) -> Result<(), String> {let _spot=crate::profile::spot("push_meshes");
    check_preempted()?;
    let convert_normals = material_editor_converts_normals(game_root);
    let remove_rim = material_editor_removes_rim(game_root);
    let mut work = Vec::with_capacity(meshes.len());
    for (i, mut mesh) in meshes.into_iter().enumerate() {
        if is_dummy_part(&mesh.name, name.as_deref()) || is_hidden_part(&mesh.name) {
            continue;
        }
        if me_hides_mesh(me_off, outfit, role, property.as_deref(), &mesh) {
            eprintln!(
                "ok-extract: skip material-editor off role={role} name={} renderers={:?}",
                mesh.name, mesh.renderer_names
            );
            continue;
        }
        apply_me_shader(game_root, me_shader, outfit, role, property.as_deref(), &mut mesh);
        if let Some(overrides)=me_float { apply_me_floats(overrides,outfit,role,property.as_deref(),&mut mesh); }
        apply_material_editor_remove_rim(remove_rim, me_float, outfit, role, property.as_deref(), &mut mesh);
        let source_program=mesh.mat.as_ref().is_some_and(|m|matches!(m.shader.to_ascii_lowercase().as_str(),"xukmi/mainitemplus"|"shader forge/main_item"|"shader forge/main_alpha"|"shader forge/main_opaque"));
        let native_hair_color=mesh.mat.as_ref().is_some_and(|m|
            is_native_hair_shader(&m.shader)) || mesh.hair_shader_hint;
        // Head/face renderers are emitted from the head prefab. An accessory
        // or a second SMR with the same name (head mod + vanilla, or a
        // disabled colour variant that slipped through) must not be drawn again.
        // `cf_O_face` is only emitted by the face block below, with the composed
        // texture. A later clothes/head-mod pass used to emit the same name with
        // the bundle's raw texture and the editor drew that one.
        let bare = mesh.name.to_ascii_lowercase();
        if bare == "cf_o_face" {
            continue;
        }
        if is_head_detail(&mesh.name) && seen_meshes.contains(&bare) {
            continue;
        }
        // Key duplicates by mesh name AND the card property. Two accessory
        // slots can share the same mesh (e.g. the two Hachikuji headbands at
        // slots 14/15 are the left/right halves) and both must be emitted;
        // without the property the second slot is dropped and the accessory
        // renders as half.
        let material_key = mesh.mat.as_ref().map(|m| crate::card::me_mat_key(&m.name)).unwrap_or_default();
        let mesh_key = match property.as_deref() {
            Some(p) if !p.is_empty() => format!("{}|{p}|sub{}|{material_key}", mesh.name.to_ascii_lowercase(), mesh.submesh_index),
            _ => format!("{}|sub{}|{material_key}", mesh.name.to_ascii_lowercase(), mesh.submesh_index),
        };
        if !seen_meshes.insert(mesh_key) {
            continue;
        }
        work.push((i, mesh, source_program, native_hair_color));
    }
    // The call's inputs, the skeletons borrowed (immediate) or owned (kept by the batch).
    macro_rules! push_ctx {($body:expr, $head:expr) => {PushCtx { out_dir, game_root, role: role.to_string(), slot, category_no,
        guid: guid.clone(), property: property.clone(), name: name.clone(), body_skel: $body, head_skel: $head,
        parent_key: parent_key.map(str::to_string), hair_color, part_skel: part_skel.cloned(), part_local: part_local.copied(),
        me_tex, me_color, outfit, alpha_mask: alpha_mask.map(|(mask, scale, offset)| (mask.to_vec(), scale, offset)),
        accessory_assignment: accessory_assignment.cloned(), convert_normals,
        acc_place: ACC_PLACE.with(|c| c.borrow().clone()), clothes_active: crate::unity::active_clothes_states(),
        clothes_requested: crate::unity::explicit_requested_clothes_states(), overrides: crate::profile::snapshot(), hair_gloss_sampling }}}
    if first_load_option("A2") {
        if let Some(batch) = batch {
            use std::borrow::Cow;
            batch.groups.push((push_ctx!(body_skel.map(|s| Cow::Owned(s.clone())), head_skel.map(|s| Cow::Owned(s.clone()))), work));
            return Ok(());
        }
    }
    let ctx = push_ctx!(body_skel.map(std::borrow::Cow::Borrowed), head_skel.map(std::borrow::Cow::Borrowed));
    let done: Vec<PushDone> = work.into_iter().map(|item| ctx.transform(item)).collect();
    ctx.emit(done, parts, session_parts)
}

pub(crate) fn lookup_entry(
    vanilla: &ChaListCatalog,
    zip_lists: &mut std::collections::HashMap<String, ChaListCatalog>,
    zip_path: Option<&Path>,
    guid: &str,
    category_no: i32,
    slot: i32,
    local_slot: i32,
) -> Option<ListEntry> {
    if let Some(zp) = zip_path {
        let cat = zip_lists
            .entry(guid.to_string())
            // (once per view and archive, crate::pack_memo: callers often pass a fresh map, and every open of a conversion's 45 parsed the
            // card's zipmod lists again; the view's first open read them)
            .or_insert_with(|| (*crate::pack_memo(format!("catalog_from_zipmod|{}", zp.display()), || std::sync::Arc::new(catalog_from_zipmod(zp)))).clone());
        let hit = cat.get_slot(category_no, slot, local_slot).cloned();
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: lookup guid={guid} cat={category_no} slot={slot} local={local_slot} zip_hit={:?}",
                hit.as_ref().map(|e| (&e.name, &e.main_data, &e.main_ab))
            );
        }
        if let Some(e) = hit {
            return Some(e);
        }
        // A KKEx resolve pins this slot to a mod. Do NOT silently fall back to
        // the vanilla catalog: that loads a same-named vanilla mesh instead of
        // the mod prefab and hides the mistake. Report and skip.
        eprintln!(
            "ok-extract: ERROR KKEx entry missing guid={guid} cat={category_no} slot={slot} local={local_slot} zip={}",
            zp.display()
        );
        return None;
    }
    vanilla.get_slot(category_no, slot, local_slot).cloned()
}

pub(crate) fn outfit_prefix(outfit: i32) -> String {
    format!("outfit{outfit}.")
}
