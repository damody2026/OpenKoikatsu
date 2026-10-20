//! 星光 6/49, 雙星彩 and 幸運刮刮卡 (plan §15.1 / §15.7.2–3). Pure luck: numbers come from the save seed and the
//! draw number at the moment of the draw, so nothing can foresee them (the notebook never lists them).
use super::dates::{self, Rng};
use serde_json::{json, Value};

pub const EPOCH: i32 = 14853; // 2010-09-01

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Lotto {
    Star649,
    TwinStar,
}

impl Lotto {
    pub fn parse(s: &str) -> Option<Lotto> {
        match s {
            "star649" => Some(Lotto::Star649),
            "twinstar" => Some(Lotto::TwinStar),
            _ => None,
        }
    }
    pub fn key(self) -> &'static str {
        match self {
            Lotto::Star649 => "star649",
            Lotto::TwinStar => "twinstar",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Lotto::Star649 => "星光 6/49",
            Lotto::TwinStar => "雙星彩",
        }
    }
    pub fn price(self) -> i64 {
        match self {
            Lotto::Star649 => 10,
            Lotto::TwinStar => 20,
        }
    }
    /// Draw weekdays (0 = Sunday): 6/49 Tue & Fri, 雙星彩 Mon & Thu, both at 20:30.
    pub fn draws_on(self, day: i32) -> bool {
        let wd = dates::weekday(day);
        match self {
            Lotto::Star649 => wd == 2 || wd == 5,
            Lotto::TwinStar => wd == 1 || wd == 4,
        }
    }
    pub fn zone1(self) -> i32 {
        match self {
            Lotto::Star649 => 49,
            Lotto::TwinStar => 38,
        }
    }
    fn base_pool(self) -> i64 {
        match self {
            Lotto::Star649 => 20_000_000,
            Lotto::TwinStar => 40_000_000,
        }
    }
}

pub const DRAW_MINUTE: i32 = 20 * 60 + 30;

/// Draw number n (0-based from the epoch) of a draw day; None if the game does not draw that day.
pub fn draw_no(game: Lotto, day: i32) -> Option<i32> {
    if day < EPOCH || !game.draws_on(day) {
        return None;
    }
    Some((EPOCH..day).filter(|&d| game.draws_on(d)).count() as i32)
}

/// The first draw at or after (day, minute): (draw number, day).
pub fn next_draw(game: Lotto, day: i32, minute: i32) -> (i32, i32) {
    let mut d = day.max(EPOCH);
    if d == day && minute >= DRAW_MINUTE {
        d += 1;
    }
    while !game.draws_on(d) {
        d += 1;
    }
    (draw_no(game, d).unwrap_or(0), d)
}

#[derive(Clone, Debug)]
pub struct Draw {
    pub no: i32,
    pub day: i32,
    pub numbers: Vec<i32>, // six, sorted
    pub special: i32,      // 6/49 special number, or 雙星彩 zone-2 number
    pub pool: i64,         // jackpot available at this draw
    pub npc_jackpot: bool, // somebody else hit the jackpot
    pub shop_second: bool, // this shop sold a 2nd-prize ticket (for the red board)
}

fn draw_rng(seed: u64, game: Lotto, no: i32) -> Rng {
    Rng::new(seed ^ dates::hash(game.key()) ^ ((no as u64) << 20).wrapping_mul(0x2545_F491_4F6C_DD1D))
}

/// Draw n, with the jackpot rolled forward from the first draw (cheap: two random numbers per draw).
pub fn draw(seed: u64, game: Lotto, no: i32) -> Draw {
    let mut pool = game.base_pool();
    for i in 0..no {
        let mut r = draw_rng(seed, game, i);
        let _ = pick_numbers(&mut r, game);
        let (hit, grow) = (r.f64() < 0.08, r.range(4, 9) * 1_000_000);
        pool = if hit { game.base_pool() } else { pool + grow * if game == Lotto::TwinStar { 2 } else { 1 } / 1 };
    }
    let mut r = draw_rng(seed, game, no);
    let (numbers, special) = pick_numbers(&mut r, game);
    let npc_jackpot = r.f64() < 0.08;
    let _ = r.range(4, 9);
    let shop_second = r.f64() < 0.03;
    let mut day = EPOCH;
    let mut seen = -1;
    while seen < no {
        if game.draws_on(day) {
            seen += 1;
            if seen == no {
                break;
            }
        }
        day += 1;
    }
    Draw { no, day, numbers, special, pool, npc_jackpot, shop_second }
}

fn pick_numbers(r: &mut Rng, game: Lotto) -> (Vec<i32>, i32) {
    let mut balls: Vec<i32> = (1..=game.zone1()).collect();
    let mut out = Vec::new();
    for _ in 0..6 {
        let i = r.range(0, balls.len() as i64 - 1) as usize;
        out.push(balls.remove(i));
    }
    let special = match game {
        Lotto::Star649 => balls[r.range(0, balls.len() as i64 - 1) as usize],
        Lotto::TwinStar => r.range(1, 8) as i32,
    };
    out.sort();
    (out, special)
}

