# R39：角色載入、場景往返與原始參數

## 計畫

1. 追查卡片讀取、目前角色重用及服裝快取的參數同步差異。
2. 修正所有重用入口，使成功重新選卡後的原始身體／臉部／物理／mod 參數重新套用；單純場景往返則保留尚未存檔的編輯。
3. 驗收編輯→互動→編輯→返回卡片、同卡重選、新卡切換、從卡片直接進入互動，以及失敗載入不破壞目前選取。
4. 記錄原因、實際測試與限制，不把 UI 值正確當成模型參數正確。

## 已找到的問題

- SelectIndex 重新讀卡後，StartLoad3D 的 unchanged portable actor 捷徑直接顯示舊角色，沒有同步讀回的參數。
- TryClothingPreview 在卡片頁尚未進入 model layout 時呼叫 ApplyPendingEditorMods，會被 layout guard 擋下。
- StartLoad3D 命中服裝快取直接返回，未消耗 bEnterInteractionAfterLoad。

- 明確套用整組 shape 後未清除之前排隊的 slider 請求，下一幀可覆蓋剛恢復的原值。
- 外觀／顏色交易以工作副本覆蓋清單原卡路徑。現在保留原始來源與模式，重選讀原卡，單純往返繼續使用編輯副本。
- 讀卡失敗原本先切換索引；現在讀取成功才發布新選取。

## 驗收

- 建置：`Saved/Tests/EditorNavigationSourcesBuildR39.log`，Succeeded，87.42 秒。
- 參數與場景流程：`Saved/Tests/EditorAcceptance/EditorNavigationParametersFinalGuiR39/report.json`，13/13 通過、exit 0、測試前後 DLL SHA256 一致。
- 覆蓋：卡片初值、原生模型 mod 骨骼變化、編輯→互動→編輯、編輯→卡片→編輯、排隊滑桿後同卡重選、卡片頁直接進入互動、新卡、服裝快取還原與互動入口、切回第一張卡、讀卡失敗保留、外觀工作副本後重選原來源。
- 比對整組身體與臉部 Selected/Session 陣列、mod JSON、胸部物理欄位，以及實際肩部骨骼 scale；不是僅比對文字欄位。
- 按鈕／場景回歸：`Saved/Tests/InteractionShowcase/EditorNavigationRoundtripR39/report.txt`，functional_pass=1、exit 0、DLL 雜湊一致；五張場景截圖保存於同目錄。已檢視返回編輯器及卡片頁截圖。
- 既有單元回歸：`Saved/Tests/AnimationFilter395/EditorNavigationUnitR39.log`，5/5 成功、exit 0：LoadSelection、ShapeReset、EditorMods、EditorModsCoordinates、EditorClothing。
- 實際外觀交易回歸：`Saved/Tests/EditorAcceptance/EditorNavigationPropertiesStableR39/report.json`，13/13 通過、exit 0、DLL 雜湊一致；2258 項設定可用、實際貼圖變色、飾品旋轉／縮放／綁定改變幾何、假髮來源與頭高保留、另存讀回、半脫／穿著還原。已檢視 report.png。
- 已完成本次要求的驗收。原始卡片未覆寫；測試用編輯卡保存在 Saved 下。

## 行為規則

- 單純載入編輯器、返回、進入／離開互動場景，保留本次未存修改。
- 明確點選卡片（含同卡重選）讀回該原始來源保存值，重置到服裝 1 與穿著狀態。
- 外觀／顏色交易可繼續使用工作副本，清單保留原始來源身分；重選原卡不會誤讀副本。
- 失敗讀卡不發布新索引或破壞目前角色。
- 第一次 GUI 的 13 個案例雖通過，但測試途中 DLL 被其他建置更新，不列入正式通過；詳見 E130。
