# 一對一互動：2026-10-07 本輪實作與驗收

本輪通用修正已實作，最新版現有功能驗收通過。全部互動平均≤8ms／120FPS目標仍未完成，不能以功能PASS代替性能PASS。

## 實作

- 濕面位置與法線在當幀unique vertex gather只轉換一次，相鄰面共用同一結果；原per-face路徑保留作oracle。
- BodyDynamicRuntime reference scratch重用allocation，每次完整還原當幀source後重新計算；不保留跨幀矩陣或借用pointer。
- WorldPlan每次完整驗證ID/order/parent，TRS以10個f32 bit pattern判定；自身或父節點改變時執行原公式，首次與topology變更完整重建。動畫／物理更新頻率不變。
- runner自動補console指令分隔，避免驗收命令被吞掉。

所有衍生資料在runtime建立；沒有新增角色專用預計算資源。1024濕面atlas與高模保留。建置遇到的陰影編譯阻擋採最小修復，Coverage/Mesh回歸通過。

## 最新binary功能驗收

Rust release及UnrealEditor Development完整建置成功。原生pose17項、Ver02 reference/history oracle、實際男女APNG兩姿勢64幀完整rest/world/reference/私有particles精確比對通過。

UE八項PASS：StreamUpload、GpuStreamProxy、Paint.Scene、MaskParity、Film、RenderedMouth、ShadowSubmit.Coverage、ShadowSubmit.Mesh。Scene新舊頂點/法線/UV/顏色/tangent/bounds、GPU atlas與45個真實滴落oracle通過。

| 最新驗收tag | 結果 |
|---|---|
| InteractionWetIncrementalWorldEyesR1 | functional_pass=1；五張近拍逐張確認neutral/tracking/closed/reopened/playing正常；54嘴型、語音、行走loop、雙人控制通過 |
| InteractionWetIncrementalWorldPairPaintR1 | functional_pass=1；男女水槍／附著／清除／當前pose query通過，413hits；12切換最大21.527ms，同步控制最大104.061ms |
| InteractionWetIncrementalWorldNavigationR1 | functional_pass=1；加載3D、編輯與互動場景分離及返回流程通過 |

三輪均exit0、external0、isolated1、DLL hash stable。證據保存於Saved/Tests/InteractionShowcase/各tag，包含report.txt、run-evidence.json、完整run.log與截圖。

## 無trace正式性能

InteractionWetIncrementalWorldAllControlsVisibleR1：1600×1000，相同實際男女APNG，無其他build/tests；十階段完成、exit0/external0/isolated1/hash stable。

| 階段 | 平均 ms | Present FPS |
|---|---:|---:|
| 乾動畫1x | 5.673944 | 176.244016 |
| 乾動畫4x | 5.683627 | 175.948903 |
| 眼頭控制 | 5.819586 | 171.930071 |
| 嘴型 | 5.908142 | 169.352638 |
| 語音 | 5.835729 | 171.354098 |
| 持續肚子 | 6.505829 | 153.709811 |
| 女角射水與控制 | 8.280550 | 120.723721 |
| 男角射水與控制 | 9.801643 | 102.036506 |
| 停射後濕面播放 | 10.194791 | 98.037021 |
| 清除濕面 | 5.754306 | 173.773715 |

女角即使FPS>120，仍超過8ms，因此三個濕面階段皆FAIL。跨run小差異不足以證明各候選的性能因果收益。CoatingParallel=0正式對照無穩定收益，預設保持1。

## 根因與尚未完成項目

同一有效trace、全部thread/timer、完整paired frames比較：肚子乾播放Frame6.319949ms，濕面9.801551；新增3.481602ms中PaintTick2.577046及PairAdvance增量.381163解釋約85%。RT SceneRender3.835076→3.859764近乎相同。主要新增CPU工作為濕面持續更新，不能歸因外部工作或GPU三角形。

下一輪應以runtime通用position binding／GPU濕面publication減少CPU deform與串流組裝，保留當幀跟隨、simulation cadence、普通skinned與private GPU的fallback與原結果oracle；大範圍修改前先完成獨立設計及精確功能驗證。全功能120FPS仍列未完成；入口预算暫緩，依使用者先前優先順序。

詳細計畫與全部失敗/修復見INTERACTION_ALL_CONTROLS_120FPS_PLAN.md、INTERACTION_CROWD_PROFILE_COMPARISON.md、INTERACTION_SHOWCASE_ERRORS.md。
