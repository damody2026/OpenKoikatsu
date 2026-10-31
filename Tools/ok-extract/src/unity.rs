#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::cell::RefCell;
use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;

use unity_asset_binary::asset::parse_serialized_file;
use unity_asset_binary::bundle::load_bundle;
use unity_asset_decode::mesh::parse_mesh;
use unity_asset_decode::object::UnityObject;
use unity_asset_decode::unity_version::UnityVersion;

pub(crate) mod references;

const MESH_CLASS_ID: i32 = 43;

#[cfg(test)]
#[test]
#[ignore = "manual shader decoder diagnostic"]
fn head_item_shader_decode_audit() {
    let bytes=crate::gamefs::read("../../Saved/Tests/MouthMaterial278/Alice_keiyuu_png_selected_head.unity3d").unwrap();
    let parsed=parse_bundle_cached(bytes);
    parsed.with_serialized(|sers| {
        for ser in sers {
            for info in ser.objects_of_type(48) {
                match ser.scene_object(info) {
                    Ok(object) => eprintln!("SHADER {} name={:?} keys={:?} warnings={:?}",info.path_id,object.name(),object.class.properties().keys(),object.typetree_warnings()),
                    Err(error)=>eprintln!("SHADER {} error={error:?}",info.path_id),
                }
            }
        }
    });
}

/// Load an assetbundle from memory without the crate's default 1GB
/// decompressed-size cap. Mod bundles regularly exceed it.
pub fn load_bundle_unbounded(
    bytes: Vec<u8>,
) -> unity_asset_binary::error::Result<unity_asset_binary::bundle::AssetBundle> {
    load_bundle_with_asset_policy(bytes, true)
}

fn load_bundle_with_asset_policy(bytes: Vec<u8>, load_assets: bool)
    -> unity_asset_binary::error::Result<unity_asset_binary::bundle::AssetBundle> {
    load_bundle_source(SourceBytes::from(bytes), load_assets)
}

/// Bundle bytes held once and shared by every holder (an accessory source memo, the parsed bundle): an owned buffer,
/// or a read-only map of a large file or of a stored zip entry, which is not private memory. Installed bundles reach
/// 1.7 GB; each owned copy of one used to stay alive per holder.
#[derive(Clone)]
pub(crate) struct SourceBytes { data: unity_asset_binary::shared_bytes::SharedBytes, start: usize, len: usize }
impl SourceBytes {
    /// `len` bytes at `start` of the file at `path`, mapped read-only.
    pub(crate) fn map(path: &Path, start: u64, len: u64) -> Option<Self> {
        let file = std::fs::File::open(path).ok()?;
        let end = start.checked_add(len)?;
        if file.metadata().ok()?.len() < end { return None; }
        // The installation's files are not written while a card is opened from them.
        let map = unsafe { memmap2::Mmap::map(&file) }.ok()?;
        Some(Self { data: unity_asset_binary::shared_bytes::SharedBytes::Mmap(Arc::new(map)), start: usize::try_from(start).ok()?, len: usize::try_from(len).ok()? }.registered())
    }
    /// Large sources are found again from a plain slice of them (`BundleInput for [u8]`), so a function handed
    /// `&[u8]` shares the source instead of copying it. Weak: the table keeps nothing alive.
    fn registered(self) -> Self {
        use unity_asset_binary::shared_bytes::SharedBytes;
        if (self.len as u64) < MAP_AT_LEAST { return self; }
        let weak = match &self.data { SharedBytes::OwnedVec(v) => WeakSource::Vec(Arc::downgrade(v)), SharedBytes::Mmap(m) => WeakSource::Map(Arc::downgrade(m)), _ => return self };
        let mut table = SOURCES.lock().unwrap_or_else(|e| e.into_inner());
        table.retain(|(_, _, _, w)| w.alive());
        table.push((self.as_ptr() as usize, self.len, self.start, weak));
        self
    }
    fn find(bytes: &[u8]) -> Option<Self> {
        use unity_asset_binary::shared_bytes::SharedBytes;
        if (bytes.len() as u64) < MAP_AT_LEAST { return None; }
        let table = SOURCES.lock().unwrap_or_else(|e| e.into_inner());
        table.iter().filter(|(ptr, len, _, _)| *ptr == bytes.as_ptr() as usize && *len == bytes.len()).find_map(|(_, len, start, w)| {
            let data = match w { WeakSource::Vec(v) => SharedBytes::OwnedVec(v.upgrade()?), WeakSource::Map(m) => SharedBytes::Mmap(m.upgrade()?) };
            Some(Self { data, start: *start, len: *len })
        })
    }
    /// `len` bytes streamed from `reader` (an archive entry that cannot be read in place) into memory. The reader
    /// must then be at its end (an archive entry checks its CRC there). None when that fails.
    pub(crate) fn spill(reader: &mut impl std::io::Read, len: u64) -> Option<Self> {
        Self::spill_with(len, |out| {
            let mut filled = 0;
            while filled < out.len() {
                match reader.read(&mut out[filled..]) { Ok(0) => return None, Ok(n) => filled += n, Err(_) => return None }
            }
            // At the end now: one more read must give nothing (and lets the entry verify its checksum).
            matches!(reader.read(&mut [0u8; 1]), Ok(0)).then_some(())
        })
    }
    /// `len` bytes written by `fill` into memory, freed with the last reference (an open's temporary data: it used to
    /// go to a mapped temporary file).
    pub(crate) fn spill_with(len: u64, fill: impl FnOnce(&mut [u8]) -> Option<()>) -> Option<Self> {
        let mut out = vec![0u8; usize::try_from(len).ok()?];
        fill(&mut out)?;
        Some(out.into())
    }
    /// A whole file: mapped when large, read otherwise.
    pub(crate) fn file(path: &Path) -> std::io::Result<Self> {
        let len = std::fs::metadata(path)?.len();
        if len >= MAP_AT_LEAST { if let Some(mapped) = Self::map(path, 0, len) { return Ok(mapped); } }
        std::fs::read(path).map(Self::from)
    }
    fn range(&self) -> std::ops::Range<usize> { self.start..self.start + self.len }
    /// `len` bytes at `start` of these bytes, sharing them.
    pub(crate) fn sub(&self, start: usize, len: usize) -> Self {
        assert!(start.checked_add(len).is_some_and(|end| end <= self.len), "slice out of range");
        Self { data: self.data.clone(), start: self.start + start, len }
    }
    /// Whether the bytes are a file map (not private memory).
    pub(crate) fn is_mapped(&self) -> bool { matches!(self.data, unity_asset_binary::shared_bytes::SharedBytes::Mmap(_)) }
}
enum WeakSource { Vec(std::sync::Weak<Vec<u8>>), Map(std::sync::Weak<memmap2::Mmap>) }
impl WeakSource { fn alive(&self) -> bool { match self { Self::Vec(v) => v.strong_count() > 0, Self::Map(m) => m.strong_count() > 0 } } }
/// Live large sources: (address, length, start in the backing bytes, backing).
static SOURCES: std::sync::Mutex<Vec<(usize, usize, usize, WeakSource)>> = std::sync::Mutex::new(Vec::new());
/// Sources at least this large are mapped instead of read.
pub(crate) const MAP_AT_LEAST: u64 = 8 << 20;
impl From<Vec<u8>> for SourceBytes {
    fn from(bytes: Vec<u8>) -> Self { let len = bytes.len(); Self { data: unity_asset_binary::shared_bytes::SharedBytes::from_vec(bytes), start: 0, len }.registered() }
}
impl Default for SourceBytes { fn default() -> Self { Vec::new().into() } }
impl AsRef<[u8]> for SourceBytes { fn as_ref(&self) -> &[u8] { self } }
impl std::ops::Deref for SourceBytes {
    type Target = [u8];
    fn deref(&self) -> &[u8] { &self.data.as_bytes()[self.start..self.start + self.len] }
}
/// What a bundle can be parsed from: a slice or vector (copied when parsed) or shared bytes (not copied).
pub(crate) trait BundleInput { fn bundle_bytes(&self) -> &[u8]; fn to_source(&self) -> SourceBytes; }
impl BundleInput for [u8] { fn bundle_bytes(&self) -> &[u8] { self } fn to_source(&self) -> SourceBytes { SourceBytes::find(self).unwrap_or_else(|| self.to_vec().into()) } }
impl BundleInput for Vec<u8> { fn bundle_bytes(&self) -> &[u8] { self } fn to_source(&self) -> SourceBytes { self.as_slice().to_source() } }
impl BundleInput for SourceBytes { fn bundle_bytes(&self) -> &[u8] { self } fn to_source(&self) -> SourceBytes { self.clone() } }

fn load_bundle_source(bytes: SourceBytes, load_assets: bool)
    -> unity_asset_binary::error::Result<unity_asset_binary::bundle::AssetBundle> {
    let mut opts = unity_asset_binary::bundle::BundleLoadOptions::default();
    // ParsedBundle owns the serialized-file cache. Eagerly loading assets here
    // would parse every serialized file a second time and discard that work.
    opts.load_assets = load_assets;
    opts.decompress_blocks = load_assets;
    opts.max_memory = None;
    // Recently read blocks only: parsed files read object payloads from the blocks on demand. All bundles together
    // keep at most 256 MB of blocks (some bundles are single LZ4 blocks of ~100 MB).
    opts.max_unityfs_block_cache_memory = Some(32 * 1024 * 1024);
    unity_asset_binary::bundle::set_global_block_cache_limit(256 << 20);
    opts.max_compressed_blocks_info_size = Some(512 * 1024 * 1024);
    opts.max_blocks_info_size = Some(512 * 1024 * 1024);
    opts.max_legacy_directory_compressed_size = Some(512 * 1024 * 1024);
    opts.max_compressed_block_size = Some(16 * 1024 * 1024 * 1024);
    opts.max_blocks = 20_000_000;
    opts.max_nodes = 20_000_000;
    let range = bytes.range();
    unity_asset_binary::bundle::BundleParser::from_shared_range_with_options(bytes.data, range, opts)
}

pub fn load_bundle_path_unbounded(
    path: &Path,
) -> unity_asset_binary::error::Result<unity_asset_binary::bundle::AssetBundle> {
    let data = crate::gamefs::read(path).map_err(|e| {
        unity_asset_binary::error::BinaryError::generic(format!("Failed to read bundle: {e}"))
    })?;
    load_bundle_unbounded(data)
}

/// Unity stores one LZ4 block per graphics API in a Shader's compressedBlob.
/// The offsets/lengths are parallel arrays; decoding the entire blob as one
/// block silently falls back to compressed bytes and can find false DXBC tags.
fn decompress_shader_platforms(
    comp: &[u8], offsets: &[u32], lengths: &[u32], decompressed: &[u32],
) -> Vec<Vec<u8>> {
    let mut out = Vec::new();
    for ((&offset, &length), &size) in offsets.iter().zip(lengths).zip(decompressed) {
        let (start, len, size) = (offset as usize, length as usize, size as usize);
        if len == 0 || size == 0 || size > 128 * 1024 * 1024 { continue; }
        let Some(block) = comp.get(start..start.saturating_add(len)) else { continue; };
        if let Ok(raw) = lz4_flex::block::decompress(block, size) { out.push(raw); }
    }
    out
}

fn shader_blob_lengths(v: Option<&unity_asset_core::UnityValue>) -> Vec<u32> {
    v.map(uv_arr).unwrap_or(&[]).iter()
        .filter_map(|entry| entry.as_i64().and_then(|n| u32::try_from(n).ok()))
        .collect()
}

/// A Material Editor shader bundle can register a Shader object yet contain
/// no Direct3D program for the running Windows game. `LoadShader` then yields
/// null and `MaterialAPI.SetShader` keeps the material's previous shader.
pub fn shader_bundle_has_dxbc(bytes: &[u8], shader_name: &str) -> bool {
    shader_bundle_has_dxbc_impl(bytes,shader_name,true)
}

// Preserve the established checks for shader families without a native UE
// adapter; enabling newer registered programs also requires porting their math.
pub(crate) fn shader_bundle_has_legacy_dxbc(bytes: &[u8], shader_name: &str) -> bool {
    shader_bundle_has_dxbc_impl(bytes,shader_name,false)
}

fn shader_nested_blob_lengths(v: Option<&unity_asset_core::UnityValue>) -> Vec<u32> {
    fn append(v:&unity_asset_core::UnityValue,out:&mut Vec<u32>) {
        if let unity_asset_core::UnityValue::Array(values)=v {for value in values {append(value,out);}}
        else if let Some(n)=v.as_i64().and_then(|n|u32::try_from(n).ok()) {out.push(n);}
    }
    let mut out=Vec::new();if let Some(v)=v {append(v,&mut out);}out
}

fn shader_bundle_has_dxbc_impl(bytes: &[u8], shader_name: &str,nested:bool) -> bool {
    crate::oklib::memo_bytes(bytes,"shader_bundle_has_dxbc",&(shader_name,nested),||shader_bundle_has_dxbc_unity(bytes,shader_name,nested))
}
fn shader_bundle_has_dxbc_unity(bytes: &[u8], shader_name: &str,nested:bool) -> bool {
    let Ok(bundle) = load_bundle_unbounded(bytes.to_vec()) else { return false; };
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue; };
        let Ok(serialized) = parse_serialized_file(fb) else { continue; };
        for info in serialized.objects_of_type(48) {
            let Ok(obj) = serialized.scene_object(info) else { continue; };
            let props = obj.class.properties();
            let name = props.get("m_ParsedForm").and_then(|v| match v {
                unity_asset_core::UnityValue::Object(o) => o.get("m_Name").and_then(|v| v.as_str()),
                _ => None,
            }).or_else(|| props.get("m_Name").and_then(|v| v.as_str()));
            if name != Some(shader_name) { continue; }
            let Some(unity_asset_core::UnityValue::Bytes(comp)) = props.get("compressedBlob") else { continue; };
            // Unity 2019 serializes these as platform -> block arrays. Older
            // bundles use flat arrays. Decode blocks individually in either form.
            let lengths=if nested {shader_nested_blob_lengths}else{shader_blob_lengths};
            let blocks = decompress_shader_platforms(comp,
                &lengths(props.get("offsets")),
                &lengths(props.get("compressedLengths")),
                &lengths(props.get("decompressedLengths")));
            for raw in blocks {
                for (i, _) in raw.windows(4).enumerate().filter(|(_, w)| *w == b"DXBC") {
                    if i + 28 > raw.len() { continue; }
                    let size = u32::from_le_bytes(raw[i + 24..i + 28].try_into().unwrap()) as usize;
                    if (32..=8 * 1024 * 1024).contains(&size) && i + size <= raw.len() { return true; }
                }
            }
        }
    }
    false
}

// The native clothes CB packer is allowed only for this verified ABI. A mod
// shader with the same display name but different bindings must not be drawn
// using the old material layout merely because CreatePixelShader succeeds.
fn clothes_create_shader_layout(object:&UnityObject)->Option<()> {
    let parsed=object.class.properties().get("m_ParsedForm")?;
    let sub=uv_arr(uv_obj(parsed,"m_SubShaders")?).first()?;
    let pass=uv_arr(uv_obj(sub,"m_Passes")?).first()?;
    let mut names=HashMap::new();
    for pair in uv_arr(uv_obj(pass,"m_NameIndices")?) {
        let a=uv_arr(pair);
        let (name,index)=if a.len()==2 {(a[0].as_str()?,a[1].as_i64()?)}else{(uv_obj(pair,"first")?.as_str()?,uv_obj(pair,"second")?.as_i64()?)};
        names.insert(index,name);
    }
    let programs=uv_arr(uv_obj(uv_obj(pass,"progFragment")?,"m_SubPrograms")?);
    if programs.len()!=1{return None;}let program=&programs[0];
    for key in ["m_GlobalKeywordIndices","m_LocalKeywordIndices","m_KeywordIndices","m_Samplers"] {
        if uv_obj(program,key).is_some_and(|v|!uv_arr(v).is_empty()){return None;}
    }
    let cbs=uv_arr(uv_obj(program,"m_ConstantBuffers")?);if cbs.len()!=1||uv_obj(&cbs[0],"m_Size")?.as_i64()?!=256{return None;}
    let bindings=uv_arr(uv_obj(program,"m_ConstantBufferBindings")?);if bindings.len()!=1||uv_obj(&bindings[0],"m_Index")?.as_i64()?!=0{return None;}
    let mut offsets=HashMap::new();
    for p in uv_arr(uv_obj(&cbs[0],"m_VectorParams")?) {
        if uv_obj(p,"m_Type")?.as_i64()?!=0{return None;}
        let name=*names.get(&uv_obj(p,"m_NameIndex")?.as_i64()?)?;
        offsets.insert(name,(uv_obj(p,"m_Index")?.as_i64()?,uv_obj(p,"m_Dim")?.as_i64()?));
    }
    if offsets.get("_MainTex_ST")!=Some(&(32,4))||offsets.get("_ColorMask_ST")!=Some(&(112,4)){return None;}
    for (c,start) in [(1,48),(2,128),(3,192)] {
        for (suffix,delta) in [("Scale",0),("Offset",8)] {for (axis,delta2) in [("u",0),("v",4)] {
            if offsets.get(format!("_Pattern{suffix}{c}{axis}").as_str())!=Some(&(start+delta+delta2,1)){return None;}
        }}
        if offsets.get(format!("_PatternRotator{c}").as_str())!=Some(&(start+16,1))||offsets.get(format!("_Color{c}_2").as_str())!=Some(&(start+32,4)){return None;}
        let base=if c==1{"_Color".into()}else{format!("_Color{c}")};if offsets.get(base.as_str())!=Some(&(start+48,4)){return None;}
    }
    let textures=uv_arr(uv_obj(program,"m_TextureParams")?);if textures.len()!=5{return None;}
    for (name,index,sampler) in [("_PatternMask1",0,1),("_ColorMask",1,2),("_PatternMask2",2,3),("_PatternMask3",3,4),("_MainTex",4,0)] {
        let p=textures.iter().find(|p|uv_obj(p,"m_NameIndex").and_then(|v|v.as_i64()).and_then(|i|names.get(&i).copied())==Some(name))?;
        if uv_obj(p,"m_Index")?.as_i64()?!=index||uv_obj(p,"m_SamplerIndex")?.as_i64()?!=sampler||uv_obj(p,"m_Dim")?.as_i64()?!=2{return None;}
    }
    Some(())
}

/// Studio map stage-effect shaders OK draws with its own material (KKPreviewActor, `_OKStageEffect`): 1 volume light
/// cone, 2 spot beam, 3 laser, 4 spot glow, 5 LED panel. All tint by the hue of a scrolling `color` texture.
pub(crate) fn stage_effect_kind(shader:&str)->Option<i32> {
    match shader {
        "Shader Forge/volumeLight"=>Some(1), "Shader Forge/spotLight_beam"=>Some(2), "Shader Forge/spotLaser"=>Some(3),
        "Shader Forge/spotLight_glow"=>Some(4), "Shader Forge/electricLight"=>Some(5), _=>None,
    }
}

/// Shader.name is ParsedForm.m_Name. The Unity Object m_Name is the asset
/// label (e.g. toon_eye_lod0) and can omit the shader's namespace.
pub(crate) fn shader_runtime_name(object:&UnityObject)->String {
    object.class.properties().get("m_ParsedForm").and_then(|v|match v {
        unity_asset_core::UnityValue::Object(parsed)=>parsed.get("m_Name").and_then(|n|n.as_str()),
        _=>None,
    }).filter(|name|!name.is_empty()).map(str::to_string)
        .unwrap_or_else(||object.name().unwrap_or_default())
}

// --- FullBodyBipedIK prefab config parsing ---------------------------------

fn uv_obj<'a>(v: &'a unity_asset_core::UnityValue, k: &str) -> Option<&'a unity_asset_core::UnityValue> {
    match v {
        unity_asset_core::UnityValue::Object(o) => o.get(k),
        _ => None,
    }
}
fn uv_f32(v: &unity_asset_core::UnityValue) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}
fn uv_i32(v: &unity_asset_core::UnityValue) -> i32 {
    v.as_i64().unwrap_or(0) as i32
}
fn uv_bool(v: &unity_asset_core::UnityValue) -> bool {
    match v {
        unity_asset_core::UnityValue::Bool(b) => *b,
        _ => v.as_i64().unwrap_or(0) != 0,
    }
}
fn uv_arr<'a>(v: &'a unity_asset_core::UnityValue) -> &'a [unity_asset_core::UnityValue] {
    match v {
        unity_asset_core::UnityValue::Array(a) => a,
        _ => &[],
    }
}
fn uv_pptr(v: &unity_asset_core::UnityValue) -> Option<i64> {
    match v {
        unity_asset_core::UnityValue::Object(o) => o
            .get("m_PathID")
            .or_else(|| o.get("m_PathId"))
            .or_else(|| o.get("pathID"))
            .and_then(|x| x.as_i64()),
        unity_asset_core::UnityValue::Integer(i) => Some(*i),
        _ => None,
    }
}

fn prefab_pathid_names(
    ser: &unity_asset_binary::asset::SerializedFile,
) -> HashMap<i64, String> {
    let mut go_name: HashMap<i64, String> = HashMap::new();
    for info in &ser.objects {
        if info.type_id == 1 {
            if let Ok(obj) = ser.scene_object(info) {
                go_name.insert(info.path_id, obj.name().unwrap_or_default());
            }
        }
    }
    let mut names = HashMap::new();
    for info in &ser.objects {
        if info.type_id == 4 {
            if let Ok(obj) = ser.scene_object(info) {
                let props = obj.class.properties();
                if let Some(g) = props.get("m_GameObject").and_then(uv_pptr) {
                    if let Some(n) = go_name.get(&g) {
                        names.insert(info.path_id, n.clone());
                    }
                }
            }
        }
    }
    names
}

fn parse_prefab_ik_ser(
    ser: &unity_asset_binary::asset::SerializedFile,
) -> Option<crate::finalik::PrefabIkConfig> {
    use unity_asset_core::UnityValue;
    let names = prefab_pathid_names(ser);
    let nm = |v: &UnityValue| -> Option<String> { uv_pptr(v).and_then(|id| names.get(&id).cloned()) };
    let mut scripts: HashMap<i64, String> = HashMap::new();
    for info in ser.objects_of_type(115) {
        if let Ok(obj) = ser.scene_object(&info) {
            let class = obj
                .class
                .properties()
                .get("m_ClassName")
                .or_else(|| obj.class.properties().get("m_Name"))
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            scripts.insert(info.path_id, class);
        }
    }
    for info in ser.objects_of_type(114) {
        let Ok(obj) = ser.scene_object(&info) else {
            continue;
        };
        let props = obj.class.properties();
        let class = props
            .get("m_Script")
            .and_then(uv_pptr)
            .and_then(|id| scripts.get(&id))
            .cloned()
            .unwrap_or_default();
        if class != "FullBodyBipedIK" {
            continue;
        }
        let Some(refs) = props.get("references") else {
            continue;
        };
        let Some(solver) = props.get("solver") else {
            continue;
        };
        let mut cfg = crate::finalik::PrefabIkConfig::default();
        let r = |k: &str| nm(uv_obj(refs, k).unwrap_or(&UnityValue::Null));
        cfg.references = crate::finalik::BipedReferences {
            root: r("root"),
            pelvis: r("pelvis"),
            left_thigh: r("leftThigh"),
            left_calf: r("leftCalf"),
            left_foot: r("leftFoot"),
            right_thigh: r("rightThigh"),
            right_calf: r("rightCalf"),
            right_foot: r("rightFoot"),
            left_upper_arm: r("leftUpperArm"),
            left_forearm: r("leftForearm"),
            left_hand: r("leftHand"),
            right_upper_arm: r("rightUpperArm"),
            right_forearm: r("rightForearm"),
            right_hand: r("rightHand"),
            head: r("head"),
            spine: uv_arr(uv_obj(refs, "spine").unwrap_or(&UnityValue::Null))
                .iter()
                .filter_map(nm)
                .collect(),
            eyes: Vec::new(),
        };
        cfg.root_node = nm(uv_obj(solver, "root").unwrap_or(&UnityValue::Null));
        cfg.iterations = uv_i32(uv_obj(solver, "iterations").unwrap_or(&UnityValue::Null));
        cfg.spine_stiffness = uv_f32(uv_obj(solver, "spineStiffness").unwrap_or(&UnityValue::Null));
        cfg.pull_body_vertical =
            uv_f32(uv_obj(solver, "pullBodyVertical").unwrap_or(&UnityValue::Null));
        cfg.pull_body_horizontal =
            uv_f32(uv_obj(solver, "pullBodyHorizontal").unwrap_or(&UnityValue::Null));
        for ch in uv_arr(uv_obj(solver, "chain").unwrap_or(&UnityValue::Null)) {
            let mut pc = crate::finalik::PrefabChain::default();
            pc.pin = uv_f32(uv_obj(ch, "pin").unwrap_or(&UnityValue::Null));
            pc.pull = uv_f32(uv_obj(ch, "pull").unwrap_or(&UnityValue::Null));
            pc.push = uv_f32(uv_obj(ch, "push").unwrap_or(&UnityValue::Null));
            pc.push_parent = uv_f32(uv_obj(ch, "pushParent").unwrap_or(&UnityValue::Null));
            pc.reach = uv_f32(uv_obj(ch, "reach").unwrap_or(&UnityValue::Null));
            pc.nodes = uv_arr(uv_obj(ch, "nodes").unwrap_or(&UnityValue::Null))
                .iter()
                .filter_map(|n| nm(uv_obj(n, "transform").unwrap_or(&UnityValue::Null)))
                .collect();
            pc.children = uv_arr(uv_obj(ch, "children").unwrap_or(&UnityValue::Null))
                .iter()
                .map(|x| uv_i32(x).max(0) as usize)
                .collect();
            for cc in uv_arr(uv_obj(ch, "childConstraints").unwrap_or(&UnityValue::Null)) {
                let b1 = nm(uv_obj(cc, "bone1").unwrap_or(&UnityValue::Null)).unwrap_or_default();
                let b2 = nm(uv_obj(cc, "bone2").unwrap_or(&UnityValue::Null)).unwrap_or_default();
                if b1.is_empty() || b2.is_empty() {
                    continue;
                }
                pc.child_constraints.push((
                    uv_f32(uv_obj(cc, "pushElasticity").unwrap_or(&UnityValue::Null)),
                    uv_f32(uv_obj(cc, "pullElasticity").unwrap_or(&UnityValue::Null)),
                    b1,
                    b2,
                ));
            }
            cfg.chains.push(pc);
        }
        for ef in uv_arr(uv_obj(solver, "effectors").unwrap_or(&UnityValue::Null)) {
            let mut pe = crate::finalik::PrefabEffector::default();
            pe.bone = nm(uv_obj(ef, "bone").unwrap_or(&UnityValue::Null));
            pe.target = nm(uv_obj(ef, "target").unwrap_or(&UnityValue::Null));
            pe.position_weight = uv_f32(uv_obj(ef, "positionWeight").unwrap_or(&UnityValue::Null));
            pe.rotation_weight = uv_f32(uv_obj(ef, "rotationWeight").unwrap_or(&UnityValue::Null));
            pe.effect_child_nodes =
                uv_bool(uv_obj(ef, "effectChildNodes").unwrap_or(&UnityValue::Null));
            pe.maintain_relative_position_weight =
                uv_f32(uv_obj(ef, "maintainRelativePositionWeight").unwrap_or(&UnityValue::Null));
            pe.child_bones = uv_arr(uv_obj(ef, "childBones").unwrap_or(&UnityValue::Null))
                .iter()
                .filter_map(nm)
                .collect();
            pe.plane_bone1 = nm(uv_obj(ef, "planeBone1").unwrap_or(&UnityValue::Null));
            pe.plane_bone2 = nm(uv_obj(ef, "planeBone2").unwrap_or(&UnityValue::Null));
            pe.plane_bone3 = nm(uv_obj(ef, "planeBone3").unwrap_or(&UnityValue::Null));
            cfg.effectors.push(pe);
        }
        if let Some(sm) = uv_obj(solver, "spineMapping") {
            cfg.spine_bones = uv_arr(uv_obj(sm, "spineBones").unwrap_or(&UnityValue::Null))
                .iter()
                .filter_map(nm)
                .collect();
            cfg.spine_left_upper_arm = nm(uv_obj(sm, "leftUpperArmBone").unwrap_or(&UnityValue::Null));
            cfg.spine_right_upper_arm =
                nm(uv_obj(sm, "rightUpperArmBone").unwrap_or(&UnityValue::Null));
            cfg.spine_left_thigh = nm(uv_obj(sm, "leftThighBone").unwrap_or(&UnityValue::Null));
            cfg.spine_right_thigh = nm(uv_obj(sm, "rightThighBone").unwrap_or(&UnityValue::Null));
        }
        for bm in uv_arr(uv_obj(solver, "boneMappings").unwrap_or(&UnityValue::Null)) {
            cfg.bone_mappings.push((
                nm(uv_obj(bm, "bone").unwrap_or(&UnityValue::Null)),
                uv_f32(uv_obj(bm, "maintainRotationWeight").unwrap_or(&UnityValue::Null)),
            ));
        }
        for lm in uv_arr(uv_obj(solver, "limbMappings").unwrap_or(&UnityValue::Null)) {
            let mut pl = crate::finalik::PrefabLimbMapping::default();
            pl.parent_bone = nm(uv_obj(lm, "parentBone").unwrap_or(&UnityValue::Null));
            pl.bone1 = nm(uv_obj(lm, "bone1").unwrap_or(&UnityValue::Null));
            pl.bone2 = nm(uv_obj(lm, "bone2").unwrap_or(&UnityValue::Null));
            pl.bone3 = nm(uv_obj(lm, "bone3").unwrap_or(&UnityValue::Null));
            pl.maintain_rotation_weight =
                uv_f32(uv_obj(lm, "maintainRotationWeight").unwrap_or(&UnityValue::Null));
            pl.weight = uv_f32(uv_obj(lm, "weight").unwrap_or(&UnityValue::Null));
            cfg.limb_mappings.push(pl);
        }
        let owner=props.get("m_GameObject").and_then(uv_pptr);
        for other in ser.objects_of_type(114) {
            let Ok(frame)=ser.scene_object(&other) else {continue;};
            let values=frame.class.properties();
            if values.get("m_GameObject").and_then(uv_pptr)!=owner || values.get("m_Script").and_then(uv_pptr).and_then(|id|scripts.get(&id)).map(String::as_str)!=Some("FrameCorrect") {continue;}
            if let Some(list)=values.get("_list").and_then(|v|v.as_array()) {
                cfg.frame_bones=list.iter().map(|entry| {
                    uv_obj(entry,"component").and_then(|v|nm(v))
                }).collect();
                cfg.frame_orders=list.iter().map(|entry|uv_i32(uv_obj(entry,"type").unwrap_or(&UnityValue::Null))).collect();
            }
        }
        return Some(cfg);
    }
    None
}

/// Load `oo_base.unity3d` and parse the body `FullBodyBipedIK` config.
pub fn parse_prefab_ik(game_root: &Path) -> Option<crate::finalik::PrefabIkConfig> {
    let path = game_root.join("abdata/chara/oo_base.unity3d");
    let bundle = load_bundle_path_unbounded(&path).ok()?;
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else {
            continue;
        };
        let Ok(ser) = parse_serialized_file(fb) else {
            continue;
        };
        if let Some(cfg) = parse_prefab_ik_ser(&ser) {
            return Some(cfg);
        }
    }
    None
}

/// The Unity material attached to a renderer, captured whole so downstream
/// code can reproduce the game's tint/blend/cull decisions instead of guessing
/// from mesh names. `tex_slots` keeps every authored `m_TexEnvs` property name
/// (e.g. `_ColorMask`, `_DetailMask`, `_NormalMap`) paired with its texture
/// object name; `colors` keeps `m_Colors` (`_Color`, `_Color2`, ...).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct MeshMaterial {
    pub name: String,
    pub shader: String,
    /// The referenced Unity Shader's declaration for `_rimV`, if available.
    /// None means the shader asset was external or could not be decoded.
    #[serde(default)]
    pub shader_has_rim: Option<bool>,
    pub render_queue: i32,
    pub keywords: Vec<String>,
    #[serde(serialize_with="crate::ordered::map")]
    pub colors: HashMap<String, [f32; 4]>,
    #[serde(serialize_with="crate::ordered::map")]
    pub floats: HashMap<String, f32>,
    pub tex_slots: Vec<(String, String)>,
    /// Original texture wrapMode, resolved by the material's texture PPtr.
    /// MaterialAPI.SetTexture copies this to a MaterialEditor replacement.
    #[serde(default)]
    #[serde(serialize_with="crate::ordered::map")]
    pub tex_wraps: HashMap<String, i32>,
}

impl MeshMaterial {
    pub fn slot(&self, name: &str) -> Option<&str> {
        self.tex_slots
            .iter()
            .find(|(k, _)| k == name)
            .or_else(|| self.tex_slots.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)))
            .map(|(_, v)| v.as_str())
    }
    pub fn color(&self, name: &str) -> Option<[f32; 4]> {
        // Unity shader properties are case-sensitive. Some headmods save
        // distinct _LineColor and _linecolor entries in the same material.
        self.colors.get(name).copied().or_else(|| {
            self.colors
                .iter()
                .filter(|(k, _)| k.eq_ignore_ascii_case(name))
                .min_by(|a, b| a.0.cmp(b.0))
                .map(|(_, v)| *v)
        })
    }
    pub fn float(&self, name: &str) -> Option<f32> {
        self.floats.get(name).copied().or_else(|| {
            self.floats
                .iter()
                .filter(|(k, _)| k.eq_ignore_ascii_case(name))
                .min_by(|a, b| a.0.cmp(b.0))
                .map(|(_, v)| *v)
        })
    }
}

/// `ChaAccessoryComponent` flags: which colour channels the game overwrites
/// from the card (`ChaControl.ChangeAccessoryColor`, ChaControl.cs:2598) and
/// which renderers belong to each group. `rendNormal`/`rendAlpha`/`rendHair`
/// are arrays of Renderer PPtrs, so membership is per renderer, not per item
/// (StudioEditor `KoikatsuAccessoryRendererMapLoader`).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct AccessoryColorUse {
    pub use01: bool,
    pub use02: bool,
    pub use03: bool,
    /// Renderer path ids the card's `_Color`/`_Color2`/`_Color3` are written to.
    pub normal: std::collections::HashSet<i64>,
    /// Renderer path ids that get the card's `_Color4` (and alpha visibility).
    pub alpha: std::collections::HashSet<i64>,
    /// Renderer path ids that get the hair start colour
    /// (`ChaAccessoryComponent.rendHair`, `ChangeAccessoryColor`).
    pub hair: std::collections::HashSet<i64>,
    /// `ChaCustomHairComponent.rendHair`. HairAccessoryCustomizer writes
    /// ColorMatch / outline / gloss here, after `ChangeAccessoryColor`.
    pub custom_hair: std::collections::HashSet<i64>,
    /// `ChaCustomHairComponent.rendAccessory` ??`AccessoryColor` on all three channels.
    pub rend_accessory: std::collections::HashSet<i64>,
    pub has_hair_component: bool,
    pub has_accessory_component: bool,
    /// `trfLength` GameObject names paired with the component's stored
    /// `baseLength`. Update writes base + addLength * lengthRate each frame.
    pub length_bones: Vec<(String, Option<f32>)>,
    pub add_length: f32,
}

