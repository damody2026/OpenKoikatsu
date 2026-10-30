//! The OK Studio tools: all listed with well-formed schemas (required keys among the properties), a call without a
//! running studio is a tool error that says to launch it, and missing arguments are refused before any request.
//! Uses a port nothing listens on.
use serde_json::{Value, json};
use std::io::{BufRead, BufReader, Write};
use std::process::{Command, Stdio};

const STUDIO_TOOLS: [&str; 30] = [
    "studio_launch", "studio_stop", "studio_state", "studio_inspect", "studio_catalog_search", "studio_spawn", "studio_transform",
    "studio_reparent", "studio_delete", "studio_select", "studio_pose", "studio_camera", "studio_capture", "studio_save", "studio_load",
    "studio_undo", "studio_redo", "studio_cmd", "studio_timeline", "studio_patch", "studio_scene", "studio_bones", "studio_bounds",
    "studio_catalog", "studio_keys", "studio_duplicate", "studio_export", "studio_scenes", "studio_report", "studio_ui_shot",
];

#[test]
fn studio_tools_listed_and_called_without_a_studio() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_ok-game-mcp"))
        .env("OK_CONTROL_PORT", "1")
        .env("OK_STUDIO_PORT", "1")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let mut stdin = child.stdin.take().unwrap();
    let mut out = BufReader::new(child.stdout.take().unwrap());
    let mut send = |v: Value| writeln!(stdin, "{v}").unwrap();
    let mut read = || -> Value {
        let mut line = String::new();
        out.read_line(&mut line).unwrap();
        serde_json::from_str(&line).unwrap()
    };
    send(json!({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "0"}}}));
    assert_eq!(read()["result"]["serverInfo"]["name"], "ok-game");
    send(json!({"jsonrpc": "2.0", "method": "notifications/initialized"}));
    send(json!({"jsonrpc": "2.0", "id": 2, "method": "tools/list"}));
    let listed = read()["result"]["tools"].as_array().unwrap().clone();
    for name in STUDIO_TOOLS {
        let tool = listed.iter().find(|t| t["name"] == name).unwrap_or_else(|| panic!("{name} missing"));
        assert!(!tool["description"].as_str().unwrap_or("").is_empty(), "{name}: no description");
        let schema = &tool["inputSchema"];
        assert_eq!(schema["type"], "object", "{name}: schema type");
        let props = schema["properties"].as_object().unwrap_or_else(|| panic!("{name}: no properties"));
        for r in schema["required"].as_array().map(Vec::as_slice).unwrap_or(&[]) {
            assert!(props.contains_key(r.as_str().unwrap()), "{name}: required {r} is not a property");
        }
        for (key, p) in props {
            assert!(p.is_object(), "{name}.{key}: property schema is not an object");
        }
    }
    assert_eq!(listed.iter().filter(|t| t["name"].as_str().is_some_and(|n| n.starts_with("studio_"))).count(), STUDIO_TOOLS.len());
    // The street tools are still there.
    assert!(listed.iter().any(|t| t["name"] == "game_state"));

    send(json!({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "studio_state", "arguments": {}}}));
    let r = read();
    assert_eq!(r["result"]["isError"], true);
    assert!(r["result"]["content"][0]["text"].as_str().unwrap().contains("OK Studio is not running"));

    send(json!({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "studio_transform", "arguments": {"location": [0, 0, 0]}}}));
    let r = read();
    assert_eq!(r["result"]["isError"], true);
    assert!(r["result"]["content"][0]["text"].as_str().unwrap().contains("id is required"));

    send(json!({"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "studio_delete", "arguments": {"ids": 3}}}));
    let r = read();
    assert_eq!(r["result"]["isError"], true);
    assert!(r["result"]["content"][0]["text"].as_str().unwrap().contains("ids must be array"));

    // The newer tools: required arguments and types are checked before any request; ui_shot goes to the studio.
    let refused = [
        (6, "studio_patch", json!({"id": 1}), "fields is required"),
        (7, "studio_patch", json!({"id": 1, "fields": [1]}), "fields must be object"),
        (8, "studio_keys", json!({"id": -2}), "action is required"),
        (9, "studio_catalog", json!({"group": 0}), "kind is required"),
        (10, "studio_export", json!({"kind": "mp4", "fps": "30"}), "fps must be number"),
        (11, "studio_bones", json!({}), "id is required"),
        (12, "studio_duplicate", json!({"ids": "1"}), "ids must be array"),
        (13, "studio_scene", json!({"map": 1.5}), "map must be integer"),
        (14, "studio_ui_shot", json!({}), "OK Studio is not running"),
        (15, "studio_report", json!({}), "OK Studio is not running"),
    ];
    for (n, name, args, says) in refused {
        send(json!({"jsonrpc": "2.0", "id": n, "method": "tools/call", "params": {"name": name, "arguments": args}}));
        let r = read();
        assert_eq!(r["result"]["isError"], true, "{name}");
        let text = r["result"]["content"][0]["text"].as_str().unwrap();
        assert!(text.contains(says), "{name}: {text}");
    }
    drop(stdin);
    child.kill().ok();
}
