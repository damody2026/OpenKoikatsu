//! FBX mesh input and output.
//!
//! Binary FBX 7.4 and 7.5 are read with `fbxcel-dom`. ASCII FBX is read by a
//! small scanner that pulls geometries, model transforms, and connections.
//! Output is binary FBX 7.4 with one mesh node. Node translation, rotation,
//! scale, pre/post rotation, and geometric transform are baked into the
//! vertices. Materials, skinning, and animation are ignored.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::fs::File;
use std::io::{BufReader, BufWriter, Read, Write};
use std::path::Path;

use fbxcel_dom::fbxcel::low::v7400::AttributeValue;
use fbxcel_dom::any::AnyDocument;
use fbxcel_dom::v7400::data::mesh::PolygonVertex;
use fbxcel_dom::v7400::object::geometry::TypedGeometryHandle;
use fbxcel_dom::v7400::object::{ObjectHandle, TypedObjectHandle};
use fbxcel_dom::v7400::Document;

use crate::math::Vec3;
use crate::mesh::{Faces, OutputMesh, Surface};

const FBX_MAGIC: &[u8] = b"Kaydara FBX Binary  \x00\x1a\x00";
const FBX_VERSION: u32 = 7400;

pub fn load_fbx(path: &Path) -> Result<Surface, String> {
    crate::say!("Loading \"{}\" ..", path.display());
    let mut file = File::open(path).map_err(|e| format!("Unable to open FBX file: {e}"))?;
    let mut magic = [0u8; FBX_MAGIC.len()];
    let n = file
        .read(&mut magic)
        .map_err(|e| format!("Unable to read FBX file: {e}"))?;
    let binary = n == FBX_MAGIC.len() && magic == FBX_MAGIC;
    let surface = if binary {
        load_binary(path)?
    } else {
        let mut bytes = magic[..n].to_vec();
        file.read_to_end(&mut bytes)
            .map_err(|e| format!("Unable to read FBX file: {e}"))?;
        let text = String::from_utf8_lossy(&bytes);
        if !text.contains("FBX") {
            return Err("File is not an FBX document".into());
        }
        load_ascii(&text)?
    };
    crate::say!(
        "done. (V={}, F={})",
        surface.vertices.len(),
        surface.faces.cols()
    );
    Ok(surface)
}

pub fn write_fbx(path: &Path, mesh: &OutputMesh) -> Result<(), String> {
    crate::say!(
        "Writing \"{}\" (V={}, F={}) ..",
        path.display(),
        mesh.vertices.len(),
        mesh.faces.cols()
    );
    let polygons = polygons_from_output(mesh)?;
    let node = scene_node(mesh, &polygons, Xform::default());
    let bytes = encode_binary(&node);
    let file = File::create(path).map_err(|e| format!("Unable to open FBX file: {e}"))?;
    let mut out = BufWriter::new(file);
    out.write_all(&bytes)
        .map_err(|e| format!("Unable to write FBX file: {e}"))?;
    crate::say!("done.");
    Ok(())
}

fn load_binary(path: &Path) -> Result<Surface, String> {
    let file = File::open(path).map_err(|e| format!("Unable to open FBX file: {e}"))?;
    let reader = BufReader::new(file);
    let doc = match AnyDocument::from_seekable_reader(reader) {
        Ok(AnyDocument::V7400(_, doc)) => doc,
        Ok(_) => {
            return Err(
                "Unsupported FBX version. Use FBX 7.4 or 7.5, or ASCII FBX.".into(),
            );
        }
        Err(err) => return Err(format!("Failed to read binary FBX: {err}")),
    };
    load_binary_document(&doc)
}

fn load_binary_document(doc: &Document) -> Result<Surface, String> {
    let mut positions = Vec::new();
    let mut indices = Vec::new();
    let mut seen = HashSet::new();
    for obj in doc.objects() {
        if obj.class() != "Model" {
            continue;
        }
        let world = world_matrix(obj, 0);
        for src in obj.source_objects() {
            if src.label().is_some() {
                continue;
            }
            let Some(child) = src.object_handle() else {
                continue;
            };
            if child.class() != "Geometry" || child.subclass() != "Mesh" {
                continue;
            }
            if !seen.insert(object_key(child)) {
                continue;
            }
            let TypedObjectHandle::Geometry(TypedGeometryHandle::Mesh(mesh)) = child.get_typed()
            else {
                continue;
            };
            let xform = mul(world, geometric_matrix(obj));
            append_geometry(&mesh, xform, &mut positions, &mut indices)?;
        }
    }
    if positions.is_empty() {
        for obj in doc.objects() {
            if obj.class() != "Geometry" || obj.subclass() != "Mesh" {
                continue;
            }
            let TypedObjectHandle::Geometry(TypedGeometryHandle::Mesh(mesh)) = obj.get_typed()
            else {
                continue;
            };
            append_geometry(&mesh, Mat4::identity(), &mut positions, &mut indices)?;
        }
    }
    surface_from_parts(positions, indices)
}

