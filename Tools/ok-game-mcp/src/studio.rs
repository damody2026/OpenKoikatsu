//! OK Studio (the scene editor, SceneEditor.cmd / Tools/run_scene_editor.lua): its control server is HTTP on 127.0.0.1,
//! port OK_STUDIO_PORT (default 47890), open when the editor was started with -Control -StudioControlPort=<port>.
//! Every op is POST /studio/<op> with a JSON body; the reply is {"ok": true, ...} or {"ok": false, "error": "..."}
//! (busy: {"busy": "<reason>"} while the user drags, a scene loads or the timeline plays/exports). Each studio_* tool
//! passes its arguments through as the body. Docs/OK_STUDIO_MCP.md.
use crate::game::http;
use anyhow::{Context, Result, bail};
use serde_json::{Value, json};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

pub struct Studio {
    pub root: PathBuf,
    pub port: u16,
}

impl Studio {
    /// The same checkout as the street game; the port is OK_STUDIO_PORT, else 47890.
    pub fn locate(root: &Path) -> Studio {
        let port = std::env::var("OK_STUDIO_PORT").ok().and_then(|p| p.parse().ok()).unwrap_or(47890);
        Studio { root: root.to_path_buf(), port }
    }

    /// POST /studio/<op>; the reply as it came (ok true or false). Loading, saving and capturing can take a while.
    pub fn call(&self, op: &str, body: &Value) -> Result<Value> {
        http(self.port, &format!("/studio/{op}"), Some(body), Duration::from_secs(180), || {
            format!("OK Studio is not running on 127.0.0.1:{} (start it with studio_launch, or SceneEditor.cmd with -- -Control -StudioControlPort={})", self.port, self.port)
        })
    }

    pub fn up(&self) -> bool {
        self.call("state", &json!({})).is_ok_and(|r| r["ok"] == true)
    }

    fn log_file(&self) -> PathBuf {
        self.root.join("Saved/Studio/ok-game-mcp-studio-launch.log")
    }

    fn log_tail(&self) -> String {
        let text = std::fs::read_to_string(self.log_file()).unwrap_or_default();
        let lines: Vec<&str> = text.lines().collect();
        lines[lines.len().saturating_sub(20)..].join("\n")
    }

    /// Tools/lua/lua.exe Tools/run_scene_editor.lua [scene] [-WindowWidth N -WindowHeight N] [-NoBuild] -- -Control
    /// -StudioControlPort=<port> [unreal args], detached, from the checkout; waits until state answers.
    pub fn launch(&self, a: &Value) -> Result<Value> {
        if self.up() {
            bail!("OK Studio already answers on port {}; quit it first (studio_stop)", self.port);
        }
        let logs = self.root.join("Saved/Studio");
        std::fs::create_dir_all(&logs)?;
        let log = std::fs::File::create(self.log_file())?;
        let mut cmd = Command::new(self.root.join("Tools/lua/lua.exe"));
        cmd.arg("Tools/run_scene_editor.lua");
        if let Some(scene) = a["scene"].as_str().filter(|s| !s.is_empty()) {
            cmd.arg(scene);
        }
        if let Some(w) = a["width"].as_u64() {
            cmd.args(["-WindowWidth", &w.to_string()]);
        }
        if let Some(h) = a["height"].as_u64() {
            cmd.args(["-WindowHeight", &h.to_string()]);
        }
        if a["no_build"] == true {
            cmd.arg("-NoBuild");
        }
        cmd.args(["--", "-Control", &format!("-StudioControlPort={}", self.port)]);
        cmd.args(a["options"].as_array().into_iter().flatten().filter_map(|x| x.as_str()));
        cmd.env("OK_STUDIO_PORT", self.port.to_string()).current_dir(&self.root);
        cmd.stdout(Stdio::from(log.try_clone()?)).stderr(Stdio::from(log)).stdin(Stdio::null());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            const DETACHED_PROCESS: u32 = 0x0000_0008;
            const CREATE_NEW_PROCESS_GROUP: u32 = 0x0000_0200;
            cmd.creation_flags(DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP);
        }
        let mut child = cmd.spawn().context("could not start Tools/run_scene_editor.lua")?;
        let pid = child.id();
        let timeout = Duration::from_secs_f64(a["timeout_s"].as_f64().unwrap_or(120.0));
        let start = Instant::now();
        while start.elapsed() < timeout {
            if let Some(status) = child.try_wait()? {
                if !status.success() && !self.up() {
                    bail!("the launcher stopped ({status}); see {}:\n{}", self.log_file().display(), self.log_tail());
                }
            }
            if self.up() {
                return Ok(json!({"ok": true, "pid": pid, "port": self.port, "seconds": start.elapsed().as_secs()}));
            }
            std::thread::sleep(Duration::from_secs(2));
        }
        bail!(
            "OK Studio did not answer on port {} in {}s (launcher pid {pid} keeps running; studio_state later, or see {}):\n{}",
            self.port,
            timeout.as_secs(),
            self.log_file().display(),
            self.log_tail()
        )
    }
}

