# 綠色草坡與 Normal

使用者要求山、土丘呈現綠色有草的表面，而且必須有 normal。原先森林地表的來源主要是褐色土壤，僅調整飽和度仍會像裸山。

## 材質方法

- 使用既有已烘焙的 2K 草地 Base Color、Normal、Roughness，不以常數綠色取代紋理。
- 建立 `/Game/UniversityCity/GrassHillsV1/M_GrassWorldTriplanar_v2`，只替換地形自然草地與森林地表材質槽。保留第一版材質，避免改寫其他正在開啟的 Editor 已載入資產。
- 世界座標三向投影，8m 重複單位，依幾何法線混合三個投影面。坡頂與陡坡共享連續尺度，避免沿陡坡拉長 UV。
- Normal 以 `TC_NORMALMAP`、`sRGB=false` 匯入，將 Blender/OpenGL 的綠通道轉為 Unreal/DirectX 方向。三個投影面的法線擾動依各自基底轉為世界空間，再混合、正規化接入 Normal；材質 `tangent_space_normal=false`。
- 草地 normal 強度 0.6；roughness 使用線性灰階取樣，限制在 0.8～1，metallic 為 0。加入小幅大尺度明暗變化，避免山體像整片油漆。
- 草綠色採線性 RGB tint `(0.78, 0.82, 0.74)`，來源草地顏色減飽和 22%，避免第一輪實拍的過亮青綠；不把 diffuse 顏色當 normal。
- 所有地形幾何、面數、樹木位置、碰撞、建築與道路保留。這是材質變更，沒有新增需 remesh 的幾何。

## 執行及驗收

```powershell
.\Tools\lua\lua.exe Tools/street_city_grass_hills.lua grass_prepare
.\Tools\lua\lua.exe Tools/street_city_grass_hills.lua grass_install
.\Tools\lua\lua.exe Tools/street_city_grass_hills.lua grass_render
.\Tools\lua\lua.exe Tools/publish_street_city_grass_hills.lua
```

`grass_prepare` 在 Entry 地圖先匯入並編譯材質，防止編譯失敗的材質進入正式地圖。`grass_install` 建立完整元件基線及原地圖備份，只修改允許的自然地表槽，保存前核對地圖 SHA 防止覆蓋其他 session。

若其他作業更新正式地圖，先保存差異、檢查上一輪全部樹位，再以最新世界重建基線，不能用本次舊備份覆蓋。世界載入前後也必須檢查 SHA；已具備相同來源 SHA 與匯入設定的貼圖直接重用，不重存正在串流讀取的檔案。

`grass_render` 重新開啟正式地圖，先比對全部元件狀態、允許的材質變更、normal 實際接線與空間、貼圖解析度／色彩空間／壓縮設定、編譯指令數，再拍攝近中遠山坡、校園背景、河岸與草地細節。照片不當作遊戲 FPS 測試。

產物：`output/street-social-city-production/grass-hills-v1/`；包含 `baseline.json`、`UniversityCity-before-grass.umap`、`grass_prepare.json`、`grass_install.json`、`grass_audit.json`、`grass_render.json` 及 `index.html`。

## 最新世界的整合與驗收

若其他 Editor 在驗收後又保存世界，使用 `grass_merge`：重新載入最新地圖，核對已批准的全部樹位，只把本次擁有的 32 個自然地表槽對齊到草坡材質，其他元件採最新世界狀態。需要時才保存；沒有槽位漂移就完全不重寫地圖。原基線與收據另存，再進行原生稽核及六張拍攝；發布器依最新成功拍攝收據選擇 `grass_render.json` 或 `grass_merge.json`。

最終重新開啟通過 **3,612 項檢查**，包含 2K normal 接線、世界法線空間、BC5／線性與綠通道方向設定，以及其他元件與實例狀態比對。六張照片涵蓋校園背景、山腳近中遠景、河岸遠眺及草皮細節。地形面數增加 0；草坡 shader 為 215 pixel instructions、4 samplers，未據此宣稱遊戲 FPS。

與其他作業的新世界比對時，32 個槽已全部正確，`latest_merge.rebound_slots=[]`，沒有再改寫地圖。本次完整實拍驗收快照的地圖 SHA-256：`6083f478e2db39f64c60396af58762b8e7590354d4ff5be012dd64cf3769a6bd`。材質 SHA-256：`ccae72fc80a115ef59b7a5ace5efc031994d7a5f6554bcbc2e95a09f841c86f0`。地圖、材質與六張照片雜湊均已核對。

共享世界在此後仍可能被其他工作保存；全圖位元雜湊變動不等於草坡退回。以 `grass_current` 只讀重開最新檔案，核對全部 32 個本次擁有的材質槽與 normal 設定、父材質雜湊，另存 `grass_current.json` 的槽位語義雜湊與當時世界雜湊。這能區分外部變更與本次功能回歸，避免為了無關存檔反覆覆蓋世界或重拍同一功能。
