//! The property market and the economy's numbers (Content/IndependentResources/StreetSocialData/property.json): units
//! and lots for rent or sale, buildings, roles and pay, names for applicants, and mainland China payroll and business
//! taxes. Read like Unreal's OKStreetProperty() so both sides see the same market.
use serde_json::Value;
use std::collections::BTreeMap;

#[derive(Debug, Clone, Default)]
pub struct UnitDef {
    pub id: String,
    pub name: String,
    pub place: String,
    pub kind: String,
    pub floor: i32,
    pub capacity: i32,
    pub area: f32,
    pub traffic: f32,
    pub rent: i64,
    pub price: i64,
    pub lot: bool,
}

#[derive(Debug, Clone, Default)]
pub struct BuildingDef {
    pub id: String,
    pub name: String,
    pub kind: String,
    pub cost: i64,
    pub days: i32,
    pub capacity: i32,
    pub floors: i32,
}

#[derive(Debug, Clone, Default)]
pub struct RoleDef {
    pub id: String,
    pub name: String,
    pub weight: f32,
    pub base_pay: i64,
}

#[derive(Debug, Clone)]
pub struct PropertyData {
    pub units: Vec<UnitDef>,
    pub buildings: Vec<BuildingDef>,
    pub roles: Vec<RoleDef>,
    pub family: Vec<String>,
    pub given: Vec<String>,
    pub deposit_months: f32,
    pub land_growth: f32,
    pub property_tax: f32,
    pub resale: f32,
    pub closing: f32,
    pub severance_months: f32,
    pub mortgage_down: f32,
    pub mortgage_rate: f32,
    pub foreclosure_ratio: f32,
    pub building_resale: f32,
    pub mortgage_months: i32,
    pub employee_rates: BTreeMap<String, f32>,
    pub employer_rates: BTreeMap<String, f32>,
    pub base_min: f32,
    pub base_max: f32,
    pub iit_threshold: f32,
    pub vat_small: f32,
    pub cit_small: f32,
    pub cit_rate: f32,
    pub cit_small_limit: f32,
    /// (upper bound of yearly taxable income, rate, quick deduction)
    pub iit_brackets: Vec<(f64, f64, f64)>,
}

fn num(o: &Value, k: &str, def: f64) -> f64 {
    o.get(k).and_then(Value::as_f64).unwrap_or(def)
}

fn text(o: &Value, k: &str) -> String {
    o.get(k).and_then(Value::as_str).unwrap_or("").to_string()
}

