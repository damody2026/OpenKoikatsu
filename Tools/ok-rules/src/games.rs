//! Games and luck with the heroines: 吹牛 (Cheat) — how bold each heroine is, whether and when she calls a play, what
//! she plays, what the game leaves behind — and the heroines' own scratch cards and his predictions coming true or not.
//! Unreal deals, shows the table and speaks the lines; the market and lottery service draws the numbers.
use crate::effects::Effects;
use crate::relation::Relation;
use crate::rng::RandomStream;
use crate::traits::profile_str;
use serde::Deserialize;
use serde_json::{json, Value};

fn s<'a>(r: &'a Value, k: &str) -> &'a str {
    r.get(k).and_then(Value::as_str).unwrap_or("")
}
fn n(r: &Value, k: &str) -> f64 {
    r.get(k).and_then(Value::as_f64).unwrap_or(0.0)
}
fn b(r: &Value, k: &str) -> bool {
    r.get(k).and_then(Value::as_bool).unwrap_or(false)
}

/// How bold she is at cards, 0.2..0.8, fixed per heroine.
pub fn bold(name: &str) -> f32 {
    0.2 + (crate::academics::hash(name) % 100) as f32 / 100.0 * 0.6
}

#[derive(Deserialize, Debug, Clone, Default)]
#[serde(default)]
struct Seat {
    seat: i32,
    name: String,
    /// Cards of this turn's rank in her hand.
    held: i32,
    /// She dislikes the one who played (her feeling toward that heroine is below 0).
    distrusts: bool,
    /// The one who played is him and she is his lover.
    lover: bool,
    /// Cards of this rank everyone saw go to someone else (not her, not the one who played): shown in a call and taken.
    known_elsewhere: i32,
    /// Everyone saw the one who played take at least as many of this rank as she claims: it is most likely true.
    known_truth: bool,
    /// Her wrong calls lately (each right call takes one off): burned, she calls less.
    burned: i32,
    /// This round adds up to more of the rank than there are, by her count (what she holds, what she really put down,
    /// what the others claimed, this play included): somebody lied, most likely the last one.
    round_impossible: bool,
}

/// A play went down: each heroine decides at once whether she calls it, and when (seconds into the window). Request:
/// claimed (cards), left (the player's cards left after it), window (seconds), seats [seat], seed.
pub(crate) fn bluff_calls(r: &Value) -> Result<Value, String> {
    let claimed = n(r, "claimed") as i32;
    let left = n(r, "left") as i32;
    let window = n(r, "window") as f32;
    let seats: Vec<Seat> = serde_json::from_value(r.get("seats").cloned().unwrap_or(json!([]))).map_err(|e| format!("seats: {e}"))?;
    let mut rng = RandomStream::new(n(r, "seed") as i32);
    let mut calls = Vec::new();
    for st in seats {
        let p = if st.held + st.known_elsewhere + claimed > 4 {
            0.95 // she can count: there are not that many
        } else if st.known_truth {
            0.02 // everyone saw her take them
        } else if st.round_impossible {
            0.8 // the round has claimed more than there are
        } else {
            let mut p = 0.06 + 0.12 * (claimed - 1) as f32 + 0.08 * st.held as f32 + if left == 0 { 0.45 } else if left <= 2 { 0.15 } else { 0.0 };
            p *= 0.6 + bold(&st.name);
            if st.distrusts {
                p += 0.1;
            }
            if st.lover {
                p *= 0.8;
            }
            p *= 0.65f32.powi(st.burned.clamp(0, 4));
            p.clamp(0.0, 0.9)
        };
        if rng.frand() < p {
            let at = 0.7 + (2.6f32.min(window - 0.2) - 0.7) * rng.frand();
            calls.push(json!({"seat": st.seat, "at": at}));
        }
    }
    Ok(json!({"calls": calls}))
}