/// Read the accessory prefab's `ChaAccessoryComponent` MonoBehaviour. A mod
/// pack can hold many accessories, so associate the component with the
/// prefab GameObject named `prefab_name` (the list `MainData`).
pub fn accessory_color_use(bytes: &[u8], prefab_name: &str) -> Option<AccessoryColorUse> {
    let bundle = load_bundle_unbounded(bytes.to_vec()).ok()?;
    let _ = bundle.data_checked();
    let files:Vec<_>=bundle.files.iter().filter_map(|file|bundle.extract_file_data(file).ok())
        .filter_map(|fb|parse_serialized_file(fb).ok()).collect();
    accessory_color_use_in(&files,prefab_name)
}
/// Shared by the byte entry point above and the cached parsed bundle.
pub(crate) fn accessory_color_use_in(files: &[unity_asset_binary::asset::SerializedFile], prefab_name: &str) -> Option<AccessoryColorUse> {
    let want = prefab_name.to_ascii_lowercase();
    let want_trim = prefab_name.trim_start_matches("p_").to_ascii_lowercase();
    let mut fallback: Option<AccessoryColorUse> = None;
    for ser in files {
        let mut scripts: HashMap<i64, String> = HashMap::new();
        for info in ser.objects_of_type(115) {
            if let Ok(obj) = ser.scene_object(&info) {
                let props = obj.class.properties();
                let class = props
                    .get("m_ClassName")
                    .or_else(|| props.get("m_Name"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                scripts.insert(info.path_id, class);
            }
        }
        let mut go_names: HashMap<i64, String> = HashMap::new();
        let mut tf_go: HashMap<i64, i64> = HashMap::new();
        let mut tf_children: HashMap<i64, Vec<i64>> = HashMap::new();
        for info in ser.objects_of_type(1) {
            if let Ok(obj) = ser.scene_object(&info) {
                if let Some(n) = obj.name() {
                    go_names.insert(info.path_id, n);
                }
            }
        }
        for info in ser.objects_of_type(4) {
            if let Ok(obj) = ser.scene_object(&info) {
                let props = obj.class.properties();
                if let Some(g) = props.get("m_GameObject").and_then(pptr_path_id) {
                    tf_go.insert(info.path_id, g);
                }
                if let Some(unity_asset_core::UnityValue::Array(ch)) = props.get("m_Children") {
                    tf_children.insert(info.path_id, ch.iter().filter_map(pptr_path_id).collect());
                }
            }
        }
        let mut go_tf: HashMap<i64, i64> = HashMap::new();
        for (tf, go) in &tf_go {
            go_tf.insert(*go, *tf);
        }
        // Expand the prefab root into every descendant GameObject so the
        // component can sit on a child (e.g. `o_acs_head_headband_00`).
        let mut subtree: std::collections::HashSet<i64> = std::collections::HashSet::new();
        let mut stack: Vec<i64> = go_names
            .iter()
            .filter(|(_, n)| {
                let l = n.to_ascii_lowercase();
                l == want || (!want_trim.is_empty() && l == want_trim)
            })
            .map(|(id, _)| *id)
            .collect();
        while let Some(go) = stack.pop() {
            if !subtree.insert(go) {
                continue;
            }
            if let Some(tf) = go_tf.get(&go) {
                if let Some(kids) = tf_children.get(tf) {
                    for t in kids {
                        if let Some(g) = tf_go.get(t) {
                            stack.push(*g);
                        }
                    }
                }
            }
        }
        // A different prefab's component is never a default for this asset.
        if !prefab_name.is_empty() && subtree.is_empty() { continue; }
        let mut found: Option<AccessoryColorUse> = None;
        for info in ser.objects_of_type(114) {
            let Ok(obj) = ser.scene_object(&info) else {
                continue;
            };
            let props = obj.class.properties();
            let class = props
                .get("m_Script")
                .and_then(pptr_path_id)
                .and_then(|id| scripts.get(&id))
                .cloned()
                .unwrap_or_default();
            let b = |k: &str| props.get(k).map(uv_bool).unwrap_or(false);
            let ids = |k: &str| -> std::collections::HashSet<i64> {
                props
                    .get(k)
                    .and_then(|v| v.as_array())
                    .map(|x| x.iter().filter_map(pptr_path_id).collect())
                    .unwrap_or_default()
            };
            let go = props.get("m_GameObject").and_then(pptr_path_id);
            let in_tree = go.map(|g| subtree.contains(&g)).unwrap_or(false);
            if class.contains("ChaCustomHairComponent") {
                if !in_tree && fallback.is_some() {
                    continue;
                }
                let length_bones: Vec<(String, Option<f32>)> = props.get("trfLength")
                    .and_then(|v| v.as_array())
                    .map(|transforms| {
                        let bases = props.get("baseLength").and_then(|v| v.as_array());
                        transforms.iter().enumerate().filter_map(|(i, value)| {
                            let name = pptr_path_id(value)
                                .and_then(|tf| tf_go.get(&tf))
                                .and_then(|g| go_names.get(g))?.clone();
                            let base = bases.and_then(|b| b.get(i)).map(uv_f32);
                            Some((name, base))
                        }).collect()
                    })
                    .unwrap_or_default();
                let add = props.get("addLength").map(uv_f32).unwrap_or(0.0);
                let apply_hair = |dst: &mut AccessoryColorUse| {
                    dst.has_hair_component = true;
                    dst.custom_hair = ids("rendHair");
                    dst.rend_accessory = ids("rendAccessory");
                    dst.length_bones = length_bones.clone();
                    dst.add_length = add;
                };
                if in_tree {
                    if let Some(dst) = found.as_mut() {
                        apply_hair(dst);
                    } else {
                        let mut cfg = AccessoryColorUse::default();
                        apply_hair(&mut cfg);
                        found = Some(cfg);
                    }
                } else if let Some(dst) = fallback.as_mut() {
                    apply_hair(dst);
                } else {
                    let mut cfg = AccessoryColorUse::default();
                    apply_hair(&mut cfg);
                    fallback = Some(cfg);
                }
                continue;
            }
            if !class.contains("ChaAccessoryComponent") {
                continue;
            }
            let use_cfg = AccessoryColorUse {
                has_accessory_component: true,
                use01: b("useColor01"),
                use02: b("useColor02"),
                use03: b("useColor03"),
                normal: ids("rendNormal"),
                alpha: ids("rendAlpha"),
                hair: ids("rendHair"),
                ..AccessoryColorUse::default()
            };
            if in_tree {
                if let Some(prev) = found.as_mut() {
                    prev.has_accessory_component = true;
                    prev.use01 = use_cfg.use01;
                    prev.use02 = use_cfg.use02;
                    prev.use03 = use_cfg.use03;
                    prev.normal = use_cfg.normal;
                    prev.alpha = use_cfg.alpha;
                    prev.hair = use_cfg.hair;
                } else {
                    found = Some(use_cfg);
                }
            } else if fallback.is_none() {
                fallback = Some(use_cfg);
            }
        }
        if found.is_some() {
            return found;
        }
    }
    fallback
}

/// `ChaClothesComponent` renderer groups. `ChangeCustomClothes`
/// (ChaControl.cs:2859-2966) writes create-RT 0 to `rendNormal01`/`rendAlpha01`/
/// `rendAccessory`, RT 1 to `rendNormal02`, RT 2 to `rendNormal03`.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct ClothesRendererMap {
    pub overlay_renderers: Vec<i64>,
    pub normal01: std::collections::HashSet<i64>,
    pub normal02: std::collections::HashSet<i64>,
    pub normal03: std::collections::HashSet<i64>,
    pub alpha01: std::collections::HashSet<i64>,
    pub accessory: std::collections::HashSet<i64>,
}

/// Read the clothes prefab's `ChaClothesComponent` (same prefab-subtree
/// association as `accessory_color_use`).
pub fn clothes_renderer_map<B: BundleInput + ?Sized>(bytes: &B, prefab_name: &str) -> Option<ClothesRendererMap> {
    crate::oklib::memo_bytes(bytes.bundle_bytes(),"clothes_renderer_map",prefab_name,||clothes_renderer_map_unity(bytes,prefab_name))
}
fn clothes_renderer_map_unity<B: BundleInput + ?Sized>(bytes: &B, prefab_name: &str) -> Option<ClothesRendererMap> {
    // Lazily: the serialized files' metadata, objects read when used (installed bundles reach 1.7 GB).
    let bundle = Arc::new(load_bundle_source(bytes.to_source(), false).ok()?);
    let want = prefab_name.to_ascii_lowercase();
    let want_trim = prefab_name.trim_start_matches("p_").to_ascii_lowercase();
    for node in &bundle.nodes {
        let file = unity_asset_binary::bundle::BundleFileInfo::new(node.name.clone(), node.offset, node.size);
        let Ok(ser) = parse_shared_serialized(BundleFileBytes { bundle: bundle.clone(), file }) else {
            continue;
        };
        let mut scripts: HashMap<i64, String> = HashMap::new();
        for info in ser.objects_of_type(115) {
            if let Ok(obj) = ser.scene_object(&info) {
                let props = obj.class.properties();
                let class = props
                    .get("m_ClassName")
                    .or_else(|| props.get("m_Name"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                scripts.insert(info.path_id, class);
            }
        }
        let mut go_names: HashMap<i64, String> = HashMap::new();
        let mut tf_go: HashMap<i64, i64> = HashMap::new();
        let mut tf_children: HashMap<i64, Vec<i64>> = HashMap::new();
        for info in ser.objects_of_type(1) {
            if let Ok(obj) = ser.scene_object(&info) {
                if let Some(n) = obj.name() {
                    go_names.insert(info.path_id, n);
                }
            }
        }
        for info in ser.objects_of_type(4) {
            if let Ok(obj) = ser.scene_object(&info) {
                let props = obj.class.properties();
                if let Some(g) = props.get("m_GameObject").and_then(pptr_path_id) {
                    tf_go.insert(info.path_id, g);
                }
                if let Some(unity_asset_core::UnityValue::Array(ch)) = props.get("m_Children") {
                    tf_children.insert(info.path_id, ch.iter().filter_map(pptr_path_id).collect());
                }
            }
        }
        let mut go_tf: HashMap<i64, i64> = HashMap::new();
        for (tf, go) in &tf_go {
            go_tf.insert(*go, *tf);
        }
        let mut subtree: std::collections::HashSet<i64> = std::collections::HashSet::new();
        let mut stack: Vec<i64> = go_names
            .iter()
            .filter(|(_, n)| {
                let l = n.to_ascii_lowercase();
                l == want || (!want_trim.is_empty() && l == want_trim)
            })
            .map(|(id, _)| *id)
            .collect();
        while let Some(go) = stack.pop() {
            if !subtree.insert(go) {
                continue;
            }
            if let Some(tf) = go_tf.get(&go) {
                if let Some(kids) = tf_children.get(tf) {
                    for t in kids {
                        if let Some(g) = tf_go.get(t) {
                            stack.push(*g);
                        }
                    }
                }
            }
        }
        for info in ser.objects_of_type(114) {
            let Ok(obj) = ser.scene_object(&info) else {
                continue;
            };
            let props = obj.class.properties();
            let class = props
                .get("m_Script")
                .and_then(pptr_path_id)
                .and_then(|id| scripts.get(&id))
                .cloned()
                .unwrap_or_default();
            if !class.contains("ChaClothesComponent") {
                continue;
            }
            let ids = |k: &str| -> std::collections::HashSet<i64> {
                props
                    .get(k)
                    .and_then(|v| v.as_array())
                    .map(|x| x.iter().filter_map(pptr_path_id).collect())
                    .unwrap_or_default()
            };
            let one = |k: &str| -> std::collections::HashSet<i64> {
                props.get(k).and_then(pptr_path_id).into_iter().collect()
            };
            let cfg = ClothesRendererMap {
                // KCOX selects ElementAtOrDefault(0), never the first non-null
                // renderer. The chosen create RT is shared by its renderer group.
                overlay_renderers: {
                    let first=|key:&str|props.get(key).and_then(|v|v.as_array()).and_then(|v|v.first()).and_then(pptr_path_id).filter(|id|*id!=0);
                    let mut selected=Vec::new();
                    for (a,b) in [("rendNormal01","rendNormal02"),("rendAlpha01","rendAlpha02")] {
                        if let Some(id)=first(a) {selected.push(id);if let Some(id)=first(b){selected.push(id);}break;}
                    }
                    selected
                },
                normal01: ids("rendNormal01"),
                normal02: ids("rendNormal02"),
                normal03: ids("rendNormal03"),
                alpha01: ids("rendAlpha01"),
                accessory: one("rendAccessory"),
            };
            let go = props.get("m_GameObject").and_then(pptr_path_id);
            if go.map(|g| subtree.contains(&g)).unwrap_or(false) {
                return Some(cfg);
            }
        }
    }
    None
}

/// Node existence used by `ChaControl.AddClothesStateKind` through
/// `ChaReference.GetReferenceInfo`. This walks the named prefab subtree, so a
/// sibling prefab in the same asset bundle cannot lend it a reference node.
pub fn prefab_node_names(bytes: &[u8], prefab_name: &str) -> std::collections::HashSet<String> {
    crate::oklib::memo_bytes(bytes,"prefab_node_names",prefab_name,||prefab_node_names_unity(bytes,prefab_name))
}
fn prefab_node_names_unity(bytes: &[u8], prefab_name: &str) -> std::collections::HashSet<String> {
    let mut names = std::collections::HashSet::new();
    let Ok(bundle) = load_bundle_unbounded(bytes.to_vec()) else { return names; };
    let want = prefab_name.to_ascii_lowercase();
    let want_trim = prefab_name.trim_start_matches("p_").to_ascii_lowercase();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue; };
        let Ok(ser) = parse_serialized_file(fb) else { continue; };
        let mut go_names = HashMap::<i64, String>::new();
        let mut tf_go = HashMap::<i64, i64>::new();
        let mut tf_children = HashMap::<i64, Vec<i64>>::new();
        for info in ser.objects_of_type(1) {
            if let Ok(obj) = ser.scene_object(&info) {
                if let Some(name) = obj.name() {
                    go_names.insert(info.path_id, name.to_ascii_lowercase());
                }
            }
        }
        for info in ser.objects_of_type(4) {
            if let Ok(obj) = ser.scene_object(&info) {
                let props = obj.class.properties();
                if let Some(go) = props.get("m_GameObject").and_then(pptr_path_id) {
                    tf_go.insert(info.path_id, go);
                }
                if let Some(unity_asset_core::UnityValue::Array(children)) = props.get("m_Children") {
                    tf_children.insert(info.path_id, children.iter().filter_map(pptr_path_id).collect());
                }
            }
        }
        let go_tf: HashMap<i64, i64> = tf_go.iter().map(|(tf, go)| (*go, *tf)).collect();
        let mut stack: Vec<i64> = go_names.iter()
            .filter(|(_, name)| **name == want || (!want_trim.is_empty() && **name == want_trim))
            .map(|(id, _)| *id).collect();
        let mut seen = std::collections::HashSet::new();
        while let Some(go) = stack.pop() {
            if !seen.insert(go) { continue; }
            if let Some(name) = go_names.get(&go) { names.insert(name.clone()); }
            if let Some(kids) = go_tf.get(&go).and_then(|tf| tf_children.get(tf)) {
                for tf in kids {
                    if let Some(child) = tf_go.get(tf) { stack.push(*child); }
                }
            }
        }
    }
    names
}

/// A prefab's baked lightmaps: (hierarchy node, lightmap index, Renderer.m_LightmapScaleOffset) per lightmapped
/// MeshRenderer, and the lightmap textures by index (RGBM PNG: colour = 5 x alpha x rgb, gamma space).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabLightmaps { pub renderers: Vec<(i32, i32, [f32; 4])>, pub textures: Vec<Vec<u8>> }

/// A Camera of a prefab (a Studio map scene) that draws into a RenderTexture (`m_TargetTexture`, class 84).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabRenderCamera {
    /// Hierarchy node (`prefab_hierarchy` numbering) of the camera's GameObject.
    pub node: i32,
    /// Camera.enabled and its GameObject's m_IsActive.
    pub enabled: bool,
    pub active: bool,
    /// Vertical field of view (degrees), clip planes (metres), orthographic size (half height, metres).
    pub fov: f32, pub near: f32, pub far: f32, pub orthographic: bool, pub ortho_size: f32,
    pub culling_mask: u32,
    /// m_ClearFlags (1 skybox, 2 colour, 3 depth, 4 nothing) and m_BackGroundColor.
    pub clear_flags: i32, pub background: [f32; 4],
    /// The target RenderTexture: name and size.
    pub target: String, pub width: i32, pub height: i32,
    /// The target's identity across prefabs (`render_texture_key`): a Studio camera item and the screen items showing
    /// it are separate prefabs sharing one RenderTexture asset.
    pub target_key: u32,
}

/// `PrefabRenderCamera` / `PrefabRenderTextures` as libraries recorded them before target keys and shared screens
/// (query `prefab_render_textures`; the records are bincode, which reads no added fields).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabRenderCameraV1 {
    pub node: i32, pub enabled: bool, pub active: bool,
    pub fov: f32, pub near: f32, pub far: f32, pub orthographic: bool, pub ortho_size: f32,
    pub culling_mask: u32, pub clear_flags: i32, pub background: [f32; 4],
    pub target: String, pub width: i32, pub height: i32,
}
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabRenderTexturesV1 { pub cameras: Vec<PrefabRenderCameraV1>, pub users: Vec<(i32, String, String, i32)> }
impl From<PrefabRenderTexturesV1> for PrefabRenderTextures {
    fn from(v: PrefabRenderTexturesV1) -> Self {
        PrefabRenderTextures {
            cameras: v.cameras.into_iter().map(|c| PrefabRenderCamera { node: c.node, enabled: c.enabled, active: c.active, fov: c.fov, near: c.near,
                far: c.far, orthographic: c.orthographic, ortho_size: c.ortho_size, culling_mask: c.culling_mask, clear_flags: c.clear_flags,
                background: c.background, target: c.target, width: c.width, height: c.height, target_key: 0 }).collect(),
            users: v.users, shared: Vec::new(),
        }
    }
}

/// A RenderTexture asset's identity within its bundle: its name and path id hashed to 24 bits (exact in a float
/// material value; 0 is none).
pub fn render_texture_key(name: &str, path_id: i64) -> u32 {
    let mut h: u32 = 0x811c9dc5;
    for b in name.bytes().chain(path_id.to_le_bytes()) { h = (h ^ b as u32).wrapping_mul(0x01000193); }
    (h & 0xff_ffff).max(1)
}

/// A prefab's render-to-texture cameras, and the renderers whose materials sample their RenderTextures:
/// (renderer node, material name, texture property, camera index).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabRenderTextures {
    pub cameras: Vec<PrefabRenderCamera>, pub users: Vec<(i32, String, String, i32)>,
    /// Renderers sampling a RenderTexture no camera of this prefab draws (a Studio screen item): (renderer node,
    /// material name, texture property, `render_texture_key`, width, height).
    pub shared: Vec<(i32, String, String, u32, i32, i32)>,
}

/// One time of day of a Studio map's SunLightInfo (on the map root): the GameObjects shown then (`visibleList`, by
/// `prefab_hierarchy` node; every list is hidden first, then the current one shown) and the sun (`_targetLight`'s Euler
/// angle in degrees, colour, intensity).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabSunInfo { pub kind: i32, pub visible: Vec<i32>, pub angle: [f32; 3], pub color: [f32; 4], pub intensity: f32 }

/// A prefab Light's runtime part (`prefab_light_modes`): baking mode, culling mask, shadow type, active GameObject.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabLightMode { pub node: i32, pub lightmapping: i32, pub culling_mask: u32, pub shadows: i32, pub active: bool }

/// A Studio map's SunLightInfo: the sun Light's node, type, culling mask and baking mode (m_Lightmapping 4 realtime),
/// and the times of day it has (SunLightInfo.Info.Type 0 morning, 1 day, 2 evening, 3 night).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabSunLight { pub light_node: i32, pub light_type: i32, pub culling_mask: u32, pub lightmapping: i32, pub infos: Vec<PrefabSunInfo> }

/// A baked ReflectionProbe of a prefab (a Studio map scene): what Unity Standard's indirect specular samples. `node` is
/// the probe's GameObject (`prefab_hierarchy` numbering; -1 for the scene's skybox reflection, used outside every probe);
/// the box (Unity metres, around the node) picks the renderers it reflects for.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabReflectionProbe {
    pub node: i32,
    pub box_size: [f32; 3],
    pub box_offset: [f32; 3],
    pub importance: i32,
    pub intensity: f32,
    /// Face size of mip 0 and the mips in the atlas.
    pub size: u32,
    pub mips: u32,
    /// The cubemap as one PNG: the six faces (+X, -X, +Y, -Y, +Z, -Z, as Unity stores them) side by side, each mip
    /// below the previous one (mip k starts at row size * (2 - 2^(1-k))); 8-bit display values (BC6H clamped to 0..1).
    pub atlas: Vec<u8>,
}

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabReflectionProbes { pub probes: Vec<PrefabReflectionProbe> }

/// Lossless default or local reflection environment.
/// Cube faces retain native D3D order, each followed by its authored mip chain.
pub struct SceneReflectionCube {
    pub name: String, pub size: u32, pub mips: u32, pub dxgi_format: u32,
    pub bytes: Vec<u8>, pub intensity: f32,
}

/// Material values a prefab's Animators hold once their controllers have settled (anim::controller_steady_clips at
/// t = 0): (renderer node, material property name hash (CRC32, low 28 bits), component (Unity binding attribute's top
/// four bits: 4..7 = r, g, b, a of a colour, others a float), value). Unity's renderer animation sets every material of
/// the renderer that has the property (`material._EmissionColor.r`); the live stage's stairs, arch, wings and ribbon
/// glow only through these.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabAnimatedMaterials { pub values: Vec<(i32, u32, u8, f32)> }

/// Per-instance activeInHierarchy: a child cannot override a disabled ancestor.
/// Time-of-day roots are normalized by the caller because SunLightInfo switches them at runtime.
fn prefab_active_in_hierarchy(h: &crate::pose::PrefabHierarchy, active: &[bool], overrides: &HashMap<usize, bool>, node: i32) -> Option<bool> {
    let mut n = node; let mut visible = true;
    while n >= 0 {
        let i = n as usize; let entry = h.nodes.get(i)?;
        let value = if let Some(v) = overrides.get(&i) { *v } else { *active.get(i)? };
        visible &= value; n = entry.parent;
    }
    Some(visible)
}

/// Unity's built-in primitive meshes (PathID in Library/unity default resources): Cube 10202, Sphere 10207 (radius 0.5,
/// 24 segments x 16 rings), Plane 10209 (10 x 10 in XZ), Quad 10210 (1 x 1 facing -Z). Unity-space positions.
fn builtin_mesh(path_id: i64) -> Option<ExtractedMesh> {
    let (name, positions, normals, uvs, indices): (&str, Vec<[f32; 3]>, Vec<[f32; 3]>, Vec<[f32; 2]>, Vec<u32>) = match path_id {
        10207 => {
            let (seg, ring) = (24u32, 16u32);
            let (mut p, mut n, mut t, mut idx) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
            for r in 0..=ring {
                let phi = std::f32::consts::PI * r as f32 / ring as f32;
                for s in 0..=seg {
                    let theta = 2.0 * std::f32::consts::PI * s as f32 / seg as f32;
                    let d = [phi.sin() * theta.cos(), phi.cos(), phi.sin() * theta.sin()];
                    p.push([d[0] * 0.5, d[1] * 0.5, d[2] * 0.5]); n.push(d);
                    t.push([s as f32 / seg as f32, 1.0 - r as f32 / ring as f32]);
                }
            }
            for r in 0..ring { for s in 0..seg {
                let a = r * (seg + 1) + s; let b = a + seg + 1;
                idx.extend_from_slice(&[a, a + 1, b, a + 1, b + 1, b]);
            } }
            ("Sphere", p, n, t, idx)
        }
        10210 => ("Quad", vec![[-0.5, -0.5, 0.0], [0.5, -0.5, 0.0], [-0.5, 0.5, 0.0], [0.5, 0.5, 0.0]], vec![[0.0, 0.0, -1.0]; 4],
            vec![[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]], vec![0, 3, 1, 3, 0, 2]),
        10209 => {
            let (mut p, mut n, mut t, mut idx) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
            for z in 0..=10u32 { for x in 0..=10u32 {
                p.push([5.0 - x as f32, 0.0, 5.0 - z as f32]); n.push([0.0, 1.0, 0.0]); t.push([x as f32 / 10.0, z as f32 / 10.0]);
            } }
            for z in 0..10u32 { for x in 0..10u32 {
                let a = z * 11 + x;
                idx.extend_from_slice(&[a, a + 11, a + 1, a + 1, a + 11, a + 12]);
            } }
            ("Plane", p, n, t, idx)
        }
        10202 => {
            let (mut p, mut n, mut t, mut idx) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
            for axis in 0..3usize { for sign in [1.0f32, -1.0] {
                let mut normal = [0.0; 3]; normal[axis] = sign;
                let (u, v) = ((axis + 1) % 3, (axis + 2) % 3);
                let base = p.len() as u32;
                for (a, b) in [(-0.5f32, -0.5f32), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)] {
                    let mut q = [0.0; 3]; q[axis] = 0.5 * sign; q[u] = a; q[v] = b;
                    p.push(q); n.push(normal); t.push([a + 0.5, b + 0.5]);
                }
                if sign > 0.0 { idx.extend_from_slice(&[base, base + 1, base + 3, base, base + 3, base + 2]); }
                else { idx.extend_from_slice(&[base, base + 3, base + 1, base, base + 2, base + 3]); }
            } }
            ("Cube", p, n, t, idx)
        }
        _ => return None,
    };
    let obj = mesh_to_obj(name, &positions, &uvs, &indices);
    Some(ExtractedMesh {
        name: name.into(), obj, path_id,
        submesh_ranges: vec![(0, indices.len(), 0)],
        source_tangents: Some(false),
        positions, normals, uvs, indices,
        ..Default::default()
    })
}

/// unity_ColorSpaceDouble as compiled into a pixel program: 4.5948 (linear projects) when that literal is in the
/// bytecode, else 2 (gamma projects add the colour to itself instead of multiplying).
fn color_space_double(dxbc: &[u8]) -> f32 {
    let linear = dxbc.chunks_exact(4).any(|w| (4.594_f32..4.596).contains(&f32::from_le_bytes([w[0], w[1], w[2], w[3]])));
    if linear { 4.594_793_8 } else { 2.0 }
}

/// A cubemap's faces and mips (Unity Texture2D payload of a Cubemap: six faces one after another, each with its mip
/// chain) as the atlas `PrefabReflectionProbe::atlas` describes; None for formats it does not decode.
fn cubemap_atlas(tex: &unity_asset_decode::texture::Texture2D) -> Option<(u32, u32, Vec<u8>)> {
    use unity_asset_decode::texture::TextureFormat;
    let size = tex.width.max(1) as usize;
    if tex.height as usize != size || tex.image_data.is_empty() { return None; }
    let mips = tex.mip_count.max(1) as usize;
    let block = |w: usize, bytes: usize| w.div_ceil(4).max(1) * w.div_ceil(4).max(1) * bytes;
    let mip_bytes = |w: usize| -> Option<usize> {
        Some(match tex.format {
            TextureFormat::BC6H | TextureFormat::DXT5 | TextureFormat::BC7 => block(w, 16),
            TextureFormat::DXT1 => block(w, 8),
            TextureFormat::RGBA32 | TextureFormat::BGRA32 | TextureFormat::ARGB32 => w * w * 4,
            TextureFormat::RGB24 => w * w * 3,
            TextureFormat::RGBAHalf => w * w * 8,
            _ => return None,
        })
    };
    let face_bytes: usize = (0..mips).map(|k| mip_bytes((size >> k).max(1))).sum::<Option<usize>>()?;
    if tex.image_data.len() < face_bytes * 6 { return None; }
    let mut atlas = image::RgbaImage::new((size * 6) as u32, (size * 2) as u32);
    for face in 0..6 {
        let mut offset = face * face_bytes;
        let mut row = 0usize;
        for k in 0..mips {
            let w = (size >> k).max(1);
            let len = mip_bytes(w)?;
            let src = &tex.image_data[offset..offset + len];
            offset += len;
            let mut px = vec![0u32; w * w];
            let decoded = match tex.format {
                TextureFormat::BC6H => texture2ddecoder::decode_bc6(src, w, w, &mut px, false).is_ok(),
                TextureFormat::BC7 => texture2ddecoder::decode_bc7(src, w, w, &mut px).is_ok(),
                TextureFormat::DXT5 => texture2ddecoder::decode_bc3(src, w, w, &mut px).is_ok(),
                TextureFormat::DXT1 => texture2ddecoder::decode_bc1(src, w, w, &mut px).is_ok(),
                _ => {
                    for (i, p) in px.iter_mut().enumerate() {
                        let [r, g, b] = match tex.format {
                            TextureFormat::RGBA32 => [src[i * 4], src[i * 4 + 1], src[i * 4 + 2]],
                            TextureFormat::BGRA32 => [src[i * 4 + 2], src[i * 4 + 1], src[i * 4]],
                            TextureFormat::ARGB32 => [src[i * 4 + 1], src[i * 4 + 2], src[i * 4 + 3]],
                            TextureFormat::RGB24 => [src[i * 3], src[i * 3 + 1], src[i * 3 + 2]],
                            _ => {
                                let h = |c: usize| { let v = f16_to_f32(u16::from_le_bytes([src[i * 8 + c * 2], src[i * 8 + c * 2 + 1]])); (v.clamp(0., 1.) * 255.).round() as u8 };
                                [h(0), h(1), h(2)]
                            }
                        };
                        *p = 0xff00_0000 | (r as u32) << 16 | (g as u32) << 8 | b as u32;
                    }
                    true
                }
            };
            if !decoded { return None; }
            // Unity stores texture rows bottom-up; the atlas is top-down like every other exported PNG.
            for y in 0..w { for x in 0..w {
                let v = px[(w - 1 - y) * w + x];
                atlas.put_pixel((face * size + x) as u32, (row + y) as u32, image::Rgba([(v >> 16) as u8, (v >> 8) as u8, v as u8, 255]));
            } }
            row += w;
        }
    }
    let mut png = Vec::new();
    image::DynamicImage::ImageRgba8(atlas).write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png).ok()?;
    Some((size as u32, mips as u32, png))
}

/// A map of byte strings read whole: bincode writes a `Vec<u8>` and a byte string alike, and element by element was
/// most of what reading a card's records cost.
mod byte_map {
    use std::collections::BTreeMap;
    pub fn serialize<S: serde::Serializer>(map: &BTreeMap<String, Vec<u8>>, s: S) -> Result<S::Ok, S::Error> {
        use serde::ser::SerializeMap;
        let mut out = s.serialize_map(Some(map.len()))?;
        for (k, v) in map { out.serialize_entry(k, serde_bytes::Bytes::new(v))?; }
        out.end()
    }
    pub fn deserialize<'de, D: serde::Deserializer<'de>>(d: D) -> Result<BTreeMap<String, Vec<u8>>, D::Error> {
        let map: BTreeMap<String, serde_bytes::ByteBuf> = serde::Deserialize::deserialize(d)?;
        Ok(map.into_iter().map(|(k, v)| (k, v.into_vec())).collect())
    }
}

/// A texture payload as records keep it: `Texture2D` written as it is, read through a copy of its fields whose texels
/// are one byte string (see `byte_map`).
pub(crate) struct TexturePayload(pub unity_asset_decode::texture::Texture2D);

impl serde::Serialize for TexturePayload {
    fn serialize<S: serde::Serializer>(&self, s: S) -> Result<S::Ok, S::Error> { self.0.serialize(s) }
}

impl<'de> serde::Deserialize<'de> for TexturePayload {
    fn deserialize<D: serde::Deserializer<'de>>(d: D) -> Result<Self, D::Error> {
        use unity_asset_decode::texture::{GLTextureSettings, StreamingInfo, Texture2D, TextureFormat};
        #[derive(serde::Deserialize)]
        struct Fields {
            name: String, width: i32, height: i32, complete_image_size: i32, format: TextureFormat, mip_map: bool,
            mip_count: i32, is_readable: bool, image_count: i32, texture_dimension: i32, light_map_format: i32,
            color_space: i32, data_size: i32, stream_info: StreamingInfo, texture_settings: GLTextureSettings,
            #[serde(with = "serde_bytes")]
            image_data: Vec<u8>,
            forced_fallback_format: Option<i32>, downscale_fallback: Option<bool>, is_alpha_channel_optional: Option<bool>,
            mips_stripped: Option<i32>,
        }
        let f = <Fields as serde::Deserialize>::deserialize(d)?;
        Ok(TexturePayload(Texture2D {
            name: f.name, width: f.width, height: f.height, complete_image_size: f.complete_image_size, format: f.format,
            mip_map: f.mip_map, mip_count: f.mip_count, is_readable: f.is_readable, image_count: f.image_count,
            texture_dimension: f.texture_dimension, light_map_format: f.light_map_format, color_space: f.color_space,
            data_size: f.data_size, stream_info: f.stream_info, texture_settings: f.texture_settings, image_data: f.image_data,
            forced_fallback_format: f.forced_fallback_format, downscale_fallback: f.downscale_fallback,
            is_alpha_channel_optional: f.is_alpha_channel_optional, mips_stripped: f.mips_stripped,
        }))
    }
}

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct ExtractedMesh {
    /// Settings of textures created by runtime blits, keyed by shader property.
    /// [filter, wrap_u, wrap_v, srgb, mip_policy]; policy 1 averages encoded
    /// bytes like the captured Unity RenderTexture chain. Authored Texture2D
    /// mips must not be regenerated merely because a PNG exists.
    #[serde(default)]
    pub runtime_textures: std::collections::BTreeMap<String, [i32; 5]>,
    /// Additional authored material textures. Raw Unity row order until push_meshes.
    #[serde(default, with = "byte_map")]
    pub shader_extra_textures: std::collections::BTreeMap<String, Vec<u8>>,
    pub name: String,
    /// OBJ text is rebuilt from positions when writing files. Skipped in the
    /// SQLite blob so a cached prefab is not several times larger than the mesh.
    #[serde(default, skip)]
    pub obj: String,
    #[allow(dead_code)]
    pub path_id: i64,
    /// Renderer component path ids in the prefab that draw this mesh. Used to
    /// match `ChaAccessoryComponent.rendNormal`/`rendAlpha`/`rendHair` PPtrs so
    /// card colour is applied per renderer (not per item).
    pub renderer_ids: Vec<i64>,
    /// GameObject names of those renderers. Material Editor stores
    /// `Renderer.enabled` against this name, not the shared-mesh asset name.
    // Bincode uses positional fields: omitting an empty Vec shifts every
    // following field during deserialization. Keep the field even when empty.
    #[serde(default)]
    pub renderer_names: Vec<String>,
    /// Renderer component, not the Mesh's optional bone channels, decides
    /// whether Unity skins it. Imported MeshFilter assets may retain weights.
    #[serde(default)]
    pub renderer_skinned: Option<bool>,
    /// Unity material of this renderer (first submesh material).
    pub mat: Option<MeshMaterial>,
    /// Unity `m_SubMeshes`: index-buffer start/count and baseVertex. A renderer
    /// may bind a different material to every range; keep them separate.
    #[serde(default)]
    pub submesh_ranges: Vec<(usize, usize, u32)>,
    #[serde(default)]
    pub submesh_index: usize,
    /// Renderer belongs to a hair group while the mod bundle's external
    /// shader reference has no resolvable name.
    #[serde(default)]
    pub hair_shader_hint: bool,
    /// The resolved renderer has a Shader Forge/main_hair_front layer. This
    /// includes Material Editor copies on the same renderer/submesh.
    #[serde(default, with = "serde_bytes")]
    pub texture_png: Option<Vec<u8>>,
    pub texture_name: String,
    /// OK base library: texture fields stored as (field, texture, transform)
    /// references to the source's OK textures; empty outside a stored model.
    /// Serialized only inside OK base library records (`with_texture_plans`):
    /// sessions, okPv packs and caches keep the original mesh layout.
    #[serde(default, skip_serializing_if = "texture_plan_hidden", deserialize_with = "texture_plan_compat")]
    pub texture_plan: Vec<(String, String, u8)>,
    /// Hair shader `_ColorMask` (RGB ??_Color / _Color2 / _Color3).
    #[serde(with = "serde_bytes")]
    pub color_mask_png: Option<Vec<u8>>,
    /// Material `_AlphaMask` retained when no outer garment replaces it.
    #[serde(with = "serde_bytes")]
    pub alpha_mask_png: Option<Vec<u8>>,
    /// Shader `_DetailMask`; retained separately from baked albedo.
    #[serde(with = "serde_bytes")]
    pub detail_mask_png: Option<Vec<u8>>,
    /// Shader `_LineMask`; its channels gate the clothing ramp and painted seams.
    #[serde(with = "serde_bytes")]
    pub line_mask_png: Option<Vec<u8>>,
    /// Shader `_NormalMap`; retained for the fragment normal path.
    #[serde(with = "serde_bytes")]
    pub normal_map_png: Option<Vec<u8>>,
    /// `main_skin` `_NormalMask`; G bends face lighting toward the view.
    #[serde(with = "serde_bytes")]
    pub normal_mask_png: Option<Vec<u8>>,
    /// Whether Unity supplied the tangent basis required by a tangent-space
    /// normal map. `None` is reserved for meshes built outside the Unity decoder.
    #[serde(default)]
    pub source_tangents: Option<bool>,
    /// Authored Unity tangent.xyz and handedness.w in mesh rest space.
    #[serde(default)]
    pub tangents: Vec<[f32; 4]>,
    /// Hair shader `_HairGloss` (matcap sheen).
    #[serde(with = "serde_bytes")]
    pub hair_gloss_png: Option<Vec<u8>>,
    pub positions: Vec<[f32; 3]>,
    /// Unity `m_Normals` (file space, rest). Empty if the channel is missing.
    pub normals: Vec<[f32; 3]>,
    /// File-space normals saved before the first skin pass (mirrors `rest_positions`).
    pub rest_normals: Vec<[f32; 3]>,
    pub uvs: Vec<[f32; 2]>,
    /// Unity `mesh.uv2` (TEXCOORD1), used by main_hair `_HairGloss`.
    #[serde(default)]
    pub uv2: Vec<[f32; 2]>,
    /// Unity TEXCOORD2: body `_overtex2` uses this independently of nipple UVs.
    #[serde(default)]
    pub uv3: Vec<[f32; 2]>,
    /// Unity `mesh.uv4` (TEXCOORD3). `Koikano/main_skin` samples `_overtex3` with this.
    pub uv4: Vec<[f32; 2]>,
    /// Authored Unity COLOR0, used by main_skin nipple/underhair UV gates.
    #[serde(default)]
    pub vertex_colors: Vec<[u8; 4]>,
    pub indices: Vec<u32>,
    pub weights: Vec<[f32; 4]>,
    pub bone_idx: Vec<[u16; 4]>,
    pub bind_poses: Vec<crate::pose::Mat4>,
    /// SMR bone names in bind-pose order. Hair/acc prefabs reuse one
    /// transform tree but each renderer has its own bone list.
    pub bone_names: Vec<String>,
    /// Per-renderer skeleton (SMR bone order + the prefab transform tree).
    /// Hair/acc must skin with this, not the first mesh's bone list.
    pub smr_skel: Option<crate::pose::Skeleton>,
    #[serde(default)]
    pub bone_implants: Vec<crate::dbone::implant::Marker>,
    /// File-space verts before CPU skin. Empty until first `skin_mesh`.
    pub rest_positions: Vec<[f32; 3]>,
    /// After skin, parent this bone's world onto the mesh (hair/acc).
    pub parent_bone: Option<String>,
    /// Bone whose neutral-to-animated delta moves vertices already baked into
    /// body space. Unlike parent_bone this must not cause a second attachment.
    #[serde(default)]
    pub rigid_follow_bone: Option<String>,
    /// 0 = none, 1 = body skeleton, 2 = head skeleton.
    pub skel_kind: u8,
    /// Blend shapes: (channel name, [(mesh vertex index, delta at weight 100)]).
    pub blend_shapes: Vec<(String, Vec<(u32, [f32; 3])>)>,
    /// Mesh GameObject transform relative to the prefab root. Skinned meshes
    /// bake this into their bind poses; non-skinned accessories must apply it
    /// explicitly or they land at the parent origin.
    pub prefab_local: Option<crate::pose::Mat4>,
    /// `N_move` / `N_move2` (parentWorld, selfWorld) in prefab space. The card's
    /// addMove replaces those nodes' local TRS (ChaControl.cs:2570), so the mesh
    /// rigid transform becomes `parentWorld * addMove * inv(selfWorld) * meshWorld`.
    /// SMRs replace these nodes' local TRS in their hierarchy before skinning;
    /// a node can affect only a subset of the renderer's bone slots.
    pub move_nodes: [Option<(crate::pose::Mat4, crate::pose::Mat4)>; 2],
    /// Accessory DynamicBone is evaluated after the card's parent/N_move
    /// placement, since its force and collision tests operate in world space.
    #[serde(default)]
    pub(crate) deferred_dbones: Option<crate::dbone::DBoneSet>,
}

