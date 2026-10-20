//! Market-data helpers for the StreetSocial finance build scripts (Tools/build_*market*/us/ashare*.lua):
//! SQLite access, OpenCC conversion, PDF text, BaoStock's TCP protocol and one-shot zstd frames.
use crate::{arg, flag, opt, read_input, write_output, Res};
use serde_json::{json, Map, Value};
use std::collections::HashMap;
use std::io::{Read, Write};

// ------------------------------------------------------------------------------------------ zstd
/// zstd-c-sized <in> <out> [level]: single frame with the content size in the header, like
/// python-zstandard `ZstdCompressor(level).compress(data)` (same libzstd -> same bytes).
pub fn zstd_sized(args: &[String]) -> Res<Value> {
    let input = read_input(arg(args, 0, "in")?)?;
    let out = arg(args, 1, "out")?;
    let level: i32 = args.get(2).and_then(|v| v.parse().ok()).unwrap_or(3);
    let data = zstd::bulk::compress(&input, level).map_err(|e| e.to_string())?;
    write_output(out, &data)?;
    Ok(json!({ "bytes": data.len() }))
}

/// zstd-c-batch <spec.json|-> : {"level": n, "files": [{"in": raw, "out": dst}, ...]} -> [{"bytes", "sha256"}]
/// Same frames as zstd-c-sized (content size in the header), compressed on several threads.
pub fn zstd_c_batch(args: &[String]) -> Res<Value> {
    use sha2::Digest;
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| format!("spec: {e}"))?;
    let level = spec.get("level").and_then(Value::as_i64).unwrap_or(3) as i32;
    let files: Vec<(String, String)> = spec.get("files").and_then(Value::as_array).cloned().unwrap_or_default().iter()
        .map(|f| (f["in"].as_str().unwrap_or("").to_string(), f["out"].as_str().unwrap_or("").to_string())).collect();
    let n = files.len();
    let results = std::sync::Mutex::new(vec![Value::Null; n]);
    let next = std::sync::atomic::AtomicUsize::new(0);
    let threads = std::thread::available_parallelism().map(|x| x.get()).unwrap_or(4).min(16).max(1);
    let err = std::sync::Mutex::new(None::<String>);
    std::thread::scope(|s| {
        for _ in 0..threads {
            s.spawn(|| loop {
                let i = next.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                if i >= n { break; }
                let (src, dst) = &files[i];
                let r = std::fs::read(src).map_err(|e| format!("{src}: {e}"))
                    .and_then(|raw| zstd::bulk::compress(&raw, level).map_err(|e| e.to_string()))
                    .and_then(|data| write_output(dst, &data).map(|_| data));
                match r {
                    Ok(data) => results.lock().unwrap()[i] = json!({ "bytes": data.len(), "sha256": crate::codec::hex(&sha2::Sha256::digest(&data)) }),
                    Err(e) => *err.lock().unwrap() = Some(e),
                }
            });
        }
    });
    if let Some(e) = err.into_inner().unwrap() {
        return Err(e);
    }
    Ok(Value::Array(results.into_inner().unwrap()))
}

/// zstd-batch <spec.json|-> : {"files": [{"in": f, "out": raw_file}, ...]} -> one entry per file:
/// {"bytes": compressed size, "sha256": of the compressed file, "content_size": frame header value or null,
///  "size": decompressed size of the FIRST frame, "eof": first frame complete, "trailing": bytes after it}
/// ("error": msg when the frame cannot be decoded). The first frame's content is written to `out`.
pub fn zstd_batch(args: &[String]) -> Res<Value> {
    use sha2::Digest;
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "spec")?)?).map_err(|e| format!("spec: {e}"))?;
    let mut out = Vec::new();
    for f in spec.get("files").and_then(Value::as_array).cloned().unwrap_or_default() {
        let src = f.get("in").and_then(Value::as_str).unwrap_or("");
        let raw = match std::fs::read(src) {
            Ok(r) => r,
            Err(e) => {
                out.push(json!({ "error": format!("{src}: {e}") }));
                continue;
            }
        };
        let mut info = Map::new();
        info.insert("bytes".into(), json!(raw.len()));
        info.insert("sha256".into(), json!(crate::codec::hex(&sha2::Sha256::digest(&raw))));
        let cs = zstd::zstd_safe::get_frame_content_size(&raw).ok().flatten();
        info.insert("content_size".into(), json!(cs));
        match zstd::zstd_safe::find_frame_compressed_size(&raw) {
            Ok(n) => {
                info.insert("eof".into(), json!(true));
                info.insert("trailing".into(), json!(raw.len() - n));
                match zstd::decode_all(&raw[..n]) {
                    Ok(data) => {
                        info.insert("size".into(), json!(data.len()));
                        if let Some(dst) = f.get("out").and_then(Value::as_str) {
                            write_output(dst, &data)?;
                        }
                    }
                    Err(e) => {
                        info.insert("error".into(), json!(e.to_string()));
                    }
                }
            }
            Err(_) => {
                info.insert("eof".into(), json!(false));
                info.insert("error".into(), json!("zstd frame not terminated"));
            }
        }
        out.push(Value::Object(info));
    }
    Ok(Value::Array(out))
}

