//! ok_rules.dll: C ABI over the StreetSocial rules (see include/ok_rules.h). Nothing here may unwind into Unreal.
//! The rules keep no game state: every call carries the facts it needs, so the DLL can be swapped while the game runs.
pub mod academics;
pub mod activities;
pub mod motions;
pub mod bank;
pub mod bias;
pub mod bluff;
pub mod icu;
pub mod items;
pub mod locale;
pub mod region;
pub mod tr;
pub mod bonds;
pub mod calendar;
pub mod campus;
pub mod catalog;
pub mod contacts;
pub mod day;
pub mod effects;
pub mod errand;
pub mod estate;
pub mod events;
pub mod games;
pub mod heroine;
pub mod invite;
pub mod mind;
pub mod needs;
pub mod photos;
pub mod places;
pub mod reading;
pub mod plan;
pub mod promise;
pub mod property;
pub mod relation;
pub mod rng;
pub mod series;
pub mod shop;
pub mod social;
pub mod song;
pub mod talk;
pub mod traits;
pub mod venture;
pub mod weather;
pub mod work;
pub mod tuning;

use serde_json::{json, Value};
use std::cell::RefCell;
use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::RwLock;
use tuning::Tuning;

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

/// The rules with their tuning; what the C handle points at. Usable directly from Rust (tests, tools).
pub struct Rules {
    tuning_dir: Option<PathBuf>,
    tuning: RwLock<Tuning>,
    /// The dialogue catalogues (talk.rs), once Unreal named them (`talk_load`); read again on reload.
    talk: RwLock<Option<talk::TalkData>>,
}

impl Rules {
    pub fn open(config_json: &str) -> Result<Rules, String> {
        let config: Value = serde_json::from_str(config_json).map_err(|e| format!("config: {e}"))?;
        let tuning_dir = config.get("tuning_dir").and_then(Value::as_str).filter(|s| !s.is_empty()).map(PathBuf::from);
        let tuning = Tuning::load(tuning_dir.as_deref())?;
        tr::install(&tuning.texts);
        region::install(&tuning.regions);
        events::install(&tuning.events);
        Ok(Rules { tuning_dir, tuning: RwLock::new(tuning), talk: RwLock::new(None) })
    }

    pub fn reload(&self) -> Result<(), String> {
        let fresh = Tuning::load(self.tuning_dir.as_deref())?;
        tr::install(&fresh.texts);
        region::install(&fresh.regions);
        events::install(&fresh.events);
        *self.tuning.write().map_err(|_| "tuning_lock_poisoned")? = fresh;
        let paths = self.talk.read().map_err(|_| "talk_lock_poisoned")?.as_ref().map(|t| t.paths.clone());
        if let Some(paths) = paths {
            let fresh = talk::load(&paths)?;
            *self.talk.write().map_err(|_| "talk_lock_poisoned")? = Some(fresh);
        }
        Ok(())
    }

