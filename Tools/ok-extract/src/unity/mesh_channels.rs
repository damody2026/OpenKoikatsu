//! Vertex channels of Unity meshes: stream formats, positions, normals and tangents.
use super::*;

pub(crate) fn format_size(fmt: u8) -> usize {
    // Unity 2019+ VertexFormat: 0 Float32, 1 Float16, 2 UNorm8, 8 UInt16, 9 SInt16, 10 UInt32, 11 SInt32.
    match fmt {
        0 | 10 | 11 | 12 => 4,
        1 | 4 | 5 | 8 | 9 => 2,
        _ => 1,
    }
}

pub(crate) fn read_f32(data: &[u8], off: usize) -> f32 {
    let Ok(b) = data.get(off..off + 4).unwrap_or(&[]).try_into() else {
        return 0.0;
    };
    f32::from_le_bytes(b)
}

/// Unity VertexFormat scalar conversion, shared by geometry and texture coords.
/// Channel stride/offset is in bytes; Float16 must advance by two, not four.
pub(crate) fn read_vertex_component(data: &[u8], off: usize, format: u8) -> f32 {
    let u16_value=||data.get(off..off+2).and_then(|s|s.try_into().ok()).map(u16::from_le_bytes).unwrap_or(0);
    match format {
        0 => read_f32(data,off),
        1 => f16_to_f32(u16_value()),
        2 => data.get(off).copied().unwrap_or(0) as f32 / 255.,
        3 => ((data.get(off).copied().unwrap_or(0) as i8) as f32 / 127.).max(-1.),
        4 => u16_value() as f32 / 65535.,
        5 => (u16_value() as i16 as f32 / 32767.).max(-1.),
        6 => data.get(off).copied().unwrap_or(0) as f32,
        7 => data.get(off).copied().unwrap_or(0) as i8 as f32,
        8 => u16_value() as f32,
        9 => u16_value() as i16 as f32,
        10 => read_u32_le(data,off) as f32,
        11 => read_u32_le(data,off) as i32 as f32,
        _ => 0.,
    }
}

/// Read a blend weight from a `VertexAttributeFormat`/dimension-packed channel.
pub(crate) fn read_attr_weight(data: &[u8], off: usize, fmt: u8) -> f32 {
    match fmt {
        0 => read_f32(data, off),
        1 => f16_to_f32(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).map(u16::from_le_bytes).unwrap_or(0)),
        2 => data.get(off).copied().unwrap_or(0) as f32 / 255.0,
        6 => data.get(off).copied().unwrap_or(0) as f32,
        8 => u16::from_le_bytes(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).unwrap_or([0, 0])) as f32,
        10 => read_u32_le(data, off) as f32,
        _ => 0.0,
    }
}

/// Read a bone index from a packed channel (clamped to u16; indices are small).
pub(crate) fn read_attr_bone(data: &[u8], off: usize, fmt: u8) -> u16 {
    let v = match fmt {
        0 => read_f32(data, off) as u32,
        1 => f16_to_f32(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).map(u16::from_le_bytes).unwrap_or(0)) as u32,
        2 | 3 | 6 | 7 => data.get(off).copied().unwrap_or(0) as u32,
        4 | 5 | 8 | 9 => u16::from_le_bytes(data.get(off..off + 2).and_then(|s| <[u8; 2]>::try_from(s).ok()).unwrap_or([0, 0])) as u32,
        10 | 11 => read_u32_le(data, off),
        _ => 0,
    };
    v.min(u16::MAX as u32) as u16
}

pub(crate) fn f16_to_f32(bits: u16) -> f32 {
    let sign = (bits >> 15) as u32;
    let exp = ((bits >> 10) & 0x1F) as u32;
    let mant = (bits & 0x3FF) as u32;
    let f = if exp == 0 {
        (mant as f32) * 2f32.powi(-24)
    } else if exp == 31 {
        if mant == 0 { f32::INFINITY } else { f32::NAN }
    } else {
        (1.0 + mant as f32 / 1024.0) * 2f32.powi(exp as i32 - 15)
    };
    if sign == 1 { -f } else { f }
}

pub(crate) fn decode_positions(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 3]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() || chs.is_empty() || chs[0].dimension < 3 {
        return Vec::new();
    }
    let (strides, stream_off) = channel_layout(mesh);
    let c0 = &chs[0];
    let stride = strides[c0.stream as usize];
    let base = stream_off[c0.stream as usize];
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let o = base + i * stride + c0.offset as usize;
        out.push(std::array::from_fn(|axis|read_vertex_component(data,o+axis*format_size(c0.format),c0.format)));
    }
    out
}

/// Read the Unity normal channel (VertexAttribute index 1). Unity stores the
/// mesh's authored normals here; Illusion faces have interior cavity triangles
/// so geometric normals cannot reproduce them.
pub(crate) fn decode_normals(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 3]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() || chs.len() < 2 {
        return Vec::new();
    }
    let c = &chs[1];
    if c.dimension < 3 {
        return Vec::new();
    }
    let (strides, stream_off) = channel_layout(mesh);
    let s = c.stream as usize;
    if s >= strides.len() {
        return Vec::new();
    }
    let stride = strides[s];
    let base = stream_off[s];
    if stride == 0 || base + n * stride > data.len() {
        return Vec::new();
    }
    let off = c.offset as usize;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let o = base + i * stride + off;
        let mut v: [f32;3] = std::array::from_fn(|axis|read_vertex_component(data,o+axis*format_size(c.format),c.format));
        let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
        v = if l > 1e-8 {
            [v[0] / l, v[1] / l, v[2] / l]
        } else {
            [0.0, 0.0, 1.0]
        };
        out.push(v);
    }
    out
}

/// Unity's newer channel layout puts Tangent at 2; the older layout used by
/// KKS SAC bodies puts Color32 at 2 and Tangent at 7. Accept only a four-float
/// tangent channel here, never the four-byte color channel.
pub(crate) fn decode_tangents(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 4]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let Some(c) = [2, 7].iter().filter_map(|&i| mesh.vertex_data.channels.get(i))
        .find(|c| c.dimension == 4 && matches!(c.format, 0 | 1)) else { return Vec::new() };
    if n == 0 { return Vec::new() }
    let data = &mesh.vertex_data.data_size;
    let (strides, offsets) = channel_layout(mesh);
    let stream = c.stream as usize;
    if stream >= strides.len() || strides[stream] == 0 { return Vec::new() }
    let stride = strides[stream];
    let base = offsets[stream] + c.offset as usize;
    let component_size = format_size(c.format);
    if base + (n - 1) * stride + 4 * component_size > data.len() { return Vec::new() }
    let read = |off: usize| -> f32 { if c.format == 0 { read_f32(data, off) }
        else { f16_to_f32(u16::from_le_bytes(data[off..off + 2].try_into().unwrap())) } };
    let mut result = Vec::with_capacity(n);
    for i in 0..n {
        let off = base + i * stride;
        let t = std::array::from_fn(|axis| read(off + axis * component_size));
        if !t.iter().all(|v| v.is_finite()) ||
            !(0.25..=1.75).contains(&(t[0] * t[0] + t[1] * t[1] + t[2] * t[2])) ||
            t[3].abs() < 0.5 { return Vec::new() }
        result.push(t);
    }

    result
}