// ---------------------------------------------------------------------------------------- sqlite
fn to_sql(v: &Value) -> rusqlite::types::Value {
    use rusqlite::types::Value as V;
    match v {
        Value::Null => V::Null,
        Value::Bool(b) => V::Integer(*b as i64),
        Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                V::Integer(i)
            } else {
                V::Real(n.as_f64().unwrap_or(0.0))
            }
        }
        Value::String(s) => V::Text(s.clone()),
        other => V::Text(other.to_string()),
    }
}

fn from_sql(v: rusqlite::types::ValueRef) -> Value {
    use rusqlite::types::ValueRef as R;
    match v {
        R::Null => Value::Null,
        R::Integer(i) => json!(i),
        R::Real(f) => serde_json::Number::from_f64(f).map(Value::Number).unwrap_or(Value::Null),
        R::Text(t) => Value::String(String::from_utf8_lossy(t).into_owned()),
        R::Blob(b) => json!({ "$bytes": crate::codec::hex(b) }),
    }
}

fn params_of(v: Option<&Value>) -> Vec<rusqlite::types::Value> {
    match v {
        Some(Value::Array(a)) => a.iter().map(to_sql).collect(),
        _ => Vec::new(),
    }
}

/// Run one statement, draining any rows it returns (PRAGMA journal_mode=..., etc.).
fn exec(conn: &rusqlite::Connection, sql: &str, params: &[rusqlite::types::Value]) -> Res<usize> {
    let mut st = conn.prepare_cached(sql).map_err(|e| format!("{sql}: {e}"))?;
    let mut rows = st.query(rusqlite::params_from_iter(params.iter())).map_err(|e| format!("{sql}: {e}"))?;
    while rows.next().map_err(|e| format!("{sql}: {e}"))?.is_some() {}
    drop(rows);
    Ok(conn.changes() as usize)
}

/// Python `round(x, 6)` (the exact binary value rounded half-even; Rust formatting is exact).
fn round6(x: f64) -> f64 {
    format!("{x:.6}").parse().unwrap_or(x)
}

fn zstd_file(path: &str) -> Res<Vec<u8>> {
    let raw = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    zstd::decode_all(&raw[..]).map_err(|e| format!("{path}: {e}"))
}

fn le_i32(b: &[u8], o: usize) -> i32 { i32::from_le_bytes(b[o..o + 4].try_into().unwrap()) }
fn le_u32(b: &[u8], o: usize) -> u32 { u32::from_le_bytes(b[o..o + 4].try_into().unwrap()) }
fn le_i64(b: &[u8], o: usize) -> i64 { i64::from_le_bytes(b[o..o + 8].try_into().unwrap()) }
fn le_f64(b: &[u8], o: usize) -> f64 { f64::from_le_bytes(b[o..o + 8].try_into().unwrap()) }
fn le_f32(b: &[u8], o: usize) -> f32 { f32::from_le_bytes(b[o..o + 4].try_into().unwrap()) }

/// .okbar rows (Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md): struct '<iiiiiiqdffiiB7x' after a 16-byte header.
pub fn okbar_rows(path: &str) -> Res<Vec<Vec<Value>>> {
    let b = zstd_file(path)?;
    if b.len() < 16 || &b[..4] != b"OKBR" {
        return Err(format!("bad header {path}"));
    }
    let n = le_u32(&b, 8) as usize;
    let mut out = Vec::with_capacity(n);
    for r in 0..n {
        let o = 16 + r * 64;
        if o + 64 > b.len() {
            return Err(format!("{path}: truncated"));
        }
        let mut row: Vec<Value> = (0..6).map(|k| json!(le_i32(&b, o + 4 * k))).collect();
        row.push(json!(le_i64(&b, o + 24)));
        row.push(json!(le_f64(&b, o + 32)));
        row.push(json!(le_f32(&b, o + 40) as f64));
        row.push(json!(le_f32(&b, o + 44) as f64));
        row.push(json!(le_i32(&b, o + 48)));
        row.push(json!(le_i32(&b, o + 52)));
        row.push(json!(b[o + 56] as i64));
        out.push(row);
    }
    Ok(out)
}

fn okfx_rows(path: &str) -> Res<Vec<(i32, f64)>> {
    let b = zstd_file(path)?;
    if b.len() < 12 || &b[..4] != b"OKFX" {
        return Err(format!("bad header {path}"));
    }
    let n = le_u32(&b, 8) as usize;
    Ok((0..n).map(|r| (le_i32(&b, 12 + r * 12), le_f64(&b, 16 + r * 12))).collect())
}

