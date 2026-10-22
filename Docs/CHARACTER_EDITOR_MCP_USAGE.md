# 人物編輯器 MCP 使用說明

適用於目前 OpenKoikatsu 人物編輯模式，更新日期：2026-10-10。外部 AI 可透過本機 stdio MCP 查詢、調整人物參數，拍攝目標範圍，並依正／側／背三視圖反覆調整身形。

## 1. 快速開始

### 準備環境

- 使用已包含 MCP 功能的 Unreal 建置。
- MCP server、影像處理、截面量測與搜尋皆為 Rust 原生執行檔，使用者不需 Python、NumPy、Pillow 或 Rust 工具鏈。
- 若要從原始碼重建，開發者需 Rust/Cargo 與 MSVC 建置工具，執行：

```powershell
Tools/lua/lua.exe Tools/character_mcp/build.lua --test
```

產物：`Tools/character_mcp/bin/ok-character-mcp.exe`。Windows release 使用靜態 CRT；可攜至其他位置，並更新設定中的執行檔與信箱絕對路徑。

### 啟動人物編輯器

```powershell
& E:/PhysX/UE5.8/Engine/Binaries/Win64/UnrealEditor.exe C:/portable/OpenKoikatsu/OpenKoikatsu.uproject /Engine/Maps/Entry -game -windowed -CharacterMCP -CharacterMCPDir="C:/portable/OpenKoikatsu/Saved/CharacterMCP" -CharacterMCPCard="C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png"
```

| 選項 | 用途 |
|---|---|
| `-CharacterMCP` | 啟用編輯器 MCP 控制，必要 |
| `-CharacterMCPDir=` | 信箱目錄，必須與 MCP server 的 `--mailbox` 相同 |
| `-CharacterMCPCard=` | 自動載入指定 PNG 角色卡；省略時自行進入編輯模式並載入 3D 角色 |

每個遊戲執行個體使用不同信箱。等待角色完成載入後開始操作。

### 設定外部 AI 的 MCP 客戶端

將以下內容加入客戶端的 stdio MCP 設定。設定檔位置依客戶端而異；command 使用 Rust 執行檔的絕對路徑。亦可參考 `Tools/character_mcp/mcp-config.example.json`。

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

由 MCP 客戶端啟動 server；server 不會代為啟動遊戲。連線後先呼叫 `character_state`。目前應可列出 31 個工具，實際工具及輸入格式以 `tools/list` 回傳為準。使用同一個客戶端連線完成一輪搜尋，避免多個控制者同時修改人物。

## 2. 每次操作都要遵守的規則

1. 呼叫 `character_state`，取得目前 `session`、`revision`、`busy`、`pending_properties`。
2. 查詢參數描述，依 `kind`、`min/max`、`choices`、`supported` 使用合法值。
3. 修改請帶目前的 `session` 和 `revision`。載入、套用、還原、切換服裝等操作後重新取得狀態，不自行猜測版本遞增量。
4. 身體／臉部／骨骼等立即套用；`requires_apply=true` 的詳細屬性先暫存，呼叫 `character_apply` 才提交。
5. 出現逾時、取消或版本錯誤時，先查狀態和讀回參數，再決定下一步。已開始的原生作業可能仍會完成。

`session` 表示目前人物工作階段；`revision` 表示其編輯版本。它們是避免舊請求覆蓋新人物或人工修改的必要資訊。`editor_progress` 的 heartbeat 最多約落後 0.5 秒，只作診斷；正式寫入前仍用 `character_state`。

以下 JSON 都是 `tools/call` 的 `params`，不是 PowerShell 指令。範例中的 `<SESSION>`、其他 ID、路徑及 `revision: 1` 都須換成當前實際值；各範例獨立，不能把同一個 revision 連續套用。

```json
{"name":"character_state","arguments":{}}
```

工具結果的 `content` 包含文字 JSON，截圖／遮罩另含 `type=image` 的 PNG。先檢查 `isError`；不要把錯誤文字當作成功資料。一般 MCP 客戶端會直接顯示圖片，不必自行解析 base64。

## 3. 查詢與調整所有編輯參數

### 查目錄

```json
{"name":"parameters_list","arguments":{"session":"<SESSION>","query":"shape.body.","offset":0,"limit":100}}
```

查全部參數時省略 `query`。每頁最多 500 項；使用回傳的 `next_offset` 繼續，直到 `next_offset >= total`。`editor_controls` 同樣支援 `query`、`offset`、`limit`。

