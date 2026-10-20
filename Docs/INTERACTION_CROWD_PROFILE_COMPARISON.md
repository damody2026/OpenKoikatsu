# Crowd 與一對一互動場景效能根因比較

## 本輪結論

最新獨立乾播放已通過兩姿勢平均<=6ms：**5.818209／5.811446ms，171.875／172.010FPS**，最大完整幀12.063／8.796ms。無外部UE／編譯／分析干擾、四DLL hash stable、正常exit0。完整Editor build、五項UE回歸、新版細分trace與完整PairPaint功能驗收均完成。這份乾動畫結果不能代替水槍／全部控制120FPS验收。

2026-10-07重新量測已重現Crowd平均5.x ms。主要差異是CPU執行路徑：Crowd使用runtime生成、常駐GPU的動畫bank；一對一仍逐幀完成雙角色原生動畫、IK、物理與可變顯示資料更新。雙人的GPU時間比100人Crowd低，不能把差距歸因於高模三角形數，亦不能靠降低畫質解決這個CPU瓶頸。

## 正式獨立無trace結果

視窗1600×1000、FPS上限解除。兩輪順序執行，無遊戲／編譯／分析程序干擾，binary hash穩定，正常exit0。乾雙人無水槍；Crowd保留原CMD的100張APNG、背景動作streaming與眨眼，停用diagnostic截圖與demo輸入。

| 執行路徑 | 完整幀平均 ms | GameThread ms | RenderThread ms | GPU ms |
|---|---:|---:|---:|---:|
| Crowd GPU mode／100人 | 5.044074 | 1.944818 | 5.034554 | 3.616767 |
| Crowd CPU mode／100人 | 119.724766 | 99.881018 | 38.306426 | 4.149559 |
| 雙人直接曲線取樣／姿勢一1x | 7.325042 | 7.323650 | 6.570330 | 2.210667 |
| 雙人直接曲線取樣／姿勢二1x | 7.431856 | 7.429922 | 6.659131 | 2.221361 |
| 雙人修正後／姿勢一1x | 5.818209 | 5.811944 | 5.428826 | 1.990630 |
| 雙人修正後／姿勢二1x | 5.811446 | 5.808273 | 5.400419 | 2.002397 |

初期雙人Present為136.452／134.628FPS，最大完整幀19.755ms；當時平均6ms未達。最新修正後Present為171.875／172.010FPS，兩姿勢乾平均6ms均通過。所有控制並行／水槍FPS另做獨立矩陣，不能把乾播放結果當成全部互動通過。

Crowd使用CMD原本的R1147 packaged Development executable與R1149 common patch；雙人使用目前Editor game模式。這不是相同binary的A/B，框架差異仍需列入判讀，不能把全部差值算成原生動畫。

證據：

- `Saved/Crowd/CrowdPairRootReference1600VisibleR2/scene.json`、`run-evidence.json`：2974個GPU模式樣本、100人完成、GPU failures0、external work0。
- `Saved/Tests/InteractionShowcase/DryPair6msDirectChannelsVisibleR2/report.txt`、`run-evidence.json`：速度與姿勢明確記錄、四DLL hash穩定、isolated1。

## Trace定位

Trace只用於定位，完整幀验收以上述無trace結果為準。CPU inclusive scopes重疊，worker CPU合計不能加到GT等待上。

| 工作 | Crowd穩定段 ms/幀 | 雙人PaletteBatch穩定段 ms/幀 |
|---|---:|---:|
| UWorldTick | .449 | 5.667 |
| PairAdvance | 不適用 | 5.032 |
| 原生PairNative | 不適用 | 2.873 |
| NativeSampleWait | 不適用 | .841 |
| NativeDynamicsWait | 不適用 | 1.266 |
| RoleDisplay | 不適用 | 2.069 |
| Private palette batch | 不適用 | .591 |
| SceneRender | 4.288／1次 | 4.583／3次 |
| 每幀PrivateGpuGraph | 常駐bank | 1.630／約1次 |

Crowd trace GameThread的Frame scope約5.095ms，其中3.136ms等待RenderThread；它不同於上表正式報告的GameThread工作時間。雙人WorldTick的增加幾乎由PairAdvance解釋，這是主要CPU根因。

原生worker雙角色curve取樣合計1.060ms、37個private rig的AccessoryStep合計2.897ms；這些是CPU工作合計。雙人SceneRender含主視圖和兩個角色toon shadow capture。Crowd約664.7萬顯示三角形（原始910.4萬），仍比雙人的GPU時間高。

Trace資料：`Saved/Crowd/Profile-CrowdPairRootReference1600Events-RootR2.json`（111–114秒）；`Saved/Crowd/Profile-DryPair6msPaletteBatchEvents-Root.json`（84–87秒）。Crowd trace退出有既有777003問題，因此只作診斷；正式無trace重跑exit0。

