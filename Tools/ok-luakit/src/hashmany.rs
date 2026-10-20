//! hash-many: digest many files in one process (Lua scripts that verify hundreds of voice clips).
use crate::{arg, read_input, Res};
use serde_json::Value;

/// `hash-many sha256|md5 <list>`: one path per line (UTF-8; "-" reads stdin).
/// Returns an array of hex digests in the same order; null where a file cannot be read.
pub fn hash_many(args: &[String]) -> Res<Value> {
    let algo = arg(args, 0, "algo")?;
    let list = arg(args, 1, "list")?;
    if algo != "sha256" && algo != "md5" {
        return Err(format!("unknown hash {algo}"));
    }
    let text = String::from_utf8(read_input(list)?).map_err(|e| format!("{list}: {e}"))?;
    let mut out = Vec::new();
    for line in text.lines() {
        let path = line.trim_end_matches('\r');
        if path.is_empty() {
            continue;
        }
        match crate::codec::hash(&[algo.to_string(), path.to_string()]) {
            Ok(v) => out.push(v.get("digest").cloned().unwrap_or(Value::Null)),
            Err(_) => out.push(Value::Null),
        }
    }
    Ok(Value::Array(out))
}
