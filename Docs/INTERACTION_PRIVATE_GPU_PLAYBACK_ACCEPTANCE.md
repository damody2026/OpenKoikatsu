# APNG 雙人高模 GPU 播放驗收（2026-10-07）

使用者當前優先順序：先完成執行效能 >120fps；入口超時延期；水槍不量測。所有角色資料從 APNG 和共用資源 runtime 取得，不新增角色專用預烘焙資源。

## 根因與正式實作

CPU 基線兩段安靜播放為 10.131214／10.141897ms、98.699849／98.602828 Present fps。主要成本是私有衣髮大量頂點的 native LBS、轉換、draw packing 和上傳。實際配對 native root 在 dynamics 內部已生成 CPU 頂點，僅排除後面的 display skin 不足以移除成本。

- Runtime 捕獲一次有 revision 的 owned 原始 source、浮點 signed weights、draw mapping；每幀只處理 palette/post 與保守 bounds。
- 已安裝 GPU part 在原 solver 的 render consumption 階段排除 CPU 頂點生成。骨骼、衣髮物理、共享時間與求解頻率保持原算法。
- 同一 actor 各 sections 合併一個 RDG graph。保留原高模、材質、眼睛、描邊、UV、顏色、索引和陰影。
- GPU 發布經 proxy epoch、resident generation 查詢、全部 section install receipts；pending 保留發布所有權，失敗恢復 CPU。不能假設 generation 永遠為零。
- 頭眼／嘴型等特殊變形仍用原 renderer。不支援的私有來源保留 CPU。
- 水槍與截圖／取景需要幾何時，從已求解姿勢按需生成當前 CPU 幾何，不再推進物理。
- 私有 GPU 預設只在互動場景 opt-in；進入時含既有男角，新載入男角也啟用，離開恢復原發布者。可用 `ok.Animation.PrivateGpuStreams 0` 對照。

## 已通過的功能與正確性

`PairPrivateGpuDefaultEyesSpeedFunctional`：functional_pass=1、audit_completed=1、exit=0；四個 DLL hashes 前後一致、外部 UE=0。

- 54 嘴型、語音播放、walking 跨兩週期繼續移動、男女共享時鐘、暫停／重播、頭眼追蹤／嘴型／肚子控制、男性替換與失敗保留原角色通過。
- 12 次 ready 姿勢切換：最慢 click-to-play 16.408ms，切換期間最大單幀 29.196ms，均 <300ms；首次 ready commit 73.483ms。
- 13 個速度窗口／69 次調速：0.25、0.5、1、2、4 倍、連續變速和暫停，所有相鄰 baseline 的 CPU／frame 成本門檻通過；callback 最大 0.030302ms。個別 baseline 為 8.007296／8.142011ms，不能宣稱每個速度窗口都 <=8ms。
- 已人工檢視 PairEyesNeutral、Tracking、Closed、Reopened、Playing 五張 PNG：眼睛顯示正常，閉眼與重開正常。截圖屬功能驗收，不拿當效能證據。
- 水槍 SkipPaint；保留按需幾何接口與原功能，不宣稱本輪水槍驗收。
- 整段功能測試混合載入、替換與截圖，aggregate max 1130.939ms；不能拿其平均或最高值冒充安靜播放。入口6141.801ms只記錄，entry_budget_enforced=0。

Native 男／女實際 APNG 4 個 filtered solver/query oracle 通過：128244／210168 vertices、粒子和原 solver 完全一致、按需幾何精確等價、stale revision 原子拒絕。

GPU readback 210 publications／456612 draw vertices，最大位置差 0.0000458012cm，normal 0.00000694219、tangent 0.000000273143；原始 material raster 與 resident/CPU takeover/epoch/batch 拒絕測試通過。PrivateGpuNativePairFullRegression 26/26 通過。

## 安靜可見播放初次達標

`PairPrivateGpuNativeSolverVisibleProfile`（opt-in），1280×720，兩段各15秒；無外部 UE、四 DLL hashes 穩定、額外水槍命中0。

| 窗口 | 樣本 | 全幀平均 ms | 最大 ms | 實際 Present fps |
|---|---:|---:|---:|---:|
| 0 | 1901 | 7.889456 | 9.481102 | 126.755135 |
| 1 | 1913 | 7.889408 | 9.289000 | 126.749109 |

