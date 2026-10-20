# E302：讀卡高模載入完成後平均 16ms

## 最新三卡重測：2026-10-06 16:40–16:44（Asia/Taipei），Reader302RepeatBudgetR48

依使用者「再驗一次三卡平均」要求，正式根專案、CPU高模、1600x1000、關閉profile、不改程式或降低模型。C010/C001/C018各outfit0，每卡20秒動畫與264真實slider events、3秒噴射、15秒停止後流動、half/off/restore各5秒，off換另一個存在的Studio clip。functional valid=1，raw mean gate **21/21 PASS**，9張圖像存在/尺寸/非灰屏檢查通過。

單位ms，身體更新是shape工作平均；其餘是包含首幀的完整window整幀平均，動畫window同時包含身體參數調整。沒有裁切早期幀或以Actor Tick替代整幀。

| 卡 | 身體更新 | 動畫編輯整幀 | 噴射整幀 | 停止後整幀 | 半脫整幀 | 全脫整幀 | 穿回整幀 |
|---|---:|---:|---:|---:|---:|---:|---:|
| C010 | 6.290429 | 7.236645 | 7.830270 | 7.457016 | 7.717680 | 8.550454 | 7.915394 |
| C001 | 5.213485 | 6.127602 | 6.187128 | 7.665871 | 8.635110 | 7.611694 | 6.407671 |
| C018 | 4.518150 | 5.001098 | 6.254089 | 5.815703 | 5.572102 | 6.551941 | 6.221349 |

整幀各stage平均5.001098–8.635110ms；身體工作平均4.518150–6.290429ms。APNG資源載入C010=4.882673秒、C001=3.718233秒、C018=2.728255秒。平均門檻通過，不代表每幀都<=16ms。792次實際slider events、stream/morph/socket/clock、衣服離線存卡重讀、paint質量守恆與粒子上限等functional oracle通過。

啟動前與中途快照未偵測到其他Unreal或ShaderCompileWorker；中途只有本次Reader PID90188。這不是所有OS背景程序的完整性能隔離保證。啟動前空列表用[]明確儲存於environment.json/cpu_before.json。實際引擎5.8.3，BuildId=1323cea4-7408-4662-8321-6abdaf191604.

**版本範圍：** 測試啟動DLL SHA256=7BB4E9998411EE92A442E58C22160B9B1A87EABB2296D7F06881249818A99BEC。測試08:44:43 UTC（16:44:43台北）已退出；其後16:45:09正式DLL更新成216C90F2A386A0CF104E0BD56E6D267CF9B7067BBC15E7FBBB2A05DC5C87A0AD。本次測量仍對應launch.json啟動版本，不能冒稱後來更新的DLL也已通過。version_check.json與environment_end.json保留差異。沒有重建、覆蓋或停止外部工作，也未追逐其他工作更新而無限重跑。此次只重測三卡，不重宣稱舊版13項unit tests驗證了新DLL。

證據：Saved/Tests/Reader302RepeatBudgetR48/{report.txt,frame_budget.json,image_checks.json,launch.json,version_check.json,environment_end.json}，同名environment/cpu_before.json；所有本次測試程序已退出。

## 前次驗收：2026-10-06 BuildR54 / RenderReadyBudgetR47，21/21 平均門檻通過

本輪先完成 ProfileR44（舊版 traced 8/21 PASS）與細分 ProfileR45，再執行 R53 未開 trace 三卡 FinalBudgetR46；R46 的 C010 half 16.700953ms FAIL，未當作完成。R54 實作首次動畫載入的渲染資源就緒流程後，正式根專案 RenderReadyBudgetR47 **functional valid=1、raw mean gate 21/21 PASS**。所有舊失敗與環境紀錄保留；沒有拼接不同測試的最快階段，也沒有裁切首幀或用 Actor Tick 代替整幀。

CPU 高模、1600×1000、C010/C001/C018 各 outfit0；每卡20秒動畫並264個實際 slider delegates，共792次，3秒連續噴射、15秒停止後流動、half/off/restore各5秒，off使用另一個存在的Studio clip。真正衣服模型切換、GUI存卡離線重讀、socket/clock、parallel/serial所有stream屬性、inactive morph legacy oracle均通過。粒子上限、衣服塗層、質量守恆通過。

| 卡 | APNG資源秒 | 身體更新ms | 動畫編輯整幀ms | 噴射整幀ms | 停止後整幀ms | 半脫整幀ms | 全脫整幀ms | 穿回整幀ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| C010 | 2.557807 | 10.954539 | 12.434619 | 13.934572 | 13.182094 | 13.425888 | 14.620444 | 13.334154 |
| C001 | 6.544710 | 10.776623 | 12.248978 | 11.802617 | 11.462196 | 12.912453 | 12.608708 | 13.468485 |
| C018 | 1.162396 | 8.018078 | 9.211132 | 9.280330 | 9.007645 | 9.255012 | 9.735691 | 9.512268 |

載入後原始整幀平均9.007645–14.620444ms；身體更新工作平均8.018078–10.954539ms。shape gate 是身體參數工作平均，動畫編輯窗口另有整幀平均；不把它混成每次滑鼠事件的完整frame latency。APNG資源載入1.162396–6.544710秒，不含首次動畫shader/渲染setup；初始卡顯示仍遵循正常asynchronous shader行為。平均通過不代表每幀皆<=16ms，尖峰完整保留於report。

