//! 競技彩 (plan §15.1 / §15.7.4): real 2010–2026 results for four competitions, Elo odds computed at load,
//! Taiwan-date conversion, cutoff and settlement. Pure functions of the data; nothing here is random.
use super::dates;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::Path;

#[derive(Clone, Copy, PartialEq, Eq, Debug, Hash)]
pub enum Comp {
    Nba,
    Mlb,
    Wc,
    Cba,
}

impl Comp {
    pub fn key(self) -> &'static str {
        match self {
            Comp::Nba => "nba",
            Comp::Mlb => "mlb",
            Comp::Wc => "wc",
            Comp::Cba => "cba",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Comp::Nba => "NBA 季後賽",
            Comp::Mlb => "MLB 季後賽",
            Comp::Wc => "世界盃",
            Comp::Cba => "CBA 季後賽",
        }
    }
    fn parse(s: &str) -> Option<Comp> {
        Some(match s {
            "nba" => Comp::Nba,
            "mlb" => Comp::Mlb,
            "wc" => Comp::Wc,
            "cba" => Comp::Cba,
            _ => return None,
        })
    }
    /// Local date -> Taiwan date: the Americas and the World Cups (evening kick-offs in Europe/Africa/Americas,
    /// late ones in Qatar) land on the next Taiwan day; CBA plays in China, the same day.
    fn tw_shift(self) -> i32 {
        if self == Comp::Cba { 0 } else { 1 }
    }
    fn home_edge(self) -> f64 {
        match self {
            Comp::Nba | Comp::Cba => 60.0,
            Comp::Mlb => 25.0,
            Comp::Wc => 0.0,
        }
    }
    fn k(self) -> f64 {
        match self {
            Comp::Wc => 40.0,
            Comp::Mlb => 12.0,
            _ => 20.0,
        }
    }
    fn soccer(self) -> bool {
        self == Comp::Wc
    }
    fn baseball(self) -> bool {
        self == Comp::Mlb
    }
}

#[derive(Clone, Debug)]
pub struct Game {
    pub id: String,
    pub comp: Comp,
    pub season: i32,
    pub stage: String,
    pub series: String,
    pub game_no: i32,
    pub local_day: i32,
    pub tw_day: i32,
    pub home: String,
    pub away: String,
    pub hs: i32,
    pub as_: i32,
    pub extra: bool,
    pub pens: Option<(i32, i32)>,
    pub host: bool, // World Cup host plays at home
    // Pre-game model, fixed at load.
    pub p_home: f64,
    pub p_draw: f64,
    pub exp_margin: f64,
    pub exp_total: f64,
}

impl Game {
    /// Who advanced (World Cup knockouts include extra time and penalties); None for a draw.
    pub fn winner(&self) -> Option<&str> {
        if self.hs != self.as_ {
            return Some(if self.hs > self.as_ { &self.home } else { &self.away });
        }
        self.pens.map(|(h, a)| if h > a { self.home.as_str() } else { self.away.as_str() })
    }
}

#[derive(Clone, Debug)]
pub struct Series {
    pub id: String,
    pub comp: Comp,
    pub season: i32,
    pub stage: String,
    pub teams: (String, String),
    pub winner: String,
    pub first_tw_day: i32,
    pub last_tw_day: i32,
    pub p_first: f64,
}

pub struct Sports {
    pub games: Vec<Game>,
    pub by_id: HashMap<String, usize>,
    pub series: Vec<Series>,
    pub series_by_id: HashMap<String, usize>,
    pub names: HashMap<(Comp, String), String>,
    /// (comp, season) -> first and last Taiwan day, champion, pre-playoff Elo of every team in it.
    pub seasons: Vec<(Comp, i32, i32, i32, String, Vec<(String, f64)>)>,
    pub margin_return: f64,
}

fn phi(x: f64) -> f64 {
    // Abramowitz–Stegun 7.1.26 via erf.
    let t = 1.0 / (1.0 + 0.3275911 * (x.abs() / std::f64::consts::SQRT_2));
    let y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * (-(x * x / 2.0)).exp();
    if x >= 0.0 { 0.5 * (1.0 + y) } else { 0.5 * (1.0 - y) }
}

