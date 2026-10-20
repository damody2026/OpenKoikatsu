# 1對1即時互動展示場景

日期：2026-10-06。狀態：R66 雙人姿勢切換已實作並通過 APNG 雙角色實機 <=300ms 驗收及七項回歸。R65 已加入場景內男性 APNG 載入與原生雙人動畫。完整 8ms／120fps 效能與卡片矩陣驗收仍保留至後續。

## 當前狀態與接續順序

已實作女角入口、返回與重進、眼睛／頭部獨立追視、54嘴型與語音開合、腹部、已準備姿勢搜尋與分頁、暫停／重播／0.25～4倍速、水槍。R40完整建置、SparseNormalsR1七項檢查與FunctionalR13原生流程通過，姿勢及其他控制最大4.141ms。正式 APNG runtime 入口已實作，R46 大型C036與C055完整流程通過，進場4.483／2.604秒；C001性能模式進場4.128秒。共用曲線與顯示材質可進場後背景準備，沒有角色專屬離線姿勢依賴。120fps全情境尚未達標。

1. 已推進至 R64，APNG入口、共用矩陣、chart、Transport平行預查詢及相容leader姿勢的功能驗證通過；行走raw片段改為循環採樣，保留session elapsed連續。R60原生增量world cache依A/B結果撤回。效能優化依使用者要求停在此版本；R65 接續男性角色與原生配對功能。維持動畫、私有物理、材質與液體守恆。禁止依特定角色名稱改寫行為或使用角色專屬離線烘焙。
2. 六階段可見視窗檢查：乾燥、追視／語音／連續嘴型、腹部拖曳、持續噴射、停噴保留污漬、污漬下連續切換姿勢。每階段保留全幀平均與最大值、DXGI Present提交率。
3. 測量工具記錄外部UE程序及三個依賴DLL hash；有其他UE重疊不得標示性能驗收成功。背景準備不視為即時動作切換已達標。
4. 完成功能與效能後才做完整代表卡片／複雜服裝／CPU與GPU／配對矩陣及聲畫驗收。APNG 是正式入口；原生遊戲目錄測試僅作輔助，不能代替 APNG 驗收。
5. 每個失敗追加至錯誤MD，包含證據、修正及防止重犯的方法；遇到問題由本任務自行決定並繼續。

R58～R63性能保留作歷史調查，不能當R64正常行走循環的性能證據：當時raw clip在超過duration後停於尾幀，功能audit也漏測多周期動作。R64已修正並由實際APNG確認兩周期後腿部仍在動、54嘴型／實際語音／水槍／返回重進成功；入口4115.995ms、同步控制187.637ms，十一項回歸通過。最新正常循環無trace六階段平均7.105／7.404／7.623／9.720／10.000／11.378ms，後三階段未達8ms與120Hz；完整性能及卡片矩陣驗收尚未完成。

## 目標與入口

載入場景選定女角及服裝後，以「1對1互動」進入專用展示模式。沿用角色與相機，在同一 World 切換展示環境及精簡介面，避免重載角色。返回恢復 Gallery 狀態，取消過期背景工作、停止語音並清理水槍。

1對1預設為玩家相機與指定女角互動；雙角色姿勢沿用配對播放，第二角色成本納入驗收。

## 操作

|操作|要求|
|---|---|
|眼睛看 camera|独立開關，每幀跟隨相機；關閉恢復動畫／中性視線|
|頭部看 camera|頸部與頭部受限旋轉、平滑追視，與眼睛開關獨立|
|嘴型與開合|依角色實際支援項目建立選單，手動／語音自動開合；播放時可換嘴型|
|肚子大小|只暴露腹部參數，連續拖曳即時更新，保留動畫、嘴型及污漬|
|姿勢|分類、搜尋、切換、暫停、重播、速度；雙人使用共用時鐘|
|語音|選句、播放、停止及切換，預設角色聲線|
|水槍|沿用 A＋左鍵、R 清除、原有設定與動畫附著|
|相機|旋轉、平移、縮放；UI操作不得誤觸相機或水槍|

其他身體及臉部參數在進場時固定，不加入完整捏人介面。

## 架構與生命週期

- 場景控制：準備／啟用／返回、角色引用與互動狀態。
- 精簡 UMG：只顯示互動控制，Gallery 保存卡片與載入狀態。
- 共用動畫準備：不可變資源快取，每角色獨立 Session；背景解析不修改正在播放的 Session。
- 請求帶版本與角色身分；返回或重新選取後拒絕過期結果。
- 每幀由單一 owner 推進姿勢，依序整合頭部追視、物理、蒙皮、眼睛／嘴型及水槍。
- 頭部追視從本幀動畫基礎重算，禁止累加旋轉；保持頭髮、配件及碰撞一致。
- 腹部更新合併同幀滑桿事件，只處理受影響資料；不重建角色。

## 動畫、語音及水槍

昂貴動畫解析、映射、IK初始化、材質／shader、水槍BVH／atlas在準備階段完成。切換維持舊動作，成功後才提交；可直接選擇的姿勢必須在300ms內可見。背景載入不等於符合切換延遲，快取記憶體及冷／暖切換必須量測。

語音背景解碼及PCM快取，重用播放元件。嘴型開合跟隨實際音訊播放而非動畫時間；手動選嘴型有明确優先權，停止恢復最新手動值。沿用音量嘴型，不宣稱音素辨識。

水槍共用本幀姿勢／蒙皮快照，預建表面資源、重用稀疏覆蓋緩衝。覆蓋及附著每幀跟隨；液體維持固定步長及守恆。特殊cloth／deformer另測，不以降低必要更新頻率冒充達標。

## 效能驗收

- 不限幀能力測試：完整可見場景每種情境平均幀時間 ≤8ms。
- 120fps模式另驗證呈現節奏（120fps間隔8.333ms，刻意限幀等待與執行成本分開）。
- 記錄Game Thread、Render Thread、GPU、等待、P95／P99／最大值、超8.333ms比例及連續掉幀。
- 一般控制下一可用更新幀開始反映；姿勢點擊到可見、語音點擊到開始均 ≤300ms。
- 互動開始後任何首次操作、切換、GC或初始化均不得造成 >300ms 停頓。
- 冷載入／進場準備另外記錄，禁止混入或掩蓋互動測試。
- 固定硬體、解析度、畫質、角色、服裝、動畫與噴射設定；使用實際可見視窗／RHI／音訊，不以NullRHI、hidden幀計數或Actor CPU代替全場FPS。

## 基準及風險

現有 ANIMATION_CPU_FIX300 三卡乾燥整幀約10.56–12.44ms；PAINT_PERFORMANCE289大型卡活躍塗層曾達數十ms。這些歷史數據不代表本場景最新結果，必須重新profile。頭部完整追視需要新增；SPEECH_DISPLAY357沒有涵蓋所有GPU嘴型及聲畫同步。

## 實作與驗收清單

- [ ] 目前版本代表角色基準與分段profile。
- [x] 新場景入口、展示環境、精簡介面、返回與取消。
- [x] 眼睛及頭部獨立追視，從每幀基礎姿勢施加受限偏移。
- [x] 嘴型／開合／腹部操作接線；腹部更新成本仍須優化與驗收。
- [ ] 動畫準備快取、切換提交、單人與配對時鐘。
- [x] 語音預載／元件重用、嘴型優先權及停止恢復（54嘴型實際PCM逐頂點對照）。
- [x] 水槍共享變形資料、命中／附著／流動／滴落／清除（Scene／ConservativeTransport及原生展示通過；最終矩陣另驗）。
- [ ] 依profile優化至8ms並驗證300ms最大停頓。
- [ ] 完整建置、功能回歸、實際聲畫錄影及效能證據。

測試一般／複雜服裝飾品／獨立卡／配對姿勢；乾燥、持續噴射、停噴污漬、語音追視、腹部拖曳與連續切換。嘴型遍歷支援項目及開合值，近景檢查嘴唇、牙齒、舌頭、口腔及实际音訊同步。未完成效能矩陣之前不得標示120fps已達標。

## 第一階段實作紀錄（2026-10-06）

新增 `Source/OpenKoikatsu/OKInteractionScene.h/.cpp`，在 Gallery 選定女角後使用「載入女角並進入1對1互動」；已載入相同角色時直接沿用 Actor。返回恢復控制項的完整順序及 slot 設定。展示模式提供眼睛／頭部追視、嘴型／開合、腹部、原有動畫／配對選單、播放／停止語音及水槍。一般捏人控制不顯示。

頭部新增 Rust 原生受限追視，在物理前套用到本幀動畫骨架，頸頭分攤旋轉，附件跟隨；關閉恢復動畫。語音新增背景 PCM 解碼、128MiB 快取及播放元件重用，過期播放請求不提交。手動嘴型不被配對喘息自動嘴型覆蓋，可切換語音自動開合。

驗證證據：

- `Saved/InteractionBuildR5.log`：Editor Development 完整建置成功。
- `Saved/InteractionRustTests.log`：2 項頭部追視測試通過，包含受限角度、附件跟隨、長時間不累加、關閉恢復。
- `Saved/Tests/InteractionShowcase/RegressionR1/run.log`：7 項 Unreal 測試通過，涵蓋 Gallery 選取、相機、控制項返回、語音核心、實際嘴部網格渲染、共享臉部變形及水膜。
- `Saved/Tests/InteractionShowcase/FunctionalR1/report.txt`：真實 March_7th 角色进場、54 嘴型操作、實際音訊播放、手動開合優先權、停止、返回及再次進場通過；同步控制回呼最大1.282ms。這個值不是從點擊到畫面呈現的延遲。
- FunctionalR1：整幀間隔平均39.273ms、P95約14.3ms、P99約23.1ms、最大22436.979ms，包含首次動作解析／shader準備與截圖。此輪明確**未達8ms及300ms要求**，也沒有驗證120fps Present。後續將預設動作準備移到互動啟用前；其他首次切換仍待預載及快取。
- `Saved/InteractionBuildR7.log`：最新版本建置成功；預設動作準備已移到進場前，修正地板高度與主方向光優先權。
- `Saved/Tests/InteractionShowcase/FunctionalR2/report.txt`：最新版本上述功能再次通過；播放動作時水槍80次命中、沉積32單位，守恆檢查及返回清除通過。同步控制回呼最大1.376ms。
- `Saved/Tests/InteractionShowcase/RegressionR2/run.log`：7項回歸全部通過，新增原 Gallery slot padding 恢復檢查也通過。
- FunctionalR2 的噴射前互動樣本：平均14.182ms、P95約14.8ms、P99約15.8ms、最大686.779ms、916樣本，包含截圖／UI更新；**仍未達8ms，300ms最大停頓及120fps尚未驗收通過**。這份全幀摘要在啟動水槍前擷取，不能代表持續噴射效能。
- `Saved/InteractionBuildR8.log`、`Saved/Tests/InteractionShowcase/FunctionalR3/report.txt`：最新建置與完整功能驗證通過，54嘴型、實際語音、80水槍命中、32單位沉積、守恆、清除與重進場正常。測試槍口改用玩家控制器相同偏移，停噴2秒後截圖；人工檢視 `FunctionalR3/Painted.png`，頭髮／臉／衣服正常顯示且衣服留有污漬。R2立即停噴截圖的近鏡遮擋不能作為材質正常與否的判據。
- FunctionalR3 噴射前平均13.671ms、P95約15.3ms、P99約18.4ms、最大704.366ms、955樣本；同步回呼最大1.514ms。截圖等成本仍包含在內，未滿足效能驗收。噴射成本及實际可見120fps需獨立測試。

執行方式（專案根目錄 PowerShell）：

```powershell
./Tools/run_interaction_showcase.ps1 -Tag MyFunctionalRun
./Tools/run_interaction_showcase.ps1 -Tag MyRegressionRun -Regression
```

人工操作可用 `./Tools/run_card_reader_development.ps1 -CardDirectory '' -GpuSkin` 開啟原生遊戲卡片模式，選女角後點「載入女角並進入1對1互動」。目前入口需要原生 Session；離線 APNG 卡片尚未接上本展示模式，列入後續相容性工作。

Tag 必須唯一；腳本啟動自己的測試程序，記錄測試及 DLL hash，退出時只清理其擁有的程序。使用正常 RHI／音訊，功能測試視窗隱藏，因此不能用這個腳本的幀數宣稱可見視窗120fps。

剩餘主要工作：動畫可選項的準備與快取、腹部受影響資料更新、水槍共享變形快照、完整角色／服裝／配對矩陣及可見視窗 CPU／Render／GPU／Present 分段量測。現階段交付為可操作的第一階段場景，效能驗收尚未完成。

## 持續實作計畫

- R19完整建置、RegressionR3四項及FunctionalR5通過：54嘴型、實際語音、手動嘴型覆寫、水槍80命中／32沉積、已準備動作提交6.463ms、返回與重進。已檢視Painted.png的角色與衣服污漬。語音按鈕只開放已解碼句子，128MiB快取保留當前選句，不被背景預載淘汰。功能截圖造成的753ms保留在報告，不作純效能證據。
- R21完整建置與GeometryR2兩項通過：序列／平行串流CPU頂點、法線、切線、bounds與真正GPU上傳一致；跨表面幾何／法線／碰撞三角形／BVH對照max_error=0。13k頂點序列0.126ms、平行0.153ms，故平行封包預設改回關閉，僅保留診斷選項。PerformanceR6平均13.413／14.308／14.320／22.224／21.288ms仍未達標；後续重新測量跨表面更新。

- 最新持續驗證：R18完整Editor target建置成功；PerformanceR5正常RHI無截圖五階段平均11.756／13.015／12.846／19.401／21.837ms，最大23.647／22.922／23.718／40.777／42.046ms。路徑快取已消除先前約150ms週期停頓，但平均8ms與120fps仍未達標。下一項針對串流封包與精確塗層幾何成本，維持既有資料／物理流程。

