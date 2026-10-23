//! ok-tts: Qwen3-TTS 12Hz for OpenKoikatsu, in Rust on CUDA (cudarc + cuBLAS + NVRTC).
//!
//!   ok-tts serve  [--port 47860] [--model DIR] [--voices voices.json] [--parent-pid N]   (exits when process N exits)
//!                 [--backend base|design] [--base DIR] [--profiles DIR]
//!       base (the default when <exe>/profiles has profiles and the Base model is found): every voice is the fixed
//!       speaker embedding of its profile, so a character keeps one voice; design: VoiceDesign from the description.
//!   ok-tts profile [--base DIR] [--profiles DIR] [--keys 08,m0] [--ages 18] [--ref-text TEXT] [--force]   (see profile.rs)
//!   ok-tts card-voice --card CARD.png [--set VOICE.okvoice | --remove]   (a character's own voice; see cardvoice.rs)
//!   ok-tts voice-export --personality 08 [--age 18] [--card CARD.png] [--profiles DIR] --out DIR
//!       read only: the voice she actually speaks with (her card's okVo first, else her design's profile, the same
//!       order as synthesis), written as reference.wav (24 kHz mono) + voice.json; prints voice.json
//!   ok-tts say    --text TEXT [--personality 00] [--age 18] [--out line.ogg|line.wav] [--greedy]
//!   ok-tts bench  --endpoint http://127.0.0.1:47861 --corpus lines.txt --out-dir ABS [--rounds 3] [--mode stream|file] [--fresh] [--json out.json]
//!   ok-tts serve  ... [--stream-later-frames 4]   (frames per streamed piece after the first)
//!   ok-tts consistency --base BASE_DIR --corpus lines.txt --out DIR [--personality 08] [--other 00,03,m0]   (VoiceDesign vs Base clone)
//!   ok-tts speaker-sim --base BASE_DIR a.wav b.wav[#1|#2] ...   (speaker-embedding cosine of each pair)
//!   ok-tts parity --reference DIR      (compare against Tools/ok-tts/reference/dump_reference.py output; run with OK_TTS_WEIGHTS=bf16)
//!   ok-tts gemv-bench                  (decode-step linear layer timings, bf16 vs int8 weights, in CUDA graphs)
//!
//! CUDA DLLs (cublas64_12, cublasLt64_12, nvrtc64_120_0, nvrtc-builtins) are searched next to the exe,
//! in <exe>/cuda, in --cuda-dir, then on PATH. Only the NVIDIA driver must be installed.

mod log;
mod parent;
mod profile;
mod audio;
mod bench;
mod cardvoice;
mod codec;
mod consistency;
mod engine;
mod gpu;
mod sampling;
mod server;
mod speaker;
mod talker;
mod text;
mod voicemgr;
mod weights;

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};

use crate::engine::{default_base_model_dir, default_model_dir, Engine, SAMPLE_RATE};
use crate::talker::GenConfig;

struct Args {
    cmd: String,
    opts: HashMap<String, String>,
    flags: Vec<String>,
    positional: Vec<String>,
}

fn parse_args() -> Args {
    let mut it = std::env::args().skip(1);
    let cmd = it.next().unwrap_or_else(|| "help".into());
    let mut opts = HashMap::new();
    let mut flags = Vec::new();
    let mut positional = Vec::new();
    let rest: Vec<String> = it.collect();
    let mut i = 0;
    while i < rest.len() {
        let a = &rest[i];
        if let Some(name) = a.strip_prefix("--") {
            if i + 1 < rest.len() && !rest[i + 1].starts_with("--") {
                opts.insert(name.to_string(), rest[i + 1].clone());
                i += 2;
                continue;
            }
            flags.push(name.to_string());
        } else {
            positional.push(a.clone());
        }
        i += 1;
    }
    Args { cmd, opts, flags, positional }
}

fn exe_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_else(|| PathBuf::from("."))
}

/// Make the CUDA runtime DLLs findable before cudarc loads them.
fn add_dll_dirs(extra: Option<&String>) {
    let mut dirs: Vec<PathBuf> = vec![exe_dir(), exe_dir().join("cuda")];
    if let Some(e) = extra {
        dirs.insert(0, PathBuf::from(e));
    }
    let mut path = std::env::var_os("PATH").unwrap_or_default();
    for d in dirs.iter().rev() {
        if d.is_dir() {
            let mut p = d.as_os_str().to_owned();
            p.push(";");
            p.push(&path);
            path = p;
        }
    }
    std::env::set_var("PATH", path);
}

