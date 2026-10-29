# 四個 agent 的交接（2026-10-29 整合後）

給使用者重新叫回 C 組、E 組 agent 用。接續 [10-28 的交接](AGENTS_HANDOFF_20261028.md)；10-28 00:51 起的 72 小時無人值守，在這次整合時由使用者叫停（四個 agent 都暫停，十分鐘巡檢已刪除）。

## 1. 整合結果（2026-10-29）

| 項目 | C | E |
|---|---|---|
| 路徑 | `C:\portable\OpenKoikatsu` | `E:\OpenKoikatsu` |
| 分支 | master | duo2（master、duo 指向同一個 commit） |
| HEAD | 合併 commit `ae3c2841`（E 線 duo2 `2707ed2c0` 併進 master），之後只多一個 commit：這份交接與錯誤紀錄 | 同左 |
| origin | master、duo2 都指向上面那個 commit | 同左 |
| 引擎 | `D:\PhysX\UE5.8` | `E:\PhysX\UE5.8` |
| 引擎 HEAD | `11576da863ff`（`UE5.8-physX-5.11-ok`，工作樹乾淨，兩邊相同，這次不用同步） | 同左 |
| StreetSocial base | `Saved/StreetSocial/Base1029` | `Saved/StreetSocial/Base1029` |
| 唯一的 stage | `Int1029C.stage` | `Int1029E.stage` |
| 開卡副本（OK_EXTRACT_ROOT） | 不需要（C 工作樹的 ok-extract = HEAD） | `E:\Saved\okx-int1029\Tools\ok-extract`（HEAD 匯出，已編好） |

- **合併衝突**：只有 `Docs/STREET_ERRORS.md`，用 `git merge-file --union` 合併，兩邊的每一行都還在。
- **驗證**：
  - 合併後 `ok-rules` cargo test 219 項通過（1 項標為略過）；兩邊的 ok-rules、ok-story、ok-mind release 都重編過。
  - 完整重建（新 base）兩邊都成功：cook `Packages Cooked: 1240 … Success - 0 error(s), 0 warning(s)`，UAT `BUILD SUCCESSFUL`。E 的 UBT log 有 `ok-extract from OK_EXTRACT_ROOT E:\Saved\okx-int1029\Tools\ok-extract`（KKUnityBundle、KKRuntimePose 都是）。
  - 兩邊的 OpenKoikatsuEditor 也重編成功（讀卡器用）。
  - 實際啟動（都有截圖）：
    - C、E 讀卡器：log 有 `ok.FaceFlatten = "0"` 與 `OK preview render readiness`，角色正常載入。
    - E StreetSocial（沙盒 `Verify1029`，連接埠 48200）：所有角色就緒，TTS／ASR READY；和貞德·Alter 對話正常回覆，236 FPS。
    - C StreetSocial（沙盒 `Verify1029`，連接埠 48100）：惠惠（戀人）立繪與對話正常，TTS 就緒，220 FPS。
    - 驗證完只關了自己的 PID。
- **清理**：C、E 共刪掉 77 個目錄：舊的 `*.stage`（C 的 `Conq1028j`～`Conq1029i`、`Int1028C`、`Int1028cC`；E 的 `Gal1028R`～`Gal1029J`、`Int1028cE`）、`Base1028b`、舊的 `Rebuild-*`，以及 `E:\Saved\okx-*` 的舊開卡副本（只留 `okx-int1029`）。刪法用 `rmdir /s /q`，不跟隨 junction；`Base1029` 檔案數刪除前後相同。刪除後可用空間 C 365 GB、E 409 GB。**之前的 Conq／Gal stage 都連到已刪的 Base1028b，已不能用；恢復後請重新打 exe-only tag。**
- **worktree**：四個 repo（C、E、D 引擎、E 引擎）都只有主工作樹，`git worktree prune` 過，沒有 stash。

### 工作目錄裡還沒提交的改動（整合時沒提交，都放回原處）

