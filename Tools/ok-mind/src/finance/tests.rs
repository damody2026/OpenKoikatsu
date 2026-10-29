//! Finance against the real data in Content/IndependentResources (skipped when the data is not there).
use super::*;

fn data() -> Option<PathBuf> {
    let p = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources");
    p.join("StreetSocialData/sports/mlb_postseason.json").exists().then_some(p)
}

fn fin() -> Option<(Finance, PathBuf)> {
    let d = data()?;
    let dir = std::env::temp_dir().join(format!("okm-fin-{}-{}", std::process::id(), dates::hash(&format!("{:?}", std::thread::current().id()))));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    Some((Finance::open(&dir, &d, 12345).unwrap(), dir))
}

fn op(f: &Finance, v: Value) -> Value {
    f.op(&v).unwrap_or_else(|e| panic!("{v}: {e}"))
}

#[test]
fn mlb_2010_bet_settles_with_the_real_result() {
    let Some((f, _)) = fin() else { return };
    let board = op(&f, json!({"op": "sports_board", "date": "2010-10-05", "minute": 600, "days": 3, "back": 0}));
    let games = board["games"].as_array().unwrap();
    let g = games.iter().find(|g| g["id"] == "mlb:2010-ALDS-MIN-NYY-G1").expect("ALDS G1 listed");
    assert_eq!(g["date"], "2010-10-07"); // local 10-06 + 1 day
    assert_eq!(g["open"], true);
    assert!(g.get("home_score").is_none(), "no result before it happens");
    // NYY (away) won 6-4.
    let bet = op(&f, json!({"op": "sports_bet", "date": "2010-10-05", "minute": 600, "stake": 1000, "selections": [{"target": "mlb:2010-ALDS-MIN-NYY-G1", "market": "ml", "pick": "away"}]}));
    let odds = bet["odds"].as_f64().unwrap();
    eprintln!("CHECK mlb odds {odds}");
    // Closed on the game's Taiwan day.
    assert!(f.op(&json!({"op": "sports_bet", "date": "2010-10-07", "minute": 1, "stake": 100, "selections": [{"target": "mlb:2010-ALDS-MIN-NYY-G1", "market": "ml", "pick": "home"}]})).is_err());
    let before = op(&f, json!({"op": "tick", "date": "2010-10-07", "minute": 11 * 60}));
    assert_eq!(before["claims"], 0);
    let after = op(&f, json!({"op": "tick", "date": "2010-10-07", "minute": 12 * 60}));
    assert_eq!(after["claims"].as_i64().unwrap(), (1000.0 * odds).floor() as i64);
    let paid = op(&f, json!({"op": "claim", "date": "2010-10-07", "minute": 12 * 60, "where": "shop"}));
    assert_eq!(paid["paid"].as_i64().unwrap(), (1000.0 * odds).floor() as i64);
    // Daily cap.
    for _ in 0..5 {
        op(&f, json!({"op": "sports_bet", "date": "2010-10-08", "minute": 600, "stake": 2000, "selections": [{"target": "champ:mlb:2011", "market": "champion", "pick": "STL"}]}));
    }
    assert!(f.op(&json!({"op": "sports_bet", "date": "2010-10-08", "minute": 600, "stake": 100, "selections": [{"target": "champ:mlb:2011", "market": "champion", "pick": "STL"}]})).unwrap_err().starts_with("daily_cap"));
}

#[test]
fn prediction_hit_raises_streak() {
    let Some((f, _)) = fin() else { return };
    let p = op(&f, json!({"op": "predict", "date": "2010-10-05", "minute": 600, "heroine": "h1", "type": "sports", "name": "NYY", "outcome": "win"}));
    assert_eq!(p["ok"], true);
    let t = op(&f, json!({"op": "tick", "date": "2010-10-08", "minute": 13 * 60}));
    let e = t["events"].as_array().unwrap().iter().find(|e| e["kind"] == "prediction").expect("settled");
    assert_eq!(e["hit"], true);
    assert_eq!(e["streak"], 1);
}

