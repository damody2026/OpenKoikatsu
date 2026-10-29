# C 線「街區攻略」交接（2026-10-28，10-29 08:50 更新）

給接手的新 session。讀完這份就能繼續：玩十位女主、找出不合理的地方、用通用的方式修好、記錄、提交。

- 10-29 08:50 因使用者要整合 C⇔E 而暫停。暫停時 C 的 HEAD 是本文件這次提交。
- 攻略用 sandbox `Conq1028`（控制埠 48070，MCP 預設 47870 連不到，要用 CLI 並設 `OK_CONTROL_PORT=48070`）。
- 最後一次可用的 stage：`Saved/StreetSocial/Build/Conq1029h.stage`（base `Saved/StreetSocial/Base1028b`）。`Conq1029i` 編到一半被停掉，不要用。整合後照協調者給的新 base 重編。
- 第 4 輪、第 5 輪的發現逐列記在 `Docs/STREET_PLAYTEST_FINDINGS.md`（「攻略第四輪」「攻略第五輪」兩節，R4-n、R5-n）。
- 以下第 5～7 節的第 3 輪內容是 10-28 之前的歷史；目前狀態看第 9 節。

---

## 1. 範圍與邊界

**負責**
- C checkout：`C:\portable\OpenKoikatsu`，`master` 分支，引擎 `D:\PhysX\UE5.8`。
- 街區交流 demo 的遊玩測試和修正：對話、約定、購物、用餐、外出、多人同行、傳聞、存讀檔、電話、心情與需求、對話畫面（ok-story 立繪、鏡頭）、街區角色的烘焙動態（頭髮）。
- 修好的問題給 R3 編號，寫進 `Docs/STREET_PLAYTEST_FINDINGS.md`。自己做錯的事寫進 `Docs/STREET_ERRORS.md`：只往最新版尾端加，不要用舊版本覆蓋。

**不碰**
- `StreetSocialCity/`（使用者的另一個 UE 專案），以及 `Tools/*street_city*`、`Docs/StreetSocialCity/*`：這些是街道城市那組的。
- `OKGalleryWidget.cpp`、`OKCharacterMCP.cpp`、`OKExtractBuildStamp.h`：別人的，或建置自動改的。
- E checkout（`E:\OpenKoikatsu`）和 E 引擎。
- 別的 session 寫在 `STREET_ERRORS.md` 的條目，不要提交，也不要還原。
- 角色卡和第三方 mod 轉出的模型不進版控（commit hook 會擋）。

**協調**
- 協調者 session 會送需求和整合通知，照它的指示做。
- E 組（百人房）要量 FPS 或錄影時會說「請暫停」。這時要：
  1. 存檔、關遊戲。
  2. 停掉 ok-tts／asr／song（只停自己埠上的：遊戲 47870、tts 47860、asr 47861、song 47880）。
  3. 中止打包（TaskStop 背景工作，並清掉 D: 引擎底下自己留下的 dotnet／MSBuild 節點）。
  4. 回覆已經停了，等他們傳「測試結束」或「錄影結束」再繼續。
- 同一顆引擎不要同時跑兩個打包。自己的 Unreal 最多同時開兩個。

---

## 2. 固定規則與使用者的決定

- **規則寫在 Rust**（`Tools/ok-rules`，無狀態的 DLL，用 JSON 的 `rule` 名稱分派）。C++ 只負責畫面。
- **「規則只記錄，判斷交給 AI」**：不要用關鍵字去判斷台詞。要讀懂他說了什麼，就在 judge／reply 的 JSON 加欄位，由 DeepSeek 填，C++ 再依欄位行動、交給 Rust 換算。
- 給 AI 的提示：靜態部分放前面（快取前綴），每輪會變的狀態放在當輪訊息裡，見記憶 `deepseek-cache-prefix`。三種語言（zh-CN、zh-TW、en）的文案都要補，用語意 id 加 ICU MessageFormat（`OKT`／`OKMsg`、Rust 的 `tr::t`／`tr::m`）。
- **傳聞**：只有 10 m 內看得到的人會知道，私下 1 對 1 沒人知道；只在互為朋友的女主之間傳，而且要她們聊過天才會交換八卦（R3-241～248）。
- **多人同行**：戀人走在他兩側（最多兩位），朋友前後排成一列；只有一位戀人時就在他身邊（R3-252）。
- **取消約定只能親自打電話**（R3-268）：
  - 傳訊息、當面隨口說、她的 act，都不算取消（傳訊息時她會知道約定還在）。爽約照舊（`appointments_tick` 的 missed）。
  - 通話中說要取消：judge 的 `cancel_grace` 給 0～10 分，Rust `social.rs call_cancel` 換算成好感 -3（10 分）～-9（0 分）。
  - 用約定列表的「打電話取消」撥過去，一句話都沒說就掛斷：-15。
  - 她自己提出取消、他答應：不扣。
  - 病嬌（`traits.yandere`，或卡片性格 24）而且已經是戀人：不扣，改成她走來找他，抵達後 `BeginPrivate` 進入 1 對 1。
  - 改期（judge 的 `replaces`）不算取消，不扣。
