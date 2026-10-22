//! The campus (Docs/STREET_SOCIAL_CAMPUS_PLAN.md): colleges, departments and their timetables, enrolment, attendance,
//! exams graded from intelligence, studying (alone, with her, her own evenings), expulsion and the entrance exam again,
//! and the office's leave / return / transfer / admission. Unreal keeps the records (OKStreetAcademicJson) and the
//! moment-to-moment facts (where he stands, who studies beside him); every rule is here.
use crate::calendar::date;
use crate::campus::{self, civil_day, Phase};
use crate::effects::Effects;
use crate::rng::RandomStream;
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

pub struct College {
    pub id: &'static str,
    pub name: &'static str,
    pub building: &'static str,
    pub code: &'static str,
    pub building_name: &'static str,
}

pub struct Dept {
    pub id: &'static str,
    pub name: &'static str,
    pub college: &'static str,
    /// Entrance / transfer exam bar (intelligence + luck >= bar).
    pub bar: i32,
    /// Four major courses, separated by "|".
    pub courses: &'static str,
}

pub const COLLEGES: [College; 8] = [
    College { id: "engineering", name: "工程學院", building: "campus_e1", code: "E1", building_name: "工程一館" },
    College { id: "eecs", name: "電資學院", building: "campus_ee", code: "EE", building_name: "電資館" },
    College { id: "management", name: "管理學院", building: "campus_ma", code: "MA", building_name: "管理大樓" },
    College { id: "design", name: "設計學院", building: "campus_t4", code: "T4", building_name: "第四教學大樓" },
    College { id: "humanities", name: "人文社會學院", building: "campus_t2", code: "T2", building_name: "第二教學大樓" },
    College { id: "appliedsci", name: "應用科技學院", building: "campus_ib", code: "IB", building_name: "國際大樓" },
    College { id: "general", name: "共同教育委員會", building: "campus_t1", code: "T1", building_name: "第一教學大樓" },
    College { id: "industry", name: "產學創新學院", building: "campus_rb", code: "RB", building_name: "綜合研究大樓" },
];

pub const DEPTS: [Dept; 21] = [
    Dept { id: "me", name: "機械工程系", college: "engineering", bar: 50, courses: "工程力學|熱力學|機構學|機械設計" },
    Dept { id: "mse", name: "材料科學與工程系", college: "engineering", bar: 50, courses: "材料科學導論|物理冶金|材料熱力學|高分子材料" },
    Dept { id: "ce", name: "營建工程系", college: "engineering", bar: 48, courses: "靜力學|結構學|土壤力學|營建管理" },
    Dept { id: "che", name: "化學工程系", college: "engineering", bar: 52, courses: "有機化學|化工熱力學|輸送現象|反應工程" },
    Dept { id: "ee", name: "電子工程系", college: "eecs", bar: 58, courses: "電子學|電路學|訊號與系統|半導體元件" },
    Dept { id: "eecs_ee", name: "電機工程系", college: "eecs", bar: 56, courses: "電磁學|控制系統|電力系統|電路學" },
    Dept { id: "csie", name: "資訊工程系", college: "eecs", bar: 60, courses: "資料結構|演算法|作業系統|計算機組織" },
    Dept { id: "photonics", name: "光電工程研究所", college: "eecs", bar: 62, courses: "光電導論|雷射原理|光學|光電元件" },
    Dept { id: "im_ind", name: "工業管理系", college: "management", bar: 48, courses: "作業研究|生產管理|統計學|品質管理" },
    Dept { id: "ba", name: "企業管理系", college: "management", bar: 46, courses: "管理學|行銷學|會計學|組織行為" },
    Dept { id: "im", name: "資訊管理系", college: "management", bar: 50, courses: "程式設計|資料庫系統|系統分析|資訊管理導論" },
    Dept { id: "finance", name: "財務金融研究所", college: "management", bar: 55, courses: "投資學|財務管理|計量經濟|衍生性商品" },
    Dept { id: "design", name: "設計系", college: "design", bar: 45, courses: "基本設計|設計素描|色彩學|設計史" },
    Dept { id: "arch", name: "建築系", college: "design", bar: 50, courses: "建築設計|建築史|構造學|都市計畫" },
    Dept { id: "afl", name: "應用外語系", college: "humanities", bar: 44, courses: "英文寫作|口譯入門|日語會話|語言學概論" },
    Dept { id: "dle", name: "數位學習與教育研究所", college: "humanities", bar: 50, courses: "教育心理學|數位學習設計|教學科技|研究方法" },
    Dept { id: "ast", name: "應用科技研究所", college: "appliedsci", bar: 55, courses: "奈米科技|生醫材料|應用物理|科技實驗" },
    Dept { id: "bme", name: "醫學工程研究所", college: "appliedsci", bar: 57, courses: "生理學|醫學影像|生物力學|醫療器材" },
    Dept { id: "color", name: "色彩與照明科技研究所", college: "appliedsci", bar: 50, courses: "色彩科學|照明設計|視覺感知|光度學" },
    Dept { id: "smart_mfg", name: "智慧製造科技研究所", college: "industry", bar: 54, courses: "智慧製造導論|工業物聯網|自動化系統|數位孿生" },
    Dept { id: "ai", name: "人工智慧跨域科技研究所", college: "industry", bar: 62, courses: "機器學習|深度學習|線性代數|資料科學" },
];

