//! Mesh data of Unity bundles: UVs and colours, skin weights and indices, skin objects and blend shapes.
use super::*;

pub(crate) fn decode_uvs(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[f32; 2]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() || chs.len() < 5 {
        return vec![[0.0, 0.0]; n];
    }
    let (strides, stream_off) = channel_layout(mesh);
    // Illusion meshes store the main texture UV in an arbitrary TexCoord slot
    // (o_body_a uses TexCoord1 while TexCoord0 is garbage). Pick the channel
    // whose values look like real UVs.
    let mut best: Option<(f32, Vec<[f32; 2]>)> = None;
    // Tiled UVs (a 10 m Studio floor quad runs 0..10) fail the narrow range below; when no channel passes it, the
    // first channel whose every value is finite and within a wide range is the UV (it was all zeros before).
    let mut tiled: Option<Vec<[f32; 2]>> = None;
    // The main texture UV can sit in any 2-component channel: the face uses
    // TexCoord0..3 (channels 4-7), the body uses texcoord1 (channel 4 with a
    // 4-byte offset), the pantshot uses channel 3. Scan every 2-component
    // channel and keep the one that looks most like UVs.
    for (ci, c) in chs.iter().enumerate() {
        let _ = ci;
        if c.dimension != 2 || c.stream as usize >= strides.len() {
            continue;
        }
        let stride = strides[c.stream as usize];
        let base = stream_off[c.stream as usize];
        if stride == 0 || base + n * stride > data.len() {
            continue;
        }
        let off = c.offset as usize;
        // Some meshes pad the texture streams to a 16-byte boundary, so the
        // first vertex reads (0,0) and every later vertex takes its
        // predecessor's UV. Try the channel offset and offset+8 and keep the
        // reading with fewer untouched (exactly zero) vertices.
        let mut local_best: Option<(usize, usize, [f32; 2], Vec<[f32; 2]>)> = None;
        for extra in [0usize, 8usize] {
            // In an interleaved record, offset+8 is the next channel (uv0 at 40 reads uv1 at 48), not padding.
            if extra != 0 && off + extra + 2 * format_size(c.format) <= stride && !UV_LEGACY_PADDING.with(|f| f.get()) {
                continue;
            }
            if base + extra + (n.saturating_sub(1)) * stride + off + 2*format_size(c.format) > data.len() {
                continue;
            }
            let mut uv = Vec::with_capacity(n);
            let mut good = 0usize;
            let mut zeros = 0usize;
            let mut sum = 0.0f64;
            for i in 0..n {
                let o = base + extra + i * stride + off;
                let u = read_vertex_component(data, o, c.format);
                let v = read_vertex_component(data, o + format_size(c.format), c.format);
                // Illusion atlas UVs may be offset outside [0,1] (e.g. the
                // HEXA headband mesh uses V ~ -0.37), so accept a wide but
                // still finite/plausible range.
                if u.is_finite()
                    && v.is_finite()
                    && (-8.0..=8.0).contains(&u)
                    && (-8.0..=8.0).contains(&v)
                {
                    good += 1;
                    sum += u.abs() as f64 + v.abs() as f64;
                }
                if u == 0.0 && v == 0.0 {
                    zeros += 1;
                }
                uv.push([u, v]);
            }
            let cand = (zeros, good, [extra as f32, 0.0], uv);
            if local_best
                .as_ref()
                .map(|b| (cand.0, std::cmp::Reverse(cand.1)) < (b.0, std::cmp::Reverse(b.1)))
                .unwrap_or(true)
            {
                local_best = Some(cand);
            }
        }
        let Some((_zeros, good, _ext, uv)) = local_best else { continue };
        let score = good as f32 / n as f32;
        // Require a non-degenerate UV set (not all zeros).
        let total: f64 = uv.iter().map(|p| p[0].abs() as f64 + p[1].abs() as f64).sum();
        let not_zero = total > 1.0;
        if tiled.is_none() && not_zero && uv.iter().all(|p| p.iter().all(|v| v.is_finite() && v.abs() <= 4096.0)) { tiled = Some(uv.clone()); }
        if not_zero && best.as_ref().map(|b| score > b.0).unwrap_or(true) {
            best = Some((score, uv));
        }
    }
    if std::env::var("OK_UVDIAG").is_ok() {
        let info: Vec<String> = chs
            .iter()
            .enumerate()
            .map(|(ci, c)| {
                format!("c{ci}:s{}o{}f{}d{}", c.stream, c.offset, c.format, c.dimension)
            })
            .collect();
        eprintln!(
            "ok-extract: UVDIAG {} n={n} strides={strides:?} soff={stream_off:?} [{}]",
            mesh.name,
            info.join(" ")
        );
        // A small mesh: every 2-component channel's raw values.
        if n <= 8 {
            for (ci, c) in chs.iter().enumerate().filter(|(_, c)| c.dimension == 2 && (c.stream as usize) < strides.len()) {
                let (stride, base) = (strides[c.stream as usize], stream_off[c.stream as usize]);
                let values: Vec<[f32; 2]> = (0..n).map(|i| { let o = base + i * stride + c.offset as usize; [read_vertex_component(data, o, c.format), read_vertex_component(data, o + format_size(c.format), c.format)] }).collect();
                eprintln!("ok-extract: UVDIAG {} channel {ci} values {values:?}", mesh.name);
            }
        }
    }
    if std::env::var("OK_UVPEEK").is_ok() && mesh.name.to_ascii_lowercase().contains("hitomi") {
        let s = chs[4].stream as usize;
        let b = stream_off[s] as isize;
        for delta in [-16isize, -8, 0, 8, 16] {
            let o = b + delta;
            if o < 0 {
                continue;
            }
            let o = o as usize;
            let pairs: Vec<String> = (0..3)
                .map(|i| {
                    format!(
                        "({:.4},{:.4})",
                        read_f32(data, o + i * 8),
                        read_f32(data, o + i * 8 + 4)
                    )
                })
                .collect();
            eprintln!("ok-extract: UVPEEK {} base{delta:+} {}", mesh.name, pairs.join(" "));
        }
    }
    match best {
        Some((score, uv)) if score > 0.5 => uv,
        _ => tiled.filter(|_| !UV_NO_TILED_FALLBACK.with(|f| f.get())).unwrap_or_else(|| vec![[0.0, 0.0]; n]),
    }
}