- **心情**（R3-269）：-100～100，半衰期 6 遊戲小時，放進每輪狀態訊息和右側面板。
- **飽足上限**（R3-269）：依人設 50～1000，一般人 100。飽足度仍存 0～100，代表「胃裝了幾成」；食物依 100÷上限換算。
- **惠惠爆裂魔法**（R3-269）：
  - 放完飽足清 0。
  - 成功率依施法前的飽足度：0 時 5%，80 以上 95%。
  - 成功心情 +30，失敗 -30。
  - 她怎麼反應交給 AI。
- 規格細節見 `Docs/STREET_HEROINE_PROFILE_FIELDS.md`。
- **只提交自己的 hunk**：工作樹是共用的。部分提交的做法：複製工作檔，把別人的或未驗證的段落拿掉，用 `git hash-object -w --path <檔> <暫存檔>` 加 `git update-index --cacheinfo` 寫進 index。`STREET_ERRORS.md` 的 HEAD 版本是 CRLF，要用 `--no-filters`。
- commit 訊息不能有 BOM：用 Write 工具或 `[IO.File]::WriteAllText(..., UTF8Encoding($false))` 寫檔，再 `git commit -F`。結尾加 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。提交後 push 到 origin master。
- 使用者的決定直接照做，不反問。對話一律用繁體中文。
- 長時間無人看管也要繼續：一輪玩完就開新的一輪；等背景工作時要用一定會叫醒自己的方式（見 §4）。

---

## 3. 架構重點

### Rust `Tools/ok-rules/src`（入口 `lib.rs`，各模組的 `eval(kind)`）

| 模組 | 負責 |
|---|---|
| relation.rs | 關係紀錄。`mood_level`／`mood_minute`／`mood_at`／`add_mood`／`mood_band`，`favor_cap`，`add_liking`（依階段換算，不是好感點數），`patch` |
| social.rs | 約定 tick（`appointments_tick`：遲到、爽約、履約）、`call_cancel`、`explosion_cast`、`appointment_cancel`（舊）、傳聞、吃醋、相遇、主動行動 |
| promise.rs | 約定時間與地點解析：開門前 60 分鐘、打烊後 30 分鐘的門口都算（R3-253c、R3-260）、說一次就改期 |
| shop.rs | 結帳付款人、`order_too_full`（`OVERFULL_SATIETY` 115，依上限換算）、入場費 `entry_fee`、借錢、禮物 |
| needs.rs | 需求衰減與增加。`advance_with(…, satiety_max)`；`satiety_max(profile)` |
| plan.rs | 她的行程、自己吃飯（`meal`，依上限換算） |
| traits.rs | 人設 traits（含 `yandere`） |
| places.rs | 地點、讀檔後的同行恢復（`resume_with_him`）、隊形（`party_formation`） |
| items.rs | 東西的送、借、還（R3-262：說的是畫就不配到雨傘） |
| series.rs | 每週固定的約 |
| mind.rs / bias.rs | 心理軸、短期情緒（emotion，半衰期 2 小時，和長期心情是兩回事） |
| 其他 | activities、campus、bank、weather、photos、song… |