/// General-education courses (taught in T1), two per semester for every department.
pub const GENERAL: [&str; 8] = ["大學國文", "大學英文", "微積分", "普通物理", "藝術欣賞", "哲學概論", "經濟學", "體育"];

/// Class slots: morning 09:00-12:00, afternoon 13:30-16:30.
pub const SLOT_START: [i32; 2] = [540, 810];
pub const SLOT_END: [i32; 2] = [720, 990];

pub fn find_dept(id: &str) -> Option<&'static Dept> {
    DEPTS.iter().find(|d| d.id == id)
}
pub fn find_college(id: &str) -> Option<&'static College> {
    COLLEGES.iter().find(|c| c.id == id)
}

/// A stable 32-bit hash of a string (FNV-1a over UTF-8) for seeds that come from names.
pub fn hash(s: &str) -> u32 {
    s.bytes().fold(0x811c9dc5u32, |h, b| (h ^ b as u32).wrapping_mul(0x01000193))
}

#[derive(Serialize, Deserialize, Debug, Clone, Default, PartialEq)]
#[serde(default)]
pub struct Course {
    pub name: String,
    pub building: String,
    pub floor: i32,
    pub room: i32,
    pub weekday: i32,
    pub slot: i32,
    pub held: i32,
    pub attended: i32,
    pub midterm: f32,
    #[serde(rename = "final")]
    pub fin: f32,
    pub grade: f32,
}

impl Course {
    fn new(name: String, building: &str) -> Course {
        Course { name, building: building.into(), floor: 2, room: 1, midterm: -1.0, fin: -1.0, grade: -1.0, ..Default::default() }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(default)]
pub struct Academic {
    pub dept: String,
    pub status: String,
    pub term: String,
    pub courses: Vec<Course>,
    pub transcript: Vec<String>,
    pub intelligence: f32,
    pub leave_until: String,
    pub fees_paid: String,
    pub scholarship: String,
}

impl Default for Academic {
    fn default() -> Self {
        Academic { dept: String::new(), status: "enrolled".into(), term: String::new(), courses: vec![], transcript: vec![], intelligence: 55.0, leave_until: String::new(), fees_paid: String::new(), scholarship: String::new() }
    }
}

/// A department's six courses for a term: four major ones in its college building, two general ones in T1. Fixed per
/// department and term so classmates share rooms and times.
pub fn timetable(dept_id: &str, term_key: &str) -> Vec<Course> {
    let mut out = Vec::new();
    let Some(d) = find_dept(dept_id) else { return out };
    let building = find_college(d.college).map(|c| c.building).unwrap_or("campus_t1");
    let mut rng = RandomStream::new((hash(dept_id) ^ hash(term_key)) as i32);
    let second = term_key.ends_with("-2");
    let mut days = [0, 1, 2, 3, 4];
    for i in (1..days.len()).rev() {
        days.swap(i, rng.rand_range(0, i as i32) as usize);
    }
    for (i, m) in d.courses.split('|').enumerate() {
        let m = crate::tr::lookup(&format!("ui.name.course.{}_{i}", d.id), m);
        let mut x = Course::new(format!("{m}{}", if second { crate::tr::t("academics.paren_two") } else { crate::tr::t("academics.paren_one") }), building);
        x.weekday = days[i % 5];
        x.slot = rng.rand_range(0, 1);
        x.floor = 2 + rng.rand_range(0, 3);
        x.room = 1 + rng.rand_range(0, 3);
        out.push(x);
    }
    for i in 0..2 {
        let g = ((rng.rand_range(0, GENERAL.len() as i32 - 1) + i * 3) as usize) % GENERAL.len();
        let mut x = Course::new(crate::tr::lookup(&format!("ui.name.course.general_{g}"), GENERAL[g]).into(), "campus_t1");
        x.weekday = days[((4 + i) % 5) as usize];
        x.slot = 1 - out.last().map(|c: &Course| c.slot).unwrap_or(0);
        x.floor = 2 + rng.rand_range(0, 3);
        x.room = 1 + rng.rand_range(0, 3);
        out.push(x);
    }
    // No two courses in the same weekday and slot.
    for a in 0..out.len() {
        for b in a + 1..out.len() {
            if out[a].weekday == out[b].weekday && out[a].slot == out[b].slot {
                out[b].slot = 1 - out[b].slot;
                if out[a].slot == out[b].slot {
                    out[b].weekday = (out[b].weekday + 1) % 5;
                }
            }
        }
    }
    out
}

/// An exam score from intelligence, attendance and luck (an average student, 55 with 70% attendance, passes).
pub fn exam_score(intelligence: f32, attend: f32, rng: &mut RandomStream) -> f32 {
    let luck = -8.0f32 + 16.0 * rng.frand();
    (intelligence * 0.7 + attend.clamp(0.0, 1.0) * 20.0 + 15.0 + luck).clamp(0.0, 100.0)
}

/// Intelligence gained by an hour of study; together with a clever partner, triple.
pub fn study_gain(intelligence: f32, hours: f32, boosted: bool) -> f32 {
    let base = 0.6 * hours * if intelligence >= 80.0 { 0.5 } else { 1.0 };
    base * if boosted { 3.0 } else { 1.0 }
}

/// More than half of the graded courses failed.
pub fn failed_too_many(courses: &[Course]) -> bool {
    let graded = courses.iter().filter(|c| c.grade >= 0.0).count();
    let failed = courses.iter().filter(|c| c.grade >= 0.0 && c.grade < 60.0).count();
    graded > 0 && failed * 2 > graded
}

/// Studying happens in the library, a dorm, a café or a campus building.
pub fn study_place(place: &str) -> Result<(), &'static str> {
    if matches!(place, "library" | "school" | "lecture" | "boys_dorm" | "girls_dorm" | "cafe") || place.starts_with("campus_") {
        Ok(())
    } else {
        Err(crate::tr::t("academics.not_study_place"))
    }
}

