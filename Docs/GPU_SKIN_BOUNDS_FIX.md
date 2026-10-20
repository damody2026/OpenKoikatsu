# GPU 五官缺失：通用姿勢包圍盒修正

2026-10-05。目標是修正 APNG／原卡 runtime GPU skin 顯示中的五官剔除，不是將百人場景的全部動畫改成 GPU pose，也不是 120 FPS 驗收。

## 原因及判別

Alice 的小五官元件沿用 reference-pose 網格包圍盒。角色身形／頭部骨骼改變後，該包圍盒沒有跟隨實際蒙皮位置。原本虹膜包圍盒中心約 Z=156 cm，顯示頂點約 Z=133–136 cm；原有 radius 約 3.7 cm，無法涵蓋頂點。錯誤 bounds 導致五官在渲染可見性剔除時消失。

- 隱藏臉皮、身體、前髮、飾品的既有測試未修復。
- 本輪關閉 morph／occlusion queries、使用舊 bone-buffer allocation、強制雙面也未修復。
- 相同 pose／材質，用 procedural 診斷代理繪製時五官能顯示。
- 對全部可見 head 元件放大 bounds 的隔離診斷恢復五官。正式實作沒有採用固定放大 100 倍。

## 實作

新增 `KKRuntimeSkinBounds.h/.cpp`，讓 runtime poseable／skeletal 元件以自己的 palette 算渲染 bounds；`KKPreviewActor` 建立元件時使用這兩個類別。原參考網格、貼圖、材質、深度測試與 GPU 蒙皮保留。

1. 每個網格首次使用時，按非零蒙皮權重建立各使用骨骼的 reference-space AABB，並快取各 morph 的 delta 區間。
2. 更新 bounds 時加入當前 active morph 的區間；負 morph 權重交換區間端點，未被 morph 影響的頂點仍涵蓋在零位移區間。
3. 以當前 `inverse-reference × component-space` palette 轉換這些 AABB，取聯集再轉到元件世界座標。
4. 保留 1 cm 的量化／描邊餘裕。palette 更新只處理使用骨骼和 active morph，不每幀遍歷頂點；asset 或 skin-weight buffer 替換時重建快取。

非負且正規化的蒙皮權重讓頂點位於各骨骼結果的凸組合中，因此該聯集保守涵蓋蒙皮頂點。此 runtime builder 只有 LOD0；尚不把這項測試解讀為任意 cloth／mesh-deformer 的 bounds 驗證。

## 已完成的驗證

- `Saved/Tests/AliceGpuBoundsBuildR4.log`：Development Editor 編譯、連結成功。
- `AliceGpuPosedBoundsR1`：Alice 7 套服裝，保持自動表情，GPU 圖片已產生。
- `GpuFaceBoundsAcceptanceR1`：Alice 7 套服裝，固定表情、1920×1080 近距離 GPU 截圖。
- `GpuFaceBoundsOtherCardsR1`：March 7th／Augusta 各 7 套，固定表情近距離 GPU 截圖。
- 共 21 個 GPU outfit、294 筆元件 bounds 稽核，所有已顯示頂點超出渲染包圍盒的距離均為 0 cm。
- `CpuFaceBoundsAcceptanceR1`：Alice 相同 7 套、相同固定表情和鏡位的 CPU 圖。
- `GpuFaceBoundsComparisonR1/report.json`：Alice 全圖 RGB 平均絕對差 0.777–0.908／255；眼部 ROI 4.933–5.090／255。五官恢復、位置接近，仍有既有高光及臉部描邊差異，沒有聲稱逐像素一致或完整畫質驗收。

以上是功能測試。部分程序同時執行，因此不作 frame-time／120 FPS 效能證據。

## 重現

`Tools/run_gpu_skin_visual_audit.ps1` 使用隔離 host，可傳 `-AuditHost`。指定卡號時傳字串，例如 `-CardIds '2,1,50'`，不要傳 PowerShell 陣列。

