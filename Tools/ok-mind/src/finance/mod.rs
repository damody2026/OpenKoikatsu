//! Money outside the wallet (plan §15): 星運彩券行 tickets, 競技彩, the A-share and US accounts, predictions and the
//! heroines' own bets. Synchronous (no LLM): the game calls `fin` queries and applies the TWD amounts they return.
//! State lives in <save_dir>/finance.json, written atomically after every change.
pub mod dates;
pub mod lottery;
pub mod market;
pub mod sports;

use crate::store::write_atomic;
use lottery::Lotto;
use market::{Bar, Market, Mkt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sports::Sports;
use std::collections::{BTreeMap, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, OnceLock};

pub const SCHEMA: u32 = 1;
// RMB: one ticket at most 2,000, 10,000 a day; prizes over 10,000 are paid at the ATM / online bank.
pub const TICKET_CAP: i64 = 2_000;
pub const DAILY_CAP: i64 = 10_000;
pub const BIG_PRIZE: i64 = 10_000;
/// Wiring money between China and the US account costs 1% each way (the hero is Chinese and lives on RMB).
pub const US_WIRE_FEE: f64 = 0.01;
/// A-share commission per trade (minimum CNY 5).
pub const CN_COMMISSION: f64 = 0.005;

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Position {
    pub qty: i64,
    pub today: i64, // bought today (A-share T+1: not sellable yet)
    pub cost: i64,  // total cost, milli-currency
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Order {
    pub id: i64,
    pub code: String,
    pub side: String, // buy | sell
    pub kind: String, // market | limit
    pub price: i32,   // limit, x1000
    pub qty: i64,
    pub filled: i64,
    pub avg: i32,
    pub status: String, // open | filled | cancelled | rejected
    pub trade_day: i32, // exchange date of the session
    pub day: i32,
    pub minute: i32,
    pub checked: usize, // last path index examined
    pub note: String,
    pub frozen: i64,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Trade {
    pub id: i64,
    pub order: i64,
    pub code: String,
    pub side: String,
    pub qty: i64,
    pub price: i32,
    pub fee: i64,
    pub tax: i64,
    pub day: i32,
    pub minute: i32,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Account {
    pub cash: i64,   // milli-currency
    pub frozen: i64, // reserved by open buy orders
    pub positions: BTreeMap<String, Position>,
    pub orders: Vec<Order>,
    pub trades: VecDeque<Trade>,
    pub filled: BTreeMap<String, i64>, // "code@day" -> shares traded (1% liquidity cap)
    pub today: i32,
    pub actions_through: i32, // corporate actions applied through this exchange date
    pub deposited_twd: i64,
    /// First day of each current holding (a heroine's holding period rules).
    #[serde(default)]
    pub since: BTreeMap<String, i32>,
}

/// A price alert on one stock (plan: STREET_SOCIAL_FINANCE_IMPROVEMENT_PLAN 4.3).
#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Alert {
    pub id: i64,
    pub market: String,
    pub code: String,
    pub kind: String, // above | below | pct | limit_up | limit_down
    pub value: f64,
    pub fired: i32, // exchange day it last fired (once a session)
}

/// His advice to a heroine about her own account, judged a week later.
#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Advice {
    pub id: i64,
    pub heroine: String,
    pub code: String,
    pub side: String, // buy | sell
    pub accepted: bool,
    pub executed: bool,
    pub day: i32,
    pub price: i32,
    pub due: i32,
    pub status: String, // open | done
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Sel {
    pub target: String,
    pub market: String,
    pub pick: String,
    pub odds: f64,
    pub line: f64,
    pub label: String,
    pub result: Option<bool>,
    pub void: bool,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Ticket {
    pub id: i64,
    pub kind: String, // lotto | sports | scratch
    pub game: String,
    pub draw_no: i32,
    pub draw_day: i32,
    pub lines: Vec<(Vec<i32>, i32)>,
    pub sels: Vec<Sel>,
    pub stake: i64,
    pub odds: f64,
    pub status: String, // open | won | lost | void | unscratched
    pub payout: i64,
    pub prize: String,
    pub day: i32,
    pub serial: i64,
    pub cells: Vec<i64>,
    pub owner: String, // "" = player, else heroine id
    pub with: String,  // heroine who chose it with him
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Claim {
    pub id: i64,
    pub ticket: i64,
    pub amount: i64,
    pub big: bool,
    pub note: String,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Prediction {
    pub id: i64,
    pub heroine: String,
    pub audience: Vec<String>,
    pub kind: String, // sports | stock | lotto
    pub target: String,
    pub pick: String,
    pub market: String,
    pub direction: String,
    pub ref_price: i32,
    pub made_day: i32,
    pub due_day: i32,
    pub status: String, // open | hit | miss
    pub text: String,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct NpcPosition {
    pub id: i64,
    pub heroine: String,
    pub market: String,
    pub code: String,
    pub qty: f64,
    pub entry: i32,
    pub entry_day: i32,
    pub exit_day: i32,
    pub twd_in: i64,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Ledger {
    pub day: i32,
    pub minute: i32,
    pub kind: String,
    pub twd: i64,
    pub note: String,
}

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
pub struct Book {
    pub schema: u32,
    pub seed: u64,
    pub next_id: i64,
    pub accounts: BTreeMap<String, Account>,
    pub tickets: Vec<Ticket>,
    pub claims: Vec<Claim>,
    pub predictions: Vec<Prediction>,
    pub npc: Vec<NpcPosition>,
    pub ledger: VecDeque<Ledger>,
    pub bets_day: i32,
    pub bets_total: i64,
    pub last_tick: (i32, i32),
    pub draws_done: BTreeMap<String, i32>,
    pub board: VecDeque<Value>,
    pub scratch_serial: i64,
    pub streak: BTreeMap<String, i32>, // heroine -> consecutive sports predictions hit
    #[serde(default)]
    pub alerts: Vec<Alert>,
    #[serde(default)]
    pub advice: Vec<Advice>,
}

impl Book {
    fn id(&mut self) -> i64 {
        self.next_id += 1;
        self.next_id
    }
    fn log(&mut self, day: i32, minute: i32, kind: &str, twd: i64, note: String) {
        self.ledger.push_back(Ledger { day, minute, kind: kind.into(), twd, note });
        while self.ledger.len() > 3000 {
            self.ledger.pop_front();
        }
    }
}

pub struct Finance {
    path: PathBuf,
    data: PathBuf,
    book: Mutex<Book>,
    sports: OnceLock<Result<Arc<Sports>, String>>,
    markets: [OnceLock<Result<Arc<Market>, String>>; 2],
    fx: [OnceLock<Vec<(i32, f64)>>; 2],
    pub ret: f64,
    /// The wallet's currency: "TWD" (shop prices) or "CNY". The finance home currency is always CNY.
    pub wallet: String,
}

fn now_of(q: &Value) -> Result<(i32, i32), String> {
    let day = dates::parse(q["date"].as_str().ok_or("fin: date is required")?).ok_or("fin: bad date")?;
    let minute = q["minute"].as_i64().unwrap_or(12 * 60).clamp(0, 1439) as i32;
    Ok((day, minute))
}

fn str_of<'a>(q: &'a Value, k: &str) -> Result<&'a str, String> {
    q[k].as_str().ok_or_else(|| format!("fin: {k} is required"))
}

const CN_UNIVERSE: [&str; 20] = [
    "sh.600519", "sz.000858", "sh.601318", "sh.600036", "sz.000001", "sz.000002", "sh.600030", "sh.601857", "sh.600900", "sz.000333",
    "sz.300750", "sz.002594", "sh.601012", "sz.300059", "sh.600276", "sz.000651", "sh.601398", "sh.600887", "sz.002415", "sh.688981",
];
const US_UNIVERSE: [&str; 22] = [
    "AAPL", "MSFT", "NVDA", "AMZN", "GOOGL", "META", "TSLA", "NFLX", "AMD", "INTC", "JPM", "BAC", "XOM", "WMT", "KO", "DIS", "BA", "NKE", "MCD", "COST", "SPY", "QQQ",
];

/// Account key: "cn" / "us" for him, "h:<heroine id>:cn" for a heroine's own account.
pub fn acct_key(m: Mkt, owner: &str) -> String {
    if owner.is_empty() { m.key().into() } else { format!("h:{owner}:{}", m.key()) }
}

fn owner_of(q: &Value) -> &str {
    q["owner"].as_str().unwrap_or("")
}

fn owner_of_key(key: &str) -> &str {
    key.strip_prefix("h:").and_then(|r| r.rsplit_once(':')).map_or("", |(o, _)| o)
}

pub fn universe(m: Mkt) -> &'static [&'static str] {
    match m {
        Mkt::Cn => &CN_UNIVERSE,
        Mkt::Us => &US_UNIVERSE,
    }
}

impl Finance {
    pub fn open(save_dir: &Path, data_dir: &Path, seed: u64) -> Result<Finance, String> {
        Self::open_with(save_dir, data_dir, seed, "CNY")
    }

    pub fn open_with(save_dir: &Path, data_dir: &Path, seed: u64, wallet: &str) -> Result<Finance, String> {
        let path = save_dir.join("finance.json");
        let book = match std::fs::read_to_string(&path) {
            Ok(text) => {
                let b: Book = serde_json::from_str(&text).map_err(|e| format!("finance_invalid: {e}"))?;
                if b.schema > SCHEMA {
                    return Err(format!("finance_schema_unsupported {}", b.schema));
                }
                b
            }
            Err(_) => Book { schema: SCHEMA, seed, ..Default::default() },
        };
        Ok(Finance { path, data: data_dir.to_path_buf(), book: Mutex::new(book), sports: OnceLock::new(), markets: [OnceLock::new(), OnceLock::new()], fx: [OnceLock::new(), OnceLock::new()], ret: 0.9, wallet: if wallet == "CNY" { "CNY".into() } else { "TWD".into() } })
    }

    pub fn sports(&self) -> Result<Arc<Sports>, String> {
        self.sports.get_or_init(|| Sports::load(&self.data.join("StreetSocialData").join("sports"), self.ret).map(Arc::new)).clone()
    }

    pub fn market(&self, m: Mkt) -> Result<Arc<Market>, String> {
        let i = if m == Mkt::Cn { 0 } else { 1 };
        self.markets[i].get_or_init(|| Market::load(&self.data.join("MarketData"), m).map(Arc::new)).clone()
    }

    /// Wallet units per unit of the account currency (CNY or USD) on a day.
    pub fn fx(&self, m: Mkt, day: i32) -> Option<f64> {
        let acct = self.twd_rate(m, day)?;
        let wallet = if self.wallet == "CNY" { self.twd_rate(Mkt::Cn, day)? } else { 1.0 };
        Some(acct / wallet)
    }

    /// CNY per unit of the account currency (the hero's home money).
    pub fn home_fx(&self, m: Mkt, day: i32) -> f64 {
        match m {
            Mkt::Cn => 1.0,
            Mkt::Us => self.twd_rate(Mkt::Us, day).unwrap_or(31.5) / self.twd_rate(Mkt::Cn, day).unwrap_or(4.7),
        }
    }

    fn twd_rate(&self, m: Mkt, day: i32) -> Option<f64> {
        let i = if m == Mkt::Cn { 0 } else { 1 };
        let rates = self.fx[i].get_or_init(|| {
            let f = if m == Mkt::Cn { "cny_twd" } else { "usd_twd" };
            let root = self.data.join("MarketData");
            market::read_fx_db(&root, f).unwrap_or_else(|| market::read_okfx(&root.join("fx").join(format!("{f}.okfx"))).unwrap_or_default())
        });
        market::fx_on(rates, day).or(Some(if m == Mkt::Cn { 4.7 } else { 31.5 }))
    }

    fn save(&self, b: &Book) -> Result<(), String> {
        let text = serde_json::to_string(b).map_err(|e| e.to_string())?;
        write_atomic(&self.path, text.as_bytes())
    }

    pub fn op(&self, q: &Value) -> Result<Value, String> {
        let op = str_of(q, "op")?;
        let mut b = self.book.lock().unwrap();
        let before = b.next_id;
        let zh_cn = q["lang"].as_str() == Some("zh-CN");
        // Its own words (休市，下次開市…, an order refused) in the game's script too.
        let mut out = self.dispatch(&mut b, op, q).map_err(|e| if zh_cn { crate::deepseek::localize(&e, "zh-CN") } else { e })?;
        // A Simplified game reads Simplified names: the data's names are Traditional first (貴州茅臺 -> 贵州茅台).
        if q["lang"].as_str() == Some("zh-CN") {
            fn walk(v: &mut Value) {
                match v {
                    Value::Object(m) => {
                        for (k, x) in m.iter_mut() {
                            if let (true, Value::String(s)) = (k == "name" || k.ends_with("_name") || k == "comp_label" || k == "text" || k == "note" || k == "message", &*x) {
                                *x = Value::String(crate::deepseek::localize(s, "zh-CN"));
                            } else if let (true, Value::Array(names)) = (k == "names", &mut *x) {
                                for n in names.iter_mut() {
                                    if let Value::String(s) = n {
                                        *n = Value::String(crate::deepseek::localize(s, "zh-CN"));
                                    }
                                }
                            } else {
                                walk(x);
                            }
                        }
                    }
                    Value::Array(a) => a.iter_mut().for_each(walk),
                    _ => {}
                }
            }
            walk(&mut out);
        }
        let mutating = !matches!(op, "summary" | "sports_board" | "lotto_info" | "quote" | "quotes" | "chart" | "fundamentals" | "search" | "account" | "notebook" | "tickets" | "movers" | "session" | "ledger" | "resolve" | "alerts");
        if mutating || b.next_id != before {
            self.save(&b)?;
        }
        Ok(out)
    }

    fn dispatch(&self, b: &mut Book, op: &str, q: &Value) -> Result<Value, String> {
        match op {
            "summary" => self.summary(b, q),
            "tick" => self.tick(b, q),
            "sports_board" => {
                let (day, minute) = now_of(q)?;
                let days = q["days"].as_i64().unwrap_or(10).clamp(1, 60) as i32;
                let back = q["back"].as_i64().unwrap_or(3).clamp(0, 30) as i32;
                Ok(self.sports()?.board(day, minute, day - back, day + days))
            }
            "sports_bet" => self.sports_bet(b, q),
            "lotto_info" => self.lotto_info(b, q),
            "lotto_buy" => self.lotto_buy(b, q),
            "scratch_buy" => self.scratch_buy(b, q),
            "scratch_reveal" => self.scratch_reveal(b, q),
            "tickets" => Ok(json!({"tickets": b.tickets.iter().rev().filter(|t| t.owner.is_empty()).take(q["count"].as_u64().unwrap_or(40) as usize).map(ticket_json).collect::<Vec<_>>(),
                                   "claims": b.claims.iter().map(|c| json!({"id": c.id, "ticket": c.ticket, "amount": c.amount, "big": c.big, "note": c.note})).collect::<Vec<_>>()})),
            "claim" => self.claim(b, q),
            "quote" => self.quote(q),
            "quotes" => {
                let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
                let codes: Vec<String> = q["codes"].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect()).unwrap_or_else(|| universe(m).iter().map(|s| s.to_string()).collect());
                let mut out = Vec::new();
                for c in codes.iter().take(60) {
                    let mut qq = q.clone();
                    qq["code"] = json!(c);
                    if let Ok(v) = self.quote(&qq) {
                        out.push(v);
                    }
                }
                Ok(json!({"quotes": out}))
            }
            "chart" => self.chart(q),
            "fundamentals" => self.fundamentals_op(q),
            "search" => self.search(q),
            "session" => {
                let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
                let (day, minute) = now_of(q)?;
                let mk = self.market(m)?;
                Ok(session_json(&mk, day, minute))
            }
            "movers" => self.movers(q),
            "account" => self.account(b, q),
            "deposit" | "withdraw" => self.transfer(b, q, op == "deposit"),
            "order" => self.order(b, q),
            "alert_add" => self.alert_add(b, q),
            "alert_del" => {
                let id = q["id"].as_i64().ok_or("fin: id")?;
                b.alerts.retain(|a| a.id != id);
                Ok(json!({"ok": true}))
            }
            "alerts" => Ok(json!({"alerts": b.alerts.iter().filter(|a| q["market"].as_str().map_or(true, |m| m == a.market)).map(|a| json!({"id": a.id, "market": a.market, "code": a.code, "kind": a.kind, "value": a.value})).collect::<Vec<_>>()})),
            "heroine_invest" => self.heroine_invest(b, q),
            "advise" => self.advise(b, q),
            "cancel" => {
                let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
                let id = q["order"].as_i64().ok_or("fin: order")?;
                let a = b.accounts.entry(acct_key(m, owner_of(q))).or_default();
                let o = a.orders.iter_mut().find(|o| o.id == id && o.status == "open").ok_or("order_not_open")?;
                o.status = "cancelled".into();
                o.note = "已撤單".into();
                let fr = o.frozen;
                o.frozen = 0;
                // The reserve goes back to what he can spend (it was taken out of cash when the order went in).
                a.frozen -= fr;
                a.cash += fr;
                Ok(json!({"ok": true}))
            }
            "notebook" => self.notebook(q),
            "resolve" => self.resolve(q),
            "predict" => self.predict(b, q),
            "npc_trade" => self.npc_trade(b, q),
            "npc_scratch" => {
                let heroine = str_of(q, "heroine")?.to_string();
                let budget = q["budget"].as_i64().unwrap_or(40).clamp(20, 1000);
                let price = if budget >= 100 { 50 } else { 20 };
                let count = (budget / price).clamp(1, 10);
                let mut won = 0;
                for _ in 0..count {
                    b.scratch_serial += 1;
                    let (_, w) = lottery::scratch(b.seed ^ dates::hash(&heroine), b.scratch_serial, price);
                    won += w;
                }
                let (day, minute) = now_of(q)?;
                b.log(day, minute, "npc_lottery", won - count * price, format!("{heroine} 刮了 {count} 張"));
                Ok(json!({"spent": count * price, "won": won, "cards": count}))
            }
            "ledger" => Ok(json!({"ledger": b.ledger.iter().rev().take(q["count"].as_u64().unwrap_or(50) as usize).map(|l| json!({"date": dates::format(l.day), "minute": l.minute, "kind": l.kind, "twd": l.twd, "note": l.note})).collect::<Vec<_>>()})),
            "ledger_add" => {
                let (day, minute) = now_of(q)?;
                b.log(day, minute, str_of(q, "kind")?, q["twd"].as_i64().unwrap_or(0), q["note"].as_str().unwrap_or("").to_string());
                Ok(json!({"ok": true}))
            }
            other => Err(format!("fin: unknown op {other}")),
        }
    }

    // -------------------------------------------------------------------------------------------- summary

    fn summary(&self, b: &Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let mut total = 0i64;
        let mut out = serde_json::Map::new();
        for m in [Mkt::Cn, Mkt::Us] {
            let Some(a) = b.accounts.get(m.key()) else { continue };
            let fx = self.fx(m, day).unwrap_or(1.0);
            let mut value = (a.cash + a.frozen) as f64 / 1000.0;
            if let Ok(mk) = self.market(m) {
                for (code, p) in &a.positions {
                    if let Some(px) = self.price_now(&mk, code, day, minute) {
                        value += p.qty as f64 * px as f64 / 1000.0;
                    }
                }
            }
            let twd = (value * fx).round() as i64;
            total += twd;
            out.insert(m.key().into(), json!({"value": value, "twd": twd, "fx": fx}));
        }
        let unclaimed: i64 = b.claims.iter().map(|c| c.amount).sum();
        let open = b.tickets.iter().filter(|t| t.owner.is_empty() && (t.status == "open" || t.status == "unscratched")).count();
        Ok(json!({"accounts": out, "invest_twd": total, "unclaimed": unclaimed, "open_tickets": open,
                  "bets_today": if b.bets_day == day { b.bets_total } else { 0 }}))
    }

    // -------------------------------------------------------------------------------------------- lottery

    fn lotto_info(&self, b: &Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let mut games = Vec::new();
        for g in [Lotto::Star649, Lotto::TwinStar] {
            let (no, nd) = lottery::next_draw(g, day, minute);
            let next = lottery::draw(b.seed, g, no);
            let mut recent = Vec::new();
            for k in 1..=q["history"].as_i64().unwrap_or(10).clamp(1, 60) as i32 {
                if no - k < 0 {
                    break;
                }
                recent.push(lottery::draw_json(&lottery::draw(b.seed, g, no - k), g));
            }
            games.push(json!({"game": g.key(), "label": g.label(), "price": g.price(), "next_no": no, "next_date": dates::format(nd), "next_minute": lottery::DRAW_MINUTE,
                              "pool": next.pool, "recent": recent}));
        }
        Ok(json!({"games": games, "board": b.board.iter().cloned().collect::<Vec<_>>(),
                  "scratch": lottery::THEMES.iter().map(|t| json!({"id": t.id, "name": t.name, "price": t.price})).collect::<Vec<_>>()}))
    }

    fn lotto_buy(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let g = Lotto::parse(str_of(q, "game")?).ok_or("fin: game")?;
        let draws = q["draws"].as_i64().unwrap_or(1).clamp(1, 10) as i32;
        let mut lines: Vec<(Vec<i32>, i32)> = Vec::new();
        if let Some(arr) = q["lines"].as_array() {
            for l in arr {
                let mut nums: Vec<i32> = l["numbers"].as_array().ok_or("fin: numbers")?.iter().filter_map(|v| v.as_i64().map(|x| x as i32)).collect();
                nums.sort();
                nums.dedup();
                if nums.len() != 6 || nums.iter().any(|&n| n < 1 || n > g.zone1()) {
                    return Err(format!("lotto_bad_numbers: 請選 6 個 1–{} 的不同號碼", g.zone1()));
                }
                let z2 = l["zone2"].as_i64().unwrap_or(0) as i32;
                if g == Lotto::TwinStar && !(1..=8).contains(&z2) {
                    return Err("lotto_bad_numbers: 第 2 區請選 1–8".into());
                }
                lines.push((nums, z2));
            }
        }
        let quick = q["quick"].as_i64().unwrap_or(0).clamp(0, 20);
        let mut rng = dates::Rng::new(b.seed ^ (b.next_id as u64 + 1).wrapping_mul(0x51) ^ ((day as u64) << 11) ^ minute as u64);
        for _ in 0..quick {
            lines.push(lottery::quick_pick(&mut rng, g));
        }
        if lines.is_empty() || lines.len() > 20 {
            return Err("lotto_no_lines".into());
        }
        let per = lines.len() as i64 * g.price();
        if per > TICKET_CAP {
            return Err("ticket_cap".into());
        }
        let (first, mut d) = lottery::next_draw(g, day, minute);
        let mut ids = Vec::new();
        for k in 0..draws {
            if k > 0 {
                d += 1;
                while !g.draws_on(d) {
                    d += 1;
                }
            }
            let id = b.id();
            b.tickets.push(Ticket { id, kind: "lotto".into(), game: g.key().into(), draw_no: first + k, draw_day: d, lines: lines.clone(), stake: per, status: "open".into(), day,
                                    owner: q["owner"].as_str().unwrap_or("").into(), with: q["with"].as_str().unwrap_or("").into(), ..Default::default() });
            ids.push(id);
        }
        let cost = per * draws as i64;
        b.log(day, minute, "lottery", -cost, format!("{} {} 注 × {} 期", g.label(), lines.len(), draws));
        Ok(json!({"cost": cost, "tickets": ids, "lines": lines.iter().map(|(n, z)| json!({"numbers": n, "zone2": z})).collect::<Vec<_>>(), "first_draw": dates::format(lottery::next_draw(g, day, minute).1)}))
    }

    fn scratch_buy(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let theme = lottery::THEMES.iter().find(|t| q["theme"].as_str() == Some(t.id)).ok_or("fin: theme")?;
        let count = q["count"].as_i64().unwrap_or(1).clamp(1, 20);
        let mut ids = Vec::new();
        for _ in 0..count {
            b.scratch_serial += 1;
            let serial = b.scratch_serial;
            let id = b.id();
            b.tickets.push(Ticket { id, kind: "scratch".into(), game: theme.id.into(), stake: theme.price, status: "unscratched".into(), day, serial, prize: theme.name.into(),
                                    owner: q["owner"].as_str().unwrap_or("").into(), ..Default::default() });
            ids.push(id);
        }
        b.log(day, minute, "lottery", -(theme.price * count), format!("幸運刮刮卡「{}」× {count}", theme.name));
        Ok(json!({"cost": theme.price * count, "tickets": ids}))
    }

    fn scratch_reveal(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let id = q["ticket"].as_i64().ok_or("fin: ticket")?;
        let seed = b.seed;
        let t = b.tickets.iter_mut().find(|t| t.id == id && t.kind == "scratch").ok_or("ticket_missing")?;
        if t.status != "unscratched" {
            return Ok(json!({"ticket": ticket_json(t)}));
        }
        let (cells, win) = lottery::scratch(seed, t.serial, t.stake);
        t.cells = cells;
        t.payout = win;
        t.status = if win > 0 { "won".into() } else { "lost".into() };
        let tj = ticket_json(t);
        if win > 0 {
            let cid = b.id();
            b.claims.push(Claim { id: cid, ticket: id, amount: win, big: win > BIG_PRIZE, note: format!("刮刮卡中 NT${win}") });
            b.log(day, minute, "lottery_win", 0, format!("刮刮卡中 NT${win}"));
        }
        Ok(json!({"ticket": tj, "win": win}))
    }

    fn claim(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let at_atm = q["where"].as_str() == Some("atm");
        let mut paid = 0;
        let mut kept = Vec::new();
        let mut notes = Vec::new();
        for c in std::mem::take(&mut b.claims) {
            if c.big && !at_atm {
                kept.push(c);
            } else {
                paid += c.amount;
                notes.push(c.note.clone());
            }
        }
        b.claims = kept;
        if paid > 0 {
            b.log(day, minute, "claim", paid, notes.join("、"));
        }
        Ok(json!({"paid": paid, "left_big": b.claims.iter().filter(|c| c.big).map(|c| c.amount).sum::<i64>()}))
    }

    // -------------------------------------------------------------------------------------------- 競技彩

    fn sports_bet(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let sp = self.sports()?;
        let stake = q["stake"].as_i64().ok_or("fin: stake")?;
        if !(10..=TICKET_CAP).contains(&stake) {
            return Err("stake_out_of_range: 每張 10–2,000 元".into());
        }
        if b.bets_day != day {
            b.bets_day = day;
            b.bets_total = 0;
        }
        let owner = q["owner"].as_str().unwrap_or("").to_string();
        if owner.is_empty() && b.bets_total + stake > DAILY_CAP {
            return Err(format!("daily_cap: 今天還能投注 NT${}", DAILY_CAP - b.bets_total));
        }
        let arr = q["selections"].as_array().ok_or("fin: selections")?;
        if arr.is_empty() || arr.len() > 4 {
            return Err("selections_count: 單場或 2–4 場過關".into());
        }
        let mut sels = Vec::new();
        let mut odds = 1.0;
        for s in arr {
            let target = str_of(s, "target")?.to_string();
            let market = str_of(s, "market")?.to_string();
            let pick = str_of(s, "pick")?.to_string();
            if sels.iter().any(|x: &Sel| x.target == target) {
                return Err("same_game_twice: 過關不能重複同一場".into());
            }
            let close = sp.close_day(&target, &market).ok_or("unknown_target")?;
            if !Sports::open_for_bets(close, day) {
                return Err("closed: 已截止投注".into());
            }
            let (o, line) = sp.odds_of(&target, &market, &pick).ok_or("unknown_pick")?;
            odds *= o;
            sels.push(Sel { label: self.sel_label(&sp, &target, &market, &pick, line), target, market, pick, odds: o, line, result: None, void: false });
        }
        let id = b.id();
        let odds = (odds * 100.0).round() / 100.0;
        b.tickets.push(Ticket { id, kind: "sports".into(), game: if sels.len() > 1 { format!("{} 串 1", sels.len()) } else { "單場".into() }, sels, stake, odds, status: "open".into(), day,
                                owner: owner.clone(), with: q["with"].as_str().unwrap_or("").into(), ..Default::default() });
        if owner.is_empty() {
            b.bets_total += stake;
        }
        b.log(day, minute, "sports_bet", -stake, format!("競技彩 #{id} 賠率 {odds:.2}"));
        Ok(json!({"cost": stake, "ticket": id, "odds": odds, "max_payout": (stake as f64 * odds).floor() as i64}))
    }

    fn sel_label(&self, sp: &Sports, target: &str, market: &str, pick: &str, line: f64) -> String {
        if let Some(g) = sp.game(target) {
            let h = sp.name(g.comp, &g.home);
            let a = sp.name(g.comp, &g.away);
            let side = match pick {
                "home" => h.clone(),
                "away" => a.clone(),
                "draw" => "和局".into(),
                "over" => format!("大於 {line}"),
                "under" => format!("小於 {line}"),
                other => other.into(),
            };
            let what = match market {
                "spread" => format!("讓分 {}{}", if pick == "home" { "" } else { "受" }, line.abs()),
                "ml" => "不讓分".into(),
                "total" => "大小分".into(),
                "score" => "比分".into(),
                "advance" => "晉級".into(),
                _ => market.into(),
            };
            return format!("{} {}（{}）vs {}（{}）{} {}", g.comp.label(), dates::format(g.tw_day), a, h, "主", what, side);
        }
        if market == "series" {
            if let Some(&i) = sp.series_by_id.get(target) {
                let s = &sp.series[i];
                return format!("{} 系列賽勝方 {}", s.comp.label(), sp.name(s.comp, pick));
            }
        }
        if market == "champion" {
            if let Some((c, y)) = sports::parse_champ(target) {
                return format!("{} {y} 冠軍 {}", c.label(), sp.name(c, pick));
            }
        }
        format!("{target} {market} {pick}")
    }

    // -------------------------------------------------------------------------------------------- tick

    fn tick(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let mut events = Vec::new();
        // Lotto draws that have happened (only the last few days get an announcement; older ones settle quietly).
        for g in [Lotto::Star649, Lotto::TwinStar] {
            let (next, _) = lottery::next_draw(g, day, minute);
            let done_before = *b.draws_done.get(g.key()).unwrap_or(&-1);
            let first = (done_before + 1).max(next - 400);
            for no in first..next {
                let d = lottery::draw(b.seed, g, no);
                if day - d.day <= 2 {
                    events.push(json!({"kind": "lotto_draw", "draw": lottery::draw_json(&d, g)}));
                }
                if d.shop_second {
                    b.board.push_front(json!({"date": dates::format(d.day), "text": format!("賀！本店開出{}貳獎", g.label())}));
                }
                let mut wins = Vec::new();
                for t in b.tickets.iter_mut().filter(|t| t.kind == "lotto" && t.game == g.key() && t.draw_no == no && t.status == "open") {
                    let mut total = 0;
                    let mut names = Vec::new();
                    for (nums, z2) in &t.lines {
                        if let Some((name, amt)) = lottery::prize(g, &d, nums, *z2) {
                            total += amt;
                            names.push(name);
                        }
                    }
                    t.payout = total;
                    t.prize = names.join("、");
                    t.status = if total > 0 { "won".into() } else { "lost".into() };
                    if total > 0 {
                        wins.push((t.id, total, t.prize.clone(), t.owner.clone(), names.contains(&"頭獎") || names.contains(&"貳獎")));
                    }
                }
                for (tid, amt, prize, owner, big_prize) in wins {
                    if owner.is_empty() {
                        let cid = b.id();
                        b.claims.push(Claim { id: cid, ticket: tid, amount: amt, big: amt > BIG_PRIZE, note: format!("{} 第 {} 期 {}", g.label(), no + 1, prize) });
                        b.log(day, minute, "lottery_win", 0, format!("{} {} NT${amt}", g.label(), prize));
                        if big_prize {
                            b.board.push_front(json!({"date": dates::format(d.day), "text": format!("賀！本店開出{}{}", g.label(), prize)}));
                        }
                    }
                    events.push(json!({"kind": "payout", "ticket": tid, "amount": amt, "prize": prize, "owner": owner, "big": amt > BIG_PRIZE, "jackpot": big_prize}));
                }
                b.draws_done.insert(g.key().into(), no);
            }
        }
        while b.board.len() > 30 {
            b.board.pop_back();
        }
        // 競技彩 results.
        if let Ok(sp) = self.sports() {
            let mut payouts = Vec::new();
            for t in b.tickets.iter_mut().filter(|t| t.kind == "sports" && t.status == "open") {
                let mut all = true;
                for s in t.sels.iter_mut() {
                    if s.result.is_some() || s.void {
                        continue;
                    }
                    let Some(sd) = sp.settle_day(&s.target, &s.market) else { s.void = true; continue };
                    if Sports::settled(sd, day, minute) {
                        match sp.settle(&s.target, &s.market, &s.pick, s.line) {
                            Some(r) => s.result = Some(r),
                            None => s.void = true,
                        }
                    } else {
                        all = false;
                    }
                }
                if !all {
                    continue;
                }
                if t.sels.iter().any(|s| s.result == Some(false)) {
                    t.status = "lost".into();
                } else {
                    let odds: f64 = t.sels.iter().map(|s| if s.void { 1.0 } else { s.odds }).product();
                    t.payout = (t.stake as f64 * odds).floor() as i64;
                    t.status = if t.sels.iter().all(|s| s.void) { "void".into() } else { "won".into() };
                    payouts.push((t.id, t.payout, t.owner.clone(), t.sels.first().map(|s| s.label.clone()).unwrap_or_default()));
                }
                events.push(json!({"kind": "sports_settled", "ticket": t.id, "status": t.status, "payout": t.payout, "owner": t.owner, "with": t.with}));
            }
            for (tid, amt, owner, label) in payouts {
                if owner.is_empty() {
                    let cid = b.id();
                    b.claims.push(Claim { id: cid, ticket: tid, amount: amt, big: amt > BIG_PRIZE, note: format!("競技彩 {label}") });
                    b.log(day, minute, "sports_win", 0, format!("競技彩 #{tid} NT${amt}"));
                }
            }
            // Recent final scores for the screens.
            if b.last_tick.0 != day {
                let finals: Vec<Value> = sp.games.iter().filter(|g| g.tw_day == day).map(|g| sp.game_json(g, day, 12 * 60 + 1)).collect();
                if !finals.is_empty() {
                    events.push(json!({"kind": "sports_day", "games": finals}));
                }
            }
        }
        // Market accounts: corporate actions, T+1 roll, limit orders, end-of-session cancels.
        for m in [Mkt::Cn, Mkt::Us] {
            let Ok(mk) = self.market(m) else { continue };
            let clock = mk.clock(day, minute);
            b.accounts.entry(m.key().into()).or_default();
            let suffix = format!(":{}", m.key());
            let keys: Vec<String> = b.accounts.keys().filter(|k| k.as_str() == m.key() || (k.starts_with("h:") && k.ends_with(&suffix))).cloned().collect();
            for key in keys {
            let owner = owner_of_key(&key).to_string();
            let mut ev = Vec::new();
            {
                let fx = self.fx(m, day).unwrap_or(1.0);
                let a = b.accounts.get_mut(&key).unwrap();
                let session_day = clock.view.map(|x| x.0).unwrap_or(clock.complete_through);
                if a.today != session_day {
                    for p in a.positions.values_mut() {
                        p.today = 0;
                    }
                    a.today = session_day;
                }
                {
                    let upto = clock.view.map(|x| x.0).unwrap_or(clock.complete_through);
                    if a.actions_through == 0 {
                        a.actions_through = upto;
                    }
                    if upto > a.actions_through {
                        let codes: Vec<String> = a.positions.keys().cloned().collect();
                        for code in codes {
                            let Ok(bars) = mk.bars(&code) else { continue };
                            for bar in bars.iter().filter(|x| x.day > a.actions_through && x.day <= upto) {
                                let p = a.positions.get_mut(&code).unwrap();
                                // Splits (US reverse splits too: ratio below 1) and dividends; a short pays the dividend.
                                if (bar.split - 1.0).abs() > 1e-4 && p.qty != 0 {
                                    let old = p.qty;
                                    p.qty = (p.qty as f64 * bar.split as f64).trunc() as i64;
                                    p.today = (p.today as f64 * bar.split as f64).trunc() as i64;
                                    let what = if m == Mkt::Cn { "送轉" } else if bar.split > 1.0 { "拆股" } else { "合股" };
                                    ev.push(json!({"kind": "corporate", "market": m.key(), "code": code, "text": format!("{code} {what}：{old} 股變成 {} 股", p.qty)}));
                                }
                                if bar.div > 0.0 && p.qty != 0 {
                                    let cash = (p.qty as f64 * bar.div as f64 * 1000.0).round() as i64;
                                    a.cash += cash;
                                    let unit = if m == Mkt::Cn { "元" } else { "美元" };
                                    let text = if cash >= 0 { format!("{code} 除息：每股 {:.3} {unit}，入帳 {:.2} {unit}", bar.div, cash as f64 / 1000.0) } else { format!("{code} 除息：放空要付每股 {:.3} {unit}，扣款 {:.2} {unit}", bar.div, -cash as f64 / 1000.0) };
                                    ev.push(json!({"kind": "corporate", "market": m.key(), "code": code, "text": text, "twd": (cash as f64 / 1000.0 * fx).round()}));
                                }
                            }
                        }
                        a.actions_through = upto;
                    }
                }
            }
            for mut e in ev {
                if !owner.is_empty() {
                    e["owner"] = json!(owner);
                }
                events.push(e);
            }
            let fills = self.work_orders(b, m, &key, &mk, day, minute)?;
            events.extend(fills.into_iter().map(|mut e| {
                if !owner.is_empty() {
                    e["owner"] = json!(owner);
                }
                e
            }));
            }
            events.extend(self.check_alerts(b, m, &mk, day, minute));
            if m == Mkt::Us {
                events.extend(self.margin_call(b, &mk, day, minute));
            }
        }
        events.extend(self.judge_advice(b, day));
        // Predictions due.
        let mut settled = Vec::new();
        for i in 0..b.predictions.len() {
            if b.predictions[i].status != "open" {
                continue;
            }
            let p = b.predictions[i].clone();
            let result = match p.kind.as_str() {
                "sports" => self.sports().ok().and_then(|sp| {
                    let sd = sp.settle_day(&p.target, &p.market)?;
                    Sports::settled(sd, day, minute).then(|| sp.settle(&p.target, &p.market, &p.pick, 0.0).unwrap_or(false))
                }),
                "stock" => self.settle_stock_prediction(&p, day, minute),
                "lotto" => (day > p.due_day || (day == p.due_day && minute >= lottery::DRAW_MINUTE)).then_some(false),
                _ => Some(false),
            };
            if let Some(hit) = result {
                b.predictions[i].status = if hit { "hit".into() } else { "miss".into() };
                settled.push((p, hit));
            }
        }
        for (p, hit) in settled {
            let streak = b.streak.entry(p.heroine.clone()).or_insert(0);
            if p.kind == "sports" {
                *streak = if hit { *streak + 1 } else { 0 };
            }
            events.push(json!({"kind": "prediction", "id": p.id, "heroine": p.heroine, "audience": p.audience, "hit": hit, "text": p.text, "type": p.kind, "streak": *streak}));
        }
        // Heroines' own positions that reach their exit day.
        let mut closed = Vec::new();
        for (i, n) in b.npc.iter().enumerate() {
            if day < n.exit_day {
                continue;
            }
            let Some(m) = Mkt::parse(&n.market) else { continue };
            let Ok(mk) = self.market(m) else { continue };
            let clock = mk.clock(day, minute);
            let exit = self.close_on_or_before(&mk, &n.code, clock.complete_through.min(n.exit_day.max(n.entry_day)));
            let Some(px) = exit else { continue };
            let fx = self.fx(m, day).unwrap_or(1.0);
            let back = (n.qty * px as f64 / 1000.0 * fx).round() as i64;
            closed.push(i);
            let name = mk.name_on(&n.code, day);
            events.push(json!({"kind": "npc_trade_closed", "heroine": n.heroine, "code": n.code, "name": name, "twd_in": n.twd_in, "twd_out": back, "pnl": back - n.twd_in}));
        }
        for i in closed.into_iter().rev() {
            b.npc.remove(i);
        }
        b.last_tick = (day, minute);
        Ok(json!({"events": events, "claims": b.claims.iter().map(|c| c.amount).sum::<i64>()}))
    }

    // -------------------------------------------------------------------------------------------- markets

    fn price_now(&self, mk: &Market, code: &str, day: i32, minute: i32) -> Option<i32> {
        let bars = mk.bars(code).ok()?;
        let clock = mk.clock(day, minute);
        if let Some((d, f)) = clock.view {
            if let Some(bar) = bars.iter().find(|x| x.day == d) {
                let path = mk.path(code, bar);
                let i = ((path.len() - 1) as f64 * f).floor() as usize;
                return Some(path[i.min(path.len() - 1)]);
            }
        }
        let i = bars.partition_point(|x| x.day <= clock.complete_through);
        (i > 0).then(|| bars[i - 1].c)
    }

    fn close_on_or_before(&self, mk: &Market, code: &str, day: i32) -> Option<i32> {
        let bars = mk.bars(code).ok()?;
        let i = bars.partition_point(|x| x.day <= day);
        (i > 0).then(|| bars[i - 1].c)
    }

    fn quote(&self, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let code = str_of(q, "code")?;
        let mk = self.market(m)?;
        let meta = mk.meta(code).ok_or_else(|| format!("unknown_symbol {code}"))?.clone();
        let bars = mk.bars(code)?;
        let clock = mk.clock(day, minute);
        let done = bars.partition_point(|x| x.day <= clock.complete_through);
        let live_bar = clock.view.and_then(|(d, f)| bars.iter().find(|x| x.day == d).map(|b| (*b, f)));
        // Never reveal a stock before its listing (its code, name or existence are the future).
        if done == 0 && live_bar.is_none() {
            return Err("not_listed: 這檔股票還沒有上市".into());
        }
        let mut v = json!({"market": m.key(), "code": meta.code, "symbol": mk.symbol_on(code, day), "name": mk.name_on(code, day), "board": meta.board, "listed": true});
        if let Some((bar, f)) = live_bar {
            let path = mk.path(code, &bar);
            let i = (((path.len() - 1) as f64) * f).floor() as usize;
            let seen = &path[..=i.min(path.len() - 1)];
            let price = *seen.last().unwrap();
            let vol = (bar.vol as f64 * (i as f64 / (path.len() - 1) as f64)).round() as i64;
            v["price"] = json!(price as f64 / 1000.0);
            v["prev_close"] = json!(bar.pc as f64 / 1000.0);
            v["open"] = json!(bar.o as f64 / 1000.0);
            v["high"] = json!(*seen.iter().max().unwrap() as f64 / 1000.0);
            v["low"] = json!(*seen.iter().min().unwrap() as f64 / 1000.0);
            v["volume"] = json!(vol);
            v["limit_up"] = json!(bar.lu as f64 / 1000.0);
            v["limit_down"] = json!(bar.ld as f64 / 1000.0);
            v["at_limit_up"] = json!(bar.lu > 0 && price >= bar.lu);
            v["at_limit_down"] = json!(bar.ld > 0 && price <= bar.ld);
            v["suspended"] = json!(bar.suspended());
            v["status"] = json!(if bar.suspended() { "停牌" } else { "交易中" });
            v["date"] = json!(dates::format(bar.day));
            v["depth"] = depth(price, bar.lu, bar.ld, code, bar.day, i);
            v["change"] = json!((price - bar.pc) as f64 / 1000.0);
            v["pct"] = json!(if bar.pc > 0 { (price - bar.pc) as f64 / bar.pc as f64 * 100.0 } else { 0.0 });
        } else if done > 0 {
            let bar = bars[done - 1];
            v["price"] = json!(bar.c as f64 / 1000.0);
            v["prev_close"] = json!(bar.pc as f64 / 1000.0);
            v["open"] = json!(bar.o as f64 / 1000.0);
            v["high"] = json!(bar.h as f64 / 1000.0);
            v["low"] = json!(bar.l as f64 / 1000.0);
            v["volume"] = json!(bar.vol);
            v["limit_up"] = json!(bar.lu as f64 / 1000.0);
            v["limit_down"] = json!(bar.ld as f64 / 1000.0);
            v["suspended"] = json!(bar.suspended());
            v["status"] = json!("收盤");
            v["date"] = json!(dates::format(bar.day));
            v["change"] = json!((bar.c - bar.pc) as f64 / 1000.0);
            v["pct"] = json!(if bar.pc > 0 { (bar.c - bar.pc) as f64 / bar.pc as f64 * 100.0 } else { 0.0 });
            v["depth"] = depth(bar.c, bar.lu, bar.ld, code, bar.day, 999);
        } else {
            v["status"] = json!("未上市");
        }
        // 52-week range from complete bars only.
        if done > 0 {
            let from = bars[done - 1].day - 365;
            let year = &bars[..done];
            let hi = year.iter().filter(|x| x.day >= from).map(|x| x.h).max().unwrap_or(0);
            let lo = year.iter().filter(|x| x.day >= from).map(|x| x.l).min().unwrap_or(0);
            v["high52"] = json!(hi as f64 / 1000.0);
            v["low52"] = json!(lo as f64 / 1000.0);
        }
        // Fundamentals published by now (plan: never ahead of the game day): valuation at the shown price.
        let funds = mk.fundamentals(code, day);
        if let Some(f0) = funds.last() {
            let price = v["price"].as_f64().unwrap_or(0.0);
            let mut f = f0.clone();
            let k: f64 = bars.iter().filter(|b| b.day > f0.available && b.day <= day).map(|b| b.split as f64).product();
            if (k - 1.0).abs() > 1e-6 && k > 0.0 {
                for v in [&mut f.eps, &mut f.eps_ttm, &mut f.bvps, &mut f.dividend_ps] {
                    *v = v.map(|x| x / k);
                }
                f.total_shares = f.total_shares.map(|s| s * k);
            }
            let f = &f;
            let mut x = f.json();
            x["split_since_report"] = json!(k);
            if price > 0.0 {
                if let Some(e) = f.eps_ttm.filter(|e| e.abs() > 1e-9) {
                    x["pe_ttm"] = json!(price / e);
                }
                if let Some(b) = f.bvps.filter(|b| *b > 1e-9) {
                    x["pb"] = json!(price / b);
                }
                if let Some(s) = f.total_shares.filter(|s| *s > 0.0) {
                    x["market_cap"] = json!(price * s);
                }
                let fy = funds.iter().rev().find(|g| g.period == "FY");
                if let Some(d) = fy.and_then(|g| g.dividend_ps).filter(|d| *d > 0.0) {
                    x["dividend_yield"] = json!(d / price * 100.0);
                }
            }
            v["fundamentals"] = x;
        }
        v["price_basis"] = json!(meta.basis);
        v["session"] = session_json(&mk, day, minute);
        Ok(v)
    }

    /// Reported periods published by the game day (newest first), for the fundamentals tab.
    fn fundamentals_op(&self, q: &Value) -> Result<Value, String> {
        let (day, _) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let code = str_of(q, "code")?;
        let mk = self.market(m)?;
        mk.meta(code).ok_or_else(|| format!("unknown_symbol {code}"))?;
        let count = q["count"].as_u64().unwrap_or(12).clamp(1, 80) as usize;
        let rows: Vec<Value> = mk.fundamentals(code, day).iter().rev().take(count).map(|f| f.json()).collect();
        Ok(json!({"market": m.key(), "code": code, "periods": rows}))
    }

    fn chart(&self, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let code = str_of(q, "code")?;
        let mk = self.market(m)?;
        let bars = mk.bars(code)?;
        let clock = mk.clock(day, minute);
        let period = q["period"].as_str().unwrap_or("d");
        if period == "intraday" {
            let (bar, upto) = match clock.view.and_then(|(d, f)| bars.iter().find(|x| x.day == d).map(|b| (*b, f))) {
                Some((b, f)) => (b, f),
                None => {
                    let i = bars.partition_point(|x| x.day <= clock.complete_through);
                    if i == 0 {
                        return Ok(json!({"points": [], "prev_close": 0}));
                    }
                    (bars[i - 1], 1.0)
                }
            };
            let path = mk.path(code, &bar);
            let n = ((path.len() - 1) as f64 * upto).floor() as usize + 1;
            return Ok(json!({"date": dates::format(bar.day), "prev_close": bar.pc as f64 / 1000.0, "minutes": m.session_minutes(),
                             "points": path[..n.min(path.len())].iter().map(|&p| p as f64 / 1000.0).collect::<Vec<_>>(),
                             "limit_up": bar.lu as f64 / 1000.0, "limit_down": bar.ld as f64 / 1000.0}));
        }
        let done = bars.partition_point(|x| x.day <= clock.complete_through);
        let mut list: Vec<Bar> = bars[..done].to_vec();
        // The running session as a partial bar.
        if let Some((d, f)) = clock.view {
            if let Some(bar) = bars.iter().find(|x| x.day == d) {
                let path = mk.path(code, bar);
                let i = ((path.len() - 1) as f64 * f).floor() as usize;
                let seen = &path[..=i.min(path.len() - 1)];
                list.push(Bar { c: *seen.last().unwrap(), h: *seen.iter().max().unwrap(), l: *seen.iter().min().unwrap(), vol: (bar.vol as f64 * f) as i64, amount: bar.amount * f, ..*bar });
            }
        }
        // Forward-adjusted (前復權) as of the last visible bar: earlier prices scaled through splits and dividends.
        let mut list = list;
        let mut f = 1.0f64;
        for i in (1..list.len()).rev() {
            let (b, prev) = (list[i], list[i - 1]);
            if f != 1.0 {
                let s = |v: i32| (v as f64 * f).round() as i32;
                let p = &mut list[i];
                p.o = s(b.o);
                p.h = s(b.h);
                p.l = s(b.l);
                p.c = s(b.c);
                p.pc = s(b.pc);
            }
            if ((b.split - 1.0).abs() > 1e-4 || b.div > 0.0) && prev.c > 0 {
                let r = ((prev.c as f64 - b.div as f64 * 1000.0) / b.split as f64) / prev.c as f64;
                if r.is_finite() && r > 0.0 {
                    f *= r;
                }
            }
        }
        if f != 1.0 && !list.is_empty() {
            let s = |v: i32| (v as f64 * f).round() as i32;
            let b = list[0];
            list[0] = Bar { o: s(b.o), h: s(b.h), l: s(b.l), c: s(b.c), pc: s(b.pc), ..b };
        }
        let list = match period {
            "w" => aggregate(&list, |d| (d + 3).div_euclid(7)),
            "m" => aggregate(&list, |d| {
                let (y, mo, _) = dates::civil_from_days(d);
                y * 12 + mo as i32
            }),
            _ => list,
        };
        let count = q["count"].as_u64().unwrap_or(120).clamp(10, 2000) as usize;
        let start = list.len().saturating_sub(count);
        Ok(json!({"bars": list[start..].iter().map(market::bar_json).collect::<Vec<_>>()}))
    }

    fn search(&self, q: &Value) -> Result<Value, String> {
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let text = q["q"].as_str().unwrap_or("").trim().to_lowercase();
        let mk = self.market(m)?;
        let (day, _) = now_of(q).unwrap_or((lottery::EPOCH, 0));
        let mut out = Vec::new();
        if text.is_empty() {
            return Ok(json!({"results": []}));
        }
        for meta in &mk.stocks {
            if meta.first > day {
                continue; // not listed yet in the game's year
            }
            if meta.last > 0 && meta.last < day {
                continue; // delisted by now
            }
            let (symbol, name) = (mk.symbol_on(&meta.code, day), mk.name_on(&meta.code, day));
            let hit = symbol.to_lowercase().starts_with(&text) || (m == Mkt::Cn && meta.code.to_lowercase() == text) || name.to_lowercase().contains(&text);
            if hit {
                out.push(json!({"code": meta.code, "symbol": symbol, "name": name, "board": meta.board}));
                if out.len() >= 20 {
                    break;
                }
            }
        }
        Ok(json!({"results": out}))
    }

    fn movers(&self, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let mut rows = Vec::new();
        for code in universe(m) {
            let mut qq = q.clone();
            qq["code"] = json!(code);
            qq["date"] = json!(dates::format(day));
            qq["minute"] = json!(minute);
            if let Ok(v) = self.quote(&qq) {
                if v["pct"].is_number() {
                    rows.push(v);
                }
            }
        }
        rows.sort_by(|a, b| b["pct"].as_f64().unwrap_or(0.0).partial_cmp(&a["pct"].as_f64().unwrap_or(0.0)).unwrap());
        let up: Vec<Value> = rows.iter().take(8).cloned().collect();
        let down: Vec<Value> = rows.iter().rev().take(8).cloned().collect();
        Ok(json!({"up": up, "down": down}))
    }

    fn account(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let mk = self.market(m)?;
        let fx = self.fx(m, day).unwrap_or(1.0);
        let a = b.accounts.entry(acct_key(m, owner_of(q))).or_default().clone();
        let mut positions = Vec::new();
        let mut value = 0.0;
        for (code, p) in &a.positions {
            if p.qty == 0 {
                continue;
            }
            let px = self.price_now(&mk, code, day, minute).unwrap_or(0);
            let mv = p.qty as f64 * px as f64 / 1000.0;
            value += mv;
            let cost = p.cost as f64 / 1000.0;
            positions.push(json!({"code": code, "symbol": mk.symbol_on(code, day), "name": mk.name_on(code, day), "qty": p.qty, "since": a.since.get(code).map(|d| dates::format(*d)),
                "sellable": if m == Mkt::Cn { p.qty - p.today } else { p.qty }, "today": p.today, "cost": cost, "avg": if p.qty > 0 { cost / p.qty as f64 } else { 0.0 },
                "price": px as f64 / 1000.0, "value": mv, "pnl": mv - cost, "pnl_pct": if cost > 0.0 { (mv - cost) / cost * 100.0 } else { 0.0 }}));
        }
        let cash = a.cash as f64 / 1000.0;
        let frozen = a.frozen as f64 / 1000.0;
        Ok(json!({"market": m.key(), "currency": m.currency(), "fx": fx, "cash": cash, "frozen": frozen, "value": value, "total": cash + frozen + value,
                  "total_twd": ((cash + frozen + value) * fx).round(), "deposited_twd": a.deposited_twd, "home_fx": self.home_fx(m, day), "total_home": (cash + frozen + value) * self.home_fx(m, day), "wallet": self.wallet,
                  "positions": positions,
                  "orders": a.orders.iter().rev().take(40).map(|o| json!({"id": o.id, "code": o.code, "symbol": mk.symbol_on(&o.code, day), "name": mk.name_on(&o.code, day),
                       "side": o.side, "kind": o.kind, "price": o.price as f64 / 1000.0, "qty": o.qty, "filled": o.filled, "avg": o.avg as f64 / 1000.0,
                       "status": o.status, "note": o.note, "date": dates::format(o.day), "minute": o.minute})).collect::<Vec<_>>(),
                  "trades": a.trades.iter().rev().take(40).map(|t| json!({"code": t.code, "side": t.side, "qty": t.qty, "price": t.price as f64 / 1000.0,
                       "fee": t.fee as f64 / 1000.0, "tax": t.tax as f64 / 1000.0, "date": dates::format(t.day), "minute": t.minute})).collect::<Vec<_>>(),
                  "session": session_json(&mk, day, minute)}))
    }

    /// Deposit converts TWD to the account currency at the day's rate; withdraw converts back.
    fn transfer(&self, b: &mut Book, q: &Value, deposit: bool) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let fx = self.fx(m, day).ok_or("fx_missing")?;
        let owner = owner_of(q).to_string();
        let a = b.accounts.entry(acct_key(m, &owner)).or_default();
        if deposit {
            let twd = q["twd"].as_i64().ok_or("fin: twd")?;
            if twd <= 0 {
                return Err("amount".into());
            }
            let fee_rate = if m == Mkt::Us { US_WIRE_FEE } else { 0.0 };
            let milli = (twd as f64 / fx * (1.0 - fee_rate) * 1000.0).floor() as i64;
            let fee = (twd as f64 * fee_rate).round();
            a.cash += milli;
            a.deposited_twd += twd;
            if !owner.is_empty() {
                return Ok(json!({"twd": -twd, "amount": milli as f64 / 1000.0, "fx": fx}));
            }
            b.log(day, minute, "fx", -twd, format!("入金 {} {:.2}（匯率 {fx:.4}{}）", m.currency(), milli as f64 / 1000.0, if fee > 0.0 { format!("，匯出手續費 1% = {fee:.0}") } else { String::new() }));
            Ok(json!({"twd": -twd, "amount": milli as f64 / 1000.0, "fx": fx, "fee": fee}))
        } else {
            let amount = q["amount"].as_f64().ok_or("fin: amount")?;
            let milli = (amount * 1000.0).round() as i64;
            if milli <= 0 || milli > a.cash {
                return Err("insufficient_cash: 可用資金不足".into());
            }
            a.cash -= milli;
            let fee_rate = if m == Mkt::Us { US_WIRE_FEE } else { 0.0 };
            let gross = milli as f64 / 1000.0 * fx;
            let twd = (gross * (1.0 - fee_rate)).floor() as i64;
            a.deposited_twd -= twd;
            if !owner.is_empty() {
                return Ok(json!({"twd": twd, "amount": amount, "fx": fx}));
            }
            b.log(day, minute, "fx", twd, format!("出金 {} {amount:.2}（匯率 {fx:.4}{}）", m.currency(), if fee_rate > 0.0 { format!("，匯回中國手續費 1% = {:.0}", gross * fee_rate) } else { String::new() }));
            Ok(json!({"twd": twd, "amount": amount, "fx": fx, "fee": (gross * fee_rate).round()}))
        }
    }

    // -------------------------------------------------------------------------------------------- alerts

    fn alert_add(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let code = str_of(q, "code")?.to_string();
        let kind = str_of(q, "type")?.to_string(); // "kind" is the request kind ("fin")
        if !["above", "below", "pct", "limit_up", "limit_down"].contains(&kind.as_str()) {
            return Err("fin: kind".into());
        }
        if (kind == "limit_up" || kind == "limit_down") && m != Mkt::Cn {
            return Err("alert: 美股沒有漲跌停".into());
        }
        let value = q["value"].as_f64().unwrap_or(0.0);
        if (kind == "above" || kind == "below" || kind == "pct") && !(value > 0.0) {
            return Err("alert: 請填大於 0 的數值".into());
        }
        self.market(m)?.meta(&code).ok_or_else(|| format!("unknown_symbol {code}"))?;
        if b.alerts.iter().any(|a| a.market == m.key() && a.code == code && a.kind == kind && (a.value - value).abs() < 1e-9) {
            return Err("alert: 已經有這個提醒".into());
        }
        if b.alerts.len() >= 60 {
            return Err("alert: 提醒最多 60 個".into());
        }
        let id = b.id();
        b.alerts.push(Alert { id, market: m.key().into(), code, kind, value, fired: 0 });
        Ok(json!({"id": id}))
    }

    /// Alerts on the running session: each fires at most once a session.
    fn check_alerts(&self, b: &mut Book, m: Mkt, mk: &Market, day: i32, minute: i32) -> Vec<Value> {
        let mut out = Vec::new();
        let Some((live_day, f)) = mk.clock(day, minute).live else { return out };
        for a in b.alerts.iter_mut().filter(|a| a.market == m.key() && a.fired != live_day) {
            let Ok(bars) = mk.bars(&a.code) else { continue };
            let Some(bar) = bars.iter().find(|x| x.day == live_day).copied() else { continue };
            if bar.suspended() {
                continue;
            }
            let path = mk.path(&a.code, &bar);
            let px = path[(((path.len() - 1) as f64) * f).floor() as usize];
            let price = px as f64 / 1000.0;
            let pct = if bar.pc > 0 { (px - bar.pc) as f64 / bar.pc as f64 * 100.0 } else { 0.0 };
            let name = mk.name_on(&a.code, day);
            let unit = if m == Mkt::Cn { "" } else { "$" };
            let text = match a.kind.as_str() {
                "above" if price >= a.value => Some(format!("{name} 漲到 {unit}{price:.2}（提醒價 {unit}{:.2}）", a.value)),
                "below" if price <= a.value => Some(format!("{name} 跌到 {unit}{price:.2}（提醒價 {unit}{:.2}）", a.value)),
                "pct" if pct.abs() >= a.value => Some(format!("{name} 今天{} {:.2}%（現價 {unit}{price:.2}）", if pct > 0.0 { "漲" } else { "跌" }, pct.abs())),
                "limit_up" if bar.lu > 0 && px >= bar.lu => Some(format!("{name} 漲停了（{price:.2}）")),
                "limit_down" if bar.ld > 0 && px <= bar.ld => Some(format!("{name} 跌停了（{price:.2}）")),
                _ => None,
            };
            if let Some(text) = text {
                a.fired = live_day;
                out.push(json!({"kind": "alert", "market": m.key(), "code": a.code, "alert": a.id, "text": text}));
            }
        }
        out
    }

    // -------------------------------------------------------------------------------------------- heroines' own accounts

    /// His advice to a heroine; she has already decided (`accepted`). Accepted advice is carried out on her next
    /// trading turn, and every piece of advice is judged a week later ("advice_result").
    fn advise(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let heroine = str_of(q, "heroine")?.to_string();
        let code = str_of(q, "code")?.to_string();
        let side = str_of(q, "side")?.to_string();
        if side != "buy" && side != "sell" {
            return Err("fin: side".into());
        }
        let mk = self.market(Mkt::Cn)?;
        let price = self.price_now(&mk, &code, day, minute).ok_or("not_listed: 這檔股票還沒有上市")?;
        let id = b.id();
        let accepted = q["accepted"].as_bool().unwrap_or(false);
        b.advice.push(Advice { id, heroine, code, side, accepted, executed: false, day, price, due: day + 7, status: "open".into() });
        Ok(json!({"id": id, "price": price as f64 / 1000.0}))
    }

    fn judge_advice(&self, b: &mut Book, day: i32) -> Vec<Value> {
        let mut out = Vec::new();
        let Ok(mk) = self.market(Mkt::Cn) else { return out };
        for a in b.advice.iter_mut().filter(|a| a.status == "open" && day >= a.due) {
            let Some(now) = self.close_on_or_before(&mk, &a.code, a.due.min(day)) else { continue };
            a.status = "done".into();
            let pct = (now - a.price) as f64 / a.price.max(1) as f64 * 100.0;
            let right = if a.side == "buy" { pct > 0.0 } else { pct < 0.0 };
            out.push(json!({"kind": "advice_result", "heroine": a.heroine, "code": a.code, "name": mk.name_on(&a.code, day), "side": a.side,
                            "accepted": a.accepted, "pct": pct, "right": right}));
        }
        b.advice.retain(|a| a.status == "open" || a.day >= day - 60);
        out
    }

    /// A heroine's trading turn (A shares, once a session): money in, his accepted advice, her own sell rules,
    /// then perhaps one purchase in her style. Prices, limits, T+1 and fees are the same as his.
    fn heroine_invest(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let heroine = str_of(q, "heroine")?.to_string();
        let style = q["style"].as_str().unwrap_or("random").to_string();
        let mk = self.market(Mkt::Cn)?;
        let Some((live_day, _)) = mk.clock(day, minute).live else { return Ok(json!({"skipped": "closed"})) };
        let key = acct_key(Mkt::Cn, &heroine);
        let base = json!({"market": "cn", "date": dates::format(day), "minute": minute, "owner": heroine});
        let mut actions = Vec::new();
        let deposit = q["deposit"].as_i64().unwrap_or(0);
        if deposit > 0 {
            let mut d = base.clone();
            d["twd"] = json!(deposit);
            self.transfer(b, &d, true)?;
        }
        let order = |b: &mut Book, code: &str, side: &str, qty: i64, why: &str, actions: &mut Vec<Value>| {
            if qty <= 0 {
                return;
            }
            let mut o = base.clone();
            o["code"] = json!(code);
            o["side"] = json!(side);
            o["kind"] = json!("market");
            o["qty"] = json!(qty);
            let r = self.order(b, &o);
            let name = mk.name_on(code, day);
            match r {
                Ok(v) => actions.push(json!({"code": code, "name": name, "side": side, "qty": qty, "price": v["price"], "status": v["status"], "why": why})),
                Err(e) => actions.push(json!({"code": code, "name": name, "side": side, "qty": qty, "status": "error", "error": e, "why": why})),
            }
        };
        let sellable = |b: &Book, code: &str| -> i64 { b.accounts.get(&key).and_then(|a| a.positions.get(code)).map_or(0, |p| p.qty - p.today) };
        let lot_cost = |code: &str| -> Option<i64> {
            let bars = mk.bars(code).ok()?;
            let bar = bars.iter().find(|x| x.day == live_day)?;
            if bar.suspended() {
                return None;
            }
            let px = self.price_now(&mk, code, day, minute)?;
            if bar.lu > 0 && px >= bar.lu {
                return None; // sealed at the limit: nothing to buy
            }
            Some(px as i64 * 100)
        };
        // 1. His accepted advice.
        let todo: Vec<(i64, String, String)> = b.advice.iter().filter(|a| a.heroine == heroine && a.accepted && !a.executed).map(|a| (a.id, a.code.clone(), a.side.clone())).collect();
        for (id, code, side) in todo {
            if side == "sell" {
                let n = sellable(b, &code);
                order(b, &code, "sell", n, "advice", &mut actions);
            } else if let Some(lot) = lot_cost(&code) {
                let cash = b.accounts.get(&key).map_or(0, |a| a.cash);
                let per = lot + lot / 100 + 5_000;
                let lots = if cash >= per { ((cash / 2) / per).max(1) } else { 0 };
                order(b, &code, "buy", lots * 100, "advice", &mut actions);
            }
            if let Some(a) = b.advice.iter_mut().find(|a| a.id == id) {
                a.executed = true;
            }
        }
        // 2. Her own sell rules: take profit, stop loss, holding time.
        let (tp, sl, hold) = match style.as_str() {
            "safe" => (0.20, -0.12, 90),
            "chase" => (0.08, -0.05, 7),
            _ => (0.15, -0.10, 30),
        };
        let held: Vec<(String, i64, i64, i32)> = b.accounts.get(&key).map(|a| a.positions.iter().filter(|(_, p)| p.qty > 0).map(|(c, p)| (c.clone(), p.qty, p.cost, a.since.get(c).copied().unwrap_or(day))).collect()).unwrap_or_default();
        for (code, qty, cost, since) in &held {
            let Some(px) = self.price_now(&mk, code, day, minute) else { continue };
            let ret = (*qty as f64 * px as f64) / (*cost).max(1) as f64 - 1.0;
            let why = if ret >= tp { "take_profit" } else if ret <= sl { "stop_loss" } else if day - since >= hold { "held_long" } else { continue };
            let n = sellable(b, code);
            order(b, code, "sell", n, why, &mut actions);
        }
        // 3. Maybe one purchase in her style.
        let positions = b.accounts.get(&key).map_or(0, |a| a.positions.values().filter(|p| p.qty > 0).count());
        let max_pos = if style == "chase" { 1 } else { 3 };
        let cash = b.accounts.get(&key).map_or(0, |a| a.cash);
        let mut rng = dates::Rng::new(dates::hash(&format!("{heroine}{day}")));
        if positions < max_pos && actions.iter().all(|a| a["side"] != "buy") {
            let safe = ["sh.510300", "sh.510050", "sh.601398", "sh.601318", "sh.600036", "sh.600900", "sz.000002"];
            let pool: Vec<&str> = if style == "safe" { safe.to_vec() } else { CN_UNIVERSE.iter().copied().chain(safe.iter().copied()).collect() };
            let mut cands: Vec<(String, i64, f64)> = Vec::new();
            for code in pool {
                let Some(lot) = lot_cost(code) else { continue };
                if lot + lot / 100 + 5_000 > cash * 9 / 10 {
                    continue; // she keeps a little cash back
                }
                let bars = mk.bars(code).unwrap();
                let i = bars.partition_point(|x| x.day < live_day);
                let prev = if i > 0 && bars[i - 1].pc > 0 { (bars[i - 1].c - bars[i - 1].pc) as f64 / bars[i - 1].pc as f64 } else { 0.0 };
                cands.push((code.to_string(), lot, prev));
            }
            let pick = match style.as_str() {
                "chase" => cands.iter().max_by(|a, b| a.2.partial_cmp(&b.2).unwrap()).cloned(),
                "safe" => cands.first().cloned(),
                _ => (!cands.is_empty()).then(|| cands[(rng.next_u64() % cands.len() as u64) as usize].clone()),
            };
            if let Some((code, lot, _)) = pick {
                let budget = if style == "safe" { cash * 6 / 10 } else { cash / 2 };
                let lots = (budget / (lot + lot / 100 + 5_000)).max(1);
                order(b, &code, "buy", lots * 100, style.as_str(), &mut actions);
            }
        }
        let a = b.accounts.get(&key).cloned().unwrap_or_default();
        let mut value = 0i64;
        for (code, p) in &a.positions {
            value += p.qty * self.price_now(&mk, code, day, minute).unwrap_or(0) as i64;
        }
        Ok(json!({"actions": actions, "cash": a.cash as f64 / 1000.0, "value": value as f64 / 1000.0, "total": (a.cash + a.frozen + value) as f64 / 1000.0}))
    }

    fn fees(m: Mkt, side: &str, code: &str, value_milli: i64, day: i32) -> (i64, i64) {
        match m {
            Mkt::Cn => {
                let commission = ((value_milli as f64 * CN_COMMISSION).round() as i64).max(5_000);
                let transfer = if code.starts_with("sh.") { (value_milli as f64 * 0.00001).round() as i64 } else { 0 };
                let stamp_rate = if day < dates::days_from_civil(2023, 8, 28) { 0.001 } else { 0.0005 };
                let tax = if side == "sell" { (value_milli as f64 * stamp_rate).round() as i64 } else { 0 };
                (commission + transfer, tax)
            }
            Mkt::Us => (if day < dates::days_from_civil(2019, 10, 1) { 7_000 } else { 0 }, 0),
        }
    }

    fn order(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let m = Mkt::parse(str_of(q, "market")?).ok_or("fin: market")?;
        let code = str_of(q, "code")?.to_string();
        let side = str_of(q, "side")?.to_string();
        let kind = q["kind"].as_str().unwrap_or("market").to_string();
        let qty = q["qty"].as_i64().ok_or("fin: qty")?;
        let mk = self.market(m)?;
        let meta = mk.meta(&code).ok_or("unknown_symbol")?;
        if meta.is_index && !(m == Mkt::Us && meta.etf) && m == Mkt::Cn {
            return Err("not_tradable: 指數不能交易".into());
        }
        if side != "buy" && side != "sell" {
            return Err("fin: side".into());
        }
        let clock = mk.clock(day, minute);
        let Some((trade_day, f)) = clock.live else {
            return Err(format!("market_closed: {}", session_json(&mk, day, minute)["text"].as_str().unwrap_or("休市")));
        };
        let bars = mk.bars(&code)?;
        let Some(bar) = bars.iter().find(|x| x.day == trade_day).copied() else { return Err("no_trading: 今天沒有交易（未上市、停牌或已下市）".into()) };
        if bar.suspended() {
            return Err("suspended: 停牌中".into());
        }
        if qty <= 0 {
            return Err("qty".into());
        }
        if m == Mkt::Cn && side == "buy" && qty % 100 != 0 {
            return Err("lot: 買進要以 100 股（1 手）為單位".into());
        }
        let path = mk.path(&code, &bar);
        let idx = (((path.len() - 1) as f64) * f).floor() as usize;
        let price = path[idx.min(path.len() - 1)];
        let limit = if kind == "limit" { (q["price"].as_f64().ok_or("fin: price")? * 1000.0).round() as i32 } else { 0 };
        if kind == "limit" && (limit <= 0 || (bar.lu > 0 && limit > bar.lu) || (bar.ld > 0 && limit < bar.ld)) {
            return Err("price_out_of_band: 委託價超出漲跌停範圍".into());
        }
        let owner = owner_of(q).to_string();
        let key = acct_key(m, &owner);
        if !owner.is_empty() && side == "sell" && m == Mkt::Us {
            // Heroines never short.
            let held = b.accounts.get(&key).and_then(|a| a.positions.get(&code)).map_or(0, |p| p.qty);
            if qty > held {
                return Err("insufficient_shares: 可賣股數不足".into());
            }
        }
        let a = b.accounts.entry(key.clone()).or_default();
        // T+1 roll for this session first: an order can arrive before the day's first tick.
        if a.today != trade_day {
            for p in a.positions.values_mut() {
                p.today = 0;
            }
            a.today = trade_day;
        }
        if side == "sell" {
            let p = a.positions.get(&code).cloned().unwrap_or_default();
            let pending: i64 = a.orders.iter().filter(|o| o.status == "open" && o.side == "sell" && o.code == code).map(|o| o.qty - o.filled).sum();
            let sellable = if m == Mkt::Cn { p.qty - p.today } else { p.qty } - pending;
            if m == Mkt::Us && qty > sellable {
                // US short sale: the part beyond what he holds opens a short; 150% margin in cash after the sale.
                let short_after = (qty - sellable).max(0);
                let short_value = short_after * price as i64;
                if a.cash + qty * (price as i64) < short_value * 3 / 2 {
                    return Err(format!("margin: 放空需要 150% 保證金，現金至少要 {:.2}", (short_value * 3 / 2 - qty * price as i64) as f64 / 1000.0));
                }
            } else if qty > sellable {
                return Err(if m == Mkt::Cn && p.today > 0 { "t_plus_1: 今天買進的股票明天才能賣".into() } else { "insufficient_shares: 可賣股數不足".into() });
            }
        }
        let id = b.id();
        let a = b.accounts.get_mut(&key).unwrap();
        let mut o = Order { id, code: code.clone(), side: side.clone(), kind: kind.clone(), price: limit, qty, status: "open".into(), trade_day, day, minute, checked: idx, ..Default::default() };
        if side == "buy" {
            let est = if kind == "limit" { limit } else if bar.lu > 0 { bar.lu.max(price) } else { price };
            let need = qty * est as i64 + Self::fees(m, "buy", &code, qty * est as i64, day).0;
            if need > a.cash {
                return Err(format!("insufficient_cash: 需要約 {:.2}，可用 {:.2}", need as f64 / 1000.0, a.cash as f64 / 1000.0));
            }
            a.cash -= need;
            a.frozen += need;
            o.frozen = need;
        }
        let mut events = Vec::new();
        // Immediate fill check at the current price.
        let blocked = (side == "buy" && bar.lu > 0 && price >= bar.lu) || (side == "sell" && bar.ld > 0 && price <= bar.ld);
        let marketable = kind == "market" || (side == "buy" && price <= limit) || (side == "sell" && price >= limit);
        if blocked {
            o.note = if side == "buy" { "漲停，無法買進".into() } else { "跌停，無法賣出".into() };
            if kind == "market" {
                o.status = "rejected".into();
                a.cash += o.frozen;
                a.frozen -= o.frozen;
                o.frozen = 0;
            }
        } else if marketable {
            let fill_price = if kind == "market" { price } else if side == "buy" { price.min(limit) } else { price.max(limit) };
            if let Some(e) = fill(a, m, &mut o, &bar, fill_price, day, minute, id) {
                events.push(e);
            }
            if kind == "market" && o.status == "open" {
                // A market order never rests: what the 1% cap leaves is cancelled.
                o.status = if o.filled > 0 { "filled".into() } else { "rejected".into() };
                if o.filled < o.qty {
                    o.note = "超過當日可成交量，部分未成交".into();
                }
                a.cash += o.frozen;
                a.frozen -= o.frozen;
                o.frozen = 0;
            }
        }
        let status = o.status.clone();
        let note = o.note.clone();
        a.orders.push(o);
        if a.orders.len() > 400 {
            a.orders.retain(|x| x.status == "open" || x.day >= day - 30);
        }
        // Holding start for each position (a heroine's holding rules); cleared once it is sold out.
        {
            let a = b.accounts.get_mut(&key).unwrap();
            let held = a.positions.get(&code).map_or(0, |p| p.qty);
            if held == 0 {
                a.since.remove(&code);
            } else {
                a.since.entry(code.clone()).or_insert(day);
            }
        }
        let twd = self.fx(m, day).unwrap_or(1.0) * qty as f64 * price as f64 / 1000.0;
        if !owner.is_empty() {
            return Ok(json!({"order": id, "status": status, "note": note, "price": price as f64 / 1000.0, "events": events}));
        }
        b.log(day, minute, if side == "buy" { "stock_buy" } else { "stock_sell" }, 0, format!("{} {} {} {} 股（約 NT${:.0}）", m.key(), if side == "buy" { "買" } else { "賣" }, code, qty, twd));
        Ok(json!({"order": id, "status": status, "note": note, "price": price as f64 / 1000.0, "events": events}))
    }

    fn work_orders(&self, b: &mut Book, m: Mkt, key: &str, mk: &Market, day: i32, minute: i32) -> Result<Vec<Value>, String> {
        let clock = mk.clock(day, minute);
        let mut events = Vec::new();
        let a = b.accounts.entry(key.into()).or_default();
        for i in 0..a.orders.len() {
            if a.orders[i].status != "open" {
                continue;
            }
            let td = a.orders[i].trade_day;
            let ended = clock.complete_through >= td && clock.live.map(|x| x.0) != Some(td);
            let upto = match clock.live {
                Some((d, f)) if d == td => f,
                _ if ended => 1.0,
                _ => continue,
            };
            let code = a.orders[i].code.clone();
            let Ok(bars) = mk.bars(&code) else { continue };
            let Some(bar) = bars.iter().find(|x| x.day == td).copied() else { continue };
            let path = mk.path(&code, &bar);
            let end = (((path.len() - 1) as f64) * upto).floor() as usize;
            let mut o = a.orders[i].clone();
            let mut k = o.checked + 1;
            while k <= end.min(path.len() - 1) && o.status == "open" {
                let p = path[k];
                let blocked = (o.side == "buy" && bar.lu > 0 && p >= bar.lu) || (o.side == "sell" && bar.ld > 0 && p <= bar.ld);
                let hit = if o.side == "buy" { p <= o.price } else { p >= o.price };
                if hit && blocked {
                    o.note = if o.side == "buy" { "漲停未成交".into() } else { "跌停未成交".into() };
                } else if hit {
                    let (limit_price, oid) = (o.price, o.id);
                    if let Some(e) = fill(a, m, &mut o, &bar, limit_price, day, minute, oid) {
                        events.push(e);
                    }
                    if o.status == "open" {
                        break; // the 1% cap is reached for today
                    }
                }
                k += 1;
            }
            o.checked = end;
            if ended && o.status == "open" {
                o.status = "cancelled".into();
                o.note = if o.note.is_empty() { "收市未成交，已撤單".into() } else { format!("{}，收市撤單", o.note) };
                a.cash += o.frozen;
                a.frozen -= o.frozen;
                o.frozen = 0;
            }
            a.orders[i] = o;
        }
        let _ = (day, minute);
        Ok(events)
    }

    /// US shorts: when cash falls under 130% of the shorts' market value during a session, they are bought back.
    fn margin_call(&self, b: &mut Book, mk: &Market, day: i32, minute: i32) -> Vec<Value> {
        let mut events = Vec::new();
        let Some((live_day, _)) = mk.clock(day, minute).live else { return events };
        let Some(a) = b.accounts.get_mut("us") else { return events };
        let shorts: Vec<(String, i64)> = a.positions.iter().filter(|(_, p)| p.qty < 0).map(|(c, p)| (c.clone(), -p.qty)).collect();
        if shorts.is_empty() {
            return events;
        }
        let mut value = 0i64;
        let mut prices = Vec::new();
        for (code, q) in &shorts {
            let Some(px) = self.price_now(mk, code, day, minute) else { continue };
            value += q * px as i64;
            prices.push((code.clone(), *q, px));
        }
        if a.cash * 100 >= value * 130 {
            return events;
        }
        for (code, q, px) in prices {
            let Ok(bars) = mk.bars(&code) else { continue };
            let Some(bar) = bars.iter().find(|x| x.day == live_day).copied() else { continue };
            b.next_id += 1;
            let id = b.next_id;
            let mut o = Order { id, code: code.clone(), side: "buy".into(), kind: "market".into(), qty: q, status: "open".into(), trade_day: live_day, day, minute, note: "保證金不足，強制回補".into(), ..Default::default() };
            let a = b.accounts.get_mut("us").unwrap();
            // A forced cover ignores the 1% liquidity cap: the broker closes it whatever it takes.
            a.filled.remove(&format!("{code}@{}", bar.day));
            let big = Bar { vol: bar.vol.max(q * 200), ..bar };
            if let Some(e) = fill(a, Mkt::Us, &mut o, &big, px, day, minute, id) {
                events.push(e);
            }
            o.status = "filled".into();
            a.orders.push(o);
            events.push(json!({"kind": "margin_call", "market": "us", "code": code, "qty": q, "price": px as f64 / 1000.0}));
        }
        events
    }

    // -------------------------------------------------------------------------------------------- notebook, predictions

    /// The player's "memory of the future" (plan §15): real results of the coming games, index direction by year, and
    /// a few famous stock moves. Lotto is never listed.
    fn notebook(&self, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let mut sports = Vec::new();
        if let Ok(sp) = self.sports() {
            for g in sp.games.iter().filter(|g| g.tw_day >= day && g.tw_day <= day + q["days"].as_i64().unwrap_or(30) as i32) {
                let mut v = sp.game_json(g, i32::MAX, 0);
                v["open"] = json!(Sports::open_for_bets(g.tw_day, day));
                v.as_object_mut().unwrap().remove("markets");
                sports.push(v);
            }
        }
        let mut years = Vec::new();
        let (y0, _, _) = dates::civil_from_days(day);
        for (m, code) in [(Mkt::Cn, "sh.000001"), (Mkt::Us, "SPY")] {
            let Ok(mk) = self.market(m) else { continue };
            let Ok(bars) = mk.bars(code) else { continue };
            for y in y0..=y0 + 2 {
                let yb: Vec<&Bar> = bars.iter().filter(|b| dates::civil_from_days(b.day).0 == y).collect();
                let (Some(f), Some(l)) = (yb.first(), yb.last()) else { continue };
                let big: Vec<Value> = yb.iter().filter(|b| b.pc > 0 && ((b.c - b.pc) as f64 / b.pc as f64).abs() >= 0.05).map(|b| json!({"date": dates::format(b.day), "pct": (b.c - b.pc) as f64 / b.pc as f64 * 100.0})).collect();
                years.push(json!({"market": m.key(), "index": mk.meta(code).map(|x| x.name.clone()).unwrap_or_default(), "year": y, "pct": (l.c - f.o) as f64 / f.o as f64 * 100.0, "big_days": big}));
            }
        }
        let mut stocks = Vec::new();
        for m in [Mkt::Cn, Mkt::Us] {
            let Ok(mk) = self.market(m) else { continue };
            for y in y0..=y0 + 1 {
                let mut rows = Vec::new();
                for code in universe(m) {
                    let Ok(bars) = mk.bars(code) else { continue };
                    let yb: Vec<&Bar> = bars.iter().filter(|b| dates::civil_from_days(b.day).0 == y).collect();
                    let (Some(f), Some(l)) = (yb.first(), yb.last()) else { continue };
                    // A-share prices are raw; adjust the year's return for splits inside it.
                    let split: f64 = yb.iter().map(|b| b.split as f64).product::<f64>();
                    rows.push((code.to_string(), mk.meta(code).map(|x| x.name.clone()).unwrap_or_default(), (l.c as f64 * split / f.o as f64 - 1.0) * 100.0));
                }
                rows.sort_by(|a, b| b.2.partial_cmp(&a.2).unwrap());
                for r in rows.iter().take(3) {
                    stocks.push(json!({"market": m.key(), "year": y, "code": r.0, "name": r.1, "pct": r.2}));
                }
            }
        }
        let _ = minute;
        Ok(json!({"sports": sports, "index_years": years, "stocks": stocks}))
    }

    /// Finds what a spoken prediction refers to: a team's next game, or a stock.
    fn resolve(&self, q: &Value) -> Result<Value, String> {
        let (day, _) = now_of(q)?;
        let text = q["name"].as_str().unwrap_or("").trim().to_lowercase();
        if text.is_empty() {
            return Ok(json!({"found": false}));
        }
        if q["type"].as_str() == Some("sports") {
            let sp = self.sports()?;
            let matches = |comp: sports::Comp, code: &str| code.to_lowercase() == text || sp.name(comp, code).to_lowercase().contains(&text) || (text.chars().count() >= 2 && sp.name(comp, code).contains(q["name"].as_str().unwrap_or("")));
            for g in sp.games.iter().filter(|g| g.tw_day >= day) {
                for (side, code) in [("home", &g.home), ("away", &g.away)] {
                    if matches(g.comp, code) {
                        return Ok(json!({"found": true, "target": g.id, "side": side, "team": code, "name": sp.name(g.comp, code), "date": dates::format(g.tw_day), "game": sp.game_json(g, day, 0)}));
                    }
                }
            }
            return Ok(json!({"found": false}));
        }
        for m in [Mkt::Cn, Mkt::Us] {
            let Ok(mk) = self.market(m) else { continue };
            let live = || mk.stocks.iter().filter(|x| x.first <= day && x.last >= day);
            let hit = live().find(|x| mk.symbol_on(&x.code, day).to_lowercase() == text || (m == Mkt::Cn && x.code.to_lowercase() == text))
                .or_else(|| live().find(|x| mk.name_on(&x.code, day).to_lowercase().contains(&text)));
            if let Some(x) = hit {
                return Ok(json!({"found": true, "market": m.key(), "code": x.code, "name": mk.name_on(&x.code, day)}));
            }
        }
        Ok(json!({"found": false}))
    }

    fn predict(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let heroine = q["heroine"].as_str().unwrap_or("").to_string();
        let audience: Vec<String> = q["audience"].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect()).unwrap_or_default();
        let kind = str_of(q, "type")?.to_string();
        let id = b.id();
        let mut p = Prediction { id, heroine, audience, kind: kind.clone(), made_day: day, status: "open".into(), text: q["text"].as_str().unwrap_or("").into(), ..Default::default() };
        match kind.as_str() {
            "sports" => {
                let r = self.resolve(&json!({"type": "sports", "name": q["name"], "date": dates::format(day)}))?;
                if r["found"] != true {
                    b.next_id -= 1;
                    return Ok(json!({"ok": false, "why": "找不到這支隊伍近期的比賽"}));
                }
                let win = q["outcome"].as_str().unwrap_or("win") != "lose";
                let side = r["side"].as_str().unwrap_or("home");
                p.target = r["target"].as_str().unwrap_or("").into();
                p.market = "ml".into();
                p.pick = if win == (side == "home") { "home".into() } else { "away".into() };
                p.due_day = dates::parse(r["date"].as_str().unwrap_or("")).unwrap_or(day);
                if p.text.is_empty() {
                    p.text = format!("{} {} 會{}", r["date"].as_str().unwrap_or(""), r["name"].as_str().unwrap_or(""), if win { "贏" } else { "輸" });
                }
            }
            "stock" => {
                let r = self.resolve(&json!({"type": "stock", "name": q["name"], "date": dates::format(day)}))?;
                if r["found"] != true {
                    b.next_id -= 1;
                    return Ok(json!({"ok": false, "why": "找不到這檔股票"}));
                }
                let m = Mkt::parse(r["market"].as_str().unwrap_or("cn")).unwrap_or(Mkt::Cn);
                let mk = self.market(m)?;
                let code = r["code"].as_str().unwrap_or("").to_string();
                p.ref_price = self.price_now(&mk, &code, day, minute).unwrap_or(0);
                p.target = code;
                p.market = m.key().into();
                p.direction = q["direction"].as_str().unwrap_or("up").into();
                p.due_day = day + q["days"].as_i64().unwrap_or(7).clamp(1, 365) as i32;
                if p.text.is_empty() {
                    p.text = format!("{} 在 {} 前會{}", r["name"].as_str().unwrap_or(""), dates::format(p.due_day), match p.direction.as_str() { "down" => "跌", "limit_up" => "漲停", _ => "漲" });
                }
            }
            "lotto" => {
                // Lotto is pure luck: a "sure" number is a prediction that settles as a miss at the draw.
                let g = Lotto::parse(q["game"].as_str().unwrap_or("star649")).unwrap_or(Lotto::Star649);
                p.due_day = lottery::next_draw(g, day, minute).1;
                p.target = g.key().into();
            }
            _ => return Err("fin: type".into()),
        }
        let out = json!({"ok": true, "id": p.id, "text": p.text, "due": dates::format(p.due_day)});
        b.predictions.push(p);
        Ok(out)
    }

    fn settle_stock_prediction(&self, p: &Prediction, day: i32, minute: i32) -> Option<bool> {
        let m = Mkt::parse(&p.market)?;
        let mk = self.market(m).ok()?;
        let clock = mk.clock(day, minute);
        if clock.complete_through < p.due_day && day <= p.due_day {
            return None;
        }
        let bars = mk.bars(&p.target).ok()?;
        let window: Vec<&Bar> = bars.iter().filter(|b| b.day > p.made_day && b.day <= p.due_day.min(clock.complete_through)).collect();
        let last = window.last()?;
        Some(match p.direction.as_str() {
            "down" => last.c < p.ref_price,
            "limit_up" => window.iter().any(|b| b.lu > 0 && b.c >= b.lu),
            _ => last.c > p.ref_price,
        })
    }

    /// A heroine's own trade (plan §15.2 女主自己炒股): rules pick the stock, real closes settle it.
    fn npc_trade(&self, b: &mut Book, q: &Value) -> Result<Value, String> {
        let (day, minute) = now_of(q)?;
        let heroine = str_of(q, "heroine")?.to_string();
        let m = Mkt::parse(q["market"].as_str().unwrap_or("cn")).unwrap_or(Mkt::Cn);
        let twd = q["twd"].as_i64().unwrap_or(0);
        if twd <= 0 {
            return Err("fin: twd".into());
        }
        let style = q["style"].as_str().unwrap_or("random");
        let mk = self.market(m)?;
        let clock = mk.clock(day, minute);
        let tip = q["code"].as_str().map(String::from);
        let code = match (tip, style) {
            (Some(c), _) if mk.meta(&c).is_some() => c,
            (_, "chase") => {
                // Yesterday's biggest gainer in the list she watches.
                let mut best = (String::new(), f64::MIN);
                for c in universe(m) {
                    if let Ok(bars) = mk.bars(c) {
                        let i = bars.partition_point(|x| x.day <= clock.complete_through);
                        if i > 0 && bars[i - 1].pc > 0 {
                            let pct = (bars[i - 1].c - bars[i - 1].pc) as f64 / bars[i - 1].pc as f64;
                            if pct > best.1 {
                                best = (c.to_string(), pct);
                            }
                        }
                    }
                }
                best.0
            }
            (_, "safe") => if m == Mkt::Us { "SPY".into() } else { "sh.601318".into() },
            _ => {
                let mut r = dates::Rng::new(dates::hash(&heroine) ^ day as u64);
                universe(m)[r.range(0, universe(m).len() as i64 - 1) as usize].to_string()
            }
        };
        let px = self.close_on_or_before(&mk, &code, clock.complete_through).ok_or("no_price")?;
        let fx = self.fx(m, day).unwrap_or(1.0);
        let qty = twd as f64 / fx / (px as f64 / 1000.0);
        let hold = q["days"].as_i64().unwrap_or(if style == "chase" { 3 } else { 20 }) as i32;
        let id = b.id();
        b.npc.push(NpcPosition { id, heroine: heroine.clone(), market: m.key().into(), code: code.clone(), qty, entry: px, entry_day: day, exit_day: day + hold, twd_in: twd });
        let name = mk.name_on(&code, day);
        b.log(day, minute, "npc_stock", -twd, format!("{heroine} 買了 {name}"));
        Ok(json!({"id": id, "code": code, "name": name, "exit": dates::format(day + hold)}))
    }
}

fn fill(a: &mut Account, m: Mkt, o: &mut Order, bar: &Bar, price: i32, day: i32, minute: i32, order_id: i64) -> Option<Value> {
    let key = format!("{}@{}", o.code, bar.day);
    let cap = ((bar.vol as f64) * 0.01).floor() as i64;
    let used = *a.filled.get(&key).unwrap_or(&0);
    let mut room = (cap - used).max(0);
    if m == Mkt::Cn && o.side == "buy" {
        room -= room % 100;
    }
    let want = o.qty - o.filled;
    let q = want.min(room);
    if q <= 0 {
        o.note = "已達當日可成交量上限（真實成交量的 1%）".into();
        return None;
    }
    let value = q * price as i64;
    let (fee, tax) = Finance::fees(m, &o.side, &o.code, value, day);
    if o.side == "buy" {
        let cost = value + fee;
        // Pay from the frozen reserve; any shortfall from cash.
        let from_frozen = cost.min(o.frozen);
        o.frozen -= from_frozen;
        a.frozen -= from_frozen;
        a.cash -= cost - from_frozen;
        let p = a.positions.entry(o.code.clone()).or_default();
        // Buying back a short first (US), then any rest opens or adds to a long.
        let covering = if p.qty < 0 { q.min(-p.qty) } else { 0 };
        if covering > 0 {
            p.cost = p.cost * (p.qty + covering) / p.qty;
            p.qty += covering;
        }
        let opening = q - covering;
        p.qty += opening;
        p.cost += if covering > 0 { opening * price as i64 } else { cost };
        if m == Mkt::Cn {
            p.today += q;
        }
        if p.qty == 0 {
            a.positions.remove(&o.code);
        }
    } else {
        let p = a.positions.entry(o.code.clone()).or_default();
        let closing = q.min(p.qty.max(0));
        if closing > 0 {
            let share_cost = p.cost * closing / p.qty;
            p.qty -= closing;
            p.cost -= share_cost;
        }
        // The rest is a short sale (US only; A-share orders never get here): its basis is the proceeds, negative.
        let opening = q - closing;
        p.qty -= opening;
        p.cost -= opening * price as i64;
        a.cash += value - fee - tax;
        if p.qty == 0 {
            a.positions.remove(&o.code);
        }
    }
    *a.filled.entry(key).or_insert(0) += q;
    let total_before = o.avg as i64 * o.filled;
    o.filled += q;
    o.avg = ((total_before + price as i64 * q) / o.filled) as i32;
    if o.filled >= o.qty {
        o.status = "filled".into();
        o.note.clear();
        if o.frozen > 0 {
            a.cash += o.frozen;
            a.frozen -= o.frozen;
            o.frozen = 0;
        }
    }
    a.trades.push_back(Trade { id: order_id, order: o.id, code: o.code.clone(), side: o.side.clone(), qty: q, price, fee, tax, day, minute });
    while a.trades.len() > 500 {
        a.trades.pop_front();
    }
    Some(json!({"kind": "order_fill", "market": m.key(), "order": o.id, "code": o.code, "side": o.side, "qty": q, "price": price as f64 / 1000.0, "status": o.status}))
}

fn depth(price: i32, lu: i32, ld: i32, code: &str, day: i32, i: usize) -> Value {
    let mut r = dates::Rng::new(dates::hash(code) ^ (day as u64) << 9 ^ i as u64);
    let tick = 10;
    let mut asks = Vec::new();
    let mut bids = Vec::new();
    for k in 1..=5 {
        let a = price + tick * k;
        let bd = price - tick * (k - 1);
        if lu == 0 || a <= lu {
            asks.push(json!({"price": a as f64 / 1000.0, "qty": r.range(1, 400) * 100}));
        }
        if ld == 0 || bd >= ld {
            bids.push(json!({"price": bd as f64 / 1000.0, "qty": r.range(1, 400) * 100}));
        }
    }
    json!({"asks": asks, "bids": bids, "simulated": true})
}

fn aggregate(list: &[Bar], key: impl Fn(i32) -> i32) -> Vec<Bar> {
    let mut out: Vec<Bar> = Vec::new();
    let mut last_key = i32::MIN;
    for b in list {
        let k = key(b.day);
        if k != last_key || out.is_empty() {
            out.push(*b);
            last_key = k;
        } else {
            let o = out.last_mut().unwrap();
            o.h = o.h.max(b.h);
            o.l = o.l.min(b.l);
            o.c = b.c;
            o.vol += b.vol;
            o.amount += b.amount;
            o.day = b.day;
        }
    }
    out
}

pub fn session_json(mk: &Market, day: i32, minute: i32) -> Value {
    let c = mk.clock(day, minute);
    let text = match c.live {
        Some(_) => "交易中".to_string(),
        None => format!("休市，下次開市 {} {:02}:{:02}", dates::format(c.next_open.0), c.next_open.1 / 60, c.next_open.1 % 60),
    };
    let mut v = json!({"open": c.live.is_some(), "text": text, "next_open": dates::format(c.next_open.0), "next_open_minute": c.next_open.1, "data_through": dates::format(c.complete_through)});
    if mk.mkt == Mkt::Us {
        let us_day = c.live.map(|x| x.0).unwrap_or(day);
        v["dst"] = json!(dates::us_dst(us_day));
        v["open_tw"] = json!(dates::us_open_tw_minute(us_day));
        if let Some((d, f)) = c.live {
            let et = 570 + (f * 390.0) as i32;
            v["et"] = json!(format!("{} {:02}:{:02}", dates::format(d), et / 60, et % 60));
        }
    }
    v
}

pub fn ticket_json(t: &Ticket) -> Value {
    json!({"id": t.id, "kind": t.kind, "game": t.game, "draw_no": t.draw_no, "draw_date": if t.draw_day > 0 { dates::format(t.draw_day) } else { String::new() },
           "lines": t.lines.iter().map(|(n, z)| json!({"numbers": n, "zone2": z})).collect::<Vec<_>>(),
           "selections": t.sels.iter().map(|s| json!({"target": s.target, "market": s.market, "pick": s.pick, "odds": s.odds, "line": s.line, "label": s.label, "result": s.result, "void": s.void})).collect::<Vec<_>>(),
           "stake": t.stake, "odds": t.odds, "status": t.status, "payout": t.payout, "prize": t.prize, "date": dates::format(t.day), "cells": t.cells, "with": t.with})
}

#[cfg(test)]
mod tests;
