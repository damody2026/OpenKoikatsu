# StreetSocial 自由對話：地點、同行活動與即時對話 計畫

日期：2026-10-10。範圍：StreetSocial 場景（`Source/OpenKoikatsu/OKStreetSocial*`）、語音辨識伺服器（`Tools/ok-asr`）。
交付物全部是 C++ 與 Rust；Python 只用於開發期實驗，不進入成品。

## 目標

1. 街區新增可互動地點並建模：餐廳、點心店、書局、圖書館、學校、操場，以及放休息房間／精油護理室的會館。
2. AI 知道附近有哪些地點（距離、方位），也知道每個地點能做什麼。
3. 內建活動可以在自由對話中提出：玩家邀約時由她決定答應與否，她也可以主動提議。
4. 去活動地點改成兩人一起走過去；走路途中與活動進行中都能繼續自由對話。
5. 內建活動結束後，兩人留在原地，不再傳送回活動前的位置。
6. 新增即時對話模式：麥克風持續收音，玩家一講完話就自動送給 AI，她回覆時暫停收音以免錄到自己的聲音。

## 現況（調查結果）

- 街道幾何是烘焙在 `StreetSocial.umap` 裡的方塊，資料來自 `OKStreetProps`（Rules.h）。地面範圍 X ±3200、Y ±2800 cm，上緣 z=0。
- 執行期生成幾何已有前例：`StreetCube/StreetSphere/StreetCylinder` + `StreetMaterial00..11`，這些都已經 cook 進 pak，不需要重新 cook。
- 玩家移動：WASD + 解析式阻擋（`OKStreetBlockedPoint`，只認烘焙道具）+ 膠囊掃掠（認得執行期生成的碰撞）。NPC 只走既有路點環。
- 活動開始時，她會瞬移到玩家旁（`TickPhoto` Phase 0），道具生成在玩家腳下；活動結束時，`FinishPhoto` 會把兩人傳送回原位。設施（`BeginFacility`）是在場外 X=7000 的隱藏房間。
- 走路同行（`BeginCompanion`）、潑水、西瓜、合照、設施期間都會清掉 `S.Target`，自由對話面板也就跟著消失。
- 有多個自動驗收探針檢查「結束後回到原位」：Photo、Seated、Drink、Meal、PhoneUse、Splash、Watermelon。

## 設計

### A. 聊天對象與即時對話（第 1 階段）

- 新增 `FreeTalkPartner()`：依序取 `Target` → 同行中的她（`Outing`／`Companion`）→ 活動中的她（`Pair.Partner`）→ 設施裡的她。
  - 自由對話面板改成只要有聊天對象就顯示，不再綁定 `Target`。
  - `EndTalk` 不再關閉自由對話；只有在聊天對象消失時才關閉。
- 即時對話模式（`ok-asr` 新端點，已完成）：
  - 伺服器端：`/listen/start`、`/listen/stop`、`/listen/mute`、`/listen/poll`。
  - VAD 判斷：自適應底噪；連續 80 ms 超過門檻就開始一句；靜音 1.0 秒結束一句；保留開口前 0.3 秒；少於 0.4 秒的丟棄，最長 20 秒。
  - 遊戲端：面板加「即時對話」開關，每 0.25 秒輪詢一次。辨識到的句子在 AI 空閒時直接送出；AI 忙碌時先排隊，回覆後接著送。
  - 防回音：她說話時、或還有分段語音沒播完時，遊戲會對伺服器 mute，停止收音。
  - 離線驗證：`ok-asr vad --wav` 把兩段 TTS 語音（中間夾 1 秒靜音）正確切成兩句，句中 0.8 秒的停頓沒有被切開。

### B. 地點（第 2 階段）

新檔 `OKStreetSocialPlaces.h`：定義地點資料、執行期建模、路線用走廊點，以及給 AI 的描述文字。

| 地點 | 中心 (cm) | 大小 | 門口／活動點 | 可做的內建活動 |
|---|---|---|---|---|
| 餐廳 | (2500,-2300) | 900×800 | (1950,-2200) | 進餐廳用餐（設施 0）、一起吃飯 |
| 點心店 | (2500,2300) | 900×800 | (1950,2200) | 一起喝飲料 |
| 書局 | (-2500,-2200) | 900×900 | (-1950,-2100) | 一起合照、一起用手機 |
| 會館 | (-2500,1800) | 900×1200 | (-1950,1700) | 休息房間（設施 1）、精油護理室（設施 2） |
| 學校 | (5400,-1700) | 1400×1600 | (4550,-1700) | 坐下聊天、一起合照 |
| 圖書館 | (5400,0) | 1200×1000 | (4650,0) | 坐下聊天 |
| 操場 | (5000,1700) | 場地 1800×1600 | (4500,1700) | 潑水、打西瓜 |

- 學校、圖書館、操場放在 X 3200～6400 的延伸區，會補一塊地面（頂面 z=0）。
- 建模用現成的方塊 mesh 與 12 種材質組合：主體、屋頂、窗、門、招牌色帶；操場是草地、跑道、球門。
- 建築主體有碰撞，所以玩家掃掠、她跟隨、路線規劃都會自動避開。
- 走廊點：`(3000,50)`、`(3000,-1550)`、`(3000,1650)`、`(3600,-1700)`、`(3600,0)`、`(3600,1700)`，加上各地點門口。只用於同行路線，不加進 NPC 的巡邏路點。
- 小地圖：每個地點畫一個標記和名稱。
- 互動：走到門口 3 公尺內時，右側面板出現該地點的活動按鈕。

