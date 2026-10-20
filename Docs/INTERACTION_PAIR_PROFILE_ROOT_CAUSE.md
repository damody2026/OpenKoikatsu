# 雙人動畫效能根因分析（R73，2026-10-06）

## 結論

目前的主要限制在 CPU：原生動畫更新與程序式頂點顯示在每幀的關鍵路徑上先後執行。GPU 約 1.45ms，還有餘裕。不能只把配件物理 CPU 合計、Render Thread 的等待或 verbose log 的耗時，當成完整幀瓶頸。

本輪依使用者指示不測水槍。沒有觸發射擊，兩個量測窗口額外命中皆 0。使用從 APNG 載入的兩位角色、共用 PairedAnimations、空 GameRoot，沒有使用每卡特製資源。

## 證據與量測方式

- 無 trace 的 R72：兩個固定姿勢完整平均 10.008／10.656ms，實際 Present 99.919／93.846fps；GPU 1.412／1.406ms。120fps 尚未通過。
- R73 只新增 profiling 標記，未新增動畫／物理演算法優化。`PairProfileR73NativeRetry` 的四個 DLL 首尾 SHA256 相同，`external_unreal_observed=0`、`performance_evidence_isolated=1`。Trace 整輪兩姿勢 10.457／10.390ms，Present 95.632／96.239fps；trace 有量測開銷，正式 FPS 仍以上面的無 trace 跑法為準。
- 依 quiet phase anchor，暖機後各取 3 秒，再保留完整 paired frames：第一姿勢 294 幀，第二姿勢 293 幀。完整引擎 Tick 平均 10.185／10.196ms；不是只看動畫 callback。
- 包含全部 8 個 native worker IDs：78～85。RoleSample 分別為 588／586 次，等於每個完整幀兩個角色。私人物理皆每幀 37 次，蒙皮皆每幀 71 次。
- 匯出全部 CPU/GPU/Frame 事件後，依 thread metadata ID 排除非 CPU 事件，保留全部 timer 建立 exclusive event tree。避免同名 worker 被篩選器漏掉。
- quiet phase 0／1 與 verbose phase 2 分開，後者不納入根因數字。分析程序在 quiet 窗口開始前已完成；没有同期編譯。

## 主執行緒關鍵路徑（ms／完整配對幀）

| 範圍 | 姿勢一 | 姿勢二 | 說明 |
|---|---:|---:|---|
| 完整 Engine Tick | 10.185 | 10.196 | 包含通用引擎／UI／場景工作 |
| PairAdvance | 7.035 | 7.042 | 以下子項位於這個範圍，不能再加一次 |
| 原生更新 PairNative | 3.880 | 3.860 | 同步呼叫完成後才能顯示 |
| 角色 SkinDisplay（兩角色合計） | 2.589 | 2.593 | 包含骨架發布與以下 streams |
| 程序式頂點 streams | 2.025 | 2.013 | SkinDisplay 的子項 |
| 其中：來源轉換／準備 | 0.430 | 0.433 | 第一個 ParallelFor 階段 |
| 其中：generated draw／GPU 打包 | 1.588 | 1.574 | 第二個 ParallelFor 階段 |
| 眼睛／表情／語音面部更新 | 0.338 | 約 0.34 | 必須保留正確顯示 |
| deferred shadow 請求 | 0.006 | 約 0.006 | 已沒有每幀重綁大量 MID 的成本 |
| 接觸 morph | 0.169 | 約 0.17 | 配對後當幀處理 |

`ParallelFor Task` 在 GameThread 的實際執行合計為 1.665／1.649ms，`ParallelFor.Wait` 約 0.113ms。這表示 streams 的主要成本是頂點工作本身，不只是等其他 worker。兩角色目前各自先轉換來源頂點，再產生 draw／打包；共四個同步批次。角色骨架已走 GPU／exact leader，私人配件仍在 CPU 蒙皮後發布程序式 geometry。

PairAdvance 外約 3.15ms 是另一个不能忽略的預算：viewport draw 約 1.09／1.11ms，還有 skeletal mesh 更新、Slate、World／task 調度及 deferred captures。這些範圍彼此也有父子包含，不能直接加 inclusive 表格求總和。

## 原生 3.86～3.88ms 的分解

