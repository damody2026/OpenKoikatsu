//! ICU MessageFormat, the subset the game's text tables use (Docs/STREET_I18N_PLAN_20261014.md). The only formatter:
//! Unreal asks for its words through locale.rs "text"; Localization/icu_cases.json is its test.
//!
//! - `{name}`: the argument (a number prints plainly, without grouping: years and clock digits stay as they are)
//! - `{n, number}` grouped (1,234); `{n, number, ::STEMS}` with stems `.0` / `.00`... (fixed decimals), `integer`,
//!   `integer-width/*00` (at least that many digits), `sign-always`, `group-off`; and ours: `{amount, number, ::currency}`
//!   is the current region's money, the amount in its smallest unit; `{yuan_N}` / `{price_N}` (no argument needed) are
//!   N yuan of the game's base money / a shelf price of N yuan in the current region; `{currency_symbol}` its symbol
//! - `{n, plural, =0 {...} one {...} other {...}}` with `#` for the number (en: one is exactly 1; Chinese: other)
//! - `{x, select, a {...} other {...}}`
//! - apostrophes as in ICU's default mode: `''` is one quote; a quote before `{ } #` (or `|`) starts quoted text up to
//!   the next quote; any other quote is literal ("It's").
//!
//! A missing argument or a broken pattern shows the placeholder as written, never fails.
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq)]
pub enum Arg {
    Str(String),
    Int(i64),
    Num(f64),
}

impl Arg {
    fn number(&self) -> Option<f64> {
        match self {
            Arg::Int(i) => Some(*i as f64),
            Arg::Num(f) => Some(*f),
            Arg::Str(s) => s.trim().parse().ok(),
        }
    }
    fn plain(&self) -> String {
        match self {
            Arg::Str(s) => s.clone(),
            Arg::Int(i) => i.to_string(),
            Arg::Num(f) => {
                if f.fract() == 0.0 && f.abs() < 1e15 {
                    format!("{}", *f as i64)
                } else {
                    let s = format!("{f:.6}");
                    s.trim_end_matches('0').trim_end_matches('.').to_string()
                }
            }
        }
    }
}

pub type Args<'a> = HashMap<&'a str, Arg>;

pub fn format(pattern: &str, lang: &str, args: &Args) -> String {
    let chars: Vec<char> = pattern.chars().collect();
    let mut out = String::new();
    let mut i = 0;
    message(&chars, &mut i, lang, args, None, false, &mut out);
    out
}

/// Plural category of n in lang.
pub fn plural_category(lang: &str, n: f64) -> &'static str {
    if lang.starts_with("en") && n == 1.0 {
        "one"
    } else {
        "other"
    }
}

/// Copy text and arguments until the end, or until an unmatched '}' when nested (left for the caller).
fn message(c: &[char], i: &mut usize, lang: &str, args: &Args, hash: Option<f64>, nested: bool, out: &mut String) {
    while *i < c.len() {
        let ch = c[*i];
        match ch {
            '\'' => {
                let next = c.get(*i + 1).copied();
                if next == Some('\'') {
                    out.push('\'');
                    *i += 2;
                } else if matches!(next, Some('{') | Some('}') | Some('|')) || (next == Some('#') && hash.is_some()) {
                    *i += 1;
                    while *i < c.len() {
                        if c[*i] == '\'' {
                            if c.get(*i + 1) == Some(&'\'') {
                                out.push('\'');
                                *i += 2;
                                continue;
                            }
                            *i += 1;
                            break;
                        }
                        out.push(c[*i]);
                        *i += 1;
                    }
                } else {
                    out.push('\'');
                    *i += 1;
                }
            }
            '{' => {
                let start = *i;
                if !argument(c, i, lang, args, out) {
                    // Broken or missing: show it as written, up to its closing brace.
                    let end = matching(c, start).unwrap_or(c.len() - 1);
                    out.extend(&c[start..=end]);
                    *i = end + 1;
                }
            }
            '}' if nested => return,
            '#' if hash.is_some() => {
                out.push_str(&Arg::Num(hash.unwrap_or(0.0)).plain());
                *i += 1;
            }
            _ => {
                out.push(ch);
                *i += 1;
            }
        }
    }
}