| 類別 | key／操作 |
|---|---|
| 身體 44 項 | `shape.body.0` 至 `shape.body.43`；`.0` 為身高滑桿 |
| 臉部 52 項 | `shape.face.0` 至 `shape.face.51` |
| 骨骼 | `bone.<骨名>.scale.uniform`、`scale.x/y/z`、`length`、`position.x/y/z`、`rotation.x/y/z` |
| 物理 | `physics.*`，例如 `physics.bustSoftness` |
| 腹部形變 | `pregnancy.*`；相關開關／預設另查編輯控制 |
| 詳細屬性 | 依卡片動態列舉，例如 `hair.parts.0.baseColor.0` |
| 表情、手勢、動畫、語音及其他 UI | 查 `editor_controls`，用 `editor_control_set` 或 `editor_action` |
| 頭髮、服裝、配件素材 | 查 `appearance_catalog`，用 `appearance_select` |

滑桿值通常是編輯器數值，不能直接當作公分或圍度；範圍與單位以描述為準。左右骨骼使用各自骨名，要同步修改就將左右 key 放在同一批次。`supported=false` 表示目前模型或模式不能使用；不能假定寫入會有作用。

測試卡可盤點 4,300 項資料參數及 49 個其他編輯控制。其他角色的屬性、骨骼可用性和動態控制可能不同，不應硬編碼這些數量。`stable_key=false` 的 UI 備援 key 在介面重建後要重新查詢。

### 批次修改、讀回、重設

```json
{"name":"parameters_set","arguments":{"session":"<SESSION>","revision":1,"edits":[{"key":"shape.body.0","value":0.7},{"key":"shape.body.21","value":0.4}]}}
```

```json
{"name":"parameters_get","arguments":{"session":"<SESSION>","keys":["shape.body.0","shape.body.21"]}}
```

每次 `edits` 或 `keys` 最多 256 項。批次會先完整驗證；未知 key、重複 key、錯誤型別或超出範圍會拒絕整批。數值使用 JSON number，開關使用 `true/false`，文字／選項依描述使用 string。

`parameters_reset` 接受 `session`、`revision`、`keys`，重設到可靠預設值；詳細屬性可撤銷尚未套用的變更。沒有可靠預設的欄位會回報原因。

### 詳細屬性與外觀

`parameters_set` 回覆 `pending_properties > 0` 時，使用最新狀態呼叫：

```json
{"name":"character_apply","arguments":{"session":"<SESSION>","revision":1}}
```

外觀套用可能重建資源並重載角色，冷快取可能需數分鐘。等待期間用 `editor_progress` 查看 `operation`、`phase`、`busy` 與等待條件；不要重送相同套用請求。

素材操作先呼叫 `appearance_catalog` 取得 `targets`；指定其中的 `target` 再查素材項目，使用實際回傳的項目識別值呼叫 `appearance_select`。`item=""` 重設該 target。`appearance_add_accessory` 新增配件槽後，重新查目錄及版本。素材庫須已由本機原遊戲／獨立資源設定提供。

編輯控制範例：

```json
{"name":"editor_control_set","arguments":{"session":"<SESSION>","revision":1,"key":"control.AutomaticBlinkCheck","value":false}}
```

`editor_action.command` 可用 `tpose`、`walk`、`pause`、`replay`、`blink`、`reset_left_hand`、`reset_right_hand`、`clothing`、`speak`、`stop_speech`、`load_speech`。語音來源、聲線、選項等先經編輯控制設定。

## 4. 拍攝三視圖與目標範圍

```json
{"name":"capture_configure","arguments":{"session":"<SESSION>","revision":1,"projection":"orthographic","ortho_width":250,"tpose":true,"hide_hair":true,"hide_accessories":true}}
```

可另外設定 `yaw`、`pitch`（度）、`distance`、`focus:[x,y,z]`（公分）。依角色大小調整 `ortho_width`、焦點與距離，使整個目標入鏡；一輪比較期間保留同一尺度。`body_masks` 的幾何投影 frame 與彩圖攝影設定各自管理。

```json
{"name":"capture_views","arguments":{"session":"<SESSION>","target":"preview","side":"right"}}
```

回傳正／側／背三張 PNG、各圖尺寸及版本。`side` 可為 `left` 或 `right`，預設 right。拍攝期間凍結人物 tick，完成後恢復攝影與播放狀態。

```json
{"name":"capture_region","arguments":{"session":"<SESSION>","target":"rect","rect":{"x":0,"y":0,"width":320,"height":480}}}
```

