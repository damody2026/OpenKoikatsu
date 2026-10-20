//! The university calendar (Docs/STREET_SOCIAL_CAMPUS_PLAN.md): fall classes from the 2nd Monday of September, spring
//! from the 4th Monday of February, midterms 8 weeks in, finals in week 18, the term ending the Saturday after.
use crate::calendar::{civil_from_days, days_from_civil};
use serde::Serialize;
use serde_json::{json, Value};

/// A term; dates are days since 1970-01-01.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Term {
    pub key: String,
    pub start: i64,
    pub midterm: i64,
    pub fin: i64,
    pub end: i64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Phase {
    Break,
    Classes,
    Midterm,
    Final,
}

/// Monday 0 .. Sunday 6 of days since 1970-01-01.
pub fn weekday(z: i64) -> i64 {
    (z % 7 + 7 + 3) % 7
}

/// The `n`th `wd` (Monday 0) of a month.
pub fn nth_weekday(year: i64, month: u32, wd: i64, n: i64) -> i64 {
    let mut d = days_from_civil(year, month, 1);
    while weekday(d) != wd {
        d += 1;
    }
    d + 7 * (n - 1)
}

pub fn term_of(academic_year: i64, half: i32) -> Term {
    let start = if half == 1 { nth_weekday(academic_year, 9, 0, 2) } else { nth_weekday(academic_year + 1, 2, 0, 4) };
    let fin = start + 7 * 17;
    Term { key: format!("{academic_year}-{half}"), start, midterm: start + 7 * 8, fin, end: fin + 5 }
}

/// The term a date belongs to (the most recent one that has started) and the phase within it.
pub fn term_at(date: i64) -> (Term, Phase) {
    let (y, _, _) = civil_from_days(date);
    let mut best = term_of(y - 1, 1);
    for t in [term_of(y - 1, 1), term_of(y - 1, 2), term_of(y, 1)] {
        if t.start <= date {
            best = t;
        }
    }
    let phase = if date > best.end {
        Phase::Break
    } else if date >= best.fin {
        Phase::Final
    } else if date >= best.midterm && date < best.midterm + 5 {
        Phase::Midterm
    } else {
        Phase::Classes
    };
    (best, phase)
}

/// Days a fee window opens before the term and closes after it starts.
pub const FEES_OPEN_DAYS: i64 = 7;
pub const FEES_DEADLINE_DAYS: i64 = 14;
/// A term's tuition and dorm fee in the request's region (region.rs [fixed]).
#[allow(non_snake_case)]
pub fn TUITION_FEE() -> i64 {
    crate::region::fixed("tuition", 2500)
}
#[allow(non_snake_case)]
pub fn DORM_FEE() -> i64 {
    crate::region::fixed("dorm", 600)
}

/// The term whose fee window (7 days before the start .. 14 days after) contains the date.
pub fn fees_term(date: i64) -> Option<Term> {
    let (y, _, _) = civil_from_days(date);
    for year in [y - 1, y] {
        for half in [1, 2] {
            let t = term_of(year, half);
            if date >= t.start - FEES_OPEN_DAYS && date <= t.start + FEES_DEADLINE_DAYS {
                return Some(t);
            }
        }
    }
    None
}

/// The first term starting after the date (what a break is waiting for).
pub fn next_term(date: i64) -> Term {
    let (y, _, _) = civil_from_days(date);
    [term_of(y - 1, 2), term_of(y, 1), term_of(y, 2), term_of(y + 1, 1)].into_iter().find(|t| t.start > date).unwrap_or_else(|| term_of(y + 1, 1))
}

/// The term after `key` ("2010-1" -> "2010-2" -> "2011-1").
pub fn next_term_key(key: &str) -> String {
    match key.split_once('-') {
        Some((y, "1")) => format!("{y}-2"),
        Some((y, _)) => format!("{}-1", y.parse::<i64>().unwrap_or(0) + 1),
        None => key.into(),
    }
}

