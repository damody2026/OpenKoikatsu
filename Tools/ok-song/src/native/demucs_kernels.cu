// Demucs (HTDemucs) kernels, f32. Feature maps are [C][F][T] (frequency branch) or [C][L] (time branch, F = 1).

// out[c][fo][to] = in[c][fo * sf + of][to * st + ot], zero outside (a strided, shifted view used by every conv tap).
extern "C" __global__ void gather_shift_f32(float* out, const float* in, int c, int fo_n, int to_n, int fi_n, int ti_n,
                                            int sf, int of, int st, int ot) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    long long n = (long long)c * fo_n * to_n;
    if (i >= n) return;
    int to = i % to_n;
    long long r = i / to_n;
    int fo = r % fo_n;
    int ch = r / fo_n;
    int f = fo * sf + of, t = to * st + ot;
    out[i] = (f < 0 || f >= fi_n || t < 0 || t >= ti_n) ? 0.f : in[((long long)ch * fi_n + f) * ti_n + t];
}

// Transposed conv along F: y rows are (k * co + c) of [fi][t]; out[c][f][t] = b[c] + sum_k y[k][c][(f + lo - k) / s][t].
extern "C" __global__ void col2im_f_f32(float* out, const float* y, const float* b, int co, int k, int fi_n, int t_n, int s,
                                        int lo, int fo_n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    long long n = (long long)co * fo_n * t_n;
    if (i >= n) return;
    int t = i % t_n;
    long long r = i / t_n;
    int f = r % fo_n + lo;
    int ch = r / fo_n;
    float acc = b[ch];
    for (int kk = 0; kk < k; ++kk) {
        int d = f - kk;
        if (d < 0 || d % s) continue;
        int fi = d / s;
        if (fi >= fi_n) continue;
        acc += y[(((long long)kk * co + ch) * fi_n + fi) * t_n + t];
    }
    out[i] = acc;
}

// GroupNorm with one group over (C, T) for each b of a [C][B][T] map (in place), affine per channel.
extern "C" __global__ void groupnorm1_cbt_f32(float* x, const float* w, const float* bias, int c, int b_n, int t_n, float eps) {
    __shared__ float scratch[32];
    int b = blockIdx.x;
    long long n = (long long)c * t_n;
    float s = 0.f;
    for (long long i = threadIdx.x; i < n; i += blockDim.x) s += x[((i / t_n) * b_n + b) * t_n + i % t_n];
    float mean = block_sum(s, scratch) / n;
    float v = 0.f;
    for (long long i = threadIdx.x; i < n; i += blockDim.x) { float d = x[((i / t_n) * b_n + b) * t_n + i % t_n] - mean; v += d * d; }
    float r = rsqrtf(block_sum(v, scratch) / n + eps);
    for (long long i = threadIdx.x; i < n; i += blockDim.x) {
        int ch = i / t_n;
        long long at = ((long long)ch * b_n + b) * t_n + i % t_n;
        x[at] = (x[at] - mean) * r * w[ch] + bias[ch];
    }
}

// GroupNorm with one group over a whole [L][C] token map (in place), affine per channel (MyGroupNorm norm_out).
extern "C" __global__ void groupnorm1_rows_f32(float* x, const float* w, const float* bias, long long n, int c, float eps) {
    __shared__ float scratch[32];
    float s = 0.f;
    for (long long i = threadIdx.x; i < n; i += blockDim.x) s += x[i];
    float mean = block_sum(s, scratch) / n;
    float v = 0.f;
    for (long long i = threadIdx.x; i < n; i += blockDim.x) { float d = x[i] - mean; v += d * d; }
    float r = rsqrtf(block_sum(v, scratch) / n + eps);
    for (long long i = threadIdx.x; i < n; i += blockDim.x) x[i] = (x[i] - mean) * r * w[i % c] + bias[i % c];
}

// GLU over channels: out[c][j] = in[c][j] * sigmoid(in[c + C][j]).
extern "C" __global__ void glu_f32(float* out, const float* in, int c, long long n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * n) return;
    float g = in[(long long)c * n + i];
    out[i] = in[i] / (1.f + expf(-g));
}

// x[c][f][t] += scale * emb[f][c]
extern "C" __global__ void add_freq_emb_f32(float* x, const float* emb, int c, int f_n, int t_n, float scale) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * f_n * t_n) return;
    long long r = i / t_n;
    int f = r % f_n, ch = r / f_n;
    x[i] += scale * emb[(long long)f * c + ch];
}

// [C][F][T] <-> tokens [(t * F + f)][C]
extern "C" __global__ void cft_to_tokens_f32(float* out, const float* in, int c, int f_n, int t_n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * f_n * t_n) return;
    int ch = i % c;
    long long tok = i / c;
    int f = tok % f_n, t = tok / f_n;
    out[i] = in[((long long)ch * f_n + f) * t_n + t];
}

extern "C" __global__ void tokens_to_cft_f32(float* out, const float* in, int c, int f_n, int t_n) {
    long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long long)c * f_n * t_n) return;
    int t = i % t_n;
    long long r = i / t_n;
    int f = r % f_n, ch = r / f_n;
    out[i] = in[((long long)t * f_n + f) * c + ch];
}

// LayerNorm with affine over each row of [rows, d].
extern "C" __global__ void layernorm_affine_rows_f32(float* out, const float* in, const float* w, const float* b, int d, float eps) {
    __shared__ float scratch[32];
    const float* x = in + (long long)blockIdx.x * d;
    float s = 0.f;
    for (int i = threadIdx.x; i < d; i += blockDim.x) s += x[i];
    float mean = block_sum(s, scratch) / d;
    float v = 0.f;
    for (int i = threadIdx.x; i < d; i += blockDim.x) { float e = x[i] - mean; v += e * e; }
    float r = rsqrtf(block_sum(v, scratch) / d + eps);
    for (int i = threadIdx.x; i < d; i += blockDim.x) out[(long long)blockIdx.x * d + i] = (x[i] - mean) * r * w[i] + b[i];
}
