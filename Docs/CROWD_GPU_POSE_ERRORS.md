# 百人 GPU pose：錯誤與防重複紀錄

本階段保留原材質與 section，停止離線 atlas／LOD 的自動套用。玩家卡片不能依賴固定角色清單；離線材質烘焙不符合使用者要求的 runtime 合併條件。

| 問題 | 原因 | 修正／下一次檢查 |
|---|---|---|
| `TaskGraph.NumWorkerThreadsToIgnore` 沒有減少 worker | 此版本的相容 tuning 路徑為空操作 | 使用 process-local corelimit 與 oversubscriptionratio；以實際執行緒及 trace 驗證 |
| build metadata 遇到另一個 Editor 鎖住 NetCore DLL | 共用 engine 正在使用 | 不關閉其他工作；使用專用 runtime 與 project module pin，檢查所有 DLL／BuildID |
| TObjectPtr 容器迭代以 auto* 宣告編譯失敗 | 元素不是裸指標 | 使用 const auto& 與 Get()；GC 資源容器使用 TObjectPtr |
| atlas R1 材質變成灰色 fallback | 刪除 polygon group 後 ID 仍為 69、70，超過兩個材質槽 | Compact IDs、清除 LODMaterialMap、逐 section 驗證 MaterialIndex；V2 修正此問題 |
| V2 atlas 身體變黑 | 完整原 shader 的離線烘焙未保持原外觀；原因未完全釐清 | 不能視為視覺驗收通過；此路徑已停止，不自動選用 |
| 非碰撞角色的 capture bounds 為零 | GetComponentsBoundingBox 預設只包含碰撞元件 | 使用 GetComponentsBoundingBox(true)；黑圖不能算通過 |
| 把最佳單次結果當穩定 FPS | 背景負載與重複量測差異很大 | 報告 mean、P95、樣本與負載限制；100 人皆動畫才可驗收 120 FPS |
| 直接把 SkeletalMesh 換成 GPU provider 可能失敗 | UE ASTP 要求 unified bone map，且每 LOD required bones 最多 1024（mesh-space 層最多 256） | 建立獨立 instancing derivative，保留原 section／骨架；超限必須記錄並拒絕，不能假裝 GPU pose 成功 |

GPU pose 的建立、編譯、畫面與效能驗證結果會追加到本檔。

## Game low LOD：2026-10-03

使用者重新授權每卡遊戲低模／256 貼圖與按需高模；材質 atlas／section 合併仍停用，動畫頻率不降低。新增 GameLow256R1，不沿用含完整 LOD0 的 Performance256 作為快速載入證據。

第一次編譯暴露三項錯誤：TObjectPtr 容器不能用 auto* 迭代；TArray 沒有 Count(value)；新增 header 成員後，舊 GENERATED_BODY 行號與 class size 已過期。修正容器用法，重新執行 UHT 並編譯 Module.OpenKoikatsu.gen.cpp；不得只重編 scene 而沿用舊反射大小，否則存在配置記憶體越界風險。新增私有成員也需要此檢查。

GameLowVisualC001R1 在退出時 AOKCrowdScene destructor 發生 access violation，且樣本數不合理；整輪效能資料作廢。精確原因：目前 link response 使用 Module.OpenKoikatsu.cpp.obj（此 unity unit 內含 generated cpp），不是另外存在的 Module.OpenKoikatsu.gen.cpp.obj。先前重编 gen.cpp 並未被 link 使用，仍混入舊 layout。修正為重編實際 unity unit，並讓 build helper 在任何 unit 不存在於 link response 時立即拒絕；不能以 cl 成功當成已整合。0765/0766 不再使用，後續需 exit=0 與合理樣本數後才驗收。

補充：3600 samples 本身不是 layout 錯誤證據，因場景最多保留最後 3600 幀；log 的 frame counter 也會循環。R1 作廢原因是實際退出崩潰及舊 layout 混入。

GameLowVisualC001R2 exit=0，但低模衣服出現明顯拉伸，視覺驗收失敗，不能發布或批次生成。排查方向為直接在 compact reference mesh 上 inline reduction 的 bone map／inverse bind 路徑；先用未 compact 的 Packed 作對照，保留失敗資產供調查。高模顯示成功也不能掩蓋低模變形。後續必須比對簡化前後 reference skeleton、bone palettes 與實際行走畫面。

未 compact 的 Packed 對照 GameLowFullRigC001R1 也出現同樣拉伸，因此不能認定 compact 是原因。核對 UE reduction 預設 bMergeCoincidentVertBones=true：多層衣服／私有 rig 不應自動合併同位置頂點的骨骼權重。下一次關閉此選項、保護 bone／edge／vertex color 邊界，保留最多 12 influences，再做獨立行走畫面驗證；仍不得以三角形數降低算通過。

GameLowC001ProtectedR1 的 CPU audit 使用 GetCPUSkinnedVertices，但 NullRHI 不建立 MeshObject，觸發 assertion。改為以 imported LOD soft vertices、section bone map、inverse bind 與動畫 component-space matrices 計算邊界；此檢查不依賴 Render MeshObject，也不代表 GPU/材質驗收。保存前檢查各 LOD 的 7 個動畫時間點；遊戲截圖仍必須獨立驗證。

ProtectedR2 通過 bounds 檢查，但遊戲仍有異常：bounds 不能證明局部皮膚正確。真正必要的格式條件見 KKImportIndependentCardCommandlet.cpp 的 ExactSkeletal：每個 __ok_palette_part_slot 必須有 3 個相等的 16-bit 權重，總和 65535。通用 reduction 的裁剪／量化不保證此條件。改成完整三欄為一組、最多四組，21845 級量化後三欄等重；從簡化 LOD model 重建獨立 mesh description，再逐渲染頂點檢查三欄和總權重，另保留實際遊戲視覺驗證。不可再只拿三角形／bounds 當低模驗收。

TriplesR1 衣服拉伸消除，但腿部缺失，仍未通過。原 exact skin 的 stored vertex positions 是經 per-vertex blended matrix inverse 處理的編碼座標，不保證可直接拿來做幾何簡化；在 encoded reference space 中退化的三角形，可能在實際動畫姿勢中有效。改在 reference animation pose 的實際形狀空間簡化，再以重組後的權重與 reference skin blend inverse 還原編碼座標。bounds 檢查新增過度縮小檢查，避免漏掉缺失肢體。FVertexBoneWeights 是 attribute view，非 FBoneWeights；新增 API 型別以本地 header／編譯診斷為準。舊 R1 試點資產保留於 RejectedAssets，後續使用 Schema=1001 區分已修正 recipe。

## 2026-10-03：GPU 首次試點

`GpuPrepareC001R1.json` 明確失敗：三月七 PackedRig 主網格 required bones = 2311，超過 ASTP 上限 1024。不能提高 shader 上限後便宣稱正確，亦不能丟棄私有骨骼。改成原角色可見部件各自建立 compact weighted rig（保留全部祖先）、各自 provider；材質／section 不合併。此修改仍需實測驗收。

`GpuPrepareC001R2.json`：48 個可見部件、72 sections、最大 301 required bones，資產建立成功。但 `BenchmarkGpuPilotC001R1.log` 顯示材質缺少 InstancedSkinnedMesh usage flag，遊戲使用 default material；即使播放時間前進與 CPU evaluator=0，也不能算畫面或效能驗收通過。R2 資產路徑複製原材質圖／參數，僅添加 GPU 元件所需 usage flag，原資產不修改；下一次必須確認沒有 fallback。
`Build755GpuR3.log`：usage enum 寫成複數導致編譯失敗；本地 MaterialInterface.h 的正確名稱為 MATUSAGE_InstancedSkinnedMesh，已修正。後續新增 UE API 必須逐字核對本地 header。
`MatrixRenderer-MatrixR1/Compile.log`：複製 engine private header 後，相對 include SkinningTransformProvider.h 失效；改成 Skinning/SkinningTransformProvider.h，保留 engine source 不變。
`MatrixRenderer-MatrixR2/Compile.log`：GetAssetUserDataArray() 回傳裸指標容器，錯用 Data.Get()；已改為 Data。本地 API 型別須核對，不能依名稱推測。
`BenchmarkMatrixPilotC001R1.png`：主網格消失，只剩面部部件；視覺驗收失敗。AnimBank 不需要 ASTP unified bone mapping，先改回既有 section bone palettes，排除新 mapping 與專用 runtime 的相容問題；原因仍待驗證，不能把時鐘前進當成完整動畫成功。
`MatrixDebugR1` 編譯：該編譯單元沒有 LogRenderer 宣告，GPU readback 診斷改用 LogTemp。
Debug 啟動 PowerShell 中誤寫 $_ .Replace（有空格）導致整段解析失敗；已改為 $_.Replace。解析失敗前沒有執行任何動作，重跑使用原新 tag。
`Build759MatrixGeometryAudit.log`：project 主模組與七個 plugin DLL 已 link，但共用 NetCore 被另一個 Editor 鎖定，整體 build failed。沒有停止其他 Editor；驗證各自 0759 DLL 存在後，只對本 project manifests 切換 0759，保持私有 runtime BuildID。先前 audit 使用 0758，不作新增幾何驗證結果。
GPU readback 已確認 compute 有輸出資料；R2 改 section palettes 仍主網格消失。MatrixGeometryAuditC001R2 檢查 1,251,679 個 sample vertices，矩陣插值最大誤差 0.518887 cm，超過 0.1 cm 門檻，尚未通過。下一版提高到 240 Hz 並回到每部件的小骨架，分離大型 instanced mesh 與非均勻縮放問題。

## 2026-10-03：Optimize for Instancing 核對與 R3 試點

本地 UE EngineTypes.h 的選項說明為「optimizes the bone map for lower transform storage memory costs for instancing and enables GPU instanced LOD」。SkeletalMeshRenderData.cpp 的 BuildUnifiedBoneMap 建立共用 bone map；此選項沒有合併材質 section，也沒有使不同角色網格成為同一個 draw。100 張不同卡應各自保留骨架及外觀，不可把此選項當成百人批次渲染保證。

Build760 再次在共用 NetCore DLL 鎖定處失敗；project/plugin 0760 已成功 link。新增 Tools/build_crowd_gpu_module.ps1 使用既有 response files，只編譯本 project 的 GPU pose／scene cpp 並 link 新 0761 module，避免再次觸發其他 Editor 的 engine DLL。project manifests 已備份到 Saved/Crowd/ModulePins/MatrixPartsR3，主模組 0761，plugins 0760，私有 runtime BuildID 維持不變。

MatrixPrepareC001R3.json：48 可見部件、72 sections、最大 301 required bones；240 Hz 完整矩陣快取建立成功。BenchmarkMatrixPilotC001R3.png 已能顯示完整角色，未見 R1/R2 的身體消失；既有過亮材質問題仍存在，不能視為外觀完全驗收。MatrixGeometryAuditC001R3.json：13 時間點、1,251,679 個頂點比較，最大誤差 0.115319 cm，仍超過 0.1 cm 門檻，因此不可發布為已驗證 GPU 動畫。提高取樣率前應評估百人 GPU 記憶體，再驗證中間時間及循環接縫。

單人診斷 BenchmarkMatrixPilotC001R3：frame mean 2.6404 ms、P95 3.3058 ms、GT 1.4533 ms、RT 2.6272 ms、GPU 0.9335 ms、CPU evaluators 0、GPU parts 48。既有 CPU pose 的 BenchmarkCpuComparisonC001R3：frame mean 2.4075 ms、P95 2.9999 ms、GT 1.5142 ms、RT 2.3993 ms、GPU 0.9240 ms。兩次使用不同 renderer runtime，只有一次測試，不能當嚴格 A/B 結論，也不能外推 100 人；目前沒有證據證明整體變快。完整矩陣路徑維持 opt-in，預設保持既有 PackedRig。

修正驗收漏洞：GPU provider enabled 與時間前進並不代表畫面／骨骼正確。GPU 報告增加 gpu_pose_validation_pending=true；target_met 在 GPU 路徑驗收完成前固定為 false。complete 仍僅表示載入／時鐘診斷完成，不等同視覺驗收。下一步需完成同 runtime 的重複 A/B、幾何／法線／循環與實際 GPU readback 驗證後，才能解除此限制。

## Optimize for Instancing 開關的獨立實驗

InstancingBonesR1 複製 PackedRig mesh，只改每個 LOD 的 bOptimizeForInstancing；保留原 SkeletalMeshComponent、CPU pose、動畫、骨架、材質、section、碰撞與畫質。檢查渲染後的頂點位置、法線／切線、每個 section 的骨骼索引對應與權重，避免把建置成功誤當成 skin mapping 正確。這組測試不等同 GPU pose 轉移，也不等同 100 人 instanced draw 合併。

先前 BenchmarkInstancingOff100R1 啟動時資產建置仍在執行，已停止該 owned benchmark 並標為 discarded；不可用於 A/B。之後所有正式量測均須等 owned 資產建置工作結束，並使用同一 runtime，以 Off/On/On/Off 順序重複測試。

原單一建置工作已完成 C000–C009，為縮短準備時間，建置程式加入經驗證的 partial asset resume，將剩餘工作分成四個不重疊的區段。只停止本 task 的建置 process；其他 Editor 不受影響。這是資產準備並行，正式遊戲仍固定 8 個 TaskGraph workers。

2026-10-03 補充修正：section bone map 重複數量不能當成每幀 transform 上傳量。核對 SkeletalRenderGPUSkin.cpp：一般 GPU 蒙皮的 VertexFactory allocation 路徑也支援 FUnifiedBoneBuffer；SkinningSceneExtensionProxy.cpp 的 Scene allocation 路徑在 unified mode 只使用第一個有效 section 的 bone map。先前「效益依賴 instanced 元件」的描述過於絕對。應量測 actual frame cost，不能依 964 萬 section references 直接判定成本增加。

100 張開關版本已完成（InstancingPrepared100R2.json），505 個 mesh，10,882,117 個渲染頂點的映射、權重、法線／切線檢查通過；材質與 section 保持一致。三月七 InstancingVisualC001R1.png 顯示完整角色，無部件消失；既有過亮材質問題仍存在。此畫面驗證與資產建置並行，效能數字不納入正式比較。

PowerShell 執行 rg 時再次把含 * 的 Windows 路徑直接當參數，搜尋失敗；修正為 rg DIRECTORY -g 'PATTERN'，不得將 shell 未展開的 wildcard path 傳給 rg。這類讀取失敗不影響資產，但增加無效查詢，後续須遵守相同搜尋方式。

四輪 benchmark 都保留既有 CommonUI viewport client 初始化錯誤：目前 game viewport client 不是 CommonGameViewportClient 的衍生類，CommonUI routing 不完整。此錯誤也出現在 Off 組；100 人行走與本次專用 camera controller 的測試可完成，沒有將 UI 修正混入本次建置開關比較。

InstancingABR2 四輪完成，exit 均為 0，selected=loaded=animated=100、failed=0，8 workers，所有 LOD 0，100 CPU pose evaluators。Off1 40.3567 ms／P95 47.7362；On1 45.2212／54.3296；On2 38.6626／40.9565；Off2 36.9667／41.6895。開啟兩輪平均 41.9419 ms，關閉 38.6617 ms，每幀成本增加 8.4844%；兩次重測本身亦有約 9–17% 波動，因此不宣稱固定回退百分比。沒有量到可證明收益，預設保持 PackedRig，InstancingBonesR1 留作手動實驗。不能把已完成的開關測試寫成 GPU pose 遷移完成或 120 FPS 達標；相關 GPU matrix 驗收仍未完成。

## 2026-10-03：低模衣服不使用彈性
- 使用者批准低模衣服關閉彈性，不能凍結所有服裝 palette transforms；其中也包含正常身體行走、AssignedWeights 與私有骨架綁定。
- 新增離線 `OK_NO_CLOTH_DYNAMICS=1`，共用服裝 DynamicBone 與私有衣服 particles 都略過，保留身體、頭髮與飾品的原有設定。高模既有 clip 不修改；低模需重烘服裝動畫軌。
- GameLowC001CanonicalR1 退出 1 且無具體 Error：拒絕發布；已加入 reduced reference blend singular 診斷，不可把 exit 0 或 bounds 當完整外觀驗收。
- GameLowC001CanonicalRigidR2：只改 plugin DLL suffix 導致舊 import library 同時載入兩份模組、重複註冊 UObject。R3 絕對 module manifest 路徑亦不支援。已恢復 KKUnityBundle-0760 manifest，專用離線烘焙直接呼叫本 project DLL 靜態連結的 native C ABI，避免影響其他 Editor 與舊 plugin。
- Windows rg 不接受把 wildcard 當 literal 搜尋路徑。後續用 `rg ... Docs -g 'CROWD*.md'` 及 `rg --files` 先確認所在模組，避免猜測 KKSharedAnimation.h 路徑。

CanonicalRigidR4 診斷：lod0 vertex93 的 reduced weights 生成奇異矩陣；簡化器插值 palette 可能造成不可逆混合。加入只針對奇異頂點、距離限 5cm 的最近原始綁定回復，不使用奇異矩陣求逆；若無安全來源則拒絕整卡。需外觀驗證，不能視為已解決。原生衣服回歸 1 passed，6 clothes、21 other dynamics、body_motion 0.87674916。

CanonicalRigidR5 原生衣服重烘與 bounds 3 LOD × 7 samples 通過，但遊戲實際畫面仍有腿部缺失，拒絕批次發布。UE GetMeshDescription 會利用 RawPointIndices2 把不同 render vertices 合併；簡化後它們的 palette 權重可能不同，不能依原始 seam mapping 合併。改為保留每個 render vertex，再修復 affine 權重；加入按材質的 posed surface area 檢查以補強 bounds。

SafeSurfacesR1：替換 16 個受損材質區塊後，MeshDescription triangle IDs 有空洞，觸發 FaceSmoothingMasks.Num()==TriangleCount assert。新增 Compact(Remappings)，在交給 skeletal mesh builder 前整理 element IDs。StageGeometryR1 已確定 reference description/build area0 約9012，reduced reference area0約1464，問題發生在 generic reducer。

SafeSurfacesR2：Compact 後確認下一個 assert 是空 polygon 的 PerimeterEdges.Num()==Triangles.Num()+2；DeleteTriangle 不會移除原 polygon。改用 DeletePolygon 替換整個需恢復區塊，不留下空 polygon，再 Compact。

SafeSurfacesR3：lod1 material56 的面積389.47／原始22.60，被 surface audit攔下。AuthoredBindingsR1 改保留 signed-palette 與小部件的原始編碼座標、原始權重，避免反覆世界姿勢轉換；3 LOD × 7時間點完整通過，遊戲 screenshot 四肢恢復。主網格大多退回原幾何（136301 triangles），不能宣稱減面目標完成。cloth dynamics 原生／UE 解碼／實際遊戲驗證完成。

## 2026-10-03：等待遊戲截圖完成後再讀取
GeometryTransferC001R1 測試仍在啟動時提前讀取 PNG，檔案尚未存在。之後先確認 screenshot 檔案及 benchmark 結束，再做影像檢查；這不是模型測試失敗。

## 2026-10-03：幾何簡化與矩陣骨骼權重分離
上一版直接讓 UE skeletal reducer 處理 synthetic affine palette，再用整組恢復規則保護 signed palette，導致三月七主網格三個 LOD 均保留約 136301 面。修正為 reference pose 空間中以單一 root 權重暫時簡化，按材質與空間位置轉移原始完整權重，使用原始 encoded 頂點作數值穩定錨點。不得讓減面器拆散或重分配矩陣的三個 column。GeometryTransferC001R1 通過 3 LOD × 7 行走時間點 bounds/posed area 檢查，主網格 59306/36395/24862 面；保留 31 個小型或覆蓋異常材質群，69 section 未合併。

## 2026-10-03：TObjectPtr 容器迭代編譯修正
DetailTransferR1 新增診斷時再次用了 auto* 迭代 TObjectPtr 容器，MSVC C3535。改用 const auto&。既有紀錄已提醒此限制；之後新增 Residents 迴圈先比照現有迴圈，編譯完成後才啟動測試。

## 2026-10-03：LOD 表面回復後面數逆增
C079 遠級修復兩個材質群後 24526 > 24414，不能直接通過，也不能放寬允許遠級變昂貴。回用前一級已重建的 encoded geometry/weights，另補數值診斷。GameLowMonotoneC079R1 通過，主網格 45626/24414/24414，保留原始失敗紀錄。

## 2026-10-03：百人未 Cook 載入與效能未達標

GameLow100FirstR1：100 名不同 hash 角色均行走，但程序啟動到 ready 378.556 秒，平均 frame 28.548 ms / P95 30.880 ms，RT 28.343 ms、GPU 3.225 ms、可見 section 5945、shadow section 0。GameLow100RandomWarmR1 暖快取換序仍 355.116 秒，frame 27.524 / P95 31.495 ms，不能宣稱小貼圖與減面已達一分鐘／120 FPS。首次 NullRHI 準備並未消除遊戲中的貼圖平台資料建置；暖載入仍有 package PostLoad／skinned asset 等待。下一次只在載入阶段暫停尚未就緒的角色 tick/render，增加 async loading budget，ready 後恢復原設定與完整動畫頻率；此措施尚待實測，不等同 Cook 完成。

## 2026-10-04：載入階段隱藏角色與材質旗標

GameLow100LoadingGateR1 在載入階段暫停尚未就緒角色、100 ms async budget，ready 後恢復 5 ms／300 ticking skin parts／100 evaluators。載入 143.672 秒仍不及一分鐘，frame 54.795 / P95 77.482 ms 出現回歸，未當成效能通過。此 case 有 2448 次 missing skeletal material usage 警告。Engine SkeletalMeshSceneProxy.cpp 明確以 default material 替換缺少 usage 的材質；需修正 gameplay 低模 parent 材質的旗標，保留原始資產與備份，不把角色偏白當作已通過。下一個 far-outline LOD 只使用 WPO 標記的描邊材質（原生生成器僅 outline 使用 WPO），不更動動畫頻率、不合併 section；此模式仍待重測。

### 2026-10-04 初始載入與驗收修正
- Auto 原本跳過 GameLow256R1，會回退 PackedRig，違反初始只載低模；改為預設明確選低模並禁止不完整時回退。
- 近景 pilot 在 ready 後立即升高模，讓初始載入驗證混入高資源；改為玩家鏡頭移動後才允許按距離載高，切換測試例外須明確指定。
- FarOutlineLOD 加入 actor tag 後，Tags.Last() 不再是角色索引，可能讓多人走同路徑；改為按 CrowdIndex_ 名稱搜尋。
- 修改 dense 單行程式時 apply_patch 行匹配失敗，未產生部分更新；重新以完整匹配修改並檢查。
- 現有低模仍有三種 LOD，重卡遠景仍逾 30,000 面。未通過新規格，保持未完成狀態；不可只用平均面數宣告通過。
- 新版初始載入檢查改由實際選角路徑推導範圍，不固定 C000–C099，支援玩家任意選角所在目錄。
- LowOnlyEntryC001R1（0802）：Auto 選 GameLow256R1，initial_high_meshes=0、initial_high_or_large_textures=0、initial_low_only_met=true。近景 30 秒 detail_switches=0、high_detail_residents=0、animated=1。process load 33.730 s 為單角診斷，不能宣告百人 60 秒通過。
- schema1004 最遠 LOD 加總全身：29/100 卡 >=30,000 面；C010=1,571,616、C015=215,121、C029=122,741。這些是阻止最終低模驗收的證據，不能忽略高面數異常卡。
- 百人 LowOnlyRandom100R1 遊戲通過初始資源隔離：高mesh=0、高/大texture=0，100人全動畫、100不同SHA1、退出0。123.792秒載入、mean53.401/P95 60.972ms，未達60秒/120FPS。不得把只載低模等同已達效能目標。
- 既有部分低材質 missing skeletal usage 警告仍出現；0803修了新建材質與提供repair工具，但此批舊1004快取尚未執行material repair，外觀/效能不得視為最終驗收。

### 2026-10-04 單一低模小表面修正
- SingleLowC010R1 全身27,966面，但material34的17.574 cm²小表面被全局簡化移除，七姿勢覆蓋驗證拒絕保存。批次C000/C051/C052也有約18–24 cm²表面丟失。
- 不放寬表面驗收、不把整個高模section還原。加入有上限的小表面恢復：只針對10–100 cm²缺損材質，按canonical位置網格聚類，保留來源完整權重、UV、法線與私有骨架，總計使用最多約1700面安全餘裕；仍須重新通過原姿勢與面積檢查。C010未通過前不得進場驗收。
- 發現RegenerateLOD後PostEditChange可能再次執行減面，重卡重複數分鐘；單一低模避免多餘PostEditChange，仍等待Build完成。
- 預覽證據檔有Benchmark前綴；曾錯讀SingleLowVisualC001R1.log不存在路徑。後續使用工具實際回報檔名。
- 小表面恢復第一次在C000觸發GPU affine權重檢查（SmallRecoveryC000R1），未保存。將恢復頂點留在canonical座標，統一走已驗證的完整來源權重與encoded位置轉移路徑，避免另一套權重/座標處理；重新驗證，不取消權重檢查。
- 原完整專案Game build被原生預覽/材質匯入Editor API阻擋。新增隔離DevelopmentHost構建工具：相同OpenKoikatsu和KKImportedAssets runtime類別及資產路徑，只編譯百人正式場景與Cook資產播放，不載原生預覽或KKS解析模組。原編輯器與原專案未更換。主專案完整Game構建尚非完成。
- SmallRecoveryBindingsC000R1仍未通過affine欄位檢查。進一步發現簡化與Compact會改變polygon group ID，不能用低模group數字當來源材質索引。修正binding轉移與小表面恢复，以穩定的ImportedMaterialSlotName/PackedSlot名稱匹配來源；新增group使用空閒ID，避免覆寫另一個材質group。檢查不放寬。
- StableMaterialBindingsR1編譯失敗：FPolygonGroupID沒有IsValid()；改用GetValue()<0判斷未初始化ID，使用新DLL suffix重新構建，未發布失敗DLL。
- SurfaceWeightsC000R1診斷確定實際原因：__ok_palette_0_83三欄被第二次減面剪成單欄65535。Final mesh複製了暫存root-only簡化器的「絕對面數終止條件與MaxBonesPerVertex=1」，恢復小表面後超出暫存面數便再次自動簡化，剪掉affine欄位。修正Final終止條件為100%保留、取消絕對面數上限、MaxBonesPerVertex=12；最終只由全角色<30,000驗證限額，不由暫存簡化器再減面。之前slot名稱修正仍有必要，不能以第二次失敗認定已解決主因。
- FinalWeightsC000R1（0812）通過：恢復四個小表面後整人29,099面，singleLOD=1，七姿勢表面/範圍驗證通過，完整GPU affine權重檢查通過。確認終止條件修正解決該卡單欄剪除，未放寬驗收。
- 第一次Cook採用UE預設ZenStore，不產生預期loose .umap；後續工具明確使用SkipZenStore，保存第一次Cook日誌作為錯誤紀錄，DDC編譯成果可重用。
- FinalWeightsC010R1完整權重已通過，整人28,768面；material34去除重合三角面後面積總和1.619/17.574，原總面積驗收仍拒絕。不能把重合面積總和直接當表面覆蓋。加入僅限<100 cm²小表面的雙向幾何覆蓋檢查：所有頂點與三角形中心，來源→低模和低模→來源，七個行走時間點都必須距離對方三角面<=0.25 cm；缺失面、不相符輪廓與動作仍拒絕。大表面原面積/範圍標準不變。記錄每次幾何驗收，不直接略過這些材質。
- Stage-PilotC001R1：UAT即使skipbuild/skipcook仍要求Editor Target規則，新增僅供UAT判定的Editor target，不構建或替換Editor DLL；新tag重新Stage。
- Stage-PilotC001R2：Engine未建置UnrealPak.exe，Pak階段失敗。檢查primary與隔離Engine均無該檔後，以本地UE源碼建置UnrealPak（8個編譯worker）；只新增打包工具，不替換共享Editor binary。
- DevelopmentPilotC001LooseR1正式遊戲啟動但找不到低模variant；CookDir沒有把新建Character/Walk收進Cook（原資產未呼叫AssetRegistry AssetCreated）。改為explicit PACKAGE列舉Character和Walk，讓硬依賴跟隨；Cook驗收增加每張卡的Character文件檢查，不只驗map。保存失敗Cook與遊戲日誌。
- Cook工具文字替換使用雙引號導致PowerShell提前展開腳本變數，出現foreach語法錯誤；改用literal apply_patch修正，後續腳本含變數的改動直接用literal patch。該次未啟動Cook。
- DevelopmentPilotC001PakR1載入低模時在ACL curve compression設定反序列化崩潰，正式Host關閉了已Cook動畫需要的ACLPlugin。依日誌/ACLPlugin資產路徑補上ACLPlugin runtime依賴並重建/重Stage；不是用Editor內播放代替正式驗證。

### 2026-10-04 Blender、1萬面試點與正式 profile
- ACLPilotC001R1：修正 ACL runtime 依賴後，Cook Development 單人通過，process→ready 3.635 秒、mean 2.005 / P95 2.514 ms、高模/大貼圖 0。單人不計為百人 60 秒或 120 FPS 驗收。
- Blender 5.2 已安裝；幾何交換只包含 canonical 位置與所有 UV channels。UE 保留原私有骨架、完整 affine 權重、動畫；不得用一般 FBX 重新匯入 rig 取代 signed palette。
- Blender10kC001R1：整人 9,588 三角面、單一低 LOD、貼圖≤256²，完整權重與七姿勢變形驗證通過。這是替代低模配置試點，不增加遊戲第三種模型層級；尚非百人載入或效能成績。
- BlenderC028R1：均分小部件預算不足，material29 無法壓入45面；Blender Python exception 預設程序仍返回0，UE因缺 output 拒絕保存。加入 --python-exit-code 1 與逐次減面計數；按來源面數分配剩餘預算，先保留最多12面小部件最低額。不發布失敗卡。
- BlenderC092R1：全身29,463面，material39恢復後面積83.861/310.326，仍被原面積守門拒絕。新增對所有面積異常 section 執行嚴格雙向幾何驗證（每頂點與三角中心≤0.25cm、七時間點），面數/完整權重/動作驗證不放寬；不得只忽略面積失敗。
- 新增正式 Development 的暖機 trace 與載入 trace 兩種獨立模式，避免暖機開始覆寫正在錄製的載入 trace。每張卡另記 package_wait_ms / character_build_ms；前者含排程與等待，不直接當磁碟 IO 時間。
- 修正 Development 報告與 control 使用 Game 的 AllowAsyncRenderThreadUpdates，Editor-game 仍使用 EditorGameWorld 變數，避免報告錯誤設定值。
- Low10kC001FunctionalR1：程序成功進場但 VSM 觸發 BlueNoise.cpp:75 handled ensure，退出非0；Cook 的 SkipSoftReferences 漏掉 renderer 的 BlueNoise、Hair 引擎貼圖。補 explicit engine PACKAGE，不停用 ensure、不把該次含錯誤資料當成有效 FPS。載入 trace 也出現反覆 FlushAsyncLoading，需修正資源後重測。
- Profile export R1：Insights 命令的帶引號反斜線路徑被解讀為 escape，CSV 寫到錯誤相對路徑；改用斜線絕對路徑並驗證三個CSV存在。UE 5.8 export sortBy僅支援TotalInclusiveTime，使用該值；分析時再按Excl欄位排序。增加thread清單，Render* wildcard避免實際RenderThread名稱不完全一致。
- BlenderMicroC028R3 通過：203個離散小飾件使collapse有拓撲下限。≤0.5cm微小島使用來源頂點的小平面代理，整人29,451面、完整affine權重/七姿勢範圍與表面驗收通過。無第三種遊戲LOD。
- BlenderWeightedC092R2 通過：全身29,465面、單LOD、完整權重/七姿勢驗收通過。
- BlenderMicroC010R3：material73仍無法減入13面，失敗未保存。依使用者「刪不下去就算了」，對難減表面保留清理後來源幾何，改檢查所有恢復部件共同總預算；降低其它部件 base budget 至26,000、保護餘裕3,500再試，不刪除硬部件、不放寬整人<30k限制。
- PreserveC010R4：保留所有清理後原始表面需22,562面保護額，超過3,476，拒絕保存。改保留Blender已完成減面但再也不能縮小的結果（collapse floor），按共同預算只減容易縮小的部件；所有原行走表面檢查仍需通過。
- PreserveFloorC010R5主網格28,936面通過完整權重；後續一個小飾件分配4面，UE第二次RegenerateLOD失敗卻缺少具體錯誤日誌。提高小部件最低預算12面（整人仍<30k），補上reducer mesh/budget錯誤。不得用「0 error(s)」判斷commandlet成功，exit=1及complete receipt缺失仍算失敗。
- 正式Host默认改用GameLow30kR1；禁止默認選舊三LOD的GameLow256R1。主Editor模組需使用包含此變更的0821或更新版本，既有GameLow256R1只保留歷史驗證用途。
- SmallSurfaceC010R7整人29,136面，但material73表面距離偏差1.395cm，面積22.053/84.311，不接受該減面結果。依使用者指示保留material73完整原始encoded頂點、完整权重和UV，跳過nearest binding轉移；再降低其它部分base budget至24,000、保護餘裕5,500。其它小部件的每part保護額至少24面，解決原最小額12大於按比例reserve4的silent return。
- 提供離線Blender結果重用，只允許canonical位置/UV/材質群的完整幾何請求一致，budget/reserve單獨重新驗證，不重用rig或動畫。每次輸出仍通過總面數與行走驗證。
- Cook StrictLow99ProfileR1 的corelimit9只產生6個shader worker，未充分利用9950X3D的32 logical processors。驗證本次Cook PID72116及六個直接SCW子程序的路徑/parent後重啟離線Cook，24個shader worker，保留DDC與生成資產；未動其它Editor或程序。此為匯入/打包速度設定，遊戲測試仍8 workers，不算遊戲FPS優化成績。
- Build tag SmallSurfaceBudgetR1 / R2 已存在於較早測試，工具正確拒絕重用；改為含日期時間的新tag，不覆寫舊證據。
- PowerShell環境rg檔案路徑含*不是可靠glob；搜尋目錄配合-g篩選檔名，Saved與Content搜尋另加--no-ignore。不要再把帶*的檔案路徑傳給rg。
- KeepMaterials在所有部件呼叫時，某些小部件並無material73；應跳過不屬該mesh的group，不能誤報整張卡失敗。該修正包含在0824。
- StrictLow99Workers24R2重新利用SM6快取後轉而编譯大量未使用的SM5。新增Config/Custom/CrowdCookSM6/DefaultEngine.ini與-CustomConfig，只對正式DX12/SM6測試Cook清除DX11/Vulkan formats；一般DefaultEngine.ini不修改。驗證本次Cook parent及直接SCW後重啟R3，保存R2restart receipt和日誌；已編譯的DDC繼續重用。

- C010 R8: preserved material73 was added after the full recovery reserve (30,300 main triangles). R9/R10 reserve excludes preserved geometry. R10 main deformation passes at29,140 triangles, but tiny part3 fails posed bounds after reduction to10 triangles; add explicit KeepSmallParts, retain full encoded geometry/weights for small authored parts and keep strict whole-character budget. Recovery trigger now matches acceptance area ratio0.30 rather than replacing already-acceptable sections below0.65.
- DevelopmentBlueNoiseFixedNoTraceC001R3: exit0, initial high mesh/large texture0, load1.597s; single-card evidence only. StopLoadTrace20261004T0321: stopping startup trace atready did not fix exit777003; do not accept that run. WarmTrace20261004T0326: CPU/GPU warm trace exits0, load1.626s. Narrow startup trace to cpu/frame/bookmark to isolate loadtime/file channel shutdown failure.

- KeepSmallC01020261004T0327 completed: whole28,678 triangles, retained authored small parts140/408/812 triangles; all5parts singleLOD, fullweights and seven poses accepted. All100 low cards now prepared.
- Insights UE5.8 ExportTimerStatistics hardcodes GPU queue inclusion even with CPU thread filters; its Game/Render CSVs are not pure CPU tables. Added optional event exports and streaming CPU-only aggregator. Reject infinite end times from open scopes at Trace.Stop. Use bounded3-second warm window to limit export volume; cross-thread exclusive sums still not frame times/draw counts.
- Scene-load trace change initially did not reach launcher: quoted -tracefile form did not match string replacement, so previous run still used startup trace. Fixed actual argument line and verified launch receipt before judging new SceneLoadingTrace implementation.
- Low100 baseline completed/exit0: load8.663s, mean27.652/P9530.923ms,100unique/100animated, highmesh/largeTex0,5793visible sections. Warm trace completed/exit0: load7.518s, mean25.490/P9530.388ms, Render25.378/Game12.859/GPU2.639ms. 120FPS not met. Workers24 request yields23 measured workers and27.105/P9531.687ms; no performance benefit, retain8default.

- ActualSceneLoadTraceC00120261004T0410 receipt confirms CrowdLoadingTrace and no startup -tracefile, but still exit777003; loading-phase recording remains broken. Disable -LoadProfile with clear diagnostic to avoid repeated failed runs. Loading per-card counters stay available, warm -Profile is verified.
- No destructive file deletion was needed. KeepSmallParts retains authored geometry only under explicit threshold; per-part allowance may exceed proportional allocation but final whole-character triangle guard still applies.
- WPO outline audit: all current importer WPO assignments are in KKMainItemPlus outline branch and MakeOutlineMaterial in KKPreviewActor. Far-outline test reduces section5793→3680, avg27.652→21.729ms, P9530.923→25.217ms. Enable for game-low default, -CrowdFullOutlines preserves comparison path. No offline material merge/atlas or lower animation rate.
- Full-room camera lacked the manual exposure already applied to pilot camera. Apply manual exposure and zero bias to both cameras, then verify the new screenshot; do not declare existing overwhite issue fixed without visual evidence.

- LowHigh10020261004T0450: process exit0 produced no log or high assets. UE FCommandLine::MaxCommandLineSize is16,384, not Windows32,767. Request29,803 characters exceeded UE buffer. Previous verification only checked pre-existing low assets and falsely accepted Cook; reject no-log exits, verify requested Character+Walk for both low/high, sequentially split requests before16,000 chars. Do not run parallel Cooks against same output.
- RandomDefault20261004T0440:100 shuffled unique prepared cards, default far-outlineLOD + manual exposure, exit0; load7.688s, avg21.769/P9525.182ms. Screenshot visibly restores room/character contrast compared to auto exposure, but close-up color fidelity still not fully accepted.

- UE SaveStringToFile JSON report may be UTF16 with BOM. Python analysis must detect BOM; UTF8-only read caused UnicodeDecodeError, fixed by decoding UTF16 for FF FE/FE FF. CSV event exports remain UTF8.
- CPU event aggregator fixture verified nested parent/children, sibling scopes, independent threads and infinite open scopes; parent10ms minus children3+2ms=5ms exclusive.

