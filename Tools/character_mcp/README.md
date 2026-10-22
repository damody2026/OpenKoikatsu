# 人物編輯器 MCP

完整操作手冊（含外部 AI 指示與全部工具）：[`Docs/CHARACTER_EDITOR_MCP_USAGE.md`](../../Docs/CHARACTER_EDITOR_MCP_USAGE.md)。

完整計畫：[`Docs/CHARACTER_EDITOR_MCP_PLAN.md`](../../Docs/CHARACTER_EDITOR_MCP_PLAN.md)。

目前使用官方 `rmcp` 3.5.1 的 Rust stdio server。全部 31 個工具、參考影像、遮罩、截面量測、搜尋、敏感度與背景作業皆由同一執行檔處理，使用時無需 Python 或 pip 套件。

開發者從原始碼建置（需 Rust/Cargo 與 MSVC）：

```powershell
Tools/lua/lua.exe Tools/character_mcp/build.lua --test
```

產物為 `Tools/character_mcp/bin/ok-character-mcp.exe`，Windows release 使用靜態 CRT。仍需已包含 MCP 功能的 Unreal 建置。

## 啟動

啟動人物編輯器時加入 `-CharacterMCP`，載入角色的 3D 模型。例如：

```powershell
& E:/PhysX/UE5.8/Engine/Binaries/Win64/UnrealEditor.exe C:/portable/OpenKoikatsu/OpenKoikatsu.uproject /Engine/Maps/Entry -game -windowed -CharacterMCP
```

亦可加 `-CharacterMCPCard="C:/path/to/character.png"` 自動載入一張卡。選用 `-CharacterMCPDir="C:/path/to/mailbox"` 可隔離多個執行個體；每個信箱只能對應一個遊戲執行個體。

在支援 stdio 的 MCP 客戶端加入以下設定：

```json
{
  "mcpServers": {
    "openkoikatsu-character": {
      "command": "C:/portable/OpenKoikatsu/Tools/character_mcp/bin/ok-character-mcp.exe",
      "args": [
        "--mailbox", "C:/portable/OpenKoikatsu/Saved/CharacterMCP",
        "--timeout", "1800"
      ]
    }
  }
}
```

不開網路監聽埠。信箱屬本機使用者控制通道，不能放在不受信任的人可寫入的共用目錄。圖片／另存卡片保留於信箱的 `images`／`cards`，請求與已讀回覆會清理。

首次外觀套用可能需重建所有服裝資源；設定的 1800 秒期限保留冷快取建置時間。state 最多等待 10 秒，長作業進度另用 `editor_progress` 查看。

## 工具與使用流程

1. `character_state` 取得 `session`、`revision` 與 busy 狀態。
   已載入角色後亦可用 `character_load` 載入另一張本機 PNG 卡，完成後採用新的 session／revision。
2. `parameters_list` 搜尋人物資料；`editor_controls` 搜尋其餘編輯控制。查詢支援分頁，最多每頁 500 項。
3. `snapshot_create` 保存卡片資料與尚未套用的屬性。
4. `parameters_set` 批次修改人物資料；`editor_control_set` 修改選單、開關、文字或動畫控制；`editor_action` 控制服裝狀態、姿勢、播放與語音命令。
5. 若有 `pending_properties`，呼叫 `character_apply` 產生外觀與重新載入。完成後使用回傳的新 session／revision。
6. `capture_configure` 固定攝影條件，`capture_views` 取得正／側／背 PNG，或 `capture_region` 截取局部。
7. 改差時用 `snapshot_restore`，完成時用 `character_save` 另存新卡。

人物參數 key：

| 範圍 | 範例 |
|---|---|
| 身體 44 項 | `shape.body.0`（身高） |
| 臉部 52 項 | `shape.face.0` |
| 骨骼，各側獨立 | `bone.cf_J_FaceBase.scale.x` |
| 骨骼等比縮放 | `bone.cf_J_FaceBase.scale.uniform` |
| 骨骼長度／位置／旋轉 | `bone.<骨名>.length`、`.position.x`、`.rotation.z` |
| 身體物理 | `physics.bustSoftness`、`physics.bustWeight` |
| 腹部形變 | `pregnancy.inflationSize` |
| 詳細卡片屬性 | 以查詢回傳 key 為準，例如 `hair.parts.0.baseColor.0` |
| 編輯控制 | `control.AutomaticBlinkCheck`、`control.HandCombos.0` 等，以目錄為準 |
| 動態動畫參數 | `animation.<來源參數名>` |

