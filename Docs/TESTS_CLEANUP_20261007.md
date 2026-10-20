# Saved/Tests 清理紀錄（2026-10-07）

使用者要求清理 Tests 不需要的資料。範圍限定 `C:\portable\OpenKoikatsu\Saved\Tests`。

- 清理前：109904376652 bytes，109.904 GB。
- 計畫刪除：1407 個可重建／已被取代的檔案，共 40.766 GB。
- 保留報告、截圖、JSON fixture、角色 .okpreview／APNG、匯入資產、manifest 宣告的 snapshot 模組、近期／載入中的檔案及 junction。
- 保留目前 GPU coating trace、先前濕面 CPU 基準 trace，以及最新 Reader trace。
- Reader302HostR1 已由主專案取代，依據 Docs/READER_FRAME_BUDGET302.md；保留其專案設定、來源及資產，只清建置輸出。
- 逐檔驗證絕對路徑及所有上層目錄，拒絕 reparse 路徑；不執行整個目錄的遞迴刪除。
- 詳細目標：TESTS_CLEANUP_20261007_plan.json；執行 journal：TESTS_CLEANUP_20261007_deleted.jsonl。
- 邏輯容量與磁碟釋放量分開量測，hard links 或其他程序寫入可能造成兩者不同。

狀態：已完成刪除與保留資源核對。

完成：1407 個檔案刪除成功。剩餘 69.138 GB；邏輯容量減少 40.766 GB。C 槽可用空間觀察增加 40.767 GB。

驗證：69221 個角色資源／截圖／匯入資產／材質資料檔案仍存在且長度不變；全部 1,407 個刪除目標已不存在，journal 全部 removed；3 個保留 trace 存在；主專案 4 個 runtime DLL SHA256 完全不變。
