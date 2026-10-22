//! Thin CUDA layer: one context + stream, NVRTC-compiled kernels, cuBLAS GEMMs, raw device buffers.
//! Buffers use the stream-ordered allocator, so per-step temporaries are cheap.

use std::collections::HashMap;
use std::ffi::c_void;
use std::sync::Arc;

use anyhow::{anyhow, bail, Context, Result};
use cudarc::cublas::sys as cb;
use cudarc::driver::{result as dr, sys as ds, CudaContext, CudaFunction, CudaStream};

/// ok-tts kernels; ok-song includes this file and appends its own source.
pub const KERNELS: &str = include_str!("kernels.cu");
pub const KERNEL_NAMES: &[&str] = &[
    "embed_bf16", "rmsnorm_bf16", "add_bf16", "silu_mul_bf16", "silu_bf16", "fill_rows_bf16",
    "qk_norm_rope_bf16", "store_v_bf16", "attention_bf16", "sum_rows_bf16", "bf16_to_f32",
    "fill_rows_f32", "rvq_gather", "pad_left_f32", "depthwise_conv_f32", "overlap_add_f32", "snake_f32",
    "layernorm_cols_f32", "gelu_f32", "add_scaled_f32", "transpose_f32", "rmsnorm_f32", "silu_mul_f32",
    "rope_f32", "attention_f32", "clamp_f32", "sample_bf16", "codec_sum_bf16", "inc_u64",
];

/// Device buffer owned by the stream-ordered allocator.
pub struct Buf {
    pub ptr: u64,
    pub bytes: usize,
    stream: ds::CUstream,
}

unsafe impl Send for Buf {}

impl Buf {
    pub fn at(&self, byte_offset: usize) -> u64 {
        debug_assert!(byte_offset <= self.bytes);
        self.ptr + byte_offset as u64
    }
}

impl Drop for Buf {
    fn drop(&mut self) {
        if self.ptr != 0 {
            unsafe {
                let _ = dr::free_async(self.ptr, self.stream);
            }
        }
    }
}

pub enum Arg {
    P(u64),
    I(i32),
    F(f32),
    L(i64),
}

/// Fields drop in order: cuBLAS and its workspace (freed on `stream`) before the stream and the context.
pub struct Gpu {
    blas: cudarc::cublas::CudaBlas,
    _blas_workspace: Buf,
    funcs: HashMap<&'static str, CudaFunction>,
    cs: ds::CUstream,
    pub stream: Arc<CudaStream>,
    pub ctx: Arc<CudaContext>,
}

/// An instantiated CUDA graph, replayed on the engine stream.
pub struct Graph {
    exec: ds::CUgraphExec,
    graph: ds::CUgraph,
}

unsafe impl Send for Graph {}

impl Drop for Graph {
    fn drop(&mut self) {
        unsafe {
            ds::cuGraphExecDestroy(self.exec);
            ds::cuGraphDestroy(self.graph);
        }
    }
}

unsafe impl Send for Gpu {}

impl Gpu {
    pub fn new(device: usize) -> Result<Self> {
        Self::with_kernels(device, KERNELS, KERNEL_NAMES)
    }

