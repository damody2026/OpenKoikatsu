# 互動展示錯誤與修正紀錄

## R64 Raw行走循環與快取取捨

RawLoopPerformanceR1正式正常循環測量：7.105／7.404／7.623／9.720／10.000／11.378ms；後三階段未達8ms／120Hz，完整性能驗收不能通過。入口4006.993ms、同步194.212ms、全幀最大50.943ms符合限制，亦不能代替120fps。RawLoopFunctionalR1確認兩周期後腿部持續變化與完整功能成功，RawLoopRegressionR1十一項通過。舊raw尾幀凍結的較快結果不具正常循環比較意義，保留此新版為後續基準，防止重複以不動的角色量測「行走」。

原生loop測試首次編譯遇到Rust let-else的if-expression必須加括號；按compiler診斷包住整個if-expression。Rust的let-else不是一般assignment，右側if-expression不可沿用C++寫法。修正後重跑測試與UE建置，未通過的中途版本不算實作成功。

行走片段未循環：Actor elapsed改為monotonic以支援多layer時鐘，但raw fallback沒有duration phase轉換，超过片段後採樣末幀。補上raw preview循環採樣，不把整個session／controller elapsed重置；驗證多周期及實際APNG腿部在兩周期後持續動作。先前功能audit只測首個周期內速度／暫停，漏掉長時間循環；此項加入流程，不以looping flag=true或elapsed持續增加當成實際動作已循環。舊性能數字也不能當修正後循環播放的性能證據。

WorldReuseOffPerformanceR1與WorldReuseOnPerformanceR2同DLL隔離A/B，最後10.435vs10.665ms，增量cache在各階段沒有收益，production撤回。嘗試依working-tree diff reverse時工作區已有其他任務新commit，該diff為空且git apply拒絕；沒有改動檔案。後續精確撤回本任務的已驗證區塊，不reset其他任務的提交或整個工作區。

## R60 增量world矩陣測試編譯

IncrementalWorldPerformanceR1無trace、無其他UE、hash一致；六階段6.330／6.752／6.809／7.988／7.872／10.605ms，最後仍失敗，亦未證明比R58有整場改善。入口6228.921ms、同步167.656ms仍符合限制。不能因逐位元正確就宣稱最佳化有效；增加原公式切換並以相同DLL的獨立A/B決定是否保留預設。功能IncrementalWorldFunctionalR1成功（入口4067.536ms、54嘴型、實際語音、82命中、返回重進），截圖尖峰1062.117ms不是live性能證據。

此次誤跑未篩選的完整原生suite，包含未標ignore的大型遊戲資源probe；並行測試占用約98GB工作集、主機commit約200GB，且出現override_lookup_tracks_zip_changes_and_owned_indexes失敗。為避免污染性能及干擾共用工作區，驗證PID、parent PID、完整exe路徑後只停止本任務的測試子程序，完整suite標示中止，不能宣稱成功。今後依改動範圍選取原生world／pose／物理純測試，資源probe另行隔離；該索引測試獨立重跑查證，未查明原因前不能歸因於world改動。

InteractionIndexR61獨立重跑同一索引測試成功（0.03秒），涵蓋zip修改／刪除／新增、owned index與immutable snapshot；不能宣稱已查明原並行失敗的根因，也不能將中止suite寫成全通過。InteractionWorldR61五項相關測試通過、兩項外部fixture測試明確ignore。

新增Rust fixture誤用C++式`.01`／`-.2`浮點literal，Rust要求整数部分，造成7個編譯錯誤；修正為`0.01`／`-0.2`等。新增跨語言測試時按目的語言寫literal，先跑對應cargo測試再建UE。另一次多檔apply_patch以不存在的MD標題作anchor而被拒絕，未修改檔案；後續使用已讀取的完整實際標題，不能提交占位anchor。

搜尋原生碼曾猜測native目錄而失敗；實際位置為Tools/ok-extract/src。跨模組調查先rg --files再符號搜尋，不能重複猜測路徑。

## Windows 搜尋

- PowerShell下將`目錄/*.rs`直接當rg路徑不會可靠展開，產生os error 123，不能解讀為檔案不存在。規則：傳實際目錄，再使用`-g '*.rs'`或`rg --files`過濾；不重複使用路徑萬用字元。

## Binding plan 對照測試

- 首輪測試解構load_clip_at回傳值時漏掉duration，E0308編譯失敗；修正為六元素。規則：新增測試先確認來源函式簽名，不以記憶中的回傳結構撰寫。失敗輪次InteractionBindingPlanR1不列為通過證據。
- 單leaf組合測試直接要求motion_plan，但未先準備clip metadata，Option為None；補上prepare_animation_clip前置條件。規則：測試需遵守正式播放的資源準備契約，失敗InteractionMotionPlanR1不是姿勢對照結果。
- R2確認普通固定時鐘walk刻意沒有MotionNode，走原始片段取樣，準備clip不會創造MotionNode；對照fixture改用同一原生walk片段建立完整權重leaf。規則：區分固定時鐘state與parameter-aware motion，不能把None當成cache未準備。R1原因判斷不足，R2亦不列通過。

## 首輪建置與驗證

- InteractionExpressionR1測試fixture使用C++慣用的`.5`，Rust要求`0.5`，在測試編譯前失敗；修正常值後重新執行。規則：跨語言撰寫fixture時遵守該語言的literal语法，編譯失敗不列入姿勢驗證結果。

- PerformanceR9與FunctionalR6同時啟動，造成本任務兩個UE測量程序重疊；R9保留作並行壓力探索資料，不列效能驗收。工具加入named mutex，序列化本任務各次audit，在退出／錯誤／取消時釋放並保存自有PID與log。規則：獨立測量不得同時啟動另一個重型驗證，即使工具呼叫表面上先後執行，其session仍可能同時運行。

- 同一32項動作頁逐項讀取解析810MB動畫包，背景準備持續約98秒並持續競爭記憶體頻寬。改為明確worker scope內重用不可變SerializedFile；最多4組來源與1GiB serialized payload，取消／完成由scope exit釋放。ConcurrentMotionR3批次2205ms、load=1、retained=810218660bytes、播放最大0.754ms；巢狀scope、來源root隔離及退出釋放測試亦通過。規則：逐项提供已完成結果仍需重用同批共同解析成本，且快速搜尋不得產生多份大型背景payload。
- GeometryR2證明13k頂點平行封包0.153ms比序列0.126ms稍慢；預設恢復序列，不宣稱加速。PerformanceR6/R7平均仍超過8ms。規則：結果一致只是正確性證據，啟用效能改動還需要真實負載收益；保留慢幀與失敗測量。

- R20將RefreshGeometry移入FSurface成員函式後，局部Section名稱遮蔽FSurface.Section，C4458建置失敗；改為CapturedSection。規則：自由函式轉為成員函式時逐一檢查所有局部名稱與類別欄位，先前Role教訓適用所有成員，不能只避免引擎欄位。

- 先前共用幾何oracle測試設定SurfaceHz=120，但正式排程上限60Hz，因此Tick(1/120)可能未執行幾何更新，實際走fallback。修正測試為60Hz與Tick(1/60)，並加入多表面移動後的序列／背景worker頂點、法線、碰撞三角形與BVH bounds逐值比較。規則：測試必須確定覆蓋欲驗證分支，不僅比較兩個相同fallback。
- 查詢來源時猜測KKPreviewAnimation.cpp與OKPaintPose.h造成不存在路徑；實際邏輯位於KKPreviewActor.cpp、OKPaintSkinning.h。規則：未知名稱先rg --files及符號查找，再讀具體檔案，不重複猜測路徑。

- 背景準備32動作同時播放的原生測試定位慢點在motion_plan；每幀source_path呼叫Windows canonicalize，與背景bundle讀取競爭。R1平均1.314ms、最大271.137ms。加入依root／descriptor識別、128項上限、不快取失敗的thread-local路徑快取後，R2平均0.271ms、最大30.619ms，17485幀通過。規則：已載入不可變資源的來源解析不得在每幀重開檔案handle；完整場景與呈現速度仍需另外測量。
- R17 module-only建置成功產生KKRuntimePose DLL，但沒有寫入該新plugin的UnrealEditor.modules；PerformanceR4仍在載入前退出。完整R18 target建置成功並生成匹配BuildId的manifest。規則：首次新增必需模組時，DLL存在不代表引擎可發現模組；需確認metadata及實際啟動，不能手改manifest規避建置。

- `PerformanceR3`第一次啟動停在Engine初始Turnkey／SDK查詢，超過4分鐘沒有World、角色或測量樣本，UE CPU幾乎不增；已確認命令列屬本輪tag後僅停止本輪PID 109420。這輪沒有可用效能結果，不能當功能失敗或達標證據。重啟需新tag，保留原始log；不終止其他任務的UE／UBT程序。
- 後續確認上述停頓為UE啟動的ValidatePlatforms子cmd等待Build.bat的共用鎖；引擎批次檔第27行鎖定自身，並非角色動畫故障。停止UE後子cmd仍持有繼承pipe，原腳本無限等待CopyToAsync EOF。已確認舊Parent PID與精確SDK命令後清理本輪SDK子cmd，脚本順利以失敗結束。工具補取消token、退出後有界drain與只清理自有SDK子cmd，並在啟動前保存hash；禁止停止其他人的建置解鎖。
- Retry同樣等待該共用鎖。查TargetPlatformManagerModule.cpp的LaunchUBTValidatePlatformsProc，正式`-Multiprocess`路徑會跳過SDK setup子程序，LaunchEngineLoop.cpp也避免清理共用shader工作目錄。本測試host改用這個引擎多程序路徑（引擎與Win64模組已建置），不改World tick／渲染／物理／測量；只適用測試host並非8ms產品修正。清理Retry的已驗證自有UE及SDK cmd後以新tag重測。
- PerformanceR3Host使用多程序路徑成功越過SDK等待，但共享工作區新加入的KKRuntimePose plugin尚無Editor DLL，Engine在載入場景前結束（exit1）。不修改或停用另一工作的plugin設定；R17將其納入必需模組建置，後續重新驗證啟動。此輪仍無效能樣本。

- R12互動相關模組編譯／連結完成，但完整target因另一個既有UE程序占用引擎NetCore DLL而LNK1104。不終止不屬本任務的程序。查UBT TargetDescriptor.cs確認`-Module=`支援`+`清單，後續針對OpenKoikatsu及KKUnityBundle建置；完整target留待引擎占用解除後驗收。規則：共享工作區建置鎖與引擎檔案占用必須區分，不將其他工作的程序當成本任務可清除資源。

- 真實動作對照重跑漏設`OK_TEST_LEGACY_PORTABLE`與`OK_TEST_GAME_ROOT`，`InteractionWorldMotionR1`在fixture載入前因NotPresent失敗。規則：忽略式真實資源測試的環境變數必須在每次啟動明確設定，不依賴前一個shell狀態；此輪不是演算法不一致證據。
- 語音預載原先等全批解碼完成才提交，沒有聲線世代取消且滿128MiB後新聲線無法快取。改為優先目前選句、逐句提交、角色與世代驗證、取消以及128MiB最近使用淘汰。規則：背景執行仍需所有權、取消及有界容量政策。
- 姿勢準備原先等32項完成才開放整頁；改為每個成功項目即時開放，舊動作持续播放，未準備項目不可選。規則：背景批次不應延後已完成結果的可用性。

- 建置期间繼續修改反射header，使GENERATED_BODY位置與UHT輸出不一致；後續完整重跑UHT。規則：同一輪建置期間不修改正在編譯的檔案。
- 新增播放方法時舊物件檔未重編，出現缺少PlaySpeechPcm符號；重編實作來源後連結成功。規則：新增ABI／方法需同時確認宣告、實作及依賴重編。
- 局部變數Role遮蔽AActor成員，C4458建置失敗；改名PreviewRole。規則：避免沿用引擎常見成員名稱。
- 首次動作解析與材質準備在互動啟用後執行，造成22秒停頓；預設動作準備移到進場前。規則：其他動作仍需準備快取，非同步不等於點擊至可見小於300ms。
- 借用Gallery控制項只保留順序，丟失slot寬度與padding；保存slot template並新增返回padding回歸，RegressionR2通過。規則：移動UMG控制項須保留父項、順序與slot設定。
- Floor作為根元件時非均勻縮放影響牆位置；加入中性Scene根。地板原定位低於角色100cm；改為角色腳底。規則：展示根不帶網格縮放，世界定位依實際角色基準。
- 多方向燈同優先權產生覆蓋畫面的警告；設定主方向光優先權。規則：補光不能與主光競爭forward shading選擇。
- 水槍立即停噴截圖出現近鏡黑影；改用玩家相同槍口偏移，停噴2秒後檢視，FunctionalR3衣服／臉／頭髮及污漬正常。未逐項隔離槍口與粒子因素，不宣稱已證明材質故障。規則：數值命中通過不替代實際畫面检查。
- 含截圖的幀間隔不能當純執行成本或120fps Present證明；保留慢幀且明確標記未達門檻。規則：功能截圖與效能驗收分開，但最終互動操作自身造成的停頓不能排除。

- PerformanceR10：R24五階段平均10.210／11.758／11.430／13.295／11.982ms，最大18.808／22.954／21.942／37.072／28.883ms，仍未達8ms，不能宣稱新增名稱快取提升全幀速度。測試mutex執行成功。
- 真實水槍測量每次幾何更新對無液體退化三角形呼叫ComputeBaryCentric2D，產生404條預期退化警告。引擎此方法實際呼叫ComputeBarycentricTri並保留零結果；改用相同核心算式／門檻／零fallback，另將transport幾何改為只計算濕chart及實際走訪鄰面。需逐值與守恆回歸。規則：不可忽略反覆診斷訊息；先查原始數值行為，再保留語意修正熱路徑，禁止僅降低log等級掩蓋问题。
- 工具rg在Windows將路徑中的星號當檔名，OS123再次發生；後續使用明確目錄加-g篩選，不用路徑wildcard。

- R25建置C4834：ComputeBarycentricTri標示nodiscard，直接丟棄bool結果被當錯誤；改成明確處理false並回傳原引擎零fallback。規則：呼叫數值核心時必須處理有效性回傳，不假設只有輸出參數。DXGI Present量測來源已編譯，整體target仍需重建。

- TransportR1四項中三項通過，ConservativeTransport的退化面斷言失敗：AddDefaulted後FVector預設不初始化，測試128乾燥面並非零面。明確設P為ZeroVector後重跑。其餘逐格eager／sparse、守恆對照並未報錯。進度回報在未讀完整Result之前誤報四項通過，已更正。規則：向使用者報通過前須讀所有Test Completed及工具exit結果；數學測試所有輸入必須明確初始化。

- PowerShell純ps1正常返回不會設定LASTEXITCODE；串行TransportR2後以LASTEXITCODE判斷使PerformanceR11未啟動，後已單獨啟動成功。規則：純PowerShell腳本使用throw／ErrorActionPreference／成功狀態，只有native executable使用LASTEXITCODE；不要把測試缺少執行當作已通過。

- SourcesR1實際StreamSources與RestoreControls通過，79,219頂點全部属性／bounds一致，序列2.039198ms／平行0.933498ms。未完成項是CLI誤用OpenKoikatsu.Animation.StreamUpload，真實名稱為OpenKoikatsu.AnimationCatalog.StreamUpload；工具正確拒絕缺項，PerformanceR12未接續啟動。規則：測試完整名稱必須從IMPLEMENT宣告或實際Automation清單取得，不猜測namespace。新原生March夾具在關閉DB的回歸模式需約246秒冷載入，主程序仍有工作並正常完成，不能只憑log未更新判定死鎖。此次stack診斷因擁有程序已退出被目標驗證阻擋，未暫停任何程序。

- 既有PerformanceR1–R12為防止自動輸入被正常控制器停噴，曾停用PlayerController tick；不能替代完整互動控制器的最終效能。R29改用開發版腳本輸入進入原UpdatePaintAim路徑，保留PlayerTick、相機、UI命中及原瞄準表面查詢，報告新增player_controller_tick_preserved。規則：測試輸入應替換輸入來源，不停用產品更新流程。PerformanceR13保留未達標資料；另一個非本任務UnrealEditor PID22632於13:08:21啟動，與後半測量重疊，不能把該輪差異全歸於本次改動，也不終止其他任務程序。

- 新塗層oracle若推進既有Scene fixture角色，會改變後續錨點驗證的初始姿勢而主水槍仍持有舊幾何。執行前改成獨立Y=360角色，兩個oracle sprayer共同讀取其相同姿勢，最後清理；CoatingR1全部五項成功。規則：獨立對照夾具不能改變後續既有案例的前置狀態。

- 搜尋再次猜測不存在的OKPaintSurface.inl，實作位於OKPaintSprayer.cpp；後以目錄搜尋找到。規則：先rg --files或目錄rg取得實際路徑，再讀取，禁止憑類型名字猜檔名。