fn append_geometry(
    mesh: &fbxcel_dom::v7400::object::geometry::MeshHandle<'_>,
    xform: Mat4,
    positions: &mut Vec<Vec3>,
    indices: &mut Vec<u32>,
) -> Result<(), String> {
    let polys = mesh
        .polygon_vertices()
        .map_err(|e| format!("FBX mesh is missing polygon data: {e}"))?;
    let points: Vec<_> = polys
        .raw_control_points()
        .map_err(|e| format!("FBX mesh is missing vertices: {e}"))?
        .collect();
    let base = positions.len() as u32;
    for p in &points {
        let v = xform.transform([p.x, p.y, p.z]);
        positions.push(Vec3::new(v[0] as f32, v[1] as f32, v[2] as f32));
    }
    let mut face = Vec::new();
    for &raw in polys.raw_polygon_vertices() {
        let corner = PolygonVertex::new(raw);
        let id = corner.to_u32();
        if id as usize >= points.len() {
            return Err(format!("FBX polygon index {id} is out of range"));
        }
        face.push(base + id);
        if corner.is_end() {
            push_fan(&face, indices);
            face.clear();
        }
    }
    if !face.is_empty() {
        return Err("FBX polygon list is missing its closing vertex".into());
    }
    Ok(())
}

fn object_key(obj: ObjectHandle<'_>) -> u64 {
    obj.object_id().raw() as u64
}

