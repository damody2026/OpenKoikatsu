# 雙人水槍方案二／1024 共用 atlas 驗收

## 實作範圍

依 [實作計畫](INTERACTION_WATERGUN_ATLAS_1024_PLAN.md) 完成方案二，預設 `AtlasResolution=1024`。每個場景的 sprayer 共用 atlas page pool，男女與所有部件使用同一套配置算法；無性別、卡片、mesh 名稱或索引例外。APNG 讀檔後 runtime 建立，不需要預烘焙角色資源。

每 face 在 surface 初次建立時以當時捕獲的世界三角形面積決定 chart 邊長：`pow2(ceil(sqrt(2*area_cm2)*resolution/256+2))`，夾至 4..64。256cm 是一致的密度參考跨度，+2 是邊界空間；1024 對應約 4 samples/cm，再向上取 pow2。chart 尺寸固定到 surface 移除／全場清除，動畫和 UI 未清除前的解析度變更不會重新分配。這是遮罩取樣精度的降低，原高模、材質、GPU 動畫與碰撞幾何保持原狀。

實際有液體的 face 才 lazy 配置 buddy chart／1024×1024 PF_FloatRGBA page。完整 CPU page mirror 保留其他角色資料，dirty 區域以最多 4×4 block 的有界矩形快照交給 render thread 上傳。按 page 建立材質與 overlay section，保留 vertex slack pooling；shader 只讀 atlas，沒有 framebuffer feedback。

不同解析度 face 間的 conservative transport 使用各自的格面積交換體積，維持原 double 質量 ledger。120Hz 彈道與 20Hz 表面模擬不變，卡頓時間保留為 backlog，沒有跳步、刪除液體或減少男角部件。清除單一角色只清除其 chart，其他角色 GPU 格資料保持；移除部件回收 buddy 配置，全場清除重置 page／mirror。單角色清除保留 chart 供後續再噴射，高水位 page 在全場清除時釋放。

## 功能與回歸

- 最終建置 `Saved/InteractionSharedAtlas1024BuildR5.log`：Succeeded，17.05秒。
- `SharedAtlas1024RegressionR4`：ChartCoordinates、ConservativeTransport、Film、MaskParity、PhysicalAtlas、Scene 共 6/6 PASS。包含 4→32 與 32→4 接縫傳輸 150 步守恆、brush／drip、700 個混合 chart 無重疊／回收合併、GPU 單角色清除範圍與其他格保留、overlay pooling。該混合配置未特別強迫多 page，真實量測亦需依實際 page 數報告，不能稱多 page 渲染已經實景驗證。
- `SharedAtlas1024PairFunctional`：真實 APNG 女角 C001、男角 InteractionMaleAPNGR65 的高模雙人動畫功能 PASS。女角 92 次新命中／36.8cm³，男角 213 次新命中／33.6cm³；private GPU 當前 pose query error 均為 0，動畫附著位移分別 4.440454／0.274038cm。姿勢切換、清除、嘴型／語音、眼睛／頭／腹控制、行走 loop、角色替換与失敗保留、返回再進皆通過。
- 12 次 ready 姿勢切換 budget PASS，最大同步控制 181.904ms。功能 audit 入口 7.553606秒；入口不是本輪優化目標，也不以單次結果保證任意 APNG 都低於10秒。
- 已人工查看 `Painted.png`、`PairPaint0.png`、`PairPaint1.png`：原角色外觀與眼睛正常，男女頭髮上均有實際塗層。1024／低 chart 解析度在近距離水痕輪廓可見较粗三角格邊緣，屬本次降低取樣精度的代價。

原始證據均在 `Saved/Tests/InteractionShowcase/<Tag>/`。上述功能與回歸偵測到其他 Unreal 同時執行，功能斷言仍 PASS、DLL hash 穩定，但其時間不作獨立效能證據。

## 效能證據規則

`SharedAtlas1024PairVisibleProfile` 完成七段 profile，但 external Unreal 重疊，`performance_evidence_isolated=0`；保留原始 report，不能用它作正式 FPS 改善比較。runner 另補上 packaged `OpenKoikatsu.exe` 外部程序辨識，避免只看 UnrealEditor 漏掉其他同專案遊戲。獨立複測使用新 Tag `SharedAtlas1024PairVisibleProfileR2`，不覆蓋舊資料。

