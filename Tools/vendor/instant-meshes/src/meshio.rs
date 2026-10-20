//! OBJ, PLY, and FBX input / output.

use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Write};
use std::path::Path;

use crate::math::Vec3;
use crate::mesh::{Faces, OutputMesh, Surface};

pub fn load_mesh(path: &Path) -> Result<Surface, String> {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    match ext.as_str() {
        "obj" => load_obj(path),
        "ply" => load_ply(path),
        #[cfg(feature = "fbx")]
        "fbx" => crate::fbxio::load_fbx(path),
        _ => Err(format!(
            "Unsupported input format '{ext}'. Use .obj, .ply, or .fbx"
        )),
    }
}

pub fn write_mesh(path: &Path, mesh: &OutputMesh) -> Result<(), String> {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    match ext.as_str() {
        "obj" => write_obj(path, mesh),
        "ply" => write_ply(path, mesh),
        #[cfg(feature = "fbx")]
        "fbx" => crate::fbxio::write_fbx(path, mesh),
        _ => Err(format!(
            "Unsupported output format '{ext}'. Use .obj, .ply, or .fbx"
        )),
    }
}

fn load_obj(path: &Path) -> Result<Surface, String> {
    crate::say!("Loading \"{}\" ..", path.display());
    let file = File::open(path).map_err(|e| format!("Unable to open OBJ file: {e}"))?;
    let reader = BufReader::new(file);
    let mut positions = Vec::new();
    let mut indices = Vec::new();
    for line in reader.lines() {
        let line = line.map_err(|e| e.to_string())?;
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut parts = line.split_whitespace();
        match parts.next() {
            Some("v") => {
                let x: f32 = parse_f32(parts.next())?;
                let y: f32 = parse_f32(parts.next())?;
                let z: f32 = parse_f32(parts.next())?;
                positions.push(Vec3::new(x, y, z));
            }
            Some("f") => {
                let verts: Vec<&str> = parts.collect();
                if verts.len() < 3 {
                    continue;
                }
                let ids: Result<Vec<u32>, String> = verts
                    .iter()
                    .map(|tok| {
                        let p = tok.split('/').next().unwrap_or("");
                        let mut idx: i32 = p
                            .parse()
                            .map_err(|_| format!("Invalid face index '{tok}'"))?;
                        if idx < 0 {
                            idx += positions.len() as i32 + 1;
                        }
                        if idx <= 0 {
                            return Err(format!("Invalid face index '{tok}'"));
                        }
                        Ok((idx - 1) as u32)
                    })
                    .collect();
                let ids = ids?;
                for k in 1..ids.len() - 1 {
                    indices.extend_from_slice(&[ids[0], ids[k], ids[k + 1]]);
                }
            }
            _ => {}
        }
    }
    if positions.is_empty() {
        return Err("OBJ file contains no vertices".into());
    }
    let mut faces = Faces::new(3, indices.len() / 3);
    faces.idx = indices;
    crate::say!("done. (V={}, F={})", positions.len(), faces.cols());
    Ok(Surface {
        faces,
        vertices: positions,
        normals: Vec::new(),
    })
}

fn parse_f32(token: Option<&str>) -> Result<f32, String> {
    token
        .ok_or_else(|| "Missing floating point value".to_string())?
        .parse()
        .map_err(|_| "Could not parse floating point value".to_string())
}

