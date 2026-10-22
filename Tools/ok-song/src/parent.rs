//! `serve --parent-pid N`: exit when the game that started this server exits, so a closed game never leaves
//! the server (and its microphone / GPU memory) running. Without the option the server runs until killed.

#[cfg(windows)]
pub fn watch(pid: u32) {
    use std::ffi::c_void;
    extern "system" {
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
        fn WaitForSingleObject(handle: *mut c_void, millis: u32) -> u32;
    }
    std::thread::spawn(move || unsafe {
        const SYNCHRONIZE: u32 = 0x0010_0000;
        let handle = OpenProcess(SYNCHRONIZE, 0, pid);
        if handle.is_null() {
            crate::log!("parent process {pid} is already gone; exiting");
            std::process::exit(0);
        }
        WaitForSingleObject(handle, 0xFFFF_FFFF);
        crate::log!("parent process {pid} exited; shutting down");
        std::process::exit(0);
    });
}

#[cfg(not(windows))]
pub fn watch(_pid: u32) {}
