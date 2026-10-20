# 遊戲低模與按需高模

2026-10-03 使用者批准先做低模／小材質與畫面大小 LOD，明確要求動畫頻率不降低；停止材質 atlas／section 合併。陰影採重要性分級。共用身體骨架動畫另行核對，不等同完整私有骨架可直接共用。

## 實施與驗收清單

- [x] 100張 GameLow30kR1 全部完成：低模整人含衣服、頭髮、飾品 <30,000 三角面、單一 render LOD。遊戲只有低／高兩種模型。原高模保留；舊三LOD資料僅歷史試驗。
- [x] 256 級貼圖實際縮小來源像素，保留 alpha 與色彩空間，產生 mip；不烘焙材質、不合併 section。
- [x] 遊戲低模只保留當前服裝；APNG 全部服裝仍保留。
- [x] 移除遊戲低模中的 native AnimationSnapshot：此場景播放已烘焙的 UAnimSequence，不使用來源重建資料。C001 舊 Character 本身約 50 MB，不能把這批不必要資料一起載入；原 APNG／高模資產仍保留。
- [x] 所有角色維持原動畫求值頻率，停用 update rate optimizations。
- [x] 進場只載低模及 ≤256×256 角色貼圖並稽核間接依賴；高模使用 soft paths，進場完成且玩家移動鏡頭後，近距離 650 cm 非同步載入，離開 900 cm 回低模；最多同時 2 名高模，逐名切換並保留行走相位。
- [x] 高模使用已載入的低模作陰影代理，不新增動畫 evaluator；1200 cm 外停止角色動態投影。代理保留原 section，因此尚未做到少 section 陰影代理。
- [x] 外部 100 人選角 JSON 與單卡任意來源 builder，避免只支援固定 C000–C099 組合。
- [ ] 單人遊戲畫面：臉、頭髮、alpha clipping、輪廓、行走。
- [x] 三月七高／低切換遊戲驗證與錯誤檢查；100 種角色切換仍需驗收。
- [x] 百人低模資產完成，正式Cook Development重測：100unique/100animated、初始僅低模小貼圖，載入7–10秒。FPS已量測但尚未達120。
- [ ] 百人Development平均及P95均≤8.33ms。
- [ ] 冷／暖載入、隨機選角與連續換組驗收。

早期 C001 47700 / 23848 / 11923 版本出現服装异常拉伸，已隔離；保護權重版本 88735 / 88667 / 88667 仍出現四肢遺失，也未通過。不能把這些數字當有效低模成績。

早期驗證使用 Development Editor -game；2026-10-04 已建立、Cook並測試獨立 Development runtime Host，詳見下方新證據。Uncooked 資產仍可能含供編輯／重新簡化使用的原始網格資料；只有 render LOD 已簡化，必須另量磁碟大小、實際常駐資源與載入時間。

## 操作

```powershell
./Tools/optimize_crowd_quality.ps1 -Variant GameLow30kR1 -Tag FreshTag
./Tools/run_crowd_room.ps1 -Quality GameLow256R1 -TaskWorkers 8 -ParallelEndFrameUpdates 1
```

選角檔格式：`{"characters":[{"character":"/Game/.../Character.Character","walk":"/Game/.../Walk/Animations.Animations","name":"角色名稱"}, ...]}`，需 100 筆，傳入 `-SelectionFile`。選角來源所在目錄應包含 `GameLow256R1/Character` 及 `GameLow256R1/Walk/Animations`。未知人物需先完成每卡資料準備，不能將首次 KKS 全服裝轉換假裝為已通過 60 秒。

任意已匯入單卡可使用 `OKPackCrowd -OptimizeQuality -QualityVariant=GameLow256R1 -SourceCharacter=/Game/.../Character.Character -SourceWalk=/Game/.../Walk/Animations.Animations -DestinationRoot=/Game/...`。這是每卡準備，與玩家如何組合 100 人無關。

高低切換只處理本場景的固定第 0 套服裝、行走片段；互動、表情與換裝狀態需另接到正式遊戲控制器。距離門檻與同時高模數是目前初始配置，尚未做自動 VRAM 快取容量控制。

錯誤及修正追加於 CROWD_GPU_POSE_ERRORS.md；材質過亮是既有問題，不算低模外觀已完全驗收。

## 低模衣服關閉彈性

離線原生求值已加入衣服專用開關，保留 AssignedWeights 與私有 rig，不會將衣服的所有動畫軌凍結。March 7th 原生回歸通過：6 個衣服部件沒有 particles、21 個其他部件仍有 dynamics，身體持續走動、私有綁定資料不變。證據：Saved/Crowd/RigidClothNativeTestR1.log。低模另烘服裝 palette 軌，高模原 clip 不改；遊戲端動畫頻率維持原設定。UE 服裝重烘已驗證：546 條軌、125 frames、60.484 Hz（保留原序列 rate），解碼位置誤差 0.000007638 cm。完整網格服裝控制試點的遊戲畫面、行走正常；後續 AuthoredBindingsR1 保留受損與 signed-palette 部件的原始編碼座標／綁定，3 LOD × 7 samples 的 bounds 與 surface area 通過；遊戲截圖的四肢與服裝恢復完整，沒有大片服裝拉伸。既有過白材質仍需另外處理。

這個設定主要限制衣服次要形變。場景原本播放已烘焙動畫，沒有逐幀執行原生衣服物理解算，因此不能把它當成已省下大量每幀 CPU 物理成本。

### 衣服無彈性試點最終證據

- 模組：OpenKoikatsu-0791，Rust release lib 已建置。高模原始動畫不修改，低模 clip 使用獨立資產。
- 原生：Saved/Crowd/RigidClothNativeTestR1.log，1 passed；6 衣服部件無 particles，21 其他部件仍有 dynamics，骨架契約不變。
- UE：Saved/Crowd/RigidClothUEProofR2.log，546 tracks／125 frames，原 rate 60.484 Hz，解碼最大位置誤差 0.000007638 cm。
- 幾何：Saved/Crowd/GameLowC001AuthoredBindingsR1.log，3 LOD × 7 時間點外框與各材質表面檢查通過。
- 實際遊戲：Saved/Crowd/BenchmarkGameLowAuthoredClothC001R1.png／.json；單角色持續行走、四肢完整，平均 frame 2.274 ms，P95 2.891 ms。單人數字不代表百人 120 FPS 驗收。
- 限制：C001 主網格 136301／136301／136301 triangles，69／69／69 sections；保留了 41 個不適合直接簡化的區塊，尚未達到有效主網格減面。這個版本可驗證小貼圖與衣服無彈性，不能稱為完成大幅減面。

## GeometryTransferR1：有效減面與切換

Gameplay schema 1004。UE reducer 只處理 reference pose 空間的幾何，暫時設為單一 root 權重；之後按材質和空間位置轉移來源完整骨骼權重，保留矩陣 palette 三軸，不將完整角色換成共同 body pose。

三月七主網格 59306 / 36395 / 24862 triangles（來源約 136285），69 / 69 / 69 sections。其他部件為 3478/1735/866、140/140/140、408/408/408、812/812/812。31 個小型或覆蓋異常材質群使用原始幾何回退，因此實際比例高於理想 35%/17.5%/8.75%，不能用目標比例替代實際數量。全 3 LOD × 7 行走時間點 bounds 與 posed surface area 通過，原始 encoded 坐標錨點避免矩陣反解數值偏移。證據：Saved/Crowd/GameLowGeometryTransferC001R1.log。

遊戲：BenchmarkGeometryTransferC001R1.png/.json，單人平均 2.303 ms / P95 3.116 ms，仍有既有材質過白。此數字不能當百人效能證據。BenchmarkGeometryDetailSwitchC001R1.json 證實 2 次切換、phase error 0、detail_switch_test_met=true，切回低模後 1 evaluator / 5 skin components。陰影代理 RegisterComponent 後明確關閉 tick。

批次工具 Tools/prepare_crowd_low_batch.ps1 每卡獨立記錄成功/失敗，遇到失敗繼續其餘卡；錯誤寫入 Docs/CROWD_LOW_ERRORS_<tag>.md。準備工作屬於卡片匯入階段，不是玩家選角後一分鐘的場景載入。

## 2026-10-04 最新進場規格
- 遊戲預設只選 GameLow256R1；快取不齊全就明確失敗，禁止回退載入高模。
- 高模與高解析貼圖只能在進場完成且玩家移動鏡頭接近角色之後按需請求。低模近景測試不再自動觸發高模；切換診斷需明確指定。
- ready 當下掃描已載入角色 skeletal mesh 與 texture，包含間接依賴；若有高模或大於 256×256 的角色貼圖則記錄路徑並拒絕驗收。
- 最終只保留高、低兩種模型。低模整人含衣服、頭髮、飾品合計最多 29,999 三角面。現有 schema1004 的三種低 LOD 尚未符合此規格；不得稱為完成。
- 動畫頻率維持原值；低模衣服關閉彈性；APNG 仍保存全部服裝。

### 初始只載低模：遊戲實測
LowOnlyRandom100R1，0803，Development Editor -game、8 workers、Auto、固定種子隨機100張不同卡：loaded=100、animated=100、distinct_source_hashes=100。initial_high_meshes=0、initial_high_or_large_textures=0、initial_low_only_met=true；30秒測試 high_detail_residents=0、detail_switches=0。進場資源檢查通過，過程無錯誤退出（exit0）。
啟動到ready=123.791885秒；平均53.400988ms、P95=60.972303ms。60秒載入和120FPS都未通過；使用的仍是schema1004舊低模，不是最終小於30k單一低模。此結果只驗證初始低模路徑，不能宣告整體完成或跨環境效能改善。
證據：Saved/Crowd/BenchmarkLowOnlyRandom100R1.json、.launch.json、.log、.exit.txt。近景低模保持測試：BenchmarkLowOnlyEntryC001R1.json。

## 單一低模新配方 GameLow30kR1（schema1005，驗證中）
新配方只建立一個低模 render LOD，使用全角色 28,000 面預算依各部件原始面數分配，保留 1,999 面安全餘裕。骨骼、完整 affine 權重與原動畫频率保留。生成後必須全角色實際三角面 <30,000；多姿勢範圍與材質表面檢查不通過即拒絕保存 Character。停用會恢復整個原始高面數 section 的舊保護回退，改以驗證失敗追查具體表面。先驗證 C010 重卡及 C001，再批次；不覆寫 schema1004 舊快取。

## Blender 與 1 萬面試點（2026-10-04）
使用已安裝 Blender 5.2，Tools/blender_crowd_surfaces.py 離線處理 canonical 幾何、全部 UV channel。原角色 skeleton、完整 affine skin weights 和烘焙動畫在 Unreal 中保留；Blender 不參與遊戲載入。每個輸出仍通過七行走姿勢、完整權重與整人面數檢查。難以減面的表面保留，從可簡化部件重新分配整人預算，不刪除部件。

GameLow10kR1（schema1006）為另一個替代低模配方，最多9,999三角面、單一 render LOD、實體貼圖≤256²。執行時選擇30k或10k其中一種低模配方，仍只有「低模、高模」兩種模型層級，並未新增中模。動畫更新頻率維持原值。C001三月七試點全身9,588面，七姿勢及GPU權重驗證通過；證據Saved/Crowd/Blender10kC001R1.log。

實際Cook Development Host已可編譯/打包/啟動。ACLPilotC001R1（30k）單人初始高mesh與大貼圖都是0、3.635秒到ready、mean2.005/P95 2.514ms；單人不驗收百人效能。1萬面第一次正式試跑缺引擎BlueNoise貼圖而handled ensure，失敗日誌保留，已補Cook必要引擎soft資源。

