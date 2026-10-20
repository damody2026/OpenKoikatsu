//! 吹牛 (Cheat, the Chinese way) as a whole game: the table goes in with one action and comes back changed, with the
//! lines to say, the log, who will call the play and when, and the winner. Unreal keeps the clock (when the few seconds
//! after a play are up, when a heroine's turn comes, waiting for voices), draws the table and his hand, and passes his
//! clicks and words in as actions.
//!
//! The game: the deck is dealt out. Whoever leads a round puts 1-4 cards face down and claims any rank ("兩張 7"); the
//! others in turn either play 1-4 cards claiming that same rank, or pass. Cards can be anything: claiming is lying. The
//! last play can be called 「吹牛！」 by anyone until the next play covers it: the cards are shown, a liar takes the whole
//! pile, a wrong call makes the caller take it, and whoever was right leads the next round. When everyone else has
//! passed since the last play, the pile is put away and its player leads. Whoever has played her last cards and is not
//! caught wins.
//!
//! Cards 0..51: rank = card % 13 (A..K), suit = card / 13. Seat 0 is him. Request: table (absent for "deal"), action
//! {kind, ...}, seed. Kinds: deal {players [{name, lover, distrusts [seat]}], seat 0 first}; play {seat, cards, claim
//! (the rank, when leading)}; pass {seat}; heroine_turn {seat}; hear {text, selected, claim}; challenge {seat};
//! window_closed (the few seconds after a play are up: the turn moves on).
use crate::games::{bluff_calls, bold};
use crate::locale::Texts;
use crate::rng::RandomStream;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(default)]
pub struct Player {
    pub name: String,
    /// She is his lover (calls his plays a little less).
    pub lover: bool,
    /// Seats she distrusts (calls their plays a little more).
    pub distrusts: Vec<usize>,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(default)]
pub struct Table {
    pub players: Vec<Player>,
    pub hands: Vec<Vec<i32>>,
    pub pile: Vec<i32>,
    /// Whose turn, and the round's rank to claim (-1: the one whose turn it is leads and claims any rank).
    pub turn: usize,
    pub rank: i32,
    /// The last play: who (-1 none yet), the rank claimed, the real cards.
    pub last_seat: i32,
    pub last_rank: i32,
    pub last_cards: Vec<i32>,
    /// The last play can still be called (until the next play covers it or the round ends).
    pub open: bool,
    /// The few seconds right after a play, before the turn moves on (the heroines decide to call in them).
    pub window: bool,
    /// Passes in a row since the last play.
    pub passes: usize,
    /// Cards everyone saw go into a hand ([card, seat]): shown in a call, then taken with the pile. Gone once played.
    pub known: Vec<[i32; 2]>,
    /// Per seat: wrong calls lately (a right call takes one off). Burned, a heroine calls less.
    pub burned: Vec<i32>,
    /// This round's plays: [seat, cards claimed, cards really of the rank]. Cleared when a round starts.
    pub round: Vec<[i32; 3]>,
    pub caught: Vec<i32>,
    pub rounds: i32,
    /// -1 while playing.
    pub winner: i32,
}

/// How fast the street clock runs at the card table: a game takes a quarter of an hour of real time, and at the usual
/// 24 game minutes a real minute that would be six hours gone; at this rate it is about 45 minutes.
pub const CLOCK_RATE: f64 = 0.125;

pub fn rank_name(rank: i32) -> &'static str {
    ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"][rank.rem_euclid(13) as usize]
}
pub fn card_name(card: i32) -> String {
    format!("{}{}", ["♠", "♥", "♦", "♣"][(card / 13).rem_euclid(4) as usize], rank_name(card % 13))
}
fn sort_hand(h: &mut [i32]) {
    h.sort_by_key(|c| (c % 13, *c));
}

/// The first of these words in text (earliest, then longest): "w1,w2:value|..." from a text table; ASCII words only
/// count as whole words, so "a" is not found in "pass". Returns (where it starts, where it ends, value).
fn find_word(text: &str, list: &str) -> Option<(usize, usize, i32)> {
    let lower = text.to_lowercase();
    let mut best: Option<(usize, usize, i32)> = None;
    for group in list.split('|') {
        let (words, value) = group.rsplit_once(':').unwrap_or((group, "0"));
        let value: i32 = value.trim().parse().unwrap_or(0);
        for w in words.split(',').map(str::trim).filter(|w| !w.is_empty()) {
            let w = w.to_lowercase();
            let ascii = w.chars().all(|c| c.is_ascii());
            let mut from = 0;
            while let Some(i) = lower[from..].find(&w).map(|i| i + from) {
                let end = i + w.len();
                let whole = !ascii || {
                    let before = lower[..i].chars().next_back().is_none_or(|c| !c.is_ascii_alphanumeric());
                    let after = lower[end..].chars().next();
                    // "7s", "aces": a plural s after an ASCII word is still the word.
                    let after_ok = after.is_none_or(|c| !c.is_ascii_alphanumeric()) || (after == Some('s') && lower[end + 1..].chars().next().is_none_or(|c| !c.is_ascii_alphanumeric()));
                    before && after_ok
                };
                if whole {
                    if best.is_none_or(|(bi, be, _)| i < bi || (i == bi && end > be)) {
                        best = Some((i, end, value));
                    }
                    break;
                }
                from = i + w.len().max(1);
            }
        }
    }
    best
}

