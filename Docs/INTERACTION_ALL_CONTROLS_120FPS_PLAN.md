# 一對一所有互動120FPS：根因與實作計畫

## 範圍與門檻

使用者要求一對一場景所有互動達120FPS，參考CrowdRoomDevelopmentAPNGGPU.cmd。高模、APNG runtime讀檔、雙人原生姿勢／物理、眼睛／頭向、全部嘴型／語音、肚子大小及水槍保留。其他body／face shape只在人物編輯器可改，原加載3D入口保留。入口10秒優化仍按既有要求延期，切換／慢幀300ms與平均<=8ms、實際Present>120FPS須分別記錄。

## 已確認的參考路徑

CMD呼叫Tools/run_crowd_runtime_apng_development.ps1，再啟動R1147.stage packaged Development。Runtime場景使用runtime生成的常駐GPU動畫bank、共享pose/palette、批次更新與frame cache，背景準備及4MiB分批上傳。資料來源仍是APNG，不用每張卡特化的預烘焙資源。

OKCrowdRuntimeScene.cpp將播放時間量化到60Hz，嘴型／眨眼也以60Hz bucket及變更判斷更新；它沒有逐幀原生雙人constraints／IK及水槍CPU碰撞／流動／coating。100人數字不是一對一同等工作量或相同fps證據，不能用其60Hz取樣替代一對一120Hz控制，不能減少男角部件或裁掉高模取得通過。

## 執行步驟

1. 新版binary先做獨立trace定位7段雙人水槍（女基準／噴射／停射、男基準／噴射／停射、清空），分GT wall time、worker CPU與Render/GPU；舊1024結果僅為前版對照。
2. 增加逐項互動矩陣：雙人1x／4x速度、眼頭控制、嘴型切換與開合、真語音、持續肚子調整、男女噴射與停射、控制並行噴射。記錄同視窗1600×1000下完整幀分佈與Present，不用functional audit平均代替性能。
3. 依trace先消除CPU全網格回讀／skin、重複幾何與pose準備、coating每幀重建／buffer copy，再處理肚子重算／表情與語音更新。runtime immutable資料與可變參數分離，借用crowd常駐／批次／按需策略；支援精確CPU oracle/fallback。
4. 水槍只重算真正在碰撞或液體流動／渲染中消費的資料；当前pose精度、接縫質量、20Hz surface與120Hz彈道不降低，不刪除backlog。塗層優先GPU附著、或精確稀疏CPU頂點更新，不能停留在舊pose。
5. 非變更參數不重建資源，表情／肚子更新有界；原生animation／物理結果保留。先量測各項，再選擇通用實作，不能按性別／卡名寫例外。
6. 每次改動建置、相關數學／幾何／GPU顯示／導航回歸，真實APNG男女功能與圖像比對。失敗原始資料保留並追加INTERACTION_SHOWCASE_ERRORS.md。
7. 新版獨立Visible無trace驗收全部矩陣；必要時重取trace確認瓶頸搬移。四DLL各run前後及目前檔案核對，external UE（含packaged）干擾時不當效能證據。
8. 更新本MD及完整驗收結果，明確列每項是否達標；未達標不宣稱完成，也不以安靜播放代替控制／噴射。

## 本輪已實作與驗證

- 已加入十段全控制量測：安靜1x／4x、眼头跟鏡頭、54嘴型輪換／開合、語音、逐幀肚子、女性並行噴射、男性語音／並行噴射、男性停射濕面、清空。每段3秒暖機、10秒獨立量測；Present／完整幀分佈與各子系統成本分開。
- 已將private source revision判斷改為常數時間所有權判斷，保留真正publication的所有有限值／layout驗證；CPU private查詢分區段更新、37部件批次並行，保持動畫／particle history不變。
- 已常駐塗層page streams／UV／索引／顏色並只更新可變頂點，已實作稀疏dirty chart上傳（最多256 regions，CPU mirror保留其他部件chart）。
- 已常駐shape共用分類表、按骨名批次寫shape、復用骨架snapshot並只重建live breast需要的references；新舊worlds／seams／history oracle已通過。
- 已常駐當前private palette副本，塗層每幀僅計算消費頂點；20Hz碰撞與120Hz彈道不變。後續碰撞直接從同一source/palette計算世界位置，不產生完整native法線／切線、不回讀GPU；不支援及尚未準備部件保留原native fallback，ok.Paint.ResidentGeometry 0提供獨立oracle。
- AllControls120SparsePoseRegression及AllControls120ResidentCollisionRegression各5/5通過，包含兩張實際男女APNG的GPU／原生位置（.001cm sparse位置門檻）、濕面物理atlas／CPU-render streams、controls及54嘴型。
- AllControls120SparsePoseFunctional完整功能通過：54嘴型、語音、肚子／頭、walking兩次循環、男角載入／替換／失敗保留、12次ready雙人切換。切換click max18.584ms、frame max37.815ms。該功能run背景prepare最大1128.508ms仍保留；功能平均不能作120fps性能證據。

## 獨立無trace矩陣對照（1600×1000、同APNG）

| 控制 | 原始 FPS / ms | 批次查詢版 FPS / ms |
|---|---:|---:|
| 安靜1x |121.02 / 8.262|120.69 / 8.286|
|4x速度|124.59 / 8.027|124.79 / 8.013|
|眼／頭|121.65 / 8.220|120.56 / 8.294|
|嘴型|120.40 / 8.306|121.19 / 8.252|
|語音|123.25 / 8.114|123.19 / 8.118|
|持續肚子|106.50 / 9.390|110.61 / 9.041|
|女性全部控制＋噴射|77.14 / 12.956|86.59 / 11.551|
|男性語音／全部控制＋噴射|64.60 / 15.468|75.90 / 13.169|
|男性停射濕面＋全部控制|58.99 / 16.961|71.20 / 14.037|
|清空|123.35 / 8.107|122.65 / 8.153|