/// A heroine's play: what she has of the rank, honestly; nothing of it, she lies with one or two cards she needs last;
/// now and then a bold one slips one more in. Request: rank, hand [card 0..51], name, seed. Answer: cards.
pub(crate) fn bluff_play(r: &Value) -> Result<Value, String> {
    let rank = n(r, "rank") as i32;
    let hand: Vec<i32> = r.get("hand").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_i64).map(|c| c as i32).collect();
    let boldness = bold(s(r, "name"));
    let mut rng = RandomStream::new(n(r, "seed") as i32);
    let (mut play, mut others): (Vec<i32>, Vec<i32>) = hand.iter().partition(|c| *c % 13 == rank);
    // The ranks needed last first.
    others.sort_by_key(|c| std::cmp::Reverse((c % 13 - rank + 13) % 13));
    if play.is_empty() {
        let count = if others.len() >= 2 && rng.frand() < 0.35 + 0.2 * boldness { 2 } else { 1 };
        play.extend(others.iter().take(count));
    } else if play.len() < 4 && !others.is_empty() && rng.frand() < boldness * 0.35 {
        play.push(others[0]);
    }
    play.truncate(4);
    Ok(json!({"cards": play}))
}

/// The game ended with a winner: each heroine had fun (+12), those who played together grow closer, each remembers it.
/// Request: heroines [{id, name, relation, caught}], winner (index into heroines, -1 for him), winner_name, place.
fn bluff_end(r: &Value) -> Result<Value, String> {
    let list: Vec<Value> = r.get("heroines").and_then(Value::as_array).cloned().unwrap_or_default();
    let winner = n(r, "winner") as i64;
    let names: Vec<&str> = list.iter().map(|h| s(h, "name")).collect();
    let mut fx = Effects::default();
    for (k, h) in list.iter().enumerate() {
        if let Some(mut rel) = h.get("relation").and_then(|v| serde_json::from_value::<Relation>(v.clone()).ok()) {
            let before = rel.clone();
            rel.fun = (rel.fun + 12.0).min(100.0);
            fx.relation(&rel, &before);
        }
        let caught = n(h, "caught") as i32;
        let won = if winner == k as i64 { crate::tr::t("games.its_me_aside").to_string() } else { s(r, "winner_name").to_string() };
        let lies = if caught > 0 { crate::tr::m("games.caught_lying_count", &[("count", crate::tr::a(&caught))]) } else { String::new() };
        fx.observe(s(h, "id"), "game", &crate::tr::m("games.bluff_poker_with_others", &[("names", crate::tr::a(&(names.join(crate::tr::t("bank.list_separator"))))), ("winner", crate::tr::a(&won)), ("count", crate::tr::a(&lies))]), s(r, "place"), 0.45, "observation");
    }
    for a in 0..list.len() {
        for c in a + 1..list.len() {
            fx.bond_event(s(&list[a], "id"), s(&list[c], "id"), "bluff_game");
        }
    }
    Ok(json!({"effects": fx.into_value()}))
}

/// Once a game day (11:00-21:00) each heroine may buy scratch cards: the money-minded, the hard-up and the gloomy go
/// more often; never more than 5% of her cash. Request: day, heroines [{actor, id, relation, profile, busy}]. Answer:
/// buy [{id, budget}].
fn scratch_who(r: &Value) -> Result<Value, String> {
    let day = n(r, "day") as i32;
    let mut buy = Vec::new();
    for h in r.get("heroines").and_then(Value::as_array).into_iter().flatten() {
        if b(h, "busy") {
            continue;
        }
        let Ok(rel) = serde_json::from_value::<Relation>(h.get("relation").cloned().unwrap_or(Value::Null)) else { continue };
        if rel.cash < 0 {
            continue;
        }
        let p = h.get("profile").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
        let (stress, _) = crate::mind::money_stress(&rel, &p);
        let mut rng = RandomStream::new(day.wrapping_mul(7331).wrapping_add((n(h, "actor") as i32).wrapping_mul(977)));
        let odds = 0.04 + if profile_str(&p, "material") == "Mercenary" { 0.12 } else { 0.0 } + if stress > 60.0 { 0.10 } else { 0.0 } + if rel.fun < 30.0 { 0.08 } else { 0.0 } + if profile_str(&p, "archetype") == "G" { 0.06 } else { 0.0 };
        if rng.frand() < odds && rel.cash >= crate::region::amount("cash", 400) {
            buy.push(json!({"id": rel.id, "budget": (rel.cash / 20).clamp(crate::region::amount("cash", 20), crate::region::amount("cash", 100))}));
        }
    }
    Ok(json!({"buy": buy}))
}

