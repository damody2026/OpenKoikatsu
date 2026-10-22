//! His bank (Docs/STREET_SOCIAL_FINANCE_IMPROVEMENT_PLAN.md stage 1): interest on savings, time deposits, a credit card,
//! a student loan, the phone bill, tuition and dorm fees, and the heroines' own savings, goals and tuition. The banking
//! record travels as the save writes it (OKStreetBankingJson) and comes back changed; money through the books.
use crate::calendar::date;
use crate::campus::{self, Phase};
use crate::day::charge_bill;
use crate::effects::Effects;
use crate::relation::Relation;
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};

// ------------------------------------------------------------------------------------------------ rates

/// Deposit rates (% a year) from the day they applied. 2008-12 -> 2015-10: the People's Bank of China benchmark rates;
/// from 2015-10-24 the big state banks' listed rates (approximate). Loan: the 1-year benchmark lending rate, the 1-year
/// LPR from 2019-08-20.
#[derive(Debug, Clone, Copy, PartialEq, Serialize)]
pub struct Rates {
    pub from: i32,
    pub demand: f32,
    pub m3: f32,
    pub m6: f32,
    pub y1: f32,
    pub y2: f32,
    pub y3: f32,
    pub loan: f32,
}

const fn r(from: i32, demand: f32, m3: f32, m6: f32, y1: f32, y2: f32, y3: f32, loan: f32) -> Rates {
    Rates { from, demand, m3, m6, y1, y2, y3, loan }
}

pub const RATES: [Rates; 30] = [
    r(20081223, 0.36, 1.71, 1.98, 2.25, 2.79, 3.33, 5.31),
    r(20101020, 0.36, 1.91, 2.20, 2.50, 3.25, 3.85, 5.56),
    r(20101226, 0.36, 2.25, 2.50, 2.75, 3.55, 4.15, 5.81),
    r(20110209, 0.40, 2.60, 2.80, 3.00, 3.90, 4.50, 6.06),
    r(20110406, 0.50, 2.85, 3.05, 3.25, 4.15, 4.75, 6.31),
    r(20110707, 0.50, 3.10, 3.30, 3.50, 4.40, 5.00, 6.56),
    r(20120608, 0.40, 2.85, 3.05, 3.25, 4.10, 4.65, 6.31),
    r(20120706, 0.35, 2.60, 2.80, 3.00, 3.75, 4.25, 6.00),
    r(20141122, 0.35, 2.35, 2.55, 2.75, 3.35, 4.00, 5.60),
    r(20150301, 0.35, 2.10, 2.30, 2.50, 3.10, 3.75, 5.35),
    r(20150511, 0.35, 1.85, 2.05, 2.25, 2.85, 3.50, 5.10),
    r(20150628, 0.35, 1.60, 1.80, 2.00, 2.60, 3.25, 4.85),
    r(20150826, 0.35, 1.35, 1.55, 1.75, 2.35, 3.00, 4.60),
    r(20151024, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 4.35),
    r(20190820, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 4.25),
    r(20191120, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 4.15),
    r(20200220, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 4.05),
    r(20200420, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 3.85),
    r(20211220, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 3.80),
    r(20220120, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 3.70),
    r(20220822, 0.30, 1.35, 1.55, 1.75, 2.25, 2.75, 3.65),
    r(20220915, 0.25, 1.25, 1.45, 1.65, 2.15, 2.60, 3.65),
    r(20230608, 0.20, 1.25, 1.45, 1.65, 2.05, 2.45, 3.65),
    r(20230620, 0.20, 1.25, 1.45, 1.65, 2.05, 2.45, 3.55),
    r(20230821, 0.20, 1.25, 1.45, 1.65, 2.05, 2.45, 3.45),
    r(20230901, 0.20, 1.25, 1.45, 1.55, 1.85, 2.20, 3.45),
    r(20231222, 0.20, 1.15, 1.35, 1.45, 1.65, 1.95, 3.45),
    r(20240722, 0.15, 1.05, 1.25, 1.35, 1.45, 1.75, 3.35),
    r(20241021, 0.10, 0.80, 1.00, 1.10, 1.20, 1.50, 3.10),
    r(20250520, 0.05, 0.65, 0.85, 0.95, 1.05, 1.25, 3.00),
];

pub fn rates_on(ymd: i32) -> Rates {
    let mut i = 0;
    while i + 1 < RATES.len() && RATES[i + 1].from <= ymd {
        i += 1;
    }
    RATES[i]
}

pub const DEPOSIT_TERMS: [i32; 5] = [3, 6, 12, 24, 36];

pub fn term_rate(r: &Rates, months: i32) -> f32 {
    if months <= 3 {
        r.m3
    } else if months <= 6 {
        r.m6
    } else if months <= 12 {
        r.y1
    } else if months <= 24 {
        r.y2
    } else {
        r.y3
    }
}

/// Monthly phone fee, card limit and the yearly student-loan cap, in the request's region (region.rs [fixed]).
#[allow(non_snake_case)]
pub fn PHONE_FEE() -> i64 {
    crate::region::fixed("phone_fee", 58)
}
#[allow(non_snake_case)]
pub fn CARD_LIMIT() -> i64 {
    crate::region::fixed("card_limit", 3000)
}
pub const CARD_DAILY_INTEREST: f64 = 0.0005;
#[allow(non_snake_case)]
pub fn LOAN_YEAR_CAP() -> i64 {
    crate::region::fixed("loan_year_cap", 6000)
}

/// Scholarship by a term's average grade (no failed course).
pub fn scholarship(average: f32, any_failed: bool) -> i64 {
    if any_failed {
        0
    } else if average >= 90.0 {
        crate::region::amount("cash", 4000)
    } else if average >= 85.0 {
        crate::region::amount("cash", 2000)
    } else if average >= 80.0 {
        crate::region::amount("cash", 1000)
    } else {
        0
    }
}

/// What the father he never met sends: the next multiple of ¥100,000 strictly above the shortfall.
pub fn father_gift(shortfall: i64) -> i64 {
    if shortfall <= 0 {
        0
    } else {
        let step = crate::region::amount("cash", 100_000);
        (shortfall / step + 1) * step
    }
}

// ------------------------------------------------------------------------------------------------ the record