- PerformanceR16/R17時其他UE專案又啟動：PID27932於13:25:12、PID20972於13:26:00；同時另有Crowd runtime host UBT建置。R16後半及R17不能作獨立性能驗收，不終止不屬於此任務的程序。工具加入每2秒外部UE程序生命週期記錄；即使外部程序於本測試結束前退出，仍保留external_unreal_observed及performance_evidence_isolated=0，禁止標示120fps驗證。另加入KKRuntimePose已連結依賴DLL hash。規則：本任務mutex只保證本任務串行，不能代表整台機器沒有其他工作。

- ChainScratchR1測試編譯E0616：dbone子模組不可直接讀session::AccessoryRuntime私有state。補cfg(test)唯讀getter，不擴大產品權限；ChainScratchR2實際21rig／72鏈oracle成功。規則：先確認跨模組可見性，測試支援介面限cfg(test)，避免為測試公開可變運行狀態。
- 與cargo合併命令時workdir改為Tools/ok-extract，仍用Docs相對路徑導致Add-Content失敗；已以完整絕對路徑補寫。規則：跨工作目錄的證據與文件寫入一律用絕對路徑，檔案写入失敗不視為已記錄。

- R34完整target因同期新增KKRuntimePoseScheduler編譯C2280失敗：匯出class含TArray<TUniquePtr<FJob>>，MSVC具現化隱含copy assignment而UniquePtr不可複製。檢查所有呼叫皆直接建構／MakeShared，沒有複製需求；明確刪除copy constructor與assignment，保留唯一工作／reservation所有權，不撤除共用模組。互動／PreparedPalette來源當輪已編譯成功，完整重建驗證待跑。

- 搜尋再度猜測Source/OpenKoikatsu/Private/OKPaintSprayer.cpp，但本專案來源直接置於Source/OpenKoikatsu；證據檔也曾猜runtime.log／result.txt，實際工具寫run.log／report.txt。已改以rg --files或Get-ChildItem查詢；等待mutex時輸出目錄可能只有建立、尚無run.log，須Test-Path後讀取。規則仍是先確認實際路徑，避免重複無效搜尋。
- 在cargo命令workdir已是Tools/ok-extract時，rg仍傳Tools/ok-extract/src/dbone.rs而搜尋失敗；cargo本身正常啟動。跨工作目錄不只寫檔，讀取與搜尋也必須使用絕對路徑，避免同類錯誤。
- WorldInvalidationUnitR1編譯失敗：新增Rust夾具使用C++慣用的.13等小數常值，Rust要求整數部分。已改為0.13等；未執行到單元斷言，不能視為測試失敗或通過。規則：Rust小數須明確0前綴，不直接複製C++語法。
- DeferredAttachmentR1命令的cargo成功，但命令前兩項讀取又因workdir為Tools/ok-extract失敗；證據採絕對路徑補讀，今後合併cargo命令時所有檔案参数必须完整绝对路径，不再附根目錄相對讀取。
- R39編譯C3535／C2440：auto*不能從TObjectPtr推導，語音目錄callback的Gallery引用改為UI->Gallery.Get()；Printf錯誤為此型別錯誤的後續診斷。新增声线验收標記曾會被Stage5效能字串覆寫，改為追加保留。規則：auto*從UE強引用取原生指標須顯式Get；功能報告累積的驗收證據不可被後續summary覆蓋。
- 搜尋額外猜測session/clothes.rs但實作仍在session.rs；檢查回到已知實際檔案。進度文件較早的current status引用R35／R9已過時，應在每輪成功後更新頂端，而不是只在尾端追加。
- OrdinaryR1文件先填22／3而實際報告是13通過／6忽略，已立即更正；沒有測試失敗。禁止以猜測數字追加證據，必須直接讀該輪test result再填。

## APNG 入口與 native shared-pose oracle 修正

- NativeSharedPoseR1 失敗於 fixture precondition：C001 APNG 沒有 coordinated clothes runtime；不是矩陣對照失敗。不可把 SharedPoseR1 的單元通過誤稱為完整服装 fixture 通過。保留錯誤證據，使用具備該 runtime 的 fixture 才可執行此特定檢查。
- 過去互動 audit 固定 bIndependentCards=false，不能證明使用者的 APNG 路徑。新增明確 APNG 測試參數，清空 GameRoot，驗證 portable session 與10秒進場時間。
- PrepareMotions 使用原生 GameRoot 資源準備，portable catalog 若在背景讀 UObject 會回空。修正為 Game Thread 擷取共用曲線，背景只做原始資料解析；禁止角色專屬離線姿勢依賴。

## ApngFunctionalR1：首次切換材質準備停頓

實際獨立 APNG C001、GameRoot 空，進場3708.871ms成功，但首次已準備動作提交1847.167ms失敗。run.log 顯示 shader wait1638.177ms，render wait9.021ms。共用曲線準備不等於顯示材質已準備。修正：進場前呼叫現有 PreparePlaybackMaterials，材質與 render resource 成本納入10秒入口，不得推到首次即時操作；保留原始失敗 report，重新驗證。

## ApngFunctionalR2：速度檢查起點

修正材質準備後，APNG進場5974.424ms、首次切換179.286ms。速度檢查失敗：既有 AnimationReadyClock 明確丟棄選取後包含先前載入的第一個 World delta，而 audit 在 rebase 前就開始量測。修正只改測試起點：等待 Pending=false 才開始2倍速檢查，不放寬容差、不移除時鐘保護。效能模式亦先選取已準備動作才暖機，避免把靜止角色算為動畫場景。

## ApngFunctionalLargeR1：大型APNG的冷shader超時

C036 127276802 bytes、獨立Session，角色open2565ms、貼圖1333ms、顯示2429ms，但兩個材質的同步等待17654ms，總入口24368.106ms，超10秒。R43 把等待移到入口仍不能滿足大型卡片，不能以C001通過推論所有APNG。通用修正：現有材質準備增加非阻塞模式，背景編譯進場後繼續；未就緒的姿勢選單不可選，ready標記亦檢查顯示資源；其他控制保持可用。禁止縮減品質、改shader或偽報載入完成來掩蓋失敗。

## ApngPerformanceR1：平均8ms仍未全數通過

正式APNG無截圖測量，六階段平均6.940／7.338／7.223／8.467／8.732／11.283ms。最大112.160ms、首次姿勢170.442ms未超300ms，入口4128.336ms未超10秒；但後三階段平均與120Hz呈現不合格。禁止將功能通過、前三階段或原生輔助結果寫成全場120fps通過；後續只接受同樣APNG、可見RHI、無截圖與完整水槍流程作比較。

## 接續調查的檔案搜尋規則

rg 在 Windows 不能把 OKPaint*.cpp 當成 literal 絕對路徑；改用絕對目錄搭配 -g。猜測 accessory.rs 亦不存在。先 rg --files 或在 src 全目錄以符號搜尋，取得真實路徑後才讀取，禁止重複猜測檔名。

## R47 shared affine 測試建置失敗

FReferenceSkeletonModifier 的一參數範例使用 USkeleton 指標，不能直接套到 FReferenceSkeleton 值（C2665）。查閱當前UE ReferenceSkeleton.h 後，使用 FReferenceSkeletonModifier(Ref,nullptr)。新測試需先確認範例的實際變數型別與當前建構子，不依名稱推斷。

ApngTraceR1 同時觀測到另外兩個 UE 程序，性能數字不能當驗收或比較基準；只用來找成本與呼叫結構。正式APNG無截圖獨立測量仍以ApngPerformanceR1為基準。

## SharedAffineR1：synthetic ref skeleton 測試生命週期

新SharedAffinePose測試 Valid=false、矩陣最大差0。原因：FReferenceSkeletonModifier 在析構時才完成 final skeleton，測試在 modifier 仍存活時以64個Pose對照尚未完成的Ref.GetNum，因此被快取正確拒絕。修正只關閉 modifier 的建立作用域，再取 final reference hierarchy；不放寬完整骨數／拓撲驗證。其餘既有回歸項目仍通過，失敗測試不可作新最佳化成功證據。
補充搜尋規則：UE engine 的 ReferenceSkeleton 實作也不能假設同名cpp存在；在已知Engine/Private目錄以符號搜尋實際來源。

## ApngPerformanceR2：快取成本造成退步

R58新預設：ExactLeaderDefaultPerformanceR1隔離完整測量，前五階段已≤8ms且Present≥120Hz；最後污漬下連續切換平均10.317ms、Present96.915Hz仍失敗。不能因前五階段、最大118.089ms符合300ms、三卡片功能通過而宣稱全場120fps。保留最後階段為待優化項，完整驗收仍在最後。

R59功能回歸完成，不能將其當成重新性能測量：ExactLeaderFinalRegressionR1十一項與ExactLeaderFinalFunctionalR1均成功；後者使用截圖，最大1000.093ms包含截圖成本。性能仍引用R58無截圖六階段，明確保留最後階段失敗。R58最後階段GT10.225ms、GPU1.130ms、GT等待0.091ms、水槍自身1.203ms；禁止未量測就把整個超額歸因於GPU或水槍，也不得以刪除物理、降品質或降低必要更新頻率來掩蓋。

Workers4R2有完成、無重疊，但六階段7.106／7.852／7.773／8.413／8.399／12.719ms，首提交280.132ms，較原策略更慢；不將固定4 workers寫成預設通用解。選擇調度策略必須依完整場景成本，不用單一階段或CPU核心數推論。

R55編譯：SkelFollowers元素是TObjectPtr，不可用auto*直接推導range-for元素（C3535）；沿用既有明確UPoseableMeshComponent*的轉換。UE指標容器的元素型別需先讀header，不依既有使用表面樣式推斷。

ApngPerformanceWorkers4R1首次已準備動作312.139ms超300ms，audit正確拒絕，不可放寬限制；4 workers沒有完成六階段，不能稱為改善。首次選取仍包含私有物理冷初始化，改於portable静態進場時以原StaticInteractionFrame(0)初始化並發布；保留240步與粒子歷史、不硬編碼4 workers。另修正audit先記錄實際同步最大值再判斷失敗，避免失敗report寫成0ms。

ApngPerformanceR4（有trace）與R5（無trace）均記錄其他UE重疊，禁止將二者當成隔離效能驗收／A-B因果結論。R4只用來分辨PaintSurfaceMasks、NativeAnimation、Streams等成本。AffineOffR1無重疊，六階段8.046／8.028／7.486／9.347／9.079／11.237ms，仍不全數通過；它不能直接與受干擾的R5宣稱快取好壞。

ApngAffineParityR1／ApngPerformanceR3錯用工具原生卡片預設：指定-Apng卻漏傳-Card，使用March_7th普通PNG，不能當成portable APNG。entry失敗且UI等待不是affine最佳化卡住；R1提前中止、R3明確Showcase entry failed，兩者無效。工具現在在-Apng未指定卡片時直接拒絕執行，往後明確使用唯讀output/CrowdAPNG/C001.png或其他portable卡片。不可把錯誤fixture的失敗歸因於新實作。

R51編譯修正：按需WorldAt的測試呼叫誤放在Cached宣告前（C2065），移到cache已取得後的比較迴圈。插入測試程式必須確認作用域與宣告順序，不能只匹配相同for迴圈。

R49 完整 APNG 六階段平均 7.516／8.164／9.555／11.316／11.571／13.880ms，最大126.934ms、首次191.137ms；獨立程序仍不能達標，亦比R1退步。功能R4通過並不證明效能改善。原快取每個component比對全姿勢、且提前計算未使用骨骼，增加成本；改為明確pose publication作用域識別同次完整拷貝，仍驗證完整拓撲，按需計算世界矩陣。另加入實際角色每一矩陣係數的原路徑oracle，僅驗證時開啟；正式性能測量關閉。若修正仍無益，停用而不以功能通過保留預設最佳化。

R65 調查：PowerShell 上再次將 * 放入 rg 的檔案路徑造成 os error 123；改用目錄與 -g，檔案定位先用 rg --files。此為工具使用錯誤，未更動產品行為。

R65 首輪配對功能測試成功，但首次男角 SetGameAnimation 在提交時同步等待材質 4620.927ms，配對提交 4935.578ms。原因：只準備原生動畫，漏接男角的背景材質 readiness。修正：男角載入提交即啟動非阻塞材質準備；配對開始前以 generation + actor 身份驗證的 timer 等待雙方 readiness，保留目前動畫。首次控制器/片段仍需背景解析，不宣稱首次即時或120fps。

R65R2 擴充測試 Pair live head/mouth/belly controls failed：測試用 Actor.SetMouthOpen 直接改值，繞過 Gallery.OnMouthChanged 保存 SavedMouthOpen；開始一般語音時 StopHBreath 會恢復保存值。產品 UI callback 已保存此值。改測真實 UI callback 與 checkbox 狀態；失敗訊息加入 head/mouth/belly 實值，等待重新驗證，不能只放寬斷言。

R65R3 修正驗證：改用 Gallery.OnMouthChanged 真實 UI callback 後，pair_live_head_mouth_belly_controls=1，並且替換／失敗保留／取消全部通過。此失敗是測試繞過 UI 狀態保存，不以放寬判定解決。R65 材質等待已改為雙方背景 readiness gate；後續實機提交時間另記 plan，不將暖快取結果當冷啟動／120fps驗收。

R66：原流程每次選取才解析控制器與全部 blend-tree 葉片，背景等待 6～17 秒；只量 game-thread commit 不代表點選到播放 <=300ms。改成進場背景依序準備，只有雙方片段、原生層組合與 MotionIK 都就緒的姿勢／階段可選；材質也需就緒。尚未完成的姿勢不列為可即時播放，不把冷解析時間隱藏於量測外而宣稱冷資源瞬間播放。

R66：原 FramePairedPose 呼叫拍照用的 GetCPUSkinnedVertices，造成渲染同步等待，首次就緒提交 295.136ms，幾乎用盡預算。即時取景改用現有 runtime skin bounds（骨骼 envelope + active morph），以保守包圍盒角點取景；靜態精確拍照保留原頂點流程。

R66 編譯修正：新 ShowPairStatus 呼叫漏傳必要的 bOnScreen 參數（C2660）；讀宣告後補 false。另兩次猜測 Audit／RuntimeSkinBounds 檔案目錄錯誤，改以 rg --files 定位。工具定位先查檔案清單，不能憑名稱猜目錄。

R66 驗收修正：ResetPerformance 將 PreviousFrame 歸零會漏掉第一個切換幀，必須在 callback 前保留前一次 scene tick 的 monotonic 時戳，將同步切換與該幀先前的時間納入第一幀。每次切換另量完整 UI callback，取其與 click-to-play 的最大值；不以 timer 等待後的時間冒充播放提交時間。PairFunctionalR66Final 的切換最長完整幀 81.815ms、12 次 click-to-play 最大52.661ms、首次已就緒提交253.787ms，通過300ms判定。

R66 審查修正：舊 sorted MotionIK 掃描遇到任何同名損壞資料都拒絕，新索引若直接 insert 會被後面的有效同名資料覆蓋，改成保留首次錯誤，其他有效值仍沿用原覆蓋順序。新增 motion_index_preserves_errors_across_duplicate_assets 測試通過；不同 motion 不互相污染。

R67 速度量測規則：先取消目錄新準備，並以 worker completion token 等待目前 native 解碼結束，不能只看到 bPairCataloguePreparing=false 就假設無背景工作。各倍率穿插 1 倍基準，每階段先穩定1.5秒，再記錄至少4秒完整幀與雙人 Advance CPU；連續拖曳另量UI callback。不能把自然姿勢／渲染噪声稱為完全零差異，或把倍率穩定等同8ms／120fps通過。

R67 審查修正：OnPairSpeed 原先忽略 SetSpeed 的拒絕結果，無效倍率雖不改動畫速度，仍可能觸發停止／移動呼吸語音切換。改成驗證失敗立即返回，不變更音訊狀態。正常0～4倍速度仍只改同一共用時鐘；native兩方 dynamics 使用未乘速的實際 frame delta。

R67R1 實機：各0～4倍與連續拖曳的CPU／完整幀均通過相鄰1倍基準比較，實際elapsed倍率正確，但速度callback最大10.7705ms超8ms，functional_pass=0，不可只引用cpu_stable=1稱整輪通過。停止／播放切換會在StopHBreath／BeginHBreath內多次立即發布嘴型；雙人Advance也未沿用單人FaceDisplayBatch，pose／gaze／blink／speech同幀重複發布。改用既有巢狀face batch，只發布同幀最終狀態，保留全部資料更新與語音語義；等待重驗。

R68 建置紀錄：第一次完整建置遇到共用工作區正在新增的 OKStartCrowdRuntimeScene 尚無定義（LNK2019），第二次遇到同檔案 if(Installed) 下 ENQUEUE_RENDER_COMMAND 的多陳述式巨集作用域錯誤（C2065／C2923）。原工作區實作補齊並加上大括號後，InteractionBuildR68R3 完整建置通過。未寫暫時 stub、未撤回其他任務原始碼，未以失敗的連結結果驗證功能。