目前先完成100張嚴格低模，再測正式Development百人。工具Tools\lua\lua.exe Tools\run_crowd_development.lua：一般Benchmark量FPS；每卡載入計數固定寫入JSON。--load-profile因本機UE記錄資產載入後退出777003而暫時禁用；--profile（≥45秒）擷取暖機後20秒CPU/GPU trace，已驗證正常退出。Tools/export_crowd_profile.ps1匯出threads、全部CPU及Game/Render統計。每張卡另記package_wait_ms、character_build_ms。跨thread加總不是整幀時間，profile有額外負擔，須與同設定無trace基準分開。

驗收仍未完成：100不同卡、初始僅低模小貼圖、啟動到ready≤60秒、平均及P95≤8.33ms、近景高模按需切換、外觀（既有過白問題未解）。不以離線Cook時間判定玩家載入時間。

## 正式百人低模與profile（2026-10-04）

最後C010以Blender恢復難減表面，保留140/408/812面小飾件，整人28,678面、七姿勢及完整權重通過。沒有刪除難減部件；低於1萬面只保留C001試點，載入已滿足一分鐘，暫不批量強行縮小。

|正式Development測試|載入至ready(s)|平均(ms)|P95(ms)|可見section|
|---|---:|---:|---:|---:|
|Low100Baseline20261004T0345，8workers|8.663|27.652|30.923|5793|
|Low100Warm20261004T0355，暖機trace|7.518|25.490|30.388|5793|
|Low100Workers24A20261004T0405，實測23workers|9.897|27.105|31.687|5793|
|Low100Outline20261004T0420，遠景停描邊|7.168|21.729|25.217|3680|

各測試exit0、100unique/100animated、initial_high_meshes=0、initial_high_or_large_textures=0、animation_rate_lod=false。這些是已準備的100卡，並非首次KKS/APNG匯入時間，也不是清除OS/DDC快取後的冷載入驗收。120FPS未達成。

暖機3秒CPU事件window共有約121frames。跨thread exclusive工作量：ParallelDraw4132.68ms、Extract Pose From Anim Data3205.62ms、MeshObject Update2843.80ms、SkeletalComponentAnimEvaluate_WorkerThread974.02ms、GenerateDynamicMeshDrawCommands962.26ms。RenderThread WaitForTasks約2304.09ms，即每frame19.04ms等待；不是CPU工作時間。上述跨thread工作會重疊，不能相加為frame，也不能把6050個ParallelDraw task當drawcall。GPU平均2.639ms，當前CPU側最慢。

來源：Saved/Crowd/Profile-Low100WarmWindow20261004T0400-CPUOnly.csv、DevelopmentLow100Warm20261004T0355.utrace。UE5.8 ExportTimerStatistics會無條件混入GPUqueues，CPU-only數值由ExportTimingEvents與Tools/summarize_crowd_events.py取得。

遠景材質設定停用描邊（來源產生器只有Outline建立WPO），近距1600cm內恢復。不合併section、不降低動畫頻率；仍保留8workers默認。100張高模全部Cook已完成（ChunkLowHigh10020261004T0455-A/B），C001與C010正式Cook高低往返通過；所有100張逐一近景切換、精確色彩與OS冷載入仍需驗證。

隨機排序驗收 Low100RandomDefault20261004T0440：100unique/100animated、initial low-only=true、load7.688s、mean21.769/P9525.182ms、exit0。固定manual exposure改善百人過白；近景精確色彩尚需驗證。整批離線驗收清單：Saved/Crowd/StrictLow100Preparation20261004.json，100張整人最大29,465面、總2,843,247面，來源日志、七姿勢與256貼圖證據逐卡保存。

載入細分（RandomDefault）：100張character_build_ms加總322.78ms、P954.58ms、最大8.40ms；package_wait_ms加總5475.18ms、P95437.62ms、最大533.05ms，包含批次排程等待，不能當純磁碟IO。當前固定低模載入已通過，壓1萬面不是主要CPU渲染瓶頸的解法。

高模Cook包含原Character/Walk依賴，資源體積較大，新增stage/run工具的StageRoot參數，正式全低高包放D磁碟，保留C磁碟既有低模驗收包，不刪除或移動用戶資產。

## 完整低／高模Development包

位置D:/OpenKoikatsuCrowd/DevelopmentStage/Windows/OpenKoikatsu.exe，pak約5.92GiB。100張低Character/Walk及100張PackedRig高Character/Walk皆存在；高資源只作soft按需載入。C001与C010各完成低→高→低、initial高mesh/大貼圖0、動畫phase誤差0、detail_switch_test_met=true、exit0。證據DevelopmentCookHighSwitchC00120261004T0522.json和DevelopmentCookHighSwitchC01020261004T0525.json。

FullCookRandom100Final20261004T0530：100unique/100animated、初始高mesh/大貼圖0、load7.359s、avg28.900/P9541.923ms、Render28.796/Game16.515/GPU1.944ms、exit0。section仍3680，沒有降低動畫頻率。此較慢結果必須保留，21.77ms不能當穩定成績。

目前的一分鐘載入數字只涵蓋已轉換、低模驗收、Cook完成的100張卡。首次新增任意玩家KKS/APNG卡的轉換、低模製備及Cook尚不包含在7–10秒裡；需在角色庫匯入/準備階段完成，不能宣稱任意未準備卡都能在一分鐘內首次進場。

FullCookRandom100Repeat20261004T0535：load7.179s、avg30.354/P9541.388ms、GPU1.953ms、exit0。FullCookWarmFinal20261004T0540：load7.410s、avg25.502/P9536.349ms、Render25.438/Game14.753/GPU2.042ms、exit0，无Error/Fatal/Ensure。完整包未达120FPS，较早21.77ms不是稳定结果。C与D两个stage的Game exe SHA256完全相同，不把差异归因于源码或二进制不同。

完整包暖机3秒CPU事件（约125frames）：MeshObject Update exclusive3708.91ms，Extract Pose From Anim Data3535.48ms，ParallelDraw3491.21ms，SkeletalComponentAnimEvaluate_WorkerThread1008.51ms，GenerateDynamicMeshDrawCommands893.56ms。RenderThread WaitForTasks2293.81ms。当前较远景停描边之前，draw录制工作有所下降；蒙皮更新和动画工作仍大，不再继续强压三角面。来源Profile-FullCookWarmWindow20261004T0545-CPUOnly.csv。跨thread时间不等于frame或可直接节省时间。

## 低模骨骼資料精簡試驗 GameLow30kR2（schema1007）

依完整包 CPU profile，先驗證移除低模未使用骨骼，沒有再次減面或離線合併材質。保留所有實際權重使用骨骼、必要父節點，以及 follower 私有骨架的聯集；角色動畫仍每幀求值。新版本共用 R1 的 256² 貼圖、材質與烘焙動畫，沒有重新烘焙動畫或增加高模初始載入。

C001 頂點位置、法線、切線、UV、顏色、三角形材質組與按名稱對應的原始權重逐一相等；13 個行走時間點所有保留骨骼姿勢一致。骨骼總數2507→2357（5.98%）。離線證據 CompactLowC001Pose20261004T0650.log；其中 legacy audit 的 vertices=30641 是骨骼姿勢比較次數，不是頂點數。

正式 Cook Development 試點 CompactLowPilotR2A20261004T0720：初始高mesh/大貼圖0，load2.125s，avg1.999/P952.426ms。低→高→低切換2次，phase誤差0，動畫頻率未降低，exit0。R1 對照 CompactLowPilotR1B20261004T0730：load1.781s，avg1.993/P952.424ms，相同切換驗證通過。單人沒有可辨識效能收益，不能推算百人120FPS。近景截圖仍有皮膚過白現象，未通過精確色彩驗收。

100卡試驗分支仍非默認；只有資料驗證與正式百人重測通過後才能決定使用。默認仍 GameLow30kR1。Runtime 對R2同樣強制單一低LOD與整人<30k；允許共用R1低解析貼圖，但不允許間接載入R1網格或高解析貼圖。

### 2026-10-04 本輪決策依據
- GameLow30kR1 的正式完整包100卡載入約7–10秒，已準備資產的載入不是目前主要障礙；暫不強行降到1萬面。
- CPU暖機trace同時有蒙皮更新、動畫解壓與draw錄製成本。只能驗證單一改動，再用同一完整Cook包A/B；不能只依骨骼數或worker總CPU時間承諾120FPS。
- R2是可回退資料版本，先保留原R1預設；GPU pose、動畫降頻、全部角色共用私有骨架、離線材質section合併均未啟用。
- 單人R2與R1約2ms並無可辨識效能改善。100卡中部分來源因低模已不用的affine骨骼較多而有更大縮減，先完成100卡正確性再量測整體，不能拿三月七6%當全體平均。
- Source低模骨骼資料欄位、真正section bone palette、runtime動畫required骨骼是不同量；R2共享原動畫，不能宣稱動畫資產大小或解壓工作同步縮小。

百人R2資料製備完成（CompactLow10020261004T0740 exit0）：100/100通過13姿勢與原幾何/按名稱權重一致，整批骨骼193903→179821（7.262%）。逐卡清單Saved/Crowd/CompactLow100Preparation20261004.json；含C001先前pilot來源。共享原R1貼圖、材質、動畫，原R1與高模保存。Cook-CompactLow10020261004T0750與Stage-CompactLow10020261004T0800成功，R2正式Development包可測試。

### R2正式百人ABBA驗收（不啟用默認）

同一完整Development包、固定隨機100不同卡、相同鏡位、8workers；每輪30秒，轉換/Cook/建置均停止後依序執行。

|順序|版本|進場(s)|平均(ms)|P95(ms)|
|---|---|---:|---:|---:|
|A|R1|8.125|24.419|26.590|
|B|R2|7.402|22.249|26.896|
|C|R2|7.947|28.815|36.941|
|D|R1|9.879|32.735|37.986|

四輪都100unique/100animated、初始高mesh與大貼圖0、animation_rate_lod=false、exit0。骨骼193903→179821；section palette references仍273060，可見section仍3680。R2兩組平均較低，但第一組P95略差，後兩輪兩版均明显變慢，短序列不足以斷定穩定收益或建立因果。R2保留opt-in，默認與launcher仍R1。所有120FPS target_met=false，不稱作完成。

證據總表Saved/Crowd/CompactLow100ABBA20261004.json，逐輪完整JSON/log/exit與截圖保留。100卡R2畫面確認沒有整體消失；近景皮膚過白仍需材質修復，未通過精確視覺驗收。

本輪结论：目前已有的低模/256²/按需高模路徑能满足已準備100卡的一分鐘進場；不要因FPS問題再強壓幾何。R2減少部分資料量，但沒有減少draw提交數，也未縮小原動畫資料。後續達到8.33ms仍需降低渲染提交及固定行走pose的工作量；玩家任意選卡要求方案通用，不能以固定100卡離線材質合併取代需求。這輪未啟用GPU pose、動畫降頻或新的離線section合併。

## 主骨架驅動低模跟隨元件試驗（2026-10-04）

UE5.8 PhysAnim.cpp FinalizeAnimationUpdate 每個主骨架更新後呼叫 RefreshFollowerComponents，後者通知所有 follower 重新提交動態渲染資料及 socket child transforms。低模 follower 自身 Tick 又更新 LOD、呼叫 UpdateFollowerComponent；在固定單LOD、無 morph/物理的 follower 可測試省去此獨立 Tick。

新選項 -CrowdLeaderDrivenFollowers / run_crowd_development.ps1 -LeaderDrivenFollowers，只處理單LOD、無MorphTarget、未模擬物理而全部section骨骼名稱/映射正確的低模 follower。維持主骨架每幀動畫求值、私有骨骼、每幀 follower 渲染提交；不減面、不合併材質、不新增動畫資料。進場ready恢復Tick時尊重此設定，高模維持原路徑，切回低模重新配置。尚未驗收，不作預設。

注意 RefreshFollowerComponents 不等同於 SkeletalMeshComponent::UpdateFollowerComponent，後者另負責 morph curves；因此含morph的網格保留獨立Tick，不可一律停掉所有follower。

