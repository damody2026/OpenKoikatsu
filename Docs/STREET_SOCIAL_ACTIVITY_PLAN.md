# StreetSocial 活動點功能補完

依核准計畫7.1，由主代理直接實作；目前先做功能，不做效能優化。

1. 到達活動點時記錄 DwellPoint。推進下一個路線點不改變目前休息／觀看類型；交談 Held 暫停停留計時，結束後恢復。
2. 通用活動點占用：已有停留者／交談者優先；同時接近時使用穩定 slot 順序決定先後，其他人在入口等待。等待不跳過目的地，活動點空出後繼續。
3. 原生測試覆蓋各活動類型、Held恢復、占用競爭與釋放；正式runtime自我測試使用本場景的實際路線活動點和角色狀態，要求核對旗標。
4. Development／Editor編譯，原生測試與六人語音／好感測試通過後更新原CMD。測試使用獨立存檔，保留APNGruntime與原百人demo。

2026-10-08：本輪實作及功能驗證完成。Development／Editor、26 項 Python、12 項原生與六人實際 Tick／語音／存檔測試通過；公開 CMD 已更新 StreetActivity02。完整證據見 STREET_SOCIAL_ACTIVITY_VALIDATION.md，完整人工驗收仍留最後。
