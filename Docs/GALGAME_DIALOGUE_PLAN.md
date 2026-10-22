# Galgame 對話演出計畫（左下立繪、逐句運鏡、gal 系統）

2026-10-22，E 線（`duo2`，OK Studio）。研究對象：`https://github.com/2396846264/-vrm-galgame-editor`（three.js＋WebView2 的 VRM／FBX／MMD 視覺小說編輯器）。

**該倉庫沒有授權條款，只參考做法與數值，程式碼全部依 OK 架構自己寫。**

### 實作狀態（2026-10-22）

使用說明見 [GALGAME_STORY_USAGE.md](GALGAME_STORY_USAGE.md)。

| 階段 | 狀態 | 驗證 |
|---|---|---|
| G0 模組骨架、C ABI、熱換 | 完成：`Tools/ok-story`、`UOKStoryPlayer`（`OKStory.Reload` 換 DLL 時保留狀態） | `cargo test`；UE 載入 |
| G1 劇本、推進、選項、物品 | 完成 | `tests/director.rs`；示範劇本自測走完選項分支 |
| G2 對話框、打字、自動、快進 | 完成（`SOKStoryView` 自繪） | 單元測試；1600×900 截圖 |
| G3 語音同步打字 | 完成（語音檔；`ok-tts` 未接上） | 單元測試；實際 ogg 播放時截到一半的字 |
| G4 左下即時立繪 | 完成：正交擷取 LDR 顏色加 HDR 反向不透明度，在 UI 材質裡合成 | 截圖：透明背景、場外說話者的分身立繪 |
| G5 逐句運鏡 | 完成，另加深度探測避牆：每個自動鏡頭有多個候選位置，C++ 用 8×8 深度擷取回報視線距離 | `shots_avoid_walls` 測試；1901 場景各鏡頭截圖 |
| G6 幕標題、表情、眨眼與視線 | 完成（換動作直接切換，沒有 crossfade） | 20 人 5 分鐘模擬測試 |
| G7 存讀檔、Backlog、設定 | 完成（縮圖用場景擷取，不含 UI） | 自測依序打開存檔、讀檔、紀錄、設定、選單並截圖 |
| G8 編輯 API 與 MCP | 完成：`edit.rs`、`ok-story-mcp`（劇本工具，加上透過 control 資料夾操作執行中的遊戲） | 單元測試；`game_state`／`game_screenshot` 對執行中的遊戲實測 |
| G8 Scene Editor 劇本面板 | 未做：目前用 F8 相機編輯加 MCP 代替 | — |

以下是原始計畫內容。

---

## 0. 目標與範圍

在 OK Studio 場景上播放一段劇本，畫面呈現：

- 底部全寬的對話框，以名牌＋打字效果顯示，打字進度和語音同步；
- 說話者的**即時 3D 立繪**疊在左下，與場上角色同一份動作、表情與嘴型；
- 每句可指定相機，句與句之間平滑運鏡；
- 選項分支、物品條件、存讀檔（含縮圖）、Backlog、自動播放與已讀快進；
- 角色會自動眨眼、自然移動視線，多人同場時錯開，避免同步。

不做：

- MP4 過場、小遊戲節點、新聞或宣戰事件版面；
- 多套 UI 主題（只留樣式表接口）。

舞台：第一版只用 OK Studio 場景（`.okscene`），街區地圖之後再說。

---

## 1. 參考作品的機制（自己整理）

### 1.1 版面
- 對話框貼底、全寬，高 26%；名字與文字從左側 27.5% 開始。
- 立繪區從左緣開始，寬 54%、高 68%，底部對齊，畫在對話框**之上**；角色略側身朝名字那一側。
- 每個角色有名字顏色。

### 1.2 即時立繪
- **拍攝方式**：用第二台正交相機拍場上同一個角色，所以動作、表情、嘴型、眨眼、視線自然一致，不建第二個模型。
- **取景**：
  - 角色第一次出現、套用劇本動作之前，記下「胸部支點 → 頭」的相對位移和 hips 的靜止朝向。
  - 之後每幀只取 hips 相對靜止朝向的**水平 yaw**與支點位置，不抵消頭頸轉動，所以點頭、轉頭會留在立繪裡。
  - 相機位置用角色空間的偏移表示：往側面約 0.85、往上 0.05、往前 1.7（VRM 單位為公尺，乘以角色縮放），看向頭部上方約 0.03；可視寬度約 0.4，高度依立繪框比例向上延伸以容納頭髮。
  - 作者可微調 offsetX、offsetY（±100% 畫面寬高）與 scale（0.25～3）。
- **合成**：只開啟角色與燈光所在的圖層，背景清成透明，寫入小 render target 後疊到 UI 上；下緣疊一層由透明漸變為對話框底色的漸層（約從 50% 高度開始），讓立繪融入對話框。
- **說話者不在場**：用一組離場動作與表情（預設原地循環）驅動立繪。

### 1.3 劇本資料
- **幕**：名稱、場景、初始相機、句子列表。
- **句**：
  - 說話者、文字、語音、音效；
  - 動作與選項、表情權重；
  - 相機（可空）；
  - 站位：整個舞台共 30 格（3 欄 × 10 排），每人有縮放、XYZ 偏移、yaw、pitch、道具，同一角色只能佔一格；
  - 選項：跳轉幕、物品條件、是否扣物品；
  - 物品發放。
- **相機繼承**：往前找最近一句有相機的 → 幕的初始相機 → 場景預設相機。