fn load_ply(path: &Path) -> Result<Surface, String> {
    crate::say!("Loading \"{}\" ..", path.display());
    let file = File::open(path).map_err(|e| format!("Unable to open PLY file: {e}"))?;
    let mut reader = BufReader::new(file);
    let mut header = String::new();
    loop {
        let mut line = String::new();
        let n = reader.read_line(&mut line).map_err(|e| e.to_string())?;
        if n == 0 {
            return Err("Unexpected end of PLY header".into());
        }
        header.push_str(&line);
        if line.trim() == "end_header" {
            break;
        }
    }
    let mut format = String::new();
    let mut vertex_count = 0usize;
    let mut face_count = 0usize;
    let mut in_vertex = false;
    let mut vx = None;
    let mut vy = None;
    let mut vz = None;
    let mut vprops = 0usize;
    for line in header.lines() {
        let line = line.trim();
        let mut p = line.split_whitespace();
        match p.next() {
            Some("format") => format = p.next().unwrap_or("").to_string(),
            Some("element") => {
                let name = p.next().unwrap_or("");
                let count: usize = p.next().unwrap_or("0").parse().unwrap_or(0);
                in_vertex = name == "vertex";
                if in_vertex {
                    vertex_count = count;
                    vprops = 0;
                }
                if name == "face" {
                    face_count = count;
                }
            }
            Some("property") if in_vertex => {
                let mut rest: Vec<&str> = p.collect();
                if rest.len() >= 2 {
                    let name = rest.pop().unwrap();
                    match name {
                        "x" => vx = Some(vprops),
                        "y" => vy = Some(vprops),
                        "z" => vz = Some(vprops),
                        _ => {}
                    }
                    vprops += 1;
                }
            }
            _ => {}
        }
    }
    if format != "ascii" {
        return Err(format!(
            "Only ASCII PLY is supported by this port (file format is '{format}')"
        ));
    }
    let (ix, iy, iz) = (
        vx.ok_or("PLY is missing vertex x")?,
        vy.ok_or("PLY is missing vertex y")?,
        vz.ok_or("PLY is missing vertex z")?,
    );
    let mut vertices = Vec::with_capacity(vertex_count);
    for _ in 0..vertex_count {
        let mut line = String::new();
        reader.read_line(&mut line).map_err(|e| e.to_string())?;
        let nums: Vec<f32> = line
            .split_whitespace()
            .filter_map(|t| t.parse().ok())
            .collect();
        if nums.len() <= iz.max(iy).max(ix) {
            return Err("Truncated PLY vertex".into());
        }
        vertices.push(Vec3::new(nums[ix], nums[iy], nums[iz]));
    }
    let mut indices = Vec::new();
    for _ in 0..face_count {
        let mut line = String::new();
        reader.read_line(&mut line).map_err(|e| e.to_string())?;
        let nums: Vec<i64> = line
            .split_whitespace()
            .filter_map(|t| t.parse().ok())
            .collect();
        if nums.is_empty() {
            continue;
        }
        let n = nums[0] as usize;
        if nums.len() < 1 + n || n < 3 {
            return Err("PLY face is not a polygon".into());
        }
        let ids: Vec<u32> = nums[1..1 + n].iter().map(|v| *v as u32).collect();
        for k in 1..n - 1 {
            indices.extend_from_slice(&[ids[0], ids[k], ids[k + 1]]);
        }
    }
    let mut faces = Faces::new(3, indices.len() / 3);
    faces.idx = indices;
    crate::say!("done. (V={}, F={})", vertices.len(), faces.cols());
    Ok(Surface {
        faces,
        vertices,
        normals: Vec::new(),
    })
}