骨骼以實際骨名識別，不依賴滑桿排列。左右同步使用同一批 edits 同時修改左右 key。缺少骨骼時 `supported=false` 且拒絕寫入。詳細屬性先暫存，可用 reset 撤銷；shape／bone／physics／pregnancy 立即更新。

`editor_controls` 包含隱藏但可用的控制；`supported=false` 表示目前停用或無有效回呼。`stable_key=false` 的 UI 備援 key 只在目前動態介面有效，介面重建後應重新查詢。素材用 `appearance_catalog`／`appearance_select`，新增槽位用 `appearance_add_accessory`。

寫入範例（先用實際回傳值取代 session／revision）：

```json
{
  "session": "<character_state 回傳值>",
  "revision": 2,
  "edits": [
    {"key": "shape.body.0", "value": 0.7},
    {"key": "shape.body.21", "value": 0.4}
  ]
}
```

範圍截圖使用 viewport 像素座標，預設 `target=preview`；`target=rect` 時另傳 `rect: {x,y,width,height}`。圖片由 MCP image content 直接回傳，不需要 AI 自己開檔。三視圖維持同一鏡頭尺度並凍結人物 tick，完成後恢復鏡頭與播放狀態；拍攝期間人物資料有變化會失敗而非混用不同版本。

正交三視圖先設定 `projection=orthographic`、`ortho_width`、`focus`、`distance`、`tpose=true`；單位為 Unreal 公分，角度為度。調整身形期間重用拍攝設定。隱藏衣物只處理顯示，不一定解除原服裝的身體遮罩；需要完整身體輪廓時用 `editor_action` 的 `clothing`、`clothing_kind=102` 經正式服裝流程移除。

## 驗證與邊界

### 從三視圖自動調整身形

1. 用 `editor_action` 的 `clothing_kind=102` 移除服裝，再用 `capture_configure` 固定 T-pose、隱藏髮／配件。參考圖姿勢應一致。
2. `body_geometry` 匯出當前形變後身體／頭部網格及骨骼位置。`character_measure` 回傳實際頂點 bounds；`body_sections` 以指定 world Z 高度量測各個閉合截面，手臂與軀幹保持分離，開放／非流形截面明列無效。
3. `body_masks` 產生正交正／側／背身體遮罩。`reference_prepare` 讀取三張本機參考圖，設定 `mode=mask`（白色人物、黑底）、`alpha`（透明背景）或 `background`（明確 RGB 背景與容差）。複雜照片由外部 AI 提供人物分割遮罩；工具不把任意照片直接當作正確分割。
4. 若知道目標真實身高，傳入 `physical_height`（公分）。未提供時以目前角色身高對齊，只比較比例。每個視圖只在準備時對齊尺度／平移；搜尋期間維持同一投影，越界候選拒絕。
   若參考圖來自 `body_masks`，可直接傳回其 `frame` 作為 `projection_frame`，保留原投影而不重新對齊。`side_view=left/right` 指定參考側別；預設 right 與 `capture_views` 一致。
5. `body_sensitivity` 對指定 key 做雙向小幅擾動，回傳輪廓變化率與誤差導數，完成後還原。`body_fit` 執行有界搜尋；可傳任何已支援、立即套用、具有範圍的數值參數組合，預設全部 44 項身體參數。骨骼／臉部同樣可加入 `keys`。
6. 用 `body_compare` 查看三視圖 IoU 與輪廓誤差。搜尋以 `target_loss`、`max_evaluations` 或最小步長停止，回報實際停止原因；完成時寫回最佳值。`resume` 接受前次 run ID，但必須相同角色、參考資料及 key。
   外部 AI 建議使用 `body_fit_start`，立即取得 job ID，再以 `body_fit_status` 輪詢，避免客戶端單次工具逾時。`body_fit_cancel` 可取消；完成結果保存在磁碟，服務重開仍可查詢。`max_seconds` 預設 1800 秒，與評估次數一起限制搜尋；單次已開始的編輯仍需完成或逾時。
7. 使用 `character_save` 保存，再重新開啟確認。攝影彩圖仍可用原有 `capture_views` 取得。

命令列範例（遊戲須先啟用 MCP 並載入角色）：

