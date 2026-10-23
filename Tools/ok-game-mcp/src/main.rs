//! ok-game-mcp: plays StreetSocial like a player through the game's development control channel (MCP over stdio).
//! The game must run with -StreetSocialControl (game_launch does that). Everything goes through the paths a player
//! uses: walking, keys, the UI's buttons, saying things to her, waiting with the clock running. Profiling: game_exec runs
//! engine console commands, game_trace_start/stop record an Unreal Insights trace. Docs/STREET_SOCIAL_GAME_MCP.md.
mod game;

use anyhow::{Context, Result, bail};
use base64::Engine;
use game::Game;
use rmcp::{ServerHandler, ServiceExt, model::*, service::{RequestContext, RoleServer}};
use serde_json::{Value, json};
use std::sync::Arc;
use std::time::{Duration, Instant};

fn tools() -> Value {
    let t = |name: &str, description: &str, properties: Value, required: &[&str]| {
        json!({"name": name, "description": description, "inputSchema": {"type": "object", "properties": properties, "required": required}})
    };
    json!([
        t("game_launch", "Start the StreetSocial game windowed with the control channel and wait until it answers (the first start after Saved/ was cleared rebuilds the build: up to an hour). options: more launcher options, e.g. [\"-Trace\", \"default,cpu,gpu,frame\"] or [\"-Exec\", \"stat unit\"] or [\"-Sandbox\", \"name\"].",
          json!({"window": {"type": "string", "description": "WxH, default 1280x720"}, "options": {"type": "array", "items": {"type": "string"}}, "timeout_s": {"type": "number"}}), &[]),
        t("game_stop", "Stop the game on this checkout's control port (only that one; other sessions' sandbox games keep running).", json!({}), &[]),
        t("game_state", "What is going on: game time, the player, the talk, cart and table, the heroines (nearest first). full=true returns the whole raw state.",
          json!({"full": {"type": "boolean"}, "girls": {"type": "integer", "description": "how many heroines to list (default 5)"}}), &[]),
        t("game_talk", "Say something to the heroine he is talking with (as typed or spoken), let the clock run until she has answered and stopped speaking, then pause it. Returns her lines and the notice (favor change).",
          json!({"text": {"type": "string"}, "timeout_s": {"type": "number"}}), &["text"]),
        t("game_say", "Say something without waiting for her answer.", json!({"text": {"type": "string"}}), &["text"]),
        t("game_key", "Press a key as the player: E (talk), F, Escape, Tab, N (next morning), One..Four, LeftShift, or W/A/S/D held for seconds. Keys only act while the clock runs and the cursor is off (run_clock does that).",
          json!({"key": {"type": "string"}, "seconds": {"type": "number"}, "run_clock": {"type": "boolean", "description": "default true: run the clock around the press, turn the cursor off, pause after"}}), &["key"]),
        t("game_buttons", "Labels of the buttons on screen.", json!({}), &[]),
        t("game_click", "Press the on-screen button with this label (index: which of several with the same label).",
          json!({"label": {"type": "string"}, "index": {"type": "integer"}}), &["label"]),
        t("game_goto", "Walk to a heroine (slot 1..10) or a place id with the clock running, until he arrives or someone talks to him; then pause. Ends an open talk first.",
          json!({"girl": {"type": "integer"}, "place": {"type": "string"}, "timeout_s": {"type": "number"}}), &[]),
        t("game_wait", "Let the clock run: for seconds, until a game time (HH:MM), or until the walk/outing with her ends; then pause. Returns the state summary.",
          json!({"seconds": {"type": "number"}, "until": {"type": "string"}, "outing": {"type": "boolean"}, "timeout_s": {"type": "number"}}), &[]),
        t("game_clock", "Pause or run the game clock.", json!({"paused": {"type": "boolean"}}), &["paused"]),
        t("game_look", "Point the camera (yaw degrees, pitch -50..15).", json!({"yaw": {"type": "number"}, "pitch": {"type": "number"}}), &["yaw"]),
        t("game_phone", "Type into the open phone thread and send.", json!({"text": {"type": "string"}}), &["text"]),
        t("game_screenshot", "A screenshot of the game window (with UI). scale 2..4: a high-resolution shot without UI.", json!({"scale": {"type": "number"}}), &[]),
        t("game_log", "The last lines of the running game's log, optionally only those containing a text.",
          json!({"lines": {"type": "integer"}, "contains": {"type": "string"}}), &[]),
        t("game_exec", "Run an engine console command in the game and return what it printed (stat unit, stat dumphitches, t.HitchFrameTimeThreshold 60, r.ScreenPercentage 100, ...).",
          json!({"command": {"type": "string"}}), &["command"]),
        t("game_trace_start", "Start recording an Unreal Insights trace (Trace.File) to Saved/Profiling/<name>.utrace. channels default: default,cpu,gpu,frame,bookmark,loadtime.",
          json!({"name": {"type": "string"}, "channels": {"type": "string"}}), &[]),
        t("game_trace_stop", "Stop the trace being recorded (Trace.Stop); returns the file.", json!({}), &[]),
        t("game_call", "Any other control endpoint, POST with a JSON body (GET when body is omitted): /saves {op: open|save|load|new, slot}, /rules {action: status|reload|eval, request}, /mind {action}, /song {action, girl}, /money {amount}, /cursor {on}, /summon {slots}, /locale {lang, region}, /menu, /camera {actor, bone, yaw, pitch, distance}, /mic {ignore}, /navigate {stop: true}, /bluff {do, cards}.",
          json!({"path": {"type": "string"}, "body": {"type": "object"}}), &["path"]),
    ])
}