資料：Saved/Tests/InteractionShowcase/AllControls120BaselineMatrix及AllControls120BatchQueryMatrix/report.txt。兩輪外部UE=0、四DLL前後hash一致；批次查詢版明確未與編譯／native tests／trace export重疊。所有互動120FPS和平均<=8ms尚未通過；不得宣稱已完成。ResidentGeometry新版尚須完整paint oracle及獨立矩陣。

## 使用者更新優先級：乾雙人動畫平均<=6ms

先停止把噴射並行矩陣作為主要優化迴圈。乾雙人動畫必須先平均<=6ms，為水槍留約2ms餘裕。保持高模、120Hz原生雙人／物理與全部控制，不用60Hz cache或降畫質取得目標。先取新版乾動畫獨立trace，按GT wall critical path拆出原生求解／display／face／GPU提交／UE tick，針對最主要項目實作；回歸後獨立visible無trace量測，6ms未達標不宣稱完成。

ResidentCollisionPairPaint新版功能PASS：女92新命中、男220新命中、各36.8沉積，private位置vs原生獨立query最大0.000006688cm，男女濕面隨姿勢、清空、替換均通過。此為功能證據，並非FPS驗收。

### 乾動畫最新實作／獨立量測
新增有界、thread-owned動畫SkeletonSnapshot池，保留TF HashMap順序與重複骨名語義；層動畫base無必要時不複製。原生skin stages借用immutable bind poses，共用資源依manifest revision查IK索引，精確leader follower每次pose publication共用完整骨palette邊界計算。
- DryPair6msResidencyBuild編譯成功；Animator原生41項與snapshot1項PASS。
- 實際APNG native staged oracle：111 samples／133440 vertices／37 parts，最大誤差0。
- DryPair6msResidencyRegression：ExactLeaderPose、SharedAffinePose、LiveAffinePalette、兩張APNG PrivateGpuSource共4/4 PASS。
- DryPair6msResidencyVisible獨立1600x1000、無trace、無水槍：1x平均7.811960ms／128.008FPS；另一姿勢1x 7.796593ms／128.265FPS；最大幀16.333ms。四DLL hash穩定、external UE=0、isolated=1。
- 平均6ms尚未達標。verbose階段8.778ms含log輸出，不作正式性能驗收。繼續trace確認剩餘原生／display與框架成本，水槍性能量測延後。
- LayerMask新版：42 native動畫測試PASS，拓撲／遮罩invalidate測試PASS；trace乾動畫1x 7.738ms／129.22FPS，另一姿勢1x7.714ms／129.64FPS。原生取樣CPU合計1.575ms（雙角色，非wall）：reset .197、curves1.130、modifiers .240；剩餘GTprivate GPU .857ms，原生物理等待1.217ms。6ms仍未達標。新增跨角色GPU publication scope：disjoint合併、重複target／nested／direct request按原順序flush，並每component合併bounds更新；正在獨立回歸。
- GpuFrameRegression四項PASS（GPU proxy/actual男女GPU/native/bounds/mouth）。新IK scoped names測試2項PASS；actual APNG 64幀骨架及private粒子oracle完全一致。GpuFrameIkVisible獨立無trace：1x7.590705ms／131.669FPS，另一姿勢1x7.665415ms／130.459FPS；render約6.71ms，GT7.59ms，GPU2.23ms。6ms仍未達標，提交合併收益不足，重新trace定位。

## 使用者要求：重新profile Crowd CMD 與雙人乾動畫根因差異
正式更正：前述dry phase1數字是另一個ready雙人姿勢的1x，不是4x；只有十段controls矩陣phase1是真正4x。既有R1151 Crowd GPU mode平均完整幀4.461ms、GT1.608ms、Render4.458ms、hardware GPU3.396ms（不能把GPU mode數字當GPU timer）。
先驗證目前PaletteBatch並凍結binary；新增profile_crowd_apng_reference.ps1，直接使用CMD相同R1147 executable／R1149 common patch／100 APNG、同1600x1000，暖機後CPU/GPU各15秒，停用diagnostic截圖／自動demo，trace和完整GT/Render/RHI/GPU分開。順序量測，禁止與compile/export/tests重疊；雙人新版同條件trace。根因比較需列runtime求解、mutable vertex/palette uploads、GPU graph、UE skeletal proxies/components、Slate/framework，以及package/editor build差異。僅對可保留高模、live controls與120Hz physics的共用資源/runtime常駐方案實作；不得用Crowd低頻pose bank掩蓋双人功能或只比GPU數字。

### Crowd 與雙人 CPU trace 根因比較（診斷資料）
同1600×1000 fresh Crowd trace完整幀5.777767ms，GT2.188320ms，Render5.767845ms，硬體GPU3.598826ms；100 APNG全部載入、GPU failures0。Trace退出遇到既有777003，不作正式成功驗收，另跑無trace。
取GPU穩定段111–114秒，587完整Frame：GameThread Frame5.095ms中等待RenderThread3.136ms，UWorldTick .449ms；Render每幀1次SceneRender4.288ms。Crowd顯示約664.7萬三角形（原始910.4萬），並非只因幾何少。
前版雙人穩定段：UWorldTick5.831ms，PairAdvance5.219ms（Native2.954ms、RoleDisplay2.180ms），GPU約2.23ms。WorldTick差5.382ms幾乎全部由雙人即時求解與mutable顯示更新解釋。Render每幀3次SceneRender（主視圖與兩個角色toon影子capture），另有每幀private GPU graph1.723ms。原生worker CPU合計與GT等待重疊，不能直接加總成完整幀。
通用修正方向：常駐不可變source／binding／layout，批次傳當前palette，保留逐幀原生物理、IK及高模。新palette batch已建置，兩張實際APNG的獨立serial palette／GPU幾何oracle以及四項UE回歸全部通過；效能尚需本輪獨立量測。對Crowd的60Hz pose bank不能直接取代雙人live求解。
資料：Saved/Crowd/Profile-CrowdPairRootReference1600Events-RootR2.json；Saved/Crowd/Profile-DryPair6msGpuFrameIkEvents-Root.json。

