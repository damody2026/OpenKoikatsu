# 街區：新功能實作計畫（來自攻略第三輪盤點）

來源：[STREET_MISSING_ACTIVITIES.md](STREET_MISSING_ACTIVITIES.md)。本計畫做「程式與資料」的部分。新場景由使用者製作，包括夜市、海邊／河堤、空地、百貨 8F 大熊、公園噴水池，以及店員、撐傘、畫板等動畫與 prop。這裡先把玩法接口做好，場景到位後填入地點 id 與素材即可。

## 分工：遊戲邏輯在 Rust，C++ 只做呈現

每個功能都走同一條路：

```text
玩家操作（按鍵、按鈕、說話）
  → C++：收集輸入與現況（位置、時間、對話與 judge 的 JSON、street_ext 狀態），組成請求
  → Rust：計算（能不能做、結果、數值、新狀態、要告訴她的事實、要演出的指令）
  → C++：照 Rust 的回傳呈現（走路、動畫、特效、截圖、UI、通知），並存下新狀態
```

- C++ 不在中間做判斷。Rust 回傳的「要演出什麼」要明確到 C++ 照著做就好，例如 `{"play": "sit_sketch", "prop": "board", "minutes": 75}`。
- 需要 AI 解讀的事（她答應了什麼、要做什麼）先經過 judge。C++ 把 judge 的 JSON 原樣交給 Rust，由 Rust 決定怎麼記錄。

| 層 | 負責 | 不做 |
|---|---|---|
| **Rust `Tools/ok-rules`**（無狀態 DLL，可熱重載） | 所有遊戲規則與決定：活動能不能做、時長與數值、物品歸屬與借還、照片紀錄、約定排程與重複、節日、天氣影響、打工與課表、校務、開場白要用哪條路。新狀態以 JSON 文件進出：`{state, event}` → `{state, effects, notice, tell_her}` | 不碰 UE 物件、動畫、畫面 |
| **AI（DeepSeek 回覆與 judge）** | 解讀對話：她要做什麼、答應了什麼、要送／借什麼、是不是每週、要辦什麼事 | 不算數值 |
| **UE C++** | 把 Rust 的結果演出來：走路與動畫、prop、特效、截圖、UI 面板與手機頁、存讀檔（Rust 狀態當不透明 JSON 存）、把 judge 欄位原樣轉給 Rust | 不寫判斷規則：不決定誰付錢、能不能做、數值多少 |

- **狀態放哪裡**：
  - 新功能的狀態集中成一份 Rust 擁有的 JSON：`street_ext`，含她的物品、借還、照片紀錄、閱讀、重複約定、工作進度、節日參與。
  - C++ 只負責存讀這份 JSON 與把它附在請求裡，不解析內容。UI 要顯示時，用 Rust 的 `*_view` 規則取得排好的列。
- **介面形式**：沿用 `lib.rs eval` 的路由。每個功能一個模組或一組規則 kind（例如 `items::eval`），各有 `cargo test`。
- **提示詞的事實**：她的狀態段要加的行（擁有的物品、借出、照片、讀過的書、節日），由 Rust 產生文字（`mind_facts`），C++ 只把它附到 turn message。這樣也能保住 DeepSeek 的快取前綴。
- 其他沿用的規則：
  - 規則只記錄，判斷交給 AI。
  - 文字放 `Localization/<lang>/*.toml`（語意 id＋ICU，三種語言）。
  - 資料放 `Content/IndependentResources/StreetSocialData/`。
  - 腳本用 Lua，不新增 `.py`／`.ps1`。
- **每批的驗證**：
  1. `cargo test`
  2. 熱重載規則
  3. 打包新 `.stage`
  4. 遊戲內實測
  5. 通過才提交
  6. 結果記進 `STREET_PLAYTEST_FINDINGS.md`

## 現有狀況（2026-10-22 盤點）與要搬到 Rust 的部分

