// ok-tts CUDA kernels, compiled at runtime with NVRTC (no CUDA toolkit headers needed).
// bf16 is stored as unsigned short. Rounding mirrors PyTorch: math in f32, round to
// nearest-even once per op, so greedy decoding can be compared against the Python reference.

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

// ---------------------------------------------------------------- bf16 transformer (talker, predictor)

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

extern "C" __global__ void silu_bf16(bf16* x, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { float g = bf2f(x[i]); x[i] = f2bf(g / (1.f + __expf(-g))); }
}

// Fill each row with the bias so a following GEMM with beta=1 adds it inside the f32 accumulation.
extern "C" __global__ void fill_rows_bf16(bf16* out, const bf16* bias, int n, int rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < (long long)n * rows) out[i] = bias[i % n];
}

// Per-head RMSNorm (q_norm / k_norm) then rotate-half RoPE. One block per (token, head), blockDim = head_dim.
// x is [tokens, row_stride] with heads starting at x + head_offset. Result goes to dst:
// dst == 0 -> in place; otherwise dst is a KV cache [heads, cache_len, d] written at position pos0 + token.
// (The `_dp` variants of these three read pos0 from device memory, so one decode step can be captured in a CUDA
// graph and replayed at every position.)
__device__ __forceinline__ void qk_norm_rope(bf16* x, int row_stride, int head_offset, const bf16* w, int d,
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
extern "C" __global__ void qk_norm_rope_bf16(bf16* x, int row_stride, int head_offset, const bf16* w, int d,
                                             float eps, const float* inv_freq, int pos0, bf16* dst, int cache_len) {
    qk_norm_rope(x, row_stride, head_offset, w, d, eps, inv_freq, pos0, dst, cache_len);
}
extern "C" __global__ void qk_norm_rope_dp_bf16(bf16* x, int row_stride, int head_offset, const bf16* w, int d,
                                                float eps, const float* inv_freq, const int* pos, bf16* dst, int cache_len) {
    qk_norm_rope(x, row_stride, head_offset, w, d, eps, inv_freq, *pos, dst, cache_len);
}

// Copy V heads from the fused QKV rows into the cache [heads, cache_len, d].
__device__ __forceinline__ void store_v(const bf16* x, int row_stride, int head_offset, int d, int pos0,
                                        bf16* dst, int cache_len) {
    int token = blockIdx.x, head = blockIdx.y;
    for (int i = threadIdx.x; i < d; i += blockDim.x)
        dst[((long long)head * cache_len + pos0 + token) * d + i] = x[(long long)token * row_stride + head_offset + head * d + i];
}
extern "C" __global__ void store_v_bf16(const bf16* x, int row_stride, int head_offset, int d, int pos0,
                                        bf16* dst, int cache_len) {
    store_v(x, row_stride, head_offset, d, pos0, dst, cache_len);
}
extern "C" __global__ void store_v_dp_bf16(const bf16* x, int row_stride, int head_offset, int d, const int* pos,
                                           bf16* dst, int cache_len) {
    store_v(x, row_stride, head_offset, d, *pos, dst, cache_len);
}

// Causal attention against a KV cache. One block per (query, head); dynamic smem holds the scores.
// q rows are [tokens, q_stride]; out is [tokens, heads * d]. window <= 0 means unlimited.
__device__ __forceinline__ void attention(bf16* out, const bf16* q, int q_stride, const bf16* kc, const bf16* vc,
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
extern "C" __global__ void attention_bf16(bf16* out, const bf16* q, int q_stride, const bf16* kc, const bf16* vc,
                                          int heads, int kv_heads, int d, int cache_len, int pos0, int window, float scale) {
    attention(out, q, q_stride, kc, vc, heads, kv_heads, d, cache_len, pos0, window, scale);
}
// Dynamic smem must hold d + cache_len floats (the position is known only on the device).
extern "C" __global__ void attention_dp_bf16(bf16* out, const bf16* q, int q_stride, const bf16* kc, const bf16* vc,
                                             int heads, int kv_heads, int d, int cache_len, const int* pos, int window, float scale) {
    attention(out, q, q_stride, kc, vc, heads, kv_heads, d, cache_len, *pos, window, scale);
}

// out[0, :] = sum over rows of in[rows, h] (f32 accumulation, one rounding), plus optional add row.
extern "C" __global__ void sum_rows_bf16(bf16* out, const bf16* in, int rows, int h, const bf16* add) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= h) return;
    float s = 0.f;
    for (int r = 0; r < rows; ++r) s += bf2f(in[(long long)r * h + i]);
    float v = rbf(s);
    out[i] = add ? f2bf(v + bf2f(add[i])) : f2bf(v);
}

