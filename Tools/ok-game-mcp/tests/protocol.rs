//! The server speaks MCP over stdio: initialize, the tool list, and a tool call that reports a missing game as a tool
//! error (not a crash). Uses a port nothing listens on.
use serde_json::{Value, json};
use std::io::{BufRead, BufReader, Write};
use std::process::{Command, Stdio};

#[test]
fn initialize_list_and_call_without_a_game() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_ok-game-mcp"))
        .env("OK_CONTROL_PORT", "1")
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
    let tools = read()["result"]["tools"].as_array().unwrap().iter().map(|t| t["name"].as_str().unwrap().to_string()).collect::<Vec<_>>();
    for name in ["game_launch", "game_state", "game_talk", "game_key", "game_goto", "game_screenshot", "game_exec", "game_trace_start"] {
        assert!(tools.iter().any(|t| t == name), "{name} missing");
    }
    send(json!({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "game_state", "arguments": {}}}));
    let r = read();
    assert_eq!(r["result"]["isError"], true);
    assert!(r["result"]["content"][0]["text"].as_str().unwrap().contains("control channel is not up"));
    drop(stdin);
    child.kill().ok();
}
