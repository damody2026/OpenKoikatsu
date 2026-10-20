# 雙人 GPU 場景水槍驗收（2026-10-07）

水槍功能驗收完成：APNG載入高模、原材質／骨骼物理與private GPU播放下，單人及雙人動畫都可實際射擊、命中、沉積並顯示塗層。塗層跟隨動畫，切換姿勢後碰撞query仍正確，停射／清除及返回重進通過。本輪確認功能正確，未驗證噴射期間120fps。

## 實作前計畫

1. 保留APNG高模、原材質／骨骼物理與GPU播放；沿用真實控制器水槍輸入，不直接寫入油漆資料。
2. 驗收原單人射擊，再於雙人動畫播放中分別射擊男女；驗證命中／沉積、private GPU表面覆蓋與當前query幾何，不能只確認GPU body能命中。
3. 停射後確認油漆附著位置隨動畫移動，切換姿勢後query仍正確；驗證體積守恆、清除、返回與重進。
4. 建置、Film回歸及實際1600×1000水槍功能audit；檢視射擊／附著圖，保留原始report、模組hash與錯誤紀錄。
5. 不以先前SkipPaint的120fps證據宣稱噴射時也120fps。入口超時仍延期。

## 補齊驗收缺口

原audit僅在男角載入前射擊。本輪新增獨立 `-PairPaint` 模式，於雙人動畫播放中使用原控制器的ScriptedPaintAim／開火路徑，各射擊3秒；每角色射擊前清空所有塗層，避免前段殘留造成誤判。

瞄準表面由runtime當前private GPU部件的非退化三角形選取，不固定mesh名稱、索引或角色資源；从上方追蹤同一三角形實際射擊，避免另一角色遮擋。新增非Shipping幾何檢查：刷新當前pose的按需CPU query，核對水槍碰撞頂點與query頂點；不修改產品碰撞規則、不直接灌入塗層。

## 最終實測結果

`PairPaintGpuFinalFunctional`：audit_completed=1、functional_pass=1、paired_watergun_pass=1、paired_watergun_clear_pass=1、paired_watergun_pose_query_pass=1。

| 雙人動畫射擊目標 | 新增命中 | 沉積體積 cm³ | 檢查private GPU表面 | 有塗層private表面 | query最大誤差 cm | 附著點0.7秒位移 cm |
|---|---:|---:|---:|---:|---:|---:|
| 女角 | 91 | 36.4 | 36 | 1 | 0 | 3.874845 |
| 男角 | 212 | 28.4 | 30 | 13 | 0 | 0.469212 |

兩角色射擊中骨骼動畫持續更新；停射後附著點隨動畫移動。換姿勢後兩角色query仍通過；守恆誤差檢查<=1e-7；清除後RemainingVolume=0且不在噴射。單人射擊、54嘴型／語音、男女替換與失敗保留、返回載入場景再重進亦通過。12次準備好姿勢切換，最慢23.969ms，切換區間最大幀56.724ms。

人工檢視 `Painted.png` 可見女角裙子塗層；`PairPaint0.png` 可見女角頭髮白色沉積，`PairPaint1.png` 可見男角黑髮上的白色沉積。兩段均先清空，無前段殘留。截圖含幾何診斷／截圖讀回負載，不能作120fps量測；尤其男角截圖顯示5.07fps，未隱藏此證據，也不據此宣稱穩態噴射效能。

## 建置與回歸

- `Saved/InteractionPairPaintFinalBuild.log`：Succeeded，14.71秒。
- `PairPaintGpuRegression`：31/31 PASS（較早同產品邏輯版本，之後只修正audit清空及runtime瞄準）。
- `PairPaintGpuFinalRegression`：目前最終binary 8/8 PASS：Film、Scene、MaskParity、ConservativeTransport、ChartCoordinates、RestoreControls、GpuStreamProxy、PrivateGpuSource。
- 最終功能與8項回歸均exit0、模組hash穩定、external_unreal_observed=0。兩次執行前後四DLL與目前launcher使用版本逐一相同。
- 相關source diff --check通過。水槍既有按需query路徑工作正常，本輪未發現需要更動的產品水槍功能錯誤；補充的是通用且可重跑的雙人驗收。

## 證據與重跑

- [最終功能report](../Saved/Tests/InteractionShowcase/PairPaintGpuFinalFunctional/report.txt)
- [單人塗層](../Saved/Tests/InteractionShowcase/PairPaintGpuFinalFunctional/Painted.png)、[女角雙人塗層](../Saved/Tests/InteractionShowcase/PairPaintGpuFinalFunctional/PairPaint0.png)、[男角雙人塗層](../Saved/Tests/InteractionShowcase/PairPaintGpuFinalFunctional/PairPaint1.png)
- [最終回歸log](../Saved/Tests/InteractionShowcase/PairPaintGpuFinalRegression/run.log)
- [目前模組驗收receipt](../Saved/InteractionWatergunGpuCurrentAcceptanceVerification.json)
- [錯誤及防重犯紀錄](INTERACTION_SHOWCASE_ERRORS.md)：保留初版未清空及相互遮擋的誤判，初版診斷不列入正式通過。

```powershell
./Tools/run_interaction_showcase.ps1 -Tag <新Tag> -Apng -Card C:/portable/OpenKoikatsu/output/CrowdAPNG/C001.png -PartnerCard C:/portable/OpenKoikatsu/Saved/TestCards/InteractionMaleAPNGR65.png -PairPaint -IgnoreEntryBudget -WindowWidth 1600 -WindowHeight 1000
```

PairPaint必須指定PartnerCard，不可和SkipPaint、Regression或Performance／PairProfile／PairSpeed／PairEyes混用。安靜播放先前的120fps驗收仍屬歷史binary；本輪因增加驗收程式已重新編譯，不把舊效能數據稱為目前binary的新量測。

## 2026-10-07 雙人水槍profile與兩個優化方案
已完成同binary兩輪無trace及獨立trace，詳細見INTERACTION_WATERGUN_PAIR_PROFILE.md。女角噴射72.33／88.91FPS，男角2.99／7.84FPS；男角最慢幀521.121ms，水槍120fps未達標。根因是液體遮罩傳輸與並行等待，並非男性高模三角形更多。兩個通用方案已寫入INTERACTION_WATERGUN_OPTIMIZATION_OPTIONS.md；建議先修正依物理面積的共用atlas分配，再做精度不變的稀疏格點傳輸。本輪沒有實作這兩個優化。profile新增計時／workload及audit後重建，原水槍功能驗收屬前版歷史證據；目前profile三run與四個DLL hashes已另外核對，沒有把舊安靜120fps移作噴射證據。
