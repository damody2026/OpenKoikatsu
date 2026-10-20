//! PyTorch checkpoint reader without Python or torch (Docs/VIDEO_MOTION_NATIVE_PLAN.md P1).
//!
//! Runs the pickle opcode stream as data only: GLOBAL/REDUCE/BUILD never execute
//! anything, they just build inert nodes. Tensors are recognised from
//! `torch._utils._rebuild_tensor_v2` / `_rebuild_parameter` calls on persistent
//! storages, and named by walking dicts and nn.Module states (`_parameters`,
//! `_buffers`, `_modules`), so Lightning `.ckpt`, mmpose `.pth` and whole
//! pickled ultralytics models (`.pt`) all resolve to `state_dict`-style names.

use std::collections::HashMap;
use std::io::Read;
use std::path::Path;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DType {
    F32,
    F16,
    BF16,
    F64,
    I64,
    I32,
    I16,
    I8,
    U8,
    Bool,
}

impl DType {
    pub fn size(self) -> usize {
        match self {
            DType::F64 | DType::I64 => 8,
            DType::F32 | DType::I32 => 4,
            DType::F16 | DType::BF16 | DType::I16 => 2,
            DType::I8 | DType::U8 | DType::Bool => 1,
        }
    }
    fn from_storage(name: &str) -> Option<DType> {
        Some(match name {
            "FloatStorage" => DType::F32,
            "HalfStorage" => DType::F16,
            "BFloat16Storage" => DType::BF16,
            "DoubleStorage" => DType::F64,
            "LongStorage" => DType::I64,
            "IntStorage" => DType::I32,
            "ShortStorage" => DType::I16,
            "CharStorage" => DType::I8,
            "ByteStorage" => DType::U8,
            "BoolStorage" => DType::Bool,
            _ => return None,
        })
    }
}

#[derive(Clone, Debug)]
pub struct TensorInfo {
    pub name: String,
    pub dtype: DType,
    pub storage: String,
    pub offset: usize,
    pub shape: Vec<usize>,
    pub stride: Vec<usize>,
}

impl TensorInfo {
    pub fn numel(&self) -> usize {
        self.shape.iter().product()
    }
}

#[derive(Clone, Debug)]
enum Val {
    None,
    Bool(bool),
    Int(i64),
    Float(f64),
    Str(String),
    Bytes(Vec<u8>),
    Tuple(Vec<usize>),
    List(Vec<usize>),
    Dict(Vec<(usize, usize)>),
    Global(String, String),
    /// callable(args): REDUCE / NEWOBJ result, with the state of a later BUILD.
    /// `items` holds SETITEMS on dict-like results (OrderedDict) separately from the BUILD state.
    Call { func: usize, args: usize, state: Option<usize>, items: Option<usize> },
    /// persistent id tuple ('storage', type, key, location, numel)
    Storage { dtype: Option<DType>, key: String },
    Mark,
}

struct Vm<'a> {
    data: &'a [u8],
    pos: usize,
    arena: Vec<Val>,
    stack: Vec<usize>,
    memo: HashMap<u64, usize>,
}

fn err<T>(msg: impl Into<String>) -> Result<T, String> {
    Err(msg.into())
}

