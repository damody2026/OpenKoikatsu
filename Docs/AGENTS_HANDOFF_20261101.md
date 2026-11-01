# 2026-11-01 整合交接

使用者在 2026-11-01 22:30 要求整合 C／E／F 三個專案與三個引擎、暫停所有 agent、推上 master、清掉 worktree 與暫存檔，並把四個 agent 與協調者的工作寫成交接文件給下一個 AI。本文件是下一個 AI 的起點。上一份是 [2026-10-31 整合交接](AGENTS_HANDOFF_20261031.md)。

## 整合結果

| 項目 | 狀態 |
|---|---|
| 專案 | C `master`、E `duo2`、F `duo2` 都在同一個 commit（本文件的提交），推上 `origin/master` 與 `origin/duo2`（兩者相同） |
| 引擎 | `D:\PhysX\UE5.8`、`E:\PhysX\UE5.8`、`F:\UE5.8` 都在 `11576da863ff`（= `ok/UE5.8-physX-5.11-ok`，這輪沒有變動）。F 有 16 個第三方 `.sh`／`.bat` 只差換行符號 |
| Rust | 合併後 `ok-extract` 預設與 `--no-default-features` 都 `cargo check` 通過；`ok-rules` 238 測試通過；`ok-mind` check 通過 |
| C 街區 | 完整重建 `Int1101C`，base `Saved/StreetSocial/BaseInt1101C`（D: 引擎）。實際啟動：讀進既有存檔（04:14、五位女主好感都在），「所有角色已就緒」 |
| E 街區 | 完整重建 `Int1101E`，base `Saved/StreetSocial/BaseInt1101E`（E: 引擎）。實際啟動：十位女主就緒 |
| 讀卡器 | C、E 的 `CardReaderDevelopment.cmd` 都重編編輯器（各約 144 秒）並啟動，進入遊戲迴圈、prewarm 基礎庫成功；log 只有已知的 CommonUI routing error（`C:\portable\OpenKoikatsu\Saved\Logs\CardReader\20261101-225757-14f09723.log`、`E:\OpenKoikatsu\Saved\Logs\CardReader\20261101-225756-0d391650.log`） |
| F OK Studio | 編輯器與遊戲目標都建置連結成功；`verify_ok_studio.lua --tag Int1101F` 6/6 通過（證據 `F:\OpenKoikatsu\Saved\OKStudio\Verify-Int1101F`） |

建置時三邊工作樹裡未提交的程式改動先 `git stash` 停放（只建已提交的內容），建完已 `stash pop` 放回。

## 本輪併入的提交（8e2545bef 之後，51 個）

- **c-conquest**（C）：R6-69～R6-96，街區攻略第 12 輪的修正（約定、請客、點菜打包、聽說的事、抽籤通知等）。最後一筆 `8dce02189`。
- **c-street-city**（C）：`91bcb5983`（內陸水域海床、沙丘）、`2754e048f`（內陸水域灰石護岸、岸邊探測、實走 runner 低 FPS 誤判重試）。
- **e-galgame**（E）：`d0ea4ba71`（陰莖以陰道為支點、凸起包住龜頭、頭髮下垂）、`4194f8b17`（慾望 200、流水／噴水、陰道開合、高潮抖動、原作喘息與叫聲）、`745fb79e9`、`4ada59bc6`（叫聲跟抽插、水反光、街上原地 1 對 1）、`85acda441`（原作插入語音與效果音、睪丸隱藏、插入點下移、半透明開關、上癮、身體開發度、物理與灰色背景）。
- **F**：U32～U48（關節補正、Standard 打光、SunShafts 深度、遠面、IK 旋轉與權重、FK 初始化、材質編輯器、場景瀏覽器分頁、快搜篩選、設定窗、資源目錄等），以及 Studio 角色語音 ogg（LFS，約 1.9 GB）與 fsbex 修正。
- **協調者**：`f34171c7d`（錯誤紀錄）、本文件。

## 各 agent 暫停時的狀態與下一步

Session ID（Claude 桌面 app 的 linked session，恢復時用 `SendMessage`／`mcp__ccd_session_mgmt__send_message` 傳給它們）：