### PaletteBatch新版獨立trace與後續根因修正
DryPair6msPaletteBatchTrace：1600×1000，external UE=0、isolated=1。奉仕・亀頭いじり/WLoop/1x完整幀7.660792ms、130.530578FPS；奉仕・片手コキ/WLoop/1x7.810862ms、128.031633FPS。仍未達6ms。
84–87秒387完整雙人幀：PairAdvance5.032ms、Native2.873ms、RoleDisplay2.069ms；批次palette .591ms，NativeSampleWait .841ms、DynamicsWait1.266ms。原生curve取樣雙角色合計1.060ms；AccessoryStep2.897ms為worker CPU，不能和等待再相加。原生physics與display是關鍵，不是模型三角形數。Render含3次SceneRender4.583ms、PrivateGpuGraph1.630ms。
已按此trace實作直接曲線地址計畫：decoded clip與validated rig/avatar binding持有Arc ownership，thread-local最多32計畫，runtime編譯需要的channel地址，逐幀直接讀streamed/dense/constant，不建立完整HashMap或複製bindings。保留原始開始時間、controller時鐘、raw loop、dense clock、cubic／step／非有限係數fallback、constant覆蓋與骨名語義；不支援或debug走舊路徑。沒有降低取樣頻率或量化時間。
DirectChannelsUnitR3 PASS；Animator43 PASS/34 ignored；實際APNG 64幀雙人骨架／private particles精確oracle PASS；實際C001 APNG 240幀walking direct vs legacy取樣world/local TRS精確一致PASS。尚需建置、UE regression及獨立幀時間驗證。
Crowd無trace本輪正常exit0、100人、GPU failures0，mean5.075ms/GT1.980/GPU3.774，但external work=1，僅診斷不作正式驗收；monitor已補PID/name/time追查，需重跑external=0。

### 下一個原生瓶頸修正計畫：骨架 world 計算的常駐地址
AccessoryStep在trace占雙角色2.897ms worker CPU，涉及37 private rig逐幀world重建。既有WorldPlan仍每次用HashMap驗證全部parent，再對每個node HashMap查TRS。新實作將確切TF inventory順序納入拓撲signature，runtime編譯TF slot→topological node索引，每幀連續讀local TRS／按原topological順序相乘，消除兩批hash查找；所有ID、parent、bone輸出與inventory順序仍逐次驗證，rehash／增刪／重排／reparent自動重編。矩陣乘法順序保持不變，未修改physics cadence/history。正在準備驗證；效能測試期間僅編輯原始碼，未執行建置或native測試，避免污染當前DirectChannels測量。

DirectChannelsVisibleR2獨立無trace：1x完整幀7.325042ms/136.451961FPS、第二姿勢1x7.431856ms/134.628033FPS；最大19.7552ms，GT7.324ms、GPU2.211ms，四DLL stable/external work0/isolated1。新版direct sampling有實際完整幀收益，但6ms仍未達標。WorldPlan修改尚未進入此binary。為查明物理剩餘細項，新增native trace18–23拓撲／collider／forces／particles／apply／worlds，C++ spec同步24 entries。

Crowd正式R2無trace：2974樣本/100人/exit0/external0/hash stable，完整幀5.044074ms、GT1.944818、Render5.034554、RHI2.151485、GPU3.616767。比較細節見INTERACTION_CROWD_PROFILE_COMPARISON.md。DenseWorld拓撲／rehash oracle PASS、實際APNG64幀骨架/private particles完全一致PASS，時間因外部cl編譯不可採用；正在建置。

依render trace與程式碼核對，每個private GPU section先deform至48-byte transient FOutput，再第二dispatch pack成position/tangent stream。新增共用DeformPackedCS，與原兩pass共用EvaluateDeform/WritePacked math，省一個pass和scratch buffer；ok.GpuDeform.FusedPacking 0保留舊GPU oracle，缺shader亦fallback。新增FusedDeform真GPU byte oracle含signed weights、fallback、負非均勻縮放、collapsed post、Unity space、secondary offset、alias/capacity rejection。此修正尚未建置／驗收，不能宣稱效能收益。

DenseWorld分組native回歸：pose14 PASS/2 ignored、DynamicBone37 PASS/17 ignored、FinalIK10 PASS、sqlcache isolated1 PASS。全庫並行run因大素材記憶體不足aborted，不能以分組結果冒稱全庫通過；scope相關的真APNG64幀oracle亦PASS。新增fused GPU同舊兩pass的真readback byte oracle，待UE build後執行。

DenseWorldBuildR3成功（共享StreetSocial source編譯項修正完成）。DenseWorldFusedRegression七項PASS：實際男女PrivateGpuSource、GpuStreamProxy、ExactLeaderPose、RenderedMouth、VertexPack、ResidentDeform、FusedDeform；後續RuntimeGpuRegression亦PASS，共八項。FusedDeform四種模式readback位置及packed tangent byte-identical。正在新binary獨立trace，尚未有新版完整幀驗收數字。

