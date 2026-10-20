# 編輯器背景卡片交易 R28

## 本輪計畫

1. 將圖片外觀與詳細設定共用的「保存修改快照 → 原生請求 → 讀回完整卡片」移至背景工作。主執行緒只擷取值資料與提交已完成結果，不在工作執行緒操作 UObject。
2. 背景工作持有獨立卡片快照；來源、輸出與請求使用唯一檔名。任一階段失敗不提交，保留現有角色及未套用修改。提交前核對角色、服裝、載入世代。
3. 記錄輸入保存、原生請求、輸出解析與原生來源建置／最後保存各階段耗時；保留實際 100 ms／60 FPS 驗收門檻。
4. 建置後執行背景交易回歸、編輯器既有回歸及真實 GUI 飾品驗收，檢視假髮位置與保存結果。新錯誤追加至 CHARACTER_EDITOR_ERRORS.md。

## 範圍與尚未完成

R27 已證明來源可只擷取指定欄位，但 UE 仍重建整套顯示元件。本輪背景卡片交易消除主執行緒的保存／解析工作，不等於已完成 renderer 保留、可見更新與最終保存分離，亦不保證達成 100 ms／60 FPS。仍需依新的分階段實測決定下一個瓶頸，不能以平均 FPS 或跳過背景採樣宣稱通過。

## 已實作

- 新增值資料工作 FOKAppearanceCardJob：獨立修改快照在背景保存、執行原生請求、讀回完整輸出；失敗不提交。工作入口明確檢查不在遊戲主執行緒。
- 圖片式外觀套用／新增空欄、詳細設定讀取、詳細設定套用均接入同一流程；原始卡片不覆寫，提交檢查角色、服裝及載入世代。UObject／顯示更新仍只在主執行緒。
- Saved/AppearanceEditor/*.request.json.timing.json 保存 snapshot_ms、queue_ms、input_save_ms、native_request_ms、output_read_ms、worker_thread 與成功／錯誤狀態。
- 原生 *.verified.json 新增 timings_ms，區分 source_write、preview_build、metadata_hash_write 及 through_card_write。原有資源與完整 KKEx 保存契約不變。
- 新回歸涵蓋背景執行、快照隔離、完整模組區塊保留、讀回結果、各階段失敗及無輸出卡片的目錄讀取。

## 目前驗證

EditorAsyncCardNativeR28：原生正式單欄卡片新增／重置／舊卡 fallback 測試通過（1 passed）。新增只建 2 renderer，保留 34 renderer 與 6 套其他服裝。來源更新 1751.306 ms；receipt 的 preview_build=1687.0746 ms、metadata_hash_write=56.3081 ms。此為原生測試，非點選至可見時間。

EditorAsyncCardBuildR28 建置成功（118.08 秒）。EditorAsyncCardIntegrationR28 的既有 12 項回歸通過；新交易測試的保存失敗 fixture 假設錯誤，已記錄 E95、修正後以新標籤重驗。第一次失敗不刪除，也不列為通過。

## 完成驗證與限制

- EditorAsyncCardRepairBuildR28 建置成功（82.46 秒）。EditorAsyncCardIntegrationFinalR28 共 13 項 UE 回歸成功／退出碼 0，包含新的背景交易測試；原先 fixture 錯誤保留為 E95。
- EditorAsyncCardPropertiesR28 使用已清空 accessory1 的 reset fixture，前 7 項控制項成功，之後因空欄沒有可變動幾何而失敗，已記錄 E96；無截圖，不列為完整通過。
- EditorAsyncCardPropertiesOriginalR28 使用原始 C000 卡，13 項 GUI 全部成功／退出碼 0。髮色、眼線／瞳孔、材質顏色、飾品旋轉／縮放／綁骨架、另存讀回、半脫及穿回均通過。截圖已檢視，未編輯假髮的來源與頭部高度保持正確。
- 正式詳細設定背景作業：snapshot=7.1711 ms、input_save=190.7156 ms、native_request=184743.8823 ms、output_read=103.9558 ms，worker_thread=true。完整七套外觀重建仍很慢，不能把「背景執行」當成「立即完成」。
- EditorAsyncCardExtraR28 的 8 項 GUI 功能成功，但完整嚴格驗收 **valid=false／退出碼 1**。切換 7868.7986 ms；37,890 幀平均 310.7176 FPS、最低 7.22646 FPS、最長幀 138.3804 ms，18 幀超過 16.667 ms。全部幀均包含可見後至背景衣物／材質預載完成，未剔除長幀。
- 飾品正式背景作業：snapshot=12.7553 ms、queue=13.4431 ms、input_save=245.3815 ms、native_request=4150.0253 ms、output_read=175.8237 ms，worker_thread=true。正式 receipt：single_slot=20、parts_rebuilt=1、parts_reused=34、unchanged_outfits_reused=6；preview_build=3844.6628 ms、metadata_hash_write=112.5348 ms。副本已保存到該 GUI 目錄的 card-job-timing.json、single-slot-receipt.json。
- 飾品驗收啟動／採樣中／結束均另有 E:/PhysX UE 程序，環境及 GPU 資料已保存；不能作隔離改善比較，也不能直接斷言外部負載造成失敗。二進位雜湊在驗收前後一致，截圖已檢視，假髮位置正常。

背景卡片交易本身已完成並通過功能驗證。**100 ms／完成後持續 60 FPS、UE 未變動 renderer 的保留、背景衣物來源重用、可見更新與最後保存分離，以及功能盤點的其他缺項仍未完成。**

原始證據：Saved/Tests/AnimationFilter395/EditorAsyncCardIntegrationFinalR28.log；Saved/Tests/EditorAcceptance/EditorAsyncCardPropertiesOriginalR28、EditorAsyncCardExtraR28。錯誤紀錄 E93–E97 保留本輪搜尋／路徑錯誤、兩個測試 fixture 修正與效能失敗。

EditorAsyncCardClothesR28：7 項 GUI／32 次切換全部成功／退出碼 0，最長 155.8938 ms。形變、骨架、左右手、動畫時鐘、暫停、幾何與遮罩正確；截圖已檢視。此測試沿用原有 200 ms 門檻，因此不能稱為符合 100 ms。

本輪背景交易功能已通過。續作將優先移除來源目錄的重複解析，以具檔案變動失效的共用快取處理，並保留所有完整來源契約測試。