1. 補獨立、無截圖的乾燥／追視語音／腹部拖曳／持續噴射／停噴測量。採固定記憶體統計，記錄CPU、Render、GPU與操作延遲；測量前準備資源，呈現節奏獨立驗證。
2. 原生動畫每幀binding解析改成依Avatar與骨架拓樸的不可變執行計畫；保留路徑衝突、來源優先權、曲線取樣及物理原意，用舊流程逐幀比較。移除預設每幀診斷集合建構及stderr輸出。
3. 動畫及語音準備改為有界、可取消的共用服務。背景工作只處理不可變資源；角色替換、返回及聲線切換後舊結果不得提交。即時姿勢選單只能提供準備完成的項目，準備失敗必須可見且維持舊動作。
4. 腹部事件合併至每幀一次；從原生shape相依性縮小更新範圍，保持頭髮／配件／碰撞及既有液體一致。單人及配對皆由單一時鐘推進。
5. 水槍每幀共用角色變形資料，命中、BVH及塗層重用同一姿勢版本；特殊deformer保持精確路徑，不停物理、不降低必要更新頻率。
6. 修完功能後才跑最終矩陣：代表角色與複雜服裝、單人／配對、全部嘴型與實際語音、冷暖切換、持續與停止噴射、返回重進；完整建置及回歸，正常RHI可見視窗驗證平均8ms、120fps與300ms門檻。

遇到錯誤一律追加至 `Docs/INTERACTION_SHOWCASE_ERRORS.md`：症狀、根因證據、修正、驗證與防止重犯的規則。禁止以特定卡片名稱分支、停掉物理、隱藏昂貴操作或刪除慢幀達標。

進度：Binding執行計畫快取已通過240時間點與舊流程逐值比對，以及骨架／Avatar失效與32項有界快取檢查（InteractionBindingPlanR2）。PerformanceR1無截圖五階段平均11.231／12.059／14.415／17.654／16.594ms，最大均小於300ms；Game Thread是主瓶頸，GPU約1.1ms。仍未達8ms。已加入每次水槍更新內命中／BVH／塗層的幾何共享，以及獨立變形oracle測試；R10建置成功，功能對照待執行。姿勢改成來源搜尋、每頁32項準備及僅可選已準備項目，具有取消及角色身分驗證；新增選擇及提交時間測試待執行。

- R11建置成功。`SharedPoseR1` 的 `OpenKoikatsu.Paint.Scene`、`Interaction.RestoreControls` 兩項成功，含共用與獨立幾何路徑逐頂點比較，未改碰撞／物理更新頻率。
- `InteractionMotionPlanR3`：單一完整權重姿勢保留原加權與正規化算式，240時間點與舊組合流程相同；骨架配置重用含拓樸變更回退通過。
- `InteractionWorldPlanR1`：階層重建改為依完整父節點／輸出骨骼驗證的128項有界執行計畫，缺失父節點、骨骼順序及名稱改變與原遞迴計算相同。追加跨拓樸及真實動作檢查進行中。
- `InteractionWorldPlanR2`、`InteractionWorldMotionR2`通過父節點切換／self與0父節點／128容量／240原生時間點檢查。R13指定專案模組建置成功；R14平行水槍幾何建置成功。
- `FunctionalR4`全功能通過，已準備姿勢提交8.760ms、54嘴型、83命中33.2沉積；人工檢視Painted.png顯示新姿勢及衣服污漬。含準備背景與截圖的平均16.875ms、最大757.547ms仍不作效能通過。
- `PerformanceR2`無截圖平均14.233／15.043／15.262／21.006／22.369ms，最大148.131／148.148／153.172／156.243／159.094ms。效能未達8ms，不能以先前較快的R1取代新版本結果。正在以原生trace定位週期慢幀。
- `GeometryR1`水槍Scene、控制返回、語音核心三項通過。水槍平行／單執行緒世界頂點及碰撞三角形max_error=0，fixture時間0.222／0.223ms，fixture不代表真實大角色加速幅度。
- `TraceR1`原生trace及CLI匯出完成，Game Thread慢幀落在KK_NativeAnimation（最大約222ms），一般串流提交平均約2.08ms；trace觀察本身增加開銷，不當120fps驗收資料。舊ProfileR1也將曲線階段慢幀定位到138–184ms。
- `InteractionBorrowedSkinR1`：私有飾品／服裝蒙皮改為借用來源幾何、僅複製需要指派的世界矩陣，不複製完整Transform圖。148次palette與44480頂點通過CPU／palette流程及舊geometry逐值oracle；各次skin parity valid=1。可選DQS、平滑、relax與診斷仍使用原mutable路徑。
- `CacheR1`語音128MiB跨聲線淘汰、重複提交容量計算與返回取消工作通過。R15/R16專案模組建置成功，R16加入只在慢取樣時記錄細分階段，下一輪量測定位剩餘曲線停頓。

- R22完整建置與PerformanceR8獨立測量：五階段平均9.166／9.738／9.781／12.511／12.434ms，最大17.052／11.853／13.643／31.782／26.813ms；水槍跨表面更新後actor_mean約2.52／2.49ms。平均8ms仍未通過。
- 背景動作準備改為顯式生命週期的不可變SerializedFile共用，有界1GiB／4資源；取消及退出释放。ConcurrentMotionR3準備32動作2205.118ms、一次解檔，並行播放平均0.282ms／最大0.754ms；巢狀生命週期及資源釋放測試通過。
- R23完整建置及MouthAllR1四項回歸通過：全部54嘴型配合實際語音，共2430取樣步／15,202,080頂點與權重對照，誤差0；不同角色互不干擾。52k頂點四section批次串流序列0.853140ms、平行0.583285ms，CPU資料、bounds及GPU UV／顏色上傳一致。
- FunctionalR6通過動作提交7.394ms、載入場景選單同步、54嘴型／語音／手動覆寫、水槍80命中32沉積、返回與重進。截圖停頓保留但不作純效能證據。
- PerformanceR9与FunctionalR6誤並行，資料僅作負載探索，不作效能驗收；測試啟動工具已加入具名mutex串行執行，正式量測與建置分開。
- 新骨架名稱查找依完整source／ID／名稱inventory驗證32項快取，保留重名first／last原語意；ABMX基線使用集合查找，私人蒙皮與植入橋共用索引。InteractionExpressionR2七項、RigNamesR2及RigSkinParityR1（148 palette／44480頂點，誤差0）通過。R24建置與獨立效能量測進行中，尚不宣稱速度改善。

- R26完整target建置成功，新增原生DXGI Present計數，僅在階段邊界查詢，包含swapchain身分與有效性檢查；可見視窗才允許120fps驗證。此計數衡量應用程式Present提交，不能代表60Hz實體螢幕每秒顯示120張；未支援時保持未驗證。
- ExpressionOracleR1真實原生動作240時間點：原各分支全骨架snapshot／逐分支palette發布與新按需snapshot／最終單次發布的所有Transform位置、旋轉、縮放及worlds完全相同。fixture平均0.257130ms→0.043150ms；七項原Expression測試通過。完整角色幀效能仍須下一輪測量。
- TransportR1：Film、MaskParity、Scene三項通過；新增退化面夾具未初始化造成ConservativeTransport斷言失敗，已修正重跑待驗。不能將此輪列為四項通過。

- R27完整建置與TransportR2四項通過，含130面僅2個濕面、完整eager／按需sparse的逐格厚度及質量exact對照、姿勢revision改變、原引擎barycentric對照與零面fallback。
- PerformanceR11可見正常RHI、無截圖：乾燥7.497ms／133.386 Present/s首次通過該階段預算；追視語音8.086／123.664、腹部8.274／120.863、噴射10.788／92.708、停噴10.997／90.947，最大均9.486–31.014ms。DXGI實測成功，五階段整体未達8ms與120fps。
- 新合併LBS streams單一palette／influence walk，保留原float累加與fallback。CombinedSkinR1涵蓋257頂點、缺失權重／骨索引、零／負權重、空palette、不同normal／tangent長度，与原三條路徑exact相同；CombinedSkinOracleR1真實148 palette／44480頂點對舊geometry通過。8 worker私人動力池NativePoolR1與序列30幀／三種dt的粒子及頂點資料完全相同；尚未將8 worker設為預設，待量測。

- R28/R29/R30/R31完整Editor target建置成功。SourcesR1的79,219頂點序列／平行來源轉換exact對照通過，2.039198ms／0.933498ms；StreamUpload命名錯誤已於CoatingR1以正確namespace補跑成功。
- PerformanceR12可見正常RHI、8 worker：五階段平均7.212／8.971／8.710／9.808／10.138ms，Present138.658／111.483／114.796／101.975／98.656每秒，仍未達完整預算，未改worker預設。PerformanceR13保留完整PlayerController平均8.667／8.523／9.634／12.367／16.635ms；有其他UE程序重疊，不能將差異視為最佳化結論。
- R30塗層改用既有compact animated streams，保持UV／顏色與拓樸變更；CoatingR1五項Scene、ConservativeTransport、AnimationCatalog.StreamUpload、Camera.KksMouse、Interaction.RestoreControls全成功。獨立移動角色的stock／compact塗層沉積、剩餘體積、位置／法線／切線／UV／色彩／indices／bounds完全相同。下一步FunctionalR7及獨立可見PerformanceR14，未宣稱8ms通過。

- FunctionalR7完整PlayerController通過54嘴型／實際語音／手動覆寫／腹部／姿勢／水槍79命中31.6沉積／返回重進；已準備姿勢提交4.494ms，Painted.png人工檢查塗層確實在衣服上。截圖最大653.953ms屬功能記錄，不替代无截圖性能檢查。
- PerformanceR14正常可見RHI、完整控制器、無其他UE程序：乾燥7.274ms／137.481 Present/s、追視語音7.818／127.906、腹部7.913／126.378，三階段通過；持續噴射9.963／100.379、停噴11.544／86.553未通過，五階段最大11.946–60.657ms均<300ms。所有資料保留，整體120fps未通過。下一步同幀臉部提交合併與mask chart lookup重用，保持物理固定步長及原float順序。
- 配對角色冷載入需建立Session及材質，不能在live互動中同步做。展示中的按鈕明確返回載入場景，於準備流程載入第二角色；已有角色的配對姿勢仍能播放。無特定卡片分支，沒有在live頁面執行冷資源重建。

- R32完整target建置成功；BatchR1四項RenderedMouth、ConservativeTransport、Scene、RestoreControls全部成功。54嘴型實際語音2,430步／15,202,080頂點與權重觀察，批次／原逐次提交exact相同；濕chart直接存取保留原transport逐格資料與守恆。
- PerformanceR15 4 worker平均8.824／8.734／9.031／11.960／12.255ms未達；不採用4 worker作預設。R16其他UE重疊平均10.399／10.085／10.546／13.573／14.600；R17 8 worker平均7.424／7.797／7.744／9.896／10.917，曾重疊外部UE，不能作正式比較。所有最大均<300ms，整体未達。
- TraceR3僅定位，不作速度資格：Game Thread KK_NativeAnimation約4.201ms／PreviewSkinDisplay1.518ms／Streams1.141ms；profile會大量輸出，另跑無profile驗收。新增DynamicBone暫存配置重用的ChainScratchR2：21實際rig／72鏈／120幀，多種dt、移動placement，粒子與worlds對原獨立map exact相同。

- R33完整target建置69.90秒成功；ChainScratchR3涵蓋restore／animated-pose兩種solver模式、21rig／72鏈／120幀，粒子與worlds逐值exact一致。FunctionalR8驗證新2倍速時鐘、暫停／繼續、重播，加上54嘴型／語音／腹部／水槍／返回重進成功，動作提交4.611ms。
- PerformanceR18工具捕捉外部UE PID84900、14444於13:34:57、13:35:07啟動；報告正確標示external_unreal_observed=1、performance_evidence_isolated=0。五階段8.731／9.135／10.551／17.197／23.211ms僅留作負載證據，最大105.276ms；不作8ms資格。
- R35完整target建置5.98秒成功，已修正共用scheduler明確不可複製。PaletteR1七項全部成功：ExactPaletteMath、SignedPalette、RenderedMouth、RestoreControls、SpeechCache、ConservativeTransport、Scene；預轉換palette與原每influence轉換保留負／零／微小／非有限權重及無效索引的有效性與每個矩陣係數。54嘴型实际語音2,430步／15,202,080頂點權重誤差0。
- 性能審核新增第六階段：保留污漬後每0.25秒切換準備完成的姿勢；第五階段仍完整記錄停噴成本，沒有拿動作切換替代原情境。追視／語音階段另加連續嘴型選擇。正式資格需六階段全通過。

- FunctionalR9全功能成功，準備姿勢提交4.374ms。PerformanceR19獨立可見六階段平均7.219／7.796／8.015／9.675／11.156／14.617ms，Present138.529／128.256／124.773／103.369／89.574／68.362每秒；34次即時姿勢切換最大提交7.562ms，全幀最大47.158ms。external_unreal_observed=0，仍未通過整體8ms／120fps。
- R36完整target建置12.93秒成功。新增水滴查詢批次：GT擷取可見表面後各水滴平行遍歷不可變BVH，保留原反向更新、世界碰撞、沉積、亂數與物理時間順序；原逐顆路徑仍作oracle。新增64條查詢的batch／原BVH／全三角形比對，stock／compact塗層案例強制對照查詢後端。PacketSweepsR1、FunctionalR10、PerformanceR20依序驗證中，不先宣稱通過。

- PacketSweepsR1四項全部成功；64條captured parallel／原BVH／全三角形查詢與塗層物理、頂點對照一致。FunctionalR10成功，54嘴型、實際語音、手動覆寫、倍速／暫停／重播、82命中32.8沉積、返回重進；最大控制6.196ms。
- PerformanceR20獨立可見六階段7.229／8.518／9.236／10.487／10.927／14.254ms，34切換最大7.277ms、全幀最大39.932ms；仍未通過8ms／120fps，不能宣稱批次查詢已改善整體速度。另跑TraceR4定位；trace不作效能资格。
- 下一步：塗層貼圖按dirty chart水平連續段、相同段垂直合併，上傳矩形最多256個，碎片超限回退原4x4塊；保留完整half-float內容与ping-pong时序。GPU atlas逐texel在噴射／停噴後與原上傳後端比較待跑。DynamicBone每步從完整當前Transform parent inventory統計子節點，避免每粒子掃描／排序全骨架；舊掃描仍作獨立oracle，真實21rig／72鏈／120幀逐值驗證中。

