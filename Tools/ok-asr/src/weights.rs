//! safetensors access (memory mapped), as in Tools/ok-tts/src/weights.rs. The checkpoint is all bf16:
//! decoder tensors are uploaded as raw bf16, encoder tensors are widened to f32 on the host first.

use std::path::Path;

use anyhow::{bail, Context, Result};
use memmap2::Mmap;
use safetensors::{Dtype, SafeTensors};

use crate::gpu::{Buf, Gpu};

pub struct Store {
    map: Mmap,
}

impl Store {
    pub fn open(path: &Path) -> Result<Self> {
        let file = std::fs::File::open(path).with_context(|| format!("open {}", path.display()))?;
        let map = unsafe { Mmap::map(&file) }?;
        SafeTensors::deserialize(&map).with_context(|| format!("parse {}", path.display()))?;
        Ok(Self { map })
    }

    fn view(&self) -> SafeTensors<'_> {
        SafeTensors::deserialize(&self.map).expect("validated in open")
    }

    pub fn bytes(&self, name: &str, dtype: Dtype) -> Result<(&[u8], Vec<usize>)> {
        let st = self.view();
        let t = st.tensor(name).with_context(|| format!("missing tensor {name}"))?;
        if t.dtype() != dtype {
            bail!("tensor {name} is {:?}, expected {dtype:?}", t.dtype());
        }
        let shape = t.shape().to_vec();
        let data = t.data();
        // SafeTensors borrows from the mmap; re-slice from the map so the lifetime is tied to self.
        let offset = data.as_ptr() as usize - self.map.as_ptr() as usize;
        Ok((&self.map[offset..offset + data.len()], shape))
    }

    /// Upload one bf16 tensor.
    pub fn bf16(&self, gpu: &Gpu, name: &str) -> Result<Buf> {
        gpu.upload(self.bytes(name, Dtype::BF16)?.0)
    }

    /// Upload several bf16 tensors concatenated along the first (output) dimension.
    pub fn bf16_cat(&self, gpu: &Gpu, names: &[String]) -> Result<Buf> {
        let mut all = Vec::new();
        for n in names {
            all.extend_from_slice(self.bytes(n, Dtype::BF16)?.0);
        }
        gpu.upload(&all)
    }

    fn widen(&self, name: &str, out: &mut Vec<f32>) -> Result<()> {
        let (bytes, _) = self.bytes(name, Dtype::BF16)?;
        out.extend(bytes.chunks_exact(2).map(|c| f32::from_bits((u16::from_le_bytes([c[0], c[1]]) as u32) << 16)));
        Ok(())
    }

    /// Upload one bf16 tensor widened to f32.
    pub fn f32(&self, gpu: &Gpu, name: &str) -> Result<Buf> {
        self.f32_cat(gpu, &[name.to_string()])
    }

    /// Upload several bf16 tensors widened to f32, concatenated along the first dimension.
    pub fn f32_cat(&self, gpu: &Gpu, names: &[String]) -> Result<Buf> {
        let mut all = Vec::new();
        for n in names {
            self.widen(n, &mut all)?;
        }
        gpu.upload(&all)
    }
}
