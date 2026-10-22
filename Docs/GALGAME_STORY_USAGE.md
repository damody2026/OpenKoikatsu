# OK Studio 劇本播放（galgame 對話演出）使用說明

在 OK Studio 場景上播放 galgame 式劇本。計畫與設計：[GALGAME_DIALOGUE_PLAN.md](GALGAME_DIALOGUE_PLAN.md)。

## 分工

- **`Tools/ok-story`**：Rust，產出 `ok_story.dll`。負責全部邏輯：
  - 劇本解析與驗證；
  - 推進、選項、物品、旗標、已讀；
  - 打字時鐘、自動播放、快進；
  - 運鏡、立繪取景、眨眼與視線；
  - 存讀檔、設定。
- **UE**：`Plugins/OKStudio`。
  - `UOKStoryPlayer` 回報事實並執行指令；
  - `SOKStoryView` 畫出 Rust 給的畫面，並把輸入轉成事件。
  - C++ 不做任何判斷。

## 啟動

```bash
Tools/lua/lua.exe Tools/run_scene_editor.lua -Story Content/IndependentResources/Story/Samples/demo.okstory.json
```

- `run_scene_editor.lua` 會先 `cargo build --release` 建 `Tools/ok-story`，把 DLL 複製到 `Saved/Story/Build/`，再以 `OK_STORY_DLL` 指給編輯器。改了 Rust 之後重新 build，在遊戲主控台執行 `OKStory.Reload` 即可換上新版，播放狀態保留。
- 加 `-StorySelfTest` 會自動點完整個劇本，每句截圖一張，報告寫在 `Saved/Story/SelfTest/<時間>/report.json`，跑完自動結束。
- 劇本播完，或在選單選「結束」，編輯器就會關閉。從 Studio 內播放則用主控台 `OKStory.Play <檔案>`。

主控台指令：

| 指令 | 作用 |
|---|---|
| `OKStory.Play <file>` | 在目前的 Studio 場景上播放劇本 |
| `OKStory.Stop` | 結束播放 |
| `OKStory.Input <json>` | 送一個輸入事件 |
| `OKStory.State` | 把目前狀態寫進 log |
| `OKStory.Reload` | 換上新建好的 DLL；DLL 沒變時改為重讀劇本、tuning 與文字 |
| `OKStory.SelfTest <file> [exit]` | 自動點完劇本並截圖 |

## 操作

| 輸入 | 作用 |
|---|---|
| 左鍵、Enter、Space、↓、滾輪往下 | 先顯示整句，再按才前進 |
| 右鍵、H | 隱藏 UI，再按一次恢復 |
| 按住 Ctrl | 快進（只快進已讀句；設定可改為也快進未讀） |
| A | 自動播放 |
| 滾輪往上 | 回顧紀錄（Backlog） |
| Esc | 選單，再按一次關閉 |
| F5／F9 | 快速存檔／快速讀檔 |
| F8 | 相機編輯：WASD／QE 移動，滑鼠轉向，Shift 加速，Alt 減速，Enter 把目前視角存進這一句，Esc 取消 |

## 劇本格式 `.okstory.json`（schema 1）

範例：`Content/IndependentResources/Story/Samples/demo.okstory.json`。它使用本機的 Studio 樣本場景與角色卡；這些檔案依規則不進版控，在乾淨 clone 上要換成自己的場景與卡片。