/// What a step produced besides the table, and the words to write it in.
struct Out<'a> {
    tx: &'a Texts,
    lang: &'a str,
    log: Vec<String>,
    say: Vec<Value>,
    calls: Value,
    next_turn: bool,
    window_length: f64,
}

impl<'a> Out<'a> {
    fn t(&self, key: &str, args: &[(&str, String)]) -> String {
        self.tx.t(self.lang, &format!("bluff.{key}"), args)
    }
    /// One of the "|"-separated lines of a key, at random.
    fn pick(&self, rng: &mut RandomStream, key: &str) -> String {
        let all = self.t(key, &[]);
        let lines: Vec<&str> = all.split('|').collect();
        lines[rng.rand_range(0, lines.len() as i32 - 1) as usize].to_string()
    }
    fn list(&self, key: &str) -> Vec<String> {
        self.t(key, &[]).split('|').map(str::to_string).collect()
    }
}

fn a(k: &'static str, v: impl ToString) -> (&'static str, String) {
    (k, v.to_string())
}

impl Table {
    fn seat_name(&self, seat: usize, out: &Out) -> String {
        if seat == 0 {
            out.t("you", &[])
        } else {
            self.players.get(seat).map(|p| p.name.clone()).unwrap_or_default()
        }
    }
    fn say(&self, out: &mut Out, seat: usize, line: &str) {
        out.say.push(json!({"seat": seat, "line": line}));
    }
    fn next_seat(&self, seat: usize) -> usize {
        (seat + 1) % self.players.len()
    }
    fn log(out: &mut Out, line: String) {
        out.log.push(line);
    }

    fn deal(players: Vec<Player>, rng: &mut RandomStream, out: &mut Out) -> Result<Table, String> {
        let n = players.len();
        if !(3..=6).contains(&n) {
            return Err(out.t("need_players", &[]));
        }
        let mut deck: Vec<i32> = (0..52).collect();
        for i in (1..deck.len()).rev() {
            deck.swap(i, rng.rand_range(0, i as i32) as usize);
        }
        let mut hands = vec![Vec::new(); n];
        for (i, c) in deck.into_iter().enumerate() {
            hands[i % n].push(c);
        }
        hands.iter_mut().for_each(|h| sort_hand(h));
        let t = Table { turn: rng.rand_range(0, n as i32 - 1) as usize, rank: -1, caught: vec![0; n], last_seat: -1, winner: -1, hands, players, ..Default::default() };
        let line = out.t("dealt", &[a("players", n), a("each", 52 / n), a("first", t.seat_name(t.turn, out))]);
        Table::log(out, line);
        for k in 1..n {
            let line = out.pick(rng, "opening");
            t.say(out, k, &line);
        }
        Ok(t)
    }

