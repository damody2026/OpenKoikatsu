//! Raw byte access the shader / animation export scripts need beyond
//! `unity-dump`: one zip member as bytes, and the serialized bytes of a Unity
//! object (TextAsset scripts with binary payloads are not valid UTF-8 strings,
//! so the typetree dump cannot carry them).
use crate::{arg, opt, write_output, Res};
use serde_json::{json, Value};
use std::io::Read;
use unity_asset_binary::asset::parse_serialized_file;

pub fn run(cmd: &str, args: &[String]) -> Res<Value> {
    match cmd {
        "zip-member" => zip_member(args),
        "unity-raw" => unity_raw(args),
        "unity-stream" => unity_stream(args),
        "zip-names" => zip_names(args),
        "unity-fields" => unity_fields(args),
        _ => unity_version(args),
    }
}

fn value_json(v: &unity_asset_core::UnityValue) -> Value {
    use unity_asset_core::UnityValue as U;
    match v {
        U::Null => Value::Null,
        U::Bool(b) => Value::Bool(*b),
        U::Integer(i) => json!(i),
        U::Float(f) => serde_json::Number::from_f64(*f).map(Value::Number).unwrap_or_else(|| {
            json!({ "$float": if f.is_nan() { "NaN" } else if *f > 0.0 { "Infinity" } else { "-Infinity" } })
        }),
        U::String(s) => Value::String(s.clone()),
        U::Array(a) => Value::Array(a.iter().map(value_json).collect()),
        U::Bytes(b) => json!({ "$bytes": b.iter().map(|x| format!("{x:02x}")).collect::<String>() }),
        U::Object(o) => {
            let mut m = serde_json::Map::new();
            for (k, x) in o { m.insert(k.clone(), value_json(x)); }
            Value::Object(m)
        }
    }
}

/// Copy `path` (a.b.c) from `src` into the pruned tree `dst`, keeping key order.
fn prune_into(dst: &mut serde_json::Map<String, Value>, src: &indexmap::IndexMap<String, unity_asset_core::UnityValue>, path: &[&str]) {
    use unity_asset_core::UnityValue as U;
    if path.len() == 1 {
        // "name#len" stores len(value) under "name#len" instead of the value itself.
        if let Some(base) = path[0].strip_suffix("#len") {
            if let Some(v) = src.get(base) {
                let n = match v { U::Array(a) => a.len(), U::Bytes(b) => b.len(), U::Object(o) => o.len(), U::String(s) => s.chars().count(), _ => return };
                dst.insert(path[0].to_string(), json!(n));
            }
            return;
        }
    }
    let Some(v) = src.get(path[0]) else { return; };
    if path.len() == 1 { dst.insert(path[0].to_string(), value_json(v)); return; }
    if let U::Object(o) = v {
        let entry = dst.entry(path[0].to_string()).or_insert_with(|| Value::Object(serde_json::Map::new()));
        if let Value::Object(m) = entry { prune_into(m, o, &path[1..]); }
    }
}

/// unity-fields <file> --type T --fields a,b.c[,...] [--name N]
/// -> [{path_id, class_id, type, name, tree}] like unity-dump, with the tree pruned to
/// the listed dotted paths (missing paths are left out). Keeps large typetrees such as
/// AnimationClip curve data out of the JSON when a script needs only a few fields.
pub fn unity_fields(args: &[String]) -> Res<Value> {
    use unity_asset_binary::object::UnityObject;
    let path = arg(args, 0, "file")?;
    let want_type = opt(args, "--type").ok_or("missing --type")?;
    let want_name = opt(args, "--name");
    let fields: Vec<Vec<&str>> = opt(args, "--fields").ok_or("missing --fields")?.split(',').filter(|s| !s.is_empty()).map(|s| s.split('.').collect()).collect();
    let mut out = Vec::new();
    for fb in serialized_files(path)? {
        let Ok(serialized) = parse_serialized_file(fb) else { continue; };
        for info in &serialized.objects {
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue; };
            if obj.class_name() != want_type && format!("Class_{}", info.type_id) != want_type { continue; }
            let props = obj.class.properties();
            let name = props.get("m_Name").and_then(|v| v.as_str()).map(|s| s.to_string());
            if let Some(n) = want_name { if name.as_deref() != Some(n) { continue; } }
            let mut tree = serde_json::Map::new();
            for f in &fields { prune_into(&mut tree, props, f); }
            out.push(json!({ "path_id": info.path_id, "class_id": info.type_id, "type": obj.class_name(), "name": name, "tree": Value::Object(tree) }));
        }
    }
    Ok(Value::Array(out))
}

