//! Heroine songs: who sings and when (Docs/HEROINE_SINGING_IMPLEMENTATION_PLAN.md §5). The singer is the heroine who
//! likes him most on the shared 0..100 liking scale (Relation::liking: favor read inside her stage, so a friend's raw
//! favor never outranks a lover's by accident); ties go to the smaller stable id. Only the save's relations count (the
//! heroines he has met), not whoever happens to be loaded. The game asks "may a song be prepared / offered now?" and
//! gets the singer with the reason; making and playing the song is ok-song and the scene.

use serde::{Deserialize, Serialize};

use crate::relation::Relation;

/// Thresholds in one place (the dev control channel may skip them; the automatic event never does).
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct SongTuning {
    /// Real play time (minutes) before the first song of a session.
    pub min_play_minutes: f64,
    /// Her liking (0..100) needed to sing for him.
    pub min_liking: f32,
    /// At most this many sung songs per game day.
    pub per_day: u32,
    /// Real play minutes after a song before the next may be prepared.
    pub cooldown_minutes: f64,
    /// Real play minutes after he declines an invitation before another is offered.
    pub decline_cooldown_minutes: f64,
}

impl Default for SongTuning {
    fn default() -> Self {
        Self { min_play_minutes: 10.0, min_liking: 50.0, per_day: 1, cooldown_minutes: 30.0, decline_cooldown_minutes: 20.0 }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct Ranked {
    pub id: String,
    pub liking: f32,
    pub stage: String,
}

/// The heroines he has met, most liked first; ties by stable id. Relations with a non-finite favor or an empty id are
/// left out (bad data never sings).
pub fn ranking(relations: &[Relation]) -> Vec<Ranked> {
    let mut out: Vec<Ranked> = relations.iter()
        .filter(|r| !r.id.is_empty() && r.favor.is_finite())
        .map(|r| Ranked { id: r.id.clone(), liking: r.liking(), stage: r.stage.clone() })
        .filter(|r| r.liking.is_finite())
        .collect();
    out.sort_by(|a, b| b.liking.partial_cmp(&a.liking).unwrap_or(std::cmp::Ordering::Equal).then_with(|| a.id.cmp(&b.id)));
    out
}

/// What the scene knows when it asks.
#[derive(Clone, Debug, Default, Deserialize)]
#[serde(default)]
pub struct SongAsk {
    /// Real play minutes this session.
    pub played_minutes: f64,
    pub day: i32,
    /// Songs sung on `day`.
    pub sung_today: u32,
    /// Play minutes at the end of the last song (None: none yet).
    pub last_song_at: Option<f64>,
    /// Play minutes when he last declined (None: never).
    pub declined_at: Option<f64>,
    /// An event already in progress (preparing, ready, offered, singing): no second one.
    pub event_active: bool,
    /// Development: skip the time and liking thresholds (never set by the automatic event).
    #[serde(default)]
    pub override_thresholds: bool,
}

#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct SongDecision {
    pub ok: bool,
    /// "ok", "no_heroines", "too_early", "liking_low", "sung_today", "cooldown", "declined_recently", "event_active"
    pub reason: String,
    pub singer: Option<Ranked>,
    pub ranking: Vec<Ranked>,
    pub override_thresholds: bool,
}

/// May a song be prepared now, and by whom. The singer is always the top of the ranking: when she cannot sing yet the
/// answer is "not now", never the second heroine.
pub fn decide(relations: &[Relation], ask: &SongAsk, t: &SongTuning) -> SongDecision {
    let ranking = ranking(relations);
    let top = ranking.first().cloned();
    let no = |reason: &str| SongDecision { ok: false, reason: reason.into(), singer: top.clone(), ranking: ranking.clone(), override_thresholds: ask.override_thresholds };
    let Some(singer) = top.clone() else { return no("no_heroines") };
    if ask.event_active {
        return no("event_active");
    }
    if !ask.override_thresholds {
        if ask.played_minutes < t.min_play_minutes {
            return no("too_early");
        }
        if singer.liking < t.min_liking {
            return no("liking_low");
        }
        if ask.sung_today >= t.per_day {
            return no("sung_today");
        }
        if ask.last_song_at.is_some_and(|at| ask.played_minutes - at < t.cooldown_minutes) {
            return no("cooldown");
        }
        if ask.declined_at.is_some_and(|at| ask.played_minutes - at < t.decline_cooldown_minutes) {
            return no("declined_recently");
        }
    }
    SongDecision { ok: true, reason: "ok".into(), singer: Some(singer), ranking, override_thresholds: ask.override_thresholds }
}

/// One lyric sheet: the text shown (Traditional Chinese) and the text the song model reads (Simplified), with a style.
struct Sheet {
    id: &'static str,
    stages: &'static [&'static str],
    /// Hours of the street clock it suits (inclusive start, exclusive end; wraps past midnight).
    hours: (u32, u32),
    display: &'static str,
    model: &'static str,
    tags: &'static [&'static str],
}