/// sqlite <db> <spec.json|-> : runs `ops` in order on one connection (autocommit unless the ops issue
/// BEGIN/COMMIT) and prints one result per op.
///   {"sql": s, "params": [..]}                 -> {"changes": n}
///   {"sql": s, "many": [[..], ..]}             -> {"changes": n}   (executemany)
///   {"sql": s, "params": [..], "query": true}  -> {"rows": [[..]]}  ("out": file writes JSON lines instead)
///   {"script": s}                              -> {}               (executescript)
///   {"okbar": file, "sid": id}                 -> {"rows": n}  DELETE + INSERT bars, split/div round(.,6)
///       ("market": m, "code": c instead of "sid" looks the symbol up)
///   {"okfx": file, "pair": p}                  -> {"rows": n}  DELETE + INSERT fx
/// Spec keys: "readonly" (open read-only), "timeout" (busy timeout, seconds).
pub fn sqlite(args: &[String]) -> Res<Value> {
    let db = arg(args, 0, "db")?;
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 1, "spec")?)?).map_err(|e| format!("spec: {e}"))?;
    let ro = spec.get("readonly").and_then(Value::as_bool).unwrap_or(false);
    let flags = if ro {
        rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY | rusqlite::OpenFlags::SQLITE_OPEN_URI
    } else {
        rusqlite::OpenFlags::SQLITE_OPEN_READ_WRITE | rusqlite::OpenFlags::SQLITE_OPEN_CREATE | rusqlite::OpenFlags::SQLITE_OPEN_URI
    };
    let conn = rusqlite::Connection::open_with_flags(db, flags).map_err(|e| format!("{db}: {e}"))?;
    let timeout = spec.get("timeout").and_then(Value::as_f64).unwrap_or(5.0);
    conn.busy_timeout(std::time::Duration::from_millis((timeout * 1000.0) as u64)).map_err(|e| e.to_string())?;
    conn.set_prepared_statement_cache_capacity(64);
    let empty = Vec::new();
    let ops = spec.get("ops").and_then(Value::as_array).unwrap_or(&empty);
    let mut results = Vec::with_capacity(ops.len());
    for op in ops {
        results.push(run_op(&conn, op)?);
    }
    Ok(Value::Array(results))
}

fn run_op(conn: &rusqlite::Connection, op: &Value) -> Res<Value> {
    if let Some(script) = op.get("script").and_then(Value::as_str) {
        conn.execute_batch(script).map_err(|e| format!("script: {e}"))?;
        return Ok(json!({}));
    }
    if let Some(path) = op.get("okbar").and_then(Value::as_str) {
        let sid = match op.get("sid").and_then(Value::as_i64) {
            Some(s) => s,
            None => {
                // {"market": m, "code": c}: the symbol row inserted by an earlier op
                let (m, c) = (op.get("market").and_then(Value::as_str), op.get("code").and_then(Value::as_str));
                let (Some(m), Some(c)) = (m, c) else { return Err("okbar op needs sid or market+code".into()) };
                conn.query_row("SELECT id FROM symbols WHERE market=? AND code=?", rusqlite::params![m, c], |r| r.get(0))
                    .map_err(|e| format!("{m} {c}: {e}"))?
            }
        };
        let rows = okbar_rows(path)?;
        exec(conn, "DELETE FROM bars WHERE sid=?", &[rusqlite::types::Value::Integer(sid)])?;
        let mut st = conn
            .prepare_cached("INSERT INTO bars VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)")
            .map_err(|e| e.to_string())?;
        for r in &rows {
            let i = |k: usize| r[k].as_i64().unwrap_or(0);
            let f = |k: usize| r[k].as_f64().unwrap_or(0.0);
            st.execute(rusqlite::params![sid, i(0), i(1), i(2), i(3), i(4), i(5), i(6), f(7), round6(f(8)), round6(f(9)), i(10), i(11), i(12)])
                .map_err(|e| format!("{path}: {e}"))?;
        }
        return Ok(json!({ "rows": rows.len() }));
    }
    if let Some(path) = op.get("okfx").and_then(Value::as_str) {
        let pair = op.get("pair").and_then(Value::as_str).ok_or("okfx op needs pair")?;
        let rows = okfx_rows(path)?;
        exec(conn, "DELETE FROM fx WHERE pair=?", &[rusqlite::types::Value::Text(pair.into())])?;
        let mut st = conn.prepare_cached("INSERT INTO fx VALUES(?,?,?)").map_err(|e| e.to_string())?;
        for (d, r) in &rows {
            st.execute(rusqlite::params![pair, d, r]).map_err(|e| format!("{path}: {e}"))?;
        }
        return Ok(json!({ "rows": rows.len() }));
    }
    let sql = op.get("sql").and_then(Value::as_str).ok_or("op needs sql, script, okbar or okfx")?;
    if let Some(many) = op.get("many").and_then(Value::as_array) {
        let mut total = 0;
        for p in many {
            total += exec(conn, sql, &params_of(Some(p)))?;
        }
        return Ok(json!({ "changes": total }));
    }
    let params = params_of(op.get("params"));
    if op.get("query").and_then(Value::as_bool).unwrap_or(false) {
        let mut st = conn.prepare_cached(sql).map_err(|e| format!("{sql}: {e}"))?;
        let ncol = st.column_count();
        let mut rows = st.query(rusqlite::params_from_iter(params.iter())).map_err(|e| format!("{sql}: {e}"))?;
        let out_path = op.get("out").and_then(Value::as_str);
        let mut writer = match out_path {
            Some(p) => Some(std::io::BufWriter::new(std::fs::File::create(p).map_err(|e| format!("{p}: {e}"))?)),
            None => None,
        };
        let mut all = Vec::new();
        let mut n = 0usize;
        while let Some(row) = rows.next().map_err(|e| format!("{sql}: {e}"))? {
            let vals: Vec<Value> = (0..ncol).map(|k| from_sql(row.get_ref(k).unwrap())).collect();
            n += 1;
            if let Some(w) = writer.as_mut() {
                serde_json::to_writer(&mut *w, &vals).map_err(|e| e.to_string())?;
                w.write_all(b"\n").map_err(|e| e.to_string())?;
            } else {
                all.push(Value::Array(vals));
            }
        }
        if let Some(mut w) = writer {
            w.flush().map_err(|e| e.to_string())?;
            return Ok(json!({ "rows_written": n }));
        }
        return Ok(json!({ "rows": all }));
    }
    Ok(json!({ "changes": exec(conn, sql, &params)? }))
}

