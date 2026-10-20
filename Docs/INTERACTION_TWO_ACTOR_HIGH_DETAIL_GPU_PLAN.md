# 雙人高模 runtime GPU 路線

## 判斷與現有證據

參考 `CrowdRoomDevelopmentAPNGGPU.cmd` → `run_crowd_runtime_apng_development.ps1` → `OKCrowdRuntimeScene.cpp`。應重用 runtime render mesh、immutable geometry、GPU palette／skinning、共用材質 shader 與 cached draw commands，不再把主要精力放在 CPU vertex generation／packing 排程。

Crowd 並非直接的雙人互動替代品：目前會呼叫 `OKReduceRuntimeMeshesAsync`（body/clothes/hair-accessory 15k/15k/30k triangle budgets），預先準備 idle/walk 曲線，關閉該路線的 cast shadows，且有特定 presentation 隱藏規則。不能帶入双人高模場景。R945 receipt 的 GPU 5.193ms 只有193 samples；GPU ready59.470秒，不能當双人≤10秒載入或持續120FPS證據。

現在雙人 body/head 已部分 GPU skinning，髮型／服裝 private renderer 仍有 native CPU LBS、C++ draw generation、bounds與upload，才是要搬走的工作。只有兩人能保留原始網格／貼圖品質，但仍須量測 GPU、draw calls、透明材質、shadows 與 live physics；不能只按100→2人推論達標。

## 分階段實作與驗收

1. 共用 runtime geometry 加入明確 `preserve_source_geometry` 模式：驗證索引與stream後完全跳過減面，保留positions/normals/tangents/UV/color/skin/mouth endpoints與indices。預設 Crowd 行為維持原值。新增原資料與異常索引回歸測試及report品質欄位。
2. 以實際男／女 APNG 建立兩人原始高模GPU probe。讀取APNG及共用資源，禁止角色專用烘焙；這只驗證高模建網格／shader／GPU skinning基礎，不能冒充完成雙人互動。未通過前不更換現有场景renderer。
3. 雙人互動保留 native 共用clock、接觸／IK、動態骨骼、頭眼camera與即時肚子參數。新增同幀palette-only路徑與live affine palette上傳，GPU只消費已求解的小型骨骼資料，不每幀生成／搬運所有高模頂點。
4. 先搬髮型／服裝／配件的private skinning，保留原material、section、outlines、cast shadows與深度／透明順序。依實際part contract分類，未支援的眼睛／face morph保留原路徑；不是用剛性附件取代動態骨骼。
5. 保留任意嘴型切換與語音嘴型；不能只採用Crowd的closed/open mouth interpolation。通過眼球方向、blink、mouth patterns、morph+palette次序的同相位圖像對照。
6. GPU網格／materials runtime背景準備，進場≤10秒、只有準備完成的角色才原子切renderer。GPU使用bone/morph conservative bounds；CPU查詢沿用同份palette按需skin queried vertices，避免GPU readback或每幀全網格CPU skin。水槍功能保留，本輪效能仍略過水槍。
7. 預設版本完整APNG／54嘴型／語音／walking loop／雙人切換12次／速度13階段／male replace／return／shadow oracle／眼睛PNG驗收。同binary同姿勢GPU開關A/B/A；全幀平均≤8ms、實際Present≥119.5FPS、切換最大幀≤300ms。高模品質與載入指標也須通過，不放寬門檻。

## 本輪界線

先實作第1階段的通用品質契約與回歸，確認高模資料可直接流向現有runtime GPU建網格API。雙人live GPU renderer整合與120FPS屬後續阶段；不將原始Crowd短時間量測當作雙人驗收。

補充差異：Crowd目前對送入播放參數的時鐘floor到60Hz，雙人120Hz顯示不能照搬這個量化；shader interpolation／current及previous palette必須使用本幀shared elapsed。正式Game用共用cooked shader library；Editor probe以明確WITH_EDITOR選項使用既有共用材質ShaderMap，不作packaged120FPS或live interaction證據。

