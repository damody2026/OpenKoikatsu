//! Text side: Qwen2 byte-level BPE, chat-template token ids, voice prompts, dialogue clean-up.

use std::collections::HashMap;
use std::path::Path;

use anyhow::{anyhow, bail, Context, Result};
use serde::Deserialize;
use tokenizers::models::bpe::BPE;
use tokenizers::normalizers::unicode::NFC;
use tokenizers::pre_tokenizers::byte_level::ByteLevel;
use tokenizers::pre_tokenizers::sequence::Sequence;
use tokenizers::pre_tokenizers::split::{Split, SplitPattern};
use tokenizers::{PreTokenizerWrapper, SplitDelimiterBehavior, Tokenizer};

pub const IM_START: u32 = 151644;
pub const IM_END: u32 = 151645;
pub const LANGUAGE_CHINESE: u32 = 2055;
pub const LANGUAGE_ENGLISH: u32 = 2050; // talker_config.codec_language_id

/// The language a line is read in, from the line itself: Chinese (or Japanese) characters -> Chinese, else letters ->
/// English (the game's English mode). Callers need not say.
pub fn language_of(text: &str) -> u32 {
    let cjk = text.chars().any(|c| matches!(c as u32, 0x3040..=0x30FF | 0x3400..=0x9FFF | 0xF900..=0xFAFF));
    let latin = text.chars().filter(|c| c.is_ascii_alphabetic()).count();
    if !cjk && latin > 0 { LANGUAGE_ENGLISH } else { LANGUAGE_CHINESE }
}

/// A voice's design (written for Mandarin) for an English line: the sentences about Mandarin and Chinese diction are
/// replaced by one asking for natural American English; the timbre, age and manner stay.
pub fn english_instruct(instruct: &str) -> String {
    let kept: Vec<&str> = instruct
        .split_inclusive(['。', '；'])
        .filter(|s| !["普通话", "普通話", "北京", "中文", "汉语", "漢語", "咬字"].iter().any(|w| s.contains(w)))
        .collect();
    format!("{}用自然的美式英语说话，发音清楚。", kept.concat())
}

const QWEN2_SPLIT: &str = r"(?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\r\n\p{L}\p{N}]?\p{L}+|\p{N}| ?[^\s\p{L}\p{N}]+[\r\n]*|\s*[\r\n]+|\s+(?!\S)|\s+";

pub struct TextTokenizer {
    tk: Tokenizer,
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
        Ok(Self { tk })
    }

    /// Plain BPE ids for a text without special tokens.
    pub fn bpe(&self, text: &str) -> Result<Vec<u32>> {
        if text.is_empty() {
            return Ok(Vec::new());
        }
        Ok(self.tk.encode(text, false).map_err(|e| anyhow!("tokenize: {e}"))?.get_ids().to_vec())
    }

    /// "<|im_start|>assistant\n{text}<|im_end|>\n<|im_start|>assistant\n"
    pub fn assistant(&self, text: &str) -> Result<Vec<u32>> {
        let mut ids = vec![IM_START];
        ids.extend(self.bpe(&format!("assistant\n{text}"))?);
        ids.push(IM_END);
        ids.extend(self.bpe("\n")?);
        ids.push(IM_START);
        ids.extend(self.bpe("assistant\n")?);
        Ok(ids)
    }

    /// "<|im_start|>user\n{instruct}<|im_end|>\n"
    pub fn instruct(&self, instruct: &str) -> Result<Vec<u32>> {
        let mut ids = vec![IM_START];
        ids.extend(self.bpe(&format!("user\n{instruct}"))?);
        ids.push(IM_END);
        ids.extend(self.bpe("\n")?);
        Ok(ids)
    }

    /// Like Python tokenizer(text): splits on <|im_start|> / <|im_end|> markers (used by parity tests).
    pub fn encode_with_markers(&self, text: &str) -> Result<Vec<u32>> {
        let mut ids = Vec::new();
        let mut rest = text;
        loop {
            let a = rest.find("<|im_start|>").map(|i| (i, IM_START, 12));
            let b = rest.find("<|im_end|>").map(|i| (i, IM_END, 10));
            let next = match (a, b) {
                (Some(x), Some(y)) => Some(if x.0 < y.0 { x } else { y }),
                (x, y) => x.or(y),
            };
            match next {
                Some((i, id, len)) => {
                    ids.extend(self.bpe(&rest[..i])?);
                    ids.push(id);
                    rest = &rest[i + len..];
                }
                None => {
                    ids.extend(self.bpe(rest)?);
                    return Ok(ids);
                }
            }
        }
    }
}

#[derive(Deserialize)]
struct VoiceEntry {
    timbre: String,
    seed: u64,
    #[serde(default)]
    name: String,
    #[serde(default)]
    label: String,
}

#[derive(Deserialize)]
struct MaleEntry {
    name: String,
    timbre: String,
    seed: u64,
}

#[derive(Deserialize)]
struct VoiceFile {
    #[serde(default)]
    male_prefix: String,
    #[serde(default)]
    male_suffix: String,
    #[serde(default)]
    male: HashMap<String, MaleEntry>,
    female_prefix: String,
    suffix: String,
    age12: String,
    age_hold: HashMap<String, String>,
    fallback: String,
    voices: HashMap<String, VoiceEntry>,
}

