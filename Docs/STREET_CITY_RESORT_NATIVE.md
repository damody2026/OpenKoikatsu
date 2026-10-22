# 度假室外層：Unreal 原生整合

使用者在 2026-10-20 明確授權修改 StreetSocialCity，覆蓋 AGENTS.md 對該目錄的本次作業限制。作業不改主專案 StreetSocial.cmd 的打包版，也不涉及遊戲互動。

## 原生資料位置

- 專案：`C:/portable/OpenKoikatsu/StreetSocialCity/StreetSocialCity.uproject`
- 地圖：`/Game/UniversityCity/Maps/UniversityCity`
- 新資產：`/Game/UniversityCity/ResortV1/`
- 新 Actor 資料夾：`UniversityCity/ResortV1/`
- 最新報告：`output/street-social-city-production/resort-native-v1/index.html`
- 啟動正式地圖：`StreetSocialCity/OpenCity.cmd`

## 實作方式

100 種物件的 300 個 GLB LOD 已匯入 StaticMesh；每種 LOD0 原生網格包含三段實際 LOD，近中遠屏幕尺寸為 1 / 0.16 / 0.045，無 Nanite 依賴。來源 SHA、原生面數、尺寸與材質槽記錄於 imports.json。原生匯入不重建已認證的幾何。

樹、機車、自行車沿用原本已通過 remesh 的原生 StaticMesh，保留樹的 1992 / 912 / 336、自行車的 12999 / 3000 / 1000 三角面；機車保留原本 6000 面單段模型。以新 HISM 群組更新實例，舊管理群組關閉顯示與碰撞，避免雙重繪製。原建築、地形及既有場景物件另存 baseline-objects.json，重開後檢查名稱、位置、網格。

樹葉材質複製原本 MASKED、雙面、貼圖 alpha=A 的材質，僅於底色乘上 LeafTint，樹皮維持原材質。樹木與樹幹碰撞使用對應變體的相同位置與縮放。原始根部不移動。

車漆材質複製完整 PBR 圖，僅更換 BaseColorTexture 採樣；原有 normal / roughness / metallic 輸入保留。六種 2K 車漆貼圖各有 MaterialInstanceConstant，並確保兩種載具的六色都實際出現在原生地圖。這只調整配色，不增加車輛數。

度假物件依飯店花園、商街、交通站、校園、河岸、海灣等 26 個區域配置，共 300 個實例。沿用地形、水域、建築、入口、既有物件排除，再加入校園操場與公園的完整外框。座椅／服務檯朝向鄰近步道。原始可攜展示配置保留，正式世界布局獨立保存於 resort-native-v1/placement.json。

花草使用原本已 remesh 的 799 面元件，均勻縮放後重複填入花壇，避免將單叢拉成長條。梯形花箱、育苗架、花牆逐層填入；每層最多 8 株，60m 距離剔除。每株僅加入水平 94–100%、高度 96–104%、±12° 微調。24 組花壇分享四種花色與不同葉色係數，新增實例數以最新 install.json／audit.json 為準。

## 程式換色

- 載具母材質：`/Game/UniversityCity/ResortV1/Materials/M_scooter_Paint`、`M_bicycle_Paint`
- 六色實例：`MI_scooter_0`～`MI_scooter_5`，自行車同樣為 0～5。
- 可透過元件 `SetMaterial` 切換六色實例；使用 MID 時，`BaseColorTexture` 為車漆貼圖參數。
- 物件裝飾／布面：`ResortTint`。木材、金屬、石材不一起染色。
- 樹葉：`LeafTint`；花草：`FlowerTint`、`LeafFactor`。

這些是原生 Unreal 材質參數及資產，不依賴網頁預覽。遊戲中的選色 UI 與互動控制不屬於這次作業。

## 重跑與驗收

```
Tools/lua/lua.exe Tools/street_city_resort_run.lua probe
Tools/lua/lua.exe Tools/street_city_resort_run.lua import 1 100
Tools/resort-props/target/release/resort-props.exe native-layout C:/portable/OpenKoikatsu
Tools/lua/lua.exe Tools/street_city_resort_run.lua install
Tools/lua/lua.exe Tools/street_city_resort_run.lua render
```

Lua 負責作業、資料與程序控制。嵌入 payload 使用 Unreal 已安裝的 Python API，由既有 run_street_city_unreal_deferred.py 在 Editor 啟動完成後執行；沒有新增 .py／.ps1 專案腳本。每次執行均有 run_id，程序結束後必須取得同次的新報告，不能用舊報告判成功。

原生材質參數設定後讀回驗證；本機 UE5.8 的部分 setter 雖成功設定仍回 false，因此不以回傳 bool 判定。HISM 使用 runtime setter，避免 Blueprint 重建使元件 handle 失效。

render 會以全新 Editor 重開保存地圖，先做原生資產、LOD、PBR、尺寸、實例／變體、原環境回歸及 2m 店面入口碰撞檢查，再以 D3D12 SceneCapture 實拍。相機候選點排除建築外框並以 0.35m sweep 檢查視線，避免截到室內牆壁。相機不保存到地圖，也不另改燈光或擺展示道具。

最後人工檢視原生截圖與日誌，確認沒有材質編譯錯誤，報告及地圖雜湊一致後，才發布 activation.json。驗收範圍為這次室外物件層；既有地形三角接縫、建築外觀美術並非本次重新製作項目，也未宣稱完成打包遊戲或幀率基準測試。

## 回復

`resort-native-v1/backups/UniversityCity-before-916c7e5a82d340c2ea455271c390c5cbf98d689930f1c7f7d0d235e12e7d0c95.umap` 是整合前的原始地圖。關閉此專案 Editor 後，可將備份複製回 Content/UniversityCity/Maps/UniversityCity.umap；新資產資料夾可保留，沒有必要遞迴刪除。StreetSocialCity 及產出資產維持本機，不加入版控。

錯誤、失敗日誌與修正保留於 Docs/STREET_ERRORS.md 與 resort-native-v1/backups/。