#[test]
fn lotto_numbers_unknown_until_drawn() {
    let Some((f, _)) = fin() else { return };
    let info = op(&f, json!({"op": "lotto_info", "date": "2010-09-01", "minute": 600}));
    let g = &info["games"][0];
    assert!(g["recent"].as_array().unwrap().is_empty());
    assert!(g.get("numbers").is_none());
    let buy = op(&f, json!({"op": "lotto_buy", "date": "2010-09-01", "minute": 600, "game": "star649", "quick": 3, "draws": 2}));
    assert_eq!(buy["cost"], 60);
    let t = op(&f, json!({"op": "tick", "date": "2010-09-08", "minute": 600}));
    assert!(t["events"].as_array().unwrap().iter().any(|e| e["kind"] == "lotto_draw"));
    let tickets = op(&f, json!({"op": "tickets", "date": "2010-09-08", "minute": 600}));
    assert!(tickets["tickets"].as_array().unwrap().iter().all(|t| t["status"] != "open"));
    let s = op(&f, json!({"op": "scratch_buy", "date": "2010-09-08", "minute": 600, "theme": "lucky", "count": 2}));
    let id = s["tickets"][0].as_i64().unwrap();
    let r = op(&f, json!({"op": "scratch_reveal", "date": "2010-09-08", "minute": 600, "ticket": id}));
    assert_eq!(r["ticket"]["cells"].as_array().unwrap().len(), 9);
}

#[test]
fn an_afternoon_market_order_fills() {
    let Some((f, _)) = fin() else { return };
    // It fills from the real A-share bars: skipped where MarketData was not built (Tools/build_ashare_data).
    if !data().is_some_and(|d| d.join("MarketData").exists()) {
        return;
    }
    op(&f, json!({"op": "deposit", "market": "cn", "date": "2010-09-29", "minute": 833, "twd": 9000}));
    let o = op(&f, json!({"op": "order", "market": "cn", "date": "2010-09-29", "minute": 833, "code": "sh.600348", "side": "buy", "kind": "market", "qty": 500}));
    eprintln!("CHECK afternoon {o}");
    assert_eq!(o["status"], "filled", "{o}");
    // A resting limit order cancelled: its whole reserve is his to spend again.
    let before = op(&f, json!({"op": "account", "market": "cn", "date": "2010-09-29", "minute": 834}))["cash"].as_f64().unwrap();
    let l = op(&f, json!({"op": "order", "market": "cn", "date": "2010-09-29", "minute": 834, "code": "sh.600348", "side": "buy", "kind": "limit", "price": 13.5, "qty": 100}));
    assert_eq!(l["status"], "open", "{l}");
    op(&f, json!({"op": "cancel", "market": "cn", "order": l["order"], "date": "2010-09-29", "minute": 835}));
    let after = op(&f, json!({"op": "account", "market": "cn", "date": "2010-09-29", "minute": 835}));
    assert_eq!((after["cash"].as_f64().unwrap(), after["frozen"].as_f64().unwrap()), (before, 0.0), "{after}");
}

#[test]
fn a_shares_trade_in_two_sittings() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Cn) else { return };
    let wed = dates::parse("2010-09-29").unwrap();
    assert!(mk.clock(wed, 9 * 60 + 29).live.is_none());
    assert_eq!(mk.clock(wed, 9 * 60 + 30).live, Some((wed, 0.0)));
    // The midday break: closed, the screens hold the morning, it opens again at 13:00.
    let lunch = mk.clock(wed, 12 * 60);
    assert!(lunch.live.is_none());
    assert_eq!((lunch.view, lunch.next_open), (Some((wed, 0.5)), (wed, 13 * 60)));
    assert_eq!(mk.clock(wed, 13 * 60 + 52).live.unwrap().0, wed, "13:52 on a Wednesday is trading");
    let after = mk.clock(wed, 15 * 60);
    assert!(after.live.is_none());
    assert_eq!(after.complete_through, wed);
    let s = op(&f, json!({"op": "session", "market": "cn", "date": "2010-09-29", "minute": 16 * 60, "lang": "zh-CN"}));
    assert!(s["text"].as_str().unwrap_or("").contains("开市"), "{s}");
}

