// ok-asr CUDA kernels, compiled at runtime with NVRTC (no CUDA toolkit headers needed).
// The bf16 decoder kernels are the ones from Tools/ok-tts/src/kernels.cu: math in f32, round to
// nearest-even once per op like PyTorch. The audio encoder runs in f32.

typedef unsigned short bf16;
#define INFINITY __int_as_float(0x7f800000)

__device__ __forceinline__ float bf2f(bf16 h) { return __uint_as_float(((unsigned int)h) << 16); }
__device__ __forceinline__ bf16 f2bf(float f) {
    unsigned int u = __float_as_uint(f);
    if ((u & 0x7fffffffu) > 0x7f800000u) return (bf16)0x7fc0;
    u += 0x7fffu + ((u >> 16) & 1u);
    return (bf16)(u >> 16);
}
__device__ __forceinline__ float rbf(float f) { return bf2f(f2bf(f)); }

__device__ float block_sum(float v, float* scratch) {
    for (int o = 16; o > 0; o >>= 1) v += __shfl_xor_sync(0xffffffffu, v, o);
    int lane = threadIdx.x & 31, warp = threadIdx.x >> 5;
    __syncthreads();
    if (lane == 0) scratch[warp] = v;
    __syncthreads();
    int warps = (blockDim.x + 31) >> 5;
    v = (threadIdx.x < warps) ? scratch[threadIdx.x] : 0.f;
    if (warp == 0) for (int o = 16; o > 0; o >>= 1) v += __shfl_xor_sync(0xffffffffu, v, o);
    if (threadIdx.x == 0) scratch[0] = v;
    __syncthreads();
    float r = scratch[0];
    __syncthreads();
    return r;
}
__device__ float block_max(float v, float* scratch) {
    for (int o = 16; o > 0; o >>= 1) v = fmaxf(v, __shfl_xor_sync(0xffffffffu, v, o));
    int lane = threadIdx.x & 31, warp = threadIdx.x >> 5;
    __syncthreads();
    if (lane == 0) scratch[warp] = v;
    __syncthreads();
    int warps = (blockDim.x + 31) >> 5;
    v = (threadIdx.x < warps) ? scratch[threadIdx.x] : -INFINITY;
    if (warp == 0) for (int o = 16; o > 0; o >>= 1) v = fmaxf(v, __shfl_xor_sync(0xffffffffu, v, o));
    if (threadIdx.x == 0) scratch[0] = v;
    __syncthreads();
    float r = scratch[0];
    __syncthreads();
    return r;
}

// ---------------------------------------------------------------- bf16 Qwen3 decoder (from ok-tts)

extern "C" __global__ void embed_bf16(bf16* out, const bf16* table, const int* ids, int h) {
    const bf16* row = table + (long long)ids[blockIdx.x] * h;
    bf16* dst = out + (long long)blockIdx.x * h;
    for (int i = threadIdx.x; i < h; i += blockDim.x) dst[i] = row[i];
}

// Qwen3 RMSNorm: f32 math, cast to bf16, then weight * x in bf16.
extern "C" __global__ void rmsnorm_bf16(bf16* out, const bf16* in, const bf16* w, int h, float eps) {
    __shared__ float scratch[32];
    const bf16* x = in + (long long)blockIdx.x * h;
    bf16* y = out + (long long)blockIdx.x * h;
    float s = 0.f;
    for (int i = threadIdx.x; i < h; i += blockDim.x) { float v = bf2f(x[i]); s += v * v; }
    float r = rsqrtf(block_sum(s, scratch) / h + eps);
    for (int i = threadIdx.x; i < h; i += blockDim.x) y[i] = f2bf(bf2f(w[i]) * rbf(bf2f(x[i]) * r));
}

extern "C" __global__ void add_bf16(bf16* out, const bf16* a, const bf16* b, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = f2bf(bf2f(a[i]) + bf2f(b[i]));
}

// out[r, i] = silu(gu[r, i]) * gu[r, inter + i]   (gate and up projections are fused in one GEMM)
extern "C" __global__ void silu_mul_bf16(bf16* out, const bf16* gu, int inter, int rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)inter * rows) return;
    long long r = i / inter, c = i % inter;
    float g = bf2f(gu[r * 2 * inter + c]);
    float u = bf2f(gu[r * 2 * inter + inter + c]);
    out[i] = f2bf(rbf(g / (1.f + __expf(-g))) * u);
}