通用修改：PreparePlaybackMaterials在首次原遊戲/Studio/Unreal動畫setup、ready clock發布前完成active-world material resources。R54新增UWorld::SendAllEndOfFrameUpdates與FlushRenderingCommands，提交shader更新造成的deferred component render states並完成已排隊render resources；只在此actor尚未ready的載入邊界執行，DestroyAllParts清除旗標。未直接呼叫引擎明示禁止直接呼叫的component deferred-update internals；不在每幀等待、不強制GPU idle、不推進動畫暖機。log phase=0.000000，manual Tick/ready clock oracle通過。

新增KK_StreamPrepareSources/FillDraw/SubmitDraw scopes與dry_edit/spray/released regions。ProfileR45 C010 off：來源準備1.802ms、平行填充0.693ms、提交2.239ms；未假設平行填充就是主要瓶頸。不同執行環境負載有變化，不能把兩輪數字差異全部宣稱為單一修正的因果改善。

BuildR54 Succeeded；Reader302RegressionR28 實際D3D 13/13 Success、exit0。GPU byte readback與LWC量化、readonly texture reuse、affine/socket、時鐘、相機、嘴部、油漆transport/film/mask/scene完整通過。9張1600×1000圖像存在/尺寸/非灰屏檢查與全部人工視覺檢視完成；人物頭髮、衣服、透明水柱、衣服附著流痕可見，未見本輪新出現的身體裂開或漂浮手飾。沒有新增降模、停物理或卡名特判，不需重製APNG。

範圍限制：本輪完整驗收是上述三卡outfit0與測试中的動作，並非100張全部outfits/所有動畫或硬體滑鼠流程的保證。GUI參數使用實際control delegate及數值文字/最終shape比對。R47仍有外部Crowd Cook工作，不是quiet環境；其command line已記錄，未停止/改動外部工作。loading期間既有recursive FlushRenderingCommands warning保留追蹤，未被當作播放成本來源的已證明根因。

使用CardReaderDevelopment.cmd；LauncherR54確認正式根專案、CPU、output/CrowdAPNG、1600×1000。正式root DLL hash與launch receipt相符，詳current_hashes.json。所有本次Reader UE與Insights已退出，沒有留下排程。

主模組SHA256：E3E08152B1AC0171250DB3586A4F10555594AC05CC52C2A9D01289963B879599
KKUnityBundle SHA256：FFDCC27B585D8A3D1B6E74FCEC938A42B6BD13631DF97F557F1DC4CBCDBE992F

證據：Saved/Tests/Reader302RenderReadyBudgetR47/{report.txt,frame_budget.json,image_checks.json,launch.json,current_hashes.json}與同名environment.json；Reader302BuildR54.log、Reader302RegressionR28.log、Reader302LauncherR54.json；ProfileR44/R45 trace/CSV/profile_summary.json，FinalBudgetR46失敗證據。


## 歷史狀態：2026-10-06 R52 / R43，功能完成、16ms 效能尚未驗收通過

本次依使用者要求重新 profile 後繼續實作。BuildR52 成功；Reader302RegressionR27 真實 D3D 13/13 Success。R43 完整三卡、20 秒動畫、792 真實 slider events、3 秒噴射、15 秒停止後流動、half/off/restore 各 5 秒，功能 valid=1、serial/parallel stream 全屬性與 inactive morph legacy oracle、socket/clock、9 張 1600×1000 影像均通過；另人工檢視 C010_Flow5s、C001_Jet、C018_Flow5s。

**原始效能 gate 11/21 PASS、10/21 FAIL，未完成 16ms 目標。** C010 dry 17.214473ms、half 32.272121ms、off 25.711765ms；C001 dry 17.037953ms、spray 23.096154ms、half 22.239220ms、restore 18.328108ms；C018 spray 20.888341ms、released 22.177246ms、restore 23.587374ms。其他項目見 frame_budget.json。全部數值保留，不裁切 early frames、不以 Actor Tick 或暖機後值取代 raw 整幀。

APNG 模型／貼圖載入均 <=10 秒：
- card=C010 outfit=0 load_seconds=2.603197 cpu_skin=1
- card=C001 outfit=0 load_seconds=8.586384 cpu_skin=1
- card=C018 outfit=0 load_seconds=5.043302 cpu_skin=1

通用修改：KKStreamMeshProxy 的 bounds 計算併入原頂點更新 pass，供應的 UV／normal／tangent／color 通道只在迴圈外判斷。完整雙精度 CPU vertices、四 UV、GPU packed TBN、draw topology 都保留。新增 exact FBox reference test 與原 GPU byte readback 保證一致。

PreparePlaybackMaterials 將目前人物、active-world shader platform 的 material resource 先全部提交，再一次等待。在首次原遊戲／Studio／Unreal 動畫載入、AnimationReadyClock 發布前執行；卡片資源載入保留原本 asynchronous shader 行為，未改成等全部 shaders 完成。首次動畫 shader setup 可以 loading，waiting receipt 不算成 card load、不藏在每幀；後續已就緒狀態切換重用，DestroyAllParts 清除 readiness。使用實際 material interface resource，保留 static instance permutations；不等待所有世界或其他平台的 shader 工作。

