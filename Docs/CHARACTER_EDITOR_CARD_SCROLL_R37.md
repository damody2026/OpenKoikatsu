# 角色卡連續捲動清單 R37

1. 移除角色卡上一頁／下一頁及每頁 40 張限制，改用 UListView 虛擬化連續列表。清單填滿左側剩餘高度，顯示可拖動捲軸；目錄切換回到頂部。
2. 全部卡片作輕量 item，只產生可見列。沿用原本索引點選，重用列時重新綁定索引與完整路徑，避免點錯卡。
3. 名稱 metadata 只對可見列單一背景工作讀取，立即顯示檔名，不一次讀取整目錄大型 APNG。目錄 generation／完整路徑驗證後才更新，錯誤只嘗試一次；選卡已取得資料直接回填。
4. 建置後檢查載入期間禁止選卡仍有效，並以實際 100 張目錄 GUI 驗證捲到末尾、回到頂部、選取末張並載入 3D。截圖與結果保留。

## 實作與驗收結果

左側改成 UOKCardListView 虛擬化連續清單，使用正式 /Game/UI/WBP_CardListEntry 資產。清單在垂直欄位中 Fill 剩餘高度，捲軸位於清單右側；卡片數量標籤取代頁碼，全部 100 張都能直接到達。可見列才逐張於背景讀取名稱；舊目錄／路徑結果不發布，避免整目錄大型 APNG 讀取及按鈕建立。原生列依目前 list item 身分綁定索引，卡片編輯改變路徑後也不會誤認為過期列。

EditorCardScrollBuildR37 初次失敗已記錄 E121。EditorCardScrollBuildFixR37（115.64 秒）、EditorCardScrollEntryBuildR37（83.58 秒）、EditorCardScrollFinalBuildR37（83.47 秒）成功。最終 GUI EditorCardScrollFinalGuiR37 valid=true、退出碼 0、5 項全部通過：

- 完整 100 張卡片皆在同一清單，沒有上一頁／下一頁；清單底部不超出視窗，可見列數小於總數，確實虛擬化。
- 捲到最後一張 C099，跨過原本 40／80 張分頁邊界。
- 最後一張按鈕可用、中心位於清單可視範圍，實際列點選 callback 正確選中 C099 並讀取卡片。
- 從最後一張卡片載入人物編輯器 3D 成功。
- 回到卡片介面後能捲回第一張。

頂部 top.png、底部 bottom.png 及 report.png 已檢視，底部 C099 位於畫面內，捲軸與卡片列均存在。正式 GUI 保存於 Saved/Tests/EditorAcceptance/EditorCardScrollFinalGuiR37；啟動紀錄有 DLL SHA256，腳本已核對結束時一致。此輪驗收操作使用原生清單捲動 API 及列 callback，沒有將程式化驗證稱為實體滑鼠拖曳測試；捲軸拖曳／滾輪由 UE 標準 SListView 提供。

前兩個 GUI 標籤因錯誤模式及原生列類別限制失敗，保留 E123、E124 與原始截圖，不計通過。修正後使用正式 Blueprint，未修改引擎或偽造 cooked 狀態。建置後需要重啟仍載入舊 DLL 的程式，才會顯示新版介面。

最終 DLL 的 EditorCardScrollFinalLoadingR37：LoadSelection Success／退出碼 0，載入中禁止選卡、完成後恢復操作仍有效。與 GUI 結果合計完成本次清單修正驗收。
