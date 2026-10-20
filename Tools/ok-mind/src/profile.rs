//! Heroine profiles (plan §4.1): written once by DeepSeek from the card, validated here, then editable by hand in
//! Saved/StreetSocial/mind/heroines/<id>.json. The rules read numbers only from here, never from free text.
use crate::store::write_atomic;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::path::{Path, PathBuf};

pub const PROFILE_SCHEMA: u32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct Income {
    /// allowance | parttime | salary | scholarship | family | other
    pub source: String,
    pub amount: i64,
    /// monthly | weekly | per_shift
    pub cycle: String,
    pub payday: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct FixedCost {
    pub name: String,
    pub amount: i64,
    pub due_day: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct MoneyNeed {
    pub reason: String,
    pub amount: i64,
    /// YYYY-MM-DD on the 2010+ calendar
    pub deadline: String,
    pub severity: f32,
    pub secret: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct SavingGoal {
    pub item: String,
    pub amount: i64,
    pub why: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct Money {
    pub cash: i64,
    pub bank: i64,
    pub income: Vec<Income>,
    pub fixed_costs: Vec<FixedCost>,
    /// frugal | normal | generous | lavish
    pub spending_style: String,
    pub treat_pride: i32,
    pub expects_treat: i32,
    pub reciprocity: i32,
    pub saving_goals: Vec<SavingGoal>,
    pub money_needs: Vec<MoneyNeed>,
    /// "CNY" since 2026-10-11 (the game runs on RMB); older files held NT$ and are converted once on load.
    pub currency: String,
}

impl Money {
    /// NT$ -> RMB at 4.7, once.
    pub fn to_rmb(&mut self) {
        if self.currency == "CNY" {
            return;
        }
        let c = |v: i64| if v <= 0 { v } else { ((v as f64 / 4.7).round() as i64).max(1) };
        self.cash = c(self.cash);
        self.bank = c(self.bank);
        for i in &mut self.income { i.amount = c(i.amount); }
        for f in &mut self.fixed_costs { f.amount = c(f.amount); }
        for g in &mut self.saving_goals { g.amount = c(g.amount); }
        for n in &mut self.money_needs { n.amount = c(n.amount); }
        self.currency = "CNY".into();
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct DrinkTaste {
    pub sweetness: String,
    pub ice: String,
    pub toppings: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct Tastes {
    pub likes: Vec<String>,
    pub dislikes: Vec<String>,
    pub allergies: Vec<String>,
    pub diet: String,
    pub drink: DrinkTaste,
    pub spice: i32,
    /// never | social | likes
    pub alcohol: String,
    /// practical | cute | luxury | handmade | experience
    pub gift_style: String,
    pub lucky_numbers: Vec<i32>,
    pub favorite_teams: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct Traits {
    pub worship_proneness: i32,
    pub jealousy_proneness: i32,
    pub possessiveness: i32,
    pub insecurity: i32,
    pub competitiveness: i32,
    pub self_esteem: i32,
    pub tsundere: i32,
    pub mask_axes: Vec<String>,
    pub shyness: i32,
    pub public_boldness: i32,
    pub charm_resistance: i32,
    pub money_resistance: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct Occupation {
    /// student | clerk | shop_staff | office | idle_rich | other
    pub primary: String,
    /// staff | manager | owner
    pub role: String,
    pub workplace_place_id: String,
    pub job_id: String,
    pub competence: i32,
    pub pride: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct PersonalityCore {
    pub catchphrase: String,
    pub pride: String,
    pub fear: String,
    pub desire: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(default)]
pub struct Profile {
    pub schema: u32,
    pub id: String,
    pub display_name: String,
    pub age_in_setting: i32,
    /// S H G D E Q C (relife types)
    pub archetype: String,
    pub personality_core: PersonalityCore,
    pub orientation: String,
    pub material: String,
    pub drama_mode_override: String,
    pub occupation: Occupation,
    pub home_place_id: String,
    pub favorite_places: Vec<String>,
    pub birthday: String,
    /// Campus department id (Docs/STREET_SOCIAL_CAMPUS_PLAN.md); empty when she is not a student.
    pub department: String,
    /// 0-100; None on older profiles (the game derives one).
    pub intelligence: Option<i32>,
    pub money: Money,
    pub tastes: Tastes,
    pub traits: Traits,
    pub bio: String,
    /// llm | hand
    pub source: String,
}

const ARCHETYPES: [&str; 7] = ["S", "H", "G", "D", "E", "Q", "C"];
const ORIENTATIONS: [&str; 6] = ["Monogamous", "Open", "Poly", "Competitive", "Indifferent", "Recruiter"];
const MATERIALS: [&str; 3] = ["Romantic", "Mixed", "Mercenary"];
/// Must match OKStreetDepts() in Source/OpenKoikatsu/OKStreetSocialCampus.h.
const DEPARTMENTS: [&str; 21] = [
    "me", "mse", "ce", "che", "ee", "eecs_ee", "csie", "photonics", "im_ind", "ba", "im", "finance", "design", "arch", "afl", "dle", "ast", "bme", "color",
    "smart_mfg", "ai",
];

fn pick(value: &str, allowed: &[&str], fallback: &str) -> String {
    allowed.iter().find(|a| a.eq_ignore_ascii_case(value.trim())).map(|a| a.to_string()).unwrap_or_else(|| fallback.to_string())
}

fn pct(v: i32) -> i32 {
    v.clamp(0, 100)
}

fn valid_date(s: &str) -> bool {
    let p: Vec<&str> = s.split('-').collect();
    if p.len() != 3 {
        return false;
    }
    let (Ok(y), Ok(m), Ok(d)) = (p[0].parse::<i32>(), p[1].parse::<u32>(), p[2].parse::<u32>()) else { return false };
    let days = match m {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 => 29,
        2 => 28,
        _ => return false,
    };
    (1..=days).contains(&d)
}

/// Clamps and repairs a generated profile. Errors only for what cannot be repaired (the adult gate).
const TEMPLATE_BIRTHDAY_MD: &str = "-05-12";

pub fn validate(mut p: Profile, id: &str, places: &[String]) -> Result<Profile, String> {
    p.schema = PROFILE_SCHEMA;
    p.id = id.to_string();
    // Hard gate (relife ARCH §20.3): every heroine is an adult.
    if p.age_in_setting < 18 {
        return Err(format!("profile_rejected: age_in_setting {} is under 18", p.age_in_setting));
    }
    p.archetype = pick(&p.archetype, &ARCHETYPES, "Q");
    p.orientation = pick(&p.orientation, &ORIENTATIONS, "Monogamous");
    p.material = pick(&p.material, &MATERIALS, "Mixed");
    if !["", "A", "B", "C"].contains(&p.drama_mode_override.as_str()) {
        p.drama_mode_override.clear();
    }
    let known = |id: &String| places.is_empty() || places.contains(id);
    if !known(&p.home_place_id) {
        p.home_place_id = "girls_dorm".into();
    }
    p.favorite_places.retain(|x| known(x));
    if !p.occupation.workplace_place_id.is_empty() && !known(&p.occupation.workplace_place_id) {
        p.occupation.workplace_place_id.clear();
    }
    p.occupation.role = pick(&p.occupation.role, &["staff", "manager", "owner"], "staff");
    p.occupation.competence = pct(p.occupation.competence);
    p.occupation.pride = pct(p.occupation.pride);
    // The model tends to copy the template's example date; a missing or copied birthday gets a stable one from her id.
    if !valid_date(&p.birthday) || p.birthday.ends_with(TEMPLATE_BIRTHDAY_MD) {
        let h = id.bytes().fold(2166136261u32, |h, b| (h ^ b as u32).wrapping_mul(16777619));
        let year = p.birthday.get(..4).and_then(|y| y.parse::<u32>().ok()).filter(|y| (1950..=1992).contains(y)).unwrap_or(1990);
        let month = h % 12 + 1;
        let day = (h / 12) % 28 + 1;
        p.birthday = format!("{year:04}-{month:02}-{day:02}");
    }
    if p.occupation.primary == "student" {
        if !DEPARTMENTS.contains(&p.department.as_str()) {
            let h = id.bytes().fold(2166136261u32, |h, b| (h ^ b as u32).wrapping_mul(16777619));
            p.department = DEPARTMENTS[(h % DEPARTMENTS.len() as u32) as usize].into();
        }
    } else {
        p.department.clear();
    }
    p.intelligence = p.intelligence.map(|v| v.clamp(10, 100));
    let m = &mut p.money;
    m.currency = "CNY".into(); // generated in RMB
    m.cash = m.cash.max(0);
    m.bank = m.bank.max(0);
    m.income.retain(|i| i.amount > 0);
    m.fixed_costs.retain(|c| c.amount > 0);
    m.treat_pride = pct(m.treat_pride);
    m.expects_treat = pct(m.expects_treat);
    m.reciprocity = pct(m.reciprocity);
    m.spending_style = pick(&m.spending_style, &["frugal", "normal", "generous", "lavish"], "normal");
    for n in &mut m.money_needs {
        n.amount = n.amount.max(0);
        n.severity = n.severity.clamp(0.0, 1.0);
        if !valid_date(&n.deadline) {
            n.deadline.clear();
        }
    }
    let t = &mut p.traits;
    for v in [
        &mut t.worship_proneness, &mut t.jealousy_proneness, &mut t.possessiveness, &mut t.insecurity, &mut t.competitiveness,
        &mut t.self_esteem, &mut t.tsundere, &mut t.shyness, &mut t.public_boldness, &mut t.charm_resistance, &mut t.money_resistance,
    ] {
        *v = pct(*v);
    }
    t.mask_axes.retain(|a| ["affection", "admiration", "jealousy"].contains(&a.as_str()));
    if t.tsundere > 0 && t.mask_axes.is_empty() {
        t.mask_axes = vec!["affection".into(), "admiration".into(), "jealousy".into()];
    }
    p.tastes.spice = p.tastes.spice.clamp(0, 3);
    p.tastes.alcohol = pick(&p.tastes.alcohol, &["never", "social", "likes"], "social");
    p.tastes.gift_style = pick(&p.tastes.gift_style, &["practical", "cute", "luxury", "handmade", "experience"], "cute");
    p.tastes.lucky_numbers.retain(|n| (1..=49).contains(n));
    if p.source.is_empty() {
        p.source = "llm".into();
    }
    Ok(p)
}

/// The instruction appended after the card's facts; the reply must be one JSON object in this shape.
pub fn generation_prompt(card: &Value, places: &[String]) -> String {
    let template = json!({
        "display_name": "", "age_in_setting": 20, "archetype": "S|H|G|D|E|Q|C",
        "personality_core": {"catchphrase": "", "pride": "", "fear": "", "desire": ""},
        "orientation": "Monogamous|Open|Poly|Competitive|Indifferent|Recruiter", "material": "Romantic|Mixed|Mercenary",
        "drama_mode_override": "", "bio": "三到五句的背景",
        "occupation": {"primary": "student|clerk|shop_staff|office|idle_rich|other", "role": "staff", "workplace_place_id": "", "job_id": "", "competence": 50, "pride": 50},
        "home_place_id": "girls_dorm", "favorite_places": ["cafe"], "birthday": "YYYY-MM-DD",
        "department": "csie", "intelligence": 60,
        "money": {"cash": 600, "bank": 2000, "income": [{"source": "allowance", "amount": 1300, "cycle": "monthly", "payday": 1}],
                  "fixed_costs": [{"name": "手機", "amount": 99, "due_day": 10}], "spending_style": "frugal|normal|generous|lavish",
                  "treat_pride": 50, "expects_treat": 50, "reciprocity": 50,
                  "saving_goals": [{"item": "", "amount": 0, "why": ""}], "money_needs": [{"reason": "", "amount": 0, "deadline": "2010-10-20", "severity": 0.5, "secret": false}]},
        "tastes": {"likes": ["dk_pearl_milk"], "dislikes": [], "allergies": [], "diet": "none", "drink": {"sweetness": "半糖", "ice": "去冰", "toppings": []},
                   "spice": 1, "alcohol": "social", "gift_style": "cute", "lucky_numbers": [7], "favorite_teams": []},
        "traits": {"worship_proneness": 50, "jealousy_proneness": 50, "possessiveness": 50, "insecurity": 50, "competitiveness": 50, "self_esteem": 50,
                   "tsundere": 0, "mask_axes": [], "shyness": 50, "public_boldness": 50, "charm_resistance": 50, "money_resistance": 50}
    });
    format!(
        "以下是一張角色卡的資料：\n{card}\n\n\
遊戲背景：2010 年 9 月開始的台灣大學城開放世界，她是住在這裡的成年女性（18 歲以上）。\
可用的地點 id：{places}。\n\
請依卡片的性格為她寫一份完整的人物設定。數值 0–100，金額一律用人民幣（大家都用人民幣），符合 2010 年的物價（例如一杯珍奶約 10 元、一頓簡餐約 20–50 元、學生每月生活費約 1,000–2,000 元）。\
birthday 依她的年齡與性格自己決定（不要用 5 月 12 日）。\n她若是學生（occupation.primary 為 student），department 填她讀的科系 id（{depts}），否則留空字串；intelligence 是她的學業能力 0–100，依性格決定。money_needs 只在她真的有經濟困境時才填，沒有就給空陣列；secret 表示她不想讓別人知道。\
tastes.likes 可以填飲料店或店家的品項 id（例如 dk_pearl_milk、cf_latte）或簡短的喜好描述。\
所有內容（喜歡的偶像、歌、品牌、產品、App、興趣、背景經歷）只能用 2010 年 9 月以前就已經存在的東西，不要出現之後才有的產品、作品或事件。用繁體中文寫文字欄位。只輸出一個 JSON 物件，欄位與下面範本完全相同：\n{template}",
        card = card,
        places = places.join("、"),
        depts = DEPARTMENTS.join("、"),
        template = template
    )
}

pub struct Profiles {
    dir: PathBuf,
    /// Content/IndependentResources/StreetSocialData/Profiles: the shipped profiles. A save without one (a new save, or
    /// Saved/ wiped) starts from the shipped file instead of asking DeepSeek again.
    seed: Option<PathBuf>,
}

fn file_name(id: &str) -> String {
    let safe: String = id.chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '_' }).collect();
    format!("{safe}.json")
}

impl Profiles {
    pub fn new(save_dir: &Path, seed: Option<PathBuf>) -> Profiles {
        Profiles { dir: save_dir.join("heroines"), seed }
    }
    pub fn load(&self, id: &str) -> Result<Option<Profile>, String> {
        let path = self.dir.join(file_name(id));
        if !path.exists() {
            match self.seed.as_ref().map(|d| d.join(file_name(id))).filter(|p| p.exists()) {
                Some(shipped) => {
                    std::fs::create_dir_all(&self.dir).map_err(|e| format!("profile_dir: {e}"))?;
                    std::fs::copy(&shipped, &path).map_err(|e| format!("profile_seed_copy: {e}"))?;
                }
                None => return Ok(None),
            }
        }
        let text = std::fs::read_to_string(&path).map_err(|e| format!("profile_unreadable: {e}"))?;
        let mut p: Profile = serde_json::from_str(text.trim_start_matches('\u{feff}')).map_err(|e| format!("profile_invalid: {e}"))?;
        if p.money.currency != "CNY" {
            p.money.to_rmb();
            self.save(&p)?;
        }
        // Hand edits still pass the adult gate.
        if p.age_in_setting < 18 {
            return Err(format!("profile_rejected: {} is under 18", path.display()));
        }
        Ok(Some(p))
    }
    pub fn save(&self, p: &Profile) -> Result<(), String> {
        std::fs::create_dir_all(&self.dir).map_err(|e| format!("profile_dir: {e}"))?;
        let text = serde_json::to_string_pretty(p).map_err(|e| e.to_string())?;
        write_atomic(&self.dir.join(file_name(&p.id)), text.as_bytes())
    }
}