extern "C" __global__ void bf16_to_f32(float* out, const bf16* in, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = bf2f(in[i]);
}

// ---------------------------------------------------------------- f32 codec decoder ([C, T] layout)

extern "C" __global__ void fill_rows_f32(float* out, const float* bias, int cols, int rows_total, int per_row) {
    // per_row == 1: out[r, c] = bias[c] for [rows, cols];  per_row == 0: out[c, t] = bias[c] for [cols(=C), rows(=T)]
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)cols * rows_total) return;
    out[i] = per_row ? bias[i % cols] : bias[i / rows_total];
}

// Codebook gather: out[dim, t] (+)= table_q[codes[t * ncb + q], :] summed over q in [q0, q1)
extern "C" __global__ void rvq_gather(float* out, const float* tables, const int* codes, int ncb, int q0, int q1,
                                      int dim, int entries, int t_len) {
    int t = blockIdx.x;
    for (int c = threadIdx.x; c < dim; c += blockDim.x) {
        float s = 0.f;
        for (int q = q0; q < q1; ++q) s += tables[((long long)q * entries + codes[t * ncb + q]) * dim + c];
        out[(long long)c * t_len + t] = s;
    }
}

extern "C" __global__ void pad_left_f32(float* out, const float* in, int c, int t, int pad) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    int tp = t + pad;
    if (i >= (long long)c * tp) return;
    int ch = i / tp, x = i % tp;
    out[i] = x < pad ? 0.f : in[(long long)ch * t + x - pad];
}

// Depthwise causal conv: weight [C, k], input already left padded [C, T + (k-1)].
extern "C" __global__ void depthwise_conv_f32(float* out, const float* in_pad, const float* w, const float* b,
                                              int c, int t, int k) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * t) return;
    int ch = i / t, x = i % t;
    const float* row = in_pad + (long long)ch * (t + k - 1) + x;
    float s = b[ch];
    for (int j = 0; j < k; ++j) s += w[ch * k + j] * row[j];
    out[i] = s;
}

// Transposed conv overlap-add: y is [Co*K, T] from the GEMM, out is [Co, T*stride] (right trim K - stride).
extern "C" __global__ void overlap_add_f32(float* out, const float* y, const float* b, int co, int t, int k, int stride) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    int tout = t * stride;
    if (i >= (long long)co * tout) return;
    int ch = i / tout, x = i % tout;
    float s = b[ch];
    for (int j = x / stride; j >= 0; --j) {
        int kk = x - j * stride;
        if (kk >= k) break;
        if (j < t) s += y[((long long)ch * k + kk) * t + j];
    }
    out[i] = s;
}

// SnakeBeta with precomputed a = exp(alpha), ib = 1 / (exp(beta) + 1e-9). In place on [C, T].
extern "C" __global__ void snake_f32(float* out, const float* in, const float* a, const float* ib, int c, int t) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * t) return;
    int ch = i / t;
    float x = in[i], s = sinf(x * a[ch]);
    out[i] = x + ib[ch] * s * s;
}