R38/R40 CPU trace 顯示 NativeAnimation 約 3.6–4.4ms、NativeSkin 2.5–3.3ms、PreviewStreams 3.3–4.4ms，Slate Tick 0.43–0.52ms。profile_summary.json 和原 trace/CSV 保存於各 profile 資料夾。nested inclusive 不能重複相加，跨 worker 合計也不是 GameThread 成本。R39、R41、R42 的失敗、超時、主動終止及已反證方案全部保留在 Docs/ERROR_LOG.md。

R43 開始時有 1 個外部 Crowd Unreal 工作（environment.json）；結束後觀察到 4 個外部工作。不是 quiet run，不能聲稱背景工作是唯一原因，也不能用 R50/R42 或舊 R46 PASS 冒充最新版達標。沒有停止或修改外部工作。自己的測試及等候程序皆已結束，沒有排程或遺留自動測試。安靜環境的完整驗收仍待外部負載結束，下一步先記錄背景環境再跑完整原門檻；若仍 FAIL，再 profile 不同段落，不先做單卡特例。

使用 CardReaderDevelopment.cmd，LauncherR52 dry-run 確認正式根專案、CPU、output/CrowdAPNG、1600×1000。沒有降低高模，也沒有重製 APNG。
R43 launch 主模組 SHA256：E9A72B27BEC34B745B85B0D29C97B3ADDF00566E43CC6DCF9C8ADF8E0291FCAB。
KKUnityBundle SHA256：48CE0478B4E30BB06616652B66D41F4C7DE7A11C73C24F4570B329F4B94A4309。
證據：Saved/Tests/Reader302AnimationReadyBudgetR43/{report.txt,frame_budget.json,image_checks.json,launch.json}、同名 environment.json；BuildR52.log、RegressionR27.log、LauncherR52.json。

## E302 歷史驗收：R46 / R37（2026-10-06）

正式根專案 BuildR46 成功。Reader302CachedControlsBudgetR37 是未開 profile 的 CPU 高模 1600×1000 完整測試：C010、C001、C018 各 outfit0，20 秒動畫、3 秒噴射、15 秒停止後流動，half/off/restore 各 5 秒，切換另一個已存在的 Studio 動畫。原始平均門檻 21/21 PASS，valid=1；不裁切早期幀，不以 Actor Tick 取代整幀。

| 卡 | APNG 秒 | 調身體 ms | 動畫整幀 ms | 噴射整幀 ms | 停止後整幀 ms | 半脫整幀 ms | 全脫整幀 ms | 穿回整幀 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| C010 | 2.168819 | 9.859983 | 11.448287 | 13.327894 | 12.147513 | 13.454443 | 13.732102 | 14.768363 |
| C001 | 4.845972 | 9.820225 | 11.076676 | 10.248194 | 10.082231 | 12.571314 | 11.771040 | 12.257189 |
| C018 | 1.025704 | 7.521159 | 8.391893 | 8.217053 | 7.949816 | 8.146906 | 8.512535 | 8.348281 |

每卡實際 ShapeSlider delegate 264 次，共 792 次，數值文字及最終 shape 一致；ReaderControlsCache=1。並行／串行 streams 全屬性比對 946995 / 97310 / 89926 draw vertices，inactive morph 舊路徑比對 7818 / 25474 / 26808 vertices 均相等、時鐘未前進。socket、已蒙皮部件不重複 rigid transform、油漆質量守恆通過。噴射 physics CPU 平均 1.741352 / 0.557248 / 0.450627ms，停止後 1.096224 / 0.379545 / 0.250110ms。粒子上限 512，實測 packets 峰值 7 / 7 / 8、impact FX 峰值 16。

9 張 1600×1000 圖像存在檢查及人工視覺檢查完成：人物、頭髮、衣服、透明白色水柱及附著流痕可見，未見本輪新出現的破裂或漂浮手飾。這不等於所有卡／outfit／動畫皆已驗證；GUI 滑桿是實際控制項 delegate 驗證，未宣稱硬體滑鼠全流程驗證。平均達標並非每幀達標：C010 第一次 half_loaded 211.780600ms、初次表面建立 paint max 90.444300ms 等尖峰完整保留。

Reader302RegressionR23 使用 R46 主模組與實際 D3D，13/13 Success，涵蓋 GPU stream byte readback、雙精度 TBN 量化、readonly texture reuse、affine/socket、時鐘、嘴部、KKS mouse 邏輯、油漆 transport/film/mask/scene，退出 0。未改引擎／全域 Slate 設定，未降模、未重製 APNG、未刪舊失敗證據。

通用優化：保留 CPU 完整幾何、減少未變 UV/color 上傳、64KiB 靜態上傳切塊、緊湊動畫封包、跨部件一次並行 dispatch、略過零權重 morph 拷貝、弱引用唯讀 GPU 貼圖重用、同 collector primitive uniform 重用，以及讀卡控制樹局部 UInvalidationBox 快取。保持讀卡／自身 preview scope，不覆蓋外部 Crowd/GPU 工作。