### ok-mind `Tools/ok-mind/src/profile.rs`
- 人設的 `Profile`／`Traits`。
- 載入時，人設欄位（`satiety_max`、`traits.yandere`）一律以 Content 那份為準。

### ok-story `Tools/ok-story`
- 對話畫面的 galgame 呈現。`portrait.rs` 是立繪（R3-263），`live.rs` 是街區對話鏡頭（R3-264 `ease_retarget`）。
- 遊戲啟動時複製一份到 `Saved/StreetSocial/Build/ok-story/` 使用，所以改了要 `cargo build` 再重開遊戲。

### ok-extract `Tools/ok-extract`
- 角色與動態骨。`dbone.rs` 的 DynamicBone 是在烘焙時跑的。
- 街區頭髮是「準備動作時用 60 Hz 烘焙成 GPU 關鍵幀」再播放（`Plugins/KKRuntimePose/.../KKRuntimePoseBank.cpp`），不是即時模擬。

### C++ `Source/OpenKoikatsu/OKStreetSocial*`（都 include 進 `OKStreetSocialScene.cpp`）

| 檔案 | 負責 |
|---|---|
| OKStreetSocialScene.cpp | 狀態結構 `FOKStreetSceneState`（大量成員都在一行）；FreeTalk 送出／回覆；**JudgeFreeTalk**（judge 回呼，所有 judge 欄位在這裡處理）；右側面板 |
| Mind.inl | SaveCalendar（存檔內容）、StartCalendarAndMind（讀檔還原）、FillMindStatus（每輪給 AI 的狀態：Needs 後面接心情）、ApplyChoiceReply（她的 act）、FeedHer、TooFullFor、MenuText |
| Schedule.inl | 每分鐘的排程：入場費判斷、讀檔後放回、購物車還原 |
| Appointments.inl | 約定：CancelPromiseWith（20 秒內去重）、CancelAppointment（`call_cancel`）、MovePromiseWith、TickAppointments、赴約 |
| PhoneInbox.inl | 手機：PhoneCall／PhoneHangUp（不說話掛斷的判斷）、CallToCancel、ReachOutByPhone、約定列表按鈕 |
| Invite.inl | 女主主動走過來（TickApproach／StepApproach）、YandereComes |
| Contacts.inl | BeginPrivate／FinishPrivate（1 對 1 互動場景） |
| Economy.inl | 菜單、購物車、結帳、同桌用餐（TickMeal）、入場費（PayEntry）、SatietyMax／MoodNow |
| Outing.inl | 外出活動、solo 活動（惠惠爆裂魔法的起點）、NextDay |
| Photos.inl | StartEffect／TickEffect（爆裂特效，失敗時只冒一小團灰煙）、AfterExplosion（她先開口說這一發） |
| Party.inl | 多人同行隊形 |
| LiveTalk.inl | 即時對話、FreeTalkPartner（通話中回傳電話那頭的人，用之前要注意）、TickThenAction |
| Encounter.inl | 傳聞、相遇 |
| Galgame.inl | 對話畫面（ok-story）的事實輸入、立繪擷取 |
| Needs.inl | 需求 tick（帶 `satiety_max`） |
| RulesGlue.inl | C++ 和規則之間的 Relation JSON 轉換（新欄位兩邊都要加） |
| Control.inl | 給 ok-game MCP 用的控制通道 |

### judge JSON 的重要欄位（`ui.scene.ai_judge_prompt`，三種語言）
- `do_now`：現在一起做的活動 id，或 `go:地點`。
  - 「走回去／送你回去」沒說地點時就是 `go:她住處`（R3-266）。
  - 在原地附近走走才是 `walk`／`stroll`。
- `then`：做完現在的事之後要做的。
- `promise {day,time,place,what,hours}`、`replaces`（改期）、`cancel_promise`、`cancel_scope`、**`cancel_grace`**（-1 或 0～10）。
- `gift`、`she_gives`、`she_lends_item`、`he_returns_item`…、`told_about`、`song_answer`、`she_photo`、`gives_number` 等。
- reply（她的回覆）JSON 有 `favor_delta`、`rel_delta` 加 `emotion`、`act`（例如 `cancel_promise`，只在通話中算）。