/// What her scratch cards did. Request: relation, spent, won. Answer: effects.
fn scratch_result(r: &Value) -> Result<Value, String> {
    let mut rel: Relation = serde_json::from_value(r.get("relation").cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))?;
    let before = rel.clone();
    let (spent, won) = (n(r, "spent") as i64, n(r, "won") as i64);
    rel.cash += won - spent;
    rel.fun = (rel.fun + if won > spent { 10.0 } else { -3.0 }).clamp(0.0, 100.0);
    let mut fx = Effects::default();
    fx.relation(&rel, &before);
    let text = if won > 0 { crate::tr::m("games.scratch_card_won", &[("spent_amount", crate::tr::a(&spent)), ("won_amount", crate::tr::a(&crate::tr::money(won)))]) } else { crate::tr::m("games.scratch_card_lost", &[("spent_amount", crate::tr::a(&spent))]) };
    fx.observe(&rel.id, "lottery", &text, "lottery", if won >= spent * 5 { 0.6 } else { 0.3 }, "observation");
    Ok(json!({"effects": fx.into_value()}))
}

/// His prediction came true or not, for each heroine who heard it: a hit raises admiration and mystique (a 0.9 memory),
/// a miss costs mystique; a streak makes her curious. Request: hit, text, streak, relations [relation].
fn prediction(r: &Value) -> Result<Value, String> {
    let hit = b(r, "hit");
    let text = s(r, "text");
    let streak = n(r, "streak") as i32;
    let mut fx = Effects::default();
    for v in r.get("relations").and_then(Value::as_array).into_iter().flatten() {
        let Ok(mut rel) = serde_json::from_value::<Relation>(v.clone()) else { continue };
        let before = rel.clone();
        let (adm, mys, tr) = if hit { (15.0, 20.0, 2.0) } else { (-3.0, -10.0, 0.0) };
        rel.admiration = (rel.admiration + adm).clamp(0.0, 100.0);
        rel.mystique = (rel.mystique + mys).clamp(0.0, 100.0);
        rel.trust = (rel.trust + tr).clamp(0.0, 100.0);
        fx.relation(&rel, &before);
        fx.note_at(&rel.id, "prediction", &if hit { crate::tr::m("games.prophecy_came_true", &[("text", crate::tr::a(&text))]) } else { crate::tr::m("games.prophecy_did_not_come_true", &[("text", crate::tr::a(&text))]) }, "", if hit { 0.9 } else { 0.4 });
        if hit && streak >= 2 {
            let mind = if streak >= 10 {
                crate::tr::m("games.ask_him_to_buy_together", &[("streak", crate::tr::a(&streak))])
            } else if streak >= 5 {
                crate::tr::m("games.want_to_ask_how", &[("streak", crate::tr::a(&streak))])
            } else {
                crate::tr::m("games.guessed_right_again", &[("streak", crate::tr::a(&streak))])
            };
            fx.recent(&rel.id, &mind);
        }
    }
    Ok(json!({"effects": fx.into_value()}))
}

