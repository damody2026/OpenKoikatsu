//! The rules' own sentences in the player's language (Docs/STREET_I18N_PLAN_20261014.md, P1): notices, news, memories
//! and ledger notes. A sentence is a message id in the text tables (Localization/<lang>/rules_text.toml and the other
//! tables, ICU MessageFormat, icu.rs): `tr::t("bank.deposit_min")` when it has no arguments (a `&'static str`, like the
//! literal it replaced), `tr::m("bank.matured", &[("amount", tr::a(&x)), ("months", tr::a(&m))])` with named ones.
//! A language without the message uses zh-TW's; a missing id shows the id.
//!
//! The language and region come from the request (`scope`, set for every request by `Rules::eval`); money goes in as an
//! argument already formatted by `money` (the region's currency: "NT$1,200", "¥45", "$1.99").
use crate::icu::{self, Arg};
use crate::locale::{Texts, FALLBACK_LANG, LANGS, REGIONS};
use std::cell::Cell;
use std::collections::HashMap;
use std::sync::RwLock;

/// A message: its pattern, and its text formatted without arguments (what `t` hands out).
type Table = HashMap<&'static str, (&'static str, &'static str)>;

static TABLES: RwLock<Option<HashMap<&'static str, &'static Table>>> = RwLock::new(None);
/// zh-TW text (formatted without arguments) -> its id: labels kept as Chinese data in Unreal's static tables are
/// looked up by their text (`text`).
static BY_ZH: RwLock<Option<&'static HashMap<&'static str, &'static str>>> = RwLock::new(None);

thread_local! {
    static LANG: Cell<&'static str> = const { Cell::new(FALLBACK_LANG) };
    static REGION: Cell<&'static str> = const { Cell::new("CN") };
}

fn leak(s: &str) -> &'static str {
    Box::leak(s.to_string().into_boxed_str())
}

/// Take the messages of the loaded text tables. They live for the whole run (a reload leaks the old ones, a few
/// thousand short strings), so `t` can hand out `&'static str`.
pub fn install(texts: &Texts) {
    let mut all = HashMap::new();
    for lang in LANGS {
        let mut table = Table::new();
        for (k, pattern) in texts.table(lang, "") {
            let plain = icu::format(&pattern, lang, &icu::Args::new());
            table.insert(leak(&k), (leak(&pattern), leak(&plain)));
        }
        all.insert(lang, &*Box::leak(Box::new(table)));
    }
    let by_zh: HashMap<&'static str, &'static str> = all.get(FALLBACK_LANG).map(|t| t.iter().map(|(k, (_, plain))| (*plain, *k)).collect()).unwrap_or_default();
    if let Ok(mut z) = BY_ZH.write() {
        *z = Some(Box::leak(Box::new(by_zh)));
    }
    if let Ok(mut t) = TABLES.write() {
        *t = Some(all);
    }
}

fn with_table<R>(f: impl FnOnce(&Table) -> R) -> R {
    if TABLES.read().map(|t| t.is_none()).unwrap_or(true) {
        install(&Texts::builtin()); // tests and tools: the tables built into the DLL
    }
    let tables = TABLES.read().expect("tr tables");
    let all = tables.as_ref().expect("tr tables installed");
    f(all.get(lang()).or_else(|| all.get(FALLBACK_LANG)).expect("zh-TW table"))
}

/// Use this language and region on this thread until the guard is dropped.
pub struct Scope {
    lang: &'static str,
    region: &'static str,
}

pub fn scope(lang: &str, region: &str) -> Scope {
    let l = LANGS.iter().copied().find(|x| *x == lang).unwrap_or(FALLBACK_LANG);
    let g = REGIONS.iter().copied().find(|x| *x == region).unwrap_or("CN");
    Scope { lang: LANG.with(|c| c.replace(l)), region: REGION.with(|c| c.replace(g)) }
}

impl Drop for Scope {
    fn drop(&mut self) {
        LANG.with(|c| c.set(self.lang));
        REGION.with(|c| c.set(self.region));
    }
}

pub fn lang() -> &'static str {
    LANG.with(Cell::get)
}

pub fn region() -> &'static str {
    REGION.with(Cell::get)
}

