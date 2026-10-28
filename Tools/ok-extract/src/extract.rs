//! The open: a card (and outfit) to an `OkSession`, stage by stage (OK_PROFILE names the stages). Being split into
//! per-stage functions (Docs/OK_EXTRACT_REFACTOR_PLAN.md); the items it uses live in the crate root.
use super::*;

/// Runtime APNG editing already owns the exact current card metadata. Reuse
/// the same extraction pipeline without saving a temporary source PNG or
/// resolving a stale filename/SQL card cache. The path is report provenance
/// only; neither extraction nor accessory context creation depends on it.
pub(crate) fn extract_session_from_info(
    game_root:&Path,card_path:Option<&Path>,outfit:i32,out_dir:Option<&Path>,
    mut parsed_card:Option<CardInfo>,
)->Result<crate::session::OkSession,String>{
    if !game_root.join("abdata").ok_is_dir(){
        return Err(format!("game_root missing abdata: {}",game_root.display()));
    }
    if let Some(card)=parsed_card.as_mut(){crate::mods::apply_strip_all_migrations(game_root,card);}
    if game_root.join("require_complete_mods").ok_is_file() {
        if let Some(card)=parsed_card.as_ref() {
            let wanted:std::collections::HashSet<String>=card.resolves.iter().map(|r|r.mod_id.clone()).filter(|g|!g.is_empty()).collect();
            let found=crate::mods::find_guids(&game_root.join("mods"),Some(&wanted));
            let mut missing:Vec<_>=wanted.into_iter().filter(|g|!found.contains_key(g)).collect();missing.sort();
            if !missing.is_empty(){return Err(format!("missing adjacent zipmod GUIDs: {}",missing.join(", ")));}
            // Body descriptor GUIDs belong to the selector registry, not the
            // package manifest registry. Use the same resolver as extraction.
            if let Some(g)=card.body_guid.as_deref().filter(|g|!g.is_empty()&&!matches!(*g,"Default.Body.Female"|"Default.Body.Male")) {
                if crate::mods::uncensor_body_for_card(game_root,Some(g),card.sex).is_none() {
                    return Err(format!("missing adjacent zipmod body descriptor: {g}"));
                }
            }
        }
    }
    // Within one view (a card pack mounted: a conversion, or an OK card opened again) the entries answer the same, so
    // what an earlier open of it extracted is kept: a conversion opens every outfit, and outfits 1.. extracted outfit
    // 0's accessories again. Their reads were recorded by that first open into the same pack. Any other open starts
    // afresh (begin_load still clears on a changed reference signature).
    let view = crate::oklib::mounted_pack_identity();
    let epoch = format!("{}|{view}|{}|{}", game_root.display(), crate::oklib::mounted_pack_mods_identity(), crate::dbone::dynamic_bones_fix_enabled(game_root));
    let previous = ENTRY_EPOCH.with(|e| e.replace(epoch.clone()));
    if view.is_empty() || previous != epoch { clear_entry_memo(); }
    crate::unity::references::begin_load(game_root);
    let t_all = Instant::now();
    // OK_PROFILE: the per-slot stage timings (appearance::timings) of this open too.
    let stages = std::env::var_os("OK_PROFILE").map(|_| crate::appearance::timings::Capture::begin());
    let mut t = Instant::now();
    if let Some(dir) = out_dir {
        std::fs::create_dir_all(dir).map_err(|e| format!("mkdir: {e}"))?;
        if let Ok(rd) = crate::gamefs::read_dir(dir) {
            for entry in rd.flatten() {
                let p = entry.path();
                if let Some(ext) = p.extension().and_then(|e| e.to_str()) {
                    if matches!(ext, "obj" | "png" | "json") {
                        let _ = std::fs::remove_file(p);
                    }
                }
            }
        }
    }
    let mut session_parts: Vec<crate::session::SessionPart> = Vec::new();
    let mut manifest = Manifest::default();
    manifest.clothes_states = crate::unity::clothes_states();
    manifest.shoes_type = crate::unity::requested_shoes_type();
    let mut parts: Vec<ManifestPart> = Vec::new();
    let mut skin_tint = [0.78, 0.56, 0.46, 1.0];
    let mut skin_sub = skin_tint;
    profile_tick("parse_card", t);
    t = Instant::now();
    EYES_OPEN_MAX_BITS.store(
        eye_controller_open_max(parsed_card.as_ref().map_or(1.0,|c| c.eyes_open_max)).to_bits(),
        std::sync::atomic::Ordering::Relaxed,
    );
    EYEBROW_OPEN_MAX_BITS.store(
        parsed_card.as_ref().map_or(1.0,|c| c.eyebrow_open_max).clamp(0.0,1.0).to_bits(),
        std::sync::atomic::Ordering::Relaxed,
    );
    if let Some(c) = parsed_card.as_ref() {
        skin_tint = c.skin_color;
        skin_sub = c.skin_sub_color;
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!("ok-extract: card eyesOpenMax={}", c.eyes_open_max);
        }
    }

    let t_sub = Instant::now();
    let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom"));
    profile_tick("lists: vanilla", t_sub);
    let t_sub = Instant::now();
    let ramp_png = configured_ramp_png(game_root, &vanilla);
    profile_tick("lists: ramp", t_sub);
    if std::env::var("OK_DUMP_HAIR_INPUTS").as_deref() == Ok("1")
        || std::env::var("OK_DUMP_MATERIAL_INPUTS").as_deref() == Ok("1") {
        if let (Some(dir), Some(bytes)) = (out_dir, ramp_png.as_deref()) {
            std::fs::write(dir.join("toon-ramp.png"), &*crate::teximg::as_png(bytes)).map_err(|e| e.to_string())?;
        }
    }
    eprintln!("ok-extract: global toon ramp bytes={}", ramp_png.as_ref().map_or(0, Vec::len));
    let mods_dir = game_root.join("mods");
    let t_sub = Instant::now();
    let mod_catalog = crate::list::load_mods_catalog_shared(&mods_dir);
    let mod_catalog = &*mod_catalog;
    profile_tick("lists: mods catalog", t_sub);
    let t_sub = Instant::now();
    let guid_index: std::collections::HashMap<String, PathBuf> = parsed_card
        .as_ref()
        .map(|c| {
            if c.resolves.is_empty() {
                Default::default()
            } else {
                let wanted: std::collections::HashSet<String> =
                    c.resolves.iter().map(|r| r.mod_id.clone()).collect();
                crate::mods::find_guids(&mods_dir, Some(&wanted))
            }
        })
        .unwrap_or_default();
    // Resolve the list entries before creating body masks or reading prefab
    // variants. The game derives state availability and linked clothing from
    // these entries before DrawClothes, including for clothes later hidden.
    profile_tick("lists: guids", t_sub);
    let t_sub = Instant::now();
    let selected_clothes = parsed_card.as_ref().map(|card|
        select_card_clothes(card, outfit, &vanilla, &mod_catalog, &guid_index));
    profile_tick("lists: select clothes", t_sub);
    let selected_top_type: i32 = selected_clothes.as_ref()
        .and_then(|slots| slots[0].as_ref())
        .and_then(|(entry, _, _)| entry.info("Kind").parse().ok()).unwrap_or(0);
    let selected_top_subparts = parsed_card.as_ref().map(|card|
        select_card_top_subparts(card, outfit, selected_top_type,
            &vanilla, &mod_catalog, &guid_index));
    let mut state_types = std::array::from_fn(|kind| selected_clothes.as_ref()
        .and_then(|slots| slots[kind].as_ref())
        .and_then(|(entry, _, _)| entry.info("StateType").trim().parse::<u8>().ok()));
    let linked_not_bot = selected_clothes.as_ref().and_then(|slots| slots[0].as_ref())
        .is_some_and(|(entry, _, _)| entry.info("Coordinate").trim() == "2");
    let linked_not_shorts = selected_clothes.as_ref().and_then(|slots| slots[2].as_ref())
        .is_some_and(|(entry, _, _)| entry.info("Coordinate").trim() == "2");
    let requested_states = crate::unity::requested_clothes_states();
    let mut reference_flags = [false; 4];
    if requested_states != [0; 9] {
        if let Some(slots) = selected_clothes.as_ref() {
            reference_flags = selected_clothes_reference_flags(game_root, slots);
            let top_not_bra = slots[0].as_ref()
                .is_some_and(|(entry, _, _)| entry.info("NotBra").trim() == "1");
            let sub_not_bra: [Option<bool>; 2] = std::array::from_fn(|kind|
                selected_top_subparts.as_ref()
                    .and_then(|sub| sub[kind].as_ref())
                    .and_then(|(_, entry, _, _)|
                        (!is_dummy_part(&entry.main_data, Some(&entry.name)))
                            .then(|| entry.info("NotBra").trim() == "1")));
            let not_bra = match selected_top_type {
                1 => sub_not_bra[0].unwrap_or(top_not_bra),
                2 if sub_not_bra.iter().any(Option::is_some) =>
                    sub_not_bra.iter().any(|value| *value == Some(true)),
                _ => top_not_bra,
            };
            state_types = adjust_clothes_state_types(
                state_types, reference_flags, not_bra, linked_not_shorts);
        }
    }
    let resolved_states = resolve_clothes_states(
        requested_states, state_types,
        linked_not_bot, linked_not_shorts);
    let _clothes_scope = crate::unity::set_session_clothes_states(resolved_states);
    manifest.clothes_states = resolved_states;
    // Whatever way this extraction ends (an error, a preempted background open), its helpers finish before it
    // returns, while its card pack is still mounted.
    let _prefetch_guard = PrefetchGuard;
    // Predict the hair/accessory (KKEx resolves) and clothing items the item passes extract, with the same
    // filters, lookups and shake switches, and start extracting them on helper threads now (`prefetch_entries`),
    // so they run while this thread builds the body, face and uncensor parts.
    if let Some(card) = parsed_card.as_ref() {
        let want_outfit = outfit_prefix(outfit);
        let mut items: Vec<(Option<PathBuf>, ListEntry, bool)> = Vec::new();
        let mut lists: std::collections::HashMap<String, ChaListCatalog> = std::collections::HashMap::new();
        let mut seen = std::collections::HashSet::new();
        for resolve in &card.resolves {
            if !is_mesh_category(resolve.category_no) { continue; }
            let p = resolve.property.to_ascii_lowercase();
            if p.contains("outfit") && !p.contains(&want_outfit) { continue; }
            let role = role_for_category(resolve.category_no, &resolve.property);
            if role.starts_with("clothes") || is_dummy_part("", Some(&resolve.name)) { continue; }
            if !seen.insert(format!("{}:{}:{}", resolve.category_no, resolve.slot, resolve.property)) { continue; }
            let zip_path = guid_path(&guid_index, &resolve.mod_id);
            let Some(entry) = lookup_entry(&vanilla, &mut lists, zip_path.map(|p| p.as_path()), &resolve.mod_id,
                resolve.category_no, resolve.slot, resolve.local_slot) else { continue };
            let shake = if role.starts_with("acc") {
                accessory_slot_from_property(&resolve.property).and_then(|s| card.accessory.get(s)).is_none_or(|p| !p.no_shake)
            } else {
                !(101..=104).contains(&resolve.category_no) || !card.hair_parts[(resolve.category_no - 101) as usize].no_shake
            };
            items.push((zip_path.cloned(), entry, shake));
        }
        if let Some(slots) = selected_clothes.as_ref() {
            for (kind, slot) in slots.iter().enumerate() {
                let Some((entry, _, zip_path)) = slot.as_ref() else { continue };
                if !crate::unity::clothes_kind_visible(kind, &manifest.clothes_states, manifest.shoes_type)
                    || is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
                let zip = entry.extra.get("__zip").map(PathBuf::from).or_else(|| zip_path.clone());
                items.push((zip, entry.clone(), true));
            }
        }
        // Hair slots (ChaFileHair.parts), as the hair-slot pass looks them up.
        for (i, &hid) in card.hair_ids.iter().enumerate() {
            if hid == 0 { continue; }
            let cat = 101 + i as i32;
            let resolve = card.resolves.iter().find(|r| (r.slot == hid || r.local_slot == hid) && (r.category_no == cat || (101..=104).contains(&r.category_no)));
            let (guid, zip_path) = resolve.map_or((String::new(), None), |r| (r.mod_id.clone(), guid_path(&guid_index, &r.mod_id).cloned()));
            let Some(entry) = lookup_entry(&vanilla, &mut lists, zip_path.as_deref(), &guid, cat, hid, hid) else { continue };
            if is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
            items.push((zip_path, entry, !card.hair_parts[i].no_shake));
        }
        // Accessory slots without a KKEx resolve (the ChaFileAccessory.parts pass): vanilla id, else the mod list.
        let resolved_slots: std::collections::HashSet<usize> = card.resolves.iter()
            .filter(|r| is_mesh_category(r.category_no) && role_for_category(r.category_no, &r.property).starts_with("acc"))
            .filter(|r| { let p = r.property.to_ascii_lowercase(); !p.contains("outfit") || p.contains(&want_outfit) })
            .filter_map(|r| accessory_slot_from_property(&r.property)).collect();
        for (slot_i, a) in card.accessory.iter().enumerate() {
            if a.type_id == 120 || !is_mesh_category(a.type_id) || resolved_slots.contains(&slot_i) { continue; }
            if !role_for_category(a.type_id, "").starts_with("acc") { continue; }
            let Some(entry) = vanilla.get(a.type_id, a.id).or_else(|| mod_catalog.get(a.type_id, a.id)).cloned() else { continue };
            if is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
            let zip = entry.extra.get("__zip").map(PathBuf::from);
            items.push((zip, entry, !a.no_shake));
        }
        // The top's sailor/jacket subparts (the subclothes pass).
        if manifest.clothes_states[0] != 3 {
            for (_, entry, _, zip_path) in selected_top_subparts.iter().flatten().flatten() {
                if is_dummy_part(&entry.main_data, Some(&entry.name)) { continue; }
                let zip = entry.extra.get("__zip").map(PathBuf::from).or_else(|| zip_path.clone());
                items.push((zip, entry.clone(), true));
            }
        }
        prefetch_entries(game_root, items);
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!("ok-extract: clothes requested={requested_states:?} resolved={resolved_states:?} types={state_types:?} refs={reference_flags:?} notBot={linked_not_bot} notShorts={linked_not_shorts}");
    }
    let selected_hair_gloss = parsed_card.as_ref()
        .and_then(|card| selected_hair_gloss_texture(game_root, card, &vanilla, &mod_catalog, &guid_index));
    profile_tick("lists+mods+guids", t);
    t = Instant::now();
    let body_bundle = game_root.join("abdata/chara/oo_base.unity3d");
    // ChaControl.InitBaseCustomTextureBody loads the draw material from
    // mm_base, independently of the body mesh bundle (ChaControl.cs:10709).
    let skin_material_bundle = crate::gamefs::read_source(game_root.join("abdata/chara/mm_base.unity3d"))
        .ok().map(crate::unity::parse_bundle_cached);
    let vanilla_head_bundle = game_root.join("abdata/chara/bo_head_00.unity3d");
    let mut head_bundle = vanilla_head_bundle.clone();
    // Sideloader lets mods replace vanilla assetbundles. `[DeathWeasel][KKS]
    // Face Fixes` ships `chara/bo_head_00.unity3d` with the fixed face/eye
    // meshes, skeleton and shape animation the game actually loads. Opt in with
    // OK_FACE_FIXES so the vanilla pipeline stays available for comparison.
    if std::env::var("OK_FACE_FIXES").is_ok() {
        if let Some(b) = crate::mods::override_bundle(&mods_dir, "chara/bo_head_00.unity3d") {
            let tmp = std::env::temp_dir().join("ok_bo_head_00.unity3d");
            if std::fs::write(&tmp, &b).is_ok() {
                eprintln!("ok-extract: FACE_FIXES override bytes={} -> {}", b.len(), tmp.display());
                head_bundle = tmp;
            }
        } else {
            eprintln!("ok-extract: FACE_FIXES requested but no override found");
        }
    }
    let t_body = Instant::now();
    let (mut body_meshes, mut body_skel, mut body_texs) = load_base_bundle(&body_bundle);
    profile_tick("load_base_bundle body", t_body);
    // StudioEditor builds the head from the armature prefab `p_cf_head_bone`
    // (in the base bundle) parented under `cf_s_head`, not from the head mesh's
    // own SMR skeleton (KoikatsuCharacterAssembler.cs:237-274). That armature
    // has the full head bone tree (97 bones, scale 1) so the head/hair scale is
    // inherited from `cf_s_head` alone.
    let body_bundle_bytes = crate::gamefs::read_source(&body_bundle).unwrap_or_default();
    let parsed_body = crate::unity::parse_bundle_cached(body_bundle_bytes.clone());
    parsed_body.configure_game_references(game_root);
    // ChaControl.Load: sexFemale selects p_cf_body_00 or p_cm_body_00.
    // Filtering the requested prefab prevents a largest-mesh female fallback.
    if parsed_card.as_ref().is_some_and(|card|card.sex==0) {
        let (male_meshes,_)=parsed_body.meshes_for_maindata("p_cm_body_00");
        if !male_meshes.iter().any(|m|m.name.eq_ignore_ascii_case("o_body_a")) {
            return Err("male body prefab p_cm_body_00 has no o_body_a".into());
        }
        body_skel=male_meshes.iter().find(|m|m.name.eq_ignore_ascii_case("o_body_a"))
            .and_then(|m|m.smr_skel.clone());
        if body_skel.is_none() {return Err("male body renderer has no source skeleton".into());}
        body_meshes=male_meshes;
        eprintln!("ok-extract: selected male body prefab p_cm_body_00");
    }
    if let (Some(body), Some(bone_prefab)) = (
        body_skel.as_mut(), parsed_body.skeleton_for_prefab_name("p_cf_body_bone"),
    ) {
        let added = body.merge_missing_tree_transforms(&bone_prefab);
        if added > 0 {
            eprintln!("ok-extract: body collider tree merged {added} transforms");
        }
    }
    // ChaControl.cs:8085-8104: objHeadBone = `p_cf_head_bone` loaded from
    // `studio/base/00.unity3d` (not oo_base), parented under cf_s_head.
    let head_bone_bundle = game_root.join("abdata/studio/base/00.unity3d");
    let parsed_head_rig=crate::mods::override_bundle(&game_root.join("mods"),"studio/base/00.unity3d").map(crate::unity::SourceBytes::from)
        .or_else(||crate::gamefs::read_source(&head_bone_bundle).ok()).map(crate::unity::parse_bundle_cached);
    let prefab_head_skel = parsed_head_rig.as_ref()
        .and_then(|rig|rig.skeleton_for_prefab_name("p_cf_head_bone"))
        .or_else(|| parsed_body.skeleton_for_prefab_name("p_cf_head_bone"));
    let pv_config=parsed_head_rig.as_ref().and_then(|rig|rig.studio_pv_config("p_cf_body_bone"));
    if let (Some(body),Some(rig))=(&mut body_skel,&parsed_head_rig) {
        if let Some(source)=rig.skeleton_for_prefab_name("p_cf_body_bone") {body.merge_missing_tree_transforms(&source);}
    }
    let head_has_eye_controller=parsed_head_rig.as_ref().map_or_else(
        ||parsed_body.prefab_has_component_field("p_cf_head_bone","eyeLookScript"),
        |rig|rig.prefab_has_component_field("p_cf_head_bone","eyeLookScript"));
    let mut eye_look_config=parsed_head_rig.as_ref().and_then(|rig|rig.eye_look_config("p_cf_head_bone"))
        .or_else(||parsed_body.eye_look_config("p_cf_head_bone"));
    // EyeLookCalc.Awake runs on the newly instantiated rig BEFORE copying the
    // selected head prefab or applying sliders/ABMX. Preserve that Init basis.
    if let (Some(c),Some(rig))=(&mut eye_look_config,&prefab_head_skel) {
        c.init=crate::eye_look::Runtime::init_from_skeleton(rig,c);
    }
    // `[DeathWeasel][KKS] Face Fixes` overrides chara/bo_head_00.unity3d and is
    // what the game actually loads: its face skeleton places the eyes/nose
    // correctly and its meshes are the fixed ones. Its bundle omits
    // cf_face_00_t/_mc, so the custom face texture keeps using the vanilla
    // bundle's textures (composite is built from head_texs).
    // NOTE: the game loads `[DeathWeasel][KKS] Face Fixes` over
    // abdata/chara/bo_head_00.unity3d. Its skeleton places the eyes/nose
    // correctly (verified against the mesh dump face bones), but its meshes
    // ship changed UVs and its detail maps are not provided, so rendering the
    // override against the vanilla textures (which this pipeline uses) is
    // worse than the vanilla meshes. Keep vanilla until the override's own
    // textures + cf_m_face shader can be reproduced.
    let t_head = Instant::now();
    let head_bundle_bytes = crate::gamefs::read_source(&head_bundle).unwrap_or_default();
    let mut parsed_head = crate::unity::parse_bundle_cached(head_bundle_bytes);
    profile_tick("load_base_bundle head", t_head);
    let mut head_texs = parsed_head.textures_keep_base();
    // The Face Fixes bundle omits `cf_face_00_t`/`_mc`, so borrow the vanilla
    // bundle's textures for anything the override does not ship.
    if head_bundle != vanilla_head_bundle {
        let vb = crate::gamefs::read_source(&vanilla_head_bundle).unwrap_or_default();
        let vanilla_head = crate::unity::parse_bundle_cached(vb);
        let have: std::collections::HashSet<String> =
            head_texs.iter().map(|(n, _)| n.to_ascii_lowercase()).collect();
        for (n, b) in vanilla_head.textures_keep_base() {
            if !have.contains(&n.to_ascii_lowercase()) {
                head_texs.push((n, b));
            }
        }
        eprintln!("ok-extract: FACE_FIXES merged textures={}", head_texs.len());
    }
    // The game instantiates the list entry's `MainData` prefab; the bundle holds
    // several prefabs that reuse mesh names with different SMR bone lists. Only
    // the active prefab's meshes have the SMR the live game uses (e.g. the iris
    // is 1 bone, the sclera 54), so filter to it instead of name-matching.
    // `mod_catalog.get(100, headId)` is ambiguous: every sideloader head reuses
    // a small local id (usually 1). Resolve the card's own KKEx row, same as
    // the face texture.
    let mut face_zip_lists = std::collections::HashMap::new();
    let face_entry = parsed_card.as_ref().and_then(|card| {
        resolve_face_entry(&vanilla, card, &guid_index, &mut face_zip_lists)
    });
    if let Some(card) = parsed_card.as_ref() {
        if face_entry.is_none() && card.resolves.iter().any(|r| {
            r.category_no == 100 && r.property.to_ascii_lowercase().contains("headid")
        }) {
            // The mod is not installed: the game keeps its default head.
            eprintln!("ok-extract: head KKEx resolve has no list entry for headId={}; default head", card.head_id);
        }
    }
    let head_main = face_entry
        .as_ref()
        .map(|e| e.info("MainData"))
        .filter(|s| !s.is_empty() && s != "0")
        .unwrap_or_else(|| "p_cf_head_00".to_string());
    let head_shape_asset = face_entry
        .as_ref()
        .map(|e| e.info("ShapeAnime"))
        .filter(|s| !s.is_empty() && s != "0")
        .unwrap_or_else(|| "cf_anmShapeHead_00".to_string());
    eprintln!(
        "ok-extract: head entry={} main={} ab={}",
        face_entry.as_ref().map(|e| e.name.as_str()).unwrap_or("none"),
        head_main,
        face_entry.as_ref().map(|e| e.main_ab.as_str()).unwrap_or("")
    );
    // ChaControl.LoadCharaFbxData (ChaControl.cs:10596-10607) instantiates
    // `GetInfo(MainAB)` / `GetInfo(MainData)`, not always vanilla bo_head_00.
    // A sideloader head (KK_384283 Head Mod) ships its own mesh and eye textures.
    // Shape curves live in the same bundle as the head prefab (`ShapeAnime` =
    // `cf_anmShapeHead_00`). A sideloader head's curves are the mod's face
    // algorithm; vanilla keys are only the fallback when that asset is absent.
    let shape_bundle = head_bundle.clone();
    let mut mod_shape_keys = None;
    let mut head_key = crate::sqlcache::file_key(&head_bundle);
    if let Some(entry) = face_entry.as_ref() {
        let ab = entry.main_ab.as_str();
        if !ab.is_empty()
            && ab != "0"
            && !ab.eq_ignore_ascii_case("chara/bo_head_00.unity3d")
        {
            let zip = entry.extra.get("__zip").map(std::path::Path::new);
            if let Some(bytes) = accessory_bundle_bytes(game_root, zip, entry) {
                // Read from the bundle in memory (it was written to one temporary file name that cards opening at once
                // overwrote for each other).
                let trial = crate::shape::load_head_keys_named_bytes(&bytes, &head_shape_asset);
                if !trial.is_empty() {
                    eprintln!(
                        "ok-extract: head shape keys from mod bundle n={}",
                        trial.len()
                    );
                    mod_shape_keys = Some(trial);
                }
                let parsed = crate::unity::parse_bundle_cached(bytes);
                let (ms, _) = parsed.meshes_for_maindata(&head_main);
                if pick_named(&ms, "cf_O_face").is_some() {
                    eprintln!(
                        "ok-extract: head from list ab={ab} main={head_main} meshes={}",
                        ms.len()
                    );
                    let mut texs = parsed.textures_keep_base();
                    let have: std::collections::HashSet<String> =
                        texs.iter().map(|(n, _)| n.to_ascii_lowercase()).collect();
                    for (n, b) in std::mem::take(&mut head_texs) {
                        if !have.contains(&n.to_ascii_lowercase()) {
                            texs.push((n, b));
                        }
                    }
                    head_texs = texs;
                    parsed_head = parsed;
                    head_key = match zip {
                        Some(z) => crate::sqlcache::zip_key(z, ab),
                        None => format!("ab:{ab}|v{}", crate::sqlcache::CACHE_VER),
                    };
                } else {
                    eprintln!("ok-extract: list head ab={ab} has no cf_O_face, keeping vanilla");
                }
            }
        }
    }
    let mut head_shape_keys = mod_shape_keys.unwrap_or_else(|| crate::shape::load_head_keys_named(&shape_bundle, &head_shape_asset));
    if head_shape_keys.is_empty() && shape_bundle != vanilla_head_bundle {
        head_shape_keys = crate::shape::load_head_keys_named(&vanilla_head_bundle, &head_shape_asset);
    }
    if head_shape_keys.is_empty() {
        eprintln!("ok-extract: head ShapeAnime asset missing: {head_shape_asset}");
    }
    let t_head_md = Instant::now();
    let (head_filtered, mut head_skel) =
        if let Some(hit) = crate::sqlcache::load_prefab(game_root, &head_key, &head_main) {
            eprintln!("ok-extract: sql head prefab hit {head_main}");
            hit
        } else {
            let got = parsed_head.meshes_for_maindata(&head_main);
            crate::sqlcache::store_prefab(
                game_root,
                &head_key,
                &head_main,
                &got.0,
                got.1.as_ref(),
            );
            got
        };
    profile_tick("meshes_skel_for_maindata head", t_head_md);
    let head_meshes = if head_filtered.is_empty() {
        parsed_head.decode_all().0
    } else {
        head_filtered
    };
    let head_mouth_patterns = parsed_head.face_mouth_patterns(&head_main);
    let head_eye_patterns = parsed_head.face_control_patterns(&head_main,"EyesCtrl");
    let head_eyebrow_patterns = parsed_head.face_control_patterns(&head_main,"EyebrowCtrl");
    let mut head_face_settings=parsed_head.face_runtime_settings(&head_main);
    head_face_settings[3]=if parsed_card.as_ref().map_or(true,|card|card.eyes_blink) {1.}else{0.};
    head_face_settings[11]=if head_has_eye_controller {1.}else{0.};
    let head_eye_materials=parsed_head.eye_material_settings(&head_main);
    // Emitted material passes may have a different display name but retain
    // the source renderer's entire FBS controller identity.
    let mut head_fbs_sources = std::collections::HashMap::<String, String>::new();
    eprintln!("ok-extract: head FaceBlendShape mouth targets={} eyes={} eyebrow={}",
        head_mouth_patterns.len(),head_eye_patterns.len(),head_eyebrow_patterns.len());
    // ChaControl.cs:8115 + 8497: aaWeightsHead.CreateBoneList(objHeadBone =
    // "p_cf_head_bone") then AssignedWeightsAndSetBounds remaps every head SMR's
    // own bone list (component.bones[i].name) onto that armature. Set
    // `OK_OLDPHEAD` to keep the head mesh prefab's own SMR skeleton instead.
    if std::env::var("OK_OLDPHEAD").is_ok() {
        if let Some(face) = head_meshes
            .iter()
            .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
        {
            if let Some(s) = parsed_head.skeleton(face.path_id) {
                head_skel = Some(s);
            }
        }
    } else if let Some(s) = prefab_head_skel.clone() {
        eprintln!(
            "ok-extract: head_skel = p_cf_head_bone ({} bones)",
            s.names.len()
        );
        head_skel = Some(s);
    }
    // StudioEditor instantiates p_cf_head_bone then RebindSkinning every head
    // SMR onto that full name map. The face SMR is only 45 bones; iris/sclera
    // extras (cf_J_hitomi_tx_*) live in tfs but not in names until we append.
    if let Some(sk) = head_skel.as_mut() {
        let before = sk.names.len();
        sk.include_tree_bones();
        if sk.names.len() != before {
            eprintln!(
                "ok-extract: head_skel include_tree_bones smr={} names={} tfs={} hitomi={}",
                sk.smr_count,
                sk.names.len(),
                sk.tfs.len(),
                sk.index_of("cf_J_hitomi_tx_L").is_some()
            );
        }
    }
    // ChaControl.cs:8630 `CommonLib.CopySameNameTransform(objHeadBone, objHead)`:
    // the head mesh prefab's LOCAL transforms overwrite every same-named node of
    // `p_cf_head_bone` before the face shape is applied (ChaControl.cs:8726
    // InitShapeFace targets objHeadBone). The armature authors the head attach
    // nodes at different positions than the mesh, so without this the hair
    // accessories hang ~0.03 high / ~0.016 forward (see pose.rs).
    if std::env::var("OK_NOCOPYTRF").is_err() {
        if let Some(mesh_skel) = parsed_head.skeleton_for_prefab_name(&head_main) {
            if let Some(sk) = head_skel.as_mut() {
                let changed = crate::pose::copy_same_name_transform(sk, &mesh_skel);
                if changed > 0 {
                    eprintln!(
                        "ok-extract: CopySameNameTransform head mesh '{}' onto p_cf_head_bone changed={changed}",
                        head_main
                    );
                }
            }
        }
    }
    // The supplied Studio T-pose captures use ChaControl's !hiPoly body-bone
    // reference layout even though their generated texture RTs use the 1024px
    // path. Keep texture resolution selection separate from attach nodes.
    // OK_BONE_HIPOLY=1 opts into the unmodified prefab references.
    if std::env::var("OK_BONE_HIPOLY").ok().as_deref() != Some("1") {
        if let Some(sk) = body_skel.as_mut() {
            crate::pose::apply_low_poly_body_references(sk);
        }
    }
    let mut body_rest = body_skel.clone();
    // Parse local PPtrs against the original prefab IDs. Merging the tree
    // into the SMR rig assigns synthetic IDs; runtime rebinds by these names.
    let body_dynamics = parsed_body.skeleton_for_prefab_name("p_cf_body_bone")
        .map(|s| parsed_body.body_dynamics_for_skeleton(&s)).unwrap_or_default();
    let body_colliders = parsed_body.skeleton_for_prefab_name("p_cf_body_bone")
        .map(|s| parsed_body.body_colliders_for_skeleton(&s)).unwrap_or_default();
    let mut clothes_dynamics = crate::dbone::DBoneSet::default();
    eprintln!("ok-extract: body Ver02 components={}", body_dynamics.len());
    let head_rest = head_skel.clone();
    profile_tick("load_base_bundles", t);
    check_preempted()?;
    t = Instant::now();
    eprintln!(
        "ok-extract: body texs={} names={:?} head meshes={}",
        body_texs.len(),
        body_texs
            .iter()
            .map(|(n, b)| format!("{n}:{}", b.len()))
            .collect::<Vec<_>>(),
        head_meshes.len()
    );
    let correct = crate::shape::load_correct_info(game_root);
    let (sex, type_bone, nip_rate) = parsed_card
        .as_ref()
        .map(|c| (c.sex, c.type_bone, c.nip_stand_rate))
        .unwrap_or((0, 0, 0.0));
    let mut body_head_scale = [1.0f32; 3];
    if let (Some(sk), Some(card)) = (body_skel.as_mut(), parsed_card.as_ref()) {
        if !card.shape_body.is_empty() && std::env::var("OK_NOSHAPE").is_err() {
            body_head_scale = crate::shape::apply_body_shape(
                game_root,
                sk,
                &card.shape_body,
                &correct,
                sex,
                type_bone,
                nip_rate,
            );
        }
    }
    let head_correct = 1.0 / (1.0 + correct.get(2).map(|c| c.scl[1]).unwrap_or(0.0));
    match head_skel.as_mut() {
        Some(sk) => {
            if let Some(card) = parsed_card.as_ref() {
                if !card.shape_face.is_empty() {
                    // The Face Fixes override ships the fixed skeleton/meshes but
                    // no shape animation, so fall back to the vanilla bundle's
                    // `cf_anmShapeHead_00` (the game keeps using it).
                    crate::shape::apply_head_shape_keys(
                        sk,
                        &head_shape_keys,
                        &card.shape_face,
                        body_head_scale,
                        head_correct,
                        type_bone,
                    );
                }
            }
            crate::pose::assigned_weights_remap(sk);
            // ABM/ABMX head modifiers are part of the card and applied by the
            // game to the head skeleton (`OK_NO_ABM_HEAD` disables for A/B).
            if std::env::var("OK_NO_ABM_HEAD").is_err() {
                if let Some(card) = parsed_card.as_ref() {
                    let names: Vec<String> =
                        card.abm.iter().map(|b| b.name.clone()).collect();
                    let base = crate::pose::capture_baseline(sk, &names);
                    crate::pose::apply_abm(sk, &card.abm, 0, &base);
                }
            }
            if std::env::var("OK_DIAG").is_ok() {
                for n in [
                    "cf_J_FaceRoot",
                    "cf_J_Nose_tip",
                    "cf_J_Eye_rz_L",
                    "cf_J_MouthBase_ty",
                    "a_n_head",
                    "a_n_headflont",
                    "a_n_headtop",
                    "a_n_headside",
                    "cf_J_Mayu_ty",
                    "cf_J_ChinLow",
                    "cf_J_Chin_Base",
                    "cf_J_Chin_s",
                    "cf_J_ChinTip_Base",
                    "cf_J_FaceLow_sx",
                ] {
                    if let Some(m) = sk.world_tf(n) {
                        let local = sk.tfs.values().find(|t| t.name == n);
                        eprintln!(
                            "ok-extract: HEADBONE {n} world={:?} wscl={:?} local_pos={:?} local_scl={:?} parent={}",
                            crate::pose::mat_pos(&m),
                            crate::pose::world_scale_pub(&m),
                            local.map(|t| t.pos),
                            local.map(|t| t.scale),
                            local.and_then(|t| t.parent).and_then(|p| sk.tfs.get(&p)).map(|t| t.name.as_str()).unwrap_or("")
                        );
                    }
                }
                let rw = sk.root_world();
                eprintln!(
                    "ok-extract: HEADBONE root pos={:?} scl={:?}",
                    crate::pose::mat_pos(&rw),
                    crate::pose::world_scale_pub(&rw)
                );
            }
        }
        None => eprintln!("ok-extract: no head skeleton"),
    }
    // The face SMR skeleton is pruned to its 45 bones; the game remaps hair
    // prefabs onto the head's full bone tree (cf_J_hairF_top / cf_J_hairB_top),
    // which carries the head-shape scale. Build that full skeleton once and
    // skin the hair against it (see the hair branch in `rig_mesh`).
    let head_full_rest = parsed_head.skeleton_full_named("cf_J_hairF_top");
    let mut head_full_skel = head_full_rest.clone();
    if let Some(sk) = head_full_skel.as_mut() {
        if let Some(card) = parsed_card.as_ref() {
            if !card.shape_face.is_empty() {
                crate::shape::apply_head_shape_keys(
                    sk,
                    &head_shape_keys,
                    &card.shape_face,
                    body_head_scale,
                    head_correct,
                    type_bone,
                );
            }
            // Hair is skinned against this full head tree, while face parts
            // use p_cf_head_bone above. ABMX runs on the shared objHeadBone in
            // Unity, so both reconstructions must receive the same modifiers.
            if std::env::var("OK_NO_ABM_HEAD").is_err() {
                let names: Vec<String> = card.abm.iter().map(|b| b.name.clone()).collect();
                let base = crate::pose::capture_baseline(sk, &names);
                crate::pose::apply_abm(sk, &card.abm, 0, &base);
            }
        }
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: head_full_skel names={} hairF={} hairF_scl={:?}",
                sk.names.len(),
                sk.index_of("cf_J_hairF_top").is_some(),
                sk.world_tf("cf_J_hairF_top")
                    .map(|m| crate::pose::world_scale_pub(&m))
            );
        }
    }
    profile_tick("shape_body+head", t);
    t = Instant::now();
    // ChaControl: InitShapeBody(objBodyBone) then AssignedWeights then AnimPlay(Fmannequin_00).
    if let Some(sk) = body_skel.as_mut() {
        crate::pose::assigned_weights_remap(sk);
        let abm_names: Vec<String> = parsed_card
            .as_ref()
            .map(|c| c.abm.iter().map(|b| b.name.clone()).collect())
            .unwrap_or_default();
        // The dump reference is the neutral T-pose.  Stand_D_00 is a Maker
        // animation, and applying it here moves March's body vertices by up to
        // 54 cm from the captured mesh.  A named pose remains opt-in for pose
        // previews; the card alone does not request one.
        let pose = std::env::var("OK_POSE").ok().filter(|_| crate::profile::var("OK_NOPOSE").is_err());
        let scene = std::env::var("OK_POSE_SCENE").ok().and_then(|v| v.parse().ok()).unwrap_or(0);
        let (arm, hand) = if let Some(state) = pose.as_ref() {
            let shaped = sk.clone();
            crate::anim::apply_clip_at(game_root, sk, state, f32::MAX);
            crate::shape::apply_body_shape_always(sk, &shaped);
            crate::anim::clip_limb_corrections_mode(game_root, state, scene)
        } else {
            (true, true)
        };
        crate::pose::apply_scene_limb_size_corrections(sk, arm, hand);
        if pose.as_ref().is_some_and(|state|
            crate::anim::clip_expression_bone_mode(game_root, state, scene))
            && std::env::var("OK_NOEXPR").is_err() {
            crate::pose::apply_expression_bone(sk);
        }
        // ABMX modifies each bone relative to its current (posed) local TRS
        // (`BoneModifier.Apply` runs after the Animator), so the baseline must be
        // captured AFTER the pose/expression pass, not before it.
        let abm_base = crate::pose::capture_baseline(sk, &abm_names);
        if let Some(card) = parsed_card.as_ref() {
            crate::pose::apply_abm(sk, &card.abm, 0, &abm_base);
            if std::env::var("OK_FINALIK").is_ok() {
                crate::finalik::apply_final_ik(
                    game_root,
                    &card.shape_body,
                    sk,
                    "Stand_D_00",
                    "p_cf_body_bone",
                );
            }
        }
    }
    profile_tick("pose+abm", t);
    t = Instant::now();
    let mut t_mark = Instant::now();
    if let Some(b) = body_skel.as_ref() {
        eprintln!(
            "ok-extract: body tf cf_s_head={} HairParent={} a_n_hair_pony={}",
            b.world_tf("cf_s_head").is_some(),
            b.world_tf("cf_J_FaceUp_ty").is_some(),
            b.world_tf("a_n_hair_pony").is_some()
        );
    }
    if let Some(h) = head_skel.as_ref() {
        eprintln!(
            "ok-extract: head tf cf_J_FaceUp_ty={} a_n_hair_pony={} root_ok",
            h.world_tf("cf_J_FaceUp_ty").is_some(),
            h.world_tf("a_n_hair_pony").is_some()
        );
        if let Some(m) = bone_world(body_skel.as_ref(), Some(h), "cf_J_FaceUp_ty") {
            eprintln!(
                "ok-extract: HairParent world pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
        if let Some(m) = h.world_tf("cf_J_FaceBase") {
            eprintln!(
                "ok-extract: FaceBase local-space world pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
        if let Some(m) = h.world_tf("a_n_headtop") {
            eprintln!(
                "ok-extract: a_n_headtop pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
        if let Some(m) = h.world_tf("a_n_headside") {
            eprintln!(
                "ok-extract: a_n_headside pos={:?} scl={:?}",
                crate::pose::mat_pos(&m),
                crate::pose::world_scale_pub(&m)
            );
        }
    }
    if std::env::var("OK_ATTACH").is_ok() {
        // Neck/head chain, to compare against the dump skeleton.json locals.
        if let Some(b) = body_skel.as_ref() {
            for n in [
                "cf_j_hips",
                "cf_j_spine01",
                "cf_j_spine02",
                "cf_j_spine03",
                "cf_j_neck",
                "cf_j_head",
                "cf_s_spine03",
                "cf_s_neck",
                "cf_s_head",
                "cf_j_thigh00_R",
                "cf_j_leg01_R",
                "cf_j_leg03_R",
                "cf_j_foot_R",
            ] {
                if let Some(tf) = b.tfs.values().find(|t| t.name == n) {
                    eprintln!(
                        "ok-extract: BODYLOCAL {n} pos={:?} rot={:?} scl={:?}",
                        tf.pos, tf.rot, tf.scale
                    );
                }
                if let Some(m) = b.world_tf(n) {
                    eprintln!(
                        "ok-extract: BODYWORLD {n} r0={:?} r1={:?} r2={:?}",
                        m[0], m[1], m[2]
                    );
                }
            }
        }
        // ChaReference.RefObjKey -> actual transform (matches the game
        // skeleton.json `referenceObjects` ground truth world positions).
        for rk in [
            "HeadParent",
            "HairParent",
            "a_n_head",
            "a_n_headflont",
            "a_n_headtop",
            "a_n_headside",
            "a_n_hair_pony",
            "a_n_hair_twin_L",
            "a_n_hair_twin_R",
            "a_n_neck",
            "a_n_megane",
            "a_n_nose",
            "a_n_mouth",
            "a_n_nip_R",
            "a_n_nip_L",
            "a_n_bust_f",
            "a_n_bust",
            "a_n_waist_f",
            "a_n_waist_b",
            "a_n_waist_R",
            "a_n_heel_R",
            "a_n_heel_L",
            "a_n_elbo_R",
            "a_n_leg_R",
            "a_n_shoulder_R",
            "a_n_shoulder_L",
            "a_n_arm_R",
            "cf_j_foot_R",
            "cf_j_foot_L",
            "cf_j_leg03_R",
            "cf_j_leg01_R",
            "cf_j_thigh00_R",
            "cf_s_thigh02_R",
        ] {
            let target = match rk {
                "HeadParent" => "cf_s_head",
                "HairParent" => "cf_J_FaceUp_ty",
                other => other,
            };
            if let Some(m) = bone_world(body_skel.as_ref(), head_skel.as_ref(), target) {
                eprintln!(
                    "ok-extract: ATTACH {rk} obj={target} pos={:?} scl={:?}",
                    crate::pose::mat_pos(&m),
                    crate::pose::world_scale_pub(&m)
                );
            }
        }
    }
    if std::env::var("OK_DIAG").is_ok() {
        if let Some(h) = head_skel.as_ref() {
            for tf in h.tfs.values() {
                if !tf.name.starts_with("cf_J_") {
                    continue;
                }
                eprintln!(
                    "ok-extract: HEADLOCAL {} {:.6} {:.6} {:.6} | {:.6} {:.6} {:.6}",
                    tf.name, tf.pos[0], tf.pos[1], tf.pos[2], tf.scale[0], tf.scale[1], tf.scale[2]
                );
            }
        }
    }
    let mut seen_meshes: std::collections::HashSet<String> = std::collections::HashSet::new();
    let mut body_mesh = body_skel
        .as_ref()
        .and_then(|s| pick_path(&body_meshes, "o_body_a", s.source_mesh))
        .cloned()
        .or_else(|| pick_named(&body_meshes, "o_body_a").cloned())
        .or_else(|| pick_largest(&body_meshes).cloned());
    profile_tick("body tf/pose prelude", t_mark); t_mark = Instant::now();
    let mut replaced_body = false;
    let mut replacement_genitals: Vec<ExtractedMesh> = Vec::new();
    if let Some(card) = parsed_card.as_ref() {
        let nv = body_mesh.as_ref().map(|m| m.positions.len()).unwrap_or(0);
        if let Some((source, bytes)) =
            crate::mods::uncensor_body_for_card(&game_root,card.body_guid.as_deref(),card.sex)
        {
            let parsed=crate::unity::parse_bundle_ref(&bytes);
            parsed.configure_game_references(game_root);
            // The installed uncensor's skin textures and body meshes are the same for every card that uses it
            // (~0.25 s to decode each time); the card-shaped work below (skeleton merge, dynamics) still runs.
            type UncensorDecoded=(Vec<Option<Vec<u8>>>,Vec<ExtractedMesh>);
            static UNCENSOR_BODY:std::sync::Mutex<Option<(String,std::sync::Arc<UncensorDecoded>)>>=std::sync::Mutex::new(None);
            let stamp=crate::gamefs::metadata(&source.zipmod).ok();
            let memo_key=format!("{}|{}|{:?}|{}|{}|{}|{}",source.zipmod.display(),stamp.as_ref().map_or(0,|m|m.len()),stamp.as_ref().and_then(|m|m.modified().ok()),
                source.file,source.asset,source.main_texture,source.color_mask);
            let cached=UNCENSOR_BODY.lock().unwrap_or_else(|e|e.into_inner()).as_ref().filter(|(k,_)|stamp.is_some()&&*k==memo_key).map(|(_,d)|d.clone());
            let decoded=cached.unwrap_or_else(||{
                let d=std::sync::Arc::new((vec![parsed.texture_png_named(&source.main_texture),parsed.texture_png_named(&source.color_mask)],
                    parsed.meshes_for_maindata(&source.asset).0));
                *UNCENSOR_BODY.lock().unwrap_or_else(|e|e.into_inner())=Some((memo_key,d.clone()));
                d
            });
            // UncensorSelector's CreateBodyTexture transpiler redirects OOBase,
            // BodyMainTex and the sex-specific BodyColorMask, not just geometry.
            for (index,(name,alias)) in [(&source.main_texture,"cf_body_00_t"),
                (&source.color_mask,if card.sex==0{"cm_body_00_mc"}else{"cf_body_00_mc"})].into_iter().enumerate() {
                if let Some(png)=decoded.0[index].clone() {
                    let bytes=png.len();
                    body_texs.retain(|(n,_)|!n.eq_ignore_ascii_case(alias));
                    body_texs.push((alias.into(),png));
                    eprintln!("ok-extract: uncensor body skin {alias} <- {name} bytes={bytes} (vagina uses this uncensor texture, not the mosaic base)");
                } else {
                    eprintln!("ok-extract: uncensor body skin {name} missing from {}; vagina stays on the loaded body texture", source.file);
                }
            }
            let meshes=&decoded.1;
            if let Some(mut m) = meshes
                .iter()
                .filter(|m| m.name.eq_ignore_ascii_case("o_body_a") && !m.weights.is_empty())
                .max_by_key(|m| m.positions.len())
                .cloned()
            {
                // UncensorSelector replaces the body's mesh/rig, not the live
                // cf_m_body material. Keep the game's shader properties and
                // texture slots instead of inheriting the uncensor asset's
                // placeholder material (which loses _ShadowExtend, etc.).
                if let Some(base) = body_mesh.as_ref() {
                    if base.mat.is_some() {
                        m.mat = base.mat.clone();
                    }
                    if base.detail_mask_png.is_some() {
                        m.detail_mask_png = base.detail_mask_png.clone();
                        if let Some(settings) = base.runtime_textures.get("_DetailMask") {
                            m.runtime_textures.insert("_DetailMask".into(), *settings);
                        } else {
                            m.runtime_textures.remove("_DetailMask");
                        }
                    }
                    if base.line_mask_png.is_some() {
                        m.line_mask_png = base.line_mask_png.clone();
                    }
                    if base.normal_map_png.is_some() {
                        m.normal_map_png = base.normal_map_png.clone();
                    }
                }
                let nv2 = m.positions.len();
                if let Some(sk) = body_skel.as_mut() {
                    // RebindSkinning uses this replacement renderer's bones.
                    // A fixed SAC order cannot describe other installed bodies.
                    if let Some(source_skeleton)=m.smr_skel.as_ref() {
                        sk.merge_missing_tree_transforms(source_skeleton);
                        if let Some(rest)=body_rest.as_mut() {rest.merge_missing_tree_transforms(source_skeleton);}
                        let dynamics=parsed.dynamic_bones_for_skeleton(source_skeleton)
                            .with_game_fixes(game_root).retarget_body(source_skeleton,sk);
                        eprintln!("ok-extract: replacement body DynamicBone components={}",dynamics.len());
                        clothes_dynamics.extend(dynamics);
                    }
                    let names=&m.bone_names;
                    sk.set_bone_order(&names);
                    sk.include_tree_bones();
                    if let Some(rest) = body_rest.as_mut() {
                        rest.set_bone_order(&names);
                        rest.include_tree_bones();
                    }
                    eprintln!(
                        "ok-extract: body bones reordered to source renderer order={} first={:?}",
                        names.len(),
                        sk.names.iter().take(4).collect::<Vec<_>>()
                    );
                }
                eprintln!(
                    "ok-extract: uncensor body guid={} <- {} asset={} verts={nv2} (was {nv}) weights={}",
                    source.guid,source.file,source.asset,
                    m.weights.len()
                );
                body_mesh = Some(m);
                replaced_body = true;
                replacement_genitals = meshes.iter()
                    .filter(|item| is_genital_display_mesh(&item.name))
                    .cloned()
                    .collect();
            }
        }
    }
    // o_body_a is not weighted to the penis chain. Append those bones before
    // the body is skinned so its existing indices stay put and the separate
    // penis mesh can bind by name. A replacement body that bakes the penis
    // into o_body_a contributes no extra meshes here.
    let mut genital_sources: Vec<ExtractedMesh> = if replaced_body {
        replacement_genitals
    } else if parsed_card.as_ref().is_some_and(|card| card.sex == 0) {
        body_meshes.iter().filter(|item| is_genital_display_mesh(&item.name)).cloned().collect()
    } else {
        Vec::new()
    };
    profile_tick("uncensor body", t_mark); t_mark = Instant::now();
    if let Some(card) = parsed_card.as_ref() {
        let seed = card.birth_day.wrapping_add(card.personality)
            .wrapping_add((card.voice_pitch * 100.0) as i32);
        let nail = card.nail_color;
        adopt_uncensor_genitals(
            &mut genital_sources, &game_root, card, crate::mods::UncensorGenitalKind::Penis,
            is_penis_mesh, seed, skin_tint, skin_sub, nail, replaced_body, outfit,
        );
        adopt_uncensor_genitals(
            &mut genital_sources, &game_root, card, crate::mods::UncensorGenitalKind::Balls,
            is_balls_mesh, seed, skin_tint, skin_sub, nail, replaced_body, outfit,
        );
    }
    profile_tick("uncensor genitals", t_mark); t_mark = Instant::now();
    genital_sources.sort_by(|a, b| a.name.to_ascii_lowercase().cmp(&b.name.to_ascii_lowercase()));
    if !genital_sources.is_empty() {
        let added = body_skel.as_mut().map(|sk| {
            genital_sources.iter().filter_map(|mesh| mesh.smr_skel.as_ref())
                .map(|part| sk.append_missing_tree_bones(part)).sum::<usize>()
        }).unwrap_or(0);
        let added_rest = body_rest.as_mut().map(|sk| {
            genital_sources.iter().filter_map(|mesh| mesh.smr_skel.as_ref())
                .map(|part| sk.append_missing_tree_bones(part)).sum::<usize>()
        }).unwrap_or(0);
        eprintln!("ok-extract: genital bones appended skeleton={added} rest={added_rest}");
    }
    if let Some(mesh) = body_mesh.as_mut() {
        if let Some(bundle) = skin_material_bundle.as_ref() {
            if let Some(mat) = bundle.material_named(if parsed_card.as_ref().is_some_and(|c|c.sex==0) {"cm_m_body"}else{"cf_m_body"}) {
                if let Some(name) = mat.slot("_DetailMask") {
                    if let Some(png) = bundle.texture_png_named(name) {
                        mesh.detail_mask_png = Some(orient_raw_mask(png));
                        if let Some(sampling) = bundle.texture_sampling_named(name) {
                            mesh.runtime_textures.insert("_DetailMask".into(), [
                                sampling.filter, sampling.wrap_u, sampling.wrap_v,
                                i32::from(sampling.srgb), sampling.mip_count,
                            ]);
                        } else {
                            mesh.runtime_textures.remove("_DetailMask");
                        }
                    }
                }
                mesh.line_mask_png = mat.slot("_LineMask")
                    .and_then(|name| bundle.texture_png_named(name).map(orient_raw_mask))
                    .or_else(|| mesh.line_mask_png.take());
                mesh.normal_map_png = mat.slot("_NormalMap")
                    .and_then(|name| bundle.texture_png_named(name))
                    .or_else(|| mesh.normal_map_png.take());
                eprintln!("ok-extract: body draw mat={} shader={} detail={} line={} normal={} shadow_extend={:?}",
                    mat.name, mat.shader, mesh.detail_mask_png.as_ref().map_or(0, Vec::len),
                    mesh.line_mask_png.as_ref().map_or(0, Vec::len),
                    mesh.normal_map_png.as_ref().map_or(0, Vec::len), mat.float("_ShadowExtend"));
                mesh.mat = Some(mat);
            }
        }
        profile_tick("pre-skin body setup", t_mark); t_mark = Instant::now();
        let t_skin = Instant::now();
        rig_mesh(mesh, "body", body_skel.as_ref(), head_skel.as_ref(), None, None, None, None, None);
        profile_tick("skin+rig body", t_skin);
        let t_body_tex = Instant::now();
        compose_body_texture(
            mesh,
            parsed_card.as_ref(),
            skin_tint,
            skin_sub,
            &body_texs,
            outfit,
        );
        // ChaControl.ChangeSettingBodyDetail assigns the list's normal and
        // line detail to the draw material, then sets DetailNormalMapScale.
        // main_skin combines this detail normal with cf_m_body's base normal;
        // the Koikano and Shader Forge DXBC use different blend operators.
        let mut body_normal_detail_png = None;
        if let Some(card) = parsed_card.as_ref() {
            if let Some(mat) = mesh.mat.as_mut() {
                mat.floats.insert("_DetailNormalMapScale".into(), card.body_detail_power);
                // VisibleAddBodyLine overwrites the draw material after it is
                // loaded; mm_base's serialized default is always on.
                mat.floats.insert("_linetexon".into(), if card.body_draw_add_line { 1.0 } else { 0.0 });
            }
            if let Some(line) = load_catalog_png(
                game_root, &vanilla, &mod_catalog, 420, card.body_detail_id,
                "LineMask", &head_texs, &body_texs,
            ) {
                mesh.line_mask_png = Some(orient_raw_mask(line));
            }
            if let Some(detail) = load_catalog_png(
                game_root, &vanilla, &mod_catalog, 420, card.body_detail_id,
                "NormallMapDetail", &head_texs, &body_texs,
            ) {
                eprintln!("ok-extract: body normal detail id={} scale={:.3} bytes={}",
                    card.body_detail_id, card.body_detail_power, detail.len());
                // Koikano/main_skin samples _NormalMap and _NormalMapDetail
                // independently (0072.asm.txt t7/t8). Keep the detail map
                // separate; pre-blending it changes the sampled normal.
                body_normal_detail_png = Some(convert_runtime_normal_map(detail));
            } else {
                eprintln!("ok-extract: body normal detail unavailable id={}", card.body_detail_id);
            }
        }
        profile_tick("compose_body_texture", t_body_tex);
        eprintln!(
            "ok-extract: body tex {} bytes name={}",
            mesh.texture_png.as_ref().map(|p| p.len()).unwrap_or(0),
            mesh.texture_name
        );
        // ChaControl assigns the equipped top/inner's _AlphaMask to the draw
        // material. Keep it separate from MainTex: main_skin samples t0 at
        // the body's UV and clips min(max(r,1-alpha_a),max(g,1-alpha_b)).
        t_mark = Instant::now();
        // The body's normal map converts on another thread while the masks and MaterialEditor run below: the card's
        // MaterialEditor normal map when it has one (it replaces the body's below), else the body's own. Whatever the
        // body ends up with and was not converted here converts as before.
        let normal_source = parsed_card.as_ref()
            .and_then(|card| me_texture(Some(&card.me_tex), outfit, "body", None, mesh, "normalmap").map(|png| png.to_vec()))
            .or_else(|| mesh.normal_map_png.clone());
        let normal_job = normal_source.clone().map(|png| {
            let pack = crate::oklib::current_pack_scope();
            std::thread::spawn(move || { let _pack = crate::oklib::enter_pack_scope(pack); convert_runtime_normal_map(png) })
        });
        let mut body_blend = "opaque";
        if let Some(card) = parsed_card.as_ref() {
            mesh.alpha_mask_png = over_body_mask_png(game_root, card, &vanilla, &mod_catalog, &guid_index, outfit)
                .map(orient_raw_mask);
            if mesh.alpha_mask_png.is_some() {
                eprintln!("ok-extract: body over-mask loaded");
            }
        }
        // KSOX `BodyOver` overlay (type 1), applied after the alpha mask.
        if let Some(card) = parsed_card.as_ref() {
            if let Some(ov) = ksox_overlay_png(card, outfit, 1) {
                if let Some(png) = mesh.texture_png.as_ref() {
                    if let Some(c) = composite_overlay(png, &ov) {
                        mesh.texture_png = Some(c);
                        eprintln!("ok-extract: body over-overlay applied");
                    }
                }
            }
        }
        // Material Editor applies material texture overrides after ChaControl
        // creates the draw material. Body bypasses the generic clothing/hair
        // path, so apply the same renderer/material lookup explicitly here.
        if let Some(card) = parsed_card.as_ref() {
            // Body bypasses push_meshes. Replay its texture-coordinate edits
            // before emit_part captures the material ST fields. Keep this
            // addition confined to texture transforms, not unrelated numeric
            // shader properties that this legacy body path does not yet port.
            let transforms:crate::card::MeFloatMap=card.me_float.iter()
                .filter(|((_,_,_,_,property),_)|["_st_x","_st_y","_st_z","_st_w"].iter()
                    .any(|axis|property.to_ascii_lowercase().ends_with(axis)))
                .map(|(key,value)|(key.clone(),*value)).collect();
            apply_me_floats(&transforms,outfit,"body",None,mesh);
            if let Some(png) = me_maintex(Some(&card.me_tex), outfit, "body", None, mesh) {
                eprintln!("ok-extract: ME body MainTex bytes={}", png.len());
                mesh.texture_png = Some(png.to_vec());
                mark_material_editor_main_texture(mesh);
            }
            for (slot, setter) in [
                ("detailmask", 0u8), ("linemask", 1), ("normalmap", 2), ("alphamask", 3),
            ] {
                if let Some(png) = me_texture(Some(&card.me_tex), outfit, "body", None, mesh, slot) {
                    eprintln!("ok-extract: ME body {slot} bytes={}", png.len());
                    match setter {
                        0 => {
                            mesh.detail_mask_png = Some(png.to_vec());
                            mesh.runtime_textures.remove("_DetailMask");
                        },
                        1 => mesh.line_mask_png = Some(png.to_vec()),
                        2 => mesh.normal_map_png = Some(png.to_vec()),
                        _ => mesh.alpha_mask_png = Some(png.to_vec()),
                    }
                }
            }
        }
        if let Some(mask) = mesh.alpha_mask_png.take() {
            mesh.alpha_mask_png = body_mask_for_alpha_state(&mask, clothes_alpha_state(manifest.clothes_states[0]));
            if mesh.alpha_mask_png.is_some() { body_blend = "masked"; }
        }
        profile_tick("body masks+ME", t_mark); t_mark = Instant::now();
        let normal_now = mesh.normal_map_png.take();
        let converted = match normal_job {
            Some(job) if normal_now.is_some() && normal_now == normal_source => job.join().ok(),
            Some(job) => { let _ = job.join(); None }
            None => None,
        };
        mesh.normal_map_png = converted.or_else(|| normal_now.map(convert_runtime_normal_map));
        profile_tick("body normal convert", t_mark); t_mark = Instant::now();
        if let Some(card)=parsed_card.as_ref() {apply_native_skin_draw_settings(mesh,card,outfit,true);}
        let (p, tex) = emit_part(out_dir, &mut session_parts, "body", "body", mesh, body_blend, false)?;
        profile_tick("body draw+emit", t_mark); t_mark = Instant::now();
        if let Some(part) = session_parts.last_mut() {
            part.normal_detail = body_normal_detail_png;
        }
        if let (Some(card), Some(part)) = (parsed_card.as_ref(), session_parts.last_mut()) {
            // ChaControl.ChangeSettingNip / ChangeSettingUnderhair assign these
            // to the draw material after CreateBodyTexture. They use separate
            // authored UV channels and must not be baked into its MainTex.
            if let Some((png, settings)) = load_catalog_display_texture(
                game_root, &vanilla, &mod_catalog, 423, card.nip_id, "NipTex",
            ) {
                part.overtex_png = Some(png);
                assign_display_texture_settings(part, "_overtex1", settings);
            }
            part.overcolor = card.nip_color;
            part.nip_size = card.areola_size;
            part.nip_specular = card.nip_gloss_power;
            if card.underhair_id != 0 {
                if let Some((png, settings)) = load_catalog_display_texture(
                    game_root, &vanilla, &mod_catalog, 424, card.underhair_id, "UnderhairTex",
                ) {
                    part.overtex2_png = Some(png);
                    assign_display_texture_settings(part, "_overtex2", settings);
                }
                part.overcolor2 = card.underhair_color;
            }
            eprintln!("ok-extract: body display overtex nip={} underhair={} uv1={} uv2={} color={}",
                part.overtex_png.as_ref().map_or(0, Vec::len),
                part.overtex2_png.as_ref().map_or(0, Vec::len),
                part.uv2.len(), part.uv3.len(), part.vertex_colors.len());
        }
        seen_meshes.insert(mesh.name.to_ascii_lowercase());
        parts.push(ManifestPart {
            role: "body".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: body_blend.into(),
            ..Default::default()
        });
    }
    // Penis and scrotum are separate body renderers. A replacement body that
    // does not ship them keeps the penis inside o_body_a; do not also attach
    // the vanilla meshes to that rig. The gallery hides these until asked.
    let mut genital_emitted = 0;
    // Parts keep the order the sources list them (a HashMap here gave a different part order on every run).
    let mut genital_best: Vec<ExtractedMesh> = Vec::new();
    for mesh in genital_sources {
        if mesh.positions.is_empty() || !is_genital_display_mesh(&mesh.name) { continue; }
        match genital_best.iter_mut().find(|prev| prev.name.eq_ignore_ascii_case(&mesh.name)) {
            Some(prev) => { if prev.positions.len() < mesh.positions.len() { *prev = mesh; } }
            None => genital_best.push(mesh),
        }
    }
    for mut mesh in genital_best {
        if !seen_meshes.insert(mesh.name.to_ascii_lowercase()) { continue; }
        let part_skel = mesh.smr_skel.clone();
        mesh.skel_kind = 1;
        if let (Some(sk), Some(ps)) = (body_skel.as_ref(), part_skel.as_ref()) {
            remap_part_to_body(&mut mesh, sk, ps);
            let _ = skin_mesh(&mut mesh, sk);
        } else if let Some(sk) = body_skel.as_ref() {
            let _ = skin_mesh(&mut mesh, sk);
        }
        let stem = format!("genital_{}", mesh.name.replace(['/', '\\'], "_"));
        let (p, tex) = emit_part(out_dir, &mut session_parts, &stem, "body", &mesh, "opaque", false)?;
        eprintln!("ok-extract: genital mesh {} verts={} tex={}",
            mesh.name, mesh.positions.len(), mesh.texture_png.as_ref().map_or(0, Vec::len));
        genital_emitted += 1;
        parts.push(ManifestPart {
            role: "body".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: "opaque".into(),
            ..Default::default()
        });
    }
    if genital_emitted == 0 && parsed_card.as_ref().is_some_and(|card| card.sex == 0) {
        eprintln!("ok-extract: no separate penis or scrotum mesh; the display toggle has nothing to switch");
    }
    // NOTE: the mouth (o_mnpa/o_mnpb) is a face child whose SMR has its own
    // bone list and a runtime pattern atlas (cf_t_mnpb_00_XX); it needs the
    // mouth SMR skeleton + cf_m_face composite, not the body skeleton.
    // Mouth (lips): o_mnpa/o_mnpb have their own SMR bone list; rig with that
    // skeleton and use the cf_t_mnpb_00_XX pattern atlas.
    for mname in ["o_mnpb", "o_mnpa"] {
        let Some(mut mesh) = body_meshes
            .iter()
            .filter(|m| m.name.eq_ignore_ascii_case(mname))
            .max_by_key(|m| m.positions.len())
            .cloned()
        else {
            continue;
        };
        if !seen_meshes.insert(mesh.name.to_ascii_lowercase()) {
            continue;
        }
        // o_mnpa / o_mnpb are hidden by the card preview (StudioEditor hides
        // them too); they are not the mouth.
        if is_hidden_part(&mesh.name) {
            continue;
        }
        let part_skel = crate::unity::skeleton_for_mesh(&body_bundle, &mesh.name);
        rig_mesh(
            &mut mesh,
            "mouth",
            body_skel.as_ref(),
            head_skel.as_ref(),
            None,
            part_skel.as_ref(),
            None,
            None,
            None,
        );
        if std::env::var("OK_FACE_RAW").is_ok() {
            for i in 0..4 {
                let n = format!("cf_t_mnpb_00_{i:02}");
                if let Some((_, png)) = crate::unity::named_png_from_bundle(
                    &game_root.join("abdata/chara/etc.unity3d"),
                    &[&n],
                ) {
                    let _ = std::fs::write(
                        format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\mnpb_{i:02}.png"),
                        &png,
                    );
                }
            }
        }
        if let Some((_, png)) = crate::unity::named_png_from_bundle(
            &game_root.join("abdata/chara/etc.unity3d"),
            &["cf_t_mnpb_00_00"],
        ) {
            mesh.texture_png = Some(png);
        }
        let stem = format!("mouth_{}", sanitize(&mesh.name));
        let (p, tex) = emit_part(out_dir, &mut session_parts, &stem, "mouth", &mesh, "masked", false)?;
        parts.push(ManifestPart {
            role: "mouth".into(),
            path: p,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: "masked".into(),
            ..Default::default()
        });
    }
    if std::env::var("OK_FACE_RAW").is_ok() {
        for m in head_meshes.iter() {
            if m.name.to_ascii_lowercase().contains("face") {
                eprintln!(
                    "ok-extract: HEADFACE {} path={} v={} tris={}",
                    m.name,
                    m.path_id,
                    m.positions.len(),
                    m.indices.len() / 3
                );
            }
        }
    }
    // ChaControl loads the head prefab and draws the renderer named `cf_O_face`.
    // `p_cf_head_bone.source_mesh` is 0 (different bundle), so matching by path
    // id, or falling back to the largest mesh, bound the face texture to
    // `cf_O_eyeline` and never emitted the face. KK_091207's session had no
    // `cf_O_face`; the editor drew that eyeline opaque.
    let face_mesh = pick_named(&head_meshes, "cf_O_face").cloned().or_else(|| {
        eprintln!(
            "ok-extract: cf_O_face missing from head prefab '{head_main}', using vanilla p_cf_head_00"
        );
        let vb = crate::gamefs::read_source(&vanilla_head_bundle).unwrap_or_default();
        if vb.is_empty() {
            return None;
        }
        let parsed = crate::unity::parse_bundle_cached(vb);
        let (ms, _) = parsed.meshes_for_maindata("p_cf_head_00");
        pick_named(&ms, "cf_O_face").cloned()
    });
    if let Some(mut mesh) = face_mesh.filter(|_| std::env::var("OK_SKIP_FACE").is_err()) {
        if let Some(mat) = mesh.mat.as_ref() {
            eprintln!("ok-extract: face material={} shader={} rimV={:?} rimPower={:?} faceNormal={:?} shadowColor={:?}",
                mat.name, mat.shader, mat.float("_rimV"), mat.float("_rimpower"),
                mat.float("_FaceNormalG"), mat.color("_ShadowColor"));
        }
        eprintln!(
            "ok-extract: face mesh={} verts={} prefab={head_main}",
            mesh.name,
            mesh.positions.len()
        );
        // Diagnostic `OK_FACE_DROP_CAVITY`: drop `cf_O_face` triangles whose
        // vertices all sample the vanilla `cf_face_00_t` mouth-cavity rectangle
        // (a dark patch otherwise shows on the chin).
        if std::env::var("OK_FACE_DROP_CAVITY").is_ok() {
            let mut kept = Vec::with_capacity(mesh.indices.len());
            let mut dropped = 0usize;
            for t in mesh.indices.chunks_exact(3) {
                let all = t.iter().all(|&v| {
                    let q = mesh.uvs.get(v as usize).copied().unwrap_or([0.0, 2.0]);
                    (0.28..=0.72).contains(&q[0])
                        && ((0.78..=1.0).contains(&q[1]) || (0.005..=0.23).contains(&q[1]))
                });
                if all {
                    dropped += 1;
                } else {
                    kept.extend_from_slice(t);
                }
            }
            eprintln!(
                "ok-extract: DROPCAVITY face tris {} -> {} dropped={}",
                mesh.indices.len() / 3,
                kept.len() / 3,
                dropped
            );
            mesh.indices = kept;
        }
        // aaWeightsHead.AssignedWeightsAndSetBounds remaps the SMR's own bone
        // list (component.bones[i].name) onto objHeadBone = p_cf_head_bone
        // (ChaControl.cs:8497 / AssignedAnotherWeights.cs:150). Use the face
        // SMR's own skeleton as the rebind source.
        let face_own_skel = parsed_head.skeleton(mesh.path_id);
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: FACE own_skel={} head_skel={}",
                face_own_skel.as_ref().map(|s| s.names.len()).unwrap_or(0),
                head_skel.as_ref().map(|s| s.names.len()).unwrap_or(0)
            );
        }
        if std::env::var("OK_CHINDIAG").is_ok() {
            if let Some(ps) = face_own_skel.as_ref() {
                eprintln!("FACEOWN bones={}", ps.names.len());
                for (i, n) in ps.names.iter().enumerate() {
                    eprintln!("  own[{i}] {n}");
                }
            }
            if let Some(bi) = mesh.bone_idx.get(342) {
                eprintln!("RAW342 bi={bi:?} w={:?}", mesh.weights.get(342));
            }
            let mut low = 0usize;
            let mut hist = [0usize; 5];
            for w in &mesh.weights {
                let s = w[0] + w[1] + w[2] + w[3];
                let b = if s < 0.5 {
                    low += 1;
                    0
                } else if s < 0.9 {
                    1
                } else if s < 1.1 {
                    2
                } else {
                    3
                };
                hist[b] += 1;
            }
            eprintln!(
                "WEIGHTSUM n={} low<0.5={} mid={} ok={} hi={} ",
                mesh.weights.len(),
                hist[0],
                hist[1],
                hist[2],
                hist[3]
            );
        }
        let key = mesh.name.to_ascii_lowercase();
        let authored_fbs = HeadFbsInput {
            mouth:head_mouth_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyes:head_eye_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyebrow:head_eyebrow_patterns.get(&key).map(|(_,p)|p.as_slice()),
        };
        rig_mesh(
            &mut mesh,
            "head",
            body_skel.as_ref(),
            head_skel.as_ref(),
            None,
            face_own_skel.as_ref(),
            None,
            Some(&authored_fbs),
            None,
        );
        if std::env::var("OK_CHINDIAG").is_ok() {
            if let Some(sk) = head_skel.as_ref() {
                for n in [
                    "cf_J_ChinLow",
                    "cf_J_Chin_s",
                    "cf_J_ChinTip_Base",
                    "cf_J_CheekLow_s_L",
                    "cf_J_FaceLow_sx",
                    "cf_J_FaceBase",
                ] {
                    if let Some(w) = sk.world_tf(n) {
                        let p = crate::pose::mat_pos(&w);
                        eprintln!("CHINWORLD {n} {p:?}");
                    }
                }
            }
            let mut zero = 0usize;
            let mut tot = 0usize;
            for (i, p) in mesh.positions.iter().enumerate() {
                if p[1] < 1.34 && p[1] > 1.28 && p[0].abs() > 0.03 && p[0].abs() < 0.05 && p[2] > -0.01 {
                    tot += 1;
                    if tot <= 6 {
                        eprintln!(
                            "CHINVERT i={i} p={p:?} bi={:?} w={:?}",
                            mesh.bone_idx.get(i),
                            mesh.weights.get(i)
                        );
                        if let Some(sk) = head_skel.as_ref() {
                            if let Some(bi) = mesh.bone_idx.get(i) {
                                for b in bi {
                                    let name = sk.names.get(*b as usize).map(|s| s.as_str()).unwrap_or("?");
                                    eprintln!("  bone {b} {name}");
                                }
                            }
                        }
                    }
                    if mesh.bone_idx.get(i).map(|b| b[0] == 0).unwrap_or(false) {
                        zero += 1;
                    }
                }
            }
            eprintln!("CHINVERT side_n={tot} bone0={zero}");
        }
        // Material Editor's face MainTex replaces the draw texture after
        // CreateFaceTexture. Only bake the create shader when no replacement
        // is present for this material.
        if let Some(card) = parsed_card.as_ref() {
            profile_tick("genital+mouth+face prep", t_mark); t_mark = Instant::now();
            let t_face_tex = Instant::now();
            if let Some(png) = me_maintex(Some(&card.me_tex), outfit, "head", None, &mesh) {
                mesh.texture_png = Some(png.to_vec());
                // CreateFaceTexture has already installed its RT in the game
                // before MaterialEditor replaces it; no CPU bake is required.
                mark_runtime_main_texture(&mut mesh);
                mark_material_editor_main_texture(&mut mesh);
                eprintln!("ok-extract: face Material Editor MainTex applied bytes={}", png.len());
            } else if let Some(png) = compose_face_texture(
                game_root,
                &vanilla,
                &mod_catalog,
                card,
                &head_texs,
                &body_texs,
                // CreateFaceTexture writes the same card skinMainColor as the body.
                card.skin_color,
                card.skin_sub_color,
                outfit,
                &guid_index,
            ) {
                mesh.texture_png = Some(png);
                mark_runtime_main_texture(&mut mesh);
            } else {
                // A missing list row used to leave cf_O_face with the bundle's
                // empty render texture (solid black). Every card, mod head or
                // not, gets at least the skin colour.
                eprintln!(
                    "ok-extract: face compose failed headId={}, skin fallback",
                    card.head_id
                );
                mesh.texture_png = crate::unity::solid_png(skin_tint, 512, 512);
            }
            profile_tick("compose_face_texture", t_face_tex);
            // MaterialEditor applies texture properties to the draw material
            // after CustomTextureControl has built the face MainTex.  The
            // card's face DetailMask can replace the bundle default entirely;
            // leaving the default here puts its green mask edge on the brow.
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "detailmask") {
                mesh.detail_mask_png = Some(png.to_vec());
                mesh.runtime_textures.remove("_DetailMask");
            }
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "linemask") {
                mesh.line_mask_png = Some(png.to_vec());
            }
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "normalmap") {
                mesh.normal_map_png = Some(png.to_vec());
            }
            if let Some(png) = me_texture(Some(&card.me_tex), outfit, "head", None, &mesh, "alphamask") {
                mesh.alpha_mask_png = Some(png.to_vec());
            }
        } else if let Some(png) = mesh.texture_png.as_ref() {
            if let Some(t) = crate::unity::tint_png(png, skin_tint) {
                mesh.texture_png = Some(t);
            }
        }
        if std::env::var("OK_FACE_SKIN").is_ok() {
            mesh.texture_png = crate::unity::solid_png(skin_tint, 64, 64);
        }
        if let Some(card)=parsed_card.as_ref() {apply_native_skin_draw_settings(&mut mesh,card,outfit,false);}
        if let Some(card)=parsed_card.as_ref() {apply_native_face_overlays(game_root,&vanilla,&mod_catalog,card,outfit,&mut mesh);}
        // main_skin clips min(max(r,1-_alpha_a),max(g,1-_alpha_b)) < 0.5 on the face too: a card's _AlphaMask
        // (Material Editor) cuts the scalp that would show through its hair. Disabled channels are whitened as for
        // the body, and the face draws masked like the body.
        let face_blend = face_alpha_blend(&mut mesh);
        let (p, tex) =
            emit_part(out_dir, &mut session_parts, "head", "head", &mesh, face_blend, false)?;
        seen_meshes.insert(mesh.name.to_ascii_lowercase());
        parts.push(ManifestPart {
            role: "head".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend: face_blend.into(),
            unlit: true,
            ..Default::default()
        });
    }
    profile_tick("face overlays+emit", t_mark);
    let mut seen_head = std::collections::HashSet::new();
    let t_hdet = Instant::now();
    // An OK Studio eyes pattern whose eye set shows a gag eye (ChaControl.ChangeEyesPtn): that object is built, with
    // the set's texture.
    let studio_eyes = crate::studio_status::current().and_then(|o| o.eyes_ptn).and_then(|p| crate::eye_set::eye_set(game_root, p));
    for mut mesh in head_meshes.iter().cloned() {
        let key = mesh.name.to_ascii_lowercase();
        // FBS target references are authoritative, including inactive tears
        // and arbitrarily named mod meshes. Visibility must not remove their
        // controller channels from the session.
        let fbs_target = head_mouth_patterns.contains_key(&key)
            || head_eye_patterns.contains_key(&key)
            || head_eyebrow_patterns.contains_key(&key);
        let shown_gag = studio_eyes.as_ref().is_some_and(|set| crate::eye_set::eye_reference(&mesh.name) >= 3
            && crate::eye_set::part_shown(set, &mesh.name) == Some(true));
        // Pupils of an eye set with an expression (hearts, stars): the texture they draw over themselves.
        if crate::eye_set::eye_reference(&mesh.name) == 2 {
            if let Some(png) = studio_eyes.as_ref().and_then(|set| crate::eye_set::expression_png(game_root, set)) { mesh.shader_extra_textures.insert("_expression".into(), png); }
        }
        if shown_gag {
            if let Some(png) = studio_eyes.as_ref().and_then(|set| crate::eye_set::gag_texture_png(game_root, set)) { mesh.texture_png = Some(png); }
            // Its sprite sheet flips, pulses and turns in the drawing (Koikano/sub_texture_animation).
            if let Some(set) = studio_eyes.as_ref().filter(|set| set.animates_gag()) { set.apply_gag_animation(mesh.mat.get_or_insert_with(Default::default)); }
        } else {
            if !fbs_target && !is_head_detail(&mesh.name) {
                continue;
            }
            if !fbs_target && is_hidden_part(&mesh.name) {
                continue;
            }
        }
        if std::env::var("OK_NOHEADDETAIL").is_ok() {
            continue;
        }
        if let Ok(skip) = std::env::var("OK_SKIP_DETAIL") {
            let ln = mesh.name.to_ascii_lowercase();
            if skip
                .split(',')
                .filter(|s| !s.is_empty())
                .any(|s| ln.contains(&s.to_ascii_lowercase()))
            {
                continue;
            }
        }
        if std::env::var("OK_SKIP_SCLERA").is_ok() {
            let ln = mesh.name.to_ascii_lowercase();
            if (ln == "cf_ohitomi_l" || ln == "cf_ohitomi_r") {
                continue;
            }
        }
        let head_key = mesh.name.to_ascii_lowercase();
        if !seen_head.insert(head_key.clone()) || !seen_meshes.insert(head_key) {
            continue;
        }
        let part_skel = parsed_head.skeleton(mesh.path_id);
        if std::env::var("OK_WDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("hitomi") {
            for i in 0..mesh.bone_idx.len().min(3) {
                let b = mesh.bone_idx[i];
                let w = mesh.weights.get(i).copied().unwrap_or([0.0; 4]);
                let n0 = part_skel
                    .as_ref()
                    .and_then(|s| s.names.get(b[0] as usize).cloned())
                    .unwrap_or_default();
                let n1 = part_skel
                    .as_ref()
                    .and_then(|s| s.names.get(b[1] as usize).cloned())
                    .unwrap_or_default();
                eprintln!(
                    "ok-extract: RAWWD {} v{i} i={:?} w={:?} n0={} n1={}",
                    mesh.name, b, w, n0, n1
                );
            }
        }
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!(
                "ok-extract: HDET {} path={} partskel={} binds={} verts={} normals={}",
                mesh.name,
                mesh.path_id,
                part_skel.as_ref().map(|s| s.names.len()).unwrap_or(0),
                mesh.bind_poses.len(),
                mesh.positions.len(),
                mesh.normals.len()
            );
        }
        let key = mesh.name.to_ascii_lowercase();
        let authored_fbs = HeadFbsInput {
            mouth:head_mouth_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyes:head_eye_patterns.get(&key).map(|(_,p)|p.as_slice()),
            eyebrow:head_eyebrow_patterns.get(&key).map(|(_,p)|p.as_slice()),
        };
        rig_mesh(
            &mut mesh,
            "head",
            body_skel.as_ref(),
            head_skel.as_ref(),
            None,
            part_skel.as_ref(),
            None,
            Some(&authored_fbs),
            None,
        );
        if let Some(card) = parsed_card.as_ref() {
            resolve_head_detail_texture(
                game_root,
                &vanilla,
                &mod_catalog,
                card,
                &guid_index,
                outfit,
                &mut mesh,
                &head_texs,
                &body_texs,
            );
        }
        if mesh.texture_png.is_none() {
            let ln = mesh.name.to_ascii_lowercase();
            if ln.contains("hitomi") {
                mesh.texture_png = simple_iris_png();
            } else if ln.contains("noseline") || ln.contains("nose") {
                mesh.texture_png = crate::unity::solid_png([0.04, 0.03, 0.04, 0.35], 32, 32);
            } else if ln.contains("eyeline") || ln.contains("mayuge") {
                // A missing line texture must not become an opaque dark quad
                // over the eyes. The game simply has no texture on that pass.
                mesh.texture_png = crate::unity::solid_png([0.0, 0.0, 0.0, 0.0], 32, 32);
            } else if ln.contains("tooth") || ln.contains("canine") || ln.contains("tang") {
                // A missing source MainTex uses the shader's white default.
                mesh.texture_png = crate::unity::solid_png([1.0; 4], 8, 8);
            }
        }
        let stem = format!("head_{}", sanitize(&mesh.name));
        seen_meshes.insert(mesh.name.to_ascii_lowercase());
        let (blend, unlit) = blend_for_mesh("head", &mesh);
        let ln = mesh.name.to_ascii_lowercase();
        let eyeline_up = ln.contains("eyeline") && !ln.contains("low");
        // Unity draws extra materials on the last submesh. rendEyelineUp has
        // materials[1]=EyelineShadowTex * skinMainColor (ChaControl.cs:4046/4073).
        if eyeline_up {
            if let Some(card) = parsed_card.as_ref() {
                // ChaControl.ChangeSettingEyelineUp assigns slot0 and slot1
                // separately. Preserve slot1 identity before looking up ME
                // MainTex/Color; the stroke's replacement belongs only to slot0.
                let mut kage = eyeline_shadow_mesh(&parsed_head,&mesh);
                resolve_head_detail_texture(game_root,&vanilla,&mod_catalog,card,&guid_index,
                    outfit,&mut kage,&head_texs,&body_texs);
                if kage.texture_png.is_some() {
                    head_fbs_sources.insert(kage.name.to_ascii_lowercase(), key.clone());
                    eprintln!("ok-extract: KAGE {} slot1={}",kage.name,
                        kage.mat.as_ref().map(|m|m.name.as_str()).unwrap_or(""));
                    let kstem = format!("head_{}", sanitize(&kage.name));
                    let (kp, ktex) = emit_part(
                        out_dir,
                        &mut session_parts,
                        &kstem,
                        "head",
                        &kage,
                        &blend,
                        unlit,
                    )?;
                    parts.push(ManifestPart {
                        #[cfg(test)]
                        source_mesh: Some(mesh.name.clone()),
                        role: "head".into(),
                        path: kp,
                        guid: None,
                        mesh: kage.name.clone(),
                        texture: ktex,
                        blend: blend.clone(),
                        unlit,
                        ..Default::default()
                    });
                }
            }
        }
        let (p, tex) = emit_part(out_dir, &mut session_parts, &stem, "head", &mesh, &blend, unlit)?;
        parts.push(ManifestPart {
            role: "head".into(),
            path: p,
            guid: None,
            mesh: mesh.name.clone(),
            texture: tex,
            blend,
            unlit,
            ..Default::default()
        });
    }
    profile_tick("head_detail meshes", t_hdet);

    let t_ctx = Instant::now();
    let mut accessory_context=None;
    if let Some(card) = parsed_card.as_ref() {
        let context=std::sync::Arc::new(crate::accessory_extract::Context {
            schema:1,dependencies:crate::accessory_extract::Context::dependencies(card,outfit)?,
            body:body_skel.clone(),head:head_full_skel.as_ref().or(head_skel.as_ref()).cloned(),
            head_textures:head_texs.clone(),body_textures:body_texs.clone(),
            hair_gloss:selected_hair_gloss.as_ref().map(|g|g.0.clone()),
            hair_gloss_sampling:selected_hair_gloss.as_ref().map(|g|[g.1.filter,g.1.wrap_u,g.1.wrap_v,i32::from(g.1.srgb),i32::from(g.1.mip_count>1)]),
        });
        accessory_context=Some(context.clone());
        // OK_FIRSTLOAD=A2: the item passes below keep their meshes here; flushed after the subclothes.
        let mut push_batch = PushBatch::default();
        manifest.card = card_path.map(|path|path.to_string_lossy().into_owned()).unwrap_or_default();
        manifest.marker = card.marker.clone();
        manifest.lastname = card.lastname.clone();
        manifest.firstname = card.firstname.clone();
        manifest.nickname = card.nickname.clone();
        manifest.head_id = card.head_id;
        manifest.hair_ids = card.hair_ids;
        manifest.shape_face = card.shape_face.clone();
        manifest.shape_body = card.shape_body.clone();
        manifest.clothes_ids = card.clothes_ids.clone();
        manifest.hair_color = display_hair(card.hair_color);
        manifest.skin_color = skin_tint;
        manifest.clothes_color = card.clothes_color;
        manifest.sub_parts_id = card.sub_parts_id.clone();
        manifest.outfit = outfit;
        manifest.mapped_sliders = vec!["bone_morph ShapeBodyInfoFemale+ShapeHeadInfoFemale".into()];
        manifest.unmapped_face.clear();
        manifest.unmapped_body.clear();
        let index = &guid_index;
        let mut zip_lists: std::collections::HashMap<String, ChaListCatalog> =
            std::collections::HashMap::new();
        let mut seen_part = std::collections::HashSet::new();
        // Accessory slots already emitted by the KKEx resolve pass, so the
        // vanilla `ChaFileAccessory.parts` pass does not drop the 2nd slot when
        // two slots share the same item id (e.g. the miko sleeve's L/R halves).
        let mut acc_slots_done: std::collections::HashSet<usize> =
            std::collections::HashSet::new();
        let want_outfit = outfit_prefix(outfit);
        profile_tick("accessory context+manifest", t_ctx);
        check_preempted()?;
        let t_res = Instant::now();
        for resolve in &card.resolves {
            if !is_mesh_category(resolve.category_no) {
                continue;
            }
            let p = resolve.property.to_ascii_lowercase();
            if p.contains("outfit") && !p.contains(&want_outfit) {
                continue;
            }
            let role_early = role_for_category(resolve.category_no, &resolve.property);
            if role_early.starts_with("clothes") {
                continue;
            }
            if role_early.starts_with("acc") {
                if let Some(si) = accessory_slot_from_property(&resolve.property) {
                    acc_slots_done.insert(si);
                    // An OK Studio state switches the slot off (ChaFileStatus.showAccessory): not loaded.
                    if crate::studio_status::accessory_hidden(si) { continue; }
                }
            }
            if is_dummy_part("", Some(&resolve.name)) {
                continue;
            }
            let key = format!("{}:{}:{}", resolve.category_no, resolve.slot, resolve.property);
            if !seen_part.insert(key) {
                continue;
            }
            let zip_path = guid_path(&index, &resolve.mod_id);
            if zip_path.is_none() {
                manifest.missing.push(resolve.mod_id.clone());
            }
            let entry = lookup_entry(
                &vanilla,
                &mut zip_lists,
                zip_path.map(|p| p.as_path()),
                &resolve.mod_id,
                resolve.category_no,
                resolve.slot,
                resolve.local_slot,
            );
            let Some(entry) = entry else {
                if zip_path.is_some() {
                    let dummy = ListEntry {
                        category_no: resolve.category_no,
                        id: resolve.slot,
                        name: resolve.name.clone(),
                        main_ab: String::new(),
                        main_data: String::new(),
                        main_tex: String::new(),
                        parent: String::new(),
                        extra: Default::default(),
                    };
                    let role = role_for_category(resolve.category_no, &resolve.property);
                    if role.starts_with("acc") {
                        let source=crate::accessory_extract::Source {entry:dummy.clone(),zip:zip_path.cloned(),guid:Some(resolve.mod_id.clone()),
                            category:resolve.category_no,id:resolve.slot,property:resolve.property.clone(),display:resolve.name.clone(),missing_entry:true};
                        crate::accessory_extract::emit(game_root,card,outfit,&source,&context,out_dir,&mut parts,&mut session_parts,&mut seen_meshes,Some(&mut push_batch))?;
                        continue;
                    }
                    let shake_enabled = if (101..=104).contains(&resolve.category_no) && role.contains("hair") && !role.contains("acc") {
                        !card.hair_parts[(resolve.category_no - 101) as usize].no_shake
                    } else if role.contains("acc") {
                        accessory_slot_from_property(&resolve.property)
                            .and_then(|s| card.accessory.get(s)).is_none_or(|p| !p.no_shake)
                    } else { true };
                    let (mut meshes, part_skel) = meshes_for_entry_shake(
                        game_root, zip_path.map(|p| p.as_path()), &dummy, shake_enabled);
                    if role.contains("hair") && !role.contains("acc") {
                        set_selected_hair_gloss(&mut meshes, selected_hair_gloss.as_ref().map(|g|g.0.as_slice()));
                        if (101..=104).contains(&resolve.category_no) {set_hair_part_materials(&mut meshes,&card.hair_parts[(resolve.category_no-101) as usize]);}
                    }
                    let pk = parent_for_part(card, &resolve.property, "", role);
                    let acc_local = acc_local_for(card, &resolve.property);
                    push_meshes(
                        &mut parts,
                        &mut session_parts,
                        out_dir,
                        game_root,
                        role,
                        meshes,
                        Some(resolve.slot),
                        Some(resolve.category_no),
                        Some(resolve.mod_id.clone()),
                        Some(resolve.property.clone()),
                        Some(resolve.name.clone()),
                        body_skel.as_ref(),
                        head_full_skel.as_ref().or(head_skel.as_ref()),
                        pk.as_deref(),
                        if role.contains("hair") && !role.contains("acc") {
                            Some(hair_cols_for(card, resolve.category_no))
                        } else {
                            None
                        },
                        &mut seen_meshes,
                        part_skel.as_ref(),
                        acc_local.as_ref(),
                        Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                        Some(&card.me_color),
                        Some(&card.me_shader),
                        Some(&card.me_float),
                        outfit,
                        None,
                        accessory_assignment(game_root,zip_path.map(|p|p.as_path()),&dummy).as_ref(), None, Some(&mut push_batch),
                    )?;
                }
                continue;
            };
            let role = role_for_category(resolve.category_no, &resolve.property);
            if role.starts_with("clothes") {
                continue;
            }
            if role.starts_with("acc") {
                let source=crate::accessory_extract::Source {entry:entry.clone(),zip:zip_path.cloned(),
                    guid:Some(resolve.mod_id.clone()),category:resolve.category_no,id:resolve.slot,
                    property:resolve.property.clone(),display:if resolve.name.is_empty(){entry.name.clone()}else{resolve.name.clone()},missing_entry:false};
                crate::accessory_extract::emit(game_root,card,outfit,&source,&context,out_dir,&mut parts,&mut session_parts,&mut seen_meshes,Some(&mut push_batch))?;
                continue;
            }
            let (mut meshes, part_skel) = meshes_for_entry_shake(game_root, zip_path.map(|p|p.as_path()), &entry,
                !(101..=104).contains(&resolve.category_no)||!card.hair_parts[(resolve.category_no-101) as usize].no_shake);
            if role.contains("hair") && !role.contains("acc") {
                set_selected_hair_gloss(&mut meshes, selected_hair_gloss.as_ref().map(|g|g.0.as_slice()));
            }
            let display = if resolve.name.is_empty() {
                entry.name.clone()
            } else {
                resolve.name.clone()
            };
            let pk = parent_for_part(card, &resolve.property, &entry.parent, role);
            let acc_local = acc_local_for(card, &resolve.property);
            let tint = if role.contains("hair") && !role.contains("acc") {
                Some(hair_cols_for(card, resolve.category_no))
            } else {
                None
            };
            if (101..=104).contains(&resolve.category_no) && role.contains("hair") {
                set_hair_part_materials(&mut meshes,&card.hair_parts[(resolve.category_no - 101) as usize]);
            }
            let assignment=if role.contains("acc") {accessory_assignment(game_root,zip_path.map(|p|p.as_path()),&entry)} else {None};
            push_meshes(
                &mut parts,
                &mut session_parts,
                out_dir,
                game_root,
                role,
                meshes,
                Some(resolve.slot),
                Some(resolve.category_no),
                Some(resolve.mod_id.clone()),
                Some(resolve.property.clone()),
                Some(display),
                body_skel.as_ref(),
                head_full_skel.as_ref().or(head_skel.as_ref()),
                pk.as_deref(),
                tint,
                &mut seen_meshes,
                part_skel.as_ref(),
                acc_local.as_ref(),
                Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                Some(&card.me_color),
                Some(&card.me_shader),
                Some(&card.me_float),
                outfit,
                None,
                assignment.as_ref(), None, Some(&mut push_batch),
            )?;
        }
        profile_tick("resolves (hair/acc KKEx)", t_res);
        // Hair slots from ChaFileHair.parts even if UAR skipped them.
        let t_hair = Instant::now();
        for (i, &hid) in card.hair_ids.iter().enumerate() {
            // Id 0 is a real vanilla hairstyle in every hair category (back hair 0 is the default long hair of many
            // cards); empty slots are "なし" catalog entries, which is_dummy_part skips below.
            let cat = 101 + i as i32;
            if std::env::var("OK_DIAG").is_ok() {
                for r in card.resolves.iter().filter(|r| (100..=110).contains(&r.category_no)) {
                    eprintln!(
                        "ok-extract: hairresolve cat={} slot={} local={} guid={} name={} prop={}",
                        r.category_no, r.slot, r.local_slot, r.mod_id, r.name, r.property
                    );
                }
            }
            if parts.iter().any(|p| p.slot == Some(hid) && p.category_no == Some(cat)) {
                continue;
            }
            let resolve = card.resolves.iter().find(|r| {
                (r.slot == hid || r.local_slot == hid)
                    && (r.category_no == cat || (101..=104).contains(&r.category_no))
            });
            let (guid, zip_path, property, display) = if let Some(r) = resolve {
                (
                    r.mod_id.clone(),
                    guid_path(&index, &r.mod_id).cloned(),
                    r.property.clone(),
                    r.name.clone(),
                )
            } else {
                (String::new(), None, format!("ChaFileHair.parts[{i}]"), String::new())
            };
            let entry = lookup_entry(
                &vanilla,
                &mut zip_lists,
                zip_path.as_deref(),
                &guid,
                cat,
                hid,
                hid,
            );
            if std::env::var("OK_DIAG").is_ok() {
                eprintln!(
                    "ok-extract: hair id={hid} cat={cat} resolve={:?} entry={:?}",
                    resolve.map(|r| (&r.mod_id, &r.name, &r.property, r.slot, r.local_slot, r.category_no)),
                    entry.as_ref().map(|e| (&e.name, &e.main_data, e.category_no))
                );
            }
            let Some(entry) = entry else {
                eprintln!("ok-extract: hair slot {hid} cat {cat} not in catalog");
                continue;
            };
            if is_dummy_part(&entry.main_data, Some(&entry.name))
                || is_dummy_part("", Some(&display))
            {
                eprintln!("ok-extract: skip dummy hair id={hid} cat={cat} {display}");
                continue;
            }
            let role = role_for_category(cat, &property);
            let (mut meshes, part_skel) = meshes_for_entry_shake(
                game_root, zip_path.as_deref(), &entry, !card.hair_parts[i].no_shake);
            set_selected_hair_gloss(&mut meshes, selected_hair_gloss.as_ref().map(|g|g.0.as_slice()));
            set_hair_part_materials(&mut meshes,&card.hair_parts[i]);
            let name = if display.is_empty() {
                entry.name.clone()
            } else {
                display
            };
            eprintln!(
                "ok-extract: extra hair id={hid} cat={cat} meshes={} name={name}",
                meshes.len()
            );
            push_meshes(
                &mut parts,
                &mut session_parts,
                out_dir,
                game_root,
                role,
                meshes,
                Some(hid),
                Some(cat),
                if guid.is_empty() { None } else { Some(guid) },
                Some(property),
                Some(name),
                body_skel.as_ref(),
                head_full_skel.as_ref().or(head_skel.as_ref()),
                Some("cf_J_FaceUp_ty"),
                Some(hair_cols_for(card, cat)),
                &mut seen_meshes,
                part_skel.as_ref(),
                None,
                Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                Some(&card.me_color),
                Some(&card.me_shader),
                Some(&card.me_float),
                outfit,
                None,
                None, None, Some(&mut push_batch),
            )?;
        }
        profile_tick("hair slots", t_hair);
        // Accessories from ChaFileAccessory.parts (slots with no KKEx entry, e.g. necklaces).
        let t_acc = Instant::now();
        for (slot_i, a) in card.accessory.iter().enumerate() {
            // ChaFileAccessory uses category 120 for an empty slot. Zero is
            // also a valid item id in other categories (e.g. stock glasses).
            if a.type_id == 120 || !is_mesh_category(a.type_id) {
                continue;
            }
            let role = role_for_category(a.type_id, "");
            if role.starts_with("clothes") {
                continue;
            }
            // ChangeAccessory only instantiates accessory categories; a slot
            // saved with another type shows nothing in the game.
            if !role.starts_with("acc") {
                eprintln!("ok-extract: accessory[{slot_i}] cat={} id={} is not an accessory category; skipped", a.type_id, a.id);
                continue;
            }
            // Skip only the slots the resolve pass already emitted; dedup by
            // (category,id) would drop a second slot sharing the same item id.
            if acc_slots_done.contains(&slot_i) || crate::studio_status::accessory_hidden(slot_i) {
                continue;
            }
            // ChaFileAccessory.parts stores the card's local id. Slots with a
            // KKEx resolve are added by the `card.resolves` pass above; what is
            // left here has no mod resolve, so the id is a *vanilla* item. A mod
            // catalog entry sharing the same (category, id) must not shadow it
            // (e.g. cat124 id14 is the vanilla `p_acs_neck_star`, not a mod item
            // that happens to also use id 14).
            let entry = vanilla
                .get(a.type_id, a.id)
                .cloned()
                .or_else(|| mod_catalog.get(a.type_id, a.id).cloned());
            let Some(entry) = entry else {
                eprintln!(
                    "ok-extract: accessory[{slot_i}] cat={} id={} not in catalog",
                    a.type_id, a.id
                );
                continue;
            };
            if is_dummy_part(&entry.main_data, Some(&entry.name)) {
                continue;
            }
            let source=crate::accessory_extract::Source {zip:entry.extra.get("__zip").map(PathBuf::from),
                guid:None,category:a.type_id,id:a.id,property:format!("outfit{outfit}.accessory{slot_i}"),
                display:entry.name.clone(),entry,missing_entry:false};
            crate::accessory_extract::emit(game_root,card,outfit,&source,&context,out_dir,&mut parts,&mut session_parts,&mut seen_meshes,Some(&mut push_batch))?;
        }
        profile_tick("accessories", t_acc);
        // Clothes from ChaFileClothes.parts (per outfit).
        // Sideloader's UniversalAutoResolver.ResolveStructure identifies an item
        // by (GUID, original Slot) from the per-outfit ResolveInfo whose
        // Property == "outfitN.ChaFileClothes.<Name>" (StructReference
        // ChaFileClothesGenerator). The id stored in the card is a resolved
        // LocalSlot that goes stale when the mod updates, so the ResolveInfo is
        // the authority. Only fall back to the card id for vanilla items.
        let t_clo = Instant::now();
        let bra_over_mask = over_layer_mask_png(game_root, card, &vanilla, &mod_catalog, &index, outfit, "OverBraMask");
        let inner_over_mask = over_layer_mask_png(game_root, card, &vanilla, &mod_catalog, &index, outfit, "OverInnerMask");
        let mut top_type = 0;
        let mut top_not_bra = false;
        let mut not_bot = false;
        let mut sub_not_bra = [None; 2];
        let mut hide_shorts_bot = false;
        let mut bottom_present = false;
        let mut not_shorts = false;
        let mut top_prefab_world = crate::pose::ident();
        for (kind, &cid) in card.clothes_ids.iter().enumerate() {
            if kind >= CLOTHES_PROP.len() {
                break;
            }
            // 105..=111 for parts[0..6]; both shoe slots are category 112.
            let cat = if kind < 7 { 105 + kind as i32 } else { 112 };
            let Some((entry, guid, zip_path)) = selected_clothes.as_ref()
                .and_then(|slots| slots.get(kind))
                .and_then(|slot| slot.as_ref()).cloned() else {
                continue;
            };
            if kind == 0 {
                top_type = entry.info("Kind").parse().unwrap_or(0);
                top_not_bra = entry.info("NotBra").trim() == "1";
                not_bot = entry.info("Coordinate").trim() == "2";
            } else if kind == 1 {
                let listed = entry.info("HideShorts");
                hide_shorts_bot = if listed.is_empty() {
                    [2, 3, 9, 16, 22, 25, 28, 29, 30, 32, 34, 35, 45].contains(&cid)
                } else {
                    listed.trim() == "1"
                };
            } else if kind == 2 {
                not_shorts = entry.info("Coordinate").trim() == "2";
            }
            if !crate::unity::clothes_kind_visible(kind, &manifest.clothes_states, manifest.shoes_type) {
                continue;
            }
            if is_dummy_part(&entry.main_data, Some(&entry.name)) {
                continue;
            }
            if kind == 1 && not_bot {
                continue;
            }
            let zip = entry
                .extra
                .get("__zip")
                .map(PathBuf::from)
                .or_else(|| zip_path.clone());
            let t_part = Instant::now();
            let (mut meshes, part_skel) = meshes_for_entry(game_root, zip.as_deref(), &entry);
            profile_tick("clothes meshes_for_entry", t_part);
            if kind == 0 { top_prefab_world = part_skel.as_ref().map_or_else(crate::pose::ident, |s| s.root_world()); }
            if meshes.is_empty() {
                continue;
            }
            if kind == 1 {
                bottom_present = true;
            }
            resolve_material_alpha_masks(game_root, &mut meshes);
            eprintln!(
                "ok-extract: clothes kind={kind} cat={cat} id={cid} meshes={} entry={}",
                meshes.len(),
                entry.name
            );
            let cols = card
                .clothes_colors
                .get(kind)
                .copied()
                .unwrap_or([[1.0, 1.0, 1.0, 1.0]; 4]);
            let t_part = Instant::now();
            let pats = build_clothes_patterns(
                game_root,
                &vanilla,
                &mod_catalog,
                card,
                &guid_index,
                kind,
                0,
                &head_texs,
                &body_texs,
                outfit,
            );
            profile_tick("clothes patterns", t_part);
            // ChaControl.CreateClothesTexture + ChangeCustomClothes: up to 3
            // create RTs (MainTex / MainTex02 / MainTex03) assigned to
            // rendNormal01 / rendNormal02 / rendNormal03.
            let t_part = Instant::now();
            let cmp = accessory_bundle_bytes(game_root, zip.as_deref(), &entry)
                .and_then(|b| crate::unity::clothes_renderer_map(&b, &entry.main_data));
            profile_tick("clothes renderer map", t_part);
            let t_part = Instant::now();
            let (baked, used_mask) = bake_clothes_create_textures(
                game_root,
                &entry,
                cols,
                &pats,
                &head_texs,
                &body_texs,
            );
            if baked[0].is_none() && std::env::var("OK_DIAG").is_ok() {
                eprintln!(
                    "ok-extract: clothes no list MainTex cat={cat} id={cid} mt='{}' zip={:?}",
                    entry.info("MainTex"),
                    entry.extra.get("__zip")
                );
            }
            profile_tick("clothes bake create", t_part);
            let t_part = Instant::now();
            apply_clothes_create_textures(&mut meshes, cmp.as_ref(), &baked);
            profile_tick("clothes apply create", t_part);
            let t_part = Instant::now();
            finalize_clothes_texture(&mut meshes);
            profile_tick("clothes finalize", t_part);
            let t_part = Instant::now();
            apply_clothes_emblems(game_root, &vanilla, &mod_catalog, card, kind, &mut meshes, &head_texs, &body_texs);
            profile_tick("clothes emblems", t_part);
            apply_clothes_overlay(card,outfit,["ct_clothesTop","ct_clothesBot","ct_bra","ct_shorts","ct_gloves","ct_panst","ct_socks","ct_shoes_inner","ct_shoes_outer"][kind],&mut meshes,cmp.as_ref(),&baked);
            if std::env::var("OK_CLOTHDIAG").is_ok() {
                for m in &meshes {
                    if let Some(png) = m.texture_png.as_deref() {
                        let slot = cmp
                            .as_ref()
                            .and_then(|mp| clothes_create_slot(&m.renderer_ids, mp))
                            .unwrap_or(0);
                        eprintln!(
                            "ok-extract: CLOTHDIAG kind={kind} cat={cat} mesh={} slot={slot} used={} col0={:?} mean={:?}",
                            m.name,
                            used_mask,
                            cols[0],
                            png_mean(png)
                        );
                    }
                }
            }
            let role = role_for_category(cat, "");
            let t_part = Instant::now();
            // ChaControl loads top/bottom with copyDynamicBone=true: the prefab
            // components retarget their roots onto aaWeightsBody before skinning.
            {
                if let (Some(part), Some(body), Some((bytes,source_zip))) = (
                    part_skel.as_ref(), body_skel.as_mut(),
                    accessory_bundle_source(game_root, zip.as_deref(), &entry),
                ) {
                    let parsed = crate::unity::parse_bundle_cached(bytes);
                    parsed.configure_game_references_from_zip(game_root,source_zip.as_deref());
                    clothes_dynamics.extend(crate::dbone::implant::shared_clothes(&parsed, part, body,
                        game_root, clothes_load_settings(&format!("outfit{outfit}.clothes{kind}")).1, None, kind<=1));
                }
            }
            profile_tick("clothes dynamics", t_part);
            let t_part = Instant::now();
            push_meshes(
                &mut parts,
                &mut session_parts,
                out_dir,
                game_root,
                role,
                meshes,
                Some(cid),
                Some(cat),
                if guid.is_empty() { None } else { Some(guid) },
                Some(format!("outfit{outfit}.clothes{kind}")),
                Some(entry.name.clone()),
                body_skel.as_ref(),
                head_skel.as_ref(),
                None,
                if used_mask {
                    None
                } else {
                    Some([cols[0]; 3])
                },
                &mut seen_meshes,
                part_skel.as_ref(),
                None,
                Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                Some(&card.me_color),
                Some(&card.me_shader),
                Some(&card.me_float),
                outfit,
                if kind == 2 {
                    let (scale, offset) = if entry.info("Coordinate") == "2" { (2.0, -1.0) } else { (1.0, 0.0) };
                    bra_over_mask.as_deref().map(|mask| (mask, scale, offset))
                } else { None },
                None, None, Some(&mut push_batch),
            )?;
            profile_tick("clothes push", t_part);
        }
        profile_tick("clothes", t_clo);
        // Sub-clothes: ChaFileClothes.subPartsId (ChaFileDefine.ClothesSubKind
        // partsA/B/C). ChaControl.ChangeClothesTopPartsNoAsync loads them only
        // when the top exists (Kind 1 or 2), at category
        // `(top.Kind == 1 ? 200 : 210) + kind`, parented under ct_clothesTop and
        // skinned to the body. Try jacket (210+kind) then sailor (200+kind).
        let t_sub = Instant::now();
        {
            // ChaControl.ChangeClothesTopPartsAsync picks category
            // `(1 == topType ? 200 : 210) + kind` from the top's list Kind.
            // The top's Kind selects the subpart list even when the saved ID
            // is zero. ID 0 is a real vanilla item (e.g. jacket subpart A),
            // not an absent-part sentinel. Resolved mod entries below retain
            // their explicit category and take precedence.
            for kind in 0..3usize {
                if manifest.clothes_states[0] == 3 {
                    continue;
                }
                let sid = card.sub_parts_id.get(kind).copied().unwrap_or(0);
                // Sailor sub-clothes are categories 200/201/202 and jackets
                // 210/211/212. `ChaControl.ChangeClothesTopPartsAsync` selects
                // `(1 == topType ? 200 : 210) + kind`; Sideloader records which
                // one is equipped via the ResolveInfo Property name
                // ("ClothesSailorSubX" / "ClothesJacketSubX"), which also pins
                // the item to (GUID, original Slot) so a stale card id does not
                // load the wrong mod.
                let Some((cat, entry, guid, zip_path)) = selected_top_subparts.as_ref()
                    .and_then(|sub| sub.get(kind))
                    .and_then(|choice| choice.as_ref()).cloned() else {
                    continue;
                };
                if is_dummy_part(&entry.main_data, Some(&entry.name)) {
                    continue;
                }
                if kind < 2 {
                    sub_not_bra[kind] = Some(entry.info("NotBra").trim() == "1");
                }
                let zip = entry
                    .extra
                    .get("__zip")
                    .map(PathBuf::from)
                    .or_else(|| zip_path.clone());
                let (mut meshes, part_skel) =
                    meshes_for_entry(game_root, zip.as_deref(), &entry);
                if meshes.is_empty() {
                    continue;
                }
                resolve_material_alpha_masks(game_root, &mut meshes);
                eprintln!(
                    "ok-extract: subclothes kind={kind} cat={cat} id={sid} meshes={} entry={} ab={} data={}",
                    meshes.len(),
                    entry.name, entry.main_ab, entry.main_data
                );
                // ChaControl.ChangeCustomClothes(false, kind, ...) reads the
                // *top* part's colors for every sub kind, and sub-kind C
                // (`!main && 2 == kind`) takes `_Color` from `colorInfo[3]`
                // instead of `colorInfo[0]`.
                let top_cols = card
                    .clothes_colors
                    .first()
                    .copied()
                    .unwrap_or([[1.0, 1.0, 1.0, 1.0]; 4]);
                let ch0 = if kind == 2 { 3 } else { 0 };
                let mut cols = [[1.0f32; 4]; 4];
                cols[0] = top_cols[ch0];
                cols[1] = top_cols[1];
                cols[2] = top_cols[2];
                let pats = build_clothes_patterns(
                    game_root,
                    &vanilla,
                    &mod_catalog,
                    card,
                    &guid_index,
                    0,
                    ch0,
                    &head_texs,
                    &body_texs,
                    outfit,
                );
                let cmp = accessory_bundle_bytes(game_root, zip.as_deref(), &entry)
                    .and_then(|b| crate::unity::clothes_renderer_map(&b, &entry.main_data));
                let (baked, used_mask) = bake_clothes_create_textures(
                    game_root,
                    &entry,
                    cols,
                    &pats,
                    &head_texs,
                    &body_texs,
                );
                apply_clothes_create_textures(&mut meshes, cmp.as_ref(), &baked);
                finalize_clothes_texture(&mut meshes);
                #[cfg(test)]
                if let Ok(folder)=std::env::var("OK_AUDIT_SUBPART_OUTPUT") {
                    let folder=PathBuf::from(folder);std::fs::create_dir_all(&folder).unwrap();
                    let prefix=format!("outfit{outfit}_sub{kind}");
                    for (i,png) in baked.iter().enumerate() {if let Some(png)=png {std::fs::write(folder.join(format!("{prefix}_baked{i}.png")),png).unwrap();}}
                    std::fs::write(folder.join(format!("{prefix}.json")),serde_json::to_vec(&serde_json::json!({
                        "colors":cols,"top_colors":top_cols,"entry":entry.extra,
                        "map":cmp.as_ref().map(|m|format!("{m:?}")),
                        "meshes":meshes.iter().map(|m|serde_json::json!({"name":m.name,"renderer_ids":m.renderer_ids})).collect::<Vec<_>>()
                    })).unwrap()).unwrap();
                }
                apply_clothes_emblems(game_root, &vanilla, &mod_catalog, card, 0, &mut meshes, &head_texs, &body_texs);
                apply_clothes_overlay(card,outfit,["ct_top_parts_A","ct_top_parts_B","ct_top_parts_C"][kind],&mut meshes,cmp.as_ref(),&baked);
                let role = role_for_category(cat, "");
                // Top subparts use the same copyDynamicBone path as the top.
                if let (Some(part), Some(body), Some((bytes,source_zip))) = (
                    part_skel.as_ref(), body_skel.as_mut(),
                    accessory_bundle_source(game_root, zip.as_deref(), &entry),
                ) {
                    let parsed = crate::unity::parse_bundle_cached(bytes);
                    parsed.configure_game_references_from_zip(game_root,source_zip.as_deref());
                    clothes_dynamics.extend(crate::dbone::implant::shared_clothes(&parsed, part, body,
                        game_root, clothes_load_settings(&format!("outfit{outfit}.clothesTopParts{kind}")).1, Some(&top_prefab_world),true));
                }
                push_meshes(
                    &mut parts,
                    &mut session_parts,
                    out_dir,
                    game_root,
                    role,
                    meshes,
                    Some(sid),
                    Some(cat),
                    if guid.is_empty() { None } else { Some(guid) },
                    Some(format!("outfit{outfit}.clothesTopParts{kind}")),
                    Some(entry.name.clone()),
                    body_skel.as_ref(),
                    head_skel.as_ref(),
                    None,
                    if used_mask {
                        None
                    } else {
                        Some([cols[0]; 3])
                    },
                    &mut seen_meshes,
                    part_skel.as_ref(),
                    Some(&top_prefab_world),
                    Some(&card.me_renderer_off),
                        Some(&card.me_tex),
                    Some(&card.me_color),
                    Some(&card.me_shader),
                    Some(&card.me_float),
                    outfit,
                    if kind == 0 { inner_over_mask.as_deref().map(|mask| (mask, 1.0, 0.0)) } else { None },
                    None, None, Some(&mut push_batch),
                )?;
            }
        }
        profile_tick("subclothes", t_sub);
        let t_batch = Instant::now();
        push_batch.flush(&mut parts, &mut session_parts)?;
        profile_tick("deferred meshes", t_batch);
        // ChaControl.DrawClothes disables the bra renderer when the selected
        // top (or its sailor/jacket subparts) sets NotBra. The subpart rules
        // replace the top's value exactly as ChangeClothesTopPartsAsync does.
        let not_bra = match top_type {
            1 => sub_not_bra[0].unwrap_or(top_not_bra),
            2 if sub_not_bra.iter().any(Option::is_some) =>
                sub_not_bra.iter().any(|value| *value == Some(true)),
            _ => top_not_bra,
        };
        if not_bra {
            parts.retain(|part| part.role != "clothes_bra");
            session_parts.retain(|part| part.role != "clothes_bra");
        }
        // DrawClothes suppresses shorts if the bra occupies both coordinates,
        // or if a full worn bottom requests HideShorts. The latter uses the
        // list's HideShorts cell, falling back to vanilla botHideIds only when
        // that cell is absent.
        if not_shorts || (hide_shorts_bot && bottom_present && manifest.clothes_states[1] == 0) {
            parts.retain(|part| part.role != "clothes_shorts");
            session_parts.retain(|part| part.role != "clothes_shorts");
        }
        eprintln!("ok-extract: top_type={top_type} top_not_bra={top_not_bra} sub_not_bra={sub_not_bra:?} bra_visible={} hide_shorts_bot={hide_shorts_bot} not_shorts={not_shorts} shorts_visible={}", !not_bra, !not_shorts && !(hide_shorts_bot && bottom_present && manifest.clothes_states[1] == 0));
    }
    if parts.is_empty() && session_parts.is_empty() {
        return Err("no meshes extracted".into());
    }
    manifest.skin_color = skin_tint;
    manifest.parts = parts;
    #[cfg(test)]
    if let Ok(path)=std::env::var("OK_SOURCE_PART_MANIFEST_OUTPUT") {
        // Test-only provenance: preserve the existing emission order and card
        // property identity without exporting/reimporting geometry or textures.
        let bytes=serde_json::to_vec_pretty(&manifest).map_err(|e|e.to_string())?;
        std::fs::write(path,bytes).map_err(|e|e.to_string())?;
    }
    if let Some(dir) = out_dir {
        let manifest_path = dir.join("manifest.json");
        let json = serde_json::to_string_pretty(&manifest).map_err(|e| e.to_string())?;
        std::fs::write(&manifest_path, json).map_err(|e| e.to_string())?;
        if let Some(c) = card_path {
            let _ = std::fs::write(dir.join(".ok_stamp"), stamp_text(game_root, c, outfit));
        }
    }
    profile_tick("clothes+hair+write", t);
    let t_keys = Instant::now();
    let body_keys = crate::shape::load_keys_named(game_root, "chara/oo_base.unity3d", "cf_anmShapeBody");
    let head_keys = head_shape_keys;
    profile_tick("load shape keys (session)", t_keys);
    clear_entry_prefetch();
    profile_tick("extract_card_outfit TOTAL", t_all);
    for (name, s) in stages.map(crate::appearance::timings::Capture::finish).unwrap_or_default() {
        eprintln!("ok-extract: STAGE {name:28} {:8.1}ms x{} max {:.1}ms", s.total_ms, s.calls, s.max_ms);
    }
    crate::profile::dump_spots();
    let (shape_body, shape_face, sex, type_bone, nip, abm) = parsed_card
        .as_ref()
        .map(|c| {
            (
                c.shape_body.clone(),
                c.shape_face.clone(),
                c.sex,
                c.type_bone,
                c.nip_stand_rate,
                c.abm.clone(),
            )
        })
        .unwrap_or_else(|| (Vec::new(), Vec::new(), 0, 0, 0.0, Vec::new()));
    let sexfaces_installed = game_root.join("BepInEx/plugins/KKS_SexFaces.dll").ok_is_file();
    if let Some((_, sampling)) = selected_hair_gloss.as_ref() {
        // ChaControl.LoadHairGlossMask selects one Texture2D for the card;
        // HairAccessoryCustomizer can attach the same asset to mod hair.
        // Preserve that asset's sampler and mip policy for every part that
        // actually received it, independent of renderer/card names.
        for part in &mut session_parts {
            if part.hair_gloss.is_some() {
                part.runtime_textures.insert("_HairGloss".into(), [
                    sampling.filter, sampling.wrap_u, sampling.wrap_v,
                    i32::from(sampling.srgb), i32::from(sampling.mip_count > 1),
                ]);
            }
        }
    }
    for part in &mut session_parts {
        let display_key = part.name.to_ascii_lowercase();
        let key = head_fbs_sources.get(&display_key).unwrap_or(&display_key);
        if part.role == "head" {
            // ChaControl.UpdateVisible selects tears independently of eye
            // patterns; default tearsLv=0 hides all three referenced objects.
            // Retain their FBS data while keeping the preview draw state.
            if let Some(source) = head_meshes.iter().find(|mesh| mesh.name.eq_ignore_ascii_case(key)) {
                // ChaReference resolves GameObjects, not the Mesh asset name.
                // A mod can rename the asset without changing its tear object.
                if is_hidden_head_reference(source) {
                    // An OK Studio state shows the tears object of its level (ChaControl.ChangeTearsLv); otherwise
                    // the hidden references stay hidden.
                    let tears = source.renderer_names.iter().map(String::as_str).chain(std::iter::once(source.name.as_str())).find_map(crate::studio_status::tears_size);
                    part.render_visible = match (tears, crate::studio_status::current().and_then(|o| o.tears_lv)) {
                        (Some(size), Some(level)) => size == level,
                        _ => false,
                    };
                }
                // The OK Studio eye set hides n_eyebase / n_hitomi and shows its gag eye.
                if let Some(set) = crate::studio_status::current().and_then(|o| o.eyes_ptn).and_then(|p| crate::eye_set::eye_set(game_root, p)) {
                    for name in source.renderer_names.iter().map(String::as_str).chain(std::iter::once(source.name.as_str())) {
                        match crate::eye_set::part_shown(&set, name) {
                            Some(false) => { part.render_visible = false; break; }
                            Some(true) => { part.render_visible = true; break; }
                            None => {}
                        }
                    }
                }
                // ChaReference resolves the actual head GameObject, rather than
                // treating every mesh with canine in its name as double teeth.
                if is_double_tooth_reference(source) {
                    part.render_visible = parsed_card.as_ref().is_some_and(|card| card.double_tooth);
                    eprintln!("ok-extract: doubleTooth name={} visible={}", part.name, part.render_visible);
                }
            }
        }
        if let Some((target_index, patterns)) = head_mouth_patterns.get(key) {
            part.mouth_patterns = patterns.clone();
            if sexfaces_installed {
                let raw_x = head_meshes.iter()
                    .find(|m| m.name.eq_ignore_ascii_case(&part.name))
                    .map(|m| m.positions.iter().map(|p| p[0]).collect::<Vec<_>>());
                apply_sexfaces_mouth_extensions(part, *target_index, raw_x.as_deref());
            }
        }
        if part.role.contains("head") || part.role.contains("face") {
            part.eye_patterns=head_eye_patterns.get(key).map(|(_,p)|p.clone()).unwrap_or_default();
            part.eyebrow_patterns=head_eyebrow_patterns.get(key).map(|(_,p)|p.clone()).unwrap_or_default();
            let eyes=f32::from_bits(EYES_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed));
            let brow=f32::from_bits(EYEBROW_OPEN_MAX_BITS.load(std::sync::atomic::Ordering::Relaxed));
            part.face_default_rates=[eyes,brow];
            part.face_runtime_settings=head_face_settings;
            if let Some(settings)=head_eye_materials.get(key) {
                if let Some(raw)=head_meshes.iter().find(|mesh|mesh.name.eq_ignore_ascii_case(key)) {
                    let mut runtime=crate::eye_gaze::IrisRuntime {settings:*settings,raw_uvs:raw.uvs.clone()};
                    if let Some(card)=parsed_card.as_ref() {
                        let lerp=|a:f32,b:f32,t:f32| a+(b-a)*t.clamp(0.,1.);
                        runtime.settings[10]=lerp(0.2,-0.6,card.pupil_x)*if settings[0]==1. {-1.}else{1.};
                        runtime.settings[11]=lerp(-0.5,0.5,card.pupil_y);
                        runtime.settings[12]=lerp(1.8,-0.2,card.pupil_width);
                        runtime.settings[13]=lerp(1.8,-0.2,card.pupil_height);
                    }
                    runtime.apply(&mut part.uvs,0.,0.);
                    if let Some(card)=parsed_card.as_ref() {
                        part.eye_draw=source_eye_draw(game_root,&vanilla,&mod_catalog,card,raw,&guid_index);
                        if let Some(draw)=part.eye_draw.as_mut() {
                            draw.apply(runtime.st(0.,0.),&runtime.raw_uvs,&mut part.uv2,&mut part.uv3);
                            // EyeLookMaterialControll overrides MainTex ST each Update.
                            // Its transform is already on the dynamic UV stream.
                            part.main_tex_st=[1.,1.,0.,0.];
                        }
                    }
                    part.iris_runtime=Some(runtime);
                }
            }
            // The baseline is what apply_default_fbs baked into this mesh, with the inputs it had (an absent
            // controller as absent). A CPU head bakes the OBJ preview's pattern 0 at 80% open: assuming it closed
            // made every live mouth open twice, the tongue pushed out through the chin.
            fn none_if_empty(p:&[(i32,i32)])->Option<&[(i32,i32)]> {(!p.is_empty()).then_some(p)}
            let input=HeadFbsInput {mouth:none_if_empty(&part.mouth_patterns),
                eyes:none_if_empty(&part.eye_patterns),eyebrow:none_if_empty(&part.eyebrow_patterns)};
            let channels:Vec<&str>=part.blend_shapes.iter().map(|(name,_)|name.to_str().unwrap_or("")).collect();
            part.face_baseline_weights=default_fbs_weights(&part.name,&channels,Some(&input));
        }
    }
    // KK_InvisibleBody turns off every renderer under the body and head bones (body, face, eyes, hair) but not the
    // accessories at their attach points; the clothes hang under BodyTop and stay.
    if parsed_card.as_ref().is_some_and(|card| card.invisible_body) {
        for part in session_parts.iter_mut().filter(|p| !p.role.starts_with("clothes") && !p.role.starts_with("acc")) {
            part.render_visible = false;
        }
    }
    assign_draw_layers(&mut session_parts);
    Ok(crate::session::OkSession {
        accessory_context,
        editor_pregnancy:Default::default(),
        pv_config,pv_mask:15,pv_fk:false,
        eye_look_config,
        eye_look_runtime:None,
            head_look:crate::head_look::HeadLook::default(),
            physx_secondary:false,
            external_animation:None,
            scene_gpu_parts:Vec::new(),
            physics:Default::default(),
            body_interaction:None,
        game_root: game_root.to_path_buf(),
        ramp_png,
        shadow_depth: configured_shadow_depth(game_root),
        line_width: configured_line_width(game_root),
        line_depth: configured_line_depth(game_root),
        kano_ver_shift: configured_kano_ver_shift(game_root),
        body_rest,
        body_dynamics,
        body_dynamic_runtime: None,
        yure_policy:None,
        body_colliders,
        clothes_dynamics,
        clothes_dynamic_runtime: None,
        bust_softness: parsed_card.as_ref().map_or(0.5, |c| c.bust_softness),
        bust_weight: parsed_card.as_ref().map_or(0.5, |c| c.bust_weight),
        collider_settings: crate::dbone::game_colliders::Settings::load(game_root),
        collider_overrides: parsed_card.as_ref().map_or(crate::dbone::game_colliders::CardOverrides::default(), |c| c.collider_overrides),
        head_rest,
        body_skel,
        head_skel,
        body_keys,
        head_keys,
        hand_shapes: crate::shape::hand::HandShapes::load(game_root),
        user_fk: Vec::new(),
        user_fk_ids: Default::default(),
        hand_shape_ids: Default::default(),
        studio_ik_solver: None,
        studio_ik: Vec::new(),
            studio_ik_rotations: Vec::new(),
        correct,
        sex,
        type_bone,
        nip_stand_rate: nip,
        shape_body,
        shape_face,
        abm,
        parts: session_parts,
        coordinate_count: parsed_card
            .as_ref()
            .map(|c| c.coordinate_count)
            .unwrap_or(0),
        skel_export: None,
        anim_time: f32::MAX,
        paired_ik_runtime: None,
        anim_clip: std::env::var("OK_POSE").unwrap_or_else(|_| "Stand_D_00".to_string()),
        anim_scene_mode: 0,
        anim_parameters: std::collections::HashMap::new(),
        anim_clock:Default::default(),anim_crossfade:None,anim_layer_crossfades:Default::default(),
        body_shaped: None,
        shape_reference_scratch: None,
        head_shaped: None,
        head_full_rest,
        head_full_posed: head_full_skel,
    })
}
