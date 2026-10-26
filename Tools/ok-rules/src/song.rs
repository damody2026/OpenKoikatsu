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

/// One lyric sheet: the text shown (Traditional Chinese), sectioned by [主歌] / [副歌] / [橋段] and written out in full
/// (a chorus sung three times is written three times: the caption follows the song line by line). The text the song
/// model reads is made from it (`model_text`: Simplified, [Verse] / [Chorus] / [Bridge]). `occasion`: the sheet of a
/// milestone song (song_milestone) rather than one for her stage and the hour.
struct Sheet {
    id: &'static str,
    stages: &'static [&'static str],
    /// Hours of the street clock it suits (inclusive start, exclusive end; wraps past midnight).
    hours: (u32, u32),
    occasion: &'static str,
    display: &'static str,
    tags: &'static [&'static str],
}

/// A full song, a minute and a half: two verses and the chorus three times. The song model does not stop when the sheet
/// ends (it sings on to the limit), so the limit is the length; the last two seconds fade out (ok-song audio::mix).
pub const SONG_MS: u32 = 90_000;

const SHEETS: &[Sheet] = &[
    Sheet { id: "evening_walk", stages: &["friend", "lover"], hours: (16, 23), occasion: "",
            display: "[主歌]\n放學後的街角 路燈慢慢亮起\n你說今天的風 好像特別溫柔\n[副歌]\n晚風吹過街角的燈\n你走在我身邊慢慢等\n把今天的笑聲都收好\n明天還要一起走一程\n\
                      [主歌]\n影子並肩走著 一長一短的樣子\n你忽然停下來 指著第一顆星\n[副歌]\n晚風吹過街角的燈\n你走在我身邊慢慢等\n把今天的笑聲都收好\n明天還要一起走一程\n\
                      [副歌]\n晚風吹過街角的燈\n你走在我身邊慢慢等\n把今天的笑聲都收好\n明天還要一起走一程",
            tags: &["piano", "warm", "female vocal", "ballad", "mandarin pop"] },
    Sheet { id: "sunny_day", stages: &["stranger", "friend"], hours: (6, 16), occasion: "",
            display: "[主歌]\n陽光落在窗邊 書頁輕輕翻動\n你笑著跟我說 早安這個城市\n[副歌]\n一起走過熱鬧的街\n風把影子拉得很長很長\n這樣普通的一天\n因為你也變得閃閃發亮\n\
                      [主歌]\n路口的麵包香 飄進我們的話題\n你說下次一起 去更遠的地方\n[副歌]\n一起走過熱鬧的街\n風把影子拉得很長很長\n這樣普通的一天\n因為你也變得閃閃發亮\n\
                      [副歌]\n一起走過熱鬧的街\n風把影子拉得很長很長\n這樣普通的一天\n因為你也變得閃閃發亮",
            tags: &["acoustic guitar", "bright", "female vocal", "pop", "upbeat"] },
    Sheet { id: "lover_night", stages: &["lover"], hours: (20, 6), occasion: "",
            display: "[主歌]\n夜色裡的街道 只剩我們兩個\n手心裡的溫度 比星星還要清楚\n[副歌]\n就這樣靠著你\n把心跳慢慢說給你聽\n不用說太多的話\n你在這裡就很好\n\
                      [主歌]\n路燈一盞一盞 替我們數著腳步\n晚安說了好幾次 還是捨不得走\n[副歌]\n就這樣靠著你\n把心跳慢慢說給你聽\n不用說太多的話\n你在這裡就很好\n\
                      [副歌]\n就這樣靠著你\n把心跳慢慢說給你聽\n不用說太多的話\n你在這裡就很好",
            tags: &["piano", "soft", "female vocal", "ballad", "romantic"] },
    // Milestones (song_milestone): the day she became his lover, her favor at 100 and at its height, and every 500 lines
    // of talk after that.
    Sheet { id: "confession", stages: &["lover"], hours: (0, 24), occasion: "confession",
            display: "[主歌]\n原來那些心跳 不是我的錯覺\n你說出口的時候 世界忽然好安靜\n[副歌]\n從今天開始\n我是你的戀人\n把害羞藏在笑容裡\n這一刻我會一直記得\n\
                      [主歌]\n好多次想開口 又把話吞回去\n謝謝你比我勇敢 先走向了我\n[副歌]\n從今天開始\n我是你的戀人\n把害羞藏在笑容裡\n這一刻我會一直記得\n\
                      [副歌]\n從今天開始\n我是你的戀人\n把害羞藏在笑容裡\n這一刻我會一直記得",
            tags: &["piano", "strings", "female vocal", "ballad", "romantic"] },
    Sheet { id: "favor100", stages: &["lover"], hours: (0, 24), occasion: "favor100",
            display: "[主歌]\n習慣了你的聲音 習慣了你的步調\n一天沒見到你 就覺得少了什麼\n[副歌]\n越來越喜歡你\n比昨天再多一點點\n你說的每句玩笑\n我都偷偷放在心裡面\n\
                      [主歌]\n一起走過的地方 都變成我的地圖\n每個轉角的回憶 都有你的笑容\n[副歌]\n越來越喜歡你\n比昨天再多一點點\n你說的每句玩笑\n我都偷偷放在心裡面\n\
                      [副歌]\n越來越喜歡你\n比昨天再多一點點\n你說的每句玩笑\n我都偷偷放在心裡面",
            tags: &["acoustic guitar", "warm", "female vocal", "pop", "sweet"] },
    Sheet { id: "favor150", stages: &["lover"], hours: (0, 24), occasion: "favor150",
            display: "[主歌]\n從陌生到熟悉 從朋友到戀人\n一路走到這裡 每一步都是你\n[副歌]\n一直一直在一起\n春天夏天秋天冬天\n就算世界很大很大\n我只想待在你身邊\n\
                      [主歌]\n吵過架也和好 哭過也笑過\n你的手還是那樣 溫暖得剛剛好\n[副歌]\n一直一直在一起\n春天夏天秋天冬天\n就算世界很大很大\n我只想待在你身邊\n\
                      [副歌]\n一直一直在一起\n春天夏天秋天冬天\n就算世界很大很大\n我只想待在你身邊",
            tags: &["piano", "strings", "female vocal", "ballad", "emotional"] },
    Sheet { id: "lines", stages: &["lover"], hours: (0, 24), occasion: "lines",
            display: "[主歌]\n我們聊了好多 從早晨聊到夜晚\n說不完的小事 你都認真地聽\n[副歌]\n跟你說話的時候\n時間總是跑得好快\n再多說一句就好\n今天也想跟你在一起\n\
                      [主歌]\n喜歡的歌 討厭的菜\n你都記得 清清楚楚\n[副歌]\n跟你說話的時候\n時間總是跑得好快\n再多說一句就好\n今天也想跟你在一起\n\
                      [副歌]\n跟你說話的時候\n時間總是跑得好快\n再多說一句就好\n今天也想跟你在一起",
            tags: &["acoustic guitar", "bright", "female vocal", "pop", "upbeat"] },
];

