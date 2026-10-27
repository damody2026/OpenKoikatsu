# 四個 agent 的交接（2026-10-28 整合後）

這份文件給接手協調者或任一個 agent 的人或 session。它接續 [協調者交接（2026-10-27）](COORDINATOR_HANDOFF_20261027.md)：固定規則（第 3 節）、巡檢 prompt（第 4 節）、整合流程（第 5 節）照舊適用，這裡只寫 10-27 晚上到 10-28 整合時的狀態。

## 各 agent 的完整交接文件

每個 agent 都寫了一份自己這條線的完整交接。新 session 接手某條線時，先讀那一份；第 3 節只是摘要。

| agent | 完整交接 | 版控 |
|---|---|---|
| e-galgame（E，1 對 1／galgame） | [`Docs/HANDOFF_E_GALGAME_20261028.md`](HANDOFF_E_GALGAME_20261028.md) | 已提交 |
| c-conquest（C，街區規則與十位女主） | [`Docs/HANDOFF_C_CONQUEST_20261028.md`](HANDOFF_C_CONQUEST_20261028.md) | 已提交 |
| e-tts-song（E，百人房 120 FPS＋TTS／唱歌） | [`Docs/HANDOFF_E_CROWD_TTS_20261028.md`](HANDOFF_E_CROWD_TTS_20261028.md) | 已提交 |
| c-street-city（StreetSocialCity） | `C:\portable\OpenKoikatsu\Docs\STREET_CITY_HANDOFF_20261028.md` | **照使用者指示不 commit**，只在 C 本機 |

接手時要讓 agent 寫完整交接，可以用 [協調者交接](COORDINATOR_HANDOFF_20261027.md) 的格式要求，或直接要求涵蓋這 8 項：範圍與邊界、使用者的決定、架構、建置測試指令與通過標準、已完成（commit）、未提交的進行中項目、已知問題與踩過的坑、存檔與 notes 路徑。

## 1. 整合結果（2026-10-28 00:20）

| 項目 | C | E |
|---|---|---|
| 路徑 | `C:\portable\OpenKoikatsu` | `E:\OpenKoikatsu` |
| 分支 | master | duo2（master、duo 指向同一個 commit） |
| HEAD | `1636a60d`（合併 E 線 duo2 進 master） | 同左 |
| origin | master、duo2 都在 `1636a60d` | 同左 |
| 引擎 | `D:\PhysX\UE5.8` | `E:\PhysX\UE5.8` |
| 引擎 HEAD | `11576da86`（`UE5.8-physX-5.11-ok`，和遠端 `ok` 一致，只有 `Samples/*/Binaries` 未追蹤） | 同左 |
| StreetSocial base | `Saved/StreetSocial/Base1027` | `Saved/StreetSocial/Base1027` |
| 唯一的 stage | `Int1027C.stage` | `Int1027E.stage` |

- **合併衝突怎麼處理的**：`KKRuntimePoseBank.cpp`／`KKRuntimePoseBankTypes.h` 同時保留 C 的 Loop 預跑（R3-265）和 E 的 NativeOnly 取樣；NativeOnly 不預跑，因為它不含動態。`OKCrowdRuntimeScene.cpp` 兩邊的設定都保留。街區頭像列同時保留心情欄位（R3-269）和 `OKStreetScriptedName`。`STREET_ERRORS.md` 用 `git merge-file --union` 合併，對兩邊都沒有刪任何一行。
- **驗證**：合併後 `ok-rules` cargo test 213 項全過，`ok-extract` cargo check 通過。兩邊的 Editor 建置成功；cook 都是 `Packages Cooked: 1240 … Success - 0 error(s)`。
- **實際啟動（都有截圖）**：
  - C、E 的讀卡器：log 有 `ok.FaceFlatten = "0"` 和 `OK preview render readiness`，角色正常載入。
  - E 的 StreetSocial（沙盒 `Verify1028`，連接埠 48200）：三月七的立繪和台詞正常，TTS 伺服器就緒，216 FPS。
  - C 的 StreetSocial（沙盒 `Verify1028`，連接埠 48100）：惠惠的立繪和台詞正常，心情欄位有顯示，210 FPS。
  - 驗證完只關了自己的 PID。