fn poisson(l: f64, k: i32) -> f64 {
    let mut p = (-l).exp();
    for i in 1..=k {
        p *= l / i as f64;
    }
    p
}

fn half_line(x: f64) -> f64 {
    (x - 0.5).round() + 0.5
}

/// Decimal odds at the configured return, rounded down to 0.01, at least 1.01.
pub fn price(p: f64, ret: f64) -> f64 {
    let p = p.clamp(0.001, 0.995);
    ((ret / p * 100.0).floor() / 100.0).max(1.01)
}

fn s(v: &Value, k: &str) -> String {
    v[k].as_str().unwrap_or("").to_string()
}

impl Sports {
    pub fn load(dir: &Path, ret: f64) -> Result<Sports, String> {
        let mut raw: Vec<Game> = Vec::new();
        let mut series_raw: Vec<(Comp, i32, Value)> = Vec::new();
        let mut names = HashMap::new();
        let mut champions: HashMap<(Comp, i32), String> = HashMap::new();
        for (file, comp) in [("nba_playoffs.json", Comp::Nba), ("mlb_postseason.json", Comp::Mlb), ("world_cup.json", Comp::Wc), ("cba_playoffs.json", Comp::Cba)] {
            let path = dir.join(file);
            let text = std::fs::read_to_string(&path).map_err(|e| format!("sports_missing {}: {e}", path.display()))?;
            let doc: Value = serde_json::from_str(&text).map_err(|e| format!("sports_invalid {file}: {e}"))?;
            if let Some(teams) = doc["teams"].as_object() {
                for (code, t) in teams {
                    let name = t["zh_tw"].as_str().or(t["en"].as_str()).unwrap_or(code).to_string();
                    names.insert((comp, code.clone()), name);
                }
            }
            let blocks = doc["seasons"].as_array().or(doc["tournaments"].as_array()).cloned().unwrap_or_default();
            for b in blocks {
                let season = b["year"].as_i64().unwrap_or(0) as i32;
                if let Some(c) = b["champion"].as_str() {
                    champions.insert((comp, season), c.to_string());
                }
                let host = b["host"].as_str().unwrap_or("").to_string();
                for sr in b["series"].as_array().cloned().unwrap_or_default() {
                    series_raw.push((comp, season, sr));
                }
                let games = b["games"].as_array().or(b["matches"].as_array()).cloned().unwrap_or_default();
                for g in games {
                    let Some(local_day) = dates::parse(g["date"].as_str().unwrap_or("")) else { continue };
                    let (Some(hs), Some(as_)) = (g["home_score"].as_i64(), g["away_score"].as_i64()) else { continue };
                    let pens = g["penalties"].as_array().and_then(|p| Some((p.first()?.as_i64()? as i32, p.get(1)?.as_i64()? as i32)))
                        .or_else(|| g["penalties"].as_object().and_then(|p| Some((p.get("home")?.as_i64()? as i32, p.get("away")?.as_i64()? as i32))));
                    let home = s(&g, "home");
                    let host_home = comp == Comp::Wc && !host.is_empty() && names.get(&(comp, home.clone())).map(|n| host_matches(&host, &home, n)).unwrap_or(false);
                    raw.push(Game {
                        id: format!("{}:{}", comp.key(), s(&g, "id")),
                        comp,
                        season,
                        stage: s(&g, "stage"),
                        series: if g["series"].is_string() { format!("{}:{}", comp.key(), s(&g, "series")) } else { String::new() },
                        game_no: g["game"].as_i64().unwrap_or(0) as i32,
                        local_day,
                        tw_day: local_day + comp.tw_shift(),
                        home,
                        away: s(&g, "away"),
                        hs: hs as i32,
                        as_: as_ as i32,
                        extra: g["ot"].as_i64().unwrap_or(0) > 0 || g["extra_time"].as_bool().unwrap_or(false) || g["innings"].as_i64().unwrap_or(9) > 9,
                        pens,
                        host: host_home,
                        p_home: 0.5,
                        p_draw: 0.0,
                        exp_margin: 0.0,
                        exp_total: 0.0,
                    });
                }
            }
        }
        raw.sort_by(|a, b| (a.local_day, &a.id).cmp(&(b.local_day, &b.id)));
        // Elo by competition, carried across seasons with a quarter regression to the mean.
        let mut elo: HashMap<(Comp, String), f64> = HashMap::new();
        let mut last_season: HashMap<Comp, i32> = HashMap::new();
        let mut recent: HashMap<(Comp, String), Vec<(i32, i32)>> = HashMap::new(); // (scored, conceded)
        let mut seasons: Vec<(Comp, i32, i32, i32, String, Vec<(String, f64)>)> = Vec::new();
        for g in raw.iter_mut() {
            if last_season.get(&g.comp) != Some(&g.season) {
                for ((c, _), v) in elo.iter_mut() {
                    if *c == g.comp {
                        *v = 1500.0 + (*v - 1500.0) * 0.75;
                    }
                }
                last_season.insert(g.comp, g.season);
            }
            let rh = *elo.entry((g.comp, g.home.clone())).or_insert(1500.0);
            let ra = *elo.entry((g.comp, g.away.clone())).or_insert(1500.0);
            let edge = if g.comp.soccer() { if g.host { 100.0 } else { 0.0 } } else { g.comp.home_edge() };
            let dr = rh + edge - ra;
            let p = 1.0 / (1.0 + 10f64.powf(-dr / 400.0));
            let avg = |team: &String, comp: Comp| -> Option<f64> {
                let v = recent.get(&(comp, team.clone()))?;
                (v.len() >= 3).then(|| v.iter().map(|(a, b)| (a + b) as f64).sum::<f64>() / v.len() as f64)
            };
            let default_total = match g.comp {
                Comp::Nba => 205.0,
                Comp::Cba => 200.0,
                Comp::Mlb => 8.0,
                Comp::Wc => 2.5,
            };
            let total = match (avg(&g.home, g.comp), avg(&g.away, g.comp)) {
                (Some(a), Some(b)) => (a + b) / 2.0,
                (Some(a), None) | (None, Some(a)) => (a + default_total) / 2.0,
                _ => default_total,
            };
            if g.comp.soccer() {
                let draw = (0.29 - 0.25 * (2.0 * p - 1.0).abs()).clamp(0.12, 0.30);
                g.p_home = p * (1.0 - draw);
                g.p_draw = draw;
                g.exp_margin = dr / 220.0;
                g.exp_total = 2.5;
            } else {
                g.p_home = p;
                g.exp_margin = if g.comp.baseball() { dr / 160.0 } else { dr / 28.0 };
                g.exp_total = total;
            }
            // Update.
            let outcome = if g.hs > g.as_ { 1.0 } else if g.hs < g.as_ { 0.0 } else { 0.5 };
            let mult = if g.comp.soccer() { (1.0 + (g.hs - g.as_).abs() as f64).ln().max(1.0) } else { 1.0 };
            let delta = g.comp.k() * mult * (outcome - p);
            *elo.get_mut(&(g.comp, g.home.clone())).unwrap() += delta;
            *elo.get_mut(&(g.comp, g.away.clone())).unwrap() -= delta;
            for (team, sc, co) in [(&g.home, g.hs, g.as_), (&g.away, g.as_, g.hs)] {
                let v = recent.entry((g.comp, team.clone())).or_default();
                v.push((sc, co));
                if v.len() > 10 {
                    v.remove(0);
                }
            }
            // Season bookkeeping (pre-playoff ratings are taken from the first game's moment).
            match seasons.iter_mut().find(|x| x.0 == g.comp && x.1 == g.season) {
                Some(x) => x.3 = x.3.max(g.tw_day),
                None => {
                    let ratings: Vec<(String, f64)> = Vec::new();
                    seasons.push((g.comp, g.season, g.tw_day, g.tw_day, champions.get(&(g.comp, g.season)).cloned().unwrap_or_default(), ratings));
                }
            }
        }
        // Teams of each season with the rating they had before its first game.
        {
            let mut elo2: HashMap<(Comp, String), f64> = HashMap::new();
            let mut last: HashMap<Comp, i32> = HashMap::new();
            let mut idx = 0;
            let mut order: Vec<usize> = (0..raw.len()).collect();
            order.sort_by_key(|&i| (raw[i].local_day, raw[i].id.clone()));
            while idx < order.len() {
                let g = &raw[order[idx]];
                if last.get(&g.comp) != Some(&g.season) {
                    for ((c, _), v) in elo2.iter_mut() {
                        if *c == g.comp {
                            *v = 1500.0 + (*v - 1500.0) * 0.75;
                        }
                    }
                    last.insert(g.comp, g.season);
                    let mut teams: Vec<String> = raw.iter().filter(|x| x.comp == g.comp && x.season == g.season).flat_map(|x| [x.home.clone(), x.away.clone()]).collect();
                    teams.sort();
                    teams.dedup();
                    let ratings = teams.iter().map(|t| (t.clone(), *elo2.get(&(g.comp, t.clone())).unwrap_or(&1500.0))).collect();
                    if let Some(x) = seasons.iter_mut().find(|x| x.0 == g.comp && x.1 == g.season) {
                        x.5 = ratings;
                    }
                }
                let rh = *elo2.entry((g.comp, g.home.clone())).or_insert(1500.0);
                let ra = *elo2.entry((g.comp, g.away.clone())).or_insert(1500.0);
                let edge = if g.comp.soccer() { if g.host { 100.0 } else { 0.0 } } else { g.comp.home_edge() };
                let p = 1.0 / (1.0 + 10f64.powf(-(rh + edge - ra) / 400.0));
                let outcome = if g.hs > g.as_ { 1.0 } else if g.hs < g.as_ { 0.0 } else { 0.5 };
                let mult = if g.comp.soccer() { (1.0 + (g.hs - g.as_).abs() as f64).ln().max(1.0) } else { 1.0 };
                let delta = g.comp.k() * mult * (outcome - p);
                *elo2.get_mut(&(g.comp, g.home.clone())).unwrap() += delta;
                *elo2.get_mut(&(g.comp, g.away.clone())).unwrap() -= delta;
                idx += 1;
            }
        }
        let by_id = raw.iter().enumerate().map(|(i, g)| (g.id.clone(), i)).collect();
        // Series: dates from their games; the first game's win probability prices the series.
        let mut series = Vec::new();
        for (comp, season, sr) in series_raw {
            let id = format!("{}:{}", comp.key(), s(&sr, "id"));
            let teams = match (sr["teams"].as_array(), sr["high_seed"].as_str(), sr["low_seed"].as_str()) {
                (Some(t), _, _) if t.len() == 2 => (t[0].as_str().unwrap_or("").to_string(), t[1].as_str().unwrap_or("").to_string()),
                (_, Some(h), Some(l)) => (h.to_string(), l.to_string()),
                _ => continue,
            };
            let games: Vec<&Game> = raw.iter().filter(|g| g.series == id).collect();
            let Some(first) = games.iter().min_by_key(|g| g.local_day) else { continue };
            let last = games.iter().map(|g| g.tw_day).max().unwrap_or(first.tw_day);
            let pf = if first.home == teams.0 { first.p_home } else { 1.0 - first.p_home };
            let best_of = sr["best_of"].as_i64().unwrap_or(7) as i32;
            series.push(Series {
                id,
                comp,
                season,
                stage: s(&sr, "stage"),
                teams,
                winner: s(&sr, "winner"),
                first_tw_day: first.tw_day,
                last_tw_day: last,
                p_first: series_prob(pf, best_of),
            });
        }
        let series_by_id = series.iter().enumerate().map(|(i, x)| (x.id.clone(), i)).collect();
        Ok(Sports { games: raw, by_id, series, series_by_id, names, seasons, margin_return: ret })
    }

