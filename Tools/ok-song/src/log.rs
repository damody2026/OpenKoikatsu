//! Server log: every line goes to stderr and is appended, timestamped, to `<exe dir>/logs/<name>.log`, so a
//! server that stopped can be diagnosed afterwards. Panics are written there too (with a backtrace). The file
//! is renamed to `<name>.old.log` when it passes 5 MB.
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

static FILE: OnceLock<Mutex<File>> = OnceLock::new();
const ROTATE_BYTES: u64 = 5 * 1024 * 1024;

/// Opens the log next to the executable and installs the panic hook. Returns the log path.
pub fn init(name: &str) -> Option<PathBuf> {
    let dir = std::env::current_exe().ok()?.parent()?.join("logs");
    std::fs::create_dir_all(&dir).ok()?;
    let path = dir.join(format!("{name}.log"));
    if std::fs::metadata(&path).map(|m| m.len() > ROTATE_BYTES).unwrap_or(false) {
        let _ = std::fs::rename(&path, dir.join(format!("{name}.old.log")));
    }
    let file = OpenOptions::new().create(true).append(true).open(&path).ok()?;
    let _ = FILE.set(Mutex::new(file));
    let default_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        write(&format!("PANIC {info}\n{}", std::backtrace::Backtrace::force_capture()));
        default_hook(info);
    }));
    write(&format!("==== {name} started pid={} args={:?}", std::process::id(), std::env::args().skip(1).collect::<Vec<_>>()));
    Some(path)
}

pub fn write(line: &str) {
    eprintln!("{line}");
    if let Some(file) = FILE.get() {
        if let Ok(mut f) = file.lock() {
            let _ = writeln!(f, "{} {line}", timestamp());
            let _ = f.flush();
        }
    }
}

/// UTC "YYYY-MM-DD HH:MM:SS.mmm" without extra dependencies.
fn timestamp() -> String {
    let d = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    let secs = d.as_secs() as i64;
    let (days, rem) = (secs.div_euclid(86_400), secs.rem_euclid(86_400));
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = yoe + era * 400 + i64::from(month <= 2);
    format!("{year:04}-{month:02}-{day:02} {:02}:{:02}:{:02}.{:03}Z", rem / 3600, rem % 3600 / 60, rem % 60, d.subsec_millis())
}

/// `log!` is `eprintln!` that also lands in the log file.
#[macro_export]
macro_rules! log {
    ($($arg:tt)*) => { $crate::log::write(&format!($($arg)*)) };
}