- ChildCountsR1真實21rig／72鏈／120幀、多dt、restore與animated模式，粒子及worlds逐值一致。R37完整target建置61.02秒成功。
- SparseUploadsR1四項RestoreControls、ConservativeTransport、Scene、RenderedMouth全成功。新／原GPU atlas在噴射與停噴後每個half-float texel完全相同；54嘴型語音oracle仍誤差0。FunctionalR11全功能成功，79命中31.6沉積、最大控制5.717ms；正式PerformanceR21待讀結果。
- 続續處理骨骼cache失效：只清除實際改動節點及其後代，保留未改祖先／獨立分支，所有矩陣與原quat运算順序不變。每步重建當前完整parent圖，舊全clear路徑保留oracle；新增非均勻／負scale、self與0 parent、連續reparent的獨立矩陣對照待跑。
- PerformanceR21獨立可見六階段6.759／7.458／7.535／9.289／9.269／10.097ms，Present147.953／134.081／132.722／107.667／107.898／99.015每秒；34切換最大4.877ms，全幀最大33.645ms。前三階段通過，整體未通過。停止噴射與切換平均已有下降，仍保留所有原數據。

- WorldInvalidationUnitR2：非均勻／負scale、0／self parent、120次reparent及逐節點位置／旋轉改動，全clear／子樹失效矩陣exact一致。WorldInvalidationR1、R2真實21rig／72鏈／120幀逐粒子與worlds一致；R2包含有效dt才延後中間placement-world發布，dt0保留立即发布。DeferredAttachmentR1完整祖先鏈shear／60幀物理与旧发布路径exact一致。R38完整target建置中。
- R38完整target建置39.36秒成功。下一輪法線按被塗層引用的頂點計算，使用固定拓樸的incident-face CSR保留原三角形／edge累加順序及退化重複索引；按幾何revision逐頂點失效，原完整表面累加保留獨立oracle。來源骨架、衣服、污漬與物理品質不降低，待R39建置及實際塗層對照。

- FunctionalR12全功能成功，最大控制4.505ms。PerformanceR22独立可見默认worker：六階段6.023／6.712／6.687／8.487／9.976／9.049ms；34切換最大5.192ms、最大整幀29.209ms，未全通過。R23独立8 worker为5.763／6.649／6.566／9.386／10.163／10.027ms，最重階段更差；不改默认worker。
- 檢查發現live聲線選單仍同步讀取一般對話JSON及查音檔存在。改成背景catalog准备、序列解析与取消／角色／generation驗證，GT只提交完整列表，再走既有PCM預載。新增連續切兩個聲線、取消舊結果、實際播放最後準備聲線的功能案例。R39包含按需法線及聲線目錄準備，建置／逐值回歸／實際功能待驗。

- R40完整target建置6.84秒成功。SparseNormalsR1七項全部成功：Camera.KksMouse、Interaction.RestoreControls、Interaction.SpeechCache、Paint.ConservativeTransport、Paint.Scene、Speech.Core、Speech.RenderedMouth。原完整法線／按需incident-face法線誤差0；實際塗層全部vertex attrs／bounds一致，原／新GPU atlas逐half-float texel一致；54嘴型／語音2,430步與15M資料觀察仍exact。
- FunctionalR13全功能成功，包含兩次連續聲線選擇、取消過期catalog及最後聲線實際播放（voice_catalog_cancel_and_replacement_pass=1），79命中31.6沉積、最大同步控制4.141ms，返回及重進成功。這是當前功能驗證；PerformanceR24及最終矩陣未完成前不標示全案验收。
- PerformanceR24獨立可見六階段5.839／6.675／6.603／8.139／8.061／9.006ms；34切換最大4.892ms、整幀最大26.250ms。接近但仍未全通過8ms，保留不通過標記。下一步共用服裝rig復原沿用完整inventory驗證的配置重用，保留原full clone作oracle，增加拓樸新增／刪除、reparent、bridge與粒子歷史逐值檢查。TraceR5只定位剩餘瓶頸。
- SharedPoseR1包含64節點、共用bridge、120幀、拓樸增刪與reparent，完整Skeleton、粒子歷史與worlds對舊full clone exact一致。OrdinaryR1共13項通過、6項外部夾具忽略，沒有失敗。追加真實原生coordinated clothes的120幀Rig／粒子／顯示骨架oracle進行中。



## 2026-10-06 最新範圍：APNG runtime 入口與10秒限制

先保留 R40 已驗證最佳化，優先修正實際APNG入口，後續效能工作亦以APNG測量為準。正式流程：讀取任意含角色資料的 APNG → runtime 建立角色私有骨架、蒙皮、物理及顯示 → 可操作互動場景；只允許共用原始動畫、材質與語音資源，不依賴任何角色專屬離線烘焙。

1. 修正測試工具固定原生入口的問題，APNG 模式清空 GameRoot 並驗證 Session.IsPortable。
2. 從載入請求至場景與控制可用計時，冷啟動不得超過10000ms；不以逾時截斷、隱藏載入或快取命中冒充達標。
3. 必要角色解碼與貼圖準備在工作執行緒完成；進場後背景準備共用動畫曲線與語音，不阻塞進場。UObject 存取留在 Game Thread，背景只讀不可變資料。
4. 動作使用原始共用曲線；角色映射及私有物理 runtime 處理，切換保留角色物理生命週期。只把已準備完成的動作開放即時選取。
5. 驗證 APNG 進場、嘴型／語音、追視、腹部、姿勢及水槍完整流程，再量測六階段全幀。R40 原生 PerformanceR24 為5.839／6.675／6.603／8.139／8.061／9.006ms，尚未全數達8ms，不作 APNG 達標證據。


## R46 APNG runtime 實作與當前驗證

- PrepareMotions 對 portable session 使用共用原始曲線，不读取 KKS GameRoot；Game Thread 只擷取資源，背景驗證／建立共用不可變解析快取，角色映射與私有物理由 session runtime 處理。
- 材質準備增加非阻塞模式：scene 與控制先可用，shader compiler 在進場後继续工作；動作準備狀態包含顯示 readiness，未完成不開放姿勢選取。入口與所有姿勢可用分開計時，沒有把兩者混稱為載入完成。
- 語音目錄與PCM在進場後背景準備；使用共用聲線資料，沒有角色專屬預解碼PCM依賴。返回／改聲線取消過期結果。
- 共享曲線同內容快取只在已驗證快取命中時省略重解析；重選不 reset 私有 DynamicBone 歷史。PortableSharedHistoryR1 使用 APNG 與共用 walk_01.okanim，21組私有粒子逐值不變，1項通過。SharedClipR2 無遊戲檔案取樣及損毀資料兩項通過，1項原生來源oracle未執行。
- 完整 Editor R46 建置成功。ApngFunctionalR3 C001 通過：入口5593.582ms（尚是同步入口準備版本）、首次姿勢172.623ms、54嘴型、實際語音、聲線取消與替換、83次命中／33.2沉積、返回重進。
- 最終非阻塞版本 ApngFunctionalLargeR2 C036（127276802 bytes）通過：入口4483.003ms，進場時display仍在背景準備，請求至首個可選姿勢6148.268ms；最大同步控制195.879ms，54嘴型、實際語音、聲線切換、79次命中／31.6沉積、返回重進。未依角色名稱改寫。
- 功能測試包含截圖，C036記錄全幀最大855.427ms，不作300ms卡頓或120fps效能驗收證據。六階段無截圖APNG性能測量另行執行。

ApngFunctionalVariantR1 C055（92615929 bytes）完整流程通過：入口2603.764ms，進場時display仍在背景準備，請求至首個可選姿勢7284.345ms；最大同步控制95.725ms，54嘴型、實際語音、声線取消替換、79次命中／31.6沉積、返回重進。材質約4.7秒背景完成而沒有把進場阻塞到完成；與C036共同驗證非阻塞進場。


## ApngPerformanceR1 與回歸結果（R46）

C001、1280×720、正常RHI／音訊／PlayerController、可見視窗、無截圖、GameRoot空。進場4128.336ms、首次姿勢170.442ms；34次已準備動作切換，無其他UE重疊、依賴DLL hash一致。

|階段|全幀平均ms|最大ms|DXGI Present提交Hz|8ms平均通過|
|---|---:|---:|---:|---|
|乾燥動畫|6.940|9.380|144.084|是|
|追視／語音／嘴型|7.338|11.050|136.260|是|
|腹部操作|7.223|11.214|138.440|是|
|連續噴水|8.467|28.017|118.102|否|
|停噴保留污漬|8.732|21.114|114.479|否|
|污漬下連續姿勢切換|11.283|112.160|88.614|否|

300ms停頓上限於這次無截圖測量內通過；全場120fps與每階段平均8ms尚未通過，不能結案為完整性能驗收。後續以APNG的動畫／蒙皮與有污漬時的GT成本為主要工作，不再以原生遊戲目錄結果替代。

ApngRegressionR1七項全部通過：Camera.KksMouse、Interaction.RestoreControls、Interaction.SpeechCache、Paint.ConservativeTransport、Paint.Scene、Speech.Core、Speech.RenderedMouth。保留角色顯示／液體守恆／GPU像素與54嘴型逐值對照，未降低畫質、刪除服裝或改以NullRHI測量。

當前入口、資源生命週期及功能已验证成功；完整卡片矩陣與最終性能／聲畫驗收仍保留在最後。測試APNG均為唯讀，沒有產生任何卡片專屬烘焙結果；只有共用材質shader的正常引擎快取。

## 接續計畫：APNG 即時效能通用最佳化

1. 在正式 APNG 路徑擷取噴水、停噴與切換動作的 CPU trace，分開動畫求值、私有物理、蒙皮提交、污漬更新及等待，依量測排序。
2. 優先消除每幀重建、重複查詢與可重用資料配置。所有角色映射／快取 runtime 建立，完整拓撲與內容改變時正確失效；不使用角色名稱或離線角色資源。
3. 每個改動先以原有路徑逐值對照驗證動畫／物理／嘴型或水槍守恆，再建置及確認當前功能成功。
4. 背景準備保持可取消，進場不超10秒；姿勢切換不超300ms。不得關閉必要更新、降低品質或移除服裝來達標。
5. 以無截圖 APNG 六階段量測比較，只有平均全幀不超8ms且可見 Present至少120Hz才標示性能通過；完整代表卡片矩陣最後驗收。
6. 所有失敗、原因、修正與防重犯規則追加錯誤MD；不再向使用者要求確認。

R47接續實作：多個 Skeletal 顯示元件使用同一 runtime 骨架姿勢時，共享 affine source-world 及可相容的 engine-world inverse 計算。快取以完整父索引／骨名／local姿勢逐值驗證，固定數量上限，保留每個mesh自己的inverse bind與UsedBones，不合格／非GameThread即退回原路徑；不得使用GFrameCounter alone判斷姿勢一致。新增開關及舊路徑矩陣逐值oracle，確認shear／負scale／collapsed carrier與姿勢或拓撲變動正確。

R50接續實作：FOKPaintMask runtime 重用解析度專用Bary／Interior chart，套用Brush、Transport、TakeDrip及Advance。只缓存最多128×128的座標；解析度改變重建，較大尺寸保持原路徑。所有世界姿勢、液體體積、seam查詢與浮點累加順序不變。Brush在worker上只讀已準備座標，不能在worker中懶惰修改同一mask。ConservativeTransport以快取1／舊路徑0逐值比較，另測解析度改變及大尺寸fallback。

## R52 當前改動功能驗證

SharedCacheR3七項全部通過：SharedAffinePose、StreamUpload、RestoreControls、ChartCoordinates、ConservativeTransport、Paint.Scene、Speech.RenderedMouth。54嘴型共15202080個位置觀測最大差0，變形隔離通過。

ApngAffineParityR2使用C001 portable APNG、空GameRoot、正常RHI／音訊。完整互動成功且每次affine更新均與原公式所有矩陣係數完全一致；入口4718.323ms，請求至首個Ready姿勢6548.224ms、同步提交最大184.363ms、54嘴型、實際語音、聲線取消替換、80命中／32沉積、返回重進通過。截圖功能測試不是性能驗收；其1090ms截圖尖峰不得混入無截圖正式測量。

後續仍以六階段CPU成本及無截圖Present數值決定預設最佳化是否有效；未完成最終8ms／120fps驗收。

R53接續：污漬Transport中跨三角形的唯讀路徑查詢，先平行計算結果，待worker全部完成後仍按原chart／cell順序累加體積與寫入快取；不得平行累加float而改變結果。scratch最多262144 cells，超過上限保留原查詢；姿勢／解析度／設定改變仍依既有revision失效。強制平行與原串行逐值對照，再驗證實際水槍。

R53建置成功；ParallelRoutesR1四項全部通過，強制平行vs原串行100步逐值一致，包含姿勢revision變化、液體守恆、chart與實際GPU污漬。ApngParallelFunctionalR1強制啟用完整流程通過：入口4255.828ms、首個Ready6023.441ms、同步控制最大173.785ms、54嘴型與實際語音、82次命中／32.8沉積、返回重進。

R54：portable靜態角色進場時呼叫既有StaticInteractionFrame(0)及完整顯示發布，在場景載入期間初始化當前姿勢的原始DynamicBone／服裝／配件物理。使用既有240步初始化，不刪步、不更換求解器；已存在粒子歷史不重置，已有動畫角色也不重設姿勢。此成本必須包含在10秒入口，首次live選擇不再負擔冷初始化。所有資源只在runtime角色資料中建立。

R55計畫：相容poseable元件共用leader姿勢。僅完整骨名、父索引、reference TRS、所有inverse-bind係數、元件相對transform完全一致時接受；不相容仍逐元件更新，帶獨立嘴型的skeletal元件保持原路徑。Leader的affine palette必須涵蓋所有follower的draw bones及祖先，不能僅計算dummy/body用骨。Follower取得leader的不可變affine override，保留自己的材質、幾何、可見性與bounds。初始開關預設關閉，逐值oracle與實際APNG驗證後再決定是否啟用。

