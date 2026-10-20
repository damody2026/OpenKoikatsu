//! `ok-extract . --motion-catalogue <in.tsv> <out.json>`: every solo motion in ONE list, each with a Traditional
//! Chinese label (Content/IndependentResources/MotionCatalogue/MotionCatalogue.json, read by OKMotionCatalogue.cpp).
//!
//! Dev-time data generator. Input is the TSV the interaction pair scene writes when started with
//! -DumpMotionCatalogue (Saved/MotionCatalogue.tsv, Tools/run_interaction_pair.lua). Re-run after the shared
//! animation resources change. Formerly OK_Demo's Scripts/build_motion_catalogue.py; same output byte for byte.
//!
//! Names are Unity animator states: "<family> / <layer>.<state>". The label is composed from a family label, and a
//! state/layer label built token by token from the vocabulary below. Codes whose meaning is not certain from the
//! naming convention (e.g. SS/WS/SF/WF speed transitions) are kept as the code instead of being guessed.
use std::collections::{HashMap, HashSet};

/// Vocabulary (lower-case token -> Chinese).
const TOKENS: &[(&str, &str)] = &[
    // H-motion state machine
    ("idle", "待機"), ("loop", "循環"), ("start", "開始"), ("stop", "停止"), ("in", "進入"), ("out", "離開"),
    ("wloop", "弱循環"), ("sloop", "強循環"), ("mloop", "中循環"), ("oloop", "高潮前循環"), ("orgasm", "高潮"),
    ("insert", "插入"), ("insertidle", "插入待機"), ("pull", "拔出"), ("drop", "落下"), ("touch", "觸碰"),
    ("drink", "吞下"), ("vomit", "吐出"), ("oral", "口交"), ("dislikes", "討厭"), ("back", "背面"), ("front", "正面"),
    ("base", "基本"), ("action", "動作"), ("reaction", "反應"), ("attack", "攻擊"), ("defense", "防禦"), ("down", "倒下"),
    ("cutin", "特寫"),
    // touch-layer vocabulary (romaji)
    ("te", "手"), ("yubi", "手指"), ("sita", "舌頭"), ("denma", "電動按摩棒"), ("vibe", "震動棒"),
    ("mune", "胸部"), ("kokan", "下體"), ("anal", "後庭"), ("siri", "臀部"),
    ("sawari", "輕觸"), ("sawri", "輕觸"), ("momu", "揉捏"), ("kosuru", "摩擦"), ("ijiru", "撥弄"), ("tutuku", "戳弄"),
    ("hipparu", "拉扯"), ("ireru", "放入"), ("kakimawasu", "攪動"), ("nameru", "舔"), ("namemawasu", "環舔"),
    ("ositukeru", "壓上"), ("nademawasu", "撫摸"),
    // daily-life / studio vocabulary
    ("stand", "站立"), ("tachi", "站姿"), ("suwari", "坐姿"), ("sleep", "睡覺"), ("soine", "陪睡"), ("syagami", "蹲下"),
    ("talk", "對話"), ("walk", "走路"), ("run", "跑步"), ("aruki", "走"), ("hasiru", "跑"), ("dance", "舞蹈"), ("dansu", "舞蹈"),
    ("eat", "飲食"), ("chase", "追逐"), ("cheer", "應援"), ("penlight", "螢光棒"), ("live", "演唱會"), ("call", "呼喊"),
    ("kiss", "接吻"), ("twoshot", "雙人合照"), ("undou", "運動"), ("yoga", "瑜伽"), ("stretch", "伸展"), ("sadou", "茶道"),
    ("manken", "網咖"), ("koikatu", "Koikatsu"), ("isu", "椅子"), ("tukue", "桌子"), ("ne", "躺"), ("nugi", "脫衣"),
    ("meka", "機械"), ("nage", "投擲"), ("kasa", "傘"), ("katana", "日本刀"), ("konbou", "棍棒"), ("tyugosi", "背對"),
    ("fantasy", "奇幻"), ("halloween", "萬聖節"), ("xmas", "聖誕"), ("beachball", "沙灘球"), ("beachchair", "沙灘椅"),
    ("beach", "海灘"), ("date", "約會"), ("kokuhaku", "告白"), ("pool", "泳池"), ("drink_", "飲料"), ("shopping", "購物"),
    ("tabletennis", "桌球"), ("tennis", "網球"), ("bsk", "籃球"), ("airmat", "氣墊"), ("surf", "衝浪"), ("ukiwa", "泳圈"),
    ("suiei", "游泳"), ("dive", "潛水"), ("kaigara", "貝殼"), ("sunaijiri", "玩沙"), ("onsen", "溫泉"), ("engawa", "緣廊"),
    ("taki", "瀑布"), ("tsuri", "釣魚"), ("sora", "看天空"), ("kansatsu", "觀察"), ("sanshin", "三線琴"), ("uchiwa", "團扇"),
    ("bingata", "紅型"), ("sisa", "風獅爺"), ("esthetic", "美容"), ("aquarium", "水族館"), ("shimakko", "條紋"),
    ("poster", "海報"), ("pole", "鋼管"), ("tpose", "T字姿勢"), ("squat", "蹲"), ("land", "著地"), ("ecg", "事件CG"),
    ("ks", "KKS"), ("mc", "男主"), ("mannequin", "模特兒"), ("fmannequin", "女模特兒"), ("mmannequin", "男模特兒"),
    ("lewd", "挑逗"), ("ang", "生氣"), ("joy", "開心"), ("sat", "悲傷"), ("oth", "其他"), ("other", "其他"), ("pose", "姿勢"),
    ("gsit", "地面坐"), ("gsleep", "地面睡"), ("mstand", "男站姿"), ("h", "H"), ("adv", "ADV"),
    ("math", "數學"), ("teacher", "老師"), ("homeroom", "班導"), ("yuka", "地面"), ("chair", "椅子"), ("desk", "桌子"),
    ("floor", "地板"), ("summer", "夏日"), ("wa", "和風"), ("male", "男性"), ("female", "女性"),
    // weapons / fighting
    ("fighta", "格鬥A"), ("fightb", "格鬥B"), ("aurashot", "氣功波"), ("sword", "劍"), ("shieldsword", "盾劍"),
    ("dagger1", "短劍1"), ("dagger2", "短劍2"), ("gun1", "手槍1"), ("gun2", "手槍2"), ("throwing", "投擲武器"),
    ("magicwand", "魔杖"), ("magicbook", "魔導書"), ("largesword", "大劍"), ("rod", "長棍"), ("spear", "長槍"),
    ("hammer", "鎚"), ("sickle", "鐮刀"), ("rifle", "步槍"), ("bow", "弓"),
];
const FAMILIES: &[(&str, &str)] = &[
    ("action", "動作"), ("action_05", "動作05"), ("action_19", "動作19"), ("action_19_2", "動作19-2"), ("adv", "ADV劇情"),
    ("chair", "椅子"), ("chair_desk", "椅子與桌子"), ("custom_pose", "自訂姿勢"), ("dance", "舞蹈"), ("dance_02", "舞蹈02"),
    ("dance_19", "舞蹈19"), ("eat", "飲食"), ("emotion", "情緒表情"), ("fantasy", "奇幻"), ("floor", "地板"),
    ("gravure_00", "寫真"), ("live", "演唱會"), ("male", "男性動作"), ("meka", "機械"), ("nage", "投擲"), ("nugi", "脫衣"),
    ("outdoor_00", "戶外"), ("pose", "姿勢"), ("pose_04", "睡姿"), ("pose_05", "坐姿"), ("pose_07", "姿勢07"),
    ("pose_08", "姿勢08"), ("pose_09", "姿勢09"), ("pose_10", "姿勢10"), ("pose_12", "萬聖節"), ("pose_13", "姿勢13"),
    ("pose_17", "聖誕"), ("pose_19", "姿勢19"), ("pose_31", "姿勢31"), ("reaction", "反應"), ("sleep", "睡覺"),
    ("summer", "夏日"), ("tachi", "站姿"), ("talk", "對話"), ("tpose", "T字姿勢"), ("wa", "和風"), ("walk_run", "走路與跑步"),
    ("koikatsuS_f_anim", "KKS 女角動作"), ("koikatsuS_m_anim", "KKS 男角動作"), ("f_cutin", "女 特寫"), ("m_cutin", "男 特寫"),
];
const GROUP_ORDER: &[&str] = &["自訂姿勢", "日常", "戰鬥", "H・愛撫", "H・奉仕", "H・插入", "H・其他", "H・道具"];