/// okbar-dump <file> : rows as arrays (d,o,h,l,c,pc,vol,amount,split,div,lu,ld,flags) plus header fields.
pub fn okbar_dump(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "file")?;
    Ok(json!({ "rows": okbar_rows(path)? }))
}

// ---------------------------------------------------------------------------------------- opencc
// Port of opencc-python-reimplemented 0.1.7 (the converter the Python scripts used): per chain item
// (a dictionary or a group of dictionaries) the longest, leftmost dictionary key is replaced and the
// text left/right of it is matched again; a group tries its dictionaries in order on what is still
// unmatched. Text is first split at OpenCC's sentence separators, which are never converted.
macro_rules! dicts {
    ($($name:literal),*) => { &[$(($name, include_str!(concat!("../data/opencc/dictionary/", $name)))),*] };
}
static DICTS: &[(&str, &str)] = dicts!(
    "HKVariants.txt", "HKVariantsPhrases.txt", "HKVariantsRev.txt", "HKVariantsRevPhrases.txt", "STCharacters.txt",
    "STPhrases.txt", "TSCharacters.txt", "TSPhrases.txt", "TWPhrases.txt", "TWPhrasesRev.txt", "TWVariants.txt",
    "TWVariantsRev.txt", "TWVariantsRevPhrases.txt"
);
macro_rules! configs {
    ($($name:literal),*) => { &[$(($name, include_str!(concat!("../data/opencc/config/", $name, ".json")))),*] };
}
static CONFIGS: &[(&str, &str)] = configs!("hk2s", "s2hk", "s2t", "s2tw", "s2twp", "t2hk", "t2s", "t2tw", "tw2s", "tw2sp");

struct Dict {
    max_len: usize,
    min_len: usize,
    map: HashMap<Vec<char>, Vec<char>>,
}

fn load_dict(name: &str) -> Res<Dict> {
    let text = DICTS.iter().find(|d| d.0 == name).ok_or_else(|| format!("no dictionary {name}"))?.1;
    let (mut max_len, mut min_len) = (1usize, 1000usize);
    let mut map = HashMap::new();
    for line in text.lines() {
        let line = line.trim();
        let mut it = line.split('\t');
        let (Some(k), Some(v)) = (it.next(), it.next()) else { continue };
        let key: Vec<char> = k.chars().collect();
        // multiple mappings: the first one
        let val: Vec<char> = v.split(' ').next().unwrap_or("").chars().collect();
        max_len = max_len.max(key.len());
        min_len = min_len.min(key.len());
        map.insert(key, val);
    }
    Ok(Dict { max_len, min_len, map })
}

fn chain_of(config: &str) -> Res<Vec<Vec<Dict>>> {
    let text = CONFIGS.iter().find(|c| c.0 == config).ok_or_else(|| format!("unknown conversion {config}"))?.1;
    let cfg: Value = serde_json::from_str(text).map_err(|e| e.to_string())?;
    let mut chain = Vec::new();
    for item in cfg["conversion_chain"].as_array().ok_or("bad config")? {
        let d = &item["dict"];
        let mut group = Vec::new();
        if d["type"] == "group" {
            for x in d["dicts"].as_array().ok_or("bad group")? {
                group.push(load_dict(x["file"].as_str().ok_or("bad dict")?)?);
            }
        } else {
            group.push(load_dict(d["file"].as_str().ok_or("bad dict")?)?);
        }
        chain.push(group);
    }
    Ok(chain)
}