/// Ours: `yuan_N` is N yuan (the game's base money) in the current region, `price_N` a shelf price of N yuan there
/// (region.rs); a message can name a fixed amount without the code passing it ("{yuan_4000, number, ::currency}").
fn base_amount(name: &str) -> Option<Arg> {
    if let Some(n) = name.strip_prefix("yuan_").and_then(|n| n.parse::<i64>().ok()) {
        return Some(Arg::Int(crate::region::amount("cash", n)));
    }
    name.strip_prefix("price_").and_then(|n| n.parse::<i64>().ok()).map(|n| Arg::Int(crate::region::price(n)))
}

fn matching(c: &[char], open: usize) -> Option<usize> {
    let mut depth = 0;
    let mut j = open;
    while j < c.len() {
        match c[j] {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(j);
                }
            }
            _ => {}
        }
        j += 1;
    }
    None
}

fn skip_ws(c: &[char], i: &mut usize) {
    while *i < c.len() && c[*i].is_whitespace() {
        *i += 1;
    }
}

fn word(c: &[char], i: &mut usize) -> String {
    skip_ws(c, i);
    let mut s = String::new();
    while *i < c.len() && !c[*i].is_whitespace() && !matches!(c[*i], ',' | '{' | '}') {
        s.push(c[*i]);
        *i += 1;
    }
    skip_ws(c, i);
    s
}

/// One `{...}` at c[*i]; false (and nothing written) when it cannot be formatted.
fn argument(c: &[char], i: &mut usize, lang: &str, args: &Args, out: &mut String) -> bool {
    let save = *i;
    *i += 1;
    let name = word(c, i);
    let base_arg = if name == "currency_symbol" { Some(Arg::Str(crate::locale::currency(crate::tr::region()).symbol.to_string())) } else { base_amount(&name) };
    let Some(value) = args.get(name.as_str()).or(base_arg.as_ref()) else {
        *i = save;
        return false;
    };
    if c.get(*i) == Some(&'}') {
        *i += 1;
        out.push_str(&value.plain());
        return true;
    }
    if c.get(*i) != Some(&',') {
        *i = save;
        return false;
    }
    *i += 1;
    let kind = word(c, i);
    match kind.as_str() {
        "number" => {
            let mut style = String::new();
            if c.get(*i) == Some(&',') {
                *i += 1;
                while *i < c.len() && c[*i] != '}' {
                    style.push(c[*i]);
                    *i += 1;
                }
            }
            if c.get(*i) != Some(&'}') {
                *i = save;
                return false;
            }
            *i += 1;
            match value.number() {
                Some(n) => out.push_str(&number(n, style.trim())),
                None => out.push_str(&value.plain()),
            }
            true
        }
        "plural" | "select" => {
            if c.get(*i) != Some(&',') {
                *i = save;
                return false;
            }
            *i += 1;
            let n = value.number();
            let wanted_exact = n.map(|n| format!("={}", Arg::Num(n).plain()));
            let category = if kind == "plural" { n.map(|n| plural_category(lang, n)).unwrap_or("other") } else { "" };
            let key = if kind == "select" { value.plain() } else { String::new() };
            // Pick the option: an exact =N, then the category (plural) or the value (select), then other.
            let mut options: Vec<(String, usize, usize)> = vec![];
            loop {
                skip_ws(c, i);
                if c.get(*i) == Some(&'}') {
                    *i += 1;
                    break;
                }
                let sel = word(c, i);
                if c.get(*i) != Some(&'{') || sel.is_empty() {
                    *i = save;
                    return false;
                }
                let Some(end) = matching(c, *i) else {
                    *i = save;
                    return false;
                };
                options.push((sel, *i + 1, end));
                *i = end + 1;
            }
            let pick = options
                .iter()
                .find(|o| wanted_exact.as_deref() == Some(o.0.as_str()))
                .or_else(|| options.iter().find(|o| if kind == "plural" { o.0 == category } else { o.0 == key }))
                .or_else(|| options.iter().find(|o| o.0 == "other"));
            if let Some((_, s, e)) = pick {
                let mut j = *s;
                let inner = &c[..*e];
                message(inner, &mut j, lang, args, if kind == "plural" { n } else { None }, true, out);
            }
            true
        }
        _ => {
            *i = save;
            false
        }
    }
}