#[test]
fn us_sessions_follow_daylight_saving_and_fills_use_the_real_day() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Us) else { return };
    let jul = dates::parse("2025-07-15").unwrap();
    let dec = dates::parse("2025-12-15").unwrap();
    assert!(mk.clock(jul, 21 * 60 + 29).live.is_none());
    assert_eq!(mk.clock(jul, 21 * 60 + 30).live.unwrap().0, jul);
    assert!(mk.clock(dec, 22 * 60 + 29).live.is_none());
    assert_eq!(mk.clock(dec, 22 * 60 + 30).live.unwrap().0, dec);
    // 03:00 Taiwan on 07-16 is still the US 07-15 session.
    assert_eq!(mk.clock(jul + 1, 3 * 60).live.unwrap().0, jul);
    op(&f, json!({"op": "deposit", "market": "us", "date": "2010-09-01", "minute": 1300, "twd": 100000}));
    let o = op(&f, json!({"op": "order", "market": "us", "date": "2010-09-01", "minute": 22 * 60, "code": "AAPL", "side": "buy", "kind": "market", "qty": 10}));
    assert_eq!(o["status"], "filled", "{o}");
    eprintln!("CHECK aapl fill {o}");
    let bars = mk.bars("AAPL").unwrap();
    let bar = bars.iter().find(|b| b.day == dates::parse("2010-09-01").unwrap()).unwrap();
    let px = (o["price"].as_f64().unwrap() * 1000.0).round() as i32;
    assert!(px >= bar.l && px <= bar.h);
    // A year later the position is worth 10 x that day's real close.
    let acct = op(&f, json!({"op": "account", "market": "us", "date": "2011-09-02", "minute": 600}));
    let last = bars.iter().filter(|b| b.day <= dates::parse("2011-09-01").unwrap()).last().unwrap();
    let v = acct["positions"][0]["value"].as_f64().unwrap();
    assert!((v - 10.0 * last.c as f64 / 1000.0).abs() < 0.01, "{v}");
    // Closed outside the session.
    assert!(f.op(&json!({"op": "order", "market": "us", "date": "2010-09-02", "minute": 12 * 60, "code": "AAPL", "side": "sell", "kind": "market", "qty": 1})).is_err());
}

#[test]
fn ashare_t_plus_one_and_limits() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Cn) else { return };
    let Ok(bars) = mk.bars("sh.600519") else { return };
    let d = bars.iter().find(|b| b.day >= dates::parse("2010-09-01").unwrap() && !b.suspended()).unwrap().day;
    let date = dates::format(d);
    op(&f, json!({"op": "deposit", "market": "cn", "date": date, "minute": 500, "twd": 2_000_000}));
    let o = op(&f, json!({"op": "order", "market": "cn", "date": date, "minute": 600, "code": "sh.600519", "side": "buy", "kind": "market", "qty": 100}));
    assert_eq!(o["status"], "filled", "{o}");
    assert!(f.op(&json!({"op": "order", "market": "cn", "date": date, "minute": 601, "code": "sh.600519", "side": "sell", "kind": "market", "qty": 100})).unwrap_err().starts_with("t_plus_1"));
    assert!(f.op(&json!({"op": "order", "market": "cn", "date": date, "minute": 601, "code": "sh.600519", "side": "buy", "kind": "market", "qty": 50})).unwrap_err().starts_with("lot"));
    assert!(f.op(&json!({"op": "order", "market": "cn", "date": date, "minute": 15 * 60 + 1, "code": "sh.600519", "side": "buy", "kind": "market", "qty": 100})).is_err());
    let next = dates::format(mk.next_trading(d));
    let s = op(&f, json!({"op": "order", "market": "cn", "date": next, "minute": 600, "code": "sh.600519", "side": "sell", "kind": "market", "qty": 100}));
    assert_eq!(s["status"], "filled", "{s}");
    // A one-price limit-up day can never be bought.
    for code in ["sz.002594", "sz.300059", "sz.000002", "sh.600030"] {
        let Ok(bars) = mk.bars(code) else { continue };
        if let Some(b) = bars.iter().find(|b| b.lu > 0 && b.o == b.lu && b.h == b.lu && b.l == b.lu && b.c == b.lu && b.day > dates::parse("2010-09-01").unwrap()) {
            let date = dates::format(b.day);
            op(&f, json!({"op": "deposit", "market": "cn", "date": date, "minute": 500, "twd": 1_000_000}));
            let o = op(&f, json!({"op": "order", "market": "cn", "date": date, "minute": 640, "code": code, "side": "buy", "kind": "market", "qty": 100}));
            assert_eq!(o["status"], "rejected", "{o}");
            break;
        }
    }
}