fn parent_model(obj: ObjectHandle<'_>) -> Option<ObjectHandle<'_>> {
    obj.destination_objects().find_map(|link| {
        if link.label().is_some() {
            return None;
        }
        let dest = link.object_handle()?;
        if dest.class() == "Model" {
            Some(dest)
        } else {
            None
        }
    })
}

fn world_matrix(obj: ObjectHandle<'_>, depth: u32) -> Mat4 {
    if depth > 64 {
        return Mat4::identity();
    }
    let local = local_matrix(obj);
    match parent_model(obj) {
        Some(parent) => mul(world_matrix(parent, depth + 1), local),
        None => local,
    }
}

fn local_matrix(obj: ObjectHandle<'_>) -> Mat4 {
    let t = read_vec3(obj, "Lcl Translation", [0.0, 0.0, 0.0]);
    let r = read_vec3(obj, "Lcl Rotation", [0.0, 0.0, 0.0]);
    let s = read_vec3(obj, "Lcl Scaling", [1.0, 1.0, 1.0]);
    let pre = read_vec3(obj, "PreRotation", [0.0, 0.0, 0.0]);
    let post = read_vec3(obj, "PostRotation", [0.0, 0.0, 0.0]);
    let order = read_i32(obj, "RotationOrder").unwrap_or(0);
    let rotation = mul(euler(0, pre), mul(euler(order, r), transpose(euler(0, post))));
    mul(mul(Mat4::translation(t), rotation), Mat4::scale(s))
}

fn geometric_matrix(obj: ObjectHandle<'_>) -> Mat4 {
    let t = read_vec3(obj, "GeometricTranslation", [0.0, 0.0, 0.0]);
    let r = read_vec3(obj, "GeometricRotation", [0.0, 0.0, 0.0]);
    let s = read_vec3(obj, "GeometricScaling", [1.0, 1.0, 1.0]);
    mul(mul(Mat4::translation(t), euler(0, r)), Mat4::scale(s))
}

fn read_vec3(obj: ObjectHandle<'_>, name: &str, default: [f64; 3]) -> [f64; 3] {
    let Some(props) = obj.direct_properties() else {
        return default;
    };
    let Some(prop) = props.get_property(name) else {
        return default;
    };
    let mut out = [0.0; 3];
    let mut n = 0;
    for value in prop.value_part() {
        if n == 3 {
            break;
        }
        if let Some(x) = attr_f64(value) {
            out[n] = x;
            n += 1;
        }
    }
    if n == 3 {
        out
    } else {
        default
    }
}

fn read_i32(obj: ObjectHandle<'_>, name: &str) -> Option<i32> {
    let prop = obj.direct_properties()?.get_property(name)?;
    prop.value_part().iter().find_map(attr_i32)
}

fn attr_f64(value: &AttributeValue) -> Option<f64> {
    match value {
        AttributeValue::F64(v) => Some(*v),
        AttributeValue::F32(v) => Some(*v as f64),
        AttributeValue::I64(v) => Some(*v as f64),
        AttributeValue::I32(v) => Some(*v as f64),
        AttributeValue::I16(v) => Some(*v as f64),
        _ => None,
    }
}

fn attr_i32(value: &AttributeValue) -> Option<i32> {
    match value {
        AttributeValue::I32(v) => Some(*v),
        AttributeValue::I16(v) => Some(*v as i32),
        AttributeValue::I64(v) => Some(*v as i32),
        AttributeValue::F64(v) => Some(*v as i32),
        _ => None,
    }
}

fn load_ascii(text: &str) -> Result<Surface, String> {
    let roots = parse_nodes(text)?;
    let mut geometries: HashMap<i64, (Vec<[f64; 3]>, Vec<i32>)> = HashMap::new();
    let mut xforms: HashMap<i64, Xform> = HashMap::new();
    let mut parent_of: HashMap<i64, i64> = HashMap::new();
    for node in &roots {
        if node.name == "Objects" {
            for child in &node.children {
                let Some(id) = leading_id(&child.header) else {
                    continue;
                };
                if child.name == "Geometry" && header_contains(&child.header, "Mesh") {
                    let (verts, polys) = ascii_geometry(child)?;
                    geometries.insert(id, (verts, polys));
                } else if child.name == "Model" {
                    xforms.insert(id, ascii_xform(child));
                }
            }
        } else if node.name == "Connections" {
            for child in &node.children {
                if child.name != "C" {
                    continue;
                }
                let nums = numbers_in(&child.header);
                if nums.len() >= 2 {
                    parent_of.insert(nums[0] as i64, nums[1] as i64);
                }
            }
        }
    }
    let mut positions = Vec::new();
    let mut indices = Vec::new();
    let mut used = HashSet::new();
    for (&geom_id, (verts, polys)) in &geometries {
        let model_id = parent_of.get(&geom_id).copied();
        let xform = match model_id {
            Some(id) => ascii_world(id, &xforms, &parent_of, 0),
            None => Mat4::identity(),
        };
        used.insert(geom_id);
        append_raw(verts, polys, xform, &mut positions, &mut indices)?;
    }
    if positions.is_empty() {
        for (verts, polys) in geometries.values() {
            append_raw(verts, polys, Mat4::identity(), &mut positions, &mut indices)?;
        }
    }
    let _ = used;
    surface_from_parts(positions, indices)
}

fn ascii_world(
    id: i64,
    xforms: &HashMap<i64, Xform>,
    parent_of: &HashMap<i64, i64>,
    depth: u32,
) -> Mat4 {
    if depth > 64 {
        return Mat4::identity();
    }
    let local = xforms.get(&id).copied().unwrap_or_default().matrix();
    match parent_of.get(&id).copied() {
        Some(parent) if xforms.contains_key(&parent) => {
            mul(ascii_world(parent, xforms, parent_of, depth + 1), local)
        }
        _ => local,
    }
}

fn ascii_geometry(node: &AsciiNode) -> Result<(Vec<[f64; 3]>, Vec<i32>), String> {
    let mut verts = Vec::new();
    let mut polys = Vec::new();
    for child in &node.children {
        if child.name == "Vertices" {
            let nums = numbers_in(&child.body);
            if nums.len() % 3 != 0 {
                return Err("FBX Vertices array length is not a multiple of 3".into());
            }
            verts = nums
                .chunks(3)
                .map(|c| [c[0], c[1], c[2]])
                .collect();
        } else if child.name == "PolygonVertexIndex" {
            polys = numbers_in(&child.body).iter().map(|v| *v as i32).collect();
        }
    }
    if verts.is_empty() {
        return Err("FBX geometry has no vertices".into());
    }
    Ok((verts, polys))
}

fn ascii_xform(node: &AsciiNode) -> Xform {
    let mut xform = Xform::default();
    let Some(props) = node.children.iter().find(|c| c.name == "Properties70") else {
        return xform;
    };
    for prop in &props.children {
        if prop.name != "P" {
            continue;
        }
        let fields = split_csv(&prop.header);
        if fields.is_empty() {
            continue;
        }
        let name = unquote(&fields[0]);
        let nums: Vec<f64> = fields.iter().skip(4).filter_map(|s| s.parse().ok()).collect();
        let vec3 = if nums.len() >= 3 {
            [nums[0], nums[1], nums[2]]
        } else {
            [0.0, 0.0, 0.0]
        };
        match name.as_str() {
            "Lcl Translation" => xform.translation = vec3,
            "Lcl Rotation" => xform.rotation = vec3,
            "Lcl Scaling" if nums.len() >= 3 => xform.scale = vec3,
            "PreRotation" => xform.pre_rotation = vec3,
            "PostRotation" => xform.post_rotation = vec3,
            "GeometricTranslation" => xform.geometric_translation = vec3,
            "GeometricRotation" => xform.geometric_rotation = vec3,
            "GeometricScaling" if nums.len() >= 3 => xform.geometric_scale = vec3,
            "RotationOrder" if !nums.is_empty() => xform.order = nums[0] as i32,
            _ => {}
        }
    }
    xform
}

fn append_raw(
    verts: &[[f64; 3]],
    polys: &[i32],
    xform: Mat4,
    positions: &mut Vec<Vec3>,
    indices: &mut Vec<u32>,
) -> Result<(), String> {
    let base = positions.len() as u32;
    for p in verts {
        let v = xform.transform(*p);
        positions.push(Vec3::new(v[0] as f32, v[1] as f32, v[2] as f32));
    }
    let mut face = Vec::new();
    for &raw in polys {
        let corner = PolygonVertex::new(raw);
        let id = corner.to_u32();
        if id as usize >= verts.len() {
            return Err(format!("FBX polygon index {id} is out of range"));
        }
        face.push(base + id);
        if corner.is_end() {
            push_fan(&face, indices);
            face.clear();
        }
    }
    if !face.is_empty() {
        return Err("FBX polygon list is missing its closing vertex".into());
    }
    Ok(())
}

fn push_fan(face: &[u32], indices: &mut Vec<u32>) {
    if face.len() < 3 {
        return;
    }
    for k in 1..face.len() - 1 {
        indices.extend_from_slice(&[face[0], face[k], face[k + 1]]);
    }
}

fn surface_from_parts(positions: Vec<Vec3>, indices: Vec<u32>) -> Result<Surface, String> {
    if positions.is_empty() || indices.is_empty() {
        return Err("FBX file contains no polygon mesh".into());
    }
    let mut faces = Faces::new(3, indices.len() / 3);
    faces.idx = indices;
    Ok(Surface {
        faces,
        vertices: positions,
        normals: Vec::new(),
    })
}

fn polygons_from_output(mesh: &OutputMesh) -> Result<Vec<Vec<u32>>, String> {
    let degree = mesh.faces.degree;
    let mut irregular: BTreeMap<u32, BTreeMap<u32, u32>> = BTreeMap::new();
    let mut polygons = Vec::new();
    for f in 0..mesh.faces.cols() {
        if degree == 4 && mesh.faces.get(2, f) == mesh.faces.get(3, f) {
            irregular
                .entry(mesh.faces.get(2, f))
                .or_default()
                .insert(mesh.faces.get(0, f), mesh.faces.get(1, f));
            continue;
        }
        let mut poly = Vec::with_capacity(degree);
        for j in 0..degree {
            poly.push(mesh.faces.get(j, f));
        }
        polygons.push(poly);
    }
    for edges in irregular.values() {
        let start = *edges.keys().next().ok_or("Broken irregular face chain")?;
        let mut poly = Vec::new();
        let mut v = start;
        for _ in 0..=edges.len() {
            poly.push(v);
            v = *edges.get(&v).ok_or("Broken irregular face chain")?;
            if v == start {
                break;
            }
        }
        if poly.len() >= 3 {
            polygons.push(poly);
        }
    }
    if polygons.is_empty() {
        return Err("Mesh has no faces to write".into());
    }
    Ok(polygons)
}

#[derive(Clone, Copy)]
struct Xform {
    translation: [f64; 3],
    rotation: [f64; 3],
    scale: [f64; 3],
    order: i32,
    pre_rotation: [f64; 3],
    post_rotation: [f64; 3],
    geometric_translation: [f64; 3],
    geometric_rotation: [f64; 3],
    geometric_scale: [f64; 3],
}

impl Default for Xform {
    fn default() -> Self {
        Self {
            translation: [0.0, 0.0, 0.0],
            rotation: [0.0, 0.0, 0.0],
            scale: [1.0, 1.0, 1.0],
            order: 0,
            pre_rotation: [0.0, 0.0, 0.0],
            post_rotation: [0.0, 0.0, 0.0],
            geometric_translation: [0.0, 0.0, 0.0],
            geometric_rotation: [0.0, 0.0, 0.0],
            geometric_scale: [1.0, 1.0, 1.0],
        }
    }
}

impl Xform {
    fn matrix(self) -> Mat4 {
        let rotation = mul(
            euler(0, self.pre_rotation),
            mul(euler(self.order, self.rotation), transpose(euler(0, self.post_rotation))),
        );
        let local = mul(mul(Mat4::translation(self.translation), rotation), Mat4::scale(self.scale));
        let geometric = mul(
            mul(Mat4::translation(self.geometric_translation), euler(0, self.geometric_rotation)),
            Mat4::scale(self.geometric_scale),
        );
        mul(local, geometric)
    }
}

#[derive(Clone, Copy)]
struct Mat4 {
    m: [f64; 16],
}

impl Mat4 {
    fn identity() -> Self {
        Self {
            m: [
                1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
            ],
        }
    }

    fn translation(t: [f64; 3]) -> Self {
        let mut m = Self::identity();
        m.m[12] = t[0];
        m.m[13] = t[1];
        m.m[14] = t[2];
        m
    }

    fn scale(s: [f64; 3]) -> Self {
        let mut m = Self::identity();
        m.m[0] = s[0];
        m.m[5] = s[1];
        m.m[10] = s[2];
        m
    }

    fn transform(self, p: [f64; 3]) -> [f64; 3] {
        [
            self.m[0] * p[0] + self.m[4] * p[1] + self.m[8] * p[2] + self.m[12],
            self.m[1] * p[0] + self.m[5] * p[1] + self.m[9] * p[2] + self.m[13],
            self.m[2] * p[0] + self.m[6] * p[1] + self.m[10] * p[2] + self.m[14],
        ]
    }
}

fn mul(a: Mat4, b: Mat4) -> Mat4 {
    let mut out = [0.0; 16];
    for col in 0..4 {
        for row in 0..4 {
            out[col * 4 + row] = a.m[row] * b.m[col * 4]
                + a.m[4 + row] * b.m[col * 4 + 1]
                + a.m[8 + row] * b.m[col * 4 + 2]
                + a.m[12 + row] * b.m[col * 4 + 3];
        }
    }
    Mat4 { m: out }
}

fn transpose(m: Mat4) -> Mat4 {
    let mut out = [0.0; 16];
    for col in 0..4 {
        for row in 0..4 {
            out[row * 4 + col] = m.m[col * 4 + row];
        }
    }
    Mat4 { m: out }
}

fn rot_x(rad: f64) -> Mat4 {
    let (s, c) = rad.sin_cos();
    let mut m = Mat4::identity();
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    m
}

fn rot_y(rad: f64) -> Mat4 {
    let (s, c) = rad.sin_cos();
    let mut m = Mat4::identity();
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    m
}

fn rot_z(rad: f64) -> Mat4 {
    let (s, c) = rad.sin_cos();
    let mut m = Mat4::identity();
    m.m[0] = c;
    m.m[1] = s;
    m.m[4] = -s;
    m.m[5] = c;
    m
}

fn euler(order: i32, degrees: [f64; 3]) -> Mat4 {
    let rx = rot_x(degrees[0].to_radians());
    let ry = rot_y(degrees[1].to_radians());
    let rz = rot_z(degrees[2].to_radians());
    // Column vectors, intrinsic order: XYZ means Rx first, then Ry, then Rz.
    match order {
        1 => mul(ry, mul(rz, rx)),
        2 => mul(rx, mul(rz, ry)),
        3 => mul(rz, mul(rx, ry)),
        4 => mul(ry, mul(rx, rz)),
        5 => mul(rx, mul(ry, rz)),
        _ => mul(rz, mul(ry, rx)),
    }
}

struct AsciiNode {
    name: String,
    header: String,
    body: String,
    children: Vec<AsciiNode>,
}

fn parse_nodes(text: &str) -> Result<Vec<AsciiNode>, String> {
    let mut i = 0;
    parse_node_list(text, &mut i, true)
}

fn parse_node_list(text: &str, i: &mut usize, top: bool) -> Result<Vec<AsciiNode>, String> {
    let bytes = text.as_bytes();
    let mut nodes = Vec::new();
    loop {
        skip_ws_and_comments(text, i);
        if *i >= bytes.len() {
            break;
        }
        if bytes[*i] == b'}' {
            if !top {
                *i += 1;
            }
            break;
        }
        let name_start = *i;
        while *i < bytes.len() && (bytes[*i].is_ascii_alphanumeric() || bytes[*i] == b'_') {
            *i += 1;
        }
        if name_start == *i || *i >= bytes.len() || bytes[*i] != b':' {
            *i += 1;
            continue;
        }
        let name = text[name_start..*i].to_string();
        *i += 1;
        skip_spaces(text, i);
        let header_start = *i;
        let mut quote = false;
        while *i < bytes.len() {
            let c = bytes[*i];
            if c == b'"' {
                quote = !quote;
            } else if !quote && (c == b'{' || c == b'\n' || c == b'\r') {
                break;
            }
            *i += 1;
        }
        let header = text[header_start..*i].trim().to_string();
        skip_spaces(text, i);
        if *i < bytes.len() && bytes[*i] == b'{' {
            *i += 1;
            let body_start = *i;
            let children = parse_node_list(text, i, false)?;
            let body = text[body_start..(*i).saturating_sub(1)].to_string();
            nodes.push(AsciiNode {
                name,
                header,
                body,
                children,
            });
        } else {
            nodes.push(AsciiNode {
                name,
                header,
                body: String::new(),
                children: Vec::new(),
            });
        }
    }
    Ok(nodes)
}

fn skip_spaces(text: &str, i: &mut usize) {
    let bytes = text.as_bytes();
    while *i < bytes.len() && (bytes[*i] == b' ' || bytes[*i] == b'\t') {
        *i += 1;
    }
}

fn skip_ws_and_comments(text: &str, i: &mut usize) {
    let bytes = text.as_bytes();
    loop {
        while *i < bytes.len() && bytes[*i].is_ascii_whitespace() {
            *i += 1;
        }
        if *i < bytes.len() && bytes[*i] == b';' {
            while *i < bytes.len() && bytes[*i] != b'\n' {
                *i += 1;
            }
            continue;
        }
        break;
    }
}

fn leading_id(header: &str) -> Option<i64> {
    let token = header.split(',').next()?.trim();
    token.parse().ok()
}

fn header_contains(header: &str, word: &str) -> bool {
    split_csv(header).iter().any(|f| unquote(f) == word)
}

fn numbers_in(text: &str) -> Vec<f64> {
    let mut out = Vec::new();
    let mut current = String::new();
    for c in text.chars() {
        if c.is_ascii_digit() || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' {
            current.push(c);
        } else if !current.is_empty() {
            if let Ok(v) = current.parse::<f64>() {
                out.push(v);
            }
            current.clear();
        }
    }
    if let Ok(v) = current.parse::<f64>() {
        out.push(v);
    }
    out
}

