// ok-song CUDA kernels, appended after the ok-tts kernels (which provide bf16 helpers, block_sum / block_max,
// RMSNorm, SwiGLU, KV-cache attention and mix64). Same rounding rule: f32 math, one bf16 rounding per PyTorch op.

// Interleaved Llama-3 RoPE (torchtune Llama3ScaledRoPE): pairs (x[2j], x[2j+1]) rotate by pos * theta[j], all in f32,
// one rounding. One block per (token, head), blockDim = d / 2. dst == 0 -> in place; otherwise dst is a KV cache
// [heads, cache_len, d] written at position pos0 + token.
// y[r, o] = bf16(s[o] * sum_i x[r, i] * q[o, i]) for up to 4 rows (decoding a frame): one warp per output row, 16 int8
// weights per lane step. inp is a multiple of 16; x rows and q rows are 32 / 16-byte aligned.
extern "C" __global__ void gemv_i8_bf16(bf16* y, const bf16* x, const signed char* q, const float* s, int rows, int inp, int out) {
    int o = (blockIdx.x * blockDim.x + threadIdx.x) >> 5, lane = threadIdx.x & 31;
    if (o >= out) return;
    float acc[4] = {0.f, 0.f, 0.f, 0.f};
    const signed char* w = q + (long long)o * inp;
    for (int i = lane * 16; i < inp; i += 32 * 16) {
        int4 wv = *(const int4*)(w + i);
        const signed char* wb = (const signed char*)&wv;
#pragma unroll
        for (int r = 0; r < 4; ++r) {
            if (r < rows) {
                const uint4* xv = (const uint4*)(x + (long long)r * inp + i);
                uint4 a = xv[0], b = xv[1];
                const bf16* xa = (const bf16*)&a;
                const bf16* xb = (const bf16*)&b;
                float t = 0.f;
#pragma unroll
                for (int k = 0; k < 8; ++k) t += bf2f(xa[k]) * (float)wb[k] + bf2f(xb[k]) * (float)wb[8 + k];
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

extern "C" __global__ void rope_il_bf16(bf16* x, int row_stride, int head_offset, int d, const float* theta, int pos0,
                                        bf16* dst, int cache_len) {
    int token = blockIdx.x, head = blockIdx.y, j = threadIdx.x;
    if (j >= d / 2) return;
    bf16* src = x + (long long)token * row_stride + head_offset + head * d;
    float a = bf2f(src[2 * j]), b = bf2f(src[2 * j + 1]);
    float ang = (float)(pos0 + token) * theta[j];
    float c = cosf(ang), s = sinf(ang);
    bf16 o0 = f2bf(a * c - b * s), o1 = f2bf(b * c + a * s);
    if (dst) {
        bf16* row = dst + ((long long)head * cache_len + pos0 + token) * d;
        row[2 * j] = o0; row[2 * j + 1] = o1;
    } else {
        src[2 * j] = o0; src[2 * j + 1] = o1;
    }
}

// rows x h: every row gets table[*tok] (one sampled audio token broadcast to all CFG rows).
extern "C" __global__ void embed_one_bf16(bf16* out, const bf16* table, const int* tok, int h, int rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)h * rows) return;
    out[i] = table[(long long)tok[0] * h + i % h];
}

// out[r, :] = sum over the codebooks of audio_emb[codes[q] + q * vocab] (f32 sum, one rounding), for every row r.
extern "C" __global__ void audio_sum_bf16(bf16* out, const bf16* table, const int* codes, int ncb, int vocab, int h, int rows) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= h) return;
    float s = 0.f;
    for (int q = 0; q < ncb; ++q) s += bf2f(table[((long long)codes[q] + (long long)q * vocab) * h + i]);
    bf16 v = f2bf(s);
    for (int r = 0; r < rows; ++r) out[(long long)r * h + i] = v;
}

// Classifier-free guidance + top-k sampling of one token. logits: rows (1 or 2) x vocab bf16; with 2 rows the
// guided logits are uncond + (cond - uncond) * cfg, rounded per op like the bf16 tensors in HeartMuLa.
// top_k <= 1 is greedy. The token goes to *out; *stop is set to 1 when it is >= stop_from. One block of 1024.
#define SAMPLE_MAX 8448
extern "C" __global__ void sample_cfg_bf16(const bf16* logits, int rows, int vocab, float cfg, float temperature,
                                           int top_k, const unsigned long long* rng, int slot, int* out,
                                           int stop_from, int* stop) {
    __shared__ float vals[SAMPLE_MAX];
    __shared__ float scratch[32];
    __shared__ int best_idx;
    for (int i = threadIdx.x; i < vocab; i += blockDim.x) {
        float c = bf2f(logits[i]);
        float v = c;
        if (rows == 2) {
            float u = bf2f(logits[vocab + i]);
            v = rbf(u + rbf(rbf(c - u) * cfg));
        }
        vals[i] = v;
    }
    __syncthreads();
    float mx = -INFINITY;
    for (int i = threadIdx.x; i < vocab; i += blockDim.x) mx = fmaxf(mx, vals[i]);
    mx = block_max(mx, scratch);
    if (top_k <= 1) {
        if (threadIdx.x == 0) best_idx = 0x7fffffff;
        __syncthreads();
        for (int i = threadIdx.x; i < vocab; i += blockDim.x) if (vals[i] == mx) atomicMin(&best_idx, i);
        __syncthreads();
    } else {
        float inv_t = 1.f / fmaxf(temperature, 1e-5f);
        for (int i = threadIdx.x; i < vocab; i += blockDim.x) vals[i] = rbf(vals[i] * inv_t);
        __syncthreads();
        // k-th largest value, ties kept (`logits < topk(logits)[..., -1]` is removed).
        float thr = -INFINITY, above = INFINITY;
        int count = 0;
        while (count < top_k) {
            float m = -INFINITY;
            for (int i = threadIdx.x; i < vocab; i += blockDim.x) if (vals[i] < above) m = fmaxf(m, vals[i]);
            m = block_max(m, scratch);
            if (!(m > -INFINITY)) break;
            float cnt = 0.f;
            for (int i = threadIdx.x; i < vocab; i += blockDim.x) if (vals[i] == m) cnt += 1.f;
            count += (int)block_sum(cnt, scratch);
            above = m;
            thr = m;
        }
        float top = rbf(mx * inv_t);
        float total = 0.f;
        for (int i = threadIdx.x; i < vocab; i += blockDim.x) {
            float p = vals[i] >= thr ? __expf(vals[i] - top) : 0.f;
            vals[i] = p; total += p;
        }
        total = block_sum(total, scratch);
        if (threadIdx.x == 0) {
            // rng[0] = seed, rng[1] = frame counter (bumped on the device, so graphs can replay).
            unsigned long long ctr = (rng[1] << 4) | (unsigned long long)slot;
            double u = (double)(mix64(rng[0] ^ mix64(ctr)) >> 11) * (1.0 / 9007199254740992.0) * total;
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
        *out = best_idx;
        if (stop && best_idx >= stop_from) *stop = 1;
    }
}
