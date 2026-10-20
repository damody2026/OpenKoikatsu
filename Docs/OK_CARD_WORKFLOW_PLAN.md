# OK 卡工作流程計畫（2026-10-16）

目標：只靠 OK 共用資源（`Content/IndependentResources/base_v1`）與 OK 卡就能還原、轉換、編輯人物。舊版 okPv APNG 卡不再維護（會淘汰）。

## 驗收項目與現況

| # | 項目 | 現況 | 缺口 |
|---|---|---|---|
| 1 | OK 共用資源 + 新 OK 卡 → 人物，不需原作遊戲 | 115 張 OK 卡封閉重開（只有基礎庫與卡內包）全部等於原作的遊戲模式 digest；StreetSocial 自測 A、B 組用 OK 卡通過 | 驗收工具沒有證明「完全沒讀遊戲目錄」 |
| 2 | OK 共用資源 + KKS 卡 + 角色 zipmod → 人物 | 113/115 張製作時結果等於原作 | 2 張（兔田佩克拉、kks_重绘琪亚娜）製作時拿到基礎庫只記了代號、沒轉換過的模型，查詢結果錯，飾品挑錯來源 |
| 3 | OK 共用資源 + KKS 卡 + 角色 zipmod → 新 OK 卡 | CLI `--oklib card` / `cards`，約 13 秒一張 | 遊戲與編輯器裡沒有入口 |
| 4 | 共用資源的模型可編輯、另存新檔 | OKModel 編輯器：材質顏色／數值／貼圖槽、換貼圖、網格隱藏與位移旋轉縮放，存成 `<id>.okedit.json` 疊加；自測只驗了材質顏色 | 沒有另存新檔；開卡快取鍵沒算進編輯檔，改了模型已快取的卡看不到；換貼圖與網格編輯沒有自測 |
| 5 | OK 卡的人物、衣服、動畫可在 Unreal 裡可視化編輯 | 動畫編輯器可用 OK 卡當預覽角色；`.okclothes` 匯入／匯出／換穿只在遊戲 UI | Unreal 編輯器沒有人物（身體、臉）與衣服的編輯工具；換穿後 OK 卡的模型清單與卡內包不會更新 |
| 6 | OK 卡的人物參數可在遊戲裡可視化編輯 | 身體、臉滑桿與名字存檔後仍是正常 OK 卡 | 外觀編輯（換道具、細部屬性）存檔會寫出「okLb + 舊 okLc + okPv」混合卡，新加的 mod 道具只在 okPv 裡，重開時遺失 |

## 設計

### 共用：從編輯後的卡重做 OK 卡
- `oklib::remake_ok_card(lib, game, card_bytes) -> OK 卡 bytes`：先拿掉 `okLb`／`okLc`／`okPv`，剩下的就是原作 KKS 卡的資料，再走 `make_ok_card` 的流程：基礎庫、卡片自己的 zipmod、原作模式實際用到但卡沒列出的 zipmod。結果是乾淨的 `okLb + okLc`。
- FFI `ok_okcard_make(lib, game, card_in, card_out, err, cap)`，C++ 包成 `FOkSession::MakeOkCard`。
- 遊戲與編輯器所有「存 OK 卡」的地方都走它，okLb 的模型清單、digest 和卡內包就不會過期。

### 1. 不需原作遊戲
- `okcard-check` 封閉重開時加檢查：後備來源沒有被使用（`gamefs::fallback_used()` 為 false），也沒有讀到原作遊戲目錄底下的檔案（gamefs 計數）。
- 驗收：115 張重跑，全部 MATCH 而且「遊戲讀取 0」。

### 2. KKS 卡 + zipmod 還原
- 錄卡內包時，若 zip 項目只是基礎庫的模型代號、基礎庫卻沒有該模型（從沒轉換過），就改讀原作安裝目錄的真檔，結果錄進卡內包（`gamefs::set_record_source`）。
- 缺口清單解析修正：模型 ID 取整行第一個 32 位十六進位字。
- 驗收：115 張「製作時 digest = 原作 = 封閉重開」。

### 3. 轉換入口
- 遊戲／編輯器主控台指令 `OK.MakeOkCard <KKS 卡> [輸出]`，走 FFI。
- 批次仍用 `Tools/convert_crowd_okcards.lua` 與 `--oklib cards`。
- 驗收：用主控台指令轉一張，再用 `okcard-check` 驗。

### 4. 模型編輯與另存新檔
- 另存新檔：FFI `ok_okmodel_save_as(lib, id) -> 新 id`。新 id 由「原 id + 編輯內容」雜湊而來，寫入 `models/xx/<新id>.okmodel`（原模型複本）與 `<新id>.okedit.json`（含換用的貼圖），成為基礎庫裡獨立的新模型，原模型不受影響。編輯器工具列加「Save As New Model」，完成後開啟新模型。
- 開卡快取鍵：模型編輯存檔時更新 `base_v1/edits.stamp`，快取鍵納入它，改過模型後卡會重建。
- 自測補上：換貼圖、網格隱藏與位移，另存新檔後重開新模型確認編輯還在、原模型不變。