    pub fn name(&self, comp: Comp, team: &str) -> String {
        self.names.get(&(comp, team.to_string())).cloned().unwrap_or_else(|| team.to_string())
    }

    pub fn game(&self, id: &str) -> Option<&Game> {
        self.by_id.get(id).map(|&i| &self.games[i])
    }

    /// Bets close at the end of the Taiwan day before the game; results post at 12:00 on the game's Taiwan day.
    pub fn open_for_bets(tw_day: i32, now_day: i32) -> bool {
        now_day < tw_day
    }
    pub fn settled(tw_day: i32, now_day: i32, now_minute: i32) -> bool {
        now_day > tw_day || (now_day == tw_day && now_minute >= 12 * 60)
    }

    /// Every market a game offers, with odds.
    pub fn markets(&self, g: &Game) -> Vec<Value> {
        let r = self.margin_return;
        let mut out = Vec::new();
        if g.comp.soccer() {
            let pa = 1.0 - g.p_home - g.p_draw;
            out.push(json!({"market": "ml", "label": "不讓分（90 分鐘）", "picks": [
                {"pick": "home", "odds": price(g.p_home, r)}, {"pick": "draw", "odds": price(g.p_draw, r)}, {"pick": "away", "odds": price(pa, r)}]}));
        } else {
            out.push(json!({"market": "ml", "label": "不讓分", "picks": [
                {"pick": "home", "odds": price(g.p_home, r)}, {"pick": "away", "odds": price(1.0 - g.p_home, r)}]}));
        }
        let (line, sd) = if g.comp.baseball() {
            (if g.exp_margin >= 0.0 { -1.5 } else { 1.5 }, 3.5)
        } else if g.comp.soccer() {
            (-half_line(g.exp_margin).max(-2.5).min(2.5), 1.4)
        } else {
            (-half_line(g.exp_margin), 12.0)
        };
        let p_cover = phi((g.exp_margin + line) / sd);
        out.push(json!({"market": "spread", "label": "讓分", "line": line, "picks": [
            {"pick": "home", "odds": price(p_cover, r)}, {"pick": "away", "odds": price(1.0 - p_cover, r)}]}));
        let (tline, p_over) = if g.comp.soccer() {
            let l = g.exp_total;
            (2.5, 1.0 - poisson(l, 0) - poisson(l, 1) - poisson(l, 2))
        } else {
            let l = half_line(g.exp_total);
            (l, phi((g.exp_total - l) / if g.comp.baseball() { 3.5 } else { 18.0 }))
        };
        out.push(json!({"market": "total", "label": "大小分", "line": tline, "picks": [
            {"pick": "over", "odds": price(p_over.clamp(0.05, 0.95), r)}, {"pick": "under", "odds": price((1.0 - p_over).clamp(0.05, 0.95), r)}]}));
        if g.comp.soccer() {
            let pa = 1.0 - g.p_home - g.p_draw;
            let lh = (g.exp_total * (0.5 + (g.p_home - pa) * 0.45)).clamp(0.3, 2.4);
            let la = (g.exp_total - lh).max(0.3);
            let mut picks = Vec::new();
            let mut covered = 0.0;
            for h in 0..=3 {
                for a in 0..=3 {
                    let p = poisson(lh, h) * poisson(la, a);
                    covered += p;
                    picks.push(json!({"pick": format!("{h}-{a}"), "odds": price(p, r)}));
                }
            }
            picks.push(json!({"pick": "other", "odds": price(1.0 - covered, r)}));
            out.push(json!({"market": "score", "label": "正確比分（90 分鐘）", "picks": picks}));
            if g.stage != "group" {
                let adv = g.p_home + g.p_draw * 0.5;
                out.push(json!({"market": "advance", "label": "晉級（含延長、PK）", "picks": [
                    {"pick": "home", "odds": price(adv, r)}, {"pick": "away", "odds": price(1.0 - adv, r)}]}));
            }
        }
        out
    }