## 續作：live palette 發布
先新增 runtime poseable component 的直接 RefToLocal palette 發布介面：完整矩陣不轉成 TRS、不反轉 source 的零 scale；驗證全部輸入後原子發布 value-owned snapshot，錯誤保留上次成功畫面。資產更換／清除／leader sharing 必須使 direct palette 失效。bounds 與 render 使用同一份 snapshot；舊 render thread snapshot 保持不變。新增 shear、反射、零 scale、同幀更新、錯誤保留、asset 更換與清除回歸。此為 private GPU renderer 所需通用 bridge，不將介面完成冒充整個 renderer 或 120FPS 達標。

### live palette 已完成／下一個整合門檻
新增 PublishExactSkinPalette／PublishNativeSkinPalette 與 ordered native slot adapter；成功發布同時 flip Unreal 的 carrier buffer，native affine 不轉 TRS。實際 APNG probe 現在直接安裝共用 walk_01 bytes（修正 portable InstallSharedClip 標記遺漏），3個實際時間點檢查 renderer matrix 與獨立原公式；C00155個／male53個可查 renderer，總324次發布與972個抽樣矩陣，誤差0且實際動畫矩陣會變動。18項回歸通過。這是讀取 solved palette 的 bridge，仍非全網格 GPU 畫面或雙人 renderer 整合驗收。

下一步必須先定義 private renderer 的 source binding／draw contract，不能只把現有 CPU secondary section 頂點當成靜態 reference 丟給一般 skeletal skinning：現有 outlines／overlays 的 offset 會用 RestNormal→CurrentNormal 的 FindBetweenNormals 旋轉，這與直接用 affine 矩陣 skin offset 不等價。需要保留既有 MIDs、UV0..3、color／tangent／sections／透明順序，並在共用 shader／vertex path 明確實作等價 normal-motion offset（額外 runtime vertex attributes；預設關閉以保留既有 draw 行為）。任意嘴型、eye UV、face sphere normal 先留原功能路線，逐類別比較 same-pose 圖像後才切換。

private GPU source／slot mapping 必須從同一已載入 APNG session 的 immutable資料複製，不再重開／重讀 APNG，也不製作卡片專用資產。worker 只處理 owned plain geometry；遊戲線程保留native solver和UObject提交。來源revision／shape／clothing／actor更換立即失效舊job；資源準備和shader permutation完成後才原子切換，否則繼續原renderer。

還要量測／解決 native palette 讀取：目前 SkinPalette 會先 count 再 copy，native API 每次仍驗證頂點weights／indices，不能把目前 bridge 宣稱為完整 O(bones) pipeline。應以native已驗證source generation綁定的契約避免重複 immutable validation，且維持source更換／signed influences／unweighted fallback／destroyed slots 的原公式。不可單純移除驗證或讓不支援part不見。最後才能取得GPU renderer lease、停止該part CPU LBS／packing；CPU ray／paint查詢按需skin，保留水槍功能但效能測試SkipPaint。

### 本輪：消除 palette 重複匯出
1. 在 session 保存成功讀取的 part palette capacity hint；暖機後先直接 copy，native 每次仍完整驗證來源及本幀矩陣，不缓存 pose／借用來源指標。
2. 骨骼數量縮小接受 native 回傳的實際長度；增長或失敗重新 count 再 copy，最多一次重試。暫存輸出後才提交，任何失敗保留呼叫端上次成功資料。
3. Close／重新 Open 清除 hints，move 轉移 session 所有權及 hints；無效 part 不建立 cache entry。此步只減少重複 export，並非 O(bones) 完成，也不直接宣稱雙人 FPS 提升。
4. 回歸測試以可計數的 native 邊界模擬固定／增長／縮小／失敗及非法長度，另外重跑實際 APNG 動畫矩陣和完整現有功能。成功編譯後才開始驗收。

