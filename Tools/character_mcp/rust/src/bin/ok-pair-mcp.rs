//! MCP server (stdio) for tuning the running interaction pair scene without restarting it.
//!
//! It talks to the scene's control port (AOKInteractionPairGameMode -PairControl, started by
//! `Tools/pair_ctl.lua start`): /exec?cmd=a;b  /reset  /stats  /pose?filter=
//! Every OKPhysXSecondary.* tunable is a console variable read each frame, so a sweep is: set cvars, settle, reset
//! the statistics, wait, read them back. Formerly OK_Demo's Scripts/mcp/ok_demo_mcp.py.
//!
//!   ok-pair-mcp [--port 47871] [--log Saved/Logs/pair_control.log] [--pose 後背位]
use anyhow::{Context, Result, bail};
use clap::Parser;
use rmcp::{
    ServerHandler, ServiceExt,
    model::*,
    service::{RequestContext, RoleServer},
};
use serde_json::{Map, Value, json};
use std::{
    io::{Read, Write},
    net::TcpStream,
    path::PathBuf,
    time::{Duration, Instant},
};

#[derive(Parser, Clone)]
#[command(version, about = "OpenKoikatsu interaction pair physics tuning MCP server")]
struct Args {
    #[arg(long, default_value_t = 47871)]
    port: u16,
    /// Log of the running scene; a new "PairControl measuring" line means a restarted pose is playing.
    #[arg(long, default_value = "Saved/Logs/pair_control.log")]
    log: PathBuf,
    /// Paired action measured after a restart.
    #[arg(long, default_value = "後背位")]
    pose: String,
    #[command(subcommand)]
    command: Option<Command>,
}
#[derive(clap::Subcommand, Clone)]
enum Command {
    /// Compare the solvers with their tuned parameters on the running scene (one JSON line per configuration).
    /// Default: rounds outside, configurations inside, median per metric. --steady: one configuration at a time,
    /// warm-up window discarded, then the average of two windows (as a user would switch and watch).
    Compare {
        #[arg(long, default_value_t = 10.0)]
        seconds: f64,
        #[arg(long, default_value_t = 3)]
        rounds: usize,
        #[arg(long)]
        steady: bool,
    },
}

/// Every tunable back to its default before a configuration (formerly OK_Demo's Scripts/mcp/compare_tuned.py).
const COMPARE_RESET: &[&str] = &["Enabled 1", "AltSafetyNet 0", "AltSubstepCollision 0", "AltRadiusScale 1", "AltSubstepScale 1",
    "AltDampingScale 1", "AltDriveScale 1", "AltLimitScale 1", "JoltIterations 4", "JoltFriction 0.2", "JoltCollisionSteps 1",
    "MuJoCoIterations 100", "MuJoCoSlack 0.001", "MuJoCoTimeConst 0.01", "MuJoCoTendonTimeConst 0.01", "MuJoCoDampRatio 1"];
const SAFE: &str = "AltSafetyNet 1;AltSubstepCollision 1";
/// The tuned configurations found with pair_sweep (Bullet left out: not shipped).
fn compare_configs() -> Vec<(&'static str, String)> {
    vec![
        ("PhysX 5.11", "Backend 0".into()),
        ("Verlet (original)", "Enabled 0".into()),
        ("Kawaii (tuned)", "Backend 1;AltSafetyNet 1".into()),
        ("Chaos (tuned)", "Backend 2;AltSafetyNet 1".into()),
        ("Jolt (default)", "Backend 4".into()),
        ("Jolt (tuned)", format!("Backend 4;{SAFE};AltRadiusScale 0.01;JoltIterations 1")),
        ("MuJoCo (default)", "Backend 5".into()),
        ("MuJoCo (tuned, no clipping)", format!("Backend 5;{SAFE};MuJoCoIterations 5;MuJoCoSlack 0.01;MuJoCoTimeConst 0.008")),
        ("MuJoCo (tuned, smooth)", format!("Backend 5;{SAFE};MuJoCoIterations 5;MuJoCoSlack 0.01;MuJoCoDampRatio 1.5")),
    ]
}
const COMPARE_KEYS: &[&str] = &["solveMsPerSecond", "gameThreadMs", "maxPenetration", "trueViolationsPer1k", "popsPer1k", "jitterPer1k", "stretch"];