    pub fn eval(&self, request_json: &str) -> Result<String, String> {
        let mut request: Value = serde_json::from_str(request_json).map_err(|e| format!("request: {e}"))?;
        // The rules' own sentences and money are in the request's language and region (tr.rs, region.rs); the heroines'
        // profiles (yuan) come in in the region's money.
        let _scope = tr::scope(locale::lang(&request), locale::region(&request));
        region::localize_request(&mut request);
        // The activities table's own minutes and needs for the end of one (activities.toml), read by shop.rs.
        if matches!(request.get("rule").or_else(|| request.get("kind")).and_then(Value::as_str), Some("activity_end" | "activity_minutes")) {
            if let Ok(t) = self.tuning.read() {
                activities::table_end(&t.activities, &mut request);
            }
        }
        let request = request;
        // Which rule answers: `rule` (Unreal always sends it), else `kind` for hand-written requests. A request's own data
        // may then use `kind` freely (a rumour's kind, an activity's kind) without changing which rule answers it.
        let kind = request.get("rule").or_else(|| request.get("kind")).and_then(Value::as_str).unwrap_or_default().to_string();
        let tuning = self.tuning.read().map_err(|_| "tuning_lock_poisoned")?;
        let answer = match kind.as_str() {
            "status" => json!({"abi": ABI_VERSION, "version": env!("CARGO_PKG_VERSION"), "tuning": tuning.sources()}),
            "debug_panic" => panic!("debug_panic requested"),
            "talk_load" => {
                let data = talk::load(&request)?;
                let out = json!({"nodes": data.nodes.len(), "branches": data.branches.len(), "skipped": data.skipped, "warnings": data.warnings, "step_up": data.rules.step_up, "beyond": data.rules.beyond, "topics": data.topics.topics.len()});
                *self.talk.write().map_err(|_| "talk_lock_poisoned")? = Some(data);
                out
            }
            _ => {
                let routed = needs::eval(&tuning.needs, &kind, &request)
                    .or_else(|| places::eval(tuning.places_in(locale::lang(&request)), &kind, &request))
                    .or_else(|| heroine::eval(tuning.acts_in(locale::lang(&request)), tuning.places_in(locale::lang(&request)), &kind, &request))
                    .or_else(|| invite::eval(tuning.places_in(locale::lang(&request)), &kind, &request))
                    .or_else(|| mind::eval(&kind, &request))
                    .or_else(|| day::eval(&kind, &request))
                    .or_else(|| bank::eval(&kind, &request))
                    .or_else(|| campus::eval(&kind, &request))
                    .or_else(|| academics::eval(&kind, &request))
                    .or_else(|| social::eval(&kind, &request))
                    .or_else(|| games::eval(&kind, &request))
                    .or_else(|| song::eval(&kind, &request))
                    .or_else(|| (kind == "bluff_step").then(|| bluff::bluff_step(&tuning.texts, &request)))
                    .or_else(|| (kind == "card_table_place").then(|| bluff::card_table_place(tuning.places_in(locale::lang(&request)), &request)))
                    .or_else(|| locale::eval(&tuning.texts, &kind, &request))
                    .or_else(|| venture::eval(&kind, &request))
                    .or_else(|| (kind == "property_data").then(|| Ok(tuning.property_json_in(locale::region(&request), locale::lang(&request)))))
                    .or_else(|| region::eval(&tuning.catalog, &kind, &request))
                    .or_else(|| estate::eval(tuning.property_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| activities::eval(tuning.activities_in(locale::lang(&request)), &tuning.activities, tuning.catalog_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| motions::eval(&tuning.motions, &kind, &request))
                    .or_else(|| photos::eval(&kind, &request))
                    .or_else(|| work::eval(&kind, &request))
                    .or_else(|| series::eval(&kind, &request))
                    .or_else(|| weather::eval(&kind, &request))
                    .or_else(|| errand::eval(&kind, &request))
                    .or_else(|| contacts::eval(&kind, &request))
                    .or_else(|| events::eval(&kind, &request))
                    .or_else(|| reading::eval(tuning.catalog_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| items::eval(tuning.catalog_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| catalog::eval(tuning.catalog_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| promise::eval(tuning.catalog_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| shop::eval(tuning.catalog_in(locale::region(&request), locale::lang(&request)), &kind, &request))
                    .or_else(|| plan::eval(tuning.catalog_in(locale::region(&request), locale::lang(&request)), tuning.places_in(locale::lang(&request)), &kind, &request))
                    .or_else(|| talk::eval(self.talk.read().ok().as_ref().and_then(|t| t.as_ref()), &kind, &request));
                match routed {
                    Some(answer) => answer?,
                    None => {
                        let lang = locale::lang(&request).to_string();
                        bonds::eval(tuning.bonds_in(&lang), &kind, request)?
                    }
                }
            }
        };
        serde_json::to_string(&answer).map_err(|e| e.to_string())
    }
}

/// Opaque to C.
pub struct OkrHandle {
    rules: Rules,
}

unsafe fn handle<'a>(h: *mut OkrHandle) -> Result<&'a OkrHandle, String> {
    h.as_ref().ok_or_else(|| "null_handle".to_string())
}

#[no_mangle]
pub extern "C" fn okr_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub unsafe extern "C" fn okr_open(config_json: *const c_char) -> *mut OkrHandle {
    guard(std::ptr::null_mut(), || {
        let rules = Rules::open(text_arg(config_json)?)?;
        Ok(Box::into_raw(Box::new(OkrHandle { rules })))
    })
}

#[no_mangle]
pub unsafe extern "C" fn okr_eval(h: *mut OkrHandle, request_json: *const c_char) -> *const c_char {
    guard(std::ptr::null(), || Ok(out(handle(h)?.rules.eval(text_arg(request_json)?)?)))
}

#[no_mangle]
pub unsafe extern "C" fn okr_reload(h: *mut OkrHandle) -> i32 {
    guard(0, || handle(h)?.rules.reload().map(|_| 1))
}

#[no_mangle]
pub unsafe extern "C" fn okr_free(text: *const c_char) {
    if !text.is_null() {
        let _ = catch_unwind(|| drop(CString::from_raw(text as *mut c_char)));
    }
}

#[no_mangle]
pub extern "C" fn okr_last_error() -> *const c_char {
    LAST_ERROR.with(|l| l.borrow().clone()).map(out).unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn okr_close(h: *mut OkrHandle) {
    if h.is_null() {
        return;
    }
    guard((), || {
        drop(Box::from_raw(h));
        Ok(())
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rule_names_the_rule_kind_is_data() {
        // A rumour, an activity and an entrust all carry their own `kind`; `rule` still picks the answer.
        let rules = Rules::open("{}").unwrap();
        let rumor: Value = serde_json::from_str(&rules.eval(r#"{"rule":"rumor_make","kind":"treat","text":"x","heat":50,"day":1,"global_mode":"B","near":[],"rumors":[]}"#).unwrap()).unwrap();
        assert_eq!(rumor["rumor"]["kind"], "treat");
        assert!(rumor["rumor"].get("minute").is_none(), "no minute asked, none kept");
        // When it happened that day comes back with it (R3-243).
        let timed: Value = serde_json::from_str(&rules.eval(r#"{"rule":"rumor_make","kind":"treat","text":"x","heat":50,"day":1,"minute":530,"global_mode":"B","near":[],"rumors":[]}"#).unwrap()).unwrap();
        assert_eq!(timed["rumor"]["minute"], 530);
        let act: Value = serde_json::from_str(&rules.eval(r#"{"rule":"activity_end","kind":"pair","code":2,"id":"drink","name":"一起喝飲料","relation":{"id":"a"}}"#).unwrap()).unwrap();
        assert_eq!(act["minutes"], 25);
        let ent: Value = serde_json::from_str(&rules.eval(r#"{"rule":"entrust_start","kind":"help","amount":500,"relation":{"id":"a"}}"#).unwrap()).unwrap();
        assert!(ent.is_object());
    }

    #[test]
    fn ffi_round_trip_and_errors() {
        unsafe {
            let config = CString::new(r#"{"tuning_dir": ""}"#).unwrap();
            let h = okr_open(config.as_ptr());
            assert!(!h.is_null());
            let request = CString::new(r#"{"kind":"status"}"#).unwrap();
            let answer = okr_eval(h, request.as_ptr());
            assert!(!answer.is_null());
            let text = CStr::from_ptr(answer).to_str().unwrap().to_string();
            okr_free(answer);
            assert!(text.contains("\"abi\":1"), "{text}");

            // A panic inside a rule comes back as NULL plus an error, never as an unwind.
            let boom = CString::new(r#"{"kind":"debug_panic"}"#).unwrap();
            assert!(okr_eval(h, boom.as_ptr()).is_null());
            let error = okr_last_error();
            assert!(CStr::from_ptr(error).to_str().unwrap().starts_with("panic:"));
            okr_free(error);

            let unknown = CString::new(r#"{"kind":"nope"}"#).unwrap();
            assert!(okr_eval(h, unknown.as_ptr()).is_null());
            assert_eq!(okr_reload(h), 1);
            okr_close(h);
        }
    }
}