R56：C001完整功能及每個follower繪圖用骨的原affine公式全係數oracle通過；15個相容元件共用，54嘴型／語音／水槍／返回通過。独立六階段6.421／6.795／6.847／8.020／7.958／10.257ms，最大119.359ms、首次190.186ms，入口4232.124ms；有改善但整場8ms未達標。R57增加draw-only骨、shear／負scale／collapsed scale、exact render palettes與bounds、inverse-bind／拓撲／transform變化拒絕及停用後解除leader的測試。

R58啟用僅限明確單一姿勢發布的Actor opt-in。展示入口保存原值並啟用，返回恢復原值、重建獨立bone arrays；原載入場景的逐元件骨骼編輯維持原所有權。不可僅靠全域CVar將所有Gallery元件變成follower。當前仍需驗證新預設與返回生命週期。

R58已驗證：ExactLeaderRegressionR1十一項全部成功。新預設C036／C055完整APNG流程（empty GameRoot）及全係數follower oracle成功，入口4713.826／1999.365ms，同步控制8.906／6.424ms，54嘴型／語音、取消替換、81／82命中、返回後解除leader並恢復骨架編輯所有權。SyncMouthPose的Body來源僅限單一姿勢發布opt-in；一般Gallery沿用第一個獨立Follower，以保留逐元件骨骼編輯语意。

## 新預設無截圖獨立效能證據（ExactLeaderDefaultPerformanceR1，R58）

|階段|全幀平均ms|最大ms|Present提交Hz|8ms平均|
|---|---:|---:|---:|---|
|乾燥動畫|6.406|15.630|156.089|通過|
|追視／語音／嘴型|6.647|14.262|150.439|通過|
|腹部操作|6.311|11.463|158.449|通過|
|連續噴水|7.358|26.452|135.893|通過|
|停噴保留污漬|7.478|13.809|133.667|通過|
|污漬下連續姿勢切換|10.317|118.089|96.915|未通過|

入口4088.741ms、首次同步179.098ms、34次動作切換；沒有其他UE、三個依賴DLL hash一致。所有即時停頓都小於300ms，但最後一階段平均與120Hz未通過。這是当前功能的性能檢查，不是最終完整卡片矩陣驗收。

## R60 接續計畫：原生骨架世界矩陣增量求值

R64調整：同DLL A/B原公式六階段6.478／6.979／6.859／8.157／7.902／10.435ms，增量矩陣6.627／7.052／6.945／8.246／8.235／10.665ms，皆無外部UE、hash前後一致；沒有正效益，撤回這項production最佳化及診斷開關。

使用者指出行走應循環。調查確認Actor保留monotonic elapsed以支援獨立layer epoch，但raw clip直接以elapsed採樣，超過duration後曲線停在尾幀。通用修正：raw preview在曲線採樣時依duration取循環phase；session elapsed／physics dt仍連續，controller各layer原clock及non-looping末端、明確f32::MAX尾幀採樣保持原語意。新增原生多周期曲線oracle，實際APNG功能audit確認走過兩個周期後腿部仍持續變化，然後重新量測正常循環播放的六階段；舊凍結raw片段的性能不得當成修正後的120fps證據。

ExactLeaderTraceR2使用R59、正式C001 APNG、可見RHI／音訊、空GameRoot；無外部UE且三個DLL hash前後一致。帶trace六階段最後平均10.602ms、首次同步165.866ms、入口4048.691ms，仍未通過性能。70～77秒區間原生動畫平均2.052ms，顯示發布2.369ms；此區間作成本分析，無traceR58仍是既有性能基準。

1. 在現有runtime WorldPlan內保存各節點local TRS的float位元與已算出的世界矩陣；完整拓撲、source_mesh及輸出骨索引驗證維持原規則。
2. local TRS位元或祖先求值有變化才重算；使用原trs／mat_mul順序，沒有近似epsilon、跳幀或品質降低。不同角色即使拓撲相同，也要逐節點驗證TRS，不能依source或frame識別替代。
3. 計畫從最近使用項開始搜尋，保留128項上限；只有runtime記憶體資料，沒有角色離線資源。
4. 先測逐位元矩陣oracle：重複求值、局部／祖先變動、交錯角色、signed zero、負／零scale、missing parent、輸出重排、拓撲變更及cache淘汰。再建置、APNG當前功能檢查及无trace性能比較；若成本退步，不保留無效最佳化。

## R59 當前功能驗證完成

R61接續：共用曲線選取目前每次都枚舉asset registry並逐一GetAsset，會在姿勢切換的Game Thread載入無關共用資源。增加最多256筆、只保存key與object path的runtime查詢快取；命中時重新確認registry存在、實際型別與Source／Clip，失效則依原順序重新搜尋。所有UObject存取仍限Game Thread，不pin角色或共用曲線、不新增離線資料。實際APNG選取、頁面背景準備及取消流程驗證後比較同步切換成本。

R62實作完成並建置成功：WorldPlan另保留local matrix，祖先變動但本節點TRS不變時只做原world乘法，不重算相同trs。相關原生測試五項全部通過（另兩項明確ignore），120次不同pose／同拓撲角色交錯及原world_of逐位元對照通過；重複相同pose不重新求值任何節點。共用曲線lookup最多256筆，不持有UObject，命中仍校驗registry與實際Source／Clip。正在跑APNG無trace六階段、全係數affine oracle功能流程及十一項回歸；性能效益與當前功能成功需等此次結果，不以synthetic通過推論。

R64建置成功。原生RawPreviewLoop十二周期相同phase曲線對照通過，六項clock及四項world測試通過；外部fixture測試明確ignore，不宣稱完整原生suite成功。RawLoopFunctionalR1：APNG C001／空GameRoot／正常RHI與音訊，全affine係數oracle及完整功能通過；walking_moves_after_two_cycles=1、54嘴型、實際語音、目錄取消／替換、83命中／33.2沉積、返回重進。入口4115.995ms，首個Ready5732.203ms，同步187.637ms；截圖942.566ms不屬於live性能證據。曲線存在性檢查也改為驗證單一快取path，避免HasAnimationClip在每次切換枚舉整個共用曲線目錄；沒有新增角色專屬資源。

## R64 正常循環播放的當前功能與性能檢查

RawLoopRegressionR1十一項全部成功，涵蓋ExactLeaderPose、SharedAffinePose、StreamUpload、KksMouse、RestoreControls、SpeechCache、ChartCoordinates、ConservativeTransport、Paint.Scene、Speech.Core、Speech.RenderedMouth。功能與回歸各自三個DLL hash前後一致；diff whitespace檢查通過。

RawLoopPerformanceR1：正式C001 APNG、空GameRoot、正常可見RHI／音訊／PlayerController、無trace與截圖、沒有其他UE，三個DLL hash一致。入口4006.993ms、同步控制最大194.212ms、34次已準備動作切換；全幀最大50.943ms，300ms停頓與10秒入口通過。

|階段|全幀平均ms|最大ms|Present提交Hz|8ms平均|
|---|---:|---:|---:|---|
|正常循環行走|7.105|13.044|140.750|通過|
|追視／語音／嘴型|7.404|15.212|135.070|通過|
|腹部操作|7.623|12.618|131.193|通過|
|持續噴水|9.720|27.489|102.893|未通過|
|停噴保留污漬|10.000|25.519|99.930|未通過|
|污漬下連續姿勢切換|11.378|50.943|87.801|未通過|

目前功能已確認實作成功，行走循環修正已由實際動作而非flag驗證。完整120fps仍未達標；正常循環會持续改變接觸幾何／污漬附著，最後階段水槍自身平均2.228ms（geometry0.423／simulation0.957／coating0.819），GT11.211ms、GPU1.189ms。後續性能調查以此正常動作基準處理CPU動畫與液體更新，不能回到凍結raw動作、減少必要物理或刪服裝來取得較快數字。

R59建置成功。ExactLeaderFinalRegressionR1十一項全部通過：ExactLeaderPose、SharedAffinePose、StreamUpload、KksMouse、RestoreControls、SpeechCache、ChartCoordinates、ConservativeTransport、Paint.Scene、Speech.Core、Speech.RenderedMouth。涵蓋54嘴型逐值／渲染對照、相容leader矩陣及bounds、拓撲／inverse-bind／transform變動拒絕、停用後恢復獨立骨架、液體守恆與平行查詢逐值一致。

ExactLeaderFinalFunctionalR1：C001從APNG讀取、GameRoot空、正常RHI與實際音訊，完整流程成功。入口4056.521ms（≤10秒），進場時允許背景顯示準備，請求到首個Ready姿勢5746.165ms；同步控制最大175.421ms（≤300ms）。54嘴型、實際語音、聲線目錄取消與替換、83次水槍命中／33.2沉積、返回及重進、恢復原pose-sharing所有權皆通過。回歸及功能測試的三個依賴DLL hash各自前後一致。

功能測試有截圖，1000.093ms全幀尖峰包含截圖成本，不能當成live性能證據，也不能略去後宣稱120fps。性能判定繼續使用上方無截圖獨立六階段測量。當前實作成功；完整性能與卡片矩陣驗收尚未完成。

剩餘工作依量測處理污漬下切換動作的Game Thread成本：R58最後階段GT平均10.225ms、GPU1.130ms、GT等待0.091ms，水槍自身平均1.203ms。不能直接將全部差額歸因於水槍；需針對同一階段的姿勢求值、私有物理、顯示提交及污漬重附著分段量測，再選擇保持同幀結果與資源生命週期的通用改動。舊ApngTraceR1有外部UE重疊，只能作呼叫結構參考，不能作新最佳化效益或達標證據。

## R65：展示場景新增男性與原生男女互動（2026-10-06）

使用者指定效能優化先停在 R64；本次先完成配對功能及功能驗收，不宣稱雙人達到 120fps。

1. 在展示場景提供男性卡片路徑、目錄男性選單、載入／替換、上一／下一姿勢及停止配對。APNG 使用空 GameRoot 直接開啟；不返回 Gallery。
2. 共用來源資源保存原生 H 動畫控制器、AnimationInfo、MotionIK、Avatar 與工廠骨架。提供可重現的安裝腳本與 NonUFS 打包設定；不存女角或男角的預算姿勢。資源存放於 Content/IndependentResources/PairedAnimations，與使用者遊戲目錄分離。
3. Portable Session 增加共用控制器來源通道：查目錄不改變正在播放的單人來源；選擇雙人動畫時才切換來源，回到單人時恢復共用原始曲線通道。
4. 男性 Session 開啟及材質解碼在 worker 完成；GameThread 生成 actor、提交並保留原角色直至新角色成功。generation 檢查取消返回／重載產生的舊結果，失敗保留原配對。
5. 沿用原場景精確角色控制器、共同階段、單一 elapsed 時鐘、層組合、身高參數及雙方接觸修正；保留眼睛／頭部、嘴型／語音、肚子與水槍。
6. 驗收 APNG 女角＋APNG 男角、空 GameRoot、兩方動作同步且持续變化、不同姿勢與階段、暫停／重播／停止／返回；原場景相關回歸。編譯與遇到的錯誤記錄到 errors MD。


### R65 最終實作與功能證據

- 展示場景直接輸入男性 APNG 完整路徑，或從相同卡片目錄選男角，再按「載入／替換男性角色」。留空路徑時使用男角選單。無須離開展示場景。上一／下一姿勢、共同階段、速度、暫停／重播、停止配對沿用原場景。
- 共用來源由 `Tools/prepare_paired_animation_resources.ps1 -SourceRoot <來源目錄>` 準備，21 個原始共用檔案（約 2.53 GB），本機已安裝；來源資料不提交 Git。NonUFS 打包包含該資料夾。共用資料含原生控制器／Avatar／MotionIK／工廠骨架，不含角色專屬預算姿勢。首次控制器及片段仍在背景解析，完成前保留現有動作；不宣稱尚未準備的動作能即時切換。
- 男性開檔及材質解碼為 worker 私有 Session；最多一個載入工作，快速修改只保留最後一次要求。成功生成後才替換舊配對，錯誤保留舊角色。返回／停止／重載用 generation 取消發布並清掉佇列。材質以非阻塞 readiness 準備，雙方完成後才提交。
- 男性驗收卡 `Saved/TestCards/InteractionMaleAPNGR65.png` 是實際四視角 APNG：768×1024，4 frames，2/1/1/1 秒，循環播放；角色資料由既有通用 portable/shared 轉換流程產生，測試程式沒有特定角色分支。
- `PairFunctionalR65R3`（可見 1280×720、正常 RHI／audio、女角 C001＋男性 APNG、空 GameRoot）`functional_pass=1`。進場 5912.078ms，首個已準備單人動作 7543.901ms；行走兩周期後仍變化、54嘴型、真實語音、水槍 82 次命中／32.8 沉積通過。
- 配對資料 99 個姿勢。實測兩個姿勢的兩方骨骼持續變化，elapsed 同步；階段切換比對實際提交到 coordinator 的雙方 key，並非只驗證選單文字。暫停／重播、双人時的頭部／手動嘴型／肚子控制、最新男性選擇佇列、有效替換、錯誤替換保留舊配對、返回等待 worker 完成且不發布過期角色、重進通過。
- 原生 FFI 針對無效句柄／來源的測試 1 PASS。建置 `InteractionBuildR65R7.log` PASS。測試截圖造成的幀峰值不代表性能實測；本次未跑 120fps 效能資格測試。原先 R64 六情境性能結論維持，未達標項目沒有改標通過。

- R65R3 配對提交時間（ms）：297.502, 77.002, 56.922, 165.061；最大 297.502。這僅代表本次已準備動畫提交，不包含男角初次 LoadSession／背景解析／截图，亦不代表完整 ≤0.3 秒保證。背景首次動畫仍可能等待十多秒。詳細證據：`Saved/Tests/InteractionShowcase/PairFunctionalR65R3/report.txt`、`pair-timing-summary.json` 與 `run.log`。

- 最終回歸 PairRegressionR65Final：7 PASS（Gallery.LoadSelection、Interaction.RestoreControls、Interaction.SpeechCache、Camera.KksMouse、Paint.Scene、Speech.Core、Speech.RenderedMouth）；所有測試期間三個 DLL hash 保持一致。已目視檢查 Paired.png，兩個角色與雙人姿勢控制均有顯示。功能驗收完成；完整卡片／姿勢矩陣及120fps驗收仍依原計畫延後。