// Per-head RMSNorm (q_norm / k_norm) then rotate-half RoPE. One block per (token, head), blockDim = head_dim.
// x is [tokens, row_stride] with heads starting at x + head_offset. Result goes to dst:
// dst == 0 -> in place; otherwise dst is a KV cache [heads, cache_len, d] written at position pos0 + token.
extern "C" __global__ void qk_norm_rope_bf16(bf16* x, int row_stride, int head_offset, const bf16* w, int d,
                                             float eps, const float* inv_freq, int pos0, bf16* dst, int cache_len) {
    __shared__ float scratch[32];
    __shared__ float v[256];
    int token = blockIdx.x, head = blockIdx.y, i = threadIdx.x;
    bf16* src = x + (long long)token * row_stride + head_offset + head * d;
    float xv = bf2f(src[i]);
    float r = rsqrtf(block_sum(xv * xv, scratch) / d + eps);
    v[i] = w ? bf2f(f2bf(bf2f(w[i]) * rbf(xv * r))) : xv;
    __syncthreads();
    int half = d / 2;
    float pos = (float)(pos0 + token);
    float freq = pos * inv_freq[i % half];
    float c = rbf(cosf(freq)), s = rbf(sinf(freq));
    float rot = i < half ? -v[i + half] : v[i - half];
    bf16 outv = f2bf(rbf(v[i] * c) + rbf(rot * s));
    if (dst) dst[((long long)head * cache_len + pos0 + token) * d + i] = outv;
    else src[i] = outv;
}

// Copy V heads from the fused QKV rows into the cache [heads, cache_len, d].
extern "C" __global__ void store_v_bf16(const bf16* x, int row_stride, int head_offset, int d, int pos0,
                                        bf16* dst, int cache_len) {
    int token = blockIdx.x, head = blockIdx.y;
    for (int i = threadIdx.x; i < d; i += blockDim.x)
        dst[((long long)head * cache_len + pos0 + token) * d + i] = x[(long long)token * row_stride + head_offset + head * d + i];
}

// Causal attention against a KV cache. One block per (query, head); dynamic smem holds the scores.
// q rows are [tokens, q_stride]; out is [tokens, heads * d]. window <= 0 means unlimited.
extern "C" __global__ void attention_bf16(bf16* out, const bf16* q, int q_stride, const bf16* kc, const bf16* vc,
                                          int heads, int kv_heads, int d, int cache_len, int pos0, int window, float scale) {
    extern __shared__ float smem[];
    __shared__ float scratch[32];
    int token = blockIdx.x, head = blockIdx.y, kvh = head / (heads / kv_heads);
    int p = pos0 + token;
    int start = window > 0 ? max(0, p - window + 1) : 0;
    int len = p - start + 1;
    float* qv = smem;          // d
    float* sc = smem + d;      // len
    for (int i = threadIdx.x; i < d; i += blockDim.x) qv[i] = bf2f(q[(long long)token * q_stride + head * d + i]);
    __syncthreads();
    const bf16* kbase = kc + (long long)kvh * cache_len * d;
    const bf16* vbase = vc + (long long)kvh * cache_len * d;
    float mx = -INFINITY;
    for (int j = threadIdx.x; j < len; j += blockDim.x) {
        const bf16* kr = kbase + (long long)(start + j) * d;
        float s = 0.f;
        for (int i = 0; i < d; ++i) s += qv[i] * bf2f(kr[i]);
        s *= scale; sc[j] = s; mx = fmaxf(mx, s);
    }
    mx = block_max(mx, scratch);
    float sum = 0.f;
    for (int j = threadIdx.x; j < len; j += blockDim.x) { float e = __expf(sc[j] - mx); sc[j] = e; sum += e; }
    sum = block_sum(sum, scratch);
    float inv = 1.f / sum;
    for (int i = threadIdx.x; i < d; i += blockDim.x) {
        float acc = 0.f;
        for (int j = 0; j < len; ++j) acc += sc[j] * bf2f(vbase[(long long)(start + j) * d + i]);
        out[(long long)token * heads * d + head * d + i] = f2bf(acc * inv);
    }
}

// Greedy pick over a full vocabulary row; ties go to the lowest id like torch.argmax. One block.
extern "C" __global__ void argmax_bf16(const bf16* logits, int vocab, int* out) {
    __shared__ float sv[32];
    __shared__ int si[32];
    float best = -INFINITY;
    int bi = 0x7fffffff;
    for (int i = threadIdx.x; i < vocab; i += blockDim.x) {
        float v = bf2f(logits[i]);
        if (v > best || (v == best && i < bi)) { best = v; bi = i; }
    }
    for (int o = 16; o > 0; o >>= 1) {
        float ov = __shfl_xor_sync(0xffffffffu, best, o);
        int oi = __shfl_xor_sync(0xffffffffu, bi, o);
        if (ov > best || (ov == best && oi < bi)) { best = ov; bi = oi; }
    }
    int lane = threadIdx.x & 31, warp = threadIdx.x >> 5;
    if (lane == 0) { sv[warp] = best; si[warp] = bi; }
    __syncthreads();
    if (warp == 0) {
        int warps = (blockDim.x + 31) >> 5;
        best = lane < warps ? sv[lane] : -INFINITY;
        bi = lane < warps ? si[lane] : 0x7fffffff;
        for (int o = 16; o > 0; o >>= 1) {
            float ov = __shfl_xor_sync(0xffffffffu, best, o);
            int oi = __shfl_xor_sync(0xffffffffu, bi, o);
            if (ov > best || (ov == best && oi < bi)) { best = ov; bi = oi; }
        }
        if (lane == 0) *out = bi == 0x7fffffff ? 0 : bi;
    }
}