Follower trace 初步结果：Off 300 ticking parts；On 实际101（100主骨架+1个必须保留独立更新的可见morph follower），不是100。402个无morph且映射正确的follower设置leader驱动。三个含morph的follower维持原路径，含隐藏元件。全部505组件及3680可见section保持。

3秒CPU-only窗口：Off113个完整动画更新frame，MeshObject Update33900次=每frame300；On72个完整frame，21600次=每frame300。各约100次动画解压/frame，跟随渲染更新频率未降低。USkinnedMeshComponent_TickComponent自身exclusive约0.0198→0.0074ms/frame（跨thread事件时间，并非总frame收益）；这部分省时过小，无法解决120FPS目标。profile整帧Off25.754/P9529.243ms，On34.050/P9544.733ms，存在回归，禁止将此开关作为默認。窗口头尾会包含跨边界scope，因此不能拿FEngineLoop::Tick的114/74直接当完整动画更新frame。

## 低模透明材質遮罩試點 GameLow30kMaskR1（schema1008）

正式百人材質提交統計 MaterialSubmissionAudit10020261004T1030：3680可見section，116 opaque、2357 masked、1207其它透明混合。r.SkeletalMesh.UseCachedMDCs=1、r.MeshDrawCommands.UseCachedCommands=1，快取已開啟，不能再把開啟快取當新優化。

此試驗只改材質混合：原BLEND_Translucent且Surface、未禁用深度測試的材質複製成Masked，Opacity pin/constant原樣接OpacityMask，clip=.5；保留完整shader、normal/UV與全部參數，沒有atlas或section合併。No-depth-test眼線、既有Opaque/Masked及其他blend modes保持原樣。材質實例父鏈/override保留並換到對應Masked父材質。共享R1≤256²貼圖與原動畫，另存低模cache供比較，原R1與高模均保留且默認不變。

C001 authoring試點 MaskLowC00120261004T1050 exit0：24 changed slots、51 preserved，逐頂點幾何與原始named權重/骨架/UV/材質組一致（材質物件本身有意更換）。尚待正式Cook視覺與perf驗收。此材質LOD會失去部分柔和透明效果，必須檢查頭髮邊緣、眉眼及衣服；NoZ眼線刻意不轉換。不得未經單卡外觀驗收直接批量。

### 固定正面診斷與試驗停止條件
MaskFrontC00120261004T1200：low-only、high residents0、exit0、avg2.729/P953.952ms；OriginalFrontC00120261004T1210：相同前鏡位low-only、exit0、avg2.184/P952.616ms。Mask版沒有單人效能優勢，眼睛/透明邊緣有差異，不批量或默認。

HighFrontDiagnosticC00120261004T1220 明確為PackedRig高模診斷（不作初始low-only验收），也呈現開口/黑色缺口與過白，因此不能把缺口歸因於本輪Masked轉換或30k低模本身。低模額外使衣服輪廓更粗，但主要嘴部/臉部顯示問題跨高低模存在。增加pilot部件mouth metadata與morph數量報告，以及僅單人診斷用的CrowdMouthBaselineDiagnostic，以辨別基準形狀/口型權重是否缺失。該診斷不啟用於正式100人場景。

## 骨骼緩衝配置對照
新增 -CrowdVertexFactoryBoneBuffers / launcher -BoneBuffers VertexFactory。BeginPlay 在建立角色之前設定 r.GPUSkin.BoneTransformAllocationMode=0；原 Scene=1 保持預設。這是既有逐幀 CPU pose + GPU skin 的另一個緩衝配置，不是 GPU pose、新動畫資料或動畫降頻。兩種配置都保留私有骨架及 full weights。依 UE5.8 SkeletalRenderGPUSkin.cpp，0 使用 VertexFactory 分配，1 在可行時使用 Scene extension。新增 report 實際 cvar，以防開關未套用卻誤比較；不能只依 scene upload scope 消失就稱省下等量 frame 時間。

### 骨骼緩衝正式百人 ABBA（保留 Scene 預設）

Development build BoneBufferComparison20261004T1300、Stage1310成功。VertexBonePilot20261004T1320單人高低模往返通過、phase一致、每幀動畫保持、初始無高模/大貼圖，exit0。百人同一固定隨機選卡、8workers、RHI width8、每輪ready後45秒；建置/Cook/trace分析程序均在遊戲量測前停止。

|順序|配置|初始載入(s)|平均(ms)|P95(ms)|
|---|---|---:|---:|---:|
|A|Scene|7.505|22.731|26.691|
|B|VertexFactory|7.479|31.647|38.556|
|C|VertexFactory|8.079|31.344|35.164|
|D|Scene|8.055|31.693|42.451|

四輪均100unique/100animated、initial_low_only_met=true、animation_rate_lod=false、3680 visible sections、exit0。後一輪Scene也明顯變慢，不能以A/B單組宣稱可重現精確回歸幅度；整組没有足夠證據支持改用VertexFactory。保留Scene預設，VertexFactory只保留明確測試選項。全部target_met=false；已準備資產的進場7.48–8.08秒，不含任意新卡第一次KKS/APNG解析、低模製備與Cook。

暖機CPU-only窗口25–28秒：Scene有130次完整300mesh update，Vertex有102次。scene transform upload task在Vertex消失，但FSkeletalMeshUpdater::ProcessUpload的exclusive跨thread工作由約0.086→10.350ms/frame；D3D12 upload allocator呼叫約5882→11607/frame，exclusive約1.167→5.369ms/frame。顯示成本移轉到逐section上傳/分配，不能把原task消失當成省下同量frame time。這些scope時間有跨執行緒重疊，不能相加；ParallelDraw仍是task數，非draw calls。兩張相同12秒俯視截圖已檢查，沒有整體角色消失或新增整體拉伸，既有過白問題仍在。

原Scene窗口的主要exclusive跨thread工作/frame：動畫解壓約28.26ms、MeshObject Update約27.77ms、ParallelDraw約25.97ms；三者都仍須減少實際資料量或提交量，單純停follower Tick/更換bone buffer配置不足。GPU兩種配置約1.96–2.14ms，CPU為主要限制。

完整證據 Saved/Crowd/BoneBuffers100ABBA20261004.json，對應兩份CPUOnly.csv、四輪JSON/log/exit與截圖保留。本輪未將無收益的followers、masked材質或VertexFactory設為默認，未降低動畫頻率，未啟用新的GPU pose資料或離線section合併。

## 先 profile 再擴批：Selective SRT 與材質提交（2026-10-04）

玩家要求先profile後，停止擴批。四個正式Cook Development run均exit0，8workers，動畫未降頻、初始沒有高mesh/大貼圖；所有遊戲量測期間均沒有同時跑建置/Cook/trace分析。

|配置|人數|進場(s)|平均(ms)|P95(ms)|GT(ms)|RT(ms)|GPU(ms)|
|---|---:|---:|---:|---:|---:|---:|---:|
|原R1|1|1.811|1.991|2.454|0.872|1.986|0.840|
|Selective SRT|1|1.566|2.606|3.769|1.140|2.590|0.834|
|目前原R1|100|7.814|22.268|27.164|12.936|22.211|1.938|
|遮罩材質|1|1.982|2.003|2.435|見JSON|見JSON|見JSON|

百人完整、100不同卡、100動畫、3680可見section。GT/RT/GPU是重疊管線階段，不能相加。仍沒有達到8.33ms。

Selective SRT 是新低模cache GameLowSRTR1/schema1009，仍只有高/低兩種模型，不新增第三種距離模型，也不是GPU pose bank。逐組將可安全表示的一組三column affine骨骼改成一個SRT骨骼，剪切誤差大的組別保留三column；剔除未用的動畫骨骼，另產生對應低模clip。C001202組可簡化，原始key和quarter-frame插值逐組篩選；實際compressed clip完整125frame/501採樣，11881215頂點比較，最大位置誤差0.035413173cm，minimum normal dot0.999887369（約0.86°）。這是相對既有R1低模的驗證，不能宣稱修好原KKS外觀；也不是連續時間的形式證明。所有geometry/UV/morph與材質來源保留。骨骼總數2507→1895、軌道2978→1778。單人高低模往返1570通過、初始low-only；不同骨架不能直接共用高模shadow proxy，新試驗切到高模時使用高模自己的陰影。

source key資訊只用於篩選，最初「在中間時間重新分解」不能代表目標clip的插值，現實作已驗證分解後key的插值，再檢查實際壓縮結果。Normals guard已加到保存manifest之前。不可把有明顯剪切的矩陣一律SRT化；C001最差保守誤差界49.07cm。可選SRTErrorCM≤0.05，只保留不超過容差的組別。

25–28秒CPU-only窗口：單卡SRT animation extract約0.10818→0.09554ms/frame、MeshObject update0.06877→0.06183、pose matrix upload0.02784→0.01989；減少這些局部工作，但ParallelDraw0.33717→0.36905、section仍71，沒有總frame改善。資料縮小不代表FPS通過，暫不擴100卡、保持opt-in。

遮罩單卡：23個可見透明section變0，可見section總數仍71。GenerateDynamicMeshDrawCommands的exclusive跨thread工作約0.05238→0.00044ms/frame，但ParallelDraw0.33717→0.39752；整frame1.991→2.003ms，沒有可辨識收益。此結果支持「刪掉dynamic command建置scope仍可能增加其他提交工作」，不能只依單一scope消失擴批。外觀軟透明/眼睛差異仍未通過，不作默認。

百人CPU-only exclusive跨thread工作/frame：MeshObject Update27.806、動畫解壓23.414、ParallelDraw22.996、dynamic mesh command建置6.313、pose matrix upload3.001ms。這些scope有執行緒重疊，也包含被OS暫停的時間，不等同於CPU active時間，不能相加換算frame。

Render Thread等待context：UpdatePrimitive等待約8.896ms/次（144次）；核對RendererScene.cpp FScene::Update，開始時ImmediateFlush(WaitForOutstandingTasksOnly)，等待尚未完成的RHI命令工作以安全修改proxy，不能全歸因當前frame動畫或GPU。RenderFinish等待約4.900ms/次（144次），對應mesh pass setup tasks同步；Visibility等待約2.261ms/次。RHIThread的大量WaitForTasks也不是GPU自己花掉相同時間。這些等待context不能直接全部視作能刪掉的工作。

決策：保持R1。後續優先研究玩家任選卡仍可使用的section提交縮減與pose/transform資料配置；不依單卡改善推算百人、不中止動畫求值、不新增已延後的GPU動畫bank，也不以固定100卡離線材質合併取代runtime需求。

完整證據Saved/Crowd/ProfileFirstFindings20261004.json；等待context另存ProfileFirst100WaitContexts20261004.json；四份utrace/CPUOnly.csv、JSON/log/exit與截圖保留。

## 2026-10-04 Render Thread：畫面尺寸 section LOD

先依profile確認UpdatePrimitive等待RHI命令工作、RenderFinish等待mesh pass setup；實作可套用任選已準備卡片的runtime section可見性策略，不合併固定100卡材質、不新增模型級別、atlas或GPU動畫bank，動畫頻率完全不變。

OKPackCrowd -BuildSectionBounds按每material slot的所有骨骼chunk聚合範圍，壓縮動畫先CacheDerivedDataForCurrentPlatform/WaitOnExistingCompression，遍歷全部key與quarter-frame（本批每卡501個時間點），software skin實際低模頂點與各自private palette。工作執行緒僅處理immutable plain arrays，UObject讀取與JSON發佈在commandlet thread。輸出max pose範圍加25%及0.2cm padding；不是只讀bind pose，也不是將走動軌跡聯集變成巨大範圍。資料為取樣估計，並非連續時間形式證明。

可傳-Selection=<100-card manifest>生成任選已準備cache的索引，預設才使用C000..099。這些離線資料不能在每次進場重建；尚未準備的新卡首次匯入/Cook不包含在目前7–10s進場時間中。

