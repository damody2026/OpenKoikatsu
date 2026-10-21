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
                match UnityObject::from_serialized_file(ser,info) {
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
    /// `len` bytes streamed from `reader` (an archive entry that cannot be read in place) into a mapped temporary
    /// file that the system deletes once the map is gone: no private memory however large the entry. The reader
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
    /// `len` bytes written by `fill` into a mapped temporary file the system deletes once the map is gone.
    pub(crate) fn spill_with(len: u64, fill: impl FnOnce(&mut [u8]) -> Option<()>) -> Option<Self> {
        static SERIAL: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let dir = std::env::temp_dir().join("ok-extract-spill");
        std::fs::create_dir_all(&dir).ok()?;
        let path = dir.join(format!("{}-{}.tmp", std::process::id(), SERIAL.fetch_add(1, std::sync::atomic::Ordering::Relaxed)));
        let mut options = std::fs::OpenOptions::new();
        options.read(true).write(true).create_new(true);
        #[cfg(windows)]
        {
            use std::os::windows::fs::OpenOptionsExt;
            // FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE; shared for delete so the map can outlive the handle.
            options.attributes(0x100).custom_flags(0x0400_0000).share_mode(0x1 | 0x2 | 0x4);
        }
        let file = options.open(&path).ok()?;
        #[cfg(not(windows))]
        let _ = std::fs::remove_file(&path);
        let size = usize::try_from(len).ok()?;
        file.set_len(len).ok()?;
        let mut out = unsafe { memmap2::MmapMut::map_mut(&file) }.ok()?;
        fill(&mut out[..size])?;
        let map = out.make_read_only().ok()?;
        Some(Self { data: unity_asset_binary::shared_bytes::SharedBytes::Mmap(Arc::new(map)), start: 0, len: size }.registered())
    }
    /// A whole file: mapped when large, read otherwise.
    pub(crate) fn file(path: &Path) -> std::io::Result<Self> {
        let len = std::fs::metadata(path)?.len();
        if len >= MAP_AT_LEAST { if let Some(mapped) = Self::map(path, 0, len) { return Ok(mapped); } }
        std::fs::read(path).map(Self::from)
    }
    fn range(&self) -> std::ops::Range<usize> { self.start..self.start + self.len }
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
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue; };
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
            if let Ok(obj) = UnityObject::from_serialized_file(ser, info) {
                go_name.insert(info.path_id, obj.name().unwrap_or_default());
            }
        }
    }
    let mut names = HashMap::new();
    for info in &ser.objects {
        if info.type_id == 4 {
            if let Ok(obj) = UnityObject::from_serialized_file(ser, info) {
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
        if let Ok(obj) = UnityObject::from_serialized_file(ser, &info) {
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
        let Ok(obj) = UnityObject::from_serialized_file(ser, &info) else {
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
            let Ok(frame)=UnityObject::from_serialized_file(ser,&other) else {continue;};
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
    /// `ChaCustomHairComponent.rendAccessory` — `AccessoryColor` on all three channels.
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
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
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
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
                if let Some(n) = obj.name() {
                    go_names.insert(info.path_id, n);
                }
            }
        }
        for info in ser.objects_of_type(4) {
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
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
            let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) else {
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
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
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
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
                if let Some(n) = obj.name() {
                    go_names.insert(info.path_id, n);
                }
            }
        }
        for info in ser.objects_of_type(4) {
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
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
            let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) else {
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
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
                if let Some(name) = obj.name() {
                    go_names.insert(info.path_id, name.to_ascii_lowercase());
                }
            }
        }
        for info in ser.objects_of_type(4) {
            if let Ok(obj) = UnityObject::from_serialized_file(&ser, &info) {
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
}

/// A prefab's render-to-texture cameras, and the renderers whose materials sample their RenderTextures:
/// (renderer node, material name, texture property, camera index).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabRenderTextures { pub cameras: Vec<PrefabRenderCamera>, pub users: Vec<(i32, String, String, i32)> }

/// One time of day of a Studio map's SunLightInfo (on the map root): the GameObjects shown then (`visibleList`, by
/// `prefab_hierarchy` node; every list is hidden first, then the current one shown) and the sun (`_targetLight`'s Euler
/// angle in degrees, colour, intensity).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabSunInfo { pub kind: i32, pub visible: Vec<i32>, pub angle: [f32; 3], pub color: [f32; 4], pub intensity: f32 }

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

#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct ExtractedMesh {
    /// Settings of textures created by runtime blits, keyed by shader property.
    /// [filter, wrap_u, wrap_v, srgb, mip_policy]; policy 1 averages encoded
    /// bytes like the captured Unity RenderTexture chain. Authored Texture2D
    /// mips must not be regenerated merely because a PNG exists.
    #[serde(default)]
    pub runtime_textures: std::collections::BTreeMap<String, [i32; 5]>,
    /// Additional authored material textures. Raw Unity row order until push_meshes.
    #[serde(default)]
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
    #[serde(default)]
    pub texture_png: Option<Vec<u8>>,
    pub texture_name: String,
    /// OK base library: texture fields stored as (field, texture, transform)
    /// references to the source's OK textures; empty outside a stored model.
    /// Serialized only inside OK base library records (`with_texture_plans`):
    /// sessions, okPv packs and caches keep the original mesh layout.
    #[serde(default, skip_serializing_if = "texture_plan_hidden", deserialize_with = "texture_plan_compat")]
    pub texture_plan: Vec<(String, String, u8)>,
    /// Hair shader `_ColorMask` (RGB ??_Color / _Color2 / _Color3).
    pub color_mask_png: Option<Vec<u8>>,
    /// Material `_AlphaMask` retained when no outer garment replaces it.
    pub alpha_mask_png: Option<Vec<u8>>,
    /// Shader `_DetailMask`; retained separately from baked albedo.
    pub detail_mask_png: Option<Vec<u8>>,
    /// Shader `_LineMask`; its channels gate the clothing ramp and painted seams.
    pub line_mask_png: Option<Vec<u8>>,
    /// Shader `_NormalMap`; retained for the fragment normal path.
    pub normal_map_png: Option<Vec<u8>>,
    /// `main_skin` `_NormalMask`; G bends face lighting toward the view.
    pub normal_mask_png: Option<Vec<u8>>,
    /// Whether Unity supplied the tangent basis required by a tangent-space
    /// normal map. `None` is reserved for meshes built outside the Unity decoder.
    #[serde(default)]
    pub source_tangents: Option<bool>,
    /// Authored Unity tangent.xyz and handedness.w in mesh rest space.
    #[serde(default)]
    pub tangents: Vec<[f32; 4]>,
    /// Hair shader `_HairGloss` (matcap sheen).
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
            let Ok(shader) = UnityObject::from_serialized_file(&serialized, info) else { continue; };
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

impl Default for TextureSampling {
    fn default()->Self {Self{filter:1,wrap_u:0,wrap_v:0,srgb:true,mip_count:1}}
}

/// OK base library surface: every Unity query of a bundle is recorded or
/// replayed here (see oklib.rs). Bodies live in the `*_unity` methods.
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
                return f();
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
                        let obj=UnityObject::from_serialized_file(ser,info).ok()?;
                        let props=obj.class.properties();
                        let go=props.get("m_GameObject").and_then(uv_pptr)?;
                        let node=*go_node.get(&go)?;
                        let layer=ser.objects_of_type(1).into_iter().find(|i|i.path_id==go)
                            .and_then(|i|UnityObject::from_serialized_file(ser,i).ok())
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
    pub fn prefab_render_textures(&self,prefab:&str)->PrefabRenderTextures {self.memo_prefab("prefab_render_textures",prefab,prefab,||self.prefab_render_textures_unity(prefab))}
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
                let cube=UnityObject::from_serialized_file(target,info).map_err(|e|e.to_string())?;
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
                    let object=UnityObject::from_serialized_file(ser,info).map_err(|e|e.to_string())?;let p=object.class.properties();
                    if let Some(cube)=read(from,p.get("m_GeneratedSkyboxReflection"),p.get("m_ReflectionIntensity").map(uv_f32).unwrap_or(1.))?{out.push((from,0,cube));}
                }
                for info in ser.objects_of_type(215){
                    let object=UnityObject::from_serialized_file(ser,info).map_err(|e|e.to_string())?;let p=object.class.properties();
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
                        Some((file, UnityObject::from_serialized_file(ser, info).ok()?))
                    };
                    let mut active = vec![true; h.nodes.len()];
                    for info in ser.objects_of_type(1) {
                        if let Some(&node) = go_node.get(&info.path_id) {
                            if let Ok(o) = UnityObject::from_serialized_file(ser, info) {
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
                        let Ok(a) = UnityObject::from_serialized_file(ser, info) else { continue; };
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
                let rgba=crate::teximg::load_from_memory(&png).ok()?.to_rgba8();
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
            .or_else(||self.dependency_texture_bundle(name)?.texture_sampling_named(name))
    }
    pub fn skeleton(&self,path_id:i64)->Option<crate::pose::Skeleton> {self.memo("skeleton",&path_id,||self.skeleton_unity(path_id))}
    pub fn skeleton_full_named(&self,probe:&str)->Option<crate::pose::Skeleton> {self.memo("skeleton_full_named",probe,||self.skeleton_full_named_unity(probe))}
    pub fn skeleton_for_prefab_name(&self,prefab_name:&str)->Option<crate::pose::Skeleton> {let _spot=crate::profile::spot("skeleton_for_prefab_name");self.memo_prefab("skeleton_for_prefab_name",prefab_name,prefab_name,||self.skeleton_for_prefab_name_unity(prefab_name))}
    pub fn textures_keep_base(&self)->Vec<(String,Vec<u8>)> {let _spot=crate::profile::spot("textures_keep_base");
        // A library model's answer cannot change while the library replays: restoring its packed textures from the
        // store again for every card (the base head, ~0.35 s) is kept once per process.
        type Memo=std::sync::Mutex<std::collections::HashMap<crate::oklib::Id,std::sync::Arc<Vec<(String,Vec<u8>)>>>>;
        static MEMO:std::sync::OnceLock<Memo>=std::sync::OnceLock::new();
        let replay=self.lib.filter(|_|crate::oklib::active().is_some_and(|s|s.mode==crate::oklib::Mode::Replay));
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
        if m.len() >= 24 {
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

fn parse_bundle_uncached(bytes: SourceBytes) -> ParsedBundle {let _spot=crate::profile::spot("parse_bundle_uncached");
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
                UnityObject::from_serialized_file(file, info).ok()
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
                    UnityObject::from_serialized_file(ser,info).ok().map(|obj|
                        (info.path_id,obj.name().unwrap_or_default()))).collect();
                for info in ser.objects_of_type(114) {
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                    if let Ok(obj)=UnityObject::from_serialized_file(file,info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}
                }
                for info in file.objects_of_type(4) {
                    if let Ok(obj)=UnityObject::from_serialized_file(file,info) {
                        let p=obj.class.properties();
                        if let Some(go)=p.get("m_GameObject").and_then(references::PPtr::local) {
                            nodes.insert(info.path_id,(go,p.get("m_Father").and_then(references::PPtr::local).unwrap_or(0)));by_go.insert(go,info.path_id);
                        }
                    }
                }
                let objects:Vec<_>=file.objects_of_type(114).into_iter().filter_map(|info|UnityObject::from_serialized_file(file,&info).ok()).collect();
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
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
                    let Some(unity_asset_core::UnityValue::Array(list))=obj.class.properties().get("lightmapParameters").cloned() else {continue;};
                    for e in &list {
                        let (Some(r),Some(i),Some(st))=(uv_obj(e,"renderer").and_then(uv_pptr),uv_obj(e,"lightmapIndex").map(uv_i32),uv_obj(e,"scaleOffset").map(vec4)) else {continue;};
                        restored.insert(r,(i,st));
                    }
                }
                let mut renderers=Vec::new();
                for info in ser.objects_of_type(23) {
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
                    if slot == "m_DirLightmap" && obj.class.properties().get("m_LightmapsMode").map(uv_i32) != Some(1) { continue; }
                    let Some(unity_asset_core::UnityValue::Array(maps))=obj.class.properties().get("m_Lightmaps").cloned() else {continue;};
                    for m in &maps {
                        let id=uv_obj(m,slot).and_then(uv_pptr).unwrap_or(0);
                        let name=sers.iter().find_map(|s| s.objects_of_type(28).into_iter().find(|i|i.path_id==id)
                            .and_then(|i|UnityObject::from_serialized_file(s,i).ok()).and_then(|o|o.name())).unwrap_or_default();
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
                    let object=sers.iter().find_map(|s|s.objects_of_type(89).into_iter().find(|i|i.path_id==id).and_then(|i|UnityObject::from_serialized_file(s,i).ok()))?;
                    cubemap_atlas(&read_texture_payload(&object,&self.files,None)?)
                };
                let vec3=|v:Option<&unity_asset_core::UnityValue>,d:f32|v.map(|v|["x","y","z"].map(|k|uv_obj(v,k).map(uv_f32).unwrap_or(d))).unwrap_or([d;3]);
                let mut probes=Vec::new();
                for info in ser.objects_of_type(215) {
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
                    let p=obj.class.properties();
                    let Some(node)=p.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_node.get(&go)).copied() else {continue;};
                    if !p.get("m_Enabled").map(uv_bool).unwrap_or(true) {continue;}
                    let baked=p.get("m_CustomBakedTexture").filter(|v|uv_pptr(v).is_some_and(|id|id!=0)).or_else(||p.get("m_BakedTexture"));
                    let Some((size,mips,atlas))=baked.and_then(cubemap) else {continue;};
                    probes.push(PrefabReflectionProbe{node,box_size:vec3(p.get("m_BoxSize"),10.),box_offset:vec3(p.get("m_BoxOffset"),0.),
                        importance:p.get("m_Importance").map(uv_i32).unwrap_or(1),intensity:p.get("m_IntensityMultiplier").map(uv_f32).unwrap_or(1.),size,mips,atlas});
                }
                for info in ser.objects_of_type(104) {
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                let find=|class:i32,id:i64| sers.iter().find_map(|s|s.objects_of_type(class).into_iter().find(|i|i.path_id==id).and_then(|i|UnityObject::from_serialized_file(s,i).ok()));
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
                    let Ok(a)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
                    let p=obj.class.properties();
                    let (Some(unity_asset_core::UnityValue::Array(list)),Some(target))=(p.get("_infos"),p.get("_targetLight")) else {continue;};
                    if !p.get("m_GameObject").and_then(uv_pptr).is_some_and(|go|go_node.contains_key(&go)) {continue;}
                    let mut sun=PrefabSunLight{light_node:-1,light_type:1,culling_mask:u32::MAX,lightmapping:4,infos:Vec::new()};
                    let light_id=uv_pptr(target).unwrap_or(0);
                    if let Some(l)=ser.objects_of_type(108).into_iter().find(|i|i.path_id==light_id).and_then(|i|UnityObject::from_serialized_file(ser,i).ok()) {
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
                let object=|v:&unity_asset_core::UnityValue,class:i32,from:usize|->Option<(UnityObject,usize)> {
                    let id=uv_pptr(v).filter(|id|*id!=0)?;
                    let find=|s:&SerializedFile| s.objects_of_type(class).into_iter().find(|i|i.path_id==id).and_then(|i|UnityObject::from_serialized_file(s,i).ok());
                    if uv_obj(v,"m_FileID").map(uv_i32).unwrap_or(0)==0 {find(sers.get(from)?).map(|o|(o,from))}
                    else {sers.iter().enumerate().filter(|(i,_)|*i!=from).find_map(|(i,s)|find(s).map(|o|(o,i)))}
                };
                let render_texture=|v:&unity_asset_core::UnityValue,from:usize|->Option<(String,i32,i32)> {
                    let (o,_)=object(v,84,from)?;
                    let p=o.class.properties();
                    Some((o.name().unwrap_or_default(),p.get("m_Width").map(uv_i32).unwrap_or(0),p.get("m_Height").map(uv_i32).unwrap_or(0)))
                };
                let mut cameras=Vec::new();
                for info in ser.objects_of_type(20) {
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
                    let p=obj.class.properties();
                    let go=p.get("m_GameObject").and_then(uv_pptr).unwrap_or(0);
                    let Some(node)=go_node.get(&go).copied() else {continue;};
                    let Some((target,width,height))=p.get("m_TargetTexture").and_then(|t|render_texture(t,gi)) else {continue;};
                    let f=|k:&str,d:f32|p.get(k).map(uv_f32).unwrap_or(d);
                    let active=ser.objects_of_type(1).into_iter().find(|i|i.path_id==go).and_then(|i|UnityObject::from_serialized_file(ser,i).ok())
                        .and_then(|o|o.class.properties().get("m_IsActive").map(uv_bool)).unwrap_or(true);
                    let background=p.get("m_BackGroundColor").map(|c|["r","g","b","a"].map(|k|uv_obj(c,k).map(uv_f32).unwrap_or(0.))).unwrap_or([0.,0.,0.,1.]);
                    let culling_mask=p.get("m_CullingMask").and_then(|m|uv_obj(m,"m_Bits")).and_then(|v|v.as_i64()).map(|v|v as u32).unwrap_or(u32::MAX);
                    cameras.push(PrefabRenderCamera{node,enabled:p.get("m_Enabled").map(uv_bool).unwrap_or(true),active,
                        fov:f("field of view",60.),near:f("near clip plane",0.3),far:f("far clip plane",1000.),
                        orthographic:p.get("orthographic").map(uv_bool).unwrap_or(false),ortho_size:f("orthographic size",5.),
                        culling_mask,clear_flags:p.get("m_ClearFlags").map(uv_i32).unwrap_or(1),background,target,width,height});
                }
                if cameras.is_empty() {return Default::default();}
                let mut users=Vec::new();
                for class in [23,137] { for info in ser.objects_of_type(class) {
                    let Ok(obj)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                            let Some((target,_,_))=render_texture(tex,file) else {continue;};
                            if let Some(c)=cameras.iter().position(|c:&PrefabRenderCamera|c.target==target) {
                                let user=(node,name.clone(),key.as_str().unwrap_or("").to_owned(),c as i32);
                                if !users.contains(&user) {users.push(user);}
                            }
                        }
                    }
                }}
                PrefabRenderTextures{cameras,users}
            });
        }
        Default::default()
    }
    fn prefab_has_component_field_unity(&self, prefab:&str, field:&str)->bool {
        self.with_serialized(|sers| sers.iter().any(|ser| {
            let mut gos=HashMap::new();let mut nodes=HashMap::new();let mut by_go=HashMap::new();
            for info in ser.objects_of_type(1) {
                if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}
            }
            for info in ser.objects_of_type(4) {
                if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {
                    let props=obj.class.properties();
                    if let Some(go)=props.get("m_GameObject").and_then(uv_pptr) {
                        nodes.insert(info.path_id,(go,props.get("m_Father").and_then(uv_pptr).unwrap_or(0)));
                        by_go.insert(go,info.path_id);
                    }
                }
            }
            ser.objects_of_type(114).into_iter().any(|info| {
                let Ok(obj)=UnityObject::from_serialized_file(ser,info) else{return false;};
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
                for info in ser.objects_of_type(1) {if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}}
                for info in ser.objects_of_type(4) {if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {
                    let p=obj.class.properties();if let Some(go)=p.get("m_GameObject").and_then(uv_pptr) {
                        nodes.insert(info.path_id,(go,p.get("m_Father").and_then(uv_pptr).unwrap_or(0)));by_go.insert(go,info.path_id);
                    }
                }}
                for info in ser.objects_of_type(114) {if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {scripts.insert(info.path_id,obj.class.properties().clone());}}
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
                for info in ser.objects_of_type(1) {if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {gos.insert(info.path_id,obj.name().unwrap_or_default());}}
                for info in ser.objects_of_type(4) {if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {
                    let p=obj.class.properties();if let Some(go)=p.get("m_GameObject").and_then(uv_pptr) {
                        nodes.insert(info.path_id,(go,p.get("m_Father").and_then(uv_pptr).unwrap_or(0)));by_go.insert(go,info.path_id);
                    }
                }}
                for info in ser.objects_of_type(114) {if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {scripts.insert(info.path_id,obj.class.properties().clone());}}
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
                if let Ok(obj)=UnityObject::from_serialized_file(ser,info) {
                    if let Some(go)=obj.class.properties().get("m_GameObject").and_then(uv_pptr) {
                        go_renderers.insert(go,info.path_id);
                    }
                }
            }
            for info in ser.objects_of_type(114) {
                let Ok(obj)=UnityObject::from_serialized_file(ser,info) else{continue;};
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
                    if let Ok(obj) = UnityObject::from_serialized_file(ser, go) {
                        go_names.insert(go.path_id, obj.name().unwrap_or_default());
                    }
                }
                let mut target_mesh = HashMap::new();
                for renderer in ser.objects_of_type(137) {
                    let Some(mesh_name) = renderer_mesh.get(&renderer.path_id) else { continue; };
                    let Ok(obj) = UnityObject::from_serialized_file(ser, renderer) else { continue; };
                    if let Some(go) = obj.class.properties().get("m_GameObject").and_then(uv_pptr) {
                        target_mesh.insert(go, mesh_name.clone());
                    }
                }
                for info in ser.objects_of_type(114) {
                    let Ok(obj) = UnityObject::from_serialized_file(ser, info) else { continue; };
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
                    UnityObject::from_serialized_file(ser, info).ok().map(|o| (info.path_id, o.name().unwrap_or_default()))).collect();
                for info in ser.objects_of_type(137).into_iter().chain(ser.objects_of_type(23)) {
                    let Ok(renderer)=UnityObject::from_serialized_file(ser,info) else {continue;};
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
                    UnityObject::from_serialized_file(file, info).ok().map(|o| (info.path_id, o.name().unwrap_or_default()))).collect();
                for info in file.objects_of_type(114) {
                    if let Ok(o) = UnityObject::from_serialized_file(file, info) {
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
                    if let Ok(o) = UnityObject::from_serialized_file(ser, info) {
                        textures.insert(o.path_id(), o.name().unwrap_or_default());
                    }
                }
            }
            for ser in sers {
                // m_FileID=0 points into this serialized file. PathIDs from
                // another file in the bundle must never answer HasProperty.
                let mut shaders = HashMap::<i64, String>::new();
                let mut shader_rim = HashMap::<i64, bool>::new();
                for info in ser.objects_of_type(48) {
                    if let Ok(o) = UnityObject::from_serialized_file(ser, info) {
                        let name = shader_runtime_name(&o);
                        if !name.is_empty() { shaders.insert(o.path_id(), name); }
                        if let Some(has_rim) = shader_declares_property(&o, "_rimV") {
                            shader_rim.insert(o.path_id(), has_rim);
                        }
                    }
                }
                for info in ser.objects_of_type(21) {
                    let Ok(o) = UnityObject::from_serialized_file(ser, info) else { continue; };
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
                let Ok(object)=UnityObject::from_serialized_file(file,info) else {continue;};
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
                let Ok(object)=UnityObject::from_serialized_file(file,info) else {continue;};
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
        if self.files.is_empty() && crate::oklib::replaying() {
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
                for info in file.objects_of_type(21) { if let Ok(o) = UnityObject::from_serialized_file(file, info) { materials.push(o.name().unwrap_or_default()); } }
                for info in file.objects_of_type(48) { if let Ok(o) = UnityObject::from_serialized_file(file, info) { shaders.push(shader_runtime_name(&o)); } }
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
    /// `n_top_a`/`n_top_b`(?�脫) `n_bot_a`/`n_bot_b`/`n_bot_c`(?�脫) `o_inner_a/b`
    /// `o_bra_a/b` `n_panst_*` `n_socks_*` ??node per state; the character shows
    /// only one (`ChaControl.cs:1317-1495`, default state 0 = 穿好 = the `_a`
    /// node). We loaded the whole prefab, so both states rendered at once.
    fn clothes_state_filter(&self, meshes: &mut Vec<ExtractedMesh>, chains: &[(i64, Vec<String>)], clothes_category: Option<i32>) {
        let states = clothes_states();
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
                    let Ok(obj) = UnityObject::from_serialized_file(ser, info) else {
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
                    let Ok(object) = UnityObject::from_serialized_file(ser, &info) else {
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
        let info = file.0.objects_of_type(28).into_iter().find(|i| i.path_id == path_id)?;
        UnityObject::from_serialized_file(&file.0, info).ok()?.name()
    }

    /// A texture of a dependency bundle's material, by name, as PNG (cached under the external path).
    fn external_texture_png(&self, external_path: &str, name: &str) -> Option<Vec<u8>> {
        let key = format!("{}|{}", external_path.to_ascii_lowercase(), name.to_ascii_lowercase());
        if let Some(png) = self.external_textures.borrow().get(&key) { return png.clone(); }
        let png = self.external_file(external_path).and_then(|file| {
            let (ser, streams) = (&file.0, &file.1);
            ser.objects_of_type(28).into_iter().find_map(|info| {
                let object = UnityObject::from_serialized_file(ser, info).ok()?;
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
            let Some(file) = self.external_file(path) else { continue; };
            let (ser, streams) = (&file.0, &file.1);
            for info in ser.objects_of_type(MESH_CLASS_ID) {
                if !ids.contains(&info.path_id) || self.meshes.borrow().contains_key(&info.path_id) { continue; }
                let Ok(object) = UnityObject::from_serialized_file(ser, &info) else { continue; };
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
                    let Ok(object) = UnityObject::from_serialized_file(ser, &info) else {
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
            // OK texture: the texels are the model data, the PNG is derived.
            let payload=self.memo_texture("texture_payload",name,||self.texture_payload_unity(name));
            let Some((n,png))=payload.and_then(|p|png_from_payload(p.0,None)) else { return self.dependency_texture_png(name); };
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
            for file in files {
                let roots=prefab_asset_roots(file);
                let mut objects=std::collections::BTreeSet::new();
                let mut go_names=HashMap::new();
                for info in file.objects_of_type(1) {
                    if let Ok(obj)=UnityObject::from_serialized_file(file,info) {
                        let name=obj.name().unwrap_or_default();
                        objects.insert(name.to_ascii_lowercase());go_names.insert(info.path_id,name);
                    }
                }
                for info in file.objects_of_type(4) {
                    let Ok(obj)=UnityObject::from_serialized_file(file,info) else {continue};
                    let p=obj.class.properties();
                    if p.get("m_Father").and_then(uv_pptr).unwrap_or(0)==0 {
                        if let Some(name)=p.get("m_GameObject").and_then(uv_pptr).and_then(|go|go_names.get(&go)) {dir.top.push(name.clone());}
                    }
                }
                for info in file.objects_of_type(MESH_CLASS_ID) {
                    if let Ok(obj)=UnityObject::from_serialized_file(file,&info) {dir.meshes.insert(obj.name().unwrap_or_default().to_ascii_lowercase());}
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
                    let Ok(object) = UnityObject::from_serialized_file(ser, info) else {
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
                    let Ok(object) = UnityObject::from_serialized_file(ser, info) else {
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
                    let Ok(object) = UnityObject::from_serialized_file(ser, info) else {
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
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else {
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
                let Ok(object) = UnityObject::from_serialized_file(&serialized, info) else {
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

fn format_size(fmt: u8) -> usize {
    // Unity 2019+ VertexFormat: 0 Float32, 1 Float16, 2 UNorm8, 8 UInt16, 9 SInt16, 10 UInt32, 11 SInt32.
    match fmt {
        0 | 10 | 11 | 12 => 4,
        1 | 4 | 5 | 8 | 9 => 2,
        _ => 1,
    }
}

fn read_f32(data: &[u8], off: usize) -> f32 {
    let Ok(b) = data.get(off..off + 4).unwrap_or(&[]).try_into() else {
        return 0.0;
    };
    f32::from_le_bytes(b)
}

/// Unity VertexFormat scalar conversion, shared by geometry and texture coords.
/// Channel stride/offset is in bytes; Float16 must advance by two, not four.
fn read_vertex_component(data: &[u8], off: usize, format: u8) -> f32 {
    let u16_value=||data.get(off..off+2).and_then(|s|s.try_into().ok()).map(u16::from_le_bytes).unwrap_or(0);
    match format {
        0 => read_f32(data,off),
        1 => f16_to_f32(u16_value()),
        2 => data.get(off).copied().unwrap_or(0) as f32 / 255.,
        3 => ((data.get(off).copied().unwrap_or(0) as i8) as f32 / 127.).max(-1.),
        4 => u16_value() as f32 / 65535.,
        5 => (u16_value() as i16 as f32 / 32767.).max(-1.),
        6 => data.get(off).copied().unwrap_or(0) as f32,
        7 => data.get(off).copied().unwrap_or(0) as i8 as f32,
        8 => u16_value() as f32,
        9 => u16_value() as i16 as f32,
        10 => read_u32_le(data,off) as f32,
        11 => read_u32_le(data,off) as i32 as f32,
        _ => 0.,
    }
}

/// Read a blend weight from a `VertexAttributeFormat`/dimension-packed channel.
fn read_attr_weight(data: &[u8], off: usize, fmt: u8) -> f32 {
    match fmt {
        0 => read_f32(data, off),
        1 => f16_to_f32(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).map(u16::from_le_bytes).unwrap_or(0)),
        2 => data.get(off).copied().unwrap_or(0) as f32 / 255.0,
        6 => data.get(off).copied().unwrap_or(0) as f32,
        8 => u16::from_le_bytes(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).unwrap_or([0, 0])) as f32,
        10 => read_u32_le(data, off) as f32,
        _ => 0.0,
    }
}

/// Read a bone index from a packed channel (clamped to u16; indices are small).
fn read_attr_bone(data: &[u8], off: usize, fmt: u8) -> u16 {
    let v = match fmt {
        0 => read_f32(data, off) as u32,
        1 => f16_to_f32(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).map(u16::from_le_bytes).unwrap_or(0)) as u32,
        2 | 3 | 6 | 7 => data.get(off).copied().unwrap_or(0) as u32,
        4 | 5 | 8 | 9 => u16::from_le_bytes(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).unwrap_or([0, 0])) as u32,
        10 | 11 => read_u32_le(data, off),
        _ => 0,
    };
    v.min(u16::MAX as u32) as u16
}

fn f16_to_f32(bits: u16) -> f32 {
    let sign = (bits >> 15) as u32;
    let exp = ((bits >> 10) & 0x1F) as u32;
    let mant = (bits & 0x3FF) as u32;
    let f = if exp == 0 {
        (mant as f32) * 2f32.powi(-24)
    } else if exp == 31 {
        if mant == 0 { f32::INFINITY } else { f32::NAN }
    } else {
        (1.0 + mant as f32 / 1024.0) * 2f32.powi(exp as i32 - 15)
    };
    if sign == 1 { -f } else { f }
}

fn decode_positions(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 3]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() || chs.is_empty() || chs[0].dimension < 3 {
        return Vec::new();
    }
    let (strides, stream_off) = channel_layout(mesh);
    let c0 = &chs[0];
    let stride = strides[c0.stream as usize];
    let base = stream_off[c0.stream as usize];
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let o = base + i * stride + c0.offset as usize;
        out.push(std::array::from_fn(|axis|read_vertex_component(data,o+axis*format_size(c0.format),c0.format)));
    }
    out
}

/// Read the Unity normal channel (VertexAttribute index 1). Unity stores the
/// mesh's authored normals here; Illusion faces have interior cavity triangles
/// so geometric normals cannot reproduce them.
fn decode_normals(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 3]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() || chs.len() < 2 {
        return Vec::new();
    }
    let c = &chs[1];
    if c.dimension < 3 {
        return Vec::new();
    }
    let (strides, stream_off) = channel_layout(mesh);
    let s = c.stream as usize;
    if s >= strides.len() {
        return Vec::new();
    }
    let stride = strides[s];
    let base = stream_off[s];
    if stride == 0 || base + n * stride > data.len() {
        return Vec::new();
    }
    let off = c.offset as usize;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let o = base + i * stride + off;
        let mut v: [f32;3] = std::array::from_fn(|axis|read_vertex_component(data,o+axis*format_size(c.format),c.format));
        let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
        v = if l > 1e-8 {
            [v[0] / l, v[1] / l, v[2] / l]
        } else {
            [0.0, 0.0, 1.0]
        };
        out.push(v);
    }
    out
}

/// Unity's newer channel layout puts Tangent at 2; the older layout used by
/// KKS SAC bodies puts Color32 at 2 and Tangent at 7. Accept only a four-float
/// tangent channel here, never the four-byte color channel.
fn decode_tangents(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 4]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let Some(c) = [2, 7].iter().filter_map(|&i| mesh.vertex_data.channels.get(i))
        .find(|c| c.dimension == 4 && matches!(c.format, 0 | 1)) else { return Vec::new() };
    if n == 0 { return Vec::new() }
    let data = &mesh.vertex_data.data_size;
    let (strides, offsets) = channel_layout(mesh);
    let stream = c.stream as usize;
    if stream >= strides.len() || strides[stream] == 0 { return Vec::new() }
    let stride = strides[stream];
    let base = offsets[stream] + c.offset as usize;
    let component_size = format_size(c.format);
    if base + (n - 1) * stride + 4 * component_size > data.len() { return Vec::new() }
    let read = |off: usize| -> f32 { if c.format == 0 { read_f32(data, off) }
        else { f16_to_f32(u16::from_le_bytes(data[off..off + 2].try_into().unwrap())) } };
    let mut result = Vec::with_capacity(n);
    for i in 0..n {
        let off = base + i * stride;
        let t = std::array::from_fn(|axis| read(off + axis * component_size));
        if !t.iter().all(|v| v.is_finite()) ||
            !(0.25..=1.75).contains(&(t[0] * t[0] + t[1] * t[1] + t[2] * t[2])) ||
            t[3].abs() < 0.5 { return Vec::new() }
        result.push(t);
    }

    result
}

thread_local! {
    static ACTIVE_CLOTHES_STATES: std::cell::Cell<Option<[u8; 9]>> = const { std::cell::Cell::new(None) };
    static REQUESTED_CLOTHES_STATES: std::cell::Cell<Option<[u8; 9]>> = const { std::cell::Cell::new(None) };
}

pub struct RequestedClothesScope(Option<[u8;9]>);
impl Drop for RequestedClothesScope {
    fn drop(&mut self){REQUESTED_CLOTHES_STATES.with(|cell|cell.set(self.0));}
}
pub fn request_clothes_states(states:[u8;9])->RequestedClothesScope {
    RequestedClothesScope(REQUESTED_CLOTHES_STATES.with(|cell|cell.replace(Some(states))))
}
pub fn explicit_requested_clothes_states()->Option<[u8;9]>{REQUESTED_CLOTHES_STATES.with(|cell|cell.get())}

/// A session owns its resolved `fileStatus.clothesState`. Keep it scoped so a
/// later card in the same process cannot inherit this card's state types.
pub struct ClothesStatesScope(Option<[u8; 9]>);

impl Drop for ClothesStatesScope {
    fn drop(&mut self) {
        ACTIVE_CLOTHES_STATES.with(|cell| cell.set(self.0));
    }
}

pub fn set_session_clothes_states(states: [u8; 9]) -> ClothesStatesScope {
    ClothesStatesScope(ACTIVE_CLOTHES_STATES.with(|cell| cell.replace(Some(states))))
}

/// Parse the requested runtime state; `set_session_clothes_states` applies the
/// card's StateType and linked-garment rules before prefab selection.
pub fn requested_clothes_states() -> [u8; 9] {
    if let Some(states)=REQUESTED_CLOTHES_STATES.with(|cell|cell.get()){return states;}
    let mut states = [0u8; 9];
    if let Ok(raw) = std::env::var("OK_CLOTHES_STATES") {
        for (i, value) in raw.split(',').take(9).enumerate() {
            if let Ok(state) = value.trim().parse::<u8>() {
                states[i] = state.min(3);
            }
        }
    }
    states
}

pub(crate) fn active_clothes_states() -> Option<[u8; 9]> { ACTIVE_CLOTHES_STATES.with(|cell| cell.get()) }
pub fn clothes_states() -> [u8; 9] {
    ACTIVE_CLOTHES_STATES.with(|cell| cell.get().unwrap_or_else(requested_clothes_states))
}

/// ChaFileStatus.MemberInit uses outdoor shoes (1). Studio card import does
/// not load saved Status; scene/runtime controls can explicitly select 0/1.
pub fn requested_shoes_type() -> u8 {
    std::env::var("OK_SHOES_TYPE").ok().and_then(|value| value.parse().ok()).unwrap_or(1)
}

/// Object-level visibility in ChaControl.DrawClothes, before switching the
/// DEF/NUGE child nodes. Gloves, socks and shoes have no loosened object;
/// pantyhose has only DEF and NUGE.
pub fn clothes_kind_visible(kind: usize, states: &[u8; 9], shoes_type: u8) -> bool {
    match kind {
        0..=3 => states[kind] != 3,
        4 | 6 => states[kind] == 0,
        7 => shoes_type == 0 && states[kind] == 0,
        8 => shoes_type == 1 && states[kind] == 0,
        5 => states[kind] <= 1,
        _ => false,
    }
}

fn clothes_state_node_visible(name: &str, states: &[u8; 9], clothes_category: Option<i32>) -> Option<bool> {
    let (prefix, suffix) = name.rsplit_once('_')?;
    let variant = match suffix.to_ascii_lowercase().as_str() {
        "a" => 0, "b" => 1, "c" => 2, _ => return None,
    };
    let prefix = prefix.to_ascii_lowercase();
    let kind = match prefix.as_str() {
        "n_top" | "o_inner" => 0,
        "n_bot" => 1,
        "n_uwt" | "o_bra" => 2,
        "n_uwb" => 3,
        "n_panst" => 5,
        "n_socks" => 6,
        "n_shoes_inner" => 7,
        "n_shoes_outer" | "n_shoes" => 8,
        _ => return None,
    };
    // Bool8.Any(n) in the decompiled DrawClothes is an AND. Main top/bottom
    // and bra variants each contain a region controlled by the adjacent
    // garment's state; top subparts instead use topPartsKey[top_state].
    let selected = match (clothes_category, prefix.as_str()) {
        (Some(200..=202 | 210..=212), _) => states[0] == variant,
        (Some(105), "n_top" | "o_inner") => states[0] == variant,
        (Some(105), "n_bot") => states[0] != 3 && match variant {
            0 => states[1] == 0, 1 => states[1] != 0, _ => false,
        },
        (Some(106), "n_bot") => states[1] == variant,
        (Some(106), "n_top") => states[1] != 2 && match variant {
            0 => states[0] == 0, 1 => states[0] != 0, _ => false,
        },
        (Some(107), "n_top" | "n_uwt" | "o_bra") => states[2] == variant,
        (Some(107), "n_bot") => states[2] != 3 && match variant {
            0 => states[3] == 0, 1 => states[3] != 0, _ => false,
        },
        (Some(108), "n_bot" | "n_uwb") => states[3] == variant,
        (Some(105..=112), _) => states[(clothes_category.unwrap() - 105) as usize] == variant,
        _ => states[kind] == variant,
    };
    Some(selected)
}

fn decode_uvs(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 2]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() || chs.len() < 5 {
        return vec![[0.0, 0.0]; n];
    }
    let (strides, stream_off) = channel_layout(mesh);
    // Illusion meshes store the main texture UV in an arbitrary TexCoord slot
    // (o_body_a uses TexCoord1 while TexCoord0 is garbage). Pick the channel
    // whose values look like real UVs.
    let mut best: Option<(f32, Vec<[f32; 2]>)> = None;
    // The main texture UV can sit in any 2-component channel: the face uses
    // TexCoord0..3 (channels 4-7), the body uses texcoord1 (channel 4 with a
    // 4-byte offset), the pantshot uses channel 3. Scan every 2-component
    // channel and keep the one that looks most like UVs.
    for (ci, c) in chs.iter().enumerate() {
        let _ = ci;
        if c.dimension != 2 || c.stream as usize >= strides.len() {
            continue;
        }
        let stride = strides[c.stream as usize];
        let base = stream_off[c.stream as usize];
        if stride == 0 || base + n * stride > data.len() {
            continue;
        }
        let off = c.offset as usize;
        // Some meshes pad the texture streams to a 16-byte boundary, so the
        // first vertex reads (0,0) and every later vertex takes its
        // predecessor's UV. Try the channel offset and offset+8 and keep the
        // reading with fewer untouched (exactly zero) vertices.
        let mut local_best: Option<(usize, usize, [f32; 2], Vec<[f32; 2]>)> = None;
        for extra in [0usize, 8usize] {
            if base + extra + (n.saturating_sub(1)) * stride + off + 2*format_size(c.format) > data.len() {
                continue;
            }
            let mut uv = Vec::with_capacity(n);
            let mut good = 0usize;
            let mut zeros = 0usize;
            let mut sum = 0.0f64;
            for i in 0..n {
                let o = base + extra + i * stride + off;
                let u = read_vertex_component(data, o, c.format);
                let v = read_vertex_component(data, o + format_size(c.format), c.format);
                // Illusion atlas UVs may be offset outside [0,1] (e.g. the
                // HEXA headband mesh uses V ~ -0.37), so accept a wide but
                // still finite/plausible range.
                if u.is_finite()
                    && v.is_finite()
                    && (-8.0..=8.0).contains(&u)
                    && (-8.0..=8.0).contains(&v)
                {
                    good += 1;
                    sum += u.abs() as f64 + v.abs() as f64;
                }
                if u == 0.0 && v == 0.0 {
                    zeros += 1;
                }
                uv.push([u, v]);
            }
            let cand = (zeros, good, [extra as f32, 0.0], uv);
            if local_best
                .as_ref()
                .map(|b| (cand.0, std::cmp::Reverse(cand.1)) < (b.0, std::cmp::Reverse(b.1)))
                .unwrap_or(true)
            {
                local_best = Some(cand);
            }
        }
        let Some((_zeros, good, _ext, uv)) = local_best else { continue };
        let score = good as f32 / n as f32;
        // Require a non-degenerate UV set (not all zeros).
        let total: f64 = uv.iter().map(|p| p[0].abs() as f64 + p[1].abs() as f64).sum();
        let not_zero = total > 1.0;
        if not_zero && best.as_ref().map(|b| score > b.0).unwrap_or(true) {
            best = Some((score, uv));
        }
    }
    if std::env::var("OK_UVDIAG").is_ok() {
        let info: Vec<String> = chs
            .iter()
            .enumerate()
            .map(|(ci, c)| {
                format!("c{ci}:s{}o{}f{}d{}", c.stream, c.offset, c.format, c.dimension)
            })
            .collect();
        eprintln!(
            "ok-extract: UVDIAG {} n={n} strides={strides:?} soff={stream_off:?} [{}]",
            mesh.name,
            info.join(" ")
        );
    }
    if std::env::var("OK_UVPEEK").is_ok() && mesh.name.to_ascii_lowercase().contains("hitomi") {
        let s = chs[4].stream as usize;
        let b = stream_off[s] as isize;
        for delta in [-16isize, -8, 0, 8, 16] {
            let o = b + delta;
            if o < 0 {
                continue;
            }
            let o = o as usize;
            let pairs: Vec<String> = (0..3)
                .map(|i| {
                    format!(
                        "({:.4},{:.4})",
                        read_f32(data, o + i * 8),
                        read_f32(data, o + i * 8 + 4)
                    )
                })
                .collect();
            eprintln!("ok-extract: UVPEEK {} base{delta:+} {}", mesh.name, pairs.join(" "));
        }
    }
    match best {
        Some((score, uv)) if score > 0.5 => uv,
        _ => vec![[0.0, 0.0]; n],
    }
}

fn decode_texcoord(mesh: &unity_asset_decode::mesh::Mesh, texcoord: usize, extra: usize) -> Vec<[f32; 2]> {
    decode_texcoord_slot(mesh,4+texcoord,extra)
}

fn texcoord_slot_base(version: &UnityVersion) -> usize {
    // Unity changed the serialized ShaderChannel enumeration in 2018:
    // pre-2018 COLOR=2/UV0=3/TANGENT=7; 2018+ TANGENT=2/COLOR=3/UV0=4.
    if version.major >= 2018 {4}else{3}
}

fn decode_texcoord_slot(mesh: &unity_asset_decode::mesh::Mesh, slot: usize, extra: usize) -> Vec<[f32;2]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() {
        return Vec::new();
    }
    // Unity 2019 VertexAttribute: TexCoord0..7 occupy declared slots 4..11.
    // Filtering by dimension shifts sparse channels and misses float3/4 UVs.
    let Some(c) = chs.get(slot).filter(|c|c.dimension >= 2) else {
        return Vec::new();
    };
    let (strides, stream_off) = channel_layout(mesh);
    if c.stream as usize >= strides.len() {
        return Vec::new();
    }
    let stride = strides[c.stream as usize];
    let base = stream_off[c.stream as usize];
    if stride == 0 {
        return Vec::new();
    }
    let off = c.offset as usize;
    if base + extra + (n.saturating_sub(1)) * stride + off + 2*format_size(c.format) > data.len() {
        return Vec::new();
    }
    let mut uv = Vec::with_capacity(n);
    for i in 0..n {
        let o = base + extra + i * stride + off;
        uv.push([read_vertex_component(data,o,c.format),read_vertex_component(data,o+format_size(c.format),c.format)]);
    }
    uv
}

fn decode_vertex_colors(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[u8; 4]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    // Unity Color32 is four UNorm8 components (format 2). Compressed meshes
    // shift its channel index, so locate it by format and dimension.
    let Some(c) = mesh.vertex_data.channels.iter().find(|c| c.dimension == 4 && c.format == 2) else {
        return Vec::new();
    };
    let (strides, offsets) = channel_layout(mesh);
    let s = c.stream as usize;
    if n == 0 || s >= strides.len() || strides[s] == 0 {
        return Vec::new();
    }
    let stride = strides[s];
    let base = offsets[s] + c.offset as usize;
    if base + (n - 1) * stride + 4 > data.len() {
        return Vec::new();
    }
    (0..n).map(|i| {
        let p = base + i * stride;
        [data[p], data[p + 1], data[p + 2], data[p + 3]]
    }).collect()
}

fn channel_layout(mesh: &unity_asset_decode::mesh::Mesh) -> (Vec<usize>, Vec<usize>) {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    let max_stream = chs.iter().map(|c| c.stream).max().unwrap_or(0) as usize;
    let mut strides = vec![0usize; max_stream + 1];
    for c in chs {
        if c.dimension == 0 {
            continue;
        }
        let sz = if c.format == 10 { 4 } else { format_size(c.format) };
        let end = c.offset as usize + sz * c.dimension as usize;
        let s = c.stream as usize;
        strides[s] = strides[s].max(end);
    }
    for s in &mut strides {
        *s = (*s + 3) & !3;
    }
    let mut stream_off = vec![0usize; strides.len()];
    let mut acc = 0usize;
    for i in 0..strides.len() {
        // Unity starts each nonempty vertex stream on a 16-byte boundary.
        // The stride itself stays 4-byte aligned. Without the inter-stream
        // gap, body COLOR0 reads UV data and UV1 becomes UV0.
        if strides[i] != 0 {
            acc = (acc + 15) & !15;
        }
        stream_off[i] = acc;
        acc += strides[i] * n;
    }
    // Inter-stream alignment gaps are not per-vertex stride. Only undeclared
    // whole records beyond the aligned layout extend the final stream (legacy
    // Illusion weight records); trailing alignment padding stays padding.
    if n > 0 && data.len() > acc {
        let extra=(data.len()-acc)/n;
        if extra > 0 {
            if let Some(last)=strides.iter().rposition(|&stride|stride>0) {strides[last]+=extra;}
        }
    }
    (strides, stream_off)
}

fn read_u32_le(data: &[u8], off: usize) -> u32 {
    data.get(off..off + 4)
        .and_then(|s| <[u8; 4]>::try_from(s).ok())
        .map(u32::from_le_bytes)
        .unwrap_or(0)
}

/// Illusion skin records are 32 bytes. Three encodings occur in practice:
/// 1 = `[w0][i0][i1][i2][i3][w1][w2][w3]` (native BoneWeight: body meshes),
/// 2 = `[w0][w1][w2][w3][i0][i1][i2][i3]` (weights then bone indices),
/// 3 = `[i0][i1][i2][i3][w0][w1][w2][w3]` (bone indices then weights).
fn parse_skin_record(rec: &[u8], l: u8) -> ([f32; 4], [u16; 4]) {
    let u = |k: usize| read_u32_le(rec, k * 4);
    let f = |k: usize| read_f32(rec, k * 4);
    let sanitize = |mut w: [f32; 4], idx: [u32; 4]| -> ([f32; 4], [u16; 4]) {
        let mut b = [0u16; 4];
        for k in 0..4 {
            if idx[k] > 255 {
                w[k] = 0.0;
                b[k] = 0;
            } else {
                b[k] = idx[k] as u16;
            }
        }
        (w, b)
    };
    match l {
        1 => sanitize([f(0), f(5), f(6), f(7)], [u(1), u(2), u(3), u(4)]),
        2 => sanitize([f(0), f(1), f(2), f(3)], [u(4), u(5), u(6), u(7)]),
        4 => sanitize([f(5), f(6), f(7), f(0)], [u(1), u(2), u(3), u(4)]),
        // `[i0][i1][i2][w0][w1][w2][w3][i3]`. Head-mod face meshes interleave
        // the last index after the weights; layout 2 then reads index bits as
        // weights and drops the chin/cheek influences (sum << 1).
        5 => sanitize([f(3), f(4), f(5), f(6)], [u(0), u(1), u(2), u(7)]),
        _ => sanitize([f(4), f(5), f(6), f(7)], [u(0), u(1), u(2), u(3)]),
    }
}

/// A correctly decoded record has four plausible weights and small bone
/// indices. Unused slots are 0xFFFFFFFF (Koikatsu face meshes); those are
/// already zeroed by `parse_skin_record`. A wrong layout reads float bits as
/// indices (huge ??weight 0) or index integers as weights (out of range).
fn skin_record_valid(w: &[f32; 4], b: &[u16; 4]) -> bool {
    w.iter().all(|v| v.is_finite() && *v >= -0.01 && *v <= 1.01)
        && {
            let s = w.iter().sum::<f32>();
            s > 0.5 && s < 1.5
        }
        && w.iter().zip(b.iter()).all(|(wi, bi)| *wi <= 0.02 || (*bi as u32) < 256)
}

fn decode_weights(mesh: &unity_asset_decode::mesh::Mesh) -> (Vec<[f32; 4]>, Vec<[u16; 4]>) {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if std::env::var("OK_MAYUDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("mayu") {
        let info: Vec<String> = chs
            .iter()
            .enumerate()
            .map(|(i, c)| format!("{i}:s{}o{}f{}d{}", c.stream, c.offset, c.format, c.dimension))
            .collect();
        eprintln!(
            "ok-extract: SKINCH mayuge n={n} data={} chs={} [{}]",
            data.len(),
            chs.len(),
            info.join(" ")
        );
        eprintln!(
            "ok-extract: SKINCH mayuge compressed={} comp_weights={:?} comp_bones={:?}",
            mesh.compressed_mesh.is_some(),
            mesh.compressed_mesh.as_ref().map(|c| c.weights.data.len()),
            mesh.compressed_mesh.as_ref().map(|c| c.bone_indices.data.len()),
        );
    }
    if std::env::var("OK_SKINCH").is_ok() && n > 2000 {
        let info: Vec<String> = chs
            .iter()
            .enumerate()
            .map(|(i, c)| format!("{i}:s{}o{}f{}d{}", c.stream, c.offset, c.format, c.dimension))
            .collect();
        eprintln!("ok-extract: SKINCH n={n} chs={} [{}]", chs.len(), info.join(" "));
        eprintln!(
            "ok-extract: SKINCH compressed={:?} comp_weights={:?}",
            mesh.compressed_mesh.is_some(),
            mesh.compressed_mesh
                .as_ref()
                .map(|c| (c.weights.data.len(), c.bone_indices.data.len()))
        );
        if chs.len() < 14 && n == 9048 {
            let (strides, stream_off) = channel_layout(mesh);
            eprintln!(
                "ok-extract: SACREC n={n} data={} per_vert={} strides={:?} off={:?}",
                data.len(),
                data.len() / n.max(1),
                strides,
                stream_off
            );
            for i in 0..3 {
                let o0 = stream_off[0] + i * strides[0];
                let o1 = stream_off[1] + i * strides[1];
                if o0 + strides[0] <= data.len() && o1 + strides[1] <= data.len() {
                    eprintln!(
                        "ok-extract: SACREC v{i} s0={:?} s1={:?}",
                        &data[o0..o0 + strides[0]],
                        &data[o1..o1 + strides[1]]
                    );
                }
            }
        }
    }
    if n == 0 || data.is_empty() || chs.len() < 14 {
        return (Vec::new(), Vec::new());
    }
    let cw = &chs[12];
    let cb = &chs[13];
    // Meshes whose skin has fewer than four influences (e.g. `cf_O_mayuge`
    // ships two: ch12 = 2xf32 weights, ch13 = 2xu32 bone indices) are stored
    // as ordinary vertex channels rather than 32-byte Illusion records. Decode
    // those generically, padding to four influences.
    if (cw.dimension as usize) < 4 || (cb.dimension as usize) < 4 {
        if cw.stream != cb.stream || cw.dimension == 0 || cb.dimension == 0 {
            return (Vec::new(), Vec::new());
        }
        let (strides, stream_off) = channel_layout(mesh);
        let s = cw.stream as usize;
        let stride = strides[s];
        let base = stream_off[s];
        if stride == 0 || base + n * stride > data.len() {
            return (Vec::new(), Vec::new());
        }
        let infl = (cw.dimension as usize).min(cb.dimension as usize).min(4);
        let wsz = format_size(cw.format);
        let isz = format_size(cb.format);
        let mut weights = Vec::with_capacity(n);
        let mut bones = Vec::with_capacity(n);
        for i in 0..n {
            let wb = base + cw.offset as usize + i * stride;
            let bb = base + cb.offset as usize + i * stride;
            let mut w = [0.0f32; 4];
            let mut b = [0u16; 4];
            for k in 0..infl {
                w[k] = read_attr_weight(data, wb + k * wsz, cw.format);
                b[k] = read_attr_bone(data, bb + k * isz, cb.format);
            }
            weights.push(w);
            bones.push(b);
        }
        if std::env::var("OK_SKINCH").is_ok() || std::env::var("OK_MAYUDIAG").is_ok() {
            eprintln!(
                "ok-extract: SKINATTR {} n={n} infl={infl} stride={stride} w(s={} o={} fmt={} dim={}) b(s={} o={} fmt={} dim={}) v0=w{:?} b{:?}",
                mesh.name,
                cw.stream, cw.offset, cw.format, cw.dimension,
                cb.stream, cb.offset, cb.format, cb.dimension,
                weights[0], bones[0]
            );
        }
        return (weights, bones);
    }
    if cw.stream != 2 || cb.stream != 2 {
        return (Vec::new(), Vec::new());
    }
    let (strides, stream_off) = channel_layout(mesh);
    let s2 = cw.stream as usize;
    let stride = strides[s2];
    let base = stream_off[s2];
    if stride < 32 || base + n * stride > data.len() {
        return (Vec::new(), Vec::new());
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: DIAG skin bind(s={},off={},fmt={},dim={}) wt(s={},off={},fmt={},dim={}) stride={stride} base={base} n={n}",
            cb.stream, cb.offset, cb.format, cb.dimension,
            cw.stream, cw.offset, cw.format, cw.dimension
        );
    }
    // Pick the skin encoding and stream start together. Unity aligns vertex
    // streams to 16 bytes; summing channel strides can leave the computed
    // start 4/8/12 bytes before the first record. Some meshes also prefix a
    // stream with another 16 bytes. A misaligned record can still look valid
    // while rotating the four weights and losing one influence.
    let mut layout = 3u8;
    let mut best = 0usize;
    let mut best_sum_error = f64::INFINITY;
    let mut chosen_base = base;
    for off in (0usize..=28).step_by(4) {
        if base + off + n * stride > data.len() {
            continue;
        }
        for l in [1u8, 2, 3, 4] {
            let (c, sum_error) = (0..n)
                .fold((0usize, 0.0f64), |(valid, error), i| {
                    let o = base + off + i * stride;
                    let (w, b) = parse_skin_record(&data[o..o + 32], l);
                    if skin_record_valid(&w, &b) {
                        (valid + 1, error + (w.iter().sum::<f32>() - 1.0).abs() as f64)
                    } else {
                        (valid, error)
                    }
                });
            // Layouts 1 and 3 can both pass the broad validity check: treating
            // an integer bone index as float weight produces a near-zero value,
            // shifts the other three weights, and silently loses the fourth.
            // The authored weights sum to one; use that invariant to break ties.
            if c > best || (c == best && sum_error < best_sum_error) {
                best = c;
                best_sum_error = sum_error;
                layout = l;
                chosen_base = base + off;
            }
        }
    }
    let base = chosen_base;
    // Only fall back to the legacy layout when nothing validated.
    if best == 0 {
        layout = 3;
    }
    if let Ok(s) = std::env::var("OK_SKIN_LAYOUT") {
        if let Ok(v) = s.trim().parse::<u8>() {
            layout = v;
        }
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: DIAG skin n={n} stride={stride} base={base} lay={layout} best_valid={best}"
        );
    }
    let mut weights = Vec::with_capacity(n);
    let mut bones = Vec::with_capacity(n);
    let want: Vec<usize> = std::env::var("OK_DUMP_V")
        .ok()
        .map(|s| {
            s.split(',')
                .filter_map(|x| x.trim().parse::<usize>().ok())
                .collect()
        })
        .unwrap_or_default();
    for i in 0..n {
        let off = base + i * stride;
        let rec = &data[off..off + 32];
        let (mut w, mut b) = parse_skin_record(rec, layout);
        // Head-mod faces mix the standard record with `[i0 i1 i2 w0 w1 w2 w3 i3]`.
        // The mesh-wide layout leaves those chin/cheek verts at sum << 1, stuck
        // on FaceRoot. Retry only the broken records so valid ones stay put.
        if w.iter().sum::<f32>() < 0.5 {
            let (w5, b5) = parse_skin_record(rec, 5);
            if w5.iter().sum::<f32>() > 0.9 && skin_record_valid(&w5, &b5) {
                w = w5;
                b = b5;
            }
        }
        if want.contains(&i) {
            let hex: Vec<String> = rec.iter().map(|x| format!("{x:02x}")).collect();
            let prev = if off >= 16 {
                let p: Vec<String> = data[off - 16..off].iter().map(|x| format!("{x:02x}")).collect();
                p.join("")
            } else {
                String::new()
            };
            eprintln!(
                "ok-extract: DUMP {} n={n} v{i} stride={stride} base={base} lay={layout} b=[{},{},{},{}] w=[{:.3},{:.3},{:.3},{:.3}] prev16={} hex={}",
                mesh.name,
                b[0], b[1], b[2], b[3], w[0], w[1], w[2], w[3], prev, hex.join("")
            );
        }
        weights.push(w);
        bones.push(b);
    }
    (weights, bones)
}

fn decode_indices(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<u32> {
    let buf = &mesh.index_buffer;
    if mesh.index_format == 1 {
        buf.chunks_exact(4)
            .filter_map(|c| c.try_into().ok())
            .map(u32::from_le_bytes)
            .collect()
    } else {
        buf.chunks_exact(2)
            .filter_map(|c| c.try_into().ok())
            .map(|b: [u8; 2]| u16::from_le_bytes(b) as u32)
            .collect()
    }
}

pub fn mesh_to_obj(name: &str, pos: &[[f32; 3]], uv: &[[f32; 2]], idx: &[u32]) -> String {
    let mut s = String::new();
    s.push_str(&format!("# {name} verts={} tris={}\n", pos.len(), idx.len() / 3));
    for p in pos {
        s.push_str(&format!("v {} {} {}\n", p[0], p[1], p[2]));
    }
    for t in uv {
        s.push_str(&format!("vt {} {}\n", t[0], t[1]));
    }
    let has_uv = uv.len() == pos.len() && uv.iter().any(|t| t[0].abs() + t[1].abs() > 0.0001);
    for tri in idx.chunks_exact(3) {
        if tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2] {
            continue;
        }
        if has_uv {
            s.push_str(&format!(
                "f {}/{} {}/{} {}/{}\n",
                tri[0] + 1,
                tri[0] + 1,
                tri[1] + 1,
                tri[1] + 1,
                tri[2] + 1,
                tri[2] + 1
            ));
        } else {
            s.push_str(&format!("f {} {} {}\n", tri[0] + 1, tri[1] + 1, tri[2] + 1));
        }
    }
    s
}

fn basename_lower(s: &str) -> String {
    s.replace('\\', "/")
        .rsplit('/')
        .next()
        .unwrap_or(s)
        .to_ascii_lowercase()
}

fn try_slice(data: &[u8], offset: u64, size: u32) -> Option<Vec<u8>> {
    if size == 0 {
        return None;
    }
    let start = offset as usize;
    let end = start.saturating_add(size as usize);
    if end <= data.len() {
        Some(data[start..end].to_vec())
    } else {
        None
    }
}

fn load_stream<B: StreamBytes>(files: &[(String, B)], path: &str, offset: u64, size: u32) -> Option<Vec<u8>> {
    if size == 0 {
        return None;
    }
    let want = basename_lower(path);
    if !want.is_empty() {
        for (n, data) in files {
            let bn = basename_lower(n);
            if bn == want || bn.ends_with(&want) || want.ends_with(&bn) {
                if let Some(b) = data.read_range(offset,size) {
                    return Some(b);
                }
            }
        }
    }
    for (n, data) in files {
        let l = n.to_ascii_lowercase();
        if l.ends_with(".ress") || l.ends_with(".resource") {
            if let Some(b) = data.read_range(offset,size) {
                return Some(b);
            }
        }
    }
    if want.is_empty() {
        for (_, data) in files {
            if let Some(b) = data.read_range(offset,size) {
                return Some(b);
            }
        }
    }
    None
}

struct CursorLe<'a> {
    d: &'a [u8],
    p: usize,
}

impl<'a> CursorLe<'a> {
    fn new(d: &'a [u8]) -> Self {
        Self { d, p: 0 }
    }
    fn align(&mut self) {
        self.p = (self.p + 3) & !3;
    }
    fn remaining(&self) -> usize {
        self.d.len().saturating_sub(self.p)
    }
    fn i32(&mut self) -> Option<i32> {
        let b = self.d.get(self.p..self.p + 4)?.try_into().ok()?;
        self.p += 4;
        Some(i32::from_le_bytes(b))
    }
    fn u32(&mut self) -> Option<u32> {
        Some(self.i32()? as u32)
    }
    fn u64(&mut self) -> Option<u64> {
        let b = self.d.get(self.p..self.p + 8)?.try_into().ok()?;
        self.p += 8;
        Some(u64::from_le_bytes(b))
    }
    fn bool(&mut self) -> Option<bool> {
        let v = *self.d.get(self.p)?;
        self.p += 1;
        Some(v != 0)
    }
    fn aligned_string(&mut self) -> Option<String> {
        let n = self.u32()? as usize;
        if self.p + n > self.d.len() {
            return None;
        }
        let s = String::from_utf8_lossy(&self.d[self.p..self.p + n]).into_owned();
        self.p += n;
        self.align();
        Some(s)
    }
    fn byte_array(&mut self) -> Option<Vec<u8>> {
        let n = self.i32()? as usize;
        if n == 0 {
            self.align();
            return Some(Vec::new());
        }
        if n > self.remaining() {
            return None;
        }
        let b = self.d[self.p..self.p + n].to_vec();
        self.p += n;
        self.align();
        Some(b)
    }
}

fn parse_tex_unity2019(data: &[u8]) -> Option<unity_asset_decode::texture::Texture2D> {
    use unity_asset_decode::texture::{Texture2D, TextureFormat};
    let mut r = CursorLe::new(data);
    let name = r.aligned_string()?;
    let _forced = r.i32()?;
    let _down = r.bool()?;
    let _alpha = r.bool()?;
    r.align();
    let width = r.i32()?;
    let height = r.i32()?;
    let complete = r.i32()?;
    let _mips_stripped = r.i32()?;
    let format_val = r.i32()?;
    let mip_count = r.i32()?;
    let is_readable = r.bool()?;
    let _pre = r.bool()?;
    let _ignore = r.bool()?;
    let _stream_mips = r.bool()?;
    r.align();
    let _prio = r.i32()?;
    let image_count = r.i32()?;
    let dim = r.i32()?;
    let _filter = r.i32()?;
    let _aniso = r.i32()?;
    let _bias = r.i32()?;
    let _wrap_u = r.i32()?;
    let _wrap_v = r.i32()?;
    let _wrap_w = r.i32()?;
    let light = r.i32()?;
    let color_space = r.i32()?;
    let _blob = r.byte_array()?;
    let image_data = r.byte_array().unwrap_or_default();
    let mut stream_off = 0u64;
    let mut stream_size = 0u32;
    let mut stream_path = String::new();
    if r.remaining() >= 12 {
        stream_off = r.u64().unwrap_or(0);
        stream_size = r.u32().unwrap_or(0);
        stream_path = r.aligned_string().unwrap_or_default();
    }
    if width < 4 || height < 4 || width > 8192 || height > 8192 {
        return None;
    }
    let mut tex = Texture2D::new(name, width, height, TextureFormat::from(format_val));
    tex.complete_image_size = complete;
    tex.mip_count = mip_count;
    tex.is_readable = is_readable;
    tex.image_count = image_count;
    tex.texture_dimension = dim;
    tex.light_map_format = light;
    tex.color_space = color_space;
    tex.image_data = image_data;
    tex.data_size = tex.image_data.len() as i32;
    tex.stream_info.path = stream_path;
    tex.stream_info.offset = stream_off;
    tex.stream_info.size = stream_size;
    Some(tex)
}

/// Unity meshes that store skin data in the newer `m_Skin` (BoneWeight1 array)
/// field instead of vertex channels. Returns (weights, bone indices) in the
/// SMR's own bone order.
fn decode_skin_object(
    object: &UnityObject,
) -> Option<(Vec<[f32; 4]>, Vec<[u16; 4]>)> {
    let skin_opt = object.class.properties().get("m_Skin");
    if std::env::var("OK_MAYUDIAG").is_ok() {
        let nm = object.name().unwrap_or_default();
        if nm.to_ascii_lowercase().contains("mayu") {
            match skin_opt {
                Some(unity_asset_core::UnityValue::Array(a)) => {
                    let keys = a.first().and_then(|e| match e {
                        unity_asset_core::UnityValue::Object(o) => {
                            Some(o.keys().cloned().collect::<Vec<_>>())
                        }
                        _ => None,
                    });
                    eprintln!("ok-extract: SKINOBJ {nm} m_Skin len={} first_keys={keys:?}", a.len());
                }
                Some(v) => eprintln!("ok-extract: SKINOBJ {nm} m_Skin not array: {v:?}"),
                None => eprintln!("ok-extract: SKINOBJ {nm} m_Skin MISSING"),
            }
        }
    }
    let skin = skin_opt?;
    let arr = match skin {
        unity_asset_core::UnityValue::Array(a) => a,
        _ => return None,
    };
    if arr.is_empty() {
        return None;
    }
    let mut weights = Vec::with_capacity(arr.len());
    let mut bones = Vec::with_capacity(arr.len());
    for e in arr {
        let unity_asset_core::UnityValue::Object(o) = e else {
            return None;
        };
        let getf = |k: &str| -> f32 {
            o.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32
        };
        let geti = |k: &str| -> u16 {
            o.get(k).and_then(|v| v.as_i64()).unwrap_or(0) as u16
        };
        weights.push([getf("weight[0]"), getf("weight[1]"), getf("weight[2]"), getf("weight[3]")]);
        bones.push([
            geti("boneIndex[0]"),
            geti("boneIndex[1]"),
            geti("boneIndex[2]"),
            geti("boneIndex[3]"),
        ]);
    }
    if weights.iter().all(|w| w.iter().all(|x| *x == 0.0)) {
        return None;
    }
    Some((weights, bones))
}

fn decoded_from_object(
    object: &UnityObject,
    ver: &UnityVersion,
    stream_bytes: Option<&[u8]>,
    source_version: &str,
) -> Option<ExtractedMesh> {
    let mut mesh = match parse_mesh(object, ver) {
        Ok(mesh)=>mesh,
        Err(error)=>{eprintln!("ok-extract: mesh decode failed name={} path={} error={error}",object.name().unwrap_or_default(),object.path_id());return None;}
    };
    let authored_tangents = decode_tangents(&mesh);
    let compressed_tangents = object.class.properties().get("m_CompressedMesh")
        .and_then(|v| match v { unity_asset_core::UnityValue::Object(fields) => fields.get("m_Tangents"), _ => None })
        .and_then(|v| match v { unity_asset_core::UnityValue::Object(fields) => fields.get("m_NumItems"), _ => None })
        .and_then(|v| v.as_i64())
        .is_some_and(|count| count > 0);
    if std::env::var("OK_SKINCH").is_ok() && mesh.name == "o_body_a" && mesh.vertex_data.vertex_count > 8000 {
        if let Some(unity_asset_core::UnityValue::Object(vd)) =
            object.class.properties().get("m_VertexData")
        {
            if let Some(unity_asset_core::UnityValue::Array(chans)) = vd.get("m_Channels") {
                eprintln!("ok-extract: VD chans={}", chans.len());
                for (i, c) in chans.iter().enumerate() {
                    eprintln!("ok-extract: VD ch{i} {c:?}");
                }
            }
            if let Some(v) = vd.get("m_Streams") {
                eprintln!("ok-extract: VD streams={v:?}");
            }
        }
        for k in ["m_CompressedMesh", "m_MeshCompression", "m_BindPose", "m_BoneNameHashes"] {
            if let Some(v) = object.class.properties().get(k) {
                let s = format!("{v:?}");
                eprintln!("ok-extract: VD {} = {}", k, &s[..s.len().min(300)]);
            }
        }
        if let Some(skin) = object.class.properties().get("m_Skin") {
            match skin {
                unity_asset_core::UnityValue::Bytes(b) => {
                    eprintln!("ok-extract: VD m_Skin Bytes len={} head={:?}", b.len(), &b[..b.len().min(40)]);
                }
                unity_asset_core::UnityValue::Array(a) => {
                    eprintln!("ok-extract: VD m_Skin Array len={} first={:?}", a.len(), a.first());
                }
                other => {
                    let s = format!("{other:?}");
                    eprintln!("ok-extract: VD m_Skin other {}", &s[..s.len().min(200)]);
                }
            }
        }
        let keys: Vec<&String> = object.class.properties().keys().collect();
        eprintln!("ok-extract: VD keys={keys:?}");
    }
    if std::env::var("OK_SHAPES").is_ok()
        && (mesh.name == "o_body_a" || mesh.name.eq_ignore_ascii_case("cf_O_face"))
    {
        if let Some(shapes) = object.class.properties().get("m_Shapes") {
            if let unity_asset_core::UnityValue::Object(so) = shapes {
                if std::env::var("OK_SHAPES_KEYS").is_ok() {
                    let keys: Vec<&String> = so.keys().collect();
                    let sizes: Vec<String> = so
                        .iter()
                        .map(|(k, v)| match v {
                            unity_asset_core::UnityValue::Array(a) => format!("{k}:{}", a.len()),
                            _ => format!("{k}:?"),
                        })
                        .collect();
                    eprintln!("ok-extract: SHAPESKEYS {} keys={:?} sizes={:?}", mesh.name, keys, sizes);
                }
                if let Some(unity_asset_core::UnityValue::Array(chans)) = so.get("channels") {
                    eprintln!("ok-extract: SHAPES {} channels={}", mesh.name, chans.len());
                    for (ci, c) in chans.iter().enumerate() {
                        if let unity_asset_core::UnityValue::Object(co) = c {
                            let name = co
                                .get("name")
                                .and_then(|v| v.as_str())
                                .unwrap_or("?");
                            let fw = co.get("fullWeights");
                            eprintln!("ok-extract: SHAPES ch{ci} name={name} fullWeights={fw:?}");
                        }
                    }
                }
                if std::env::var("OK_SHAPES_KEYS").is_ok() {
                    if let Some(unity_asset_core::UnityValue::Array(sh)) = so.get("shapes") {
                        for (i, s) in sh.iter().take(3).enumerate() {
                            eprintln!("ok-extract: SHAPES {}-shape[{i}]={s:?}", mesh.name);
                        }
                    }
                    if let Some(unity_asset_core::UnityValue::Array(fw)) = so.get("fullWeights") {
                        eprintln!("ok-extract: SHAPES {}-fullWeights[0..3]={:?}", mesh.name, &fw[..3.min(fw.len())]);
                    }
                    if let Some(unity_asset_core::UnityValue::Array(vs)) = so.get("vertices") {
                        for (i, v) in vs.iter().take(2).enumerate() {
                            eprintln!("ok-extract: SHAPES {}-v[{i}]={v:?}", mesh.name);
                        }
                    }
                }
            }
        }
    }
    if mesh.vertex_data.data_size.is_empty() {
        if let Some(b) = stream_bytes {
            mesh.vertex_data.data_size = b.to_vec();
        }
    }
    let pos = decode_positions(&mesh);
    let idx = decode_indices(&mesh);
    let index_bytes = if mesh.index_format == 1 { 4 } else { 2 };
    let submesh_ranges = mesh.sub_meshes.iter().map(|s| (
        s.first_byte as usize / index_bytes,
        s.index_count as usize,
        s.base_vertex,
    )).collect();
    // A single triangle or quad is a valid Unity Mesh (many accessory
    // primitives are four-vertex planes). The old eight-vertex threshold
    // silently dropped these prefabs even when their renderer was enabled.
    if pos.len() < 3 || idx.len() < 3 {
        eprintln!("ok-extract: mesh omitted name={} vertices={} indices={} source_vertices={} data={} warnings={:?}",mesh.name,pos.len(),idx.len(),mesh.vertex_data.vertex_count,mesh.vertex_data.data_size.len(),object.typetree_warnings());
        return None;
    }
    if !pos.iter().any(|p| p[0].abs() + p[1].abs() + p[2].abs() > 0.0001) {
        eprintln!("ok-extract: mesh omitted all-zero positions name={} vertices={} data={} warnings={:?}",mesh.name,pos.len(),mesh.vertex_data.data_size.len(),object.typetree_warnings());
        return None;
    }
    let uv = decode_uvs(&mesh);
    let source_version=UnityVersion::parse_version(source_version).unwrap_or_else(|_|ver.clone());
    let uv_base=texcoord_slot_base(&source_version);
    let uv2 = decode_texcoord_slot(&mesh, uv_base+1, 0);
    let uv3 = decode_texcoord_slot(&mesh, uv_base+2, 0);
    let uv4 = decode_texcoord_slot(&mesh, uv_base+3, 0);
    let vertex_colors = decode_vertex_colors(&mesh);
    // Diagnostic `OK_FACE_DROP_CAVITY`: the vanilla face texture `cf_face_00_t`
    // paints the mouth cavity as a dark rectangle (u 0.32-0.68, v_img 0.83-0.99
    // => Unity v 0.01-0.17). A few `cf_O_face` front-facing chin triangles sample
    // it and show a dark patch. Drop those triangles to test whether the patch
    // is that inner-mouth geometry.
    let mut idx = idx;
    if std::env::var("OK_FACE_DROP_CAVITY").is_ok() {
        eprintln!("ok-extract: DROPCAVITY seen mesh name={:?}", mesh.name);
    }
    if std::env::var("OK_FACE_DROP_CAVITY").is_ok() && mesh.name.eq_ignore_ascii_case("cf_O_face") {
        let mut kept = Vec::with_capacity(idx.len());
        for t in idx.chunks_exact(3) {
            let all = t.iter().all(|&v| {
                let i = v as usize;
                let q = uv.get(i).copied().unwrap_or([0.0; 2]);
                (0.28..=0.72).contains(&q[0]) && (0.78..=1.0).contains(&q[1])
            });
            if !all {
                kept.extend_from_slice(t);
            }
        }
        eprintln!(
            "ok-extract: DROPCAVITY {} tris {} -> {}",
            mesh.name,
            idx.len() / 3,
            kept.len() / 3
        );
        idx = kept;
    }
    let (mut weights, mut bone_idx) = match decode_skin_object(object) {
        Some(wb) if wb.0.len() == pos.len() => wb,
        _ => decode_weights(&mesh),
    };
    let bind_poses = crate::pose::bind_poses_from_object(object);
    // 1-bone meshes (iris `cf_Ohitomi_*02`, tongue) often omit the weight
    // vertex channels; Unity then rigid-skins every vertex to bone 0.
    // Without this, remap/skin skip the mesh and `apply_parent` parks it
    // on the head root instead of `cf_J_hitomi_tx_*`.
    // Only do this for single-bone meshes: applying it to a multi-bone mesh
    // whose weights failed to parse would pin everything to bone 0 (this is
    // what distorted one eyebrow half when `cf_O_mayuge`'s 2-influence skin
    // was not decoded).
    if weights.len() != pos.len() && !bind_poses.is_empty() {
        // Several rigid parts (iris, tongue, some accessories like
        // `on_neck_star_stone`) omit the weight channels; Unity then rigid-skins
        // every vertex to bone 0. Keep this fallback for them, but still warn:
        // on a genuinely multi-weight mesh it silently pins everything to bone 0
        // (which distorted one eyebrow half until `cf_O_mayuge`'s 2-influence
        // skin was decoded above).
        eprintln!(
            "ok-extract: WARN {} weights {} != verts {} (binds {}); rigid bone-0 fallback",
            mesh.name,
            weights.len(),
            pos.len(),
            bind_poses.len()
        );
        weights = vec![[1.0, 0.0, 0.0, 0.0]; pos.len()];
        bone_idx = vec![[0u16; 4]; pos.len()];
    }
    let normals = decode_normals(&mesh);
    let obj = mesh_to_obj(&mesh.name, &pos, &uv, &idx);
    Some(ExtractedMesh {
        runtime_textures: Default::default(), texture_plan: Vec::new(),
        shader_extra_textures: Default::default(),
        name: mesh.name,
        obj,
        path_id: object.path_id(),
        renderer_ids: Vec::new(),
        renderer_names: Vec::new(),
        renderer_skinned: None,
        mat: None,
        submesh_ranges,
        submesh_index: 0,
        hair_shader_hint: false,
        texture_png: None,
        texture_name: String::new(),
        color_mask_png: None,
        alpha_mask_png: None,
        detail_mask_png: None,
        line_mask_png: None,
        normal_map_png: None,
        normal_mask_png: None,
        source_tangents: Some(!authored_tangents.is_empty() || compressed_tangents),
        tangents: authored_tangents,
        hair_gloss_png: None,
        positions: pos,
        normals,
        rest_normals: Vec::new(),
        uvs: uv,
        uv2,
        uv3,
        uv4,
        vertex_colors,
        indices: idx,
        weights,
        bone_idx,
        bind_poses,
        bone_names: Vec::new(),
        smr_skel: None,
        bone_implants: Vec::new(),
        rest_positions: Vec::new(),
        parent_bone: None,
        rigid_follow_bone: None,
        skel_kind: 0,
        blend_shapes: decode_blend_shapes(object),
        prefab_local: None,
        move_nodes: [None, None],
        deferred_dbones: None,
    })
}

/// Parse Unity `m_Shapes` into (channel name, deltas). Each delta is stored at
/// the frame weight recorded in `fullWeights` (Koikatsu uses 100).
fn decode_blend_shapes(
    object: &unity_asset_decode::object::UnityObject,
) -> Vec<(String, Vec<(u32, [f32; 3])>)> {
    use unity_asset_core::UnityValue;
    let mut out = Vec::new();
    let Some(UnityValue::Object(so)) = object.class.properties().get("m_Shapes") else {
        return out;
    };
    let (Some(UnityValue::Array(chans)), Some(UnityValue::Array(shapes)), Some(UnityValue::Array(verts))) =
        (so.get("channels"), so.get("shapes"), so.get("vertices"))
    else {
        return out;
    };
    for (ci, c) in chans.iter().enumerate() {
        let UnityValue::Object(co) = c else { continue };
        let name = co.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string();
        let frame = co.get("frameIndex").and_then(|v| v.as_i64()).unwrap_or(ci as i64);
        let si = if frame >= 0 { frame as usize } else { ci };
        if si >= shapes.len() {
            continue;
        }
        let Some(sobj) = shapes[si].as_object() else { continue };
        let first = sobj.get("firstVertex").and_then(|v| v.as_i64()).unwrap_or(-1);
        let count = sobj.get("vertexCount").and_then(|v| v.as_i64()).unwrap_or(0);
        if first < 0 || count <= 0 {
            out.push((name, Vec::new()));
            continue;
        }
        let mut deltas = Vec::with_capacity(count as usize);
        for k in first as usize..(first as usize + count as usize) {
            let Some(UnityValue::Object(ve)) = verts.get(k) else { continue };
            let idx = ve.get("index").and_then(|v| v.as_i64()).unwrap_or(-1);
            if idx < 0 {
                continue;
            }
            let Some(UnityValue::Object(vv)) = ve.get("vertex") else { continue };
            let x = vv.get("x").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            let y = vv.get("y").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            let z = vv.get("z").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            deltas.push((idx as u32, [x, y, z]));
        }
        out.push((name, deltas));
    }
    out
}

fn recover_inline_texture_data(raw: &[u8], min_bytes: usize) -> Option<Vec<u8>> {
    let mut found = None;
    // The inline image size precedes the mip chain. Name padding and Unity
    // version change its header offset; streamed textures have no such blob.
    let name_bytes=u32::from_le_bytes(raw.get(..4)?.try_into().ok()?) as usize;
    let after_name=(name_bytes.checked_add(7)?) & !3;
    if after_name>raw.len(){return None;}
    let header_end=raw.len().min(after_name.checked_add(256)?);
    for pos in (after_name..header_end.saturating_sub(4)).step_by(4) {
        let size = u32::from_le_bytes(raw[pos..pos + 4].try_into().ok()?) as usize;
        let remaining = raw.len().saturating_sub(pos + 4);
        if size >= min_bytes && size <= remaining && remaining - size <= 128 {
            found = Some((pos + 4, size));
        }
    }
    let (start, size) = found?;
    Some(raw[start..start + size].to_vec())
}

fn repair_inline_texture_payload(raw: &[u8], tex: &mut unity_asset_decode::texture::Texture2D, min_bytes: usize) {
    // A best-effort converter can return the header/trailer along with the
    // mip chain. A nonempty payload large enough for mip 0 is not proof that
    // it starts at the declared byte array. Prefer the serialized array count
    // and its bounds for both truncated and oversized converter results.
    if let Some(bytes) = recover_inline_texture_data(raw, min_bytes) {
        if tex.image_data != bytes {
            if std::env::var_os("OK_TEXDIAG").is_some() {
                eprintln!("Texture payload repaired name={} old={} declared={}",tex.name,tex.image_data.len(),bytes.len());
            }
            tex.data_size = bytes.len() as i32;
            tex.image_data = bytes;
        }
    }
}

fn read_texture_payload<B: StreamBytes>(object:&UnityObject,files:&[(String,B)],raw_bundle:Option<&[u8]>)->Option<unity_asset_decode::texture::Texture2D> {
    use unity_asset_decode::texture::{Texture2D, TextureFormat, TextureProcessor};
    let proc = TextureProcessor::new(version());
    let mut tex = proc
        .convert_object(object)
        .ok()
        .or_else(|| parse_tex_unity2019(object.raw_data()))?;
    let fill_stream = |t: &mut Texture2D| {
        if !t.image_data.is_empty() || !t.is_streamed() {
            return;
        }
        if let Some(b) = load_stream(
            files,
            &t.stream_info.path,
            t.stream_info.offset,
            t.stream_info.size,
        ) {
            t.image_data = b;
            t.data_size = t.image_data.len() as i32;
        } else if let Some(raw) = raw_bundle {
            if let Some(b) = try_slice(raw, t.stream_info.offset, t.stream_info.size) {
                t.image_data = b;
                t.data_size = t.image_data.len() as i32;
            }
        }
    };
    fill_stream(&mut tex);
    if tex.image_data.is_empty() {
        if let Some(mut t2) = parse_tex_unity2019(object.raw_data()) {
            fill_stream(&mut t2);
            if !t2.image_data.is_empty() {
                tex = t2;
            }
        }
    }
    // Validate the declared inline mip chain. The converter can return either
    // a tiny prefix or a large span that still includes part of the header.
    let blocks = if tex.width >= 4 && tex.height >= 4 {
        ((tex.width as usize + 3) / 4)
            .checked_mul((tex.height as usize + 3) / 4)
    } else {
        None
    };
    let top_mip_bytes = match tex.format {
        TextureFormat::DXT1 => blocks.and_then(|count| count.checked_mul(8)),
        TextureFormat::DXT5 | TextureFormat::BC7 => blocks.and_then(|count| count.checked_mul(16)),
        TextureFormat::Alpha8 => (tex.width as usize).checked_mul(tex.height as usize),
        TextureFormat::RGB24 => (tex.width as usize).checked_mul(tex.height as usize)?.checked_mul(3),
        TextureFormat::RGBA32 | TextureFormat::ARGB32 | TextureFormat::BGRA32 =>
            (tex.width as usize).checked_mul(tex.height as usize)?.checked_mul(4),
        TextureFormat::RGB565 | TextureFormat::RGBA4444 | TextureFormat::ARGB4444 | TextureFormat::R16 =>
            (tex.width as usize).checked_mul(tex.height as usize)?.checked_mul(2),
        _ => None,
    };
    // Texels read from the stream (.resS) are exactly what Unity uploads; the inline-array repair would take a
    // false length match in the object header instead (small streamed textures, e.g. the 8x8 Studio ColorMasks).
    let from_stream = tex.is_streamed() && tex.image_data.len() == tex.stream_info.size as usize;
    if let Some(min_bytes) = top_mip_bytes.filter(|_| !from_stream) {
        repair_inline_texture_payload(object.raw_data(), &mut tex, min_bytes);
    }
    Some(tex)
}

fn png_from_texture<B: StreamBytes>(
    object: &UnityObject,
    files: &[(String, B)],
    raw_bundle: Option<&[u8]>,
) -> Option<(String, Vec<u8>)> {
    let tex=read_texture_payload(object,files,raw_bundle)?;
    png_from_payload(tex,object.name())
}

/// PNG of a texture's texels (OK textures store the texels; Unity and OK
/// paths decode through this one function).
/// A Texture2D object of `files` (a parsed bundle's files, for streamed texels) as PNG, found by the caller (by path id).
pub(crate) fn texture_object_png(object: &UnityObject, files: &[(String, BundleFileBytes)]) -> Option<Vec<u8>> {
    read_texture_payload(object, files, None).and_then(|t| png_from_payload(t, object.name())).map(|(_, png)| png)
}

pub(crate) fn png_from_payload(tex: unity_asset_decode::texture::Texture2D, object_name: Option<String>) -> Option<(String, Vec<u8>)> {let _spot=crate::profile::spot("png_from_payload");
    use crate::memosnap::{TEX,active,get,put};
    // Recorded into / replayed from the card (OK_CARD_MEMO=tex): keyed by the texels, format and size.
    if !active(TEX) { return png_from_payload_uncached(tex, object_name); }
    let key = {
        use sha2::Digest;
        let mut h = sha2::Sha256::new();
        h.update(b"payload_v1");
        h.update(format!("{:?}|{}|{}|{}", tex.format, tex.width, tex.height, crate::teximg::raw_enabled()).as_bytes());
        h.update(&tex.image_data);
        h.finalize().to_vec()
    };
    let name = if tex.name.is_empty() { object_name.clone().unwrap_or_else(|| "tex".into()) } else { tex.name.clone() };
    if let Some(bytes) = get(TEX, &key) { return Some((name, (*bytes).clone())); }
    let out = png_from_payload_uncached(tex, object_name)?;
    put(TEX, &key, || Some(out.1.clone()));
    Some(out)
}

fn png_from_payload_uncached(tex: unity_asset_decode::texture::Texture2D, object_name: Option<String>) -> Option<(String, Vec<u8>)> {
    use std::io::Cursor;
    use unity_asset_decode::texture::{TextureFormat,TextureProcessor};
    let proc=TextureProcessor::new(version());
    if tex.image_data.is_empty() {
        return None;
    }
    let w = tex.width as u32;
    let h = tex.height as u32;
    if w < 4 || h < 4 {
        return None;
    }
    // Large BC1/BC3/BC7 maps: decoded in bands of block rows in parallel (blocks are independent; the bytes are the
    // ones the single-call paths below produce: [p>>16, p>>8, p, p>>24] of the decoder's packed texel).
    if w as usize * h as usize >= 512 * 512 && matches!(tex.format, TextureFormat::DXT1 | TextureFormat::DXT5 | TextureFormat::BC7) {
        if let Some(img) = decode_bc_banded(&tex.image_data, w as usize, h as usize, tex.format) {
            let buf = crate::teximg::encode_rgba_owned(img)?;
            let name = if tex.name.is_empty() { object_name.unwrap_or_else(|| "tex".into()) } else { tex.name };
            return Some((name, buf));
        }
    }
    let img = if tex.format == TextureFormat::DXT1 {
        // Unity's DXT1 is DXGI BC1 RGBA: endpoint0 <= endpoint1 reserves
        // selector 3 for transparent black. The generic decoder uses BC1 RGB
        // and silently turns cutouts into opaque black (including iris masks).
        // Keep the decoder's BGRA packing here; the shared S3TC correction
        // below changes it to RGBA exactly once.
        decode_unity_dxt1(&tex.image_data, w, h)?
    } else if tex.format == TextureFormat::BC7 {
        // unity-asset-decode's validate() rejects BC7; decode it directly.
        let mut output = vec![0u32; (w as usize) * (h as usize)];
        texture2ddecoder::decode_bc7(&tex.image_data, w as usize, h as usize, &mut output).ok()?;
        let rgba: Vec<u8> = output
            .iter()
            .flat_map(|&p| {
                [
                    ((p >> 16) & 0xFF) as u8,
                    ((p >> 8) & 0xFF) as u8,
                    (p & 0xFF) as u8,
                    ((p >> 24) & 0xFF) as u8,
                ]
            })
            .collect();
        image::RgbaImage::from_raw(w, h, rgba)?
    } else {
        match proc.decode_texture(&tex) {
            Ok(image)=>image,
            Err(error)=>{if std::env::var_os("OK_TEXDIAG").is_some(){eprintln!("Texture decode name={} dimensions={}x{} format={:?} bytes={} error={error}",tex.name,w,h,tex.format,tex.image_data.len());}return None;}
        }
    };
    // texture2ddecoder packs `color(r,g,b,a)` as little-endian [b,g,r,a]; both it
    // and unity-asset-decode's S3TC/BC wrappers then read back byte 0 as "R", so
    // red and blue are swapped for DXT1/DXT5 (BC7 fixed above).
    let img = if matches!(tex.format, TextureFormat::DXT1 | TextureFormat::DXT5) {
        let mut im = img;
        for p in im.pixels_mut() {
            p.0.swap(0, 2);
        }
        im
    } else {
        img
    };
    let buf = crate::teximg::encode_rgba_owned(img)?;
    let name = if tex.name.is_empty() {
        object_name.unwrap_or_else(|| "tex".into())
    } else {
        tex.name
    };
    Some((name, buf))
}

fn keep_base_tex(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.contains("cf_body")
        || n.contains("cf_face")
        || n.contains("cf_head")
        || n.contains("cf_t_")
        || n.contains("nip")
        || n.contains("underhair")
        || n.contains("hitomi")
        || n.contains("eyeline")
        || n.contains("mayuge")
        || n.contains("noseline")
        || n.contains("sirome")
        || n.contains("tooth")
        || n.contains("tang")
        || n.contains("face")
        || n.contains("head")
        || n.contains("khohoaka")
        || n.contains("hoho")
}

fn textures_from_serialized(
    serialized: &unity_asset_binary::asset::SerializedFile,
    files: &[(String, Vec<u8>)],
    filter: bool,
) -> Vec<(String, Vec<u8>)> {
    const TEXTURE_2D: i32 = 28;
    let mut out = Vec::new();
    for info in serialized.objects_of_type(TEXTURE_2D) {
        let Ok(object) = UnityObject::from_serialized_file(serialized, info) else {
            continue;
        };
        if filter {
            let n = object.name().unwrap_or_default();
            if !keep_base_tex(&n) {
                continue;
            }
        }
        if let Some(t) = png_from_texture(&object, files, None) {
            out.push(t);
        }
    }
    out
}

/// Rank a catalog texture name against a list key (`eri`, `cw_t_mayuge_011_vl`).
/// Exact / `_low` beat substring hits; `kage` / thumb / mole copies lose.
fn tex_name_rank(have: &str, want: &str) -> i32 {
    let h = have.to_ascii_lowercase();
    let w = want.to_ascii_lowercase();
    if w.is_empty() || h.contains("thumb") {
        return 0;
    }
    let kage = (h.contains("kage") && !h.contains("eyeshadow")) || h.starts_with("mole_");
    if h == w {
        return 1000;
    }
    if h == format!("{w}_low") {
        return 900;
    }
    if !kage && (h.starts_with(&w) || w.starts_with(&h)) {
        return 400;
    }
    if !kage && h.contains(&w) {
        return 100;
    }
    if kage && (h == format!("{w}_kage") || h == format!("{w}_kage_low") || h.contains(&w)) {
        return 10;
    }
    0
}

pub fn pick_named_png(textures: &[(String, Vec<u8>)], names: &[&str]) -> Option<(String, Vec<u8>)> {
    let mut best: Option<(i32, usize, String, Vec<u8>)> = None;
    for want in names {
        for (n, b) in textures {
            let r = tex_name_rank(n, want);
            if r <= 0 {
                continue;
            }
            let key = (r, b.len(), n.clone(), b.clone());
            let better = match &best {
                None => true,
                Some(cur) => key.0 > cur.0 || (key.0 == cur.0 && key.1 > cur.1),
            };
            if better {
                best = Some(key);
            }
        }
        if let Some((_, _, n, b)) = best {
            return Some((n, b));
        }
    }
    None
}

pub fn named_png_from_bytes(bundle_bytes: impl Into<SourceBytes>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let bundle_bytes = bundle_bytes.into();
    let parsed = parse_bundle_ref(&bundle_bytes);
    named_png_cached_fallback(&parsed,names,||Some(bundle_bytes.to_vec()))
}
fn named_png_cached_fallback(parsed:&ParsedBundle,names:&[&str],source:impl FnOnce()->Option<Vec<u8>>)->Option<(String,Vec<u8>)>{
    let key=names.iter().map(|s|s.to_ascii_lowercase()).collect::<Vec<_>>();
    if parsed.missing_texture_queries.borrow().contains(&key){return None;}
    let result=named_png_from_parsed(parsed,names);
    // A complete metadata directory proves an absent name. Re-reading and
    // eagerly decompressing the same bundle cannot discover another object.
    // Decode failures and incomplete/legacy directories keep the old fallback.
    let dir=parsed.texture_directory();
    let absent=dir.complete&&!dir.catalog.iter().any(|(have,_)|names.iter().any(|want|tex_name_rank(have,want)>0));
    let result=result.or_else(||if absent{None}else{named_png_from_bytes_legacy(source()?,names)});
    if result.is_none(){let mut missing=parsed.missing_texture_queries.borrow_mut();if missing.len()>=128{missing.pop_front();}missing.push_back(key);}
    result
}
fn named_png_from_parsed(parsed:&ParsedBundle,names:&[&str])->Option<(String,Vec<u8>)> {
    named_png_in_directory(&parsed.texture_directory(),names,&|n|parsed.texture_png_named(n))
}
/// AssetBundle name lookup over a texture directory: an exact unique match,
/// else the best ranked catalog name (rank, then pixel area) that decodes.
pub(crate) fn named_png_in_directory(dir:&TexDirectory,names:&[&str],decode:&dyn Fn(&str)->Option<Vec<u8>>)->Option<(String,Vec<u8>)> {
    for want in names {
        let key=want.to_ascii_lowercase();
        if dir.exact.contains_key(&key) {
            let exact=dir.names.get(&key).filter(|v|!v.1&&tex_name_rank(&v.0,want)==1000).map(|v|v.0.clone())
                .or_else(||dir.assets.get(&key).filter(|n|tex_name_rank(n,want)==1000).cloned());
            if let Some(exact)=exact {if let Some(png)=decode(&exact){return Some((exact,png));}}
        }
        let mut ranked:Vec<(i32,u32,String)>=dir.catalog.iter().filter_map(|(n,px)|{let r=tex_name_rank(n,want);(r>0).then_some((r,*px,n.clone()))}).collect();
        ranked.sort_by(|a,b|b.0.cmp(&a.0).then(b.1.cmp(&a.1)));
        for (_,_,n) in ranked {if let Some(png)=decode(&n){return Some((n,png));}}
    }
    None
}

/// AssetBundle.LoadAsset<Texture2D> uses an exact asset name. Catalog fallback
/// candidates must not be replaced by prefix/substring matches.
pub fn exact_png_from_bytes(bundle_bytes: Vec<u8>, name: &str) -> Option<Vec<u8>> {
    exact_png_from_ref(&bundle_bytes,name)
}

/// Candidate-name probes must not clone the entire (often >100 MiB)
/// source bundle. ParsedBundle owns its data on a cache miss.
pub(crate) fn exact_png_from_ref(bundle_bytes: &[u8], name: &str) -> Option<Vec<u8>> {
    let parsed = parse_bundle_ref(bundle_bytes);
    parsed.exact_texture_png(name)
}

/// Unity maker thumbnails are usually Sprite rectangles inside Texture2D atlases.
pub fn sprite_png_from_bytes(bytes: Vec<u8>, name: &str) -> Option<Vec<u8>> {
 crate::oklib::memo_bytes(&bytes.clone(), "sprite_png", name, || sprite_png_from_bytes_unity(bytes, name))
}
fn sprite_png_from_bytes_unity(bytes: Vec<u8>, name: &str) -> Option<Vec<u8>> {
 let parsed=parse_bundle_cached(bytes);
 parsed.with_serialized(|files| {for file in files {for info in file.objects_of_type(213) {
  let Ok(sprite)=UnityObject::from_serialized_file(file,info) else {continue;};
  if !sprite.name().unwrap_or_default().eq_ignore_ascii_case(name){continue;}
  let props=sprite.class.properties();let rd=props.get("m_RD")?;
  let texture=uv_obj(rd,"texture").and_then(uv_pptr)?;
  let rect=uv_obj(rd,"textureRect")?;
  let x=uv_obj(rect,"x").map(uv_f32)? as u32;let y=uv_obj(rect,"y").map(uv_f32)? as u32;
  let w=uv_obj(rect,"width").map(uv_f32)? as u32;let h=uv_obj(rect,"height").map(uv_f32)? as u32;
  let texture_info=file.objects_of_type(28).into_iter().find(|i|i.path_id==texture)?;
  let obj=UnityObject::from_serialized_file(file,texture_info).ok()?;
  let (_,png)=png_from_texture(&obj,&parsed.files,None)?;
  let image=crate::teximg::load_from_memory(&png).ok()?.to_rgba8();
  if w==0||h==0||x+w>image.width()||y+h>image.height(){return None;}
  let mut cropped=image::imageops::crop_imm(&image,x,y,w,h).to_image();
  let settings=uv_obj(rd,"settingsRaw").map(uv_i32).unwrap_or(0);
  // SpritePackingRotation occupies bits 2..5 of SpriteSettings.
  cropped=match (settings>>2)&15 {1=>image::imageops::flip_horizontal(&cropped),2=>image::imageops::flip_vertical(&cropped),3=>image::imageops::rotate180(&cropped),4=>image::imageops::rotate270(&cropped),_=>cropped};
  cropped=image::imageops::flip_vertical(&cropped);
  let mut out=std::io::Cursor::new(Vec::new());image::DynamicImage::ImageRgba8(cropped).write_to(&mut out,image::ImageFormat::Png).ok()?;return Some(out.into_inner());
 }}None})
}

fn named_png_from_bytes_legacy(bundle_bytes: Vec<u8>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    named_png_from_bytes_legacy_unity(bundle_bytes,names)
}
/// Every Texture2D of a bundle in object order with its texels as the eager
/// (legacy) loader reads them: the data the legacy name lookup ranks over.
pub(crate) fn legacy_texture_payloads(bytes: &[u8]) -> Vec<(String, Option<unity_asset_decode::texture::Texture2D>)> {
    crate::oklib::memo_bytes(bytes, "legacy_texture_payloads", &(), || {
        let mut out = Vec::new();
        each_legacy_texture(bytes, |name, payload| out.push((name, payload())));
        out
    })
}
/// Each (name, texels) of a stored `legacy_texture_payloads` result in order, deserialized one at a time.
fn each_stored_texture_payload(raw: &[u8], mut each: impl FnMut(String, Option<unity_asset_decode::texture::Texture2D>)) -> Option<()> {
    use serde::de::{Deserializer, SeqAccess, Visitor};
    struct List<'f, F>(&'f mut F);
    impl<'de, F: FnMut(String, Option<unity_asset_decode::texture::Texture2D>)> Visitor<'de> for List<'_, F> {
        type Value = ();
        fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result { f.write_str("a texture payload list") }
        fn visit_seq<S: SeqAccess<'de>>(self, mut seq: S) -> Result<(), S::Error> {
            while let Some((name, payload)) = seq.next_element::<(String, Option<unity_asset_decode::texture::Texture2D>)>()? { (self.0)(name, payload); }
            Ok(())
        }
    }
    use bincode::Options;
    // The options of `bincode::deserialize`, which stored the list.
    let mut de = bincode::Deserializer::from_slice(raw, bincode::DefaultOptions::new().with_fixint_encoding().allow_trailing_bytes());
    with_texture_plans(|| de.deserialize_seq(List(&mut each))).ok()
}
/// The eager loader's Texture2D objects of a bundle in order, each with its texels read when asked for. The
/// bundle's files are read through the loaded bundle on demand, not copied out whole.
fn each_legacy_texture(bytes: &[u8], mut each: impl FnMut(String, &dyn Fn() -> Option<unity_asset_decode::texture::Texture2D>)) {
    // UnityFS: loaded lazily; `check_blocks` fails where decompressing the whole bundle at once would, without
    // keeping the decompressed data (hundreds of MB). Its files are the directory nodes, as the eager loader's.
    let source = bytes.to_source();
    let Ok(lazy) = load_bundle_source(source.clone(), false) else { return };
    let (bundle, files) = if lazy.header.is_unity_fs() {
        if lazy.check_blocks().is_err() { return; }
        let files: Vec<unity_asset_binary::bundle::BundleFileInfo> = lazy.nodes.iter()
            .map(|n| unity_asset_binary::bundle::BundleFileInfo::new(n.name.clone(), n.offset, n.size)).collect();
        (Arc::new(lazy), files)
    } else {
        drop(lazy);
        let Ok(bundle) = load_bundle_source(source, true) else { return };
        let _ = bundle.data_checked();
        let files = bundle.files.clone();
        (Arc::new(bundle), files)
    };
    // A file that cannot be read answers no stream read, as when it was left out of the eagerly extracted list.
    let files: Vec<(String, BundleFileBytes)> = files.into_iter()
        .map(|f| (f.name.clone(), BundleFileBytes { bundle: bundle.clone(), file: f })).collect();
    for (_n, data) in &files {
        let Ok(serialized) = parse_shared_serialized(data.clone()) else { continue };
        for info in serialized.objects_of_type(28) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let name = object.name().unwrap_or_default();
            each(name, &|| read_texture_payload(&object, &files, Some(bytes)));
        }
    }
}
#[allow(dead_code)]
fn named_png_from_bytes_legacy_unity(bundle_bytes: Vec<u8>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let wants: Vec<String> = names.iter().map(|s| s.to_ascii_lowercase()).collect();
    let mut best: Option<(i32, String, Vec<u8>)> = None;
    let mut consider = |name: String, payload: &dyn Fn() -> Option<unity_asset_decode::texture::Texture2D>| {
        let score = wants.iter().map(|w| tex_name_rank(&name.to_ascii_lowercase(), w)).max().unwrap_or(0);
        if score == 0 || !best.as_ref().map(|b| score > b.0).unwrap_or(true) { return; }
        if let Some((n, png)) = payload().and_then(|p| png_from_payload(p, Some(name.clone()))) { best = Some((score, n, png)); }
    };
    // Recording an OK base library keeps every payload (the memo); otherwise only candidates are decoded.
    if crate::oklib::active().is_some() {
        // Replaying: the stored list is read one texture at a time (a large pack's list was hundreds of MB once
        // decompressed, and as much again deserialized), with the same ranking in the same order.
        let streamed = crate::oklib::memo_bytes_visit(&bundle_bytes, "legacy_texture_payloads", &(),
            |raw| each_stored_texture_payload(raw, |name, payload| consider(name, &|| payload.clone())));
        if streamed.is_none() {
            for (name, payload) in legacy_texture_payloads(&bundle_bytes) { consider(name, &|| payload.clone()); }
        }
    } else {
        each_legacy_texture(&bundle_bytes, consider);
    }
    best.map(|(_, n, b)| (n, b))
}
#[allow(dead_code)]
fn named_png_from_bytes_legacy_eager(bundle_bytes: Vec<u8>, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let raw = bundle_bytes.clone();
    let Ok(bundle) = load_bundle_unbounded(bundle_bytes) else {
        return None;
    };
    let _ = bundle.data_checked();
    let mut files = Vec::new();
    for file in &bundle.files {
        let Ok(bytes) = bundle.extract_file_data(file) else {
            continue;
        };
        files.push((file.name.clone(), bytes));
    }
    let wants: Vec<String> = names.iter().map(|s| s.to_ascii_lowercase()).collect();
    let mut best: Option<(i32, String, Vec<u8>)> = None;
    for (_n, bytes) in &files {
        let Ok(serialized) = parse_serialized_file(bytes.clone()) else {
            continue;
        };
        for info in serialized.objects_of_type(28) {
            let Ok(object) = UnityObject::from_serialized_file(&serialized, info) else {
                continue;
            };
            let nm = object.name().unwrap_or_default();
            let ln = nm.to_ascii_lowercase();
            let base_score = wants.iter().map(|w| tex_name_rank(&ln, w)).max().unwrap_or(0);
            if base_score == 0 {
                continue;
            }
            let score = base_score;
            if best.as_ref().map(|b| score > b.0).unwrap_or(true) {
                if let Some(t) = png_from_texture(&object, &files, Some(&raw)) {
                    best = Some((score, t.0, t.1));
                }
            }
        }
    }
    best.map(|(_, n, b)| (n, b))
}

pub fn named_png_from_bundle(path: &Path, names: &[&str]) -> Option<(String, Vec<u8>)> {
    let parsed=parse_bundle_path_cached(path)?;
    named_png_cached_fallback(&parsed,names,||crate::gamefs::read(path).ok())
}

pub(crate) fn named_png_from_zip(path:&Path,asset:&str,names:&[&str])->Option<(String,Vec<u8>)>{
    let parsed=parse_bundle_zip_cached(path,asset)?;
    named_png_cached_fallback(&parsed,names,||crate::mods::zipmod_bundle_named(path,asset).map(|b|b.to_vec()))
}

/// Preserve the serialized Texture2D import colour space when a face texture
/// is baked through Unity's linear-source -> sRGB RenderTexture Blit.
pub fn named_png_with_color_space_from_bytes(
    bundle_bytes: Vec<u8>, names: &[&str],
) -> Option<(String, Vec<u8>, Option<i32>)> {
    let parsed=parse_bundle_ref(&bundle_bytes);
    let (name, png) = named_png_cached_fallback(&parsed,names,||Some(bundle_bytes))?;
    let color_space = parsed.texture_color_space(&name);
    Some((name, png, color_space))
}

pub fn named_png_with_color_space_from_bundle(
    path: &Path, names: &[&str],
) -> Option<(String, Vec<u8>, Option<i32>)> {
    let parsed=parse_bundle_path_cached(path)?;
    let(name,png)=named_png_cached_fallback(&parsed,names,||crate::gamefs::read(path).ok())?;
    let color_space=parsed.texture_color_space(&name);Some((name,png,color_space))
}

pub(crate) fn named_png_with_color_space_from_zip(path:&Path,asset:&str,names:&[&str])
    ->Option<(String,Vec<u8>,Option<i32>)> {
    let parsed=parse_bundle_zip_cached(path,asset)?;
    let(name,png)=named_png_cached_fallback(&parsed,names,||crate::mods::zipmod_bundle_named(path,asset).map(|b|b.to_vec()))?;
    let color_space=parsed.texture_color_space(&name);Some((name,png,color_space))
}

fn score_texture(mesh_name: &str, hint: &str, tex_name: &str, bytes_len: usize) -> i64 {
    let n = mesh_name.to_ascii_lowercase();
    let h = hint.to_ascii_lowercase();
    let t = tex_name.to_ascii_lowercase();
    let mut s = 0i64;
    if !h.is_empty() && (t == h || t.contains(&h) || h.contains(&t)) {
        s += 80_000_000;
    }
    if t.contains(&n) || n.contains(&t) {
        s += 50_000_000;
    }
    if n.contains("body") && (t.contains("body") || t.contains("skin") || t.contains("cf_m_body")) {
        s += 40_000_000;
    }
    if (n.contains("face") || n.contains("head"))
        && (t.contains("face") || t.contains("head") || t.contains("cf_m_face") || t.contains("cf_o_face"))
    {
        s += 40_000_000;
    }
    if n.contains("hair") && t.contains("hair") {
        s += 40_000_000;
    }
    if t.ends_with("_t") || t.contains("_t_") || t.contains("main") {
        s += 2_000_000;
    }
    if is_aux_tex(tex_name) {
        s -= 40_000_000;
    }
    s + bytes_len.min(5_000_000) as i64
}

/// ColorMask / DetailMask / normal / thumbs are not albedo.
fn is_aux_tex(name: &str) -> bool {
    let t = name.to_ascii_lowercase();
    t.contains("thumb")
        || t.contains("colormask")
        || t.contains("_mc")
        || t.contains("_md")
        || t.ends_with("_n")
        || t.contains("_n_")
        || t.contains("normal")
}

fn pick_texture_for_mesh<'a>(
    name: &str,
    hint: &str,
    textures: &'a [(String, Vec<u8>)],
) -> Option<&'a (String, Vec<u8>)> {
    if textures.is_empty() {
        return None;
    }
    if textures.len() == 1 {
        return textures.first();
    }
    let (best, score) = textures
        .iter()
        .map(|t| (t, score_texture(name, hint, &t.0, t.1.len())))
        .max_by_key(|(_, s)| *s)?;
    // Require a name match. Byte-size-only scores used to glue a random 2MB atlas
    // onto every mesh (pupils, teeth, tongue).
    let n = name.to_ascii_lowercase();
    let h = hint.to_ascii_lowercase();
    if score >= 10_000_000 {
        Some(best)
    } else if n.contains("hair")
        || n.contains("kami")
        || h.contains("hair")
        || h.contains("kami")
    {
        textures
            .iter()
            .filter(|t| t.1.len() > 2048 && !t.0.to_ascii_lowercase().contains("thumb"))
            .max_by_key(|t| t.1.len())
            .or(Some(best))
    } else if textures.len() == 1 {
        Some(best)
    } else {
        None
    }
}

fn attach_textures(meshes: &mut [ExtractedMesh], textures: &[(String, Vec<u8>)], hint: &str) {
    if textures.is_empty() {
        return;
    }
    for m in meshes {
        if m.texture_png.is_some() {
            continue;
        }
        if let Some((tn, bytes)) = pick_texture_for_mesh(&m.name, hint, textures) {
            m.texture_name = tn.clone();
            m.texture_png = Some(bytes.clone());
        }
    }
}

fn pptr_path_id(v: &unity_asset_core::UnityValue) -> Option<i64> {
    match v {
        unity_asset_core::UnityValue::Object(o) => o
            .get("m_PathID")
            .or_else(|| o.get("m_PathId"))
            .and_then(|x| x.as_i64()),
        unity_asset_core::UnityValue::Integer(i) => Some(*i),
        _ => None,
    }
}

/// The materials of one serialized file (path id -> material), with the texture names and wrap modes it adds to
/// `tex_id_name`/`tex_id_wrap` (shaders resolve inside the file).
/// Where material textures outside the bundle's own files live: `own` is the external path of the file being read
/// (None for the bundle itself), `external_name` names a texture of another file, `source` collects texture name
/// (lower case) -> external path for every texture a material takes from outside the bundle.
pub(crate) struct TextureSources<'a> {
    pub own: Option<&'a str>,
    pub external_name: &'a mut dyn FnMut(&str, i64) -> Option<String>,
    pub source: &'a mut HashMap<String, String>,
}

fn materials_in(ser: &unity_asset_binary::asset::SerializedFile, tex_id_name: &mut HashMap<i64, String>, tex_id_wrap: &mut HashMap<i64, i32>, sources: &mut TextureSources) -> HashMap<i64, MeshMaterial> {
    let mut shader_id_name: HashMap<i64, String> = HashMap::new();
    let mut shader_id_rim: HashMap<i64, bool> = HashMap::new();
    let mut mat_info: HashMap<i64, MeshMaterial> = HashMap::new();
    shader_id_name.clear();
    shader_id_rim.clear();
    for info in ser.objects_of_type(28) {
        if let Ok(object) = UnityObject::from_serialized_file(ser, info) {
            let n = object.name().unwrap_or_default();
            if !n.is_empty() {
                tex_id_name.insert(object.path_id(), n);
            }
            if let Some(settings) = object.class.properties().get("m_TextureSettings") {
                let wrap = uv_obj(settings,"m_WrapU").or_else(||uv_obj(settings,"m_WrapMode"))
                    .and_then(|v|v.as_i64()).unwrap_or(0) as i32;
                tex_id_wrap.insert(object.path_id(),wrap);
            }
        }
    }
    for info in ser.objects_of_type(48) {
        if let Ok(object) = UnityObject::from_serialized_file(ser, info) {
            let n = shader_runtime_name(&object);
            if !n.is_empty() {
                shader_id_name.insert(object.path_id(), n);
            }
            if let Some(has_rim) = shader_declares_property(&object, "_rimV") {
                shader_id_rim.insert(object.path_id(), has_rim);
            }
        }
    }
    for info in ser.objects_of_type(21) {
        let Ok(object) = UnityObject::from_serialized_file(ser, info) else {
            continue;
        };
        let props = object.class.properties();
        let mut m = MeshMaterial {
            name: object.name().unwrap_or_default(),
            shader: props
                .get("m_Shader")
                .and_then(local_pptr_path_id)
                .and_then(|id| shader_id_name.get(&id).cloned())
                .unwrap_or_default(),
            shader_has_rim: props.get("m_Shader").and_then(local_pptr_path_id)
                .and_then(|id| shader_id_rim.get(&id).copied()),
            render_queue: props
                .get("m_CustomRenderQueue")
                .and_then(|v| v.as_i64())
                .unwrap_or(-1) as i32,
            keywords: props
                .get("m_ShaderKeywords")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .split_whitespace()
                .map(|s| s.to_string())
                .collect(),
            colors: HashMap::new(),
            floats: HashMap::new(),
            tex_slots: Vec::new(),
            tex_wraps: HashMap::new(),
        };
        if let Some(unity_asset_core::UnityValue::Object(sp)) =
            props.get("m_SavedProperties")
        {
            if let Some(unity_asset_core::UnityValue::Array(envs)) = sp.get("m_TexEnvs")
            {
                for e in envs {
                    let (key, val) = match e {
                        unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                            let k = pair[0].as_str().unwrap_or("").to_string();
                            (k, pair[1].clone())
                        }
                        unity_asset_core::UnityValue::Object(eo) => (
                            eo.get("first")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string(),
                            eo.get("second")
                                .cloned()
                                .unwrap_or(unity_asset_core::UnityValue::Null),
                        ),
                        _ => continue,
                    };
                    store_tex_env_transform(&mut m, &key, &val);
                    let tex_ref = val
                        .as_object()
                        .and_then(|so| so.get("m_Texture"))
                        .and_then(references::PPtr::read);
                    // A texture of another serialized file (FileID > 0): named and decoded from that file.
                    if let Some(p) = tex_ref.filter(|p| p.file > 0) {
                        let Some(ext) = ser.externals.get(p.file as usize - 1).map(|e| e.path.clone()) else { continue; };
                        let Some(name) = (sources.external_name)(&ext, p.path) else { continue; };
                        sources.source.insert(name.to_ascii_lowercase(), ext);
                        m.tex_slots.push((key, name));
                        continue;
                    }
                    let tex_id = tex_ref.map(|p| p.path);
                    if let Some(wrap) = tex_id.and_then(|id|tex_id_wrap.get(&id)) {
                        m.tex_wraps.insert(key.clone(),*wrap);
                    }
                    let Some(name) = tex_id.and_then(|id| tex_id_name.get(&id).cloned())
                    else {
                        continue;
                    };
                    if let Some(own) = sources.own { sources.source.insert(name.to_ascii_lowercase(), own.to_owned()); }
                    m.tex_slots.push((key, name));
                }
            }
            if let Some(unity_asset_core::UnityValue::Array(cols)) = sp.get("m_Colors") {
                for e in cols {
                    let (key, val) = match e {
                        unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                            let k = pair[0].as_str().unwrap_or("").to_string();
                            (k, pair[1].clone())
                        }
                        unity_asset_core::UnityValue::Object(eo) => (
                            eo.get("first")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string(),
                            eo.get("second")
                                .cloned()
                                .unwrap_or(unity_asset_core::UnityValue::Null),
                        ),
                        _ => continue,
                    };
                    if let Some(c) = val.as_object() {
                        let col = [
                            c.get("r").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32,
                            c.get("g").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32,
                            c.get("b").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32,
                            c.get("a").and_then(|v| v.as_f64()).unwrap_or(1.0) as f32,
                        ];
                        m.colors.insert(key, col);
                    }
                }
            }
            if let Some(unity_asset_core::UnityValue::Array(fs)) = sp.get("m_Floats") {
                for e in fs {
                    let (key, val) = match e {
                        unity_asset_core::UnityValue::Array(pair) if pair.len() == 2 => {
                            let k = pair[0].as_str().unwrap_or("").to_string();
                            (k, pair[1].clone())
                        }
                        unity_asset_core::UnityValue::Object(eo) => (
                            eo.get("first")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string(),
                            eo.get("second")
                                .cloned()
                                .unwrap_or(unity_asset_core::UnityValue::Null),
                        ),
                        _ => continue,
                    };
                    let f = val.as_f64().or_else(|| {
                        val.as_object()
                            .and_then(|o| o.get("x"))
                            .and_then(|v| v.as_f64())
                    });
                    if let Some(f) = f {
                        m.floats.insert(key, f as f32);
                    }
                }
            }
        }
        mat_info.insert(object.path_id(), m);
    }
    mat_info
}