#[test]
fn us_split_days_have_no_gap_and_no_limits() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Us) else { return };
    let bars = mk.bars("AAPL").unwrap();
    // As-traded prices (market.sqlite after Tools/build_us_actions.lua) jump by the split ratio on the split day;
    // split-adjusted ones (the .okbar files) have split = 1 and no jump. Either way the move net of the split is small.
    for (before, after, ratio) in [("2014-06-06", "2014-06-09", 7.0), ("2020-08-28", "2020-08-31", 4.0)] {
        let b = bars.iter().find(|x| x.day == dates::parse(before).unwrap()).unwrap();
        let a = bars.iter().find(|x| x.day == dates::parse(after).unwrap()).unwrap();
        assert!((a.split as f64 - 1.0).abs() < 1e-4 || (a.split as f64 - ratio).abs() < 1e-4, "{after}: split {}", a.split);
        let jump = (a.c as f64 * a.split as f64 / b.c as f64 - 1.0).abs();
        assert!(jump < 0.15, "{before}->{after}: {jump}");
        assert!((a.pc as f64 - b.c as f64 / a.split as f64).abs() <= 10.0, "{after}: pc {} vs {}", a.pc, b.c);
        assert_eq!(a.lu, 0);
        assert_eq!(a.ld, 0);
    }
}

#[test]
fn stamp_tax_halves_on_2023_08_28() {
    let before = dates::parse("2023-08-25").unwrap();
    let after = dates::parse("2023-08-28").unwrap();
    let value = 1_000_000_000; // CNY 1,000,000 in milli
    assert_eq!(Finance::fees(Mkt::Cn, "sell", "sh.600519", value, before).1, 1_000_000);
    assert_eq!(Finance::fees(Mkt::Cn, "sell", "sh.600519", value, after).1, 500_000);
    assert_eq!(Finance::fees(Mkt::Cn, "buy", "sh.600519", value, after).1, 0);
    assert_eq!(Finance::fees(Mkt::Us, "buy", "AAPL", value, dates::parse("2019-09-30").unwrap()).0, 7_000);
    assert_eq!(Finance::fees(Mkt::Us, "buy", "AAPL", value, dates::parse("2019-10-01").unwrap()).0, 0);
}

/// Plan §16 P12: thirty game days of ticks with the player betting, scratching and trading every day.
/// Nothing may error unexpectedly, the books must stay consistent and the file must stay small.
#[test]
fn thirty_day_long_run() {
    let Some((f, dir)) = fin() else { return };
    let start = dates::parse("2010-10-01").unwrap();
    let mut spent = 0i64;
    let mut paid = 0i64;
    let has_us = f.market(Mkt::Us).is_ok();
    if has_us {
        op(&f, json!({"op": "deposit", "market": "us", "date": "2010-10-01", "minute": 600, "twd": 300000}));
        spent += 300000;
    }
    for d in 0..30 {
        let day = start + d;
        let date = dates::format(day);
        let _ = f.op(&json!({"op": "scratch_buy", "date": date, "minute": 600, "theme": "lucky"})).map(|r| spent += r["cost"].as_i64().unwrap());
        let tickets = op(&f, json!({"op": "tickets", "date": date, "minute": 600, "count": 1}));
        if let Some(t) = tickets["tickets"].as_array().and_then(|a| a.first()).filter(|t| t["status"] == "unscratched") {
            op(&f, json!({"op": "scratch_reveal", "date": date, "minute": 601, "ticket": t["id"]}));
        }
        spent += op(&f, json!({"op": "lotto_buy", "date": date, "minute": 610, "game": if d % 2 == 0 { "star649" } else { "twinstar" }, "quick": 2}))["cost"].as_i64().unwrap();
        let board = op(&f, json!({"op": "sports_board", "date": date, "minute": 620, "days": 3, "back": 0}));
        if let Some(g) = board["games"].as_array().and_then(|a| a.iter().find(|g| g["open"] == true)) {
            if let Ok(r) = f.op(&json!({"op": "sports_bet", "date": date, "minute": 620, "stake": 500, "selections": [{"target": g["id"], "market": "ml", "pick": "home"}]})) {
                spent += r["cost"].as_i64().unwrap();
            }
        }
        for minute in (0..1440).step_by(30) {
            op(&f, json!({"op": "tick", "date": date, "minute": minute}));
            if has_us && minute == 22 * 60 + 30 && d % 3 == 0 {
                let side = if d % 2 == 0 { "buy" } else { "sell" };
                let _ = f.op(&json!({"op": "order", "market": "us", "date": date, "minute": minute, "code": "MSFT", "side": side, "kind": "limit", "price": 25.0, "qty": 20}));
            }
        }
        paid += op(&f, json!({"op": "claim", "date": date, "minute": 1439, "where": "atm"}))["paid"].as_i64().unwrap();
    }
    let s = op(&f, json!({"op": "summary", "date": dates::format(start + 30), "minute": 600}));
    assert_eq!(s["unclaimed"], 0);
    // Lottery returns are well below the stakes over a month (negative expectation), and never NaN or negative.
    assert!(paid >= 0);
    let size = std::fs::metadata(dir.join("finance.json")).unwrap().len();
    assert!(size < 2_000_000, "finance.json {size} bytes");
    eprintln!("CHECK long run: spent {spent} paid {paid} invest {}", s["invest_twd"]);
}

