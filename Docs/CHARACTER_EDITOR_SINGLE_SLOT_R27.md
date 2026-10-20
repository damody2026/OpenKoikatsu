# R27：飾品單欄擷取與卡片更新

## 計畫與實作

1. 全角色與單欄共用原作／模組飾品建立入口，保留材質編輯器、HideHair、兩層位移、動態骨架與綁定語意。
2. 保存建立當時的完整頭部／身體來源貼圖池、髮光澤及採樣、完整參考骨架與依賴指紋；不以合成皮膚近似來源。
3. 以額外共用資源保存 context，不改舊 SessionBlob／portable residue 佈局。OKSC0003 可讀 OKSC0002 與舊 raw blob；來源快取管線版本 254。
4. 單欄更新只讀全域資料及部件來源參照，不解壓其他 renderer 資產；依確切 source_property 移除／附加部件，重映射索引，保留其他全域／部件與服裝的資源參照。
5. 外觀 apply 已優先使用單欄路徑；舊卡缺少 context／來源欄位或依賴改變時，回到既有完整來源載入。正式 export_edited 保留完整 KKEx，receipt 記錄 single_slot、parts_rebuilt、parts_reused。
6. 原作素材查詢成功時不再載入模組目錄。來源解析模式與完整擷取一致；未指定衣物狀態與明確全穿使用相同有效指紋。

## 原生驗證（2026-10-08）

- EditorAccessoryContractFinalR27：1 passed、0 failed；假髮 3 renderer、一般空欄與額外空欄的全部部件欄位與完整擷取一致。原作項鍊位移、依賴失效、全穿等價、新舊快取及分離資源還原通過。
- EditorAccessoryCardPatchFinalR27：1 passed、0 failed；正式外觀 export_edited 新增／重置飾品及再次載入通過。新增只建立 2 renderer，保留 34 renderer 與其餘 6 套服裝；所有原始資源參照、更新前後展示幾何／蒙皮／貼圖與形變一致，包含假髮。
- EditorPartPatchFinalR27：1 passed、0 failed；重複 renderer 名稱依索引／來源獨立處理，索引重映射、全域保留、空替換及無效輸入拒絕通過。
- EditorCacheRegressionR27：11 passed、0 failed、2 ignored。
- EditorOutfitRegressionR27：3 passed、0 failed、3 ignored。

ignored 不計入通過數。初次失敗與原因保留在 CHARACTER_EDITOR_ERRORS.md E83–E90，未以刪除斷言或放寬契約通過。

原生正式卡片更新曾量到 892.966–2589.562 ms；此為來源擷取／保存流程，**不是 UI 點選至可見時間，也未達到 100 ms**。不能以非同步、單欄來源更新或平均 FPS 宣稱完整效能通過。

## 編輯器驗收

新版建置成功（EditorSingleSlotBuildR27，162.55 秒）。EditorSingleSlotIntegrationR27 的 12 項 UE 回歸全部通過；EditorSingleSlotPropertiesR27 的 13 項 GUI 全部通過，截圖已檢視，假髮位置正確。

EditorSingleSlotClothesR27 首次 201.1597 ms 超過既有 200 ms 門檻，保留失敗報告。其他 UE 工作退出後，EditorSingleSlotClothesIsolatedR27 的 7 項 GUI／32 次切換全部通過，最長 165.0008 ms；形變、骨架、左右手、時鐘、暫停、幾何與遮罩保持正確，截圖已檢視。165 ms 不能稱為滿足 100 ms。

EditorSingleSlotExtraReadyR27（已具來源 context 的卡）與 EditorSingleSlotExtraLegacyR27（舊 C000）各 8 項功能通過；空欄不重建、額外欄位顯示、真實飾品幾何、攝影機、顏色／位移／綁定與保存均通過。正式 GUI receipt 證明 slot 20 單欄建立 1 renderer、保留 34 renderer／其餘 6 套服裝；舊卡正式完整恢復路徑也驗證成功。兩張 report.png 均已檢視，假髮在頭部。

| 嚴格驗收 | 點選至可見 | 幀數 | 最低 FPS | 最長幀 | 超出 16.667 ms |
|---|---:|---:|---:|---:|---:|
| ExtraReadyR27 | 11993.9302 ms | 30783 | 7.5503 | 132.4455 ms | 20 |
| ExtraLegacyR27 | 39863.9379 ms | 21782 | 12.0812 | 82.7729 ms | 62 |
| ExtraIsolatedR27 | 4906.1713 ms | 30832 | 13.6358 | 73.3365 ms | 15 |

三輪 valid=false／退出碼 1，保留所有長幀。Ready 啟動時另有 E:/PhysX UE 探測程序，環境已保存，不能作隔離效能比較；Legacy 啟動／採樣中僅有本輪 UE，無本次編譯或其他測試。ExtraIsolatedR27 在其他 UE 工作退出後重新驗證單欄路徑，8 項功能通過；平均 371.6902 FPS 不能代替最低 13.6358 FPS。啟動／結束二進位雜湊均核對成功，三張 report.png 均已檢視，頭髮位置正確。

原始報告：Saved/Tests/EditorAcceptance/EditorSingleSlotExtraReadyR27、EditorSingleSlotExtraLegacyR27、EditorSingleSlotExtraIsolatedR27。每輪含完整逐幀資料、啟動／結束狀態、截圖及環境記錄。ExtraIsolatedR27/single-slot-receipt.json 保存正式單欄 receipt 的副本。

UE 目前仍重新建立整套顯示元件；真正保留未變動 renderer 的顯示更新尚未接入。服裝背景預載與最後保存也仍可能超出幀預算。

100 ms／完成後持續 60 FPS 及功能盤點中尚缺項目仍未完成。最新嚴格驗收須保留所有長幀，不與編譯或本次其他測試同時執行。

後續通用改造：保留未變動 renderer 並正確更新 native／UE 索引與動畫狀態；將可見更新與最終卡片保存分成可恢復交易；預備可重用的飾品資源；服裝狀態重用相同原始資料並只更新必要部件／身體遮罩。不能移除背景採樣、改用平均 FPS 或只測暖切換來通過要求。