/// Studying for `minutes`: his gain, and hers beside him (x3 when he is at 75 or more).
pub fn study(me: &mut Academic, her: Option<&mut Academic>, minutes: i32) {
    if minutes <= 0 {
        return;
    }
    let hours = minutes as f32 / 60.0;
    let boost = me.intelligence >= 75.0;
    me.intelligence = (me.intelligence + study_gain(me.intelligence, hours, false)).min(100.0);
    if let Some(h) = her {
        h.intelligence = (h.intelligence + study_gain(h.intelligence, hours, boost)).min(100.0);
    }
}

fn transcript_lines(a: &mut Academic) {
    let term = a.term.clone();
    for c in &a.courses {
        if c.grade >= 0.0 {
            a.transcript.push(format!("{term}|{}|{:.0}|{}%", c.name, c.grade, if c.held > 0 { c.attended * 100 / c.held } else { 0 }));
        }
    }
}

/// A new term: last term's graded courses go to the transcript, this term's timetable comes in.
pub fn start_term(a: &mut Academic, key: &str) {
    transcript_lines(a);
    a.term = key.into();
    a.courses = timetable(&a.dept, key);
    if a.status == "leave" && a.leave_until.as_str() <= key {
        a.status = "enrolled".into();
        a.leave_until.clear();
    }
}

/// A heroine's first record: the profile's department when it names one, otherwise a stable pick from her id.
pub fn new_record(id: &str, profile: &Value) -> Academic {
    let named = profile.get("department").and_then(Value::as_str).unwrap_or("");
    let dept = if find_dept(named).is_some() && named != "general" { named.to_string() } else { DEPTS[hash(id) as usize % DEPTS.len()].id.to_string() };
    let student = profile.get("occupation").and_then(|o| o.get("primary")).and_then(Value::as_str) == Some("student");
    let intelligence = match profile.get("intelligence").and_then(Value::as_f64).filter(|v| v.is_finite()) {
        Some(v) => (v as f32).clamp(10.0, 100.0),
        None => {
            let competence = profile.get("occupation").and_then(|o| o.get("competence")).and_then(Value::as_f64).map(|v| v as i32).unwrap_or(50) as f32;
            (30.0 + competence * 0.6 + (hash(id) % 11) as f32).clamp(20.0, 95.0)
        }
    };
    Academic { dept, status: if student { "enrolled" } else { "none" }.into(), intelligence, ..Default::default() }
}

