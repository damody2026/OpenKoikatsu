# 女角關係勾選框驗證（2026-10-08）

右下「女角數值」新增陌生、朋友、戀人三個互斥 checkbox，直接保存選定女角的 Stage，其他數值不變。重按目前選項不會清空關係。勾選讀取最新 Store，告白或其他互動變更關係後自動更新。切換時取消舊關係尚未提交的語音、話題、觸控與告白結果；維持當前女角及一對一場景。

- Development 建置成功；StreetSocial.cmd 指向 StreetRelationChecks01.stage。
- 執行檔 SHA256：fc07f51a795375205cb41d8c53859deffd3e6ce704321741c191d8a027e30be7。
- 實機證據：Saved/StreetSocial/Runtime-StreetRelationChecks01Presentation/social.validation.json。
- 五位女角 × 三種關係，共 15 個真實 Slate ToggleCheckedState 回呼案例，逐次重讀存檔、檢查互斥勾選；驗證第一位女角六個數值保留、外部 Stage 更新同步。
- 同次驗證五位女角站姿、六位角色跑步、話題展開、滑桿雙向更新及畫面布局；passed=true，errors=[]。
- 啟動器 -ValidateOnly 通過，共用動畫、關係劇情與語音資源雜湊通過。

本次為 UI 與保存行為的定向驗證。未修改的系統沿用 StreetPrivateAny01 完整回歸基準，未宣稱在新執行檔重跑所有活動。receipt.json 以 regression_baseline_receipt、regression_baseline_executable 與 inherited_evidence_executable 明確保留基準來源；presentation_evidence 為新版實測結果。測試使用 selftest.relationships.json，不操作玩家正式存檔。