impl<'a> Vm<'a> {
    fn push(&mut self, v: Val) -> usize {
        self.arena.push(v);
        let id = self.arena.len() - 1;
        self.stack.push(id);
        id
    }
    fn alloc(&mut self, v: Val) -> usize {
        self.arena.push(v);
        self.arena.len() - 1
    }
    fn take(&mut self, n: usize) -> Result<&'a [u8], String> {
        if self.pos + n > self.data.len() {
            return err("pickle truncated");
        }
        let s = &self.data[self.pos..self.pos + n];
        self.pos += n;
        Ok(s)
    }
    fn u8(&mut self) -> Result<u8, String> {
        Ok(self.take(1)?[0])
    }
    fn u16(&mut self) -> Result<u16, String> {
        let b = self.take(2)?;
        Ok(u16::from_le_bytes([b[0], b[1]]))
    }
    fn u32(&mut self) -> Result<u32, String> {
        let b = self.take(4)?;
        Ok(u32::from_le_bytes(b.try_into().unwrap()))
    }
    fn u64(&mut self) -> Result<u64, String> {
        let b = self.take(8)?;
        Ok(u64::from_le_bytes(b.try_into().unwrap()))
    }
    fn line(&mut self) -> Result<String, String> {
        let start = self.pos;
        while self.pos < self.data.len() && self.data[self.pos] != b'\n' {
            self.pos += 1;
        }
        let s = String::from_utf8_lossy(&self.data[start..self.pos]).into_owned();
        self.pos += 1;
        Ok(s)
    }
    fn pop(&mut self) -> Result<usize, String> {
        self.stack.pop().ok_or_else(|| "pickle stack underflow".to_string())
    }
    fn pop_mark(&mut self) -> Result<Vec<usize>, String> {
        let mut items = Vec::new();
        loop {
            let id = self.pop()?;
            if matches!(self.arena[id], Val::Mark) {
                items.reverse();
                return Ok(items);
            }
            items.push(id);
        }
    }
    fn top(&self) -> Result<usize, String> {
        self.stack.last().copied().ok_or_else(|| "pickle stack empty".to_string())
    }
    fn str_of(&self, id: usize) -> Option<&str> {
        match &self.arena[id] {
            Val::Str(s) => Some(s),
            _ => None,
        }
    }
    fn set_items(&mut self, dict: usize, items: Vec<usize>) -> Result<(), String> {
        let pairs: Vec<(usize, usize)> = items.chunks(2).filter(|c| c.len() == 2).map(|c| (c[0], c[1])).collect();
        match &mut self.arena[dict] {
            Val::Dict(d) => {
                d.extend(pairs);
                Ok(())
            }
            // OrderedDict() and other dict-like REDUCE results.
            Val::Call { items, .. } => {
                let st = match *items {
                    Some(s) => s,
                    None => {
                        let s = self.alloc(Val::Dict(Vec::new()));
                        if let Val::Call { items, .. } = &mut self.arena[dict] {
                            *items = Some(s);
                        }
                        s
                    }
                };
                if let Val::Dict(d) = &mut self.arena[st] {
                    d.extend(pairs);
                }
                Ok(())
            }
            _ => err("SETITEM on non-dict"),
        }
    }

    fn run(&mut self) -> Result<usize, String> {
        loop {
            let op = self.u8()?;
            match op {
                0x80 => {
                    self.u8()?;
                } // PROTO
                0x95 => {
                    self.u64()?;
                } // FRAME
                b'.' => return self.pop(), // STOP
                b'(' => {
                    self.push(Val::Mark);
                }
                b'}' => {
                    self.push(Val::Dict(Vec::new()));
                }
                b']' => {
                    self.push(Val::List(Vec::new()));
                }
                b')' => {
                    self.push(Val::Tuple(Vec::new()));
                }
                0x8f => {
                    self.push(Val::List(Vec::new()));
                } // EMPTY_SET
                b'N' => {
                    self.push(Val::None);
                }
                0x88 => {
                    self.push(Val::Bool(true));
                }
                0x89 => {
                    self.push(Val::Bool(false));
                }
                b'J' => {
                    let v = self.u32()? as i32 as i64;
                    self.push(Val::Int(v));
                }
                b'K' => {
                    let v = self.u8()? as i64;
                    self.push(Val::Int(v));
                }
                b'M' => {
                    let v = self.u16()? as i64;
                    self.push(Val::Int(v));
                }
                0x8a => {
                    // LONG1
                    let n = self.u8()? as usize;
                    let b = self.take(n)?;
                    let mut v: i64 = 0;
                    for (i, byte) in b.iter().enumerate().take(8) {
                        v |= (*byte as i64) << (8 * i);
                    }
                    if n > 0 && n < 8 && b[n - 1] & 0x80 != 0 {
                        v -= 1i64 << (8 * n);
                    }
                    self.push(Val::Int(v));
                }
                b'G' => {
                    let b = self.take(8)?;
                    self.push(Val::Float(f64::from_be_bytes(b.try_into().unwrap())));
                }
                b'X' => {
                    let n = self.u32()? as usize;
                    let s = String::from_utf8_lossy(self.take(n)?).into_owned();
                    self.push(Val::Str(s));
                }
                0x8c => {
                    let n = self.u8()? as usize;
                    let s = String::from_utf8_lossy(self.take(n)?).into_owned();
                    self.push(Val::Str(s));
                }
                0x8d => {
                    let n = self.u64()? as usize;
                    let s = String::from_utf8_lossy(self.take(n)?).into_owned();
                    self.push(Val::Str(s));
                }
                b'U' => {
                    let n = self.u8()? as usize;
                    let s = String::from_utf8_lossy(self.take(n)?).into_owned();
                    self.push(Val::Str(s));
                }
                b'T' => {
                    let n = self.u32()? as usize;
                    let s = String::from_utf8_lossy(self.take(n)?).into_owned();
                    self.push(Val::Str(s));
                }
                b'C' => {
                    let n = self.u8()? as usize;
                    let b = self.take(n)?.to_vec();
                    self.push(Val::Bytes(b));
                }
                b'B' => {
                    let n = self.u32()? as usize;
                    let b = self.take(n)?.to_vec();
                    self.push(Val::Bytes(b));
                }
                0x8e => {
                    let n = self.u64()? as usize;
                    let b = self.take(n)?.to_vec();
                    self.push(Val::Bytes(b));
                }
                b'q' => {
                    let k = self.u8()? as u64;
                    let t = self.top()?;
                    self.memo.insert(k, t);
                }
                b'r' => {
                    let k = self.u32()? as u64;
                    let t = self.top()?;
                    self.memo.insert(k, t);
                }
                0x94 => {
                    let k = self.memo.len() as u64;
                    let t = self.top()?;
                    self.memo.insert(k, t);
                }
                b'p' => {
                    let k: u64 = self.line()?.trim().parse().map_err(|_| "bad PUT")?;
                    let t = self.top()?;
                    self.memo.insert(k, t);
                }
                b'h' => {
                    let k = self.u8()? as u64;
                    let id = *self.memo.get(&k).ok_or("bad memo")?;
                    self.stack.push(id);
                }
                b'j' => {
                    let k = self.u32()? as u64;
                    let id = *self.memo.get(&k).ok_or("bad memo")?;
                    self.stack.push(id);
                }
                b'g' => {
                    let k: u64 = self.line()?.trim().parse().map_err(|_| "bad GET")?;
                    let id = *self.memo.get(&k).ok_or("bad memo")?;
                    self.stack.push(id);
                }
                b'0' => {
                    self.pop()?;
                }
                b'1' => {
                    self.pop_mark()?;
                }
                b'2' => {
                    let t = self.top()?;
                    self.stack.push(t);
                }
                b't' => {
                    let items = self.pop_mark()?;
                    self.push(Val::Tuple(items));
                }
                0x85 => {
                    let a = self.pop()?;
                    self.push(Val::Tuple(vec![a]));
                }
                0x86 => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(Val::Tuple(vec![a, b]));
                }
                0x87 => {
                    let c = self.pop()?;
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(Val::Tuple(vec![a, b, c]));
                }
                b'l' => {
                    let items = self.pop_mark()?;
                    self.push(Val::List(items));
                }
                b'a' => {
                    let v = self.pop()?;
                    let l = self.top()?;
                    if let Val::List(items) = &mut self.arena[l] {
                        items.push(v);
                    }
                }
                b'e' => {
                    let vs = self.pop_mark()?;
                    let l = self.top()?;
                    if let Val::List(items) = &mut self.arena[l] {
                        items.extend(vs);
                    }
                }
                0x90 => {
                    // ADDITEMS (set)
                    let vs = self.pop_mark()?;
                    let l = self.top()?;
                    if let Val::List(items) = &mut self.arena[l] {
                        items.extend(vs);
                    }
                }
                0x91 => {
                    let items = self.pop_mark()?;
                    self.push(Val::List(items));
                } // FROZENSET
                b'd' => {
                    let items = self.pop_mark()?;
                    let pairs = items.chunks(2).filter(|c| c.len() == 2).map(|c| (c[0], c[1])).collect();
                    self.push(Val::Dict(pairs));
                }
                b's' => {
                    let v = self.pop()?;
                    let k = self.pop()?;
                    let d = self.top()?;
                    self.set_items(d, vec![k, v])?;
                }
                b'u' => {
                    let items = self.pop_mark()?;
                    let d = self.top()?;
                    self.set_items(d, items)?;
                }
                b'c' => {
                    let module = self.line()?;
                    let name = self.line()?;
                    self.push(Val::Global(module, name));
                }
                0x93 => {
                    let name = self.pop()?;
                    let module = self.pop()?;
                    let (m, n) = (self.str_of(module).unwrap_or("").to_string(), self.str_of(name).unwrap_or("").to_string());
                    self.push(Val::Global(m, n));
                }
                b'R' | 0x81 => {
                    // REDUCE / NEWOBJ
                    let args = self.pop()?;
                    let func = self.pop()?;
                    self.push(Val::Call { func, args, state: None, items: None });
                }
                0x92 => {
                    // NEWOBJ_EX
                    let _kw = self.pop()?;
                    let args = self.pop()?;
                    let func = self.pop()?;
                    self.push(Val::Call { func, args, state: None, items: None });
                }
                b'b' => {
                    let st = self.pop()?;
                    let obj = self.top()?;
                    match &mut self.arena[obj] {
                        Val::Call { state, .. } => *state = Some(st),
                        _ => {}
                    }
                }
                b'Q' => {
                    // BINPERSID: ('storage', StorageType, key, location, numel)
                    let pid = self.pop()?;
                    let v = self.storage(pid);
                    self.push(v);
                }
                other => return err(format!("unsupported pickle opcode 0x{other:02x} at {}", self.pos - 1)),
            }
        }
    }

    fn storage(&self, pid: usize) -> Val {
        let Val::Tuple(items) = &self.arena[pid] else { return Val::None };
        if items.len() < 3 {
            return Val::None;
        }
        let dtype = match &self.arena[items[1]] {
            Val::Global(_, name) => DType::from_storage(name),
            _ => None,
        };
        let key = match &self.arena[items[2]] {
            Val::Str(s) => s.clone(),
            Val::Int(i) => i.to_string(),
            _ => String::new(),
        };
        Val::Storage { dtype, key }
    }

    fn int(&self, id: usize) -> Option<i64> {
        match self.arena[id] {
            Val::Int(i) => Some(i),
            Val::Bool(b) => Some(b as i64),
            _ => None,
        }
    }
    fn ints(&self, id: usize) -> Option<Vec<usize>> {
        match &self.arena[id] {
            Val::Tuple(v) | Val::List(v) => v.iter().map(|&i| self.int(i).map(|x| x as usize)).collect(),
            _ => None,
        }
    }
    fn global_name(&self, id: usize) -> Option<(&str, &str)> {
        match &self.arena[id] {
            Val::Global(m, n) => Some((m, n)),
            _ => None,
        }
    }

    /// Tensor described by node `id`, if it is a tensor rebuild call.
    fn tensor(&self, id: usize) -> Option<(DType, String, usize, Vec<usize>, Vec<usize>)> {
        let Val::Call { func, args, .. } = &self.arena[id] else { return None };
        let (_, name) = self.global_name(*func)?;
        let Val::Tuple(a) = &self.arena[*args] else { return None };
        match name {
            "_rebuild_parameter" | "_rebuild_parameter_with_state" => self.tensor(*a.first()?),
            "_rebuild_tensor_v2" | "_rebuild_tensor" => {
                let Val::Storage { dtype, key } = &self.arena[*a.first()?] else { return None };
                let offset = self.int(*a.get(1)?)? as usize;
                let shape = self.ints(*a.get(2)?)?;
                let stride = self.ints(*a.get(3)?)?;
                Some(((*dtype)?, key.clone(), offset, shape, stride))
            }
            _ => None,
        }
    }

    fn walk(&self, id: usize, path: &str, out: &mut Vec<TensorInfo>, depth: usize) {
        if depth > 64 {
            return;
        }
        if let Some((dtype, storage, offset, shape, stride)) = self.tensor(id) {
            out.push(TensorInfo { name: path.to_string(), dtype, storage, offset, shape, stride });
            return;
        }
        let join = |k: &str| if path.is_empty() { k.to_string() } else { format!("{path}.{k}") };
        match &self.arena[id] {
            Val::Dict(pairs) => {
                for &(k, v) in pairs {
                    let key = match &self.arena[k] {
                        Val::Str(s) => s.clone(),
                        Val::Int(i) => i.to_string(),
                        _ => continue,
                    };
                    // nn.Module state: parameters/buffers/children keep the module's own path.
                    if matches!(key.as_str(), "_parameters" | "_buffers" | "_modules") {
                        self.walk(v, path, out, depth + 1);
                    } else if !key.starts_with('_') || path.is_empty() {
                        self.walk(v, &join(&key), out, depth + 1);
                    }
                }
            }
            Val::Call { state, items, .. } => {
                if let Some(i) = items {
                    self.walk(*i, path, out, depth + 1);
                }
                if let Some(st) = state {
                    self.walk(*st, path, out, depth + 1);
                }
            }
            Val::Tuple(items) | Val::List(items) => {
                // (state, slotstate) tuples from BUILD
                if items.len() == 2 && matches!(self.arena[items[0]], Val::Dict(_)) {
                    self.walk(items[0], path, out, depth + 1);
                }
            }
            _ => {}
        }
    }
}