### 細分trace後續（DenseWorldFused；診斷）
整輪loading期間有其他UE，但quiet0前約35秒已全部退出；102–105秒419完整雙人幀可作瓶頸定位，整輪FPS不作正式验收。PairAdvance4.595ms、Native2.544ms（SampleWait .592、DynamicsWait1.219）、RoleDisplay1.963ms、palette batch .569ms。Render PrivateGpuGraph1.321ms（前版1.630），SceneRender4.118ms；乾完整幀診斷7.187ms，6ms仍未達標。
原生worker細項：DynamicApply1.087ms、Particles .582、Forces .460、Topology .458、Worlds .190；其inclusive/worker sums不能直接加到GT。World索引與直接curve有收益，但原生仍主要physics，而palette亦有重複policy讀取。
已依此根因實作：AttachedDynamicSet持有child topology signature，每步驗證全部ID/order/parent，只有變更才重建counts/child lists；沒有改粒子歷史或步數。GPU palette批次只讀一次當幀AccessoryFramePolicy供所有parts共享，serial export保留當次policy，未持有全域長期設定快取。新增／補強TF reserve、增刪及reparent oracle；正在native單線程分組與男女120幀private rig oracle，完成才建置與正式量測。

### 2026-10-07：乾播放6ms持續修正與驗收

無干擾TopologyPolicyUiVisibleR2：兩姿勢1x全幀6.841537／6.899454ms，146.103／144.940FPS。平均6ms尚未通過；詳細Crowd GPU5.044ms對照與根因在INTERACTION_CROWD_PROFILE_COMPARISON.md。不能把trace／verbose段或有其他程式干擾的數字當正式FPS。

後續精確native修正已通過scope測試與實際APNG oracle：inverse direction basis、跨chain有效rotation reuse、reflection-aware lossy orientation與寫後子樹invalidate、baseline inventory zip、批次共用parent attachment、凍結peer targets後並行IK。男女120幀private/full-rebuild一致，64幀paired serial／parallel bones與粒子一致，DynamicBone37 PASS、Pose15 PASS。

Export runtime綁定表已完成且單元／最新五項UE回歸PASS：保持case/duplicate/source priority、missing值與locked identity；名稱、ID、inventory order、輸出配置變更重建，當幀讀live TRS。停用互動場景可選骨架debug複製（trace .11ms），使用UE TGuardConsoleVariable，離開立即還原原值且不覆寫較高console優先序；editor GIsEditor維持除錯資料。新增實際entry／return guard還原驗收。

完整PairPaint出現audit設計錯誤：Stage60第二角色全域ClearPaint清掉第一角色，最後卻檢查兩角色都有wet registry。診斷重現確認clear而無topology invalidation。改第一角色global clear、第二角色ClearPaintForActor，以保留兩角色濕面並完整驗收pose switch。不得把舊失敗run當完整PASS。下一步建置最新navigation驗收、修正後完整PairPaint、獨立無trace乾播放。乾平均<=6ms之後才做全部互動／水槍120FPS矩陣。

### 乾6ms階段已達標；接續新版全控制矩陣

2026-10-07 StableBoundsVisible：1600×1000、高模雙角色APNG乾動畫，兩姿勢平均5.818209／5.811446ms，171.875／172.010FPS，最大完整幀12.063／8.796ms；external0、四DLL hash stable、正常exit0。真正有效的通用修正包含step內精確world-cache invalidation reuse、runtime export binding、融合GPU變形與packing、GPU保守bounds union未變時避免重複UpdateBounds／render transform dirty。沒有減少頂點、物理頻率或live控制。完整Editor build及最新五項UE回歸PASS；外層worker transaction／16 workers無收益，撤回／保持8。

乾6ms達標不等於全控制水槍120FPS。下一步收尾新trace、完整PairPaint與眼睛／嘴型視覺確認、entry／return，再跑十段全控制／男女噴射／停射濕面獨立矩陣，依新版瓶頸繼續修正。根因與每轮完整數據集中於INTERACTION_CROWD_PROFILE_COMPARISON.md。

新版濕面trace已定位：14個表面逐幀coating合計2.039ms、CPU bounds publication造成draw command worker .495ms，持續肚子shape1.023ms。後續先處理通用stream publication／濕面批次，再獨立無trace重測；不得直接減少物理／surface刷新頻率或當幀跟隨精度。乾6ms正式結果已通過。

本輪乾動畫比較／根因修正完成：FinalEyes五組眼睛實際圖像與54嘴型語音／12姿勢切換PASS、FinalNavigation人物編輯器與互動分離及入口返回PASS，兩輪exit0/hash stable。乾正式5.818209／5.811446ms；濕面全部120FPS未達，繼續以已保存的wet all-thread trace修正。

## 2026-10-07：濕面 publication 根因修正（本輪）

Trace已定位男停射：14 surfaces coating2.039ms，其中stream update .499ms、atlas .410ms、sparse query .270ms；CPU動態tight bounds導致draw command cache .495ms worker；live belly另1.023ms。先修正publication，再量測決定後續，不降低任何演算頻率。

1. 動態CPU section仍保存每幀精確tight bounds、全部vertices／normals／tangents。另提供collision-free animated component通用保守render envelope；只有envelope真正擴張才更新primitive transform。預設原精確bounds路徑，濕面開啟此模式；任何新pose／scale／topology必須被CalcBounds包住，不能少畫／改頂點。
2. 濕面topology／UV／colors未變時只提交mutable streams，將同幀多surface更新收集為一批；重建或UV變化保持原完整提交與fallback。CPU與RT輸出精確比對，stock mesh oracle保留；不能跨frame延遲。
3. 全Editor build；stream GPU／CPU readback、bounds contained／expand／scale、paint stock-vs-stream每個頂點及GPU atlas texel、conservation／anchor回歸；再以男女實際APNG完整PairPaint驗收。
4. 同1600×1000，無trace全控制10段／男女水槍，檢查整輪external work與DLL hash；有干擾的結果只診斷。新trace判斷draw command／publication改善，未達8ms／120FPS則依剩餘根因繼續。