fn vec3(description: &str) -> Value {
    json!({"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": description})
}

fn id(description: &str) -> Value {
    json!({"type": ["integer", "string"], "description": description})
}

fn ids(description: &str) -> Value {
    json!({"type": "array", "items": {"type": ["integer", "string"]}, "description": description})
}

/// The studio tools: name, description, properties, required.
pub fn tools() -> Vec<Value> {
    let t = |name: &str, description: &str, properties: Value, required: &[&str]| {
        json!({"name": name, "description": description, "inputSchema": {"type": "object", "properties": properties, "required": required}})
    };
    let loc = || vec3("[x, y, z] cm");
    let rot = || vec3("[pitch, yaw, roll] degrees");
    let scl = || vec3("[x, y, z]");
    vec![
        t("studio_launch", "Start OK Studio (Tools/run_scene_editor.lua, the SceneEditor.cmd launcher) with its control server on OK_STUDIO_PORT (default 47890), detached, and wait until studio_state answers (default 120 s; the launcher rebuilds the editor first when the engine changed). Returns the launcher pid.",
          json!({"scene": {"type": "string", "description": "an .okscene, OK scene card PNG or KStudio scene PNG to open (path relative to the checkout or absolute); none: a new scene"},
                 "width": {"type": "integer"}, "height": {"type": "integer"},
                 "no_build": {"type": "boolean", "description": "skip the editor rebuild check (-NoBuild)"},
                 "options": {"type": "array", "items": {"type": "string"}, "description": "more Unreal arguments after --"},
                 "timeout_s": {"type": "number"}}), &[]),
        t("studio_stop", "Quit OK Studio (the command app.quit; it may ask about unsaved changes in its own UI).", json!({}), &[]),
        t("studio_state", "The scene: scene_file, dirty, selection ids, primary, objects [{id, parent, name, kind, visible, attach_bone, location, rotation, scale, error?}], ui (ui.commands: the command ids for studio_cmd), busy, timeline {time, duration, playing}.",
          json!({}), &[]),
        t("studio_inspect", "Everything about one object.", json!({"id": id("object id from studio_state")}), &["id"]),
        t("studio_catalog_search", "Search what can be added or applied: items, effects, maps, animations, poses, cards. query: space-separated terms that must all match; -word excludes. Returns [{kind, key, name, path?}]; key is what studio_spawn item/effect and studio_pose anim take, path what card / pose_file take.",
          json!({"query": {"type": "string"}, "kinds": {"type": "array", "items": {"type": "string", "enum": ["item", "effect", "map", "anim", "pose", "card"]}}, "limit": {"type": "integer"}}), &["query"]),
        t("studio_spawn", "Add an object. character: card (path); item: item \"group/category/no\"; effect: effect key; light: light_type; camera, folder: nothing else. parent: object id to attach to (bone: a bone of a character parent). Returns {id}.",
          json!({"kind": {"type": "string", "enum": ["character", "item", "light", "camera", "effect", "folder"]},
                 "card": {"type": "string"}, "item": {"type": "string", "description": "group/category/no"}, "effect": {"type": "string"},
                 "light_type": {"type": "string", "enum": ["directional", "point", "spot"]},
                 "parent": id("parent object id"), "bone": {"type": "string"}, "name": {"type": "string"},
                 "location": loc(), "rotation": rot(), "scale": scl()}), &["kind"]),
        t("studio_transform", "Move, turn or scale an object (relative=true: add to the current values).",
          json!({"id": id("object id"), "location": loc(), "rotation": rot(), "scale": scl(), "relative": {"type": "boolean"}}), &["id"]),
        t("studio_reparent", "Attach an object to another (parent -1: the scene root); bone: a bone of a character parent.",
          json!({"id": id("object id"), "parent": id("new parent id, -1 for the root"), "bone": {"type": "string"}}), &["id", "parent"]),
        t("studio_delete", "Delete objects (undoable).", json!({"ids": ids("object ids")}), &["ids"]),
        t("studio_select", "Select objects (the first is primary; [] clears).", json!({"ids": ids("object ids")}), &["ids"]),
        t("studio_pose", "Pose a character: FK bone rotations, IK effector positions, a pose file, an animation, expression, hands, look direction, mirror.",
          json!({"id": id("character id"),
                 "fk": {"type": "array", "items": {"type": "object", "properties": {"bone": {"type": "string"}, "euler": vec3("[x, y, z] degrees")}, "required": ["bone", "euler"]}},
                 "ik": {"type": "array", "items": {"type": "object", "properties": {"effector": {"type": "string"}, "position": vec3("[x, y, z] cm")}, "required": ["effector", "position"]}},
                 "clear_fk": {"type": "boolean"}, "clear_ik": {"type": "boolean"},
                 "pose_file": {"type": "string"}, "anim": {"type": "string", "description": "kind/group/category/no (studio_catalog_search kinds [\"anim\"])"},
                 "expression": {"type": "object", "properties": {"eyebrow": {"type": "integer"}, "eyes": {"type": "integer"}, "mouth": {"type": "integer"}, "mouth_open": {"type": "number"}}},
                 "hands": {"type": "object", "properties": {"left": {"type": "integer"}, "right": {"type": "integer"}}},
                 "look": {"type": "object", "properties": {"eyes": {"type": ["integer", "string"]}, "neck": {"type": ["integer", "string"]}}},
                 "mirror": {"type": "string", "enum": ["all", "left_to_right", "right_to_left"]}}), &["id"]),
        t("studio_camera", "The view camera: orbit focus/yaw/pitch/distance, a preset, frame the selection, look through a camera object, save/load bookmarks 0..9. Returns {focus, yaw, pitch, distance}.",
          json!({"focus": vec3("[x, y, z] cm"), "yaw": {"type": "number"}, "pitch": {"type": "number"}, "distance": {"type": "number"},
                 "preset": {"type": "string", "enum": ["front", "side", "top", "reset"]}, "focus_selection": {"type": "boolean"},
                 "use_camera": id("camera object id"), "bookmark_save": {"type": "integer", "minimum": 0, "maximum": 9}, "bookmark_load": {"type": "integer", "minimum": 0, "maximum": 9}}), &[]),
        t("studio_capture", "Render the view to an image file (never overwrites; path optional) and show it. transparent: PNG with alpha.",
          json!({"width": {"type": "integer"}, "height": {"type": "integer"}, "transparent": {"type": "boolean"}, "path": {"type": "string"}}), &[]),
        t("studio_save", "Save the scene under a new name (never overwrites). Returns {file}.", json!({"name": {"type": "string"}}), &["name"]),
        t("studio_load", "Open a scene (.okscene, OK scene card or KStudio scene PNG); merge=true adds it to the current one. Returns {report}.",
          json!({"path": {"type": "string"}, "merge": {"type": "boolean"}}), &["path"]),
        t("studio_undo", "Undo the last edit.", json!({}), &[]),
        t("studio_redo", "Redo.", json!({}), &[]),
        t("studio_cmd", "Run a command of the studio's command table (ids in studio_state ui.commands), with an optional argument.",
          json!({"id": {"type": "string"}, "arg": {"description": "the command's argument, any JSON"}}), &["id"]),
        t("studio_timeline", "The timeline: seek (time s), play/stop, add keys for objects. Returns {time, duration, playing}.",
          json!({"time": {"type": "number"}, "play": {"type": "boolean"}, "key": {"type": "object", "properties": {"ids": ids("object ids")}, "required": ["ids"]}}), &[]),
        t("studio_patch", "Change any setting of one object by deep-merging `fields` into its JSON (the same JSON studio_inspect returns under `object`: objects merge key by key, arrays and plain values replace). For everything studio_transform / studio_pose do not cover (colours, light intensity, item patterns, character clothes state, visibility...); studio_inspect first to see the keys. id, kind and parent cannot change (use studio_reparent). Undoable, in the same undo history as the person's edits in the SceneEditor. Returns {object} after the change.",
          json!({"id": id("object id"), "fields": {"type": "object", "description": "partial object JSON to merge, e.g. {\"visible\": false}"}}), &["id", "fields"]),
        t("studio_scene", "The scene's look and global settings, for the first version's mood: post (bloom, depth of field, fog, sky, filter, background..., deep-merged like studio_patch; the keys are what this tool returns under post), the map (-1 none) and its transform, the timeline duration and the active camera (-1 free view). With no arguments it only reads. Returns {post, map, map_transform, duration, active_camera, bookmarks}.",
          json!({"post": {"type": "object", "description": "partial post settings to merge (keys as returned under post)"},
                 "map": {"type": "integer", "description": "map number (studio_catalog kind map), -1 none"},
                 "map_transform": {"type": "object", "properties": {"location": loc(), "rotation": rot(), "scale": scl()}},
                 "duration": {"type": "number", "description": "timeline length in seconds"},
                 "active_camera": id("camera object id to render through, -1 the free view")}), &[]),
        t("studio_bones", "A character's posing handles, read before studio_pose: FK bones [{bone, name, part, euler, set, world}] (euler: current rotation in degrees, set: holds an FK value, world: position cm), IK effectors [{effector, name, enabled, position, world}], look_point, and accessory_points [{key, name, group, world}] (bones to attach items to with studio_spawn / studio_reparent bone).",
          json!({"id": id("character id")}), &["id"]),
        t("studio_bounds", "World-space bounding boxes in cm, to place objects on the floor, on top of or next to each other (e.g. move an item so its min z meets a table's max z). No ids: every object. Returns {objects [{id, min, max, center, size}]}.",
          json!({"ids": ids("object ids (default: all)")}), &[]),
        t("studio_catalog", "Browse a catalogue by group and category (studio_catalog_search stays the text search). kind alone lists its groups; with group, its categories; with group + category, the entries (keys for studio_spawn item/effect, studio_scene map, post filter / sky, studio_pose anim, accessory point bones). anim_kind: which animation table for kind anim. Returns {groups?, categories?, entries [...]}.",
          json!({"kind": {"type": "string", "enum": ["item", "map", "effect", "anim", "filter", "sky", "accessory_point"]},
                 "group": {"type": "integer"}, "category": {"type": "integer"}, "anim_kind": {"type": "integer", "description": "kind anim: the animation table"}}), &["kind"]),
        t("studio_keys", "The timeline keys of one object (id -2: the scene's own track): list, set a key at time (the object's current state), remove the key at time, move it to `to`, set its ease, clear all, or smooth (smooth=true: curves through the keys). Returns {keys [{time, ease, ...}], duration}.",
          json!({"id": id("object id, -2 for the scene track"),
                 "action": {"type": "string", "enum": ["list", "set", "remove", "move", "ease", "clear", "smooth"]},
                 "time": {"type": "number", "description": "seconds: the key to set / remove / move / ease"},
                 "to": {"type": "number", "description": "move: the new time in seconds"},
                 "ease": {"type": "string", "enum": ["linear", "ease_in_out", "ease_in", "ease_out", "step"]},
                 "smooth": {"type": "boolean"}}), &["id", "action"]),
        t("studio_duplicate", "Copy objects (undoable), e.g. a row of chairs or a second character with the same card. Returns {ids} of the new copies.",
          json!({"ids": ids("object ids")}), &["ids"]),
        t("studio_export", "Export the timeline: png (an image sequence into a folder), mp4 (a movie), okanim (one character's animation; id). Runs in the background: progress in studio_state `export` {running, frame, frames, last_movie, error}; edits answer busy meanwhile. Returns {folder} or {file}.",
          json!({"kind": {"type": "string", "enum": ["png", "mp4", "okanim"]},
                 "fps": {"type": "number"}, "width": {"type": "integer"}, "height": {"type": "integer"},
                 "from": {"type": "number", "description": "start time s"}, "to": {"type": "number", "description": "end time s"},
                 "folder": {"type": "string", "description": "output folder"},
                 "id": id("okanim: the character")}), &["kind"]),
        t("studio_scenes", "List scene files to open with studio_load (default: the studio's scene folder). Returns {folder, files [{path, name, kind, modified}]}.",
          json!({"folder": {"type": "string"}}), &[]),
        t("studio_report", "What the last open could not show and which resources the scene lacks (cards, mods, items). Returns {load [lines], missing [lines]}.",
          json!({}), &[]),
        t("studio_ui_shot", "Screenshot the whole studio window WITH its UI panels (studio_capture renders only the view), to see what the person sees in the SceneEditor. Written a frame or two later; waits up to 15 s for the file and shows it. Returns {file}.",
          json!({"name": {"type": "string", "description": "file name (never overwrites)"}}), &[]),
    ]
}

