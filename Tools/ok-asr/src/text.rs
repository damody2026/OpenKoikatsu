//! Text side: Qwen2 byte-level BPE (same set-up as Tools/ok-tts/src/text.rs), the Qwen3-ASR chat
//! template, and decoding of generated ids ("language X<asr_text>text").

use std::collections::HashMap;
use std::path::Path;

use anyhow::{anyhow, Context, Result};
use tokenizers::models::bpe::BPE;
use tokenizers::normalizers::unicode::NFC;
use tokenizers::pre_tokenizers::byte_level::ByteLevel;
use tokenizers::pre_tokenizers::sequence::Sequence;
use tokenizers::pre_tokenizers::split::{Split, SplitPattern};
use tokenizers::{PreTokenizerWrapper, SplitDelimiterBehavior, Tokenizer};

pub const IM_START: u32 = 151_644;
pub const IM_END: u32 = 151_645;
pub const AUDIO_START: u32 = 151_669;
pub const AUDIO_END: u32 = 151_670;
pub const AUDIO_PAD: u32 = 151_676;
pub const ASR_TEXT: u32 = 151_704;

const QWEN2_SPLIT: &str = r"(?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\r\n\p{L}\p{N}]?\p{L}+|\p{N}| ?[^\s\p{L}\p{N}]+[\r\n]*|\s*[\r\n]+|\s+(?!\S)|\s+";

pub struct TextTokenizer {
    tk: Tokenizer,
    /// id -> raw bytes for BPE tokens; added tokens map to Some(text) unless special (None).
    pieces: HashMap<u32, Option<Vec<u8>>>,
}

/// GPT-2 bytes_to_unicode, inverted: the printable stand-in char -> original byte.
fn unicode_to_byte() -> HashMap<char, u8> {
    let mut bs: Vec<u32> = (b'!' as u32..=b'~' as u32).chain(0xA1..=0xAC).chain(0xAE..=0xFF).collect();
    let mut cs = bs.clone();
    let mut n = 0;
    for b in 0..256u32 {
        if !bs.contains(&b) {
            bs.push(b);
            cs.push(256 + n);
            n += 1;
        }
    }
    bs.into_iter().zip(cs).map(|(b, c)| (char::from_u32(c).unwrap(), b as u8)).collect()
}

impl TextTokenizer {
    pub fn load(model_dir: &Path) -> Result<Self> {
        let vocab = model_dir.join("vocab.json");
        let merges = model_dir.join("merges.txt");
        let bpe = BPE::from_file(&vocab.to_string_lossy(), &merges.to_string_lossy())
            .build()
            .map_err(|e| anyhow!("BPE: {e}"))?;
        let mut tk = Tokenizer::new(bpe);
        tk.with_normalizer(Some(NFC));
        let split = Split::new(SplitPattern::Regex(QWEN2_SPLIT.into()), SplitDelimiterBehavior::Isolated, false)
            .map_err(|e| anyhow!("split regex: {e}"))?;
        let pre: Vec<PreTokenizerWrapper> = vec![split.into(), ByteLevel::new(false, false, false).into()];
        tk.with_pre_tokenizer(Some(Sequence::new(pre)));

        let back = unicode_to_byte();
        let words: HashMap<String, u32> = serde_json::from_str(&std::fs::read_to_string(&vocab).context("vocab.json")?)?;
        let mut pieces: HashMap<u32, Option<Vec<u8>>> = words
            .into_iter()
            .map(|(w, id)| (id, Some(w.chars().map(|c| back.get(&c).copied().unwrap_or(b'?')).collect())))
            .collect();
        let config: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(model_dir.join("tokenizer_config.json")).context("tokenizer_config.json")?)?;
        if let Some(added) = config["added_tokens_decoder"].as_object() {
            for (id, t) in added {
                let id: u32 = id.parse()?;
                let special = t["special"].as_bool().unwrap_or(false);
                let text = t["content"].as_str().unwrap_or_default().as_bytes().to_vec();
                pieces.insert(id, if special { None } else { Some(text) });
            }
        }
        Ok(Self { tk, pieces })
    }

    fn bpe(&self, text: &str) -> Result<Vec<u32>> {
        if text.is_empty() {
            return Ok(Vec::new());
        }
        Ok(self.tk.encode(text, false).map_err(|e| anyhow!("tokenize: {e}"))?.get_ids().to_vec())
    }

    /// Chat template with one audio and an empty or given system context; `language` forces text-only output.
    /// Returns the ids and the index of the first <|audio_pad|>.
    pub fn prompt(&self, context: &str, audio_tokens: usize, language: Option<&str>) -> Result<(Vec<u32>, usize)> {
        let mut ids = vec![IM_START];
        ids.extend(self.bpe(&format!("system\n{context}"))?);
        ids.push(IM_END);
        ids.extend(self.bpe("\n")?);
        ids.push(IM_START);
        ids.extend(self.bpe("user\n")?);
        ids.push(AUDIO_START);
        let audio_at = ids.len();
        ids.extend(std::iter::repeat(AUDIO_PAD).take(audio_tokens));
        ids.push(AUDIO_END);
        ids.push(IM_END);
        ids.extend(self.bpe("\n")?);
        ids.push(IM_START);
        ids.extend(self.bpe("assistant\n")?);
        if let Some(lang) = language {
            ids.extend(self.bpe(&format!("language {lang}"))?);
            ids.push(ASR_TEXT);
        }
        Ok((ids, audio_at))
    }

    /// batch_decode(skip_special_tokens=True): special added tokens are dropped, <asr_text> is kept.
    pub fn decode(&self, ids: &[u32]) -> String {
        let mut bytes = Vec::new();
        for id in ids {
            if let Some(Some(b)) = self.pieces.get(id) {
                bytes.extend_from_slice(b);
            }
        }
        String::from_utf8_lossy(&bytes).into_owned()
    }
}

/// parse_asr_output: "language Chinese<asr_text>..." -> ("Chinese", "..."); "language None" -> empty.
pub fn parse_output(raw: &str, forced: Option<&str>) -> (String, String) {
    let s = raw.trim();
    if let Some(lang) = forced {
        return (lang.to_string(), s.to_string());
    }
    match s.split_once("<asr_text>") {
        Some((meta, text)) => {
            let lang = meta.lines().find_map(|l| l.trim().strip_prefix("language ")).unwrap_or("").trim().to_string();
            if lang.eq_ignore_ascii_case("none") {
                return (String::new(), String::new());
            }
            (lang, text.trim().to_string())
        }
        None => (String::new(), s.to_string()),
    }
}

/// The game UI is Traditional Chinese; Mandarin comes back Simplified.
/// The script Chinese is shown in: the player's language ("zh-CN": Simplified; else Traditional), set when listening
/// starts (`/listen/start` {"script": "zh-CN"}).
static SIMPLIFIED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub fn set_script(lang: &str) {
    SIMPLIFIED.store(lang.eq_ignore_ascii_case("zh-CN"), std::sync::atomic::Ordering::Relaxed);
}

pub fn to_display(language: &str, text: &str) -> String {
    if language.eq_ignore_ascii_case("chinese") || language.eq_ignore_ascii_case("cantonese") {
        let variant = if SIMPLIFIED.load(std::sync::atomic::Ordering::Relaxed) { zhconv::Variant::ZhCN } else { zhconv::Variant::ZhTW };
        zhconv::zhconv(text, variant)
    } else {
        text.to_string()
    }
}
