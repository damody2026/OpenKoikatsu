# E 線 galgame／1 對 1 交接（2026-10-28）

給接手「[E組] galgame 對話演出」的新 session。讀完這份就能接著做，不必看舊對話。
整合後的狀態：HEAD `3b8b95d88`（duo2），E 的 stage 是 `Int1027E`，base 是 `Saved/StreetSocial/Base1027`。

## 1. 範圍與邊界

**負責**
- E 線（`E:\OpenKoikatsu`，分支 `duo2`，引擎 `E:\PhysX\UE5.8`，Lua 用 `E:\OpenKoikatsu\Tools\lua\lua.exe`）。
- 街頭 galgame 演出：對話框、左下即時立繪、逐句鏡頭（第一人稱 pov／越肩 over／雙人）、群聊、走路時的演出。
- 1 對 1 場景（Interaction／高模、雙人姿勢求解）的正確性與效能，目標 120 FPS。
- 嘴型、送語音前的文字處理（拿掉舞台指示），E 的 TTS／ASR 接線。
- 實玩：用新存檔玩還沒測過的女主，修實際遇到的問題。

**不要碰**
- C 線 `C:\portable\OpenKoikatsu` 的檔案（只能讀）。不要叫 C 重建任何東西。
- `StreetSocialCity/`、`E:\StreetSocialCityProduction`（使用者的另一個 UE 專案）。
- 別的 session 的未提交改動。E 工作樹常有百人房（e-tts-song）的 GpuRig／KKCrowdGpuPacking／ok-extract gpu_rig、appearance、okchara 等改動，也有唱歌 session 的 `OKStreetSocialSong.inl`、`Tools/ok-rules/src/song.rs`。只提交自己的 hunk（見第 4.6 節）。
- 三月七 1 對 1 載入慢：**使用者說「可能是現在背景程式太重，晚點再處理」→ 擱置**，在乾淨環境重量之前不要動。

## 2. 固定規則與使用者的決定

- 回覆一律繁體中文，提到檔案給完整絕對路徑。（10-27 有幾則誤寫成英文，使用者說「講中文」。）
- 使用者的決定直接照辦，不反問、不二度確認。
- 只 kill 自己的 PID（用 `game_stop`，或控制埠 47970 上的 PID），不要依 exe 路徑或啟動時間殺程序。
- 量 FPS 或判斷 TTS 是否順暢時，GPU 上只能有一個 Unreal 遊戲。自己的 Unreal 實例最多兩個。
- 和百人房 session（e-tts-song，`local_090d3ec5-debc-4f70-9d72-c65b4963a136`）共用 E 引擎與 GPU：
  - 開遊戲或打包前先通知它。它要量 FPS 或錄 demo 時，立刻關掉自己的遊戲、暫停打包，等它傳「測試結束」或「錄影結束」再開。
  - 它在工作樹裡的未提交程式會一起編進 exe-only stage。它加了 global shader 時，要等 patch 重建（`Launch/runtime.json` 的 patch 欄位）後才能用，否則一啟動就當掉。
- 協調者 session：`local_237f11ad-67cc-4a05-9cd6-a0cfd56cefca`。它要求暫停時，提交已驗證的工作、列出未完成項、關遊戲和背景工作，等「整合完成」。
- 打包一律 exe-only，加 `--skip-cook --skip-base`，每次用新 tag。完整 cook 只有整合時由協調者做。
- `.stage` 目錄裡有 junction，不要遞迴刪除。要換版本就用新 tag。
- 錯誤紀錄：街區相關寫 `E:\OpenKoikatsu\Docs\STREET_ERRORS.md`，其他寫 `Docs/ERROR_LOG.md`，寫在最後面。
- 先量測再改：不要照猜測寫最佳化（見第 7.3 節）。
- 長時間無人值守時不要停在兩輪之間；被擋住時每 10 分鐘回來確認，期間做不用 GPU 的事。
- 測試行為時用 `-Exec "t.MaxFPS 60"`；量 FPS 時不要加。