R68R1 圖像重現：PairEyesNeutral／Tracking／Reopened 均在 eye open=1 時缺虹膜，Closed 正常閉合；關閉追視與gaze=0也會發生，不能歸因blink或視線角度。Face batch沒有解決虹膜缺失，也沒解決10.8032ms速度callback；保持未通過。

R68R4 診斷編譯：OkMeshView 在 KKPreviewActor.h 只有前向宣告，audit需明確include ok_extract.h，補上後R68R5建置通過。不能在只見forward declaration時存取原生view欄位。

R68R2 實機在準備第3姿勢前遇到D3D12 GPU page fault／Device Removed，VRAM使用4385.97MB／budget31419MB，非耗盡記憶體。尚未執行眼睛CPU讀回診斷，亦沒有外部UE重疊；不把此次當功能／速度驗證通過。保留nv-gpudmp與run.log，先同條件重試，不換低品質模式掩蓋。

### R68R3 診斷讀回失敗
眼睛診斷誤對隱藏的 Skel 來源元件呼叫 GetCPUSkinnedVertices，MeshObject 尚未建立而觸發 assertion。診斷必須先檢查可見性與 GetMeshObject；隱藏來源只記錄狀態與 bounds，不讀回頂點。此診斷不進入正式每幀路徑。

R68R6 建置尚未結束就啟動 R68R4 audit，導致 OpenKoikatsu DLL 被測試程序鎖定、LNK1104。已核對 owned-process.json 與 command line，停止僅本次自有程序。之後必須等待建置 exit code=0，再啟動驗收；此次資料不可作驗收依據。

R68 不讀回對照：PairEyesNoReadbackR68R1 的 Neutral 畫面仍只有眼白；PairEyesSpeedR68R5 在 GetCPUSkinnedVertices 之後反而虹膜正常。UE 的讀回會切換 CPU/GPU skinning 並重建 render state，不能以此當修復成功。追查確認 UPoseableMeshComponent::RefreshBoneTransforms 沒有 RefreshFollowerComponents，共享姿勢優化跳過 follower 獨立更新後，未發布新 render data。補共用發布流程的 follower bounds/render 更新，正常路徑不允許靠讀回重建。

R68Final：五種不讀回眼睛近景通過，12次切換最大83.949ms完整幀、首次就緒提交293.876ms；速度 callback 降到0.070501ms。但0.5倍階段 CPU 19.846137ms／完整幀28.938307ms，超相鄰1倍15.961117／22.305663ms比較預算，其他倍率通過；因此此輪 functional_pass=0，不以眼睛成功宣稱速度整輪成功。保留原判定與畫質，重新量測以區分短時負載及倍率工作量。

R68FinalR2 同條件、相同三DLL重測：0.5倍CPU18.110740ms，相鄰1倍17.556179ms（約+3.16%），所有倍率比較PASS；callback最大0.051402ms。保留前輪比較失敗，不調大預算或改畫質。正常路徑不讀回的五張眼睛圖通過，9項回歸通過。診斷讀回會改渲染狀態的教訓保留，後續視覺驗收預設禁止讀回。

R69Baseline：專用 profile 在較早進入配對時，首次就緒提交683.229ms超300ms，沿用功能測試的提早結束因此尚未收集穩定播放。此數值與trace保留，不稱切換通過。Profile 改為記錄切換預算失敗但繼續收集瓶頸；正式功能測試仍保留300ms失敗判定。最終須以正常無trace模式重驗切換。

R69：在ok-extract工作目錄執行Rust測試時，附帶的Docs相對路徑落到Tools/ok-extract/Docs而寫入失敗。已回workspace根目錄補寫；跨工作目錄文件必須用絕對路徑。

R69：R2 profile 有外部 om.uproject UnrealEditor 重疊，不能當獨占FPS驗收（external-unreal.jsonl），不終止其他工作。Windows Python預設cp950無法讀含中文UTF8 CPP，後續文字讀寫明確encoding=utf-8。R2 render7.18／8.22ms有改善，但完整19.44／22.84ms仍未達標。

R69 R3首次build被共享UnrealEditor.exe的LiveCoding mutex阻止，來源是另一個om.uproject editor；確認我們只編譯OpenKoikatsu本地模組、無本專案UE程序且engine無待重建動作後，使用UBT支援的-NoHotReloadFromIDE進行本專案正常建置，不停止外部editor或更改其設定。

R69 R3 generated測試在Generated.Add(Generated[0])觸發TArray來源屬於自身容器assert；修成local copy再Add，不能在可能reallocate的Add參數傳自身元素。GPU generated讀回之前已跑到重複request步驟，仍需完整重跑，不宣稱崩潰輪通過。

R69 R5 compile發現UE5.8 RefreshMorphTargets為private，不能由actor直接呼叫；runtime skeletal renderer新增受限於KKCharAnimInstance的owned morph publication，使用相同FAnimationRuntime append/zero規則，保留每個renderer獨立weights與bounds，需完整54嘴型、眼睛與語音渲染驗收。

R69 R5Retry本專案DLL連結成功，但共享Engine的NetCore被另一om專案占用而LNK1104，overall build仍Failed；後續限制-Module=本專案模組，不能當成完整build PASS，也不停止其他專案。

R69 R6 compile：SkelFollowers元素是TObjectPtr，不能用auto*推導range-for，改為明確UPoseableMeshComponent*。HVoice快取路徑Key與既有內層Key產生shadow warning-as-error，改為CachePath。

R69 R6Final受同工作區Crowd新代碼兩項型別錯誤阻止（Track.Y uint32比較、FMatrix44f.TransformPosition需要FVector3f）；只補正這兩個型別，不改Crowd演算法或覆蓋其他工作。

R69 R6 regression啟動失敗GetLastError127；dumpbin確認OpenKoikatsu新增依賴OKAddSourceMaterialScenePass，而舊KKCrowdGpuPacking DLL缺該export。限制本專案模組build時仍需包含本地依賴KKCrowdGpuPacking，已補入並建置成功，後續也加入該DLL首尾hash驗證，避免把依賴ABI不一致誤判成場景問題。
R69 Frozen第一轮脚本因工作目錄而未編輯成功，該輪測試仍是舊實作，不能算新freeze驗收；改用絕對路徑並以FrozenR2重跑。

R69 R7正常APNG眼睛audit等候10分鐘仍未完成；沒有單人motion/readiness診斷，不能作功能證據。補motion preparation開始／完成log及60秒單人等待失敗訊息，同時修正portable states:來源應用CommonPairRoot原生prepare、不能當ImportedSharedClipBytes。R8首次APNG entry10288.763ms超10秒，保留失敗；R8Retry entry7112.393ms通過但首次單人提交334.408ms超300，保留失敗與外部UE負載紀錄，未放寬預算。R8Retry2正在完整驗收。讀取檔案先rg --files定位，rg目錄搭配-g，不傳PowerShell未展開的wildcard檔案路徑。

R69 R8Workers8 profile冷進場失敗body skeleton missing，無有效FPS結果。R7把SceneMode設在portable shared source選擇之前，SetAnimSceneMode因尚未shared而返回false且被忽略；此外未播放單人時neutral模式釋放mutable body_skel、仍保留body_shaped，pre-sample驗證誤拒絕合法lazy rig。修為先選雙方控制器，再checked SceneMode3且不發布獨立frame；validate允許完整shaped baseline、sample建立body_skel後才contact／physics，無兩者仍先拒絕。64幀oracle加入每姿勢首幀body_skel=None的真實冷初始化案例。先前R8功能成功不能代替直接冷進雙人驗收。
R69Cold oracle首輪assert假定每個APNG已保有body_shaped而失敗：部分冷來源只有body_rest，sample會正常apply_shape產生baseline。lazy驗證須允許rest或shaped，兩者皆缺才拒絕，不能以測試假定取代APNG來源契約；新ColdR2重跑。
R69最終SceneMode順序：native setter只寫Session metadata，不要求已選shared controller。FOkSession移除portable&&!bSharedAnimation的錯誤前置限制，Restart先checked SetGameSceneMode(3,false)，再選雙方控制器(false)，Advance單次完整coupled frame。保留預設單人publish=true；沒有先播放單人或角色特例。上段「選後才設定」為調查中間方案，最終不採用，因選前SceneMode參與controller分類成本。
R69FinalWorkers8第一次 entry13934.922ms超10秒，沒有穩定播放數據；external inventory記錄另一本專案runtime material bake host並行（R777）。保留原失敗，正常功能測試仍嚴格10秒。專用PairProfile改成entry超時記錄entry_within_10_seconds=0／BudgetPassed=false後繼續診斷CPU瓶頸，與既有首commit超300只記失敗但繼續profile一致；profile不能因此算功能或120FPS通過，來源非portable仍立即失敗。
R69中斷期間完成結果：PairProfileR69Final12.491／17.735ms，80.066／56.387FPS，external_unreal_observed=0；首次370.360ms超300。PairEyesSpeedR69Final在單人首提交超300提前失敗，未完成13階段速度驗收。保留報告，先前R8功能成功不等同最終版本全驗收成功。
R70 rotation測試首次Rust compile失敗：新測試誤用C++可用的*.01，Rust float literal必須0.01；已修正，首輪沒有測試結果不能標PASS。
R70旋轉memo的逐bit oracle PASS，但相同checksum微量測只有75.165～76.301ms→73.964～74.019ms（約1.5～3.1%），收益不足以支持增加parent依賴快取及記憶體。已移除新memo正式路徑與實驗測試，保留測量log作避免重複嘗試的證據。轉查粒子對所有身體collider逐一窄相碰撞的成本，採保守bounds提前排除並以原碰撞公式逐值驗證。
R70碰撞測試compile失敗：Rust科學記號也不可寫C++的1.e30，須1e30或1.0e30。之後新增Rust測試先檢查科學記號與小數literal，首輪不算測試結果。

R70臨時profile彙總以Windows預設cp950輸出損坏替代字元而UnicodeEncodeError；明確UTF-8讀取及stdout後重跑，不算實作測試失敗。worker8第二姿勢抖動使全幀15.588ms，即使Advance約8.062也不可冒充120fps；保留兩固定窗口與原報告，未改worker預設。

R70 Index首次編輯又使用與workdir不相容的相對路徑而FileNotFoundError，之後執行的cargo仍是舊code，不計新驗證；改用apply_patch絕對路徑。IndexR2編譯發現 &setup_index(game)? 不能在?運算前隱式Arc deref，改先綁定Arc再borrow；重跑R3。之後編輯固定使用絕對路徑與獨立工具呼叫，失敗編輯不接續測試。

R70 IndexR3 filter motion_ik未匹配任何測試，0 tests不算PASS；先list列出名稱後改h_playback::tests::，3 PASS／18 ignored。另一次同工具工作目錄中的相對C++讀取失败，後續所有讀取也固定workspace絕對路徑。PairEyesSpeedR70R1首次pair ready量測2075.624ms失敗，真正commit86.988ms、26次50ms shader輪詢；來源是catalogue callback忽略顯示就緒而先自動select，不放寬預算，改自動start與UI相同readiness邊界。

R70Workers8第二姿勢有大量Paint surface／contact log：可見視窗接收到實體滑鼠水槍輸入，對照混入噴漆及新surface初始化。先前「單純worker抖動」推論撤回；沒有額外pose commit但仍不是純動畫AB證據。Audit從Stage0固定ScriptedPaintAim、fire=false，水槍功能階段再明確fire=true；profile逐window記錄unexpected paint hits且非0取消budget PASS。正式場景水槍與正常滑鼠控制未關閉。另兩次rg猜檔名／PowerShellwildcard錯誤已改明確檔案或目录-g模式，沒有做任何修改。

R71policy編譯首輪發現renderer子測試也直接呼叫原skin_palette簽名，4個E0061。正式帶policy版本改獨立方法名，保留cfg(test)舊簽名wrapper讓原oracle繼續執行；之後變更private方法先rg完整src包含子模組，首輪不算PASS。Trace顯示quiet Native約4.03ms／frame；verbose native細項存在log鎖開销，不能把verbose dynamics5ms等同quiet的真實成本。

R71 R1 C++ build失敗：TAutoConsoleVariable沒有直接Set，應AsVariable()->Set；TArray<TObjectPtr<MID>>的range auto*不能直接推導，改明確UMaterialInstanceDynamic*。先查IConsoleManager.h宣告後修正，再buildR2；R1不計建置成功。PolicyCold64幀逐值PASS49.23秒，微時間含並行build壓力不作FPS證據。

R71EyesSpeedR2首提交76.017ms、12次切換8.025～21.315ms、完整最慢切換幀36.237ms；54嘴型／真語音、眼睛五圖、13速度phase與68callback max0.038ms、shadow1048576像素两角色depth maxError0全PASS。但最後male replacement失敗：延後autoStart沒有保持bPairPreparing，觀察者在shader尚未完成時以為replace已完成。新增明確bPairAutoStartPending，整段背景顯示準備保持pending，材質與曲線皆ready後才commit；取消／使用者選擇清除pending，不做重設時間。整份functional仍0，不能將前段PASS冒充完整驗收。

R72 ScopeUnits初次编译E0599：新增microbenchmark误用legacy append_particles（Particle），而真实ApplyParticles使用DParticle。改为与AttachedDynamicSet相同的gather_dparticles；生产代码未发布，失败log InteractionRustR72ScopeUnits.log保留。Rust编译曾短暂重叠功能速度阶段，此轮速度不能单独作为隔离性能结论；最终再以未编译环境复验。

R71首次normal motion缓存以section数判斷重用过于粗略：material draw常为不重叠索引，单一outline每vertex只用一次，多出来的source quaternion与读写可能抵消收益。R72改在runtime load／compact之后根据实际draw indices计数（饱和2），只预算确实被两个以上draw消费的rotation；无复用part不保存mask、不扫offset每帧。旧公式parity及正常RHI仍须重新验证。

R73NativeTrace整輪觀察外部UE PID40472（14:47:09起），FPS不算隔離證據，report isolated=0；沒有停止他人程序。補外部process exit observation，改新tag重取。另確認UE5.8 TimingExporter::MakeThreadFilterInclusive以TMap<FString,uint32>存名稱→ID，8個UnnamedThread只保留最後一個；name filter輸出不能代表全部native CPU。新增threads=*匯出與thread inventory按ID排除GPU／Frame／Verse的summary選項，保留8個同名worker。先前R73CPU檔只有worker84，不能用其合計判斷物理／skin根因；該失敗證據保留。

R73 profiling registration复核：副作用不能放在checkf表达式内（DO_CHECK=0可能省略调用）。改为先执行FFI registration、再check返回值；Development执行路径相同，原trace数值仍对应其保存hash，不覆盖数据。

R73ProfileFinal第一次整合build失败C2280在其他并行修改的KKCrowdGpuPacking/OKSourceMaterialMesh.cpp Pipeline copy assignment。复核时该文件已改为TUniquePtr＋MakeUnique copy construction，未覆盖他人修正；重新build确认。Native trace四DLL hashes在量测期间不变，因此历史根因资料保留且不受随后源码变化冒充更新。

## R74 UTF-8 原始碼讀取
Python Windows 預設 cp950 讀取含 UTF-8 BOM 的 CPP 失敗；後續明確指定 utf-8，避免部分批次修改中斷。失敗時只有計畫及 API declaration 已寫入，尚未改 CPP。

R74 第一輪編譯：部分修改其實已包含 proxy implementation；重跑文字替換產生重複函式（C2084）。已逐段檢查並移除重複，不應假設腳本中斷前未写入任何 CPP；後續以現況為準。保留 InteractionBuildR74.log。

## R74 合併 barrier 未產生收益（預設關閉）
同一 binary 正常 RHI 兩姿勢：Fused ON 全幀 10.243125/10.239121 ms，Present 97.624409/97.661975；OFF 10.050503/10.147326 ms，Present 99.496813/98.544037。兩次 external_unreal_observed=0、performance_evidence_isolated=1、paint=0，不是 120 FPS。減少 batch barrier 不代表消除頂點計算：canonical 仍須轉換、draw 又直接轉換 native；增加資料讀取且可合併 subset 受正常旋轉快取依賴限制。保留 opt-in 與 exact parity 以便回歸，預設 0，禁止以此宣稱優化成功。根因 profile 已显示工作執行而非等待占大宗，下一個實際方案須共用一次 source-indexed 轉換／法線旋轉結果給所有 draw，不能再重複轉換。

R74 opt-in parity 全功能測試失敗，保留 PairEyesSpeedR74/report.txt：4x speed phase7 CPU 14.687630 ms、full 20.633409 ms；之前 1x phase6 7.744057/10.797806，之後 1x phase8 15.531781/22.005014，後續全階段持續較慢。不能直接歸因倍速，也不能以眼睛 screenshot PASS 宣稱整體通過。先檢查外部負载與 module hashes，再以預設關閉融合／parity 的路徑重验；不放寬判定門檻。眼睛五張已逐張人工驗證正確、12切換最慢52.260ms、shadow depth oracle PASS、parity實際64來源jobs已觸發，這些個別結果保留。