impl<'a> Vm<'a> {
    fn describe(&self, id: usize, depth: usize, out: &mut String) {
        let pad = "  ".repeat(depth);
        match &self.arena[id] {
            Val::Dict(p) => {
                out.push_str(&format!("{pad}dict({})
", p.len()));
                if depth < 3 {
                    for &(k, v) in p.iter().take(6) {
                        out.push_str(&format!("{pad} key {:?}
", self.arena[k]));
                        self.describe(v, depth + 1, out);
                    }
                }
            }
            Val::Call { func, args, state, items } => {
                out.push_str(&format!("{pad}call {:?} state={} items={}
", self.arena[*func], state.is_some(), items.is_some()));
                if depth < 3 {
                    self.describe(*args, depth + 1, out);
                    if let Some(s) = state {
                        self.describe(*s, depth + 1, out);
                    }
                }
            }
            Val::Tuple(v) | Val::List(v) => {
                out.push_str(&format!("{pad}seq({})
", v.len()));
                if depth < 3 {
                    for &i in v.iter().take(4) {
                        self.describe(i, depth + 1, out);
                    }
                }
            }
            other => out.push_str(&format!("{pad}{:?}
", other)),
        }
    }
}

/// Top of the object graph (debugging unknown checkpoint layouts).
pub fn describe(path: &Path) -> Result<String, String> {
    let file = std::fs::File::open(path).map_err(|e| e.to_string())?;
    let mut archive = zip::ZipArchive::new(file).map_err(|e| e.to_string())?;
    let pkl = (0..archive.len())
        .filter_map(|i| archive.by_index(i).ok().and_then(|f| f.name().ok().map(|n| n.to_string())))
        .find(|n| n.ends_with("data.pkl"))
        .ok_or("no data.pkl")?;
    let mut bytes = Vec::new();
    archive.by_name(&pkl).map_err(|e| e.to_string())?.read_to_end(&mut bytes).map_err(|e| e.to_string())?;
    let mut vm = Vm { data: &bytes, pos: 0, arena: Vec::new(), stack: Vec::new(), memo: HashMap::new() };
    let root = vm.run()?;
    let mut out = String::new();
    vm.describe(root, 0, &mut out);
    Ok(out)
}

pub struct Checkpoint {
    archive: zip::ZipArchive<std::fs::File>,
    prefix: String,
    pub tensors: Vec<TensorInfo>,
}

impl Checkpoint {
    pub fn open(path: &Path) -> Result<Self, String> {
        let file = std::fs::File::open(path).map_err(|e| format!("無法開啟權重檔 {}：{e}", path.display()))?;
        let mut archive = zip::ZipArchive::new(file).map_err(|e| format!("{} 不是 PyTorch zip 格式（{e}）；請使用原始下載的檔案", path.display()))?;
        let pkl = (0..archive.len())
            .filter_map(|i| archive.by_index(i).ok().and_then(|f| f.name().ok().map(|n| n.to_string())))
            .find(|n| n.ends_with("data.pkl"))
            .ok_or_else(|| format!("{} 裡找不到 data.pkl", path.display()))?;
        let prefix = pkl.trim_end_matches("data.pkl").to_string();
        let mut bytes = Vec::new();
        archive.by_name(&pkl).map_err(|e| e.to_string())?.read_to_end(&mut bytes).map_err(|e| e.to_string())?;
        let mut vm = Vm { data: &bytes, pos: 0, arena: Vec::new(), stack: Vec::new(), memo: HashMap::new() };
        let root = vm.run().map_err(|e| format!("{}：{e}", path.display()))?;
        let mut tensors = Vec::new();
        vm.walk(root, "", &mut tensors, 0);
        Ok(Checkpoint { archive, prefix, tensors })
    }

    /// Raw little-endian bytes of `t`, made contiguous.
    pub fn bytes(&mut self, t: &TensorInfo) -> Result<Vec<u8>, String> {
        let name = format!("{}data/{}", self.prefix, t.storage);
        let mut raw = Vec::new();
        self.archive.by_name(&name).map_err(|e| format!("{name}：{e}"))?.read_to_end(&mut raw).map_err(|e| e.to_string())?;
        let es = t.dtype.size();
        let n = t.numel();
        let contiguous = {
            let mut expect = 1usize;
            let mut ok = true;
            for (d, s) in t.shape.iter().zip(&t.stride).rev() {
                if *d != 1 && *s != expect {
                    ok = false;
                }
                expect *= d;
            }
            ok
        };
        if contiguous {
            let start = t.offset * es;
            let end = start + n * es;
            if end > raw.len() {
                return Err(format!("{} 超出儲存區大小", t.name));
            }
            return Ok(raw[start..end].to_vec());
        }
        let mut out = Vec::with_capacity(n * es);
        let mut index = vec![0usize; t.shape.len()];
        for _ in 0..n {
            let mut off = t.offset;
            for (i, s) in index.iter().zip(&t.stride) {
                off += i * s;
            }
            out.extend_from_slice(raw.get(off * es..off * es + es).ok_or("stride out of range")?);
            for d in (0..index.len()).rev() {
                index[d] += 1;
                if index[d] < t.shape[d] {
                    break;
                }
                index[d] = 0;
            }
        }
        Ok(out)
    }
}