## 3. 架構重點

### 3.1 街頭 galgame 演出
| 部分 | 檔案 | 說明 |
|---|---|---|
| 演出邏輯（Rust） | `Tools/ok-story/src/live.rs`、`portrait.rs`、`tuning.rs`（`ok_story.dll`，`oks_live_*`） | 收「誰說了什麼」＋facts（骨架、相機、語音進度），回傳打字進度、立繪相機、鏡頭與深度探針。C ABI 用 JSON。 |
| 街頭接線（C++） | `Source/OpenKoikatsu/OKStreetSocialGalgame.inl` | `TickGalgame`：把即時對話字幕（`FreeTalkCaptionPlayer`／`FreeTalkCaptionHer`）、劇本台詞轉成 `line` 輸入；群聊逐人送（`G.Group`，跟著每人的語音開始）；套用鏡頭、隱藏擋鏡頭的路人、拍立繪（`G.Portrait.Update`，只拍說話者自己的元件）。 |
| 畫面 | `Plugins/OKStudio/Source/OKStudio/Private/SOKStoryView.cpp`、`OKStoryCapture.cpp` | 對話框 widget（`OKStoryWrap` 換行禁則：收尾標點不放行首、最後一行不留 1～2 字孤字）、立繪擷取。與 OK Studio 共用。 |

- 立繪：`live.rs` 收到 `Line{who}` 就 `portrait.want(who)`；`portrait.rs` 先淡出舊的再換人，30 Hz 擷取。C++ 用 `who` 的 `c<slot>` 找 `OKGetCrowdRuntimeCharacterComponents`。
- 每句台詞 log 一行 `Street galgame line c<slot>: ...`。控制通道 `/galgame` 回傳 `story.who`、`story.portrait`、`shot`，用來查立繪與鏡頭。
- 名字：繁中遊戲裡卡片名字是簡體的女主（雷电将军、贞德…）在畫面上照字體顯示（`OKStreetScriptedName`）；送給模型的格式化字串裡，含 `name` 的參數由 `Tools/ok-rules/src/locale.rs` `text_of` 用 zhconv 轉字體。
- 開關：`ok.Street.Galgame 1`（預設）。說明文件：`Docs/GALGAME_STREET.md`（各日期小節都有細節）。

### 3.2 群聊
- `Source/OpenKoikatsu/OKStreetSocialParty.inl`：
  - `SendPartyTalk`：組 system／user prompt，經 ok-mind（`SubmitMindChat`）呼叫模型。
  - `FinishPartyTalk`：解析回覆 JSON（`lines`、`favor`、`leave`、`stay`、`do_now`、`orders`、`payer`、`faces`、`told_about`）。
  - `PartyHandOver`：說話對象離開時，交接給下一位成員。
  - `PartyStartLeaving`：給 10 秒挽留時間。
  - `TickParty`：成員站位與跟隨。
- 群聊提示：`Content/IndependentResources/StreetSocialData/Localization/<lang>/ui_text.toml` 的 `ai_party_prompt`（三種語言都要改）。改完可在遊戲裡用控制通道 `/rules {"action":"reload"}` 重新載入（`LoadUiTexts`），不用重新打包。stage 的 `StreetSocialData` 是 junction，連回工作樹。
- 模型回覆 JSON 壞掉時：`Tools/ok-mind/src/mind.rs` `run_chat` 會補括號（`close_json_object`），補不了就帶 `mind.malformed_json_reminder` 重問一次。
- 規則記錄，判斷交給 AI：關鍵字規則不判斷對話內容，要加的是回覆 JSON 欄位，C++ 依欄位行動。