runtime -CrowdSectionLODFile=<json> -CrowdSectionLODPixels=2或4，驗證schema1/recipe1005、hash、mesh path、material/section count。僅無morph且無mouth patterns、預設可見的低模mesh可用；WPO材質跳過。近於1600cm、近裁切平面或high detail保留完整材質；以camera depth與off-axis擴張估算像素直徑，恢復閾值1.25倍形成遲滯。每0.25s更新策略，動畫仍逐frame。ShowMaterialSection只在實際狀態改變時呼叫。舊outline policy改為只更新WPO且只在狀態改變時呼叫，避免重設其他策略的slot並减少不必要的render-state dirty。

索引SectionBounds10020261004T1710.json覆蓋100個唯一source hash、505個mesh，其中100個主mesh符合資格，941697 bytes。commandlet exit0、0 errors；2 warnings為插件HTTP heartbeat逾時，沒有geometry/sampling ensure。先前T1703輸出有compressed-data ensure，exit1，禁止當成驗收資料。

單卡恢復測試SectionLODRestoreC00120261004T1714：max hidden14、restore14、transitions28，near hidden0、visible71、1人持續動畫、initial low-only true；section_lod_switch_test_met=true。此測試用於功能驗證，與離線工作部分重疊，不用其frame時間判斷效能。

效能使用同一Cooked Development binary、固定任選100人manifest、overview camera、1600×1000、8 workers、Scene bone buffer、45s ready測量；不在測量時執行build/Cook/分析。以下保留A/B/B/A原始JSON與CPU trace，效能結論待全部對照完成。

Profile第一版A2/B2的25–28s CPU窗口，估計144/160frame。ParallelDraw exclusive跨thread elapsed工作/frame 23.10→19.56ms（約15.3%減少）、GenerateDynamicMeshDrawCommands6.45→4.43ms（約31.3%減少）。Animation Extract24.46→24.54ms且約100calls/frame，MeshObject Update24.63→24.94ms且約300calls/frame，沒有以降低動畫求值次數偷換成FPS收益。ParallelDraw約50task/frame不變，不能把task數當draw call數。

Render Thread等待UpdatePrimitive9.337→4.770ms/frame、RenderFinish4.989→3.151，但Visibility1.627→3.078增加。這些等待與管線階段重疊、受OS排程影響，不能相加計算frame節省。整frame配對降幅約10%，與提交工作減少相符；不可說所有等待都消失。

Profile也發現新section LOD策略自身每0.25s重新查mesh path/WPO/材質、建立集合會增加GT工作。最終版在低模建立/高低模替換時一次驗證並綁定eligible component/slot，更新時只查弱指標、距離、像素及狀態，避免重複字串和材質分類。保護高模/臉部/口腔/morph/WPO，變更mesh時舊綁定不再使用。

2026-10-04 使用者要求測試視角改第三人稱，後續百人Development launcher預設ThirdPerson（可明確-ThirdPerson:$false作歷史overview比較）。相機跟隨第44索引角色，local +Y後方360cm、肩旁45cm、初始地面約163cm，隨角色行走/轉向，RMB可轉視角。初始仍只載low/256材質；完成低模進場後，近景依既有政策按需升級最多2人高模。第三人稱A/B保持相同動態鏡頭與高模政策，不能混入俯視數據。新增camera_mode/position/follow index，以及frustum_candidate_residents（AABB與視錐交集，並非遮擋後真正draw的人數）。visible_sections仍是全scene enabled slots，不等於真正提交draw數，現在在JSON明確標註scope。

2026-10-04 第三人稱第一輪baseline ThirdPersonBase100A20261004T1730：100 loaded/animated、initial low-only true、load8.011s、frame18.435/P9522.372ms、Render Thread18.374ms、GPU1.788ms、2 high detail residents、6 switches、phase error0。相機地面高度162.513cm，報告視錐候選34人；已檢查截圖，視角在場景內、跟隨人物背後，其他人持續行走。此為關閉section LOD的基準；不能與舊overview測量直接比較。


## 2026-10-04：第三人稱 Render/RHI、60 Hz 與 16 worker 實測

實作可選的 UOKCrowdRenderSkin/AOKCrowdRenderActor（project class，未改 plugin ABI）。動畫時間和人物移動每幀前進；固定 baked pose 可限制最高 60 Hz，按 world time bucket 判斷，同一 bucket 跳過重复求值，低於 60 FPS 不固定隔幀降半。強制初始化/高低模相位設定、required-bones invalid 時仍求值。私有骨架、weights、leader/follower 骨骼對應沿用原 PlayImportedAnimation，沒有讓不同身形共用同一 packed pose。只對已保存固定 baked clip 試點；動態改曲線/互動場景需另外驗證。

視野外骨骼 render upload 使用每卡行走全範圍 bounds、實際 LocalPlayer 視錐、200cm halo，每幀更新；1800cm內、投影陰影、visible WPO、物理、高模全部保護。只在 end-of-frame upload 暫用 UE AlwaysTickPose 可見性條件，保留初次/force/recently-rendered exceptions，返回前復原；動畫求值不依可見性停用。JSON suppressed_candidates 是策略候選，不冒充實際省略更新數。runtime flag -CrowdCullRenderUploads；-CrowdBoundsOnly 僅載 bounds，關閉 section LOD，隔離兩項實驗。

新增真正 GRHIThreadTime 報表、動畫求值/skip counter、worker 實際數。launcher 支援 AnimationHz、CullRenderUploads、BoundsOnly、RHIWidth、DisableOcclusion。Windows 16physical/32logical，corelimit17只產生15workers；>=16改 corelimit=Workers+2並驗證 task_workers。失敗/錯誤已記錄 CROWD_GPU_POSE_ERRORS.md。

同一批任選100張不同 source_hash、同一第三人稱跟隨44號鏡頭、1600x1000、Cooked Development、R1低模/256貼圖、近景最多2高模、45s ready/10s暖機/20s CPU trace。測量期間不執行build/Cook/分析。全數100 loaded/animated、首次low-only、phase round-trip通過；120FPS仍全部失敗。以下各管線時間會重疊，不可相加。

| 試次（完整tag見比較JSON） | worker/RHI寬度 | upload cull | frame/P95 ms | RT ms | RHI ms | prepared載入秒 |
|---|---|---|---|---|---|---|
| Base8A | 8/8 | off | 16.764/19.662 | 16.690 | 3.294 | 9.311 |
| Cull8B | 8/8 | on | 15.587/21.081 | 15.537 | 3.357 | 7.446 |
| Cull16D | 16/8 | on | 16.600/20.782 | 16.551 | 4.210 | 7.445 |
| Cull16E | 16/16 | on | 12.414/16.146 | 12.361 | 2.873 | 7.165 |
| Cull8F | 8/8 | on | 13.815/19.274 | 13.763 | 2.985 | 7.300 |
| Cull16E2 | 16/16 | on | 14.002/20.148 | 13.962 | 3.443 | 8.327 |
| Base16G | 16/16 | off | 17.450/20.779 | 17.401 | 4.192 | 8.759 |
| NoOcc16H | 16/16 | on, occlusion off | 12.525/18.089 | 12.468 | 3.199 | 8.204 |

16/16 + upload cull兩輪12.414與14.002ms，對照off17.450ms，支持此路徑，仍有明顯P95波動。8worker兩輪15.587與13.815ms。不能承諾16一定快，也不能把12.414當穩定120；只將16/16保留為試驗配置，不全域改UE。

CPU窗口25–28s，Base16G/Cull16E2：MeshObject Update 299.44→193.41calls/frame（約35.4%減少）；跨worker exclusive elapsed 36.13→23.59ms/frame。ExtractPose94.21→78.73calls/frame是60Hz上限在更高渲染FPS時生效，不是停止不可見人物動畫；受profile窗口及frame denominator影響。ParallelDraw79.25→76.91 tasks/frame，不能當draw calls。

RT等待OcclusionSubmittedFence 4.090→3.045ms/frame，Visibility等待2.893→2.266。NoOcc16H驗證關閉r.AllowOcclusionQueries後該RHI fence消失，Visibility等待1.636ms/frame；仍保留frustum clipping，可能多画被遮挡幾何，因此只針對這個開放大房間試驗，不能套所有map。NoOcc平均12.525/P9518.089沒有超過最快Cull16E，先保持opt-in，需要重複/多鏡位驗證。其他跨執行緒elapsed不能與上述wait相加。

資料：Saved/Crowd/RenderUploadWorkersComparison20261004.json；Profile-Base16G20261004T1750-Attribution.json、Profile-Cull16E220261004T1750-Attribution.json、Profile-NoOcc16H20261004T1753-Attribution.json，原始utrace與CSV保留。第三人稱截圖已檢查；既有偏白材質問題仍存在，不能宣稱外觀全部通過。

任選100不同角色的策略是每卡通用低模快取（source hash + recipe + outfit），初始只讀低模/256資料；高模只按需讀取。Bounds也按每卡來源hash驗證，不按固定100人的位置/外觀寫死。完整APNG保留全部服裝，遊戲初始只載當前服装。本次7–9s是已準備Cook快取載入，未證明100張全新KKS首次轉換+低模準備+Cook能在60s內；必須在加卡/匯入階段準備快取，不能混成遊戲入場的達標數字。

目前優先保留可證明減少骨骼提交的路徑；section LOD在第三人稱ABBA 18.435/17.142/19.702/16.821ms沒有重現收益，因此不預設啟用。GPU pose bank仍延後。後續瓶頸包括可見角色主/陰影section提交、visibility tasks、private palette更新；應做通用runtime相容section整併/陰影代理或不改外觀的buffer重用，而非只再加worker。

FinalNoOcc16Validation20261004T1758（更新報表後重測）：100 loaded/animated，load7.517s、frame10.945/P9514.780ms、GT8.359、RT10.857、RHI2.682；actual workers16/width16、pose60Hz、hardware occlusion false、always_tick true/full_pose false。與NoOcc16H合看10.945–12.525ms，不宣稱穩定120。


### 60 Hz求值相位分散追加驗證

按SourceHash產生0..1週期offset，100人不必同幀完整求值；角色內所有private parts與高低模共用該角色offset，初始化/相位切換仍強制refresh。只改求值排程，不改clip播放phase或世界移動。-StaggerPose保持可選。

| 配置 | frame/P95 ms | GT ms | RT ms | RHI ms | prepared載入秒 |
|---|---|---|---|---|---|
| StaggerNoOcc16A20261004T1801 | 10.078/15.310 | 7.997 | 10.023 | 3.131 | 7.731 |
| StaggerNoOcc16B20261004T1802 | 10.337/15.154 | 8.171 | 10.228 | 3.135 | 7.275 |
| NoStaggerNoOcc16Control20261004T1803 | 12.229/17.125 | 9.255 | 12.060 | 3.113 | 7.756 |

兩輪stagger平均10.078–10.337ms（約97–99FPS），P9515.154–15.310；同binary/no-stagger對照12.229/P9517.125ms。仍全部target_met=false。不能稱完成120FPS。設定均16actualworkers/RHIWidth16/uploadcull/occlusionoff/pose60Hz。前一版FinalNoOcc平均10.945，波動仍需注意，收益不只憑最快單次。

Stagger16B25–28s窗口343frame：ExtractPose18004次，約6001次/秒，相當於100人各60Hz，沒有因不可見人數減少求值。MeshObject Update173.69次/frame、ParallelDraw73.82tasks/frame；跨worker exclusive elapsed分別14.27與8.50ms/frame，不能相加當frame時間或把tasks當drawcalls。

補齊Task system WaitImpl事件：前一版分析只包含WaitForTasks，不代表全部RT等待。新工具也列FTaskBase::WaitImpl_StateChangeEvent_WaitFor，兩種事件可能nested、不可相加。Stagger16B主要RT等待Visibility1.651ms/frame、RenderGraph Tasks::Wait1.027、RenderFinish0.594、SkinningSceneExtension PreGPUSceneUpdate0.403；下一步應針對這些工作/同步，不只是增加threads。