`target` 可為 `preview`、`viewport`、`rect`；座標從 viewport 左上角開始，以像素計。先讀 `viewport_width/height`，裁切範圍須在 viewport 內。截圖包含場景渲染，不包含編輯器 UI。

只設定 `hide_clothes=true` 可能仍保留服裝的身體遮罩。需要完整身體時，先經正式服裝流程移除：

```json
{"name":"editor_action","arguments":{"session":"<SESSION>","revision":1,"command":"clothing","clothing_kind":102}}
```

`clothing_kind=0..8` 切換指定服裝槽，`100` 全穿、`101` 部分脫下、`102` 全部移除。

## 5. 依三視圖自動調整身形

### 準備輸入

三張參考圖應為相同人物、相同姿勢，對應正／側／背。自動搜尋要求固定 T-pose，身體輪廓應排除頭髮、服裝與配件。一般照片先由外部 AI 處理分割、姿勢及透視；工具接受以下輸入：

| mode | 格式 |
|---|---|
| `mask` | 黑底、白色人物的二值圖，預設；一般彩色照片會被拒絕 |
| `alpha` | 背景透明的 PNG，以 alpha > 127 為人物 |
| `background` | 明確提供 `background:[R,G,B]`，以 `tolerance`（預設 24）移除與邊界連通的背景 |

圖檔必須是 server 所在電腦可讀取的絕對路徑。先移除服裝、固定姿勢、隱藏衣髮配件，並套用或撤銷所有暫存屬性。

```json
{"name":"reference_prepare","arguments":{"session":"<SESSION>","front":"C:/refs/front.png","side":"C:/refs/side.png","back":"C:/refs/back.png","mode":"mask","side_view":"right","physical_height":165}}
```

回傳 `reference` ID、固定投影 `frame` 及對齊資訊。`physical_height` 為目標真實身高（公分）；不知道時省略，僅比較比例。`side` 是圖片路徑，`side_view` 才是左／右側別。

若目標直接由 `body_masks` 產生，可將它回傳的整個 `frame` 放進 `projection_frame`，保留精確原投影；圖像尺寸必須與 frame 相同。一般參考圖在準備階段只對齊一次，之後比較及搜尋都用同一 reference，不每次重新縮放。

### 比較與選參數

```json
{"name":"body_compare","arguments":{"session":"<SESSION>","reference":"<REFERENCE_ID>"}}
```

回傳三張當前輪廓，以及每視圖 IoU、邊界誤差與綜合 loss。IoU 越高越好，loss 越低越好。

可先用 `body_sensitivity` 指定 `keys` 和 `step_fraction`，測量各參數雙向小幅變動的影響；完成後還原原值。優先調整整體比例，再處理胸腰臀、四肢及骨骼局部；需要時分組搜尋，保留每輪較好的結果。

### 啟動背景搜尋

```json
{"name":"body_fit_start","arguments":{"session":"<SESSION>","revision":1,"reference":"<REFERENCE_ID>","max_evaluations":300,"max_seconds":1800,"step_fraction":0.025,"target_loss":0.002}}
```

省略 `keys` 時使用全部 44 項身體參數；也可指定目錄中已支援、立即套用、具有 min/max 的數值 key，包含臉部與骨骼。詳細外觀暫存屬性仍由一般編輯工具操作，不放入數值搜尋。

| 選項 | 預設／範圍 |
|---|---|
| `keys` | 預設 44 項身體參數；指定時 1–128 項，不重複 |
| `max_evaluations` | 預設 160；1–2000 |
| `max_seconds` | 預設 1800；5–7200 秒；已開始的編輯仍需完成或逾時 |
| `step_fraction` | 預設 0.025；0.0001–0.5；相對於每項參數完整範圍 |
| `target_loss` | 預設 0.002；0–1；目標誤差，不代表百分比 |
| `resume` | 前次搜尋的 run ID，接續最佳值及步長 |

工具立即回傳 `job` ID，約每 1–2 秒查詢一次：

```json
{"name":"body_fit_status","arguments":{"job":"<JOB_ID>","include_images":false}}
```

- `queued/running`：查看 `progress.evaluation_count`、`best_loss` 等資訊，繼續等待。
- `completed`：讀取 `result.best_values`、`best_loss`、`stop_reason`、`report`；最佳值已寫回角色。需要結果圖片時加 `include_images:true`。
- `failed/cancelled/interrupted`：查看 error、checkpoint、`best_restored`、`recovery_error`，先查人物狀態與參數再恢復。