候選無收益或錯誤均記錄INTERACTION_SHOWCASE_ERRORS.md；不得稱全功能120FPS已完成。

第二候選：同一Tick收集每個surface的dirty chart immutable描述（獨占atlas區域、profile pointer、原始Scale／Flow），blocking並行填寫互不重疊texels，再合併同page uploadRects。新頁CPU arrays的data allocation在本Tick不變，所有maps/allocations/UObjects仍GT；完整每texel數學順序不變。多角色移動、原serial block／新batch sparse的GPU half-float texel oracle必須exact。

為區分持續肚子1ms細項，新增native ShapeRestore／Body／Head／FullHead／References五個trace事件及CPU conservative bounds真擴張事件。只增加觀測，不改shape演算法；完整trace規格陣列同步30，之後先看根因再修改形狀更新。當前atlas正式FPS期間只編輯source，沒有build／native tests。

DetailedTrace 181–184秒293pairs/586roles：ShapeBody.487／Head.157／References.327ms；atlas prepare.078+fill.028+copy.047已改善。CPU envelope只有.64次/frame擴張/.00114ms，draw command仍54.4calls/.402ms。下一輪附著滴落換collision-free mutable stream，原6頂點8面、UV/normal/material及物理位置／生命週期不變，固定近端LWC anchor+source conservative envelope，實際頂點bounds仍fallback包覆，遠距離teleport重新anchor。舊stock PMC oracle保留。後續再依trace修正deferred head重做／reference scratch，真APNG oracle先驗證。

DropStreamsRegressionR2：StreamUpload及Paint.Scene PASS；22+23個真實附著滴落世界座標誤差0，完整GPU mutable streams、normal/UV/color/tangent/bounds與兩角色multi-surface half-float atlas exact。DeferredShapePairOracleR4：兩侧相同snapshot初始化、相同production frozen-contact transaction，兩姿勢64幀骨架／身體與私有粒子exact PASS。下一步44身體shape通用回歸、完整Editor建置，無trace全控制矩陣；尚未宣告全部120FPS。

References .327ms根因下一候選：breast reference每chain clone完整Skeleton，world查詢亦反覆由root重算。只在同一次refresh內重用reference skeleton，逐chain還原上一chain被覆寫的target TRS；immutable source world cache跨chain共用，reference world cache在全部reference覆寫後建立且每chain清空。保留完整fresh-clone逐查oracle，真APNG64幀／44shape及Ver02全重建history exact後才建置。當前FPS量測期間僅編輯source與MD，不執行build／tests。

ReferencePairOracleR1／Ver02OracleR1 PASS：actual男女APNG64幀完整骨架和私有粒子exact；Ver02三種shape因子各12幀，完整重建reference與same-refresh cache結果及粒子history exact。測試耗時只當功能證據，不能當FPS量測。正在完整建置，下一步trace比較dry與wet render dirty來源。

後續具體計畫：1. 移除deferred head skip不必要的head_skel存在條件：完整head baseline存在時，下一Animator reset必定重建，前一frame合併head後head_skel=None仍可以省略被覆寫的准备；same-face/parent-scale及完整baseline條件保留，64幀oracle保持exact。2. 濕面geometry跨surface收集immutable prepared buffers，分頂點block並行skin，再分face block更新triangle/bounds，最後同surface原reverse-order BVH union；不降低20Hz、不跳過任一vertex/triangle。舊逐surface路徑由CVar保留，CPU positions/faces/BVH、GPU coating/atlas exact驗證後才驗收FPS。

GeometryJobsBuildR2完整建置PASS，五項RegressionR1 PASS；forced jobs vs逐surface原路徑在3900vertices／6908triangles exact（兩個當幀），45attached drops/world及GPUatlas維持exact。GeometryJobsPairPaintR1實際男女APNG完整功能PASS/exit0/external0/hash stable；12姿勢最大同步切換41.593ms，全部同步控制最大107.815ms。全控制FPS矩陣正在獨立量測，尚不宣告120FPS。

下一參考更新候選：live refresh只更新原refresh_breast_references實際讀取的reference/initial/local/params/gravity/end，不建立立即被丟棄的fresh particle pos/prev、previous與collider vectors。所有chain選擇保留first-valid root/kind與particle-count規則，沿用相同matrix math；rig-name index逐次驗證ID/order/name inventory。原fresh-runtime oracle保留，真男女APNG與Ver02逐frame骨架／粒子exact後才切換binary。當前FPS期間只編輯source。

DirectReferencePairOracleR1及Ver02OracleR1 PASS；新增reference欄位snapshot在每次shape prepare後與原fresh-runtime oracle exact，64幀骨架／私有粒子亦exact；Ver02完整runtime／cached fresh／direct三側reference及36physics幀exact。新版將GeometryJobs預設設0（前輪較慢），只保留受測experimental job path。runner新增可選EngineCoreLimit，同Crowd使用-corelimit與oversubscriptionratio=1；先做9核心A/B，未取得無干擾收益前不更改正常launcher預設。