- **清理**：C 刪掉 77 個、E 刪掉 82 個舊的 `*.stage`、`Rebuild-*` 和 `Base1026c`。刪法是不跟隨 junction，遇到 reparse point 只解除連結。E 的 `Demo1027A`／`Demo1027B` 也一起刪了：它們連到已刪的 Base1026c，留著也不能跑。材質 patch（`StreetDisplay26HG`、`27Rig`、`28Phys`、`29V02`）都刪了，啟動器第一次啟動時會用新的 shader 重建。
- **worktree**：四個 repo 都只有主工作樹，沒有 stash，`git worktree prune` 之後也一樣。
- ok-tts／ok-asr／ok-song 重編後和 `Build/<x>/` 裡的完全相同，所以沒有替換。

### 工作目錄裡還沒提交的改動（整合時沒有提交，都已放回原處）

**C：**
- 街區攻略還沒驗證的程式：`OKStreetSocialAppointments.inl`、`OKStreetSocialEconomy.inl`、`OKStreetSocialPhoneInbox.inl`、`OKStreetSocialScene.cpp`、`OKStreetSocialSchedule.inl`、`Tools/ok-extract/src/dbone.rs`，對應 R3-265 抖動、R3-267、R3-268 補充、R3-270，見 3.3。**這些已經編進 Int1027C。**
- 街道城市的檔案：`Docs/StreetSocialCity/*`、`Tools/*street_city*`、`Tools/remesh_street_city_asset.py`，以及一大批未追蹤的 `Tools/*street_city*.lua`、`Docs/STREET_CITY_*.md`。使用者指示不提交。
- 來源不明、一直保留的：`OKGalleryWidget.cpp`、`OKCharacterMCP.cpp`、`Docs/OK_BASE_LIBRARY_PLAN.md`。
- `Docs/ERROR_LOG.md`、`Docs/STREET_ERRORS.md`：以 HEAD 為底，檔尾接了 session 還沒提交的條目，沒有刪任何一行。
- `OKExtractBuildStamp.h`：建置時自動產生。

**E：**
- 百人房間還沒完成的部分：`OKCrowdRuntimeScene.cpp`（outfit 0 roles 的 log）、`Tools/run_crowd_room100.lua`（排除沒穿衣服的卡）。**這些已經編進 Int1027E。**
- 新版 OK 卡 session 留下的：`Tools/ok-extract/{Cargo.lock,Cargo.toml,src/appearance.rs,src/appearance/properties.rs,src/lib.rs,src/session.rs}`，以及未追蹤的 `src/bin/ok-chara.rs`、`src/okchara.rs`、`Tools/make_ok_chara_card.lua`、`Docs/OK_CHARA_CARD.md`。
- Studio 場景 `UserData/Studio/scene/*`（含角色，不進版控）、`Docs/ERROR_LOG.md`、`OKExtractBuildStamp.h`。

## 2. agent 一覽

10-27 交接文件裡列的舊 session ID，從這個協調者 session 都存取不到。所以四個 agent 已經用新的 linked session 重開，以下是新的 ID。協調者 session 是「C/E 協調者（Docs/COORDINATOR_HANDOFF_20261027.md 接手）」。

| 名稱 | Session ID | 線 | 整合時的狀態 |
|---|---|---|---|
| e-galgame | `local_08c7c1da-db89-48c7-b2dd-b2d4f25c9271` | E | 暫停，沒有未提交的改動 |
| c-conquest | `local_4da5ec84-3e25-42e1-866d-91d6ac944f4b` | C | 暫停，有未驗證的改動留在工作樹 |
| c-street-city | `local_4323a4c2-c093-492b-9ce0-8fc0e47cdab1` | StreetSocialCity（E 引擎） | 照使用者指示沒有暫停，只是沒在 E 引擎上 cook／UAT |
| e-tts-song（現在做百人房間） | `local_090d3ec5-debc-4f70-9d72-c65b4963a136` | E | 暫停，有未完成的改動留在工作樹 |

