# 實際 APNG 私有來源 GPU 轉接驗收

日期：2026-10-07。範圍：runtime owned source/pose/draw converter、native Unity GPU skin 模式與 resident 更新。實際雙人場景尚未切換至這條衣髮 GPU 路徑。

## 本輪實作

- 新增 KKPrivateGpuSource：以有效來源 revision 擷取原始頂點/方向/四個 signed float weights/bone slots，將借用資料複製成 owned arrays。後續 OKReadPrivateGpuPose 只取得 palette/post，不重建頂點。
- Native matrices 保留 consumed upper 3x4 affine rows，將未使用的 homogeneous row canonicalize。最後 identity slot 保留原本未加權 fallback。
- OKBuildPrivateGpuDraw 保留來源索引次序、重複 draw vertices 與 UE secondary offset/rest normal/section normal。無效來源/索引/非有限值不修改 caller 已有資料。
- GPU 明確 NativeUnitySpace：native skin/normalize -> native post/rot_normal -> Unity (X,Y,Z) 到 UE (-X,Z,Y)、位置乘100 -> UE secondary normal-motion rotation。退化 post 的 native incoming direction fallback 保留；不能把 post/basis 併為單一階段。
- resident install 保存座標模式；palette-only updates 使用同一 immutable mode。新模式已可由原始 stream publication request 安裝。
- private FFI palette/stages 共用 skin_body 選擇，coordinated implanted clothes 依原 renderer 使用 solved clothes_dynamic_runtime。
- 不完整方向來源回報 unsupported 並保留 CPU，不刪部件、不減模、不量化權重、不預烘焙角色資源。

## 實際 GPU 驗收

PrivateGpuSourceQueryFullRegression：25/25 PASS，exit0、四模組 SHA256 穩定、external Unreal0。最終編譯 InteractionPrivateGpuSourceQueryBuild.log Result: Succeeded。

實際女性 C001 與男性 InteractionMaleAPNGR65 APNG，共用 walking clip，三個動畫時間點。當前姿態先求解一次，再像 viewer 一樣 Skin() 發布同姿態 CPU 查詢資料，沒有額外推進 physics。

- 210 GPU publications，456,612 draw vertices；包含完整 reverse-index surface 與 duplicate-index secondary displacement。
- 相對原始 native 當前位置/法線/切線：最大 position 4.58011964e-5cm，normal 6.94219398e-6，tangent 2.73142803e-7；原 metadata/handedness保留，全部PASS。
- 女性一個部件缺少完整方向串流，仍保留CPU；未刪幾何。其他70個private parts（三幀210次）全部覆蓋。
- NativeResidentDeform：3個獨立graphs，GPU位置exact、packed方向byte exact；零方向先fallback nativeZ，collapsed post保留後再轉UE Y；immutable144bytes僅上傳一次，3次palette合計144bytes。
- RuntimeDeform：129 sources/387draws/6 correction-space cases，共2322個GPU輸出；UE position3.7785475e-6cm、native position.000636474603cm、normal4.19208381e-7、tangent1.02367697e-7，既有UE門檻保留。包含signed/fallback/zero方向/退化post/near-opposite。
- 原LocalVertexFactory pack19907頂點byte mismatches0、resident/實際sceneproxy raster/語音嘴型等回歸PASS。

Native修正後重跑四個測試全PASS：女性133440頂點、男性174294頂點，staged native oracle誤差0、568暖讀每來源/layout只驗證一次；來源revision生命週期與invalid FFI拒絕PASS。證據 InteractionPrivateGpuRigNativeFemale/Male/Revision/Invalid.log。

## 失敗與更正

初版GPU oracle用尚未發布的PartAt positions，比對到前一份CPU幾何，導致61cm差異。修正為同一已求解pose上的Skin()查詢後，誤差降為.0000458cm。不得重跑dynamics當作參考。初版/調查版log完整保留：PrivateGpuSourceFullRegression、PrivateGpuSourceRigOracle；兩次測試編譯錯誤與座標來源調查亦記於INTERACTION_SHOWCASE_ERRORS.md。

## 尚需場景整合

這證明实际APNG來源的GPU計算與原 renderer 幾何一致，還不是实际衣髮/toon/outline/shadow的新渲染圖像驗收。secondary映射oracle使用真實source加驗證位移，不是正式載入component全部authored sections。

下一步：runtime conservative bounds（signed coefficients不能套正權重convex hull）、actual loaded-part secondary draw capture、按需CPU查詢、all-section pending install/CPU fallback發布協調，再完整新路徑圖像與whole-frame/Present量測。完整性能目標未宣稱完成。

## 本次優先播放性能
入口 timeout 依使用者指示暫停處理。PairEyesSpeedPrivateGpuSourceTrace 已完成完整功能 PASS（audit_completed=1/functional_pass=1），但 whole frame mean14.431ms、max1139.748ms，trace 不作安靜120FPS證據。
安靜 CPU 基線 PairGpuBeforeBaseline：phase0/1 frame mean10.131214/10.141897ms，Present98.699849/98.602828fps；advance7.038734/7.101827ms。phase2較重15.440673ms/64.784969fps。
新增 runtime signed interval bounds 與 actual GPU caller 的第一段；PrivateGpuDisplayBoundsRegression 7/7 PASS，含實際兩 APNG、GPU方向/packing/resident/proxy及bounds。GPU caller 本輪首次 profile 暴露 readiness 排除錯誤，已保留且修正，不能宣稱完整高模 GPU 已驗收或120FPS。