| 主執行緒等待／執行範圍 | 姿勢一 ms | 意義 |
|---|---:|---|
| 資源／配對 validation | 0.410 | 每幀核對身份、來源與資源 revision |
| 兩角色 sample＋等待 | 1.250 | 兩角色平行，但呼叫者要等較慢角色 |
| 接觸／IK | 0.554 | 兩角色之間的同步屏障 |
| dynamics＋等待 | 1.657 | 身體、衣物、骨架 export、配件物理與蒙皮 |

兩姿勢分解相近。Native Dynamics 已平行，不是未使用多核心。

以下是 8 個 worker 的 **CPU 工作合計／配對幀**，不是上述等待時間，也不能加進完整幀時間：

| Native worker 工作 | 姿勢一 ms | 姿勢二 ms |
|---|---:|---:|
| 37 次配件物理解算 | 3.138 | 3.053 |
| 71 次 LBS 蒙皮 | 2.435 | 2.374 |
| 兩角色 Animator sample | 2.232 | 2.237 |
| 蒙皮 palette 準備 | 0.797 | 0.762 |
| 物理 placement／collider setup 的 exclusive 工作 | 0.431 | 0.405 |
| 骨架 export | 0.415 | 0.392 |
| 衣物物理 | 0.334 | 0.340 |
| 身體物理 | 0.276 | 0.276 |

因此只針對配件物理做小優化，不能直接省掉表內的 3ms 完整幀時間；它原本就分散到多個 worker，還受同步點與後續 CPU 顯示限制。

## Render Thread 與 GPU

- Render Thread 的 `WaitForTasks` 約 5.138／5.149ms；這是等待，不是 GPU 花了五毫秒。
- 頂點 stream upload 約 0.676／0.679ms inclusive，真正 exclusive 約 0.622／0.626ms；每幀約 126 個 section 更新。
- 每幀 3 個 SceneRender（主視圖＋兩個 depth capture），陰影渲染仍有成本，但當前 GT shadow request 已降到約 0.006ms。
- GPU 約 1.45ms，降低解析度／眼睛品質／停用物理，沒有針對已確認的主要 CPU 管線成本。

## 依證據排列的後續工作

1. **優先處理 streams 的重複資料傳遞與同步批次。** 保持 owned CPU sections、bounds、GPU tangent 量化完全一致，研究把來源轉換與 draw packing 合併到同一批工作；以舊路徑逐頂點／GPU stream 比對。其目前約 2.02ms，且在 GT 的關鍵路徑上。
2. **拆細 Animator sample／contact 的內部重複骨架計算與 revision 核對。** 合計約 2.2ms 的原生同步段，須保持每幀來源驗證與接觸先後順序，不能以過期快取掩蓋。
3. **GPU 私人蒙皮是結構性候選，尚未實作或驗證。** 應 runtime 建立通用 renderer／palette，保持 signed weights、affine transforms、outline、morph 與當前 CPU 查詢能力；不能假定移到 GPU 就能省掉所有 worker CPU 合計，也不能要求 APNG 附帶特製資產。
4. **對 PairAdvance 外的 3.15ms 做第二個專門 profile。** 檢查 owned skeletal renderers、viewport／Slate 與 end-of-frame 更新，依 exclusive 與事件關係決定，不能省略眼睛 follower 更新來取得數字。
5. 每次只改一個已定位環節，用相同 APNG／兩姿勢重跑正常 RHI、實際 Present、速度與眼睛／嘴型驗收。目標仍是平均完整幀 <=8ms、Present >=119.5fps、切換 <=300ms、APNG 可操作 <=10秒；尚未全部達成。

已先完成根因分析；後續優化依上述證據排序，不在尚未定位的環節繼續猜測。

## 保存資料與重現

- `Saved/Tests/InteractionShowcase/PairProfileR72/report.txt`：無 trace 基準。
- `Saved/Tests/InteractionShowcase/PairProfileR73NativeRetry/`：完整 trace、run log、Present report 與四 DLL hashes。
- `Saved/Crowd/Profile-InteractionR73Quiet{0,1}-CPUEvents.csv`：全部執行緒／全部 timer 的 3 秒原始事件，包含非 CPU；分析需 thread inventory。
- 同 prefix 的 `-Threads.csv` 與 `-Root.json`：按 ID 過濾與完整幀根因分析。
- `Tools/analyze_interaction_root_cause.py`：檢查完整幀及每幀兩次 role sample，分開 GT 與 worker CPU。
- `Tools/summarize_crowd_cpu_events.py --thread-inventory ... --all-timers`：完整 exclusive event tree；未指定 inventory 時仍嚴格拒絕 GPU／Frames 輸入。
- R73 第一輪與漏 worker 的匯出亦保存，錯誤詳見 `INTERACTION_SHOWCASE_ERRORS.md`，不當成隔離 FPS 或完整 worker 證據。