| agent | session | checkout | 標題 |
|---|---|---|---|
| e-galgame | `local_f60d0ffc-61f0-4be5-b495-8b82f91113a3` | `E:\OpenKoikatsu`（duo2，引擎 `E:\PhysX\UE5.8`） | [E組] galgame／1 對 1 實玩 |
| f-hs2studio6-ui | `local_032a9287-a1e3-4c33-a32e-fcceee0da7d5` | `F:\OpenKoikatsu`（duo2，引擎 `F:\UE5.8`） | [F組] 參考 HS2Studio6 寫工作室 UI／工作流強化計畫 |
| c-conquest | `local_ed138316-60b5-41a7-9825-619c5c0b4a7d` | `C:\portable\OpenKoikatsu`（master，引擎 `D:\PhysX\UE5.8`） | [C組] 街區攻略：十位女主循環 |
| c-street-city | `local_b8435541-ae45-4ac4-8d1b-0d7beaa8f54e` | `C:\portable\OpenKoikatsu`（master，引擎 `D:\PhysX\UE5.8`） | [C組] 街道城市：旅館第 11 輪與世界視覺 |

e-okcard（E 的 OK 卡量測）已被使用者封存，不要再傳訊給它。

### e-galgame（E，一對一 H 互動，第一優先）

- **目標**：使用者要求 1 對 1 H 互動做到完美；清單全部打勾並跑完一輪乾淨的才算完成。只做 H 互動。
- **工作樹未提交（自己的）**：腹部穿模回拉，`Source/OpenKoikatsu/OKHScene.cpp`／`.h` 共 9 行，未在遊戲裡驗證。
- **下一步**：
  1. 驗證穿模回拉：所有性交姿勢逐一拍側面與正面，確認陰莖不再穿出腹部；不行再修。
  2. 身體開發度實測：用 `dev` 參數設 0 和 100 對照，看深度上限、胸部變大與口交臉部。
  3. 喘息加速（最多 1.8 倍、音高不變）的聽感待使用者確認。
- **已知事項**：插入姿勢 067（泳池後入）、079／080（香蕉船）在私人房間沒有道具，刻意排除；071 已可進入。高潮次數會存檔、隔天才歸零，測試要推到隔天。測試按鈕「鎖住她」是切換式的，只在顯示「鎖住」時按。

### f-hs2studio6-ui（F，OK Studio UI／工作流，第二優先）

- **計畫文件**：`Docs/OK_STUDIO_UI_WORKFLOW_PLAN.md`，U0～U48 都有完成紀錄。
- **工作樹未提交**：無（自己的）。
- **下一步**：U49 地圖窗「設定」→ U50 角色卡路徑列 → U51 資源檢查「資源目錄」跳轉 → U52 材質編輯器隱藏屬性與接收陰影；做完再對照 HS2Studio6 參考截圖列下一批。U28 效能要等全機只剩一個 Unreal 時量。
- **已知差異**：舞台下方那一條（heat 層與海立方體）記為已知差異，不再追。
- **注意**：提交訊息要寫驗證（遊戲＋編輯器連結、`verify_ok_studio` 結果）；拆 hunk 要帶上下文（見 ERROR_LOG）。建置時設 `MSBUILDDISABLENODEREUSE=1`。

### c-conquest（C，街區攻略循環）

- **目標**：照 `memory` 的 conquest loop：zh-CN 親自攻略五位（現為十位）女主到戀人，新遊戲再來，直到一輪沒有不合理行為。
- **進度**：第 12 輪，遊戲日 9/9 13:41 在水族館等三月七。三月七、安可、優香是戀人 150；惠惠是朋友 200（她說「我得好好想想」）；胡桃是朋友 128。
- **存檔**：`Conq1028` 的 auto 槽（Conq1102m 21:21 正常退出時自動存）；欄位 6 是 20:37 的手動存檔。恢復後先讀 auto 槽。
- **下一步**：14:00 和三月七去水族館、晚上打電話給安可、週六 10:00 和胡桃去水族館，然後等惠惠的答覆。R6-90（當場用掉的東西可再買）還沒在遊戲裡驗證，要等下一次抽籤。
- **工作樹未提交**：無。
- **規則**：只開一個 Unreal（換版前先關舊版）；每個修正都要遊戲＋編輯器連結；ok-rules 測試要跟著改（needs_words 這種文字斷言）。

