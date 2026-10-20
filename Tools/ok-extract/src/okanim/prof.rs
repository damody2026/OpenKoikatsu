//! Accumulated phase timings of OK playback (microseconds), read and reset by `ok_okanim_profile`.

use std::sync::atomic::{AtomicU64, Ordering};

pub const PHASES: [&str; 12] = ["pose", "placement", "walk", "posture", "ik", "feet", "finish", "finish.body_dyn", "finish.clothes", "finish.export", "finish.accessories", "apply_total"];
static TOTAL: [AtomicU64; 12] = [const { AtomicU64::new(0) }; 12];
static COUNT: [AtomicU64; 12] = [const { AtomicU64::new(0) }; 12];

pub fn add(phase: usize, since: std::time::Instant) {
    if phase < PHASES.len() {
        TOTAL[phase].fetch_add(since.elapsed().as_micros() as u64, Ordering::Relaxed);
        COUNT[phase].fetch_add(1, Ordering::Relaxed);
    }
}
pub fn add_us(phase: usize, us: u64) {
    if phase < PHASES.len() { TOTAL[phase].fetch_add(us, Ordering::Relaxed); COUNT[phase].fetch_add(1, Ordering::Relaxed); }
}

/// JSON {phase: [total_us, count]} since the last reset; resets when asked.
pub fn snapshot(reset: bool) -> String {
    let mut m = serde_json::Map::new();
    for (i, p) in PHASES.iter().enumerate() {
        let t = if reset { TOTAL[i].swap(0, Ordering::Relaxed) } else { TOTAL[i].load(Ordering::Relaxed) };
        let c = if reset { COUNT[i].swap(0, Ordering::Relaxed) } else { COUNT[i].load(Ordering::Relaxed) };
        m.insert(p.to_string(), serde_json::json!([t, c]));
    }
    serde_json::Value::Object(m).to_string()
}