pub(crate) fn decode_texcoord(mesh: &unity_asset_decode::mesh::Mesh, texcoord: usize, extra: usize) -> Vec<[f32; 2]> {
    decode_texcoord_slot(mesh,4+texcoord,extra)
}

pub(crate) fn texcoord_slot_base(version: &UnityVersion) -> usize {
    // Unity changed the serialized ShaderChannel enumeration in 2018:
    // pre-2018 COLOR=2/UV0=3/TANGENT=7; 2018+ TANGENT=2/COLOR=3/UV0=4.
    if version.major >= 2018 {4}else{3}
}

pub(crate) fn decode_texcoord_slot(mesh: &unity_asset_decode::mesh::Mesh, slot: usize, extra: usize) -> Vec<[f32;2]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if n == 0 || data.is_empty() {
        return Vec::new();
    }
    // Unity 2019 VertexAttribute: TexCoord0..7 occupy declared slots 4..11.
    // Filtering by dimension shifts sparse channels and misses float3/4 UVs.
    let Some(c) = chs.get(slot).filter(|c|c.dimension >= 2) else {
        return Vec::new();
    };
    let (strides, stream_off) = channel_layout(mesh);
    if c.stream as usize >= strides.len() {
        return Vec::new();
    }
    let stride = strides[c.stream as usize];
    let base = stream_off[c.stream as usize];
    if stride == 0 {
        return Vec::new();
    }
    let off = c.offset as usize;
    if base + extra + (n.saturating_sub(1)) * stride + off + 2*format_size(c.format) > data.len() {
        return Vec::new();
    }
    let mut uv = Vec::with_capacity(n);
    for i in 0..n {
        let o = base + extra + i * stride + off;
        uv.push([read_vertex_component(data,o,c.format),read_vertex_component(data,o+format_size(c.format),c.format)]);
    }
    uv
}

