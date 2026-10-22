//! A character's own voice inside her card: the `.okvoice` bytes (profile.rs) in a PNG chunk named `okVo` (ancillary,
//! private, safe to copy, like the card's other `ok..` chunks). Works on PNG and APNG cards alike; images are not
//! re-encoded, other chunks keep their order, and whatever follows IEND (the original game's card data) is kept.
//!
//!   ok-tts card-voice --card CARD.png                       show the voice in a card
//!   ok-tts card-voice --card CARD.png --set VOICE.okvoice   put a voice in (replacing one already there)
//!   ok-tts card-voice --card CARD.png --remove              take it out

use std::path::Path;

use anyhow::{bail, Context, Result};

use crate::audio::write_atomic;
use crate::profile::Profile;

pub const CHUNK: &[u8; 4] = b"okVo";
const SIGNATURE: &[u8; 8] = b"\x89PNG\r\n\x1a\n";

fn crc32(data: &[&[u8]]) -> u32 {
    static TABLE: std::sync::OnceLock<[u32; 256]> = std::sync::OnceLock::new();
    let table = TABLE.get_or_init(|| {
        let mut t = [0u32; 256];
        for (n, slot) in t.iter_mut().enumerate() {
            let mut c = n as u32;
            for _ in 0..8 {
                c = if c & 1 != 0 { 0xedb8_8320 ^ (c >> 1) } else { c >> 1 };
            }
            *slot = c;
        }
        t
    });
    let mut c = 0xffff_ffffu32;
    for part in data {
        for b in *part {
            c = table[((c ^ *b as u32) & 0xff) as usize] ^ (c >> 8);
        }
    }
    c ^ 0xffff_ffff
}

struct Chunk<'a> {
    kind: [u8; 4],
    /// the whole chunk: length, type, data, CRC
    raw: &'a [u8],
    data: &'a [u8],
}

/// Chunks up to and including IEND, and the bytes after it.
fn parse(png: &[u8]) -> Result<(Vec<Chunk<'_>>, &[u8])> {
    if png.len() < 8 || &png[..8] != SIGNATURE {
        bail!("not a PNG");
    }
    let mut at = 8;
    let mut chunks = Vec::new();
    loop {
        let head = png.get(at..at + 8).context("PNG ends before IEND")?;
        let len = u32::from_be_bytes([head[0], head[1], head[2], head[3]]) as usize;
        let kind = [head[4], head[5], head[6], head[7]];
        let end = at + 12 + len;
        let raw = png.get(at..end).context("PNG chunk runs past the end")?;
        chunks.push(Chunk { kind, raw, data: &raw[8..8 + len] });
        at = end;
        if &kind == b"IEND" {
            return Ok((chunks, &png[at..]));
        }
    }
}

/// The voice stored in a card, if any.
pub fn read(png: &[u8]) -> Result<Option<Profile>> {
    let (chunks, _) = parse(png)?;
    match chunks.iter().find(|c| &c.kind == CHUNK) {
        Some(c) => Ok(Some(Profile::from_bytes(c.data).context("okVo chunk")?)),
        None => Ok(None),
    }
}

/// The card with `voice` stored (None: removed). The chunk goes right before IEND.
pub fn write(png: &[u8], voice: Option<&Profile>) -> Result<Vec<u8>> {
    let (chunks, trailing) = parse(png)?;
    let mut out = Vec::with_capacity(png.len() + 64 * 1024);
    out.extend_from_slice(SIGNATURE);
    for c in &chunks {
        if &c.kind == CHUNK {
            continue;
        }
        if &c.kind == b"IEND" {
            if let Some(v) = voice {
                let data = v.to_bytes()?;
                out.extend_from_slice(&(data.len() as u32).to_be_bytes());
                out.extend_from_slice(CHUNK);
                out.extend_from_slice(&data);
                out.extend_from_slice(&crc32(&[CHUNK, &data]).to_be_bytes());
            }
        }
        out.extend_from_slice(c.raw);
    }
    out.extend_from_slice(trailing);
    Ok(out)
}

pub fn read_file(card: &Path) -> Result<Option<Profile>> {
    read(&std::fs::read(card).with_context(|| format!("read {}", card.display()))?).with_context(|| format!("card {}", card.display()))
}

/// Store a voice in a card file (atomically; the card is otherwise unchanged).
pub fn write_file(card: &Path, voice: Option<&Profile>) -> Result<()> {
    let png = std::fs::read(card).with_context(|| format!("read {}", card.display()))?;
    let out = write(&png, voice).with_context(|| format!("card {}", card.display()))?;
    // Check before replacing: the new file reads back to the same voice.
    let back = read(&out)?;
    if back.as_ref().map(|p| p.tag()) != voice.map(|p| p.tag()) {
        bail!("the voice did not read back from {}", card.display());
    }
    write_atomic(card, &out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profile::{VoiceMeta, ENCODER, SCHEMA};

    fn chunk(kind: &[u8; 4], data: &[u8]) -> Vec<u8> {
        let mut v = (data.len() as u32).to_be_bytes().to_vec();
        v.extend_from_slice(kind);
        v.extend_from_slice(data);
        v.extend_from_slice(&crc32(&[kind, data]).to_be_bytes());
        v
    }

    fn voice(seed: u64) -> Profile {
        Profile {
            meta: VoiceMeta { schema: SCHEMA, id: "card".into(), source: "runtime".into(), description: "女性".into(), seed,
                              ref_text: "好。".into(), ref_audio_sha1: String::new(), design_model: "d".into(), base_model: "b".into(),
                              encoder: ENCODER.into(), created: "unix:0".into() },
            embedding: (0..2048).map(|i| i as f32 * 0.5 + seed as f32).collect(),
            ref_ogg: vec![9; 100],
        }
    }

    #[test]
    fn crc_matches_png_iend() {
        // Every PNG ends with IEND, CRC ae 42 60 82.
        assert_eq!(crc32(&[b"IEND", b""]), 0xae42_6082);
    }

    #[test]
    fn voice_goes_in_replaces_and_comes_out_keeping_the_rest() {
        let mut png = SIGNATURE.to_vec();
        png.extend(chunk(b"IHDR", &[0; 13]));
        png.extend(chunk(b"okPv", b"preview"));
        png.extend(chunk(b"IDAT", &[1, 2, 3]));
        png.extend(chunk(b"IEND", b""));
        png.extend_from_slice(b"original game card data after IEND");
        assert!(read(&png).unwrap().is_none());
        let a = write(&png, Some(&voice(1))).unwrap();
        assert_eq!(read(&a).unwrap().unwrap().tag(), voice(1).tag());
        assert!(a.ends_with(b"original game card data after IEND"));
        let b = write(&a, Some(&voice(2))).unwrap();
        assert_eq!(read(&b).unwrap().unwrap().tag(), voice(2).tag());
        assert_eq!(b.len(), a.len(), "replaced, not added");
        let c = write(&b, None).unwrap();
        assert_eq!(c, png, "removing gives the original card back");
    }
}