extern "C" __global__ void f32_to_bf16(bf16* out, const float* in, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = f2bf(in[i]);
}

// ---------------------------------------------------------------- f32 audio encoder

extern "C" __global__ void fill_rows_f32(float* out, const float* bias, int cols, int rows_total, int per_row) {
    // per_row == 1: out[r, c] = bias[c] for [rows, cols];  per_row == 0: out[c, t] = bias[c] for [cols(=C), rows(=T)]
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)cols * rows_total) return;
    out[i] = per_row ? bias[i % cols] : bias[i / rows_total];
}

extern "C" __global__ void gelu_f32(float* x, long long n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { float v = x[i]; x[i] = 0.5f * v * (1.f + erff(v * 0.70710678118654752f)); }
}

extern "C" __global__ void add_f32(float* out, const float* a, const float* b, long long n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = a[i] + b[i];
}

// Conv2d(k=3, stride=2, padding=1) as im2col: cols[(ch*9 + ky*3 + kx), (oy*wo + ox)] from in[ch, h, w].
// The row order matches a PyTorch weight [co, ci, 3, 3] viewed as [co, ci*9].
extern "C" __global__ void im2col_3x3s2_f32(float* cols, const float* in, int c, int h, int w, int ho, int wo) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    long long plane = (long long)ho * wo;
    if (i >= (long long)c * 9 * plane) return;
    int col = (int)(i % plane);
    long long row = i / plane;
    int k = (int)(row % 9), ch = (int)(row / 9);
    int y = (col / wo) * 2 - 1 + k / 3, x = (col % wo) * 2 - 1 + k % 3;
    cols[i] = (y >= 0 && y < h && x >= 0 && x < w) ? in[((long long)ch * h + y) * w + x] : 0.f;
}

// [c, f, t] -> rows [t, c * f]  (permute(0, 3, 1, 2).view(b, t, c * f) for one chunk)
extern "C" __global__ void cnn_rows_f32(float* out, const float* in, int c, int f, int t) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    long long width = (long long)c * f;
    if (i >= width * t) return;
    int tt = (int)(i / width);
    int r = (int)(i % width);
    out[i] = in[(long long)r * t + tt];
}

// nn.LayerNorm over the last dimension of [rows, h]; one block per row.
extern "C" __global__ void layernorm_rows_f32(float* out, const float* in, const float* w, const float* b, int h, float eps) {
    __shared__ float scratch[32];
    const float* x = in + (long long)blockIdx.x * h;
    float* y = out + (long long)blockIdx.x * h;
    float s = 0.f;
    for (int i = threadIdx.x; i < h; i += blockDim.x) s += x[i];
    float mean = block_sum(s, scratch) / h;
    float v = 0.f;
    for (int i = threadIdx.x; i < h; i += blockDim.x) { float d = x[i] - mean; v += d * d; }
    float r = rsqrtf(block_sum(v, scratch) / h + eps);
    for (int i = threadIdx.x; i < h; i += blockDim.x) y[i] = (x[i] - mean) * r * w[i] + b[i];
}

// Bidirectional attention inside fixed windows of `window` tokens (the encoder's cu_seqlens blocks).
// q, k, v are column blocks of the fused [n, row_stride] rows; out is [n, heads * d].
extern "C" __global__ void attention_window_f32(float* out, const float* qkv, int row_stride, int heads, int d, int n,
                                                int window, float scale) {
    extern __shared__ float smem[];
    __shared__ float scratch[32];
    int p = blockIdx.x, head = blockIdx.y;
    int start = (p / window) * window;
    int len = min(start + window, n) - start;
    float* qv = smem; float* sc = smem + d;
    const float* q = qkv + (long long)p * row_stride + head * d;
    for (int i = threadIdx.x; i < d; i += blockDim.x) qv[i] = q[i];
    __syncthreads();
    int koff = heads * d, voff = 2 * heads * d;
    float mx = -INFINITY;
    for (int j = threadIdx.x; j < len; j += blockDim.x) {
        const float* kr = qkv + (long long)(start + j) * row_stride + koff + head * d;
        float s = 0.f;
        for (int i = 0; i < d; ++i) s += qv[i] * kr[i];
        s *= scale; sc[j] = s; mx = fmaxf(mx, s);
    }
    mx = block_max(mx, scratch);
    float sum = 0.f;
    for (int j = threadIdx.x; j < len; j += blockDim.x) { float e = expf(sc[j] - mx); sc[j] = e; sum += e; }
    sum = block_sum(sum, scratch);
    for (int i = threadIdx.x; i < d; i += blockDim.x) {
        float acc = 0.f;
        for (int j = 0; j < len; ++j) acc += sc[j] * qkv[(long long)(start + j) * row_stride + voff + head * d + i];
        out[(long long)p * heads * d + head * d + i] = acc / sum;
    }
}