## R66：雙人切換 <=300ms
1. 指標改量測從點選至 coordinator 提交並播放的完整延遲，加上切換區間最大完整幀時間；不把 background worker 排除於切換時間。
2. 進場後 background 逐姿勢準備雙方原生控制器、共同階段、所有 blend tree 葉片與 IK。只有已準備完的姿勢／階段開放選擇，明示準備進度，未準備項目不假裝能即時切換。保持 <=10秒入場。
3. cache共用 IK config／MotionIK 來源索引，避免每個動作重讀oo_base；準備工作不持有播放所需全域鎖。
4. 點選已準備姿勢直接提交，免重開worker；保留相機、語音、私有物理及原生接觸修正。
5. APNG雙角色實機多姿勢／階段／連續往返切換，量測click-to-commit <=300ms 與切換完整幀上限；建置、回歸、錯誤更新MD。

### R66 完成與驗收

- 上述五項已完成。新增 `OKPairedPreparation.cpp`：背景逐姿勢準備雙方控制器、所有可達曲線葉片、原生層與 MotionIK；完成後發布不可變的 ready 資料，只列出真正就緒的姿勢與共同階段。材質未就緒時不開放選單切換。介面顯示 ready 數及 99 個來源的背景進度；在場景內持續準備，不阻塞入口。
- 已準備姿勢及階段直接從 ready map 提交，免再次開 worker／查控制器目錄。資料以共用資源來源為準，沒有角色名稱條件或角色專用離線姿勢；APNG 身高等角色參數仍由 native runtime 計算。換男角重用共用準備，返回取消發布，重進恢復剩餘進度，原生解碼以背景鎖避免重複並行。
- MotionIK 與 prefab IK 共用來源索引按原 metadata revision 更新；保留同名錯誤拒絕語義。factory skeleton 在無 mods 的共用資料包按完整 SHA manifest 身份快取；原生有 mods 的遊戲來源維持完整 override／內容身份驗證。資料包更新須使用安裝工具一併更新 manifest。
- 即時配對鏡頭沿用原視角／投影，以現有 runtime 骨骼／morph 包圍盒取景，免讀回 GPU 蒙皮頂點。包圍盒較保守，會留較多取景邊界；精確靜態拍照保留原逐頂點流程。
- 最終建置 `Saved/InteractionBuildR66Final.log` PASS。原生針對 hostile MotionIK、完整內容身份、同名 motion 錯誤保持的三項測試各 1 PASS。
- `PairFunctionalR66Final`：可見 1280×720、正常 RHI/audio、女角 `output/CrowdAPNG/C001.png`＋男性 APNG、空 GameRoot。`functional_pass=1`、進場 **6007.403ms**；首次已就緒配對提交 **253.787ms**。
- 12 次真實 UI callback，涵蓋 5 個姿勢、WLoop／Drink 階段與前後切換；**click-to-play 30.091～52.661ms**，切換區間 **最長完整幀 81.815ms**，全部 <=300ms。完整 callback 與 coordinator 實際雙方 key／state 比對；第一幀保留前一個 scene tick 時戳，包含切換前該幀已花費的時間。沒有 screenshot／角色載入混入切換區間。
- 正常語音、54 嘴型、水槍、雙方運動／同一 elapsed、暫停／重播、頭部／嘴型／腹部即時控制、男性替換、錯誤保留、最新要求佇列、取消返回與重進皆通過。已目視檢查即時包圍盒取景，兩角色在畫面內。
- `PairRegressionR66Final`：Camera.KksMouse、Gallery.LoadSelection、Interaction.RestoreControls、Interaction.SpeechCache、Paint.Scene、Speech.Core、Speech.RenderedMouth **7 PASS**。實機測試三個依賴 DLL hashes 一致，沒有外部 UE 程序重疊。
- 證據：`Saved/Tests/InteractionShowcase/PairFunctionalR66Final/report.txt`、`pair-timing-summary.json`、`run.log`。尚未準備的原始資源在 runtime 背景解析，本次第一個完整姿勢準備約 41.3 秒；介面可先進入並操作既有單人功能。此冷準備不是已開放姿勢的切換延遲。測試末尾 6/99 姿勢就緒，未將全部 99 姿勢矩陣或整體 120fps 標為通過。

## R67：雙人播放速度與每幀耗時
1. 檢查速度是否只改共用動畫時鐘；物理使用實際 frame delta，不隨速度增加更新次數。
2. 加入可選實機速度 audit：0／0.25／0.5／1／2／4 倍、連續拖曳，穿插 1 倍基準，記錄雙人 Advance CPU、完整幀、速度 callback 與實際 elapsed。
3. 停止新增背景準備並等待目前解碼真正結束後量測，避免冷解析混入速度比較。保留語音、接觸修正、材質與私有物理。
4. 如有倍率相關的額外工作直接修正；正常姿勢／系統噪聲不稱為零成本，以實測與固定工作量一起判讀。當前功能驗證完成後記錄結果與錯誤，完整120fps矩陣留至最後。

## R68：雙人眼睛渲染
1. 固定眼睛開合、臉部樣式與頭部近景，截取追視關閉／開啟／閉眼三種雙人實際渲染，區分虹膜UV、蒙皮與眨眼問題。
2. 對照單人與雙人更新順序；合併同幀 pose、gaze、blink、speech 顯示發布，不略過原生眼睛資料或以角色分支遮掩。
3. 修正重現的問題並目視確認虹膜、眼白、閉眼與再次睜眼；重新確認速度成本與雙人切換，將失敗證據寫入錯誤MD。

R68 虹膜根因候選：共享姿勢的 UPoseableMeshComponent 不像 skeletal component 自動 RefreshFollowerComponents；跟隨元件跳過獨立 OKApplySkelPose 後，GPU render data 未收到領導者新姿勢。通用姿勢發布新增 follower bounds 更新與 RefreshFollowerComponents，不重建 render state、不讀回頂點、不做特定眼睛／角色分支。以完全不讀回的雙人眼睛對照與動畫骨骼 regression 驗證。

### R68 眼睛修正與實際渲染驗收

- 根因已用有／無診斷讀回對照確認：虹膜是共享骨骼姿勢的 poseable follower，眼白是獨立 morph skeletal component。UPoseableMeshComponent 的 RefreshBoneTransforms 不會自動 RefreshFollowerComponents；跳過 follower 自己的 pose update 時，沒有發布領導者新姿勢到 GPU。診斷讀回會切換 CPU/GPU skinning 並重建 proxy，因而掩蓋錯誤。
- OKApplySkelPose 在領導者姿勢完成後更新 follower 的 runtime bounds，再 RefreshFollowerComponents 發布動態資料與 transform。保留共用 affine palette、各 renderer 自己的材質／幾何／morph；不每幀重建 proxy、不讀回頂點、不依角色或眼睛部件分支。
- PairEyesNoReadbackR68R1 修正前的近景仍缺兩側虹膜。PairEyesSpeedR68Final 修正後，在同樣 APNG 女角＋APNG 男角、空 GameRoot、可見 1280×720、正常 RHI/audio 下，Neutral／Tracking／Closed／Reopened／Playing 五張近景已直接目視通過；Playing 明確恢復雙人動畫且頭部持續移動。沒有啟用 OK_INTERACTION_EYE_READBACK，也沒有 pair-eye-geometry.txt。
- 證據：Saved/Tests/InteractionShowcase/PairEyesSpeedR68Final/eye-visual-review.json 與 PairEyes*.png。自動 functional_pass 不包含像素判定，眼睛結論以另存的目視結果為依據，不把修正前 functional_pass=1 誤稱渲染通過。
- 速度 UI 將 0／非0 的呼吸音組切換排到目前 cue 結束，保留目前語音／嘴型且快速拖曳只保留最後一組。兩種 cue bank 在姿勢／階段切換時準備；速度 callback 不重新找音檔、解碼、停止或重建當前語音。StopHBreath 清掉 pending queue。

### R67／R68 最終當前功能驗收

- Saved/InteractionBuildR68Final.log：完整建置 PASS。正常不讀回的眼睛五張近景已通過，詳見上段與 eye-visual-review.json。
- PairSpeedR68FinalR2 使用與眼睛驗收完全相同的三個 DLL，functional_pass=1、audit_completed=1。APNG 進場 5948.656ms，首次已就緒雙人提交251.304ms；12次真實UI姿勢／階段切換 click-to-play 36.881～65.266ms，最長完整幀96.012ms，全部<=300ms。
- 13階段：1倍基準交錯0.25／0.5／2／4倍、連續拖曳、0倍停止，固定倍率實際共用 elapsed 正確。每階段穩定1.5秒後量至少4秒，等背景 native 解碼確實完成後才比較。所有變速階段CPU及完整幀均通過相鄰1倍基準15%＋0.5ms比較預算；這是量測容許自然姿勢／負載波動，不是宣稱每幀完全相同。
- 4倍 Advance CPU平均15.348765ms，相鄰1倍15.421482ms；完整幀21.260242ms，相鄰1倍21.434837ms。0.5倍CPU18.110740ms，相鄰1倍17.556179ms（約+3.16%）；前輪0.5倍尖峰未重現，前輪失敗仍完整保存。68次真實速度callback最大0.051402ms，從原10.7705ms改善；變速不增加native sample／physics／render更新次數，物理仍用未乘速實際delta。
- 男性替換、錯誤替換保留、返回取消過期載入、重新進場、兩方時鐘／持續運動、暫停重播、54嘴型／語音、頭部／肚子／水槍功能通過。R2測試三DLL首尾SHA256一致，未觀察到外部UE程序重疊。
- PairRegressionR68Final：Gallery.LoadSelection、Interaction.RestoreControls、Interaction.SpeechCache、Camera.KksMouse、Paint.Scene、Speech.Core、Speech.RenderedMouth、AnimationCatalog.ExactLeaderPose、AnimationCatalog.SharedAffinePose，9 PASS；測試DLL hashes穩定。
- 證據：Saved/Tests/InteractionShowcase/PairSpeedR68FinalR2/report.txt、module-hashes.json，以及 PairEyesSpeedR68Final/PairEyes*.png／eye-visual-review.json、PairRegressionR68Final/run.log。診斷讀回與速度比較失败已記入 errors MD。
- 當前眼睛／速度控制／已準備姿勢切換驗收完成；整體平均8ms／120fps與99姿勢／全角色矩陣仍未標成通過。

## R69：Profile 雙人播放並優化到 120fps
1. 增加專用雙人 profile 入口：實際 APNG 女角＋男性、空 GameRoot、正常畫質與音訊，兩個就緒姿勢各15秒基準；取消新背景準備並等待目前解碼真正完成，另以5秒詳細 profile 找瓶頸。
2. 同時記錄完整幀平均／P95／P99／最慢幀、GT／Render／RHI／GPU／等待與真正DXGI Present頻率；Unreal Insights追蹤 native pair、骨骼發布、CPU配件、臉部嘴型、陰影Capture、render proxy與GPU。
3. 依inclusive／exclusive CPU成本與每幀工作次數優先修正重複或不必要的工作；保留原生物理、雙人接觸、追視、嘴型語音、陰影與即時控制，不靠降低畫質、跳動畫幀或停用必要功能。
4. 以同一卡片／姿勢／畫質重測，關閉逐幀log與trace做最終幀率驗收；保持APNG進場<=10秒、已就緒姿勢切換<=300ms，回歸眼睛五種渲染與既有九項測試。
5. 記錄瓶頸、前後數據、所有失敗及尚未達標項；CPU動畫時間不冒充120fps，整體平均<=8ms並實際Present>=120才列為達標。

R69 baseline R2：兩姿勢完整幀17.643／17.805ms，實際Present56.68／56.17fps；GT17.427／17.590ms、Render11.958／12.112ms、GPU1.637／1.655ms，主要限制在CPU而非GPU。初次全區間trace含背景準備，不能把其均值當穩定播放；先優化每幀 facial proxy重建、MotionIK全狀態複製與每個target的整份snapshot複製，再做界定穩定區間的完整CPU事件exclusive分析。

R69 穩定區間80～110秒、全CPU timers 的exclusive分析：1694次配對更新，native平均7.312ms、每角色display2.415ms、shadow1.153ms；RenderThread 0 的KK_StreamUpload exclusive共9273.872ms（約5.475ms／配對幀）。這些CPU執行／跨thread等待不能與GPU timer混加；優先消除骨架／proxy重建和資料複製，後續需處理procedural streams傳輸。

R69 R1：第一姿勢完整幀17.214ms／58.09fps，第二姿勢21.663ms／46.17fps，未達標。下一批改動：播放陰影同幀deferred capture（拍照維持同步）、兩角色取樣及私人物理共用最多4個native workers、procedural tangent完全相同的量化提前到既有packing jobs，RT改為連續記憶體複製；接觸barrier與陰影／頂點／嘴型回歸不可省略。

R69 R5／R6通用實作：generated geometry直接寫入owned CPU section並在同一worker job量化GPU TBN，省略中間positions/normals/tangents複製；可用ok.Animation.GeneratedStreams=0回到原陣列路徑驗證。兼容morph renderer共用exact leader，但各自保持morph overrides與bounds，非相同reference／inverse bind／relative transform或第三方anim instance退回原路徑。雙人actor取得owned renderer tick ownership，每幀仍完整sample/contact/physics/gaze/mouth/render，Stop恢復各component原tick；自己的morph overrides當幀發布。語音索引改thread-safe immutable snapshots，依path/size/mtime失效，在已有背景pair準備中預載，不生成每角色專用資源。
R69 R5（有其他UE重疊）完整幀15.288／14.900ms，Present65.416／67.113，Advance11.420／11.113，Render6.687／6.542，GPU1.483／1.467；仍未達8ms/120FPS。R69R6Dependencies本地四模組build PASS，PairRegressionR69R6Retry 13項PASS（原9＋stream／affine rigid／HBreath／immutable cache并行及revision），四DLL hashes首尾一致。APNG eyes／速度／功能與最終R6幀率仍待驗收。