#[test]
fn rmb_wires_cost_one_percent_and_us_can_short() {
    let Some((f, dir)) = fin() else { return };
    drop(f);
    let f = Finance::open_with(&dir, &data().unwrap(), 9, "CNY").unwrap();
    let Ok(mk) = f.market(Mkt::Us) else { return };
    // RMB wallet: 10,000 CNY into the US account loses 1% on the wire.
    let d = op(&f, json!({"op": "deposit", "market": "us", "date": "2015-03-02", "minute": 600, "twd": 10000}));
    let usd = d["amount"].as_f64().unwrap();
    let rate = f.home_fx(Mkt::Us, dates::parse("2015-03-02").unwrap());
    assert!((usd - 10000.0 / rate * 0.99).abs() < 0.02, "{usd} {rate}");
    // Back home: another 1%.
    let w = op(&f, json!({"op": "withdraw", "market": "us", "date": "2015-03-02", "minute": 600, "amount": 100.0}));
    assert!((w["twd"].as_f64().unwrap() - (100.0 * rate * 0.99).floor()).abs() <= 1.0, "{w}");
    // Short 10 AAPL, then buy them back the next session; the position closes out.
    let day = dates::parse("2015-03-02").unwrap();
    let us_open = dates::us_open_tw_minute(day) + 60;
    let s = op(&f, json!({"op": "order", "market": "us", "date": "2015-03-02", "minute": us_open, "code": "AAPL", "side": "sell", "kind": "market", "qty": 10}));
    assert_eq!(s["status"], "filled", "{s}");
    let acct = op(&f, json!({"op": "account", "market": "us", "date": "2015-03-02", "minute": us_open + 1}));
    assert_eq!(acct["positions"][0]["qty"], -10);
    let c = op(&f, json!({"op": "order", "market": "us", "date": "2015-03-03", "minute": dates::us_open_tw_minute(day + 1) + 60, "code": "AAPL", "side": "buy", "kind": "market", "qty": 10}));
    assert_eq!(c["status"], "filled", "{c}");
    let acct = op(&f, json!({"op": "account", "market": "us", "date": "2015-03-03", "minute": 23 * 60 + 59}));
    assert!(acct["positions"].as_array().unwrap().is_empty(), "{acct}");
    // Too big a short is refused for margin.
    assert!(f.op(&json!({"op": "order", "market": "us", "date": "2015-03-03", "minute": dates::us_open_tw_minute(day + 1) + 90, "code": "AAPL", "side": "sell", "kind": "market", "qty": 100000})).unwrap_err().starts_with("margin"));
    let _ = mk;
    // A shares: no short selling, 0.5% commission.
    assert_eq!(Finance::fees(Mkt::Cn, "buy", "sz.000001", 1_000_000_000, day).0, 5_000_000);
}