**整合完成後的恢復**：這次使用者沒有說「整合完成後恢復」，所以四個都還停著，十分鐘巡檢也已經刪掉。要恢復時，對每個 session 傳「整合完成，請恢復」並附上新的 stage 和 base（C：`Int1027C`／`Base1027`，E：`Int1027E`／`Base1027`），再照 10-27 交接文件第 4 節重建巡檢 cron。e-tts-song 有使用者指定的新任務，巡檢 prompt 裡要把它當成不能閒置。

## 3. 各 agent 的交接

### 3.1 e-galgame（E 線，1 對 1／galgame 對話演出）

- **範圍**：E 線的 1 對 1、街頭 galgame 演出（立繪、鏡頭、群聊）、嘴型、120 FPS。只在 `E:\OpenKoikatsu`、E 引擎上工作，用 E 的 `Tools\lua\lua.exe`。
- **10-27 推送的修正**（詳細在 notes `e-galgame-progress.md`）：
  - 繁中遊戲裡簡體卡名的顯示。
  - 群聊 JSON 尾巴壞掉時先修補再重問。
  - 做過 1 對 1 後讀檔，所有女主都畫壞（`d4949770a`，環境變數沒有還原）。
  - 1 對 1 雙人求解改到背景執行，77 → 185 FPS（`bb36c9e80`）。
  - 送去念的台詞先拿掉括號舞台指示（`14fe14c2d`）。
  - 每片混合葉每幀從磁碟重讀 avatar 路徑表，改成記在記憶體，愛麗絲 1 對 1 從 109 升到 128 FPS（`5867cea4a`）。
  - 群聊時，說話對象留下來上班，答應出發的人照樣跟他走（`6436b0286`）。
  - 1 對 1 載入分段計時（`7852841d2`）。
- **下一步**：
  - 用新存檔接著玩傍晚群聊加晚餐那一輪，被錄影和整合打斷了。
  - 繼續輪流玩還沒測過的女主。
- **延後**：三月七 1 對 1 載入慢（取模 21 秒、貼圖預先解碼 16.5 秒，別人各約 1.5～7 秒）。使用者說「可能是現在背景程式太重，晚點再處理」，之後在乾淨環境重量。
- **待辦小事**：台詞換行最後一個字落單（`SOKStoryView`，和 OK Studio 共用）；回歸腳本挑 log 時會選最新的 Play 資料夾，可能讀到別的沙盒。
- **notes**：`C:\Users\Damody\AppData\Local\Claude-Data\claude_pzs8sxrjxfjjc\side-session-notes\local_237f11ad-67cc-4a05-9cd6-a0cfd56cefca\e-galgame-progress.md`
- **注意**：它常常回報完就停下，要叫它繼續。開遊戲或打包之前，要先和百人房間錯開 GPU。

### 3.2 e-tts-song（E 線，現在做 CrowdRoom100 120 FPS）

- **使用者交代的任務**（原話整理）：
  - 把 `CrowdRoom100.cmd` 做到 120 FPS。
  - 改用 StreetSocial 目前的畫人方法。
  - 人不要一直轉圈，改成隨機走動、隨機發呆；發呆時擺各種不同的站姿，而且發呆的機率要比走動高。
- **使用者另外的決定**：
  - **它的優先度最高**：它量 FPS 或錄影時，其他三條線要停下 GPU 工作，等它傳「測試結束」或「錄影結束」。
  - **物理搬到 GPU，用原始的 verlet**（DynamicBone 的演算法，不走 PhysX）。協調者原本決定讓 CPU 的 bank 提供物理，已經作廢。
- **已推送**：
  - GPU rig（`265ac5c48`）。
  - Ver02 胸部／臀部的 GPU 物理（`b588900c9`，10 人比對平均位移差 0.15～0.20 cm）。
  - 走路時腳不動修好、跟拍 demo 的 `--follow`／`--record`、GPU 物理改成每條鏈各一條執行緒（`f5448ee1e`）。