fn local_pptr_path_id(v: &unity_asset_core::UnityValue) -> Option<i64> {
    if uv_obj(v, "m_FileID").and_then(|id| id.as_i64()).unwrap_or(0) != 0 {
        return None;
    }
    pptr_path_id(v)
}

fn version() -> UnityVersion {
    UnityVersion::parse_version("2019.4.9f1").unwrap_or_default()
}

fn mesh_stream_bytes<B: StreamBytes>(
    object: &UnityObject,
    files: &[(String, B)],
) -> Option<Vec<u8>> {
    use unity_asset_core::UnityValue;
    let props = object.class.properties();
    let UnityValue::Object(stream) = props.get("m_StreamData")? else {
        return None;
    };
    let path = stream
        .get("path")
        .and_then(|v| v.as_str())
        .unwrap_or_default();
    let offset = stream.get("offset").and_then(|v| v.as_i64()).unwrap_or(0) as u64;
    let size = stream.get("size").and_then(|v| v.as_i64()).unwrap_or(0) as u32;
    load_stream(files, path, offset, size)
}

pub fn unpack_illusion_albedo(png: &[u8]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let mut rsum = 0u64;
    let mut gsum = 0u64;
    let mut bsum = 0u64;
    let mut n = 0u64;
    for p in img.pixels() {
        if p[0] < 16 && p[1] < 16 && p[2] < 16 {
            continue;
        }
        rsum += p[0] as u64;
        gsum += p[1] as u64;
        bsum += p[2] as u64;
        n += 1;
    }
    if n == 0 {
        return None;
    }
    let r = rsum / n;
    let g = gsum / n;
    let b = bsum / n;
    // Packed Illusion albedo: luma in R, G/B near 0. Mean R can be dark.
    if r < g.saturating_mul(2) || r < b.saturating_mul(2) || g > 40 && b > 40 {
        return None;
    }
    let mut minv = 255u8;
    let mut maxv = 0u8;
    for p in img.pixels() {
        minv = minv.min(p[0]);
        maxv = maxv.max(p[0]);
    }
    if maxv.saturating_sub(minv) < 14 {
        return None;
    }
    let scale = (200.0 / (r as f32).max(1.0)).clamp(1.0, 16.0);
    let mut out = img;
    for p in out.pixels_mut() {
        let lift = |x: u8| -> u8 { ((x as f32) * scale).clamp(0.0, 255.0) as u8 };
        if p[1] > p[0] && p[1] > p[2] {
            let v = lift(p[1]);
            p[0] = v;
            p[1] = v;
            p[2] = v / 2;
        } else if p[2] > p[0] && p[2] > p[1] {
            let v = lift(p[2]);
            p[0] = v;
            p[1] = v / 2;
            p[2] = v;
        } else {
            let v = lift(p[0]);
            p[0] = v;
            p[1] = ((v as u16 * 210) / 255) as u8;
            p[2] = ((v as u16 * 200) / 255) as u8;
        }
    }
    crate::teximg::encode_rgba_owned(out)
}