**C：**
- 街道城市的檔案：`Docs/StreetSocialCity/*`、`Tools/*street_city*`、`Tools/remesh_street_city_asset.py`，加上一大批未追蹤的 `Tools/*street_city*.lua`、`Docs/STREET_CITY_*.md`。使用者指示一律不提交。
- `Docs/STREET_ERRORS.md` 檔尾：c-street-city 的旅館條目（街道城市內容，不提交）。
- `Docs/ERROR_LOG.md` 檔尾：E-250 水槍那幾條（10-25 讀卡器 session 留下的，來源 session 已不在）。
- 來源不明、一直保留的：`Source/OpenKoikatsu/OKGalleryWidget.cpp`、`OKCharacterMCP.cpp`。
- `OKExtractBuildStamp.h`：建置時自動產生。

**E：**
- 新版 OK 卡（okchara，10-24 的 session）留下的：`Tools/ok-extract/{Cargo.lock,Cargo.toml,src/appearance.rs,src/appearance/properties.rs,src/lib.rs,src/session.rs}`，未追蹤的 `src/bin/ok-chara.rs`、`src/okchara.rs`、`Tools/make_ok_chara_card.lua`、`Docs/OK_CHARA_CARD.md`，以及 `Docs/ERROR_LOG.md` 檔尾那幾條 10-24 okCh 條目。**E 的打包一律用 `OK_EXTRACT_ROOT` 指向乾淨副本，不要連到這批改動。**
- `Content/IndependentResources/base_v1/missing.txt`（轉卡追蹤輸出）、Studio 場景 `UserData/Studio/scene/*`（含角色，不進版控）、`OKExtractBuildStamp.h`。
- crowd 沒做完的「CrowdRoom100 預設 GPU rig」：存成 patch `E:\OpenKoikatsu\Saved\Crowd\e-okcard-gpu-rig-default-20261029.patch`，工作樹是 HEAD。

## 2. 四個 agent 與完整交接

| 名稱 | Session ID（在 linked session 列表裡） | 線 | 完整交接（先讀這份） | 暫停時 |
|---|---|---|---|---|
| c-conquest | `local_71e498e8-f226-4131-8118-646e6153f9ff` | C，街區規則與十位女主循環 | [`Docs/HANDOFF_C_CONQUEST_20261028.md`](HANDOFF_C_CONQUEST_20261028.md) 第 9 節 | 第 5 輪進行中，一批未驗證修正已提交（`3adec53c`） |
| e-galgame | `local_a0b10ff7-ded3-47f2-97b4-cae9e0f5ba44` | E，1 對 1／街頭 galgame 實玩 | [`Docs/HANDOFF_E_GALGAME_20261028.md`](HANDOFF_E_GALGAME_20261028.md) 第 9 節 | `5c4c672d5` 未驗證 |
| crowd-gpu-physics（現在叫 e-okcard，做 OK 卡轉換與開卡速度） | `local_26f3bbaf-e73d-47de-8b4c-c0bdbae675bc` | E，OK 卡／百人房 | [`Docs/HANDOFF_E_CROWD_TTS_20261028.md`](HANDOFF_E_CROWD_TTS_20261028.md) C 節（百人房在 A 節） | 100 張批次待重量 |
| c-street-city | `local_4e050a6f-b5c8-4452-8687-6809382d9194` | StreetSocialCity（E 引擎） | `C:\portable\OpenKoikatsu\Docs\STREET_CITY_HANDOFF_20261028.md` 第 9 節（**照使用者指示不 commit，只在 C 本機**） | 第 11 輪完整實走沒跑完 |

協調者（主 session）是「C/E 協調者」，這份文件就是它寫的。原 session 都還在，可以直接傳訊息叫醒；如果要開新的 session 接手，用第 4 節的 prompt。

## 3. 各線摘要（細節看第 2 節的完整交接）

