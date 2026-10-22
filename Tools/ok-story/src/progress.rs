//! Where the reader is and what they have: items, flags, lines read, choices made, the backlog. Saved as it is.
use crate::script::{At, Choice, Cond, ItemCount, Story};
use crate::tuning::Words;
use ok_rules::icu::Arg;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet, VecDeque};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BacklogEntry {
    pub line: String,
    pub name: String,
    pub color: String,
    pub text: String,
    /// The voice command to replay (file, tts voice).
    pub voice: Option<String>,
    pub tts: Option<String>,
    pub who: String,
    /// The choice taken at this line.
    pub chose: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Progress {
    pub act: usize,
    pub line: usize,
    pub items: BTreeMap<String, i64>,
    pub flags: BTreeMap<String, i64>,
    pub read: BTreeSet<String>,
    pub chosen: Vec<(String, usize)>,
    pub backlog: VecDeque<BacklogEntry>,
    pub ended: bool,
}

/// A choice as the reader sees it now.
pub struct ChoiceState {
    pub shown: bool,
    pub enabled: bool,
    pub hint: String,
}

impl Progress {
    pub fn at(&self) -> At {
        At { act: self.act, line: self.line }
    }

    pub fn holds(&self, c: &Cond) -> bool {
        c.flags.iter().all(|(k, v)| self.flags.get(k).copied().unwrap_or(0) == *v)
            && c.items.iter().all(|i| self.items.get(&i.item).copied().unwrap_or(0) >= i.n)
    }

    pub fn give(&mut self, list: &[ItemCount]) {
        for i in list {
            *self.items.entry(i.item.clone()).or_default() += i.n;
        }
    }

    pub fn take(&mut self, list: &[ItemCount]) {
        for i in list {
            let e = self.items.entry(i.item.clone()).or_default();
            *e = (*e - i.n).max(0);
        }
        self.items.retain(|_, n| *n > 0);
    }

    pub fn set(&mut self, flags: &BTreeMap<String, i64>) {
        for (k, v) in flags {
            self.flags.insert(k.clone(), *v);
        }
    }

    pub fn choice_state(&self, story: &Story, words: &Words, c: &Choice) -> ChoiceState {
        let missing: Vec<String> = c
            .need
            .iter()
            .filter_map(|n| {
                let have = self.items.get(&n.item).copied().unwrap_or(0);
                (have < n.n).then(|| {
                    let name = story.items.get(&n.item).map(|d| d.name.get(&words.lang)).unwrap_or_else(|| n.item.clone());
                    words.m("ui.item_have", &[("name", Arg::Str(name)), ("n", Arg::Int(n.n)), ("have", Arg::Int(have))])
                })
            })
            .collect();
        ChoiceState {
            shown: self.holds(&c.when),
            enabled: missing.is_empty(),
            hint: if missing.is_empty() { String::new() } else { words.m("ui.missing", &[("items", Arg::Str(missing.join("、")))]) },
        }
    }

    pub fn push_backlog(&mut self, entry: BacklogEntry, size: usize) {
        self.backlog.push_back(entry);
        while self.backlog.len() > size.max(1) {
            self.backlog.pop_front();
        }
    }
}
