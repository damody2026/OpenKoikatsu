//! ok_mind.dll: C ABI over the mind (see include/ok_mind.h). Nothing here may unwind into Unreal.
pub mod config;
pub mod context;
pub mod deepseek;
pub mod finance;
pub mod memory;
pub mod mind;
pub mod profile;
pub mod queue;
pub mod store;
pub mod text;

use mind::Mind;
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

/// Runs an FFI body: errors and panics become `fallback` plus a last-error message.
fn guard<T>(fallback: T, body: impl FnOnce() -> Result<T, String>) -> T {
    LAST_ERROR.with(|l| *l.borrow_mut() = None);
    match catch_unwind(AssertUnwindSafe(body)) {
        Ok(Ok(v)) => v,
        Ok(Err(e)) => {
            set_error(e);
            fallback
        }
        Err(p) => {
            let text = p
                .downcast_ref::<&str>()
                .map(|s| s.to_string())
                .or_else(|| p.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "unknown panic".into());
            set_error(format!("panic: {text}"));
            fallback
        }
    }
}

unsafe fn text_arg<'a>(p: *const c_char) -> Result<&'a str, String> {
    if p.is_null() {
        return Err("null_argument".into());
    }
    CStr::from_ptr(p).to_str().map_err(|_| "argument_not_utf8".to_string())
}

fn out(text: String) -> *const c_char {
    // JSON escapes control characters, so an interior NUL can only come from a bug; drop it rather than fail.
    let text = if text.contains('\0') { text.replace('\0', "") } else { text };
    CString::new(text).map(|c| c.into_raw() as *const c_char).unwrap_or(std::ptr::null())
}

/// Opaque to C.
pub struct OkmHandle {
    mind: Mind,
}

unsafe fn handle<'a>(h: *mut OkmHandle) -> Result<&'a OkmHandle, String> {
    h.as_ref().ok_or_else(|| "null_handle".to_string())
}

#[no_mangle]
pub extern "C" fn okm_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub unsafe extern "C" fn okm_open(config_json: *const c_char) -> *mut OkmHandle {
    guard(std::ptr::null_mut(), || {
        let cfg = config::Config::parse(text_arg(config_json)?)?;
        let mind = Mind::open(cfg)?;
        Ok(Box::into_raw(Box::new(OkmHandle { mind })))
    })
}

#[no_mangle]
pub unsafe extern "C" fn okm_submit(h: *mut OkmHandle, job_json: *const c_char) -> u64 {
    guard(0, || handle(h)?.mind.submit(text_arg(job_json)?))
}

#[no_mangle]
pub unsafe extern "C" fn okm_query(h: *mut OkmHandle, query_json: *const c_char) -> *const c_char {
    guard(std::ptr::null(), || Ok(out(handle(h)?.mind.query(text_arg(query_json)?)?)))
}

#[no_mangle]
pub unsafe extern "C" fn okm_poll(h: *mut OkmHandle) -> *const c_char {
    guard(std::ptr::null(), || Ok(handle(h)?.mind.poll().map(out).unwrap_or(std::ptr::null())))
}

#[no_mangle]
pub unsafe extern "C" fn okm_free(text: *const c_char) {
    if !text.is_null() {
        let _ = catch_unwind(|| drop(CString::from_raw(text as *mut c_char)));
    }
}

#[no_mangle]
pub extern "C" fn okm_last_error() -> *const c_char {
    LAST_ERROR.with(|l| l.borrow().clone()).map(out).unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn okm_save(h: *mut OkmHandle) -> i32 {
    guard(0, || handle(h)?.mind.save().map(|_| 1))
}

#[no_mangle]
pub unsafe extern "C" fn okm_close(h: *mut OkmHandle) {
    if h.is_null() {
        return;
    }
    guard((), || {
        let owned = Box::from_raw(h);
        owned.mind.close()
    })
}

#[cfg(test)]
mod tests;
