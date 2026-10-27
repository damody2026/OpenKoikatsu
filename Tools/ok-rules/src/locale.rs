//! Languages and regions (Docs/STREET_I18N_PLAN_20261014.md). The language (zh-TW, zh-CN, en) picks the words; the
//! region (TW, CN, US) picks the setting, the money and the prices. Every request carries both ("lang", "region"; set by
//! Unreal for all requests), and a rule that writes text a player or a heroine reads asks for it here by key.
//!
//! Text tables: StreetSocialData/Localization/<lang>/*.toml, nested tables flattened to dotted keys
//! ([bluff] played = "..." -> "bluff.played"); copies are built into the DLL. A missing key falls back to zh-TW, then
//! to the key itself (so a gap shows instead of vanishing). Texts are ICU MessageFormat (icu.rs): "{name}",
//! "{n, plural, one {# card} other {# cards}}", "{x, select, ...}", "{v, number, ::.0}".
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::path::Path;

pub const LANGS: [&str; 3] = ["zh-TW", "zh-CN", "en"];
pub const REGIONS: [&str; 3] = ["TW", "CN", "US"];
pub const FALLBACK_LANG: &str = "zh-TW";

macro_rules! tables {
    ($lang:literal) => {
        ($lang, [
            include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Localization/", $lang, "/rules.toml")),
            include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Localization/", $lang, "/ui.toml")),
            include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Localization/", $lang, "/rules_text.toml")),
            include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Localization/", $lang, "/ui_text.toml")),
            include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Localization/", $lang, "/mind_text.toml")),
            include_str!(concat!("../../../Content/IndependentResources/StreetSocialData/Localization/", $lang, "/names.toml")),
        ])
    };
}
const BUILTIN: [(&str, [&str; 6]); 3] = [tables!("zh-TW"), tables!("zh-CN"), tables!("en")];

/// The language a request asks for (an unknown or missing one is zh-TW).
pub fn lang(r: &Value) -> &str {
    let l = r.get("lang").and_then(Value::as_str).unwrap_or(FALLBACK_LANG);
    LANGS.iter().copied().find(|x| *x == l).unwrap_or(FALLBACK_LANG)
}

/// The region a request plays in (an unknown or missing one is CN: the money of saves made before regions).
pub fn region(r: &Value) -> &str {
    let g = r.get("region").and_then(Value::as_str).unwrap_or("CN");
    REGIONS.iter().copied().find(|x| *x == g).unwrap_or("CN")
}

/// The region a language plays in unless the player picks another: zh-TW Taiwan, zh-CN mainland China, en the US.
pub fn home_region(lang: &str) -> &'static str {
    match lang {
        "en" => "US",
        "zh-CN" => "CN",
        _ => "TW",
    }
}

/// A region's money: amounts are integers in its smallest unit (yuan for TWD and CNY, cents for USD).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Currency {
    pub code: &'static str,
    pub symbol: &'static str,
    pub digits: u32,
}

pub fn currency(region: &str) -> Currency {
    match region {
        "TW" => Currency { code: "TWD", symbol: "NT$", digits: 0 },
        "US" => Currency { code: "USD", symbol: "$", digits: 2 },
        _ => Currency { code: "CNY", symbol: "¥", digits: 0 },
    }
}

impl Currency {
    /// "NT$1,200", "¥45", "$1.99", "-$3.50".
    pub fn format(&self, amount: i64) -> String {
        let sign = if amount < 0 { "-" } else { "" };
        let a = amount.unsigned_abs();
        let unit = 10u64.pow(self.digits);
        let whole = group(a / unit);
        if self.digits == 0 {
            format!("{sign}{}{whole}", self.symbol)
        } else {
            format!("{sign}{}{whole}.{:0w$}", self.symbol, a % unit, w = self.digits as usize)
        }
    }
}

fn group(n: u64) -> String {
    let s = n.to_string();
    let mut out = String::new();
    for (i, c) in s.chars().enumerate() {
        if i > 0 && (s.len() - i) % 3 == 0 {
            out.push(',');
        }
        out.push(c);
    }
    out
}