```powershell
Tools/run_gpu_skin_visual_audit.ps1 -Tag FreshTag -CardIds '2,1,50' -AllOutfits -WornOnly -FacePortrait -FreezeFace -RequireBounds
```

加 `-CpuSkin` 取得 CPU 對照。`Tools/compare_gpu_skin_face_images.py CPU_DIR GPU_DIR FRESH_OUTPUT_DIR` 產生 CPU／GPU／差異圖及數值，不以單一全圖指標自動宣告視覺通過。

正常互動讀卡器可用 `CardReaderDevelopment.cmd -GpuSkin` 啟用 GPU。原預設 CPU 模式仍可用於對照；已開啟的程序必須重新啟動才會載入新版 DLL。

錯誤與排除紀錄在 `Docs/CROWD_GPU_POSE_ERRORS.md`；原卡、APNG 與全部服裝資料均未重寫。

## 後續：GPU 臉部描邊

CPU 的 `AddOutlineSection` 建立臉部描邊，GPU 分支此前只建立部分衣服的描邊，造成下巴／耳朵輪廓缺失。現在對具有原生 outline pass 的 face skin 建立獨立反向繞序 skeletal shell，使用相同來源線寬、detail/alpha mask、骨骼權重及表情 morph。沒有按卡號分支。

- `KKSkeletalBuilder` 的可選 face-cavity 排除與 CPU 的 UE UV V > .91 規則一致。保留嘴唇附近三角形，不畫口腔內部的反向描邊。
- shell 與表面接收相同 local pose 和 morph 權重；跟隨重新載入生命週期，卸載時銷毀。
- reader skin audit 比較 shell/surface 的實際蒙皮頂點，門檻 .001 cm，並檢查實際 renderer bounds，门檻 .05 cm。
- 測試可設 `OK_READER_EXPRESSION_PROBE=1`，固定嘴巴 .65、眼睛 .35、眉毛 .6 的開合率，驗證非預設表情。
- 此步只補回原先 CPU 已有的臉部 outline pass，不表示所有 shader family 的 GPU outline 或百人 GPU-only pose 已完成。

`GpuFaceOutlineBuildR3.log` Development Editor 成功；`GpuFaceOutlineAcceptanceR1` 三名角色共 21 套全部通過，21 個 face shell 各 2695 頂點與表面位置差 0 cm、bounds 外逃 0 cm。`GpuFaceOutlineComparisonR1` 的 Alice 七套 CPU/GPU 近照全圖 RGB MAE 為 .696–.875/255。下巴描邊已恢復；眼部仍有差異，沒有把全圖平均當成五官完全一致的證明。本輪未量測每幀平均時間或作 120 FPS 驗收。

## 表情對照發現的 CPU morph 空間錯誤

原 CPU `ApplyMouth` 將 reference-space morph delta 直接加到已經 skin 的 `Part.BaseVertices`；GPU 則先加 morph 再 skin。當頭部骨骼具有角色身形縮放／旋轉，兩者不同。半閉眼測試原 CPU 幾乎閉眼，眼部 MAE 20.392–20.400/255。

修正 CPU 為先加總 reference morph delta，再以角色自己的 skin palette 轉換方向向量（忽略平移）後加到 posed baseline，順序與 GPU 一致。無可用 palette 的非蒙皮來源保留原位移 fallback；沒有按 Alice、卡號或固定骨架參數分支。零 morph 更新跳過 skin 運算。

隔離試驗 `FaceMorphSpaceProbeComparisonR1`：Alice 七套張嘴／半閉眼對照的全圖 MAE .427–.588/255，眼部 2.760–2.898/255。`GpuFaceOutlineExpressionR1` 七套 GPU outline 均與當前 morph surface 的頂點差 0 cm、bounds 外逃 0 cm。正式程式已移除試驗環境開關，`GpuFaceMorphSpaceBuildR2.log` Development Editor 編譯成功。尚未對所有角色、所有表情、原作 Unity 動畫或效能進行完整驗收。

