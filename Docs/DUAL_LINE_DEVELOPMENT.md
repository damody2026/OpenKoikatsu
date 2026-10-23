# 雙線開發：大世界（C）與 1 對 1 互動（E）

同一台機器上有兩份同一個 repo 的 clone，各走一條線：

| 資料夾 | 分支 | 負責 |
|---|---|---|
| `C:\portable\OpenKoikatsu` | `master` | 大世界場景（街景、城市、經濟、行程、群眾、渲染風格…） |
| `E:\OpenKoikatsu` | `duo` | 1 對 1 互動（觸碰、一對一場景、Pair、私人互動） |

E 那邊做好一段，就把 `duo` 合併進 `master`。

## 一、每份 clone 自己的埠與語音程序

每份 clone 的根目錄可以放一個 `local-instance.json`。這個檔不進 git，也不放在 `Saved/`，所以清空 `Saved/` 時會保留：

```json
{"port_base": 47960, "voice": false}
```

| 用途 | 埠 | C（沒有這個檔） | E（47960） |
|---|---|---|---|
| ok-tts | base | 47860 | 47960 |
| ok-asr | base+1 | 47861 | 47961 |
| StreetSocial 控制通道 | base+10 | 47870 | 47970 |
| Pair 控制通道 | base+11 | 47871 | 47971 |

- 由 `Tools/ok_instance.lua` 讀取。下面這些工具都會自動用自己那份 clone 的埠：
  - `Tools/run_street_social_development.lua`（`StreetSocial.cmd`）
  - `Tools/ok-game-mcp`（街區遊戲的 MCP，見 [STREET_SOCIAL_GAME_MCP.md](STREET_SOCIAL_GAME_MCP.md)）
  - `Tools/run_interaction_pair.lua`、`Tools/pair_ctl.lua`
- 環境變數 `OK_CONTROL_PORT` 和命令列的 `-ControlPort`、`-Port` 仍然可以直接指定埠號。
- `voice: false`：這份 clone 完全不碰 ok-tts、ok-asr，不啟動也不關閉。遊戲使用內建語音，沒有語音輸入。E 目前就是這樣設定。
- 開語音的那一份，啟動時只關掉執行檔位在自己 `Build/` 底下的 ok-tts、ok-asr（`ok-luakit proc-list`、`proc-kill`），不會動到另一份 clone 的程序。
- 遊戲端：
  - TTS 端點讀 `OK_TTS_ENDPOINT`，由啟動器設定，優先於 `voice-settings.json` 裡存的端點。
  - ASR 端點讀 `OK_ASR_ENDPOINT`。

## 二、Git 流程

0. 遠端（2026-10-20 起）：`origin` 是 `https://github.com/damody2026/OpenKoikatsu.git`，歷史從一個單一 commit 重新開始，之後照一般方式 commit 與推送。版控只放共用資源，角色卡與第三方 mod 的模型只留本機（[BASE_LIBRARY_SHARED_RESOURCES_PLAN.md](BASE_LIBRARY_SHARED_RESOURCES_PLAN.md)）。E 的 `local` 仍指向 C。
1. C 一律在 `master` 上 commit，並且常 push。
2. E 在 `duo` 上開發。**定期把 `master` 合併進 `duo`**（至少每天一次，或 C 推了大改動之後），衝突在 E 這邊解決。
3. E 完成一段時：
   1. 先把最新的 `master` 合併進 `duo`。
   2. 在 E 編譯並跑自測。
   3. 再把 `duo` 合併進 `master` 並 push。
   這時 `master` 這邊應該已經沒有衝突要解。
4. 引擎分開（2026-10-17）：`C:\portable\OpenKoikatsu` 用 `D:\PhysX\UE5.8`（該工作複本的 `Tools/engine.local.json`：`{"engine": "D:/PhysX/UE5.8"}`，不進 git），`E:\OpenKoikatsu` 用 `E:\PhysX\UE5.8`（預設值）。`Tools/ok_engine.ps1` 與 Lua 工具依序看 `OK_ENGINE_ROOT`、`Tools/engine.local.json`、預設 E:。原因：UBT 的 Live Coding 檢查以引擎 `UnrealEditor.exe` 路徑為 mutex，共用一個引擎時任一邊開著編輯器／遊戲，另一邊就編不過。同一份工作複本只有一套 `Binaries`，裡面所有工作階段必須用同一個引擎（兩個引擎的 Build ID 不同，換引擎會整份重編）。直接呼叫 `Build.bat` 時可加 `-NoHotReloadFromIDE`。`Tools/EnginePatches` 改引擎時，兩個引擎要各自套用。

## 三、降低衝突的規則（兩邊的 Claude session 都要遵守）

衝突熱點：

- `OKStreetSocialScene.cpp`：約 450 KB，有很多上千字的單行程式碼。
- `OKStreetSocialScene.h`：狀態結構 `FOKStreetSceneState` 就在這裡。

規則：

1. **1 對 1 的新程式碼寫在新檔案**：例如 `Source/OpenKoikatsu/OKStreetSocialDuo*.inl`（由 `OKStreetSocialScene.cpp` include）或獨立的 `.cpp`。對既有大檔案只加最少的掛鉤行，例如一行呼叫、一行 include。
2. **新的狀態欄位不要加進 `FOKStreetSceneState`**：1 對 1 的狀態放在 `OKStreetSocialDuoState.h` 的 `FOKStreetDuoState`，主結構只放一個成員。
3. **新程式碼一行一個敘述**，不要再寫超長單行，這樣衝突範圍才會縮小到單行。既有程式碼不整批重新排版，因為那會和所有正在進行的工作衝突。
4. **翻譯**：
   - `Localization/<lang>/ui.toml` 和 `rules.toml` 裡，各功能的文字放在自己的區段，只往後加。
   - `ui_text.toml` 和 `rules_text.toml` 也是一樣：新的鍵加在自己功能的區段。合併衝突時兩邊新增的鍵都保留；同一個鍵兩邊改法不同時，才需要人工決定。
5. **其他產生檔**：`catalog.json`、`catalogue.json` 等有衝突時也是重新產生。
6. **Rust 規則**（`Tools/ok-rules`）：照功能分模組，1 對 1 的規則放在自己的 `duo*.rs`。
7. **錯誤紀錄**：兩邊都往 `Docs/STREET_ERRORS.md` 後面加列。合併時兩邊的列都要保留，這種衝突直接把兩邊的內容都留下。
8. **快取版本號**：C 和 E 共用 `%LOCALAPPDATA%\OpenKoikatsu\oklib-cache`。`SESSION_CACHE_REVISION` 升號前先看另一條線用到哪一號，取兩邊最大值再加一；同一號兩邊內容不同時，會互相讀到對方的快取。合併衝突時取較大的號再加一。
9. **快取結構**：會寫進 `oklib::memo` 或 session 快取的 bincode 結構不能加減欄位，否則舊紀錄全部讀不出來。

## 四、Claude 的記憶

記憶是依專案路徑分開存的：

- C：`~/.claude/projects/C--portable-OpenKoikatsu/memory/`
- E：`~/.claude/projects/E--OpenKoikatsu/memory/`

E 開始時已經從 C 複製了一份。之後某一邊學到的通用規則，例如建置流程、陷阱、使用者偏好，要手動同步到另一邊；只跟某一條線有關的就留在原處。
