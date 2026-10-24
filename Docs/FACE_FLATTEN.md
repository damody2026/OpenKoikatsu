# 臉部壓平（鳴潮式三渲二）

2026-10-23。頭部沿視線方向壓扁，讓鼻子、臉頰從側面與 3/4 角度不再凸出，臉看起來像平面插畫。`StreetSocial.cmd` 與 `CardReaderDevelopment.cmd` 都有。

## 做法

- **母材質**：`Tools/GpuPoseAudit/Source/GpuPoseAudit/PrepareFlatFaceMaterial.cpp` 把每個共用母材質（含 `_Outline`、`_Fg`、`M_OKTearLit`）複製成 `<名稱>_Flat`，多一個 World Position Offset 節點：
  - 只有在頭部中心前方的部分會往平面移動，後腦不動。
  - 描邊沿用原本的外殼位移，再跟著表面一起壓。
- **只給頭部用**：`Role == "head"` 的零件（臉、眼、眉、鼻、淚）改用 `_Flat` 母材質。身體仍用原本的母材質，陰影與覆蓋計算維持原狀。
  - 判斷在 `OKCrowdRuntimeScene.cpp`（街區）與 `KKPreviewRuntimeMaterials.inl`（讀卡器）。
- **參數**：`__OK_FaceFlatPivot`、`__OK_FaceFlatDir`、`__OK_FaceFlatten`。
  - 方向由 CPU 依玩家鏡頭算好傳入，每個繪製 pass 都用同一個方向，影子由畫面上那張壓平的臉投出。
- **效能**：GPU 端是每個頭部頂點多做幾個乘加。CPU 端每個角色每幀只算一次（`OKFaceFlattenChanged`）：
  - 頭移動不到 0.5 cm、方向變化不到約 0.5° 就不寫材質參數。
  - 街區中離鏡頭 25 m 以外的角色不取骨骼。

## 設定

| 控制台變數 | 預設 | 說明 |
|---|---|---|
| `ok.FaceFlatten` | 0 | 0 關閉 .. 0.9 最強（2026-10-24 起預設 0，使用者決定街區與讀卡器都預設不壓平）。到 1 會把臉、眼、口腔、牙齒壓到同一深度，互相穿插出黑塊，所以上限是 0.9。啟動參數 `-FaceFlatten=` 也可以設定。街區以 0 啟動時，頭部改用原本沒有位移的母材質，遊戲中再調高不會生效；讀卡器的頭部一律用 `_Flat` 母材質（強度 0 時不位移），滑桿隨時可以打開。 |
| `ok.FaceFlattenLock` | 0 | 1：壓扁方向固定在當下，轉鏡頭可以從側面確認臉真的扁了。 |
| `ok.FaceFlattenForward` | 2 | 壓扁平面在頭部骨骼前方幾公分（朝鏡頭方向）。 |

讀卡器的編輯頁在「眼睛跟隨鏡頭」下方有「臉部壓扁」滑桿與「固定壓扁方向（轉視角檢查）」勾選框。

要測試時，街區用 `StreetSocial.cmd -Exec "ok.FaceFlatten 0.6"`（必須在啟動時就給），讀卡器直接拉滑桿，或用 `CardReaderDevelopment.cmd -- -ExecCmds="ok.FaceFlatten 0.6"`。

## 重建

1. `Tools\lua\lua.exe Tools\prepare_face_materials.lua`：在 materials.json 的每個 root 產生 `_Flat` 母材質，已有的會保留。
2. `Tools\lua\lua.exe Tools\rebuild_street_social_patch.lua`：把它們 cook 進街區的 runtime patch。讀卡器在 editor 裡直接讀未 cook 的資產。