| 項目 | 現在在哪 | 計畫 |
|---|---|---|
| 共同活動表 | C++ `OKStreetSocialPlaces.h:189–293`（寫死在程式裡，含中文名） | **搬成資料** `activities.json`，由 Rust 讀取與過濾。C++ 只保留每個活動的演出資料：Anchor 錨點、clip、Code（對應 BeginPair 的姿勢） |
| 活動結束的數值 | Rust `shop.rs:335 activity_end`、`COSTS` 表 | 擴充同一處，新欄位都由 Rust 處理 |
| 品項、職缺 | `catalog.json`，C++ 與 Rust 各有 parser | 新欄位只在 Rust 用；C++ 的 parser 只保留顯示需要的欄位 |
| 他的背包 | C++ `Rules.h:346 Inventory` | 物品歸屬規則移到 Rust `items.rs`；C++ 的 `Inventory` 由 Rust 回傳的狀態同步 |
| 照片 | C++ `Scene.cpp:2249 CapturePhoto`，只記 `LastPhoto` | 拍照（截圖）留在 C++；照片紀錄、歸屬、相簿由 Rust `photos.rs` 處理 |
| 約定 | 結構在 C++ `Rules.h:270`；規則在 Rust `social.rs:374 appointments_tick` | 重複與排下一次寫在 Rust `appointments_tick`；`AddPromiseFromChat` 的日期解析與重複判定也移到 Rust（`promise_from_judge`） |
| 天氣 | Rust `plan.rs:46 raining_at` | 對出遊與活動的影響也在 Rust |
| 節日 | Rust `plan.rs:64 holiday_on`（寫死） | 改成資料 `events.json`，由 Rust 讀 |
| 開場白 | Rust `talk.rs:915`，但 C++ 決定走哪條路 | 由 Rust 決定走哪條路（`greeting_route`） |
| 導航終點 | C++ `Control.inl:329` 用 `Door` | 純空間計算，留在 C++（呈現層） |
| 菜單按鈕 | C++ `BuildShopPanel`、`/buttons` | 純 UI，留在 C++ |

## 第 0 批：共用基礎

### 0-1 活動資料與可用性（Rust `activities.rs`）

- **資料** `activities.json`：每個活動一列。
  - 欄位：`id, place（空＝就地）, kind, code, minutes, who, weather, needs, yields, once_per_day, costs, names{zh-TW,zh-CN,en}`。
  - 由一支 Lua 腳本把 `Places.h` 現有的表一次轉出。
- **Rust 規則**：
  - `activity_list {place, here, minute, day, girl, state}` → 這時候能做的活動，以及不能做的原因。原因包括下雨、缺物品、今天做過、只限某人。
    - 面板和 judge 提示詞的「可選活動」都從這裡來。
  - `activity_start {id, girl, state}` → 允許或拒絕。
    - 允許：回傳扣掉的物品與新 `state`。
    - 拒絕：回傳 `tell_her` 事實（例：「下雨了」「沒有飼料」），讓她用自己的話回應。
  - `activity_end`（擴充現有）→ 時長、數值、需求、`yields` 產出（照片、畫、工作進度、閱讀進度）、新 `state`。
- **C++**：
  - 啟動時向 Rust 取活動表（含名稱）。
  - 演出照舊：`BeginPair`／Walk／Solo、Anchor、clip。
  - 開始前問 `activity_start`，結束時送 `activity_end`，把回來的 `state` 存起來。
  - 刪掉 `Places.h` 裡寫死的活動表，只留演出對照（id → Anchor、Code、clip）。

### 0-2 她的物品、借出與送出（Rust `items.rs`）

- **狀態**（在 `street_ext`）：
  - `owned{heroine: [{item, from: bought|gift|drawn|goal, day}]}`
  - `his_borrowed[{item, owner, day, due}]`
  - `her_borrowed[...]`
- **Rust 規則**：
  - `items_checkout {cart, payer, girl}`：結帳後東西歸誰。`HerOwn` 的給她，他買給她的走送禮，他自己的進他背包。現在 `FinishCheckout` 的 C++ 判斷整段移過來。
  - `items_judge {she_gives, she_lends, he_returns, he_lends}`：judge 欄位轉成狀態變化。
    - 物品必須在當事人手上，否則拒絕並回傳原因（記 log，不改狀態）。
  - `items_daily {day}`：借期將到時，回傳 `due` 約定讓 C++ 建立；逾期天數則寫進 `mind_facts`。
    - 逾期怎麼反應交給 AI。
  - `items_view`：他的背包頁的列（我的／借來的），以及「能不能拿去送人」。
  - `saving_goals`（`bank.rs`）買到的東西改呼叫 `items.rs` 入她的背包。
- **judge**：
  - 新欄位 `she_gives`、`she_lends {item, days}`、`he_returns`、`he_lends`。
  - 三語提示詞各加說明：只填對話裡明確發生的事。