    /// Cards face down claimed as the round's rank (or, leading, the rank he names); each heroine decides now whether
    /// and when she calls it (games.rs bluff_calls). The play before it can no longer be called.
    fn play(&mut self, seat: usize, cards: &[i32], claim: Option<i32>, rng: &mut RandomStream, out: &mut Out) -> Result<(), String> {
        if self.window {
            return Err(out.t("wait_window", &[]));
        }
        if self.turn != seat {
            return Err(out.t("not_your_turn", &[a("who", self.seat_name(self.turn, out))]));
        }
        if cards.is_empty() || cards.len() > 4 {
            return Err(out.t("one_to_four", &[]));
        }
        let hand = &self.hands[seat];
        if cards.iter().any(|c| !hand.contains(c)) || (1..cards.len()).any(|i| cards[..i].contains(&cards[i])) {
            return Err(out.t("not_in_hand", &[]));
        }
        let rank = if self.rank >= 0 {
            if claim.is_some_and(|c| c != self.rank) {
                return Err(out.t("must_claim", &[a("rank", rank_name(self.rank))]));
            }
            self.rank
        } else {
            match claim {
                Some(c) if (0..13).contains(&c) => c,
                _ => return Err(out.t("name_rank", &[])),
            }
        };
        // What the others know of her hand before this play: how many of the rank they saw her take.
        let seen_with_her = self.known.iter().filter(|[c, s]| *s as usize == seat && c % 13 == rank).count();
        self.hands[seat].retain(|c| !cards.contains(c));
        self.pile.extend_from_slice(cards);
        self.known.retain(|[c, _]| !cards.contains(c));
        let leading = self.rank < 0;
        if leading {
            self.round.clear();
        }
        let real = cards.iter().filter(|c| *c % 13 == rank).count() as i32;
        self.round.push([seat as i32, cards.len() as i32, real]);
        self.rank = rank;
        self.last_seat = seat as i32;
        self.last_rank = rank;
        self.last_cards = cards.to_vec();
        self.open = true;
        self.window = true;
        self.passes = 0;
        let left = self.hands[seat].len();
        let lead = if leading { out.t("leading", &[]) } else { String::new() };
        let line = out.t("played", &[a("who", self.seat_name(seat, out)), a("n", cards.len()), a("lead", lead), a("rank", rank_name(rank)), a("left", left)]);
        Table::log(out, line);
        let counts = out.list("count_words");
        let claim = out.t("claim", &[a("count", counts.get(cards.len() - 1).cloned().unwrap_or_default()), a("rank", rank_name(rank))]);
        let line = if seat == 0 { claim } else { claim + &out.pick(rng, "claim_ends") };
        self.say(out, seat, &line);
        out.window_length = if seat == 0 { 3.0 } else { 5.0 };
        let seats: Vec<Value> = (1..self.players.len())
            .filter(|k| *k != seat)
            .map(|k| {
                let p = &self.players[k];
                let held = self.hands[k].iter().filter(|c| *c % 13 == rank).count();
                let elsewhere = self.known.iter().filter(|[c, s]| c % 13 == rank && *s as usize != k && *s as usize != seat).count();
                let mine: i32 = self.round.iter().filter(|p| p[0] as usize == k).map(|p| p[2]).sum();
                let others: i32 = self.round.iter().filter(|p| p[0] as usize != k).map(|p| p[1]).sum();
                json!({"seat": k, "name": p.name, "held": held, "burned": self.burned.get(k).copied().unwrap_or(0), "known_elsewhere": elsewhere, "known_truth": seen_with_her >= cards.len(), "round_impossible": held as i32 + mine + others > 4, "distrusts": seat > 0 && p.distrusts.contains(&seat), "lover": seat == 0 && p.lover})
            })
            .collect();
        out.calls = bluff_calls(&json!({"claimed": cards.len(), "left": left, "window": out.window_length, "seed": rng.unsigned() as i32 & 0x7fff_ffff, "seats": seats}))?["calls"].clone();
        Ok(())
    }

    /// Not following this round. When everyone else has passed since the last play, the pile is put away and its player
    /// leads a new round.
    fn pass(&mut self, seat: usize, rng: &mut RandomStream, out: &mut Out) -> Result<(), String> {
        if self.window {
            return Err(out.t("wait_window", &[]));
        }
        if self.turn != seat {
            return Err(out.t("not_your_turn", &[a("who", self.seat_name(self.turn, out))]));
        }
        if self.rank < 0 {
            return Err(out.t("cannot_pass", &[]));
        }
        self.passes += 1;
        let line = out.t("passed", &[a("who", self.seat_name(seat, out))]);
        Table::log(out, line);
        let line = if seat == 0 { out.t("my_pass", &[]) } else { out.pick(rng, "pass_lines") };
        self.say(out, seat, &line);
        let last = self.last_seat.max(0) as usize;
        if self.passes + 1 >= self.players.len() {
            let line = out.t("all_passed", &[a("n", self.pile.len()), a("who", self.seat_name(last, out))]);
            Table::log(out, line);
            self.pile.clear();
            self.open = false;
            self.rank = -1;
            self.passes = 0;
            self.turn = last;
        } else {
            self.turn = self.next_seat(seat);
        }
        self.rounds += 1;
        out.next_turn = true;
        Ok(())
    }

    /// Someone calls the last play: the cards are shown; a liar takes the pile, a wrong call makes the caller take it;
    /// whoever was right leads the next round.
    fn challenge(&mut self, seat: usize, rng: &mut RandomStream, out: &mut Out) -> Result<(), String> {
        if !self.open || self.last_seat < 0 {
            return Err(out.t("nothing_to_call", &[]));
        }
        if self.last_seat as usize == seat {
            return Err(out.t("own_play", &[]));
        }
        self.window = false;
        self.open = false;
        let last = self.last_seat as usize;
        let lie = self.last_cards.iter().any(|c| c % 13 != self.last_rank);
        let shown: Vec<String> = self.last_cards.iter().map(|c| card_name(*c)).collect();
        let (loser, right) = if lie { (last, seat) } else { (seat, last) };
        let took = self.pile.len();
        // Everyone saw the shown cards; they go with the pile to the one who takes it.
        for c in &self.last_cards {
            self.known.push([*c, loser as i32]);
        }
        let pile = std::mem::take(&mut self.pile);
        self.hands[loser].extend(pile);
        sort_hand(&mut self.hands[loser]);
        if lie {
            self.caught[last] += 1;
        }
        if self.burned.len() != self.players.len() {
            self.burned = vec![0; self.players.len()];
        }
        self.burned[seat] = if lie { (self.burned[seat] - 1).max(0) } else { self.burned[seat] + 1 };
        let verdict = out.t(if lie { "lie" } else { "truth" }, &[]);
        let line = out.t("called", &[a("who", self.seat_name(seat, out)), a("shown", shown.join(" ")), a("verdict", verdict), a("loser", self.seat_name(loser, out)), a("took", took)]);
        Table::log(out, line);
        let call = if seat == 0 { out.t("my_call", &[]) } else { out.pick(rng, "call_lines") };
        self.say(out, seat, &call);
        if last != 0 {
            let answer = out.pick(rng, if lie { "caught_lines" } else { "truth_lines" });
            self.say(out, last, &answer);
        }
        if self.hands[last].is_empty() {
            self.win(last, rng, out);
            return Ok(());
        }
        self.rank = -1;
        self.passes = 0;
        self.turn = right;
        self.rounds += 1;
        let line = out.t("leads", &[a("who", self.seat_name(right, out))]);
        Table::log(out, line);
        out.next_turn = true;
        Ok(())
    }

