//! The typewriter: how much of a line shows. Without a voice it types at the reader's speed with a pause after
//! punctuation; with a voice it follows the clip's progress, and never takes back what it has shown.
use crate::tuning::TextTuning;
use serde::{Deserialize, Serialize};
use unicode_segmentation::UnicodeSegmentation;

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Typing {
    pub full: String,
    pub total: usize,
    pub shown: usize,
    /// Character times typed but not shown yet.
    budget: f64,
    pub revealed: bool,
}

pub fn graphemes(s: &str) -> Vec<&str> {
    s.graphemes(true).collect()
}

impl Typing {
    pub fn new(text: &str) -> Typing {
        let total = graphemes(text).len();
        Typing { full: text.to_string(), total, shown: 0, budget: 0.0, revealed: total == 0 }
    }

    pub fn reveal(&mut self) {
        self.shown = self.total;
        self.revealed = true;
    }

    fn cost(t: &TextTuning, g: &[&str], i: usize) -> f64 {
        let pause = |s: &str| t.pause_after.contains(s);
        if i > 0 && pause(g[i - 1]) && !pause(g[i]) {
            1.0 + t.pause_chars
        } else {
            1.0
        }
    }

    /// Types for `dt` seconds at `speed` (1..10).
    pub fn type_for(&mut self, t: &TextTuning, dt: f64, speed: u32) {
        if self.revealed {
            return;
        }
        let cps = t.speeds.get(speed.clamp(1, 10) as usize - 1).copied().unwrap_or(30.0);
        self.budget += dt.max(0.0) * cps;
        let g = graphemes(&self.full);
        while self.shown < self.total {
            let c = Self::cost(t, &g, self.shown);
            if self.budget < c {
                break;
            }
            self.budget -= c;
            self.shown += 1;
        }
        if self.shown >= self.total {
            self.revealed = true;
        }
    }

    /// Follows a voice clip at `progress` (0..1).
    pub fn follow(&mut self, progress: f64) {
        if self.revealed {
            return;
        }
        let want = (self.total as f64 * progress.clamp(0.0, 1.0)).ceil() as usize;
        self.shown = self.shown.max(want.min(self.total));
        self.budget = 0.0;
        if self.shown >= self.total {
            self.revealed = true;
        }
    }

    pub fn visible(&self) -> String {
        graphemes(&self.full).into_iter().take(self.shown).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn types_at_speed_with_pauses() {
        let t = TextTuning::default();
        // Speed 5: 30 per second. "あい。うえ": あ い 。 cost 1 each, う costs 1 + 3 after the full stop.
        let mut a = Typing::new("あい。うえ");
        assert_eq!(a.total, 5);
        a.type_for(&t, 3.0 / 30.0, 5);
        assert_eq!(a.visible(), "あい。");
        a.type_for(&t, 3.0 / 30.0, 5);
        assert_eq!(a.shown, 3, "still pausing");
        a.type_for(&t, 1.5 / 30.0, 5);
        assert_eq!(a.visible(), "あい。う");
        a.type_for(&t, 1.0, 5);
        assert!(a.revealed);
    }

    #[test]
    fn clusters_stay_whole() {
        let mut a = Typing::new("e\u{301}👨‍👩‍👧x");
        assert_eq!(a.total, 3);
        a.type_for(&TextTuning::default(), 2.0 / 30.0, 5);
        assert_eq!(a.visible(), "e\u{301}👨‍👩‍👧");
    }

    #[test]
    fn voice_progress_never_goes_back() {
        let mut a = Typing::new("0123456789");
        a.follow(0.25);
        assert_eq!(a.shown, 3);
        a.follow(0.5);
        assert_eq!(a.shown, 5);
        a.follow(0.1);
        assert_eq!(a.shown, 5);
        a.follow(1.0);
        assert!(a.revealed);
    }
}