### 5. Unreal 編輯器裡編輯 OK 卡
- 新資產 `UOKCardAsset`（匯入 OK 卡 PNG，記錄卡路徑）與編輯器 `FOKCardEditor`：
  - 預覽視窗：以 OK 卡開 session 顯示人物（基礎庫，不需原作遊戲）。
  - 人物：身體、臉參數（Details 面板滑桿），即時套用到預覽。
  - 衣服：選服裝組、匯入／匯出 `.okclothes`、換穿。
  - 動畫：選 `.okanim` 播放預覽，可開 OKAnimation 編輯器。
  - 存檔：寫回卡片資料區塊，再走 `remake_ok_card` 產生 OK 卡。
- 自測 `OKCard.EditorSelfTest`：開 OK 卡、改一個身體參數、換一件衣服、播一段動畫、存檔、重開確認。

### 6. 遊戲內外觀編輯
- `appearance::export_edited`：輸入是 OK 卡時，不走 okPv 的 `portable::shared*`，改成寫回編輯後的資料區塊再 `remake_ok_card`。輸出不含 okPv。
- `okclothes` 換穿：輸入是 OK 卡時同樣重做 OK 卡。
- 遊戲的 Save（`OnSave`）：OK 卡存檔後重做 OK 卡；okPv 專用的步驟（okpreview 複製、`CopyPortableClothingStates`）對 OK 卡跳過。
- 快速頭部貼圖路徑（`live_head`）只支援 okPv，OK 卡走完整重建。
- 自測：對 OK 卡套一個外觀道具與一個細部屬性，輸出只有 okLb + okLc、沒有 okPv，封閉重開的結果等於原作模式套同樣編輯的結果。

## 順序
2 → 1 → 3（含共用的 remake）→ 6 → 4 → 5，最後整體驗收：
- 115 張轉換三方一致、遊戲讀取 0；
- `OK.MakeOkCard` 主控台轉換；
- OKModel 編輯器自測（含另存新檔）；
- OKCard 編輯器自測；
- 遊戲內外觀編輯自測；
- StreetSocial 自測。

## 執行結果（2026-10-16）

| # | 結果 |
|---|---|
| 1 | `okcard-check ... live warm`：115 張全部 MATCH，`gaps=0 game_reads=0`（封閉重開期間經 gamefs 對原作安裝目錄的存取數為 0，後備來源未使用）。通過的卡寫入遊戲開卡快取 |
| 2 | 115 張「製作時 digest = 原作遊戲模式 = OK 卡封閉重開」。兔田佩克拉、kks_重绘琪亚娜 的差異來自 MaterialEditor 著色器查找的程序全域快取只以著色器名稱為鍵：第一次嘗試找不到（卡沒列出那個著色器包 zipmod），補上 zipmod 重試時仍讀到「找不到」。鍵改為「根目錄 + 後備來源 + 著色器」 |
| 3 | `remake_ok_card`（任何卡 → OK 卡），FFI `ok_okcard_make`，C++ `FOkSession::MakeOkCard`，主控台 `OK.MakeOkCard <card> [out]`；CLI `--oklib card/cards` 也接受已是 OK 卡的輸入。批次 115 張 581 秒（12 程序） |
| 4 | OKModel 編輯器：工具列「Save As New Model」，FFI `ok_okmodel_save_as`（新 id = 原 id + 編輯內容的雜湊，複製模型與替換貼圖，原模型不變）；模型編輯存檔更新 `base_v1/edits.stamp`，開卡快取鍵納入它。自測新增：網格隱藏（5 → 4 部位）、另存新檔、還原原模型不影響新模型；RESULT PASS |
| 5 | 新的 OK 卡編輯器（`Plugins/OKAnimation/Source/OKAnimationEditor/Private/OKCardEditor.*`，`OKCard.Open <png>`）：預覽、身體／臉參數即時套用（參數名稱表移到 `KKCard/KKShapeRows.h`，遊戲與編輯器共用）、服裝組、匯出／換穿 `.okclothes`、播放 OK 動畫、存成 OK 卡／另存新 OK 卡。自測 `OKCard.EditorSelfTest`：RESULT PASS |
| 6 | 遊戲內外觀編輯：輸入是 OK 卡時以其原作資料預覽（基礎庫 + 安裝的 zipmod），不再產生 okPv；存檔（`OnSave`）在卡沒有 okPv 時於背景 `MakeOkCard`。`.okclothes` 換穿在 OK 卡上自動重做 OK 卡。Rust 測試 `edited_ok_card_is_made_again_and_reopens_from_ok_data`：換穿後重做，原作 = 製作 = 封閉重開 |