/// Loose check of the arguments against the tool's schema: required ones present, the given ones of the right JSON
/// type (top level only; the studio checks the rest).
fn check(name: &str, a: &Value) -> Result<()> {
    let Some(tool) = tools().into_iter().find(|t| t["name"] == name) else { bail!("unknown tool {name}") };
    let schema = &tool["inputSchema"];
    for r in schema["required"].as_array().into_iter().flatten().filter_map(Value::as_str) {
        if a[r].is_null() {
            bail!("{name}: {r} is required");
        }
    }
    let fits = |ty: &str, v: &Value| match ty {
        "string" => v.is_string(),
        "boolean" => v.is_boolean(),
        "number" => v.is_number(),
        "integer" => v.is_i64() || v.is_u64() || v.as_f64().is_some_and(|f| f.fract() == 0.0),
        "array" => v.is_array(),
        "object" => v.is_object(),
        _ => true,
    };
    for (key, v) in a.as_object().into_iter().flatten() {
        let types: Vec<&str> = match &schema["properties"][key]["type"] {
            Value::String(t) => vec![t.as_str()],
            Value::Array(list) => list.iter().filter_map(Value::as_str).collect(),
            _ => continue,
        };
        if !v.is_null() && !types.iter().any(|t| fits(t, v)) {
            bail!("{name}: {key} must be {}", types.join(" or "));
        }
    }
    Ok(())
}