### 存檔
- `Saved/StreetSocial/relationships.json`（遊戲裡的 slot 另外存）。
- 關係紀錄裡的 `"mood"` 物件：`emotion`／`intensity`／`minute` 是短期情緒；`level`／`level_minute` 是長期心情。
- 讀檔會還原：和他在一起的人（`with_him`）、桌上的菜（`table`）、購物車（`cart`／`cart_place`／`cart_her`）、傳聞時刻。
- ext 裡的 `entries` 記今天已經買過的門票。

### 人設
- `Content/IndependentResources/StreetSocialData/Profiles/apng-<sha>.json`，欄位見 `Docs/STREET_HEROINE_PROFILE_FIELDS.md`。

---

## 4. 打包、啟動、測試

**exe-only 打包**（平常開發用；換新 tag，base 用整合時那份）：

```powershell
.\Tools\lua\lua.exe Tools\rebuild_street_social_runtime.lua --tag <新tag> --engine D:/PhysX/UE5.8 --base-dir Saved/StreetSocial/<整合時的 base，10-29 暫停前是 Base1028b> --skip-cook --skip-base
```

- 用 PowerShell 的 `run_in_background` 跑，輸出導到 `Saved\StreetSocial\<tag>.out`。完成時一定會收到通知。
- 等待期間繼續玩，或去寫紀錄，不要直接結束回合。
- 判斷完成要看輸出最後有 `done` 和 `Rebuild-<tag>/stage.log`。`.stage` 目錄一開始 staging 就會建立，看到它不代表完成。
- 中斷過的 tag 不要再用。`.stage` 裡有 junction，不要遞迴刪除。
- 完整的 cook 和 base 只在協調者整合 C／E 時才跑。

**Rust 測試**

```powershell
cargo test --release --manifest-path Tools\ok-rules\Cargo.toml
cargo test --release --manifest-path Tools\ok-mind\Cargo.toml
cargo test --release --manifest-path Tools\ok-story\Cargo.toml
cargo test --release --lib --manifest-path Tools\ok-extract\Cargo.toml dbone
```

- ok-rules 目前 215 個測試通過（10-29）。

**規則熱載入**
1. `cargo build --release --manifest-path Tools\ok-rules\Cargo.toml`
2. ok-game MCP：`game_call /rules {"action":"reload"}`

注意：讀檔用的是啟動時複製的那份 DLL。牽涉到讀檔、或熱載入沒生效時，要重開遊戲。

**ok-game MCP**（`Tools/ok-game-mcp`；工具沒載入時，可用 `ok-game-mcp.exe call <工具> '<JSON>'`，PowerShell 裡的 JSON 引號要寫成 `\"`）
- `game_launch` 會啟動最新的 `.stage`。
- 存讀檔：`game_call /saves {"op":"save","slot":"1"}`、`{"op":"load",...}`。注意是 `op`，不是 `action`；寫錯不會存，還會回傳上一次的 notice。
- 移動：`game_goto {girl:N}` 或 `{place:"cafe"}`。
- 說話：先 `game_key E`，再用 `game_talk` 對當面的人說話。
- 手機：`game_click "手机（N 条未读）"` → 點她的名字 → `game_phone` 打字。「打电话」是撥打，「挂断」是掛斷。
- 約定列表：`[+] 约定（N）` 展開後有「打电话取消 #id」。
- `game_state`、`game_buttons`、`game_screenshot`、`game_log`、`game_wait`（遊戲時間大約每秒 0.4 分鐘）、`+1小时` 按鈕。
- 不要用 `game_inspect`：它會進入角色檢視模式，離開時正在進行的約會、同行全部結束。
- log：`Saved/StreetSocial/Play-<時間>/game.log`。常用的 grep：`Street judge`、`Street appointment`、`call cancel`、`Street effect`、`Runtime pose (jitter|seam)`、`dbone chain`。

---

## 5. 10-27 這輪完成的 R3（commit，驗證結果詳見 FINDINGS）

