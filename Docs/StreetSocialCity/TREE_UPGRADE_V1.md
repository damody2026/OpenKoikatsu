# 大學城樹木替換 v1

本次依使用者選擇：主體採傳統 LOD＋遠景 Impostor，少量花樹採不透明立體樹冠。正式場景為 `StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap`，不是 StreetSocial demo。

## 範圍與保留契約

- 原位替換 649 個樹木 StaticMesh／ISM／HISM 元件，合計 1,475 個可見實例：綠樹 1,187、花樹 288。
- 綠樹三種分枝輪廓，花樹一種立體輪廓；原有葉色參數繼續沿用，各實例再有微幅亮度差。
- 每個元件的 Actor／相對座標、旋轉、縮放、可見性、碰撞設定、陰影設定與裁切距離維持不變；所有 HISM 世界實例 transform 數值逐一雜湊驗證。
- 另外 2,952 個非樹 StaticMesh 元件，其模型、材質、transform、碰撞及實例全部比對保留。包含建築、原有外牆、三語招牌、載具與雕塑；盆景不在本次替換清單。
- 原地圖備份在 `output/street-social-city-production/tree-upgrade-v1/UniversityCity-before-trees.umap`。不覆寫舊樹資產，不自動覆蓋備份。

## 幾何與材質

| 樹型 | 原近景尺寸範圍 | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---|---:|---:|---:|---:|
| camphor-a／b／c | 7 × 6.5 × 8 m | 4,628 | 2,644 | 360 | 8 |
| flower-a | 5.5 × 5 × 6 m | 3,788 | 2,476 | 948 | 8 |

面數皆為三角面。表中尺寸為近景設計範圍，並繼承原實例縮放。遠景 billboard 有額外透明留白及包圍盒擴張，不能把其包圍盒當成樹幹碰撞或近景實體尺寸。

綠樹以立體分枝和較小葉簇取代巨大交叉葉片。葉片使用現有透明葉枝貼圖、明確 Alpha Mask 與 Two Sided Foliage；花樹前三級均使用不透明幾何。樹皮有共用 2K 顏色、法線與粗糙度；金屬值為常數 0。未呼叫 Tripo 或新的 AI 生圖服務。

每種樹的遠景圖集為 2K、4×4 格：8 個水平方位乘 0°／45°兩種仰角，每格 512px。圖集直接由**通過 remesh 的 LOD0**在 Blender 烘焙，包含顏色／Alpha 與相機空間法線／葉冠遮罩。法線圖集使用線性 BC Masks，保留 A 通道；葉冠著色才套用原葉色，樹皮不跟著染色。

遠景朝向和圖集索引在頂點階段計算，再內插給像素階段，避免每個像素重算 atan2。採最近的烘焙方位；轉動時有離散方位變化，不宣稱完整幾何或連續角度重建。Blender→glTF→UE 的 Y 軸反射已反映於方位與法線基底。

四級 LOD 畫面占比為 `1 / 0.60 / 0.32 / 0.18`，材質啟用 LOD dither。實際切換距離依 FOV、樹大小與包圍盒而變；保留原有裁切／陰影距離，不用固定公尺數承諾切換。Nanite 未啟用。

## Remesh 與可重現流程

所有 16 組網格都執行使用者指定的 `E:/Paper/instant-meshes-rs/target/release/instant-meshes.exe`，以共用 `street_city_remesh_gate.py` 核對工具、來源、輸出雜湊、逐件命令、幾何誤差及面數。

本流程採明確的 `--recover-source-topology --project-source` 路徑：實際執行 Rust remesh，使用外部場結果作有界採樣，回復適合薄葉／分枝的來源拓樸並投影 UV。這不代表最終網格直接使用 Rust 原始輸出拓樸；完整 provenance 存於每級 `remesh-report.json`。前三級另外指定 `--smooth-shading`，此參數預設關閉，不改其他模型既有行為。

```powershell
# 未安裝前才能記錄原始基準；安裝後會拒絕覆寫。
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_probe
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_before

.\Tools\lua\lua.exe Tools/build_street_city_tree_upgrade.lua build 1 4
.\Tools\lua\lua.exe Tools/build_street_city_tree_upgrade.lua remesh 1 12
.\Tools\lua\lua.exe Tools/bake_street_city_tree_impostors.lua 1 4
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_import
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_install
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_refine

# 獨立程序重開正式地圖：先驗收，再於同一程序交錯量測舊樹／新樹。
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_compare
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_preview
.\Tools\lua\lua.exe Tools/publish_street_city_tree_upgrade.lua
```

工具不能同時寫入相同 UE 專案。僅匯入／材質微調／單樹預覽使用空地圖，正式保存及驗收才載入全城。安裝在保存前先完成保留契約驗證，保存後另開 Editor 再驗；不得把安裝程序內的檢查當作獨立重開驗收。

## 原生驗收與效能解讀

所有收據、原生照片、CSV 與圖庫位於 `output/street-social-city-production/tree-upgrade-v1/`。

- `tree_audit.json`：獨立重開驗收，含 3,629 項檢查、七種材質的實際編譯指令數；要求有效像素 shader，避免用預設灰材質通過。
- `tree_compare_before.json`／`tree_compare_after.json`：同一 Editor 程序交錯舊樹與新樹，九個相同鏡位，各有 PNG SHA 和 CSV 檔名。最後恢復新樹，**比較程序不保存地圖**。
- 每鏡位 1440×1000、預熱90幀、量測90幀，再略過前10筆。主比較為 GPUTime p50／p95；另列 RHI 計數。RHI 間接繪製計數不等於所有實例展開後的真實三角面數。
- `performance.json`／`index.html`：逐鏡位結果，必須如實呈現增加或降低；不能只用面數聲稱變快。這是 Editor SceneCapture 工作負載，非打包遊戲 FPS，其他 GPU 程序仍會造成波動。
- `tree_preview.json`：Unreal 強制 LOD 的單樹檢視，用於核對輪廓、法線及 billboard；不可把 Blender 圖集當成 Unreal 實拍。

錯誤與修正持續追加至 `Docs/STREET_ERRORS.md`。若重建來源，remesh 排程必須比對新 source SHA；僅有舊的 passed 狀態不得重用。
