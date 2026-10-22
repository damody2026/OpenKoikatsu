# 活動場景美術修訂 v2

## 問題與做法

- 大熊：由兩個素色花箱改為玫瑰／香檳色拍照角、花拱、軟座、木格柵背景與低亮度暖光；保留拍照站位與三語文字。
- 噴泉：水體用 Single Layer Water；水柱獨立使用流動高光材質，池內設低功率藍白燈。白天與夜間分別驗收。
- 日落：原城市水體只覆蓋有限地圖，遠景可能看到黑色世界邊界。以既有認證 remesh 面片延伸海平面，再套用多方向動態法線、吸收、散射與反射。既有水體網格不重新生成。
- 煙火：四個空間分離的三維煙花群，每群 96 條實例尾焰；材質驅動擴散、重力垂落、閃爍、淡出、七秒循環。在遊戲中自行播放，不依賴 Python Tick 或外部腳本。
- 觀魚池：低吸收清水、細波與水下焦散亮度變化、深木池緣、植栽與池內低功率燈；不得為了反光遮掉錦鯉。
- 書店：參考誠品沉穩木質、書牆、展示與閱讀氛圍，不複製商標；新增三座 2.65m 高密集書牆、暖色軌道燈、深木地板、閱讀軟座，維持 12×18m 原建築與偏移入口的 2m 通道。
- 排練區：6×8m、0.45m 高舞台及三階踏步，側幕、支架、音箱、譜架、座席、遮棚與設備櫃；不再只放四角界標。

## 水面參考

1. [Epic：Single Layer Water](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine)：Opaque／Masked 搭配 SingleLayerWater，提供體積吸收、散射、折射、反射。此材質的 Opacity 控制表面／體積比例，不能沿用一般透明材質的直覺。
2. [Epic：Water Meshing System and Surface Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-meshing-system-and-surface-rendering-in-unreal-engine)：水面幾何與遠景延伸須一起處理。
3. [Epic：Water Waves](https://dev.epicgames.com/documentation/en-us/unreal-engine/simulating-waves-using-the-water-waves-asset)：多方向、不同波長與振幅的波形可減少規則重複。本版以解析波形及導數計算動態法線，不宣稱已導入 Water Body／Gerstner Water Waves Asset。
4. [誠品生活官方資料](https://www.eslitespectrum.com/Investors/Files/CorporationExplain/20250303094357832.pdf)：沉穩木質調與閱讀場所精神作為書店參考。

## 工具與效能

- `Tools/street_activity_art_plan.lua`：追加穩定識別碼的美術物件，保留舊配置並停用本流程已淘汰物件。
- `Tools/resort-props/src/activities.rs`：書牆、舞台、音箱、譜架由既有驗證 remesh 基本元件組裝；三階 LOD，不破壞接縫重做整棟 remesh。
- `Tools/street_city_activity_art_payload.lua`：原生材質、實例煙火與保存／重開檢查。材質僅覆寫本版新增物件、S07 的木材槽及地圖水體槽，不更改其他建築文字與共用 PBR 母材質。
- 水、玻璃、發光仍採參數與程序化著色，符合使用者不要求這些表面補 PBR 貼圖的規則。木、布、金屬等沿用已產出的 normal／ORM。
- 原地圖備份：`activity-scenes-v1/backups/art-v2-before.umap`；前版報告與 JSON：`backups/pre-art-v2/`。
- 第二輪實拍修正：海面解析法線按像素覆蓋範圍衰減高頻細波，避免遠景閃點；提高遠景粗糙度，保留近處波紋。煙火尾焰加長並提高發光亮度。魚池增加深青石材底與池壁。大熊區使用原已驗收的 6000 面玫瑰花束，沿用其 Tripo PBR 與 remesh 收據，再派生三階 LOD。

## 驗收方式

1. 原生匯入全部新增三階 LOD 模型，檢查來源認證與材質依賴。
2. 保存完整地圖，再以另一個 Editor 程序重開，檢查物件、材質編譯、語言、LOD、實例煙火及 2m 通道膠囊掃掠。
3. 原生 SceneCapture 實拍七個修訂區域，煙火使用兩個時間點驗證動畫差異；噴泉另外拍夜景。
4. 最後渲染程序須退出成功；報告、收據及實拍的保存地圖 SHA 必須相同。任何尚未通過項目不能寫成完成。
5. `Tools/verify_street_activity_art_frames.lua` 核對所有原生 PNG 的 SHA，並比較煙火、海面、噴泉及觀魚池兩個時間點的固定畫面區域；必須有實際可見的像素變化，不能只驗證材質有 Time 節點。

## 本版完成驗收

- 正式地圖保存 SHA：`7eab403e2f4338cbdf430f7bbd1fa2847c43226cf90b35ff007234a9e0ace14f`。
- 384 個場景物件實例、54 種實際使用模型；另外四組煙火共 384 條三維實例尾焰。
- 11,954 項原生檢查與 43 條通路／站位掃掠通過；保留三語切換與既有植栽、LOD。
- 20 張原生實拍；煙火、海面、噴泉、魚池的兩個時間點均有可見差異，PNG 雜湊全部核對。
- 最新匯入、美術材質建立、安裝、重開驗收及最終渲染程序均退出 0；歷史失敗與關閉異常仍保留於錯誤紀錄，不宣稱本次已找到舊異常根因。
- 本輪沒有再購買 Tripo 生成：新增玫瑰佈置重用已認證的既有花束，規則設施使用已 remesh 元件組裝。
