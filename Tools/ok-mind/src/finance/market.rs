//! Daily bars for the A-share (cn) and US (us) markets in .okbar files (Docs/STREET_SOCIAL_MARKET_DATA_FORMAT.md),
//! sessions on the game clock (plan §15.8) and a deterministic intraday path through the real O/H/L/C.
//! Nothing after the game's "now" is ever returned.
//! When MarketData/market.sqlite exists (Tools/build_market_sqlite.lua) everything is read from it, including
//! quarterly fundamentals; otherwise the per-symbol .okbar files are used.
use super::dates::{self, Rng};
use serde_json::{json, Value};
use std::collections::{HashMap, VecDeque};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mkt {
    Cn,
    Us,
}

impl Mkt {
    pub fn parse(s: &str) -> Option<Mkt> {
        match s {
            "cn" => Some(Mkt::Cn),
            "us" => Some(Mkt::Us),
            _ => None,
        }
    }
    pub fn key(self) -> &'static str {
        match self {
            Mkt::Cn => "cn",
            Mkt::Us => "us",
        }
    }
    pub fn currency(self) -> &'static str {
        match self {
            Mkt::Cn => "CNY",
            Mkt::Us => "USD",
        }
    }
    /// Session length in minutes and points on the intraday path (one per minute, both ends).
    pub fn session_minutes(self) -> i32 {
        match self {
            Mkt::Cn => 240, // 09:30–11:30 and 13:00–15:00 (Shanghai, Shenzhen)
            Mkt::Us => 390, // 09:30–16:00 ET
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Bar {
    pub day: i32,
    pub o: i32,
    pub h: i32,
    pub l: i32,
    pub c: i32,
    pub pc: i32,
    pub vol: i64,
    pub amount: f64,
    pub split: f32,
    pub div: f32,
    pub lu: i32,
    pub ld: i32,
    pub flags: u8,
}

impl Bar {
    pub fn suspended(&self) -> bool {
        self.flags & 1 != 0
    }
    pub fn st(&self) -> bool {
        self.flags & 2 != 0
    }
}

#[derive(Clone, Debug)]
pub struct Meta {
    pub code: String,
    pub symbol: String,
    pub name: String,
    pub board: String,
    pub is_index: bool,
    pub etf: bool,
    pub first: i32,
    pub last: i32,
    /// "raw" (as traded: the game applies split/dividend) or e.g. "split_adjusted_thetrading_tools".
    pub basis: String,
}

/// One reported period (cumulative year to date), visible from `available` on.
#[derive(Clone, Debug, Default)]
pub struct Fund {
    pub period_end: i32,
    pub period: String,
    pub available: i32,
    pub currency: String,
    pub revenue: Option<f64>,
    pub net_income: Option<f64>,
    pub eps: Option<f64>,
    pub eps_ttm: Option<f64>,
    pub bvps: Option<f64>,
    pub roe: Option<f64>,
    pub total_shares: Option<f64>,
    pub equity: Option<f64>,
    pub total_assets: Option<f64>,
    pub operating_cf: Option<f64>,
    pub dividend_ps: Option<f64>,
}

impl Fund {
    pub fn json(&self) -> Value {
        json!({"period_end": dates::format(self.period_end), "period": self.period, "available": dates::format(self.available), "currency": self.currency,
               "revenue": self.revenue, "net_income": self.net_income, "eps": self.eps, "eps_ttm": self.eps_ttm, "bvps": self.bvps, "roe": self.roe,
               "total_shares": self.total_shares, "equity": self.equity, "total_assets": self.total_assets, "operating_cf": self.operating_cf, "dividend_ps": self.dividend_ps})
    }
}

pub fn db_path(root: &Path) -> PathBuf {
    root.join("market.sqlite")
}

fn open_db(root: &Path) -> Option<rusqlite::Connection> {
    let p = db_path(root);
    if !p.exists() {
        return None;
    }
    rusqlite::Connection::open_with_flags(&p, rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY | rusqlite::OpenFlags::SQLITE_OPEN_NO_MUTEX).ok()
}

/// Daily rates of a pair ("usd_twd", "cny_twd") from market.sqlite, if it has them.
pub fn read_fx_db(root: &Path, pair: &str) -> Option<Vec<(i32, f64)>> {
    let db = open_db(root)?;
    let mut st = db.prepare("SELECT day, rate FROM fx WHERE pair=?1 ORDER BY day").ok()?;
    let rows: Vec<(i32, f64)> = st.query_map([pair], |r| Ok((dates::from_ymd(r.get::<_, i32>(0)?), r.get::<_, f64>(1)?))).ok()?.filter_map(Result::ok).collect();
    (!rows.is_empty()).then_some(rows)
}

fn decompress(path: &Path) -> Result<Vec<u8>, String> {
    let file = std::fs::File::open(path).map_err(|e| format!("market_missing {}: {e}", path.display()))?;
    let mut reader = std::io::BufReader::new(file);
    let mut dec = ruzstd::StreamingDecoder::new(&mut reader).map_err(|e| format!("market_zstd {}: {e}", path.display()))?;
    let mut out = Vec::new();
    dec.read_to_end(&mut out).map_err(|e| format!("market_zstd {}: {e}", path.display()))?;
    Ok(out)
}

fn i32_at(b: &[u8], o: usize) -> i32 {
    i32::from_le_bytes(b[o..o + 4].try_into().unwrap())
}

pub fn read_okbar(path: &Path) -> Result<Vec<Bar>, String> {
    let b = decompress(path)?;
    if b.len() < 16 || &b[0..4] != b"OKBR" {
        return Err(format!("market_bad_header {}", path.display()));
    }
    let rows = u32::from_le_bytes(b[8..12].try_into().unwrap()) as usize;
    if b.len() < 16 + rows * 64 {
        return Err(format!("market_truncated {}", path.display()));
    }
    let mut out = Vec::with_capacity(rows);
    for r in 0..rows {
        let o = 16 + r * 64;
        out.push(Bar {
            day: dates::from_ymd(i32_at(&b, o)),
            o: i32_at(&b, o + 4),
            h: i32_at(&b, o + 8),
            l: i32_at(&b, o + 12),
            c: i32_at(&b, o + 16),
            pc: i32_at(&b, o + 20),
            vol: i64::from_le_bytes(b[o + 24..o + 32].try_into().unwrap()),
            amount: f64::from_le_bytes(b[o + 32..o + 40].try_into().unwrap()),
            split: f32::from_le_bytes(b[o + 40..o + 44].try_into().unwrap()),
            div: f32::from_le_bytes(b[o + 44..o + 48].try_into().unwrap()),
            lu: i32_at(&b, o + 48),
            ld: i32_at(&b, o + 52),
            flags: b[o + 56],
        });
    }
    Ok(out)
}

pub fn read_okfx(path: &Path) -> Result<Vec<(i32, f64)>, String> {
    let b = decompress(path)?;
    if b.len() < 12 || &b[0..4] != b"OKFX" {
        return Err(format!("fx_bad_header {}", path.display()));
    }
    let rows = u32::from_le_bytes(b[8..12].try_into().unwrap()) as usize;
    let mut out = Vec::with_capacity(rows);
    for r in 0..rows {
        let o = 12 + r * 12;
        if o + 12 > b.len() {
            break;
        }
        out.push((dates::from_ymd(i32_at(&b, o)), f64::from_le_bytes(b[o + 4..o + 12].try_into().unwrap())));
    }
    Ok(out)
}

/// Where the market stands at a game moment.
#[derive(Clone, Copy, Debug)]
pub struct Clock {
    /// Exchange date whose session is running now, with the fraction elapsed.
    pub live: Option<(i32, f64)>,
    /// What the screens show: the running session, or over the midday break the morning as it closed.
    pub view: Option<(i32, f64)>,
    /// Bars with day <= this are complete and visible.
    pub complete_through: i32,
    /// Next session open in Taiwan (day, minute).
    pub next_open: (i32, i32),
}

pub struct Market {
    pub mkt: Mkt,
    dir: PathBuf,
    /// market.sqlite and each code's symbols.id, when the database is used.
    db: Option<Mutex<rusqlite::Connection>>,
    sid: HashMap<String, i64>,
    /// Name and ticker history (market.sqlite `names`): (first day, name, ticker or None), oldest first.
    names: HashMap<String, Vec<(i32, String, Option<String>)>>,
    pub stocks: Vec<Meta>,
    by_code: HashMap<String, usize>,
    pub calendar: Vec<i32>,
    cache: Mutex<(HashMap<String, Arc<Vec<Bar>>>, VecDeque<String>)>,
}

const CACHE_CAP: usize = 192;

impl Market {
    pub fn load(root: &Path, mkt: Mkt) -> Result<Market, String> {
        if let Some(db) = open_db(root) {
            if let Some(m) = Self::load_db(root, mkt, db)? {
                return Ok(m);
            }
        }
        let dir = root.join(mkt.key());
        let text = std::fs::read_to_string(dir.join("stocks.json")).map_err(|_| format!("market_data_missing: {}資料還沒有準備好（MarketData/{}），請先用 Tools/build_{}_data.py 產生", if mkt == Mkt::Cn { "A 股" } else { "美股" }, mkt.key(), if mkt == Mkt::Cn { "ashare" } else { "us" }))?;
        let rows: Vec<Value> = serde_json::from_str(&text).map_err(|e| format!("market_stocks_invalid: {e}"))?;
        let mut stocks = Vec::new();
        for r in rows {
            let ymd = |k: &str| r[k].as_str().and_then(|s| s.parse::<i32>().ok()).map(dates::from_ymd).unwrap_or(0);
            let name = r["name_zh_tw"].as_str().filter(|s| !s.is_empty()).or(r["name"].as_str()).unwrap_or("").to_string();
            stocks.push(Meta {
                code: r["code"].as_str().unwrap_or("").to_string(),
                symbol: r["symbol"].as_str().unwrap_or("").to_string(),
                name,
                board: r["board"].as_str().unwrap_or("").to_string(),
                is_index: r["is_index"].as_bool().unwrap_or(false),
                etf: r["etf"].as_bool().unwrap_or(false),
                first: ymd("first"),
                last: ymd("last"),
                basis: "raw".into(),
            });
        }
        let by_code = stocks.iter().enumerate().map(|(i, m)| (m.code.clone(), i)).collect();
        let cal_text = std::fs::read_to_string(dir.join("calendar.json")).map_err(|e| format!("market_missing calendar: {e}"))?;
        let cal: Vec<String> = serde_json::from_str(&cal_text).map_err(|e| format!("market_calendar_invalid: {e}"))?;
        let mut calendar: Vec<i32> = cal.iter().filter_map(|s| dates::parse(s)).collect();
        calendar.sort();
        Ok(Market { mkt, dir, db: None, sid: HashMap::new(), names: HashMap::new(), stocks, by_code, calendar, cache: Mutex::new((HashMap::new(), VecDeque::new())) })
    }

    /// From market.sqlite; None when it has no symbols for this market (then the .okbar files are used).
    fn load_db(root: &Path, mkt: Mkt, db: rusqlite::Connection) -> Result<Option<Market>, String> {
        let e = |x: rusqlite::Error| format!("market_db: {x}");
        let mut stocks = Vec::new();
        let mut sid = HashMap::new();
        {
            let mut st = db.prepare("SELECT id, code, symbol, name, name_zh_tw, board, is_index, etf, first, last, price_basis FROM symbols WHERE market=?1 ORDER BY id").map_err(e)?;
            let rows = st
                .query_map([mkt.key()], |r| {
                    let ymd = |v: Option<i32>| v.filter(|&x| x > 0).map(dates::from_ymd).unwrap_or(0);
                    let zh: Option<String> = r.get(4)?;
                    let name: Option<String> = r.get(3)?;
                    Ok((
                        r.get::<_, i64>(0)?,
                        Meta {
                            code: r.get(1)?,
                            symbol: r.get::<_, Option<String>>(2)?.unwrap_or_default(),
                            name: zh.filter(|s| !s.is_empty()).or(name).unwrap_or_default(),
                            board: r.get::<_, Option<String>>(5)?.unwrap_or_default(),
                            is_index: r.get::<_, i64>(6)? != 0,
                            etf: r.get::<_, i64>(7)? != 0,
                            first: ymd(r.get(8)?),
                            last: ymd(r.get(9)?),
                            basis: r.get::<_, Option<String>>(10)?.unwrap_or_else(|| "raw".into()),
                        },
                    ))
                })
                .map_err(e)?;
            for row in rows {
                let (id, m) = row.map_err(e)?;
                sid.insert(m.code.clone(), id);
                stocks.push(m);
            }
        }
        if stocks.is_empty() {
            return Ok(None);
        }
        let mut calendar: Vec<i32> = {
            let mut st = db.prepare("SELECT day FROM calendar WHERE market=?1").map_err(e)?;
            let days = st.query_map([mkt.key()], |r| r.get::<_, i32>(0)).map_err(e)?.filter_map(Result::ok).map(dates::from_ymd).collect();
            days
        };
        calendar.sort();
        let by_code = stocks.iter().enumerate().map(|(i, m)| (m.code.clone(), i)).collect();
        // Optional: Tools/build_stock_names.lua. Without it names fall back to today's (ST prefix fixed per day).
        let mut names: HashMap<String, Vec<(i32, String, Option<String>)>> = HashMap::new();
        {
            let code_of: HashMap<i64, &str> = sid.iter().map(|(c, &i)| (i, c.as_str())).collect();
            if let Ok(mut st) = db.prepare("SELECT sid, start, name, symbol FROM names ORDER BY sid, start") {
                if let Ok(rows) = st.query_map([], |r| Ok((r.get::<_, i64>(0)?, r.get::<_, i32>(1)?, r.get::<_, String>(2)?, r.get::<_, Option<String>>(3)?))) {
                    for (id, start, name, symbol) in rows.filter_map(Result::ok) {
                        if let Some(code) = code_of.get(&id) {
                            names.entry(code.to_string()).or_default().push((dates::from_ymd(start), name, symbol.filter(|s| !s.is_empty())));
                        }
                    }
                }
            }
        }
        Ok(Some(Market { mkt, dir: root.join(mkt.key()), db: Some(Mutex::new(db)), sid, names, stocks, by_code, calendar, cache: Mutex::new((HashMap::new(), VecDeque::new())) }))
    }

    fn bars_db(&self, code: &str) -> Result<Vec<Bar>, String> {
        let id = *self.sid.get(code).ok_or_else(|| format!("unknown_symbol {code}"))?;
        let db = self.db.as_ref().unwrap().lock().unwrap();
        let mut st = db.prepare_cached("SELECT day, o, h, l, c, pc, vol, amount, split, div, lu, ld, flags FROM bars WHERE sid=?1 ORDER BY day").map_err(|x| format!("market_db: {x}"))?;
        let rows = st
            .query_map([id], |r| {
                Ok(Bar {
                    day: dates::from_ymd(r.get(0)?),
                    o: r.get(1)?,
                    h: r.get(2)?,
                    l: r.get(3)?,
                    c: r.get(4)?,
                    pc: r.get(5)?,
                    vol: r.get(6)?,
                    amount: r.get(7)?,
                    split: r.get::<_, f64>(8)? as f32,
                    div: r.get::<_, f64>(9)? as f32,
                    lu: r.get(10)?,
                    ld: r.get(11)?,
                    flags: r.get::<_, i64>(12)? as u8,
                })
            })
            .map_err(|x| format!("market_db: {x}"))?;
        let out = rows.collect::<Result<Vec<_>, _>>().map_err(|x| format!("market_db: {x}"));
        out
    }

    /// Reported periods of a symbol published on or before `day`, oldest first (empty without the database).
    pub fn fundamentals(&self, code: &str, day: i32) -> Vec<Fund> {
        let (Some(db), Some(&id)) = (self.db.as_ref(), self.sid.get(code)) else { return Vec::new() };
        let db = db.lock().unwrap();
        let Ok(mut st) = db.prepare_cached(
            "SELECT period_end, period, available, currency, revenue, net_income, eps, eps_ttm, bvps, roe, total_shares, equity, total_assets, operating_cf, dividend_ps
             FROM fundamentals WHERE sid=?1 AND available<=?2 ORDER BY period_end, period",
        ) else {
            return Vec::new();
        };
        let rows = st.query_map(rusqlite::params![id, dates::ymd(day)], |r| {
            Ok(Fund {
                period_end: dates::from_ymd(r.get(0)?),
                period: r.get(1)?,
                available: dates::from_ymd(r.get(2)?),
                currency: r.get(3)?,
                revenue: r.get(4)?,
                net_income: r.get(5)?,
                eps: r.get(6)?,
                eps_ttm: r.get(7)?,
                bvps: r.get(8)?,
                roe: r.get(9)?,
                total_shares: r.get(10)?,
                equity: r.get(11)?,
                total_assets: r.get(12)?,
                operating_cf: r.get(13)?,
                dividend_ps: r.get(14)?,
            })
        });
        let out = rows.map(|it| it.filter_map(Result::ok).collect()).unwrap_or_default();
        out
    }

    pub fn meta(&self, code: &str) -> Option<&Meta> {
        self.by_code.get(code).map(|&i| &self.stocks[i])
    }

    fn history_on(&self, code: &str, day: i32) -> Option<&(i32, String, Option<String>)> {
        let h = self.names.get(code)?;
        let i = h.partition_point(|x| x.0 <= day);
        h.get(i.saturating_sub(1))
    }

    /// The name the stock had on `day` — never a later rename or a later ST tag.
    pub fn name_on(&self, code: &str, day: i32) -> String {
        if let Some(h) = self.history_on(code, day) {
            return h.1.clone();
        }
        let Some(m) = self.meta(code) else { return String::new() };
        if self.mkt != Mkt::Cn {
            return m.name.clone();
        }
        // Today's name carries today's ST state: take it off, and put "ST" back only when that day was ST.
        let base = strip_st(&m.name);
        let st = self.bars(code).ok().and_then(|b| {
            let i = b.partition_point(|x| x.day <= day);
            (i > 0).then(|| b[i - 1].st())
        });
        if st == Some(true) { format!("ST{base}") } else { base }
    }

    /// The ticker shown on `day` (US tickers change: GOOG/GOOGL, FB/META); A shares keep their code.
    pub fn symbol_on(&self, code: &str, day: i32) -> String {
        if let Some(s) = self.history_on(code, day).and_then(|h| h.2.clone()) {
            return s;
        }
        self.meta(code).map(|m| m.symbol.clone()).unwrap_or_default()
    }

    pub fn trading_day(&self, day: i32) -> bool {
        self.calendar.binary_search(&day).is_ok()
    }

    fn file_of(&self, m: &Meta) -> PathBuf {
        let name = m.code.replace(['.', '/'], "-");
        let name = if self.mkt == Mkt::Cn { m.code.clone() } else { name };
        let sub = if m.is_index || (self.mkt == Mkt::Us && ["SPY", "QQQ", "DIA", "IWM"].contains(&m.code.as_str())) { "index" } else { "daily" };
        let p = self.dir.join(sub).join(format!("{name}.okbar"));
        if p.exists() { p } else { self.dir.join(if sub == "index" { "daily" } else { "index" }).join(format!("{name}.okbar")) }
    }

    pub fn bars(&self, code: &str) -> Result<Arc<Vec<Bar>>, String> {
        {
            let mut c = self.cache.lock().unwrap();
            if let Some(b) = c.0.get(code).cloned() {
                c.1.retain(|k| k != code);
                c.1.push_back(code.to_string());
                return Ok(b);
            }
        }
        let m = self.meta(code).ok_or_else(|| format!("unknown_symbol {code}"))?;
        let bars = Arc::new(if self.db.is_some() { self.bars_db(code)? } else { read_okbar(&self.file_of(m))? });
        let mut c = self.cache.lock().unwrap();
        c.0.insert(code.to_string(), bars.clone());
        c.1.push_back(code.to_string());
        while c.1.len() > CACHE_CAP {
            if let Some(old) = c.1.pop_front() {
                c.0.remove(&old);
            }
        }
        Ok(bars)
    }

    /// Market clock for a Taiwan (day, minute).
    pub fn clock(&self, day: i32, minute: i32) -> Clock {
        let len = self.mkt.session_minutes();
        match self.mkt {
            Mkt::Cn => {
                // Two sittings with the midday break between: the fraction counts trading minutes only.
                let (open, noon, resume, close) = (9 * 60 + 30, 11 * 60 + 30, 13 * 60, 15 * 60);
                let trading = self.trading_day(day);
                let half = (noon - open) as f64 / len as f64;
                let live = if trading && minute >= open && minute < noon {
                    Some((day, (minute - open) as f64 / len as f64))
                } else if trading && minute >= resume && minute < close {
                    Some((day, half + (minute - resume) as f64 / len as f64))
                } else {
                    None
                };
                let lunch = trading && minute >= noon && minute < resume;
                let view = if lunch { Some((day, half)) } else { live };
                let complete = if trading && minute >= close { day } else { self.prev_trading(day) };
                let next = if trading && minute < open { (day, open) } else if lunch { (day, resume) } else { (self.next_trading(day), open) };
                Clock { live, view, complete_through: complete, next_open: next }
            }
            Mkt::Us => {
                let mut live = None;
                let o = dates::us_open_tw_minute(day);
                if self.trading_day(day) && minute >= o {
                    live = Some((day, ((minute - o) as f64 / len as f64).min(1.0)));
                }
                let y = day - 1;
                let oy = dates::us_open_tw_minute(y);
                if self.trading_day(y) && minute + 1440 < oy + len {
                    live = Some((y, (minute + 1440 - oy) as f64 / len as f64));
                }
                // complete: last US day whose close (Taiwan) has passed.
                let mut complete = day;
                loop {
                    if self.trading_day(complete) {
                        let close_day = complete + 1;
                        let close_min = dates::us_open_tw_minute(complete) + len - 1440;
                        if (day, minute) >= (close_day, close_min) {
                            break;
                        }
                    }
                    complete -= 1;
                    if complete < day - 30 {
                        break;
                    }
                }
                let mut nd = day;
                let next = loop {
                    if self.trading_day(nd) {
                        let om = dates::us_open_tw_minute(nd);
                        if (nd, om) > (day, minute) {
                            break (nd, om);
                        }
                    }
                    nd += 1;
                    if nd > day + 30 {
                        break (nd, 0);
                    }
                };
                Clock { live, view: live, complete_through: complete, next_open: next }
            }
        }
    }

    pub fn prev_trading(&self, day: i32) -> i32 {
        let i = self.calendar.partition_point(|&d| d < day);
        if i == 0 { day - 1 } else { self.calendar[i - 1] }
    }
    pub fn next_trading(&self, day: i32) -> i32 {
        let i = self.calendar.partition_point(|&d| d <= day);
        self.calendar.get(i).copied().unwrap_or(day + 1)
    }

    /// The minute path of one bar (prices x1000, rounded to 0.01), session_minutes + 1 points.
    pub fn path(&self, code: &str, b: &Bar) -> Vec<i32> {
        intraday(self.mkt, code, b)
    }
}

fn round_cent(v: f64) -> i32 {
    ((v / 10.0).round() * 10.0) as i32
}

/// A deterministic path from open to close that touches the real high and low (plan §15.2). A close sealed at the
/// limit stays at the limit for the last part of the day; a one-price limit day is flat.
pub fn intraday(mkt: Mkt, code: &str, b: &Bar) -> Vec<i32> {
    let n = mkt.session_minutes() as usize + 1;
    if b.suspended() || (b.o == b.h && b.h == b.l && b.l == b.c) {
        return vec![b.c; n];
    }
    let mut r = Rng::new(dates::hash(code) ^ (b.day as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15));
    let last = n - 1;
    let mut anchors: Vec<(usize, i32)> = vec![(0, b.o)];
    let sealed_up = b.lu > 0 && b.c == b.lu && b.h == b.lu;
    let sealed_down = b.ld > 0 && b.c == b.ld && b.l == b.ld;
    let mut t_hi = if b.o == b.h { 0 } else if b.c == b.h && !sealed_up { last } else { r.range(1, last as i64 - 1) as usize };
    let mut t_lo = if b.o == b.l { 0 } else if b.c == b.l && !sealed_down { last } else { r.range(1, last as i64 - 1) as usize };
    if t_hi == t_lo {
        if t_hi + 1 < last { t_lo = t_hi + 1 } else { t_hi = t_lo - 1 }
    }
    let mut seal: Option<(usize, i32)> = None;
    if sealed_up || sealed_down {
        let lim = if sealed_up { b.lu } else { b.ld };
        let other_t = if sealed_up { t_lo } else { t_hi };
        let start = if b.o == lim { 0 } else { (other_t + 1).max((last as f64 * 0.3) as usize).min(last - 1) };
        let start = start.max(other_t + 1).min(last);
        if sealed_up { t_hi = start } else { t_lo = start }
        seal = Some((start, lim));
    }
    let mut ext = vec![(t_hi, b.h), (t_lo, b.l)];
    ext.sort_by_key(|x| x.0);
    for e in ext {
        if e.0 > 0 && e.0 < last {
            anchors.push(e);
        } else if e.0 == 0 {
            anchors[0].1 = e.1;
        }
    }
    anchors.push((last, b.c));
    anchors.dedup_by_key(|a| a.0);
    let range = (b.h - b.l).max(10) as f64;
    let mut path = vec![b.c; n];
    for w in anchors.windows(2) {
        let (t0, v0) = w[0];
        let (t1, v1) = w[1];
        let steps = t1 - t0;
        if steps == 0 {
            path[t0] = v0;
            continue;
        }
        // Brownian bridge between the anchors.
        let sigma = range * 0.08;
        let mut walk = vec![0.0f64; steps + 1];
        for i in 1..=steps {
            walk[i] = walk[i - 1] + r.normal() * sigma;
        }
        for i in 0..=steps {
            let f = i as f64 / steps as f64;
            let v = v0 as f64 + (v1 - v0) as f64 * f + (walk[i] - walk[steps] * f);
            path[t0 + i] = round_cent(v.clamp(b.l as f64, b.h as f64));
        }
        path[t0] = v0;
        path[t1] = v1;
    }
    if let Some((start, lim)) = seal {
        // Before it seals the price stays off the limit (it touches it only when it seals).
        for (i, p) in path.iter_mut().enumerate() {
            if i >= start {
                *p = lim;
            } else if i > 0 {
                *p = if sealed_up { (*p).min(lim - 10) } else { (*p).max(lim + 10) };
            }
        }
    }
    path
}

/// Exchange rate on a day (last known on or before), TWD per unit.
pub fn fx_on(rates: &[(i32, f64)], day: i32) -> Option<f64> {
    let i = rates.partition_point(|x| x.0 <= day);
    if i == 0 { rates.first().map(|x| x.1) } else { Some(rates[i - 1].1) }
}

/// "*ST五糧" / "ST五糧" / "S*ST五糧" / "SST五糧" / "S五糧" -> "五糧" (A-share status prefixes on a name).
pub fn strip_st(name: &str) -> String {
    let n = name.trim();
    for p in ["S*ST", "SST", "*ST", "ST", "S "] {
        if let Some(rest) = n.strip_prefix(p) {
            if rest.chars().next().is_some_and(|c| !c.is_ascii()) || p == "S " {
                return rest.trim().to_string();
            }
        }
    }
    if let Some(rest) = n.strip_prefix('S') {
        if rest.chars().next().is_some_and(|c| !c.is_ascii()) {
            return rest.to_string();
        }
    }
    n.to_string()
}

pub fn bar_json(b: &Bar) -> Value {
    json!({"date": dates::format(b.day), "o": b.o as f64 / 1000.0, "h": b.h as f64 / 1000.0, "l": b.l as f64 / 1000.0,
           "c": b.c as f64 / 1000.0, "pc": b.pc as f64 / 1000.0, "v": b.vol, "amount": b.amount,
           "lu": b.lu as f64 / 1000.0, "ld": b.ld as f64 / 1000.0, "suspended": b.suspended(), "st": b.st()})
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn path_touches_ohlc_and_seals_limit() {
        let b = Bar { day: 15000, o: 10_000, h: 11_000, l: 9_500, c: 10_500, pc: 10_000, vol: 1000, lu: 11_000, ld: 9_000, split: 1.0, ..Default::default() };
        let p = intraday(Mkt::Cn, "sh.600000", &b);
        assert_eq!(p.len(), 241);
        assert_eq!(p[0], b.o);
        assert_eq!(*p.last().unwrap(), b.c);
        assert_eq!(*p.iter().max().unwrap(), b.h);
        assert_eq!(*p.iter().min().unwrap(), b.l);
        let sealed = Bar { c: 11_000, ..b };
        let p = intraday(Mkt::Cn, "sh.600000", &sealed);
        assert_eq!(*p.last().unwrap(), 11_000);
        let first = p.iter().position(|&v| v == 11_000).unwrap();
        assert!(p[first..].iter().all(|&v| v == 11_000));
        // Sealed after the day's low was made (where that falls is the path's own draw).
        assert!(p[..first].contains(&9_500));
        assert_eq!(*p.iter().min().unwrap(), 9_500);
        let flat = Bar { o: 11_000, h: 11_000, l: 11_000, c: 11_000, ..b };
        assert!(intraday(Mkt::Cn, "x", &flat).iter().all(|&v| v == 11_000));
    }
}