### 3.3 1 對 1（Interaction）
- 進入：`OKStreetSocialContacts.inl` `BeginPrivate` → `GalgameHideForPrivate`（收起街頭對話框）＋`OKStreetPrivateEnvSave`。
- 離開：`FinishPrivate` 還原取模環境變數（`Source/OpenKoikatsu/OKStreetSocialPrivateEnv.cpp/.h`：`OK_GPU_SKIN`、`OK_SKELMESH`、`OK_NOPOSE`、`OK_UE_ANIM`、`OK_HEAD_GPUSKIN`、`OK_CPU_SKIN`、`OK_GAME_ROOT`）。她在 4 m 內就用 `ResumeTalkQuiet` 接回對話。
- 女角高模載入：`Source/OpenKoikatsu/OKInteractionMode.cpp` `LoadFemale`。log 會分段記耗時：`Interaction female load %s: card, session, body physics, textures ms`。
- 雙人姿勢：遊戲裡沒有 OK 配對包，全部走原生路徑。`Plugins/KKUnityBundle/.../KKPairedPlayback.cpp` 把原生 GPU 求解放到背景（`bPendingNative`，下一幀顯示，`ok.Anim.AsyncPair`）。`KKPreviewActor.cpp` `PairedFrameGpuParts`。
- Rust 求解：`Tools/ok-extract/src/anim.rs`（動畫取樣；`load_avatar_tos_shared` 依根目錄把 avatar 路徑表記在記憶體）、`session.rs`（飾品動態準備）、`dbone/`（動態骨）。
- 只缺切線的部件：`KKPrivateGpuSource.cpp` 由法線補切線，改走 GPU。缺其他資料流時，錯誤訊息會列出缺哪些。

### 3.4 嘴型與語音前處理
- 嘴型（10-26）：跟著語音開合，講完閉上；表情要張嘴時依那個嘴型決定張多大（`19af9225b`），舌頭不穿出下巴（`87625f6d0`）。
- 送 TTS 前拿掉括號裡的舞台指示（≤16 字的括號）：`Source/OpenKoikatsu/OKStreetSpokenText.h` `OKStreetSpokenText`。用在她的段落（`RequestFreeTalkSegment`）與他的台詞（`OKStreetSocialVoice.inl` `RequestTtsLine`）。字幕保留括號。
- E 的語音：`E:\OpenKoikatsu\local-instance.json` 內容 `{"port_base": 47960, "voice": true}`，遊戲 47960 起、控制埠 47970。ok-tts／asr 跟著遊戲啟動和關閉。

## 4. 打包、啟動、測試

### 4.1 打包（exe-only）
```powershell
E:\OpenKoikatsu\Tools\lua\lua.exe Tools\rebuild_street_social_runtime.lua --tag <新名字> --base-dir Saved/StreetSocial/Base1027 --skip-cook --skip-base
```
- 在 `E:\OpenKoikatsu` 執行。日誌在 `Saved/StreetSocial/Rebuild-<tag>/`（編譯錯誤看 `build.log`）。約 5～15 分鐘，放背景跑。
- 打包前確認 E 引擎上沒有別人的 UAT 或 cook。閒置的 `dotnet MSBuild.dll /nodeReuse` 程序不算。
- 遊戲 target 會把警告當錯誤（例如 C4456 變數遮蔽）。別人的檔案編不過時通知對方，不要替對方改。
- 整合後 patch 都刪了，啟動器第一次啟動時會用新 shader 重建 patch。

### 4.2 啟動與操作（ok-game-mcp）
- 一律用 `E:\OpenKoikatsu\Tools\ok-game-mcp\target\release\ok-game-mcp.exe call <工具> '<JSON>'`，JSON 在 git-bash 裡比較好處理引號。
- 啟動：
  ```bash
  ok-game-mcp.exe call game_launch '{"Executable":"E:/OpenKoikatsu/Saved/StreetSocial/Build/<tag>.stage/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe","Exec":"t.MaxFPS 60"}'
  ```
