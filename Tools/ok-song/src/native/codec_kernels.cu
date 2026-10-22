// HeartCodec kernels (f32), appended after ok-tts's kernels and ok-song's kernels.cu.

// out[t, d] = sum over q of tables[q][codes[q * t_len + t]][d]   (RVQ codes [Q, T] -> summed codebook vectors [T, D])
extern "C" __global__ void rvq_rows_f32(float* out, const float* tables, const int* codes, int nq, int entries, int dim, int t_len) {
    int t = blockIdx.x;
    for (int d = threadIdx.x; d < dim; d += blockDim.x) {
        float s = 0.f;
        for (int q = 0; q < nq; ++q) s += tables[((long long)q * entries + codes[q * t_len + t]) * dim + d];
        out[(long long)t * dim + d] = s;
    }
}

// Nearest-neighbour upsampling along rows: out[r * rep + k, :] = in[r, :].
extern "C" __global__ void repeat_rows_f32(float* out, const float* in, int rows, int cols, int rep) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)rows * rep * cols) return;
    long long r = i / cols / rep;
    out[i] = in[r * cols + i % cols];
}

// [b][t][c] -> [b][t + 2 pad][c] with zero rows around each batch item.
extern "C" __global__ void pad_rows_f32(float* out, const float* in, int b, int t, int c, int pad) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    int tp = t + 2 * pad;
    if (i >= (long long)b * tp * c) return;
    int ch = i % c;
    long long r = i / c;
    int bi = r / tp, x = r % tp - pad;
    out[i] = (x < 0 || x >= t) ? 0.f : in[((long long)bi * t + x) * c + ch];
}

extern "C" __global__ void scale_f32(float* x, long long n, float s) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) x[i] *= s;
}

extern "C" __global__ void silu_f32(float* x, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { float g = x[i]; x[i] = g / (1.f + expf(-g)); }
}

// AdaLN-single modulation on rows [b * t, d]: out = in * (1 + scale) + shift, where
// shift = table[ts * d + c] + mod[b * mstride + ms * d + c] and likewise scale with (tc, mc).
extern "C" __global__ void modulate_f32(float* out, const float* in, const float* table, const float* mod, int mstride,
                                        int ts, int ms, int tc, int mc, int d, int t, long long rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * d) return;
    int c = i % d;
    long long b = i / d / t;
    float shift = table[ts * d + c] + mod[b * mstride + ms * d + c];
    float scale = table[tc * d + c] + mod[b * mstride + mc * d + c];
    out[i] = in[i] * (1.f + scale) + shift;
}

// x += gate * h, gate = table[idx * d + c] + mod[b * mstride + idx * d + c].
extern "C" __global__ void gated_add_f32(float* x, const float* h, const float* table, const float* mod, int mstride, int idx,
                                         int d, int t, long long rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * d) return;
    int c = i % d;
    long long b = i / d / t;
    x[i] = x[i] + (table[idx * d + c] + mod[b * mstride + idx * d + c]) * h[i];
}

// Interleaved RoPE on rows (position = row % t); grid (rows, heads), blockDim = d / 2.
extern "C" __global__ void rope_il_f32(float* x, int row_stride, int head_offset, int d, const float* theta, int t) {
    int row = blockIdx.x, head = blockIdx.y, j = threadIdx.x;
    if (j >= d / 2) return;
    float* p = x + (long long)row * row_stride + head_offset + head * d;
    float a = p[2 * j], b = p[2 * j + 1];
    float ang = (float)(row % t) * theta[j];
    float c = cosf(ang), s = sinf(ang);
    p[2 * j] = a * c - b * s;
    p[2 * j + 1] = a * s + b * c;
}

// Row softmax of scale * s, in place; one block per row.
extern "C" __global__ void softmax_rows_f32(float* s, int n, float scale) {
    __shared__ float scratch[32];
    float* r = s + (long long)blockIdx.x * n;
    float mx = -INFINITY;
    for (int i = threadIdx.x; i < n; i += blockDim.x) mx = fmaxf(mx, r[i] * scale);
    mx = block_max(mx, scratch);
    float sum = 0.f;
    for (int i = threadIdx.x; i < n; i += blockDim.x) { float e = expf(r[i] * scale - mx); r[i] = e; sum += e; }
    sum = block_sum(sum, scratch);
    float inv = 1.f / sum;
    for (int i = threadIdx.x; i < n; i += blockDim.x) r[i] *= inv;
}

