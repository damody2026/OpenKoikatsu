//! Baked opens inside the card (`okBk` PNG chunk): the open-cache entries of the card's outfits, so a card opens like a
//! cache hit on any machine, the first time. Read only with `OK_CARD_BAKED=1`; written by `ok-card-bake`.
//!
//! Payload: `OKBK0001`, a chunk table, then the entries.
//!   chunk:  codec u8 (0 LZ4 block, 1 zstd), raw length u32, packed length u32, packed bytes
//!   entry:  key (u16 length + UTF-8), fallback identity (u16 length + UTF-8), chunk count u32, chunk indices u32...
//! An entry's preview bytes are its chunks concatenated. Chunks are content-defined (oklib::chunk_spans), so outfits
//! of one card share their identical body, face and hair data. The key is the open-cache key, which hashes the card
//! without its `okBk` chunk: a baked card and the plain card share one key.
use std::path::Path;

pub const BAKE_CHUNK: &[u8; 4] = b"okBk";
/// Recorded memo results of the open (memosnap.rs), same container; read with OK_CARD_MEMO.
pub const MEMO_CHUNK: &[u8; 4] = b"okMm";
const MAGIC: &[u8; 8] = b"OKBK0001";

/// Opens read the baked entries (`OK_CARD_BAKED=1`; read once per process).
pub fn enabled() -> bool {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ON.get_or_init(|| std::env::var("OK_CARD_BAKED").as_deref() == Ok("1"))
}

/// The card's PNG chunks as (offset of the length field, total size including length, type and CRC, type).
fn chunks(bytes: &[u8]) -> Vec<(usize, usize, [u8; 4])> {
    let mut out = Vec::new();
    if bytes.len() < 8 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return out; }
    let mut pos = 8usize;
    while pos + 12 <= bytes.len() {
        let len = u32::from_be_bytes(bytes[pos..pos + 4].try_into().unwrap()) as usize;
        let kind: [u8; 4] = bytes[pos + 4..pos + 8].try_into().unwrap();
        let total = 12 + len;
        if pos + total > bytes.len() { break; }
        out.push((pos, total, kind));
        if &kind == b"IEND" { break; }
        pos += total;
    }
    out
}

/// The card bytes without `okBk`/`okMm` chunks (borrowed when there is none).
pub fn strip(bytes: &[u8]) -> std::borrow::Cow<'_, [u8]> {
    strip_kinds(bytes, &[BAKE_CHUNK, MEMO_CHUNK])
}
fn strip_kinds<'a>(bytes: &'a [u8], kinds: &[&[u8; 4]]) -> std::borrow::Cow<'a, [u8]> {
    let list = chunks(bytes);
    if !list.iter().any(|c| kinds.contains(&&c.2)) { return std::borrow::Cow::Borrowed(bytes); }
    let mut out = bytes[..8].to_vec();
    let mut end = 8;
    for (pos, total, kind) in list {
        end = pos + total;
        if !kinds.contains(&&kind) { out.extend_from_slice(&bytes[pos..pos + total]); }
    }
    out.extend_from_slice(&bytes[end..]);
    std::borrow::Cow::Owned(out)
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Codec { Lz4, Zstd(i32) }

/// One entry to bake: open-cache key, fallback identity (empty for an OK card), preview bytes.
pub struct Entry { pub key: String, pub fallback: String, pub preview: Vec<u8> }