- 啟動後必查：最新 `Saved/StreetSocial/Play-*/game.log` 裡有 `Base Directory`，而且沒有 `LogWindows: Error`，才算遊戲真的在跑。同時有兩個 Play 資料夾時（別人的 Editor -game），用控制埠 PID 找自己的那份。
- 常用工具：
  - `game_call {"path":"/saves","body":{"op":"new"}}`：開新存檔（也可 `save`／`load` 加 slot）。
  - `game_wait {"until":"17:30","timeout_s":900}`：讓時間跑到指定時刻。單次會因 timeout 停在中途，要分段呼叫。
  - `game_goto {"girl":N}`、`game_key {"key":"E"|"Tab"|"Escape"|"End"}`、`game_talk {"text":...}`（等她回完）、`game_say`（不等）。
  - `game_state`、`game_screenshot`、`game_inspect {"girl":N}`（單獨看某位的模型，按 End 離開）、`game_call {"path":"/galgame"}`。
  - `/fps`（POST）、`/private {"girl":N}`／`{"off":true}`（直接進出 1 對 1）、`/rules {"action":"reload"}`（重新載入規則與文字表）。
- 按 Tab 找人時要再確認 `game_state` 第二行的名字；女主會走動，可能選到別人（10-27 對刻晴喊了「將軍」）。
- 開著對話不說話時時間照樣走，她會覺得被冷落、好感下降。長時間等待前先 Escape 結束對話。

### 4.3 回歸腳本
```powershell
E:\OpenKoikatsu\Tools\lua\lua.exe Tools\galgame_regression.lua [--launch] [--girl 3] [--port N] [--keep] [--private]
```
- 檢查項目：scene、pov、portrait、text、skip、rollback、memory、direction（舞台指示不念），加 `--private` 時還檢查 1 對 1 結束後取模環境是否還原，並在讀檔（slot 9）後截圖。
- 每項印 PASS／FAIL／WARN，有 FAIL 時結束碼為 1。用控制埠上的 PID 找這場的 log。
- 10-27 結果：刻晴 9/9、胡桃與貞德·Alter 9/9、貞德 `--private` 12/12、三月七 10/10。

### 4.4 量 FPS
- GPU 上只能有這一個遊戲，不加 `t.MaxFPS`。用 `/private {"girl":N}` 進 1 對 1，等載入完（看 log 的 `Interaction female load`）再讀 `/fps`，取三次。
- 不要用 `startfpschart`（每次都會建新資料夾）。

### 4.5 trace（細分 Rust 雙人求解）
1. 啟動時就開：`game_launch` 加 `"options":["-Trace","default,cpu,frame","-GameArgs","-OKNativeTrace"]`。Rust 區段在第一次求解時登記，事後才開會登記成空類型、全被丟掉。
2. 匯出：
   ```powershell
   UnrealInsights.exe -OpenTraceFile=<trace.utrace> -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics out.csv" -AutoQuit -NoUI
   ```
3. 看 `KK_Native*`：區段名稱表在 `KKRustExtract.cpp` 的 `NativeTraceSpecs[47]`，Rust 端用 `Scope::new(id)`，id 37～46 是 10-27 加的。
4. trace 動輒幾百 MB，看完就刪。

### 4.6 只提交自己的 hunk
- 先 `git diff <檔案>` 確認只有自己的改動，才整檔 `git add`。
- 檔案裡混有別人的 hunk 時：組出「HEAD 版本＋自己的修改」的內容，`git hash-object -w --no-filters` 寫入物件，再 `git update-index --cacheinfo 100644,<hash>,<path>` 放進索引。
- 推送：`git push origin duo2`，用 `git ls-remote origin duo2` 確認。這個 checkout 的 `origin` 是 GitHub（`https://damody2026@github.com/damody2026/OpenKoikatsu.git`）；另一個 remote `local` 才是 `file:///C:/portable/OpenKoikatsu`。
- 很多檔案是 CRLF：Lua／sed 改檔時要保留換行格式，改完用 `git diff | od -c` 確認有 `\r`。`/tmp` 路徑 Windows 的 Lua 讀不到，改用 `C:/Users/Damody/AppData/Local/Temp`。

## 5. 已完成（10-26～10-27，都在 duo2）