形狀更新重复計算候選（Body仍約.478ms）：apply_dst只寫local後立即重建world，body shoulder補正又重建一次；改local-only寫入並重用當次allowed tree，保留head wrapper完整publish。animator root selection各候選重建children表／sort closure重算subtree，改單次children表選最大樹（相同last-tie規則與子節點order）。AssignedWeights重算rest後又重算相同world，改從相同原recursive rest結果發布world/names；原路徑test oracle保留，actual APNG逐frame rest/world/reference/particles exact後才建置。

ShapePublicationPairOracleR2 PASS：actual男女兩姿勢64幀，加入body/head shaped rest矩陣與body reference欄位exact，完整bone/world與私有particles亦exact。TreeOracleR2 PASS覆蓋equal-size root ties／duplicate names／reparent／missing IDs／負和零scale，DuplicateShapeOracleR2 PASS。使用者已關閉外部工作，接下來建置這版並重新量測；不再同時執行build/tests。

使用者要求重驗已完成：ShapePublicationRustBuildR1及EditorBuildR1成功；AllControlsVisibleR1十階段完成，exit0/external0/isolated1/hash stable。乾1x5.803628ms/172.305556FPS、4x5.785466/172.845471、肚子6.628950/150.947900、女噴8.227856/121.505108、男噴9.686335/103.255182、男停射濕面10.392080/96.185901。完整8ms/120FPS仍FAIL；此次排除了先前外部工作干擾，不能將未達標歸因於外部工作。下一項仍為CPU濕面持續更新的通用優化，再重新做全控制性能與最新binary功能驗收。完整數據與證據路徑已寫入INTERACTION_CROWD_PROFILE_COMPARISON.md。

下一輪實作計畫：先消除coating每個face重複執行同一shared vertex的world-to-local、normal rotation/scale/normalize；在既有unique vertex gather worker同幀計算一次，face只複製相同結果，tangent仍每face原公式。只快取scratch allocation，不保留跨幀pose結果，不更改mask/physics cadence。保留CoatingVertexTransforms=0原oracle，Scene回歸兩側分別0/1，完整頂點／normal／tangent／bounds與GPU buffers exact。build與回歸成功後，獨立十階段效能重驗；若無收益則還原預設，不以功能通過宣稱性能改善。之後用新trace細分剩餘coating與physics成本，最後最新binary PairPaint、眼睛、嘴型／語音與導航完整驗收。

UniqueVertexBuildR2 Succeeded，RegressionR1五項PASS（StreamUpload／GpuStreamProxy／Paint.Scene／MaskParity／Film）；Scene的stock原per-face transforms與stream unique transforms對比exact、GPU atlas texels exact，45滴落物理／世界座標／GPU資料保持一致。ShadowSubmit最小編譯修復另外做Coverage／Mesh回歸，RenderedMouth驗證同步進行。性能收益尚未量測。

CoatingSerialAllControlsVisibleR2正式隔離PASS evidence，但全效能仍FAIL；男濕10.288588ms（parallel10.313348），女噴8.382691比parallel8.121422慢，因此不改CoatingParallel預設1。新trace reference仍.170528ms，下一候選BodyDynamicRuntime保存非序列化reference skeleton scratch：每次refresh先完整restore_from當幀source（含topology/name/TRS），再原chain還原／覆寫流程；不快取跨幀矩陣或引用，不跳過reference計算。先真APNG64幀全rest/world/reference/particles exact與Ver0236幀oracle，再Rust+Editor完整build和功能回歸。全部120FPS未完成。

ReferenceScratchPairOracleR1真APNG64幀exact PASS。下一通用候選：WorldPlan原本每次rebuild全部TRS與parent matrix multiplication，改用每節點10個f32 bit pattern驗證local輸入，只有local改變或parent結果改變才執行原公式；topology/ID/order/source仍每次完整驗證，不降低任何呼叫頻率。首次／reparent／rig replacement完整重建，-0/+0 bit變化亦不得漏更新。新增recursive world_of bit-exact oracle覆蓋24幀局部修改／未修改／負scale／parent變更／name與輸出順序變更，連同原pose及真APNG particle oracle驗證後才建置。

本輪最新binary驗收完成：pose17項／Ver02／實際APNG64幀oracle PASS、完整Rust與Editor build成功、八項UE regression PASS。EyesR1五張近拍本人逐張確認正常；PairPaintR1功能PASS，413hits、12切換max21.527ms／全部同步控制104.061ms；NavigationR1功能PASS，編輯與互動分離／加載3D／返回正常。全部exit0/external0/hash stable。十階段正式性能男濕10.194791ms/98.037021仍FAIL；全部120FPS未完成，下一輪優先結構性處理CPU濕面deform/publication。完整本輪實作、驗收與剩餘限制集中記錄INTERACTION_RUNTIME_OPTIMIZATION_ACCEPTANCE_20261007.md。

## 下一輪：直接讀取角色GPU位置的通用塗層

1. 建立獨立RDG coating計算介面：輸入已變形float3位置byte buffer、runtime頂點/face-edge binding、固定塗層offset/normal及來源→overlay affine矩陣，輸出沿用FOutput與原GPU pack。來源不限定APNG角色或private骨架；ordinary skinned/未準備來源仍保留完整CPU路徑。
2. 先驗證kernel：多個當幀位置、負與非均勻scale、shear、degenerate edge、無效索引／非finite／非affine／buffer alias，CPU oracle及真GPU readback。正常RDG不讀回CPU；驗收才readback。shader可用、buffer用途／大小及dispatch限制全部檢查，失敗不得新增pass或改輸出。
3. 接入collision-free animated proxy：來源proxy epoch/section、當幀GPU位置、overlay generation/bindings與conservative bounds需驗證；先獨立跨proxy測試source更新、topology replacement、CPU fallback與拒絕transaction。
4. 接入DrawFilm：正常當幀GPU跟隨，mask/film/BVH仍原cadence，附著滴落保留當幀CPU稀疏query；bindings與chart topology在runtime產生，只在geometry/topology/normal-transform變動更新。不支援來源或shader未準備時採原結果路徑，不能隱藏人物或跳過物理。
5. build、原GPU/paint回歸、實際APNG PairPaint/eyes/mouth/navigation驗收後，無其他build/tests獨立十階段FPS與新trace。kernel功能PASS不是整合／120FPS完成；每個階段狀態及錯誤都寫MD。