    /// Settles one selection: Some(true) win, Some(false) loss, None void (refund this leg).
    pub fn settle(&self, target: &str, market: &str, pick: &str, line: f64) -> Option<bool> {
        if market == "series" {
            let sr = &self.series[*self.series_by_id.get(target)?];
            return Some(sr.winner == pick);
        }
        if market == "champion" {
            let (comp, season) = parse_champ(target)?;
            let x = self.seasons.iter().find(|x| x.0 == comp && x.1 == season)?;
            return Some(x.4 == pick);
        }
        let g = self.game(target)?;
        let side = |p: &str| if p == "home" { 1 } else { -1 };
        match market {
            "ml" => {
                if g.comp.soccer() {
                    if g.extra {
                        return Some(pick == "draw");
                    }
                    let res = if g.hs > g.as_ { "home" } else if g.hs < g.as_ { "away" } else { "draw" };
                    Some(res == pick)
                } else {
                    Some((g.hs > g.as_) == (pick == "home"))
                }
            }
            "spread" => {
                if g.comp.soccer() && g.extra {
                    return None;
                }
                let m = (g.hs - g.as_) as f64 + line; // home with the line
                if m == 0.0 {
                    return None;
                }
                Some((m > 0.0) == (side(pick) > 0))
            }
            "total" => {
                if g.comp.soccer() && g.extra {
                    return None;
                }
                let t = (g.hs + g.as_) as f64;
                if t == line {
                    return None;
                }
                Some((t > line) == (pick == "over"))
            }
            "score" => {
                if g.extra {
                    return None;
                }
                let s = format!("{}-{}", g.hs, g.as_);
                let listed = g.hs <= 3 && g.as_ <= 3;
                Some(if pick == "other" { !listed } else { s == pick })
            }
            "advance" => {
                let w = g.winner()?;
                Some((w == g.home) == (pick == "home"))
            }
            _ => None,
        }
    }