#[derive(Serialize, Deserialize, Debug, Clone, Default, PartialEq)]
#[serde(default)]
pub struct Deposit {
    pub id: i32,
    pub amount: i64,
    pub months: i32,
    pub start: i32,
    pub rate: f32,
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Card {
    pub issued: bool,
    pub stopped: bool,
    pub limit: i64,
    pub balance: i64,
    pub statement: i64,
    pub min_due: i64,
    pub due_day: i32,
    pub late: i32,
    pub interest: f64,
}

impl Default for Card {
    fn default() -> Self {
        Card { issued: false, stopped: false, limit: CARD_LIMIT(), balance: 0, statement: 0, min_due: 0, due_day: -1, late: 0, interest: 0.0 }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Loan {
    pub owed: i64,
    pub year: String,
    pub taken_this_year: i64,
    pub repay_from: i32,
    pub monthly: i64,
    pub interest: f64,
}

impl Default for Loan {
    fn default() -> Self {
        Loan { owed: 0, year: String::new(), taken_this_year: 0, repay_from: -1, monthly: 0, interest: 0.0 }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, Default, PartialEq)]
#[serde(default)]
pub struct Banking {
    pub deposits: Vec<Deposit>,
    pub next_deposit: i32,
    pub demand_interest: f64,
    pub card: Card,
    pub loan: Loan,
    pub phone_arrears: i64,
    pub notes: Vec<String>,
    pub net_worth: Vec<(i32, i64)>,
    /// The father's texts, gifts and the bankruptcy state: kept as the save has them (OKStreetSocialBank.inl).
    pub father: Value,
}

impl Banking {
    pub fn deposits_total(&self) -> i64 {
        self.deposits.iter().map(|d| d.amount).sum()
    }

    /// A line in the bank's messages, dated (newest last, at most 40).
    pub fn note(&mut self, day: i32, text: &str) {
        let d = date(day, 8.0);
        self.notes.push(crate::tr::m("bank.date_text_line", &[("year", crate::tr::a(&d.year)), ("month", crate::tr::a(&d.month)), ("day", crate::tr::a(&d.day)), ("text", crate::tr::a(&text))]));
        while self.notes.len() > 40 {
            self.notes.remove(0);
        }
    }
}

/// His wallet and savings as the rules move them (a bill comes from savings first, then the wallet).
pub struct Purse<'a> {
    pub cash: i64,
    pub bank: i64,
    pub fx: &'a mut Effects,
}

impl Purse<'_> {
    /// OKPrivateTake: nothing is taken unless all of it can be.
    pub fn take(&mut self, amount: i64, category: &str, note: &str) -> bool {
        if amount <= 0 {
            return true;
        }
        if self.bank + self.cash < amount {
            return false;
        }
        let from_bank = self.bank.min(amount);
        if from_bank > 0 {
            self.bank_move(-from_bank, category, note);
        }
        if amount > from_bank {
            self.cash_move(-(amount - from_bank), category, note);
        }
        true
    }

    pub fn bank_move(&mut self, amount: i64, category: &str, note: &str) {
        self.bank += amount;
        self.fx.bank(amount, category, note);
    }

    pub fn cash_move(&mut self, amount: i64, category: &str, note: &str) {
        self.cash += amount;
        self.fx.cash(amount, category, note);
    }

    /// A post to one of his other accounts (deposit, card, loan, arrears).
    pub fn post(&mut self, account: &str, amount: i64, category: &str, note: &str) {
        self.fx.post("private", account, amount, category, note);
    }
}

fn ymd(day: i32) -> i32 {
    let d = date(day, 8.0);
    (d.year * 10000 + d.month as i64 * 100 + d.day as i64) as i32
}

fn mmdd(day: i32) -> String {
    let d = date(day, 8.0);
    format!("{:02}/{:02}", d.month, d.day)
}

fn yyyymmdd(day: i32) -> String {
    let d = date(day, 8.0);
    format!("{}/{:02}/{:02}", d.year, d.month, d.day)
}

fn round_half_away(x: f64) -> i64 {
    if x >= 0.0 {
        (x + 0.5).floor() as i64
    } else {
        -((-x + 0.5).floor() as i64)
    }
}

fn s<'a>(v: &'a Value, k: &str) -> &'a str {
    v.get(k).and_then(Value::as_str).unwrap_or("")
}

// ------------------------------------------------------------------------------------------------ daily

/// A heroine's money for the bank day.
#[derive(Deserialize, Debug, Clone)]
pub struct BankHeroine {
    pub relation: Relation,
    #[serde(default)]
    pub profile: Value,
    /// Her academic record when she is a student (OKStreetAcademicJson); fees_paid is updated.
    #[serde(default)]
    pub academic: Option<Value>,
}