// LayerNorm over channels for each time step of [C, T]; one block per t.
extern "C" __global__ void layernorm_cols_f32(float* out, const float* in, const float* w, const float* b, int c, int t, float eps) {
    __shared__ float scratch[32];
    int x = blockIdx.x;
    float s = 0.f;
    for (int ch = threadIdx.x; ch < c; ch += blockDim.x) s += in[(long long)ch * t + x];
    float mean = block_sum(s, scratch) / c;
    float v = 0.f;
    for (int ch = threadIdx.x; ch < c; ch += blockDim.x) { float d = in[(long long)ch * t + x] - mean; v += d * d; }
    float r = rsqrtf(block_sum(v, scratch) / c + eps);
    for (int ch = threadIdx.x; ch < c; ch += blockDim.x)
        out[(long long)ch * t + x] = (in[(long long)ch * t + x] - mean) * r * w[ch] + b[ch];
}

extern "C" __global__ void gelu_f32(float* x, long long n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { float v = x[i]; x[i] = 0.5f * v * (1.f + erff(v * 0.70710678118654752f)); }
}

// out = res + scale[c] * y. layout 0: [C, T] (c = i / t); layout 1: [T, C] (c = i % c).
extern "C" __global__ void add_scaled_f32(float* out, const float* res, const float* y, const float* scale, int c, int t, int layout) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * t) return;
    int ch = layout ? (int)(i % c) : (int)(i / t);
    out[i] = res[i] + (scale ? scale[ch] : 1.f) * y[i];
}

extern "C" __global__ void transpose_f32(float* out, const float* in, int rows, int cols) {
    __shared__ float tile[32][33];
    int bx = blockIdx.x * 32, by = blockIdx.y * 32;
    int x = bx + threadIdx.x, y = by + threadIdx.y;
    for (int j = 0; j < 32; j += 8) if (x < cols && y + j < rows) tile[threadIdx.y + j][threadIdx.x] = in[(long long)(y + j) * cols + x];
    __syncthreads();
    x = by + threadIdx.x; y = bx + threadIdx.y;
    for (int j = 0; j < 32; j += 8) if (x < rows && y + j < cols) out[(long long)(y + j) * rows + x] = tile[threadIdx.x][threadIdx.y + j];
}

extern "C" __global__ void rmsnorm_f32(float* out, const float* in, const float* w, int h, float eps) {
    __shared__ float scratch[32];
    const float* x = in + (long long)blockIdx.x * h;
    float s = 0.f;
    for (int i = threadIdx.x; i < h; i += blockDim.x) s += x[i] * x[i];
    float r = rsqrtf(block_sum(s, scratch) / h + eps);
    for (int i = threadIdx.x; i < h; i += blockDim.x) out[(long long)blockIdx.x * h + i] = w[i] * (x[i] * r);
}

extern "C" __global__ void silu_mul_f32(float* out, const float* gu, int inter, int rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)inter * rows) return;
    long long r = i / inter, c = i % inter;
    float g = gu[r * 2 * inter + c];
    out[i] = g / (1.f + expf(-g)) * gu[r * 2 * inter + inter + c];
}

// RoPE (rotate half) in place on q and k of the fused [T, 3*heads*d] rows; one block per (t, head), blockDim = d.
extern "C" __global__ void rope_f32(float* x, int row_stride, int head_offset, int d, const float* inv_freq) {
    __shared__ float v[256];
    int t = blockIdx.x, head = blockIdx.y, i = threadIdx.x;
    float* src = x + (long long)t * row_stride + head_offset + head * d;
    v[i] = src[i];
    __syncthreads();
    int half = d / 2;
    float f = (float)t * inv_freq[i % half];
    float rot = i < half ? -v[i + half] : v[i - half];
    src[i] = v[i] * cosf(f) + rot * sinf(f);
}

