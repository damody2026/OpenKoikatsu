# OK 模型編輯器（.okmodel 視覺化與編輯）

> 2026-10-15 自 OK_Demo（`E:\PhysX\UE5.8\Samples\OK_Demo`，已退役）移入本專案。路徑與參數已改為本專案的名稱：OK_Demo → OpenKoikatsu、`Scripts/*` → `Tools/*`、`-Demo*` → `-Pair*`。整合計畫見 `Docs/PHYSX_ENGINE_PORT_PLAN.md`。

## 目標

在 Unreal Editor 中開啟、檢視、編輯 OK 基礎庫的 `.okmodel`（每個 Unity 來源轉成的 OK 模型），
改動會套用到所有用到該模型的角色卡（基礎庫模式開卡時生效）。

## 範圍

| 功能 | 說明 |
|---|---|
| 開啟 | 內容瀏覽器匯入 `.okmodel`（或從基礎庫瀏覽），以專屬資產編輯器開啟 |
| 內容樹 | prefab、網格（含停用的渲染器）、材質、貼圖、骨架、動態骨／碰撞體、表情設定 |
| 預覽 | 3D 視窗顯示選中的 prefab（原始姿勢、材質與貼圖）；可切換衣服穿脫狀態、顯示骨架 |
| 編輯 | 材質顏色／數值／貼圖槽、替換貼圖（PNG）、網格顯示開關與位移旋轉縮放 |
| 存檔 | 寫入模型旁的 `<id>.okedit.json`（覆寫層）；原始轉換資料不動，刪除覆寫即還原 |
| 生效 | 基礎庫模式下 `material_named`、`meshes_for_maindata`、`texture_png_named` 等查詢套用覆寫 |

## 架構

- **Rust（ok-extract）**
  - `okmodel_view.rs`：以重播模式開啟模型（基礎庫＋id），列出內容、取 prefab 網格（貼圖引用還原後的完整資料）、取貼圖 PNG、材質、骨架。
  - 覆寫層：`models/xx/<id>.okedit.json`，在 `ParsedBundle` 的查詢包裝中套用（只在基礎庫模式）。
  - FFI：`ok_okmodel_info`／`open_session`／`edits_get`／`edits_set`／`texture_png`。
  - 預覽 session（`ModelView::prefab_session`）**走卡片流程的同一條路**，不另寫一套：
    1. 角色（role）：由基礎庫重播的遊戲清單（原版清單＋來源 zipmod 的清單）以「來源 bundle 檔名＋prefab 名」查出類別，
       `role_for_category` 轉成遊戲用的 role（衣服 105→`clothes_top`、107→`clothes_bra`、頭髮 101→`hair_b`…）。
    2. 清單貼圖：衣服的 MainTex／ColorMask 來自清單項目（`CreateClothesTexture`），以物品預設色（材質 `_Color`..`_Color4`）烘焙。
    3. 姿勢：蒙皮網格用 prefab 自己的渲染器骨架（`smr_skel`）擺成綁定姿勢後轉為靜態網格（沒有角色骨架）。
    4. 收尾：`push_meshes`（遮罩、髮色、Material Editor 規則、混合模式、SessionPart），無卡片資料。
    5. 遊戲設定（toon ramp、線寬、陰影）由基礎庫（等同 game root）讀取。
  - 指令列：`--oklib model-view <lib> <id> [prefab]`、`model-texture <lib> <id> <貼圖> <out.png>`、
    `model-diff <lib> <id> <prefab> <card|->`（預覽 part 與卡片 session 中同名 part 的材質／設定差異；`-` 印出預覽 part）。
- **UE**
  - `UOKModelAsset`：記錄基礎庫路徑與模型 id（不複製資料）。
  - Factory（匯入 `.okmodel`）＋ AssetTypeActions。
  - 資產編輯器：內容樹、預覽視窗（重用既有 session → 網格／材質建構）、屬性面板（材質參數、網格變換）、存檔／還原。
  - 模組 `OKModelEditor`（KKUnityBundle 外掛內，Editor）：`UOKModelImportFactory`、`UAssetDefinition_OKModel`、`FOKModelEditor`。
  - 主控台指令：`OKModel.Open <file.okmodel | 資產路徑>`（檔案會先匯入 `/Game/OKModels/OK_<id>`）、
    `OKModel.EditorSelfTest [exit]`（結果寫到 `Saved/OKModel/editor_selftest.txt`，視窗截圖 `Saved/OKModel/<id>.png`）。

## 使用方式

1. 內容瀏覽器 → Import → 選基礎庫 `models/xx/<id>.okmodel`（或主控台 `OKModel.Open <路徑>`）。
2. 左側 Contents：點 prefab 切換預覽；點網格加入網格編輯（隱藏／位移／旋轉／縮放）；點材質加入材質編輯（預填轉換值）；點貼圖在 Texture 分頁顯示。
3. 右側 Details 修改後 0.6 秒自動套用（寫入 `<id>.okedit.json` 並重載預覽）；工具列 Apply Edits／Revert to Converted／Disabled Renderers／Refresh。
4. 存檔資產時同時寫入覆寫。覆寫在基礎庫模式下對所有用到此模型的卡片生效；Revert 刪除覆寫即回到轉換值。

## 階段與驗收

| # | 項目 | 驗收 | 狀態 |
|---|---|---|---|
| M1 | Rust 讀取：內容列表、prefab 網格、貼圖、材質、骨架 | 指令列印出某模型完整內容；網格與重播一致 | ✅ `model-view` 列出內容；prefab 角色／類別由清單查得 |
| M2 | Rust 覆寫層＋FFI | 覆寫材質顏色後開卡，該部件顏色改變；刪除覆寫後 digest 回到原值 | ✅ KK_320437 digest 改變；刪除覆寫回到 07276b7e |
| M3 | UE 資產＋匯入＋開啟（內容樹、預覽視窗） | 編輯器開啟 oo_base／髮型／衣服模型，預覽正確 | ✅ 自測：緊身衣（d46548、57771f）、原版上衣 co_top_01（粉紅娃娃裝＋蕾絲）、後髮 bo_hair_b_00 |
| M4 | UE 編輯（材質參數、貼圖替換、網格變換）＋存檔 | 修改後存檔，重開編輯器與開卡都反映修改 | 🟡 材質顏色已自測（改色→覆寫檔寫入→預覽 session 值改變→還原刪除覆寫、值回原值；開卡生效見 M2）；貼圖替換、網格隱藏／變換的 Rust 套用已寫，UE 端尚未自測 |
| M5 | 文件與錯誤紀錄 | Plan／Errors 更新 | ✅ PhysX_Errors #108–#112 |

## 已知限制

- 預覽是「單獨穿這件、預設顏色、綁定姿勢」：依賴其他衣物或角色卡的效果（外層衣服的遮罩、卡片顏色／花紋、
  `p_o_bodytights` 這類共用網格在卡上換成服裝道具材質）不會出現。例：kyriel 緊身衣單獨穿時腿部被遮罩隱藏。
- 部分貼圖在原始 bundle 中就沒有可解碼影像（如 1 像素的 `_mc` 遮罩占位），Texture 分頁顯示「no preview image」。
- 對焦範圍以所有頂點計算，含被遮罩隱藏的部分。