R74Default 重驗 functional_pass=1：0.25/0.5/2/4倍、拖曳、暫停所有 CPU/full-frame adjacent budget 通過。Opt-in parity 失敗結果仍保留，不以重驗成功覆寫或斷言根因是 parity。Default screenshot 出現開啟 console/stat FPS overlay，表示可見 audit 過程收到實體輸入；截圖 FPS 不能當穩態 Present 證據。兩次 DLL hash 均穩定。
CommonUI 使用非 CommonGameViewportClient 的既有 log error 在 audit 起動出現；目前既有 camera/UI automation 及完整操作驗收通過，這個訊息不應誤當新增 native stream assertion。仍需在獨立 UI routing 工作處理，不任意改現有 viewport 架構。

R74 最終 code review 發現 runtime cache mode 切換邊界：ON 記錄姿勢 A，OFF 更新到 B，重新 ON 時來源恰好 A，若沿用舊 snapshot 會錯誤跳過 A 的重新提交。改為 bypass 時清除 snapshot，適用所有 snapshot eligibility／morph／cvar 切換。新增 SourceGeometrySnapshot A→bypass B→A 回歸，避免只測 steady-state。預設 OFF 不新增 per-card resource；仍保留 canonical source arrays。

## R74CacheBoundary profile 未通過：Renderer crash
2026-10-06 15:35:58 UTC，男角已載入、材質背景準備時，Foreground Worker #0 EXCEPTION_ACCESS_VIOLATION read 0xffffffffffffffff；Renderer SetLooseParameters MeshPassProcessor.cpp:134 → FMeshDrawShaderBindings::SetOnCommandList → FDrawVisibleMeshCommandsAnyThreadTask。尚未進入 quiet profile，沒有最終 FPS 證據。保留 Saved/Tests/InteractionShowcase/PairProfileR74CacheBoundary/run.log、stderr/exit/crash。13項 regression PASS 不代表完整 runtime 本輪成功；先重現／定位資源生命週期，不能直接歸因 snapshot 或放寬／關閉渲染功能。

## R74 follower 銷毀順序已定位
Default 完整 functional_pass=1 仍有 teardown ensure（15:28:58，與較早 R72 相同）：SkinnedMeshSceneProxyDesc.cpp:455，LeaderBoneMap 不等於 follower ref skeleton。Callstack CalcBounds→DetachFromComponent→DestroyAllParts→OnStopPair，BodySkel 已 SetSkinnedAssetAndUpdate(nullptr)，followers 才 DestroyComponent。修正為先銷毀所有骨架／嘴型 follower，再清空 persistent leader 的 asset。不能把 handled ensure 視為完整乾淨驗收，也不據此宣稱前述 Renderer loose parameter crash 已定位；兩者分別追蹤。

Audit launcher 原先在失敗／incomplete report 時先 throw，尚未存 module-after hashes 與 isolation 摘要，導致診斷證據不足。改為 process 結束後、任何驗收失敗 throw 之前保存 module-hashes-after.json 與 run-evidence.json；原驗收門檻及 stable hash 檢查保留。較早失败手工补存 after-failure hashes，新輪次自動保留。

R74Teardown regression 首輪測試程式错误：CreateWorld 默认已初始化，接著 InitializeNewWorld 导致 WorldSettings 重複名稱 fatal（LevelActor.cpp:586）。改用既有 StreamUpload test 的 CreateWorld(...,&InitializationValues) 單次初始化。不是 shader crash，也不是 follower 修正結果。保留 PairRegressionR74Teardown log 和 run-evidence：exit3/hash stable/isolated1；新的 launcher 已證明失敗也會保留證據。

R74TeardownR2 新增測試設定失敗：Preview actor 的 BodySkel 在 LoadSession 才建立，constructor 尚無 leader；不應假設 SpawnActor 已含完整角色。測試明確建立並註冊 runtime leader、指派 actor ownership，再附掛 follower；保留原非 null 檢查。不是功能失敗的掩蓋／放寬 assertion。

R75 第一輪編譯 C2065：替換 section loop 時沒有限定 RefreshRuntimeAccessories，誤插入 LoadSession 的另一個相似 loop。已移除誤插入，依 function 起點定位修改；Source.FirstWork/EndWork 必須是每一 part 的 exact work 範圍，避免跨 part／重复 GPU ranges。保留 InteractionBuildR75.log，之後不可用全檔第一個相似片段取代。

R75 同 binary 初次 1024-source balanced 排程：ON 全幀9.934021/9.878292ms、Present100.664264/101.228523；OFF9.404828/9.544249ms、106.331495/104.775510，兩次 isolation1、四DLL stable。ON 仍退步，不可宣稱收益。GT waiting ON0.334297/0.338345ms，OFF0.098054/0.100044ms。來源工作內混合不同 section／quaternion／compact density，較大不均勻 work 不適合 balanced batching；下一輪用有界256-source粒度與 Unbalanced 動態派工，保留1024／balanced開關做相同binary對照。不降低畫質或物理工作，也不放寬8ms目標。source conversion mode0保留原serial路徑，不偷偷改其語意。

R75Scheduling 256／Unbalanced 未達標：全幀17.253484/12.883458ms，GT wait0.165772/1.068504ms，phase1最大653.626ms；externalUE0不能證明沒有其他OS排程噪音，未確認其根因，不冒稱單純source algorithm造成所有退步。UE ParallelFor 預設可使用所有logical workers；source jobs與Render tasks共用scheduler，故下一輪明確有界participants（含GT，auto=min(physical cores,8)），用MinBatch限制task數，保留1..32調試。不得透過禁用物理／眼睛／shadow換FPS。

R75Bounded 隔離正常 RHI：全幀10.286079/10.818219ms、Present97.220070/92.436360、GT wait0.047759/0.048919ms。等待已降低但全幀沒改善，禁止把 wait 指標改善當作FPS收益；SourceIndexedStreams改回預設0。
源码复核：R72 baseline 已經每 source 只轉換一次、對真正重複 draw 的法線旋轉做共用 cache；R75 從 R74 重複 native conversion 恢復到單次轉換，不表示比實際 baseline 減少數學工作。原 draw generator／double→float／tangent packing／CPU section copy／bounds 工作仍全部存在。下一個通用架構須消除串行 CPP生成打包階段的 wall time，例如讓 native private skin 完成的 worker 同時產生 display packets（保留 CPU canonical查詢、exact math、兩階段GT prepare/publish），不再只微調批次排程。

R75Final 預設速度驗收失敗：PairEyesSpeedR75Final phase7（4倍）advance9.352392ms、frame13.209806ms，相鄰基準7.669850／10.878113，超過既有1.15×+0.5ms門檻。時鐘3.999999倍率正確，69次callback最慢0.041198ms；其餘倍率門檻PASS，但整輪audit_completed=0，不能當成完整功能PASS。externalUE0、hash stable、無ensure/fatal；無證據斷言全部為OS噪音。C++復核每幀一次ApplyPairedFrame，僅elapsed乘Speed、physics_dt仍PoseDelta、face/eye用真實Delta，沒有倍率次數的substep loop。保留原門檻與失敗資料，以相同binary複測判斷是否重現，不修改測試使其通過。

R75FinalRetry 相同四DLL binary 重新完整驗收PASS：13 speed phases 全原CPU/frame門檻PASS、69callbacks最大0.050500ms；phase7 4倍clock4.000000、CPU7.320260／frame10.442578，相鄰7.785101／11.048011，未重現失敗。externalUE0／hash stable、完整male replacement與返回PASS、無ensure/fatal；不由單次複測宣稱前次噪音根因已定位或已修復。
R76 首輪build失敗（InteractionBuildR76.log）：新增external regression誤插在Generated requests宣告之前，C2065；actor方法內Role名稱遮蔽既有member，C4458當error。等待build完全結束後，依新增測試區塊與function範圍移動／改名RoleIndex；不能只靠全檔第一個BatchMode reset定位。沒有修改驗收門檻。

R76 原設想在dynamic update強制emit private頂點會與後面的shared Skin重複工作。復核後改為skin_all_observed在原render_solved_accessories工作完成時回呼，paired FFI先完成原coupled frame，再joined兩角色Skin；C++後續同幀只消費SkinReady一次。observer不存session，canonical/CPUsection/GPUpacket在blocking call內完成；無morph/eye/sphere依賴的private parts可用，其他partfallback。這是設計修正，不能把尚未量到的時間移動稱为收益。
R76 native observer新增unit test首次編譯E0599：OkSession並未實作Clone，應使用兩次同fixture載入做舊路徑oracle，並以實際to_animation_bytes/from_animation_bytes複製路徑驗證context不保留；不能假設session有Clone trait。保留InteractionRustR76Observer.log，生產release lib build不受cfg(test)影響，仍須補native測試與最終重鏈結驗收。
R76NativeParity first APNG audit functional_pass=1，但沒有native display parity處理marker，不能算新路徑PASS。根因：APNG runtime session不等於is_shared_root；非shared native dynamics已產生private geometry，skin_all不走render_solved，因此只在render_solved掛observer漏掉主要分支。修正scoped observer同時傳至paired dynamics geometry completion；palette-only分支不emit，稍後render_solved才emit，各part一次。新增native oracle涵蓋兩個completion分支；舊完整audit僅證明功能fallback正常，不能作新路徑性能採用證據。
R76 NativeDisplay ON正常RHI firstprofile未改善：PairProfileR76Native quiet全幀18.152839/16.896371ms、Present55.085040/59.188872，Advance12.549816/11.797558；GPU約1.48ms、wait約0.002ms、externalUE0、hash stable。不可把wait下降稱收益，也不能直接由沒有外部UE推論沒有OS影響。先相同binary OFF比較，保留默认0；没有採用此性能結果。
R76Math default normalRHI：11.050756/11.177166ms、90.499310/89.452719FPS、entry9745.492ms；同binary原公式OFF：11.369962/11.391301ms、87.949073/87.781611、entry13673.216ms（10秒超標）。兩輪externalUE0/hashstable；此對照約2–3%全幀下降，但不能把兩個跨run樣本當作穩定收益或120目標完成，未查明載入波動根因。profile audit_completed1僅表示量測流程結束，entry_within_10_seconds0和120fps0仍是失敗，不能當功能驗收PASS。保留資料；另外完整functional驗收新版。

R76MathParity功能與原公式parity通過，但PairEyesPlaying.png被互動console遮蔽，不能宣稱五張均无遮挡；其餘四張眼睛已人工確認。最終新版重新驗收畫面。啟動FPS使用Engine公開SetEngineStat(FPS,true)；GameViewportClient.SetStatEnabled是private，不使用該接口，也不使用會反向關閉的toggle。

R77基準PairProfileR77Baseline completed1但quiet全幀16.593339／23.442753ms，Present60.262490／42.658660，未達標且跨phase波動大。externalUE0不能排除整台機器負載；後續比較只用相同binary開關，保留原門檻。源碼複核修正：rig_name_index已快取索引，但每次呼叫仍逐tfs驗證inventory；world_of的ancestor cache原本每renderer獨立。不能宣稱原索引完全未快取。

R77 AB build（InteractionBuildR77AB.log）失敗：OKCrowdRuntimeScene.cpp406 C2181，SpeechJob的if沒有包大括號直接UE_LOG後接else，巨集展開打斷if/else。只給錯誤log分支補大括號，不修改該場景行為，保存失敗log。UE_LOG條件分支後接else必須加大括號。

R77 same-process same-pose ABA（PairProfileR77AssignedABA）失敗：OFF11.582770ms／86.325850FPS、ON12.092381ms／82.701789FPS、OFF11.608001ms／86.137518FPS。前後OFF接近，ON沒有收益，不能將計算共用本身稱成功性能優化；AssignedWorldFrame保留opt-in且native及run script預設0。此輪entry17103.490ms與firstpair385.791ms均超標，保留未達標記錄，不修改門檻。具體串行prepare vs worker saved cost的比例未量測，不宣稱原因已完全定位。externalUE0／四DLLstable不等於整台機器負載固定。

R77 hash比較命令曾ParserError：PowerShell foreach statement後不能直接pipe。改用array subexpression收集結果後pipe；未修改任何binary或測試結果。

雙人高模GPU調查：先前假設的KKRuntimePoseableMeshComponent.h／KKRuntimePoseNative.cpp／GPUSkin.md不存在，實際類別在KKUnityBundle/Public/KKRuntimeSkinBounds.h、runtime native在KKRuntimePoseModule.cpp；搜尋須先rg --files，不猜檔名／不對PowerShell字面萬用路徑rg。Crowd先做runtime減面，不可把GPU短測5.193ms直接當雙人高模、10秒或持續120FPS證據。

高模GPU probe首輪PairHighGpuSourceProbe拒絕：Editor host缺Common runtime shader library，CrowdRoomDevelopmentAPNGGPU.cmd實際使用staged Game及共用cooked library；不能拿舊staged binary驗證新品質入口。新增明確WITH_EDITOR診斷editor_common_materials設定，僅使用既有共用材質資源；正式Game仍要求library。probe必須有兩個角色report、GPUready／matrix零mismatch／同phasecaptures與原始trianglecount，不能因exit0或空rows判PASS。保存失敗log。

高模男女GPU probe PairHighGpuSourceProbeEditor失敗：男性APNG的Koikano/main_clothes_emblem（o_emblem_jacket01_cm）不在Crowd共用材質recipe涵蓋範圍。不能略過該part或套近似shader當品質PASS；此證據支持雙人GPU skinning重用現有preview原材質，而非直接替換整個Crowd renderer。接著以已支援APNG驗證原始高模幾何與GPU基礎，不能當男女互動完成。

## Live palette 續作檢查記錄
檔案搜尋再次出現 PowerShell wildcard 路徑與猜測檔名問題：`Source/OpenKoikatsu/OKInteraction*` 不能直接交給 rg 當目錄，`run_interaction_regression.ps1` 與 public `KKRuntimeRigMetadata.h` 不存在。後續先 `rg --files` 或查 symbols；實際回歸工具為 `Tools/run_interaction_showcase.ps1 -Regression`，public render rig 介面為 `KKRuntimeRenderRig.h`。
Direct palette 不可以由弱指標有效性代表發布狀態：舊資產 GC 後 weak pointer 會失效，但舊 snapshot 仍可能存在；需獨立 HasDirectPalette 與資產 identity 檢查。同幀 engine carrier 刷新也必須重新取消 carrier 矩陣，不可只保存舊 override。本轮已補入實作與回歸，並拒絕 direct/private 與 shared leader 混接。

LivePaletteBridgeRegression：LiveAffinePalette FAIL，其餘16項 PASS。根因是 Unreal component-space transforms 有讀寫雙緩衝；只寫 editable carrier／發布 override，renderer 仍可能讀另一份 carrier。同幀 engine refresh 測試找出此錯誤。修正為成功發布後明確 finalize／flip，並在 refresh 使用即將發布的 editable carrier 重新取消矩陣；輸入驗證失敗仍不 flip、不修改 snapshot。不可將 synthetic identity carrier 第一輪通過視為完整驗收。

LivePaletteNativeAndBuffer：direct carrier／signed／math 3項 PASS，實際 APNG native adapter FAIL 於 part54 o_top_jacket02_a。嚴格 native homogeneous row==[0,0,0,1] 檢查與既有 native point/vector 蒙皮及 OKSkinPaletteToUnreal 的「只使用上方3x4」契約不同。移除未使用 row 的 exact equality 門檻，仍檢查16係數 finite，並以原轉換公式為 oracle；Unreal 輸入 palette 仍必須是 affine。增加 unused row 不改變 output 的回歸，以及實際 unused-row deviation 日誌，避免把資料表示驗證誤用為新限制。

LivePaletteFinalRegression：17/18 PASS，LiveNativePalette fixture FAIL 於 head control，native `ok_set_head_look` 要求 body rig 含 cf_j_head。通用 renderer palette 測試不應以女性 head-look 前置條件限制其他性別／private rig；移除此額外要求，只用真實 native StaticInteractionFrame 求解後發布矩陣。女性 head／eyes 功能由完整男女互動功能 audit 驗收，不與通用 matrix bridge 混為一個門檻。