pub fn solid_png(color: [f32; 4], w: u32, h: u32) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let px = image::Rgba([
        (color[0].clamp(0.0, 1.0) * 255.0) as u8,
        (color[1].clamp(0.0, 1.0) * 255.0) as u8,
        (color[2].clamp(0.0, 1.0) * 255.0) as u8,
        (color[3].clamp(0.0, 1.0) * 255.0) as u8,
    ]);
    let img = image::RgbaImage::from_pixel(w.max(1), h.max(1), px);
    crate::teximg::encode_rgba_owned(img)
}

/// Hair shader KKHairDiffuse `GetDiffuse() * MainTex`.
/// `GetDiffuse`: `d = 1 + mask.r*(C-1); d += mask.g*(C2-d); d += mask.b*(C3-d)`.
/// Missing ColorMask samples the shader default ("black") ??GetDiffuse = 1.
pub fn hair_get_diffuse(
    main: &[u8],
    mask: Option<&[u8]>,
    c1: [f32; 4],
    c2: [f32; 4],
    c3: [f32; 4],
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let src = crate::teximg::load_from_memory(main).ok()?.to_rgba8();
    let (w, h) = src.dimensions();
    if w == 0 || h == 0 {
        return None;
    }
    let mask_img = mask.and_then(|m| crate::teximg::load_from_memory(m).ok().map(|i| i.to_rgba8()));
    let (mw, mh) = mask_img
        .as_ref()
        .map(|i| i.dimensions())
        .unwrap_or((1, 1));
    let mut out = src.clone();
    for y in 0..h {
        for x in 0..w {
            let p = src.get_pixel(x, y).0;
            let (mr, mg, mb) = if let Some(mi) = mask_img.as_ref() {
                let mx = x * mw / w.max(1);
                let my = y * mh / h.max(1);
                let m = mi.get_pixel(mx.min(mw - 1), my.min(mh - 1)).0;
                (m[0] as f32 / 255.0, m[1] as f32 / 255.0, m[2] as f32 / 255.0)
            } else {
                (0.0, 0.0, 0.0)
            };
            let mut d = [
                1.0 + mr * (c1[0] - 1.0),
                1.0 + mr * (c1[1] - 1.0),
                1.0 + mr * (c1[2] - 1.0),
            ];
            d[0] += mg * (c2[0] - d[0]);
            d[1] += mg * (c2[1] - d[1]);
            d[2] += mg * (c2[2] - d[2]);
            d[0] += mb * (c3[0] - d[0]);
            d[1] += mb * (c3[1] - d[1]);
            d[2] += mb * (c3[2] - d[2]);
            out.put_pixel(
                x,
                y,
                image::Rgba([
                    ((p[0] as f32) * d[0]).clamp(0.0, 255.0) as u8,
                    ((p[1] as f32) * d[1]).clamp(0.0, 255.0) as u8,
                    ((p[2] as f32) * d[2]).clamp(0.0, 255.0) as u8,
                    p[3],
                ]),
            );
        }
    }
    crate::teximg::encode_rgba_owned(out)
}

