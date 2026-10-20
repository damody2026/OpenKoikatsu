# StreetSocial 候選切換補完計畫

本輪補核准計畫 4.2：附近可交談 NPC 依鏡頭朝向與距離排序，玩家可切換候選。

1. 延用女性、250 cm 內、視線可達、角色淡入與動作就緒等判斷；新增明確而確定的鏡頭排序與 Tab 循環切換。
2. 手動選擇維持至候選無效或交談結束，提示與 E 同源；E 重新驗證後不悄悄改談另一位。
3. 通用 runtime APNG 路徑，不依卡名、私人 Cook 或每幀重新讀卡；原語音、效果、取消、身分與保存不變。
4. Grok Build 4.7 fast 僅實作有界 source/test/docs；主代理獨立審查、Development/Editor 編譯、原生測試、實際六人語音好感測試，再更新公開 receipt。
5. 記錄錯誤和實際每幀平均/P95；不將本輪功能測試當作最終人工視窗、百人或長時間驗收。

已完成本輪功能實作與獨立驗證；公開 CMD 更新 StreetCandidate02。11 項原生與 26 項 Python 測試、六人實際場景通過；詳細證據、性能限制及 Grok 用量見 STREET_SOCIAL_CANDIDATE_SELECTION.md。完整人工驗收延後。