- **C++**：
  - 把 judge 欄位原樣轉給 `items_judge`。
  - 背包 UI 依 `items_view` 畫兩個分頁。
  - 收到物品時顯示一行通知（文字由 Rust 回傳的 id 查表）。

### 0-3 照片紀錄與相簿（Rust `photos.rs`）

- **狀態**：`photos[{id, file, day, minute, place, taker, people, topic, album, sent_to}]`。
- **Rust 規則**：
  - `photo_taken {file, place, taker, people, topic}` → 新增紀錄，並決定歸屬：他拍的進他的相簿；她拍的進她的相簿，若她說要給他就同時送出。
  - `photo_send {id, from, to}` → 訊息串要加的照片訊息，以及她的 `tell_her`（照片的地點、時間、誰在裡面）。
  - `photos_view {owner}` → 相簿頁的列，依相簿與日期排序。
- **judge**：新欄位 `she_photo {topic, subject: him|scene|both}`、`she_sends_photo {which: latest|topic}`。
- **C++**：
  - 拍照：從他或她的視角擺相機截圖。
  - 截好圖後把檔名送 `photo_taken`。
  - 手機加「相簿」頁（依 `photos_view` 畫）。
  - 訊息串顯示照片縮圖，可點開大圖。

### 0-4 重複約定（Rust `social.rs`）

- **約定欄位**：加 `repeat`（`weekly:<0-6>`）與 `series`。C++ 的結構加兩個欄位，只存不判斷。
- **Rust 規則**：
  - `promise_from_judge {promise, now, plans}`：把 judge 的 `{day, time, place, what, repeat}` 轉成約定或拒絕原因。日期解析、04:00 換日、公休檢查、和其他約定衝突，都從 C++ `AddPromiseFromChat` 移過來。
  - `appointments_tick`（擴充）：一次約定結束（done／missed／cancelled）時，依 `repeat` 回傳下一次的約定。
  - `promise_cancel {scope: this|all|ask}`：AI 讀不出範圍時回傳 `ask`，C++ 開一個選擇。
- **judge**：`promise` 加 `repeat`（例 `{"weekly": "wed"}`）；`cancel_promise` 加 `scope`。
- **C++**：建立或刪除約定、約定面板顯示「每週三」、開選擇。

### 0-5 控制通道看不到捲動清單外的按鈕（C++ 的 UI 修正）

- `/buttons` 包含被裁掉的按鈕，並回傳 `clipped` 旗標。點擊流程已能捲到該按鈕。
- 菜單的 `MaxHeight(260)` 依畫面高度放寬。

## 第 1 批：資料與小活動

主要是 `catalog.json` 與 `activities.json`。規則都走 0-1、0-2，C++ 不新增邏輯。

| 項目 | 資料 | Rust | C++ | 驗收 |
|---|---|---|---|---|
| 糖果 | 便利商店 `cv_candy` | — | — | 能買、能送胡桃 |
| 糰子 | 點心店 `snk_dango_tricolor` 等 2–3 種 | — | — | 雷電點得到 |
| 繪本、科普書 | 書局類別 `picturebook`、`popsci`：繪本 4–6 本、煙火圖鑑 1 本 | 書局菜單依類別分組，排序由 Rust 回傳 | 照順序畫 | 安可、惠惠找得到 |
| 小說（可讀、可借） | 書局小說分類型（推理、劍客、遊記、愛情），tag `book` | 2-4 讀書用 | — | 雷電的劍客小說能借我 |
| 素描本、畫具 | `bk_sketchbook`、`bk_paints` | 寫生的 `needs` | — | 能送、寫生會用掉 |
| 計算機、跑鞋 | 百貨 `dept_calculator`、`dept_running_shoes` | `saving_goals` 對上優香、愛麗絲的目標 | — | 存到錢會買，進她的背包 |
| 餵魚 | 活動 `park_feed_fish`（`needs park_fishfood`） | `activity_start` 扣飼料；沒有就提示攤子 | 池邊錨點、站姿 | 有飼料能開始，用掉一包 |
| 晨練／站樁 | 活動 `park_exercise`（就地，weather dry） | 數值寫在 `costs` | 沿用 `venue_stretch` | 雷電「那就開始」時就地開始 |
| 跑步 | 活動 `park_jog`（Walk，weather dry，耗體力） | 雙方體力、好感 | 沿公園步道走一圈 | 和愛麗絲跑一圈 |