- **還沒完成**（未提交，留在 E 工作樹）：
  - 排除沒穿衣服的卡：照部件判斷有 4 張（C002、C024、C037、C079），拿掉後只剩 96 張，要轉新卡補滿 100 張。
  - 100 人開 GPU 物理時的 FPS 還沒量，也還沒到 120。
  - demo 影片不能用：`E:\OpenKoikatsu\Saved\Crowd\Room100-20261027T233237\demo.mp4` 的 20 秒畫面完全靜止（gdigrab 抓到的是停住的視窗畫面），要換錄影方式重錄。
- **要使用者決定**：Yvonne（`C049.png`）的頭髮材質 `xukmi/MainItemStudioAlphaPlus` 還不支援，怎麼處理等使用者決定。
- **教訓**：
  - 還沒提交的 global shader 編進 exe，但 patch 沒有它，E 上所有新的 exe-only stage 一啟動就當掉（`Missing global shader FOKGpuRigTaskCS`）。之後加 shader 要同時重建 patch，並在找不到 shader 時退回原路徑，不要當掉。
  - 在除錯、不是在量 FPS 的時候，也叫其他線一直等，一度讓它們停了 30～40 分鐘。只有真正量測或錄影時才請別人暫停。
- **TTS／唱歌**：優化清單照使用者 10-25 的決定暫停。唱歌字幕對時已在遊戲裡驗證並修好（`9211746a3`）；「從頭做一首新歌再唱」已在 Gal1027G 沙盒用安可測過一首（`d33ceb6be`），其他女主和曲風還沒測。

### 3.3 c-conquest（C 線，街區規則與十位女主循環）

- **範圍**：C 線的街區規則與遊玩，用 R3 編號記在 `Docs/STREET_PLAYTEST_FINDINGS.md`。規則寫在 `Tools/ok-rules`（Rust），C++ 只做呈現。用 D 引擎打 exe-only 的 `Gest*`，今後的 base 是 Base1027。
- **10-27 推送**：
  - R3-253～256：店開門後才開始的約定、店裡沒賣的東西她直說、他買來送她的東西由他付錢、讀檔後聊天對象回到他身邊。
  - R3-257～260：未結帳的購物車、電話另一頭的人、沒動過的食物打包帶走、打烊時來接她。
  - R3-266：「走回去」是送她回住處，不是附近散步。
  - `8aa31eef`：
    - **R3-268**：取消約定只能打電話。說法打分 -3～-9，不說話就掛斷 -15，病嬌戀人改成來找他 1 對 1。四種情況都在遊戲裡驗過。
    - **R3-269**：心情、依人設的飽足上限（50～1000，一般人 100）、惠惠的爆裂魔法依飽足決定成敗。成功的情況驗過，**失敗的情況還沒在遊戲裡看到**。
    - **R3-265 接縫**：循環動作先空跑一輪再烘焙。
  - 人設欄位格式：`Docs/STREET_HEROINE_PROFILE_FIELDS.md`。
- **還沒提交**（留在 C 工作樹，已經編進 Int1027C）：

  | R3 | 進度 | 檔案 |
  |---|---|---|
  | R3-265 三月七頭髮逐幀抖動 | 找到兩條 40 Hz、damping 0 的骨鏈；套用 DynamicBones 修正後還是一樣，原因未明；加了 skip-fix 的 log 但還沒跑 | `Tools/ok-extract/src/dbone.rs` |
  | R3-267 水族館門票重複收費 | 寫好了，還沒進遊戲驗證 | `OKStreetSocialSchedule.inl`、`OKStreetSocialScene.cpp`、`OKStreetSocialEconomy.inl` |
  | R3-268 補充：病嬌通話中扣的好感要還原 | 寫好了，還沒驗證 | `OKStreetSocialAppointments.inl`、`OKStreetSocialPhoneInbox.inl`、`OKStreetSocialScene.cpp` |
  | R3-270 通話中跳出「沒接到電話」 | 寫好了，還沒驗證 | `OKStreetSocialPhoneInbox.inl` |