### 續作：原材質 GPU 頂點生成核心
1. 在共用 GPU 模組增加 runtime compute pass：immutable 原始 positions／normals／tangents／浮點 weights／bone indices、draw→source mapping 與 secondary offsets，只提交本幀 affine palette。保留 signed weights／unweighted placement，不減面，不使用角色專用資產。
2. secondary section 使用既有 RestNormal→CurrentNormal 最短旋轉，包含相反方向／零法線分支。獨立 GPU output 與既有 CPU 公式逐頂點比對；不是把 offset 當普通骨骼向量。
3. 先以真實 RHI dispatch/readback oracle 驗證 signed／shear／反射／零scale／fallback、surface與secondary draws及無效來源拒絕。readback 僅測試用，正式 pass 的輸入輸出皆為 RDG buffer。
4. 核心通過後接入 stream proxy 原材質／LocalVertexFactory：需 UAV vertex buffers、正確barrier／draw順序、revision／背景準備／bounds與CPU查詢契約；接入完成並通過畫面oracle前不停止native CPU geometry、不宣稱120FPS完成。

## 續作：常駐 GPU deform 與原 LocalVertexFactory 格式
1. GPU pack pass 輸出原 UE float3 position (12 bytes) 與兩個 FPackedNormal tangent (8 bytes)，對照 DynamicMeshVertex → StaticMeshVertexTangentDatum 的 quantize／basis sign 順序，保留原 UV／color／indices／material。
2. render-thread resident source＋draw mapping 只在 generation 安裝時上傳；每幀只送 affine palette。初始化包含首幀計算與打包，驗證／budget／generation 失敗保留前一代；雙輸出緩衝區保留前一幀，使用 RDG transition 至 vertex/SRV 可讀。
3. 實際 GPU readback 驗證 packing bytes、跨 graph 更新、immutable upload 不增加、失敗保留及舊 generation 拒絕。此階段緩衝區 API 成功後，再由 private stream scene proxy 消費；未接入前維持原 native geometry，禁止以此核心驗收宣稱120FPS。

## 續作：GPU 輸出發布到既有 stream proxy
1. 新增 owned GPU section request／非阻塞 receipt，包含 proxy epoch＋source generation；重建proxy、來源替換或CPU重新寫入時拒絕過期GPU工作。GT不等待GPU、不在worker碰UObject／FFI。
2. resident deform／pack後以RDG GPU-to-GPU copy發布到原Position／Tangent buffers；沿用原LocalVertexFactory及其typed SRV、UV／color／indices／material，避免重綁draw資料。原光柵陰影維持；ray tracing route未驗證時明確拒絕並保留原CPU renderer。
3. caller提供finite conservative local bounds，發布前與舊bounds合併，失敗不得縮掉目前有效幀。此API不提供CPU查詢幾何；native source lease／bounds推導／CPU按需skin仍需下一步實作後才能為實際角色停止CPU geometry。
4. actual scene proxy驗收GPU buffers三幀更新、原材質和immutable streams保留、epoch／generation／CPU takeover／錯誤保留，並重跑完整回歸和APNG雙人功能。GPU readback僅測試，不放寬性能門檻。
整合順序補充：enqueue GPU install之後暫停該part CPU publication，直到receipt決定GPU或恢復CPU；native solve／query snapshot仍可維持。不能在receipt成功後才阻止已排入queue的CPU packets覆蓋新GPU generation。同part多section需一致完成後切owner route。

## 2026-10-07 原始蒙皮來源版本與驗證快取