pub(crate) fn decode_vertex_colors(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<[u8; 4]> {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    // Unity Color32 is four UNorm8 components (format 2). Compressed meshes
    // shift its channel index, so locate it by format and dimension.
    let Some(c) = mesh.vertex_data.channels.iter().find(|c| c.dimension == 4 && c.format == 2) else {
        return Vec::new();
    };
    let (strides, offsets) = channel_layout(mesh);
    let s = c.stream as usize;
    if n == 0 || s >= strides.len() || strides[s] == 0 {
        return Vec::new();
    }
    let stride = strides[s];
    let base = offsets[s] + c.offset as usize;
    if base + (n - 1) * stride + 4 > data.len() {
        return Vec::new();
    }
    (0..n).map(|i| {
        let p = base + i * stride;
        [data[p], data[p + 1], data[p + 2], data[p + 3]]
    }).collect()
}

pub(crate) fn channel_layout(mesh: &unity_asset_decode::mesh::Mesh) -> (Vec<usize>, Vec<usize>) {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    let max_stream = chs.iter().map(|c| c.stream).max().unwrap_or(0) as usize;
    let mut strides = vec![0usize; max_stream + 1];
    for c in chs {
        if c.dimension == 0 {
            continue;
        }
        let sz = if c.format == 10 { 4 } else { format_size(c.format) };
        let end = c.offset as usize + sz * c.dimension as usize;
        let s = c.stream as usize;
        strides[s] = strides[s].max(end);
    }
    for s in &mut strides {
        *s = (*s + 3) & !3;
    }
    let mut stream_off = vec![0usize; strides.len()];
    let mut acc = 0usize;
    for i in 0..strides.len() {
        // Unity starts each nonempty vertex stream on a 16-byte boundary.
        // The stride itself stays 4-byte aligned. Without the inter-stream
        // gap, body COLOR0 reads UV data and UV1 becomes UV0.
        if strides[i] != 0 {
            acc = (acc + 15) & !15;
        }
        stream_off[i] = acc;
        acc += strides[i] * n;
    }
    // Inter-stream alignment gaps are not per-vertex stride. Only undeclared
    // whole records beyond the aligned layout extend the final stream (legacy
    // Illusion weight records); trailing alignment padding stays padding.
    if n > 0 && data.len() > acc {
        let extra=(data.len()-acc)/n;
        if extra > 0 {
            if let Some(last)=strides.iter().rposition(|&stride|stride>0) {strides[last]+=extra;}
        }
    }
    (strides, stream_off)
}

pub(crate) fn read_u32_le(data: &[u8], off: usize) -> u32 {
    data.get(off..off + 4)
        .and_then(|s| <[u8; 4]>::try_from(s).ok())
        .map(u32::from_le_bytes)
        .unwrap_or(0)
}

/// Illusion skin records are 32 bytes. Three encodings occur in practice:
/// 1 = `[w0][i0][i1][i2][i3][w1][w2][w3]` (native BoneWeight: body meshes),
/// 2 = `[w0][w1][w2][w3][i0][i1][i2][i3]` (weights then bone indices),
/// 3 = `[i0][i1][i2][i3][w0][w1][w2][w3]` (bone indices then weights).
pub(crate) fn parse_skin_record(rec: &[u8], l: u8) -> ([f32; 4], [u16; 4]) {
    let u = |k: usize| read_u32_le(rec, k * 4);
    let f = |k: usize| read_f32(rec, k * 4);
    let sanitize = |mut w: [f32; 4], idx: [u32; 4]| -> ([f32; 4], [u16; 4]) {
        let mut b = [0u16; 4];
        for k in 0..4 {
            if idx[k] > 255 {
                w[k] = 0.0;
                b[k] = 0;
            } else {
                b[k] = idx[k] as u16;
            }
        }
        (w, b)
    };
    match l {
        1 => sanitize([f(0), f(5), f(6), f(7)], [u(1), u(2), u(3), u(4)]),
        2 => sanitize([f(0), f(1), f(2), f(3)], [u(4), u(5), u(6), u(7)]),
        4 => sanitize([f(5), f(6), f(7), f(0)], [u(1), u(2), u(3), u(4)]),
        // `[i0][i1][i2][w0][w1][w2][w3][i3]`. Head-mod face meshes interleave
        // the last index after the weights; layout 2 then reads index bits as
        // weights and drops the chin/cheek influences (sum << 1).
        5 => sanitize([f(3), f(4), f(5), f(6)], [u(0), u(1), u(2), u(7)]),
        _ => sanitize([f(4), f(5), f(6), f(7)], [u(0), u(1), u(2), u(3)]),
    }
}

/// A correctly decoded record has four plausible weights and small bone
/// indices. Unused slots are 0xFFFFFFFF (Koikatsu face meshes); those are
/// already zeroed by `parse_skin_record`. A wrong layout reads float bits as
/// indices (huge ??weight 0) or index integers as weights (out of range).
pub(crate) fn skin_record_valid(w: &[f32; 4], b: &[u16; 4]) -> bool {
    w.iter().all(|v| v.is_finite() && *v >= -0.01 && *v <= 1.01)
        && {
            let s = w.iter().sum::<f32>();
            s > 0.5 && s < 1.5
        }
        && w.iter().zip(b.iter()).all(|(wi, bi)| *wi <= 0.02 || (*bi as u32) < 256)
}

pub(crate) fn decode_weights(mesh: &unity_asset_decode::mesh::Mesh) -> (Vec<[f32; 4]>, Vec<[u16; 4]>) {
    let n = mesh.vertex_data.vertex_count as usize;
    let data = &mesh.vertex_data.data_size;
    let chs = &mesh.vertex_data.channels;
    if std::env::var("OK_MAYUDIAG").is_ok() && mesh.name.to_ascii_lowercase().contains("mayu") {
        let info: Vec<String> = chs
            .iter()
            .enumerate()
            .map(|(i, c)| format!("{i}:s{}o{}f{}d{}", c.stream, c.offset, c.format, c.dimension))
            .collect();
        eprintln!(
            "ok-extract: SKINCH mayuge n={n} data={} chs={} [{}]",
            data.len(),
            chs.len(),
            info.join(" ")
        );
        eprintln!(
            "ok-extract: SKINCH mayuge compressed={} comp_weights={:?} comp_bones={:?}",
            mesh.compressed_mesh.is_some(),
            mesh.compressed_mesh.as_ref().map(|c| c.weights.data.len()),
            mesh.compressed_mesh.as_ref().map(|c| c.bone_indices.data.len()),
        );
    }
    if std::env::var("OK_SKINCH").is_ok() && n > 2000 {
        let info: Vec<String> = chs
            .iter()
            .enumerate()
            .map(|(i, c)| format!("{i}:s{}o{}f{}d{}", c.stream, c.offset, c.format, c.dimension))
            .collect();
        eprintln!("ok-extract: SKINCH n={n} chs={} [{}]", chs.len(), info.join(" "));
        eprintln!(
            "ok-extract: SKINCH compressed={:?} comp_weights={:?}",
            mesh.compressed_mesh.is_some(),
            mesh.compressed_mesh
                .as_ref()
                .map(|c| (c.weights.data.len(), c.bone_indices.data.len()))
        );
        if chs.len() < 14 && n == 9048 {
            let (strides, stream_off) = channel_layout(mesh);
            eprintln!(
                "ok-extract: SACREC n={n} data={} per_vert={} strides={:?} off={:?}",
                data.len(),
                data.len() / n.max(1),
                strides,
                stream_off
            );
            for i in 0..3 {
                let o0 = stream_off[0] + i * strides[0];
                let o1 = stream_off[1] + i * strides[1];
                if o0 + strides[0] <= data.len() && o1 + strides[1] <= data.len() {
                    eprintln!(
                        "ok-extract: SACREC v{i} s0={:?} s1={:?}",
                        &data[o0..o0 + strides[0]],
                        &data[o1..o1 + strides[1]]
                    );
                }
            }
        }
    }
    if n == 0 || data.is_empty() || chs.len() < 14 {
        return (Vec::new(), Vec::new());
    }
    let cw = &chs[12];
    let cb = &chs[13];
    // Meshes whose skin has fewer than four influences (e.g. `cf_O_mayuge`
    // ships two: ch12 = 2xf32 weights, ch13 = 2xu32 bone indices) are stored
    // as ordinary vertex channels rather than 32-byte Illusion records. Decode
    // those generically, padding to four influences.
    if (cw.dimension as usize) < 4 || (cb.dimension as usize) < 4 {
        if cw.stream != cb.stream || cw.dimension == 0 || cb.dimension == 0 {
            return (Vec::new(), Vec::new());
        }
        let (strides, stream_off) = channel_layout(mesh);
        let s = cw.stream as usize;
        let stride = strides[s];
        let base = stream_off[s];
        if stride == 0 || base + n * stride > data.len() {
            return (Vec::new(), Vec::new());
        }
        let infl = (cw.dimension as usize).min(cb.dimension as usize).min(4);
        let wsz = format_size(cw.format);
        let isz = format_size(cb.format);
        let mut weights = Vec::with_capacity(n);
        let mut bones = Vec::with_capacity(n);
        for i in 0..n {
            let wb = base + cw.offset as usize + i * stride;
            let bb = base + cb.offset as usize + i * stride;
            let mut w = [0.0f32; 4];
            let mut b = [0u16; 4];
            for k in 0..infl {
                w[k] = read_attr_weight(data, wb + k * wsz, cw.format);
                b[k] = read_attr_bone(data, bb + k * isz, cb.format);
            }
            weights.push(w);
            bones.push(b);
        }
        if std::env::var("OK_SKINCH").is_ok() || std::env::var("OK_MAYUDIAG").is_ok() {
            eprintln!(
                "ok-extract: SKINATTR {} n={n} infl={infl} stride={stride} w(s={} o={} fmt={} dim={}) b(s={} o={} fmt={} dim={}) v0=w{:?} b{:?}",
                mesh.name,
                cw.stream, cw.offset, cw.format, cw.dimension,
                cb.stream, cb.offset, cb.format, cb.dimension,
                weights[0], bones[0]
            );
        }
        return (weights, bones);
    }
    if cw.stream != 2 || cb.stream != 2 {
        return (Vec::new(), Vec::new());
    }
    let (strides, stream_off) = channel_layout(mesh);
    let s2 = cw.stream as usize;
    let stride = strides[s2];
    let base = stream_off[s2];
    if stride < 32 || base + n * stride > data.len() {
        return (Vec::new(), Vec::new());
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: DIAG skin bind(s={},off={},fmt={},dim={}) wt(s={},off={},fmt={},dim={}) stride={stride} base={base} n={n}",
            cb.stream, cb.offset, cb.format, cb.dimension,
            cw.stream, cw.offset, cw.format, cw.dimension
        );
    }
    // Pick the skin encoding and stream start together. Unity aligns vertex
    // streams to 16 bytes; summing channel strides can leave the computed
    // start 4/8/12 bytes before the first record. Some meshes also prefix a
    // stream with another 16 bytes. A misaligned record can still look valid
    // while rotating the four weights and losing one influence.
    let mut layout = 3u8;
    let mut best = 0usize;
    let mut best_sum_error = f64::INFINITY;
    let mut chosen_base = base;
    for off in (0usize..=28).step_by(4) {
        if base + off + n * stride > data.len() {
            continue;
        }
        for l in [1u8, 2, 3, 4] {
            let (c, sum_error) = (0..n)
                .fold((0usize, 0.0f64), |(valid, error), i| {
                    let o = base + off + i * stride;
                    let (w, b) = parse_skin_record(&data[o..o + 32], l);
                    if skin_record_valid(&w, &b) {
                        (valid + 1, error + (w.iter().sum::<f32>() - 1.0).abs() as f64)
                    } else {
                        (valid, error)
                    }
                });
            // Layouts 1 and 3 can both pass the broad validity check: treating
            // an integer bone index as float weight produces a near-zero value,
            // shifts the other three weights, and silently loses the fourth.
            // The authored weights sum to one; use that invariant to break ties.
            if c > best || (c == best && sum_error < best_sum_error) {
                best = c;
                best_sum_error = sum_error;
                layout = l;
                chosen_base = base + off;
            }
        }
    }
    let base = chosen_base;
    // Only fall back to the legacy layout when nothing validated.
    if best == 0 {
        layout = 3;
    }
    if let Ok(s) = std::env::var("OK_SKIN_LAYOUT") {
        if let Ok(v) = s.trim().parse::<u8>() {
            layout = v;
        }
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: DIAG skin n={n} stride={stride} base={base} lay={layout} best_valid={best}"
        );
    }
    let mut weights = Vec::with_capacity(n);
    let mut bones = Vec::with_capacity(n);
    let want: Vec<usize> = std::env::var("OK_DUMP_V")
        .ok()
        .map(|s| {
            s.split(',')
                .filter_map(|x| x.trim().parse::<usize>().ok())
                .collect()
        })
        .unwrap_or_default();
    for i in 0..n {
        let off = base + i * stride;
        let rec = &data[off..off + 32];
        let (mut w, mut b) = parse_skin_record(rec, layout);
        // Head-mod faces mix the standard record with `[i0 i1 i2 w0 w1 w2 w3 i3]`.
        // The mesh-wide layout leaves those chin/cheek verts at sum << 1, stuck
        // on FaceRoot. Retry only the broken records so valid ones stay put.
        if w.iter().sum::<f32>() < 0.5 {
            let (w5, b5) = parse_skin_record(rec, 5);
            if w5.iter().sum::<f32>() > 0.9 && skin_record_valid(&w5, &b5) {
                w = w5;
                b = b5;
            }
        }
        if want.contains(&i) {
            let hex: Vec<String> = rec.iter().map(|x| format!("{x:02x}")).collect();
            let prev = if off >= 16 {
                let p: Vec<String> = data[off - 16..off].iter().map(|x| format!("{x:02x}")).collect();
                p.join("")
            } else {
                String::new()
            };
            eprintln!(
                "ok-extract: DUMP {} n={n} v{i} stride={stride} base={base} lay={layout} b=[{},{},{},{}] w=[{:.3},{:.3},{:.3},{:.3}] prev16={} hex={}",
                mesh.name,
                b[0], b[1], b[2], b[3], w[0], w[1], w[2], w[3], prev, hex.join("")
            );
        }
        weights.push(w);
        bones.push(b);
    }
    (weights, bones)
}