/// Her own A-share account: on weekdays 09:20-13:20, each investing heroine takes her turn at her own minute. Her
/// style (chasing when she is a gambler or hard up, safe when she learned or is capable, else random) and the money in
/// (up to 30-40% of what she has, from her purse keeping ¥500, then savings keeping ¥1,500; at least ¥500). Request: day,
/// minute, weekday, everyone (development: all with money), heroines [{actor, id, relation, profile, invest_total, done,
/// busy}]. Answer: turns [{actor, id, style, deposit, from_cash, from_bank}].
fn invest_plan(r: &Value) -> Result<Value, String> {
    let (day, minute) = (n(r, "day") as i32, n(r, "minute") as i32);
    if !(560..=800).contains(&minute) || n(r, "weekday") as i32 >= 5 {
        return Ok(json!({"turns": []}));
    }
    let mut turns = Vec::new();
    for h in r.get("heroines").and_then(Value::as_array).into_iter().flatten() {
        if b(h, "done") || b(h, "busy") {
            continue;
        }
        let id = s(h, "id");
        if minute < 560 + (crate::academics::hash(&format!("{id}{day}")) % 200) as i32 {
            continue;
        }
        let Ok(rel) = serde_json::from_value::<Relation>(h.get("relation").cloned().unwrap_or(Value::Null)) else { continue };
        // Her turn came today (even when she does nothing).
        let mut turn = json!({"actor": h["actor"], "id": id, "style": "", "deposit": 0});
        let learned = rel.has_flag("learned_stocks");
        if rel.cash >= 0 && (learned || rel.has_flag("investor") || rel.greed > 70.0 || b(r, "everyone")) {
            let p = h.get("profile").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
            let (stress, _) = crate::mind::money_stress(&rel, &p);
            let competence = crate::catalog::profile_int(&p, "occupation", "competence", 50);
            let style = if profile_str(&p, "archetype") == "G" || stress > 60.0 { "chase" } else if learned || competence >= 60 { "safe" } else { "random" };
            let target = ((rel.cash + rel.bank) as f64 * if style == "safe" { 0.3 } else { 0.4 }) as i64;
            let spare = (rel.cash - crate::region::amount("cash", 500)).max(0) + (rel.bank - crate::region::amount("cash", 1500)).max(0);
            let mut deposit = spare.min(target - n(h, "invest_total") as i64).clamp(0, crate::region::amount("cash", 100_000));
            if deposit < crate::region::amount("cash", 500) {
                deposit = 0;
            }
            let from_cash = (rel.cash - crate::region::amount("cash", 500)).clamp(0, deposit);
            turn = json!({"actor": h["actor"], "id": id, "style": style, "deposit": deposit, "from_cash": from_cash, "from_bank": deposit - from_cash});
        }
        turns.push(turn);
    }
    Ok(json!({"turns": turns}))
}

/// His advice about her account: whether she takes it (trust, admiration, having learned; the careful C type less).
/// Request: relation, profile, key (code+side, for the roll), day. Answer: yes.
fn advice_answer(r: &Value) -> Result<Value, String> {
    let rel: Relation = serde_json::from_value(r.get("relation").cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))?;
    let p = r.get("profile").cloned().filter(Value::is_object).unwrap_or_else(|| json!({}));
    let chance = (0.2 + rel.trust / 200.0 + rel.admiration / 300.0 + if rel.has_flag("learned_stocks") { 0.15 } else { 0.0 } - if profile_str(&p, "archetype") == "C" { 0.15 } else { 0.0 }).clamp(0.05, 0.95);
    let seed = (crate::academics::hash(&format!("{}{}", rel.id, s(r, "key"))) as i32) ^ (n(r, "day") as i32);
    Ok(json!({"yes": RandomStream::new(seed).frand() < chance}))
}