/// One Unity assetbundle with UnityFS decompressed once. Meshes and PNG
/// textures are decoded lazily for the requested prefab so a 40MB hair/shoe
/// pack is not fully PNG-encoded just to pull one item.
thread_local! {
    /// Migration only: read `texture_plan` as absent (legacy records).
    static TEXTURE_PLAN_ABSENT: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
    /// Library record (de)serialization: `texture_plan` is part of the layout.
    static TEXTURE_PLAN_SERIALIZED: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
}
/// Run (de)serialization of OK base library records: mesh texture plans
/// are part of their layout. Elsewhere ExtractedMesh keeps its old layout.
pub(crate) fn with_texture_plans<R>(f: impl FnOnce() -> R) -> R {
    let previous = TEXTURE_PLAN_SERIALIZED.with(|c| c.replace(true));
    let r = f();
    TEXTURE_PLAN_SERIALIZED.with(|c| c.set(previous));
    r
}
fn texture_plan_hidden(_: &Vec<(String, String, u8)>) -> bool { !TEXTURE_PLAN_SERIALIZED.with(|c| c.get()) }
fn texture_plan_compat<'de, D: serde::Deserializer<'de>>(d: D) -> Result<Vec<(String, String, u8)>, D::Error> {
    use serde::Deserialize;
    if TEXTURE_PLAN_ABSENT.with(|c| c.get()) || !TEXTURE_PLAN_SERIALIZED.with(|c| c.get()) { return Ok(Vec::new()); }
    Vec::<(String, String, u8)>::deserialize(d)
}
/// One legacy mesh: `texture_plan` was omitted when empty. Try it present
/// (and well-formed), else absent.
/// Bounded bincode read (legacy layout probing): a length larger than the
/// remaining bytes is an error, never an allocation.
fn legacy_decode<T: serde::de::DeserializeOwned>(r: &mut &[u8]) -> Result<T, bincode::Error> {
    use bincode::Options;
    let limit = r.len() as u64;
    bincode::DefaultOptions::new().with_fixint_encoding().allow_trailing_bytes().with_limit(limit).deserialize_from(r)
}
fn read_legacy_mesh(reader: &mut &[u8]) -> Option<ExtractedMesh> {
    let fields = ["texture_png","color_mask_png","alpha_mask_png","detail_mask_png","line_mask_png","normal_map_png","normal_mask_png","hair_gloss_png"];
    let mut probe = *reader;
    let present = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| legacy_decode::<ExtractedMesh>(&mut probe))).unwrap_or_else(|_| Err(Box::new(bincode::ErrorKind::Custom("panic".into()))));
    if let Ok(mesh) = present {
        if !mesh.texture_plan.is_empty() && mesh.texture_plan.iter().all(|(f, n, t)| *t <= 3 && !n.is_empty() && (fields.contains(&f.as_str()) || f.starts_with("extra:"))) {
            *reader = probe;
            return Some(mesh);
        }
    }
    TEXTURE_PLAN_ABSENT.with(|c| c.set(true));
    let mut probe = *reader;
    let mesh = legacy_decode::<ExtractedMesh>(&mut probe);
    TEXTURE_PLAN_ABSENT.with(|c| c.set(false));
    let mesh = mesh.ok()?;
    *reader = probe;
    Some(mesh)
}
fn read_legacy_meshes(reader: &mut &[u8]) -> Option<Vec<ExtractedMesh>> {
    let n = u64::from_le_bytes(reader.get(..8)?.try_into().ok()?);
    *reader = &reader[8..];
    (0..n).map(|_| read_legacy_mesh(reader)).collect()
}
/// OK base library migration: re-encode a stored value whose meshes were
/// written with the legacy `texture_plan` layout. None: not a mesh record.
pub(crate) fn migrate_mesh_record(query: &str, raw: &[u8]) -> Option<Result<Vec<u8>, String>> {
    // Already in the current layout (the whole value decodes): unchanged.
    let current = |r: &mut &[u8]| -> bool {
        let ok = match query {
            "meshes_for_maindata" => legacy_decode::<(Vec<ExtractedMesh>, Vec<(i64, Vec<String>)>)>(&mut *r).is_ok(),
            "decode_all" => legacy_decode::<(Vec<ExtractedMesh>, Vec<(String, Vec<u8>)>, Option<crate::pose::Skeleton>)>(&mut *r).is_ok(),
            "base_bundle" => legacy_decode::<(Vec<ExtractedMesh>, Option<crate::pose::Skeleton>, Vec<(String, Vec<u8>)>)>(&mut *r).is_ok(),
            _ => false,
        };
        ok && r.is_empty()
    };
    if !matches!(query, "meshes_for_maindata" | "decode_all" | "base_bundle") { return None; }
    with_texture_plans(|| {
        let mut probe = raw;
        if std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| current(&mut probe))).unwrap_or(false) { return None; }
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| migrate_legacy(query, raw))).unwrap_or_else(|_| Some(Err("panic".into())))
    })
}
fn migrate_legacy(query: &str, raw: &[u8]) -> Option<Result<Vec<u8>, String>> {
    let mut r = raw;
    let out = match query {
        "meshes_for_maindata" => (|| {
            let meshes = read_legacy_meshes(&mut r)?;
            let chains: Vec<(i64, Vec<String>)> = legacy_decode(&mut r).ok()?;
            bincode::serialize(&(meshes, chains)).ok()
        })(),
        "decode_all" => (|| {
            let meshes = read_legacy_meshes(&mut r)?;
            let textures: Vec<(String, Vec<u8>)> = legacy_decode(&mut r).ok()?;
            let skel: Option<crate::pose::Skeleton> = legacy_decode(&mut r).ok()?;
            bincode::serialize(&(meshes, textures, skel)).ok()
        })(),
        "base_bundle" => (|| {
            let meshes = read_legacy_meshes(&mut r)?;
            let skel: Option<crate::pose::Skeleton> = legacy_decode(&mut r).ok()?;
            let textures: Vec<(String, Vec<u8>)> = legacy_decode(&mut r).ok()?;
            bincode::serialize(&(meshes, skel, textures)).ok()
        })(),
        _ => return None,
    };
    Some(match out { Some(v) if r.is_empty() => Ok(v), Some(_) => Err("trailing bytes".into()), None => Err("undecodable".into()) })
}

impl ExtractedMesh {
    pub(crate) fn uses_skin_renderer(&self) -> bool {
        self.renderer_skinned.unwrap_or(!self.bind_poses.is_empty())
    }
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub(crate) struct AccessoryBodyBinding {
    pub renderers: std::collections::HashSet<i64>,
    pub armature_root: i64,
}

pub struct ParsedBundle {
    pub files: Vec<(String, BundleFileBytes)>,
    serialized: RefCell<Option<Vec<unity_asset_binary::asset::SerializedFile>>>,
    serialized_names: RefCell<Vec<String>>,
    /// Index into `files` of each parsed serialized file.
    serialized_file_index: RefCell<Vec<usize>>,
    script_root: RefCell<Option<std::path::PathBuf>>,
    script_origin: RefCell<Option<std::path::PathBuf>>,
    script_maps: RefCell<Option<Vec<references::Scripts>>>,
    meshes: RefCell<HashMap<i64, ExtractedMesh>>,
    /// Dependency serialized files (CAB name -> parsed file and its bundle's
    /// stream nodes) that renderer meshes point into; None = unreadable.
    external_files: RefCell<HashMap<String, Option<std::rc::Rc<ExternalFile>>>>,
    /// Dependency-bundle textures by "<external path>|<name>" (lower case).
    external_textures: RefCell<HashMap<String, Option<Vec<u8>>>>,
    /// Texture name (lower case) -> the external file a material takes it from (filled with the material maps).
    dependency_texture_source: RefCell<HashMap<String, String>>,
    textures: RefCell<HashMap<String, Vec<u8>>>,
    /// Bytes held by `textures` (a cache, cleared when it grows past TEXTURE_CACHE_BYTES).
    texture_bytes: std::cell::Cell<usize>,
    tex_names: RefCell<Vec<(String, u32)>>,
    tex_objects: RefCell<Option<HashMap<String,TextureObjectLocation>>>,
    tex_name_objects: RefCell<Option<HashMap<String,(usize,i64,String,bool)>>>,
    tex_exact_objects: RefCell<HashMap<String,Option<TextureObjectLocation>>>,
    tex_asset_objects: RefCell<Option<HashMap<String,Option<(usize,i64)>>>>,
    missing_texture_queries: RefCell<std::collections::VecDeque<Vec<String>>>,
    tex_catalog_complete: std::cell::Cell<bool>,
    skel_by_path: RefCell<HashMap<i64, Option<crate::pose::Skeleton>>>,
    prefab_mesh_ids: RefCell<HashMap<String, Vec<i64>>>,
    /// mesh path id -> renderer component path ids that draw it, per prefab.
    prefab_mesh_renderers: RefCell<HashMap<String, HashMap<i64, Vec<i64>>>>,
    /// mesh path id -> GameObject names of the renderers that draw it.
    prefab_renderer_names: RefCell<HashMap<String, HashMap<i64, Vec<String>>>>,
    prefab_locals: RefCell<HashMap<String, HashMap<i64, crate::pose::Mat4>>>,
    prefab_moves: RefCell<
        HashMap<String, HashMap<i64, [Option<(crate::pose::Mat4, crate::pose::Mat4)>; 2]>>,
    >,
    /// One object-walk per serialized file. Later prefabs in the same bundle
    /// query this instead of decoding every UnityObject again.
    prefab_graphs: RefCell<Option<Vec<PrefabGraph>>>,
    /// DynamicBone components parsed once per serialized file.
    dbone_sets: RefCell<Option<Vec<crate::dbone::DBoneSet>>>,
    /// OK base library identity (content id) while a library is active, or
    /// the id of a library token. Queries are recorded/replayed under it.
    pub(crate) lib: Option<crate::oklib::Id>,
    tex_directory: RefCell<Option<std::rc::Rc<TexDirectory>>>,
    prefab_directory: RefCell<Option<std::rc::Rc<PrefabDirectory>>>,
    /// Whole-bundle scans that do not depend on the requested prefab, kept so
    /// a bundle with many prefabs is not rescanned per prefab.
    material_maps: RefCell<Option<std::rc::Rc<MaterialMaps>>>,
    state_chains: RefCell<Option<std::rc::Rc<Vec<(i64, Vec<String>)>>>>,
    renderer_kinds: RefCell<Option<std::rc::Rc<HashMap<i64,bool>>>>,
    skel_graphs: RefCell<Option<std::rc::Rc<Vec<crate::pose::SkelGraph>>>>,
}
type ExternalFile = (unity_asset_binary::asset::SerializedFile, Vec<(String, Vec<u8>)>);
/// Mesh path id -> materials, renderer path id -> materials, and for textures materials take from another serialized
/// file (a dependency bundle's material, or a `m_Texture` with `m_FileID` != 0) their lower-case name -> that file.
type MaterialMaps = (HashMap<i64, Vec<MeshMaterial>>, HashMap<i64, Vec<MeshMaterial>>, HashMap<String, String>);

/// Texture directory of a source (OK model data): what name lookups resolve to.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub(crate) struct TexDirectory {
    /// (name, pixel area) in object order; fuzzy lookups rank over it.
    pub catalog: Vec<(String, u32)>,
    /// Every object's metadata was readable (an absent name is proven absent).
    pub complete: bool,
    /// lowercase name -> (name, duplicated name)
    pub names: std::collections::BTreeMap<String, (String, bool)>,
    /// lowercase container alias -> object name
    pub assets: std::collections::BTreeMap<String, String>,
    /// lowercase key -> name of the object an exact lookup resolves to
    pub exact: std::collections::BTreeMap<String, String>,
}

/// Prefab directory of a source (OK model data).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub(crate) struct PrefabDirectory {
    /// Per serialized file: container prefab roots (lowercase), or None when
    /// the file has none and GameObject names are used instead.
    pub roots: Vec<Option<std::collections::BTreeSet<String>>>,
    /// Per serialized file: every GameObject name (lowercase).
    pub objects: Vec<std::collections::BTreeSet<String>>,
    /// Scene-root GameObject names (original case).
    pub top: Vec<String>,
    /// Mesh object names (lowercase).
    pub meshes: std::collections::BTreeSet<String>,
}
impl PrefabDirectory {
    pub(crate) fn contains(&self, name: &str) -> bool {
        let key = name.to_ascii_lowercase();
        self.roots.iter().zip(&self.objects).any(|(roots, objects)| match roots {
            Some(roots) => roots.contains(&key),
            None => objects.contains(&key),
        })
    }
    /// A mesh query for this name can only return nothing.
    pub(crate) fn proves_no_meshes(&self, main_data: &str) -> bool {
        let key = main_data.to_ascii_lowercase();
        let want = key.trim_start_matches("p_");
        !self.objects.iter().any(|o| o.contains(&key))
            && !self.meshes.iter().any(|n| n == want || n.contains(want) || want.contains(n.as_str()))
    }
}

#[derive(Clone,Copy,Debug,serde::Serialize,serde::Deserialize)]
pub(crate) struct TextureSampling {
    pub filter: i32,
    pub wrap_u: i32,
    pub wrap_v: i32,
    pub srgb: bool,
    pub mip_count: i32,
}

#[derive(Clone,Copy)]
struct TextureObjectLocation {
    file: usize,
    path_id: i64,
    color_space: Option<i32>,
    sampling: Option<TextureSampling>,
}

/// The declaration used by Material.HasProperty for a shader loaded from a
/// MaterialEditor mod bundle. None means the named Shader was not readable.
pub fn shader_bundle_declares_property(bytes: &[u8], shader_name: &str, property: &str) -> Option<bool> {
    crate::oklib::memo_bytes(bytes,"shader_declares_property",&(shader_name,property),||shader_bundle_declares_property_unity(bytes,shader_name,property))
}
fn shader_bundle_declares_property_unity(bytes: &[u8], shader_name: &str, property: &str) -> Option<bool> {
    let bundle = load_bundle_unbounded(bytes.to_vec()).ok()?;
    let _ = bundle.data_checked();
    for file in &bundle.files {
        let Ok(fb) = bundle.extract_file_data(file) else { continue; };
        let Ok(serialized) = parse_serialized_file(fb) else { continue; };
        for info in serialized.objects_of_type(48) {
            let Ok(shader) = serialized.scene_object(info) else { continue; };
            if shader_runtime_name(&shader) == shader_name {
                return shader_declares_property(&shader, property);
            }
        }
    }
    None
}

/// Unity Material.HasProperty queries the Shader declaration, not the
/// material's saved float map. A mod material can omit a property when it is
/// still at the shader default, so retain the declaration for runtime rules.
fn shader_declares_property(object: &UnityObject, wanted: &str) -> Option<bool> {
    let parsed = object.class.properties().get("m_ParsedForm")?.as_object()?;
    let prop_info = parsed.get("m_PropInfo")?.as_object()?;
    let props = match prop_info.get("m_Props")? {
        unity_asset_core::UnityValue::Array(props) => props,
        _ => return None,
    };
    Some(props.iter().any(|prop| {
        prop.as_object().and_then(|entry| entry.get("m_Name"))
            .and_then(|name| name.as_str()) == Some(wanted)
    }))
}

/// The shader's own fixed-function state, as Unity draws it: the cull mode of the main colour pass and whether
/// another pass is an inverted-hull outline (culls the faces the main pass keeps). A cull mode bound to a material
/// property (`Cull [_Cull]`) is returned as that property name; the material's saved float then decides it. `zwrite`
/// is the main colour pass's depth write (0 / 1, or a property name), bound the same way.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct ShaderPassState { pub cull: Result<i64, String>, pub outline: bool, pub zwrite: Result<i64, String>, pub alpha_to_mask: bool }

pub(crate) fn shader_pass_state(object: &UnityObject) -> Option<ShaderPassState> {
    let parsed = object.class.properties().get("m_ParsedForm")?;
    let sub = uv_arr(uv_obj(parsed, "m_SubShaders")?).first()?;
    // (cull, pass name, light mode) for each pass that draws (UsePass and GrabPass carry no state of their own).
    let mut passes = Vec::new();
    for pass in uv_arr(uv_obj(sub, "m_Passes")?) {
        if uv_obj(pass, "m_Type").and_then(|v| v.as_i64()).unwrap_or(0) != 0 { continue; }
        let Some(state) = uv_obj(pass, "m_State") else { continue };
        let culling = uv_obj(state, "culling").or_else(|| uv_obj(state, "m_Culling"))?;
        let bound = uv_obj(culling, "name").and_then(|v| v.as_str()).unwrap_or("");
        let cull = if bound.is_empty() || bound == "<noninit>" {
            Ok(uv_obj(culling, "val").and_then(|v| v.as_f64()).unwrap_or(2.0) as i64)
        } else { Err(bound.to_string()) };
        // ZWrite: Unity's default is On.
        let zwrite = match uv_obj(state, "zWrite").or_else(|| uv_obj(state, "m_ZWrite")) {
            Some(z) => match uv_obj(z, "name").and_then(|v| v.as_str()).unwrap_or("") {
                "" | "<noninit>" => Ok(uv_obj(z, "val").and_then(|v| v.as_f64()).unwrap_or(1.0) as i64),
                name => Err(name.to_string()),
            },
            None => Ok(1),
        };
        let name = uv_obj(state, "m_Name").and_then(|v| v.as_str()).unwrap_or("").to_ascii_uppercase();
        let mut light_mode = String::new();
        if let Some(tags) = uv_obj(state, "m_Tags").and_then(|t| uv_obj(t, "tags")) {
            for pair in uv_arr(tags) {
                let a = uv_arr(pair);
                let (k, v) = if a.len() == 2 { (a[0].as_str(), a[1].as_str()) }
                    else { (uv_obj(pair, "first").and_then(|v| v.as_str()), uv_obj(pair, "second").and_then(|v| v.as_str())) };
                if k.is_some_and(|k| k.eq_ignore_ascii_case("LightMode")) { light_mode = v.unwrap_or("").to_ascii_lowercase(); }
            }
        }
        // AlphaToMask On (a fixed value; Studio draws without MSAA, so one sample: alpha below one half is not drawn).
        let to_mask = uv_obj(state, "alphaToMask").or_else(|| uv_obj(state, "m_AlphaToMask"))
            .filter(|a| matches!(uv_obj(a, "name").and_then(|v| v.as_str()).unwrap_or(""), "" | "<noninit>"))
            .and_then(|a| uv_obj(a, "val")).and_then(|v| v.as_f64()).is_some_and(|v| v > 0.5);
        passes.push((cull, name, light_mode, zwrite, to_mask));
    }
    // Shadow, depth and meta passes never draw colour.
    let colour = |(_, name, mode, _, _): &&(Result<i64, String>, String, String, Result<i64, String>, bool)|
        !matches!(mode.as_str(), "shadowcaster" | "shadowcollector" | "meta" | "prepassbase" | "deferred")
            && !name.contains("SHADOW") && !name.contains("META");
    // Additive per-light passes repeat the main pass's geometry state.
    let colour: Vec<_> = passes.iter().filter(colour).filter(|p| p.2 != "forwardadd").collect();
    // An inverted-hull outline is a colour pass named OUTLINE, or a Cull Front pass beside one that draws front faces
    // (Cull Back or Off; Koikatsu draws its outline first, then the surface).
    let draws_front = colour.iter().any(|p| matches!(p.0, Ok(0) | Ok(2)));
    let is_outline = |p: &(Result<i64, String>, String, String, Result<i64, String>, bool)| p.1.contains("OUTLINE") || (draws_front && p.0 == Ok(1));
    let main = colour.iter().position(|p| !is_outline(p)).or((!colour.is_empty()).then_some(0))?;
    let outline = colour.iter().enumerate().any(|(i, p)| i != main && is_outline(p));
    Some(ShaderPassState { cull: colour[main].0.clone(), outline, zwrite: colour[main].3.clone(), alpha_to_mask: colour[main].4 })
}

/// The depth write of a pass state for a material: a value bound to a property takes the material's float (Unity's
/// default, On, when the material has none).
pub(crate) fn resolve_zwrite(zwrite: &Result<i64, String>, floats: &HashMap<String, f32>) -> f32 {
    match zwrite { Ok(v) => (*v != 0) as i32 as f32, Err(name) => floats.get(name).map_or(1.0, |v| (*v > 0.5) as i32 as f32) }
}

/// Material floats carrying the shader's pass state to the renderer: `_OKShaderCull` (0 off, 1 front, 2 back),
/// `_OKShaderOutline` (0/1) and `_OKShaderState`, the 24-bit FNV-1a hash of the shader name they were read from. Later
/// steps replace a material's shader (Material Editor, native shader swaps); the renderer trusts the state only while
/// the hash still names the material's shader. Absent when the shader object is not in the material's file.
pub(crate) fn record_shader_pass_state(floats: &mut HashMap<String, f32>, shader: &str, state: Option<&ShaderPassState>) {
    let Some(state) = state else { return };
    let cull = match &state.cull { Ok(v) => Some(*v as f32), Err(name) => floats.get(name).copied() };
    let Some(cull) = cull else { return };
    floats.insert("_OKShaderCull".into(), cull);
    floats.insert("_OKShaderOutline".into(), if state.outline { 1.0 } else { 0.0 });
    let zwrite = resolve_zwrite(&state.zwrite, floats);
    floats.insert("_OKShaderZWrite".into(), zwrite);
    if state.alpha_to_mask { floats.insert("_OKShaderAlphaToMask".into(), 1.0); } else { floats.remove("_OKShaderAlphaToMask"); }
    floats.insert("_OKShaderState".into(), shader_state_key(shader) as f32);
}

pub(crate) fn shader_state_key(shader: &str) -> u32 {
    shader.bytes().fold(0x811c9dc5u32, |h, b| (h ^ b as u32).wrapping_mul(0x01000193)) & 0xFF_FFFF
}

impl Default for TextureSampling {
    fn default()->Self {Self{filter:1,wrap_u:0,wrap_v:0,srgb:true,mip_count:1}}
}