fn reply(op: &str, r: Value) -> Value {
    let text = serde_json::to_string_pretty(&r).unwrap_or_default();
    if r["ok"] == true {
        return json!({"content": [{"type": "text", "text": text}], "isError": false});
    }
    let head = match r["busy"].as_str() {
        Some(why) => format!("studio {op}: busy ({why}); try again when the user has finished / the load or playback is over"),
        None => format!("studio {op}: {}", r["error"].as_str().unwrap_or("refused")),
    };
    json!({"content": [{"type": "text", "text": format!("{head}\n{text}")}], "isError": true})
}

pub fn run(studio: &Studio, name: &str, a: &Value) -> Result<Value> {
    check(name, a)?;
    let body = if a.is_object() { a.clone() } else { json!({}) };
    Ok(match name {
        "studio_launch" => {
            let r = studio.launch(a)?;
            json!({"content": [{"type": "text", "text": serde_json::to_string_pretty(&r).unwrap_or_default()}], "isError": false})
        }
        "studio_stop" => reply("cmd", studio.call("cmd", &json!({"id": "app.quit"}))?),
        // Both answer {file}; ui_shot writes it a frame or two later, image_part waits up to 15 s for it.
        "studio_capture" | "studio_ui_shot" => {
            let op = &name["studio_".len()..];
            let r = studio.call(op, &body)?;
            let mut out = reply(op, r.clone());
            if r["ok"] == true {
                if let Some(file) = r["file"].as_str() {
                    match crate::image_part(file) {
                        Ok(img) => out["content"].as_array_mut().expect("content").push(img),
                        Err(e) => out["content"].as_array_mut().expect("content").push(json!({"type": "text", "text": format!("(no preview: {e:#})")})),
                    }
                }
            }
            out
        }
        other => {
            let op = other.strip_prefix("studio_").unwrap_or(other);
            reply(op, studio.call(op, &body)?)
        }
    })
}