/// The text tables of every language.
#[derive(Debug, Default)]
pub struct Texts {
    tables: BTreeMap<String, BTreeMap<String, String>>,
    pub sources: Vec<String>,
}

fn flatten(prefix: &str, v: &toml::Value, out: &mut BTreeMap<String, String>) {
    match v {
        toml::Value::Table(t) => {
            for (k, x) in t {
                let key = if prefix.is_empty() { k.clone() } else { format!("{prefix}.{k}") };
                flatten(&key, x, out);
            }
        }
        toml::Value::String(s) => {
            out.insert(prefix.to_string(), s.clone());
        }
        other => {
            out.insert(prefix.to_string(), other.to_string());
        }
    }
}

impl Texts {
    /// Every *.toml of Localization/<lang>/ beside the rules folder; the built-in tables when a language has none.
    pub fn load(dir: Option<&Path>) -> Result<Texts, String> {
        let mut texts = Texts::default();
        let root = dir.and_then(Path::parent).map(|d| d.join("Localization"));
        for (lang, builtin) in BUILTIN {
            let mut table = BTreeMap::new();
            let folder = root.as_ref().map(|r| r.join(lang)).filter(|f| f.is_dir());
            let mut files: Vec<_> = folder.iter().flat_map(|f| std::fs::read_dir(f).into_iter().flatten().flatten()).map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "toml")).collect();
            files.sort();
            if files.is_empty() {
                for text in builtin {
                    let v: toml::Value = toml::from_str(text).map_err(|e| format!("built-in {lang} table: {e}"))?;
                    flatten("", &v, &mut table);
                }
                texts.sources.push(format!("builtin:{lang}"));
            }
            for f in files {
                let text = std::fs::read_to_string(&f).map_err(|e| format!("{}: {e}", f.display()))?;
                let v: toml::Value = toml::from_str(&text).map_err(|e| format!("{}: {e}", f.display()))?;
                flatten("", &v, &mut table);
                texts.sources.push(f.display().to_string());
            }
            texts.tables.insert(lang.to_string(), table);
        }
        Ok(texts)
    }

    pub fn builtin() -> Texts {
        Texts::load(None).expect("built-in texts parse")
    }

    /// The text of a key in exactly this language (no fallback), as written.
    pub fn own(&self, lang: &str, key: &str) -> Option<&str> {
        self.tables.get(lang).and_then(|t| t.get(key)).map(String::as_str)
    }

    fn raw(&self, lang: &str, key: &str) -> Option<&str> {
        self.tables.get(lang).and_then(|t| t.get(key)).or_else(|| self.tables.get(FALLBACK_LANG).and_then(|t| t.get(key))).map(String::as_str)
    }

    /// The text of key in lang, an ICU message (icu.rs) with these arguments.
    pub fn t(&self, lang: &str, key: &str, args: &[(&str, String)]) -> String {
        let pattern = self.raw(lang, key).unwrap_or(key);
        let map: crate::icu::Args = args.iter().map(|(k, v)| (*k, crate::icu::Arg::Str(v.clone()))).collect();
        crate::icu::format(pattern, lang, &map)
    }

    /// As t with a count n (argument {n}, a number for `{n, plural, ...}`); key.one / key.other variants still win.
    pub fn tn(&self, lang: &str, key: &str, n: i64, args: &[(&str, String)]) -> String {
        let variant = format!("{key}.{}", if n == 1 { "one" } else { "other" });
        let chosen = if self.tables.get(lang).is_some_and(|t| t.contains_key(&variant)) { variant } else { key.to_string() };
        let pattern = self.raw(lang, &chosen).unwrap_or(&chosen);
        let mut map: crate::icu::Args = args.iter().map(|(k, v)| (*k, crate::icu::Arg::Str(v.clone()))).collect();
        map.insert("n", crate::icu::Arg::Int(n));
        crate::icu::format(pattern, lang, &map)
    }

    /// Every key under a prefix ("ui." for the scene's own words) in one language, fallbacks filled in.
    pub fn table(&self, lang: &str, prefix: &str) -> BTreeMap<String, String> {
        let mut out = BTreeMap::new();
        for l in [FALLBACK_LANG, lang] {
            if let Some(t) = self.tables.get(l) {
                for (k, v) in t.range(prefix.to_string()..).take_while(|(k, _)| k.starts_with(prefix)) {
                    out.insert(k.clone(), v.clone());
                }
            }
        }
        out
    }

    /// Keys a language lacks that zh-TW has (the gaps to translate).
    pub fn missing(&self, lang: &str) -> Vec<String> {
        let (Some(base), Some(t)) = (self.tables.get(FALLBACK_LANG), self.tables.get(lang)) else { return vec![] };
        base.keys().filter(|k| !t.contains_key(*k) && !k.ends_with(".one") && !k.ends_with(".other")).cloned().collect()
    }
}

