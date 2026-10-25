//! Voice first on a shared GPU. A voice line (ok-tts) must start within a fraction of a second; a song (ok-song) can
//! take minutes either way. While any voice process on this machine has a line queued or in synthesis, background GPU
//! work (song generation, separation, voice conversion) waits at its next loop step, so the line gets the GPU instead
//! of a time slice behind it (raising the voice's GPU scheduling priority was tried and stalls the game: TTS plan §17).
//!
//! The sign is a lease in a named shared-memory table, one slot per voice process: a deadline on the system's tick
//! clock, refreshed every 100 ms while lines are pending and dropped when none are. A voice process that dies leaves a
//! deadline that runs out within LEASE_MS, so nothing waits forever. Other platforms: no sharing (never busy).
//!
//!   holder (ok-tts): `let _voice = VoiceBusy::new();` around a line's queue wait and synthesis
//!   waiter (ok-song): `wait_for_voice()` at each loop step of its GPU work

#![allow(dead_code)]

use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::time::Duration;

/// How long a lease lasts past its last refresh.
const LEASE_MS: u64 = 400;
const SLOTS: usize = 64;

#[repr(C)]
struct Slot {
    pid: AtomicU64,
    deadline: AtomicU64,
}

#[cfg(windows)]
mod sys {
    use std::ffi::c_void;
    #[link(name = "kernel32")]
    extern "system" {
        pub fn CreateFileMappingW(file: isize, attributes: *const c_void, protect: u32, size_high: u32, size_low: u32, name: *const u16) -> isize;
        pub fn MapViewOfFile(mapping: isize, access: u32, offset_high: u32, offset_low: u32, bytes: usize) -> *mut c_void;
        pub fn GetTickCount64() -> u64;
        pub fn GetCurrentProcessId() -> u32;
    }
    pub const PAGE_READWRITE: u32 = 0x04;
    pub const FILE_MAP_ALL_ACCESS: u32 = 0x000F_001F;
    pub const INVALID_HANDLE_VALUE: isize = -1;
}

/// The machine-wide table (zero-filled by the system when first created; kept while any process maps it).
#[cfg(windows)]
fn table() -> Option<&'static [Slot]> {
    static TABLE: std::sync::OnceLock<Option<usize>> = std::sync::OnceLock::new();
    let base = *TABLE.get_or_init(|| unsafe {
        let name: Vec<u16> = "Local\\OKVoiceGpuLease1".encode_utf16().chain(std::iter::once(0)).collect();
        let bytes = SLOTS * std::mem::size_of::<Slot>();
        let h = sys::CreateFileMappingW(sys::INVALID_HANDLE_VALUE, std::ptr::null(), sys::PAGE_READWRITE, 0, bytes as u32, name.as_ptr());
        if h == 0 {
            return None;
        }
        let p = sys::MapViewOfFile(h, sys::FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        (!p.is_null()).then_some(p as usize)
    });
    base.map(|p| unsafe { std::slice::from_raw_parts(p as *const Slot, SLOTS) })
}

#[cfg(not(windows))]
fn table() -> Option<&'static [Slot]> {
    None
}

#[cfg(windows)]
fn now_ms() -> u64 {
    unsafe { sys::GetTickCount64() }
}

#[cfg(not(windows))]
fn now_ms() -> u64 {
    0
}

fn my_pid() -> u64 {
    std::process::id() as u64
}

/// This process's slot: the one it already holds, else a free one, else one whose holder's lease ran out long ago.
fn my_slot(t: &'static [Slot]) -> Option<&'static Slot> {
    static INDEX: AtomicUsize = AtomicUsize::new(usize::MAX);
    let pid = my_pid();
    let i = INDEX.load(Ordering::Relaxed);
    if i < SLOTS && t[i].pid.load(Ordering::Acquire) == pid {
        return Some(&t[i]);
    }
    let now = now_ms();
    for (k, s) in t.iter().enumerate() {
        let owner = s.pid.load(Ordering::Acquire);
        let stale = owner != 0 && s.deadline.load(Ordering::Acquire) + 60_000 < now;
        if (owner == 0 || stale) && s.pid.compare_exchange(owner, pid, Ordering::AcqRel, Ordering::Acquire).is_ok() {
            s.deadline.store(0, Ordering::Release);
            INDEX.store(k, Ordering::Relaxed);
            return Some(s);
        }
    }
    None
}

static PENDING: AtomicUsize = AtomicUsize::new(0);

fn refresh() {
    if let Some(s) = table().and_then(my_slot) {
        let d = if PENDING.load(Ordering::Acquire) > 0 { now_ms() + LEASE_MS } else { 0 };
        s.deadline.store(d, Ordering::Release);
    }
}

/// A voice line queued or being made in this process (held by ok-tts per request). The lease stays up while any is
/// alive, refreshed by one background thread.
pub struct VoiceBusy(());

impl VoiceBusy {
    pub fn new() -> Self {
        static REFRESHER: std::sync::Once = std::sync::Once::new();
        PENDING.fetch_add(1, Ordering::AcqRel);
        refresh();
        REFRESHER.call_once(|| {
            std::thread::spawn(|| loop {
                std::thread::sleep(Duration::from_millis(100));
                if PENDING.load(Ordering::Acquire) > 0 {
                    refresh();
                }
            });
        });
        VoiceBusy(())
    }
}

impl Drop for VoiceBusy {
    fn drop(&mut self) {
        if PENDING.fetch_sub(1, Ordering::AcqRel) == 1 {
            refresh();
        }
    }
}

/// Is another process making a voice line now?
pub fn voice_busy() -> bool {
    let Some(t) = table() else { return false };
    let (now, pid) = (now_ms(), my_pid());
    t.iter().any(|s| {
        let owner = s.pid.load(Ordering::Acquire);
        owner != 0 && owner != pid && s.deadline.load(Ordering::Acquire) > now
    })
}

static WAITED_MS: AtomicU64 = AtomicU64::new(0);

/// Background GPU work: wait while a voice line is being made (checked every 5 ms). Returns how long it waited.
pub fn wait_for_voice() -> Duration {
    let start = std::time::Instant::now();
    while voice_busy() {
        std::thread::sleep(Duration::from_millis(5));
    }
    let waited = start.elapsed();
    WAITED_MS.fetch_add(waited.as_millis() as u64, Ordering::Relaxed);
    waited
}

/// Total time this process has waited for voice lines (for job reports).
pub fn waited_total() -> Duration {
    Duration::from_millis(WAITED_MS.load(Ordering::Relaxed))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_lease_is_seen_by_others_and_ends_with_the_last_line() {
        let Some(t) = table() else { return };
        let me = my_slot(t).unwrap();
        {
            let _a = VoiceBusy::new();
            let _b = VoiceBusy::new();
            assert!(me.deadline.load(Ordering::Acquire) > now_ms());
            // the holder itself is not "another process"
            assert!(!voice_busy());
        }
        assert_eq!(me.deadline.load(Ordering::Acquire), 0);
        // another process's lease (a fake slot) makes a waiter wait until it runs out
        let other = t.iter().find(|s| s.pid.load(Ordering::Acquire) == 0).unwrap();
        other.pid.store(u64::MAX, Ordering::Release);
        other.deadline.store(now_ms() + 120, Ordering::Release);
        assert!(voice_busy());
        let waited = wait_for_voice();
        assert!(waited >= Duration::from_millis(60) && waited < Duration::from_millis(2000), "{waited:?}");
        other.pid.store(0, Ordering::Release);
        other.deadline.store(0, Ordering::Release);
    }
}