fn split_csv(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut current = String::new();
    let mut quote = false;
    for c in text.chars() {
        if c == '"' {
            quote = !quote;
            current.push(c);
        } else if c == ',' && !quote {
            out.push(current.trim().to_string());
            current.clear();
        } else {
            current.push(c);
        }
    }
    if !current.trim().is_empty() {
        out.push(current.trim().to_string());
    }
    out
}

fn unquote(text: &str) -> String {
    let text = text.trim();
    text.trim_matches('"').to_string()
}

enum Prop {
    Bool(bool),
    I32(i32),
    I64(i64),
    F64(f64),
    String(Vec<u8>),
    Raw(Vec<u8>),
    ArrI32(Vec<i32>),
    ArrF64(Vec<f64>),
}

struct Node {
    name: &'static str,
    props: Vec<Prop>,
    children: Vec<Node>,
}

fn scene_node(mesh: &OutputMesh, polygons: &[Vec<u32>], xform: Xform) -> Node {
    let mut vertices = Vec::with_capacity(mesh.vertices.len() * 3);
    for v in &mesh.vertices {
        vertices.extend_from_slice(&[v.x as f64, v.y as f64, v.z as f64]);
    }
    let mut poly = Vec::new();
    for face in polygons {
        for (i, &idx) in face.iter().enumerate() {
            if i + 1 == face.len() {
                poly.push(!(idx as i32));
            } else {
                poly.push(idx as i32);
            }
        }
    }
    Node {
        name: "",
        props: Vec::new(),
        children: vec![
            header_extension(),
            file_id_node(),
            creation_time_node(),
            global_settings(),
            documents_node(),
            definitions_node(),
            objects_node(&vertices, &poly, xform),
            connections_node(),
        ],
    }
}

