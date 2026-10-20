# 常駐 GPU deform／LocalVertexFactory 格式驗收

## 本輪完成內容

- `OKAddRuntimeGpuPackPass` 將 GPU deform 結果打包成 float3 position（12 bytes）與 UE 原 FPackedNormal tangent X/Z（8 bytes）。保留 float position bits、原 signed handedness、UE RoundToInt 的規則；不改 UV、color、indices 或材質。
- `FOKResidentGpuDeform` 在 render thread 安裝 owned source／draw mapping 與首幀，後續只上傳本幀 affine palette。source 一次完整驗證，palette 每幀驗證；沒有每幀 CPU 頂點打包、來源重傳或正式 GPU readback。
- 位置／切線使用雙槽常駐 VertexBuffer＋UAV／SRV 資源；RDG 將 compute 輸出轉為 vertex／graphics SRV 可讀狀態。首幀 previous=current，後續 previous 保留上一幀；更換來源重置歷史，不使用跨 generation 累計計數選槽。
- generation 拒絕過期 install／update，拓撲變更要求重新安裝。非 finite／projective palette、非法 draw mapping、容量與 replacement budget 失敗保留已發布 buffers／generation 及呼叫者 Result。Release 使 generation 失效；舊 pooled/RHI 引用保留有效，沒有跨 graph 借用 FRDGBufferRef。
- 邏輯 budget 計算本代＋待替換本代＋一個 scratch dispatch，包含雙槽輸出；不代表 driver VRAM 或呼叫者額外持有的更舊 generation 的總量。正式 scene installer 仍需整合來源生命周期與整體 residency 管理。

## 編譯與完整回歸

`Saved/InteractionResidentDeformFinalBuild.log`：Result Succeeded，9.74 秒。`ResidentGpuDeformFullRegression`：22/22 Success、exit0、四個 DLL hashes stable、external Unreal0；包括实际男女 APNG live native palette、source snapshots／generation／bounds、嘴型 render、語音及原 stream upload 回歸。

### 實際 GPU vertex packing

`OpenKoikatsu.GPU.VertexPack` 實際 D3D GPU dispatch／readback：19,907 個頂點，position float bits 全一致、8-byte tangent **byte_mismatches=0**。CPU oracle 直接使用原 `FDynamicMeshVertex` 與 `TStaticMeshVertexTangentDatum<FPackedNormal>::SetTangents`，並非複製 shader 打包公式。

覆蓋一般方向、正負 handedness、全零與平行退化、超範圍 clamp、正負半整數捨入，以及所有 -8..8 三維整數方向乘 1..4 的共線組合。不足 capacity 與輸入／輸出 alias 拒絕測試通過。

`GpuPackCollinearProbe` 曾有 20/259 個 handedness byte 不一致，原 log 保留。根因是整數共線經 float(1/127) 解包後可能不再精確共線。保留非共線快路徑，只在負 handedness 的整數共線分支，按解包 float 的 IEEE significand／exponent 比較精確乘積，使用 12-bit limbs 表示 48-bit product；不依赖 GPU double、FMA 或任意 epsilon。新增廣泛共線測例後通過。

### 實際 GPU residency

`OpenKoikatsu.GPU.ResidentDeform`：三個分別 Execute 的 graph、每幀 129 頂點，更新 translation 後 **max_error_cm=0**。immutable upload 共 **18,576 bytes**，三幀 palette upload 共 **144 bytes**；沒有重傳 source／mapping。

測試覆蓋 current／previous 雙槽切換、呼叫者 source 於安裝後修改不影響 GPU、非法 palette／topology／draw mapping／budget／generation 不覆蓋 current、replacement 首幀 previous=current、Release 後拒絕舊 generation。readback 僅在測試。

既有 `OpenKoikatsu.GPU.RuntimeDeform` 同版再次 PASS，包含 signed weights、fallback、post-skin correction、secondary offset 及近 180° normal motion。

## 場景整合狀態

本輪尚未把 private stream scene proxy 切換為這些 buffers。其原材質／LocalVertexFactory／UV／color／indices 仍使用原路徑；不能把 synthetic buffer readback 稱作實際 APNG GPU draw，也不能宣稱 120FPS 已完成。

下一步仍需 scene proxy 消費 GPU stream、native immutable source generation／lease（避免每幀 native 全 source 驗證）、背景準備原子切換、同 palette conservative bounds、CPU 按需查詢，以及眼睛／嘴型／outline／shadow 畫面 oracle。所有資料需由 APNG runtime 建立，不新增角色專用 prebake；水槍維持功能，本輪不量測。

## 本版最後完整功能驗收

`PairEyesSpeedResidentGpuFinal`：audit_completed=1、functional_pass=1、exit0、四個 DLL hashes stable、external Unreal0。APNG entry 6158.357ms、first ready walking 8035.554ms，進場與首個 walking 在10秒內；walking 兩輪仍播放。first ready pair 72.690ms，12次已準備切換最大 click-to-play 17.376ms／最大 frame 30.145ms，300ms 切換門檻 PASS。

13 速度階段含 pause／continuous 調速之原 CPU 與 frame budgets 全 PASS，68 次調速 callback 最大0.032ms。shared clock、54嘴型、speech／voice cancel-and-replacement、head／eyes／belly、male replacement 及失敗保留、return、paired stable shadow depth oracle，完整功能 PASS。五張原始 PairEyes PNG 已逐張 view_image 確認 iris／highlight／眼球可見、閉眼後重新睜眼與播放正常，`eye-visual-review.json` 保存；stat fps 可見。

混合階段 mean 6.677ms、最大1303.685ms，最大仍未達300ms；播放 speed phases whole mean 約9.66–10.13ms、pause8.375906ms。混合平均與 paused 截圖135FPS不能作為播放穩態120FPS證據。GPU buffers 尚未被這個 scene proxy 消費，不宣稱本輪有 FPS 提升；前輪 quiet profile 失敗證據維持，不重跑未使用新路徑的性能比較以推論收益。水槍 SkipPaint。