## 已實作修正與驗證

1. 常駐private source與revision、批次讀取當前palette；保持source變更的原子失敗／舊路徑fallback。兩張實際APNG的批次／serial palette及GPU幾何oracle一致，四项UE回歸PASS。
2. runtime編譯曲線channel地址，常駐clip與rig/avatar計畫，逐幀直接取sample，省去完整曲線字典與binding複製。保留開始時間、controller時鐘、looping、dense時鐘、step/cubic/nonfinite fallback及constant覆蓋。
3. channel邊界測試PASS，Animator43 PASS；實際APNG 240幀walking direct vs legacy world/local TRS完全一致；64幀雙人骨架／private particles精確oraclePASS。UE GPU／bounds／54嘴型四项PASS。獨立完整幀從先前7.591ms降到7.325ms，改善有限，6ms未達標。

## 本輪驗收狀態

- [x] Crowd 100人與高模雙人的独立完整幀對照、all-thread trace根因比較。
- [x] Runtime骨架地址／binding／精確物理cache修正及真APNG oracle。
- [x] GPU publication沒有bounds變化時避免無效primitive dirty，實際GPU頂點readback驗證。
- [x] 完整Editor build、五項UE回歸、54嘴型／語音／眼頭／肚子／循環行走／12姿勢切換／男女水槍功能驗收。
- [x] 兩姿勢乾平均<=6ms：5.818209／5.811446ms，独立無trace。
- [x] 最新paired close-up眼睛圖像與編輯器／互動入口返回複驗。
- [ ] 全控制／水槍120FPS独立矩陣；首輪外部干擾，濕面需另細分trace。

不把Crowd的60Hz量化pose bank直接套到live雙人求解，不減少物理頻率／刪除backlog，不按性別或卡名設定例外。所有人物資源由APNG與共用資料runtime處理。

## 2026-10-07：細分trace後續修正

DenseWorldFused trace穩定102–105秒：PairAdvance4.595ms、PairNative2.544ms、RoleDisplay1.963ms；DynamicsWait1.219ms、SampleWait0.592ms、palette batch0.569ms。worker合計DynamicApply1.087ms、Particles0.582ms、Forces0.460ms、Topology0.458ms。PrivateGpuGraph1.321ms，原本1.630ms。SlatePrepass約0.16ms、InteractionWidget paint約0.13ms。

已完成runtime稠密WorldPlan、融合GPU變形／packing、private拓撲inventory驗證快取、每批palette共用同一frame policy、Slate標準invalidation controls cache。WorldPlan重建保留HashMap迭代順序與矩陣乘法；拓撲快取遇rehash／增刪／reparent自動重建。兩張實際APNG 120幀private物理與獨立重建oracle完全一致，女21rigs／72chains、男16rigs／25chains。GPU融合259頂點四種模式position與packed tangent byte完全一致，最新UE五項回歸及人物編輯器／互動返回入口功能PASS。

DenseWorldFused trace載入期間有別的UE工作，因此其7.187ms不能當正式效能驗收。TopologyPolicyUi第一輪亦受另一個UE與cl編譯干擾，已記錄並重跑。平均6ms尚未通過。

下一個精確優化針對DynamicApply：世界位置轉local只使用inverse basis，取消未使用的inverse translation欄求解，但保留完整來源矩陣、四次pivot、奇異值fallback與逐項運算順序。先驗證每個保留元素bit一致及實際APNG物理，再進正式binary。不採近似逆矩陣或改物理頻率。

## 2026-10-07：獨立無trace乾播放進展

DryPair6msTopologyPolicyUiVisibleR2正常exit0、external0、四DLL hash穩定。姿勢一1x平均6.841537ms／146.103FPS（GT6.832515／Render5.940975／GPU2.039047）；姿勢二1x平均6.899454ms／144.940FPS（GT6.898330／Render5.992115／GPU2.044689）。最大完整幀16.350／16.526ms。verbose診斷段7.843675ms，不能混成一般乾動畫。相較DirectChannelsVisibleR2的7.325042／7.431856ms，有0.484／0.532ms改善；平均6ms仍未通過。

後續實作：保留完整source與四次pivot，僅不求解unused inverse translation欄；4096一般／仿射／奇異／非有限矩陣保留basis bit exact PASS。private物理rotation cache只在本次solver step有效，rotation寫入仍逐級invalidate，跨chain共享有效祖先結果，position寫入不會改rotation。palette批次父attachment依當前body owner與原key共用readonly結果，missing parent仍失敗、完整驗證後才寫出。

## 2026-10-07：BasisParent trace與精確求解後續