骨骼upload仍可能在pose持有的渲染幀重送。不能簡單return跳過SendRenderDynamicData，因previous/current bone buffers、motion vectors、force-initial bookkeeping必須正確。後續以pose revision重用GPU資料需要先驗證動作速度/重入/TAA，不把未驗證早退當安全優化。

當前native Development已build/stage，rootEditor module0849已build/repin。Development與Editor launcher預設AnimationHz60；GPU pose bank、OptimizeForInstancing、sectionLOD等失敗/延後方案保持原狀。cull/occlusionoff/stagger只透過明確試验flags啟用，任選新卡仍需先產生hash驗證的低模/行走bounds快取；未導入固定100外觀合併。

StaggerPose60DetailRoundTrip20261004T1805：單人low→high→low測試exit0、detail_switch_test_met通過、動畫phase保持、初始low-only通過。只屬功能驗證，單人FPS不可驗收百人120。Editor launcher FullAnimationRate明確覆蓋AnimationHz0；Development可-AnimationHz0對照。

2026-10-04 繼續profile後新增GatherTasks試驗，UE預設main view gather4（與worker16不同）。Gather8=11.233/P9516.339ms、Gather16=10.879/P9516.505、回測Gather4=11.648/P9517.836；較之前Gather4=10.078–10.337也有波動，不足以說16必定快，default仍4。任選角色draw提交沒有因增task自動減少。下一步測陰影範圍1200→600cm與offscreen近距保護1800→1200cm，近身/高模/visible WPO/物理仍保護，所有設定可明確對照。

### 2026-10-04 第三人稱提交成本、完整量測與 CPU cache 試驗

品質控制新增低模 outline/shadow/upload-near 半徑，以及只把互動焦點切成高模的 FocusedHighOnly。初始仍全員低模與≤256²材質，動畫求值60Hz。高模保留outline；隱藏低模outline可減少section提交，但不會將不同外觀自動instancing。設定只是實驗選項，尚未全面改為預設。BoundsOnly停用小部件sectionLOD，僅使用保守bounds保護upload剔除。

全程保留暖機10s後的50s：固定3600-frame ring改為有限benchmark依時長配置容量，target必須run_finished且all_warm_frames_retained。管線GT/RT/RHI時間相互重疊，不能相加。第三人稱相機跟隨44號，百人都持續走動；實際畫面可見角色數由相機決定。

| 完整區間 Development 試驗 | frame mean/P95 ms | RT/RHI mean ms | prepared入場秒 |
|---|---|---|---|
| FullWindowOutline0Focus16A20261004T1827 | 7.496/10.269 | 7.188/2.277 | 7.838 |
| FullWindowOutline0Focus8A20261004T1828 | 7.609/11.809 | 見原始JSON | 7.289 |
| LargestL3Focus8B20261004T1830 | 6.365/12.479 | 5.973/1.748 | 6.218 |
| BoneWorkBaseSeries20261004T1833 | 5.427/12.443 | 見原始JSON | 6.470 |

這些試驗outline0/shadow600/upload-near1200、focused1high、CullRenderUploads/BoundsOnly/DisableOcclusion/StaggerPose，前兩輪未限制CPU cache。1830/1833採8workers/RHIwidth8、9950X3D最大L3 CCD：96MiB maskFFFF；Windows查詢另有32MiB maskFFFF0000。UE原生processaffinity16加owned PID監測，startup後mask變更即拒絕證據；LargestL3Focus8A20261004T1829因mask重置無效，另存invalid.json。沒有修改其他程序或Windows全域設定。不能只憑跨輪最快值斷言CCD或8workers一定更快。

FrameSeries於測量結束一次寫出逐幀frame/GT/RT/RHI/GPU、主骨骼求值次數與骨骼數成本proxy。1833保留9218frames/50.0227s，超8.333ms約10.65%。frame與pose_count相關約0.904，但60Hz due數本身隨frame interval增加，相關不能當因果證明。shadow/detail變更78次，變更附近mean約6.9ms，尚無證據足以解釋全部P95。

1833最大109.9902ms發生ready12.115805s，對應舊benchmark截圖在12s觸發GPU readback。後續版本截圖移至ready3s暖機期；旧量測仍保存，不能假裝其最大值屬純遊戲成本，也不刪掉不利frames。新的BalancedPose把100位角色依主骨骼數成本分成16組，組內均勻配置60Hz求值相位；維持各卡私有骨架與動畫，切高低模沿用phase。尚待相同binary對照，未啟用GPU pose bank。

以上全部target_met=false，平均超120FPS不能代表P95通過。入場時間僅prepared/Cook完成快取；首次KKS→APNG→低模建置→Cook不包含在這些秒數內。

### 2026-10-04 工作量分散、FPS 上限與重測

BalancedPose保持60Hz，以main RefSkeleton骨骼數估成本，16bin總成本10554–11686；不是直接量到的CPU時間。same-binary無上限Control8=5.890/P9512.793ms、Balanced8=5.503/P9512.314ms，收益有限，不能單憑此達標。Balanced8R2因背景搜尋干擾只存資料不作正式對照。無上限Balanced16=8.642/P9514.409；未採16作預設。

新增MaxFPS設定減少無上限render work，維持動畫60Hz與所有NPC移動。測144兩輪mean6.977/7.260、P957.548/9.014ms，第二輪Require120明確失敗，不宣稱穩定通過。4workers/144=15.091/P9527.173，16workers/144=11.778/P9519.164，皆不採。AboveNormal只套本次owned game PID，監測startup/late priority changes；144/Priority試驗7.281/P959.086沒有通過，預設仍Normal。

停測後5s OS CPU snapshot觀察到UnrealEditor約3.42cores、git約1.02core，這不是當時每幀競爭的證明。保留其他程序，新增launch-to-exit background-cpu.json（只追蹤surviving pre-existing processes，無法涵蓋中途退出/新開程序）供後續判讀。

| 無 trace、8 workers、96MiB CCD、160FPS 上限 | frame mean/P95 ms | RT/RHI mean ms | prepared入場秒 | target_met |
|---|---|---|---|---|
| PrewarmBalanced8Cap160R1 | 6.417/7.616 | 3.565/1.419 | 5.576 | true |
| PrewarmBalanced8Cap160R2 | 6.531/7.753 | 3.456/1.420 | 5.444 | true |

兩輪均完整50s，100distinct/animated，初始全低模≤256²，focused high1、thirdperson44、outline0/shadow600/uploadnear1200、CullRenderUploads/BoundsOnly/DisableOcclusion/StaggerPose/BalancedPose。這是該百人prepared sample及測試鏡位的P95驗收，不等於任意新卡、任意鏡位、每個frame都必達120。R2仍P9914.675ms、max24.553ms、超8.333ms約4%，不隱藏尖峰。主骨骼求值約5985次/秒（aggregate約59.85Hz/人，第一筆/替換計數有邊界影響，不冒充逐人頻率驗證）。

Warm profile PrewarmBalanced8ProfileR1（僅診斷，含statnamed/trace）frame5.170/P9510.097；25–28s有755frames、17988 ExtractPose≈5996次/秒、MeshObject113.72/frame、ParallelDraw49.15tasks/frame。RT骨骼PreGPUScene等待0.604ms/frame、Visibility0.368、RenderGraph0.234，彼此nested/overlap不能加總。RHI WaitForTasks2.395含管線等候，不等於同樣長的GPU提交CPU工作。引擎預設SkeletalMesh.UseCachedMDCs=true，已有cache不能再算新成果。

新增CrowdRoomDevelopment.cmd與Tools/run_crowd_optimized.ps1，預設Play，Benchmark模式強制Require120/FrameSeries。預設使用本次prepared100供重現，可傳SelectionFile與SectionLODFile換玩家選擇；bounds按source hash匹配，未知mesh不剔除upload。新選卡仍須預先轉換/Cook，沒有宣稱首次轉換也能60秒。最終binary將新增frame_rate_limit報表稽核；另測FocusOnlyShadows（只焦點角色投影），尚未採作預設。

### 最終驗收與啟動（2026-10-04）

最終source/module0857與staged Development已完成。新增frame_rate_limit與outline半徑稽核，wrapper真正執行Cooked Development，沒有啟動Editor預覽。OptimizedFinal160R3透過新wrapper -Mode Benchmark -Tag啟動，exit0/Require120通過：frame6.567/P957.872ms、RT4.064/RHI1.566ms、prepared load5.908s、maxFPS160、100distinct/animated、初始low-only、完整50s。CPU cache maskFFFF/no late change。與前兩輪Cap160R1/R2構成三輪相同配置通過，平均約152–156FPS，P957.616–7.872ms。不能保證每個frame都120，仍保留P99/max完整證據。

OptimizedFocusShadow160R1：-FocusOnlyShadows可選，frame6.487/P957.360、P9913.646/max42.893ms、RT3.800/RHI1.403、load5.521s。全場景啟用shadow sections144→29（不是當幀drawcalls），shadow state transitions710→5；100NPC姿勢/網格不變，只焦點角色投動態陰影。只一輪功能/效能試驗、P99仍尖峰，預設不開，保留附近NPC陰影。Focus變更/距離超出仍依原策略更新。

OptimizedDetailRoundTripFinalR1：單卡low→high→low，60Hz/StaggerPose/BalancedPose/FocusedHighOnly/Outline0/FocusOnlyShadows，exit0、detail_switch_test_met/initial_low_only_met通過，frame_rate_limit160；單卡target false合理，不能用單卡驗收百人。

啟動：根目錄 CrowdRoomDevelopment.cmd。第三人稱相機跟隨44號，右鍵可旋轉；只互動焦點載高模，初始全低模。可傳 -FocusResident、-CameraResident、-SelectionFile、-SectionLODFile。預設sample已準備；其他玩家選卡仍需完成低模/動畫/bounds/Cook快取，沒有固定外觀合併，也沒有首次KKS轉換≤60s證據。室內開放場景DisableOcclusion旗標不適合直接外推多房間遮擋場景。可選 -FocusOnlyShadows犧牲其餘NPC陰影；-Affinity Default可停用硬體專用CCD親和性。

重測：CrowdRoomDevelopment.cmd -Mode Benchmark（自產生唯一tag，60s，完整暖機後50s，Require120強制檢查，失敗會報錯）。Saved/Crowd/RenderOptimizationFinal20261004.json彙整本輪全部正式對照，原始JSON/CSV/log/affinity/背景CPU保留。截圖已在暖機3s取得。

既有角色偏白/部分面部缺口仍在高低模視覺中，未以效能通過宣稱已修好外觀；需要獨立材質/遮罩修正。新launcher ProcessPriority保持Normal，4/16workers與AboveNormal失敗方案未採預設。

### 2026-10-04 RT 工作是否可搬 GPU：先研究、再量測

本地UE5.8原始碼優先，並核對Epic [Mesh Drawing Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine)、[Skeletal Mesh Rendering Paths](https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-rendering-paths-in-unreal-engine?lang=en-US)。文件MeshDrawing的部分cache限制仍描述舊行為；本地SkeletalRenderGPUSkin.cpp已supports static relevance且SkeletalMesh.UseCachedMDCs預設true，不能依舊句子推論所有skeletal只能dynamic，也不能把本來已開的cache算新優化。