/// OK base library surface: every Unity query of a bundle is recorded or
/// replayed here (see oklib.rs). Bodies live in the `*_unity` methods.
thread_local! { static PREFAB_ABSENT: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
impl ParsedBundle {
    fn memo<A: serde::Serialize + ?Sized, R: serde::Serialize + serde::de::DeserializeOwned>(&self, method: &str, args: &A, f: impl FnOnce() -> R) -> R {
        // A card pack stores mesh records with texture plans (texture references decoded on use), and decoding a plan
        // asks this bundle's texture directory. Recording reads textures straight from Unity and never needs the
        // directory, so record it with the bundle's first query, or the OK card reopened from OK data alone misses it.
        if method != "texture_directory2" && self.tex_directory.borrow().is_none() && crate::oklib::recording_card_pack() {
            let _ = self.texture_directory();
        }
        crate::oklib::memo(self.lib.as_ref(), method, args, f)
    }
    fn library_model(&self)->bool {self.files.is_empty()&&self.lib.is_some()&&crate::oklib::replaying()}
    /// Editor overlay of this model (OK base library only).
    fn model_edits(&self)->Option<std::sync::Arc<crate::okmodel_edit::ModelEdits>> {
        if !self.library_model(){return None;}
        crate::okmodel_edit::active(self.lib.as_ref()?)
    }
    pub(crate) fn texture_directory(&self)->std::rc::Rc<TexDirectory> {
        if let Some(d)=self.tex_directory.borrow().as_ref(){return d.clone();}
        // v2: duplicated names resolve (an older directory missed them).
        let d=std::rc::Rc::new(self.memo("texture_directory2",&(),||self.texture_directory_unity()));
        *self.tex_directory.borrow_mut()=Some(d.clone());d
    }
    pub(crate) fn prefab_directory(&self)->std::rc::Rc<PrefabDirectory> {
        if let Some(d)=self.prefab_directory.borrow().as_ref(){return d.clone();}
        let d=std::rc::Rc::new(self.memo("prefab_directory",&(),||self.prefab_directory_unity()));
        *self.prefab_directory.borrow_mut()=Some(d.clone());d
    }
    /// Texture queries by name: a name the directory does not know resolves
    /// to nothing in Unity too, so a model answers it without a record.
    fn memo_texture<R:serde::Serialize+serde::de::DeserializeOwned>(&self,query:&str,name:&str,f:impl FnOnce()->Option<R>)->Option<R> {
        let key=name.to_ascii_lowercase();
        if self.library_model()&&!self.texture_directory().exact.contains_key(&key){
            if std::env::var_os("OKLIB_TRACE_SILENT").is_some(){if let Some(s)=crate::oklib::active(){s.note_missing(format!("SILENT {query} {} {name}",self.lib.map(|i|crate::oklib::id_hex(&i)).unwrap_or_default()));}}
            return None;
        }
        // Lookups ignore case: record and answer under the lowercase name.
        self.memo(query,key.as_str(),f)
    }
    /// Prefab-keyed queries: a GameObject name the source does not contain
    /// gets the same answer Unity gives (computed on the empty model).
    fn memo_prefab<A:serde::Serialize+?Sized,R:serde::Serialize+serde::de::DeserializeOwned>(&self,query:&str,prefab:&str,args:&A,f:impl FnOnce()->R)->R {
        if self.library_model() {
            let key=prefab.to_ascii_lowercase();
            if !self.prefab_directory().objects.iter().any(|o|o.contains(&key)) {
                if std::env::var_os("OKLIB_TRACE_SILENT").is_some(){if let Some(s)=crate::oklib::active(){s.note_missing(format!("SILENT {query} {} {prefab}",self.lib.map(|i|crate::oklib::id_hex(&i)).unwrap_or_default()));}}
                // The bundle has no such prefab: `f` finds nothing in the model handle, which is the answer, not a gap
                // of the library (an OK card's strict open would fail on it, okstrict.rs).
                PREFAB_ABSENT.with(|a|a.set(true));
                let out=f();
                PREFAB_ABSENT.with(|a|a.set(false));
                return out;
            }
        }
        self.memo(query,args,f)
    }
    pub(crate) fn exact_texture_png(&self,name:&str)->Option<Vec<u8>> {self.exact_texture_png_unity(name)}
    pub(crate) fn accessory_color_use(&self,prefab_name:&str)->Option<AccessoryColorUse> {self.memo_prefab("accessory_color_use",prefab_name,prefab_name,||self.accessory_color_use_unity(prefab_name))}
    pub(crate) fn contains_prefab(&self,name:&str)->bool {self.prefab_directory().contains(name)}
    pub fn face_runtime_settings(&self,main_data:&str)->[f32;12] {self.memo_prefab("face_runtime_settings",main_data,main_data,||self.face_runtime_settings_unity(main_data))}
    pub(crate) fn accessory_body_binding(&self,prefab:&str)->Option<AccessoryBodyBinding> {
        self.memo_prefab("accessory_body_binding",prefab,&(prefab,self.script_context()),||self.accessory_body_binding_unity(prefab))
    }
    pub fn prefab_has_component_field(&self,prefab:&str,field:&str)->bool {self.memo_prefab("prefab_has_component_field",prefab,&(prefab,field),||self.prefab_has_component_field_unity(prefab,field))}
    /// Transform tree, mesh nodes and lights of a prefab (recorded into the library with the model).
    pub fn prefab_hierarchy(&self,prefab:&str)->crate::pose::PrefabHierarchy {self.memo_prefab("prefab_hierarchy",prefab,prefab,||self.prefab_hierarchy_unity(prefab))}
    /// Baked lightmaps of a prefab's renderers (recorded into the library with the model by `studio-nodes`).
    pub fn prefab_lightmaps(&self,prefab:&str)->PrefabLightmaps {self.memo_prefab("prefab_lightmaps",prefab,prefab,||self.prefab_lightmaps_unity(prefab))}
    pub fn prefab_lightmap_directions(&self,prefab:&str)->PrefabLightmaps {self.memo_prefab("prefab_lightmap_directions",prefab,prefab,||self.prefab_lightmaps_slot_unity(prefab,"m_DirLightmap"))}
    /// Renderer node, Unity shadow casting mode (Off/On/TwoSided/ShadowsOnly), layer and enabled state.
    pub fn prefab_renderer_settings(&self,prefab:&str)->Vec<(i32,i32,i32,bool)> {
        self.memo_prefab("prefab_renderer_settings",prefab,prefab,|| {
            for (gi,graph) in self.transform_graphs().iter().enumerate() {
                let (h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
                if h.nodes.is_empty() {continue;}
                return self.with_serialized(|sers| {
                    let Some(ser)=sers.get(gi) else {return Vec::new();};
                    ser.objects_of_type(23).into_iter().chain(ser.objects_of_type(137)).filter_map(|info| {
                        let obj=ser.scene_object(info).ok()?;
                        let props=obj.class.properties();
                        let go=props.get("m_GameObject").and_then(uv_pptr)?;
                        let node=*go_node.get(&go)?;
                        let layer=ser.objects_of_type(1).into_iter().find(|i|i.path_id==go)
                            .and_then(|i|ser.scene_object(i).ok())
                            .and_then(|o|o.class.properties().get("m_Layer").map(uv_i32)).unwrap_or(0);
                        Some((node,props.get("m_CastShadows").map(uv_i32).unwrap_or(1),layer,
                            props.get("m_Enabled").map(uv_bool).unwrap_or(true)))
                    }).collect()
                });
            }
            Vec::new()
        })
    }
    /// Cameras rendering into RenderTextures and the materials sampling them (recorded by `studio-nodes`).
    pub fn prefab_render_textures(&self,prefab:&str)->PrefabRenderTextures {
        // v2 (target keys, shared screens); a library recorded before has v1 records (a map's own cameras and screens).
        if self.library_model() && !crate::oklib::has_record(self.lib.as_ref(),"prefab_render_textures2",prefab)
            && crate::oklib::has_record(self.lib.as_ref(),"prefab_render_textures",prefab) {
            return self.memo_prefab("prefab_render_textures",prefab,prefab,PrefabRenderTexturesV1::default).into();
        }
        self.memo_prefab("prefab_render_textures2",prefab,prefab,||self.prefab_render_textures_unity(prefab))
    }
    /// A map's SunLightInfo: times of day, what each shows, the sun (recorded by `studio-nodes`).
    pub fn prefab_sun_light(&self,prefab:&str)->PrefabSunLight {self.memo_prefab("prefab_sun_light",prefab,prefab,||self.prefab_sun_light_unity(prefab))}
    /// Baked reflection probes of a prefab and the scene's skybox reflection (recorded by `studio-nodes`).
    pub fn prefab_reflection_probes(&self,prefab:&str)->PrefabReflectionProbes {self.memo_prefab("prefab_reflection_probes",prefab,prefab,||self.prefab_reflection_probes_unity(prefab))}
    /// Converter-only read, independent of model/session memo formats.
    /// Lossless scene reflection payloads, identified by serialized file and probe GameObject.
    /// GameObject zero denotes RenderSettings' default environment.
    pub fn scene_reflection_cubes(&self)->Result<Vec<(usize,i64,SceneReflectionCube)>,String>{
        self.serialized_files(|sers| {
            let names=self.serialized_names.borrow();
            let key=|name:&str|name.replace('\\',"/").rsplit('/').next().unwrap_or(name).to_ascii_lowercase();
            let read=|from:usize,value:Option<&unity_asset_core::UnityValue>,intensity:f32|->Result<Option<SceneReflectionCube>,String>{
                let Some(ptr)=value.and_then(references::PPtr::read) else{return Ok(None);};
                let target=if ptr.file==0{from}else{
                    let external=sers[from].externals.get(ptr.file as usize-1).ok_or("reflection external file index invalid")?;
                    let matches:Vec<_>=names.iter().enumerate().filter(|(_,n)|key(n)==key(&external.path)).map(|(i,_)|i).collect();
                    if matches.len()!=1{return Err("reflection external file absent or ambiguous".into());} matches[0]
                };
                let target=sers.get(target).ok_or("reflection file absent")?;
                let info=target.objects_of_type(89).into_iter().find(|i|i.path_id==ptr.path).ok_or("reflection cubemap absent")?;
                let cube=target.scene_object(info).map_err(|e|e.to_string())?;
                let texture=read_texture_payload(&cube,&self.files,None).ok_or("reflection payload absent")?;
                use unity_asset_decode::texture::TextureFormat;
                let format=match texture.format{TextureFormat::BC6H=>95,TextureFormat::RGBAHalf=>10,other=>return Err(format!("unsupported lossless reflection format {other:?}"))};
                let size=texture.width as u32;let mips=texture.mip_count as u32;
                if !intensity.is_finite()||intensity<0.{return Err("invalid reflection intensity".into());}
                if size<4||size>4096||!size.is_power_of_two()||texture.height as u32!=size||mips==0||mips>size.ilog2()+1{return Err("invalid reflection dimensions/mips".into());}
                let face_bytes:usize=(0..mips).map(|m|{let w=(size>>m).max(1) as usize;if format==95{w.div_ceil(4).pow(2)*16}else{w*w*8}}).sum();
                if texture.image_data.len()!=face_bytes*6{return Err("reflection payload length invalid".into());}
                Ok(Some(SceneReflectionCube{name:cube.class.properties().get("m_Name").and_then(|v|v.as_str()).unwrap_or("").into(),size,mips,dxgi_format:format,bytes:texture.image_data,intensity}))
            };
            let mut out=Vec::new();
            for(from,ser) in sers.iter().enumerate(){
                for info in ser.objects_of_type(104){
                    let object=ser.scene_object(info).map_err(|e|e.to_string())?;let p=object.class.properties();
                    if let Some(cube)=read(from,p.get("m_GeneratedSkyboxReflection"),p.get("m_ReflectionIntensity").map(uv_f32).unwrap_or(1.))?{out.push((from,0,cube));}
                }
                for info in ser.objects_of_type(215){
                    let object=ser.scene_object(info).map_err(|e|e.to_string())?;let p=object.class.properties();
                    if !p.get("m_Enabled").map(uv_bool).unwrap_or(true){continue;}
                    let Some(go)=p.get("m_GameObject").and_then(references::PPtr::read) else{continue;};
                    if go.file!=0{return Err("reflection probe GameObject is external".into());}
                    let baked=p.get("m_CustomBakedTexture").filter(|v|references::PPtr::read(v).is_some()).or_else(||p.get("m_BakedTexture"));
                    if let Some(cube)=read(from,baked,p.get("m_IntensityMultiplier").map(uv_f32).unwrap_or(1.))?{out.push((from,go.path,cube));}
                }
            }
            Ok(out)
        })
    }
    pub fn scene_default_reflection_cube(&self)->Result<Option<SceneReflectionCube>,String>{
        let mut defaults=self.scene_reflection_cubes()?.into_iter().filter(|(_,go,_)|*go==0);
        let first=defaults.next().map(|(_,_,cube)|cube);
        if defaults.next().is_some(){return Err("ambiguous scene default reflection".into());}Ok(first)
    }
    pub fn prefab_lossless_reflections(&self,prefab:&str)->Result<Vec<(i32,SceneReflectionCube)>,String>{
        let all=self.scene_reflection_cubes()?;
        for(gi,graph) in self.transform_graphs().iter().enumerate(){
            let(h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);if h.nodes.is_empty(){continue;}
            return Ok(all.into_iter().filter_map(|(file,go,cube)|{
                if file!=gi{return None;}if go==0{Some((-1,cube))}else{go_node.get(&go).map(|node|(*node as i32,cube))}
            }).collect());
        }
        Ok(Vec::new())
    }
    /// Material values of a prefab's settled Animators (recorded by `studio-nodes`).
    pub fn prefab_animated_materials(&self,prefab:&str)->PrefabAnimatedMaterials {self.memo_prefab("prefab_animated_materials",prefab,prefab,||self.prefab_animated_materials_unity(prefab))}
    /// How each Light of a prefab (`prefab_hierarchy` lights, same order) takes part at runtime: its baking mode
    /// (m_Lightmapping: 4 realtime, 1 mixed, 2 baked into the lightmaps), culling mask (the layers it lights), shadow type
    /// and whether its GameObject is active (a time-of-day list root counts as active: SunLightInfo shows it at its time).
    /// Recorded by `studio-nodes`.
    pub fn prefab_light_modes(&self, prefab: &str) -> Vec<PrefabLightMode> {
        self.memo_prefab("prefab_light_modes", prefab, prefab, || {
            for (gi, graph) in self.transform_graphs().iter().enumerate() {
                let (h, go_node) = crate::pose::prefab_hierarchy_in(graph, prefab);
                if h.nodes.is_empty() { continue; }
                let listed: std::collections::HashSet<i32> = self.prefab_sun_light(prefab).infos.iter().flat_map(|i| i.visible.clone()).collect();
                return self.with_serialized(|sers| {
                    let Some(ser) = sers.get(gi) else { return Vec::new(); };
                    let mut active = vec![true; h.nodes.len()];
                    for info in ser.objects_of_type(1) {
                        if let (Some(&node), Ok(o)) = (go_node.get(&info.path_id), ser.scene_object(info)) {
                            active[node as usize] = o.class.properties().get("m_IsActive").map(uv_bool).unwrap_or(true) || listed.contains(&(node as i32));
                        }
                    }
                    let mut out = Vec::new();
                    for info in ser.objects_of_type(108) {
                        let Ok(obj) = ser.scene_object(info) else { continue };
                        let p = obj.class.properties();
                        let Some(node) = p.get("m_GameObject").and_then(uv_pptr).and_then(|go| go_node.get(&go)).copied() else { continue };
                        let mut on = true;
                        let mut n = node as i32;
                        while n >= 0 { on &= active[n as usize]; n = h.nodes[n as usize].parent; }
                        out.push(PrefabLightMode { node: node as i32, lightmapping: p.get("m_Lightmapping").map(uv_i32).unwrap_or(4),
                            culling_mask: p.get("m_CullingMask").and_then(|m| uv_obj(m, "m_Bits")).and_then(|v| v.as_i64()).map(|v| v as u32).unwrap_or(u32::MAX),
                            shadows: p.get("m_Shadows").and_then(|s| uv_obj(s, "m_Type")).map(uv_i32).unwrap_or(0), active: on });
                    }
                    out
                });
            }
            Vec::new()
        })
    }
    /// Per-renderer-instance activeInHierarchy, including static activeSelf and settled Animator curves.
    /// SunLightInfo list roots remain eligible and are gated separately by the selected time of day.
    pub fn prefab_renderer_visibility(&self, prefab: &str) -> Vec<(i32, bool)> {
        self.memo_prefab("prefab_renderer_visibility", prefab, prefab, || {
            for (gi, graph) in self.transform_graphs().iter().enumerate() {
                let (h, go_node) = crate::pose::prefab_hierarchy_in(graph, prefab);
                if h.nodes.is_empty() { continue; }
                return self.with_serialized(|sers| {
                    let Some(ser) = sers.get(gi) else { return Vec::new(); };
                    let names = self.serialized_names.borrow();
                    let file_key = |name: &str| name.replace('\\', "/").rsplit('/').next().unwrap_or(name).to_ascii_lowercase();
                    let resolve = |from: usize, value: &unity_asset_core::UnityValue, class: i32| {
                        let ptr = references::PPtr::read(value)?;
                        let file = if ptr.file == 0 { from } else {
                            let path = &sers.get(from)?.externals.get(ptr.file as usize - 1)?.path;
                            let key = file_key(path); names.iter().position(|n| file_key(n) == key)?
                        };
                        let ser = sers.get(file)?;
                        let info = ser.objects_of_type(class).into_iter().find(|i| i.path_id == ptr.path)?;
                        Some((file, ser.scene_object(info).ok()?))
                    };
                    let mut active = vec![true; h.nodes.len()];
                    for info in ser.objects_of_type(1) {
                        if let Some(&node) = go_node.get(&info.path_id) {
                            if let Ok(o) = ser.scene_object(info) {
                                active[node as usize] = o.class.properties().get("m_IsActive").map(uv_bool).unwrap_or(true);
                            }
                        }
                    }
                    // Source SunLightInfo.Set changes these activeSelf flags for the chosen time.
                    // Keep each listed root eligible here; _OKSunVisible gates it in the renderer.
                    for info in self.prefab_sun_light(prefab).infos {
                        for node in info.visible { if let Some(v) = active.get_mut(node as usize) { *v = true; } }
                    }
                    let mut overrides = HashMap::new();
                    for info in ser.objects_of_type(95) {
                        let Ok(a) = ser.scene_object(info) else { continue; };
                        let p = a.class.properties();
                        if !p.get("m_Enabled").map(uv_bool).unwrap_or(true) { continue; }
                        let Some(&anim_node) = p.get("m_GameObject").and_then(uv_pptr).and_then(|id| go_node.get(&id)) else { continue; };
                        if prefab_active_in_hierarchy(&h, &active, &overrides, anim_node) != Some(true) { continue; }
                        let Some((ctrl_file, ctrl)) = p.get("m_Controller").and_then(|v| resolve(gi, v, 91)) else { continue; };
                        let below: HashMap<u32, usize> = (0..h.nodes.len()).filter_map(|node| {
                            let mut names = Vec::new(); let mut n = node;
                            while n != anim_node as usize {
                                names.push(h.nodes[n].name.as_str());
                                let parent = h.nodes[n].parent; if parent < 0 { return None; } n = parent as usize;
                            }
                            names.reverse(); Some((crate::anim::unity_string_to_hash(&names.join("/")) as u32, node))
                        }).collect();
                        for clip in crate::anim::controller_steady_clips(&ctrl) {
                            let Some((_, c)) = resolve(ctrl_file, &clip, 74) else { continue; };
                            for (path, attr, ty, values) in crate::anim::generic_clip_values(&c, 0.).0 {
                                if ty == 1 && attr == crate::anim::unity_string_to_hash("m_IsActive") as u32 {
                                    if let (Some(&node), Some(&value)) = (below.get(&path), values.first()) {
                                        overrides.insert(node, value >= 0.5);
                                    }
                                }
                            }
                        }
                    }
                    h.mesh_nodes.iter().filter_map(|(_, node)| {
                        prefab_active_in_hierarchy(&h, &active, &overrides, *node).map(|v| (*node, v))
                    }).collect()
                });
            }
            Vec::new()
        })
    }
    pub(crate) fn studio_pv_config(&self,prefab:&str)->Option<crate::pv::Config> {let _spot=crate::profile::spot("studio_pv_config");self.memo_prefab("studio_pv_config",prefab,prefab,||self.studio_pv_config_unity(prefab))}
    pub(crate) fn eye_look_config(&self,prefab:&str)->Option<crate::eye_look::Config> {let _spot=crate::profile::spot("eye_look_config");self.memo_prefab("eye_look_config",prefab,prefab,||self.eye_look_config_unity(prefab))}
    pub fn eye_material_settings(&self,main_data:&str)->HashMap<String,[f32;14]> {self.memo_prefab("eye_material_settings",main_data,main_data,||self.eye_material_settings_unity(main_data))}
    pub fn face_control_patterns(&self,main_data:&str,controller:&str)->HashMap<String,(usize,Vec<(i32,i32)>)> {
        self.memo_prefab("face_control_patterns",main_data,&(main_data,controller),||self.face_control_patterns_unity(main_data,controller))
    }
    pub fn renderer_material_slot(&self,renderer_ids:&[i64],slot:usize)->Option<MeshMaterial> {
        self.renderer_material_slot_unity(renderer_ids,slot)
    }
    pub fn material_named(&self,wanted:&str)->Option<MeshMaterial> {
        let mut mat=self.memo("material_named",wanted,||self.material_named_unity(wanted));
        if let (Some(e),Some(m))=(self.model_edits(),mat.as_mut()){crate::okmodel_edit::apply_material(&e,m);}
        mat
    }
    pub fn texture_png_named(&self,name:&str)->Option<Vec<u8>> {self.texture_png_named_unity(name)}
    pub(crate) fn texture_gpu_named(&self,name:&str)->Option<crate::clothes_gpu::Texture> {
        if let (Some(e),Some(id))=(self.model_edits(),self.lib) {
            if let Some(png)=crate::okmodel_edit::texture_png(&crate::oklib::active()?.dir,&id,&e,name) {
                // A replaced texture is RGBA8 texels (sRGB unless the original was linear).
                let rgba=crate::teximg::load_from_memory(&png).ok()?.into_rgba8();
                let sampling=self.texture_sampling_named(name).unwrap_or_default();
                return Some(crate::clothes_gpu::Texture{width:rgba.width(),height:rgba.height(),format:if sampling.srgb{29}else{28},mips:1,
                    sampling:TextureSampling{mip_count:1,..sampling},aniso:1,bias:0.0,data:rgba.into_raw()});
            }
        }
        self.memo_texture("texture_gpu_named",name,||self.texture_gpu_named_unity(name))
    }
    pub(crate) fn shader_pixel_dxbc_named(&self,name:&str)->Option<Vec<u8>> {self.memo("shader_pixel_dxbc_named",name,||self.shader_pixel_dxbc_named_unity(name))}
    pub(crate) fn texture_sampling_named(&self,name:&str)->Option<TextureSampling> {
        // Replay's local-texture directory intentionally excludes external
        // names. Follow the recorded dependency outside that local guard,
        // just as decode_tex does for the image itself.
        self.memo_texture("texture_sampling_named",name,||self.texture_sampling_named_unity(name))
            .or_else(|| {
                // A missing local sampler is not evidence of an external
                // texture. Older cards deliberately omit queries for names
                // proven absent by their texture directory.
                if self.library_model() && !crate::oklib::has_record(self.lib.as_ref(),"dependency_texture_model",name.to_ascii_lowercase().as_str()) {return None;}
                self.dependency_texture_bundle(name)?.texture_sampling_named(name)
            })
    }
    pub fn skeleton(&self,path_id:i64)->Option<crate::pose::Skeleton> {self.memo("skeleton",&path_id,||self.skeleton_unity(path_id))}
    pub fn skeleton_full_named(&self,probe:&str)->Option<crate::pose::Skeleton> {self.memo("skeleton_full_named",probe,||self.skeleton_full_named_unity(probe))}
    pub fn skeleton_for_prefab_name(&self,prefab_name:&str)->Option<crate::pose::Skeleton> {let _spot=crate::profile::spot("skeleton_for_prefab_name");self.memo_prefab("skeleton_for_prefab_name",prefab_name,prefab_name,||self.skeleton_for_prefab_name_unity(prefab_name))}
    pub fn textures_keep_base(&self)->Vec<(String,Vec<u8>)> {let _spot=crate::profile::spot("textures_keep_base");
        // A library model's answer cannot change while the library replays: restoring its packed textures from the
        // store again for every card (the base head, ~0.35 s) is kept once per process.
        type Memo=std::sync::Mutex<std::collections::HashMap<crate::oklib::Id,std::sync::Arc<Vec<(String,Vec<u8>)>>>>;
        static MEMO:std::sync::OnceLock<Memo>=std::sync::OnceLock::new();
        // (not a model of an OK card's own third-party mods: its answer is the card's, okstrict.rs)
        let replay=self.lib.filter(|id|crate::oklib::active().is_some_and(|s|s.mode==crate::oklib::Mode::Replay)
            &&(crate::okstrict::active().is_none()||crate::okstrict::library_model(id)));
        if let Some(id)=replay {
            if let Some(hit)=MEMO.get_or_init(Default::default).lock().unwrap_or_else(|e|e.into_inner()).get(&id).cloned(){return (*hit).clone();}
        }
        let out=crate::oklib::memo_packed(self.lib.as_ref(),"textures_keep_base",&(),||self.textures_keep_base_unity(),
            |l|{let mut l=l.clone();self.strip_named_textures(&mut l);l},|l|self.restore_named_textures(l));
        if let Some(id)=replay {
            let mut memo=MEMO.get_or_init(Default::default).lock().unwrap_or_else(|e|e.into_inner());
            if memo.len()>=64{memo.clear();}
            memo.insert(id,std::sync::Arc::new(out.clone()));
        }
        out
    }
    pub fn decode_all(&self)->(Vec<ExtractedMesh>,Vec<(String,Vec<u8>)>,Option<crate::pose::Skeleton>) {
        crate::oklib::memo_packed(self.lib.as_ref(),"decode_all",&(),||self.decode_all_unity(),
            |(m,t,s)|{let (mut m,mut t)=(m.clone(),t.clone());self.strip_mesh_textures(&mut m);self.strip_named_textures(&mut t);(m,t,s.clone())},
            |(m,t,_)|{self.restore_mesh_textures(m);self.restore_named_textures(t);})
    }
    /// Skeleton-input queries only resolve the skeleton's transform ids, names
    /// and hierarchy; recorded under that topology (a conflict is reported if
    /// a result ever depends on the card-shaped transforms).
    pub fn body_dynamics_for_skeleton(&self,skel:&crate::pose::Skeleton)->crate::dbone::ver02::BodyDynamicSet {
        self.memo("skel:body_dynamics",&(skeleton_topology(skel),self.script_context()),||self.body_dynamics_for_skeleton_unity(skel))
    }
    pub(crate) fn body_colliders_for_skeleton(&self,skel:&crate::pose::Skeleton)->crate::dbone::BodyColliderSet {
        self.memo("skel:body_colliders",&(skeleton_topology(skel),self.script_context()),||self.body_colliders_for_skeleton_unity(skel))
    }
    pub(crate) fn bone_implants_for_skeleton(&self,skel:&crate::pose::Skeleton)->Vec<crate::dbone::implant::Marker> {
        self.memo("skel:bone_implants",&(skeleton_topology(skel),self.script_context()),||self.bone_implants_for_skeleton_unity(skel))
    }
}

/// Card-independent identity of a skeleton: transform ids, names, parents.
pub(crate) fn skeleton_topology(skel:&crate::pose::Skeleton)->[u8;16] {
    if !crate::oklib::active().is_some() {return [0;16];}
    let mut nodes:Vec<(i64,&str,Option<i64>,Option<u32>)>=skel.tfs.iter().map(|(id,tf)|(*id,tf.name.as_str(),tf.parent,tf.sibling_index)).collect();
    nodes.sort();
    let bytes=bincode::serialize(&(nodes,&skel.bone_ids,&skel.names,skel.smr_count,skel.source_mesh)).unwrap_or_default();
    crate::oklib::hash16(&bytes)
}

#[derive(Clone,Copy,Hash,PartialEq,Eq)]
enum BundleCacheKey{Bytes(u64),Source(u64)}
thread_local! {
    static BUNDLE_CACHE: RefCell<HashMap<BundleCacheKey, Arc<ParsedBundle>>> = RefCell::new(HashMap::new());
    static BUNDLE_SOURCE_SERIAL: std::cell::Cell<u64> = const {std::cell::Cell::new(0)};
    // Only an index into BUNDLE_CACHE: no second owner retaining decoded
    // bundles after the existing bounded parser cache evicts them.
    static BUNDLE_PATH_INDEX: RefCell<HashMap<std::path::PathBuf,(u64,std::time::SystemTime,BundleCacheKey)>> = RefCell::new(HashMap::new());
    static BUNDLE_ZIP_INDEX: RefCell<HashMap<(std::path::PathBuf,String),(u64,std::time::SystemTime,BundleCacheKey)>> = RefCell::new(HashMap::new());
}
/// The parsed-bundle caches are per thread (the parser's state is not Send). `clear_bundle_cache` must still reach
/// every thread: an OK card pack records only queries that run, so a worker that kept a bundle parsed during an
/// earlier open (rayon pool) would answer from its cache and leave the pack without that record. Each thread clears
/// its own caches the next time it uses them after the generation moved.
static BUNDLE_CACHE_GENERATION: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
thread_local! { static BUNDLE_CACHE_SEEN: std::cell::Cell<u64> = const { std::cell::Cell::new(0) }; }
// The old main-UV scan also tried offset+8 inside interleaved records; set only by the scan that lists what it changed.
thread_local! { pub(crate) static UV_LEGACY_PADDING: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
// The old rule without the tiled-UV fallback in `decode_uvs` (for listing the sources it changes).
thread_local! { pub(crate) static UV_NO_TILED_FALLBACK: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
fn sync_bundle_cache() {
    let current = BUNDLE_CACHE_GENERATION.load(std::sync::atomic::Ordering::Acquire);
    if BUNDLE_CACHE_SEEN.with(|seen| seen.replace(current)) != current {
        BUNDLE_CACHE.with(|c| c.borrow_mut().clear());
        BUNDLE_PATH_INDEX.with(|c| c.borrow_mut().clear());
        BUNDLE_ZIP_INDEX.with(|c| c.borrow_mut().clear());
        references::clear_cache();
        // The per-entry mesh and accessory-source memos answer from the same bundles (C069: an accessory source picked
        // during the game-mode open skipped `contains_prefab` while its pack was recorded).
        crate::clear_entry_memo();
    }
}
/// Drops the bundles this thread parsed (a prefetch helper once its items are handed over: the asking thread never
/// uses them, and each helper used to keep up to its cache limit of parsed bundles until the card ended).
pub(crate) fn release_thread_bundle_cache() {
    BUNDLE_CACHE.with(|c| c.borrow_mut().clear());
    BUNDLE_PATH_INDEX.with(|c| c.borrow_mut().clear());
    BUNDLE_ZIP_INDEX.with(|c| c.borrow_mut().clear());
}
fn fresh_source_key()->BundleCacheKey {
    BUNDLE_SOURCE_SERIAL.with(|serial|{let value=serial.get().checked_add(1).expect("bundle source generation exhausted");serial.set(value);BundleCacheKey::Source(value)})
}

fn content_key(bytes: &[u8]) -> u64 {
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    bytes.len().hash(&mut h);
    let n = bytes.len();
    if n == 0 {
        return h.finish();
    }
    let chunk = 128.min(n);
    h.write(&bytes[..chunk]);
    if n > chunk {
        h.write(&bytes[n - chunk..]);
    }
    for k in [n / 4, n / 2, (3 * n) / 4] {
        if k + 32 <= n {
            h.write(&bytes[k..k + 32]);
        }
    }
    h.finish()
}

// File views retain the compressed container. UnityFS resource payloads are
// decompressed only for the requested stream range; legacy formats use the
// loader's existing extraction path. No guessed object offsets are used.
#[derive(Clone)]
pub struct BundleFileBytes {
    bundle: Arc<unity_asset_binary::bundle::AssetBundle>,
    file: unity_asset_binary::bundle::BundleFileInfo,
}
trait StreamBytes {
    fn read_range(&self,offset:u64,size:u32)->Option<Vec<u8>>;
}
impl StreamBytes for Vec<u8> {
    fn read_range(&self,offset:u64,size:u32)->Option<Vec<u8>> {try_slice(self,offset,size)}
}
impl StreamBytes for BundleFileBytes {
    fn read_range(&self,offset:u64,size:u32)->Option<Vec<u8>> {
        if offset.checked_add(size as u64)?>self.file.size{return None;}
        let absolute=self.file.offset.checked_add(offset)?;
        let range=unity_asset_binary::bundle::BundleFileInfo::new(self.file.name.clone(),absolute,size as u64);
        self.bundle.extract_file_data(&range).ok()
    }
}
pub(crate) fn parse_shared_serialized(bytes: BundleFileBytes)
    -> unity_asset_binary::error::Result<unity_asset_binary::asset::SerializedFile> {
    // Resource streams are never SerializedFiles. Do not copy/decompress their
    // entire payload merely to discover that their header is not an asset.
    let name=bytes.file.name.to_ascii_lowercase();
    if name.ends_with(".ress")||name.ends_with(".resource") {
        return Err(unity_asset_binary::error::BinaryError::invalid_data("resource stream is not a serialized file"));
    }
    // Header and metadata only; object payloads are decompressed from the bundle's blocks when an object is read.
    // A whole file held in memory was up to 1.35 GB for one texture pack (and every UnityObject read from it kept it).
    if let Some(file)=parse_serialized_metadata(&bytes) {return Ok(file);}
    let data=bytes.bundle.extract_file_data(&bytes.file)?;
    parse_serialized_file(data)
}

struct BundleObjects(BundleFileBytes);
impl unity_asset_binary::asset::ObjectSource for BundleObjects {
    fn read(&self,offset:u64,size:usize)->Option<Vec<u8>> {self.0.read_range(offset,u32::try_from(size).ok()?)}
}
fn parse_serialized_metadata(bytes:&BundleFileBytes)->Option<unity_asset_binary::asset::SerializedFile> {
    use unity_asset_binary::{asset::{SerializedFileHeader,SerializedFileParser},reader::{BinaryReader,ByteOrder}};
    let prefix=bytes.read_range(0,(bytes.file.size.min(48)) as u32)?;
    let header=SerializedFileHeader::from_reader(&mut BinaryReader::new(&prefix,ByteOrder::Big)).ok()?;
    if header.version<9||!header.is_valid()||header.file_size!=bytes.file.size||header.data_offset>bytes.file.size{return None;}
    let metadata=bytes.read_range(0,u32::try_from(header.data_offset).ok()?)?;
    SerializedFileParser::from_metadata_with_source(metadata,bytes.file.size,Arc::new(BundleObjects(bytes.clone()))).ok()
}

pub fn clear_bundle_cache() {
    // Every thread, not only this one (see sync_bundle_cache).
    BUNDLE_CACHE_GENERATION.fetch_add(1, std::sync::atomic::Ordering::AcqRel);
    sync_bundle_cache();
}

/// Between independent card opens of one process: `clear_bundle_cache`, and every pool thread drops its parsed
/// bundles and entry memos now instead of at its next use. An idle worker otherwise keeps what it parsed for an
/// earlier card (tens of GB over a batch of cards). Call outside pool work, with no open in progress.
pub fn release_bundle_caches() {
    clear_bundle_cache();
    crate::clear_mask_bakes();
    let release = || { sync_bundle_cache(); crate::mods::clear_zip_bundle_cache(); };
    release();
    rayon::broadcast(|_| release());
    crate::session::native_workers_broadcast(release);
    return_free_heap();
}

/// Gives the process heap's free blocks back to the system (Windows keeps freed segments committed otherwise, so a
/// batch of cards grew by the fragments each one left).
fn return_free_heap() {
    #[cfg(windows)]
    {
        extern "system" {
            fn GetProcessHeap() -> isize;
            fn HeapCompact(heap: isize, flags: u32) -> usize;
        }
        unsafe { HeapCompact(GetProcessHeap(), 0); }
    }
}

pub fn parsed_if_cached(bytes: &[u8]) -> Option<Arc<ParsedBundle>> {
    sync_bundle_cache();
    let key = BundleCacheKey::Bytes(content_key(bytes));
    BUNDLE_CACHE.with(|c| c.borrow().get(&key).cloned())
}

fn cache_put(key: BundleCacheKey, v: Arc<ParsedBundle>) {
    sync_bundle_cache();
    BUNDLE_CACHE.with(|c| {
        let mut m = c.borrow_mut();
        // A conversion opens its card 42 times (every outfit in six garment states) over 60??50 bundles: at 24 the
        // cache emptied itself in every open, which parsed them all again and lost their decoded textures
        // (raiden_shogun 41 ??32 s, jeanne 15.7 ??13.7 s, the same peak memory). Its worker drops the cache after the
        // card (release_bundle_caches). Other opens keep the small cache: what it holds after an open stays held.
        if m.len() >= if crate::oklib::recording_card_pack() { 160 } else { 24 } {
            m.clear();
        }
        m.insert(key, v);
    });
}

pub fn parse_bundle_ref<B: BundleInput + ?Sized>(bytes: &B) -> Arc<ParsedBundle> {let _spot=crate::profile::spot("parse_bundle_ref");
    let key = BundleCacheKey::Bytes(content_key(bytes.bundle_bytes()));
    if let Some(p) = parsed_if_cached(bytes.bundle_bytes()) {
        return p;
    }
    let parsed = Arc::new(parse_bundle_uncached(bytes.to_source()));
    cache_put(key, parsed.clone());
    parsed
}

pub fn parse_bundle_cached(bytes: impl Into<SourceBytes>) -> Arc<ParsedBundle> {
    let bytes = bytes.into();
    sync_bundle_cache();
    let key=BundleCacheKey::Bytes(content_key(&bytes));
    if let Some(parsed)=BUNDLE_CACHE.with(|c|c.borrow().get(&key).cloned()){return parsed;}
    let parsed=Arc::new(parse_bundle_uncached(bytes));
    cache_put(key,parsed.clone());parsed
}

/// Stable installed sources should not be re-read for each candidate name or
/// renderer. Validate the canonical file's size/mtime on every call; a missing
/// file or cache eviction never returns a stale parsed bundle.
pub(crate) fn parse_bundle_path_cached(path:&Path)->Option<Arc<ParsedBundle>> {
    sync_bundle_cache();
    let path=crate::gamefs::canonicalize(path).ok()?;
    let metadata=crate::gamefs::metadata(&path).ok()?;
    let modified=metadata.modified().ok();
    if let Some(modified)=modified {
        let hit=BUNDLE_PATH_INDEX.with(|index|index.borrow().get(&path).copied());
        if let Some((_,_,key))=hit.filter(|(length,time,_)|*length==metadata.len()&&*time==modified) {
            if let Some(parsed)=BUNDLE_CACHE.with(|cache|cache.borrow().get(&key).cloned()){return Some(parsed);}
        }
    }
    let bytes=crate::gamefs::read_source(&path).ok()?;
    // A file identity must not enter the sampled byte cache: two different
    // bundles (or a same-size replacement) can share all sampled locations.
    // The index contract already validates canonical path, length and mtime.
    // Give every actual reload a distinct owned generation. The path index
    // alone reuses a validated live parser; neither sampled byte identity nor
    // an unnecessary full-bundle hash can alias an earlier source generation.
    let key=fresh_source_key();
    let parsed=Arc::new(parse_bundle_uncached(bytes.into()));cache_put(key,parsed.clone());
    if let Some(modified)=modified {
        BUNDLE_PATH_INDEX.with(|index| {
            let mut index=index.borrow_mut();if index.len()>=256{index.clear();}
            index.insert(path,(metadata.len(),modified,key));
        });
    }
    Some(parsed)
}

pub(crate) fn parse_bundle_zip_cached(path:&Path,asset:&str)->Option<Arc<ParsedBundle>> {
    sync_bundle_cache();
    let path=crate::gamefs::canonicalize(path).ok()?;
    let metadata=crate::gamefs::metadata(&path).ok()?;
    let modified=metadata.modified().ok();
    let identity=(path.clone(),asset.replace('\\',"/").to_ascii_lowercase());
    if let Some(modified)=modified {
        let hit=BUNDLE_ZIP_INDEX.with(|index|index.borrow().get(&identity).copied());
        if let Some((_,_,key))=hit.filter(|(length,time,_)|*length==metadata.len()&&*time==modified) {
            if let Some(parsed)=BUNDLE_CACHE.with(|cache|cache.borrow().get(&key).cloned()){return Some(parsed);}
        }
    }
    let bytes=crate::mods::zipmod_bundle_named(&path,asset)?;
    // Archive path/entry/mtime validation owns reuse. A reload always receives
    // a new generation, even when its bytes match legacy sampled locations.
    let key=fresh_source_key();
    let parsed=Arc::new(parse_bundle_uncached(bytes.into()));cache_put(key,parsed.clone());
    if let Some(modified)=modified {
        BUNDLE_ZIP_INDEX.with(|index|{let mut index=index.borrow_mut();if index.len()>=256{index.clear();}
            index.insert(identity,(metadata.len(),modified,key));});
    }
    Some(parsed)
}

// Callers with a full-content cache key must not reuse the sampled-content
// parser cache when a different bundle happens to share those samples.
pub(crate) fn parse_bundle_fresh(bytes: Vec<u8>) -> ParsedBundle {
    parse_bundle_uncached(bytes.into())
}

pub(crate) fn parse_bundle_uncached(bytes: SourceBytes) -> ParsedBundle {let _spot=crate::profile::spot("parse_bundle_uncached");
    let profile=std::env::var_os("OK_APPEARANCE_PROFILE").is_some();
    let started=std::time::Instant::now();
    let source_len=bytes.len();
    let lib=crate::oklib::bundle_id(&bytes);
    let token=crate::oklib::token_id(&bytes).is_some();
    let empty = || ParsedBundle {
        files: Vec::new(),
        serialized: RefCell::new(None),
        serialized_names: RefCell::new(Vec::new()),
        serialized_file_index: RefCell::new(Vec::new()),
        script_root: RefCell::new(None),
        script_origin: RefCell::new(None),
        script_maps: RefCell::new(None),
        meshes: RefCell::new(HashMap::new()),
        external_files: RefCell::new(HashMap::new()),
        external_textures: RefCell::new(HashMap::new()),
        dependency_texture_source: RefCell::new(HashMap::new()),
        textures: RefCell::new(HashMap::new()),
        texture_bytes: std::cell::Cell::new(0),
        tex_names: RefCell::new(Vec::new()),
        tex_objects: RefCell::new(None),
        tex_name_objects: RefCell::new(None),
        tex_exact_objects: RefCell::new(HashMap::new()),
        tex_asset_objects: RefCell::new(None),
        missing_texture_queries: RefCell::new(std::collections::VecDeque::new()),
        tex_catalog_complete: std::cell::Cell::new(false),
        skel_by_path: RefCell::new(HashMap::new()),
        prefab_mesh_ids: RefCell::new(HashMap::new()),
        prefab_mesh_renderers: RefCell::new(HashMap::new()),
        prefab_renderer_names: RefCell::new(HashMap::new()),
        prefab_locals: RefCell::new(HashMap::new()),
        prefab_moves: RefCell::new(HashMap::new()),
        prefab_graphs: RefCell::new(None),
        dbone_sets: RefCell::new(None),
        lib,
        tex_directory: RefCell::new(None),
        prefab_directory: RefCell::new(None),
        material_maps: RefCell::new(None),
        state_chains: RefCell::new(None),
        renderer_kinds: RefCell::new(None),
        skel_graphs: RefCell::new(None),
    };
    // A library token has no Unity data: every query is answered by the library.
    if token { return empty(); }
    let Ok(bundle) = load_bundle_source(bytes, false) else {
        return empty();
    };
    let loaded=started.elapsed();
    let decompressed=loaded;
    let bundle=Arc::new(bundle);
    // The vendor lazy loader exposes directory nodes but deliberately does not
    // populate `files` until eager parse_files. Build file views from the nodes.
    let files=bundle.nodes.iter().map(|node|{
        let file=unity_asset_binary::bundle::BundleFileInfo::new(node.name.clone(),node.offset,node.size);
        (node.name.clone(),BundleFileBytes{bundle:bundle.clone(),file})
    }).collect();
    if profile { eprintln!("APPEARANCE_PROFILE phase=bundle_parse bytes={} load_ms={:.3} decompress_ms={:.3} extract_ms={:.3}",source_len,loaded.as_secs_f64()*1000.,(decompressed-loaded).as_secs_f64()*1000.,(started.elapsed()-decompressed).as_secs_f64()*1000.); }
    ParsedBundle {
        files,
        serialized: RefCell::new(None),
        serialized_names: RefCell::new(Vec::new()),
        serialized_file_index: RefCell::new(Vec::new()),
        script_root: RefCell::new(None),
        script_origin: RefCell::new(None),
        script_maps: RefCell::new(None),
        meshes: RefCell::new(HashMap::new()),
        external_files: RefCell::new(HashMap::new()),
        external_textures: RefCell::new(HashMap::new()),
        dependency_texture_source: RefCell::new(HashMap::new()),
        textures: RefCell::new(HashMap::new()),
        texture_bytes: std::cell::Cell::new(0),
        tex_names: RefCell::new(Vec::new()),
        tex_objects: RefCell::new(None),
        tex_name_objects: RefCell::new(None),
        tex_exact_objects: RefCell::new(HashMap::new()),
        tex_asset_objects: RefCell::new(None),
        missing_texture_queries: RefCell::new(std::collections::VecDeque::new()),
        tex_catalog_complete: std::cell::Cell::new(false),
        skel_by_path: RefCell::new(HashMap::new()),
        prefab_mesh_ids: RefCell::new(HashMap::new()),
        prefab_mesh_renderers: RefCell::new(HashMap::new()),
        prefab_renderer_names: RefCell::new(HashMap::new()),
        prefab_locals: RefCell::new(HashMap::new()),
        prefab_moves: RefCell::new(HashMap::new()),
        prefab_graphs: RefCell::new(None),
        dbone_sets: RefCell::new(None),
        lib,
        tex_directory: RefCell::new(None),
        prefab_directory: RefCell::new(None),
        material_maps: RefCell::new(None),
        state_chains: RefCell::new(None),
        renderer_kinds: RefCell::new(None),
        skel_graphs: RefCell::new(None),
    }
}

impl ParsedBundle {
    pub(crate) fn prepare_texture_directory(&self){self.ensure_tex_catalog();}
    fn exact_texture_png_unity(&self,name:&str)->Option<Vec<u8>> {let _spot=crate::profile::spot("exact_texture_png");
        if name.is_empty(){return None;}
        self.decode_tex(name)
    }
    /// Sideloader overrides assets by name, not the whole bundle. A patch
    /// bundle may only contain a subset of the vanilla bundle's prefabs.
    /// Same result as `accessory_color_use` over this bundle's cached files.
    fn accessory_color_use_unity(&self, prefab_name: &str) -> Option<AccessoryColorUse> {
        self.with_serialized(|files| accessory_color_use_in(files, prefab_name))
    }
    fn contains_prefab_unity(&self, name: &str) -> bool {
        self.with_serialized(|files| files.iter().any(|file| {
            let roots = prefab_asset_roots(file);
            if !roots.is_empty() { return roots.contains_key(&name.to_ascii_lowercase()); }
            file.objects_of_type(1).into_iter().any(|info|
                file.scene_object(info).ok()
                    .and_then(|obj| obj.name()).is_some_and(|n| n.eq_ignore_ascii_case(name)))
        }))
    }
    /// Authored FaceBlendShape/Blink settings on the selected head root.
    /// Frequency, speed, brow sync, enabled (card replaces it), eye/brow min,
    /// eye/brow FixedRate. Defaults come from the decompiled field initializers.
    fn face_runtime_settings_unity(&self, main_data: &str) -> [f32; 12] {
        let mut settings=[30.,0.15,1.,1.,0.,0.,-0.1,-0.1,0.1,0.3,0.1,0.];
        self.with_serialized(|sers| {
            for ser in sers {
                let names: HashMap<_,_> = ser.objects_of_type(1).into_iter().filter_map(|info|
                    ser.scene_object(info).ok().map(|obj|
                        (info.path_id,obj.name().unwrap_or_default()))).collect();
                for info in ser.objects_of_type(114) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let props=obj.class.properties();
                    if !props.get("m_GameObject").and_then(uv_pptr).and_then(|id|names.get(&id))
                        .is_some_and(|name|name.eq_ignore_ascii_case(main_data)) {continue;}
                    if !props.contains_key("EyesCtrl") {continue;}
                    let number=|root:&str,key:&str,default:f32| props.get(root).and_then(|v|uv_obj(v,key))
                        .and_then(|v|v.as_f64().or_else(||v.as_i64().map(|n|n as f64)))
                        .map(|n|n as f32).unwrap_or(default);
                    settings[0]=number("BlinkCtrl","BlinkFrequency",30.);
                    settings[1]=number("BlinkCtrl","BaseSpeed",0.15);
                    settings[2]=props.get("EyebrowCtrl").and_then(|v|uv_obj(v,"SyncBlink"))
                        .map(|v|if uv_bool(v){1.}else{0.}).unwrap_or(1.);
                    for (index,root,key) in [(4,"EyesCtrl","OpenMin"),(5,"EyebrowCtrl","OpenMin"),
                        (6,"EyesCtrl","FixedRate"),(7,"EyebrowCtrl","FixedRate")] {
                        settings[index]=number(root,key,settings[index]);
                    }
                    for (i,key) in [(8,"EyeLookUpCorrect"),(9,"EyeLookDownCorrect"),(10,"EyeLookSideCorrect")] {
                        settings[i]=props.get(key).and_then(|v|v.as_f64()).map(|n|n as f32).unwrap_or(settings[i]);
                    }
                }
            }
        });
        settings
    }

    /// Match GetComponentsInChildren(includeInactive:true) on the actual rig.
    fn accessory_body_binding_unity(&self,prefab:&str)->Option<AccessoryBodyBinding> {
        self.with_scripts(|files,scripts| {
            for (file,scripts) in files.iter().zip(scripts) {
                let mut gos=HashMap::new();let mut nodes=HashMap::new();let mut by_go=HashMap::new();
                for info in file.objects_of_type(1) {
                    if let Ok(obj)=file.scene_object(info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}
                }
                for info in file.objects_of_type(4) {
                    if let Ok(obj)=file.scene_object(info) {
                        let p=obj.class.properties();
                        if let Some(go)=p.get("m_GameObject").and_then(references::PPtr::local) {
                            nodes.insert(info.path_id,(go,p.get("m_Father").and_then(references::PPtr::local).unwrap_or(0)));by_go.insert(go,info.path_id);
                        }
                    }
                }
                let objects:Vec<_>=file.objects_of_type(114).into_iter().filter_map(|info|file.scene_object(&info).ok()).collect();
                for object in &objects {
                    let p=object.class.properties();
                    if scripts.class(p.get("m_Script"))!=Some("ChaAccessoryClothes") {continue;}
                    let Some(go)=p.get("m_GameObject").and_then(references::PPtr::local) else {continue};
                    let mut id=by_go.get(&go).copied().unwrap_or(0);let mut belongs=false;
                    for _ in 0..nodes.len() {
                        let Some((node,parent))=nodes.get(&id) else {break};
                        if gos.get(node).is_some_and(|name|name.eq_ignore_ascii_case(prefab)) {belongs=true;break;}id=*parent;
                    }
                    if !belongs {continue;}
                    let root=p.get("ArmatureRoot").and_then(references::PPtr::local)?;
                    for component in &objects {
                        let q=component.class.properties();
                        if q.get("m_GameObject").and_then(references::PPtr::local)!=Some(go)
                            || scripts.class(q.get("m_Script"))!=Some("ChaAccessoryComponent") {continue;}
                        let renderers=q.get("rendNormal")?.as_array()?.iter().filter_map(references::PPtr::local).collect();
                        return Some(AccessoryBodyBinding{renderers,armature_root:root});
                    }
                }
            }
            None
        })
    }

    fn prefab_hierarchy_unity(&self, prefab:&str)->crate::pose::PrefabHierarchy {
        let graphs=self.transform_graphs();
        for (gi,graph) in graphs.iter().enumerate() {
            let (mut h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
            if h.nodes.is_empty() {continue;}
            h.lights=self.with_serialized(|sers| {
                let Some(ser)=sers.get(gi) else {return Vec::new();};
                let mut lights=Vec::new();
                for info in ser.objects_of_type(108) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let props=obj.class.properties();
                    let Some(node)=props.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied() else {continue;};
                    let f=|k:&str|props.get(k).map(uv_f32).unwrap_or(0.);
                    let color=props.get("m_Color").map(|c|[uv_obj(c,"r").map(uv_f32).unwrap_or(1.),uv_obj(c,"g").map(uv_f32).unwrap_or(1.),uv_obj(c,"b").map(uv_f32).unwrap_or(1.),uv_obj(c,"a").map(uv_f32).unwrap_or(1.)]).unwrap_or([1.;4]);
                    lights.push(crate::pose::PrefabLight{node,kind:props.get("m_Type").map(uv_i32).unwrap_or(2),color,intensity:f("m_Intensity"),range:f("m_Range"),spot_angle:f("m_SpotAngle"),enabled:props.get("m_Enabled").map(uv_bool).unwrap_or(true)});
                }
                lights
            });
            return h;
        }
        Default::default()
    }
    /// Baked lighting of a prefab (a Studio map scene): each MeshRenderer's lightmap (index, scale-offset) by hierarchy
    /// node (`prefab_hierarchy` numbering), and the scene's lightmap textures (LightmapSettings.m_Lightmaps, RGBM PNG).
    fn prefab_lightmaps_unity(&self, prefab:&str)->PrefabLightmaps {
        self.prefab_lightmaps_slot_unity(prefab, "m_Lightmap")
    }

    fn prefab_lightmaps_slot_unity(&self, prefab:&str, slot:&str)->PrefabLightmaps {
        let graphs=self.transform_graphs();
        for (gi,graph) in graphs.iter().enumerate() {
            let (h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
            if h.nodes.is_empty() {continue;}
            let (renderers,names)=self.with_serialized(|sers| {
                let Some(ser)=sers.get(gi) else {return (Vec::new(),Vec::new());};
                let vec4=|v:&unity_asset_core::UnityValue|["x","y","z","w"].map(|k|uv_obj(v,k).map(uv_f32).unwrap_or(0.));
                // What the map's own script puts on each renderer when it loads (Illusion's `lightmapParameters`:
                // renderer, lightmapIndex, scaleOffset), by renderer path id; it wins over the renderer's serialized values.
                let mut restored=HashMap::new();
                for info in ser.objects_of_type(114) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let Some(unity_asset_core::UnityValue::Array(list))=obj.class.properties().get("lightmapParameters").cloned() else {continue;};
                    for e in &list {
                        let (Some(r),Some(i),Some(st))=(uv_obj(e,"renderer").and_then(uv_pptr),uv_obj(e,"lightmapIndex").map(uv_i32),uv_obj(e,"scaleOffset").map(vec4)) else {continue;};
                        restored.insert(r,(i,st));
                    }
                }
                let mut renderers=Vec::new();
                for info in ser.objects_of_type(23) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let props=obj.class.properties();
                    let Some(node)=props.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied() else {continue;};
                    // Unity 2019 serializes the atlas placement as m_LightmapTilingOffset (m_LightmapScaleOffset in the API
                    // and other versions); reading only the latter left every renderer on the whole lightmap.
                    let serialized=(props.get("m_LightmapIndex").map(uv_i32).unwrap_or(-1),
                        props.get("m_LightmapTilingOffset").or_else(||props.get("m_LightmapScaleOffset")).map(vec4).unwrap_or([1.,1.,0.,0.]));
                    let (index,st)=restored.get(&info.path_id).copied().unwrap_or(serialized);
                    // 0xFFFF: not lightmapped; 0xFFFE: realtime GI only.
                    if !(0..0xFFFE).contains(&index) {continue;}
                    renderers.push((node,index,st));
                }
                // The lightmaps live in the scene's shared assets (another file of the bundle): found by path id and type.
                let mut names=Vec::new();
                for info in ser.objects_of_type(157) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    if slot == "m_DirLightmap" && obj.class.properties().get("m_LightmapsMode").map(uv_i32) != Some(1) { continue; }
                    let Some(unity_asset_core::UnityValue::Array(maps))=obj.class.properties().get("m_Lightmaps").cloned() else {continue;};
                    for m in &maps {
                        let id=uv_obj(m,slot).and_then(uv_pptr).unwrap_or(0);
                        let name=sers.iter().find_map(|s| s.objects_of_type(28).into_iter().find(|i|i.path_id==id)
                            .and_then(|i|s.scene_object(i).ok()).and_then(|o|o.name())).unwrap_or_default();
                        names.push(name);
                    }
                }
                (renderers,names)
            });
            if renderers.is_empty() {return Default::default();}
            let textures=names.iter().map(|n| if n.is_empty() {Vec::new()} else {self.decode_tex(n).unwrap_or_default()}).collect();
            return PrefabLightmaps{renderers,textures};
        }
        Default::default()
    }
    /// Baked reflection probes of a prefab (a Studio map scene): every enabled ReflectionProbe (class 215) of the
    /// prefab's scene file with its baked cubemap, and the scene's skybox reflection (RenderSettings
    /// m_GeneratedSkyboxReflection, node -1). The cubemaps live in the scene's shared assets (another file).
    fn prefab_reflection_probes_unity(&self, prefab:&str)->PrefabReflectionProbes {
        let graphs=self.transform_graphs();
        for (gi,graph) in graphs.iter().enumerate() {
            let (h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
            if h.nodes.is_empty() {continue;}
            return self.with_serialized(|sers| {
                let Some(ser)=sers.get(gi) else {return Default::default();};
                let cubemap=|v:&unity_asset_core::UnityValue|->Option<(u32,u32,Vec<u8>)> {
                    let id=uv_pptr(v).filter(|id|*id!=0)?;
                    let object=sers.iter().find_map(|s|s.objects_of_type(89).into_iter().find(|i|i.path_id==id).and_then(|i|s.scene_object(i).ok()))?;
                    cubemap_atlas(&read_texture_payload(&object,&self.files,None)?)
                };
                let vec3=|v:Option<&unity_asset_core::UnityValue>,d:f32|v.map(|v|["x","y","z"].map(|k|uv_obj(v,k).map(uv_f32).unwrap_or(d))).unwrap_or([d;3]);
                let mut probes=Vec::new();
                for info in ser.objects_of_type(215) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let p=obj.class.properties();
                    let Some(node)=p.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied() else {continue;};
                    if !p.get("m_Enabled").map(uv_bool).unwrap_or(true) {continue;}
                    let baked=p.get("m_CustomBakedTexture").filter(|v|uv_pptr(v).is_some_and(|id|id!=0)).or_else(||p.get("m_BakedTexture"));
                    let Some((size,mips,atlas))=baked.and_then(cubemap) else {continue;};
                    probes.push(PrefabReflectionProbe{node,box_size:vec3(p.get("m_BoxSize"),10.),box_offset:vec3(p.get("m_BoxOffset"),0.),
                        importance:p.get("m_Importance").map(uv_i32).unwrap_or(1),intensity:p.get("m_IntensityMultiplier").map(uv_f32).unwrap_or(1.),size,mips,atlas});
                }
                for info in ser.objects_of_type(104) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let p=obj.class.properties();
                    if let Some((size,mips,atlas))=p.get("m_GeneratedSkyboxReflection").and_then(cubemap) {
                        probes.push(PrefabReflectionProbe{node:-1,box_size:[0.;3],box_offset:[0.;3],importance:0,intensity:p.get("m_ReflectionIntensity").map(uv_f32).unwrap_or(1.),size,mips,atlas});
                    }
                }
                PrefabReflectionProbes{probes}
            });
        }
        Default::default()
    }
    /// Renderer material curves of a prefab's Animators at their settled state (see `PrefabAnimatedMaterials`): each
    /// enabled Animator under the prefab, its controller's steady clips at t = 0, the bindings on MeshRenderer (23) and
    /// SkinnedMeshRenderer (137) whose path (CRC of the path under the Animator's GameObject) names a node.
    fn prefab_animated_materials_unity(&self, prefab:&str)->PrefabAnimatedMaterials {
        let graphs=self.transform_graphs();
        for (gi,graph) in graphs.iter().enumerate() {
            let (h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
            if h.nodes.is_empty() {continue;}
            return self.with_serialized(|sers| {
                let Some(ser)=sers.get(gi) else {return Default::default();};
                let find=|class:i32,id:i64| sers.iter().find_map(|s|s.objects_of_type(class).into_iter().find(|i|i.path_id==id).and_then(|i|s.scene_object(i).ok()));
                // A node's path below an ancestor (Unity binding paths are relative to the Animator's GameObject).
                let path_of=|anc:usize,node:usize|->Option<String> {
                    let mut names=Vec::new();
                    let mut n=node;
                    while n!=anc { names.push(h.nodes.get(n)?.name.clone()); let p=h.nodes[n].parent; if p<0 {return None;} n=p as usize; }
                    names.reverse();
                    Some(names.join("/"))
                };
                let mut values=Vec::new();
                for info in ser.objects_of_type(95) {
                    let Ok(a)=ser.scene_object(info) else {continue;};
                    let p=a.class.properties();
                    if !p.get("m_Enabled").map(uv_bool).unwrap_or(true) {continue;}
                    let Some(anim_node)=p.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied() else {continue;};
                    let Some(ctrl)=p.get("m_Controller").and_then(uv_pptr).and_then(|id|find(91,id)) else {continue;};
                    let below:HashMap<u32,usize>=(0..h.nodes.len()).filter_map(|n|path_of(anim_node as usize,n).map(|s|(crate::anim::unity_string_to_hash(&s) as u32,n))).collect();
                    for clip in crate::anim::controller_steady_clips(&ctrl) {
                        let Some(c)=uv_pptr(&clip).and_then(|id|find(74,id)) else {continue;};
                        // The clip's first frame: Studio loads the map with its Animators entering their default states,
                        // so a material curve that changes later (the live stage's volume-light cones stay at 0 brightness
                        // for most of vl_center_intensity) shows its start. GameObject visibility is evaluated separately
                        // by prefab_renderer_visibility and inherited by descendant renderer instances.
                        let (v0,_)=crate::anim::generic_clip_values(&c,0.);
                        for (path,attr,ty,v) in v0 {
                            if ty!=23&&ty!=137 {continue;}
                            let (Some(&node),Some(&x))=(below.get(&path),v.first()) else {continue;};
                            values.push((node as i32,attr&0x0FFF_FFFF,(attr>>28) as u8,x));
                        }
                    }
                }
                PrefabAnimatedMaterials{values}
            });
        }
        Default::default()
    }
    /// A Studio map's SunLightInfo (the MonoBehaviour with `_infos` and `_targetLight`, on the map root): the sun Light
    /// and each time of day's shown GameObjects and sun. Nodes are `prefab_hierarchy` numbering (inactive ones too).
    fn prefab_sun_light_unity(&self, prefab:&str)->PrefabSunLight {
        let graphs=self.transform_graphs();
        for (gi,graph) in graphs.iter().enumerate() {
            let (h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
            if h.nodes.is_empty() {continue;}
            return self.with_serialized(|sers| {
                let Some(ser)=sers.get(gi) else {return Default::default();};
                for info in ser.objects_of_type(114) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let p=obj.class.properties();
                    let (Some(unity_asset_core::UnityValue::Array(list)),Some(target))=(p.get("_infos"),p.get("_targetLight")) else {continue;};
                    if !p.get("m_GameObject").and_then(uv_pptr).is_some_and(|go|go_node.contains_key(&go)) {continue;}
                    let mut sun=PrefabSunLight{light_node:-1,light_type:1,culling_mask:u32::MAX,lightmapping:4,infos:Vec::new()};
                    let light_id=uv_pptr(target).unwrap_or(0);
                    if let Some(l)=ser.objects_of_type(108).into_iter().find(|i|i.path_id==light_id).and_then(|i|ser.scene_object(i).ok()) {
                        let lp=l.class.properties();
                        sun.light_node=lp.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied().unwrap_or(-1);
                        sun.light_type=lp.get("m_Type").map(uv_i32).unwrap_or(1);
                        sun.lightmapping=lp.get("m_Lightmapping").map(uv_i32).unwrap_or(4);
                        sun.culling_mask=lp.get("m_CullingMask").and_then(|m|uv_obj(m,"m_Bits")).and_then(|v|v.as_i64()).map(|v|v as u32).unwrap_or(u32::MAX);
                    }
                    let v3=|v:Option<&unity_asset_core::UnityValue>|v.map(|v|["x","y","z"].map(|k|uv_obj(v,k).map(uv_f32).unwrap_or(0.))).unwrap_or([0.;3]);
                    for i in list {
                        let visible=uv_obj(i,"visibleList").map(uv_arr).unwrap_or(&[]).iter().filter_map(uv_pptr).filter_map(|go|go_node.get(&go).copied()).collect();
                        let color=uv_obj(i,"color").map(|c|["r","g","b","a"].map(|k|uv_obj(c,k).map(uv_f32).unwrap_or(1.))).unwrap_or([1.;4]);
                        sun.infos.push(PrefabSunInfo{kind:uv_obj(i,"type").map(uv_i32).unwrap_or(0),visible,angle:v3(uv_obj(i,"angle")),color,
                            intensity:uv_obj(i,"intensity").map(uv_f32).unwrap_or(1.)});
                    }
                    return sun;
                }
                Default::default()
            });
        }
        Default::default()
    }
    /// Render-to-texture of a prefab (a Studio map scene, e.g. the live stage's big screens): every Camera whose
    /// m_TargetTexture is a RenderTexture, and every MeshRenderer/SkinnedMeshRenderer material slot sampling one of
    /// those textures. Nodes are `prefab_hierarchy` numbering.
    fn prefab_render_textures_unity(&self, prefab:&str)->PrefabRenderTextures {
        use unity_asset_binary::asset::SerializedFile;
        let graphs=self.transform_graphs();
        for (gi,graph) in graphs.iter().enumerate() {
            let (h,go_node)=crate::pose::prefab_hierarchy_in(graph,prefab);
            if h.nodes.is_empty() {continue;}
            return self.with_serialized(|sers| {
                let Some(ser)=sers.get(gi) else {return Default::default();};
                // A PPtr written in file `from` with m_FileID 0 is looked up in that file, another file's by path id in the
                // bundle's other files (a scene's materials and RenderTextures live in its shared assets). Returns the
                // object and the file holding it (its own PPtrs are relative to that file).
                let object=|v:&unity_asset_core::UnityValue,class:i32,from:usize|->Option<(std::sync::Arc<UnityObject>,usize)> {
                    let id=uv_pptr(v).filter(|id|*id!=0)?;
                    let find=|s:&SerializedFile| s.objects_of_type(class).into_iter().find(|i|i.path_id==id).and_then(|i|s.scene_object(i).ok());
                    if uv_obj(v,"m_FileID").map(uv_i32).unwrap_or(0)==0 {find(sers.get(from)?).map(|o|(o,from))}
                    else {sers.iter().enumerate().filter(|(i,_)|*i!=from).find_map(|(i,s)|find(s).map(|o|(o,i)))}
                };
                let render_texture=|v:&unity_asset_core::UnityValue,from:usize|->Option<(String,i32,i32,u32)> {
                    let (o,_)=object(v,84,from)?;
                    let p=o.class.properties();
                    let name=o.name().unwrap_or_default();
                    let key=render_texture_key(&name,uv_pptr(v).unwrap_or(0));
                    Some((name,p.get("m_Width").map(uv_i32).unwrap_or(0),p.get("m_Height").map(uv_i32).unwrap_or(0),key))
                };
                let mut cameras=Vec::new();
                for info in ser.objects_of_type(20) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let p=obj.class.properties();
                    let go=p.get("m_GameObject").and_then(uv_pptr).unwrap_or(0);
                    let Some(node)=go_node.get(&go).copied() else {continue;};
                    let Some((target,width,height,target_key))=p.get("m_TargetTexture").and_then(|t|render_texture(t,gi)) else {continue;};
                    let f=|k:&str,d:f32|p.get(k).map(uv_f32).unwrap_or(d);
                    let active=ser.objects_of_type(1).into_iter().find(|i|i.path_id==go).and_then(|i|ser.scene_object(i).ok())
                        .and_then(|o|o.class.properties().get("m_IsActive").map(uv_bool)).unwrap_or(true);
                    let background=p.get("m_BackGroundColor").map(|c|["r","g","b","a"].map(|k|uv_obj(c,k).map(uv_f32).unwrap_or(0.))).unwrap_or([0.,0.,0.,1.]);
                    let culling_mask=p.get("m_CullingMask").and_then(|m|uv_obj(m,"m_Bits")).and_then(|v|v.as_i64()).map(|v|v as u32).unwrap_or(u32::MAX);
                    cameras.push(PrefabRenderCamera{node,enabled:p.get("m_Enabled").map(uv_bool).unwrap_or(true),active,
                        fov:f("field of view",60.),near:f("near clip plane",0.3),far:f("far clip plane",1000.),
                        orthographic:p.get("orthographic").map(uv_bool).unwrap_or(false),ortho_size:f("orthographic size",5.),
                        culling_mask,clear_flags:p.get("m_ClearFlags").map(uv_i32).unwrap_or(1),background,target,width,height,target_key});
                }
                let (mut users,mut shared)=(Vec::new(),Vec::new());
                for class in [23,137] { for info in ser.objects_of_type(class) {
                    let Ok(obj)=ser.scene_object(info) else {continue;};
                    let p=obj.class.properties();
                    let Some(node)=p.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied() else {continue;};
                    for m in p.get("m_Materials").map(uv_arr).unwrap_or(&[]) {
                        let Some((mat,file))=object(m,21,gi) else {continue;};
                        let name=mat.name().unwrap_or_default();
                        let Some(saved)=mat.class.properties().get("m_SavedProperties").cloned() else {continue;};
                        for env in uv_obj(&saved,"m_TexEnvs").map(uv_arr).unwrap_or(&[]) {
                            let unity_asset_core::UnityValue::Array(pair)=env else {continue;};
                            let [key,value]=pair.as_slice() else {continue;};
                            let Some(tex)=uv_obj(value,"m_Texture") else {continue;};
                            let Some((target,width,height,target_key))=render_texture(tex,file) else {continue;};
                            if let Some(c)=cameras.iter().position(|c:&PrefabRenderCamera|c.target==target) {
                                let user=(node,name.clone(),key.as_str().unwrap_or("").to_owned(),c as i32);
                                if !users.contains(&user) {users.push(user);}
                            } else {
                                let user=(node,name.clone(),key.as_str().unwrap_or("").to_owned(),target_key,width,height);
                                if !shared.contains(&user) {shared.push(user);}
                            }
                        }
                    }
                }}
                PrefabRenderTextures{cameras,users,shared}
            });
        }
        Default::default()
    }
    fn prefab_has_component_field_unity(&self, prefab:&str, field:&str)->bool {
        self.with_serialized(|sers| sers.iter().any(|ser| {
            let mut gos=HashMap::new();let mut nodes=HashMap::new();let mut by_go=HashMap::new();
            for info in ser.objects_of_type(1) {
                if let Ok(obj)=ser.scene_object(info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}
            }
            for info in ser.objects_of_type(4) {
                if let Ok(obj)=ser.scene_object(info) {
                    let props=obj.class.properties();
                    if let Some(go)=props.get("m_GameObject").and_then(uv_pptr) {
                        nodes.insert(info.path_id,(go,props.get("m_Father").and_then(uv_pptr).unwrap_or(0)));
                        by_go.insert(go,info.path_id);
                    }
                }
            }
            ser.objects_of_type(114).into_iter().any(|info| {
                let Ok(obj)=ser.scene_object(info) else{return false;};
                let props=obj.class.properties();
                if !props.contains_key(field) {return false;}
                let mut id=props.get("m_GameObject").and_then(uv_pptr).and_then(|go|by_go.get(&go)).copied().unwrap_or(0);
                for _ in 0..nodes.len() {
                    let Some(&(go,parent))=nodes.get(&id) else{return false;};
                    if gos.get(&go).is_some_and(|name|name.eq_ignore_ascii_case(prefab)) {return true;}
                    id=parent;
                }
                false
            })
        }))
    }

    /// Preparation._pvCopy owns these GameObject PPtrs. Never use Reset names.
    fn studio_pv_config_unity(&self,prefab:&str)->Option<crate::pv::Config> {
        self.with_serialized(|sers| {
            for ser in sers {
                let mut gos=HashMap::new();let mut nodes=HashMap::new();let mut by_go=HashMap::new();let mut scripts=HashMap::new();
                for info in ser.objects_of_type(1) {if let Ok(obj)=ser.scene_object(info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}}
                for info in ser.objects_of_type(4) {if let Ok(obj)=ser.scene_object(info) {
                    let p=obj.class.properties();if let Some(go)=p.get("m_GameObject").and_then(uv_pptr) {
                        nodes.insert(info.path_id,(go,p.get("m_Father").and_then(uv_pptr).unwrap_or(0)));by_go.insert(go,info.path_id);
                    }
                }}
                for info in ser.objects_of_type(114) {if let Ok(obj)=ser.scene_object(info) {scripts.insert(info.path_id,obj.class.properties().clone());}}
                for preparation in scripts.values() {
                    let go=preparation.get("m_GameObject").and_then(references::PPtr::local).unwrap_or(0);
                    if gos.get(&go).is_none_or(|n|n!=prefab) {continue;}
                    let Some(copy)=preparation.get("_pvCopy").and_then(references::PPtr::local).and_then(|id|scripts.get(&id)) else {continue;};
                    let root=*by_go.get(&go)?;
                    let path=|v:&unity_asset_core::UnityValue|->Option<String> {
                        // Both arrays contain GameObject references, not Transform references.
                        if uv_obj(v,"m_FileID").map(uv_i32).unwrap_or(0)!=0 {return None;}
                        let mut id=*by_go.get(&uv_pptr(v)?)?;let mut names=Vec::new();
                        for _ in 0..nodes.len() {
                            if id==root {names.reverse();return Some(names.join("/"));}
                            let &(go,parent)=nodes.get(&id)?;names.push(gos.get(&go)?.clone());id=parent;
                        }
                        None
                    };
                    let pv=copy.get("pv").map(uv_arr)?;let bone=copy.get("bone").map(uv_arr)?;
                    if pv.len()!=4||bone.len()!=4 {return None;}
                    let pairs=pv.iter().zip(bone).map(|(p,b)|Some([path(p)?,path(b)?])).collect::<Option<Vec<_>>>()?;
                    return Some(crate::pv::Config{pairs});
                }
            }
            None
        })
    }

    /// Resolve EyeLookController's actual calculation component under this rig.
    fn eye_look_config_unity(&self,prefab:&str)->Option<crate::eye_look::Config> {
        use crate::eye_look::{Config,EyeRef,Pattern};
        self.with_serialized(|sers| {
            for ser in sers {
                let mut gos=HashMap::new();let mut nodes=HashMap::new();let mut by_go=HashMap::new();
                let mut scripts=HashMap::new();
                for info in ser.objects_of_type(1) {if let Ok(obj)=ser.scene_object(info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}}
                for info in ser.objects_of_type(4) {if let Ok(obj)=ser.scene_object(info) {
                    let p=obj.class.properties();if let Some(go)=p.get("m_GameObject").and_then(uv_pptr) {
                        nodes.insert(info.path_id,(go,p.get("m_Father").and_then(uv_pptr).unwrap_or(0)));by_go.insert(go,info.path_id);
                    }
                }}
                for info in ser.objects_of_type(114) {if let Ok(obj)=ser.scene_object(info) {scripts.insert(info.path_id,obj.class.properties().clone());}}
                for controller in scripts.values() {
                    let Some(calc)=controller.get("eyeLookScript").and_then(uv_pptr).and_then(|id|scripts.get(&id)) else{continue;};
                    let mut id=controller.get("m_GameObject").and_then(uv_pptr).and_then(|id|by_go.get(&id)).copied().unwrap_or(0);
                    let mut under=false;
                    for _ in 0..nodes.len() {let Some(&(go,parent))=nodes.get(&id) else{break;};if gos.get(&go).is_some_and(|n|n==prefab) {under=true;break;}id=parent;}
                    if !under {continue;}
                    let name=|v:&unity_asset_core::UnityValue|uv_pptr(v).and_then(|id|nodes.get(&id)).and_then(|(go,_)|gos.get(go)).cloned();
                    let field=|key:&str|calc.get(key).map(uv_f32).unwrap_or(0.);
                    let vec=|key:&str| {let v=calc.get(key);["x","y","z"].map(|k|v.and_then(|v|uv_obj(v,k)).map(uv_f32).unwrap_or(0.))};
                    let eyes:Vec<_>=calc.get("eyeObjs").map(uv_arr).unwrap_or(&[]).iter().filter_map(|v|Some(EyeRef {
                        name:name(uv_obj(v,"eyeTransform")?)?,lr:uv_obj(v,"eyeLR").map(uv_i32).unwrap_or(0)
                    })).collect();
                    let patterns:Vec<_>=calc.get("eyeTypeStates").map(uv_arr).unwrap_or(&[]).iter().map(|v|Pattern {
                        kind:uv_obj(v,"lookType").map(uv_i32).unwrap_or(0),values:["thresholdAngleDifference","bendingMultiplier","maxAngleDifference",
                            "minBendingAngle","maxBendingAngle","upBendingAngle","downBendingAngle","nearDis","leapSpeed","forntTagDis","hAngleLimit","vAngleLimit"]
                            .map(|key|uv_obj(v,key).map(uv_f32).unwrap_or(0.))
                    }).collect();
                    if eyes.len()!=2||patterns.is_empty() {continue;}
                    let root=calc.get("rootNode").and_then(name).or_else(||calc.get("m_GameObject").and_then(uv_pptr).and_then(|go|gos.get(&go)).cloned())?;
                    return Some(Config {root,center:calc.get("trfCenter").and_then(name).unwrap_or_default(),eyes,patterns,init:None,
                        look:vec("headLookVector"),up:vec("headUpVector"),correct:calc.get("correct").map(uv_bool).unwrap_or(false),
                        separation:field("centerEyeLength"),away:field("sorasiRate"),control_distance:field("targetObjMaxDir")});
                }
            }
            None
        })
    }

    /// EyeLookMaterialControll fields, matched through its renderer reference.
    fn eye_material_settings_unity(&self,main_data:&str)->HashMap<String,[f32;14]> {
        let (meshes,_)=self.meshes_for_maindata(main_data);
        let renderer_mesh:HashMap<_,_>=meshes.iter().flat_map(|mesh|mesh.renderer_ids.iter().map(|id|(*id,mesh.name.to_ascii_lowercase()))).collect();
        self.with_serialized(|sers| {
            let mut result=HashMap::new();
            for ser in sers {
            let mut go_renderers=HashMap::new();
            for info in ser.objects_of_type(137) {
                if !renderer_mesh.contains_key(&info.path_id) {continue;}
                if let Ok(obj)=ser.scene_object(info) {
                    if let Some(go)=obj.class.properties().get("m_GameObject").and_then(uv_pptr) {
                        go_renderers.insert(go,info.path_id);
                    }
                }
            }
            for info in ser.objects_of_type(114) {
                let Ok(obj)=ser.scene_object(info) else{continue;};
                let props=obj.class.properties();
                if !props.contains_key("InsideWait") || !props.contains_key("texStates") {continue;}
                // ReSetupMaterial falls back to GetComponent<Renderer>(); all
                // selected heads serialize a null _renderer before Start.
                let renderer=props.get("_renderer").and_then(uv_pptr).filter(|id|*id!=0)
                    .or_else(||props.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_renderers.get(&go).copied()));
                let Some(key)=renderer.and_then(|id|renderer_mesh.get(&id)) else{continue;};
                let mut values=[0.,-100.,100.,-100.,100.,-100.,100.,-80.,80.,0.001,0.,0.,0.,0.];
                for (i,name) in [(0,"eyeLR"),(1,"InsideWait"),(2,"OutsideWait"),(3,"UpWait"),(4,"DownWait"),
                    (5,"InsideLimit"),(6,"OutsideLimit"),(7,"UpLimit"),(8,"DownLimit"),(9,"power")] {
                    values[i]=props.get(name).and_then(|v|v.as_f64().or_else(||v.as_i64().map(|n|n as f64))).map(|n|n as f32).unwrap_or(values[i]);
                }
                for (i,root,key) in [(10,"offset","x"),(11,"offset","y"),(12,"scale","x"),(13,"scale","y")] {
                    values[i]=props.get(root).and_then(|v|uv_obj(v,key)).and_then(|v|v.as_f64()).map(|n|n as f32).unwrap_or(values[i]);
                }
                result.insert(key.clone(),values);
            }}
            result
        })
    }

    /// FaceBlendShape.MouthCtrl.FBSTarget of the selected head prefab. The
    /// ObjTarget is a GameObject PPtr; preserve FBSTarget order because runtime
    /// plugins such as SexFaces address Face/Neck/Teeth/Eyes/Tongue by index.
    pub fn face_mouth_patterns(&self, main_data: &str) -> HashMap<String, (usize, Vec<(i32, i32)>)> {let _spot=crate::profile::spot("face_mouth_patterns");
        self.face_control_patterns(main_data, "MouthCtrl")
    }

    /// Read a controller's authored target/channel indices. Names of blendshape
    /// channels are not a controller identity and may be arbitrary in mod heads.
    fn face_control_patterns_unity(&self, main_data: &str, controller: &str) -> HashMap<String, (usize, Vec<(i32, i32)>)> {
        let (meshes, _) = self.meshes_for_maindata(main_data);
        let mut renderer_mesh = HashMap::new();
        for mesh in &meshes {
            for renderer in &mesh.renderer_ids {
                renderer_mesh.insert(*renderer, mesh.name.to_ascii_lowercase());
            }
        }
        self.with_serialized(|sers| {
            let mut result = HashMap::new();
            for ser in sers {
                let mut go_names = HashMap::new();
                for go in ser.objects_of_type(1) {
                    if let Ok(obj) = ser.scene_object(go) {
                        go_names.insert(go.path_id, obj.name().unwrap_or_default());
                    }
                }
                let mut target_mesh = HashMap::new();
                for renderer in ser.objects_of_type(137) {
                    let Some(mesh_name) = renderer_mesh.get(&renderer.path_id) else { continue; };
                    let Ok(obj) = ser.scene_object(renderer) else { continue; };
                    if let Some(go) = obj.class.properties().get("m_GameObject").and_then(uv_pptr) {
                        target_mesh.insert(go, mesh_name.clone());
                    }
                }
                for info in ser.objects_of_type(114) {
                    let Ok(obj) = ser.scene_object(info) else { continue; };
                    let props = obj.class.properties();
                    let root = props.get("m_GameObject").and_then(uv_pptr)
                        .and_then(|id| go_names.get(&id));
                    if !root.is_some_and(|name| name.eq_ignore_ascii_case(main_data)) { continue; }
                    let Some(targets) = props.get(controller)
                        .and_then(|v| uv_obj(v, "FBSTarget")) else { continue; };
                    for (target_index, target) in uv_arr(targets).iter().enumerate() {
                        let Some(target_go) = uv_obj(target, "ObjTarget").and_then(uv_pptr) else { continue; };
                        let Some(mesh_name) = target_mesh.get(&target_go).cloned()
                            .or_else(|| go_names.get(&target_go).map(|s| s.to_ascii_lowercase()))
                        else { continue; };
                        let Some(patterns) = uv_obj(target, "PtnSet") else { continue; };
                        let pairs: Vec<(i32, i32)> = uv_arr(patterns).iter()
                            .filter_map(|p| Some((
                                uv_obj(p, "Close")?.as_i64()? as i32,
                                uv_obj(p, "Open")?.as_i64()? as i32,
                            )))
                            .collect();
                        if !pairs.is_empty() { result.insert(mesh_name, (target_index, pairs)); }
                    }
                }
            }
            result
        })
    }

    /// Material identity for an additional pass on the renderer's last
    /// submesh. Cloning slot0 must not carry its MaterialEditor keys to slot1.
    /// Material names per renderer slot, in the renderer order the slot
    /// lookup scans (OK model data: lookups by any renderer id set).
    fn renderer_material_names_unity(&self) -> Vec<(i64, Vec<Option<String>>)> {
        self.with_serialized(|sers| {
            let mut out = Vec::new();
            for ser in sers {
                let names: HashMap<i64, String> = ser.objects_of_type(21).into_iter().filter_map(|info|
                    ser.scene_object(info).ok().map(|o| (info.path_id, o.name().unwrap_or_default()))).collect();
                for info in ser.objects_of_type(137).into_iter().chain(ser.objects_of_type(23)) {
                    let Ok(renderer)=ser.scene_object(info) else {continue;};
                    let slots = renderer.class.properties().get("m_Materials").map(|v| uv_arr(v).iter()
                        .map(|m| local_pptr_path_id(m).and_then(|id| names.get(&id).cloned())).collect()).unwrap_or_default();
                    out.push((info.path_id, slots));
                }
            }
            out
        })
    }
    fn renderer_material_slot_unity(&self, renderer_ids: &[i64], slot: usize) -> Option<MeshMaterial> {
        let table = self.memo("renderer_materials", &(), || self.renderer_material_names_unity());
        let name = table.iter().find_map(|(id, slots)| renderer_ids.contains(id).then(|| slots.get(slot).cloned().flatten()).flatten());
        self.material_named(&name?)
    }
    /// OK base library conversion: GameObject names that own components.
    pub(crate) fn library_component_objects(&self) -> Vec<String> {
        self.with_serialized(|files| {
            let mut out = std::collections::BTreeSet::new();
            for file in files {
                let names: HashMap<i64, String> = file.objects_of_type(1).into_iter().filter_map(|info|
                    file.scene_object(info).ok().map(|o| (info.path_id, o.name().unwrap_or_default()))).collect();
                for info in file.objects_of_type(114) {
                    if let Ok(o) = file.scene_object(info) {
                        if let Some(name) = o.class.properties().get("m_GameObject").and_then(uv_pptr).and_then(|id| names.get(&id)) { out.insert(name.clone()); }
                    }
                }
            }
            out.into_iter().collect()
        })
    }

    /// Resolve a standalone material asset, such as `cf_m_body` in
    /// `chara/mm_base.unity3d`. The body mesh comes from `oo_base` (or an
    /// uncensor bundle), while ChaControl loads this draw material separately.
    fn material_named_unity(&self, wanted: &str) -> Option<MeshMaterial> {
        self.with_serialized(|sers| {
            let mut textures = HashMap::<i64, String>::new();
            for ser in sers {
                for info in ser.objects_of_type(28) {
                    if let Ok(o) = ser.scene_object(info) {
                        textures.insert(o.path_id(), o.name().unwrap_or_default());
                    }
                }
            }
            for ser in sers {
                // m_FileID=0 points into this serialized file. PathIDs from
                // another file in the bundle must never answer HasProperty.
                let mut shaders = HashMap::<i64, String>::new();
                let mut shader_rim = HashMap::<i64, bool>::new();
                let mut shader_state = HashMap::<i64, ShaderPassState>::new();
                for info in ser.objects_of_type(48) {
                    if let Ok(o) = ser.scene_object(info) {
                        let name = shader_runtime_name(&o);
                        if !name.is_empty() { shaders.insert(o.path_id(), name); }
                        if let Some(has_rim) = shader_declares_property(&o, "_rimV") {
                            shader_rim.insert(o.path_id(), has_rim);
                        }
                        if let Some(state) = shader_pass_state(&o) { shader_state.insert(o.path_id(), state); }
                    }
                }
                for info in ser.objects_of_type(21) {
                    let Ok(o) = ser.scene_object(info) else { continue; };
                    if !o.name().unwrap_or_default().eq_ignore_ascii_case(wanted) { continue; }
                    let props = o.class.properties();
                    let mut mat = MeshMaterial {
                        name: wanted.to_string(),
                        shader: props.get("m_Shader").and_then(local_pptr_path_id)
                            .and_then(|id| shaders.get(&id).cloned()).unwrap_or_default(),
                        shader_has_rim: props.get("m_Shader").and_then(local_pptr_path_id)
                            .and_then(|id| shader_rim.get(&id).copied()),
                        render_queue: props.get("m_CustomRenderQueue")
                            .and_then(|v| v.as_i64()).unwrap_or(-1) as i32,
                        keywords: props.get("m_ShaderKeywords").and_then(|v| v.as_str())
                            .unwrap_or("").split_whitespace().map(str::to_string).collect(),
                        ..Default::default()
                    };
                    if let Some(unity_asset_core::UnityValue::Object(saved)) = props.get("m_SavedProperties") {
                        // SetShader preserves every saved colour/vector, even
                        // when the source shader does not expose the property.
                        if let Some(unity_asset_core::UnityValue::Array(colors)) = saved.get("m_Colors") {
                            for entry in colors {
                                let (key, value) = match entry {
                                    unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 =>
                                        (pair[0].as_str().unwrap_or(""), &pair[1]),
                                    unity_asset_core::UnityValue::Object(pair) =>
                                        (pair.get("first").and_then(|v|v.as_str()).unwrap_or(""),
                                         pair.get("second").unwrap_or(&unity_asset_core::UnityValue::Null)),
                                    _ => continue,
                                };
                                if let Some(value) = value.as_object() {
                                    let rgba = ["r", "g", "b", "a"].map(|axis|
                                        value.get(axis).and_then(|v|v.as_f64())
                                            .unwrap_or(if axis == "a" {1.} else {0.}) as f32);
                                    mat.colors.insert(key.to_string(), rgba);
                                }
                            }
                        }
                        if let Some(unity_asset_core::UnityValue::Array(envs)) = saved.get("m_TexEnvs") {
                            for entry in envs {
                                let (key, value) = match entry {
                                    unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 =>
                                        (pair[0].as_str().unwrap_or(""), &pair[1]),
                                    _ => continue,
                                };
                                store_tex_env_transform(&mut mat, key, value);
                                let name = value.as_object().and_then(|v| v.get("m_Texture"))
                                    .and_then(pptr_path_id).and_then(|id| textures.get(&id));
                                if let Some(name) = name { mat.tex_slots.push((key.to_string(), name.clone())); }
                            }
                        }
                        if let Some(unity_asset_core::UnityValue::Array(floats)) = saved.get("m_Floats") {
                            for entry in floats {
                                if let unity_asset_core::UnityValue::Array(pair) = entry {
                                    if pair.len() == 2 {
                                        if let (Some(key), Some(value)) = (pair[0].as_str(), pair[1].as_f64()) {
                                            mat.floats.insert(key.to_string(), value as f32);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    let state = props.get("m_Shader").and_then(local_pptr_path_id).and_then(|id| shader_state.get(&id));
                    let shader = mat.shader.clone();
                    record_shader_pass_state(&mut mat.floats, &shader, state);
                    return Some(mat);
                }
            }
            None
        })
    }

    fn texture_png_named_unity(&self, name: &str) -> Option<Vec<u8>> {let _spot=crate::profile::spot("texture_png_named_unity");
        self.decode_tex(name)
    }

    fn texture_gpu_named_unity(&self,name:&str)->Option<crate::clothes_gpu::Texture> {
        let sampling=self.texture_sampling_named(name)?;
        self.with_serialized(|files| {
            for file in files {for info in file.objects_of_type(28) {
                let Ok(object)=file.scene_object(info) else {continue;};
                if !object.name().unwrap_or_default().eq_ignore_ascii_case(name){continue;}
                let tex=read_texture_payload(&object,&self.files,None)?;
                use unity_asset_decode::texture::TextureFormat;
                let srgb=tex.color_space!=0;
                let format=match tex.format {
                    TextureFormat::DXT1=>if srgb{72}else{71},TextureFormat::DXT5=>if srgb{78}else{77},
                    TextureFormat::BC7=>if srgb{99}else{98},TextureFormat::RGB24|TextureFormat::RGBA32|TextureFormat::ARGB32|TextureFormat::BGRA32=>if srgb{29}else{28},
                    _=>return None,
                };
                // Reorder every texel of every authored mip, preserving alpha.
                let data=match tex.format {
                    TextureFormat::RGB24=>tex.image_data.chunks_exact(3).flat_map(|p|[p[0],p[1],p[2],255]).collect(),
                    TextureFormat::ARGB32=>tex.image_data.chunks_exact(4).flat_map(|p|[p[1],p[2],p[3],p[0]]).collect(),
                    TextureFormat::BGRA32=>tex.image_data.chunks_exact(4).flat_map(|p|[p[2],p[1],p[0],p[3]]).collect(),
                    _=>tex.image_data,
                };
                let settings=object.class.properties().get("m_TextureSettings")?;
                let aniso=uv_obj(settings,"m_Aniso").and_then(|v|v.as_i64()).unwrap_or(1).clamp(0,16) as u32;
                let bias=uv_obj(settings,"m_MipBias").and_then(|v|v.as_f64()).unwrap_or(0.0) as f32;
                return Some(crate::clothes_gpu::Texture{data,width:tex.width as u32,height:tex.height as u32,format,mips:tex.mip_count.max(1) as u32,sampling,aniso,bias});
            }}None
        })
    }

    fn shader_pixel_dxbc_named_unity(&self,name:&str)->Option<Vec<u8>> {
        self.with_serialized(|files| {
            for file in files {for info in file.objects_of_type(48) {
                let Ok(object)=file.scene_object(info) else {continue;};
                if std::env::var_os("OK_CLOTHES_GPU_DIAG").is_some(){eprintln!("CLOTHES_SHADER name={} wanted={name} keys={:?}",shader_runtime_name(&object),object.class.properties().keys());}
                if shader_runtime_name(&object)!=name {continue;}
                if name=="Koikano/create_topN" {clothes_create_shader_layout(&object)?;}
                let props=object.class.properties();
                let unity_asset_core::UnityValue::Bytes(comp)=props.get("compressedBlob")? else{return None;};
                let blocks=decompress_shader_platforms(comp,&shader_nested_blob_lengths(props.get("offsets")),&shader_nested_blob_lengths(props.get("compressedLengths")),&shader_nested_blob_lengths(props.get("decompressedLengths")));
                if std::env::var_os("OK_CLOTHES_GPU_DIAG").is_some(){eprintln!("CLOTHES_SHADER blocks={:?}",blocks.iter().map(Vec::len).collect::<Vec<_>>());}
                for raw in blocks {for at in raw.windows(4).enumerate().filter_map(|(i,w)|(w==b"DXBC").then_some(i)) {
                    let size=u32::from_le_bytes(raw.get(at+24..at+28)?.try_into().ok()?) as usize;
                    let bytes=raw.get(at..at+size)?;let chunks=u32::from_le_bytes(bytes.get(28..32)?.try_into().ok()?) as usize;
                    for i in 0..chunks {let offset=u32::from_le_bytes(bytes.get(32+i*4..36+i*4)?.try_into().ok()?) as usize;
                        let tag=bytes.get(offset..offset+4)?;if tag!=b"SHDR"&&tag!=b"SHEX" {continue;}
                        let version=u32::from_le_bytes(bytes.get(offset+8..offset+12)?.try_into().ok()?);
                        if std::env::var_os("OK_CLOTHES_GPU_DIAG").is_some(){eprintln!("CLOTHES_SHADER chunk={tag:?} version={version:x}");}
                        if version>>16==0{return Some(bytes.to_vec());}
                    }
                }}
            }}None
        })
    }

    fn texture_sampling_named_unity(&self,name:&str)->Option<TextureSampling> {
        self.exact_texture_location(name).and_then(|location| location.sampling)
            .or_else(|| self.dependency_texture_bundle(name)?.texture_sampling_named(name))
    }

    /// A library model has no Unity internals: reaching them during replay
    /// means a query path that is not recorded (reported with its callers).
    fn library_internals(&self, what: &'static str) {
        if self.files.is_empty() && crate::oklib::replaying() && !PREFAB_ABSENT.with(|a| a.get()) {
            // A model this checkout does not have (a third-party zipmod's, out of version control): a gap naming it,
            // so the zipmod it comes from is reported (and, while an OK card is made, taken from the game).
            match self.lib {
                Some(id) if !crate::oklib::has_model(&id) => crate::oklib::note_absent_model(&id, what),
                Some(_) => crate::oklib::note_unmemoized(what),
                None => {}
            }
        }
    }

    pub(crate) fn with_serialized<R>(&self, f: impl FnOnce(&[unity_asset_binary::asset::SerializedFile]) -> R) -> R {
        self.library_internals("ParsedBundle::with_serialized");
        self.serialized_files(f)
    }

    /// `with_serialized` without noting a library query (callers note their own).
    fn serialized_files<R>(&self, f: impl FnOnce(&[unity_asset_binary::asset::SerializedFile]) -> R) -> R {
        // Nested read-only metadata queries must not take a mutable RefCell
        // borrow after initialization (e.g. texture index inside a mesh walk).
        let initialize=self.serialized.borrow().is_none();
        if initialize {
            let started=std::time::Instant::now();
            let mut slot = self.serialized.borrow_mut();
            if slot.is_none() {
                let mut v = Vec::new();
                for (index, (name, fb)) in self.files.iter().enumerate() {
                    if let Ok(s) = parse_shared_serialized(fb.clone()) {
                        v.push(s);
                        self.serialized_names.borrow_mut().push(name.clone());
                        self.serialized_file_index.borrow_mut().push(index);
                    }
                }
                *slot = Some(v);
            }
            if std::env::var_os("OK_APPEARANCE_PROFILE").is_some() {eprintln!("APPEARANCE_PROFILE phase=serialized_init ms={:.3}",started.elapsed().as_secs_f64()*1000.);}
        }
        let slot = self.serialized.borrow();
        f(slot.as_ref().map(|v| v.as_slice()).unwrap_or(&[]))
    }

    /// Bind dependency resolution to the actual installation. Byte-only parsing
    /// resolves named files inside this bundle, but never guesses a game path.
    pub(crate) fn configure_game_references(&self, root: &Path) {let _spot=crate::profile::spot("configure_game_references");
        if self.script_root.borrow().as_deref() != Some(root) {
            *self.script_root.borrow_mut() = Some(root.to_owned());
            *self.script_origin.borrow_mut() = None;
            *self.script_maps.borrow_mut() = None;
            *self.dbone_sets.borrow_mut() = None;
        }
    }
    pub(crate) fn configure_game_references_from_zip(&self,root:&Path,origin:Option<&Path>) {
        self.configure_game_references(root);
        if self.script_origin.borrow().as_deref()!=origin {
            *self.script_origin.borrow_mut()=origin.map(Path::to_owned);
            *self.script_maps.borrow_mut()=None;
            *self.dbone_sets.borrow_mut()=None;
        }
    }
    fn with_scripts<R>(&self, f:impl FnOnce(&[unity_asset_binary::asset::SerializedFile], &[references::Scripts])->R)->R {
        self.with_serialized(|files| {
            if self.script_maps.borrow().is_none() {
                let names=self.serialized_names.borrow();
                let named:Vec<_>=names.iter().zip(files).map(|(name,file)|(name.as_str(),file)).collect();
                let maps=references::resolve(&named,self.script_root.borrow().as_deref(),self.script_origin.borrow().as_deref());
                *self.script_maps.borrow_mut()=Some(maps);
            }
            f(files,self.script_maps.borrow().as_ref().unwrap())
        })
    }

    /// OK base library conversion: material and shader names of the source.
    pub(crate) fn library_inventory(&self) -> (Vec<String>, Vec<String>) {
        self.with_serialized(|files| {
            let (mut materials, mut shaders) = (Vec::new(), Vec::new());
            for file in files {
                for info in file.objects_of_type(21) { if let Ok(o) = file.scene_object(info) { materials.push(o.name().unwrap_or_default()); } }
                for info in file.objects_of_type(48) { if let Ok(o) = file.scene_object(info) { shaders.push(shader_runtime_name(&o)); } }
            }
            materials.retain(|n| !n.is_empty()); shaders.retain(|n| !n.is_empty());
            materials.sort(); materials.dedup(); shaders.sort(); shaders.dedup();
            (materials, shaders)
        })
    }
    pub(crate) fn library_prepare_dbones(&self) { self.ensure_dbone_sets(); }

    fn ensure_dbone_sets(&self) {
        if self.dbone_sets.borrow().is_none() {
            let sets = crate::oklib::memo(self.lib.as_ref(), "dbone_sets", &self.script_context(), || self.with_scripts(|sers,scripts| {
                sers.iter().zip(scripts).map(|(file,scripts)|crate::dbone::dbone_set_resolved(file,scripts)).collect::<Vec<_>>()
            }));
            *self.dbone_sets.borrow_mut() = Some(sets);
        }
    }
    /// Script resolution depends on the zipmod a bundle came from (its own
    /// script bundles); the file name identifies it independent of install path.
    fn script_context(&self) -> String {
        self.script_origin.borrow().as_deref().and_then(|p| p.file_name()).map(|n| n.to_string_lossy().to_ascii_lowercase()).unwrap_or_default()
    }

    pub fn hang_dynamic_bones(&self, skel: &mut crate::pose::Skeleton) -> usize {
        self.ensure_dbone_sets();
        let sets = self.dbone_sets.borrow();
        let Some(sets) = sets.as_ref() else {
            return 0;
        };
        let mut n = 0usize;
        for set in sets {
            n += crate::dbone::hang_set(set, skel);
        }
        n
    }

    fn skeleton_unity(&self, path_id: i64) -> Option<crate::pose::Skeleton> {
        if let Some(s) = self.skel_by_path.borrow().get(&path_id) {
            return s.clone();
        }
        let found = self.with_serialized(|sers| {
            for ser in sers {
                if let Some(s) = crate::pose::skeleton_from_serialized(ser, path_id) {
                    return Some(s);
                }
            }
            None
        });
        self.skel_by_path.borrow_mut().insert(path_id, found.clone());
        found
    }

    /// Full transform tree of the bundle's prefab as one skeleton, optionally
    /// requiring a probe bone name. Used to get the head's hair bones
    /// (cf_J_hairF_top ...) that the face SMR skeleton prunes away.
    fn skeleton_full_named_unity(&self, probe: &str) -> Option<crate::pose::Skeleton> {
        self.with_serialized(|sers| {
            for ser in sers {
                if let Some(s) = crate::pose::skeleton_full_tree(ser) {
                    if probe.is_empty()
                        || s.tfs.values().any(|t| t.name.eq_ignore_ascii_case(probe))
                    {
                        return Some(s);
                    }
                }
            }
            None
        })
    }

    /// Skeleton built from an armature prefab (`p_cf_body_bone` /
    /// `p_cf_head_bone`) that has no SkinnedMeshRenderer.
    fn skeleton_for_prefab_name_unity(&self, prefab_name: &str) -> Option<crate::pose::Skeleton> {
        self.with_serialized(|sers| {
            for ser in sers {
                if let Some(s) = crate::pose::skeleton_for_prefab(ser, prefab_name) {
                    return Some(s);
                }
            }
            None
        })
    }

    fn textures_keep_base_unity(&self) -> Vec<(String, Vec<u8>)> {
        self.ensure_tex_catalog();
        let names: Vec<String> = self
            .tex_names
            .borrow()
            .iter()
            .filter(|(n, _)| keep_base_tex(n))
            .map(|(n, _)| n.clone())
            .collect();
        let mut out = Vec::new();
        for n in names {
            if let Some(png) = self.decode_tex(&n) {
                out.push((n, png));
            }
        }
        out
    }

    pub fn meshes_for_maindata(
        &self,
        main_data: &str,
    ) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {let _spot=crate::profile::spot("meshes_for_maindata");
        self.meshes_for_maindata_with_renderer_state(main_data, false)
    }

    pub fn dynamic_bones_for_skeleton(&self, skel: &crate::pose::Skeleton) -> crate::dbone::DBoneSet {
        self.ensure_dbone_sets();
        let sets = self.dbone_sets.borrow();
        if std::env::var_os("OK_DBONE_DIAG").is_some() {
            eprintln!("ok-extract: DBONE sets={} components={} script_context={:?} lib={:?}", sets.as_ref().map_or(0, |s| s.len()),
                sets.as_ref().map_or(0, |s| s.iter().map(|x| x.len()).sum::<usize>()), self.script_context(), self.lib.as_ref().map(|_| "library"));
            for (i, set) in sets.as_ref().unwrap().iter().enumerate() { set.diag_against(skel, &format!("file{i} rig_bones={}", skel.names.len())); }
        }
        let mut result = crate::dbone::DBoneSet::default();
        for set in sets.as_ref().unwrap() {
            result.extend_for_skeleton(set, skel);
        }
        result
    }

    fn body_dynamics_for_skeleton_unity(&self, skel: &crate::pose::Skeleton) -> crate::dbone::ver02::BodyDynamicSet {
        self.with_scripts(|files,scripts| {
            let mut set = crate::dbone::ver02::BodyDynamicSet::default();
            for (file,scripts) in files.iter().zip(scripts) { set.extend(crate::dbone::ver02::parse_resolved(file, skel,scripts)); }
            set
        })
    }
    fn body_colliders_for_skeleton_unity(&self, skel: &crate::pose::Skeleton) -> crate::dbone::BodyColliderSet {
        self.with_scripts(|files,scripts| {
            let mut set = crate::dbone::BodyColliderSet::default();
            for (file,scripts) in files.iter().zip(scripts) { set.extend_serialized(file, skel,scripts); }
            set
        })
    }

    fn bone_implants_for_skeleton_unity(&self, skel: &crate::pose::Skeleton) -> Vec<crate::dbone::implant::Marker> {
        self.with_scripts(|files,scripts| files.iter().zip(scripts).flat_map(|(file,scripts)|crate::dbone::implant::parse_resolved(file,skel,scripts)).collect())
    }

    /// Material Editor can enable a renderer that the prefab ships disabled,
    /// then save the other variant renderers as explicitly disabled. Only the
    /// accessory fallback uses this broader candidate set.
    pub fn meshes_for_maindata_including_disabled(
        &self,
        main_data: &str,
    ) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
        self.meshes_for_maindata_with_renderer_state(main_data, true)
    }

    fn meshes_for_maindata_with_renderer_state(
        &self,
        main_data: &str,
        include_disabled: bool,
    ) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
        self.meshes_for_maindata_with_clothes_kind(main_data, include_disabled, None)
    }

    fn meshes_for_maindata_with_clothes_kind(
        &self,
        main_data: &str,
        include_disabled: bool,
        clothes_category: Option<i32>,
    ) -> (Vec<ExtractedMesh>, Option<crate::pose::Skeleton>) {
        if main_data.is_empty() || main_data.eq_ignore_ascii_case("p_dummy") {
            return (Vec::new(), None);
        }
        // The prefab's meshes do not depend on the card; the worn clothes
        // state does. Record the former, apply the latter here.
        if self.library_model() && self.prefab_directory().proves_no_meshes(main_data) { return (Vec::new(), None); }
        // Exact spelling: some prefab lookups are case-sensitive (a different
        // spelling can fall through to the fuzzy mesh-name search).
        let (mut picked, chains) = crate::oklib::memo_packed(self.lib.as_ref(), "meshes_for_maindata", &(main_data, include_disabled),
            || crate::oklib::timed("#meshes unfiltered", || self.meshes_for_maindata_unfiltered(main_data, include_disabled)),
            |(m, c)| { let mut m = m.clone(); self.strip_mesh_textures(&mut m); (m, c.clone()) },
            |(m, _)| self.restore_mesh_textures(m));
        self.clothes_state_filter(&mut picked, &chains, clothes_category);
        if let Some(e) = self.model_edits() { self.apply_mesh_edits(&e, main_data, &mut picked); }
        // A prefab can contain static and skinned renderers together. The
        // first decoded mesh is not necessarily an SMR, so use the first
        // renderer that actually has a skeleton.
        let skel = picked.iter().find_map(|m| self.skeleton(m.path_id));
        (picked, skel)
    }

    fn meshes_for_maindata_unfiltered(&self, main_data: &str, include_disabled: bool)
        -> (Vec<ExtractedMesh>, Vec<(i64, Vec<String>)>) {
        let (ids, mesh_renderers, renderer_names) = crate::oklib::timed("#prefab ids", || self.ensure_prefab_ids(main_data, include_disabled));
        crate::oklib::timed("#decode meshes", || self.decode_meshes_batch(&ids));
        let mut picked: Vec<ExtractedMesh> = ids
            .iter()
            .filter_map(|id| self.meshes.borrow().get(id).cloned())
            .collect();
        if picked.is_empty() {
            picked = self.decode_meshes_named(main_data);
        }
        for m in picked.iter_mut() {
            if let Some(rs) = mesh_renderers.get(&m.path_id) {
                m.renderer_ids = rs.clone();
            }
            if let Some(ns) = renderer_names.get(&m.path_id) {
                m.renderer_names = ns.clone();
            }
        }
        let chains = crate::oklib::timed("#state chains", || self.clothes_state_chains());
        crate::oklib::timed("#attach picked", || self.attach_picked(&mut picked));
        crate::oklib::timed("#attach materials", || self.attach_material_texs(&mut picked));
        crate::oklib::timed("#bone names", || self.fill_bone_names(&mut picked));
        // Skinned meshes also need the renderer GameObject transform to
        // reproduce Unity BakeMesh's final renderer-scale pivot.
        if !picked.is_empty() {
            let locals = crate::oklib::timed("#prefab locals", || self.ensure_prefab_locals(main_data));
            if !locals.is_empty() {
                for m in picked.iter_mut() {
                    if let Some(l) = locals.get(&m.path_id) {
                        m.prefab_local = Some(*l);
                        if std::env::var("OK_DIAG").is_ok() {
                            eprintln!(
                                "ok-extract: prefab_local {} pos={:?} scl={:?}",
                                m.name,
                                crate::pose::mat_pos(l),
                                crate::pose::world_scale_pub(l)
                            );
                        }
                    }
                }
            }
        }
        // ChaControl.UpdateAccessoryMoveFromInfo replaces the local TRS of the
        // prefab's `N_move`/`N_move2` nodes, so accessory placement needs them.
        let moves = crate::oklib::timed("#move nodes", || self.ensure_move_nodes(main_data));
        let prefab_moves = moves.get(&0).copied().unwrap_or([None, None]);
        for m in picked.iter_mut() {
            let own_nodes = moves.get(&m.path_id).copied().unwrap_or([None, None]);
            m.move_nodes = if own_nodes.iter().any(Option::is_some) {
                own_nodes
            } else if m.smr_skel.is_some() {
                // Some mod SMRs sit beside N_move and skin through bones or
                // plugin wiring outside the renderer's transform ancestry.
                // ChaControl still updates the prefab move node for that item.
                prefab_moves
            } else {
                own_nodes
            };
        }
        (picked, chains)
    }

    /// Drop the non-worn clothes-state meshes. Koikatsu clothes prefabs carry a
    /// `n_top_a`/`n_top_b`(?嚙質) `n_bot_a`/`n_bot_b`/`n_bot_c`(?嚙質) `o_inner_a/b`
    /// `o_bra_a/b` `n_panst_*` `n_socks_*` ??node per state; the character shows
    /// only one (`ChaControl.cs:1317-1495`, default state 0 = 蝛踹末 = the `_a`
    /// node). We loaded the whole prefab, so both states rendered at once.
    fn clothes_state_filter(&self, meshes: &mut Vec<ExtractedMesh>, chains: &[(i64, Vec<String>)], clothes_category: Option<i32>) {
        let states = clothes_states();
        let picked: std::collections::HashSet<i64> = meshes.iter().map(|m| m.path_id).collect();
        note_state_nodes(chains.iter().filter(|(mesh, _)| picked.contains(mesh)).flat_map(|(_, chain)| chain.iter()), clothes_category);
        // A renderer can sit below two state nodes (for example o_bra_b inside
        // n_top_b). Every ancestor must be active, just like activeInHierarchy.
        let drop: std::collections::HashSet<i64> = chains.iter().filter(|(_, chain)| chain.iter()
            .any(|name| clothes_state_node_visible(name, &states, clothes_category) == Some(false)))
            .map(|(mesh, _)| *mesh).collect();
        if std::env::var("OK_STATEDIAG").is_ok() {
            eprintln!("ok-extract: STATE drop={} picked={}", drop.len(), meshes.len());
        }
        if !drop.is_empty() {
            meshes.retain(|m| !drop.contains(&m.path_id));
        }
    }

    /// GameObject ancestry (renderer GameObject up to the root) of each mesh.
    fn clothes_state_chains(&self) -> Vec<(i64, Vec<String>)> {
        if let Some(c) = self.state_chains.borrow().as_ref() { return (**c).clone(); }
        let chains = self.clothes_state_chains_scan();
        *self.state_chains.borrow_mut() = Some(std::rc::Rc::new(chains.clone()));
        chains
    }
    fn clothes_state_chains_scan(&self) -> Vec<(i64, Vec<String>)> {
        self.with_serialized(|sers| {
            let mut out = Vec::new();
            for ser in sers {
                // GameObject name, Transform -> (GameObject, father Transform).
                let mut go_name: HashMap<i64, String> = HashMap::new();
                let mut tf_go: HashMap<i64, i64> = HashMap::new();
                let mut tf_father: HashMap<i64, i64> = HashMap::new();
                let mut mesh_go: HashMap<i64, i64> = HashMap::new();
                for info in &ser.objects {
                    // Only these types are used: skip reading (decompressing, parsing) textures and meshes.
                    if !matches!(info.type_id, 1 | 4 | 33 | 137) { continue; }
                    let Ok(obj) = ser.scene_object(info) else {
                        continue;
                    };
                    match info.type_id {
                        1 => {
                            go_name.insert(info.path_id, obj.name().unwrap_or_default());
                        }
                        4 => {
                            let props = obj.class.properties();
                            if let Some(g) = props.get("m_GameObject").and_then(uv_pptr) {
                                tf_go.insert(info.path_id, g);
                            }
                            if let Some(f) = props.get("m_Father").and_then(uv_pptr) {
                                tf_father.insert(info.path_id, f);
                            }
                        }
                        33 | 137 => {
                            let props = obj.class.properties();
                            if let (Some(g), Some(m)) = (
                                props.get("m_GameObject").and_then(uv_pptr),
                                props.get("m_Mesh").and_then(uv_pptr),
                            ) {
                                mesh_go.insert(m, g);
                            }
                        }
                        _ => {}
                    }
                }
                let go_tf: HashMap<i64, i64> =
                    tf_go.iter().map(|(tf, go)| (*go, *tf)).collect();
                for (mesh, go) in &mesh_go {
                    let mut cur = *go;
                    let mut guard = 0;
                    let mut chain: Vec<String> = Vec::new();
                    while guard < 64 {
                        guard += 1;
                        let Some(name) = go_name.get(&cur) else { break };
                        chain.push(name.clone());
                        let Some(tf) = go_tf.get(&cur) else { break };
                        let Some(father) = tf_father.get(tf) else { break };
                        let Some(pgo) = tf_go.get(father) else { break };
                        cur = *pgo;
                    }
                    if std::env::var("OK_STATEDIAG").is_ok()
                        && chain.iter().any(|c| c.contains("skirt09") || c.contains("shorts"))
                    {
                        eprintln!("ok-extract: STATE chain={chain:?}");
                    }
                    out.push((*mesh, chain));
                }
            }
            out.sort();
            out
        })
    }

    fn fill_bone_names(&self, meshes: &mut [ExtractedMesh]) {
        let cached = self.renderer_kinds.borrow().clone();
        let renderer_types = match cached { Some(r) => r, None => {
            let r: std::rc::Rc<HashMap<i64,bool>> = std::rc::Rc::new(self.with_serialized(|files| files.iter()
                .flat_map(|file| file.objects.iter().filter(|info| matches!(info.type_id,23|137))
                    .map(|info|(info.path_id,info.type_id==137))).collect()));
            *self.renderer_kinds.borrow_mut() = Some(r.clone()); r } };
        for m in meshes {
            let kinds: std::collections::HashSet<_> = m.renderer_ids.iter()
                .filter_map(|id|renderer_types.get(id).copied()).collect();
            m.renderer_skinned = if kinds.len()==1 {kinds.iter().copied().next()} else {None};
            if let Some(s) = self.skeleton(m.path_id).filter(|_|m.renderer_skinned!=Some(false)) {
                m.bone_names = s.names.clone();
                m.smr_skel = Some(s);
            }
        }
    }

    /// Mesh GameObject ??prefab-root-relative transform, cached per prefab.
    /// Parsing the whole transform graph is expensive, so only do it once.
    /// Transform graph per serialized file, built once per bundle.
    fn transform_graphs(&self) -> std::rc::Rc<Vec<crate::pose::SkelGraph>> {
        if let Some(g) = self.skel_graphs.borrow().as_ref() { return g.clone(); }
        let graphs = std::rc::Rc::new(self.with_serialized(|sers| sers.iter().map(crate::pose::build_skel_graph).collect::<Vec<_>>()));
        *self.skel_graphs.borrow_mut() = Some(graphs.clone());
        graphs
    }

    fn ensure_prefab_locals(&self, main_data: &str) -> HashMap<i64, crate::pose::Mat4> {
        let want = main_data.to_ascii_lowercase();
        if let Some(v) = self.prefab_locals.borrow().get(&want) {
            return v.clone();
        }
        let found = (|| {
            for graph in self.transform_graphs().iter() {
                let m = crate::pose::mesh_prefab_locals_in(graph, main_data);
                if !m.is_empty() {
                    return m;
                }
            }
            HashMap::new()
        })();
        self.prefab_locals
            .borrow_mut()
            .insert(want, found.clone());
        found
    }

    /// `N_move`/`N_move2` ancestors per renderer mesh, cached per prefab.
    fn ensure_move_nodes(
        &self,
        main_data: &str,
    ) -> HashMap<i64, [Option<(crate::pose::Mat4, crate::pose::Mat4)>; 2]> {
        let want = main_data.to_ascii_lowercase();
        if let Some(v) = self.prefab_moves.borrow().get(&want) {
            return v.clone();
        }
        let found = (|| {
            for graph in self.transform_graphs().iter() {
                let mut moves = crate::pose::mesh_prefab_move_nodes_in(graph, main_data);
                let global = [
                    crate::pose::prefab_node_world_in(graph, main_data, "N_move"),
                    crate::pose::prefab_node_world_in(graph, main_data, "N_move2"),
                ];
                if global.iter().any(Option::is_some) || !moves.is_empty() {
                    // PathId 0 is not a Unity object; reserve it for the
                    // prefab-wide move nodes used by sibling SMR bone rigs.
                    moves.insert(0, global);
                    return moves;
                }
            }
            HashMap::new()
        })();
        self.prefab_moves
            .borrow_mut()
            .insert(want, found.clone());
        found
    }

    fn ensure_prefab_ids(
        &self,
        main_data: &str,
        include_disabled: bool,
    ) -> (Vec<i64>, HashMap<i64, Vec<i64>>, HashMap<i64, Vec<String>>) {
        let want = format!("{}{}", main_data.to_ascii_lowercase(), if include_disabled { "#disabled" } else { "" });
        if let Some(ids) = self.prefab_mesh_ids.borrow().get(&want) {
            let rends = self
                .prefab_mesh_renderers
                .borrow()
                .get(&want)
                .cloned()
                .unwrap_or_default();
            let names = self
                .prefab_renderer_names
                .borrow()
                .get(&want)
                .cloned()
                .unwrap_or_default();
            return (ids.clone(), rends, names);
        }
        if self.prefab_graphs.borrow().is_none() {
            let graphs = self.with_serialized(|sers| {
                sers.iter().map(scan_prefab_graph).collect::<Vec<_>>()
            });
            *self.prefab_graphs.borrow_mut() = Some(graphs);
        }
        let found = {
            let graphs = self.prefab_graphs.borrow();
            let mut found = (Vec::new(), HashMap::new(), HashMap::new());
            if let Some(graphs) = graphs.as_ref() {
                for g in graphs {
                    let (ids, rends, names) = query_prefab_graph(g, main_data, include_disabled);
                    if !ids.is_empty() {
                        found = (ids, rends, names);
                        break;
                    }
                }
            }
            found
        };
        self.prefab_mesh_ids
            .borrow_mut()
            .insert(want.clone(), found.0.clone());
        self.prefab_mesh_renderers
            .borrow_mut()
            .insert(want.clone(), found.1.clone());
        self.prefab_renderer_names
            .borrow_mut()
            .insert(want, found.2.clone());
        found
    }

    fn decode_meshes_batch(&self, ids: &[i64]) {
        if ids.is_empty() {
            return;
        }
        let need: Vec<i64> = ids
            .iter()
            .copied()
            .filter(|id| !self.meshes.borrow().contains_key(id))
            .collect();
        if need.is_empty() {
            self.warm_skeletons(ids);
            return;
        }
        let ver = version();
        // Renderer meshes in a dependency bundle: the PathID belongs to that
        // file, never to a local object.
        let external = self.external_mesh_refs(&need);
        if !external.is_empty() {
            self.decode_external_meshes(&external, &ver);
        }
        self.with_serialized(|sers| {
            for ser in sers {
                for info in ser.objects_of_type(MESH_CLASS_ID) {
                    if !need.contains(&info.path_id) || external.contains_key(&info.path_id) {
                        continue;
                    }
                    if self.meshes.borrow().contains_key(&info.path_id) {
                        continue;
                    }
                    let Ok(object) = ser.scene_object(&info) else {
                        continue;
                    };
                    let stream = mesh_stream_bytes(&object, &self.files);
                    if let Some(mesh) = decoded_from_object(&object, &ver, stream.as_deref(), &ser.unity_version) {
                        self.meshes.borrow_mut().insert(info.path_id, mesh);
                    }
                }
                let missing: Vec<i64> = ids
                    .iter()
                    .copied()
                    .filter(|id| !self.skel_by_path.borrow().contains_key(id))
                    .collect();
                if !missing.is_empty() {
                    let found = crate::pose::skeletons_from_serialized(ser, &missing);
                    for id in missing {
                        self.skel_by_path
                            .borrow_mut()
                            .insert(id, found.get(&id).cloned());
                    }
                }
            }
        });
    }

    /// Mesh path id -> external file path of the prefab renderers that draw a
    /// mesh of another serialized file (`m_Mesh.m_FileID` != 0).
    fn external_mesh_refs(&self, ids: &[i64]) -> HashMap<i64, String> {
        let graphs = self.prefab_graphs.borrow();
        let Some(graphs) = graphs.as_ref() else { return HashMap::new(); };
        ids.iter().filter_map(|id| graphs.iter().find_map(|g| g.ext_mesh.get(id)).map(|p| (*id, p.clone()))).collect()
    }

    /// A texture a material takes from another serialized file (`dependency_texture_source`), by name. An OK model
    /// records its texel payload once per name (the PNG is derived, as for the bundle's own textures), so mesh
    /// records keep the name only.
    fn dependency_texture_bundle(&self, name: &str) -> Option<Arc<ParsedBundle>> {
        let key = name.to_ascii_lowercase();
        if crate::oklib::active().is_some() {
            // Only names a material took from outside are recorded (others stay unanswered, as in Unity).
            if !self.library_model() && !self.dependency_texture_source.borrow().contains_key(&key) { return None; }
            // A library model records only names taken from outside: no record is the answer (not a gap of it).
            if self.library_model() && !crate::oklib::has_record(self.lib.as_ref(), "dependency_texture_model", key.as_str()) { return None; }
            // The dependency bundle is a library model of its own (its textures recorded there): record which one,
            // and decode the texture from it.
            let model: Option<String> = self.memo("dependency_texture_model", key.as_str(), || {
                let source = self.dependency_texture_source.borrow().get(&key).cloned()?;
                let root = self.script_root.borrow().clone()?;
                let path = references::external_source_path(&root, &source, self.script_origin.borrow().as_deref())?;
                // Each texture name records the same dependency identity.
                // Reuse the validated path parser instead of reading and
                // hashing that whole source again for every material slot.
                Some(crate::oklib::id_hex(&parse_bundle_path_cached(&path)?.lib?))
            });
            let id = crate::oklib::id_from_hex_pub(&model?)?;
            let dependency = if crate::oklib::replaying() { parse_bundle_cached(crate::oklib::token(&id)) } else {
                let source = self.dependency_texture_source.borrow().get(&key).cloned()?;
                let root = self.script_root.borrow().clone()?;
                let path = references::external_source_path(&root, &source, self.script_origin.borrow().as_deref())?;
                parse_bundle_path_cached(&path)?
            };
            if std::ptr::eq(dependency.as_ref(), self) { return None; }
            Some(dependency)
        } else {
            let source = self.dependency_texture_source.borrow().get(&key).cloned()?;
            let root = self.script_root.borrow().clone()?;
            let path = references::external_source_path(&root, &source, self.script_origin.borrow().as_deref())?;
            let dependency = parse_bundle_path_cached(&path)?;
            if std::ptr::eq(dependency.as_ref(), self) { return None; }
            Some(dependency)
        }
    }

    fn dependency_texture_png(&self, name: &str) -> Option<Vec<u8>> {
        let key = name.to_ascii_lowercase();
        let png = self.dependency_texture_bundle(name)?.decode_tex(name)?;
        self.remember_texture(key, png.clone());
        Some(png)
    }

    /// The name of texture `path_id` in external file `external_path`.
    fn external_texture_name(&self, external_path: &str, path_id: i64) -> Option<String> {
        let file = self.external_file(external_path)?;
        let info = file.0.objects_of_type(28).into_iter().find(|i| i.path_id == path_id).or_else(|| file.0.objects_of_type(89).into_iter().find(|i| i.path_id == path_id))?;
        file.0.scene_object(info).ok()?.name()
    }

    /// A texture of a dependency bundle's material, by name, as PNG (cached under the external path).
    fn external_texture_png(&self, external_path: &str, name: &str) -> Option<Vec<u8>> {
        let key = format!("{}|{}", external_path.to_ascii_lowercase(), name.to_ascii_lowercase());
        if let Some(png) = self.external_textures.borrow().get(&key) { return png.clone(); }
        let png = self.external_file(external_path).and_then(|file| {
            let (ser, streams) = (&file.0, &file.1);
            ser.objects_of_type(28).into_iter().find_map(|info| {
                let object = ser.scene_object(info).ok()?;
                if !object.name().is_some_and(|n| n.eq_ignore_ascii_case(name)) { return None; }
                png_from_texture(&object, streams, None).map(|(_, png)| png)
            })
        });
        {
            // Dependency textures are cached by the bundle holding them too (decode_tex): keep this map small.
            let mut external = self.external_textures.borrow_mut();
            if external.len() >= 256 { external.clear(); }
            external.insert(key, png.clone());
        }
        png
    }

    fn external_file(&self, external_path: &str) -> Option<std::rc::Rc<ExternalFile>> {
        let key = external_path.to_ascii_lowercase();
        if let Some(f) = self.external_files.borrow().get(&key) { return f.clone(); }
        let root = self.script_root.borrow().clone();
        let loaded = match root {
            Some(root) => match references::load_external_with_streams(&root, external_path, self.script_origin.borrow().as_deref()) {
                Ok(f) => Some(std::rc::Rc::new(f)),
                Err(e) => { eprintln!("ok-extract: external mesh file {external_path}: {e}"); None }
            },
            None => { eprintln!("ok-extract: external mesh file {external_path}: no game root (byte-only parse)"); None }
        };
        self.external_files.borrow_mut().insert(key, loaded.clone());
        loaded
    }

    fn decode_external_meshes(&self, external: &HashMap<i64, String>, ver: &UnityVersion) {
        let mut by_file: std::collections::BTreeMap<&str, Vec<i64>> = std::collections::BTreeMap::new();
        for (id, path) in external { by_file.entry(path.as_str()).or_default().push(*id); }
        for (path, ids) in by_file {
            // Unity's built-in primitives (Library/unity default resources) are in no bundle: OK builds them.
            if path.to_ascii_lowercase().contains("unity default resources") {
                for id in ids {
                    match builtin_mesh(id) {
                        Some(mesh) => { self.meshes.borrow_mut().entry(id).or_insert(mesh); }
                        None => eprintln!("ok-extract: built-in mesh PathID={id} not supported"),
                    }
                }
                continue;
            }
            let Some(file) = self.external_file(path) else { continue; };
            let (ser, streams) = (&file.0, &file.1);
            for info in ser.objects_of_type(MESH_CLASS_ID) {
                if !ids.contains(&info.path_id) || self.meshes.borrow().contains_key(&info.path_id) { continue; }
                let Ok(object) = ser.scene_object(&info) else { continue; };
                let stream = mesh_stream_bytes(&object, streams);
                match decoded_from_object(&object, ver, stream.as_deref(), &ser.unity_version) {
                    Some(mesh) => { self.meshes.borrow_mut().insert(info.path_id, mesh); }
                    None => eprintln!("ok-extract: external mesh {path} PathID={} undecodable", info.path_id),
                }
            }
        }
    }

    fn warm_skeletons(&self, ids: &[i64]) {
        let need: Vec<i64> = ids
            .iter()
            .copied()
            .filter(|id| !self.skel_by_path.borrow().contains_key(id))
            .collect();
        if need.is_empty() {
            return;
        }
        self.with_serialized(|sers| {
            for ser in sers {
                let still: Vec<i64> = need
                    .iter()
                    .copied()
                    .filter(|id| !self.skel_by_path.borrow().contains_key(id))
                    .collect();
                if still.is_empty() {
                    break;
                }
                let found = crate::pose::skeletons_from_serialized(ser, &still);
                for id in still {
                    self.skel_by_path
                        .borrow_mut()
                        .insert(id, found.get(&id).cloned());
                }
            }
        });
    }

    fn decode_mesh(&self, path_id: i64) -> Option<ExtractedMesh> {
        if let Some(m) = self.meshes.borrow().get(&path_id) {
            return Some(m.clone());
        }
        self.decode_meshes_batch(&[path_id]);
        self.meshes.borrow().get(&path_id).cloned()
    }

    fn decode_meshes_named(&self, main_data: &str) -> Vec<ExtractedMesh> {
        let want = main_data.trim_start_matches("p_").to_ascii_lowercase();
        let ver = version();
        self.with_serialized(|sers| {
            let mut out = Vec::new();
            for ser in sers {
                for info in ser.objects_of_type(MESH_CLASS_ID) {
                    let Ok(object) = ser.scene_object(&info) else {
                        continue;
                    };
                    let n = object.name().unwrap_or_default().to_ascii_lowercase();
                    if n != want && !n.contains(&want) && !want.contains(&n) {
                        continue;
                    }
                    if let Some(existing) = self.meshes.borrow().get(&object.path_id()) {
                        out.push(existing.clone());
                        continue;
                    }
                    let stream = mesh_stream_bytes(&object, &self.files);
                    if let Some(mesh) = decoded_from_object(&object, &ver, stream.as_deref(), &ser.unity_version) {
                        self.meshes.borrow_mut().insert(mesh.path_id, mesh.clone());
                        out.push(mesh);
                    }
                }
            }
            out
        })
    }

    fn texture_header(ser:&unity_asset_binary::asset::SerializedFile,
        info:&unity_asset_binary::asset::ObjectInfo,bytes:&[u8])->Option<UnityObject> {
        Self::texture_header_fields(ser,info,bytes,&["m_Name","m_Width","m_Height","m_TextureSettings","m_ColorSpace","m_MipCount"])
    }

    fn texture_header_fields(ser:&unity_asset_binary::asset::SerializedFile,
        info:&unity_asset_binary::asset::ObjectInfo,bytes:&[u8],fields:&[&str])->Option<UnityObject> {
        use unity_asset_binary::typetree::{TypeTreeSerializer,TypeTreeParseOptions,TypeTreeParseMode};
        let internal=if info.type_index>=0 {ser.types.get(info.type_index as usize)}
            else {ser.types.iter().find(|t|t.class_id==info.type_id)};
        let registry=if !ser.enable_type_tree||internal.is_none_or(|t|t.type_tree.is_empty()) {
            ser.type_tree_registry.as_ref().and_then(|r|r.resolve(&ser.unity_version,info.type_id))
        }else{None};
        let tree=registry.as_deref().or_else(||internal.filter(|_|ser.enable_type_tree).map(|t|&t.type_tree))?;
        let children=&tree.nodes.first()?.children;
        let prefix=fields.iter().map(|name|children.iter().position(|n|n.name==*name))
            .collect::<Option<Vec<_>>>()?.into_iter().max()?+1;
        if children[..prefix].iter().any(|n|matches!(n.name.as_str(),"image data"|"m_ImageData"|"m_PlatformBlob")) {return None;}
        let mut reader=unity_asset_binary::reader::BinaryReader::new(bytes,ser.header.byte_order());
        let parsed=TypeTreeSerializer::new(tree).parse_object_prefix_detailed_with_ref_types(
            &mut reader,TypeTreeParseOptions{mode:TypeTreeParseMode::Strict},prefix,&ser.ref_types).ok()?;
        if fields.iter().any(|name|!parsed.properties.contains_key(*name)){return None;}
        let mut class=unity_asset_core::UnityClass::new(28,"Texture2D".into(),info.path_id.to_string());
        class.update_properties(parsed.properties);
        Some(UnityObject::from_info_and_class(info.clone(),class))
    }

    /// The (lowercase) names of the cubemaps in this bundle and its loaded dependency files: the material slots naming
    /// one are sampled as cubes (`cube_slot_atlases`).
    pub(crate) fn cubemap_names(&self) -> std::collections::HashSet<String> {
        let mut names = std::collections::HashSet::new();
        let mut add = |ser: &unity_asset_binary::asset::SerializedFile| for info in ser.objects_of_type(89) {
            if let Some(n) = UnityObject::from_serialized_file(ser, info).ok().and_then(|o| o.name()) { names.insert(n.to_ascii_lowercase()); }
        };
        self.with_texture_sources(|sers| for (_, ser) in sers.iter() { add(ser); });
        for file in self.external_files.borrow().values().flatten() { add(&file.0); }
        names
    }

    /// Every cube slot of the meshes' materials (a slot naming a cubemap, Skybox/Cubemap `_Tex`, StylizedWater
    /// `_Reflection`, ...) as its cube atlas (`cubemap_atlas`: the six faces side by side, mips below) in
    /// `shader_extra_textures[<slot>]`, for the renderer's own graphs to sample by direction.
    /// A library model answers from its `cube_atlas_named` records only (recorded by conversion or `studio-cubes`): a
    /// slot naming no recorded cubemap is an ordinary texture there, not a gap.
    pub(crate) fn cube_slot_atlases(&self, meshes: &mut [ExtractedMesh]) {
        let replay = self.library_model();
        let cubes = if replay { Default::default() } else { self.cubemap_names() };
        if !replay && cubes.is_empty() { return; }
        let mut atlases: HashMap<String, Option<Vec<u8>>> = HashMap::new();
        for m in meshes.iter_mut() {
            let Some(mat) = m.mat.as_ref() else { continue };
            for (slot, name) in &mat.tex_slots {
                let key = name.to_ascii_lowercase();
                if m.shader_extra_textures.contains_key(slot) { continue; }
                if replay { if !crate::oklib::has_record(self.lib.as_ref(), "cube_atlas_named", key.as_str()) { continue; } }
                else if !cubes.contains(&key) { continue; }
                let atlas = atlases.entry(key.clone()).or_insert_with(|| self.memo("cube_atlas_named", key.as_str(), || self.cubemap_atlas_named(name)));
                if let Some(png) = atlas { m.shader_extra_textures.insert(slot.clone(), png.clone()); }
            }
        }
    }

    /// Records `cube_atlas_named` for every cube slot of this bundle's renderer materials (models converted before cube
    /// slots were sampled; `studio-cubes`). Returns how many slots named a cubemap.
    pub(crate) fn record_cube_slot_atlases(&self) -> usize {
        let cubes = self.cubemap_names();
        if cubes.is_empty() { return 0; }
        let maps = self.resolved_material_maps();
        let mut names: std::collections::BTreeMap<String, String> = Default::default();
        for mat in maps.1.values().flatten() {
            for (_, name) in &mat.tex_slots { let key = name.to_ascii_lowercase(); if cubes.contains(&key) { names.insert(key, name.clone()); } }
        }
        for (key, name) in &names { self.memo("cube_atlas_named", key.as_str(), || self.cubemap_atlas_named(name)); }
        names.len()
    }

    /// Cubemap `name` as the reflection atlas PNG (`cubemap_atlas`): this bundle's, else a loaded dependency file's.
    pub(crate) fn cubemap_atlas_named(&self, name: &str) -> Option<Vec<u8>> {
        let find = |ser: &unity_asset_binary::asset::SerializedFile, streams: &dyn Fn(&UnityObject) -> Option<unity_asset_decode::texture::Texture2D>| {
            ser.objects_of_type(89).into_iter().find_map(|info| {
                let object = ser.scene_object(info).ok()?;
                if !object.name().is_some_and(|n| n.eq_ignore_ascii_case(name)) { return None; }
                cubemap_atlas(&streams(&object)?).map(|(_, _, png)| png)
            })
        };
        if let Some(png) = self.with_texture_sources(|sers| sers.iter().find_map(|(_, ser)| find(ser, &|o| read_texture_payload(o, &self.files, None)))) {
            return Some(png);
        }
        let externals: Vec<std::rc::Rc<ExternalFile>> = self.external_files.borrow().values().flatten().cloned().collect();
        externals.iter().find_map(|file| find(&file.0, &|o| read_texture_payload(o, &file.1, None)))
    }

    /// The serialized files (with their index in `files`) textures are looked up in: the parsed files' metadata,
    /// shared with `with_serialized` (a second parse of every file's metadata used to be kept for textures).
    fn with_texture_sources<R>(&self,f:impl FnOnce(&[(usize,&unity_asset_binary::asset::SerializedFile)])->R)->R {
        self.library_internals("ParsedBundle::with_texture_sources");
        self.serialized_files(|sers| {
            let sources:Vec<(usize,&unity_asset_binary::asset::SerializedFile)>=
                self.serialized_file_index.borrow().iter().copied().zip(sers.iter()).collect();
            f(&sources)
        })
    }

    fn texture_object(&self,index:usize,ser:&unity_asset_binary::asset::SerializedFile,
        info:&unity_asset_binary::asset::ObjectInfo)->Option<UnityObject> {
        let bytes=self.files.get(index)?.1.read_range(info.byte_start,info.byte_size)?;
        UnityObject::from_serialized_object_bytes(ser,info,bytes).ok()
    }

    fn exact_texture_location(&self,name:&str)->Option<TextureObjectLocation> {
        let key=name.to_ascii_lowercase();
        // One resolution rule whatever was queried before (container alias,
        // else the first object with that name): results never depend on
        // call order, which a recorded OK model could not reproduce.
        if let Some(location)=self.tex_exact_objects.borrow().get(&key){return *location;}
        if self.tex_asset_objects.borrow().is_none(){
            let objects=self.with_texture_sources(|files|{
                let mut objects=HashMap::new();
                for (index,file) in files {for info in file.objects_of_type(142){
                    let Some(object)=self.texture_object(*index,file,info) else{continue};
                    let Some(container)=object.class.properties().get("m_Container") else{continue};
                    for row in uv_arr(container){
                        use unity_asset_core::UnityValue;
                        let pair=match row{UnityValue::Array(a) if a.len()==2=>Some((&a[0],&a[1])),UnityValue::Object(m)=>m.get("first").zip(m.get("second")),_=>None};
                        let Some((asset_name,value))=pair else{continue};
                        let Some(asset_name)=asset_name.as_str() else{continue};
                        let Some(asset)=uv_obj(value,"asset") else{continue};
                        if uv_obj(asset,"m_FileID").and_then(|v|v.as_i64()).unwrap_or(0)!=0{continue;}
                        let Some(path)=uv_pptr(asset) else{continue};
                        if !file.objects.iter().any(|i|i.path_id==path&&i.type_id==28){continue;}
                        let normalized=asset_name.replace('\\',"/").to_ascii_lowercase();
                        let leaf=normalized.rsplit('/').next().unwrap_or(&normalized);
                        let stem=Path::new(leaf).file_stem().and_then(|s|s.to_str()).unwrap_or(leaf);
                        for key in [leaf,stem]{
                            let entry=objects.entry(key.to_string()).or_insert(Some((*index,path)));
                            if *entry!=Some((*index,path)){*entry=None;}
                        }
                    }
                }}objects
            });
            *self.tex_asset_objects.borrow_mut()=Some(objects);
        }
        let asset=self.tex_asset_objects.borrow().as_ref().and_then(|m|m.get(&key)).copied().flatten();
        let asset=asset.filter(|(index,path)|self.with_texture_sources(|files|{
            let Some((_,file))=files.iter().find(|(i,_)|i==index) else{return false};
            let Some(info)=file.objects_of_type(28).into_iter().find(|i|i.path_id==*path) else{return false};
            let bytes=self.files[*index].1.read_range(info.byte_start,info.byte_size.min(256));
            bytes.as_deref().and_then(|b|Self::texture_header_fields(file,info,b,&["m_Name"]))
                .or_else(||self.texture_object(*index,file,info))
                .is_some_and(|o|o.name().is_some_and(|n|n.eq_ignore_ascii_case(name)))
        }));
        if asset.is_none()&&self.tex_name_objects.borrow().is_none(){
            let started=std::time::Instant::now();
            let objects=self.with_texture_sources(|files|{
                let mut objects=HashMap::new();
                for (index,file) in files {for info in file.objects_of_type(28){
                    let bytes=self.files[*index].1.read_range(info.byte_start,info.byte_size.min(256));
                    let object=bytes.as_deref().and_then(|b|Self::texture_header_fields(file,info,b,&["m_Name"]))
                        .or_else(||self.texture_object(*index,file,info));
                    if let Some(object)=object{let name=object.name().unwrap_or_default();
                        objects.entry(name.to_ascii_lowercase()).and_modify(|v:&mut (usize,i64,String,bool)|v.3=true).or_insert((*index,info.path_id,name,false));}
                }}objects
            });
            if std::env::var_os("OK_APPEARANCE_PROFILE").is_some(){eprintln!("APPEARANCE_PROFILE phase=texture_name_index count={} ms={:.3}",objects.len(),started.elapsed().as_secs_f64()*1000.);}
            *self.tex_name_objects.borrow_mut()=Some(objects);
        }
        let location=asset.or_else(||self.tex_name_objects.borrow().as_ref().and_then(|m|m.get(&key)).map(|v|(v.0,v.1)));
        let result=location.and_then(|(index,path)|self.with_texture_sources(|files|{
            let (_,file)=files.iter().find(|(i,_)|*i==index)?;
            let info=file.objects_of_type(28).into_iter().find(|i|i.path_id==path)?;
            let bytes=self.files[index].1.read_range(info.byte_start,info.byte_size.min(4096));
            let object=bytes.as_deref().and_then(|b|Self::texture_header(file,info,b)).or_else(||self.texture_object(index,file,info))?;
            let props=object.class.properties();
            let color_space=props.get("m_ColorSpace").and_then(|v|v.as_i64()).map(|v|v as i32)
                .or_else(||parse_tex_unity2019(object.raw_data()).map(|t|t.color_space));
            let sampling=props.get("m_TextureSettings").map(|settings|{
                let value=|key:&str|uv_obj(settings,key).and_then(|v|v.as_i64()).map(|v|v as i32);
                let wrap=value("m_WrapMode").unwrap_or(0);
                TextureSampling{filter:value("m_FilterMode").unwrap_or(1),wrap_u:value("m_WrapU").unwrap_or(wrap),
                    wrap_v:value("m_WrapV").unwrap_or(wrap),srgb:props.get("m_ColorSpace").and_then(|v|v.as_i64()).unwrap_or(1)!=0,
                    mip_count:props.get("m_MipCount").and_then(|v|v.as_i64()).unwrap_or(1) as i32}
            });
            Some(TextureObjectLocation{file:index,path_id:path,color_space,sampling})
        }));
        self.tex_exact_objects.borrow_mut().insert(key,result);result
    }

    fn ensure_tex_catalog(&self) {
        if self.tex_objects.borrow().is_some() {
            return;
        }
        let started=std::time::Instant::now();
        let (names,objects,complete) = self.with_texture_sources(|sers| {
            let mut names = Vec::new();
            let mut objects=HashMap::new();
            let mut complete=sers.len()==self.files.iter().filter(|(n,_)|{let n=n.to_ascii_lowercase();!n.ends_with(".ress")&&!n.ends_with(".resource")}).count();
            for (file_index,ser) in sers {
                for info in ser.objects_of_type(28) {
                    let header=self.files[*file_index].1.read_range(info.byte_start,info.byte_size.min(4096));
                    let Some(object) = header.as_deref().and_then(|b|Self::texture_header(ser,info,b))
                        .or_else(||self.texture_object(*file_index,ser,info)) else {
                        complete=false;continue;
                    };
                    let n = object.name().unwrap_or_default();
                    let props = object.class.properties();
                    let color_space=props.get("m_ColorSpace").and_then(|v|v.as_i64()).map(|v|v as i32)
                        .or_else(||parse_tex_unity2019(object.raw_data()).map(|t|t.color_space));
                    let sampling=props.get("m_TextureSettings").map(|settings| {
                        let value=|key:&str|uv_obj(settings,key).and_then(|v|v.as_i64()).map(|v|v as i32);
                        let wrap=value("m_WrapMode").unwrap_or(0);
                        TextureSampling{filter:value("m_FilterMode").unwrap_or(1),
                            wrap_u:value("m_WrapU").unwrap_or(wrap),wrap_v:value("m_WrapV").unwrap_or(wrap),
                            srgb:props.get("m_ColorSpace").and_then(|v|v.as_i64()).unwrap_or(1)!=0,
                            mip_count:props.get("m_MipCount").and_then(|v|v.as_i64()).unwrap_or(1) as i32}
                    });
                    objects.entry(n.to_ascii_lowercase()).or_insert(TextureObjectLocation{
                        file:*file_index,path_id:info.path_id,color_space,sampling});
                    if n.is_empty() {continue;}
                    let w = props.get("m_Width").and_then(|v| v.as_i64()).unwrap_or(0) as u32;
                    let h = props.get("m_Height").and_then(|v| v.as_i64()).unwrap_or(0) as u32;
                    names.push((n, w.saturating_mul(h.max(1))));
                    // The old exact lookup returned the first object whose
                    // name matched case-insensitively. Preserve that ordering
                    // even when several serialized files reuse a texture name.
                }
            }
            (names,objects,complete)
        });
        if std::env::var_os("OK_APPEARANCE_PROFILE").is_some() {eprintln!("APPEARANCE_PROFILE phase=texture_catalog count={} ms={:.3}",names.len(),started.elapsed().as_secs_f64()*1000.);}
        *self.tex_names.borrow_mut() = names;
        *self.tex_objects.borrow_mut() = Some(objects);
        self.tex_catalog_complete.set(complete);
    }

    /// Decoded textures are kept for reuse, up to TEXTURE_CACHE_BYTES per bundle: a parsed bundle stays in the bundle
    /// cache, and OK Studio scenes decode a new map's worth of textures each, which piled up gigabytes.
    fn remember_texture(&self, key: String, png: Vec<u8>) {
        const TEXTURE_CACHE_BYTES: usize = 192 << 20;
        let mut cache = self.textures.borrow_mut();
        if self.texture_bytes.get() + png.len() > TEXTURE_CACHE_BYTES { cache.clear(); self.texture_bytes.set(0); }
        self.texture_bytes.set(self.texture_bytes.get() + png.len());
        if let Some(old) = cache.insert(key, png) { self.texture_bytes.set(self.texture_bytes.get().saturating_sub(old.len())); }
    }

    fn decode_tex(&self, name: &str) -> Option<Vec<u8>> {
        let key = name.to_ascii_lowercase();
        if let Some(png) = self.textures.borrow().get(&key) {
            return Some(png.clone());
        }
        if let (Some(e),Some(id))=(self.model_edits(),self.lib) {
            if let Some(png)=crate::okmodel_edit::texture_png(&crate::oklib::active()?.dir,&id,&e,name) {
                self.remember_texture(key,png.clone());
                return Some(png);
            }
        }
        if crate::oklib::memo_active() {
            // OK texture: the texels are the model data, the PNG is derived. A library model is content-addressed (and
            // unedited here), so the PNG of one of its textures is the same in every process: kept on disk.
            // Not for a card's own model while a pack is recorded or an OK card opens strictly (okstrict.rs): a PNG kept on
            // disk would stand in for the texels the card must carry itself.
            let own=self.lib.is_some_and(|id|crate::oklib::recording_card_pack()||(crate::okstrict::active().is_some()&&!crate::okstrict::library_model(&id)));
            let disk=!crate::memosnap::active(crate::memosnap::TEX)&&!own;
            if let Some((n,png))=self.lib.filter(|_|disk).and_then(|id|derived_png_load(&id,&key)) {
                self.remember_texture(n.to_ascii_lowercase(),png.clone());
                return Some(png);
            }
            let payload=self.memo_texture("texture_payload",name,||self.texture_payload_unity(name).map(|(t,n)|(TexturePayload(t),n))).map(|(t,n)|(t.0,n));
            let Some((n,png))=payload.and_then(|p|png_from_payload(p.0,None)) else { return self.dependency_texture_png(name); };
            if let Some(id)=self.lib.filter(|_|disk) { derived_png_save(&id,&key,&n,&png); }
            self.remember_texture(n.to_ascii_lowercase(),png.clone());
            return Some(png);
        }
        let Some(location)=self.exact_texture_location(name) else { return self.dependency_texture_png(name); };
        let found = self.with_texture_sources(|sers| {
            let (_,ser)=sers.iter().find(|(index,_)|*index==location.file)?;
            let info=ser.objects_of_type(28).into_iter().find(|info|info.path_id==location.path_id)?;
            let object=self.texture_object(location.file,ser,info)?;
            png_from_texture(&object,&self.files,None)
        });
        if let Some((n, png)) = found {
            self.remember_texture(n.to_ascii_lowercase(), png.clone());
            return Some(png);
        }
        None
    }

    fn texture_payload_unity(&self, name: &str) -> Option<(unity_asset_decode::texture::Texture2D, String)> {
        let location=self.exact_texture_location(name)?;
        self.with_texture_sources(|sers| {
            let (_,ser)=sers.iter().find(|(index,_)|*index==location.file)?;
            let info=ser.objects_of_type(28).into_iter().find(|info|info.path_id==location.path_id)?;
            let object=self.texture_object(location.file,ser,info)?;
            let mut tex=read_texture_payload(&object,&self.files,None)?;
            let object_name=object.name().unwrap_or_else(||"tex".into());
            if tex.name.is_empty(){tex.name=object_name.clone();}
            Some((tex,object_name))
        })
    }

    /// Mesh textures as references to the source's OK textures: a field is
    /// replaced by `Some(empty)` + a plan entry only when re-deriving it gives
    /// the identical bytes, so restoring is exact.
    fn derive_mesh_texture(&self, name: &str, transform: u8) -> Option<Vec<u8>> {
        let png = crate::oklib::timed("#derive decode", || self.decode_tex(name))?;
        match transform {
            0 => Some(png),
            1 => Some(crate::orient_raw_mask(png)),
            2 => Some(unpack_illusion_albedo(&png).unwrap_or(png)),
            3 => {
                if self.texture_sampling_named(name).is_some() {
                    if let Some(texture) = self.texture_gpu_named(name) {
                        if let Ok(read) = crate::clothes_gpu::texture_readback(texture) { return Some(read); }
                    }
                }
                Some(png)
            }
            _ => None,
        }
    }
    fn mesh_texture_fields(m: &mut ExtractedMesh) -> Vec<(String, &mut Option<Vec<u8>>)> {
        vec![("texture_png".into(), &mut m.texture_png), ("color_mask_png".into(), &mut m.color_mask_png),
            ("alpha_mask_png".into(), &mut m.alpha_mask_png), ("detail_mask_png".into(), &mut m.detail_mask_png),
            ("line_mask_png".into(), &mut m.line_mask_png), ("normal_map_png".into(), &mut m.normal_map_png),
            ("normal_mask_png".into(), &mut m.normal_mask_png), ("hair_gloss_png".into(), &mut m.hair_gloss_png)]
    }
    fn mesh_texture_candidates(m: &ExtractedMesh, field: &str) -> Vec<(String, u8)> {
        let slot = |p: &str| m.mat.as_ref().and_then(|mat| mat.slot(p)).map(str::to_owned);
        let mut out = Vec::new();
        let mut add = |name: Option<String>, t: u8| if let Some(n) = name { out.push((n, t)); };
        match field {
            "texture_png" => { add(slot("_MainTex"), 0); add((!m.texture_name.is_empty()).then(|| m.texture_name.clone()), 0); add((!m.texture_name.is_empty()).then(|| m.texture_name.clone()), 2); }
            "color_mask_png" => add(slot("_ColorMask"), 0),
            "alpha_mask_png" => add(slot("_AlphaMask"), 1),
            "detail_mask_png" => add(slot("_DetailMask"), 1),
            "line_mask_png" => add(slot("_LineMask"), 1),
            "normal_map_png" => { add(slot("_NormalMap"), 3); add(slot("_NormalMap"), 0); }
            "normal_mask_png" => add(slot("_NormalMask"), 1),
            "hair_gloss_png" => add(slot("_HairGloss"), 1),
            extra => add(extra.strip_prefix("extra:").and_then(slot), 0),
        }
        out
    }
    pub(crate) fn strip_mesh_textures(&self, meshes: &mut [ExtractedMesh]) {
        for m in meshes.iter_mut() {
            let mut plan = Vec::new();
            let fields: Vec<String> = Self::mesh_texture_fields(m).into_iter().map(|(f, _)| f).collect();
            let extras: Vec<String> = m.shader_extra_textures.keys().map(|k| format!("extra:{k}")).collect();
            for field in fields.into_iter().chain(extras) {
                let candidates = Self::mesh_texture_candidates(m, &field);
                let current = if let Some(prop) = field.strip_prefix("extra:") { m.shader_extra_textures.get(prop).cloned() }
                    else { Self::mesh_texture_fields(m).into_iter().find(|(f, _)| *f == field).and_then(|(_, v)| v.clone()) };
                let Some(current) = current.filter(|c| !c.is_empty()) else { continue };
                let Some((name, t)) = candidates.into_iter().find(|(n, t)| self.derive_mesh_texture(n, *t).as_deref() == Some(current.as_slice())) else { continue };
                plan.push((field.clone(), name, t));
                if let Some(prop) = field.strip_prefix("extra:") { m.shader_extra_textures.insert(prop.to_owned(), Vec::new()); }
                else if let Some((_, v)) = Self::mesh_texture_fields(m).into_iter().find(|(f, _)| *f == field) { *v = Some(Vec::new()); }
            }
            m.texture_plan = plan;
        }
    }
    pub(crate) fn restore_mesh_textures(&self, meshes: &mut [ExtractedMesh]) {
        for m in meshes.iter_mut() {
            for (field, name, t) in std::mem::take(&mut m.texture_plan) {
                let png = self.derive_mesh_texture(&name, t);
                if let Some(prop) = field.strip_prefix("extra:") {
                    match png { Some(p) => { m.shader_extra_textures.insert(prop.to_owned(), p); } None => { m.shader_extra_textures.remove(prop); } }
                } else if let Some((_, v)) = Self::mesh_texture_fields(m).into_iter().find(|(f, _)| *f == field) { *v = png; }
            }
        }
    }
    /// (name, png) lists: an entry equal to the named OK texture keeps the name only.
    pub(crate) fn strip_named_textures(&self, list: &mut [(String, Vec<u8>)]) {
        for (n, png) in list.iter_mut() { if self.decode_tex(n).as_deref() == Some(png.as_slice()) { png.clear(); } }
    }
    pub(crate) fn restore_named_textures(&self, list: &mut Vec<(String, Vec<u8>)>) {
        for (n, png) in list.iter_mut() { if png.is_empty() { if let Some(p) = self.decode_tex(n) { *png = p; } } }
        list.retain(|(_, png)| !png.is_empty());
    }

    /// Editor overlay on a prefab's meshes: hidden meshes, vertex transforms,
    /// material values and texture slots (fields re-derived from the slot).
    fn apply_mesh_edits(&self, edits: &crate::okmodel_edit::ModelEdits, main_data: &str, meshes: &mut Vec<ExtractedMesh>) {
        let prefab = main_data.to_ascii_lowercase();
        meshes.retain(|m| !edits.meshes.get(&format!("{prefab}/{}", m.name.to_ascii_lowercase())).is_some_and(|e| e.hidden));
        for m in meshes.iter_mut() {
            if let Some(e) = edits.meshes.get(&format!("{prefab}/{}", m.name.to_ascii_lowercase())) {
                crate::okmodel_edit::transform_mesh(e, &mut m.positions, &mut m.normals);
            }
            let Some(mat) = m.mat.as_mut() else { continue };
            let Some(me) = edits.materials.get(&mat.name.to_ascii_lowercase()).cloned() else { continue };
            crate::okmodel_edit::apply_material(edits, mat);
            for (slot, tex) in &me.textures {
                let (field, transform) = match slot.as_str() {
                    "_MainTex" => ("texture_png", 0), "_ColorMask" => ("color_mask_png", 0), "_AlphaMask" => ("alpha_mask_png", 1),
                    "_DetailMask" => ("detail_mask_png", 1), "_LineMask" => ("line_mask_png", 1), "_NormalMap" => ("normal_map_png", 3),
                    "_NormalMask" => ("normal_mask_png", 1), "_HairGloss" => ("hair_gloss_png", 1), _ => ("", 0),
                };
                let png = self.derive_mesh_texture(tex, transform);
                if field.is_empty() {
                    match png { Some(p) => { m.shader_extra_textures.insert(slot.clone(), p); } None => { m.shader_extra_textures.remove(slot); } }
                    continue;
                }
                if field == "texture_png" { m.texture_name = tex.clone(); }
                if let Some((_, v)) = Self::mesh_texture_fields(m).into_iter().find(|(f, _)| f == field) { *v = png; }
            }
        }
    }

    fn texture_color_space(&self, name: &str) -> Option<i32> {
        self.memo_texture("texture_color_space", name, || self.exact_texture_location(name)?.color_space)
    }

    pub(crate) fn library_texture_color_space(&self, name: &str) -> Option<i32> { self.texture_color_space(name) }

    fn texture_directory_unity(&self) -> TexDirectory {
        self.ensure_tex_catalog();
        let catalog=self.tex_names.borrow().clone();
        // Build the alias and name indexes (an impossible key forces both).
        let _=self.exact_texture_location("\u{0}");
        let names:std::collections::BTreeMap<String,(String,bool)>=self.tex_name_objects.borrow().as_ref()
            .map(|m|m.iter().map(|(k,v)|(k.clone(),(v.2.clone(),v.3))).collect()).unwrap_or_default();
        let by_location:HashMap<(usize,i64),String>=self.tex_name_objects.borrow().as_ref()
            .map(|m|m.values().map(|v|((v.0,v.1),v.2.clone())).collect()).unwrap_or_default();
        let aliases:Vec<String>=self.tex_asset_objects.borrow().as_ref()
            .map(|m|m.iter().filter(|(_,v)|v.is_some()).map(|(k,_)|k.clone()).collect()).unwrap_or_default();
        let mut assets=std::collections::BTreeMap::new();
        let mut exact=std::collections::BTreeMap::new();
        let mut keys:std::collections::BTreeSet<String>=names.keys().cloned().collect();
        keys.extend(aliases.iter().cloned());
        keys.extend(catalog.iter().map(|(n,_)|n.to_ascii_lowercase()));
        for key in keys {
            let Some(location)=self.exact_texture_location(&key) else {continue};
            // An exact lookup only resolves objects named like the key; a
            // duplicated name's other object is absent from the first-object map.
            let name=by_location.get(&(location.file,location.path_id)).cloned()
                .or_else(||catalog.iter().find(|(n,_)|n.eq_ignore_ascii_case(&key)).map(|(n,_)|n.clone()))
                .unwrap_or_else(||key.clone());
            if aliases.contains(&key) {assets.insert(key.clone(),name.clone());}
            exact.insert(key,name);
        }
        TexDirectory{catalog,complete:self.tex_catalog_complete.get(),names,assets,exact}
    }

    fn prefab_directory_unity(&self) -> PrefabDirectory {
        self.with_serialized(|files| {
            let mut dir=PrefabDirectory::default();
            let file_index=self.serialized_file_index.borrow().clone();
            for (file,index) in files.iter().zip(file_index) {
                let roots=prefab_asset_roots(file);
                let mut objects=std::collections::BTreeSet::new();
                let mut go_names=HashMap::new();
                for info in file.objects_of_type(1) {
                    if let Ok(obj)=file.scene_object(info) {
                        let name=obj.name().unwrap_or_default();
                        objects.insert(name.to_ascii_lowercase());go_names.insert(info.path_id,name);
                    }
                }
                for info in file.objects_of_type(4) {
                    let Ok(obj)=file.scene_object(info) else {continue};
                    let p=obj.class.properties();
                    if p.get("m_Father").and_then(uv_pptr).unwrap_or(0)==0 {
                        if let Some(name)=p.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_names.get(&go)) {dir.top.push(name.clone());}
                    }
                }
                for info in file.objects_of_type(MESH_CLASS_ID) {
                    // Only the name, which leads the object (a whole mesh parse is its vertex and index arrays).
                    let head=self.files[index].1.read_range(info.byte_start,info.byte_size.min(1024));
                    let obj=head.as_deref().and_then(|b|Self::texture_header_fields(file,info,b,&["m_Name"]))
                        .or_else(||UnityObject::from_serialized_file(file,&info).ok());
                    if let Some(obj)=obj {dir.meshes.insert(obj.name().unwrap_or_default().to_ascii_lowercase());}
                }
                dir.roots.push((!roots.is_empty()).then(||roots.keys().cloned().collect()));
                dir.objects.push(objects);
            }
            dir.top.sort();dir.top.dedup();
            dir
        })
    }

    fn attach_picked(&self, picked: &mut [ExtractedMesh]) {
        if picked.is_empty() {
            return;
        }
        self.ensure_tex_catalog();
        let catalog = self.tex_names.borrow().clone();
        for m in picked.iter_mut() {
            if m.texture_png.is_some() {
                continue;
            }
            let best = catalog
                .iter()
                .filter(|(n, _)| !is_aux_tex(n))
                .map(|(n, px)| (n, score_texture(&m.name, "", n, *px as usize)))
                .max_by_key(|(_, s)| *s);
            // Name match only. Do not fall back to the largest texture ??
            // hair bundles pack ColorMask/DetailMask (`_mc`/`_md`) that are
            // bigger than albedo and would paint the mesh green.
            let tn = match best {
                Some((n, score)) if score >= 10_000_000 => Some(n.clone()),
                _ => None,
            };
            let Some(tn) = tn else {
                continue;
            };
            if let Some(png) = self.decode_tex(&tn) {
                m.texture_name = tn;
                if let Some(unpacked) = unpack_illusion_albedo(&png) {
                    m.texture_png = Some(unpacked);
                } else {
                    m.texture_png = Some(png);
                }
            }
        }
    }

    /// Bind `_MainTex` / `_ColorMask` from the SMR/MeshRenderer material.
    fn resolved_material_maps(&self) -> std::rc::Rc<MaterialMaps> {
        let cached = self.material_maps.borrow().clone();
        let maps = match cached { Some(m) => m, None => {
            let m = std::rc::Rc::new(self.with_serialized(|sers| {
            let mut tex_id_name: HashMap<i64, String> = HashMap::new();
            let mut tex_id_wrap: HashMap<i64, i32> = HashMap::new();
            let mut mat_info: HashMap<i64, MeshMaterial> = HashMap::new();
            let mut mesh_mat: HashMap<i64, Vec<MeshMaterial>> = HashMap::new();
            let mut renderer_mat: HashMap<i64, Vec<MeshMaterial>> = HashMap::new();
            // Materials of dependency bundles, per external path (parsed once), and where each one came from.
            let mut external: HashMap<String, HashMap<i64, MeshMaterial>> = HashMap::new();
            // Texture name -> the external file it is decoded from.
            let mut texture_source: HashMap<String, String> = HashMap::new();
            let mut external_name = |ext: &str, path_id: i64| self.external_texture_name(ext, path_id);
            for ser in sers {
                mat_info.extend(materials_in(ser, &mut tex_id_name, &mut tex_id_wrap,
                    &mut TextureSources { own: None, external_name: &mut external_name, source: &mut texture_source }));
                // A renderer's material: this file's, or (FileID > 0) one of a dependency bundle.
                let mut material = |v: &unity_asset_core::UnityValue| -> Option<MeshMaterial> {
                    let p = references::PPtr::read(v)?;
                    if p.file == 0 { return mat_info.get(&p.path).cloned(); }
                    let ext_path = ser.externals.get(p.file as usize - 1)?.path.clone();
                    let mats = external.entry(ext_path.to_ascii_lowercase()).or_insert_with(|| match self.external_file(&ext_path) {
                        Some(file) => materials_in(&file.0, &mut HashMap::new(), &mut HashMap::new(),
                            &mut TextureSources { own: Some(&ext_path), external_name: &mut external_name, source: &mut texture_source }),
                        None => HashMap::new(),
                    });
                    mats.get(&p.path).cloned()
                };
                // SkinnedMeshRenderer (137) has m_Mesh + m_Materials.
                for info in ser.objects_of_type(137) {
                    let Ok(object) = ser.scene_object(info) else {
                        continue;
                    };
                    let props = object.class.properties();
                    let Some(mesh_id) = props.get("m_Mesh").and_then(pptr_path_id) else {
                        continue;
                    };
                    let mats: Vec<MeshMaterial> = props
                        .get("m_Materials")
                        .and_then(|m| m.as_array())
                        .map(|a| a.iter().filter_map(&mut material).collect())
                        .unwrap_or_default();
                    if !mats.is_empty() {
                        renderer_mat.insert(object.path_id(), mats.clone());
                        mesh_mat.entry(mesh_id).or_insert(mats);
                    }
                }
                // MeshRenderer (23) has materials; MeshFilter (33) has m_Mesh.
                let mut go_mesh: HashMap<i64, i64> = HashMap::new();
                let mut go_mat: HashMap<i64, (i64, Vec<MeshMaterial>)> = HashMap::new();
                for info in ser.objects_of_type(33) {
                    let Ok(object) = ser.scene_object(info) else {
                        continue;
                    };
                    let props = object.class.properties();
                    let Some(go) = props.get("m_GameObject").and_then(pptr_path_id) else {
                        continue;
                    };
                    if let Some(mesh_id) = props.get("m_Mesh").and_then(pptr_path_id) {
                        go_mesh.insert(go, mesh_id);
                    }
                }
                for info in ser.objects_of_type(23) {
                    let Ok(object) = ser.scene_object(info) else {
                        continue;
                    };
                    let props = object.class.properties();
                    let Some(go) = props.get("m_GameObject").and_then(pptr_path_id) else {
                        continue;
                    };
                    if let Some(mats) = props
                        .get("m_Materials")
                        .and_then(|m| m.as_array())
                        .map(|a| a.iter().filter_map(&mut material).collect::<Vec<_>>())
                    {
                        go_mat.insert(go, (object.path_id(), mats));
                    }
                }
                let mut go_ids: Vec<i64> = go_mesh.keys().copied().collect();
                go_ids.sort_unstable();
                for go in go_ids {
                    let Some(mesh_id) = go_mesh.get(&go) else {
                        continue;
                    };
                    let Some((renderer_id, mats)) = go_mat.get(&go) else {
                        continue;
                    };
                    let mats = mats.clone();
                    if !mats.is_empty() {
                        renderer_mat.insert(*renderer_id, mats.clone());
                        mesh_mat.entry(*mesh_id).or_insert(mats);
                    }
                }
            }
            (mesh_mat, renderer_mat, texture_source)
        }));
            self.dependency_texture_source.borrow_mut().extend(m.2.iter().map(|(k, v)| (k.clone(), v.clone())));
            *self.material_maps.borrow_mut() = Some(m.clone()); m } };
        maps
    }

    pub(crate) fn standard_material_texture_dependencies(&self) -> std::collections::BTreeSet<String> {
        let maps=self.resolved_material_maps();
        maps.1.values().flatten().filter(|m|m.shader.starts_with("Standard"))
            .flat_map(|m|["_MainTex","_BumpMap","_MetallicGlossMap","_EmissionMap"].into_iter().filter_map(|slot|m.slot(slot).map(str::to_owned)))
            .collect()
    }

    fn attach_material_texs(&self, picked: &mut Vec<ExtractedMesh>) {
        if picked.is_empty() { return; }
        let maps = self.resolved_material_maps();
        let (mesh_mats, renderer_mats) = (&maps.0, &maps.1);

        let mut expanded = Vec::with_capacity(picked.len());
        for m in std::mem::take(picked) {
            // One Unity Mesh can be referenced by several renderer objects with
            // different materials. Resolve the selected prefab renderer first;
            // a mesh-id map alone makes the last unrelated renderer win.
            let mats = m.renderer_ids.iter().find_map(|id| renderer_mats.get(id))
                .or_else(|| mesh_mats.get(&m.path_id));
            let Some(mats) = mats else {
                expanded.push(m);
                continue;
            };
            if m.submesh_ranges.len() > 1 && mats.len() > 1 {
                for (i, &(start, count, base)) in m.submesh_ranges.iter().enumerate() {
                    let end = start.saturating_add(count).min(m.indices.len());
                    if start >= end { continue; }
                    let mut part = m.clone();
                    part.indices = m.indices[start..end].iter().map(|v| v.saturating_add(base)).collect();
                    part.submesh_index = i;
                    part.mat = mats.get(i).or_else(|| mats.last()).cloned();
                    expanded.push(part);
                }
            } else {
                let mut part = m;
                part.mat = mats.first().cloned();
                expanded.push(part);
            }
        }
        *picked = expanded;
        for m in picked.iter_mut() {
            // Studio map stage effects (Shader Forge light cones, beams, lasers, LED panels) are drawn by OK's own
            // material: their texture slots, and the kind in `_OKStageEffect`.
            if let Some(kind)=m.mat.as_ref().and_then(|mat|stage_effect_kind(&mat.shader)) {
                let slots: &[&str]=if kind==5 {&["_Emission","_MaskFast","_MaskSlow","_MaskFull"]} else {&["_color","_mask"]};
                for property in slots {
                    if let Some(png)=m.mat.as_ref().and_then(|mat|mat.slot(property)).and_then(|name|self.decode_tex(name)) {
                        m.shader_extra_textures.insert((*property).into(),png);
                    }
                }
                if let Some(mat)=m.mat.as_mut() { mat.floats.insert("_OKStageEffect".into(),kind as f32); }
            }
            // Unity's Skybox/Cubemap on a mesh (Studio's sky dome items): `_Tex` as a cube atlas (`_OKSkyCube`, the
            // reflection atlas layout), drawn by OK's own material, which samples it along the object-space vertex.
            if m.mat.as_ref().is_some_and(|mat| mat.shader == "Skybox/Cubemap") {
                if let Some(png) = m.mat.as_ref().and_then(|mat| mat.slot("_Tex")).and_then(|name| self.cubemap_atlas_named(name)) {
                    m.shader_extra_textures.insert("_OKSkyCube".into(), png);
                    if let Some(mat) = m.mat.as_mut() { mat.floats.insert("_OKSkybox".into(), 1.0); }
                    // unity_ColorSpaceDouble is baked into the program when it is compiled: 4.5948 in a linear project,
                    // 2 in a gamma one. A mod's own copy of the shader keeps the colour space it was built in (nashi's
                    // studio skybox: 2, so its sky is 2.3x darker than the built-in program would draw it).
                    let double = self.memo("skybox_color_space_double", "Skybox/Cubemap", || self.shader_pixel_dxbc_named("Skybox/Cubemap").map(|dxbc| color_space_double(&dxbc)));
                    if let (Some(d), Some(mat)) = (double, m.mat.as_mut()) { mat.floats.insert("_OKSkyDouble".into(), d); }
                    // The object-space direction the program samples along, kept as vertex colour (d繚0.5+0.5): the
                    // positions get the renderer transform baked in later.
                    m.vertex_colors = m.positions.iter().map(|p| {
                        let l = (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]).sqrt().max(1e-6);
                        let c = |v: f32| ((v / l * 0.5 + 0.5) * 255.0).round().clamp(0.0, 255.0) as u8;
                        [c(p[0]), c(p[1]), c(p[2]), 255]
                    }).collect();
                }
            }
            // Xffect/heat_distortion_t2 (Studio's 瘞湧 items): a GrabPass then SrcAlpha/OneMinusSrcAlpha of
            // grab繚_MainTex.rgb with alpha _MainTex.a, i.e. the screen behind times lerp(1, _MainTex.rgb, _MainTex.a)
            // (plus a small _HeatForce ripple). OK draws it as that multiply (`_OKModulate`).
            if m.mat.as_ref().is_some_and(|mat| mat.shader.to_ascii_lowercase().starts_with("xffect/heat_distortion")) {
                if let Some(mat) = m.mat.as_mut() { mat.floats.insert("_OKModulate".into(), 1.0); }
            }
            let Some(mat) = m.mat.as_ref() else { continue; };
            // MDK's Standard-style slots are not character _NormalMap slots.
            // Record dependency-model identities and sampler provenance along
            // with the PNG so base-library playback can resolve them alone.
            if mat.shader == "Shader Forge/main_StandardMDK_studio" {
                for property in ["_BumpMap", "_ParallaxMap", "_MetallicGlossMap", "_EmissionMap"] {
                    if let Some(name) = mat.slot(property) {
                        if let Some(png) = self.decode_tex(name) {
                            m.shader_extra_textures.insert(property.into(), png);
                            if let Some(s) = self.texture_sampling_named(name) {
                                m.runtime_textures.insert(property.into(), [s.filter, s.wrap_u, s.wrap_v, i32::from(s.srgb), s.mip_count]);
                            }
                        }
                    }
                }
            }
            // Capture the whole material for downstream tint/blend/slot use.
            for property in ["_NormalMapDetail","_AnotherRamp","_EmissionMask","_EmissionMap","_ReflectionMapCap","_ReflectMapDetail","_Texture2","_Texture3","_liquidmask","_overtex1","_overtex2","_overtex3"] {
                if let Some(name)=mat.slot(property) {
                    if let Some(png)=self.decode_tex(name) {
                        m.shader_extra_textures.insert(property.into(),png);
                        if property.starts_with("_overtex") {
                            if let Some(s)=self.texture_sampling_named(name) {
                                m.runtime_textures.insert(property.into(),[s.filter,s.wrap_u,s.wrap_v,i32::from(s.srgb),s.mip_count]);
                            }
                        }
                        if property=="_NormalMapDetail" {
                            if let Some(s)=self.texture_sampling_named(name) {
                                m.runtime_textures.insert("_OKSourceNormalMapDetail".into(),
                                    [s.filter,s.wrap_u,s.wrap_v,i32::from(s.srgb),s.mip_count]);
                            }
                        }
                        if matches!(property,"_AnotherRamp"|"_Texture2"|"_Texture3"|"_liquidmask") {
                            if let Some(sampling) = self.texture_sampling_named(name) {
                                m.runtime_textures.insert(property.into(), [
                                    sampling.filter, sampling.wrap_u, sampling.wrap_v,
                                    i32::from(sampling.srgb), sampling.mip_count,
                                ]);
                            }
                        }
                    }
                }
            }
            if let Some(g) = mat.slot("_HairGloss") {
                if let Some(png) = self.decode_tex(g) {
                    m.hair_gloss_png = Some(crate::orient_raw_mask(png));
                }
            }
            if let Some(name) = mat.slot("_AlphaMask") {
                m.alpha_mask_png = self.decode_tex(name).map(crate::orient_raw_mask);
            }
            if let Some(name) = mat.slot("_DetailMask") {
                m.detail_mask_png = self.decode_tex(name).map(crate::orient_raw_mask);
                if m.detail_mask_png.is_some() {
                    if let Some(sampling) = self.texture_sampling_named(name) {
                        m.runtime_textures.insert("_DetailMask".into(), [
                            sampling.filter, sampling.wrap_u, sampling.wrap_v,
                            i32::from(sampling.srgb), sampling.mip_count,
                        ]);
                    }
                }
            }
            if let Some(name) = mat.slot("_LineMask") {
                m.line_mask_png = self.decode_tex(name).map(crate::orient_raw_mask);
            }
            if let Some(name) = mat.slot("_NormalMap") {
                m.normal_map_png = self.decode_tex(name);
                if let Some(s)=self.texture_sampling_named(name) {
                    {
                        if let Some(texture)=self.texture_gpu_named(name) {
                            match crate::clothes_gpu::texture_readback(texture) {
                                Ok(png)=>m.normal_map_png=Some(png),
                                Err(e)=>eprintln!("ok-extract: normal source GPU readback fallback {name}: {e}"),
                            }
                        }
                    }
                    // Preserve the source view separately from the converted
                    // runtime RT; normal-named assets can still be sRGB.
                    m.runtime_textures.insert("_OKSourceNormalMap".into(),
                        [s.filter,s.wrap_u,s.wrap_v,i32::from(s.srgb),s.mip_count]);
                }
            }
            if let Some(name) = mat.slot("_NormalMask") {
                m.normal_mask_png = self.decode_tex(name).map(crate::orient_raw_mask);
            }
            let main = mat.slot("_MainTex").map(|s| s.to_string());
            let mask = mat.slot("_ColorMask").map(|s| s.to_string());
            let (main, mask) = (&main, &mask);
            // Preserve the historical behaviour: only override the heuristic
            // texture when the material actually authored MainTex/ColorMask.
            if main.is_none() && mask.is_none() {
                continue;
            }
            if let Some(name) = main {
                if let Some(png) = self.decode_tex(name) {
                    m.texture_name = name.clone();
                    // Material albedo is regular RGBA. Packed R-luma unpack
                    // is body-only and would destroy hair/clothes MainTex.
                    m.texture_png = Some(png);
                }
            } else {
                // Shader default `_MainTex` = "white". Never keep a heuristic
                // `_md` DetailMask (green) as albedo.
                m.texture_name = "white".into();
                m.texture_png = solid_png([1.0, 1.0, 1.0, 1.0], 8, 8);
            }
            if let Some(name) = mask {
                if let Some(png) = self.decode_tex(name) {
                    if let Some(sampling) = self.texture_sampling_named(name) {
                        m.runtime_textures.insert("_ColorMask".into(), [
                            sampling.filter, sampling.wrap_u, sampling.wrap_v,
                            i32::from(sampling.srgb), sampling.mip_count,
                        ]);
                    }
                    eprintln!(
                        "ok-extract: ColorMask {} <- {} for {}",
                        name,
                        png.len(),
                        m.name
                    );
                    if main.is_none() {
                        if let Ok(img) = crate::teximg::load_from_memory(&png) {
                            let rgba = img.to_rgba8();
                            let (w, h) = rgba.dimensions();
                            m.texture_png = solid_png([1.0, 1.0, 1.0, 1.0], w.max(1), h.max(1));
                        }
                    }
                    m.color_mask_png = Some(png);
                }
            }
        }
        self.cube_slot_atlases(picked);
    }

    fn decode_all_unity(
        &self,
    ) -> (
        Vec<ExtractedMesh>,
        Vec<(String, Vec<u8>)>,
        Option<crate::pose::Skeleton>,
    ) {
        let ver = version();
        let mut skel = None;
        let mut best = 0usize;
        for (_, fb) in &self.files {
            let Ok(serialized) = parse_shared_serialized(fb.clone()) else {
                continue;
            };
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = serialized.scene_object(&info) else {
                    continue;
                };
                let pid = object.path_id();
                if self.meshes.borrow().contains_key(&pid) {
                    continue;
                }
                let stream = mesh_stream_bytes(&object, &self.files);
                if let Some(mesh) = decoded_from_object(&object, &ver, stream.as_deref(), &serialized.unity_version) {
                    let prefer = mesh.name == "o_body_a" || mesh.name == "cf_O_face";
                    if prefer {
                        if let Some(s) =
                            crate::pose::skeleton_from_serialized(&serialized, mesh.path_id)
                        {
                            let score = s.bone_ids.len() + 10_000;
                            if score > best {
                                best = score;
                                skel = Some(s);
                            }
                        }
                    }
                    self.meshes.borrow_mut().insert(pid, mesh);
                }
            }
            for info in serialized.objects_of_type(28) {
                let Ok(object) = serialized.scene_object(info) else {
                    continue;
                };
                let n = object.name().unwrap_or_default();
                if n.is_empty() || self.textures.borrow().contains_key(&n.to_ascii_lowercase()) {
                    continue;
                }
                if let Some((nn, png)) = png_from_texture(&object, &self.files, None) {
                    self.remember_texture(nn.to_ascii_lowercase(), png);
                }
            }
        }
        let meshes: Vec<ExtractedMesh> = self.meshes.borrow().values().cloned().collect();
        let textures: Vec<(String, Vec<u8>)> = self
            .textures
            .borrow()
            .iter()
            .map(|(n, b)| (n.clone(), b.clone()))
            .collect();
        if skel.is_none() {
            skel = meshes.first().and_then(|m| self.skeleton(m.path_id));
        }
        (meshes, textures, skel)
    }
}

