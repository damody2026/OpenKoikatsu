//! ModBoneImplantor 1.1.2: BoneImplantProcess and live body parenting.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use super::*;
pub(crate) mod rig;

pub(crate) fn world_exact(body:&Skeleton,name:&str)->Option<Mat4> {
    let id=body.tfs.iter().find(|(_,tf)|tf.name==name).map(|(id,_)|*id)?;
    Some(world_of(id,&body.tfs,&mut HashMap::new()))
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Marker { pub component: i64, pub source: Option<i64>, pub destination: Option<i64>, pub colliders: Vec<i64> }

pub(crate) fn installed(root: &Path) -> bool {
    ["BepInEx/plugins/ModBoneImplantor.dll", "BepInEx/plugins/ModBoneImplantor/ModBoneImplantor.dll"]
        .iter().any(|path| root.join(path).ok_is_file())
}

pub(crate) fn shared_clothes(parsed:&crate::unity::ParsedBundle, source:&Skeleton, body:&Skeleton,
    game_root:&Path, name:&str, parent:Option<&Mat4>,copy_dynamic:bool) -> DBoneSet {
    parsed.configure_game_references(game_root);
    let markers=parsed.bone_implants_for_skeleton(source);
    let mut set=parsed.dynamic_bones_for_skeleton(source).with_game_fixes(game_root);
    if installed(game_root) && !markers.is_empty() {
        let mut rig=source.clone(); rename_prefab_root(&mut rig,name);
        let (implanted,_)=prepare(&mut rig,body,&markers,parent);
        set.add_implanted_colliders(&markers,&implanted);
        rig::loaded(&set,&rig,body,name,copy_dynamic)
    } else if copy_dynamic {set.retarget_clothes(source,body,name,parent)} else {DBoneSet::default()}
}

pub(crate) fn rename_prefab_root(skel:&mut Skeleton,name:&str) {
    let roots:Vec<_>=skel.tfs.iter().filter(|(_,tf)|tf.parent.is_none()||tf.parent==Some(0)
        ||tf.parent.is_some_and(|id|!skel.tfs.contains_key(&id))).map(|(id,_)|*id).collect();
    for id in roots {skel.tfs.get_mut(&id).unwrap().name=name.into();}
    skel.rebuild_worlds();
}

pub(crate) fn parse_resolved(file: &SerializedFile, skel: &Skeleton, scripts:&crate::unity::references::Scripts) -> Vec<Marker> {
    // Markers are BoneImplantProcess behaviours: a file without that script has none (decoding its every GameObject,
    // Transform and behaviour was a third of a card conversion's allocations).
    if !scripts.has_class(|class| class == "BoneImplantProcess") { return Vec::new(); }
    let mut transforms = HashMap::new(); let mut children = HashMap::new();
    let mut active_go=HashMap::new(); let mut component_order=HashMap::new();
    for info in &file.objects {
        if info.type_id != 4 && info.type_id != 1 { continue; }
        let Ok(obj) = file.scene_object(info) else { continue; }; let p=obj.class.properties();
        if info.type_id==1 {
            active_go.insert(info.path_id,behaviour_enabled(p.get("m_IsActive")));
            if let Some(UnityValue::Array(items))=p.get("m_Component") {
                for (order,item) in items.iter().enumerate() {
                    let id=pptr(item).or_else(||if let UnityValue::Object(fields)=item {
                        ["component","second","data","m_Component"].iter().find_map(|key|fields.get(*key).and_then(pptr))
                    } else {None});
                    if let Some(id)=id {component_order.insert(id,order);}
                }
            }
        }
        else if let Some(go)=p.get("m_GameObject").and_then(pptr) { transforms.insert(go,info.path_id); children.insert(info.path_id,pptr_list(p.get("m_Children"))); }
    }
    let active_tf:HashMap<_,_>=transforms.iter().map(|(go,tf)|(*tf,*active_go.get(go).unwrap_or(&true))).collect();
    let mut colliders = parse_colliders_resolved(file,None,scripts);
    colliders.sort_by_key(|(id,_)|component_order.get(id).copied().unwrap_or(usize::MAX));
    fn walk(id:i64,children:&HashMap<i64,Vec<i64>>,colliders:&[(i64,BodyCollider)],out:&mut Vec<i64>) {
        out.extend(colliders.iter().filter(|(_,c)|c.transform==id).map(|(id,_)|*id));
        if let Some(children_of)=children.get(&id) {for child in children_of {walk(*child,children,colliders,out);}}
    }
    let mut markers:Vec<_>=file.objects.iter().filter(|info|info.type_id==114).filter_map(|info| {
        let obj=file.scene_object(info).ok()?; let p=obj.class.properties();
        if scripts.class(p.get("m_Script"))!=Some("BoneImplantProcess") {return None;}
        let component=*transforms.get(&p.get("m_GameObject").and_then(pptr)?)?;
        if !skel.tfs.contains_key(&component) {return None;}
        let mut current=Some(component);
        for _ in 0..skel.tfs.len() {
            let Some(id)=current else {break;}; let Some(tf)=skel.tfs.get(&id) else {break;};
            if tf.parent.is_some_and(|parent|skel.tfs.contains_key(&parent)) && active_tf.get(&id)==Some(&false) {return None;}
            current=tf.parent;
        }
        Some((info.path_id,{
            let source=p.get("trfSrc").and_then(pptr); let mut ids=Vec::new();
            if let Some(id)=source {walk(id,&children,&colliders,&mut ids);}
            Marker {component,source,destination:p.get("trfDst").and_then(pptr),colliders:ids}
        }))
    }).collect();
    // Unity enumerates components in hierarchy order, rather than object-table order.
    fn hierarchy(id:i64,children:&HashMap<i64,Vec<i64>>,order:&mut HashMap<i64,usize>) {
        if order.contains_key(&id) {return;}
        order.insert(id,order.len());
        if let Some(ids)=children.get(&id) {for id in ids {hierarchy(*id,children,order);}}
    }
    let mut roots:Vec<_>=skel.tfs.iter().filter(|(_,tf)|tf.parent.is_none()||tf.parent==Some(0)
        ||tf.parent.is_some_and(|id|!skel.tfs.contains_key(&id))).map(|(id,_)|*id).collect(); roots.sort_unstable();
    let mut order=HashMap::new(); for root in roots {hierarchy(root,&children,&mut order);}
    markers.sort_by_key(|(id,marker)|(order.get(&marker.component).copied().unwrap_or(usize::MAX),
        component_order.get(id).copied().unwrap_or(usize::MAX)));
    markers.into_iter().map(|(_,marker)|marker).collect()
}

pub(crate) fn prepare(skel: &mut Skeleton, body: &Skeleton, markers: &[Marker], parent: Option<&Mat4>)
    -> (std::collections::HashSet<i64>,Vec<(i64,String)>) {
    let mut implanted=std::collections::HashSet::new(); let mut bridges=Vec::new();
    // Embed the top prefab's parent only on the private prefab branch. Body
    // bridge roots already live in character space and must not inherit it.
    if let Some(tf)=parent.and_then(attached_parent_tf) {
        let id=(i64::MIN..).find(|id|!skel.tfs.contains_key(id)).unwrap();
        let roots:Vec<_>=skel.tfs.iter().filter(|(_,tf)|tf.parent.is_none()||tf.parent==Some(0)).map(|(id,_)|*id).collect();
        skel.tfs.insert(id,tf); for root in roots {skel.tfs.get_mut(&root).unwrap().parent=Some(id);}
    }
    let mut mapped=HashMap::new();
    fn bridge(id:i64,body:&Skeleton,skel:&mut Skeleton,mapped:&mut HashMap<i64,i64>,bridges:&mut Vec<(i64,String)>)->Option<i64> {
        if let Some(id)=mapped.get(&id) {return Some(*id);}
        let original=body.tfs.get(&id)?;
        let parent=original.parent.filter(|id|body.tfs.contains_key(id)).and_then(|id|bridge(id,body,skel,mapped,bridges));
        let target=(i64::MIN..).find(|id|!skel.tfs.contains_key(id))?;
        let mut tf=original.clone(); tf.parent=parent; tf.name=format!("__ok_body_bridge/{}",original.name);
        skel.tfs.insert(target,tf); mapped.insert(id,target); bridges.push((target,original.name.clone())); Some(target)
    }
    for marker in markers {
        let (Some(src),Some(dst))=(marker.source,marker.destination) else {continue;};
        if src==dst||!skel.tfs.contains_key(&src) {continue;}
        let Some(name)=skel.tfs.get(&dst).map(|tf|tf.name.clone()) else {continue;};
        let Some(target)=body.tfs.iter().find(|(_,tf)|tf.name==name).map(|(id,_)|*id) else {continue;};
        let Some(parent)=bridge(target,body,skel,&mut mapped,&mut bridges) else {continue;};
        let after=children_of(&body.tfs,target).iter().enumerate()
            .map(|(i,id)|body.tfs[id].sibling_index.unwrap_or(i as u32)+1).max().unwrap_or(0);
        skel.reparent(src,Some(parent),after);
        let mut pending=vec![src]; while let Some(id)=pending.pop() {
            if !implanted.insert(id) {continue;} pending.extend(children_of(&skel.tfs,id));
        }
    }
    skel.rebuild_worlds(); (implanted,bridges)
}

#[cfg(test)]
pub(crate) static FORCE_SYNC_REBUILD:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);

