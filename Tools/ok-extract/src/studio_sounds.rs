//! Studio sounds into the OK base library: the BGM sheet's AudioClips as Ogg Vorbis files `studio/bgm/<clip>.ogg` and
//! the BGM list into `studio_items.json` (U29); the character voice lists into `studio_voices.json` and their clips as
//! `studio/voice/<clip>.ogg` (U30). A clip's data is an FMOD sound bank (FSB5) in the bundle's `.resource` file
//! (AudioClip.m_Resource: source, offset, size); `fsbex` writes its Vorbis stream as Ogg.
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

/// The library folder of the BGM files.
pub const BGM_DIR: &str = "studio/bgm";
/// The library folder of the character voice files.
pub const VOICE_DIR: &str = "studio/voice";

/// The FSB5 banks of the AudioClips named in `clips` in a game bundle (`abdata/...`), loading the bundle once.
fn clip_banks(game: &Path, bundle: &str, clips: &BTreeSet<&str>) -> Result<Vec<(String, Vec<u8>)>, String> {
    use unity_asset_decode::object::UnityObject;
    let path = game.join(bundle);
    let ab = crate::unity::load_bundle_path_unbounded(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    // clip → ("CAB-….resource" file name, offset, size)
    let mut found: Vec<(String, String, u64, u64)> = Vec::new();
    for file in &ab.files {
        let Ok(bytes) = ab.extract_file_data(file) else { continue };
        let Ok(serialized) = unity_asset_binary::asset::parse_serialized_file(bytes) else { continue };
        for info in serialized.objects_of_type(83) {
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue };
            let props = obj.class.properties();
            let Some(name) = props.get("m_Name").and_then(|v| v.as_str()).filter(|n| clips.contains(n)) else { continue };
            let Some(r) = props.get("m_Resource").and_then(|v| v.as_object()) else { continue };
            let source = r.get("m_Source").and_then(|v| v.as_str()).unwrap_or_default();
            // "archive:/CAB-…/CAB-….resource": the bundle file of that name.
            let res = source.rsplit('/').next().unwrap_or(source).to_owned();
            found.push((name.to_owned(), res, r.get("m_Offset").and_then(|v| v.as_i64()).unwrap_or(0) as u64, r.get("m_Size").and_then(|v| v.as_i64()).unwrap_or(0) as u64));
        }
    }
    let mut resources: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut out = Vec::with_capacity(found.len());
    for (clip, res, offset, size) in found {
        if !resources.contains_key(&res) {
            let file = ab.files.iter().find(|f| f.name == res).ok_or_else(|| format!("{bundle}: no resource file {res}"))?;
            resources.insert(res.clone(), ab.extract_file_data(file).map_err(|e| e.to_string())?);
        }
        let data = &resources[&res];
        match data.get(offset as usize..(offset + size) as usize) {
            Some(bank) => out.push((clip, bank.to_vec())),
            None => eprintln!("studio-sounds: {bundle}: {res} shorter than {offset}+{size} ({clip})"),
        }
    }
    Ok(out)
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

/// Writes the clips of `wanted` (bundle → clips) missing from `dir` as `<dir>/<clip>.ogg`, one bundle per thread.
/// Returns (written, already there, failed).
fn write_clips(game: &Path, dir: &Path, wanted: &BTreeMap<&str, BTreeSet<&str>>) -> Result<(usize, usize, usize), String> {
    use rayon::prelude::*;
    use std::sync::atomic::{AtomicUsize, Ordering::Relaxed};
    std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    let (written, kept, failed) = (AtomicUsize::new(0), AtomicUsize::new(0), AtomicUsize::new(0));
    wanted.par_iter().for_each(|(bundle, clips)| {
        let missing: BTreeSet<&str> = clips.iter().copied().filter(|c| !dir.join(format!("{c}.ogg")).is_file()).collect();
        kept.fetch_add(clips.len() - missing.len(), Relaxed);
        if missing.is_empty() { return; }
        let banks = match clip_banks(game, bundle, &missing) {
            Ok(b) => b,
            Err(e) => { eprintln!("studio-sounds: {e}"); failed.fetch_add(missing.len(), Relaxed); return; }
        };
        let mut done = BTreeSet::new();
        for (clip, bank) in banks {
            match bank_to_audio(&bank) {
                Ok((bytes, "ogg")) if std::fs::write(dir.join(format!("{clip}.ogg")), &bytes).is_ok() => { written.fetch_add(1, Relaxed); }
                Ok((_, ext)) => { eprintln!("studio-sounds: {clip} is {ext}, not Vorbis (skipped)"); failed.fetch_add(1, Relaxed); }
                Err(e) => { eprintln!("studio-sounds: {clip}: {e}"); failed.fetch_add(1, Relaxed); }
            }
            done.insert(clip);
        }
        for c in missing.iter().filter(|c| !done.contains(**c)) { eprintln!("studio-sounds: {bundle}: no AudioClip {c}"); failed.fetch_add(1, Relaxed); }
    });
    Ok((written.into_inner(), kept.into_inner(), failed.into_inner()))
}

/// Writes every Studio BGM as `<lib>/studio/bgm/<clip>.ogg` (existing files kept) and the BGM list into
/// studio_items.json. Returns (written, already there, failed).
pub fn convert(game: &Path, lib: &Path) -> Result<(usize, usize, usize), String> {
    let file = lib.join(crate::studio_items::FILE_NAME);
    let text = std::fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
    let mut c: crate::studio_items::Catalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    c.bgm = crate::studio_items::catalog(game).bgm;
    crate::studio_items::write(lib, &c)?;
    let mut wanted: BTreeMap<&str, BTreeSet<&str>> = BTreeMap::new();
    for b in &c.bgm { wanted.entry(b.bundle.as_str()).or_default().insert(b.file.as_str()); }
    write_clips(game, &lib.join(BGM_DIR), &wanted)
}

/// Writes the Studio voice lists as `<lib>/studio_voices.json` and every voice as `<lib>/studio/voice/<clip>.ogg`
/// (existing files kept). Returns (written, already there, failed).
pub fn convert_voices(game: &Path, lib: &Path) -> Result<(usize, usize, usize), String> {
    let voices = crate::studio_items::catalog(game).voices;
    let text = serde_json::to_string_pretty(&voices).map_err(|e| e.to_string())?;
    std::fs::write(lib.join(crate::studio_items::VOICE_FILE_NAME), text).map_err(|e| e.to_string())?;
    let mut wanted: BTreeMap<&str, BTreeSet<&str>> = BTreeMap::new();
    for v in &voices.voices { wanted.entry(v.bundle.as_str()).or_default().insert(v.file.as_str()); }
    write_clips(game, &lib.join(VOICE_DIR), &wanted)
}
