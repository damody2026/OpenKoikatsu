//! Small file-system and format helpers that the Lua standard library lacks:
//! hard links, file identity, disk space, zip member text and font metrics.
use crate::{arg, opt, Res};
use serde_json::{json, Value};
use std::io::Read;

#[cfg(windows)]
mod win {
    #[repr(C)]
    #[derive(Default)]
    pub struct FileTime { pub low: u32, pub high: u32 }
    #[repr(C)]
    #[derive(Default)]
    pub struct ByHandleFileInformation {
        pub attributes: u32,
        pub creation: FileTime,
        pub access: FileTime,
        pub write: FileTime,
        pub volume_serial: u32,
        pub size_high: u32,
        pub size_low: u32,
        pub links: u32,
        pub index_high: u32,
        pub index_low: u32,
    }
    #[link(name = "kernel32")]
    extern "system" {
        pub fn GetFileInformationByHandle(handle: *mut core::ffi::c_void, info: *mut ByHandleFileInformation) -> i32;
        pub fn GetDiskFreeSpaceExW(path: *const u16, caller: *mut u64, total: *mut u64, free: *mut u64) -> i32;
    }
}

/// hardlink <existing> <new> -> {linked:true} | {linked:false, os_error, message}
/// (never replaces an existing <new>; os_error 183/80 means it already exists).
pub fn hardlink(args: &[String]) -> Res<Value> {
    let src = arg(args, 0, "existing")?;
    let dst = arg(args, 1, "new")?;
    Ok(match std::fs::hard_link(src, dst) {
        Ok(()) => json!({ "linked": true }),
        Err(e) => json!({ "linked": false, "os_error": e.raw_os_error(), "message": e.to_string() }),
    })
}

/// file-id <path> -> {volume, index, links, size, mtime_ns} (Python st_dev/st_ino/st_nlink/st_size/st_mtime_ns).
#[cfg(windows)]
pub fn file_id(args: &[String]) -> Res<Value> {
    use std::os::windows::fs::OpenOptionsExt;
    use std::os::windows::io::AsRawHandle;
    let path = arg(args, 0, "path")?;
    // FILE_FLAG_BACKUP_SEMANTICS also opens directories; share everything.
    let file = std::fs::OpenOptions::new().access_mode(0x80).share_mode(7).custom_flags(0x0200_0000)
        .open(path).map_err(|e| format!("{path}: {e}"))?;
    let mut info = win::ByHandleFileInformation::default();
    if unsafe { win::GetFileInformationByHandle(file.as_raw_handle() as *mut _, &mut info) } == 0 {
        return Err(format!("{path}: {}", std::io::Error::last_os_error()));
    }
    let ft = ((info.write.high as u64) << 32) | info.write.low as u64;
    let mtime_ns = (ft as i128 - 116_444_736_000_000_000i128) * 100;
    Ok(json!({
        "volume": info.volume_serial,
        "index": ((info.index_high as u64) << 32) | info.index_low as u64,
        "links": info.links,
        "size": ((info.size_high as u64) << 32) | info.size_low as u64,
        "mtime_ns": mtime_ns as i64,
    }))
}

/// disk-free <path> -> {total, free, available} like shutil.disk_usage (free = total free bytes).
#[cfg(windows)]
pub fn disk_free(args: &[String]) -> Res<Value> {
    use std::os::windows::ffi::OsStrExt;
    let path = arg(args, 0, "path")?;
    let wide: Vec<u16> = std::ffi::OsStr::new(path).encode_wide().chain(std::iter::once(0)).collect();
    let (mut caller, mut total, mut free) = (0u64, 0u64, 0u64);
    if unsafe { win::GetDiskFreeSpaceExW(wide.as_ptr(), &mut caller, &mut total, &mut free) } == 0 {
        return Err(format!("{path}: {}", std::io::Error::last_os_error()));
    }
    Ok(json!({ "total": total, "free": free, "available": caller }))
}

