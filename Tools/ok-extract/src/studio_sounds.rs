//! Studio sounds into the OK base library (U29): the BGM sheet's AudioClips as Ogg Vorbis files
//! `studio/bgm/<clip>.ogg`, and the BGM list into `studio_items.json`. A clip's data is an FMOD sound bank (FSB5) in
//! the bundle's `.resource` file (AudioClip.m_Resource: source, offset, size); `fsbex` writes its Vorbis stream as Ogg.
use std::path::Path;

/// The library folder of the BGM files.
pub const BGM_DIR: &str = "studio/bgm";

/// The FSB5 bank of AudioClip `clip` in a game bundle (`abdata/...`).
fn clip_bank(game: &Path, bundle: &str, clip: &str) -> Result<Vec<u8>, String> {
    use unity_asset_decode::object::UnityObject;
    let path = game.join(bundle);
    let ab = crate::unity::load_bundle_path_unbounded(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut resource: Option<(String, u64, u64)> = None;
    for file in &ab.files {
        let Ok(bytes) = ab.extract_file_data(file) else { continue };
        let Ok(serialized) = unity_asset_binary::asset::parse_serialized_file(bytes) else { continue };
        for info in serialized.objects_of_type(83) {
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let props = obj.class.properties();
            if props.get("m_Name").and_then(|v| v.as_str()) != Some(clip) { continue; }
            let r = props.get("m_Resource").and_then(|v| v.as_object()).ok_or("clip has no m_Resource")?;
            let source = r.get("m_Source").and_then(|v| v.as_str()).unwrap_or_default();
            // "archive:/CAB-…/CAB-….resource": the bundle file of that name.
            let name = source.rsplit('/').next().unwrap_or(source).to_owned();
            resource = Some((name, r.get("m_Offset").and_then(|v| v.as_i64()).unwrap_or(0) as u64, r.get("m_Size").and_then(|v| v.as_i64()).unwrap_or(0) as u64));
        }
    }
    let (name, offset, size) = resource.ok_or_else(|| format!("{bundle}: no AudioClip {clip}"))?;
    let file = ab.files.iter().find(|f| f.name == name).ok_or_else(|| format!("{bundle}: no resource file {name}"))?;
    let data = ab.extract_file_data(file).map_err(|e| e.to_string())?;
    data.get(offset as usize..(offset + size) as usize).map(<[u8]>::to_vec).ok_or_else(|| format!("{bundle}: {name} shorter than {offset}+{size}"))
}

/// The first stream of an FSB5 bank as Ogg Vorbis (or WAV for a PCM bank: fsbex writes each format's own container).
pub fn bank_to_audio(bank: &[u8]) -> Result<(Vec<u8>, &'static str), String> {
    let bank = fsbex::Bank::new(std::io::Cursor::new(bank)).map_err(|e| e.to_string())?;
    let ext = if bank.format() == fsbex::AudioFormat::Vorbis { "ogg" } else { "wav" };
    let stream = bank.into_iter().next().ok_or("empty sound bank")?;
    let mut out = Vec::new();
    stream.write(&mut out).map_err(|e| e.to_string())?;
    Ok((out, ext))
}

/// Writes every Studio BGM as `<lib>/studio/bgm/<clip>.ogg` (existing files kept) and the BGM list into
/// studio_items.json. Returns (written, already there, failed).
pub fn convert(game: &Path, lib: &Path) -> Result<(usize, usize, usize), String> {
    let file = lib.join(crate::studio_items::FILE_NAME);
    let text = std::fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
    let mut c: crate::studio_items::Catalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    c.bgm = crate::studio_items::catalog(game).bgm;
    crate::studio_items::write(lib, &c)?;
    let dir = lib.join(BGM_DIR);
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let (mut written, mut kept, mut failed) = (0, 0, 0);
    for b in &c.bgm {
        let out = dir.join(format!("{}.ogg", b.file));
        if out.is_file() { kept += 1; continue; }
        match clip_bank(game, &b.bundle, &b.file).and_then(|bank| bank_to_audio(&bank)) {
            Ok((bytes, "ogg")) => { std::fs::write(&out, bytes).map_err(|e| e.to_string())?; written += 1; }
            Ok((_, ext)) => { eprintln!("studio-sounds: {} is {ext}, not Vorbis (skipped)", b.file); failed += 1; }
            Err(e) => { eprintln!("studio-sounds: {} {}: {e}", b.no, b.file); failed += 1; }
        }
    }
    Ok((written, kept, failed))
}
