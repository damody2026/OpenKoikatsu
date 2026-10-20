//! Optional native CPU trace bridge. The callback is registered once before
//! playback and must remain valid for the lifetime of the native module.
use std::sync::atomic::{AtomicUsize,Ordering};
type Callback=extern "C" fn(u32,i32);
static CALLBACK:AtomicUsize=AtomicUsize::new(0);

#[no_mangle]
pub extern "C" fn ok_set_cpu_trace_callback(callback:Option<Callback>)->i32{
    let Some(callback)=callback else{return -1;};
    let pointer=callback as usize;
    match CALLBACK.compare_exchange(0,pointer,Ordering::AcqRel,Ordering::Acquire){
        Ok(_)=>0,Err(existing)if existing==pointer=>0,Err(_)=>-1,
    }
}

pub(crate) struct Scope{callback:Option<Callback>,event:u32}
impl Scope{
    #[inline]
    pub(crate) fn new(event:u32)->Self{
        let pointer=CALLBACK.load(Ordering::Acquire);
        let callback=if pointer==0{None}else{Some(unsafe{std::mem::transmute::<usize,Callback>(pointer)})};
        if let Some(callback)=callback{callback(event,1);}
        Self{callback,event}
    }
}
impl Drop for Scope{
    #[inline]
    fn drop(&mut self){if let Some(callback)=self.callback{callback(self.event,0);}}
}
