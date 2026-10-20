# 頂點色新模型卡（A005）

2026-10-10：參考 [JulianKnodt/texture_to_vert_colors](https://github.com/JulianKnodt/texture_to_vert_colors)（Texture Aware Remeshing），把 `Content/CharacterCards/CrowdAPNG/C005.png` 轉成「沒有貼圖、顏色全在頂點上」的新模型，存成同資料夾的 `A005.png` 四視圖 APNG。`CardReaderDevelopment.cmd` 預設就讀這個資料夾，選 A005 →「加载3D」即可。

## 重現

```powershell
.\Tools\make_vertex_color_card.ps1 -SourceCard Content\CharacterCards\CrowdAPNG\C005.png -OutputCard Content\CharacterCards\CrowdAPNG\A005.png
```

工作檔在 `Saved/VertexColorCards/A005`（`bake/outfitN` 烘焙圖、`convert.json` 每個部件的面數、`views` 四視圖）。`-ReuseBakes` 沿用已烘焙的圖，只重做轉換與擷取；`-Ratio` 改面數比例（預設 2）。需要已建置的 UE 編輯器模組（含 `ok.BakeCardAlbedo` 與 shader kind 20）。

## 做法

1. **烘焙真正的顏色**（UE，`Source/OpenKoikatsu/OKAlbedoBake.cpp`，`ok.BakeCardAlbedo`）。頭髮、飾品的 `_MainTex` 常是 8×8 白圖，顏色來自 color mask、`_Color*`、ramp 等 shader 運算，直接取貼圖會錯。所以把每個部件用它自己的材質重畫成一張平面：頂點位置＝UV0，所有法線朝向正交相機（toon 的相機光因此正好落在法線上），其他頂點串流不變；以 `SceneColorHDR` 擷取，得到每個 texel「完全受光」的顏色與覆蓋率。在平面上畫不出任何東西的材質（原生 skin、IBL）改用自己的 `_MainTex`，身體再套上 clothes-over 遮罩（`lowpoly::bake_alpha_mask`），被衣服蓋住的皮膚不畫；貼圖也全透明的部件（原本就看不見）在新卡隱藏。
2. **細分**（Rust，`Tools/ok-extract/src/vertcolor.rs`）。同位置的頂點共用幾何 id，最長邊優先二分，直到每條邊在烘焙圖上不超過 1 texel（單一部件上限約 40 萬面，超過就放寬門檻）。UV 縫兩側各自內插，不會產生裂縫。烘焙圖先把 UV 島外與 alpha 洞補上最近的顏色，避免雙線性取樣混到黑底。
3. **取色**：每個細分頂點取烘焙圖顏色（RGBA，RGB 為 sRGB 編碼的線性場景色，A 為覆蓋率）。
4. **顏色感知簡化**：Garland & Heckbert 1998 的 7 維 quadric（位置＋RGBA，幾何正規化到單位對角線，顏色權重 0.25），開放邊界加剛性約束平面，只做半邊塌縮，並檢查 link condition 與翻面。顏色平坦處被大量合併，顏色邊界保持銳利。目標面數＝max(原面數×2, 64, 細分面數×1%)。
5. **屬性轉移**：因為只做半邊塌縮，每個輸出頂點都是某個細分頂點，保有它在原三角形上的重心座標；蒙皮權重（合併後取前四）、表情 blendshape、法線、切線、其他 UV 都精確內插。飾品／頭髮的私有 rig（`AccessoryRuntime`）的來源網格一起改寫。
6. **打包**：沿用 `portable::normalize` 的流程，七套服裝逐套載入、轉換、`split_preview_resources`、`OKPV0003` 封裝，IEND 後的 Custom/Coordinate 不動。寫出後逐套重新載入檢查。最後用 `ok.ExportApngCard` 擷取新模型的四視圖。

UE 端：轉換後的部件 `shader_kind = 20`，`KKPreviewActor.cpp` 的 `MakeVertexColorMaterial` 以 COLOR0 當 unlit emissive（sRGB 解碼），再乘上相機光的明暗階（陰影色減半），masked/translucent 用 COLOR0.a；程序網格與 `KKSkeletalBuilder.cpp` 的骨架網格都會上傳頂點色。`_MainTex` 換成只含覆蓋率的遮罩（白色，alpha＝覆蓋率，經 `_MainTex_ST` 對應），讓描邊殼仍照原本形狀裁切。

## 結果（2026-10-10）

| 服裝 | 原三角 → 新三角 | 頂點色部件 |
|---|---|---|
| 0 | 121,418 → 313,114 | 41 |
| 1 | 194,541 → 439,343 | 37 |
| 2 | 78,385 → 190,612 | 30 |
| 3 | 92,421 → 227,220 | 32 |
| 4 | 81,355 → 203,667 | 30 |
| 5 | 112,129 → 281,630 | 38 |
| 6 | 101,004 → 260,863 | 39 |

（數字以 `Saved/VertexColorCards/A005/convert.json` 為準。）A005.png 約 147 MB（C005 為 55 MB）：頂點色模型不在共用資源包裡，全部是卡片獨有資源。

驗證：`cargo test --release --lib vertcolor`（細分不超過 1 texel 且無裂縫、平坦色塊被合併而色階保持銳利、權重與 morph 內插、烘焙圖補色），轉換後七套服裝逐套重新載入，UE 擷取 `APNG export PASS`。

## 錯誤與修正

- **烘焙全黑**：暫存材質在第一次畫到平面時才編譯該 vertex factory 的 shader，編譯期間的擷取是空的。改為編譯結束後重新擷取（至少兩次）。
- **光環變成實心圓盤**：描邊殼用 `_MainTex` alpha 裁切，轉換後的白貼圖讓被挖空的部分也畫出描邊。改為放入只含覆蓋率的遮罩；小四邊形帶細節 alpha 的部件另保留細分面數的 1%。
- **胸口破洞露出白色／膚色**：完全透明的部件（原本就看不見）被畫成白色，改為隱藏；身體的 `_MainTex` 備援沒套 clothes-over 遮罩，皮膚穿過背心，改套遮罩。顏色權重從 1 降到 0.25，讓幾何誤差較小。
- **Windows PowerShell 重新導向**：`-ErrorAction Stop` 下重新導向原生程式的 stderr 會變成終止錯誤，改用 `Start-Process -RedirectStandardError`。

## 限制

- 眼睛、眉毛、眼線、淚、鼻線等貼花與 UV 動畫部件保留原本貼圖（每套 13 個）。
- 顏色是「正面受光」狀態，明暗只剩相機光的一階陰影；高光、rim、hair gloss 不隨視角變化。頭髮比原卡略暗、外套白色略灰。
- 頂點色無法表現比面更細的細節，文字、細線會變粗糙；可提高 `-Ratio` 換取面數。
- 每張卡需要 UE 烘焙（每套服裝約 30 秒）。