## R74 後續實驗：只合併工作批次不足
相同 binary 正常 RHI／無 trace 的兩姿勢 A/B：融合 ON 10.243/10.239ms，OFF 10.051/10.147ms。沒有實際收益，預設關閉；CPU canonical 更新與直接 native draw 轉換仍重複，不能把 barrier 數減少當作消除頂點成本。完整預設功能 audit PASS，13項 regression PASS；120 FPS 未驗收通過。後續通用方向應以來源索引統一工作，讓所有 draw 共用同一次 canonical 轉換及法線旋轉，再精確打包、更新 CPU查詢與bounds，不依赖 per-card 預建資源。

R74 最終當前 binary：13 regression PASS；完整 APNG PairEyes/Speed/SkipPaint audit PASS，且 teardown log 無骨架 ensure。最終隔離正常 RHI 兩姿勢 9.569357/9.572967ms、Present104.501234/104.458581FPS、GPU1.365668/1.364470ms；external UE0、4DLL stable且與功能測試完全相同。120尚未通過，詳細採用／未採用與限制見 INTERACTION_SHOWCASE_ACCEPTANCE_R74.md。
# R75 接續驗證：來源索引排程沒有減少 baseline 必做工作

2026-10-07：R75新增canonical source job直接emit draw ranges，exact CPU/GPU與實際APNG parity功能PASS，但同binary1024 balanced ON9.934/9.878ms、OFF9.405/9.544ms，沒有性能收益。256 Unbalanced曾出現653.626ms最大幀，根因未證實；有界8參與者wait降到約0.048ms，全幀仍10.286/10.818ms。最終預設SourceIndexedStreams=0。

源码複核：baseline已每source單次轉換及真正重複法線旋轉cache，R75只比R74實驗消除重複轉換，不比實際baseline少draw generation／packing／CPU writes／bounds。禁止將barrier等待下降宣稱全幀優化。

最終同四DLL正常RHI isolated PairProfileR75Final：quiet全幀9.433605/9.479136ms、PairAdvance6.623726/6.654700ms、Present106.005521/105.498206、GPU1.450102/1.382820ms；wait0.095781/0.098475。120fps_present_verified=0，水槍未量。

下一個架構為frame-local兩階段GT prepare/publish，讓native private skin完成worker同時產生canonical與display packets，避免目前skin完成後的串行CPP生成打包階段。此方向**尚未實作**；須保留eye/morph特殊路徑、CPU查詢、exact math、shadow時序及角色替換生命週期，並重新獨立oracle及全幀驗證。完整細節及失败速度複測見 INTERACTION_SHOWCASE_ACCEPTANCE_R75.md。


R76已實作並驗證上述frame-local管線，native observer涵蓋shared及non-shared completion；原公式／GPU packing oracle通過。量測未改善，預設NativeDisplayStreams=0。326完整paired frames的trace：PairAdvance6.703358ms、Native5.715148ms、DynamicsWait3.516226ms；native callback CPU總和13.255316ms/frame，不是wall time。工作移至native critical path不能視為收益。另採用runtime immutable rest-normal cache與zero-offset identity rotation省略；同binary兩輪約2–3%差異，仍未達8ms／Present120FPS。完整證據見INTERACTION_SHOWCASE_ACCEPTANCE_R76.md。

R77 scoped AssignedWorldFrame exact oracle PASS4464 matrices＋streams；同程序同姿勢OFF→ON→OFF quiet15秒：11.582770／12.092381／11.608001ms，86.325850／82.701789／86.137518 PresentFPS。前後OFF接近，新cache未改善，預設0。不能只依重複計算下降採用，serial prepare與worker節省成本比例未量測。最終預設正常RHI PairProfileR77Final10.878257／10.745831ms、91.926537／93.052426FPS，Advance7.486265／7.393287ms，GPU約1.47ms；8ms／120未達。完整功能／速度／眼睛／13回歸PASS，水槍仍略過。證據INTERACTION_SHOWCASE_ACCEPTANCE_R77.md。