### c-street-city（C，街道城市世界，最低優先）

- **交接文件**：`Docs/STREET_CITY_HANDOFF_20261028.md`（含內陸護岸一節與驗收進度）。
- **進度**：內陸護岸 4 段實走第 0–2 段通過，第 3 段與重拍沒跑（`walk-coast-next.txt` 目前是 3）。
- **下一步**：從第 3 段接著跑實走，過了再依 `edge-recheck-views.json` 重拍 G099、G113、A04、A17 的岸邊；之後依剩餘清單：世界邊緣山丘、地面接縫、海灘內緣階梯、A17 死路、A13 空地、地標碰撞。
- **使用者決定（2026-11-01）**：`StreetSocialCity/` 不要動。開場低 FPS 造成的輸入煙霧測試誤判只在 runner 端重試（已做，最多 3 次並記錄開場 FPS）。
- **工作樹未提交**：先前就沒提交的 street-city 工具與文件（`Tools/apply_street_city_motel_*`、`Tools/attach_street_city_immersion_*`、`Docs/STREET_CITY_*_2026102x.md`、`Tools/street_city_resort_architecture*.lua`、`Tools/street_city_vertical_sign_text.lua`、`Tools/remesh_street_city_asset.py`、`Docs/StreetSocialCity/*`），照原樣留著。
- **規則**：一次只開一個 Blender；Unreal slot 最低優先，背景鏈每步讓位。

## 三邊工作樹留著的未提交改動（不屬於任何進行中的 agent）

- **E、F 的 okchara WIP**：`Tools/ok-extract/Cargo.toml`／`Cargo.lock`、`src/appearance.rs`、`src/appearance/properties.rs`、`src/lib.rs`、`src/session.rs`，未追蹤的 `src/okchara.rs`、`src/bin/ok-chara.rs`，以及 `Tools/make_ok_chara_card.lua`。照舊不動。
- **E 的 e-okcard 舊 WIP**：`OKResidentGpuPose.cpp/.h`、`OKCrowdRuntimeScene.cpp`、`OKCrowdRuntimeProgressiveGpu.inl`、`KKPreviewLoading.cpp`、`base_v1/missing.txt`（e-okcard 已封存，沒量測過）。
- **F**：`base_v1/converted.tsv`、`missing.txt`、`sources.okidx` 與約 90 個 `base_v1/models/**/*.okmodel` 的改動；`UserData/Studio/scene/ill_ss_sample_0{0,1,2}` 的 `.okscene`／`.import.json`（開 KStudio 範例時重新匯入產生的）。來源不明，不要提交前先確認。
- **C**：`Source/OpenKoikatsu/OKCharacterMCP.cpp`、`OKGalleryWidget.cpp`（來源不明，10-31 起就在）。
- **三邊**：`Plugins/KKUnityBundle/Source/KKUnityBundle/Private/OKExtractBuildStamp.h` 是建置自動改的，不提交。

## 協調者的工作方式（下一個 AI 接手協調時照做）

- **十分鐘巡檢**（本輪已刪除排程，恢復 agent 時用 `CronCreate` 重建，例如 `3,13,23,33,43,53 * * * *`）：
  1. `mcp__ccd_session_mgmt__list_sessions {"linked": true}` 看四個 agent；閒置的依本文件與它最後的回報派下一步，不讓 agent 空轉。
  2. `Tools\lua\lua.exe Tools\unreal_slot.lua status` 看 Unreal slot；全機最多 3 個 Unreal。優先序 e-galgame > F > c-conquest > c-street-city（`Tools/unreal_slot.lua` 的 PRIORITY）。佔著 slot 卻沒有 Unreal 在跑的要請它 release。
  3. 檢查三邊新提交：沒有角色卡（png／apng）、沒有 API key（`sk-…`、`api_key = "…"`；DeepSeek key 絕不能上傳）、文字檔是 UTF-8；改到共用程式碼（`Tools/ok-extract`、`Tools/ok-rules`、`Tools/ok-mind`、`Plugins/`、`Source/`、`Cargo.*`、`.Build.cs`）的提交要有 `cargo check`（含 `--no-default-features`）與 UE 遊戲＋編輯器連結結果，沒有就請該 agent 補。