InteractionLivePaletteVerifiedBuild FAIL：新增 bounds oracle 的 vertex loop signed/unsigned mismatch（本輪）與同 workspace 新增 runtime disk cache 的 TAtomic::FetchAdd／report Cache 重名編譯錯誤。修正 loop 為 uint32，兩個計數使用引擎提供 AddExchange，disk JSON Cache 限定內部 scope，保留並行工作功能。LivePaletteVerifiedRegression 在未確認 build 完成時啟動，UECPCore 模組載入失敗，沒有任何 test PASS；後續所有 audit 必須在 build session 結束且明確 Result Succeeded 後才啟動。不可只看工具 shell exit0（Build.bat stdout 已重定向）認定 build 成功。
InteractionLivePaletteRepairBuild FAIL：Cache 即使加內層 scope，UE 把 C4456 shadow warning 當作 error；改用 DiskCache 名稱（只改 disk cache JSON block），保留 texture Cache。不能用加 scope 解決引擎嚴格 shadowing 編譯規則。

LivePaletteCompleteRegression 18/18 PASS，實際 APNG static native 可查到 female6／male5個 renderer，unused native homogeneous row error1.19209e-7／0。仍不足以代表動畫時的所有 private rig。續查發現 FOkSession::InstallSharedClip 原先未設定 bSharedAnimation，portable caller 即使已成功安裝共用 bytes，ApplyAnimFrame 仍可能提前return true而未更新。修正安裝成功後標記啟用；失敗不改flag。實際 APNG probe 改成共用 walk_01 bytes、3個真實時間點，要求矩陣實際變動，避免把靜態矩陣相等當成即時動畫接入證據。

PairEyesSpeedLivePaletteAnimated：最新binary完整功能 audit 在continuous speed phase9預算失敗，functional_pass0。phase8/9/10 Advance6.970347／13.232908／12.940269ms，whole9.970352／19.179420／18.901708ms；相鄰baseline判定phase9 CPU/frame均0；callback最大0.098299ms，其餘倍率phase通過。後續paused phase11也12.484537ms，不能直接歸因於變速callback；亦不能因先前binary功能通過就忽略本輪失敗。externalUE0、4DLLstable。進場6041.018ms、firstpair71.663ms、12switch最大click34.056ms／最大frame60.670ms通過；初始混合phase最大1288.401ms，不算穩態性能通過。下一步同binary正常RHI profile與變速重測維持原門檻，保留所有失敗evidence。

PairEyesSpeedLivePaletteAnimatedRepeat同binary重測PASS：phase9 Advance7.433306ms／whole10.499345ms，相鄰baseline6.925036／9.862423ms、原CPU／frame門檻均PASS；13phase全部PASS，callback0.042800ms。首次phase9 FAIL保留，不能宣稱其根因已完全排除。profile quiet9.785／9.700ms、Present102.190／103.090，仍FAIL8ms／120FPS；混合階段最大1224.309ms也沒有刪除。不要用重測成功或stat fps單張132FPS圖代替持續性能驗收。

Palette capacity續作：搜尋再次錯把 KKMaterial* 當成 PowerShell 字面路徑交給 rg，且 repo 沒有對應檔名；後續改用既有目錄加 -g 過濾或全目錄 symbol 搜尋，實際材質建立分布於 KKForge*／KKMainItemPlus／KKPreviewActor。此錯誤未造成程式修改或測試失敗。
容量 hint 不是 source lease：同一 palette 長度不代表 geometry／weights 沒變。新讀取仍讓 native 每次求解、完整驗證後回傳本幀 source view；不缓存 borrowed pointers／矩陣，不停止 CPU skin，不把一次匯出優化宣稱為 120FPS 或完整 O(bones) 路線。
本輪續查又猜測不存在的 KKRuntimePose/Shaders 及 Source/OpenKoikatsu/OKInteractionAudit.cpp；不能由模組或編譯檔名推測實際目錄。搜尋改為先列檔案再讀，這些失敗都沒有作為實作／驗收證據。
PaletteCapacityRegression19/19及PairEyesSpeedPaletteCapacity完整功能PASS；PairProfilePaletteCapacity quiet9.892／9.842ms、Present101.094／101.611，8ms／120FPS依舊FAIL，GPU約1.37ms／GT約9.78ms，不能把palette單次匯出當作主要CPU geometry工作已搬GPU。混合phase最大1245.183ms同樣保留。3輪四DLL完全一致、external Unreal0；未量水槍，未放寬門檻。

RuntimeDeform首次build失敗：aggregate {} 不能隱式調用 FVector4f 的 explicit constructor，改為成員明確預設值；MakeShared 回傳 TSharedRef，Get() 為reference而非指標，AddEnqueueCopyPass需operator->()。未在失敗build上啟動驗收。
GpuDeformCoreFirst啟動時global shader compile FAIL：UE根參數parser不接受本shader的多個scalar同列宣告，改成每個uniform各一行。C++成功build不能取代實際shader編譯／dispatch驗收；失敗log保留。
PrivateSkinStages首次實際APNG native oracle因某part缺tangent stream而越界。native／原renderer允許缺normal或tangent，測試必須逐stream覆蓋并記錄缺stream part數；不能憑空製造與原画面不同的頂點屬性。後續GPU source installer應保留該section已有的author attributes或明確保持原renderer，沒有在此階段偷偷省略part。另一次patch context失配沒有修改檔案，已拆成精確context重試；KKExactSkinning.h實際在Public，不再猜Private路徑。
PairProfileGpuDeformCore：quiet17.157／12.967ms，Present58.299／77.108FPS，phase1最大336.226ms；GPU1.441／1.459ms、GT16.957／12.526ms。相較之前CPU側波動大，根因未確認，不能歸因尚未接入場景的compute shader或直接抹除此次結果。4DLLstable、external Unreal0；300ms全幀／8ms／120FPS都未達。功能audit中的12個ready switches仍PASS，兩類指標不可混稱。

## 2026-10-07：接近 180° 法線旋轉的 GPU 浮點相消
- `GpuDeformNearOppositeProbe` 實際 GPU readback 失敗：位置最大偏差 0.0173789304cm、法線 0.0827190339。不能只驗 exact opposite 就宣稱分支等價。
- 根因：FP32 的 `1 + dot(A,B)` 在接近反向時相消，可能跨過 UE `1e-6` 的分支門檻。
- 修正：單位向量在接近反向時使用等價的 `0.5 * dot(A+B,A+B)`；保留 UE 的門檻與反向旋轉軸選擇，不改成任意 epsilon 或硬體限定的 double shader。
- 永久加入接近反向的 source normal 測例，位置、法線與 UE FQuat oracle 比較；修正後結果另見驗收檔。

## 2026-10-07 最後功能驗收仍保留的性能失敗
PairEyesSpeedStableGpuFinal 功能 PASS，但混合階段最大幀 1273.247ms，不能說全部背景操作低於300ms；Ready switches 最大39.173ms才是本次切換證據。局部眼睛截圖 paused FPS 129～137不能代替動畫穩態Present；播放 screenshot約98.87FPS，quiet profile仍失敗。不得以6.777ms混合平均或GPU核心的readback PASS宣稱120FPS完成。

## 2026-10-07 resident GPU history 與 packed basis 邊界
- review 發現不得用跨 generation 累計 Evaluations/Installations 決定 previous slot：更換來源的首幀可能讀未初始化另一槽。改用本 generation 的 HasPrevious，並加入替換後首幀 previous=current 的永久測試。
- graph frame 的 FRDGBufferRef 不可跨 graph builder 生命周期保存；測試以 owned FRDGPooledBuffer reference 保存 previous，下一個 graph 再 RegisterExternalBuffer，不借用已釋放 graph 指標。
- UE 解包 FPackedNormal 使用 float(1/127)。整數共線不等於解包後精確共線（例如 (1,3,0) 與 (3,9,0)）；不能直接以整數 cross=0 宣告退化。新增 GPU packing 邊界測例，結果與修正另列。
- 查 UE PlatformMath 路徑時誤指 Math/GenericPlatformMath.h；實際檔案為 GenericPlatform/GenericPlatformMath.h。先 rg --files 核對檔案，不重複猜目錄。
GpuPackCollinearProbe 已實際重現 20/259 個 packed tangent handedness byte 不一致：GPU normal.W=0x7f、原 UE=0x81。修正保留一般整數 cross 快路徑，只對負 handedness 的整數共線分支比較原 float 解包之精確乘積，使用 12-bit limbs 表示 48-bit significand；不依賴 GPU double、FMA 或 epsilon，不改原 UE float(1/127) 契約。永久測例保留，原失敗 log 不刪除。

## 2026-10-07 resident GPU 核心最後驗收界線
ResidentGpuDeformFullRegression 22/22 PASS，19,907 packing vertices byte_mismatches=0，3幀129 vertices position error=0。PairEyesSpeedResidentGpuFinal 完整功能PASS，但混合最大幀1303.685ms，speed播放全幀約9.66–10.13ms；仍未達全部卡頓300ms／8ms／120FPS。新 resident output尚未接入scene proxy，不能把功能 PASS 或局部 paused FPS 135當性能收益。原 GpuPackCollinearProbe 20個mismatch FAIL log 保留。

## 2026-10-07 GPU stream proxy 綁定與 actor 隔離 review
- Build 新增直接 KKCrowdGpuPacking module dependency 後同步補 plugin descriptor dependency，避免只依賴 KKRuntimePose 的間接關係。
- review 發現 per-component epoch 可在另一個角色新建 component 時同值；改為 GT-only process-wide unique epoch，加入跨 component request 拒絕回歸。這是來源身分隔離，不能只靠每個 resident 的局部 generation=1。
- GPU publication 不更新 CPU query vertices；實際角色只有在 source lease／bounds／CPU按需查詢整合後才能停掉 native geometry。不能將此通用 scene-proxy API 冒稱全部APNG角色已切GPU。

## 2026-10-07 GPU publication與CPU接管的整合順序
原CPU publication在GPU install之後執行會正確使resident generation失效；因此角色caller不能一邊等待receipt、一邊繼續向同section排CPU uploads，否則published receipt可能在GT讀到前已被較晚CPU packet取代。下一步caller需在enqueue install後暫停該part CPU publication（仍保留native solve／query snapshot），成功轉入GPU updates，失敗恢復CPU，並協調同part所有sections；不能只在看到成功receipt後才停止CPU uploads。
本版23回歸及PairEyesSpeedGpuProxyFinal完整功能PASS；仍保留mixed最大1314.143ms、播放全幀9.75–10.18ms的性能失敗。新APNG private GPU caller尚未整合，不能說120FPS完成或拿三角形raster oracle替代衣髮／outline／shadow圖像驗收。

## 2026-10-07 private source revision 的契約檢查

- 調查時更正：native session 的 C ABI 使用有效的原始 Handle，沒有 registry 驗證。來源 revision 只能拒絕有效 session 上的過期來源，不能使已 free 的 Handle 可用。UE 由 FOkSession 所屬權與 Close 清空 Handle 保護；測試不得解參考已 free 指標。
- 每幀不得靠 pointer/length 或 v2 hash 判斷完整頂點來源相同。改為不可序列化的程序唯一 revision，來源替換/抽離與 runtime clone 重置驗證；palette layout 改變不得復活舊 token。
- 保留來源只讀 accessor，移除跨模組可寫 source 欄位。portable renderer 查詢改用 source_mesh()，避免新增外部 mutation 繞過失效。
- 驗證快取不儲存 shader 錯誤輸出，也不快取動態 palette 有限值檢查；動態非法矩陣仍每次拒絕。
- 工具錯誤：PowerShell 下不要把帶 `*` 的檔案路徑直接傳給 rg；rg 在 Windows 不會替 shell 展開它。改用明確檔案路徑或目錄配 `--glob`，避免重複無效搜尋。

## 2026-10-07 來源版本完成後最後場景驗收：未達性能門檻

`PairEyesSpeedPrivateRevisionFinal` 功能 PASS、exit0、四模組穩定、external UE0。背景最大幀仍 1278.791ms >300ms，playing whole frame 平均 9.628789–10.079022ms >8ms，120fps_present_verified=0。保留完整 evidence/report，不用混合平均 6.570ms 或 paused 截圖的 FPS 宣稱播放達標。

本輪快取只去除新 staged API 的每幀來源掃描；actual APNG caller 尚未啟用，不可把 CPU 波動解讀為 GPU 優化收益。下一步仍是 runtime source/draw capture、signed-weight conservative bounds、query 按需求解與全 section ownership 切換。ready 動作切換最大18.303ms與調速成本 budget PASS 不等於所有背景幀已達300ms。

## 2026-10-07 native GPU 座標轉接注意事項

- 必須依目前 viewer 實碼使用 Unity (X,Y,Z) -> UE (-X,Z,Y)、位置乘100。不要凭常見引擎慣例假設不同 swizzle；`KKPreviewActor::OkToUe/OkToUeDir` 與 `KKExactSkinning` 是本專案對照來源。
- 不能把 native post correction 與 UE basis 折成單一矩陣：native `rot_normal` 在退化 post 下保留 incoming vector。正确流程為 native skin/normalize -> native post/rot_normal -> basis/units -> UE secondary draw rotation。
- 原始 inverse-bind 的 unused homogeneous row 有極小浮點誤差；converter 依原 skin 的 upper 3x4 語意重建 affine row，有限值仍全部檢查，不因未使用 row 的 roundoff 拒絕有效角色。

### 2026-10-07 真實 APNG GPU oracle 首次失敗（保留）

PrivateGpuSourceFullRegression：實際 GPU oracle FAIL，210 publications/456612 draw vertices，position61.0057526cm、normal1.36774302、tangent1.3324573。合成 NativeUnitySpace 六案例 PASS（nativeposition .000636475cm），不能代替真實 APNG。

調查發現 FFI palette/stages 一律傳 body_skel，而 renderer 的 coordinated implanted clothes 使用 clothes_dynamic_runtime solved rig。修正共用来源選擇 skin_body，並加 per-part 差異記錄追查其餘差異。原 Native fixture 以同樣普通 body 做 oracle，需一併改選正確 rig；不能只與自己的錯誤 palette 比對。

工具錯誤：apply_patch 單次不能有兩個 Update File 指向相同檔案，合併該檔 hunks 後再執行；此次首次 patch 未套用。
- NativeResident 新測試首次編譯失敗：使用不存在的 FDynMeshVertex；正確 UE 型別為 FDynamicMeshVertex。查 DynamicMeshBuilder.h/既有 packing oracle 後更正，失敗 log 保留 InteractionPrivateGpuSourceRigBuild.log。
- 同測試第二次編譯失敗：FDynamicMeshVertex 沒有 GetTangentX/GetTangentZ，必須由 TangentX/TangentZ.ToFVector3f() 讀取，GetTangentY() 才是方法。已依 DynamicMeshBuilder.h 與既有 packing oracle 更正，保留 RigRepairBuild.log。

真實 oracle 第二次仍 FAIL，同最大值，per-part log 顯示各 hair/clothes 都不同，故 common clothes rig 選擇不是此61cm誤差的根因。確認共享曲線 ApplyAnimFrame 僅求解 palette/particles，不發布 CPU geometry，原测试取 PartAt 讀到前一份 positions。驗收需像 RefreshSkinnedDisplay 一樣在當前已求解 pose 上呼叫 Session.Skin()，不能再跑一次 dynamics。已修正這個參考資料生命週期錯誤，保留 PrivateGpuSourceRigOracle。skin_body 修正仍保留，以匹配 renderer coordinated rig 選擇。

### 2026-10-07 最後場景驗收入口超時

PairEyesSpeedPrivateGpuSourceFinal：entry10195.450ms >10000ms，audit_completed=0，functional_pass=0（入口就停止），process exit0並非功能PASS。四模組hash穩定、external UE0。worker open439.736ms/texture_prepare1628.569ms，GT發布期約7.3秒；尚不能將變慢歸因native新模式（actual scene還未呼叫），也不能只挑較快重跑掩蓋失敗。後續trace重跑觀察入口與完整功能，保留此全部evidence。

2026-10-07 GPU exclusions profile仍109.5–110.1fps（9.08–9.13ms）：實際 paired native resource root 並非 is_shared_root，finish_paired_dynamics 中 update_render_impl(!palette_only) 已生成全部CPU頂點。僅 skip 後續 skin_all 並未停止該次LBS。必須傳 GPU installed exclusions 到 paired solver 的 render flag，保留同一個120Hz attachment/physics過程。stderr保留每part skin_ms 與 dynamics資料；不得宣稱停止全部CPU蒙皮已生效。

2026-10-07 GPU bounds 首次編譯：匿名 namespace 的 FKey 與 UE input FKey 衝突（C2872/C2440）。改為專用 FPrivateBoundsKey；保留 InteractionPrivateGpuDisplayBuild.log。搜尋再度傳 Windows wildcard 檔名給 rg 失敗；後續必須用已存在目錄與 --glob。入口 trace export 已完成保留，不再分析入口。

2026-10-07 actual GPU caller 初版 readiness 錯誤：第一次 native pose 尚未求解時 source revision=0，不能用 proxy epoch 永久快取為不支援。PairPrivateGpuPublicationProfile 中多個 part 因此未啟用，該次不能代表完整 GPU 性能。修正為等待 source revision 非零再捕獲；後續必須以 installed source vertices/sections 確認實際涵蓋率。GPU updates 應合併 actor 各 section 到單一 RDG graph，避免逐 section Execute 的 CPU 開銷。

