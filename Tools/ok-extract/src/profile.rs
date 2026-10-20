//! Thread-scoped extraction profile. Background appearance preparation must
//! extract under the canonical portable-export switches, but the process
//! environment is concurrently read (and toggled) by the live renderer.
//! Like `request_clothes_states`, a scope overrides only the current thread;
//! without a scope every read falls through to the process environment.
use std::{cell::RefCell,env::VarError,ffi::OsString};

/// Switches the established portable exporter sets before source extraction.
pub(crate) const PORTABLE_EXPORT:[&str;5]=["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"];

thread_local! {
    static OVERRIDES:RefCell<Vec<(&'static str,Option<&'static str>)>>=const{RefCell::new(Vec::new())};
}

fn lookup(key:&str)->Option<Option<&'static str>>{
    OVERRIDES.with(|o|o.borrow().iter().rev().find(|(k,_)|*k==key).map(|(_,v)|*v))
}
pub(crate) fn var(key:&str)->Result<String,VarError>{
    match lookup(key){Some(Some(v))=>Ok(v.to_owned()),Some(None)=>Err(VarError::NotPresent),None=>std::env::var(key)}
}
pub(crate) fn var_os(key:&str)->Option<OsString>{
    match lookup(key){Some(v)=>v.map(OsString::from),None=>std::env::var_os(key)}
}

/// Restores the previous thread view on drop; scopes nest.
pub(crate) struct Scope(usize);
impl Drop for Scope{fn drop(&mut self){OVERRIDES.with(|o|o.borrow_mut().truncate(self.0));}}
pub(crate) fn scope(values:&[(&'static str,Option<&'static str>)])->Scope{
    OVERRIDES.with(|o|{let mut o=o.borrow_mut();let depth=o.len();o.extend_from_slice(values);Scope(depth)})
}
pub(crate) fn portable_export()->Scope{
    scope(&PORTABLE_EXPORT.map(|k|(k,Some("1"))))
}

#[cfg(test)]mod tests{
    use super::*;
    #[test]fn scope_is_thread_local_nested_and_restored(){
        let key="OK_PROFILE_TEST_SWITCH";std::env::remove_var(key);
        assert!(var(key).is_err());
        {
            let _a=scope(&[(key,Some("1"))]);assert_eq!(var(key).unwrap(),"1");
            std::thread::spawn(move||assert!(var(key).is_err(),"other threads keep the process view")).join().unwrap();
            {let _b=scope(&[(key,None)]);assert!(var(key).is_err());assert!(var_os(key).is_none());}
            assert_eq!(var(key).unwrap(),"1");
        }
        assert!(var(key).is_err());
        let _p=portable_export();
        for k in PORTABLE_EXPORT{assert_eq!(var(k).unwrap(),"1");}
        assert!(crate::sqlcache::env_sig().contains("OK_HEAD_GPUSKIN=1"));
    }
}

/// OK_PROFILE: total wall time and call count per named hot spot, printed and reset by `dump_spots` (once per
/// extraction), so nested helpers called from many places show their share without a sampling profiler.
pub(crate) struct Spot(&'static str,Option<std::time::Instant>);
static SPOTS:std::sync::Mutex<Vec<(&'static str,f64,u32)>>=std::sync::Mutex::new(Vec::new());
fn spots_on()->bool{static ON:std::sync::OnceLock<bool>=std::sync::OnceLock::new();*ON.get_or_init(||std::env::var_os("OK_PROFILE").is_some())}
pub(crate) fn spot(name:&'static str)->Spot{Spot(name,spots_on().then(std::time::Instant::now))}
impl Drop for Spot{fn drop(&mut self){
 let Some(start)=self.1 else{return};let ms=start.elapsed().as_secs_f64()*1000.;
 let mut spots=SPOTS.lock().unwrap_or_else(|e|e.into_inner());
 if let Some(row)=spots.iter_mut().find(|r|r.0==self.0){row.1+=ms;row.2+=1;}else{spots.push((self.0,ms,1));}
}}
pub(crate) fn dump_spots(){
 if !spots_on(){return;}
 let mut spots=std::mem::take(&mut *SPOTS.lock().unwrap_or_else(|e|e.into_inner()));
 spots.sort_by(|a,b|b.1.total_cmp(&a.1));
 for (name,ms,n) in spots{eprintln!("ok-extract: SPOT {name:<32} {ms:>9.1}ms x{n}");}
}

/// This thread's switch overrides, to apply on a helper thread doing part of the same extraction.
pub(crate) fn snapshot()->Vec<(&'static str,Option<&'static str>)>{OVERRIDES.with(|o|o.borrow().clone())}