pub fn eval(texts: &Texts, kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        // The scene's words in its language, and the region's money.
        "text_table" => Ok(json!({"lang": lang(r), "texts": texts.table(lang(r), r.get("prefix").and_then(Value::as_str).unwrap_or("ui."))})),
        "locale_info" => {
            let c = currency(region(r));
            Ok(json!({"lang": lang(r), "region": region(r), "home_region": home_region(lang(r)), "langs": LANGS, "regions": REGIONS, "currency": {"code": c.code, "symbol": c.symbol, "digits": c.digits}, "missing": texts.missing(lang(r)).len()}))
        }
        "money_format" => Ok(json!({"text": currency(region(r)).format(r.get("amount").and_then(Value::as_i64).unwrap_or(0))})),
        // Unreal's own words (OKT / OKMsg): one message {"id", "args"} or several {"items": [{"id", "args"}]}, each an id or a
        // zh-TW text that is one, formatted in the request's language and region. Unreal keeps no tables or formatter.
        "text" => Ok(json!({"text": text_of(r)})),
        "texts" => Ok(json!({"texts": r.get("items").and_then(Value::as_array).into_iter().flatten().map(text_of).collect::<Vec<_>>()})),
        // Her reply as spoken lines ({"texts": [...], "max": 15}) and joined back into one ({"reply"}).
        "message_hint" => {
            let (body, hint) = message_hint(r.get("message").and_then(Value::as_str).unwrap_or(""));
            Ok(json!({"body": body, "hint": hint}))
        }
        "floor_named" => {
            let labels: Vec<String> = r.get("labels").and_then(Value::as_array).into_iter().flatten().filter_map(|v| v.as_str().map(str::to_string)).collect();
            Ok(json!({"floor": floor_named(r.get("text").and_then(Value::as_str).unwrap_or(""), &labels)}))
        }
        "reply_lines" => {
            let max = r.get("max").and_then(Value::as_u64).unwrap_or(15) as usize;
            let mut lines = Vec::new();
            let texts: Vec<&str> = r.get("texts").and_then(Value::as_array).into_iter().flatten().map(|t| t.as_str().unwrap_or("")).collect();
            for (k, t) in texts.iter().enumerate() {
                // The model's lines of one reply: one cut in the middle of a sentence ("你今天还和胡桃" / "一起吃了饭吧。")
                // has no stop of its own. Before another of its lines it ends with a comma, not the full stop a last
                // line gets ("还和胡桃。一起吃了饭吧").
                let fixed = fix_hey(t.trim());
                let t = fixed.as_str();
                if k + 1 < texts.len() && t.chars().last().is_some_and(|c| !"。！？!?～~…」』）)\"，、；：,;:—-".contains(c)) {
                    split_lines(&format!("{t}{}", if latin(t) { "," } else { "，" }), max, &mut lines);
                } else {
                    split_lines(t, max, &mut lines);
                }
            }
            Ok(json!({"reply": join_lines(&lines), "lines": lines}))
        }
        "reply_join" => {
            let lines: Vec<String> = r.get("lines").and_then(Value::as_array).into_iter().flatten().filter_map(|v| v.as_str().map(str::to_string)).collect();
            Ok(json!({"reply": join_lines(&lines)}))
        }
        // Chinese text the tables have no translation for (a line's own zh-TW, as the fallback) in the language's script:
        // zh-CN Mainland Simplified, zh-TW Taiwan Traditional; English is returned as it is (it needs a translation).
        "script_texts" => {
            let l = lang(r);
            let texts: Vec<String> = r.get("texts").and_then(Value::as_array).into_iter().flatten().map(|t| {
                let t = t.as_str().unwrap_or("");
                match l {
                    "zh-CN" => zhconv::zhconv(t, zhconv::Variant::ZhCN),
                    "zh-TW" => zhconv::zhconv(t, zhconv::Variant::ZhTW),
                    _ => t.to_string(),
                }
            }).collect();
            Ok(json!({"lang": l, "texts": texts}))
        }
        _ => return None,
    })
}