### 10-26（前任 galgame 線，嘴型、對話框、群聊名字）
- `19af9225b` 嘴巴跟著語音開合，講完閉上；表情要張嘴時依嘴型決定張多大。
- `87625f6d0` 講話時舌頭不再穿出下巴，閉嘴時嘴巴真的閉上。
- `ad7bed68f` 立繪只畫到文字欄左邊。
- `b19a015bf` 名字標籤用遊戲字體；「⋯」換成字型有的「…」。
- `69e44e64c` 只有他的話才回滾；「〜」換成全形「～」。
- `5c0f752ed` 第一人稱：她坐下時鏡頭跟著轉向她的臉。
- `d2f87e4dc` 第一人稱：她的臉離開畫面可見範圍才重新取景（取代「移動 30 cm」）。
- `5691375df` 第一人稱：她鞠躬時鏡頭不再跟著往下拍頭頂。
- `85f6e7a16` 卡片名字是簡體的女主，群聊台詞不再被整句丟掉。
- `5e04a427f` 群聊名字用遊戲字體給模型；正在播的語音斷掉時下一句立刻送出。
- `ba069bc05` 在班女主的「下班才能走」提醒寫出她的名字，不再被當成他的班。
- `ba7d00ca4`、`69f94c288`、`78d19a1fe` 街區 120 FPS：bonds_tick 移到背景、路線拉直分幀、單幀卡頓細分計時。

### 10-27（本線）
- `e5efba75d` 繁中遊戲裡簡體卡名的女主照字體顯示（小地圖、標題、導航、目標標籤，`locale.rs` 名字參數轉字體）；群聊時他的話進對話框；「一起讀書」不算冷場；太飽沒點成的菜講清楚、不會上桌。
- `c849fa6c9` 群聊回覆 JSON 尾巴壞掉時先修補、修不了重問一次；1 對 1 場景不再每幀印 clothes layer／shadow skipped。
- `d58ffb418` 一起走路聊天時鏡頭看向兩人中間（`GalgameWalkFocus`），她不再被右側面板擋住；`acts.toml` 加 `contact`（交換電話記進她的紀錄）。
- `4d519a2f4` 回放最近 20 句不再被上桌、點餐通知或沉默搭話打斷；立繪擷取 log 每種只印一次；`game_log` 在 PID 重用時讀這場的 log。
- `2bfbd0362` 點餐與上桌通知提醒她是在對他說話，用「你」稱呼他。
- `9a23e5635` 進 1 對 1 時收起街頭 galgame 對話框。
- `f193c29c5` 1 對 1 結束時她就在身邊就接回對話（原本她照行程走掉，連他剛付錢的午餐都不管）。
- `d4949770a` 離開 1 對 1 時還原取模環境變數；之後讀檔或開新遊戲，所有女主不再畫壞（缺衣服、頭髮變黑色尖刺）。
- `bb36c9e80` 沒有 OK 配對包的雙人姿勢也在背景求解，1 對 1 從 77 提升到 185 FPS。
- `14fe14c2d` 送語音前拿掉括號裡的舞台指示（她不念「（小聲）」，他也不念玩家打的「（揮手）」）。
- `156abf96d` 回歸腳本加 direction 與 `--private`；`GALGAME_STREET.md` 補上 10-27 的內容。
- `f89509192` 回歸腳本用控制埠上的 PID 找 log。
- `b4448f485` 只缺切線的部件由法線補切線、改走 GPU（愛麗絲 6 個部件原本每幀 CPU 蒙皮）；對話框換行禁則與孤字。
- `c946e469c` Rust 動畫取樣加三個計時區段（規劃、葉取樣、混合）。
- `5867cea4a` 雙人動畫不再每幀每片混合葉重讀 avatar 路徑表，愛麗絲 1 對 1 從 109 提升到 128 FPS。
- `6ab2fc406` 飾品動態準備加三個計時區段；記下 trace 誤報與未提交 global shader 造成 exe-only stage 當掉。
- `7852841d2` 女角高模背景載入分段記錄耗時（讀卡、取模、胸部物理、貼圖預先解碼）。
- `6436b0286` 群聊：說話對象在同一則回覆裡留下（例如還在上班）時，先交接、由答應的人帶頭出發；三種語言的群聊提示補「部分人去、她留下」的寫法。