fn prefixed(cvars: &str) -> String {
    cvars.split(';').map(|c| format!("OKPhysXSecondary.{c}")).collect::<Vec<_>>().join(";")
}

fn compare(pair: &Pair, seconds: f64, rounds: usize, steady: bool) -> Result<()> {
    let reset = COMPARE_RESET.iter().map(|c| format!("OKPhysXSecondary.{c}")).collect::<Vec<_>>().join(";");
    let metric = |r: &Value, k: &str| r["metrics"][k].as_f64().unwrap_or(0.0);
    let report = |name: &str, cvars: &str, rows: &[Value], average: bool| {
        let mut line = json!({"config": name, "cvars": cvars});
        for k in COMPARE_KEYS {
            let mut v: Vec<f64> = rows.iter().map(|r| metric(r, k)).collect();
            v.sort_by(f64::total_cmp);
            let value = if average { v.iter().sum::<f64>() / v.len().max(1) as f64 } else { v.get(v.len() / 2).copied().unwrap_or(0.0) };
            line[*k] = json!((value * 1000.0).round() / 1000.0);
        }
        for k in ["trueViolationsPer1k", "jitterPer1k"] {
            let v: Vec<f64> = rows.iter().map(|r| metric(r, k)).collect();
            line[format!("{k}Range")] = json!([v.iter().copied().fold(f64::INFINITY, f64::min), v.iter().copied().fold(f64::NEG_INFINITY, f64::max)]);
        }
        println!("{line}");
    };
    let configs = compare_configs();
    if steady {
        for (name, cfg) in &configs {
            pair.exec(&reset)?;
            pair.measure(&prefixed(cfg), seconds, 2.0, true)?; // warm-up: the state the previous solver left washes out
            let rows = vec![pair.measure(&prefixed(cfg), seconds, 2.0, true)?, pair.measure(&prefixed(cfg), seconds, 2.0, true)?];
            report(name, cfg, &rows, true);
        }
    } else {
        // Rounds outside, configurations inside: slow drift of the scene and the state a previous solver leaves behind
        // hit every configuration alike; the median drops a single outlier.
        let mut results: Vec<Vec<Value>> = vec![Vec::new(); configs.len()];
        for _ in 0..rounds.max(1) {
            for (i, (_, cfg)) in configs.iter().enumerate() {
                pair.exec(&reset)?;
                results[i].push(pair.measure(&prefixed(cfg), seconds, 2.0, true)?);
            }
        }
        for ((name, cfg), rows) in configs.iter().zip(&results) { report(name, cfg, rows, false); }
    }
    pair.exec(&format!("{reset};OKPhysXSecondary.Backend -1"))?;
    Ok(())
}