fn resolve(args: &Args) -> Result<(PathBuf, PathBuf)> {
    let model = match args.opts.get("model") {
        Some(m) => PathBuf::from(m),
        None => default_model_dir().context("model not found; pass --model <snapshot dir> or set OK_TTS_MODEL")?,
    };
    let voices = match args.opts.get("voices") {
        Some(v) => PathBuf::from(v),
        None => [exe_dir().join("voices.json"), exe_dir().join("../../voices.json"), PathBuf::from("voices.json")]
            .into_iter()
            .find(|p| p.is_file())
            .context("voices.json not found; pass --voices")?,
    };
    Ok((model, voices))
}

fn main() {
    let args = parse_args();
    if args.cmd == "serve" {
        // The server writes its own log (logs/ok-tts.log beside the exe), whoever starts it.
        log::init("ok-tts");
    }
    if args.cmd == "serve" {
        if let Some(pid) = args.opts.get("parent-pid").and_then(|p| p.parse().ok()) {
            parent::watch(pid);
        }
    }
    add_dll_dirs(args.opts.get("cuda-dir"));
    let result = match args.cmd.as_str() {
        "serve" => cmd_serve(&args),
        "say" => cmd_say(&args),
        "parity" => cmd_parity(&args),
        "bench" => cmd_bench(&args),
        "consistency" => cmd_consistency(&args),
        "profile" => cmd_profile(&args),
        "card-voice" => cmd_card_voice(&args),
        "voice-export" => cmd_voice_export(&args),
        "gemv-bench" => gpu::gemv_bench(),
        "speaker-sim" => args.opts.get("base").context("--base is required")
            .and_then(|b| consistency::similarity(Path::new(b), &args.positional)),
        _ => {
            // An unknown command fails: a caller asking for one this build lacks must not read empty output as success.
            Err(anyhow::anyhow!("unknown command {:?}; usage: ok-tts serve|say|bench|profile|card-voice|voice-export|consistency|speaker-sim|parity (see source header)", args.cmd))
        }
    };
    if let Err(e) = result {
        crate::log!("ok-tts: error: {e:#}");
        std::process::exit(1);
    }
}

fn max_positions(args: &Args) -> usize {
    args.opts.get("max-positions").and_then(|v| v.parse().ok()).unwrap_or(2048)
}

fn cmd_serve(args: &Args) -> Result<()> {
    let (model, voices) = resolve(args)?;
    let port: u16 = args.opts.get("port").map(|p| p.parse()).transpose()?.unwrap_or(47860);
    let positions = max_positions(args);
    let later: usize = args.opts.get("stream-later-frames").map(|v| v.parse()).transpose()?.unwrap_or(4);
    // The server keeps its own copy of the voices, so voice lookups and cached lines never wait for the engine.
    let voice_table = text::Voices::load(&voices)?;
    let profiles_dir = profiles_dir(args);
    let profiles = profile::load_all(&profiles_dir)?;
    let base = args.opts.get("base").map(PathBuf::from).or_else(default_base_model_dir);
    let backend = match args.opts.get("backend").map(String::as_str) {
        Some("design") => "design",
        Some("base") => "base",
        Some(other) => bail!("--backend must be base or design, not {other}"),
        None if !profiles.is_empty() && base.is_some() => "base",
        None => "design",
    };
    if backend == "base" {
        let base = base.context("Base model not found; pass --base <Qwen3-TTS-12Hz-1.7B-Base snapshot dir> or set OK_TTS_BASE_MODEL")?;
        if profiles.is_empty() {
            bail!("no voice profiles in {}; run ok-tts profile", profiles_dir.display());
        }
        let missing: Vec<String> = profile::all_choices(&voice_table, None).iter().map(profile::profile_id).filter(|id| !profiles.contains_key(id)).collect();
        crate::log!("ok-tts: backend base, {} profiles from {}{}", profiles.len(), profiles_dir.display(),
                    if missing.is_empty() { String::new() } else { format!(" (missing {}: {})", missing.len(), missing.join(" ")) });
        let tags = profiles.iter().map(|(id, p)| (id.clone(), p.tag())).collect();
        // The speaker encoder (CPU) turns new candidate voices into embeddings while the server runs.
        let store = weights::Store::open(&base.join("model.safetensors"))?;
        let encoder = speaker::SpeakerEncoder::load(&store).context("speaker encoder")?;
        drop(store);
        let design = Some(model.clone()).filter(|m| m.join("model.safetensors").is_file());
        let voices_path = voices.clone();
        let base_voices = voicemgr::BaseVoices::new(tags, profiles_dir.clone(), base.clone(), design, voices_path, positions, encoder);
        return server::serve(port, 8, voice_table, Some(base_voices), move || {
            let mut engine = Engine::load(&base, &voices, positions)?;
            engine.profiles = Some(profiles);
            engine.stream_later_frames = later.max(1);
            Ok(engine)
        });
    }
    crate::log!("ok-tts: backend design (VoiceDesign)");
    server::serve(port, 8, voice_table, None, move || {
        let mut engine = Engine::load(&model, &voices, positions)?;
        engine.stream_later_frames = later.max(1);
        Ok(engine)
    })
}