pub(crate) fn decode_indices(mesh: &unity_asset_decode::mesh::Mesh) -> Vec<u32> {
    let buf = &mesh.index_buffer;
    if mesh.index_format == 1 {
        buf.chunks_exact(4)
            .filter_map(|c| c.try_into().ok())
            .map(u32::from_le_bytes)
            .collect()
    } else {
        buf.chunks_exact(2)
            .filter_map(|c| c.try_into().ok())
            .map(|b: [u8; 2]| u16::from_le_bytes(b) as u32)
            .collect()
    }
}

pub fn mesh_to_obj(name: &str, pos: &[[f32; 3]], uv: &[[f32; 2]], idx: &[u32]) -> String {
    let mut s = String::new();
    s.push_str(&format!("# {name} verts={} tris={}\n", pos.len(), idx.len() / 3));
    for p in pos {
        s.push_str(&format!("v {} {} {}\n", p[0], p[1], p[2]));
    }
    for t in uv {
        s.push_str(&format!("vt {} {}\n", t[0], t[1]));
    }
    let has_uv = uv.len() == pos.len() && uv.iter().any(|t| t[0].abs() + t[1].abs() > 0.0001);
    for tri in idx.chunks_exact(3) {
        if tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2] {
            continue;
        }
        if has_uv {
            s.push_str(&format!(
                "f {}/{} {}/{} {}/{}\n",
                tri[0] + 1,
                tri[0] + 1,
                tri[1] + 1,
                tri[1] + 1,
                tri[2] + 1,
                tri[2] + 1
            ));
        } else {
            s.push_str(&format!("f {} {} {}\n", tri[0] + 1, tri[1] + 1, tri[2] + 1));
        }
    }
    s
}