/// name, applies to, default, meaning. Everything here is read every frame unless noted. (3 was Bullet: not shipped.)
const TUNABLES: &[(&str, &str, &str, &str)] = &[
    ("OKPhysXSecondary.Backend", "all", "-1", "-1 per component (card okPh, else hair/breasts/hips DynamicBone, rest PhysX), 0 PhysX, 1 Kawaii, 2 Chaos, 4 Jolt, 5 MuJoCo, 6 DynamicBone+collision (3 = Bullet, not shipped: runs PhysX)"),
    ("OKPhysXSecondary.Enabled", "all", "1", "0 = original Verlet solver"),
    ("OKPhysXSecondary.ResetState", "all (command)", "-", "put every chain back on its animated pose and rebuild solver state (also automatic when Backend/Enabled changes)"),
    ("OKPhysXSecondary.AltSafetyNet", "Kawaii/Chaos/Jolt/MuJoCo", "0", "1 = also run PhysX's blow-up reset and final exact pass"),
    ("OKPhysXSecondary.AltSubstepCollision", "Jolt/MuJoCo", "0", "1 = also run PhysX's per-substep contact (skin layer + exact push-out, per-particle radii)"),
    ("OKPhysXSecondary.ContactStiffness", "PhysX + AltSubstepCollision", "?", "skin layer spring stiffness (shared with PhysX)"),
    ("OKPhysXSecondary.ContactSkin", "PhysX + AltSubstepCollision", "?", "skin layer thickness (m) (shared with PhysX)"),
    ("OKPhysXSecondary.AltSubstepScale", "Kawaii/Chaos/Jolt/MuJoCo", "1", "substeps = PhysX substep count x this"),
    ("OKPhysXSecondary.AltDriveScale", "Jolt/MuJoCo", "1", "goal spring stiffness x this"),
    ("OKPhysXSecondary.AltDampingScale", "Jolt/MuJoCo", "1", "DynamicBone damping x this"),
    ("OKPhysXSecondary.AltLimitScale", "Jolt/MuJoCo", "1", "'stay near the goal' limit stiffness x this"),
    ("OKPhysXSecondary.AltRadiusScale", "Jolt/MuJoCo", "1", "particle collision radius x this"),
    ("OKPhysXSecondary.HardTolerance", "PhysX + AltSafetyNet", "0.002", "exact pass ignores errors below this (m)"),
    ("OKPhysXSecondary.HardMaxStep", "PhysX + AltSafetyNet", "0.01", "exact pass moves a particle at most this per frame (m)"),
    ("OKPhysXSecondary.ProjectionPasses", "PhysX + AltSafetyNet", "3", "exact pass iterations"),
    ("OKPhysXSecondary.ChaosIterations", "Chaos", "4", "constraint iterations per substep (rebuild)"),
    ("OKPhysXSecondary.JoltIterations", "Jolt", "4", "soft body iterations per substep"),
    ("OKPhysXSecondary.JoltCollisionSteps", "Jolt", "1", "collision steps per substep"),
    ("OKPhysXSecondary.JoltFriction", "Jolt", "0.2", "friction"),
    ("OKPhysXSecondary.JoltCompliance", "Jolt", "0", "bone edge compliance, 0 = rigid (rebuilds soft bodies)"),
    ("OKPhysXSecondary.MuJoCoIterations", "MuJoCo", "100", "mjOption.iterations"),
    ("OKPhysXSecondary.MuJoCoTimeConst", "MuJoCo", "0.01", "contact solref time constant (s)"),
    ("OKPhysXSecondary.MuJoCoTendonTimeConst", "MuJoCo", "0.01", "bone-length tendon solref time constant (s)"),
    ("OKPhysXSecondary.MuJoCoDampRatio", "MuJoCo", "1", "solref damping ratio"),
    ("OKPhysXSecondary.MuJoCoSlack", "MuJoCo", "0.001", "bone length slack fraction"),
    ("OKPhysXSecondary.MuJoCoImpRatio", "MuJoCo", "1", "mjOption.impratio"),
    ("OKPhysXSecondary.MuJoCoFriction", "MuJoCo", "0.2", "sliding friction"),
    ("OKPhysXSecondary.MuJoCoMargin", "MuJoCo", "0", "contact margin (m)"),
    ("OKPhysXSecondary.Hardness", "all (shared)", "10", "1 soft .. 10 hard; also changes PhysX"),
    ("OKPhysXSecondary.DriveDamping", "all (shared)", "1", "goal spring damping ratio; also changes PhysX"),
];

fn encode(s: &str) -> String {
    s.bytes().map(|b| if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) { (b as char).to_string() } else { format!("%{b:02X}") }).collect()
}

/// Blocking GET on the local control port; the body as text.
fn http(port: u16, path: &str, timeout: Duration) -> Result<String> {
    let mut stream = TcpStream::connect(("127.0.0.1", port)).with_context(|| format!("interaction pair scene not listening on 127.0.0.1:{port} (Tools/pair_ctl.lua start)"))?;
    stream.set_read_timeout(Some(timeout))?;
    write!(stream, "GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nConnection: close\r\n\r\n")?;
    let mut raw = Vec::new();
    stream.read_to_end(&mut raw)?;
    let text = String::from_utf8_lossy(&raw).into_owned();
    let (head, body) = text.split_once("\r\n\r\n").context("malformed HTTP response")?;
    if !head.starts_with("HTTP/1.1 200") && !head.starts_with("HTTP/1.0 200") { bail!("{}", head.lines().next().unwrap_or("HTTP error")); }
    // The scene answers with Content-Length; a chunked reply is decoded here as well.
    if head.to_ascii_lowercase().contains("transfer-encoding: chunked") {
        let mut out = String::new();
        let mut rest = body;
        while let Some((size, tail)) = rest.split_once("\r\n") {
            let n = usize::from_str_radix(size.trim(), 16).unwrap_or(0);
            if n == 0 || tail.len() < n { break; }
            out.push_str(&tail[..n]);
            rest = tail[n..].trim_start_matches("\r\n");
        }
        return Ok(out);
    }
    Ok(body.to_owned())
}

