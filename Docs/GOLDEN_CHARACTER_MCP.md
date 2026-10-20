# 金髮女角：MCP 角色卡調整紀錄

日期：2026-10-10。工作目錄：`Saved/GoldenCharacterMCP`。

## 基準與範圍

- 人設：`output/tripo-parts-2026-10-03/grok-complex-benchmark/tpose-v2/01-full-clothes.jpg`、`02-underwear.jpg`。
- 身體來源：使用者指定的靈砂卡；工作卡為 `Content/CharacterCards/CrowdAPNG/C0000.png`。
- 修改前完整備份：`Saved/GoldenCharacterMCP/C0000-before-character-edit.png`。
- 臉型與身形採人工正側背對照調整，未執行自動輪廓擬合；不宣稱像素一致或提供虛構 IoU。
- 服裝只替換第 1 套為近似素材；其他六套保留。刺繡、七顆裙扣、髮夾與精確裁片仍須自製素材。

## 操作

透過 `Tools/character_mcp/bin/ok-character-mcp.exe` 的 stdio MCP 和獨立信箱 `Saved/GoldenCharacterMCP` 操作。每次寫入先讀 `character_state`，使用當前 session/revision。

1. 建立完整檔案備份與 `snapshot_create`。
2. `edit_design.py` 批次修改臉型、眉眼、鼻口、耳形及部分身體滑桿，保留基準身高滑桿。
3. 重設左右瞳孔素材，移除原來菱形瞳孔，使用人類圓形瞳孔。
4. `colors.py` 設定金棕瞳色、棕色眉眼及淡妝。
5. `wardrobe.py` 選取原生洋風制服外套 207、迷你窄裙 21、厚褲襪 8、內外鞋用腰帶靴 15。窄裙主色層使用格紋 B（9）。
6. `clear_skin_overlay.py` 在工作副本移除 KSOX 皮膚覆蓋。其他插件項目保留原始 MessagePack 位元組；重新排列區塊表。此副本仍有舊的渲染快照，**必須再做完整外觀重建**，不能直接當最終卡。
7. `final_colors.py` 透過 MCP 調整深綠／酒紅／米白／金色服裝配色與原卡眼線材質顏色覆寫，再 `character_apply` 重建。
8. MCP 另存後重新匯入原有柔性頭髮。外觀重建會從原遊戲素材重建，因此自訂 Tripo 頭髮須在最後重新安裝。

參數批次分別記錄於 `design-edits.json`、`color-edits.json`、`final-color-edits.json`。素材識別值以 `wardrobe-selections.json` 為準，不能跨目錄快取硬編碼舊 hash。

## 頭髮與舊裝飾

使用同一份 `native-hair/hair-soft.json` 的幾何、材質和物理設定。工作配方 `Saved/GoldenCharacterMCP/hair-soft-design.json` 新增 `hide_source_accessory=true`，並把貼圖路徑改成絕對路徑，避免搬移配方後找不到圖檔。

原靈砂頭髮與頭飾是同一個 accessory source。匯入器保留新頭髮並隱藏同一來源中其他裝飾網格，不刪除其幾何。`hide_source_accessory` 預設 false，不改既有匯入的行為。

頭髮為 49,282 三角面、9 條柔性鏈、49 個骨骼。匯入器會逐套讀回七套服裝並驗證頭髮權重及非頭髮幾何；180 幀動態檢查確認柔性頂點有位移而固定頭皮保持固定。

服裝顏色滑桿使用 sRGB；不能直接填線性色值。此原生外套的主要可見裁片採用色層 2，附屬裁片還會讀色層 4。深色在目前渲染中還會明顯變暗，工作配色因此調亮為青綠 `[.30,.55,.58]`、酒紅 `[.50,.22,.32]`；這是針對實際輸出的調整，尚未證明渲染變暗的完整成因。配色以實際獨立重拍為準。MCP 重建後載入的卡，剛載入時的 T-pose 會出現服裝落在腳底，切過走路姿勢後才恢復；當時誤判為暫時現象，實際上是下節的讀卡錯誤。

## 本次發現的 MCP 問題

素材目錄回傳 `entry.category_no`，原 MCP 卻讀頂層 `category`，造成指定 target 時清單為空、非空素材選取被拒絕。`OKCharacterMCP.cpp` 新增相容兩種格式的分類讀取，實際驗證上衣清單 3,562 項及多次非空素材選取成功。

MCP `character_save` 寫出的 okPv 不含 `body_shaped`／`head_shaped` 身形基準（第一個 FaceBody checkpoint 起即如此）。讀卡器在 T-pose 會每幀呼叫 `ok_static_interaction_frame`，`reset_animator_pose` 把 `body_skel` 重設成不存在的基準，`body_skel` 變成 `None`，以身體骨架蒙皮的服裝因此全部塌到原點附近（腳底）。走路取樣路徑有補建基準，所以切換姿勢後會恢復。修正（`Tools/ok-extract/src/session.rs`）：
- 產生端：`SessionBlob::from_session_selected` 寫完整資料包時，若 session 從未執行 `apply_shape`，就以 rest 骨架加卡片身形值補算 `body_shaped`／`head_shaped` 再寫入。重跑 MCP 流程（`parameters_set` → `character_apply` → `character_save`）後，新卡帶有兩個基準，且與重算結果的差值為 0；剛載入時 T-pose 的服裝位置正確。
- 讀取端：`reset_animator_pose` 在基準缺失時先呼叫 `apply_shape` 補建，且不再把現有骨架覆寫為 `None`，讓已存的舊卡（含目前的 C0000）也能正確顯示。

建置時缺少 `UnrealEditor-KKRuntimePose.lib`，以現有原始 import-library response file 補回；接著 Unreal Development 建置成功。沒有中止其他工作程序。

## 交付驗證

已交付 `Content/CharacterCards/CrowdAPNG/C0000.png`，原檔備份為 `Saved/GoldenCharacterMCP/C0000-before-character-edit.png`。

- 以獨立 Unreal 程序重拍臉部、正面、側面、背面四幀 APNG，並人工確認最終配色與頭髮顯示；原生服裝剪裁與人設仍有差距。
- PNG 所有區塊 CRC 通過，四個 APNG frame control 完整；重拍前後角色資料尾段與 `okPv` 模型資源相同。
- `ok-preview-pack verify` 成功讀取七套服裝、52 個臉部與 44 個身體通道。
- 正式 C0000 透過 MCP 重新載入成功，全部 96 個臉部／身體參數與最終設定相符（誤差小於 1e-6）。
- 頭髮匯入逐套讀回、柔性動態檢查與其他幾何保留檢查通過。49,282 三角面、9 條柔性鏈、49 個骨骼。
- 交付卡 56,323,635 bytes，SHA256 `99a8e726a92c1b00e290cfd33c1e32655bfeab128abfddfa0d50c1e383c3fb3a`。

驗證紀錄：`Saved/GoldenCharacterMCP/delivery.json`、`final-pack-verify.json`、`hair-import.log`。這是人物與原生近似服裝的第一版；不宣稱刺繡、袖片、裙扣、鞋型或臉部輪廓已完全還原人設。