/// The `okBk` payload for these entries.
pub fn build(entries: &[Entry], codec: Codec) -> Result<Vec<u8>, String> {
    use rayon::prelude::*;
    let mut table: Vec<&[u8]> = Vec::new();
    let mut index: std::collections::HashMap<&[u8], u32> = std::collections::HashMap::new();
    let mut refs: Vec<Vec<u32>> = Vec::new();
    for e in entries {
        let mut list = Vec::new();
        for span in crate::oklib::chunk_spans(&e.preview) {
            let piece = &e.preview[span];
            let next = table.len() as u32;
            let id = *index.entry(piece).or_insert_with(|| { table.push(piece); next });
            list.push(id);
        }
        refs.push(list);
    }
    let packed: Vec<Vec<u8>> = table.par_iter().map(|piece| match codec {
        Codec::Lz4 => Ok(lz4_flex::block::compress(piece)),
        Codec::Zstd(level) => zstd::bulk::compress(piece, level).map_err(|e| e.to_string()),
    }).collect::<Result<_, String>>()?;
    let mut out = MAGIC.to_vec();
    out.extend((table.len() as u32).to_le_bytes());
    for (piece, p) in table.iter().zip(&packed) {
        out.push(match codec { Codec::Lz4 => 0, Codec::Zstd(_) => 1 });
        out.extend((piece.len() as u32).to_le_bytes());
        out.extend((p.len() as u32).to_le_bytes());
        out.extend(p);
    }
    out.extend((entries.len() as u32).to_le_bytes());
    for (e, list) in entries.iter().zip(&refs) {
        for s in [&e.key, &e.fallback] {
            out.extend((s.len() as u16).to_le_bytes());
            out.extend(s.as_bytes());
        }
        out.extend((list.len() as u32).to_le_bytes());
        for id in list { out.extend(id.to_le_bytes()); }
    }
    Ok(out)
}

struct Reader<'a> { b: &'a [u8], at: usize }
impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Option<&'a [u8]> { let s = self.b.get(self.at..self.at + n)?; self.at += n; Some(s) }
    fn u8(&mut self) -> Option<u8> { Some(self.take(1)?[0]) }
    fn u16(&mut self) -> Option<u16> { Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?)) }
    fn u32(&mut self) -> Option<u32> { Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?)) }
    fn str(&mut self) -> Option<&'a str> { let n = self.u16()? as usize; std::str::from_utf8(self.take(n)?).ok() }
}

/// (key, fallback, preview bytes) of every entry, or only of `want` when given.
pub fn read(payload: &[u8], want: Option<&str>) -> Option<Vec<Entry>> {
    use rayon::prelude::*;
    let mut r = Reader { b: payload, at: 0 };
    if r.take(8)? != MAGIC { return None; }
    let n = r.u32()? as usize;
    let mut table = Vec::with_capacity(n);
    for _ in 0..n {
        let codec = r.u8()?;
        let raw = r.u32()? as usize;
        let len = r.u32()? as usize;
        table.push((codec, raw, r.take(len)?));
    }
    let count = r.u32()? as usize;
    let mut out = Vec::new();
    for _ in 0..count {
        let key = r.str()?.to_string();
        let fallback = r.str()?.to_string();
        let m = r.u32()? as usize;
        let mut ids = Vec::with_capacity(m);
        for _ in 0..m { ids.push(r.u32()? as usize); }
        if want.is_some_and(|w| w != key) { continue; }
        // Each chunk decodes into its own place of the output, in parallel.
        let mut offsets = Vec::with_capacity(m);
        let mut total = 0usize;
        for &id in &ids { let (_, raw, _) = table.get(id)?; offsets.push(total); total += raw; }
        let mut preview = vec![0u8; total];
        let mut slices: Vec<&mut [u8]> = Vec::with_capacity(m);
        let mut rest: &mut [u8] = &mut preview;
        for &id in &ids { let (head, tail) = rest.split_at_mut(table[id].1); slices.push(head); rest = tail; }
        let ok = slices.into_par_iter().zip(ids.par_iter()).all(|(dst, &id)| {
            let (codec, raw, packed) = table[id];
            match codec {
                0 => lz4_flex::block::decompress_into(packed, dst).ok() == Some(raw),
                1 => zstd::bulk::decompress_to_buffer(packed, dst).ok() == Some(raw),
                _ => false,
            }
        });
        if !ok { return None; }
        out.push(Entry { key, fallback, preview });
    }
    Some(out)
}

/// The baked preview for this open-cache key, when the card carries one that is still valid here.
pub fn load(card: &Path, key: &str) -> Option<Vec<u8>> {
    load_kind(card, BAKE_CHUNK, key)
}
/// The entry for this key in the card's chunk of this kind (okBk or okMm), when still valid here.
pub fn load_kind(card: &Path, kind: &[u8; 4], key: &str) -> Option<Vec<u8>> {
    let bytes = std::fs::read(card).ok()?;
    let payload = crate::oklib::png_chunk(&bytes, kind)?;
    let entry = read(payload, Some(key))?.into_iter().next()?;
    // An entry that used installed zipmods only counts while they are unchanged (as in the open cache).
    if !entry.fallback.is_empty() && entry.fallback != crate::gamefs::fallback_identity() { return None; }
    Some(entry.preview)
}