## 第 2 批：有過程與產出的活動

| 項目 | Rust（規則） | C++（演出） | 驗收 |
|---|---|---|---|
| 2-1 寫生 `park_sketch` | `needs bk_sketchbook`、weather dry、60–90 分。`yields drawing` → `items.rs` 入她的背包（from: drawn），並有對應照片紀錄 | 她坐著拿畫板（prop 先用書本代）；結束時截她面向的景，套手繪風後製材質存檔 | 寫生一小時，她送我她的畫，相簿看得到 |
| 2-2 攝影散步 `photo_walk` | 選 3 個景點（依地點錨點清單與天氣）；每點由 AI 決定誰拍（judge `she_photo`）；結束建一本相簿 | 依序走點、停下、擺相機、截圖 | 三個點各一張，成為一本相簿 |
| 2-3 陪她工作 `help_her_work` | 人設 `occupation.side_task`（胡桃發傳單、優香社團帳目）；`help_work_end` 依地點人潮、天氣算張數與進度，寫進 `street_ext.work`；給 AI 的事實是「發了幾張、幾個人停下來看」 | 發傳單動作（先用遞東西手勢）、站在人多處 | 雨天進度較少，她的評價由 AI 依事實說 |
| 2-4 讀書與讀後感 | `reading[{who, book, progress, source}]`；他的「看書」與一起讀的活動 `yields book_progress`；讀完時 `mind_facts` 加「你們都讀完了《…》」；送書者的好感加成在 favor 規則 | 新的「看書」按鈕（和念書分開）、進度條 | 和胡桃讀同一本，讀完她會聊讀後感 |
| 2-5 撐傘、雨天 | 出遊開始時判斷：下雨與否、誰有傘、親密度 stage → `walk_style: umbrella_single\|umbrella_shared\|none`；都沒傘就給 AI「下雨、沒傘」的事實；雨天活動替換表 | 依 `walk_style` 換走路動作與傘 prop | 有傘時撐傘走；沒傘時她會提議去買 |
| 2-6 爆裂魔法 `megumin_explosion` | `who` 只限惠惠、一天一次、weather dry；結束時她體力歸零並回傳 `needs_support`，他可扶或背 | 詠唱 clip → 遠處爆炸特效與音效 → 她坐倒；扶她沿用 companion | 有特效，之後要扶她 |
| 2-7 接她下班 | `pickup` 到點時回傳 `change_outfit`（卡片有便服才換）與「下班」開場事實；夜路回程加「夜裡只有你們兩人」事實 | 換 outfit、從門口走出 | 三月七換回便服出門 |

## 第 3 批：校務、打工、開場、導航

| 項目 | Rust（規則） | C++（演出） | 驗收 |
|---|---|---|---|
| 3-1 學務處、系辦 | `campus_office {who, errand}`：他的選課、抵免（接 `academics.rs`）；她的助學貸款、繳學費（`bank.rs` 加女主版，只對 `tuition_by:self` 有效，記金額與還款日）。judge 新欄位 `her_errand`；陪辦算活動 `campus_ad_errand`；她有待辦校務時給 AI 事實 | `campus_ad` 的按鈕列（依 Rust 回傳）、窗口錨點 | 陪貞德·Alter 辦貸款，她的錢與還款日有紀錄 |
| 3-2 短班與課表 | `jobs[]` 加學生短班（平日 17–21、週末 9–17）；`shift_rows {day, timetable}` 回傳各班，撞課時附上課名 | 照列畫，撞課顯示提示 | 能排短班；撞課有標示 |
| 3-3 開場白依人設 | `greeting_route {girl, ai_ready, profile}` → `ai`（她自己開場）或 `catalog`（離線時用台詞庫）；先顯示的中性短句依 `profile.speech_style` 由 Rust 選 | 照路由送出；AI 回覆到了接上 | 走近雷電按 E，開場符合人設 |
| 3-4 導航終點 | —（純空間） | 終點改成門口往室內 1.5 m；`PlaceHere` 把門口 2 m 內算進去 | 導航後所在地點不是空的，店內按鈕出現 |
| 3-5 公休日寫法 | `closed_weekdays` 接受 `"mon"` 等名稱（舊數字仍可讀，0＝週一）；修正 `Jobs.inl:46` 的錯誤註解 | — | 先問使用者圖書館原意 |

