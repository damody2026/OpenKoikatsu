//! Vocaloid Motion Data (`.vmd`) binary parser and MMD interpolation helpers.
//!
//! Matches `vmdlib.py::VmdLib.loadVmd` and `VmdBoneKeyFrame` / `VmdMorphKeyFrame` /
//! `VmdCameraKeyFrame` / `VmdIkVisibleKeyFrame`.

type Result<T> = std::result::Result<T, String>;
macro_rules! bail {
    ($($arg:tt)*) => {
        return Err(format!($($arg)*))
    };
}
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

use crate::vnge::cubic_bezier_interpolate;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VmdBoneKeyFrame {
    pub name: String,
    pub frame_no: u32,
    pub pos: [f32; 3],
    pub rot: [f32; 4],
    pub x_curve: [f32; 4],
    pub y_curve: [f32; 4],
    pub z_curve: [f32; 4],
    pub r_curve: [f32; 4],
}

impl Default for VmdBoneKeyFrame {
    fn default() -> Self {
        Self {
            name: String::new(),
            frame_no: 0,
            pos: [0.0, 0.0, 0.0],
            rot: [0.0, 0.0, 0.0, 1.0],
            x_curve: [0.25, 0.25, 0.75, 0.75],
            y_curve: [0.25, 0.25, 0.75, 0.75],
            z_curve: [0.25, 0.25, 0.75, 0.75],
            r_curve: [0.25, 0.25, 0.75, 0.75],
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VmdMorphKeyFrame {
    pub name: String,
    pub frame_no: u32,
    pub weight: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VmdCameraKeyFrame {
    pub frame_no: u32,
    pub dist: f32,
    pub pos: [f32; 3],
    /// Degrees (`rot_rad * 180.0 / PI`), matching `vmdlib.py`.
    pub rot: [f32; 3],
    pub x_curve: [f32; 4],
    pub y_curve: [f32; 4],
    pub z_curve: [f32; 4],
    pub r_curve: [f32; 4],
    pub d_curve: [f32; 4],
    pub v_curve: [f32; 4],
    pub view_angle: f32,
    pub orthographic: u8,
}

impl Default for VmdCameraKeyFrame {
    fn default() -> Self {
        Self {
            frame_no: 0,
            dist: -45.0,
            pos: [0.0, 10.0, 0.0],
            rot: [0.0, 180.0, 0.0],
            x_curve: [0.25, 0.25, 0.75, 0.75],
            y_curve: [0.25, 0.25, 0.75, 0.75],
            z_curve: [0.25, 0.25, 0.75, 0.75],
            r_curve: [0.25, 0.25, 0.75, 0.75],
            d_curve: [0.25, 0.25, 0.75, 0.75],
            v_curve: [0.25, 0.25, 0.75, 0.75],
            view_angle: 30.0,
            orthographic: 0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VmdIkVisibleKeyFrame {
    pub frame_no: u32,
    pub visible: bool,
    pub ik_bones: BTreeMap<String, bool>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct VmdFile {
    pub model_name: String,
    pub bone_keyframes: Vec<VmdBoneKeyFrame>,
    pub morph_keyframes: Vec<VmdMorphKeyFrame>,
    pub camera_keyframes: Vec<VmdCameraKeyFrame>,
    pub ik_keyframes: Vec<VmdIkVisibleKeyFrame>,
}

/// Evaluates MMD's cubic Bezier curve `[x1, y1, x2, y2]` at `x` in `[0, 1]`.
/// Matches `vmdlib.py::VmdLib.mmdCubicBezierInterpolate(cuv, x)` which calls
/// `cubic_bezier_interpolate(cuv[0], cuv[2], cuv[1], cuv[3], x)`.
#[inline]
pub fn mmd_cubic_bezier(curve: &[f32; 4], x: f32) -> f32 {
    if (curve[0] - curve[1]).abs() < 1e-6 && (curve[2] - curve[3]).abs() < 1e-6 {
        return x;
    }
    cubic_bezier_interpolate(curve[0], curve[2], curve[1], curve[3], x)
}

/// Decode a null-padded Shift-JIS (CP932) byte slice into UTF-8 `String`.
pub fn decode_shift_jis(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let slice = &bytes[..end];
    if slice.is_empty() {
        return String::new();
    }
    if let Ok(ascii) = std::str::from_utf8(slice) {
        return ascii.to_string();
    }
    #[cfg(windows)]
    {
        #[link(name = "kernel32")]
        extern "system" {
            fn MultiByteToWideChar(
                code_page: u32,
                dw_flags: u32,
                lp_multi_byte_str: *const u8,
                cb_multi_byte: i32,
                lp_wide_char_str: *mut u16,
                cch_wide_char: i32,
            ) -> i32;
        }
        const CP_SHIFT_JIS: u32 = 932;
        unsafe {
            let needed = MultiByteToWideChar(
                CP_SHIFT_JIS,
                0,
                slice.as_ptr(),
                slice.len() as i32,
                std::ptr::null_mut(),
                0,
            );
            if needed > 0 {
                let mut wide = vec![0u16; needed as usize];
                let written = MultiByteToWideChar(
                    CP_SHIFT_JIS,
                    0,
                    slice.as_ptr(),
                    slice.len() as i32,
                    wide.as_mut_ptr(),
                    needed,
                );
                if written > 0 {
                    return String::from_utf16_lossy(&wide[..written as usize]);
                }
            }
        }
    }
    String::from_utf8_lossy(slice).into_owned()
}

fn read_u32(bytes: &[u8], pos: &mut usize) -> Result<u32> {
    if *pos + 4 > bytes.len() {
        bail!("unexpected EOF reading u32 at {}", *pos);
    }
    let v = u32::from_le_bytes(bytes[*pos..*pos + 4].try_into().unwrap());
    *pos += 4;
    Ok(v)
}

fn read_f32(bytes: &[u8], pos: &mut usize) -> Result<f32> {
    if *pos + 4 > bytes.len() {
        bail!("unexpected EOF reading f32 at {}", *pos);
    }
    let v = f32::from_le_bytes(bytes[*pos..*pos + 4].try_into().unwrap());
    *pos += 4;
    Ok(v)
}

impl VmdFile {
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() < 30 {
            bail!("VMD file too short ({} bytes)", bytes.len());
        }
        let header = &bytes[..30];
        let name_len = if header.starts_with(b"Vocaloid Motion Data 0002") {
            20usize
        } else if header.starts_with(b"Vocaloid Motion Data file") {
            10usize
        } else {
            bail!("invalid VMD header");
        };
        let mut pos = 30;
        if pos + name_len > bytes.len() {
            bail!("VMD truncated at model name");
        }
        let model_name = decode_shift_jis(&bytes[pos..pos + name_len]);
        pos += name_len;

        let mut vmd = VmdFile {
            model_name,
            ..Default::default()
        };

        // 1. Bone keyframes (111 bytes each)
        if pos + 4 > bytes.len() {
            return Ok(vmd);
        }
        let bone_count = read_u32(bytes, &mut pos)? as usize;
        if pos + bone_count * 111 > bytes.len() {
            bail!(
                "VMD truncated in bone keyframes: count={} pos={} len={}",
                bone_count,
                pos,
                bytes.len()
            );
        }
        vmd.bone_keyframes.reserve(bone_count);
        for _ in 0..bone_count {
            let name = decode_shift_jis(&bytes[pos..pos + 15]);
            pos += 15;
            let frame_no = read_u32(bytes, &mut pos)?;
            let px = read_f32(bytes, &mut pos)?;
            let py = read_f32(bytes, &mut pos)?;
            let pz = read_f32(bytes, &mut pos)?;
            let rx = read_f32(bytes, &mut pos)?;
            let ry = read_f32(bytes, &mut pos)?;
            let rz = read_f32(bytes, &mut pos)?;
            let rw = read_f32(bytes, &mut pos)?;
            let mut c = [0.0f32; 16];
            for i in 0..16 {
                let raw = read_u32(bytes, &mut pos)?;
                c[i] = ((raw & 0xff) as f32) / 127.0;
            }
            vmd.bone_keyframes.push(VmdBoneKeyFrame {
                name,
                frame_no,
                pos: [px, py, pz],
                rot: [rx, ry, rz, rw],
                x_curve: [c[0], c[1], c[2], c[3]],
                y_curve: [c[4], c[5], c[6], c[7]],
                z_curve: [c[8], c[9], c[10], c[11]],
                r_curve: [c[12], c[13], c[14], c[15]],
            });
        }
        vmd.bone_keyframes.sort_by_key(|k| k.frame_no);

        // 2. Morph keyframes (23 bytes each)
        if pos + 4 > bytes.len() {
            return Ok(vmd);
        }
        let morph_count = read_u32(bytes, &mut pos)? as usize;
        if pos + morph_count * 23 > bytes.len() {
            return Ok(vmd);
        }
        vmd.morph_keyframes.reserve(morph_count);
        for _ in 0..morph_count {
            let name = decode_shift_jis(&bytes[pos..pos + 15]);
            pos += 15;
            let frame_no = read_u32(bytes, &mut pos)?;
            let weight = read_f32(bytes, &mut pos)?;
            vmd.morph_keyframes.push(VmdMorphKeyFrame {
                name,
                frame_no,
                weight,
            });
        }
        vmd.morph_keyframes.sort_by_key(|k| k.frame_no);

        // 3. Camera keyframes (61 bytes each)
        if pos + 4 > bytes.len() {
            return Ok(vmd);
        }
        let cam_count = read_u32(bytes, &mut pos)? as usize;
        if pos + cam_count * 61 > bytes.len() {
            return Ok(vmd);
        }
        vmd.camera_keyframes.reserve(cam_count);
        let rad_to_deg = 180.0f32 / std::f32::consts::PI;
        for _ in 0..cam_count {
            let frame_no = read_u32(bytes, &mut pos)?;
            let dist = read_f32(bytes, &mut pos)?;
            let px = read_f32(bytes, &mut pos)?;
            let py = read_f32(bytes, &mut pos)?;
            let pz = read_f32(bytes, &mut pos)?;
            let rx = read_f32(bytes, &mut pos)? * rad_to_deg;
            let ry = read_f32(bytes, &mut pos)? * rad_to_deg;
            let rz = read_f32(bytes, &mut pos)? * rad_to_deg;
            let mut c = [0.0f32; 24];
            for i in 0..24 {
                c[i] = (bytes[pos + i] as f32) / 127.0;
            }
            pos += 24;
            let view_angle = read_u32(bytes, &mut pos)? as f32;
            let orthographic = bytes[pos];
            pos += 1;
            vmd.camera_keyframes.push(VmdCameraKeyFrame {
                frame_no,
                dist,
                pos: [px, py, pz],
                rot: [rx, ry, rz],
                x_curve: [c[0], c[2], c[1], c[3]],
                y_curve: [c[4], c[6], c[5], c[7]],
                z_curve: [c[8], c[10], c[9], c[11]],
                r_curve: [c[12], c[14], c[13], c[15]],
                d_curve: [c[16], c[18], c[17], c[19]],
                v_curve: [c[20], c[22], c[21], c[23]],
                view_angle,
                orthographic,
            });
        }
        vmd.camera_keyframes.sort_by_key(|k| k.frame_no);

        // 4. Light keyframes (28 bytes each)
        if pos + 4 > bytes.len() {
            return Ok(vmd);
        }
        let light_count = read_u32(bytes, &mut pos)? as usize;
        if pos + light_count * 28 > bytes.len() {
            return Ok(vmd);
        }
        pos += light_count * 28;

        // 5. Self-shadow keyframes (9 bytes each)
        if pos + 4 > bytes.len() {
            return Ok(vmd);
        }
        let shadow_count = read_u32(bytes, &mut pos)? as usize;
        if pos + shadow_count * 9 > bytes.len() {
            return Ok(vmd);
        }
        pos += shadow_count * 9;

        // 6. IK / Visibility keyframes
        if pos + 4 > bytes.len() {
            return Ok(vmd);
        }
        let ik_count = read_u32(bytes, &mut pos)? as usize;
        for _ in 0..ik_count {
            if pos + 9 > bytes.len() {
                break;
            }
            let frame_no = read_u32(bytes, &mut pos)?;
            let visible = bytes[pos] != 0;
            pos += 1;
            let ik_bone_count = read_u32(bytes, &mut pos)? as usize;
            if pos + ik_bone_count * 21 > bytes.len() {
                break;
            }
            let mut ik_bones = BTreeMap::new();
            for _ in 0..ik_bone_count {
                let bname = decode_shift_jis(&bytes[pos..pos + 20]);
                let enabled = bytes[pos + 20] != 0;
                pos += 21;
                if !bname.is_empty() {
                    ik_bones.insert(bname, enabled);
                }
            }
            vmd.ik_keyframes.push(VmdIkVisibleKeyFrame {
                frame_no,
                visible,
                ik_bones,
            });
        }
        vmd.ik_keyframes.sort_by_key(|k| k.frame_no);

        Ok(vmd)
    }
}