/// The bank's day. Request: day, player_cash, player_bank, net_worth (the day that ended, Unreal's sum of everything he
/// owns), banking, player_academic, heroines.
pub fn daily(request: &Value) -> Result<Value, String> {
    let day = request.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
    let mut b: Banking = serde_json::from_value(request.get("banking").cloned().unwrap_or(json!({}))).map_err(|e| format!("banking: {e}"))?;
    let mut me = request.get("player_academic").cloned().unwrap_or(json!({}));
    let heroines: Vec<BankHeroine> = serde_json::from_value(request.get("heroines").cloned().unwrap_or(json!([]))).map_err(|e| format!("heroines: {e}"))?;
    let mut fx = Effects::default();
    let mut purse = Purse { cash: request.get("player_cash").and_then(Value::as_i64).unwrap_or(0), bank: request.get("player_bank").and_then(Value::as_i64).unwrap_or(0), fx: &mut fx };
    let today = date(day, 8.0);
    let rate = rates_on(ymd(day));
    let civil = campus::civil_day(day);
    let mut shout: Vec<String> = Vec::new();
    // The day that just ended, for the net-worth chart.
    if b.net_worth.last().map(|w| w.0 < day - 1).unwrap_or(true) {
        b.net_worth.push((day - 1, request.get("net_worth").and_then(Value::as_i64).unwrap_or(0)));
        while b.net_worth.len() > 6000 {
            b.net_worth.remove(0);
        }
    }
    // Savings account: daily accrual, paid on the 21st of the last month of each quarter.
    b.demand_interest += purse.bank as f64 * rate.demand as f64 / 100.0 / 360.0;
    if today.month % 3 == 0 && today.day == 21 {
        let i = b.demand_interest.floor() as i64;
        if i > 0 {
            purse.bank_move(i, "interest", crate::tr::t("bank.checking_interest_credited"));
            b.demand_interest -= i as f64;
            b.note(day, &crate::tr::m("bank.demand_interest_accrued", &[("interest", crate::tr::a(&crate::tr::money(i))), ("demand_rate", crate::tr::a(&rate.demand))]));
        }
    }
    // Time deposits: at maturity the interest is added and the whole rolls over at today's rate.
    let mut matured = Vec::new();
    for d in b.deposits.iter_mut() {
        let mature = d.start + (d.months as f32 * 365.0 / 12.0 + 0.5).floor() as i32;
        if day < mature {
            continue;
        }
        let i = round_half_away(d.amount as f64 * d.rate as f64 / 100.0 * d.months as f64 / 12.0);
        d.amount += i;
        purse.post("deposit", i, "interest", &crate::tr::m("bank.term_deposit_maturity_interest", &[("deposit_id", crate::tr::a(&d.id))]));
        d.start = day;
        d.rate = term_rate(&rate, d.months);
        matured.push(crate::tr::m("bank.term_deposit_rollover", &[("deposit_id", crate::tr::a(&d.id)), ("interest", crate::tr::a(&crate::tr::money(i))), ("principal", crate::tr::a(&crate::tr::money(d.amount))), ("months", crate::tr::a(&d.months)), ("rate", crate::tr::a(&d.rate))]));
    }
    for m in matured {
        b.note(day, &m);
    }
    // Phone: ¥58 on the 10th; unpaid, the phone is cut off until the arrears are cleared.
    if today.day == 10 {
        if purse.take(PHONE_FEE() + b.phone_arrears, "phone", crate::tr::t("bank.phone_monthly_fee_label")) {
            purse.post("arrears", b.phone_arrears, "phone", crate::tr::t("bank.phone_arrears_settled_label"));
            if b.phone_arrears > 0 {
                shout.push(crate::tr::t("bank.phone_arrears_cleared_notice").into());
            }
            b.note(day, &crate::tr::m("bank.phone_fee_charged", &[("amount", crate::tr::a(&crate::tr::money(PHONE_FEE() + b.phone_arrears)))]));
            b.phone_arrears = 0;
        } else {
            b.phone_arrears += PHONE_FEE();
            purse.post("arrears", -PHONE_FEE(), "phone", crate::tr::t("bank.phone_fee_debit_failed"));
            shout.push(crate::tr::m("bank.phone_suspended_pay_hint", &[("amount", crate::tr::a(&crate::tr::money(b.phone_arrears)))]));
        }
    }
    // Credit card: statement on the 5th, due 20 days later; interest on what is left after the due date.
    {
        let cd = &mut b.card;
        if cd.issued {
            if today.day == 5 && cd.balance > 0 {
                cd.statement = cd.balance;
                cd.min_due = cd.balance.min((cd.balance / 10).max(crate::region::amount("cash", 50)));
                cd.due_day = day + 20;
                shout.push(crate::tr::m("bank.card_statement_summary", &[("statement_amount", crate::tr::a(&crate::tr::money(cd.statement))), ("min_due", crate::tr::a(&crate::tr::money(cd.min_due))), ("due_date", crate::tr::a(&(mmdd(cd.due_day))))]));
            }
            if cd.due_day >= 0 && day > cd.due_day && cd.statement > 0 {
                cd.interest += cd.statement as f64 * CARD_DAILY_INTEREST;
                let i = cd.interest.floor() as i64;
                if i > 0 {
                    cd.balance += i;
                    purse.post("card", -i, "card_fee", crate::tr::t("bank.card_overdue_interest_label"));
                    cd.statement += i;
                    cd.interest -= i as f64;
                }
            }
            if cd.due_day >= 0 && day == cd.due_day + 1 && cd.min_due > 0 {
                cd.late += 1;
                let fee = (cd.min_due * 5 / 100).max(1);
                cd.balance += fee;
                purse.post("card", -fee, "card_fee", crate::tr::t("bank.card_overdue_fee_label"));
                cd.statement += fee;
                cd.min_due = 0;
                shout.push(crate::tr::m("bank.card_min_payment_missed", &[("fee", crate::tr::a(&crate::tr::money(fee))), ("late_count", crate::tr::a(&cd.late))]));
                if cd.late >= 3 && !cd.stopped {
                    cd.stopped = true;
                    shout.push(crate::tr::t("bank.card_frozen_three_overdue").into());
                }
            }
        }
    }
    // Student loan: interest-free while studying; six months after he stops being a student, monthly repayments.
    {
        let status = s(&me, "status").to_string();
        let l = &mut b.loan;
        if l.owed > 0 && l.repay_from < 0 && (status == "expelled" || status == "none") {
            l.repay_from = day + 180;
            l.monthly = (l.owed + 59) / 60;
            shout.push(crate::tr::m("bank.student_loan_repay_schedule", &[("owed", crate::tr::a(&crate::tr::money(l.owed))), ("repay_from", crate::tr::a(&(yyyymmdd(l.repay_from)))), ("monthly", crate::tr::a(&crate::tr::money(l.monthly)))]));
        }
        if l.owed > 0 && l.repay_from >= 0 && day >= l.repay_from {
            l.interest += l.owed as f64 * rate.loan as f64 / 100.0 / 360.0;
            let i = l.interest.floor() as i64;
            if i > 0 {
                l.owed += i;
                purse.post("loan", -i, "loan_interest", crate::tr::t("bank.student_loan_interest_label"));
                l.interest -= i as f64;
            }
            if today.day == 10 {
                let pay = l.monthly.min(l.owed);
                if purse.take(pay, "loan_pay", crate::tr::t("bank.student_loan_repay_label")) {
                    l.owed -= pay;
                    purse.post("loan", pay, "loan_pay", crate::tr::t("bank.student_loan_repay_label"));
                    let owed = l.owed;
                    b.note(day, &crate::tr::m("bank.student_loan_payment_made", &[("payment", crate::tr::a(&crate::tr::money(pay))), ("owed", crate::tr::a(&crate::tr::money(owed)))]));
                } else {
                    shout.push(crate::tr::t("bank.student_loan_debit_failed").into());
                }
            }
        }
    }
    // Tuition and dorm: open 7 days before the term, due 14 days after the start; unpaid, the school files a leave.
    let fee_term = campus::fees_term(civil);
    // While it is open and unpaid it stays on his task list (two passing notices were easy to miss: he was put on leave).
    let mut fees_due = String::new();
    if let Some(t) = &fee_term {
        if s(&me, "status") == "enrolled" && s(&me, "fees_paid") != t.key {
            let d = crate::calendar::date(campus::game_day(t.start + campus::FEES_DEADLINE_DAYS), 8.0);
            fees_due = crate::tr::m("bank.tuition_due_task", &[("amount", crate::tr::a(&crate::tr::money(campus::TUITION_FEE() + campus::DORM_FEE()))), ("month", crate::tr::a(&(d.month as i64))), ("day", crate::tr::a(&(d.day as i64)))]);
        }
    }
    if let Some(t) = &fee_term {
        if s(&me, "status") == "enrolled" && s(&me, "fees_paid") != t.key {
            if civil == t.start - campus::FEES_OPEN_DAYS {
                shout.push(crate::tr::m("bank.tuition_due_notice", &[("term", crate::tr::a(&t.key)), ("tuition", crate::tr::a(&crate::tr::money(campus::TUITION_FEE()))), ("dorm", crate::tr::a(&crate::tr::money(campus::DORM_FEE())))]));
            }
            if civil == t.start + campus::FEES_DEADLINE_DAYS - 3 {
                shout.push(crate::tr::t("bank.tuition_due_three_days").into());
            }
        }
    }
    {
        let (t, phase) = campus::term_at(civil);
        if phase != Phase::Break && civil == t.start + campus::FEES_DEADLINE_DAYS + 1 && s(&me, "status") == "enrolled" && s(&me, "fees_paid") != t.key {
            let until = campus::next_term_key(&t.key);
            me["status"] = json!("leave");
            me["leave_until"] = json!(until);
            shout.push(crate::tr::m("bank.tuition_unpaid_leave", &[("term", crate::tr::a(&until))]));
        }
    }
    // Heroines: monthly saving, quarterly interest, saving goals, their own tuition.
    let mut academics = Map::new();
    for h in &heroines {
        let before = h.relation.clone();
        let mut r = before.clone();
        if r.cash < 0 {
            continue;
        }
        let money = h.profile.get("money").cloned().unwrap_or(Value::Null);
        let style = money.get("spending_style").and_then(Value::as_str).unwrap_or("");
        if today.day == 1 {
            let pct = match style {
                "frugal" => 0.30f32,
                "generous" => 0.05,
                "lavish" => 0.0,
                _ => 0.15,
            };
            let mv = ((r.cash - crate::region::amount("cash", 500)).max(0) as f32 * pct) as i64;
            r.cash -= mv;
            r.bank += mv;
        }
        if today.month % 3 == 0 && today.day == 21 {
            r.bank += (r.bank as f64 * rate.demand as f64 / 100.0 * 0.25) as i64;
        }
        // Saving goals from her profile: once the money is there (keeping ¥300), she buys it.
        for g in money.get("saving_goals").and_then(Value::as_array).into_iter().flatten() {
            let item = s(g, "item");
            let amount = g.get("amount").and_then(Value::as_f64).unwrap_or(0.0);
            let flag = format!("goal:{item}");
            if item.is_empty() || amount <= 0.0 || r.has_flag(&flag) || r.bank + r.cash - crate::region::amount("cash", 300) < amount as i64 {
                continue;
            }
            let need = amount as i64;
            let from_bank = r.bank.min(need);
            r.bank -= from_bank;
            r.cash -= need - from_bank;
            r.add_flag(&flag);
            r.fun = (r.fun + 15.0).min(100.0);
            fx_observe(purse.fx, &r.id, &crate::tr::m("bank.saved_bought_item", &[("item_name", crate::tr::a(&item)), ("price", crate::tr::a(&crate::tr::money(need)))]));
        }
        if let (Some(a), Some(t)) = (&h.academic, &fee_term) {
            if s(a, "status") == "enrolled" && civil >= t.start && s(a, "fees_paid") != t.key {
                // Her family pays the term unless her profile says she pays it herself (tuition_by "self"): charging
                // everyone left six of ten in debt on the first day of term, borrowing from him every day.
                if money.get("tuition_by").and_then(Value::as_str) == Some("self") {
                    charge_bill(&mut r, campus::TUITION_FEE() + campus::DORM_FEE());
                }
                let mut a = a.clone();
                a["fees_paid"] = json!(t.key);
                academics.insert(r.id.clone(), a);
            }
        }
        purse.fx.relation(&r, &before);
    }
    for t in &shout {
        b.note(day, t);
    }
    Ok(json!({"banking": b, "player_academic": me, "academics": academics, "fees_due": fees_due, "effects": fx.into_value(), "message": shout.join(crate::tr::t("academics.semicolon_sep"))}))
}