| R3 | commit | 內容與驗證 |
|---|---|---|
| 235～238 | ffb1877b | 活動中約的「之後見」不算提早履約；菜單沒有的東西直說；「近來在讀的書」不是此刻在看。遊戲內驗證 |
| 238 | d33d10b2 | 讀檔後和他在一起的女主放回原處 |
| 239 | fa0bd827 | 下雨時戶外空閒改去最空的室內，不全擠進書局 |
| 240 | b25cf262 | 售貨機飲料已在菜單結帳，活動結束不再收一次 |
| 241～248 | 4b5c84f0 | 傳聞只限 10 m 內、只在朋友間、記時刻；約定說定後撤回追問；其他 |
| 249～251 | 462a2b4b | 吃醋依在乎程度；吃完後的外出不被取消；和她一起在別處也算履約（249 尚未在遊戲裡遇到） |
| 252 | 35b5f5c6、0ef45d73 | 多人同行隊形（戀人兩側、朋友前後），用實際行進方向（使用者指正過前後顛倒），依他的步速追上 |
| 253、253b、254 | f821d8bf、36264256 | 讀檔放回同行的人、桌上的菜進存檔、店開門後才開始的活動 |
| 253c | e33b7e7f | 開門前一小時內的門口可以約 |
| 255、255b、256 | a2111d58、3914bd2d | 店裡沒賣的東西直說；他買來送她的由他付；讀檔還原自由對話的對象 |
| 257～259 | 66da2f5e | 未結帳的購物車讀檔還在；手機那頭的人不算在身邊；離開時沒動過的食物打包。都在 Gest146／149 驗證 |
| 260 | 8d8f3d7a、fd6dd13b | 店剛打烊的門口（接她下班）可以約。實際 22:00 接到惠惠 |
| 261 | d7a643d4 | 判定讀出的借錢不再等她下一句（Gest150 已編入，還沒遇到有人接受借錢） |
| 262 | d7a643d4 | 送畫不會被當成送傘。熱載入後驗證 |
| 263、264 | 6fafb778 | ok-story 立繪不跟骨盆扭（cargo 驗證：擺動 <2°）；換說話者時鏡頭平滑（二階抖動從 60 cm 降到 2 cm）。遊戲裡看過 |
| 265（接縫） | 8aa31eef | 循環動作烘焙時先空跑一輪。接縫警告從 11 筆降到 4 筆，最大倍數從 136 降到 21 |
| 266 | 698069fb | 「走回去」判成 go:住處。Gest152 驗證 |
| 268 | 8aa31eef | 取消只能打電話。四種情況都在 Gest157 驗證：訊息不算、敷衍扣 9、不說話掛斷扣 15、病嬌戀人 0 並來 1 對 1 |
| 269 | 8aa31eef | 心情、飽足上限、爆裂魔法。成功分支已驗證（95% 成功、飽足清 0、她先開口、面板「心情 不錯」），**失敗分支還沒看到** |

更早的 R3（203～234）見 `git log`，以及 FINDINGS 裡 R3-203 以後的列。

---

## 6. 未提交的四項（已編進 Int1027C，工作目錄 `git status` 可以看到）

1. **R3-265 三月七頭髮逐幀抖動**：`Tools/ok-extract/src/dbone.rs`
   - 已改：
     - 骨鏈建立時記錄更新頻率低於 60 Hz 的骨鏈（log 「dbone chain … steps at 40 Hz … skip-fix X」）。
     - `dynamic_bones_fix_enabled`：從基礎庫開 OK 卡、又沒有遊戲安裝目錄時，當成有 KKS_Fix_DynamicBones（記錄「dbone fix for …: library …, on …」）。
   - 現象：三月七的 `conversation_wait_04` 有 21 根骨頭在 241 幀裡 92 幀方向反轉（`__ok_palette_68_*`、`67_*`、`0_81_0`）。找到的原因是兩條 40 Hz、damping 0、stiffness 0.7 的骨鏈（joint1-1／joint2-1）在 60 Hz 烘焙裡每 3 幀有 1 幀走 skip。
   - 套用修正後在 Gest157 還是 92 幀，修正可能沒有生效。
   - 驗證步驟：
     1. 用 Int1027C 啟動。
     2. 和三月七說話，讓她的 conversation_wait_04 被烘焙。
     3. grep `dbone fix for`（看 `on` 是 true 還是 false）、`skip-fix`、`Runtime pose jitter: … conversation_wait_04`。
   - 如果 skip-fix 已經是 true 還在抖：改成在烘焙裡對低頻骨鏈做 step 之間的插值，或把它換算成 60 Hz 的係數。
   - 另外，男主的 `lover_hand_male` 也有 48 根骨頭的抖動警告（`__ok_palette_0_80_*`），可以一起看。