fn lookup(table: &[(&str, &'static str)], key: &str) -> Option<&'static str> {
    table.iter().find(|(k, _)| *k == key).map(|(_, v)| *v)
}
fn size(low: &str) -> Option<&'static str> { match low { "s" => Some("S型"), "m" => Some("M型"), "l" => Some("L型"), _ => None } }
fn side(low: &str) -> &'static str { match low { "l" => "左", "r" => "右", "f" => "前", _ => "後" } }
/// Aibu (caress) body-part letters: mune / siri / kokan / anal
fn aibu_part(low: &str) -> Option<&'static str> { match low { "m" => Some("胸部"), "s" => Some("臀部"), "k" => Some("下體"), "a" => Some("後庭"), _ => None } }
fn sex(code: &str) -> &'static str { match code { "f" => "女", "m" => "男", _ => "" } }
fn kh_category(cat: &str) -> Option<&'static str> {
    Some(match cat { "a" => "愛撫", "h" => "奉仕", "s" => "插入", "t" => "高潮動作(kht)", "n" => "愛撫疊加(khn)", "r" => "愛撫疊加(khr)", "h3" => "奉仕2", "s3" => "插入2", _ => return None })
}
fn khp_item(item: &str) -> &str { match item { "denma" => "電動按摩棒", "sita" => "舌頭", "te" => "手", "yubi" => "手指", "vibe" => "震動棒", other => other } }
fn is_digits(s: &str) -> bool { !s.is_empty() && s.chars().all(|c| c.is_ascii_digit()) }

/// Chinese for one token, or None when it is a plain code/number.
fn tok(t: &str) -> Option<String> {
    let low = t.to_lowercase();
    if let Some(z) = lookup(TOKENS, &low) { return Some(z.to_owned()); }
    // sloop1, wloop2, dagger1 ...
    let split = low.find(|c: char| !c.is_ascii_lowercase()).unwrap_or(low.len());
    let (word, number) = low.split_at(split);
    if !word.is_empty() && is_digits(number) {
        if let Some(z) = lookup(TOKENS, word) { return Some(format!("{z}{number}")); }
    }
    None
}

/// Split 'Stand_Ang_00' / 'ks_f_airmat_00_00_action' / 'isu tukue.ks_f_...' into tokens.
fn words(text: &str) -> Vec<&str> { text.split(['_', ' ', '.']).filter(|p| !p.is_empty()).collect() }

fn render(tokens: &[&str], aibu: bool) -> String {
    let mut out: Vec<String> = Vec::new();
    let mut nums: Vec<&str> = Vec::new();
    let n = tokens.len();
    let mut first = true;
    let mut i = 0;
    let lower = |k: usize| tokens[k].to_lowercase();
    while i < n {
        let t = tokens[i];
        let low = t.to_lowercase();
        if is_digits(t) {
            nums.push(t);
            i += 1;
            first = false;
            continue;
        }
        if !nums.is_empty() { out.push(nums.join("-")); nums.clear(); }
        let prev = if i > 0 { lower(i - 1) } else { String::new() };
        if first && size(&low).is_some() && n > 1 {
            out.push(size(&low).unwrap().to_owned());
        } else if low == "ks" {
            // "Koikatsu Sunshine" marker, not worth a word
        } else if (low == "f" || low == "m") && (first || (i > 0 && prev == "ks")) {
            out.push(if low == "f" { "女" } else { "男" }.to_owned());
        } else if low == "a" && i > 0 && ["in", "out", "orgasm", "drink", "vomit"].contains(&prev.as_str()) {
            out.push("變化A".to_owned());
        } else if low == "b" && i > 0 && prev == "orgasm" {
            out.push("變化B".to_owned());
        } else if low == "a" && (i == 0 || size(&prev).is_some()) {
            out.push("後庭".to_owned());
        } else if aibu && aibu_part(&low).is_some() && i + 1 < n && ["idle", "loop", "touch"].contains(&lower(i + 1).as_str()) {
            out.push(aibu_part(&low).unwrap().to_owned());
        } else if ["l", "r", "f", "b"].contains(&low.as_str()) && i == n - 1 && i > 0 {
            out.push(side(&low).to_owned());
        } else if (low == "c" || low == "d") && i + 1 < n {
            match tok(tokens[i + 1]) {
                Some(next) => { out.push(format!("{next}({})", t.to_uppercase())); i += 1; }
                None => out.push(format!("{}級", t.to_uppercase())),
            }
        } else {
            out.push(tok(t).unwrap_or_else(|| t.to_owned()));
        }
        first = false;
        i += 1;
    }
    if !nums.is_empty() { out.push(nums.join("-")); }
    out.join("・")
}

fn group_for_cat(cat: &str) -> &'static str {
    match cat { "a" => "H・愛撫", "h" | "h3" => "H・奉仕", "s" | "s3" => "H・插入", _ => "H・其他" }
}

fn family_label(fam: &str, pair_zh: &HashMap<String, String>) -> (String, &'static str) {
    // kh<cat>_<sex>[_<rest>]
    if let Some(after) = fam.strip_prefix("kh") {
        if let Some(us) = after.find('_') {
            let (cat, tail) = (&after[..us], &after[us + 1..]);
            let mut chars = tail.chars();
            if cat.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit()) {
                if let Some(sx @ ('f' | 'm' | 'p' | 'o')) = chars.next() {
                    let rest_part = chars.as_str();
                    let rest = if rest_part.is_empty() { Some(None) } else { rest_part.strip_prefix('_').filter(|r| !r.is_empty()).map(Some) };
                    if let Some(rest) = rest {
                        let sx = sx.to_string();
                        let s = sex(&sx);
                        if rest.is_some() {
                            if let Some(pair) = pair_zh.get(fam) {
                                return (format!("{pair}{}", if s.is_empty() { String::new() } else { format!("({s})") }), group_for_cat(cat));
                            }
                        }
                        let name = kh_category(cat).map(str::to_owned).unwrap_or_else(|| format!("kh{cat}"));
                        let mut extra = String::new();
                        if let Some(rest) = rest {
                            extra = if rest.starts_with("base") { " 基本".to_owned() } else { format!(" {}", rest.replace("_base", " 基本").replace('_', "-")) };
                            if rest.starts_with("base") && rest.chars().count() > 4 { extra = format!(" 基本{}", &rest[4..]); }
                        }
                        return (format!("{name}{}{extra}", if s.is_empty() { String::new() } else { format!("({s})") }), group_for_cat(cat));
                    }
                }
            }
        }
    }
    // khp_<item>_item[L|R]
    if let Some(after) = fam.strip_prefix("khp_") {
        if let Some(us) = after.find('_') {
            let (item, tail) = (&after[..us], &after[us..]);
            let side = match tail { "_item" => Some(""), "_itemL" => Some("左"), "_itemR" => Some("右"), _ => None };
            if let Some(side) = side.filter(|_| !item.is_empty() && item.chars().all(|c| c.is_ascii_lowercase())) {
                return (format!("道具 {}{}", khp_item(item), if side.is_empty() { String::new() } else { format!(" {side}") }), "H・道具");
            }
        }
    }
    // f_03_Sword
    let mut parts = fam.splitn(3, '_');
    if let (Some(sx @ ("f" | "m")), Some(number), Some(weapon)) = (parts.next(), parts.next(), parts.next()) {
        if is_digits(number) && !weapon.is_empty() && weapon.chars().all(|c| c.is_alphanumeric() || c == '_') {
            let label: Vec<String> = weapon.split('_').map(|p| tok(p).unwrap_or_else(|| p.to_owned())).collect();
            return (format!("{}{}", if sx == "f" { "女 " } else { "男 " }, label.join("・")), "戰鬥");
        }
    }
    if let Some(z) = lookup(FAMILIES, fam) {
        let group = if fam == "custom_pose" { "自訂姿勢" } else if fam.contains("cutin") { "戰鬥" } else { "日常" };
        return (z.to_owned(), group);
    }
    (fam.to_owned(), "日常")
}

