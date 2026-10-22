//! Sharded safetensors (memory mapped). The checkpoints are f32; GPU weights are converted to bf16 on the host with
//! the same round-to-nearest-even as `tensor.to(torch.bfloat16)`.

use std::collections::HashMap;
use std::path::Path;

use anyhow::{bail, Context, Result};
use memmap2::Mmap;
use safetensors::{Dtype, SafeTensors};

use super::gpu::{Buf, Gpu};

pub struct Shards {
    maps: Vec<Mmap>,
    index: HashMap<String, usize>,
}

pub fn bf16_bits(f: f32) -> u16 {
    let u = f.to_bits();
    if (u & 0x7fff_ffff) > 0x7f80_0000 {
        return 0x7fc0;
    }
    ((u + 0x7fff + ((u >> 16) & 1)) >> 16) as u16
}

pub fn bf16_value(b: u16) -> f32 {
    f32::from_bits((b as u32) << 16)
}

/// Convert in parallel (the 3B backbone is ~3.6 G values).
fn convert(src: &[u8], dst: &mut [u16]) {
    let threads = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4).min(16);
    let per = dst.len().div_ceil(threads).max(1 << 16);
    std::thread::scope(|s| {
        for (i, chunk) in dst.chunks_mut(per).enumerate() {
            let bytes = &src[i * per * 4..(i * per + chunk.len()) * 4];
            s.spawn(move || {
                for (d, c) in chunk.iter_mut().zip(bytes.chunks_exact(4)) {
                    *d = bf16_bits(f32::from_le_bytes([c[0], c[1], c[2], c[3]]));
                }
            });
        }
    });
}

impl Shards {
    /// A directory with `model.safetensors.index.json`, or a single `model.safetensors`.
    pub fn open(dir: &Path) -> Result<Self> {
        let idx = dir.join("model.safetensors.index.json");
        let mut files: Vec<String> = Vec::new();
        let mut names: Vec<(String, String)> = Vec::new();
        if idx.is_file() {
            let v: serde_json::Value = serde_json::from_slice(&std::fs::read(&idx)?).with_context(|| format!("parse {}", idx.display()))?;
            for (k, f) in v["weight_map"].as_object().context("weight_map")? {
                let f = f.as_str().context("weight_map file")?.to_string();
                if !files.contains(&f) {
                    files.push(f.clone());
                }
                names.push((k.clone(), f));
            }
        } else {
            files.push("model.safetensors".into());
        }
        let mut maps = Vec::new();
        for f in &files {
            let p = dir.join(f);
            let file = std::fs::File::open(&p).with_context(|| format!("open {}", p.display()))?;
            let map = unsafe { Mmap::map(&file) }?;
            SafeTensors::deserialize(&map).with_context(|| format!("parse {}", p.display()))?;
            maps.push(map);
        }
        let mut index = HashMap::new();
        if names.is_empty() {
            for n in SafeTensors::deserialize(&maps[0])?.names() {
                index.insert(n.to_string(), 0);
            }
        } else {
            for (k, f) in names {
                index.insert(k, files.iter().position(|x| *x == f).unwrap());
            }
        }
        Ok(Self { maps, index })
    }

    pub fn has(&self, name: &str) -> bool {
        self.index.contains_key(name)
    }

    /// Raw f32 bytes and shape.
    pub fn raw(&self, name: &str) -> Result<(&[u8], Vec<usize>)> {
        let i = *self.index.get(name).with_context(|| format!("missing tensor {name}"))?;
        let map = &self.maps[i];
        let st = SafeTensors::deserialize(map)?;
        let t = st.tensor(name)?;
        if t.dtype() != Dtype::F32 {
            bail!("tensor {name} is {:?}, expected F32", t.dtype());
        }
        let shape = t.shape().to_vec();
        let data = t.data();
        let off = data.as_ptr() as usize - map.as_ptr() as usize;
        Ok((&map[off..off + data.len()], shape))
    }

    pub fn shape(&self, name: &str) -> Result<Vec<usize>> {
        Ok(self.raw(name)?.1)
    }

    pub fn f32(&self, name: &str) -> Result<Vec<f32>> {
        let (b, _) = self.raw(name)?;
        Ok(b.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect())
    }

    /// One row of a 2-D tensor as bf16 bits (host side, e.g. a text-embedding lookup).
    pub fn row_bf16(&self, name: &str, row: usize) -> Result<Vec<u16>> {
        let (b, shape) = self.raw(name)?;
        let w = shape[1];
        if row >= shape[0] {
            bail!("{name}: row {row} out of {}", shape[0]);
        }
        Ok(b[row * w * 4..(row + 1) * w * 4].chunks_exact(4).map(|c| bf16_bits(f32::from_le_bytes([c[0], c[1], c[2], c[3]]))).collect())
    }

    /// Upload tensors as bf16, concatenated along the first dimension.
    pub fn bf16(&self, gpu: &Gpu, names: &[String]) -> Result<Buf> {
        let mut parts = Vec::new();
        let mut total = 0;
        for n in names {
            let (b, _) = self.raw(n)?;
            total += b.len() / 4;
            parts.push(b);
        }
        let mut host = vec![0u16; total];
        let mut at = 0;
        for b in parts {
            let n = b.len() / 4;
            convert(b, &mut host[at..at + n]);
            at += n;
        }
        gpu.upload(&host)
    }

    pub fn bf16_one(&self, gpu: &Gpu, name: &str) -> Result<Buf> {
        self.bf16(gpu, &[name.to_string()])
    }

    /// A [n, rows, cols] tensor uploaded as n transposed [cols, rows] blocks (so `x @ w[i]` becomes a linear layer).
    pub fn bf16_transposed_blocks(&self, gpu: &Gpu, name: &str) -> Result<Buf> {
        let (b, shape) = self.raw(name)?;
        let (n, r, c) = match shape.as_slice() {
            [n, r, c] => (*n, *r, *c),
            [r, c] => (1, *r, *c),
            _ => bail!("{name}: unexpected shape {shape:?}"),
        };
        let mut conv = vec![0u16; n * r * c];
        convert(b, &mut conv);
        let mut host = vec![0u16; n * r * c];
        for k in 0..n {
            let src = &conv[k * r * c..(k + 1) * r * c];
            let dst = &mut host[k * r * c..(k + 1) * r * c];
            for i in 0..r {
                for j in 0..c {
                    dst[j * r + i] = src[i * c + j];
                }
            }
        }
        gpu.upload(&host)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bf16_rounding_matches_torch() {
        // 1 + 2^-8 is a tie between 1 and 1 + 2^-7: even mantissa wins (1.0).
        assert_eq!(bf16_bits(1.0 + 1.0 / 256.0), 0x3f80);
        assert_eq!(bf16_bits(1.0 + 3.0 / 256.0), 0x3f82);
        assert_eq!(bf16_value(bf16_bits(-2.5)), -2.5);
        assert_eq!(bf16_bits(f32::NAN), 0x7fc0);
    }
}