平均 <=8ms、Present >120，120fps_playback_present_verified=1。13.7%／11.9% 單幀超過8.33ms，所以這是平均播放達標，不能宣稱每幀都達120Hz。診斷 phase2 刻意開啟逐部件 profiling，9.078429ms／110.158350fps，另記而不納入安靜播放門檻。

## 最後配置補強與保留的未達標結果

預設 GPU 重測 `PairPrivateGpuFinalDefaultVisibleProfile`：7.983697／8.412702ms，125.255785／118.857888fps，phase1 未達標。不能用初次快輪取代這輪。一般 launcher 舊 CPU 模式1600×1000 `PairPrivateGpuReaderCpuVisibleProfile`：23.099350／19.400406ms、43.286267／51.556061fps，phase1 最大945.561901ms，未達標。

進一步 profile 發現全域 CLIP_CACHE 鎖內執行完整曲線採樣，男女與背景準備互相串行。改成 immutable Arc<CachedClip>，鎖內只拿owned snapshot，在鎖外採樣；冷載入與替換仍發布完整驗證內容，序列化格式不變。Native 動畫41項PASS、34項原忽略；新並行採樣／替換snapshot oracle通過。最後build成功42.11秒，ImmutableClipGpuFinalRegression 26/26 PASS、四DLL穩定、外部UE=0。

CardReaderDevelopment.cmd 現在預設GPU蒙皮，保留1600×1000；PowerShell `Tools/run_card_reader_development.ps1 -GpuSkin:$false` 仍可對照CPU。正常啟動與CPU對照的DryRun收據保存於 Saved/InteractionReaderGpuLaunchDryRun.json、InteractionReaderCpuLaunchDryRun.json。

最終正常1600×1000 GPU路徑 `PairImmutableClipReaderGpuVisibleProfile`：無 console override、Visible=1、兩段各15秒、四DLL穩定／外部UE=0／額外水槍命中0／exit0。

| 窗口 | 樣本 | 全幀平均 ms | 最大 ms | 實際 Present fps |
|---|---:|---:|---:|---:|
| 0 | 1921 | 7.855590 | 16.373001 | 127.298179 |
| 1 | 1911 | 7.897920 | 16.715400 | 126.615566 |

120fps_playback_present_verified=1。11.2%／11.5% 單幀超8.33ms，仍是平均播放達標；不是每一幀120Hz。完整solver更新平均5.288538／5.326085ms，Render5.389302／5.428546ms，GPU2.206855／2.218218ms。診斷phase2開啟逐部件profile為8.769052ms／114.037747fps，不納入安靜播放，也不隱藏。入口9876.890ms只記錄，未優化；首次ready commit261.297ms。

## 最終 binary 功能驗收

`PairImmutableClipReaderGpuFinalFunctional`，1600×1000，functional_pass=1、audit_completed=1、error空、exit0，外部UE=0、四DLL hashes前後穩定，與最終Visible profile使用同一組DLL。

- 12 ready switches最慢21.950ms、切換期間最大35.013ms，首次ready commit106.887ms。同步控制最大153.725ms。
- 13速度窗口、69調速請求，CPU/frame相鄰baseline門檻全部PASS；callback最大0.031199ms，實際0.25／0.5／2／4倍與暫停時計正確。窗口平均7.428629～8.085473ms；1倍與4倍有些窗口略超8ms，不能宣稱所有短窗口<=8ms。
- 54嘴型、語音active、walking跨兩週期、語音catalog取消／替換、眼睛／頭部／嘴型／肚子控制、共享時鐘、暫停／重播、男性替換及失敗保留通過。返回後stale男性不發布、重新進入場景通過。
- 原stable shadow depth oracle通過；已人工檢視最終1600×1000五張PairEyes PNG，全部正常。
- 水槍validation_skipped=1。入口5937.545ms／entry_budget_enforced=0；全流程包含載入與截圖，aggregate max1244.310ms，沒有宣稱載入／背景操作全部<300ms。

## 驗收後複測與結論