fn label(name: &str, pair_zh: &HashMap<String, String>) -> (String, &'static str) {
    let Some((fam, rest)) = name.split_once(" / ") else { return (name.to_owned(), "日常") };
    let Some((layer, state)) = rest.split_once('.') else { return (name.to_owned(), "日常") };
    let (famz, group) = family_label(fam, pair_zh);
    let detail = if layer != "Base Layer" {
        // "52:Denma_mune_sawari_L" -> tool / body part / action / side
        let body = layer.split_once(':').map_or(layer, |(_, b)| b);
        render(&words(body), false)
    } else {
        render(&words(state), fam.starts_with("kha"))
    };
    (format!("{famz}｜{detail}"), group)
}

/// Builds the catalogue JSON from the dump TSV. Returns (json, motion count, untranslated token report).
pub fn build(tsv: &str) -> Result<(String, usize, String), String> {
    let rows: Vec<Vec<&str>> = tsv.lines().map(|l| l.split('\t').collect()).collect();
    let mut pair_zh = HashMap::new();
    for r in rows.iter().filter(|r| r[0] == "pair" && r.len() > 2) {
        let j: serde_json::Value = serde_json::from_str(r[1]).map_err(|e| format!("pair row: {e}"))?;
        for key in ["female_motion", "male_motion"] {
            let motion = j[key].as_str().ok_or_else(|| format!("pair row without {key}"))?;
            pair_zh.insert(motion.to_owned(), r[2].to_owned());
        }
    }
    let mut seen = HashSet::new();
    let mut items: Vec<(usize, usize, String, String)> = Vec::new(); // (group rank, order, key, label)
    for (order, r) in rows.iter().filter(|r| r[0] == "solo").enumerate() {
        let (source, name) = (*r.get(1).ok_or("solo row without source")?, *r.get(2).ok_or("solo row without name")?);
        if !seen.insert(name) { continue; }
        let (z, group) = label(name, &pair_zh);
        let rank = GROUP_ORDER.iter().position(|g| *g == group).unwrap_or(99);
        items.push((rank, order, format!("{source}|{name}"), z));
    }
    items.sort_by_key(|x| (x.0, x.1));
    let motions: Vec<String> = items.iter().map(|(_, _, k, z)| {
        format!("{{\"k\":{},\"z\":{}}}", serde_json::to_string(k).unwrap(), serde_json::to_string(z).unwrap())
    }).collect();
    let json = format!("{{\"version\":1,\"count\":{},\"motions\":[{}]}}", items.len(), motions.join(","));
    // Leftovers: tokens that stayed romaji (letters only, not single-letter codes).
    let mut left: Vec<(String, usize)> = Vec::new();
    for (_, _, _, z) in &items {
        let chars: Vec<char> = z.chars().collect();
        let mut i = 0;
        while i < chars.len() {
            if chars[i].is_ascii_alphabetic() {
                let mut j = i + 1;
                while j < chars.len() && chars[j].is_ascii_alphanumeric() { j += 1; }
                if j - i >= 3 {
                    let word: String = chars[i..j].iter().collect();
                    match left.iter_mut().find(|(w, _)| *w == word) { Some(e) => e.1 += 1, None => left.push((word, 1)) }
                    i = j;
                    continue;
                }
            }
            i += 1;
        }
    }
    left.sort_by(|a, b| b.1.cmp(&a.1));
    let shown: Vec<String> = left.iter().take(120).map(|(w, c)| format!("{w}×{c}")).collect();
    Ok((json, items.len(), format!("untranslated tokens ({}): {}", left.len(), shown.join(", "))))
}