取消使用 `body_fit_cancel`，參數只有 `job`，再輪詢直到終止。`cancelling` 不表示已完成回復。正常完成可能因 `target_loss`、`evaluation_budget`、`time_budget` 或 `minimum_step` 停止；完成不一定已達目標門檻。

續跑時重新取得人物 session/revision，在 `body_fit_start` 加 `resume:<RUN_ID>`，並使用相同 reference、keys 及來源角色卡。job ID 用來查背景工作，run ID 用來續跑；以回傳欄位為準。服務重開可查持久化工作結果，但不會自動繼續中斷的搜尋。

`body_fit` 提供同步版本，搜尋選項相同。一般外部 AI 建議使用背景版本，避免單次工具呼叫逾時。搜尋期間不要人工修改人物；偵測到外部版本變更時會停止，保留人工修改。

### 命令列一鍵流程

遊戲需先按第 1 節啟動。CLI 會準備 T-pose／服裝狀態、載入參考、執行背景搜尋，正常完成後另存角色卡：

```powershell
& C:/portable/OpenKoikatsu/Tools/character_mcp/bin/ok-character-mcp.exe fit --mailbox C:/portable/OpenKoikatsu/Saved/CharacterMCP --front C:/refs/front.png --side C:/refs/side.png --back C:/refs/back.png --mode mask --physical-height 165 --max-evaluations 300 --save-name "三視圖身形"
```

`--keys shape.body.0 shape.body.22 shape.body.23` 指定搜尋組；`--sensitivity` 只探測並還原，不另存。已有 reference 可用 `--reference <ID>`，續跑另加 `--resume <RUN_ID>` 及相同 keys。CLI 進度走 stderr，最終 JSON 結果走 stdout。

## 6. 量測、回復與保存

- `character_measure`：回傳當前身體／頭部精確頂點 bounds、extent、骨骼 world 座標，單位公分。
- `body_geometry`：另將完整當前網格存成 JSON，回傳 `geometry` 絕對路徑；排除衣髮配件與外框。
- `body_sections`：傳入 `heights:[世界Z高度...]`，最多 128 個平面。各閉合圈包含周長、寬、深、面積及中心；不要將手臂圈加進軀幹圍度。查看 `valid/invalid_components`，不能把任意高度的圈直接命名為腰圍。
- `body_masks`：回傳幾何三視圖遮罩。可指定 `reference` 固定 frame，或 `width/height`（64–1024；預設 256 × 384）建立新 frame。
- `snapshot_create`：取得 `snapshot` ID。開始修改前建立，之後用 `snapshot_restore` 加最新 session/revision 回復。
- 快照存於編輯器記憶體，最多 32 份，綁定來源人物及 outfit；不含攝影、表情、動畫等預覽狀態，也不代替角色卡存檔。

```json
{"name":"character_save","arguments":{"session":"<SESSION>","revision":1,"name":"三視圖身形"}}
```

先確認 `pending_properties=0`。回傳 `saved_card` 新 PNG 路徑，必要的獨立資源會一起保存，不覆寫原始卡。用 `character_load` 帶該路徑及最新 session/revision 重開，再以 `parameters_get` 確認結果。

## 7. 全部 31 個工具索引

表中 **S** 表示必須帶 `session`，**R** 表示必須帶 `revision`；可選欄位與完整型別以客戶端 `tools/list.inputSchema` 為準。

| 工具 | 必要資訊／用途 |
|---|---|
| `character_state` | 無；正式人物狀態及版本 |
| `editor_progress` | 無；不排在長作業後的診斷 heartbeat |
| `character_load` | S、R、path；載入 PNG 卡 |
| `parameters_list` | S；參數目錄／分頁搜尋 |
| `parameters_get` | S、keys；讀取值 |
| `parameters_set` | S、R、edits；批次寫入 |
| `parameters_reset` | S、R、keys；重設／撤銷暫存屬性 |
| `editor_controls` | S；其他 UI 控制目錄 |
| `editor_control_set` | S、R、key、value；改控制並呼叫原生回呼 |
| `editor_action` | S、R、command；姿勢／服裝／語音命令 |
| `appearance_catalog` | S；查外觀 targets 及素材 |
| `appearance_select` | S、R、target、item；選外觀素材 |
| `appearance_add_accessory` | S、R；新增配件槽 |
| `character_apply` | S、R；提交暫存屬性並等待更新 |
| `snapshot_create` | S；建立人物資料快照 |
| `snapshot_restore` | S、R、snapshot；回復快照 |
| `capture_configure` | S、R；設定攝影／姿勢／顯示 |
| `capture_region` | S；截取目標區域 PNG |
| `capture_views` | S；固定正／側／背彩圖 |
| `character_measure` | S；精確 bounds 與骨骼位置 |
| `body_geometry` | S；完整解剖網格 JSON |
| `body_sections` | S、heights；截面與周長 |
| `body_masks` | S；三視圖幾何遮罩 |
| `reference_prepare` | S、front、side、back；準備參考與固定投影 |
| `body_compare` | S、reference；計算輪廓誤差 |
| `body_sensitivity` | S、R、reference、keys；雙向敏感度與還原 |
| `body_fit` | S、R、reference；同步有界搜尋 |
| `body_fit_start` | S、R、reference；背景搜尋 |
| `body_fit_status` | job；進度／結果 |
| `body_fit_cancel` | job；要求取消 |
| `character_save` | S、R、name；另存新卡 |