BasisParentTrace遭06:42:34–06:43:01另一個OpenKoikatsu程序干擾，整輪FPS不採用；quiet1於06:43:09開始，選102–105秒（約06:43:12–15）的獨立CPU事件範圍診斷。439幀：PairAdvance4.481ms／Native2.508／DynamicsWait1.150／RoleDisplay1.884；palette .461（前 .569），DynamicTopology .057（前 .458），DynamicApply worker合計1.170、Forces .456、Export .350。相關資料Profile-DryPair6msBasisParentEvents-Root.json。

Inverse基底micro A/B checksum相同，262144次完整逆矩陣8.927／8.863ms，基底7.744／7.721ms，約13%改善；這不是完整幀FPS，不依此宣告整場達標。

追加runtime通用修正：兩邊peer targets全部凍結後，獨立IK solver並行且join再推進物理；一個solver step內共用reflection-aware lossy orientation，Apply後invalidate所有粒子節點子樹，frame開始清空，保留negative/nonuniform scale原算法；baseline restore在確切HashMap inventory order一致時直接zip，order不符使用原hashlookup。新DynamicBone37 PASS、Pose15 PASS；男女120幀private/full-rebuild oracle PASS，64幀雙人serial／parallel骨架與private粒子完全一致。完整UE功能測試正在執行。

下一項Export .350ms worker根因：每次refresh重建lowercase名稱HashMap並分配String。改為runtime compile bindings，以source inventory ID／名稱／迭代順序及export名稱／locked配置作signature；當幀仍讀live TRS，duplicate與source priority、missing保留舊值、locked identity不变。測試需覆蓋rehash／增刪／rename／source切換／輸出重排與locked改變，及實際APNG palette回歸後才能採用效能結果。

### 匯出與debug資料修正驗證

ExportBindingsRegression五項UE PASS（男女實際APNG PrivateGpuSource／GpuStreamProxy／ExactLeaderPose、RenderedMouth54、FusedDeform）。runtime binding單元驗證八輪source priority、duplicate/case/Unicode、rehash、rename、增刪、source None、export重排及locked變更，保留missing原值與live TRS。Export僅讀当幀資料，不維持跨frame TF references。

BasisParent trace還有SkelMeshCopyDebugPose約0.11ms GT，此資料供debug顯示使用。互動Scene BeginPlay在game runtime以引擎標準TGuardConsoleVariable暫設0，EndPlay立即恢復原值；console更高priority仍有效，真正GIsEditor保留debug資料。ok.Interaction.CopyDebugPose1可作舊路徑診斷。已加入navigation entry與return的實際值檢查，最新build成功，正在功能驗收；尚未有最新無trace6ms結果。

### 最新完整功能驗收PASS

DryPair6msScopedDebugNavigation正常exit0／functional1：APNG人物編輯器與互動入口／返回，live body editing、互動fixed body except belly、Load3D按鈕、debug pose scope設定與返回還原均通過。

DryPair6msWetRegistryPairPaint正常exit0／audit_completed1／functional1：54嘴型與語音、walking兩週期loop、聲音取消／替換、live頭部／嘴型／肚子、12 ready姿勢切換、男女實際水槍、濕面anchor、切換後雙角色query、conservation、clear、男角替換與失敗保留、離開取消均PASS。12姿勢切換最大完整幀38.140ms，最大同步控制156.901ms（仍低於300ms）；女92hits／36.8deposit、男210hits／31.2deposit，private geometry error女0.000005293cm／男0.000006838cm。修正驗收清除順序後，兩角色registry保留與pose-switch檢查通過。

這是完整功能驗收，不代表噴射時120FPS已達標。新版無traceDryPair6msExportScopedVisible乾播放量測進行中，等待isolated與完整幀结果；平均6ms未先行宣告。

### 無trace最新正式乾播放結果（6ms未達）

DryPair6msExportScopedVisible正常exit0／四DLL hash stable／external0／isolated1。姿勢一1x全幀6.433019ms、155.446FPS，GT6.431766／Render5.871483／GPU2.036022ms，最大14.481097ms；姿勢二1x6.480722ms、154.303FPS，GT6.477970／Render5.900009／GPU2.050640ms，最大11.576898ms。verbose段7.708914ms，與正常乾播放分開。相比DirectChannelsVisibleR2的7.325042／7.431856ms，降低0.892／0.951ms，但未通過<=6ms。

下一輪先重新細分trace；runtime candidate把已驗證的原生sample／frozen-contact IK／private physics置於同一scoped worker transaction，保留每個join/barrier及錯誤檢查，减少GT多次停等／worker喚醒。OK_PAIR_STAGE_DISPATCH1保留原dispatch作精確A/B，runner新增LegacyStageDispatch。增加KK_NativeFrameWait trace事件與array容量static_assert，尚待unit／真APNG／建置／量測，不能先宣告收益。

### Worker transaction 的實際結果：目前沒有收益

