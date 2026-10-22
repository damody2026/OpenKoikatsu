//! Reading PyTorch checkpoints (`torch.save` zip archives: data.pkl + data/<key> storages) without Python, and
//! writing their tensors as f32 safetensors. The pickle machine covers what state dicts use; anything else becomes an
//! opaque object that is skipped.

use std::collections::HashMap;
use std::io::Read;
use std::path::Path;

use anyhow::{bail, Context, Result};

#[derive(Clone, Debug)]
enum V {
    None,
    Bool(bool),
    Int(i64),
    Float(f64),
    Str(String),
    Bytes(Vec<u8>),
    List(Vec<V>),
    Tuple(Vec<V>),
    Dict(Vec<(V, V)>),
    Global(String, String),
    Storage { dtype: String, key: String },
    Tensor { dtype: String, key: String, offset: usize, shape: Vec<usize>, stride: Vec<usize> },
    Object,
    Mark,
}

fn int(v: &V) -> Result<i64> {
    match v {
        V::Int(i) => Ok(*i),
        V::Bool(b) => Ok(*b as i64),
        _ => bail!("expected int, got {v:?}"),
    }
}

fn ints(v: &V) -> Result<Vec<usize>> {
    match v {
        V::Tuple(x) | V::List(x) => x.iter().map(|e| int(e).map(|i| i as usize)).collect(),
        _ => bail!("expected tuple, got {v:?}"),
    }
}

/// Apply a callable from REDUCE / NEWOBJ.
fn call(f: &V, args: Vec<V>) -> Result<V> {
    let V::Global(m, n) = f else { return Ok(V::Object) };
    Ok(match (m.as_str(), n.as_str()) {
        ("torch._utils", "_rebuild_tensor_v2") | ("torch._utils", "_rebuild_tensor") => {
            let V::Storage { dtype, key } = &args[0] else { bail!("tensor without storage") };
            V::Tensor { dtype: dtype.clone(), key: key.clone(), offset: int(&args[1])? as usize, shape: ints(&args[2])?, stride: ints(&args[3])? }
        }
        ("torch._utils", "_rebuild_parameter") | ("torch._utils", "_rebuild_parameter_with_state") => args.into_iter().next().unwrap_or(V::None),
        ("collections", "OrderedDict") | ("builtins", "dict") => V::Dict(Vec::new()),
        _ => V::Object,
    })
}

fn storage_dtype(name: &str) -> String {
    match name {
        "FloatStorage" => "f32",
        "HalfStorage" => "f16",
        "BFloat16Storage" => "bf16",
        "DoubleStorage" => "f64",
        "LongStorage" => "i64",
        "IntStorage" => "i32",
        "BoolStorage" => "bool",
        "ByteStorage" => "u8",
        _ => "other",
    }
    .to_string()
}

