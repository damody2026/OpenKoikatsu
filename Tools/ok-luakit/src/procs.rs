//! Running processes for Lua launchers: list by executable name with full image paths, and stop by pid.
//!   proc-list [name ...]      -> [{pid, name, path}]   (names compared case-insensitively, ".exe" optional)
//!   proc-kill <pid>           -> {killed: bool}
//!   proc-times                -> [{pid, name, cpu_seconds}]  (Get-Process CPU: user+kernel; name without ".exe";
//!                                processes that cannot be opened are skipped)
//!   proc-wait <pid> <ms>      -> {exists, exited, path}  (WaitForExit(ms) on a process this caller did not start)
//!   proc-priority <pid> [class] -> {priority}  (Idle|BelowNormal|Normal|AboveNormal|High|RealTime; sets when given)
//!   proc-affinity <pid> [mask]  -> {mask}      (process affinity mask; sets when given)
//!   cpu-cache                 -> [{CacheBytes, Mask, Group, LogicalProcessors}]  (L3 caches with one group mask)
use crate::{arg, Res};
use serde_json::{json, Value};

#[cfg(windows)]
mod win {
    #[repr(C)]
    pub struct ProcessEntry32W {
        pub size: u32,
        pub usage: u32,
        pub pid: u32,
        pub heap: usize,
        pub module: u32,
        pub threads: u32,
        pub parent: u32,
        pub priority: i32,
        pub flags: u32,
        pub exe: [u16; 260],
    }
    pub const TH32CS_SNAPPROCESS: u32 = 2;
    pub const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    pub const PROCESS_TERMINATE: u32 = 0x0001;
    pub const INVALID_HANDLE: isize = -1;
    #[link(name = "kernel32")]
    extern "system" {
        pub fn CreateToolhelp32Snapshot(flags: u32, pid: u32) -> isize;
        pub fn Process32FirstW(snap: isize, entry: *mut ProcessEntry32W) -> i32;
        pub fn Process32NextW(snap: isize, entry: *mut ProcessEntry32W) -> i32;
        pub fn OpenProcess(access: u32, inherit: i32, pid: u32) -> isize;
        pub fn QueryFullProcessImageNameW(process: isize, flags: u32, name: *mut u16, size: *mut u32) -> i32;
        pub fn TerminateProcess(process: isize, code: u32) -> i32;
        pub fn CloseHandle(handle: isize) -> i32;
    }
}