fn header_extension() -> Node {
    Node {
        name: "FBXHeaderExtension",
        props: Vec::new(),
        children: vec![
            leaf("FBXHeaderVersion", vec![Prop::I32(1003)]),
            leaf("FBXVersion", vec![Prop::I32(FBX_VERSION as i32)]),
            Node {
                name: "CreationTimeStamp",
                props: Vec::new(),
                children: vec![
                    leaf("Version", vec![Prop::I32(1000)]),
                    leaf("Year", vec![Prop::I32(1970)]),
                    leaf("Month", vec![Prop::I32(1)]),
                    leaf("Day", vec![Prop::I32(1)]),
                    leaf("Hour", vec![Prop::I32(10)]),
                    leaf("Minute", vec![Prop::I32(0)]),
                    leaf("Second", vec![Prop::I32(0)]),
                    leaf("Millisecond", vec![Prop::I32(0)]),
                ],
            },
            leaf("Creator", vec![Prop::String(b"instant-meshes".to_vec())]),
        ],
    }
}

fn file_id_node() -> Node {
    leaf(
        "FileId",
        vec![Prop::Raw(vec![
            0x28, 0xb3, 0x2a, 0xeb, 0xb6, 0x24, 0xcc, 0xc2, 0xbf, 0xc8, 0xb0, 0x2a, 0xa9, 0x2b,
            0xfc, 0xf1,
        ])],
    )
}

