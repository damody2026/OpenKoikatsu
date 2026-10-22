# 大學城追加 40 種 Tripo 景物與原生實拍

## 本輪範圍

- 新增 E001–E040，40 個不同 Tripo Model 工作，不把重用配件算入新增種類。
- 百貨 1–10F 各一件對應樓層主題的公共藝術；另 30 件分布校園、宿舍周邊、住宅、商店街、交通站、森林、河岸及海灣。
- 保留 D001–D030 與既有庭園配件，保留各建築原有牆面風格。
- 最後逐件拍攝先前 30 件與本輪 40 件在正式 Unreal 地圖的樣子。

## 執行與資料

輸出根目錄：`output/street-social-city-production/city-destinations-v3/`。

1. `Tools/build_street_city_expansion40.lua` 建立主題配置與缺口盤點 `plan.json`。
2. `Tools/street_city_citywide_art.lua dest_probe 3` 記錄原生十層公共路徑與舊景物狀態；不可在安裝後重建此基準。
3. Chrome 操作 Tripo Model，2K PBR、生成上限 30000 三角形；`jobs.json` 保留所有不同工作識別。
4. 匯出後等待並檢查本機 GLB；`source-proofs.json` 記錄雜湊、內嵌貼圖尺寸、normal 與 metallic-roughness 存在性。
5. `Tools/prepare_street_city_destinations.lua <first> <last> 3` 正規化公尺尺度，經指定 `E:/Paper/instant-meshes-rs/target/release/instant-meshes.exe`。低預算失去有機細節時保留拒絕收據，允許每件最多 30000 三角形；明確記錄外部場參考拓樸修補。
6. `dest_import 3` 匯入 PBR 與三階 LOD；`dest_install 3` 依原生地面支撐、樓層標高、走道與既有物件碰撞選址並保存。
7. `dest_verify 3` 獨立重開驗收；要求先前 30 件與配件不變、140 條百貨公共路線無新增阻擋。
8. `dest_capture 3` 在正式地圖逐件拍攝，記錄相機、原圖雜湊及保存地圖雜湊；不以 Tripo 預覽代替 Unreal 實拍。
9. `Tools/publish_street_city_expansion40.lua` 僅接受一致且通過的原生驗收與 70 張拍攝收據後發布圖庫。

## 座標與配置約束

世界資料為東、北、上，單位公尺；Unreal 為 X=北×100、Y=東×100、Z=上×100。Rotator 一律使用具名參數。百貨樓層模型共用建築原點，須再加 `geometry.contract.floors.z_m`，不可把 Actor Z 當成每層地坪。

戶外先排除建築範圍、道路、水域與入口保留帶，再檢查五點支撐與局部平整。800m 操場等超出高度圖範圍的設施，使用正式節點平台與原生射線；不得把查不到高度當成零高程。

## 已知驗收界線

沒有新增遊戲互動。本輪以新增物件與休憩組合增加美感，牆面修改為零。原有固定入口測線的歷史訊號另存於全城基準報告；要求不增加阻擋，不把它們宣稱為已修復。LOD／剔除已設置，但未測量目標硬體 FPS。

最終數量、正式地圖雜湊與拍攝結果以輸出根目錄 `acceptance.json`、`expansion-audit.json`、`render.json` 為準；缺少這些通過收據時不得宣稱交付完成。

## 最終原生驗收（2026-10-22）

40個不同Tripo工作全部完成、下載並通過指定工具 remesh 閘門；原生網格均有三階且面數依序減少的LOD，每件LOD0最多30000三角形。正式保存40件新主物件及66個重用庭園配件，先前30件與其65個Actor狀態不變。百貨10層各1件；140條保留路線無新增阻擋，10件均有對應樓板支撐。新資產452項檢查、全城原始54755項檢查通過。

70張1440×1000原生照片逐一檢視；34張首輪補拍，另6張、4張針對前景遮擋及正面細節調整鏡位。所有原圖雜湊與尺寸通過核對，拍攝未變更模型配置或保存地圖。實際生成造型未含原先預想細節時，圖庫採用真實呈現名稱，原始prompt與工作識別仍保留供追溯。

圖庫：`output/street-social-city-production/city-destinations-v3/index.html`。

正式地圖 SHA-256：`553a6fe58b460ca9620a94bebff1843276667d8acec6d4216adb8becdc876de3`。