GPU coating階段進度：KernelR3真GPU readback四幀129頂點PASS（位置最大1.0400565e-6cm／tangent7.78430948e-8）；跨proxy source current-frame／generation／epoch／invalid binding／replacement recovery PASS。DrawFilm接入runtime bindings與原normal cadence，stable topology幀只查附著滴落CPU頂點、其他位置由GPU跟隨；geometry/topology/transform變動保留原CPU結果，unsupported來源原路徑。預設仍0，正式收益與完整功能驗收後才啟用。

PairPaintR1 functional_pass1、408hits、54 mouths／水槍兩角色／clear／pose query PASS，但外部compiler/game存在，FPS不得引用。RecoveryRegressionR1八項PASS。GPU AllControlsVisibleR1 external1/isolated0，只作診斷；CPU OracleVisibleR1 external0/isolated1，乾6.685540ms、男噴11.394204、男濕11.845771。先前commentary收益結論撤回並記ERRORS.md。

後續批次版已實作：多個coating合併單一RDG，source更新先flush；weak components/epochs/proxy pointer在延後flush再次驗證；來源epoch改變只invalidates bindings，保留目的端generation與pendingreceipt；來源未準備Retryable8幀後恢復；runtime開關重啟強制重建bindings。BatchBuildR2成功（final source），BatchRegressionR1八項全部PASS含雙目的端same graph／scope內DestroyComponent安全拒絕。BatchAllControlsVisibleR1正式隔離量測進行中；全部120FPS仍未完成。

## 2026-10-07 清理後的效能計畫

Tests清理已完成並核對69,221個保留資源檔案。BatchAllControlsVisibleR1正式量測exit0/external0/isolated1/hash stable：乾6.280296ms、男噴10.452025ms、男停射濕面10.748009ms；全120FPS尚未完成，GPU coating正常預設仍0。

BatchControlsTraceR1僅作根因分析：乾PairAdvance4.245544ms，濕5.429677ms；濕PaintTick2.826643ms、coating .90ms，其中CPU稀疏位置查詢已降到.02ms。GPU約2.2ms，剩餘主要CPU成本。Worker時間為CPU總和，不當作牆鐘成本。

下一項實作：共享已驗證的不可變IK setup。每幀仍呼叫setup_index進行原資源revision檢查，角色sex/root/controller/state/skeleton/finite shape檢查不變；cache以Arc source snapshot identity及motion/state精確比較，持有source避免地址重用。只共享config與selected-state原資料；solver修改target/bend/shape之前仍取得自己的data clone，不能污染另一角色或下一幀。保留原setup builder作oracle，測試same snapshot重用、replacement snapshot失效、不同state、無效binding，actual APNG64幀完整骨架與私有粒子exact後才建置量測。此項不宣稱單獨可達120FPS，後續仍需處理剩餘physics/display與濕面成本。

SharedSetupUnitR1 PASS：精確snapshot/state cache identity、replacement invalidation、無效binding與錯誤、solver mutable clone隔離、eviction後oracle一致。SharedSetupPairOracleR1 actual男女APNG兩姿勢64幀完整骨架／shaped world/rest／references與私有particles exact PASS；oracle側每幀重新build setup，優化側cache，非兩側同cache。Rust+Editor完整build進行中，收益尚未驗收。

GPU coating後續小項：DrawFilm目前每幀先QueueFace全部wet faces（hash/index/mark/unique gather），GpuFast成功又清空全部讀取，最後只QueueFace attached drops。改先完成精確GpuFast/revision/epoch/receipt判斷，!GpuFast才收集全部face；成功時只收集水滴。不快取profile指標、不更改排序／mask／film／collision／normal cadence／fallback數學。新增原gather oracle CVar供Scene／actual APNG比較並細分gather trace，完整build與既有回歸再量測。

SharedSetupRustBuildR1及EditorBuildR1成功；SharedSetupRegressionR1八項PASS。SharedSetupPairPaintR1 functional_pass1/hash stable/exit0，但external1，不能引用FPS。SparseGatherBuildR1成功，SparseGatherRegressionR1八項PASS；Scene的0/1 gather設定CPU fallback全部position/normal/tangent/bounds/GPU atlas與attached drops exact。SparseGatherAllControlsVisibleR1正式矩陣進行中。

SparseGatherEyesR1 functional_pass1/exit0/hash stable，五張PairEyes近拍已逐張檢查：兩眼虹膜與睫毛可見，閉眼正常，重新睜眼／追鏡頭／動畫中眼睛正常。external1，畫面stat FPS不當性能驗收。PairPaint最新版重新驗證中；SparseGatherAllControlsVisibleR2 external1，十階段數字只診斷不採認。

本輪當前功能完成驗證：SparseGatherEyes／PairPaint／Navigation皆functional_pass1/exit0/hash stable；眼睛五張近拍本人確認。CPUOracleVisibleR1完整隔離PASS，乾6.734965ms／男濕11.906603ms（GPU0）。GPU R3在最後clear有external，R4入口就有另一遊戲及Cargo、核對receipt後只中止自己的量測。正式GPU A/B因反覆外部工作尚未取得，預設GPU coating維持0。完整資料、狀態與下一步見INTERACTION_GPU_COATING_RUNTIME_20261007.md。120FPS尚未完成，不能宣告達標。