fn unpickle(b: &[u8]) -> Result<V> {
    let mut stack: Vec<V> = Vec::new();
    let mut memo: HashMap<u32, V> = HashMap::new();
    let mut i = 0usize;
    let rd = |i: &mut usize, n: usize| -> Result<&[u8]> {
        let s = b.get(*i..*i + n).context("truncated pickle")?;
        *i += n;
        Ok(s)
    };
    let pop_mark = |stack: &mut Vec<V>| -> Result<Vec<V>> {
        let at = stack.iter().rposition(|v| matches!(v, V::Mark)).context("no mark")?;
        let items = stack.split_off(at + 1);
        stack.pop();
        Ok(items)
    };
    loop {
        let op = *b.get(i).context("pickle ended without STOP")?;
        i += 1;
        match op {
            0x80 => i += 1,                                  // PROTO
            0x95 => i += 8,                                  // FRAME
            b'.' => return stack.pop().context("empty stack"),
            b'}' => stack.push(V::Dict(Vec::new())),
            b']' => stack.push(V::List(Vec::new())),
            b')' => stack.push(V::Tuple(Vec::new())),
            b'(' => stack.push(V::Mark),
            b'N' => stack.push(V::None),
            0x88 => stack.push(V::Bool(true)),
            0x89 => stack.push(V::Bool(false)),
            b'J' => { let s = rd(&mut i, 4)?; stack.push(V::Int(i32::from_le_bytes(s.try_into()?) as i64)); }
            b'K' => { let s = rd(&mut i, 1)?; stack.push(V::Int(s[0] as i64)); }
            b'M' => { let s = rd(&mut i, 2)?; stack.push(V::Int(u16::from_le_bytes(s.try_into()?) as i64)); }
            0x8a => {
                let n = rd(&mut i, 1)?[0] as usize;
                let s = rd(&mut i, n)?;
                let mut v: i64 = 0;
                for (k, byte) in s.iter().enumerate().take(8) {
                    v |= (*byte as i64) << (8 * k);
                }
                if n > 0 && n < 8 && s[n - 1] & 0x80 != 0 {
                    v -= 1i64 << (8 * n);
                }
                stack.push(V::Int(v));
            }
            b'G' => { let s = rd(&mut i, 8)?; stack.push(V::Float(f64::from_be_bytes(s.try_into()?))); }
            b'X' => { let n = u32::from_le_bytes(rd(&mut i, 4)?.try_into()?) as usize; stack.push(V::Str(String::from_utf8_lossy(rd(&mut i, n)?).into())); }
            0x8c => { let n = rd(&mut i, 1)?[0] as usize; stack.push(V::Str(String::from_utf8_lossy(rd(&mut i, n)?).into())); }
            0x8d => { let n = u64::from_le_bytes(rd(&mut i, 8)?.try_into()?) as usize; stack.push(V::Str(String::from_utf8_lossy(rd(&mut i, n)?).into())); }
            b'U' => { let n = rd(&mut i, 1)?[0] as usize; stack.push(V::Str(String::from_utf8_lossy(rd(&mut i, n)?).into())); }
            b'C' => { let n = rd(&mut i, 1)?[0] as usize; stack.push(V::Bytes(rd(&mut i, n)?.to_vec())); }
            b'B' => { let n = u32::from_le_bytes(rd(&mut i, 4)?.try_into()?) as usize; stack.push(V::Bytes(rd(&mut i, n)?.to_vec())); }
            b'c' => {
                let line = |i: &mut usize| -> Result<String> {
                    let end = b[*i..].iter().position(|c| *c == b'\n').context("GLOBAL")? + *i;
                    let s = String::from_utf8_lossy(&b[*i..end]).into_owned();
                    *i = end + 1;
                    Ok(s)
                };
                let m = line(&mut i)?;
                let n = line(&mut i)?;
                stack.push(V::Global(m, n));
            }
            0x93 => {
                let n = stack.pop().context("STACK_GLOBAL")?;
                let m = stack.pop().context("STACK_GLOBAL")?;
                match (m, n) {
                    (V::Str(m), V::Str(n)) => stack.push(V::Global(m, n)),
                    _ => bail!("STACK_GLOBAL with non-strings"),
                }
            }
            b't' => { let items = pop_mark(&mut stack)?; stack.push(V::Tuple(items)); }
            0x85 => { let a = stack.pop().context("TUPLE1")?; stack.push(V::Tuple(vec![a])); }
            0x86 => { let b2 = stack.pop().context("TUPLE2")?; let a = stack.pop().context("TUPLE2")?; stack.push(V::Tuple(vec![a, b2])); }
            0x87 => {
                let c = stack.pop().context("TUPLE3")?;
                let b2 = stack.pop().context("TUPLE3")?;
                let a = stack.pop().context("TUPLE3")?;
                stack.push(V::Tuple(vec![a, b2, c]));
            }
            b'l' => { let items = pop_mark(&mut stack)?; stack.push(V::List(items)); }
            b'a' => {
                let v = stack.pop().context("APPEND")?;
                if let Some(V::List(l)) = stack.last_mut() { l.push(v); }
            }
            b'e' => {
                let items = pop_mark(&mut stack)?;
                if let Some(V::List(l)) = stack.last_mut() { l.extend(items); }
            }
            b'd' => {
                let items = pop_mark(&mut stack)?;
                let mut d = Vec::new();
                for kv in items.chunks(2) {
                    if kv.len() == 2 { d.push((kv[0].clone(), kv[1].clone())); }
                }
                stack.push(V::Dict(d));
            }
            b's' => {
                let v = stack.pop().context("SETITEM")?;
                let k = stack.pop().context("SETITEM")?;
                if let Some(V::Dict(d)) = stack.last_mut() { d.push((k, v)); }
            }
            b'u' => {
                let items = pop_mark(&mut stack)?;
                if let Some(V::Dict(d)) = stack.last_mut() {
                    for kv in items.chunks(2) {
                        if kv.len() == 2 { d.push((kv[0].clone(), kv[1].clone())); }
                    }
                }
            }
            b'R' => {
                let args = stack.pop().context("REDUCE")?;
                let f = stack.pop().context("REDUCE")?;
                let args = match args { V::Tuple(a) => a, other => vec![other] };
                stack.push(call(&f, args)?);
            }
            0x81 => {
                let args = stack.pop().context("NEWOBJ")?;
                let cls = stack.pop().context("NEWOBJ")?;
                let args = match args { V::Tuple(a) => a, other => vec![other] };
                stack.push(call(&cls, args)?);
            }
            b'b' => {
                let state = stack.pop().context("BUILD")?;
                // OrderedDict state / object __setstate__: dicts merge, everything else is ignored
                if let (Some(V::Dict(d)), V::Dict(s)) = (stack.last_mut(), state) {
                    d.extend(s);
                }
            }
            b'Q' => {
                let pid = stack.pop().context("BINPERSID")?;
                let V::Tuple(t) = pid else { bail!("persistent id is not a tuple") };
                // ('storage', storage_type, key, location, numel)
                let dtype = match &t.get(1) { Some(V::Global(_, n)) => storage_dtype(n), _ => "other".into() };
                let key = match &t.get(2) { Some(V::Str(s)) => s.clone(), _ => bail!("storage key") };
                stack.push(V::Storage { dtype, key });
            }
            b'q' => { let k = rd(&mut i, 1)?[0] as u32; memo.insert(k, stack.last().cloned().context("BINPUT")?); }
            b'r' => { let k = u32::from_le_bytes(rd(&mut i, 4)?.try_into()?); memo.insert(k, stack.last().cloned().context("LONG_BINPUT")?); }
            0x94 => { let k = memo.len() as u32; memo.insert(k, stack.last().cloned().context("MEMOIZE")?); }
            b'h' => { let k = rd(&mut i, 1)?[0] as u32; stack.push(memo.get(&k).cloned().context("BINGET")?); }
            b'j' => { let k = u32::from_le_bytes(rd(&mut i, 4)?.try_into()?); stack.push(memo.get(&k).cloned().context("LONG_BINGET")?); }
            b'0' => { stack.pop(); }
            b'2' => { let v = stack.last().cloned().context("DUP")?; stack.push(v); }
            _ => bail!("unsupported pickle opcode 0x{op:02x} at {}", i - 1),
        }
    }
}

