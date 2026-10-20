//! Bake animated Studio attachment points, independent of character cards and game installation at playback.
use std::{collections::HashMap, path::Path};
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;

fn pid(v: &UnityValue) -> i64 {
    v.as_object()
        .and_then(|o| o.get("m_PathID"))
        .and_then(|v| v.as_i64())
        .unwrap_or(0)
}
fn loop_time(v: &UnityValue) -> Option<bool> {
    match v {
        UnityValue::Object(m) => m
            .get("m_LoopTime")
            .and_then(|v| v.as_bool())
            .or_else(|| m.values().find_map(loop_time)),
        UnityValue::Array(a) => a.iter().find_map(loop_time),
        _ => None,
    }
}

pub fn convert(game: &Path, lib: &Path) -> Result<(), String> {
    let catalog: crate::studio_items::Catalog = serde_json::from_slice(
        &std::fs::read(lib.join("studio_items.json")).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())?;
    let mut bundles = std::collections::BTreeMap::<String, Vec<&crate::studio_items::Item>>::new();
    for i in &catalog.items {
        if i.anime && !i.child.is_empty() {
            bundles.entry(i.bundle.clone()).or_default().push(i);
        }
    }
    let mut entries = Vec::new();
    let mut unsupported = Vec::new();
    for (bundle, items) in bundles {
        let parsed = crate::unity::parse_bundle_cached(
            std::fs::read(game.join(&bundle)).map_err(|e| format!("{bundle}: {e}"))?,
        );
        for item in items {
            let hierarchy = parsed.prefab_hierarchy(&item.prefab);
            let Some(child) = hierarchy.find(&item.child) else {
                unsupported.push(format!("{}: attachment missing", item.prefab));
                continue;
            };
            let animation = parsed.with_serialized(|sers| {
                let find = |class, id| sers.iter().find_map(|s|s.objects_of_type(class).into_iter().find(|i|i.path_id==id).and_then(|i|UnityObject::from_serialized_file(s,i).ok()));
                let gos: HashMap<i64,String> = sers.iter().flat_map(|s|s.objects_of_type(1).into_iter().filter_map(|i|UnityObject::from_serialized_file(s,i).ok().and_then(|o|o.name()).map(|n|(i.path_id,n)))).collect();
                for ser in sers { for ai in ser.objects_of_type(95) {
                    let a = UnityObject::from_serialized_file(ser,ai).ok()?;
                    let p = a.class.properties();
                    if gos.get(&p.get("m_GameObject").map(pid).unwrap_or(0)) != Some(&item.prefab) { continue; }
                    let controller = find(91,p.get("m_Controller").map(pid).unwrap_or(0))?;
                    let clips = crate::anim::controller_default_clips(&controller);
                    // A single default layer is unambiguous. Do not silently choose a leaf/layer for unsupported controllers.
                    if clips.len()!=1 { return None; }
                    let clip = find(74,pid(&clips[0]))?;
                    let (bindings,duration) = crate::anim::generic_clip_values(&clip,0.);
                    if !duration.is_finite() || duration<=0. { return None; }
                    let mut paths = HashMap::new();
                    for index in 0..hierarchy.nodes.len() {
                        let mut names=Vec::new(); let mut n=index;
                        while hierarchy.nodes[n].parent>=0 { names.push(hierarchy.nodes[n].name.as_str()); n=hierarchy.nodes[n].parent as usize; }
                        names.reverse(); paths.insert(crate::anim::unity_string_to_hash(&names.join("/")) as u32,index);
                    }
                    if bindings.iter().any(|(path,attr,ty,_)|*ty==4 && (1..=4).contains(attr) && !paths.contains_key(path)) { return None; }
                    let segments=(duration*120.).ceil().max(1.) as usize;
                    if segments>12000 { return None; }
                    let mut frames=Vec::new();
                    for f in 0..=segments {
                        let mut h=hierarchy.clone();
                        for (path,attr,ty,value) in crate::anim::generic_clip_values(&clip,duration*f as f32/segments as f32).0 {
                            if ty!=4 {continue;}
                            let Some(&index)=paths.get(&path) else {continue;}; let node=&mut h.nodes[index];
                            match (attr,value.len()) {
                                (1,3)=>node.pos=[value[0],value[1],value[2]],
                                (2,4)=>node.rot=crate::finalik::qnorm([value[0],value[1],value[2],value[3]]),
                                (3,3)=>node.scale=[value[0],value[1],value[2]],
                                (4,3)=>node.rot=crate::pose::euler_deg_to_quat([value[0],value[1],value[2]]), _=>{}
                            }
                        }
                        // Studio's guide replaces the prefab root; child coordinates are relative to that root.
                        h.nodes[0].pos=[0.;3]; h.nodes[0].rot=[0.,0.,0.,1.]; h.nodes[0].scale=[1.;3];
                        let (pos,rot,scale)=h.node_world(child)?;
                        frames.push(serde_json::json!({"pos":pos,"rot":rot,"scale":scale}));
                    }
                    let looping=loop_time(&UnityValue::Object(clip.class.properties().clone())).unwrap_or(false);
                    return Some(serde_json::json!({"group":item.group,"category":item.category,"no":item.no,"prefab":item.prefab,"clip":clip.name(),"duration":duration,"looping":looping,"pattern":0,"frames":frames}));
                }} None
            });
            if let Some(animation) = animation {
                entries.push(animation);
            } else {
                unsupported.push(format!(
                    "{}: no supported default Transform clip",
                    item.prefab
                ));
            }
        }
        crate::unity::clear_bundle_cache();
    }
    let result = serde_json::json!({"format":"ok-studio-child-animation-1","sample_rate":120,"animations":entries,"unsupported":unsupported});
    std::fs::write(
        lib.join("studio_child_animations.json"),
        serde_json::to_vec(&result).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())?;
    println!(
        "STUDIO CHILD ANIMATIONS converted={} unsupported={}",
        result["animations"].as_array().unwrap().len(),
        result["unsupported"].as_array().unwrap().len()
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    #[ignore = "installed game and base library: baked attachment matches native clip off the frame grid"]
    fn native_rotating_attachment() {
        use unity_asset_decode::object::UnityObject;
        let game = crate::test_support::game_path("");
        let parsed = crate::unity::parse_bundle_cached(
            std::fs::read(game.join("abdata/studio/00.unity3d")).unwrap(),
        );
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../Content/IndependentResources/base_v1/studio_child_animations.json");
        let value: serde_json::Value =
            serde_json::from_slice(&std::fs::read(lib).unwrap()).unwrap();
        let entry = value["animations"]
            .as_array()
            .unwrap()
            .iter()
            .find(|a| a["group"] == 10 && a["category"] == 3 && a["no"] == 397)
            .unwrap();
        let frames = entry["frames"].as_array().unwrap();
        parsed.with_serialized(|sers| {
            let clip = sers
                .iter()
                .find_map(|ser| {
                    ser.objects_of_type(74)
                        .into_iter()
                        .filter_map(|i| UnityObject::from_serialized_file(ser, i).ok())
                        .find(|o| o.name().as_deref() == Some("N_studio_kaiten"))
                })
                .unwrap();
            let duration = crate::anim::generic_clip_values(&clip, 0.).1;
            assert!((duration - entry["duration"].as_f64().unwrap() as f32).abs() < 1e-6);
            for phase in [
                0.,
                0.01,
                0.25,
                0.40283203125,
                0.496917724609375,
                0.5,
                0.73,
                0.99,
                1.,
            ] {
                let values = crate::anim::generic_clip_values(&clip, duration * phase).0;
                let q = &values
                    .iter()
                    .find(|(path, attr, ty, _)| {
                        *path == crate::anim::unity_string_to_hash("N_chousei/N_move") as u32
                            && *attr == 2
                            && *ty == 4
                    })
                    .unwrap()
                    .3;
                let expected = crate::finalik::qnorm([q[0], q[1], q[2], q[3]]);
                let index = phase * (frames.len() - 1) as f32;
                let lo = index.floor() as usize;
                let hi = (lo + 1).min(frames.len() - 1);
                let quat = |i: usize| {
                    let a = frames[i]["rot"].as_array().unwrap();
                    [
                        a[0].as_f64().unwrap() as f32,
                        a[1].as_f64().unwrap() as f32,
                        a[2].as_f64().unwrap() as f32,
                        a[3].as_f64().unwrap() as f32,
                    ]
                };
                let actual = crate::finalik::qlerp(quat(lo), quat(hi), index - lo as f32);
                let dot = (0..4)
                    .map(|i| expected[i] * actual[i])
                    .sum::<f32>()
                    .abs()
                    .min(1.);
                let error = 2. * dot.acos();
                println!("phase {phase:.9}: angular error {error:.7} radians");
                assert!(error < 0.002, "native attachment angular error {error}");
            }
        });
    }
}
