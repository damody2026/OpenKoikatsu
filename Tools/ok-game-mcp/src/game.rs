//! The game side: the StreetSocial development control channel (HTTP on 127.0.0.1, only open when the game was started
//! with -StreetSocialControl; Source/OpenKoikatsu/OKStreetSocialControl.inl), the checkout it belongs to, and starting or
//! stopping that game.
use anyhow::{Context, Result, bail};
use serde_json::{Value, json};
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

pub struct Game {
    pub root: PathBuf,
    pub port: u16,
}

impl Game {
    /// The checkout is the folder holding OpenKoikatsu.uproject (OK_ROOT, else up from the exe, else up from here).
    /// The port is OK_CONTROL_PORT, else local-instance.json port_base + 10 (Tools/ok_instance.lua), else 47870.
    pub fn locate() -> Result<Game> {
        let root = match std::env::var_os("OK_ROOT") {
            Some(r) => PathBuf::from(r),
            None => {
                let starts = [std::env::current_exe().ok(), std::env::current_dir().ok()];
                starts
                    .iter()
                    .flatten()
                    .flat_map(|p| p.ancestors().map(Path::to_path_buf).collect::<Vec<_>>())
                    .find(|p| p.join("OpenKoikatsu.uproject").is_file())
                    .context("OpenKoikatsu.uproject not found above the exe or the working folder; set OK_ROOT")?
            }
        };
        let port = match std::env::var("OK_CONTROL_PORT").ok().and_then(|p| p.parse().ok()) {
            Some(p) => p,
            None => {
                let base = std::fs::read_to_string(root.join("local-instance.json"))
                    .ok()
                    .and_then(|t| serde_json::from_str::<Value>(t.trim_start_matches('\u{feff}')).ok())
                    .and_then(|v| v["port_base"].as_u64())
                    .unwrap_or(47860);
                (base + 10) as u16
            }
        };
        Ok(Game { root, port })
    }

