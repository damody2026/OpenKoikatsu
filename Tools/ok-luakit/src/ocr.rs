//! `ocr <request.json> <output.json> [--lang zh-Hant-TW,zh-Hans-CN]`: Windows.Media.Ocr over image files.
//! Request: [{id, path, ...}]. Output: [{id, path, texts: {<lang>: text}}] (UTF-8), stdout {rows: n}.
//! Same engines and result shape as the former Tools/ocr_street_city_logos.ps1.

use crate::{arg, Res};
use serde_json::{json, Value};

#[cfg(windows)]
pub fn ocr(args: &[String]) -> Res<Value> {
    use windows::core::HSTRING;
    use windows::Globalization::Language;
    use windows::Graphics::Imaging::BitmapDecoder;
    use windows::Media::Ocr::OcrEngine;
    use windows::Storage::{FileAccessMode, StorageFile};

    let request = arg(args, 0, "request.json")?;
    let output = arg(args, 1, "output.json")?;
    let langs: Vec<String> = crate::opt(args, "--lang").unwrap_or("zh-Hant-TW,zh-Hans-CN").split(',').map(str::to_string).collect();
    let e = |what: &str, err: windows::core::Error| format!("{what}: {err}");
    let mut engines = Vec::new();
    for l in &langs {
        let lang = Language::CreateLanguage(&HSTRING::from(l.as_str())).map_err(|x| e(l, x))?;
        let engine = OcrEngine::TryCreateFromLanguage(&lang).map_err(|_| format!("no OCR engine for {l} (install the Windows language OCR feature)"))?;
        engines.push((l.clone(), engine));
    }
    let text = std::fs::read_to_string(request).map_err(|x| format!("{request}: {x}"))?;
    let rows: Value = serde_json::from_str(text.trim_start_matches('\u{feff}')).map_err(|x| format!("{request}: {x}"))?;
    let rows = match rows { Value::Array(a) => a, v => vec![v] };
    let mut out = Vec::new();
    for row in &rows {
        let id = row.get("id").cloned().unwrap_or(Value::Null);
        let path = row.get("path").and_then(Value::as_str).ok_or("request row without path")?;
        // StorageFile wants a full Windows path.
        let full = std::fs::canonicalize(path).map_err(|x| format!("{path}: {x}"))?;
        let full = full.to_string_lossy().trim_start_matches(r"\\?\").to_string();
        let file = StorageFile::GetFileFromPathAsync(&HSTRING::from(full.as_str())).and_then(|o| o.join()).map_err(|x| e(path, x))?;
        let stream = file.OpenAsync(FileAccessMode::Read).and_then(|o| o.join()).map_err(|x| e(path, x))?;
        let decoder = BitmapDecoder::CreateAsync(&stream).and_then(|o| o.join()).map_err(|x| e(path, x))?;
        let bitmap = decoder.GetSoftwareBitmapAsync().and_then(|o| o.join()).map_err(|x| e(path, x))?;
        let mut texts = serde_json::Map::new();
        for (l, engine) in &engines {
            let r = engine.RecognizeAsync(&bitmap).and_then(|o| o.join()).map_err(|x| e(path, x))?;
            texts.insert(l.clone(), json!(r.Text().map_err(|x| e(path, x))?.to_string()));
        }
        let _ = bitmap.Close();
        let _ = stream.Close();
        out.push(json!({ "id": id, "path": path, "texts": texts }));
    }
    std::fs::write(output, serde_json::to_string_pretty(&out).map_err(|x| x.to_string())?).map_err(|x| format!("{output}: {x}"))?;
    Ok(json!({ "rows": out.len() }))
}

#[cfg(not(windows))]
pub fn ocr(_: &[String]) -> Res<Value> { Err("ocr is Windows only".into()) }
