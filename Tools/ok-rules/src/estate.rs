//! Companies (Docs/STREET_COMPANY_PLAN_20261012.md, Docs/STREET_HEROINE_COMPANY_PLAN_20261012.md): his company and the
//! heroines' own, run by one engine — property (rent, buy with or without a mortgage, build, sell, lease out), people
//! (applicants, hiring, morale, pay, severance), the monthly books with Chinese payroll and business taxes, bankruptcy,
//! and a heroine opening, growing and closing her own business.
//!
//! The money columns of a company (its account, the owner's loan, capital, retained profit, the ledger) belong to
//! Unreal's books: the rules simulate the same wallet to decide what can be paid, and answer with the wallet operations
//! in order (effect "wallet"), which Unreal replays through FOKCompanyWallet so the journal stays reconciled. Everything
//! else about the companies comes back as the new records.
use crate::calendar::date;
use crate::effects::Effects;
use crate::property::{round_half_from_zero, PropertyData, UnitDef};
use crate::relation::Relation;
use crate::rng::RandomStream;
use serde::{Deserialize, Deserializer, Serialize};
use serde_json::{json, Value};
use std::collections::{BTreeMap, BTreeSet};

/// The save writes int64 money as strings ("1234"); accept both.
fn de_i64<'d, D: Deserializer<'d>>(d: D) -> Result<i64, D::Error> {
    let v = Value::deserialize(d)?;
    Ok(match v {
        Value::String(s) => s.parse().unwrap_or(0),
        Value::Number(n) => n.as_i64().or_else(|| n.as_f64().map(|f| f as i64)).unwrap_or(0),
        _ => 0,
    })
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Holding {
    pub unit: String,
    /// rent | own | borrow
    pub mode: String,
    pub building: String,
    #[serde(rename = "use")]
    pub use_: String,
    pub landlord: String,
    pub tenant: String,
    #[serde(deserialize_with = "de_i64")]
    pub lease_rent: i64,
    pub since: i32,
    pub build_start: i32,
    pub build_done: i32,
    pub missed_rent: i32,
    pub missed_loan: i32,
    pub leave_month: i32,
    #[serde(deserialize_with = "de_i64")]
    pub rent: i64,
    #[serde(deserialize_with = "de_i64")]
    pub deposit: i64,
    #[serde(deserialize_with = "de_i64")]
    pub bought: i64,
    #[serde(deserialize_with = "de_i64")]
    pub build_paid: i64,
    #[serde(deserialize_with = "de_i64")]
    pub loan: i64,
    #[serde(deserialize_with = "de_i64")]
    pub loan_payment: i64,
    pub hq: bool,
}

impl Default for Holding {
    fn default() -> Self {
        Holding {
            unit: String::new(),
            mode: "rent".into(),
            building: String::new(),
            use_: String::new(),
            landlord: String::new(),
            tenant: String::new(),
            lease_rent: 0,
            since: 0,
            build_start: -1,
            build_done: -1,
            missed_rent: 0,
            missed_loan: 0,
            leave_month: -1,
            rent: 0,
            deposit: 0,
            bought: 0,
            build_paid: 0,
            loan: 0,
            loan_payment: 0,
            hq: false,
        }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Worker {
    pub id: String,
    pub name: String,
    pub role: String,
    pub holding: String,
    pub skill: i32,
    pub diligence: i32,
    pub loyalty: i32,
    pub since: i32,
    pub missed: i32,
    pub week: i32,
    pub morale: f32,
    #[serde(deserialize_with = "de_i64")]
    pub pay: i64,
    #[serde(deserialize_with = "de_i64")]
    pub ask: i64,
}

impl Default for Worker {
    fn default() -> Self {
        Worker { id: String::new(), name: String::new(), role: String::new(), holding: String::new(), skill: 50, diligence: 50, loyalty: 50, since: 0, missed: 0, week: 0, morale: 70.0, pay: 0, ask: 0 }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Estate {
    pub holdings: Vec<Holding>,
    pub workers: Vec<Worker>,
    pub applicants: Vec<Worker>,
    /// The company's own book lines; Unreal's wallet writes them (passed through untouched).
    pub ledger: Value,
    pub refused: Vec<String>,
    pub tax_year: i32,
    #[serde(deserialize_with = "de_i64")]
    pub loss_carry: i64,
    #[serde(deserialize_with = "de_i64")]
    pub ytd_profit: i64,
    #[serde(deserialize_with = "de_i64")]
    pub cash: i64,
    #[serde(deserialize_with = "de_i64")]
    pub owner_loan: i64,
    #[serde(deserialize_with = "de_i64")]
    pub capital: i64,
    #[serde(deserialize_with = "de_i64")]
    pub retained: i64,
    #[serde(deserialize_with = "de_i64")]
    pub owner_pay: i64,
    pub auto_fund: bool,
    pub applicant_week: i32,
    pub next_worker: i32,
    pub reputation: f32,
    pub debt: i32,
}

impl Default for Estate {
    fn default() -> Self {
        Estate {
            holdings: Vec::new(),
            workers: Vec::new(),
            applicants: Vec::new(),
            ledger: json!([]),
            refused: Vec::new(),
            tax_year: 0,
            loss_carry: 0,
            ytd_profit: 0,
            cash: 0,
            owner_loan: 0,
            capital: 0,
            retained: 0,
            owner_pay: 0,
            auto_fund: true,
            applicant_week: -1,
            next_worker: 1,
            reputation: 50.0,
            debt: 0,
        }
    }
}

impl Estate {
    pub fn hq(&self) -> Option<&Holding> {
        self.holdings.iter().find(|h| h.hq)
    }
}

/// A heroine on his staff (FOKStreetEmployee).
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Default)]
#[serde(default)]
pub struct Employee {
    pub heroine: String,
    pub role: String,
    #[serde(deserialize_with = "de_i64")]
    pub pay: i64,
    pub title_public: bool,
    pub exclusive: bool,
    pub since: i32,
    pub missed: i32,
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Company {
    pub name: String,
    pub office: String,
    pub founded: i32,
    pub paid_month: i32,
    #[serde(deserialize_with = "de_i64")]
    pub capital: i64,
    #[serde(deserialize_with = "de_i64")]
    pub last_profit: i64,
    pub staff: Vec<Employee>,
    pub estate: Estate,
}

impl Default for Company {
    fn default() -> Self {
        Company { name: String::new(), office: String::new(), founded: -1, paid_month: -1, capital: 0, last_profit: 0, staff: Vec::new(), estate: Estate::default() }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct HeroCompany {
    pub owner: String,
    pub name: String,
    pub kind: String,
    pub founded: i32,
    pub paid_month: i32,
    pub profit_streak: i32,
    pub loss_streak: i32,
    pub offer_until: i32,
    pub offer_week: i32,
    #[serde(deserialize_with = "de_i64")]
    pub capital: i64,
    #[serde(deserialize_with = "de_i64")]
    pub last_profit: i64,
    #[serde(deserialize_with = "de_i64")]
    pub offer_wage: i64,
    #[serde(deserialize_with = "de_i64")]
    pub player_minutes: i64,
    pub estate: Estate,
}

impl Default for HeroCompany {
    fn default() -> Self {
        HeroCompany {
            owner: String::new(),
            name: String::new(),
            kind: String::new(),
            founded: 0,
            paid_month: -1,
            profit_streak: 0,
            loss_streak: 0,
            offer_until: -1,
            offer_week: -1,
            capital: 0,
            last_profit: 0,
            offer_wage: 0,
            player_minutes: 0,
            estate: Estate::default(),
        }
    }
}

/// His tax records as an employee (FOKStreetPersonal).
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Default)]
#[serde(default)]
pub struct Personal {
    pub tax_year: i32,
    pub bonus_year: i32,
    #[serde(deserialize_with = "de_i64")]
    pub ytd_income: i64,
    #[serde(deserialize_with = "de_i64")]
    pub ytd_social: i64,
    #[serde(deserialize_with = "de_i64")]
    pub ytd_tax: i64,
    #[serde(deserialize_with = "de_i64")]
    pub housing_fund: i64,
    #[serde(deserialize_with = "de_i64")]
    pub pension_paid: i64,
    #[serde(deserialize_with = "de_i64")]
    pub ytd_bonus: i64,
    #[serde(deserialize_with = "de_i64")]
    pub ytd_bonus_tax: i64,
}

const BORROW: &str = "borrow:";
const HERO_JOB: &str = "hc_";

/// Year*12 + month of a game day (the books' month).
pub fn month_of(day: i32) -> i32 {
    let d = date(day, 12.0);
    (d.year * 12 + d.month as i64) as i32
}

fn kind_name(kind: &str) -> &'static str {
    match kind {
        "cafe" => crate::tr::t("estate.cafe_kind"),
        "shop" => crate::tr::t("estate.shop_kind"),
        _ => crate::tr::t("estate.studio_kind"),
    }
}

#[derive(Default)]
struct Month {
    revenue: i64,
    paid: i64,
    missed: bool,
    wages_missed: bool,
    quit: i32,
}

/// Everything one request touches: the companies, the money of whoever owns them, the heroines involved.
pub struct World<'a> {
    p: &'a PropertyData,
    pub day: i32,
    pub cash: i64,
    pub bank: i64,
    pub company: Company,
    pub heroes: Vec<HeroCompany>,
    pub rels: BTreeMap<String, Relation>,
    profiles: BTreeMap<String, Value>,
    names: BTreeMap<String, String>,
    place_names: BTreeMap<String, String>,
    pub personal: Personal,
    pub player_jobs: Vec<String>,
    pub job_rating: BTreeMap<String, i32>,
    pub fx: Effects,
    pub message: String,
    pub payslip: String,
    rng: RandomStream,
}

impl<'a> World<'a> {
    pub fn from_request(p: &'a PropertyData, r: &Value) -> Result<World<'a>, String> {
        let field = |k: &str| r.get(k).cloned().unwrap_or(Value::Null);
        let parse = |k: &str, def: Value| -> Result<Value, String> { Ok(r.get(k).cloned().filter(|v| !v.is_null()).unwrap_or(def)) };
        let rels: Vec<Relation> = serde_json::from_value(parse("relations", json!([]))?).map_err(|e| format!("relations: {e}"))?;
        Ok(World {
            p,
            day: r.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32,
            cash: r.get("player_cash").and_then(Value::as_i64).unwrap_or(0),
            bank: r.get("player_bank").and_then(Value::as_i64).unwrap_or(0),
            company: serde_json::from_value(parse("company", json!({}))?).map_err(|e| format!("company: {e}"))?,
            heroes: serde_json::from_value(parse("hero_companies", json!([]))?).map_err(|e| format!("hero_companies: {e}"))?,
            rels: rels.into_iter().map(|r| (r.id.clone(), r)).collect(),
            profiles: serde_json::from_value(parse("profiles", json!({}))?).unwrap_or_default(),
            names: serde_json::from_value(parse("names", json!({}))?).unwrap_or_default(),
            place_names: serde_json::from_value(parse("place_names", json!({}))?).unwrap_or_default(),
            personal: serde_json::from_value(parse("personal", json!({}))?).map_err(|e| format!("personal: {e}"))?,
            player_jobs: serde_json::from_value(parse("player_jobs", json!([]))?).unwrap_or_default(),
            job_rating: serde_json::from_value(parse("job_rating", json!({}))?).unwrap_or_default(),
            fx: Effects::default(),
            message: String::new(),
            payslip: String::new(),
            rng: RandomStream::new(field("seed").as_i64().unwrap_or(0) as i32),
        })
    }

    pub fn answer(self) -> Value {
        json!({
            "company": self.company,
            "hero_companies": self.heroes,
            "personal": self.personal,
            "player_jobs": self.player_jobs,
            "job_rating": self.job_rating,
            "effects": self.fx.into_value(),
            "message": self.message,
            "payslip": self.payslip,
            // The money as the rules simulated it, so Unreal can check its replay agrees.
            "check": {"player_cash": self.cash, "player_bank": self.bank},
        })
    }

    fn name_of(&self, id: &str) -> String {
        self.names.get(id).cloned().unwrap_or_else(|| id.to_string())
    }

    fn profile_int(&self, id: &str, section: &str, field: &str, def: i64) -> i64 {
        self.profiles.get(id).and_then(|p| p.get(section)).and_then(|s| s.get(field)).and_then(Value::as_f64).map(|v| v as i64).unwrap_or(def)
    }

    // ------------------------------------------------------------------------------------------ units

    pub fn unit_of(&self, unit: &str) -> UnitDef {
        if let Some(place) = unit.strip_prefix(BORROW) {
            let name = self.place_names.get(place).cloned().unwrap_or_else(|| place.to_string());
            return UnitDef { id: unit.into(), place: place.into(), kind: "office".into(), capacity: 3, traffic: 0.3, name: crate::tr::m("estate.borrowed_corner_label", &[("name", crate::tr::a(&name))]), ..Default::default() };
        }
        self.p.unit(unit).cloned().unwrap_or(UnitDef { id: unit.into(), name: unit.into(), ..Default::default() })
    }

    pub fn holding_place(&self, h: &Holding) -> String {
        let u = self.unit_of(&h.unit);
        if u.lot {
            u.id
        } else {
            u.place
        }
    }

    pub fn holding_capacity(&self, h: &Holding, day: i32) -> i32 {
        if !h.tenant.is_empty() {
            return 0;
        }
        let u = self.unit_of(&h.unit);
        if !u.lot {
            return u.capacity;
        }
        match self.p.building(&h.building) {
            Some(b) if h.build_done >= 0 && day >= h.build_done => b.capacity,
            _ => 0,
        }
    }

    pub fn holding_use(&self, h: &Holding) -> String {
        let u = self.unit_of(&h.unit);
        if u.lot {
            self.p.building(&h.building).map(|b| b.kind.clone()).unwrap_or_else(|| "land".into())
        } else {
            u.kind
        }
    }

    pub fn holding_value(&self, h: &Holding, day: i32) -> i64 {
        if h.mode != "own" {
            return 0;
        }
        let mut v = self.p.land_price(&self.unit_of(&h.unit), day);
        if let Some(b) = self.p.building(&h.building) {
            if h.build_start >= 0 {
                v += (b.cost as f32 * self.p.building_resale) as i64;
            }
        }
        v
    }

    pub fn holding_costs(&self, h: &Holding, day: i32) -> i64 {
        (if h.mode == "rent" { h.rent } else { 0 }) + h.loan_payment + if h.mode == "own" { (self.holding_value(h, day) as f32 * self.p.property_tax / 12.0) as i64 } else { 0 }
    }

    /// Who uses a unit: "player", a heroine id, or "" (the market).
    pub fn unit_user(&self, unit: &str) -> String {
        for h in &self.heroes {
            if h.estate.holdings.iter().any(|x| x.unit == unit) {
                return h.owner.clone();
            }
        }
        if self.company.estate.holdings.iter().any(|x| x.unit == unit) {
            return "player".into();
        }
        String::new()
    }

    pub fn headcount(&self, e: &Estate, unit: &str, at_hq: i32) -> i32 {
        let mut n = e.workers.iter().filter(|w| w.holding == unit).count() as i32;
        if e.hq().map(|h| h.unit == unit).unwrap_or(false) {
            n += at_hq;
        }
        n
    }

    /// A month's revenue of one holding: an office, what its people produce; a shop or café, the street's people passing
    /// (traffic, size), served by its staff. Hero staff: the heroines on his staff at headquarters; extra work: him at
    /// her company.
    pub fn revenue(&self, e: &Estate, h: &Holding, day: i32, hero_staff: Option<&[Employee]>, extra_work: f64) -> i64 {
        let use_ = self.holding_use(h);
        if self.holding_capacity(h, day) <= 0 {
            return 0;
        }
        let (mut office, mut serve, mut people) = (0.0f64, 0.0f64, 0i32);
        for w in e.workers.iter().filter(|w| w.holding == h.unit) {
            people += 1;
            let weight = self.p.role(&w.role).map(|r| r.weight as f64).unwrap_or(1.0);
            let work = (w.skill as f64 / 50.0) * weight * (0.6 + w.diligence as f64 / 250.0) * (0.5 + w.morale as f64 / 200.0);
            office += work * crate::region::amount("salary", 3500) as f64;
            serve += work;
        }
        if h.hq {
            for st in hero_staff.unwrap_or(&[]) {
                let comp = self.profile_int(&st.heroine, "occupation", "competence", 50) as f64;
                office += (crate::region::amount("salary", 3200) as f64 + comp * crate::region::amount("salary", 150) as f64) * match st.role.as_str() {
                    "ceo" => 1.6,
                    "manager" => 1.2,
                    _ => 0.9,
                };
                serve += comp / 50.0;
                people += 1;
            }
            if extra_work > 0.0 {
                office += extra_work * crate::region::amount("salary", 3500) as f64;
                serve += extra_work;
                people += 1;
            }
        }
        if use_ == "shop" || use_ == "cafe" {
            let u = self.unit_of(&h.unit);
            let need = (self.holding_capacity(h, day) / 2).max(1);
            if people == 0 {
                return 0;
            }
            let staffing = (people as f64 / need as f64).min(1.0);
            let quality = (serve / people.max(1) as f64).clamp(0.4, 2.0);
            return ((u.traffic * u.area) as f64 * if use_ == "cafe" { crate::region::amount("rent", 170) as f64 } else { crate::region::amount("rent", 140) as f64 } * staffing * quality) as i64;
        }
        office as i64
    }

    // ------------------------------------------------------------------------------------------ wallets

    fn estate(&self, book: &str) -> &Estate {
        match book.strip_prefix(HERO_JOB) {
            Some(owner) => &self.heroes.iter().find(|h| h.owner == owner).expect("hero book").estate,
            None => &self.company.estate,
        }
    }

    fn estate_mut(&mut self, book: &str) -> &mut Estate {
        match book.strip_prefix(HERO_JOB) {
            Some(owner) => &mut self.heroes.iter_mut().find(|h| h.owner == owner).expect("hero book").estate,
            None => &mut self.company.estate,
        }
    }

    fn owner_funds(&self, book: &str) -> i64 {
        match book.strip_prefix(HERO_JOB) {
            Some(owner) => self.rels.get(owner).map(|r| r.bank + r.cash.max(0)).unwrap_or(0),
            None => self.bank + self.cash,
        }
    }

    pub fn funds(&self, book: &str) -> i64 {
        let e = self.estate(book);
        e.cash.max(0) + if e.auto_fund { self.owner_funds(book) } else { 0 }
    }

    fn op(&mut self, book: &str, act: &str, amount: i64, kind: &str, note: &str) {
        self.fx.0.push(json!({"op": "wallet", "book": book, "act": act, "amount": amount, "kind": kind, "note": note}));
    }

    /// His savings first, then the wallet (OKPrivateTake), as the owner putting money in.
    fn player_take(&mut self, amount: i64) {
        let from_bank = self.bank.min(amount);
        self.bank -= from_bank;
        self.cash -= amount - from_bank;
    }

    /// The owner puts money in (owner_loan to be repaid, or capital). `quiet`: part of a pay (Unreal's Pay does it).
    fn from_owner_sim(&mut self, book: &str, amount: i64, kind: &str) -> bool {
        if amount <= 0 {
            return true;
        }
        if self.owner_funds(book) < amount {
            return false;
        }
        match book.strip_prefix(HERO_JOB) {
            Some(owner) => {
                if let Some(r) = self.rels.get_mut(owner) {
                    let b = r.bank.min(amount);
                    r.bank -= b;
                    if amount > b {
                        r.cash = r.cash.max(0) - (amount - b);
                    }
                }
            }
            None => self.player_take(amount),
        }
        let e = self.estate_mut(book);
        e.cash += amount;
        if kind == "owner_loan" {
            e.owner_loan += amount;
        } else {
            e.capital += amount;
        }
        true
    }

    pub fn from_owner(&mut self, book: &str, amount: i64, kind: &str, note: &str) -> bool {
        if !self.from_owner_sim(book, amount, kind) {
            return false;
        }
        if amount > 0 {
            self.op(book, "from_owner", amount, kind, note);
        }
        true
    }

    fn to_owner_sim(&mut self, book: &str, amount: i64, kind: &str) {
        let e = self.estate_mut(book);
        e.cash -= amount;
        if kind == "owner_repay" {
            e.owner_loan -= amount;
        }
        match book.strip_prefix(HERO_JOB) {
            Some(owner) => {
                if let Some(r) = self.rels.get_mut(owner) {
                    r.bank += amount;
                }
            }
            None => self.bank += amount,
        }
    }

    pub fn to_owner(&mut self, book: &str, amount: i64, kind: &str, note: &str) {
        if amount <= 0 {
            return;
        }
        self.to_owner_sim(book, amount, kind);
        self.op(book, "to_owner", amount, kind, note);
    }

    pub fn pay(&mut self, book: &str, amount: i64, kind: &str, note: &str) -> bool {
        if amount <= 0 {
            return true;
        }
        if self.funds(book) < amount {
            return false;
        }
        let has = self.estate(book).cash.max(0);
        if has < amount {
            self.from_owner_sim(book, amount - has, "owner_loan");
        }
        self.estate_mut(book).cash -= amount;
        self.op(book, "pay", amount, kind, note);
        true
    }

    pub fn receive(&mut self, book: &str, amount: i64, kind: &str, note: &str) {
        if amount == 0 {
            return;
        }
        self.estate_mut(book).cash += amount;
        self.op(book, "receive", amount, kind, note);
    }

    pub fn repay_owner(&mut self, book: &str, most: i64) {
        let e = self.estate(book);
        let n = most.min(e.owner_loan).min(e.cash);
        if n > 0 {
            self.to_owner_sim(book, n, "owner_repay");
            self.op(book, "repay_owner", most, "owner_repay", crate::tr::t("estate.repay_owner_shareholder_loan"));
        }
    }

    fn cash_op(&mut self, amount: i64, category: &str, note: &str) {
        self.cash += amount;
        self.fx.cash(amount, category, note);
    }

    fn bank_op(&mut self, amount: i64, category: &str, note: &str) {
        self.bank += amount;
        self.fx.bank(amount, category, note);
    }

    fn rumor(&mut self, about: &str, text: String, heat: f32) {
        self.fx.0.push(json!({"op": "rumor", "id": about, "kind": "wealth", "text": text, "heat": heat, "secret": false}));
    }

    fn observe(&mut self, id: &str, text: String, place: &str, importance: f32) {
        self.fx.observe(id, "company", &text, place, importance, "observation");
    }

    /// A step's changes to one heroine (only what that step changed).
    fn rel_step(&mut self, id: &str, f: impl FnOnce(&mut Relation)) {
        let Some(r) = self.rels.get_mut(id) else { return };
        let before = r.clone();
        f(r);
        let after = r.clone();
        self.fx.relation(&after, &before);
    }

    // ------------------------------------------------------------------------------------------ the engine

    fn tax_year(&mut self, book: &str) {
        let year = date(self.day, 12.0).year as i32;
        let e = self.estate_mut(book);
        if e.tax_year != year {
            e.tax_year = year;
            e.ytd_profit = 0;
        }
    }

    /// A holding is gone: who worked there moves to headquarters (or nowhere), and headquarters moves if it was it.
    fn rehome(&mut self, book: &str, unit: &str) {
        let day = self.day;
        if self.estate(book).hq().is_none() {
            let pick = self.estate(book).holdings.iter().position(|x| self.holding_capacity(x, day) > 0 && self.holding_use(x) != "land");
            if let Some(i) = pick {
                self.estate_mut(book).holdings[i].hq = true;
            }
        }
        let hq = self.estate(book).hq().map(|h| h.unit.clone()).unwrap_or_default();
        for w in self.estate_mut(book).workers.iter_mut().filter(|w| w.holding == unit) {
            w.holding = hq.clone();
        }
    }

    /// His place rented to her: when her lease ends, his holding is free again.
    fn free_landlord_place(&mut self, h: &Holding) {
        if h.landlord != "player" {
            return;
        }
        if let Some(mine) = self.company.estate.holdings.iter_mut().find(|x| x.unit == h.unit) {
            mine.tenant.clear();
        }
    }

    /// This week's applicants: more with a better reputation; each asks for pay by role and skill.
    fn applicants(&mut self, book: &str, seed: i32) {
        let week = self.day / 7;
        let p = self.p;
        let e = self.estate_mut(book);
        if e.applicant_week == week {
            return;
        }
        e.applicant_week = week;
        e.applicants.clear();
        e.refused.clear();
        if p.roles.is_empty() || p.family.is_empty() || p.given.is_empty() {
            return;
        }
        let mut rng = RandomStream::new(week.wrapping_mul(7919).wrapping_add(seed));
        let n = 3 + (e.reputation / 20.0) as i32;
        for _ in 0..n {
            let id = format!("w{}", e.next_worker);
            e.next_worker += 1;
            let (family, given) = (&p.family[rng.rand_range(0, p.family.len() as i32 - 1) as usize], &p.given[rng.rand_range(0, p.given.len() as i32 - 1) as usize]);
            let name = crate::tr::m("estate.person_name", &[("family", crate::tr::a(family)), ("given", crate::tr::a(given))]); // name order by language
            let role = &p.roles[rng.rand_range(0, p.roles.len() as i32 - 1) as usize];
            let mut w = Worker { id, name, role: role.id.clone(), week, ..Default::default() };
            w.skill = rng.rand_range(20, 90);
            w.diligence = rng.rand_range(20, 90);
            w.loyalty = rng.rand_range(20, 90);
            w.ask = (role.base_pay as f32 * (0.8 + w.skill as f32 / 100.0)) as i64 / crate::region::amount("salary", 50).max(1) * crate::region::amount("salary", 50).max(1);
            e.applicants.push(w);
        }
    }

    /// The first of the month for one company: revenue, VAT, leases ending, rent (to the market or to him as
    /// landlord), mortgage, property tax, wages (heroines on his staff first), the owner's pay, overheads, corporate tax,
    /// and repaying the owner. Missed: two months of rent lose the lease, three of the mortgage lose the property, two of
    /// wages lose the person. `who`: how rumours name the owner.
    fn estate_month(&mut self, book: &str, who: &str, with_staff: bool, extra_work: f64) -> Month {
        let mut out = Month::default();
        let day = self.day;
        let month = month_of(day);
        let p = self.p;
        let staff: Vec<Employee> = if with_staff { self.company.staff.clone() } else { Vec::new() };
        {
            let e = self.estate(book).clone();
            for h in &e.holdings {
                out.revenue += self.revenue(&e, h, day, if with_staff { Some(&staff) } else { None }, extra_work);
            }
        }
        self.receive(book, out.revenue, "revenue", crate::tr::t("estate.last_month_revenue"));
        self.tax_year(book);
        // VAT (small-scale taxpayer) on what came in.
        let vat = (out.revenue as f32 * p.vat_small) as i64;
        if self.pay(book, vat, "vat", crate::tr::t("estate.value_added_tax")) {
            out.paid += vat;
        } else {
            out.missed = true;
        }
        // Leases given notice end now: deposit back less any rent owed.
        let mut k = self.estate(book).holdings.len();
        while k > 0 {
            k -= 1;
            let h = self.estate(book).holdings[k].clone();
            if h.mode != "rent" || h.leave_month < 0 || month < h.leave_month {
                continue;
            }
            let name = self.unit_of(&h.unit).name;
            let back = (h.deposit - h.missed_rent as i64 * h.rent).max(0);
            self.receive(book, back, "deposit", &crate::tr::m("estate.moveout_deposit_refund", &[("name", crate::tr::a(&name))]));
            if h.landlord == "player" {
                self.pay("company", back, "deposit", &crate::tr::m("estate.tenant_moved_out_refund", &[("name", crate::tr::a(&name))]));
            }
            self.free_landlord_place(&h);
            self.estate_mut(book).holdings.remove(k);
            self.rehome(book, &h.unit);
        }
        let mut k = self.estate(book).holdings.len();
        while k > 0 {
            k -= 1;
            let h = self.estate(book).holdings[k].clone();
            let name = self.unit_of(&h.unit).name;
            if h.mode == "rent" {
                if self.pay(book, h.rent, "rent", &crate::tr::m("estate.rent_income", &[("name", crate::tr::a(&name))])) {
                    self.estate_mut(book).holdings[k].missed_rent = 0;
                    out.paid += h.rent;
                    if h.landlord == "player" {
                        self.receive("company", h.rent, "rent_income", &crate::tr::m("estate.tenant_rent_income", &[("name", crate::tr::a(&name))]));
                    }
                } else {
                    let missed = h.missed_rent + 1;
                    self.estate_mut(book).holdings[k].missed_rent = missed;
                    if missed >= 2 {
                        // Evicted: the deposit covers what it can; the building on a rented lot stays with the landlord.
                        self.receive(book, (h.deposit - 2 * h.rent).max(0), "deposit", &crate::tr::m("estate.reclaimed_deposit_leftover", &[("name", crate::tr::a(&name))]));
                        if h.landlord == "player" {
                            self.receive("company", h.deposit.min(2 * h.rent), "rent_income", &crate::tr::m("estate.deposit_offsets_rent_arrears", &[("name", crate::tr::a(&name))]));
                        }
                        self.free_landlord_place(&h);
                        self.estate_mut(book).holdings.remove(k);
                        self.rehome(book, &h.unit);
                        let e = self.estate_mut(book);
                        e.reputation = (e.reputation - 10.0).max(0.0);
                        out.missed = true;
                        self.rumor("", crate::tr::m("estate.gossip_rent_unpaid_reclaim", &[("who", crate::tr::a(&who)), ("name", crate::tr::a(&name))]), 65.0);
                        continue;
                    }
                    out.missed = true;
                }
            }
            if h.loan_payment > 0 {
                // The interest part of the payment is a cost; the rest repays what is owed.
                let h = self.estate(book).holdings[k].clone();
                let interest = h.loan_payment.min(((h.loan as f32 * p.mortgage_rate) as f64 / 12.0) as i64);
                let principal = h.loan.min(h.loan_payment - interest);
                if self.funds(book) >= interest + principal {
                    self.pay(book, interest, "loan_interest", &crate::tr::m("estate.mortgage_interest", &[("name", crate::tr::a(&name))]));
                    self.pay(book, principal, "loan_principal", &crate::tr::m("estate.mortgage_principal", &[("name", crate::tr::a(&name))]));
                    out.paid += interest;
                    let x = &mut self.estate_mut(book).holdings[k];
                    x.loan -= principal;
                    x.missed_loan = 0;
                    if x.loan == 0 {
                        x.loan_payment = 0;
                    }
                } else {
                    let missed = h.missed_loan + 1;
                    self.estate_mut(book).holdings[k].missed_loan = missed;
                    if missed >= 3 {
                        // Foreclosed: sold at the auction ratio, the loan repaid first, the rest to the owner.
                        let value = (self.holding_value(&h, day) as f32 * p.foreclosure_ratio) as i64;
                        self.receive(book, (value - h.loan).max(0), "foreclosure", &crate::tr::m("estate.foreclosure_leftover", &[("name", crate::tr::a(&name))]));
                        self.estate_mut(book).holdings.remove(k);
                        self.rehome(book, &h.unit);
                        let e = self.estate_mut(book);
                        e.reputation = (e.reputation - 15.0).max(0.0);
                        out.missed = true;
                        self.rumor("", crate::tr::m("estate.gossip_foreclosure", &[("who", crate::tr::a(&who)), ("name", crate::tr::a(&name))]), 70.0);
                        continue;
                    }
                    out.missed = true;
                }
            }
            let h = self.estate(book).holdings[k].clone();
            if h.mode == "own" {
                let tax = (self.holding_value(&h, day) as f32 * p.property_tax / 12.0) as i64;
                if self.pay(book, tax, "tax", &crate::tr::m("estate.property_holding_tax", &[("name", crate::tr::a(&name))])) {
                    out.paid += tax;
                } else {
                    out.missed = true;
                }
            }
        }
        // Wages: heroines on his staff first (their own rules), then the staff.
        if with_staff {
            for i in 0..self.company.staff.len() {
                let st = self.company.staff[i].clone();
                if !self.rels.contains_key(&st.heroine) {
                    continue;
                }
                let employer = p.social(st.pay, true, 1.0).total();
                let who_name = self.name_of(&st.heroine);
                if self.pay(book, st.pay + employer, "wage", &crate::tr::m("estate.employee_salary_line", &[("name", crate::tr::a(&who_name)), ("amount", crate::tr::a(&crate::tr::money(employer)))])) {
                    let net = p.net_pay(st.pay, 1.0).0;
                    self.rel_step(&st.heroine, |r| r.cash = r.cash.max(0) + net);
                    self.company.staff[i].missed = 0;
                    out.paid += st.pay + employer;
                } else {
                    self.company.staff[i].missed += 1;
                    out.wages_missed = true;
                    self.estate_mut(book).debt += st.pay as i32;
                    self.rel_step(&st.heroine, |r| r.trust = (r.trust - 10.0).max(0.0));
                    self.observe(&st.heroine, crate::tr::t("estate.no_salary_this_month").into(), "", 0.8);
                }
            }
            let gone: Vec<String> = self.company.staff.iter().filter(|s| s.missed >= 2).map(|s| s.heroine.clone()).collect();
            for g in &gone {
                self.observe(g, crate::tr::t("estate.two_months_unpaid_quit").into(), "", 0.9);
            }
            self.company.staff.retain(|s| s.missed < 2);
        }
        for i in 0..self.estate(book).workers.len() {
            let w = self.estate(book).workers[i].clone();
            let employer = p.social(w.pay, true, 1.0).total();
            if self.pay(book, w.pay + employer, "wage", &crate::tr::m("estate.employee_salary_line", &[("name", crate::tr::a(&w.name)), ("amount", crate::tr::a(&crate::tr::money(employer)))])) {
                self.estate_mut(book).workers[i].missed = 0;
                out.paid += w.pay + employer;
            } else {
                let e = self.estate_mut(book);
                e.workers[i].missed += 1;
                e.workers[i].morale = (e.workers[i].morale - 25.0).max(0.0);
                e.debt += w.pay as i32;
                out.wages_missed = true;
            }
        }
        {
            let e = self.estate_mut(book);
            let before = e.workers.len();
            e.workers.retain(|w| w.missed < 2);
            out.quit = (before - e.workers.len()) as i32;
        }
        // His own monthly pay as the boss (a wage like any other: the company pays its share, his payslip withholds).
        let owner_pay = self.estate(book).owner_pay;
        if owner_pay > 0 && !book.starts_with(HERO_JOB) {
            let employer = p.social(owner_pay, true, 1.0).total();
            if self.pay(book, owner_pay + employer, "owner_pay", &crate::tr::m("estate.owner_salary_breakdown", &[("owner_pay_amount", crate::tr::a(&crate::tr::money(owner_pay))), ("employer_amount", crate::tr::a(&crate::tr::money(employer)))])) {
                out.paid += owner_pay + employer;
                self.bank_op(owner_pay, "owner_pay", crate::tr::t("estate.owner_salary_paid_by_company"));
                self.payslip = self.player_payroll(owner_pay, 1.0, "bank");
            } else {
                out.wages_missed = true;
            }
        }
        let overhead = crate::region::amount("cash", 200) * (self.estate(book).workers.len() as i64 + if with_staff { self.company.staff.len() as i64 } else { 0 });
        if self.pay(book, overhead, "overhead", crate::tr::t("estate.miscellaneous_expenses")) {
            out.paid += overhead;
        }
        // Corporate income tax on the month's profit, after losses carried forward; the small low-profit rate within the
        // year's limit.
        {
            let profit = out.revenue - out.paid;
            let e = self.estate_mut(book);
            if profit < 0 {
                e.loss_carry += -profit;
            } else {
                let taxable = (profit - e.loss_carry).max(0);
                e.loss_carry = (e.loss_carry - profit).max(0);
                let rate = if (e.ytd_profit + taxable) as f64 <= p.cit_small_limit as f64 { p.cit_small } else { p.cit_rate };
                let cit = (taxable as f32 * rate) as i64;
                e.ytd_profit += taxable;
                if cit > 0 {
                    if self.pay(book, cit, "cit", &crate::tr::m("estate.corporate_income_tax", &[("rate_percent", crate::tr::a(&(rate * 100.0)))])) {
                        out.paid += cit;
                    } else {
                        out.missed = true;
                    }
                }
            }
        }
        // A month of costs kept, the rest first repays what the owner lent.
        let e = self.estate(book);
        if e.owner_loan > 0 {
            let most = e.cash - out.paid;
            self.repay_owner(book, most);
        }
        let e = self.estate_mut(book);
        e.reputation = (e.reputation + if out.wages_missed || out.missed { -8.0 } else { 2.0 }).clamp(0.0, 100.0);
        if out.quit > 0 {
            self.rumor("", crate::tr::m("estate.gossip_company_quit", &[("who", crate::tr::a(&who)), ("count", crate::tr::a(&out.quit))]), 55.0);
        }
        out
    }

    /// Bankruptcy: leases end, people leave; owned property is auctioned only as far as the debts need (wages owed and
    /// what money there is going first), the rest stays the owner's; the owner's loan repaid as far as the money goes.
    fn dissolve(&mut self, book: &str) -> usize {
        let p = self.p;
        let day = self.day;
        let mut owed = self.estate(book).debt as i64;
        let cash = owed.min(self.funds(book));
        self.pay(book, cash, "wage", crate::tr::t("estate.closure_settles_unpaid_wages"));
        owed -= cash;
        let leased: Vec<Holding> = self.estate(book).holdings.iter().filter(|h| h.mode != "own").cloned().collect();
        for h in &leased {
            self.free_landlord_place(h);
        }
        self.estate_mut(book).holdings.retain(|h| h.mode == "own");
        let mut owned = self.estate(book).holdings.clone();
        owned.sort_by_key(|h| self.holding_value(h, day));
        self.estate_mut(book).holdings = owned;
        let mut k = 0;
        while k < self.estate(book).holdings.len() && owed > 0 {
            let h = self.estate(book).holdings[k].clone();
            let value = (self.holding_value(&h, day) as f32 * p.foreclosure_ratio) as i64 - h.loan;
            if value <= 0 {
                k += 1;
                continue;
            }
            let left = value - owed;
            owed = (owed - value).max(0);
            if left > 0 {
                let name = self.unit_of(&h.unit).name;
                self.receive(book, left, "auction", &crate::tr::m("estate.auction_leftover_after_debt", &[("name", crate::tr::a(&name))]));
            }
            self.estate_mut(book).holdings.remove(k);
        }
        {
            let e = self.estate_mut(book);
            for h in e.holdings.iter_mut() {
                h.hq = false;
            }
            e.debt = 0;
            e.workers.clear();
            e.applicants.clear();
        }
        let loan = self.estate(book).owner_loan;
        self.repay_owner(book, loan);
        let cash = self.estate(book).cash;
        if cash > 0 {
            self.to_owner(book, cash, "liquidation", crate::tr::t("estate.liquidation_refund_to_owner"));
        }
        // What the company cannot repay is the owner's loss (Unreal's wallet writes it off).
        if self.estate(book).owner_loan > 0 {
            self.fx.0.push(json!({"op": "wallet", "book": book, "act": "write_off", "amount": self.estate(book).owner_loan, "kind": "liquidation", "note": crate::tr::t("estate.unpaid_shareholder_loan_liquidation")}));
            self.estate_mut(book).owner_loan = 0;
        }
        // The books close: no capital, no profit kept (Unreal's wallet does the same to its columns).
        self.fx.0.push(json!({"op": "wallet", "book": book, "act": "close_books", "amount": 0, "kind": "", "note": ""}));
        let e = self.estate_mut(book);
        e.capital = 0;
        e.retained = 0;
        e.holdings.len()
    }

    /// Every new day for one company: buildings finished (their last payment), morale, people who leave, this week's
    /// applicants. Present: units the owner was at today.
    fn estate_daily(&mut self, book: &str, who: &str, present: &BTreeSet<String>, at_hq: i32, seed: i32) {
        let p = self.p;
        let day = self.day;
        for k in 0..self.estate(book).holdings.len() {
            let h = self.estate(book).holdings[k].clone();
            let Some(b) = p.building(&h.building) else { continue };
            if h.build_start < 0 || day < h.build_done || h.build_paid >= b.cost {
                continue;
            }
            if self.pay(book, b.cost - h.build_paid, "build", &crate::tr::m("estate.final_payment_property", &[("name", crate::tr::a(&b.name))])) {
                self.estate_mut(book).holdings[k].build_paid = b.cost;
                let name = self.unit_of(&h.unit).name;
                self.rumor("", crate::tr::m("estate.gossip_built_building", &[("who", crate::tr::a(&who)), ("place", crate::tr::a(&name)), ("building", crate::tr::a(&b.name))]), 70.0);
                if self.estate(book).hq().is_none() && b.kind == "office" {
                    self.estate_mut(book).holdings[k].hq = true;
                }
            } else {
                // On hold until it can be paid.
                self.estate_mut(book).holdings[k].build_done = day + 1;
            }
        }
        // Morale: crowded, underpaid, the boss never around; loyalty holds people when it falls.
        let e = self.estate(book).clone();
        let mut morale: Vec<f32> = Vec::new();
        for w in &e.workers {
            let market = p.role(&w.role).map(|r| r.base_pay).unwrap_or(crate::region::amount("salary", 2400)) as f32 * (0.8 + w.skill as f32 / 100.0);
            let mut m = w.morale + ((w.pay as f32 / market - 1.0) * 4.0).clamp(-2.0, 2.0);
            let h = e.holdings.iter().find(|x| x.unit == w.holding);
            if let Some(h) = h {
                if self.headcount(&e, &w.holding, at_hq) as f32 > self.holding_capacity(h, day) as f32 * 0.8 {
                    m -= 1.0;
                }
                if present.contains(&h.unit) {
                    m += 1.0;
                }
            } else {
                m -= 3.0;
            }
            morale.push((m + (70.0 - m) * 0.03).clamp(0.0, 100.0));
        }
        let mut rng = RandomStream::new(day.wrapping_mul(613).wrapping_add(seed));
        let est = self.estate_mut(book);
        for (w, m) in est.workers.iter_mut().zip(morale) {
            w.morale = m;
        }
        let before = est.workers.len();
        est.workers.retain(|w| !(w.morale < 30.0 && rng.frand() < 0.15 * (1.0 - w.loyalty as f32 / 120.0)));
        let left = before - est.workers.len();
        if left > 0 {
            est.reputation = (est.reputation - 2.0 * left as f32).max(0.0);
        }
        self.applicants(book, seed);
    }

    // ------------------------------------------------------------------------------------------ his payslip

    fn personal_year(&mut self) {
        let year = date(self.day, 12.0).year as i32;
        let p = &mut self.personal;
        if p.tax_year != year {
            p.tax_year = year;
            p.ytd_income = 0;
            p.ytd_social = 0;
            p.ytd_tax = 0;
            p.ytd_bonus = 0;
        }
    }

    /// Income tax due now on his pay so far this year (the cumulative withholding method; the one-time bonus apart).
    fn cumulative_due(&self) -> i64 {
        let month = date(self.day, 12.0).month as f64;
        let p = &self.personal;
        (self.p.iit_yearly((p.ytd_income - p.ytd_social) as f64 - self.p.iit_threshold as f64 * month) - (p.ytd_tax - p.ytd_bonus_tax)).max(0)
    }

    /// Pay he earned as an employee, already in `account` ("cash" or "bank"): his 五險一金 share for the time it covers
    /// and the income tax withheld come out; the housing fund (both shares) goes to his 公積金. Returns the payslip line.
    /// Pay for labour service (a student's part-time job on the mainland): the income tax on labour remuneration only,
    /// per payment (labour_service_tax). No social insurance.
    pub fn player_labour_service(&mut self, gross: i64, account: &str) -> String {
        if gross <= 0 {
            return String::new();
        }
        self.personal_year();
        let tax = labour_service_tax(gross);
        self.personal.ytd_income += gross;
        self.personal.ytd_tax += tax;
        if tax > 0 {
            if account == "bank" {
                self.bank_op(-tax, "iit", crate::tr::t("estate.personal_income_tax_withheld"));
            } else {
                self.cash_op(-tax, "iit", crate::tr::t("estate.personal_income_tax_withheld"));
            }
        }
        crate::tr::m("estate.labour_service_breakdown", &[("income_tax_amount", crate::tr::a(&crate::tr::money(tax))), ("net_pay_amount", crate::tr::a(&crate::tr::money(gross - tax)))])
    }

    pub fn player_payroll(&mut self, gross: i64, months: f32, account: &str) -> String {
        if gross <= 0 {
            return String::new();
        }
        self.personal_year();
        let mine = self.p.social(gross, false, months);
        let employer = self.p.social(gross, true, months);
        self.personal.ytd_income += gross;
        self.personal.ytd_social += mine.total();
        let due = self.cumulative_due();
        self.personal.ytd_tax += due;
        self.personal.housing_fund += mine.housing + employer.housing;
        self.personal.pension_paid += mine.pension + employer.pension;
        let note = crate::tr::m("estate.social_insurance_personal_split", &[("pension_amount", crate::tr::a(&crate::tr::money(mine.pension))), ("medical_amount", crate::tr::a(&crate::tr::money(mine.medical))), ("unemployment_amount", crate::tr::a(&crate::tr::money(mine.unemployment))), ("housing_fund_amount", crate::tr::a(&crate::tr::money(mine.housing)))]);
        if account == "bank" {
            self.bank_op(-mine.total(), "social", &note);
            self.bank_op(-due, "iit", crate::tr::t("estate.personal_income_tax_withheld"));
        } else {
            self.cash_op(-mine.total(), "social", &note);
            self.cash_op(-due, "iit", crate::tr::t("estate.personal_income_tax_withheld"));
        }
        crate::tr::m("estate.salary_deduction_breakdown", &[("social_insurance_amount", crate::tr::a(&crate::tr::money(mine.total()))), ("income_tax_amount", crate::tr::a(&crate::tr::money(due))), ("net_pay_amount", crate::tr::a(&crate::tr::money(gross - mine.total() - due))), ("housing_fund_amount", crate::tr::a(&crate::tr::money(mine.housing + employer.housing)))])
    }

    // ------------------------------------------------------------------------------------------ his month and day

    fn ensure_company(&mut self) {
        let co = &mut self.company;
        if co.founded < 0 || !co.estate.holdings.is_empty() {
            return;
        }
        let office = if co.office.is_empty() { "cafe".to_string() } else { co.office.clone() };
        co.estate.holdings.push(Holding { unit: format!("{BORROW}{office}"), mode: "borrow".into(), since: co.founded, hq: true, use_: "office".into(), ..Default::default() });
    }

    /// His company's month: the books, bankruptcy when more is owed than he has, and the heroines' idea of his wealth.
    fn company_month(&mut self) {
        self.ensure_company();
        let m = self.estate_month("company", crate::tr::t("estate.pronoun_he"), true, 0.0);
        self.company.last_profit = m.revenue - m.paid;
        self.company.paid_month = month_of(self.day);
        let broke = self.company.estate.debt as i64 > self.funds("company") && (m.quit > 0 || self.company.staff.iter().any(|s| s.missed >= 1));
        if broke {
            let name = self.company.name.clone();
            for st in self.company.staff.clone() {
                self.observe(&st.heroine, crate::tr::m("estate.his_company_closed", &[("name", crate::tr::a(&name))]), "", 0.9);
            }
            self.rumor("", crate::tr::m("estate.gossip_his_company_closed", &[("name", crate::tr::a(&name))]), 80.0);
            self.dissolve("company");
            self.company.staff.clear();
            self.company.founded = -1;
            self.company.paid_month = -1;
        }
        // The heroines' idea of his wealth drifts toward what the company looks like.
        if self.company.founded >= 0 {
            let e = &self.company.estate;
            let mut assets = self.bank + self.cash + e.cash - e.owner_loan;
            for h in &e.holdings {
                assets += self.holding_value(h, self.day) - h.loan;
            }
            let look = (25.0 + e.holdings.len() as f32 * 6.0 + (e.workers.len() + self.company.staff.len()) as f32 * 2.0 + ((assets.max(1) as f64).log10() as f32) * 5.0).clamp(0.0, 100.0);
            for id in self.rels.keys().cloned().collect::<Vec<_>>() {
                self.rel_step(&id, |r| r.perceived_wealth += (look - r.perceived_wealth) * 0.15);
            }
        }
        self.fx.log(format!("Street company: month {} revenue {} paid {} profit {} holdings {} workers {} reputation {:.0}{}", self.company.paid_month, m.revenue, m.paid, self.company.last_profit, self.company.estate.holdings.len(), self.company.estate.workers.len(), self.company.estate.reputation, if broke { " DISSOLVED" } else { "" }));
    }

    // ------------------------------------------------------------------------------------------ her company

    /// Where she sets up: his places offered for rent, office units, or a lot to rent or buy and build on; the busiest
    /// she can afford (half a year of costs kept back).
    fn hero_take_place(&mut self, owner: &str, kind: &str) -> bool {
        let p = self.p;
        let day = self.day;
        let book = format!("{HERO_JOB}{owner}");
        let funds = self.funds(&book);
        struct Opt {
            unit: String,
            mode: String,
            build: String,
            cost: i64,
            monthly: i64,
            score: f32,
            from_player: bool,
        }
        let mut options: Vec<Opt> = Vec::new();
        let b = p.buildings.iter().find(|x| x.kind == kind);
        for mine in &self.company.estate.holdings {
            if mine.lease_rent <= 0 || !mine.tenant.is_empty() || self.holding_capacity(mine, day) <= 0 {
                continue;
            }
            options.push(Opt {
                unit: mine.unit.clone(),
                mode: "rent".into(),
                build: String::new(),
                cost: mine.lease_rent * 3,
                monthly: mine.lease_rent,
                score: self.unit_of(&mine.unit).traffic + 0.25 + if self.holding_use(mine) == kind { 0.3 } else { 0.0 },
                from_player: true,
            });
        }
        for u in &p.units {
            if !self.unit_user(&u.id).is_empty() {
                continue;
            }
            if !u.lot {
                if kind == "office" && u.rent > 0 {
                    options.push(Opt { unit: u.id.clone(), mode: "rent".into(), build: String::new(), cost: u.rent * 3, monthly: u.rent, score: u.traffic, from_player: false });
                }
                continue;
            }
            let Some(b) = b else { continue };
            if u.rent > 0 {
                options.push(Opt { unit: u.id.clone(), mode: "rent".into(), build: b.id.clone(), cost: u.rent * 3 + b.cost, monthly: u.rent, score: u.traffic, from_player: false });
            }
            if u.price > 0 {
                let price = p.land_price(u, day);
                options.push(Opt {
                    unit: u.id.clone(),
                    mode: "own".into(),
                    build: b.id.clone(),
                    cost: price + (price as f32 * p.closing) as i64 + b.cost,
                    monthly: (price as f32 * p.property_tax / 12.0) as i64,
                    score: u.traffic + 0.15,
                    from_player: false,
                });
            }
        }
        let mut best: Option<&Opt> = None;
        for o in &options {
            if o.cost + o.monthly * 6 <= funds && best.map(|b| o.score > b.score).unwrap_or(true) {
                best = Some(o);
            }
        }
        let Some(best) = best else { return false };
        let (unit, mode, build, from_player, monthly) = (best.unit.clone(), best.mode.clone(), best.build.clone(), best.from_player, best.monthly);
        let u = self.unit_of(&unit);
        let mut hold = Holding { unit: unit.clone(), mode: mode.clone(), since: day, use_: kind.into(), ..Default::default() };
        if from_player {
            // His place: she pays him the first month and two months' deposit; it keeps his building.
            let Some(mine) = self.company.estate.holdings.iter().find(|x| x.unit == unit).cloned() else { return false };
            if !self.pay(&book, mine.lease_rent * 3, "rent", &crate::tr::m("estate.deposit_first_rent_paid_out", &[("name", crate::tr::a(&u.name))])) {
                return false;
            }
            self.receive("company", mine.lease_rent, "rent_income", &crate::tr::m("estate.tenant_first_month_rent", &[("name", crate::tr::a(&u.name))]));
            self.receive("company", mine.lease_rent * 2, "deposit", &crate::tr::m("estate.tenant_deposit_entry", &[("name", crate::tr::a(&u.name))]));
            hold.landlord = "player".into();
            hold.rent = mine.lease_rent;
            hold.deposit = mine.lease_rent * 2;
            hold.building = mine.building.clone();
            hold.build_start = mine.build_start;
            hold.build_done = mine.build_done;
            hold.build_paid = mine.build_paid;
            hold.use_ = self.holding_use(&mine);
            if let Some(m) = self.company.estate.holdings.iter_mut().find(|x| x.unit == unit) {
                m.tenant = owner.into();
            }
        } else if mode == "rent" {
            if !self.pay(&book, monthly * 3, "rent", &crate::tr::m("estate.deposit_and_first_rent", &[("name", crate::tr::a(&u.name))])) {
                return false;
            }
            hold.rent = monthly;
            hold.deposit = monthly * 2;
        } else {
            let price = p.land_price(&u, day);
            if !self.pay(&book, price + (price as f32 * p.closing) as i64, "buy", &crate::tr::m("estate.land_purchase", &[("name", crate::tr::a(&u.name))])) {
                return false;
            }
            hold.bought = price;
        }
        if !build.is_empty() && hold.build_start < 0 {
            if let Some(bd) = p.building(&build) {
                if self.pay(&book, bd.cost * 3 / 10, "build", &crate::tr::m("estate.construction_start_payment", &[("name", crate::tr::a(&bd.name))])) {
                    hold.building = bd.id.clone();
                    hold.build_start = day;
                    hold.build_done = day + bd.days;
                    hold.build_paid = bd.cost * 3 / 10;
                }
            }
        }
        let e = self.estate_mut(&book);
        hold.hq = e.hq().is_none();
        e.holdings.push(hold);
        self.fx.log(format!("Street hero company: {owner} takes {} ({mode}{})", u.name, if from_player { ", his place" } else { "" }));
        true
    }

    /// She closes her company: leases end (deposits back, his place free), her people let go with what she can pay,
    /// what she owns stays hers; he loses the job if he had one.
    fn hero_close(&mut self, owner: &str, why: &str) {
        let Some(k) = self.heroes.iter().position(|h| h.owner == owner) else { return };
        let book = format!("{HERO_JOB}{owner}");
        let name = self.heroes[k].name.clone();
        for h in self.heroes[k].estate.holdings.clone().iter().filter(|h| h.mode == "rent") {
            let back = (h.deposit - h.missed_rent as i64 * h.rent).max(0);
            let unit = self.unit_of(&h.unit).name;
            self.receive(&book, back, "deposit", &crate::tr::m("estate.deposit_refund_entry", &[("unit", crate::tr::a(&unit))]));
            if h.landlord == "player" {
                self.pay("company", back, "deposit", &crate::tr::m("estate.tenant_closed_refund_deposit", &[("unit", crate::tr::a(&unit))]));
            }
        }
        for w in self.estate(&book).workers.clone() {
            let amount = w.pay.min(self.funds(&book));
            self.pay(&book, amount, "severance", &crate::tr::m("estate.severance_pay_entry", &[("name", crate::tr::a(&w.name))]));
        }
        self.dissolve(&book);
        self.player_jobs.retain(|j| *j != book);
        self.fx.0.push(json!({"op": "hero_close", "owner": owner}));
        self.heroes.remove(k);
        let her = self.name_of(owner);
        self.observe(owner, crate::tr::m("estate.closed_company_reason", &[("name", crate::tr::a(&name)), ("reason", crate::tr::a(&why))]), "", 0.9);
        self.rumor(owner, crate::tr::m("estate.gossip_company_closed", &[("who", crate::tr::a(&her)), ("company_name", crate::tr::a(&name))]), 65.0);
        self.fx.log(format!("Street hero company: {name} closes ({why})"));
    }

    /// Every day for every heroine: found, run, offer him a job, grow, close.
    fn heroes_daily(&mut self, heroines: &[(String, i32, i32)]) {
        let p = self.p;
        let day = self.day;
        let d = date(day, 12.0);
        let month = month_of(day);
        for (id, actor, seed) in heroines {
            let (id, actor, seed) = (id.clone(), *actor, *seed);
            let name = self.name_of(&id);
            let Some(r) = self.rels.get(&id).cloned() else { continue };
            let book = format!("{HERO_JOB}{id}");
            if !self.heroes.iter().any(|h| h.owner == id) {
                // 1. Opening: rich enough, able enough, not in debt or on his staff; a few percent a day.
                let wealth = r.bank + r.cash.max(0);
                let comp = self.profile_int(&id, "occupation", "competence", 50);
                if wealth < crate::region::amount("cash", 150_000) || comp < 50 || r.debt > 0 || self.company.staff.iter().any(|e| e.heroine == id) {
                    continue;
                }
                let mut rng = RandomStream::new(day.wrapping_mul(977).wrapping_add(actor.wrapping_mul(131)));
                if rng.frand() >= (0.06 + (wealth - crate::region::amount("cash", 150_000)) as f32 / crate::region::amount("cash", 100_000) as f32 * 0.02).min(0.15) {
                    continue;
                }
                let fav: Vec<String> = self.profiles.get(&id).and_then(|p| p.get("favorite_places")).and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_str).map(String::from).collect()).unwrap_or_default();
                let kind = if fav.iter().any(|f| f == "cafe" || f == "snack" || f == "drinkshop") {
                    "cafe"
                } else if comp >= 70 {
                    "office"
                } else {
                    "shop"
                };
                let company = HeroCompany { owner: id.clone(), kind: kind.into(), name: crate::tr::m("estate.owner_of_kind", &[("name", crate::tr::a(&name)), ("kind", crate::tr::a(&(kind_name(kind))))]), founded: day, paid_month: month, ..Default::default() };
                // A trial: if no place can be had, nothing happened.
                let saved = (self.heroes.clone(), self.rels.clone(), self.company.clone(), self.fx.0.len(), self.cash, self.bank);
                self.fx.0.push(json!({"op": "hero_open", "company": company}));
                self.heroes.push(company);
                // Half of what she has goes in as the company's capital; the rest stays hers (and may be lent to it).
                self.from_owner(&book, wealth / 2, "capital", crate::tr::t("estate.owner_injects_capital"));
                let cap = self.estate(&book).capital;
                if let Some(h) = self.heroes.iter_mut().find(|h| h.owner == id) {
                    h.capital = cap;
                }
                if !self.hero_take_place(&id, kind) {
                    self.heroes = saved.0;
                    self.rels = saved.1;
                    self.company = saved.2;
                    self.fx.0.truncate(saved.3);
                    self.cash = saved.4;
                    self.bank = saved.5;
                    continue;
                }
                let hq = self.estate(&book).hq().cloned().unwrap();
                let where_ = self.unit_of(&hq.unit).name;
                let place = self.holding_place(&hq);
                let company_name = self.heroes.iter().find(|h| h.owner == id).unwrap().name.clone();
                self.observe(&id, crate::tr::m("estate.opened_own_business_named", &[("place", crate::tr::a(&where_)), ("kind", crate::tr::a(&(kind_name(kind)))), ("company_name", crate::tr::a(&company_name))]), &place, 0.9);
                self.rumor(&id, crate::tr::m("estate.gossip_opened_business", &[("name", crate::tr::a(&name)), ("place", crate::tr::a(&where_)), ("kind", crate::tr::a(&(kind_name(kind))))]), 70.0);
                if r.stage != "stranger" {
                    let delay = self.rng.frand_range(30.0, 120.0);
                    self.fx.text(&id, &crate::tr::m("estate.opened_business_invite_him", &[("place", crate::tr::a(&where_)), ("kind", crate::tr::a(&(kind_name(kind))))]), "company_open", delay);
                }
                self.fx.log(format!("Street hero company: {name} opens {company_name} at {where_} (wealth {wealth})"));
                continue;
            }
            // 2. The day: buildings, morale (she is there), applicants; she hires up to half the room while she can pay.
            let present: BTreeSet<String> = self.estate(&book).hq().map(|h| h.unit.clone()).into_iter().collect();
            self.estate_daily(&book, &name, &present, 1, seed);
            for hold in self.estate(&book).holdings.clone() {
                let room = self.holding_capacity(&hold, day);
                let e = self.estate(&book).clone();
                if room <= 0 || self.headcount(&e, &hold.unit, if hold.hq { 1 } else { 0 }) >= (room / 2).max(1) {
                    continue;
                }
                let mut monthly = 0;
                for x in &e.holdings {
                    monthly += self.holding_costs(x, day);
                }
                for w in &e.workers {
                    monthly += w.pay;
                }
                let funds = self.funds(&book);
                let Some(k) = e.applicants.iter().position(|a| !e.refused.contains(&a.id) && funds > (monthly + a.ask) * 4) else { break };
                let est = self.estate_mut(&book);
                let mut w = est.applicants.remove(k);
                w.pay = w.ask;
                w.holding = hold.unit.clone();
                w.since = day;
                w.morale = 65.0;
                let hired = w.name.clone();
                est.workers.push(w);
                self.fx.log(format!("Street hero company: {book} hires {hired}"));
                break;
            }
            // 3. The month: the same books as his; three losing months and she closes, three good ones and she may grow.
            let co = self.heroes.iter().find(|h| h.owner == id).unwrap().clone();
            if d.day == 1 && co.paid_month != month {
                let him = co.player_minutes as f64 / (22.0 * 8.0 * 60.0);
                let m = self.estate_month(&book, &name, false, him);
                let profit = m.revenue - m.paid;
                {
                    let h = self.heroes.iter_mut().find(|h| h.owner == id).unwrap();
                    h.last_profit = profit;
                    h.paid_month = month;
                    h.player_minutes = 0;
                    if profit >= 0 {
                        h.profit_streak += 1;
                        h.loss_streak = 0;
                    } else {
                        h.loss_streak += 1;
                        h.profit_streak = 0;
                    }
                }
                self.fx.log(format!("Street hero company: {} month {month} revenue {} paid {} profit {profit}", co.name, m.revenue, m.paid));
                let broke = self.estate(&book).debt as i64 > self.funds(&book) && m.quit > 0;
                let h = self.heroes.iter().find(|h| h.owner == id).unwrap().clone();
                if h.loss_streak >= 3 || broke || h.estate.hq().is_none() {
                    self.hero_close(&id, if broke { crate::tr::t("estate.unable_to_pay_salary") } else if h.loss_streak >= 3 { crate::tr::t("estate.three_months_losses") } else { crate::tr::t("estate.no_place_to_operate") });
                    continue;
                }
                if h.profit_streak >= 3 && self.funds(&book) > crate::region::amount("cash", 400_000) && h.estate.holdings.len() < 3 && self.hero_take_place(&id, &h.kind) {
                    self.heroes.iter_mut().find(|x| x.owner == id).unwrap().profit_streak = 0;
                    self.observe(&id, crate::tr::t("estate.second_branch_opened").into(), "", 0.7);
                    self.rumor(&id, crate::tr::m("estate.gossip_branch_opened", &[("name", crate::tr::a(&name))]), 60.0);
                }
            }
            // 4. Mondays: she may ask him to work for her (a friend or more who likes or trusts him, not already there).
            let r = self.rels.get(&id).cloned().unwrap();
            let co = self.heroes.iter().find(|h| h.owner == id).unwrap().clone();
            if day % 7 == 0
                && co.offer_week != day / 7
                && r.stage != "stranger"
                && (r.liking() >= 40.0 || r.trust >= 45.0)
                && !self.player_jobs.contains(&book)
                && RandomStream::new(day.wrapping_mul(31).wrapping_add(actor)).frand() < 0.25
            {
                let wage = (crate::region::amount("salary", 2600) as f64 / 176.0 * 1.5 * (1.0 + r.liking() as f64 / 200.0)) as i64;
                {
                    let h = self.heroes.iter_mut().find(|h| h.owner == id).unwrap();
                    h.offer_week = day / 7;
                    h.offer_until = day + 7;
                    h.offer_wage = wage;
                }
                let delay = self.rng.frand_range(20.0, 90.0);
                self.fx.text(&id, &crate::tr::m("estate.need_helper_job_offer", &[("kind", crate::tr::a(&(kind_name(&co.kind)))), ("company_name", crate::tr::a(&co.name)), ("hourly_wage", crate::tr::a(&crate::tr::money(wage)))]), "job_offer", delay);
                self.fx.log(format!("Street hero company: {name} offers him a job at ¥{wage}/h"));
            }
        }
        let _ = p;
    }
}

// ------------------------------------------------------------------------------------------------ requests

#[derive(Deserialize, Debug, Clone)]
struct DailyHeroine {
    id: String,
    actor: i32,
    /// Seeds her company's applicants and quits (Unreal's hash of her id).
    seed: i32,
}

/// The companies' day (from NextDay): his month on the 1st, his company's day, the heroines' companies.
fn daily(p: &PropertyData, r: &Value) -> Result<Value, String> {
    let mut w = World::from_request(p, r)?;
    let visited: BTreeSet<String> = serde_json::from_value(r.get("company_visited").cloned().unwrap_or(json!([]))).unwrap_or_default();
    let heroines: Vec<DailyHeroine> = serde_json::from_value(r.get("heroines").cloned().unwrap_or(json!([]))).map_err(|e| format!("heroines: {e}"))?;
    let d = date(w.day, 12.0);
    let month = month_of(w.day);
    if w.company.founded >= 0 && d.day == 1 && w.company.paid_month != month {
        w.company_month();
    }
    if w.company.founded >= 0 {
        w.ensure_company();
        let at_hq = w.company.staff.len() as i32;
        w.estate_daily("company", crate::tr::t("estate.pronoun_he"), &visited, at_hq, 7);
    }
    if !r.get("self_test").and_then(Value::as_bool).unwrap_or(false) {
        let list: Vec<(String, i32, i32)> = heroines.into_iter().map(|h| (h.id, h.actor, h.seed)).collect();
        w.heroes_daily(&list);
    }
    Ok(w.answer())
}

/// What he does with his company (and with hers). Request: action, args, plus the world.
fn action(p: &PropertyData, r: &Value) -> Result<Value, String> {
    let mut w = World::from_request(p, r)?;
    let act = r.get("action").and_then(Value::as_str).unwrap_or("");
    let s = |k: &str| r.get(k).and_then(Value::as_str).unwrap_or("").to_string();
    let n = |k: &str| r.get(k).and_then(Value::as_i64).unwrap_or(0);
    let b = |k: &str| r.get(k).and_then(Value::as_bool).unwrap_or(false);
    let notice = |text: String| Ok(json!({"notice": text}));
    let day = w.day;
    let unit_id = s("unit");
    // The applicants he sees are this week's (company_view makes them the same way): acting on one finds it, even before
    // the first new day of a company founded today.
    if w.company.founded >= 0 && act != "found" {
        w.applicants("company", 31);
    }
    match act {
        "found" => {
            let capital = crate::region::amount("cash", 50_000);
            if w.company.founded >= 0 {
                return notice(crate::tr::t("estate.already_have_company").into());
            }
            if w.cash + w.bank < capital {
                return notice(crate::tr::m("estate.company_capital_needed", &[("capital_amount", crate::tr::a(&crate::tr::money(capital)))]));
            }
            // The capital moves from his own money into the company's own account (a new company starts its books at 0).
            w.fx.0.push(json!({"op": "wallet", "book": "company", "act": "reset", "amount": 0, "kind": "", "note": ""}));
            let e = &mut w.company.estate;
            e.cash = 0;
            e.capital = 0;
            e.owner_loan = 0;
            e.retained = 0;
            w.from_owner("company", capital, "capital", crate::tr::t("estate.company_capital_label"));
            let name = s("name");
            let office = s("office");
            w.company.name = if name.is_empty() { crate::tr::t("estate.spark_studio_name").into() } else { name };
            w.company.office = if office.is_empty() { "cafe".into() } else { office };
            w.company.founded = day;
            w.company.capital = capital;
            w.ensure_company();
            let cname = w.company.name.clone();
            w.rumor("", crate::tr::m("estate.gossip_he_founded_company", &[("company_name", crate::tr::a(&cname))]), 70.0);
            let place = w.place_names.get(&w.company.office).cloned().unwrap_or_else(|| w.company.office.clone());
            w.message = crate::tr::m("estate.company_founded", &[("company_name", crate::tr::a(&cname)), ("place", crate::tr::a(&place))]);
        }
        "rent" => {
            w.ensure_company();
            if w.company.founded < 0 {
                return notice(crate::tr::t("estate.found_company_first").into());
            }
            let Some(u) = p.unit(&unit_id).filter(|u| u.rent > 0).cloned() else { return notice(crate::tr::t("estate.not_for_rent_here").into()) };
            if w.company.estate.holdings.iter().any(|h| h.unit == unit_id) {
                return notice(crate::tr::t("estate.already_yours").into());
            }
            let user = w.unit_user(&unit_id);
            if !user.is_empty() && user != "player" {
                return notice(crate::tr::t("estate.already_in_use_here").into());
            }
            let deposit = (u.rent as f32 * p.deposit_months) as i64;
            if !w.pay("company", deposit, "deposit", &crate::tr::m("estate.deposit_paid", &[("name", crate::tr::a(&u.name))])) || !w.pay("company", u.rent, "rent", &crate::tr::m("estate.first_month_rent_entry", &[("name", crate::tr::a(&u.name))])) {
                return notice(crate::tr::m("estate.not_enough_deposit_first_month", &[("deposit_amount", crate::tr::a(&crate::tr::money(deposit))), ("first_month_rent", crate::tr::a(&crate::tr::money(u.rent)))]));
            }
            let mut h = Holding { unit: unit_id.clone(), mode: "rent".into(), since: day, rent: u.rent, deposit, use_: if u.lot { "land".into() } else { u.kind.clone() }, ..Default::default() };
            // The first real office takes over from a borrowed corner as headquarters.
            if !u.lot && u.kind == "office" {
                let hq = w.company.estate.holdings.iter_mut().find(|x| x.hq);
                match hq {
                    Some(x) if x.mode == "borrow" => {
                        x.hq = false;
                        h.hq = true;
                    }
                    None => h.hq = true,
                    _ => {}
                }
            }
            w.company.estate.holdings.push(h);
            w.message = crate::tr::m("estate.rented_unit_terms", &[("name", crate::tr::a(&u.name)), ("rent", crate::tr::a(&crate::tr::money(u.rent))), ("deposit", crate::tr::a(&crate::tr::money(deposit)))]);
        }
        "buy" => {
            w.ensure_company();
            if w.company.founded < 0 {
                return notice(crate::tr::t("estate.found_company_first").into());
            }
            let loan_on = b("loan");
            let Some(u) = p.unit(&unit_id).filter(|u| u.price > 0).cloned() else { return notice(crate::tr::t("estate.not_for_sale_here").into()) };
            let held = w.company.estate.holdings.iter().position(|h| h.unit == unit_id);
            if held.map(|i| w.company.estate.holdings[i].mode == "own").unwrap_or(false) {
                return notice(crate::tr::t("estate.already_yours").into());
            }
            let user = w.unit_user(&unit_id);
            if !user.is_empty() && user != "player" {
                return notice(crate::tr::t("estate.already_in_use_here").into());
            }
            let price = p.land_price(&u, day);
            let closing = (price as f32 * p.closing) as i64;
            let down = if loan_on { (price as f32 * p.mortgage_down) as i64 } else { price };
            let loan = price - down;
            if !w.pay("company", down + closing, "buy", &crate::tr::m("estate.property_purchase_closing", &[("name", crate::tr::a(&u.name)), ("price_label", crate::tr::a(&(if loan_on { crate::tr::t("estate.down_payment_label") } else { crate::tr::t("estate.price_label") }))), ("closing_cost", crate::tr::a(&crate::tr::money(closing)))])) {
                return notice(crate::tr::m("estate.not_enough_cash_purchase", &[("total_amount", crate::tr::a(&crate::tr::money(down + closing))), ("payment_label", crate::tr::a(&(if loan_on { crate::tr::t("estate.down_payment_short") } else { crate::tr::t("estate.price_label") }))), ("down_payment", crate::tr::a(&crate::tr::money(down))), ("closing_cost", crate::tr::a(&crate::tr::money(closing)))]));
            }
            let idx = if let Some(i) = held {
                // Buying a unit he rents: the lease ends and its deposit comes back.
                let dep = w.company.estate.holdings[i].deposit;
                w.receive("company", dep, "deposit", &crate::tr::m("estate.deposit_refund_on_purchase", &[("name", crate::tr::a(&u.name))]));
                let x = &mut w.company.estate.holdings[i];
                x.mode = "own".into();
                x.rent = 0;
                x.deposit = 0;
                x.leave_month = -1;
                i
            } else {
                let mut h = Holding { unit: unit_id.clone(), mode: "own".into(), since: day, use_: if u.lot { "land".into() } else { u.kind.clone() }, ..Default::default() };
                if !u.lot && u.kind == "office" {
                    match w.company.estate.holdings.iter_mut().find(|x| x.hq) {
                        Some(x) if x.mode == "borrow" => {
                            x.hq = false;
                            h.hq = true;
                        }
                        None => h.hq = true,
                        _ => {}
                    }
                }
                w.company.estate.holdings.push(h);
                w.company.estate.holdings.len() - 1
            };
            let payment = PropertyData::loan_payment(loan, p.mortgage_rate, p.mortgage_months);
            let x = &mut w.company.estate.holdings[idx];
            x.bought = price;
            x.loan = loan;
            x.loan_payment = payment;
            x.missed_loan = 0;
            w.message = if loan_on {
                crate::tr::m("estate.bought_with_mortgage_details", &[("name", crate::tr::a(&u.name)), ("price", crate::tr::a(&crate::tr::money(price))), ("down_payment", crate::tr::a(&crate::tr::money(down))), ("loan", crate::tr::a(&crate::tr::money(loan))), ("monthly_payment", crate::tr::a(&crate::tr::money(payment))), ("months", crate::tr::a(&p.mortgage_months))])
            } else {
                crate::tr::m("estate.bought_property_price", &[("name", crate::tr::a(&u.name)), ("amount", crate::tr::a(&crate::tr::money(price)))])
            };
            w.rumor("", crate::tr::m("estate.gossip_he_bought", &[("name", crate::tr::a(&u.name))]), 60.0);
        }
        "build" => {
            let building = s("building");
            let i = w.company.estate.holdings.iter().position(|x| x.unit == unit_id);
            let bd = p.building(&building).cloned();
            let (Some(i), Some(bd)) = (i, bd) else { return notice(crate::tr::t("estate.build_only_on_own_land").into()) };
            if !w.unit_of(&unit_id).lot {
                return notice(crate::tr::t("estate.build_only_on_own_land").into());
            }
            if w.company.estate.holdings[i].build_start >= 0 {
                return notice(crate::tr::t("estate.land_already_built").into());
            }
            let first = bd.cost * 3 / 10;
            if !w.pay("company", first, "build", &crate::tr::m("estate.construction_deposit_30pct", &[("name", crate::tr::a(&bd.name))])) {
                return notice(crate::tr::m("estate.construction_first_payment_due", &[("amount", crate::tr::a(&crate::tr::money(first)))]));
            }
            let x = &mut w.company.estate.holdings[i];
            x.building = bd.id.clone();
            x.build_start = day;
            x.build_done = day + bd.days;
            x.build_paid = first;
            x.use_ = bd.kind.clone();
            let rented = x.mode == "rent";
            w.message = crate::tr::m("estate.construction_started_schedule", &[("unit_name", crate::tr::a(&(w.unit_of(&unit_id).name))), ("name", crate::tr::a(&bd.name)), ("days", crate::tr::a(&bd.days)), ("amount", crate::tr::a(&crate::tr::money(bd.cost - first))), ("leased_note", crate::tr::a(&(if rented { crate::tr::t("estate.leased_land_building_note") } else { "" })))]);
        }
        "set_hq" => {
            let Some(i) = w.company.estate.holdings.iter().position(|x| x.unit == unit_id) else { return notice(crate::tr::t("estate.cannot_operate_office_yet").into()) };
            let h = w.company.estate.holdings[i].clone();
            if w.holding_capacity(&h, day) <= 0 || w.holding_use(&h) == "land" {
                return notice(crate::tr::t("estate.cannot_operate_office_yet").into());
            }
            for x in w.company.estate.holdings.iter_mut() {
                x.hq = false;
            }
            w.company.estate.holdings[i].hq = true;
            let name = w.unit_of(&unit_id).name;
            let place = w.holding_place(&h);
            for st in w.company.staff.clone() {
                w.observe(&st.heroine, crate::tr::m("estate.company_moved_to", &[("place", crate::tr::a(&name))]), &place, 0.5);
            }
            w.message = crate::tr::m("estate.headquarters_located_at", &[("place", crate::tr::a(&name))]);
        }
        "leave" => {
            let Some(i) = w.company.estate.holdings.iter().position(|x| x.unit == unit_id && x.mode != "own") else { return notice(crate::tr::t("estate.not_renting_here").into()) };
            if w.company.estate.holdings[i].mode == "borrow" {
                let hq = w.company.estate.holdings[i].hq;
                w.company.estate.holdings.remove(i);
                if hq {
                    if let Some(x) = w.company.estate.holdings.first_mut() {
                        x.hq = true;
                    }
                }
                w.message = crate::tr::t("estate.stop_borrowing_corner").into();
            } else {
                w.company.estate.holdings[i].leave_month = month_of(day) + 1;
                w.message = crate::tr::m("estate.moveout_notice_sent", &[("unit_name", crate::tr::a(&(w.unit_of(&unit_id).name)))]);
            }
        }
        "sell" => {
            let Some(i) = w.company.estate.holdings.iter().position(|x| x.unit == unit_id && x.mode == "own") else { return notice(crate::tr::t("estate.not_yours_here").into()) };
            let h = w.company.estate.holdings[i].clone();
            if h.build_start >= 0 && h.build_done > day {
                return notice(crate::tr::t("estate.cannot_sell_under_construction").into());
            }
            let value = (w.holding_value(&h, day) as f32 * p.resale) as i64;
            let net = value - h.loan;
            let name = w.unit_of(&unit_id).name;
            w.receive("company", net, "sell", &crate::tr::m("estate.sold_property_repay_loan", &[("name", crate::tr::a(&name)), ("value", crate::tr::a(&crate::tr::money(value))), ("loan", crate::tr::a(&crate::tr::money(h.loan)))]));
            w.company.estate.holdings.remove(i);
            w.rehome("company", &unit_id);
            w.message = crate::tr::m("estate.sold_property_net", &[("name", crate::tr::a(&name)), ("value", crate::tr::a(&crate::tr::money(value))), ("net_amount", crate::tr::a(&crate::tr::money(net)))]);
        }
        "pay_off" => {
            let Some(i) = w.company.estate.holdings.iter().position(|x| x.unit == unit_id && x.loan > 0) else { return notice(crate::tr::t("estate.no_mortgage_due").into()) };
            let loan = w.company.estate.holdings[i].loan;
            let name = w.unit_of(&unit_id).name;
            if !w.pay("company", loan, "loan_principal", &crate::tr::m("estate.mortgage_prepaid", &[("name", crate::tr::a(&name))])) {
                return notice(crate::tr::m("estate.payoff_amount_notice", &[("amount", crate::tr::a(&crate::tr::money(loan)))]));
            }
            let x = &mut w.company.estate.holdings[i];
            x.loan = 0;
            x.loan_payment = 0;
            x.missed_loan = 0;
            w.message = crate::tr::m("estate.mortgage_fully_paid", &[("name", crate::tr::a(&name))]);
        }
        "lease_out" => {
            let rent = n("rent");
            let Some(i) = w.company.estate.holdings.iter().position(|x| x.unit == unit_id && x.mode == "own") else { return notice(crate::tr::t("estate.only_own_property_rentable").into()) };
            let tenant = w.company.estate.holdings[i].tenant.clone();
            let name = w.unit_of(&unit_id).name;
            if !tenant.is_empty() && rent <= 0 {
                // A tenant: her lease ends next month.
                let next = month_of(day) + 1;
                if let Some(co) = w.heroes.iter_mut().find(|h| h.owner == tenant) {
                    if let Some(hers) = co.estate.holdings.iter_mut().find(|x| x.unit == unit_id) {
                        hers.leave_month = next;
                    }
                }
                w.observe(&tenant, crate::tr::m("estate.landlord_reclaiming_unit", &[("name", crate::tr::a(&name))]), "", 0.75);
                w.company.estate.holdings[i].lease_rent = 0;
                w.message = crate::tr::t("estate.tenant_notified_reclaim").into();
            } else {
                let x = &mut w.company.estate.holdings[i];
                x.lease_rent = rent.max(0);
                if x.hq && rent > 0 {
                    x.hq = false;
                }
                w.message = if rent > 0 { crate::tr::m("estate.listed_for_rent", &[("name", crate::tr::a(&name)), ("rent", crate::tr::a(&crate::tr::money(rent)))]) } else { crate::tr::m("estate.stop_renting_out", &[("name", crate::tr::a(&name))]) };
            }
        }
        "hire" => {
            let applicant = s("applicant");
            let offer = n("offer");
            let e = &w.company.estate;
            let Some(k) = e.applicants.iter().position(|a| a.id == applicant).filter(|_| !e.refused.contains(&applicant)) else { return notice(crate::tr::t("estate.applicant_dropped").into()) };
            let Some(h) = e.holdings.iter().find(|x| x.unit == unit_id).cloned() else { return notice(crate::tr::t("estate.branch_full_rent_or_build").into()) };
            let at_hq = w.company.staff.len() as i32;
            if w.holding_capacity(&h, day) <= w.headcount(e, &unit_id, at_hq) {
                return notice(crate::tr::t("estate.branch_full_rent_or_build").into());
            }
            let mut worker = e.applicants[k].clone();
            let ratio = offer as f32 / worker.ask.max(1) as f32;
            let chance = if ratio >= 1.0 { 1.0 } else { ratio.powf(4.0) * (0.5 + e.reputation / 100.0).clamp(0.3, 1.3) };
            let roll = r.get("roll").and_then(Value::as_f64).unwrap_or(0.0) as f32;
            if roll >= chance {
                w.company.estate.refused.push(applicant);
                w.message = crate::tr::m("estate.worker_declined_salary", &[("name", crate::tr::a(&worker.name)), ("amount", crate::tr::a(&crate::tr::money(worker.ask)))]);
            } else {
                w.company.estate.applicants.remove(k);
                worker.pay = offer;
                worker.holding = unit_id.clone();
                worker.since = day;
                worker.morale = (60.0 + (ratio - 1.0) * 60.0).clamp(40.0, 95.0);
                let role = p.role(&worker.role).map(|r| r.name.clone()).unwrap_or_else(|| worker.role.clone());
                w.message = crate::tr::m("estate.hired_worker", &[("name", crate::tr::a(&worker.name)), ("role", crate::tr::a(&role)), ("amount", crate::tr::a(&crate::tr::money(offer)))]);
                w.company.estate.workers.push(worker);
            }
        }
        "fire" => {
            let id = s("worker");
            let Some(k) = w.company.estate.workers.iter().position(|x| x.id == id) else { return Ok(json!({})) };
            let worker = w.company.estate.workers[k].clone();
            let severance = (worker.pay as f32 * p.severance_months) as i64;
            if !w.pay("company", severance, "severance", &crate::tr::m("estate.severance_pay_entry", &[("name", crate::tr::a(&worker.name))])) {
                return notice(crate::tr::m("estate.severance_cost_notice", &[("amount", crate::tr::a(&crate::tr::money(severance)))]));
            }
            let e = &mut w.company.estate;
            e.workers.remove(k);
            e.reputation = (e.reputation - 3.0).max(0.0);
            for x in e.workers.iter_mut() {
                x.morale = (x.morale - 5.0).max(0.0);
            }
            w.message = crate::tr::m("estate.laid_off_severance", &[("name", crate::tr::a(&worker.name)), ("severance_amount", crate::tr::a(&crate::tr::money(severance)))]);
        }
        "raise" => {
            let id = s("worker");
            let pay = n("pay");
            let Some(x) = w.company.estate.workers.iter_mut().find(|x| x.id == id).filter(|_| pay > 0) else { return Ok(json!({})) };
            x.morale = (x.morale + (pay - x.pay) as f32 / x.pay.max(1) as f32 * 80.0).clamp(0.0, 100.0);
            x.pay = pay;
            w.message = crate::tr::m("estate.salary_raised", &[("name", crate::tr::a(&x.name)), ("amount", crate::tr::a(&crate::tr::money(pay)))]);
        }
        "assign" => {
            let id = s("worker");
            let h = w.company.estate.holdings.iter().find(|x| x.unit == unit_id).cloned();
            let at_hq = w.company.staff.len() as i32;
            let full = h.as_ref().map(|h| w.holding_capacity(h, day) <= w.headcount(&w.company.estate, &unit_id, at_hq)).unwrap_or(true);
            let has = w.company.estate.workers.iter().any(|x| x.id == id);
            if !has || full {
                return notice(crate::tr::t("estate.no_vacancy_there").into());
            }
            let name = w.unit_of(&unit_id).name;
            let x = w.company.estate.workers.iter_mut().find(|x| x.id == id).unwrap();
            x.holding = unit_id.clone();
            w.message = crate::tr::m("estate.transferred_to_branch", &[("name", crate::tr::a(&x.name)), ("place", crate::tr::a(&name))]);
        }
        "inject" => {
            let amount = n("amount");
            if w.company.founded < 0 || amount <= 0 {
                return Ok(json!({}));
            }
            if !w.from_owner("company", amount, "capital", crate::tr::t("estate.shareholder_capital_injection")) {
                return notice(crate::tr::t("estate.savings_and_wallet_short").into());
            }
            w.company.capital = w.company.estate.capital;
            w.message = crate::tr::m("estate.capital_injection_made", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("capital", crate::tr::a(&crate::tr::money(w.company.capital)))]);
        }
        "repay_owner" => {
            let amount = n("amount");
            let e = &w.company.estate;
            if e.owner_loan <= 0 {
                return notice(crate::tr::t("estate.company_owes_no_money").into());
            }
            let take = (if amount > 0 { amount } else { e.owner_loan }).min(e.owner_loan).min(e.cash.max(0));
            if take <= 0 {
                return notice(crate::tr::t("estate.company_no_cash_to_repay").into());
            }
            w.repay_owner("company", take);
            w.message = crate::tr::m("estate.shareholder_loan_repaid", &[("repaid_amount", crate::tr::a(&crate::tr::money(take))), ("owed_amount", crate::tr::a(&crate::tr::money(w.company.estate.owner_loan)))]);
        }
        "bonus" => {
            let amount = n("amount");
            if w.company.founded < 0 || amount <= 0 {
                return Ok(json!({}));
            }
            if w.company.estate.cash < amount {
                return notice(crate::tr::m("estate.bonus_limited_to_cash", &[("cash_amount", crate::tr::a(&crate::tr::money(w.company.estate.cash.max(0))))]));
            }
            w.pay("company", amount, "bonus", crate::tr::t("estate.bonus_to_owner"));
            w.bank_op(amount, "bonus", crate::tr::t("estate.bonus_paid_by_company"));
            w.personal_year();
            let (tax, how) = if w.personal.bonus_year != w.personal.tax_year {
                w.personal.bonus_year = w.personal.tax_year;
                let t = p.bonus_tax(amount);
                w.personal.ytd_bonus += amount;
                w.personal.ytd_bonus_tax += t;
                w.personal.ytd_tax += t;
                (t, crate::tr::t("estate.annual_bonus_taxed_separately"))
            } else {
                w.personal.ytd_income += amount;
                let t = w.cumulative_due();
                w.personal.ytd_tax += t;
                (t, crate::tr::t("estate.bonus_already_used_this_year"))
            };
            w.bank_op(-tax, "iit", &crate::tr::m("estate.bonus_tax_label", &[("how", crate::tr::a(&how))]));
            w.message = crate::tr::m("estate.bonus_paid_to_you", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("tax", crate::tr::a(&crate::tr::money(tax))), ("how", crate::tr::a(&how)), ("net_amount", crate::tr::a(&crate::tr::money(amount - tax)))]);
        }
        "dividend" => {
            let amount = n("amount");
            if w.company.founded < 0 || amount <= 0 {
                return Ok(json!({}));
            }
            let e = &w.company.estate;
            let most = e.retained.min(e.cash.max(0));
            if amount > most {
                return notice(crate::tr::m("estate.dividend_limit_explained", &[("retained_amount", crate::tr::a(&crate::tr::money(e.retained))), ("cash_amount", crate::tr::a(&crate::tr::money(e.cash.max(0)))), ("max_amount", crate::tr::a(&crate::tr::money(most.max(0))))]));
            }
            w.to_owner("company", amount, "dividend", crate::tr::t("estate.dividend_to_owner"));
            w.company.estate.retained -= amount;
            w.fx.0.push(json!({"op": "wallet", "book": "company", "act": "retained", "amount": -amount, "kind": "dividend", "note": ""}));
            let tax = round_half_from_zero(amount as f64 * 0.2);
            w.bank_op(-tax, "dividend_tax", crate::tr::t("estate.dividend_tax_20_percent"));
            w.personal.ytd_tax += tax;
            w.personal.ytd_bonus_tax += tax;
            w.message = crate::tr::m("estate.dividend_paid", &[("amount", crate::tr::a(&crate::tr::money(amount))), ("tax", crate::tr::a(&crate::tr::money(tax))), ("net_amount", crate::tr::a(&crate::tr::money(amount - tax)))]);
        }
        "owner_pay" => {
            let pay = n("pay");
            if w.company.founded < 0 {
                return Ok(json!({}));
            }
            w.company.estate.owner_pay = pay.max(0);
            w.message = if pay > 0 { crate::tr::m("estate.owner_salary_set", &[("amount", crate::tr::a(&crate::tr::money(pay)))]) } else { crate::tr::t("estate.owner_no_salary").into() };
        }
        "auto_fund" => {
            let on = b("on");
            w.company.estate.auto_fund = on;
            w.message = if on { crate::tr::t("estate.auto_loan_from_you").into() } else { crate::tr::t("estate.stop_lending_to_you").into() };
        }
        "hero_offer_answer" => {
            let owner = s("owner");
            let accept = b("accept");
            let Some(co) = w.heroes.iter_mut().find(|h| h.owner == owner) else { return Ok(json!({})) };
            co.offer_until = -1;
            let (name, wage) = (co.name.clone(), co.offer_wage);
            let job = format!("{HERO_JOB}{owner}");
            if accept {
                if !w.player_jobs.contains(&job) {
                    w.player_jobs.push(job.clone());
                }
                w.job_rating.entry(job).or_insert(65);
            }
            w.observe(&owner, if accept { crate::tr::m("estate.accepted_job_at_your_company", &[("name", crate::tr::a(&name))]) } else { crate::tr::m("estate.declined_job_at_your_company", &[("name", crate::tr::a(&name))]) }, "", if accept { 0.7 } else { 0.5 });
            w.message = if accept { crate::tr::m("estate.accepted_job_scheduled", &[("name", crate::tr::a(&name)), ("hourly_wage", crate::tr::a(&crate::tr::money(wage)))]) } else { crate::tr::m("estate.declined_job_invite", &[("name", crate::tr::a(&name))]) };
        }
        "hero_job_quit" => {
            let owner = s("owner");
            let job = format!("{HERO_JOB}{owner}");
            w.player_jobs.retain(|j| *j != job);
            w.rel_step(&owner, |r| r.add_liking(-2.0));
            w.observe(&owner, crate::tr::t("estate.quit_your_company_job").into(), "", 0.6);
            w.message = crate::tr::t("estate.quit_that_job").into();
        }
        "hero_job_paid" => {
            // His shift at her company, paid from her company's money; she remembers when she could not pay.
            let job = s("job");
            let pay = n("pay");
            let minutes = n("minutes");
            let Some(owner) = job.strip_prefix(HERO_JOB).map(String::from) else { return Ok(w.answer()) };
            if !w.heroes.iter().any(|h| h.owner == owner) {
                return Ok(w.answer());
            }
            w.heroes.iter_mut().find(|h| h.owner == owner).unwrap().player_minutes += minutes;
            let employer = p.social(pay, true, minutes as f32 / (176.0 * 60.0)).total();
            let book = job.clone();
            if !w.pay(&book, pay + employer, "wage", &crate::tr::m("estate.his_parttime_salary", &[("amount", crate::tr::a(&crate::tr::money(employer)))])) {
                let name = w.heroes.iter().find(|h| h.owner == owner).unwrap().name.clone();
                w.cash_op(-pay, "wage", &crate::tr::m("estate.company_cannot_pay_salary", &[("name", crate::tr::a(&name))]));
                w.observe(&owner, crate::tr::t("estate.cannot_pay_his_salary").into(), "", 0.8);
                w.message = crate::tr::t("estate.her_company_cannot_pay").into();
            }
        }
        "settle_month" => {
            // His company's month right now (the in-game accounting test).
            if w.company.founded >= 0 {
                w.company_month();
            }
        }
        "ensure" => w.ensure_company(),
        "hero_take_place" => {
            // A heroine's company finding a place (the in-game test sets one up by hand).
            let owner = s("owner");
            let kind = s("kind");
            if !w.heroes.iter().any(|h| h.owner == owner) || !w.hero_take_place(&owner, &kind) {
                return notice(crate::tr::t("estate.she_no_place_to_operate").into());
            }
        }
        "applicants" => {
            w.ensure_company();
            w.applicants("company", 31);
        }
        "show_around" => {
            // Showing her his own place: her idea of what he has, and a little admiration.
            let id = s("heroine");
            w.rel_step(&id, |r| {
                r.perceived_wealth = (r.perceived_wealth + 5.0).min(100.0);
                r.admiration = (r.admiration + 2.0).min(100.0);
            });
        }
        "withhold" => {
            // A part-time job's pay already in his wallet: his share and the tax withheld (EndShift).
            let gross = n("gross");
            let minutes = n("minutes");
            // On the mainland a student's part-time work (勤工助學) is labour service, not employment: no 五險一金, only the
            // income tax on labour remuneration (none up to 800 a payment). Elsewhere a part-timer is insured like staff.
            w.payslip = if crate::locale::region(r) == "CN" { w.player_labour_service(gross, "cash") } else { w.player_payroll(gross, minutes as f32 / (176.0 * 60.0), "cash") };
        }
        _ => return Err(format!("unknown_company_action: {act}")),
    }
    Ok(w.answer())
}