### 1.4 運鏡
- **移動時間**：
  - `秒數 = clamp(距離/4 + 角度/π × 0.7 + |ΔFOV|/100, 0.35, 4) / clamp(速度, 0.85, 1.15)`，距離以公尺計。
  - 位置與 FOV 線性插值、旋轉 slerp，進度套 smoothstep。
  - 位移、角度與 FOV 都幾乎沒變時直接設定，不做移動。
- **同幕與換幕**：同一幕內平滑移動，換幕或重新進入時直接切。
- **跳過**：點擊或前進時，進行中的移動直接跳到終點。
- **編輯**：
  - 操作：WASD 前後左右、QE 上下，滑鼠轉視角，Shift ×3、Alt ×0.25。
  - 確定與取消：左鍵確定，Esc 還原。
- **空間感視差**：滑鼠位置換成 −1..1，以 `1−e^(−8·dt)` 平滑追隨，相機暫時平移約 4×2.5 cm、旋轉約 1.2°×0.8°（乘強度），只在渲染當下套用。

### 1.5 演出細節
- **幕標題**：460 ms 帶回彈地從左滑入（cubic-bezier .16,1.18,.3,1），停 800 ms，340 ms 滑出；播完才開始打字、語音與獎勵。
- **語音同步打字**：
  - 每 25 ms 依 `已播秒數 / 總長` 算出顯示字數；
  - 「正在發聲」才驅動說話嘴型；
  - 語音結束時顯示全文，並排程自動前進；
  - 語音失敗時改用固定字速。
- **自動眨眼**：
  - 每人各自計時，6～12 秒一次；閉合快、張開慢；
  - 動畫本身有眨眼時讓位，並重新計時；
  - 不允許眨眼時延後 0.2～1 秒再試；
  - 整個場景同時最多一人。
- **自然視線**：
  - 每 8～16 秒小幅看向別處，停 0.45～1.2 秒；
  - 偶爾看同場其他人的頭 1～1.8 秒，每人冷卻 45～75 秒；
  - 上限：水平 13°、垂直 6°；排除自己、身後與太遠的人；
  - 同時最多兩人，起始時間錯開至少 1.2 秒並加隨機延遲；
  - 原動作有注視目標時讓位；
  - 重新入場重新計時；暫停時計時也暫停。
- **存檔縮圖**：截圖前隱藏 UI，只拍遊戲畫面，JPEG 有大小上限。

---

## 2. E: 現有部件盤點（2026-10-22 核對）

| 需求 | 現有 | 位置 | 缺口 |
|---|---|---|---|
| 場景與角色 | `UOKStudioGame`（Document、SceneActor、Viewer）、`AOKSceneActor`、`AKKPreviewActor` | `Plugins/OKStudio/Public/OKStudioGame.h`、`OKSceneActor.h` | 沒有劇本播放模式 |
| 文字 UI | Studio 用 Slate（`SOKStudioPanel`）；街區用 Slate＋`SOKStreetSpeakerText`；Interaction 用 C++ UMG | `OKStreetSocialSpeakerText.h`、`OKInteractionScene.cpp:108` | 沒有名牌、打字、gal 版面 |
| 語音 | `PlaySpeechFile`／`PlaySpeechPcm`／`IsSpeaking`、`SetSpeechPaused`；`FKKSpeechMeter{Rms, RenderedFrames}`；`Tools/ok-tts` | `KKPreviewActor.h:274-305`、`KKSpeech.h:16-23` | 沒有對外提供播放進度（`RenderedFrames / 總 frame` 可算出） |
| 嘴型 | RMS 驅動，`bSpeechDrivesMouth`、`SetMouthOpen` | 同上 | 無 |
| 透明擷取 | `OKRenderStudio` 的 LDR 顏色 ＋ HDR `1−A` coverage；`OKCardPreviewCapture` 的 ShowOnly 正交擷取 | `OKRenderStudio.cpp:64-104`、`OKCardPreviewCapture.cpp:167` | 目前是 CPU 讀回，不能每幀做 |
| 相機 | Studio 的 Camera 物件、`ActiveCamera`、timeline ease／Catmull-Rom；`Viewer` 與 orbit 參數 | `OKSceneDocument.h:348,657` | 沒有逐句相機與自動算時間的插值 |
| 表情 | `SetFacePattern`、`SetMouthPattern`、`SetFaceControl`；街區名稱查表 `FOKStreetExpressions` | `KKPreviewActor.h:316-362`、`OKStreetSocialExpression.h` | 沒有「劇本表情名 → pattern」表 |
| 眨眼與視線 | `SetAutomaticBlink`、`ForceBlink`、`SetEyeLookTarget(世界座標)`、`FollowEyeLookCamera`、`SetEyeGaze` | 同上 | 沒有多人錯峰；看人要自己取頭骨位置 |
| 邏輯 DLL 的先例 | `Tools/ok-rules`（`ok_rules.dll`，C ABI＋JSON、`panic = "unwind"` 由每個入口捕捉；UE 端 `FOKStreetRules` 以 `GetDllExport` 載入，`ok.StreetRules.Reload` 複製到 `reload/` 後熱換） | `Tools/ok-rules/Cargo.toml`、`Source/OpenKoikatsu/OKStreetRules.cpp` | 街區專用，不放劇本邏輯；新模組照這套載入與熱換方式 |
| 存檔與 log | 街區 8 欄存檔、自由對話 log 面板 | `OKStreetSocialSaves.inl`、`OKStreetSocialScene.cpp:2134` | 沒有縮圖、自動播放、已讀 |