    /// When a selection's result is known (Taiwan day).
    pub fn settle_day(&self, target: &str, market: &str) -> Option<i32> {
        match market {
            "series" => Some(self.series[*self.series_by_id.get(target)?].last_tw_day),
            "champion" => {
                let (comp, season) = parse_champ(target)?;
                Some(self.seasons.iter().find(|x| x.0 == comp && x.1 == season)?.3)
            }
            _ => Some(self.game(target)?.tw_day),
        }
    }

    /// Last moment to bet (exclusive Taiwan day).
    pub fn close_day(&self, target: &str, market: &str) -> Option<i32> {
        match market {
            "series" => Some(self.series[*self.series_by_id.get(target)?].first_tw_day),
            "champion" => {
                let (comp, season) = parse_champ(target)?;
                Some(self.seasons.iter().find(|x| x.0 == comp && x.1 == season)?.2)
            }
            _ => Some(self.game(target)?.tw_day),
        }
    }

    /// The odds a selection gets at the moment it is placed (fixed per selection; the model never moves).
    pub fn odds_of(&self, target: &str, market: &str, pick: &str) -> Option<(f64, f64)> {
        let r = self.margin_return;
        match market {
            "series" => {
                let sr = &self.series[*self.series_by_id.get(target)?];
                let p = if pick == sr.teams.0 { sr.p_first } else if pick == sr.teams.1 { 1.0 - sr.p_first } else { return None };
                Some((price(p, r), 0.0))
            }
            "champion" => {
                let probs = self.champion_probs(target)?;
                let p = probs.iter().find(|(t, _)| t == pick)?.1;
                Some((price(p, r), 0.0))
            }
            _ => {
                let g = self.game(target)?;
                for m in self.markets(g) {
                    if m["market"] != market {
                        continue;
                    }
                    let line = m["line"].as_f64().unwrap_or(0.0);
                    for p in m["picks"].as_array()? {
                        if p["pick"] == pick {
                            return Some((p["odds"].as_f64()?, line));
                        }
                    }
                }
                None
            }
        }
    }