    /// A device with the given NVRTC source and the kernel names to load from it.
    pub fn with_kernels(device: usize, source: &str, names: &[&'static str]) -> Result<Self> {
        let ctx = CudaContext::new(device).context("CUDA device (driver / nvcuda.dll)")?;
        let stream = ctx.new_stream()?;
        let (major, minor) = (
            ctx.attribute(ds::CUdevice_attribute::CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR)?,
            ctx.attribute(ds::CUdevice_attribute::CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR)?,
        );
        let arch: &'static str = Box::leak(format!("compute_{major}{minor}").into_boxed_str());
        let opts = cudarc::nvrtc::CompileOptions { arch: Some(arch), use_fast_math: Some(false), ..Default::default() };
        let ptx = cudarc::nvrtc::compile_ptx_with_opts(source, opts).map_err(|e| anyhow!("NVRTC: {e:?}"))?;
        let module = ctx.load_module(ptx)?;
        let mut funcs = HashMap::new();
        for name in names {
            funcs.insert(*name, module.load_function(name).with_context(|| format!("kernel {name}"))?);
        }
        let blas = cudarc::cublas::CudaBlas::new(stream.clone())?;
        let cs = stream.cu_stream();
        // A fixed cuBLAS workspace keeps GEMMs capturable in CUDA graphs.
        const WORKSPACE: usize = 64 << 20;
        let ws = unsafe { dr::malloc_sync(WORKSPACE) }?;
        let _blas_workspace = Buf { ptr: ws, bytes: WORKSPACE, stream: cs };
        let st = unsafe { cb::cublasSetWorkspace_v2(*blas.handle(), ws as *mut c_void, WORKSPACE) };
        if st != cb::cublasStatus_t::CUBLAS_STATUS_SUCCESS {
            bail!("cublasSetWorkspace failed: {st:?}");
        }
        Ok(Self { ctx, stream, cs, blas, funcs, _blas_workspace })
    }

    pub fn bind(&self) -> Result<()> {
        self.ctx.bind_to_thread()?;
        Ok(())
    }

    /// Give the device's pooled free memory back. Engines share the device's memory pool, so what a dropped engine
    /// (VoiceDesign loaded for a while beside Base) freed would otherwise stay reserved by this process.
    pub fn trim_pool(&self) -> Result<()> {
        self.bind()?;
        self.sync()?;
        unsafe {
            let mut pool: ds::CUmemoryPool = std::ptr::null_mut();
            let r = ds::cuDeviceGetMemPool(&mut pool, self.ctx.cu_device());
            if r != ds::CUresult::CUDA_SUCCESS {
                bail!("cuDeviceGetMemPool: {r:?}");
            }
            let r = ds::cuMemPoolTrimTo(pool, 0);
            if r != ds::CUresult::CUDA_SUCCESS {
                bail!("cuMemPoolTrimTo: {r:?}");
            }
        }
        Ok(())
    }

    pub fn alloc(&self, bytes: usize) -> Result<Buf> {
        let ptr = unsafe { dr::malloc_async(self.cs, bytes.max(4)) }?;
        Ok(Buf { ptr, bytes, stream: self.cs })
    }

    pub fn zeros(&self, bytes: usize) -> Result<Buf> {
        let b = self.alloc(bytes)?;
        unsafe { dr::memset_d8_async(b.ptr, 0, bytes.max(4), self.cs) }?;
        Ok(b)
    }

    pub fn upload<T: Copy>(&self, data: &[T]) -> Result<Buf> {
        let bytes = std::mem::size_of_val(data);
        let b = self.alloc(bytes)?;
        unsafe { dr::memcpy_htod_async(b.ptr, data, self.cs) }?;
        // The host slice may be freed right after; finish the copy now.
        self.sync()?;
        Ok(b)
    }

    pub fn write<T: Copy>(&self, dst: u64, data: &[T]) -> Result<()> {
        unsafe { dr::memcpy_htod_async(dst, data, self.cs) }?;
        self.sync()
    }

    /// Record everything `body` enqueues on the stream into a graph (no host syncs allowed inside).
    pub fn capture(&self, body: impl FnOnce() -> Result<()>) -> Result<Graph> {
        self.sync()?;
        unsafe { ds::cuStreamBeginCapture_v2(self.cs, ds::CUstreamCaptureMode::CU_STREAM_CAPTURE_MODE_RELAXED) }.result()?;
        let recorded = body();
        let mut graph = std::ptr::null_mut();
        let ended = unsafe { ds::cuStreamEndCapture(self.cs, &mut graph) }.result();
        recorded?;
        ended?;
        let mut exec = std::ptr::null_mut();
        unsafe { ds::cuGraphInstantiateWithFlags(&mut exec, graph, 0) }.result()?;
        Ok(Graph { exec, graph })
    }

    pub fn replay(&self, g: &Graph) -> Result<()> {
        unsafe { ds::cuGraphLaunch(g.exec, self.cs) }.result()?;
        Ok(())
    }

    pub fn read<T: Copy + Default>(&self, src: u64, count: usize) -> Result<Vec<T>> {
        let mut out = vec![T::default(); count];
        self.sync()?;
        unsafe { dr::memcpy_dtoh_sync(&mut out, src) }?;
        Ok(out)
    }

    pub fn copy(&self, dst: u64, src: u64, bytes: usize) -> Result<()> {
        unsafe { dr::memcpy_dtod_async(dst, src, bytes, self.cs) }?;
        Ok(())
    }

    pub fn sync(&self) -> Result<()> {
        self.stream.synchronize()?;
        Ok(())
    }

    pub fn launch(&self, name: &str, grid: (u32, u32, u32), block: (u32, u32, u32), smem: u32, args: &[Arg]) -> Result<()> {
        let f = self.funcs.get(name).ok_or_else(|| anyhow!("unknown kernel {name}"))?;
        let mut store: Vec<[u8; 8]> = args
            .iter()
            .map(|a| {
                let mut v = [0u8; 8];
                match a {
                    Arg::P(p) => v.copy_from_slice(&p.to_le_bytes()),
                    Arg::L(l) => v.copy_from_slice(&l.to_le_bytes()),
                    Arg::I(i) => v[..4].copy_from_slice(&i.to_le_bytes()),
                    Arg::F(x) => v[..4].copy_from_slice(&x.to_le_bytes()),
                }
                v
            })
            .collect();
        let mut params: Vec<*mut c_void> = store.iter_mut().map(|v| v.as_mut_ptr() as *mut c_void).collect();
        unsafe { dr::launch_kernel(f.cu_function(), grid, block, smem, self.cs, &mut params) }
            .with_context(|| format!("launch {name}"))?;
        Ok(())
    }

    /// 1-D launch helper: `n` threads in blocks of 256.
    pub fn launch1(&self, name: &str, n: usize, args: &[Arg]) -> Result<()> {
        let blocks = n.div_ceil(256).max(1) as u32;
        self.launch(name, (blocks, 1, 1), (256, 1, 1), 0, args)
    }

    /// Row-major linear layer: y[rows, out] = x[rows, inp] * w[out, inp]^T (+ beta * y).
    pub fn linear_bf16(&self, y: u64, x: u64, w: u64, rows: usize, inp: usize, out: usize, beta: f32) -> Result<()> {
        let alpha = 1.0f32;
        let status = unsafe {
            cb::cublasGemmEx(
                *self.blas.handle(),
                cb::cublasOperation_t::CUBLAS_OP_T,
                cb::cublasOperation_t::CUBLAS_OP_N,
                out as i32,
                rows as i32,
                inp as i32,
                &alpha as *const f32 as *const c_void,
                w as *const c_void,
                cb::cudaDataType::CUDA_R_16BF,
                inp as i32,
                x as *const c_void,
                cb::cudaDataType::CUDA_R_16BF,
                inp as i32,
                &beta as *const f32 as *const c_void,
                y as *mut c_void,
                cb::cudaDataType::CUDA_R_16BF,
                out as i32,
                cb::cublasComputeType_t::CUBLAS_COMPUTE_32F,
                cb::cublasGemmAlgo_t::CUBLAS_GEMM_DEFAULT,
            )
        };
        if status != cb::cublasStatus_t::CUBLAS_STATUS_SUCCESS {
            bail!("cublasGemmEx failed: {status:?}");
        }
        Ok(())
    }

    /// Column-major SGEMM, straight through to cuBLAS.
    #[allow(clippy::too_many_arguments)]
    pub fn sgemm(&self, ta: bool, tb: bool, m: usize, n: usize, k: usize, a: u64, lda: usize, b: u64, ldb: usize,
                 beta: f32, c: u64, ldc: usize) -> Result<()> {
        let alpha = 1.0f32;
        let op = |t: bool| if t { cb::cublasOperation_t::CUBLAS_OP_T } else { cb::cublasOperation_t::CUBLAS_OP_N };
        let status = unsafe {
            cb::cublasSgemm_v2(*self.blas.handle(), op(ta), op(tb), m as i32, n as i32, k as i32, &alpha,
                               a as *const f32, lda as i32, b as *const f32, ldb as i32, &beta, c as *mut f32, ldc as i32)
        };
        if status != cb::cublasStatus_t::CUBLAS_STATUS_SUCCESS {
            bail!("cublasSgemm failed: {status:?}");
        }
        Ok(())
    }

    /// Row-major f32 linear: y[rows, out] = x[rows, inp] * w[out, inp]^T (+ beta * y).
    pub fn linear_f32(&self, y: u64, x: u64, w: u64, rows: usize, inp: usize, out: usize, beta: f32) -> Result<()> {
        self.sgemm(true, false, out, rows, inp, w, inp, x, inp, beta, y, out)
    }

    /// Channel-major f32 matmul: y[co, t] = w[co, ci] * x[ci, t] where x rows have stride `ldx` (+ beta * y).
    #[allow(clippy::too_many_arguments)]
    pub fn chan_matmul_f32(&self, y: u64, w: u64, x: u64, co: usize, ci: usize, t: usize, ldx: usize, beta: f32) -> Result<()> {
        self.sgemm(false, false, t, co, ci, x, ldx, w, ci, beta, y, t)
    }
}

pub fn ptr_f32(base: u64, index: usize) -> u64 {
    base + (index * 4) as u64
}

pub fn ptr_bf16(base: u64, index: usize) -> u64 {
    base + (index * 2) as u64
}