- 原因：private staged palette 目前 count/copy 都掃描全部高模權重；GPU 發布前不能把這項 O(vertices) 工作留在每幀。
- 來源契約：AccessoryRuntime 的原始頂點、方向、權重與索引在建立後不可直接修改；來源還原/抽離必須清除驗證快取。動態 rig worlds 更新不修改原始幾何。
- 實作：runtime-only 驗證快取與程序內唯一非零 revision；不同 clone/deserialize、來源替換或 palette 槽數改變取得不同 revision，不序列化且不依賴記憶體位址。
- API：保留舊 staged API；新增來源 revision 查詢及帶 expected revision 的 staged 讀取。錯誤不發布任何輸出。每次仍驗證當前 palette/post 有限值，來源資料只驗證一次。
- UE：新增版本查詢與版本限定的讀取，沿用有限次容量重試及原子輸出，Close/move 使用既有 Handle 所屬權。
- 驗收：單元測試驗證重複讀取不掃描來源、clone/還原/換來源/改槽數拒絕舊版本，以及實際男女 APNG 與 native renderer 等價；完成編譯與既有 GPU/互動回歸。
- 本階段不宣稱 APNG 私有衣髮 GPU caller 或 120fps 完成；接入仍需查詢、bounds 與 pending install 發布協調。

### 下一段接入的必要契約（來源版本已驗證）

- source view 不能跨 native 呼叫借用：在 GT 擁有 session 時一次複製 rest stream/weights/bones，記下 native revision；RT request 必須持有自己的 buffer。
- GPU skin 的方向零向量 fallback 必須留意座標系。Native normalize 的 [0,0,1] fallback 與 Unreal normalize 的 Z 不是同一方向；converter 必須保持 native 蒙皮方向語意，再依 native post correction 與座標基底轉到 UE，不能直接假設轉換前後 fallback 等價。以實際 APNG GPU readback 對照確認。
- GPU rest source 的 palette/bind slots、方向、secondary draw offset/rest normal 必須與既有 RuntimeDrawVertices/RuntimeSectionOffsets/RuntimeSectionNormals 一致；source revision 只證明 native 來源，不能代替 proxy epoch、section topology 與 draw mapping generation。
- conservative bounds 預計算在 runtime，一次建每個 influence 的原始 bounds，每幀只處理骨骼與 post correction；signed weights 必須保守處理，不能套用正權重 convex hull 假設。
- CPU query 按需取得現在的 pose，且 CPU publication 不得與 pending GPU install 互相覆蓋。全 section 共同成功後才停用 native full geometry；失敗恢復原發布者。
- 來源版本只識別 immutable native rest mesh。任何會在 native skin 之後修改外層 SessionPart 的 private morph/特殊 normal/眼睛變形，仍需獨立輸入契約或暫留原來的 CPU 發布；不能因為 revision 未變就忽略當前變形。

## 2026-10-07 實際 APNG 私有來源 GPU 轉接

1. 實作 owned native source/pose converter：來源 revision 僅捕獲一次、原始浮點 signed weights 不量化、palette 保留 native skin 座標且最後為 identity fallback；每幀只複製 palette/post。
2. GPU 新增 native Unity space 選項：先 native skin/normalize，再 native post rot_normal，最後轉 UE (-X,Z,Y) 與公分。不能把 basis 併入 post，因為 collapsed post 必須先保留 incoming native direction，再轉基底。
3. draw mapping/offset/rest normal 用現有 UE section 語意；捕獲函式失敗保持既有輸出。非完整方向 streams 回報 unsupported，保留原 CPU renderer。
4. 用女性與男性 APNG、共用 walk 曲線做實際 GPU readback，比對 native 當前 positions/normals/tangents；另用零向量/退化 post/signed 權重的獨立 oracle 驗證方向 fallback 和 secondary displacement。
5. 编譯與完整回歸/雙人功能驗收，錯誤保存在錯誤 MD。此階段先確定通用轉接功能，不把 source readback 當作 actual APNG renderer GPU 啟用或120fps達標。

## 2026-10-07 優先實際雙人播放 >120fps（暫不處理入口超時）