/// A week after his advice: took it and he was right, took it and he was wrong, ignored it and he was right, ignored
/// it and he was wrong. Request: relation, took, right, what, pct. Answer: effects.
fn advice_result(r: &Value) -> Result<Value, String> {
    let mut rel: Relation = serde_json::from_value(r.get("relation").cloned().ok_or("missing_field: relation")?).map_err(|e| format!("relation: {e}"))?;
    let before = rel.clone();
    let (took, right) = (b(r, "took"), b(r, "right"));
    let (what, pct) = (s(r, "what"), n(r, "pct"));
    let (adm, mys, tr, text) = match (took, right) {
        (true, true) => (8.0, 3.0, 5.0, crate::tr::m("games.followed_tip_right", &[("what", crate::tr::a(&what)), ("change_percent", crate::tr::a(&pct))])),
        (true, false) => (-3.0, 0.0, -6.0, crate::tr::m("games.followed_tip_wrong", &[("what", crate::tr::a(&what)), ("change_percent", crate::tr::a(&pct))])),
        (false, true) => (5.0, 5.0, 2.0, crate::tr::m("games.tip_ignored_missed_gain", &[("what", crate::tr::a(&what)), ("change_percent", crate::tr::a(&pct))])),
        (false, false) => (-2.0, -2.0, 0.0, crate::tr::m("games.tip_ignored_good_call", &[("what", crate::tr::a(&what)), ("change_percent", crate::tr::a(&pct))])),
    };
    rel.admiration = (rel.admiration + adm).clamp(0.0, 100.0);
    rel.mystique = (rel.mystique + mys).clamp(0.0, 100.0);
    rel.trust = (rel.trust + tr).clamp(0.0, 100.0);
    let mut fx = Effects::default();
    fx.relation(&rel, &before);
    fx.note_at(&rel.id, "stock_advice", &text, "", if right { 0.6 } else { 0.45 });
    Ok(json!({"effects": fx.into_value()}))
}

pub fn eval(kind: &str, r: &Value) -> Option<Result<Value, String>> {
    Some(match kind {
        "advice_answer" => advice_answer(r),
        "advice_result" => advice_result(r),
        "invest_plan" => invest_plan(r),
        "bluff_calls" => bluff_calls(r),
        "bluff_play" => bluff_play(r),
        "bluff_end" => bluff_end(r),
        "scratch_who" => scratch_who(r),
        "scratch_result" => scratch_result(r),
        "prediction" => prediction(r),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cheat() {
        assert!((0.2..=0.8).contains(&bold("小美")));
        // Holding three of the rank and four claimed: she knows it is a lie.
        let v = bluff_calls(&json!({"claimed": 2, "left": 10, "window": 5, "seed": 3, "seats": [{"seat": 1, "name": "a", "held": 3}]})).unwrap();
        let calls = v["calls"].as_array().unwrap();
        if let Some(c) = calls.first() {
            let at = c["at"].as_f64().unwrap();
            assert!((0.7..=2.6).contains(&at));
        }
        // Honest when she can be.
        let v = bluff_play(&json!({"rank": 0, "hand": [0, 13, 5], "name": "a", "seed": 1})).unwrap();
        let cards: Vec<i64> = v["cards"].as_array().unwrap().iter().map(|c| c.as_i64().unwrap()).collect();
        assert!(cards.contains(&0) && cards.contains(&13));
        // Nothing of the rank: one or two of what she needs last.
        let v = bluff_play(&json!({"rank": 0, "hand": [1, 12, 25], "name": "a", "seed": 1})).unwrap();
        assert!(v["cards"].as_array().unwrap().iter().all(|c| c.as_i64().unwrap() % 13 != 0));
    }

    #[test]
    fn predictions() {
        let v = prediction(&json!({"hit": true, "text": "x", "streak": 5, "relations": [{"id": "a", "admiration": 10, "mystique": 10}]})).unwrap();
        let fx = v["effects"].as_array().unwrap();
        assert_eq!(fx[0]["set"]["admiration"], 25.0);
        assert!(fx.iter().any(|e| e["op"] == "recent"));
    }
}
