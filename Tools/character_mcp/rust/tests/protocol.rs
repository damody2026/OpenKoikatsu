use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader, Write},
    process::{Child, Command, Stdio},
    sync::mpsc,
    time::Duration,
};
struct Process(Child);
impl Drop for Process {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}
#[test]
fn official_sdk_stdio_handshake_tools_validation_and_cancel() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let mut process = Process(
        Command::new(env!("CARGO_BIN_EXE_ok-character-mcp"))
            .args(["--mailbox", temp.path().to_str().unwrap(), "--timeout", "2"])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()?,
    );
    let mut input = process.0.stdin.take().unwrap();
    let output = process.0.stdout.take().unwrap();
    let (tx, rx) = mpsc::channel();
    let reader = std::thread::spawn(move || {
        for line in BufReader::new(output).lines() {
            if tx.send(line).is_err() {
                break;
            }
        }
    });
    let send = |input: &mut std::process::ChildStdin, value: Value| -> Result<()> {
        writeln!(input, "{value}")?;
        input.flush()?;
        Ok(())
    };
    let receive = |id: i64| -> Result<Value> {
        loop {
            let line = rx
                .recv_timeout(Duration::from_secs(10))
                .context("MCP response timeout")??;
            let value: Value = serde_json::from_str(&line).context("Non-JSON noise in stdout")?;
            if value["id"].as_i64() == Some(id) {
                return Ok(value);
            }
        }
    };
    send(
        &mut input,
        json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"Rust acceptance","version":"1"}}}),
    )?;
    let init = receive(1)?;
    assert_eq!(init["result"]["serverInfo"]["version"], "2.0.0");
    assert_eq!(init["result"]["protocolVersion"], "2025-11-25");
    send(
        &mut input,
        json!({"jsonrpc":"2.0","method":"notifications/initialized"}),
    )?;
    send(
        &mut input,
        json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
    )?;
    assert_eq!(receive(2)?["result"]["tools"].as_array().unwrap().len(), 31);
    send(
        &mut input,
        json!({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"character_state","arguments":{"extra":true}}}),
    )?;
    assert_eq!(receive(3)?["result"]["isError"], true);
    send(
        &mut input,
        json!({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"character_state","arguments":{}}}),
    )?;
    send(
        &mut input,
        json!({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":4,"reason":"acceptance test"}}),
    )?;
    send(
        &mut input,
        json!({"jsonrpc":"2.0","id":5,"method":"ping","params":{}}),
    )?;
    assert!(receive(5)?["result"].is_object());
    drop(input);
    let deadline = std::time::Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(status) = process.0.try_wait()? {
            ensure!(status.success(), "Server failed on EOF: {status}");
            break;
        }
        ensure!(
            std::time::Instant::now() < deadline,
            "Server did not exit after stdin EOF"
        );
        std::thread::sleep(Duration::from_millis(10));
    }
    reader.join().unwrap();
    Ok(())
}
