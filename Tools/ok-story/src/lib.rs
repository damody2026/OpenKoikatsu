//! ok_story.dll: the galgame story director (include/ok_story.h). Nothing here may unwind into Unreal.
pub mod camera;
pub mod director;
pub mod io;
pub mod life;
pub mod math;
pub mod portrait;
pub mod progress;
pub mod rng;
pub mod script;
pub mod text;
pub mod tuning;

use director::{Config, Director};
use std::cell::RefCell;
use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub const ABI_VERSION: u32 = 1;

thread_local! {
    static LAST_ERROR: RefCell<Option<String>> = const { RefCell::new(None) };
}

fn set_error(e: String) {
    LAST_ERROR.with(|l| *l.borrow_mut() = Some(e));
}

fn give(s: String) -> *const c_char {
    CString::new(s.replace('\0', " ")).map(|c| c.into_raw() as *const c_char).unwrap_or(std::ptr::null())
}

unsafe fn text<'a>(p: *const c_char) -> Result<&'a str, String> {
    if p.is_null() {
        return Err("null string".into());
    }
    CStr::from_ptr(p).to_str().map_err(|e| format!("not UTF-8: {e}"))
}

/// Runs `f`, turning errors and panics into the last error and `fail`.
fn guard<T>(fail: T, f: impl FnOnce() -> Result<T, String>) -> T {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(v)) => v,
        Ok(Err(e)) => {
            set_error(e);
            fail
        }
        Err(p) => {
            let msg = p.downcast_ref::<String>().cloned().or_else(|| p.downcast_ref::<&str>().map(|s| s.to_string())).unwrap_or_default();
            set_error(format!("ok_story panicked: {msg}"));
            fail
        }
    }
}

unsafe fn director<'a>(h: *mut Director) -> Result<&'a mut Director, String> {
    h.as_mut().ok_or_else(|| "null handle".to_string())
}

#[no_mangle]
pub extern "C" fn oks_abi_version() -> u32 {
    ABI_VERSION
}

/// # Safety
/// `config_json` is a NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn oks_open(config_json: *const c_char) -> *mut Director {
    guard(std::ptr::null_mut(), || {
        let cfg: Config = serde_json::from_str(text(config_json)?).map_err(|e| format!("config: {e}"))?;
        Ok(Box::into_raw(Box::new(Director::open(cfg)?)))
    })
}

/// # Safety
/// `h` comes from oks_open; `input_json` is a NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn oks_input(h: *mut Director, input_json: *const c_char) -> i32 {
    guard(0, || {
        let d = director(h)?;
        let i = serde_json::from_str(text(input_json)?).map_err(|e| format!("input: {e}"))?;
        d.input(i);
        Ok(1)
    })
}

/// # Safety
/// `h` comes from oks_open; `facts_json` is a NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn oks_tick(h: *mut Director, facts_json: *const c_char) -> *const c_char {
    guard(std::ptr::null(), || {
        let d = director(h)?;
        let facts = serde_json::from_str(text(facts_json)?).map_err(|e| format!("facts: {e}"))?;
        let frame = d.tick(facts);
        Ok(give(serde_json::to_string(&frame).map_err(|e| e.to_string())?))
    })
}

/// # Safety
/// `h` comes from oks_open; `result_json` is a NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn oks_ack(h: *mut Director, result_json: *const c_char) -> i32 {
    guard(0, || {
        let d = director(h)?;
        let a = serde_json::from_str(text(result_json)?).map_err(|e| format!("ack: {e}"))?;
        Ok(d.ack(a) as i32)
    })
}

/// # Safety
/// `h` comes from oks_open; `query_json` is a NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn oks_query(h: *mut Director, query_json: *const c_char) -> *const c_char {
    guard(std::ptr::null(), || {
        let d = director(h)?;
        let q: serde_json::Value = serde_json::from_str(text(query_json)?).map_err(|e| format!("query: {e}"))?;
        Ok(give(d.query(&q).to_string()))
    })
}

/// # Safety
/// `h` comes from oks_open.
#[no_mangle]
pub unsafe extern "C" fn oks_export(h: *mut Director) -> *const c_char {
    guard(std::ptr::null(), || Ok(give(director(h)?.export().to_string())))
}

/// # Safety
/// `h` comes from oks_open; `state_json` is a NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn oks_import(h: *mut Director, state_json: *const c_char) -> i32 {
    guard(0, || {
        let d = director(h)?;
        let v: serde_json::Value = serde_json::from_str(text(state_json)?).map_err(|e| format!("import: {e}"))?;
        d.import(&v)?;
        Ok(1)
    })
}

/// # Safety
/// `h` comes from oks_open.
#[no_mangle]
pub unsafe extern "C" fn oks_reload(h: *mut Director) -> i32 {
    guard(0, || {
        director(h)?.reload()?;
        Ok(1)
    })
}

/// # Safety
/// `p` is NULL or a string returned by this library.
#[no_mangle]
pub unsafe extern "C" fn oks_free(p: *const c_char) {
    if !p.is_null() {
        drop(CString::from_raw(p as *mut c_char));
    }
}

#[no_mangle]
pub extern "C" fn oks_last_error() -> *const c_char {
    LAST_ERROR.with(|l| l.borrow_mut().take()).map(give).unwrap_or(std::ptr::null())
}

/// # Safety
/// `h` comes from oks_open and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn oks_close(h: *mut Director) {
    if !h.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(h))));
    }
}