mod mesh_channels;
pub(crate) use mesh_channels::*;

mod clothes_states;
pub use clothes_states::*;

mod mesh_decode;
pub use mesh_decode::*;

mod textures;
pub use textures::*;

mod tints;
pub use tints::*;

mod prefabs;
pub use prefabs::*;

#[cfg(test)]
mod tests;

pub fn pick_maindata<'a>(meshes: &'a [ExtractedMesh], main_data: &str) -> Option<&'a ExtractedMesh> {
    if main_data.is_empty() || main_data.eq_ignore_ascii_case("p_dummy") {
        return None;
    }
    let want = main_data.trim_start_matches("p_");
    pick_named(meshes, main_data)
        .or_else(|| pick_named(meshes, want))
        .or_else(|| {
            meshes.iter().find(|m| {
                let n = m.name.to_ascii_lowercase();
                let w = want.to_ascii_lowercase();
                n.contains(&w) || w.contains(&n)
            })
        })
}

pub fn pick_named<'a>(meshes: &'a [ExtractedMesh], want: &str) -> Option<&'a ExtractedMesh> {
    let mut best: Option<&ExtractedMesh> = None;
    for m in meshes {
        // `obj` is intentionally omitted from prefab cache blobs.  Selecting
        // by its string length made every cached prefab look empty and sent
        // modded faces through the vanilla fallback on the second load.
        if m.name.eq_ignore_ascii_case(want)
            && best.map(|b| (m.indices.len(), m.positions.len()) > (b.indices.len(), b.positions.len())).unwrap_or(true)
        {
            best = Some(m);
        }
    }

    best
}