fn in_hours(hour: u32, (start, end): (u32, u32)) -> bool {
    if start <= end { hour >= start && hour < end } else { hour >= start || hour < end }
}

/// The song model's text of a sheet: Simplified Chinese, sections [Verse] / [Chorus] / [Bridge], a blank line between.
fn model_text(display: &str) -> String {
    let mut out = String::new();
    for line in display.lines() {
        let l = line.trim();
        if l.is_empty() {
            continue;
        }
        let mark = match l {
            "[主歌]" => Some("[Verse]"),
            "[副歌]" => Some("[Chorus]"),
            "[橋段]" => Some("[Bridge]"),
            _ => None,
        };
        match mark {
            Some(m) => {
                if !out.is_empty() {
                    out.push('\n');
                }
                out.push_str(m);
            }
            None => out.push_str(&zhconv::zhconv(l, zhconv::Variant::ZhCN)),
        }
        out.push('\n');
    }
    out.trim_end().to_string()
}

fn sheet_json(sheet: &Sheet, display: &str, template: &str) -> serde_json::Value {
    serde_json::json!({"template": template, "lyrics_display": display, "lyrics_model": model_text(display), "tags": sheet.tags,
                       "max_duration_ms": SONG_MS, "occasion": sheet.occasion})
}

/// The lyric sheet for her stage and the hour; the same day and stage always give the same sheet (a cached song is
/// reused, and switching slots does not reshuffle it). A milestone's occasion picks its own sheet.
pub fn lyrics(stage: &str, hour: u32, day: i32) -> serde_json::Value {
    lyrics_for(stage, hour, day, "")
}

pub fn lyrics_for(stage: &str, hour: u32, day: i32, occasion: &str) -> serde_json::Value {
    if let Some(sheet) = SHEETS.iter().find(|s| !occasion.is_empty() && s.occasion == occasion) {
        return sheet_json(sheet, sheet.display, sheet.id);
    }
    let general = || SHEETS.iter().filter(|s| s.occasion.is_empty());
    let fits: Vec<&Sheet> = general().filter(|s| s.stages.contains(&stage) && in_hours(hour, s.hours)).collect();
    let pool: Vec<&Sheet> = if fits.is_empty() { general().filter(|s| s.stages.contains(&stage)).collect() } else { fits };
    let pool = if pool.is_empty() { general().collect() } else { pool };
    let sheet = pool[(day.unsigned_abs() as usize) % pool.len()];
    sheet_json(sheet, sheet.display, sheet.id)
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
    lyrics_with_for(stage, hour, day, "", shared)
}

/// The first verse made from their places; the confession and the height of her love keep their own words.
pub fn lyrics_with_for(stage: &str, hour: u32, day: i32, occasion: &str, shared: &[serde_json::Value]) -> serde_json::Value {
    let mut v = lyrics_for(stage, hour, day, occasion);
    let spots = shared_spots(shared);
    if spots.is_empty() || matches!(occasion, "confession" | "favor150") {
        return v;
    }
    let display = v["lyrics_display"].as_str().unwrap_or("").to_string();
    let verse = match spots.as_slice() {
        [a] => format!("還記得{}的那天\n{}\n你說過的每一句話\n我都悄悄記在心上", a.name, a.line),
        [a, b, ..] => format!("還記得{}的那天\n{}\n還有在{}的時候\n{}", a.name, a.line, b.name, b.line),
        [] => unreachable!(),
    };
    // The first [主歌] section's lines give way to the verse of their places; everything after it stays.
    let rest = display.split_once("[主歌]\n").map(|(_, r)| r).unwrap_or(&display);
    let rest = rest.find("\n[").map(|i| &rest[i + 1..]).unwrap_or("");
    let display = format!("[主歌]\n{verse}\n{rest}");
    let template = format!("{}+{}", v["template"].as_str().unwrap_or(""), spots.iter().map(|s| s.place).collect::<Vec<_>>().join("+"));
    v["template"] = template.into();
    v["lyrics_model"] = model_text(&display).into();
    v["lyrics_display"] = display.into();
    v
}