/// The card with this `okBk` payload (an existing one replaced), before IEND.
pub fn with_payload(card: &[u8], payload: &[u8]) -> Result<Vec<u8>, String> {
    with_chunk(card, BAKE_CHUNK, payload)
}
/// The card with this payload in a chunk of `kind` (an existing one of that kind replaced), before IEND.
pub fn with_chunk(card: &[u8], kind: &[u8; 4], payload: &[u8]) -> Result<Vec<u8>, String> {
    let plain = strip_kinds(card, &[kind]);
    let list = chunks(&plain);
    let iend = list.iter().find(|c| &c.2 == b"IEND").ok_or("card has no IEND")?.0;
    let len = u32::try_from(payload.len()).map_err(|_| "baked payload over 4 GiB")?;
    if len > 0x7fff_ffff { return Err("baked payload over the PNG chunk limit".into()); }
    let mut out = plain[..iend].to_vec();
    let mut chunk = kind.to_vec();
    chunk.extend_from_slice(payload);
    out.extend(len.to_be_bytes());
    out.extend(&chunk);
    out.extend(crate::portable::crc(&chunk).to_be_bytes());
    out.extend_from_slice(&plain[iend..]);
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn png_with(chunks: &[(&[u8; 4], &[u8])]) -> Vec<u8> {
        let mut out = b"\x89PNG\r\n\x1a\n".to_vec();
        for (kind, data) in chunks {
            out.extend((data.len() as u32).to_be_bytes());
            let mut c = kind.to_vec();
            c.extend_from_slice(data);
            out.extend(&c);
            out.extend(crate::portable::crc(&c).to_be_bytes());
        }
        out
    }
    #[test]
    fn entries_round_trip_share_chunks_and_strip_keeps_the_key() {
        let shared: Vec<u8> = (0..300_000u32).map(|i| (i.wrapping_mul(2654435761) >> 13) as u8).collect();
        let mut a = shared.clone(); a.extend(b"outfit-a");
        let mut b = shared.clone(); b.extend(b"outfit-b");
        for codec in [Codec::Lz4, Codec::Zstd(3)] {
            let payload = build(&[Entry { key: "k0".into(), fallback: String::new(), preview: a.clone() },
                Entry { key: "k1".into(), fallback: "fb".into(), preview: b.clone() }], codec).unwrap();
            // The shared part is stored once.
            assert!(payload.len() < shared.len() + shared.len() / 2, "{}", payload.len());
            let one = read(&payload, Some("k1")).unwrap();
            assert_eq!(one.len(), 1);
            assert_eq!(one[0].preview, b);
            assert_eq!(one[0].fallback, "fb");
            assert_eq!(read(&payload, None).unwrap().len(), 2);
            assert!(read(&payload, Some("none")).unwrap().is_empty());
        }
        let card = png_with(&[(b"IHDR", &[0; 13]), (b"IDAT", b"x"), (b"IEND", b"")]);
        let baked = with_payload(&card, b"payload").unwrap();
        assert_eq!(crate::oklib::png_chunk(&baked, BAKE_CHUNK), Some(&b"payload"[..]));
        assert_eq!(&*strip(&baked), &card[..]);
        let again = with_payload(&baked, b"other").unwrap();
        assert_eq!(&*strip(&again), &card[..]);
        assert_eq!(crate::oklib::png_chunk(&again, BAKE_CHUNK), Some(&b"other"[..]));
        // Both kinds side by side; the key strips both, replacing one keeps the other.
        let both = with_chunk(&again, MEMO_CHUNK, b"memo").unwrap();
        assert_eq!(crate::oklib::png_chunk(&both, BAKE_CHUNK), Some(&b"other"[..]));
        assert_eq!(crate::oklib::png_chunk(&both, MEMO_CHUNK), Some(&b"memo"[..]));
        assert_eq!(&*strip(&both), &card[..]);
    }
}