/// The number after `key=` where key starts a word.
fn field<'a>(text: &'a str, key: &str) -> Option<&'a str> {
    let pattern = format!("{key}=");
    let mut from = 0;
    while let Some(i) = text[from..].find(&pattern) {
        let at = from + i;
        let boundary = at == 0 || !text[..at].chars().next_back().is_some_and(|c| c.is_alphanumeric() || c == '_');
        if boundary {
            let value = &text[at + pattern.len()..];
            let end = value.find(|c: char| !(c.is_ascii_digit() || c == '.' || c == '-' || c == '/')).unwrap_or(value.len());
            return Some(&value[..end]);
        }
        from = at + pattern.len();
    }
    None
}

/// The numbers of /stats as a flat object (solver, cost, frame time, clipping, jitter, stretch ...).
fn parse_stats(text: &str) -> Value {
    let mut out = Map::new();
    if let Some(i) = text.find("solver=") {
        let v: String = text[i + 7..].chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
        out.insert("solver".into(), json!(v));
    }
    for key in ["seconds", "solveMsPerSecond", "maxPenetration", "violations", "maxProjection", "resets", "steps", "pops", "jitter", "maxJerk", "swing", "stretch", "maxSwing"] {
        if let Some(v) = field(text, key).and_then(|v| v.split('/').next()).and_then(|v| v.parse::<f64>().ok()) { out.insert(key.into(), json!(v)); }
    }
    if let Some(v) = field(text, "hard").and_then(|v| v.parse::<i64>().ok()) { out.insert("hardFixes".into(), json!(v)); }
    for (key, depth, count) in [("true", "trueDepth", "trueViolations"), ("softTissue", "softTissueDepth", "softTissueViolations")] {
        if let Some((d, n)) = field(text, key).and_then(|v| v.split_once('/')) {
            if let (Ok(d), Ok(n)) = (d.parse::<f64>(), n.parse::<i64>()) { out.insert(depth.into(), json!(d)); out.insert(count.into(), json!(n)); }
        }
    }
    if let Some(i) = text.find("GT ") {
        let v: String = text[i + 3..].chars().take_while(|c| c.is_ascii_digit() || *c == '.').collect();
        if let Ok(v) = v.parse::<f64>() { out.insert("gameThreadMs".into(), json!(v)); }
    }
    // Rates per 1000 chain steps, so runs with different frame rates compare.
    let steps = out.get("steps").and_then(Value::as_f64).unwrap_or(0.0);
    if steps > 0.0 {
        for key in ["trueViolations", "pops", "jitter", "violations"] {
            if let Some(v) = out.get(key).and_then(Value::as_f64) {
                out.insert(format!("{key}Per1k"), json!((v * 1000.0 / steps * 100.0).round() / 100.0));
            }
        }
    }
    Value::Object(out)
}

#[derive(Clone)]
struct Pair(Args);