- Chunked low/high Cook A/B completed, verified all100 low/high Character+Walk assets. D-stage succeeds (~5.92GiB pak), preserving C low-only stage. C001/C010 Cooked high-low round trips pass exit0, initial high/large textures0 and phase error0.
- FullCookRandom100Final20261004T0530: load7.359s low-only100, exit0, but avg28.900/P9541.923ms versus earlier21.769/25.182. GPU1.944ms and same3680sections; do not cherry-pick faster result or claim120FPS. Repeat same full-stage setup without compilation/conversion running.

- FullCookRandom100Repeat20261004T0535 confirms slower full-stage performance: load7.179s, avg30.354/P9541.388ms, exit0. FullCookWarmFinal20261004T0540 exits0 with validated warm trace; avg25.502/P9536.349ms. Keep all evidence; no120FPS claim. CPU-only3sec events identify MeshObject Update3708.91ms, animation extraction3535.48ms and ParallelDraw3491.21ms cross-thread work, RenderThread waiting2293.81ms. Current frame bottleneck remains CPU; further triangle reduction is not justified by loading time.

- CompactLowC00120261004T0610 exited0 but ran the old Packed branch, no compact completion/asset. Build helper compiled the generated unity UHT module, not OKCrowdPacked.cpp (separate link object). Add that compilation unit explicitly; do not accept exit0 without requested manifest and compact audit log. Pilot did not publish R2.

- CompactLowC001Actual20261004T0620: bones2357/2507 (~6% saved), but reused AuditRig GetCPUSkinnedVertices asserts MeshObject under NullRHI. No R2 assets published. For compaction, exact source mesh-description geometry/UV/normal/named weights and parent/reference poses are already proven; compare every retained component-space bone pose by name at13times without requiring RHI MeshObject. Never ignore the assert or claim a saved asset.

### 2026-10-04 Compact pose audit compiler guard
- Build 0831 failed with C2181 because UE_LOG macro was used directly before else. Add braces around both branches; never infer success from a launched compiler.
- The compact audit counts retained bone pose comparisons, not skinned vertices; static SameSkinSource independently checks geometry, named weights, reference transforms and attributes.

- Summary-shell parser fix: PowerShell foreach statement cannot pipe directly; collect its output using @(foreach(...){...}) before piping. The failed read-only command made no asset changes.

- CompactLow100 ABBA completed: R1 avg24.419/32.735ms vs R2 avg22.249/28.815ms; P95 does not improve consistently and later runs slow down in both variants. Retain R1 default, opt-in R2 only. Bone totals improve7.262% but section bone references273060 and visible sections3680 do not change; do not equate reference skeleton compaction with reduced draw submission or claim120FPS. Prepared-card load7.4–9.9s does not include first arbitrary KKS/APNG import.

- 2026-10-04 follower investigation: repeated Windows rg glob-path failure (AnimSequence*.h as a path). Search its directory with -g 'AnimSequence*.h'; shell globbing is not supported by this PowerShell invocation. No code/data mutation from failed search.
- RefreshFollowerComponents only marks render data/socket transforms; it does not replace follower morph-curve refresh. Leader-driven follower optimization is restricted to single-LOD/no-morph/no-physics followers with verified bone names/mappings. High-detail meshes retain original ticking.

- LeaderDrivenFollowers20261004T0900 build failed C2039: LeaderPoseComponent is USkinnedMeshComponent, which has no GetSkeletalMeshAsset. Cast to USkeletalMeshComponent and skip other leader types. Do not stage a failed build or infer the new behavior is in the old executable.

- LeaderDriven pilot launch was attempted while staging still returned a running session. Stage temporarily removes/replaces its executable; launcher correctly refused missing binary before starting the game. Wait for verified stage exit0 before dependent launch. No pilot evidence was produced.

- Leader-driven follower profile did not demonstrate a useful frame benefit: Off25.754/P9529.243ms vs On34.050/P9544.733ms. On has101 ticking parts, not100; one visible morph follower correctly retains Tick. MeshObjectUpdate remains300 per complete frame and animation extraction about100 per frame. Tick exclusive saving is only~0.0124ms/frame cross-thread; duplicate dirty marking is idempotent, not a second full mesh submission. Reject default enable; do not equate fewer ticking components with large CPU savings.

- Masked front screenshot shows face/mouth gaps, but OriginalFrontC00120261004T1210 has the same gaps. Initial attribution to masked conversion was premature; do not claim a regression without the same-camera baseline. Masking does alter iris/soft-edge appearance and has no pilot FPS benefit, so do not batch/enable it. Compare high-detail front to distinguish pre-existing authoring/material failure from low mesh reduction. FrontCamera diagnostics explicitly disable high auto-loading to avoid comparing mixed models.

2026-10-04 口型診斷核對：UE5.8 ResetMorphTargetCurves 只清 active target/weight buffer，不清 SetMorphTarget 的 MorphTargetCurves map；UpdateFollowerComponent 會重新套用自身 map。因此不能以「follower Tick 擦掉設定」解釋單次 close=1 診斷未改善。高低模皆有問題，根因尚未證實，禁止直接對全部人物強加口型權重。

2026-10-04 骨骼緩衝ABBA：VertexFactory移除ProvideMeshObjectTransformsTask但增加ProcessUpload及D3D12 upload allocator工作；不能因單一profile scope消失宣稱優化成功。A Scene22.731/P9526.691、B Vertex31.647/P9538.556、C Vertex31.344/P9535.164、D Scene31.693/P9542.451；後輪Scene變慢，無穩定frame收益證據，保留Scene。詳細證據BoneBuffers100ABBA20261004.json。所有4輪exit0、100unique/animated、初始low-only、動畫未降頻；120FPS未完成。

2026-10-04 Selective SRT：不能把所有 affine palette 直接縮成一般SRT；C001有組別保守位置誤差界接近49cm，必須按完整片段逐組篩選。最初只檢查「每個插值時間重新分解矩陣」不足以代表新動畫；已改成檢查「分解後的兩個key再插值」，並另用實際compressed clip驗證全部key和quarter-frame。位置通過不代表法線通過，現在發佈manifest前同時驗證法線。

2026-10-04 Selective SRT shadows：新低模的_srt骨骼不存在於原高模，不能把低模影子proxy套到高模leader。這個試驗在高模階段改用高模自己的陰影，沒有新增或降頻第二個動畫evaluator。

2026-10-04 讀取KKImportedAnimSequence.cpp失敗：此類實作不在預期的同名Private檔案。使用rg --files定位，不能假定每個Public header都有同名cpp。UE5.8編譯提示GetVertexVertexInstances已deprecated；已改用GetVertexVertexInstanceIDs，避免下一版API移除後重犯。

2026-10-04 Profile-first結論：不能因Selective SRT骨骼/軌道减少就批量轉換。單卡Animation Extract約省12%，Mesh update約省10%，但71個section不變且整frame沒有改善；暫停擴批。Masked幾乎消除dynamic draw-command generation，但ParallelDraw增加，frame持平，不能只盯單一scope。

2026-10-04 等待解讀：Render Thread UpdatePrimitive約8.9ms wait實際由FScene::Update的WaitForOutstandingTasksOnly觸發，不能盲目歸因GPU或当前動畫；RenderFinish約4.9ms為mesh pass setup同步。Scope exclusive是時間跨度，仍可能包含OS descheduling，不能當作純CPU active工作或直接相加。

2026-10-04 搜尋仍錯猜RenderingCommands.cpp與SceneUpdateTask.cpp路徑，檔案不存在；已改用資料夾rg搜索stat symbol定位RendererScene.cpp。後續先rg --files或資料夾symbol検索，避免重複猜不存在檔案。