證據：Saved/Tests/Reader302CachedControlsBudgetR37/{report.txt,frame_budget.json,image_checks.json}；Saved/Tests/Reader302BuildR46.log；Saved/Tests/Reader302RegressionR23.log；Saved/Tests/Reader302LauncherR46.json。
使用 CardReaderDevelopment.cmd，預設 output/CrowdAPNG，CPU，1600×1000，載入正式根專案 DLL。驗收後本次啟動的 UE 已退出。
正式主模組 SHA256：5B16BB4874D0E63B1685B9CE3C9948411E66D06F78095865A7D61F9253C138D6。
KKUnityBundle SHA256：588126E22C615C5EFC724A5B77F66D1BD25B93753480DE091E9CEEA1BD3E9145。


## 驗收範圍

保留高模、衣服真正半脫／全脫資料、原始骨架動畫與物理。卡片／動畫／衣服載入時間可 loading；載入後的整幀、即時身體參數變更工作、噴射中與停止後的油漆物理各檢查平均 16ms。單次尖峰另外記錄，不能以 Actor Tick 時間冒充整幀。

## 已實作

E301 的 live shape 路徑保留 DynamicBone 歷史，更新 shape/reference 後在正常 Animator frame 求解一次；頭部重用未改變的形變基準；CPU 已蒙皮的 body/head 不重複套 rigid 變換。44 個 GUI 身體滑桿每張 264 次變更，動畫與靜止三卡驗證見 READER_SHAPE_EDIT301.md。

E302 取代原本昂貴的 shear residual 頂點更新。UKKAffineProceduralMeshComponent 用完整 GetRenderMatrix 保留非等比骨骼比例與剪切，CalcBounds 使用同一矩陣，不改寫模型頂點。只在矩陣改變時更新 render transform。CPU rigid follower 仍依原卡指定的 socket 計算 neutral inverse × current，不改成任意手腕骨骼，也不降低面數。

水槍 broad phase 使用更新後 bounds，細部三角形與塗層當前位置使用 GetRenderMatrix；塗層從世界位置轉回自身的元件座標，維持附著。讀卡截圖／取景同樣使用實際渲染矩陣。新增 affine shear／bounds／immutable vertices 回歸，以及油漆剪切後貼合距離與質量守恆回歸。

測試加入噴射中 3 秒、停止後 15 秒、half/off/restore 每階段 5 秒動畫整幀門檻。水槍包含每幀表面瞄準查詢；GUI 形變維持每 50ms 下一個滑桿事件。可用 OK_READER_ALTERNATE_CLIP=1 在全脫後換另一個已匯入 Studio clip，再穿回原 walk；不得猜不存在的動畫名稱。

## 尚未完成效能驗收

Reader302ProfileR1 功能 valid1，但 half/off/restore 平均 21.107345 / 18.829390 / 22.424927ms。rigid 殘差的逐頂點更新平均 3.486ms、最大 75.897ms，已由上述 render matrix 方案取代。

Reader302AffineFunctionalR1 功能 valid1，264 次形變、3 秒噴射／15 秒流動、衣服／GUI存卡／離線重讀都通過；C010 35 個 rigid（8 個 arm）實際 draw/socket 誤差 0.000083333cm。衣服塗層 2，粒子 packets peak18、impact peak16、jet limit512，質量誤差約 0。3 張 1600×1000影像 valid，已檢視 Flow5s，手部裝飾跟隨。

這輪與 Crowd 模型處理／编譯並行：參數平均20.136399ms、乾燥整幀18.436243ms、噴射15.171306ms、停止後15.776784ms、換衣後21.871741/23.853843/23.585157ms。不能宣稱新16ms範圍已通過。rigid follow profile 已降低到約0.1～0.2ms，沒有把丟失剪切當成效能優化。

模組建置 Reader302ModuleBuildR1/R2/R3 成功。完整 BuildR6/R7/R8 的 NetCore DLL 連結被其他 Crowd 背景工作鎖定，主專案正式 manifest 尚未完成；没有殺其他程序或手改主專案 manifest。獨立 Reader302HostR1 使用此次編譯的 DLL、相同引擎 BuildId 與 runtime 依賴做功能驗證，記錄模組 hash。後續 ModuleBuildR4 已產生正常 manifests，BudgetR5 與 ProfileR6 均已用主專案正常啟動；不再需要用獨立 host 作為當前啟動版。完整引擎重連結仍受外部 DLL 鎖定影響，但此次修改的兩個模組已正式編譯。

RegressionR3 實際 D3D 十項 Success。NullRHI 的 RegressionR2 只有九項通過，Paint.Scene 包含 RT readback 需要實際 RHI；不能當成完整通過。新增 shear 塗層回歸的 RegressionR4 也十項 Success，確認剪切後塗層離實際表面僅厚度 offset，質量守恆。

## 接續

1. 主專案正常載入已驗證（ModuleBuildR4 + BudgetR5/ProfileR6）；待外部 Crowd Cook 完成後，在相同 DLL 上作完整平均16ms驗收，不再用受干擾結果挑最快值。
2. 執行同版本 C001/C010/C018 的完整讀卡 GUI 平均16ms驗收，包含264形變、動畫、每幀瞄準+噴射／流動、half/off/restore與替代動畫；保留失敗結果。
3. 任何超限要再 profile；保持原始高模、動作／骨骼變形與液體物理，不可降低模型或關閉物理來冒充完成。

## 後續 R4/R5 嚴格結果