- **使用者規定**：
  - 沒說要整合就不整合 C／E／F：不推送、不在三邊之間同步或合併，各組提交留在各自 checkout。
  - C／E／F 之間不能編譯衝突，衝突就是重大 bug，最優先修（AGENTS.md「不能讓別組編譯失敗」）。
  - `StreetSocialCity/` 不要動。
  - 不問使用者，只有真正需要使用者決定的事才回報。
  - 回覆用繁體中文，檔案給完整絕對路徑。
- **app 限制**：沒有使用者發言時，連續傳給其他 session 約 10 則訊息後會被擋，使用者發言後重置。
- **整合流程**（使用者說要整合時）：
  1. 暫停 agent，請它們提交已驗證的工作、關掉自己的 Unreal、release slot。
  2. **先把對方 checkout 的 LFS 物件複製過來**（`robocopy <src>\.git\lfs\objects <dst>\.git\lfs\objects /E /XC /XN /XO`），否則合併會在 smudge 時中止並留下半寫的檔案（本輪犯過，見 ERROR_LOG）。
  3. 在 C 用 `git fetch <E/F 路徑> duo2:refs/integ/X` 取回，`git merge --no-ff`；工作樹有 WIP 的檔案先 stash。E、F 用 stash → `merge --ff-only refs/integ/C` → stash pop，pop 後掃衝突標記。
  4. 停放三邊未提交程式（stash），`cargo check` 兩種 feature、ok-rules 測試，C／E 各跑 `Tools/rebuild_street_social_runtime.lua --tag Int<日期>C|E --engine <各自引擎> --base-dir Saved/StreetSocial/BaseInt<日期>C|E`（E 用 `E:\OpenKoikatsu\Tools\lua\lua.exe`），F 建置編輯器＋遊戲並跑 `verify_ok_studio.lua`。
  5. 實際啟動兩邊 `StreetSocial.cmd`（`-Control`，C 控制埠 47870、E 47970，用 `ok-game-mcp.exe call game_state` 確認「所有角色已就緒」）與 `CardReaderDevelopment.cmd`。
  6. 刪掉舊 stage／base／Rebuild／Play 紀錄（`cmd /c rmdir /s /q`，stage 裡有 junction，絕不用 `Remove-Item -Recurse`），stash pop 放回 WIP，寫交接文件，推 `master` 與 `duo2`。

## 待使用者決定

- F 的 H 動作變體檔（約 13,639 個、361 MB）與 `studio_anims.json` 是否進版控（要用 Git LFS）。乾淨 clone 可用一條指令重產，步驟在 `Docs/OK_STUDIO_UI_WORKFLOW_PLAN.md`。（10-31 起未決）
- 喘息加速的聽感（e-galgame）。

## 清理

- 刪除 C、E 舊的開發 stage（Conq1101a～Conq1102m、Gal1101R20n～R22j）、`Play-*` 與 `Rebuild-*` 紀錄、`E:\Saved\okx-*` 三個 Rust 匯出資料夾、`%TEMP%\ffwip_*` 74 個同步暫存，共 510 個資料夾、約 57 GB。刪完確認 Content 與 base／host 都在、版控沒有被刪的檔。
- 整合版啟動確認後刪除 `Int1031C`／`Int1031E` stage 與 `BaseInt1031C`／`BaseInt1031E`。
- 三個專案與三個引擎都沒有額外的 git worktree；Claude app 的 worktree 已清。

## 恢復 agent

巡檢排程已刪除，四個 agent 都停在等恢復指示。恢復時重建十分鐘巡檢，把本文件各 agent 的「下一步」派給對應 session；Unreal slot 照舊用 `Tools/unreal_slot.lua`。