```json
{
  "schema": 1,
  "title": {"zh": "標題"},
  "characters": {
    "mio": {"card": "UserData/.../char_0.png", "scene_object": 1, "name": {"zh": "澪"}, "color": "#ff8fb4",
            "personality": 25, "voice": {"tts": null, "pitch": 1.0},
            "portrait": {"offset_x": 0, "offset_y": 0, "scale": 1},
            "offstage": {"motion": "0:5:0", "face": "smile"}}
  },
  "items": {"ticket": {"name": {"zh": "電影票"}}},
  "start": "intro",
  "acts": [
    {"id": "intro", "title": {"zh": "第一幕"}, "scene": "UserData/Studio/scene/x.okscene",
     "camera": {"shot": "wide"},
     "cast": {"left": {"who": "mio", "pos": [0, 0, 0], "yaw": 90, "motion": "0:5:0", "face": "normal"}},
     "next": "act2",
     "lines": [
       {"id": "intro.1", "who": "mio", "text": {"zh": "台詞"}, "voice": "Voice/mio_001.ogg", "se": "SE/door.ogg",
        "face": "smile", "motion": "0:5:1", "camera": {"shot": "speaker"}, "look": "camera",
        "cast": {"right": {"who": "sora"}, "left": null},
        "give": [{"item": "ticket", "n": 1}], "take": [], "set": {"route": 1},
        "when": {"flags": {"route": 1}, "items": []},
        "choices": [{"text": {"zh": "選項"}, "goto": "act2", "need": [{"item": "ticket"}], "consume": true,
                     "give": [], "set": {}, "when": {}}],
        "goto": "intro.5", "name": {"zh": "？？？"}}
     ]}
  ]
}
```

### 欄位說明

**文字**：可以是一個字串（所有語言共用），也可以是 `{"zh": .., "ja": .., "en": ..}`。語言用環境變數 `OK_STORY_LANG` 指定，預設 `zh`；找不到時依序退回 zh、ja、en。

**角色**：
- `card`：卡片路徑（絕對路徑，或相對劇本與專案根目錄）。
- `scene_object`：直接使用場景裡已有的角色物件，保留它的姿勢。沒填時以卡片路徑比對；都找不到就新增一個角色。
- `personality`：表情表要用哪個 profile；沒填時讀卡片的值。
- `offstage`：不在場說話時，立繪用分身的動作與表情。

**站位（`cast`）**：
- 幕的 `cast` 是開場站位；句子上的 `cast` 只寫有變動的部分，`null` 代表退場。
- `pos`、`yaw`、`scale` 沒寫時保留場景原本的值。
- 同一個角色不能同時站兩格（驗證時會擋下）。

**動作**：寫成 `"group:category:no"` 或 `"kind:group:category:no"`，例如 `0:5:0` 是會話的待機 1。
- 編號對應 `Content/IndependentResources/base_v1/studio_anims.json`。
- 換動作是直接切換，目前沒有 crossfade。

**表情**：可寫遊戲表情表（`StreetSocialData/RuntimeV2/expressions.json`）裡的日文名稱，也可寫英文別名。
- 別名：normal、closed、smile、smile_closed、laugh、happy、shy、blush、sad、cry、tearful_smile、angry、pout、surprise、think、worry、serious、smug、wink、doki、sleepy、look_away。
- 對照表在 `tuning.json` 的 `faces`，可用 `Story/faces.json` 增加別名。

**相機**：
- 固定視角：`{"pos": [..], "rot": [x,y,z,w]}` 或 `{"pos": [..], "look_at": [..]}`，可加 `fov`。
- 自動鏡頭 `{"shot": ...}`：`speaker`（特寫說話者）、`two`（說話者與最近的一人）、`wide`（全員）、`studio`／`studio:<id>`（場景的相機物件）。`on` 可指定改拍誰。
- 自動鏡頭會從多個位置與距離中挑一個看得見人、中間沒有牆的位置（UE 用小型深度擷取回報視線距離）。
- 句子沒寫相機時沿用上一句；同一幕內平滑移動，換幕或加 `"cut": true` 時直接切換。
- 在 F8 相機編輯裡按 Enter，會把目前視角寫回劇本檔。

**視線（`look`）**：
- `camera`：說話者的預設，看鏡頭；
- `none`：交給動畫；
- `auto`：交給自然視線；
- 角色 id：看那個人。
- 其他在場的人預設看說話者。眨眼與小幅移開視線由 Rust 錯峰排程。

