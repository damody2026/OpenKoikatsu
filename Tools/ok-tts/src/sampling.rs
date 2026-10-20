//! Sampling settings. Drawing happens on the GPU (kernels.cu: sample_bf16), in the order of
//! faster_qwen3_tts.sampling.sample_logits: repetition penalty -> suppress -> temperature -> top-k -> draw.

#[derive(Clone, Copy, Debug)]
pub struct Sampler {
    pub temperature: f32,
    pub top_k: usize,
    pub greedy: bool,
}

/// First index of the maximum, like torch.argmax (parity checks).
pub fn argmax(v: &[f32]) -> usize {
    let mut best = 0;
    for (i, &x) in v.iter().enumerate() {
        if x > v[best] { best = i; }
    }
    best
}