2. **R3-267 水族館門票重複收費**：
   - 檔案：`OKStreetSocialSchedule.inl`（EntryHer 只在有同行女主時更新）、`OKStreetSocialScene.cpp`（`EntryHerPlace`）、`OKStreetSocialEconomy.inl`（入場 log 加 day／actor／分鐘）。
   - 現象：胡桃在門口會合時買了 ¥74，開始看魚時又買一次。
   - 驗證：12/5 10:00 和安可的水族館約（#118）。log 裡 `Street entry: aquarium` 應該只有一筆。如果規則端又收了一次，看新的 day／actor 欄位。另外規則端（shop.rs `entry_fee` 的 `entries`）為什麼沒擋下第二次，原因還沒查到。
3. **R3-268 補充：病嬌通話好感還原**：
   - 檔案：`OKStreetSocialPhoneInbox.inl`（PhoneCall 記下 `CallStartFavor`）、`OKStreetSocialAppointments.inl`（`Comes` 時還原）、`OKStreetSocialScene.cpp`（欄位）。
   - 驗證：
     1. 暫時在 Content 的某位戀人 profile 的 traits 加 `"yandere": true`，先備份，驗完還原，不要提交。
     2. 用「打電話取消」撥給她，取消她的約。
     3. 看 log「favor back to」，好感應該維持在通話開始時的值。
4. **R3-270 通話中跳出「沒接到電話」的訊息**：`OKStreetSocialPhoneInbox.inl`（PhoneCall 接通時 `PendingTexts.RemoveAll(missed_call)`）。
   - 驗證：撥給一位女主，沒接；再撥一次接通。通話中不應該收到她「剛才沒接到你電話」的訊息。

**R3-269 失敗分支怎麼觸發**
1. 去找惠惠（actor 9），要在晴天（雨天不能放）。
2. 打開「女主數值（查看／修改）」面板，把她的飽足調到 0～10，這時成功率大約 5～16%。或者趁早上早餐前、飽足低的時候。
3. 邀她「去操場放爆裂魔法」（活動 `megumin_explosion`，一天一次，slot 1 的 11/28 已經放過了）。
4. 看 log「explosion by actor=9 fizzles」。畫面應該只冒一小團灰煙，沒有火球、閃光和震動；她先開口說失敗；右側面板顯示「心情 低落」或「很差」。

---

## 7. 存檔與目前進度

- **slot 1**：2010/11/28（日）19:44，¥202，在操場和惠惠一起（她剛放完爆裂魔法，飽足 3）。
  - 下一步：帶她去吃晚飯。學生餐廳週末只開到 14 點，要去餐廳或便利店。
- slot 9、10 是測試用的存檔；slot 2～8 是使用者的舊檔，不要覆寫。
- **已排好的約定**：
  - #122：12/1（三）18:00 早濑优香，咖啡廳吃蛋糕（他請）。
  - #120：12/4（六）15:00 雷电将军，點心店找新開的和果子店（答應過她「只有我們兩個」）。
  - #118：12/5（日）10:00 安可，水族館門口（她想先看水母柱；順便驗 R3-267）。
  - 三月七 12/4、胡桃 12/1、貞德·Alter 11/30 的約都在測試 R3-268 時取消了。和胡桃的書局之約可以重新約。
  - 答應雷電將軍「每天晚上傳訊息告訴她一天的事」。
- **關係**（2026-10-27 結束時）：

  | 女主 | 關係 | 好感 | 備註 |
  |---|---|---|---|
  | 惠惠 | 戀人 | 150 | |
  | 安可 | 戀人 | 150 | |
  | 爱丽丝 | 戀人 | 148 | |
  | 雷电将军 | 戀人 | 149 | |
  | 胡桃 | 戀人 | 147 | |
  | 貞德·Alter | 朋友 | 101 | 被測試掛斷扣了 15 |
  | 早濑优香 | 朋友 | 91 | |
  | 貞德 | 朋友 | 84 | |
  | 刻晴 | 朋友 | 47 | |
  | 三月七 | 朋友 | 6 | 測試敷衍取消扣 9，加上她自己的反應，需要修補關係 |