BudgetR4（先前 DLL）valid0：形變13.280711ms／乾燥15.668880ms，噴射整幀16.089402ms，不能四捨五入通過。

延伸 bit-exact native dirty check 至小型 rigid kind0，保留 skeleton render matrix 跟隨。ModuleBuildR4 成功並產生正常主專案／runtime plugin manifests；正常主專案 BudgetR5 已實際可啟動，不再由舊獨立 host 代替。

BudgetR5 valid0：C010形變11.569786ms、乾燥整幀13.641483ms、噴射物理1.043777ms／整幀15.480303ms、停止後物理0.889601ms／整幀14.388258ms、half後walk15.250438ms，都通過。但 removed 後替代動畫 studio/anime/00.unity3d|f_tachi_00_00 為16.421403ms，仍不符合完整範圍；restored未執行。ProfileR6 正針對此不同動畫追查。

仍有其他 Crowd authoring 工作，以上保留實際測試條件及失敗，不以另一張卡或先前較快結果替代。

## ProfileR6 與當前限制

完整主專案功能 valid1，包括替代站立動畫／穿回walk，3張影像valid。平均rigid矩陣更新0.075～0.099ms；skin2.853～3.360ms，procedural geometry4.730～5.862ms，pose其餘約5ms，frame尚有渲染等待。各階段細節見 operations_profile.json。

此輪期間外部 Crowd Cook（corelimit19）持續處理約19,000個package：乾燥17.212423ms、噴射18.115147ms、停止18.059060ms、half17.574461ms、off18.092860ms、restore17.679958ms。這是實際超限資料，不能拿「功能valid1」或Actor平均冒稱效能通過。當前功能修改已實作成功，擴大後完整平均16ms目標仍未完成；三卡同版本最後驗收需在可比較條件下继续。

## BudgetR6：沒有把最初空閒誤稱整輪空閒

以主專案同一DLL測三卡，C010形變10.902081ms、乾燥整幀13.090446ms、噴射物理0.886187ms／整幀13.896075ms、停止物理0.800606ms／整幀13.587282ms。其後半脫整幀17.164198ms超限，valid0，另外兩卡沒有執行。

外部負載時間已核對：原Cook於00:48:22結束；本輪動畫測量00:49:57開始，但00:50:27啟動新的Crowd authoring PID46244（RoleBudget-Finish3R107，9workers），半脫播放00:50:38開始。因此整轮並非持續空閒，不能選最早四個成功數據冒稱所有操作通過，也不能據此將原始高模降級。

當前功能已成功，完整同版本三卡平均16ms驗收仍未完成。反覆啟動的外部模型／Cook作業讓可比較的驗收條件不能維持；沒有停止外部作業。主專案完整Editor RegressionR5停在TurnkeySupport載入超過2分鐘、沒有執行測試，僅終止自己PID49052。最新相同DLL以隔離host（BpGenerator editor插件不啟用）執行RegressionR6。

## R9–R14：水槍物理也必須納入16ms

使用者追加要求：白色水槍物理平均16ms以内。驗收同時計算噴射／停止後物理CPU工作與整幀平均；不將單獨物理CPU時間當成整體FPS，也不降低高模或停止流動來通過。

BudgetR9 valid0：C010形變12.126526、乾燥14.384445、噴射14.513545、停止14.248225ms，但半脫16.827989ms。這輪結束後外部authoring才啟動，半脫超限不能只歸因外部工作。新增載入世界時鐘只排除載入前的首個world delta；實際慢幀仍原樣處理。BuildR13把此判斷移至真正的TickActor作用域，直接手動Tick不會被誤認成world tick。

ProfileR10（BuildR13）功能valid1，嚴格效能未通過：shape12.634995、dry14.573769、spray15.524287、released15.348166、half17.680229、off17.616998、restore16.778735ms。水槍CPU平均1.038048／0.883962ms，粒子packet峰18、impact峰16、jet上限512，質量誤差約0。逐部位profile顯示換衣後程序網格更新約6ms，skin約2.5–3ms，仍需優化。沒有以功能valid1冒稱平均16ms全通過。

OtherCardsR11：C018 shape8.224503、dry9.645082、spray9.926254ms，但衣服coating確認失敗，valid0；C001未執行。測試原先瞄準源頂點並用法線朝向篩選，對反向winding／UV邊界不可靠。BuildR14改用實際render matrix上的三角形內部跟隨，不放寬必須命中衣服的條件；新增在後續遊戲幀手動Tick精確取樣的整合檢查。兩張卡仍須重測。

BuildR13/R14完整建置Succeeded。隔離host的RegressionR7–R9未執行測試：新增KKCrowdGpuPacking成為主模組實際DLL依賴，不能以DisablePlugins繞過；其餘runtime manifests也需同步最新build。已更新獨立host依賴再重測；未改主專案插件設定。