---

## 3. 架構

**分工：所有劇本與演出邏輯放在新的 Rust 模組 `Tools/ok-story`（`ok_story.dll`），C++ 只做呈現。** C++ 不做任何判斷、計時、插值或隨機，只做三件事：

1. 把事實回報給 Rust；
2. 照 Rust 回傳的畫面描述去畫；
3. 執行 Rust 下達的指令，並回報結果。

```text
                 ┌──────────── Tools/ok-story（Rust，ok_story.dll）────────────┐
.okstory.json ──►│ script    解析、驗證、繼承合併（相機／站位／表情）           │
tuning.json   ──►│ progress  幕/句推進、選項、物品、旗標、已讀、Backlog         │
faces.json    ──►│ text      打字時鐘（字速、標點停頓、語音進度、TTS 切換）     │
                 │ flow      自動播放、快進、幕標題時間軸、淡入淡出、等待條件   │
                 │ camera    逐句相機、預設鏡頭計算、運鏡插值、跳過、視差       │
                 │ portrait  立繪取景（支點、yaw、偏移、正交範圍）、換人淡入淡出 │
                 │ life      多人眨眼/視線排程（可重現的種子 RNG）              │
                 │ save      存檔內容、hash 比對、讀檔還原、設定                │
                 └──────────────▲───────────────────────────┬─────────────────┘
                   事實 Facts    │ C ABI（JSON，UTF-8）        │ 畫面 Frame ＋ 指令 Commands
                   輸入 Input    │                            ▼
UOKStoryPlayer（UE C++，只做呈現；掛在 UOKStudioGame 上）
  ├─ 收集事實：dt、視窗尺寸、說話者與在場者的骨骼世界座標、語音進度/結束、場景載入完成、動作就緒
  ├─ 轉送輸入：點擊、按鍵、選項按下、選單操作（不解讀，只轉成事件）
  ├─ 執行指令：載入場景、放置/隱藏角色、播放動作、套表情 pattern、ForceBlink、SetEyeLookTarget、
  │            播放語音/TTS/SE、生成/釋放場外分身、拍存檔縮圖、寫縮圖檔
  ├─ 套用畫面：Viewer 相機姿勢、立繪 SceneCapture 的變換與正交寬、各 UI 元素的文字/透明度/位置
  ├─ UOKLivePortrait   SceneCapture＋合成材質（只照 Rust 給的變換拍）
  └─ SOKStoryOverlay   Slate：對話框、名牌、打字、選項、幕標題、選單、Backlog、存讀檔畫面
```

- **原則一：邏輯一律在 Rust**（遵循 `street-logic-in-rust`）。C++ 裡若出現 `if`，只能用來判斷「指令要套到哪個元件」，不能決定劇情、時間或數值。所有可調數值都在 Rust 讀的 `tuning.json`。
- **原則二：可重現**。Rust 的時間只來自 C++ 回報的 `dt`，隨機只來自存檔裡的種子，所以同一串 Facts/Input 一定得到同一串 Frame/Commands。這讓大部分驗收可以在 `cargo test` 裡用錄好的 Facts 重播完成，不必開 Unreal。
- **原則三：可熱換**。照 `ok-rules` 的方式：C++ 把 DLL 複製到 `reload/` 再載入。換版前呼叫 `oks_export`，把 director 狀態序列化，再由新 DLL `oks_import` 還原，遊戲不必重開（`street-iterate-hot-reload`）。
- **原則四：UI 用 Slate**，和 `SOKStudioPanel`、街區 UI 一致。Slate 元件只是「Rust 畫面描述」的顯示器。
- **原則五：文字放在 `Localization/<lang>/*.toml`**，以語意 id 加 ICU 訊息格式；劇本台詞可用 id 引用，或以 `text.ja`／`text.zh` 內嵌（給 AI 生成的劇本用）。語言選擇與 ICU 格式化在 Rust 端完成，C++ 收到的是最終字串。

### 3.1 為什麼是新模組，不放進 `ok-rules`
- `ok-rules` 是街區規則，無狀態，每次呼叫都帶齊事實；劇本播放需要逐幀的時鐘與插值狀態，性質不同。
- 獨立 crate 讓 E 線（OK Studio）與 C 線（街區）可以分別修改，不會在同一份 Cargo 專案裡衝突。
- 兩邊共用的部分（ICU 格式化、語系載入）抽成 `ok-rules` 裡已有模組的 path 依賴，不複製程式碼。

### 3.2 C ABI（`Tools/ok-story/include/ok_story.h`）

| 函式 | 用途 |
|---|---|
| `oks_abi() -> u32` | ABI 版本；C++ 不符就拒絕載入 |
| `oks_open(config_json) -> handle` | 建立 director：劇本路徑、tuning、faces、語言、存檔目錄、種子 |
| `oks_input(handle, input_json) -> bool` | 玩家輸入事件（排入佇列，下次 tick 處理） |
| `oks_tick(handle, facts_json) -> frame_json` | 每幀一次：推進時鐘，回傳畫面與新指令 |
| `oks_ack(handle, result_json) -> bool` | 指令執行結果（成功/失敗、語音長度、場景就緒等） |
| `oks_query(handle, query_json) -> json` | 唯讀查詢：存檔列表、Backlog、劇本驗證報告、`story state`（控制通道用） |
| `oks_export(handle) -> json`／`oks_import(handle, json)` | 熱換與除錯快照 |
| `oks_free(str)`、`oks_last_error() -> str`、`oks_close(handle)` | 記憶體與錯誤 |