/// Days since 1970 of game day `day` (its civil date at 08:00).
pub fn civil_day(day: i32) -> i64 {
    days_from_civil(2010, 9, 1) + day as i64
}

/// Game day of a civil day.
pub fn game_day(civil: i64) -> i32 {
    (civil - days_from_civil(2010, 9, 1)) as i32
}

fn term_json(t: &Term) -> Value {
    let ymd = |z: i64| {
        let (y, m, d) = civil_from_days(z);
        json!([y, m, d])
    };
    json!({"key": t.key, "start": ymd(t.start), "midterm": ymd(t.midterm), "final": ymd(t.fin), "end": ymd(t.end), "start_day": game_day(t.start)})
}

pub fn eval(kind: &str, request: &Value) -> Option<Result<Value, String>> {
    match kind {
        "campus_term" => Some((|| {
            // By game day, or by a civil date [y, m, d].
            let date = match request.get("date").and_then(Value::as_array) {
                Some(d) if d.len() == 3 => days_from_civil(d[0].as_i64().unwrap_or(2010), d[1].as_u64().unwrap_or(1) as u32, d[2].as_u64().unwrap_or(1) as u32),
                _ => civil_day(request.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32),
            };
            let (t, phase) = term_at(date);
            let fees = fees_term(date);
            Ok(json!({"term": term_json(&t), "phase": phase, "fees_term": fees.as_ref().map(term_json), "next_term": term_json(&next_term(date))}))
        })()),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn the_break_waits_for_the_next_term() {
        // 2010-09-08 (before the fall term starts on 09-13): the next term is 2010-1, not the spring that ended.
        let d = days_from_civil(2010, 9, 8);
        assert_eq!(term_at(d).1, Phase::Break);
        let n = next_term(d);
        assert_eq!((n.key.as_str(), n.start), ("2010-1", days_from_civil(2010, 9, 13)));
        assert_eq!(next_term(days_from_civil(2010, 10, 1)).key, "2010-2");
    }

    use super::*;

    // Ported from Tests/OKStreetCampus.spec.cpp (Terms).
    #[test]
    fn spec_terms_and_phases() {
        let fall = term_of(2010, 1);
        assert_eq!(fall.start, days_from_civil(2010, 9, 13));
        assert_eq!(fall.midterm, days_from_civil(2010, 11, 8));
        assert_eq!(fall.fin, days_from_civil(2011, 1, 10));
        assert_eq!(term_of(2010, 2).start, days_from_civil(2011, 2, 28));
        let cases = [
            ((2010, 9, 7), Phase::Break, None),
            ((2010, 9, 14), Phase::Classes, Some("2010-1")),
            ((2010, 11, 12), Phase::Midterm, Some("2010-1")),
            ((2010, 11, 15), Phase::Classes, Some("2010-1")),
            ((2011, 1, 14), Phase::Final, Some("2010-1")),
            ((2011, 2, 1), Phase::Break, None),
            ((2011, 3, 1), Phase::Classes, Some("2010-2")),
        ];
        for ((y, m, d), phase, key) in cases {
            let (t, p) = term_at(days_from_civil(y, m, d));
            assert_eq!(p, phase, "{y}-{m}-{d}");
            if let Some(k) = key {
                assert_eq!(t.key, k);
            }
        }
    }

    #[test]
    fn fee_window_and_next_term() {
        let start = days_from_civil(2010, 9, 13);
        assert_eq!(fees_term(start - 7).unwrap().key, "2010-1");
        assert!(fees_term(start - 8).is_none());
        assert_eq!(fees_term(start + 14).unwrap().key, "2010-1");
        assert!(fees_term(start + 15).is_none());
        assert_eq!((next_term_key("2010-1"), next_term_key("2010-2")), ("2010-2".to_string(), "2011-1".to_string()));
    }
}
