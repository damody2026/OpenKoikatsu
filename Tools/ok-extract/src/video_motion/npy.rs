//! Minimal .npz reader (numpy savez / savez_compressed): float and int arrays,
//! C order, little endian. Values are widened to f64.

use std::collections::HashMap;
use std::io::Read;
use std::path::Path;

pub struct Array {
    pub shape: Vec<usize>,
    pub data: Vec<f64>,
}

impl Array {
    pub fn len0(&self) -> usize { self.shape.first().copied().unwrap_or(1) }
    pub fn row(&self, i: usize) -> &[f64] {
        let w = self.data.len() / self.len0().max(1);
        &self.data[i * w..(i + 1) * w]
    }
}

fn parse(bytes: &[u8]) -> Result<Array, String> {
    if bytes.len() < 10 || &bytes[..6] != b"\x93NUMPY" { return Err("not an npy array".into()); }
    let (hlen, start) = if bytes[6] == 1 { (u16::from_le_bytes([bytes[8], bytes[9]]) as usize, 10) }
        else { (u32::from_le_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]) as usize, 12) };
    let header = std::str::from_utf8(&bytes[start..start + hlen]).map_err(|e| e.to_string())?;
    let field = |key: &str| -> Option<String> {
        let i = header.find(&format!("'{key}':"))? + key.len() + 3;
        let rest = header[i..].trim_start();
        Some(if rest.starts_with('(') { rest[..=rest.find(')')?].to_string() }
             else { rest[..rest.find(',').unwrap_or(rest.len())].trim().trim_matches('\'').to_string() })
    };
    let descr = field("descr").ok_or("npy descr")?;
    let fortran = field("fortran_order").as_deref() == Some("True");
    let shape: Vec<usize> = field("shape").ok_or("npy shape")?.trim_matches(|c| c == '(' || c == ')')
        .split(',').filter_map(|s| s.trim().parse().ok()).collect();
    let body = &bytes[start + hlen..];
    let n: usize = shape.iter().product();
    let data: Vec<f64> = match descr.as_str() {
        "<f8" => body.chunks_exact(8).take(n).map(|c| f64::from_le_bytes(c.try_into().unwrap())).collect(),
        "<f4" => body.chunks_exact(4).take(n).map(|c| f32::from_le_bytes(c.try_into().unwrap()) as f64).collect(),
        "<i8" => body.chunks_exact(8).take(n).map(|c| i64::from_le_bytes(c.try_into().unwrap()) as f64).collect(),
        "<i4" => body.chunks_exact(4).take(n).map(|c| i32::from_le_bytes(c.try_into().unwrap()) as f64).collect(),
        "<u4" => body.chunks_exact(4).take(n).map(|c| u32::from_le_bytes(c.try_into().unwrap()) as f64).collect(),
        other => return Err(format!("unsupported dtype {other}")),
    };
    if data.len() != n { return Err("truncated npy".into()); }
    if fortran && shape.len() > 1 {
        // Column-major -> row-major.
        let mut out = vec![0.; n];
        let mut fstride = vec![1usize; shape.len()];
        for d in 1..shape.len() { fstride[d] = fstride[d - 1] * shape[d - 1]; }
        let mut idx = vec![0usize; shape.len()];
        for o in out.iter_mut() {
            *o = data[idx.iter().zip(&fstride).map(|(i, s)| i * s).sum::<usize>()];
            for d in (0..shape.len()).rev() { idx[d] += 1; if idx[d] < shape[d] { break; } idx[d] = 0; }
        }
        return Ok(Array { shape, data: out });
    }
    Ok(Array { shape, data })
}

/// Reads the named arrays (all supported ones when `want` is empty).
pub fn read_npz(path: &Path, want: &[&str]) -> Result<HashMap<String, Array>, String> {
    let file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut zip = zip::ZipArchive::new(file).map_err(|e| e.to_string())?;
    let mut out = HashMap::new();
    for i in 0..zip.len() {
        let mut entry = zip.by_index(i).map_err(|e| e.to_string())?;
        let name = entry.name().trim_end_matches(".npy").to_string();
        if !want.is_empty() && !want.contains(&name.as_str()) { continue; }
        let mut bytes = Vec::with_capacity(entry.size() as usize);
        entry.read_to_end(&mut bytes).map_err(|e| e.to_string())?;
        if let Ok(a) = parse(&bytes) { out.insert(name, a); }
    }
    for w in want { if !out.contains_key(*w) { return Err(format!("{}: missing array {w}", path.display())); } }
    Ok(out)
}