/// Bake a shader's uniform alpha factor into the exported albedo's alpha.
/// RGB stays unchanged; UE then applies the same per-pixel blend coverage.
pub fn scale_png_alpha(png: &[u8], factor: f32) -> Option<Vec<u8>> {
    // Studio items share textures between many renderers: each texture and factor is decoded and encoded once.
    let factor = factor.clamp(0.0, 1.0);
    crate::png_transform_cache::transform("scale_alpha_v1", png, &factor.to_bits().to_le_bytes(), || scale_png_alpha_uncached(png, factor))
}
fn scale_png_alpha_uncached(png: &[u8], factor: f32) -> Option<Vec<u8>> {
    let mut img = crate::teximg::load_from_memory(png).ok()?.into_rgba8();
    for p in img.pixels_mut() {
        p[3] = ((p[3] as f32) * factor).round() as u8;
    }
    crate::teximg::encode_rgba_owned(img)
}

/// Alpha-to-coverage under the game's 8x MSAA: a texel covers round(alpha x 8) of the pixel's 8 samples and blends
/// with its alpha on those, so the resolved pixel takes alpha x round(alpha x 8) / 8 (alpha 0.17 shows at 0.02, 0.4 at
/// 0.15, 1 stays 1). Baked into the texture's alpha, the plain alpha blend then draws the same.
pub fn alpha_to_coverage_png(png: &[u8]) -> Option<Vec<u8>> {
    crate::png_transform_cache::transform("alpha_to_coverage_8x_v1", png, &[], || {
        let mut img = crate::teximg::load_from_memory(png).ok()?.into_rgba8();
        for p in img.pixels_mut() { let a = p[3] as f32 / 255.0; p[3] = (a * (a * 8.0).round() / 8.0 * 255.0).round() as u8; }
        crate::teximg::encode_rgba_owned(img)
    })
}