/// Fallback when ColorMask is missing: `_Color * MainTex` (ColorMask.r = 1).
pub fn tint_hair_png(png: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    hair_get_diffuse(png, None, color, color, color)
}

pub fn overlay_skin_png(png: &[u8], skin: [f32; 4]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (sr, sg, sb) = (
        skin[0].clamp(0.0, 1.0),
        skin[1].clamp(0.0, 1.0),
        skin[2].clamp(0.0, 1.0),
    );
    // KK body shader is MainTex * skinColor. Multiply keeps nipples / navel / makeup.
    for p in img.pixels_mut() {
        if (p[0] as u16) + (p[1] as u16) + (p[2] as u16) < 18 {
            continue;
        }
        p[0] = ((p[0] as f32) * sr).clamp(0.0, 255.0) as u8;
        p[1] = ((p[1] as f32) * sg).clamp(0.0, 255.0) as u8;
        p[2] = ((p[2] as f32) * sb).clamp(0.0, 255.0) as u8;
    }
    crate::teximg::encode_rgba_owned(img)
}

/// Koikano/create_skinbody fragment, without optional sunburn/paint textures:
/// MainTex * Color * lerp(1, Color2, ColorMask.g * Color2.a)
///         * lerp(1, Color5, ColorMask.b).
/// ColorMask.r is not read by this base/nail branch (DXBC blob 1).
pub fn apply_body_create_colors(
    main: &[u8], mask: &[u8], skin: [f32; 4], sub: [f32; 4], nail: [f32; 4],
) -> Option<Vec<u8>> {
    apply_body_create_colors_sampled(main,mask,skin,sub,nail,false)
}