2026-10-04 Section LOD API注意：ShowMaterialSection實際按MaterialID隱藏整個slot（包含同材質所有骨骼chunk），並每次MarkRenderStateDirty。不能逐chunk計算小範圍後隱藏slot；不能每幀無條件呼叫。本次按slot聚合501個行走樣本，僅在可見狀態改變時呼叫，使用近景恢復與像素遲滯。抽樣+25%padding仍不是數學上連續時間界限，先保持opt-in且保護morph/口腔/WPO。
2026-10-04 PowerShell中rg Tools/*crowd*.ps1不是有效glob路徑，已重犯並修正：使用rg -g '*crowd*.ps1' Tools。這是查詢錯誤，不影響編譯/資產。

2026-10-04 SectionBounds百人第一次輸出存在，但commandlet exit=1：部分動畫載入後壓縮DDC尚未就緒，GetBoneTransform enforce compressed觸發ensure並退回raw。該JSON不得用於驗收。修正為每卡CacheDerivedDataForCurrentPlatform並WaitOnExistingCompression後才取樣；成功必須同時具備exit=0與0 error。

2026-10-04 Section LOD第一次近景測試實際全部恢復（visible71、hidden0），但restores counter為0，測試失敗。原因是舊outline距離切換無條件恢復所有非outline slots，新策略看不到恢復事件。修正outline只更新真正WPO slots且僅當狀態變化時呼叫，避免策略互相覆寫與無謂render-state dirties；重新跑測試，不將失敗報告當通過。

2026-10-04 Section LOD相機投影修正：初版用viewport寬/水平FOV估算，不足以涵蓋UE aspect-axis constraint或letterbox；改讀LocalPlayer.GetProjectionData的實際投影矩陣與constrained view rect，取XY像素尺度最大值。重新編譯/復測，不能將不同演算法版本混成同一A/B。

2026-10-04 PowerShell統計查詢語法錯誤：不能將foreach statement直接接pipe；先賦值到taskRows再Format-Table。已更正，沒有修改測量資料。

2026-10-04 UE文字log的[frame]欄會modulo1000循環，不可拿跨區間欄位差直接換算FPS或判斷freeze；效能以完整JSON wall-frame samples及trace frame事件為準。

2026-10-04 worker16修正：corelimit17實際15worker；Windows logical>physical減2，>=16改Workers+2並驗證actual task_workers。RenderCull16C僅15worker+RHIWidth16。修正腳本首次PowerShell replace含不必要backtick導致ParserError，未執行任何修改；改Python純文字更新。再次猜錯HAL/WindowsPlatformMisc路徑，後續改symbol資料夾搜尋。

2026-10-04 FinalPose60Cull1755 build失敗C2248：AreRequiredCurvesUpToDate是UE private方法，不能僅看宣告附近下一個private label判斷access。移除額外曲線guard，試點限定已保存固定baked動畫，強制phase初始化不走cap，required-bones失效仍強制更新。不修改UE private API。

2026-10-04 60Hz報表語意修正：早期full_pose_tick_policy僅檢查AlwaysTickPoseAndRefreshBones enum/URO，新增manual cap後仍true容易誤解。改always_tick_pose_policy保留enum稽核，full_pose_tick_policy僅AnimationHz0才true；旧报告应同时读取animation_pose_hz，不可声称每帧完整求值。

2026-10-04 本輪再次錯猜Tools/ok-forge不存在，且rg KKForge*.cpp Windows glob路徑無效。已用rg --files定位CPP Forge與KKPreviewActor，再用資料夾-g搜尋。原pipeline WorldPositionOffset僅由Outline材質建立，調小outline距離試點仍保護HighDetailMain；不得把任意未分類WPO材質當outline。

2026-10-04 outline距離調小前發現替換角色會複製FarOutlineLOD tag，但新mesh section預設全顯示，若遠距tag相同會漏套用隱藏策略。替換時移除FarOutlineLOD以重套低模狀態；使用HighDetailResident tag保護所有高模（不依CompatibleShadowRig才有的HighDetailMain），切回low清除此tag。未把尚未修復版本當效能通過。

2026-10-04 百人benchmark舊報表固定3600-frame ring，在高FPS的45/60s測量只保留後段，不能當完整暖機後整段P95。舊數據保留但標為last3600；正式驗收改benchmark動態capacity=runSeconds*1000（最大600000），記錄seen/window/all_warm_frames_retained，target要求測量完成且無丟樣本。Play仍bounded3600。run_finished以前仅controlled case赋true，也修為普通RunSeconds期滿true。FocusedHighNormal16A=8.156/P9512.868，不因one-high就宣稱收益。

2026-10-04 追加查詢錯誤：SkeletalMeshRender.cpp猜錯檔名，往後只對資料夾rg symbol並用rg --files定位；77091已完成後誤poll得到Unknown process id，後續使用先前exit0結果，不重啟build。均沒有修改資產或測量資料。

2026-10-04 LargestL3Focus8A affinity首次set/read65535，但startup後實際變4294967295（未持續pin）；不能將此輪當96MiB CCD evidence，已另存invalid.json。改用UE原生processaffinity16（只在cache mask恰為前16logical時）並每秒監控owned PID：前10s允许startup重套，後續變更即拒絕驗收，保存final affinity檢查。沒有調整其他程序/Windows全域設定。SDK查實9950X3D L3 96MiB maskFFFF、32MiB maskFFFF0000。

2026-10-04 逐幀證據 BoneWorkBaseSeries20261004T1833 發現截圖在 ready12.115805s 造成109.9902ms停頓，原本12s截圖落入10s後量測。改為ready3s暖機截圖，完整保留10s後所有frames；舊數據不得聲稱已排除截圖干擾。pose-count/frame相關性亦受60Hz due數隨frame interval增加影響，不能當因果證明，需相同binary受控A/B。

2026-10-04 查詢cached MDC時再次誤用Windows檔案glob，已改folder -g。其後未加檔案filter的Config/Source/Tools遞迴搜尋意外長時間未結束，已終止該精確rg程序。PrewarmBalanced8R2受背景查詢干擾，僅保留原始結果不作正式A/B；後續benchmark期間不做遞迴搜尋。
2026-10-04 上述rg檢查時沒有匹配程序，工具隨後回報exit0；沒有實際Stop-Process。保留Balanced8R2干擾標記，避免將嘗試終止寫成已終止。

2026-10-04 Cap144R1首次mean6.977/P957.548 target true，但Cap144R2 mean7.260/P959.014 Require120正確拒絕。不能以單次通過聲稱穩定完成120。保留兩輪完整50s樣本與affinity證據，再測較少worker是否减少同CCD CPU爭用。

2026-10-04 記錄失敗設定：4workers/Cap144=15.091/P9527.173；16workers/Cap144=11.778/P9519.164；8workers/Cap144/AboveNormal=7.281/P959.086。未採預設，不因CPU特別好就一律增加threads/priority。新的160上限在8workers/96MiB CCD連續三輪mean6.417–6.567/P957.616–7.872通過，但P99尖峰未消失；不能聲稱所有鏡位/任選未準備角色卡/每幀都120。CPU telemetry只涵蓋launch前已存在且exit時仍存活程序，不把中途退出Editor等漏計當作沒有背景負載。根module0857/staged final與高低模round-trip均exit0。

2026-10-04 RT/GPU研究查dynamic data時又用了Public/SkinnedMeshSceneProxy*路徑glob，已改Public資料夾-g *.h。後續禁止在rg positional paths帶萬用字元，即使搭配-g也不會讓Windows展開。無匹配symbol的exit1要與無效路徑區分。

2026-10-04 RT/GPU研究補充：SkinCache.Mode1但CompileShaders0，不能稱為compute SkinCache啟用。本地vertex skinning已在GPU，GpuInstanceCull/batched1亦原已啟用。Follower60R1 profile P997.074，但FinalNoTraceR1 P9912.596，不能以一次profile宣稱尖峰消失。Uncapped舊Tick對照4.506/P959.143觸發Require120失敗，原始資料保留；新路徑3.905/P955.900。之前每幀pose follower回退未被抹除，本次重測因60Hz/update policy條件改變而進行。

2026-10-04 本輪最終記錄：FNoTraceR1 RT3.071/P9912.596，低於profile預期；同binary兩輪control RT約2.94，因此不能只報最佳2.442或P997.009。三輪follower RT平均約2.68/工作量MeshObject近半，無上限C4.218→F3.622約14.1%，採用並保留LegacyFollowerTicks。安全guard build/stage/root0858、三輪無traceP95、高低模round-trip均通過。PowerShell多種Select-Object接續輸出曾沿用首個table schema隱藏後續欄位，改ConvertTo-Json逐case，不把空白顯示當資料遺失。

2026-10-04 ShadowPolicy 試點教訓：原路徑每0.25秒更新一次，trace平均0.067ms；不是主要RT瓶頸。元件快取降至0.040ms，但不能將整幀差異歸因於這約0.027ms的低頻工作。150cm遲滯將shadow transitions710→480，P99卻12.233→14.022ms；不採遲滯預設，不宣稱減少切換就消除尖峰。該實驗也把新caster進入距離600→450cm，品質改變必須列出；原始報告保留。快取要檢查Actor替換、component count、expired weak pointers，且每次讀HighDetailMain tag，不能永久保存初始化component清單。RoundTripR1兩次切換、3次cache rebuild通過；單卡target false並非百人驗收失敗。

2026-10-04 ShadowPolicy最終採用修正：profile初看快取較快，曾暫設wrapper default true供測試；同binary無tracecontrol RT/RHI2.312/1.167，快取兩輪2.384/1.226與2.518/1.275，沒有證明每幀收益。最終wrapper恢復原策略，改CacheShadowPolicy opt-in，遲滯預設0；不能以低頻scope約40%減少就宣稱RT/RHI優化成功。此次全部100人P95通過且功能round-trip通過，但仍有18–22ms最大幀。影子設定試點不是消除P99的最優解。

2026-10-04 DrawBatch調查：錯猜OKCrowdAnimComponent.cpp不存在，改rg --files定位OKCrowdScene.cpp，不再猜檔名。PowerShell雙引號Replace把待寫入的$MinDraws提前展開成空白（產生'++'），檢查即發現，尚未啟動遊戲；改apply_patch保留字面变量，AST parser通過。後續編輯腳本優先apply_patch，不用有變數展開風險的Replace。新的batch設定加入報表與launcher驗證，不能只看命令列就當CVar生效。

2026-10-04 DrawBatch64ProfileR1新時間錨點揭露trace啟動在ready10.002s，10.090s frame88.596ms；先前profile在ready10s啟動也可能受到相同干擾。不能把這幀當正常RT/RHI執行或刪幀假裝已改善。修正所有warm trace在ready5s開始（仍記錄20s），正式frame window維持ready10–60s；重新build/profile同版本control，不與舊profile直接當受控A/B。NoTrace過去結果不受這個啟動問題影響。

2026-10-04 查詢再次猜TimingExporter.cpp位置錯誤，改直接讀已定位的TimingProfilerManager.cpp確認-timers逗號名稱filter支援，不再需要猜exporter路徑。export helper首行因BOM context apply_patch失敗、沒有變更，改File.ReadAllText做單引號字面替換後patch其餘行；未使用會展開變數的雙引號Replace。Filtered timing events只能計數/檢查完整scope duration；缺少children時不能套用exclusive時間推導。

2026-10-04 DrawBatch最終不採512：CPU-only matched ready10–24.9s tasks/frame49.77→14.54，但uncapped frame3.698→3.966、P956.055→6.789、RHI1.250→1.340較差；減少tasks會交換有效平行度，不能把task少71%當frame少71%。保留MinDraws64，256/512可選。TextureCacheSize256必須RHIInit前套用-dpcvars（DP init line2857在RHIInit3171前），ExecCmds太晚，initializer已鎖定；本輪實際report256，但20sCreateTexture仍2380次，沒有消除重建。兩輪無trace雖通過，不挑單次最佳當保證。

2026-10-04 allocator0採用限制：CPU trace消除暖機後CreateTextureInternal，保留原draw/動畫/外觀；RHI非串流貼圖配置719.383→830.969MiB（+111.586），不是總常駐VRAM。最後兩輪control RT2.924/2.353、RHI1.436/1.223；三輪pool RT2.565/2.318/2.183、RHI1.302/1.213/1.153，保留全部而非只報最佳。Default120s target true/完整110s/P996.842，但max33.029ms仍存在，不稱每幀穩120。memory snapshots warm-window56.317與110.006s均830.969MiB。DetailRoundTrip低高低2次通過、回低4followers/1tick，單卡target false合理。Build/stage/root0862與host/staged SHA256一致，wrapper採0、可-TransientAllocator1回原路徑。新卡首次APNG/低模準備/Cook仍不含在5.181s載入數據。

2026-10-04 ReportIdleLegacyProfileR1排除假設：5個measured reporting frames都未超8.333ms，最大SaveReport2.365ms；idle overshoot max0.197ms，slow frames沒有>1ms的FPS等待超時。不能把報表或Sleep0當37.267ms主因。最大慢幀ready46.326/RHI31.101，已超出原ready5–25s trace；新增可選TraceSeconds20–90，做完整warm區間捕捉再下結論。report_completed/total/max JSON只含之前已完成calls，可包含量測外final CSV export；判斷幀內成本要讀frame CSV report_ms，不能直接拿JSON max歸因。新idle欄只記FApp frame-rate wait，不是全部RT/RHI waits或OS context-switch證據。

## 2026-10-04：長 trace 的 RHI 提交等待與匯出範圍修正

- `RhiLongQuietProfileR1` 停用週期報告仍有慢幀，55 秒 trace 內 ready=25.123733 的整幀 34.295604 ms。trace anchor offset=5.576206501 s。
- 原事件匯出只選 `RHIThread`，漏掉 `RHISubmissionThread` / `RHIInterruptThread`，因此不能據此判定 Submission_Wait 的根源。工具已改為 `RHI*`，重新匯出保留在 `Profile-RhiHitchSubmissionR1-CPUEvents.csv`。
- trace=30.6670106–30.6931124 的 Submission_Wait=26.1018 ms；提交執行緒同時 `FD3D12Queue::ExecuteCommandLists`=29.1201 ms。事件包含 residency 管理或直接 DX12 提交，以及可能的排程暫停，不能直接宣稱全是 GPU 執行或驅動 active CPU。
- 不以 Present 搬到另一執行緒當成勝利；先單獨測 `r.RHICmd.ParallelTranslate.BatchSubmissions=1`，比較整幀與尾端分布。未驗證前保留預設 0。
- 本輪一次搜尋猜測 D3D12Residency.cpp 路徑不存在；改用 rg --files 查檔，勿沿用猜測路徑。

### SubmissionBatchNoTraceR1 未通過：保留原設定

module0866、相同 100 人第三人稱、8workers/RHI8、160cap/60Hz：控制組 mean/P95/P99=6.319/6.724/8.193 ms，RT/RHI=2.768/1.370 ms；batch=1 mean/P95/P99=6.639/8.577/16.886 ms，RT/RHI=3.747/1.602 ms，超8.333ms幀比例0.986%→5.363%。Require120 正確拒絕，資料完整保留。最大幀40.885→33.272不能掩蓋P95/P99退步；未採用batch=1。下一步反向重測，排除單輪環境變動，不把提交合併當必然較快。

### 反向重測與下一個獨立試驗

- SubmissionBatchNoTraceR2 mean/P95/P99=6.279/6.698/7.062 ms、RT/RHI=2.526/1.465；反向控制 SubmissionControlNoTraceR2=6.338/6.839/8.980 ms、RT/RHI=2.828/1.378。兩輪變動大，batch在兩輪均提高RHI平均，保留預設0。
- 本地 D3D12Viewport.cpp:989 SyncToSubmissionThread 為原生 Present 強制 FlushCommands(WaitForSubmission)。另有引擎現成 r.D3D12.PresentOnSubmissionThread，將Present依序放入提交管線。下一輪單獨驗證；不可只用RHI等待移走宣稱整體效率提高，須檢查wall/P95/P99、GPU、全部幀與動畫總求值量。
- 彙整第一次 Python 以 utf-8-sig 讀UE UTF-16 JSON產生UnicodeDecodeError；已改為依BOM辨識utf-16/utf-8-sig，資料未修改。

### PresentSubmissionNoTraceR1：RHI降低不代表整幀改善

module0867同設定控制 mean/P95/P99=6.343/6.846/8.871、RT/RHI=2.723/1.375 ms；present=1為6.443/7.020/13.470、RT/RHI=3.018/1.064 ms。GPU1.353→1.350 ms，aggregate主poses5989→5975/s，沒有降低動畫頻率。雖RHI平均約少23%，整幀與尾端退步，不能當整體優化。保留預設0，反向重測後決策。

### Present 搬移後仍有同步慢幀：不採為預設

- 第二輪present=1 mean/P95/P99=6.256/6.672/6.883 ms、RT/RHI=2.468/1.040；反向控制=6.270/6.699/6.981、RT/RHI=2.543/1.332。RHI下降一致，wall收益小且第一輪反向；不以單輪最佳採預設。
- PresentSubmissionLongProfileR1 mean/P95/P99/max=6.473/7.929/12.488/21.024 ms，全部幀保留，非NoTrace驗收基準。anchor offset=5.327765298s；ready14.579836附近事件保留Profile-PresentSubmissionHitchR1。
- 已移除原RHI→submission的Submission_Wait路徑，但trace19.8307948–19.8416119仍有EndDrawingViewport內SyncPoint_Wait=10.8171ms；同時submission ExecuteCommandLists=10.6051ms，另外19.8837103–19.900178有16.4677ms提交事件。等待會轉到後續frame completion，不能宣稱搬移根治。
- IDXGIOutput::WaitForVBlank_NoBusyWait位於RHIFrameFlipStatTrackingRunnable，是統計執行緒，不能當RHIThread的VSync等待；遊戲r.VSync=0，勿據名稱誤判。
- Native DX12提交與present是CPU/API工作，不能直接搬成GPU shader。現階段證據不支持再增加worker就能消除此停頓，也不支持GPU pose bank可修它。BatchSubmissions與PresentOnSubmission保留可選，正式預設皆0。

### 120 秒完整報告出現37.6ms：修正「報告不是慢幀來源」的範圍

早期60s ReportIdleLegacyProfileR1之5次warm報告max2.365ms、無超budget，只能證明那次短測。最新SubmissionFinalDefault120sR1有11次warm報告、5次相關幀超budget，max報告37.582297ms；因此不可外推所有report都安全。本輪將optimized正式launcher預設SuppressPeriodicReports=true，避免遊玩中每10s全量枚舉/排序/JSON寫入/RHI memory查詢；載入與結束報告及全數frame CSV保留。base launcher維持false供控制；optimized可用-SuppressPeriodicReports:$false恢復。不是刪除或排除慢幀，並且不表示429個slowframes全由報告造成（報告對應5幀）。下一輪完整120s驗證新預設。

### QuietReportsDefault120sR1 驗收未過，必須保留

新預設第一輪mean/P95/P99/max=7.387/12.720/19.770/102.878 ms、超budget16.625%；報告列全部0，代表移除報告不會解決其他停頓，不能宣称正式Development已穩定120。affinity無晚改/repins0、priority正常。背景存活process delta：SuperExplorer96.609s/explorer74.047s/Taskmgr44.563s，較先前控制SuperExplorer36.672s增加；這是環境差異證據，不是context-switch歸因或因果證明，也不修改/停止其他程式。保留完整資料並同設定重測，不排除fail樣本。

本輪一次讀取background.json路徑錯誤；正確檔名是已列出的background-cpu.json。下次直接按inventory名稱讀取。

### QuietReportsDefault120sR2 仍失敗：重測不能挑成功才保留

mean/P95=9.143/18.626ms、RT7.470/RHI2.159/GT6.759/GPU1.378ms，prepared load6.658s。所有warm report_ms=0。背景存活git137.406s/explorer116.359s/SuperExplorer111.063s/TGitCache77.078s，不當確定原因但CPU環境較前輪重。完整失敗保留，正式120目標仍未完成。

下一輪只調遊戲自身：測Affinity Default讓Windows使用全部CPU核心，再用8/16worker與RHI寬度獨立比較，避免將之前quiet環境的LargestL3快值外推成所有負載最佳。optimized wrapper新增可選Workers/RHIWidth（預設仍8/8）；base現有actual count驗證保留。絕不停止外部程式或更動其affinity。

### 全CPU核心/16worker壓力環境試验均未過

PressureDefault8NoTraceR1 load12.383s、mean/P95=11.298/20.755ms；PressureDefault16NoTraceR1 load7.824s、mean/P95=9.106/15.777ms，actual worker驗證通過，RHI width仍8。後者比前者快但不同時間的環境不可當固定百分比收益，且都沒到120。預設worker仍8/affinityLargestL3，沒有更動外部process。下一輪在目前負載重新trace8/L3，分析RT等待與CPU事件，不再盲目套新設定。

## 使用者更正：本輪效能試驗全部不作驗收（其他程式並行）

2026-10-04 使用者明確表示「你這輪的測試結果先不算，因為有其它程式同時在跑」。本輪 ReportIdleLegacyProfileR1、RhiLongQuietProfileR1、SubmissionControl/BatchNoTraceR1/R2、PresentControl/SubmissionNoTraceR1/R2、PresentSubmissionLongProfileR1、SubmissionFinalDefault120sR1、QuietReportsDefault120sR1/R2、PressureDefault8/16NoTraceR1、PressureLargestL3ProfileR1 的效能資料均保留為受干擾診斷，不可用來驗收120 FPS、比較設定勝負或宣稱改善百分比。之前本輪的推論/採用描述由本節更正；沒有刪除失敗或挑成功樣本。

回復 optimized launcher 的 SuppressPeriodicReports 預設false，僅保留可選。BatchSubmissions/PresentOnSubmission預設false、Workers/RHIWidth預設8/8、Affinity預設LargestL3不變；效能設定未依本轮資料採用。新增trace/成本量測可供將來無競爭環境重測。已核對最後本輪owned benchmark已自行完成，沒有殘留本輪遊戲程序；未更動其他程式。

低模面數是離線資產屬性，不依本輪FPS：StrictLow100Preparation20261004.json 的100張 GameLow30kR1 全部 triangle_limit=29999、all_triangle_budgets_met=true、最大29465、貼圖最大256。當前launch明確使用 GameLow30kR1。不能把 frustum/section 隱藏後的可見三角數當成整個角色資產面數。

## 互動Play視窗修正

使用者要求查看場景時，Play launcher仍用WindowStyle Hidden，角色載入完成但MainWindowHandle為0。已改Play使用Normal、Benchmark保留Hidden，並只對核對路徑的本次遊戲PID45584之UnrealWindow顯示視窗；未重啟或操作其它程式。後續互動啟動不得沿用背景測試的Hidden設定。

## 2026-10-04：第三人稱WASD控制

原控制器在CrowdThirdPerson直接返回，Scene又每幀用巡邏軌道覆寫角色位置，所以WASD無法控制跟隨人物。互動Play新增CrowdPlayerControl，排除跟隨角色巡邏更新，以水平鏡頭方向WASD、180cm/s、斜走normalize、室內邊界±2425cm操作；其餘NPC保持巡邏。鏡頭絕對yaw與角色朝向分離，避免控制角色轉身導致鏡頭回授旋轉。高低模actor換置後重新套用動畫暫停/播放狀態。

一般Benchmark不開玩家控制，保持原測試路徑。MovementTest是獨立功能模式，不能帶Require120。第一個host build快照後補上斜走速度驗證，已安排最後source快照重新建置，不把早期binary當最終版。

WASD功能驗證與建置成功，未發生編譯/操作錯誤。以實際整合功能資料驗證位置不再被巡邏覆寫、斜走不加速、停止不漂移、牆邊限制與高模換置。報表FPS target欄只沿用既有自動統計，不作本次效能通過宣稱。

## 2026-10-04 近景臉部與輪廓診斷

- optimized launcher 的 OutlineRadius=0 會關閉所有低模 WPO 輪廓 section；依使用者要求改為玩家附近 1600cm 保留，仍使用原低模與 256 貼圖。不能把 section 開啟當成眉眼像高模的視覺驗收。
- 舊 CrowdPilotFrontCamera 使用固定世界方向，角色走動旋轉後不一定是正面。改成跟著角色 local +Y，新增 FaceDistance 供近距離測試；local -Y 的對照確認為背面。
- FaceLocalPlus180R1 仍見白色眼區/異常臉部遮蓋，不能宣稱近景已修好。下一步驗證 affine palette 的 component bounds 是否錯誤剔除獨立 HeadMesh。
- 一次多檔 patch 因 EndPlay 長行不匹配而失敗，重新分開套用。一次 PowerShell 字串取代展開了 $FaceDistance，造成參數缺變數；已用 literal apply_patch 修正並 ParseFile 通過。
- FaceOriginalR1 嘗試 FullSkinReferences，但載入 failed=1、loaded=0，不是可用的高模畫面對照；其空場景 screenshot 不得作 culling 證據。已確認 PID/執行檔後停止測試程序。
- 這些是功能/畫面測試，所有 FPS 數值均不作效能驗收，仍遵守使用者對其他程式干擾的排除要求。

### 2026-10-04 近景臉部與穿衣修正的驗證錯誤
- `auto* Card = TObjectPtr` 不能推導為指標，改用 `.Get()` 後重新編譯。
- 高模診斷若在 Ready 前替換頭部，會違反初始低模資源審核。診斷移至 Ready 後；正式 FaceLOD 也是 Ready 後才非同步請求。
- 原始 UE Editor 的 UECPCore 模組與当前隔離編譯不相容，資產命令列一律用 `D:/UE5.8/CrowdRuntimeStable`。
- PowerShell 傳入 `KeepMaterials` 必須把逗號字串加引號；DestinationRoot 本身不含 Variant，工具還會追加 Variant。誤生成的 nested C000 pilot 不能作為正式百人資產。
- 0872 身體遮罩工具誤用可寫權重類型接收 const 權重，改成 generic CoverageBlend，0873 成功編譯。
- 首次 BuildBodyCoverage 返回 1 卻無原因。加入缺失 body/clothes/masks 的明確診斷，避免把沒有產物的 exit 當成功。
- FaceDetailR1 的高模眼睛仍白，完整高模同樣白；原因不能歸咎低模面數。原預覽用獨立眼白/虹膜/眼線 renderer priority 101..107，packed mesh 遺失順序。R2 保留各 renderer 並恢復 priority，須視覺驗證再採用。
- 這一輪其他程式仍在執行，只驗證功能、外觀與載入資源邊界，不作 120 FPS 效能驗收。
- FaceLayers40R10 誤漏 `-StageRoot`，工具使用 Saved 中舊 stage；報告缺少新字段且 run_finished=false，外觀與效能皆作廢。後续測試必須指定 `D:/OpenKoikatsuCrowd/DevelopmentStage` 并檢查 launch.json 的 binary 路徑。
- BuildBodyCoverage 的 body renderer 實際是 `o_body_a`，不能硬寫 `cf_O_body`；改用匯入角色的 `Role=body`，避免不同資源版本命名差異。
- BodySafe180R12 的初始低模審核將合法共用的 R1 低模頭髮/飾品判成高模，沒有通過。BodySafe 明確允許 R1 資源路徑，仍保留每個角色總面數 <30k、單 LOD、材質 <=256 的實際審核；沒有放寬到原始高模資料。
- BodySafeR1 僅刪皮膚未能消除胸前灰色穿插。HideBody slots 0/1 後灰塊仍在；HideBra slots 33/34 後消失，證明此卡的實際穿出層是內衣。R2 對皮膚與內衣分層計算外衣遮蔽，不得靠一律隱藏所有內衣或縮小整個胸部處理。
- C000 的頭髮模組由 `accessory` 角色載入，僅篩選 `Role contains hair` 產生 hair=0/0。NearDetailR1 無效；R2 增加離線頭部區域幾何判斷，涵蓋此類自訂假髮與頭部大型飾件。高模身體仍不得硬引用。
- 0882 編譯失敗：MeshDescription vertex colour 是 FVector4f，alpha 分量是 `.W`，不是 FColor 的 `.A`。修正後重新編譯，不使用未成功連結的 DLL。
- NearDetailR3 的頭髮預算為 min(3×低模, max(低模, 85%原始))；不透過無意義細分湊三倍，也不直接載入完整高模。重用 R1 小貼圖，第一卡實際 5056→7482 triangles。
- 最新頭髮需求已覆蓋三倍／85%規則：整個髮型原始總 triangles <=10000 保留原始高模幾何，>10000 才降至總 triangles <=10000。NearDetailR5 先打包相容座標的頭髮幾何，再控制總預算；材質 section 並未合併。不可把「每個頭髮部件 <=10000」冒充整個髮型 <=10000。
- NearDetailR3 百人準備在 C004 返回1，保留日誌與部分實驗資產，不能當全部完成。新版 R5 改用整個髮型統一預算與較足夠的小表面保留預算，後續須重新驗证百人完成數。
- 內層遮蔽忽略 WPO 描邊材質，不用背面輪廓當成不透明外衣；Masked/Translucent 同時檢查 opacity 與 vertex alpha，超出 UV 範圍保守保留。保留頸部最上方8cm皮膚，避免低模領口縫隙露出空洞。BodySafeR3 是目前候選，R1/R2 為比較資產。
- BodySafeR3 已大幅消除胸罩穿插，但近景仍有小灰斑，不能算修好。正向遮蔽射線無法處理「內層原始幾何已稍微在外衣之外」的點。R4 補上反方向最多3cm的局部外衣檢查，仍要求7個姿勢、外衣opaque coverage與UV/alpha一致，避免誤用身體背面的衣服當遮蔽。

### 2026-10-04 R24: Original clothing is not the low garment surface
R5 still showed a small bra patch in the front screenshot. Relaxing corner coverage did not fix it. The coverage test used original outer clothing, while the displayed low garment can sit further inward after reduction. R6 tests opacity and intersections against the actual low outer garment at seven walk poses. Original inner geometry remains the correspondence/normal source. Do not treat original garment coverage as evidence that a low garment hides the same surface.

### 2026-10-04 R26 preparation restart
R6 low-garment coverage no longer consumes the original clothing material masks. Its first batch still baked both original and low materials, wasting preparation time. Removed the unused original bakes without changing the low-surface coverage logic. Stopped only the owned ActualLow100R26 commandlet (PID receipt and command line checked). Completed Character packages are retained; missing manifests must be rebuilt before launch. Preparation will resume in four disjoint ranges; this is offline preprocessing, not a game FPS test.
R27 isolation invocation: PowerShell unquoted -HideSlots 0,1 becomes an array and fails validation. Always pass the CSV as a quoted string; no game launched for the rejected invocation.
R27 C001 white garment blocks persisted after hiding body slots 0/1 and bra slots 61/62 independently. They cannot be claimed as skin/bra penetration. Keep the isolation screenshots rather than changing skin coverage to remove unrelated garment colours. C000 R6 front and walk samples remove the known bra patch. Near hair shadows must remain enabled by the normal distance shadow policy after the old packed hair sections are hidden; tagged NearHairShadow in both cached and uncached policy paths.
R28 audit API correction: UE5.8 relocated MaterialInterface.h to Engine/Public/Materials. The deprecated five-argument GetUsedTextures is now a final empty compatibility stub, so a successful call cannot validate texture sizes. Replaced it with the current optional-argument overload and will repeat the audit. Do not accept a zero-texture report from R28 as low texture evidence.
R29 current GetUsedTextures still returned no textures under NullRHI without compiled shader maps. Use editor GetReferencedTexturesAndOverrides (expressions, nested functions and overrides) for the offline dependency audit; now explicitly reject TextureMax=0. R28/R29 texture fields are invalid and not acceptance evidence.
R30 audit still rejected zero size: uncooked textures under NullRHI may have no platform data, so GetSizeX/Y are not the source dimensions. The offline audit now measures Texture.Source dimensions, matching the actual 256² pixel shrink in the importer, and logs invalid card/triangle/texture totals. Runtime initial-load audit still measures the cooked platform dimensions independently.
R32 launcher integration: default FaceLOD returns before legacy whole-body high/low switching. The existing -DetailSwitchTest diagnostic must explicitly add -CrowdDisableFaceLOD, otherwise it cannot exercise its intended path. Added that command-line flag for the diagnostic, and an explicit -DisableFaceLOD option for controlled comparisons; normal Play retains FaceLOD. Default low variant is now BodySafeR6; its Cook defaults to including near face/hair packages while preserving the initial low-only runtime boundary.
Final R32 acceptance: AuditNear100R32 verifies 100 unique low/near pairs, source textures maximum256, low maximum29063, hair maximum9924 and all private skeleton hierarchies. Full cooked third-person/WASD run exits0 and loads/animates100 with initial low-only, body low, load7.090475s and movement checks passed. Known C000 bra patch absent in seven visual walk samples. FPS remains unaccepted; raw target_met=false retained. Saved final189.732GB, junction skipped.

## 2026-10-04 — Pale crowd colors / output transfer

- Do not assume pale imported characters are caused by overly strong room lights: Forge/toon materials are unlit and feed their already-composed color to Emissive. Check camera output transfer against ApplyStudioExposure first. Crowd omitted the studio's ToneCurveAmount=0 / ExpandGamut=0; same-view cooked-game A/B confirmed stronger card colors after matching these settings. Do not compensate by rewriting card colors or indiscriminately increasing saturation.
- Windows rg does not expand literal wildcard path arguments (e.g. Tools/generate_forge*). Use rg directory searches with -g filters or rg --files, then concrete paths.
- Color-only visual runs should use run_crowd_development without Require120. run_crowd_optimized Benchmark automatically enforces 120 FPS and throws when frame target fails, even if functional game run_finished=true and exit=0. ColorToon100R1 is functional/visual evidence only, not a passing FPS test.

- ColorMovement100R2 regression: run_crowd_development without Require120 verifies all 100 cards, initial low-only, WASD/idle/boundary and face near/far restoration after the color fix. No new functional errors found. Keep performance target separate from this completed appearance fix.

## 2026-10-04 — Anti-aliasing / DPI validation errors

- FXAA alone left thin toon hair/eyes/outlines visibly jagged. Crowd now uses TAA at actual viewport100%; preserve low texture sizes independently of render resolution.
- Never claim a requested2560x1440 test is1440p without viewport telemetry. NativeTaa1440Movement100R1 and NativeDpiTaa1440R2/Saved1440R3 were1828x1142 because UE's unattended flag disables OS high DPI by default. Cvar EnableHighDPIAwareness=1 alone does not prove process awareness. Set bAllowHighDpiWhenUnattended=true before window creation; NativeDpiTaaActual1440R4 verifies true2560x1440 and normal exit.
- PowerShell Add-Type does not bind pipeline text to TypeDefinition. Use Add-Type -TypeDefinition $taskType explicitly for the read-only DPI query. The first failed query did not modify game/system state.
- ToonRenderProfileR3 trace recorded before this image-quality change is not the current AA baseline and overlapped another live game; do not use its25ms result for optimization acceptance.
- Relaunched interactive NativeTaaPlayR4 (PID109244): player_control=true, loaded100/failed0, AA2, primary/secondary100; process responds normally. Idle controlled character is excluded from movement-based animated count until walking, so live idle animated99 is not a missing resident.
- PowerShell's -ErrorAction applies to cmdlets, not rg; passing it to rg was parsed as an encoding flag and rejected. Removed it and repeated the targeted log query.

## 2026-10-04 — Stocking/skin intersections after low-mesh reduction

- User screenshots show large alternating skin/stocking triangles on legs and similar tight-garment artifacts on torso. Geometry-layer intersection after simplification is the leading hypothesis; screenshots alone do not establish pure depth-buffer z-fighting or rule out separate transparency-order problems. Reproduce with source/low geometry and walking poses before declaring the root cause verified.
- Code inspection: BodySafeR6 deletes/insets underlying skin only when CoverageOpaque passes. Translucent masks with alpha below253/255 deliberately preserve the underlying skin (including stockings/lace), so existing opaque clothing coverage does not solve these close translucent layers. Do not weaken that threshold to delete skin behind transparent stockings.
- Preferred low-gameplay treatment for skin-tight, appearance-only stockings: transfer source garment color/opacity coverage into body UV/material space and composite into the low skin material; draw the physical surface once. Keep lace edges, actual displaced/loose fabric, shoes and geometry accessories separate. Bake transferable color/opacity inputs, not one view's fully lit emissive shader output; preserve source color space and blend behavior.
- Fallback for geometry that must remain separate: derive its fitted surface and skinning correspondence from the reduced body, constrain separation over representative poses, preserve leg joints/stocking borders. A constant normal offset alone cannot guarantee separation under mismatched topology/weights; arbitrary global outward inflation changes silhouette.
- Do not treat TAA, translucent priority, DisableDepthTest or a near-plane tweak as a verified geometry fix. Opaque-only covered-skin removal remains valid, while sheer stockings need the skin contribution retained/composited.
- No stocking assets or material alpha were changed in this diagnostic turn. Next validation should use an affected card, low/high comparison, stock/skin isolation, several walking phases and a camera orbit; retain the whole-character under30k policy and source card assets.

## 2026-10-04 — Cross-leg skin bindings (KK_372839 / C014)

- Postponed stocking transparency/intersection treatment at the user's request. Investigated a distinct skin triangle pulled between legs after reduction.
- TransferAuthoredBindings originally recovered source skin weights from the nearest same-material vertex in a walking reference pose. Closely spaced limbs can choose the opposite leg. Added an offline UV-constrained correspondence search, retaining full authored affine weight groups; geometric distance remains bounded to10cm and unmatched vertices retain existing weights.
- C014 pilot:13 walking samples, stretched-triangle heuristic2 ->0, max edge ratio5.254614 ->2.146890, reference-position error0.0000475cm, packed mesh24841 triangles unchanged. Side camera at yaw90/height85/distance400, sample3/6 has legs open and visibly reproduced the original skin bridge; after repair it is gone. All seven side samples captured, normal game exit/low-only/near details passed on LegSideAfterC014R3.
- New derived cache GameLowBindingSafeR7 copies BodySafeR6 low materials/UV/topology and retained body-coverage fixes. Source high/APNG assets are untouched. General importer binding transfer now also uses the UV correspondence rule. Initial default is not switched until the full100 preparation/audit/cook passes.
- Build/verification errors: const skeletal weights require templated VertexBlend (FVertexBoneWeightsConst does not convert to mutable FVertexBoneWeights); initial front camera missed legs, so added PilotViewYaw/PilotTargetHeight and used the requested open-leg side view.
- R7 initially failed schema/shared-resource validation because the runtime only recognized BodySafeR6's reused GameLow30kR1 resources as low. Extended the existing low checks for R7 without relaxing texture256/triangle30000 limits; failed LegSideAfterC014R2 is not acceptance evidence.
- Concurrent unrelated root builds added PaintSprayer and removed module manifests before failing; their UHT/class compile errors and UBT mutex prevented authoring/runtime work temporarily. No unrelated source was reverted. Restored missing manifests only to verified old0760 plugin binaries and the verified C++-only0900 authoring module. Explicit generated-module-object reuse is limited to C++ changes with no reflected layout changes. Development host now waits for UBT mutex instead of failing immediately. Commandlets disable BpGeneratorUltimate for this task, avoiding its missing UECPCore metadata without changing project plugin enablement.
- Failed patch against build-helper parameter line made no changes; reapplied using exact current text. Preserve failed logs and fresh build suffixes. These functional tests overlap preparation/background activity and are not FPS evidence.
- FinalR7 patch compiled but root linking failed because a concurrent build replaced OpenKoikatsuPlayerController.cpp.obj with PaintSprayer dependencies absent from the reused generated module. Kept verified0900 for authoring, which already contains the functional correspondence repair. Do not publish a failed DLL or overwrite unrelated build outputs. Runtime host is isolated from those controller/paint classes.
- Full100 repair/audit normal exit0: 68 cards improve the13-phase stretched-edge heuristic, sum1651 ->244, no per-card count regression. Eleven cards retain suspects, so this is not proof all remaining garment/affine-deformation artifacts are gone. AuditBindingNear100R7 independently verifies100 unique source hashes, whole initial low maximum29063 triangles, source textures maximum256 and near hair maximum9924; original topology/triangle budget retained.
- Final full100 Cook, regenerated501-phase bounds and isolated Development stage succeeded. LegBindingMovement100R8 exits0, loaded/animated100 failed0, initial low-only, WASD/idle/boundary and near face round-trip allpass. Load7.473417s. target_met=false; do not relabel functional verification as120FPS acceptance. Interactive LegBindingPlay100R9 reopened with R7.

## 2026-10-04 — Residual binding investigation

- ResidualDiagnosticsBuildR1 compiled the crowd diagnostic but linking hit concurrent PaintSprayer::RemainingVolume missing definition. Do not change unrelated paint code or invent a stub. The definition subsequently appeared in its source, permitting a fresh full build retry. Preserve the failed log.
- C030 open-leg side phase3 reproduces stretched cloth across the leg gap. A stretched-edge count alone must be classified by material and source pose before deciding whether the remaining defect is transfer correspondence or rigid garment animation.
- ResidualRestoreBuildR4: UE5.8 JSON object keys use shared string storage and cannot be passed directly to FJsonValueString. Convert the verified slot name to FString (Slot.ToString()); no assets were written by the failed compile.
- First bounded C007 Blender candidate ResidualBlenderC007R6 worsened suspect count6 ->46 at29897 triangles, accepted=false/applied=false. Published R8 Character was unchanged; backup kept. Added opt-in topology-island binding hints to prevent position/UV transfer from sampling the opposite disconnected stocking island.
- C007 second candidate uses per-source connected-island binding indices carried through Blender decimation. ResidualBlenderC007R7:6 ->0 suspects,29897 whole mesh-description triangles, accepted=true/applied=true. Both stocking sections retained. Saved new BodyTopologySafeR1, backed up and reserialized only the derived R8 C007 Character to reference it. R7 and source APNG/high assets untouched. R6 rejected candidate remains documented; no destructive triangle stripping.
- C094 first topology-hint candidate failed10cm binding guard (24.154213cm), no JSON acceptance or asset write. Its many nearby shoe islands could be welded together by the shared0.01cm cleanup, interpolating island ids. Restrict optional binding-hint welds to each authored island; never loosen the distance guard. Original high rigid-clip geometry has0 suspects, while original high animation has28, so validation here concerns the unchanged gameplay rigid clip only.
- C094 island-preserving weld retry ResidualBlenderC094R10 passes6 ->0 suspects, whole29900 triangles, accepted/applied=true. C007 regression with the same weld policy is0 ->0/accepted=true/applied=false. Do not confuse original high animation shoe suspects with the separately verified rigid gameplay-clip result.
- Final R10 full100 low/near audit passes, maximum29900 triangles and256 source textures. Functional Development SurfaceTopologyMovement100R10 passes100 loaded/animated, failed0, initial low-only, WASD and near face round-trip; load6.822303s. target_met=false; no120FPS acceptance. C007/C094 side open-leg game runs pass. Remaining218 suspects are in five long-garment cards with corresponding original high geometry failures under the same gameplay clip, documented separately.

## 2026-10-04 — Hair shell investigation

- HairAuditBuildR1 failed because `auto*` cannot deduce a pointer from `TObjectPtr<UMaterialInterface>`; use `.Get()`. Fixed and Editor build R2 succeeded; failed build wrote no assets.
- Do not classify WPO through compiled material flags under NullRHI: all returned false in HairOutlineAuditC079R1. Editor authoring must inspect the parent's MP_WorldPositionOffset expression. Runtime continues using compiled flags.
- Engine materials headers in this local UE5.8 tree are under Public/Materials, not the older Classes/Materials layout. Discover paths with rg --files before reading. Earlier guessed filenames and literal wildcard paths were invalid; do not repeat them.
- Face orientation must be evaluated in a sampled animation pose, not affine palette bind coordinates. Hair repair samples the existing compressed walking clip at time0, without changing animation, vertices, skin weights, UVs or triangle count.
- C079 preliminary derivative NearDetailR6/GameLowHairSafeR9 corrected223 near and340 low hair shell triangle windings; visual acceptance is still pending. R5/R8 and source cards are preserved.
- HairRepairBuildR2 guard failed to compile because a comma-declared `auto` mixed const and mutable skin-weight view types. Declare them separately. HairRepairBuildR3 succeeded. Do not skip preservation guards to get a build through.
- C079 R1 cooked Development front visual confirms the large fringe black polygons disappeared after shell winding repair. Side R2 retains silhouette outlines; this is not a blanket outline-disable workaround.
- Hair shell full100 preparation succeeded. C079 pilot is separately recorded; HairRepair100R3 contains99 new rows and reuses already-prepared C079. The final full100 AuditHairNear100R4 confirms100 unique hashes, low maximum29900, textures256, near hair maximum9924 and compatible private hierarchy.
- NearDetailR6 is the main visual fix requested by the user (middle hair <=10000), not a high-hair fallback. C079 is still8974 triangles; front/side/seven-phase images and C085 before/after confirm the original large black patches disappear. Ordinary thin strand-overlap contours remain.
- Editor build HairRepairFinalBuildR4, host HairShellR2, Cook Hair100R4 A/B and stage Hair100R4 succeeded. BoundsHair100R4 covers100 cards and501 animation samples per card. ResumeC079 wrapper returns already_prepared=true without rewriting existing caches.
- HairMovement100R6 normalexit0, loaded/animated100 failed0, load8.183776s, initial_low_only_met/movement_test_met/face_round_trip_met/body_stays_low=true. Mean8.777109ms/P9511.290800ms/target_met=false: functional evidence, not uncontended120FPS acceptance.
- User clarified the main target is <=10000 near middle hair and requires a general solution. Moved the shared material/geometry rule into OKCrowdHairOutline.cpp/.h and wired it directly into OKBuildCrowdNearHair and BuildNearHair, including the <=limit branch that previously bypassed reduction. No card-index/name conditional is used by the rule; per-card counts are diagnostic results only. The separate R9/R6 tool migrates older saved caches.
- HairGenericBuildR5 succeeded. Generic generator tests SmallR5 (5012 original, no reduction), ReducedR5 (35636 original ->8974) and MiddleC085R5 (8974 input) all exit0, automatically fix shells and have second_pass_changes=0. Complete vertices/weights/morph preservation guards still run. prepare_crowd_near_detail engine default updated from incompatible old CrowdRuntimeStable to current D:/UE5.8.
- Interactive HairShellPlay100R7 successfully loaded100/failed0, initial_low_only_met=true and player_control=true; its log later shows normal window shutdown, not a crash. Do not report a closed process as still running.
- HairGenericFinalBuildR6 succeeds after adding missing mesh-description and <=10000 argument guards. Diff whitespace check passes. Runtime host/staged shaders and current R9/R6 assets need no rebuild for this editor-only factoring; they use the same previously verified repair output.

## 2026-10-04 — Universal near-hair pipeline completion

- Continued review found preparation still targeted NearDetailR5 and skipped existing Face packages without examining the shell recipe. Default generation/preparation now targets R6 and validates cached source hashes, per-person <=10000 count and read-only shell orientation before reuse.
- Added SourceRoot to face/near-hair authoring so inputs need not be C000..C099. The generic preparation tool supports arbitrary imported /Game roots for face and middle hair; body generation remains a separate existing workflow. This adds no gameplay validation or per-frame work.
- HairUniversalPipelineBuildR7 succeeded. R8 then hit unrelated concurrent OKPaintSprayer.cpp FMath::RoundDownToPowerOfTwo compilation errors. Crowd source compiled; preserve the failed log, do not change the other task's paint implementation or claim R8 build passed. Continue independent authoring tests using the last successfully linked R7 while waiting for that source to be corrected.
- Expected regression rejection: HairUniversalRejectLegacyR8 refuses C079 legacy R5 with223 outline issues; this is an intentional cache-validity failure, not a new runtime error. It does not overwrite the old asset.
- HairUniversalPipelineBuildR9 was attempted while our own 100-card read-only commandlet still held the project DLL. Linker correctly failed LNK1104 / UBA9001. Avoid this scheduling error: serialize Editor linking before authoring commandlets and wait for the owned commandlet to finish before retrying. No code or assets were reverted; do not terminate unrelated Unreal processes.
- SourceRoot full-generation test UniversalSourceRootC085R8 completed: input35770 hair triangles, output8974, generic shell correction1834. It used StartIndex0 with SourceRoot C085, confirming lookup comes from the asset root rather than the scene index. No card-specific tuning was supplied.
- A PowerShell source replacement failed at parse time because backslash does not escape a single quote in PowerShell. No command body or file write executed. Use literal apply_patch for C++ character literals instead of string-built shell replacements.
- Final read-only audit HairUniversalCache100R8 exits0:100 unique cards, outline_issues0, maximum9924 hair triangles. Final build R10 succeeds after the commandlet releases the DLL; latest DLL timestamp follows the updated sources. Generated custom-recipe AuditNearGame C085 R10 verifies8974 hair triangles and private hierarchy compatibility. Final SourceRoot cache reuse R10 exits0; preparation script syntax and diff whitespace checks pass.
- Do not claim animation-wide proof from a reference-pose winding audit. The full100 check validates the generic orientation rule at its authored reference pose; prior seven-phase/front/side visual samples are separate evidence. This turn changes authoring/caching only and adds no game-frame work or FPS acceptance claim.

## 2026-10-04 Room authoring build
- The first Editor build (AtriumEditorBuildR1) failed while UBT tried deleting a locked hot-reload DLL. No room assets changed. Use -NoHotReload for this authoring build; R2 succeeded without interrupting other applications. Keep builds and map-authoring commandlets sequential to avoid DLL locks.


## 2026-10-04 Atrium HISM material validation
- AtriumMovement100R1 rendered default gray/black materials although map creation and Cook succeeded. The authoring commandlet did not auto-enable MATUSAGE_InstancedStaticMeshes. Explicitly set that usage before PostEditChange/save; recook and visually inspect the packaged game. Cook success alone is insufficient. R1 FPS is invalid for evaluating the final room.


## 2026-10-04 Inactive map actor iteration
- AtriumMovement100R2 revealed the old floor intersecting the new floor and extra lighting. TActorIterator defaults to OnlyActiveLevels and omitted actors in this inactive commandlet-loaded map. Iterate PersistentLevel->Actors explicitly, verify DestroyActor return values and final environment group/light counts. The authoring generator now replaces previous generations instead of accumulating coplanar geometry.

- AtriumEditorBuildR4 failed because Level.Actors stores TObjectPtr; auto* deduction cannot convert it. Use explicit AActor* in range iteration. Validate actor counts before saving the map.

## 2026-10-04 Room regression test API and build gate
- New room test failed to compile because UE5.8 GetStaticMesh returns TObjectPtr; pass .Get() to TestNotNull. The first automation launch used the previous DLL and only performed the original two tests; that run does not validate the new room test. Require successful build before launching tests and confirm all three expected tests were registered/performed.

- AtriumEnvironmentTestsBuildR2 was rejected by the engine-wide Live Coding mutex from a different project (om.uproject). Do not close unrelated editors. For this project-only build use -NoHotReloadFromIDE together with -NoHotReload; no engine binary changes are requested.
- AtriumDefaultGeneratorFinalBuildR1 encountered a hot-reload DLL held during another process shutdown. The build was gated and no authoring/test command was launched with stale binaries. After the editor processes exited, R2 completed with the same project-only build flags.

## 2026-10-04 Near-detail current-mesh shadow rule
- Previous shadows used a separate 600 cm radius while face/hair detail loaded at 1600 cm, so nearby detailed characters could have no ground shadow. Both cached and uncached policies now include residents with active near detail, preserving the distant fallback. Visible HeadMesh and near hair cast their current pose; hidden replaced low components do not cast duplicate hidden shadows. Async completion refreshes shadow policy immediately. No card-specific IDs or full high-body load are required.
- Verification: near-shadow moving case 17 detailed / 17 shadow residents; cached case 36 / 36; both zero missing near-shadow components and face round-trip passed. These functional runs do not establish 120 FPS or an isolated performance improvement.


## 2026-10-04 Quality-preserving 120 FPS baseline
- Room120BaselineProfileR1 was rejected before launching because warm profiling requires at least 45 seconds. Use 45+ seconds; no result exists for R1. Keep profile and untraced acceptance measurements separate.


## 2026-10-04 Parallel bone packing integration
- ParallelBonePackingR1 compilation requires both Renderer/Private and Renderer/Internal for UE5.8 scene-extension headers. Add both include paths explicitly; no engine binaries or foreign scenes are modified. Ensure the optional BeginPlay hook is inserted despite mixed CRLF/LF, and verify the runtime installed flag rather than assuming a source replacement succeeded.


## 2026-10-04 Incorrect half-float format assumption
- The hardware F16C candidate never activated: this engine defines USE_COMPRESSED_BONE_TRANSFORM=0 and uploads full float 3x4 matrices. Room120HardwareVerifyR1 only validates parallel packing, not hardware conversion. Removed its dead code/flags; do not change the project macro independently of the engine/shaders. The float transpose already uses engine SIMD, so duplicating it is not a new optimization.
- Separate runs changed nearby character population and background contention. Do not derive an overall FPS improvement from their packing-task timings or accept moving-camera results as a stationary-scene 120 FPS pass.
- Quality120FinalEditorBuildR1 exposed a stale lambda capture and removed registration argument during cleanup. Corrected both explicitly; require a new successful build before staging.
- Quality120FinalEditorBuildR2 compiled and linked the project DLL, but UBT also requested an engine NetCore relink held by a concurrently running PaintSpray commandlet. Overall Editor build failed; do not terminate unrelated work or claim a complete Editor build. Validate the isolated Development host separately.

## 2026-10-04 Controlled bone packing A/B
- BalancedBonesABR1 failed because calling the exported engine baseline via SkinningSceneExtension.h also requires Engine/Internal/SkeletalMeshUpdater.h. Added the explicit Engine/Internal include path to authoring and isolated host. Do not bypass headers with an invented class declaration.
- Discover class definitions with rg before assuming a per-class filename: crowd render classes live in OKCrowdScene.cpp/h, and engine skinning extensions in Renderer/Private/Skinning.
- BalancedABEngineR1 followed the walking NPC because the launcher enabled player control only in Play mode. It is a moving-camera diagnostic, not a fixed-camera A/B. Controlled Benchmark sessions now explicitly enable the same player-control path and reset to the same starting layout. Retain R1 as rejected comparison evidence.
- FixedABEngineR1 failed the 100-animation gate because normal player idle sets its play rate to zero (99 walking, one idle). Keep the gate: controlled fixed-camera performance mode now plays the player walk in place, while ordinary WASD gameplay retains idle behavior. Do not weaken the animation check or call this rejected run a valid 100-walking comparison.
- Generalization audit found the optional packer iterated maximum allocation count when using a bone map. Engine iterates the active map length; those counts can differ for multi-LOD meshes. Allocate the maximum span but write only active map entries, retain full identity fallback. Existing single-LOD crowd tests did not establish multi-LOD correctness.
- The original equality check reused PackRange and established concurrent destination consistency, not an independent indexing oracle. Verification now uses separate engine-style map/source iteration. Rebuild and reverify before adoption.
- FixedWorkers16L3R1 and FixedWorkers8AllR1 comparison gates rejected camera/population drift. Resetting TravelTime before the next tick still let the next frame's DeltaSeconds influence player spawn. Controlled spawn now uses exact path time zero; ordinary gameplay is unchanged.
- Completed controlled reports were periodically overwritten while NPCs continued moving, changing reported frustum population after timing samples stopped. Freeze the successful final report until the next case. Settle async near-detail loading before the first A/B; final resident count alone does not prove equal warm-up workload.

## 2026-10-04 Cache-aware role placement
- CacheRolePlacementR1 compilation exposed DWORD vs uint32 template deduction in FMath::Max; cast the Windows cache size explicitly. Use CountBits64 for processor masks (the 64-bit CountBits overload is deprecated in UE 5.8).
- FixedWorkers8AllR2 passed the corrected fixed-camera/near-population gate: engine 19.22 / 19.58 ms means, bone-range version 21.59 ms. Do not enable bone parallelization from task-only timing. Equal endpoint frustum counts are not a reliable gate for animated bounds; keep them as diagnostics and verify camera/near-detail population instead.
- User explicitly rejected hardware-specific tuning. Removed the own-process cache/role affinity experiment and its host mirrors; preserved raw test evidence. The optimized launcher and general comparison tool now default to unrestricted OS affinity. Do not revive cache-mask or CPU-model tuning as a solution.
- Follow-up audit found stale generated-host scene sources still referenced removed placement code before rebuilding. Rebuild refreshes exact scene mirrors; verify generated sources and staged executable, not just primary sources. Recommended launch/comparison tools explicitly pass Default affinity and no longer offer LargestL3.
- Diagnostic read used the wrong staging-log prefix (DevelopmentStage-); actual stage script writes Stage-. Check the script variable before constructing evidence paths. NoHardwareTuningR1 build/stage completed successfully.

## 2026-10-05 Exact duplicate pose encoding experiment
- New mode 3 requires extending both PowerShell validators and the scene control-request validator; first source audit found the scene still rejected values above 2. Fixed before running any acceptance measurement. Rebuild host after updating the copied source.
- Reuse is limited to byte-identical source matrices, equal output counts and the identical bone map inside one provider invocation. No persistent revision-only cache: revisions or mesh pointers alone do not prove compatibility after LOD/asset changes. All current/previous GPU outputs remain written.
- ExactPoseReuseABR1 overlapped our own SubmissionBatchR1 compilation during case 2. Keep raw evidence but reject it as an uncontended adoption comparison. Do not compile or stage another executable during timed cases. Candidate source equality reported zero reused bones in sampled warm reports; last-pass counters do not prove a whole-run total.
- SubmissionBatchABR1 passed camera/population gates but repeated 64-draw baseline drifted from 20.308 to 26.830 ms (~32.1%). Gate matching camera alone cannot establish comparable timing; added a repeated-baseline drift screening field. Keep the default unchanged. A drift below 10% is not statistical proof of a gain.
- Repeated the known PowerShell rg wildcard-path mistake for Skinning*. Search the Shaders/Private directory with -g instead. Corrected read-only search; no shader implementation or GPU-offload claim resulted.
- Baseline-screening audit: control request uses rhi_width but reports use rhi_command_width. Compare the actual report field; missing JSON properties otherwise compare as null/null and can silently pass incompatible configurations.

## 2026-10-05 GPU matrix gathering
- BoneFootprintR1 engine-provider probe preserves upload prerequisites. Last warm pass: raw source 11,116,032 bytes, unique pointer source 11,116,032, written/allocated encoded data 11,392,128 each. Raw matrix input need not increase transfer for this actual scene; no unused allocation tail savings were found.
- GpuBoneGatherR1 failed C4459: local WorkCount hides the atomic report counter, even with qualified use. Rename the local to GpuWorkCount. No failed build was staged.
- Engine transform buffer uses typed float4 views; GPU output must use a typed uint4 UAV view for bit-preserving writes, not a structured-buffer UAV. Compressed-transform builds fall back to the engine.
- GPU provider flushes upload producers before adding the RDG compute consumer. Preserve the completed setup task as a prerequisite for later providers; clearing the shared builder without restoring a prerequisite could permit later callbacks to race mesh updates. GPU source reads stay inside deferred uploader callbacks, never on the calling Render Thread.
- GpuBoneGatherABR1 executed on GPU, verified 653 proxies with zero bit mismatches and zero fallbacks. Mean frame 24.370 ms versus engine 21.370/21.942 ms: do not enable. Initial raw uploader used 16-byte scatter units (four CPU scatter indices per matrix) and re-uploaded immutable bone maps; corrected to 64-byte matrix units and reuse Context.BoneMapBufferSRV. Engine bone-map offsets are bytes, with uint32 GPU entries; CPU maps are uint16 and must not determine the GPU stride.
- GpuBoneGatherShaderR2 Cook failed before shader compilation: root UnrealEditor-OpenKoikatsu.dll could not load (Windows error 127). Changing the exported GPU helper from buffer to SRV changes ABI; rebuilding only the plugin leaves the consuming project DLL stale. Rebuild the root project module as well before Cook; do not stage this failed Cook.
- GpuBoneGatherR4 profile: engine matrix task 2.425 ms/call versus GPU raw-copy callback 1.372 ms/call, but scatter builders doubled from one to two per frame (7.15 ms/frame engine versus ~7.28 ms/frame candidate aggregate inclusive). Early builder flush defeated CPU savings. Use mapped QueueBufferUpload fills after UE 5.8 Compile setup-task wait; keep the shared scatter builder intact. Immediate RDG falls back, and readiness assertions guard upload order. This uses local engine ordering and needs audit on engine upgrades. Trace totals overlap and are not FPS savings.
- Formatting-only in-memory PowerShell -replace test incorrectly treated its scriptblock argument as a Match object and emitted null-array errors. It never wrote the project file. Reindent with explicit regex Match per line, then validate JSON. Host/source hashes for bone-packing cpp/h and scene cpp matched.

## 2026-10-05 CPU-only profiling correction and worker scaling
- Local UE 5.8 TimingExporter.cpp ExportTimerStatistics sets bIncludeOldGpu1/2=true and GpuQueueFilter=true independently of CpuThreadFilter. Consequently the legacy GameThread/RenderThread statistics CSVs include GPU timers; filtering by Render* also includes the Rendering Frames track. Never label their entire contents CPU-only. Export all CPU timing events for a bounded window and reconstruct exclusive nested scope costs. Tools/export_crowd_profile.ps1 now writes a warning beside each export. Tools/summarize_crowd_cpu_events.py rejects GPU/frame tracks, crossing scopes and overlapping children, and requires confirmation of unfiltered timer export. Four accounting regression tests passed.
- RHI* event selection includes RHIFrameFlipStatTrackingRunnable, whose WaitForVBlank is not RHIThread work. Select exact thread ID/name from the inventory before interpreting RHI costs; do not infer game VSync from that utility thread.
- Repeated the guessed-engine-source-path error for RenderScene.cpp and SkinningSceneExtension.cpp (actual RendererScene.cpp and Skinning/SkinningSceneExtension.cpp). Read-only commands failed; no files changed. Locate with rg --files first, then read the exact returned path.
- CpuWaitWorkers8R1 / CpuWaitWorkers16R1 / CpuWaitWorkers8R2 retained the identical ordered 100 cards, camera, 1600x1000 viewport, 45 near-face residents and zero missing shadows/packing mismatches. Frame means 25.119 / 21.097 / 19.364 ms; baseline drift 22.9%. Reject a causal worker-count comparison. No own builds/staging/trace export ran during these timing cases; external contention remains uncontrolled. No automatic adoption, affinity change or quality reduction.

## 2026-10-05 Lean Development mesh-update experiment
- Investigation rejected a custom GPU-skin subclass: FSkeletalMeshObjectGPUSkin is final, while component previous-transform mode bookkeeping is private. Do not bypass that bookkeeping or wrap the render object with a type incompatible with engine casts. A minimal Engine cpp-only opt-in debug-copy switch preserves those paths.
- Skipping the debug pose copy must also prevent UpdateRefToLocalMatrices from regenerating that debug output through its optional pointer. Pass nullptr when the copy is disabled; normal current/previous matrix calculation is unchanged.
- Empty debug data must be reported as nullptr by GetComponentSpaceTransforms; otherwise bone/physics debug consumers can index an empty array. Added that guard before staging. The original Editor path remains active and game users restore copy mode 1 for debug overlays.
- Repeated the known Windows rg wildcard-path error for SkeletalRenderGPUSkin.* and SkeletalMesh*. Those read-only searches failed; directory searches with -g work. No retry may pass a wildcard as a literal Windows path.
- LeanDebugPoseABR1 completed all per-case character/shadow/packing gates, but the final near-face population was 43 versus 45 in the first two cases. Baseline mean drift was 18.5%; the wrapper correctly rejected camera/population comparability. Candidate 13.018 ms is diagnostic only, not an adoption result. Keep default copy mode 1.
- apply_patch briefly failed writing OKCrowdScene.cpp during optional Engine-capability guards. Verified ample disk space, unchanged file contents and writable attributes; a separate retry succeeded. The failed call did not apply the intended guards, so do not start a build until the actual source is checked.
- Component-matrix reuse must compare owned source value bytes, not borrowed pointers/revision IDs. Cache only pre-inverse-bind transform conversion; hidden-parent/fallback and each mesh's private inverse bind must run unchanged. Current and previous arrays are independently processed. Any mismatch returns the original conversion and disables subsequent reuse; verification is excluded from FPS acceptance.
- Scene stats query is guarded by source capability detection and !WITH_EDITOR to avoid linking an optional Engine export on an unpatched or authoring Engine. The cpp-only export still requires rebuilding the consuming Game target; source detection alone does not make old Engine binaries compatible.
- ComponentMatrixVerifyR1 verified all 982,801 cache hits with zero byte mismatches, but recorded 193,809,584 misses (~0.50% hit rate). ComponentMatrixCacheABR1 screening passed with 9.05% baseline drift; means 26.064 / 19.731 / 23.706 ms. That narrow coverage does not justify attributing the entire intermediate difference to saved conversion work. No adoption or cache-size expansion from this result.
- ComponentMatrixIdentityVerifyR1 verified 1,124,581 exact identity/unit-scale conversions, zero mismatches/failures, with 249,979,209 fallbacks (~0.45% coverage). Do not assume imported affine palette transforms predominantly qualify for a pure-translation shortcut. Signed quaternion zero bits and non-unit scales intentionally fall back.
- ComponentMatrixIdentityABR1 matched camera/population and had 4.70% repeated-baseline drift. Means 16.862 / 15.728 / 16.069 ms; P95 21.163 / 19.415 / 19.857 ms. It does not meet 120 FPS and low eligibility plus uncontrolled background work prevents a robust whole-frame benefit claim. Keep mode 0.
- A documentation patch used a nonexistent wrapped-line context and was rejected without changing the file. Retry with the actual existing paragraph; no build or runtime change resulted.
- Follow-up cached-MDC investigation: r.SkeletalMesh.UseCachedMDCs already defaults true, and normal GPU skin supports static relevance. GetViewRelevance defines IsDynamic as a local lambda, not a separate SkinnedMeshSceneProxy.cpp class method. A guessed read of that nonexistent file failed. Read the existing GetViewRelevance body before searching for a separate implementation. Do not treat enabling already-active cached MDCs as a new optimization.

## 2026-10-05 Pose-bone audit compile R1
USkinnedMeshComponent does not expose the derived USkeletalMeshComponent GetSkeletalMeshAsset API. Use GetSkinnedAsset and a checked USkeletalMesh cast for follower traversal; non-skeletal followers are excluded. The initial build failed before producing a staged executable. Subsequent parse errors were cascading errors, not separate Clamp/section failures.

## 2026-10-05 Palette/weight audit limitations
The cooked Development probe found 754 parts with unavailable CPU skin weights. Palette closure is not an exact positive-weight count. Report weighted_closure_complete=false and prohibit using this diagnostic scan as Require120 acceptance. Do not retain large CPU mesh buffers simply to audit them, do not remove private animation bones based on palette counts, and do not equate a count difference with per-frame saved milliseconds.
File lookup error repeated during follow-up: guessed importer/commandlet CPP paths did not exist; use rg --files and only open returned paths. Imported animation methods may be implemented outside a matching-named CPP.

## 2026-10-05 Isolated editable-weight audit pilot
Disabling default Engine plugins also disabled ACLPlugin content, causing missing default compression settings warnings. Positive mesh-weight counts were available and zero mapping gaps were observed, but the pilot is not animation compression validation. Enable the installed ACLPlugin explicitly in the isolated audit host; do not silently accept a compression-settings fallback. The audit host builds only its two modules with UsePrecompiled/NoEngineChanges and does not replace root project modules or Engine DLLs.

## 2026-10-05 Animation audit pilot dependency failure
AnimationTrackPilotR1 compiled successfully but exited 3 while loading saved RigidClothes_0000. The saved animation uses AnimationData/ControlRig editor model classes; disabling all default plugins excluded these imports and triggered AnimSequenceBase's missing-data-model assertion. Mesh-only weight audits were unaffected. Explicitly enable existing AnimationData and ControlRig plugins in the isolated audit host before loading animation assets. Do not modify saved assets or interpret a missing model as zero animation tracks. No animation-audit output was produced by the failed pilot.
ACLDecompressionImpl.h explicitly decompresses the whole compressed pose for a requested subset. A 5.76% smaller RequiredBones array is not proof of a 5.76% reduction in compressed track decoding. Profile and count actual codec/track data before claiming this.

## 2026-10-05 Compression assumptions corrected after pilot
AnimationTrackPilotR2 succeeded after enabling the saved model dependencies. The sampled cooked-source walk codec is AnimCompress_BitwiseCompressOnly, not ACL. Generic ACL whole-pose behavior must not be attributed to this sequence. The FKControlRig model's legacy GetBoneAnimationTracks view returned zero while valid compressed data contained 2,309 tracks / 6,899,388 bytes. Zero legacy raw-track count is not zero animation content; report raw_track_view_available and use compressed mapping counts. Actual in-game codec reporting is now required before choosing codec-specific optimization. A diagnostic PowerShell field-selection command also had an accidental trailing pipeline command; rerun the corrected Select-Object/ConvertTo-Json pipeline, with no source effects.
# 2026-10-05 Final weighted-seed validation/tooling corrections

- ConstantScaleCacheR1 game build failed because UE TAtomic has no FetchAdd method. Inspected the installed Atomic.h, replaced counter updates with its atomic operator+= and rebuilt using a fresh tag. This was a compile-only failure; no failed executable was staged.
- Patch export initially failed because a Python -c command used shell-incompatible quote escaping. Replaced it with a literal PowerShell here-string piped to Python. No Engine source mutation or partial patch output occurred in the failed command. The final complete patch includes the profiling scopes; do not apply it together with the standalone scopes patch.
- Scalar/SIMD switching is not bit exact: the read-only pilot compared 13,854 matrices, found 3,149 differences and maximum float matrix element error 0.000132936. Its same-process timing improvement was within baseline drift. Keep the original SIMD path; do not adopt switching or claim a quality-preserving gain.
- Final WASD/face-LOD/equality diagnostics returned a raw P95 below 8.33 ms because the test changes camera/detail workload. That is not steady-quality FPS acceptance. The new fps_acceptance_eligible=false gate and target_met=false prevent claiming 120 FPS from this functional test. Report only the fixed-camera untraced A/B timings as comparative performance evidence.

- A source lookup guessed AnimBoneCompressionCodec_BitwiseCompressOnly.cpp, which does not exist locally. Use rg --files first; the actual implementation is AnimCompress_BitwiseCompressOnly.cpp and AnimEncoding_ConstantKeyLerp.cpp. No code was changed based on the failed lookup.
- A diagnostic log lookup used -build.log instead of the wrapper's .build.log suffix. Discover actual files before reading; the audit itself completed successfully.
- Overlapping Select-Object wildcards (*count*, *resident*) selected resident_lod_counts twice and produced a diagnostic error. Use explicit unique receipt fields. This did not affect game execution or saved evidence.
- Weighted seed launcher gates originally required 100 applications even in the supported one-card pilot mode. Changed these gates to require at least the actual loaded population; the independent 100-distinct-card gate remains for normal crowd launches. Verify the pilot separately rather than weakening population acceptance.
- AnimationTrackPilotR3 confirms AnimationSequencerDataModel, valid compressed tracks and an unavailable legacy raw-track view. Actual hundred-game codec report confirms BitwiseCompressOnly for all clips. Do not interpret raw_tracks=0 as no animation or extrapolate ACL full-pose decoding to this workload.

### GPU animation planning: repository instruction discovery

Attempted to read root AGENTS.md, but no physical file exists at C:/portable/OpenKoikatsu/AGENTS.md. The user-supplied AGENTS instructions still apply. Check file existence before future reads; do not treat a missing root file as cancellation of supplied coordination/authorization rules. No source assets or runtime files were changed by this failed read.

### Independent GPU pose oracle: readback ownership API

First isolated build IndependentR1 failed C2665 at AddEnqueueCopyPass. MakeShared returns TSharedRef; its Get() returns a reference, unlike TUniquePtr/TSharedPtr Get(). Pass &Readback.Get() to the verified FRHIGPUBufferReadback* overload. Preserve shared lifetime through queued render commands. Consult concrete smart-pointer types instead of copying call syntax from the older unique-owned packing validator. No Editor Engine binaries were rebuilt.

### Independent GPU oracle: host shader platform and first startup

IndependentR2 C++ build succeeded, but the isolated host selected its default SM5 rendering configuration and was terminated by the owned-process 180-second timeout while first-start default material shaders were compiling; no pose result was produced. Give the isolated host an explicit DX12/SM6 rendering configuration matching the game's relevant rendering settings, and allow a bounded 600-second first shader startup. This is authoring/test setup time, not game loading/FPS evidence. Stop only the exact owned process on timeout. Shader worker processes and unrelated sessions must not be broadly killed.

### Independent GPU pose development: Windows ripgrep path syntax

A discovery command passed Source/OpenKoikatsu/OKCrowd*cpp as a path to rg, which Windows did not expand and rg rejected. Use a concrete directory plus -g 'OKCrowd*.cpp', or rg --files with filtering. No implementation depended on the failed discovery output.

### Independent GPU pose development: exact-skinning source location

A follow-up read guessed KKExactSkinning.cpp under KKImportedAssets and failed. The verified source is Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKExactSkinning.cpp. Discover a filename with rg --files before reading a guessed plugin path. This read did not modify assets or affect the GPU oracle result.

### Real-card GPU oracle R1: failed numerical equivalence

RealCardR1 built and ran actual GPU work, but one private clip (2,309 bones, 126 native uniform samples) failed the predeclared 0.001 matrix-element gate: max 2.4808921814, RMS 0.0628075046, finite. Do not enable gameplay or widen the gate. Added diagnostic local-pose versus component-pose comparisons and worst-bone identity to distinguish interpolation/data export from native FTransform hierarchy semantics. The failed report is retained. Key preparation is setup only and has not established compact loading or skin-vertex correctness.

Additional discovery commands guessed two Engine source paths (AnimEncoding_ConstantKeyLerp.h and AnimEncoding.cpp under Private) that did not exist. Use rg --files; ConstantKeyLerp.h is under Engine/Public and source discovery must precede reading/searching specific filenames.

### Real-card GPU oracle R2: matrix hierarchy is not native FTransform hierarchy

Local animation sampling passed with max matrix error 0.0000534057617, while component composition still differed by 2.4808921814 at cf_j_sk_06_05. This isolates the failure to hierarchy semantics, not key preparation/interpolation. Added an explicit native positive-scale TRS hierarchy mode: root-to-leaf quaternion product, component-wise scale product and parent rotation/scale translation. Matrix multiplication remains a separate explicit mode. Native mode rejects negative scales (which require UE's matrix reconstruction semantics), unsupported modes and depth >256 before dispatch. No card-name special case or tolerance increase. Real-card rendering remains off until actual pose validation passes.

### Real ten-card oracle R1: artificial total-skeleton cap

RealTenR1 rejected one card at the initial dimension guard, leaving 9 prepared actors and preventing all GPU comparisons. The initial 4,096 total-bone cap was an arbitrary prototype guard, not UE's per-section palette limit. Increased the validated total per-actor bound to 65,536 (16-bit bone-index range); kept index/range/depth checks and expanded rejection diagnostics with dimensions. This does not increase per-section palette limits or simplify rigs. Added independent per-actor interpolation phases; endpoints remain explicit. No card-specific exception. A future packed-render integration must validate consumer-specific capacities and dispatch limits independently.

### Real ten-card oracle R2: negative-scale reconstruction required

After removing the artificial total-bone cap, all ten clips prepared, but validation found negative scales and correctly blocked GPU dispatch. Added the actual native reconstruction branch used when either parent or local scale has a negative component: multiply the matrices, normalize each axis, restore desired scale signs, extract/normalize quaternion, and preserve the native zero-axis identity-rotation behavior. Native hierarchy composition stays root-to-leaf. Removed the negative-scale rejection only together with this implementation. Extended synthetic coverage to native hierarchies with negative/zero scales; do not use generic matrix composition as a substitute or clamp scales to force acceptance.

### Real GPU vertex oracle: vector helper lookup and isolated build queue

The first vertex-oracle edit assumed FVector4f::Dot4, but installed Vector4.h defines Dot4 as a free function. Corrected source and the isolated snapshot while Build.bat was still queued behind an existing build mutex, before our compiler ran. Do not guess ownership/API from spelling; verify primary headers first. Two read-only discovery commands also guessed SkeletalMeshAttributes.h/BoneWeights.h under the wrong module; verified locations are SkeletalMeshDescription/Public and AnimationCore/Public. Existing unrelated build ownership is preserved; wait for the mutex rather than killing other build processes.

### Resident R1: cached UBT source inventory
The isolated host contained OKResidentGpuPose.cpp, but cached UBT makefiles omitted the new translation unit and linking failed with seven unresolved symbols. Added -gather to the isolated oracle build so new source files are always discovered. Preserve failed evidence; do not delete broad Intermediate directories or touch the authoring Editor build.

### Palette research: Windows rg glob paths and native transform layout

Two read-only searches repeated a Windows rg path-glob mistake (Tools/*profile* and RenderGraph*.cpp as path arguments). Use an existing directory plus -g for filename filters. No files were changed by those failures. StoreCompressedBoneTransform is defined in Engine/Shaders/Shared/SkinningDefinitions.h, not the initially searched Runtime headers. Verified the actual native transposed float32 3x4 encoding and compare GPU palette output against that function; do not infer layout from the C++ type name or substitute a TRS/half representation for private affine data.

### AnimationCacheR1: unity-build anonymous namespace name collision

The cache constant named Layout collided with the existing resident function's local Layout under UE unity compilation; warning C4459 is an error in this configuration. Renamed all cache format constants with the Cache prefix rather than disabling warnings or changing build settings. Anonymous namespaces across cpp files in one unity translation unit are the same namespace; use subsystem-specific names for file-private symbols. Source validation remains shared between cache decode and resident initialization.

### AnimationCacheFinalR1: Unreal future result ownership

TFuture::Get returns a const reference, unlike std::future::get. Copying a worker result containing TUniquePtr fails C2280. Changed diagnostic handoff to TFuture::Consume, which transfers ownership and invalidates the future. Verify installed future API semantics before choosing Get/Consume; never duplicate ownership or wait for futures inside the gameplay frame loop. The core asynchronous loader compiled successfully; the error was in the diagnostic result consumer.

### ManifestSyntheticR1: negative unsigned synthetic binding offset

The synthetic fixture wrote -A * 0.001f and -B * 0.1f with uint32 indices; unary negation occurs before floating conversion, producing unsigned wrap and C4146. Convert the index to float before negation. This fixes fixture inverse bindings; no warning suppression or tolerance increase. The ten real-card manifest/selection test already passed and was unaffected.

### Manifest review: explicit NUL validation and component-pose contract

During source review, removed a TEXT("\0") substring check because it is an empty string rather than an embedded-NUL test. Validate each character value directly. Also require identity pose-task bindings in the manifest contract: this stage describes unbound component poses that the mesh palette binds once. Accepting already-bound task output here would double-apply mesh inverse binds. Added a conservative selection byte-budget preflight before copying track keys; preserve output on rejection.

### ManifestSyntheticR2: launcher receipt branch omitted

The actual GPU synthetic test passed, but the launcher routed its result to the legacy two-clip receipt gate. A multiline string replacement did not match CRLF text. Added the standalone synthetic-manifest branch with a structured patch and rejected mixed incompatible modes. Preserve the successful raw GPU report and failed launcher receipt separately; retest using a fresh tag and the same verified binary snapshot. Verify the final branch in source instead of assuming replacement succeeded.

### Formal control R1: executable and cooked global shader mismatch

The isolated Development build includes the newly registered independent-pose and palette global shaders. Reusing the previous crowd Pak unchanged crashed during startup: missing FOKIndependentPoseCS permutation 0. An opt-in request does not exempt a registered global shader from startup validation. Preserve the failed log/stage; do not overwrite the stable stage or disable validation. Cook matching SM6 global shader resources with the verified isolated Editor plugin, package an isolated shader patch, and retest with a fresh tag. Shader changes require matching Cook content even when the feature is disabled.

### Windows search correction during control work

Again passed a wildcard path to rg (OKCrowd*); Windows did not expand it. Use an existing directory argument and -g 'OKCrowd*.cpp'. The failed read-only search made no changes. Do not use shell-glob path arguments for future searches.

### Shader Cook R1: implicit SM5 target / unsupported ancestor stack compiler

The isolated oracle host's config added SM6 but retained the Engine's default SM5 target. Cook attempted FXC/SM5 and failed to unroll the dynamically indexed native ancestor stack. Declare the independent-pose shader's actual minimum capability as SM6; expose a render-thread HasShader check and reject resident initialization/evaluation before allocation or dispatch when unsupported. This is a software feature requirement, not vendor/model tuning. Use the existing CrowdCookSM6 config for the matching formal-scene patch. No algorithm, tolerance, or material-quality change. Do not globally disable shader verification or hide missing shader failures.

### Formal control R2: incomplete launch prerequisites

Shader startup passed with the new patch, but the control test enabled render-upload culling without the required per-mesh bounds manifest. Scene validation correctly exited. Add a checked SectionLODFile input using the existing SectionBoundsHair100R4 manifest rather than relaxing scene validation or turning off the production policy. Preserve failed evidence and run a fresh tag. A successful executable build or shader startup is not a successful formal-scene test.

### Actual renderer verification R1: GPU writes alone are insufficient

RenderBridgeR1 executes GPU palette writes and toggles successfully, but the subsequent actual scene-buffer/native-CPU comparison failed (30 components, max error 22.609542847, 141020 scalar mismatches). The bridge automatically returns to CPU after a mismatch; preserve both receipts. Do not interpret queued GPU passes or isolated pose/palette oracles as actual-renderer correctness. Diagnose clock, pose history and final renderer layout; add worst actor/bone/time/matrix evidence, retain the 0.001 tolerance, and rerun before removing CPU work or making any performance/quality claim.
### Actual renderer buffer R2/R3/R4: finalize clock, not requested clock

Actual renderer readback initially differed by 22.61 cm. Reading single-node time before RefreshBoneTransforms made the discrepancy worse (126.20 cm): the requested clock is not the finalized evaluated pose clock. Capture clip/time after Super::FinalizeBoneTransform and enqueue the corresponding render-state clock from that finalization callback, rather than only from scene Tick. Preserve CPU pose during this integration and fail back to CPU on comparison error. RenderVerifyR4 compared 25,872 actually rendered matrices across 30 components, maximum scalar error 0.00006103515625, zero mismatches. Previous rendered buffer and full visual acceptance remain separate work.

### Timing evidence discipline

The capped 60 FPS RenderVerifyR4 functional run included GPU readback and concurrent oracle/build work. Its 24.976 ms mean / 51.298 ms P95 cannot establish throughput or a CPU/GPU improvement. Use uncapped formal Development CPU/GPU/CPU windows, exclude diagnostic readback and owned builds, retain all post-warm-up frames, and always report mean plus P95 against the 8.333 ms budget. The current bridge retains native CPU pose and its fixture covers only ten cards; explicitly label this intermediate hybrid scope.
### UncappedHybridR1: foreign editor started during timing

CPU/GPU/CPU mean frame times were 14.393/14.042/17.377 ms and P95 19.241/18.252/25.167 ms. A foreign UnrealEditor started during the run; CPU controls drifted 20.73%. Retain the raw timing evidence but reject speedup inference. Added explicit CPU control drift reporting to the benchmark receipt. Do not terminate another task's Editor to manufacture a clean result; report the interference and rerun later with matched clean controls. See CROWD_GPU_HYBRID_TIMING_R1.md.
### History audit implementation: verified names and paths

One read-only shader lookup used Source/KKCrowdGpuPacking/Shaders instead of the plugin-level Shaders directory. A guarded report-field replacement also assumed Independent instead of the actual IndependentStats name and rejected without writing that file. Inspect the declared path/name before scripted replacement; keep replacement guards. No asset or shader was changed by either failed command.
### Mixed independent batch history

The resident batch's global Same flag does not establish that each actor advanced:
one actor can hold its 60 Hz pose while another advances. At the renderer palette
boundary, compare that actor's last/current owned clip IDs and times and select
current as previous for the held actor. Native previous matrices are now audited
when UE requests a previous update; deliberate first-ON history reset is excluded
from comparison to pre-switch CPU history. RenderHistoryR2 passes cumulative
179,439 comparisons (12,762 previous), max 0.000186920166015625, zero errors.

### OFF bridge overhead

The initial adapter scanned 100 actors and copied/enqueued playback arrays even
while OFF. Disabled mode must not add that per-frame workload to the control.
Skip scans and finalized-clock commands while OFF, enqueue a single disable
transition, then rescan/reset on ON. Runtime OFF/ON/OFF regression passed with
100 animated cards and zero active GPU components/actors after OFF.
### UncappedHistoryR1: stable controls do not prove an uncontaminated run

CPU/GPU/CPU means 22.504/22.668/21.800 ms, P95 31.590/32.738/32.892 ms.
CPU drift 3.13% passes the numeric drift gate, but process monitoring confirms a
foreign ok-preview-pack conversion consumed substantial CPU during all phases.
The drift gate is necessary, not sufficient. Reject clean speedup/120 FPS acceptance
and preserve timings plus process evidence. Do not attribute the absolute increase
over an earlier quiet-session run to code changes without matched clean controls.
See CROWD_GPU_HISTORY_CHECKPOINT.md for functional proof and remaining CPU work.
### GPU-only provider probe build

Native provider requires a non-null FTransformProviderRenderProxy; returning null
silently selects reference-pose provider. Supply a scene-owned resource proxy and
register a distinct provider ID. R1 compilation found protected AnimationMinScreenSize
(use SetAnimationMinScreenSize) and a local MaterialIndex shadowing bug (rename loop
variable). Preserve compiler warnings. The setter correction followed the R2 source
snapshot, so a fresh R3 build is required. A repeated Windows rg wildcard path failed
read-only; use an existing directory with -g. Wrong SkinningSceneExtensionProxy.h
location was corrected using rg --files: the header belongs to Runtime/Engine/Public.
### GPU-only provider R1: renderer context capacity is not initialized length

First real provider probe crashed during reference lookup. Skinning provider context
Primitives/Proxies views include allocated capacity beyond the initialized prefix;
scanning their full Num dereferenced an uninitialized pointer. For primitive-provider
context only [0, current valid indirection.Index] is known initialized by the engine
fill loop; bound the optional diagnostic reference lookup to that prefix. Missing
reference must skip comparison, never read arbitrary capacity. Add explicit probe
execution/matrix/error gates to the launcher so zero probe execution cannot pass.
Preserve GpuOnlyProviderR1 crash evidence. No production scene adoption occurred.
### GPU-only provider R2: source context and scheduling contract

The tightened probe gate correctly failed: one GPU component but zero validated
matrices. Native and instanced provider contexts do not share usable reference
indices. Capture the native MeshObject only from its valid native indirections,
order native registration before the diagnostic provider, and retain the diagnostic
source actor until probe teardown. Use the instanced skeleton-batch animation
scheduling path for continuous updates. The probe has one independently owned rig
and no animation sharing; future multiple private rigs require isolated provider
keys/ownership before adoption. Non-batch primitive dirty updates alone do not
establish continuous GPU animation. No production display was switched.
### GPU-only provider R3: batch context has no primitive/proxy arrays

Batch callbacks provide SkeletonBatches and indirection offsets; Primitives and
Proxies arrays are empty. The earlier common adapter assumed a primitive context
and hit index 0 of an empty array. The GPU-only render proxy must register its own
actual instanced proxy for its private provider ID, and use that validated proxy
in the batch callback. Each provider object now owns a fresh GUID, avoiding
accidental sharing of incompatible private rigs. Preserve R3 crash evidence.
### GPU-only provider R4: diagnostic batch/primitive phase alignment

Actual probe comparison found ~25 cm mismatch and safely rejected execution.
Instanced batch callbacks occur before native primitive callbacks/mesh-update
reference phase. For the CPU-reference diagnostic only, retain a graph-owned batch
context and submit its probe in the native callback of that same graph. Do not
compare two different clock/update phases or loosen tolerance. A non-diagnostic
GPU-owned provider executes independently in the batch callback; this probe still
retains native references and does not represent full crowd CPU offload.
### GPU-only provider R5: diagnostic reference freshness

Probe R5 still rejected ~18 cm mismatch. A native reference can be outside the
render-upload policy and hold data from a different pose. The diagnostic source
now bypasses render-upload suppression while the probe exists, restored at teardown;
the probe also uses the exact regular renderer playback snapshot in the same graph.
This is diagnostic freshness, not a production culling change or a tolerance change.
No pass is claimed until actual readback gates succeed. Batch failure/OFF must not
call native primitive fallback with empty context arrays; skip the hidden diagnostic
batch instead. Another read-only shader path error repeated the source-relative
Shaders mistake; actual shaders live at plugin-root Shaders.
### GPU-only provider R6: current matrices pass; native previous update is None

After keeping the diagnostic source upload fresh, R6 current matrices passed
18,696 comparisons, max 0.0000152587890625, zero errors. The tightened history
gate still rejected the run because this native source sets UpdateMode=None,
so the previous-native-update count was zero. The GPU-only diagnostic now retains
the previous frame's already verified native current matrices every frame, after
native prerequisites, and uses that as its independent GPU render-history reference.
This CPU reference copying is diagnostic-only; it does not add an animation
evaluator to the GPU-only component or change production previous-transform policy.
### GPU-only provider R7 verified closure

Development R11 build and actual scene GpuOnlyProviderR7 now pass current/history
gates: 32,718 total / 14,022 previous matrices, max 0.0000152587890625, zero errors.
No CPU skeletal pose evaluator exists on the single instanced probe; tick is disabled.
100 originals remain animated and OFF hides the probe. This closes diagnostic
provider integration correctness, not full production crowd CPU offload/material
appearance acceptance. Capped/readback ON frame mean 21.210 ms, P95 30.883 ms;
do not claim speedup or 120 FPS from this functional run.

### Instanced material preparation R1 build
MakeShared returns TSharedRef; calling ToSharedRef again failed compilation. Pass the existing shared reference directly to FJsonSerializer. No source assets were written. Also corrected research paths: UE5.8 material headers live under Engine/Public/Materials, and the existing oracle helper is run_independent_gpu_pose_oracle.ps1 (there is no build_independent_gpu_pose_oracle.ps1).


### Instanced material preparation R2/R3 inherited usage cache
Strict BasePropertyOverrides byte comparison rejected a valid inherited usage change. UE5.8 UpdateOverridableBaseProperties copies parent usage flags into BasePropertyOverrides. Verification now permits only the InstancedSkinnedMesh usage bit, compares every other base override and parameter unchanged, and checks the final material interface usage (not only its root material). No blanket parameter-comparison bypass or card-specific exception is used. Engine implementation is in Private/Materials/MaterialShared.cpp; no separate MaterialInstanceBasePropertyOverrides.cpp exists.


### Instanced material Cook R1 empty asset subset
Cook exited successfully but omitted every requested material. UE5.8 CollectFilesToCook enumerates CookDirectories only when SkipSoftReferences is unset. The package-count gate rejected the empty Cook before producing a Pak. The material helper removes SkipSoftReferences while retaining the bounded generated-material Cook directory; the existing global-only shader helper remains unchanged.


### Instanced material Cook R2 Editor BuildId drift
Building the isolated Development game changed the installed-engine module manifest identity. The older isolated Editor plugins were rejected before Cook startup. Rebuild the isolated GpuPoseAuditEditor after game build, and compare plugin/module BuildIds before starting Cook. Do not manually forge module identities or touch authoring Editor DLLs.


### Original material checkpoint verified closure
OriginalMaterialsR4 preparation passes 249 material shader resources / 157 slots. OriginalMaterialsR3 Cook passes all 249 prepared packages and isolated library. Development OriginalMaterialsR2 build and GpuOnlyOriginalMaterialsR1 formal scene OFF/ON/OFF pass, 59 original prepared slots on one GPU-only component, 32,718 current/history matrices including 14,022 previous, zero errors. Shader loading log has no missing-map or default-material fallback errors. ON functional frame mean 20.233 ms / P95 34.365 ms, capped at 60 with CPU references/readback; OFF control drift 33.42%, so no performance acceptance. Missing pre-recorded pipeline cache is a pending PSO-prewarm task, not a missing material shader. Full 100-character GPU ownership, near parts and 120 FPS remain open.


### Same-frame visual capture R1 missing bounds input
The game exited before capture because upload culling requires the matching low-mesh bounds file. The capture-receipt gate rejected exit0 without images. The dedicated visual runner now supplies the same SectionBoundsHair100R4 input as formal control tests. Also corrected helper discovery: no build_card_reader_development.ps1 exists; use actual reader build/test sources.


### Alice GPU face investigation — 2026-10-05
- Alice GPU five-feature loss is a failed visual gate; body-part-only crowd comparison does not validate a full GPU face.
- AliceGpuNoMorphR1: old reader host BuildId no longer matches the local engine; do not mutate the shared reader host. Use isolated copies with compatible DLL snapshots and matching manifests.
- AliceGpuNoMorphR2: isolated plugin copy omitted Shaders, causing AddShaderSourceDirectoryMapping assertion. Include shader directories before launching.
- AliceGpuNoMorphR3: relative APNG directory could not resolve in isolated game process. Pass absolute CardDirectory. R4 loaded Alice successfully; morph disabled and occlusion disabled still lack eyes.
- Diagnostic R1 matched hidden Skel follower before visible Mouth replacement. Select visible components when diagnosing displayed parts; CPU vertex audit prioritizes Mouth separately.
- Use actual existing paths from rg --files before reading engine source; components and plugin intermediates are not located beside generic engine/private or project module intermediates.
- Bounds R2 restored GPU eyes without changing materials, meshes, morphs or CPU/GPU deformation. Two-sided materials did not fix the missing features. Reference-pose imported bounds were misplaced relative to shaped posed faces; use per-bone posed envelopes, including active morph intervals, instead of a constant oversized bounds hack.
- Runtime bounds build R1/R2: UE 5.8 exposes LeaderPoseComponent directly and GetMorphTargets returns TObjectPtr elements; do not assume getter or auto-pointer iteration. Build R3 succeeds after both API corrections.
- CardIds must be a comma-separated string ('2,1,50'), not a PowerShell array. Unquoted 2,1,50 bound to the old string parameter as space-separated text and only Alice was exercised; reran '1,50' explicitly and verified 21 GPU outfits across all three cards. New audit validates the argument format.
- Tight posed bone/morph bounds replaced the 100x diagnostic. GPU face restored in all Alice outfits; 21 GPU outfit captures and 294 bounds samples contain every displayed vertex (maximum escape 0 cm). Remaining highlight/jaw-outline differences are recorded rather than claiming exact CPU/GPU equivalence.
# 2026-10-05 GPU face outline follow-up

- Final default-path audit R1 failed before scene initialization: UE engine BuildId changed from `8efe457f-c803-4fca-96e9-fb6466924b49` to `60660ed3-9361-44d4-9fc3-dffe94156301`, so isolated-host manifests rejected KKCrowdGpuPacking. This is a host/module alignment failure, not a visual test pass or character failure. Align only this task's isolated host manifests with the current local engine, copy current built main/plugin DLLs, then retry with fresh R2 output. Never modify another task's host or count startup failure as acceptance.

- Confirmed CPU `ApplyMouth` added unskinned reference-space morph deltas directly onto posed vertices. A source head with non-unit scale changes eye-open strength. Transform the summed delta with the character's skin palette as a vector, never as a position with translation. The isolated Alice seven-outfit test reduced eye ROI MAE from ~20.4 to 2.760–2.898/255. Promoted to the default implementation after the diagnostic; do not retain a per-card or temporary environment bypass.

- Non-default expression comparison exposed a separate CPU/GPU mismatch: with eye-open .35 and mouth-open .65, CPU eyes nearly closed while GPU stayed half-open; eye ROI MAE ~20.4/255. Equal outline/surface vertices do not prove CPU/GPU expression parity. Preserve the captures in `GpuFaceOutlineExpressionComparisonR1` and inspect the morph coordinate space before changing materials or claiming full visual acceptance.

- Do not mistake restored facial features for complete CPU/GPU parity. The CPU face had a source outline pass, while the GPU branch only constructed selected clothing outlines. Add the face outline with original skin weights, morphs, source line width, detail/alpha masks, reverse winding and the same inner-mouth triangle exclusion; do not substitute a card-specific jaw line.
- A shell in reference pose or with missing expression morphs would detach during blinking/mouth movement. Validate transformed shell vertices against the surface and check its actual renderer bounds in the reader audit.
- PowerShell `rg` does not expand path globs such as `Private/OKSkeletal*`. Discover filenames with `rg --files` then search exact files or the directory. No source file existed at that guessed path.

# 2026-10-05 GPU eye highlight UV follow-up

- Final static UV audit passed 21 outfits / 42 eye buffers (max UV error .000343246), but rendered images remain different: Augusta eye ROI 9.254–9.910/255, Alice 3.107–3.177/255. Never declare full CPU/GPU visual parity merely because buffer channels, outline vertices, bounds and startup tests pass. Preserve comparison images and investigate remaining pose/geometry differences separately. Parallel functional runs are not valid 120 FPS performance evidence.

- Do not infer matching material objects imply matching sampling. CPU `FillSection` uses `eye_sample_uvs`, Unity UV1 and UV2 for eye draws, while the skeletal builder previously used raw UV0 and generic UV4 overlay mapping, sometimes allocating only one channel. The eye shader samples TEXCOORD1/2, so both missing highlight channels and wrong main-eye coordinates change appearance. Preserve all three channels by source `eye_draw`, not card number or mesh-name exceptions. Add actual buffer/source UV comparison to the skin audit.
- Initial R1 compile covered only the two highlight channels. Code comparison also exposed `eye_sample_uvs` versus raw UV0; included both in R2 before claiming acceptance.
- `SetEyeGaze` currently updates procedural eye streams. Runtime changing GPU gaze needs a separate parameter/update-path implementation; restoring startup streams alone must not be reported as complete animated eye-look parity.

# 2026-10-05 GPU live eye gaze

- `GpuEyeGazeFinalR1` passed the reader sequence, but the queued Automation command had no Test Completed result before the reader requested exit. A queued test is not a passed unit test. Run affine edge-case automation separately through UnrealEditor-Cmd with `-TestExit="Automation Test Queue Empty"`, and inspect the actual completion result.
- The sequence patch initially failed to match a minified screenshot line; apply_patch was atomic and changed nothing. Read the exact line before a replacement. Also, guessed `MaterialEditorOnlyData.h` under Classes/Materials did not exist; discover paths with `rg --files` before reading engine headers. No custom-UV performance change was made from that unsuccessful lookup.

- R1 live/sequence reports were false positives for gaze movement. GPU parts intentionally have `Part.Comp == nullptr`; the legacy setter skipped them before reaching the new MID update. The UV audit compared stale `Part.UVs` rather than fresh Session UVs, so unchanged images passed the numeric gate. Fix the guard to accept GPU eye materials, skip only procedural upload when Comp is null, and compare shader rows against fresh source `eye_sample_uvs/uv2/uv3`. Preserve R1 images as failed diagnostic evidence, never count them as dynamic-gaze acceptance.
- A sequence screenshot is required for movement; nine successful API return values plus unchanged mesh pointers cannot prove visible eye movement.

- Build R1/R2 failed because the new vector-parameter expression lacked `Materials/MaterialExpressionVectorParameter.h`. Add the concrete expression include rather than relying on material header transitive declarations; dependent NewObject errors were secondary. Builds are not acceptance unless Result is Succeeded.
- Main-eye affine gaze must run before the material's static MainTex ST, matching the procedural sampling order. Implement with material nodes; the existing mask path explicitly records a custom-expression black-mask issue, so do not reintroduce that route.
- Morph-capable GPU parts replace the visible poseable follower with a skeletal component. `Part.SurfaceMat` must point at the visible replacement material; otherwise gaze parameters update only the hidden follower.
- Static eye UV audit must evaluate the material's live rows before comparing to the source target UV; immutable buffer coordinates alone intentionally stay at their loaded values. Also verify skeletal asset pointers survive multiple gaze updates without rebuilding.


## 2026-10-05 — Source/GPU palette diagnostics and affine-origin probe

- Avoid guessing engine/module file locations: OKPaintSkinning is a header, ok_extract.h was not at the guessed Public path, and engine SkinnedMeshComponent.cpp is under Private/Components. Use rg --files before reads. These missing-file reads made no changes.
- PowerShell does not expand Bash brace-list paths (`{SkinnedMeshComponent,SkeletalMeshComponent}.h`). Supply separate paths; the parse failure executed no command.
- Affine-origin probe R1 failed compilation because UE 5.8 has no GetLeaderPoseComponent() accessor. The verified existing public LeaderPoseComponent weak pointer is used instead. R2 build succeeded.
- Do not compare all bone palette entries indiscriminately: Unreal intentionally leaves unused bones at identity in a mesh's RefToLocal buffer. Matrix statistics must be limited to RenderSections.BoneMap; otherwise large unused-bone differences give misleading diagnostics. Vertex skinning comparison remains valid.
- GpuGeometryProbeR1 separates reference-vertex skinning from morphs. Augusta iris errors remain ~0.171/0.142 cm before morphs, while its source baseline agrees within ~0.000031 cm. This rules out gaze UV and morph parameters as the primary iris-position cause.
- OK_AFFINE_BONE_ORIGINS is an isolated investigation flag, not a production/final fix. It tests exact matrix-chain bone origins while retaining engine TRS orientations/scales; remaining affine shear is not represented by FTransform. Do not label it full CPU/GPU parity or a GPU animation offload.

### Affine palette investigation outcomes

- Origin-only `GpuAffineOriginsProbeR1` did not materially fix iris geometry (.170/.142 cm remain); do not ship a position-offset workaround. Its code and environment flag were removed.
- Palette probe R1 / R2 and acceptance R1 failed before scene startup due changing engine BuildIds / not refreshing every owned plugin manifest. These are host failures, not geometry evidence. `sync_gpu_skin_audit_host.ps1` now refreshes the entire owned host's module set, checks BuildId compatibility, rejects binary-directory junctions and rejects a running host.
- Syncing while the owned CPU audit still ran hit a DLL file lock. Waited for that owned audit to finish; no foreign process was stopped. The new running-host preflight avoids partial sync attempts.
- Skipping unused singular bones was necessary but insufficient: Augusta's *used* cf_J_ChinLow bone has source scale Z=0. Rejecting the entire snapshot left the face incorrect. Used singular render bones now have an invertible TRS carrier while the affine palette retains the actual zero-scale source geometry. The final used-bone refinement leaves the dummy leader's unused bones alone.
- Matrix diagnostics now restrict comparisons to RenderSections.BoneMap. Reference-vertex skin checks are the primary affine oracle, not whole-image averages or all skeleton matrices.
- Final ordinary build R1 was rejected by UBT with `Unable to build while Live Coding is active`. This is a compiler-detected state, not evidence of a visible Live Coding panel or an active compilation in the user's UI. No other editor was stopped, mutex deleted or guard bypassed to overwrite loaded project files. A private compile/link completed using fresh objects/DLL output in `GpuAffinePalettePrivateBuildR1` and final private functionality passed. Later read-only mutex recheck found it absent; ordinary UBT was retried.
- `rg` does not expand PowerShell wildcard/brace arguments; use `rg --files --no-ignore` for ignored Intermediate response files, then exact paths or `-g` patterns. Missing wildcard-path reads changed no files.

### CPU walking reference guard / camera oracle correction

- `CpuAffinePaletteWalkR1` was NOT a valid moving-pose CPU visual reference. `ApplyPoseFrame` returned immediately when SkelFollowers was empty; its audit checked setter success but did not check AnimPhase advanced. GPU advanced to .37 seconds while CPU stayed at the initial pose. `GpuAffinePaletteWalkComparisonR1` therefore compares different animation times and must not be treated as native visual acceptance.
- Native CPU animation now evaluates without GPU followers, skins the current source frame and refreshes all procedural part bases and existing secondary sections. GPU rendering retains its prior no-CPU-body-skin path. CPU external UE retargeting still requires its supported skeleton path; this change is for source/native clips.
- Walk audits now assert the actual AnimPhase matches the requested elapsed time. Portrait framing uses a shared current source-palette face envelope instead of GPU's cached reference BaseVertices, so cameras match during animated comparisons.
- The final needed-bone refinement ordinary build was later retried after the read-only Live Coding mutex check found it absent. `GpuAffinePaletteFinalBuildR2.log` succeeded; `CpuAnimationReferenceBuildR1.log` also succeeded. The private fallback is not the sole compilation evidence.
- `WalkParityCorrectClockComparisonR1` is not visual acceptance despite both clocks reaching .37 seconds. CPU mesh views contain prefab-local vertices/indices while GPU views contain shared-skeleton reference data. Feeding both to OKDeformFromRef does not establish a shared comparison camera. Fresh portrait framing uses the exact native part palette plus raw vertices in both modes and logs focus/distance/yaw. Large whole-image differences must be investigated, not hidden by successful matrix gates.
- A subsequent rg command accidentally used a literal PowerShell `session*` path again. It failed without mutations. Use the containing directory and a `-g` filter or an exact path; prior wildcard warnings remain applicable.
- `WalkNativeCameraComparisonR1` still showed large differences, so the camera change is not an image fix. Native `skin_all` handled body/head kinds 1/2 but omitted full-head/hair kind 3; CPU hair stayed at rest during a moving face pose. Kind 3 now skins with its own full-head skeleton and applies the source head-to-body transform. `CpuFullHeadSkinBuildR1.log` confirms ordinary compilation.
- CPU mouth/expression code also applied the shared-skeleton deformation helper to private prefab vertex indices and already transformed CPU morph deltas. It now obtains canonical raw morph deltas and applies the actual native per-part palette once as vectors. Unskinned parts retain the existing fallback. `CpuNativeMorphPaletteBuildR1.log` confirms compilation; fresh image evidence remains required.
- `WalkNativeReferenceComparisonR2` remains visibly different (full MAE 26.82–43.46 /255). Do not report the kind-3/morph corrections as fixing this observed image difference: the compared hair includes kind-4 procedural private rigs. Shared native playback only computes their palette, but runtime GPU preview refresh read stale positions without calling skin_all. RefreshSkinnedDisplay now calls native Skin for both modes; OK_SKELMESH skips GPU body/head skinning while emitting the procedural private-rig vertices.
- `GpuPrivateRenderRefreshBuildR1.log` compiled but link failed with LNK1104 on the root plugin DLL. Read-only inspection found a newly running root editor (PID 35552); this is a file lock, not a visible Live Coding panel. That editor was not stopped. Use a fresh isolated plugin build and the owned audit host to verify the change without overwriting its loaded DLL.
- A later ordinary link retry succeeded (`GpuPrivateRenderRefreshBuildR2.log`); the lock had disappeared without agent termination. Final `GpuWalkPrivateRefreshR3` / `CpuWalkPrivateRefreshR3` both advance to .37 seconds and pass functional checks. Their viewed montages show the large stale procedural hair pose discrepancy removed. Full RGB MAE is .1147 / 1.0765 / .2331 for C001 / C002 / C050; eye ROI .1815 / .4787 / .4747. Alice's upper blue ornaments still differ, so this is close facial parity, not pixel identity/full asset acceptance. No frame-time claim is derived from these image runs.

### Binding metadata cache / formal evidence
- BindingCacheBeforeR1 baseline is contaminated by foreign UnrealEditor/rustc work. CPU controls drift 5.78%; exclude it from speedup acceptance. Never stop unrelated applications to manufacture a clean result.
- Code review exposed a pre-existing same-primitive-ID mesh replacement bug: registration skipped already known IDs even after asset/manifest binding changed. Registration now compares mesh/actor/part and resets palette metadata on rebind.
- Component count alone is insufficient for invalidation. Complete weak-object membership includes serial numbers, detecting same-count replacement and address reuse. Removed/replaced pose leaders detach callbacks.
- The first documentation patch used an absent heading anchor and failed atomically. Retried with an independent add-file patch; no code rollback or evidence deletion was required.
- BindingCacheFaceCycleR2 failed: population growth during near-face unloading exposed per-component previous-history ownership. 60,176 scalar mismatches (max 310.482719) triggered CPU fallback; this is rejected evidence. Component history now resets on first/reappearing render submission and asset rebind, while current matrices and continuous previous matrices retain .001 gates. New population resets diagnostic sampling so subsequent continuous history is checked rather than merely excluded. Fresh cycle verification is required.
- BindingComponentHistoryCycleR4 observed 30 actual GPU components with zero matrix mismatches after the history correction, but the owned process then exited before completing the control sequence. The log has no recorded fatal/normal exit and no dump was found. Cause remains unproven; this is not full acceptance. Harness early-exit errors now preserve the process exit code; retry uses a fresh tag.
- BindingComponentHistoryCycleR5 still fails at population growth (59,980 scalar mismatches, max 308.995842). Per-component history reset alone is not established as the cause/fix. Prior commentary was a hypothesis; it must not be reported as resolved. Diagnostic mismatch logs now identify current versus previous entries explicitly before further changes.

### BindingMismatchKindR6: current palette mismatch
Near/far verification failed with 59678 mismatches, maximum 293.295700073. The worst entry explicitly has previous=0; the history-only explanation was rejected. RestoreLowFace lacked the required-bone rebuild/evaluation used by FinishFaceLOD. Testing the symmetric refresh before accepting it as a fix. No performance or visual acceptance claimed.


Explicit pose-history edit first attempt failed before mutation because Windows Python default cp950 could not decode UTF-8. Use explicit utf-8-sig reads / utf-8 writes. R7 still failed previous palette verification (10790 errors); symmetric restore refresh alone is insufficient.


### ExplicitPoseHistoryR9
Development compiled (23.79 s); near/far test still failed 10557 previous-matrix scalar comparisons, max 2.862468719, immediately following face restoration. Evaluated-pose timestamps alone do not reset history when the required-bone layout changes. R10 explicitly clears native motion vectors and GPU prior-pose metadata at topology transitions. Also removed render-frame-gap exclusion: existing bindings retain strict prior-pose checks across skipped submissions. Await verification; no success claim.


### R10 resolution and evidence-reading error

Explicit prior/current evaluated clocks plus native/GPU motion-history reset on required-bone topology changes passed the formal OFF/ON/OFF near/far test. 164028 matrices including 3273 prior matrices, zero mismatches. Keep strict existing-binding checks across render gaps. Benchmark and CPU trace remain separate from diagnostic readback.

Reading UE report JSON as UTF-8 failed because this report has a UTF-16 BOM. Use BOM-aware PowerShell Get-Content/ConvertFrom-Json (or detect the BOM in Python); no runtime or source failure resulted.

### R12 trace export timebase
First CPU event export used 10-15 seconds but the trace anchor is 44.1756877 seconds in Insights' process timebase; export contained only a header. Derive bounded windows from CrowdWarmTraceAnchor, not relative recorder wall time. Re-exporting 54.1756877-59.1756877 as a fresh artifact; no empty statistics accepted.


### R14/R15: cache alternation and contaminated timing

One resident evaluator alternated prior/current clocks, defeating matching-clock reuse. Split current/history states with shared immutable GPU resources and private selection/output. R14 functional near/far OFF/ON/OFF passes 164028 matrices including 3273 prior, zero mismatches, max .000118255615234375, history immutable uploads zero. Below-60-FPS diagnostic reported zero reuses, so do not claim an observed dispatch reduction in this scene.

R15 CPU control drift 17.346% exceeds 5%; foreign OpenKoikatsu and UnrealEditor consumed significant CPU. Exclude performance acceptance and do not stop foreign processes. Turning off native CPU pose without complete visible/shadow-part palette and feature coverage would break near-detail followers; that shortcut was not applied. Native CPU ownership elimination remains outstanding.

### RuntimeBoneIdentityR16 compile dependency
ReferenceSkeleton.h exists in Engine/Public, but KKCrowdGpuPacking declared only Core/RenderCore/RHI and Projects. Adding reference-skeleton validation requires a private Engine module dependency; fixed Build.cs. This was a compile dependency failure, not a missing engine header. No failed binary accepted.


### Runtime manifest v2 byte budget consistency

The async loader still had an independent 16 MiB manifest cap after the identity format required a 64 MiB bound. Found before adoption; replaced with the shared public byte limit and aligned encoder/decoder. Keep one bound to avoid a full-crowd manifest passing serialization but failing loading. R18 real joint-loader and R19 scene tests pass. No per-card exception or CPU-free ownership claimed.

RuntimeBoneIdentityR19 passes 567099 matrices including 6969 prior, zero mismatch, max .00019073486328125, and runtime near bindings execute. Full native CPU ownership removal remains a separate uncompleted step. Do not equate added GPU component coverage with saved CPU work or 120 FPS.

### GPU comparison launcher controls

Initial launcher help text incorrectly suggested an Esc menu without source evidence. Source check confirmed WASD, right mouse, F8 and F9; removed the unsupported Esc claim before delivery. Launcher validation does not claim game execution or FPS acceptance. Do not launch the old stable CPU executable and assume it is the same build as the current GPU snapshot; F8 in the same GPU-launched process is the controlled comparison.

## R20 diagnosis: avoid disabling native updates before GPU ownership
- Native primitive provider dispatch depends on DirtyBoneTransforms and MeshObject revision updates. Disabling RefreshBoneTransforms or dynamic uploads alone can freeze rendered GPU geometry; do not claim this as CPU offload.
- Provider registration's boolean denotes skeleton batches, not asynchronous or unconditional updates.
- R20 GPU phase alone has CPU tracing; CPU controls drift about 7.8%. Treat frame numbers as diagnostics, not comparable performance evidence.
- Export thread name is `RenderThread 0`; exact matching `RenderThread` silently omits it. Use a prefix for aggregation and inspect thread inventory.
- One read command used the word `seventy` for Select-Object -First; PowerShell requires numeric 70. Corrected immediately; no file mutation occurred.
- Evidence and next ownership steps: Docs/CROWD_CPU_RENDER_COST_R20.md.

## R21 history borrowing validation
- An invocation used nonexistent -VerifyGpuFaceCycle. Read param block: correct flags are -VerifyGpu -FaceCycle. No test started from the bad invocation.
- rg with a wildcard in the directory argument fails on Windows; use the directory plus -g '*Oracle*.cpp'.
- R21 near/far functional run compared 585045 current matrices with zero mismatches, but native renderer reported zero previous matrices. The strict harness rejected it; do not count current-only comparison as history acceptance or relax the harness. Supplement with the GPU-only provider probe (R23).
- Reading the R21 receipt before checking exit status failed because a rejected test produces no final receipt. Inspect phase reports and console log after failure.
- Oracle build log suffix is `.build.log`, not `-build.log`; use the script's output basename when inspecting artifacts.
- R23 supplemental history verification passed without loosening checks: native previous matrices 10242; GPU-only probe previous matrices 14022; zero mismatches. R21 remains a rejected current-only run.
- R25 control drift passed but process monitoring found foreign rustc/cl/dotnet and SuperExplorer CPU work. Do not label it clean performance acceptance or infer speedup/regression from its OFF/ON/OFF numbers. No foreign process was terminated.

## R26 ownership preflight correction
- Whole-actor adoption must inspect all relevant parts and original material usage, not only the currently executed GPU bridge component count.
- Initial preflight treated a nonempty outer ExternalMorphSets array as active morph state. UE allocates per-LOD containers even with no sets; corrected in R27 to inspect each container and actual nonzero/nonfinite weights and curves. Do not use the initial R26 morph blocker count as asset evidence.
- Report hook initially used IndependentGpu instead of the actual member IndependentGpuRuntime; corrected before compilation.

## Per-card GPU shard export (R28-R30)
- ManifestSelectionOracle previously required at least two actors and indexed actor 1 in cross-actor corruption cases. Single-card export now has an explicit canonical-selection test and reports zero cross-actor rejection cases; it does not pretend to exercise the 16 multi-actor cases.
- Selection/rebasing can canonicalize reversed palette order, so raw bytes of the source are not necessarily equal to selected bytes. Test idempotence of canonical selection instead.
- R29 strict character check correctly rejected original selection Character.Character as the expected low-model identity. Runtime uses the profile-specific Character.Character. Preserve original selection and gpu_character separately; validate Library->Character against gpu_character. Explicit custom library input requires explicit gpu_character.
- Never mark a per-card offline index as runtime adopted or use offline cache-loader milliseconds as evidence that 100 actors enter the scene in under one minute.

## Selected-shard runtime integration R32-R35
- R32 loaded/composed two selected shards on a worker in 72.703 ms and current matrices passed, but initial capped diagnostic samples contained no native previous matrices. It remains rejected by the strict harness. R34 now continues diagnostic sampling until actual previous matrices have been compared; this does not change benchmark/runtime rendering or relax comparison checks.
- R33 rejection-test code used Pick.Add(Pick[0]); Unreal TArray prohibits adding an element by reference from its own storage. Copy the FString before Add. This was a test harness assertion, not a shard loader crash.
- R34 oracle build overlapped the Development build and UnrealBuildTool rejected the second instance with ConflictingInstance. Wait for the first build to complete before starting another Unreal build; game functional tests may run separately. Retry uses a fresh evidence tag (R35).
- R35 loader oracle report passed all eight rejection cases, but wrapper validation fell through into the synthetic pose validator because a broad text replacement split an elseif chain. Restored the exclusive ShardIndex / ManifestSynthetic / RealCards / Resident branches and reran with a fresh tag.
- R34 sustained sampling still exposed no native previous uploads for the two-card shard fixture, though GPU-only probe history passed. Source inspection found native uploads previous transforms on either UpdateMode or DirtyBoneTransforms::Previous; the diagnostic checked only UpdateMode. R36 now follows the actual native condition. This affects diagnostics only, not rendering, and the strict previous-matrix gate remains.
- R36 still has zero native previous comparison samples despite sustained sampling and matching native previous-upload flags. Current matrices and GPU-only probe history pass, but do not call this full native motion-history acceptance. Keep strict gate and record R36 as rejected; validate selected-shard loading/toggling separately without claiming GPU history verification.

## Selected material preparation R38–R39
- The old preparation script hardcoded the low walk library and omitted near-detail character assets. The generic preparation input now validates actual Library.Character identities and explicitly covers requested face/hair variants; do not assume a base low-model material manifest covers near-detail meshes.
- A report read guessed `.diff.json`; the image comparison script actually writes `.comparison.json`. Corrected the read; no mutation or test failure occurred.
- R39 original-material image comparison passed for one GPU-only HairShellSkin0 component (57 slots). Do not claim whole-character GPU face/hair acceptance from this: other parts remain native, even though their shader assets are now prepared and cooked.
- R39 frame mean 17.670 ms is a single-pilot fixed-phase 60 FPS-capped visual diagnostic. Never use it as the hundred-character benchmark or 120 FPS acceptance.

## Near-detail GPU-only probe R40–R44
- Probe originally required a baked manifest mesh entry. Near-detail meshes use validated runtime palettes; copy that palette into the probe binding and permit it only after the existing skeleton compatibility checks pass. Exact ProbeMesh selection must never silently pick another mesh.
- R41 full-body comparison passed for C015 FaceDetailR2/Part4, but R43 facial close-up rejected the front view (worst tile mean 14.2525). Matrix comparisons were zero mismatches. Retain the rejection: distant whole-frame similarity is not adequate face acceptance. Do not loosen image thresholds.
- PowerShell array expressions with concatenation require parentheses for each element: @('key='+value,'other='+value) joined the two arguments into one. Corrected before the close-up launch; use @((...),(...)).
- Two investigation searches repeated an invalid Windows wildcard directory argument; corrected to enumerate actual paths with rg --files or use a literal directory and -g. These produced no source changes.
- Three guessed engine source/shader paths were absent; locate them with rg --files first. InstancedSkinnedMeshSceneProxy is in Engine/Private; the component implementation is under Private/Components; the shared shader is GpuSkinVertexFactory.ush.
- R49 build rejected implicit uint8-to-bool conversion of FBox.IsValid (C4800 treated as error). Compare IsValid != 0 explicitly. Retry in fresh R50 evidence namespace.

## R55–R58 rigid accessory investigation
- Do not fix floating cuffs with per-card offsets or assume missing animation tracks. C015 native parts25/26/35/36 all have three affine tracks and move16.8/24.5cm.
- Importer recorded rigid inverse binds AFTER ApplyAnimFrame(0), although rigid vertices were placed BEFORE animation. Compare saved reference, skeleton rest, and actual palette; a self-consistency check against the same incorrect reference cannot detect this bug.
- R55 original left/right arm affine matrices differ from rest-socket animation by117.3096/110.3142; saved first-frame reference matches original palette. Fresh R57 repaired seven rigid parts, compressed matrix reconstruction max3.8147e-6, preserved all source assets and geometry. This is numerical evidence; visual acceptance remains required.
- R56 repair commandlet compilation failed because helper `Error` conflicted with an Unreal symbol. Rename to `MatrixDifference`; R57 build passed. Do not reuse generic engine-reserved names for namespace helpers.
- Several searches this turn guessed absent files. Discover exact paths using rg --files before reading; do not infer filenames from function names.
- R58 build is serialized behind an independently started OpenKoikatsuEditor build; do not interrupt or terminate that foreign build.
- New repaired clips use fresh package identities. Existing GPU animation caches must be regenerated; runtime current_clip_missing is an intentional fallback, never silently play stale palettes.
- R58 linker failed because this agent launched a read-only audit while its host rebuild was still queued behind another build; the audit locked the same plugin DLL. Never launch that Editor host until the queued build has completed. The audit exited normally and R59 retries only after no UnrealBuildTool process remains.
- R60 Cook reported success but COOKDIR discovery omitted newly created animation packages through the host mount. R61 uses explicit -PACKAGE animation-library requests and checks cooked output before making a Pak.
- R62 native-only Development capture failed at startup because its fresh stage omitted the required existing GPU global-shader patch. Even CPU mode must include shader permutations compiled into the current executable. R63 includes the verified ControlR2 shader patch; no renderer setting was changed to bypass the error.
- R63 numerical repair is NOT visually accepted: the previous floating cuffs disappeared rather than visibly following the arms. Do not batch or publish this candidate. A skeleton reference pose is not necessarily the card's original rigid placement pose; inspect actual native neutral placement and reduced/near merged geometry before selecting the migration inverse bind.
- R65 native-neutral audit disproved migration via shared skeleton rest. Shared rest excludes card-specific shape/ABMX placement: native neutral differs by10.85cm on arms and11.43cm on neck. Native neutral origins match original accessory geometry centers (arms X≈±20,Z130; neck Z137). R1 candidate is rejected; runtime now selects only WalkRigidBindingsR2. R2 reconstructs each outfit's actual neutral placement by restoring shape from its native animation snapshot with no animation, then corrects its own rigid palette. This preserves player card offsets and does not remap all accessories to wrist bones.

## R70 onward: new grouped geometry recipe
- User superseded the previous whole-person30k/hair10k recipe: preserve head/eyes/eye rims, body<=15000, clothes total<=15000, hair+all other accessories total<=30000. Budgets are triangles aggregated over parts, not per item. Small source meshes stay unchanged; do not subdivide just to consume budget. Keep the prior256px texture policy.
- R71 legacy authoring UHT manifest failed on another task's new AOKPaintSprayer class. Never strip unrelated classes to satisfy stale build metadata.
- R72 compile of the new recipe succeeded, but manual link failed LNK2005 because a stale unity object already contained quality functions. Do not combine hand-selected standalone objects with an unverified unity link snapshot. R73 uses UBT to gather current inputs with a fresh module suffix, preserving the loaded interactive Editor.
- PowerShell comma-containing ModuleWithSuffix must be passed as one quoted argument.
- R74 authoring failed because plugin manifests preloaded suffixed GPU shader DLLs while the new game module imported unsuffixed DLLs. This loaded duplicate shader registrations after shader initialization. Use an isolated authoring host with consistent unsuffixed dependency DLLs and private manifests; do not change renderer settings.
- R75 assumed the R73 hot-reload DLL still existed. It was absent; a later unsuffixed build existed and was verified to contain GameLowRoleBudgetR1 before copying into isolated R76. Do not assume generated binary names survive subsequent builds. R76 leaves workspace module manifests untouched.
- R76 pilot rejected part22 at walk sample5: linear source-triangle allocation starved a smaller head accessory beside an extremely dense accessory, and zero surface reserve discarded shape. New allocation caps small meshes at their original size, distributes remaining budget by square-root source size, and reserves part of each allocation for existing small-surface recovery. Aggregate limits and seven-pose audit remain enforced.
- R79 exposed that suffix builds also suffix dependency imports (KKCrowdGpuPacking-0002). An isolated host must copy the exact plugin module filenames from the build manifests, not assume unsuffixed imports. Copy private manifests and selected DLLs together from the completed build; never load both forms.
- R81 proved current UBT metadata points dependency0003 although the main DLL actually imports0002. The authoritative dependency is the PE import table. Authoring isolation now reads dumpbin /DEPENDENTS for the main DLL and selects those exact plugin DLL names, overriding mutable workspace manifests for directly imported modules.
- R82 geometry generation and seven-pose audit passed (C015 groups14920/13924/11288/22610), but process exit was1 due unrelated PostEngineInit KKCardImporterEditor loading the other plugin suffix. Do not treat an asset receipt alone as clean process success. The isolated authoring descriptor excludes this unused late editor importer; original project/plugin descriptors remain unchanged. R83 owned batch was stopped before regeneration to rerun on the corrected host.
- R88 native Development capture rejected: face/eye geometry was preserved numerically but eye layers and fringe were absent. Original parts use head/hair_f/hair_b roles; the near-face path supplied translucency sort and conservative bounds, while the new root path did not. Apply the same renderer-layer sorting/bounds generically, and repair all hair-prefixed roles. Reaudit existing generated cards rather than treating triangle equality as visual acceptance.
- R86 C010 rejected a328-triangle wrist accessory after reduction shrank its bounds. Shared group allocation now preserves original parts <=1024 triangles when their combined cost is at most half the group budget; remaining geometry still shares the same strict cap. C092 also failed small-surface coverage; it will be regenerated and reaudited rather than bypassing the check. Other completed assets remain reusable after hair reorientation.
- C092 source audit identifies the failed part as a2000-triangle accessory, not a face or dense hair mesh. Refine the generic small-part policy: preserve non-hair parts <=2048 and hair <=1024 in ascending size with non-hair priority, within half the group budget. This protects small authored ornaments while keeping strict budgets and leaves the remaining allocation for dense hair/accessories. No card index is used in the policy.
- R102 C092 remained rejected even when its2000-triangle accessory was preserved: material1 surface area became zero after duplicating/rebuilding a source mesh. This is not a justification to skip the material audit. Preserved copies now clear inherited LOD material remapping before rebuilding, matching the existing independent reduced-mesh path. Failure logging includes both source and generated mesh paths to distinguish remapping from reduction.
- R104 logging disproved the preserved-copy hypothesis for C092: the failing mesh was SKL_00164 (actually reduced), since the shared small-part cap was exhausted. The existing surface recovery only protected source areas below100cm², while the failure was310cm². The grouped recipe now explicitly enables recovery for all materially lost surfaces within its existing reserve. Other recipes retain their previous recovery policy; material area checks and strict caps remain unchanged.
- R106 source compilation and the main module link completed, but the aggregate build failed because a different Reader task held the workspace GPU plugin DLL. Do not terminate the other task. Verify the linked main module through a copied authoring host and use UBT -Module=OpenKoikatsu (confirmed in TargetDescriptor.cs) for bounded module validation.
- R107 surface recovery still produced only24.8% of the actual animated source area. The grouped reducer now captures evaluated component-space matrices from the source actor and reconstructs its imported render geometry, instead of assuming low-level clip extraction plus saved MeshDescription exactly matches the rendered source. A separate CanonicalEvaluatedR1 cache key prevents reuse of older canonical roots; matrix differences are logged. Bounds and surface checks are retained.
- R109 compilation rejected a conditional FString::Printf format string under UE compile-time format checking. Keep the format a literal and pass the cache flavor as a %s argument. No runtime assets were published by this failed compilation.
- R111 evaluated-pose extraction alone did not satisfy C092 surface coverage. Add bounded adaptive retention: the audit identifies the failed part, and the recipe preserves its authored geometry only when the actual aggregate group count remains within the requested cap, then repeats all seven poses. There is no card-specific exception or relaxed threshold. Dense failures that cannot fit the budget remain rejected.
- Adaptive candidate audit failures log warnings while retrying. Final audit thresholds still decide success; unrecoverable candidates return nonzero and are not published. Avoid logging candidate errors as fatal UE errors, which would turn a fully recovered commandlet run into exit1.

### R118 — partial iterative Cook pruned the preceding selection
Full generation receipt passed, but native startup rejected incomplete GameLowRoleBudgetR1. The second 8-card iterative Cook removed the first92 cooked cards; successful subset exits did not prove union completeness. Changed this profile to one bounded request with 100 explicit COOKDIR arguments, retaining only its selected gameplay directories. Verify all100 Character and Walk packages after Cook and stage, and reject incomplete native startup. R118 is rejected evidence.
R119 follow-up: COOKDIR must use the authoring project mounted Content path, even when it is a junction to the same physical source. Root workspace paths are outside the owned project mount and did not publish expected packages. Fixed mounted paths; retain final100-package checks.
R120 correction: engine source shows COOKDIR enumeration is disabled by SkipSoftReferences. Keep explicit packages and compact them using CookCommandlet-supported PACKAGE=A+B syntax, removing repeated switch overhead to fit one complete request. Do not disable SkipSoftReferences or pull original high assets into this release.

### R133 — shared small textures rejected by namespace-only admission
RigR2 correctly loaded100 cards with no original high meshes, but startup rejected6693 shared R1 textures despite dimensions <=256. The low-only guard recognized only the active variant directory. Added the exact R1 Resources directory to texture-only admission for RigR2; do not add it to permitted mesh roots or relax256px checks. R133 aborted before animation sampling and is rejected performance evidence. Rebuild native Development and restage before measuring.

### R140 — unrelated Rust test syntax blocked Editor synchronization
Native Development and visual/profile tests passed, but Editor prebuild failed parsing new pose.rs test literals .5 and .125 (Rust requires a leading integer part). Minimal repair:0.5 and0.125 only; preserve the rest of the concurrent pose work. R140 is failed Editor build evidence; retry separately. Source-discovery reminder: Windows rg file globs in path arguments are not expanded; use rg --files or explicit files / -g filters.
R142 retry: Result Succeeded; the Editor prebuild syntax error is resolved. Native Development release remains the independently tested R135 stage.

### R144 — private renderer revision API
GPU clock compile failed because UpdateBoneTransformRevisionNumber is private. Use public ForceMotionVector instead: UE queues its own revision/history update and marks native skin data dirty, allowing the independent provider to fill current/previous GPU slots without evaluating a CPU pose. Force each supported component, not only the leader, to retain independent previous-slot allocation. Do not change engine visibility or poke private fields. Clock mode rejects nonzero morphs/curves, physics/deformers/cloth/root motion and absent palettes, and uses clip-key-derived conservative bounds.
R145 compile: GetMorphTargetCurves returns TMap, which has no ContainsByPredicate. Use range iteration to check nonzero values; zero-valued curve entries do not require CPU animation. Keep nonfinite/nonzero guards.

### R148 — final GPU coverage and status were erased during teardown
The pilot executed1748 GPU render passes with one owned clock and zero CPU pose evaluations, but its final report showed ready=false and zero renderer coverage after releasing render resources. Preserve a complete pre-release statistics snapshot; never interpret released resources as a runtime backend failure. Also update the displayed status and ownership audit for actual clock adoption instead of the old hardcoded CPU-retained blockers. These pilot timings were capped60FPS and are not120FPS evidence.

### R148 / R153 — wall-clock screenshots cannot establish skin equivalence
Different-time viewport screenshots appeared to disagree on stockings. Added seven exact clip times and three identical non-temporal scene-capture views, switching the full native character between CPU and GPU matrices. R153 all21 pairs differ at most0.00191% of pixels by more than8 byte levels. Stocking/body intersections occur on both sides and remain the previously deferred geometry issue. Fixed-time captures must also record that GPU passes actually executed; do not accept identical pictures if both sides accidentally used CPU.

### R160 — make every backend rejection observable to clock ownership
Code review found the old CPU-retained bridge did not increment fallback counters for history-evaluation rejection or an unavailable/immediate-mode backend. After CPU ticks are disabled, that omission could leave frozen native fallback matrices indefinitely. Count these rejection paths, invalidate readiness, and restore clocks before testing readiness. A subsequent successful native GPU pass reestablishes readiness. This was found by review, not a reproduced100-card failure. Keep CPU fallback available whenever the backend cannot produce fresh matrices.

### R162 — adoption stall and overly broad per-component GPU bounds
Full100 functional CPU/GPU/CPU passed:100 clocks owned, zero CPU pose evaluations during ownership, zero GPU render fallback. Timing is rejected as speedup evidence: first GPU adoption scanned the entire pose-name catalog for every weighted native bone, causing a long initialization frame to enter the nominal warm sample window (mean42.112ms versus P9530.285ms). CPU controls also drifted26.02%. Replace repeated string scans with one per-actor FName map.

The same test increased GPU mean from1.97ms to12.48ms. Each tiny part incorrectly inherited the actor's largest conservative hair/garment radius, reducing culling efficiency. Keep the same mathematical all-key bound per part instead of applying its maximum to every component; retain original bounds and the face/hair visibility guard. This changes culling metadata, not geometry, materials, animation frequency or shader quality. R165 native build succeeded; repeat exact visuals and full100 profile before publishing the candidate.

### R172 — native culling parity is separate from pose equivalence
The first comparison that exercised actual GPU clock bounds failed the pixel gate (worst mean0.28628 byte levels,0.28420% of pixels over8 levels). GPU shows shoe geometry which the original CPU bounds omit; R149's native matrix verification passed, and the discrepancy follows bounds changes. Preserve R172 as rejected complete-runtime visual-equivalence evidence. For the matrix/shading diagnostic, use identical conservative bounds on both CPU and GPU and label this explicitly; do not claim this proves original CPU/GPU culling parity. Full native culling/temporal acceptance remains pending. Also reset the diagnostic ownership flag before evaluating each next CPU reference phase, otherwise the GPU-owned guard would skip that CPU reference update.

## R178 — palette audit counter contract

The batch oracle now executes one legacy job and two batched jobs per phase (18 job passes across six phases). Numerical/readback comparisons completed with no error, but the existing C++ audit still required six passes and marked the receipt failed. Updated C++ and PowerShell gates to require 18, with explicit batch/reset-history and invalid-scatter evidence. R178 stays rejected; rerun under a fresh tag. Numerical tolerances remain unchanged.

Documentation scripting on this Windows host must specify UTF-8 explicitly: default Python cp950 failed to read this MD. No document write occurred on that failed attempt.

## R183 — incorrect cache identity in visual launch

The first batch visual run used an unverified cache identity. Stopped only its verified staged game process; do not count this run. Read bytes 20..39 of the 40-byte cache header before passing the identity; correct pilot identity is 632257BEABBB0A45B66543C5206B49411F69B722. Use a fresh evidence tag.

## R184 — profile format and shadow warning

Native scene JSON is UTF-16 (BOM FF FE), while script receipts are UTF-8. A Python UTF-8-only diagnostic read failed; use BOM detection or PowerShell Get-Content for scene reports. This did not change the scene or benchmark. The GPU full100 run also emitted VSM Non-Nanite Marking Job Queue overflow with the pre-existing loose conservative animated bounds. Preserve that warning as an unresolved performance issue; do not reduce shadow quality to hide it.


## R185 — concurrent foreign Editor contaminates GPU A/B timing

Both functional runs pass, but CPU controls drift10.80% (R184) and7.93% (R185). R185 background receipt records foreign UnrealEditor PID3380 consuming718.72 CPU seconds across153.66 seconds (4.68 cores). Do not count the raw34.56ms versus21.30ms difference as clean speedup acceptance. Retain the traces, raw times and work-count reduction, and explicitly mark comparison performance acceptance false. Do not stop another task's Editor to force a benchmark pass. The VSM warning/loose animated bounds remain unresolved.

## R192 — redundant palette imports and quadratic clock membership

Batch alias validation imported two immutable per-part buffers on every frame, although only the combined catalog is consumed. Query FRDGBuilder::FindExternalBuffer instead; an unregistered resource cannot equal an already registered output. UE implementation keys the lookup by RHI buffer identity, preserving alias rejection. Ownership compared every saved part against the full lookup list; replace this quadratic scan with adoption-order comparison, with safe restoration/readoption on reorder.

## R196 — same component, replacement mesh stale bounds

Ownership stored component identity but not its adopted mesh asset. A registered mesh replacement could otherwise reuse old animation bounds. Save each adopted mesh identity and invalidate/recompute ownership on replacement, even with unchanged component count and clip. Dynamic physics, morph, cloth, LOD and deformer guards continue to run; do not cache these mutable states blindly.

A source lookup guessed RenderGraphBuilder.inl under Private and failed. rg located it under Public; use rg file discovery before naming an unverified include path.


## R198/R199 — control drift prevents speedup acceptance

New query/membership full100 and CPU restoration pass with no fallback, but CPU control drift is17.03% baseline and15.29% candidate. Preserve raw28.53ms/20.07ms frame measurements and CPU-only scope results; mark the comparison unaccepted for stable speedup/120FPS. Candidate retains the VSM marking queue overflow from loose bounds. Prepared-cache loading is not cold APNG conversion or first GPU adoption evidence. Final acceptance must separately exercise same-component live mesh replacement and moving viewport TAA history.

## R205 — influence-bound API compilation and file discovery

UE FVector3f has ContainsNaN, not IsFinite. R205 failed compilation; use ContainsNaN (checks nonfinite vector components), retain scalar FMath::IsFinite for computed radii, and rebuild under a fresh tag. Early source searches also passed Windows wildcards as literal rg paths and guessed nonexistent GPU header names. Use rg directory searches with -g and rg --files to discover headers first.


## R209–R218 — distinguish variable influences from missing CPU arrays

The initial reach helper accepted only constant-influence buffers. C015 reported11/38 tight parts; this alone does NOT establish Cook CPU-data stripping. R215 reason logging identified27 variable-influence parts (positions stride12, matching vertex/weight counts). R216 decodes the native lookup (offset = entry>>8, count = entry&255) and uses per-vertex count for the weight offset. R218 now covers38/38 and audits300,951 animated native vertices across7 phases. Do not substitute fixed stride/count for unlimited-influence meshes or assume section palette entries all affect vertices.

R210/R213/R216 native Development builds succeeded. R212/R215 remained partial and must not be cited as improvement. A source inspection used the word `seventy` as Select-Object's numeric -First argument; rerun with70. R219 test invocation used legacy Windows PowerShell's default ANSI decoding for a UTF-8 selection JSON and failed before launch; run the established harness using pwsh and explicitly decode UTF-8 selection files for compatibility. No game benchmark came from R219.


## R224/R225 — preserve structured launch arguments and bound profile output

The launch receipt stores `arguments` as an array. Applying string.replace to the array failed before launch; replace each observed argument separately and join the already quoted arguments only for the owned test launch. Do not infer a receipt schema. The first CPU-event summary filter searched complete rows and matched parent names across thousands of part scopes, producing excessive output. Filter exact timer names/thread names first, aggregate parent partitions, and print only top bounded rows. Render thread is named `RenderThread 0`, not exactly `RenderThread`; identify actual thread names from the CSV.


## R228 — merged eligibility traversal must guard dead components

Code review after merging live eligibility and adoption-membership checks found a potential null access if both saved and lookup weak component pointers had expired. Identity checking must short-circuit on the live Good result before reading the component's weight buffer or relative transform. Add Good&& to the identity expression and compile a fresh build; do not use the earlier snapshot for tests. Dynamic physics, morphs, deformers, registration and visible extra meshes must still be checked each frame; do not cache them merely to reduce timer time.


## R231–R236 — qualify CPU-scope wins separately from complete frame time

R231 single-pass eligibility reduced GPU Game Thread mean15.305→13.876ms versus R226, but frame mean17.197→18.312ms. Do not label this a whole-frame speedup. R233 CPU-only events show eligibility3.287→2.266ms per100 actors, independent tick6.256→5.289ms/frame; nested scopes are not additive. Implement an explicit runtime0/1 switch with the exact legacy two-pass/cloth query so same-process GPU0→GPU1→GPU0 evidence can decide the default. All dynamic state/identity checks remain live.

Native UE cached skeletal draw commands are already enabled (`r.SkeletalMesh.UseCachedMDCs=1`, `r.MeshDrawCommands.UseCachedCommands=1`); enabling them cannot be presented as new work. Do not set bRenderStatic for moving/deforming characters: it changes deformability/shadow semantics. A repeated rg lookup again passed Windows literal glob paths; use exact discovered files or directory + -g filters, never wildcard path arguments.


## R239/R240 — no-trace confirmation and default decision

Repeat the eligibility-only same-process0→1→0 run without trace because recording overhead was an unresolved confound. Control drift1.559%; fast GameThread saves1.044ms but complete frame17.644ms exceeds legacy17.005/17.270ms. Do not default-enable the candidate based solely on a nested CPU timer or report this as reaching120FPS. Keep explicit eligibility0 in the comparison launcher, expose1 for live comparison, retain exact functional and timing receipts. Source/build copies and profile/executable hashes match;0/1 launch validation passes.


## R241 full-palette memoization investigation

- The native USkeletalMeshComponent already caches local/world skeletal bounds. Do not add a second nominal bounds cache and call it a structural optimization.
- ForceMotionVector increments revision even when CPU pose is frozen. Do not remove it or key a frozen palette solely on revision; GPU temporal history still needs publication. Memoization compares owned exact numerical dependencies including current/previous input separately.
- SkeletalRenderGPUSkin.h is Engine/Private, not Engine/Public. One initial search used the wrong presumed header path; discover it with rg --files before reading. Unbounded git status also emitted unrelated generated files; use bounded/path-scoped status when reviewing this task.
- Cache entry replacement allocates fresh arrays so retained capacity does not silently exceed the32MiB payload budget. A pointer or hash selects a slot; exact dependency bytes, never pointer identity alone, authorize reuse. Verification mode recomputes the original formula and is excluded from speedup measurements.

- R244 independently verifies448,003 cached full palettes without bit differences. The first implementation copies entire comparison signatures every call and includes unused reference/visibility data; this adds work and has poor coverage. It is superseded by R245/R246: compare owned snapshots directly on hits, snapshot only numeric fields/indices read by the native formula, copy the matched prefix only after a miss. R247 verifies2,165,900 hits with0 mismatches. Verification mode still runs the original calculation and cannot be used as optimized-performance evidence.
- A Python documentation read omitted encoding on Windows and defaulted to cp950, failing on UTF8 punctuation. Use explicit encoding=utf-8 (or utf-8-sig for existing BOM files) for all project text; do not rely on platform locale. Re-ran the unfinished documentation edits with explicit UTF8.

- R248/R251 full-matrix cache is slower even without trace. CPU-only event export identifies37.3865ms/frame aggregate cache work across parallel workers versus21.8824ms/frame aggregate baseline MeshObject Update work (not wall frame times). Cache scope max calls can exceed14ms: bulk32MiB-budget cache clearing resets thousands of allocations on a single hot-path miss. Replace wholesale flush with bounded admission refusal; retain valid existing entries, calculate new inputs natively if they do not fit. This is an allocator-stability correction, not a proven speedup. Require a new build/profile before adoption.

- R258 bounded admission verifies4,791,110 hits with0 bit mismatches. R259 no-trace timings16.924/18.041/17.181ms still reject default adoption despite ~78.9% middle-report cache coverage. High hit rate and valid mathematical reuse do not imply a net performance gain; include input verification/bookkeeping and array copies. Keep the existing GPU launcher release and cache0.

## R262 runtime primitive grouping safeguards

Native FSkeletalMeshMerge can quantize high precision tangents and 16-bit bone weights. Use it only for layout; restore raw tangent/UV bytes and all influence weights, then audit topology, colors, private inverse binds and section materials. Retain translucent, morph, mouth-switch and cloth parts independently. Reject unsupported CPU payloads and influence counts rather than truncate them. Keep the original complete pose leader. Transient merged assets need reflected ownership until components are built.

Tooling error: Python pathlib.read_text without encoding failed on a UTF-8 BOM header under cp950. The preceding INL edit had succeeded; the header edit had not. Corrected with explicit utf-8-sig. Directory rg globs must use -g, not literal wildcard paths.

R269 grouping visual oracle initially failed pixel equivalence (~1.8 mean byte error); the freshly spawned original actor omitted hair/gloves/accessories. Its native affine-bone bounds lacked the baked whole-clip envelope used by both grouped CPU/GPU captures. Preserve failing captures. Assign independently derived per-part conservative GPU clip bounds and force native render uploads for the original oracle, then rerun; do not weaken pixel thresholds or accept missing parts. This diagnostic isolates geometry/shading; native viewport culling remains separate final acceptance.

R276 bounds-only correction did not restore missing original parts; that diagnosis was insufficient. The new original actor has never undergone an animation tick, unlike the warmed resident. SetPosition plus RefreshBoneTransforms alone cannot initialize its animation proxy. Use the existing native SetAnimationPosition path (TickAnimation(0), refresh, component-world update for all parts), rebuild and recompare. Preserve R269/R276 failures; no threshold relaxation.

R279 still fails original/grouped pictures despite zero-time animation ticking. The newly constructed original must follow the same post-construction rebind/initialization sequence as LoadNext; after leader allocation rebuild follower leader maps. Add per-part visibility/map/bone/bounds log evidence. Treat the initialization diagnosis as unproven until matching captures pass.

R281 normal separate-process original scene, warmed through the real LoadNext pipeline, restores every part and passes all21 matched-pose original/grouped comparisons. R279 groupedCPU vs warmed originalCPU: worst MAE0.010538/255, >8-byte pixels0.041962%; groupedGPU vs warmed originalCPU:0.010628/255 and0.042343%. Do not infer bounds or TickAnimation as proven causes of the fresh-actor failure. Remove the unnecessary in-capture actor constructor/rebind experiment (R280 was built but not run); use normal warmed scene references with --reference-prefix and verify character/clip/sample time identity. This avoids unverified initialization paths and extra actors in a renderer equivalence test.

Final grouping safety review: require identical native USkeleton owner to the original leader, reject independently mouth-hidden parts, and keep sections requesting runtime tangent recomputation separate. Native merge does not prove preservation of their duplicated-vertex/tangent adjacency buffers. Unsupported formats remain original components; preserve output part count on early rejection. These guards need a final rebuild and100-person/visual rerun before releasing a new grouped launcher stage.


## R295–R297: stale role allocation is not caught by schema/source-only reuse

Mash C004 original outfit0=189,619 triangles; R1/RigR2=60,700. Current allocator predicts original preservation for26 small parts which existing assets still simplify (Part_14:1102->372; historicalR84 generation target377). Same schema1016/source hash permits reuse across changed allocation policies, and later audit checks caps/head/deformation but not updated per-part preservation. Do not claim all100 reused assets were regenerated under the final policy. Include recipe revision/per-part preservation in freshness validation before future regeneration. Do not blame GPU/native mesh grouping: RigR2 audit preserves R1 geometry; DevelopmentR296 hasLOD0 and59,072 enabled triangles, exactly60,700 minus closed-mouth tooth/tongue1628;21 CPU/GPU image pairs pass. Tight-body surface clipping remains a distinct, unproven local cause. Investigation in Docs/CROWD_MASH_GEOMETRY_AUDIT.md.

Tooling during investigation: avoid recursive wholeSaved/Crowd file inventories (cooked trees generate huge output), literal wildcard rgpaths and guessed scripts. Use top-level Get-ChildItem and directory + -g filters; these diagnostics were corrected without asset changes.


## R298–R320: versioned role regeneration and validation tooling

New R3 schema1017 uses original source geometry, SHA1 source mesh/character/animation package fingerprints plus explicit policy revision, a separate output namespace, source-named hair accessory recognition, linear clothing weights with tight-layer priority,64-triangle maximum repair reserve and two bounded refill passes. R4 compacts R3 with a fingerprint of the source Character package and source recipe. Reject stale outputs; never silently treat schema/source-card-hash alone as a fresh recipe. Preservation checks apply to parts whose current allocation covers the authored mesh, not every small part unconditionally. The current policy spends at most one quarter of each group on preserving small parts; do not claim it restores all26 preservation mismatches under the prior policy.

R310 first native launch used a one-card selection fixture; the scene explicitly requires100 selection entries before selecting a pilot. Corrected R311 uses full selection plus PilotCard85. GPU exporter alone permits a one-card fixture. Preserve R310 failure; do not loosen scene selection validation.

R308 intentionally stale sidecar is correctly rejected by the native generator. The initial wrapper check used ambiguous Select-String positional arguments and failed while checking the expected rejection; finally restored the exact original bytes. Rechecked using explicit -LiteralPath/-Pattern and confirmed restored recipe equals R300 receipt; R308.json records this verification.

R316 control harness assumed .NET Core File.Move(source,dest,true), which does not exist under Windows PowerShell5. No game started. Use File.Replace on an existing control file and two-argument Move on its first publication; both retain atomic control updates. R317 reruns the functional CPU/GPU/CPU sequence.

Visual oracle deliberately retains CPU animation during matched-pose captures; R311 owned actor0 is expected and cannot be used as GPU ownership/FPS evidence. Separate normal R314 run verifies owned actor1, CPU pose evaluations0 and render fallbacks0. R314 single-pilot frame mean1.835ms/P952.248ms is not100-person120FPS acceptance, and concurrent authoring/background jobs rule out a controlled performance comparison.

Increasing triangle counts has not resolved thin garment/body intersections or all hair black speckles; R311 screenshots still show them. Do not mark full visual quality accepted from low-budget, deformation or CPU/GPU pixel gates.

R317 revealed an additional Windows PowerShell5 interop issue: null passed to File.Replace backup-path overload becomes an empty path and throws. R321 probes concrete backup paths successfully. Control publication now atomically replaces and retains a .previous file; R322 full CPU/GPU/CPU sequence passes after this correction. R317 failure is preserved.

## R328/R329: full-selection geometry Cook did not reach package roots
- R328 exited without a Cook log or character output; its cause was not established. Do not infer success from exit0 alone. Added log-presence checks and redirected stdout/stderr.
- R329 logged an exit0 Cook with only engine packages: repeated COOKDIR roots did not produce the requested R4 Characters in this invocation. Host source Character paths existed. The exact directory-filter cause remains unproven.
- Changed to one explicit PACKAGE root per selected Walk/Animations library. The library hard-references its selected Character, which references the low meshes/materials. This keeps the full100 command under the checked15500-character bound. R331 now enumerates21426 packages. Publication additionally requires every Character and Walk library plus the supplementary shader archive; final native validation is still required.

- R331 verification: .gpuanim.manifest is binary OKGM, not JSON/text. A diagnostic Get-Content read produced garbled output. Use header bytes/native loader and the JSON audit receipt; never decode this manifest as text. No artifacts were changed.

- R334 worker probe: standalone Benchmark default follows a walking NPC (player_control=false), whereas the CPU/GPU control harness locks the player's world position (player_control=true). These camera trajectories are not comparable; do not rank8/16 workers using those runs. Added an explicit PlayerControl benchmark option and configurable Workers in the same-scene toggle harness. Repeat with identical player-control/camera policy.

- R335 exposed a reporting bug: an intentionally stationary controlled player pauses its walking clip, so animated=99 with100 loaded/owned is correct. The report required animated=100 and rejected the scene. Added explicit player_animation_paused and animation_expected_active, exempting only the valid controlled player's intentionally paused clip. Other paused NPCs still fail. FPS eligibility safeguards and mean/P95 thresholds are retained. Rebuild and repeat native checks before publishing.

- R338 CPU control drift12.49% and R339 drift92.42% failed the5% comparison gate despite functional ownership/toggle success. Do not select their best GPU interval as stable performance evidence or attribute the drift to background activity without proof. R333 drift1.08% passed on the previous report-only binary. Preserve each run and capture the native CPU trace; no geometry/component-count doubling was observed in R339. An ad-hoc PowerShell foreach statement was also piped directly and failed parsing; use ForEach-Object or collect its output before piping.

- R340 showed the first idle-report correction was incomplete: UpdatePlayerMovement freezes the walk with SetPlayRate(0), leaving IsPlaying=true. Detect the explicitly stationary initialized controlled player, verify its actor/probe and zero play rate; IsPlaying alone cannot identify this pause. The initial correction did not pass native acceptance and was not published. Rebuild and verify the actual99/99 result.

- CPU summary CLI uses the events CSV as a positional argument, not --input; the guessed --input invocation was rejected. Re-ran with the documented positional argument and explicit bounded start/end plus all-timers. Check argparse usage before composing analysis commands.


## Behavior demo integration (2026-10-06)
- Do not classify an idle by animation index != 0: banks may put idle at index 0 and multiple walks elsewhere. Track the selected clip category explicitly.
- Do not reset NPC route time to global scene time after a pause; integrate per-resident speed so resume stays continuous.
- Fade procedural look yaw in after braking and out before restart, otherwise the whole actor snaps when entering/leaving Look.
- GPU ownership restoration must only restore clip time when the current animation asset is still the saved clip. Never overwrite a newly selected clip with the old clip time.
- PowerShell rg file globs such as Tools/*animation* are not expanded as paths; use rg Tools -g instead.
- Walk-only release banks cannot provide authored idle/emote animation. Report missing idle choices instead of claiming a paused walk is an authored standing animation.

- BehaviorCpuR353 was rejected before launch: grouped runtime palettes require a GPU cache even when testing CPU clock ownership. Use the existing CPU/GPU/CPU control harness with the same cache and grouped geometry rather than silently changing renderer configuration.

- A GPU bank missing a selected clip can retain its previous palette. The behavior selector must preflight exact character/clip/duration membership before selecting it in GPU mode; restoring CPU clock ownership alone does not prove render palette fallback. Unsupported clips remain unselected and are reported as missing idle choices.


## Authored behavior banks (2026-10-06)
- Old authoring snapshot R298 failed to load KKCrowdGpuPacking after external binaries/manifests changed (BehaviorClipsR358). Prepare a fresh immutable authoring host from current binaries; never rewrite running root Editor DLLs.
- Japanese clip f_aruki_* means walking; looking for only the English substring walk silently prevents choosing authored walking variants. Accept aruki too.
- Do not leave original high-mesh preview references in copied action sequences. Explicitly clear PreviewMesh, verify exact private skeleton identity, and point the new behavior library at the existing R4 character; otherwise initial low-only loading can pull high geometry through hard references.

- BehaviorSnapshotBuildR362 failed before compiling because KKUnityBundle rules require project-local Tools/ok-extract/build-release.cmd. Add the host Tools junction explicitly; do not alter plugin rules or build root binaries.
- GPU RealGpuPoseAudit currently reads Animations[0] only. A multi-clip library alone does not produce a multi-clip GPU bank. Add explicit per-clip audits and combine private clip tracks with strict actor/mesh/palette identity checks before adoption.
- Full card listing R360 took about five minutes (7,286 import zipmods), confirming that repeated full-source opening is unsuitable for batch animation baking. Use saved original animation snapshots plus exported raw clip curves, preserving card-specific shape and private rigs.

- BehaviorGpuBuildR366 failed because OKGpuAnimationBundle.h does not exist; bundle loading is declared in OKGpuAnimationManifest.h. Check actual exported headers before adding includes.

- SnapshotPilotR365 failed because duplicating the walking data model retains its populated rational frame rate; Unreal rejects changing that populated model to a non-factor idle rate. Create a fresh UKKImportedAnimSequence with the same skeleton/compression settings and initialize its model before setting the exact authored duration/sample rate. Preserve failed output in memory/logs; no animation asset was published.

- GPU clock adoption after a clip change must not use FinalizedPoseTime from the previous clip. Use the new single-node time until that clip has a finalized CPU pose.
- GPU owned clocks originally wrapped every clip with fmod. Preserve IsLooping: clamp non-looping clips and stop their mirrored playback at the endpoint; future one-shot actions must not silently loop.
- Python default cp950 cannot reliably read UTF8/BOM source files on this Windows host. Always pass encoding=utf-8-sig on reads and explicit UTF8 on writes.

## Runtime APNG boundary / long-idle admission — 2026-10-06
- Error: developing all-card Editor baked behavior libraries risked turning the fixed test corpus into the production pipeline. Player-selected APNG cards require runtime preparation. Do not require per-card Cook/SavePackage or a C### asset index. Keep offline tooling as comparison fixtures; runtime admission now accepts private banks in memory. APNG parsing, geometry creation and runtime playback metadata remain explicit outstanding work.
- R378 long-idle GPU matrix audit failed (0.00114369 > 0.001), even though vertex error was small. No tolerance relaxation or release adoption. Compact keys saved little; do not assume sparse TRS keys make 100 long private clips fit the current 2GiB budget.
- Runtime workers must not reuse the offline baker's process-global OK_NO_CLOTH_DYNAMICS toggle; concurrent sessions need explicit session-local policy. The current baker is single-process offline validation only.

## Native preparation isolation — 2026-10-06
- R386: native enabling legacy KKUnityBundle failed on GetExpressionCollection/GetEditorOnlyData/PostEditChange, UAnimSequence data model editing and an Editor-only KKCardReader test include. The complete legacy preview/import module is not a valid runtime dependency. Preserve it for authoring; isolate KKRuntimePose with the native snapshot/sampling backend. R387 then built native Development successfully.
- R388: the Editor test adapter failed to link the moved runtime sampler through a transitive module dependency. Add direct KKRuntimePose dependency to every module importing its symbols; R390 build passed. Do not maintain a duplicate sampler implementation in the legacy module.
- Native row sampling success does not prove original face/morph/material behavior, hand bind correctness or rendered CPU/GPU parity. Metadata fixtures are not a substitute for runtime metadata generation. No new 120 FPS or one-minute loading acceptance is claimed.
- Runtime source environment/dynamics policy remains a backend integration issue. The new worker does not mutate process-wide environment variables; enforce session-local low-cloth policy before concurrent production use rather than copying the offline baker's global toggle.

## Runtime GPU audit R391/R396: harness initialization errors (2026-10-06)

R391 omitted `-AllowCommandletRendering` and asserted on the absent global shader map. The audit now rejects that invocation before dispatch. R396 enabled rendering but reported an uninitialized/stale resident pose: `Frame==0` and `Frame++` appeared in different arguments of the same call, whose evaluation order can increment before selecting the initialization pointer. Use one immutable frame value for the call and increment only after success. These failed tests provide no GPU accuracy or performance evidence; rerun the unchanged 0.001 matrix gate after rebuilding.

## Runtime rig discovery R399/R402 initialization (2026-10-06)

Discovering palettes immediately from an animation snapshot rejected `o_body_a`: neutral/static refresh alone does not rebuild the shaped animation skeleton. `apply_anim_frame` also returns early with an empty shared animation root. Install shipped curve bytes and evaluate frame zero before discovering renderer palette slots; capture neutral rigid inverse binds before that evaluation. R404 then passed on C011, C002 and C000 without per-card fixture metadata. Failures now write machine-readable receipts as well as logs. Keep the actor-specific high/low render rig contract separate: discovery alone does not prove a rendered hand accessory or face is correct.

## Native GPU regression R406: matching shader Cook required

R406 never reached APNG preparation: the reference stage had an older global shader archive without `FOKSkinPaletteBatchCS`. This is a stage/launch error, not a pose comparison failure. Use the same current RoleFreshAllR333 cooked shader patch as the validated crowd launcher (`-pakdir=`), with a fresh output tag; never interpret NullRHI preparation success as native GPU shader availability.

## Native host helper R409 initial script edit

An unanchored text replacement changed `$taskPlugins` initialization instead of only the project's Plugins field, causing a duplicate hashtable key before invoking UBT. Fix both sites explicitly and run the complete helper; PowerShell syntax parsing alone does not catch this runtime type error. No binary or release was produced by the failed invocation.

## Native helper R410/R411 shared-workspace build failures

The helper's project Plugins field initially retained the original list instead of the runtime option list, allowing legacy Editor API dependencies. Assign that field explicitly. R411 also encountered a concurrently edited Rust test with invalid `.5` syntax. Before applying a one-token repair, the file changed and the guarded edit aborted without modifying it; R412 retries the current shared source. Preserve unrelated edits and verify a fresh binary after shared native-library changes.

## Runtime multi-clip regression R416 compile correction

`MakeShared` returns `TSharedRef`; its `Get()` exposes a reference rather than the pointer expected by the optional second-result argument. R416 failed compilation. Pass `&First.Get()` explicitly, retain the optional-null case, and rebuild before claiming multi-clip playback works. No player release was changed.

## Runtime cache guardrails (2026-10-06)

Do not identify a cached pose only by card/clip names: curve bytes, rig/palette layout and sampling recipe can change while names remain the same. The cache now requires the producer's versioned PreparationIdentity. Do not free an in-flight reservation immediately upon a cancellation request; release after joining/consuming the worker, otherwise replacement jobs can exceed the intended output-buffer bound. CPU key/track cache accounting excludes GPU upload copies and native-session/input memory; never report it as total process or GPU memory control. These are enforced identity/admission tests and lifecycle contracts, not a new frame-rate claim.


### R426 test summary completion guard

The helper previously derived summary completion only from the process exit and the probe passed flag. A later helper-only assertion could therefore throw while the summary still said complete. Require an explicit per-row gates_passed flag set only after all native, GPU, private-clock and cache assertions succeed. Cache acceptance additionally requires zero pending reservations. Do not interpret preparation-only completion as rendered scene acceptance.


### R427 GPU admission probe compile repair

The new render-command diagnostic instantiated FRDGBuilder while its headers provided only the forward declaration. Native Development compilation rejected the incomplete type. Include RenderGraphBuilder.h directly in the probe; do not rely on unity compilation or transitive includes. Rebuild and rerun actual APNG GPU readback before accepting the change.

GPU admission accounting is logical buffer accounting for this pose owner and one replacement overlap, not a global VRAM/fence manager. Releasing a pooled-buffer reference is not evidence that all queued GPU use or driver allocation has finished. Multiple owners, repeated replacements and deferred retirement require an aggregate manager/fence accounting before a total memory guarantee can be claimed.


### GPU cap compatibility / retained-pose verification

Keep the resident initializer default aligned with the existing aggregate bundle admission ceiling (2 GiB minus one), rather than silently imposing a new 512 MiB default on the old hundred-character path. Callers can supply a smaller explicit cap. A successful readback alone does not verify preserved pose contents: compare every retained row bitwise with the pre-rejection GPU output. This is a diagnostic assertion only and introduces no gameplay readback.


### R434 aggregate retirement implementation checks

Native compilation succeeded but emitted the Unreal first-header error because OKResidentGpuPose.h preceded OKCrowdRuntimePoseProbe.h. Restore the matching probe header as the first include. Never put Retire/Cancel side effects only inside check(): disabled checks in Shipping would omit the operation. Evaluate the operation first and then assert its result.

Retirement fences are written after the last executed graph and the manager remains owned until Collect observes completion. This path currently dispatches compute on the graphics queue; future async compute requires queue-specific last-use synchronization before the same retirement proof is valid. Gameplay Collect only polls once per entry and never waits/flushes. The diagnostic alone dispatches queued RHI commands and polls for completion with a timeout. Logical bank accounting reserves two rolling pose outputs, not every in-flight renderer allocation or physical driver VRAM.


### R435 pooled-buffer ownership compile repair

The new manager header stores TArray<TRefCountPtr<FRDGPooledBuffer>> by value; an implicit entry destructor needs the complete pooled-buffer type. A forward declaration compiled in unity but failed in the separate native pose probe translation unit. Include RenderGraphResources.h in the owning header rather than relying on consumer include order. Rebuild and rerun native GPU retirement tests.


### R440 transient file sharing / module callback lifetime

Writing the scene diagnostic briefly failed with PermissionError while the source retained ordinary Archive attributes. Read the current contents and retry the bounded edit; do not change ACLs, force attributes or terminate another process. Module frame-end callback captures its own shared budget handle, and callback removal runs on the render thread before module teardown; it does not race access to a moved global pointer.


### R444 hundred-character scene regression: shared-bank overcount

The new aggregate budget charged the history pose a second copy of immutable keys/tracks/tasks/binds even though InitializeShared only shares buffer references. In the actual hundred-character scene this rejected history initialization and caused repeated CPU fallback; R444 is a failed functional regression and is not accepted performance evidence. Do not increase the cap to mask the mistake.

ReserveShared charges only each owner's private output/selection capacity. A dependency links the shared lease to its original lease; even after the original GPU fence completes, its immutable charge remains until all dependent leases have retired and completed. Reacquire TMap pointers after Reserve because insertion can rehash. Regression adds an actual shared-history owner alongside two independent uploaded banks and checks complete fence collection.


### R448 dependent cancellation regression

The stronger unused-lease test failed before GPU evaluation: CancelUnused accepted an original lease with an active shared child. The intended guard edit had not matched the current source text, and an unchecked replace silently left it absent. Add the explicit Dependents rejection with an exact match-count assertion; rerun the test. Original leases must survive active dependent leases, including before upload.


### R454 receipt text decoding

The scene JSON written by Unreal has a UTF-16 BOM; reading it unconditionally as UTF-8-sig failed while writing the acceptance receipt. Inspect the BOM and decode UTF-16 when present, otherwise UTF-8-sig. APNG helper JSON remains UTF-8. The repaired receipt writer independently gates 100 GPU actors, ready backend, zero fallback and zero CPU pose instead of trusting the scene runner exit alone.


### R455 move-only async selection result

The diagnostic used auto Result = TFuture.Get(), which copies the future's const result reference. FOKGpuBundleLoadResult owns a TUniquePtr and cannot be copied. Retain the future and call Consume() to move its ownership. Gameplay must poll readiness before Consume; only this synchronous diagnostic waits for preparation. Clip-window identities hash selected content plus length-delimited character/clip and rig/mesh metadata, not a potentially zero source runtime manifest identity or ambiguous concatenated indices.


### R460 duplicate-ID diagnostic container alias

The malformed duplicate-selection test called Clips.Add(Clips[0]). Unreal rejects adding an element by reference from the same array because reallocation may invalidate it; the diagnostic asserted before producing a report. Copy the FString to a separate local value before Add. This is a test construction error, not permission to remove duplicate-ID validation. Keep the failed receipt incomplete and rerun all APNG tests after repair.


### R465 runtime scheduler cancellation guardrails

Cancellation is cooperative ownership cancellation, not a stopped native worker. Keep the running output reservation charged until IsReady/Consume; otherwise replacements can exceed capacity while abandoned workers still allocate. A selection generation change must discard old completions before cache publication and consumer delivery. Controlled futures now test both rules and ensure queued obsolete jobs never launch. Queue backpressure includes undelivered completed handles, which pin the cache; callers must Take/release results and retry deferred submission after Tick. Real APNG idle/walk preparations are submitted concurrently through separate native sessions. This is runtime background preparation, not proof of scene first-load60s or frame-rate improvement.


### R470 Windows ripgrep path glob

Passing Source/OpenKoikatsu/OKCrowd* directly as a path argument on Windows produced os error123. Search the directory and use rg -g filename filters instead of relying on shell expansion. No source modification resulted from the failed search.

### R470 render rig compaction binding guardrails

Only prune the output render-pose rig, never the native simulation snapshot. Exact world-space palette columns may contain private cloth/accessory/negative-scale results; retain all three columns, including negative signed/fallback slot identities. Optional native sockets require ancestor closure. Numeric bone indices must be remapped; keeping old parent or consumer indices silently binds geometry to the wrong pose. Validate exact retained keys against a full session before accepting savings. A compact bank alone does not establish rendered mesh/attachment equivalence until the runtime geometry consumer uses the same names/remap.


### R472 compact pose cache publication rejected

The first real compact-rig diagnostic failed: both compact results were rejected by cache Publish with identity-or-byte-bound error, while the full reference published. The preallocation estimate used logical array length plus256 bytes; this can undercount Unreal allocator quantization when almost every exact palette track remains animated and the key array does not shrink enough. Use DefaultCalculateSlackReserve with element size/alignment for both Keys and Tracks; explicitly Reserve Keys before SetNum so the admission estimate follows the same allocation path. Add allocated/reserved/identity values to the failure diagnostic. Preserve R472 as failed and rerun; no threshold relaxation or acceptance on the failed result.

R475 repair verification: all three actual APNG compact outputs now publish successfully under the quantized reservation estimate and pass full-reference retained-key bitwise comparison plus GPU matrix/retirement gates. R472 remains failed evidence.


### R477 matrix vector conversion compile error

FMatrix.TransformPosition/TransformVector return FVector4d; FVector3f has no direct constructor for that type. Convert explicitly to FVector (XYZ), normalize directions in3D, then convert to FVector3f. The first native mesh build failed before execution; preserve the log and rebuild.


### R483 source mesh binding acceptance boundary

GPU bone matrix readback followed by CPU vertex reconstruction is a binding audit, not actual vertex-shader skinning or rendered visual parity. Use the same initialization clip/time0 when comparing source posed positions; comparing an idle source with walk phase0 is invalid. Keep original triangle counts labelled as source geometry. Runtime materials/textures, topology-safe reduction and actual render-resource construction remain unfinished. Numeric remap now rejects duplicate targets and validates all vertices before mutating any; controlled tests ensure a late invalid vertex leaves earlier vertices unchanged.


### R485 transient mesh skeleton/tangent construction guardrails

Finish the FReferenceSkeletonModifier scope before copying its skeleton to USkeletalMesh; its finalization must not occur after the copy. Runtime triangle partitioning must key vertices by source renderer/index, not position, and keep every triangle even when sections split. UV vertical conversion does not by itself justify reversing authored tangent handedness: Unity(-X,Z,Y) -> UE has positive determinant and the existing source builder retains tangent W. Correct the earlier capture's extra minus before render-buffer acceptance. Tangent/normal rendered material parity is still a separate required gate.


### R488 variable skin buffer influence-order false rejection

The first real transient mesh resource diagnostic initialized position/skin GPU buffers but failed its positional influence-slot comparison. FSkinWeightVertexBuffer.operator= sorts variable influences by weight descending. Verify the exact global-bone/16-bit-weight map and total65535, including every nonzero stored influence, rather than assuming the input slot order survives. No skin fidelity relaxation: missing/changed/truncated entries still fail. Preserve R488 as incomplete and rerun. The runtime factory now also rejects a project configuration that truncates12-influence data; shader/feature support is a shared cook requirement, not a per-card asset or hardware tweak.

R491 verification of the R488 repair: all three real APNG transient meshes now pass exact global-bone/weight totals after variable influence sorting, with initialized RHI position/skin buffers and no triangle/UV change. Resource creation is not equivalent to production material rendering or a registered GPU animation provider. Keep those acceptance boundaries explicit.

### R495 budget allocation insufficiency / R498 corrected refill

The first runtime QEM implementation conservatively preserved opening vertices, skin-support transition vertices, disconnected components and invalid/non-manifold parts. All functional/skin tests passed, but per-part proportional allocation left group targets exceeded even where another part still had safe reduction capacity. Do not interpret reduction_verified or probe passed as budgets_met. R496 adds at most3 within-group refill rounds, consuming spare capacity only from parts which previously reached their allocation. Never relax topology/skin protection to force a budget.

R498 C011 now meets body15000/clothes14846/hair+accessory29984. C002 still has45241 hair/accessory triangles; C000 still body18005/clothes18726. Report these explicitly; do not label all cards as budget-compliant or silently discard invalid triangles. Retain the original part when manifold construction, endpoint identity or component-retention audits fail. Isolated/unindexed source vertices must not be counted as visible surface components. MinimalExistingVertexError can keep one mesh vertex ID while choosing the other endpoint's position: carry that endpoint's complete attributes, not the retained ID's old UV/weights. Source endpoint identity is not proof of animated/rendered visual parity.

### R500 source discovery command guardrail

Do not guess a source file path (KKRuntimePoseNative.cpp/material.rs did not exist). Use rg --files or symbol searches to locate the module/session source first. Windows wildcard paths passed directly to rg fail; use directory plus -g instead. These read failures did not modify runtime state; record them to avoid repeating discovery mistakes.

### R504/R505 deterministic preparation vs live clip-state contract

All3 texture resource diagnostics passed, but C000 overall GPU/source initialization binding failed (~7.76cm) twice. Its primary position mismatch was misleadingly overwritten by the later paused-clock check because the sample loop had already exited. Preserve the first sample error and only evaluate paused reuse after successful samples. R504/R505 remain incomplete evidence.

The native live ok_session_install_shared_clip now correctly retains private accessory particle state across behavior transitions. Offline source capture/snapshot reconstruction previously relied on that API resetting accessories. Do not revert live behavior continuity or relax the0.01cm binding gate. Add a separate ok_session_prepare_shared_clip for fresh offline initialization which resets private accessory particles, and use it consistently in KKRuntimePose source discovery/capture and bank sampling. This isolates deterministic preparation semantics from live playback. Reverify all3 APNGs after the repair before accepting the texture unit.

R508 and final R511 verification of the R504/R505 repair: all3 actual APNGs now pass complete texture/resource and prior GPU gates; C000 binding error returned to0.00230955cm, without relaxing any threshold or reverting live clip continuity. Offline preparation and live transitions must remain separate entry points. Exact mip count validation prevents repeated1x1 entries from reaching RHI. Pixel platform-buffer equality is not GPU texture readback or rendered material equivalence; do not claim eye/hair/lighting parity until shared shader integration and actual images are checked.

### R513 common material tool native build failure

The isolated material Editor host initially failed in KKRuntimePose module rules with only a generic native-extractor build error. A direct build-release.cmd succeeded immediately; no native source change was required. Do not guess the cause or discard other processes. Module rules now capture stdout/stderr asynchronously and include both on failure, preserving actionable diagnostics without pipe deadlock. Retry with fresh evidence after the build diagnostics repair. No material generation acceptance for R513.

R514 identified the R513 cause: Rust include_str! paths through the logical Tools junction escaped into the isolated host's Plugins instead of the real workspace. Earlier hosts happened to have legacy source schemas, masking the defect. Resolve the Tools junction to its physical target in KKRuntimePose module rules before invoking cargo and linking the native library. Do not copy legacy runtime modules or per-card assets into the material host as a workaround. Common shader generation also requires a valid compiled shader map with no compile errors before reporting success.

### R515 UE5.8 material authoring API compile corrections

UE5.8 FJsonObject.Values uses UTF8 keys, requiring explicit FString conversion before adding to FString-keyed maps. UMaterial.GetMaterialResource accepts EShaderPlatform (GMaxRHIShaderPlatform), not feature level. Read local declarations and existing tool calls; no cast between unrelated enums. R515 has no compiled-material acceptance.

### R517–R519：共用材質 Cook 工具快照檢查
- R517 在 Cook 前拒絕執行：KKRuntimeMaterial.h 已新增 MID 診斷欄位，但 Editor host 仍是前一版。沒有使用過期工具產生資源。
- R518 同步該標頭並以 WaitMutex 重新建置成功，再以 R519 執行 Cook。
- 通用防護：Cook 腳本比對 KKRuntimePose / KKCrowdGpuPacking 來源雜湊，遇到快照過期直接失敗；不可只複製來源後略過重建。

### R524–R526：材質來源欄位不可默認為有效
- 審查發現 GetIntegerField(schema) 及缺少 material 的預設路徑，會讓不完整資料進入解析或只套預設值。
- 改用 TryGetNumberField 並明確要求 material object；格式不合法返回錯誤，Out 保持原樣。
- 原生 Development R526 拒絕 oracle 與三張真實 APNG 回歸通過。不能將零 eligible 的卡片當作 shader 覆蓋：測試整體要求至少一個實際 MID，且所有未支援部件單獨計數。

### R528：材質家族與混合模式不得共用錯誤的父材質
- Forge MainAlpha 的 _OKNativeAlphaOpaque 決定 Masked / Translucent，不能將全部 Alpha 當作半透明或直接沿用 Opaque 父材質。
- 新增 runtime family identity，factory 同時檢查 schema、family、blend 與 two-sided 設定。各卡只建立 transient MID；父材質仍是共用資源。
- 發現上一版材質 factory 只檢查 schema / render state，無法排除同樣 Masked 但不同 shader layout 的父材質。新增 __OK_RuntimeMaterialFamily 並納入 shader 使用，避免 Cook 優化掉身分參數。
- 搜尋時猜測 KKForgeAlphaMaterial.cpp 路徑不存在；先使用 rg --files 得到實際 KKForgeAlpha.cpp 再讀取，不應依名稱猜測檔案。

### R536：共用材質擴充的防錯規則
- Cook / runtime 原先各維護父材質名稱與固定 3 個數量；新增家族時易漏載。改由單一 recipe catalog / parent-name API 供兩邊列舉，回歸檢查 catalog 身分與 family accounting。
- MainItem 的 native discard 不能再疊加 UE .5 opacity mask，否則將較薄透明邊錯誤剪掉；共同父材質保留來源程式 discard 與常數 1 mask。
- 當前單元驗證僅涵蓋 MID / 參數 / 貼圖與資源，不得當成實際渲染輪廓、陰影、像素對照或百人效能驗收。

### R543：頭髮 shader 與 UV 的通用防錯
- Forge main_hair / main_hair_front 必須使用各自的原始 Forward 程式與屬性預設值，不能用 Item shader 假裝支援。
- 已核對安裝 bundle 與缓存 DXBC 位元完全一致，再用既有指令轉譯器生成共用程式。MainHair raw0 / MainHairFront raw5 對應 native blob12 的 DIRECTIONAL，不能把 raw index / native blob index 混為一談，也不能把 front raw1 的 shadow variant 当成 Forward。
- 舊 SkeletalBuilder 為其他效果重排 UV channels，頭髮的 UV1 在舊網格是 channel2；新 runtime mesh 保留原始次序，因此 SecondaryUV 必須使用 channel1，禁止照抄舊 graph 的 channel2。
- 新路径僅支援來源確定的 Forge hair 及目前 masked/unlit 配置；Koikano、未知 shader / hair hint、其他混合設定保留 unsupported，不使用角色名猜測。
- 審查另外發現 runtime capture 的缺少 UV channel fallback 使用 Unreal(0,0)，再經 shader 的 V 翻轉會變成 Unity(0,1)，不是原始預設(0,0)。R546 將缺失 channel 的儲存值改成(0,1)，經相同轉換回到 Unity(0,0)。這是通用座標修正，不依角色或頭髮名稱特化。

### R551–R554：來源 shader 分類與 render state
- ShaderKind0 / ShaderKind3 不能代表同一套 shader。R553 的實際來源清單證實 kind0 包含 eye / eyew / nose / brow；kind3 包含 clothes opaque / item / alpha 與不同 blend。後續必須按 source shader + render state 分類。
- Studio Alpha 是 translucent、單面；舊共用 factory 一律要求 TwoSided=true，將無法正確描述這一族。新增 catalog blend / two-sided policy，同時供 Cook authoring 與 runtime factory 使用，避免同為 translucent 就套到錯誤的 Alpha 父材質。
- Python 初次讀取 R553 JSON 錯誤假設 UTF16；實際這批是 UTF8。後續先檢查 BOM，UTF16 BOM 用 UTF16，否則 UTF8-sig，不能依以前檔案的編碼推定。
- 讀取 Clothes 0000.asm.txt 前未先取得實際檔案清單，該檔不存在；修正為 rg --files 發現 DXBC 輸出清單，不推定每種 shader 都有同一索引的 disassembly。
- source StudioAlpha 雖然透明混合，但 zWrite=1；不能因為blend透明就推定不寫深度。生成器已加入原始 pass state assertion；UE Translucent 深度橋接尚未完成，必須保持 render-state/visual acceptance 未通過。不得把 MID 成功視為source pass完全等價。

## R561 眼白來源分析與工具錯誤

- PowerShell 不以反斜線跳脫雙引號；複雜 Python 編輯指令遭 ParserError，未執行。改用儲存 Python 腳本與 apply_patch，不再拼接雙重跳脫指令。
- raw DXBC 掃描順序不是 Unity m_BlobIndex，Forge/toon_eyew_lod0 raw0/raw8 都是 vertex。檢查 token stage 後使用 raw2 fragment，並驗證 CB[8,1] 和每個 texture/sampler 與原始 metadata 一致。不能沿用前一家 shader 的 index。
- `FKKRuntimeMaterialPart` 眼部欄位是 EyeDraw；實作前確認 header。新增 JSON 計數僅新增所需 field，避免複製整列導致重複 SetField。
- Forge 眼白原作 stencil Replace/ref2，目前共用 UE 材質只驗證混合/cull/depth-write 設定與參數綁定。禁止宣稱 stencil 或實際畫面相等；需後續渲染對照。

## R569–R570 虹膜輸入/UV錯誤避免

- 原runtime mesh直接捕捉V.uvs，遺失native EyeDraw的eye_sample_uvs。改用解析後採樣流並保留UV1/2，且要求對應material契約存在；不再在shader重複旋轉/高光ST。
- 虹膜PS register意義與通用材質不同。原作VS確認後建立專用input映射；不能因shader編譯成功就認定register意義正確。
- 只比CB尺寸不足以確認Unity blob對應。兩個眼部generator增加ShaderSubProgram table解析與byte equality；eye/eyew都是blob8，但raw掃描index分別0/2。
- UnityPy ShaderConverter沒有parse函數；檢查其ShaderProgram/ShaderSubProgram API後直接使用已存在的EndianBinaryReader，不猜函數名。
- Windows rg不接受把檔案wildcard直接放在未展開的path。用實際資料夾搭配 -g，先rg --files查實際header/source，避免猜不存在的Snapshot/ok_extract.h路徑。
- 本輪只補DIRECTIONAL配方/採樣與factory，stencil Replace/ref2、shadow variants、實際rendered CPU/GPU pixel parity尚未驗收，不宣稱五官已在正式場景修好。

## R577 統計紀錄更正

R568 docs逐卡分配誤寫C00240/C00024（眼白4/6），實際R567 summary是41/23（眼白5/5），總84正確。以完整summary.cards[].result解析而非依預估分配填寫；R577三卡數字均從原始JSON確認，記錄完整gates_passed與exit_code。診斷snapshot耗時不得當成FPS或百人載入驗收。

## R578–R579 Koikano解析/取樣風險

- Unity2019.4沒有m_KeywordIndices，採m_GlobalKeywordIndices+m_LocalKeywordIndices；初次沿用舊EC欄位遭KeyError。改為版本相容取得並與ShaderSubProgram實際keywords再次確認。
- 原translator只對t0 cube選xyz，其他texture都當xy，不能用在unity_ProbeVolumeSH或非t0 cubemap。現在逐一resource declaration辨識維度，拒收未知格式/未宣告sample，3D/Cube不做2D V反轉。
- 原translator不支援if/else/endif，不能刪除probe分支後冒稱原始shader完整。加入bitmask条件與巢狀配對檢查，保持原始DXBC程式；共同shader compile測驗涵蓋真實3D sampler型別。
- ProbeVolumeWorldToObject是場景volume矩陣，不是view矩陣。增加4row runtime binding與獨立alpha，預設identity；非有限matrix拒收且不改變原輸出。
- 原stage source的stencil、sceneSH L2和volume lighting仍需正式渲染驗證。本單元RHI存在不等於probe照明和CPU/GPU画面已驗收。

- R580 authoring build失敗C4456：matrix row局部Name隱藏外層material Name，UE視為錯誤。改用MatrixRowName，保留R580失敗log，新tag R581重新編譯；避免重用常見Name變數造成巢狀shadow warning。

- R581 log曾顯示另一個Build.bat正在執行；遵循WaitMutex等待，未停止其他程序。R580失敗紀錄保留，修正後R581/R583重新成功；不能把等待mutex當成我們的編譯/遊戲效能瓶頸。

## R588–R590 Koikano 虹膜輸入契約

- 初版 generator 假定 texture metadata sampler 必為非負；原作 unity_SpecCube1 為 -1，實際與 Cube0 共用 s0。改由完整 sample 指令恢復唯一 sampler，並驗證 metadata/使用一致，不建立 sampler -1。
- 初次動態執行分析腳本缺少 Tools import 路徑，發生 ModuleNotFoundError。改為明確設定 Tools 路徑和 generator 的 __file__，避免解析錯誤資源根目錄。
- Koikano main_eye 的高光 UV1/UV2 register 對應與 Forge 相反；由原作 vertex DXBC 確认映射，不能沿用另一 shader 的寄存器配置。
- runtime 主眼 UV 已旋轉，原作 expression 需要旋轉前 UV。新增第四 UV 流保存逆旋轉結果，僅在唯一 expression 指令前恢復該輸入；保留第三高光的旋轉與顏色設定。
- 發現預設 SpecCube0_HDR 原先會覆寫傳入場景參數；先設定預設再套場景覆寫，增加具體數值回歸。
- Forge 虹膜 expression 的原始 UV/視角位移仍需另行核對；本輪 Koikano 修正不能當成 Forge 或全場景畫面驗收。SH L2、眼部 stencil 與實際反射照明同樣待驗證。
- R592 初次讀取 build log 猜錯檔名；R594 初次複製相容 pak 同樣猜錯名稱，均為不存在路徑且未覆寫原資料。改先 rg --files 取得實際清單，再複製已存在的兩個相容 pak；延續禁止猜測產物名稱的規則。

## R597 Forge 鼻線來源核對

- 初版 nose generator 沿用 eye-white CB[8,1]，assert 失敗；實際 nose 只用 CB[4]、raw0/native blob8。檢查 disassembly 後固定來源 SHA256 與 CB[4]，不可沿用其他 shader 的大小或 raw index。
- 另行動態分析 vertex 時再遇 Tools import path 缺失；修正 sys.path 後原作 vertex 證實 o1.xy 直接傳 UV0，ST 由 fragment 處理。後續動態分析須使用含明確 Tools 路徑的固定模板，不能省略。
- 不能推定名稱沒有 lod0：EC 實際是 Shader Forge/toon_nose_lod0。Koikano/main_eyeb 不在本次查詢的 KKS/EC shader bundle，仍需找到其真實來源。
- Koikano/main_nose 為透明但 ZWriteOn，有 ShadowCaster；本輪 Forge nose ZWriteOff 配方不適用。以精確 shader/family/blend/draw 契約拒收，直到深度橋接完成，不因外觀相似共用錯誤 pass。
- 初次在 Saved/Crowd 递迴列 material 名稱產生大量 Cook 檔輸出；後續限定具體測試資料夾或根目錄，避免重複噪音。
- R598 build C2011/C2374：複製 EyeWhite 到 KoikanoEyeWhite 之間的模板範圍包含 ForgeEye，導致重複 struct/include。刪除第二份精確區塊並斷言只剩一份；以新 tag R599 重建。後續模板切片必須終止於該配方的最後 include，不使用相鄰家族作為邊界。
- 新增 native vertex UV provenance assert 時把 dcl_output 宣告也當作 UV 寫入而失敗；改只比對非 dcl 指令，仍要求唯一 mov o1.xy,v1.xyxx。宣告與執行指令需區分，不能刪除驗證。
- 初次以 Python 普通字串替換含正則 \b 的行，\b 被轉義成 backspace，替換未命中並有 SyntaxWarning；改用 apply_patch 精確修改該行。後续帶反斜線的原碼使用 raw string 或 patch，不依賴一般字串 escape。修正後 source generator 再次通過，保留失敗資訊。

## R606 眉毛來源與 Forge expression UV

- 找到 Koikano/main_eyeb 位於原作 abdata/chara/bo_head_00.unity3d，並非 MaterialEditor 的 EC/KKS shader bundle；原始透明 pass ZWrite=1、stencil Replace。現有 UE transparent parent 尚無 source depth/stencil 橋接，不能直接以普通透明配方冒稱完整還原眉毛。本輪先修已確認的 Forge 虹膜 expression UV，眉毛整合仍待完成。
- 曾猜 eye_draw.rs 路徑不存在；先 rg --files 找出實際 eye_gaze.rs，後續依實際檔名讀取。
- Forge expression 原作使用旋轉前 UV 加 view parallax；舊 runtime v1 已包含主旋轉，會使 expression 隨瞳孔轉。共用 UV3 保存逆主旋轉 input，僅替換原始 expression mad 的 v1 為 v7，保留原作 parallax 與 expression ST。
- 初次精確比對 instruction 使用 fxc 十進位 literal，precise_lines 實際採 DXBC 的十六進位 bit 值，assert 拒收；改以原始 0xbd75c28f 位元表示比對，不改成近似浮點或移除 source 檢查。
- 深入核對 EyeDraw.apply 後發現：Forge 先旋轉後 EyeLook ST，Koikano 先 texcoord/EyeLook ST 後旋轉。R590 的 Koikano expression 單純逆旋轉仍包含 EyeLook ST，並非原作 expression vertex varying。因此改新增不改動 OkMeshView ABI 的 native export，從 iris.raw_uvs 擷取 Forge 原始 UV、Koikano 只套 vertex _texcoord_ST；雙方 UV3 都由該通用路徑取得。R607 只包含初版逆旋轉方案，不用於驗收，R608 重建正確實作。
- 本輪曾猜 runtime native header/source 位於 Public/KKRuntimeNativeSession.cpp 等不存在路徑；實際為 Private/KKRuntimeNativeSession.h、Build.cs 連結 staticlib。後續先使用 rg --files 回傳的精確位置，不猜目錄。

## R615 原作 depth/stencil 光柵橋接

- 原作 stencil ref=2/完整mask不能直接覆寫 Unreal scene stencil 的保留位元；本單元僅在私有 depth/stencil target 驗證通用 RHI 狀態轉換，尚未接正式角色 mesh pass，不能宣稱眉毛/眼部 stencil 畫面已修復。
- Unity一般深度 LessEqual 到 Unreal reversed-Z 必須轉 GreaterEqual；stencil 比較不反轉。透明 alpha 的原作 SrcAlpha/OneMinusSrcAlpha 也用於 alpha channel，半透明紅在透明背景的 alpha 是0.25，不能誤用 UE 常見 separate-alpha 規則判成0.5。
- RHI 沒有對應 Unity SrcAlphaSaturate 的普通 factor，因此明確拒收，不能靜默轉Zero。非法 enum/mask/reference 在狀態提交前拒收，保留前一次輸出。
- 早期檢查猜錯 KKCrowdGpuPackingModule.cpp 和 Renderer/Private/ScreenRendering.h 路徑；以 rg --files 查實際 KKCrowdGpuPacking.cpp、Engine/Public/ScreenRendering.h。新 oracle 使用已存在引擎 OneColor shaders，沒有新增 runtime shader compilation。
- R616 build C2440：FBlendStateInitializerRHI::FRenderTarget 的 color write mask 是最後參數，初版誤放第一位。依本機 UE5.8 RHI.h 修正順序，新 tag R617 重建，保留失敗 log。

## R624–R627 source material mesh pass

- R625 C++ build failed: UE5.8 has no SetViewUniformBuffer on this pass state, and ComputeMeshFillMode/CullMode require material before override settings. Corrected against installed headers. Preserve R625 build log; do not treat this as a passed feature.
- R626 base material authoring reported success despite a custom mesh shader failing (GetMaterialOpacityMask is not an available generated-material function). Replaced with GetMaterialMask; added a render-thread check for the exact custom VS/PS pair before saving. R626 assets/receipt are excluded from acceptance.
- Cook source-snapshot validation previously omitted plugin Shaders. Include shader source hashes, so editing HLSL requires a fresh bake host snapshot.
- Diagnostic AddDrawDynamicMeshPass is for isolated verification only. It rebuilds commands and must not become the production per-frame crowd path; cached production submission and rendered character parity remain pending.
- Again attempted an incorrect Private/PrepareRuntimeSharedMaterial.cpp path; actual file is directly under Source/GpuPoseAudit. Use this recorded exact path on subsequent reads.
- R627/R628 newly added checks rejected the common material: all three vertex factories were registered and material existed, but the shader pairs were not ready. PostEditChange can begin async DDC shader-map lookup before compiler jobs are submitted. Waiting only on FinishAllCompilation was insufficient. Call the material resource FinishCompilation before inspecting its exact shader pairs; retain the strict failure gate.
- During API lookup used outdated Engine/Classes/Materials/Material.h and Engine/Public/MaterialShader.h paths. UE5.8 actual headers are Engine/Public/Materials/Material.h and Renderer/Public/MaterialShader.h. Use rg --files for relocated headers.
- R629 FinishCompilation alone still failed all exact shader-pair checks; async DDC waiting was not sufficient to make the required permutations available. Added an editor-only explicit CacheGivenTypes for the custom VS/PS and the three supported vertex factories. This is common-resource authoring, not runtime per-card compilation. Keep the rejected R629 log; the proposed synchronization cause is not considered proven.
- R630 assertion: CacheGivenTypes expects zipped VF/shader/pipeline arrays of equal lengths, not a Cartesian product. Removed that helper rather than bypassing checks; common authoring now uses UMaterial::CacheShaders(Synchronous) and FinishCompilation before exact permutation verification. No runtime path compiles shaders. The owned commandlet exited; no unrelated process was stopped.
- R631 synchronous common-material CacheShaders succeeded: all 36 exact custom VS/PS pairs available. The failure gate was retained throughout; R627–R630 are rejected evidence. Diagnostic submission preflights resource/shader/target validity and reports submission separately from rendered pixel verification.
- R637 native probe crashed before APNG reporting: old base Cook DefaultLightFunctionMaterial became incomplete because the custom shader ShouldCompilePermutation accepted every Unlit material, including engine defaults/light functions. Restricted to MD_Surface, non-special/non-default, Unlit, skeletal + instanced-skinned usage. This prevents unrelated engine shaders from acquiring the new requirement. R633/R637 are not native acceptance evidence; fresh common bake/Cook and native build required.

## R646 actual material pixel test implementation errors

- R646 C++ compiled but Editor link failed LNK2019/LNK1120 for UObject handle resolution. The packing module now uses Engine/UObject viewport/material APIs and must declare CoreUObject directly; added private dependency. Do not rely on transitive private dependencies.
- During source inspection guessed StaticMeshVertexBuffer.cpp under Engine/Private and passed a shell wildcard path to rg; actual file is Engine/Private/Rendering/StaticMeshVertexBuffer.cpp and InitFromDynamicVertex is in Engine/Private/StaticMesh.cpp. Use rg --files and directory roots instead of guessed/wildcard filenames.
- R652 actual draw reached mesh command setup but asserted PrimitiveSceneInfo: FMeshBatch defaults to scene primitive ID mode even for an isolated private-uniform-buffer mesh. Set fixture element PrimitiveIdMode=PrimID_ForceZero; the diagnostic VF does not support primitive ID streams and uses its private identity UB. Do not invent a scene proxy or borrow a crowd actor's GPUScene record. Native runtime exited without a report; R652 is failed evidence.
- Looked for Renderer/Private/MeshMaterialShader.cpp, which does not exist in this engine layout; query declarations/definitions from actual directories instead.
- R658 draw submitted and read back but the first material pixel was transparent instead of expected RGB/alpha. Exact shader availability alone did not catch this. Added explicit view/instanced-view uniform references to the source mesh RDG pass parameters; previously the pass declared only render targets. Re-run actual pixel checks after fresh common bake/Cook. R658 remains failed evidence; keep pixel tolerance unchanged.
- R664/R665 confirmed the view-uniform fix with actual drawing: all three native APNG probes pass four texel RGB/alpha comparisons within 1 byte/channel. Retain R658 failed evidence. The oracle-only VF uses existing local-VF HLSL, private primitive UB and ForceZero ID; it is not claimed as proof of animated GPU-skin visual parity or production performance.

## R666–R672 actual material depth/stencil controls

- No failed build or runtime test this unit. Keep source-alpha blend arithmetic: two layers of source alpha .5 over transparent have output alpha .375 (96/255), not .75. RGB reference allows 1 byte for UNORM quantization.
- A stencil rejection alone would pass even if stencil was never written. The positive Equal2 draw must add a second layer, while Equal3 must reject the third, so the test verifies write + accept + reject.
- Depth-write-on occlusion must be paired with a no-depth-write control using identical near/far mesh geometry; this proves the second draw was available and that depth write caused rejection. Use independent private targets to avoid state contamination; never overwrite engine scene stencil.
- R671 all three native APNG probes pass; no animation/scene/FPS acceptance is implied by this oracle. It remains one-shot diagnostics and never runs per frame in the player scene.

## R673 runtime scene integration
- Inspection guessed OKCrowdGameMode files and Engine/Public/Components/InstancedSkinnedMeshComponent.h. Actual implementations are OKCrowdScene and Engine/Classes/Components. Find paths with rg --files before reading; do not repeat guessed path searches.
- Cross-card texture reuse must key actual prepared mip bytes and explicit sampler/color settings, not only the PNG source hash. Shared textures are immutable; private materials retain them, and the cache holds weak references.
- R676 link failed because the isolated native host copies a source whitelist, not the whole Source directory. Add new runtime source/header to that whitelist before compiling. No success claimed for R676.
- The R670 patch directory is SourceMeshRasterR670.patch; guessed RuntimeSourceMeshRasterR670.patch was absent. Resolve existing evidence paths before launching tests.
- Windows rg literal paths containing '*' are not shell-expanded. Use directory roots and rg -g filters.
- R677 compile rejected ENQUEUE_RENDER_COMMAND after an unbraced if: the macro declares a tag before dispatch. Put the complete macro invocation inside braces.
- R680 source inspection failed on IBL's Unity 5.6 table: it uses 8-byte entries, while Koikano Unity 2019 uses 12. Keep version-specific table stride explicit. R679 also selected pixel type 17 for vertex stages; D3D11 vertex programs use type 15, so its absent vertex selections were not evidence of missing programs.
- Python text reads must specify encoding=utf8 for generated parsed JSON; Windows default cp950 fails on shader property labels.
- Remaining family generation initially rejected round_ne and the clothes immediate constant buffer. DXBC custom-data opcode 53 uses a following length word; recover class-3 table values from raw words, never rounded dumpbin text. Dynamic icb indexing and integer shifts preserve bit interpretation and five-bit shift counts. New regression checks cover exact 1/3 bits and signed/unsigned shifts.
- Missing family inspection is complete at R681. R680 remains failed due to its wrong IBL table stride. Do not conflate generated programs with production raster or visual acceptance.
- R682 common shader compilation failed on KoikanoSkin: VertexColor's default material output is RGB (float3), not RGBA. Append its explicit alpha output (index 4) before passing it to native float4 varyings. R682 assets are incomplete and must not be cooked or selected.
- Main-pass native discard must also execute for masked depth/shadow draws. Connecting a constant 1 OpacityMask bypassed the native program there. Feed the native custom program into OpacityMask and keep its returned mask 1 after its own discard, rather than adding an unintended .5 alpha threshold.
- R684 shader authoring failed on clothes MatrixV: Unity metadata declares 4 rows but DXBC trims its constant buffer to the 3 used rotation rows. Generate only the rows within the actual compiled CB extent, and reject unknown matrix semantics rather than treating every matrix as the view matrix.
- Source inspection found KoikanoNose also has unity_WorldToObject. It must use the current primitive's inverse transform, not camera rotation. Implement direction-vector basis conversion with the engine DFMultiplyVector/GetWorldToLocalDF helpers. Its selected DIRECTIONAL light has w=0, so translation is unused. Keep matrix semantics explicit.

## R692: runtime mesh identity rejected
The formal APNG room reached mesh/material creation but the manifest validator required /Game package paths. Runtime APNG meshes have no Cook package. Added a restricted /Runtime/APNG/ identity namespace (alphanumeric, hyphen, underscore suffix); identities are never passed to LoadObject. Retest with a fresh tag before accepting the room gate.


## R695: raw bones are not the renderer palette
The skeleton-batched provider assumed MaxBoneTransforms equals private reference skeleton count. UE BoneMap storage concatenates section bone maps instead. Use per-proxy ownership and map the actor palette through the actual render bone map, retaining private inverse binds and validating output count. Rebuild only when storage mode/map changes, never each frame. Also avoid guessed file paths and literal wildcards with rg; enumerate files first.


## R698: provider registration and proxy batching must agree
Changing renderer registration to primitive ownership alone left the component provider advertising skeleton batching. This produced no callback and no GPU-ready result. Set the runtime provider's UsesSkeletonBatching to false as well; preserve the existing diagnostic default. Add actual same-phase room captures and mean/P95 frame, GT, RT, RHI, GPU windows before claiming parity or speed.


## R702: unsupported IBL shader and incorrect selected pass
Three-card room rejected C002 because IBL was not admitted. Inspection found the generated IBL candidate was FORWARDADD rather than FORWARDBASE; it must not be used as base color. Regenerate from pass 1 blob 4. Original pass 0 is an opaque backface-culled color-mask-zero depth prepass without alpha clipping. Add an IBL-only runtime depth mesh/component on both CPU and GPU, with main pass disabled, rather than forcing translucent base material to write depth. Native screenshot and depth behavior are still required gates.


## R710: malformed PowerShell Pak response
Unparenthesized concatenations in an array collapsed two response rows into one; UnrealPak returned success but added only one file. Introduced a reusable common-only scene patch packager with explicitly parenthesized rows. Both matching global cache and bytecode archive must be packaged from the same Cook as the materials; do not mix a newer pose shader with an old global archive.


## R712: native scene image gate failed
Three APNGs reached GPU-ready in 5.20s, but comparison failed worst-tile thresholds (7.65-9.85 RGB). IBL depth showed black occlusion blocks; both modes showed missing iris layers. Do not treat low full-frame mean as face acceptance. Preserve authored render-queue ordering of merged sections. Also disable GPU pose evaluation during CPU reference mode; hidden components alone do not prove provider work is off. Replace the hardcoded cooked-character report with loaded UObject inspection.


## R720–R723: actual room depth differs despite pose oracle passing

- Source render-queue ordering corrected the Alice iris/eye layer. The GPU IBL ornament depth still leaves black silhouettes above the head.
- A 0.01 cm PixelDepthOffset experiment did not fix the image gate; it is not an accepted production fix. Compare the actual renderer palette against CPU bone matrices before further depth changes.
- R723 native three-card regression passed packed keys, clip windows, resident GPU evaluation and private actor selection. This is numerical evidence, not formal-room visual acceptance or 100-character FPS.
- File discovery must precede reads: use rg --files and directory searches with -g, never guess plugin subdirectories or pass literal Windows wildcards as paths.

## R726 / R728: cold room blockers measured

- R726 selected 100 APNGs with no private Cook imports. At the 60-second deadline only 22 completed, 15 failed, four were still preparing and 59 had not started. Completed-card snapshot mean 5231.2 ms, reduction 437.9 ms, render/pose wait 1081.7 ms, upload/commit 160.6 ms. Do not substitute synthetic 100-actor oracle results for this load gate.
- Unsupported shaders include IBL_Shader, Koikano hair_main_sun and item_glasses, xukmi/MainItemPlus, KKUSSitem and Forge toon_glasses_lod0 / main_StandardMDK_studio. C013 has a singular reference palette. Unsupported materials remain explicit failures rather than silent fallback.
- R728 read back 1980 actual room palette matrices: maximum CPU/GPU error 9.536743e-7, zero mismatches. IBL image gate still fails, localizing the next investigation to rendering/depth rather than pose.
- Removed duplicate snapshot reconstruction and shared clip initialization from source mesh capture by borrowing the already owned native session. CPU cross-clip blending now uses the same quaternion slerp as GPU.

## R732 / R734: capture validation and protected renderer update

- A pixel gate can pass while scene palette comparison fails. The scene test now independently rejects missing captures, nonempty capture_error or matrix mismatches, regardless of process exit code. R732 images pass only with depth disabled; its palette failure prevents acceptance.
- Direct SendRenderDynamicData_Concurrent is protected on USkinnedMeshComponent. Flush pending scene component updates through the public World SendAllEndOfFrameUpdates instead; do not rely on dirty pose state already having reached the renderer during a synchronous capture.

## R747: production VF requires scene / instance-culling uniforms

- The late source depth prototype crashed because a scene instanced skinned vertex factory requires the InstanceCulling static uniform buffer. Isolated oracle VF success does not prove native scene VF binding.
- Add the scene and instance-culling RDG uniform parameters to the pass; verify real scene geometry and CPU/GPU capture afterwards. Do not silently disable IBL depth.
- Animation envelope code must use unsigned track counts and float matrices with FVector3f input; implicit double FVector conversions are rejected by UE.

## R750: stale scene proxy / mesh-object pointers

- Runtime visibility/shadow changes recreate render state. Never retain raw PrimitiveSceneProxy or FSkeletalMeshObject pointers across frames for the late source pass or numerical checks. Resolve a stable primitive component ID on the render thread each frame and at the actual readback prerequisite.
- Stage snapshots now preserve the matching PDB for crash diagnosis. R750 crashed; it is not a visual or performance result.

## R757 runtime canonical-material array alias
Actual room reached Commit, then TArray asserted because Canonical.Add(Canonical[Match]) passes a reference into the same array being resized. Copy the int32 to a local before Add. This is a generic exact-binding material reuse fix, not a card exception. R754 separately failed during vendor D3D12 device initialization before loading the scene; that launch provides no functional or performance evidence. R755 compilation assumed an ABI rest_normals field that does not exist; use declared fields and verify the ABI first.

## R766 real room parity and R767 hundred-card deadline
R766 actual room (C002/C000/C011) passes front/side/back CPU/GPU image gates with depth enabled: 1,980 renderer skin matrices checked, zero mismatches, max error 9.536743e-7; late depth submissions 824. Section counts 54->46, 31->23, 43->31 without removing triangles. Readback uses stable component identity across proxy recreation. This supersedes earlier raw-pointer matrix evidence.
R767 cold hundred APNGs at 60,059.49ms: complete 63, failed16, preparing8, not started13. No <=60s success or hundred-card FPS claim. Additional installed families discovered: Koikano/hair_main_sun_front, Goo, Forge/main_item_ditherd, Forge/main_item_emission, KKUSS, Forge/toon_eyew_lod0, DA/Chara/Oil Painting, BetterMatcap. C013 source skin recovery still fails the exact source-geometry gate; do not silently replace it with identity. R768 confirms all new shader sources are available. Hair front base pixel DXBC is byte-identical to hair sun for all eight variants, so reusing the base programs is justified (source culling/outline remains separate). New DXBC translation supports truncation, signed comparisons, structured loops and indexable temporary arrays; 9 regression checks pass. Multiphase source materials still require production integration beyond base-family coverage.

## R775/R776 initial geometry and palette synchronization
Root cause of C013 rank-deficient binding failure: shared playback deliberately advances solved bone palettes without CPU skinning. Snapshot capture read old CPU vertex positions against a new animation palette. Refresh CPU geometry exactly once after initialization and before background mesh capture via the declared native skin binding; never restore per-frame CPU skinning. R776 C013 actual room passes all three CPU/GPU image views and actual renderer palette gate. R775 isolated one-clip probe hit a separate clip-window regression gate; it is not evidence against the successful scene capture. R770 launch incorrectly used the unstaged host executable (missing Global library); always snapshot/stage before launching GPU tests.

## R790 near-side source depth parity regression
At phase .137, distance110cm/focus150cm, three-card real room: 1,986 renderer matrices pass, cached commands audited from SceneInfo (3 GPU primitives /310 cached command records). Front/back image gates pass, side fails worst64px tile mean24.00: the small head accessory has a GPU black patch. Do not generalize R766 distant three-view parity to all near views. Diagnose source depth with a no-depth control; keep production depth enabled. The 5-second three-card observation was CPU frame mean2.804/P953.755ms and GPU mean2.307/P953.185ms; CPU/GPU RT2.794/2.306, RHI1.244/1.121ms. These are not hundred-card acceptance, and the visual gate failed.
R783 100-card retry at60,068ms completed70, failed2 and28 still pending. R786 at60,143ms completed59; concurrent Unreal activity means no clean performance comparison. Two remaining source issues: native Koikano/low_clothes_alpha (socks), and C041 accessory hair_r161.001 has explicit null source material. Do not disguise this as a supported authored shader. R788 finds low-clothes shader in native co_socks_00 bundle; base pixel programs differ from main-clothes, so generate actual low variants.


## R802 source depth near parity / R796 native material contract
- Near-camera .137 phase, depth enabled: VS WPO/raster isolation follows UE DepthOnly/BasePass invariant math; front/side/back all pass (RGB mean .471/.472/.469, worst tile .524/.511/.526). 1,980 actual GPU palette matrices, zero mismatch, max 1.073e-6. The extra pass remains enabled; no depth-disable workaround.
- R796 audit confirms C041 hair_r161.001 has no material in native KKS and APNG. Do not misdiagnose APNG loss or alias an unknown shader to a white material. Establish native missing-material semantics before handling it.
- R798 last20 APNG coverage found Shader Forge/main_item_studio on two cards. Add installed common shader program; no per-card material asset.
- Tool errors: PowerShell double-quoted Python escaping and guessed renderer source paths failed. Use apply_patch / literal here-string for edits; rg --files before reads; rg directory -g pattern instead of a literal Windows wildcard path.

## R805/R806/R810 retained source commands integration
- Renderer private header requires Renderer/Internal and Engine/Internal include paths; copy construction, not assignment, is required for the atomic-containing minimal pipeline initializer.
- R810 actual room crashed because the batched pass omitted Scene uniform buffer. Instance-culling BuildRenderingCommands does not populate Scene; explicitly bind GetSceneUniformBufferRef per current view, as in the prior diagnostic pass. Preserve actual room crash log; compilation alone never establishes draw pass safety.

## R820 / R823 renderer visibility root causes
- R813 Scene UB still null: InstanceCulling BuildRenderingCommands memzeros the whole draw params, so Scene must be assigned AFTER Build, not before.
- R816 retained batched pass near side failed because manual source-depth iteration did not honor per-view HiddenPrimitives/ShowOnlyPrimitives. CPU and GPU depth references were simultaneously submitted in isolated comparison views. R820 honors these lists and passes all3 near views, 1,986 actual matrices zero mismatch. No depth or quality reduction.
- R823 common prefab inspection proves the material-null mesh renderer is default Enabled=false with a null material. Off rows for sibling renderers must never enable it. Fix explicit Enabled=1 parsing and native selection; legacy APNG compatibility uses common prefab default metadata keyed by mesh/renderer IDs, not card names. Inspect all296 renderers in the common bundle.
- R824 Rust audit failed on private source model access; expose crate-only access, preserving serialized layout. Unicode CP950 console output failed after JSON was saved; print ASCII-safe summary.
- R820 three-card CPU frame mean2.344/P952.996ms, GPU2.318/P953.027ms (GT1.041/.940,RT2.341/2.313,RHI.938/1.026,GPU.901/1.018). Concurrent source audit was running: functional evidence only, not a clean hundred-card performance acceptance.

## R832/R833: invalid blended geometry and failed bank cleanup
- CPU/GPU parity alone is insufficient: both paths had exploded face/hair geometry during idle/walk crossfade. Exact affine-column helper bones encode a single column with quaternion twist that has no physical meaning. Interpolating that twist between independently prepared clips can deform the column. Helper tracks now carry an explicit column/axis mode; sample and transition interpolation operate on their column vectors, while ordinary bones retain quaternion interpolation. Runtime preparation identity bumped to v2.
- Hundred-card bank composition exceeded the 2 GiB byte budget. Keep the gate; measure and compact the actual payload rather than raising it blindly.
- Failed composition attempted to unregister a never-registered provider, causing an engine assertion and losing the report. Track registration on the render thread, unregister only successful registrations, and write the failed composition report before stopping.
- Regression gates: idle, walk, and intermediate blends must preserve sensible geometry and actual GPU matrix parity; 100-card failures must write evidence and exit without assertions.
- Avoid literal wildcard paths in Windows rg invocations; search directories with -g filters.

## R840/R848: transition geometry fixed; remaining native depth/color invariance
- R840 .5 idle/walk blend no longer explodes geometry; actual renderer matrices match (max1.53e-5), but near side image still fails (worst tile9.63). R842 with depth disabled passes all3 image gates. Disabling depth is diagnostic only.
- Compact affine keys store translation/time plus column vectors, with exact constant-column/payload reuse. No clip/character pose sharing and no frequency reduction. Persisted format version3 distinguishes column semantics; prior formats remain readable.
- Reusing the source color material for the depth component alone (R848) is insufficient: near side still fails9.46. Move native IBL depth and alpha color to the same mesh/material vertex permutation in a production post-opaque callback, before engine translucency. Remove IBL sections from the stock main pass only when that production route is enabled. Keep source alpha-zero depth behavior; do not add arbitrary depth bias.
- R849 first100-card native GPU-ready room: all100, no per-card Cook, zero provider failures. Load61,971.28ms fails<=60s. CPU frame mean43.796/P9549.120ms, GPU frame mean5.288/P956.657ms; GPU GT1.618,RT5.287,RHI2.109,GPU3.550ms. This camera/configuration result does not resolve the near side depth gate or full final visual acceptance.

## R856/R859/R862: source pass omitted dynamic CPU reference batches
- Post-opaque custom IBL depth/color failed near-side pixel gates (worst tile about74.5) although actual palette matrices matched. Full depth mesh and replacing cached color with dynamic submission did not fix it. Do not repeat these experiments as a solution.
- The collector inspected only PrimitiveSceneInfo.StaticMeshes. Conventional CPU skeletal components submit frame-owned View.DynamicMeshElements; those source sections were absent from the custom CPU pass. Collect eligible per-view dynamic batches too, without retaining their transient addresses in the persistent GPU command cache. Revalidate before claiming this explains every remaining difference.
- R863 moves private column-key packing into each card's owned background job, overlapping later card preparation. All100 GPU-ready in52,356.15ms, strict60s gate true, zero provider failures, no private Cook. CPU frame mean46.824/P9559.191ms; GPU5.522/P957.983ms, GT1.516/RT5.521/RHI1.874/GPU3.909ms. Near IBL visual gate remains open; this is not final full-quality120fps acceptance.
- R866 confirms the missing dynamic CPU batches were the near-side discrepancy: production depth enabled, blend.5 phase.137, near110cm; all3 image gates pass (worst tiles .539/.523/.523), actual matrix mismatches0/max1.53e-5. Geometry is sensible. Cached GPU color restored; CPU dynamic batches remain frame-owned. No instance-culling-manager/engine patch was necessary.

## R867/R868: repeat gates remain necessary
- R867100 cards all GPU-ready, no provider failures, but load71,506.60ms fails60s. GPU frame mean4.475/P955.507ms (GT1.501,RT4.474,RHI1.721,GPU3.597); CPU55.069/P9560.186ms. Preserve this slower load result alongside R863; one52s pass does not establish reliable load acceptance.
- Pure idle phase.73 R868/R871 still differs at head bows (side tile62.389), actual matrices match and face geometry stays sane. R866's profiled blend pass is not universal parity. Source capture instrumentation shows retained batches on both views, no dynamic candidates in this configuration; the dynamic-omission explanation is incomplete.
- R874 disabling skin cache on the custom-only depth asset gives identical failure; reverted. Do not repeat this workaround or claim skin-cache causality without evidence.
- Private packing reserved raw-sized key capacity even after compaction, and CPU references retained the raw duplicate. Shrink successful packed storage and retain one compact CPU key pool with per-clip track ranges, prepared in the owning background job. Verify sampling and load before promoting this change.
- R878 compact CPU sampling matches renderer matrices (max1.91e-6) and preserves sane geometry; legacy source draw difference remains. R881 enabling engine depth rendering and R884 refreshing CPU draw bindings do not change the mismatch; both diagnostic routes reverted/replaced.
- Common-render architecture evaluates CPU reference poses on CPU and scatter-uploads their actual skinning matrices, or evaluates GPU poses and palettes by compute. Both use the same instanced mesh/material/source passes; the CPU option must never silently run the GPU pose evaluator. R887 exposed that visibility=false removed the reference SceneInfo, so CPU upload failed explicitly. Reference components must remain registered/visible to the scene, with main/depth/shadow drawing disabled and AlwaysTickPoseAndRefreshBones, rather than disappear from the scene.

## R890/R893: scene-frame capture and numerical depth sensitivity
- The transform provider executes once per real scene frame. Switching CPU/GPU sources inside a single frame did not evaluate both paths (R890). Freeze playback, transforms and behavior, capture CPU, then capture GPU on the next real frame. Do not simulate frame counters or describe these captures as same-frame.
- R893 verifies 7,893 actual renderer matrices, zero mismatches, maximum error2.861e-6, and genuine CPU pose uploads. Near idle phase.73 still fails front/side/back image gates (worst tiles53.701/62.663/18.579). Common rendering alone does not resolve overlapping head accessory depth sensitivity; sensible face geometry and matrix parity do not establish pixel parity.
- R895 candidate samples affine helper column vectors directly into matrices for matrix composition or exactly identity native parents. Ordinary/nonidentity native hierarchy semantics remain unchanged. Explicit float32 lerp steps limit contraction differences. This is a candidate pending actual room verification, not accepted evidence.
- R894 common material author build failed because a Rayon pair closure captured raw session pointers, which are not Sync. After paired-frame validation, borrow the two distinct sessions exclusively before joining, matching the existing paired-frame pattern. Do not add unsafe Send/Sync implementations. R895 release/native build succeeds.
- R900 direct-column candidate does not fix near idle image gates (side62.252). The CPU capture was still armed in the same frame that first froze playback, potentially after the provider had already evaluated running playback. Add a preparation-only frame, then CPU capture, then GPU capture, preserving the frozen scene through both real frame boundaries. Matrix comparison validates the GPU against frozen CPU references; it cannot by itself prove the earlier CPU image consumed those references.
- CPU palette uploads now reject a BoneMap/count mismatch before deferred indexing; malformed provider input must fail explicitly rather than read past the palette map.

## R903/R904/R905: frozen-scene animated regression passes
- Preparation-only frame, CPU capture frame, GPU capture frame resolve the false near head-accessory difference. Production source depth remains enabled; no depth bias, hidden accessory or relaxed image thresholds.
- Three APNG runtime cards C002/C000/C011, near110cm/focus150cm: idle phase.73, blend.5 phase.137 (the original R832 exploding case), and walk phase.49 all pass front/side/back pixel gates. Worst64px tiles respectively .109/.065/.093 or less; each scene checks7,893 actual renderer matrices, mismatch0/maxerror0, CPUuploads2, providerfailures0.
- Inspected near side idle/blended screenshots: face and hair remain bounded and sensible, no giant planes. Pixel parity must still be accompanied by this geometry inspection.
- The direct-column candidate alone did not solve R900. The verified fix includes genuine frozen scene-frame scheduling, common renderer submission, and affine-column interpolation; do not attribute all observed parity to numerical shader changes.
- Tool invocation initially guessed RuntimeLoadR833 instead of the existing RuntimeHundredR833 selection directory. Locate the existing receipt with rg --files before reuse; failed invocation launched no game and generated no valid performance evidence.

## R906 hundred-card Development measurement
- All100 APNG characters GPU-ready in77,202.83ms; strict60s gate FAIL. No private Cook, providerfailures0. Extended diagnostic deadline does not turn this into a load pass.
- Same process/configuration CPU frame mean131.399/P95157.308ms (GT130.953,RT19.566,RHI5.426,GPU4.030); GPU mean7.058/P959.262ms (GT2.340,RT7.056,RHI2.722,GPU4.029). CPU and GPU now share rendering, so earlier conventional-CPU timing is not an identical render-path baseline.
- GPU average meets8.333ms but P95 does not; do not claim stable120FPS. No owned compiler/material author/oracle ran concurrently; external workload isolation is not established by this test.
- Source commands4 builds/8,712 reuses, actual cached main commands10,109. These are command cache records, not measured draw-call count.
- Load diagnostics per-character mean snapshot5,617ms, nativeopen1,902ms, materialcapture2,109ms, pose/renderwait3,422ms, meshcommit230ms. These stages overlap; do not sum them as scene load time or infer frame savings. Compaction alone did not establish reliable60s loading.
- A later read guessed a nonexistent runtime launcher despite a file inventory. Stop guessing paths; use returned inventory paths only. No game was launched by that failed read.

## R907/R908 concurrent immutable texture preparation
- The completed prepared-pixel cache did not coalesce concurrent first misses: multiple card workers could decode/resize identical PNGs at once. Add a byte-verified, sampler/color/mip-policy keyed in-flight result. The owner prepares synchronously; joining workers wait only in background. Decode failures publish errors and wake every waiter; no lock spans decoding/resizing.
- Bound retained in-flight encoded bytes to256MiB independently of the completed cache. Use actual reserved allocation slack in the admission check to prevent unsigned budget underflow. Hash collisions or capacity pressure prepare independently and cannot share wrong pixels.
- Reports now expose preparation cache hits, actual decode jobs and joined jobs. This implementation is pending native scene verification; counters alone are not proof of unchanged pixels or60s loading.
- A combined engine-header search included a guessed EventPool.h. Only Event.h from rg --files was actually needed; use its FEventRef manual-reset RAII API. No engine file was changed.
- R910 actual three-card transition regression passes front/side/back and actual matrices after concurrent texture coalescing. Preparation counters:189 decode jobs,24 completed-cache hits,3 joined in-flight jobs. New reports distinguish genuine joins from GPU upload reuse.
- Follow-up removes redundant per-channel full RGB sRGB conversion (one RGB conversion preserves independent channel output and original alpha floor), and releases each card's prepared mip copy after successful GPU texture creation/reuse. Transient texture BulkData and immutable shared prepared cache keep their own storage; source material parameters stay private. Native validation and repeat load are required before performance claims.
- R914 passes near transition all3 pixel/matrix gates. Compared with R910 GPU images, mean RGB difference<=.000310/255, no channel-error>10 pixels. No texture resolution/frequency reduction.
- R915 all100 runtime APNG ready59,897.94ms, strict60s pass, zero provider failures/no private Cook. GPU frame mean4.927/P956.005ms (GT1.447,RT4.926,RHI1.998,GPU3.574); CPU100.375/P95111.369ms. Shared preparation2,483 cachehits/4,296 actualdecodes/92joins. This configuration passes but load has only102ms margin; repeat required. CPU-window GPU timings14.464/P9521.214ms differ from the later GPU window and suggest nonstationary GPU work; do not attribute all cross-run improvements exclusively to texture coalescing.
- R916 removes duplicate same-actor CPU source-pass references only after exact skeleton order/parents/inverse-bind byte agreement. Main and source render meshes remain independent, consume one actor's private reference pose, and retain their own mapped render palettes. This is not cross-character pose sharing. Await actual palette and image gates before acceptance.
- R918 catches48 matrix-component mismatches/max14.36 despite identical skeleton/binds. CPU render matrix evaluation depends on ActiveBoneIndices; the stripped main asset excludes some source-only accessory bones. Skeleton/bind equality alone is not sufficient. Build the single CPU reference from the full-section source asset when it exists, then map both main/source palettes against it. Never silently ignore the failed palette gate or upload stale matrices for missing active bones.
- R921 full-section private reference passes near transition front/side/back and7,893 actual matrices with mismatch0/max0. R922 all100 ready56,244.52ms (strict60s pass), no provider failures/no private Cook. GPU frame mean5.449/P957.294ms (GT1.405,RT5.448,RHI2.033,GPU3.765); CPU108.827/P95122.413ms. Source commands4builds/11,472reuses. R915 andR922 are two passing configurations/windows; arbitrary-card long-session acceptance remains separate.
- R923 retains the original hundred-card selection player C000 at index0 and deterministically shuffles the other99 APNGs (seed923). This changes submission/preparation order, not the selected card cohort. Add actual hundred-card matrix/near image gates to the repeated load/frame measurement; do not label it a new random100-card sample from a larger library. The original exploding-card C002 near source-pass regression remains covered separately by R921; do not assume the hundred-card index0 matches the three-card index0.
- R923 load56,248.63ms PASS;215,643 actual matrices mismatch0/max.00012946, C000 near front/side/back pixel gates PASS, no provider failures/no private Cook. GPU frame mean12.879/P9512.990ms (GT3.009,RT12.629,RHI3.031,GPU4.019), CPU138.123/P95183.256ms. This order/layout test fails120FPS despite R922 passing; preserve it, do not cherry-pick the earlier5.449ms. Reordering changes which private meshes occupy scene positions and near-shadow eligibility, so it is not merely a byte-identical preparation-order benchmark. Trace RT before attributing the regression to a cause.
- R924 CPU/frame/GPU trace produced a complete100-card report and usable293MB trace, but exit status777003 fails the process gate. Local engine GenericPlatformCrashContext.h identifies777003 as CrashReporterCrashed. The game log records normal shutdown, no new crash XML was found in the inspected game crash directories; underlying shutdown/reporting cause is unresolved. Treat this as diagnostic data, never successful acceptance. Traced load78.635s and GPU mean8.769/P9512.060ms are instrumented, not a clean performance comparison.
- CPU-only bounded event export100–108s,914 scene render calls: UpdateAllPrimitiveSceneInfos inclusive1.341ms/render; Skinning inclusive.477/exclusive.458; UpdateInstanceProxyData inclusive.264/exclusive.216; transient allocation inclusive.335/exclusive.224; cached source BuildRenderingCommandsDeferred inclusive.163. Inclusive scopes overlap. RT WaitForTasks totals1,893ms over the interval under WaitUntilTasksComplete; RHI idle/task waits are not GPU work and cannot simply be added to frame time. ExportTimerStatistics mixes GPU timers even with thread filters; use all-timer CPU events for exclusive attribution.
- CSV analysis initially requested nonexistent ExclusiveSeconds/InclusiveSeconds columns; the exporter uses ExclusiveMs/InclusiveMs. Inspect the schema first. Comparison JSON is written after the test exits, so reading it before the running test finishes is premature.

- R927 first single-layout scatter cache passes 7,893 actual matrices and all three near CPU/GPU pixel gates, but 492 layout builds and zero reuses show it does not optimize the actual renderer. Do not report a performance win from successful pixels. UE current/history destinations may ping-pong; retain a bounded set of exact layouts and verify real reuse before acceptance.
- PowerShell rg with a wildcard embedded in a filesystem path produced an invalid path; use the directory plus -g filename glob. Python Windows default cp950 cannot read UTF-8 project docs; specify encoding=utf-8.

- R928 single-layout cache repeated100 original R923 placement: load88,294.86ms (strict60s FAIL), GPU mean7.292/P9510.164ms, RT7.290/RHI2.326ms; CPU147.083/P95169.642ms. Exit0, zero provider failures, 1,998 layout builds/zero reuse. The changed timings do not demonstrate a cache benefit.
- R931 CPU/frame-only trace still exits777003 after normal shutdown logs, although matrices mismatch0 and layout cache records6 builds/2,795 reuses. Removing the GPU channel does not fix the trace-exit failure. Complete the owned trace before engine teardown (pause channels/stop connection); verify a fresh trace exits0 before calling that lifecycle change successful. No engine source modification or ignored exit-status gate.

- R932 bounded exact-layout cache passes the native Development hundred-card scene:215,643 actual matrices/mismatch0 and near front/side/back CPU/GPU gates. Layout33 builds/2,562 reuses (98.7% retained). Exit0/provider failures0/no private Cook. GPU mean5.683/P957.716ms (GT1.428/RT5.681/RHI2.087/GPU3.815); CPU117.734/P95131.860ms. Load60,335.50ms fails strict60s, even though close. R928 omitted visual captures whereas R932 includes them; do not attribute their entire timing difference to the cache. A matching baseline/repetition remains required.

- R935 explicitly pauses trace channels and successfully stops the owned connection before shutdown (logged1), yet still exits777003. This experiment disproves that merely closing the trace connection repairs the shutdown issue. CPU/frame anchors export correctly; retain the trace as diagnostic-only and continue untraced functional/performance comparison. Do not suppress or rewrite the failed exit code.

- R936 matching baseline cancelled when the user accepted current performance and requested stopping optimization. Only its verified owned R926 executable was stopped; foreign processes were untouched. A cancelled run cannot support a before/after speedup claim.

- R938 runtime speech/action build failed C2181: UE_LOG used as an unbraced if branch before else. Logging macros may expand into compound statements; wrap the branch in braces before rebuilding. No failed binary was put into the verified APNG launcher.

- Launcher inventory command initially piped a PowerShell foreach statement directly, causing an empty pipe parser error. Use a pipeline with ForEach-Object or collect the foreach expression before piping. No files were changed by the failed inventory command.

## R942 urgent runtime presentation regression (2026-10-07)
- Speech/action work paused for user-reported global color differences and visible genital meshes on all women.
- Root cause: APNG scene camera omitted the original crowd camera's manual exposure, ToneCurveAmount=0, ExpandGamut=0 and bloom=0. CPU/GPU parity alone could not catch this shared output error.
- Root cause: native APNG snapshots intentionally retain genital assets, but runtime rig capture treated their presence as a display request. Preview explicitly hides o_dankon/o_dan_f/cm_o_dan00/cm_o_dan_f by default; runtime had omitted that policy.
- Fix: restore the existing crowd output settings; exclude these optional renderers before CPU/GPU scene mesh/material/palette construction, preserving the source snapshot. No card-name exceptions, no anatomy deletion, no mesh budget reduction.
- Verification pending: fresh Development build, native APNG hundred-character scene, three-view CPU/GPU comparisons and hidden-renderer logs. Do not mark passed before results.
- Investigation reminder: use directory + rg -g filename pattern on Windows, not wildcard path arguments. Check file inventories before guessed source paths.

## R941 runtime action/audio checkpoint
- Native Development three-card test exited 0. GPU/CPU 7,893 matrices: no mismatches; front/side/back comparisons passed for halfway idle-to-gesture blend.
- Independent local catalog admitted idle/walk/talk gesture. Actor 0 executed/completed one action and rendered 361,540 audio frames; peak RMS 0.626413. Actor 1 completed its independently scheduled action.
- This proves action playback and real audio output. Audio-driven mouth scalar is implemented; actual facial vertex lip deformation remains unfinished. Not hundred-character loading/performance acceptance.

- R942 build rejected C4456: new renderer Name shadowed existing nested palette bone Name. Renamed RendererName; UE warnings are errors, inspect enclosing scope names before adding locals.

### R943/R944/R945 verified urgent fix
- R943 native Development build succeeded; immutable R944 snapshot. R945 used the same shuffled 100 APNG cohort as R932, with common R895 materials/R897 shaders, no extra action/voice assets.
- All 100 completed, exit 0, GPU failures 0. Logs record 100 default-hidden o_dankon renderers before scene mesh construction. Optional source assets remain in the APNG/snapshot.
- Restored manual exposure 0, physical exposure off, film curve 0, gamut expansion 0, bloom 0. Inspected the new front GPU capture versus R932: pink hair/clothes and skin now use the existing crowd output transfer instead of the gray/desaturated film result. This is not a claim of exhaustive original-game material fidelity.
- Front/side/back CPU/GPU comparison passed; 214,503 matrices, 0 mismatches. Front/back exact pixels; side mean channel error 0.0000261/255, maximum 7/255.
- Selection to GPU-ready 59,469.885 ms. Short 1-second diagnostic GPU window (193 samples): frame mean 5.193 ms, P95 7.394 ms; GT 1.885, RT 5.192, RHI 1.989, GPU 3.542 ms. Not sustained FPS acceptance or an isolated performance improvement claim.
- CrowdRoomDevelopmentAPNGGPU.cmd now points to R944 through its launcher. Launcher validates R945 functional, visual and presentation receipts. Original selected APNG files were not rewritten. Existing running game needs reopening to use the new executable.

## R946 runtime speech morph implementation
- First native build rejected C2440: FMatrix::TransformVector returns FVector4; explicit FVector XYZ extraction is needed at both affine vector conversion boundaries before FVector3f. Corrected; no perspective divide or point translation.
- Current mouth implementation captures each APNG's native mouth pair/baseline and transforms raw deltas through its exact private skin/reference inverse. Body animation remains GPU; only speaking mouths interpolate a retained position span on CPU and upload it. This is a functional step, not GPU morph evaluation or a performance optimization claim.

- R950 live audio test: exit0, three private mouth streams each1,052 affected vertices; speaker rendered361,540 frames, mouth peak0.997, action completed. However2,094 mouth updates tracked uncapped scene ticks instead of the accepted60Hz animation cadence. Added per-actor60Hz update buckets, immediate gain/reset transitions, and measured action/speech overlap counter. Non-speakers upload no mouth vertices.

- R949/R951 controlled open/closed same-pose raster comparison: only mouth bbox(515,738)-(550,767) changed,396 nonzero RGB channels. Native private morph reference conversion did not stretch other face vertices. Both three-view CPU/GPU tests passed; real-audio R950 also passed. Mouth data is independent per APNG, not a shared face approximation.

## R955 hundred-action admission failure
- All100 characters prepared, but four extra clips per private rig exceeded the unchanged2GiB animation bank budget after67/100 actors. GPU was never ready; exit0 did not make this functional pass. Already existing exact within-actor track payload dedup was active; do not promise it as a new fix.
- Added an explicit generic NPC action preload budget (default1), distributing the common action library across NPCs while the controlled actor retains all admitted actions. This reduces work and residency; no source rig, clip, sample rate or mesh quality reduction. Requests for unadmitted actions remain explicit failures; arbitrary on-demand action streaming/eviction is still future work.
- Frozen visual oracle now uses each NPC's admitted local slot rather than indexing the controlled actor's longer catalog.

- R958 bounded-admission hundred test passes:100 ready59.286s, GPU frame mean4.971/P956.989ms over5s, all3views pass. Speaker has103 mouth updates (60Hz ceiling; actual CPU test window slower),361,540 audio frames, but action/speech overlap0. Initial asynchronous Vorbis setup delayed speech past the short gesture. Corrected coupled requests to start the admitted action only when audio is decoded/ready; T/Y use this coordinated API. Added configurable demo delay and GPU-overlap counter so actual GPU execution, not only an earlier CPU A/B window, is verified.

- A/B tester originally launched the demonstration in its CPU warmup window. Added explicit delay and `action_speech_gpu_overlap_seconds`; R962 must demonstrate coordinated speech+gesture while GpuMode=true before claiming live GPU success. Prior open-mouth GPU captures establish raster compatibility, not this live overlap.

## R960/R961/R962 verified runtime speaking action checkpoint
- R960 native Development build succeeded; immutable R961 snapshot. R962 uses100 APNGs, four common action sources, independent private catalogs: controlled actor6 clips (idle/walk+4 actions),99 NPCs3 clips (idle/walk+one distributed action).
- All100 GPU-ready, exit0, GPU failures0,214,503 actual skinning matrices mismatch0. Front/side/back CPU/GPU captures are pixel-exact. Inspected front capture: normal face/eyes/cloth/hair geometry, preserved R945 color output. Logs retain100 default-hidden genital renderers.
- Coordinated line was scheduled inside the GPU measurement window. Actor0 executed/completed its gesture; real audio rendered238,592 frames by reporting;268 mouth uploads, peak opening0.9963; action/speech overlap1.3336s, entirely in GpuMode=true. This validates real simultaneous GPU body animation, speech and mouth updates. Earlier R950 separately verified complete361,540-frame playback and silent-mouth restoration.
- Across NPCs+player82 action requests,76 completed before the frozen capture; remaining actions were still in progress, not failed. None reported an actor error.
- Five-second GPU diagnostic (1,127 samples): frame mean4.434ms/P955.631ms; GT1.530ms, RT4.433ms, RHI1.670ms, GPU3.415ms. This is not sustained120FPS full acceptance or an isolated before/after performance claim.
- Loading64,171.628ms FAILS strict60seconds (prior R95859,285.967ms passed one run). Preserve both evidence sets; do not treat one good load as stable compliance. Background on-demand action admission/eviction remains unfinished; this checkpoint bounds NPC upfront residency.
- Root CrowdRoomDevelopmentAPNGGPU.cmd now opens R961 with common action/voice manifests. T/Y coordinate action and speech after audio preparation. All resource hashes and R962 functional/presentation receipts are validated before launch; ValidateOnly and NoSpeechActions modes pass. No game was left running by these tests.
- Existing common Cook lacks optional default SoundClass/submix assets; UE logs creation of runtime stub submixes. Actual AudioDevice playback and rendered PCM frames were verified. Authoring a complete shared audio-settings Cook remains final packaging work, not a silent test pass for missing assets.

## R963 runtime action admission build
- Native Development host uses an explicit source allowlist. New `OKCrowdRuntimeStream.inl` was omitted, producing C1083; added it to the host builder before rebuilding. Do not treat compilation of the root project as proof of the isolated runtime host.
- A source patch write briefly failed; reread confirmed prior successful patches intact and retry succeeded. No source restore or unrelated edits removed.

## R969 persistent runtime GPU cache build
- UE TAtomic exposes operator+=, not FetchAdd. Corrected byte counters.
- Anonymous namespace globals in plugin unity compilation can shadow unrelated locals (Invalid). Scoped new cache internals under their own namespace.
- Scene reporting already named texture stats Cache; renamed disk-cache JSON to DiskCache.
- Cache inventory must match exactly 51 characters (pose- + 40 hex + .okgpc); corrected before execution so eviction counts entries.
- Packed result requires explicit request opt-in; legacy CPU probes retain raw layout. Scene envelopes use packed track accessors and affine-column scale length.
- Intermittent source file sharing violations: reread and retried boundedly, preserving current unrelated changes.

## R973-R985 persistent GPU cache regression and verification
- R973/R974 first restart produced zero cache hits and duplicated 12 pose files. Cause: serialized native snapshots include hash-map order, so their byte hash is unstable between processes. Do not use transient snapshot bytes as a persistent card identity.
- Fix: hash actual APNG content plus outfit and stable private-rig/curve recipe; verify APNG content before/after capture. Include shared/native/code versions. R977/R978: 12/12 hits across restart, 72,028,320 B unchanged, three exact CPU/GPU views.
- R979/R980 hundred-card test: cold 85.002 s (FAIL 60 s), warm 35.378 s, 304/304 cache hits, 2,133,499,248 B. Do not report warm-cache performance as uncached first-use compliance.
- R981 on-demand NPC stream: seven installs, zero errors; one still active at report explains six releases. Active/fading/audio-held banks must not be evicted prematurely.
- R982 deliberate payload corruption: invalid entry rejected and recomputed; 11 hits/one rebuild, no GPU failures.
- R983 32 MiB quota fixture: oldest two strict-owned files deleted, newest retained, unrelated files retained; strict abandoned temp cleaned. Do not recursively delete a user cache directory or include arbitrary files in eviction.
- R985 default directory / requested 1920x1080 / 100 APNGs: 30.294 s to GPU-ready, 307 hits/3 new background clips, no cache failures, 2,183,316,672 B managed disk data. GPU frame mean4.525/P955.469ms over5s, not sustained acceptance. 214,503 matrix comparisons with zero mismatches and three exact view comparisons.
- Root launcher switched to R976; ValidateOnly passes both normal and NoSpeechActions configurations. GPU and disk limits each default4GiB; quotas are independent. Cache capacity changes must not be mistaken for a frame performance improvement.

## R987 preparation investigation
- File discovery mistake: guessed KKRuntimeMaterialCapture.cpp and KKRuntimeNativeSession.cpp do not exist; actual implementations are KKRuntimeMaterial.cpp and inline KKRuntimeNativeSession.h. Use rg --files before naming implementation paths.
- PowerShell/rg Windows glob mistake: a literal wildcard pathname argument caused OS error123. Search the existing directory with rg filters instead.
- Cold R979 per-actor overlapping means: native open881ms, rig capture1116ms, mesh capture550ms, material capture2141ms, render mesh/pose wait5206ms. Do not sum across workers or interpret pose wait as pure native compute.
- R987 change retains task-private part buffers between samples; resets native pointer views/readiness each frame, computes exact signed/weighted palette matrices once per slot/sample for three axis tracks, precomputes rigid inverse bind once. Preserves 60Hz sampling/quaternion continuity and all private rigs.

## R989-R991 lossless palette preparation verification
R987 build succeeded; R989 cold and R990 warm three-card checks passed, 7,833 matrix comparisons zero mismatches and three exact CPU/GPU views. R991 old-vs-new full GPU cache payload hashes all12 match. Build-stamped cache identities changed; do not compare filenames to determine deformation equality. Compare validated payloads and rendered output. R990 has12hits/zero misses; 72,028,320B unchanged. Full hundred-card timing must be measured independently.

## R992-R999: quantify the limit of scratch reuse and remove duplicate native palette queries
R992 hundred-card cold run:85.288s, compared with R97985.002s. No overall loading speedup demonstrated. GPU short-window mean7.410/P959.635ms. R994 same-stage restart:30.295s, no GPU failure and three exact CPU/GPU views. Do not claim scratch reuse solved the cold-load target.

Native ffi.rs ok_part_skin_palette performs palette generation and full vertex-weight validation even for its size-only call. The previous C++ wrapper queried size then copied at every sample, doubling that work. R996 keeps the native checked copy, tries existing task-private capacity first, and falls back to size query + reallocation + checked copy if needed. Positive native counts must fit passed capacity; native API checks capacity before writing. No vertex validation or private physics was disabled. Buffers are actor/session-private, with native views refreshed each sample.

R996 Development build passed; R997 stage; R998 fresh three-card12poses:7,833 matrix comparisons zero mismatches and three exact CPU/GPU views. R999 old R977 vs new R998 payload hashes:12/12 bitwise identical. R1000 hundred-card cold-load test is the gate before public launcher integration.

## R1000 checked-capacity palette fast path
R996 build/R997 stage verified. R99912/12 full animation cache payloads bitwise match pre-optimization baseline. R1000 cold100APNGs54.583s vs R99285.288s (~36% shorter), all100ready/zero GPU errors/214,503 checked matrices zero mismatches. Three CPU/GPU views pass (side tiny raster error max7). Frame mean4.381/P955.319ms over5seconds; do not call this final sustained120FPS acceptance or guarantee arbitrary cards always load below60s. Cache311misses/310stored means one later background action was still unpublished at report; zero failures.

## R1002-R1003 final current-feature checkpoint
R1002 default-cache restart30.640s, frame mean4.454/P955.401ms,100ready/zero GPU errors/214,503 matrix comparisons zero mismatches; three CPU/GPU views exact.305cachehits/4newbackgroundclips/zero cache failures; two LRU evictions;4,291,608,672B below4GiB. Root launcher R997 integrated, ValidateOnly normal and NoSpeechActions pass. R1003 feature checkpoint passed; full sustained visible120FPS and worst-case arbitrary card startup are not established.

## R1004-R1006 dependency integration failure
Content-based cache fingerprint build R1004 succeeded, but R1006 exited before scene startup: Missing global shader FOKRuntimeGpuDeformCS permutation0. The plugin source now contains additional shared GPU shader registrations absent from the old R897 global Cook. A successful C++ build is not sufficient to launch against an arbitrary older global shader package. Keep the last verified R997 launcher while rebuilding the isolated Editor shader host and matching common global Cook; preserve existing workspace shader work rather than removing registrations or bypassing verification.

## R1010-R1013 content fingerprint evidence
Matching global shader Cook R1008 / patch R1009 resolves startup failure. R1010 and R1013 three-card GPU/CPU views pass. First rebuild showed0hits: binary string inspection confirms code fingerprint changed DB7841... to B53E227..., while native library SHA stayed D1F068.... GPU deform source/header and test source changed during this interval (timestamps03:53:47/03:54:36), so this is expected content invalidation, not an unchanged-code test. Repeat after capturing both native and source fingerprints; do not call0hits stable-cache reuse.

## R1014-R1025 simultaneous source changes / stable code-version checks
C++ fingerprint stayedB53E227... across actual recompiles; native library changed fromD1F068... to6CC760... because ffi/session sources changed during checks. This legitimately invalidates previous cache entries. Never describe such a run as an unchanged-native rebuild test. R1017 normal restart12/12hits with no writes; R1024 old vs new native payload12/12bitwise identical. R1018 immutable executable used for functional hundred-card test, rather than continuously replacing the tested executable with new host builds. Its matching common shader patch isR1009. No performance claim for R1025 while other compilation runs.

## R1025-R1027 diagnostic report timing
R1025 initial100poses303misses/303stored; zero GPU/cache failures. R1026 restart303/303hits, zero misses/writes, 4,293,601,488B within4GiB;100ready/214,503matrixcomparisons zero mismatches; three CPU/GPU views pass. Both no-profile capture-only runs report soon after2seconds and exit before asynchronous voice starts; mouth/audio counters0 cannot validate speech. R1027 uses the existing1second timing mode solely to keep observation alive for12seconds, requests speech at1second; timings are explicitly excluded from performance evidence while native compilation runs. Do not change production speech behavior to work around a prematurely ending test.

## R1027 demo scheduling gate
Extended observation confirms real audio361,540frames and242mouth updates, but GPU action/speech overlap is0 because the profile harness runs its CPU phase first and the demonstration at1second finishes there. Keep the functional gate failed; this is not GPU action verification. Use the established5second profile observation and demo at15seconds to overlap the GPU phase; exclude its timings while concurrent compilation runs. Public launcher remainsR997 until this gate passes.

## R1028 real admission synchronization failure
R1028 GPU speech succeeds (237,568 real frames,263 mouth updates,1.336s GPU action/speech overlap), but full-scene matrix gate fails:16,321 of214,503 matrices mismatch, max80.134. Do not ignore this as raster noise or accept the build. Root cause in streaming admission: render upload immediately set Stream/StreamActor before the game-thread acknowledgement replaced clip metadata and CPU keys. Frozen comparison can begin in that boundary, then suspend ServiceStream acknowledgement while the GPU already uses the new bank. This also risks a one-frame wrong runtime pose.

R1029 fix separates upload readiness from activation. Game thread switches private clip catalog/CPU keys before queuing render activation. If the slot becomes active meanwhile, wait for inactivity rather than repeatedly failing replacement. If a pending request expires/changes after upload, discard the unactivated pose and retire its scratch lease. Native rigs, sampled animation data and quality are unchanged. Verify with an unadmitted streamed action active during full-scene CPU/GPU capture, plus automatic NPC behavior, before replacing the launcher.

## R1031-R1032 admission fix verification
Unadmitted NPC1 action/private-bank frozen capture passes inR1031:7,833matrices zero mismatches, one install/release/zero stream errors, actual GPU voice/action overlap1.3346s. R1032 automatic100APNGs:7stream installs/zero stream errors,214,503matrices zero mismatches, three views pass,263mouthupdates/238,592audioframes/1.3347s GPU overlap. Old-generation cache eviction remains within4GiB with zero failures. R1028 mismatch must not be counted as a passed comparison; only the acknowledged-activation build R1030 is eligible for new integration.

## R1033-R1034 final integration checkpoint
R1033 confirms100characters, three exact views and214,503matrices zero mismatches after acknowledged stream activation.267mouthupdates/238,592audioframes/1.3337s actual GPU overlap.305cachehits/zero failures;4GiB eviction holds. R1030 public launcher now includes matching common R1009 global shaders and content-versioned cache, ValidateOnly normal/NoSpeechActions pass. R1034 checks runtime feature gates and post-exit managed-file capacity. Current timing evidence excluded; do not present concurrent-build timings as fresh120FPS measurements.

## R1035 common audio Cook corrections
- BaseEngine.ini still names legacy /Engine/EngineSounds/MasterSubmix, but this UE install has no such source asset. Require the actual Master SoundClass and configured Submixes/Master*Default assets; do not invent legacy packages.
- Reusing IndependentGpuPoseHost failed before Cook because engine BuildId changed and plugin DLL manifests were stale. R1035 log retained. Common engine audio needs no project modules: R1036 uses a fresh content-only isolated host with engine plugins disabled, avoiding plugin rebuild/version coupling.
- PowerShell does not expand wildcard file arguments passed to rg. Use rg -g filters against directory paths. Verify script paths with rg --files before reading guessed names.

R1036 recovery passed: isolated content-only host Cook,73,029B common audio pak. R1038 confirms audio settings load with zero missing SoundClass/submix/stub errors and actual audio/mouth/action overlap. Audio verifier rejects unchanged old R1031 logs (expected negative gate); launcher updated only after positive gate. No card-specific resources or pose-data changes.

## R1040 speech interruption and failure lifetime
Inspection found resume-behavior only cleared PendingSpeechAction; its decode future could still publish voice later. Stream admission failures also cleared the action link but retained speech preparation, allowing an uncoordinated line. No-audio-device completion could leave the action link holding GPU overlays indefinitely.
Plan/fix: per-character nonblocking CancelSpeech drops the future (workers own plain data), stops active audio and releases wave references, accounts final meter frames and resets mouth parameters. Public stop-speech API cancels only its linked pending action; resume cancels voice and behavior override together. Linked stream failure cancels voice, independent speech is retained. Decode/audio-device failures clear the linked lifetime; preparation deadline60s. Actual mouth geometry restores on the next update. Add cancellation evidence fields and deterministic demo control, then test active cancellation and normal speech.
R1043 startup rejected before the scene because current GPU plugin registers FOKRuntimeGpuPackCS, absent from the older R1009 common shader Cook. Keep failed log and old verified public launcher. Refresh isolated Editor host/global shaders and build a matching immutable combined common patch before retesting. Never bypass missing shader checks or remove another task's shader registration.

R1049/R1050 speech cancellation gates passed active and pending decode paths. R1051 hundred scene normal speech/palettes/images passed; cold61.094s still fails60s and is explicitly recorded false in presentation validation. Do not infer that a180s diagnostic observation budget satisfies the shipping60s constraint. Updated global shader CookR1045 resolves R1043 missing registration without removing plugin features. Public stageR1048 and patchR1047 integrated after gates.

R1055/R1056 open/closed CPU-GPU captures pass pixel-exact with7833actual matrices mismatch0. Visual inspection shows native eyelids closing correctly on Alice with simultaneous .5mouth opening. Initial unified contiguous span unnecessarily included distant unchanged geometry:46,820/14,851/36,691vertices for three cards despite1870eye affected vertices each. Before enabling auto blink, replace that span with validated short contiguous ranges (merge gaps<=64vertices), preserving baseline/nonaffected vertices and applying mouth+eye deltas in one update. Avoid separate mouth/eye buffer writes that overwrite each other's face vertices. R1057 rebuild tests this lossless segmented layout.
R1057 compile rejected a conditional expression mixing nullptr and TSharedRef from MakeShared (C2446). Return nullptr in an explicit branch, then convert the shared reference to the function's TSharedPtr result. No executable or launcher replaced on the failed build. R1058 retries after the type fix.
## R1066 progressive entry and cache interpretation
The actual public second launch was a valid GPU pose cache hit: first65.851s (0hits/303misses) vs second40.716s (303hits/0misses,2,129,924,112B read). Do not diagnose this as a cache failure or promise instant startup: meshes, material preparation, textures and renderer registration are not stored in this pose cache. R1065 independent warm functional run took31.612s; differing workloads/settings are not a controlled performance comparison.

Previously scene animation/visibility was gated on the entire100-character aggregate GPU bank. Fix loading-stage presentation without corrupting aggregate actor indexing: select pending jobs/commits by distance but keep Entries and manifests in stable selection order. The controlled actor always has priority. While loading, use its private CPU pose with the identical main/source material separation. Never draw all full source sections in the main pass (that would alter IBLAlpha composition), share another actor's rig, or expose an uninitialized pose. Destroy temporary main components and replace late-source primitive lists when the aggregate provider takes over. Only diagnostics freeze capture transforms; interactive input remains active during preparation.

R1068: three cards start showing3.177s, finalGPU4.392s, priority0/2/1,7833matrices mismatch0 and all final CPU/GPU views exact. R1071: single-worker scheduling proof first3.284s/final7.636s, loading-to-GPU front/side/back within strict image thresholds; inspect actual loading images in addition to final CPU/GPU parity. Benchmark default cache now Saved/Crowd/BenchRuntimeGpuCache; public default remains the user's4GiB cache.

## R1073 validation setup corrections
- The audio receipt is RuntimeAudioPatch-R1036.json, not an invented .receipt.json filename. Verify paths with rg --files before use; the failed invocation made no changes.
- R1073 played its demonstration line1s after ready, inside the intentionally selected CPU profile window (GPU mode starts10s after ready). Audio and mouth worked, but GPU action/voice overlap was correctly0, so the overlap gate rejected it. Keep the rejection; R1074 schedules speech12s after ready so the GPU path is actually tested. Do not weaken the overlap assertion.
- A warm startup can legitimately miss newly requested background actions even when every initial pose is cached. R1073 reports304hits/6misses, no corruption/failure; do not require global misses0 after behavior/action streaming. Keep positive hit, corruption, private palette, image and functional checks.
- Cold R1072 all-GPU ready94.474s and warm R1073 all-GPU ready61.234s do NOT satisfy the60s full-load target. First visible5.048s /2.409s; warm controlled actor4.288s. Progressive entry improves first interaction; it is not proof that total preparation became faster, or that loading-time CPU animation sustains120FPS.
R1074 recovery passes actual delayed GPU speech overlap and all100palette/image/native-blink gates; stageR1070 integrated into public launcher. Warm total71.191s still exceeds60s and is recorded false. Temporary loading CPU presentation increases work; do not claim it sped up all-character readiness. Public wrapper remains a single launcher, with benchmark cache isolated and no per-card Cook assets introduced.
R1074 actual CMD validation found a Windows PowerShell compatibility issue that PS7-only script validation missed: Get-FileHash was not discoverable in the CMD's host environment. Replace launcher file identity checks with streaming .NET SHA256 (same digest, disposable stream/hash), then rerun the exact CMD wrapper: passed. Preserve the child exit code around pause so a failed launcher cannot report success. This correction is generic across PowerShell hosts; no system module installation or environment changes.

R1075 compile rejected C2014: prepending an include to a UTF-8 source read without utf-8-sig moved the existing BOM to line2, before #include. Remove that embedded BOM and retain one only at byte0; subsequent undefined API errors were a cascade from the missed first include. Use utf-8-sig when editing BOM-bearing source. The public verified R1070 launcher was not replaced on this failed build. Retry R1076.


## R1077-R1084 loading profile and within-actor follower pose
Same immutable R1077/warm100/cards/settings: serialR1079 allGPU49.132s, evaluation24.756s, commit1.340s,41,148,883bone samples; parallelR1080 allGPU50.696s, evaluation6.189s, commit7.098s,86,194,117bone samples. Evaluation per million bones601.63ms ->71.80ms. Faster pose evaluation allows more animation frames/work; the total load did NOT improve. Never equate cross-frame accumulated savings with a guaranteed equal load-time reduction. Both final CPU/GPU images exact and actual palettes0mismatch; parallel loading/GPU images pass strict thresholds.

Next removes redundant same-actor hierarchy updates: source CPU owns the full private rig; its main loading component follows via SetLeaderPoseComponent. Existing identical private names/parent order/inverse-bind guards remain required. UPoseableMeshComponent::RefreshBoneTransforms does NOT invoke RefreshFollowerComponents automatically: explicitly notify followers after updating the source. Do not call follower RefreshBoneTransforms or copy its local arrays after leader binding. Three-card R1084 loading/GPU and final CPU/GPU three-view gates passed. No pose sharing between different actors.

R1081 cold functional test overlapped the R1082 build: retain functional audio/pose/images, exclude its58.520s load from controlled performance/60s acceptance. Run fresh isolated-cache R1085 only after build completion, then same-executable warm test. Public cache untouched.

R1088 reserved NPC action slot successfully streamed and private pose/images passed, but the real audio gate rejected the run: stream-triggered captures completed before the audio device consumed the new speech. Keep the failure. Diagnostic completion must wait for requested demo speech preparation/playback to finish (or cancellation/error), not just completion of image captures. Empty voice catalog must not create an infinite wait. R1090 adds that lifecycle condition before retesting NPC action+speech. Public launcher is still the verified R1070 version.


## R1094/public R1091 integration checkpoint
After fixing diagnostic speech completion, R1092 reserved-slot NPC action/speech passed actual audio consumption and GPU overlap; R1093/R1094 hundred-actor private palette, image, native blink and presentation gates passed. Public launcher now uses R1091 and the exact CrowdRoomDevelopmentAPNGGPU.cmd -ValidateOnly returned 0.
Warm first visible 2.311s/all GPU ready 52.546s; cold all GPU ready 71.792s still FAILS the 60s target. Short offscreen GPU-mode frame mean 7.22749ms/P95 9.65470ms is not evidence of stable visible 120FPS. Do not label cold-load or sustained-FPS work complete. On-demand action misses are expected even with all 204 initial pose entries cached; do not reintroduce a global zero-miss gate. Public cache was not cleared or replaced by the isolated benchmark caches.

R1095 inspection initially used an unverified scene.report.json filename; actual evidence is scene.json (discovered with rg --files). No changes resulted from the failed read. Build also reported the existing ParallelFor include before the matching scene header; reorder matching header first as Unreal requires, retain BOM only at byte0. Keep the verified public launcher until fresh build and scene gates pass.
R1100 serial/parallel cache comparison: cumulative lock wait improved 5132.549ms ->1905.688ms, but whole load 36.638s ->36.741s did not improve. Do not call that a startup-speed win or attribute R1098 cold50.271s to a zero-hit decode change. Separate later background action misses from initial204 cached clips. R1102 injected corruption in a verified isolated two-clip test directory only; one invalid entry rebuilt, one healthy hit retained, zero failures and exact CPU/GPU captures.
Windows rg path arguments do not expand embedded filename wildcards; inferred OKGpuAnimationCompose.cpp was absent. Discover through rg --files or search the actual directory before reading. Both failed reads were nonmutating.

R1104-R1106 final current-feature checkpoint passes100private actors, exact CPU/GPU three views, matrix mismatch0, real audio and native automatic blink. Observed cold49.490s/warm35.864s both pass60s for this selection. Short offscreen frame mean4.49198ms/P955.42910ms is not sustained visible120FPS acceptance. Cache concurrency reduced worker lock waiting, not whole startup by itself; array reservation yields only a modest observed single-run improvement. Preserve these limits in reports. Cache recipe stamps changed with source changes, so a new build may regenerate old-version cache entries; public cache is neither cleared nor replaced by test caches. Initial JSON comparison draft used selection_to_report_ms as load_ms; corrected to the actual selection_to_gpu_ready_ms field before integration. Report/capture time includes diagnostics and is not loading time.
R1109 telemetry confirms admission-only4ms deadline cannot bound a114-149ms Commit or442.034ms worst loading frame. Do not blame skeleton creation without profiling: measured rig ~1ms and buffers5-9ms; most commit work is elsewhere. An inferred OKRuntimeGpuAnimation.cpp lookup was absent; discover declaration/definition with rg before opening files. The failed read was nonmutating.
R1110 build failed because moved envelope loop retained E.Bones[B].Parent, which cannot and must not be captured implicitly by the worker. Use the already owned Actor.PoseBones[B].Parent (same private hierarchy) rather than capturing E or scene lifetime. No failed executable published. RetryR1111.

R1116 corrected diagnostic interpretation: loading screenshots synchronously flush/read GPU output, so max442/386/338ms from image runs includes capture overhead. Preserve those numbers but label them as capture-contaminated. Clean R1117/R1118 confirms real max169.046->110.241ms;14/913->22/1141frames above8.33ms. Do not claim zero hitches or fewer missed-budget frames merely because the maximum improved. Report diagnostic capture contamination explicitly. Failed RenderCommandFence.h lookup used Engine instead of its actual RenderCore module; discover with rg --files. Pending R1119 adds per-actor reveal ordering; keep public launcher unchanged until final gates.
R1121 fixed0.5ms material batches plus RHI fences and full CPU loading presentation failed total-load requirement (46/100at96.54s). This prototype cannot be promoted as meeting60s or no-dropped-frame goals. Primary verified PID and exact R1120 executable path before stopping its own test; retain absent-report failure as aborted evidence. Hiding drawing alone is insufficient: also gate the hidden actors' CPU pose work and main-loading visibility path, which previously ran independently of ShowLoadingCharacter. Continue own actor clocks/behavior while deferring private pose/render work until GPU installation.
R1126 real nonvisual100warm load64.474s violates60s; worst2658.776ms is a real initialization outlier, not screenshot overhead. Do not present P957.755ms as proof of no frame drops. Keep verified public R1104 until chunked admission passes functional/loading gates. New immutable token must own the entire exclusive bank and expose const data only; do not replace integrity validation with a caller-supplied trusted bool or allow evaluation before all partial key uploads complete. Preserve lease retirement on cancellation/failure.
R1129 chunked upload test crashed: after consuming the composition future, the next tick restarted composition using already moved/null private banks. Guard composition launch with PendingComposition as well as the future; the owned validated bank must remain the single admission state until installation. Failed prototype not published. Retry R1130.
R1133/R1134 staged immutable GPU upload functional gates pass, but100cold67.105s and cleanwarm71.408s violate60s. Warm loadingmean5.022ms/P957.632ms/max874.319ms,572/14198frames over8.33ms. Max reduction fromR1126 2658.776ms is single-run evidence, not zero hitches or improved total startup. Do not promote publicR1104 or call performance complete. Captures contaminateR1133frame telemetry; onlyR1134usedforactual loading distribution. Immutable pending-bank guard prevents reusing moved data. GPU draw dependencies evaluate the first pose before rendering, while the aggregate upload itself must complete before GPU component creation.
Public promotion R1131 was explicitly authorized by the user after reviewing the improvement and remaining limitations. Previous hold-on-R1104 notes are historical, not a current prohibition. Keep cold functional/image/audio evidence R1133 separate from warm cache evidence R1134: cold receipts cannot satisfy the204-hit gate. Exact CMD -ValidateOnly passed; no cache deletion or new game launch. Performance acceptance still pending.
R1135 addresses the aggregate visibility regression: a ready NPC must not wait for all100actors. Do not restore all-NPC CPU loading presentation (R1121 regressed to96s at46actors). Use independent private GPU admission while retaining aggregate final-scene rendering and native authored source passes. Preserve packed-bank ownership separately from streamed-action unique banks; no moved-null bank reuse.
R1135 build failed because the isolated Development host copies an explicit runtime source list; the new progressive GPU include was absent. Add the include to the build staging list, not just the primary source tree. Retry R1136; failed executable not published.
R1139 final CPU/GPU parity and99private early-reveal gates passed (98before100prepared), but loading-vs-final capture failed because the old loading capture hid CPU actors only; newly visible private GPU NPCs occluded the tested actor. Fix diagnostic capture isolation to hide other GPU main/depth proxies too, preserving actual gameplay visibility. Do not loosen pixel thresholds or hide NPCs in the game. Also record actual first visible NPC rather than only controlled proxy. Retry R1141.
R1143 confirms corrected capture isolation; thresholds unchanged, exact final CPU/GPU parity and progressive loading handover pass.99NPCs reveal privately,98before all100prepared, first2.457s. R1144 clean loadingmean5.330ms/P957.727ms/max93.797ms (485/13644over8.33ms), all-ready72.816s: no zero-hitch/60s/sustained120FPS claim. One miss after204initialhits is an on-demand action, not startup cache failure. ManagedGPUpeak3.20GiB accounts for temporary+aggregate overlap, returns to~1.61GiB after retirement. PublicCMD promoted toR1142 with exactValidateOnlyexit0; user requested correcting progressive display on the promoted launcher. R1139 loading image failure remains preserved as a diagnostic isolation failure, not replaced or erased.
R1145 initial inspection repeated Windows rg embedded-wildcard paths and returned invalid filename syntax; no mutation from the failed searches. Use rg --files and directory filters, never embedded wildcard path arguments. DitherTemporalAA expression/header or Engine material-function asset was not found in this UE installation; use explicit interleaved-gradient temporal coverage derived from the local engine RandomInterleavedGradientNoise.ush implementation. Do not merely set an opacity scalar on opaque materials; their shaders must support reveal and be Cooked. Preserve authored blend modes, native cutouts, premultiplied alpha, depth/shadow source passes and100%appearance. A shared common-parent Cook is permitted; player-specific asset precomputation remains runtime.
R1150 first reveal validator wrongly required every actor fully visible before a short3card diagnostic ends and counted all antialias-filtered changed pixels as binary coverage. Two actors legitimately had opacity0.868/0.992 at report; half-opacity integrated image strength0.485 is correct, while FXAA spreads changed coverage to0.922. Verify bounded started states and integrated50%strength (0.35-0.65); require all fully visible only in100card full scene gate. Keep actual pixel metrics, do not loosen final CPU/GPU parity thresholds. External Python nonzero exit must be checked explicitly in PowerShell; initial100run proceeded independently while the validator was being corrected, no failed evidence was promoted.
R1151 fade/current feature gates pass: all100fully revealed, preserved start across private/aggregate,0/50/100images half strength0.487, exact CPU/GPU final three-view parity, native private palettes, blink/audio/presentation. R1152 clean warm loadingmean5.195ms/P957.688ms/max86.844ms,491/13639over8.33ms,70.934s all-ready. Keep93.230s image/first-common-shader run separate; no promise of60s/hitch-free120FPS. PublicCMD R1147/materialsR1145/patchR1149 validated with actual Windows PowerShell. New common shader sources are Cooked; opacity changes never rely on uncooked runtime material compilation. GPU time starts once and retained across provider handover.