#[cfg(test)]
mod alpha_to_mask_tests {
    #[test]
    fn alpha_keeps_its_share_of_the_eight_samples() {
        let mut img = image::RgbaImage::new(3, 1);
        for (x, a) in [(0, 44u8), (1, 128), (2, 200)] { img.put_pixel(x, 0, image::Rgba([10, 20, 30, a])); }
        let png = crate::teximg::encode_rgba_owned(img).unwrap();
        let cut = crate::teximg::load_from_memory(&super::alpha_to_coverage_png(&png).unwrap()).unwrap().into_rgba8();
        // 44 (0.17): 1 of 8 samples -> 6; 128 (0.5): 4 of 8 -> 64; 200 (0.78): 6 of 8 -> 150.
        assert_eq!([cut.get_pixel(0, 0)[3], cut.get_pixel(1, 0)[3], cut.get_pixel(2, 0)[3]], [6, 64, 150]);
        assert_eq!(cut.get_pixel(0, 0)[0], 10, "colour kept");
    }
}

fn store_tex_env_transform(mat: &mut MeshMaterial, key: &str, value: &unity_asset_core::UnityValue) {
    let Some(env) = value.as_object() else { return };
    let pair = |name: &str, fallback: f32| -> [f32; 2] {
        let p = env.get(name).and_then(|v| v.as_object());
        [
            p.and_then(|v| v.get("x")).and_then(|v| v.as_f64()).map(|v| v as f32).unwrap_or(fallback),
            p.and_then(|v| v.get("y")).and_then(|v| v.as_f64()).map(|v| v as f32).unwrap_or(fallback),
        ]
    };
    let scale = pair("m_Scale", 1.0);
    let offset = pair("m_Offset", 0.0);
    for (suffix, value) in [("x", scale[0]), ("y", scale[1]), ("z", offset[0]), ("w", offset[1])] {
        mat.floats.insert(format!("{key}_ST_{suffix}"), value);
    }
}

