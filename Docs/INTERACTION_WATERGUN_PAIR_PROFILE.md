# 雙人動畫水槍效能 profile 計畫（2026-10-07）

1. APNG男女高模、private GPU、1600×1000可見視窗、原材質與物理，保留雙人動畫。停止背景動作準備，避免載入污染穩態量測。
2. 同一姿勢分七階段：女角視角乾淨未噴射、女角持續噴射、女角停射後塗層、男角視角乾淨未噴射、男角持續噴射、男角停射後塗層、清空後播放。每階段暖機3秒、量測15秒。射擊前清除前一角色殘留，必須有新增命中與沉積。
3. 射擊位置從runtime幾何選取，只在量測外執行選取及精確query；量測中以最近骨骼追蹤目標，不以每幀全mesh檢查污染水槍成本。各角色的未噴射比較需使用相同相機。
4. 先以無trace的可見Present取得幀時間、P95/P99、最大幀、實際FPS、GT/Render/RHI/GPU，以及水槍geometry/simulation/coating分項平均。
5. 再以獨立trace診斷主要熱點，保留原始utrace、區間及timer資料；trace耗損不能代替無traceFPS。量測不修改產品演算法、不宣稱達到120fps。
6. 結果寫回本MD，錯誤記入INTERACTION_SHOWCASE_ERRORS.md；保留模組hash及外部UE干擾紀錄。


## 初輪診斷（正式複測進行中）

PairWatergunVisibleProfile前6區間有實際命中，未噴射約121.69／117.78FPS，女角噴射89.23FPS，男角噴射6.93FPS。最後清空區間意外恢復實體输入，不能列入乾淨基準；修正及原始report保留於錯誤MD。PairWatergunTraceProfile同版diagnostic的清空區間實際hits0，但新版仍加上輸入鎖定與硬檢查，避免間歇性污染。

獨立trace男角噴射中段154.430455..159.430455秒（5秒），匯出全部timer，依thread inventory排除GPU／Frame Tracks，計算exclusive CPU事件：

- GameThread `OK_PaintTick` inclusive4635.428ms／35次；`OK_PaintStep` inclusive3808.508ms／605次。
- GameThread `ParallelFor.Wait` exclusive3138.354ms；它是等待時間，不能當作CPU計算工作。
- 非GT `PaintSurfaceMasks` exclusive7007.428ms（各執行緒工作總和，不是幀時間）；GT同scope另238.820ms。
- GameThread `KK_PrivateGpuCpuQuery` inclusive154.481ms；`OK_PaintCoating` inclusive518.090ms。

根因已有profile證據：水槍表面遮罩傳輸造成主要CPU工作及同步等待，塗層繪製與geometry query為其次；不能把目前瓶頸歸因為Unreal GPU雙人骨骼播放。液滴Niagara計算較小，不代表其所有render成本為零。全程trace只開cpu/frame/bookmark，GPU queue事件檔為空，GPU時間另由原始frame report讀取。

證據：Saved/Tests/InteractionShowcase/PairWatergunTraceProfile/runtime.utrace；Saved/Crowd/Profile-PairWatergunMaleDiagnostic-CPUEvents.csv、CPUExclusive.csv、Threads.csv與Anchor.csv。使用既有export工具，路徑位於Saved/Crowd但内容是本次雙人水槍，並非Crowd場景。不同inclusive scope互相包含，禁止相加當成整幀成本。

## 最終profile完成（同binary兩轮無trace＋獨立trace）

硬體：Ryzen 9 9950X3D／RTX 5090，1600×1000可見視窗，APNG高模、private GPU、原物理／材質。載入與背景準備排除，各段3秒暖機、15秒量測，男女視角分別有自己的乾淨基準。以上初輪diagnostic保留，以下是最終binary結果。

| 區間 | R1平均ms／FPS | R2平均ms／FPS | R1最大幀ms | R2最大幀ms |
|---|---:|---:|---:|---:|
| 女角視角乾淨播放 | 8.726／114.59 | 8.505／117.57 | 42.328 | 31.955 |
| 女角持續噴射 | 13.822／72.33 | 11.243／88.91 | 31.685 | 25.020 |
| 女角停射後 | 11.966／83.52 | 11.660／85.77 | 25.829 | 102.850 |
| 男角視角乾淨播放 | 9.272／107.86 | 8.218／121.69 | 16.924 | 14.590 |
| 男角持續噴射 | 331.374／2.99 | 127.953／7.84 | 521.121 | 232.580 |
| 男角停射後 | 189.153／5.26 | 56.016／17.86 | 297.762 | 111.264 |
| 全部清空後 | 16.872／59.26 | 8.377／119.37 | 53.296 | 16.314 |

R1男角出現521.121ms慢幀與2.380196秒fixed-step backlog；R2男角最大232.580ms，並非穩定低於300ms。兩輪都未達水槍120fps，不能挑較快R2掩蓋R1。R1清空後59.26fps未在同版trace及R2重現（124.25／119.37fps）；三次clear時皆0surfaces／0hits，watergun actor成本為0。清空後低FPS的單次原因未證實，不歸因殘留水槍資料，也不宣稱本輪乾淨基準達8ms。

