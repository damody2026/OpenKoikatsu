//! Studio YureCtrl.LoadExcel/Proc. Asset and state identities remain scoped.
use super::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub(crate) struct Policy {
    pub active: [bool;4],
    pub shapes: [[bool;9];2],
}
impl Default for Policy {
    fn default()->Self {Self{active:[true;4],shapes:[[true;9];2]}}
}

fn crc32(name:&str)->u32 {
    let mut crc=!0u32;
    for byte in name.bytes() {crc^=byte as u32;for _ in 0..8 {crc=(crc>>1)^if crc&1!=0 {0xedb88320}else{0};}}
    !crc
}
fn flag(value:&UnityValue)->Option<bool> {
    match value {UnityValue::Bool(v)=>Some(*v),_=>value.as_i64().filter(|v|*v==0||*v==1).map(|v|v!=0)}
}
fn rows(value:&UnityValue)->Option<HashMap<String,Policy>> {
    let UnityValue::Array(rows)=value else {return None;};
    let mut result=HashMap::new();
    for row in rows {
        let UnityValue::Object(f)=row else {return None;};
        let name=f.get("name")?.as_str()?.to_owned();let mut p=Policy::default();
        for (i,key) in ["Mune_L","Mune_R","Siri_L","Siri_R"].iter().enumerate() {p.active[i]=flag(f.get(*key)?)?;}
        for (side,keys) in [["UDPos_L","LRRot_L","LRPos_L","UDRot_L","Sharpness_L","Shape_L","Bulge_NipL","Thickness_NipL","Sharpness_NipL"],
            ["UDPos_R","LRRot_R","LRPos_R","UDRot_R","Sharpness_R","Shape_R","Bulge_NipR","Thickness_NipR","Sharpness_NipR"]].iter().enumerate() {
            for (i,key) in keys.iter().enumerate() {p.shapes[side][i]=flag(f.get(*key)?)?;}
        }
        result.insert(name,p); // Source LoadParam overwrites duplicate rows in serialized order.
    }
    Some(result)
}