R69 R6正常RHI無trace：兩姿勢完整11.401／14.630ms，Present87.709／68.351，Advance8.775／11.126，Render4.810／6.485，GPU1.416／1.430；沒有外部UE紀錄，APNG控制可用6066.532ms，首次提交437.439ms仍超300（playback430.115，framing0.423，speech6.766），後續須修正首次角色獨立frame／scene mode分類。R69 R7：雙人先設定SceneMode3、選好兩控制器再只發布一次接觸後完整frame；接觸前一次凍結所需target／bend／root transforms，省完整骨架snapshot複製，test與原clone snapshot逐值比對；post correction統一發布worlds，FFI完整frame只在最後physics後輸出骨架。R7四本地模組建置PASS77秒，等待64幀native新oracle、正常眼睛近景與最終功能／效能驗收。

R69 R8當前功能：PairEyesR69R8Retry2正常RHI/audio、APNG兩角色、空GameRoot，functional_pass=1／audit_completed=1，四DLL首尾hash一致。APNG controls6970.804ms；已準備單人首提交295.756ms、雙人首提交107.969ms，12次雙人姿勢／階段切換19.602～42.082ms、最長完整切換幀70.020ms，<=300ms。54嘴型、真實語音、行走兩周期持續移動、水槍80命中／32沉積、男性替換及失敗保留、取消返回／重進、live頭部／嘴型／腹部全部通過。正常無頂點讀回Neutral／Tracking／Closed／Reopened／Playing五張眼睛圖逐張目視通過，記錄eye-visual-review.json。功能流程平均9.221ms含不同模式與載入截圖，不當成穩定雙人FPS。R7新凍結target／bend oracle及serial/parallel私人物理64幀精確比對PASS（InteractionRustR69FrozenR2.log）。R8四本地模組build PASS29.82秒。正在同binary比較4／8 native workers正常Present，最終120FPS仍未通過。
R69ColdR2新測試PASS64幀（50.88秒），每姿勢開始強制mutable body_skel=None且保留合法rest/shaped，serial與parallel實際FFI的骨骼及私人粒子逐值相等。R10四本地模組build PASS60.26秒。SceneMode metadata選前setter與publish suppression將再以R11建置確認、直接冷進雙人正常RHI測試；test微秒數非120FPS證據。
R69最終回歸PairRegressionR69Final：13項PASS、四DLL hashes一致，包含RenderedMouth真實渲染、ExactLeaderPose／SharedAffinePose、StreamUpload generated serial/parallel GPU讀回、AffineRigidDraw、HBreath解碼與HBreathCache並行revision。R69三個MotionIK資料／content identity單元測試PASS18ignored（未將ignored算PASS）。
R69Workers8FinalR2 profile完成，但測量phase中多次額外pair commit，elapsed重置、另有R779 cook程序重疊，不能當兩固定姿勢可比較的4／8 worker證據；原report13.420／11.590ms、74.506／86.271FPS仍保存，未採用8 worker預設。首次1974.663ms包含shader readiness重試且超300，亦不稱已通過。預設4 workers正常RHI final profile重測中。

## R70：剩餘 CPU 瓶頸與完整驗收
1. 收回中斷期間的實測：R69Final完整幀12.491／17.735ms、80.066／56.387FPS，GPU1.469／2.266ms，未達8ms；冷首次370.360ms仍超300。PairEyesSpeedR69Final首次單人提交超300而失敗，不能列為速度PASS。
2. 先檢查穩定量測期間是否有意外重新提交與時鐘重置，確保固定姿勢基準有效；記錄source／state／rate與外部程序，不能把混合操作當固定姿勢對比。
3. 私有物理熱點優先檢查旋轉／signed lossy scale的重複階層計算。只採用依目前Tf與父節點資料逐值驗證的runtime快取，保留負尺度、重掛父節點、同地址重用與浮點運算順序；以舊遞迴oracle與64幀真實APNG粒子比對驗證。
4. 實作前後以相同輸入做微量測，無收益的方案不進正式路徑。再跑正常RHI雙人profile、冷／熱切換、54嘴型／語音／水槍、眼睛五圖、0～4倍及拖曳完整驗收，四DLL hashes一致。
5. 所有失败保留到errors MD；每角色計算仍在runtime，無角色專屬預製檔，不靠停用物理／眼睛／陰影或跳更新取得FPS。
R70碰撞保守bounds微量測兩輪約5.7ms→4.0ms（約30%）且checksum相同，72種inside／axis／scale／signed radius組合、每組1024隨機位置及边界／非有限端點與原窄相逐bit一致PASS。採用外側capsule保守AABB剔除；inside及非有限值仍舊公式。重用root world矩陣allocation，test逐值核對舊fresh HashMap查詢。下一步真正APNG64幀與全部DynamicBone測試，微量測不當120FPS。
R70 Rust驗證：所有DynamicBone單元36 PASS／16 ignored；真正兩APNG冷初始化＋兩姿勢64幀PASS，骨骼及私人粒子exact，updateParticles2仍逐碰撞核對舊公式，root world查詢也逐值對照。InteractionRustR70Dbone.log、InteractionRustR70Cold.log。開始四本地模組建置與正常RHI測量，不把native微量測當完整幀率。

R70正常RHI兩固定姿勢：4 workers完整11.740／11.641ms、Present85.142／85.899FPS，entry6188.407ms、首次296.935ms；無外部UE且hash一致。8 workers為10.984／15.588ms、91.046／64.169FPS，首次251.877ms，後半GT/GPU抖動增加，不能只憑第一姿勢改預設。兩者皆未達120。下一步讓一次雙人驗證共享同一份已核對資源revision的MotionIK index，兩角色仍每幀驗證身份／形狀與來源，只省重複目錄掃描／metadata查詢，保持runtime資源修改當幀生效。

R70功能驗收失敗待修：PairEyesSpeedR70R1進場5933.983ms、54嘴型／語音／水槍／行走PASS，但row callback在男性材質未就緒時自動選第一姿勢，提交86.988ms卻包含約2秒shader輪詢，click-to-play2075.624ms。通用修正為只有兩角色material與曲線皆就緒後才發起自動播放，與UI可點選狀態一致；明確使用者的既有選擇仍保留原請求時間，不以重設計時隱藏等待。由runtime就緒輪詢觸發，不做每卡專用資產。

R70 worker對照補強：第二姿勢變差確認混入滑鼠噴漆工作，Audit使用既有ScriptedPaintAim明確輸入並逐窗口驗證額外命中0，再以同binary4／8／16 workers依序量測；不採用受干擾那輪數據決定預設。IndexCold64幀與原粒子／骨骼PASS36.48秒；3項MotionIK資料／identity測試PASS18ignored；四模組R2建置PASS67.36秒，Gallery readiness後續修改再增量建置確認。

使用者追加「先不要量水槍」：接下來只量雙人動畫；新增功能audit的SkipPaint開關，report明記watergun_validation_skipped=1。既有水槍項不在本輪重新驗收，正式能力保留；profile不射擊並檢查unrequested hits=0。Stage8進雙人後也保持scripted false，避免測試流程自行恢復實體按鍵而污染速度窗口。

R71下一批計畫：用正常RHI完整CPU事件trace釐清Advance外額外約3ms。原生每個配件重讀profiling／rigid cloth／skin parity／optional renderer等全域環境變數，Windows環境鎖可能序列化平行jobs；改為每次同步配件dispatch讀一次不可變policy，再傳入各job，仍每幀重讀以支援現有live開關，保留數值運算與所有renderer分支。先舊路徑／snapshot路徑逐值及真實APNG64幀驗證，量測有收益才採用。

R71 quiet trace90～93秒、283配對幀：Native4.033ms、每role display1.270ms，其中generated packing0.784ms；每role shadow0.340ms。Depth capture每幀ping-pong使所有98左右MID重綁KKShadowDepth，重新發布uniform資源。計畫對deferred SCS_SceneDepth playback使用固定target，同幀完成depth寫入再主視圖讀取；同步拍照仍ping-pong。不改capture數量／解析度／材質與陰影，新增同姿勢兩路depth GPU讀回及材質綁定／時鐘oracle，證明穩定target沒有feedback或上一幀資料後再做FPS驗收。

R71下一個streams通用优化：同一native source vertex的normal rotation只依reference normal與當幀normal，不依outline／shell section。把原FindBetweenNormals公式放進既有source conversion jobs，每source vertex只算一次，各draw section共用；face sphere後改normal的路徑保持原算式。每幀重新計算，runtime建立有界角色陣列，不做跨pose猜測cache。保留cvar原路徑，debug逐draw quaternion原公式exact對照，再確認CPU/GPU streams與正常RHI幀率。

R71正常RHI新實作（policy snapshot＋stable depth target）8workers完整10.267／10.348ms、Present97.396／96.632FPS，first264.925ms、entry6512.477ms，no paint hits／externalUE、hash一致，仍未達8ms。同步depth與deferred稳定target两角色各1048576像素exact maxError0且clocks held。採用通用worker上限依available logical threads：max(ceil(n/2),4)再限制到n與8；小機器維持既有最多4，大機器最多8，保留OK_NATIVE_WORKERS覆寫。16對照收益小、不以單一卡硬設16。Outline normal motion重用及male replacement pending修正已實作，下一版重跑full功能（SkipPaint）與原公式parity、正常profile。

R72原生物理解算計畫：world rotation只在單次chain ApplyParticles內快取，parent topology在當次solver step重建。旋轉寫入使該節點與已快取descendants失效；position写入不影響quaternion，chain與frame邊界清空。保留舊wrapper與uncached oracle，檢查live reparent／負scale／自parent／missing parent及真實APNG64幀bones與particles exact；沒有微量測收益則移除，不採用先前需逐祖先驗證的跨查詢memo。

R71R3功能完整PASS（audit_completed=1 functional_pass=1），依使用者SkipPaint：entry6185.084ms、首次双人76.653ms、12switch7.764～18.690ms、最大切換全幀34.068ms；54嘴型／語音／walking兩周期／男性替换與失敗保留／返回重進及live controls皆PASS。正常GPU五張眼睛圖逐張目視，review JSON保存。NormalMotionParity逐draw与旧quaternion exact通過整個功能流程；stable shadow兩角色depth oracle PASS。13速度階段69callbacks最大0.086699ms，report CPU stable=1，但因Rustcompile短暫重疊仍將隔離後復驗。R72 scope單元14PASS8ignored，負scale／reparent／missing／self-parent與旧world/quaternion bits通過，下一步微量測和APNG每chain旧公式oracle。

R71R3正常RHI兩固定姿勢完整10.524／10.503ms、Present95.019／95.212FPS，Advance7.446／7.469ms，entry8357.650ms、首次258.831ms。額外paint hits0、externalUE0、四hash一致；仍未達120。normal motion共用尚無明確收益，須同binary Cache0対照後決定。R72 scope微量測deep-chain兩輪uncached約290ms／cached116ms（同quat checksum）；只代表局部熱點，64frame真APNG每chain与旧ApplyParticles逐bitoracle正在跑。

R71同binary normal cache off對照：完整10.110／10.386ms、Present98.907／96.284FPS，比粗略cache on10.524／10.503較好；entry6451.345ms、first228.559ms、no paint／externalUE0/hash一致。确认全part预计算不可直接採用。R72实际draw multiplicity修正已实现，只有>=2消费的source vertex才预算，其他保持旧公式。R72 ScopeCold每chain旧ApplyParticles oracle＋两APNG64frame bones/private particles exact PASS40.91秒，default worker8，无UE重叠。四模組R72Scope建置中。

使用者要求先profile找根因：暂停新增算法优化。R73仅增加opt-in native CPU trace bridge（Trace launcher启用），固定15个UE scope覆盖validation／两role采样／contacts／body与clothes physics／export／accessory sync、setup、step与skin prepare/LBS/correction；用一次注册的process lifetime callback，RAII保证同worker嵌套begin/end，正常无trace不注册。完整CPU事件必须包含Rust Rayon workers与全部timer，以exclusive事件树分析；区分worker CPU总量与GT wait/critical path，排除verbose phase、水枪、姿势切换／外部UE。输出根因报告MD后才决定下一项优化。R72当前功能PASS，12回归PASS，quiet10.008／10.656ms、99.919／93.846FPS，未达120。

R73 profiling bridge四模組build PASS91.66秒，quiet phase起點anchor增量build PASS9.64秒。R72最新12回歸PASS、功能full PASS（SkipPaint）、APNG controls6295.494ms、first79.798ms、速度callback最大0.083402ms且CPU budget PASS。新版trace不改motion/physics數值，只有固定native scope與quiet anchors；正在取正常雙人Trace，等完成後先讀anchor/thread inventory，再輸出完整3秒CPU event树与GPU event，不使用混合CPU/GPU statistics冒充CPU。

R73根因profile完成：Retry全程externalUE0/hash一致，兩quiet窗口294／293完整幀，全部8native worker IDs78～85，role samples588／586=2x frames。GT Native3.880／3.860ms（validation0.410、sample wait1.250、contact0.554、dynamics wait1.657）；两角色display2.589／2.593ms，其中streams2.025／2.013，GT实际ParallelFor tasks1.665／1.649ms。Native CPU合计37private physics3.138／3.053、71skin2.435／2.374，不能加成帧时间。RT等待5.14ms、真正stream upload0.623ms，GPU1.45ms；120未达到。详见INTERACTION_PAIR_PROFILE_ROOT_CAUSE.md。新增all-thread analyzer以thread ID保留同名workers、强制完整frame两role sample计数。

R73 profiling最终整合build重试PASS8.52秒（InteractionBuildR73ProfileFinalR2.log）；Python分析工具py_compile及git diff --check通过。根因MD已保存并开启。当前根因表对应NativeRetry保存的四DLL hash，不将后续并行源码／build变化回写为旧trace结果。

## R74：合併來源轉換與渲染串流批次

依 R73 完整 CPU trace，雙人每幀約 10.2 ms，來源轉換約 0.43 ms、生成及打包約 1.58 ms。先合併可獨立讀取 native snapshot 的私人蒙皮工作與 GPU 打包，減少串行批次 barrier；保留 CPU canonical vertices、法線、切線、bounds 和原路徑。眼睛、球面法線及 morph／旋轉快取依賴走既有路徑。