fn creation_time_node() -> Node {
    leaf(
        "CreationTime",
        vec![Prop::String(b"1970-01-01 10:00:00:000".to_vec())],
    )
}

fn global_settings() -> Node {
    Node {
        name: "GlobalSettings",
        props: Vec::new(),
        children: vec![
            leaf("Version", vec![Prop::I32(1000)]),
            Node {
                name: "Properties70",
                props: Vec::new(),
                children: vec![
                    p_i32("UpAxis", 1),
                    p_i32("UpAxisSign", 1),
                    p_i32("FrontAxis", 2),
                    p_i32("FrontAxisSign", 1),
                    p_i32("CoordAxis", 0),
                    p_i32("CoordAxisSign", 1),
                    p_f64("UnitScaleFactor", 1.0),
                ],
            },
        ],
    }
}

fn documents_node() -> Node {
    Node {
        name: "Documents",
        props: Vec::new(),
        children: vec![
            leaf("Count", vec![Prop::I32(1)]),
            Node {
                name: "Document",
                props: vec![
                    Prop::I64(3),
                    Prop::String(b"Document::Scene\x00\x01Document".to_vec()),
                    Prop::String(b"Scene".to_vec()),
                ],
                children: vec![
                    Node {
                        name: "Properties70",
                        props: Vec::new(),
                        children: vec![
                            leaf(
                                "P",
                                vec![
                                    Prop::String(b"SourceObject".to_vec()),
                                    Prop::String(b"object".to_vec()),
                                    Prop::String(Vec::new()),
                                    Prop::String(Vec::new()),
                                ],
                            ),
                            leaf(
                                "P",
                                vec![
                                    Prop::String(b"ActiveAnimStackName".to_vec()),
                                    Prop::String(b"KString".to_vec()),
                                    Prop::String(Vec::new()),
                                    Prop::String(Vec::new()),
                                    Prop::String(Vec::new()),
                                ],
                            ),
                        ],
                    },
                    leaf("RootNode", vec![Prop::I64(0)]),
                ],
            },
        ],
    }
}

fn definitions_node() -> Node {
    Node {
        name: "Definitions",
        props: Vec::new(),
        children: vec![
            leaf("Version", vec![Prop::I32(100)]),
            leaf("Count", vec![Prop::I32(2)]),
            Node {
                name: "ObjectType",
                props: vec![Prop::String(b"Geometry".to_vec())],
                children: vec![leaf("Count", vec![Prop::I32(1)])],
            },
            Node {
                name: "ObjectType",
                props: vec![Prop::String(b"Model".to_vec())],
                children: vec![leaf("Count", vec![Prop::I32(1)])],
            },
        ],
    }
}

fn objects_node(vertices: &[f64], polygons: &[i32], xform: Xform) -> Node {
    Node {
        name: "Objects",
        props: Vec::new(),
        children: vec![
            Node {
                name: "Geometry",
                props: vec![
                    Prop::I64(1),
                    Prop::String(b"Geometry::Mesh\x00\x01Geometry".to_vec()),
                    Prop::String(b"Mesh".to_vec()),
                ],
                children: vec![
                    leaf("GeometryVersion", vec![Prop::I32(124)]),
                    leaf("Vertices", vec![Prop::ArrF64(vertices.to_vec())]),
                    leaf("PolygonVertexIndex", vec![Prop::ArrI32(polygons.to_vec())]),
                ],
            },
            Node {
                name: "Model",
                props: vec![
                    Prop::I64(2),
                    Prop::String(b"Model::Mesh\x00\x01Model".to_vec()),
                    Prop::String(b"Mesh".to_vec()),
                ],
                children: vec![
                    leaf("Version", vec![Prop::I32(232)]),
                    Node {
                        name: "Properties70",
                        props: Vec::new(),
                        children: vec![
                            p_vec3("Lcl Translation", "Lcl Translation", xform.translation),
                            p_vec3("Lcl Rotation", "Lcl Rotation", xform.rotation),
                            p_vec3("Lcl Scaling", "Lcl Scaling", xform.scale),
                            p_vec3("PreRotation", "PreRotation", xform.pre_rotation),
                            p_vec3("PostRotation", "PostRotation", xform.post_rotation),
                            p_vec3(
                                "GeometricTranslation",
                                "GeometricTranslation",
                                xform.geometric_translation,
                            ),
                            p_vec3(
                                "GeometricRotation",
                                "GeometricRotation",
                                xform.geometric_rotation,
                            ),
                            p_vec3("GeometricScaling", "GeometricScaling", xform.geometric_scale),
                            p_enum("RotationOrder", xform.order),
                        ],
                    },
                    leaf("Shading", vec![Prop::Bool(true)]),
                    leaf("Culling", vec![Prop::String(b"CullingOff".to_vec())]),
                ],
            },
        ],
    }
}