// Self attention over the whole sequence (no cache): q, k, v are column blocks of the fused [T, row_stride] rows.
extern "C" __global__ void attention_f32(float* out, const float* qkv, int row_stride, int heads, int d, int t_len,
                                         int window, float scale) {
    extern __shared__ float smem[];
    __shared__ float scratch[32];
    int p = blockIdx.x, head = blockIdx.y;
    int start = window > 0 ? max(0, p - window + 1) : 0;
    int len = p - start + 1;
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

extern "C" __global__ void clamp_f32(float* x, int n, float lo, float hi) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) x[i] = fminf(hi, fmaxf(lo, x[i]));
}

// ---------------------------------------------------------------- on-device sampling (no host round trip)

__device__ __forceinline__ unsigned long long mix64(unsigned long long z) {
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// One block (1024 threads). Order follows faster_qwen3_tts sample_logits:
// repetition penalty -> suppress -> (greedy argmax | temperature -> top-k -> softmax -> draw).
// suppress_from: ids >= suppress_from are banned except keep_id; ban_id (if >= 0) is banned too.
// seen: optional repetition mask (penalized ids); after drawing, seen[token] = 1 when update_seen.
// The drawn id goes to *out. If it equals eos_id, eos_step is lowered to `step` (atomicMin).
extern "C" __global__ void sample_bf16(const bf16* logits, int vocab, float temperature, int top_k, int greedy,
                                       int suppress_from, int keep_id, int ban_id, int* seen, float rep, int update_seen,
                                       unsigned long long seed, unsigned long long counter, int* out,
                                       int eos_id, int* eos_step, int step, const unsigned long long* rng) {
    // Inside a captured graph the seed and frame number come from device memory (rng[0], rng[1]).
    if (rng) { seed = rng[0]; counter = ((rng[1] + 1ull) << 8) | counter; }
    __shared__ float vals[4096];
    __shared__ float scratch[32];
    __shared__ int best_idx;
    for (int i = threadIdx.x; i < vocab; i += blockDim.x) {
        float v = bf2f(logits[i]);
        if (seen && seen[i] && rep != 1.f) v = rbf(v > 0.f ? v / rep : v * rep);
        if ((i >= suppress_from && i != keep_id) || i == ban_id) v = -INFINITY;
        vals[i] = v;
    }
    __syncthreads();
    float mx = -INFINITY;
    for (int i = threadIdx.x; i < vocab; i += blockDim.x) mx = fmaxf(mx, vals[i]);
    mx = block_max(mx, scratch);
    if (greedy || !(mx > -INFINITY)) {
        if (threadIdx.x == 0) best_idx = 0x7fffffff;
        __syncthreads();
        for (int i = threadIdx.x; i < vocab; i += blockDim.x) if (vals[i] == mx) atomicMin(&best_idx, i);
        __syncthreads();
    } else {
        float inv_t = 1.f / fmaxf(temperature, 1e-5f);
        for (int i = threadIdx.x; i < vocab; i += blockDim.x) vals[i] *= inv_t;
        __syncthreads();
        // k-th largest value (ties kept, like `logits < topk_vals[..., -1:]`).
        float thr = -INFINITY;
        if (top_k > 0 && top_k < vocab) {
            float above = INFINITY;
            int count = 0;
            while (count < top_k) {
                float m = -INFINITY;
                for (int i = threadIdx.x; i < vocab; i += blockDim.x) if (vals[i] < above) m = fmaxf(m, vals[i]);
                m = block_max(m, scratch);
                if (!(m > -INFINITY)) break;
                float c = 0.f;
                for (int i = threadIdx.x; i < vocab; i += blockDim.x) if (vals[i] == m) c += 1.f;
                count += (int)block_sum(c, scratch);
                above = m;
                thr = m;
            }
        }
        float top = mx * inv_t;
        float total = 0.f;
        for (int i = threadIdx.x; i < vocab; i += blockDim.x) {
            float p = vals[i] >= thr && vals[i] > -INFINITY ? __expf(vals[i] - top) : 0.f;
            vals[i] = p; total += p;
        }
        total = block_sum(total, scratch);
        if (threadIdx.x == 0) {
            double u = (double)(mix64(seed ^ mix64(counter)) >> 11) * (1.0 / 9007199254740992.0) * total;
            int pick = -1;
            for (int i = 0; i < vocab; ++i) {
                if (vals[i] == 0.f) continue;
                pick = i;
                if (u < vals[i]) break;
                u -= vals[i];
            }
            best_idx = pick < 0 ? 0 : pick;
        }
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        int t = best_idx;
        *out = t;
        if (seen && update_seen) seen[t] = 1;
        if (eos_step && t == eos_id) atomicMin(eos_step, step);
    }
}

// out = sum over 16 codebooks of tables[q][codes[q]] (+ extra row); tables is a device array of row pointers.
extern "C" __global__ void codec_sum_bf16(bf16* out, const unsigned long long* tables, const int* codes, int ncb, int h, const bf16* extra) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= h) return;
    float s = 0.f;
    for (int q = 0; q < ncb; ++q) s += bf2f(((const bf16*)tables[q])[(long long)codes[q] * h + i]);
    float v = rbf(s);
    out[i] = extra ? f2bf(v + bf2f(extra[i])) : f2bf(v);
}