## 第 4 批：節日與季節活動

| 項目 | Rust | C++ | 備註 |
|---|---|---|---|
| 4-1 節日系統 | `holiday_on` 改成資料 `events.json`（`id, date 或每年規則, hours, place, activities, invite_days`）。`events_today`：當天開放的活動、人潮加權；前幾天起在 `mind_facts` 加「X 日有 Y」，讓 AI 自己提起或邀約 | 節日裝飾、prop 開關（依 Rust 回傳的 id） | 遊戲內 9/22 中秋已過，第一個驗證用萬聖節或下一輪的中秋 |
| 4-2 煙火大會 | `fireworks_festival` 的資料與活動；judge 能把惠惠已有的約定對到這個節日（`promise_from_judge` 比對節日 id） | 煙火特效、看煙火的姿勢 | 地點等河堤或海邊場景。場景完成前，給惠惠的事實是「煙火大會在 X 月，日期未定」 |
| 4-3 夜市、海邊 | 依 0-1 加活動（小吃攤、遊戲攤、看日落、海邊散步）；夜市營業時間 `[[18,24]]` | 場景演出 | 等使用者的場景 |

## 依賴與順序

```text
0-5 按鈕（獨立，先做）
0-1 活動資料／activities.rs ─┬─ 第1批
                             ├─ 2-1 寫生、2-3 陪她工作、2-5 撐傘、2-6 爆裂魔法、2-7 接下班
0-2 items.rs ────────────────┼─ 2-1、2-4、第1批的計算機／跑鞋
0-3 photos.rs ───────────────┴─ 2-1、2-2
0-4 重複約定（獨立；先把 AddPromiseFromChat 搬到 Rust）
第3批（獨立，可和第1、2批交錯）
4-1 → 4-2、4-3（等場景）
```

建議順序：

1. 0-5
2. 0-1（含活動表搬成資料）
3. 第 1 批
4. 0-2（含 `FinishCheckout` 的物品規則搬到 Rust）
5. 0-3
6. 2-4
7. 2-1
8. 2-2
9. 2-3
10. 3-4
11. 3-2
12. 3-3
13. 0-4（含 `AddPromiseFromChat` 搬到 Rust）
14. 3-1
15. 2-5
16. 2-6
17. 2-7
18. 4-1
19. 3-5
20. 場景到位後：4-2、4-3

**搬移時的保護**：把現有 C++ 邏輯搬到 Rust 時，先寫 Rust 測試，重現現有行為與 R3 系列的修正（R3-54、R3-66、R3-70、R3-74 等），再刪 C++，避免搬的時候把修好的問題帶回來。

## 需要使用者提供的素材與決定

| 項目 | 用在 | 先用的替代 |
|---|---|---|
| 夜市、海邊／河堤、空地場景 | 4-2、4-3、2-6 | 爆裂魔法先放操場 `playground` |
| 百貨 8F 大熊、公園噴水池 | 現有 `dept_bear`、`park_photo` | — |
| 店員動作（站櫃台、搖飲料、結帳） | 女主打工時的動作 | 繼續用 `venue_shopping` |
| 畫板、傘 prop；撐傘走路與共撐動作；發傳單動作 | 2-1、2-5、2-3 | 書本 prop、拿東西手勢 |
| 爆炸、煙火特效與音效 | 2-6、4-2 | 現有閃光與粒子 |
| 圖書館公休日原意（週日或週一） | 3-5 | 維持週一 |

## 驗收總表

| 編號 | 驗收情境 |
|---|---|
| 0-1 | 下雨時寫生、跑步不在可選活動裡；judge 選了也不開始，她用自己的話說下雨了；`Places.h` 已無寫死的活動表 |
| 0-2 | 雷電借我劍客小說 → 背包「借來的」出現 → 7 天內還她；逾期她會提起；結帳物品歸屬的規則測試全過 |
| 0-3 | 三月七說「拍好了傳給你」→ 手機收到照片，相簿看得到地點與日期 |
| 0-4 | 約刻晴「每週三晚上七點圖書館」→ 下週三自動有約；取消時問這次或全部 |
| 0-5 | 便利商店菜單第 10 項以後在 `/buttons` 看得到也點得到 |
| 1 | 新品項都買得到；餵魚、站樁、跑步能就地開始 |
| 2-1～2-7 | 見第 2 批的「驗收」欄 |
| 3-1～3-4 | 見第 3 批的「驗收」欄 |
| 4-1 | 節日前幾天她會提起，當天地點開放節日活動 |