pub(crate) fn basename_lower(s: &str) -> String {
    s.replace('\\', "/")
        .rsplit('/')
        .next()
        .unwrap_or(s)
        .to_ascii_lowercase()
}

pub(crate) fn try_slice(data: &[u8], offset: u64, size: u32) -> Option<Vec<u8>> {
    if size == 0 {
        return None;
    }
    let start = offset as usize;
    let end = start.saturating_add(size as usize);
    if end <= data.len() {
        Some(data[start..end].to_vec())
    } else {
        None
    }
}

pub(crate) fn load_stream<B: StreamBytes>(files: &[(String, B)], path: &str, offset: u64, size: u32) -> Option<Vec<u8>> {
    if size == 0 {
        return None;
    }
    let want = basename_lower(path);
    if !want.is_empty() {
        for (n, data) in files {
            let bn = basename_lower(n);
            if bn == want || bn.ends_with(&want) || want.ends_with(&bn) {
                if let Some(b) = data.read_range(offset,size) {
                    return Some(b);
                }
            }
        }
    }
    for (n, data) in files {
        let l = n.to_ascii_lowercase();
        if l.ends_with(".ress") || l.ends_with(".resource") {
            if let Some(b) = data.read_range(offset,size) {
                return Some(b);
            }
        }
    }
    if want.is_empty() {
        for (_, data) in files {
            if let Some(b) = data.read_range(offset,size) {
                return Some(b);
            }
        }
    }
    None
}

pub(crate) struct CursorLe<'a> {
    d: &'a [u8],
    p: usize,
}

impl<'a> CursorLe<'a> {
    fn new(d: &'a [u8]) -> Self {
        Self { d, p: 0 }
    }
    fn align(&mut self) {
        self.p = (self.p + 3) & !3;
    }
    fn remaining(&self) -> usize {
        self.d.len().saturating_sub(self.p)
    }
    fn i32(&mut self) -> Option<i32> {
        let b = self.d.get(self.p..self.p + 4)?.try_into().ok()?;
        self.p += 4;
        Some(i32::from_le_bytes(b))
    }
    fn u32(&mut self) -> Option<u32> {
        Some(self.i32()? as u32)
    }
    fn u64(&mut self) -> Option<u64> {
        let b = self.d.get(self.p..self.p + 8)?.try_into().ok()?;
        self.p += 8;
        Some(u64::from_le_bytes(b))
    }
    fn bool(&mut self) -> Option<bool> {
        let v = *self.d.get(self.p)?;
        self.p += 1;
        Some(v != 0)
    }
    fn aligned_string(&mut self) -> Option<String> {
        let n = self.u32()? as usize;
        if self.p + n > self.d.len() {
            return None;
        }
        let s = String::from_utf8_lossy(&self.d[self.p..self.p + n]).into_owned();
        self.p += n;
        self.align();
        Some(s)
    }
    fn byte_array(&mut self) -> Option<Vec<u8>> {
        let n = self.i32()? as usize;
        if n == 0 {
            self.align();
            return Some(Vec::new());
        }
        if n > self.remaining() {
            return None;
        }
        let b = self.d[self.p..self.p + n].to_vec();
        self.p += n;
        self.align();
        Some(b)
    }
}

