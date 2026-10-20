//! One prefab rig shared by every renderer and DynamicBone component.
use super::*;

#[derive(Clone,Debug,serde::Serialize,serde::Deserialize)]
pub(crate) struct Rig {
    name:String,
    transforms:Vec<(i64,Tf)>,
    #[serde(serialize_with="crate::ordered::map")]
    links:HashMap<i64,String>,
    colliders:Vec<(i64,BodyCollider)>,
}
#[derive(Clone,Debug,serde::Serialize,serde::Deserialize)]
enum Node {Body(String),Private(i64)}
#[derive(Clone,Debug,serde::Serialize,serde::Deserialize)]
pub(crate) struct Bindings {
    rig:String, root:Node, component:Option<Node>, exclusions:Vec<Node>, not_rolls:Vec<Node>,
    nodes:Vec<(Node,bool)>, colliders:Vec<i64>, implanted_colliders:Vec<i64>,
}

pub(crate) fn key(name:&str,id:i64)->String {format!("__ok_clothes/{name}/{id}")}

pub(crate) fn loaded(set:&DBoneSet,source:&Skeleton,body:&Skeleton,name:&str,copy_dynamic:bool)->DBoneSet {
    let links:HashMap<_,_>=source.tfs.iter().filter_map(|(id,tf)|
        tf.name.strip_prefix("__ok_body_bridge/").map(|name|(*id,name.to_owned()))).collect();
    let live:std::collections::HashSet<_>=source.tfs.keys().filter(|id|
        !DBoneSet::removed_clothes_transform(source,**id)).copied().collect();
    let private=|id:i64| live.contains(&id).then_some(Node::Private(id));
    let node=|id:i64,copy:bool| {
        if copy {if let Some(name)=source.tfs.get(&id).map(|tf|&tf.name) {
            if body.tfs.values().any(|tf|tf.name==*name) {return Some(Node::Body(name.clone()));}
        }} private(id)
    };
    let mut bones=Vec::new();
    for original in &set.bones {
        if original.component_transform.is_some_and(|id|!live.contains(&id)) {continue;}
        let copy=copy_dynamic && !original.ver01;
        let Some(root)=node(original.root,copy) else {continue;};
        let mut db=original.clone();
        db.source_root_name=source.tfs.get(&db.root).map(|tf|tf.name.clone());
        db.component_name=db.component_transform.and_then(|id|source.tfs.get(&id))
            .map(|tf|tf.name.clone()).unwrap_or_else(||name.into());
        db.overlay_bindings=Some(Bindings {rig:name.into(),root,
            component:db.component_transform.and_then(private),
            exclusions:db.exclusions.iter().filter_map(|id|node(*id,copy)).collect(),
            not_rolls:db.not_rolls.iter().filter_map(|id|node(*id,copy)).collect(),
            nodes:db.ver01_nodes.iter().filter_map(|(id,rotation)|private(*id).map(|id|(id,*rotation))).collect(),
            colliders:if db.ver01 {db.collider_refs.clone()} else {Vec::new()},
            implanted_colliders:db.implanted_collider_refs.clone()});
        db.component_ancestry.clear(); db.implanted_collider_ancestry.clear();
        bones.push(db);
    }
    let mut transforms:Vec<_>=source.tfs.iter().filter(|(id,_)|live.contains(id)&&!links.contains_key(id))
        .map(|(id,tf)|(*id,tf.clone())).collect();
    transforms.sort_by_key(|(id,tf)|(tf.sibling_index.unwrap_or(u32::MAX),*id));
    let rig=Rig {name:name.into(),transforms,links,
        colliders:set.colliders.iter().filter(|(_,c)|live.contains(&c.transform)).map(|(id,c)|(*id,c.clone())).collect()};
    DBoneSet {bones,colliders:HashMap::new(),rigs:vec![rig]}
}

/// Allocate all prefab transforms before gathering any shared-root particles.
/// Body links alias the real body node, so earlier components affect later
/// components through the same Transform identity during LateUpdate.
pub(crate) fn instantiate(set:&mut DBoneSet,skel:&mut Skeleton)->HashMap<i64,Tf> {
    let body:HashMap<_,_>=skel.tfs.iter().map(|(id,tf)|(tf.name.clone(),*id)).collect();
    let mut mapped=HashMap::new(); let mut collider_ids=HashMap::new(); let mut helpers=HashMap::new();
    let mut next_sibling:HashMap<i64,u32>=HashMap::new();
    for tf in skel.tfs.values() {if let Some(parent)=tf.parent {
        let next=next_sibling.entry(parent).or_default();
        *next=(*next).max(tf.sibling_index.map_or(0,|index|index+1));
    }}
    for rig in &set.rigs {
        for (id,name) in &rig.links {if let Some(body_id)=body.get(name) {mapped.insert((rig.name.clone(),*id),*body_id);}}
        for (id,tf) in &rig.transforms {
            let target=(i64::MIN..).find(|id|!skel.tfs.contains_key(id)).unwrap();
            let mut tf=tf.clone(); tf.name=key(&rig.name,*id); tf.parent=None;
            skel.tfs.insert(target,tf); mapped.insert((rig.name.clone(),*id),target);
        }
    }
    for rig in &set.rigs {
        for (id,source) in &rig.transforms {
            let target=mapped[&(rig.name.clone(),*id)];
            let tf=skel.tfs.get_mut(&target).unwrap();
            tf.parent=source.parent.and_then(|parent|mapped.get(&(rig.name.clone(),parent)).copied());
            if source.parent.is_some_and(|parent|rig.links.contains_key(&parent)) {
                if let Some(parent)=tf.parent {
                    let next=next_sibling.entry(parent).or_default();
                    tf.sibling_index=Some(*next);*next+=1;
                }
            }
            helpers.insert(target,tf.clone());
        }
        for (id,source) in &rig.colliders {
            let Some(transform)=mapped.get(&(rig.name.clone(),source.transform)).copied() else {continue;};
            let target=(i64::MIN..).find(|id|!set.colliders.contains_key(id)).unwrap();
            let mut collider=source.clone(); collider.transform=transform;
            set.colliders.insert(target,collider); collider_ids.insert((rig.name.clone(),*id),target);
        }
    }
    let resolve=|node:&Node,rig:&str|match node {
        Node::Body(name)=>body.get(name).copied(),
        Node::Private(id)=>mapped.get(&(rig.to_owned(),*id)).copied(),
    };
    set.bones.retain_mut(|db| {
        let Some(binding)=&db.overlay_bindings else {return true;};
        let Some(root)=resolve(&binding.root,&binding.rig) else {return false;};
        db.root=root; db.component_transform=binding.component.as_ref().and_then(|node|resolve(node,&binding.rig));
        db.exclusions=binding.exclusions.iter().filter_map(|node|resolve(node,&binding.rig)).collect();
        db.not_rolls=binding.not_rolls.iter().filter_map(|node|resolve(node,&binding.rig)).collect();
        db.ver01_nodes=binding.nodes.iter().filter_map(|(node,rotation)|resolve(node,&binding.rig).map(|id|(id,*rotation))).collect();
        db.collider_refs=binding.colliders.iter().filter_map(|id|collider_ids.get(&(binding.rig.clone(),*id)).copied()).collect();
        db.implanted_collider_refs=binding.implanted_colliders.iter().filter_map(|id|collider_ids.get(&(binding.rig.clone(),*id)).copied()).collect();
        true
    });
    skel.rebuild_worlds(); helpers
}