fn profiles_dir(args: &Args) -> PathBuf {
    args.opts.get("profiles").map(PathBuf::from).unwrap_or_else(|| exe_dir().join("profiles"))
}

/// The voice a heroine actually speaks with, for other tools (ok-song): never changes the card or the profiles.
fn cmd_voice_export(args: &Args) -> Result<()> {
    let (_, voices_path) = resolve(args)?;
    let voices = text::Voices::load(&voices_path)?;
    let personality = args.opts.get("personality").context("--personality is required")?;
    let age: u32 = args.opts.get("age").map(|v| v.parse()).transpose()?.unwrap_or(18);
    let out = PathBuf::from(args.opts.get("out").context("--out DIR is required")?);
    let choice = voices.choose(personality, age);
    let id = profile::profile_id(&choice);
    let card = args.opts.get("card").map(PathBuf::from);
    let (voice, source, origin) = match card.as_ref().map(|c| cardvoice::read_file(c)).transpose()? {
        Some(Some(v)) => (v, "card", card.as_ref().unwrap().display().to_string()),
        _ => {
            let file = profiles_dir(args).join(format!("{id}.okvoice"));
            if !file.is_file() {
                bail!("VOICE_MISSING: no voice in the card and no profile {}", file.display());
            }
            (profile::Profile::load(&file)?, "profile", file.display().to_string())
        }
    };
    let samples = audio::decode_ogg(&voice.ref_ogg).context("INVALID_AUDIO: the voice's reference clip does not decode")?;
    if samples.len() < SAMPLE_RATE as usize || audio::rms(&samples) < 0.002 {
        bail!("INVALID_AUDIO: the voice's reference clip is shorter than a second or silent");
    }
    std::fs::create_dir_all(&out)?;
    let wav = audio::wav_bytes(&samples, SAMPLE_RATE);
    audio::write_atomic(&out.join("reference.wav"), &wav)?;
    let report = serde_json::json!({
        "schema": 1, "source": source, "origin": origin, "profile_id": id, "personality": choice.key, "age": choice.age,
        "voice_schema": voice.meta.schema, "voice_source": voice.meta.source, "embedding_tag": voice.tag(),
        "reference_ogg_sha1": profile::sha1_hex(&voice.ref_ogg), "reference_wav": out.join("reference.wav"),
        "reference_wav_sha1": profile::sha1_hex(&wav), "sample_rate": SAMPLE_RATE, "channels": 1,
        "seconds": samples.len() as f64 / SAMPLE_RATE as f64, "description": voice.meta.description,
    });
    let text = serde_json::to_string_pretty(&report)?;
    audio::write_atomic(&out.join("voice.json"), text.as_bytes())?;
    println!("{text}");
    Ok(())
}

fn cmd_card_voice(args: &Args) -> Result<()> {
    let card = PathBuf::from(args.opts.get("card").context("--card is required")?);
    if let Some(v) = args.opts.get("set") {
        let voice = profile::Profile::load(Path::new(v))?;
        cardvoice::write_file(&card, Some(&voice))?;
        println!("stored voice {} (tag {}) in {}", voice.meta.id, voice.tag(), card.display());
    } else if args.flags.iter().any(|f| f == "remove") {
        cardvoice::write_file(&card, None)?;
        println!("removed the voice from {}", card.display());
    }
    match cardvoice::read_file(&card)? {
        Some(p) => println!("{}", serde_json::to_string_pretty(&serde_json::json!({"tag": p.tag(), "meta": p.meta, "reference_ogg_bytes": p.ref_ogg.len()}))?),
        None => println!("no voice in {}", card.display()),
    }
    Ok(())
}