fn fx_observe(fx: &mut Effects, id: &str, text: &str) {
    fx.observe(id, "saving_goal", text, "", 0.75, "observation");
}

// ------------------------------------------------------------------------------------------------ his actions

/// What he does at the bank (online or at the counter). Request: what, amount, arg, day, player_cash, player_bank,
/// banking, player_academic; card_charge also place and why. Answer: done (message) or notice (why not), and the changed
/// banking, academic record and effects.
pub fn action(request: &Value) -> Result<Value, String> {
    let what = request.get("what").and_then(Value::as_str).unwrap_or("");
    let amount = request.get("amount").and_then(Value::as_i64).unwrap_or(0);
    let arg = request.get("arg").and_then(Value::as_i64).unwrap_or(0) as i32;
    let day = request.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
    let mut b: Banking = serde_json::from_value(request.get("banking").cloned().unwrap_or(json!({}))).map_err(|e| format!("banking: {e}"))?;
    let mut me = request.get("player_academic").cloned().unwrap_or(json!({}));
    let mut fx = Effects::default();
    let mut purse = Purse { cash: request.get("player_cash").and_then(Value::as_i64).unwrap_or(0), bank: request.get("player_bank").and_then(Value::as_i64).unwrap_or(0), fx: &mut fx };
    let rate = rates_on(ymd(day));
    let civil = campus::civil_day(day);
    let refuse = |notice: &str| Ok(json!({"notice": notice}));
    let done: String = match what {
        "deposit_open" => {
            if amount < crate::region::amount("cash", 100) {
                return refuse(&crate::tr::m("bank.term_deposit_minimum", &[("amount", crate::tr::a(&crate::tr::money(crate::region::amount("cash", 100))))]));
            }
            if purse.bank < amount {
                return refuse(crate::tr::t("bank.checking_insufficient_hint"));
            }
            let id = b.next_deposit;
            b.next_deposit += 1;
            purse.bank_move(-amount, "deposit_term", &crate::tr::m("bank.fixed_deposit_opened", &[("deposit_id", crate::tr::a(&id))]));
            purse.post("deposit", amount, "deposit_term", &crate::tr::m("bank.new_term_deposit_label", &[("deposit_id", crate::tr::a(&id))]));
            let d = Deposit { id, amount, months: arg, start: day, rate: term_rate(&rate, arg) };
            let msg = crate::tr::m("bank.new_term_deposit_notice", &[("deposit_id", crate::tr::a(&d.id)), ("amount", crate::tr::a(&crate::tr::money(amount))), ("months", crate::tr::a(&arg)), ("rate", crate::tr::a(&d.rate))]);
            b.deposits.push(d);
            msg
        }
        "deposit_close" => {
            let Some(i) = b.deposits.iter().position(|d| d.id == arg) else { return Ok(json!({})) };
            let d = b.deposits.remove(i);
            let days = (day - d.start).max(0);
            let interest = round_half_away(d.amount as f64 * rate.demand as f64 / 100.0 * days as f64 / 360.0);
            purse.post("deposit", -d.amount, "deposit_term", &crate::tr::m("bank.term_deposit_closed", &[("deposit_id", crate::tr::a(&d.id))]));
            purse.bank_move(d.amount, "deposit_term", &crate::tr::m("bank.term_deposit_principal_returned", &[("deposit_id", crate::tr::a(&d.id))]));
            purse.bank_move(interest, "interest", &crate::tr::m("bank.term_deposit_early_interest", &[("deposit_id", crate::tr::a(&d.id))]));
            crate::tr::m("bank.term_deposit_early_close", &[("deposit_id", crate::tr::a(&d.id)), ("principal", crate::tr::a(&crate::tr::money(d.amount))), ("interest", crate::tr::a(&crate::tr::money(interest)))])
        }
        "card_apply" => {
            if b.card.issued {
                return Ok(json!({}));
            }
            b.card = Card { issued: true, ..Default::default() };
            crate::tr::m("bank.card_approved_notice", &[("limit", crate::tr::a(&crate::tr::money(b.card.limit)))])
        }
        "card_pay" => {
            let cd = &mut b.card;
            let pay = (if amount < 0 { cd.balance } else { amount }).min(cd.balance);
            if pay <= 0 {
                return Ok(json!({}));
            }
            if !purse.take(pay, "card_pay", crate::tr::t("bank.card_payment_label")) {
                return refuse(crate::tr::t("bank.savings_wallet_insufficient"));
            }
            purse.post("card", pay, "card_pay", crate::tr::t("bank.card_payment_label"));
            cd.balance -= pay;
            cd.statement = (cd.statement - pay).max(0);
            cd.min_due = (cd.min_due - pay).max(0);
            if cd.statement == 0 {
                cd.interest = 0.0;
            }
            crate::tr::m("bank.card_payment_made", &[("payment", crate::tr::a(&crate::tr::money(pay))), ("balance", crate::tr::a(&crate::tr::money(cd.balance)))])
        }
        "card_charge" => {
            // Paying by card where cards are taken (department store, hotel, restaurant, aquarium), within the limit.
            let place = request.get("place").and_then(Value::as_str).unwrap_or("");
            let why = request.get("why").and_then(Value::as_str).unwrap_or("");
            let cd = &mut b.card;
            if !cd.issued || cd.stopped || !matches!(place, "department" | "hotel" | "restaurant" | "aquarium") || cd.balance + amount > cd.limit {
                return Ok(json!({"charged": false}));
            }
            cd.balance += amount;
            // What it was for: the caller says ("food" at a till); without it, shopping.
            let category = request.get("category").and_then(Value::as_str).filter(|c| !c.is_empty()).unwrap_or("shopping");
            purse.post("card", -amount, category, &crate::tr::m("bank.card_swipe_reason", &[("reason", crate::tr::a(&why))]));
            b.note(day, &crate::tr::m("bank.card_swipe_detail", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("reason", crate::tr::a(&why))]));
            return Ok(json!({"charged": true, "banking": b, "effects": fx.into_value()}));
        }
        "phone_pay" => {
            if b.phone_arrears <= 0 {
                return Ok(json!({}));
            }
            if purse.take(b.phone_arrears, "phone", crate::tr::t("bank.clear_phone_debt")) {
                purse.post("arrears", b.phone_arrears, "phone", crate::tr::t("bank.clear_phone_debt"));
            } else {
                return refuse(crate::tr::t("bank.cannot_pay_phone_debt"));
            }
            let msg = crate::tr::m("bank.phone_arrears_paid", &[("amount", crate::tr::a(&crate::tr::money(b.phone_arrears)))]);
            b.phone_arrears = 0;
            msg
        }
        "fees_pay" | "loan_apply" => {
            let Some(t) = campus::fees_term(civil) else { return refuse(crate::tr::t("bank.not_tuition_period")) };
            if s(&me, "status") != "enrolled" {
                return refuse(crate::tr::t("bank.no_tuition_needed"));
            }
            if s(&me, "fees_paid") == t.key {
                return refuse(crate::tr::t("bank.tuition_already_paid"));
            }
            let fees = campus::TUITION_FEE() + campus::DORM_FEE();
            let msg = if what == "fees_pay" {
                if !purse.take(fees, "tuition", &crate::tr::m("bank.term_fees_label", &[("term", crate::tr::a(&t.key))])) {
                    return Ok(json!({"notice": crate::tr::m("bank.insufficient_tuition_loan", &[("amount", crate::tr::a(&crate::tr::money(fees)))])}));
                }
                crate::tr::m("bank.fees_paid_notice", &[("term", crate::tr::a(&t.key)), ("fees", crate::tr::a(&crate::tr::money(fees)))])
            } else {
                let year = t.key.split('-').next().unwrap_or("").to_string();
                if b.loan.year != year {
                    b.loan.year = year;
                    b.loan.taken_this_year = 0;
                }
                if b.loan.taken_this_year + fees > LOAN_YEAR_CAP() {
                    return Ok(json!({"notice": crate::tr::m("bank.loan_cap_remaining", &[("amount", crate::tr::a(&crate::tr::money(LOAN_YEAR_CAP() - b.loan.taken_this_year)))])}));
                }
                b.loan.owed += fees;
                purse.post("loan", -fees, "tuition", &crate::tr::m("bank.student_loan_direct_pay_label", &[("term", crate::tr::a(&t.key))]));
                b.loan.taken_this_year += fees;
                b.loan.repay_from = -1;
                crate::tr::m("bank.student_loan_approved_direct_pay", &[("fees", crate::tr::a(&crate::tr::money(fees))), ("term", crate::tr::a(&t.key))])
            };
            me["fees_paid"] = json!(t.key);
            msg
        }
        _ => return Err(format!("unknown_bank_action: {what}")),
    };
    b.note(day, &done);
    Ok(json!({"done": done, "banking": b, "player_academic": me, "effects": fx.into_value()}))
}