// LayerNorm without affine over each row of [rows, d].
extern "C" __global__ void layernorm_rows_f32(float* out, const float* in, int d, float eps) {
    __shared__ float scratch[32];
    const float* x = in + (long long)blockIdx.x * d;
    float s = 0.f;
    for (int i = threadIdx.x; i < d; i += blockDim.x) s += x[i];
    float mean = block_sum(s, scratch) / d;
    float v = 0.f;
    for (int i = threadIdx.x; i < d; i += blockDim.x) { float e = x[i] - mean; v += e * e; }
    float r = rsqrtf(block_sum(v, scratch) / d + eps);
    for (int i = threadIdx.x; i < d; i += blockDim.x) out[(long long)blockIdx.x * d + i] = (x[i] - mean) * r;
}

// Estimator input [batch][t][1024]: [x | incontext | 0] for the unconditional row (batch 2, row 0), else [x | incontext | mu].
extern "C" __global__ void est_input_f32(float* out, const float* x, const float* ic, const float* mu, int t, int batch) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)batch * t * 1024) return;
    int c = i % 1024;
    long long r = i / 1024;
    int b = r / t, row = r % t;
    float v;
    if (c < 256) v = x[(long long)row * 256 + c];
    else if (c < 512) v = ic[(long long)row * 256 + c - 256];
    else v = (batch == 2 && b == 0) ? 0.f : mu[(long long)row * 512 + c - 512];
    out[i] = v;
}

// First rows: x = (1 - (1 - 1e-6) t) * noise + t * incontext.
extern "C" __global__ void incontext_mix_f32(float* x, const float* noise, const float* ic, long long n, float t) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    x[i] = (1.f - (1.f - 1e-6f) * t) * noise[i] + t * ic[i];
}

// Euler step with CFG: x += dt * (u + g * (c - u)); d holds [u | c] (or just the prediction when batch == 1).
extern "C" __global__ void cfg_euler_f32(float* x, const float* d, long long n, float dt, float g, int batch) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float v = d[i];
    if (batch == 2) { float u = d[i], c = d[n + i]; v = u + g * (c - u); }
    x[i] = x[i] + dt * v;
}

// One stereo half of a latent [t, 256] as [128, t], quantized like ScalarModel.decode: round(9 x) / 9.
extern "C" __global__ void latent_channel_f32(float* out, const float* lat, int t, int ch) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)128 * t) return;
    int k = i / t, x = i % t;
    out[i] = rintf(9.f * lat[(long long)x * 256 + ch * 128 + k]) / 9.f;
}

// [c, t] -> [c, t + left + right] with zeros.
extern "C" __global__ void pad_lr_f32(float* out, const float* in, int c, int t, int left, int right) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    int tp = t + left + right;
    if (i >= (long long)c * tp) return;
    int ch = i / tp, x = i % tp - left;
    out[i] = (x < 0 || x >= t) ? 0.f : in[(long long)ch * t + x];
}

extern "C" __global__ void prelu_f32(float* x, long long n, const float* a) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { float v = x[i]; x[i] = v >= 0.f ? v : a[0] * v; }
}

// [c, t] -> [c, t * rep], each sample repeated.
extern "C" __global__ void repeat_time_f32(float* out, const float* in, int c, int t, int rep) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * t * rep) return;
    long long ch = i / ((long long)t * rep);
    int x = (i % ((long long)t * rep)) / rep;
    out[i] = in[ch * t + x];
}

extern "C" __global__ void add_f32(float* out, const float* a, const float* b, long long n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = a[i] + b[i];
}

// out[r, off + c] = in[r, c] for c < cols, with out rows of stride `ostride`.
extern "C" __global__ void copy_cols_f32(float* out, int ostride, int off, const float* in, int cols, long long rows) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * cols) return;
    long long r = i / cols;
    out[r * ostride + off + i % cols] = in[i];
}