GPU batch 回歸首次編譯使用 auto MakeShared 結果再 ToSharedRef，實際已是 TSharedRef（C2039）；直接傳入。失敗 InteractionPrivateGpuBatchBuild.log 保留。首輪逐 section GPU profile phase0/1 全幀14.534493/18.834232ms，Render14.525378/18.725802ms，Present68.851224/53.103430fps，較CPU更慢；不得預設啟用。

BatchBoundsRegression 6/7 PASS，GPU proxy batch 的第二 component 被拒絕；原 fixture 將 component 隱藏，不能假設具有 ready render proxy，也未指定材質與 Verify oracle 一致。改為可見、原預設材質的獨立 component 再驗證。此回歸失敗尚未修復前誤啟動 PairPrivateGpuBatchProfile；該 run 僅診斷，不作驗收。

Native exclusion oracle 首次編譯失敗：Rust 不接受 .23 浮點 literal，OkSession 也無 Clone。改為0.23、兩個獨立 APNG session 同步步進作為原 CPU oracle，避免 serialize 還原丟失 runtime particle state。InteractionPrivateGpuExclusionsFemale.log 保留。

InteractionPrivateGpuExclusionsBuild 因其他 Crowd source 同時變更而編譯失敗：OKCrowdRuntimeScene.cpp line2 C2014 及後續未宣告。檢查時該檔前兩行已恢復合法 include，另有 Saved/Crowd Development UBT 正在執行；本輪不覆蓋其他工作的檔案，等待 mutex 後重新編譯。此 log 保留，不把失敗當成功。

PrivateGpuExclusionsRepairFullRegression 實際25個測試全部PASS，runner因要求不存在的 OpenKoikatsu.AnimationCatalog.PaletteReadCapacity 拒絕結果；正確名稱是 OpenKoikatsu.Animation.PaletteReadCapacity。必須補跑真正測試再算26項，不改runner放寬檢查。GpuStreamProxy batch已PASS。

Native paired exclusions 首次 cargo 編譯多了一個閉合括號（ffi.rs native_parallel_work/rayon::join）。依三層呼叫修正，失敗 InteractionPrivateGpuNativeSkipFemale.log 保留。

Native filtered solver oracle 第二次編譯 E0277：Vec 與 &Vec 的 assert_eq 左右型別不同；統一借用比較。InteractionPrivateGpuNativeSkipFemaleRepair.log 保留。

Code review發現重新安裝同一proxy時不能假設GPU generation=0：CPU takeover已使resident generation遞增。新增先reserve publication並異步查generation，再安裝的handshake；proxy重建仍以unique epoch拒絕舊job。GpuStreamProxy新增CPU takeover後generation query保持CPUstreams的回歸。Native filtered solver在男女APNG4個測試PASS，128244/210168頂點，particles與原120Hz solver完全相同。
2026-10-07 最後建置已 Succeeded（20.15秒），但串接驗收命令寫錯：Select-String 未顯式指定 -LiteralPath/-Pattern，將 Result: 當路徑；-PaletteCards 也誤傳2，它實際要求以 | 分隔的明確卡片路徑。Runner 拒絕後沒有執行效能量測，不能稱該輪回歸完成。修正明確參數與兩個fixture路徑，使用新Tag重跑。另將外部UE干擾時的新 playback verified旗標一併清零，避免只取消舊總合格旗標。
補充：-PaletteCards 是 string[]，caller 要傳兩個獨立路徑；runner 才會以 | 串成子程序環境變數。不能在caller自行拼接 |。
FinalDefaultVisibleProfile完成但未通過：phase0 7.983697ms／125.255785fps，phase1 8.412702ms／118.857888fps；不能宣稱預設播放穩定>120。新增playback flag也需套用額外paint hits==0，已修正。搜尋配對source誤猜不存在OKInteractionWidget.cpp與wildcard檔名，改用已知Source/OpenKoikatsu目錄和--glob。
一般launcher原CPU mode1600×1000量測未通過（43.29／51.56fps，最高945.56ms），原有1280 GPU audit不能代替正常launcher驗收。完整原失敗report保留，採不可變curve快照並恢復正式GPU預設後需重測。
本輪只讀取證據時亦曾讀取尚未產生的report.txt，以及誤猜InteractionAudit.log（runner實際固定run.log）；不屬產品功能失敗，但後續應先查看runner契約／列目錄，等待audit完成再讀report。最終功能與两輪Visible原始報告均完整，未修改門檻或覆蓋失敗量測。
2026-10-07 水槍audit審查：角色射擊驗證必須先清除前一段油漆，否則女角可能沿用單人沉積、男角可能沿用前一段誤射，形成false positive。已在每個角色射擊前ClearPaint；初版PairPaintGpuFunctional即使成功也僅作診斷，須重編譯後以新Tag正式重跑。
PairPaintGpuFunctional首輪雖functional_pass=1，但人工截圖顯示女角髖部瞄準被男角頭部遮擋，女角private paint可能是先前單人殘留，不能作正式通過。修正測試每角色清空油漆，runtime從該角色private GPU實際當前三角形選最高且非退化的暴露表面，由上方實際射擊並追蹤同一三角形；不改產品碰撞、不穿透遮擋、不固定角色mesh名稱或索引。
2026-10-07 水槍profile搜尋時再次把KKPrivateGpu*.cpp當作Windows實際路徑，rg回報os error123；改為搜尋已知目錄搭配--glob。效能新runner採7段男女各自同相機基準，避免不同視角直接比較。既有CommonUI viewport warning及double轉float編譯warning保留，不當成本輪水槍功能故障。
2026-10-07 PairWatergunVisibleProfile最後清空區間把ScriptedPaintAim.Reset，恢復實體滑鼠輸入，於可見窗口產生非預期噴射844hits／15.2cm³；此區間不能作乾淨基準，第一輪不列正式完成。修正保留ScriptedPaintAim=ZeroVector及fire=false，新增乾淨區間hits==0且SurfaceCount==0硬檢查。原始report保留，已排程舊binary的trace可診斷0..5但phase6無效；待trace退出後重新build與新Tag重跑，不能在audit中替換DLL。
水槍profile補充workload統計時apply_patch使用了不完整的Printf單行context而拒絕；未修改演算法。改以完整且assert存在的替換並檢查trace scope未重複。
PairWatergunVisibleFinalProfile正常exit0、hash穩定、externalUE0，但caller用LASTEXITCODE判斷以Diagnostics.Process執行的PowerShell runner；變數未設定(null -ne 0)導致caller exit1並跳過預排trace。已改以ErrorActionPreference=Stop及runner例外／run-evidence為準，獨立新Tag trace及無trace複測。正式首輪男角331ms與清空後59fps異常保留，不能選早輪較快結果替代。補充舊diagnostic trace phase6實際hits0、surfaces0，先前擔心污染未發生。
水槍profile只讀追查native query時誤猜character_session.rs不存在；實際session.rs已由目錄rg定位。後續直接對已知src目錄--glob搜尋，避免猜檔名。
最終trace手動export命令陣列未逐項加括號，PowerShell字串相加與逗號運算合併成單行；Insights exit0但只輸出threads，缺事件檔不能稱成功。保留原命令與log，改每項獨立括號、新ExportR2輸出，檢查CSV存在後才分析。正式無trace R2 APNG入口11.44秒，按已延期範圍不列入播放量測；Insights anchor export02:43:44退出，早於R2 phase0開始，沒有與量測重疊。
2026-10-07 atlas1024首次建置失敗：UE將member/local shadow警告C4458／C4456視為error；局部Size與N不能遮蔽原成員／normal buffer。另外PI是UE常數macro，不能當index array名稱。改ChartEdge／PageIndices，不關閉警告。
SharedAtlas1024Regression 5項PASS但Scene FAIL：新版page mesh壓成live vertices，遺失原coating slack pooling，屬真回歸；已恢復per-page容量及degenerate padding。兩個測試仍假設每角色獨享texture、第二triangle UV.X>.5，對共用buddy atlas已不成立；改以實際owned chart rect確認清除且其他GPU格完整保留、triangle UV bbox不重疊，未放寬質量／渲染門檻。密度必須固定SharedAtlas.Resolution直到clear，避免UI設定變更污染已存在chart。
atlas1024 pooling修復後第二次建置被局部Capacity遮蔽原scope的Capacity(C4456)拒絕；改PageVertexCapacity，不關掉compiler檢查。
SharedAtlas1024RegressionR3 Scene crash：padding用PV.Add(PV[0])／PN.Add(PN[0])等直接引用同個TArray元素，UE檢查禁止容器變更時引用自身元素。改先複製PadPosition／PadNormal／PadUV／PadTangent值，再Add；保留crash callstack。
atlas1024讀取report時把PowerShell -ErrorAction參數接到rg，rg將-E誤判成encoding；改先Test-Path再rg，避免跨工具旗標。完整APNG雙人功能已PASS，原始報告保留。
2026-10-07 SharedAtlas1024PairVisibleProfile功能／量測完成且hash穩定，但external UnrealEditor重疊，不列正式效能驗收。另發現runner只辨識UnrealEditor，漏掉同專案packaged OpenKoikatsu.exe；已補入外部程序辨識，不中止其他工作。原始失敗report保留，新Tag待外部程序自然結束後重跑。功能audit本身PASS，但其FPS亦不作獨立效能證據。讀取log時曾誤猜InteractionShowcase.log，實際固定run.log，改先列目錄。
atlas驗收只讀複查時Tests路徑漏掉Source/OpenKoikatsu，Get-Content回報不存在；已rg --files定位，產品實作未變。之後使用實際Source/OpenKoikatsu/Tests完整路徑。
最終diff --check指出兩份MD追加後EOF多餘空行；已只正規化這兩份文件的EOF後重查，不修改功能與原始數據。SharedAtlas1024PairVisibleProfileR2外部UE0、hash穩定、七段完成，方案二功能PASS，水槍120fps仍未達標（男75.29／女94.91fps），不宣稱全部目標完成。
navigation調查初次Shot未加showui，截圖只有3D地板，不能當按鈕消失證據；已改engine RequestScreenshot showUI=true的專用實際導航audit。獨立測試只終止自己以GalleryNavigationBefore.log啟動並驗明命令列的UE程序；未終止其他程序。KKCardTypes路徑誤猜KKUnityBundle，改rg --files實際KKCard模組。
使用者說互動不調身體參數時，曾將原先明確要求的肚子控制一起移除；使用者澄清肚子是例外。最終保留肚子專用窄入口，其餘body/face shape callback鎖定；驗收要同時驗證肚子有效與其他參數拒絕，避免再次一刀切。
肚子恢復patch初版假設OnHeadCamera是單行直接forward，但實際還會回寫Applied結果，context拒絕且未修改；已讀實際source重新套用，保留原錯誤回饋。
GalleryEditorInteractionNavigation首次真實1280×720驗收FAIL：Card page entries clipped or hidden。原始report保留；不能因source有button就宣稱恢復成功。audit原先先判斷再截图，失敗時無畫面，改先保存卡片頁並於下一frame斷言、記錄實際cached geometry，不放寬可見範圍門檻。
GalleryEditorInteractionNavigationR2仍FAIL。實景截圖確認入口不見：將Right改為InspectorDetails後，RootBox仍AddChild(Right)，把details從scroll搬出，入口wrapper InspectCol成孤兒。已改RootBox加入InspectCol，保留headers和scroll完整階層。此為本輪引入並由真實UI驗收捕捉的回歸；R1/R2失敗原始證據保留。
R3導航功能PASS，但人工截圖發現卡片頁互動按鈕長標籤的最後一字換行超過44px header。改為短且明確的「進入1對1互動場景」，載入女角的行為放tooltip；不可只用button bbox通過替代文字視覺驗收。
所有互動120fps調查時再次將Source/OpenKoikatsu/OKCrowd*當Windows實際路徑交給rg，回報os error123；已rg --files定位OKCrowdRuntimeScene.cpp，後續只搜實際檔案／目錄搭配--glob。
# 2026-10-07 全控制120FPS調查補充

- 匯出工具還在執行時提早讀 Anchor.csv，以及矩陣尚未結束時讀 report.txt，造成檔案不存在。讀結果應以工具完成或 Test-Path 為前提，不能將尚未產出的檔案當作失敗。
- 再次將 Windows 路徑萬用字元直接傳給 rg，並猜測不存在的 native 檔名。應先 rg --files 找真實檔案，或 rg 指定目錄搭配 -g；不得重复猜路徑。
- Get-Process UnrealEditor 未涵蓋 UnrealEditor-Cmd，曾誤判目前没有執行中 UE；以 runner owned-process.json 與 Win32_Process 完整名稱／命令確認。該 run 正常完成，無 crash。

- Shape batch 初次 Rust 測試編譯失敗：Rust float 必須寫 0.2（不能 .2），OnceLock 回傳借用後 for row 不能再加一層 &。已修正；需重新測試，該失敗 run 不作驗收。
- Native測試過寬使用 live_ --include-ignored，同時觸發兩個未相關且長時間的原遊戲資源測試。已核對 owned PID/完整命令後停止該測試，改為兩個 exact 測試；保留原log，不能把其未結束狀態当作總套件通過。
- AllControls120ResidencyMatrix的部分時間與Rust測試編譯重疊；AllControls120StreamsMatrixTrace也與native shape測試編譯重疊。external UE=0不足以排除本機CPU干擾，因此這兩輪只作功能／定位資料，不能當獨立FPS驗收。後續performance run期間不得編譯、匯出／分析trace或跑其他測試；先完成所有工作再順序量測。
- Resident collision patch 首次包含未完整的 CVar context，apply_patch 原子拒絕，沒有任何前段修改；不能假設前段已套用。已 rg 確認狀態，再用完整實際 context 重套。PrivateGpuSource.h 位於 Public，native accessory 實作在 session.rs；後續查詢必須先定位實際檔案。
- 乾動畫6ms階段 patch 曾用截斷行context及反向排序hunks，皆被原子拒絕；已按source順序完整context套用。首輪native編譯將Vec改Cow後仍zip(&binds)，Cow借用不是IntoIterator；已改binds.iter()，保留失敗log並重跑。追加錯誤MD曾在native workdir使用relative Docs路徑，已更正absolute workspace路徑。