```powershell
& C:/portable/OpenKoikatsu/Tools/character_mcp/bin/ok-character-mcp.exe fit --mailbox C:/portable/OpenKoikatsu/Saved/CharacterMCP --front C:/refs/front.png --side C:/refs/side.png --back C:/refs/back.png --mode mask --physical-height 165 --max-evaluations 300
```

`--keys shape.body.0 shape.body.22 shape.body.23` 可縮小搜尋範圍；`--sensitivity` 只做敏感度估計。CLI 完成搜尋後另存新卡。結果／每次試驗／最佳值保存在信箱 `fitting/run-<ID>.json`；準備好的參考遮罩與投影保存在 `fitting/<reference ID>/`。

`editor_progress` 可在長作業進行時讀取 heartbeat、phase 及等待條件；它不排在長作業之後，版本最多落後 0.5 秒，正式修改前仍查 `character_state`。執行期錯誤記入信箱 `errors.md`，開發錯誤與修正記於 [`Docs/CHARACTER_EDITOR_MCP_ERRORS.md`](../../Docs/CHARACTER_EDITOR_MCP_ERRORS.md)。

取消／例外保存 checkpoint；只有角色版本仍由該搜尋持有時才還原最佳值，遇到外部人工改動會停止並保留人工修改。可檢查 `best_restored`／`recovery_error` 再決定續跑。

```powershell
cargo test --locked --manifest-path Tools/character_mcp/rust/Cargo.toml
```

Unreal automation：`OpenKoikatsu.CharacterMCP.Registry`。實際渲染驗收另由 `verify_live.lua` 執行：`Tools\lua\lua.exe Tools\character_mcp\verify_live.lua --card <card.png>`。

- 取消／逾時不能保證撤銷已開始的外觀作業；先查 state 和讀回值，不要盲目重送。
- 快照保存人物卡片資料與暫存詳細屬性；即時表情、動畫、攝影是獨立預覽狀態，不宣稱全部一併復原。
- 量測為模型當前姿勢的頂點／三角網格，截面不是照片推測；工具不把某個任意高度的最大閉合圈自動命名為胸／腰／臀。可以用骨骼標記指定需要的高度。
- 支援當前編輯器已提供的參數；未知二進位模組資料保留，不宣稱已實作原遊戲所有第三方插件。
- 已有遮罩、參考對齊、敏感度與有界搜尋；任意姿勢照片的語義分割、透視反推與新增 morph 由外部 AI／其他模型處理。三張輪廓無法唯一決定所有內部參數，既有 morph／骨骼能表達的幾何與參考姿勢也限制最終誤差。


Rust 遷移計畫與驗收：[CHARACTER_EDITOR_MCP_RUST_PLAN.md](../../Docs/CHARACTER_EDITOR_MCP_RUST_PLAN.md)。舊 Python server／搜尋實作已刪除（2026-10-22）；`client.lua` 與 `verify_live.lua`（`Tools\lua\lua.exe` 執行）是開發驗收工具，實際啟動的是 Rust 執行檔。

最終驗收：Rust 16 項測試、Registry、RustLiveR5 的 20 項實機案例全部通過。版本與證據索引：`Saved/Tests/CharacterMCP/RustValidation.json`。

## 雙人互動物理調參（`ok-pair-mcp`）

同一個 crate 的第二個執行檔，用來調整一對一互動場景的 PhysX 次級動態（頭髮、胸、裙、飾品）。原本是 OK_Demo 的 `Scripts/mcp/ok_demo_mcp.py`。

1. 啟動場景：`Tools\lua\lua.exe Tools\pair_ctl.lua start`（加 `--background` 可不搶焦點）。場景的控制埠是 `127.0.0.1:47871`。
2. 在 MCP 客戶端加入下面這段設定：

```json
{
  "mcpServers": {
    "openkoikatsu-pair": {
      "command": "C:/portable/OpenKoikatsu/Tools/character_mcp/bin/ok-pair-mcp.exe",
      "args": ["--port", "47871"]
    }
  }
}
```

工具：
- `pair_exec`、`pair_get_cvars`：執行或讀取 console 指令與變數。
- `pair_reset`、`pair_stats`：歸零或讀取穿模、抖動、效能統計。
- `pair_measure`、`pair_sweep`：量測一組設定，或對某個參數掃一串值。
- `pair_pose`：切換稽核用的雙人動作。
- `pair_tunables`：列出可在執行中調整的參數。

各後端調好的參數組比較：`ok-pair-mcp.exe compare [--seconds 10] [--rounds 3] [--steady]`。