DryPair6msWorkerTransactionOracle：男女實際APNG 64幀serial／parallel骨架與private粒子精確一致。最新DLL包含新dispatch標記；DryPair6msWorkerTransactionRegression五項UE回歸PASS。DryPair6msWorkerTransactionVisible正常exit0／external0／isolated1／hash stable：姿勢一6.582093ms、姿勢二6.506994ms，PairAdvance4.240320／4.213123ms。比前一版6.433019／6.480722ms沒有可確認收益，不能把減少dispatch次數直接當成效能改善。正在補新的all-thread trace，依實際critical path決定保留或撤回；平均6ms仍未達。

### 新trace：計算量仍是主因，匯出修正確實減少重複工作

DryPair6msWorkerTransactionTrace正常exit0／external0／hash stable，載入renderer崩潰未重現。quiet1的101–104秒443完整幀、886角色sample。Frame6.753ms、PairAdvance4.407ms；Native2.318ms（FrameWait2.064、Validate.167），RoleDisplay1.993ms（palette.507、PreviewStreams1.174）；原生worker Export合計.110ms，先前.350ms，binding table省掉重建名稱索引。Contacts .376ms、DynamicsWait1.057ms、DynamicApply worker合計1.221ms。worker totals不是critical wall time，不可直接相加。

後續優先驗證worker容量對37個private rig及palette並行的影響，8→16只作A/B，不預先修改預設。仍保留同一frame內sample→凍結contact→IK→physics→publication全部barriers；若增加worker沒有收益則保持原限制。資料為`Saved/Crowd/Profile-DryPair6msWorkerTransactionEvents-Root.json`。

下一個通用候選：DynamicApply／Forces重複清掉未變動祖先的world matrix。solver step的TF寫入僅發生在Apply，現有position／rotation setters已逐節點invalidate完整cached descendants；particle integration與collider pose讀取不改TF。因此frame開始仍清空、寫入仍精確invalidate，只保留本步內未變動矩陣跨particle phases／chains，獨立oracle維持原本每階段重建。必須通過37-rig真APNG 120幀及雙人serial／parallel完整pose／粒子比對後才建置採用。

### WorldCarry實測有效，乾播放接近6ms

DynamicBone37 PASS，男女120幀共37rigs／97chains private/full-rebuild oracle完全一致，雙人64幀serial／parallel骨架與private粒子一致；UE五項GPU／leader／54嘴型／fused回歸PASS。DLL／native lib已更新；整專案build仍有另一工作未完成表情API的錯誤，詳見錯誤記錄，不能把模組建置寫成全專案PASS。

DryPair6msWorldCarryVisible正常exit0／external0／isolated1／四DLL hash stable：姿勢一全幀6.113992ms、PairAdvance3.962258ms；姿勢二全幀6.094284ms、PairAdvance3.967102ms。相較同排程的WorkerTransactionVisible降低.468／.413ms，是實際完整幀改善，沒有降頻／近似。仍未通過<=6ms，正在用同binary比較舊dispatch邊界，然後完整功能驗收。

### 排程A/B結論與下一個批次索引修正

WorldCarryLegacyDispatchVisible正常exit0／external0／hash stable：姿勢一5.948418ms、姿勢二6.159812ms，PairAdvance3.909108／4.023567ms。單一姿勢已<6ms，但另一姿勢仍未達；outer transaction没有穩定收益，已撤回並移除診斷環境參數／runner switch，保留KK_NativeFrameWait計時，原始各階段join不變。16 workers也沒有收益，保持8。

palette批次的每個part會再次驗證同一body／clothes rig_name_index完整inventory。改為批次在caller只驗證每個body owner一次並共享owned readonly Arc index；workers仍分別以當幀TF求worlds，沒有共用或延遲live矩陣、不啟用先前失敗的AssignedWorldFrame預計算。現有first duplicate policy、rehash／rename／增刪invalidation與缺骨fallback保留，serial API保留原索引路徑作精確oracle。

下一個render根因：GPU streams已有保守bounds union，但每幀即使union完全不變仍UpdateBounds／MarkRenderTransformDirty，導致UE反覆更新primitive並重建draw commands，Crowd常駐bank不做這種更新。只在原始union真正改變時更新bounds／render transform；每幀仍提交全部GPU pose／vertices，bounds不縮小，invalid／stale publication仍不能縮小last valid bounds。新增實際proxy readback驗證：bounds內換pose必須更新GPU頂點且不dirty transform，bounds擴張仍必須dirty；涵蓋direct／batch publication。

BatchIndexOracleR3的4464 live矩陣exact，男女native staged oracle max_error0，UE五項PASS，scoped模組build PASS。正式BatchIndexVisible正常exit0／external0／hash stable：6.051386／6.140988ms，PairAdvance3.981424／4.055271。與WorldCarryLegacyDispatch的5.948418／6.159812相比，單獨索引共享沒有可確認完整幀收益，仍未達兩姿勢6ms；下輪優先測bounds publication，若沒有trace收益則撤回額外索引分支。