    /// The few seconds after a play are up with nobody calling: its player wins with no cards left, else the next turn.
    /// The play can still be called until the next one covers it.
    fn window_closed(&mut self, rng: &mut RandomStream, out: &mut Out) {
        if !self.window {
            return;
        }
        self.window = false;
        let last = self.last_seat as usize;
        if self.hands[last].is_empty() {
            self.win(last, rng, out);
            return;
        }
        self.turn = self.next_seat(last);
        self.rounds += 1;
        out.next_turn = true;
    }

    fn win(&mut self, seat: usize, rng: &mut RandomStream, out: &mut Out) {
        self.winner = seat as i32;
        self.window = false;
        self.open = false;
        let liar = (0..self.caught.len()).filter(|k| self.caught[*k] > 0).max_by_key(|k| self.caught[*k]);
        let most = liar.map(|k| out.t("most_caught", &[a("who", self.seat_name(k, out)), a("n", self.caught[k])])).unwrap_or_default();
        let line = out.t("won", &[a("who", self.seat_name(seat, out)), a("most", most)]);
        Table::log(out, line);
        let line = if seat == 0 { out.t("my_win", &[]) } else { out.pick(rng, "win_lines") };
        self.say(out, seat, &line);
    }

    /// A heroine's move: leading, the rank she holds most of (now and then a bold one slips one more card in);
    /// following, what she has of the rank, else a lie with one or two cards or a pass, by how bold she is and how few
    /// cards she has left.
    fn heroine_move(&mut self, seat: usize, rng: &mut RandomStream, out: &mut Out) -> Result<(), String> {
        let hand = self.hands[seat].clone();
        let boldness = bold(&self.players[seat].name);
        if self.rank < 0 {
            let rank = (0..13).max_by_key(|r| (hand.iter().filter(|c| *c % 13 == *r).count(), -r)).unwrap_or(0);
            let (mut cards, others): (Vec<i32>, Vec<i32>) = hand.iter().partition(|c| *c % 13 == rank);
            if cards.len() < 4 && !others.is_empty() && rng.frand() < boldness * 0.35 {
                cards.push(others[rng.rand_range(0, others.len() as i32 - 1) as usize]);
            }
            cards.truncate(4);
            return self.play(seat, &cards, Some(rank), rng, out);
        }
        let rank = self.rank;
        let (mut cards, mut others): (Vec<i32>, Vec<i32>) = hand.iter().partition(|c| *c % 13 == rank);
        // The ranks she has most of are what she keeps for leading: lie with the odd ones.
        others.sort_by_key(|c| hand.iter().filter(|d| *d % 13 == *c % 13).count());
        if cards.is_empty() {
            let urge = 0.25 + 0.45 * boldness + if hand.len() <= 3 { 0.25 } else { 0.0 };
            if rng.frand() >= urge {
                return self.pass(seat, rng, out);
            }
            let count = if others.len() >= 2 && rng.frand() < 0.3 + 0.2 * boldness { 2 } else { 1 };
            cards.extend(others.iter().take(count));
        } else if cards.len() < 4 && !others.is_empty() && rng.frand() < boldness * 0.3 {
            cards.push(others[0]);
        }
        cards.truncate(4);
        self.play(seat, &cards, None, rng, out)
    }

