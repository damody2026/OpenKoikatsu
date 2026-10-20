//! Priority job queue shared by the workers, plus the per-minute request gate.
use serde_json::Value;
use std::cmp::Ordering;
use std::collections::{BinaryHeap, VecDeque};
use std::sync::{Condvar, Mutex};
use std::time::{Duration, Instant};

/// Lower runs first. Talking with the player always beats background work (plan §7).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Priority {
    Dialogue = 0,
    Proactive = 1,
    Overheard = 2,
    Summary = 3,
    Reflection = 4,
    Profile = 5,
    Background = 6,
}

impl Priority {
    pub fn parse(s: &str) -> Option<Priority> {
        Some(match s {
            "dialogue" => Priority::Dialogue,
            "proactive" => Priority::Proactive,
            "overheard" => Priority::Overheard,
            "summary" => Priority::Summary,
            "reflection" => Priority::Reflection,
            "profile" => Priority::Profile,
            "background" => Priority::Background,
            _ => return None,
        })
    }
    pub fn name(self) -> &'static str {
        match self {
            Priority::Dialogue => "dialogue",
            Priority::Proactive => "proactive",
            Priority::Overheard => "overheard",
            Priority::Summary => "summary",
            Priority::Reflection => "reflection",
            Priority::Profile => "profile",
            Priority::Background => "background",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Job {
    pub id: u64,
    pub priority: Priority,
    pub kind: String,
    pub payload: Value,
}

struct Entry(Job);
impl PartialEq for Entry {
    fn eq(&self, other: &Self) -> bool {
        self.0.id == other.0.id
    }
}
impl Eq for Entry {}
impl PartialOrd for Entry {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
impl Ord for Entry {
    // BinaryHeap pops the greatest: the most urgent priority, then the oldest job.
    fn cmp(&self, other: &Self) -> Ordering {
        other.0.priority.cmp(&self.0.priority).then_with(|| other.0.id.cmp(&self.0.id))
    }
}

pub struct Queue {
    state: Mutex<(BinaryHeap<Entry>, bool)>,
    ready: Condvar,
}

impl Queue {
    pub fn new() -> Queue {
        Queue { state: Mutex::new((BinaryHeap::new(), false)), ready: Condvar::new() }
    }
    pub fn push(&self, job: Job) -> bool {
        let mut s = self.state.lock().unwrap();
        if s.1 {
            return false;
        }
        s.0.push(Entry(job));
        self.ready.notify_one();
        true
    }
    /// Blocks until a job arrives; None once the queue is closed.
    pub fn pop(&self) -> Option<Job> {
        let mut s = self.state.lock().unwrap();
        loop {
            if s.1 {
                return None;
            }
            if let Some(Entry(job)) = s.0.pop() {
                return Some(job);
            }
            s = self.ready.wait(s).unwrap();
        }
    }
    pub fn close(&self) {
        self.state.lock().unwrap().1 = true;
        self.ready.notify_all();
    }
    pub fn is_closed(&self) -> bool {
        self.state.lock().unwrap().1
    }
    pub fn len(&self) -> usize {
        self.state.lock().unwrap().0.len()
    }
}

/// At most `per_minute` requests in any 60 s window; after a 429 everything but dialogue waits out the pause.
pub struct RateGate {
    per_minute: u32,
    state: Mutex<(VecDeque<Instant>, Option<Instant>)>,
}

impl RateGate {
    pub fn new(per_minute: u32) -> RateGate {
        RateGate { per_minute, state: Mutex::new((VecDeque::new(), None)) }
    }
    /// How long to wait before this request may start; Duration::ZERO means it was admitted and counted.
    pub fn admit(&self, priority: Priority, now: Instant) -> Duration {
        let mut s = self.state.lock().unwrap();
        if priority != Priority::Dialogue {
            if let Some(until) = s.1 {
                if until > now {
                    return until - now;
                }
                s.1 = None;
            }
        }
        while let Some(&first) = s.0.front() {
            if now.duration_since(first) >= Duration::from_secs(60) {
                s.0.pop_front();
            } else {
                break;
            }
        }
        if s.0.len() as u32 >= self.per_minute {
            let first = *s.0.front().unwrap();
            return Duration::from_secs(60) - now.duration_since(first);
        }
        s.0.push_back(now);
        Duration::ZERO
    }
    pub fn pause(&self, until: Instant) {
        let mut s = self.state.lock().unwrap();
        s.1 = Some(s.1.map_or(until, |old| old.max(until)));
    }
}
