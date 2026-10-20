# E298：依分段 profile 優化持續動畫 CPU

2026-10-05。承接 E297 使用者要求：通用修正、持續動畫整體平均13ms、錯誤記錄MD；不以停動畫、停物理或降低取樣頻率取得數字。

## 依 profile 實作

1. `OkSession::skin_all` 原本先複製rest positions/normals，再複製成臨時renderer，並每幀複製UV、顏色、indices、weights、binds。改成重用輸出容量、在同步呼叫期間移交幾何buffers，計算後還原來源buffers；幾何skin不讀UV／顏色，這些不再塞進臨時renderer。開啟可選 `OK_SMOOTH` 時，weights／bone_idx保留舊臨時副本，避免把平滑結果寫回原始權重並反覆平滑。
2. `RefreshRuntimeAccessories` 僅傳會改變的位置、法線與切線。已查看實際 UE5.8 `ProceduralMeshComponent.cpp::UpdateMeshSection`：未傳入且數量不匹配的UV／顏色會保留原值。三個暫存陣列跨section重用，禁用縮容量；render command已有自己的完整section副本，可安全同步重用來源陣列。沒有修改section拓樸或材質。
3. Avatar TOS使用不可變Arc快照；ingest以copy-on-write更新，來源優先次序不變。rig-only helper透過overlay查詢，不每幀clone整份表。共享資源的TOS仍依各自root隔離。
4. 名称／父階層／bone ID順序的binding解析快取最多保留8份拓樸／thread。完整比較ID、name、parent、HashMap迭代順序、bone_ids；保留原本重名優先順序。姿勢及世界矩陣不在快取中。換卡、改名、重設父節點或骨骼順序會miss重建。
5. 三月七的乾燥播放15.36ms，但測試暫停PlayerController tick後的停噴窗口11.21ms，指出actor外尚有成本。PlayerTick在沒有噴射時仍做瞄準投影／碰撞查詢、每幀寫Niagara參數與移動隱藏槍。改為非Fire只在正在發射時停止一次；保留原nozzle、已發射packet與表面film繼續模擬。Fire時沿用原來的彈道瞄準公式。相機透過弱引用保留gallery，每次仍讀gallery當前PreviewActor，避免重掃UObject；不能單純快取actor，因為舊、新actor可能共用同一camera。

未增加網格面數、未停止DynamicBone、未重製APNG、未建立新的大型資料庫。

## 精確性與操作驗證

- `Anim298SkinParityR1.log`：C010、30個根節點移動／旋轉姿勢，和舊clone oracle逐項比對位置、法線、rest資料、weights、bone_idx、binds、indices、全部UV與頂點色相等；12,942,360個非私有rig頂點。
- `Anim298SmoothParityR1.log`：C001、`OK_SMOOTH=1`，30幀同項比對相等；464,670個頂點。未將每幀平滑結果累積回原權重。
- `Anim298BindingParityR1.log`：C010、30姿勢binding解析等於uncached，名稱／parent／bone order改變失效，容量上限8驗證通過。
- `Anim298AnimRegressionR2.log`：動畫Rust38項通過、31項明確忽略、0失敗；含來源取樣、TOS overlay／snapshot、共享動畫roundtrip、duration等。明確忽略的大型fixture不冒充已跑。
- `Anim298BuildR6.log`：正式編譯成功。
- `Saved/Logs/Anim298RegressionR1.log`：9項逐項Success：CompactProceduralDraw、MutableStreams、SourceGeometrySnapshot、KksMouse、SharedDeformation、ConservativeTransport、Film、MaskParity、Scene。KksMouse新增idle不做aim query／保留nozzle、cached gallery可在同camera切換actor。

## 中間實測（不是最後版本）

正式專案CPU，1600×1000，20秒乾燥walk、3秒噴射／15秒停噴。E297開profile，E298正式性能數字關profile，兩者條件差異保留，不冒稱精確FPS。

|測試|卡片|actor平均ms|乾燥frame平均ms|停噴frame平均ms|
|---|---|---:|---:|---:|
|Anim297ProfileR2|C010|14.852927|17.836459|未測|
|Anim298PerfR1：buffer／mutable streams|C010|11.494431|13.944396|14.620754|
|Anim298PerfR2：再加TOS／binding快取|C001|11.465156|15.364966|11.205174|
|同上|C010|10.456402|13.632285|13.496305|
|同上|C018|7.475825|9.464585|10.076296|

R1、R2 valid=1，全部影像檢查通過，來源到顯示索引誤差0cm，真實半脫／全脫／穿回及GUI edited存卡／離線重載通過。R2是加入控制器閒置修正前的數字；不能拿它宣稱所有卡達13ms。

## 最後驗收

`Anim298PerfR3` 使用完整R6版本、控制器正常tick，新增 `idle_paint_aim_queries=0` 硬性檢查；20秒動畫、3秒噴射／15秒停噴，C001/C010/C018。結果以下追加。

Main SHA256 `095742ED191DF3006EB86E3572CA883C996BD9A0B0B048DE75007852FAEBC87D`。
Plugin SHA256 `4476C2C28FCE6DFA90307AE6B1AA74C725E90CEC1251E28E61D083A5000D08A9`。

## 本輪錯誤

- BuildR1：工作區既有GPU oracle直接實例化FRDGBuilder卻缺少完整header。只補 `RenderGraphBuilder.h`，BuildR2成功，沒有改其運算。
- BuildR4：新增相機測試使用AOKPaintSprayer但只看到forward declaration。第一次自動修正header的字串anchor沒匹配實際 `../OpenKoikatsuPlayerController.h`，未檢查replace是否成功，BuildR5重複失敗。改用明確 `../OKPaintSprayer.h` 並驗證存在，BuildR6成功。以後所有source replace必須assert anchor或結果，不能把工具exit0當成功修改。
- 在Rust cwd查reader report時一次用了workspace相對Saved路徑，找不到；改成完整絕對路徑。另一次rg把OKGallery*當路徑再次出現Windows glob錯誤；必須目录＋-g，不能用shell positional glob。
- 每次性能測試都等正式build Succeeded、Rust測試結束及UE回歸程序退出後才啟動；保留launch.json hash與獨立abslog，不沿用E297已停止的build競態測試。

### R3實測與追查

最終R6（idle aim queries全部0）仍未穩定13ms：C001 actor18.344185/frame22.990046ms；C010 actor12.887783/frame16.324873ms；C018 actor8.135345/frame10.632436ms。三卡功能驗收valid1。不得以R2較快數字取代R3。

Anim298ProfileR1再次量測C00120秒：actor12.190390/frame15.373784ms；CPU mesh cloning0.034ms、geometry skin0.437ms，原clone瓶頸已降低；私有rig刷新仍有成本。shared CPU流程在palette solve後會dt0刷新geometry，這只是後續優化候選，尚未修改或驗證，不能宣稱已消除。
