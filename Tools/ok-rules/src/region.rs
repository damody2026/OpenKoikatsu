//! Regions (Docs/STREET_I18N_PLAN_20261014.md, P2): Taiwan, mainland China and a US college town, 2010. The game's own
//! amounts (catalog.json, property.json, the rules' constants) are written in yuan, the base region (Regions/CN). The
//! region a request plays in gets them in its own money, in its smallest unit (yuan, NT$, cents):
//!
//! - `price(base)`: a shelf price, converted by the region's goods factor and snapped to its usual prices (its ladder):
//!   ¥17 coffee -> NT$85 -> $4.49... a localized price, not an exchange rate
//! - `amount(kind, base)`: any other amount (wage, salary, rent, property, cash) by that kind's factor, rounded to a
//!   number people would use there
//! - `fixed(key, base)`: an amount the region has its own number for ([fixed]: tuition, phone fee...), else `amount`
//! - `convert(amount, from, to)`: a save's money moved to another region (by the cash factor, not rounded)
//!
//! Regions/<code>/region.toml beside the rules folder; copies are built into the DLL. The region of the request is the
//! thread's (`tr::region`, set by `Rules::eval`).
use serde::Deserialize;
use serde_json::Value;
use std::collections::HashMap;
use std::path::Path;
use std::sync::{Arc, RwLock};