    /// His words at the table, read with the language's word lists (bluff.hear_*): a call ("吹牛", "cheat") calls the
    /// last play; a pass ("過", "pass") passes; a count ("兩張", "two") plays that many, and when he leads the rank after
    /// it ("兩張 7", "two sevens"). His picked cards if that many are picked, else the cards of the rank first, then odd
    /// ones. Anything else is table talk (Ok(false)).
    fn hear(&mut self, text: &str, selected: &[i32], claim: Option<i32>, rng: &mut RandomStream, out: &mut Out) -> Result<bool, String> {
        let words = |key: &str| out.t(key, &[]).split('|').map(|w| w.trim().to_lowercase()).filter(|w| !w.is_empty()).collect::<Vec<_>>();
        let lower = text.to_lowercase();
        let said = |list: &[String]| list.iter().any(|w| find_word(&lower, &format!("{w}:1")).is_some());
        if said(&words("hear_call")) {
            if self.open && self.last_seat > 0 {
                self.challenge(0, rng, out)?;
                return Ok(true);
            }
            return Ok(false);
        }
        if self.window || self.turn != 0 {
            return Ok(false);
        }
        if said(&words("hear_pass")) && self.rank >= 0 {
            self.pass(0, rng, out)?;
            return Ok(true);
        }
        // The count, and the rank said after it: split by the counter word where the language has one ("張").
        let card_word = out.t("hear_card_word", &[]);
        let (count_part, rank_part) = if card_word.is_empty() {
            match find_word(&lower, &out.t("hear_counts", &[])) {
                Some((_, end, _)) => (lower[..end].to_string(), lower[end..].to_string()),
                None => return Ok(false),
            }
        } else {
            match lower.find(&card_word) {
                Some(at) => (lower[..at].to_string(), lower[at + card_word.len()..].to_string()),
                None => return Ok(false),
            }
        };
        let Some((_, _, n)) = find_word(&count_part, &out.t("hear_counts", &[])) else { return Ok(false) };
        let named = find_word(&rank_part, &out.t("hear_ranks", &[])).map(|(_, _, r)| r - 1);
        let claim = if self.rank >= 0 { Some(self.rank) } else { named.or(claim) };
        let Some(want) = claim else {
            return Err(out.t("name_rank_example", &[]));
        };
        let n = (n.max(1) as usize).min(self.hands[0].len());
        let cards = if selected.len() == n {
            selected.to_vec()
        } else {
            let (mut play, others): (Vec<i32>, Vec<i32>) = self.hands[0].iter().partition(|c| *c % 13 == want);
            play.truncate(n);
            let mut others = others;
            others.sort_by_key(|c| self.hands[0].iter().filter(|d| *d % 13 == *c % 13).count());
            play.extend(others.into_iter().take(n - play.len()));
            play
        };
        self.play(0, &cards, Some(want), rng, out)?;
        Ok(true)
    }
}

fn cards_of(v: Option<&Value>) -> Vec<i32> {
    v.and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_i64).map(|c| c as i32).collect()
}

/// Cards are agreed on outside: where to sit down for them first. Indoors already: here (""). Otherwise the place he
/// named if it is open and has somewhere to sit inside, else within 40 m the most public such place (places.toml
/// privacy: a cafe before a dorm lounge before a hotel), the nearest of those; none: "" (and the game says why it
/// cannot start). Request: said, indoors, places [{id, name, dist, open, seat}]. Answer: place.
pub fn card_table_place(norms: &crate::places::PlacesTuning, r: &Value) -> Result<Value, String> {
    if r.get("indoors").and_then(Value::as_bool).unwrap_or(false) {
        return Ok(json!({"place": ""}));
    }
    let usable: Vec<Value> = r
        .get("places")
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter(|p| p.get("open").and_then(Value::as_bool).unwrap_or(false) && p.get("seat").and_then(Value::as_bool).unwrap_or(false))
        .cloned()
        .collect();
    let said = r.get("said").and_then(Value::as_str).unwrap_or("");
    let dist = |p: &Value| p.get("dist").and_then(Value::as_f64).unwrap_or(f64::MAX);
    let place = crate::social::named_place_id(said, &usable).or_else(|| {
        let privacy = |p: &Value| norms.norms(p.get("id").and_then(Value::as_str).unwrap_or("")).privacy;
        usable
            .iter()
            .filter(|p| dist(p) <= 4000.0)
            .min_by(|a, b| privacy(a).cmp(&privacy(b)).then(dist(a).total_cmp(&dist(b))))
            .and_then(|p| p.get("id").and_then(Value::as_str))
            .map(str::to_string)
    });
    Ok(json!({"place": place.unwrap_or_default()}))
}