pub fn apply_body_create_colors_sampled(
    main:&[u8],mask:&[u8],skin:[f32;4],sub:[f32;4],nail:[f32;4],main_srgb:bool,
)->Option<Vec<u8>> {
    use std::io::Cursor;
    let src = crate::teximg::load_from_memory(main).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask).ok()?.to_rgba8();
    let (w, h) = src.dimensions();
    let (mw, mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 {
        return None;
    }
    let mut out = src;
    // Same arithmetic per texel as before, without recomputing the card colours' and the u8 texels' transfer
    // functions for every channel of every texel, and with the rows in parallel (~0.3 s for a 2048 body).
    let linear=|v:f32,c:usize| if c<3 {crate::srgb_channel_to_linear(v)} else {v};
    let (sub_l,nail_l,skin_l):([f32;4],[f32;4],[f32;4])=(std::array::from_fn(|c|linear(sub[c],c)),std::array::from_fn(|c|linear(nail[c],c)),std::array::from_fn(|c|linear(skin[c],c)));
    let base_lut:[f32;256]=std::array::from_fn(|i|{let base=i as f32/255.;if main_srgb {crate::srgb_channel_to_linear(base)} else {base}});
    let raw_lut:[f32;256]=std::array::from_fn(|i|i as f32/255.);
    {
        use rayon::prelude::*;
        let row=w as usize*4;
        out.as_mut().par_chunks_mut(row).enumerate().for_each(|(y,line)|{
            for x in 0..w as usize {
                let m=crate::sample_rgba_clamped(&mask,(x as f32+0.5)/w as f32,(y as f32+0.5)/h as f32);
                let sub_weight=m[1]*sub[3];
                let nail_weight=m[2];
                let p=&mut line[x*4..x*4+4];
                for c in 0..4 {
                    let sub_tint = 1.0 + sub_weight * (sub_l[c] - 1.0);
                    let nail_tint = 1.0 + nail_weight * (nail_l[c] - 1.0);
                    let base=if c<3 {base_lut[p[c] as usize]} else {raw_lut[p[c] as usize]};
                    let value=base * skin_l[c] * sub_tint * nail_tint;
                    let value=if c<3 {crate::linear_channel_to_srgb(value)}else{value};
                    p[c]=(value*255.).clamp(0.,255.).round() as u8;
                }
            }
        });
    }
    crate::teximg::encode_rgba_owned(out)
}

/// `Koikano/main_skin` `_LINETEXON_ON`: multiply `_DetailMask` (black lines on white).
pub fn multiply_line_tex(base: &[u8], line: &[u8]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(line).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    for y in 0..dh {
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let sy = (y as u64 * sh as u64 / dh as u64) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1)).0;
            let d = dst.get_pixel_mut(x, y);
            d[0] = ((d[0] as u16 * p[0] as u16) / 255) as u8;
            d[1] = ((d[1] as u16 * p[1] as u16) / 255) as u8;
            d[2] = ((d[2] as u16 * p[2] as u16) / 255) as u8;
        }
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Display material `_overtex1` / `_overtex2`: lerp(base, over * color, over.a) on the same UV.
pub fn overlay_overtex(base: &[u8], over: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    let (cr, cg, cb, ca) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
        color[3].clamp(0.0, 1.0),
    );
    let mut used = 0u32;
    for y in 0..dh {
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let sy = (y as u64 * sh as u64 / dh as u64) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32) / 255.0 * ca;
            if a < 0.02 {
                continue;
            }
            used += 1;
            let d = dst.get_pixel_mut(x, y);
            let sr = (p[0] as f32) * cr;
            let sg = (p[1] as f32) * cg;
            let sb = (p[2] as f32) * cb;
            d[0] = (d[0] as f32 * (1.0 - a) + sr * a).clamp(0.0, 255.0) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + sg * a).clamp(0.0, 255.0) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + sb * a).clamp(0.0, 255.0) as u8;
        }
    }
    if used < 8 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

pub fn overlay_overtex_mapped(
    base: &[u8],
    over: &[u8],
    color: [f32; 4],
    uv0: &[[f32; 2]],
    uv_over: &[[f32; 2]],
    indices: &[u32],
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    if uv0.len() != uv_over.len() || uv0.is_empty() || indices.len() < 3 {
        return None;
    }
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    let (cr, cg, cb, ca) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
        color[3].clamp(0.0, 1.0),
    );
    let mut used = 0u32;
    for tri in indices.chunks_exact(3) {
        let i0 = tri[0] as usize;
        let i1 = tri[1] as usize;
        let i2 = tri[2] as usize;
        if i0 >= uv0.len() || i1 >= uv0.len() || i2 >= uv0.len() {
            continue;
        }
        let p = |uv: [f32; 2]| -> [f32; 2] {
            [uv[0] * dw as f32, (1.0 - uv[1]) * dh as f32]
        };
        let a = p(uv0[i0]);
        let b = p(uv0[i1]);
        let c = p(uv0[i2]);
        let minx = a[0].min(b[0]).min(c[0]).floor().max(0.0) as i32;
        let miny = a[1].min(b[1]).min(c[1]).floor().max(0.0) as i32;
        let maxx = a[0].max(b[0]).max(c[0]).ceil().min(dw as f32 - 1.0) as i32;
        let maxy = a[1].max(b[1]).max(c[1]).ceil().min(dh as f32 - 1.0) as i32;
        if minx > maxx || miny > maxy {
            continue;
        }
        let den = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if den.abs() < 1e-8 {
            continue;
        }
        let o0 = uv_over[i0];
        let o1 = uv_over[i1];
        let o2 = uv_over[i2];
        for y in miny..=maxy {
            for x in minx..=maxx {
                let px = x as f32 + 0.5;
                let py = y as f32 + 0.5;
                let w1 = ((px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1])) / den;
                let w2 = ((b[0] - a[0]) * (py - a[1]) - (px - a[0]) * (b[1] - a[1])) / den;
                let w0 = 1.0 - w1 - w2;
                if w0 < -0.001 || w1 < -0.001 || w2 < -0.001 {
                    continue;
                }
                let uo = (w0 * o0[0] + w1 * o1[0] + w2 * o2[0]).clamp(0.0, 1.0);
                let vo = (w0 * o0[1] + w1 * o1[1] + w2 * o2[1]).clamp(0.0, 1.0);
                let sx = (uo * sw as f32) as u32;
                let sy = ((1.0 - vo) * sh as f32) as u32;
                let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
                let aov = (p[3] as f32) / 255.0 * ca;
                if aov < 0.02 {
                    continue;
                }
                used += 1;
                let d = dst.get_pixel_mut(x as u32, y as u32);
                d[0] = (d[0] as f32 * (1.0 - aov) + (p[0] as f32) * cr * aov).clamp(0.0, 255.0) as u8;
                d[1] = (d[1] as f32 * (1.0 - aov) + (p[1] as f32) * cg * aov).clamp(0.0, 255.0) as u8;
                d[2] = (d[2] as f32 * (1.0 - aov) + (p[2] as f32) * cb * aov).clamp(0.0, 255.0) as u8;
            }
        }
    }
    eprintln!("ok-extract: overlay_mapped used={used} dst={dw}x{dh} src={sw}x{sh}");
    let _ = std::fs::create_dir_all(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw");
    let _ = dst.save(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw\overlay_mapped.png");
    if used < 8 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Same as `overlay_overtex`, but samples `over` with the per-layer UV layout
/// the game passes to `cf_m_face_create` (`_hokuro` for the mole, `_paint1/2`
/// for face paint): `uv' = (uv - offset) / scale`.
pub fn overlay_overtex_uv(
    base: &[u8],
    over: &[u8],
    color: [f32; 4],
    offset: [f32; 2],
    scale: [f32; 2],
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 || scale[0] == 0.0 || scale[1] == 0.0 {
        return None;
    }
    let (cr, cg, cb) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
    );
    let mut used = 0u32;
    for y in 0..dh {
        for x in 0..dw {
            let u = (x as f32 + 0.5) / dw as f32;
            let v = (y as f32 + 0.5) / dh as f32;
            let su = (u - offset[0]) / scale[0];
            let sv = (v - offset[1]) / scale[1];
            if !(0.0..1.0).contains(&su) || !(0.0..1.0).contains(&sv) {
                continue;
            }
            let sx = (su * sw as f32) as u32;
            let sy = (sv * sh as f32) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32) / 255.0;
            if a < 0.02 {
                continue;
            }
            used += 1;
            let d = dst.get_pixel_mut(x, y);
            d[0] = (d[0] as f32 * (1.0 - a) + (p[0] as f32) * cr * a).clamp(0.0, 255.0) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + (p[1] as f32) * cg * a).clamp(0.0, 255.0) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + (p[2] as f32) * cb * a).clamp(0.0, 255.0) as u8;
        }
    }
    if used < 4 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Composite an Illusion mask texture (shape in alpha/luma) onto `base`,
/// painting a flat tint colour. Used for lips (and other `_Color`-tinted
/// overlays) where the texture RGB is itself a mask.
pub fn blend_mask_tint(base: &[u8], over: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw == 0 || dh == 0 || sw == 0 || sh == 0 {
        return None;
    }
    let (cr, cg, cb) = (
        color[0].clamp(0.0, 1.0),
        color[1].clamp(0.0, 1.0),
        color[2].clamp(0.0, 1.0),
    );
    let ca = color[3].clamp(0.0, 1.0);
    let mut used = 0u32;
    for y in 0..dh {
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let sy = (y as u64 * sh as u64 / dh as u64) as u32;
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32 / 255.0) * ca;
            if a <= 0.0 {
                continue;
            }
            used += 1;
            let d = dst.get_pixel_mut(x, y);
            let sr = (p[0] as f32 / 255.0) * cr * 255.0;
            let sg = (p[1] as f32 / 255.0) * cg * 255.0;
            let sb = (p[2] as f32 / 255.0) * cb * 255.0;
            d[0] = (d[0] as f32 * (1.0 - a) + sr * a).clamp(0.0, 255.0) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + sg * a).clamp(0.0, 255.0) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + sb * a).clamp(0.0, 255.0) as u8;
        }
    }
    if used < 4 {
        return None;
    }
    crate::teximg::encode_rgba_owned(dst)
}

/// Stamp a small overtex (centered areola) onto body UVs. `_nipsize` scales the blit.
pub fn stamp_overtex(
    base: &[u8],
    over: &[u8],
    color: [f32; 4],
    uvs: &[[f32; 2]],
    nipsize: f32,
) -> Option<Vec<u8>> {
    use std::io::Cursor;
    if uvs.is_empty() {
        return None;
    }
    let mut dst = crate::teximg::load_from_memory(base).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    if dw < 8 || dh < 8 || sw < 4 || sh < 4 {
        return None;
    }
    let (cr, cg, cb) = (
        color[0].clamp(0.0, 2.0),
        color[1].clamp(0.0, 2.0),
        color[2].clamp(0.0, 2.0),
    );
    let scale = (0.018 + 0.03 * nipsize.clamp(0.0, 1.0)) * dw as f32;
    for uv in uvs {
        let cx = uv[0].fract().abs() * dw as f32;
        let cy = (1.0 - uv[1].fract().abs()) * dh as f32;
        let x0 = (cx - scale).max(0.0) as u32;
        let y0 = (cy - scale).max(0.0) as u32;
        let x1 = ((cx + scale) as u32).min(dw - 1);
        let y1 = ((cy + scale) as u32).min(dh - 1);
        for y in y0..=y1 {
            for x in x0..=x1 {
                let u = ((x as f32 - (cx - scale)) / (scale * 2.0)).clamp(0.0, 1.0);
                let v = ((y as f32 - (cy - scale)) / (scale * 2.0)).clamp(0.0, 1.0);
                let sx = (u * (sw - 1) as f32) as u32;
                let sy = (v * (sh - 1) as f32) as u32;
                let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1));
            let a = (p[3] as f32) / 255.0 * color[3].clamp(0.0, 1.0);
            if a < 0.02 {
                continue;
            }
                // Illusion overtex is a mask tinted by _Color; its own RGB is
                // often a green mask, so stamp the colour shaped by the mask.
                let d = dst.get_pixel_mut(x, y);
                let sr = (cr * 255.0).clamp(0.0, 255.0);
                let sg = (cg * 255.0).clamp(0.0, 255.0);
                let sb = (cb * 255.0).clamp(0.0, 255.0);
                d[0] = (d[0] as f32 * (1.0 - a) + sr * a).clamp(0.0, 255.0) as u8;
                d[1] = (d[1] as f32 * (1.0 - a) + sg * a).clamp(0.0, 255.0) as u8;
                d[2] = (d[2] as f32 * (1.0 - a) + sb * a).clamp(0.0, 255.0) as u8;
            }
        }
    }
    crate::teximg::encode_rgba_owned(dst)
}

pub fn tint_png(png: &[u8], color: [f32; 4]) -> Option<Vec<u8>> {
    let (cr, cg, cb) = (color[0].clamp(0.0, 2.0), color[1].clamp(0.0, 2.0), color[2].clamp(0.0, 2.0));
    if (cr - 1.0).abs() < 0.02 && (cg - 1.0).abs() < 0.02 && (cb - 1.0).abs() < 0.02 {
        return None;
    }
    let parameters:Vec<u8>=[cr,cg,cb].into_iter().flat_map(|v|v.to_bits().to_le_bytes()).collect();
    crate::png_transform_cache::transform("tint_rgb_v1",png,&parameters,||tint_png_uncached(png,[cr,cg,cb]))
}
fn tint_png_uncached(png:&[u8],color:[f32;3])->Option<Vec<u8>>{
    use std::io::Cursor;
    let [cr,cg,cb]=color;
    let mut img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    for p in img.pixels_mut() {
        p[0] = ((p[0] as f32) * cr).clamp(0.0, 255.0) as u8;
        p[1] = ((p[1] as f32) * cg).clamp(0.0, 255.0) as u8;
        p[2] = ((p[2] as f32) * cb).clamp(0.0, 255.0) as u8;
    }
    crate::teximg::encode_rgba_owned(img)
}

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

