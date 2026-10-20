# 水槍性能優化驗證（2026-10-05）

第一階段E282數據及0955版本保留如下；目前正式版本已更新至第二階段0957，見本頁末尾。

原實作對每個飛行packet重新掃描world，候選角色及沾漆覆蓋層又反覆呼叫GetCPUSkinnedVertices。UE5.8這個API會更新骨骼、切換CPU skinning並同步重建渲染狀態，並非單純讀頂點。

現在每次surface更新只發現一次候選，包含噴射散射範圍與仍在飛行的液滴；FOKPaintPose讀取現有骨骼矩陣、skin weights與morph deltas，逐幀覆蓋只計算沾漆所需頂點。動畫附著、UV厚度貼圖、重力流動、滴落、面數及預設畫質均保留。cloth、external morph或MeshDeformer仍使用原精確fallback，這些特殊資產不保證相同加速幅度。

完整OpenKoikatsuEditor Win64 Development建置PerfFullBuildR7成功。正式manifest使用UnrealEditor-OpenKoikatsu.dll，與測試0955的SHA256相同。修改前基準0956使用相同完整target物件，僅將Sprayer換成保存的原實作（增加統計輸出，不改行為），不替換正式DLL。

同一March_7th_keiyuu原卡、1600×900、原生Studio走路動畫、Flow12／JetSpeed1600／SurfaceHz20、相同固定鏡頭與瞄準、測試散射0.15°，暖機2秒後實測8秒：

| 指標 | 修改前 | 優化後 |
| --- | ---: | ---: |
| 水槍Actor CPU更新平均 | 11.642 ms | 5.828 ms |
| 水槍Actor CPU更新P95 | 14.254 ms | 8.724 ms |
| 全輪水槍最高單次成本（含初始化及暖機） | 497.145 ms | 73.884 ms |
| 引擎幀計數／wall clock平均 | 44.502 ms | 39.236 ms |
| 8秒採樣幀 | 180 | 204 |

平均水槍CPU成本下降約50%，最高單次成本下降約85%。幀時間包含動畫、遊戲、渲染等待等，不是GPU Present FPS，也不是使用者330ms場景的精確重現；其它Editor／背景工作仍可能影響整體時間。兩輪均4個表面18384面、持續命中且總量守恆、report.valid1、正常exit0。依實際動畫／幀步長，命中51／60次，故不是完全相同污漬分布的逐像素比較。

證據：Saved/Tests/PaintSpray396/PerfBaselineAnimatedR2與PerfOptimizedAnimatedR2。較早R1的暖機計時包含動畫載入，R2改為SetPoseMode完成後才計時，上表以R2為準；靜態初測因截圖shader編譯令退出停滯，不能宣稱正常完成。

PerfFunctionalR1原GUI載卡／沾漆／原動畫／返回清除／重開／再次載卡valid1、exit0，94次命中37.6cm³；Painted、Animated、Cleared畫面已檢視。PerfRegressionR3三項Camera.KksMouse／Paint.Film／Paint.Scene成功，包含多人物與多材質分區、hide/show、不同幀率守恆、逐幀附著、正負Morph與非均勻縮放對引擎CPU oracle精度。測試不代表所有卡片或所有cloth/deformer均已驗證。

Tools/run_paint_performance_audit.ps1可對保存的DLL在隔離host重跑，Animate開啟原生走路，Regression執行測試，Functional執行實際讀卡流程。性能audit不截圖，避免截圖引發BufferVisualization shader同步編譯污染量測；初始化／索引／動畫載入時間不混入8秒Actor平均。測試前複製既有mod索引，子程序環境明確傳入且finally還原。使用者直接重啟CardReaderDevelopment.cmd即可載入正式新版本。

## 第二階段E283：拓撲建立及稀疏覆蓋

BuildAdjacency改用int64量化座標的無向邊鍵，省去逐邊字串格式化／串接與owner陣列分配；計算邊的owner總數，仍只連接恰好兩個owner。DrawFilm只遍歷Film.Active，而非全部三角形。保持原量化容差、接縫關係、非流形規則、每幀姿勢、UV chart與畫質，無卡片名稱特例。

20,001面fixture包含负大座標、重複頂點接縫與非流形邊，全部鄰接對原字串實作oracle一致。拓撲建立71.605→3.927ms，此數字只代表fixture該步驟。

相同March原卡／原生Studio走路／2秒暖機＋8秒量測的同輪比較：

| 指標 | 上一版0955 | 本版0957 |
| --- | ---: | ---: |
| 水槍Actor CPU平均 | 8.558 ms | 6.644 ms |
| 水槍Actor CPU P95 | 12.449 ms | 10.860 ms |
| 全輪Actor最高成本（含初始化／暖機） | 78.275 ms | 26.311 ms |
| 引擎幀計數／wall clock平均 | 41.078 ms | 39.458 ms |

平均水槍成本降低約22%，最高成本降低約66%。兩輪4表面18384面、命中64／56次、守恆、valid1、正常exit0；證據PerfPreviousAnimatedR3／PerfOptimizedAnimatedR3。背景負载與實際污漬分布會變動，不將不同輪次數值拼接成加速倍率，不宣稱330ms整幀情境精確重現或保證GPU FPS。

完整PerfFullBuildR9成功；PerfRegressionR4三項成功，包含新鄰接oracle、既有跨UV流動／不同幀率守恆／逐幀附著／Morph精度／多分區隱藏與清除。PerfFunctionalR2實際GUI載卡／95hits、38cm³／動畫／返回清除／重開／重載valid1、exit0，三張畫面已檢視。正常DLL與實測0957 hash相同，PerformanceFinal0957保存DLL／manifest／來源hash；重啟CardReaderDevelopment.cmd即可使用。