#[test]
fn sqlite_matches_okbar_and_charts_are_forward_adjusted() {
    let Some(d) = data() else { return };
    let root = d.join("MarketData");
    if !market::db_path(&root).exists() {
        return;
    }
    let mk = Market::load(&root, Mkt::Cn).unwrap();
    // The database holds exactly what the .okbar file holds.
    for code in ["sh.600519", "sz.000002", "sh.000001"] {
        let Some(meta) = mk.meta(code) else { continue };
        let sub = if meta.is_index { "index" } else { "daily" };
        let Ok(file) = market::read_okbar(&root.join("cn").join(sub).join(format!("{code}.okbar"))) else { continue };
        let db = mk.bars(code).unwrap();
        assert_eq!(db.len(), file.len(), "{code}");
        for (a, b) in db.iter().zip(file.iter()) {
            assert_eq!((a.day, a.o, a.h, a.l, a.c, a.pc, a.vol, a.lu, a.ld, a.flags), (b.day, b.o, b.h, b.l, b.c, b.pc, b.vol, b.lu, b.ld, b.flags), "{code}");
            assert!((a.split - b.split).abs() < 1e-5 && (a.div - b.div).abs() < 1e-5, "{code}");
        }
    }
    // Moutai pays a dividend every year: the forward-adjusted chart has no ex-dividend gap.
    let Some((f, _)) = fin() else { return };
    let ch = op(&f, json!({"op": "chart", "market": "cn", "date": "2016-12-30", "minute": 23 * 60, "code": "sh.600519", "period": "d", "count": 2000}));
    let bars = ch["bars"].as_array().unwrap();
    for w in bars.windows(2) {
        let (pc, prev) = (w[1]["pc"].as_f64().unwrap(), w[0]["c"].as_f64().unwrap());
        assert!((pc - prev).abs() <= 0.011 + prev * 1e-4, "gap on {}: {prev} -> {pc}", w[1]["date"]);
    }
    // The last bar stays at its real price.
    let raw = mk.bars("sh.600519").unwrap();
    let last = raw.iter().filter(|b| b.day <= dates::parse("2016-12-30").unwrap()).last().unwrap();
    assert!((bars.last().unwrap()["c"].as_f64().unwrap() - last.c as f64 / 1000.0).abs() < 1e-6);
}

#[test]
fn fundamentals_never_show_before_publication() {
    let Some(d) = data() else { return };
    let root = d.join("MarketData");
    if !market::db_path(&root).exists() {
        return;
    }
    let mk = Market::load(&root, Mkt::Cn).unwrap();
    let all = mk.fundamentals("sh.600519", dates::parse("2026-10-09").unwrap());
    if all.is_empty() {
        return; // fundamentals not built yet
    }
    for day in ["2012-03-01", "2018-05-02", "2024-08-15"] {
        let d = dates::parse(day).unwrap();
        let seen = mk.fundamentals("sh.600519", d);
        assert!(seen.iter().all(|f| f.available <= d && f.period_end < d), "{day}");
        assert!(seen.len() < all.len());
    }
}

#[test]
fn us_valuation_survives_a_split_before_the_next_report() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Us) else { return };
    if mk.fundamentals("AAPL", dates::parse("2026-10-09").unwrap()).is_empty() {
        return; // fundamentals not built
    }
    // 4-for-1 on 2020-08-31; the next 10-K came on 2020-10-30. Mid-September the P/E must still be ~30, not ~8.
    for (date, lo, hi) in [("2020-09-15", 20.0, 50.0), ("2014-07-15", 10.0, 25.0), ("2012-03-01", 8.0, 25.0)] {
        let q = op(&f, json!({"op": "quote", "market": "us", "date": date, "minute": 23 * 60 + 59, "code": "AAPL"}));
        let pe = q["fundamentals"]["pe_ttm"].as_f64().unwrap_or(0.0);
        assert!(pe > lo && pe < hi, "{date}: P/E {pe} {}", q["fundamentals"]);
        assert!(dates::parse(q["fundamentals"]["available"].as_str().unwrap()).unwrap() <= dates::parse(date).unwrap());
    }
}