impl PropertyData {
    pub fn parse(root: &Value) -> PropertyData {
        let mut units = Vec::new();
        for (field, lot) in [("units", false), ("lots", true)] {
            for o in root.get(field).and_then(Value::as_array).into_iter().flatten() {
                let mut u = UnitDef {
                    id: text(o, "id"),
                    name: text(o, "name"),
                    place: text(o, "place"),
                    kind: text(o, "kind"),
                    floor: num(o, "floor", 1.0) as i32,
                    capacity: num(o, "capacity", if lot { 0.0 } else { 4.0 }) as i32,
                    area: num(o, "area", 40.0) as f32,
                    traffic: num(o, "traffic", 0.5) as f32,
                    rent: num(o, "rent", 0.0) as i64,
                    price: num(o, "price", 0.0) as i64,
                    lot,
                };
                if lot && u.kind.is_empty() {
                    u.kind = "land".into();
                }
                if !u.id.is_empty() {
                    units.push(u);
                }
            }
        }
        let buildings = root
            .get("buildings")
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .map(|o| BuildingDef {
                id: text(o, "id"),
                name: text(o, "name"),
                kind: text(o, "kind"),
                cost: num(o, "cost", 100000.0) as i64,
                days: num(o, "days", 10.0) as i32,
                capacity: num(o, "capacity", 6.0) as i32,
                floors: num(o, "floors", 1.0) as i32,
            })
            .filter(|b| !b.id.is_empty())
            .collect();
        let roles = root
            .get("roles")
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .map(|o| RoleDef { id: text(o, "id"), name: text(o, "name"), weight: num(o, "weight", 1.0) as f32, base_pay: num(o, "base_pay", 2400.0) as i64 })
            .filter(|r| !r.id.is_empty())
            .collect();
        let names = root.get("names").cloned().unwrap_or(Value::Null);
        let strings = |k: &str| names.get(k).and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_str).map(String::from).collect()).unwrap_or_default();
        let e = root.get("economy").cloned().unwrap_or(Value::Null);
        let cn = root.get("china").cloned().unwrap_or(Value::Null);
        let rates = |k: &str| -> BTreeMap<String, f32> { cn.get(k).and_then(Value::as_object).map(|m| m.iter().filter_map(|(k, v)| v.as_f64().map(|v| (k.clone(), v as f32))).collect()).unwrap_or_default() };
        let brackets = cn
            .get("iit_brackets_yearly")
            .and_then(Value::as_array)
            .map(|a| {
                a.iter()
                    .filter_map(|r| r.as_array().filter(|r| r.len() == 3).map(|r| (r[0].as_f64().unwrap_or(0.0), r[1].as_f64().unwrap_or(0.0), r[2].as_f64().unwrap_or(0.0))))
                    .collect()
            })
            .unwrap_or_default();
        PropertyData {
            units,
            buildings,
            roles,
            family: strings("family"),
            given: strings("given"),
            deposit_months: num(&e, "deposit_months", 2.0) as f32,
            land_growth: num(&e, "land_growth_monthly", 0.004) as f32,
            property_tax: num(&e, "property_tax_yearly", 0.012) as f32,
            resale: num(&e, "resale_ratio", 0.92) as f32,
            closing: num(&e, "closing_cost", 0.03) as f32,
            severance_months: num(&e, "severance_months", 1.0) as f32,
            mortgage_down: num(&e, "mortgage_down", 0.3) as f32,
            mortgage_rate: num(&e, "mortgage_rate", 0.05) as f32,
            mortgage_months: num(&e, "mortgage_months", 240.0) as i32,
            foreclosure_ratio: num(&e, "foreclosure_ratio", 0.85) as f32,
            building_resale: num(&e, "building_resale", 0.7) as f32,
            employee_rates: rates("employee"),
            employer_rates: rates("employer"),
            base_min: num(&cn, "base_min", 1500.0) as f32,
            base_max: num(&cn, "base_max", 30000.0) as f32,
            iit_threshold: num(&cn, "iit_threshold_monthly", 5000.0) as f32,
            vat_small: num(&cn, "vat_small", 0.01) as f32,
            cit_small: num(&cn, "cit_small_rate", 0.05) as f32,
            cit_rate: num(&cn, "cit_rate", 0.25) as f32,
            cit_small_limit: num(&cn, "cit_small_limit_yearly", 3000000.0) as f32,
            iit_brackets: brackets,
        }
    }

    pub fn unit(&self, id: &str) -> Option<&UnitDef> {
        self.units.iter().find(|u| u.id == id)
    }

    pub fn building(&self, id: &str) -> Option<&BuildingDef> {
        self.buildings.iter().find(|b| b.id == id)
    }

    pub fn role(&self, id: &str) -> Option<&RoleDef> {
        self.roles.iter().find(|r| r.id == id)
    }

    // ------------------------------------------------------------------------------------------ the economy

    /// Land prices rise slowly from the first game day: base x (1 + growth)^months.
    pub fn land_price(&self, u: &UnitDef, day: i32) -> i64 {
        (u.price as f64 * (1.0 + self.land_growth as f64).powf(day.max(0) as f64 / 30.0)) as i64
    }

    /// A level monthly payment for a loan (annuity).
    pub fn loan_payment(principal: i64, yearly_rate: f32, months: i32) -> i64 {
        if principal <= 0 || months <= 0 {
            return 0;
        }
        let r = yearly_rate as f64 / 12.0;
        if r <= 0.0 {
            return (principal + months as i64 - 1) / months as i64;
        }
        (principal as f64 * r / (1.0 - (1.0 + r).powf(-(months as f64)))).ceil() as i64
    }

    /// 五險一金 on one payment, the employee's share or the employer's (pro-rated for part of a month).
    pub fn social(&self, gross: i64, employer: bool, months: f32) -> Social {
        let mut s = Social::default();
        if gross <= 0 {
            return s;
        }
        let base = (gross as f64).clamp(self.base_min as f64 * months as f64, self.base_max as f64 * months as f64);
        let rates = if employer { &self.employer_rates } else { &self.employee_rates };
        let rate = |k: &str| rates.get(k).map(|v| *v as f64).unwrap_or(0.0);
        let yuan = |v: f64| round_half_from_zero(v);
        s.pension = yuan(base * rate("pension"));
        s.medical = yuan(base * (rate("medical") + rate("maternity")));
        s.unemployment = yuan(base * rate("unemployment"));
        s.housing = yuan(base * rate("housing_fund"));
        s.other = yuan(base * rate("injury"));
        s
    }

    /// Individual income tax on a year's taxable income so far (the cumulative withholding table).
    pub fn iit_yearly(&self, taxable: f64) -> i64 {
        if taxable <= 0.0 {
            return 0;
        }
        for (upper, rate, deduction) in &self.iit_brackets {
            if taxable <= *upper {
                return (taxable * rate - deduction).max(0.0) as i64;
            }
        }
        // Beyond the table: its last bracket (the table ends at a sentinel, so this is only for a malformed one).
        let (_, rate, deduction) = self.iit_brackets.last().copied().unwrap_or((0.0, 0.45, 181920.0));
        (taxable * rate - deduction) as i64
    }

    /// A year's one-time bonus taxed on its own: its twelfth picks the bracket, less the monthly quick deduction.
    pub fn bonus_tax(&self, bonus: i64) -> i64 {
        if bonus <= 0 {
            return 0;
        }
        let month = bonus as f64 / 12.0;
        for (upper, rate, deduction) in &self.iit_brackets {
            if month <= upper / 12.0 {
                return (round_half_from_zero(bonus as f64 * rate - deduction / 12.0) as f64).max(0.0) as i64;
            }
        }
        let (_, rate, deduction) = self.iit_brackets.last().copied().unwrap_or((0.0, 0.45, 181920.0));
        round_half_from_zero(bonus as f64 * rate - deduction / 12.0)
    }

    /// One payment of pay to someone with no year-to-date record: her 五險一金, then tax as if it were a month's pay.
    /// Returns (in hand, social, tax).
    pub fn net_pay(&self, gross: i64, months: f32) -> (i64, i64, i64) {
        let s = self.social(gross, false, months);
        let taxable = (gross - s.total()) as f64 - (self.iit_threshold * months) as f64;
        let m = months.max(0.01);
        let tax = if taxable > 0.0 { self.iit_yearly(taxable * 12.0 / m as f64) * ((m * 1000.0) as i64) / 12000 } else { 0 };
        (gross - s.total() - tax, s.total(), tax)
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Social {
    pub pension: i64,
    pub medical: i64,
    pub unemployment: i64,
    pub housing: i64,
    pub other: i64,
}

impl Social {
    pub fn total(&self) -> i64 {
        self.pension + self.medical + self.unemployment + self.housing + self.other
    }
}

/// Unreal's FMath::RoundHalfFromZero.
pub fn round_half_from_zero(v: f64) -> i64 {
    if v >= 0.0 {
        (v + 0.5).floor() as i64
    } else {
        (v - 0.5).ceil() as i64
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tuning::Tuning;

    #[test]
    fn market_and_taxes_from_the_data() {
        let p = Tuning::builtin().property;
        assert!(!p.units.is_empty() && !p.roles.is_empty() && !p.iit_brackets.is_empty(), "property.json parsed");
        assert_eq!(PropertyData::loan_payment(1200, 0.0, 12), 100);
        assert_eq!(PropertyData::loan_payment(0, 0.05, 240), 0);
        assert!(PropertyData::loan_payment(1_000_000, 0.05, 240) > 6000);
        let s = p.social(8000, false, 1.0);
        assert!(s.total() > 0 && s.housing > 0);
        // Tax is never negative and grows with income.
        assert_eq!(p.iit_yearly(-5.0), 0);
        assert!(p.iit_yearly(200_000.0) > p.iit_yearly(100_000.0));
        let (net, social, tax) = p.net_pay(20000, 1.0);
        assert_eq!(net + social + tax, 20000);
        assert!(p.bonus_tax(36000) >= 0);
    }
}