const SHEETS: &[Sheet] = &[
    Sheet { id: "evening_walk", stages: &["friend", "lover"], hours: (16, 23),
            display: "[主歌]\n放學後的街角 路燈慢慢亮起\n你說今天的風 好像特別溫柔\n[副歌]\n晚風吹過街角的燈\n你走在我身邊慢慢等\n把今天的笑聲都收好\n明天還要一起走一程",
            model: "[Verse]\n放学后的街角 路灯慢慢亮起\n你说今天的风 好像特别温柔\n\n[Chorus]\n晚风吹过街角的灯\n你走在我身边慢慢等\n把今天的笑声都收好\n明天还要一起走一程",
            tags: &["piano", "warm", "female vocal", "ballad", "mandarin pop"] },
    Sheet { id: "sunny_day", stages: &["stranger", "friend"], hours: (6, 16),
            display: "[主歌]\n陽光落在窗邊 書頁輕輕翻動\n你笑著跟我說 早安這個城市\n[副歌]\n一起走過熱鬧的街\n風把影子拉得很長很長\n這樣普通的一天\n因為你也變得閃閃發亮",
            model: "[Verse]\n阳光落在窗边 书页轻轻翻动\n你笑着跟我说 早安这个城市\n\n[Chorus]\n一起走过热闹的街\n风把影子拉得很长很长\n这样普通的一天\n因为你也变得闪闪发亮",
            tags: &["acoustic guitar", "bright", "female vocal", "pop", "upbeat"] },
    Sheet { id: "lover_night", stages: &["lover"], hours: (20, 6),
            display: "[主歌]\n夜色裡的街道 只剩我們兩個\n手心裡的溫度 比星星還要清楚\n[副歌]\n就這樣靠著你\n把心跳慢慢說給你聽\n不用說太多的話\n你在這裡就很好",
            model: "[Verse]\n夜色里的街道 只剩我们两个\n手心里的温度 比星星还要清楚\n\n[Chorus]\n就这样靠着你\n把心跳慢慢说给你听\n不用说太多的话\n你在这里就很好",
            tags: &["piano", "soft", "female vocal", "ballad", "romantic"] },
];

fn in_hours(hour: u32, (start, end): (u32, u32)) -> bool {
    if start <= end { hour >= start && hour < end } else { hour >= start || hour < end }
}

/// The lyric sheet for her stage and the hour; the same day and stage always give the same sheet (a cached song is
/// reused, and switching slots does not reshuffle it).
pub fn lyrics(stage: &str, hour: u32, day: i32) -> serde_json::Value {
    let fits: Vec<&Sheet> = SHEETS.iter().filter(|s| s.stages.contains(&stage) && in_hours(hour, s.hours)).collect();
    let pool: Vec<&Sheet> = if fits.is_empty() { SHEETS.iter().filter(|s| s.stages.contains(&stage)).collect() } else { fits };
    let pool = if pool.is_empty() { SHEETS.iter().collect() } else { pool };
    let sheet = pool[(day.unsigned_abs() as usize) % pool.len()];
    serde_json::json!({"template": sheet.id, "lyrics_display": sheet.display, "lyrics_model": sheet.model, "tags": sheet.tags, "max_duration_ms": 30000})
}