fn cmd_profile(args: &Args) -> Result<()> {
    let (design, voices) = resolve(args)?;
    let opt = |k: &str| args.opts.get(k).cloned();
    let list = |k: &str| opt(k).map(|v| v.split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect::<Vec<_>>());
    profile::build(&profile::BuildArgs {
        design,
        base: opt("base").map(PathBuf::from).or_else(default_base_model_dir).context("Base model not found; pass --base")?,
        voices,
        out: profiles_dir(args),
        keys: list("keys"),
        ages: list("ages").map(|a| a.iter().map(|v| v.parse()).collect::<Result<Vec<u32>, _>>()).transpose()?,
        ref_text: opt("ref-text").unwrap_or_else(|| profile::DEFAULT_REF_TEXT.into()),
        force: args.flags.iter().any(|f| f == "force"),
        positions: max_positions(args),
    })
}

fn cmd_consistency(args: &Args) -> Result<()> {
    let (design, voices) = resolve(args)?;
    let opt = |k: &str| args.opts.get(k).cloned();
    consistency::run(&consistency::ConsistencyArgs {
        design,
        base: PathBuf::from(opt("base").context("--base <Qwen3-TTS-12Hz-1.7B-Base snapshot dir> is required")?),
        voices,
        corpus: PathBuf::from(opt("corpus").context("--corpus is required")?),
        out: PathBuf::from(opt("out").context("--out is required")?),
        personality: opt("personality").unwrap_or_else(|| "08".into()),
        other: opt("other").unwrap_or_else(|| "00,03,m0".into()),
        age: opt("age").map(|v| v.parse()).transpose()?.unwrap_or(18),
        ref_text: opt("ref-text").unwrap_or_else(|| "今天的风好大，我们去那边的咖啡店坐一下吧，我请你喝热可可，然后再慢慢走回去。".into()),
        positions: max_positions(args),
    })
}

fn cmd_bench(args: &Args) -> Result<()> {
    let opt = |k: &str| args.opts.get(k).cloned();
    bench::run(&bench::BenchArgs {
        endpoint: opt("endpoint").unwrap_or_else(|| "http://127.0.0.1:47860".into()),
        corpus: PathBuf::from(opt("corpus").context("--corpus is required")?),
        out_dir: PathBuf::from(opt("out-dir").context("--out-dir is required")?),
        rounds: opt("rounds").map(|v| v.parse()).transpose()?.unwrap_or(1),
        personality: opt("personality").unwrap_or_else(|| "08".into()),
        age: opt("age").map(|v| v.parse()).transpose()?.unwrap_or(18),
        stream: opt("mode").as_deref() != Some("file"),
        fresh: args.flags.iter().any(|f| f == "fresh"),
        json: opt("json").map(PathBuf::from),
        later_frames: opt("later-frames").map(|v| v.split(',').map(|n| n.trim().parse()).collect::<Result<Vec<usize>, _>>()).transpose()?.unwrap_or_default(),
    })
}

fn cmd_say(args: &Args) -> Result<()> {
    let (model, voices) = resolve(args)?;
    let engine = Engine::load(&model, &voices, max_positions(args))?;
    let text = text::clean_line(args.opts.get("text").context("--text is required")?);
    let personality = args.opts.get("personality").cloned().unwrap_or_else(|| "00".into());
    let age: u32 = args.opts.get("age").map(|a| a.parse()).transpose()?.unwrap_or(18);
    let mut cfg = GenConfig::default();
    if args.flags.iter().any(|f| f == "greedy") {
        cfg.talker.greedy = true;
        cfg.predictor.greedy = true;
    }
    let runs: usize = args.opts.get("runs").map(|r| r.parse()).transpose()?.unwrap_or(1);
    let mut speech = None;
    for _ in 0..runs {
        let s = engine.speak(&text, &personality, age, cfg.clone())?;
        let audio = s.samples.len() as f64 / SAMPLE_RATE as f64;
        let chars = text.chars().count();
        println!("{} chars, {} frames, audio {:.2}s, gen {:.3}s, {:.3}s/char, rms {:.4}", chars, s.frames, audio, s.gen_seconds, s.gen_seconds / chars as f64, audio::rms(&s.samples));
        speech = Some(s);
    }
    let mut speech = speech.context("--runs must be at least 1")?;
    if let Some(out) = args.opts.get("out") {
        audio::trim_silence(&mut speech.samples, SAMPLE_RATE);
        audio::peak_normalize(&mut speech.samples);
        let bytes = if out.to_lowercase().ends_with(".wav") { audio::wav_bytes(&speech.samples, SAMPLE_RATE) } else { audio::encode_ogg(&speech.samples, SAMPLE_RATE)? };
        audio::write_atomic(Path::new(out), &bytes)?;
        println!("wrote {out}");
    }
    Ok(())
}