extern "C" __global__ void inc_u64(unsigned long long* x) { if (threadIdx.x == 0) x[0] += 1ull; }

// y[r, o] = bf16(s[o] * sum_i x[r, i] * q[o, i]) for up to 4 rows (decoding a frame). The block first copies x into
// shared memory (rows * inp bf16, given as the launch's dynamic shared memory), then each warp takes one output row,
// 16 int8 weights per lane step. inp is a multiple of 16; x and q rows are 16-byte aligned.
extern "C" __global__ void gemv_i8_bf16(bf16* y, const bf16* x, const signed char* q, const float* s, int rows, int inp, int out) {
    extern __shared__ uint4 xsmem[];
    bf16* xs = (bf16*)xsmem;
    int n = rows * inp;
    for (int i = threadIdx.x * 8; i < n; i += blockDim.x * 8) *(uint4*)(xs + i) = *(const uint4*)(x + i);
    __syncthreads();
    int o = blockIdx.x * (blockDim.x >> 5) + (threadIdx.x >> 5), lane = threadIdx.x & 31;
    if (o >= out) return;
    float acc[4] = {0.f, 0.f, 0.f, 0.f};
    const int4* w = (const int4*)(q + (long long)o * inp);
    for (int i = lane; i * 16 < inp; i += 32) {
        int4 wv = __ldg(w + i);
        const signed char* wb = (const signed char*)&wv;
        float wf[16];
#pragma unroll
        for (int k = 0; k < 16; ++k) wf[k] = (float)wb[k];
#pragma unroll
        for (int r = 0; r < 4; ++r) {
            if (r < rows) {
                const uint4* xv = (const uint4*)(xs + r * inp + i * 16);
                uint4 a = xv[0], b = xv[1];
                const bf16* xa = (const bf16*)&a;
                const bf16* xb = (const bf16*)&b;
                float t = 0.f;
#pragma unroll
                for (int k = 0; k < 8; ++k) t += bf2f(xa[k]) * wf[k] + bf2f(xb[k]) * wf[8 + k];
                acc[r] += t;
            }
        }
    }
#pragma unroll
    for (int r = 0; r < 4; ++r) {
        float v = acc[r];
        for (int m = 16; m > 0; m >>= 1) v += __shfl_xor_sync(0xffffffffu, v, m);
        if (lane == 0 && r < rows) y[(long long)r * out + o] = f2bf(v * s[o]);
    }
}

// w[o, i] = bf16(q[o, i] * s[o]) (int8 matrix widened for a prefill GEMM).
extern "C" __global__ void i8_to_bf16(bf16* w, const signed char* q, const float* s, int inp, long long n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) w[i] = f2bf((float)q[i] * s[i / inp]);
}