fn find_match(s: &[char], d: &Dict) -> Option<(usize, usize)> {
    let mut len = s.len().min(d.max_len);
    while len >= d.min_len && len > 0 {
        for i in 0..=(s.len() - len) {
            if d.map.contains_key(&s[i..i + len]) {
                return Some((i, len));
            }
        }
        len -= 1;
    }
    None
}

/// Convert `s` with the dictionaries of one chain item (group: first dictionary that matches wins).
fn convert_group(s: &[char], group: &[Dict], from: usize, out: &mut Vec<char>) {
    if s.is_empty() {
        return;
    }
    for (k, d) in group.iter().enumerate().skip(from) {
        if let Some((i, len)) = find_match(s, d).filter(|&(i, len)| !d.map[&s[i..i + len]].is_empty()) {
            convert_group(&s[..i], group, k, out);
            out.extend_from_slice(&d.map[&s[i..i + len]]);
            convert_group(&s[i + len..], group, k, out);
            return;
        }
    }
    out.extend_from_slice(s);
}

fn is_separator(c: char) -> bool {
    // Python re `\s` (str.isspace) plus the OpenCC separator list
    if c.is_whitespace() || ('\u{1c}'..='\u{1f}').contains(&c) {
        return true;
    }
    "-,.?!*　，。、；：？！…“”‘’『』「」﹁﹂—－（）《》〈〉～．／＼︒︑︔︓︿﹀︹︺︙︐［﹇］﹈︕︖︰︳︴︽︾︵︶｛︷｝︸﹃﹄【︻】︼".contains(c)
}

fn opencc_convert(text: &str, chain: &[Vec<Dict>]) -> String {
    let chars: Vec<char> = text.chars().collect();
    let mut out = String::with_capacity(text.len());
    let mut i = 0;
    while i < chars.len() {
        if is_separator(chars[i]) {
            out.push(chars[i]);
            i += 1;
            continue;
        }
        let mut j = i;
        while j < chars.len() && !is_separator(chars[j]) {
            j += 1;
        }
        let mut cur: Vec<char> = chars[i..j].to_vec();
        for group in chain {
            // a group's dictionaries are tried in order; the text left/right of a match is matched again
            // starting from the dictionary that matched (that is how the Python tree walk behaves)
            let mut next = Vec::with_capacity(cur.len());
            convert_group(&cur, group, 0, &mut next);
            cur = next;
        }
        out.extend(cur);
        i = j;
    }
    out
}

/// zhconv <conversion> <in|-> : {"text": converted}; conversion = s2tw, s2t, t2s, ... (OpenCC config names).
/// With --lines the input is converted line by line and {"lines": [...]} is returned.
pub fn zhconv(args: &[String]) -> Res<Value> {
    let chain = chain_of(arg(args, 0, "conversion")?)?;
    let input = String::from_utf8(read_input(arg(args, 1, "in")?)?).map_err(|e| e.to_string())?;
    if flag(args, "--lines") {
        let lines: Vec<Value> = input.split('\n').map(|l| Value::String(opencc_convert(l, &chain))).collect();
        return Ok(json!({ "lines": lines }));
    }
    Ok(json!({ "text": opencc_convert(&input, &chain) }))
}

// ------------------------------------------------------------------------------------------- pdf
/// pdf-text <pdf> [--pages N] : {"pages": [text, ...]} (first N pages; missing text = "").
pub fn pdf_text(args: &[String]) -> Res<Value> {
    let data = read_input(arg(args, 0, "pdf")?)?;
    let limit: usize = opt(args, "--pages").and_then(|v| v.parse().ok()).unwrap_or(usize::MAX);
    let res = std::panic::catch_unwind(|| pdf_extract::extract_text_from_mem_by_pages(&data));
    let pages = match res {
        Ok(Ok(p)) => p,
        Ok(Err(e)) => return Err(format!("pdf: {e}")),
        Err(_) => return Err("pdf: parser panicked".into()),
    };
    Ok(json!({ "pages": pages.into_iter().take(limit).collect::<Vec<_>>() }))
}

// -------------------------------------------------------------------------------------- baostock
// BaoStock client protocol (baostock 0.9.x Python package): one TCP connection per session; every
// message is "<version>\x01<type>\x01<len:10>" + body fields joined by \x01 + "\x01<crc32>\n"; replies end
// with "<![CDATA[]]>\n" and k-line replies carry a zlib-compressed body.
const BS_HOST: &str = "public-api.baostock.com:10030";
const BS_VERSION: &str = "00.9.40";
const BS_PAGE: usize = 2000;
const BS_COMPRESSED: [&str; 4] = ["96", "99", "9B", "9D"];

struct Bs {
    sock: Option<std::net::TcpStream>,
}

fn bs_header(msg_type: &str, body: &str) -> String {
    format!("{BS_VERSION}\x01{msg_type}\x01{:010}", body.chars().count())
}