// ------------------------------------------------------------------ parity against the Python reference

fn read_ref_f32(dir: &Path, name: &str) -> Result<Vec<f32>> {
    let b = std::fs::read(dir.join(format!("{name}.bin"))).with_context(|| format!("reference {name}"))?;
    Ok(b.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect())
}

fn read_ref_i64(dir: &Path, name: &str) -> Result<Vec<i64>> {
    let b = std::fs::read(dir.join(format!("{name}.bin"))).with_context(|| format!("reference {name}"))?;
    Ok(b.chunks_exact(8).map(|c| i64::from_le_bytes(c.try_into().unwrap())).collect())
}

fn compare(name: &str, ours: &[f32], reference: &[f32]) -> f64 {
    if ours.len() != reference.len() {
        println!("{name:28} LENGTH MISMATCH ours={} ref={}", ours.len(), reference.len());
        return f64::INFINITY;
    }
    let (mut max_abs, mut err, mut sig) = (0f64, 0f64, 0f64);
    for (a, b) in ours.iter().zip(reference) {
        let d = (*a as f64 - *b as f64).abs();
        max_abs = max_abs.max(d);
        err += d * d;
        sig += (*b as f64) * (*b as f64);
    }
    let rel = (err / sig.max(1e-30)).sqrt();
    println!("{name:28} n={:8} max_abs={max_abs:.3e} rel_rms={rel:.3e}", ours.len());
    rel
}