## OtherCardsR12（BuildR14，逐section threshold1024）
功能valid1，6張1600x1000影像檢查valid；C018衣服塗層1、C001衣服塗層1，兩卡質量誤差約0，後續幀manual Tick檢查6次均exact1。
C018：形變7.674759、dry9.170748、spray9.369488、release8.962565、half10.122097、off10.393354、restore9.888094ms；噴射／停止後物理CPU.524221/.269728ms。
C001：形變11.057306、dry13.468464、spray13.000717、release14.498784ms；噴射／停止後物理CPU.351742/.199738ms。但half19.920614、off16.384875、restore17.046280ms超限，不宣稱全通過，也不把最大47ms的形變工作冒稱每次16ms（使用者要求平均）。
水槍physics在三卡已有低於16ms的實測；整幀的换衣後動畫依然未達標。R15改為一次排程同一網格所有material surface/outline streams，頂點公式與UV/tangent/color保留原樣，以serial/parallel完整draw比較驗證。尚需新build及runtime parity/perf，不能用R12替代新版本驗收。

## BatchStreamsR15：完整三卡保留超限
BuildR17 functionality valid1，三卡shape/draw/socket/三角形內部命中/向下流動/半脫全脫穿回/存卡重讀均通過，9張影像valid。serial/parallel exact draw vertices：C010946995、C00197310、C01889926。check_reader_frame_budget.py結果passed=false。
C010 shape12.519617/dry14.742030/spray19.119974/released15.406071/half17.562419/off18.292770/restore18.643163ms；paint CPU5.182452/1.564197ms，首噴單次552.772101ms。
C001 shape13.450009/dry16.549442/spray18.546672/released14.980648/half19.714917/off19.140213/restore19.341653ms；paint CPU1.850988/.781970ms。
C018 shape9.656414/dry12.414651/spray11.664926/released11.030464/half12.049772/off12.893746/restore12.394881ms；paint CPU.862823/.419655ms。
外部authoring47688於01:53:04啟動，後續Cook55700於01:58:28啟動，未停止或修改外部作業。數據是真實超限，不能用較早另一輪替代。
R18把M_PaintFilm與M_PaintDrop在Sprayer BeginPlay（預覽載入）解析並UPROPERTY保留；InitializeCoating/Splash/attached drops不再互動時LoadObject。UE5.8本地源碼確認UpdateResourceImmediate只排入render command，沒有FlushRenderingCommands；不可憑方法名稱斷言它同步等待，故保留初始化clear的正確順序。新增operation first-observed/warmed診斷，仍以完整5秒原始平均判定，沒有用warm數字取代預算。新版本待實測。

## R20–R22：首噴瓶頸已定位到幾何初始化
R18實際profile首個paint更新 geometry492.711ms、simulation.036ms、coating.004ms；23個候選表面中4個各88064三角形耗104–111ms。不是材質預載就能解決。BuildR22已成功：只為真正命中的表面建立film鄰接及法線，保留發現時姿勢以維持原來接縫圖；BVH以快取中心做median partition，碰撞仍使用原始三角形精確sweep。MeasureSweepPair新增獨立遍歷三角形oracle，避免僅比較同一BVH的兩種快取模式。
BuildR21因另一處Crowd呼叫引擎private API失敗（水槍cpp通過）；未修改那邊程式，後續其修正後R22完整Succeeded。Reader302DeferredGeometryR19正在以相同版本測C010/C001/C018，尚無新效能驗收結論。R18仍有spray17.014208、off17.275413超限，不當成通過。

## R19與R21最新實測（功能通過，完整16ms尚未通過）
R19 BuildR22：三卡264身體事件各完整通過、serial/parallel全屬性exact、動畫socket/接縫/髮型/實際半脫全脫穿回/液體守恆向下流動/9影像valid；RegressionR14十一Success。物理CPU spray/release：C0101.808629/1.213638、C001.685060/.479865、C018.624026/.304523ms。全部spray/release整幀<16。仍失敗C010off16.540096/restore16.309721、C001half18.148521/off16.016060/restore16.569818。C018全階段通過。保留完整rawreport和strictchecker passed=false。
R23把UE mutable streams改成Actor持有的跨幀scratch，UpdateMeshSection仍複製自己的render command，原始頂點計算與高模未變。完整build因其他Crowd resident程序鎖住NetCore引擎DLL失敗；未終止其他工作。R24只建OpenKoikatsu+KKUnityBundle的UBT module target成功（本地原始碼確認Module ListSeparator）。主DLL02:41:45、plugin02:41:24。
PersistentStreamsR21功能valid1、三卡完整264事件/全屬性parity/九影像valid，但同時4個其他Crowd稽核process逐步啟動，整幀預算仍failed。C010shape12.265743/dry16.503945/spray15.500556/release16.775480/half17.158626/off20.054092/restore27.189375；C001shape15.017146/dry18.477036/spray17.991169/release21.543800/half23.153565/off29.859646/restore26.391453；C018shape11.578200/dry15.303968/spray17.140638/release12.672547/half15.613689/off17.148953/restore12.735849ms。此結果不能證明scratch變快或變慢，沒有以舊輪數字替代新輪通過。
R21水槍物理CPU spray/release：C0102.077980/2.064300、C0011.323239/1.629002、C0181.180185/.535378ms；仍低於使用者16ms平均門檻。首噴max98.292302ms依然有尖峰，平均達標不代表每一次工作<16。最新RegressionR15正在驗證R24模組組合。整體需求E302未完成。

最新版RegressionR15十一項全部Success。功能與物理CPU平均已驗證，整個讀卡模式16ms整幀門檻仍未通過，不能標成全部完成。