    pub fn champion_probs(&self, target: &str) -> Option<Vec<(String, f64)>> {
        let (comp, season) = parse_champ(target)?;
        let x = self.seasons.iter().find(|x| x.0 == comp && x.1 == season)?;
        let w: Vec<(String, f64)> = x.5.iter().map(|(t, e)| (t.clone(), 10f64.powf((e - 1500.0) / 400.0 * 2.5))).collect();
        let sum: f64 = w.iter().map(|x| x.1).sum();
        Some(w.into_iter().map(|(t, v)| (t, v / sum)).collect())
    }

    pub fn game_json(&self, g: &Game, now_day: i32, now_minute: i32) -> Value {
        let done = Self::settled(g.tw_day, now_day, now_minute);
        let mut v = json!({
            "id": g.id, "comp": g.comp.key(), "comp_label": g.comp.label(), "season": g.season, "stage": g.stage,
            "series": g.series, "game": g.game_no, "date": dates::format(g.tw_day), "local_date": dates::format(g.local_day),
            "home": g.home, "away": g.away, "home_name": self.name(g.comp, &g.home), "away_name": self.name(g.comp, &g.away),
            "open": Self::open_for_bets(g.tw_day, now_day), "final": done,
        });
        if done {
            v["home_score"] = json!(g.hs);
            v["away_score"] = json!(g.as_);
            v["extra"] = json!(g.extra);
            if let Some((h, a)) = g.pens {
                v["penalties"] = json!([h, a]);
            }
        }
        if Self::open_for_bets(g.tw_day, now_day) {
            v["markets"] = Value::Array(self.markets(g));
        }
        v
    }

