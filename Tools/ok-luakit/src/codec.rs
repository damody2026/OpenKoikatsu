//! Hashes, compression codecs and msgpack <-> JSON.
use crate::{arg, read_input, write_output, Res};
use serde_json::{json, Map, Value};
use std::io::{Read, Write};

pub fn hash(args: &[String]) -> Res<Value> {
    use sha2::Digest;
    let algo = arg(args, 0, "algo")?;
    let path = arg(args, 1, "file")?;
    let mut file = std::fs::File::open(path).map_err(|e| format!("{path}: {e}"))?;
    let mut buf = vec![0u8; 1 << 20];
    let digest = match algo {
        "sha256" => {
            let mut h = sha2::Sha256::new();
            loop {
                let n = file.read(&mut buf).map_err(|e| e.to_string())?;
                if n == 0 { break; }
                h.update(&buf[..n]);
            }
            hex(&h.finalize())
        }
        "md5" => {
            let mut h = md5::Md5::new();
            loop {
                let n = file.read(&mut buf).map_err(|e| e.to_string())?;
                if n == 0 { break; }
                h.update(&buf[..n]);
            }
            hex(&h.finalize())
        }
        other => return Err(format!("unknown hash {other}")),
    };
    Ok(json!({ "digest": digest }))
}

pub fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes { s.push_str(&format!("{b:02x}")); }
    s
}

pub fn unhex(s: &str) -> Res<Vec<u8>> {
    if s.len() % 2 != 0 { return Err("odd hex length".into()); }
    (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(|e| e.to_string())).collect()
}

/// <cmd> <in> <out> [extra]: zstd-d, zstd-c [level], lz4-block-d <size>, lz4-block-c,
/// zlib-d, zlib-c [level], gzip-d, gzip-c [level]
pub fn transform(cmd: &str, args: &[String]) -> Res<Value> {
    let input = read_input(arg(args, 0, "in")?)?;
    let out_path = arg(args, 1, "out")?;
    let extra = args.get(2).map(|s| s.as_str());
    let level = |d: i64| extra.and_then(|v| v.parse::<i64>().ok()).unwrap_or(d);
    let out: Vec<u8> = match cmd {
        "zstd-d" => zstd::decode_all(&input[..]).map_err(|e| e.to_string())?,
        "zstd-c" => zstd::encode_all(&input[..], level(3) as i32).map_err(|e| e.to_string())?,
        "lz4-block-d" => {
            let size: usize = extra.ok_or("lz4-block-d needs <size>")?.parse().map_err(|_| "bad size")?;
            lz4_flex::block::decompress(&input, size).map_err(|e| e.to_string())?
        }
        "lz4-block-c" => lz4_flex::block::compress(&input),
        "zlib-d" => {
            let mut d = flate2::read::ZlibDecoder::new(&input[..]);
            let mut v = Vec::new();
            d.read_to_end(&mut v).map_err(|e| e.to_string())?;
            v
        }
        "zlib-c" => {
            let mut e = flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::new(level(6) as u32));
            e.write_all(&input).map_err(|e| e.to_string())?;
            e.finish().map_err(|e| e.to_string())?
        }
        "gzip-d" => {
            let mut d = flate2::read::MultiGzDecoder::new(&input[..]);
            let mut v = Vec::new();
            d.read_to_end(&mut v).map_err(|e| e.to_string())?;
            v
        }
        "gzip-c" => {
            let mut e = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::new(level(9) as u32));
            e.write_all(&input).map_err(|e| e.to_string())?;
            e.finish().map_err(|e| e.to_string())?
        }
        _ => unreachable!(),
    };
    write_output(out_path, &out)?;
    Ok(json!({ "bytes": out.len() }))
}

fn mp_to_json(v: &rmpv::Value) -> Value {
    use rmpv::Value as M;
    match v {
        M::Nil => Value::Null,
        M::Boolean(b) => Value::Bool(*b),
        M::Integer(i) => {
            if let Some(n) = i.as_i64() { json!(n) } else if let Some(n) = i.as_u64() { json!(n) } else { Value::Null }
        }
        M::F32(f) => json!(*f as f64),
        M::F64(f) => json!(*f),
        M::String(s) => match s.as_str() {
            Some(t) => Value::String(t.to_string()),
            None => json!({ "$bytes": hex(s.as_bytes()) }),
        },
        M::Binary(b) => json!({ "$bytes": hex(b) }),
        M::Array(a) => Value::Array(a.iter().map(mp_to_json).collect()),
        M::Map(m) => {
            let mut o = Map::new();
            for (k, val) in m {
                let key = match k {
                    M::String(s) => s.as_str().unwrap_or("").to_string(),
                    other => other.to_string(),
                };
                o.insert(key, mp_to_json(val));
            }
            Value::Object(o)
        }
        M::Ext(t, b) => json!({ "$ext": t, "$bytes": hex(b) }),
    }
}

fn json_to_mp(v: &Value) -> Res<rmpv::Value> {
    use rmpv::Value as M;
    Ok(match v {
        Value::Null => M::Nil,
        Value::Bool(b) => M::Boolean(*b),
        Value::Number(n) => {
            if let Some(i) = n.as_i64() { M::from(i) } else if let Some(u) = n.as_u64() { M::from(u) } else { M::F64(n.as_f64().unwrap_or(0.0)) }
        }
        Value::String(s) => M::String(s.clone().into()),
        Value::Array(a) => M::Array(a.iter().map(json_to_mp).collect::<Res<Vec<_>>>()?),
        Value::Object(o) => {
            if let (Some(Value::String(h)), true) = (o.get("$bytes"), o.len() <= 2) {
                let bytes = unhex(h)?;
                if let Some(t) = o.get("$ext").and_then(|t| t.as_i64()) { M::Ext(t as i8, bytes) } else { M::Binary(bytes) }
            } else {
                M::Map(o.iter().map(|(k, v)| Ok((M::String(k.clone().into()), json_to_mp(v)?))).collect::<Res<Vec<_>>>()?)
            }
        }
    })
}

/// msgpack-json <in|-> : prints the decoded value (all top-level values as an array if several).
pub fn msgpack_json(args: &[String]) -> Res<Value> {
    let data = read_input(arg(args, 0, "in")?)?;
    let mut cur = &data[..];
    let mut values = Vec::new();
    while !cur.is_empty() {
        let v = rmpv::decode::read_value(&mut cur).map_err(|e| format!("msgpack: {e}"))?;
        values.push(mp_to_json(&v));
    }
    Ok(if values.len() == 1 { values.pop().unwrap() } else { Value::Array(values) })
}

/// json-msgpack <in.json|-> <out.msgpack>
pub fn json_msgpack(args: &[String]) -> Res<Value> {
    let data = read_input(arg(args, 0, "in")?)?;
    let v: Value = serde_json::from_slice(&data).map_err(|e| format!("json: {e}"))?;
    let mut out = Vec::new();
    rmpv::encode::write_value(&mut out, &json_to_mp(&v)?).map_err(|e| e.to_string())?;
    write_output(arg(args, 1, "out")?, &out)?;
    Ok(json!({ "bytes": out.len() }))
}