| 工作 | 現在／可行性 | 本輪決定 |
|---|---|---|
| 頂點蒙皮、位置／法線／切線 | 目前頂點走GPU；Skin Cache可改Compute後跨pass重用。這不會自動把pose解壓或draw command建置搬走 | 不重做GPU skinning；新增mode/compile報表，設定與實際每個mesh使用路徑須分開看 |
| Instance culling、部分draw參數 | 引擎已有GPUScene/compute culling與batched BuildRenderingCommands；r.CullInstances預設1、batched預設1 | 本輪GPU trace已有CullingPasses/Skinning/ScatterUpload；GPU per-instance occlusion預設0，開放大房間不把它當CPU全部可見性替代方案 |
| 骨骼矩陣數值計算／固定pose插值 | 可以專用compute/transform-provider，需更改資料來源與更新協定；不是把CPU callback丟給GPU就可執行 | 已存每卡CPU baked clips及private mapping；先減去重複upload。遵守先前延後GPU pose bank，未增加GPU動畫資料或離線烘焙 |
| RefToLocal準備、buffer header／allocation管理 | CPU dynamic data含引用姿勢、LOD、morph、cloth、舊骨骼buffer等；scene extension已用scatter/copy shader執行GPU部分 | CPU bookkeeping/生命週期仍須維護，不能blind return破壞previous/current bones或motion vectors |
| draw command/shader bindings、資源建立／RHI提交 | 現有FMeshBatch→FMeshDrawCommand→RHICommandList仍有CPU工作。不同draw bindings阻止合併；完整GPU-driven需共同descriptor/材質索引等架構 | 沒有強行合併玩家外觀或透明排序，沒有宣稱100不同網格變成一個draw |
| RT 等GT/worker/命令資料 | 等待是依賴關係，不是可平行計算的shader | 優先減少producer工作，避免只把同樣等待挪到GPU fence |

RTGpuStudy160ProfileR1：prepared100/thirdperson44，8workers/LargestL3/160FPS/pose60Hz，frame6.481/P957.749、RT3.860/RHI1.502/GT4.399、P9913.646ms、300 ticking parts。25–28s479frames，ExtractPose18016≈6005/s、MeshObject128.75/frame、ParallelDraw49.37tasks/frame。RT wait underGameThread breadcrumb2.352ms/frame、Frame wait0.750、RDG0.407；包含空閒/依賴，不當成可節省的實算成本，更不能加總nested waits。

既有LeaderDrivenFollowers在每幀pose版本曾回退；現在60Hz條件有改變，允許受控重測而非重跑同樣失敗。FinalizeAnimationUpdate→RefreshFollowerComponents會在leader真正更新時dirty follower動態render data及socket children；固定單LOD、無morph/physics且骨骼名/映射正確的低模可省掉自己的UpdateFollowerComponent每render-frame重複dirty。高模仍原路徑，私有服装/頭髮/飾品烘焙骨架與weights保留。

RTGpuStudyFollower60R1（profile）：frame6.271/P956.731/P997.074、RT2.659/RHI1.258/GT3.857、load5.208s。398個符合條件follower改leader驅動、103parts保留tick（100leader加必要更新）。主骨骼aggregate約5999次/s；25–28s480frames，ExtractPose18004≈6001/s、MeshObject64.97/frame（比128.75少49.5%）、ParallelDraw49.45tasks/frame幾乎不變。cross-thread exclusive scope不能相加當省下frame時間；一輪profile差異尚不足採預設。

新增安全排除：external morph sets、LOD0 mesh deformer、在用cloth asset均保留独立tick。所有骨骼映射仍逐section name匹配，含morph目標/模擬物理/多LOD不改。另報告skin_cache_mode/compile_shaders、gpu_instance_culling/occlusion/batched_commands；這些只稽核設定，不當作每個mesh實際cache命中證據。待無trace與高低模round-trip通過再採用。

### RT/GPU研究最終無trace對照與採用（2026-10-04）

最終module0858/staged Development，guard build/stage/root全部exit0。runtime稽核：SkinCache.Mode1但CompileShaders0，compute SkinCache未編譯；目前頂點GPU蒙皮不能與compute cache混稱。gpu_instance_culling1、gpu_instance_batched_commands1、gpu_instance_occlusion0，已有GPU culling、scatter/copy的能力，不再把開啟既有設定算新成果。

| 同binary，160FPS，無trace完整50s | frame/P95/P99 ms | RT/RHI mean ms | prepared load s |
|---|---|---|---|
| 舊Tick ControlNoTraceR1 | 6.298/6.751/7.668 | 2.945/1.361 | 5.290 |
| 舊Tick ControlNoTraceR2 | 6.272/6.761 | 2.941/1.315 | 5.399 |
| Follower FinalNoTraceR1 | 6.458/6.968/12.596 | 3.071/1.453 | 6.120 |
| Follower FinalNoTraceR2 | 6.267/6.703/6.949 | 2.521/1.264 | 5.312 |
| Follower WrapperFinalR3 | 6.270/6.704/7.009 | 2.442/1.238 | 5.147 |

全部target true。第一輪無trace follower RT/P99反而較差，因此不宣稱profile31%改善可固定重現。三輪follower平均RT約2.68ms，兩輪control平均2.94ms，約9%差異；背景變動與樣本較少需保留。398合格low followers/103ticking parts vs control0/300；CPU-only profile在相同約6000pose/s下，MeshObject128.75→64.97次/frame是直接工作量證據，ParallelDraw49.37→49.45task/frame幾乎相同，沒有把不同外觀merge draw。

解除cap的同binary無trace對照（其餘配置相同）：舊Tick RTGpuControlFinalUncappedR1 frame4.506/P959.143/P9915.998、RT4.218/RHI1.372，Require120正確失敗；Follower FinalUncappedR1 frame3.905/P955.900/P9913.526、RT3.622/RHI1.307，target true。RT約低14.1%、frame mean低13.3%，仍單輪不可稱所有情況保證。正式Play保留160上限避免無上限CPU競爭。

所有無trace正式case均100 distinct/animated、初始low-only、完整50s、全場啟用3690sections與1635303triangles一致（不是每幀actual visible draws）。沒有減面/減材質解析/少載角色換取本輪收益。main pose aggregate約5968–5999次/s，仍採每角色最高60Hz；不是每個低模卡共享同一pose，保留原來私有映射/weights/烘焙次要物理。

RTGpuFollowerDetailRoundTripR1：低→高→低，detail_switch_test_met true、switches2、初始low-only通過，切回low有4個leader-driven followers、1ticking leader，exit0。高模原tick路徑，morph/外部morph sets/deformer/cloth/physics/多LOD排除，沒有以取消previous/current bones bookkeeping來省update。視覺既有偏白/面部缺口未修。

Tools/run_crowd_optimized.ps1/CrowdRoomDevelopment.cmd已預設LeaderDrivenFollowers；可用-LegacyFollowerTicks復原對照。仍8workers/LargestL3/160FPS/60Hz/bounds-cull，gpu pose bank停用。單卡round-trip不是百人FPS驗收。Saved/Crowd/RenderGpuStudyFinal20261004.json彙整完整證據。全部time為prepared/Cook快取，不含新卡首次APNG轉換或Cook；不同玩家100張新卡仍須先準備資產與相符hash bounds，未知mesh保持保守更新。

### 2026-10-04 陰影規則 profile／快取與遲滯試點（module0859）

計畫：先獨立標記CrowdShadowPolicy，對照原路徑、元件快取、150cm距離遲滯；再測高低模往返與無trace百人，只有可重現的RT/RHI收益才採預設。Development build、stage與root module0859全部exit0，staged/host exe SHA256相同。原有主動畫最高60Hz、398個leader-driven followers、100不同來源、初始低模與小貼圖不變。

UpdateShadowPolicy從UpdateDetail抽出，仍每0.25秒更新；可選快取保留各resident自己的weak component bindings，Actor改變、owned component數改變或weak component失效即重建，每次讀取當前HighDetailMain tag，避免高低模替換後使用舊清單。預設仍600cm原策略。可选-CrowdCacheShadowPolicy/-CrowdShadowHysteresisCm；launcher对应-CacheShadowPolicy/-ShadowHysteresisCm。遲滯150會新caster距離<450cm才進入、已啟用caster到600cm退出，不會扩大最大陰影範圍，但會減少部分角色陰影，預設0。

| 同binary profile，完整暖機後50s | frame/P95/P99 ms | RT/RHI mean ms | shadow transitions | scope平均每次 ms |
|---|---|---|---|---|
| ShadowLegacyProfileR1 | 6.419/6.973/12.233 | 3.436/1.412 | 710 | 0.067 |
| ShadowCachedProfileR1 | 6.311/6.799/7.927 | 2.972/1.398 | 710 | 0.040 |
| ShadowHysteresis150ProfileR1 | 6.452/7.027/14.022 | 3.456/1.460 | 480 | 0.037 |

CrowdShadowPolicy scope是唯一CPU標記，統計79次。不能對混合GPU queues的其他ExportTimerStatistics scope直接當CPU專屬；本輪scope的數值不是每幀成本。快取的列舉scope少約40%，原路徑每秒約4次，本來只有很小成本；遲滯減少約32%切換，未改善P99，不能因此說切換是尖峰主因。

| 同binary無trace，完整暖機後50s | frame/P95/P99 ms | RT/RHI mean ms | prepared load s | max frame ms |
|---|---|---|---|---|
| ShadowLegacyFinalNoTraceR1 | 6.255/6.662/6.857 | 2.312/1.167 | 5.117 | 18.194 |
| ShadowCachedFinalNoTraceR1 | 6.265/6.661/6.894 | 2.384/1.226 | 5.132 | 22.308 |
| ShadowCachedFinalNoTraceR2 | 6.272/6.677/6.979 | 2.518/1.275 | 5.130 | 19.868 |

全部100人mean/P95 target true、3690啟用sections/1635303triangles相同、710shadow transitions相同、8workers/RHI8/LargestL3/160cap。快取沒有無trace每幀收益證據，因此最終不採預設；Tools/run_crowd_optimized.ps1保留原規則，快取/遲滯僅opt-in。正式default配置和LegacyFinalNoTraceR1一致。先前將快取暫放wrapper作兩輪試驗，不把該暫態當最終採用。

ShadowCachedRoundTripR1單卡low→high→low通過，2次切換、3次快取重建，回低模4個leader-driven followers/1ticking leader。所有案例JSON/完整frame CSV/analysis/log/affinity保留，Saved/Crowd/ShadowPolicyStudyFinal20261004.json彙整。仍有偶發最慢幀，不能將P99或mean/P95通過稱為每幀120；下一個尖峰調查應聚焦producer/worker等待、draw command及RHI提交，避免繼續優化這個低頻小scope。既有外觀缺口仍未修，GPU pose bank仍停用；prepared載入時間不涵蓋首次新卡轉換與Cook。

### 2026-10-04 RT/RHI 慢幀對齊、draw 工作切分與資源池（module0862）

計畫：以時間錨點對齊wall-frame和CPU trace，定位等待來源；獨立測draw command切分與transient resource重建，使用同binary無trace對照，再做持續測試與高低模往返。全部build/stage/root0862通過，host/staged exe SHA256均899DFBCEEBCEBAD5560476540E24B920A19B88A8248161F88CE334EC46CAC943。無新增GPU pose bank、無離線外觀合併／動畫資料／Cook資產，幾何、材質、最高60Hz主動畫與8workers/RHI8/LargestL3保持。

新增CrowdWarmTraceAnchor CPU事件與trace_anchor_ready_seconds：trace的StartTime减report的ready秒即時間offset，可把frame CSV ready時間換成trace座標。空scope時間量化為0不妨礙用StartTime對齊。DrawBatch64ProfileR1的ready10.002s啟動trace後出現88.596ms幀，錄製初始化污染正式window；原始資料保留但不得當受控baseline。現在trace於ready5s啟動，錄製20s；量測仍ready10s至RunSeconds，沒有刪慢幀。

初次對齊的ready24.973562s/47.340ms幀，對應trace約30.534s；30.44–30.62s CPU窗口內，Background Worker D3D12RHI::CreateTextureInternal31.628ms、RT等待FRDGBuilder::AllocateTransientResources32.384ms。這些包含descheduling、彼此重疊，不能相加；顯示資源建立／等待有可調查路徑，不證明所有慢幀由它造成。資料：Profile-DrawBatch64HitchR1-CPUOnly.csv與-waits.json。