impl Pair {
    fn get(&self, path: &str) -> Result<String> { http(self.0.port, path, Duration::from_secs(60)) }
    fn exec(&self, commands: &str) -> Result<String> {
        let joined: Vec<&str> = commands.split([';', '\n']).map(str::trim).filter(|c| !c.is_empty()).collect();
        if joined.is_empty() { return Ok(String::new()); }
        self.get(&format!("/exec?cmd={}", encode(&joined.join(";"))))
    }
    fn ready_count(&self) -> i64 {
        std::fs::read(&self.0.log).map(|b| b.windows(20).filter(|w| *w == b"PairControl measuring").count() as i64).unwrap_or(-1)
    }
    /// Reload the audited action from its first frame and wait until the scene measures again (repeatable windows).
    fn restart_pose(&self, pose: &str) -> Result<bool> {
        let before = self.ready_count();
        self.get(&format!("/pose?filter={}", encode(pose)))?;
        let deadline = Instant::now() + Duration::from_secs(120);
        while Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(500));
            if self.ready_count() > before { return Ok(true); }
        }
        Ok(false)
    }
    fn measure(&self, cvars: &str, seconds: f64, settle: f64, restart: bool) -> Result<Value> {
        if !cvars.is_empty() { self.exec(cvars)?; }
        if restart {
            // Chains back on the animated pose with fresh solver state, then the action from its first frame.
            self.exec("OKPhysXSecondary.ResetState")?;
            self.restart_pose(&self.0.pose)?;
        }
        std::thread::sleep(Duration::from_secs_f64(settle.max(0.0)));
        self.get("/reset")?;
        std::thread::sleep(Duration::from_secs_f64(seconds.max(0.5)));
        let text = self.get("/stats")?;
        Ok(json!({"cvars": cvars, "metrics": parse_stats(&text), "raw": text}))
    }
    fn call(&self, name: &str, args: &Value) -> Result<String> {
        let num = |k: &str, d: f64| args.get(k).and_then(Value::as_f64).unwrap_or(d);
        let flag = |k: &str| args.get(k).and_then(Value::as_bool).unwrap_or(true);
        let text = |k: &str| args.get(k).and_then(Value::as_str).unwrap_or("").to_owned();
        Ok(match name {
            "pair_exec" => self.exec(&text("commands"))?,
            "pair_get_cvars" => {
                // Typing a console variable's name alone prints "Name = "value"".
                let names: Vec<String> = args.get("names").and_then(Value::as_array).context("names required")?.iter().filter_map(|v| v.as_str().map(str::to_owned)).collect();
                self.exec(&names.join(";"))?
            }
            "pair_reset" => self.get("/reset")?,
            "pair_stats" => { let raw = self.get("/stats")?; serde_json::to_string_pretty(&json!({"metrics": parse_stats(&raw), "raw": raw}))? }
            "pair_measure" => serde_json::to_string_pretty(&self.measure(&text("cvars"), num("seconds", 10.0), num("settle", 2.0), flag("restart"))?)?,
            "pair_sweep" => {
                let base = text("base");
                if !base.is_empty() { self.exec(&base)?; }
                let param = text("param");
                let mut rows = Vec::new();
                for v in args.get("values").and_then(Value::as_array).context("values required")? {
                    let value = v.as_str().map(str::to_owned).unwrap_or_else(|| v.to_string());
                    let r = self.measure(&format!("{param} {value}"), num("seconds", 10.0), num("settle", 2.0), flag("restart"))?;
                    let mut row = json!({"value": v});
                    if let (Some(row), Some(m)) = (row.as_object_mut(), r["metrics"].as_object()) { row.extend(m.clone()); }
                    rows.push(row);
                }
                serde_json::to_string_pretty(&rows)?
            }
            "pair_pose" => self.get(&format!("/pose?filter={}", encode(&text("filter"))))?,
            "pair_tunables" => {
                let all: Map<String, Value> = TUNABLES.iter().map(|(k, b, d, m)| (k.to_string(), json!({"backend": b, "default": d, "meaning": m}))).collect();
                serde_json::to_string_pretty(&all)?
            }
            other => bail!("unknown tool {other}"),
        })
    }
}

fn tools() -> Value {
    let empty = json!({"type": "object", "properties": {}});
    json!([
        {"name": "pair_exec", "description": "Run console commands in the running interaction pair scene (';' separated), e.g. 'OKPhysXSecondary.Backend 4;OKPhysXSecondary.JoltIterations 8'. Returns each command's output.",
         "inputSchema": {"type": "object", "properties": {"commands": {"type": "string"}}, "required": ["commands"]}},
        {"name": "pair_get_cvars", "description": "Read the current values of console variables.",
         "inputSchema": {"type": "object", "properties": {"names": {"type": "array", "items": {"type": "string"}}}, "required": ["names"]}},
        {"name": "pair_reset", "description": "Reset the clipping/jitter/performance statistics.", "inputSchema": empty},
        {"name": "pair_stats", "description": "Statistics since the last reset, raw and parsed.", "inputSchema": empty},
        {"name": "pair_measure", "description": "Apply cvars, wait settle seconds, reset statistics, wait seconds, return parsed metrics (solver cost, game-thread ms, max penetration, visible clipping (trueViolations), pops/jitter, stretch).",
         "inputSchema": {"type": "object", "properties": {"cvars": {"type": "string"}, "seconds": {"type": "number", "default": 10}, "settle": {"type": "number", "default": 2},
                         "restart": {"type": "boolean", "default": true, "description": "reload the action from its first frame and reset the solver state first, so every measurement covers the same animation from the same start"}}}},
        {"name": "pair_sweep", "description": "Apply base cvars once, then measure each value of one parameter. Returns one row of metrics per value.",
         "inputSchema": {"type": "object", "properties": {"base": {"type": "string"}, "param": {"type": "string"}, "values": {"type": "array", "items": {"type": ["number", "string"]}},
                         "seconds": {"type": "number", "default": 10}, "settle": {"type": "number", "default": 2}, "restart": {"type": "boolean", "default": true}}, "required": ["param", "values"]}},
        {"name": "pair_pose", "description": "Switch the paired action being audited (e.g. '後背位').",
         "inputSchema": {"type": "object", "properties": {"filter": {"type": "string"}}, "required": ["filter"]}},
        {"name": "pair_tunables", "description": "List the solver parameters that can be tuned at runtime, per backend, with defaults.", "inputSchema": empty},
    ])
}

