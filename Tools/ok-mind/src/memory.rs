//! Each heroine's memory stream (plan §4.5): what happened, scored for recall by relevance, importance and recency.
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

pub const MEMORY_SCHEMA: u32 = 1;
const MINUTES_PER_DAY: i64 = 24 * 60;

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct MemoryRecord {
    pub id: String,
    pub owner: String,
    /// Game time in minutes since 2010-09-01 00:00 (day * 1440 + minute).
    pub t: i64,
    /// observation | conversation | event | reflection | gossip | plan | summary | prediction
    #[serde(rename = "type")]
    pub kind_type: String,
    /// outing, touch, gift, date, confession, treat, awkward_scene, sleep_together, ...
    pub kind: String,
    /// First person, at most about 80 characters.
    pub summary: String,
    pub entities: Vec<String>,
    pub place: String,
    pub importance: f32,
    pub emotional_tag: String,
    pub quoteable: bool,
    /// self | player | heard_from:<id>
    pub source: String,
    pub confidence: f32,
    /// llm | template
    pub author: String,
    pub last_recalled: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
struct MemoryFile {
    schema: u32,
    heroine: String,
    next: u64,
    records: Vec<MemoryRecord>,
}

/// Default importance by kind when the caller gives none (plan §4.5): rule-based, no LLM call.
pub fn base_importance(kind: &str) -> f32 {
    match kind {
        "confession" | "rescue" | "betrayal" | "sleep_together" | "intimacy" => 0.9,
        "gift" | "date" | "promise" | "treat" | "remembered_taste" | "birthday" | "reveal" | "money_help" | "entrust" => 0.6,
        "outing" | "touch" | "awkward_scene" | "loan" | "repay" | "shared_shift" | "sick_visit" | "prediction" => 0.5,
        "conversation" | "summary" => 0.4,
        _ => 0.3,
    }
}

pub fn game_minutes(day: i64, minute: i64) -> i64 {
    day * MINUTES_PER_DAY + minute.clamp(0, MINUTES_PER_DAY - 1)
}

#[derive(Debug, Clone, Default)]
pub struct RecallQuery {
    pub entities: Vec<String>,
    pub keywords: Vec<String>,
    pub now: i64,
    pub k: usize,
}

fn normalise(values: &[f32]) -> Vec<f32> {
    let (lo, hi) = values.iter().fold((f32::MAX, f32::MIN), |(lo, hi), &v| (lo.min(v), hi.max(v)));
    if values.is_empty() || (hi - lo).abs() < 1e-6 {
        return vec![if values.is_empty() || hi <= 0.0 { 0.0 } else { 1.0 }; values.len()];
    }
    values.iter().map(|v| (v - lo) / (hi - lo)).collect()
}

/// Top-k by normalised relevance + importance + recency (0.99 per game hour), ties broken by newer first.
pub fn recall(records: &[MemoryRecord], q: &RecallQuery) -> Vec<MemoryRecord> {
    if records.is_empty() || q.k == 0 {
        return Vec::new();
    }
    let relevance: Vec<f32> = records
        .iter()
        .map(|m| {
            let shared = m.entities.iter().filter(|e| q.entities.contains(e)).count() as f32;
            let place = if !m.place.is_empty() && q.entities.contains(&m.place) { 1.0 } else { 0.0 };
            let words = q.keywords.iter().filter(|w| !w.is_empty() && m.summary.contains(w.as_str())).count() as f32;
            shared + place + words
        })
        .collect();
    let importance: Vec<f32> = records.iter().map(|m| m.importance).collect();
    let recency: Vec<f32> = records
        .iter()
        .map(|m| {
            let hours = ((q.now - m.t.max(m.last_recalled)).max(0) as f32) / 60.0;
            0.99f32.powf(hours)
        })
        .collect();
    let (r, i, n) = (normalise(&relevance), normalise(&importance), normalise(&recency));
    let mut scored: Vec<(f32, usize)> = (0..records.len()).map(|x| (r[x] + i[x] + n[x], x)).collect();
    scored.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal).then(records[b.1].t.cmp(&records[a.1].t)));
    scored.into_iter().take(q.k).map(|(_, x)| records[x].clone()).collect()
}

pub struct Memories {
    dir: PathBuf,
    open: std::sync::Arc<Mutex<HashMap<String, MemoryFile>>>,
}