- 每個入口都以 `catch_unwind` 包住；錯誤以回傳值加 `oks_last_error` 表示，不會讓 Unreal 崩潰。
- JSON 先求簡單。每幀 Frame 約 1～3 KB；若 `oks_tick` 加上解析超過 0.1 ms，就把相機、立繪、文字長度這類每幀數值改成 `#[repr(C)]` 結構，指令仍走 JSON。

### 3.3 Facts／Frame／Commands 的內容

**Facts（C++ → Rust，每幀）**
```json
{ "dt": 0.0167, "viewport": [1920,1080], "dpi": 1.0,
  "actors": { "ayu": { "present": true, "root": {"pos":[..],"rot":[..],"scale":1},
                       "bones": { "head":{"pos":[..],"rot":[..]}, "spine03":{..}, "hips":{..}, "eye":{"pos":[..]} },
                       "authored_blink": false, "authored_look": false, "face_closed": false } },
  "voice": { "line": "a1.l1", "state": "playing", "progress": 0.42, "duration": 3.1 },
  "camera_editing": null }
```
骨骼只回報 Rust 需要的幾根（`cf_j_head`、`cf_j_spine03`、`cf_j_spine02`、`cf_j_hips`、兩眼中點）。

**Frame（Rust → C++，每幀）**
```json
{ "camera": {"pos":[..],"rot":[..],"fov":32.5,"moving":true},
  "portrait": { "visible": true, "who": "ayu", "alpha": 1.0,
                "capture": {"pos":[..],"rot":[..],"ortho_width":41.2}, "rect": [0,0.32,0.54,1.0], "fade_from": 0.5 },
  "dialog": { "visible": true, "name": "小鮎", "name_color": "#ff7aa8", "text_visible": "……你來", "shown": 4,
              "waiting": "click", "auto": false, "skip": false },
  "choices": [ {"text":"好啊","enabled":true,"hint":""}, {"text":"用鑰匙開門","enabled":false,"hint":"缺少：舊鑰匙 ×1（現有 0）"} ],
  "title": { "text": "第一幕　屋頂", "x": -0.12, "alpha": 0.8 },
  "layout": { "dialog_h": 0.26, "text_left": 0.275 },
  "commands": [ ... ] }
```
`text_visible` 是 Rust 依 grapheme 切好的字串，C++ 直接顯示；`shown` 是字數，給控制通道與測試用。

**Commands（Rust → C++，有 id，執行後 `oks_ack`）**

| 指令 | C++ 的動作 | ack 回報 |
|---|---|---|
| `load_scene{path}` | `UOKStudioGame::Load` | 成功與否 |
| `place{who, pos, rot, scale, visible}` | 在 `AOKSceneActor` 上放置或隱藏角色 | 角色就緒 |
| `motion{who, id, blend}` | 播放 OK Studio 動作（CrossFade） | 就緒或失敗 |
| `face{who, eyebrow, eyes, mouth, blush, tears}` | `SetFacePattern` 等 | 不需要 |
| `blink{who}`、`auto_blink{who, on}` | `ForceBlink`／`SetAutomaticBlink` | 不需要 |
| `look{who, target:{pos}|camera|none}` | `SetEyeLookTarget`／`FollowEyeLookCamera`／`StopEyeLookTracking` | 不需要 |
| `voice{who, file|tts, pitch}`、`stop_voice{who}` | `PlaySpeechFile`／TTS 後 `PlaySpeechPcm` | 開始播放與長度，或失敗 |
| `se{file}` | 播放音效 | 不需要 |
| `mouth_closed{who}` | 非說話者閉嘴 | 不需要 |
| `spawn_offstage{who, card, outfit, pos}`、`release_offstage{who}` | 生成或釋放場外分身 | 就緒 |
| `thumbnail{slot}` | 隱藏 UI、拍一幀、寫 `thumb.jpg`、恢復 UI | 檔案路徑 |
| `end{reason}` | 結束播放，回到 Studio | 不需要 |

存檔的 `state.json` 由 Rust 直接寫；圖片由 C++ 寫，因為只有 C++ 能讀 GPU 畫面。

---

## 4. 劇本格式 `.okstory.json`（schema 1）

```json
{
  "schema": 1,
  "title": "放課後",
  "characters": {
    "ayu": { "card": "UserData/chara/ok/ayu.png", "outfit": 0, "name": {"ja":"あゆ","zh":"小鮎"}, "color": "#ff7aa8",
             "portrait": {"offset_x":0, "offset_y":0, "scale":1}, "voice": {"tts":"ayu", "pitch":1.0} }
  },
  "items": { "key": {"name":{"zh":"舊鑰匙"}} },
  "acts": [
    { "id": "a1", "title": {"zh":"第一幕　屋頂"}, "scene": "UserData/Studio/scene/roof.okscene",
      "camera": {"pos":[0,-300,160], "rot":[0,0,0,1], "fov":40},
      "cast": { "c0": {"who":"ayu", "x":0, "y":0, "z":0, "yaw":0, "scale":1, "motion":"stand_idle_01"} },
      "lines": [
        { "id": "a1.l1", "who": "ayu", "text": {"zh":"……你來了啊。"}, "voice": "Voice/ayu/a1_l1.ogg",
          "face": "smile", "motion": "stand_talk_02", "camera": {"shot":"speaker"},
          "look": "player" },
        { "id": "a1.l2", "who": "", "text": {"zh":"風把她的頭髮吹亂了。"}, "se": "SE/wind.ogg" },
        { "id": "a1.l3", "who": "ayu", "text": {"zh":"要一起回去嗎？"},
          "choices": [ {"text":{"zh":"好啊"}, "goto":"a2"},
                       {"text":{"zh":"用鑰匙開門"}, "goto":"a3", "need":[{"item":"key","n":1}], "consume":true} ],
          "give": [] }
      ] }
  ]
}
```