StableBoundsFullBuildR2完整Editor build Succeeded（11.58秒），解決另一工作尚未完成API造成的先前失敗。StableBoundsFullRegression五項UE全部PASS，包括新增GPU contained-pose readback／bounds擴張dirty assertions、男女真APNG palette serial／batch、54嘴型與fused packing。新版獨立完整幀量測進行中。

### StableBounds乾播放正式通過兩姿勢6ms

StableBoundsVisible正常exit0／external0／isolated1／四DLL hash stable，1600×1000高模雙角色：姿勢一全幀5.818209ms／171.875FPS（GT5.811944／Render5.428826／GPU1.990630），姿勢二5.811446ms／172.010FPS（GT5.808273／Render5.400419／GPU2.002397）。最大完整幀12.062799／8.795500ms，遠低於300ms。相較BatchIndexVisible6.051386／6.140988降低.233／.330ms；相較本輪最早DirectChannelsVisibleR2的7.325042／7.431856降低1.507／1.620ms。verbose診斷7.345528ms單獨記錄，不混入正常乾播放。新版trace及PairPaint完整功能驗收接續執行。

### 新trace驗證根因修正

StableBoundsTrace正常exit0／external0／hash stable。quiet1 94–97秒483完整幀、966 role samples；Frame6.183ms（trace成本不能代替5.81ms正式驗收）、PairAdvance4.096／Native2.149／FrameWait1.870／RoleDisplay1.852／palette.444ms。相比WorkerTransactionEvents：DynamicApply worker合計1.221→.782ms、Export .110→.098ms、palette .507→.444ms；body inventory批次共享保留當幀world計算。Physics wait1.057→.902ms。

渲染精確根因證據：FPrimitiveSceneInfo_CacheMeshDrawCommand worker合計.548→.01344ms，約減少97.5%；RenderThread UpdateAllPrimitiveSceneInfos .32271→.23043ms；GT SendAllEndOfFrameUpdates .41514→.34908ms。原始保守bounds union大部分時間未變，移除無效dirty避免反覆重建draw commands，GPU動畫仍每幀提交／更新。SceneRender4.409→4.135ms、GPU private graph1.377→1.308ms。inclusive／worker totals不能加成單一幀成本；未降低高模／物理／live controls。資料`Saved/Crowd/Profile-DryPair6msStableBoundsEvents-Root.json`。

### 最新完整功能驗收PASS

StableBoundsPairPaint正常exit0／audit_completed1／functional1／四DLL hash stable；期間有其他工作cl編譯，因此整輪FPS不採用。54嘴型、voice active／取消／替換、walking兩週期loop、live眼頭／嘴型／肚子、fixed body lock、12次ready姿勢／state切換、共享clock、pause／replay、男女水槍、濕面query／anchor／pose-switch／clear、男角替換與失敗保留、離開取消均PASS。切換最大完整幀27.819ms，最大同步控制202.409ms；女92hits／36.8deposit、男210hits／33.2deposit，private geometry error女0.000005053cm／男0.000006579cm。Entry controls6.243sec、first ready motion8.361sec，當輪未強制entrybudget且entry實際<10sec。

本人檢視新版Tracking.png，雙眼正常可見、看camera控制開啟；圖片的瞬間FPS不作量測證據。後續補paired close-up eyes／入口返回、十段全部控制／水槍正式矩陣；目前其他StreetSocialFace驗收game仍執行，等待隔離後進行FPS測量。

### 全控制量測：外部程序干擾，不作 FPS 驗收

InteractionStableBoundsAllControlsVisible 正常 exit0、DLL hash stable，但外部 UnrealEditor 在08:12:25、08:14:12 UTC 啟動；external1／isolated0。乾動畫1x5.919ms、4x5.850ms，眼頭5.960、嘴型5.925、語音5.887、連續肚子6.695ms；女噴射8.427、男噴射11.960、男停射濕面11.405ms，清除後5.795ms，均僅診斷資料。男停射 actor3.504ms，其中coating2.137／geometry.733／simulation.632ms，不能把遮罩1024本身當成全部瓶頸；需all-thread trace定位CPU publication與render工作，之後隔離重測。

### 濕面 all-thread trace：保留後續根因，與乾6ms分開

InteractionWetRootStableBoundsTrace exit0／hash stable；external1發生載入期08:19:20–08:19:35 UTC，184–187秒分析窗口沒有外部程序。整輪帶trace、不作正式FPS；該窗口268完整pair frames／536role samples。Frame11.151ms，PairAdvance5.130ms含LiveShape1.023ms，Native2.263／RoleDisplay1.751ms，PaintTick3.454ms。