fn u16le(d: &[u8], i: usize) -> Res<usize> { d.get(i..i + 2).map(|b| u16::from_le_bytes([b[0], b[1]]) as usize).ok_or_else(|| "truncated zip".into()) }
fn u32le(d: &[u8], i: usize) -> Res<usize> { d.get(i..i + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as usize).ok_or_else(|| "truncated zip".into()) }

fn cp437(bytes: &[u8]) -> String {
    const HIGH: &str = "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■\u{a0}";
    let high: Vec<char> = HIGH.chars().collect();
    bytes.iter().map(|&b| if b < 128 { b as char } else { high[(b - 128) as usize] }).collect()
}

/// zip-member <zip> <member> <out> -> {bytes}
/// Python `ZipFile(zip).read(member)`: names decode via the UTF-8 flag or cp437;
/// stored and deflated members; KeyError text when the member is absent.
pub fn zip_member(args: &[String]) -> Res<Value> {
    use std::io::{Seek, SeekFrom};
    let path = arg(args, 0, "zip")?;
    let member = arg(args, 1, "member")?;
    let out = arg(args, 2, "out")?;
    let (mut f, cd, count) = open_central(path)?;
    let mut pos = 0usize;
    // Python keeps the last central-directory entry for duplicate names.
    let mut found = None;
    for _ in 0..count {
        if cd.get(pos..pos + 4) != Some(&PK12[..]) { return Err("Bad magic number for central directory".into()); }
        let flags = u16le(&cd, pos + 8)?;
        let method = u16le(&cd, pos + 10)?;
        let csize = u32le(&cd, pos + 20)?;
        let nlen = u16le(&cd, pos + 28)?;
        let xlen = u16le(&cd, pos + 30)?;
        let clen = u16le(&cd, pos + 32)?;
        let local = u32le(&cd, pos + 42)?;
        let raw = cd.get(pos + 46..pos + 46 + nlen).ok_or("truncated zip")?;
        let name = if flags & 0x800 != 0 { String::from_utf8_lossy(raw).into_owned() } else { cp437(raw) };
        if name == member { found = Some((method, csize, local)); }
        pos += 46 + nlen + xlen + clen;
    }
    let (method, csize, local) = found.ok_or_else(|| format!("\"There is no item named '{member}' in the archive\""))?;
    let mut header = [0u8; 30];
    f.seek(SeekFrom::Start(local as u64)).map_err(|e| format!("{path}: {e}"))?;
    f.read_exact(&mut header).map_err(|e| format!("{path}: {e}"))?;
    if header[..4] != PK34[..] { return Err("Bad magic number for file header".into()); }
    let skip = u16le(&header, 26)? + u16le(&header, 28)?;
    f.seek(SeekFrom::Start((local + 30 + skip) as u64)).map_err(|e| format!("{path}: {e}"))?;
    let mut comp = vec![0u8; csize];
    f.read_exact(&mut comp).map_err(|_| "truncated zip member")?;
    let bytes = match method {
        0 => comp,
        8 => {
            let mut v = Vec::new();
            flate2::read::DeflateDecoder::new(&comp[..]).read_to_end(&mut v).map_err(|e| format!("{member}: {e}"))?;
            v
        }
        m => return Err(format!("compression method {m} not supported: {member}")),
    };
    write_output(out, &bytes)?;
    Ok(json!({ "bytes": bytes.len() }))
}

const PK12: [u8; 4] = [0x50, 0x4b, 0x01, 0x02];
const PK34: [u8; 4] = [0x50, 0x4b, 0x03, 0x04];
const PK56: [u8; 4] = [0x50, 0x4b, 0x05, 0x06];