// ------------------------------------------------------------------------------------------------ going broke

/// A lover of his who learns about it (only lovers know).
#[derive(Deserialize, Debug, Clone)]
struct Lover {
    relation: Relation,
    #[serde(default)]
    profile: Value,
    #[serde(default)]
    name: String,
}

fn father_of(b: &Banking) -> Map<String, Value> {
    b.father.as_object().cloned().unwrap_or_default()
}
fn fnum(f: &Map<String, Value>, k: &str, def: f64) -> f64 {
    f.get(k).and_then(Value::as_f64).unwrap_or(def)
}

/// Net worth below what the student loan leaves: liquidation. Every deposit closed, everything in hand to the creditors
/// (card, then phone arrears), the card stopped, the father acts next. Request: day, now, player_cash, player_bank,
/// net_worth (Unreal's sum, before), player_name, banking, lovers [{relation, profile, name}]. Answer: banking, effects,
/// lines (the scene), shortfall.
fn bankrupt(r: &Value) -> Result<Value, String> {
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    let now = r.get("now").and_then(Value::as_i64).unwrap_or(0);
    let mut b: Banking = serde_json::from_value(r.get("banking").cloned().unwrap_or(json!({}))).map_err(|e| format!("banking: {e}"))?;
    let lovers: Vec<Lover> = serde_json::from_value(r.get("lovers").cloned().unwrap_or(json!([]))).map_err(|e| format!("lovers: {e}"))?;
    let (cash, bank) = (r.get("player_cash").and_then(Value::as_i64).unwrap_or(0), r.get("player_bank").and_then(Value::as_i64).unwrap_or(0));
    let net_worth = r.get("net_worth").and_then(Value::as_i64).unwrap_or(0);
    let mut fx = Effects::default();
    let had_deposits = !b.deposits.is_empty();
    let deposits = b.deposits_total();
    let mut pool = cash + bank + deposits;
    fx.post("private", "deposit", -deposits, "liquidated", crate::tr::t("bank.force_close_term_deposit"));
    fx.cash(-cash, "liquidated", crate::tr::t("bank.account_frozen_liquidation"));
    fx.bank(-bank, "liquidated", crate::tr::t("bank.account_frozen_liquidation"));
    b.deposits.clear();
    let phone_owed = b.phone_arrears;
    let to_card = pool.min(b.card.balance);
    b.card.balance -= to_card;
    pool -= to_card;
    b.card.statement = b.card.statement.min(b.card.balance);
    b.card.min_due = b.card.min_due.min(b.card.balance);
    let to_phone = pool.min(b.phone_arrears);
    b.phone_arrears -= to_phone;
    pool -= to_phone;
    fx.post("private", "card", to_card, "liquidated", crate::tr::t("bank.settle_credit_card_repay"));
    fx.post("private", "arrears", to_phone, "liquidated", crate::tr::t("bank.settle_phone_debt_repay"));
    fx.cash(pool, "liquidated", crate::tr::t("bank.cash_left_after_settlement"));
    b.card.stopped = true;
    // Paying debts with assets leaves the net worth where it was.
    let short = (-(net_worth + b.loan.owed)).max(1);
    // What the notices say he still owes: the card and phone debts left after the seizure (the net-worth shortfall
    // nets in money others owe him, which the creditors do not take; it only sizes the father's gift).
    let owed = match b.card.balance + b.phone_arrears { 0 => short, o => o };
    let mut f = father_of(&b);
    f.insert("bankrupt".into(), json!(true));
    f.insert("bankrupt_count".into(), json!(fnum(&f, "bankrupt_count", 0.0) as i64 + 1));
    f.insert("shortfall".into(), json!(short));
    f.insert("rescue_at".into(), json!(now));
    b.father = Value::Object(f);
    b.note(day, &crate::tr::m("bank.forced_liquidation_still_owed", &[("shortfall", crate::tr::a(&crate::tr::money(owed)))]));
    // Only his lovers know. The romantic stand by him, the mercenary cool off.
    let mut names = Vec::new();
    for l in &lovers {
        let mut rel = l.relation.clone();
        let before = rel.clone();
        let material = crate::traits::profile_str(&l.profile, "material");
        rel.admiration = (rel.admiration + if material == "Mercenary" { -15.0 } else { -3.0 }).clamp(0.0, 100.0);
        rel.trust = (rel.trust + if material == "Romantic" { 3.0 } else { -2.0 }).clamp(0.0, 100.0);
        fx.relation(&rel, &before);
        fx.observe(&rel.id, "bankrupt", &crate::tr::m("bank.bankrupt_debt_summary", &[("shortfall", crate::tr::a(&crate::tr::money(owed)))]), "", 0.85, "observation");
        fx.recent(&rel.id, crate::tr::t("bank.learned_bankrupt_frozen"));
        names.push(if l.name.is_empty() { rel.id.clone() } else { l.name.clone() });
    }
    let name = r.get("player_name").and_then(Value::as_str).filter(|x| !x.is_empty()).unwrap_or(crate::tr::t("academics.you_pronoun"));
    let mut lines = vec![
        crate::tr::t("bank.card_overdue_frozen_notice").to_string(),
        if had_deposits { crate::tr::m("bank.term_deposit_liquidated_notice", &[("amount", crate::tr::a(&crate::tr::money(to_card)))]) } else { crate::tr::m("bank.forced_card_offset_notice", &[("amount", crate::tr::a(&crate::tr::money(to_card)))]) },
        if phone_owed > 0 { crate::tr::m("bank.phone_arrears_deducted", &[("amount", crate::tr::a(&crate::tr::money(to_phone)))]) } else { crate::tr::t("bank.phone_balance_low_notice").to_string() },
        crate::tr::m("bank.collection_debt_notice", &[("name", crate::tr::a(&name)), ("shortfall", crate::tr::a(&crate::tr::money(owed)))]),
        crate::tr::m("bank.bank_balance_summary", &[("wallet", crate::tr::a(&crate::tr::money(pool))), ("deposit", crate::tr::a(&crate::tr::money(owed))), ("debt", crate::tr::a(&crate::tr::money(0)))]),
        crate::tr::t("bank.stare_at_red_text").to_string(),
    ];
    if !names.is_empty() {
        let at = lines.len() - 1;
        lines.insert(at, crate::tr::m("bank.names_know_notice", &[("names", crate::tr::a(&(names.join(crate::tr::t("bank.list_separator")))))]));
    }
    Ok(json!({"banking": b, "effects": fx.into_value(), "lines": lines, "shortfall": short}))
}