/// window-noactivate <pid> -> {windows}: restores the process's visible top-level windows without
/// activating them (SW_SHOWNOACTIVATE) and sends them to the bottom of the z-order. A minimized
/// game never becomes ready, so `pair_ctl --background` starts it minimized and calls this.
#[cfg(windows)]
pub fn window_noactivate(args: &[String]) -> Res<Value> {
    use std::ffi::c_void;
    #[link(name = "user32")]
    extern "system" {
        fn EnumWindows(f: extern "system" fn(*mut c_void, isize) -> i32, l: isize) -> i32;
        fn GetWindowThreadProcessId(h: *mut c_void, pid: *mut u32) -> u32;
        fn IsWindowVisible(h: *mut c_void) -> i32;
        fn ShowWindow(h: *mut c_void, cmd: i32) -> i32;
        fn SetWindowPos(h: *mut c_void, after: *mut c_void, x: i32, y: i32, cx: i32, cy: i32, flags: u32) -> i32;
    }
    struct Find { pid: u32, found: Vec<usize> }
    extern "system" fn each(h: *mut c_void, l: isize) -> i32 {
        let find = unsafe { &mut *(l as *mut Find) };
        let mut pid = 0u32;
        unsafe { GetWindowThreadProcessId(h, &mut pid) };
        if pid == find.pid && unsafe { IsWindowVisible(h) } != 0 {
            find.found.push(h as usize);
        }
        1
    }
    let pid: u32 = arg(args, 0, "pid")?.parse().map_err(|_| "pid must be a number".to_string())?;
    let mut find = Find { pid, found: Vec::new() };
    unsafe { EnumWindows(each, &mut find as *mut Find as isize) };
    for &h in &find.found {
        // 4 = SW_SHOWNOACTIVATE; HWND_BOTTOM = 1; SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE = 0x13
        unsafe {
            ShowWindow(h as *mut c_void, 4);
            SetWindowPos(h as *mut c_void, 1 as *mut c_void, 0, 0, 0, 0, 0x13);
        }
    }
    Ok(json!({ "windows": find.found.len() }))
}
#[cfg(not(windows))]
pub fn window_noactivate(_: &[String]) -> Res<Value> { Err("window-noactivate is Windows only".into()) }

#[cfg(not(windows))]
pub fn file_id(_: &[String]) -> Res<Value> { Err("file-id is Windows only".into()) }
#[cfg(not(windows))]
pub fn disk_free(_: &[String]) -> Res<Value> { Err("disk-free is Windows only".into()) }

fn u16le(d: &[u8], i: usize) -> Res<usize> { d.get(i..i + 2).map(|b| u16::from_le_bytes([b[0], b[1]]) as usize).ok_or_else(|| "truncated zip".into()) }
fn u32le(d: &[u8], i: usize) -> Res<usize> { d.get(i..i + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as usize).ok_or_else(|| "truncated zip".into()) }

fn cp437(bytes: &[u8]) -> String {
    const HIGH: &str = "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■\u{a0}";
    let high: Vec<char> = HIGH.chars().collect();
    bytes.iter().map(|&b| if b < 128 { b as char } else { high[(b - 128) as usize] }).collect()
}

fn read_at(file: &mut std::fs::File, offset: u64, len: usize) -> Res<Vec<u8>> {
    use std::io::{Seek, SeekFrom};
    file.seek(SeekFrom::Start(offset)).map_err(|e| e.to_string())?;
    let mut buf = vec![0u8; len];
    file.read_exact(&mut buf).map_err(|_| "truncated zip".to_string())?;
    Ok(buf)
}

/// Central directory walk with seeks (only the directory and selected members are read).
fn zip_members(path: &str, suffix: Option<&str>) -> Res<Value> {
    let mut file = std::fs::File::open(path).map_err(|e| format!("{path}: {e}"))?;
    let size = file.metadata().map_err(|e| e.to_string())?.len();
    let tail_len = size.min(22 + 65535) as usize;
    let tail = read_at(&mut file, size - tail_len as u64, tail_len)?;
    if tail_len < 22 { return Err("File is not a zip file".into()); }
    let eocd = (0..=tail_len - 22).rev().find(|&i| tail[i..i + 4] == *b"PK").ok_or("File is not a zip file")?;
    let count = u16le(&tail, eocd + 10)?;
    let cd_size = u32le(&tail, eocd + 12)?;
    let cd_offset = u32le(&tail, eocd + 16)? as u64;
    let cd = read_at(&mut file, cd_offset, cd_size)?;
    let mut pos = 0usize;
    let mut names = Vec::new();
    let mut texts = Vec::new();
    for _ in 0..count {
        if cd.get(pos..pos + 4) != Some(b"PK") { return Err("Bad magic number for central directory".into()); }
        let flags = u16le(&cd, pos + 8)?;
        let method = u16le(&cd, pos + 10)?;
        let csize = u32le(&cd, pos + 20)?;
        let nlen = u16le(&cd, pos + 28)?;
        let xlen = u16le(&cd, pos + 30)?;
        let clen = u16le(&cd, pos + 32)?;
        let local = u32le(&cd, pos + 42)? as u64;
        let raw = cd.get(pos + 46..pos + 46 + nlen).ok_or("truncated zip")?;
        let name = if flags & 0x800 != 0 { String::from_utf8_lossy(raw).into_owned() } else { cp437(raw) };
        pos += 46 + nlen + xlen + clen;
        if let Some(s) = suffix {
            if name.to_lowercase().ends_with(s) {
                let header = read_at(&mut file, local, 30)?;
                if header[..4] != *b"PK" { return Err("Bad magic number for file header".into()); }
                let start = local + 30 + u16le(&header, 26)? as u64 + u16le(&header, 28)? as u64;
                let comp = read_at(&mut file, start, csize)?;
                let bytes = match method {
                    0 => comp,
                    8 => {
                        let mut out = Vec::new();
                        flate2::read::DeflateDecoder::new(&comp[..]).read_to_end(&mut out).map_err(|e| format!("{name}: {e}"))?;
                        out
                    }
                    m => return Err(format!("compression method {m} not supported: {name}")),
                };
                texts.push(json!({ "name": name, "text": String::from_utf8_lossy(&bytes) }));
            }
        }
        names.push(Value::String(name));
    }
    Ok(json!({ "names": names, "texts": texts }))
}