## 6. 進行中與待辦

1. **恢復實玩（下一步）**：用 `Int1027E` 開新存檔，跑到傍晚 17:30，測傍晚群聊加三人晚餐（例如咖啡廳雷電將軍下班後＋安可，或百貨 10F 景觀餐廳）。整合前這一輪被錄影與暫停打斷，還沒玩到。驗證方式：
   - log 的 `Street party talk: ... do_now=` 有值，且 `Street free talk action` 由對的人執行。
   - 到了之後 `Street party order` 有點到餐。
   - 截圖確認立繪是說話者本人（先用 `game_inspect` 確認各女主的長相）。
2. **三月七 1 對 1 載入慢（擱置，等使用者）**：Gal1027R 量到取模 21.3 s＋貼圖預先解碼 16.5 s，對照優香 6.9＋5.2、安可 6.1＋1.5（當時背景很重）。之後在乾淨環境重量，再看 `Session.Open` 取模和 `PrecacheSessionTextures` 哪一段該改。不是「第二次才慢」：新程序第一次開她也要 14 秒，安可只要 1.6 秒。
3. **1 對 1 的 FPS（暫停）**：安可、優香、愛麗絲偏慢。每個角色的動態約 2 ms（身體 0.42、衣物 0.79、飾品 0.69、匯出 0.1），兩個角色平行時等待約 3.2 ms，比單人多約 1 ms，原因未查清（可能是 rayon 排程）。要再快屬結構性改動，先不做。
4. **更穩的取模環境**：Rust 端人群開卡時用 `profile::scope` 固定設定，取代現在的「進場記下、離場還原」。尚未做。
5. 小事：引擎 `stat fps` 在 1280x720 時疊在右側面板標題上；餐點備餐中就已經畫在桌上（未確認是不是刻意的）。

## 7. 已知問題、FPS 數據、踩過的坑

### 7.1 各女主 1 對 1 FPS
- 乾淨時（GPU 上只有另一個 C 的遊戲）：胡桃 185、愛麗絲 128（修正前 109）。
- Gal1027P，C 的 Gest145 同時在跑、吃很多 CPU，絕對值偏低：

| # | 女主 | FPS |
|---|---|---|
| 1 | 貞德·Alter | 151 |
| 2 | 雷電將軍 | 140 |
| 3 | 三月七 | 88（落在載入期間，不算） |
| 4 | 刻晴 | 130 |
| 5 | 胡桃 | 128 |
| 6 | 早瀨優香 | 92 |
| 7 | 貞德 | 171 |
| 8 | 愛麗絲 | 94 |
| 9 | 惠惠 | 103 |
| 10 | 安可 | 81 |

- Gal1027Q 重量（C 的遊戲與 StreetSocialCity 仍在跑）：安可 126～142、優香 112～124、愛麗絲 98～116。
- 街頭：221 FPS（只有 C 一個遊戲時）。
- 三個 Unreal 遊戲同時在跑時 GPU 96%，量不了 120 FPS。

### 7.2 已知問題（不在本線或還沒改）
- C 的 ok-game-mcp 也有 game_log PID 重用的問題（已在 E 修，整合時已帶過去）。
- 腳本話題的問答偶爾對不上（不在本線）。
- 模型偶爾說錯小事（例如紅茶已上桌卻說「我的還沒來」、說學生餐廳有薯條），屬模型判斷，沒有改。