    /// GET (body None) or POST JSON to the control channel; the reply is JSON.
    pub fn call(&self, path: &str, body: Option<&Value>) -> Result<Value> {
        let mut stream = TcpStream::connect(("127.0.0.1", self.port))
            .with_context(|| format!("the game's control channel is not up on 127.0.0.1:{} (start it with game_launch)", self.port))?;
        stream.set_read_timeout(Some(Duration::from_secs(60)))?;
        let payload = body.map(|b| b.to_string()).unwrap_or_default();
        let head = if body.is_some() {
            format!(
                "POST {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                payload.len()
            )
        } else {
            format!("GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
        };
        stream.write_all(head.as_bytes())?;
        stream.write_all(payload.as_bytes())?;
        let mut raw = Vec::new();
        stream.read_to_end(&mut raw)?;
        let split = raw.windows(4).position(|w| w == b"\r\n\r\n").context("malformed HTTP reply")?;
        let headers = String::from_utf8_lossy(&raw[..split]).to_lowercase();
        let status: u16 = headers.split_whitespace().nth(1).and_then(|s| s.parse().ok()).unwrap_or(0);
        let mut data = raw[split + 4..].to_vec();
        if headers.contains("transfer-encoding: chunked") {
            data = unchunk(&data)?;
        }
        let text = String::from_utf8_lossy(&data);
        if !(200..300).contains(&status) {
            bail!("{path}: HTTP {status}: {}", text.chars().take(400).collect::<String>());
        }
        serde_json::from_str(&text).with_context(|| format!("{path}: reply is not JSON"))
    }

    pub fn up(&self) -> bool {
        self.call("/health", None).is_ok()
    }

    /// Starts the game with the control channel (Tools/run_street_social_development.lua -Control), windowed, and waits
    /// until the channel answers. extra: more launcher options ("-Trace", "-Exec", "stat unit", "-Sandbox", "x", ...).
    pub fn launch(&self, window: &str, extra: &[String], timeout: Duration) -> Result<Value> {
        if self.up() {
            bail!("a game already answers on port {}; game_stop it first", self.port);
        }
        let lua = self.root.join("Tools/lua/lua.exe");
        let script = self.root.join("Tools/run_street_social_development.lua");
        let logs = self.root.join("Saved/StreetSocial");
        std::fs::create_dir_all(&logs)?;
        let log = std::fs::File::create(logs.join("ok-game-mcp-launch.log"))?;
        let mut cmd = Command::new(lua);
        cmd.arg(script).args(["-Control", "-Window", window]).args(extra).current_dir(&self.root);
        cmd.stdout(Stdio::from(log.try_clone()?)).stderr(Stdio::from(log)).stdin(Stdio::null());
        let mut child = cmd.spawn().context("could not start the launcher")?;
        let start = Instant::now();
        while start.elapsed() < timeout {
            if let Some(status) = child.try_wait()? {
                if !status.success() && !self.up() {
                    bail!("the launcher stopped ({status}); see Saved/StreetSocial/ok-game-mcp-launch.log:\n{}", self.launch_log_tail());
                }
            }
            if self.up() {
                return Ok(json!({"ok": true, "port": self.port, "seconds": start.elapsed().as_secs()}));
            }
            std::thread::sleep(Duration::from_secs(2));
        }
        bail!("the control channel did not come up in {}s; launcher log:\n{}", timeout.as_secs(), self.launch_log_tail())
    }

    fn launch_log_tail(&self) -> String {
        let text = std::fs::read_to_string(self.root.join("Saved/StreetSocial/ok-game-mcp-launch.log")).unwrap_or_default();
        let lines: Vec<&str> = text.lines().collect();
        lines[lines.len().saturating_sub(20)..].join("\n")
    }

    /// Stops only the game listening on this checkout's control port (another session's sandbox game keeps running).
    pub fn stop(&self) -> Result<Value> {
        let out = Command::new("netstat").args(["-ano", "-p", "TCP"]).output()?;
        let text = String::from_utf8_lossy(&out.stdout);
        let local = format!("127.0.0.1:{}", self.port);
        let any = format!("0.0.0.0:{}", self.port);
        let pid = text
            .lines()
            .map(|l| l.split_whitespace().collect::<Vec<_>>())
            .find(|f| f.len() >= 5 && (f[1] == local || f[1] == any) && f[3].eq_ignore_ascii_case("LISTENING"))
            .map(|f| f[4].to_string());
        let Some(pid) = pid else { return Ok(json!({"ok": true, "stopped": null, "note": "no game on that port"})) };
        let status = Command::new("taskkill").args(["/PID", &pid]).output()?;
        Ok(json!({"ok": status.status.success(), "stopped": pid}))
    }

    /// The newest game log (Saved/StreetSocial/Play-*/game.log or a sandbox's), its last lines, optionally only those
    /// containing a text.
    pub fn log(&self, lines: usize, contains: Option<&str>) -> Result<Value> {
        let dir = self.root.join("Saved/StreetSocial");
        let newest = std::fs::read_dir(&dir)?
            .flatten()
            .filter(|e| e.file_name().to_string_lossy().starts_with("Play-"))
            .map(|e| e.path().join("game.log"))
            .filter(|p| p.is_file())
            .max_by_key(|p| p.metadata().and_then(|m| m.modified()).ok())
            .context("no Saved/StreetSocial/Play-*/game.log yet")?;
        let bytes = std::fs::read(&newest)?;
        // The tail only: logs grow to hundreds of MB.
        let tail = &bytes[bytes.len().saturating_sub(8 << 20)..];
        let text = String::from_utf8_lossy(tail);
        let picked: Vec<&str> = text.lines().filter(|l| contains.is_none_or(|c| l.contains(c))).collect();
        let shown = picked[picked.len().saturating_sub(lines)..].join("\n");
        Ok(json!({"file": newest.to_string_lossy(), "lines": shown}))
    }
}

fn unchunk(data: &[u8]) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    let mut at = 0;
    loop {
        let end = data[at..].windows(2).position(|w| w == b"\r\n").context("bad chunk")? + at;
        let size = usize::from_str_radix(String::from_utf8_lossy(&data[at..end]).split(';').next().unwrap_or("0").trim(), 16)?;
        if size == 0 {
            return Ok(out);
        }
        out.extend_from_slice(&data[end + 2..end + 2 + size]);
        at = end + 2 + size + 2;
    }
}