/// One heroine as song_milestone sees her: her stage and favor, the milestone songs she has sung, and the lines of talk
/// with him since her last song.
#[derive(Clone, Debug, Default, Deserialize)]
#[serde(default)]
pub struct SongProgress {
    pub id: String,
    pub stage: String,
    pub favor: f32,
    pub done: Vec<String>,
    pub lines: u32,
}

/// Lines of talk with her between songs once the milestones are sung.
pub const LINES_PER_SONG: u32 = 500;

/// Her next milestone song: (occasion, due now). The day she becomes his lover; then her favor reaching 100 and its
/// height (the lover cap, 150); after those, every LINES_PER_SONG lines of talk. Not yet due but close ("near": a
/// friend who likes him a lot, favor 85 / 135, 400 lines), the song is made ahead (a full song takes minutes to make)
/// and waits until it is due.
pub fn milestone(p: &SongProgress) -> Option<(&'static str, bool)> {
    let done = |o: &str| p.done.iter().any(|d| d == o);
    let rel = Relation { id: p.id.clone(), stage: p.stage.clone(), favor: p.favor, ..Default::default() };
    match p.stage.as_str() {
        "lover" if !done("confession") => Some(("confession", true)),
        "lover" if !done("favor100") => (p.favor >= 85.0).then_some(("favor100", p.favor >= 100.0)),
        "lover" if !done("favor150") => (p.favor >= 135.0).then_some(("favor150", p.favor >= LOVER_TOP_FAVOR)),
        "lover" => (p.lines >= LINES_PER_SONG * 4 / 5).then_some(("lines", p.lines >= LINES_PER_SONG)),
        "friend" if !done("confession") => (rel.liking() >= 60.0).then_some(("confession", false)),
        _ => None,
    }
}

/// Her favor at its height as a lover (the cap, 150; a hair under it counts).
const LOVER_TOP_FAVOR: f32 = crate::relation::LOVER_FAVOR_CAP - 0.5;

/// Over all heroines: the milestone song due now (the earliest kind first, then the one who likes him most), else the
/// one to make ahead.
pub fn milestone_pick(heroines: &[SongProgress]) -> serde_json::Value {
    let order = |o: &str| ["confession", "favor100", "favor150", "lines"].iter().position(|x| *x == o).unwrap_or(9);
    let mut found: Vec<(&SongProgress, &str, bool)> = heroines.iter().filter(|h| !h.id.is_empty() && h.favor.is_finite())
        .filter_map(|h| milestone(h).map(|(o, due)| (h, o, due))).collect();
    found.sort_by(|a, b| b.2.cmp(&a.2).then(order(a.1).cmp(&order(b.1))).then(b.0.favor.total_cmp(&a.0.favor)).then(a.0.id.cmp(&b.0.id)));
    match found.first() {
        Some((h, o, due)) => serde_json::json!({"id": h.id, "occasion": o, "due": due}),
        None => serde_json::Value::Null,
    }
}


/// Musical staging for her song ("song_choreo"), like a number in a stage musical: she sways while the introduction
/// plays, gives each verse line a gesture, dances through the chorus and ends on a pose with the last note. The phrases
/// are the voiced stretches of the song's mouth envelope; the first `verse` of them are the verse (one per half line of
/// the sheet's [主歌]), the rest the chorus. Clips are library clips (motions.toml's source), all standing ones.
mod choreo {
    /// Before her first note: a light, bouncy wait.
    pub const PRELUDE_BALLAD: &[&str] = &["lib_0_10_10.okanim"];
    pub const PRELUDE_UPBEAT: &[&str] = &["lib_0_10_7.okanim"];
    /// A verse line: an open pose or a happy gesture, a different one each line.
    pub const VERSE: &[&str] = &["lib_0_1_0.okanim", "lib_0_1_2.okanim", "lib_0_1_5.okanim", "lib_0_1_8.okanim", "lib_0_1_11.okanim",
                                 "lib_0_2_0.okanim", "lib_0_2_2.okanim", "lib_0_2_4.okanim", "lib_0_2_6.okanim"];
    /// The chorus: she dances.
    pub const CHORUS_BALLAD: &[&str] = &["lib_7_22_1.okanim", "lib_7_22_2.okanim"];
    pub const CHORUS_UPBEAT: &[&str] = &["lib_7_22_1.okanim", "lib_7_22_2.okanim", "lib_0_10_8.okanim", "lib_0_10_9.okanim"];
    /// The last note: a finishing pose.
    pub const FINALE: &[&str] = &["lib_12_67_15.okanim", "lib_12_67_16.okanim"];
    /// Her line before the song (a little shy) and after it (pleased).
    pub const INTRO: &[&str] = &["lib_0_11_0.okanim", "lib_0_11_1.okanim"];
    pub const OUTRO: &[&str] = &["lib_0_2_1.okanim", "lib_0_2_3.okanim"];
    /// A clip is held at least this long (each change blends through her stand and the next clip has to be streamed
    /// in); shorter phrases keep the previous one.
    pub const MIN_HOLD: f32 = 4.0;
}