/// English (the game's English mode): letters and no CJK.
fn latin(text: &str) -> bool {
    !text.chars().any(|c| ('\u{3040}'..='\u{9FFF}').contains(&c)) && text.chars().any(|c| c.is_ascii_alphabetic())
}

/// The model writes the call "喂～" as 餵 (to feed) in Traditional: a call (at the start of a sentence, followed by a
/// pause) is 喂. "餵你吃一口" and "我來餵。" stay.
pub fn fix_hey(text: &str) -> String {
    const PAUSE: &str = "～~，,、！!？?…—－-。 ";
    let chars: Vec<char> = text.chars().collect();
    chars.iter().enumerate().map(|(i, &c)| {
        let starts = i == 0 || PAUSE.contains(chars[i - 1]) || "「『（(\"".contains(chars[i - 1]);
        if c == '餵' && starts && chars.get(i + 1).is_some_and(|n| PAUSE.contains(*n)) { '喂' } else { c }
    }).collect()
}

/// Her lines joined into one reply: English with spaces, Chinese without.
pub fn join_lines(lines: &[String]) -> String {
    lines.join(if lines.iter().any(|l| latin(l)) { " " } else { "" })
}

/// A reply cut into spoken lines of at most `max` letters: after a sentence end, else at a comma, else hard. The end of
/// one of the model's lines ends a sentence: the model sometimes leaves the stop out (and segments are joined without
/// spaces), so one is added (a question mark after a question particle, "." in English); a segment cut only for its
/// length is the middle of a sentence and gets none.
pub fn split_lines(text: &str, max: usize, out: &mut Vec<String>) {
    const ENDS: &str = "。！？!?～~";
    const TRAILING: &str = "。！？!?～~…」』）)\"";
    const SOFT: &str = "，、；：,;:…—-";
    const ASKING: &str = "吗嗎呢么麼";
    let mut line: Vec<char> = Vec::new();
    let mut flush = |line: &mut Vec<char>, stop: bool| {
        let mut s: String = line.iter().collect::<String>().trim().to_string();
        if let Some(last) = s.chars().last() {
            if stop && !TRAILING.contains(last) && !SOFT.contains(last) {
                s.push(if latin(&s) { '.' } else if ASKING.contains(last) { '？' } else { '。' });
            }
            out.push(s);
        }
        line.clear();
    };
    let clean: Vec<char> = text.trim().chars().collect();
    let mut i = 0;
    while i < clean.len() {
        let c = clean[i];
        if c == '\n' || c == '\r' {
            flush(&mut line, true);
            i += 1;
            continue;
        }
        line.push(c);
        if ENDS.contains(c) {
            // Keep "！？", "……" and closing quotes with the sentence they end.
            while i + 1 < clean.len() && TRAILING.contains(clean[i + 1]) {
                i += 1;
                line.push(clean[i]);
            }
            flush(&mut line, true);
            i += 1;
            continue;
        }
        if line.len() >= max.max(2) {
            let cut = (1..line.len() - 1).rev().find(|&j| SOFT.contains(line[j])).map(|j| j + 1).unwrap_or(line.len());
            let rest = line.split_off(cut);
            flush(&mut line, false);
            line = rest;
        }
        i += 1;
    }
    flush(&mut line, true);
}