1. 新增 blocking generated-stream API，來源工作和 draw 工作共享一次 ParallelFor；拒絕無效 request 前不執行 callback。
2. draw 直接讀不可變 native view，來源 callback 寫不重疊 canonical 範圍；不能與 draw 同時讀寫 Base arrays。
3. 提供同一 binary 開關及逐值 parity 驗證；失敗 fallback 先完成來源資料。
4. 編譯四個模組；回歸、APNG 雙人眼睛／語音／嘴型／速度及切換驗收；比較正常 RHI 全幀和 Present FPS。
5. 水槍維持 SkipPaint；120 FPS 未達標不得宣稱完成效能目標。

### R74 驗證結果及採用決定
- 四模組 Build R2 PASS 12.76s；最終 build PASS 42.32s（共同 Rust source build stamp 觸發重鏈結，未修改該其他工作）。
- PairRegressionR74：12/12 PASS，source jobs completion、duplicate request rejection、exact GPU packed normals/UV/colors/bounds PASS。
- 同一 binary A/B：融合 ON 10.243125/10.239121ms、97.624409/97.661975 Present FPS；OFF 10.050503/10.147326ms、99.496813/98.544037。兩次四 DLL hashes 相同且前後穩定、external UE=0。**無收益，預設 ok.Animation.FusedSourceStreams=0。120 FPS 未通過。**
- opt-in exact parity 實際觸發（64 source jobs/59 sections），五張雙人眼睛 screenshot 經人工檢查正確。該測試速度 phase7 失敗，不當作整體 PASS。
- PairEyesSpeedR74Default：audit_completed=1、functional_pass=1；APNG entry 6321.292ms；初次 pair ready commit 71.481ms；12切換最大 stall 35.501ms；54 mouth patterns、true speech、walking two cycles、male replacement/failure preservation、shared clock、live head/mouth/belly、return/reentry PASS。69 speed callbacks 最慢0.059500ms；13 phases 全部既定 adjacent CPU/frame budget PASS。watergun_validation_skipped=1。
- Default 五张眼睛 screenshot 逐張人工驗證 PASS，console/stat overlay 不作 FPS 證據。完整資料保留在 Saved/Tests/InteractionShowcase/PairEyesSpeedR74Default。
- 下一個效能實作必須以 canonical source-indexed 工作產生所有對應 draw，共用一次轉換／旋轉；只融合 dispatch 而重做轉換不是已採用解。

### R74 最後快取切換修正
CacheBoundary build 四模組 PASS 45.23s；PairRegressionR74CacheBoundary 13/13 PASS，包含 A→停用更新B→重新啟用A 的 source snapshot 回歸及原12功能。前一次完整 Default audit 仍保留 PASS；此次最後修正以新增 cache-boundary 回歸覆蓋，不把較早 screenshot/hash 冒充新 binary。PairProfileR74Final（修正前）正常 RHI：10.010794/10.133961ms、99.896019/98.670389 FPS、外部UE=0、hash stable、entry6302.268ms、firstpair223.067ms。最後 boundary binary 另跑 PairProfileR74CacheBoundary 供最終數字。

### R74 清理路徑補驗
發現既有完整 audit 的 male replacement/StopPair teardown handled ensure，不將其當作乾淨成功。改為 follower 全部銷毀後才清空 persistent leader asset；ExactLeaderPose 增加真實註冊、附掛的 preview follower teardown 與 idempotent cleanup 測試，再跑完整 APNG audit，確認不再有 leader-map ensure。Renderer SetLooseParameters crash 與此 handled ensure 分開追蹤。CacheBoundaryRetry 正常跑完但其他 MaterialBakeHost UE 程序 15:40:34 啟動，performance_evidence_isolated=0；其 FPS 不作採用或達標依據，不停止其他工作程序。

### R74 當前實作最終功能驗收
- TeardownR3 四模組 build PASS 16.29s；PairRegressionR74TeardownR3 13/13 PASS，包含真實註冊／附掛 preview follower 清理、重複清理、snapshot bypass 再啟用、CPU/GPU exact generated stream 資料。
- PairEyesSpeedR74TeardownFinal：audit_completed=1、functional_pass=1；APNG entry6529.626ms；first ready pair77.668ms；12次切換 click-to-play 最慢19.573ms、最大 frame stall33.230ms；68速度 callback 最慢0.062600ms、13階段全部 adjacent CPU/full-frame budget PASS。
- 54 mouths、true speech、walking loop two cycles、shared pair clock、pause/restart、live head/mouth/belly、male replacement/failure preservation、return/reentry、stable shadow pixel oracle 全通過。五張眼睛 normal RHI screenshots 再次逐張人工檢查正確。
- 最終 runtime log 沒有 leader-map ensure／fatal／Unhandled Exception；四 DLL hash stable。載入期間外部 UE 被監測到，functional PASS 不作隔離 FPS 證據。watergun_validation_skipped=1。
- 前述一次 Renderer shader-parameter crash 沒有在兩次後續 runtime 重現；不能斷言 follower teardown 修正已解掉此不同 callstack，仍保留 crash 調查紀錄。

### 接續效能方向（尚未實作，不能標為完成）
R73 的主要成本是頂點工作，不是單純等待。R74 barrier 合併仍做雙份 source 轉換所以預設關閉。下一版應採來源索引為工作單位：
1. generalized generated-stream API 先驗證全部 requests／建立 packets，再由 caller 的 source job emit 自己獨占的 section draw ranges；每個 draw vertex 只能被一個 job 寫入。
2. 每個 source job 先轉換自己的 canonical source 範圍、計算可重用 normal quaternion，再生成並打包所有對應 section；不再由 draw 重讀 native 重新轉換。所有查詢用 BaseVertices/Normals/Tangents 仍在 blocking call 返回前保持當幀資料。
3. OKCompactProceduralSections 的 DrawVertices 是遞增 source indices，可對每個 1024-source 範圍用 lower-bound 找 section 對應 draw 範圍；無需每頂點搜尋、per-card 預建或巨大反向表。
4. 跨 source 依賴的 face-sphere normals／eyes／morph 先保留原準備路徑；一般 private skin 可包含 normal quaternion cache，因為同一 job 在 emit 前已計算自己 source 的 quaternion，沒有跨 job 讀寫。
5. 每 job 分 section 累積 bounds，join 後彙總及 RT publish，保留 exact FPackedNormal quantization、UV/colors、stock fallback。缺少／重複 section 提前拒絕，不得留下 partial canonical updates。
6. debug parity 在 batch 後以已完成的 canonical source 重算舊 draw，逐值比較 CPU vertices/tangents，再用既有 GPU readback test 驗證 packet exact。正常 FPS 比較關閉 parity，使用同 binary A/B、4DLL stable、無外部 UE 的 quiet Present；只有平均≤8ms、Present≥119.5 才通過120。

R74 最終當前 binary：13 regression PASS；完整 APNG PairEyes/Speed/SkipPaint audit PASS，且 teardown log 無骨架 ensure。最終隔離正常 RHI 兩姿勢 9.569357/9.572967ms、Present104.501234/104.458581FPS、GPU1.365668/1.364470ms；external UE0、4DLL stable且與功能測試完全相同。120尚未通過，詳細採用／未採用與限制見 INTERACTION_SHOWCASE_ACCEPTANCE_R74.md。

## R75 實作計畫：來源範圍生成並打包
1. 新 API 由 caller 的 disjoint source jobs emit section draw ranges；range 內維持原 vertex generator／FPackedNormal packing，避免每頂點額外 callback。所有 request 先驗證，join 後才發佈 GPU 與 bounds。
2. 私人蒙皮在同一 job 完成 canonical source 轉換／共用 quaternion，再 emit 所有 source 對應的遞增 compact draw 範圍。眼睛、morph、球面法線保留既有前置計算。
3. 開關 SourceIndexedStreams 同一 binary A/B；SourceIndexedParity 在 join 後重算 legacy draw 逐值比較，避免 race／漏頂點。
4. GPU readback／UV-color／bounds、request rejection 回歸；四模組編譯、正常 RHI 兩姿勢 profile、完整 APNG 眼睛／速度／語音／切換 SkipPaint。無收益不得開啟預設或宣稱120。

### R75 採用決策
Source-indexed ranges API、exact data／bounds／blocking ownership 驗證作為通用基礎保留；因多輪量測未優於 baseline，SourceIndexedStreams預設0。serial source mode0仍明確走原路徑。完成 opt-in parity 與預設完整 APNG功能/速度/眼睛驗收，不把實驗宣稱效能目標完成。
### 後續真正減少串行時間的架構（待實作）
GT先 prepare 一個 frame-local generated-stream batch，捕捉 immutable section mapping、offsets、rest normals、CPU section pointers及GPU packets。Native paired private-skin 的 scoped completion observer（只在該 blocking apply call有效，不存到可clone session／不碰UObject／不重入FFI）在自己的mesh worker完成 canonical轉換、正常公式的outline生成、exact tangent packing及local bounds。所有native jobs完成後，GT再publish packets、更新bounds、標記transforms，再繼續shadow capture。Morph/eye/sphere-normal跨source依賴保留原路徑；所有CPU查詢數據維持當幀。Callbacks/contexts不可跨frame或角色替換保存；invalid source或render-state change走原stock fallback。必須以legacy CPU/GPU oracle及完整APNG workflow驗證，並同binary隔離Present驗收。
### R75 最終當前功能驗收（2026-10-07）

- 四模組 InteractionBuildR75Final PASS16.39s；PairRegressionR75Final 13/13 PASS。
- SourceIndexed opt-in：PairEyesR75Parity完整功能PASS，71,185 vertices／195 source jobs逐值parity、五眼睛PNG人工檢查PASS；entry6200.590ms。新路徑沒有性能收益，最終預設0。
- 預設完整PairEyesSpeedR75Final首輪4倍速度phase7失敗，保留原門檻與錯誤紀錄；相同四DLL PairEyesSpeedR75FinalRetry完整PASS，13速度phase原CPU/frame門檻PASS、69callbacks最大0.050500ms；不宣稱波動根因已定位。
- Retry entry6099.416ms、firstpair69.321ms；12prepared切換click最大19.729ms、frame stall最大36ms。54嘴型／語音／walking loop／shared clock／eyes／head／belly／male replacement／return／shadow oracle全PASS，五PNG人工驗證，無ensure/fatal。
- 最終同binary PairProfileR75Final isolated1／externalUE0／hash stable：quiet全幀9.433605／9.479136ms、Present106.005521／105.498206；平均8ms／120FPS未達，不將整體性能目標標記完成。watergun仍SkipPaint。
- 完整驗收與後续尚未實作的native worker completion架構記錄：INTERACTION_SHOWCASE_ACCEPTANCE_R75.md。
## R76 實作與驗收計畫
1. 新增只存在於 blocking paired apply 的 native private-skin observer；context 不存到 session／clone，不跨frame，不重入FFI、不操作UObject。每個part完成寫入當幀positions/normals後才呼叫一次。
2. GT先驗證所有generated sections並準備packets；native worker完成canonical轉換／法線旋轉／outline draw／exact packing。join後GT發佈GPU資料及bounds，再繼續既有eye/morph/shadow。
3. 僅無morph／非eye／非sphere-normal且section完整的private parts適用；其他part原路徑保留。處理完成標記只供該frame，重進／替換／開關切換不沿用snapshot。
4. 保留預設OFF開關與exact parity；native callback ownership及serial/parallel GPU streams回歸，四模組build，APNG完整功能/速度/眼睛及正常RHI同binary profile，水槍SkipPaint。無收益不預設開啟，120未達不標記完成。
R76 架構落地細節：observer掛在原render_solved_accessories的geometry完成工作（不在dynamic update強制重複產生），native paired frame完成後joined兩角色skin_all_observed，當幀C++消費一次SkinReady。178k級native oracle獨立測試以原render_solved做比較、實際snapshot clone驗證context不保留。API callback僅借用至blocking call返回，section完整emit後才GTpublish；missing sections維持原資料。
### R76 trace後的計算優化
ON trace 326完整frames：GT PairAdvance6.703／Native5.715ms、dynamicsWait3.516ms；native display callback CPU SUM13.255ms（不是wall time），71回呼／frame。GPU約1.48ms，後段draw確實0。管線搬移未證明收益，NativeDisplayStreams維持0。補實作runtime immutable UnitRestNormals cache（只在RestNormals載入指定時算一次，缺cachefallback原GetSafeNormal；parity重算原formula）及zero-offset base surface略過identity RotateVector。保留RestNormalCache／ZeroOffsetRotation同binary開關、原公式parity與正常RHI A/B；不改物理、眼睛、材質、shader或打包精度。

## 啟動自動 FPS 顯示
在本地 PlayerController BeginPlay 使用 UEngine.SetEngineStat(FPS,true) 明確啟用；不使用 toggle，不增加每幀工作。重新編譯後以新程序確認 startup enabled=1 與實際畫面。

## R77：共用每幀骨架查詢結果
已知native AccessorySkinPrepare為CPU工作：每個private clothes renderer重建body name index與world_of cache。計畫先測現有正常RHI基準，再將同一角色同一同步pass的AssignedWeights骨架world查詢集中一次，透過不可變frame-local map供worker借用；不跨幀、不修改physics、不使用特化卡片資源。保留原公式fallback與exact parity，測試coordinated/private/缺失骨骼與每幀變動。實測失敗不採用。完成後編譯、native oracle、完整APNG功能／調速／眼睛／13回歸與Present性能驗收；水槍略過。

R77量測補強：AssignedWorldAB只在PairProfile下開啟，同一程序同一姿勢OFF→ON→OFF，每段暖機3秒／quiet15秒，phase2不開verbose，三段都保留8ms／300ms／119.5Present門檻。用前後OFF評估漂移，不修改一般profile和功能驗收。

使用者建議參考CrowdRoomDevelopmentAPNGGPU.cmd：已完成不減面品質入口、原資料回歸與兩張支援APNG的GPU三視角／matrix probe。計畫INTERACTION_TWO_ACTOR_HIGH_DETAIL_GPU_PLAN.md、驗收INTERACTION_TWO_ACTOR_HIGH_DETAIL_GPU_ACCEPTANCE.md。male原材質coverage不足、probe GPUready11.8／12.5秒超標，不能當男女live互動或120FPS完成。整合重用原preview材質與live native solver，不照搬Crowd簡化／60Hz量化／presentation。

