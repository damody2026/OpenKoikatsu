# 人物視窗鋸齒修正（2026-10-09）

## 根因
DefaultEngine.ini 與人物共用 SetupStudioView 強制 FXAA。編輯與1對1共用這個人物顯示入口，切場景／重新取景也會重套，因此不能只改啟動 CMD 或設定檔。原本未明確限制 primary／secondary screen percentage；縮放視窗可能受到 Unreal 自動解析度策略影響。這次找到的是顯示設定問題，尚無證據是 APNG 角色貼圖解析度不足。

## 修正
- 預設及人物顯示入口一致改 TAA。
- primary／secondary 100%，dynamic resolution 關閉；TAA quality 2，history 100%。
- 保留現有高模、貼圖與動態模擬；不提高超採樣比例。
- 在實際互動場景 audit 驗證套用值；完整重建後跑真實 APNG／雙人及返回流程，檢查截圖。

## 驗證待辦
- 完整 Editor build。
- 原生解析度與 TAA audit 通過、角色／頭髮外框視覺檢查。
- 雙人動作與即時控制回歸；效能數據須區分功能測試與隔離 Present 驗證。
- 不修改 UE 引擎來源。

## 驗證結果
- CharacterViewportAABuildR1：完整 Succeeded，exit=0。
- CharacterViewportTAAFunctionalR1（1600×1000、真實C001 APNG與男角）：functional_pass=1、native_resolution_taa=1、exit=0、module_hashes_stable=1；走路循環、語音嘴型、雙人切換與即時控制正常。Showcase.png 已目視檢查，細髮／衣服及身體外框平順。舊證據解析度1280×720，故不將新舊截圖當同解析度量化A/B。
- CharacterViewportTAAGalleryR1：functional_pass=1、editor_and_interaction_separate=1、exit=0；載入3D只進編輯、即時身形修改、進互動、肚子例外、返回編輯後再修改、返回卡片流程通過。EditorReturned.png 已目視檢查頭髮與人物輪廓；共用 SetupStudioView 的 TAA／100% CVar 日誌正常。
- 功能流程混合平均5.710ms（1600×1000）；external_unreal_observed=1／performance_evidence_isolated=0，不能作為隔離120FPS或TAA額外成本的證明，亦不是雙人持續播放專用平均。未做定量動態拖影驗證。本修正沒有宣稱完整效能目標已達成。
- 設定檔與 SetupStudioView 都修正，不需更換CMD；新啟動程序即採用。未修改 D:/UE5.8 原始碼。