#[test]
fn cn_valuation_is_sane() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Cn) else { return };
    if mk.fundamentals("sh.600519", dates::parse("2026-10-09").unwrap()).is_empty() {
        return; // fundamentals not built
    }
    // Moutai: P/E roughly 10-60 over the years, P/B and market cap present, report published before the day.
    for date in ["2012-06-01", "2015-09-01", "2021-02-01", "2024-09-02"] {
        let q = op(&f, json!({"op": "quote", "market": "cn", "date": date, "minute": 23 * 60 + 59, "code": "sh.600519"}));
        let x = &q["fundamentals"];
        let pe = x["pe_ttm"].as_f64().unwrap_or(0.0);
        assert!(pe > 8.0 && pe < 80.0, "{date}: P/E {pe} {x}");
        assert!(x["pb"].as_f64().unwrap_or(0.0) > 1.0 && x["market_cap"].as_f64().unwrap_or(0.0) > 1e11, "{date}: {x}");
        assert!(dates::parse(x["available"].as_str().unwrap()).unwrap() <= dates::parse(date).unwrap());
    }
    let h = op(&f, json!({"op": "fundamentals", "market": "cn", "date": "2020-01-02", "minute": 600, "code": "sh.600519", "count": 8}));
    let periods = h["periods"].as_array().unwrap();
    assert_eq!(periods.len(), 8);
    assert!(periods.iter().all(|p| p["available"].as_str().unwrap() <= "2020-01-02"));
}

#[test]
fn nothing_after_the_game_day_is_shown() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Cn) else { return };
    let date = "2010-09-13";
    let day = dates::parse(date).unwrap();
    // BYD's A share (sz.002594) listed on 2011-06-30: in 2010 it does not exist for the player.
    assert!(f.op(&json!({"op": "quote", "market": "cn", "date": date, "minute": 600, "code": "sz.002594"})).unwrap_err().starts_with("not_listed"));
    let s = op(&f, json!({"op": "search", "market": "cn", "date": date, "minute": 600, "q": "002594"}));
    assert!(s["results"].as_array().unwrap().is_empty(), "{s}");
    let s = op(&f, json!({"op": "search", "market": "cn", "date": date, "minute": 600, "q": "比亞迪"}));
    assert!(s["results"].as_array().unwrap().iter().all(|r| r["code"] != "sz.002594"), "{s}");
    // Watch lists and movers only hold stocks that trade by then, with data up to the game day.
    for (m, op_name) in [("cn", "quotes"), ("us", "quotes"), ("cn", "movers"), ("us", "movers")] {
        let v = op(&f, json!({"op": op_name, "market": m, "date": date, "minute": 23 * 60}));
        let rows: Vec<&Value> = v["quotes"].as_array().into_iter().chain(v["up"].as_array()).chain(v["down"].as_array()).flatten().collect();
        assert!(!rows.is_empty());
        for r in rows {
            assert!(r["date"].as_str().is_some_and(|d| d <= date), "{m} {op_name}: {r}");
        }
    }
    // Charts end at the game day; the running session's bar carries only the turnover so far.
    let ch = op(&f, json!({"op": "chart", "market": "cn", "date": date, "minute": 10 * 60, "code": "sh.600519", "period": "d", "count": 30}));
    let last = ch["bars"].as_array().unwrap().last().unwrap().clone();
    assert_eq!(last["date"], date);
    let full = mk.bars("sh.600519").unwrap().iter().find(|b| b.day == day).unwrap().amount;
    assert!(last["amount"].as_f64().unwrap() < full * 0.5, "{last} vs {full}");
    // Today's *ST names do not leak into years before the stock was ST.
    let mut checked = 0;
    for meta in mk.stocks.iter().filter(|m| m.name.contains("ST")).take(40) {
        let Ok(bars) = mk.bars(&meta.code) else { continue };
        if let Some(b) = bars.iter().find(|b| !b.st() && !b.suspended()) {
            assert!(!mk.name_on(&meta.code, b.day).contains("ST"), "{} on {}: {}", meta.code, dates::format(b.day), mk.name_on(&meta.code, b.day));
            checked += 1;
        }
    }
    assert!(checked > 5, "{checked}");
}