### 7.3 踩過的坑與誤判
- **猜測式最佳化**：先猜世界矩陣重建、簽章才是瓶頸，連打兩次包都沒用。真正原因是磁碟快取每次重讀，讓記憶體快取永遠不中。規則：先用 trace 細分到最小區段再改；以 `Arc::ptr_eq` 當鍵的快取，在上游每次都給新 `Arc` 時等於沒有快取。
- **回報「trace 還在錄」，其實遊戲第 0 幀就當掉**：別人未提交的 global shader（`FOKGpuRigTaskCS`）被編進 exe，但 patch 裡沒有。規則：打包和開遊戲分成兩個指令，查 game.log 的 `Base Directory` 和 `LogWindows: Error` 才回報。
- **誤報「E 上沒有遊戲」**：被中斷的工具其實已經開了遊戲（PID 11152）。要用控制埠查，不要憑記憶。
- **立繪誤判**：以為群聊立繪換錯人，其實是把安可（粉髮、紅旗袍）認成胡桃（棕髮、白帽）。`/galgame` 的 `story.portrait` 是對的。先用 `game_inspect` 確認長相再下結論。
- **測試時選錯人**：Tab 迴圈停在走過來的刻晴，對她說「將軍」。
- **長時間等待時對話還開著**：女主氣「不理我」，好感下降。是測試操作造成的，不是 bug。
- **PowerShell 的 JSON 引號**：會被吃掉，改用 git-bash 呼叫 ok-game-mcp。
- **game_log 讀到舊 log**：PID 重用時讀錯，已修（`4d519a2f4`）。
- **ok-extract 暫時編不過**：百人房的 `gpu_rig` `mod flat` 改到一半，等它改完就好。

## 8. 存檔 slot 與 notes

- slot 5：第二天，刻晴已是朋友（10-27 第一輪）。
- slot 7：愛麗絲的新存檔（第二輪）。
- slot 9：`galgame_regression.lua --private` 會存讀這格，不要放重要存檔。
- 其他輪都是 `/saves new` 開的新存檔，沒有特別保留。整合後 `Saved/` 可能被清理，用之前先 `/saves` 確認。
- notes（10-27 的逐步進度，內容已整理進本文件）：
  `C:\Users\Damody\AppData\Local\Claude-Data\claude_pzs8sxrjxfjjc\side-session-notes\local_237f11ad-67cc-4a05-9cd6-a0cfd56cefca\e-galgame-progress.md`
- 相關文件：`Docs/GALGAME_STREET.md`（演出細節與量測）、`Docs/GALGAME_STORY_USAGE.md`、`Docs/STREET_ERRORS.md`（10-27 的錯誤條目）、`Docs/AGENTS_HANDOFF_20261028.md`（整合總表）。

## 9. 10-28～10-29 進度（e-galgame，2026-10-29 08:50 因整合暫停）

接手的 session：e-galgame（`local_be065ef7`）。暫停時已關掉自己的遊戲（PID 52160）、停掉打包，slot2、E 鎖（`Saved/Crowd/unreal.lock`）都已釋放，queue 沒有登記。所有改動都已提交（未 push）。

