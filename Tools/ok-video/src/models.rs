//! Model files: the user's original downloads (non-redistributable) are converted once into
//! safetensors under the cache directory. A stamp records the source size and mtime.

use crate::pickle;
use std::path::{Path, PathBuf};

/// (source under the checkpoints root, cache file, key prefix kept)
pub const SOURCES: [(&str, &str, &str); 4] = [
    ("yolo/yolov8x.pt", "yolov8x.safetensors", "model"),
    ("vitpose/vitpose-h-multi-coco.pth", "vitpose.safetensors", "state_dict"),
    ("hmr2/epoch=10-step=25000.ckpt", "hmr2.safetensors", "state_dict"),
    ("gvhmr/gvhmr_siga24_release.ckpt", "gvhmr.safetensors", "state_dict"),
];
pub const SMPLX: &str = "body_models/smplx/SMPLX_NEUTRAL.npz";

fn st_dtype(d: pickle::DType) -> safetensors::Dtype {
    use pickle::DType::*;
    match d {
        F32 => safetensors::Dtype::F32,
        F16 => safetensors::Dtype::F16,
        BF16 => safetensors::Dtype::BF16,
        F64 => safetensors::Dtype::F64,
        I64 => safetensors::Dtype::I64,
        I32 => safetensors::Dtype::I32,
        I16 => safetensors::Dtype::I16,
        I8 => safetensors::Dtype::I8,
        U8 | Bool => safetensors::Dtype::U8, // candle has no bool; same bytes
    }
}

/// Converts `src` (PyTorch zip) to safetensors keeping `prefix.` (stripped; "" keeps all).
pub fn convert(src: &Path, out: &Path, prefix: &str) -> Result<usize, String> {
    let mut ck = pickle::Checkpoint::open(src)?;
    let keep: Vec<pickle::TensorInfo> = ck
        .tensors
        .iter()
        .filter(|t| prefix.is_empty() || t.name.starts_with(&format!("{prefix}.")))
        .cloned()
        .map(|mut t| {
            if !prefix.is_empty() {
                t.name = t.name[prefix.len() + 1..].to_string();
            }
            t
        })
        .collect();
    if keep.is_empty() {
        return Err(format!("{} 裡沒有 {prefix}.* 權重；檔案可能不是原始下載版本", src.display()));
    }
    let mut data = Vec::with_capacity(keep.len());
    for t in &keep {
        data.push((t.name.clone(), t.dtype, t.shape.clone(), ck.bytes(t)?));
    }
    let views: Vec<(String, safetensors::tensor::TensorView)> = data
        .iter()
        .map(|(n, d, s, b)| (n.clone(), safetensors::tensor::TensorView::new(st_dtype(*d), s.clone(), b).unwrap()))
        .collect();
    let tmp = out.with_extension("tmp");
    safetensors::serialize_to_file(views, None, &tmp).map_err(|e| format!("無法寫入 {}：{e}", tmp.display()))?;
    std::fs::rename(&tmp, out).map_err(|e| format!("無法寫入 {}：{e}", out.display()))?;
    Ok(keep.len())
}

fn stamp(src: &Path) -> Option<String> {
    let m = std::fs::metadata(src).ok()?;
    let t = m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_secs();
    Some(format!("{} {} {}", src.display(), m.len(), t))
}

pub struct Paths {
    pub root: PathBuf,
    pub cache: PathBuf,
}

impl Paths {
    pub fn cached(&self, file: &str) -> PathBuf { self.cache.join(file) }
    pub fn smplx(&self) -> PathBuf { self.root.join(SMPLX) }

    /// Missing source files, listed for the user.
    pub fn missing(&self) -> Vec<PathBuf> {
        SOURCES.iter().map(|s| self.root.join(s.0)).chain(std::iter::once(self.smplx())).filter(|p| !p.is_file()).collect()
    }

    /// Converts whatever is missing or out of date; `progress` gets a line per conversion.
    pub fn prepare(&self, progress: &dyn Fn(&str)) -> Result<(), String> {
        let missing = self.missing();
        if !missing.is_empty() {
            return Err(format!(
                "找不到模型檔：{}。請把 GVHMR 的 checkpoints（gvhmr、hmr2、vitpose、yolo）與 SMPL-X（body_models/smplx/SMPLX_NEUTRAL.npz）放在 {}，見 Docs/VIDEO_MOTION.md",
                missing.iter().map(|p| p.display().to_string()).collect::<Vec<_>>().join("、"),
                self.root.display()
            ));
        }
        std::fs::create_dir_all(&self.cache).map_err(|e| format!("無法建立 {}：{e}", self.cache.display()))?;
        for (src, file, prefix) in SOURCES {
            let src = self.root.join(src);
            let out = self.cache.join(file);
            let stamp_path = out.with_extension("stamp");
            let want = stamp(&src).unwrap_or_default();
            if out.is_file() && std::fs::read_to_string(&stamp_path).ok().as_deref() == Some(want.as_str()) {
                continue;
            }
            progress(&format!("[步驟] 轉換模型權重 {}（只在第一次）", src.file_name().unwrap().to_string_lossy()));
            convert(&src, &out, prefix)?;
            std::fs::write(&stamp_path, want).map_err(|e| e.to_string())?;
        }
        Ok(())
    }
}