## R24/R25：完整預算與 Insights 實際瓶頸
RigReuseBudgetR24 quiet run功能valid1、792身體事件、9影像完整；整幀仍未通過。C010shape14.482044/dry17.879063/spray18.621074/released13.803585/half15.609696/off16.347453/restore16.428028；C001shape11.233076/dry13.531527/spray13.189125/released12.577179/half17.255924/off15.792570/restore16.305778；C018所有7階段平均<16ms。
R25cpu/frame/region診斷 trace定位C001換衣後5秒區段各一次697/699/804ms GT rendering-sync等待；render UpdateSection RT最高711.983ms，其中D3D12 upload allocator自身711.357ms。角色CPU平均12–13ms，但不可當整幀通過；也不可假定allocator內部是lock/driver，尚未證實。native export_ms0.000，private accessory rigs才是剩餘native成本。

## R34：保留高模、跳過不變的渲染 stream
KKUnityBundle的KKStreamMeshProxy改編安裝UE5.8 PMC scene proxy，保留原有材質、4組UV、顏色、packedTBN、描邊、動態bounds、raytracing路徑及render packet所有權。只在讀卡器自有無collision procedural meshes opt-in；第三方與collision sections用stock PMC。每個packet携帶UV/color真正dirty旗標，不變資料不再GPU upload。動態位置和切線每次仍更新，沒有降低精度、降模或跳動畫。
所有preview/secondary morph更新及outline visibility明確用wrapper路由；因stock PMC API內部對私有proxy做nonvirtual cast，不能對opt-in proxy呼叫stock update/visibility。body/head defaultsubobjects也opt-in。對externallyownedPMCs的wrapper立即走stock。
曾試project同名PMC override，但UE拒絕engine→project模組依賴；已撤回至Saved/Tests/Reader302RejectedPluginOverrideR30，未改引擎或禁用其他plugin。R31 Bounds shadow錯誤及R32 PhysicsCore link dependency修正已記ERROR_LOG。
BuildR34Succeeded。RegressionR16實際D3D十二項Success。新增StreamUpload測試檢查render-thread retained buffers與upload command counts：60動畫更新UV/color uploads0；真正UV3/color修改再排unchanged packet仍各upload一次；packedTBN/fourUV/color/position相同、bounds/visibility正確。不是獨立GPU readback，實際三卡圖片另外驗收。Reader302StreamUploadBudgetR26完整三卡實測進行中；不可尚未有結果就宣告所有16ms通過。

## R26/R27/R35：上傳配置尖峰的下一步修正
R26完整三卡功能valid1、792身體事件／9影像／零mappingerror。C010七階段10.901515/13.026742/14.397129/13.431972/15.127989/15.961384/15.596115ms全部通過；C001半脫17.254901與穿回17.616700仍超限，strictcheckerfalse；C018七項全部通過。不得用C001的warmedmean取代full5second mean。
R27診斷trace仍顯示C001每區段upload allocator659/665/681ms。本地D3D12源碼確認64KiB smallpool與static buffer range writes；dynamicbuffer卻會rename整張buffer，因此不能分批。R35只把static buffer uploads拆成最多64KiB連續byte ranges，dynamicAnyDynamic完整寫入。範圍完整覆蓋且順序與原先一致，無任何productionGPUwait。可用ok.Animation.StreamUploadChunkBytes=0做完整上傳基準，預設65536。
BuildR35Succeeded，RegressionR17十二項Success。StreamUpload現為13000頂點，跨64KiB邊界，測位置／packedTBN／四UV／color的獨立GPUreadback逐byte相同，並驗證真正dirty uploads。同步RLM_ReadOnly僅在devtest；實際production沒有Flush/GPUreadback。Reader302ChunkUploadBudgetR28三卡完整無trace效能驗收進行中，E302仍待其結果，不能提前完成。

### R28–R40 continuation: whole-frame acceptance still pending
Full untraced R28 functional audit valid=1, 792 shape events and nine image checks passed. Raw means still failed for C010 full-off 16.079505 ms, C001 half 17.276471 ms and restored 16.482958 ms. These are failures, not rounded passes. Other 18 stages passed. APNG loads were 2.374534/4.021285/1.556413 seconds.
Correctly configured diagnostic R30 isolated the remaining allocation hitch outside KK_StreamUpload: own upload maxima <0.64 ms, while SlateDrawWindowsCommand / RenderGraphExecute / SubmitBufferUploads / Upload_Multiple / AllocUploadResource blocked about 690 ms, concurrent with RHI_SubmitClose. Source inspection locates cleanup at RHIEndFrame but does not prove the uninstrumented internal cause.
R36–R38 add opt-in weak readonly GPU texture reuse with full source-byte and sampler identity, 64 MiB identity-data cap, no ownership of GPU resources, independent mutable texture factory, and stale metadata rejection. R39 shares identical primitive uniform parameters only within a single scene-proxy collector invocation. Geometry, materials, collision, UV mapping, animation and paint transport stay intact. R39 compiled; first new test launch R19 failed before tests because concurrent Crowd source had newly imported symbols absent from its existing DLL. R40 explicitly links that dependency without modifying foreign source/settings. Corrected readonly test fixture uses named ABI fields rather than incorrectly ordered aggregate initialization. RegressionR20 and a fresh full three-card budget are required before acceptance.