/// Voiced stretches (start, end seconds) of a mouth envelope: a light smoothing, a threshold at a fifth of the loud
/// level, gaps under 0.35 s bridged and stretches under 0.25 s dropped.
pub fn phrases(env: &[f32], hop: f32) -> Vec<(f32, f32)> {
    if env.is_empty() || hop <= 0.0 {
        return Vec::new();
    }
    let smooth: Vec<f32> = (0..env.len()).map(|i| {
        let (a, b) = (i.saturating_sub(3), (i + 4).min(env.len()));
        env[a..b].iter().sum::<f32>() / (b - a) as f32
    }).collect();
    let mut sorted = smooth.clone();
    sorted.sort_by(|a, b| a.total_cmp(b));
    let loud = sorted[(sorted.len() * 95 / 100).min(sorted.len() - 1)];
    if loud <= 0.0 {
        return Vec::new();
    }
    let on = loud * 0.2;
    let mut out: Vec<(f32, f32)> = Vec::new();
    let mut start = None;
    for (i, v) in smooth.iter().enumerate() {
        let t = i as f32 * hop;
        match (start, *v > on) {
            (None, true) => start = Some(t),
            (Some(s), false) => {
                out.push((s, t));
                start = None;
            }
            _ => {}
        }
    }
    if let Some(s) = start {
        out.push((s, env.len() as f32 * hop));
    }
    let mut merged: Vec<(f32, f32)> = Vec::new();
    for p in out {
        match merged.last_mut() {
            Some(last) if p.0 - last.1 < 0.35 => last.1 = p.1,
            _ => merged.push(p),
        }
    }
    merged.retain(|(s, e)| e - s >= 0.25);
    merged
}

/// The sung units of the sheet in order, each with its part: half lines (split at spaces) under [主歌] / [Verse] and
/// [橋段] / [Bridge], whole lines under [副歌] / [Chorus]. Lines under any other mark are not sung units.
fn units(lyrics: &str) -> Vec<(&'static str, String)> {
    let (mut out, mut part) = (Vec::new(), "");
    for line in lyrics.lines() {
        let l = line.trim();
        if l.starts_with('[') {
            let low = l.to_lowercase();
            part = if l.contains("主歌") || low.contains("verse") {
                "verse"
            } else if l.contains("副歌") || low.contains("chorus") {
                "chorus"
            } else if l.contains("橋段") || l.contains("桥段") || low.contains("bridge") {
                "bridge"
            } else {
                ""
            };
            continue;
        }
        match (part, l.is_empty()) {
            ("verse" | "bridge", false) => out.extend(l.split_whitespace().map(|h| (part, h.to_string()))),
            ("chorus", false) => out.push((part, l.to_string())),
            _ => {}
        }
    }
    out
}

/// The sung units' texts (see units).
pub fn unit_texts(lyrics: &str) -> Vec<String> {
    units(lyrics).into_iter().map(|(_, t)| t).collect()
}

/// The song's lines on its clock when the breaths do not show them (sung legato, the envelope rarely falls silent):
/// the voiced span split evenly by the number of lines, each boundary moved to the quietest moment within 0.5 s.
pub fn even_lines(env: &[f32], hop: f32, span: (f32, f32), lines: usize) -> Vec<(f32, f32)> {
    let (start, end) = span;
    if lines == 0 || end <= start {
        return vec![span];
    }
    let step = (end - start) / lines as f32;
    let mut cuts = vec![start];
    for k in 1..lines {
        let guess = start + step * k as f32;
        let (a, b) = (((guess - 0.5) / hop).max(0.0) as usize, (((guess + 0.5) / hop) as usize).min(env.len().saturating_sub(1)));
        let quiet = (a..=b).min_by(|x, y| env[*x].total_cmp(&env[*y])).map(|i| i as f32 * hop).unwrap_or(guess);
        cuts.push(quiet.max(*cuts.last().unwrap() + 0.5));
    }
    cuts.push(end);
    cuts.windows(2).map(|w| (w[0], w[1])).collect()
}

