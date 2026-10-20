//! Decoder for the KKABMPlugin.ABMData (ABM/ABMX "BonemodX") extended card
//! data: LZ4-compressed MessagePack `List<BoneModifier>` (version 2).

use rmpv::Value;

#[derive(Debug, Clone, Copy, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct AbmData {
    pub scale: [f32; 3],
    pub length: f32,
    pub pos: [f32; 3],
    pub rot: [f32; 3],
}

impl Default for AbmData {
    fn default() -> Self {
        Self {
            scale: [1.0; 3],
            length: 1.0,
            pos: [0.0; 3],
            rot: [0.0; 3],
        }
    }
}

impl AbmData {
    pub fn is_empty(&self) -> bool {
        self.scale == [1.0; 3]
            && self.length == 1.0
            && self.pos == [0.0; 3]
            && self.rot == [0.0; 3]
    }
}

#[derive(Debug, Clone, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct AbmBone {
    pub name: String,
    pub location: i32,
    pub coord: Vec<AbmData>,
}

impl AbmBone {
    /// Modifier for a coordinate (0 when not coordinate-specific).
    pub fn for_coord(&self, coord: usize) -> AbmData {
        if self.coord.len() == 1 {
            self.coord[0]
        } else {
            self.coord.get(coord).copied().unwrap_or_default()
        }
    }
}

fn num(v: &Value) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn arr3(v: &Value) -> [f32; 3] {
    match v.as_array() {
        Some(a) => [
            a.first().map(num).unwrap_or(0.0),
            a.get(1).map(num).unwrap_or(0.0),
            a.get(2).map(num).unwrap_or(0.0),
        ],
        None => [0.0; 3],
    }
}

/// Strip a MessagePack ext header (ext8/ext16/ext32) and return its payload.
fn strip_ext(b: &[u8]) -> Option<Vec<u8>> {
    match *b.first()? {
        0xc7 => {
            let n = *b.get(1)? as usize;
            b.get(3..3 + n).map(|s| s.to_vec())
        }
        0xc8 => {
            let n = u16::from_be_bytes([*b.get(1)?, *b.get(2)?]) as usize;
            b.get(4..4 + n).map(|s| s.to_vec())
        }
        0xc9 => {
            let n = u32::from_be_bytes([*b.get(1)?, *b.get(2)?, *b.get(3)?, *b.get(4)?]) as usize;
            b.get(6..6 + n).map(|s| s.to_vec())
        }
        _ => None,
    }
}

fn map_get<'a>(m: &'a [(Value, Value)], k: i64) -> Option<&'a Value> {    m.iter().find(|(kk, _)| kk.as_i64() == Some(k)).map(|(_, v)| v)
}

fn parse_data(v: &Value) -> AbmData {
    let g = |i: usize| -> Option<&Value> {
        if let Some(m) = v.as_map() {
            map_get(m, i as i64)
        } else {
            v.as_array().and_then(|a| a.get(i))
        }
    };
    AbmData {
        scale: g(0).map(arr3).unwrap_or([1.0; 3]),
        length: g(1).map(num).unwrap_or(1.0),
        pos: g(2).map(arr3).unwrap_or([0.0; 3]),
        rot: g(3).map(arr3).unwrap_or([0.0; 3]),
    }
}

/// Decode `boneData` (the raw ext blob from the card) into bone modifiers.
pub fn decode(binary: &[u8]) -> Vec<AbmBone> {
    // Outer MessagePack value is an ext type (99); its body is
    // [msgpack int: uncompressed length][LZ4 block].
    let payload: Vec<u8> = strip_ext(binary).unwrap_or_else(|| binary.to_vec());
    let (ulen, hdr) = match payload.first().copied() {
        Some(b) if b <= 0x7f => (b as usize, 1),
        Some(0xcc) => (payload.get(1).copied().unwrap_or(0) as usize, 2),
        Some(0xcd) => (
            u16::from_be_bytes([*payload.get(1).unwrap_or(&0), *payload.get(2).unwrap_or(&0)])
                as usize,
            3,
        ),
        Some(0xce) => (
            u32::from_be_bytes([
                *payload.get(1).unwrap_or(&0),
                *payload.get(2).unwrap_or(&0),
                *payload.get(3).unwrap_or(&0),
                *payload.get(4).unwrap_or(&0),
            ]) as usize,
            5,
        ),
        Some(0xd2) => {
            let b = [
                *payload.get(1).unwrap_or(&0),
                *payload.get(2).unwrap_or(&0),
                *payload.get(3).unwrap_or(&0),
                *payload.get(4).unwrap_or(&0),
            ];
            (i32::from_be_bytes(b).max(0) as usize, 5)
        }
        _ => (0, 0),
    };
    let body = if hdr > 0 && hdr <= payload.len() {
        &payload[hdr..]
    } else {
        &payload[..]
    };
    let out = if ulen > 0 {
        match lz4_flex::block::decompress(body, ulen) {
            Ok(o) => o,
            Err(_) => body.to_vec(),
        }
    } else {
        body.to_vec()
    };
    let Some(v) = rmpv::decode::read_value(&mut &out[..]).ok() else {
        return Vec::new();
    };
    let Some(arr) = v.as_array() else {
        return Vec::new();
    };
    if std::env::var("OK_ABM_DUMP").is_ok() {
        eprintln!("ok-extract: ABMDUMP ulen={ulen} entries={}", arr.len());
        for e in arr.iter().take(3) {
            eprintln!("ok-extract: ABMDUMP entry={:?}", e);
        }
    }
    let mut res = Vec::new();
    for bm in arr {
        let (name, coord_v, loc) = if let Some(m) = bm.as_map() {
            (
                map_get(m, 0).and_then(|v| v.as_str()).unwrap_or("").to_string(),
                map_get(m, 1).cloned(),
                map_get(m, 2).and_then(|v| v.as_i64()).unwrap_or(0) as i32,
            )
        } else if let Some(a) = bm.as_array() {
            (
                a.first().and_then(|v| v.as_str()).unwrap_or("").to_string(),
                a.get(1).cloned(),
                a.get(2).and_then(|v| v.as_i64()).unwrap_or(0) as i32,
            )
        } else {
            continue;
        };
        if name.is_empty() {
            continue;
        }
        let coord = coord_v
            .as_ref()
            .and_then(|v| v.as_array())
            .map(|a| a.iter().map(parse_data).collect())
            .unwrap_or_default();
        res.push(AbmBone {
            name,
            location: loc,
            coord,
        });
    }
    res
}