- **這一輪還沒好好玩到的**：早濑优香（只打過招呼，12/1 有約）、貞德·Alter（只打過電話）、雷电将军（這輪只傳訊息和當面道歉，12/4 有約）。

---

## 8. 踩過的坑

- **commit 訊息有 BOM**：不要用 PowerShell 的 `>` 或 Out-File 寫訊息檔，用 Write 工具，或 `[IO.File]::WriteAllText` 搭配 `UTF8Encoding($false)`。
- **toml 是 CRLF**：用 regex 改時 `.*` 會吃到 `\r`，留下單獨的 CR，git 就把整個檔案當成二進位。只用精確字串取代，寫回時保留原本的 BOM 設定。
- **熱載入的規則 DLL**：讀檔時用的是啟動時複製的那份。ok-story 也一樣，要 `cargo build` 再重開遊戲。
- **測試者自己造成好感下降**：
  - 對話中長時間不說話（每段 `game_wait` 盡量不要超過 15～20 秒，每次都要回話）。
  - 編造沒發生的事：例如說「幫三月七挑參考書」，實際是繪本；或說「有烤紅薯攤」，其實沒有。
  - 前後矛盾：說「今天整天都是你的」，下午又去赴別人的約。
  - 這些都是測試者的錯，不是遊戲的問題。
- **`game_goto` 走向正在和你說話的女主**，會先結束那段對話。
- **E 鍵對到別人**：E 對到的是最近的人，要用 Tab 切換對象。剛讀檔、角色還沒全部載入時，移動和 E 都不會反應。
- **`game_inspect`**：會進入角色檢視模式，離開時約會和同行全部結束（見 `STREET_ERRORS.md`）。
- **`FreeTalkPartner()`** 在通話中或傳訊息時回傳電話那頭的人。拿它判斷「誰在身邊」要先排除 `RemoteMode`（R3-258 就是這樣引起的）。
- **judge 回呼是非同步的**：需要「當時是不是在通話中」這類狀態時，要在送出前記下來帶進回呼（`InCall`）。
- **判斷打包完成**：看輸出的 `done`，不要看 `.stage` 目錄；等待時要有通知能叫醒自己，曾經兩次被協調者看到停著沒動。
- **烘焙快取**：KKRuntimePose 的快取在 `UserSettingsDir/OpenKoikatsu/RuntimeGpuCache/v1`，key 含 recipe identity 和建置戳記。改了烘焙方式要讓 recipe identity 跟著變，例如 `loop-preroll`。
- **Relation 的 `add_liking`**：會依關係階段換算，不等於好感點數。要扣好感點數就直接改 `favor`，並限制在 `favor_cap()` 以內。
- **只殺自己埠上的程序**：遊戲 47870、ok-tts 47860、ok-asr 47861、ok-song 47880，以及自己 D: 引擎建置留下的 dotnet。別人的遊戲、UnrealEditor 和 E: 引擎底下的程序都不要動。sandbox `Conq1028` 用的是 48070（控制）、48060（ok-tts）、48061（ok-asr）。
- **Unreal 名額**：`Saved/UnrealSlots/slot1|slot2`（mkdir＋owner.txt），排隊寫 `queue/<名>.txt`，先來先用；只在真的開 Unreal 時持有，約 60 分鐘或做完就放；打包（exe-only rebuild）不必占名額，但要先關掉自己的遊戲。
- **語系 toml 裡插英文句子**：雙引號要寫成 `\"`，否則整份 ui_text 解析失敗、遊戲 5 秒後自己結束（R4-15 時踩過，見 STREET_ERRORS）。用 sed 插多行會被併成一行，多行用 Edit。
- **在既有長函式加 `if(init;cond)` 變數**：先 grep 同名區域變數，打包建置會把遮蔽（C4456）當錯誤。
- **只提交自己的 hunk**：用暫時 index（`GIT_INDEX_FILE`）；同一檔有別人的或未驗證的段落時，用 `git diff -U0` 挑 hunk 再 `git apply --cached --unidiff-zero`；FINDINGS／STREET_ERRORS 用「HEAD 內容＋要加的列」組 blob 再 `git update-index --cacheinfo`。STREET_ERRORS 的 HEAD 有 CRLF 行，組 blob 時不要對 HEAD 那份 `tr -d '\r'`。

