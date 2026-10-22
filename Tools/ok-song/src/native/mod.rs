//! Native (Rust + CUDA, no Python) inference for the song stages. The CUDA layer is ok-tts's `gpu.rs`; ok-song adds
//! its kernels after ok-tts's.

#[path = "../../../ok-tts/src/gpu.rs"]
#[allow(dead_code)]
pub mod gpu;
pub mod bigvgan;
pub mod campplus;
pub mod codec;
pub mod demucs;
pub mod dit;
pub mod dsp;
pub mod rmvpe;
pub mod seedvc;
pub mod mula;
pub mod song;
pub mod weights;
pub mod whisper;

use anyhow::Result;

const SONG_KERNELS: &str = include_str!("kernels.cu");
const CODEC_KERNELS: &str = include_str!("codec_kernels.cu");
const DEMUCS_KERNELS: &str = include_str!("demucs_kernels.cu");
const SONG_KERNEL_NAMES: &[&str] = &[
    "rope_il_bf16", "embed_one_bf16", "audio_sum_bf16", "sample_cfg_bf16",
    "rvq_rows_f32", "repeat_rows_f32", "pad_rows_f32", "scale_f32", "silu_f32", "modulate_f32", "gated_add_f32", "rope_il_f32",
    "softmax_rows_f32", "layernorm_rows_f32", "est_input_f32", "incontext_mix_f32", "cfg_euler_f32", "latent_channel_f32", "pad_lr_f32",
    "prelu_f32", "repeat_time_f32", "add_f32", "copy_cols_f32",
    "gather_shift_f32", "col2im_f_f32", "groupnorm1_cbt_f32", "groupnorm1_rows_f32", "glu_f32", "add_freq_emb_f32", "cft_to_tokens_f32",
    "tokens_to_cft_f32", "layernorm_affine_rows_f32", "aa_up2_f32", "aa_down2_f32", "clamp_scale_f32", "mish_f32", "ada_rmsnorm_f32", "seedvc_euler_f32", "affine_act_f32", "cam_context_f32", "sigmoid_mul_f32", "avgpool2_f32", "col2im2d_s2_f32", "gru_dir_f32",
];

pub fn device(index: usize) -> Result<gpu::Gpu> {
    let source = format!("{}\n{}\n{}\n{}", gpu::KERNELS, SONG_KERNELS, CODEC_KERNELS, DEMUCS_KERNELS);
    let names: Vec<&'static str> = gpu::KERNEL_NAMES.iter().chain(SONG_KERNEL_NAMES).copied().collect();
    let g = gpu::Gpu::with_kernels(index, &source, &names)?;
    g.bind()?;
    Ok(g)
}