/// The morning after: the father he never met sends the next ¥100,000 above the shortfall and a text. Request: day, now,
/// player_bank, banking, lovers. Answer: banking, effects, gift, message.
fn father_rescue(r: &Value) -> Result<Value, String> {
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    let now = r.get("now").and_then(Value::as_i64).unwrap_or(0);
    let mut b: Banking = serde_json::from_value(r.get("banking").cloned().unwrap_or(json!({}))).map_err(|e| format!("banking: {e}"))?;
    let lovers: Vec<Lover> = serde_json::from_value(r.get("lovers").cloned().unwrap_or(json!([]))).map_err(|e| format!("lovers: {e}"))?;
    let mut f = father_of(&b);
    let gift = father_gift(fnum(&f, "shortfall", 0.0) as i64);
    let gifts = fnum(&f, "gifts", 0.0) as i64 + 1;
    f.insert("gifts".into(), json!(gifts));
    f.insert("total".into(), json!(fnum(&f, "total", 0.0) as i64 + gift));
    f.insert("day".into(), json!(day));
    f.insert("bankrupt".into(), json!(false));
    f.insert("rescue_at".into(), json!(-1));
    let mut fx = Effects::default();
    fx.bank(gift, "father", crate::tr::t("bank.overseas_remit_unknown_sender"));
    if b.card.balance == 0 {
        b.card = Card::default(); // a clean slate: he can apply again
    }
    let yuan = crate::tr::money(gift);
    let lines: [&'static str; 5] = [
        "bank.dad_letter_message",
        "bank.dad_fell_again_speech",
        "bank.dad_support_gift",
        "bank.dad_collector_speech",
        "bank.dad_money_arrived_speech",
    ];
    let bank_after = r.get("player_bank").and_then(Value::as_i64).unwrap_or(0) + gift;
    let text = crate::tr::m(lines[((gifts - 1) as usize).min(lines.len() - 1)], &[("amount", crate::tr::a(&yuan))]) + &crate::tr::m("bank.overseas_remit_received", &[("amount", crate::tr::a(&yuan)), ("balance", crate::tr::a(&crate::tr::money(bank_after)))]);
    let mut texts = f.get("texts").and_then(Value::as_array).cloned().unwrap_or_default();
    texts.push(json!({"minute": now, "text": text}));
    f.insert("texts".into(), Value::Array(texts));
    b.father = Value::Object(f);
    b.note(day, &crate::tr::m("bank.overseas_remit_received_short", &[("amount", crate::tr::a(&yuan))]));
    // His lovers learn who saved him.
    for l in &lovers {
        let mut rel = l.relation.clone();
        let before = rel.clone();
        rel.admiration = (rel.admiration + 6.0).clamp(0.0, 100.0);
        rel.mystique = (rel.mystique + 10.0).clamp(0.0, 100.0);
        fx.relation(&rel, &before);
        fx.observe(&rel.id, "bankrupt", &crate::tr::m("bank.dad_remit_after_bankrupt", &[("amount", crate::tr::a(&yuan))]), "", 0.85, "observation");
        fx.recent(&rel.id, &crate::tr::m("bank.learned_dad_remit", &[("amount", crate::tr::a(&yuan))]));
    }
    Ok(json!({"banking": b, "effects": fx.into_value(), "gift": gift, "message": crate::tr::m("bank.unknown_sms_deposit", &[("amount", crate::tr::a(&yuan))])}))
}