- `who` 為空字串時是旁白，不顯示名牌與立繪；`who` 是不在 `cast` 裡的角色時就是場外說話者（1.2 節的離場立繪）。
- `camera` 有三種寫法：
  - 省略：繼承上一句；
  - `{"pos","rot","fov"}`：固定相機；
  - `{"shot": ...}`：預設鏡頭，可用 `speaker`、`two`、`wide`、`studio:<相機物件 id>`。
- `cast` 在句上寫的是**差異**，例如 `{"c1":{"who":"rin"}}` 加人、`{"c0":null}` 退場；繼承由 `ok-story` 合併。
- 站位格由作者自由命名，座標以場景原點 cm 為單位，不沿用 3×10 格，因為 Studio 場景本來就會擺位置。編輯器提供「左、中、右」快速位置。
- `motion` 用 OK Studio 動作目錄的 id（`OKStudioCatalog`），`face` 用表情表的名稱（6.3 節）。
- `look`：`player` 看鏡頭；角色 id 看該角色的頭；`auto` 交給自然視線；省略時說話者看鏡頭，其他人交給 `auto`。
- 單位：位置 cm，旋轉用四元數（UE 座標），FOV 以度計。

---


## 5. 階段計畫

每個階段分成兩部分：**R**（Rust，`Tools/ok-story`）與 **P**（C++ 呈現）。R 先做，用 `cargo test` 驗完，P 只接上已驗證的 Frame／Commands。每階段結束時：

- 跑 `cargo test` 與 UE 建置；
- 用 `-Control` 加截圖驗收；
- 把錯誤記錄到 `Docs/ERROR_LOG.md`；
- 只提交自己的 hunk。

UE 建置前先問 E 組協調 session，同時最多兩個 Unreal。

### G0 模組骨架
- **R**：
  - `Tools/ok-story/Cargo.toml`：`crate-type = ["cdylib","rlib"]`、`panic = "unwind"`，依賴 serde／serde_json／toml；ICU 與語系沿用 `ok-rules` 的模組，以 path 依賴引入。
  - 原始檔：
    - `include/ok_story.h`；
    - `src/lib.rs`：ffi 入口，每個都包 `catch_unwind`；
    - `src/director.rs`：Director，負責 Input 佇列、tick、指令編號與 ack 狀態；
    - `src/clock.rs`：時間只靠回報的 dt 累計；
    - `src/rng.rs`：可序列化的種子 RNG。
  - 子模組 `script`、`progress`、`text`、`flow`、`camera`、`portrait`、`life`、`save` 先建空殼。
  - 測試 `tests/replay.rs`：讀錄好的 Facts＋Input 序列，逐幀和 golden Frame 比對；`UPDATE_GOLDEN=1` 時更新 golden。
  - 建置：`Tools/ok-story/build.lua`，比照 `character_mcp/build.lua`，支援 `--test`。
- **P**：
  - `FOKStoryRules`：比照 `FOKStreetRules`，處理載入、ABI 檢查、複製到 `reload/` 的熱換與 export／import；
  - 主控台指令 `ok.Story.Reload`；
  - `UOKStoryPlayer` 空殼：每幀呼叫 `oks_tick`，並把 Frame 寫進 log。
- **驗收**：UE 能載入 DLL 並收到空 Frame；熱換後 director 狀態仍在。

### G1 劇本與推進
- **R**：
  - `script`：解析與驗證 `.okstory.json`。錯誤要附路徑（如 `acts[0].lines[2].choices[1].goto`），要抓的情況：
    - 指向不存在的幕；
    - 重複的 id；
    - 未知的角色、物品或表情名；
    - 同一角色同時佔兩格。
  - `script` 的繼承合併：
    - 相機：句 → 幕 → 場景預設；
    - 站位：句上的差異與退場；
    - 說話者與名牌色；
    - 語系文字（ICU 格式化在 Rust）。
  - `progress`：
    - 狀態：目前幕與句、物品、旗標、已讀集合、選擇紀錄、Backlog（最近 200 句）；
    - 事件：`next`、`choose{i}`、`jump{line}`；
    - 選項：回傳 `enabled` 與已格式化的缺少物品提示；`consume` 時扣物品，`give` 時發物品。
  - 指令：換幕時發 `load_scene` 與 `place`；每句只對站位有變的角色發 `place`、`motion`、`face`。
- **P**：
  - `load_scene`：呼叫 `UOKStudioGame::Load`，同一路徑不重載；
  - `place`：以 card 路徑或 `scene_object` id 找角色，場景裡沒有就 `AddCharacter`；
  - `motion`、`face`：直接轉成 `AKKPreviewActor` 的呼叫。
- **驗收**：
  - `cargo test`：用測試劇本（3 幕、10 句、2 個選項、1 個物品條件）測繼承、分支、物品、已讀與錯誤訊息；
  - UE：角色依序出場、退場，表情與動作正確（截圖確認）。