fn meshes_and_textures_from_loaded_bundle(
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

fn meshes_from_loaded_bundle(bundle: &unity_asset_binary::bundle::AssetBundle) -> Vec<ExtractedMesh> {
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
    (meshes, skel, textures)
}

fn load_base_bundle_impl(
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
fn skeleton_for_mesh_unity(path: &Path, bytes: Vec<u8>, mesh_name: &str) -> Option<crate::pose::Skeleton> {
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

fn skeleton_from_bundle(
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

fn meshes_from_bundle_bytes(bytes: Vec<u8>) -> Vec<ExtractedMesh> {
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

fn prefab_name_matches(go: &str, main_data: &str) -> bool {
    let g = go.to_ascii_lowercase();
    let m = main_data.to_ascii_lowercase();
    let want = m.trim_start_matches("p_");
    g == m || g == want
}

fn prefab_name_fuzzy(go: &str, main_data: &str) -> bool {
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
fn collect_prefab_mesh_map(
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
struct PrefabGraph {
    asset_roots: HashMap<String, Vec<i64>>,
    go_name: std::collections::HashMap<i64, String>,
    go_comps: std::collections::HashMap<i64, Vec<i64>>,
    tf_go: std::collections::HashMap<i64, i64>,
    tf_children: std::collections::HashMap<i64, Vec<i64>>,
    tf_parent: std::collections::HashMap<i64, i64>,
    smr_go: std::collections::HashMap<i64, i64>,
    smr_mesh: std::collections::HashMap<i64, i64>,
    go_tf: std::collections::HashMap<i64, i64>,
    go_mr: std::collections::HashMap<i64, i64>,
    /// Renderer component path id -> Behaviour.m_Enabled. Missing means enabled.
    /// The game does not draw disabled renderers; loading them stacks unused
    /// hair/clothing variants on top of the worn ones.
    comp_enabled: std::collections::HashMap<i64, bool>,
    /// Mesh path id -> `externals[FileID-1].path` for renderer meshes stored in
    /// another serialized file (a dependency bundle, CAB reference).
    ext_mesh: std::collections::HashMap<i64, String>,
}

/// A renderer that only casts shadows (Renderer.m_CastShadows == ShadowsOnly): Unity never draws its colour. Studio
/// maps use such boxes over rooms (`p_koi_map14_shadow00`); drawn, they hide the room from a camera above it.
fn shadows_only(renderer: &UnityObject) -> bool {
    renderer.class.properties().get("m_CastShadows").and_then(|v| v.as_i64()) == Some(3)
}

fn scan_prefab_graph(serialized: &unity_asset_binary::asset::SerializedFile) -> PrefabGraph {
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

fn query_prefab_graph(
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
fn decode_bc_banded(data: &[u8], w: usize, h: usize, format: unity_asset_decode::texture::TextureFormat) -> Option<image::RgbaImage> {
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
        for (p, o) in texels.iter().zip(out.chunks_exact_mut(4)) {
            o.copy_from_slice(&[((p >> 16) & 0xFF) as u8, ((p >> 8) & 0xFF) as u8, (p & 0xFF) as u8, ((p >> 24) & 0xFF) as u8]);
        }
        true
    });
    if !ok { return None; }
    image::RgbaImage::from_raw(w as u32, h as u32, rgba)
}

fn decode_unity_dxt1(data: &[u8], width: u32, height: u32) -> Option<image::RgbaImage> {
    let mut pixels=vec![0u32;(width as usize).checked_mul(height as usize)?];
    texture2ddecoder::decode_bc1a(data,width as usize,height as usize,&mut pixels).ok()?;
    image::RgbaImage::from_raw(width,height,pixels.into_iter().flat_map(u32::to_le_bytes).collect())
}

#[cfg(test)]
mod tests {
    #[test]
    fn bundle_owned_bytes_keep_allocation_and_shared_views_alive(){
        use unity_asset_binary::{shared_bytes::SharedBytes,data_view::DataView};
        let bytes:Vec<_>=(0..4096).map(|i|(i%251) as u8).collect();
        let expected=bytes.clone();let address=bytes.as_ptr() as usize;
        let shared=SharedBytes::from_vec(bytes);assert_eq!(shared.ptr_usize(),address);
        let view=DataView::from_shared_range(shared.clone(),123..3000).unwrap();
        drop(shared);assert_eq!(view.as_bytes(),&expected[123..3000]);
        let clone=view.clone();drop(view);assert_eq!(clone.as_bytes(),&expected[123..3000]);
        assert_eq!(clone.backing_shared().ptr_usize(),address);
        let arc=Arc::<[u8]>::from(expected.clone());let address=arc.as_ptr() as usize;
        let shared=SharedBytes::from_arc(arc);assert_eq!(shared.ptr_usize(),address);assert_eq!(shared.as_bytes(),expected);
    }
    #[test]
    fn bundle_zip_cache_validates_archive_entry_and_parser_lifetime() {
        use std::io::Write;
        let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let directory=std::env::temp_dir().join(format!("ok-bundle-zip-{}-{stamp}",std::process::id()));
        std::fs::create_dir(&directory).unwrap();let path=directory.join("source.zipmod");
        let write=|marker:u8|{
            let mut zip=zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
            for name in ["abdata/chara/a.unity3d","abdata/chara/b.unity3d"] {
                zip.start_file(name,zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored)).unwrap();
                zip.write_all(&[marker;128]).unwrap();
            }
            zip.finish().unwrap();
        };
        write(1);let first=parse_bundle_zip_cached(&path,"chara/a.unity3d").unwrap();
        assert!(Arc::ptr_eq(&first,&parse_bundle_zip_cached(&directory.join(".").join("source.zipmod"),"CHARA\\A.UNITY3D").unwrap()));
        let other=parse_bundle_zip_cached(&path,"chara/b.unity3d").unwrap();assert!(!Arc::ptr_eq(&first,&other));
        assert!(parse_bundle_zip_cached(&path,"chara/missing.unity3d").is_none());
        let size=crate::gamefs::metadata(&path).unwrap().len();write(2);assert_eq!(crate::gamefs::metadata(&path).unwrap().len(),size);
        std::fs::File::options().write(true).open(&path).unwrap().set_modified(std::time::UNIX_EPOCH+std::time::Duration::from_secs((stamp/1_000_000_000) as u64+10)).unwrap();
        let changed=parse_bundle_zip_cached(&path,"chara/a.unity3d").unwrap();assert!(!Arc::ptr_eq(&first,&changed));
        BUNDLE_CACHE.with(|cache|cache.borrow_mut().clear());
        assert!(!Arc::ptr_eq(&changed,&parse_bundle_zip_cached(&path,"chara/a.unity3d").unwrap()));
        std::fs::remove_file(&path).unwrap();assert!(parse_bundle_zip_cached(&path,"chara/a.unity3d").is_none());
        clear_bundle_cache();assert!(BUNDLE_ZIP_INDEX.with(|index|index.borrow().is_empty()));std::fs::remove_dir(&directory).unwrap();
    }
    #[test]
    fn bundle_path_cache_validates_file_and_parser_lifetime() {
        let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let directory=std::env::temp_dir().join(format!("ok-bundle-path-{}-{stamp}",std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let path=directory.join("source.unity3d");
        // Invalid/empty bundles are sufficient to test ownership and file
        // invalidation without copying installed assets into a temp fixture.
        std::fs::write(&path,b"first").unwrap();
        let first=parse_bundle_path_cached(&path).unwrap();
        assert!(Arc::ptr_eq(&first,&parse_bundle_path_cached(&path).unwrap()));
        let alias=directory.join(".").join("source.unity3d");
        assert!(Arc::ptr_eq(&first,&parse_bundle_path_cached(&alias).unwrap()));
        std::fs::write(&path,b"second version").unwrap();
        let changed=parse_bundle_path_cached(&path).unwrap();
        assert!(!Arc::ptr_eq(&first,&changed));
        assert!(Arc::ptr_eq(&changed,&parse_bundle_path_cached(&path).unwrap()));
        std::fs::write(&path,b"newest version").unwrap();
        std::fs::File::options().write(true).open(&path).unwrap().set_modified(
            std::time::UNIX_EPOCH+std::time::Duration::from_secs((stamp/1_000_000_000) as u64+10)).unwrap();
        let same_size_changed=parse_bundle_path_cached(&path).unwrap();
        assert!(!Arc::ptr_eq(&changed,&same_size_changed),"mtime must invalidate same-size replacements");
        BUNDLE_CACHE.with(|cache|cache.borrow_mut().clear());
        let reloaded=parse_bundle_path_cached(&path).unwrap();
        assert!(!Arc::ptr_eq(&same_size_changed,&reloaded),"path index must not retain evicted parser objects");
        let mut interior=vec![17;2048];std::fs::write(&path,&interior).unwrap();
        let original=parse_bundle_path_cached(&path).unwrap();
        let sampled_key=content_key(&interior);interior[600]=23;
        assert_eq!(sampled_key,content_key(&interior),"fixture must change outside the legacy sampled locations");
        std::fs::write(&path,&interior).unwrap();
        std::fs::File::options().write(true).open(&path).unwrap().set_modified(
            std::time::UNIX_EPOCH+std::time::Duration::from_secs((stamp/1_000_000_000) as u64+20)).unwrap();
        assert!(!Arc::ptr_eq(&original,&parse_bundle_path_cached(&path).unwrap()),"same sampled bytes must not reuse an older path parser");
        std::fs::remove_file(&path).unwrap();
        assert!(parse_bundle_path_cached(&path).is_none(),"deleted source must not be returned from cache");
        clear_bundle_cache();
        assert!(BUNDLE_PATH_INDEX.with(|index|index.borrow().is_empty()));
        std::fs::remove_dir(&directory).unwrap();
    }
    #[test]
    fn texture_index_allows_nested_empty_serialized_reads() {
        let bundle = parse_bundle_uncached(Vec::new().into());
        bundle.with_serialized(|files| {
            assert!(files.is_empty());
            assert!(bundle.decode_tex("missing").is_none());
            assert!(bundle.texture_color_space("missing").is_none());
            assert!(bundle.texture_sampling_named("missing").is_none());
            bundle.ensure_tex_catalog();

        });
        assert!(bundle.tex_objects.borrow().as_ref().unwrap().is_empty());
    }

    #[test]
    #[ignore = "requires installed original Texture2D bundles"]
    fn texture_index_matches_linear_lookup_on_installed_sources() {
        let mut sources=Vec::new();
        for source in ["mt_eyebrow_00", "mt_eyeline_up_00", "mt_eye_white_00"] {
            let path=&*Box::leak(crate::test_support::game_path("/abdata/chara").join(format!("{source}.unity3d")).into_boxed_path());
            sources.push((source.to_string(),crate::gamefs::read(&path).expect("installed original bundle"),Some(path)));
        }
        let zip=crate::mods::all_zipmods(&crate::test_support::game_path("/mods")).into_iter()
            .find(|p|p.file_name().is_some_and(|n|n.to_string_lossy().contains("[moderchan]charamaker support MOD v1.1"))).expect("installed CMS mod");
        sources.push(("CMS_NT_00".into(),crate::mods::zipmod_bundle_named(&zip,"chara/_kero/CMS_NT_00.unity3d").expect("installed CMS bundle").to_vec(),None));
        for (source,bytes,path) in sources {
            // The oracle uses the eager container and full objects, independently
            // of ParsedBundle's directory, range extraction and header index.
            let old=load_bundle_unbounded(bytes.clone()).expect("eager oracle container");
            let old_files:Vec<_>=old.files.iter().map(|f|(f.name.clone(),old.extract_file_data(f).unwrap())).collect();
            let old_serialized:Vec<_>=old_files.iter().filter(|(name,_)|{
                let n=name.to_ascii_lowercase();!n.ends_with(".ress")&&!n.ends_with(".resource")
            }).filter_map(|(_,b)|parse_serialized_file(b.clone()).ok()).collect();
            let bundle=parse_bundle_uncached(bytes.clone().into());
            let path_bundle=path.as_ref().and_then(|p|parse_bundle_path_cached(p)).unwrap_or_else(||parse_bundle_cached(bytes.clone()));
            assert_eq!(bundle.files.len(),old_files.len(),"all directory entries including resource streams");
            for ((name,data),(expected_name,expected)) in bundle.files.iter().zip(&old_files) {
                assert_eq!(name,expected_name);
                let size=expected.len().min(4096) as u32;
                assert_eq!(data.read_range(0,size).as_deref(),Some(&expected[..size as usize]));
                let offset=expected.len().saturating_sub(size as usize) as u64;
                assert_eq!(data.read_range(offset,size).as_deref(),Some(&expected[offset as usize..]));
                assert!(data.read_range(expected.len() as u64,1).is_none());
                assert!(data.read_range(u64::MAX,1).is_none());
                assert_eq!(data.read_range(expected.len() as u64,0),Some(Vec::new()));
                // Exercise actual compressed block boundaries, not just the
                // first/last bytes. Compare against the eager independent file.
                let file=&data.file;let mut start=0u64;
                for block in &data.bundle.blocks {
                    if start>=file.offset&&start<file.offset+file.size {
                        let relative=(start-file.offset).saturating_sub(7) as usize;
                        let length=(expected.len()-relative).min(31);
                        assert_eq!(data.read_range(relative as u64,length as u32).as_deref(),Some(&expected[relative..relative+length]),"cross-block range {source}");
                    }
                    start+=block.uncompressed_size as u64;
                }
            }
            bundle.ensure_tex_catalog();
            assert!(bundle.serialized.borrow().is_none(),"texture catalog must not initialize full geometry files");
            let expected_names:Vec<_>=old_serialized.iter().flat_map(|file|file.objects_of_type(28).into_iter().filter_map(|info|{
                let object=UnityObject::from_serialized_file(file,info).ok()?;
                let name=object.name().unwrap_or_default();if name.is_empty(){return None;}
                let props=object.class.properties();
                let w=props.get("m_Width").and_then(|v|v.as_i64()).unwrap_or(0) as u32;
                let h=props.get("m_Height").and_then(|v|v.as_i64()).unwrap_or(0) as u32;
                Some((name,w.saturating_mul(h.max(1))))
            })).collect();
            assert_eq!(*bundle.tex_names.borrow(),expected_names,"complete catalog order, dimensions and names {source}");
            assert!(bundle.tex_catalog_complete.get(),"installed texture directory must be completely parsed {source}");
            assert!(named_png_cached_fallback(&bundle,&["__missing_texture__"],||panic!("a proven absent name must not re-read the full bundle")).is_none());
            let names:Vec<_>=bundle.tex_names.borrow().iter().take(10).map(|(name,_)|name.clone()).collect();
            assert_eq!(names.len(),10,"need ten real textures in {source}");
            for name in names {
                // Independent old-order scan is the oracle, including duplicate
                // names, Unity's legacy wrap fallback and raw color-space fallback.
                let expected=(|| {
                    let files=&old_serialized;
                    for file in files {for info in file.objects_of_type(28) {
                        let Ok(object)=UnityObject::from_serialized_file(file,info) else {continue;};
                        if !object.name().unwrap_or_default().eq_ignore_ascii_case(&name) {continue;}
                        let props=object.class.properties();
                        let cs=props.get("m_ColorSpace").and_then(|v|v.as_i64()).map(|v|v as i32)
                            .or_else(||parse_tex_unity2019(object.raw_data()).map(|t|t.color_space));
                        let sampling=props.get("m_TextureSettings").map(|settings| {
                            let value=|key:&str|uv_obj(settings,key).and_then(|v|v.as_i64()).map(|v|v as i32);
                            let wrap=value("m_WrapMode").unwrap_or(0);
                            (value("m_FilterMode").unwrap_or(1),value("m_WrapU").unwrap_or(wrap),
                             value("m_WrapV").unwrap_or(wrap),props.get("m_ColorSpace").and_then(|v|v.as_i64()).unwrap_or(1)!=0,
                             props.get("m_MipCount").and_then(|v|v.as_i64()).unwrap_or(1) as i32)
                        });
                        return Some((png_from_texture(&object,&old_files,None).map(|(_,png)|png),cs,sampling));
                    }} None
                })().expect("linear lookup");
                assert!(expected.0.is_some(),"oracle must produce real PNG {source}/{name}");
                if source=="CMS_NT_00" {
                    let(_,png,cs)=named_png_with_color_space_from_zip(&zip,"chara/_kero/CMS_NT_00.unity3d",&[&name]).expect("zip-backed source");
                    assert_eq!(Some(png),expected.0,"zip-backed PNG {name}");assert_eq!(cs,expected.1,"zip-backed color space {name}");
                }
                for query in [name.clone(),name.to_ascii_uppercase()] {
                    assert_eq!(bundle.decode_tex(&query),expected.0,"PNG {source}/{query}");
                    assert_eq!(path_bundle.exact_texture_png(&query),expected.0,"cached file PNG {source}/{query}");
                    assert_eq!(path_bundle.texture_color_space(&query),expected.1,"cold exact color space {source}/{query}");
                    assert_eq!(path_bundle.texture_sampling_named(&query).map(|s|(s.filter,s.wrap_u,s.wrap_v,s.srgb,s.mip_count)),expected.2,"cold exact sampling {source}/{query}");
                    assert!(path_bundle.tex_objects.borrow().is_none(),"exact lookup must not build the complete texture catalog");
                    assert_eq!(bundle.texture_color_space(&query),expected.1,"color space {source}/{query}");
                    assert_eq!(bundle.texture_sampling_named(&query).map(|s|(s.filter,s.wrap_u,s.wrap_v,s.srgb,s.mip_count)),expected.2,"sampling {source}/{query}");
                }
            }
            assert!(bundle.serialized.borrow().is_none(),"exact texture decode must not initialize full geometry files");
            for (name,location) in bundle.tex_objects.borrow().as_ref().unwrap(){
                let actual=path_bundle.exact_texture_location(name).expect("every indexed original texture must resolve");
                assert_eq!((actual.file,actual.path_id,actual.color_space),(location.file,location.path_id,location.color_space),"complete original texture identity oracle {source}/{name}");
            }
            let first=&expected_names[0].0;
            for queries in [vec![first.clone()],vec![first.to_ascii_uppercase()],vec![String::new()],
                vec!["thumb".into()],vec!["__missing_texture__".into()],vec!["__missing_texture__".into(),first.clone()],
                vec![first.trim_end_matches(|c:char|c.is_ascii_digit()).to_string()]] {
                let refs:Vec<_>=queries.iter().map(String::as_str).collect();
                let legacy=named_png_from_bytes_legacy(bytes.clone(),&refs);
                let cold=parse_bundle_uncached(bytes.clone().into());
                assert_eq!(named_png_from_parsed(&cold,&refs),legacy,"cold named/fuzzy lookup must preserve ranking {source}/{queries:?}");
                for _ in 0..2 {
                    let selected=if let Some(path)=&path {named_png_from_bundle(path,&refs)}
                        else {named_png_from_zip(&zip,"chara/_kero/CMS_NT_00.unity3d",&refs)};
                    assert_eq!(selected,legacy,"path/zip cached lookup must preserve ranking {source}/{queries:?}");
                }
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn installed_shader_declaration_distinguishes_rim_from_unsaved_material_float() {
        let path = &crate::test_support::game_path(r"\kks_decomp\plugins\KKS_Plugins\KKS_MaterialEditor\KK_Plugins.Resources.EC_Shaders.unity3d");
        if !path.ok_exists() { return; }
        let bundle = load_bundle_path_unbounded(path).expect("installed MaterialEditor shader bundle");
        let _ = bundle.data_checked();
        let mut found = std::collections::HashMap::new();
        for file in &bundle.files {
            let bytes = bundle.extract_file_data(file).expect("shader asset bytes");
            let serialized = parse_serialized_file(bytes).expect("serialized shader file");
            for info in serialized.objects_of_type(48) {
                let shader = UnityObject::from_serialized_file(&serialized, info).expect("Shader object");
                found.insert(shader_runtime_name(&shader), shader_declares_property(&shader, "_rimV"));
            }
        }
        assert_eq!(found.get("Shader Forge/main_hair"), Some(&Some(true)));
        assert_eq!(found.get("Shader Forge/main_emblem"), Some(&Some(false)));
        assert_eq!(found.get("Shader Forge/toon_glasses_lod0"), Some(&Some(false)));
        let parsed = parse_bundle_cached(crate::gamefs::read(path).expect("shader bundle bytes"));
        let hair = parsed.material_named("mat_main_hair").expect("hair material");
        assert_eq!(hair.shader, "Shader Forge/main_hair");
        assert_eq!(hair.shader_has_rim, Some(true));
        let glasses = parsed.material_named("mat_toon_glasses_lod0").expect("glasses material");
        assert_eq!(glasses.shader_has_rim, Some(false));
    }
    #[test]
    fn declared_sparse_uv_channels_do_not_shift_or_guess_data() {
        use unity_asset_decode::mesh::{Mesh,VertexData,ChannelInfo};
        let mut mesh=Mesh::default();
        let mut channels=vec![ChannelInfo::default();8];
        // Missing UV1 must not make UV2 its replacement. UV3 can be float4;
        // values outside [0,1] and tiny values are both valid authored UVs.
        channels[4]=ChannelInfo{stream:0,offset:0,format:0,dimension:2};
        channels[6]=ChannelInfo{stream:0,offset:8,format:0,dimension:2};
        channels[7]=ChannelInfo{stream:0,offset:16,format:0,dimension:4};
        let mut data=Vec::new();
        for vertex in [[0.2f32,0.3,2.,-3.,0.0004,0.0003,99.,88.],
                       [0.4,0.5,4.,-5.,0.0005,0.0006,77.,66.]] {
            for value in vertex {data.extend_from_slice(&value.to_le_bytes());}
        }
        mesh.vertex_data=VertexData{vertex_count:2,channels,data_size:data};
        assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("5.6.2f1").unwrap()),3);
        assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("2017.4.1f1").unwrap()),3);
        assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("2018.1.0f1").unwrap()),4);
        assert_eq!(texcoord_slot_base(&UnityVersion::parse_version("2019.4.9f1").unwrap()),4);
        assert!(decode_texcoord(&mesh,1,0).is_empty());
        assert_eq!(decode_texcoord(&mesh,2,0),vec![[2.,-3.],[4.,-5.]]);
        assert_eq!(decode_texcoord(&mesh,3,0),vec![[0.0004,0.0003],[0.0005,0.0006]]);
    }

    #[test]
    #[ignore="installed source mesh attributes for fresh native controls"]
    fn skinplus_source_declared_vertex_attributes() {
        let game=&crate::test_support::game_path("");
        let card=crate::card::parse_card(&game.join("UserData/chara/female/KK_380982.png")).unwrap();
        let (body,bytes)=crate::mods::uncensor_body_for_card(game,card.body_guid.as_deref(),card.sex).unwrap();
        let bundle=parse_bundle_cached(bytes);
        let (meshes,_)=bundle.meshes_for_maindata(&body.asset);
        let body=meshes.iter().find(|m|m.name=="o_body_a").unwrap();
        let vanilla=crate::load_characustom_dir(&game.join("abdata/list/characustom"));
        let index=crate::mods::find_guids(&game.join("mods"),None);
        let mut zip_lists=std::collections::HashMap::new();
        let entry=crate::resolve_face_entry(&vanilla,&card,&index,&mut zip_lists).unwrap();
        let head_bytes=crate::accessory_bundle_bytes(game,entry.extra.get("__zip").map(|s|Path::new(s)),&entry).unwrap();
        let head=parse_bundle_cached(head_bytes);
        eprintln!("SOURCE_HEAD id={} ab={} main={}",card.head_id,entry.main_ab,entry.info("MainData"));
        head.with_serialized(|files| {for file in files {for info in file.objects_of_type(MESH_CLASS_ID) {
            let object=UnityObject::from_serialized_file(file,info).unwrap();
            if object.name().unwrap_or_default()=="cf_O_face" {eprintln!("RAW_FACE_CHANNELS version={} channels={:?}",file.unity_version,object.class.properties().get("m_VertexData").and_then(|v|match v {unity_asset_core::UnityValue::Object(f)=>f.get("m_Channels"),_=>None}));}
        }}});
        let (meshes,_)=head.meshes_for_maindata(&entry.info("MainData"));
        let face=meshes.iter().find(|m|m.name=="cf_O_face").unwrap();
        let rows:Vec<_>=[body,face].into_iter().map(|m|serde_json::json!({"mesh":m.name,
            "uv0":m.uvs,"uv1":m.uv2,"uv2":m.uv3,"uv3":m.uv4,"colors32":m.vertex_colors})).collect();
        std::fs::write("C:/portable/OpenKoikatsu/Saved/Logs/Skin328SourceAttributes.json",serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
        assert_eq!(body.uv4.len(),body.positions.len());
        assert!(body.uv4.iter().flatten().all(|v|v.is_finite()));
    }

    #[test]
    fn mesh_float16_uv_streams_preserve_authored_coordinates() {
        use unity_asset_decode::mesh::{Mesh,VertexData,ChannelInfo};
        // Two packed UV sets, with the following stream aligned to 16 bytes.
        let mut mesh=Mesh::default();
        let mut channels=vec![ChannelInfo::default();6];
        channels[4]=ChannelInfo {stream:0,offset:0,format:1,dimension:2};
        channels[5]=ChannelInfo {stream:1,offset:0,format:1,dimension:2};
        let mut bytes=Vec::new();
        for pair in [[0x3400u16,0x3a00],[0x3c00,0xbc00],[0,0x3800]] {for value in pair {bytes.extend_from_slice(&value.to_le_bytes());}}
        bytes.resize(16,0);
        for pair in [[0x3800u16,0x3000],[0x3c00,0x4000],[0x3800,0x3800]] {for value in pair {bytes.extend_from_slice(&value.to_le_bytes());}}
        mesh.vertex_data=VertexData {vertex_count:3,channels,data_size:bytes};
        assert_eq!(super::decode_uvs(&mesh),vec![[0.25,0.75],[1.,-1.],[0.,0.5]]);
        assert_eq!(super::decode_texcoord(&mesh,1,0),vec![[0.5,0.125],[1.,2.],[0.5,0.5]]);
        assert_eq!(super::read_vertex_component(&[0,128],0,5),-1.);
        assert_eq!(super::read_vertex_component(&[128],0,3),-1.);
    }
    #[test]
    fn unity_dxt1_preserves_transparent_selector_and_opaque_four_colors() {
        let transparent = [0,0,255,255,255,255,255,255];
        let image = super::decode_unity_dxt1(&transparent,4,4).unwrap();
        assert!(image.pixels().all(|p|p.0==[0,0,0,0]));
        // endpoint0 > endpoint1 retains selector 3 as an opaque interpolation.
        let opaque = [0,248,31,0,255,255,255,255];
        let image=super::decode_unity_dxt1(&opaque,4,4).unwrap();
        assert!(image.pixels().all(|p|p.0[3]==255 && p.0[0]>p.0[2])); // decoder BGRA
        assert!(super::decode_unity_dxt1(&opaque[..7],4,4).is_none());
    }
    #[test]
    fn oversized_texture_payload_uses_declared_array_after_long_name() {
        use unity_asset_decode::texture::{Texture2D,TextureFormat};
        let name="長貼圖名".repeat(70);
        let image=vec![0x5a;1024];
        let mut raw=(name.len() as u32).to_le_bytes().to_vec();
        raw.extend_from_slice(name.as_bytes());while raw.len()%4!=0{raw.push(0);}
        raw.extend_from_slice(&[0;76]);
        raw.extend_from_slice(&(image.len() as u32).to_le_bytes());
        raw.extend_from_slice(&image);raw.extend_from_slice(&[0;12]);
        let mut tex=Texture2D::new(name,32,32,TextureFormat::DXT5);
        tex.image_data=vec![0;32];tex.image_data.extend_from_slice(&image);
        repair_inline_texture_payload(&raw,&mut tex,1024);
        assert_eq!(tex.image_data,image);
        assert_eq!(tex.data_size,1024);
        // A streamed object has no declared inline array to replace its data.
        repair_inline_texture_payload(&raw[..80],&mut tex,1024);
        assert_eq!(tex.image_data,image);
    }

    #[test]
    #[ignore="installed mod DXT5 payload / independent UnityPy image fixture"]
    fn native_mod_dxt5_texture_payload() {
        use unity_asset_decode::texture::TextureProcessor;
        let fixture=std::path::Path::new("C:/portable/OpenKoikatsu/Saved/Tests/SkinHsv307");
        let zip=&crate::test_support::game_path("/mods/KKSImportedMods/付费作者系列含mod/SamCosmyson/mod/[[OMEGA]] HS2 port to KK v1.8.1.zipmod");
        let bytes=crate::mods::zipmod_bundle_named(zip,"chara/HS2-KK.unity3d").unwrap();
        let parsed=parse_bundle_ref(&bytes);
        let count=parsed.with_serialized(|files| {
            let mut count=0;
            for file in files {
                for info in file.objects_of_type(28) {
                    let object=UnityObject::from_serialized_file(file,info).unwrap();
                    let name=object.name().unwrap_or_default();
                    if !["cf_shoes_sister_t","cf_shoes_sister_mc"].contains(&name.as_str()){continue;}
                    count+=1;
                    let mut tex=TextureProcessor::new(version()).convert_object(&object).unwrap();
                    let expected=crate::gamefs::read(fixture.join(format!("{name}.dxT5"))).unwrap();
                    eprintln!("DXT5 source name={name} format={:?} dimensions={}x{} bytes={} expected={} first={:?}",tex.format,tex.width,tex.height,tex.image_data.len(),expected.len(),&tex.image_data[..20.min(tex.image_data.len())]);
                    std::fs::write(fixture.join(format!("{name}.raw.bin")),object.raw_data()).unwrap();
                    repair_inline_texture_payload(object.raw_data(),&mut tex,expected.len());
                    assert!(tex.image_data==expected,"original compressed bytes must be exact");
                    let png=png_from_texture(&object,&parsed.files,None).unwrap().1;
                    std::fs::write(fixture.join(format!("{name}.Rust.png")),&png).unwrap();
                    let actual=crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
                    let mut reference=image::open(fixture.join(format!("{name}.UnityPy.png"))).unwrap().to_rgba8();
                    image::imageops::flip_vertical_in_place(&mut reference);
                    assert_eq!(actual.dimensions(),reference.dimensions());
                    let max=actual.as_raw().iter().zip(reference.as_raw()).map(|(a,b)|a.abs_diff(*b)).max().unwrap();
                    eprintln!("DXT5 decoded name={name} pixels={} RGBA max={max}",actual.width()*actual.height());
                    assert!(max<=1,"DXT5 decoded image disagrees with independent decoder");
                }
            }
            count
        });
        assert_eq!(count,2);
    }

    #[test]
    fn short_parsed_texture_recovers_declared_inline_mip_chain() {
        let min_bytes = 1024 * 1024;
        let image = vec![0x5a; min_bytes + min_bytes / 3];
        let mut raw = vec![0; 76];
        raw[28..32].copy_from_slice(&(image.len() as u32).to_le_bytes());
        raw.extend_from_slice(&(image.len() as u32).to_le_bytes());
        raw.extend_from_slice(&image);
        raw.extend_from_slice(&[0; 12]);
        assert_eq!(super::recover_inline_texture_data(&raw, min_bytes), Some(image));
        assert!(super::recover_inline_texture_data(&raw[..76], min_bytes).is_none());
    }
    use super::*;

    use std::path::PathBuf;
    use unity_asset_core::UnityValue;

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn long_sleeve_mesh_does_not_invent_source_tangents() {
        let zip = crate::test_support::game_path(r"\mods\Sideloader Modpack\gyobobo\[gyobobo]Long Sleeve T-Shirt Set v1.1.zipmod");
        if !zip.ok_exists() { return; }
        let (_, bytes) = crate::mods::zipmod_one_unity3d(&zip, "chara/_mods/LongSleeveT.unity3d")
            .expect("LongSleeveT bundle");
        let meshes = meshes_from_memory(bytes);
        let inner = meshes.iter().find(|m| m.name == "o_inner_a").expect("o_inner_a");
        assert_eq!(inner.source_tangents, Some(false));
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn pleated_skirt_keeps_authored_tangent_handedness() {
        let path = crate::test_support::game_path(r"\abdata\chara\co_bot_00.unity3d");
        if !path.ok_exists() { return; }
        let meshes = meshes_from_memory(crate::gamefs::read(path).expect("co_bot_00"));
        let skirt = meshes.iter().find(|m| m.name == "cf_bot_d_OpleatedSKF" && m.positions.len() == 924)
            .expect("924 vertex pleated skirt");
        assert_eq!(skirt.tangents.len(), skirt.positions.len());
        assert!(skirt.tangents.iter().all(|t| (t[3].abs() - 1.0).abs() < 0.01));
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn sac_innie_body_keeps_game_authored_tangents() {
        let zip = crate::test_support::game_path(r"\mods\Sideloader Modpack - KKS_UncensorSelector\[SAC] Uncensor Selector Pack v0.9.2.zipmod");
        if !zip.ok_exists() { return; }
        let (_, bytes) = crate::mods::zipmod_one_unity3d(&zip, "chara/oo_base_sacinnie.unity3d")
            .expect("SAC innie body bundle");
        let meshes = meshes_from_memory(bytes);
        let body = meshes.iter().find(|m| m.name == "o_body_a" && m.positions.len() == 9048).unwrap();
        assert_eq!(body.source_tangents, Some(true));
        assert_eq!(body.tangents.len(), body.positions.len());
        let dump = crate::test_support::game_path(r"\mesh_dump\KK_091207.png\outfit0-tpose\Ushiwakamaru_0\mesh_47_o_body_a.json");
        if !dump.ok_exists() { return; }
        let game: serde_json::Value = serde_json::from_slice(&crate::gamefs::read(dump).unwrap()).unwrap();
        let theirs = game["mesh"]["tangents"].as_array().unwrap();
        assert_eq!(body.tangents.len(), theirs.len());
        let mut sum = 0.0f64;
        let mut max = 0.0f64;
        let mut handedness_mismatch = 0usize;
        for (ours, theirs) in body.tangents.iter().zip(theirs) {
            let game = ["x", "y", "z"].map(|k| theirs[k].as_f64().unwrap() as f32);
            let dot = (ours[0] * game[0] + ours[1] * game[1] + ours[2] * game[2]).clamp(-1.0, 1.0);
            let deg = dot.acos().to_degrees() as f64;
            sum += deg;
            max = max.max(deg);
            if ours[3].signum() != (theirs["w"].as_f64().unwrap() as f32).signum() { handedness_mismatch += 1; }
        }
        let mean = sum / body.tangents.len() as f64;
        eprintln!("SAC tangent vs game mean={mean:.4} max={max:.4} degrees; handedness_mismatch={handedness_mismatch}");
        assert!(mean < 0.2 && max < 1.0 && handedness_mismatch == 0);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn four_vertex_accessory_prefab_is_not_discarded() {
        let Some(idx) = crate::sqlcache::guid_index(&crate::test_support::game_path(r"\mods")) else { return };
        let Some(zip) = idx.get("yamadamod.accprim") else { return };
        let (_, bytes) = crate::mods::zipmod_one_unity3d(zip, "chara/ymd/mf_accprim.unity3d").expect("accprim bundle");
        let (meshes, _) = parse_bundle_ref(&bytes).meshes_for_maindata("p_koi_stu_kihon14c_00");
        assert_eq!(meshes.len(), 1);
        assert_eq!(meshes[0].name, "O_nosetama");
        assert_eq!(meshes[0].positions.len(), 4);
        assert_eq!(meshes[0].indices.len(), 6);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn jacket_emblem_renderer_is_available_to_runtime_loader() {
        let path = crate::test_support::game_path(r"\abdata\chara\cpo_jacket_b_00.unity3d");
        if !path.ok_exists() { return; }
        let bytes = crate::gamefs::read(path).expect("jacket bundle");
        let parsed = parse_bundle_ref(&bytes);
        let (active, _) = parsed.meshes_for_maindata("p_o_top_jacket01");
        let (all, _) = parsed.meshes_for_maindata_including_disabled("p_o_top_jacket01");
        eprintln!("jacket active={:?} all={:?}", active.iter().map(|m| (&m.name, &m.renderer_names)).collect::<Vec<_>>(), all.iter().map(|m| (&m.name, &m.renderer_names)).collect::<Vec<_>>());
        assert!(all.iter().any(|m| m.name == "o_emblem_01_a"));
    }

    #[test]
    fn clothes_state_variants_follow_draw_clothes_slots() {
        let worn = [0u8; 9];
        assert_eq!(clothes_state_node_visible("n_top_a", &worn, Some(105)), Some(true));
        assert_eq!(clothes_state_node_visible("n_top_b", &worn, Some(105)), Some(false));
        let mut loose = worn;
        loose[0] = 1;
        loose[1] = 1;
        loose[2] = 1;
        loose[3] = 2;
        assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(105)), Some(true));
        assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(106)), Some(true));
        assert_eq!(clothes_state_node_visible("o_bra_b", &loose, Some(107)), Some(true));
        assert_eq!(clothes_state_node_visible("n_uwb_c", &loose, Some(108)), Some(true));
        assert_eq!(clothes_state_node_visible("n_uwb_a", &loose, Some(108)), Some(false));
        assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(107)), Some(true));
        loose[2] = 0;
        assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(107)), Some(false));
        assert_eq!(clothes_state_node_visible("n_bot_c", &loose, Some(108)), Some(true));
        // top lower panel follows bottoms; bottom upper panel follows top;
        // bra lower panel follows shorts, but subparts follow top alone.
        assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(105)), Some(true));
        assert_eq!(clothes_state_node_visible("n_top_b", &loose, Some(106)), Some(true));
        assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(107)), Some(true));
        assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(212)), Some(true));
        loose[1] = 0;
        assert_eq!(clothes_state_node_visible("n_bot_a", &loose, Some(105)), Some(true));
        assert_eq!(clothes_state_node_visible("n_bot_b", &loose, Some(105)), Some(false));
        loose[4] = 1;
        loose[5] = 2;
        loose[6] = 1;
        assert!(!clothes_kind_visible(4, &loose, 1));
        assert!(!clothes_kind_visible(5, &loose, 1));
        assert!(!clothes_kind_visible(6, &loose, 1));
        assert!(clothes_kind_visible(0, &loose, 1));
    }

    #[test]
    fn footwear_mode_is_exclusive_and_respects_removed_state() {
        let mut states=[0;9];
        assert!(clothes_kind_visible(7,&states,0));
        assert!(!clothes_kind_visible(8,&states,0));
        assert!(!clothes_kind_visible(7,&states,1));
        assert!(clothes_kind_visible(8,&states,1));
        assert!(!clothes_kind_visible(7,&states,2));
        assert!(!clothes_kind_visible(8,&states,2));
        states[8]=1;
        assert!(!clothes_kind_visible(8,&states,1));
        states[7]=3;
        assert!(!clothes_kind_visible(7,&states,0));
        assert!(clothes_kind_visible(0,&states,1));
    }

    #[test]
    fn material_property_lookup_prefers_exact_shader_name() {
        let mut mat = MeshMaterial::default();
        mat.colors.insert("_LineColor".into(), [0.5, 0.5, 0.5, 0.0]);
        mat.colors.insert("_linecolor".into(), [0.4, 0.4, 0.4, 0.25]);
        mat.floats.insert("_ShadowExtend".into(), 0.4);
        mat.floats.insert("_shadowextend".into(), 0.8);
        mat.tex_slots.push(("_linemask".into(), "lower".into()));
        mat.tex_slots.push(("_LineMask".into(), "exact".into()));
        assert_eq!(mat.color("_LineColor"), Some([0.5, 0.5, 0.5, 0.0]));
        assert_eq!(mat.float("_ShadowExtend"), Some(0.4));
        assert_eq!(mat.slot("_LineMask"), Some("exact"));
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn mm_base_body_draw_material_is_resolved_independently_of_body_mesh() {
        let path = crate::test_support::game_path(r"\abdata\chara\mm_base.unity3d");
        if !path.ok_exists() { return; }
        let parsed = parse_bundle_cached(crate::gamefs::read(path).unwrap());
        let mat = parsed.material_named("cf_m_body").expect("body draw material");
        eprintln!("body draw shader={} shadow={:?} detail={:?} line={:?} normal={:?}",
            mat.shader, mat.float("_ShadowExtend"), mat.slot("_DetailMask"),
            mat.slot("_LineMask"), mat.slot("_NormalMap"));
        assert!(mat.name.eq_ignore_ascii_case("cf_m_body"));
        assert_eq!(mat.float("_ShadowExtend"), Some(0.66));
        assert_eq!(mat.float("_linetexon"), Some(1.0));
        // Independently checked against this asset's UnityPy m_SavedProperties;
        // these values are hidden in the authored shader but survive SetShader.
        assert_eq!(mat.color("_EmissionColor"), Some([0.,0.,0.,1.]));
        assert_eq!(mat.color("_LiquidTiling"), Some([0.,0.,3.5,3.5]));
        assert_eq!(mat.color("_ShadowColor"), Some([1.,0.8963008,0.83599997,1.]));
        let card = crate::test_support::game_path(r"\UserData\chara\female\[Card] March_7th_keiyuu.png");
        if card.ok_exists() {
            let info = crate::card::parse_card_outfit(&card, 0).expect("March card");
            assert!(!info.body_draw_add_line, "VisibleAddBodyLine should disable this card's body lines");
        }
        for slot in ["_DetailMask", "_LineMask", "_NormalMap"] {
            let tex = mat.slot(slot).and_then(|name| parsed.texture_png_named(name));
            assert!(tex.as_ref().is_some_and(|png| png.len() > 16), "{slot}");
        }
    }

    #[test]
    fn material_editor_can_select_detached_prefab_renderer() {
        let mut graph = PrefabGraph::default();
        graph.go_name.insert(0, "f_11R".into());
        graph.go_name.insert(1, "f_11R".into());
        graph.go_name.insert(2, "AS01_O_kami_07F".into());
        graph.go_name.insert(3, "AS01_O_kami_07F_0".into());
        graph.go_tf.insert(0, 5);
        graph.go_tf.insert(1, 10);
        graph.go_tf.insert(2, 20);
        graph.go_tf.insert(3, 30);
        graph.tf_children.insert(10, vec![20]);
        graph.tf_go.insert(20, 2);
        graph.tf_go.insert(30, 3);
        graph.tf_parent.insert(30, 20);
        graph.smr_go.insert(100, 3);
        graph.smr_mesh.insert(100, 200);
        graph.comp_enabled.insert(100, false);
        assert!(query_prefab_graph(&graph, "f_11R", false).0.is_empty());
        let (meshes, _, names) = query_prefab_graph(&graph, "f_11R", true);
        assert_eq!(meshes, vec![200]);
        assert_eq!(names[&200], vec!["AS01_O_kami_07F_0"]);
    }

    #[test]
    fn cached_mesh_without_obj_remains_selectable() {
        let source = vec![ExtractedMesh {
            name: "cf_O_face".into(),
            obj: "v 0 0 0\n".into(),
            positions: vec![[0.0, 0.0, 0.0]],
            indices: vec![0, 0, 0],
            ..Default::default()
        }];
        let cached: Vec<ExtractedMesh> = bincode::deserialize(&bincode::serialize(&source).unwrap()).unwrap();
        assert!(cached[0].obj.is_empty());
        assert_eq!(pick_named(&cached, "cf_O_face").unwrap().indices.len(), 3);
    }

    fn png_px(r: u8, g: u8, b: u8, a: u8) -> Vec<u8> {
        let img = image::RgbaImage::from_pixel(1, 1, image::Rgba([r, g, b, a]));
        let mut buf = Vec::new();
        image::DynamicImage::ImageRgba8(img)
            .write_to(&mut std::io::Cursor::new(&mut buf), image::ImageFormat::Png)
            .unwrap();
        buf
    }

    #[test]
    fn hair_get_diffuse_mask_r_multiplies_color() {
        let main = png_px(128, 128, 128, 255);
        let mask = png_px(255, 0, 0, 255);
        let out = hair_get_diffuse(
            &main,
            Some(&mask),
            [1.0, 0.0, 0.0, 1.0],
            [0.0, 1.0, 0.0, 1.0],
            [0.0, 0.0, 1.0, 1.0],
        )
        .unwrap();
        let got = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
        let p = got.get_pixel(0, 0).0;
        assert!(p[0] > 100 && p[1] < 20 && p[2] < 20, "mask.r ??Color * main, got {p:?}");
    }

    #[test]
    fn hair_get_diffuse_black_mask_keeps_maintex() {
        let main = png_px(128, 64, 32, 255);
        let mask = png_px(0, 0, 0, 255);
        let out = hair_get_diffuse(
            &main,
            Some(&mask),
            [0.2, 0.4, 0.8, 1.0],
            [1.0, 0.0, 0.0, 1.0],
            [0.0, 1.0, 0.0, 1.0],
        )
        .unwrap();
        let got = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
        let p = got.get_pixel(0, 0).0;
        assert_eq!(p, [128, 64, 32, 255], "black ColorMask ??GetDiffuse=1");
    }

    fn png_mean_rgb(png: &[u8]) -> (u32, u32, u32, u32) {
        let img = crate::teximg::load_from_memory(png).unwrap().to_rgba8();
        let mut r = 0u64;
        let mut g = 0u64;
        let mut b = 0u64;
        let mut n = 0u64;
        for p in img.pixels() {
            if p[3] < 8 {
                continue;
            }
            if p[0] < 8 && p[1] < 8 && p[2] < 8 {
                continue;
            }
            r += p[0] as u64;
            g += p[1] as u64;
            b += p[2] as u64;
            n += 1;
        }
        if n == 0 {
            return (0, 0, 0, 0);
        }
        (
            (r / n) as u32,
            (g / n) as u32,
            (b / n) as u32,
            n as u32,
        )
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn named_png_prefers_eri_over_kage() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod"),
        );
        if !zip.ok_is_file() {
            return;
        }
        let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(&zip, "chara/gf_eyeline_up.unity3d")
        else {
            panic!("gf_eyeline_up missing");
        };
        let (n, png) = named_png_from_bytes(bytes, &["eri"]).expect("eri");
        let ln = n.to_ascii_lowercase();
        assert!(
            !ln.contains("kage") && !ln.contains("shadow"),
            "EyelineUpTex must not bind the shadow copy, got {n}"
        );
        assert!(
            ln == "eri" || ln == "eri_low",
            "expected eri / eri_low, got {n}"
        );
        let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
        let a_max = img.pixels().map(|p| p[3]).max().unwrap_or(0);
        assert!(a_max > 200, "eri must have opaque stroke pixels");
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn nicoichi_front_hair_has_color_mask() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\Sideloader Modpack\yamadamod\[yamadamod]hairNicoichi rev4.zipmod"),
        );
        if !zip.ok_is_file() {
            return;
        }
        let Some((_, bytes)) =
            crate::mods::zipmod_one_unity3d(&zip, "chara/ymd/mf_hairNicoichi.unity3d")
        else {
            panic!("nicoichi unity3d missing");
        };
        let (meshes, _) = meshes_skel_for_maindata_ref(&bytes, "p_cf_hair_f_04kai");
        assert!(!meshes.is_empty(), "front hair prefab empty");
        assert!(meshes.iter().any(|m| m.uv2.len() == m.positions.len()
            && m.uv2.iter().any(|uv| uv[0].abs() > 0.001 || uv[1].abs() > 0.001)),
            "hair gloss requires the prefab's authored second UV channel");
        let with_mask = meshes.iter().filter(|m| m.color_mask_png.is_some()).count();
        for m in &meshes {
            let main = m
                .texture_png
                .as_ref()
                .map(|p| png_mean_rgb(p))
                .unwrap_or((0, 0, 0, 0));
            let mask = m
                .color_mask_png
                .as_ref()
                .map(|p| png_mean_rgb(p))
                .unwrap_or((0, 0, 0, 0));
            eprintln!(
                "HAIRTEX {} tex={} main_len={} mask_len={} main_rgb={:?} mask_rgb={:?}",
                m.name,
                m.texture_name,
                m.texture_png.as_ref().map(|p| p.len()).unwrap_or(0),
                m.color_mask_png.as_ref().map(|p| p.len()).unwrap_or(0),
                main,
                mask
            );
        }
        assert!(
            with_mask == meshes.len(),
            "every hair SMR must bind _ColorMask, got {with_mask}/{}",
            meshes.len()
        );
        for m in &meshes {
            let tn = m.texture_name.to_ascii_lowercase();
            assert!(
                !tn.ends_with("_mc") && !tn.ends_with("_md"),
                "{} MainTex must not be ColorMask/DetailMask {}",
                m.name,
                m.texture_name
            );
            let main = m.texture_png.as_ref().map(|p| png_mean_rgb(p)).unwrap();
            assert!(
                main.1 <= main.0.saturating_add(40) || main.0 >= 200,
                "{} MainTex must not be the green DetailMask, mean={main:?}",
                m.name
            );
            let tinted = hair_get_diffuse(
                m.texture_png.as_deref().unwrap(),
                m.color_mask_png.as_deref(),
                [0.40, 0.41, 0.43, 1.0],
                [0.40, 0.41, 0.43, 1.0],
                [0.40, 0.41, 0.43, 1.0],
            )
            .unwrap();
            let d = png_mean_rgb(&tinted);
            assert!(
                d.1 <= d.0.saturating_add(25) && d.1 <= d.2.saturating_add(25),
                "{} GetDiffuse must follow hair color, not MainTex chroma, got {d:?}",
                m.name
            );
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn second_prefab_from_same_bundle_hits_cache() {
        let path = crate::test_support::game_path(r"\abdata\chara\co_shoes_00.unity3d");
        if !path.ok_is_file() {
            return;
        }
        let bytes = crate::gamefs::read(&path).unwrap();
        clear_bundle_cache();
        let t0 = std::time::Instant::now();
        let (m1, _) = meshes_skel_for_maindata(bytes.clone(), "p_o_shoes_summer");
        let first = t0.elapsed();
        assert!(!m1.is_empty(), "shoes prefab empty");
        let cached = parsed_if_cached(&bytes).expect("first decode must populate this thread's bundle cache");
        let t1 = std::time::Instant::now();
        let (m2, _) = meshes_skel_for_maindata(bytes.clone(), "p_o_shoes_summer");
        let second = t1.elapsed();
        assert_eq!(m1.len(), m2.len());
        eprintln!(
            "ok-extract: bundle cache first={:.1}ms second={:.1}ms n={}",
            first.as_secs_f64() * 1000.0,
            second.as_secs_f64() * 1000.0,
            m1.len()
        );
        // Wall-clock thresholds are benchmarks, not cache correctness: other
        // installed fixture tests can saturate CPU and memory concurrently.
        assert!(Arc::ptr_eq(&cached, &parsed_if_cached(&bytes).expect("cached second decode")));
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_head_uv() {
        let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
        let bundle = load_bundle(&path).expect("bundle");
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
                let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
                if mesh.name != "cf_O_face" {
                    continue;
                }
                let n = mesh.vertex_data.vertex_count as usize;
                let (strides, offs) = channel_layout(&mesh);
                eprintln!("DIAGHU face verts={n} strides={strides:?} offs={offs:?}");
                for (i, c) in mesh.vertex_data.channels.iter().enumerate() {
                    eprintln!("DIAGHU ch{i} stream={} offset={} fmt={} dim={}", c.stream, c.offset, c.format, c.dimension);
                }
                let data = &mesh.vertex_data.data_size;
                for ci in [4usize, 5, 6, 7] {
                    let Some(c) = mesh.vertex_data.channels.get(ci) else { continue };
                    if c.dimension < 2 {
                        continue;
                    }
                    let stride = strides[c.stream as usize];
                    let base = offs[c.stream as usize];
                    let mut good = 0usize;
                    let mut first = Vec::new();
                    for i in 0..n {
                        let o = base + i * stride + c.offset as usize;
                        let u = read_f32(data, o);
                        let v = read_f32(data, o + 4);
                        if u.is_finite() && v.is_finite() && u.abs() < 3.0 && v.abs() < 3.0 {
                            good += 1;
                        }
                        if i < 3 {
                            first.push([u, v]);
                        }
                    }
                    eprintln!("DIAGHU ch{ci} good={good}/{n} first={first:?}");
                }
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_body_uv() {
        let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
        let bundle = load_bundle(&path).expect("bundle");
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
                let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
                if mesh.name != "o_body_a" || mesh.vertex_data.vertex_count != 6823 {
                    continue;
                }
                let n = mesh.vertex_data.vertex_count as usize;
                let (strides, offs) = channel_layout(&mesh);
                eprintln!("DIAGUV strides={strides:?} offs={offs:?}");
                for (i, c) in mesh.vertex_data.channels.iter().enumerate() {
                    eprintln!("DIAGUV ch{i} stream={} offset={} fmt={} dim={}", c.stream, c.offset, c.format, c.dimension);
                }
                let data = &mesh.vertex_data.data_size;
                let start = offs[1];
                let hex: String = data[start..(start + 60).min(data.len())]
                    .iter().map(|b| format!("{:02x}", b)).collect();
                eprintln!("DIAGUV stream1 v0.. {hex}");
                let uvs = decode_uvs(&mesh);
                eprintln!("DIAGUV uvs first4={:?}", &uvs[..4]);
                let pos = decode_positions(&mesh);
                eprintln!("DIAGUV pos first2={:?}", &pos[..2]);
                let _ = n;
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn diag_eye_uv() {
        let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
        let bundle = load_bundle(&path).expect("bundle");
        let mut printed = 0;
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
                let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
                if mesh.name != "cf_Ohitomi_L" {
                    continue;
                }
                printed += 1;
                let n = mesh.vertex_data.vertex_count as usize;
                let (strides, offs) = channel_layout(&mesh);
                eprintln!("EYEUV strides={strides:?} offs={offs:?} n={n}");
                for (i, c) in mesh.vertex_data.channels.iter().enumerate() {
                    eprintln!(
                        "EYEUV ch{i} stream={} offset={} fmt={} dim={}",
                        c.stream, c.offset, c.format, c.dimension
                    );
                }
                let data = &mesh.vertex_data.data_size;
                for ci in [4usize, 5, 6, 7] {
                    let Some(c) = mesh.vertex_data.channels.get(ci) else { continue };
                    if c.dimension < 2 {
                        continue;
                    }
                    let stride = strides[c.stream as usize];
                    let base = offs[c.stream as usize];
                    if stride == 0 || base + n * stride > data.len() {
                        continue;
                    }
                    let off = c.offset as usize;
                    let mut first = Vec::new();
                    for i in 0..4 {
                        let o = base + i * stride + off;
                        first.push((read_f32(data, o), read_f32(data, o + 4)));
                    }
                    eprintln!("EYEUV ch{ci} first4={first:?}");
                }
            }
        }
        eprintln!("EYEUV variants printed={printed}");
    }

    fn dump_tex_stats(label: &str, png: &[u8], out_dir: &std::path::Path) {
        let img = crate::teximg::load_from_memory(png).unwrap().to_rgba8();
        let (w, h) = img.dimensions();
        let mut amin = 255u8;
        let mut amax = 0u8;
        let mut r = 0u64;
        let mut g = 0u64;
        let mut b = 0u64;
        let mut a = 0u64;
        let n = (w as u64 * h as u64).max(1);
        for p in img.pixels() {
            amin = amin.min(p[3]);
            amax = amax.max(p[3]);
            r += p[0] as u64;
            g += p[1] as u64;
            b += p[2] as u64;
            a += p[3] as u64;
        }
        eprintln!(
            "FACERAW {label} {w}x{h} rgb=({},{},{}) a_mean={} a_range=[{amin},{amax}]",
            r / n,
            g / n,
            b / n,
            a / n
        );
        let _ = std::fs::create_dir_all(out_dir);
        let _ = std::fs::write(out_dir.join(format!("{label}.png")), png);
    }

    #[test]
    fn dump_card_face_raw_textures() {
        let out = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw");
        let zip_pairs: &[(&str, &str, &str, &str)] = &[
            (
                "mayuge",
                r"mods\Sideloader Modpack\moderchan\[moderchan]charamaker support MOD v1.1.zipmod",
                "chara/_kero/CMS_NT_00.unity3d",
                "cw_t_mayuge_011_vl",
            ),
            (
                "noseline",
                r"mods\Sideloader Modpack\moderchan\[moderchan]charamaker support MOD v1.1.zipmod",
                "chara/_kero/mt_nose_NT.unity3d",
                "cw_t_noseline_002",
            ),
            (
                "eyeline_up",
                r"mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod",
                "chara/gf_eyeline_up.unity3d",
                "eri",
            ),
            (
                "eyeline_down",
                r"mods\Sideloader Modpack\GF\[GF] Character Texture Pack v1.7.3.zipmod",
                "chara/gf_eyeline_down.unity3d",
                "motoko",
            ),
        ];
        for (label, zip, ab, name) in zip_pairs {
            let zp = crate::test_support::game_path(zip);
            if !zp.ok_is_file() {
                eprintln!("FACERAW missing zip {zip}");
                continue;
            }
            match crate::mods::zipmod_one_unity3d(&zp, ab) {
                Some((_, bytes)) => {
                    let parsed = parse_bundle_ref(&bytes);
                    parsed.ensure_tex_catalog();
                    for (n, px) in parsed.tex_names.borrow().iter() {
                        if n.to_ascii_lowercase().contains(&name.to_ascii_lowercase())
                            || n.to_ascii_lowercase().contains("eri")
                            || n.to_ascii_lowercase().contains("motoko")
                            || n.to_ascii_lowercase().contains("mayuge")
                            || n.to_ascii_lowercase().contains("nose")
                        {
                            eprintln!("FACERAW catalog {label} {n} px={px}");
                        }
                    }
                    match named_png_from_bytes(bytes, &[name]) {
                        Some((n, b)) => dump_tex_stats(&format!("{label}_{n}"), &b, &out),
                        None => eprintln!("FACERAW fail tex {label} {name}"),
                    }
                }
                None => eprintln!("FACERAW fail bundle {ab}"),
            }
        }
        for (label, path, name) in [
            (
                "sirome",
                r"abdata\chara\mt_eye_white_00.unity3d",
                "cw_t_sirome_kage_001",
            ),
            (
                "hitomi",
                r"abdata\chara\mt_eye_00.unity3d",
                "cw_t_hitomi_066",
            ),
        ] {
            match named_png_from_bundle(&crate::test_support::game_path(path), &[name]) {
                Some((n, b)) => dump_tex_stats(&format!("{label}_{n}"), &b, &out),
                None => eprintln!("FACERAW fail {label} {name}"),
            }
        }
    }

    #[test]
    fn diag_eyebrow_decode() {
        let p = crate::test_support::game_path(r"\abdata\chara\mt_eyebrow_00.unity3d");
        match named_png_from_bundle(&p, &["cw_t_mayuge_010"]) {
            Some((n, b)) => eprintln!("DIAGEW named decoded {n} bytes={}", b.len()),
            None => eprintln!("DIAGEW named FAILED"),
        }
        match named_png_from_bundle(&p, &["cw_t_mayuge_200"]) {
            Some((n, b)) => eprintln!("DIAGEW named200 decoded {n} bytes={}", b.len()),
            None => eprintln!("DIAGEW named200 FAILED"),
        }
    }

    #[test]
    fn dump_body_tex() {
        for p in [
            crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d"),
            crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d"),
        ] {
            let (_, _, texs) = load_base_bundle(&p);
            for (n, b) in texs {
                eprintln!("DIAGBT tex {n} bytes={}", b.len());
                let safe: String = n.chars().map(|c| if c.is_ascii_alphanumeric() || c=='.' || c=='_' {c} else {'_'}).collect();
                let out = format!(r"C:\Users\Damody\AppData\Local\Temp\opencode\br_{safe}");
                std::fs::write(&out, &b).ok();
            }
        }
    }

    fn uv_u32s(v: &unity_asset_core::UnityValue) -> Vec<u32> {
        match v {
            unity_asset_core::UnityValue::Array(a) => a.iter().flat_map(uv_u32s).collect(),
            unity_asset_core::UnityValue::Integer(i) => vec![*i as u32],
            unity_asset_core::UnityValue::Float(f) => vec![*f as u32],
            _ => Vec::new(),
        }
    }

    fn safe_stem(s: &str) -> String {
        let t: String = s
            .chars()
            .map(|c| {
                if c.is_ascii_alphanumeric() || c == '_' || c == '-' {
                    c
                } else {
                    '_'
                }
            })
            .collect();
        if t.is_empty() {
            "unnamed".into()
        } else {
            t
        }
    }

    #[test]
    #[ignore="installed shader exporter; requires explicit fresh OK_DXBC_ROOT and OK_DXBC_BUNDLE"]
    fn dump_game_dxbc() {
        let root = PathBuf::from(std::env::var("OK_DXBC_ROOT").expect("Set a fresh OK_DXBC_ROOT for shader export"));
        assert!(!root.ok_exists(),"Preserve previous shader exports; use a fresh output directory");
        std::fs::create_dir_all(&root).unwrap();
        let bundles = [std::env::var("OK_DXBC_BUNDLE").expect("Set OK_DXBC_BUNDLE to a shader-containing bundle")];
        let mut index = String::from("bundle\tshader\tblob\tsize\tdxbc\n");
        let mut names = Vec::new();
        let mut nblob = 0usize;
        for bp in bundles {
            let path = PathBuf::from(bp);
            let Ok(bundle) = load_bundle_path_unbounded(&path) else {
                continue;
            };
            let _ = bundle.data_checked();
            let bname = path.file_stem().unwrap().to_string_lossy().into_owned();
            let bdir = root.join(&bname);
            std::fs::create_dir_all(&bdir).ok();
            for file in &bundle.files {
                let Ok(fb) = bundle.extract_file_data(file) else { continue };
                let Ok(serialized) = parse_serialized_file(fb) else { continue };
                for info in serialized.objects_of_type(48) {
                    let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else {
                        continue;
                    };
                    let props = obj.class.properties();
                    let tag = {
                        let from_parsed = props.get("m_ParsedForm").and_then(|v| match v {
                            unity_asset_core::UnityValue::Object(o) => o
                                .get("m_Name")
                                .and_then(|x| x.as_str())
                                .map(|s| s.to_string()),
                            _ => None,
                        });
                        from_parsed
                            .or_else(|| {
                                props
                                    .get("m_Name")
                                    .and_then(|v| v.as_str())
                                    .map(|s| s.to_string())
                            })
                            .unwrap_or_default()
                    };
                    if std::env::var_os("OK_SHADER_PROPS").is_some() {
                        let _ = std::fs::write(root.join(format!("{}_shader_props.txt", safe_stem(&tag))), format!("{:#?}", props));
                    }
                    names.push(tag.clone());
                    let Some(unity_asset_core::UnityValue::Bytes(comp)) =
                        props.get("compressedBlob")
                    else {
                        eprintln!("DXBC {tag} no compressedBlob");
                        continue;
                    };
                    let blocks = decompress_shader_platforms(comp,
                        &shader_blob_lengths(props.get("offsets")),
                        &shader_blob_lengths(props.get("compressedLengths")),
                        &shader_blob_lengths(props.get("decompressedLengths")));
                    let raw: Vec<u8> = blocks.iter().flat_map(|block| block.iter().copied()).collect();
                    let sdir = bdir.join(safe_stem(&tag));
                    std::fs::create_dir_all(&sdir).ok();
                    std::fs::write(sdir.join("decompressed.bin"), &raw).ok();
                    let blobs = dxbc_extract(&raw);
                    eprintln!(
                        "DXBC shader={tag} compressed={} platforms={} decompressed={} dxbc={}",
                        comp.len(),
                        blocks.len(),
                        raw.len(),
                        blobs.len()
                    );
                    for (i, blob) in blobs.iter().enumerate() {
                        nblob += 1;
                        let dxbc = sdir.join(format!("{i:04}.dxbc"));
                        std::fs::write(&dxbc, blob).ok();
                        index.push_str(&format!(
                            "{bname}\t{tag}\t{i}\t{}\t{}\n",
                            blob.len(),
                            dxbc.display()
                        ));
                    }
                }
            }
        }
        std::fs::write(root.join("index.tsv"), &index).unwrap();
        std::fs::write(root.join("shaders.txt"), names.join("\n")).ok();
        eprintln!("DXBC wrote {nblob} blobs shaders={} -> {}", names.len(), root.display());
        assert!(nblob > 0, "no DXBC programs in selected shader bundle");
    }


    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn ibl_alpha_shader_platform_blocks_are_separate() {
        let zip = crate::test_support::game_path(r"\mods\Sideloader Modpack - KK_MaterialEditor\[nashi]IBL Shader v1.1.zipmod");
        if !zip.ok_exists() { return; }
        let Some((_, bytes)) = crate::mods::zipmod_one_unity3d(&zip, "chara/IBLShader_alpha.unity3d") else {
            panic!("IBL alpha shader bundle missing from installed mod");
        };
        let bundle = load_bundle_unbounded(bytes).unwrap();
        let _ = bundle.data_checked();
        let mut found = false;
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue; };
            let Ok(serialized) = parse_serialized_file(fb) else { continue; };
            for info in serialized.objects_of_type(48) {
                let obj = UnityObject::from_serialized_file(&serialized, info).unwrap();
                assert_eq!(shader_runtime_name(&obj), "IBL_Shader_alpha");
                assert_eq!(shader_declares_property(&obj, "_rimV"), Some(true));
                let props = obj.class.properties();
                let Some(unity_asset_core::UnityValue::Bytes(comp)) = props.get("compressedBlob") else { continue; };
                let blocks = decompress_shader_platforms(comp,
                    &shader_blob_lengths(props.get("offsets")),
                    &shader_blob_lengths(props.get("compressedLengths")),
                    &shader_blob_lengths(props.get("decompressedLengths")));
                let blobs: Vec<Vec<u8>> = blocks.iter().flat_map(|raw| dxbc_extract(raw)).collect();
                assert_eq!(blocks.len(), 2, "one LZ4 block per Unity graphics API");
                assert!(blobs.len() >= 15, "full IBL shader includes pixel and vertex variants: {}", blobs.len());
                found = true;
            }
        }
        assert!(found, "IBL alpha Shader object not found");
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_me_hair_dxbc() {
        let path = crate::test_support::game_path(r"\kks_decomp\plugins\KKS_Plugins\KKS_MaterialEditor\KK_Plugins.Resources.EC_Shaders.unity3d");
        let bundle = load_bundle_path_unbounded(&path).expect("ME shader bundle");
        let _ = bundle.data_checked();
        let mut dumped = 0usize;
        let filter = std::env::var("OK_ME_SHADER_FILTER").unwrap_or_else(|_| "main_hair".into());
        let out_root = PathBuf::from(std::env::var("OK_ME_DXBC_ROOT")
            .unwrap_or_else(|_| r"C:\portable\OpenKoikatsu\Saved\ShaderDxbc\EC_Shaders".into()));
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in serialized.objects_of_type(48) {
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                let props = obj.class.properties();
                let tag = props.get("m_ParsedForm").and_then(|v| match v {
                    unity_asset_core::UnityValue::Object(o) => o.get("m_Name").and_then(|x| x.as_str()),
                    _ => None,
                }).unwrap_or("unknown");
                if !tag.contains(&filter) { continue }
                let comp = match props.get("compressedBlob") {
                    Some(unity_asset_core::UnityValue::Bytes(comp)) => comp,
                    _ => { eprintln!("ME {tag}: no compressedBlob"); continue; }
                };
                let blocks = decompress_shader_platforms(comp,
                    &shader_blob_lengths(props.get("offsets")),
                    &shader_blob_lengths(props.get("compressedLengths")),
                    &shader_blob_lengths(props.get("decompressedLengths")));
                let raw: Vec<u8> = blocks.iter().flat_map(|block| block.iter().copied()).collect();
                let blobs = dxbc_extract(&raw);
                eprintln!("ME {tag}: comp={} platforms={} raw={} dxbc={}", comp.len(), blocks.len(), raw.len(), blobs.len());
                let dir = out_root.join(safe_stem(tag));
                std::fs::create_dir_all(&dir).unwrap();
                for (i, blob) in blobs.iter().enumerate() { std::fs::write(dir.join(format!("{i:04}.dxbc")), blob).unwrap(); }
                dumped += blobs.len();
            }
        }
        if filter == "main_hair" {
            assert!(dumped > 0, "ME main_hair shaders had no DXBC");
        }
    }


    fn dxbc_extract(data: &[u8]) -> Vec<Vec<u8>> {
        let mag = b"DXBC";
        let mut out = Vec::new();
        let mut i = 0;
        while i + 28 <= data.len() {
            if &data[i..i + 4] == mag {
                let size = u32::from_le_bytes(data[i + 24..i + 28].try_into().unwrap()) as usize;
                if size >= 32 && size <= 8 * 1024 * 1024 && i + size <= data.len() {
                    out.push(data[i..i + size].to_vec());
                    i += size;
                    continue;
                }
            }
            i += 1;
        }
        out
    }

    #[test]
    #[ignore="historical installed Ushiwakamaru face dump and mod fixture required"]
    fn eyeshadow_00f_alpha_at_dump_eye_uv() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
        else {
            return;
        };
        let (_, png) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
        let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
        let (w, h) = img.dimensions();
        let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit4\Ushiwakamaru_0\mesh_22_cf_O_face.json"),
        )
        .unwrap();
        let uv_s = dump.split("\"uv\":[").nth(1).unwrap();
        let uv_s = uv_s.split(']').next().unwrap();
        let mut uvs = Vec::new();
        for cap in uv_s.split("},{") {
            let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
            let y = cap.split("\"y\":").nth(1).and_then(|s| s.split(|c| c == '}' || c == ',').next());
            if let (Some(x), Some(y)) = (x, y) {
                if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                    uvs.push([u, v]);
                }
            }
        }
        let sample = |u: f32, v: f32| -> u8 {
            let x = ((u.clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
            let y = ((v.clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
            img.get_pixel(x, y).0[3]
        };
        let mut sum = [0u64; 4];
        let mut n = 0u64;
        for [u, v] in &uvs {
            if !((*v > 0.55 && *v < 0.82)
                && ((*u > 0.22 && *u < 0.43) || (*u > 0.57 && *u < 0.80)))
            {
                continue;
            }
            n += 1;
            sum[0] += sample(*u, *v) as u64;
            sum[1] += sample(*u, 1.0 - *v) as u64;
            sum[2] += sample(*u, *v * (h as f32 / w as f32)) as u64;
            sum[3] += sample(*u, 1.0 - *v * (h as f32 / w as f32)) as u64;
        }
        eprintln!(
            "ES00F {w}x{h} eye_n={n} meanA uv=({:.1}) 1-v=({:.1}) v*h/w=({:.1}) 1-v*h/w=({:.1})",
            sum[0] as f32 / n as f32,
            sum[1] as f32 / n as f32,
            sum[2] as f32 / n as f32,
            sum[3] as f32 / n as f32,
        );
        assert!(n > 10);
        assert!(
            sum[0] > sum[1],
            "00f blobs sit at PNG y=v*h; flip before overlaying the flipped face RT"
        );
        let uv4_s = dump.split("\"uv4\":[").nth(1).unwrap();
        let uv4_s = uv4_s.split(']').next().unwrap();
        let mut uv4s = Vec::new();
        for cap in uv4_s.split("},{") {
            let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
            let y = cap.split("\"y\":").nth(1).and_then(|s| s.split(|c| c == '}' || c == ',').next());
            if let (Some(x), Some(y)) = (x, y) {
                if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                    uv4s.push([u, v]);
                }
            }
        }
        let fract = |t: f32| t - t.floor();
        let mut s4 = [0u64; 4];
        let mut mx = [0u8; 4];
        let mut n4 = 0u64;
        for ([u0, v0], [u4, v4]) in uvs.iter().zip(uv4s.iter()) {
            if !((*v0 > 0.55 && *v0 < 0.82)
                && ((*u0 > 0.22 && *u0 < 0.43) || (*u0 > 0.57 && *u0 < 0.80)))
            {
                continue;
            }
            n4 += 1;
            let a0 = sample(*u4, *v4);
            let a1 = sample(*u4, 1.0 - *v4);
            let a2 = sample(fract(*u4), fract(*v4));
            let a3 = sample(fract(*u4), 1.0 - fract(*v4));
            s4[0] += a0 as u64;
            s4[1] += a1 as u64;
            s4[2] += a2 as u64;
            s4[3] += a3 as u64;
            mx[0] = mx[0].max(a0);
            mx[1] = mx[1].max(a1);
            mx[2] = mx[2].max(a2);
            mx[3] = mx[3].max(a3);
        }
        eprintln!(
            "ES00F uv4 eye_n={n4} meanA clamp=({:.1}/{m0}) 1-v=({:.1}/{m1}) fract=({:.1}/{m2}) fract1-v=({:.1}/{m3})",
            s4[0] as f32 / n4 as f32,
            s4[1] as f32 / n4 as f32,
            s4[2] as f32 / n4 as f32,
            s4[3] as f32 / n4 as f32,
            m0 = mx[0],
            m1 = mx[1],
            m2 = mx[2],
            m3 = mx[3],
        );
        let _ = std::fs::write(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw\cw_t_eyeshadow_00f.png", &png);
    }

    #[test]
    #[ignore="historical installed Ushiwakamaru face dump required"]
    fn face_uv4_matches_dump_range() {
        let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
        if !path.ok_is_file() {
            return;
        }
        let meshes = crate::unity::meshes_from_bundle_path(&path);
        let face = meshes
            .iter()
            .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
            .expect("cf_O_face");
        let mut umin = f32::MAX;
        let mut umax = f32::MIN;
        let mut vmin = f32::MAX;
        let mut vmax = f32::MIN;
        for [u, v] in &face.uv4 {
            umin = umin.min(*u);
            umax = umax.max(*u);
            vmin = vmin.min(*v);
            vmax = vmax.max(*v);
        }
        eprintln!(
            "FACEUV4 n={} u=[{umin:.3},{umax:.3}] v=[{vmin:.3},{vmax:.3}]",
            face.uv4.len()
        );
        assert!(umin < 0.0 && umax > 1.2, "uv4 u {umin} {umax}");
        assert!(vmin < -1.0 && vmax > 1.5, "uv4 v {vmin} {vmax}");
        let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\mesh_24_cf_O_face.json"),
        )
        .unwrap();
        let parse = |key: &str| -> Vec<[f32; 2]> {
            let s = dump.split(&format!("\"{key}\":[")).nth(1).unwrap();
            let s = s.split(']').next().unwrap();
            let mut out = Vec::new();
            for cap in s.split("},{") {
                let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
                let y = cap
                    .split("\"y\":")
                    .nth(1)
                    .and_then(|s| s.split(|c| c == '}' || c == ',').next());
                if let (Some(x), Some(y)) = (x, y) {
                    if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                        out.push([u, v]);
                    }
                }
            }
            out
        };
        let d0 = parse("uv");
        let d4 = parse("uv4");
        let n = face.uv4.len().min(d4.len()).min(face.uvs.len()).min(d0.len());
        let mut e0 = 0.0f32;
        let mut e4 = 0.0f32;
        let mut e4f = 0.0f32;
        for i in 0..n {
            e0 += (face.uvs[i][0] - d0[i][0]).abs() + (face.uvs[i][1] - d0[i][1]).abs();
            e4 += (face.uv4[i][0] - d4[i][0]).abs() + (face.uv4[i][1] - d4[i][1]).abs();
            e4f += (face.uv4[i][0] - d4[i][0]).abs() + (face.uv4[i][1] - (1.0 - d4[i][1])).abs();
        }
        eprintln!(
            "FACEUV vs dump n={n} meanAbs uv0={:.4} uv4={:.4} uv4_vs_1-v={:.4} our0={:?} dump0={:?} our4={:?} dump4={:?}",
            e0 / n as f32,
            e4 / n as f32,
            e4f / n as f32,
            face.uvs[0],
            d0[0],
            face.uv4[0],
            d4[0]
        );
        let z4 = face.uv4.iter().filter(|uv| uv[0] == 0.0 && uv[1] == 0.0).count();
        let mut e4m = 0.0f32;
        let mut e4mf = 0.0f32;
        let mut hits = 0u32;
        for i in 0..n.min(200) {
            let [u, v] = face.uvs[i];
            let mut best = f32::MAX;
            let mut bj = 0usize;
            for (j, d) in d0.iter().enumerate() {
                let e = (d[0] - u).abs() + (d[1] - v).abs();
                let f = (d[0] - u).abs() + (d[1] - (1.0 - v)).abs();
                let m = e.min(f);
                if m < best {
                    best = m;
                    bj = j;
                }
            }
            if best < 0.02 {
                hits += 1;
                e4m += (face.uv4[i][0] - d4[bj][0]).abs() + (face.uv4[i][1] - d4[bj][1]).abs();
                e4mf += (face.uv4[i][0] - d4[bj][0]).abs()
                    + (face.uv4[i][1] - (1.0 - d4[bj][1])).abs();
            }
        }
        eprintln!(
            "FACEUV4 zeros={z4} matched_uv0={hits}/200 uv4_at_match={:.4} uv4_1-v={:.4}",
            e4m / hits.max(1) as f32,
            e4mf / hits.max(1) as f32
        );
    }

    #[test]
    #[ignore="historical baked PNG and installed Ushiwakamaru face dump required"]
    fn baked_face_has_eyeshadow_at_eye_uv() {
        let p = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_user_check\head.png");
        if !p.ok_is_file() {
            return;
        }
        let img = image::open(&p).unwrap().to_rgba8();
        let (w, h) = img.dimensions();
        let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit4\Ushiwakamaru_0\mesh_22_cf_O_face.json"),
        )
        .unwrap();
        let uv_s = dump.split("\"uv\":[").nth(1).unwrap();
        let uv_s = uv_s.split(']').next().unwrap();
        let mut eye = Vec::new();
        let mut cheek = Vec::new();
        for cap in uv_s.split("},{") {
            let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
            let y = cap.split("\"y\":").nth(1).and_then(|s| s.split(|c| c == '}' || c == ',').next());
            let (Some(x), Some(y)) = (x, y) else { continue };
            let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) else { continue };
            let is_eye = (v > 0.55 && v < 0.82)
                && ((u > 0.22 && u < 0.43) || (u > 0.57 && u < 0.80));
            let is_cheek = v > 0.35 && v < 0.50 && u > 0.40 && u < 0.60;
            if is_eye {
                eye.push([u, v]);
            }
            if is_cheek {
                cheek.push([u, v]);
            }
        }
        let sample = |u: f32, v: f32, flip: bool| -> [u32; 4] {
            let x = ((u.clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
            let yv = if flip { 1.0 - v } else { v };
            let y = ((yv.clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
            let p = img.get_pixel(x, y).0;
            [p[0] as u32, p[1] as u32, p[2] as u32, p[3] as u32]
        };
        let mean = |pts: &[[f32; 2]], flip: bool| -> [f32; 3] {
            let mut s = [0u64; 3];
            for [u, v] in pts {
                let p = sample(*u, *v, flip);
                s[0] += p[0] as u64;
                s[1] += p[1] as u64;
                s[2] += p[2] as u64;
            }
            let n = pts.len().max(1) as f32;
            [s[0] as f32 / n, s[1] as f32 / n, s[2] as f32 / n]
        };
        let e0 = mean(&eye, false);
        let e1 = mean(&eye, true);
        let c0 = mean(&cheek, false);
        let c1 = mean(&cheek, true);
        eprintln!(
            "FACE {w}x{h} eye_n={} cheek_n={} eye(v)={:?} eye(1-v)={:?} cheek(v)={:?} cheek(1-v)={:?}",
            eye.len(),
            cheek.len(),
            e0,
            e1,
            c0,
            c1
        );
        assert!(!eye.is_empty());
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_eyeshadow_00f_matches_dump_size() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
        else {
            return;
        };
        let (n, b) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
        let img = crate::teximg::load_from_memory(&b).unwrap().to_rgba8();
        let (w, h) = img.dimensions();
        assert_eq!(n, "cw_t_eyeshadow_00f");
        assert_eq!((w, h), (512, 256), "dump _overtex3 is 512x256");
        let dir = PathBuf::from(r"C:\portable\OpenKoikatsu\Saved\Converted\_face_raw");
        let _ = std::fs::create_dir_all(&dir);
        let _ = std::fs::write(dir.join("cw_t_eyeshadow_00f.png"), &b);
        let c = [0.529f32, 0.230, 0.230, 0.65];
        let mut tint = img.clone();
        for p in tint.pixels_mut() {
            p[0] = ((p[0] as f32) * c[0]).clamp(0.0, 255.0) as u8;
            p[1] = ((p[1] as f32) * c[1]).clamp(0.0, 255.0) as u8;
            p[2] = ((p[2] as f32) * c[2]).clamp(0.0, 255.0) as u8;
            p[3] = ((p[3] as f32) * c[3]).clamp(0.0, 255.0) as u8;
        }
        let mut tb = Vec::new();
        image::DynamicImage::ImageRgba8(tint)
            .write_to(&mut std::io::Cursor::new(&mut tb), image::ImageFormat::Png)
            .unwrap();
        let _ = std::fs::write(dir.join("cw_t_eyeshadow_00f_tinted.png"), &tb);
        let mut flip = img.clone();
        image::imageops::flip_vertical_in_place(&mut flip);
        let mut fb = Vec::new();
        image::DynamicImage::ImageRgba8(flip)
            .write_to(&mut std::io::Cursor::new(&mut fb), image::ImageFormat::Png)
            .unwrap();
        let _ = std::fs::write(dir.join("cw_t_eyeshadow_00f_flipv.png"), &fb);
        eprintln!("ES00F wrote {} raw/tinted/flipv", dir.display());
    }

    #[test]
    #[ignore="historical installed Ushiwakamaru overlay dump required"]
    fn dump_overlay_vc_matches_png_sample() {
        let json = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\mesh_24_cf_O_face.json"),
        )
        .unwrap();
        let png = crate::gamefs::read(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\tex_4_cf_m_face_00__overtex3.png"),
        )
        .unwrap();
        let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
        let (w, h) = img.dimensions();
        let parse2 = |key: &str| -> Vec<[f32; 2]> {
            let s = json.split(&format!("\"{key}\":[")).nth(1).unwrap();
            let s = s.split(']').next().unwrap();
            let mut out = Vec::new();
            for cap in s.split("},{") {
                let x = cap.split("\"x\":").nth(1).and_then(|t| t.split(',').next());
                let y = cap
                    .split("\"y\":")
                    .nth(1)
                    .and_then(|t| t.split(|c| c == '}' || c == ',').next());
                if let (Some(x), Some(y)) = (x, y) {
                    if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                        out.push([u, v]);
                    }
                }
            }
            out
        };
        let uv4 = parse2("uv4");
        let col_s = json
            .split("\"overlayVertexColors\":")
            .nth(1)
            .unwrap()
            .split("\"colors\":[")
            .nth(1)
            .unwrap();
        let col_s = col_s.split(']').next().unwrap();
        let mut cols = Vec::new();
        for cap in col_s.split("},{") {
            let a = cap
                .split("\"a\":")
                .nth(1)
                .and_then(|t| t.split(|c| c == '}' || c == ',').next())
                .and_then(|t| t.parse::<f32>().ok());
            if let Some(a) = a {
                cols.push(a);
            }
        }
        let sample = |u: f32, v: f32, flip: bool| -> f32 {
            let uu = u.clamp(0.0, 1.0);
            let vv = if flip { 1.0 - v } else { v }.clamp(0.0, 1.0);
            let x = ((uu * (w - 1) as f32).round() as u32).min(w - 1);
            let y = ((vv * (h - 1) as f32).round() as u32).min(h - 1);
            img.get_pixel(x, y).0[3] as f32 / 255.0 * 0.65
        };
        let mut e0 = 0.0f32;
        let mut e1 = 0.0f32;
        let mut n = 0u32;
        let mut npos = 0u32;
        for i in 0..cols.len().min(uv4.len()) {
            let a = cols[i];
            if a <= 0.02 && sample(uv4[i][0], uv4[i][1], false) <= 0.02 && sample(uv4[i][0], uv4[i][1], true) <= 0.02
            {
                continue;
            }
            n += 1;
            if a > 0.02 {
                npos += 1;
            }
            e0 += (a - sample(uv4[i][0], uv4[i][1], false)).abs();
            e1 += (a - sample(uv4[i][0], uv4[i][1], true)).abs();
        }
        eprintln!(
            "OVC vs PNG n={n} dumpA>0.02={npos} meanAbs y=v={:.4} y=1-v={:.4} img={w}x{h}",
            e0 / n.max(1) as f32,
            e1 / n.max(1) as f32
        );
        assert!(npos > 50, "dump overlayVertexColors should stain lids");
        let m0 = e0 / n.max(1) as f32;
        let m1 = e1 / n.max(1) as f32;
        assert!(m1 < 0.02 && m1 < m0, "dump OVC is PNG y=1-v, got {m1} vs y=v {m0}");
        let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
        if path.ok_is_file() {
            let meshes = crate::unity::meshes_from_bundle_path(&path);
            if let Some(face) = meshes.iter().find(|m| m.name.eq_ignore_ascii_case("cf_O_face")) {
                let d0 = parse2("uv");
                let mut e4 = 0.0f32;
                let mut hits = 0u32;
                let mut shown = 0u32;
                for i in 0..cols.len().min(d0.len()).min(uv4.len()) {
                    if cols[i] <= 0.2 {
                        continue;
                    }
                    let [u, v] = d0[i];
                    let mut best = f32::MAX;
                    let mut bj = 0usize;
                    for (j, ou) in face.uvs.iter().enumerate() {
                        let e = (ou[0] - u).abs() + (ou[1] - v).abs();
                        if e < best {
                            best = e;
                            bj = j;
                        }
                    }
                    if best < 0.005 {
                        hits += 1;
                        let de = (face.uv4[bj][0] - uv4[i][0]).abs() + (face.uv4[bj][1] - uv4[i][1]).abs();
                        e4 += de;
                        if shown < 6 {
                            shown += 1;
                            eprintln!(
                                "LID uv0=({:.3},{:.3}) dump4={:?} our4={:?} uv0err={:.4} uv4err={:.4}",
                                u, v, uv4[i], face.uv4[bj], best, de
                            );
                        }
                    }
                }
                eprintln!(
                    "LIDMATCH hits={hits} mean uv4 abs={:.4}",
                    e4 / hits.max(1) as f32
                );
            }
        }
    }

    #[test]
    #[ignore="historical installed Ushiwakamaru UV4 and overlay dump required"]
    fn overtex3_uv4_clamp_hits_eye_not_cheek() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
        else {
            return;
        };
        let (_, png) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
        let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
        let (w, h) = img.dimensions();
        let dump = crate::gamefs::read_to_string(crate::test_support::game_path(r"\mesh_dump\outfit4\Ushiwakamaru_0\mesh_22_cf_O_face.json"),
        )
        .unwrap();
        let parse = |key: &str| -> Vec<[f32; 2]> {
            let s = dump.split(&format!("\"{key}\":[")).nth(1).unwrap();
            let s = s.split(']').next().unwrap();
            let mut out = Vec::new();
            for cap in s.split("},{") {
                let x = cap.split("\"x\":").nth(1).and_then(|s| s.split(',').next());
                let y = cap
                    .split("\"y\":")
                    .nth(1)
                    .and_then(|s| s.split(|c| c == '}' || c == ',').next());
                if let (Some(x), Some(y)) = (x, y) {
                    if let (Ok(u), Ok(v)) = (x.parse::<f32>(), y.parse::<f32>()) {
                        out.push([u, v]);
                    }
                }
            }
            out
        };
        let uvs = parse("uv");
        let uv4s = parse("uv4");
        let sample = |u: f32, v: f32, flip: bool| -> u8 {
            let uu = u.clamp(0.0, 0.999);
            let vv = if flip { 1.0 - v } else { v }.clamp(0.0, 0.999);
            let x = ((uu * w as f32) as u32).min(w - 1);
            let y = ((vv * h as f32) as u32).min(h - 1);
            img.get_pixel(x, y).0[3]
        };
        let mean = |pred: fn(f32, f32) -> bool, flip: bool| -> (f32, u8, usize) {
            let mut s = 0u64;
            let mut mx = 0u8;
            let mut n = 0usize;
            for ([u0, v0], [u4, v4]) in uvs.iter().zip(uv4s.iter()) {
                if !pred(*u0, *v0) {
                    continue;
                }
                let a = sample(*u4, *v4, flip);
                s += a as u64;
                mx = mx.max(a);
                n += 1;
            }
            (if n == 0 { 0.0 } else { s as f32 / n as f32 }, mx, n)
        };
        let eye = |u: f32, v: f32| {
            v > 0.55 && v < 0.82 && ((u > 0.22 && u < 0.43) || (u > 0.57 && u < 0.80))
        };
        let cheek = |u: f32, v: f32| {
            v > 0.30 && v < 0.52 && ((u > 0.15 && u < 0.40) || (u > 0.60 && u < 0.85))
        };
        let jaw = |u: f32, v: f32| v > 0.12 && v < 0.32 && u > 0.25 && u < 0.75;
        let temple = |u: f32, v: f32| {
            v > 0.70 && v < 0.90 && ((u > 0.05 && u < 0.25) || (u > 0.75 && u < 0.95))
        };
        for (name, pred) in [
            ("eye", eye as fn(f32, f32) -> bool),
            ("cheek", cheek),
            ("jaw", jaw),
            ("temple", temple),
        ] {
            let (a0, m0, n) = mean(pred, false);
            let (a1, m1, _) = mean(pred, true);
            eprintln!("{name} n={n} clamp_y=v meanA={a0:.1}/{m0}  y=1-v meanA={a1:.1}/{m1}");
        }
        let (eye0, _, n_eye) = mean(eye, false);
        let (eye1, _, _) = mean(eye, true);
        let (cheek0, _, n_cheek) = mean(cheek, false);
        let (cheek1, _, _) = mean(cheek, true);
        let (jaw1, _, _) = mean(jaw, true);
        assert!(n_eye > 10 && n_cheek > 10);
        assert!(
            cheek0 > 5.0,
            "sanity: PNG y=v*h clamps cheek uv4 onto 00f blobs (got {cheek0})"
        );
        assert!(eye1 > 40.0, "lid alpha PNG y=1-v={eye1}");
        assert!(
            cheek1 < 5.0,
            "cheek must miss 00f with Unity V (PNG y=1-v), got {cheek1}"
        );
        assert!(jaw1 < 5.0, "jaw must miss 00f with Unity V, got {jaw1}");
        let _ = eye0;
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn overlay_mapped_stains_lids_not_cheeks() {
        let zip = PathBuf::from(crate::test_support::game_path(r"\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\[moderchan]charamaker support MOD v1.1.zipmod"),
        );
        let Some(bytes) = crate::mods::zipmod_bundle_named(&zip, "chara/_kero/CMS_NT_00.unity3d")
        else {
            return;
        };
        let (_, over) = named_png_from_bytes(bytes, &["cw_t_eyeshadow_00f"]).expect("00f");
        let base_path = PathBuf::from(crate::test_support::game_path(r"\mesh_dump\outfit0\Ushiwakamaru_0\tex_2_cf_m_face_00__MainTex.png"),
        );
        if !base_path.ok_is_file() {
            return;
        }
        let base = crate::gamefs::read(&base_path).unwrap();
        let head = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
        let meshes = crate::unity::meshes_from_bundle_path(&head);
        let face = meshes
            .iter()
            .find(|m| m.name.eq_ignore_ascii_case("cf_O_face"))
            .expect("cf_O_face");
        let out = overlay_overtex_mapped(
            &base,
            &over,
            [0.529, 0.230, 0.230, 0.65],
            &face.uvs,
            &face.uv4,
            &face.indices,
        )
        .expect("overlay");
        let dst = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
        let src = crate::teximg::load_from_memory(&base).unwrap().to_rgba8();
        let (w, h) = dst.dimensions();
        let pix = |img: &image::RgbaImage, u: f32, v: f32| -> [i32; 3] {
            let x = ((u.clamp(0.0, 0.999) * w as f32) as u32).min(w - 1);
            let y = (((1.0 - v).clamp(0.0, 0.999) * h as f32) as u32).min(h - 1);
            let p = img.get_pixel(x, y).0;
            [p[0] as i32, p[1] as i32, p[2] as i32]
        };
        let mut eye_d = 0.0f32;
        let mut cheek_d = 0.0f32;
        let mut n_eye = 0u32;
        let mut n_cheek = 0u32;
        for [u, v] in &face.uvs {
            let is_eye = *v > 0.55
                && *v < 0.82
                && ((*u > 0.22 && *u < 0.43) || (*u > 0.57 && *u < 0.80));
            let is_cheek = *v > 0.30
                && *v < 0.52
                && ((*u > 0.15 && *u < 0.40) || (*u > 0.60 && *u < 0.85));
            if !is_eye && !is_cheek {
                continue;
            }
            let a = pix(&src, *u, *v);
            let b = pix(&dst, *u, *v);
            let d = (a[0] - b[0]).abs() + (a[1] - b[1]).abs() + (a[2] - b[2]).abs();
            if is_eye {
                eye_d += d as f32;
                n_eye += 1;
            }
            if is_cheek {
                cheek_d += d as f32;
                n_cheek += 1;
            }
        }
        eye_d /= n_eye.max(1) as f32;
        cheek_d /= n_cheek.max(1) as f32;
        eprintln!("OVERLAY eye_d={eye_d:.1} n={n_eye} cheek_d={cheek_d:.1} n={n_cheek}");
        assert!(n_eye > 10 && n_cheek > 10);
        assert!(eye_d > 20.0, "lids must pick up 00f, eye_d={eye_d}");
        assert!(
            cheek_d * 5.0 < eye_d,
            "cheeks must stay far below lids (Unity Clamp + V), eye_d={eye_d} cheek_d={cheek_d}"
        );
    }

    #[test]
    #[ignore]
    fn diag_hair_f_118_renderer_enabled() {
        let path = PathBuf::from(
            r"C:\Users\Damody\AppData\Local\Temp\opencode\hairf50\abdata\chara\deathweasel\acc_bo_hair_f_50.unity3d",
        );
        if !path.ok_is_file() {
            eprintln!("skip missing {}", path.display());
            return;
        }
        let bundle = load_bundle(&path).expect("bundle");
        let _ = bundle.data_checked();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            let mut go_name = std::collections::HashMap::new();
            let mut go_active = std::collections::HashMap::new();
            for info in &serialized.objects {
                if info.type_id != 1 {
                    continue;
                }
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                go_name.insert(info.path_id, obj.name().unwrap_or_default());
                if let Some(v) = obj.class.properties().get("m_IsActive") {
                    go_active.insert(info.path_id, uv_bool(v));
                }
            }
            let mut scripts = std::collections::HashMap::new();
            for info in &serialized.objects {
                if info.type_id != 115 {
                    continue;
                }
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                scripts.insert(info.path_id, obj.name().unwrap_or_default());
            }
            for info in &serialized.objects {
                if info.type_id != 114 {
                    continue;
                }
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                let props = obj.class.properties();
                let class = props
                    .get("m_Script")
                    .and_then(pptr_path_id)
                    .and_then(|id| scripts.get(&id))
                    .cloned()
                    .unwrap_or_default();
                if class.is_empty() {
                    continue;
                }
                let keys: Vec<String> = props.keys().cloned().collect();
                eprintln!("MB class={class} keys={keys:?}");
            }
            for info in &serialized.objects {
                if info.type_id != 137 && info.type_id != 23 {
                    continue;
                }
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                let props = obj.class.properties();
                let go = props.get("m_GameObject").and_then(pptr_path_id);
                let name = go.and_then(|id| go_name.get(&id)).cloned().unwrap_or_default();
                if !name.to_ascii_lowercase().contains("hair_f_118") && !name.to_ascii_lowercase().contains("hairfront") {
                    continue;
                }
                let en = props.get("m_Enabled").map(uv_bool);
                let active = go.and_then(|id| go_active.get(&id).copied());
                let keys: Vec<&str> = props.keys().map(|s| s.as_str()).collect();
                eprintln!(
                    "SMR type={} go={name} enabled={en:?} go_active={active:?} keys={keys:?}",
                    info.type_id
                );
            }
        }
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_body_skeleton_and_bindpose() {
        let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
        let bundle = load_bundle(&path).expect("bundle");
        let _ = bundle.data_checked();
        let mut lines = Vec::new();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            for info in serialized.objects_of_type(1) {
                if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                    let n = obj.name().unwrap_or_default();
                    if n.contains("cf_j_arm") || n.contains("cf_j_head") || n.contains("cf_j_foot") || n.contains("N_") || n.contains("n_hair") || n == "cf_n_height" || n.contains("cf_j_hips") {
                        lines.push(format!("GO {} {}", info.path_id, n));
                    }
                }
            }
            for info in serialized.objects_of_type(4) {
                if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                    let keys: Vec<_> = obj.class.properties().keys().cloned().collect();
                    if lines.len() < 5 {
                        lines.push(format!("TFkeys={keys:?}"));
                    }
                    break;
                }
            }
            for info in serialized.objects_of_type(137) {
                if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                    let bones = obj.class.properties().get("m_Bones").map(|v| v.to_string().chars().take(300).collect::<String>());
                    let root = obj.class.properties().get("m_RootBone").map(|v| v.to_string().chars().take(80).collect::<String>());
                    lines.push(format!("SMR {} bones={:?} root={:?}", info.path_id, bones, root));
                }
            }
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
                let mesh = match parse_mesh(&object, &version()) {
                    Ok(m) => m,
                    Err(_) => continue,
                };
                if mesh.name != "o_body_a" {
                    continue;
                }
                lines.push(format!(
                    "mesh {} bind={} hashes={} rootHash={}",
                    mesh.name,
                    mesh.bind_pose.len(),
                    mesh.bone_name_hashes.len(),
                    mesh.root_bone_name_hash
                ));
                if let Some(bp) = object.class.properties().get("m_BindPose") {
                    lines.push(format!("bindkind={} preview={}", 
                        match bp { UnityValue::Array(_) => "arr", UnityValue::Object(_) => "obj", _ => "other" },
                        bp.to_string().chars().take(400).collect::<String>()));
                }
                if let Some(h) = object.class.properties().get("m_BoneNameHashes") {
                    lines.push(format!("hashes={}", h.to_string().chars().take(200).collect::<String>()));
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\skeleton.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
        assert!(!lines.is_empty());
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_avatar_tos_and_clip_curves() {
        let mut lines = Vec::new();
        for (label, p) in [
            ("pose", crate::test_support::game_path(r"\abdata\custom\custompose.unity3d")),
            ("body", crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d")),
        ] {
            let Ok(bundle) = load_bundle(&p) else { continue };
            let _ = bundle.data_checked();
            for file in &bundle.files {
                let Ok(fb) = bundle.extract_file_data(file) else { continue };
                let Ok(serialized) = parse_serialized_file(fb) else { continue };
                for info in &serialized.objects {
                    let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                    let n = obj.name().unwrap_or_default();
                    // 90 Avatar, 95 Animator
                    if info.type_id == 90 || info.type_id == 95 {
                        let keys: Vec<_> = obj.class.properties().keys().cloned().take(20).collect();
                        lines.push(format!("{label} type{} {} keys={keys:?}", info.type_id, n));
                        if let Some(tos) = obj.class.properties().get("m_TOS") {
                            let s = tos.to_string();
                            lines.push(format!("  TOS len={} {}", s.len(), s.chars().take(2500).collect::<String>()));
                        }
                        if let Some(av) = obj.class.properties().get("m_Avatar") {
                            lines.push(format!("  m_Avatar={}", av.to_string().chars().take(200).collect::<String>()));
                        }
                    }
                    if info.type_id == 74 && n.eq_ignore_ascii_case("Fmannequin_00") {
                        if let Some(UnityValue::Object(mc)) = obj.class.properties().get("m_MuscleClip") {
                            if let Some(clip) = mc.get("m_Clip") {
                                if let UnityValue::Object(co) = clip {
                                    let inner = co.get("data").unwrap_or(clip);
                                    if let UnityValue::Object(d) = inner {
                                        if let Some(UnityValue::Object(sc)) = d.get("m_StreamedClip") {
                                            let curve = sc.get("curveCount").map(|v| v.to_string()).unwrap_or_default();
                                            let n = if let Some(UnityValue::Array(a)) = sc.get("data") { a.len() } else { 0 };
                                            lines.push(format!("streamed curveCount={curve} data_n={n}"));
                                        }
                                        if let Some(UnityValue::Object(dc)) = d.get("m_DenseClip") {
                                            lines.push(format!("dense keys={:?} preview={}", dc.keys().cloned().collect::<Vec<_>>(), dc.get("m_SampleArray").map(|v| v.to_string().chars().take(200).collect::<String>()).unwrap_or_default()));
                                        }
                                        if let Some(cc) = d.get("m_ConstantClip") {
                                            lines.push(format!("const preview={}", cc.to_string().chars().take(400).collect::<String>()));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\avatar_tos.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
        assert!(!lines.is_empty());
    }

    #[test]
    fn dump_custom_pose_clips() {
        let path = crate::test_support::game_path(r"\abdata\custom\custompose.unity3d");
        if !path.ok_exists() {
            return;
        }
        let Ok(bundle) = load_bundle(&path) else {
            return;
        };
        let _ = bundle.data_checked();
        let mut lines = Vec::new();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            lines.push(format!("file {} objects={}", file.name, serialized.objects.len()));
            for info in &serialized.objects {
                let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
                let n = obj.name().unwrap_or_default();
                if info.type_id == 91 {
                    if let Some(tos) = obj.class.properties().get("m_TOS") {
                        let s = tos.to_string();
                        lines.push(format!("TOS type91 name={n} len={} preview={}", s.len(), s.chars().take(4000).collect::<String>()));
                    }
                }
                if info.type_id == 74 && n.eq_ignore_ascii_case("Fmannequin_00") {
                    let keys: Vec<_> = obj.class.properties().keys().cloned().take(40).collect();
                    lines.push(format!("id={} type={} name={} keys={keys:?}", info.path_id, info.type_id, n));
                    if info.type_id == 74 {
                        for k in ["m_Name", "m_SampleRate", "m_Legacy", "m_AnimationType"] {
                            if let Some(v) = obj.class.properties().get(k) {
                                lines.push(format!("  {k}={}", v.to_string().chars().take(80).collect::<String>()));
                            }
                        }
                        if let Some(bind) = obj.class.properties().get("m_ClipBindingConstant") {
                            if let unity_asset_core::UnityValue::Object(o) = bind {
                                if let Some(unity_asset_core::UnityValue::Array(g)) = o.get("genericBindings") {
                                    lines.push(format!("  bindings={}", g.len()));
                                    if let Some(unity_asset_core::UnityValue::Array(pp)) = o.get("pptrCurveMapping") {
                                        lines.push(format!("  pptrCurveMapping={}", pp.len()));
                                    }
                                }
                            }
                        }
                        if let Some(mc) = obj.class.properties().get("m_MuscleClip") {
                            if let unity_asset_core::UnityValue::Object(o) = mc {
                                lines.push(format!("  muscle_keys={:?}", o.keys().cloned().collect::<Vec<_>>()));
                                if let Some(clip) = o.get("m_Clip") {
                                    let s = clip.to_string();
                                    lines.push(format!("  m_Clip preview={}", s.chars().take(2000).collect::<String>()));
                                }
                            }
                        }
                    }
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\custom_pose.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
        assert!(!lines.is_empty());
    }

    #[test]
    fn body_vertex_stream_offsets_match_unity_buffer() {
        let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
        if !path.ok_exists() { return; }
        let bundle = unity_asset_binary::bundle::load_bundle(&path).expect("bundle");
        for file in &bundle.files {
            let Ok(bytes) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(bytes) else { continue };
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
                let Ok(mesh) = parse_mesh(&object, &version()) else { continue };
                if mesh.name != "o_body_a" || mesh.vertex_data.vertex_count != 6823 { continue }
                let (strides, offsets) = channel_layout(&mesh);
                assert_eq!(strides, [40, 28, 32]);
                assert_eq!(offsets, [0, 272928, 463984]);
                assert_eq!(offsets[2] + strides[2] * 6823, mesh.vertex_data.data_size.len());
                return;
            }
        }
        panic!("reference body mesh missing");
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn body_obj_has_nonzero_vertices() {
        let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
        let meshes = meshes_from_bundle_path(&path);
        let body = pick_named(&meshes, "o_body_a").expect("o_body_a");
        let has = body.obj.lines().any(|l| {
            if !l.starts_with("v ") {
                return false;
            }
            l.split_whitespace().skip(1).any(|n| n.parse::<f32>().ok().map(|f| f.abs() > 0.001).unwrap_or(false))
        });
        assert!(has, "body vertices still zero: {}", body.obj.lines().take(8).collect::<Vec<_>>().join("\n"));
        assert!(body.obj.contains("f "), "no faces");
        assert!(
            body.texture_png.as_ref().map(|p| p.len()).unwrap_or(0) > 64,
            "body texture missing name={}",
            body.texture_name
        );
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn head_texture_unpacked_from_red_atlas() {
        let path = crate::test_support::game_path(r"\abdata\chara\bo_head_00.unity3d");
        let meshes = meshes_from_bundle_path(&path);
        let head = pick_named(&meshes, "cf_O_face").expect("cf_O_face");
        let png = head.texture_png.as_ref().expect("head texture");
        let img = crate::teximg::load_from_memory(png).expect("png").to_rgba8();
        let mut rsum = 0u64;
        let mut gsum = 0u64;
        let mut n = 0u64;
        for p in img.pixels() {
            if p[0] < 16 && p[1] < 16 && p[2] < 16 {
                continue;
            }
            rsum += p[0] as u64;
            gsum += p[1] as u64;
            n += 1;
        }
        assert!(n > 0, "head tex empty");
        assert!(
            gsum * 2 > rsum,
            "head still packed red r={} g={}",
            rsum / n,
            gsum / n
        );
    }

    #[test]
    fn unpack_red_packed_atlas() {
        let mut img = image::RgbaImage::new(8, 8);
        for (x, y, p) in img.enumerate_pixels_mut() {
            *p = image::Rgba([160 + 80 * ((x + y) % 2) as u8, 0, 0, 255]);
        }
        // Flat red artwork is deliberately rejected by the existing packed
        // atlas detector; it must not be recoloured by this legacy helper.
        let flat = solid_png([200.0/255.0, 0.0, 0.0, 1.0], 8, 8).unwrap();
        assert!(unpack_illusion_albedo(&flat).is_none());
        let mut png = Vec::new();
        image::DynamicImage::ImageRgba8(img)
            .write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png)
            .unwrap();
        let out = unpack_illusion_albedo(&png).expect("unpack");
        let decoded = crate::teximg::load_from_memory(&out).unwrap().to_rgba8();
        let p = decoded.get_pixel(0, 0);
        assert!(p[1] > 100, "g={}", p[1]);
        assert!(p[2] > 80, "b={}", p[2]);
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_body_vertex_layout() {
        let path = crate::test_support::game_path(r"\abdata\chara\oo_base.unity3d");
        let bundle = unity_asset_binary::bundle::load_bundle(&path).expect("bundle");
        let _ = bundle.data_checked();
        let mut lines = Vec::new();
        for file in &bundle.files {
            let Ok(bytes) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(bytes) else { continue };
            for info in serialized.objects_of_type(MESH_CLASS_ID) {
                let Ok(object) = UnityObject::from_serialized_file(&serialized, &info) else { continue };
                let keys: Vec<_> = object.class.properties().keys().cloned().collect();
                let mesh = parse_mesh(&object, &version()).ok();
                let Some(mesh) = mesh else { continue };
                if mesh.name != "o_body_a" && mesh.name != "cf_O_face" {
                    continue;
                }
                lines.push(format!(
                    "name={} vtx={} idx={} ch={} data={} comp={} keys={:?}",
                    mesh.name,
                    mesh.vertex_data.vertex_count,
                    mesh.index_buffer.len(),
                    mesh.vertex_data.channels.len(),
                    mesh.vertex_data.data_size.len(),
                    mesh.mesh_compression,
                    keys
                ));
                for (i, ch) in mesh.vertex_data.channels.iter().enumerate() {
                    lines.push(format!(
                        "  ch{i} stream={} off={} fmt={} dim={}",
                        ch.stream, ch.offset, ch.format, ch.dimension
                    ));
                }
                if let Some(UnityValue::Object(vd)) = object.class.properties().get("m_VertexData") {
                    let vkeys: Vec<_> = vd.keys().cloned().collect();
                    lines.push(format!("  vkeys={vkeys:?}"));
                    if let Some(st) = vd.get("m_Streams") {
                        lines.push(format!("  streams={}", st.to_string().chars().take(400).collect::<String>()));
                    }
                }
                if let Some(UnityValue::Object(comp)) = object.class.properties().get("m_CompressedMesh") {
                    let nums: Vec<String> = comp
                        .iter()
                        .map(|(k, v)| match v {
                            UnityValue::Object(o) => {
                                let n = o.get("m_NumItems").and_then(|x| x.as_i64()).unwrap_or(0);
                                format!("{k}.n={n}")
                            }
                            _ => format!("{k}"),
                        })
                        .collect();
                    lines.push(format!("  compressed {}", nums.join(" ")));
                }
                if let Some(shapes) = object.class.properties().get("m_Shapes") {
                    lines.push(format!("  shapes={}", shapes.to_string().chars().take(400).collect::<String>()));
                }
                if mesh.name == "o_body_a" && mesh.vertex_data.vertex_count == 6823 {
                    let data = &mesh.vertex_data.data_size;
                    let n = mesh.vertex_data.vertex_count as usize;
                    lines.push(format!("  nbytes={} per={}", data.len(), if n>0 { data.len()/n } else { 0 }));
                    if let Some(vw) = object.class.properties().get("m_VariableBoneCountWeights") {
                        lines.push(format!("  varbones={}", vw.to_string().chars().take(300).collect::<String>()));
                    }
                    // guess stream2 at 68*n (40+28) and 72*n (40+32)
                    let (strides, stream_off) = channel_layout(&mesh);
                    lines.push(format!("  computed strides={strides:?} soff={stream_off:?}"));
                    let s2 = stream_off.get(2).copied().unwrap_or(0);
                    let st = strides.get(2).copied().unwrap_or(32);
                    for vi in [0usize, 10, 100, 500, 2144, 3000, 5000, 6822] {
                        let off = s2 + vi * st;
                        if off + 32 > data.len() {
                            continue;
                        }
                        let b = &data[off..off + 32];
                        let mut idxs = Vec::new();
                        let mut ws = Vec::new();
                        for i in 0..8 {
                            let u = u32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap());
                            let f = f32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap());
                            if u < 128 {
                                idxs.push(u);
                            } else if f > 0.02 && f <= 1.02 {
                                ws.push(f);
                            }
                        }
                        let sum: f32 = ws.iter().sum();
                        lines.push(format!("  heur v{vi} idx={idxs:?} w={ws:?} sum={sum:.3}"));
                    }
                    for (label, base) in [
                        ("v0_s2", stream_off.get(2).copied().unwrap_or(0)),
                        ("v0_plus4", stream_off.get(2).copied().unwrap_or(0).saturating_add(4)),
                        ("v2144_s2", stream_off.get(2).copied().unwrap_or(0) + 2144 * strides.get(2).copied().unwrap_or(32)),
                    ] {
                        if base + 32 <= data.len() {
                            let b = &data[base..base+32];
                            let f: Vec<f32> = (0..8).map(|i| f32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap())).collect();
                            let u: Vec<u32> = (0..8).map(|i| u32::from_le_bytes(b[i*4..i*4+4].try_into().unwrap())).collect();
                            lines.push(format!("  {label} @{base} f32={f:?} u32={u:?}"));
                        }
                    }
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\mesh_layout.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
        let mut texn = 0usize;
        let mut file_bytes = Vec::new();
        for file in &bundle.files {
            let Ok(bytes) = bundle.extract_file_data(file) else { continue };
            file_bytes.push((file.name.clone(), bytes));
        }
        for (_name, bytes) in &file_bytes {
            let Ok(serialized) = parse_serialized_file(bytes.clone()) else { continue };
            texn += textures_from_serialized(&serialized, &file_bytes, false).len();
        }
        lines.push(format!("textures_decoded={texn}"));
        eprintln!("textures_decoded={texn}");
        assert!(!lines.is_empty());
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn dump_go_and_renderers() {
        let path = crate::test_support::game_path(r"\mods\Sideloader Modpack\yamadamod\[yamadamod]hairNicoichi rev4.zipmod");
        if !path.ok_exists() {
            return;
        }
        let bytes = crate::mods::zipmod_bundle_named(
            &path,
            "chara/ymd/mf_hairNicoichi.unity3d",
        )
        .expect("bundle");
        let bundle = load_bundle_unbounded(bytes.to_vec()).expect("load");
        let _ = bundle.data_checked();
        let mut lines = Vec::new();
        for file in &bundle.files {
            let Ok(fb) = bundle.extract_file_data(file) else { continue };
            let Ok(serialized) = parse_serialized_file(fb) else { continue };
            let mut ids = std::collections::BTreeMap::<i32, usize>::new();
            for info in &serialized.objects {
                *ids.entry(info.type_id).or_insert(0) += 1;
            }
            lines.push(format!("class_ids={ids:?}"));
            for info in serialized.objects_of_type(1) {
                if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                    let name = obj.name().unwrap_or_default();
                    let keys: Vec<_> = obj.class.properties().keys().cloned().collect();
                    lines.push(format!("GO path={} name={name} keys={keys:?}", info.path_id));
                }
            }
            for cid in [23i32, 137, 33] {
                for info in serialized.objects_of_type(cid) {
                    if let Ok(obj) = UnityObject::from_serialized_file(&serialized, &info) {
                        let keys: Vec<_> = obj.class.properties().keys().cloned().collect();
                        lines.push(format!(
                            "R cid={cid} path={} keys={keys:?} mesh={:?}",
                            info.path_id,
                            obj.class.properties().get("m_Mesh").map(|v| v.to_string().chars().take(80).collect::<String>())
                        ));
                    }
                }
            }
        }
        std::fs::write(r"C:\portable\OpenKoikatsu\Saved\spike\go_dump.txt", lines.join("\n")).ok();
        eprintln!("{}", lines.join("\n"));
    }
}

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
    use std::io::Cursor;
    let mut img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let factor = factor.clamp(0.0, 1.0);
    for p in img.pixels_mut() {
        p[3] = ((p[3] as f32) * factor).round() as u8;
    }
    crate::teximg::encode_rgba_owned(img)
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
                        let Ok(o) = UnityObject::from_serialized_file(&ser, &info) else { continue };
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
            let Ok(object) = UnityObject::from_serialized_file(file, info) else { continue };
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
