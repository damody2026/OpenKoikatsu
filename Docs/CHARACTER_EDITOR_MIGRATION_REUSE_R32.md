# 原作模組遷移跨執行緒重用 R32

1. 使用 R31 細部計時的證據，將 StripAll 清單由執行緒本地快取改成最多四個來源目錄的不可變共用快取。
2. 使用既有遞迴檔案通知驗證有效性；不可用通知、載入期間變更、讀取不完整均直接讀取且不發布。設定檔仍每次讀取，保持關閉遷移與原作優先順序。
3. 測試不同執行緒命中、同大小內容變動、來源刪除／重建、容量上限、不完整讀取、原作完整清單及既有原生單欄來源契約。
4. 所有原生與模式判定改動合併建置後，再跑 UE、外觀與服裝 GUI 和嚴格飾品效能；保留全部失敗。

## 原生驗證

EditorMigrationReuseFinalR32：16 passed、3 ignored，包含原作優先順序、同大小實際 ZIP 改寫、跨執行緒同 Arc、舊 Arc 壽命與四項新增快取恢復／容量／載入期間變更測試。EditorMigrationInstalledR32 另正式執行 ignored 實作清單測試成功：91 個 StripAll GUID、冷解析 2368.034 ms、20 次暖查詢平均 0.046 ms。

EditorPartResourceFinalR32、EditorMigrationSourceContractR32、EditorMigrationCardPatchR32 各 1 passed；EditorMigrationListRegressionR32 13 passed、1 ignored。共 33 項不同原生測試實際通過；其餘 ignored 不計通過。全來源、三種單欄來源、34 個保留部件、六套其他服裝、新舊格式、空欄重置與 legacy fallback 均成功。

首次單欄來源更新仍 3314.209 ms，不能稱為效能通過。細部時序：遷移解析 2030.9313 ms、來源擷取 934.8818 ms、封裝 121.1482 ms。快取縮短後續同目錄查詢，首次解析仍不可避免；GUI 首次來源準備仍需要在可見操作前適當預備，且 UE 目前還重建完整顯示。

正在合併 UE 建置；GUI 與 100 ms／60 FPS 結果尚未計入。失敗 R32 fixture 與修正見 E107。

## R32 GUI 驗收

EditorMigrationBuildR32 建置成功（309.63 秒）；EditorMigrationIntegrationR32 共 15 項 UE Success／退出碼 0，包含首次載入／CPU 請求判定與背景骨架契約。

EditorMigrationPropertiesR32：13 項通過／退出碼 0，實際貼圖顏色、飾品旋轉／縮放／綁定、假髮來源／頭高保留、保存讀回與半脫／穿著恢復成功；report.png 已檢視。EditorMigrationClothesR32：7 項、32 次切換通過／退出碼 0，最大 184.9502 ms；門檻為 200 ms，未算 100 ms 通過。

EditorMigrationExtraR32：8 項功能成功，整體 valid=false／退出碼 1。切換 5592.7258 ms，27,611 幀最低 6.0477 FPS，165 幀超預算。採樣仍包含所有背景預備，沒有刪除長幀；二進位雜湊一致。啟動時另有 E:/PhysX UE 程序，環境保存於 EditorMigrationExtraEnvironmentR32.json，不稱隔離驗收，也不直接將失敗歸因外部負載。

33 項原生與 15 項 UE 契約、13+7+8 項 GUI 功能成功，不能代表 100 ms／60 FPS 通過。續作 R33 處理第一次來源冷解析與渲染就緒提前發布，並保守處理 linked mods 外部來源。