impl Bs {
    fn send(&mut self, msg_type: &str, body: &str) -> Option<String> {
        let head_body = bs_header(msg_type, body) + body;
        let mut crc = flate2::Crc::new();
        crc.update(head_body.as_bytes());
        let msg = format!("{head_body}\x01{}\n", crc.sum());
        let sock = self.sock.as_mut()?;
        sock.write_all(msg.as_bytes()).ok()?;
        let mut recv = Vec::new();
        let mut buf = [0u8; 8192];
        loop {
            let n = sock.read(&mut buf).ok()?;
            if n == 0 {
                return None;
            }
            recv.extend_from_slice(&buf[..n]);
            if recv.ends_with(b"<![CDATA[]]>\n") {
                break;
            }
        }
        if recv.len() < 21 {
            return None;
        }
        let head = String::from_utf8_lossy(&recv[..21]).into_owned();
        let parts: Vec<&str> = head.split('\x01').collect();
        if parts.len() > 2 && BS_COMPRESSED.contains(&parts[1]) {
            let inner: usize = parts[2].parse().ok()?;
            let end = (21 + inner).min(recv.len());
            let mut body = String::new();
            flate2::read::ZlibDecoder::new(&recv[21..end]).read_to_string(&mut body).ok()?;
            return Some(head + &body);
        }
        Some(String::from_utf8_lossy(&recv).into_owned())
    }

    /// Reply body fields (receive_data[21:-1].split("\x01")), or None on a network failure.
    fn call(&mut self, msg_type: &str, body: &str) -> Option<Vec<String>> {
        let r = self.send(msg_type, body)?;
        if r.trim().is_empty() {
            return None;
        }
        let chars: Vec<char> = r.chars().collect();
        if chars.len() < 22 {
            return None;
        }
        let body: String = chars[21..chars.len() - 1].iter().collect();
        Some(body.split('\x01').map(String::from).collect())
    }

    fn login(&mut self) -> (String, String) {
        self.sock = std::net::TcpStream::connect(BS_HOST).ok();
        if let Some(s) = &self.sock {
            let _ = s.set_read_timeout(Some(std::time::Duration::from_secs(600)));
            let _ = s.set_write_timeout(Some(std::time::Duration::from_secs(60)));
        }
        match self.call("00", "login\x01anonymous\x01123456\x010") {
            Some(b) if b.len() >= 2 => (b[0].clone(), b[1].clone()),
            _ => ("10002007".into(), "网络接收错误。".into()),
        }
    }

    fn logout(&mut self) {
        let now = chrono_now();
        let _ = self.call("02", &format!("logout\x01anonymous\x01{now}"));
        self.sock = None;
    }
}

fn chrono_now() -> String {
    // local time is not needed by the server; UTC digits keep the format
    let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0) as i64;
    let (y, m, d) = civil(secs.div_euclid(86400));
    let t = secs.rem_euclid(86400);
    format!("{y:04}{m:02}{d:02}{:02}{:02}{:02}", t / 3600, t % 3600 / 60, t % 60)
}

fn civil(z: i64) -> (i64, i64, i64) {
    let z = z + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    (if m <= 2 { yoe + era * 400 + 1 } else { yoe + era * 400 }, m, d)
}

fn sleep_s(s: f64) {
    std::thread::sleep(std::time::Duration::from_secs_f64(s.max(0.0)));
}

fn rand01() -> f64 {
    use std::hash::{BuildHasher, Hasher};
    let mut h = std::collections::hash_map::RandomState::new().build_hasher();
    h.write_u128(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0));
    (h.finish() >> 11) as f64 / (1u64 << 53) as f64
}

/// setData(): drop all whitespace, parse JSON, take "record".
fn bs_records(s: &str) -> Result<Vec<Value>, String> {
    if s.trim().is_empty() {
        return Ok(Vec::new());
    }
    let compact: String = s.chars().filter(|c| !(c.is_whitespace() || ('\u{1c}'..='\u{1f}').contains(c))).collect();
    let v: Value = serde_json::from_str(&compact).map_err(|e| format!("JSONDecodeError: {e}"))?;
    Ok(v.get("record").and_then(Value::as_array).cloned().unwrap_or_default())
}

struct Query {
    msg_type: &'static str,
    body: Vec<String>,
    fields_at: usize,
}

fn kw_str(kw: &Value, k: &str, d: &str) -> String {
    match kw.get(k) {
        Some(Value::String(s)) => s.clone(),
        Some(Value::Null) | None => d.to_string(),
        Some(v) => v.to_string(),
    }
}

