# E300：求解後的私有骨架只輸出幾何

2026-10-05。承接平均13ms目標與E298 profile；延續E299剛性配件掛接修正。

## 根因与實作

shared角色先在ApplyAnimFrame／finish_paired_dynamics呼叫update_dynamic_accessories，以本幀dt同步骨架、更新碰撞與DynamicBone。随后Skin的顯示輸出原本再以dt0呼叫同一函式。dt0不積分粒子，但仍會建立collider frames、掛接祖先鏈、implant同步、world rebuild，屬重複前處理。

對照 `C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/DynamicBone.cs:139`：LateUpdate使用Time.deltaTime更新DynamicBone。顯示輸出應讀取此階段完成的骨架。

新增AccessoryRuntime::render_solved與OkSession::render_solved_accessories：從已解算的state.skel／placement_correction／AssignedWeights palette做skin與切線，不重做求解前處理，也不修改粒子。無DynamicBone的私有renderer保留原placement；coordinated衣服使用原clothes骨架。

資源重設後尚無state時仍走原本dt0初始化，保留明確CPU查詢語意；不把未初始化／不支援的spring rig悄悄換成不同palette。初始portable載入、換shape、Animator、外部幀、雙人求解仍保留原物理更新。沒有新增序列化欄位、不需重製APNG、不依特定卡片名稱分支、不降低動畫或物理頻率。

## 精確性

最終實作fixture逐項相等，40幀含不同dt與根節點平移／旋轉：

|最終日誌|卡片|私有renderer頂點比較數|結果|
|---|---|---:|---|
|Anim300SolvedC001Final.log|三月七|1,779,200|通過|
|Anim300SolvedC010Final.log|C010|2,019,760|通過|
|Anim300SolvedC018Final.log|不知火舞|1,057,440|通過|

比較位置、法線、切線、骨架worlds、粒子歷史；連續兩次幾何查詢不改動粒子；另驗證reset後重新初始化與舊流程相等。

Anim300PaletteParityR1：palette-only與CPU顯式幾何查詢相等（148個palette／44,480頂點）。Anim300RustRegressionR1：38項動畫測試通過、31項明確忽略，0失敗。這兩項在加入reset fallback前完成；最終三卡fixture已涵蓋fallback。

## 正式驗收

Anim300BuildR1 Succeeded；Anim300PerfR1整套valid1，9張圖valid。20秒walk＋3秒噴／15秒停：

|卡片|actor平均ms|整幀平均ms|停噴整幀ms|載入秒|
|---|---:|---:|---:|---:|
|C001|9.671592|13.025948|12.060627|5.932331|
|C010|9.870873|12.936263|12.896459|2.191230|
|C018|7.105541|10.186767|10.318615|1.414848|

三卡draw mapping誤差0、socket誤差<=0.000022907cm，換衣／存卡／離線讀、液體染衣與守恆全部通過。C001仍略超13ms，不宣布全卡達標。

Anim300ProfileR1（C0015秒）：profile開啟會額外寫日誌，frame15.628563ms；cpp骨架build仍有0.060ms，CPU沒有BodySkel/SkelFollowers卻每幀生成未使用的UE姿勢陣列。已改為有骨架消費者才建立；原生骨架與CPU蒙皮保持更新。BuildR2 Succeeded。最終R2驗收已完成：三卡／9張圖valid1。

|最終R2卡片|actor平均ms|乾燥整幀ms|停噴整幀ms|載入秒|
|---|---:|---:|---:|---:|
|C001|9.154316|12.122474|12.930211|4.169418|
|C010|9.518014|12.439131|14.794957|2.225294|
|C018|7.730745|10.561600|9.459238|1.486965|

純動畫三卡平均低於13ms；C010停噴14.794957ms仍超過13，不宣稱所有狀態達標。最大actor單幀仍有52.176／61.129／34.686ms，平均不是最大值保證。

Anim300RegressionR2.log逐項9項Success：CompactProceduralDraw、MutableStreams、SourceGeometrySnapshot、KksMouse、SharedDeformation、ConservativeTransport、Film、MaskParity、Scene。R1缺少參數quote提前退出，無效。

最終main SHA256 6599B3B2D4AC80F3452ABAB732BE1018801BE50AD5DEFB51BD8C1C8CDC2A208A；plugin SHA256 4252E2976E272CC954916635A86096F5CDF5E5D536F24F7FD6404285B04F3451。BuildR2同時編譯工作區既有OKCrowdIndependentGpu.cpp變更，未修改或撤回其內容；不同輪次時間差不全歸因本次0.060ms優化。

13ms以整體frame平均判定，actor平均不替代frame；不以單一卡較快結果冒稱全部達標。

## 錯誤

首個fixture浮點literal省略0，Rust編譯失敗；已修正，Anim300SolvedParityR1不列為驗收。調查resource reset發現其後CPU顯式查詢必須初始化state，加入冷路徑fallback，不能僅跳過所有dt0更新。