### 乾動畫6ms優先回合：回報檔尚未產生
在runner仍執行時直接Get-Content report.txt，造成不存在路徑錯誤。後續必須先確認session完成，再用rg --files確認產物，不能猜測回報完成時間。
- 本輪曾再次使用 Tools/*.ps1 作為 rg 路徑（Windows不支援）；已改 rg Tools -g '*.ps1'。新增 native trace事件時發現固定spec陣列仍15，已同步18並在編譯前修正；新增事件必須同時更新容量。
- LayerMask首輪測試使用短名稱配合 --exact，實際執行0項，不作通過證據。已用完整 anim::tests::resident_layer_masks_match_traversal_and_invalidate_topology 重跑1項PASS；驗收需檢查 running N tests，不只exit code。
- dry trace顯示NativeValidate仍.316ms，查明is_shared_root是獨立raw clip虛擬root註冊，並非PairedAnimations共用controller pack；先前manifest fast path因此未啟用。已依現有factory_prefab相同的manifest/mod revision規則修正，後續必須用trace確認fast path實際有效。原生檔案搜尋亦應先rg --files，不能猜mod.rs／builtin_resources.rs等檔名。
- DryPair6msGpuFrameIkTrace的尾段與PaletteBatch native編譯啟動可能重疊，該trace只作子系統診斷，不作FPS驗收；保留完全隔離的GpuFrameIkVisible為當時性能證據。之後必須先write_stdin確認benchmark退出，再啟动所有編譯／export。
- 更正dry profiler解讀：phase0及phase1均1x，phase1選另一個ready pair，非4x。之前MD與commentary稱4x沒有根據，已明確更正；新增speed/state/selected_pair欄位避免誤讀。Crowd receipt GPU mode的frame_mean_ms=4.461是完整幀時間，hardware gpu_mean_ms=3.396，不得把mode GPU誤解為GPU timer。
- CrowdPairRootReference1600Trace完整100-card測量完成、GPU failures0，但進程退出777003。查既有 APNG_RUNTIME_SCENE_PLAN.md及CROWD_GPU_POSE_ERRORS.md，此為已知startup trace退出問題；不能normalize成0。保留診斷trace，補無trace正式性能對照。新runner第一版未redirect child stdout/stderr造成大量startup輸出；已在啟動前redirect並async讀取存檔，避免pipe堵塞。
- generic Frame分析第一次因defaultdict在彙整後建立缺省native role metric造成print KeyError，R1不作完成證據；改成無副作用get，缺少native bridge事件不能當原生CPU不存在。重跑新RootR2檔。

本輪 source rg第一次猜錯OKInteractionAudit.cpp目錄（實際在Tests），已先rg --files Source定位。後續不沿用猜測的路徑。

CrowdPairRootReference1600Visible正常exit0/100人但external_work_observed=1，舊monitor未記PID/name，不能判定干擾時間或取得正式驗收。5.075ms僅診斷。profile_crowd_apng_reference新增external-work.jsonl記PID/parent/name/command/time，正式run必須external=0。

DirectChannelsUnit首輪因Rust test coeff使用.2等無整數部分literal編譯失敗，改0.2。Rust literal必须帶0，不沿用C++簡寫。原始log保留。

DirectChannelsUnitR2誤在workspace root呼叫cargo，找不到Cargo.toml；R3改Tools/ok-extract工作目錄通過。后續反向在Tools工作目錄用相對Docs/Saved路徑也讀寫失敗，未影響native oracle；命令內跨目錄一律用絕對路徑。Anim.log未生成前讀取亦失敗，後續確認runnerexit/file existence。

DirectChannelsBuild失敗C4456：共享KKRuntimeSpeech.cpp新增voice cache大小變數Bytes遮蔽OGG buffer Bytes。僅將大小改DecodedBytes並保留cache行為，不回復其他工作者變更。需完整建置再量測。

DirectChannelsVisible在05:49:14–05:49:49遇到外部cargo/cl編譯，monitor成功記錄external-unreal.jsonl，性能evidence_isolated=0。8.919/9.994ms不作效能結果。Crowd R2啟動前偵測到外部build拒跑，目錄未建立。保留資料，等外部工作結束重跑，不停止其他工作者的程序。

DenseWorldActualPair oracle期間觀察到其他工作的cl編譯。測試的骨架／private粒子精確一致PASS可採用；serial/parallel timing不作效能證據，不與前版平均比較。

為避免native trace spec容量不同步重犯，KKRustExtract新增編譯期static_assert Names與NativeTraceSpecs大小相同；新增events必須同時擴capacity。

DenseWorldNativeSuite全庫預設32並行測試，同時載入多個大Unity bundle，在380個PASS後allocation 531898125 bytes failed中止(exit0xc0000409)；並觀察到sqlcache override_lookup_tracks_zip_changes_and_owned_indexes FAILED，尚無完整failure body因abort。不得聲稱全套PASS。大型真素材診斷勿預設多線程；接下來分組單線程物理／IK回歸，單獨重跑sqlcache，不再盲目全庫並行。

全庫並行中的sqlcache失敗在isolated單線程重跑PASS，顯示並行測試的共享fixture/environment干擾；未修改sqlcache生產實作，保留原失敗。物理／IK／pose分組PASS，後續不重跑大素材全庫多線程。

DenseWorldBuild等其他StreetSocialHost完整引擎編譯鎖約10分鐘後，因共享新增StreetSocial source的3個編譯錯誤失敗：Tests include header需../、MakeShared已TSharedRef不可ToSharedRef、WorldSettings header應GameFramework/。做最小編譯修正保留其他功能，未動他方程序或回復新增檔案。重跑完整UE build。

DenseWorldBuildR2揭露共享StreetSocial UE5.8 API錯誤：ApplicationContextMask是EAutomationTestFlags_ApplicationContextMask全域常數，DirectionalLight使用FindComponentByClass<UDirectionalLightComponent>。檢查時Sun已被其他工作者修正，保留其修正，僅補仍未修正的編譯項。

DenseWorldFusedTrace loading期間有外部UE(06:12:43–06:13:53)，整輪isolation0。quiet0約06:14:30開始，102–105秒bounded trace在外部退出後且無build/export重疊，可作root定位；7.187/7.428ms不得作正式FPS驗收。源碼rg曾猜dbone/tests.rs，實際ordinary_tests.rs/accessory_native_tests.rs，後續以rg --files結果為準。

## 2026-10-07：Topology／policy／UI量測受共享工作干擾

DryPair6msTopologyPolicyUiVisible雖然正常exit0，external-unreal.jsonl記錄06:29:34的另一個UnrealEditor-Cmd和06:29:56起的cl編譯。三段完整幀7.456／6.869／8.035ms，isolated0；不得以第二段較低值宣告改善。重新執行獨立R2；其他使用者工作不終止。量測期間只讀程式、編輯MD或未編譯原始碼。

## 2026-10-07：驗證命令與工作目錄

InverseBasis第一個命令使用--exact卻省略pose::模組名稱，測試0個；已以完整名稱重跑，4096種仿射／一般矩陣與奇異／非有限邊界的保留inverse元素bit比較，1 PASS。不可把exit0但running0tests當驗證。讀取R2報告時再發生Tools/ok-extract相對Saved路徑錯誤，已改workspace絕對路徑。另合併apply_patch hunk定位失敗，工具未修改檔案；已分拆確切上下文成功。

## 2026-10-07：BasisParent trace外部執行干擾

DryPair6msBasisParentTrace載入／quiet0期間另一個OpenKoikatsu（PID73296）06:42:33–06:43:01執行。external1、整輪FPS不得用為正式結果。quiet1於06:43:09開始，102–105秒穩定範圍在干擾程序結束後，可用CPU細分定位，仍需正式無trace獨立重跑。

## 2026-10-07：完整功能驗收尚未通過

DryPair6msLossyDenseContactsPairPaint exit0但audit_completed0／functional_pass0，錯誤Missing GPU surfaces after pose switch。不得宣告完整功能PASS。此前54嘴型／語音／live控制／12姿勢切換<=70.032ms最大frame通過，兩角色噴射及private geometry oracle（女0.000005332cm／男0.000005169cm）通過，濕面anchor能跟隨。失敗在最後pose switch後遍歷registered paint surfaces檢查Checked>0。正在區分surface被clear／topology失效與private GPU ownership；已加入surface topology invalidation與clear診斷，需repro找到根因，不能僅延長等待／刪除檢查。

ExportBindings單元驗證初次cargo命令又誤用workspace root，未執行測試。已改Tools/ok-extract工作目錄、absolute log、完整test名稱重跑PASS。來源檔案探索也出現猜測檔名／rg Windows wildcard路徑錯誤，已改rg --files及實際目錄globs；不把讀取錯誤當成runtime問題。

## 2026-10-07：共享workspace編譯修復

ExportBindingsBuild被共享StreetSocial／Crowd變更阻擋：OKStreetReadOptionalStringArray在OKStreetReadStringArray定義前呼叫但沒有宣告，C3861；OKCrowdRuntimeStream.inl本地Started遮蔽新class member，C4458。僅新增inline forward declaration及將局部變數改為StreamPrepareStart（含lambda capture），保留其他工作內容；重建R2驗證。Export runtime binding表的rehash／rename／增刪／source None切換／輸出重排／locked切換精確單元測試PASS。

## 2026-10-07：Missing GPU surfaces after pose switch真正根因

Audit Stage60每次角色噴射前都ClearPaint，Stage63從女角切到男角後因此抹掉女角的surfaces registry；Stage64卻檢查兩角色Checked>0。旧PASS依賴男角射線DiscoverSurfaces偶然也覆蓋女角，此測試對姿勢與相對站位不穩定，不能據此診斷GPU失效。通用驗收修正：第一角色ClearPaint丟棄先前solo試射，第二角色ClearPaintForActor(Role)只清當前受測角色，保留第一角色已驗證濕面；仍要求雙方新噴射命中／私人幾何oracle／anchor移動／conservation／切換後query／clear／角色替换。沒有延長等待、移除檢查或更改運行期物理。

## 2026-10-07：共享StreetSocial新測試編譯修復

ScopedDebugNavigationBuild遇共享新增測試以FOKStreetStore()臨時值傳給需要mutable lvalue的OKStreetCommitDecision，C2664連帶C2660。僅將臨時值改為同處局部UncommittedStore，保留該測試語意。R2完整editor build Succeeded9.18s。

## 2026-10-07：wet audit診斷重現

ExportBindingsPairPaint同錯誤再現。log顯示第二角色開始前Paint clear surfaces=54／volume36.8，之後只有男角新登記surfaces=41，無topology invalidation。確認是audit清除順序，不是角色GPU幾何消失。修正後binary已建置，接續完整驗收。

## 2026-10-07：ExportScoped trace載入階段renderer crash

DryPair6msExportScopedTrace在男角partial assembly後、pair commit／quiet量測前exit3：EXCEPTION_ACCESS_VIOLATION reading0，Renderer FReadOnlyMeshDrawSingleShaderBindings::SetShaderBindings（MeshPassProcessor.cpp:159）→InstanceCullingContext.cpp:1794。載入log亦有recursive FlushRenderingCommands警告；尚無證據能歸因於native求解／debug pose設定。external-unreal記錄另一個OpenKoikatsu於07:17:46開始執行，不能作FPS證據。此run沒有完整quiet範圍，不匯出作瓶頸比較。先完成已準備的worker transaction原生精確oracle，再重建／重跑GPU與完整功能，獨立trace重現／排除載入render resource lifetime問題；不得抑制null pointer或刪除renderer檢查。

### 後續重跑與未收益候選

WorkerTransactionRegression五項PASS，WorkerTransactionVisible及WorkerTransactionTrace均正常exit0／external0／hash stable；此次載入crash未重現，尚不足以宣告所有載入lifetime問題已修復。Worker transaction正式6.582／6.507ms、16 workers正式6.571／6.584ms，均未勝過前版6.433／6.481ms，不把更多worker／更少dispatch當成必然收益。保留8 workers，繼續按trace消除重複matrix計算。

本輪再次踩到Windows rg path wildcard（Tools/build*與KKGeneratedStream*）：rg不會展開這類位置參數。所有後續搜尋只傳既有目錄，檔名限制使用`-g`，先用`rg --files`取得具體路徑。程序巡檢必須涵蓋UnrealEditor-Cmd.exe，不能只比對UnrealEditor.exe。

### WorldCarry全專案build受另一個未完成API變更阻擋

DryPair6msWorldCarryBuild：KKUnityBundle（本輪native lib）與KKRuntimePose成功compile/link，但共享StreetSocial／Crowd新增呼叫FKKRuntimeFaceState、OKValidateRuntimeFaceState與八參數mouth update，目前KKRuntimeRenderMesh public/private尚未定義這些API，導致OpenKoikatsu game module C3646/C2079/C3861。不是include順序修正可解決，不能臆造另一工作的表情API或刪掉呼叫。已成功發布本輪KKUnityBundle.dll（15:42:07）、native lib/stamp（15:41）；先驗證可載入模組及APNG GPU回歸，之後在另一工作API完成後重跑完整build。不得宣告整專案build已通過。

BatchIndexOracle首次Rust build E0515：unwrap_or_else返回get_or_insert_with結果的as_ref，推導到短暫借用。改為兩分支內取ID再共用world_of，不把暫時參考返回closure。保留失敗log，R2重跑；批次索引只保留owned Arc，不持有session借用到下一frame。

BatchIndexOracleR2因少設定既有oracle前提OK_ASSIGNED_WORLD_FRAME=1，在cache.is_some失敗。這是舊共用world-precompute oracle的要求，與本輪只共享names不同；R3明確啟用該oracle環境再比較indexed／serial live worlds。正式runner仍不啟用這個較慢的world預計算。執行ignored素材測試前須讀取整個測試與prepare的環境前提，不能僅設定APNG path。

### StableBounds全專案建置第二輪共享介面修復

表情API定義已由另一工作補齊。StableBoundsFullBuild仍遇JSON shared-string key無operator+（C2678），以及OKCaptureRuntimeMeshes宣告7參數／定義6參數（LNK2019）。只將Profile.Key顯式轉FString，使用已有Expression.Name組key；capture定義補bool CaptureFacePatterns並按該flag執行已實作的Face capture，與caller及public snapshot宣告一致。R2完整build待驗證，不假定成功。

StableBoundsFullBuildR2正常exit0／Result Succeeded，11.58秒；完整建置阻擋已解除。更新全部DLL後StableBoundsFullRegression五項PASS。先前失敗log保留，與後續成功證據分開。

- InteractionStableBoundsAllControlsVisible：量測開始時無外部UE，但中途其他工作啟動2個UnrealEditor，隔離檢查拒絕性能驗收。保留原始數據作診斷；不能因exit0/hash stable就宣告FPS通過。須檢查整段external-unreal.jsonl並重測。

- 本輪讀檔錯誤：OKPaintPose.cpp不存在，且對rg傳入Windows路径KKPreviewActor*／OKPaint*造成os123。修正為rg --files定位，或目錄配-g檔名規則；不要猜檔名、不要在Windows命令位置傳path wildcard。

- Wet trace匯出session27495仍執行時就嘗試分析CPUEvents，FileNotFoundError；等待export exit0後重跑分析成功。匯出與分析有依賴，必須先確認session完成與檔案存在。

- 濕面後續誤猜KKPairedPlaybackActor.cpp／OKInteractionWidget.cpp位置，讀檔失敗；改先rg --files定位。不存在的檔案不能反覆猜路徑。

- WetPublicationAllControlsVisible功能正常但120FPS FAILED：女噴8.378ms、男噴9.744ms、男停射濕面10.545ms。external0/hash stable，不能以dry173FPS或局部coating改善宣告全控制通過。

- AtlasBatchAllControlsVisible：女Present122.236但full平均8.178ms仍未通過<=8ms；男噴9.503／停射10.108ms仍FAIL。保留failed budget，不以Present單一指標覆蓋完整幀門檻。

- DropStreams patch orchestration JavaScript括號錯誤（Unexpected token），整個exec未執行，沒有部分寫入。改拆開MD寫入與apply_patch呼叫，確認工具成功才建置。

- DropStreamsRegression：4項PASS但Paint.Scene新增droplet oracle沒有活躍附著滴落（compared0），測試正確拒絕空驗證。既有fixture濕面太薄／殘留門檻讓滴落未形成；修正測試物理設定產生真滴落，不能移除Compared>0門檻或當作功能PASS。

- 新Rust oracle以C++習慣寫.5/.3/.31等float literal，cargo報6個語法錯誤。Rust必須0.5/0.3/0.31，已修正；失敗log保留，重跑用fresh log。

- DeferredShapePairOracleR2：完整骨架與身體粒子一致，但私有配件粒子 exact 比對失敗。原測試同時比較 serial contact 與 production frozen-contact 排程，混入另一個變因；先改為兩側相同 production transaction，只切換 shape preparation，再確認差異來源。仍保留 exact 粒子門檻，不放寬誤差。

- DeferredShapePairOracleR3仍在pair0/frame0/part18失敗；原側APNG import runtime、對側animation snapshot重建，shape refresh使初始私有粒子差異暴露。R4兩側均以相同snapshot重建且採相同production pair transaction，只切換完整／優化prepare：64幀完整骨架與私有粒子exact PASS。不放寬容差；初始狀態與播放排程必須同時一致才是有效單一變因oracle。

- WetDropShapeAllControlsVisible：external0/hash stable/exit0，但phase6 8.357753ms/119.616760FPS、phase7 9.654396/103.591906、phase8 10.291743/97.181886，完整120FPS仍FAIL。滴落串流功能exact已PASS，但對比前輪沒有可證明性能收益；需要同binary A/B或新版trace，不能宣稱CacheDraw瓶頸已解決。
- 本輪log讀取誤猜Saved/Logs路徑；owned-process.json已提供run.log絕對路徑，後續直接使用該路徑，避免猜測。

- Trace export script本身exit會結束呼叫PowerShell，因此同一command裡放在export後的Python分析並未執行；先確認export session exit0，再用獨立command分析，才讀Root.json。本輪export資料完整，重新分析成功。

- GeometryJobsAllControlsVisible external0/hash stable/exit0，但仍FAIL：女噴8.530250ms／男噴9.749483／男停射10.468709。男geometry .686034ms比舊 .608714慢，跨surface細碎jobs沒有性能收益；功能exact PASS不能替代性能驗收。預設還原原逐surface路徑0，保留jobs2作通用oracle／後續粒度實驗，不能宣稱批次更快。

- DirectReferenceCore9AllControlsVisible：external_unreal_observed=1、performance_evidence_isolated=0，不能驗收FPS，完整報告保留。CoreLimit9來自通用run_crowd_development腳本，不是使用者指定的CrowdRoomDevelopmentAPNGGPU.cmd实际runtime入口；actual run_crowd_runtime_apng_development.ps1第76行没有corelimit，只stat unit。不得把不同Crowd launcher設定混為同一測試基準；正常入口不改corelimit。

- DirectReferenceAllControlsVisible：DLL stable/exit0，但external work再次被偵測，performance_evidence_isolated=0，不能正式驗收；診斷女噴8.198272ms/121.941485、男停射10.138894/98.644775，仍未過完整8ms。外部工作未獲授權停止，繼續獨立的source修正／功能驗證，待隔離條件允許再量測。

- ShapePublicationPairOracleR1編譯失敗：pose.rs沒有module級HashMap import，新helper誤用短名。改用std::collections::HashMap完整名稱；同時增加root同尺寸tie／duplicate bone／reparent／missing ID／負與零scale的原路徑exact oracle，重跑用fresh log。

- 使用者關閉外部工作後ShapePublicationAllControlsVisibleR1有效隔離重驗仍未過全部120FPS：女角8.227856ms/121.505108FPS、男角9.686335/103.255182、停射濕面10.392080/96.185901；exit0/hash stable/external0/isolated1。不得把「外部干擾已消失」或「女角FPS>120」當成完整8ms達標；乾動畫已過6ms，CPU濕面與形狀/物理更新仍待優化。測試期間未啟動其他build/tests。

- UniqueVertex測試檔修改腳本使用Python預設cp950讀UTF-8 BOM失敗，未改檔。改明確encoding=utf-8-sig並保留BOM；測試source更新後重新建置，不以較早binary執行新oracle。

- UniqueVertexBuildR1遇到既有runtime shadow source編譯阻擋：內層Added遮蔽外層Added（C4456）及spec缺WITH_DEV_AUTOMATION_TESTS的#endif（C1070）。最小修復為內層變數改AddedIndex並補關閉條件，不改陰影行為；BuildR2重新驗證。不得把build失败當成功binary量測。

- ShadowRegressionR1傳入測試prefix，UE實際Coverage/Mesh/RenderedMouth均Success，但runner要求每個完整test path有Success紀錄，因此exit1。後續傳入Coverage與Mesh完整名稱，避免用prefix當runner完整驗收清單。

- UniqueVertexAllControlsVisibleR1有效隔離量測仍FAIL：男濕10.313348ms/96.982137FPS。原/新跨run小差異不能證明候選收益，trace或same-workload A/B確認後才決定預設。rg在Windows再遇到*.cpp路徑無法展開，改目錄加-g '*.cpp'；已知錯誤不得繼續重複。完整report只能等runner exit並確認檔案存在後讀取。

- CoatingSerialAllControlsVisibleR1沒有啟動audit：runner直接把ConsoleCommands和ok.InteractionAudit相接，沒有分隔逗號。完整command line及owned-process.json確認87964為本輪專用進程，結束此進程，保留失敗證據。通用修复先Trim/TrimEnd(',')再補單一逗號，無參數維持原行为；後續fresh R2確認profile begin與完成。不得把停在空場景的FPS當互動效能。

- ParallelFor exclusive包含lambda實際工作，不能直接稱全部都是thread dispatch；正式CoatingParallel=0對照沒有完整性能收益，保持1。IncrementalWorldR1正式隔離仍FAIL男濕10.194791ms/98.037021FPS；功能精確通過不能宣稱120FPS完成。trace matched belly/wet比較支持PaintTick為主要新增CPU成本，後續應優先結構性處理濕面更新而非GPU場景render或任意CPU核心限額。

### GPU coating regression build R1
- TSharedRef::Get() returns a reference, not the raw pointer required by AddEnqueueCopyPass. Use operator->() as in the existing GPU readback tests; do not copy TSharedPtr assumptions to TSharedRef.
- Build R1 failed; no runtime/performance acceptance claimed from this build.

### GPU coating shader R1
- Including the complete RuntimeDeform.usf brought unrelated entry-point root parameters into the coating shader; SM6 reflection asserted on unbound parameters and startup exited 3.
- Extract FOutput/SafeNormal into RuntimeDeformCommon.ush; include common declarations only. C++ build success does not verify shader compilation.

### GPU coating shader root cause correction R2
- Common-include extraction alone did not fix the SM6 assertion. Generated RuntimeCoating.usf/Output.d3dasm show the comma-declared CoatingRow0/1/2 remained in Globals while UE expected them in RootShaderParameters.
- Declare each SHADER_PARAMETER global on its own line; UE shader parameter parsing does not rewrite comma declarations. The prior include-only explanation was incomplete; verify generated shader reflection before assigning cause.

### Coating proxy integration resource review
- The LocalVertexFactory position/tangent buffers are static SRV vertex buffers, not raw UAV outputs. Pack into RDG raw UAV intermediates then copy, as the existing resident deform path does.
- Read the acknowledged resident source position buffer via GetCurrent; do not construct a ByteAddressBuffer SRV over an incompatible static vertex buffer or assume its Usage flags.

- CoatingProxyBuildR2 failed because GetCurrent takes three arguments, unlike Install/Update which take an Error output. Read declarations before composing similar-looking resident APIs; R3 supplies its own failure message.

- SceneBuildR1: FMatrix::TransformVector returns FVector4<double>; explicitly construct FVector before converting to FVector3f. Do not assume every UE TransformVector overload returns a three-vector.

### Runtime GPU source readiness during body rebuild
- PairPaintR1 encountered a temporarily unpublished source during belly/body publication. Preserve the last overlay and use CPU for the current frame, but do not permanently disable coating for a transient source state.
- Receipts explicitly publish Retryable before their atomic completion status. Retry after 8 frames; stale epochs/bindings/generations and invalid descriptors remain hard failures. No parsing of error strings controls retry policy.

### GPU coating transaction review
- Source proxy replacement must invalidate only source bindings, not reset the destination generation. Resetting both identities together rejects the next valid destination update as stale.
- Retain pending receipts across source replacement; apply acknowledged source binding revision only when its source epoch still matches. Reset generation only when the destination proxy changes.
- Re-enabling GPU mode after CPU frames requires fresh runtime bindings; do not reuse old normals/topology solely because current CPU geometry is already up to date.
- Deferred coating batches validate weak component ownership, both epochs and current proxy pointers again at flush, before enqueueing captured render-thread pointers.

### Coating performance evidence gate correction
- AllControlsVisibleR1 completed but external-unreal.jsonl recorded another OpenKoikatsu process (PID 12504) during entry; report explicitly marks external_unreal_observed=1/performance_evidence_isolated=0.
- My earlier commentary compared its GPU numbers with the isolated CPU oracle before checking this gate. Those values are diagnostic only; withdraw the performance-improvement conclusion and repeat a fully isolated GPU run.
- PairPaintR1 functional_pass=1 remains a functional observation; external compiler/game processes were observed, so its FPS is not acceptance evidence. Do not call a run isolated from exit0 or successful functional flags alone.
- CpuOracleVisibleR1 explicitly reports external0/isolated1; missing external-unreal.jsonl means no events only when confirmed by report, not an unchecked assumption.

- BatchBuildR1 was already running while the transaction review changed its source files. Its success cannot certify the final source contents. Finish edits before a fresh incremental build; R2 is the authoritative build for batch/identity/re-enable corrections.

### Tests cleanup resource classification / search
- .okpreview is a character resource package (OKGalleryWidget copies it with card exports), not a disposable preview cache. Preserve all .okpreview/APNG/source assets and junctions during Tests cleanup.
- Inventory search again used Tools/cleanup* as an rg path on Windows and received an invalid path error. Pass Tools as the directory and use -g patterns; existing warning must be followed, not merely recorded.
- Cleanup completed by individually checked LiteralPath file removal; no recursive directory deletion. Keep current GPU trace plus CPU baseline and latest reader trace. Reports/screenshots/fixtures remain available even for superseded traces.

2026-10-07 SharedSetup：切換workdir到Tools/ok-extract後沿用workspace相對路徑，Python與log目的路徑不存在；尚未改檔、未執行測試。已改用absolute source/log paths，後續切換cwd需先核對路徑。

2026-10-07 搜尋再度誤用Windows路徑wildcard OKPaint*：rg拒絕，沒有改檔。應傳入目錄 Source/OpenKoikatsu 並用 -g 'OKPaint*.cpp'，不得將glob作path。

2026-10-07 GPU gather：Python預設cp950讀UTF8 cpp失敗，讀取時尚未寫入。已明確utf-8 read/write；所有含BOM／中文source需指定encoding。

2026-10-07 SparseGatherAllControlsVisibleR1：entry failed/audit_completed0，output/CrowdAPNG/C001.png已不在原路徑。此次Tests清理不包含output，尚未確定移動來源，不能當功能／性能失敗。找到Content/CharacterCards/CrowdAPNG/C001.png並與output/CrowdAPNG/C001.verified.json的SHA256精確核對相同，後續測試使用保留的正式資源路徑；不重製角色或更換測試資料。搜尋曾誤用Docs wildcard path，後續改目錄+-g。

2026-10-07 SparseGatherAllControlsVisibleR2完成十階段但external1/isolated0（另Unreal程序於量測中出現），hash stable/exit0。濕面與clear時FPS下降，不可當優化因果或正式验收數字；先完成功能，再待無外部Unreal重驗。

2026-10-07 SparseGatherAllControlsVisibleR3十階段完成／hash stable／exit0，但clear最後階段12:40:54觀察到external UnrealEditor-Cmd107984。即使前九階段已結束，整段performance_evidence_isolated0仍不採作完整正式A/B。GPU預設仍0，不能先宣告收益。CPUOracleVisibleR1 external0/isolated1，正式baseline完整有效。

2026-10-07 SparseGatherAllControlsVisibleR4入口階段external1（OpenKoikatsu8848及Cargo），正式量測中止。已用owned receipt／完整log commandline／PID／start time核對，只停止自己的69712，沒有停止其他工作。第一次停止保護拒絕：ConvertFrom-Json自動把started轉DateTime，再DateTime.Parse經文化格式字串丟失UTC造成8小時差；已直接cast DateTime後ToUniversalTime，實際與receipt差14ms。不得省略start-time保護。

2026-10-07 HeadInput候選：命令workdir為Tools/ok-extract，計畫Add-Content誤用Docs相對路徑失敗，但後續absolute source編輯仍執行。已立即補寫MD計畫；有前置文件要求時，須使用absolute path並設定ErrorAction Stop／檢查成功才進行相依修改，不能讓PowerShell非終止錯誤繼續後段命令。

2026-10-07 HeadInput依賴候選已撤回：actual APNG64幀oracle PASS，但HEAD_INPUT_REUSE診斷樣本0，沒有觀察到『舊post-remap判斷錯而真正輸入可重用』。只因程式碼前後時點不同不能斷定效能根因。已僅移除本候選新增field／cache／diagnostic／constructor初始化，原head reuse判斷恢復；不保留無證据runtime狀態。FrozenTransform優化保留。
2026-10-07 SharedBodyIndex候選撤回：沒有先查看rig_name_index回傳型別，錯誤推論clone為HashMap deep copy；實際回傳Arc<HashMap>，原batch已共享。新Arc造成雙層Arc，Rust compile E0308在建DLL前攔截。已精確撤回此次ffi候選，不保留虛假優化。以後判斷clone配置成本必須查看函式簽名與實際型別，再修改。
2026-10-07 RootScopesGpuControlsTraceR1：trace自己的48304於14:41:28UTC啟動，StreetSocial retarget stage遊戲48708於14:41:42UTC啟動；另job非本task，未停止。此trace只能呼叫樹／工作量診斷，完整FPS與根因耗時不得作隔離驗收。RootScopesPaletteRegressionR1亦external1，但三項功能PASS與hash stable可採認。
2026-10-08 身體互動調查：再次將Windows filename wildcard當rg實際path（OKInteraction*／OpenKoikatsuPlayerController*），搜尋失敗，未改程式。已改目錄搭配-g。需求核對發現原作HandCtrl+HitReaction+layer scheduler，不能以任意骨骼旋轉／整角色移動替代並稱原作等價。完整身體互動尚未實作，計畫見INTERACTION_BODY_INPUT_PLAN_20261008.md。
2026-10-09 BodyHitCore R1編譯：finalik沒有vscale，誤用dbone helper名稱，已本模組加入原f32三分量scalar multiply。UnityPy TypeTreeGeneratorAPI沒有安裝，無法解析缺typetree的原作scene設定；未安裝系統軟體，保留來源調查與明確未整合狀態，不能猜測反應曲線。shared h/list原本已有ReactionData，更正「全部互動設定缺少」過度推論。

## 2026-10-09 身體互動整合
- R3 Rust 編譯 E0616：FFI 直接讀取 HitReaction runtime 私有 definition。改為 Runtime::clear() 原地清除，取消不重配陣列；R4 原作共享曲線在內的 5 項測試通過。
- 路徑查找失敗：反編譯來源位於 C:/portable/KoikatuSunshine/kks_decomp，而不是專案根目錄。後續用絕對來源路徑。
- 修正碰撞判斷：SkinnedCollisionHelper 是表面碰撞；HSceneProc.LoadCharaTagLayer 從 h_bone_tag_layer 啟用一般點觸用骨架 primitive colliders。不能籠統把所有互動描述為蒙皮碰撞。原資料 cf_hit_spine02_L 未在共享 prefab 找到，匯出 catalog 明列 unresolved，不造假骨骼替代。
- 原作 scene TypeTreeGeneratorAPI 解析依賴已解決：僅安裝於 Saved/Tools/BodyInteractionDeps。可重現匯出工具 export_body_interaction_resources.py；六個 scene 定義一致，12 組設定 43,511 bytes。
- 10:20 另一編輯工作在相同 checkout 編譯時，本項尚未完成同步修改，導致該次建置讀到新 cpp／舊 header 而報 BodyReactionHit 簽章及 hit_local 缺失。得知同時建置後停止改 source，自己的 Build R1 排隊。後續必須先確認同 checkout 建置窗口，不能將 mixed-source 建置視為驗收證據。

- Editor R2 C4458：AKKPairedPlayback 區域變數 Role 與 AActor::Role 同名且 warnings-as-errors，改 BodyRoleIndex。Actor 方法的區域變數命名需避免引擎成員，不能只依普通 C++ 編譯習慣。
- 輸入審查：鏡頭在水槍／身體手勢持有左鍵時仍須同步 PreviousButtons；只 Cancel 並 early-return 會在持有中切換模式後錯把同一按壓接為鏡頭手勢。已維持按鍵沿追蹤，不產生重複所有權。
- Solo 原生動畫走 sample_paired_frame + finish_paired_dynamics，不經 finish_animator_pose，需在該入口補一次 body reaction；paired 仍在 contact solver 內處理，不能重複求解或推進時鐘。

- Editor R3／R4：專案 cpp 與 Rust 已編譯／主要 DLL 已連結，但 KKCrowdGpuPacking.dll 被另一編輯器驗收行程持續使用，LNK1104／UBA write retry 造成整體失敗。不得將局部成功說成整體 build PASS；不終止不屬於本項的行程，待釋放後重試。
- 施力座標審查：原 HandCtrl.TransformDirection 的角色朝向與 solver.GetRoot().up 是兩個來源。native 的 actor placement 已分離，不能又將 authored force 乘上動畫 cf_j_root 旋轉。with_release API 接收 native scene force，root.up 仍取 solver root。

- 場景回歸 InteractionBodySceneFunctionalR1 在第一次 walking 選擇後停止：audit 把 Gallery 顯示文字直接與 raw clip name 比較，但正式 UI 已透過 GameRawClipKeys／GameClipLabels 顯示帶分類的標籤。實際 SelectedGameClip／動作提交均成功（11.394ms）。驗收改為將選取標籤解析回完整 source|clip key 比較，保留 source 與真正動畫 identity 檢查；不是改 UI 或放寬到只比顯示名稱。新版測試尚待建置與重跑。
- R5 -Module=OpenKoikatsu+KKUnityBundle 仍包含相依的 KKCCrowdGpuPacking，而且外部 Properties 驗收繼續占用 DLL，無法藉此隔離建置。不得持續盲目重跑；需等該行程釋放。外部驗收不是本項啟動，不能直接終止。

### 2026-10-09 單人點觸未發布骨架與暫停時鐘
InteractionBodyUEFunctionalR3：4項PASS，BodyTouchSolo FAIL。反應 active／完成並不代表可見反應；new_batched IK 只改 local TRS，standalone 路徑忘記如 paired solver 一樣在結束時 rebuild_worlds，造成後續 physics/export 使用舊 world cache。修正為 solve 後發布一次，驗收必須比較實際主骨架位移。
另發現 paused solo 的 UI refresh 呼叫 ApplyPoseFrame(Delta) 會推進 base clock。新增 bAdvanceAnimation 參數，paused UI 仍給真實 dt 驅動反應／physics，但不推進 base animation；新增實際 actor 路徑 clock assertion。禁止以給 dt=0 修暫停，否則反應也不會完成。

### 2026-10-09 編輯與互動場景鋸齒
找到 DefaultEngine.ini 與 AKKPreviewActor::SetupStudioView 都指定 FXAA (1)，後者會覆蓋設定檔；只修改 ini 不足。兩個人物場景共用 SetupStudioView，原本也未固定 primary／secondary screen percentage。改為 TAA (2)、100% primary／secondary、禁止 dynamic resolution，TAA quality=2/history=100，避免用超採樣增加不必要成本。新增實際場景 audit 檢查 CVar。需驗證實際畫面／動畫拖影／效能，不能只根據設定值宣稱畫質或120FPS驗收完成。未修改 UE 引擎原始碼。