## 8. 常見問題與輸出位置

| 狀況 | 處理方式 |
|---|---|
| 找不到工具／server 無法啟動 | 檢查 Rust 執行檔的絕對路徑，重新連接 MCP 客戶端 |
| `Editor timed out`／未開編輯模式 | 確認遊戲加了 `-CharacterMCP`、信箱相同、角色 3D 已載入；再查 state/progress |
| `Character changed`／`Stale revision` | 重新查 state，重新確認角色／參數後使用新版本，不盲目重送舊寫入 |
| 暫存屬性尚未套用 | 先 apply，或 reset 撤銷；不要直接保存或開始擬合 |
| 找不到 Rust 執行檔 | 執行 build.lua 並確認 command 指向 bin/ok-character-mcp.exe |
| Mask 非二值／alpha 沒透明度 | 選正確 mode，或由外部 AI 提供合格人物遮罩 |
| `Geometry exceeds fixed projection frame` | 先用較寬投影重新準備 reference；搜尋期間不自動縮放遮掩比例差 |
| 圖中身體被衣物遮罩切掉 | 用正式 clothing_kind=102 流程移除，而非只 hide_clothes |
| 搜尋停止但誤差仍大 | 檢查姿勢、側別、遮罩、真實身高及模型表達能力，再選參數組、增加預算或續跑 |

所有輸出相對於 `--mailbox`：

| 位置 | 內容 |
|---|---|
| `images/` | 彩色截圖 |
| `cards/` | 新角色卡及必要獨立資源 |
| `geometry/` | 直接 body_geometry 匯出的網格 |
| `fitting/<reference ID>/` | 對齊後遮罩及 reference.json |
| `fitting/run-<run ID>.json` | 每次試驗、最佳值、停止原因及 checkpoint |
| `fitting/run-<run ID>/` | 最佳三視圖遮罩 |
| `fitting/job-<job ID>.json` | 背景作業持久化狀態／結果 |
| `status.json` | 編輯器診斷 heartbeat |
| `errors.md` | 執行期工具錯誤紀錄 |

三視圖不能唯一決定所有參數；未知插件、參考姿勢／透視、衣髮遮擋和既有 morph 的表達能力會影響吻合程度。實測精度及完整驗收見 [覆蓋與驗收](CHARACTER_EDITOR_MCP_COVERAGE.md)，開發錯誤及修正見 [錯誤紀錄](CHARACTER_EDITOR_MCP_ERRORS.md)。

## 9. 可直接交給外部 AI 的操作指示

> 使用 openkoikatsu-character MCP，依我提供的正／側／背三視圖調整目前人物，優先吻合身形。先讀 character_state，盤點參數與支援能力，建立快照。每次修改使用最新 session/revision。以固定 T-pose、正式移除服裝及隱藏衣髮配件準備身體輪廓；參考姿勢須匹配，複雜圖片先準備分割遮罩。有真實身高才加入 physical_height。先比較輪廓與敏感度，再分組調整整體比例和局部身形；背景搜尋使用 body_fit_start/status，必要時取消或續跑。不要在搜尋中重新縮放投影或覆寫人工修改。用 body_compare 和 capture_views 檢查結果，回報實際誤差與停止原因；最後 character_save 另存，重新載入並讀回參數確認。頭髮與衣服素材相似度不作為身形完成條件。


Rust 遷移計畫與驗收：[CHARACTER_EDITOR_MCP_RUST_PLAN.md](CHARACTER_EDITOR_MCP_RUST_PLAN.md)。舊 Python server／搜尋實作已刪除（2026-10-22）；`client.lua` 與 `verify_live.lua`（`Tools\lua\lua.exe` 執行）是開發驗收工具，實際啟動的是 Rust 執行檔。
