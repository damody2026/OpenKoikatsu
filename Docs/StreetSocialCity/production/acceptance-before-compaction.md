# 2026-10-12 當前功能完整驗收

本輪完成共用家具產線、真實下載候選模型處理、正式組裝驗收門檻及全城規畫重建。全城Tripo模型、材質及遊戲內互動尚未完成。

來源資料 SHA-256：`1fdc80470a13cec6a83a374852adc60684f23f5ac6d829aadf0fa48ce0d08c7e`。

| 驗收 | 結果 |
|---|---|
| 單元回歸 | 38項通過 |
| 43棟／181層：外框、牆厚、門位、至少3m淨通路、用途與設備 | 通過，詳各項獨立報告 |
| 家具共用 | 118項型錄資產供23,065個家具實例使用；跨型錄無相同模型重複；69項跨房共用 |
| 全部資產與擺放 | 19,452項語意規格、49,819個擺放；相較逐房資產減少3,817項，保留位置及房間歸屬 |
| 結構拆件 | 7,054項總成、128,844個部件、741種可共用尺寸；各軸最多6m |
| 門洞／樓板孔／屋頂邊界 | 通過；先扣洞再拆件 |
| 43棟實際控制GLB內容 | 通過：43棟、980單元、612,677項檢查；最終報告來源SHA一致 |
| 候選模型尺寸檢查 | 真實STL下載後方向校正；直接量測轉出GLB，0.8×0.6×0.75m、底部中心原點通過 |
| 正式模型門檻 | 直接量測頂點及父節點變換；核對模型SHA、幾何簽章、TripoID、參考圖SHA、生成紀錄及品質檔；缺件時拒絕正式組裝 |
| 製作紀錄持久化 | 重建保留真實模型編號、下載原檔及候選狀態；不符幾何簽章則不套用舊紀錄 |

瀏覽入口沿用已通過的14項操作流程，本輪未重跑瀏覽器操作測試；本輪另核對生成HTML、SVG及資料參照。舊的14項測試不是本輪重跑結果。

## 真實模型證據

Grok書桌參考圖已送Tripo網頁H3.1生成，編號`b5e24e87-057a-43ff-9259-a200fa9c9e70`，頁面42,100面、消耗30點。本輪取得2,105,084bytes的STL，保留原檔及SHA。匯入實際42,098個三角面、21,051個Blender頂點；GLB因平面法線拆頂點而數量較多，不混用網站統計。

繞Z軸90度校正長寬方向後，尺寸修正各軸比例差3.57%；經GLB頂點驗收為0.8×0.6×0.75m。三視實體渲染確認一張桌面、四腳與桌下空間；模型與尺度處理都有保留紀錄。

- 原始實際下載：`output/street-social-city-production/downloads/COMMON-CITY-study_desk/source.stl`
- 候選模型：同目錄`oriented/candidate.blend`及`candidate.glb`
- 尺寸驗收：同目錄`geometry-acceptance.json`
- 實體三視渲染：同目錄`inspection/front-left.png`、`rear-right.png`、`high-top.png`
- 未完成品質檢查：同目錄`preliminary-quality-review.json`

STL沒有UV／PBR；GLB及OBJ含貼圖匯出雖執行，仍未取得檔案，網站未提供明確原因。不能因為尺寸符合便批准模型。狀態為`downloaded_geometry_material_export_blocked`，actual_model_approved=false。

教室Grok圖擅自增加講台及背景牆，仍維持退件。其餘模型、扶手、外牆窗表、地形高度銜接、動態門扇／電梯及遊戲走查保留待辦，未宣稱完成。

## 可重建流程

- `python Tools/rebuild_street_city_plans.py`：38項回歸、全城圖面／用途／尺寸／共用資產驗收；不呼叫生成服務。
- `python Tools/build_street_city_controls.py`：43棟控制模型匯出，直接讀GLB驗收頂點及座標。
- `Tools/prepare_street_city_candidate.py`：Blender讀GLB／STL／OBJ，記錄原檔SHA、方向、縮放及實際界限；不批准模型。
- `Tools/render_street_city_candidate.py`：渲染真實下載網格，絕對路徑及自動完整取景。
- `Tools/inspect_street_city_model.py`：只讀驗收實際GLB尺寸及原點。

錯誤及防範：`../ERRORS_AND_PREVENTION.md`。實際進度：`generation-progress.html`及`generation-ledger.json`。

本輪收尾核對：`current-feature-acceptance.json`，全部現行規畫／拆件／共用資產／控制模型報告已核對來源SHA，正式缺件拒絕與真實STL尺寸驗收均通過。此結果不代表全城模型已完成。