### 3.1 c-conquest（C）
- **使用者的決定**：每輪五位女主都攻略成戀人（zh-CN、自己玩），一輪有不合理的行為就開新遊戲重來，直到一輪完全乾淨；不要停下來問，一直玩到使用者說停。規則寫在 `Tools/ok-rules`（Rust），C++ 只做呈現；話語的判斷由 AI judge，關鍵字規則不判斷。
- **進度**：第 4 輪五位戀人但不乾淨（終局欄位 6）。第 5 輪：雷電將軍已是戀人，愛麗絲、刻晴是朋友；存檔 sandbox `Conq1028` **欄位 7**（9/8 07:03）。9/8 已約：10:00 貞德·Alter 百貨挑耳機、16:00 雷電將軍點心店。
- **未驗證、已提交**（`3adec53c`）：R4-13、19、21、24、25、27(3)、28、R5-6、R5-7，驗法在交接第 9 節表格。
- **恢復後**：用 `Int1029C` 或新的 exe-only tag（`--base-dir Saved/StreetSocial/Base1029 --skip-cook --skip-base`），開 `Conq1028` 讀欄位 7；先聽一次十位女主的聲音（10-29 補回 okVo），再赴 9/8 兩個約，一邊驗證上面那批。

### 3.2 e-galgame（E）
- **範圍**：E 線 1 對 1、街頭 galgame 演出（立繪、鏡頭、群聊、嘴型）、女主行程。只在 `E:\OpenKoikatsu`、E 引擎，用 E 的 `Tools\lua\lua.exe`。
- **進度**：沙盒 `GalE`（`-Sandbox GalE -PortBase 48360`，控制埠 48370），**欄位 17** 或 `auto`。
- **未驗證**：`5c4c672d5`（行動失敗時告訴她兩人現在在哪裡）；`07c2036e2`（「先陪我辦事」）實玩遇到再驗。
- **恢復後**：用 `Int1029E`，或 `OK_EXTRACT_ROOT=E:\Saved\okx-int1029\Tools\ok-extract` 打新的 exe-only tag（base `Base1029`）；先驗 `5c4c672d5`，再繼續 1 對 1／街頭實玩找 bug。1 對 1 進場材質 1.6 s 要改的話，先和 e-okcard 協調（共用 KKRuntimePose／KKUnityBundle 載入路徑）。

### 3.3 e-okcard（E，原 crowd-gpu-physics）
- **使用者的目標**：新版 OK 卡只靠卡內 `okLc`／`okLt`＋基礎庫開卡（嚴格開卡）；轉卡 100 張合計 ≤ 100 秒、每張 ≤ 10 秒；冷開每張 ≤ 3 秒，OK 卡不可比現在大超過 10%。
- **進度**：100 張 12 程序 857 → 507 秒（`0f7fd1add`）；之後 `315aff4cf`（C032 根因）、`75725edac`、`b5ad7c31d` 還沒重量。
- **恢復後**：拿雙 slot 重量 100 張；每張 CPU 還要減半；冷開 march_7th、C018 約 3.1 秒。開發在匯出副本＋獨立 `CARGO_TARGET_DIR`，寫回前通知 e-galgame。
- **百人房（A 節）**：CrowdRoom100 120 FPS、GPU rig／GPU verlet 物理，未完成的 GPU rig 預設改動在上面的 patch。