/// Open a zip and read only its tail and central directory: (file, directory bytes, entry count).
fn open_central(p: &str) -> Res<(std::fs::File, Vec<u8>, usize)> {
    use std::io::{Seek, SeekFrom};
    let mut f = std::fs::File::open(p).map_err(|e| format!("{p}: {e}"))?;
    let len = f.metadata().map_err(|e| format!("{p}: {e}"))?.len() as usize;
    let tail_len = len.min(22 + 65535);
    let mut tail = vec![0u8; tail_len];
    f.seek(SeekFrom::Start((len - tail_len) as u64)).map_err(|e| format!("{p}: {e}"))?;
    f.read_exact(&mut tail).map_err(|e| format!("{p}: {e}"))?;
    let eocd = (0..tail.len().saturating_sub(21)).rev().find(|&i| tail[i..i + 4] == PK56).ok_or("File is not a zip file")?;
    let count = u16le(&tail, eocd + 10)?;
    let size = u32le(&tail, eocd + 12)?;
    let offset = u32le(&tail, eocd + 16)?;
    let mut cd = vec![0u8; size];
    f.seek(SeekFrom::Start(offset as u64)).map_err(|e| format!("{p}: {e}"))?;
    f.read_exact(&mut cd).map_err(|e| format!("{p}: {e}"))?;
    Ok((f, cd, count))
}

fn is_bundle(data: &[u8]) -> bool {
    data.starts_with(b"UnityFS") || data.starts_with(b"UnityWeb") || data.starts_with(b"UnityRaw") || data.starts_with(b"UnityArchive")
}

fn serialized_files(path: &str) -> Res<Vec<Vec<u8>>> {
    let data = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    if !is_bundle(&data) { return Ok(vec![data]); }
    let mut opts = unity_asset_binary::bundle::BundleLoadOptions::default();
    opts.load_assets = true;
    opts.decompress_blocks = true;
    opts.max_memory = None;
    opts.max_unityfs_block_cache_memory = Some(16 * 1024 * 1024 * 1024);
    opts.max_compressed_block_size = Some(16 * 1024 * 1024 * 1024);
    let bundle = unity_asset_binary::bundle::BundleParser::from_bytes_with_options(data, opts).map_err(|e| format!("bundle: {e}"))?;
    let mut out = Vec::new();
    for file in &bundle.files {
        if file.name.ends_with(".resS") || file.name.ends_with(".resource") { continue; }
        if let Ok(fb) = bundle.extract_file_data(file) { out.push(fb); }
    }
    Ok(out)
}

/// unity-raw <bundle|serialized file> --path-id ID <out> -> {bytes, class_id, unity_version}
/// Writes the object's serialized bytes (UnityPy `ObjectReader` data).
pub fn unity_raw(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "file")?;
    let id: i64 = opt(args, "--path-id").and_then(|v| v.parse().ok()).ok_or("missing --path-id")?;
    let out = args.last().filter(|_| args.len() >= 4).ok_or("missing <out>")?;
    for fb in serialized_files(path)? {
        let Ok(serialized) = parse_serialized_file(fb) else { continue; };
        if let Some(info) = serialized.objects.iter().find(|o| o.path_id == id) {
            let bytes = serialized.object_bytes(info).map_err(|e| format!("{e}"))?;
            write_output(out, bytes)?;
            return Ok(json!({ "bytes": bytes.len(), "class_id": info.type_id, "unity_version": serialized.unity_version }));
        }
    }
    Err(format!("object {id} not found in {path}"))
}

/// unity-version <bundle|serialized file> -> {unity_version} of the first serialized file.
pub fn unity_version(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "file")?;
    for fb in serialized_files(path)? {
        if let Ok(serialized) = parse_serialized_file(fb) {
            return Ok(json!({ "unity_version": serialized.unity_version }));
        }
    }
    Err(format!("no serialized file in {path}"))
}

/// unity-stream <bundle> <stream path> <offset> <size> <out> -> {bytes}
/// Texture2D/AudioClip `m_StreamData` payload: the bundle member named by the
/// last component of `path` ("archive:/CAB-x/CAB-y.resS"), sliced at offset/size.
pub fn unity_stream(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "bundle")?;
    let stream = arg(args, 1, "stream path")?;
    let offset: usize = arg(args, 2, "offset")?.parse().map_err(|_| "bad offset")?;
    let size: usize = arg(args, 3, "size")?.parse().map_err(|_| "bad size")?;
    let out = arg(args, 4, "out")?;
    let name = stream.rsplit('/').next().unwrap_or(stream);
    let data = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    if !is_bundle(&data) { return Err(format!("{path}: not an asset bundle")); }
    let mut opts = unity_asset_binary::bundle::BundleLoadOptions::default();
    opts.load_assets = false;
    opts.decompress_blocks = true;
    opts.max_memory = None;
    opts.max_unityfs_block_cache_memory = Some(16 * 1024 * 1024 * 1024);
    opts.max_compressed_block_size = Some(16 * 1024 * 1024 * 1024);
    let bundle = unity_asset_binary::bundle::BundleParser::from_bytes_with_options(data, opts).map_err(|e| format!("bundle: {e}"))?;
    let file = bundle.files.iter().find(|f| f.name == name).ok_or_else(|| format!("{name} not in {path}"))?;
    let bytes = bundle.extract_file_data(file).map_err(|e| format!("{e}"))?;
    let slice = bytes.get(offset..offset + size).ok_or("stream range out of bounds")?;
    write_output(out, slice)?;
    Ok(json!({ "bytes": slice.len() }))
}

