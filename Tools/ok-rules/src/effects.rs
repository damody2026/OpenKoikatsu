//! What a rule wants done that only Unreal can do, in order: money through the books (so the journal stays reconciled),
//! relation changes, memories, history notes, messages she sends, log lines. Unreal replays the list
//! (AOKStreetSocialScene::ApplyRulesEffects) on the store the rule ran against.
use crate::relation::Relation;
use serde_json::{json, Value};

#[derive(Debug, Default, Clone)]
pub struct Effects(pub Vec<Value>);

impl Effects {
    /// His wallet (OKCash): +amount in, -amount out.
    pub fn cash(&mut self, amount: i64, category: &str, note: &str) {
        if amount != 0 {
            self.0.push(json!({"op": "cash", "amount": amount, "category": category, "note": note}));
        }
    }

    /// His savings account (OKBank).
    pub fn bank(&mut self, amount: i64, category: &str, note: &str) {
        if amount != 0 {
            self.0.push(json!({"op": "bank", "amount": amount, "category": category, "note": note}));
        }
    }

    /// A journal post with no account change of its own (OKBookPost).
    pub fn post(&mut self, book: &str, account: &str, amount: i64, category: &str, note: &str) {
        if amount != 0 {
            self.0.push(json!({"op": "post", "book": book, "account": account, "amount": amount, "category": category, "note": note}));
        }
    }

    /// The fields of her relation that changed.
    pub fn relation(&mut self, after: &Relation, before: &Relation) {
        let set = after.patch(before);
        if !set.is_empty() {
            self.0.push(json!({"op": "relation", "id": after.id, "set": set}));
        }
    }

    /// A memory of hers (MindObserve). `kind_type`: "observation", "reflection", "summary".
    pub fn observe(&mut self, id: &str, kind: &str, text: &str, place: &str, importance: f32, kind_type: &str) {
        self.0.push(json!({"op": "observe", "id": id, "kind": kind, "text": text, "place": place, "importance": importance, "type": kind_type}));
    }

    /// What she has on her mind for the next talk (MindRecent).
    pub fn recent(&mut self, id: &str, text: &str) {
        self.0.push(json!({"op": "recent", "id": id, "text": text}));
    }

    /// Her record of a settled interaction (Interact with RecordOnly; the rules' heroine_act formats it).
    pub fn note(&mut self, id: &str, act: &str, detail: &str, importance: f32, hers: bool) {
        self.0.push(json!({"op": "note", "id": id, "act": act, "detail": detail, "importance": importance, "hers": hers}));
    }

    /// A message she sends him after `delay` real seconds (QueueHerText); `text` is what she means to say.
    pub fn text(&mut self, id: &str, text: &str, kind: &str, delay: f64) {
        self.0.push(json!({"op": "text", "id": id, "text": text, "kind": kind, "delay": delay}));
    }

    /// Her record at a place (HeroineNote: Interact RecordOnly with a place).
    pub fn note_at(&mut self, id: &str, act: &str, detail: &str, place: &str, importance: f32) {
        self.0.push(json!({"op": "note", "id": id, "act": act, "detail": detail, "place": place, "importance": importance, "hers": false}));
    }

    /// Her record at a place, with what others would hear named (HeroineNote's rumour detail).
    pub fn note_rumor(&mut self, id: &str, act: &str, detail: &str, place: &str, importance: f32, rumor_detail: &str) {
        self.0.push(json!({"op": "note", "id": id, "act": act, "detail": detail, "place": place, "importance": importance, "hers": false, "rumor_detail": rumor_detail}));
    }

    /// A rumour (MakeRumor); `id` the heroine it is about, "" for him alone.
    pub fn rumor(&mut self, id: &str, kind: &str, text: &str, heat: f32, secret: bool) {
        self.0.push(json!({"op": "rumor", "id": id, "kind": kind, "text": text, "heat": heat, "secret": secret}));
    }

    /// Two heroines' feelings for each other move by a bonds.toml event (BondEventIn).
    pub fn bond_event(&mut self, a: &str, b: &str, event: &str) {
        self.0.push(json!({"op": "bond_event", "a": a, "b": b, "event": event}));
    }

    /// News for her conversation context (MindNote).
    pub fn mind_note(&mut self, id: &str, text: &str) {
        self.0.push(json!({"op": "mind_note", "id": id, "text": text}));
    }

    /// What she means to do next (the scene's intents: seek_player, ask_help, more_shifts...).
    pub fn intent(&mut self, id: &str, intent: &str) {
        self.0.push(json!({"op": "intent", "id": id, "intent": intent}));
    }

    pub fn log(&mut self, text: String) {
        self.0.push(json!({"op": "log", "text": text}));
    }

    pub fn into_value(self) -> Value {
        Value::Array(self.0)
    }
}
