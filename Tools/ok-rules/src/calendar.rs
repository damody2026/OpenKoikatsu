//! The game calendar (OKStreetSocialCalendar.h): day 0 is 2010-09-01 and a game day starts at 04:00. Rules that care
//! about dates (paydays, bills, month ends) read them from here, from the day index Unreal passes.

/// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil).
pub fn days_from_civil(y: i64, m: u32, d: u32) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let mp = (m as i64 + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d as i64 - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

/// The civil date of days since 1970-01-01.
pub fn civil_from_days(z: i64) -> (i64, u32, u32) {
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Date {
    pub year: i64,
    pub month: u32,
    pub day: u32,
    /// Unreal's EDayOfWeek: Monday 0 .. Sunday 6.
    pub weekday: u32,
}

/// The date of game day `day` at `hours` (before 04:00 it is still the previous game day, so the next calendar date).
pub fn date(day: i32, hours: f64) -> Date {
    let date_day = day as i64 + if hours < 4.0 { 1 } else { 0 };
    let z = days_from_civil(2010, 9, 1) + date_day;
    let (year, month, d) = civil_from_days(z);
    // 1970-01-01 was a Thursday (3 when Monday is 0).
    let weekday = ((z % 7 + 7 + 3) % 7) as u32;
    Date { year, month, day: d, weekday }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn epoch_and_rollover() {
        assert_eq!(date(0, 8.0), Date { year: 2010, month: 9, day: 1, weekday: 2 }, "2010-09-01 was a Wednesday");
        assert_eq!(date(0, 3.0).day, 2, "before 04:00 belongs to the next date");
        let d = date(30, 8.0);
        assert_eq!((d.month, d.day), (10, 1));
        let d = date(122, 12.0);
        assert_eq!((d.year, d.month, d.day), (2011, 1, 1));
        assert_eq!(date(4, 12.0).weekday, 6, "2010-09-05 was a Sunday");
    }
}
