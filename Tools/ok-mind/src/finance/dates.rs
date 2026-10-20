//! Civil dates as day numbers (days since 1970-01-01), the US daylight-saving rule and a small seeded RNG.

/// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
pub fn days_from_civil(y: i32, m: u32, d: u32) -> i32 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let mp = (m as i32 + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d as i32 - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

pub fn civil_from_days(z: i32) -> (i32, u32, u32) {
    let z = z + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (if m <= 2 { y + 1 } else { y }, m, d)
}

/// 0 = Sunday .. 6 = Saturday.
pub fn weekday(day: i32) -> u32 {
    (day + 4).rem_euclid(7) as u32
}

pub fn ymd(day: i32) -> i32 {
    let (y, m, d) = civil_from_days(day);
    y * 10000 + m as i32 * 100 + d as i32
}

pub fn from_ymd(v: i32) -> i32 {
    days_from_civil(v / 10000, (v / 100 % 100) as u32, (v % 100) as u32)
}

/// "2010-09-01" -> day number.
pub fn parse(s: &str) -> Option<i32> {
    let mut it = s.trim().split('-');
    let y: i32 = it.next()?.parse().ok()?;
    let m: u32 = it.next()?.parse().ok()?;
    let d: u32 = it.next()?.parse().ok()?;
    if it.next().is_some() || !(1..=12).contains(&m) || !(1..=31).contains(&d) {
        return None;
    }
    let day = days_from_civil(y, m, d);
    (civil_from_days(day) == (y, m, d)).then_some(day)
}

pub fn format(day: i32) -> String {
    let (y, m, d) = civil_from_days(day);
    format!("{y:04}-{m:02}-{d:02}")
}

/// The n-th (1-based) given weekday of a month.
fn nth_weekday(y: i32, m: u32, wd: u32, n: u32) -> i32 {
    let first = days_from_civil(y, m, 1);
    let offset = (wd + 7 - weekday(first)) % 7;
    first + offset as i32 + 7 * (n as i32 - 1)
}

/// US daylight saving (since 2007): second Sunday of March to first Sunday of November, by US calendar date.
pub fn us_dst(day: i32) -> bool {
    let (y, _, _) = civil_from_days(day);
    day >= nth_weekday(y, 3, 0, 2) && day < nth_weekday(y, 11, 0, 1)
}

/// Taiwan minute of day when the US session (09:30–16:00 ET) of a US date opens; it closes 390 minutes later.
pub fn us_open_tw_minute(us_day: i32) -> i32 {
    if us_dst(us_day) { 21 * 60 + 30 } else { 22 * 60 + 30 }
}

/// SplitMix64: small, fast, deterministic.
#[derive(Clone)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Rng {
        Rng(seed ^ 0x9E37_79B9_7F4A_7C15)
    }
    pub fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }
    /// Uniform in [0, 1).
    pub fn f64(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 / (1u64 << 53) as f64
    }
    /// Uniform in [lo, hi] inclusive.
    pub fn range(&mut self, lo: i64, hi: i64) -> i64 {
        if hi <= lo {
            return lo;
        }
        lo + (self.next_u64() % (hi - lo + 1) as u64) as i64
    }
    pub fn normal(&mut self) -> f64 {
        let u1 = self.f64().max(1e-12);
        let u2 = self.f64();
        (-2.0 * u1.ln()).sqrt() * (std::f64::consts::TAU * u2).cos()
    }
}

/// FNV-1a over text, for seeds that depend on a name.
pub fn hash(text: &str) -> u64 {
    text.bytes().fold(0xcbf2_9ce4_8422_2325u64, |h, b| (h ^ b as u64).wrapping_mul(0x0100_0000_01b3))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn civil_round_trip_and_dst() {
        assert_eq!(format(parse("2010-09-01").unwrap()), "2010-09-01");
        assert_eq!(weekday(parse("2010-09-01").unwrap()), 3); // Wednesday
        assert!(us_dst(parse("2025-07-15").unwrap()));
        assert!(!us_dst(parse("2025-12-15").unwrap()));
        assert_eq!(us_open_tw_minute(parse("2025-07-15").unwrap()), 21 * 60 + 30);
        assert_eq!(us_open_tw_minute(parse("2025-12-15").unwrap()), 22 * 60 + 30);
        assert!(parse("2011-02-30").is_none());
    }
}