前版對照 `PairWatergunVisibleFinalProfileR2` 為 1600×1000、同 APNG 卡片與七段女／男噴射流程，男角噴射 127.952941ms／7.842908FPS、mask 84.147293ms/frame、5,143,296 格；女角 11.242556ms／88.911726FPS、154,112 格。格數包含整張 wet-face chart 的空格，不代表每格都有液體。不同 run 的命中／濕面積會變動，因此數值是相同量測流程的前後結果，不是逐事件確定性重播。

本輪未實作方案一稀疏格點，也沒有保留旧 allocator 的 runtime A/B 開關；前後比較使用保留的舊 binary 原始結果。不能用前版安靜播放 120FPS 的證據替代新版雙人噴射驗收。

## 最終獨立無 trace 結果

`SharedAtlas1024PairVisibleProfileR2` 已完成，exit0、`watergun_profile_completed=1`、`external_unreal_observed=0`、`performance_evidence_isolated=1`。視窗1600×1000，完整高模雙人動畫；每段3秒暖機及15秒採樣。女／男噴射段新命中749／1171次，新增噴射體積180／111.6cm³；清空基準0／3／6均0命中、0新增體積、0 surfaces／pages。停射段保留既有彈道與滴落命中，新增發射體積0，未停止液體模擬。

| 階段 | 平均ms | Present FPS | P95／P99約ms | 最大ms | 採樣幀 |
|---|---:|---:|---:|---:|---:|
| 女角乾淨基準0 | 7.926139 | 126.165660 | 8.9／9.9 | 14.667299 | 1904 |
| 女角噴射1 | 10.531984 | 94.913475 | 18.4／19.4 | 20.112202 | 1423 |
| 女角停射流動2 | 11.014620 | 90.732058 | 19.3／19.8 | 20.480700 | 1360 |
| 男角乾淨基準3 | 7.845518 | 127.458886 | 8.5／9.1 | 26.854198 | 1911 |
| 男角噴射4 | 13.275560 | 75.290142 | 24.7／25.9 | 34.252699 | 1136 |
| 男角停射流動5 | 14.011013 | 71.395261 | 26.1／27.0 | 32.259699 | 1070 |
| 清空後6 | 8.073256 | 123.862246 | 9.1／10.0 | 28.214000 | 1857 |

| 指標 | 前版女角噴射 | 1024女角噴射 | 前版男角噴射 | 1024男角噴射 |
|---|---:|---:|---:|---:|
| 平均全幀ms | 11.242556 | 10.531984 | 127.952941 | 13.275560 |
| Present FPS | 88.911726 | 94.913475 | 7.842908 | 75.290142 |
| 水槍actor ms/frame | 2.442569 | 2.201731 | 117.616099 | 4.541097 |
| mask ms/frame | 0.230909 | 0.167757 | 84.147293 | 0.317133 |
| geometry ms/frame | 1.618259 | 1.476234 | 8.822413 | 2.327687 |
| coating ms/frame | 0.281372 | 0.278574 | 13.755013 | 1.400890 |
| 段末wet chart儲存格 | 154112 | 94640 | 5143296 | 183760 |
| 段末wet charts | 602 | 626 | 4680 | 3910 |

男角儲存格減少96.43%，男女格數比例由33.4倍降至1.94倍。男角mask耗時由84.147降至0.317ms/frame，已消除前版遮罩配置失衡的大部分成本；男角 Present 約提升9.6倍。這些是上述同流程量測結果，濕面積／命中並非逐事件相同，不將倍率保證為所有APNG的固定收益。

新版男角停射的4974個wet charts／233136格也遠低於前版5839／6375424格；當段mask0.416765、geometry2.446161、coating2.114511、drip0.254065ms/frame。四個有液體階段各使用1張共用1024 page，backlog段末1.6..4.3ms，無丟棄時間。

**方案二與1024功能驗收完成；完整雙人噴射120FPS仍未達標。** 七個測量窗口最大幀34.253ms小於300ms，不等於背景載入與整個場景所有幀都通過300ms。功能 audit 曾有背景工作／截圖造成1148.840ms最大幀，亦保留於原始資料，不以量測窗口掩蓋。

剩餘水槍成本已轉為動態geometry query與coating更新，男角涉及11個skinned部件、女角3個，不能再把男性高耗時全歸因遮罩。單純再降低mask或做方案一不足以由此證據保證120FPS；後續優化需針對現在pose幾何及overlay更新另行量測。

六項回歸、完整功能與獨立profile的四個DLL hashes均與目前檔案逐項相符，receipt：`Saved/InteractionSharedAtlas1024CurrentVerification.json`。錯誤／修復保留於 [INTERACTION_SHOWCASE_ERRORS.md](INTERACTION_SHOWCASE_ERRORS.md)。