macro_rules! region_file {
    ($code:literal) => {
        ($code, include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Regions/", $code, "/region.toml")))
    };
}
const BUILTIN: [(&str, &str); 3] = [region_file!("TW"), region_file!("CN"), region_file!("US")];
pub const BASE: &str = "CN";

#[derive(Debug, Clone, Deserialize, Default)]
pub struct RegionData {
    pub code: String,
    pub currency: String,
    pub symbol: String,
    pub digits: u32,
    #[serde(default)]
    pub base: bool,
    #[serde(default)]
    pub ladder: Vec<f64>,
    #[serde(default)]
    pub factor: HashMap<String, f64>,
    #[serde(default)]
    pub fixed: HashMap<String, f64>,
    /// The names his company's staff are given here (property.json's "names" shape: family, given).
    #[serde(default)]
    pub names: Option<toml::Value>,
    /// Payroll and taxes in property.json's "china" shape, amounts in this region's major unit.
    #[serde(default)]
    pub tax: Option<toml::Value>,
}

impl RegionData {
    fn unit(&self) -> f64 {
        10f64.powi(self.digits as i32)
    }
    fn factor_of(&self, kind: &str) -> f64 {
        self.factor.get(kind).or_else(|| self.factor.get("cash")).copied().unwrap_or(1.0)
    }
    /// Major units (dollars) to the smallest unit kept in saves (cents).
    pub fn minor(&self, major: f64) -> i64 {
        (major * self.unit()).round() as i64
    }

    pub fn price(&self, base: i64) -> i64 {
        if self.base || base <= 0 {
            return base;
        }
        let major = base as f64 * self.factor_of("goods");
        self.minor(snap(major, &self.ladder))
    }

    pub fn amount(&self, kind: &str, base: i64) -> i64 {
        if self.base || base == 0 {
            return base;
        }
        let major = base as f64 * self.factor_of(kind);
        self.minor(nice(major, self.digits))
    }

    pub fn fixed(&self, key: &str, base: i64) -> i64 {
        if self.base {
            return base;
        }
        match self.fixed.get(key) {
            Some(major) => self.minor(*major),
            None => self.amount("cash", base),
        }
    }
}

/// The nearest usual price (within the ladder's range); beyond it a round number.
fn snap(major: f64, ladder: &[f64]) -> f64 {
    let (Some(first), Some(last)) = (ladder.first(), ladder.last()) else { return nice(major, 2) };
    if major < *first {
        return *first;
    }
    if major > *last * 1.2 {
        return nice(major, 0);
    }
    // Nearest by ratio (¥17 -> 4.25 is nearer 4.49 than 3.99 by ratio too; prices are compared in proportion).
    *ladder.iter().min_by(|a, b| ((*a / major).ln().abs()).partial_cmp(&(*b / major).ln().abs()).unwrap()).unwrap()
}

/// A number people would use: two significant figures (three from 1,000 up), never below one of the smallest unit.
fn nice(major: f64, digits: u32) -> f64 {
    if major == 0.0 {
        return 0.0;
    }
    let sign = major.signum();
    let a = major.abs();
    let figures = if a >= 1000.0 { 3 } else { 2 };
    let step = 10f64.powi(a.log10().floor() as i32 + 1 - figures);
    let floor = 10f64.powi(-(digits as i32));
    sign * ((a / step).round() * step).max(floor)
}

#[derive(Debug, Clone, Default)]
pub struct Regions {
    pub by_code: HashMap<String, Arc<RegionData>>,
    pub sources: Vec<String>,
}

impl Regions {
    /// Regions/<code>/region.toml beside the rules folder; the built-in copy for a region without one.
    pub fn load(rules_dir: Option<&Path>) -> Result<Regions, String> {
        let root = rules_dir.and_then(Path::parent).map(|d| d.join("Regions"));
        let mut out = Regions::default();
        for (code, builtin) in BUILTIN {
            let file = root.as_ref().map(|r| r.join(code).join("region.toml")).filter(|f| f.is_file());
            let (text, source) = match &file {
                Some(f) => (std::fs::read_to_string(f).map_err(|e| format!("{}: {e}", f.display()))?, f.display().to_string()),
                None => (builtin.to_string(), format!("builtin:{code}")),
            };
            let data: RegionData = toml::from_str(&text).map_err(|e| format!("region {code}: {e}"))?;
            out.sources.push(source);
            out.by_code.insert(code.to_string(), Arc::new(data));
        }
        Ok(out)
    }

    pub fn get(&self, code: &str) -> Arc<RegionData> {
        self.by_code.get(code).or_else(|| self.by_code.get(BASE)).cloned().unwrap_or_default()
    }
}

static REGIONS: RwLock<Option<Arc<Regions>>> = RwLock::new(None);

pub fn install(regions: &Regions) {
    if let Ok(mut r) = REGIONS.write() {
        *r = Some(Arc::new(regions.clone()));
    }
}

fn all() -> Arc<Regions> {
    if let Some(r) = REGIONS.read().ok().and_then(|r| r.clone()) {
        return r;
    }
    let r = Regions::load(None).expect("built-in regions parse");
    install(&r);
    REGIONS.read().ok().and_then(|r| r.clone()).expect("regions installed")
}

/// The region of the current request.
pub fn here() -> Arc<RegionData> {
    all().get(crate::tr::region())
}

pub fn get(code: &str) -> Arc<RegionData> {
    all().get(code)
}

pub fn price(base: i64) -> i64 {
    here().price(base)
}

pub fn amount(kind: &str, base: i64) -> i64 {
    here().amount(kind, base)
}

pub fn fixed(key: &str, base: i64) -> i64 {
    here().fixed(key, base)
}

/// A save's amount moved from one region's money to another's (cash factor; not rounded, sums stay sums).
pub fn convert(amount: i64, from: &str, to: &str) -> i64 {
    if from == to {
        return amount;
    }
    let (a, b) = (get(from), get(to));
    let base_yuan = amount as f64 / a.unit() / a.factor_of("cash");
    (base_yuan * b.factor_of("cash") * b.unit()).round() as i64
}

/// The heroines' profiles (made by ok-mind, money in yuan like all base data) in the request's region: every
/// "profile" object's money (cash, bank, and the amounts of income, fixed costs, saving goals and money needs).
pub fn localize_request(request: &mut Value) {
    let reg = here();
    if reg.base {
        return;
    }
    fn money(m: &mut Value, reg: &RegionData) {
        for k in ["cash", "bank"] {
            if let Some(n) = m.get(k).and_then(Value::as_f64) {
                m[k] = serde_json::json!(reg.amount("cash", n.round() as i64));
            }
        }
        for list in ["income", "fixed_costs", "saving_goals", "money_needs"] {
            if let Some(a) = m.get_mut(list).and_then(Value::as_array_mut) {
                for x in a {
                    if let Some(n) = x.get("amount").and_then(Value::as_f64) {
                        x["amount"] = serde_json::json!(reg.amount(if list == "income" { "salary" } else { "cash" }, n.round() as i64));
                    }
                }
            }
        }
    }
    fn walk(v: &mut Value, reg: &RegionData) {
        match v {
            Value::Object(map) => {
                for (k, x) in map.iter_mut() {
                    if (k == "profile" || k == "profiles") && x.is_object() {
                        if let Some(m) = x.get_mut("money") {
                            money(m, reg);
                        } else {
                            walk(x, reg); // profiles: id -> profile
                        }
                    } else {
                        walk(x, reg);
                    }
                }
            }
            Value::Array(a) => a.iter_mut().for_each(|x| walk(x, reg)),
            _ => {}
        }
    }
    walk(request, &reg);
}

/// Keys that hold money in a save (relationships.json), wherever they sit in the parts that hold money.
const MONEY_KEYS: &[&str] = &[
    "cash", "bank", "debt", "owes_player", "balance", "limit", "min_due", "statement", "monthly", "owed", "taken_this_year",
    "shortfall", "total", "phone_arrears", "capital", "last_profit", "rent", "deposit", "price", "pay", "ask", "principal",
    "returned", "amount", "cost", "build_paid", "loan", "loan_payment", "lease_rent", "value", "fee", "wage", "budget",
    "profit", "income", "spent", "stake", "payout", "interest", "demand_interest", "gift", "wealth", "salary", "bonus",
    "dividend", "tax", "fine", "funds", "allowance", "tuition",
];
/// Parts of the save without money (feelings, places, times, the calendar...): left alone.
const NO_MONEY: &[&str] = &["academics", "rumors", "bonds", "calendar", "topic_inventory", "stepup", "appointments", "blacklist", "schema", "feelings_model"];

fn convert_tree(v: &mut Value, key: &str, from: &str, to: &str) {
    match v {
        Value::Object(map) => {
            for (k, x) in map.iter_mut() {
                if NO_MONEY.contains(&k.as_str()) {
                    continue;
                }
                convert_tree(x, k, from, to);
            }
        }
        Value::Array(a) => {
            // [day, amount] pairs (net worth over time): the amount only.
            if key == "net_worth" {
                for p in a.iter_mut() {
                    if let Some(pair) = p.as_array_mut().filter(|p| p.len() == 2) {
                        if let Some(n) = pair[1].as_f64() {
                            pair[1] = serde_json::json!(convert(n.round() as i64, from, to));
                        }
                    }
                }
                return;
            }
            for x in a.iter_mut() {
                convert_tree(x, key, from, to);
            }
        }
        Value::Number(n) if MONEY_KEYS.contains(&key) => {
            if let Some(f) = n.as_f64() {
                *v = if n.is_i64() || n.is_u64() { serde_json::json!(convert(f.round() as i64, from, to)) } else { serde_json::json!(f * convert(1_000_000, from, to) as f64 / 1_000_000.0) };
            }
        }
        _ => {}
    }
}

/// A whole save moved to another region's money: every money field (MONEY_KEYS) outside the parts without money; a
/// heroine's feelings and the rest are left alone. Her own wealth as she sees it ("perceived_wealth") is a feeling.
pub fn convert_save(save: &mut Value, from: &str, to: &str) {
    if from == to {
        return;
    }
    if let Some(rels) = save.get_mut("relations").and_then(Value::as_array_mut) {
        for r in rels {
            if let Some(m) = r.get_mut("money") {
                for k in ["cash", "bank", "debt", "owes_player"] {
                    if let Some(n) = m.get(k).and_then(Value::as_f64) {
                        m[k] = serde_json::json!(convert(n.round() as i64, from, to));
                    }
                }
            }
        }
    }
    let rels = save.as_object_mut().and_then(|m| m.remove("relations"));
    convert_tree(save, "", from, to);
    if let Some(r) = rels {
        save["relations"] = r;
    }
    save["region"] = serde_json::json!(to);
    save["currency"] = serde_json::json!(get(to).currency.clone());
}

/// The rules for the scene (`region_info`): the region's money and its converted prices and wages for the catalog the
/// scene shows; `region_convert` moves a list of amounts between regions; `save_convert` a whole save.
pub fn eval(catalog_base: &crate::catalog::Catalog, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    match kind {
        "region_convert" => {
            let from = r.get("from").and_then(Value::as_str).unwrap_or(BASE);
            let to = r.get("to").and_then(Value::as_str).unwrap_or(BASE);
            let amounts: Vec<i64> = r.get("amounts").and_then(Value::as_array).map(|a| a.iter().map(|v| convert(v.as_f64().unwrap_or(0.0).round() as i64, from, to)).collect()).unwrap_or_default();
            Some(Ok(serde_json::json!({"amounts": amounts})))
        }
        // The scene's own amounts (start cash, default boxes, ticket prices...): [{kind, base}] in the request's region;
        // kind "price" is a shelf price, a [fixed] key is that region's own number, else a factor kind (cash, wage...).
        "region_amount" => {
            let reg = here();
            let out: Vec<i64> = r.get("items").and_then(Value::as_array).map(|a| a.iter().map(|x| {
                let kind = x.get("kind").and_then(Value::as_str).unwrap_or("cash");
                let base = x.get("base").and_then(Value::as_f64).unwrap_or(0.0).round() as i64;
                if kind == "price" { reg.price(base) } else if reg.fixed.contains_key(kind) { reg.fixed(kind, base) } else { reg.amount(kind, base) }
            }).collect()).unwrap_or_default();
            Some(Ok(serde_json::json!({"amounts": out})))
        }
        // ok-mind's finance (lottery, bets, brokerage deposits) keeps its wallet in yuan: what the scene sends it
        // ("to_fin") and what comes back ("from_fin", results and events) cross between that and the region's money.
        // Only the wallet amounts of the operations that touch the wallet; market prices stay in their market's money.
        "fin_wallet" => {
            let dir = r.get("dir").and_then(Value::as_str).unwrap_or("from_fin");
            let op = r.get("op").and_then(Value::as_str).unwrap_or("");
            let mut v = r.get("value").cloned().unwrap_or(Value::Null);
            let here_code = crate::tr::region();
            let (from, to) = if dir == "to_fin" { (here_code, BASE) } else { (BASE, here_code) };
            let keys: &[&str] = match (dir, op) {
                ("to_fin", "deposit") => &["twd"],
                ("to_fin", "lotto_buy") => &["cost", "price"],
                ("to_fin", "scratch_buy") => &["price"],
                ("to_fin", "sports_bet") => &["stake"],
                ("to_fin", "heroine_invest") => &["deposit", "from_cash", "from_bank", "amount"],
                ("to_fin", "npc_scratch") => &["budget"],
                ("from_fin", "deposit" | "withdraw") => &["twd", "fee"],
                ("from_fin", "claim" | "scratch_reveal" | "npc_scratch") => &["amount", "prize", "paid", "spent", "won"],
                ("from_fin", "scratch_buy" | "lotto_buy" | "sports_bet") => &["price", "cost", "stake"],
                ("from_fin", "tickets") => &["cost", "price", "prize", "amount", "payout", "stake"],
                ("from_fin", "payout") => &["amount"],
                ("from_fin", "sports_settled") => &["payout", "stake"],
                ("from_fin", "heroine_invest") => &["deposit"],
                _ => &[],
            };
            fn walk(v: &mut Value, keys: &[&str], from: &str, to: &str) {
                match v {
                    Value::Object(m) => {
                        for (k, x) in m.iter_mut() {
                            if keys.contains(&k.as_str()) {
                                if let Some(n) = x.as_f64() {
                                    *x = serde_json::json!(convert(n.round() as i64, from, to));
                                    continue;
                                }
                            }
                            walk(x, keys, from, to);
                        }
                    }
                    Value::Array(a) => a.iter_mut().for_each(|x| walk(x, keys, from, to)),
                    _ => {}
                }
            }
            if from != to {
                walk(&mut v, keys, from, to);
            }
            Some(Ok(serde_json::json!({"value": v})))
        }
        "save_convert" => {
            let from = r.get("from").and_then(Value::as_str).unwrap_or(BASE).to_string();
            let to = r.get("to").and_then(Value::as_str).unwrap_or(BASE).to_string();
            let mut save = r.get("save").cloned().unwrap_or(Value::Null);
            if !save.is_object() {
                return Some(Err("save_convert: save must be an object".into()));
            }
            convert_save(&mut save, &from, &to);
            Some(Ok(serde_json::json!({"save": save})))
        }
        "region_info" => {
            let reg = here();
            let items: serde_json::Map<String, Value> = catalog_base
                .items
                .iter()
                .map(|i| (i.id.clone(), Value::Object(i.price.iter().map(|(size, p)| (size.clone(), serde_json::json!(reg.price(*p as i64)))).collect())))
                .collect();
            let jobs: serde_json::Map<String, Value> = catalog_base.jobs.iter().map(|(id, (_, wage))| (id.clone(), serde_json::json!(reg.amount("wage", *wage as i64)))).collect();
            Some(Ok(serde_json::json!({"region": reg.code, "currency": reg.currency, "symbol": reg.symbol, "digits": reg.digits, "items": items, "jobs": jobs})))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn localized_not_exchanged() {
        let r = Regions::load(None).unwrap();
        let (tw, cn, us) = (r.get("TW"), r.get("CN"), r.get("US"));
        assert_eq!(cn.price(17), 17, "the base region is the game's own numbers");
        assert_eq!(tw.price(17), 85);
        assert_eq!(us.price(17), 449, "$4.49 in cents");
        assert_eq!(us.price(4), 99);
        assert_eq!(tw.fixed("tuition", 2500), 25000);
        assert_eq!(us.fixed("phone_fee", 58), 4999);
        assert_eq!(us.amount("wage", 20), 900, "$9.00 an hour");
        assert_eq!(tw.amount("salary", 2400), 25900);
        assert_eq!(nice(1234.0, 0), 1230.0);
        let mut save = serde_json::json!({"wallet": {"cash": 1000, "bank": 0}, "relations": [{"money": {"cash": 400, "bank": 3194, "perceived_wealth": 21.5}, "favor": 80}],
            "banking": {"card": {"limit": 3000, "due_day": 12}, "net_worth": [[3, 2000]]}, "calendar": {"day": 4, "minute": 1020}, "region": "CN"});
        convert_save(&mut save, "CN", "US");
        assert_eq!(save["wallet"]["cash"], 40000, "¥1,000 -> $400.00 by the cash factor");
        assert_eq!(save["relations"][0]["money"]["bank"], 127760);
        assert_eq!(save["relations"][0]["favor"], 80, "feelings are not money");
        assert_eq!(save["banking"]["card"]["due_day"], 12);
        assert_eq!(save["banking"]["net_worth"][0], serde_json::json!([3, 80000]));
        assert_eq!(save["calendar"]["minute"], 1020);
        assert_eq!((save["region"].as_str(), save["currency"].as_str()), (Some("US"), Some("USD")));
        let back = convert(convert(10000, "CN", "US"), "US", "CN");
        assert!((back - 10000).abs() <= 1);
    }
}