    /// Games from `from` to `to` (Taiwan days), and the series and champion markets open now.
    pub fn board(&self, now_day: i32, now_minute: i32, from: i32, to: i32) -> Value {
        let games: Vec<Value> = self.games.iter().filter(|g| g.tw_day >= from && g.tw_day <= to).map(|g| self.game_json(g, now_day, now_minute)).collect();
        let series: Vec<Value> = self
            .series
            .iter()
            .filter(|s| s.first_tw_day > now_day && s.first_tw_day <= to)
            .map(|s| json!({"id": s.id, "comp": s.comp.key(), "stage": s.stage, "teams": [s.teams.0, s.teams.1],
                "names": [self.name(s.comp, &s.teams.0), self.name(s.comp, &s.teams.1)],
                "odds": [price(s.p_first, self.margin_return), price(1.0 - s.p_first, self.margin_return)], "closes": dates::format(s.first_tw_day)}))
            .collect();
        let champs: Vec<Value> = self
            .seasons
            .iter()
            .filter(|x| x.2 > now_day && x.2 <= to + 30)
            .map(|x| {
                let target = format!("champ:{}:{}", x.0.key(), x.1);
                let probs = self.champion_probs(&target).unwrap_or_default();
                json!({"id": target, "comp": x.0.key(), "season": x.1, "closes": dates::format(x.2),
                    "picks": probs.iter().map(|(t, p)| json!({"pick": t, "name": self.name(x.0, t), "odds": price(*p, self.margin_return)})).collect::<Vec<_>>()})
            })
            .collect();
        // Nothing in the window (late September: the MLB postseason opens 10/6): when the next game is, so the page
        // does not stand empty.
        let next = self.games.iter().filter(|g| g.tw_day > to).min_by_key(|g| g.tw_day).map(|g| json!({"date": dates::format(g.tw_day), "comp_label": g.comp.label()}));
        json!({"games": games, "series": series, "champions": champs, "next": next})
    }
}

fn host_matches(host: &str, code: &str, name: &str) -> bool {
    let h = host.to_lowercase();
    let pairs = [("south africa", "RSA"), ("brazil", "BRA"), ("russia", "RUS"), ("qatar", "QAT"), ("united states", "USA"), ("usa", "USA"), ("canada", "CAN"), ("mexico", "MEX")];
    pairs.iter().any(|(n, c)| h.contains(n) && *c == code) || h == name.to_lowercase()
}

pub fn parse_champ(target: &str) -> Option<(Comp, i32)> {
    let mut it = target.split(':');
    (it.next()? == "champ").then_some(())?;
    let comp = Comp::parse(it.next()?)?;
    let season = it.next()?.parse().ok()?;
    Some((comp, season))
}

/// Probability the side with per-game probability `p` wins a best-of-n series.
fn series_prob(p: f64, best_of: i32) -> f64 {
    let need = best_of / 2 + 1;
    let q = 1.0 - p;
    let mut total = 0.0;
    for losses in 0..need {
        // need wins, `losses` losses, last game a win: C(need-1+losses, losses)
        let mut c = 1.0;
        for i in 0..losses {
            c = c * (need - 1 + losses - i) as f64 / (i + 1) as f64;
        }
        total += c * p.powi(need) * q.powi(losses);
    }
    total
}
