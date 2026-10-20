# 預覽初始化分階段 R34

1. 依 R30 逐幀追蹤中三次約 32 ms 的背景 BeginSessionLoad 初始化，將陰影目標、捕捉元件、骨架、參考材質分為有明確順序的階段；UObject 操作仍在遊戲執行緒。
2. 同步 LoadSession 維持一次完成；Begin/Continue 每次最多推進一個初始化階段，未完成不發布 ready。不變動來源資料、shader、陰影解析度、骨架或部件順序。
3. DestroyAllParts 清除階段進度。以同步／增量實際卡片契約與每階段取消後重新開始測試，避免殘留資源或過期完成。
4. 等 R33 GUI 完成及保存其二進位雜湊證據後才建置 R34；重新跑 UE 與服裝／飾品 GUI，保留完整長幀與 100 ms／60 FPS 嚴格結果。

### 最新效能要求（2026-10-08 使用者更新）

每個飾品點選至可見的門檻由 100 ms 調整為 300 ms；切換後持續至少 60 FPS，仍包含背景預備期間。新報告明列 switch_budget_ms=300、switch_budget_pass、switch_300ms_pass；原 switch_100ms_pass 保留原本 100 ms 意義作歷史比較，不改寫舊報告。此變更沒有調整衣物驗收的既有 200 ms 門檻。


## 建置與整合驗證

EditorIncrementalSetupBuildR34 因跨階段旋轉變數編譯失敗，已保留並記錄 E114；EditorIncrementalSetupBuildFixR34 成功（113.55 秒）。EditorIncrementalSetupIntegrationR34 共 17 項 Success／退出碼 0，包括新增各階段取消與重啟、既有渲染閘門及真實同步／增量載入貼圖一致性。原生程式未在 R34 變更，沿用 R33 的 34 項原生證據，不重複計數。

整合測試記錄的五階段最大時間依序 0.139、0.021、0.611、53.618、0.712 ms。骨架階段包含未預備原生參考的冷測試，仍可能超預算；不能宣稱每一階段都小於 16.667 ms。GUI 正式 worker 會先準備原生骨架，仍需以完整逐幀報告判定，而非僅看階段記錄。

衣物診斷明確使用 OK_LOAD_PROFILE=1，與接下來關閉診斷的正式標籤分開保存。飾品正式報告使用最新 300 ms 門檻。


## 衣物驗證

EditorIncrementalSetupClothesDiagnosticR34（OK_LOAD_PROFILE=1）7 項／32 次切換成功，最大 190.7894015 ms。前三個首次預備狀態套用目前模組／形狀的 modifiers 階段約 110.603、125.617、111.357 ms，後續同狀態約 0.002–0.003 ms；第一個 layout 42.866 ms，後續約 0.05–0.13 ms。這是診斷資料，不拿來當正式幀率。

EditorIncrementalSetupClothesFastR34 關閉診斷，7 項／32 次切換成功，最大 175.6240986 ms，退出碼 0。截圖已檢視；幾何、遮罩、身體及骨骼修改、獨立手勢、動畫相位與暫停、個別衣物 fallback 及失效釋放成功。沿用衣物既有 200 ms 檢查，不稱為 100 ms。飾品接續使用 300 ms，完整採樣尚在執行。


## 飾品正式結果

EditorIncrementalSetupExtraAccessoriesR34 的 8 項功能成功；新版報告明列 switch_budget_ms=300。切換 5186.2038001 ms，27,008 幀最低 5.1937502 FPS、最長 192.5391033 ms、38 幀超預算，整體失敗／退出碼 1，未開 CPU 追蹤或時序診斷。五階段初始化與取消正確，但不能視為 300 ms／持續 60 FPS 通過。保留全部逐幀資料、來源 receipt 與交易時序；原 100 ms 歷史欄位仍維持原語意。