/// Voice-design prompts per personality and age (exported from E:\TTS\gen_ages.py).
pub struct Voices {
    file: VoiceFile,
}

pub struct VoiceChoice {
    pub key: String,
    pub age: u32,
    pub instruct: String,
    pub seed: u64,
    /// Personality / tone name and short timbre label, for display in the game.
    pub name: String,
    pub label: String,
}

impl Voices {
    pub fn load(path: &Path) -> Result<Self> {
        let body = std::fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
        let file: VoiceFile = serde_json::from_str(&body).context("voices.json")?;
        if !file.voices.contains_key(&file.fallback) {
            bail!("voices.json fallback {} is not a voice", file.fallback);
        }
        Ok(Self { file })
    }

    /// Ages with their own design besides 12 (which is one design for everyone).
    pub fn ages(&self) -> Vec<u32> {
        let mut a: Vec<u32> = self.file.age_hold.keys().filter_map(|k| k.parse().ok()).collect();
        a.sort();
        a
    }

    pub fn female_keys(&self) -> Vec<String> {
        let mut k: Vec<String> = self.file.voices.keys().cloned().collect();
        k.sort();
        k
    }

    pub fn male_keys(&self) -> Vec<String> {
        let mut k: Vec<String> = self.file.male.keys().cloned().collect();
        k.sort();
        k
    }

    /// Game sends ints (0, -13) or strings ("00"); prompts are keyed "00", "-13".
    /// "m0".."m9" select a male tone (the player character); age does not apply to them.
    pub fn choose(&self, personality: &str, age: u32) -> VoiceChoice {
        if let Some(m) = self.file.male.get(personality.trim()) {
            let instruct = format!("{}{}{}", self.file.male_prefix, m.timbre, self.file.male_suffix);
            return VoiceChoice { key: personality.trim().to_string(), age: 0, instruct, seed: m.seed, name: m.name.clone(), label: m.timbre.clone() };
        }
        let key = match personality.trim().parse::<i64>() {
            Ok(n) if n >= 0 => format!("{n:02}"),
            Ok(n) => n.to_string(),
            Err(_) => personality.trim().to_string(),
        };
        let key = if self.file.voices.contains_key(&key) { key } else { self.file.fallback.clone() };
        let voice = &self.file.voices[&key];
        let age = if age == 12 || self.file.age_hold.contains_key(&age.to_string()) { age } else { 18 };
        let instruct = if age == 12 {
            format!("{}{}", self.file.female_prefix, self.file.age12)
        } else {
            format!("{}{}{}{}", self.file.female_prefix, voice.timbre, self.file.age_hold[&age.to_string()], self.file.suffix)
        };
        VoiceChoice { name: voice.name.clone(), label: voice.label.clone(), key, age, instruct, seed: voice.seed }
    }
}

/// Dialogue line -> what the model should read: drop ADV quote marks and decoration, Simplified Chinese,
/// end with a full stop.
pub fn clean_line(text: &str) -> String {
    let decoration = |c: &char| !matches!(c, '「' | '」' | '『' | '』' | '【' | '】' | '[' | ']' | '♡' | '♥' | '❤' | '☆' | '★' | '♪' | '♫');
    if language_of(text) == LANGUAGE_ENGLISH {
        // English keeps its spaces (one between words) and ends with a period.
        let words: Vec<String> = text.split_whitespace().map(|w| w.chars().filter(decoration).collect::<String>()).filter(|w| !w.is_empty()).collect();
        let mut s = words.join(" ");
        if let Some(last) = s.chars().last() {
            if !".!?…~".contains(last) {
                s.push('.');
            }
        }
        return s;
    }
    let stripped: String = text.chars().filter(decoration).filter(|c| !c.is_whitespace()).collect();
    let converted = zhconv::zhconv(&stripped, zhconv::Variant::ZhCN);
    // A pause mark right before an end mark ("她，。" from a cut reply) is dropped, and a line ending on a pause
    // mark ends with a full stop instead: the model read "，。" as an odd breath.
    let mut s = String::with_capacity(converted.len() + 3);
    for c in converted.chars() {
        if "。！？～…!?".contains(c) {
            while s.ends_with(is_pause_mark) {
                s.pop();
            }
        }
        s.push(c);
    }
    while s.ends_with(is_pause_mark) {
        s.pop();
    }
    if let Some(last) = s.chars().last() {
        if !"。！？～…!?".contains(last) {
            s.push('。');
        }
    }
    s
}

fn is_pause_mark(c: char) -> bool {
    matches!(c, '，' | '、' | ',' | '；' | ';' | '：' | ':')
}

#[cfg(test)]
mod tests {
    use super::clean_line;

    #[test]
    fn pause_before_end_mark_is_dropped() {
        assert_eq!(clean_line("哼，那干嘛非找她，。"), "哼，那干嘛非找她。");
        assert_eq!(clean_line("到底是没听见，"), "到底是没听见。");
        assert_eq!(clean_line("好、？"), "好？");
        assert_eq!(clean_line("嗯"), "嗯。");
        assert_eq!(clean_line("「還是不想理我？」"), "还是不想理我？");
        assert_eq!(clean_line("，"), "");
    }
}