## 進度

| 批 | 狀態 | 遊戲內驗證 |
|---|---|---|
| 0-5 按鈕 | 完成 | Conquest143：便利商店菜單 19 項都在 `/buttons`（捲出去的有 `clipped` 索引）；點捲出去的「買來送人」買到水果糖 |
| 0-1 活動資料 | 完成：`rules/activities.toml`、`activities.rs`（`activities_table`、`activity_check`、`activity_blocked`、`activities_here`）；`Places.h` 的寫死表移除；錨點可寫 `a|b`；物品在活動結束時才扣（`consume = false` 不扣） | Conquest143：舊活動照常（操場伸展、結算）；Conquest150：餵魚結束時才扣飼料，寫生的素描本留著。到了地點，「這裡能做的活動」讓胡桃在校門口選到 `help_her_work` |
| 第 1 批 | 完成：品項 16 項、活動 3 項（餵魚、晨練站樁、跑步） | Conquest143：跑步繞公園 7 點，她的體力 84→68；餵魚、站樁照表結算 |
| 0-2 她的物品 | 完成：`items.rs`、存檔 `ext`、judge 5 個欄位、「還她」按鈕 | Conquest146：把遊記借給三月七（`he_lends_item`）；Conquest148：貞德·Alter 把畫送我（`she_gives`）；Conquest150：送貞德的書收下後成為她的物品，提示詞有寫 |
| 0-3 照片 | 完成：`photos.rs`；他的合照自動入相簿；`she_photo`、`she_sends_photo`；手機相簿、訊息串顯示照片；她拍照時鏡頭用她的視角 | Conquest146：三月七拍月亮並傳給我，訊息串與相簿都看得到；P 鍵合照記錄為 #2 |
| 2-4 讀書 | 完成：`reading.rs`；一起讀書的活動推進兩人的進度；背包的書可「讀一小時」 | Conquest146：讀一小時 → 16%；Conquest150：和安可一起讀，兩人都有進度 |
| 2-1 寫生 | 完成：`park_sketch`；結束時她身後的景色處理成素描，成為她的畫（`drawing:<id>`），送他時進他的相簿 | Conquest148／150：產出畫作、素描化、送出 |
| 2-2 攝影散步 | 完成：`photo_walk`，停 3 個點各拍一張，同一本相簿；從斜 40 度拍她 | Conquest151：貞德全身入鏡 |
| 2-3 陪她工作 | 完成：`work.rs help_work_end`；人設 `occupation.side_task`（胡桃、優香）；`who = ["profile:..."]` | Conquest149：和胡桃在校門口發 55 份、6 人停下，她照數字回應 |
| 3-4 導航終點 | 完成：導航到地點門口時，再走進一樓室內點 | Conquest152：到咖啡廳後所在地點是 `cafe`，店內按鈕出現 |
| 3-2 短班與課表 | 完成：學生職缺加晚班；`academics.rs class_clash` 在排班按鈕上標示衝堂 | Conquest152：「外場 17–22」，撞課的標「和《資料結構（一）》衝堂」 |
| 3-3 開場白 | 完成：`social.rs greeting_route`，AI 可用時她依人設先開口 | Conquest152：惠惠用自己的話開場，還提到我陪安可餵魚 |
| 0-4 重複約定 | 完成：`series.rs`（`series_add`、`series_due`、`series_cancel`）；judge `promise.repeat`、`cancel_scope` | Conquest155：刻晴「每週三晚上七點圖書館」→「（每週三）」；「這次先取消」只取消這次，系列保留 |
| 3-5 公休日寫法 | 完成：`closed_weekdays` 可寫 `"mon"`；圖書館改成 `["mon"]` | Rust 測試；資料讀取 |
| 2-6 爆裂魔法 | 完成：`megumin_explosion`（只限惠惠、晴天、一天一次）；遠處火球、閃光、鏡頭看向爆炸並震動；結束體力歸零 | Conquest156～158：爆炸在餐廳屋頂上方升起，她說「站不穩，扶我一下」 |
| 2-5 撐傘 | 完成：`weather.rs walk_style`；下雨時出遊撐傘（情侶共撐），沒傘時告訴她；便利商店的物品類加「買下」按鈕（之前雨傘、漫畫買不到） | Conquest158／159：買傘、和刻晴雨中走去百貨，共撐一把，她說「傘歪過來了」 |
| 3-1 學務處 | 完成一部分：`errand.rs her_errand`（`student_loan` 用貸款繳掉欠的學費，記在存檔 `ext.her_loans`；`pay_tuition` 用她自己的錢繳）、`errand_facts`；judge 欄位 `her_errand`。他的選課、抵免與 `campus_ad` 按鈕列還沒做 | Conquest160（測試存檔，日期改到 9/27 週一）：陪貞德·Alter 到行政大樓，她填表後 ¥311 學費繳清、貸款 ¥311 寫入存檔；週五 18 點約刻晴時她自己判斷學務處已下班 |
| 2-7 接下班 | 完成一部分：`walk_style` 收 `minute`，21:00–05:00 出門時給她「天色已晚，路上只剩你們兩個人」；每次出遊都問一次（不只下雨）。換便服沒做（執行中換 outfit 的介面還沒有） | Conquest160：21:02 送刻晴回宿舍，她說「路上沒人了」 |
| 4-1 節日 | 完成一部分：`rules/events.toml`＋`events.rs`；`holiday_on` 與考試週改讀資料；`events_facts` 在節日前 `announce` 天起告訴她「10/31（再 5 天）是萬聖節」。節日限定活動、裝飾還沒做（等場景） | Conquest160：`events_facts` 第 55 天、第 60 天結果正確；暫時加一個 3 天後的節日，問刻晴「這幾天有什麼特別的日子」，她答「再過三天萬聖節」（測試資料已還原） |
| 4-1 節日活動 | 完成：活動表加 `event`（中秋賞月、萬聖節逛街、聖誕燈飾、跨年倒數、情人節甜點），只在那天開放；平常不列給她 | Conquest161：暫時把萬聖節改成當天，貞德·Alter 選了 `halloween_walk` 並拍下第 1 張；還原後 `activity_check` 回「今天不是萬聖節」 |
| 0-4 約定搬到 Rust | 完成：`promise.rs promise_from_chat`（新約、同一個約再說一次、時間不清、已過、店家沒開、和別人撞期）；judge 的 `replaces` 也由它決定取代哪一個（同地點優先、再取時間最近），不再取「最早的那個」 | Conquest161／162：明天 15:00 改 16:00 → 同一個約移動；改到後天 → 明天的約被取代，今晚 22:00 接 Alter 的約還在（之前會被誤刪） |
| 3-1 她的校務事實 | 完成：欠款時她知道學費的部分能到學務處辦助學貸款 | Conquest161：三月七被問到學費時說「打算去學務處問問貸款」 |