/// The mainland's income tax on one payment of labour remuneration (劳务报酬): nothing up to 800, 20% of what is over
/// 800 up to 4000, 20% of 80% of the pay above it.
pub fn labour_service_tax(gross: i64) -> i64 {
    if gross <= 800 {
        0
    } else if gross <= 4000 {
        round_half_from_zero((gross - 800) as f64 * 0.2)
    } else {
        round_half_from_zero(gross as f64 * 0.8 * 0.2)
    }
}

/// What the company screens show: each holding's figures, his equity, what market units cost today, who uses what.
fn view(p: &PropertyData, r: &Value) -> Result<Value, String> {
    let mut w = World::from_request(p, r)?;
    // What the app shows: his company as it would be (a borrowed corner when it has none, this week's applicants).
    if w.company.founded >= 0 {
        w.ensure_company();
        w.applicants("company", 31);
    }
    let day = w.day;
    let holding = |e: &Estate, h: &Holding, staff: Option<&[Employee]>, extra: f64, at_hq: i32| {
        json!({"unit": h.unit, "name": w.unit_of(&h.unit).name, "place": w.holding_place(h), "use": w.holding_use(h), "capacity": w.holding_capacity(h, day),
            "headcount": w.headcount(e, &h.unit, at_hq), "value": w.holding_value(h, day), "revenue": w.revenue(e, h, day, staff, extra), "costs": w.holding_costs(h, day),
            "sale_net": (w.holding_value(h, day) as f32 * p.resale) as i64 - h.loan})
    };
    let e = &w.company.estate;
    let staff = w.company.staff.clone();
    let mine: Vec<Value> = e.holdings.iter().map(|h| holding(e, h, Some(&staff), 0.0, staff.len() as i32)).collect();
    // What a company is worth to its owner: its money, what it owns at market and the deposits it paid, less what it owes.
    let equity = |e: &Estate| {
        let mut v = e.cash - e.owner_loan - e.debt as i64;
        for h in &e.holdings {
            v += w.holding_value(h, day) - h.loan;
            if h.mode == "rent" {
                v += h.deposit;
            }
        }
        v
    };
    // His: less the deposits his tenants paid him (his to give back).
    let tenant_deposits: i64 = w.heroes.iter().flat_map(|c| c.estate.holdings.iter()).filter(|x| x.landlord == "player").map(|x| x.deposit).sum();
    let heroes: Vec<Value> = w
        .heroes
        .iter()
        .map(|c| {
            let hs: Vec<Value> = c.estate.holdings.iter().map(|h| holding(&c.estate, h, None, 0.0, if h.hq { 1 } else { 0 })).collect();
            json!({"owner": c.owner, "holdings": hs, "equity": equity(&c.estate)})
        })
        .collect();
    let market: Vec<Value> = p
        .units
        .iter()
        .map(|u| {
            let price = p.land_price(u, day);
            json!({"unit": u.id, "price": price, "closing": (price as f32 * p.closing) as i64, "down": (price as f32 * p.mortgage_down) as i64,
                "loan_payment": PropertyData::loan_payment(price - (price as f32 * p.mortgage_down) as i64, p.mortgage_rate, p.mortgage_months),
                "deposit": (u.rent as f32 * p.deposit_months) as i64, "user": w.unit_user(&u.id)})
        })
        .collect();
    let severance: serde_json::Map<String, Value> = e.workers.iter().map(|x| (x.id.clone(), json!((x.pay as f32 * p.severance_months) as i64))).collect();
    Ok(json!({"company": w.company, "holdings": mine, "severance": severance, "equity": if w.company.founded >= 0 { equity(e) - tenant_deposits } else { 0 }, "hero_companies": heroes, "market": market,
        "mortgage_months": p.mortgage_months, "severance_months": p.severance_months}))
}