### G2 對話框、打字、自動播放與快進
- **R**：
  - `text`：
    - 無語音時依字速前進：1～10 檔，第 5 檔約每秒 30 字；標點後停頓為字間的 3 倍；以 grapheme 計數。
    - 第一次點擊顯示全文，第二次點擊才前進。
    - 輸出 `text_visible`（已切好的字串，C++ 不自己切字）、`shown`（給控制通道與測試用），以及 `waiting = typing | click | choice | auto | voice`。
  - `flow`：
    - 自動播放：打字與語音都結束後等 `0.8 s + 字數 × 0.06 s`；有選項時不自動前進。
    - 快進：只快進已讀句，每句 0.05 s；遇到選項或未讀句就停；按住 Ctrl 時暫時快進。
    - 快進中不發 `voice`，相機與幕標題直接到終點。
  - `layout`：對話框比例放在 tuning，Frame 帶出。
- **P**：
  - `SOKStoryOverlay`（Slate）照 Frame 顯示：
    - 對話框位置照 `layout`；
    - 名牌顯示 `name` 與 `name_color`，名字為空時隱藏；
    - 文字顯示 `text_visible`；
    - 選項照 `choices` 顯示，`enabled = false` 時變灰，並以 tooltip 顯示 `hint`；
    - 快捷列：自動、快進、Backlog、存讀檔、設定、隱藏 UI。
  - 輸入：滑鼠、鍵盤、按鈕一律轉成 `oks_input` 事件（`click`、`choose`、`toggle_auto`、`hold_skip`、`menu`、`hide_ui`、`backlog_open`），C++ 不判斷現在該做什麼。
- **驗收**：
  - `cargo test`：以固定 dt 重播，測字速、標點停頓、自動前進與快進邊界；
  - UE：720p、1080p、2160p 截圖，不重疊、不裁切。

### G3 語音同步
- **R**：
  - 打字進度：
    - 收到 `voice.state = playing` 後：`shown = max(目前值, ceil(字數 × progress))`，不倒退；
    - 收到 `finished` 時顯示全文，並開始自動前進計時。
  - TTS：ack 回來之前用固定字速，拿到長度後改用語音時鐘；失敗時維持固定字速，並在 Frame 的 `log` 留訊息。
  - 前進時發 `stop_voice`（設定可改成「播完才停」），對非說話者發 `mouth_closed`。
  - Backlog 重播只發 `voice`，不改變進度。
- **P**：
  - `AKKPreviewActor` 加唯讀查詢 `SpeechProgress()`（`RenderedFrames / 總 frame`）與 `SpeechDurationSeconds()`，每幀填進 Facts；
  - TTS 走 `Tools/ok-tts`，拿到 PCM 後呼叫 `PlaySpeechPcm`，再 ack 長度；
  - 嘴型沿用 RMS，不需改動。
- **驗收**：
  - `cargo test`：模擬 3 秒語音，在 progress 0.25、0.5、1.0 時檢查 `shown`；另外測 TTS 慢回應與失敗；
  - UE：實際播語音時截圖，文字與聲音大致同步。

### G4 左下即時立繪
- **R**（`portrait`，依 1.2 節機制、以 OK 骨架重寫）：
  - 參考姿勢：
    - 支點依序找 `spine03` → `spine02` → `hips` → `head`；
    - 第一次登場、套用劇本動作之前，記下 `頭 − 支點` 的位移、hips 的靜止旋轉與縮放；
    - 這些存進 director 狀態，熱換與讀檔都保留。
  - 每幀計算：
    - 水平 yaw = hips 目前旋轉 × 靜止旋轉的逆；水平分量太小時沿用上一幀；
    - 目標點 = 支點 + 依 yaw 旋轉後的位移；
    - 相機位置 = 目標點 + 依 yaw 旋轉後的偏移（側 −85、上 5、前 170 cm，乘縮放），看向目標點上方 3 cm；
    - 正交寬 = 40 cm × 縮放，再套用 `offset_x`、`offset_y`、`scale`；
    - 輸出 `capture{pos, rot, ortho_width}`。
  - 換人：淡出 100 ms、淡入 150 ms；旁白時淡出。輸出 `alpha`、`rect`、`fade_from`。
  - 場外說話者：
    - 發 `spawn_offstage`，位置取自 tuning，在主相機看不到的地方；之後以分身的骨骼取景；
    - 離場或換幕時發 `release_offstage`；
    - 下一句的場外角色先發 `spawn_offstage` 預載。
- **P**（`UOKLivePortrait`）：
  - `USceneCaptureComponent2D`：正交，以 `PRM_UseShowOnlyList` 只拍 `who` 的元件，`bCaptureEveryFrame = false`；每幀設好變換後呼叫 `CaptureScene()`。
  - 透明合成（GPU，不讀回 CPU）：
    - 方案 A：LDR capture 取顏色，HDR capture 的 `1−A` 當 coverage，在 UI 材質 `M_OKPortraitComposite` 裡合成；
    - 方案 B：只做一次 HDR capture，在材質中近似 tone mapping；
    - 先做 A，再和 B 截圖比較，把結論寫回本文件。
  - 後處理複製主相機的設定，關掉 TAA 與動態模糊。
  - Slate 依 `rect`、`alpha` 擺放立繪，下緣漸層起點用 `fade_from`。
  - `spawn_offstage`：生成獨立的 `AKKPreviewActor`，附一盞 key light，就緒後 ack。
