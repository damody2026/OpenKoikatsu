//! Unity bundle / serialized-file object dumps (UnityPy `env.objects` +
//! `read_typetree()` replacement).
use crate::codec::hex;
use crate::{arg, flag, opt, Res};
use serde_json::{json, Map, Value};
use std::collections::HashMap;
use unity_asset_binary::asset::parse_serialized_file;
use unity_asset_binary::object::UnityObject;
use unity_asset_core::UnityValue;

struct Ctx {
    bytes_dir: Option<String>,
    prefix: String,
    counter: usize,
}

fn to_json(v: &UnityValue, ctx: &mut Ctx) -> Value {
    match v {
        UnityValue::Null => Value::Null,
        UnityValue::Bool(b) => Value::Bool(*b),
        UnityValue::Integer(i) => json!(i),
        // JSON has no Infinity/NaN; mark them so ok.kit restores UnityPy's float('inf'/'nan').
        UnityValue::Float(f) => serde_json::Number::from_f64(*f).map(Value::Number).unwrap_or_else(|| {
            json!({ "$float": if f.is_nan() { "NaN" } else if *f > 0.0 { "Infinity" } else { "-Infinity" } })
        }),
        UnityValue::String(s) => Value::String(s.clone()),
        UnityValue::Array(a) => Value::Array(a.iter().map(|x| to_json(x, ctx)).collect()),
        UnityValue::Bytes(b) => match &ctx.bytes_dir {
            Some(dir) if b.len() > 256 => {
                ctx.counter += 1;
                let path = format!("{dir}/{}_{}.bin", ctx.prefix, ctx.counter);
                let _ = std::fs::create_dir_all(dir);
                match std::fs::write(&path, b) {
                    Ok(()) => json!({ "$file": path, "size": b.len() }),
                    Err(_) => json!({ "$bytes": hex(b) }),
                }
            }
            _ => json!({ "$bytes": hex(b) }),
        },
        UnityValue::Object(o) => {
            let mut m = Map::new();
            for (k, x) in o { m.insert(k.clone(), to_json(x, ctx)); }
            Value::Object(m)
        }
    }
}

fn is_bundle(data: &[u8]) -> bool {
    data.starts_with(b"UnityFS") || data.starts_with(b"UnityWeb") || data.starts_with(b"UnityRaw") || data.starts_with(b"UnityArchive")
}

fn load_bundle(bytes: Vec<u8>) -> Res<unity_asset_binary::bundle::AssetBundle> {
    let mut opts = unity_asset_binary::bundle::BundleLoadOptions::default();
    opts.load_assets = true;
    opts.decompress_blocks = true;
    opts.max_memory = None;
    opts.max_unityfs_block_cache_memory = Some(16 * 1024 * 1024 * 1024);
    opts.max_compressed_blocks_info_size = Some(512 * 1024 * 1024);
    opts.max_blocks_info_size = Some(512 * 1024 * 1024);
    opts.max_legacy_directory_compressed_size = Some(512 * 1024 * 1024);
    opts.max_compressed_block_size = Some(16 * 1024 * 1024 * 1024);
    opts.max_blocks = 20_000_000;
    opts.max_nodes = 20_000_000;
    unity_asset_binary::bundle::BundleParser::from_bytes_with_options(bytes, opts).map_err(|e| format!("bundle: {e}"))
}

/// Serialized files inside the input: (name, bytes).
fn serialized_files(path: &str) -> Res<Vec<(String, Vec<u8>)>> {
    let data = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    if !is_bundle(&data) {
        let name = std::path::Path::new(path).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
        return Ok(vec![(name, data)]);
    }
    let bundle = load_bundle(data)?;
    let _ = bundle.data_checked();
    let mut out = Vec::new();
    for file in &bundle.files {
        if file.name.ends_with(".resS") || file.name.ends_with(".resource") { continue; }
        if let Ok(fb) = bundle.extract_file_data(file) { out.push((file.name.clone(), fb)); }
    }
    Ok(out)
}

fn display_name(obj: &UnityObject) -> Option<String> {
    let props = obj.class.properties();
    if let Some(UnityValue::Object(parsed)) = props.get("m_ParsedForm") {
        if let Some(n) = parsed.get("m_Name").and_then(|v| v.as_str()) { return Some(n.to_string()); }
    }
    obj.name()
}

/// unity-dump <bundle|serialized file> [--type ClassName] [--name N] [--path-id ID]
///            [--bytes-dir DIR] [--no-tree]
/// -> [{file, path_id, class_id, type, name, container, tree}]
pub fn dump(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "file")?;
    let want_type = opt(args, "--type");
    let want_name = opt(args, "--name");
    let want_id: Option<i64> = opt(args, "--path-id").and_then(|v| v.parse().ok());
    let no_tree = flag(args, "--no-tree");
    let bytes_dir = opt(args, "--bytes-dir").map(|s| s.replace('\\', "/"));
    let mut results = Vec::new();
    for (fname, fb) in serialized_files(path)? {
        let Ok(serialized) = parse_serialized_file(fb) else { continue; };
        // AssetBundle container: path_id -> asset path.
        let mut container: HashMap<i64, String> = HashMap::new();
        for info in serialized.objects_of_type(142) {
            if let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) {
                if let Some(UnityValue::Array(items)) = obj.class.properties().get("m_Container") {
                    for item in items {
                        // Pairs come as [first, second] arrays (or {first, second} objects).
                        let (first, second) = match item {
                            UnityValue::Array(p) if p.len() == 2 => (p[0].as_str().unwrap_or("").to_string(), Some(&p[1])),
                            UnityValue::Object(p) => (p.get("first").and_then(|v| v.as_str()).unwrap_or("").to_string(), p.get("second")),
                            _ => continue,
                        };
                        if let Some(UnityValue::Object(second)) = second {
                            if let Some(UnityValue::Object(asset)) = second.get("asset") {
                                if let Some(UnityValue::Integer(pid)) = asset.get("m_PathID") { container.entry(*pid).or_insert(first); }
                            }
                        }
                    }
                }
            }
        }
        for info in &serialized.objects {
            if let Some(id) = want_id { if info.path_id != id { continue; } }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue; };
            let type_name = obj.class_name().to_string();
            if let Some(t) = want_type { if t != type_name { continue; } }
            let name = display_name(&obj);
            if let Some(n) = want_name { if name.as_deref() != Some(n) { continue; } }
            let mut entry = Map::new();
            entry.insert("file".into(), json!(fname));
            entry.insert("path_id".into(), json!(info.path_id));
            entry.insert("class_id".into(), json!(info.type_id));
            entry.insert("type".into(), json!(type_name));
            entry.insert("name".into(), name.map(Value::String).unwrap_or(Value::Null));
            entry.insert("container".into(), container.get(&info.path_id).map(|c| json!(c)).unwrap_or(Value::Null));
            entry.insert("byte_size".into(), json!(info.byte_size));
            if !no_tree {
                let mut ctx = Ctx { bytes_dir: bytes_dir.clone(), prefix: format!("{}_{}", fname.replace(['/', '\\', ':'], "_"), info.path_id), counter: 0 };
                let mut tree = Map::new();
                for (k, v) in obj.class.properties() { tree.insert(k.clone(), to_json(v, &mut ctx)); }
                entry.insert("tree".into(), Value::Object(tree));
            }
            results.push(Value::Object(entry));
        }
    }
    Ok(Value::Array(results))
}