#[derive(Clone)]
struct App(Arc<Game>);

fn s(v: &Value) -> String {
    match v {
        Value::String(t) => t.clone(),
        Value::Null => "-".into(),
        other => other.to_string(),
    }
}

/// The state in a few lines (what street_play.lua state printed): enough to decide the next step.
fn summary(st: &Value, girls: usize) -> String {
    let (t, p, f, a) = (&st["time"], &st["player"], &st["free_talk"], &st["activity"]);
    let mut out = vec![
        format!("time {}{}  player ({:.0},{:.0}) place={} cash={} cursor={}", s(&t["clock"]), if t["paused"] == true { " [paused]" } else { "" },
            p["x"].as_f64().unwrap_or(0.0), p["y"].as_f64().unwrap_or(0.0), s(&p["place"]), s(&p["cash"]), s(&p["cursor_mode"])),
        format!("message: {}", s(&st["message"])),
    ];
    if st["notice"].as_str().is_some_and(|n| !n.is_empty()) {
        out.push(format!("notice: {}", s(&st["notice"])));
    }
    out.push(format!("target={} partner={} pair={} companion={} outing={}({}@{}) nav={}/{}",
        s(&st["target"]), s(&st["partner"]), s(&st["pair"]["partner"]), s(&a["companion"]), s(&a["outing_girl"]), s(&a["outing_activity"]),
        s(&a["outing_place"]), s(&a["nav_girl"]), s(&a["nav_name"])));
    out.push(format!("talk open={} busy={} her: {}", s(&f["open"]), s(&f["busy"]), s(&f["caption_her"])));
    let list = |key: &str| -> String {
        st[key].as_array().map(|v| v.iter().map(|d| format!("{}{} {}", if d["for_her"] == true { "她:" } else { "他:" }, s(&d["name"]),
            d["percent"].as_f64().map(|x| format!("{x:.0}%")).unwrap_or_default())).collect::<Vec<_>>().join(", ")).unwrap_or_default()
    };
    let (cart, table) = (list("cart"), list("table"));
    if !cart.is_empty() { out.push(format!("cart: {cart}")); }
    if !table.is_empty() { out.push(format!("table: {table}")); }
    let mut gs: Vec<&Value> = st["girls"].as_array().map(|v| v.iter().collect()).unwrap_or_default();
    gs.sort_by(|x, y| x["dist"].as_f64().unwrap_or(1e9).total_cmp(&y["dist"].as_f64().unwrap_or(1e9)));
    for g in gs.iter().take(girls) {
        let flags: Vec<&str> = ["target", "companion", "outing", "pair", "life_pose", "commute", "scheduled", "held", "promised_treat"]
            .into_iter().filter(|k| g[*k] == true).collect();
        out.push(format!("  #{} {} {} favor={:.0} dist={:.0} place={} clip={}{} speaking={} satiety={:.0} {}", s(&g["slot"]), s(&g["name"]), s(&g["stage"]),
            g["favor"].as_f64().unwrap_or(0.0), g["dist"].as_f64().unwrap_or(0.0), s(&g["place"]), s(&g["clip"]), if g["playing"] == true { "*" } else { "" },
            s(&g["speaking"]), g["satiety"].as_f64().unwrap_or(0.0), flags.join(",")));
    }
    out.join("\n")
}