impl ServerHandler for Pair {
    async fn list_tools(&self, _request: Option<PaginatedRequestParams>, _context: RequestContext<RoleServer>) -> Result<ListToolsResult, rmcp::ErrorData> {
        serde_json::from_value(json!({"tools": tools()})).map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))
    }
    async fn call_tool(&self, request: CallToolRequestParams, _context: RequestContext<RoleServer>) -> Result<CallToolResponse, rmcp::ErrorData> {
        let args = Value::Object(request.arguments.unwrap_or_default());
        let this = self.clone();
        let name = request.name.to_string();
        // The tools wait on the game (seconds to minutes): off the async runtime.
        let outcome = tokio::task::spawn_blocking(move || this.call(&name, &args)).await.map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        let result = match outcome {
            Ok(text) => json!({"content": [{"type": "text", "text": text}]}),
            // Scene not running, bad arguments: a tool error, not a protocol error.
            Err(e) => json!({"content": [{"type": "text", "text": format!("error: {e:#}")}], "isError": true}),
        };
        serde_json::from_value::<CallToolResult>(result).map(Into::into).map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))
    }
    fn get_info(&self) -> ServerConfig {
        ServerConfig::new(ServerCapabilities::builder().enable_tools().build())
            .with_server_info(Implementation::new("openkoikatsu-interaction-pair", "1.0.0"))
            .with_instructions("Start the scene with Tools/pair_ctl.lua start first. pair_measure/pair_sweep restart the audited pose for repeatable windows.")
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let mut args = Args::parse();
    // A relative log path is relative to the project (the folder above Tools/ holding OpenKoikatsu.uproject), so the
    // server works whatever directory the MCP client starts it in.
    if args.log.is_relative() {
        let exe = std::env::current_exe()?;
        if let Some(root) = exe.ancestors().find(|d| d.join("OpenKoikatsu.uproject").is_file()) { args.log = root.join(&args.log); }
    }
    if let Some(Command::Compare { seconds, rounds, steady }) = args.command.clone() {
        let pair = Pair(args);
        return tokio::task::spawn_blocking(move || compare(&pair, seconds, rounds, steady)).await?;
    }
    let service = Pair(args).serve(rmcp::transport::stdio()).await?;
    service.waiting().await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn stats_lines_parse_like_the_python_tool() {
        let text = "solver=PhysX seconds=10.0 pose=後背位 · S_Loop\nsolveMsPerSecond=12.5 (prepare 1.0 simulate 2.0 collide 3.0 finish 4.0)\nGT 6.25 ms\nmaxPenetration=0.026 violations=264 maxProjection=0.105 resets=0 steps=52944 byBody=16:3 softTissue=0.008/7 pops=1 jitter=2 maxJerk=0.300 swing=0.0100 hard=3 (a) true=0.010/53 stretch=1.02 maxSwing=0.200@1/2\n";
        let m = parse_stats(text);
        assert_eq!(m["solver"], "PhysX");
        assert_eq!(m["violations"], 264.0);
        assert_eq!(m["trueViolations"], 53);
        assert_eq!(m["softTissueViolations"], 7);
        assert_eq!(m["hardFixes"], 3);
        assert_eq!(m["gameThreadMs"], 6.25);
        assert_eq!(m["trueViolationsPer1k"], 1.0);
        assert_eq!(encode("後背位 a;b"), "%E5%BE%8C%E8%83%8C%E4%BD%8D%20a%3Bb");
    }
}