### 3.4 c-street-city（StreetSocialCity，一律不 commit）
- **範圍**：`E:\StreetSocialCityProduction`（E 引擎），工具在 C 的 `Tools/*street_city*`，產物在 `C:\portable\OpenKoikatsu\output\street-social-city-production\`。不要碰 `C:\portable\OpenKoikatsu\StreetSocialCity\`。
- **進度**：旅館第 10 輪驗收通過（79/79、0 次輔助跨步）；第 11 輪（P7 修訂 11 間）已安裝、打包、實拍，**完整實走沒跑完**。
- **Tripo**：使用者 10-28 決定「剩下的 Tripo 點數全部用完」做全世界視覺；清單 `world-tripo-v1/plan.json` 137 件，全都還沒送出，等使用者接上 Claude in Chrome。下載要使用者本人在自己的 Chrome 分頁做 Batch Export。
- **恢復後**：看第 11 輪實拍 → 排隊重跑完整驗收 → 修 406／510／513／312 與浴室曝光；可以先做不需要 Tripo 的世界邊緣、岸線、草地材質。

## 4. 叫回 agent 的方法

### 4.1 原 session 還在（建議）
對該 session 傳：

> 整合完成，請恢復。新的 base 是 `Saved/StreetSocial/Base1029`，stage 是 C：`Int1029C`／E：`Int1029E`（合併 commit `ae3c2841`）；E 打包用 `OK_EXTRACT_ROOT=E:\Saved\okx-int1029\Tools\ok-extract`。照你的交接文件最新一節的「下一步」繼續，規則照 `C:\portable\OpenKoikatsu\Saved\SESSION_RULES_ALL_20261028.md`。

### 4.2 開新的 session 接手
新 session 的標題加 `[C組]`／`[E組]` 前綴，prompt 範本：

> 你接手 <線名>。先讀 `<完整交接路徑>`（最新一節是暫停時的狀態）和 `Docs/AGENTS_HANDOFF_20261029.md` 第 3 節，再讀共同規則 `C:\portable\OpenKoikatsu\Saved\SESSION_RULES_ALL_20261028.md`（E 線另讀 `E:\OpenKoikatsu\Saved\Crowd\SESSION_RULES_20261028.md` 與 `E_LINE_OWNERSHIP_20261028.md`）。目前 base `Base1029`、stage `Int1029C`／`Int1029E`。照交接的下一步做，不要停在回報；只提交自己的 hunk，不 push（由協調者推），Unreal 名額照 `Saved/UnrealSlots` 規則排隊。

### 4.3 協調者的十分鐘巡檢（要無人值守時才開）
用 CronCreate 每 10 分鐘（例如 `3,13,23,33,43,53 * * * *`）跑：

> 無人值守巡檢：用 list_sessions {"linked": true} 與 list_events 檢查四個 agent。規則：`C:\portable\OpenKoikatsu\Saved\SESSION_RULES_ALL_20261028.md`、`E:\OpenKoikatsu\Saved\Crowd\SESSION_RULES_20261028.md`。檢查：停在回報後沒繼續（叫它做交接的下一步）、卡住超過 30 分鐘、鎖或 slot 殘留（持有者已不在跑）、排隊公平（queue 等超過 30 分鐘、持有超過 60 分鐘）、違規（push、提交卡片或街道城市檔案、關別人 PID、Unreal 超過 2 個、在共用樹留未驗證的大改動）。新 commit 看過內容（無卡片、無街道城市檔、不引用未追蹤檔）就推上 origin（E 只推 duo2、C 只推 master）。先查各 session 裡使用者的原話再下決定；自己決定、不要問使用者。base 為 Base1029（Int1029C／Int1029E）。有決定或異常才簡短回報。

## 5. 共同規則重點（全文在 `Saved/SESSION_RULES_ALL_20261028.md`，本機檔）

- **Unreal 名額**：全機最多 2 個（Editor、遊戲、cook／UAT 都算）。`mkdir C:\portable\OpenKoikatsu\Saved\UnrealSlots\slot1`（失敗換 slot2）拿名額、寫 `owner.txt`，用完刪掉；等的時候在 `queue\<名稱>.txt` 登記。量 FPS／量測要雙 slot。持有超過 60 分鐘且有人等時，到存檔點就放掉。等待條件只看自己啟動的程序；停掉背景腳本後確認它真的結束（10-29 的兩次教訓）。
- **E 鎖**：`E:\OpenKoikatsu\Saved\Crowd\unreal.lock`（E 的建置、打包、遊戲）。**城市鎖**：`C:\portable\OpenKoikatsu\output\street-social-city-production\city.lock`。
- **E 線分工**：ok-extract、轉卡、卡片歸 e-okcard；`OKStreetSocial*`／`OKInteraction*`／`OKBodyInteraction*`／`OKStudio`／ok-story 歸 e-galgame；KKRuntimePose、KKUnityBundle 載入路徑共用，改之前先傳訊息協調。
- **版控**：只提交自己的 hunk（暫時 index）；卡片、OK 卡、含角色的 Studio 場景、街道城市的檔案一律不提交；不開 worktree；不對別人正在改的檔做 checkout／restore；agent 不 push，由協調者推（E 只推 duo2、C 只推 master，整合時才對齊 master）。
- **打包**：平常 exe-only（`--skip-cook --skip-base`、新 tag）；`-Sandbox` 啟動前先 `cargo build --release` Tools/ok-rules、ok-story、ok-mind。`.stage` 目錄裡有 junction，不要遞迴刪除。完整 cook 只在整合時做。