/// A message without arguments in the current language.
pub fn t(id: &'static str) -> &'static str {
    with_table(|t| t.get(id).map(|m| m.1)).unwrap_or(id)
}

/// A name by id in the current language (names.toml: "ui.name.dept.csie"), else the data's own name.
pub fn lookup(key: &str, fallback: &'static str) -> &'static str {
    with_table(|t| t.get(key).map(|m| m.1)).unwrap_or(fallback)
}

/// Unreal's words (OKT / OKMsg): a message id, or a zh-TW text that is a message, in the current language with these
/// arguments; anything else is formatted as it is (a missing id shows itself).
pub fn text(id: &str, args: &icu::Args) -> String {
    // id.one / id.other variants, by the argument n (as tn).
    let variant = args.get("n").and_then(|n| match n {
        Arg::Int(i) => Some(*i as f64),
        Arg::Num(f) => Some(*f),
        Arg::Str(s) => s.parse().ok(),
    }).map(|n| format!("{id}.{}", if n == 1.0 { "one" } else { "other" }));
    let pattern = variant.as_deref().and_then(|v| with_table(|t| t.get(v).map(|m| m.0))).or_else(|| with_table(|t| t.get(id).map(|m| m.0))).or_else(|| {
        let key = BY_ZH.read().ok().and_then(|z| z.and_then(|z| z.get(id).copied()))?;
        with_table(|t| t.get(key).map(|m| m.0))
    });
    icu::format(pattern.unwrap_or(id), lang(), args)
}

/// A message with named arguments in the current language.
pub fn m(id: &'static str, args: &[(&str, Arg)]) -> String {
    let pattern = with_table(|t| t.get(id).map(|m| m.0)).unwrap_or(id);
    let map: icu::Args = args.iter().map(|(k, v)| (*k, v.clone())).collect();
    icu::format(pattern, lang(), &map)
}

/// Whether what the player said has one of the words of a hear list ("hear.invite": "join us|come over|..."), in the
/// current language; matched in lower case.
pub fn heard(said: &str, id: &'static str) -> bool {
    // One script on both sides: callers hand in Simplified text, the Traditional table writes 你拿著.
    let cn = |x: &str| zhconv::zhconv(&x.to_lowercase(), zhconv::Variant::ZhCN);
    let said = cn(said);
    t(id).split('|').map(str::trim).filter(|w| !w.is_empty()).any(|w| said.contains(&cn(w)))
}

/// An argument for `m`: numbers stay numbers (for plural and number formats), anything else is its text.
pub fn a<T: ToArg + ?Sized>(v: &T) -> Arg {
    v.to_arg()
}

pub trait ToArg {
    fn to_arg(&self) -> Arg;
}
macro_rules! int_arg {
    ($($t:ty),*) => { $(impl ToArg for $t { fn to_arg(&self) -> Arg { Arg::Int(*self as i64) } })* };
}
int_arg!(i64, i32, i16, i8, u64, u32, u16, u8, usize, isize);
impl ToArg for f64 {
    fn to_arg(&self) -> Arg {
        Arg::Num(*self)
    }
}
impl ToArg for f32 {
    fn to_arg(&self) -> Arg {
        Arg::Num(*self as f64)
    }
}
impl ToArg for str {
    fn to_arg(&self) -> Arg {
        Arg::Str(self.to_string())
    }
}
impl ToArg for String {
    fn to_arg(&self) -> Arg {
        Arg::Str(self.clone())
    }
}
impl ToArg for char {
    fn to_arg(&self) -> Arg {
        Arg::Str(self.to_string())
    }
}
impl<T: ToArg + ?Sized> ToArg for &T {
    fn to_arg(&self) -> Arg {
        (**self).to_arg()
    }
}
impl ToArg for std::borrow::Cow<'_, str> {
    fn to_arg(&self) -> Arg {
        Arg::Str(self.to_string())
    }
}

/// An amount (the region's smallest unit) as money in the current region.
pub fn money<T: Amount>(amount: T) -> String {
    crate::locale::currency(region()).format(amount.amount())
}

pub trait Amount {
    fn amount(self) -> i64;
}
macro_rules! amount_int {
    ($($t:ty),*) => { $(impl Amount for $t { fn amount(self) -> i64 { self as i64 } })* };
}
amount_int!(i64, i32, u32, u64, usize, isize);
impl Amount for f64 {
    fn amount(self) -> i64 {
        self.round() as i64
    }
}
impl Amount for f32 {
    fn amount(self) -> i64 {
        self.round() as i64
    }
}
impl<T: Amount + Copy> Amount for &T {
    fn amount(self) -> i64 {
        (*self).amount()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn messages_by_id() {
        let _s = scope("en", "US");
        assert_eq!(money(199), "$1.99");
        assert_eq!(t("no.such.message"), "no.such.message");
        assert_eq!(m("no.such.message", &[("x", a(&1))]), "no.such.message");
        let args: icu::Args = [("p", Arg::Int(129900))].into_iter().collect();
        assert_eq!(icu::format("{p, number, ::currency}", "en", &args), "$1,299.00");
        assert!(heard("Come over here", "hear.invite") && !heard("hello", "hear.invite"));
        let _t = scope("zh-TW", "TW");
        assert_eq!(icu::format("{p, number, ::currency}", "zh-TW", &args), "NT$129,900");
    }
}