fn connections_node() -> Node {
    Node {
        name: "Connections",
        props: Vec::new(),
        children: vec![
            leaf(
                "C",
                vec![
                    Prop::String(b"OO".to_vec()),
                    Prop::I64(2),
                    Prop::I64(0),
                ],
            ),
            leaf(
                "C",
                vec![
                    Prop::String(b"OO".to_vec()),
                    Prop::I64(1),
                    Prop::I64(2),
                ],
            ),
        ],
    }
}

fn leaf(name: &'static str, props: Vec<Prop>) -> Node {
    Node {
        name,
        props,
        children: Vec::new(),
    }
}

fn p_vec3(name: &'static str, ty: &'static str, value: [f64; 3]) -> Node {
    leaf(
        "P",
        vec![
            Prop::String(name.as_bytes().to_vec()),
            Prop::String(ty.as_bytes().to_vec()),
            Prop::String(Vec::new()),
            Prop::String(b"A".to_vec()),
            Prop::F64(value[0]),
            Prop::F64(value[1]),
            Prop::F64(value[2]),
        ],
    )
}

fn p_i32(name: &'static str, value: i32) -> Node {
    leaf(
        "P",
        vec![
            Prop::String(name.as_bytes().to_vec()),
            Prop::String(b"int".to_vec()),
            Prop::String(b"Integer".to_vec()),
            Prop::String(Vec::new()),
            Prop::I32(value),
        ],
    )
}

fn p_enum(name: &'static str, value: i32) -> Node {
    leaf(
        "P",
        vec![
            Prop::String(name.as_bytes().to_vec()),
            Prop::String(b"enum".to_vec()),
            Prop::String(Vec::new()),
            Prop::String(Vec::new()),
            Prop::I32(value),
        ],
    )
}

fn p_f64(name: &'static str, value: f64) -> Node {
    leaf(
        "P",
        vec![
            Prop::String(name.as_bytes().to_vec()),
            Prop::String(b"double".to_vec()),
            Prop::String(b"Number".to_vec()),
            Prop::String(Vec::new()),
            Prop::F64(value),
        ],
    )
}

fn encode_binary(root: &Node) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(FBX_MAGIC);
    out.extend_from_slice(&FBX_VERSION.to_le_bytes());
    let start = out.len();
    let mut cursor = start;
    for (i, child) in root.children.iter().enumerate() {
        let last = i + 1 == root.children.len();
        cursor = write_node(&mut out, child, cursor, last);
    }
    out.extend_from_slice(&[0u8; 13]);
    out.extend_from_slice(&[
        0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66, 0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26,
        0x7e,
    ]);
    out.extend_from_slice(&[0u8; 4]);
    let pad = {
        let ofs = out.len();
        let mut p = (16 - (ofs % 16)) % 16;
        if p == 0 {
            p = 16;
        }
        p
    };
    out.extend(std::iter::repeat(0).take(pad));
    out.extend_from_slice(&FBX_VERSION.to_le_bytes());
    out.extend_from_slice(&[0u8; 120]);
    out.extend_from_slice(&[
        0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e, 0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29,
        0x0b,
    ]);
    let _ = cursor;
    out
}

fn node_span(node: &Node, is_last: bool) -> usize {
    let props_len: usize = node.props.iter().map(|p| 1 + prop_payload_len(p)).sum();
    let mut size = 12 + 1 + node.name.len() + props_len;
    if !node.children.is_empty() {
        for (i, child) in node.children.iter().enumerate() {
            size += node_span(child, i + 1 == node.children.len());
        }
        size += 13;
    } else if node.props.is_empty() && !is_last {
        size += 13;
    }
    size
}

fn prop_payload_len(prop: &Prop) -> usize {
    match prop {
        Prop::Bool(_) => 1,
        Prop::I32(_) => 4,
        Prop::I64(_) => 8,
        Prop::F64(_) => 8,
        Prop::String(v) | Prop::Raw(v) => 4 + v.len(),
        Prop::ArrI32(v) => 12 + v.len() * 4,
        Prop::ArrF64(v) => 12 + v.len() * 8,
    }
}

fn write_node(out: &mut Vec<u8>, node: &Node, at: usize, is_last: bool) -> usize {
    let span = node_span(node, is_last);
    let end = at + span;
    let props_len: u32 = node
        .props
        .iter()
        .map(|p| 1 + prop_payload_len(p))
        .sum::<usize>() as u32;
    out.extend_from_slice(&(end as u32).to_le_bytes());
    out.extend_from_slice(&(node.props.len() as u32).to_le_bytes());
    out.extend_from_slice(&props_len.to_le_bytes());
    out.push(node.name.len() as u8);
    out.extend_from_slice(node.name.as_bytes());
    for prop in &node.props {
        write_prop(out, prop);
    }
    if !node.children.is_empty() {
        let mut cursor = out.len();
        for (i, child) in node.children.iter().enumerate() {
            cursor = write_node(out, child, cursor, i + 1 == node.children.len());
        }
        out.extend_from_slice(&[0u8; 13]);
    } else if node.props.is_empty() && !is_last {
        out.extend_from_slice(&[0u8; 13]);
    }
    debug_assert_eq!(out.len(), end);
    end
}