/// A number with ICU skeleton stems (the subset above): grouped unless `group-off`, up to 3 decimals unless fixed.
pub fn number(n: f64, style: &str) -> String {
    let stems: Vec<&str> = style.trim_start_matches("::").split_whitespace().collect();
    if stems.first() == Some(&"currency") {
        // Ours, not ICU's: the current region's money, the amount in its smallest unit ("NT$1,200", "¥45", "$1.99").
        return crate::locale::currency(crate::tr::region()).format(n.round() as i64);
    }
    let mut decimals: Option<usize> = None;
    let mut min_int = 1;
    let mut sign = false;
    let mut group = true;
    for s in &stems {
        if let Some(d) = s.strip_prefix('.') {
            decimals = Some(d.chars().filter(|c| *c == '0').count());
        } else if *s == "integer" {
            decimals = Some(0);
        } else if let Some(w) = s.strip_prefix("integer-width/") {
            // zero-padded digits (dates, clocks, codes) are never grouped: 2010, not 2,010
            min_int = w.chars().filter(|c| *c == '0').count().max(1);
            group = false;
        } else if *s == "sign-always" {
            sign = true;
        } else if *s == "group-off" {
            group = false;
        }
    }
    let s = match decimals {
        Some(d) => format!("{:.*}", d, n.abs()),
        None => {
            let t = format!("{:.3}", n.abs());
            t.trim_end_matches('0').trim_end_matches('.').to_string()
        }
    };
    let (int, frac) = match s.split_once('.') {
        Some((a, b)) => (a.to_string(), Some(b.to_string())),
        None => (s.clone(), None),
    };
    let mut int = format!("{:0>width$}", int, width = min_int);
    if group {
        let mut g = String::new();
        for (k, ch) in int.chars().enumerate() {
            if k > 0 && (int.len() - k) % 3 == 0 {
                g.push(',');
            }
            g.push(ch);
        }
        int = g;
    }
    let negative = n < 0.0 && (int.chars().any(|c| c.is_ascii_digit() && c != '0') || frac.as_deref().is_some_and(|f| f.chars().any(|c| c != '0')));
    let sign_str = if negative { "-" } else if sign { "+" } else { "" };
    match frac {
        Some(f) => format!("{sign_str}{int}.{f}"),
        None => format!("{sign_str}{int}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::Value;

    #[test]
    fn shared_cases() {
        let cases: Vec<Value> = serde_json::from_str(include_str!("../../../Content/IndependentResources/StreetSocialData/Localization/icu_cases.json")).unwrap();
        for case in cases {
            let mut args = Args::new();
            for (k, v) in case["args"].as_object().unwrap() {
                let a = if let Some(i) = v.as_i64() { Arg::Int(i) } else if let Some(f) = v.as_f64() { Arg::Num(f) } else { Arg::Str(v.as_str().unwrap().to_string()) };
                args.insert(k.as_str(), a);
            }
            let got = format(case["pattern"].as_str().unwrap(), case["lang"].as_str().unwrap(), &args);
            assert_eq!(got, case["expect"].as_str().unwrap(), "pattern {}", case["pattern"]);
        }
    }
}