Coating每幀13.95 surfaces合計2.039ms：自身exclusive .567、StreamUpdate .499、AtlasUploadPrepare .410、SparseQuery .270ms，還有faces／vertices worker與dispatch。GT geometry refresh .500ms，worker captured geometry合計4.047ms不是wall time。濕面CPU streams tight bounds每幀變動又觸發CacheMeshDrawCommand約65.54 calls／.495ms worker（乾版.013ms）。這是下一個render publication與批次濕面CPU工作候選，不能以遮罩1024或性別特化例外解釋。

資料：Saved/Crowd/Profile-InteractionWetRootStableBoundsEvents-Root.json、184–187秒CPUEvents；十段控制完整診斷保留於InteractionWetRootStableBoundsTrace/report.txt。乾平均6ms的獨立無trace正式結果維持5.818209／5.811446；水槍全部120FPS尚未通過，不能套用乾結果。

### 本輪乾動畫比較與修正的最終功能驗收

DryPair6msStableBoundsFinalEyes exit0／audit_completed1／functional1／external0／isolated1／四DLL stable。本人逐張檢視PairEyesNeutral、Closed、Reopened、Playing：雙眼正確繪製、閉眼完全閉合、重新睜眼恢復、雙人播放仍顯示正常，camera gaze改變；不是用截圖stat FPS替代性能驗收。54嘴型／語音／walk兩cycles／live眼頭肚子／12 ready姿勢切換／男角替換與失敗保留通過。最大ready切換完整幀24.934ms、同步控制102.455ms。Entry controls6.013sec、首ready7.862sec；入口budget未強制但實際<10sec。

DryPair6msStableBoundsFinalNavigation正常exit0／functional1：人物編輯器與1:1互動場景分離、Load3D／載入卡入口／返回、編輯參數與互動鎖定／肚子例外、debug scope還原均通過。此次requested的Crowd差異profile→根因修正→乾<=6ms→功能複驗完成。男女水槍功能已PASS，但水槍全部120FPS仍未達標，後續以本文件濕面trace根因繼續，不宣告全控制性能完成。

### 濕面publication第一輪正式結果
InteractionWetPublicationBuildR2完整Editor Succeeded，Regression五項PASS（AffineRigidDraw／GpuStreamProxy／StreamUpload／PaintFilm／PaintScene），含contained CPU direct／batch actual GPU readback、exact CPU bounds及負scale／residual包覆。AllControlsVisible exit0/external0/isolated1/hash stable：乾1x5.762833／4x5.695155ms，eyes5.893492、mouth6.007583、voice5.835216、belly6.800156、女噴8.377964／119.330FPS、男噴9.743968／102.644FPS、男濕10.545193／94.780FPS、清除5.739195ms。男濕coating1.741282／geometry.623974／actor2.917408ms；舊tracecoating2.039、干擾run2.137僅診斷比較，不能宣稱精確A/B收益。仍未達全控制120FPS，繼續新版trace與atlas batch。

WetPublicationControlsTrace正常exit0/external0/hash stable；180–183秒281frames/562roles，Frame10.599（診斷）、PaintTick2.995、coating1.529、batchstream .206+completeStream .023、atlas .377、sparsequery .271ms。相較舊rootStreamUpdate .499，batch更新確實减少GT工作；CacheMeshDrawCommand worker .495→.397ms仍多，不能說envelope已消除全部渲染更新。PairLiveShape1.001ms、PairNative2.272ms保持，還需要後續定位。

AtlasBatch完整Editor build成功，4 paint回歸PASS（含兩個moving角色原per-surface blocks vs batch sparse全部GPUhalf-float texels精確一致）。AllControlsVisible exit0/external0/isolated1/hash stable：belly6.581991、女噴8.178220／122.236FPS、男噴9.503427／105.195FPS、男濕10.108004／98.895FPS，男濕coating1.484088ms。男女均有fresh hits/deposits，完整budget仍FAIL（女雖Present>120仍平均>8ms）。不得稱全部120FPS完成；下一輪細分nativeShape及剩餘Draw/Coating成本。

### WetDropShape全控制正式量測（仍FAILED）
1600×1000/實際男女APNG，external0/hash stable/exit0。dry1x5.582362ms/179.130211FPS、dry4x5.572843/179.543761、eyes5.779518、mouth5.854916、voice5.698553、belly6.651500/150.349141、femaleall8.357753/119.616760、maleall9.654396/103.591906、maleafterwet10.291743/97.181886、clear5.662881/176.685824。男停射actor2.740997ms/geometry.608714/simulation.565707/coating1.564455，54surfaces142235faces3916wetCharts。滴落新路徑結果exact，但尚無可證明FPS收益；未通過全互動120FPS。證據Saved/Tests/InteractionShowcase/InteractionWetDropShapeAllControlsVisible/report.txt。