fn file_name(heroine: &str) -> String {
    let safe: String = heroine.chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '_' }).collect();
    format!("{safe}.json")
}

impl Memories {
    pub fn new(save_dir: &Path) -> Memories {
        Memories { dir: save_dir.join("memory"), open: Default::default() }
    }

    fn with<T>(&self, heroine: &str, f: impl FnOnce(&mut MemoryFile) -> Result<T, String>) -> Result<T, String> {
        let mut open = self.open.lock().unwrap();
        if !open.contains_key(heroine) {
            let path = self.dir.join(file_name(heroine));
            crate::store::settle(&path);
            let file = if path.exists() {
                let text = std::fs::read_to_string(&path).map_err(|e| format!("memory_unreadable: {e}"))?;
                let file: MemoryFile = serde_json::from_str(&text).map_err(|e| format!("memory_invalid: {e}"))?;
                if file.schema != MEMORY_SCHEMA || file.heroine != heroine {
                    return Err(format!("memory_mismatch: {}", path.display()));
                }
                file
            } else {
                MemoryFile { schema: MEMORY_SCHEMA, heroine: heroine.to_string(), next: 1, records: Vec::new() }
            };
            open.insert(heroine.to_string(), file);
        }
        f(open.get_mut(heroine).unwrap())
    }

    /// Queued: the writer thread serializes her file as it is then (the newest records), not the caller.
    fn persist(&self, file: &MemoryFile) -> Result<(), String> {
        let (open, dir, heroine) = (self.open.clone(), self.dir.clone(), file.heroine.clone());
        crate::store::write_with(
            &self.dir.join(file_name(&file.heroine)),
            Box::new(move || {
                std::fs::create_dir_all(&dir).map_err(|e| format!("memory_dir: {e}"))?;
                let open = open.lock().unwrap();
                let file = open.get(&heroine).ok_or_else(|| format!("memory_closed: {heroine}"))?;
                serde_json::to_vec(file).map_err(|e| e.to_string())
            }),
        )
    }

    /// Stores a record (id and owner are filled in) and returns its id.
    pub fn add(&self, heroine: &str, mut record: MemoryRecord) -> Result<String, String> {
        self.with(heroine, |file| {
            record.id = format!("m{}", file.next);
            record.owner = heroine.to_string();
            record.importance = record.importance.clamp(0.0, 1.0);
            record.confidence = if record.confidence <= 0.0 { 1.0 } else { record.confidence.clamp(0.0, 1.0) };
            file.next += 1;
            let id = record.id.clone();
            file.records.push(record);
            self.persist(file)?;
            Ok(id)
        })
    }

    /// Recalled memories are marked, so what she just thought about stays fresh (plan §4.5 last_recalled).
    pub fn recall(&self, heroine: &str, q: &RecallQuery) -> Result<Vec<MemoryRecord>, String> {
        self.with(heroine, |file| {
            let picked = recall(&file.records, q);
            for r in file.records.iter_mut().filter(|r| picked.iter().any(|p| p.id == r.id)) {
                r.last_recalled = r.last_recalled.max(q.now);
            }
            Ok(picked)
        })
    }

    /// The id the next record will take (a rollback point: everything from it on came later).
    pub fn next(&self, heroine: &str) -> Result<u64, String> {
        self.with(heroine, |file| Ok(file.next))
    }

    /// Drops every record added since `next` was `next` (a conversation rolled back to that point); returns how many.
    pub fn truncate(&self, heroine: &str, next: u64) -> Result<usize, String> {
        self.with(heroine, |file| {
            if next == 0 || next >= file.next {
                return Ok(0);
            }
            let before = file.records.len();
            file.records.retain(|r| r.id.strip_prefix('m').and_then(|n| n.parse::<u64>().ok()).is_none_or(|n| n < next));
            file.next = next;
            let dropped = before - file.records.len();
            self.persist(file)?;
            Ok(dropped)
        })
    }

    pub fn all(&self, heroine: &str) -> Result<Vec<MemoryRecord>, String> {
        self.with(heroine, |file| Ok(file.records.clone()))
    }

    pub fn save_all(&self) -> Result<(), String> {
        let open = self.open.lock().unwrap();
        for file in open.values() {
            self.persist(file)?;
        }
        Ok(())
    }
}