pub(crate) fn parse_tex_unity2019(data: &[u8]) -> Option<unity_asset_decode::texture::Texture2D> {
    use unity_asset_decode::texture::{Texture2D, TextureFormat};
    let mut r = CursorLe::new(data);
    let name = r.aligned_string()?;
    let _forced = r.i32()?;
    let _down = r.bool()?;
    let _alpha = r.bool()?;
    r.align();
    let width = r.i32()?;
    let height = r.i32()?;
    let complete = r.i32()?;
    let _mips_stripped = r.i32()?;
    let format_val = r.i32()?;
    let mip_count = r.i32()?;
    let is_readable = r.bool()?;
    let _pre = r.bool()?;
    let _ignore = r.bool()?;
    let _stream_mips = r.bool()?;
    r.align();
    let _prio = r.i32()?;
    let image_count = r.i32()?;
    let dim = r.i32()?;
    let _filter = r.i32()?;
    let _aniso = r.i32()?;
    let _bias = r.i32()?;
    let _wrap_u = r.i32()?;
    let _wrap_v = r.i32()?;
    let _wrap_w = r.i32()?;
    let light = r.i32()?;
    let color_space = r.i32()?;
    let _blob = r.byte_array()?;
    let image_data = r.byte_array().unwrap_or_default();
    let mut stream_off = 0u64;
    let mut stream_size = 0u32;
    let mut stream_path = String::new();
    if r.remaining() >= 12 {
        stream_off = r.u64().unwrap_or(0);
        stream_size = r.u32().unwrap_or(0);
        stream_path = r.aligned_string().unwrap_or_default();
    }
    if width < 4 || height < 4 || width > 8192 || height > 8192 {
        return None;
    }
    let mut tex = Texture2D::new(name, width, height, TextureFormat::from(format_val));
    tex.complete_image_size = complete;
    tex.mip_count = mip_count;
    tex.is_readable = is_readable;
    tex.image_count = image_count;
    tex.texture_dimension = dim;
    tex.light_map_format = light;
    tex.color_space = color_space;
    tex.image_data = image_data;
    tex.data_size = tex.image_data.len() as i32;
    tex.stream_info.path = stream_path;
    tex.stream_info.offset = stream_off;
    tex.stream_info.size = stream_size;
    Some(tex)
}

/// Unity meshes that store skin data in the newer `m_Skin` (BoneWeight1 array)
/// field instead of vertex channels. Returns (weights, bone indices) in the
/// SMR's own bone order.
pub(crate) fn decode_skin_object(
    object: &UnityObject,
) -> Option<(Vec<[f32; 4]>, Vec<[u16; 4]>)> {
    let skin_opt = object.class.properties().get("m_Skin");
    if std::env::var("OK_MAYUDIAG").is_ok() {
        let nm = object.name().unwrap_or_default();
        if nm.to_ascii_lowercase().contains("mayu") {
            match skin_opt {
                Some(unity_asset_core::UnityValue::Array(a)) => {
                    let keys = a.first().and_then(|e| match e {
                        unity_asset_core::UnityValue::Object(o) => {
                            Some(o.keys().cloned().collect::<Vec<_>>())
                        }
                        _ => None,
                    });
                    eprintln!("ok-extract: SKINOBJ {nm} m_Skin len={} first_keys={keys:?}", a.len());
                }
                Some(v) => eprintln!("ok-extract: SKINOBJ {nm} m_Skin not array: {v:?}"),
                None => eprintln!("ok-extract: SKINOBJ {nm} m_Skin MISSING"),
            }
        }
    }
    let skin = skin_opt?;
    let arr = match skin {
        unity_asset_core::UnityValue::Array(a) => a,
        _ => return None,
    };
    if arr.is_empty() {
        return None;
    }
    let mut weights = Vec::with_capacity(arr.len());
    let mut bones = Vec::with_capacity(arr.len());
    for e in arr {
        let unity_asset_core::UnityValue::Object(o) = e else {
            return None;
        };
        let getf = |k: &str| -> f32 {
            o.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32
        };
        let geti = |k: &str| -> u16 {
            o.get(k).and_then(|v| v.as_i64()).unwrap_or(0) as u16
        };
        weights.push([getf("weight[0]"), getf("weight[1]"), getf("weight[2]"), getf("weight[3]")]);
        bones.push([
            geti("boneIndex[0]"),
            geti("boneIndex[1]"),
            geti("boneIndex[2]"),
            geti("boneIndex[3]"),
        ]);
    }
    if weights.iter().all(|w| w.iter().all(|x| *x == 0.0)) {
        return None;
    }
    Some((weights, bones))
}