ReferenceDetailedTrace：無干擾exit0/hash stable；dry75–78秒527pairs全幀5.673ms，wet181–184秒293pairs全幀10.204ms。wetShapeReferences.221836ms（前.327222），PairAdvance4.954184、PaintTick2.776918、Coating1.206966；CacheMeshDrawCommand2.328calls/.030594ms（前54.4/.402382），dry1.047/.012716。滴落串流改善render invalidation但非主要wall瓶頸，不能繼續假定skeletal dirty是下一主因。保留現有skeletal refresh，改依仍昂貴的shape與geometry capture修正。

### 使用者關閉外部工作後重驗：ShapePublication R1（2026-10-07）

最新Rust與Editor完整建置成功，actual APNG 64幀骨架/world/rest/reference/particles exact oracle及root tree oracle先通過。正式量測期间無本機其他build/tests，1600×1000、相同男女APNG、原引擎核心設定、無trace。十階段完成；exit_code=0、module_hashes_stable=1、external_unreal_observed=0、performance_evidence_isolated=1。證據：Saved/Tests/InteractionShowcase/InteractionWetShapePublicationAllControlsVisibleR1/report.txt 及 run-evidence.json。

| 階段 | 平均 ms | Present FPS |
|---|---:|---:|
| 乾動畫1x | 5.803628 | 172.305556 |
| 乾動畫4x | 5.785466 | 172.845471 |
| 眼睛／頭 | 5.938885 | 168.283185 |
| 嘴型 | 5.936015 | 168.562885 |
| 語音 | 5.851039 | 170.902268 |
| 持續肚子 | 6.628950 | 150.947900 |
| 女角射水＋控制 | 8.227856 | 121.505108 |
| 男角射水＋控制／語音 | 9.686335 | 103.255182 |
| 停射後濕面持續播放 | 10.392080 | 96.185901 |
| 清除濕面 | 6.042834 | 165.391259 |

女角Present超過120，但平均超過8ms，仍FAIL。男角及停射後亦FAIL；外部工作不是剩餘瓶頸的唯一原因。停射後GT10.267ms、GPU2.089ms，paint actor2.895408ms（geometry .627742、simulation .594080、coating1.671353，其他事件可能巢狀，不可重複加總）；54 surfaces、142235 faces、4313 wet charts、208736 chart texels、1024 shared atlas。先改善CPU濕面持續更新與paired shape/physics wall overlap；此輪無trace不能進一步斷言個別子事件是新增成本。完整功能驗收與全部120FPS目標尚未完成。

UniqueVertexAllControlsVisibleR1：exit0/external0/isolated1/hash stable，dry1x5.772241ms/173.239442FPS、4x5.680597/176.135481、eyes5.885878、mouth5.863370、voice5.771796、belly6.465905/154.657784、女噴8.121422/123.092558、男噴9.670065/103.378188、男濕10.313348/96.982137、clear5.714789/174.987968。仍FAIL完整8ms／120FPS。跨run差異不足以證明unique vertex transforms的因果收益，需same-workload A/B或trace確認，不宣稱全部達標。PairPaintR1最新binary實際APNG功能PASS、54mouth、413hits、12次切換最長22.367ms、synccontrol max104.363ms，exit0/external0/hash stable；RenderedMouth及ShadowSubmit Coverage/Mesh回歸PASS。

UniqueVertexControlsTraceR1 exit0/external0/hash stable；phase8 181.361–184.361秒取305完整pairs/610role samples，CPU全部threads/timers匯出。Frame9.801551ms、PairAdvance4.793711、PairNative2.233957、FrameWait1.929948、RoleDisplay1.725580、LiveShape.747475（body.399489／references.170528／head.135364），PaintTick2.577046、Coating1.140833（exclusive.416377）、SparseQuery.250013、StreamBatch.169377。ParallelFor每frame46.61次／inclusive1.950902／exclusive.910298ms；exclusive亦包含未命名lambda工作，不可直接當成純派工開銷。下一輪ConsoleCommands=ok.Paint.CoatingParallel 0做正式相同十階段A/B，先確認小批次並行是否有收益，再決定門檻；不直接推論全部ParallelFor是瓶頸。worker CPU sums不可當wall相加。完整數據：Saved/Crowd/Profile-InteractionWetUniqueVertexWetEventsR1-Root.json。

同trace phase5 141.559–144.559秒473完整pairs/946roles：持續肚子但未射水Frame6.319949ms、PairAdvance4.412548、PairNative2.026154、RoleDisplay1.617746、Shape.680013。對照wet Frame增加3.481602，PaintTick2.577046加PairAdvance增量.381163解釋約85%增量；RT SceneRender3.835076→3.859764近乎相同，不能把wet slowdown歸因GPU三角形或RT scene本身。證據Profile-InteractionWetUniqueVertexBellyEventsR1-Root.json。