fn cmd_parity(args: &Args) -> Result<()> {
    let dir = PathBuf::from(args.opts.get("reference").context("--reference DIR is required")?);
    let index: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("index.json"))?)?;
    let (model, voices) = resolve(args)?;
    let engine = Engine::load(&model, &voices, 1024)?;
    let gpu = &engine.gpu;
    let mut failures = Vec::new();

    // 1. tokenizer
    for case in index["tokenizer_cases"].as_array().context("tokenizer_cases")? {
        let text = case["text"].as_str().unwrap();
        let want: Vec<u32> = case["ids"].as_array().unwrap().iter().map(|v| v.as_u64().unwrap() as u32).collect();
        let got = engine.tokenizer.encode_with_markers(text)?;
        let ok = got == want;
        println!("tokenizer {:?} -> {}", text.chars().take(24).collect::<String>(), if ok { "OK" } else { "MISMATCH" });
        if !ok {
            println!("  want {want:?}\n  got  {got:?}");
            failures.push("tokenizer");
        }
    }
    let ids = |k: &str| -> Vec<u32> { index[k].as_array().unwrap().iter().map(|v| v.as_u64().unwrap() as u32).collect() };
    let (assistant, instruct) = (ids("assistant_ids"), ids("instruct_ids"));

    // 2. prompt embeddings and prefill
    let prompt = engine.talker.build_prompt(gpu, &instruct, &assistant, text::LANGUAGE_CHINESE, None)?;
    let to_f32 = |buf: u64, n: usize| -> Result<Vec<f32>> { Ok(gpu.read::<u16>(buf, n)?.into_iter().map(talker::bf16_to_f32).collect()) };
    let h = talker::HIDDEN;
    let rel = compare("talker_input_embeds", &to_f32(prompt.prefill.ptr, prompt.prefill_len * h)?, &read_ref_f32(&dir, "talker_input_embeds")?);
    if rel > 1e-2 { failures.push("talker_input_embeds"); }
    let rel = compare("trailing_text_hiddens", &to_f32(prompt.trailing.ptr, prompt.trailing_len * h)?, &read_ref_f32(&dir, "trailing_text_hiddens")?);
    if rel > 1e-2 { failures.push("trailing_text_hiddens"); }
    let (logits, hidden) = engine.talker.prefill_logits(gpu, &prompt)?;
    let rel = compare("prefill_hidden_last", &to_f32(hidden.at((prompt.prefill_len - 1) * h * 2), h)?, &read_ref_f32(&dir, "prefill_hidden_last")?);
    if rel > 5e-2 { failures.push("prefill_hidden_last"); }
    let ref_logits = read_ref_f32(&dir, "prefill_logits_last")?;
    let rel = compare("prefill_logits_last", &logits, &ref_logits);
    if rel > 5e-2 { failures.push("prefill_logits_last"); }
    println!("prefill argmax ours={} ref={}", sampling::argmax(&logits), sampling::argmax(&ref_logits));

    if dir.join("pred_steps.json").is_file() {
        let steps: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("pred_steps.json"))?)?;
        let toks: Vec<u32> = steps["tokens"].as_array().unwrap().iter().map(|v| v.as_u64().unwrap() as u32).collect();
        let first = steps["first"].as_u64().unwrap() as u32;
        let ref_hidden: Vec<u16> = read_ref_f32(&dir, "prefill_hidden_last")?.iter().map(|v| (v.to_bits() >> 16) as u16).collect();
        let ref_hidden = gpu.upload(&ref_hidden)?;
        let all = engine.talker.predict_forced(gpu, ref_hidden.ptr, first, &toks)?;
        let mut agree = 0;
        for (i, l) in all.iter().enumerate() {
            let r = read_ref_f32(&dir, &format!("pred_step{i}"))?;
            let rel = compare(&format!("pred_step{i} argmax {}/{}", sampling::argmax(l), sampling::argmax(&r)), l, &r);
            if sampling::argmax(l) == sampling::argmax(&r) { agree += 1; }
            if rel > 2e-2 { failures.push("predictor logits"); }
        }
        println!("predictor teacher-forced argmax agreement {agree}/15");
        if agree < 13 { failures.push("predictor argmax"); }
    }
    // 3. greedy generation
    let mut cfg = GenConfig::default();
    cfg.talker.greedy = true;
    cfg.predictor.greedy = true;
    cfg.max_new_tokens = 400;
    let t = std::time::Instant::now();
    let frames = engine.talker.generate(gpu, &prompt, &cfg)?;
    let gen_s = t.elapsed().as_secs_f64();
    let ref_codes = read_ref_i64(&dir, "codes_greedy")?;
    let ref_frames: Vec<[u32; 16]> = ref_codes.chunks_exact(16).map(|c| std::array::from_fn(|i| c[i] as u32)).collect();
    let same_prefix = frames.iter().zip(&ref_frames).take_while(|(a, b)| a == b).count();
    let first_cb_same = frames.iter().zip(&ref_frames).filter(|(a, b)| a[0] == b[0]).count();
    println!("greedy frames ours={} ref={} identical_prefix={} first_codebook_equal={} ({:.2}s)", frames.len(), ref_frames.len(), same_prefix, first_cb_same, gen_s);
    // Greedy sequences split at bf16 near-ties, so only check the utterance ends sensibly.
    if frames.is_empty() || frames.len() * 10 < ref_frames.len() * 7 || frames.len() * 10 > ref_frames.len() * 13 { failures.push("greedy length"); }

    // 4. codec on the reference codes
    let mut taps: Vec<(String, Vec<f32>)> = Vec::new();
    let wav = engine.codec.forward_debug(gpu, &ref_frames, |name, buf, c, t| {
        let mut v = gpu.read::<f32>(buf.ptr, c * t)?;
        if name == "codec_pre_transformer" {
            // The Python hook sees [T, C] (before its permute); ours is [C, T].
            v = (0..t).flat_map(|ti| (0..c).map(move |ci| (ti, ci))).map(|(ti, ci)| v[ci * t + ti]).collect();
        }
        taps.push((name.to_string(), v));
        Ok(())
    })?;
    for (name, ours) in &taps {
        if dir.join(format!("{name}.bin")).is_file() {
            let rel = compare(name, ours, &read_ref_f32(&dir, name)?);
            if rel > 0.1 { failures.push("codec stage"); }
        }
    }
    let rel = compare("codec_wav", &wav, &read_ref_f32(&dir, "codec_wav")?);
    if rel > 0.25 { failures.push("codec_wav"); }
    let speech = engine.codec.decode(gpu, &frames)?;
    std::fs::write(dir.join("rust_greedy.wav"), audio::wav_bytes(&speech, SAMPLE_RATE))?;
    std::fs::write(dir.join("rust_codec_ref_codes.wav"), audio::wav_bytes(&wav, SAMPLE_RATE))?;
    println!("wrote rust_greedy.wav and rust_codec_ref_codes.wav to {}", dir.display());
    if failures.is_empty() {
        println!("PARITY OK");
        Ok(())
    } else {
        bail!("parity failures: {failures:?}")
    }
}