fn build_query(func: &str, kw: &Value) -> Res<Query> {
    let page = BS_PAGE.to_string();
    let strip = |v: Vec<String>| v.into_iter().map(|s| s.trim().to_string()).collect::<Vec<_>>();
    Ok(match func {
        "query_trade_dates" => Query {
            msg_type: "33",
            body: strip(vec![func.into(), "anonymous".into(), "1".into(), page, kw_str(kw, "start_date", "2015-01-01"), kw_str(kw, "end_date", "")]),
            fields_at: 9,
        },
        "query_stock_basic" => Query {
            msg_type: "45",
            body: strip(vec![func.into(), "anonymous".into(), "1".into(), page, kw_str(kw, "code", ""), kw_str(kw, "code_name", "")]),
            fields_at: 9,
        },
        "query_history_k_data_plus" => Query {
            msg_type: "95",
            body: vec![func.into(), "anonymous".into(), "1".into(), page, kw_str(kw, "code", "").to_lowercase(), kw_str(kw, "fields", ""),
                kw_str(kw, "start_date", "2015-01-01"), kw_str(kw, "end_date", ""), kw_str(kw, "frequency", "d"), kw_str(kw, "adjustflag", "3")],
            fields_at: 8,
        },
        "query_dividend_data" => Query {
            msg_type: "13",
            body: strip(vec![func.into(), "anonymous".into(), "1".into(), page, kw_str(kw, "code", "").to_lowercase(), kw_str(kw, "year", ""), kw_str(kw, "yearType", "report")]),
            fields_at: 10,
        },
        other => return Err(format!("baostock: unsupported query {other}")),
    })
}

/// One query with all pages (ResultData.next()); Err = "<code> <msg>" like the RuntimeError the scripts raised.
fn bs_query_once(bs: &mut Bs, q: &Query) -> Result<(Vec<String>, Vec<Value>), String> {
    let mut body = q.body.clone();
    let b = bs.call(q.msg_type, &body.join("\x01")).ok_or("10002007 网络接收错误。")?;
    if b.len() < 2 {
        return Err("10002007 网络接收错误。".into());
    }
    if b[0] != "0" {
        return Err(format!("{} {}", b[0], b[1]));
    }
    let fields: Vec<String> = b.get(q.fields_at).map(|f| f.split(',').map(|x| x.trim().to_string()).collect()).unwrap_or_default();
    let mut page_rows = bs_records(b.get(6).map(String::as_str).unwrap_or(""))?;
    let mut cur_page = b.get(4).cloned().unwrap_or_else(|| "1".into());
    let mut rows = Vec::new();
    loop {
        let n = page_rows.len();
        rows.extend(page_rows.drain(..));
        if n != BS_PAGE {
            break;
        }
        let next: usize = cur_page.parse::<usize>().map_err(|_| "当前页面编号不正确".to_string())? + 1;
        body[2] = next.to_string();
        let r = match bs.call(q.msg_type, &body.join("\x01")) {
            Some(r) if r.len() >= 2 => r,
            _ => break,
        };
        if r[0] != "0" {
            return Err(format!("{} {}", r[0], r[1]));
        }
        cur_page = r.get(4).cloned().unwrap_or_default();
        page_rows = bs_records(r.get(6).map(String::as_str).unwrap_or(""))?;
        if page_rows.is_empty() {
            break;
        }
    }
    Ok((fields, rows))
}

enum LoginErr {
    Blacklisted,
    Failed,
}

fn login_msg(e: &LoginErr) -> String {
    match e {
        LoginErr::Blacklisted => "PermissionError('baostock: IP blacklisted (10001011); retry later with fewer workers')".into(),
        LoginErr::Failed => "RuntimeError('baostock login failed')".into(),
    }
}

/// old_py/Tools/build_ashare_data.py BS._login: 8 tries with min(60, 2^i) s pauses; blacklisting stops at once.
fn bs_login(bs: &mut Bs) -> Result<(), LoginErr> {
    for i in 0..8 {
        let (code, _) = bs.login();
        if code == "0" {
            return Ok(());
        }
        if code == "10001011" {
            return Err(LoginErr::Blacklisted);
        }
        sleep_s(f64::min(60.0, 2f64.powi(i)));
    }
    Err(LoginErr::Failed)
}

/// BS.query: 7 tries; after a failure sleep min(60, 2^i) + random, log out, log in again.
fn bs_query(bs: &mut Bs, func: &str, kw: &Value) -> Result<(Vec<String>, Vec<Value>), String> {
    let q = build_query(func, kw).map_err(|e| format!("ValueError('{e}')"))?;
    let mut last = String::new();
    for i in 0..7 {
        match bs_query_once(bs, &q) {
            Ok(r) => return Ok(r),
            Err(e) => {
                last = e;
                sleep_s(f64::min(60.0, 2f64.powi(i)) + rand01());
                bs.logout();
                bs_login(bs).map_err(|e| login_msg(&e))?;
            }
        }
    }
    Err(format!("RuntimeError(\"baostock {func} {} failed: {last}\")", py_kw(kw)))
}

fn py_kw(kw: &Value) -> String {
    let mut parts = Vec::new();
    if let Some(m) = kw.as_object() {
        for (k, v) in m {
            let vs = match v {
                Value::String(s) => format!("'{s}'"),
                other => other.to_string(),
            };
            parts.push(format!("'{k}': {vs}"));
        }
    }
    format!("{{{}}}", parts.join(", "))
}