pub fn eval(kind: &str, request: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "bank_daily" => daily(request),
        "bank_action" => action(request),
        "bank_rates" => (|| {
            let day = request.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
            let rate = rates_on(ymd(day));
            let terms: Vec<Value> = DEPOSIT_TERMS.iter().map(|m| json!({"months": m, "rate": term_rate(&rate, *m)})).collect();
            Ok(json!({"rates": rate, "terms": terms, "phone_fee": PHONE_FEE(), "card_limit": CARD_LIMIT(), "loan_year_cap": LOAN_YEAR_CAP(), "tuition": campus::TUITION_FEE(), "dorm": campus::DORM_FEE()}))
        })(),
        "bank_scholarship" => Ok(json!({"award": scholarship(request.get("average").and_then(Value::as_f64).unwrap_or(0.0) as f32, request.get("any_failed").and_then(Value::as_bool).unwrap_or(false))})),
        "bank_bankrupt" => bankrupt(request),
        "bank_father_rescue" => father_rescue(request),
        "bank_father_gift" => Ok(json!({"gift": father_gift(request.get("shortfall").and_then(Value::as_i64).unwrap_or(0))})),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::calendar::days_from_civil;

    fn ymd_of(y: i64, m: u32, d: u32) -> i32 {
        (y * 10000 + m as i64 * 100 + d as i64) as i32
    }

    // Ported from Tests/OKStreetBank.spec.cpp.
    #[test]
    fn spec_rates_scholarship_and_father() {
        assert_eq!(rates_on(ymd_of(2010, 9, 13)).y1, 2.25);
        assert_eq!(rates_on(ymd_of(2010, 10, 19)).y1, 2.25);
        assert_eq!(rates_on(ymd_of(2010, 10, 20)).y1, 2.50);
        assert_eq!(rates_on(ymd_of(2011, 8, 1)).y1, 3.50);
        assert_eq!(rates_on(ymd_of(2010, 9, 13)).demand, 0.36);
        assert!(rates_on(ymd_of(2025, 12, 1)).demand < 0.1);
        assert!(RATES.windows(2).all(|w| w[0].from < w[1].from), "rows in date order");
        assert_eq!(term_rate(&rates_on(ymd_of(2010, 9, 13)), 6), 1.98);
        assert_eq!((scholarship(92.0, false), scholarship(86.0, false), scholarship(80.0, false), scholarship(79.9, false), scholarship(95.0, true)), (4000, 2000, 1000, 0, 0));
        assert_eq!((father_gift(3000), father_gift(100_000), father_gift(230_000), father_gift(0)), (100_000, 200_000, 300_000, 0));
    }

    fn game_day(y: i64, m: u32, d: u32) -> i32 {
        (days_from_civil(y, m, d) - days_from_civil(2010, 9, 1)) as i32
    }

    fn run(extra: Value) -> Value {
        let mut req = json!({"day": game_day(2010, 9, 21), "player_cash": 100, "player_bank": 1000, "net_worth": 5000, "banking": {}, "player_academic": {"status": "enrolled"}, "heroines": []});
        req.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
        daily(&req).unwrap()
    }

    #[test]
    fn net_worth_interest_and_quarterly_payment() {
        let v = run(json!({}));
        assert_eq!(v["banking"]["net_worth"][0], json!([game_day(2010, 9, 21) - 1, 5000]));
        // 9/21 is a quarter payday: accrued 1000*0.36%/360 = 0.01, floor 0 -> nothing paid yet.
        assert!(v["effects"].as_array().unwrap().iter().all(|e| e["category"] != "interest"));
        let v = run(json!({"banking": {"demand_interest": 12.7}}));
        let fx = v["effects"].as_array().unwrap();
        assert!(fx.iter().any(|e| e["op"] == "bank" && e["amount"] == 12 && e["category"] == "interest"));
        assert!(v["banking"]["notes"][0].as_str().unwrap().contains("活期存款結息 ¥12"));
    }

    #[test]
    fn phone_bill_cuts_off_when_he_cannot_pay() {
        let v = run(json!({"day": game_day(2010, 10, 10), "player_cash": 10, "player_bank": 20}));
        assert_eq!(v["banking"]["phone_arrears"], 58);
        assert!(v["message"].as_str().unwrap().contains("停話"));
        let v = run(json!({"day": game_day(2010, 10, 10), "player_cash": 100, "player_bank": 20, "banking": {"phone_arrears": 58}}));
        let fx = v["effects"].as_array().unwrap();
        assert!(fx.iter().any(|e| e["op"] == "bank" && e["amount"] == -20) && fx.iter().any(|e| e["op"] == "cash" && e["amount"] == -96), "savings first, then the wallet: {fx:?}");
        assert_eq!(v["banking"]["phone_arrears"], 0);
    }

    #[test]
    fn liquidation_notes_the_debt_left_not_the_net_shortfall() {
        // Card 1100, nothing to seize; she owes him 540, so his net worth is -560: the notes still say 1100.
        let v = bankrupt(&json!({"day": 27, "now": 27 * 1440, "player_cash": 0, "player_bank": 0, "net_worth": -560, "banking": {"card": {"issued": true, "balance": 1100}}})).unwrap();
        assert_eq!(v["banking"]["card"]["balance"], 1100);
        assert_eq!(v["shortfall"], 560);
        let note = v["banking"]["notes"][0].as_str().unwrap();
        assert!(note.contains("1,100") && !note.contains("560"), "{note}");
    }

    #[test]
    fn card_statement_interest_and_late_fee() {
        let day5 = game_day(2010, 10, 5);
        let v = run(json!({"day": day5, "banking": {"card": {"issued": true, "balance": 1000}}}));
        let cd = &v["banking"]["card"];
        assert_eq!((cd["statement"].as_i64(), cd["min_due"].as_i64(), cd["due_day"].as_i64()), (Some(1000), Some(100), Some(day5 as i64 + 20)));
        let v = run(json!({"day": day5 + 21, "banking": {"card": {"issued": true, "balance": 1000, "statement": 1000, "min_due": 100, "due_day": day5 + 20, "interest": 0.6, "late": 2}}}));
        let cd = &v["banking"]["card"];
        // Interest 0.6 + 0.5 = 1.1 -> 1; late fee 5 on min due 100; third late: stopped.
        assert_eq!((cd["balance"].as_i64(), cd["late"].as_i64(), cd["stopped"].as_bool()), (Some(1006), Some(3), Some(true)));
    }

    #[test]
    fn unpaid_fees_mean_a_leave_and_heroines_pay_tuition() {
        let start = game_day(2010, 9, 13);
        let v = run(json!({"day": start + 15, "player_academic": {"status": "enrolled", "fees_paid": ""}}));
        assert_eq!(v["player_academic"]["status"], "leave");
        assert_eq!(v["player_academic"]["leave_until"], "2010-2");
        // While the window is open and unpaid it is on his task list; paid (or past it), it is not.
        let v = run(json!({"day": start + 3, "player_academic": {"status": "enrolled", "fees_paid": ""}}));
        assert!(v["fees_due"].as_str().unwrap_or("").contains("9/27"), "{}", v["fees_due"]);
        let v = run(json!({"day": start + 3, "player_academic": {"status": "enrolled", "fees_paid": "2010-1"}}));
        assert_eq!(v["fees_due"], "");
        // She pays her own term when her profile says so; otherwise her family does (the term is settled, her money untouched).
        let v = run(json!({"day": start, "heroines": [{"relation": {"id": "a", "cash": 1000}, "profile": {"money": {"tuition_by": "self"}}, "academic": {"status": "enrolled", "fees_paid": ""}}]}));
        assert_eq!(v["academics"]["a"]["fees_paid"], "2010-1");
        let set = &v["effects"].as_array().unwrap().iter().find(|e| e["op"] == "relation").unwrap()["set"];
        assert_eq!((set["cash"].as_i64(), set["debt"].as_i64()), (Some(0), Some(2100)));
        let v = run(json!({"day": start, "heroines": [{"relation": {"id": "a", "cash": 1000}, "profile": {}, "academic": {"status": "enrolled", "fees_paid": ""}}]}));
        assert_eq!(v["academics"]["a"]["fees_paid"], "2010-1");
        assert!(v["effects"].as_array().unwrap().iter().all(|e| e["op"] != "relation" || e["set"]["debt"].as_i64().unwrap_or(0) == 0), "{}", v["effects"]);
    }

    #[test]
    fn heroine_saving_and_goals() {
        let v = run(json!({"day": game_day(2010, 10, 1), "heroines": [{"relation": {"id": "a", "cash": 1500, "bank": 0}, "profile": {"money": {"spending_style": "frugal", "saving_goals": [{"item": "包包", "amount": 900}]}}}]}));
        let fx = v["effects"].as_array().unwrap();
        let set = &fx.iter().find(|e| e["op"] == "relation").unwrap()["set"];
        // 30% of 1000 saved, then the 900 bag: 300 from the bank, 600 from her wallet.
        assert_eq!((set["cash"].as_i64(), set.get("bank")), (Some(600), None), "bank back to 0: unchanged");
        assert!(fx.iter().any(|e| e["op"] == "observe" && e["text"].as_str().unwrap().contains("包包")));
    }

    #[test]
    fn actions() {
        let day = game_day(2010, 9, 10);
        let base = |what: &str, amount: i64, arg: i32| json!({"what": what, "amount": amount, "arg": arg, "day": day, "player_cash": 500, "player_bank": 5000, "banking": {"next_deposit": 1}, "player_academic": {"status": "enrolled"}});
        let v = action(&base("deposit_open", 1000, 12)).unwrap();
        assert_eq!(v["banking"]["deposits"][0]["rate"], 2.25f32 as f64);
        assert!(action(&base("deposit_open", 50, 12)).unwrap()["notice"].is_string());
        let v = action(&base("loan_apply", 0, 0)).unwrap();
        assert_eq!((v["banking"]["loan"]["owed"].as_i64(), v["player_academic"]["fees_paid"].as_str()), (Some(3100), Some("2010-1")));
        let mut req = base("card_charge", 200, 0);
        req["place"] = json!("hotel");
        req["why"] = json!("結帳");
        req["banking"] = json!({"card": {"issued": true}});
        let v = action(&req).unwrap();
        assert_eq!((v["charged"].as_bool(), v["banking"]["card"]["balance"].as_i64()), (Some(true), Some(200)));
        req["place"] = json!("conveni");
        assert_eq!(action(&req).unwrap()["charged"], false);
    }

    #[test]
    fn banking_round_trips_the_save_codec() {
        // The record as OKStreetBankingJson writes it (Tests/OKStreetBank.spec.cpp's sample) must come back unchanged.
        let saved = json!({"deposits": [{"id": 3, "amount": 5000, "months": 12, "start": 40, "rate": 2.5}], "next_deposit": 4, "demand_interest": 0.37,
            "card": {"issued": true, "stopped": false, "limit": 3000, "balance": 1200, "statement": 800, "min_due": 80, "due_day": 55, "late": 1, "interest": 0.0},
            "loan": {"owed": 3100, "year": "2010", "taken_this_year": 3100, "repay_from": -1, "monthly": 0, "interest": 0.0}, "phone_arrears": 58,
            "notes": ["note"], "net_worth": [[12, 2674]],
            "father": {"texts": [{"minute": 20000, "text": "我是你爸。"}], "seen": 1, "gifts": 1, "day": 13, "total": 100000, "bankrupt": false, "bankrupt_count": 0, "shortfall": 0, "rescue_at": -1}});
        let b: Banking = serde_json::from_value(saved.clone()).unwrap();
        assert_eq!(serde_json::to_value(&b).unwrap(), saved);
    }
}
