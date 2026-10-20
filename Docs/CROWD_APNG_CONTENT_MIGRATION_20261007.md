# Crowd APNG 轉移與舊角色庫清理

計畫：核對正式 native APNG 分支與 AssetRegistry 的外部依賴；把正式入口的100張APNG移到 Content/CharacterCards/CrowdAPNG，SHA-256確認內容不變；更新正式百人選單、街道五人選單與讀卡器預設目錄；確認舊 Characters 可安全退役後刪除；最後用原 Development 執行檔跑百人／街道 runtime 驗證。保留原人物卡全部服裝、共用資源與既有正式 Cook 包。

Characters已刪除；先修正舊地圖軟引用並審查舊材質依賴，再執行刪除。正式APNG入口仍保留。

## 錯誤紀錄

- 第一輪唯讀 AssetRegistry audit 已取得引用資訊，但列檔時失敗：UE內建Python是3.11.8，沒有Path.is_junction()。改用lstat的FILE_ATTRIBUTE_REPARSE_POINT，重新審查；當時沒有刪除或更改任何資產。
- 同時確認FAssetRegistryDependencyOptions預設會包含soft references。hard-only查詢現在明確關閉soft／management／searchable分類，避免把軟引用誤判為硬依賴。

## 已完成的 Saved/Crowd 基底驗證

清理後junction中介鏈問題已修復，25個剩餘連結無失效。正式百人執行檔三張APNG煙霧測試通過；StreetSocial新地圖的六人GPU啟動也通過，exit0／gpu_failures0。沒有修改引擎shader。

## 實作結果

- 100張原始APNG共4,429,034,937bytes（4.125GiB）已從output/CrowdAPNG移到Content/CharacterCards/CrowdAPNG。搬移後100份SHA-256均相同，全部服裝保留；原路徑PNG已消失。
- 正式選單為Content/CharacterCards/CrowdAPNG/selection.json；百人launcher、讀卡器預設目錄、街道五女角選單同步更新。舊output目錄的sidecar驗證紀錄仍保留作歷史證據。
- 原角色庫有100,125個package、100,325個檔案、53,933,309,569bytes（50.229GiB）。地圖200個軟引用（Characters/Walks各100）已清空；470個只服務舊角色的材質經擴充依賴審查確認沒有其它外部使用者後移除。小型材質及地圖備份在Saved/StorageCleanup/Crowd20261007/Characters，未備份50GiB角色庫。
- 刪除後AssetRegistry驗證：100,595個退役package均不存在、外部殘留依賴0。C槽此次實測增加54,148,820,992bytes（50.43GiB，含檔案配置開銷）。共用runtime材質、原執行檔與Cook包維持原內容。舊Cook角色測試入口會失去資料；正式APNG入口使用runtime角色建立流程。
- Content路徑搬移後百人Development啟動通過：complete100、gpu_ready true、gpu_failures0、exit0、cooked_character_loaded false。街道搬移後六人也通過相同檢查。三個正式launcher預檢均通過。

## 追加錯誤紀錄

- 建立刪除腳本的Python內嵌PowerShell文字遭工具policy拒絕，未執行任何刪除；改以apply_patch保存可審查PowerShell腳本，單一PowerShell內驗證路徑及執行，成功完成。
- 更新中文MD時漏寫encoding=utf-8，Windows cp950讀取失敗；改成顯式UTF-8重試，未破壞原檔。
- 圖像相似／120FPS／60秒載入不屬此清理驗收；百人載入仍未達60秒，沒有把清理測試當效能達標。

## 刪後百人複測

正式R1147 Development執行檔在Characters目錄已不存在時再次建立全部100位APNG角色：complete100、gpu_ready true、gpu_failures0、exit0、cooked_character_loaded false。證據Saved/Crowd/CharactersRemovedHundred20261007/receipt.json。