**流程**：
- `when` 不成立的句子會跳過。
- `goto` 可指向幕 id（從第一句開始）或句 id。
- 幕的 `next` 是播完後的下一幕；`""` 代表劇本結束；沒寫時接下一幕。

## 檔案位置

| 內容 | 位置 |
|---|---|
| 數值（版面、字速、運鏡、立繪、眨眼與視線） | `Content/IndependentResources/Story/tuning.json`。只要寫想改的欄位，其餘用預設。重新產生預設：`OK_STORY_DUMP_TUNING=<檔> cargo test dump_tuning` |
| 介面文字 | `Content/IndependentResources/Story/Localization/<lang>/story.toml`，ICU MessageFormat |
| 存檔 | `UserData/Story/save/<劇本名>/<格>/state.json` 與 `thumb.jpg`；已讀紀錄在 `read.json`，所有存檔共用 |
| 設定 | `UserData/Story/settings.json` |

## 測試

```bash
cargo test --release --manifest-path Tools/ok-story/Cargo.toml
```

整合測試 `tests/director.rs` 用一個假的舞台走完整個劇本，涵蓋：
- 選項、物品、場外說話者；
- 語音同步打字、快進、自動播放；
- 存讀檔、熱換；
- 相機編輯、避開牆的鏡頭；
- 隨附劇本檔的驗證。

## AI 寫劇本：`ok-story-mcp`

`Tools/ok-story/target/release/ok-story-mcp.exe` 是 MCP stdio server（`cargo build --release` 會一起建出來），讓外部 AI 編輯劇本、在執行中的 OK Studio 預覽：

```bash
claude mcp add ok-story -- E:/OpenKoikatsu/Tools/ok-story/target/release/ok-story-mcp.exe --project E:/OpenKoikatsu
```

| 工具 | 作用 |
|---|---|
| `story_new` | 建立新劇本（一幕，指定場景） |
| `story_outline` | 列出幕、句（id、說話者、開頭文字、選項）與角色 |
| `story_validate` | 列出劇本錯誤（附路徑） |
| `story_get_line`／`story_set_line`／`story_add_line`／`story_delete_line` | 逐句讀取與編輯；`null` 代表刪除欄位 |
| `story_add_act`／`story_set_act`／`story_set_character` | 編輯幕與角色 |
| `game_state`／`game_jump`／`game_input`／`game_reload`／`game_screenshot` | 操作執行中的劇本：查狀態、跳到某句、送輸入、重讀劇本、截圖 |

- 每次寫入前都會先驗證；不合法就不寫，並回報錯誤路徑。編輯時保留原本的欄位順序與不認得的欄位。
- `game_*` 工具透過 `UserData/Story/control/` 交換檔案：每個請求放一個 `<id>.json`，遊戲每 0.2 秒檢查一次，答覆寫成 `<id>.out.json`。遊戲沒有在播劇本時，等 15 秒後回報。
- 典型流程：先用 `story_add_line`／`story_set_line` 改劇本，再呼叫 `game_reload`、`game_jump` 跳到那一句，最後用 `game_screenshot` 看結果。

## 已知限制

- 換動作是直接切換，沒有 crossfade（`motion_blend` 目前沒有作用）。
- OK Studio 沒有接上 `ok-tts`，只寫了 `voice.tts` 的句子沒有語音，改用固定字速打字。
- 立繪的透明度用 HDR 擷取的反向不透明度判斷，半透明的頭髮邊緣是近似值。
- 自動鏡頭只能用深度探測避開不透明的牆。不寫深度的東西（例如 Modulate 混合的水面）擋不住探測，鏡頭仍可能放到它後面；這時請用固定相機，或用 F8 存一個相機。
- 示範劇本用的 Studio 場景與角色卡只在本機，不進版控。`ill_ss_sample_00` 在 UE 裡的畫面有問題（地圖 51 加上「野原」道具時，只看得到大片沒有貼圖的多邊形），這是 Studio 場景轉換的問題，與劇本播放無關；示範因此改用 1901／3800。