#[cfg(windows)]
fn image_path(pid: u32) -> Option<String> {
    unsafe {
        let h = win::OpenProcess(win::PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
        if h == 0 { return None; }
        let mut buf = vec![0u16; 32768];
        let mut len = buf.len() as u32;
        let ok = win::QueryFullProcessImageNameW(h, 0, buf.as_mut_ptr(), &mut len);
        win::CloseHandle(h);
        if ok == 0 { return None; }
        Some(String::from_utf16_lossy(&buf[..len as usize]))
    }
}

#[cfg(windows)]
pub fn list(args: &[String]) -> Res<Value> {
    let wanted: Vec<String> = args.iter().map(|n| n.to_lowercase().trim_end_matches(".exe").to_string()).collect();
    let mut out = Vec::new();
    unsafe {
        let snap = win::CreateToolhelp32Snapshot(win::TH32CS_SNAPPROCESS, 0);
        if snap == win::INVALID_HANDLE { return Err(format!("process snapshot: {}", std::io::Error::last_os_error())); }
        let mut e: win::ProcessEntry32W = std::mem::zeroed();
        e.size = std::mem::size_of::<win::ProcessEntry32W>() as u32;
        let mut more = win::Process32FirstW(snap, &mut e) != 0;
        while more {
            let end = e.exe.iter().position(|&c| c == 0).unwrap_or(e.exe.len());
            let name = String::from_utf16_lossy(&e.exe[..end]);
            let stem = name.to_lowercase().trim_end_matches(".exe").to_string();
            if wanted.is_empty() || wanted.contains(&stem) {
                out.push(json!({ "pid": e.pid, "name": name, "path": image_path(e.pid) }));
            }
            more = win::Process32NextW(snap, &mut e) != 0;
        }
        win::CloseHandle(snap);
    }
    Ok(Value::Array(out))
}

#[cfg(windows)]
pub fn kill(args: &[String]) -> Res<Value> {
    let pid: u32 = arg(args, 0, "pid")?.parse().map_err(|_| "pid must be a number".to_string())?;
    unsafe {
        let h = win::OpenProcess(win::PROCESS_TERMINATE, 0, pid);
        if h == 0 { return Ok(json!({ "killed": false, "message": std::io::Error::last_os_error().to_string() })); }
        let ok = win::TerminateProcess(h, 1) != 0;
        win::CloseHandle(h);
        Ok(json!({ "killed": ok }))
    }
}

#[cfg(not(windows))]
pub fn list(_: &[String]) -> Res<Value> { Err("proc-list is Windows only".into()) }
#[cfg(not(windows))]
pub fn kill(_: &[String]) -> Res<Value> { Err("proc-kill is Windows only".into()) }

#[cfg(windows)]
mod win2 {
    pub const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    pub const PROCESS_SET_INFORMATION: u32 = 0x0200;
    pub const SYNCHRONIZE: u32 = 0x0010_0000;
    #[link(name = "kernel32")]
    extern "system" {
        pub fn OpenProcess(access: u32, inherit: i32, pid: u32) -> isize;
        pub fn CloseHandle(handle: isize) -> i32;
        pub fn GetProcessTimes(p: isize, c: *mut u64, e: *mut u64, k: *mut u64, u: *mut u64) -> i32;
        pub fn GetPriorityClass(p: isize) -> u32;
        pub fn SetPriorityClass(p: isize, class: u32) -> i32;
        pub fn GetProcessAffinityMask(p: isize, mask: *mut usize, system: *mut usize) -> i32;
        pub fn SetProcessAffinityMask(p: isize, mask: usize) -> i32;
        pub fn WaitForSingleObject(h: isize, ms: u32) -> u32;
        pub fn GetLogicalProcessorInformationEx(relation: i32, buffer: *mut u8, length: *mut u32) -> i32;
    }
}

#[cfg(windows)]
struct Handle(isize);
#[cfg(windows)]
impl Drop for Handle {
    fn drop(&mut self) { unsafe { win2::CloseHandle(self.0); } }
}
#[cfg(windows)]
fn open(pid: u32, access: u32) -> Res<Handle> {
    let h = unsafe { win2::OpenProcess(access, 0, pid) };
    if h == 0 { return Err(format!("open process {pid}: {}", std::io::Error::last_os_error())); }
    Ok(Handle(h))
}
fn pid_arg(args: &[String]) -> Res<u32> {
    arg(args, 0, "pid")?.parse().map_err(|_| "pid must be a number".to_string())
}

#[cfg(windows)]
pub fn times(_: &[String]) -> Res<Value> {
    let mut out = Vec::new();
    for p in list(&[])?.as_array().cloned().unwrap_or_default() {
        let pid = p["pid"].as_u64().unwrap_or(0) as u32;
        let Ok(h) = open(pid, win2::PROCESS_QUERY_LIMITED_INFORMATION) else { continue };
        let (mut c, mut e, mut k, mut u) = (0u64, 0u64, 0u64, 0u64);
        if unsafe { win2::GetProcessTimes(h.0, &mut c, &mut e, &mut k, &mut u) } == 0 { continue; }
        let name = p["name"].as_str().unwrap_or("").to_string();
        let stem = if name.to_lowercase().ends_with(".exe") { name[..name.len() - 4].to_string() } else { name };
        out.push(json!({ "pid": pid, "name": stem, "cpu_seconds": (k + u) as f64 / 1e7 }));
    }
    Ok(Value::Array(out))
}

#[cfg(windows)]
pub fn wait(args: &[String]) -> Res<Value> {
    let pid = pid_arg(args)?;
    let ms: u32 = arg(args, 1, "ms")?.parse().map_err(|_| "ms must be a number".to_string())?;
    let Ok(h) = open(pid, win2::SYNCHRONIZE | win2::PROCESS_QUERY_LIMITED_INFORMATION) else {
        return Ok(json!({ "exists": false, "exited": true, "path": null }));
    };
    let path = image_path(pid);
    let exited = unsafe { win2::WaitForSingleObject(h.0, ms) } == 0;
    Ok(json!({ "exists": true, "exited": exited, "path": path }))
}

const CLASSES: [(&str, u32); 6] = [("Idle", 0x40), ("BelowNormal", 0x4000), ("Normal", 0x20), ("AboveNormal", 0x8000), ("High", 0x80), ("RealTime", 0x100)];

#[cfg(windows)]
pub fn priority(args: &[String]) -> Res<Value> {
    let pid = pid_arg(args)?;
    let h = open(pid, win2::PROCESS_QUERY_LIMITED_INFORMATION | win2::PROCESS_SET_INFORMATION)?;
    if let Some(want) = args.get(1) {
        let class = CLASSES.iter().find(|(n, _)| n.eq_ignore_ascii_case(want)).ok_or_else(|| format!("unknown priority class {want}"))?.1;
        if unsafe { win2::SetPriorityClass(h.0, class) } == 0 { return Err(format!("set priority: {}", std::io::Error::last_os_error())); }
    }
    let cur = unsafe { win2::GetPriorityClass(h.0) };
    if cur == 0 { return Err(format!("get priority: {}", std::io::Error::last_os_error())); }
    let name = CLASSES.iter().find(|(_, c)| *c == cur).map(|(n, _)| n.to_string()).unwrap_or_else(|| format!("0x{cur:x}"));
    Ok(json!({ "priority": name }))
}

#[cfg(windows)]
pub fn affinity(args: &[String]) -> Res<Value> {
    let pid = pid_arg(args)?;
    let h = open(pid, win2::PROCESS_QUERY_LIMITED_INFORMATION | win2::PROCESS_SET_INFORMATION)?;
    if let Some(want) = args.get(1) {
        let mask: u64 = want.parse().map_err(|_| "mask must be a number".to_string())?;
        if unsafe { win2::SetProcessAffinityMask(h.0, mask as usize) } == 0 { return Err(format!("set affinity: {}", std::io::Error::last_os_error())); }
    }
    let (mut m, mut s) = (0usize, 0usize);
    if unsafe { win2::GetProcessAffinityMask(h.0, &mut m, &mut s) } == 0 { return Err(format!("get affinity: {}", std::io::Error::last_os_error())); }
    Ok(json!({ "mask": m as u64 }))
}

/// Same record walk as the former get_crowd_cpu_cache.ps1 (Windows SDK CACHE_RELATIONSHIP:
/// header 8, Level at 8, CacheSize at 12, GroupCount at 38, GroupMask at 40, Group at 48).
#[cfg(windows)]
pub fn cpu_cache(_: &[String]) -> Res<Value> {
    let mut len = 0u32;
    unsafe { win2::GetLogicalProcessorInformationEx(2, std::ptr::null_mut(), &mut len) };
    if len == 0 || std::io::Error::last_os_error().raw_os_error() != Some(122) { return Err("Cache topology query failed".into()); }
    let mut buf = vec![0u8; len as usize];
    if unsafe { win2::GetLogicalProcessorInformationEx(2, buf.as_mut_ptr(), &mut len) } == 0 { return Err("Cache topology read failed".into()); }
    let rd32 = |o: usize| u32::from_le_bytes(buf[o..o + 4].try_into().unwrap());
    let rd16 = |o: usize| u16::from_le_bytes(buf[o..o + 2].try_into().unwrap());
    let mut out = Vec::new();
    let mut off = 0usize;
    while off < len as usize {
        let size = rd32(off + 4) as usize;
        if size < 8 || off + size > len as usize { return Err("Invalid cache record size".into()); }
        if size >= 56 && rd32(off) == 2 && buf[off + 8] == 3 && rd16(off + 38) == 1 {
            let mask = u64::from_le_bytes(buf[off + 40..off + 48].try_into().unwrap());
            out.push(json!({ "CacheBytes": rd32(off + 12), "Mask": mask, "Group": rd16(off + 48), "LogicalProcessors": mask.count_ones() }));
        }
        off += size;
    }
    Ok(Value::Array(out))
}

#[cfg(not(windows))]
pub fn times(_: &[String]) -> Res<Value> { Err("proc-times is Windows only".into()) }
#[cfg(not(windows))]
pub fn wait(_: &[String]) -> Res<Value> { Err("proc-wait is Windows only".into()) }
#[cfg(not(windows))]
pub fn priority(_: &[String]) -> Res<Value> { Err("proc-priority is Windows only".into()) }
#[cfg(not(windows))]
pub fn affinity(_: &[String]) -> Res<Value> { Err("proc-affinity is Windows only".into()) }
#[cfg(not(windows))]
pub fn cpu_cache(_: &[String]) -> Res<Value> { Err("cpu-cache is Windows only".into()) }