## live native GPU palette 續作完成項目
已實作並驗證直接 affine／native slot 發布、UE carrier雙緩衝同步／refresh、原子驗證與snapshot lifetime、資產／leader隔離，以及 portable 安裝共用 bytes 後確實播放的修正。18項回歸通過；兩張實際男女APNG三時間點324發布／972矩陣抽樣誤差0且動畫實際變動。最新binary完整功能重測PASS：entry6.214秒、12switch最大frame41.288ms、13speed原門檻PASS、54嘴型／語音／head eyes belly／male replace／return／shadow oracle正常。首次同binary continuous-speed失敗仍記錄，未歸因未證實原因。正常RHI quiet9.785／9.700ms、Present102.190／103.090，120FPS與背景操作卡頓仍未達；不能將混合phase平均6.717ms當成穩態達標。沒有把private服裝／頭髮全數切GPU，下一步原material／secondary offset shader／source generation與CPU查詢契約見高模GPU計畫。驗收：Docs/INTERACTION_LIVE_NATIVE_GPU_PALETTE_ACCEPTANCE.md；錯誤：Docs/INTERACTION_SHOWCASE_ERRORS.md。

## palette 容量提示續作
已消除FOkSession每次count+copy的重複native export，暖機後一次讀取本幀資料；數量變動重試、失敗保留舊輸出、session生命周期隔離。此通用讀取已供現有CPU嘴型／表情和live GPU bridge使用；不缓存來源指標／pose，不刪native validation。19回歸及實際男女APNG矩陣PASS；完整功能PASS：entry6.157秒、12switch最大frame44.165ms、13speed／54mouth／speech／eyes／head／belly／replace／return／shadow原門檻通過。正常RHI quiet9.892／9.842ms、Present101.094／101.611，8ms／120FPS仍FAIL；混合載入最大1245.183ms保留。詳細驗收與尚未完成GPU工作：Docs/INTERACTION_PALETTE_CAPACITY_ACCEPTANCE.md。

## runtime GPU deform 核心與 staged private palette
本輪先落地全精度 source／secondary draw mapping＋RDG compute，以及 private skin → normalize → placement correction 的通用匯出契約；不把 correction 提前合併而破壞 collapsed-normal fallback。原始材質、indices、UV、colors 保留於原 renderer。實際男女 APNG 原 geometry oracle 共 307734 頂點誤差 0；UE staged palette 213 次 publication 及 session move／Close／失敗保留通過。實際 GPU readback 1161 outputs 驗證 signed weights、fallback、shear、reflection、zero scale、secondary offsets、exact／near-opposite normal。近反向 FP32 相消問題已重現、修正並留下永久回歸；最新 20/20 回歸通過。完整記錄：INTERACTION_RUNTIME_GPU_DEFORM_ACCEPTANCE.md；錯誤：INTERACTION_SHOWCASE_ERRORS.md。
下一個實作階段仍是 GPU output 的 persistent UAV position／packed tangent、原 LocalVertexFactory 材質綁定與 barrier、不可變 source generation、背景準備原子切換、同 palette bounds／CPU 按需查詢。private 衣髮尚未正式使用新 pass，不能提前停止 native geometry，亦不宣稱 120FPS 達標。
本輪最後 PairEyesSpeedStableGpuFinal 功能完整 PASS：進場6.164855秒、ready switches最大39.173ms、調速／嘴型／語音／眼睛／replace／return／shadow正常；20回歸全 PASS。近反向 shader 精度修正已由實際GPU回讀驗證。混合背景最大1273.247ms、quiet17.156550／12.966999ms仍未通過性能目標；詳見驗收，不標記整體完成。

## 常駐 GPU deform／打包輸出續作
完成 render-thread owned source generation＋雙槽 persistent position/tangent，首幀原子 install、每幀 palette upload、失敗保留／過期 generation 拒絕／replacement history 重置、RDG vertex/SRV transition。GPU pack 保留原 UE 12-byte／8-byte 格式；整數共線解包 rounding 邊界已重現並修正（48-bit exact product，不使用 double／epsilon）。完整22回歸 PASS；實際GPU19,907個packing vertices逐byte一致，3幀129個vertices residency更新誤差0，immutable18576 bytes僅上傳一次。計畫與尚未接入scene proxy的項目：INTERACTION_TWO_ACTOR_HIGH_DETAIL_GPU_PLAN.md；詳細驗收：INTERACTION_RESIDENT_GPU_DEFORM_ACCEPTANCE.md。不能將buffers API當作private renderer整合／120FPS完成。
本版最後 PairEyesSpeedResidentGpuFinal：完整功能PASS、進場6158.357ms、walking ready8035.554ms、12ready switches最大frame30.145ms、調速callback最大0.032ms，眼睛五張原圖逐張檢視正常。混合背景最大1303.685ms保留，不標記120FPS／GPU scene renderer完成；下一步仍需原scene proxy消費與source lease／bounds／CPU queries。

## GPU stream proxy 發布續作
新增 owned SubmitGpuStreams／atomic receipt、process-wide proxy epoch＋resident generation、CPU takeover invalidation；GPU resident結果以RDG直接copy到原VF position/tangent，保留typed SRV／UV／color／indices／material與光柵draw。caller finite conservative bounds只能擴張、拒絕工作保留最後GPU幀；ray tracing未驗證保留CPU。23回歸PASS，actual scene proxy3幀13,000頂點GPU buffers正確，256×256 GPU／CPU raster 578可見pixels、差異0。原圖已檢視，不是空圖PASS。詳細驗收：INTERACTION_GPU_STREAM_PROXY_ACCEPTANCE.md。實際APNG角色尚未停止CPU geometry，下一步native source lease／bounds／queries與private part caller整合；不提前標記120FPS完成。
本版最後PairEyesSpeedGpuProxyFinal完整功能PASS：entry6111.411ms、walking ready8076.627ms、12ready switches最大frame33.401ms、調速callback最大0.059899ms，眼睛五張原PNG正常。mixed最大1314.143ms與播放9.75–10.18ms仍未達性能目標，不把APNG原route功能PASS當新private GPU整合完成。native source lease／bounds／queries與caller publication ownership仍待續作。

## 2026-10-07 runtime immutable source revision

已實作來源一次性驗證、runtime 版本與版本限定的 staged palette API。來源替換、snapshot resource 抽離/還原、clone/deserialize 或 palette layout 改變使舊版本失效；暖讀取不再掃描原始高模權重。來源跨模組只讀，無角色專用預計算。

Native 生命週期測試、實際男女 APNG 合計 307,734 頂點 native renderer 對照（最大誤差 0）、568 次暖讀一次掃描驗證通過。Unreal 編譯成功，整合回歸 23/23 PASS。詳細結果：`INTERACTION_PRIVATE_SOURCE_REVISION_ACCEPTANCE.md`。

待完成：actual APNG GPU caller、runtime conservative bounds、按需幾何查詢與 native CPU geometry 跳過、pending install/CPU fallback 跨 section 協調，以及真正整體 120fps/背景卡頓門檻驗收。來源快取成功不代表這些項目完成。

最後雙人 APNG 驗收 `PairEyesSpeedPrivateRevisionFinal` 完成，functional_pass=1；entry5.975秒、firstwalking7.841秒、12 ready switches最大18.303ms、13 speed budgets全部PASS、眼睛/54嘴型/語音/男女控制/原shadow正常，水槍SkipPaint。whole playing9.63–10.08ms、background max1278.791ms仍未達目標；完整證據見來源版本驗收文件。

## 2026-10-07 完成實際 APNG owned GPU source adapter

KKPrivateGpuSource 擷取一次 native revision/rest vertices/原始 signed weights/bone slots；palette-only pose 更新與 section draw mapping 已實作。GPU NativeUnitySpace 明確保留 native normalize/post fallback，最後轉 UE basis/公分。來源資料均 runtime 從 APNG/共用資源取得。

真正GPU readback對照女性/男性APNG當前CPU renderer：210 publications、456612 draw vertices，最大位置誤差.0000458012cm，normal6.94219e-6/tangent2.73143e-7。Native resident三幀位置/方向packed bytes exact、immutable144bytes只上傳一次。25項整合回歸全部PASS；四個native測試亦PASS。詳細 INTERACTION_PRIVATE_GPU_SOURCE_ACCEPTANCE.md。

尚未啟用 actual loaded-part GPU publication、conservative bounds/按需query/all-section install receipt協調；8ms/120fps及全部背景幀300ms仍未完成。最後功能驗收入口10195.450ms超10秒，已保留失敗並trace重跑；不可用exit0宣稱完整功能通過。

## 2026-10-07 actual private GPU playback 接入（驗收中）
已接入 runtime source/draw/bounds、all-section generation query→install→update receipts、pending reserve與CPU恢復、GPU批次graph和按需CPU query（水槍/capture framing）。原材質/VF/UV/colors/indices未更改。shared及實際native pair roots皆可跳過已安裝GPU部件的CPU頂點生成，原solver/physics保持。Native男女filtered solver/query4測試PASS，Unreal NativePair build成功120.81s，26項回歸PASS（PrivateGpuNativePairFullRegression）。
仍以cvar opt-in做actual Visible profile；原預設CPU未切換。入口依使用者要求保留但暫停優化。新120fps_playback_present_verified專指安靜雙人播放，原全範圍旗標不放寬。未取得actual FPS通過前不得宣稱達標。

## 2026-10-07 最終 private GPU／曲線並行版本
實作已完成：runtime private source/draw/bounds、all-section generation handshake／CPU fallback、每actor單一RDG graph、原paired native solver跳過GPU擁有部件CPU頂點生成、按需現在pose CPU query、互動場景進出／男女opt-in，以及immutable Arc曲線快照鎖外並行採樣。所有高模／材質／物理保留。
Native anim41項PASS；最終build42.11秒Succeeded；ImmutableClipGpuFinalRegression26/26PASS。1600×1000正常配置Visible第一輪127.298179／126.615566fps，平均7.855590／7.897920ms，最大16.715400ms；四DLL穩定、外部UE0、無paint。舊FinalDefault118.857888fps失敗與CPU43.29／51.56fps保留，沒有挑快輪掩蓋。
CardReaderDevelopment預設GpuSkin=true，CPU可明確 -GpuSkin:$false。最終功能PairImmutableClipReaderGpuFinalFunctional全部PASS，12switch最慢21.950ms／切換最大35.013ms、13速度門檻與69callbacks PASS、54嘴型／語音／眼睛／頭腹控制／男女替換／return-reentry通過。正在同binary做最後Visible複測。
入口超時依使用者要求延期，水槍不測；安靜播放達標不宣稱每單幀120Hz或全背景載入<300ms。完整證據見INTERACTION_PRIVATE_GPU_PLAYBACK_ACCEPTANCE.md。
最終驗收完成：PairImmutableClipReaderGpuVisibleProfileR2 127.066060／125.787354fps、7.870062／7.950064ms，最大16.667798ms；安靜播放120fps旗標1。最終26回歸／完整功能／两輪Visible量測的四DLL hashes逐一相同，並與目前正常launcher使用的四DLL核對一致。diff --check通過。本輪依最新範圍完成；入口與水槍量測延期，背景載入最大幀不宣稱全部<300ms。詳細完整驗收文件已更新。

## 2026-10-07 水槍功能驗收完成
使用者現已要求驗證水槍，解除此前水槍功能檢查延期。APNG高模private GPU實際雙人動畫中，男女分別清空後射擊、private表面沉積、當前pose query、動畫附著、姿勢切換及清除皆PASS；完整功能PASS、最終8項回歸PASS，先前較廣31項亦PASS。詳細見 [INTERACTION_WATERGUN_GPU_ACCEPTANCE.md](INTERACTION_WATERGUN_GPU_ACCEPTANCE.md)。本輪重編譯增加audit，最終功能與回歸四DLL核對一致；上文舊binary／120fps數字保留為歷史證據，未重測噴射120fps。入口优化仍延期。

## 2026-10-07 雙人水槍profile與兩個優化方案
已完成同binary兩輪無trace及獨立trace，詳細見INTERACTION_WATERGUN_PAIR_PROFILE.md。女角噴射72.33／88.91FPS，男角2.99／7.84FPS；男角最慢幀521.121ms，水槍120fps未達標。根因是液體遮罩傳輸與並行等待，並非男性高模三角形更多。兩個通用方案已寫入INTERACTION_WATERGUN_OPTIMIZATION_OPTIONS.md；建議先修正依物理面積的共用atlas分配，再做精度不變的稀疏格點傳輸。本輪沒有實作這兩個優化。profile新增計時／workload及audit後重建，原水槍功能驗收屬前版歷史證據；目前profile三run與四個DLL hashes已另外核對，沒有把舊安靜120fps移作噴射證據。

## 2026-10-07 方案二／1024共用液體atlas完成
使用者選擇依物理面積runtime chart與場景共用pages，預設1024。六項液體回歸與真實APNG雙人private GPU水槍、動畫附著、嘴型／語音、切換、清除PASS。獨立無trace1600×1000量測女角噴射94.913fps／10.532ms、男角75.290fps／13.276ms；男角mask84.147→0.317ms、儲存格5143296→183760，雙人噴射120fps仍未達標。乾淨基準123.86..127.46fps。四DLL目前與三個最終驗收run一致；外部程序污染首輪保留，不作正式效能證據。詳細見 [INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md](INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md)，錯誤記錄已更新。入口仍不列本輪優化範圍，方案一尚未實作。

## 人物編輯器／一對一互動場景區分與加載3D入口
已固定保留「加載3D（人物編輯器）」與独立「進入1對1互動場景」，详情独立捲動。編輯器完整body／face即時調參、容許60fps；互動private GPU播放、120fps目標，只允許肚子大小例外。OnShapeChanged在互動鎖定，肚子經専用body[21]入口。實際APNG導航5狀態截圖及參數權限PASS，3項相關回歸PASS，完整雙人嘴型／語音／肚子／水槍／切換／返回再進PASS。四DLL核對当前一致，效能沒有重測／不轉移舊120fps證據。完整記錄見 [INTERACTION_EDITOR_NAVIGATION_FIX.md](INTERACTION_EDITOR_NAVIGATION_FIX.md)。