## 下一輪：雙人接觸唯讀骨鏈查詢

根據已隔離BatchControlsTraceR1，NativeContacts約.5099ms/frame；現有frozen target/bend對每個名字分別線性查找兩次並走訪同一父骨鏈，world_rotation_of每次還配置Vec。計畫：1. 新增combined world_transform，只解析一次骨骼ID，在同一leaf-to-root迴圈保留原position與cpp quaternion乘法順序。2. frozen transaction每角色只建一次batched名字索引，所有唯讀query在插入proxy／solver修改之前完成；不跨幀保存名字或矩陣。3. world_rotation_of改同順序直接fold，去掉Vec。4. 原split-position/rotation＋原Vec oracle測試負／零／非均勻scale、missing parent、duplicate case、live edits與128深度／cycle邊界；實際男女APNG64幀骨架／references／physics exact。5. Rust+Editor build、UE回歸、功能驗收後再隔離profile，不降低IK／physics cadence。外部遊戲持續存在時先完成可獨立驗證工作，不停其他任務。

FrozenTransformUnitR1已PASS：原split getter／原Vec rotation oracle逐f32 bit一致，含132骨節、負／零／非均勻scale、missing parent／missing name／duplicate case、self-parent／cycle與live TRS edits。執行中斷後舊session已不存在，核對log確定test完成；目前跑actual APNG64幀oracle，尚未重建正式DLL。

頭部依賴候選：apply_body_shape_keys_masked回傳head_scale（AssignedWeights重映射前），head shape使用此輸入；previous_head_scale卻讀body_shaped（重映射後）。若兩者bit不同，face與實際head輸入未變仍重做head。新增非序列化runtime-only head_shape_input_scale，normal shape完成後保存真正輸入，deserialize／新session=None強制首次重建。same_face／兩個head baseline條件維持；原after-remap檢查留作test oracle。先以真APNG diagnostic確認『原檢查不同但實際輸入相同』是否發生，逐幀world/rest/reference/particles exact；未證實根因則撤回此候選，不宣告收益。

FrozenTransform當前功能完成：FinalIK九項PASS、combined getter original f32-bit oracle PASS、actual APNG64幀骨架／reference／private particles exact PASS，Rust＋Editor build成功；FrozenTransformRegressionR1八項全部PASS，RenderedMouth完整54 patterns／2430 steps／15,202,080 observations／position error0與角色隔離1。HeadInput候選actual diagnostic0，已依計畫撤回，沒有進入正式Rust staticlib/DLL。下一步同最新binary GPU0／1正式隔離量測，不能宣告120FPS。

FrozenTransform同binary正式CPU/GPU量測完成：CpuOracleVisibleR1與GpuAllControlsVisibleR1皆exit0/external0/isolated1/hash stable，兩組四DLL hashes完全相同。CPU乾6.395026ms／男噴11.297596／男停射濕面10.888418／clear9.138070；GPU乾6.528177／男噴10.962800／男濕10.906378／clear6.323396。全部120FPS仍FAIL。

男濕塗層CPU scope：CPU1.207527ms，GPU.983899ms，但GPU wet charts3658/178912texels vs CPU2598/124848texels，geometry／simulation亦不同。GPU发布21885／request21898／hardreject0。這是完整有效run，但工作量不完全相同；不能將差異全部歸因於GPU演算法，也不能宣告整體FPS改善。預設GPU0維持。下一步最新版GPU trace，分别匯出phase0／8／9，定位NativeContacts與physics/display/coating以及clear成本，不從不同workload平均推算收益。

## FrozenControlsTraceR1根因與下一實作
最新版trace isolated1／external0／hash stable。完整乾499幀、濕297幀、clear490幀，worker roles分別998／594／980。乾PairAdvance3.99792ms／NativeContacts.50729；濕PairAdvance5.15116／PaintTick2.27651／NativeContacts.55996；clear PairAdvance4.06606／PaintTick.00132。FrozenTransform功能exact，但Contact完整scope沒有可證實改善，不能宣稱降低.5ms。新trace未重現CPU正式run的clear9.138ms，該異常來源仍未定位。
下一實作：private GPU palette batch現有body_indices雖宣稱每body建立一次，卻在每個part呼叫HashMap.clone，複製所有bone names與String；此索引在整個batch唯讀，改每body一個Arc<HashMap>，part只clone Arc。不跨幀、不減少validation、不重用world／physics；body與coordinated clothes仍依同call pointer分開，原rig_name_index duplicate政策完全保留。先native私有GPU batch oracle與UE actual APNG PrivateGpuSource驗證、完整build，再隔離正式量測。這是消除確定重複配置，不預先宣稱單項可以達120FPS。
SharedBodyIndex候選撤回：rig_name_index原來已回傳Arc，沒有deep clone；錯誤已寫ERRORS。下一步先細分NativeContacts（correct before／freeze／solver／correct after）及palette batch（prepare／read／publish）的跨native trace，正常未啟用trace只用現有callback空檢查；不更動算法、姿勢、cadence或資源。用最新trace定位真正成本後才下一優化，避免再根據函式外觀猜測。
RootScopesRustBuildR1與EditorBuildR1成功（47.91秒）。RootScopesPaletteRegressionR1三項全部PASS：PrivateGpuSource提供actual男女APNG、完整batch/serial bit-exact palettes與post、GPU readback、duplicate/stale atomically reject，另GpuCoatingProxy與RuntimeCoating PASS；exit0。新的37項native trace事件不改演算法。RootScopesGpuControlsTraceR1獨立量測進行中，無並行build/tests。