pub(crate) fn decoded_from_object(
    object: &UnityObject,
    ver: &UnityVersion,
    stream_bytes: Option<&[u8]>,
    source_version: &str,
) -> Option<ExtractedMesh> {
    let mut mesh = match parse_mesh(object, ver) {
        Ok(mesh)=>mesh,
        Err(error)=>{eprintln!("ok-extract: mesh decode failed name={} path={} error={error}",object.name().unwrap_or_default(),object.path_id());return None;}
    };
    let authored_tangents = decode_tangents(&mesh);
    let compressed_tangents = object.class.properties().get("m_CompressedMesh")
        .and_then(|v| match v { unity_asset_core::UnityValue::Object(fields) => fields.get("m_Tangents"), _ => None })
        .and_then(|v| match v { unity_asset_core::UnityValue::Object(fields) => fields.get("m_NumItems"), _ => None })
        .and_then(|v| v.as_i64())
        .is_some_and(|count| count > 0);
    if std::env::var("OK_SKINCH").is_ok() && mesh.name == "o_body_a" && mesh.vertex_data.vertex_count > 8000 {
        if let Some(unity_asset_core::UnityValue::Object(vd)) =
            object.class.properties().get("m_VertexData")
        {
            if let Some(unity_asset_core::UnityValue::Array(chans)) = vd.get("m_Channels") {
                eprintln!("ok-extract: VD chans={}", chans.len());
                for (i, c) in chans.iter().enumerate() {
                    eprintln!("ok-extract: VD ch{i} {c:?}");
                }
            }
            if let Some(v) = vd.get("m_Streams") {
                eprintln!("ok-extract: VD streams={v:?}");
            }
        }
        for k in ["m_CompressedMesh", "m_MeshCompression", "m_BindPose", "m_BoneNameHashes"] {
            if let Some(v) = object.class.properties().get(k) {
                let s = format!("{v:?}");
                eprintln!("ok-extract: VD {} = {}", k, &s[..s.len().min(300)]);
            }
        }
        if let Some(skin) = object.class.properties().get("m_Skin") {
            match skin {
                unity_asset_core::UnityValue::Bytes(b) => {
                    eprintln!("ok-extract: VD m_Skin Bytes len={} head={:?}", b.len(), &b[..b.len().min(40)]);
                }
                unity_asset_core::UnityValue::Array(a) => {
                    eprintln!("ok-extract: VD m_Skin Array len={} first={:?}", a.len(), a.first());
                }
                other => {
                    let s = format!("{other:?}");
                    eprintln!("ok-extract: VD m_Skin other {}", &s[..s.len().min(200)]);
                }
            }
        }
        let keys: Vec<&String> = object.class.properties().keys().collect();
        eprintln!("ok-extract: VD keys={keys:?}");
    }
    if std::env::var("OK_SHAPES").is_ok()
        && (mesh.name == "o_body_a" || mesh.name.eq_ignore_ascii_case("cf_O_face"))
    {
        if let Some(shapes) = object.class.properties().get("m_Shapes") {
            if let unity_asset_core::UnityValue::Object(so) = shapes {
                if std::env::var("OK_SHAPES_KEYS").is_ok() {
                    let keys: Vec<&String> = so.keys().collect();
                    let sizes: Vec<String> = so
                        .iter()
                        .map(|(k, v)| match v {
                            unity_asset_core::UnityValue::Array(a) => format!("{k}:{}", a.len()),
                            _ => format!("{k}:?"),
                        })
                        .collect();
                    eprintln!("ok-extract: SHAPESKEYS {} keys={:?} sizes={:?}", mesh.name, keys, sizes);
                }
                if let Some(unity_asset_core::UnityValue::Array(chans)) = so.get("channels") {
                    eprintln!("ok-extract: SHAPES {} channels={}", mesh.name, chans.len());
                    for (ci, c) in chans.iter().enumerate() {
                        if let unity_asset_core::UnityValue::Object(co) = c {
                            let name = co
                                .get("name")
                                .and_then(|v| v.as_str())
                                .unwrap_or("?");
                            let fw = co.get("fullWeights");
                            eprintln!("ok-extract: SHAPES ch{ci} name={name} fullWeights={fw:?}");
                        }
                    }
                }
                if std::env::var("OK_SHAPES_KEYS").is_ok() {
                    if let Some(unity_asset_core::UnityValue::Array(sh)) = so.get("shapes") {
                        for (i, s) in sh.iter().take(3).enumerate() {
                            eprintln!("ok-extract: SHAPES {}-shape[{i}]={s:?}", mesh.name);
                        }
                    }
                    if let Some(unity_asset_core::UnityValue::Array(fw)) = so.get("fullWeights") {
                        eprintln!("ok-extract: SHAPES {}-fullWeights[0..3]={:?}", mesh.name, &fw[..3.min(fw.len())]);
                    }
                    if let Some(unity_asset_core::UnityValue::Array(vs)) = so.get("vertices") {
                        for (i, v) in vs.iter().take(2).enumerate() {
                            eprintln!("ok-extract: SHAPES {}-v[{i}]={v:?}", mesh.name);
                        }
                    }
                }
            }
        }
    }
    if mesh.vertex_data.data_size.is_empty() {
        if let Some(b) = stream_bytes {
            mesh.vertex_data.data_size = b.to_vec();
        }
    }
    let pos = decode_positions(&mesh);
    let idx = decode_indices(&mesh);
    let index_bytes = if mesh.index_format == 1 { 4 } else { 2 };
    let submesh_ranges = mesh.sub_meshes.iter().map(|s| (
        s.first_byte as usize / index_bytes,
        s.index_count as usize,
        s.base_vertex,
    )).collect();
    // A single triangle or quad is a valid Unity Mesh (many accessory
    // primitives are four-vertex planes). The old eight-vertex threshold
    // silently dropped these prefabs even when their renderer was enabled.
    if pos.len() < 3 || idx.len() < 3 {
        eprintln!("ok-extract: mesh omitted name={} vertices={} indices={} source_vertices={} data={} warnings={:?}",mesh.name,pos.len(),idx.len(),mesh.vertex_data.vertex_count,mesh.vertex_data.data_size.len(),object.typetree_warnings());
        return None;
    }
    if !pos.iter().any(|p| p[0].abs() + p[1].abs() + p[2].abs() > 0.0001) {
        eprintln!("ok-extract: mesh omitted all-zero positions name={} vertices={} data={} warnings={:?}",mesh.name,pos.len(),mesh.vertex_data.data_size.len(),object.typetree_warnings());
        return None;
    }
    let uv = decode_uvs(&mesh);
    let source_version=UnityVersion::parse_version(source_version).unwrap_or_else(|_|ver.clone());
    let uv_base=texcoord_slot_base(&source_version);
    let uv2 = decode_texcoord_slot(&mesh, uv_base+1, 0);
    let uv3 = decode_texcoord_slot(&mesh, uv_base+2, 0);
    let uv4 = decode_texcoord_slot(&mesh, uv_base+3, 0);
    let vertex_colors = decode_vertex_colors(&mesh);
    // Diagnostic `OK_FACE_DROP_CAVITY`: the vanilla face texture `cf_face_00_t`
    // paints the mouth cavity as a dark rectangle (u 0.32-0.68, v_img 0.83-0.99
    // => Unity v 0.01-0.17). A few `cf_O_face` front-facing chin triangles sample
    // it and show a dark patch. Drop those triangles to test whether the patch
    // is that inner-mouth geometry.
    let mut idx = idx;
    if std::env::var("OK_FACE_DROP_CAVITY").is_ok() {
        eprintln!("ok-extract: DROPCAVITY seen mesh name={:?}", mesh.name);
    }
    if std::env::var("OK_FACE_DROP_CAVITY").is_ok() && mesh.name.eq_ignore_ascii_case("cf_O_face") {
        let mut kept = Vec::with_capacity(idx.len());
        for t in idx.chunks_exact(3) {
            let all = t.iter().all(|&v| {
                let i = v as usize;
                let q = uv.get(i).copied().unwrap_or([0.0; 2]);
                (0.28..=0.72).contains(&q[0]) && (0.78..=1.0).contains(&q[1])
            });
            if !all {
                kept.extend_from_slice(t);
            }
        }
        eprintln!(
            "ok-extract: DROPCAVITY {} tris {} -> {}",
            mesh.name,
            idx.len() / 3,
            kept.len() / 3
        );
        idx = kept;
    }
    let (mut weights, mut bone_idx) = match decode_skin_object(object) {
        Some(wb) if wb.0.len() == pos.len() => wb,
        _ => decode_weights(&mesh),
    };
    let bind_poses = crate::pose::bind_poses_from_object(object);
    // 1-bone meshes (iris `cf_Ohitomi_*02`, tongue) often omit the weight
    // vertex channels; Unity then rigid-skins every vertex to bone 0.
    // Without this, remap/skin skip the mesh and `apply_parent` parks it
    // on the head root instead of `cf_J_hitomi_tx_*`.
    // Only do this for single-bone meshes: applying it to a multi-bone mesh
    // whose weights failed to parse would pin everything to bone 0 (this is
    // what distorted one eyebrow half when `cf_O_mayuge`'s 2-influence skin
    // was not decoded).
    if weights.len() != pos.len() && !bind_poses.is_empty() {
        // Several rigid parts (iris, tongue, some accessories like
        // `on_neck_star_stone`) omit the weight channels; Unity then rigid-skins
        // every vertex to bone 0. Keep this fallback for them, but still warn:
        // on a genuinely multi-weight mesh it silently pins everything to bone 0
        // (which distorted one eyebrow half until `cf_O_mayuge`'s 2-influence
        // skin was decoded above).
        eprintln!(
            "ok-extract: WARN {} weights {} != verts {} (binds {}); rigid bone-0 fallback",
            mesh.name,
            weights.len(),
            pos.len(),
            bind_poses.len()
        );
        weights = vec![[1.0, 0.0, 0.0, 0.0]; pos.len()];
        bone_idx = vec![[0u16; 4]; pos.len()];
    }
    let normals = decode_normals(&mesh);
    let obj = mesh_to_obj(&mesh.name, &pos, &uv, &idx);
    Some(ExtractedMesh {
        runtime_textures: Default::default(), texture_plan: Vec::new(),
        shader_extra_textures: Default::default(),
        name: mesh.name,
        obj,
        path_id: object.path_id(),
        renderer_ids: Vec::new(),
        renderer_names: Vec::new(),
        renderer_skinned: None,
        mat: None,
        submesh_ranges,
        submesh_index: 0,
        hair_shader_hint: false,
        texture_png: None,
        texture_name: String::new(),
        color_mask_png: None,
        alpha_mask_png: None,
        detail_mask_png: None,
        line_mask_png: None,
        normal_map_png: None,
        normal_mask_png: None,
        source_tangents: Some(!authored_tangents.is_empty() || compressed_tangents),
        tangents: authored_tangents,
        hair_gloss_png: None,
        positions: pos,
        normals,
        rest_normals: Vec::new(),
        uvs: uv,
        uv2,
        uv3,
        uv4,
        vertex_colors,
        indices: idx,
        weights,
        bone_idx,
        bind_poses,
        bone_names: Vec::new(),
        smr_skel: None,
        bone_implants: Vec::new(),
        rest_positions: Vec::new(),
        parent_bone: None,
        rigid_follow_bone: None,
        skel_kind: 0,
        blend_shapes: decode_blend_shapes(object),
        prefab_local: None,
        move_nodes: [None, None],
        deferred_dbones: None,
    })
}

