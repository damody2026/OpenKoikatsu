//! ok-story-mcp: an MCP stdio server (newline-delimited JSON-RPC 2.0) for writing OK Studio galgame stories with an AI.
//! Story tools edit the .okstory.json in place (src/edit.rs: validated before every write). Game tools reach a running
//! story player through its control folder (UserData/Story/control: one request file in, one answer file out; see
//! UOKStoryPlayer::PollControl).
//!
//!   ok-story-mcp [--project <OpenKoikatsu root>]
use ok_story::edit;
use serde_json::{json, Map, Value};
use std::io::{BufRead, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

struct Server {
    project: PathBuf,
}

fn tool(name: &str, description: &str, properties: Value, required: &[&str]) -> Value {
    json!({"name": name, "description": description, "inputSchema": {"type": "object", "properties": properties, "required": required}})
}

fn tools() -> Vec<Value> {
    let file = json!({"type": "string", "description": "Story file (.okstory.json): absolute, or relative to the project"});
    vec![
        tool("story_new", "Creates a story file with one act on an OK Studio scene.", json!({"file": file, "title": {"type": "string"}, "scene": {"type": "string", "description": ".okscene path"}}), &["file", "title", "scene"]),
        tool("story_outline", "Acts, lines (id, speaker, first words, choices) and characters of a story.", json!({"file": file}), &["file"]),
        tool("story_validate", "Errors of a story file, each with the path of what is wrong (empty: valid).", json!({"file": file}), &["file"]),
        tool("story_get_line", "One line as JSON.", json!({"file": file, "line": {"type": "string"}}), &["file", "line"]),
        tool("story_set_line", "Changes fields of a line (who, text, face, motion, camera, cast, look, choices, voice, se, give, take, set, when, goto, name); null removes a field. Not written if the story would become invalid.",
            json!({"file": file, "line": {"type": "string"}, "fields": {"type": "object"}}), &["file", "line", "fields"]),
        tool("story_add_line", "Inserts a line after a line id (or first in an act id). The line needs a new unique id.",
            json!({"file": file, "after": {"type": "string"}, "line": {"type": "object"}}), &["file", "after", "line"]),
        tool("story_delete_line", "Removes a line.", json!({"file": file, "line": {"type": "string"}}), &["file", "line"]),
        tool("story_add_act", "Adds an act (id, title, scene, camera, cast, lines, next) after an act id, or last.",
            json!({"file": file, "act": {"type": "object"}, "after": {"type": "string"}}), &["file", "act"]),
        tool("story_set_act", "Changes fields of an act (title, scene, camera, cast, next); null removes a field.",
            json!({"file": file, "act": {"type": "string"}, "fields": {"type": "object"}}), &["file", "act", "fields"]),
        tool("story_set_character", "Adds or replaces a character (card, scene_object, name, color, personality, voice, portrait, offstage); null removes it.",
            json!({"file": file, "id": {"type": "string"}, "character": {}}), &["file", "id", "character"]),
        tool("game_state", "State of the story playing in the running OK Studio (act, line, typing, camera, voice, items, flags).", json!({}), &[]),
        tool("game_jump", "Makes the running story play from a line or act id (its effects are not applied).", json!({"line": {"type": "string"}}), &["line"]),
        tool("game_input", "Sends one input event to the running story ({\"type\":\"click\"}, {\"type\":\"choose\",\"index\":0}, {\"type\":\"button\",\"id\":\"auto\"}...).", json!({"event": {"type": "object"}}), &["event"]),
        tool("game_reload", "Re-reads the story file in the running game (after editing it), keeping the place.", json!({}), &[]),
        tool("game_screenshot", "Screenshot of the running story (with its UI); returns the PNG path.", json!({}), &[]),
    ]
}

impl Server {
    fn story_path(&self, a: &Value) -> Result<PathBuf, String> {
        let f = a["file"].as_str().ok_or("file is required")?;
        let p = Path::new(f);
        Ok(if p.is_absolute() { p.to_path_buf() } else { self.project.join(p) })
    }

    fn edit(&self, a: &Value, f: impl FnOnce(&mut Value) -> Result<(), String>) -> Result<Value, String> {
        let path = self.story_path(a)?;
        let mut story = edit::load(&path)?;
        f(&mut story)?;
        edit::save(&path, &story)?;
        Ok(json!({"saved": path.to_string_lossy(), "outline": edit::outline(&story)}))
    }

    fn fields(a: &Value, key: &str) -> Result<Map<String, Value>, String> {
        a[key].as_object().cloned().ok_or_else(|| format!("{key} must be an object"))
    }

    /// One request to the running game through its control folder.
    fn game(&self, request: Value) -> Result<Value, String> {
        let dir = self.project.join("UserData/Story/control");
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let id = format!("{}-{}", std::process::id(), Instant::now().elapsed().as_nanos() ^ std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0));
        let tmp = dir.join(format!("{id}.tmp"));
        let file = dir.join(format!("{id}.json"));
        let answer = dir.join(format!("{id}.out.json"));
        std::fs::write(&tmp, request.to_string()).map_err(|e| e.to_string())?;
        std::fs::rename(&tmp, &file).map_err(|e| e.to_string())?;
        let start = Instant::now();
        while start.elapsed() < Duration::from_secs(15) {
            if let Ok(text) = std::fs::read_to_string(&answer) {
                let _ = std::fs::remove_file(&answer);
                return serde_json::from_str(&text).map_err(|e| e.to_string());
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        let _ = std::fs::remove_file(&file);
        Err("no story is playing in OK Studio (start one with SceneEditor -Story <file>, or OKStory.Play)".into())
    }

    fn call(&self, name: &str, a: &Value) -> Result<Value, String> {
        match name {
            "story_new" => {
                let path = self.story_path(a)?;
                if path.exists() {
                    return Err(format!("{} already exists", path.display()));
                }
                let story = edit::new_story(a["title"].as_str().unwrap_or("story"), a["scene"].as_str().ok_or("scene is required")?);
                if let Some(parent) = path.parent() {
                    std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
                }
                edit::save(&path, &story)?;
                Ok(json!({"saved": path.to_string_lossy(), "outline": edit::outline(&story)}))
            }
            "story_outline" => Ok(edit::outline(&edit::load(&self.story_path(a)?)?)),
            "story_validate" => Ok(json!({"errors": edit::validate(&edit::load(&self.story_path(a)?)?)})),
            "story_get_line" => edit::get_line(&edit::load(&self.story_path(a)?)?, a["line"].as_str().ok_or("line is required")?),
            "story_set_line" => {
                let fields = Self::fields(a, "fields")?;
                self.edit(a, |s| edit::set_line(s, a["line"].as_str().unwrap_or_default(), &fields))
            }
            "story_add_line" => self.edit(a, |s| edit::add_line(s, a["after"].as_str().unwrap_or_default(), a["line"].clone())),
            "story_delete_line" => self.edit(a, |s| edit::delete_line(s, a["line"].as_str().unwrap_or_default())),
            "story_add_act" => self.edit(a, |s| edit::add_act(s, a["after"].as_str(), a["act"].clone())),
            "story_set_act" => {
                let fields = Self::fields(a, "fields")?;
                self.edit(a, |s| edit::set_act(s, a["act"].as_str().unwrap_or_default(), &fields))
            }
            "story_set_character" => self.edit(a, |s| edit::set_character(s, a["id"].as_str().unwrap_or_default(), a["character"].clone())),
            "game_state" => self.game(json!({"cmd": "state"})),
            "game_jump" => self.game(json!({"cmd": "input", "event": {"type": "jump", "line": a["line"]}})),
            "game_input" => self.game(json!({"cmd": "input", "event": a["event"]})),
            "game_reload" => self.game(json!({"cmd": "reload"})),
            "game_screenshot" => self.game(json!({"cmd": "shot"})),
            _ => Err(format!("unknown tool {name}")),
        }
    }

    fn handle(&self, req: &Value) -> Option<Value> {
        let id = req.get("id").cloned()?;
        let method = req["method"].as_str().unwrap_or_default();
        let result = match method {
            "initialize" => json!({
                "protocolVersion": req["params"]["protocolVersion"].as_str().unwrap_or("2024-11-05"),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "ok-story-mcp", "version": env!("CARGO_PKG_VERSION")},
                "instructions": "Write OK Studio galgame stories (.okstory.json, Docs/GALGAME_STORY_USAGE.md). Edit with the story_* tools (each write is validated); preview in a running OK Studio with the game_* tools."
            }),
            "ping" => json!({}),
            "tools/list" => json!({"tools": tools()}),
            "tools/call" => {
                let name = req["params"]["name"].as_str().unwrap_or_default();
                let args = req["params"].get("arguments").cloned().unwrap_or(json!({}));
                match self.call(name, &args) {
                    Ok(v) => json!({"content": [{"type": "text", "text": serde_json::to_string_pretty(&v).unwrap_or_default()}]}),
                    Err(e) => json!({"content": [{"type": "text", "text": e}], "isError": true}),
                }
            }
            _ => return Some(json!({"jsonrpc": "2.0", "id": id, "error": {"code": -32601, "message": format!("unknown method {method}")}})),
        };
        Some(json!({"jsonrpc": "2.0", "id": id, "result": result}))
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let project = args
        .iter()
        .position(|a| a == "--project")
        .and_then(|i| args.get(i + 1))
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../.."));
    let server = Server { project };
    let stdin = std::io::stdin();
    let mut out = std::io::stdout();
    for line in stdin.lock().lines() {
        let Ok(line) = line else { break };
        // Some hosts (PowerShell pipes) start the stream with a byte order mark.
        let line = line.trim_start_matches('\u{feff}');
        if line.trim().is_empty() {
            continue;
        }
        let reply = match serde_json::from_str::<Value>(line) {
            Ok(req) => server.handle(&req),
            Err(e) => Some(json!({"jsonrpc": "2.0", "id": null, "error": {"code": -32700, "message": e.to_string()}})),
        };
        if let Some(r) = reply {
            let _ = writeln!(out, "{r}");
            let _ = out.flush();
        }
    }
}