fn flatten(v: &V, prefix: &str, out: &mut Vec<(String, V)>) {
    if let V::Dict(d) = v {
        for (k, val) in d {
            let key = match k {
                V::Str(s) => s.clone(),
                V::Int(i) => i.to_string(),
                _ => continue,
            };
            let name = if prefix.is_empty() { key } else { format!("{prefix}.{key}") };
            match val {
                V::Tensor { .. } => out.push((name, val.clone())),
                V::Dict(_) => flatten(val, &name, out),
                _ => {}
            }
        }
    }
}

/// Every float tensor of a torch.save archive as (name, shape, f32 values), names joined with '.'.
pub fn load(path: &Path) -> Result<Vec<(String, Vec<usize>, Vec<f32>)>> {
    let file = std::fs::File::open(path).with_context(|| format!("open {}", path.display()))?;
    let mut zip = zip::ZipArchive::new(file).with_context(|| format!("{} is not a torch zip archive", path.display()))?;
    let pkl_name = (0..zip.len()).filter_map(|k| zip.by_index(k).ok().map(|f| f.name().to_string())).find(|n| n.ends_with("/data.pkl")).context("no data.pkl")?;
    let root = pkl_name.trim_end_matches("data.pkl").to_string();
    let mut pkl = Vec::new();
    zip.by_name(&pkl_name)?.read_to_end(&mut pkl)?;
    let tree = unpickle(&pkl)?;
    let mut tensors = Vec::new();
    flatten(&tree, "", &mut tensors);
    let mut storages: HashMap<String, Vec<u8>> = HashMap::new();
    let mut out = Vec::new();
    for (name, t) in tensors {
        let V::Tensor { dtype, key, offset, shape, stride } = t else { continue };
        let width = match dtype.as_str() { "f32" => 4, "f16" | "bf16" => 2, "f64" => 8, _ => continue };
        if !storages.contains_key(&key) {
            let mut buf = Vec::new();
            zip.by_name(&format!("{root}data/{key}"))?.read_to_end(&mut buf)?;
            storages.insert(key.clone(), buf);
        }
        let s = &storages[&key];
        let get = |idx: usize| -> f32 {
            let at = (offset + idx) * width;
            let c = &s[at..at + width];
            match dtype.as_str() {
                "f32" => f32::from_le_bytes(c.try_into().unwrap()),
                "f16" => super::weights::f16_value(u16::from_le_bytes(c.try_into().unwrap())),
                "bf16" => super::weights::bf16_value(u16::from_le_bytes(c.try_into().unwrap())),
                _ => f64::from_le_bytes(c.try_into().unwrap()) as f32,
            }
        };
        let n: usize = shape.iter().product();
        let mut vals = Vec::with_capacity(n);
        let mut idx = vec![0usize; shape.len()];
        for _ in 0..n {
            vals.push(get(idx.iter().zip(&stride).map(|(a, b)| a * b).sum()));
            for d in (0..shape.len()).rev() {
                idx[d] += 1;
                if idx[d] < shape[d] {
                    break;
                }
                idx[d] = 0;
            }
        }
        out.push((name, shape, vals));
    }
    Ok(out)
}

/// Write f32 tensors as a safetensors file.
pub fn write_safetensors(path: &Path, tensors: &[(String, Vec<usize>, Vec<f32>)]) -> Result<()> {
    let mut header = serde_json::Map::new();
    let mut at = 0usize;
    for (name, shape, vals) in tensors {
        let len = vals.len() * 4;
        header.insert(name.clone(), serde_json::json!({"dtype": "F32", "shape": shape, "data_offsets": [at, at + len]}));
        at += len;
    }
    let mut h = serde_json::to_vec(&serde_json::Value::Object(header))?;
    while h.len() % 8 != 0 {
        h.push(b' ');
    }
    let tmp = path.with_extension("tmp");
    {
        let mut f = std::io::BufWriter::new(std::fs::File::create(&tmp)?);
        use std::io::Write;
        f.write_all(&(h.len() as u64).to_le_bytes())?;
        f.write_all(&h)?;
        for (_, _, vals) in tensors {
            for v in vals {
                f.write_all(&v.to_le_bytes())?;
            }
        }
        f.flush()?;
    }
    std::fs::rename(&tmp, path)?;
    Ok(())
}