/// A place he and she have been to together, as it is sung: a short name and one line of what they did there.
struct Spot {
    place: &'static str,
    name: &'static str,
    line: &'static str,
}

const SPOTS: &[Spot] = &[
    Spot { place: "library", name: "圖書館", line: "我們翻著同一本書" },
    Spot { place: "cafeteria", name: "學生餐廳", line: "一起吃著熱熱的早餐" },
    Spot { place: "bookstore", name: "書局", line: "你陪我挑了一本新書" },
    Spot { place: "park", name: "中央公園", line: "沿著小路慢慢地走" },
    Spot { place: "aquarium", name: "水族館", line: "看魚群游過藍色的光" },
    Spot { place: "drinkshop", name: "飲料店", line: "兩杯手搖飲一樣的甜" },
    Spot { place: "cafe", name: "咖啡廳", line: "咖啡香留在窗邊" },
    Spot { place: "restaurant", name: "餐廳", line: "燈光下一起吃晚餐" },
    Spot { place: "snack", name: "點心店", line: "分著同一份甜點" },
    Spot { place: "department", name: "百貨公司", line: "逛過一層又一層" },
    Spot { place: "shrine", name: "神社", line: "一起許下小小的願" },
    Spot { place: "onsen", name: "溫泉", line: "熱氣裡你的笑臉" },
    Spot { place: "playground", name: "操場", line: "跑道上吹著晚風" },
    Spot { place: "campus_sports", name: "運動場", line: "看台上吹著晚風" },
    Spot { place: "conveni", name: "便利商店", line: "深夜的燈還亮著" },
    Spot { place: "fruitshop", name: "水果店", line: "挑著最甜的那一顆" },
    Spot { place: "petshop", name: "寵物店", line: "小狗對著我們笑" },
];

/// Memory kinds that are something they did together (ok-mind records with the player as an entity).
const SHARED_KINDS: &[&str] = &["activity", "outing", "date", "treat", "talk"];

/// Up to two places from her memories with him, most important first (then the latest), each place once.
fn shared_spots(shared: &[serde_json::Value]) -> Vec<&'static Spot> {
    let num = |m: &serde_json::Value, k: &str| m.get(k).and_then(serde_json::Value::as_f64).unwrap_or(0.0);
    let mut ranked: Vec<&serde_json::Value> = shared.iter().collect();
    ranked.sort_by(|a, b| num(b, "importance").total_cmp(&num(a, "importance")).then(num(b, "t").total_cmp(&num(a, "t"))));
    let mut out: Vec<&'static Spot> = Vec::new();
    for m in ranked {
        let kind = m.get("kind").and_then(serde_json::Value::as_str).unwrap_or("");
        let place = m.get("place").and_then(serde_json::Value::as_str).unwrap_or("");
        if !SHARED_KINDS.contains(&kind) {
            continue;
        }
        if let Some(s) = SPOTS.iter().find(|s| s.place == place) {
            if !out.iter().any(|o| o.place == s.place) {
                out.push(s);
            }
        }
        if out.len() == 2 {
            break;
        }
    }
    out
}