/// The floor (0 = ground) named in a few words: digits ("3", "3F"), a Chinese numeral ("三樓", "十楼"), an English
/// ordinal ("third floor", "ground"), else the stop whose label contains the words; -1 when none.
pub fn floor_named(text: &str, labels: &[String]) -> i64 {
    let digits: String = text.chars().filter(char::is_ascii_digit).collect();
    if let Ok(n) = digits.parse::<i64>() {
        return n - 1;
    }
    const ZH: [char; 10] = ['一', '二', '三', '四', '五', '六', '七', '八', '九', '十'];
    if let Some(i) = (0..ZH.len()).rev().find(|&i| text.contains(ZH[i])) {
        return i as i64;
    }
    let lower = text.to_lowercase();
    const EN: [&str; 10] = ["first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth"];
    if lower.contains("ground") || lower.contains("lobby") {
        return 0;
    }
    if let Some(i) = (0..EN.len()).rev().find(|&i| lower.contains(EN[i])) {
        return i as i64;
    }
    if text.trim().is_empty() {
        return -1;
    }
    labels.iter().position(|l| l.contains(text.trim())).map_or(-1, |i| i as i64)
}

/// A message split into what was said or happened (body) and the control hints in it (hint), which live behind the "?":
/// a line splits at the language's semicolons and after its full stops; a piece naming a control (hear.control_hint) is a
/// hint; the rest is joined again with the language's marks.
pub fn message_hint(message: &str) -> (String, String) {
    use crate::tr::t;
    let (stop, exclaim, ask, semi) = (t("ui.contacts.full_stop"), t("ui.rules.exclaim_mark"), t("ui.rules.question_mark"), t("ui.economy.semicolon_mark"));
    let (mut body, mut hint) = (Vec::new(), Vec::new());
    for line in message.split('\n') {
        let mut pieces = Vec::new();
        let mut piece = String::new();
        let chars: Vec<char> = line.chars().collect();
        for (i, &c) in chars.iter().enumerate() {
            let next_space = chars.get(i + 1).is_none_or(|n| n.is_whitespace());
            if c == '；' || (c == ';' && next_space) {
                pieces.push(std::mem::take(&mut piece));
                continue;
            }
            piece.push(c);
            if c == '。' || (c == '.' && next_space) {
                pieces.push(std::mem::take(&mut piece));
            }
        }
        pieces.push(piece);
        let mut said = String::new();
        for p in pieces.iter().map(|p| p.trim()).filter(|p| !p.is_empty()) {
            if crate::tr::heard(p, "hear.control_hint") {
                hint.push(p.to_string());
                continue;
            }
            if !said.is_empty() && ![stop, exclaim, ask, "。", "！", "？"].iter().any(|m| said.trim_end().ends_with(m.trim())) {
                said.push_str(semi);
            } else if !said.is_empty() && semi.ends_with(' ') {
                said.push(' ');
            }
            said.push_str(p);
        }
        if !said.is_empty() {
            body.push(said);
        }
    }
    (body.join("\n"), hint.join("\n"))
}

