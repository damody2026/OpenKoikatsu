# 活動場景模型補齊：生產與驗收流程

來源：`Docs/STREET_MISSING_ACTIVITIES.md` 與 `activity-scene-gap-audit.json`。

本次處理 StreetSocialCity 的實體空間、模型、設施、商品擺設、文字材質與原生擺放。沒有把模型落地等同於 StreetSocial.cmd 的活動已實作；互動、NPC 動作、任務、背包、煙火與魔法 VFX 仍是後續工作。

## 十個場景工作包

| 工作包 | 實體內容 |
|---|---|
| ACTSPACE-01 | M1 8F 2.8m 大熊、合照前景、花箱與招牌 |
| ACTSPACE-02 | 公園噴水池、可見水流造形、水面、畫架、畫具與休息位 |
| ACTSPACE-03 | 既有八攤夜市補商品、食品、計算機、燈光、回收、座位與員工設施 |
| ACTSPACE-04 | 沿用海灣觀景步道，補望遠鏡、座椅與導引 |
| ACTSPACE-05 | 水岸觀賞平台、座位、集合入口與退場通路；預留遠方效果錨點 |
| ACTSPACE-06 | 池體、水面、淡水錦鯉、飼料機與觀看空間 |
| ACTSPACE-07 | 公園晨練區、伸展、補水與 2m 寬短程慢跑環線 |
| ACTSPACE-08 | 依目前校園館舍配置補行政、系辦與服務辦公室的櫃台、筆電、文件與候位設施 |
| ACTSPACE-09 | S07 低繪本架、正面繪本封面、畫具展示、閱讀位、文具與雨傘 |
| ACTSPACE-10 | 表演／練習空地、觀看與施演分區、標記、補水與休息位 |

## 單一來源與可重跑工具

1. `Tools/prepare_street_activity_context.lua` 讀取目前校園語義契約。不得拿舊樓層盤點的單位名稱蓋過後來調整的正式配置。
2. `Tools/resort-props` 的 `activities-build` 產生規則家具；沿用已通過 remesh 的公尺制元件，另為傘面執行 remesh。輸出三階 LOD、來源 SHA 與元件 remesh 報告。
3. `Tools/prepare_street_activity_models.lua bear|koi` 正規化 Tripo 原始下載、執行 instant-meshes-rs、驗證尺寸與表面誤差；之後以 `Tools/smooth_street_activity_models.lua` 做幾何／UV不變的有機表面法線處理。原始下載不覆寫。
4. `Tools/build_street_activity_plan.lua` 是新增模型、擺放、鏡頭、通路、燈光與效果預留錨點的來源。不得直接在最終地圖堆放沒有登記的補丁。
5. `Tools/build_street_activity_signs.lua` 使用 Windows 字型產生繁體、簡體、英文招牌；輸出逐張 SHA 與文字表。
6. `Tools/run_street_activity_scenes.lua [起始階段]` 依序執行匯入、安裝、重開原生資產與通路檢查、原生拍攝。
7. `Tools/publish_street_activity_scenes.lua` 只在原生檢查、通路檢查與拍攝均通過時產生正式驗收頁。

每階段保留 run_id；啟動器不可拿上次成功檔案冒充本次成功。匯入目的地含來源 SHA，原始來源與舊資產均保留。新增 Actor 使用專用標籤與資料夾；重新安裝時停用本流程遺留的舊 Actor，不刪除其他流程的建物與家具。

## 模型、材質與語言

- 新有機模型：Tripo Model 的大熊與錦鯉，下載為 2K GLB。不得用 Texture 功能掩蓋錯誤幾何。
- 大熊 LOD0 上限 30,000 三角形；錦鯉 5,000。原生再產生約 40% 與 15% LOD。實際數字以 `import_assets.json` 為準。
- 大熊的直接 remesh 曾遺失局部，因此採有明確來源記錄的參考拓樸重建；報告包含 remesher 輸出、修復過程及表面誤差。不能宣稱它是未經修補的直接 remesher 成果。
- 家具沿用已驗證的板件、圓柱與傘面，保持公尺尺寸。商品沿用既有 Tripo remesh 成品，不重複生成。
- 普通表面沿用原生 PBR 材質，沒有重寫全城既有材質圖。水面依使用者要求使用 constant 材質，不另塞 normal／metallic／roughness 貼圖。
- 所有本次新增文字接到 `/Game/UniversityCity/Localization/MPC_CityLanguage` 的 `LanguageIndex`：0 繁體、1 簡體、2 英文。

## 正式完成條件

- 重開 `/Game/UniversityCity/Maps/UniversityCity` 後，Actor、來源雜湊、位置、LOD、材質及碰撞與登記一致。
- 通路與雙人站位以原生膠囊掃掠檢查，不能只憑平面圖標示「2m」。不通過時要調整配置或修正既有阻擋，再重開檢查。
- 以角色高度鏡頭逐區拍攝，會場另補較高的總覽構圖；確認大熊、噴泉、魚池、商品與櫃台實際入鏡，沒有朝向錯誤或被遮住。
- 另拍三語文字與夜市夜間版本。拍攝元件與臨時光照狀態不保存進地圖。
- 結果、原生截圖、模型面數、來源與限制集中在 `output/street-social-city-production/activity-scenes-v1/index.html`。

最終通過與否以該目錄的 `acceptance.json` 為準；本說明文件本身不是驗收證明。