- **驗收**：
  - `cargo test`：錄好的骨骼序列（轉頭、轉身、點頭）——轉身時相機跟轉，轉頭與點頭時相機不動；縮放與 offset 正確。
  - UE：
    - 立繪的嘴型、眨眼、轉頭和主畫面同一幀一致；
    - 頭髮與外框沒有黑邊或白邊（放大截圖確認）；
    - 換人 5 次沒有殘影；
    - GPU 時間增加不超過 2 ms（1080p）。

### G5 逐句運鏡
- **R**（`camera`）：
  - 每句相機：固定姿勢，或 `shot` 預設鏡頭。`shot` 在換句當下依 Facts 的骨骼位置算一次，之後固定：
    - `speaker`：頭前方 120 cm、側 20°、FOV 30；
    - `two`：兩人頭的中點，兩人都落在畫面 60% 寬以內；
    - `wide`：所有在場者的包圍盒，FOV 40；
    - `studio:<id>`：場景中的 Camera 物件，姿勢由 `load_scene` 的 ack 帶回。
  - 插值：
    - 時間 = `clamp(距離/400cm + 角度/π × 0.7 + ΔFOV/100, 0.35, 4) / clamp(速度, 0.85, 1.15)`；
    - 位置與 FOV 線性插值，旋轉用 slerp，進度套 smoothstep；
    - 同一幕才平滑移動，換幕或讀檔直接切換；點擊、前進、快進時跳到終點；選單開著時暫停。
  - 空間感視差：Input 的滑鼠位置（−1..1）以 `1−e^(−8·dt)` 平滑；偏移約 4×2.5 cm、1.2°×0.8°，乘上強度，直接加在輸出的相機上，不寫回劇本或存檔。
  - 相機編輯模式：Input 傳 WASD、QE、滑鼠增量與 Shift／Alt，由 Rust 計算飛行；確定時把相機寫回劇本該句的 `camera`（Rust 寫檔），取消則還原。
- **P**：
  - 依 Frame 設定 `Viewer` 的位置、旋轉與 FOV；播放模式中停用 orbit 與 gizmo；
  - 編輯模式時把原始輸入轉成事件，並鎖定滑鼠。
- **驗收**：
  - `cargo test`：時間公式、繼承、跳過時的落點、同幕與換幕、暫停、視差不寫回；
  - UE：同幕兩句之間截中間幀，相機應落在兩端之間。

### G6 演出生命感
- **R**：
  - 幕標題（`flow`）：時間 460／800／340 ms，cubic-bezier 求值自己實作，輸出 `title{text, x, alpha}`；播完才開始第一句。
  - 表情（`faces`）：讀 `faces.json`（欄位同 `FOKStreetExpressions`，可共用街區的表），把表情名換成 pattern 再發 `face`；每個 personality 可以覆寫。
  - `life`（種子 RNG，同一存檔重播結果相同）：
    - 眨眼：
      - 播放開始時發 `auto_blink{on:false}`，之後由 Rust 排程；
      - 每人 6～12 s 一次，全場同時最多一人，衝突時延後 0.2～1 s；
      - Facts 回報 `authored_blink` 或 `face_closed` 時跳過並重新計時；
      - 觸發時發 `blink`。
    - 視線：
      - 小幅偏移：每 8～16 s，在目前方向加 ±(3～8)°、±(1～4)°，停 0.45～1.2 s，換算成世界座標後發 `look{target}`；
      - 看人：對象須在前方 ±70°、4 m 以內，看 1～1.8 s，每人冷卻 45～75 s；
      - 上限：水平 13°、垂直 6°；同時最多兩人，起始時間至少錯開 1.2 s；
      - 讓位：劇本有寫 `look`，或 Facts 回報 `authored_look` 時讓位；說話者預設 `look{camera}`；
      - 暫停時停止計時；重新入場時重新計時。
    - 結束時發 `look{none}` 與 `auto_blink{on:true}` 還原。
- **P**：只執行 `blink`、`look`、`auto_blink`、`face`；在 Facts 中回報各角色的 `authored_*` 與 `face_closed`（哪些 pattern 算閉眼，判斷表由 Rust 提供）。
- **驗收**：
  - `cargo test`：20 人、模擬 5 分鐘：
    - 任一時刻眨眼不超過 1 人、視線偏移不超過 2 人；
    - 每人至少觸發一次；
    - 偏移不超過上限；
    - 同一種子重播結果完全相同。
  - UE：3 人場景目視檢查。

### G7 存讀檔、Backlog、設定
- **R**（`save`）：
  - 存檔：
    - 位置：`UserData/Story/save/<劇本名>/slot_NN/state.json`；
    - 內容：director 狀態（進度、物品、旗標、已讀、Backlog、RNG 種子、立繪參考姿勢）、劇本 hash、時間、幕名、對白摘要；
    - 存檔時發 `thumbnail{slot}`，ack 回來才算完成。
  - 讀檔：
    - 劇本 hash 不同時在 Frame 提示「劇本已修改」，仍以句 id 還原；找不到該句就回到幕首；
    - 依序發 `load_scene`、`place`、`motion`、`face`；相機直接切換，不顯示幕標題。
  - 給畫面用的查詢：
    - `oks_query{saves}`：12 格分頁、F5／F9 快速存讀、每幕開頭自動存檔；
    - `oks_query{backlog}`：Backlog 內容；跳回某句是選配。
  - 設定（`UserData/Story/settings.json`）：字速、自動速度、只快進已讀、音量、前進時停止語音、視差強度、立繪更新頻率、UI 透明度。