// ------------------------------------------------------------------------------------------------ the campus minute

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct Heroine {
    actor: i32,
    id: String,
    name: String,
    /// Null until her profile is loaded (no record is made before).
    profile: Value,
    academic: Option<Academic>,
    /// For each slot, the place of her class block at its middle ("" when not in class).
    slot_class: [String; 2],
    /// At her study block, at its place, not on the way.
    studying: bool,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct State {
    seen_in_class: Vec<usize>,
    class_closed: String,
    exams_done: Vec<String>,
    studying: bool,
    study_minutes: i32,
    study_applied: i32,
    /// The actor studying beside him, -1 for none.
    study_partner: i32,
    her_study: BTreeMap<i32, i32>,
}

/// Every game minute. Request: day, minute, elapsed (game minutes since the last tick), here (his place), player
/// (record), heroines, state. Answer: changed, player, academics {id: record}, state, message, effects.
pub fn tick(r: &Value) -> Result<Value, String> {
    let day = r.get("day").and_then(Value::as_i64).ok_or("missing_field: day")? as i32;
    let minute = r.get("minute").and_then(Value::as_i64).unwrap_or(0) as i32;
    let elapsed = r.get("elapsed").and_then(Value::as_i64).unwrap_or(1) as i32;
    let here = r.get("here").and_then(Value::as_str).unwrap_or("");
    let mut me: Academic = serde_json::from_value(r.get("player").cloned().unwrap_or(json!({}))).map_err(|e| format!("player: {e}"))?;
    let mut heroines: Vec<Heroine> = serde_json::from_value(r.get("heroines").cloned().unwrap_or(json!([]))).map_err(|e| format!("heroines: {e}"))?;
    let mut st: State = serde_json::from_value(r.get("state").cloned().unwrap_or(json!({}))).map_err(|e| format!("state: {e}"))?;
    let civil = civil_day(day);
    let (term, phase) = campus::term_at(civil);
    let weekday = date(day, minute as f64 / 60.0).weekday as i32;
    let mut fx = Effects::default();
    let mut message = String::new();
    let mut changed = false;
    // Everyone gets a record once: he reads 資訊工程系; students among the heroines read their department.
    if me.dept.is_empty() {
        me.dept = "csie".into();
        me.status = "enrolled".into();
        me.intelligence = 55.0;
        changed = true;
    }
    for h in heroines.iter_mut() {
        if h.academic.is_none() && h.profile.is_object() {
            h.academic = Some(new_record(&h.id, &h.profile));
            changed = true;
        }
    }
    // New term.
    if phase != Phase::Break {
        for a in std::iter::once(&mut me).chain(heroines.iter_mut().filter_map(|h| h.academic.as_mut())) {
            if a.status != "none" && a.status != "expelled" && a.term != term.key {
                start_term(a, &term.key);
                changed = true;
            }
        }
    }
    // Classes held can't outnumber the course's weekdays since the term began (older saves counted every tick after 16:30).
    if phase != Phase::Break {
        let since = civil - term.start;
        for a in std::iter::once(&mut me).chain(heroines.iter_mut().filter_map(|h| h.academic.as_mut())) {
            for x in a.courses.iter_mut() {
                let most = if since >= x.weekday as i64 { ((since - x.weekday as i64) / 7 + 1) as i32 } else { 0 };
                if x.held > most {
                    x.held = most;
                    x.attended = x.attended.min(most);
                    changed = true;
                }
            }
        }
    }
    // Attendance: he counts if he is inside the course's building during its slot; she, if her plan took her there.
    if phase == Phase::Classes && weekday < 5 {
        for slot in 0..2 {
            if minute >= SLOT_START[slot] && minute < SLOT_END[slot] {
                for (k, x) in me.courses.iter().enumerate() {
                    if x.weekday == weekday && x.slot == slot as i32 && here == x.building && !st.seen_in_class.contains(&k) {
                        st.seen_in_class.push(k);
                    }
                }
            }
            let close = format!("{day}|{slot}");
            // Both slots of the day are remembered: one string for "the last slot closed" made the morning and the
            // afternoon slot close each other again on every tick after 16:30 ("attended 1/160").
            if minute >= SLOT_END[slot] && !st.class_closed.split(',').any(|c| c == close) {
                st.class_closed = if st.class_closed.starts_with(&format!("{day}|")) { format!("{},{close}", st.class_closed) } else { close };
                let mark = |a: &mut Academic, went: &dyn Fn(usize, &Course) -> bool| {
                    if a.status != "enrolled" {
                        return;
                    }
                    for (k, x) in a.courses.iter_mut().enumerate() {
                        if x.weekday != weekday || x.slot != slot as i32 {
                            continue;
                        }
                        x.held += 1;
                        if went(k, x) {
                            x.attended += 1;
                        }
                    }
                };
                let seen = st.seen_in_class.clone();
                mark(&mut me, &|k, _| seen.contains(&k));
                for h in heroines.iter_mut() {
                    let at = h.slot_class[slot].clone();
                    if let Some(a) = h.academic.as_mut() {
                        mark(a, &|_, x| !at.is_empty() && at == x.building);
                    }
                }
                changed = true;
                if !st.seen_in_class.is_empty() {
                    message = crate::tr::t("academics.class_attended").into();
                }
                st.seen_in_class.clear();
            }
        }
    }
    // Exams: Friday 17:00 of the exam week.
    let exam_week = match phase {
        Phase::Midterm => Some((term.midterm, false)),
        Phase::Final => Some((term.fin, true)),
        _ => None,
    };
    if let Some((week, fin)) = exam_week {
        let done = format!("{}|{}", term.key, if fin { "final" } else { "mid" });
        if civil == week + 4 && minute >= 17 * 60 && !st.exams_done.contains(&done) {
            st.exams_done.push(done.clone());
            let mut news = Vec::new();
            let grade = |actor: i32, id: &str, who: &str, a: &mut Academic, fx: &mut Effects, news: &mut Vec<String>| {
                if a.status != "enrolled" || a.term != term.key {
                    return;
                }
                let mut rng = RandomStream::new((hash(&done) as i32) ^ actor.wrapping_mul(7919));
                let mut failed = 0;
                for x in a.courses.iter_mut() {
                    let rate = if x.held > 0 { x.attended as f32 / x.held as f32 } else { 0.6 };
                    let score = exam_score(a.intelligence, rate, &mut rng);
                    if fin {
                        x.fin = score;
                        x.grade = (x.midterm.max(0.0) * 0.4 + score * 0.6 + 0.5).floor();
                        if x.grade < 60.0 {
                            failed += 1;
                        }
                    } else {
                        x.midterm = score;
                    }
                }
                // His scholarship: the term's average, nothing if any course failed.
                if fin && actor == 0 && a.scholarship != term.key {
                    a.scholarship = term.key.clone();
                    let graded: Vec<f32> = a.courses.iter().filter(|x| x.grade >= 0.0).map(|x| x.grade).collect();
                    if !graded.is_empty() {
                        let avg = graded.iter().sum::<f32>() / graded.len() as f32;
                        let award = crate::bank::scholarship(avg, failed > 0);
                        if award > 0 {
                            fx.bank(award, "scholarship", &crate::tr::m("academics.scholarship_avg_label", &[("avg", crate::tr::a(&avg))]));
                            news.push(crate::tr::m("academics.scholarship_received", &[("award", crate::tr::a(&crate::tr::money(award))), ("avg", crate::tr::a(&avg))]));
                        }
                    }
                }
                if fin && failed_too_many(&a.courses) {
                    a.status = "expelled".into();
                    news.push(crate::tr::m("academics.expelled_too_many_fails", &[("name", crate::tr::a(&who))]));
                    if actor > 0 {
                        fx.observe(id, "academics", &crate::tr::m("academics.self_expelled_fails", &[("fail_count", crate::tr::a(&failed))]), "", 0.9, "observation");
                    }
                } else if fin {
                    news.push(crate::tr::m("academics.final_exam_fail_count", &[("who", crate::tr::a(&who)), ("failed", crate::tr::a(&failed))]));
                    if actor > 0 {
                        fx.observe(id, "academics", &crate::tr::m("academics.final_fail_notice", &[("fail_count", crate::tr::a(&failed))]), "", if failed > 0 { 0.6 } else { 0.4 }, "observation");
                    }
                } else if actor > 0 {
                    fx.observe(id, "academics", crate::tr::t("academics.midterm_done"), "", 0.3, "observation");
                }
            };
            grade(0, "", crate::tr::t("academics.you_pronoun"), &mut me, &mut fx, &mut news);
            for h in heroines.iter_mut() {
                let (actor, id, name) = (h.actor, h.id.clone(), h.name.clone());
                if let Some(a) = h.academic.as_mut() {
                    grade(actor, &id, &name, a, &mut fx, &mut news);
                }
            }
            message = if fin { news.join(crate::tr::t("academics.semicolon_sep")) } else { crate::tr::t("academics.midterm_ended_notice").into() };
            changed = true;
        }
    }
    // Studying: every game minute while he studies, saved every 15 minutes.
    if st.studying {
        if let Err(why) = study_place(here) {
            st.studying = false;
            message = why.into();
        } else {
            st.study_minutes += elapsed;
            if st.study_minutes - st.study_applied >= 15 {
                let her = heroines.iter_mut().find(|h| h.actor == st.study_partner).and_then(|h| h.academic.as_mut());
                study(&mut me, her, st.study_minutes - st.study_applied);
                st.study_applied = st.study_minutes;
                changed = true;
            }
        }
    }
    // Her own study: minutes at her study block, at the place (flushed every half hour of study).
    if phase != Phase::Break && elapsed > 0 {
        for h in heroines.iter_mut().filter(|h| h.studying) {
            let mins = st.her_study.entry(h.actor).or_insert(0);
            *mins += elapsed;
            if *mins >= 30 {
                if let Some(a) = h.academic.as_mut() {
                    a.intelligence = (a.intelligence + study_gain(a.intelligence, *mins as f32 / 60.0, false)).min(100.0);
                    *mins = 0;
                    changed = true;
                }
            }
        }
    }
    // A new term: a heroine who was expelled sits the entrance exam again — her old department, or the easiest one.
    if phase == Phase::Classes {
        for h in heroines.iter_mut() {
            let Some(a) = h.academic.as_mut() else { continue };
            if a.status != "expelled" || a.term == term.key {
                continue;
            }
            changed = true;
            let mut rng = RandomStream::new((hash(&term.key) as i32) ^ h.actor.wrapping_mul(104729));
            let mut want = find_dept(&a.dept);
            if let Some(w) = want {
                if a.intelligence + (-10.0 + 20.0 * rng.frand()) < w.bar as f32 {
                    for d in DEPTS.iter() {
                        if d.bar < want.map(|w| w.bar).unwrap_or(i32::MAX) {
                            want = Some(d);
                        }
                    }
                }
            }
            match want {
                Some(w) if a.intelligence + (-10.0 + 20.0 * rng.frand()) >= w.bar as f32 => {
                    let same = w.id == a.dept;
                    a.dept = w.id.into();
                    a.status = "enrolled".into();
                    start_term(a, &term.key);
                    fx.observe(&h.id, "academics", &if same { crate::tr::t("academics.retake_passed_same_major").to_string() } else { crate::tr::m("academics.retake_passed_school", &[("school_name", crate::tr::a(&w.name))]) }, "", 0.7, "observation");
                }
                _ => {
                    start_term(a, &term.key);
                    a.courses.clear();
                    fx.observe(&h.id, "academics", crate::tr::t("academics.retake_failed"), "", 0.6, "observation");
                }
            }
        }
    }
    let academics: Map<String, Value> = heroines.iter().filter_map(|h| h.academic.as_ref().map(|a| (h.id.clone(), json!(a)))).collect();
    Ok(json!({"changed": changed, "player": me, "academics": academics, "state": st, "message": message, "effects": fx.into_value()}))
}

/// Stopping study: the minutes not yet counted. Request: player, her (record or null), minutes.
pub fn study_flush(r: &Value) -> Result<Value, String> {
    let mut me: Academic = serde_json::from_value(r.get("player").cloned().unwrap_or(json!({}))).map_err(|e| format!("player: {e}"))?;
    let mut her: Option<Academic> = serde_json::from_value(r.get("her").cloned().unwrap_or(Value::Null)).map_err(|e| format!("her: {e}"))?;
    study(&mut me, her.as_mut(), r.get("minutes").and_then(Value::as_i64).unwrap_or(0) as i32);
    Ok(json!({"player": me, "her": her}))
}

/// The office: leave, return, transfer, admit. Request: action, dept, day, seed, player. Answer: player, notice.
pub fn office(r: &Value) -> Result<Value, String> {
    let action = r.get("action").and_then(Value::as_str).unwrap_or("");
    let dept = r.get("dept").and_then(Value::as_str).unwrap_or("");
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    let mut a: Academic = serde_json::from_value(r.get("player").cloned().unwrap_or(json!({}))).map_err(|e| format!("player: {e}"))?;
    let (term, phase) = campus::term_at(civil_day(day));
    let mut rng = RandomStream::new(day.wrapping_mul(13).wrapping_add(hash(&format!("{action}{dept}")) as i32));
    let notice;
    match action {
        "leave" => {
            a.status = "leave".into();
            a.leave_until = campus::next_term_key(&term.key);
            notice = crate::tr::m("academics.leave_approved_return", &[("return_term", crate::tr::a(&a.leave_until))]);
        }
        "return" => {
            a.status = "enrolled".into();
            a.leave_until.clear();
            notice = crate::tr::t("academics.return_approved_notice").to_string();
        }
        _ => {
            let Some(d) = find_dept(dept) else { return Ok(json!({"player": a, "notice": "", "changed": false})) };
            // Transfers during the term (not in exam weeks); the entrance exam any time.
            if action == "transfer" && (phase == Phase::Midterm || phase == Phase::Final) {
                return Ok(json!({"player": a, "notice": crate::tr::t("academics.no_transfer_exam_week"), "changed": false}));
            }
            let what = if action == "transfer" { crate::tr::t("academics.transfer_major_label") } else { crate::tr::t("academics.enrollment_label") };
            let score = a.intelligence + (-10.0 + 20.0 * rng.frand());
            if score >= d.bar as f32 {
                if action == "transfer" {
                    transcript_lines(&mut a);
                }
                a.dept = d.id.into();
                a.status = "enrolled".into();
                a.term.clear();
                a.courses.clear();
                notice = crate::tr::m("academics.exam_passed_status", &[("subject", crate::tr::a(&what)), ("score", crate::tr::a(&score)), ("threshold", crate::tr::a(&d.bar)), ("student_level", crate::tr::a(crate::tr::lookup(&format!("ui.name.dept.{}", d.id), d.name)))]);
            } else {
                notice = crate::tr::m("academics.exam_failed_score", &[("subject", crate::tr::a(&what)), ("score", crate::tr::a(&score)), ("threshold", crate::tr::a(&d.bar))]);
            }
        }
    }
    Ok(json!({"player": a, "notice": notice, "changed": true}))
}

/// The tables for the interface (names, buildings, codes).
pub fn tables() -> Value {
    let colleges: Vec<Value> = COLLEGES.iter().map(|c| json!({"id": c.id, "name": crate::tr::lookup(&format!("ui.name.college.{}", c.id), c.name), "building": c.building, "code": c.code, "building_name": crate::tr::lookup(&format!("ui.name.college_building.{}", c.id), c.building_name)})).collect();
    let depts: Vec<Value> = DEPTS.iter().map(|d| json!({"id": d.id, "name": crate::tr::lookup(&format!("ui.name.dept.{}", d.id), d.name), "college": d.college, "bar": d.bar})).collect();
    json!({"colleges": colleges, "depts": depts, "slot_start": SLOT_START, "slot_end": SLOT_END})
}

/// Which of his classes a shift would clash with (a shift booked over a class: he chooses, it is only said).
/// Request: player (academic), day (game day of the shift), from, to (hours). Answer: course (name or "").
fn class_clash(r: &Value) -> Result<Value, String> {
    let me: Academic = serde_json::from_value(r.get("player").cloned().unwrap_or(json!({}))).map_err(|e| format!("player: {e}"))?;
    let day = r.get("day").and_then(Value::as_i64).unwrap_or(0) as i32;
    let (from, to) = (r.get("from").and_then(Value::as_f64).unwrap_or(0.0), r.get("to").and_then(Value::as_f64).unwrap_or(0.0));
    let weekday = crate::calendar::date(day, from.max(4.0)).weekday as i32;
    // Class slots: 0 is 09:00-12:00, 1 is 13:30-16:30.
    let clash = me.courses.iter().find(|c| {
        let (a, b) = if c.slot == 0 { (9.0, 12.0) } else { (13.5, 16.5) };
        c.weekday == weekday && from < b && to > a
    });
    Ok(json!({"course": clash.map(|c| c.name.clone()).unwrap_or_default()}))
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "class_clash" => class_clash(r),
        "campus_tick" => tick(r),
        "campus_study" => study_flush(r),
        "campus_office" => office(r),
        "campus_tables" => Ok(tables()),
        "study_place" => Ok(match study_place(r.get("place").and_then(Value::as_str).unwrap_or("")) {
            Ok(()) => json!({"ok": true, "why": ""}),
            Err(why) => json!({"ok": false, "why": why}),
        }),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_shift_over_a_class_names_it_and_an_evening_one_does_not() {
        // Day 0 (2010-09-01) is a Wednesday (weekday 2).
        let player = json!({"courses": [{"name": "OS", "weekday": 2, "slot": 0}, {"name": "DS", "weekday": 2, "slot": 1}]});
        assert_eq!(class_clash(&json!({"player": player, "day": 0, "from": 8.0, "to": 14.0})).unwrap()["course"], "OS");
        assert_eq!(class_clash(&json!({"player": player, "day": 0, "from": 17.0, "to": 22.0})).unwrap()["course"], "");
        assert_eq!(class_clash(&json!({"player": player, "day": 1, "from": 8.0, "to": 14.0})).unwrap()["course"], "");
    }

    #[test]
    fn timetables() {
        // Ported from Tests/OKStreetCampus.spec.cpp Timetable.
        assert_eq!(COLLEGES.len(), 8);
        for d in DEPTS.iter() {
            assert!(find_college(d.college).is_some(), "{}", d.id);
            for key in ["2010-1", "2010-2", "2011-1"] {
                let c = timetable(d.id, key);
                assert_eq!(c.len(), 6);
                assert_eq!(c, timetable(d.id, key), "deterministic");
                assert!(c.iter().filter(|x| x.building == "campus_t1").count() >= 2);
                for (a, x) in c.iter().enumerate() {
                    assert!((0..5).contains(&x.weekday) && (0..2).contains(&x.slot) && (2..=5).contains(&x.floor) && (1..=4).contains(&x.room));
                    for y in &c[a + 1..] {
                        assert!(!(x.weekday == y.weekday && x.slot == y.slot), "{} {key}: {} and {} clash", d.id, x.name, y.name);
                    }
                }
            }
        }
    }

    fn expel_rate(intelligence: f32, attend: f32, runs: i32) -> f64 {
        let mut rng = RandomStream::new((intelligence * 1000.0 + attend * 100.0) as i32);
        let mut expelled = 0;
        for _ in 0..runs {
            let mut c = timetable("csie", "2010-1");
            for x in c.iter_mut() {
                let (m, f) = (exam_score(intelligence, attend, &mut rng), exam_score(intelligence, attend, &mut rng));
                x.grade = (m * 0.4 + f * 0.6 + 0.5).floor();
            }
            expelled += failed_too_many(&c) as i32;
        }
        expelled as f64 / runs as f64
    }

    #[test]
    fn grades() {
        assert!(expel_rate(55.0, 0.7, 2000) < 0.05, "an average student (55, 70%) is rarely expelled");
        assert!(expel_rate(80.0, 0.3, 2000) < 0.01, "a bright student passes even skipping classes");
        assert!(expel_rate(30.0, 0.7, 2000) > 0.8, "a weak student is usually expelled");
        assert!(expel_rate(45.0, 0.0, 2000) > 0.5, "never attending at 45 is usually expelled");
        let mut six = vec![Course::new("x".into(), ""); 6];
        for (i, c) in six.iter_mut().enumerate() {
            c.grade = if i < 3 { 50.0 } else { 70.0 };
        }
        assert!(!failed_too_many(&six));
        six[3].grade = 59.0;
        assert!(failed_too_many(&six));
        assert!(!failed_too_many(&vec![Course::new("x".into(), ""); 6]), "ungraded courses never expel");
    }

    #[test]
    fn study_gains() {
        assert_eq!(study_gain(55.0, 1.0, false), 0.6);
        assert_eq!(study_gain(85.0, 1.0, false), 0.3);
        assert!((study_gain(55.0, 1.0, true) - 1.8).abs() < 1e-6);
        assert!((study_gain(55.0, 0.25, false) * 4.0 - study_gain(55.0, 1.0, false)).abs() < 1e-6);
        assert!(study_place("campus_ee").is_ok() && study_place("park").is_err());
    }

    #[test]
    fn a_term_on_campus() {
        // Day 12 (2010-09-13) is the first Monday of the fall term: records are made, timetables come in.
        let first = civil_day(0);
        let (t, _) = campus::term_at(first + 12);
        assert_eq!(campus::game_day(t.start), 12);
        let h = json!({"actor": 1, "id": "a", "name": "A", "profile": {"occupation": {"primary": "student"}}});
        let v = tick(&json!({"day": 12, "minute": 600, "player": {}, "heroines": [h], "state": {}})).unwrap();
        assert_eq!(v["player"]["dept"], "csie");
        assert_eq!(v["player"]["term"], "2010-1");
        assert_eq!(v["academics"]["a"]["courses"].as_array().unwrap().len(), 6);
        // A slot closing counts attendance: he was in the building.
        let me: Academic = serde_json::from_value(v["player"].clone()).unwrap();
        let wd = date(12, 10.0).weekday as i32;
        if let Some((k, c)) = me.courses.iter().enumerate().find(|(_, c)| c.weekday == wd) {
            let end = SLOT_END[c.slot as usize];
            let v2 = tick(&json!({"day": 12, "minute": end, "player": me, "heroines": [], "state": {"seen_in_class": [k], "class_closed": "12|0"}})).unwrap();
            assert_eq!(v2["player"]["courses"][k]["attended"], 1);
            assert_eq!(v2["message"], "這堂課有出席。");
        }
        // After both slots closed, later ticks the same evening count nothing again.
        let mut st = json!({});
        let mut me2 = v["player"].clone();
        for m in [SLOT_END[1], SLOT_END[1] + 1, SLOT_END[1] + 2] {
            let r = tick(&json!({"day": 12, "minute": m, "player": me2, "heroines": [], "state": st})).unwrap();
            me2 = r["player"].clone();
            if !r["state"].is_null() {
                st = r["state"].clone();
            }
        }
        let held: i64 = me2["courses"].as_array().unwrap().iter().map(|c| c["held"].as_i64().unwrap()).sum();
        assert!(held <= 2, "held {held}");
        // Finals Friday 17:00: graded.
        let mut me: Academic = serde_json::from_value(v["player"].clone()).unwrap();
        me.intelligence = 90.0;
        for c in me.courses.iter_mut() {
            c.midterm = 90.0;
        }
        let fri = campus::game_day(t.fin + 4);
        let v3 = tick(&json!({"day": fri, "minute": 17 * 60, "player": me, "heroines": []})).unwrap();
        assert!(v3["player"]["courses"][0]["grade"].as_f64().unwrap() >= 60.0);
        assert!(v3["message"].as_str().unwrap().contains("期末成績"));
        assert!(v3["effects"].as_array().unwrap().iter().any(|e| e["op"] == "bank"), "a 90 student gets the scholarship");
    }

    #[test]
    fn the_office() {
        let v = office(&json!({"action": "leave", "day": 20, "player": {"dept": "csie", "status": "enrolled", "term": "2010-1"}})).unwrap();
        assert_eq!((v["player"]["status"].as_str(), v["player"]["leave_until"].as_str()), (Some("leave"), Some("2010-2")));
        let v = office(&json!({"action": "admit", "dept": "design", "day": 20, "player": {"dept": "csie", "intelligence": 90}})).unwrap();
        assert_eq!(v["player"]["dept"], "design");
    }
}