fn resource_stamp(resource:&ClipBundleSource)->String {
    match resource {
        ClipBundleSource::File(path)=>crate::sqlcache::file_key(path),
        ClipBundleSource::Zip(path,inner)=>crate::sqlcache::zip_key(path,inner),
        ClipBundleSource::Studio{base,overlay,..}=>format!("{}|{}|{:?}",resource.key(),resource_stamp(base),overlay.as_ref().map(|(s,_,_)|resource_stamp(s))),
        ClipBundleSource::Layers{controller,..}=>format!("{}|{}",resource.key(),resource_stamp(controller)),
    }
}
pub(crate) fn load(game:&Path,key:&str,source:&str,owner:&str)->Result<Policy,&'static str> {
    static CACHE:OnceLock<Mutex<HashMap<String,Result<Policy,&'static str>>>>=OnceLock::new();
    let controller=key.split_once('|').and_then(|(s,_)|s.strip_prefix(controller::SOURCE_PREFIX)).and_then(|s|source_path(game,s));
    let scoped=source_path(game,source);
    let fallback=source.split_once('!').and_then(|(_,inner)|source_path(game,inner));
    let identity=serde_json::to_string(&(game.to_string_lossy(),key,source,owner,controller.as_ref().map(resource_stamp),scoped.as_ref().map(resource_stamp),fallback.as_ref().map(resource_stamp))).unwrap();
    let cache=CACHE.get_or_init(Default::default);
    if let Ok(entries)=cache.lock(){if let Some(policy)=entries.get(&identity){return *policy;}}
    // Decoded on disk too (an OK base library root has no bundles to read it from).
    let disk=serde_json::to_string(&(key,source,owner)).unwrap();
    if let Some(policy)=super::disk_cache::load::<Policy>(game,"yure",&disk) {
        if let Ok(mut entries)=cache.lock(){if entries.len()>=512{entries.clear();}entries.insert(identity,Ok(policy));}
        return Ok(policy);
    }
    let mut cacheable=true;
    let policy=load_uncached(game,key,source,owner,&mut cacheable);
    if cacheable { if let Ok(p)=&policy { super::disk_cache::store(game,"yure",&disk,p); } }
    if cacheable&&policy.is_ok(){if let Ok(mut entries)=cache.lock(){if entries.len()>=512{entries.clear();}entries.insert(identity,policy);}}
    policy
}
fn load_uncached(game:&Path,key:&str,source:&str,owner:&str,cacheable:&mut bool)->Result<Policy,&'static str> {

    let (controller,label)=key.split_once('|').ok_or("missing Studio state")?;
    let controller=source_path(game,controller.strip_prefix(controller::SOURCE_PREFIX).ok_or("not a state")?).ok_or("missing graph")?;
    let entries=controller::entries(&controller);
    let entry=entries.iter().find(|e|e.label==label&&e.studio_layer.is_some()).ok_or("not a Studio state")?;
    // FileInfo.Check requires both strings. Missing file/asset/state resets defaults.
    if source.is_empty()||owner.is_empty() {return Ok(Policy::default());}
    if source.starts_with("studio@") {return Err("nested Yure source");}
    let valid=if let Some((package,inner))=source.split_once('!') {
        package.starts_with("mods/")&&safe_relative(package,"zipmod")&&safe_relative(inner,"unity3d")
    } else {safe_relative(source.strip_prefix("abdata/").unwrap_or(source),"unity3d")};
    if !valid {return Err("invalid Yure source");}
    let scoped=source_path(game,source).and_then(|s|s.load());
    let fallback=source.split_once('!').and_then(|(_,inner)|source_path(game,inner));
    let Some(bundle)=scoped.or_else(||fallback.and_then(|s|s.load())) else {*cacheable=false;return Ok(Policy::default());};
    let mut matches=Vec::new();
    for file in &bundle.files {
        let Ok(bytes)=bundle.extract_file_data(file) else {continue;};
        let Ok(serialized)=parse_serialized_file(bytes) else {continue;};
        for info in &serialized.objects {
            if info.type_id!=114 {continue;}
            let Ok(object)=UnityObject::from_serialized_file(&serialized,info) else {continue;};
            if object.name().as_deref()!=Some(owner) {continue;}
            matches.push(rows(object.class.properties().get("param").ok_or("missing Yure rows")?).ok_or("invalid Yure rows")?);
        }
    }
    if matches.is_empty() {return Ok(Policy::default());}
    if matches.len()!=1 {return Err("ambiguous Yure asset");}
    let choices:Vec<_>=matches[0].iter().filter(|(name,_)|crc32(name)==entry.short_name_id).collect();
    match choices.as_slice() {[]=>Ok(Policy::default()),[(_,policy)]=>Ok(**policy),_=>Err("ambiguous Yure state hash")}
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn source_rows_are_ordered_and_sides_are_independent() {
        let row=|left:bool| {
            let mut f=match UnityValue::Object(Default::default()) {UnityValue::Object(f)=>f,_=>unreachable!()};
            f.insert("name".into(),UnityValue::String("state".into()));
            for key in ["Mune_L","Mune_R","Siri_L","Siri_R","UDPos_L","LRRot_L","LRPos_L","UDRot_L","Sharpness_L","Shape_L","Bulge_NipL","Thickness_NipL","Sharpness_NipL",
                "UDPos_R","LRRot_R","LRPos_R","UDRot_R","Sharpness_R","Shape_R","Bulge_NipR","Thickness_NipR","Sharpness_NipR"] {
                f.insert(key.into(),UnityValue::Bool(if key=="Mune_L"||key=="UDPos_L" {left}else{true}));
            }
            UnityValue::Object(f)
        };
        let data=rows(&UnityValue::Array(vec![row(true),row(false)])).unwrap();
        assert_eq!(data.len(),1);let p=data["state"];
        assert_eq!(p.active,[false,true,true,true]);assert!(!p.shapes[0][0]);assert!(p.shapes[1].iter().all(|v|*v));
        assert!(rows(&UnityValue::Array(vec![UnityValue::Object(Default::default())])).is_none());
    }
}
#[cfg(test)]
#[test]
fn yure_stamp_tracks_file_and_zip_edits() {
    let root=PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(format!("../../Saved/Tests/AnimationMenus391/Stamp{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
    std::fs::create_dir_all(&root).unwrap();let file=root.join("source.unity3d");std::fs::write(&file,b"before").unwrap();
    let raw=ClipBundleSource::File(file.clone());let zip=ClipBundleSource::Zip(file.clone(),"abdata/yure.unity3d".into());
    let before=[resource_stamp(&raw),resource_stamp(&zip)];std::fs::write(&file,b"changed and longer").unwrap();
    assert_ne!(before[0],resource_stamp(&raw));assert_ne!(before[1],resource_stamp(&zip));
}