### R41–R42 compact render packet verification
Animated updates previously copied every FProcMeshVertex (four unchanged LWC UV channels/color included) into the render command after the CPU section update. New packet is filled in that existing loop and contains only animated position/normal/tangent; UV/color arrays are separate and present only on actual dirty flags. Source normal/tangent stay double precision until original FPackedNormal quantization; a float-first conversion would change half-integer boundary rounding. CPU section, actual triangles, materials and paint queries retain original content. BuildR41/R42 succeeded. RegressionR21 thirteen tests all Success, including real GPU position/packedTBN/UV0–3/color byte equality across 64KiB chunk boundaries, queued dirty then unchanged packets, and new double quantization boundary/handedness coverage. Production has no GPU readback/wait. Full three-card untraced CompactPacketBudgetR33 now running with shape editing enabled and all phases measured; R32 single-card/no-shape diagnostic is not substituted for acceptance.

### R43–R44 global stream scheduling and inactive expression data
R34 correctly traced the shaped C001 scenario, retaining raw failures; native animation5.760–6.320 ms, CPU display streams4.859–5.118 ms and skin2.104–2.456 ms remain relevant. Original700ms resource wait is not the current sustained CPU explanation.
R43 keeps each changed source mesh/view and section, schedules disjoint512-vertex ranges across all changed sections once per actor frame, then publishes the same full buffers in source section order. Retained scratch is indexed by work item and never shrinks. The same scalar vertex/normal/offset/tangent math runs in serial/parallel modes. R44 caches immutable per-slot morph names, skipping raw index/delta copy only where effective baseline weight is absent or exactly within the original near-zero rule. Legacy unpruned path is retained as an explicit reference; full audit now compares all face/outline positions, normals, tangent handedness, four UVs and colors at the same clocks after shape editing. No visible expression/animation/physics/geometry quality is reduced. BuildR44 succeeded. RegressionR22/full budget pending.

R44 verification: RegressionR22 thirteen actual-D3D tests all Success. Rust part_at source verified views point into per-part retained positions/normals/reference tangent vectors, not a reused transient output buffer. Global workers only read these views after all game-thread part preparation and before any next native animation/skin mutation. The shaped reference audit remains required for integration. Root executable launcher DryRun previously verified CPU mode, actual project DLL and output/CrowdAPNG default; no engine/plugin override or APNG regeneration is needed for these source changes.

### R45–R46 static reader UI cost
R35 actual body/face integration passed,20 of21 raw stages passed; C010 restored16.534021 ms remains a real failure. R36 current shaped C010 trace shows static reader Slate prepass .573–.601 ms and slow paint .506–.543 ms per loaded frame. R45 uses local UInvalidationBox around the reader/gallery content, preserving root content/hit-test visibility and event invalidation; no engine/global Slate changes. R46 now sends264 actual UOKShapeSlider OnValueChanged events per card, checks displayed numeric ValueLabel after each and verifies cache plus final native shape. Thus the final dry/editing measurement includes actual control updates as well as model deformation. Full three-card untraced acceptance must be rerun; historical R35 remains a failure.

## 2026-10-06 再次 profile：R38 → R47（後續完整驗收待 R39）

依使用者新要求對 R46 正式根專案重新 CPU trace。Reader302ProfileR38 功能 valid=1，C010/outfit0、1600×1000、20 秒動畫、264 次真實 body slider delegate、3 秒噴射、15 秒停止、half/off/restore 各 5 秒。不是簡化模型，也未關閉嘴部或物理。

| 追蹤平均 ms | 半脫後 | 全脫後／另一 Studio clip | 穿回後 |
|---|---:|---:|---:|
| KK_NativeAnimation | 4.414 | 3.613 | 3.685 |
| KK_NativeSkin | 3.337 | 2.507 | 2.581 |
| KK_PreviewStreams | 4.446 | 3.729 | 3.307 |
| Slate Tick | 0.522 | 0.447 | 0.431 |

原生動畫與 streams／CPU 蒙皮是當前主要角色更新成本；介面已不再是主要成本。這些是 nested timer inclusive 平均，不可與父計時器再次相加。跨 worker 的 WaitForTasks 或 ShaderJobTask 合計也不是 GameThread 成本。trace 期間仍有 shader worker 工作，不將此 run 的平均當作正式無追蹤達標證據。

針對 streams 的 R47 修改：只在每次 update 開始時決定哪些 UV／normal／tangent／color 通道有提供；在原 CPU 頂點更新 pass 同時累积精確 FBox，省掉單獨位置掃描。四 UV、顏色、雙精度法線／切線、位置、原始 topology 和 GPU packed 量化完全保留。新增 FBox(Positions) exact equality，既有 13000 頂點 / 60 update 的 GPU readback 仍檢查所有 bytes 與 64KiB 邊界。

BuildR47 成功；Reader302RegressionR24 真實 D3D 13/13 Success（非 NullRHI）。完整三卡不開 trace 測試 Reader302FusedStreamsBudgetR39 正在驗證；在結果前不宣稱優化幅度。正式 DLL 也含目前工作區已存在的其他模組變更；沒有覆蓋或回退其來源。

profile 原始 reader.utrace、Insights.log、all_Reader_C010_*.csv 及 report.txt 保存在 Saved/Tests/Reader302ProfileR38。