同binary Visible重測 `PairImmutableClipReaderGpuVisibleProfileR2`，1600×1000，兩段各15秒，audit_completed=1／pair_profile_completed=1／120fps_playback_present_verified=1／exit0。

| 窗口 | 樣本 | 全幀平均 ms | 最大 ms | 實際 Present fps |
|---|---:|---:|---:|---:|
| 0 | 1906 | 7.870062 | 15.340600 | 127.066060 |
| 1 | 1898 | 7.950064 | 16.667798 | 125.787354 |

12.6%／18.0%幀超8.33ms。診斷phase2為8.871225ms／112.719157fps。外部UE=0、額外paint=0、四DLL hashes一致且穩定；入口5952.946ms只記錄、不列入優化工作。初次ready commit197.209ms。

本輪範圍完成：實際APNG男女高模安靜播放平均<=8ms、兩輪共四窗口實際Present>120、現有功能與ready切換<300ms通過。**不代表每一幀120Hz、所有短速度窗口<=8ms、載入／背景操作全部<300ms、或未測角色與其他硬體一樣快。**入口超時延期，水槍不量測。

機器為AMD Ryzen 9 9950X3D／RTX5090／Windows11。正常根目錄CardReaderDevelopment.cmd使用已編譯Editor模組，預設GPU／1600×1000；重新啟動可使用。CPU対照仍可明確指定。

完整證據目錄：

- `Saved/Tests/InteractionShowcase/ImmutableClipGpuFinalRegression`：26回歸及模組雜湊。
- `Saved/Tests/InteractionShowcase/PairImmutableClipReaderGpuFinalFunctional`：功能report、原始log、五張眼睛圖、模組雜湊。
- `Saved/Tests/InteractionShowcase/PairImmutableClipReaderGpuVisibleProfile` 和 `PairImmutableClipReaderGpuVisibleProfileR2`：各自完整安靜／診斷report、Present、外部程序觀測、前後模組雜湊。
- `Saved/InteractionImmutableClipNativeTests.log`：41 native動畫測試；`Saved/InteractionImmutableClipGpuBuild.log`：42.11秒Succeeded。

失敗與修正全部保留於 [INTERACTION_SHOWCASE_ERRORS.md](INTERACTION_SHOWCASE_ERRORS.md)，包含逐 section graph 比 CPU 更慢、source 未 ready 的錯誤快取、native root 排除位置錯誤、generation 重新安裝及驗收命令參數错误。
最終整合核對：26回歸、完整功能、兩輪Visible共四次audit使用同一組四DLL，與目前正常launcher模組SHA256逐一一致；run-evidence均exit0／hash穩定／externalUE0；相關source diff --check通過。

## 後續續作核對（2026-10-07）

計畫：先確認目前四個啟動模組與已驗收版本相同，再核對26項回歸、完整功能與兩輪可見Present報告；有缺口才補實作並重跑相關驗收。入口超時及水槍量測仍延期。

執行結果：目前四DLL SHA256均與最終驗收版本一致，相關source diff --check通過。26項Success紀錄完整；54嘴型、語音、walking loop、雙人切換、調速穩定、男女替換及頭眼嘴腹控制的功能旗標全部通過。兩輪Visible報告均120fps_playback_present_verified=1、exit0、hash穩定、externalUE0。

沒有發現本輪範圍內的新實作缺口或新錯誤，保留已驗證通用實作；未重複編譯或量測相同binary。舊失敗仍保留於錯誤文件，未覆蓋。本輪核對完成。

## 2026-10-07 水槍功能驗收完成
使用者現已要求驗證水槍，解除此前水槍功能檢查延期。APNG高模private GPU實際雙人動畫中，男女分別清空後射擊、private表面沉積、當前pose query、動畫附著、姿勢切換及清除皆PASS；完整功能PASS、最終8項回歸PASS，先前較廣31項亦PASS。詳細見 [INTERACTION_WATERGUN_GPU_ACCEPTANCE.md](INTERACTION_WATERGUN_GPU_ACCEPTANCE.md)。本輪重編譯增加audit，最終功能與回歸四DLL核對一致；上文舊binary／120fps數字保留為歷史證據，未重測噴射120fps。入口优化仍延期。