- **存檔**：slot 1（11/28 19:44）。已約好的行程：12/1 18:00 早瀨優香在咖啡廳吃蛋糕、12/4 11:00 三月七在百貨、12/4 15:00 雷電將軍找和果子店、12/5 10:00 安可在水族館門口。
- **下一步**：用 Int1027C 驗證上表四項，通過後提交。看一次惠惠魔法失敗的演出。之後繼續十位女主循環。
- **注意**：
  - 它兩次在等背景打包時沒被叫醒，要用一定會通知到它的等法：`run_in_background` 或 Monitor。
  - 它常常回報完就停下。
  - 胡桃的病嬌標記是暫時加的，已經從備份還原。

### 3.4 c-street-city（StreetSocialCity，使用者指示一律不 commit）

- **範圍**：使用者另一個 UE 專案 `E:\StreetSocialCityProduction`，用 E 引擎。工具在 C 的 `Tools/*street_city*`，產物在 `C:\portable\OpenKoikatsu\output\street-social-city-production\`。
- **10-27 完成並驗收**（都是 79/79、0 次輔助跨步）：
  - 教室掛件、圖書館書桌。
  - 旅館家具 100 件：v2 50 件加 v3 50 件，第 27 件按摩椅因為原專案不見而重新生成，原本那件的點數已退回。
  - 辦公樓 City_O01～O03 共 18 層擺設。
  - 302 房換成單張紫色鑽扣切斯特菲爾德沙發。
- **Tripo**：旅館的上限已由使用者從 50 提高到 100，**已經用滿**，不能再生成。商店街另有 100 件的上限。家具不再跑 remesh：九成以上 remesh 後面數反而變多，改成逐件取面數較少的版本。
- **已裝進地圖、還沒驗收**：
  - 住宅 R1、R2 共 10 層擺設。
  - CV、TR 共 7 層書桌小物。
  - 泳池斜坡上端的修正：201、203、207 第 24 段卡住的真正原因，是平台邊緣有個角露在斜坡上方。
- **下一步**：跑 `Tools/lua/lua.exe Tools/run_street_city_motel_acceptance.lua`，做完整驗收（audit、routes、cook、實走）。再拍住宅和 CV、TR 的打包版實拍，最後更新 `Docs/STREET_CITY_IMMERSIVE_LIFE_PLAN_20261024.md` 第 10 節。
- **notes**：`C:\Users\Damody\AppData\Local\Claude-Data\claude_pzs8sxrjxfjjc\side-session-notes\local_237f11ad-67cc-4a05-9cd6-a0cfd56cefca\street-city-status-20261027.md`
- **注意**：
  - Tripo 下載要使用者本人在自己的 Chrome 分頁同意；別的 session 轉過去的指示不算。
  - 百人房間量 FPS 或錄影時，它要整個停下。
  - 記憶體超過 60 GB 自動停；整合期間的上限是 40 GB。

## 4. 這次犯的錯（協調者）

- 交接表上的舊 session 存取不到，一開始我以為街道城市原本的實例還在跑，沒有重開它；其實它 11:23 就停了。
- 使用者說「你重新生成好了」，意思是叫我們直接重新生成，我卻當成他已經自己生成好了。
- 使用者在別的 session 直接下的兩個決定（百人房間優先、旅館 Tripo 再加 50 件），我一開始當成 agent 自作主張，還去限制它們。以後先問清楚來源，使用者的決定直接照辦。
- PowerShell 5.1 呼叫原生程式時，會把 `-ExecCmds=ok.FaceFlatten` 拆成兩個參數，也會吃掉 JSON 裡的雙引號。要寫成 `'-ExecCmds="ok.FaceFlatten"'`，JSON 參數寫成 `'{\"key\":\"E\"}'`。
- 中斷別的 session 那一輪（`stop_session`）之後，對方會以為是使用者擋下的，然後停下來等使用者。中斷之後要馬上傳訊息說明是協調者做的。