fn text_of(r: &Value) -> String {
    let id = r.get("id").and_then(Value::as_str).unwrap_or("");
    let mut args = crate::icu::Args::new();
    if let Some(m) = r.get("args").and_then(Value::as_object) {
        for (k, v) in m {
            let a = match v {
                Value::Number(n) if n.is_i64() => crate::icu::Arg::Int(n.as_i64().unwrap_or(0)),
                Value::Number(n) => crate::icu::Arg::Num(n.as_f64().unwrap_or(0.0)),
                Value::String(s) => crate::icu::Arg::Str(s.clone()),
                other => crate::icu::Arg::Str(other.to_string()),
            };
            args.insert(k.as_str(), a);
        }
    }
    crate::tr::text(id, &args)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_call_is_wei_not_feed() {
        assert_eq!(fix_hey("餵～你怎麼不說話了？"), "喂～你怎麼不說話了？");
        assert_eq!(fix_hey("餵，阿明"), "喂，阿明");
        assert_eq!(fix_hey("餵你吃一口嘛"), "餵你吃一口嘛");
        assert_eq!(fix_hey("我來餵。"), "我來餵。");
        assert_eq!(fix_hey("嗯…餵！你聽到沒？"), "嗯…喂！你聽到沒？");
    }

    #[test]
    fn floors_and_weekdays() {
        let labels = vec!["1F 大廳".to_string(), "2F 女裝".to_string(), "10F 景觀餐廳".to_string()];
        assert_eq!(floor_named("3F", &labels), 2);
        assert_eq!(floor_named("十樓", &labels), 9);
        assert_eq!(floor_named("third floor", &labels), 2);
        assert_eq!(floor_named("女裝", &labels), 1);
        assert_eq!(floor_named("", &labels), -1);
        let t = Texts::builtin();
        crate::tr::install(&t);
        let day = |lang: &str, d: i64| {
            let _s = crate::tr::scope(lang, "CN");
            crate::tr::text("ui.calendar.weekday_name", &[("d", crate::icu::Arg::Int(d))].into_iter().collect())
        };
        assert_eq!((day("zh-CN", 0), day("zh-CN", 6), day("en", 2)), ("一".to_string(), "日".to_string(), "Wed".to_string()));
    }

    #[test]
    fn free_talk_rules_prompt() {
        // The JSON examples keep their braces (quoted for ICU), the two numbers are filled, an English apostrophe stays.
        crate::tr::install(&Texts::builtin());
        let prompt = |lang: &str| {
            let _s = crate::tr::scope(lang, "CN");
            crate::tr::text("ui.free_talk.rules_prompt", &[("favor_step", crate::icu::Arg::Int(5)), ("segment_chars", crate::icu::Arg::Int(15))].into_iter().collect())
        };
        let (tw, cn, en) = (prompt("zh-TW"), prompt("zh-CN"), prompt("en"));
        assert!(tw.contains("（-5 到 +5 的整數）") && tw.contains("每段不超過 15 個字") && tw.contains("{\"kind\": \"end_talk\"}"), "{tw}");
        assert!(cn.contains("每段不超过 15 个字") && cn.contains("{\"kind\": \"stop_eating\"}"), "{cn}");
        assert!(en.contains("from -5 to +5") && en.contains("don't deny it") && en.contains("{\"kind\": \"end_talk\"}"), "{en}");
        assert!(!tw.contains('\'') && !cn.contains('\''));
    }

    #[test]
    fn message_hints() {
        crate::tr::install(&Texts::builtin());
        let split = |lang: &str, m: &str| {
            let _s = crate::tr::scope(lang, "CN");
            message_hint(m)
        };
        assert_eq!(split("zh-CN", "已抵达便利店。E 与 fin 交谈；Tab 切换"), ("已抵达便利店。".to_string(), "E 与 fin 交谈\nTab 切换".to_string()));
        assert_eq!(split("en", "Arrived at the store. E: talk with Aki; Tab to switch"), ("Arrived at the store.".to_string(), "E: talk with Aki\nTab to switch".to_string()));
        assert_eq!(split("zh-TW", "她笑了；你也笑了"), ("她笑了；你也笑了".to_string(), String::new()));
    }

    #[test]
    fn talk_lines() {
        let mut out = Vec::new();
        split_lines("你还记得吗\n我们去吃饭", 15, &mut out);
        assert_eq!(out, vec!["你还记得吗？", "我们去吃饭。"]);
        let mut out = Vec::new();
        split_lines("好啊！？走吧", 15, &mut out);
        assert_eq!(out, vec!["好啊！？", "走吧。"]);
        let mut out = Vec::new();
        split_lines("Fine\nLet's go", 15, &mut out);
        assert_eq!(join_lines(&out), "Fine. Let's go.");
        // Cut for length at a comma: no stop added to the first part.
        let mut out = Vec::new();
        split_lines("今天天气很好，我们一起去公园散步吧", 10, &mut out);
        assert_eq!(out[0], "今天天气很好，");
        assert_eq!(out[1], "我们一起去公园散步吧", "cut for length again: the middle of a sentence");
        // One reply's lines together: a line cut mid-sentence ends with a comma, the last with a stop.
        let texts = Texts::builtin();
        let r = eval(&texts, "reply_lines", &serde_json::json!({"texts": ["就为了问这个，不行吗？", "你今天还和胡桃", "一起吃了饭吧"], "max": 15})).unwrap().unwrap();
        assert_eq!(r["reply"], "就为了问这个，不行吗？你今天还和胡桃，一起吃了饭吧。", "{r}");
    }

    #[test]
    fn unreal_words() {
        let texts = Texts::builtin();
        crate::tr::install(&texts);
        let ask = |r: Value| {
            let _s = crate::tr::scope(lang(&r), region(&r));
            eval(&texts, r["rule"].as_str().unwrap(), &r).unwrap().unwrap()
        };
        assert_eq!(ask(json!({"rule": "text", "lang": "en", "id": "test.hello", "args": {"name": "Alter"}}))["text"], "Hello, Alter");
        // A zh-TW text that is a message comes out in the language asked for.
        assert_eq!(ask(json!({"rule": "text", "lang": "en", "id": "只有繁中"}))["text"], "只有繁中");
        assert_eq!(ask(json!({"rule": "text", "lang": "zh-TW", "id": "你好，{name}", "args": {"name": "A"}}))["text"], "你好，A");
        assert_eq!(ask(json!({"rule": "text", "lang": "en", "id": "no.such.key"}))["text"], "no.such.key");
        let many = ask(json!({"rule": "texts", "lang": "en", "region": "US", "items": [{"id": "test.cards", "args": {"n": 3}}, {"id": "{a, number, ::currency}", "args": {"a": 199}}]}));
        assert_eq!(many["texts"][0], "3 cards");
        assert_eq!(many["texts"][1], "$1.99");
    }

    #[test]
    fn money_in_each_region() {
        assert_eq!(currency("TW").format(30), "NT$30");
        assert_eq!(currency("CN").format(5), "¥5");
        assert_eq!(currency("US").format(199), "$1.99");
        assert_eq!(currency("US").format(-350), "-$3.50");
        assert_eq!(currency("TW").format(1_234_567), "NT$1,234,567");
    }

    #[test]
    fn texts_fall_back_and_fill_in() {
        let t = Texts::builtin();
        assert_eq!(t.t("en", "test.hello", &[("name", "Alter".into())]), "Hello, Alter");
        assert_eq!(t.t("zh-TW", "test.hello", &[("name", "Alter".into())]), "你好，Alter");
        assert_eq!(t.tn("en", "test.cards", 1, &[]), "1 card");
        assert_eq!(t.tn("en", "test.cards", 3, &[]), "3 cards");
        assert_eq!(t.tn("zh-TW", "test.cards", 3, &[]), "3 張");
        assert_eq!(t.t("en", "test.only_tw", &[]), "只有繁中", "missing in en: zh-TW");
        assert_eq!(t.t("en", "no.such.key", &[]), "no.such.key");
        assert!(t.missing("en").contains(&"test.only_tw".to_string()));
        assert_eq!(lang(&json!({"lang": "fr"})), "zh-TW");
        assert_eq!(region(&json!({})), "CN");
        assert_eq!((home_region("en"), home_region("zh-CN"), home_region("zh-TW")), ("US", "CN", "TW"));
    }
}