fn now_hms() -> String {
    let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0) as i64;
    let t = (secs + local_offset()).rem_euclid(86400);
    format!("{:02}:{:02}:{:02}", t / 3600, t % 3600 / 60, t % 60)
}

fn local_offset() -> i64 {
    std::env::var("OK_TZ_OFFSET").ok().and_then(|v| v.parse().ok()).unwrap_or(0)
}

/// baostock <jobs.json|-> [--workers N] [--label L]
/// jobs: {"jobs": [{"fn": "query_history_k_data_plus", "kw": {...}, "out": "file.json"}, ...]}
/// Each worker thread holds one BaoStock session (login with retries). A finished job writes
/// {"fields": [...], "rows": [[...]]} or {"error": "..."} to its `out` (atomically). Progress goes to stderr.
/// When as many jobs as there are workers failed with a blacklisting error the remaining jobs are skipped.
pub fn baostock(args: &[String]) -> Res<Value> {
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};
    let spec: Value = serde_json::from_slice(&read_input(arg(args, 0, "jobs")?)?).map_err(|e| format!("jobs: {e}"))?;
    let workers: usize = opt(args, "--workers").and_then(|v| v.parse().ok()).unwrap_or(1).max(1);
    let label = opt(args, "--label").unwrap_or("baostock").to_string();
    let jobs: Vec<Value> = spec.get("jobs").and_then(Value::as_array).cloned().unwrap_or_default();
    let total = jobs.len();
    let queue = Arc::new(Mutex::new(jobs.into_iter().collect::<std::collections::VecDeque<_>>()));
    let done = Arc::new(AtomicUsize::new(0));
    let errors = Arc::new(AtomicUsize::new(0));
    let black = Arc::new(AtomicUsize::new(0));
    let abort = Arc::new(AtomicBool::new(false));
    let t0 = std::time::Instant::now();
    let mut handles = Vec::new();
    for _ in 0..workers.min(total.max(1)) {
        let (queue, done, errors, black, abort, label) = (queue.clone(), done.clone(), errors.clone(), black.clone(), abort.clone(), label.clone());
        handles.push(std::thread::spawn(move || {
            let mut bs = Bs { sock: None };
            let login_err = bs_login(&mut bs).err().map(|e| login_msg(&e));
            loop {
                if abort.load(Ordering::SeqCst) {
                    break;
                }
                let Some(job) = queue.lock().unwrap().pop_front() else { break };
                let func = job.get("fn").and_then(Value::as_str).unwrap_or("");
                let kw = job.get("kw").cloned().unwrap_or(Value::Object(Map::new()));
                let res = match &login_err {
                    Some(e) => Err(format!("login: {e}")),
                    None => bs_query(&mut bs, func, &kw),
                };
                let out = match &res {
                    Ok((f, r)) => json!({ "fields": f, "rows": r }),
                    Err(e) => {
                        errors.fetch_add(1, Ordering::SeqCst);
                        if e.contains("blacklisted") && black.fetch_add(1, Ordering::SeqCst) + 1 >= workers {
                            eprintln!("{} {label}: BaoStock blacklisted this IP - aborting; resume later (cache is kept)", now_hms());
                            abort.store(true, Ordering::SeqCst);
                        }
                        json!({ "error": e.chars().take(300).collect::<String>() })
                    }
                };
                if let Some(path) = job.get("out").and_then(Value::as_str) {
                    let tmp = format!("{path}.part");
                    if write_output(&tmp, serde_json::to_string(&out).unwrap().as_bytes()).is_ok() {
                        let _ = std::fs::rename(&tmp, path);
                    }
                }
                let n = done.fetch_add(1, Ordering::SeqCst) + 1;
                if n % 50 == 0 || n == total {
                    let el = t0.elapsed().as_secs_f64();
                    eprintln!("{} {label}: {n}/{total} done, {} errors, eta {:.1} min", now_hms(), errors.load(Ordering::SeqCst),
                        el / n as f64 * (total - n) as f64 / 60.0);
                }
            }
            if login_err.is_none() {
                bs.logout();
            }
        }));
    }
    for h in handles {
        let _ = h.join();
    }
    Ok(json!({ "done": done.load(Ordering::SeqCst), "errors": errors.load(Ordering::SeqCst), "aborted": abort.load(Ordering::SeqCst) }))
}

// ------------------------------------------------------------------------------------- text decode
/// text-decode <encoding> <in|-> : {"text": utf8} (WHATWG labels: gbk, gb2312, gb18030, big5, shift_jis,
/// windows-1252, utf-8, ...; malformed bytes become U+FFFD like Python's errors="replace").
pub fn text_decode(args: &[String]) -> Res<Value> {
    let label = arg(args, 0, "encoding")?;
    let data = read_input(arg(args, 1, "in")?)?;
    let enc = encoding_rs::Encoding::for_label(label.as_bytes()).ok_or_else(|| format!("unknown encoding {label}"))?;
    let (text, _, _) = enc.decode(&data);
    Ok(json!({ "text": text }))
}