pub fn cli(tsv: &std::path::Path, out: &std::path::Path) -> Result<(), String> {
    let text = std::fs::read_to_string(tsv).map_err(|e| format!("{}: {e}", tsv.display()))?;
    let (json, count, report) = build(&text)?;
    if let Some(dir) = out.parent() { std::fs::create_dir_all(dir).map_err(|e| e.to_string())?; }
    std::fs::write(out, json).map_err(|e| format!("{}: {e}", out.display()))?;
    println!("motions {count} -> {}", out.display());
    println!("{report}");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn labels_follow_the_vocabulary() {
        let pairs = HashMap::from([("khs_f_00".to_owned(), "正常位".to_owned())]);
        assert_eq!(label("custom_pose / Base Layer.Fmannequin_00", &pairs), ("自訂姿勢｜女模特兒・00".to_owned(), "自訂姿勢"));
        assert_eq!(label("khs_f_00 / Base Layer.M_SLoop", &pairs).0, "正常位(女)｜M型・強循環");
        assert_eq!(label("khh_m_base / Base Layer.Idle", &pairs), ("奉仕(男) 基本｜待機".to_owned(), "H・奉仕"));
        assert_eq!(label("khp_denma_itemL / 52:Denma_mune_sawari_L.x", &pairs), ("道具 電動按摩棒 左｜電動按摩棒・胸部・輕觸・左".to_owned(), "H・道具"));
        assert_eq!(label("f_03_Sword / Base Layer.attack_01", &pairs), ("女 劍｜攻擊・01".to_owned(), "戰鬥"));
        // A leading S/M/L is the body size; an aibu body-part letter only counts after it.
        assert_eq!(label("kha_f_00 / Base Layer.M_Idle", &pairs).0, "愛撫(女) 00｜M型・待機");
        assert_eq!(label("kha_f_00 / Base Layer.S_M_Idle", &pairs).0, "愛撫(女) 00｜S型・胸部・待機");
        assert_eq!(label("no separator", &pairs), ("no separator".to_owned(), "日常"));
    }
}
