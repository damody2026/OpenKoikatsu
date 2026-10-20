//! Per-transaction wall timings, kept on the source worker and returned as
//! structured data. No logging, file I/O, or cross-thread lock in these scopes.
use std::{cell::RefCell,collections::BTreeMap,time::Instant};
use serde::Serialize;

#[derive(Clone,Default,Serialize)]
pub(crate) struct Stage { pub calls:usize,pub total_ms:f64,pub max_ms:f64 }
pub(crate) type Stages=BTreeMap<&'static str,Stage>;
thread_local! { static CURRENT:RefCell<Option<Stages>>=const{RefCell::new(None)}; }
pub(crate) struct Capture { previous:Option<Stages>,finished:bool }
impl Capture {
    pub fn begin()->Self {
        let previous=CURRENT.with(|current|current.replace(Some(Stages::new())));
        Self{previous,finished:false}
    }
    pub fn finish(mut self)->Stages {
        let stages=CURRENT.with(|current|current.replace(self.previous.take())).unwrap_or_default();
        self.finished=true;stages
    }
}
impl Drop for Capture {
    fn drop(&mut self){if !self.finished{CURRENT.with(|current|{current.replace(self.previous.take());});}}
}
pub(crate) fn measure<T>(name:&'static str,f:impl FnOnce()->T)->T {
    if !CURRENT.with(|current|current.borrow().is_some()){return f();}
    let started=Instant::now();let result=f();let ms=started.elapsed().as_secs_f64()*1000.;
    CURRENT.with(|current|{if let Some(stages)=current.borrow_mut().as_mut(){
        let stage=stages.entry(name).or_default();stage.calls+=1;stage.total_ms+=ms;stage.max_ms=stage.max_ms.max(ms);
    }});result
}

#[cfg(test)]mod tests {
    use super::*;
    #[test]fn transaction_timings_restore_nested_and_panicking_owners(){
        let outer=Capture::begin();measure("outer",||());
        let inner=Capture::begin();measure("inner",||());
        assert_eq!(inner.finish()["inner"].calls,1);
        assert!(std::panic::catch_unwind(||{let _failed=Capture::begin();measure("failed",||());panic!("fixture");}).is_err());
        std::thread::spawn(||{measure("other_thread",||());}).join().unwrap();
        measure("outer",||());let result=outer.finish();
        assert_eq!(result.len(),1);assert_eq!(result["outer"].calls,2);
        assert!(CURRENT.with(|current|current.borrow().is_none()));
    }
}