pub fn bluff_step(tx: &Texts, r: &Value) -> Result<Value, String> {
    let action = r.get("action").cloned().unwrap_or(json!({}));
    let kind = action.get("kind").and_then(Value::as_str).unwrap_or("");
    let mut rng = RandomStream::new(r.get("seed").and_then(Value::as_f64).unwrap_or(0.0) as i32);
    let mut out = Out { tx, lang: crate::locale::lang(r), log: Vec::new(), say: Vec::new(), calls: json!([]), next_turn: false, window_length: 0.0 };
    let seat = action.get("seat").and_then(Value::as_u64).unwrap_or(0) as usize;
    let claim = action.get("claim").and_then(Value::as_i64).filter(|c| (0..13).contains(c)).map(|c| c as i32);
    let mut handled = true;
    let table = if kind == "deal" {
        let players: Vec<Player> = serde_json::from_value(action.get("players").cloned().unwrap_or(json!([]))).map_err(|e| format!("players: {e}"))?;
        Table::deal(players, &mut rng, &mut out)
    } else {
        let mut t: Table = serde_json::from_value(r.get("table").cloned().unwrap_or(json!({}))).map_err(|e| format!("table: {e}"))?;
        let n = t.players.len();
        let done = if n < 3 || t.hands.len() != n || t.caught.len() != n {
            Err(out.t("bad_table", &[]))
        } else if t.winner >= 0 {
            Err(out.t("over", &[]))
        } else if seat >= n {
            Err(out.t("no_seat", &[]))
        } else {
            match kind {
                "play" => t.play(seat, &cards_of(action.get("cards")), claim, &mut rng, &mut out),
                "pass" => t.pass(seat, &mut rng, &mut out),
                "heroine_turn" if seat > 0 && t.turn == seat && !t.window => t.heroine_move(seat, &mut rng, &mut out),
                "heroine_turn" => Err(out.t("not_her_turn", &[])),
                "challenge" => t.challenge(seat, &mut rng, &mut out),
                "window_closed" => {
                    t.window_closed(&mut rng, &mut out);
                    Ok(())
                }
                "hear" => t.hear(action.get("text").and_then(Value::as_str).unwrap_or(""), &cards_of(action.get("selected")), claim, &mut rng, &mut out).map(|h| handled = h),
                _ => Err(format!("unknown bluff action {kind}")),
            }
        };
        done.map(|_| t)
    };
    Ok(match table {
        Ok(t) => json!({"ok": true, "handled": handled, "winner": t.winner, "clock_rate": CLOCK_RATE, "table": t, "log": out.log, "say": out.say, "calls": out.calls, "next_turn": out.next_turn, "window_length": out.window_length}),
        Err(why) => json!({"ok": false, "handled": false, "why": why}),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn players(n: usize) -> Value {
        json!((0..n).map(|k| json!({"name": if k == 0 { "你".to_string() } else { format!("girl{k}") }, "lover": k == 1, "distrusts": []})).collect::<Vec<_>>())
    }
    fn step(table: &Value, action: Value, seed: i32) -> Value {
        step_in("zh-TW", table, action, seed)
    }
    fn step_in(lang: &str, table: &Value, action: Value, seed: i32) -> Value {
        bluff_step(&Texts::builtin(), &json!({"table": table, "action": action, "seed": seed, "lang": lang})).unwrap()
    }
    fn tab(v: &Value) -> Table {
        serde_json::from_value(v["table"].clone()).unwrap()
    }
    fn ok(v: &Value) -> bool {
        v["ok"].as_bool().unwrap()
    }

    #[test]
    fn deal_gives_everyone_cards() {
        let v = bluff_step(&Texts::builtin(), &json!({"action": {"kind": "deal", "players": players(4)}, "seed": 7})).unwrap();
        assert!(ok(&v), "{v}");
        let t = tab(&v);
        assert_eq!(t.hands.iter().map(Vec::len).sum::<usize>(), 52);
        assert!(t.hands.iter().all(|h| h.len() == 13));
        assert_eq!(t.rank, -1, "the first one claims what she likes");
        assert_eq!(v["say"].as_array().unwrap().len(), 3, "each heroine says an opening line");
        assert!(!ok(&bluff_step(&Texts::builtin(), &json!({"action": {"kind": "deal", "players": players(2)}, "seed": 7})).unwrap()));
    }

    fn table(turn: usize) -> Table {
        Table {
            players: serde_json::from_value(players(3)).unwrap(),
            hands: vec![vec![0, 13, 5], vec![1, 2, 3], vec![4, 6, 7]],
            turn,
            rank: -1,
            caught: vec![0; 3],
            last_seat: -1,
            winner: -1,
            ..Default::default()
        }
    }

    #[test]
    fn leading_claims_any_rank_following_must_match() {
        let t = json!(table(0));
        assert!(!ok(&step(&t, json!({"kind": "play", "seat": 0, "cards": [5]}), 1)), "leading needs a claim");
        let v = step(&t, json!({"kind": "play", "seat": 0, "cards": [5], "claim": 6}), 1);
        assert!(ok(&v), "{v}");
        assert_eq!(tab(&v).rank, 6);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        assert!(!ok(&step(&v["table"], json!({"kind": "play", "seat": 1, "cards": [1], "claim": 2}), 1)), "the round is 7s");
        assert!(ok(&step(&v["table"], json!({"kind": "play", "seat": 1, "cards": [1]}), 1)));
    }

    #[test]
    fn a_lie_called_takes_the_pile_and_the_caller_leads() {
        let v = step(&json!(table(0)), json!({"kind": "play", "seat": 0, "cards": [5], "claim": 0}), 1);
        assert_eq!(v["window_length"], 3.0);
        let v = step(&v["table"], json!({"kind": "challenge", "seat": 1}), 1);
        let t = tab(&v);
        assert_eq!(t.hands[0], vec![0, 13, 5], "the liar took his card back");
        assert_eq!(t.caught[0], 1);
        assert_eq!((t.turn, t.rank), (1, -1), "the caller leads a new round");
    }

    #[test]
    fn a_play_can_be_called_until_the_next_covers_it() {
        let v = step(&json!(table(1)), json!({"kind": "play", "seat": 1, "cards": [1], "claim": 0}), 1);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        assert!(tab(&v).open && !tab(&v).window);
        let late = step(&v["table"], json!({"kind": "challenge", "seat": 0}), 1);
        assert!(ok(&late), "after the few seconds, before the next play: still callable");
        let v = step(&v["table"], json!({"kind": "play", "seat": 2, "cards": [4]}), 1);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        let t = tab(&v);
        assert_eq!(t.last_seat, 2, "now only the newest play is open");
    }

    #[test]
    fn everyone_passing_puts_the_pile_away() {
        let v = step(&json!(table(0)), json!({"kind": "play", "seat": 0, "cards": [0], "claim": 0}), 1);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        let v = step(&v["table"], json!({"kind": "pass", "seat": 1}), 1);
        let v = step(&v["table"], json!({"kind": "pass", "seat": 2}), 1);
        let t = tab(&v);
        assert!(t.pile.is_empty() && !t.open);
        assert_eq!((t.turn, t.rank), (0, -1), "he leads again");
        assert!(!ok(&step(&v["table"], json!({"kind": "pass", "seat": 0}), 1)), "the leader cannot pass");
    }

    #[test]
    fn a_true_play_called_makes_the_caller_take_it() {
        let v = step(&json!(table(0)), json!({"kind": "play", "seat": 0, "cards": [0, 13], "claim": 0}), 1);
        let v = step(&v["table"], json!({"kind": "challenge", "seat": 2}), 1);
        let t = tab(&v);
        assert_eq!(t.hands[2].len(), 5);
        assert_eq!(t.turn, 0, "he was right: he leads");
        assert!(v["say"].as_array().unwrap().iter().any(|s| s["seat"] == 2));
    }

    #[test]
    fn last_cards_uncalled_win() {
        let mut t = table(0);
        t.hands[0] = vec![0];
        let v = step(&json!(t), json!({"kind": "play", "seat": 0, "cards": [0], "claim": 0}), 1);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        assert_eq!(v["winner"], 0);
        assert!(!ok(&step(&v["table"], json!({"kind": "play", "seat": 1, "cards": [1]}), 1)), "over");
    }

    #[test]
    fn last_cards_caught_lying_do_not_win() {
        let mut t = table(0);
        t.hands[0] = vec![5];
        let v = step(&json!(t), json!({"kind": "play", "seat": 0, "cards": [5], "claim": 0}), 1);
        let v = step(&v["table"], json!({"kind": "challenge", "seat": 1}), 1);
        assert_eq!(v["winner"], -1);
    }

    #[test]
    fn out_of_turn_or_cards_not_held_are_refused() {
        let t = json!(table(0));
        assert!(!ok(&step(&t, json!({"kind": "play", "seat": 1, "cards": [1], "claim": 0}), 1)));
        assert!(!ok(&step(&t, json!({"kind": "play", "seat": 0, "cards": [1], "claim": 0}), 1)));
        assert!(!ok(&step(&t, json!({"kind": "play", "seat": 0, "cards": [0, 0], "claim": 0}), 1)));
        assert!(!ok(&step(&t, json!({"kind": "challenge", "seat": 0}), 1)), "nothing to call yet");
    }

    #[test]
    fn he_says_how_many_and_what() {
        let t = json!(table(0));
        let v = step(&t, json!({"kind": "hear", "text": "兩張A", "selected": []}), 1);
        assert_eq!(tab(&v).last_cards, vec![0, 13], "the real aces first");
        let v = step(&t, json!({"kind": "hear", "text": "一張七", "selected": [0]}), 1);
        assert_eq!((tab(&v).last_rank, tab(&v).last_cards.clone()), (6, vec![0]), "a picked ace claimed as a 7");
        assert!(!ok(&step(&t, json!({"kind": "hear", "text": "兩張", "selected": []}), 1)), "leading needs a rank");
        let v = step(&t, json!({"kind": "hear", "text": "兩張", "selected": [], "claim": 4}), 1);
        assert_eq!(tab(&v).last_rank, 4, "the rank picked on screen");
        assert!(!step(&t, json!({"kind": "hear", "text": "今天天氣真好", "selected": []}), 1)["handled"].as_bool().unwrap());
        let v = step(&json!(table(1)), json!({"kind": "heroine_turn", "seat": 1}), 3);
        assert!(ok(&v), "{v}");
        let v = step(&v["table"], json!({"kind": "hear", "text": "吹牛！", "selected": []}), 1);
        assert!(ok(&v) && v["handled"].as_bool().unwrap(), "{v}");
    }

    #[test]
    fn a_round_claiming_too_many_is_called() {
        // He leads 2 aces (true), girl1 claims 2 more (true), girl2 claims a fifth: she knows (2 + 2 + her 1 > 4).
        let mut t = table(0);
        t.hands = vec![vec![0, 13, 5], vec![26, 39, 3], vec![1, 2, 7]];
        let v = step(&json!(t), json!({"kind": "play", "seat": 0, "cards": [0, 13], "claim": 0}), 1);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        let v = step(&v["table"], json!({"kind": "play", "seat": 1, "cards": [26, 39]}), 1);
        assert_eq!(tab(&v).round.len(), 2);
        let v = step(&v["table"], json!({"kind": "window_closed"}), 1);
        let before = v["table"].clone();
        let calls = (1..41).filter(|seed| {
            let w = step(&before, json!({"kind": "play", "seat": 2, "cards": [7]}), *seed);
            w["calls"].as_array().unwrap().iter().any(|c| c["seat"] == 1)
        }).count();
        assert!(calls >= 25, "girl1 calls the fifth ace most of the time: {calls}/40");
    }

    #[test]
    fn shown_cards_are_remembered() {
        // She lies with an ace as a 7, girl1 calls: everyone saw the ace go back to her.
        let v = step(&json!(table(0)), json!({"kind": "play", "seat": 0, "cards": [5], "claim": 6}), 1);
        let v = step(&v["table"], json!({"kind": "challenge", "seat": 1}), 1);
        assert_eq!(tab(&v).known, vec![[5, 0]]);
        // Now he leads with that card claimed as what it is: everyone knows he has it, nobody calls.
        for seed in 1..30 {
            let w = step(&v["table"], json!({"kind": "play", "seat": 1, "cards": [1], "claim": 1}), seed);
            let w = step(&w["table"], json!({"kind": "window_closed"}), seed);
            let w = step(&w["table"], json!({"kind": "pass", "seat": 2}), seed);
            let w = step(&w["table"], json!({"kind": "play", "seat": 0, "cards": [5]}), seed);
            assert!(!ok(&w) || tab(&w).known.is_empty(), "played: no longer known in his hand");
        }
    }

    #[test]
    fn where_to_sit_down_for_cards() {
        let places = json!([{"id": "cafe", "name": "咖啡廳", "dist": 1200, "open": true, "seat": true}, {"id": "restaurant", "name": "餐廳", "dist": 800, "open": false, "seat": true},
                            {"id": "lodge", "name": "會館", "dist": 3000, "open": true, "seat": true}, {"id": "park", "name": "中央公園", "dist": 100, "open": true, "seat": false}]);
        let tuning = crate::tuning::Tuning::builtin().places;
        let at = |said: &str, indoors: bool| card_table_place(&tuning, &json!({"said": said, "indoors": indoors, "places": places})).unwrap()["place"].clone();
        assert_eq!(at("去會館玩牌吧", false), "lodge", "the place he named");
        assert_eq!(at("進餐廳玩撲克牌", false), "cafe", "closed: the nearest open one instead");
        assert_eq!(at("來玩吹牛", false), "cafe");
        assert_eq!(at("來玩吹牛", true), "", "indoors already: here");
        let hotel_first = json!([{"id": "hotel", "name": "飯店", "dist": 300, "open": true, "seat": true}, {"id": "cafe", "name": "咖啡廳", "dist": 1500, "open": true, "seat": true}]);
        assert_eq!(card_table_place(&tuning, &json!({"said": "來玩吹牛", "indoors": false, "places": hotel_first})).unwrap()["place"], "cafe", "a public place before a nearer hotel");
    }

    #[test]
    fn in_english() {
        let t = json!(table(0));
        let v = step_in("en", &t, json!({"kind": "hear", "text": "two aces", "selected": []}), 1);
        assert_eq!(tab(&v).last_cards, vec![0, 13], "{v}");
        assert!(v["log"][0].as_str().unwrap().starts_with("You put down 2"), "{v}");
        assert_eq!(v["say"][0]["line"], "Two A");
        let v = step_in("en", &t, json!({"kind": "hear", "text": "one seven please", "selected": [0]}), 1);
        assert_eq!(tab(&v).last_rank, 6, "{v}");
        let v = step_in("en", &t, json!({"kind": "hear", "text": "I passed the exam", "selected": []}), 1);
        assert!(!v["handled"].as_bool().unwrap(), "no count, no rank: talk");
        let v = step_in("en", &json!(table(1)), json!({"kind": "heroine_turn", "seat": 1}), 3);
        let v = step_in("en", &v["table"], json!({"kind": "hear", "text": "Cheat!", "selected": []}), 1);
        assert!(v["log"][0].as_str().unwrap().contains("\"Cheat!\""), "{v}");
        let refused = step_in("en", &json!(table(1)), json!({"kind": "play", "seat": 0, "cards": [0], "claim": 0}), 1);
        assert_eq!(refused["why"], "It's girl1's turn");
    }

    #[test]
    fn heroines_lead_follow_or_pass() {
        for seed in 1..40 {
            let v = step(&json!(table(1)), json!({"kind": "heroine_turn", "seat": 1}), seed);
            assert!(ok(&v) && tab(&v).rank >= 0, "leading she plays");
            let v = step(&v["table"], json!({"kind": "window_closed"}), seed);
            let v = step(&v["table"], json!({"kind": "heroine_turn", "seat": 2}), seed);
            assert!(ok(&v), "{v}");
        }
    }
}
