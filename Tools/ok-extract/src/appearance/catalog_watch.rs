//! Nonblocking directory change notifications invalidate the in-memory catalogue.
//! Unsupported or unavailable watchers conservatively require a fresh signature.
use std::path::Path;

#[cfg(windows)]
mod platform {
 use super::*;
 use std::os::windows::ffi::OsStrExt;
 #[link(name="kernel32")]
 extern "system" {
  fn FindFirstChangeNotificationW(path:*const u16,subtree:i32,filter:u32)->isize;
  fn FindNextChangeNotification(handle:isize)->i32;
  fn FindCloseChangeNotification(handle:isize)->i32;
  fn WaitForSingleObject(handle:isize,timeout:u32)->u32;
 }
 pub(super) struct Watch {handles:Vec<isize>,healthy:bool}
 impl Drop for Watch {fn drop(&mut self){for h in &self.handles{unsafe{FindCloseChangeNotification(*h);}}}}
 impl Watch {
  pub(super) fn for_directories(directories:&[std::path::PathBuf])->Self {
   let mut watch=Self{handles:Vec::new(),healthy:true};
   for directory in directories {
    let path:Vec<u16>=directory.as_os_str().encode_wide().chain(Some(0)).collect();
    let handle=unsafe{FindFirstChangeNotificationW(path.as_ptr(),1,0x1|0x2|0x4|0x8|0x10|0x100)};
    if handle == -1 {watch.healthy=false;}else{watch.handles.push(handle);}
   }watch
  }
  pub(super) fn changed(&mut self)->bool {
   if !self.healthy{return true;}
   let mut changed=false;
   for handle in &self.handles {
    match unsafe{WaitForSingleObject(*handle,0)} {
     258=>{}, // WAIT_TIMEOUT: no change; never block Unreal's worker.
     0=>{changed=true;if unsafe{FindNextChangeNotification(*handle)}==0{self.healthy=false;}},
     _=>{changed=true;self.healthy=false;}
    }
   }changed
  }
  pub(super) fn available(&self)->bool{self.healthy}
 }
}
#[cfg(not(windows))]
mod platform {
 use super::*;
 pub(super) struct Watch;
 impl Watch {
  pub(super) fn for_directories(_: &[std::path::PathBuf])->Self{Self}
  pub(super) fn changed(&mut self)->bool{true}
  pub(super) fn available(&self)->bool{false}
 }
}
pub(crate) struct Watch(platform::Watch);
impl Watch {
 pub(crate) fn new(root:&Path)->Self{Self(platform::Watch::for_directories(&[root.join("abdata"),root.join("mods")]))}
 pub(crate) fn for_directory(directory:&Path)->Self{Self(platform::Watch::for_directories(&[directory.to_owned()]))}
 pub(crate) fn changed(&mut self)->bool{self.0.changed()}
 pub(crate) fn available(&self)->bool{self.0.available()}
}
#[cfg(all(test,windows))]
#[test]
fn recursive_watch_detects_asset_changes_without_timed_cache_expiry(){
 let id=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
 let root=std::env::temp_dir().join(format!("ok-catalog-watch-{id}"));
 std::fs::create_dir_all(root.join("abdata/list")).unwrap();std::fs::create_dir_all(root.join("mods")).unwrap();
 let mut watch=Watch::new(&root);assert!(watch.available());assert!(!watch.changed());
 std::fs::write(root.join("abdata/list/test.unity3d"),[1,2,3]).unwrap();
 let start=std::time::Instant::now();while !watch.changed(){assert!(start.elapsed()<std::time::Duration::from_secs(1));std::thread::sleep(std::time::Duration::from_millis(1));}
 std::fs::write(root.join("mods/test.zipmod"),[4,5,6]).unwrap();
 let start=std::time::Instant::now();while !watch.changed(){assert!(start.elapsed()<std::time::Duration::from_secs(1));std::thread::sleep(std::time::Duration::from_millis(1));}
 let mut unavailable=Watch::new(&root.join("missing"));assert!(!unavailable.available());assert!(unavailable.changed());
}