fn clock(game: &Game, paused: bool) -> Result<()> {
    game.call("/clock", Some(&json!({"paused": paused}))).map(|_| ())
}

fn sleep(seconds: f64) {
    std::thread::sleep(Duration::from_secs_f64(seconds));
}

fn arg_f(a: &Value, key: &str, default: f64) -> f64 {
    a[key].as_f64().unwrap_or(default)
}

fn text(t: String) -> Value {
    json!({"content": [{"type": "text", "text": t}], "isError": false})
}

fn json_text(v: Value) -> Value {
    text(serde_json::to_string_pretty(&v).unwrap_or_default())
}

/// The talk ended (her last line said, nothing queued): navigation and other girls are free again.
fn end_talk(game: &Game) -> Result<()> {
    for _ in 0..15 {
        let st = game.call("/state", None)?;
        if st["target"] == -1 && st["pair"]["partner"] == -1 {
            return Ok(());
        }
        game.call("/cursor", Some(&json!({"on": false})))?;
        clock(game, false)?;
        game.call("/key", Some(&json!({"key": "Escape", "seconds": 0.0})))?;
        sleep(1.0);
    }
    Ok(())
}

fn run(game: &Game, name: &str, a: &Value) -> Result<Value> {
    Ok(match name {
        "game_launch" => {
            let window = a["window"].as_str().unwrap_or("1280x720");
            let options: Vec<String> = a["options"].as_array().map(|v| v.iter().filter_map(|x| x.as_str().map(String::from)).collect()).unwrap_or_default();
            let r = game.launch(window, &options, Duration::from_secs_f64(arg_f(a, "timeout_s", 3600.0)))?;
            json_text(r)
        }
        "game_stop" => json_text(game.stop()?),
        "game_state" => {
            let st = game.call("/state", None)?;
            if a["full"] == true { json_text(st) } else { text(summary(&st, a["girls"].as_u64().unwrap_or(5) as usize)) }
        }
        "game_say" => json_text(game.call("/say", Some(&json!({"text": a["text"]})))?),
        "game_talk" => {
            let r = game.call("/say", Some(&json!({"text": a["text"]})))?;
            if r["ok"] != true {
                bail!("not said: {}", s(&r["error"]));
            }
            clock(game, false)?;
            let start = Instant::now();
            let (mut lines, mut last) = (Vec::<String>::new(), String::new());
            sleep(1.0);
            while start.elapsed().as_secs_f64() < arg_f(a, "timeout_s", 120.0) {
                let st = game.call("/state", None)?;
                let her = st["free_talk"]["caption_her"].as_str().unwrap_or("").to_string();
                if !her.is_empty() && her != last {
                    lines.push(her.clone());
                    last = her;
                }
                let speaking = st["girls"].as_array().is_some_and(|g| g.iter().any(|x| x["speaking"] == true));
                let f = &st["free_talk"];
                if start.elapsed().as_secs_f64() > 3.0 && f["busy"] != true && !speaking && f["queued_events"].as_i64().unwrap_or(0) == 0 {
                    break;
                }
                sleep(0.7);
            }
            clock(game, true)?;
            let st = game.call("/state", None)?;
            let mut out: Vec<String> = lines.iter().map(|l| format!("her: {l}")).collect();
            out.push(format!("{} | {} | {}", s(&st["time"]["clock"]), s(&st["message"]), s(&st["notice"])));
            text(out.join("\n"))
        }
        "game_key" => {
            let held = arg_f(a, "seconds", 0.0);
            let around = a["run_clock"] != false;
            if around {
                game.call("/cursor", Some(&json!({"on": false})))?;
                clock(game, false)?;
            }
            let r = game.call("/key", Some(&json!({"key": a["key"], "seconds": held})))?;
            if around {
                sleep(held.max(0.0) + 1.5);
                clock(game, true)?;
            }
            json_text(r)
        }
        "game_buttons" => json_text(game.call("/buttons", None)?["buttons"].clone()),
        "game_click" => {
            let was = game.call("/state", None)?["player"]["cursor_mode"] == true;
            if !was {
                // The game only takes mouse events once cursor mode has been on for a frame.
                game.call("/cursor", Some(&json!({"on": true})))?;
                sleep(0.4);
            }
            let body = json!({"label": a["label"], "index": a["index"].as_i64().unwrap_or(0)});
            let mut r = game.call("/click", Some(&body))?;
            if r["ok"] != true && s(&r["error"]).contains("scrolled") {
                sleep(0.8); // below the fold: the panel scrolled to it, press again
                r = game.call("/click", Some(&body))?;
            }
            if !was {
                sleep(0.3);
                game.call("/cursor", Some(&json!({"on": false})))?;
            }
            json_text(r)
        }
        "game_goto" => {
            let body = if let Some(g) = a["girl"].as_i64() { json!({"girl": g}) } else if let Some(p) = a["place"].as_str() { json!({"place": p}) } else { bail!("girl or place is required") };
            end_talk(game)?;
            let r = game.call("/navigate", Some(&body))?;
            if r["ok"] != true {
                bail!("navigation refused: {}", s(&r["error"]));
            }
            clock(game, false)?;
            let start = Instant::now();
            sleep(2.0);
            while start.elapsed().as_secs_f64() < arg_f(a, "timeout_s", 300.0) {
                let st = game.call("/state", None)?;
                let act = &st["activity"];
                if (act["nav_girl"] == -1 && act["nav_point"] != true) || st["target"] != -1 {
                    break;
                }
                sleep(1.0);
            }
            clock(game, true)?;
            text(summary(&game.call("/state", None)?, 5))
        }
        "game_wait" => {
            let until = a["until"].as_str().map(String::from);
            let outing = a["outing"] == true;
            let seconds = a["seconds"].as_f64();
            if until.is_none() && !outing && seconds.is_none() {
                bail!("seconds, until or outing is required");
            }
            clock(game, false)?;
            let start = Instant::now();
            let limit = seconds.unwrap_or(arg_f(a, "timeout_s", 600.0));
            sleep(1.0);
            while start.elapsed().as_secs_f64() < limit {
                if until.is_some() || outing {
                    let st = game.call("/state", None)?;
                    if outing && st["activity"]["outing_girl"] == -1 {
                        break;
                    }
                    if let Some(u) = &until {
                        if st["time"]["clock"].as_str().is_some_and(|c| c >= u.as_str()) {
                            break;
                        }
                    }
                }
                sleep(1.0);
            }
            clock(game, true)?;
            text(summary(&game.call("/state", None)?, 5))
        }
        "game_clock" => json_text(game.call("/clock", Some(&json!({"paused": a["paused"] != false})))?),
        "game_look" => {
            let mut body = json!({"yaw": arg_f(a, "yaw", 0.0)});
            if let Some(p) = a["pitch"].as_f64() {
                body["pitch"] = json!(p);
            }
            json_text(game.call("/look", Some(&body))?)
        }
        "game_phone" => json_text(game.call("/phone", Some(&json!({"text": a["text"]})))?),
        "game_screenshot" => {
            let r = game.call("/screenshot", Some(&json!({"scale": arg_f(a, "scale", 1.0)})))?;
            let file = r["path"].as_str().context("no screenshot path")?.to_string();
            // Written a frame or two later; wait until it is there and complete.
            let start = Instant::now();
            let mut last = 0;
            loop {
                let size = std::fs::metadata(&file).map(|m| m.len()).unwrap_or(0);
                if size > 0 && size == last {
                    break;
                }
                if start.elapsed() > Duration::from_secs(15) {
                    bail!("screenshot not written: {file}");
                }
                last = size;
                sleep(0.4);
            }
            let img = image::open(&file)?.to_rgb8();
            let img = if img.width() > 1600 { image::imageops::resize(&img, 1600, img.height() * 1600 / img.width(), image::imageops::FilterType::Triangle) } else { img };
            let mut jpeg = Vec::new();
            image::codecs::jpeg::JpegEncoder::new_with_quality(&mut jpeg, 85).encode_image(&img)?;
            json!({"content": [{"type": "text", "text": file}, {"type": "image", "data": base64::engine::general_purpose::STANDARD.encode(jpeg), "mimeType": "image/jpeg"}], "isError": false})
        }
        "game_log" => {
            let r = game.log(a["lines"].as_u64().unwrap_or(60) as usize, a["contains"].as_str())?;
            text(format!("{}\n{}", s(&r["file"]), s(&r["lines"])))
        }
        "game_exec" => json_text(game.call("/exec", Some(&json!({"command": a["command"]})))?),
        "game_trace_start" => {
            let dir = game.root.join("Saved/Profiling");
            std::fs::create_dir_all(&dir)?;
            let name = a["name"].as_str().map(String::from).unwrap_or_else(|| format!("street-{}", std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)));
            let file = dir.join(format!("{name}.utrace"));
            let channels = a["channels"].as_str().unwrap_or("default,cpu,gpu,frame,bookmark,loadtime");
            let r = game.call("/exec", Some(&json!({"command": format!("Trace.File \"{}\" {channels}", file.to_string_lossy().replace('\\', "/"))})))?;
            json_text(json!({"file": file.to_string_lossy(), "channels": channels, "reply": r}))
        }
        "game_trace_stop" => json_text(game.call("/exec", Some(&json!({"command": "Trace.Stop"})))?),
        "game_call" => {
            let path = a["path"].as_str().context("path is required")?;
            let path = if path.starts_with('/') { path.to_string() } else { format!("/{path}") };
            json_text(game.call(&path, if a["body"].is_null() { None } else { Some(&a["body"]) })?)
        }
        other => bail!("unknown tool {other}"),
    })
}

