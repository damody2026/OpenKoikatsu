# 街區遊戲 MCP（ok-game-mcp）

讓 AI 像玩家一樣操作 StreetSocial：走路、按鍵、按畫面上的按鈕、跟女主說話、等時間過去、截圖，另外能下引擎主控台指令與錄 Unreal Insights trace（效能分析）。

## 架構

```
AI（Claude Code）
  │  MCP（stdio）
  ▼
Tools/ok-game-mcp（Rust）
  │  HTTP，只連 127.0.0.1:<控制埠>
  ▼
遊戲內開發控制通道（只有帶 -StreetSocialControl 啟動才開，Source/OpenKoikatsu/OKStreetSocialControl.inl）
  │  指令在遊戲主執行緒執行，走玩家同一套程式
  ▼
街區場景
```

- 註冊在專案根目錄 `.mcp.json`（`ok-game`，`cargo run --release` 啟動，第一次會自動編譯）。新開的 Claude Code session 會載入它。
- 控制埠：`OK_CONTROL_PORT`，否則 `local-instance.json` 的 `port_base + 10`，預設 47870。沙盒遊戲（`-Sandbox`）用自己的埠，設 `OK_CONTROL_PORT` 指過去。
- 沒有 MCP 的環境（例如已經開著、沒重新載入設定的 session）可以用同一套工具的命令列：
  `Tools/ok-game-mcp/target/release/ok-game-mcp.exe call <工具> '<JSON 參數>'`，`--check` 看找到的專案、埠號與遊戲是否在線。
- 以前的 `Tools/street_play.lua`（curl 呼叫同一組端點）已移除，功能都在這裡。

## 工具

| 工具 | 做什麼 |
|---|---|
| `game_launch` | 以 `-Control -Window 1280x720` 啟動遊戲，等控制通道上線；`options` 可加啟動器選項（`-Trace`、`-Exec`、`-Sandbox` 等）。Saved 被清掉後第一次啟動會先重建 stage（最多約一小時）。 |
| `game_stop` | 只關掉佔用本 checkout 控制埠的遊戲（其他 session 的沙盒不受影響）。 |
| `game_state` | 遊戲時間、玩家、對話、購物車與桌上、女主（近的在前）；`full` 回傳完整 JSON。 |
| `game_talk` | 對正在說話的女主說一句，讓時鐘走到她回完、說完再暫停，回傳她的台詞與好感提示。 |
| `game_say` | 只送出不等回覆。 |
| `game_key` | 按鍵（E 交談、F、Escape、Tab 切換對象、N 跳到隔天、One..Four、W/A/S/D 按住秒數）。按鍵只在時鐘走、游標關閉時生效，預設會自動處理並在之後暫停。 |
| `game_buttons` / `game_click` | 列出畫面按鈕／照文字按下（會自動開關游標、捲動後重按）。 |
| `game_goto` | 先結束對話，再自動導航到女主（編號 1–10）或地點 id，時鐘走到抵達或有人搭話為止。 |
| `game_wait` | 讓時鐘走：幾秒、到某個遊戲時間（`until: "15:00"`）、或到同行結束（`outing: true`）。 |
| `game_clock` / `game_look` / `game_phone` | 時鐘暫停／繼續、轉鏡頭、在開著的手機對話裡打字送出。 |
| `game_screenshot` | 截圖（含 UI），直接回傳圖片；`scale` 2–4 是不含 UI 的高解析截圖。 |
| `game_log` | 目前遊戲日誌的最後幾行，可只取含某段文字的行。 |
| `game_exec` | 引擎主控台指令，回傳輸出：`stat unit`、`stat dumphitches`、`t.HitchFrameTimeThreshold 60` 等（控制通道 `/exec`）。 |
| `game_trace_start` / `game_trace_stop` | 錄 Unreal Insights trace 到 `Saved/Profiling/<名稱>.utrace`（`Trace.File`／`Trace.Stop`），預設頻道 `default,cpu,gpu,frame,bookmark,loadtime`。 |
| `game_call` | 其他端點直接呼叫：`/saves`、`/rules`、`/mind`、`/song`、`/money`、`/cursor`、`/summon`、`/locale`、`/menu`、`/camera`、`/mic`、`/navigate {stop}`、`/bluff`。 |

## 常用流程

- 跟女主說話：`game_goto {girl: N}` → `game_key {key: "E"}`（`game_state` 確認 target 是她，不是就 `Tab` 再按）→ `game_talk`。結束：`game_key {key: "Escape"}`。
- 存讀檔：`game_call {path: "/saves", body: {op: "save", slot: "79"}}`、`op: "load"`、`op: "new"`。
- 規則熱重載：`cargo build --release`（Tools/ok-rules）後 `game_call {path: "/rules", body: {action: "reload"}}`。
- 效能分析：`game_trace_start {name: "load-hitch"}` → 重現（例如讀檔讓女主背景載入）→ `game_trace_stop`，用 Unreal Insights 開 `.utrace`；或 `game_exec "t.HitchFrameTimeThreshold 50"` + `game_exec "stat dumphitches"` 讓每個卡頓幀的耗時寫進 `game_log`。啟動時就錄：`game_launch {options: ["-Trace", "default,cpu,gpu,frame"]}`。
