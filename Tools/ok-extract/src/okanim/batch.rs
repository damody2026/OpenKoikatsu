//! Background KK -> OK batch conversion on worker threads (the game keeps its own session and frame time).
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use super::convert::{from_clip, from_clip_with, ClipSampler};
use super::OkAnim;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

#[derive(serde::Deserialize)]
/// `params`: Animator floats set over the sampler's for this clip (one point of its blend trees).
pub struct Item { pub key: String, pub file: String, #[serde(default)] pub source: String, #[serde(default)] pub params: std::collections::HashMap<String, f32> }

#[derive(Default)]
struct Progress { converted: usize, skipped: usize, failed: usize, first_error: String, entries: Vec<serde_json::Value> }

pub struct Batch {
    items: Arc<Vec<Item>>,
    next: Arc<AtomicUsize>,
    finished: Arc<AtomicUsize>,
    cancel: Arc<AtomicBool>,
    progress: Arc<Mutex<Progress>>,
    workers: Vec<std::thread::JoinHandle<()>>,
}

impl Batch {
    pub fn start(sampler: ClipSampler, items: Vec<Item>, fps: f32, threads: usize) -> Batch {
        let items = Arc::new(items);
        let (next, finished) = (Arc::new(AtomicUsize::new(0)), Arc::new(AtomicUsize::new(0)));
        let cancel = Arc::new(AtomicBool::new(false));
        let progress = Arc::new(Mutex::new(Progress::default()));
        let sampler = Arc::new(sampler);
        // Work is handed out a source at a time: a worker parses that source's bundle once for all its clips (inside an
        // animation preparation scope, released after the source, so memory stays at one bundle per worker).
        let mut groups: Vec<std::ops::Range<usize>> = Vec::new();
        for i in 0..items.len() {
            match groups.last_mut() { Some(g) if items[g.start].source == items[i].source => g.end = i + 1, _ => groups.push(i..i + 1) }
        }
        let groups = Arc::new(groups);
        let workers = (0..threads.clamp(1, 32)).map(|_| {
            let (items, groups, next, finished, cancel, progress, sampler) = (items.clone(), groups.clone(), next.clone(), finished.clone(), cancel.clone(), progress.clone(), sampler.clone());
            std::thread::spawn(move || {
                while !cancel.load(Ordering::Relaxed) {
                    let g = next.fetch_add(1, Ordering::Relaxed);
                    let Some(range) = groups.get(g).cloned() else { break };
                    crate::anim::begin_animation_preparation();
                    for i in range {
                        if cancel.load(Ordering::Relaxed) { break; }
                        let item = &items[i];
                        let r = convert_one(&sampler, item, fps);
                        let mut p = progress.lock().unwrap();
                        match r {
                            Ok((entry, skipped)) => { if skipped { p.skipped += 1 } else { p.converted += 1 }; p.entries.push(entry); }
                            Err(e) => { p.failed += 1; if p.first_error.is_empty() { p.first_error = format!("{}: {e}", item.key); } }
                        }
                        drop(p);
                        finished.fetch_add(1, Ordering::Relaxed);
                    }
                    crate::anim::end_animation_preparation();
                }
            })
        }).collect();
        Batch { items, next, finished, cancel, progress, workers }
    }

    /// {"total","processed","converted","skipped","failed","first_error","running","entries":[from..]} (entries are kept,
    /// so a caller reads them incrementally with `from`).
    pub fn poll(&self, from: usize) -> serde_json::Value {
        let p = self.progress.lock().unwrap();
        let processed = self.finished.load(Ordering::Relaxed);
        let running = processed < self.items.len() && !self.cancel.load(Ordering::Relaxed) && self.workers.iter().any(|w| !w.is_finished());
        serde_json::json!({
            "total": self.items.len(), "processed": processed, "converted": p.converted, "skipped": p.skipped, "failed": p.failed,
            "first_error": p.first_error, "running": running, "entries": p.entries.get(from.min(p.entries.len())..).unwrap_or(&[]),
        })
    }

    pub fn cancel(&self) { self.cancel.store(true, Ordering::Relaxed); }
}

impl Drop for Batch {
    fn drop(&mut self) {
        self.cancel();
        for w in self.workers.drain(..) { let _ = w.join(); }
    }
}

fn convert_one(sampler: &ClipSampler, item: &Item, fps: f32) -> Result<(serde_json::Value, bool), String> {
    let path = PathBuf::from(&item.file);
    let mut entry = serde_json::json!({ "key": item.key, "source": item.source, "file": item.file });
    // An existing file is kept; its summary still goes to the catalog.
    let existing = path.ok_exists().then(|| crate::gamefs::read(&path).ok()).flatten();
    let skipped = existing.is_some();
    let (anim, bytes) = match existing.and_then(|b| OkAnim::from_bytes(&b).ok().map(|a| (a, b))) {
        Some(v) => v,
        None if skipped => return Ok((entry, true)),
        None => {
            let a = if item.params.is_empty() { from_clip(sampler, &item.key, fps, None)? } else {
                let mut params = sampler.params.clone();
                params.extend(item.params.iter().map(|(k, v)| (k.clone(), *v)));
                from_clip_with(sampler, &item.key, fps, None, &params)?
            };
            let b = a.to_bytes()?;
            (a, b)
        }
    };
    if skipped { fill(&mut entry, &anim, bytes.len()); return Ok((entry, true)); }
    if let Some(dir) = path.parent() { std::fs::create_dir_all(dir).map_err(|e| e.to_string())?; }
    // Written under a temporary name and renamed: a cancelled batch never leaves a truncated file that a rerun would skip.
    let tmp = path.with_extension("okanim.part");
    std::fs::write(&tmp, &bytes).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, &path).map_err(|e| e.to_string())?;
    fill(&mut entry, &anim, bytes.len());
    Ok((entry, false))
}

fn fill(entry: &mut serde_json::Value, anim: &OkAnim, bytes: usize) {
    let o = entry.as_object_mut().unwrap();
    o.insert("frames".into(), anim.frames.into());
    o.insert("duration".into(), serde_json::json!(anim.duration()));
    o.insert("looping".into(), anim.looping.into());
    o.insert("scene".into(), anim.scene.into());
    o.insert("bones".into(), anim.tracks.len().into());
    o.insert("bytes".into(), bytes.into());
}