CoatingSerialAllControlsVisibleR2（修復ConsoleCommands delimiter後）exit0/external0/hash stable：女噴8.382691ms/119.294487、男噴9.680894/103.260697、男濕10.288588/97.153718。沒有證據支持改成serial，保持parallel預設1。

IncrementalWorldAllControlsVisibleR1完整build及八項UE回歸PASS，pose17項/Ver02/actual男女64幀oracle PASS。正式測試exit0/external0/isolated1/hash stable：乾1x5.673944ms/176.244016、4x5.683627/175.948903、eyes5.819586、mouth5.908142、voice5.835729、belly6.505829/153.709811、女噴8.280550/120.723721、男噴9.801643/102.036506、男濕10.194791/98.037021、clear5.754306/173.773715。仍FAIL全部8ms/120FPS；保留bit-exact generic publication及reference scratch，不能把不同run的小差異當性能因果證明。最新binary眼睛closeups／PairPaint／導航驗收繼續。

2026-10-07 GPU coating batch正式隔離結果：InteractionGpuCoatingBatchAllControlsVisibleR1 exit0/external0/isolated1，四DLL hashes不變，十階段完成。乾6.280296ms/159.336370FPS、4x6.360822/157.325696、camera6.523900/153.299450、mouth6.558404/152.458095、voice6.713745/149.051694、belly7.254046/137.818433、女噴9.143544/109.390689、男噴10.452025/95.690305、男停射濕面10.748009/92.944230、clear6.885491/145.345554。男濕GPU coating requested22664/published22651/hardreject0。當前乾<=6ms與全部120FPS均未通過。

CPU OracleVisibleR1為前一binary的GPU0基準，男停射11.845771ms；它與batch結果只能作診斷比較，不能當完全同binary A/B。最初非batch GPU1 AllControlsVisibleR1 external1，已撤回性能收益結論。

新的BatchControlsTraceR1 external0/hash stable，濕191.884144–194.884144共266完整pairedframes；trace wall Frame11.260711、PairAdvance5.429677（Native2.562098、FrameWait2.223722）、RoleDisplay1.942、LiveShape .82、NativeValidate .26、Contacts .51、PaintTick2.826643、Geometry .64、PaintStep .74、Coating .90（exclusive .56）、SparsePrivateQuery .02、GPU coating RT graph .19ms/frame。乾85.7827793–88.7827793共457frames，PairAdvance4.245544、Native2.326003、RoleDisplay1.824626。RenderScene約3calls/frame包含兩個必要ToonShadowCapture，不能任意關掉。Worker inclusive sums與ParallelFor lambda時間不得當成GT壁鐘或純排程成本。trace本身額外負載，正式FPS仍以上一段無trace結果為準。

## FrozenTransform同binary正式重驗與trace
CpuOracleVisibleR1／GpuAllControlsVisibleR1：exit0/external0/isolated1，前後與兩run的4DLL SHA256完全相同。CPU0乾6.395026ms、4x6.583707、女噴9.399699、男噴11.297596、男濕10.888418、clear9.138070。GPU1乾6.528177、4x6.340613、女噴9.356963、男噴10.962800、男濕10.906378、clear6.323396。乾<=6ms與全8ms/120FPS仍FAIL。
男濕CPU/GPU coating scope分別1.207527/.983899ms，但GPU charts3658/178912texels、CPU2598/124848，workload不同，不能斷言完整FPS收益。GPU request21898/published21885/hardreject0。預設GPU coating仍0。
FrozenTransformGpuControlsTraceR1已完整完成／external0/isolated1/hash stable。新all-thread event export含Dry499pairs998roles、Wet297pairs594roles、Clear490pairs980roles。乾PairAdvance3.99792／NativeContacts.50729／PrivateGpuPaletteBatch.42631ms；濕PairAdvance5.15116／LiveShape.77139／PaintTick2.27651／Coating.72357／SparseQuery.02457／NativeContacts.55996／PaletteBatch.41246；clear PairAdvance4.06606／PaintTick.00132。clear昂貴異常未在GPUtrace重現，不能歸因CPU塗層。新trace只是定位，不能取代formal無tracePresent驗收。資料Saved/Crowd/Profile-FrozenControls{Dry,Wet,Clear}R1-Root.json。
FrozenTransform位元oracle、actual APNG64幀／FinalIK九項／八項UE回歸（含54 mouths／2430steps）已PASS；其Contacts整scope沒有已證實收益。HeadInput與SharedBodyIndex候選未證實根因，已撤回，錯誤記錄SHOWCASE_ERRORS。下一步新增NativeContacts與Palette細分trace，再定位可消除的當幀重複工作，不減少physics／IK／wetmask／render品質。