/// Caption lines from what she actually sang: `heard` is each breath transcribed (ok-asr /transcribe_spans, one text per
/// breath). The song model does not keep to the sheet (two lines in one breath, a chorus sung once more or once less,
/// a long break), so each breath is matched to the run of sheet lines (1 to 3) its words are closest to, in sheet order
/// where it fits and jumping (a repeated chorus) where it does not; a breath that matches nothing (an "ah" between
/// verses) shows no new line. The words shown are the sheet's (`captions`, the game's script), never the transcription.
/// Returns (at, text) per caption; empty when nothing matched (then the timing comes from the sheet alone).
pub fn align_heard(breaths: &[(f32, f32)], heard: &[String], sung: &[String], captions: &[String]) -> Vec<(f32, String)> {
    let hans = |s: &str| -> Vec<char> {
        zhconv::zhconv(s, zhconv::Variant::ZhCN).chars().filter(|c| ('\u{4e00}'..='\u{9fff}').contains(c) || ('\u{3400}'..='\u{4dbf}').contains(c)).collect()
    };
    let units: Vec<Vec<char>> = sung.iter().map(|u| hans(u)).collect();
    let heard: Vec<Vec<char>> = heard.iter().map(|h| hans(h)).collect();
    let (n, m) = (units.len(), breaths.len().min(heard.len()));
    if n == 0 || m == 0 {
        return Vec::new();
    }
    let distance = |a: &[char], b: &[char]| -> f32 {
        let mut prev: Vec<usize> = (0..=b.len()).collect();
        for i in 1..=a.len() {
            let mut cur = vec![i; b.len() + 1];
            for j in 1..=b.len() {
                cur[j] = (prev[j] + 1).min(cur[j - 1] + 1).min(prev[j - 1] + usize::from(a[i - 1] != b[j - 1]));
            }
            prev = cur;
        }
        prev[b.len()] as f32 / a.len().max(b.len()).max(1) as f32
    };
    const NONE: f32 = 0.6;
    const JUMP: f32 = 0.25;
    const MAX_RUN: usize = 3;
    // cost[k][p]: best for the first k breaths with the sheet's next line p; choice[k][p] = (previous p, run start, run length).
    let inf = f32::INFINITY;
    let mut cost = vec![vec![inf; n + 1]; m + 1];
    let mut choice = vec![vec![(0usize, 0usize, 0usize); n + 1]; m + 1];
    cost[0][0] = 0.0;
    for k in 0..m {
        for p in 0..=n {
            let c = cost[k][p];
            if !c.is_finite() {
                continue;
            }
            // Nothing from the sheet in this breath.
            let none = c + if heard[k].is_empty() { 0.0 } else { NONE };
            if none < cost[k + 1][p] {
                cost[k + 1][p] = none;
                choice[k + 1][p] = (p, 0, 0);
            }
            if heard[k].is_empty() {
                continue;
            }
            for j in 0..n {
                let mut run: Vec<char> = Vec::new();
                for l in 1..=MAX_RUN.min(n - j) {
                    run.extend_from_slice(&units[j + l - 1]);
                    let c2 = c + distance(&run, &heard[k]) + if j == p { 0.0 } else { JUMP };
                    if c2 < cost[k + 1][j + l] {
                        cost[k + 1][j + l] = c2;
                        choice[k + 1][j + l] = (p, j, l);
                    }
                }
            }
        }
    }
    let mut p = (0..=n).min_by(|a, b| cost[m][*a].total_cmp(&cost[m][*b])).unwrap_or(0);
    let mut runs: Vec<(usize, usize, usize)> = Vec::new(); // (breath, start, length)
    for k in (1..=m).rev() {
        let (prev, j, l) = choice[k][p];
        if l > 0 {
            runs.push((k - 1, j, l));
        }
        p = prev;
    }
    runs.reverse();
    let text = |i: usize| captions.get(i).or(sung.get(i)).cloned().unwrap_or_default();
    let mut out = Vec::new();
    for (k, j, l) in runs {
        let (s, e) = breaths[k];
        let lens: Vec<f32> = (j..j + l).map(|i| units[i].len().max(1) as f32).collect();
        let total: f32 = lens.iter().sum();
        let mut acc = 0f32;
        for (o, len) in lens.iter().enumerate() {
            out.push((s + (e - s) * acc / total, text(j + o)));
            acc += len;
        }
    }
    out
}