/// `lyrics` with the verse made from places they have been to together (Docs/HEROINE_SINGING_IMPLEMENTATION_PLAN.md
/// section 6: shared experiences fill a template); the chorus and style stay those of the sheet. Without such memories
/// it is the sheet as it is.
pub fn lyrics_with(stage: &str, hour: u32, day: i32, shared: &[serde_json::Value]) -> serde_json::Value {
    let mut v = lyrics(stage, hour, day);
    let spots = shared_spots(shared);
    if spots.is_empty() {
        return v;
    }
    let display = v["lyrics_display"].as_str().unwrap_or("").to_string();
    let chorus = display.split_once("[副歌]").map(|(_, c)| c.trim_start_matches('\n').to_string()).unwrap_or_default();
    let verse = match spots.as_slice() {
        [a] => format!("還記得{}的那天\n{}\n你說過的每一句話\n我都悄悄記在心上", a.name, a.line),
        [a, b, ..] => format!("還記得{}的那天\n{}\n還有在{}的時候\n{}", a.name, a.line, b.name, b.line),
        [] => unreachable!(),
    };
    let display = format!("[主歌]\n{verse}\n[副歌]\n{chorus}");
    let model = format!("[Verse]\n{}\n\n[Chorus]\n{}", zhconv::zhconv(&verse, zhconv::Variant::ZhCN), zhconv::zhconv(&chorus, zhconv::Variant::ZhCN));
    let template = format!("{}+{}", v["template"].as_str().unwrap_or(""), spots.iter().map(|s| s.place).collect::<Vec<_>>().join("+"));
    v["template"] = template.into();
    v["lyrics_display"] = display.into();
    v["lyrics_model"] = model.into();
    v
}