正式預設 CPU 修正以 `CpuFaceMorphDefaultAcceptanceR2` 重測 Alice 七套，全部 valid=1、exit=0；沒有 `OK_CPU_MORPH_SKIN_TEST` 或卡號修正開關。CPU/GPU 表情對照在 `GpuFaceExpressionDefaultComparisonR1`。R1 於 scene 啟動前遇到測試 host BuildId 失配，已記錄、對齊隔離 host 後重測；不算角色驗證失敗或通過。

## GPU 眼睛高光 UV

CPU 眼睛使用 `eye_sample_uvs` 為主貼圖座標，並將 Unity UV1/UV2 傳到 TEXCOORD1/2，獨立採樣兩張高光貼圖。原 skeletal builder 使用 raw UV0／generic UV4 overlay，某些眼睛只建立一個 channel，因此即使兩條路徑使用同一材質，也會顯示不同的高光。

現在依來源 `eye_draw` 選擇三個 UV channels：`eye_sample_uvs`（缺少時回退 raw UV0）、`uv2`、`uv3`。Body、face 及 hair 的配置保留。這是 source shader family 修正，沒有卡號或角色名稱例外，不需重寫 APNG、貼圖或增加幾何面數。

`GpuEyeUvBuildR2.log` Development Editor 成功。Reader skin audit 新增 `eye_uv`，直接比较 GPU buffer 與同一來源建立的 CPU UV streams，缺少三組 channel 或誤差超過 .002 就失敗。

驗證 `GpuEyeUvAcceptanceR1`／`CpuEyeUvAcceptanceR1`：Alice、March 7th、Augusta 各七套，共 21 組同鏡位／固定表情近照，兩條路徑均 valid=1、exit=0。42 個眼睛 UV buffer 全部保留三組 channel，與來源最大誤差 .000343246（half-precision UV 量化），通過 .002 門檻；face outline 及 renderer bounds 稽核保留並通過。

`GpuEyeUvComparisonR1/report.json`／`summary.json`：March 全圖 MAE .324–.478/255、眼部 1.900–1.987；Alice 全圖 .459–.620、眼部 3.107–3.177；Augusta 全圖 1.138–1.588、眼部 9.254–9.910。已人工檢視 Alice／Augusta 近照，高光層出現且接近 CPU；仍有幾何／輪廓差異，尤其 Augusta，不宣告完整外觀一致。使用相同最新 CPU outfit00 比較 Alice 舊 GPU 與本次 GPU，眼部 MAE 4.98717 → 3.17340。UV buffer 驗證支持此修正，圖片 MAE 只描述差異。

本輪是並行功能測試，沒有正式場景 frame-time profile，不能作 Development 120 FPS／8.33 ms 的性能證據。

範圍限制：這一步修復載入時的 authored eye/highlight UV。`SetEyeGaze` 目前更新 procedural streams，GPU 的動態視線 UV 更新仍待後續實作；不要將靜態近照通過當成動態 gaze 或所有表情完整驗收。

後續已補上 runtime GPU gaze 材質參數路徑；有效動態測試以 `GpuEyeGazeSequenceR2`／`GpuLiveEyeGazeR2` 為準，R1 是失敗診斷證據。詳見 [GPU_EYE_GAZE.md](GPU_EYE_GAZE.md)。此修正仍不是 crowd GPU-only animation 或 120 FPS 驗收。

2026-10-07：雙人高模播放優化後，CardReaderDevelopment.cmd / run_card_reader_development.ps1 已預設 GpuSkin=true，正常視窗保持1600×1000。明確CPU對照可用 PowerShell `Tools/run_card_reader_development.ps1 -GpuSkin:$false`。實際雙人播放結果與fallback契約見 INTERACTION_PRIVATE_GPU_PLAYBACK_ACCEPTANCE.md；不以DryRun冒充性能驗收。