pub fn eval(p: &PropertyData, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "company_daily" => daily(p, r),
        "company_action" => action(p, r),
        "company_view" => view(p, r),
        "bonus_tax" => Ok(json!({"tax": p.bonus_tax(r.get("bonus").and_then(Value::as_i64).unwrap_or(0))})),
        "loan_payment" => {
            let principal = r.get("principal").and_then(Value::as_i64).unwrap_or(0);
            Ok(json!({"payment": PropertyData::loan_payment(principal, p.mortgage_rate, p.mortgage_months)}))
        }
        "net_pay" => {
            let gross = r.get("gross").and_then(Value::as_i64).unwrap_or(0);
            let months = r.get("months").and_then(Value::as_f64).unwrap_or(1.0) as f32;
            let (net, social, tax) = p.net_pay(gross, months);
            Ok(json!({"net": net, "social": social, "tax": tax}))
        }
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_students_shift_pay_is_labour_service_on_the_mainland() {
        assert_eq!(labour_service_tax(60), 0);
        assert_eq!(labour_service_tax(800), 0);
        assert_eq!(labour_service_tax(1800), 200);
        assert_eq!(labour_service_tax(5000), 800);
    }

    use crate::tuning::Tuning;

    fn world(extra: Value) -> Value {
        let mut base = json!({"day": 30, "player_cash": 20000, "player_bank": 100000, "company": {"founded": -1}, "hero_companies": [], "relations": [], "personal": {}});
        base.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
        base
    }

    fn ops(v: &Value, act: &str) -> Vec<Value> {
        v["effects"].as_array().unwrap().iter().filter(|e| e["op"] == "wallet" && e["act"] == act).cloned().collect()
    }

    #[test]
    fn found_rent_hire_and_settle_a_month() {
        let p = Tuning::builtin().property;
        let v = action(&p, &json!({"action": "found", "name": "星火", "office": "cafe", "place_names": {"cafe": "咖啡廳"}, "day": 30, "player_cash": 20000, "player_bank": 100000, "company": {"founded": -1}})).unwrap();
        assert_eq!(v["company"]["founded"], 30);
        assert_eq!(v["company"]["estate"]["holdings"][0]["unit"], "borrow:cafe");
        assert_eq!(ops(&v, "from_owner")[0]["amount"], 50000);
        assert_eq!((v["check"]["player_bank"].as_i64(), v["check"]["player_cash"].as_i64()), (Some(50000), Some(20000)), "savings first");
        // Rent an office unit.
        let office = p.units.iter().find(|u| !u.lot && u.kind == "office" && u.rent > 0).expect("an office to rent").clone();
        let company = v["company"].clone();
        let v = action(&p, &world(json!({"action": "rent", "unit": office.id, "company": company, "player_bank": 50000}))).unwrap();
        assert!(v["message"].as_str().unwrap().contains("租下了"), "{v}");
        let hs = v["company"]["estate"]["holdings"].as_array().unwrap();
        assert!(hs.iter().any(|h| h["unit"] == office.id.as_str() && h["hq"] == true), "the office became headquarters");
        // A month later, with a worker: revenue, VAT, rent, wages, overhead.
        let mut company = v["company"].clone();
        company["estate"]["workers"] = json!([{"id": "w1", "name": "王小明", "role": p.roles[0].id, "holding": office.id, "skill": 60, "diligence": 60, "loyalty": 60, "morale": 70, "pay": 3000}]);
        company["paid_month"] = json!(0);
        let day1 = (crate::calendar::days_from_civil(2010, 11, 1) - crate::calendar::days_from_civil(2010, 9, 1)) as i32;
        let v = daily(&p, &world(json!({"day": day1, "company": company}))).unwrap();
        assert!(ops(&v, "receive").iter().any(|o| o["kind"] == "revenue"));
        assert!(ops(&v, "pay").iter().any(|o| o["kind"] == "rent") && ops(&v, "pay").iter().any(|o| o["kind"] == "wage"));
        assert_eq!(v["company"]["paid_month"], month_of(day1));
    }

    #[test]
    fn unpaid_rent_twice_loses_the_lease() {
        let p = Tuning::builtin().property;
        let office = p.units.iter().find(|u| !u.lot && u.rent > 0).unwrap().clone();
        let company = json!({"founded": 0, "paid_month": 0, "name": "x", "estate": {"auto_fund": false, "cash": 0,
            "holdings": [{"unit": office.id, "mode": "rent", "rent": office.rent, "deposit": office.rent * 2, "missed_rent": 1, "hq": true}]}});
        let day1 = (crate::calendar::days_from_civil(2010, 11, 1) - crate::calendar::days_from_civil(2010, 9, 1)) as i32;
        let v = daily(&p, &world(json!({"day": day1, "company": company, "player_cash": 0, "player_bank": 0}))).unwrap();
        let hs = v["company"]["estate"]["holdings"].as_array().unwrap();
        assert!(hs.iter().all(|h| h["unit"] != office.id.as_str()), "the lease is lost");
        assert!(hs.iter().any(|h| h["mode"] == "borrow"), "like Unreal's CompanyEnsure: a borrowed corner again");
        assert!(v["effects"].as_array().unwrap().iter().any(|e| e["op"] == "rumor" && e["text"].as_str().unwrap().contains("付不出房租")));
    }

    #[test]
    fn wallet_borrows_from_the_owner_when_short() {
        let p = Tuning::builtin().property;
        let mut w = World::from_request(&p, &world(json!({"company": {"founded": 0, "estate": {"cash": 100, "auto_fund": true}}, "player_bank": 1000, "player_cash": 0}))).unwrap();
        assert!(w.pay("company", 500, "rent", "x"));
        assert_eq!((w.company.estate.cash, w.company.estate.owner_loan, w.bank), (0, 400, 600));
        assert!(!w.pay("company", 700, "rent", "x"), "600 is all he has");
        w.receive("company", 1000, "revenue", "x");
        w.repay_owner("company", 999);
        assert_eq!((w.company.estate.owner_loan, w.bank, w.company.estate.cash), (0, 1000, 600));
    }

    #[test]
    fn hire_on_the_day_it_was_founded() {
        // Playtest 2026-10-13: the screen showed this week's applicants, hiring one said "no longer considering".
        let p = Tuning::builtin().property;
        let v = action(&p, &json!({"action": "found", "name": "星火", "office": "cafe", "place_names": {"cafe": "咖啡廳"}, "day": 9, "player_cash": 60000, "company": {"founded": -1}})).unwrap();
        let company = v["company"].clone();
        let shown = view(&p, &world(json!({"day": 9, "company": company}))).unwrap();
        let first = shown["company"]["estate"]["applicants"][0].clone();
        assert!(first.is_object(), "{shown}");
        let unit = shown["company"]["estate"]["holdings"][0]["unit"].as_str().unwrap().to_string();
        let v = action(&p, &world(json!({"action": "hire", "day": 9, "company": company, "applicant": first["id"], "offer": first["ask"], "unit": unit, "roll": 0.5}))).unwrap();
        assert_eq!(v["company"]["estate"]["workers"].as_array().map(|w| w.len()), Some(1), "{v}");
    }

    #[test]
    fn applicants_are_seeded_by_week() {
        let p = Tuning::builtin().property;
        let mut a = World::from_request(&p, &world(json!({"company": {"founded": 0}}))).unwrap();
        a.applicants("company", 7);
        let mut b = World::from_request(&p, &world(json!({"company": {"founded": 0}}))).unwrap();
        b.applicants("company", 7);
        assert!(!a.company.estate.applicants.is_empty());
        assert_eq!(a.company.estate.applicants, b.company.estate.applicants);
        assert!(a.company.estate.applicants.iter().all(|w| w.ask % 50 == 0 && (20..=90).contains(&w.skill)));
    }

    #[test]
    fn a_rich_heroine_may_open_her_own_company() {
        let p = Tuning::builtin().property;
        let mut opened = 0;
        for day in 1..120 {
            let v = daily(&p, &world(json!({"day": day, "relations": [{"id": "a", "bank": 400000, "cash": 1000, "stage": "friend"}],
                "profiles": {"a": {"occupation": {"competence": 75}}}, "names": {"a": "小美"}, "heroines": [{"id": "a", "actor": 1, "seed": 99}]}))).unwrap();
            if !v["hero_companies"].as_array().unwrap().is_empty() {
                opened += 1;
                let hc = &v["hero_companies"][0];
                assert_eq!(hc["kind"], "office");
                assert!(v["effects"].as_array().unwrap().iter().any(|e| e["op"] == "hero_open"));
                assert!(ops(&v, "from_owner").iter().any(|o| o["book"] == "hc_a" && o["amount"] == 200500));
            }
        }
        assert!(opened > 0, "within four months she opens at some point");
    }
}