### 9.1 這兩天的 commit（duo2）
- `116e65be8` 街區用餐：坐好才開始吃；離座位 2 m 內被擋就直接入座。
- `20e8b39ba`、`ad7c45674` 1 對 1 進場卡頓：材質平行解碼改背景優先權，最多 NumWorkers/4 條通道；身體互動目錄（`catalog-v1.json` 8.5 MB）只解析一次。
- `1f20d13a8` 1 對 1 載入：配對清單在背景先取，進場那一幀不再呼叫 `NativeAnimationPairs`。
- `ced4c3d7d` 女主行程：約了午餐／晚餐的那天，約會前那頓改輕食（不餓就不吃早餐）。
- `113e29db1` 女主行程：晚餐至少在午餐結束 3 小時後（`plan.rs` `MEAL_GAP`）。
- `dd73e6e3d` 街區對話：玩家是男生，稱呼他用「你」不要寫成「妳」。
- `10ccc2e87` 女主的情境加上男主住在校外男生宿舍。
- `07c2036e2` 判斷器情境加上她今天接下來的安排與地點 id（`JudgePlanLine`）。**遊戲裡還沒看到觸發**（她說「先陪我走一趟」且他答應時，do_now 應填那件事的 go:地點）。實玩遇到再驗，不用刻意觸發。
- `648afd267` 講話嘴型：閉唇的表情嘴型說話時借 0 號嘴型的張開形狀（使用者回報嘴巴沒開合；`ok.Mouth.TalkBorrow`）。
- `e8252b945` 手機聯絡人、提示列與字幕的女主名字改成遊戲語言的字體。
- `3a682325d` 導航走到女主身邊時視線轉向她並選定她。
- `05a70eb9a` ok-mind 測試沒有 A 股行情資料時跳過。
- `78127edd6`、`9c0d0016e` `GALGAME_STREET.md`：三月七 1 對 1 乾淨重量（冷開進入 9.1 → 6.7 s，夥伴 12 → 9.1 s；材質約 1.6 s 是最大的一段，屬開卡／材質，要改先通知 crowd session）與十張女主 okVo 聲音確認（音色要使用者自己聽）。
- `5c4c672d5` **未驗證**：答應的行動失敗時告訴她兩人現在還在哪裡（惠惠在女生宿舍聽到「飲料店沒開」就說「明明都走到門口了」）。Gal1029K 打包在連結前被暫停中止；含這個檔的 unity 單元已編過、沒有新的警告或錯誤。

### 9.2 沙盒與存檔
- 沙盒 `GalE`：`-Sandbox GalE -PortBase 48360`，控制埠 48370（`OK_CONTROL_PORT=48370`），存檔在 `E:\OpenKoikatsu\Saved\StreetSocial\Sandbox\GalE\Saves\`。
- 欄位 17（10-29 08:20）：第二天 09:28，和惠惠在女生宿舍（好感 41），雷電將軍的外觀已確認正常。`auto`（08:48，關遊戲時自動存）是之後一點：惠惠剛拒絕進 1 對 1（魅力 50 對抗性 52，規則判定，不是 bug）。
- 欄位 1～16 是 10-28～10-29 各輪的新存檔，沒有特別保留。
- 最新的 E stage：`E:\OpenKoikatsu\Saved\StreetSocial\Build\Gal1029J.stage`（base `Saved/StreetSocial/Base1028b`）。`Gal1029K` 沒有完成，不要用。

### 9.3 開卡副本（OK_EXTRACT_ROOT）
- 打包時 ok-extract 用乾淨的匯出副本，不吃 crowd session 在工作樹裡的未提交改動：
  `OK_EXTRACT_ROOT='E:\Saved\okx-galgame-head3\Tools\ok-extract' Tools/lua/lua.exe Tools/rebuild_street_social_runtime.lua --tag <新 tag> --base-dir Saved/StreetSocial/Base1028b --skip-cook --skip-base`
- `E:\Saved\okx-galgame-head3` 是 `75725edac` 的匯出（`git archive HEAD Tools/ok-extract Tools/vendor Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders | tar -x -C <目錄>`，再複製前一份的 `target` 並跑 `<目錄>\Tools\ok-extract\build-release.cmd`）。整合後 HEAD 改了要重做一份新的；`okx-galgame-head2` 是更舊的，可以刪。

### 9.4 下一步
1. 整合完成後用新的 stage（或用乾淨副本重新打包一個 exe-only tag），讀 GalE 欄位 17 或 `auto`，先驗 `5c4c672d5`：約她去還沒開的店，她的回答不再說「已經走到門口」。
2. 繼續 1 對 1／街頭實玩找 bug；`07c2036e2` 遇到再驗。
3. 1 對 1 進場的材質 1.6 s：若要動，先和 crowd session（`local_26f3bbaf`）說好，KKRuntimePose／KKUnityBundle 載入路徑是共用的。
4. 已知非 bug：陌生人接電話機率 8%；控制通道會在指令之間暫停時鐘，所以活動有時顯示「約 0 分鐘」；FPS 顯示疊在右側面板上（開發用顯示，不改）。