1. 先量安靜的雙人播放基線；保持高模、原材質、眼睛、描邊、陰影與原 solver 更新頻率。
2. runtime 建立保守 bounds：依 influence bone tuple 分組保存 signed weighted homogeneous position intervals；每幀只變換 interval，不能用正權重 convex hull，也不能逐頂點求 bounds。
3. 實際 private part 的全部 sections 安裝 GPU stream，pending 即阻止 CPU render publication，receipt 確認完成才使用 generation 更新。失敗、proxy 重建、source 版本或 topology 變化恢复 CPU。
4. 第一個可量測階段保留 native CPU skin 與當前 CPU query snapshot，但移除 GPU part 的 CPU render packing/upload；依量測決定下一個必要瓶頸。CPU queries 不可讀到靜止舊幀。
5. 編譯、bounds oracle、原 GPU 回歸、actual APNG 雙人眼睛/描邊/陰影及不同速度驗收，安靜 profile 確認平均幀時間與 Present。尚未實測達標必須保留未完成狀態。

### 下一個必要成本：停止已安裝 private GPU part 的 CPU LBS
- 新增每次呼叫限定的 display skin exclusions（part index + expected native source revision），只在 all-sections receipt 成功後排除；native 驗證全部 revisions 後才發布，失敗由 caller 完整 CPU skin 回退。保留 solver/dynamics120Hz。
- 原 Skin API 仍是完整 CPU query。新增指定 private part 的已求解 pose query，絕不再跑 physics，也不借用來源跨呼叫。
- active GPU 時配對 bypass CPU observer packing，使用原 paired solver，再由 display-excluded API skin 未安裝/不支援部件；query 時才生成當前 CPU private geometry。
- 原材質/描邊/眼睛/語音維持，實際 bounds 與 source revisions每幀檢查，native fixture 比對 excluded part retained/CPU part updated/按需查詢等價與stale拒絕。

驗收範圍補充：依使用者暫不處理入口要求，保留 entry原始時間與原總合格旗標，另記120fps_playback_present_verified（僅安靜雙人播放，Visible、平均<=8ms、最大<=300ms、兩段Present>120）。功能audit可明確指定IgnoreEntryBudget，仍輸出entry_budget_enforced與entry實測；不可將入口延期寫成入口通過。

### 實際 native root 的根因與最後驗收
配對 native root 並非 shared root，finish_paired_dynamics 內部 update_render_impl 已生成 CPU 頂點；僅跳過後面的 SkinDisplay 沒有移除這筆成本。以每次呼叫驗證 part/revision 的 exclusions 直接傳入原 dynamics renderer，維持每根骨骼與粒子完整求解，只排除 GPU 已安裝 part 的 render consumption。按需query使用已求解姿勢，不重新推进物理。
預設只在互動場景女角、既有／新載入男角啟用 private GPU；離開恢復 CPU。最後先完整眼睛／速度／嘴型／語音／切換驗收，再編譯、26項回歸、無console override的可見Present重測。另確認一般launcher的CPU模式，避免只驗收特殊測量配置。

### 最後預設路徑未穩定達標：縮短全域曲線快取鎖
FinalDefault Visible profile phase1 為8.412702ms／118.857888fps，不採用只挑快輪的結果。診斷原 sample_paired_frame 約1.47ms，逐角色曲線約0.98ms；load_clip_at / load_elapsed_clip_at 在全域 CLIP_CACHE mutex 內完成整段 sample_cached，兩個rayon角色與背景姿勢準備被串行化。
通用修正：快取 immutable Arc<CachedClip>，鎖內只取得owned snapshot，鎖外採樣；冷載入／替換仍發布完整驗證資料，OKCLIP01序列化維持CachedClip原格式。以替換舊snapshot不變、並行採樣與原oracle逐值等價測試，再重新native build／回歸／完整功能／Visible profile（含讀卡CPU1600×1000）。
正常讀卡launcher原本Cpu預設，ReaderCpuVisibleProfile 1600×1000 phase0/1 為23.099350／19.400406ms、43.286267／51.556061fps，最大945.561901ms，不能宣稱一般啟動已120。調整launcher's GpuSkin預設true（-GpuSkin:$false仍可明確CPU對照）；保持相同1600×1000/APNG高模和原GPU body path，驗收最終正常配置。此調整不變更角色資料或降低精度。