### C. 同行活動（第 3 階段）

新檔 `OKStreetSocialOuting.inl`：

- `BeginOuting(Girl, ActivityId)`：
  - 查出活動對應的地點；地點不限的活動（手機、合照）也接受，會在附近的合適地點或原地進行。
  - 用 `OKStreetContactRoute`（既有路點 + 走廊點 + 門口）規劃路線。
  - 玩家自動沿路線以步行速度前進，她用既有的同行跟隨邏輯（`StepCompanion`）跟在旁邊。
  - 走路途中可以自由對話；Esc 取消。
- 抵達門口後，用既有的 `BeginPair`／`BeginFacility` 開始活動（現有的 250 cm 距離條件會成立）。
- 活動結束後留在原地：
  - `FinishPhoto` 不再傳送回原位，只解除她的 `Held` 並讓她恢復走動；鏡頭參數照舊還原。
  - 設施原本就會回到「進設施前的位置」，現在那個位置就是會館或餐廳門口，所以不用改。
- 自動驗收探針：Photo、Seated、Drink、Meal、PhoneUse、Splash、Watermelon 的「回到原位」檢查，改成「留在原地且已解除 `Held`」。

### D. AI 整合（第 4 階段）

- 系統提示（固定，可快取）：加入地點與活動目錄（id、名稱、在哪裡做），以及規則：
  - 玩家邀約時，依角色與好感決定是否答應；答應就在 `action` 填活動 id。
  - 她也可以主動提議，但要等玩家同意後才填 `action`。
- 最後一則訊息（會變動）：附近地點（距離、方位）、目前正在做什麼（走向某處／某活動中／無）。
- 輸出格式加上 `"action": "none" | 活動 id | "go:地點 id"`。
- 遊戲端驗證 `action`：要在目錄裡，而且現在條件允許（例如不在其他活動中）。允許才開始同行；否則在提示列說明為什麼不行。

### E. 驗證（每個階段）

- `ok-asr`：`cargo build` 加 `ok-asr vad` 離線切句測試（已通過）。
- 遊戲：Interaction host 建置（`prepare_interaction_runtime_host.lua` + `D:\UE5.8`）→ `stage_interaction_runtime.lua` → 用測試啟動腳本開遊戲，確認沒有崩潰。
- DeepSeek：遊戲外實驗確認 `action` 的判斷合理（答應、拒絕、主動提議）。
- 最後由使用者在遊戲內實測完整流程。

## 進度（2026-10-10）

| 階段 | 狀態 | 檔案 |
|---|---|---|
| A 聊天對象與即時對話 | 完成，已編譯 | `OKStreetSocialLiveTalk.inl`、`Tools/ok-asr/src/listen.rs` |
| B 地點 | 完成，已編譯；遊戲啟動記錄 `Street places: 7 places, 62 parts` | `OKStreetSocialPlaces.h`、`OKStreetSocialOuting.inl`、`OKStreetSocialMinimap.h`、`OKStreetSocialRules.h`（`OKStreetRuntimeBlocks`） |
| C 同行活動 | 完成，已編譯 | `OKStreetSocialOuting.inl`、`FinishPhoto`／`FinishFacility`、活動按鈕、`OKStreetSocialInvitations.inl` |
| D AI 整合 | 完成；遊戲外實驗 5 種情境全部符合 | `OKStreetSocialFreeTalk.*`、`PostFreeTalk` |
| E 驗證 | 遊戲可啟動；7 個活動探針通過；邀請探針因 C004（性格 29）沒有邀請資料而失敗，已補資料重跑 | stage `StreetFreeTalk07` |
| F 串流語音（延遲改善 5） | 完成，已編譯；首段聲音 0.10 秒（原本約 0.8 秒），和完整合成的相關係數 0.99 | `ok-tts` `/synthesize_stream`、`KKRuntimeSpeech`（`FKKRuntimeSpeechStream`）、`OKSpeakCrowdRuntimeStream`、`RequestFreeTalkSegment` |
| G 大學城地點與室內（2026-10-11） | 完成；21 個地點、15 個可走進去，28 個錨點活動在遊戲內全部實測啟動 | `Tools/street_models/`（Blender 產生 .okmesh v2：屋頂分離、房間、錨點）、`OKStreetSocialPlaces.h`、`OKStreetSocialOuting.inl` |
| H 即時對話：沉默與打斷 | 完成，已編譯；DeepSeek 主動開口與告別的回應已實測 | `OKStreetSocialLiveTalk.inl`、ok-asr `duck` 模式 |
| I 水族館、公廁、其餘室內 | 完成；26 個地點全部可到達，其中 20 個可走進去；37 個地點活動實測全部啟動；女廁擋住男主實測 4 間都通過；即時對話端到端測試 PASS（主動開口、扣好感 3 次後離開、打斷） | `interiors_public.py`、`interiors_resort.py`、`interiors_more.py`、`TickRestroom`、`TickLiveTalkTest`、ok-asr `/listen/inject` |

## 風險與注意事項

- **同時編輯**：Codex 也在修改 `OKStreetSocialScene.cpp`（2026-10-10 16:36 仍有寫入）。本計畫的新功能盡量放在新檔案，場景檔只加小段呼叫，每次修改前重新讀檔；建議這段期間避免兩邊同時改同一個檔案。
- 自動驗收探針的行為改變是刻意的，對應需求 5。
- 學校、圖書館、操場在原本地面之外：NPC 不會自己走過去，只有同行時才會去。
- 自動同行時，玩家暫時不能用 WASD 操控方向；需要時可以 Esc 取消。