已知問題（之後處理）：
- 發傳單時她的動作沒播出來（40 秒內 clip 沒準備好；已拿掉男方同一個 clip，待驗證）。
- （已修）和人對話時書局菜單看似沒有品項：其實有，是控制通道的按鈕探測漏掉巢狀捲動框裡的按鈕。
- judge 有時選 `go:地點`，不選該地點的活動（到了以後再說一次就會選到）。
- 存檔裡的人設是開局時複製的，舊存檔不會有 `side_task`；新遊戲由 AI 填。
- 2026-10-22 無人值守測試看到、還沒修的：
  - （已修）行程寫「街上」（地點空白）時，她被放開自由走動，人卻還在餐廳或已打烊的圖書館裡，就在裡面待好幾個小時。現在先走出那個地點的門口才放開（看不見時直接放到門口）。
  - 聯絡方式是雙向一筆：她只記下他的號碼、自己沒給時，兩邊都還不能聯絡。
  - 照「用餐」時段被放進餐廳的女孩，有的只是站在桌邊，沒有坐下吃：生活動作只有兩個動作槽（`TickLife`），只給離他最近的兩位播，其他人站著。要改得增加動作槽或讓遠的人用不佔槽的姿勢。
  - judge 偶爾在他還沒答應時就開始她提議的活動（安可邀他一起看繪本，理由寫「他還沒答應」，活動卻開始了）。

修正：judge 的回覆上限 200 token 不夠，欄位變多後被截斷、整個判斷作廢（約定沒排上），改為 600；給她的事實一律包成「（系統說明…）」，不再顯示成他說的話。

下一批：依計畫順序往下（2-7 換便服要等執行中換服裝的介面；4-2、4-3 等場景）。