pub fn eval(kind: &str, request: &serde_json::Value) -> Option<Result<serde_json::Value, String>> {
    match kind {
        // request: heroines [relation...], ask {played_minutes, day, sung_today, last_song_at, declined_at, event_active,
        // override_thresholds}, tuning (optional overrides) -> SongDecision
        "song_decide" => Some((|| {
            let relations: Vec<Relation> = serde_json::from_value(request.get("heroines").cloned().unwrap_or_default()).map_err(|e| format!("heroines: {e}"))?;
            let ask: SongAsk = serde_json::from_value(request.get("ask").cloned().unwrap_or_default()).map_err(|e| format!("ask: {e}"))?;
            let tuning: SongTuning = match request.get("tuning") {
                Some(t) if !t.is_null() => serde_json::from_value(t.clone()).map_err(|e| format!("tuning: {e}"))?,
                _ => SongTuning::default(),
            };
            serde_json::to_value(decide(&relations, &ask, &tuning)).map_err(|e| e.to_string())
        })()),
        // request: stage, hour, day, shared (optional: her ok-mind memories with him, best first) -> the lyric sheet
        "song_lyrics" => {
            let stage = request.get("stage").and_then(serde_json::Value::as_str).unwrap_or("friend");
            let hour = request.get("hour").and_then(serde_json::Value::as_u64).unwrap_or(18) as u32;
            let day = request.get("day").and_then(serde_json::Value::as_i64).unwrap_or(0) as i32;
            let shared = request.get("shared").and_then(serde_json::Value::as_array).cloned().unwrap_or_default();
            Some(Ok(lyrics_with(stage, hour % 24, day, &shared)))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lyrics_fit_stage_and_hour_and_stay_for_the_day() {
        assert_eq!(lyrics("lover", 22, 5)["template"], lyrics("lover", 22, 5)["template"]);
        assert_eq!(lyrics("friend", 10, 3)["template"], "sunny_day");
        assert_eq!(lyrics("friend", 18, 3)["template"], "evening_walk");
        let night = lyrics("lover", 2, 0);
        assert_eq!(night["template"], "lover_night");
        assert!(night["lyrics_model"].as_str().unwrap().contains("[Chorus]"));
        let d = eval("song_decide", &serde_json::json!({"heroines": [{"id": "apng-a", "stage": "lover", "favor": 50.0}], "ask": {"played_minutes": 20.0, "day": 1}})).unwrap().unwrap();
        assert_eq!(d["singer"]["id"], "apng-a");
    }

    #[test]
    fn shared_places_fill_the_verse() {
        let mem = |kind: &str, place: &str| serde_json::json!({"kind": kind, "place": place, "importance": 0.5});
        // dorms and plain encounters are not songs; the first two shared places are, each once
        let shared = vec![mem("encounter", "park"), mem("activity", "girls_dorm"), mem("activity", "library"), mem("outing", "library"),
                          mem("date", "aquarium"), mem("treat", "cafe")];
        let v = lyrics_with("lover", 22, 5, &shared);
        assert_eq!(v["template"], "lover_night+library+aquarium");
        let d = v["lyrics_display"].as_str().unwrap();
        assert!(d.starts_with("[主歌]\n還記得圖書館的那天\n"), "{d}");
        assert!(d.contains("還有在水族館的時候"));
        assert!(d.contains("[副歌]\n就這樣靠著你"));
        let m = v["lyrics_model"].as_str().unwrap();
        assert!(m.contains("还记得图书馆的那天") && m.contains("[Chorus]\n就这样靠着你"), "{m}");
        // one place: the second couplet is the fixed one
        let one = lyrics_with("friend", 10, 3, &[mem("activity", "cafeteria")]);
        assert_eq!(one["template"], "sunny_day+cafeteria");
        assert!(one["lyrics_display"].as_str().unwrap().contains("我都悄悄記在心上"));
        // nothing shared: the sheet as it is
        assert_eq!(lyrics_with("friend", 10, 3, &[mem("encounter", "park")]), lyrics("friend", 10, 3));
        let e = eval("song_lyrics", &serde_json::json!({"stage": "lover", "hour": 22, "day": 5, "shared": shared})).unwrap().unwrap();
        assert_eq!(e["template"], "lover_night+library+aquarium");
    }

    fn her(id: &str, stage: &str, liking: f32) -> Relation {
        let mut r = Relation { id: id.into(), stage: stage.into(), ..Default::default() };
        r.set_liking(liking);
        r
    }

    fn ask(minutes: f64) -> SongAsk {
        SongAsk { played_minutes: minutes, day: 1, ..Default::default() }
    }

    #[test]
    fn a_lover_outranks_a_friend_with_more_raw_favor() {
        let mut friend = her("apng-b", "friend", 64.0);
        friend.favor = friend.favor_cap(); // the friend's favor is maxed: liking 65 at most
        let lover = her("apng-a", "lover", 70.0);
        let r = ranking(&[friend.clone(), lover.clone()]);
        assert_eq!(r[0].id, "apng-a");
        assert!(friend.favor >= 0.0 && r[0].liking > r[1].liking);
    }

    #[test]
    fn ties_go_to_the_smaller_id_and_bad_data_is_left_out() {
        let mut nan = her("apng-z", "lover", 90.0);
        nan.favor = f32::NAN;
        let r = ranking(&[her("apng-c", "friend", 50.0), her("apng-b", "friend", 50.0), nan, her("", "lover", 99.0)]);
        assert_eq!(r.iter().map(|x| x.id.as_str()).collect::<Vec<_>>(), vec!["apng-b", "apng-c"]);
    }

    #[test]
    fn thresholds_and_cooldowns() {
        let t = SongTuning::default();
        let rel = [her("apng-a", "lover", 80.0), her("apng-b", "friend", 40.0)];
        assert_eq!(decide(&[], &ask(60.0), &t).reason, "no_heroines");
        assert_eq!(decide(&rel, &ask(5.0), &t).reason, "too_early");
        let d = decide(&rel, &ask(15.0), &t);
        assert!(d.ok && d.singer.unwrap().id == "apng-a");
        assert_eq!(decide(&[her("apng-a", "friend", 45.0)], &ask(15.0), &t).reason, "liking_low");
        assert_eq!(decide(&rel, &SongAsk { sung_today: 1, ..ask(90.0) }, &t).reason, "sung_today");
        assert_eq!(decide(&rel, &SongAsk { last_song_at: Some(70.0), ..ask(90.0) }, &t).reason, "cooldown");
        assert_eq!(decide(&rel, &SongAsk { declined_at: Some(80.0), ..ask(90.0) }, &t).reason, "declined_recently");
        assert_eq!(decide(&rel, &SongAsk { event_active: true, ..ask(90.0) }, &t).reason, "event_active");
        let dev = decide(&rel, &SongAsk { override_thresholds: true, ..ask(0.0) }, &t);
        assert!(dev.ok && dev.override_thresholds);
    }
}