/// The staging: intro / outro clips and the cues during the song (seconds on the song's clock).
/// `caption`: the sheet in the game's script (the same lines as `lyrics`); its units come back as `lines` [{at, text}]
/// on the song's clock, for a caption that shows the line being sung.
/// `heard`: each breath as transcribed (see align_heard); empty, or not one per breath: the timing comes from the lengths.
pub fn choreograph(env: &[f32], hop: f32, lyrics: &str, caption: &str, template: &str, seed: u64, heard: &[String]) -> serde_json::Value {
    let base = template.split('+').next().unwrap_or("");
    let upbeat = SHEETS.iter().find(|s| s.id == base).is_some_and(|s| s.tags.contains(&"upbeat"));
    let mut rng = seed ^ 0x2545_f491_4f6c_dd1d;
    let mut next = move |n: usize| -> usize {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        (rng % n.max(1) as u64) as usize
    };
    let sung = units(lyrics);
    let verse = sung.iter().filter(|u| u.0 == "verse").count();
    let breaths = phrases(env, hop);
    // The breaths when they show about as many phrases as the sheet has lines, else the lines spread over the voice.
    let ph = if breaths.len() * 4 >= sung.len() * 3 || breaths.is_empty() {
        breaths.clone()
    } else {
        even_lines(env, hop, (breaths[0].0, breaths[breaths.len() - 1].1), sung.len())
    };
    // The part sung in phrase i: the unit at the same place in the sheet (the sections in order: verse, chorus, verse,
    // chorus, bridge, chorus).
    let part_of = |i: usize| -> &'static str {
        if sung.is_empty() {
            return "chorus";
        }
        let k = if ph.len() == sung.len() { i } else { i * sung.len() / ph.len().max(1) };
        sung[k.min(sung.len() - 1)].0
    };
    // Caption lines: from what she sang when each breath was transcribed (align_heard), else from the sheet alone.
    let texts = unit_texts(if caption.is_empty() { lyrics } else { caption });
    let from_heard = if !heard.is_empty() && heard.len() == breaths.len() { align_heard(&breaths, heard, &unit_texts(lyrics), &texts) } else { Vec::new() };
    let timed: Vec<(f32, String)> = if !from_heard.is_empty() {
        from_heard
    } else {
        // One line per breath when the counts match, else the lines spread over the voice.
        let spans = if breaths.len() == texts.len() || breaths.is_empty() {
            breaths.clone()
        } else {
            even_lines(env, hop, (breaths[0].0, breaths[breaths.len() - 1].1), texts.len())
        };
        texts.iter().cloned().zip(spans).map(|(t, (at, _))| (at, t)).collect()
    };
    let lines: Vec<serde_json::Value> = timed.iter()
        .map(|(at, text)| serde_json::json!({"at": ((at - 0.1).max(0.0) * 100.0).round() / 100.0, "text": text})).collect();
    let mut cues: Vec<(f32, String, &str)> = Vec::new();
    fn push(cues: &mut Vec<(f32, String, &'static str)>, at: f32, clip: &str, part: &'static str) {
        if let Some(last) = cues.last() {
            if at - last.0 < choreo::MIN_HOLD || last.1 == clip {
                return;
            }
        }
        cues.push((at.max(0.0), clip.to_string(), part));
    }
    // An introduction only when she starts singing a while in (these songs often start on the first word).
    let prelude = if upbeat { choreo::PRELUDE_UPBEAT } else { choreo::PRELUDE_BALLAD };
    if ph.first().map_or(true, |p| p.0 > 1.5) {
        push(&mut cues, 0.0, prelude[next(prelude.len())], "prelude");
    }
    let chorus = if upbeat { choreo::CHORUS_UPBEAT } else { choreo::CHORUS_BALLAD };
    let mut last_verse = usize::MAX;
    let mut chorus_at = 0usize;
    for (i, (start, _)) in ph.iter().enumerate() {
        if i + 1 == ph.len() && ph.len() > 1 {
            break; // the last phrase keeps its move; the finale comes on its last note
        }
        let part = part_of(i);
        if part != "chorus" {
            // A verse or bridge line: a gesture, a different one each line.
            let mut k = next(choreo::VERSE.len());
            if k == last_verse {
                k = (k + 1) % choreo::VERSE.len();
            }
            last_verse = k;
            push(&mut cues, start - 0.2, choreo::VERSE[k], part);
        } else {
            push(&mut cues, start - 0.2, chorus[chorus_at % chorus.len()], "chorus");
            chorus_at += 1;
        }
    }
    let end = ph.last().map(|p| p.1).unwrap_or(env.len() as f32 * hop);
    // The finishing pose lands on the last note: its clip is streamed in and takes a moment to reach the pose.
    let finale = choreo::FINALE[next(choreo::FINALE.len())];
    let at = (end - 2.5).max(cues.last().map(|c| c.0 + 1.0).unwrap_or(0.0));
    cues.push((at, finale.to_string(), "finale"));
    serde_json::json!({
        "upbeat": upbeat, "phrases": ph.len(), "verse_phrases": verse, "end": end,
        "intro": choreo::INTRO[next(choreo::INTRO.len())], "outro": choreo::OUTRO[next(choreo::OUTRO.len())], "lines": lines,
        "cues": cues.iter().map(|(at, clip, part)| serde_json::json!({"at": (at * 100.0).round() / 100.0, "clip": clip, "part": part})).collect::<Vec<_>>(),
    })
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
        // request: stage, hour, day, shared (optional: her ok-mind memories with him, best first), occasion (optional: a
        // milestone's, from song_milestone) -> the lyric sheet
        "song_lyrics" => {
            let stage = request.get("stage").and_then(serde_json::Value::as_str).unwrap_or("friend");
            let hour = request.get("hour").and_then(serde_json::Value::as_u64).unwrap_or(18) as u32;
            let day = request.get("day").and_then(serde_json::Value::as_i64).unwrap_or(0) as i32;
            let shared = request.get("shared").and_then(serde_json::Value::as_array).cloned().unwrap_or_default();
            let occasion = request.get("occasion").and_then(serde_json::Value::as_str).unwrap_or("");
            Some(Ok(lyrics_with_for(stage, hour % 24, day, occasion, &shared)))
        }
        // request: heroines [{id, stage, favor, done [occasions sung], lines (since her last song)}]
        // -> {pick: {id, occasion, due} | null}: the milestone song due now, else the one to make ahead
        "song_milestone" => Some((|| {
            let heroines: Vec<SongProgress> = serde_json::from_value(request.get("heroines").cloned().unwrap_or_default()).map_err(|e| format!("heroines: {e}"))?;
            Ok(serde_json::json!({"pick": milestone_pick(&heroines), "lines_per_song": LINES_PER_SONG}))
        })()),
        // request: mouth (the song's mouth envelope), hop (seconds per value), lyrics (the displayed sheet), template, seed
        // -> {intro, outro, cues [{at, clip, part}], ...}: her musical staging (choreograph)
        "song_choreo" => {
            let env: Vec<f32> = request.get("mouth").and_then(serde_json::Value::as_array)
                .map(|a| a.iter().map(|v| v.as_f64().unwrap_or(0.0) as f32).collect()).unwrap_or_default();
            let hop = request.get("hop").and_then(serde_json::Value::as_f64).unwrap_or(0.01) as f32;
            let lyrics = request.get("lyrics").and_then(serde_json::Value::as_str).unwrap_or("");
            let template = request.get("template").and_then(serde_json::Value::as_str).unwrap_or("");
            let seed = request.get("seed").and_then(serde_json::Value::as_u64).unwrap_or(1);
            let caption = request.get("caption").and_then(serde_json::Value::as_str).unwrap_or("");
            let heard: Vec<String> = request.get("heard").and_then(serde_json::Value::as_array)
                .map(|a| a.iter().map(|v| v.as_str().unwrap_or("").to_string()).collect()).unwrap_or_default();
            Some(Ok(choreograph(&env, hop, lyrics, caption, template, seed, &heard)))
        }
        // request: mouth, hop -> breaths [[start, end]...] (the spans the game has transcribed for align_heard)
        "song_breaths" => {
            let env: Vec<f32> = request.get("mouth").and_then(serde_json::Value::as_array)
                .map(|a| a.iter().map(|v| v.as_f64().unwrap_or(0.0) as f32).collect()).unwrap_or_default();
            let hop = request.get("hop").and_then(serde_json::Value::as_f64).unwrap_or(0.01) as f32;
            let breaths: Vec<[f32; 2]> = phrases(&env, hop).into_iter().map(|(s, e)| [(s * 100.0).round() / 100.0, (e * 100.0).round() / 100.0]).collect();
            Some(Ok(serde_json::json!({"breaths": breaths})))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn captions_follow_what_she_sang() {
        // Four breaths; the model sang lines 0+1 in one breath, an "ah", line 2, then line 1 again (an extra chorus).
        let breaths = vec![(0.0, 4.0), (5.0, 6.0), (7.0, 9.0), (10.0, 12.0)];
        let heard: Vec<String> = ["原來那些心跳不是我的錯覺", "啊", "你說出口的時候", "不是我的錯覺"].iter().map(|s| s.to_string()).collect();
        let sung: Vec<String> = ["原来那些心跳", "不是我的错觉", "你说出口的时候"].iter().map(|s| s.to_string()).collect();
        let shown: Vec<String> = ["原來那些心跳", "不是我的錯覺", "你說出口的時候"].iter().map(|s| s.to_string()).collect();
        let lines = align_heard(&breaths, &heard, &sung, &shown);
        let texts: Vec<&str> = lines.iter().map(|(_, t)| t.as_str()).collect();
        assert_eq!(texts, ["原來那些心跳", "不是我的錯覺", "你說出口的時候", "不是我的錯覺"]);
        assert_eq!(lines[0].0, 0.0);
        assert!(lines[1].0 > 1.5 && lines[1].0 < 2.5, "the second line starts partway through the first breath");
        assert_eq!(lines[2].0, 7.0);
        assert_eq!(lines[3].0, 10.0);
    }

    #[test]
    fn choreography_follows_the_phrases() {
        // 2 s of introduction, eight 2 s phrases with 0.8 s breaths, 1 s of tail
        let hop = 0.01;
        let mut env = vec![0.0f32; 200];
        for _ in 0..8 {
            env.extend(std::iter::repeat(0.8).take(200));
            env.extend(std::iter::repeat(0.0).take(80));
        }
        env.extend(std::iter::repeat(0.0).take(100));
        assert_eq!(phrases(&env, hop).len(), 8);
        let sheet = "[主歌]\n夜色裡的街道 只剩我們兩個\n手心裡的溫度 比星星還要清楚\n[副歌]\n就這樣靠著你\n把心跳慢慢說給你聽\n不用說太多的話\n你在這裡就很好";
        let c = choreograph(&env, hop, sheet, "", "lover_night+park", 7, &[]);
        assert_eq!(c["verse_phrases"], 4);
        assert_eq!(c["upbeat"], false);
        let cues = c["cues"].as_array().unwrap();
        assert_eq!(cues.first().unwrap()["part"], "prelude");
        assert_eq!(cues.last().unwrap()["part"], "finale");
        assert!(cues.iter().any(|q| q["part"] == "verse") && cues.iter().any(|q| q["part"] == "chorus"));
        let ats: Vec<f64> = cues.iter().map(|q| q["at"].as_f64().unwrap()).collect();
        assert!(ats.windows(2).all(|w| w[1] > w[0]), "{ats:?}");
        // the finale lands just before the last note ends
        let end = c["end"].as_f64().unwrap();
        assert!((end - ats.last().unwrap()) < 3.0);
        assert_eq!(choreograph(&env, hop, sheet, "", "sunny_day", 7, &[])["upbeat"], true);
        // the caption: one line per unit (4 verse halves + 4 chorus lines), in order, starting with the first half line
        let lines = c["lines"].as_array().unwrap();
        assert_eq!(lines.len(), 8);
        assert_eq!(lines[0]["text"], "夜色裡的街道");
        assert_eq!(lines[4]["text"], "就這樣靠著你");
        assert!(lines.windows(2).all(|w| w[1]["at"].as_f64() > w[0]["at"].as_f64()));
        // sung legato (one breath in the middle): the lines are spread over the voice, and the chorus still dances
        let mut legato = vec![0.0f32; 200];
        legato.extend((0..1000).map(|i| 0.5 + 0.3 * ((i as f32) * 0.05).sin().abs()));
        legato.extend(std::iter::repeat(0.0).take(60));
        legato.extend((0..1000).map(|i| 0.5 + 0.3 * ((i as f32) * 0.05).sin().abs()));
        let l = choreograph(&legato, hop, sheet, "", "lover_night", 3, &[]);
        let parts: Vec<&str> = l["cues"].as_array().unwrap().iter().map(|q| q["part"].as_str().unwrap()).collect();
        assert!(parts.contains(&"verse") && parts.contains(&"chorus"), "{parts:?}");
        // nothing sung: still a prelude and a finale, no panic
        assert!(choreograph(&[], hop, sheet, "", "", 1, &[])["cues"].as_array().unwrap().len() >= 1);
    }

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

    #[test]
    fn full_songs_have_two_verses_and_three_choruses() {
        for sheet in SHEETS {
            let u = units(sheet.display);
            let count = |p: &str| u.iter().filter(|x| x.0 == p).count();
            assert_eq!(count("verse"), 8, "{}", sheet.id);
            assert_eq!(count("chorus"), 12, "{}", sheet.id);
            assert_eq!(count("bridge"), 0, "{}", sheet.id);
            let v = lyrics_for("lover", 21, 1, sheet.occasion);
            assert_eq!(v["max_duration_ms"], SONG_MS);
            let m = v["lyrics_model"].as_str().unwrap();
            assert_eq!(m.matches("[Chorus]").count(), 3, "{m}");
            assert!(!m.contains("[Bridge]") && !m.contains("[主歌]") && m.chars().count() < 640, "{m}");
        }
        // a milestone's own words; memories fill the first verse of the others, the rest stays
        assert_eq!(lyrics_for("lover", 21, 1, "confession")["template"], "confession");
        let mem = serde_json::json!({"kind": "date", "place": "aquarium", "importance": 0.9});
        assert_eq!(lyrics_with_for("lover", 21, 1, "confession", &[mem.clone()])["template"], "confession");
        let v = lyrics_with_for("lover", 21, 1, "favor100", &[mem]);
        let d = v["lyrics_display"].as_str().unwrap();
        assert!(d.starts_with("[主歌]\n還記得水族館的那天\n") && d.contains("[主歌]\n一起走過的地方") && d.matches("[副歌]").count() == 3, "{d}");
        assert_eq!(units(d).iter().filter(|x| x.0 == "chorus").count(), 12);
    }

    #[test]
    fn milestones_lover_then_favor_100_150_then_every_500_lines() {
        let p = |stage: &str, favor: f32, done: &[&str], lines: u32| SongProgress {
            id: "apng-a".into(), stage: stage.into(), favor, done: done.iter().map(|s| s.to_string()).collect(), lines };
        assert_eq!(milestone(&p("stranger", 50.0, &[], 0)), None);
        assert_eq!(milestone(&p("friend", 60.0, &[], 0)), None); // liking 45: not close yet
        assert_eq!(milestone(&p("friend", 190.0, &[], 0)), Some(("confession", false))); // made ahead
        assert_eq!(milestone(&p("lover", 0.0, &[], 0)), Some(("confession", true)));
        assert_eq!(milestone(&p("lover", 50.0, &["confession"], 0)), None);
        assert_eq!(milestone(&p("lover", 90.0, &["confession"], 0)), Some(("favor100", false)));
        assert_eq!(milestone(&p("lover", 100.0, &["confession"], 0)), Some(("favor100", true)));
        assert_eq!(milestone(&p("lover", 120.0, &["confession", "favor100"], 0)), None);
        assert_eq!(milestone(&p("lover", 140.0, &["confession", "favor100"], 0)), Some(("favor150", false)));
        assert_eq!(milestone(&p("lover", 150.0, &["confession", "favor100"], 0)), Some(("favor150", true)));
        let all = ["confession", "favor100", "favor150"];
        assert_eq!(milestone(&p("lover", 150.0, &all, 399)), None);
        assert_eq!(milestone(&p("lover", 150.0, &all, 420)), Some(("lines", false)));
        assert_eq!(milestone(&p("lover", 150.0, &all, 500)), Some(("lines", true)));
        // a due song comes before one made ahead; among due ones, the earlier milestone
        let e = eval("song_milestone", &serde_json::json!({"heroines": [
            {"id": "apng-b", "stage": "lover", "favor": 140.0, "done": ["confession", "favor100"]},
            {"id": "apng-c", "stage": "lover", "favor": 150.0, "done": ["confession", "favor100", "favor150"], "lines": 520},
            {"id": "apng-d", "stage": "lover", "favor": 100.0, "done": ["confession"]}]})).unwrap().unwrap();
        assert_eq!(e["pick"], serde_json::json!({"id": "apng-d", "occasion": "favor100", "due": true}));
        let none = eval("song_milestone", &serde_json::json!({"heroines": [{"id": "apng-b", "stage": "friend", "favor": 10.0}]})).unwrap().unwrap();
        assert!(none["pick"].is_null());
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

/// Development: OK_SONG_MOUTH=<a song's mouth.json> OK_SONG_SHEET=<template> cargo test --release choreography_of_a_song
/// -- --ignored --nocapture prints the staging a real song gets.
#[cfg(test)]
#[test]
#[ignore]
fn choreography_of_a_song() {
    let path = std::env::var("OK_SONG_MOUTH").expect("OK_SONG_MOUTH");
    let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    let env: Vec<f32> = v["values"].as_array().unwrap().iter().map(|x| x.as_f64().unwrap() as f32).collect();
    let hop = v["hop_ms"].as_f64().unwrap_or(10.0) as f32 / 1000.0;
    let template = std::env::var("OK_SONG_SHEET").unwrap_or_else(|_| "sunny_day".into());
    let sheet = SHEETS.iter().find(|s| s.id == template.split('+').next().unwrap()).unwrap().display;
    println!("{}", serde_json::to_string_pretty(&choreograph(&env, hop, sheet, "", &template, 5, &[])).unwrap());
}
