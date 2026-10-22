# 大學城植栽落地修正 v3

使用者指出 v2 商店街棕櫚枝葉稀疏、校園密度不足，以及全圖樹木直接插入道路／水泥。這次實際修改 `StreetSocialCity` 正式地圖；不是只修改預覽圖片。

## 實作規則

- 逐棵使用 Unreal complex collision 射線的實際命中面，解析材質與原始材質槽名稱。不能只用地形底圖，因為上方可能另有道路、鋪面、設施或樓板。
- 道路及步道依權威 `ground-plan.json` 全寬再加 1.2m 植栽中心緩衝；2m 方花台邊緣仍不侵入原有路寬。建物四周保留 3m，排除水域與過陡坡地。
- 天然草地、森林、河岸、沙土可種樹；都市地坪及街邊鋪面必須使用真正有土壤的花台，不能直接插入水泥。原有有土壤的方花台可保留。
- 不合法落點由近至遠搜尋合適土壤，避免擠在同一點；找不到安全位置則退除，不強塞在馬路。記錄原始與新位置、理由及支撐面。
- 商店街稀疏棕櫚與窄冠樹改為現有飽滿榕樹，不擴大原樹模預算。
- 校園使用逐實例設施占地與入口／道路檢查，新增 220 棵混合樹群；新樹間距至少 7m。樹形、方位、尺度不同，使用既有四級 LOD、HISM 及 remesh 資產。
- 舊樹穴格柵及舊樹幹碰撞群組退場，重新建立與最終樹位一一對應的不可見樹幹碰撞；不能只搬樹冠而留下原碰撞。
- 不重做其他建築牆面、商店招牌、載具或既有藝術裝飾。獨立重開後逐項比對其他元件及實例。

## 第一輪與最終修正

原有 1,475 棵：614 棵位置基本保留，819 棵搬移或校正落地高度，42 棵因無安全位置退除；新增校園 220 棵，共 1,653 棵。112 棵商店街樹形改為較飽滿的遮蔭樹。新增 454 個 2m × 2m、48cm 高土壤花台。

「819」包含高度校正，不代表每棵都水平搬走。已有合格土壤花台內的樹不因鋪面背景而一律刪除。

實拍後發現規畫圖未涵蓋全部後加人行道，因此把現場 4,693 個導盲磚實例納入 2.7m 植栽中心禁入帶。再移植 311 棵、退除 19 棵，最終共有 1,634 棵樹，新增土壤花台保留 149 個。第一輪被淘汰的照片及收據保存在 `review-rejected-sidewalks/`，不是最終驗收。

220 棵校園補植再按南側、中央南側、教學區、宿舍周邊四個南北帶分層配置，每帶 55 棵；各帶候選點先固定種子打散，再做實際地面、設施、淨空與 7m 間距檢查，避免依座標順序取前 220 個而只補到南側。

## 工具與原生驗收

```powershell
.\Tools\lua\lua.exe Tools/street_city_tree_landscaping.lua land_probe
.\Tools\lua\lua.exe Tools/street_city_tree_landscaping.lua land_install
.\Tools\lua\lua.exe Tools/street_city_tree_landscaping.lua land_repair
.\Tools\lua\lua.exe Tools/street_city_tree_landscaping.lua land_balance
.\Tools\lua\lua.exe Tools/street_city_tree_landscaping.lua land_render
.\Tools\lua\lua.exe Tools/publish_street_city_tree_landscaping.lua
```

初次盤點不可覆寫；安裝有重複執行保護，保存前後檢查地圖 SHA，若其他 session 改過地圖則中止，不覆蓋。使用 `land_audit` 可單獨重開驗收；`land_render` 先執行同一驗收再拍照。

驗收資料與照片在 `output/street-social-city-production/tree-landscaping-v3/`：

- `baseline.json`：修改前全部元件／實例的數值變換。
- `UniversityCity-before-landscaping.umap`：本次原始地圖備份。
- `land_install.json`：最終逐樹支撐面、位置、樹種與修正理由。
- `land_audit.json`：重新開啟正式地圖，核對其他元件未改、逐樹位置與支撐面、實際土壤花台、樹幹碰撞數、四級 LOD、48 份樹木 remesh 收據。
- `land_render.json`、`after/`：原生 Unreal 照片與相機資料。
- `index.html`、`planting-map.svg`：實拍與最終植栽分布總覽。

這次沿用既有已 remesh 模型，沒有新增樹木幾何。四級 LOD 延續 v2；實際遊戲 FPS 需遊玩路線量測，不能只以原生照片推論。

## 最終驗收

重新開啟正式地圖後，4,551 項檢查通過；16 張原生照片逐張核對檔案雜湊，並目視檢查商店街、校園、森林道路、河岸及渡假區。森林道路原先壓到人行道的樹／花台已移到草地，導盲磚通道保持完整。220 棵校園補植在四個區帶各 55 棵。

本次地圖 SHA-256：`7405f3a6569e67c9e5b34b41262e863f295445ef1ee85a25fb0996e4a98565cf`。