fn write_prop(out: &mut Vec<u8>, prop: &Prop) {
    match prop {
        Prop::Bool(v) => {
            out.push(b'C');
            out.push(u8::from(*v));
        }
        Prop::I32(v) => {
            out.push(b'I');
            out.extend_from_slice(&v.to_le_bytes());
        }
        Prop::I64(v) => {
            out.push(b'L');
            out.extend_from_slice(&v.to_le_bytes());
        }
        Prop::F64(v) => {
            out.push(b'D');
            out.extend_from_slice(&v.to_le_bytes());
        }
        Prop::String(v) => {
            out.push(b'S');
            out.extend_from_slice(&(v.len() as u32).to_le_bytes());
            out.extend_from_slice(v);
        }
        Prop::Raw(v) => {
            out.push(b'R');
            out.extend_from_slice(&(v.len() as u32).to_le_bytes());
            out.extend_from_slice(v);
        }
        Prop::ArrI32(v) => {
            out.push(b'i');
            write_raw_array(out, v.len(), &i32s_to_bytes(v));
        }
        Prop::ArrF64(v) => {
            out.push(b'd');
            write_raw_array(out, v.len(), &f64s_to_bytes(v));
        }
    }
}

fn write_raw_array(out: &mut Vec<u8>, count: usize, bytes: &[u8]) {
    out.extend_from_slice(&(count as u32).to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&(bytes.len() as u32).to_le_bytes());
    out.extend_from_slice(bytes);
}

fn i32s_to_bytes(values: &[i32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(values.len() * 4);
    for v in values {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out
}

fn f64s_to_bytes(values: &[f64]) -> Vec<u8> {
    let mut out = Vec::with_capacity(values.len() * 8);
    for v in values {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mesh::Faces;

    fn triangle_mesh() -> OutputMesh {
        let mut faces = Faces::new(3, 1);
        faces.set(0, 0, 0);
        faces.set(1, 0, 1);
        faces.set(2, 0, 2);
        OutputMesh {
            faces,
            vertices: vec![
                Vec3::new(1.0, 0.0, 0.0),
                Vec3::new(0.0, 1.0, 0.0),
                Vec3::new(0.0, 0.0, 1.0),
            ],
            normals: Vec::new(),
            face_normals: Vec::new(),
        }
    }

    fn quad_mesh() -> OutputMesh {
        let mut faces = Faces::new(4, 1);
        faces.set(0, 0, 0);
        faces.set(1, 0, 1);
        faces.set(2, 0, 2);
        faces.set(3, 0, 3);
        OutputMesh {
            faces,
            vertices: vec![
                Vec3::new(0.0, 0.0, 0.0),
                Vec3::new(1.0, 0.0, 0.0),
                Vec3::new(1.0, 0.0, 1.0),
                Vec3::new(0.0, 0.0, 1.0),
            ],
            normals: Vec::new(),
            face_normals: Vec::new(),
        }
    }

    #[test]
    fn binary_roundtrip_keeps_quads_as_two_triangles() {
        let path = std::env::temp_dir().join("instant-meshes-quad.fbx");
        write_fbx(&path, &quad_mesh()).unwrap();
        let loaded = load_fbx(&path).unwrap();
        assert_eq!(loaded.vertices.len(), 4);
        assert_eq!(loaded.faces.cols(), 2);
        assert!((loaded.vertices[1].x - 1.0).abs() < 1e-5);
    }

    #[test]
    fn binary_bakes_translation_and_z_rotation() {
        let mesh = triangle_mesh();
        let mut xform = Xform::default();
        xform.translation = [5.0, -2.0, 3.0];
        xform.rotation = [0.0, 0.0, 90.0];
        let polygons = polygons_from_output(&mesh).unwrap();
        let bytes = encode_binary(&scene_node(&mesh, &polygons, xform));
        let path = std::env::temp_dir().join("instant-meshes-xform.fbx");
        std::fs::write(&path, bytes).unwrap();
        let loaded = load_fbx(&path).unwrap();
        let p = loaded.vertices[0];
        assert!((p.x - 5.0).abs() < 1e-4, "x={}", p.x);
        assert!((p.y - (-1.0)).abs() < 1e-4, "y={}", p.y);
        assert!((p.z - 3.0).abs() < 1e-4, "z={}", p.z);
    }

    #[test]
    fn ascii_fbx_reads_geometry_and_translation() {
        let text = r#"; FBX 7.4.0 project file
Objects:  {
    Geometry: 10, "Geometry::Tri", "Mesh" {
        Vertices: *9 {
            a: 1,0,0, 0,1,0, 0,0,1
        }
        PolygonVertexIndex: *3 {
            a: 0,1,-3
        }
    }
    Model: 20, "Model::Tri", "Mesh" {
        Properties70:  {
            P: "Lcl Translation", "Lcl Translation", "", "A",10,0,0
        }
    }
}
Connections:  {
    C: "OO",10,20
    C: "OO",20,0
}
"#;
        let loaded = load_ascii(text).unwrap();
        assert_eq!(loaded.faces.cols(), 1);
        assert!((loaded.vertices[0].x - 11.0).abs() < 1e-5);
        assert!((loaded.vertices[1].y - 1.0).abs() < 1e-5);
    }
}
