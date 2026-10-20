# 雙人高模 GPU 基礎驗證

## 本輪完成範圍

參考 Crowd 的 runtime GPU palette／skinning、transient render mesh 與共用資源架構，新增明確的 `preserve_source_geometry` 模式。`OKReduceRuntimeMeshesAsync` 先驗證輸入 streams／indices；啟用時保留所有輸入 vertex attributes／skin／mouth endpoints／indices，完全不執行 simplification。Crowd預設仍減面，品質保留與triangle-budget compliance分開回報。

`SourceGeometryPreserved`、source vertices／triangles與最終triangles寫入角色report。此處的source是runtime捕捉的visible parts；Crowd既有presentation過濾仍存在，不能當原互動場景所有part／shader／outline／shadow的品質驗收。

新增 `Tools/run_two_actor_high_detail_gpu_probe.ps1`：恰好兩個使用者輸入APNG、兩個準備worker、原始幾何、實際GPU provider／matrix readback／三視角CPU-GPU捕捉、非空兩角色rows及triangles保留檢查、像素比對。fresh tags與四DLL hash記錄；不標記FPS或paired interaction驗收。Editor診斷使用明確 `editor_common_materials` 設定與現有共用材質資源，正式Game仍要求共用cooked shader library。

## 建置與回歸

- `InteractionHighGpuGeometryBuild.log`：四模組Succeeded，52.48秒。
- `InteractionHighGpuEditorBuild.log`：四模組Succeeded，12.38秒。
- `HighGpuGeometryRegression`：4/4 Success。
- `HighGpuGeometryFinalRegression`：14/14 Success，包含新增PreserveSourceGeometry以及既有speech／mouth、camera／UI、leader／affine、CPU-GPU stream packing、snapshot；exit0／四DLL stable／externalUE0。
- 新增原資料測試保留四個UV通道、normal／tangent與handedness、color、12個skin slots、morph endpoints及indices；拒絕越界index，原預設模式不會偷偷改為不減面。

## 實際高模GPU probe

| APNG | source vertices | source triangles | GPU建網格triangles |
|---|---:|---:|---:|
| CrowdAPNG/C001.png | 58,472 | 87,806 | 87,806 |
| CrowdAPNG/C002.png | 76,158 | 111,049 | 111,049 |

`TwoHighGpuSupportedSourceProbe`：2/2 GPUready，failures0，6,345 matrices，mismatch0，max error0.0000076294；三視角C001 CPU/GPU comparison PASS，RGB差異0。

`TwoHighGpuSupportedSourceReverse`：反向選卡驗證C002，2/2 GPUready，同樣6,345 matrices／mismatch0／max error0.0000076294；C002三視角comparison PASS，RGB差異0。兩輪front CPU/GPU及side/back GPU原圖已人工檢視，確認是實際可見角色、頭髮與眼睛內容，非空白圖。擷取在相同凍結pose的不同真實frame，不是假稱same-frame。

兩輪GPUready11,805.210／12,523.864ms，10秒目標未達。profile_seconds=0，沒有Present／FPS採樣，不能拿這些probe宣稱120FPS。各輪四DLL stable；測試腳本後續版本另增加foundation_pass欄位，不能將早期receipt沒有該欄位當成有執行新版metadata驗證。

## 保留的失敗與整合限制

1. `PairHighGpuSourceProbe`：Editor沒有Crowd staged Game的共用shader library，未進入GPU，不能算成功；明確Editor共用材質模式解決診斷環境差異，正式Game檢查未放寬。
2. `PairHighGpuSourceProbeEditor`：實際male APNG之 `Koikano/main_clothes_emblem` 未被Crowd共用recipe涵蓋，complete1/2，拒絕。沒有刪除該part或使用近似shader。兩張female成功證據不能代替male成功。
3. Crowd本身不等同原互動renderer：原始part visibility、任意嘴型／camera gaze、live paired contacts／physics、cast shadows及120Hz時間精度仍須專門整合；其60Hz播放參數量化與presentation規則不能照搬。

## 決定

採用Crowd的GPU幾何／palette架構方向，但雙人版本重用現有互動場景材質與即時native solver。先搬髮型、服裝及配件的CPU LBS／draw generation／packing到GPU，CPU只保留即時互動求解與必要的查詢；保留眼睛及morph特殊路徑直到其GPU oracle通過。

不改APNG、不需要角色專用預烘焙；進場先保持可操作，背景準備後原子切換GPU。此次完成品質入口與GPU基礎probe，尚未將原男女互動场景改為此backend，也未達10秒GPUready／120FPS。完整整合順序見 `INTERACTION_TWO_ACTOR_HIGH_DETAIL_GPU_PLAN.md`，失敗紀錄見 `INTERACTION_SHOWCASE_ERRORS.md`。