/// zip-read <zip> [--suffix S] -> {names:[...], texts:[{name, text}]}
/// Member names decode like Python zipfile (UTF-8 flag or cp437); members whose
/// lower-cased name ends with S are inflated and decoded as UTF-8 ("replace").
pub fn zip_read(args: &[String]) -> Res<Value> {
    let path = arg(args, 0, "zip")?;
    let suffix = opt(args, "--suffix").map(|s| s.to_lowercase());
    zip_members(path, suffix.as_deref())
}

/// zip-read-many <list.txt|-> [--suffix S] -> [{zip, names, texts} | {zip, error}]
/// (one zip path per line; per-archive failures are reported, not fatal).
pub fn zip_read_many(args: &[String]) -> Res<Value> {
    let list = String::from_utf8_lossy(&crate::read_input(arg(args, 0, "list")?)?).into_owned();
    let suffix = opt(args, "--suffix").map(|s| s.to_lowercase());
    let mut out = Vec::new();
    for line in list.lines() {
        let path = line.trim_end_matches('\r');
        if path.is_empty() { continue; }
        out.push(match zip_members(path, suffix.as_deref()) {
            Ok(mut v) => { v["zip"] = Value::String(path.to_string()); v }
            Err(e) => json!({ "zip": path, "error": e }),
        });
    }
    Ok(Value::Array(out))
}

/// font-metrics <font> [index] -> {units_per_em, ascent, descent, line_gap, height}
/// (unscaled font units; ab_glyph's PxScale is height px, PIL's size is em px:
/// px_scale = size * height / units_per_em).
pub fn font_metrics(args: &[String]) -> Res<Value> {
    use ab_glyph::Font;
    let path = arg(args, 0, "font")?;
    let index = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(0u32);
    let data = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    let font = ab_glyph::FontVec::try_from_vec_and_index(data, index).map_err(|e| format!("{path}: {e}"))?;
    Ok(json!({
        "units_per_em": font.units_per_em(),
        "ascent": font.ascent_unscaled(),
        "descent": font.descent_unscaled(),
        "line_gap": font.line_gap_unscaled(),
        "height": font.height_unscaled(),
    }))
}

fn pairwise_sum(a: &[f64]) -> f64 {
    // numpy DOUBLE_pairwise_sum (PW_BLOCKSIZE 128, 8 accumulators).
    let n = a.len();
    if n < 8 {
        let mut res = 0.0;
        for v in a { res += *v; }
        res
    } else if n <= 128 {
        let mut r = [0f64; 8];
        r.copy_from_slice(&a[..8]);
        let mut i = 8;
        while i < n - (n % 8) {
            for j in 0..8 { r[j] += a[i + j]; }
            i += 8;
        }
        let mut res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        while i < n { res += a[i]; i += 1; }
        res
    } else {
        let mut n2 = n / 2;
        n2 -= n2 % 8;
        pairwise_sum(&a[..n2]) + pairwise_sum(&a[n2..])
    }
}

/// u8-stats <raw> -> {count, sum, mean, std, min, max}: numpy `arr.mean()` / `arr.std()`
/// of a contiguous uint8 buffer (same float64 pairwise rounding as numpy).
pub fn u8_stats(args: &[String]) -> Res<Value> {
    let data = crate::read_input(arg(args, 0, "raw")?)?;
    if data.is_empty() { return Err("empty buffer".into()); }
    let n = data.len();
    let sum: u64 = data.iter().map(|&v| v as u64).sum();
    let mean = sum as f64 / n as f64;
    let sq: Vec<f64> = data.iter().map(|&v| { let d = v as f64 - mean; d * d }).collect();
    let var = (0.0 + pairwise_sum(&sq)) / n as f64;
    Ok(json!({
        "count": n, "sum": sum, "mean": mean, "std": var.sqrt(),
        "min": data.iter().min(), "max": data.iter().max(),
    }))
}