r.RHICmdMinDrawsPerParallelCmdList原值64。UE MeshDrawCommands.cpp依min(worker/RHIWidth,ceil(draws/minDraws))產生平行task；提高256/512只改工作切分，不減實際draw。CPU-only matched ready10–24.9s：64為49.77 ParallelDraw tasks/frame，512為14.54（少約71%），allocator0仍49.79。Tools/count_crowd_render_tasks.py排除GPU/跨window/open scope，計數除完整CPU RenderFinish；synthetic check驗證GPU排除、邊界、infinity。Filtered export缺少children，不可推exclusive時間；task count不能當draw call。CPU-count證據檔保留。

| module0861，160cap，無trace完整50s | frame/P95 ms | RT/RHI mean ms | P99 ms |
|---|---|---|---|
| DrawBatch64NoTraceR1 | 6.319/6.854 | 2.736/1.323 | 8.796 |
| DrawBatch256NoTraceR1 | 6.330/6.862 | 2.643/1.309 | 9.057 |
| DrawBatch512NoTraceR1 | 6.322/6.848 | 2.636/1.334 | 8.738 |
| DrawBatch512NoTraceR2 | 6.315/6.796 | 2.610/1.289 | 8.941 |
| TransientPool64NoTraceR1 | 6.325/6.814 | 2.567/1.274 | 9.079 |

解除cap的同binary：64 frame3.698/P956.055、RT3.423/RHI1.250；512 frame3.966/P956.789、RT3.333/RHI1.340，雖tasks較少，整幀/RHI反而較差。allocator0 frame3.544/P955.754、RT3.419/RHI1.203。全部mean/P95 target true，但P99仍13.504/15.297/12.569；不能用cap平均掩蓋平行度代價。正式MinDraws保留64。

allocator0讓RDG改用pooled resources，保留draw/shader/動作。matched CPU窗口建立texture由2127次（約0.94/frame）到0，避免這類反覆建立。RHI heap texture cache由64提高256的替代試驗用-dpcvars在RHIInit前套用：本地LaunchEngineLoop device-profile init2857早於RHIInit3171，ExecCmds太晚，cache initializer鎖定值。report確認256；20s profile仍有2380次CreateTexture（舊module profile2896次，跨module多了記憶體telemetry，不能當嚴格百分比A/B），只證明仍未消除重建。保留可選，不採預設。

新增報表rdg_transient_allocator/rhi_transient_texture_cache/rhi_streaming_texture_mb/rhi_nonstreaming_texture_mb。RHI貼圖配置不是全部常駐VRAM，不含完整buffers/alias heap residency；一般process memory_mb也不是VRAM。allocator0非串流貼圖配置830.969MiB，allocator1為719.383MiB，差111.586MiB；串流約120.17MiB，沒有降低角色貼圖品質來換成績。

| module0862，160cap，無trace完整50s | frame/P95/P99 ms | RT/RHI mean ms | max ms |
|---|---|---|---|
| ControlMemoryNoTraceR1，allocator1/cache64 | 6.427/6.870/11.657 | 2.924/1.436 | 36.144 |
| ControlMemoryNoTraceR2，allocator1/cache64 | 6.258/6.678/6.929 | 2.353/1.223 | 19.015 |
| PoolMemoryNoTraceR2，allocator0 | 6.310/6.742/7.415 | 2.565/1.302 | 36.341 |
| PoolMemoryNoTraceR3，allocator0 | 6.301/6.655/7.076 | 2.318/1.213 | 26.511 |
| PoolMemoryNoTraceR4，allocator0 | 6.254/6.654/6.846 | 2.183/1.153 | 14.414 |
| Cache256MemoryNoTraceR1，allocator1/cache256 | 6.255/6.642/6.879 | 2.311/1.194 | 18.228 |
| Cache256MemoryNoTraceR2，allocator1/cache256 | 6.253/6.660/6.851 | 2.307/1.200 | 15.543 |

保留全部控制組和候選：各輪背景/時序變動，不能挑慢control與快candidate宣稱固定百分比。末次pool相較control R2 RT約少7.3%、RHI約少5.7%，跨多輪範圍仍有重疊。採allocator0是基於多次驗收、消除反覆texture建立與可接受配置代價；不表示其他所有鏡位/硬體/場景均最好。Cache256表現也可用，作備選；其建立事件仍存在，不以單次最佳改方案。

Tools/run_crowd_optimized.ps1/CrowdRoomDevelopment.cmd正式預設allocator0、MinDraws64、TextureCache64。-TransientAllocator 1回原路徑；-TransientTextureCache 256須搭配allocator1才測該路徑。MinDraws256/512、ShadowCache/遲滯仍可選且不採預設。base launcher預設allocator1保留原基準。GPU頂點蒙皮路徑與pose bank停用不變。

Default120sR1：完整暖機後110.006s/17559samples，100不同且animated100/initial low-only、平均6.265/P956.643/P996.842、RT2.203/RHI1.188/GT3.361、prepared load5.181s。主動畫aggregate5997.10/s；398followers/103ticking parts，全場啟用3690sections/1635303triangles，包含frustum外，不當actual draw數。120s末frustum候選25，60s末通常35；持續測試相機沿場景走動，不拿110s平均當同鏡位50s嚴格A/B。最大33.029ms、0.148%幀超8.333ms，仍未每幀120。warm-window56.317s與110.006s snapshots非串流貼圖均830.969MiB，這段未持續增長，不能外推任意長時間或不同場景。

TransientPoolDetailRoundTripR1 exit0、低→高→低2次、initial_low_only與detail_switch_test_met通過，回低4followers/1tick、allocator0；單卡target false不作百人效能失敗。21組百人結果、JSON/CSV/trace/log/affinity與未採用設定均保留，彙整Saved/Crowd/RenderAllocationStudyFinal20261004.json；記憶體snapshots另檔。全部載入時間只涵蓋已準備Cook資產，不含首次KKS→APNG、全部服裝處理、低模準備與Cook；既有偏白/臉部缺口未修，不能用效能通過當視覺通過。

## 本輪效能數據排除與低模規格確認

使用者指出本輪有其他程式同時執行，故本輪Submission/Present/QuietReports/Pressure等測試不作120 FPS驗收或設定勝負證據，原始檔保留。週期報告抑制回復opt-in；其它新選項均保持舊預設。後續需在可比較環境重測。核對本輪owned benchmark已自行完成，沒有殘留本輪遊戲程序；不更動外部程序。

現行GameLow30kR1 的100張整角色離線準備驗收（含衣服/其他可見部件）全<30000三角面，最大29465，貼圖最大256²；證據Saved/Crowd/StrictLow100Preparation20261004.json。這是資產規格，不使用本輪受干擾FPS作證。

## WASD 功能驗證完成

module0868/DevelopmentHost/正式stage建置通過，host與stage exe SHA256相同：40E666AC5D5152AD398AC48967298BC57BB25510321BF48CC67C6E1D619D802C。

WasdMovementFunctionalR1：100 loaded/animated、initial_low_only=true、player_control=true、movement_test_met=true。W/S/A/D各一秒距離179.893/180.044/179.592/180.026cm，W+D斜走180.010cm，沒有sqrt(2)加速；idle drift=0cm，測試實際碰到房間邊界player_reached_boundary=true，總行走3906.580cm。期間完成1次焦點低→高換置，控制與位置仍保持。這是功能測試，不作120FPS驗收，仍遵守使用者說本輪有其他程式執行的限制。

互動Play自動加CrowdPlayerControl；一般Benchmark維持原巡邏鏡位。停止原使用者觀看程序，功能測試自動結束，不自動重新開啟互動遊戲。

## 2026-10-04 近景臉部、頭髮與低模衣物修正

新規則以整個髮型的可見三角面總數計算：原始總數 <=10000 保留原始幾何，超過才簡化到 <=10000；描邊和多個髮片也計入總預算。NearDetailR5 的100張準備已完成，8張保留原始、92張簡化，最大9924面。證據 Saved/Crowd/Hair10k100VerifiedR22.json。頭部大型自訂假髮可能以 accessory role 載入，使用姿態下的頭部區域辨識，不能只找 hair 字串。髮型保留自己的骨架、權重與烘焙物理，使用已準備的256²低模材質。

初始仍只載入 GameLowBodySafeR6 的低模角色與小貼圖；1600cm內逐一非同步載入 NearDetailR5，1800cm外回低細節。近景換高模臉、眼白、虹膜、眼框、眉毛與上述髮型，身體和衣服維持低模。臉部透明部件保留独立 renderer 與排序101..107，避免 packed眼白覆蓋虹膜。臉/頭髮追隨已有 pose leader，不增加動畫求值器，動畫仍60Hz。實際近景混合角色可能超過整人30k；整人<30k是初始低模規格，不冒充近景總預算。

近距離臉消失的原因是原始affine骨架palette導致自動bounds不正確。只對頭/口腔與近景部件使用保守角色頭部bounds，近裁切平面在本場景設定1cm，離場恢復；主packed身體仍保留原bounds。40cm測試已恢復臉、眼睛、眉毛。1600cm内輪廓恢復，遠距離仍節省描邊提交；近景替換髮型接續原有距離陰影政策，避免隱藏舊髮型後沒有頭髮投影。

GameLowBodySafeR6 在離線準備階段分層處理皮膚、胸罩與內褲。以實際簡化後的外衣、材質opacity/alpha與七個行走姿勢判斷遮蔽，排除WPO描邊，保留透明、鏤空邊界與頸部。刪除確定被覆蓋的內層面，將剩餘確定覆蓋頂點小幅內縮；不縮小整個胸部，也不一律隱藏內衣。不新增遊戲每幀射線、CPU頂點搬運或貼圖讀回。此修正依每張卡目前穿的服裝準備，換裝需要對新衣物組合重新準備，不能沿用舊服裝遮罩。原始KKS/APNG全部服裝保留。

三月七原先灰色穿插經body/bra材質隔離確認為胸罩；R6前視與行走樣本不再出現原先灰塊。C001 白色衣物區塊在分別隱藏body及bra後仍保留，不當作皮膚/胸罩穿插而錯刪。VisualMarchWalkR26、VisualSecondCardR26、VisualMarchClose40R26 均載入/動畫1、initial_low_only、body_stays_low、face_round_trip通過。這些為功能/外觀測試，不作百人120FPS驗收。

百人新版資產、Cook及第三人稱功能驗證已完成，結果如下。此前被使用者排除的受干擾FPS結果仍不採用。

AuditNear100R32：100張不同SourceHash、近景骨架階層匹配100/100、低模最大29063 triangles、總2595361 triangles、實際texture source最大256²、近景頭髮最大9924。NullRHI審核使用editor材質引用和Texture.Source尺寸；运行期另獨立審核已Cook平台貼圖尺寸與初始間接載入。新版模型/服裝材質仍保留來源section，沒有按固定百人名單合併材質。

FinalNear100ThirdPersonR32：exit0、run_finished/complete=true，100 loaded/animated、distinct100、failed0；initial_low_only=true、initial high meshes=0、large/high textures=0，body_stays_low=true。已準備資產進程載入7.090475秒，process_load_60_seconds_met=true，不含首次KKS/APNG轉換、資產準備與Cook。第三人稱player_control/movement_test=true、idle drift0cm、碰到房間邊界=true；動畫60Hz。近景共載入64次、恢復45次，結束時19位保有近景臉與髮型，無face bone mismatch或缺失近景資產訊息。100人全場走動，WASD測試與鏡頭移動期間沒有整個身體換高模。

