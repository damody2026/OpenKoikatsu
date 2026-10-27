//! Texture bytes between pipeline stages. Normally PNG; with `OK_RAW_TEXTURES=1` the open path writes a raw container
//! instead (`OKRAW\0\2\0`, width and height as little-endian u32, then one LZ4 block of the RGBA8 rows, top first).
//! LZ4 both ways is several times cheaper than deflate, and the bytes stay small enough to hash, cache and copy.
//! Every reader accepts both forms, so cached PNG results and raw ones can meet in one session; Unreal decodes it
//! with `ok_raw_texture.h`. Persistent outputs (library models, cards, exported files) keep writing PNG.
use image::{DynamicImage, ImageResult, RgbaImage};

pub(crate) const RAW_MAGIC: &[u8; 8] = b"OKRAW\0\x02\0";

/// The open path writes raw textures (`OK_RAW_TEXTURES=1`; read once per process).
pub(crate) fn raw_enabled() -> bool {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ON.get_or_init(|| std::env::var("OK_RAW_TEXTURES").as_deref() == Ok("1"))
}

pub(crate) fn is_raw(bytes: &[u8]) -> bool {
    bytes.len() >= 16 && &bytes[..8] == RAW_MAGIC
}

/// Width and height of a raw container.
pub(crate) fn raw_size(bytes: &[u8]) -> Option<(u32, u32)> {
    if !is_raw(bytes) { return None; }
    let w = u32::from_le_bytes(bytes[8..12].try_into().ok()?);
    let h = u32::from_le_bytes(bytes[12..16].try_into().ok()?);
    (w > 0 && h > 0 && (w as u64 * h as u64) <= 1 << 28).then_some((w, h))
}

/// RGBA8 pixels of a raw container.
pub(crate) fn raw_pixels(bytes: &[u8]) -> Option<(u32, u32, Vec<u8>)> {
    let (w, h) = raw_size(bytes)?;
    let n = w as usize * h as usize * 4;
    let pixels = lz4_flex::block::decompress(&bytes[16..], n).ok()?;
    (pixels.len() == n).then_some((w, h, pixels))
}

/// `image::load_from_memory` that also reads the raw container.
pub(crate) fn load_from_memory(bytes: &[u8]) -> ImageResult<DynamicImage> {
    if let Some((w, h, pixels)) = raw_pixels(bytes) {
        if let Some(img) = RgbaImage::from_raw(w, h, pixels) { return Ok(DynamicImage::ImageRgba8(img)); }
    }
    image::load_from_memory(bytes)
}

pub(crate) fn raw_bytes(img: &RgbaImage) -> Vec<u8> {
    let packed = lz4_flex::block::compress(img.as_raw());
    let mut out = Vec::with_capacity(16 + packed.len());
    out.extend_from_slice(RAW_MAGIC);
    out.extend_from_slice(&img.width().to_le_bytes());
    out.extend_from_slice(&img.height().to_le_bytes());
    out.extend_from_slice(&packed);
    out
}

/// Texture bytes of an RGBA image for the next stage: raw when enabled, else PNG.
pub(crate) fn encode_rgba(img: &RgbaImage) -> Option<Vec<u8>> {
    if raw_enabled() { return Some(raw_bytes(img)); }
    // OK_PNG_DEFAULT=1: the default level, as before (byte-for-byte comparisons with older builds).
    if std::env::var_os("OK_PNG_DEFAULT").is_some() { return png_bytes(img); }
    png_fast_bytes(img)
}

/// PNG with the fast deflate (fdeflate): a session's textures are decoded again right away (by the runtime, or by the
/// next transform), so a few percent of size is not worth the default level's time (a 2048² map: ~0.3 s -> ~0.05 s).
pub(crate) fn png_fast_bytes(img: &RgbaImage) -> Option<Vec<u8>> {
    use image::ImageEncoder;
    use image::codecs::png::{CompressionType, FilterType, PngEncoder};
    let mut buf = Vec::new();
    PngEncoder::new_with_quality(&mut buf, CompressionType::Fast, FilterType::Up)
        .write_image(img.as_raw(), img.width(), img.height(), image::ExtendedColorType::Rgba8).ok()?;
    Some(buf)
}

/// Same, taking the image.
pub(crate) fn encode_rgba_owned(img: RgbaImage) -> Option<Vec<u8>> {
    encode_rgba(&img)
}

/// Always PNG (persistent outputs, card images).
pub(crate) fn png_bytes(img: &RgbaImage) -> Option<Vec<u8>> {
    use image::ImageEncoder;
    let mut buf = Vec::new();
    image::codecs::png::PngEncoder::new(&mut buf)
        .write_image(img.as_raw(), img.width(), img.height(), image::ExtendedColorType::Rgba8).ok()?;
    Some(buf)
}

/// A raw container as PNG; PNG input is returned unchanged.
pub(crate) fn to_png(bytes: Vec<u8>) -> Vec<u8> {
    if let Some((w, h, pixels)) = raw_pixels(&bytes) {
        if let Some(img) = RgbaImage::from_raw(w, h, pixels) { if let Some(png) = png_bytes(&img) { return png; } }
    }
    bytes
}

/// PNG bytes for writing a `.png` file (raw containers converted).
pub(crate) fn as_png(bytes: &[u8]) -> std::borrow::Cow<'_, [u8]> {
    if is_raw(bytes) { std::borrow::Cow::Owned(to_png(bytes.to_vec())) } else { std::borrow::Cow::Borrowed(bytes) }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn raw_round_trip_and_png_still_loads() {
        let mut img = RgbaImage::new(5, 3);
        img.put_pixel(4, 2, image::Rgba([1, 2, 3, 4]));
        let raw = raw_bytes(&img);
        assert_eq!(raw_size(&raw), Some((5, 3)));
        assert_eq!(load_from_memory(&raw).unwrap().to_rgba8(), img);
        let png = png_bytes(&img).unwrap();
        assert!(!is_raw(&png));
        assert_eq!(load_from_memory(&png).unwrap().to_rgba8(), img);
        assert_eq!(load_from_memory(&to_png(raw.clone())).unwrap().to_rgba8(), img);
        // A truncated container does not decode.
        assert!(raw_pixels(&raw[..raw.len() - 1]).is_none());
    }
    #[test]
    fn png_bytes_equal_the_previous_encoder() {
        let img = RgbaImage::from_fn(97, 61, |x, y| image::Rgba([(x * 3) as u8, (y * 5) as u8, (x ^ y) as u8, (x * y) as u8]));
        let mut old = Vec::new();
        DynamicImage::ImageRgba8(img.clone()).write_to(&mut std::io::Cursor::new(&mut old), image::ImageFormat::Png).unwrap();
        assert_eq!(png_bytes(&img).unwrap(), old);
        // A session's textures take the fast deflate: other bytes, the same pixels.
        assert_eq!(load_from_memory(&encode_rgba(&img).unwrap()).unwrap().to_rgba8(), img);
    }
}