pub fn pick_largest(meshes: &[ExtractedMesh]) -> Option<&ExtractedMesh> {
    meshes.iter().max_by_key(|m| (m.indices.len(), m.positions.len()))
}

pub fn role_for_category(category_no: i32, property: &str) -> &'static str {
    match category_no {
        100 => "head",
        101 => "hair_b",
        102 => "hair_f",
        103 => "hair_s",
        104 => "hair_o",
        105 | 200 | 201 | 202 | 210 | 211 | 212 => "clothes_top",
        106 => "clothes_bot",
        107 => "clothes_bra",
        108 => "clothes_shorts",
        109 => "clothes_gloves",
        110 => "clothes_panst",
        111 => "clothes_socks",
        112 => "clothes_shoes",
        120..=130 => "accessory",
        _ => role_for("", property),
    }
}

pub fn is_mesh_category(category_no: i32) -> bool {
    matches!(category_no, 100..=112 | 120..=130 | 200..=212)
}

pub fn role_for(name: &str, property: &str) -> &'static str {
    let n = name.to_ascii_lowercase();
    let p = property.to_ascii_lowercase();
    if p.contains("head") || n.contains("face") || n.contains("head") {
        "head"
    } else if p.contains("hair") || n.contains("hair") {
        "hair"
    } else if p.contains("clothestop") || n.contains("top") {
        "clothes_top"
    } else if p.contains("clothesbot") || n.contains("bot") {
        "clothes_bot"
    } else if n.contains("body") {
        "body"
    } else {
        "part"
    }
}

#[cfg(test)]
#[test]
#[ignore="installed clothes shader ABI counterexamples"]
fn native_clothes_shader_abi_counterexamples() {
    use unity_asset_core::UnityValue as V;
    fn edit(node:&mut V,key:&str,old:&V,new:&V)->bool {
        match node {
            V::Object(fields)=>{
                if fields.get(key)==Some(old){fields.insert(key.into(),new.clone());return true;}
                fields.values_mut().any(|v|edit(v,key,old,new))
            },
            V::Array(values)=>values.iter_mut().any(|v|edit(v,key,old,new)),
            _=>false,
        }
    }
    let b=crate::gamefs::read(crate::test_support::game_path("/abdata/chara/mm_base.unity3d")).unwrap();
    let parsed=parse_bundle_ref(&b);
    parsed.with_serialized(|files| {
        for file in files {for info in file.objects_of_type(48) {
            let mut object=UnityObject::from_serialized_file(file,info).unwrap();
            if shader_runtime_name(&object)!="Koikano/create_topN"{continue;}
            assert!(clothes_create_shader_layout(&object).is_some());
            let original=object.class.properties().get("m_ParsedForm").unwrap().clone();
            for (key,old,new) in [
                ("m_Size",V::Integer(256),V::Integer(240)),
                ("m_Dim",V::Integer(4),V::Integer(3)),
                ("m_SamplerIndex",V::Integer(1),V::Integer(0)),
                ("m_GlobalKeywordIndices",V::Array(vec![]),V::Array(vec![V::Integer(1)])),
                ("m_Samplers",V::Array(vec![]),V::Array(vec![V::Integer(1)])),
            ] {
                let mut modified=original.clone();
                let mut subs=match modified {V::Object(ref mut fields)=>fields.get_mut("m_SubShaders").unwrap(),_=>panic!()};
                let sub=match &mut subs {V::Array(a)=>&mut a[0],_=>panic!()};
                let pass=match sub {V::Object(f)=>match f.get_mut("m_Passes").unwrap(){V::Array(a)=>&mut a[0],_=>panic!()},_=>panic!()};
                let fragment=match pass {V::Object(f)=>f.get_mut("progFragment").unwrap(),_=>panic!()};
                assert!(edit(fragment,key,&old,&new),"counterexample must edit {key}");
                object.class.set("m_ParsedForm".into(),modified);
                assert!(clothes_create_shader_layout(&object).is_none(),"must reject {key}");
            }
            return;
        }}panic!("native create shader absent");
    });
}

#[cfg(test)]
mod streamed_repair_survey {
    use super::*;
    /// Streamed textures the inline-array repair used to overwrite with a false header match, per bundle.
    #[test]
    #[ignore = "installed game: counts streamed textures the old repair corrupted"]
    fn streamed_textures_hit_by_repair() {
        let root = &crate::test_support::game_path("/abdata");
        let mut dirs = vec![root.to_path_buf()];
        let (mut total, mut bundles) = (0usize, 0usize);
        while let Some(d) = dirs.pop() {
            let Ok(rd) = std::fs::read_dir(&d) else { continue };
            for e in rd.flatten() {
                let p = e.path();
                if p.is_dir() { dirs.push(p); continue; }
                if !p.extension().is_some_and(|x| x == "unity3d") { continue; }
                let Ok(bytes) = std::fs::read(&p) else { continue };
                let Ok(bundle) = load_bundle_unbounded(bytes) else { continue };
                let mut hits = 0;
                for node in &bundle.files {
                    let Ok(data) = bundle.extract_file_data(node) else { continue };
                    let Ok(ser) = unity_asset_binary::asset::parse_serialized_file(data) else { continue };
                    for info in ser.objects_of_type(28) {
                        let Ok(o) = ser.scene_object(&info) else { continue };
                        let Ok(tex) = unity_asset_decode::texture::TextureProcessor::new(version()).convert_object(&o) else { continue };
                        if !tex.is_streamed() || tex.width < 4 || tex.height < 4 { continue; }
                        let min = ((tex.width as usize + 3) / 4) * ((tex.height as usize + 3) / 4) * 8;
                        if recover_inline_texture_data(o.raw_data(), min).is_some() { hits += 1; }
                    }
                }
                bundles += 1;
                if hits > 0 { total += hits; println!("{} {hits}", p.strip_prefix(root).unwrap().display()); }
            }
        }
        println!("streamed textures hit: {total} in {bundles} bundles");
    }
}

#[cfg(test)]
mod shader_dxbc_probe {
    /// Every DXBC program of the named shaders in a bundle (OK_SF_BUNDLE, default the live stage; OK_SF_SHADERS comma
    /// list) written to OK_SF_OUT as `<shader>_<n>_<ps|vs>.dxbc` (disassemble with `fxc /dumpbin`), and the shader's
    /// parsed form (properties, pass state) printed.
    #[test]
    #[ignore = "installed game: dump a map shader's DXBC programs"]
    fn dump_shader_dxbc() {
        use unity_asset_binary::object::UnityObject;
        let path = std::env::var("OK_SF_BUNDLE").unwrap_or_else(|_| crate::test_support::game_path("/abdata/studio/map/livestage_s.unity3d").to_string_lossy().into_owned());
        let wanted: Vec<String> = std::env::var("OK_SF_SHADERS").unwrap_or_else(|_| "Shader Forge/volumeLight,Shader Forge/electricLight".into()).split(',').map(str::to_owned).collect();
        let out = std::path::PathBuf::from(std::env::var("OK_SF_OUT").expect("OK_SF_OUT"));
        std::fs::create_dir_all(&out).unwrap();
        let parsed = super::parse_bundle_cached(std::fs::read(&path).unwrap());
        parsed.with_serialized(|files| for file in files { for info in file.objects_of_type(48) {
            let Ok(object) = file.scene_object(info) else { continue };
            let name = super::shader_runtime_name(&object);
            if std::env::var("OK_SF_LIST").is_ok() { println!("shader {name}"); }
            if !wanted.contains(&name) { continue; }
            let props = object.class.properties();
            let form = format!("{:?}", props.get("m_ParsedForm"));
            std::fs::write(out.join(format!("{}.form.txt", name.replace('/', "_"))), &form).unwrap();
            let Some(unity_asset_core::UnityValue::Bytes(comp)) = props.get("compressedBlob") else { continue };
            let blocks = super::decompress_shader_platforms(comp, &super::shader_nested_blob_lengths(props.get("offsets")), &super::shader_nested_blob_lengths(props.get("compressedLengths")), &super::shader_nested_blob_lengths(props.get("decompressedLengths")));
            let mut n = 0;
            for raw in blocks { for at in raw.windows(4).enumerate().filter_map(|(i, w)| (w == b"DXBC").then_some(i)) {
                let Some(size) = raw.get(at + 24..at + 28).map(|b| u32::from_le_bytes(b.try_into().unwrap()) as usize) else { continue };
                let Some(bytes) = raw.get(at..at + size) else { continue };
                let chunks = u32::from_le_bytes(bytes[28..32].try_into().unwrap()) as usize;
                let mut kind = "xx";
                for i in 0..chunks {
                    let offset = u32::from_le_bytes(bytes[32 + i * 4..36 + i * 4].try_into().unwrap()) as usize;
                    let tag = &bytes[offset..offset + 4];
                    if tag == b"SHDR" || tag == b"SHEX" { kind = match u32::from_le_bytes(bytes[offset + 8..offset + 12].try_into().unwrap()) >> 16 { 0 => "ps", 1 => "vs", _ => "xx" }; }
                }
                std::fs::write(out.join(format!("{}_{n}_{kind}.dxbc", name.replace('/', "_"))), bytes).unwrap();
                n += 1;
            } }
            println!("{name}: {n} programs, form {} bytes", form.len());
        } });
    }
}

#[cfg(test)]
mod prefab_renderer_visibility_tests {
    use super::*;
    #[test]
    fn active_curves_inherit_without_overriding_disabled_ancestors() {
        let h = crate::pose::PrefabHierarchy { nodes: [-1, 0, 1, 0].iter().map(|p| crate::pose::PrefabNode { parent: *p, ..Default::default() }).collect(), ..Default::default() };
        let active = [true, true, true, true];
        let mut overrides = HashMap::from([(1, false)]);
        assert_eq!(prefab_active_in_hierarchy(&h, &active, &overrides, 2), Some(false));
        assert_eq!(prefab_active_in_hierarchy(&h, &active, &overrides, 3), Some(true));
        overrides.insert(2, true);
        assert_eq!(prefab_active_in_hierarchy(&h, &active, &overrides, 2), Some(false));
        overrides.insert(1, true);
        assert_eq!(prefab_active_in_hierarchy(&h, &[true, false, true, true], &overrides, 2), Some(true));
        assert_eq!(prefab_active_in_hierarchy(&h, &[false, true, true, true], &overrides, 2), Some(false));
    }
}

/// Diagnostic (`ok-extract <game> --bundle-textures <bundle file>`): every Texture2D a bundle holds, with its pixel
/// area and whether it decodes, to tell a list entry's absent texture from one the decoder cannot read.
pub fn bundle_texture_report(bytes: Vec<u8>) -> Vec<(String, u32, bool)> {
    let parsed = parse_bundle_ref(&bytes);
    let dir = parsed.texture_directory();
    dir.catalog.iter().map(|(name, px)| (name.clone(), *px, parsed.texture_png_named(name).is_some())).collect()
}

#[cfg(test)]
mod cubemap_info_dump {
    #[test]
    fn color_space_double_reads_the_compiled_constant() {
        let mut linear = b"DXBCxxxx".to_vec();
        linear.extend_from_slice(&4.594_793_8_f32.to_le_bytes());
        assert_eq!(super::color_space_double(&linear), 4.594_793_8);
        // A gamma build adds the colour to itself: no 4.59 literal anywhere.
        assert_eq!(super::color_space_double(&[0u8; 64]), 2.0);
    }

    /// OK_CUBE_BUNDLE=<bundle> (a zipmod's inner bundle extracted to a file): each cubemap's format, encoding
    /// (m_LightmapFormat: Unity's texture usage, decides DecodeHDR) and colour space, and its alpha range (diagnostic).
    #[test]
    #[ignore = "diagnostic: bundle named by OK_CUBE_BUNDLE"]
    fn print_cubemaps() {
        let Ok(path) = std::env::var("OK_CUBE_BUNDLE") else { return };
        let bytes = std::fs::read(&path).unwrap();
        let parsed = super::parse_bundle_ref(&bytes);
        parsed.with_serialized(|files| {
            for file in files { for info in file.objects_of_type(89) {
                let Ok(o) = file.scene_object(info) else { continue };
                let Some(t) = super::read_texture_payload(&o, &parsed.files, None) else { println!("{:?}: no payload", o.name()); continue };
                let mut alpha = (255u8, 0u8);
                if let Some((_, _, png)) = super::cubemap_atlas(&t) { if let Ok(img) = image::load_from_memory(&png) { for p in img.to_rgba8().pixels() { alpha = (alpha.0.min(p[3]), alpha.1.max(p[3])); } } }
                println!("{:?} {}x{} {:?} lightmap_format {} color_space {} mips {} atlas alpha {:?}", o.name(), t.width, t.height, t.format, t.light_map_format, t.color_space, t.mip_count, alpha);
            } }
        });
    }
}

#[cfg(test)]
mod shader_pass_state_dump {
    /// OK_SHADER_BUNDLE=<bundle>: each shader's pass state as read for the renderer (diagnostic).
    #[test]
    #[ignore = "diagnostic: bundle named by OK_SHADER_BUNDLE"]
    fn print_shader_pass_state() {
        let Ok(path) = std::env::var("OK_SHADER_BUNDLE") else { return };
        let bytes = std::fs::read(&path).unwrap();
        let parsed = super::parse_bundle_ref(&bytes);
        parsed.with_serialized(|files| {
            for file in files {
                let (mut local, mut external) = (0, 0);
                for info in file.objects_of_type(21) {
                    let Ok(o) = file.scene_object(info) else { continue };
                    if o.class.properties().get("m_Shader").and_then(super::local_pptr_path_id).is_some() { local += 1 } else { external += 1 }
                }
                println!("materials with local shader {local}, external {external} (externals {:?})", file.externals.iter().map(|e| e.path.clone()).collect::<Vec<_>>());
            }
            for file in files { for info in file.objects_of_type(48) {
                let Ok(o) = file.scene_object(info) else { continue };
                println!("{} {:?}", super::shader_runtime_name(&o), super::shader_pass_state(&o));
                if std::env::var("OK_SHADER_RAW").is_ok() {
                    let sub = super::uv_arr(super::uv_obj(o.class.properties().get("m_ParsedForm").unwrap(), "m_SubShaders").unwrap()).first().cloned();
                    if let Some(sub) = sub { for p in super::uv_arr(super::uv_obj(&sub, "m_Passes").unwrap()) {
                        let s = super::uv_obj(p, "m_State");
                        if let Some(unity_asset_core::UnityValue::Object(m)) = s { println!("  keys {:?} cull {:?}", m.keys().collect::<Vec<_>>(), m.get("culling")); }
                        println!("  type={:?} name={:?} cull={:?} tags={:?}", super::uv_obj(p, "m_Type"),
                            s.and_then(|s| super::uv_obj(s, "m_Name")), s.and_then(|s| super::uv_obj(s, "m_Culling")),
                            s.and_then(|s| super::uv_obj(s, "m_Tags")));
                    }}
                }
            }}
        });
    }
}