impl ServerHandler for App {
    async fn list_tools(&self, _r: Option<PaginatedRequestParams>, _c: RequestContext<RoleServer>) -> Result<ListToolsResult, rmcp::ErrorData> {
        serde_json::from_value(json!({"tools": tools()})).map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))
    }
    async fn call_tool(&self, request: CallToolRequestParams, _c: RequestContext<RoleServer>) -> Result<CallToolResponse, rmcp::ErrorData> {
        let args = Value::Object(request.arguments.unwrap_or_default());
        let game = self.0.clone();
        let name = request.name.to_string();
        // The control channel is blocking HTTP and the waits are real time: off the async runtime.
        let result = tokio::task::spawn_blocking(move || run(&game, &name, &args)).await;
        let value = match result {
            Ok(Ok(v)) => v,
            Ok(Err(e)) => json!({"content": [{"type": "text", "text": format!("{e:#}")}], "isError": true}),
            Err(e) => json!({"content": [{"type": "text", "text": e.to_string()}], "isError": true}),
        };
        serde_json::from_value::<CallToolResult>(value).map(Into::into).map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))
    }
    fn get_info(&self) -> ServerConfig {
        ServerConfig::new(ServerCapabilities::builder().enable_tools().build())
            .with_server_info(Implementation::new("ok-game", "0.1.0"))
            .with_instructions("StreetSocial through its control channel. game_launch if game_state says the channel is not up. Keys act only while the clock runs (game_key does it). Talk: walk to her (game_goto girl), game_key E, then game_talk. Profiling: game_exec, game_trace_start/stop.")
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let game = Arc::new(Game::locate()?);
    let argv: Vec<String> = std::env::args().collect();
    if argv.get(1).map(String::as_str) == Some("--check") {
        // Development check without an MCP client: the checkout, the port and whether the game answers.
        println!("root={} port={} up={}", game.root.display(), game.port, game.up());
        return Ok(());
    }
    if argv.get(1).map(String::as_str) == Some("call") {
        // ok-game-mcp call TOOL [JSON]: the same tools from a shell (scripts, or a session without the MCP loaded).
        let name = argv.get(2).context("call TOOL [JSON]")?.clone();
        let args: Value = argv.get(3).map(|j| serde_json::from_str(j)).transpose()?.unwrap_or(json!({}));
        let r = tokio::task::spawn_blocking(move || run(&game, &name, &args)).await??;
        for c in r["content"].as_array().into_iter().flatten() {
            if c["type"] == "text" { println!("{}", s(&c["text"])); } else { println!("[{} {} bytes]", s(&c["type"]), c["data"].as_str().map(str::len).unwrap_or(0)); }
        }
        return Ok(());
    }
    let service = App(game).serve(rmcp::transport::stdio()).await?;
    service.waiting().await?;
    Ok(())
}