pub(crate) fn sync(skel:&mut Skeleton,body:&Skeleton,bridges:&[(i64,String)]) {
    // Both callers own current caches: prefab preparation and AttachedDynamicSet
    // step rebuild them after changing locals. An unimplanted rig has no body
    // assignments to synchronize. Retain a rebuild for incomplete legacy data.
    #[cfg(test)] let force=FORCE_SYNC_REBUILD.load(std::sync::atomic::Ordering::Relaxed);
    #[cfg(not(test))] let force=false;
    if !force && bridges.is_empty() && skel.worlds.len()==skel.bone_ids.len()
        && skel.names.len()==skel.bone_ids.len() { return; }
    let names=crate::pose::rig_name_index(body,false);
    for (id,name) in bridges {if let (Some(target),Some(source))=(skel.tfs.get_mut(id),names.get(name).and_then(|source|body.tfs.get(source))) {
        target.pos=source.pos; target.rot=source.rot; target.scale=source.scale;
    }} skel.rebuild_worlds();
}

#[cfg(test)]
#[test]
fn empty_implant_sync_preserves_current_dynamic_pose_and_bridge_sync() {
    let mut tfs=HashMap::new();
    for (id,parent,name) in [(1,None,"root"),(2,Some(1),"child")] {
        tfs.insert(id,Tf {name:name.into(),parent,sibling_index:None,
            pos:[id as f32,2.,-3.],rot:[0.,0.,0.,1.],scale:[1.,2.,0.5]});
    }
    let mut body=Skeleton {names:vec![],worlds:vec![],rest:vec![],smr_count:2,
        tfs: tfs.into(),bone_ids:vec![1,2],source_mesh:0};body.rebuild_worlds();
    let mut private=body.clone();
    for frame in 0..64 {
        body.tfs.get_mut(&1).unwrap().pos[0]=frame as f32*0.125;body.rebuild_worlds();
        // Simulate a private particle write, independent of the animated body.
        private.tfs.get_mut(&2).unwrap().pos[1]=frame as f32*0.25;
        private.rebuild_worlds();let mut original=private.clone();original.rebuild_worlds();
        sync(&mut private,&body,&[]);
        assert_eq!(private.names,original.names);assert_eq!(private.worlds,original.worlds);
        assert_eq!(private.tfs[&2].pos,original.tfs[&2].pos);
    }
    // Nonempty bridges still apply the exact legacy locals and hierarchy.
    let mut original=private.clone();let source=&body.tfs[&1];
    let target=original.tfs.get_mut(&2).unwrap();
    target.pos=source.pos;target.rot=source.rot;target.scale=source.scale;original.rebuild_worlds();
    sync(&mut private,&body,&[(2,"root".into()),(77,"missing".into())]);
    assert_eq!(private.worlds,original.worlds);assert_eq!(private.tfs[&2].parent,Some(1));
    private.worlds.clear();private.names.clear();sync(&mut private,&body,&[]);
    assert_eq!(private.worlds,original.worlds);assert_eq!(private.names,original.names);
}