fn write_obj(path: &Path, mesh: &OutputMesh) -> Result<(), String> {
    crate::say!(
        "Writing \"{}\" (V={}, F={}) ..",
        path.display(),
        mesh.vertices.len(),
        mesh.faces.cols()
    );
    let file = File::create(path).map_err(|e| format!("Unable to open OBJ file: {e}"))?;
    let mut os = BufWriter::new(file);
    writeln!(os, "# Instant Meshes (Rust port)").map_err(|e| e.to_string())?;
    for v in &mesh.vertices {
        writeln!(os, "v {} {} {}", v.x, v.y, v.z).map_err(|e| e.to_string())?;
    }
    for n in &mesh.face_normals {
        writeln!(os, "vn {} {} {}", n.x, n.y, n.z).map_err(|e| e.to_string())?;
    }
    let degree = mesh.faces.degree;
    let mut irregular: HashMap<u32, HashMap<u32, u32>> = HashMap::new();
    let mut face_of_centroid: HashMap<u32, u32> = HashMap::new();
    for f in 0..mesh.faces.cols() {
        if degree == 4 && mesh.faces.get(2, f) == mesh.faces.get(3, f) {
            let c = mesh.faces.get(2, f);
            irregular
                .entry(c)
                .or_default()
                .insert(mesh.faces.get(0, f), mesh.faces.get(1, f));
            face_of_centroid.entry(c).or_insert(f as u32);
            continue;
        }
        write!(os, "f").map_err(|e| e.to_string())?;
        for j in 0..degree {
            let mut idx = mesh.faces.get(j, f) + 1;
            write!(os, " {idx}").map_err(|e| e.to_string())?;
            if !mesh.face_normals.is_empty() {
                idx = f as u32 + 1;
                write!(os, "//{idx}").map_err(|e| e.to_string())?;
            }
        }
        writeln!(os).map_err(|e| e.to_string())?;
    }
    for (centroid, edges) in &irregular {
        let start = *edges.keys().next().unwrap();
        let mut v = start;
        let face = face_of_centroid.get(centroid).copied().unwrap_or(0);
        write!(os, "f").map_err(|e| e.to_string())?;
        for _ in 0..=edges.len() {
            let mut idx = v + 1;
            write!(os, " {idx}").map_err(|e| e.to_string())?;
            if !mesh.face_normals.is_empty() {
                idx = face + 1;
                write!(os, "//{idx}").map_err(|e| e.to_string())?;
            }
            v = *edges.get(&v).ok_or("Broken irregular face chain")?;
            if v == start {
                break;
            }
        }
        writeln!(os).map_err(|e| e.to_string())?;
    }
    crate::say!("done.");
    Ok(())
}

fn write_ply(path: &Path, mesh: &OutputMesh) -> Result<(), String> {
    crate::say!(
        "Writing \"{}\" (V={}, F={}) ..",
        path.display(),
        mesh.vertices.len(),
        mesh.faces.cols()
    );
    let file = File::create(path).map_err(|e| format!("Unable to open PLY file: {e}"))?;
    let mut os = BufWriter::new(file);
    let degree = mesh.faces.degree;
    writeln!(os, "ply").map_err(|e| e.to_string())?;
    writeln!(os, "format ascii 1.0").map_err(|e| e.to_string())?;
    writeln!(os, "element vertex {}", mesh.vertices.len()).map_err(|e| e.to_string())?;
    writeln!(os, "property float x").map_err(|e| e.to_string())?;
    writeln!(os, "property float y").map_err(|e| e.to_string())?;
    writeln!(os, "property float z").map_err(|e| e.to_string())?;
    writeln!(os, "element face {}", mesh.faces.cols()).map_err(|e| e.to_string())?;
    writeln!(os, "property list uchar int vertex_indices").map_err(|e| e.to_string())?;
    writeln!(os, "end_header").map_err(|e| e.to_string())?;
    for v in &mesh.vertices {
        writeln!(os, "{} {} {}", v.x, v.y, v.z).map_err(|e| e.to_string())?;
    }
    for f in 0..mesh.faces.cols() {
        if degree == 4 && mesh.faces.get(2, f) == mesh.faces.get(3, f) {
            writeln!(
                os,
                "3 {} {} {}",
                mesh.faces.get(0, f),
                mesh.faces.get(1, f),
                mesh.faces.get(2, f)
            )
            .map_err(|e| e.to_string())?;
        } else {
            write!(os, "{degree}").map_err(|e| e.to_string())?;
            for j in 0..degree {
                write!(os, " {}", mesh.faces.get(j, f)).map_err(|e| e.to_string())?;
            }
            writeln!(os).map_err(|e| e.to_string())?;
        }
    }
    crate::say!("done.");
    Ok(())
}