#[test]
fn names_and_tickers_are_those_of_the_day() {
    let Some((f, _)) = fin() else { return };
    let (Ok(us), Ok(cn)) = (f.market(Mkt::Us), f.market(Mkt::Cn)) else { return };
    let d = |s: &str| dates::parse(s).unwrap();
    if us.symbol_on("GOOGL", d("2012-06-01")) == "GOOGL" {
        return; // names table not built
    }
    assert_eq!(us.symbol_on("GOOGL", d("2012-06-01")), "GOOG");
    assert!(us.name_on("GOOGL", d("2012-06-01")).contains("Google"));
    assert!(us.name_on("GOOGL", d("2016-06-01")).contains("Alphabet"));
    assert_eq!(us.symbol_on("META", d("2015-06-01")), "FB");
    assert!(us.name_on("META", d("2015-06-01")).contains("Facebook"));
    assert_eq!(us.symbol_on("META", d("2023-06-01")), "META");
    assert!(cn.name_on("sh.600104", d("2011-06-01")).contains("上海汽車"), "{}", cn.name_on("sh.600104", d("2011-06-01")));
    assert!(cn.name_on("sz.000001", d("2011-06-01")).contains("深發展"), "{}", cn.name_on("sz.000001", d("2011-06-01")));
    assert!(cn.name_on("sz.000001", d("2013-06-01")).contains("平安銀行"));
    // A quote and a search speak the same language as the day.
    let q = op(&f, json!({"op": "quote", "market": "us", "date": "2012-06-01", "minute": 23 * 60 + 59, "code": "GOOGL"}));
    assert_eq!(q["symbol"], "GOOG");
    let s = op(&f, json!({"op": "search", "market": "us", "date": "2012-06-01", "minute": 600, "q": "alphabet"}));
    assert!(s["results"].as_array().unwrap().is_empty(), "{s}");
    let s = op(&f, json!({"op": "search", "market": "us", "date": "2015-06-01", "minute": 600, "q": "META"}));
    assert!(s["results"].as_array().unwrap().iter().all(|r| r["code"] != "META"), "{s}");
}

#[test]
fn heroine_account_alerts_and_advice() {
    let Some((f, _)) = fin() else { return };
    let Ok(mk) = f.market(Mkt::Cn) else { return };
    let day = mk.calendar.iter().copied().find(|&d| d >= dates::parse("2012-03-05").unwrap()).unwrap();
    let date = dates::format(day);
    // Her turn: money in, a safe-style purchase, in her own account only.
    let r = op(&f, json!({"op": "heroine_invest", "heroine": "h1", "style": "safe", "deposit": 8000, "date": date, "minute": 600}));
    let bought: Vec<&Value> = r["actions"].as_array().unwrap().iter().filter(|a| a["side"] == "buy" && a["status"] == "filled").collect();
    assert_eq!(bought.len(), 1, "{r}");
    let hers = op(&f, json!({"op": "account", "market": "cn", "owner": "h1", "date": date, "minute": 601}));
    assert_eq!(hers["positions"].as_array().unwrap().len(), 1, "{hers}");
    let mine = op(&f, json!({"op": "account", "market": "cn", "date": date, "minute": 601}));
    assert!(mine["positions"].as_array().unwrap().is_empty() && mine["cash"].as_f64().unwrap() == 0.0, "{mine}");
    // T+1 applies to her too: the same session sells nothing.
    let again = op(&f, json!({"op": "heroine_invest", "heroine": "h1", "style": "safe", "date": date, "minute": 640}));
    assert!(again["actions"].as_array().unwrap().iter().all(|a| a["side"] != "sell" || a["qty"] == 0), "{again}");
    // Alerts fire once a session.
    let code = bought[0]["code"].as_str().unwrap();
    op(&f, json!({"op": "alert_add", "market": "cn", "code": code, "type": "above", "value": 0.01}));
    let t1 = op(&f, json!({"op": "tick", "date": date, "minute": 620}));
    let t2 = op(&f, json!({"op": "tick", "date": date, "minute": 640}));
    let count = |t: &Value| t["events"].as_array().unwrap().iter().filter(|e| e["kind"] == "alert").count();
    assert_eq!((count(&t1), count(&t2)), (1, 0), "{t1}");
    // Advice is judged a week later.
    op(&f, json!({"op": "advise", "heroine": "h1", "code": "sh.600519", "side": "buy", "accepted": true, "date": date, "minute": 650}));
    let later = dates::format(day + 8);
    let t = op(&f, json!({"op": "tick", "date": later, "minute": 20 * 60}));
    let res: Vec<&Value> = t["events"].as_array().unwrap().iter().filter(|e| e["kind"] == "advice_result").collect();
    assert_eq!(res.len(), 1, "{t}");
    assert!(res[0]["pct"].is_number() && res[0]["heroine"] == "h1");
    // Her fills carry her id so the game does not treat them as his.
    assert!(t1["events"].as_array().unwrap().iter().filter(|e| e["kind"] == "order_fill").all(|e| e["owner"] == "h1"));
}
