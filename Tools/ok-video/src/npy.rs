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
        let name = entry.name().map(|n| n.trim_end_matches(".npy").to_string()).unwrap_or_default();
        if !want.is_empty() && !want.contains(&name.as_str()) { continue; }
        let mut bytes = Vec::with_capacity(entry.size() as usize);
        entry.read_to_end(&mut bytes).map_err(|e| e.to_string())?;
        if let Ok(a) = parse(&bytes) { out.insert(name, a); }
    }
    for w in want { if !out.contains_key(*w) { return Err(format!("{}: missing array {w}", path.display())); } }
    Ok(out)
}

/// Array to store in an .npz: little-endian f4 / f8, C order.
pub enum Out<'a> {
    F32(&'a [f32], Vec<usize>),
    F64(&'a [f64], Vec<usize>),
}

/// numpy `savez` (stored, uncompressed).
pub fn write_npz(path: &Path, arrays: &[(&str, Out)]) -> Result<(), String> {
    use std::io::Write;
    let file = std::fs::File::create(path).map_err(|e| format!("無法寫入 {}：{e}", path.display()))?;
    let mut zip = zip::ZipWriter::new(file);
    let opts = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored).large_file(true);
    for (name, arr) in arrays {
        let (descr, shape, bytes): (&str, &Vec<usize>, Vec<u8>) = match arr {
            Out::F32(d, s) => ("<f4", s, d.iter().flat_map(|v| v.to_le_bytes()).collect()),
            Out::F64(d, s) => ("<f8", s, d.iter().flat_map(|v| v.to_le_bytes()).collect()),
        };
        let shape_s = match shape.len() {
            0 => "()".to_string(),
            1 => format!("({},)", shape[0]),
            _ => format!("({})", shape.iter().map(|v| v.to_string()).collect::<Vec<_>>().join(", ")),
        };
        let mut header = format!("{{'descr': '{descr}', 'fortran_order': False, 'shape': {shape_s}, }}");
        while (10 + header.len() + 1) % 64 != 0 {
            header.push(' ');
        }
        header.push('\n');
        zip.start_file(format!("{name}.npy"), opts).map_err(|e| e.to_string())?;
        zip.write_all(b"\x93NUMPY\x01\x00").map_err(|e| e.to_string())?;
        zip.write_all(&(header.len() as u16).to_le_bytes()).map_err(|e| e.to_string())?;
        zip.write_all(header.as_bytes()).map_err(|e| e.to_string())?;
        zip.write_all(&bytes).map_err(|e| e.to_string())?;
    }
    zip.finish().map_err(|e| e.to_string())?;
    Ok(())
}