---

## 9. 第 4、5 輪（10-28～10-29）與暫停時的狀態

### 第 4 輪（新遊戲，sandbox Conq1028）：五位戀人，不乾淨
- 9/13 三月七、早濑优香、安可、惠惠、胡桃成為戀人。終局存檔：**欄位 6**（9/13 19:30）。
- 已提交（都在 `git log`）：R4-1、2、3、5、6、7、8、9、10、11、15、16、17、18、22、27（規則部分）。
- 只觀察、沒修：R4-4、12、14、20（照 R3-268 使用者決定）、23、26、29。

### 第 5 輪（10-29 04:05 新遊戲，同 sandbox）：進行中
- 起點存檔 **欄位 8**（9/1 08:11）；目前進度 **欄位 7**（9/8（三）07:03，¥2,390）。
- 這輪先攻略第 4 輪沒成為戀人的五位。關係：雷电将军 戀人（9/5）；爱丽丝 朋友；刻晴 朋友；贞德·Alter 陌生人 40；贞德 陌生人 14。
- 已排好的約：9/8 10:00 贞德·Alter 百貨門口挑耳機；9/8 16:00 雷电将军 點心店吃三色團子。爱丽丝已交換手機。
- 已提交：R5-1、R5-2、R5-5、R4-11（第 5 輪驗證）。只觀察：R5-3、R5-4。

### 已提交但「未驗證」的修正（暫停時一起提交，見該次 commit 訊息）
| 編號 | 改了什麼 | 怎麼驗 |
|---|---|---|
| R4-13 | 這裡還在做的點單時，判斷說的別處喝／吃活動當成喝這裡點的，不出發（Scene.cpp） | 在非點心店的地方點飲料、判斷給 do_now=drink；log「is what they ordered here」 |
| R4-19 | 帶手勢開口失敗時改成只播語音，不中止整段對話（Scene.cpp） | log「gesture not queued」後對話繼續 |
| R4-21 | 她上班中，聊天點單裡給她自己的那項不點（Mind.inl） | 女主在店裡上班時替自己點；log「she is on her shift」 |
| R4-24 | 跳到隔天時清掉還在做的點單與桌上東西（Mind.inl） | log「cleared overnight」 |
| R4-25 | 沉默催話提示加「決定先走就填 end_talk」（三語 ui_text） | 同行時沉默，她說要走後應該真的離開 |
| R4-27 (3) | 判斷提示：只有列出的才算已有約定（三語 ui_text） | 被刪掉的約，她再答應一次要重新記下 |
| R4-28 | 聊天點單因店裡沒賣被拒時告訴她（Mind.inl＋三語 ui_text `not_sold_here_order`） | log「order rejected」後她照實說沒賣 |
| R5-6 | 帶著的點單在他已到那家店時就下單；道別時清掉（Economy.inl、LiveTalk.inl） | log「they are there already」 |
| R5-7 | 餐點上桌時撤掉排著的「還沒上桌」提示（Economy.inl、Scene.cpp 欄位） | 上桌後她不再說「好像還有東西沒來」 |

### 恢復後的下一步
1. 用協調者給的新 base 重編 exe-only（新 tag），開 sandbox `Conq1028` 讀欄位 7。
2. 先開遊戲聽一次 10 位女主的聲音：10-29 已把 StreetHeroines 10 張卡補上 okVo（備份在 `Saved/CardBackup/voice-20261029-024537`，okId 不變，去掉 okVo 後與備份逐位元組相同）；惠惠的歌用的是她卡上的 tag，其他 9 位只對過 tag。
3. 赴 9/8 兩個約，繼續第 5 輪到五位戀人；途中驗證上表，驗過就在 FINDINGS 該列補結果。
4. 第 5 輪仍有問題就開第 6 輪，直到一輪完全乾淨（記憶：conquest-loop-until-clean）。