| R2水槍分項，ms／整幀樣本 | 女角噴射 | 男角噴射 | 男角停射 |
|---|---:|---:|---:|
| actor | 2.443 | 117.616 | 45.757 |
| geometry | 1.618 | 8.822 | 7.994 |
| simulation | 0.434 | 94.872 | 19.648 |
| coating | 0.281 | 13.755 | 18.112 |
| ballistic | 0.125 | 3.111 | 1.214 |
| mask | 0.231 | 84.147 | 14.232 |
| film | 0.000 | 0.002 | 0.001 |
| drip | 0.074 | 7.515 | 4.156 |

分項為wall時間，包含parallel等待；simulation包含ballistic/mask/film/drip，不能相加重複計算。各階段timer邊界sprayer與scene樣本約差1幀，低FPS時應保留這個誤差。Statistics裡actor_mean/max是累積歷史，正式分項採區間差分；clear即使顯示歷史max也不代表當段仍在更新。

R2女角602濕charts／154,112個chart儲存格，男角4,680／5,143,296，相差33.4倍；男角停射後5,839／6,375,424繼續增加。這是每濕face整張grid的格數，包含空格，不能解讀成全數非零液體格。男角faces86,051反而少於女角114,301；性別／總三角形數不足以解釋差距。

## 同版trace根因核對

男角噴射中段184.951317..189.951317秒（5秒）：

- GameThread OK_PaintTick inclusive4600.885ms，36次；OK_PaintStep inclusive3721.116ms，595次。
- GameThread ParallelFor.Wait exclusive3127.870ms，主要是在等液體並行工作。
- 非GT OK_PaintMaskTransport exclusive7591.911ms，GT同scope另131.742ms。這是各CPU執行緒工作總和，不能當整幀ms。
- GT private GPU query inclusive162.075ms；coating inclusive554.937ms；KK_PairAdvance inclusive239.468ms／35次。

清空後221.298275..226.298275秒（5秒）：mask／Step／query／coating／ParallelFor.Wait均0；sprayer Tick inclusive0.594ms／561次。PairAdvance仍正常執行3371.128ms／626次，平均5.385ms。證明水槍負載可隨清空移除；目前主要增量不是雙人動畫GPU運算。

代码核對：MakeSurface用每mesh目標2048和triangle count算chart大小，再clamp4..64，mesh拆分程度會造成總格數失衡。Transport反覆掃描整個wet-face grid，跨face route又在pose revision時失效；PredictionCount超262,144時目前停用route預測並行。這些設計可以放大卡片間差異；沒有直接量測證據的個別內圈比例不臆估。

## 證據與完整性

- 最終build：Saved/InteractionPairWatergunProfileFinalBuild.log，Succeeded70.10秒。只新增profile計時／workload統計及audit，不改液體算法。
- [R1無trace report](../Saved/Tests/InteractionShowcase/PairWatergunVisibleFinalProfile/report.txt)、[R2無trace report](../Saved/Tests/InteractionShowcase/PairWatergunVisibleFinalProfileR2/report.txt)。
- [同版trace report](../Saved/Tests/InteractionShowcase/PairWatergunTraceFinalProfile/report.txt)、runtime.utrace、ExportR2-Male.csv／Clean.csv、MaleCPUExclusive.csv／CleanCPUExclusive.csv及[重點CPU摘要](../Saved/Tests/InteractionShowcase/PairWatergunTraceFinalProfile/CPUFocusedSummary.json)。
- 全部3個最終run exit0、hash穩定、externalUE0；四DLL首尾逐一匹配目前launcher，[核對receipt](../Saved/InteractionWatergunProfileCurrentVerification.json)。
- R2載入11.441秒超入口预算，按既定延期不混入播放profile。Anchor匯出02:43:44退出，早於R2phase0；最終事件匯出在R2退出後執行，未污染量測。
- 相關source diff --check通過。沒有另行執行無關回歸；7個實際profile區間驗證兩角色新命中／沉積及3個乾淨階段0hits／0surfaces。
- [兩個通用優化方案](INTERACTION_WATERGUN_OPTIMIZATION_OPTIONS.md)：稀疏傳輸及依物理面積分配共用atlas；尚未實作兩個優化。

重跑：Tools/run_interaction_showcase.ps1搭配新Tag、-Apng、-Card女角APNG、-PartnerCard男角APNG、-PairWatergunProfile、-Visible、-IgnoreEntryBudget、-WindowWidth1600、-WindowHeight1000；trace另加-Trace，兩輪分開，不能同時用SkipPaint／PairPaint／AssignedWorldAB。

## 後續方案二實作
使用者選擇依物理面積共用atlas及1024預設，已完成runtime實作與功能回歸；本文件前述數字仍是前版原始證據。新版結果見 [INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md](INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md)，不能把旧版120fps安靜播放移作新版噴射驗收。