- **P**：
  - `thumbnail`：隱藏 Overlay，下一幀拍主畫面，縮成 320×180、品質 80、100 KB 以內的 `thumb.jpg`，恢復 UI 後 ack；
  - 存讀檔、Backlog、設定畫面都只顯示 query 的結果，並把操作轉成 Input。
- **驗收**：
  - `cargo test`：存、讀、再存後 state 完全相同；hash 不同與句子不存在時的還原規則；
  - UE：縮圖裡沒有 UI。

### G8 劇本編輯與 AI 工具（之後）
- **R**：
  - `oks_query{edit}`：新增、修改、刪除句，寫回 `.okstory.json` 並重新驗證；
  - MCP stdio server：獨立的 Rust 執行檔，比照人物編輯 MCP，連結 `ok-story` 的 rlib，提供列出、修改、驗證、跳句、截圖，讓外部 AI 協助寫劇本。
- **P**：Scene Editor 的「劇本」面板，照 query 結果顯示，並有「存目前相機」與「從這句播放」。
- **其他**：標題畫面、章節選擇、CG 圖鑑、介面主題。

---

## 6. 細部規格

### 6.1 控制通道指令（自測用）
C++ 端只把指令轉成 `oks_input` 或 `oks_query`，不做其他處理：

| 指令 | 作用 |
|---|---|
| `story load <file>` | 載入劇本 |
| `story state` | 即 `oks_query{state}`，回傳幕、句、`shown`、說話者、相機是否在移動、語音進度 |
| `story next`、`story choose N` | 前進、選擇選項 |
| `story skip on\|off`、`story auto on\|off` | 快進、自動播放 |
| `story save N`、`story load_slot N` | 存檔、讀檔 |
| `story record on\|off` | 把 Facts 與 Input 錄成 `tests/replay/*.jsonl`，給 `cargo test` 重播 |
| `shot` | 沿用既有指令 |

### 6.2 數值
放在 `Content/IndependentResources/Story/tuning.json`，由 Rust 讀取；執行 `ok.Story.Reload` 或檔案變更時重讀。

| 項目 | 值 |
|---|---|
| 對話框高、文字左緣 | 26%、27.5% |
| 立繪框 | 寬 54%、高 68%；漸層起點 50% |
| 立繪取景 | 側 −85、上 5、前 170 cm；看向上方 3 cm；正交寬 40 cm |
| 運鏡時間 | 距離/400 cm ＋ 角度/π × 0.7 ＋ ΔFOV/100，夾在 0.35～4 s |
| 幕標題 | 460／800／340 ms |
| 字速第 5 檔 | 30 字/s，標點停頓 ×3 |
| 自動前進等待 | 0.8 s ＋ 0.06 s/字 |
| 眨眼 | 間隔 6～12 s，同時 1 人 |
| 視線 | 8～16 s；看人冷卻 45～75 s；上限 13°／6°；同時 2 人、錯開 1.2 s |
| 立繪 | RT 768×1152（經 Frame 傳給 C++）；淡出 100 ms、淡入 150 ms |

### 6.3 表情名稱（第一版）
`normal`、`smile`、`laugh`、`shy`、`sad`、`cry`、`angry`、`surprise`、`think`、`sleepy`、`wink`、`closed`。

對應的 pattern 數值寫在 `faces.json`，依 personality 可覆寫；哪些 pattern 算「閉眼」也寫在這份表裡。

---

## 7. 風險與待決定

| 項目 | 說明 | 處理 |
|---|---|---|
| 每幀 JSON 成本 | Facts 含多個角色的骨骼 | 只回報必要的骨骼；超過 0.1 ms 就把每幀數值改成 `#[repr(C)]` |
| 指令非同步完成 | 場景、TTS、分身都要時間 | Rust 以 ack 推進，等待中在 Frame 標記 `loading`；C++ 不自己等 |
| 透明立繪邊緣 | toon 外框與頭髮 alpha 可能出現暗邊 | G4 比較 A 與 B，必要時做 premultiply 修正 |
| 每幀兩次 capture | 方案 A 等於多畫兩次單一角色 | 只拍說話者、降解析度、可隔幀更新（頻率在 tuning）；超出預算就改用 B |
| 場外分身資源 | 多一個 `AKKPreviewActor` 與 Session | 由 Rust 控制生成時機，換幕時釋放，並預先載入 |
| 熱換狀態相容 | 新 DLL 的狀態結構可能改變 | `oks_export` 帶 schema 版本；不相容時從目前句重建，並寫 log |

**待使用者決定**：

1. 劇本先手寫 JSON 加控制通道，G8 再做面板與 MCP？（建議這樣做）
2. 存檔與設定暫定放在 `UserData/Story/`，要改位置請說。
3. 第一份測試劇本用哪兩張卡？（只留本機，不進版控）

---

## 8. 順序

G0 → G1 → G2 → G3 → G4 → G5 → G6 → G7 → G8。

- G0～G3 完成後，就能播出有語音、有打字、有選項的劇本。
- G4 的左下立繪是主要的視覺特色，也是技術風險最高的部分。
- R 部分大多不需要 Unreal：可以先把 G1～G6 的 R 全部寫完並用 `cargo test` 驗過，再集中做 P。這樣 UE 建置次數最少，也符合「C++ 整批做完再建置」的做法。
- 唯一需要先動 UE 的風險是 G4 的透明擷取實驗（方案 A 與 B），可以和 G0、G1 的 R 同時進行。
