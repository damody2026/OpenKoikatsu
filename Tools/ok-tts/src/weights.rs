//! safetensors access (memory mapped). bf16 tensors are uploaded as raw bytes; f32 tensors can be read on the host.

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

    pub fn f32(&self, name: &str) -> Result<(Vec<f32>, Vec<usize>)> {
        let (bytes, shape) = self.bytes(name, Dtype::F32)?;
        Ok((bytes.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect(), shape))
    }
}