/// Parse Unity `m_Shapes` into (channel name, deltas). Each delta is stored at
/// the frame weight recorded in `fullWeights` (Koikatsu uses 100).
pub(crate) fn decode_blend_shapes(
    object: &unity_asset_decode::object::UnityObject,
) -> Vec<(String, Vec<(u32, [f32; 3])>)> {
    use unity_asset_core::UnityValue;
    let mut out = Vec::new();
    let Some(UnityValue::Object(so)) = object.class.properties().get("m_Shapes") else {
        return out;
    };
    let (Some(UnityValue::Array(chans)), Some(UnityValue::Array(shapes)), Some(UnityValue::Array(verts))) =
        (so.get("channels"), so.get("shapes"), so.get("vertices"))
    else {
        return out;
    };
    for (ci, c) in chans.iter().enumerate() {
        let UnityValue::Object(co) = c else { continue };
        let name = co.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string();
        let frame = co.get("frameIndex").and_then(|v| v.as_i64()).unwrap_or(ci as i64);
        let si = if frame >= 0 { frame as usize } else { ci };
        if si >= shapes.len() {
            continue;
        }
        let Some(sobj) = shapes[si].as_object() else { continue };
        let first = sobj.get("firstVertex").and_then(|v| v.as_i64()).unwrap_or(-1);
        let count = sobj.get("vertexCount").and_then(|v| v.as_i64()).unwrap_or(0);
        if first < 0 || count <= 0 {
            out.push((name, Vec::new()));
            continue;
        }
        let mut deltas = Vec::with_capacity(count as usize);
        for k in first as usize..(first as usize + count as usize) {
            let Some(UnityValue::Object(ve)) = verts.get(k) else { continue };
            let idx = ve.get("index").and_then(|v| v.as_i64()).unwrap_or(-1);
            if idx < 0 {
                continue;
            }
            let Some(UnityValue::Object(vv)) = ve.get("vertex") else { continue };
            let x = vv.get("x").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            let y = vv.get("y").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            let z = vv.get("z").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            deltas.push((idx as u32, [x, y, z]));
        }
        out.push((name, deltas));
    }
    out
}