host/stage Development exe SHA256：049BBBE7895B14DF64D511A5F689E07AF2D4B2BCE6175E805B42D300D8C8ED9C。root authoring module0894，只保持既有0760插件ABI。Tools/run_crowd_optimized.ps1 和一般 launcher預設改用BodySafeR6，近景FaceLOD預設啟用、輪廓1600cm、WASD Play保留；新bounds檔為Saved/Crowd/SectionBoundsBodySafe100R26.json。Cook對BodySafeR6預設包含NearDetailR5；近景資產在遊戲Ready後才載入。舊全身high/low診斷需-CrowdDisableFaceLOD，工具的-DetailSwitchTest已自動設定，避免與近景分級互相阻擋。

原始JSON、截圖、七個walk samples及失敗候選均保留。彙整Saved/Crowd/NearGameVisualFinal20261004.json，錯誤與處理方式Docs/CROWD_GPU_POSE_ERRORS.md。這次是功能與外觀修正驗證；本次frame平均9.028ms/P9512.978ms、target_met=false，且仍非排除其他程式干擾後的正式效能測試。沒有達到或宣稱120FPS驗收，後續需在無干擾條件重新profile近景成本。
本轮完成後Saved逻辑容量189.732GB，排除DevelopmentHost/Content junction；仍低於200GB，没有再删除来源或验收文件。清单Saved/Crowd/SavedInventoryFinalNearR32.json。
DefaultFaceRoundTripFinalR33：不指定Variant，默认确实选BodySafeR6；initial_low_only、face_lod_enabled、body_stays_low、face_round_trip和run_finished全部通过，近景hair8968。新默认路线已实际执行。

## 2026-10-04 — Crowd toon color output

- Investigated general pale/white appearance separately from the previous eye sorting bug. Imported character shaders (including Forge) compose color into Emissive; UE room Sun/Sky intensity does not directly shade these unlit materials.
- Crowd camera was missing the studio's ToneCurveAmount=0 and ExpandGamut=0 overrides. Default film mapping desaturated the composed toon highlights. Added those overrides, retained manual exposure bias 0 / physical exposure off, disabled bloom. No artificial saturation boost, source card changes, extra textures, or asset preparation required.
- Cooked Development host ColorOutputR1 and stage D:/OpenKoikatsuCrowd/DevelopmentStage updated. Authoring module 0895 also built/pinned. Existing cooked character assets remain compatible.
- Same card 0 / front camera 180cm / near face-and-hair: DevelopmentColorFilmPilotR1 versus DevelopmentColorToonPilotR1, both load/animate 1, initial low only, normal exit. Pink hair, blue/purple eyes, warm skin and pink checks retain more color in the new output. Comparison: Saved/Crowd/ColorOutputComparisonR1.png.
- Hundred-card third-person run DevelopmentColorToon100R1: loaded=animated=distinct hashes=100, failed=0, run_finished=true, initial_low_only_met=true, body_stays_low=true, process load 6.120 seconds. Visual screenshot inspected. Near face/hair policy remains active.
- Performance is not accepted: other programs/build work may overlap. Optimized benchmark wrapper returned failure because its 120 FPS gate remains enforced (mean 10.756ms / P95 17.552ms). The underlying game completed normally; these numbers are not a clean performance comparison.
- Diagnostic -FilmColorComparison in run_crowd_development.ps1 restores film mapping for future same-view comparisons; normal Play uses the new toon output.

### 配色修正後的百人移動回歸（ColorMovement100R2）

- 使用已發布的 Development 色彩修正版本，在第三人稱場景執行完整 WASD 自動移動、斜向速度、停止漂移與房間邊界檢查；movement_test_met=true、idle drift=0、boundary=true。
- loaded=animated=distinct_source_hashes=100、failed=0、complete=true、run_finished=true。初始高模與高／大貼圖均為0，initial_low_only_met=true；process load=6.3171596秒。
- 近景臉／頭髮載入58次、遠景還原41次；face_round_trip_met=true、body_stays_low=true、動畫pose維持60Hz。色彩設定記錄toneCurve=0、expandGamut=0、bloom=0；第三人稱截圖已檢查。
- 本次使用不帶Require120的功能驗證，避免把色彩／操作回歸誤當效能驗收。沒有新增角色素材、改變LOD面數或重新匯入人物卡。
- 證據：Saved/Crowd/DevelopmentColorMovement100R2.json、.log、.png、.exit.txt。120 FPS目標仍待有效效能驗收。

## 2026-10-04 — Native viewport resolution and temporal anti-aliasing

- Replaced the crowd's FXAA default with TAA method2, quality2, history100. Both primary and secondary screen percentage are100; dynamic resolution is disabled. This is scoped to the crowd runtime and does not alter the model/texture/near detail tiers or card colors. Added report fields for actual viewport size and image-quality cvars.
- Same-view NativeTaaPilotR1 at1600x1000: normal exit, low-only/near detail passed. Inspected screenshot against ColorToonPilotR1; thin hair/eye/outline edges are much more continuous. Comparison Saved/Crowd/NativeTaaComparisonR1.png.
- First requested2560x1440 run actually produced1828x1142. Investigation showed UE unattended runs suppress OS DPI awareness unless bAllowHighDpiWhenUnattended=true, even when EnableHighDPIAwareness reports1. NativeDpiTaaSaved1440R3 process DPI query returned0 (unaware); adding high-DPI game setting alone was insufficient. Added both early Engine ini launch overrides and DefaultEngine settings for future builds.
- NativeDpiTaaActual1440R4 is the actual2560x1440 test: log explicitly sets per-monitor DPI awareness; viewport_width2560/height1440, AA2, primary/secondary100, dynamic0. All100 loaded/animated/distinct, failed0, initial low-only, body low, face near/far and WASD/idle/boundary checks passed; process load7.314757s, exit0.
- Script accepts AntiAliasing=FXAA/TAA/TSR and WindowWidth/WindowHeight for comparisons; normal launcher defaults to TAA and still permits resizing/maximizing. Verification checks actual AA/native percentage and nonzero viewport; dimensions must be read from reports rather than inferred from command line.
- Current performance is not120 FPS acceptance: actual1440 run mean9.423610ms/P9515.131097ms,target_met=false; background processes can interfere and the enlarged tests are not a matched performance comparison.

## 2026-10-04 — UV-safe reduced skin bindings R7

Default gameplay cache is now GameLowBindingSafeR7 (derived from BodySafeR6). Reduction binding transfer disambiguates same-material source candidates with UV correspondence; original affine influences are retained and reduced reference surface, topology, materials and triangle counts remain unchanged. KK_372839/C014 open-leg side comparison: Saved/Crowd/LegSideOpenComparisonC014R3.png (left R6, right R7, walk phase3/6, yaw90/target85cm/distance400cm). Thirteen sampled phases report suspect triangles2 ->0; all seven side walk screenshots retained.

Full100 offline audit: whole initial low maximum29063tri, low source textures256, near hair maximum9924tri, 100 distinct cards and matching private bone hierarchies. Full repair heuristic1651 ->244, 68 cards improve, no per-card count increase; eleven retain suspects, which require separate classification and are not proof every distortion is fixed. Sheer stocking intersections remain postponed.

Rebuilt animation section bounds: Saved/Crowd/SectionBoundsBinding100R7.json (501 samples per card). Full100 Cook/Development staging succeeds at D:/OpenKoikatsuCrowd/DevelopmentStage. Isolated runtime host LegBindingDefaultR8 avoids unrelated paint-controller builds. Existing verified0900 authoring module generated/audited the assets; optional FinalR7 root relink failed on concurrent paint object dependencies, retained in the error log.

DevelopmentLegBindingMovement100R8 normal exit0: loaded/animated100, failed0, initial_low_only_met, movement_test_met, face_round_trip_met and body_stays_low alltrue; load7.473417s. TAA2, primary/secondary100, animation60Hz. Functional evidence only: target_met=false (mean9.4282ms/P9514.8592ms during concurrent work), no120FPS acceptance. Interactive LegBindingPlay100R9 launched at120 cap with WASD using new R7 assets.
Saved size check after staging:175.147GB logical, DevelopmentHost/Content junction skipped (LegBindingSavedSizeR8.json); below200GB.

## 2026-10-04 — Current SurfaceSafeR8 (supersedes R7 default)

Default launch/initial cache is GameLowSurfaceSafeR8. Six additional reduced garment defects were verified against original geometry using the identical rigid gameplay clip and repaired: C007/C039/C087/C090/C094/C098. Original limited surfaces were restored where the whole-person budget allows; C007 stockings and C094 shoes were locally decimated in Blender with connected-island source binding hints and island-preserving welds. Both layers/material slots are retained. No runtime mesh generation or per-frame vertex work was added. Blender recovery now uses hints by default; legacy position/UV-only matching is available only with -CrowdLegacyBindingTransfer for diagnostics.

AuditSurfaceNear100R10 verifies100 unique cards, all initial low models<30000tri, maximum29900, texture sources256 and near hair maximum9924 with matching private hierarchy. C014's already repaired R7 body is reused. Final bounds are SectionBoundsSurface100R10.json; C007/C094 rows match their BodyTopologySafeR1 mesh paths and501 sampled poses.

SurfaceTopologyMovement100R10 normal exit0, loaded/animated100, failed0, load6.822303s, initial_low_only_met, movement_test_met, face_round_trip_met and body_stays_low alltrue. TAA2/primary100/secondary100 and animation60Hz retained. Mean8.402303ms/P959.049702ms/target_met=false is functional-run telemetry, not a120FPS acceptance test. Original-import long garment deformation remains separately classified for C022/C025/C030/C049/C096 (218 stretch-test suspects), and translucent stocking intersections remain postponed.

Side/open-leg screenshots: LegSideOpenComparisonC007R9.png and LegSideOpenComparisonC094R10.png; pilot runs normalexit0, low-only. Detailed rejected attempts and remaining scope: Docs/CROWD_LEG_BINDING_REPAIR.md / Docs/CROWD_GPU_POSE_ERRORS.md. Final Saved inventory175.407GB logical, junctions skipped (SurfaceTopologySavedSizeR10.json). Full root module build, isolated Development host, Cook and stage all succeeded; staged game remains D:/OpenKoikatsuCrowd/DevelopmentStage.
Interactive SurfaceTopologyPlay100R11 confirmed: PID34784 responds, loaded100/failed0, player_control=true, initial_low_only_met=true, AA2 and primary/secondary100. Idle controlled player explains animated99; all100 animate in the bounded movement test.

## 2026-10-04 — Middle hair shell orientation

Current default is GameLowHairSafeR9 with NearDetailR6, replacing R8/R5 for gameplay. Main fix targets near middle hair <=10000, including controlled C079 at8974 triangles; large black fringe polygons came from shell faces oriented against authored normals. Correct shell winding in sampled compressed walking pose, retain surface geometry, private weights and material data. Full100 audit preserves low maximum29900/texture256 and near hair maximum9924; new bounds SectionBoundsHair100R4 uses501 poses. Initial low-only and near face/hair round-trip passed in Development HairMovement100R6, loaded/animated100 failed0, load8.184s. This is functional/visual verification, target_met=false; no new120FPS claim. See Docs/CROWD_HAIR_OUTLINE_REPAIR.md for images and errors.
The common rule is now part of OKBuildCrowdNearHair/BuildNearHair for future generated cards, including original hair already below the triangle cap. OKCrowdHairOutline.cpp takes mesh/clip/materials only; there are no character-specific conditionals. Generic Small/Reduced/ExistingMiddle tests succeeded and the second pass makes0 changes. The R9/R6 batch command is only migration of older caches.

Universal near preparation now writes R6 directly and supports an arbitrary imported SourceRoot for face/hair, independent of the fixed scene test indices. Cached faces are no longer accepted solely because the file exists: source identity, actual <=10000 hair triangles and read-only common shell orientation are checked. Full100 cache audit exits0, highest9924, outline issues0; complete arbitrary-root generation, reuse and legacy-cache rejection tests pass. Final Editor build R10 succeeds. This is an authoring workflow improvement, with no runtime or FPS benchmark change.