/// Prize for one line: (name, amount). Jackpot shares the pool with other winners when somebody else hit too.
pub fn prize(game: Lotto, d: &Draw, picks: &[i32], zone2: i32) -> Option<(&'static str, i64)> {
    let hits = picks.iter().filter(|n| d.numbers.contains(n)).count();
    match game {
        Lotto::Star649 => {
            let sp = picks.contains(&d.special);
            Some(match (hits, sp) {
                (6, _) => ("頭獎", if d.npc_jackpot { d.pool / 2 } else { d.pool }),
                (5, true) => ("貳獎", 300_000),
                (5, false) => ("參獎", 10_000),
                (4, true) => ("肆獎", 2_400),
                (4, false) => ("伍獎", 400),
                (3, true) => ("陸獎", 200),
                (2, true) => ("柒獎", 80),
                (3, false) => ("普獎", 80),
                _ => return None,
            })
        }
        Lotto::TwinStar => {
            let z = zone2 == d.special;
            Some(match (hits, z) {
                (6, true) => ("頭獎", if d.npc_jackpot { d.pool / 2 } else { d.pool }),
                (6, false) => ("貳獎", 600_000),
                (5, true) => ("參獎", 30_000),
                (5, false) => ("肆獎", 4_000),
                (4, true) => ("伍獎", 800),
                (4, false) => ("陸獎", 160),
                (3, true) => ("柒獎", 80),
                (2, true) => ("捌獎", 40),
                (3, false) => ("玖獎", 20),
                (1, true) => ("普獎", 20),
                _ => return None,
            })
        }
    }
}

pub fn quick_pick(r: &mut Rng, game: Lotto) -> (Vec<i32>, i32) {
    let (n, s) = pick_numbers(r, game);
    (n, if game == Lotto::TwinStar { s } else { 0 })
}

pub fn draw_json(d: &Draw, game: Lotto) -> Value {
    json!({"game": game.key(), "label": game.label(), "no": d.no, "issue": format!("{}{:03}", dates::civil_from_days(d.day).0 - 1911, d.no + 1),
           "date": dates::format(d.day), "numbers": d.numbers, "special": d.special, "pool": d.pool, "jackpot_hit": d.npc_jackpot})
}

// ------------------------------------------------------------------------------------------------ scratch cards

pub struct Theme {
    pub id: &'static str,
    pub name: &'static str,
    pub price: i64,
}

pub const THEMES: [Theme; 6] = [
    Theme { id: "lucky", name: "好運連連", price: 20 },
    Theme { id: "love", name: "戀愛滿分", price: 20 },
    Theme { id: "fortune", name: "財神到", price: 50 },
    Theme { id: "starlight", name: "星光閃耀", price: 50 },
    Theme { id: "diamond", name: "鑽石人生", price: 100 },
    Theme { id: "holiday", name: "黃金假期", price: 100 },
];

/// (prize, probability); about 60% back on average.
fn table(price: i64) -> Vec<(i64, f64)> {
    match price {
        20 => vec![(20, 0.20), (40, 0.08), (100, 0.02), (200, 0.008), (2_000, 0.0004), (20_000, 0.00002)],
        50 => vec![(50, 0.20), (100, 0.08), (250, 0.02), (500, 0.008), (5_000, 0.0004), (100_000, 0.00001)],
        _ => vec![(100, 0.20), (200, 0.08), (500, 0.02), (1_000, 0.008), (10_000, 0.0004), (400_000, 0.000005)],
    }
}

pub fn expected_return(price: i64) -> f64 {
    table(price).iter().map(|(p, q)| *p as f64 * q).sum::<f64>() / price as f64
}

/// A card decided the moment it is scratched: nine amounts; three of the same amount wins it.
pub fn scratch(seed: u64, serial: i64, price: i64) -> (Vec<i64>, i64) {
    let mut r = Rng::new(seed ^ 0x5C2A_7C4E ^ (serial as u64).wrapping_mul(0x9E37_79B9));
    let t = table(price);
    let roll = r.f64();
    let mut acc = 0.0;
    let mut win = 0;
    for (p, q) in &t {
        acc += q;
        if roll < acc {
            win = *p;
            break;
        }
    }
    let amounts: Vec<i64> = t.iter().map(|x| x.0).collect();
    let mut cells: Vec<i64> = Vec::new();
    if win > 0 {
        cells.extend([win, win, win]);
    }
    // Fill with other amounts, at most twice each, so no second triple appears.
    while cells.len() < 9 {
        let a = amounts[r.range(0, amounts.len() as i64 - 1) as usize];
        let count = cells.iter().filter(|&&c| c == a).count();
        if (a == win && count >= 3) || (a != win && count >= 2) {
            continue;
        }
        cells.push(a);
    }
    for i in (1..cells.len()).rev() {
        let j = r.range(0, i as i64) as usize;
        cells.swap(i, j);
    }
    (cells, win)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn draws_are_fixed_by_seed_and_scratch_returns_about_sixty_percent() {
        let a = draw(42, Lotto::Star649, 5);
        let b = draw(42, Lotto::Star649, 5);
        assert_eq!(a.numbers, b.numbers);
        assert_ne!(draw(43, Lotto::Star649, 5).numbers, a.numbers);
        assert!(Lotto::Star649.draws_on(a.day));
        assert_eq!(draw_no(Lotto::Star649, a.day), Some(5));
        for p in [20, 50, 100] {
            let e = expected_return(p);
            assert!((0.5..0.7).contains(&e), "{p}: {e}");
        }
        let mut total = 0;
        for s in 0..20000 {
            let (cells, win) = scratch(7, s, 20);
            assert_eq!(cells.len(), 9);
            if win > 0 {
                assert_eq!(cells.iter().filter(|&&c| c == win).count(), 3);
            }
            total += win;
        }
        let ratio = total as f64 / (20000.0 * 20.0);
        assert!((0.4..0.85).contains(&ratio), "{ratio}");
    }
}