fn central_names(data: &[u8]) -> Res<Vec<String>> {
    let scan_from = data.len().saturating_sub(22 + 65535);
    let eocd = (scan_from..data.len().saturating_sub(21)).rev()
        .find(|&i| data[i..i + 4] == *b"PK\x05\x06").ok_or("File is not a zip file")?;
    let count = u16le(data, eocd + 10)?;
    let mut pos = u32le(data, eocd + 16)?;
    let mut names = Vec::with_capacity(count);
    for _ in 0..count {
        if data.get(pos..pos + 4) != Some(b"PK\x01\x02") { return Err("Bad magic number for central directory".into()); }
        let flags = u16le(data, pos + 8)?;
        let nlen = u16le(data, pos + 28)?;
        let xlen = u16le(data, pos + 30)?;
        let clen = u16le(data, pos + 32)?;
        let raw = data.get(pos + 46..pos + 46 + nlen).ok_or("truncated zip")?;
        names.push(if flags & 0x800 != 0 { String::from_utf8_lossy(raw).into_owned() } else { cp437(raw) });
        pos += 46 + nlen + xlen + clen;
    }
    Ok(names)
}

/// Names from a zip's central directory, reading only the tail and the directory.
fn read_central(p: &str) -> Res<Vec<String>> {
    use std::io::{Seek, SeekFrom};
    let mut f = std::fs::File::open(p).map_err(|e| format!("{p}: {e}"))?;
    let len = f.metadata().map_err(|e| format!("{p}: {e}"))?.len() as usize;
    let tail_len = len.min(22 + 65535);
    let mut tail = vec![0u8; tail_len];
    f.seek(SeekFrom::Start((len - tail_len) as u64)).map_err(|e| format!("{p}: {e}"))?;
    f.read_exact(&mut tail).map_err(|e| format!("{p}: {e}"))?;
    let eocd = (0..tail.len().saturating_sub(21)).rev()
        .find(|&i| tail[i..i + 4] == *b"PK\x05\x06").ok_or("File is not a zip file")?;
    let count = u16le(&tail, eocd + 10)?;
    let size = u32le(&tail, eocd + 12)?;
    let offset = u32le(&tail, eocd + 16)?;
    let mut cd = vec![0u8; size];
    f.seek(SeekFrom::Start(offset as u64)).map_err(|e| format!("{p}: {e}"))?;
    f.read_exact(&mut cd).map_err(|e| format!("{p}: {e}"))?;
    // Reuse the in-memory parser: directory followed by an EOCD pointing at offset 0.
    let mut eocd_rec = tail[eocd..eocd + 22].to_vec();
    eocd_rec[16..20].copy_from_slice(&0u32.to_le_bytes());
    cd.extend_from_slice(&eocd_rec);
    central_names(&cd)
}

/// zip-names <list file|-> -> [{path, names} | {path, error}]
/// Batch `ZipFile(path).namelist()` for one zip path per input line.
pub fn zip_names(args: &[String]) -> Res<Value> {
    let list = crate::read_input(arg(args, 0, "list")?)?;
    let text = String::from_utf8_lossy(&list);
    let mut out = Vec::new();
    for line in text.lines() {
        let p = line.trim_end_matches('\r');
        if p.is_empty() { continue; }
        let res = read_central(p);
        out.push(match res {
            Ok(names) => json!({ "path": p, "names": names }),
            Err(e) => json!({ "path": p, "error": e }),
        });
    }
    Ok(Value::Array(out))
}
