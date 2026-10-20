# 街區交流系統錯誤與研究紀錄

## 2026-10-07：規劃研究階段

- Windows PowerShell 不會替 rg 展開路徑內萬用字元。使用目錄參數搭配 `-g`，或先 `rg --files`，避免不存在路徑的錯誤與漏查。
- 不應對單行壓縮 JS 用 `Get-Content -TotalCount` 直接輸出；adv_data.js 第一行包含數 MB 資料。本次已改用 JSON 解析後只顯示結構／筆數／有限樣本。後續讀 comm_data.js、h_data.js 同樣限制輸出。
- Python 在此 PowerShell 的預設 cp950 無法輸出部分日文／簡中字。檢查 Unicode 資料時先 `sys.stdout.reconfigure(encoding='utf-8')`，檔案一律明確 UTF-8。
- 檔案名稱不可由類別名猜測。本次猜測 extract_comm.py 等路徑不存在；後續先列實際檔案再讀，缺失不是可當作沒有資料的證據。
- comm_lines 的翻譯 key 數、音訊參照數、唯一檔案數與正式內容覆蓋率不同；不可把語音參照存在就報為全部對話可用。
- 會話 file 可用逗號包含多段語音，也有 callName。不能只取最後一段、按 clip 前綴硬判斷話題或把扁平文字清單當完整選項／好感規則。
- 使用者最新指示為先完成 MD review；已停止本輪獨立男角 APNG 轉換（確認目標命令包含 output/StreetSocialAPNG/Male01.raw.png，owned PID 87232），保留 Saved/StreetSocial 診斷檔。此轉換沒有完成，不能列為男角 runtime 成功。
- 本輪未改遊戲程式、地圖或百人啟動器。完整功能與性能驗收仍待實作後進行。

## 2026-10-07：Grok 平行實作與男角前置

- 使用者授權 Grok Build 平行實作；CLI 1.0.24 實際模型 ID 為 `grok-4.7-build-fast`，不使用猜測的 grok-4.7-fast。兩個隔離檔案責任的工作各有 prompt、PID 和日誌，見 Saved/StreetSocial/Grok。
- Grok 的可選 UECP MCP localhost:30000 未開啟，日誌有 handshake warning；CLI 的檔案和 shell 工具仍正常使用，不把此訊息當程式無法實作或成功驗證。
- 預設男性卡 portable 轉換完成（128.7145 秒是開發期共用／卡片轉換，不是遊戲 runtime 載入）。讀 Parameter 區塊時需跳過 block-data 前的八位元組長度，否則 msgpack 報 ExtraData。
- StreetMaleBootstrap01：男性卡 runtime 完成 1/1、GPU ready、0 GPU failure、47755 三角形，實際男性外觀有顯示。CPU/GPU 三視角圖像門檻未通過（worst tile 約12）；骨架矩陣比較通過。報告 reveal opacity=0.9158，診斷在3秒淡入尚未結束即比較不同幀 temporal coverage，不能把這次圖像差異直接判定為男角骨架錯誤。
- 通用診斷捕捉等待由 ready 後2秒改4秒，保留原3秒公開淡入。需用新 executable 重測；未重測前不報圖像通過。

## 2026-10-07：整合檢查

- StreetMaleBootstrap03 使用新 Development executable，完成淡入後三視角 CPU／GPU 圖像差異均為 0，矩陣驗證通過；這僅是男角前置驗證，不是新街區完整驗收。
- Stage 的原始 Engine 與 Content 為 junction，Get-ChildItem -Recurse 不會把其內容完整複製。Bootstrap02 因 descriptor／Content 缺失無法啟動；stage 工具現明確建立兩個 junction，Bootstrap03 已成功啟動。
- Grok 初版讀取器將性格、sex 與 source 當字串、conditions 當陣列，與實際數值／物件 schema 不符。已要求修正；不得用人造 fixture 通過取代實際 catalogue 整合驗證。
- 資料工作第一次達到 max-turns 並不代表完成。原 session 繼續修正 bundle 的 abdata/ 前綴，匹配 851 段中文語音且轉碼失敗 0；獨立 13 項 Python 測試通過。
- 語音快取第一次 Development 編譯遇到 C4456，內層 Bytes 隱藏壓縮輸入 Bytes；改為 DecodedBytes。StreetSpeechCache03 編譯成功。
- Editor 依賴編譯遇到另一個 UnrealEditor-Cmd 鎖住 UnrealEditor-NetCore.dll；未停止其他程序。其自然結束後重新編譯 editor-dependencies03 成功。
- 真實資料中可直接選擇的 generic 話題列 favor 多為 0；success/failure 分支依偏好等原規則，尚不能任選。街區首版明確採用「完整交談完成 +1」的自訂交流獎勵，再加原列效果；不是宣稱原作該列會加 1，也不藉任選 success 列製造成功。中斷對話不提交效果。
- 新場景首輪編譯遇到 C4150：UHT 的 vtable helper 會在其他 translation unit 建構 actor，TUniquePtr 的 forward-declared pimpl 仍需完整 destructor。改用 TSharedPtr，狀態定義留在 cpp。
- UE 5.8 沒有 Engine/SkyAtmosphere.h 或 ASkyAtmosphere。使用实际存在的 USkyAtmosphereComponent，掛到自製 actor，不再猜測標頭／類別名稱。
- Editor 後續編譯修正：MakeShared 回傳 TSharedRef，不能再呼叫 ToSharedRef；WorldSettings 的標頭位於 GameFramework；自動測試由 Tests 子目錄使用 ../ 讀規則標頭；UE 5.8 的 context mask 是獨立常數，測試改用 EditorContext；太陽元件使用 FindComponentByClass。editor-scene06 與 StreetScene03 Development 編譯通過。
- 地圖 authoring01 已保存 12 個共用材質，但新 UWorld 缺 RF_Public／RF_Standalone，SavePackage 拒絕以避免資料遺失。已明確設定正確 object flags，再建圖；不使用忽略錯誤或引擎特調的 workaround。

## 2026-10-07：實際新街區整合測試

- Authoring02 重新建立既有材質時遇到 partially loaded package。工具改為載入並重用既有共用材質，不重建同名 package；新場景成功保存。
- Automation01 的兩個失敗來自 Windows 路徑未正規化及錯誤要求符合条件台詞恰好三筆。規則改用 NormalizeFilename，測試改驗證三類話題各有可播放 generic 台詞，沒有降低語音／條件門檻；Automation02 成功。
- RuntimeR1／R2 的 GlobalShaderMap hash 不匹配。Cook patch 必須帶該次成對的 SM6 global shader bin 與 shader library，並排除舊 RuntimeGlobal patch。UnrealPak 使用絕對輸入／輸出路徑，退出 0 不足以證明包內容正確。StreetCook02／03 與 RuntimeR3／R4 實際啟動成功。
- RuntimeR3 沿用玩家先前 888×500 設定，即使有 ResX／ResY。新場景明確套用啟動器解析度但不存回玩家設定；R4 確認原生 1920×1080、100% scale、TAA。
- 原場景 stationary 太陽沒有烘焙照明，店面過暗。自製地圖改用 movable 太陽與共用材質環境補色；大地面只接收、不投射陰影。R4 畫面較清楚，但仍有一次 VSM non-Nanite queue overflow，尚未視為陰影性能問題解決，不隱藏警告或以硬體特調掩蓋。
- Runtime 自動報告原本在角色載入完成即寫出，早於交談，因此語音計數為 0。街區功能測試結束刷新 runtime 報告；R4 實際確認三次語音、577899 音訊 frames、633 次嘴型更新與 GPU 動作重疊。
- 測試腳本強化原生解析度／GPU／實際音訊断言時，一度使用不存在的 entries 欄位；已根據實際報告改用 characters 並獨立檢查 R4。不可猜測報告 schema。
- R4 六人平均 2.43 ms、P95 3.10 ms 為離屏功能測試，不能推算百人、正式視窗或首載無掉幀；完整驗收仍待後續。
- 強化測試腳本後 R5 再次通過：全部顯示 8.62 秒、平均 3.58 ms、P95 5.07 ms；保留重測波動，不只引用 R4 最佳時間。

## 2026-10-07：繼續完成剩餘功能

- 查 action 排程時猜測 OKCrowdRuntimeActions.inl，該檔不存在。隨後從 cpp 的實際 include 找到 OKCrowdRuntimeStream.inl；再次提醒先 rg --files 或讀 include，避免靠猜測檔名。
- 首版自動測試只對 NPC 1 交談，不能證明五名 NPC 內容皆可用。本輪改為依序驗證五人，各含開場、三類話題及道別，第五人另走日文語音，逐人音訊／嘴型證據必須存在。
- 首版開場／道別與一般話題共用提交路徑會錯誤累積好感或扣除次數。新場景將問候／道別完成與話題交易分開，不套用街區 +1；取消依然不提交。
- Nanite 街景第一次 Cook 在遊戲指出共用材質缺 Nanite usage，會回退 default material。不可只設定網格 Nanite 就宣稱外觀相同；authoring 工具必須設定並保存所有共用材質的 MATUSAGE_Nanite，重新 Cook 後再比較。StreetCook04 尚不能作為公開外觀完成版。
- 新版測試 executable 曾先覆盖共用 stage 路徑，會使舊 receipt 指向未驗證 binary。本輪已改用 Completion06.stage 等版本目錄，功能測試通過後才更新 receipt；後續不再原地覆蓋公開 binary。
- CompletionR8 更換手勢發現新語音等待 replacement 時，PendingSpeechAction 無條件固定上一個 overlay，造成60秒等待。修正只讓等待當前 Slot.Source 的語音固定該 slot，新手勢不能阻擋舊 slot 釋放；已核對測試 PID／命令列後停止此 owned 失敗測試，準備重測。
- 原流程僅依 !SpeechActive 判定完成，解碼失敗／取消也會走好感提交。新增 Preparing／Playing／Completed／Cancelled／Failed 狀態，只有 Completed 可提交。此為通用 runtime 修正。
- 外部街區控制原先沿用名單第0人預載全部動作；男角未實際使用第二串流 slot。外部控制改尊重與 NPC 相同的 preload 設定，保留原 CrowdRoom 第0人策略；接著須實測兩個私人串流同時播放，不能用男角已預載動作冒充證據。

## Completion R9 integration checks (2026-10-07)
- Active audio is not equivalent to audio completion: asynchronous decode failure/cancel must not grant dialogue effects. Added explicit Preparing/Playing/Completed/Cancelled/Failed runtime status; scene commits only Completed.
- A pending new speech action must not pin a different previous action stream. The hold test now matches source clip IDs; otherwise switching gesture03 to07 deadlocked until60s deadline.
- External-control male startup must not preload every action: this masked testing of simultaneous streams. Both participants now reserve runtime action slots, as NPCs already do.
- Confession eligibility requires friend favor divided by step-up limit >=0.7. A20..70 success curve alone does not imply a friend at favor0 can confess. Grok repair includes boundary and rejection tests.
- Inspect JSON as Unicode data; Windows console replacement glyphs are not proof of corrupt catalogue text. Verified Japanese source codepoints match previous catalogue.
- R9: 15 branch outcomes and6 refusals passed; all5 farewells failed. Schema2 farewell metadata correctly uses GetEndConversationADV, not ConditionDeterminationSpeak. Added separate source-aware farewell selector for full/short/empty talk budgets, favor ratio and lewdness; unknown/world-only IDs remain disabled. Added portable-data regression tests for all3 budget cases.
- A fixed character-wide confession token would permanently forbid a later attempt after rejection. CommitConfession now requires an explicit attempt transaction; same-attempt replay is blocked, later confirmed attempt is independent.
- StreetCompletion09 compile caught an extra closing bracket in the new farewell QueueNode expression. Corrected before runtime; failed binary was never published.
- AutomationCompletion03 caught an incorrect test assumption: when endSelect=true and remaining talkTime=0, source GetEndConversationADV condition2 is valid and beats generic fallback. Verified original Info.cs:1872; corrected the manual-farewell zero-budget assertion rather than disabling a valid source predicate.

## 表情補完工作開始
- 路徑搜尋再次證明不能猜原作Game.cs或Runtime header名稱；以rg --files查實際Manager/Game.cs及KKRuntimePoseNative.h。後續檔案讀取先確認真實路徑。
- Bridge run不支援--help；該次未帶prompt的呼叫沒有開啟工作。使用SKILL列出的run參數啟動已追蹤worker，不以未知CLI旗標重試。
- 原表情每欄空白表示isChangeSkip保留卡片現狀，不可補成零。匯出器有空欄及非有限值回歸測試。

- 新增表情接口必須對沒有MouthStream的請求回傳明確錯誤；否則非法輸入雖被拒絕，呼叫者不能區分角色尚未準備與pattern無效。已補明確error。
- 表情準備採manifest opt-in，預設false；不能為沒有交談需求的百人啟動器偷偷增加所有眉眼嘴pattern的快取成本。
- 已啟用表情卻缺共用expressions.json時直接拒絕場景初始化；不能靜默退回無表情再宣稱完成。

- Grok表情validation曾接受-0.2等(-1,0)值，與保留哨兵-1／實際[0,1]契約不符；已改成只允許-1或[0,1]，原有負小數測試會驗證。
- 原face payload只在分配完才檢查，可能先分配過多；改在sparse陣列grow之前檢查剩餘預算，計入臨時per-vertex與morph讀取缓衝。
- Windows Python讀寫C++ BOM檔必須明確utf-8-sig；本次cp950解碼失敗後的build不足以代表修正版，重新使用新tag建置。

- 本輪再次發現 PowerShell 傳入 rg 的路徑萬用字元無法當 Unix glob 使用；改用實際目錄加 -g。TimeProgressCtrl 位於 MorphAssist，先用 rg --files 查得再讀取，不猜根目錄。

- Grok transition job run-muxued8t-tkw5mg 15m02s僅有首次說明、沒有操作／檔案進展；主代理透過bridge取消並縮小下一輪責任，不能把心跳當實作成功。stop曾回報bridge PID不存在；再次查tracked job確認cancelled，另核對原agent PID以免遺留寫入worker。bridge訊息Stopped by user代表工具取消事件，此次由主代理執行，不是使用者取消任務。

- Automation-Blend01：FacePatterns／FaceTransitions／RulesAndRealCatalogue通過，SourceExpressions失敗；Unreal TryGetBoolField會把數字1轉bool，不能當嚴格JSON型別檢查。blend現在先HasTypedField<EJson::Boolean>再讀取。
- 主代理在收取Automation非零結果後誤啟動BlendR1 runtime；該輪不作修正版驗收／性能證據。依賴的後續操作必須先核對exit_code與assertions，再啟動，不在同一工具script盲目串接。

- StreetBlend02 Development build中途遇到工作區h_playback.rs:672-673六個Rust浮點常值缺整數部分的語法錯誤；讀檔時已被其它工作修正成0.5/0.3/0.31/0.2/0.19。本輪不修改／撤回該外部變更，改新tag重編；不把前次失敗當作通過。

## 2026-10-07 陰影重現 ShadowR1
未修改角色渲染先重跑 StreetBlend03：功能通過，但再次出現一次 VSM Non-Nanite Marking Job Queue overflow。因此不能把前次警告當作已解決或偶發忽略。六人離屏平均3.961ms、P957.226ms、全部顯示9.692秒；Grok同時分析中，不作受控性能比較。測試工具新增 shadow.validation.json 及 RequireCleanShadows gate，禁止僅靠功能 passed 掩蓋陰影錯誤。

ShadowBoundsR1 staging guard: PowerShell script succeeded and returned staged executable, but caller inspected inherited LASTEXITCODE and incorrectly threw Stage failed. File hash comparison confirmed source and stage match. Use script success/terminating error status plus artifact validation; LASTEXITCODE is reliable for native commands, not arbitrary PowerShell helper completion. No runtime launched before verification.

ShadowGateR1: expected failure at RequireCleanShadows assertion; functional social.validation passed but VSM warning persisted. shadow.validation.json correctly reports clean=false. ShadowDiagR1 and ShadowBoundsR1 identify actual GPU character components; page diagnostics are test-only. Initial assumption that replacing static geometry with Nanite alone resolves warning was insufficient. Stream bounds measured before/after confirms conservative sphere inflation. Do not clamp to character height: private hair, accessories and gesture interpolation require a proven envelope.

Root bounds review: Grok follow-up computed streamed radius from the new clip alone, then unioned endpoint boxes. Local hierarchy crossfade is not a world-position lerp: complementary parent scale and child translation can exceed both endpoint spheres. Root changed stream calculation to gather current base/idle tracks and incoming action jointly before per-channel maxima, and added a two-level complementary-scale/translation regression. This is conservative, retains the private rig, and does not clamp/omit geometry.

StreetShadow02 Development failed because isolated host copy list omitted new OKRuntimeSkinEnvelope.h. Root added the common runtime helper to RuntimeApng host source list; this is build integration, not a native compiler/math failure. Editor-streetshadow21 compiled new source and tests but linking failed with LNK1104 because another UnrealEditor-Cmd.exe holds the project DLL. Do not kill unrelated Editor processes; identify command line and retry after its own test ends.

Automation host creation: nested PowerShell here-string in a shell template command terminated early before creating the tool. Used apply_patch to write actual standalone helper and syntax-checked the resulting file. Avoid nested identical here-string delimiters when writing PowerShell from PowerShell.

EditorHost-Shadow01 failed because the Game host lacks Editor-only dependencies (OKCrowdCompact.h and KKImportIndependentCardCommandlet.h), not because the new envelope test failed. The unrelated Editor process has now exited naturally. Retry the real project Editor build; discard the newly created, unneeded isolated-host helper instead of keeping a broken testing path. No unrelated process stopped.

SubmitR1 exit3 before scene load: missing global shader FOKRuntimeGpuCoatingCS permutation0. Shared workspace advanced HEAD to c1bc6b589b9e38b80a39a9661bd5e4e1ecd20f09 and now includes another task's new global coating shader. StreetCook05 cannot be reused with that executable; recook common shader/map content rather than disabling the shader or ignoring startup failure. Public receipt unchanged until runtime success. No history reset or commit by root. Windows rg literal Tools/*street* failed; use directory + -g filter (previously documented rule).

StreetCook06 failed in global FOKRuntimeGpuCoatingCS ShaderParameterParser: disassembly left CoatingRow0/1/2 in $Globals; debug preprocessed source declared them together on one line. Current source RuntimeCoating.usf already has separate declarations (changed by concurrent work), so do not overwrite their fix. Retry fresh Cook07 from the corrected source; keep failure evidence. The shader is unrelated to shadow proxy and must be validated as a common startup dependency.

SubmitR2 measured caster counts154 ->144. Three characters have no section reduction at all. Always creating a shadow mesh/component duplicates memory, face updates and palette work for those no-gain cases. Root added a generic no-gain fallback before UObject/material creation: only strictly fewer caster sections get a proxy; no-benefit keeps original caster with an explicit reason. Runtime assertion now distinguishes beneficial proxies from intentional no-gain fallback and requires at least one actual reduction, instead of forcing an extra component on all six.

SubmitR2 full scene failed: Runtime face pattern invalid after transition settled. Shadow excludes non-casting facial geometry, while settled transition evaluator validates full-display patterns against the smaller shadow stream. Root projects ONLY absent shadow channels to -1 for settled endpoints; active transition uses unchanged full private weights/catalog pointers, and display validation stays strict. Added ShadowSubmit.FaceChannels test, including rejection of invalid patterns on present channels. Also StreetSubmit02 hit unrelated coating test C4756 on INFINITY macro under Development optimization; replaced NaN/Inf constants in that test with std::numeric_limits<float>, preserving intended nonfinite rejection tests. No coating shader behavior changed.

Editor-streetsubmit25 source compilation succeeded but DLL link failed with LNK1104: unrelated InteractionGpuCoatingPairPaintR1 UnrealEditor-Cmd PID6656 held both plugin DLLs. That process exited naturally. Retry Editor26; do not kill unrelated test processes. Runtime SubmitR3 continues independently from its staged copy.
`nFollow-up search: literal wildcard path OKStreetSocial* failed under Windows rg; immediately corrected to directory plus -g, no implementation/test result affected.
Follow-up engine read: InstancedSkinnedMeshComponent.cpp was not at assumed Private root; located with rg --files before reading. No code changed by the failed read.

## 2026-10-07 VSM queue reuse同步缺陷

Grok初版只在queue reset後、enqueue後同步，漏掉consumer讀完queue的同步；快lane0可提早清零下一round，慢lane漏讀工作。root審查拒絕初版，uniform mip結尾新增第三個group barrier，checker增加位置約束。修正後Unreal共用shader編譯成功、兩輪正式Development six-character功能與clean-shadow gate通過。避免再次將abstract schedule PASS誤當完整memory ordering證明。首輪效能受負載波動，追加依序AB測並保留全部數據，沒有忽略慢的一輪。

## 2026-10-07：身份重指與進行中的交談中斷

- 改過的 APNG 位元組不會自動沿用舊關係。只有明確的 `apng-` 加 64 位小寫 sha256，舊 id 到新 id，才可以搬移。檔名、路徑、名單索引和外觀都不能拿來猜人。目的地已經有關係列時拒絕，不覆蓋也不合併。
- 請求先整批驗證。任一筆失敗都不發布。同一個 `request_id` 配對相同則重放，不追加第二筆 ledger，也不重寫存檔；配對不同則拒絕。空的 `identity_retargets` 不寫進 schema 1；舊檔仍可讀，之後的存檔保留已有 ledger。ledger 格式壞掉時整份存檔拒絕。
- 原子存檔失敗時 live store 維持原狀，而且不啟動 runtime。`-RetargetFile` 只把請求寫進準備好的 manifest，不讀寫 `Saved/StreetSocial/relationships.json`。遊戲才是最後寫檔的一方。`-StreetSocialRetargetSelfTest` 若存檔路徑是使用者那份 relationships.json，直接拒絕。
- `Visible` 是進場後 3 秒揭示，不是鏡頭剔除。載入中或揭示淡入不能當成角色消失。只有進行中的男角（slot 0）或目前交談對象出現 runtime error、先前 ready 後查詢失敗或 ready 掉落、先前可見後 visible 掉落，才立刻 `EndTalk`。這會停兩邊語音和動作、清掉排隊片段、釋放占用，而且不提交好感。其他 NPC 的錯誤仍只附加，不結束這段交談。
- 這輪沒有編譯或啟動 Unreal。自動化測試名稱是 `OpenKoikatsu.StreetSocial.IdentityRetarget`。六人場景要等 root 編譯後，用新的 `-RetargetSelfTest` 和原本的 runtime 腳本各跑一次。

2026-10-07 retarget建置前核對：build_crowd_development_host.ps1的StreetSocial source copy list漏OKStreetSocialPerformance.h；既有host內有檔案可能掩蓋fresh build缺漏。root補齊明確複製列表，不靠舊host檔案成功。

更正上一筆header判斷：再追include及檔案開頭，Performance.h明確標為未接入scene/rules的純參考表，rg沒有runtime引用，因此不是當前編譯缺漏。root撤銷只由本輪加入的copy entry，保留既有列表；不把參考資料誤報成fresh build依賴。

## StreetRetarget01 build: transient nested Arc mismatch

Fresh Development build failed in Tools/ok-extract/src/ffi.rs:277 (Rust E0308). rig_name_index already returns Arc<HashMap>; the observed caller wrapped it in Arc::new again, producing &Arc<HashMap> instead of &HashMap. Before applying a repair, re-reading showed that the unrelated Rust work had already restored the correct initializer. The guarded patch did not apply; this agent changed no Rust source. Preserve the corrected current source and retry under a fresh build tag. Do not overwrite concurrently corrected user work or claim the first build passed.

## Persisted rejection harness: startup exit code is not sufficient

The root persistence harness initially required nonzero process exit for a request-id mismatch. Native startup correctly wrote passed=false, identity_retarget_rejected=1 and request_id_mismatch, preserved the isolated save byte-for-byte, and never started APNG admission. UE logged RequestExitWithStatus(0,1) during BeginPlay initialization but Windows process returned zero. The harness now accepts only ordinary 0/1 termination plus the explicit native refusal reason, failure report and absent runtime report; it still rejects crash statuses. This corrects the test assumption, not the migration behavior. Retain the failed harness evidence and rerun with a fresh tag.

## 2026-10-07 語音候補與好感回饋

- 提示掃描已排除視線被擋與動作未就緒，E 的另一段最近搜尋只看距離與 Visible。兩者可以選到不同 NPC。這次改成同一個 eligibility，E 當下重算，BeginTalk 再驗證一次。不是新的距離或視線規則。
- 原生語音審查：Completed 只在有 frame 且播放結束時寫入。Cancel 是 Cancelled，解碼失敗與沒有音訊裝置是 Failed，過期 epoch 不會標 Completed。Tick 原本就拒絕非 Completed。FinishLine 再查一次。沒有發現新的 runtime 音訊狀態 bug，也沒有改 OKCrowdRuntimeScene。
- 本輪工作者沒有編譯 Unreal、沒有啟動遊戲或 Editor、沒有改關係存檔。好感是否真的上升必須等 root 的五人 self-test；不要用假設的列效果或改骰子補成通過。

## Voiced-conversation completion: root diagnostics and delegation

Root ad-hoc audio verifier initially resolved a project-relative audio_root against catalogue directory and falsely listed1702 missing refs. Production launcher resolves against project root correctly; corrected verifier checked1702 hashes without error. Python diagnostics initially failed printing a heart symbol under cp950; use sys.stdout.reconfigure(encoding="utf-8"). These were verifier errors, not missing runtime audio.

Primary cancelled first Grok job run-muy84ck0-k6meyi after it announced a nested worker/future compile contrary to source-only scope. No unauthorized build was verified. Bridge stop emitted taskkill128 for an already absent PID but returned terminal cancelled; resumed run-muy8ei7f-rx9wbp complied with direct-source-only/no-build boundary. This was worker cancellation, not cancellation of the user's task.

Review diff helper initially enumerated a flattened backup filename Docs_STREET_SOCIAL_ERRORS.md added by worker and treated it as a live repository path. No source changed. Corrected helper uses the explicit five owned source/test paths from the root baseline and reviewed456 diff lines. Keep a declared file list for reviews, not arbitrary recursive backup contents.

## Candidate-selection inspection corrections
PowerShell does not expand rg positional wildcard paths; use rg -g filename patterns against existing directories. Initial guessed stage_street_social_development.ps1 was absent; discovered the actual tool Tools/stage_street_social.ps1 via rg --files. No production failure or changes resulted from these inspection errors.

Candidate review audio verifier initially chose one parent too high for the project root, so reconstructed voice hashes did not match. Corrected project-root derivation from parents[4] to parents[3]; this was a diagnostic path error, not a production audio mismatch. Failed assertions wrote no result and changed no game content.

## 2026-10-07：鏡頭候補文件用字

- 候補說明初稿把「沒有視線」寫成「沒有gaze」。probe 的 Tab 句子也可能讀成從 3、2、1 之後再多一次繞回。已改成視線，並寫明 yaw 0 時三次 Tab 的選中順序就是 3、2、1，第一次是從名單最後一人繞回第一人。這是文件用字，不是 runtime 失敗。
- 本輪工作者沒有編譯 Unreal、沒有啟動遊戲或 Editor、沒有 Cook。`python -m unittest discover -s Tools/tests -p test_street_social_*.py` 跑完 26 項，全部 ok。`Tools/test_street_social_runtime.ps1` 的 PowerShell 語法解析通過。原生自動化與六人 runtime 仍等 root，不能把這兩項檢查當成場景通過。

## Primary candidate UI focus review
The free-roam overlay language button used the default focusable SButton. With GameAndUI input, that creates a Tab-navigation routing risk for the new candidate hotkey. Primary made this button keyboard-focusable only during conversation; free-roam mouse clicks and L remain supported. Source review identified this risk; no manual GUI failure is claimed. A fresh Development/Editor binary and native runtime validation are required after this correction.

Final StreetCandidate02 functional run passed, but frame P95 8.6407 ms exceeds the 8.33 ms target. Preserve this limit in the validation record; mean 4.0153 ms alone does not establish stable 120 FPS. Final manual/performance acceptance remains deferred.

## Activity stop state and occupancy
Arrival advanced Cursor immediately; the next dwell Tick read Points[Route[Cursor]].Kind, which described the next destination instead of the current stop. Added a separate DwellPoint and release-on-expiry. Held keeps both the remaining dwell and stop, so resumed visitors recover the original activity. Activity points previously had no occupancy guard; added runtime-agent occupancy and deterministic approaching-slot arbitration. Waiting keeps its destination and does not trigger stuck-route skipping. Regression evidence pending fresh builds/native tests/live Tick probe.

2026-10-08 活動點回歸已通過：StreetActivity02/Editor02、Python26、Unreal12、Runtime-StreetActivity02 的真實 Tick 與 BeginTalk/EndTalk 暫停恢復、占用釋放、語音及保存全部成功。初版建置之後加強了場景證據，故使用全新 Activity02 執行檔重建測試，沒有將舊 snapshot 當作新版驗證。詳見 STREET_SOCIAL_ACTIVITY_VALIDATION.md。


## 2026-10-08：語音失敗殘留關係提交與提示覆蓋

程式碼檢查發現 RelationshipEvent 先設定 RelationshipPending，再交给 QueueNode。舊路徑在語音 admission 失敗時只回傳 false，未清掉 pending；ApplyRelationship 沒有完整語音門檻，可在未成功播放時保存關係。另 FinishLine 對有聲關係事件會自動提交，與原 ADV 文字事件的明確確認不同。這是源碼確認的狀態缺陷，未宣稱玩家存檔已遭破壞。

改成有聲事件完整播放後才開放確認，ApplyRelationship 與 UI 共用確認規則；EndTalk 清除所有未提交上下文。排入、播放失敗及逾時走同一 FailTalk，取消音訊、釋放角色並保留獨立重試提示。舊的失敗 Message 會被 EndTalk 與靠近提示覆蓋，因此失敗提示使用独立欄位，到下一次有效交談才清除。

實際場景測試只在記憶體中暫換一個語音參照，恢復原內容後驗證重試，不改 APNG／共用音訊或玩家存檔。刻意 admission 錯誤只在自測注入區間標記 expected；其他錯誤一律仍記錄，不能藉此掩蓋正常故障。新版本編譯與驗證結果另見 STREET_SOCIAL_RECOVERY_VALIDATION.md。

Recovery review helper initially treated the explicit backup paths as flattened filenames. The backup retains Source/Tools subdirectories, so the helper stopped before writing a diff. Corrected to preserve each declared relative path. No source or content was changed by this diagnostic failure.

Recovery01 回歸通過：Development／Editor、Python26／Unreal13、六人 runtime 及15組語音獨立比對成功。預期 admission 故障1次，沒有保存或殘留占用，可重試；正常遊戲 errors=[]。公開 CMD 已更新且 hash／ValidateOnly 檢查通過。此故障為自測注入，不是新版本的未修錯誤。


## 2026-10-08：有聲關係流程證據缺口

Recovery01 已驗證確認規則與 admission 故障恢復，但沒有完整跑 production Tick／FinishLine 至有聲預覽完成與明確保存。本輪補異步原生場景 probe，兩次播放 NPC 自己匹配的真實招呼語音作為路由 fixture，分別測取消及唯一保存；不宣稱該台詞是原作友誼事件。不修改共用 catalogue 或玩家保存；fixture 使用自己的獨立保存檔，結束還原遊戲狀態。

本轮初始搜尋再誤用 PowerShell positional wildcard，rg 沒有展開而失敗；已改成目錄加 -g，未修改程式。往後檢索應直接使用既知的 -g 方式，避免重複此檢查錯誤。

Voiced-preview harness review found the new receipt assertion referenced taskPrepared, which is only assigned in RetargetSelfTest. Corrected to read the normal prepared manifest into taskPreviewManifest before comparing participant IDs. Caught before running the new scenario; no failed game result is claimed.

VoicedPreview01 回歸通過：Development／Editor、Python26／Unreal13、完整異步有聲預覽取消及唯一確認保存實測成功。獨立檔案核對一個APNG身分、favor3、唯一交易；正常15話題另有10次正向好感，errors=[]。公開CMD／hash／ValidateOnly檢查成功，證據見 STREET_SOCIAL_VOICED_PREVIEW_VALIDATION.md。


## 2026-10-08：街區黑色地平線

上一輪實際截圖顯示街道邊緣之外一整條黑色空域。場景只有64×56公尺地塊，沒有遠景地表；SkyAtmosphere 的下半球直接露出。功能測試通過不能代表這項美術問題已解決。

修正放在通用 runtime 場景初始化：共用現有地面網格／材質建立低於主地面的無碰撞、無投影遠景，60公尺起始薄霧接天空；不依赖人物卡、不改光照／角色曝光。新截圖及功能回歸通過前不宣稱修好。

BackdropEditor01 source compilation completed, but linking the primary Editor DLL failed with LNK1104/Uba error9001 because an existing UnrealEditor.exe holds that file. The primary agent leaves the running Editor session intact and builds the same scene source in the isolated StreetSocialHost Editor target instead. Copied the declared StreetSocial test source, added Editor-only UnrealEd to host-only rules, and linked host Saved/StreetSocial evidence to the existing test data. These are generated verification-host changes, not production plugin edits or user-session termination.

IsolatedEditor01 initially failed on pre-existing crowd authoring-only includes OKCrowdCompact.h and KKImportIndependentCardCommandlet.h: the generated Development host intentionally does not contain those import/editor modules. In only the generated host copies of OKCrowdGpuPose.cpp and OKCrowdScene.cpp, exclude the unrelated crowd authoring blocks as in a Game build. StreetSocialScene and its rules/tests stay byte-identical to the primary source. No production source or user's active Editor was changed by this adaptation. A fresh isolated Editor build and test result are required.

Isolated Automation02 had12 successes and one rules fixture failure: Integration01 declares an absolute common voice-root path, while the host ProjectContentDir is a junction alias of the same content. The old assertion compared lexical project paths instead of the declared voice root. Updated only that existing integration assertion to require the actual declared voice-root fallback path and the exact expected rules file contents. Runtime rules lookup and rewards are unchanged. Fresh Development/isolated Editor and automation evidence are required after this portable-test correction; the failed index is retained.

IsolatedEditor03 compiled the corrected scene and rules test and linked the isolated OpenKoikatsu Editor DLL, but overall failed when it attempted to relink engine NetCore.dll held by the active Editor. Use UBT's documented -Module=OpenKoikatsu filter to complete only the changed project module, preserving the active engine DLL. Earlier engine/game rebuild paths do not justify closing the user's session. Initial lookup guessed TargetDescriptor.cs in the wrong directory; rg --files found Configuration/Descriptors/TargetDescriptor.cs and confirmed the supported -Module= argument. No source changed due to that lookup error.

Automation04 exited before tests because the single-module build regenerated the project module build identity but the isolated plugin manifests still had the prior build identity. Rebuild/select all four isolated project modules through UBT (-Module=OpenKoikatsu+KKImportedAssets+KKCrowdGpuPacking+KKRuntimePose) to generate consistent manifests. Do not hand-edit BuildId or disable runtime module compatibility checks. No native tests passed in Automation04; its failed launch evidence is retained.

Automation05 also rejected host modules before tests: module-only UBT reported zero actions and left all host manifests on the old identity, so selecting more modules alone did not regenerate them. Use a full isolated project build against the existing precompiled engine (-UsePrecompiled -NoUBTMakefiles), allowing UBT to generate compatible metadata while avoiding relinking a live engine. No manual manifest edits or compatibility-check bypasses were performed.

The ad-hoc screenshot metric used Pillow getdata(), which emitted a deprecation warning. Saved the reproducible diagnostic with get_flattened_data() instead; this is a diagnostic API update, not a game failure.

Backdrop02最終通過：Development02、預先編譯引擎的隔離Editor06、Python26、Automation-Isolated06原生13、六人正常15話題及保存回讀成功。實際遠景固定區域黑像素比例100%→0%，新截圖已檢視。公開CMD／hash／ValidateOnly成功。使用者執行中的主專案Editor未關閉或熱更新，新CMD獨立運作；歷次建置與啟動失敗全部保留為記錄。

## StreetInput01 建置／診斷
- 編譯失敗：`auto* Viewport=GEngine->GameViewport` 不能從 UE 的 TObjectPtr 推導 raw pointer。改用 `.Get()`，保留 StreetInput01.log，重新建置。
- 查閱本地 SceneViewport.cpp 發現 HasFocus 判斷的是 Slate 的特定 SViewport；UI 按鈕取得焦點不能當成 OS 視窗失焦。改用 IsForegroundWindow，並用 Slate input preprocessor 保證 Ctrl／中鍵不被 UI 吞掉。
- 診斷命令再次使用 PowerShell 的位置 wildcard 而失敗，改用目錄 + rg -g；不讀單行大型 catalogue 整行，改以 JSON 欄位摘要。
- 本輪測試補丁的自動替換對舊斷言字串作出錯誤假設，assert 中止、沒有改到測試檔。查明現行條件為 `-ne $true` 後修正；來源程式已寫入，StreetInput02 使用該最新來源成功編譯。
- 最後 native 診斷一度假設 automation log 在根 Saved/StreetSocial，該檔不存在；查閱 test_street_social_automation.ps1 後使用 Build/Automation-StreetInput02/editor.log。原生14項完整通過，非缺失日誌的推斷。

## 話題擴充研究診斷
- 起初猜測反編譯 Talk 目錄不存在，改用 rg --files 找到 TalkScene.cs／ActionGame.Communication。
- 探索腳本命名 inspect.py 遮蔽 Python 標準庫 inspect，造成 UnityPy 循環載入。改名 topic_probe.py；未改套件。
- cp950 無法輸出原作日文，設定 UTF-8 輸出後保留 probe.txt。
- StreetTopics01 編譯：UE5.8 FJsonObject key 為 TSharedString，不支援 IsNumeric；改用 LexTryParseString 並比對標準整數格式，拒絕前置0等別名。另 InterestValue 避免遮蔽外层 Value（C4456 視為錯誤）。
- 讀卡探索把 str 傳給 card_metadata(Path)，已改為 Path；無卡片改動。
- source Normal／Failure 是隨機選一筆，不是連播兩筆。實作保留隨機一筆，並新增稀有對話末段無語音男主文字顯示，不丟掉結尾。
- APNG 沒有原作 SaveData.Heroine.isVirgin；計畫修正為明示街區聊天門檻差異，不偽造卡片 virgin 設定。
- StreetTopics02 正式場景入場拒絕 topic_voice_missing：匯出工具把新語音寫入 RuntimeV2/ja_ogg，但宣告的共用 audio_root 是上一層 StreetSocialData。修正匯出目的地至共用根；保留失敗報告，重新匯出並逐檔驗 hash，未放寬 missing 判斷。
- 收尾審查發現拾取物原先限制於路點數，造成有些48種話題不能取得。改成每種話題一個共用物件，重用路點分散位置且避開建物，新增48點正式場景斷言後重測。

## 完整話題／觸控擴充（2026-10-08）
- 觸控資料匯出發現五種性格皆有一筆 stage2 See 的 com_ev_XX_395 不在原作抽取索引。保留明確 unavailable 與來源 gap，不能借用別行語音或吞掉錯誤；所有 Touch 行都有完整音檔。
- StreetTouch01 編譯被現有 Rust editor_mods.rs 的 rmpv::Value::as_map_mut 阻擋：此 API 不存在，改為 Value::Map 的可變 pattern match。之後同檔其它工作新增 as_array_mut / as_map_mut 導致 StreetTouch03 失敗；重讀時已修正，保留其它工作的修改。
- PowerShell python -c 中 JSON 引號被解析，造成報告插入腳本 SyntaxError，未改檔；改用 apply_patch。
- 首次 runtime 觸控自測被「語音未結束直接 return」守衛擋住，等待嘴型永遠沒有執行。修正測試 phase12 可在播放中執行；保留 Runtime-Runtime-StreetTouch01 失敗證據，不能當通過。
- 搜尋不可把 *.h / test*street* 作 PowerShell 位置路徑；改使用 rg 的 -g。原作 TopicDropData 在 ActionGame 命名空間目錄，使用 rg --files 尋找，不能猜根目錄。
- Runtime-StreetTouch02 發現 render rig 壓縮僅保留 renderer palette，原始手部語义骨骼被裁掉。通用修正是在 ExternalControl 場景保留存在的頭、雙手、雙胸語義骨骼及祖先；私人 GPU cache recipe 已包含 bone reference，骨架改變會自動失效重建，不使用舊錯誤索引。
- Automation-StreetTouch01：53條可用反應的檢查假定缺音屬 See，實際查表是 stage2 Touch 臉頰第二次（part3 / repeat2 / row347），五性格均缺 com_ev_XX_395。修正測試與文件，該行維持明確不可用，不替換其它語音。上面的 See 描述是初查誤判，以本筆為準。
- Runtime-StreetTouch03：第一筆真正觸碰語音／效果成功，但第二筆被拒絕。原因是自測fixture先耗掉四次話題，剩下一次交談額度，第一筆觸碰後已歸零；正式額度門檻正確。修正僅重置隔離觸控fixture的交談額度，不放寬正式遊戲門檻，不動使用者存檔。

## 缺音恢復審查（2026-10-08）
- 原始 c00/c18/c17/c27/c37 的 adv/00.unity3d 各有874/899/894/888/749個 AudioClip，均缺 com_ev_XX_395；394與396存在。不是抽取索引遺漏。
- 原先缺音即 return 且部位計數不變，造成戀人第二次摸臉頰永久停在缺音索引。保留不可用反應並僅前進會話次數，無語音時不提交任何效果。
- 本輪讀測試檔時猜測 test_street_social_topics.py 不存在；改用 rg --files，native 規則測試為實際入口。
- 審查時發現新隔離測試把 Relations 誤當 TMap；它是 TArray，編譯前改用既有 OKStreetRelationIndex，未發布錯誤版本。
- StreetTouchEditor03 隔離 build 使用相對 uproject 路徑；Build.bat 改變工作目錄後 UBT 找不到專案。改傳完整絕對路徑，保留失敗 log。查 runtime log 前先列出实际檔案，避免猜名稱。

## UI／觸碰事件審查（2026-10-08）
- 場景 Tick 僅看 PC 左鍵，沒有排除 Slate 面板，可能同次點 UI 又觸發位於其後的女角部位。改在輸入預處理時記錄實際面板命中，場景路徑拒絕该次輸入。
- 缺音雖未停止 speech，却將提示寫進 S.Message，會蓋掉正在播放的女角字幕。新增獨立通知並驗證聲音／男女字幕與未提交話題不變。
- 操作指南保留舊版右鍵鏡頭描述，最新應為FPS游標鎖定＋Ctrl／中鍵切換，須同步維護。

## 等待答案操作審查（2026-10-08）
- ToggleLanguage 只檢查 Node，女生出題後 AwaitTopicAnswer=true / Node=-1 時仍能換 catalogue 並把題目提示蓋掉。以完整交談忙碌狀態拒絕函式與UI語言切換，不改變進行中的交易。
- 結束按鈕以 Node／RelationshipPending 顯示文字，漏 CollectedActive，等待答案時顯示道別但 Farewell 實際取消。統一標籤與出口決策。原取消本身可用，不能把它誤報為無法離開。
- L／1～4／N 路徑未像移動與搭話一樣檢查前景視窗，補 Focused 守衛，避免背景時誤換語言、選話題或換天。


## 2026-10-08 世界與中文擴充

- 查找路徑先前猜測了不存在的 OKStreetSocialActivity.h、OKStreetSocialEnvironment.h 及 Manager/Action.cs；實際行為與環境定義在 OKStreetSocialRules.h，原作流程在 ActionGame/ActionControl.cs。後續先 rg --files 再讀取。
- WaitPoint 不在 map/scene 的場景 Bundle；它位於 map/waitpoint/00/<場所編號>.unity3d，且 MonoBehaviour 的 m_Name 為空。只讀有名稱物件會漏掉定位、道具及服裝設定。已改成依 parameterList/MapNo 辨識，匯出 420 筆。
- 既有 comm_zh 並非全部中文：例如貓咪、浮潛、花園鰻仍留日文，既有告白還遺失 [H]。中文檢查不能只計算非空字串；新增假名與姓名標記檢查、精確補譯。
- 首次假名範圍把「・」標點誤判為日文；改用實際平假名、片假名字母範圍，保留原文姓名代換標記。
- 原作場所有六個無獨立場景資產的嵌入設施，不能因無 asset 就忽略，也不能宣稱每列已有獨立可玩地圖。world.json 保留父場所與 pending 狀態。
- 三份新設施動畫目前僅已輸出共用曲線；未完成道具、定位與實際畫面驗證前，不加入公共播放清單冒充已完成活動。

- StreetFacilities01 的 UBT 長時間等待全域互斥鎖，僅停止本任務明確啟動的 PID 73724。隔離 Host 使用 -NoMutex -NoUBA，未中止使用者其他建置或 Editor。
- StreetFacilities02 誤把 Editor 適用的 -UsePrecompiled 套到 Development Game，缺少 ImageWrapper 等 precompiled manifest，建置失敗。Game 改回正常模組連結；不修改引擎模組或偽造 manifest。
- StreetFacilities03 因切換前一個 precompiled makefile，UHT 產物行號 54 與新頭檔 55 不一致。StreetFacilities04 使用 -NoUBTMakefiles -ForceHeaderGeneration 重建，24.95 秒成功。
- build_street_social_world.py 兩次直接覆寫返回 Invalid argument，ACL 與可用磁碟正常；未確認根因。相鄰暫存檔完成後原子替換可成功，未變更權限。
- 設施動作不能只看 Playing：舊片段或正在準備的片段也可能有播放狀態。開始計時與顯示餐具必須同時符合正確 ClipId 且沒有 Pending/Preparing。

- StreetFacilities04 的截圖請求之後立即銷毀房間，可能拍到返回街區而非動作畫面。StreetFacilities05 在截圖請求後保留設施至少 0.5 秒，三張實際截圖已檢查；不得用檔案存在取代畫面驗證。

## 2026-10-08 共同活動調查
- PowerShell 下把 `Docs/STREET_SOCIAL*` 當 rg 的位置參數會失敗；改為 `rg ... Docs -g "STREET_SOCIAL*.md"`。
- 原有設施僅女孩播放動作，不能稱為共同活動；需要兩者實際參與的流程與證據。

## 2026-10-08 合照擴充
- 再次遇到 rg 位置參數含 `OKCrowdRuntime*` 的 PowerShell 萬用字元錯誤；只用實際目錄加 `-g` 篩選。
- 擴充共用動作上限時，原有數字鍵陣列只有 8 個，必須同時限制鍵盤迴圈，避免超界；共用曲線數不等於 GPU 常駐 action slot 數。
- 讀取猜測的 test_street_social_selection.py 失敗；先 rg --files Tools/tests 找實際測試檔，不猜檔名。
- 男女骨骼名稱不同，男角頭部查詢先 cm_j_head，再兼容 cf_j_head；不能將女角骨名當唯一來源。
- PairedPhoto01 被 appearance.rs 的 rmpv API 編譯錯誤阻擋；只將兩個不存在的 as_map_mut/as_array_mut 改成 Value::Map/Array 可變模式匹配，保留新外觀功能全部篩選規則，不跳過 native build。修正需重新編譯及場景驗證。
- staged Development 的 ProjectSavedDir 不等於工作區 Saved；照片目錄改由已解析的關係存檔父目錄推導，正式啟動落在工作區 Saved/StreetSocial/Photos，測試則落在獨立測試輸出。
- 加入「按拍照後立刻結束」production probe，必須等待照片寫完才恢復角色/相機，避免保存成已離開的畫面。
- 第一位合照成功後，第二位等待男角 ready：已安裝的相同動作 prewarm 回傳成功卻未設 StreamHold，會在等待女角時被 idle 回收。通用 API 修正為命中既有 bank 時也 pin StreamHold，直到 play/cancel 釋放；不能用每張卡重建或延長超時掩蓋。
- Runtime-StreetPairedPhoto02 的第一張圖片已保存，但當幀 TickPhoto 恢復 NPC 後，日常 AI 立刻再次改動位置，觸發恢復檢查。改為記錄恢復 frame/actor，當幀跳過其 AI step，下一幀正常運作；並新增隔離合照 smoke 模式，成功後仍需完整場景回歸才發布。

### 2026-10-08 坐談來源調查
- Unity AnimatorController.m_TOS 的 UnityPy 輸出是 `[hash, name]` pair array，不能以 first/second 字典讀取；改用 dict(m_TOS)。此錯誤只影響調查腳本，未改來源或執行資料。

### 2026-10-08 StreetSeatedSmoke01：長動作超出準備上限
- 33.333 秒的 ks_f_talk_00_00_00 在 60 Hz、角色私有骨架下超過預設 128 MiB key 預配置上限，log 記錄 Runtime pose keys exceed preallocation budget；male 已完成但 female 未 ready，probe 正確失敗，沒有發布。
- 通用修正：僅按需 streamed action request 提高至 512 MiB 上限，仍最多兩個準備 slot，保留既有 4 GiB GPU residency manager 拒絕/回收與分塊上傳。初次 idle/walk 預算不變，不降低動畫頻率、不裁短片段、不依卡片或硬體特調。
- 同步檢查發現 streamed bank 組裝另有 128 MiB 上限，不能只提高 pose request。組裝包含 idle/walk 與按需 clip，必須同步保留有界餘裕；以 768 MiB 作組裝上限、512 MiB 作新 clip 上限，最終仍受 4 GiB GPU 共用預算控制。
- 調查命令再遇 PowerShell rg 位置參數萬用字元（OKGpuAnimation*），應固定用目錄加 -g；未影響檔案。

### 2026-10-08 StreetSeatedSmoke02：流程通過但座位/鏡頭不合來源姿勢
- 五位女角坐談語音、雙方 private actions、queued cancel/retry、active speech cancel、存檔與相機/人物恢復通過，但實際 screenshot 顯示來源是坐地姿勢。原本以 chair 40cm/table 62cm 加 standing camera 135cm 會遮住角色且取景太高，不能以程式斷言通過當作畫面完成。
- 改共用地墊/23cm矮桌；坐談相機由男女實際 private head 骨骼決定 focus，保留原坐姿且避免為單一卡片寫死位移。probe另存無UI畫面，重新看圖後才發布。
- 曾猜測不存在的 test_street_social_receipt.py，已改 rg --files 確認實際測試檔，未影響功能。

### 2026-10-08 StreetSeatedSmoke03：男女來源坐姿類型不同
- 無UI截圖確認男角 m_suwari_00_03 是椅坐、女角 Talk10 是地坐，不能把兩方都當坐地。這是來源動作配對錯誤，不是卡片特例。改女角使用原作 Appearance3 / f_suwari_00_00_M 椅坐，與男角椅坐配對；33秒Talk10留作之後女女地坐活動來源，不冒充男女配對。重新驗證畫面。

### 2026-10-08 StreetSeatedFull01：重複招呼並非永遠可選
- 前面的正常對話/話題/觸碰/設施/散步/合照已經進行，當日狀態使 greeting 不再 eligible；入座後強制 Topic(greeting) 讓 seated probe 等不到語音完成。專用空存檔 smoke 沒涵蓋這個狀態，因此完整回歸不可省略。
- 通用修正：入座只是延續原交談，不自動重複招呼或消耗話題；玩家仍從原條件的選單選擇。坐談 probe 用 fresh relation 的 personality-matched 原招呼作明確音訊fixture，透過相同 production QueueNode/FinishLine 測音訊完成/中斷及姿勢保留，不修改真實 Store 或 eligibility 規則。

2026-10-08 drink研究：一次 rg 再次錯用 ActionGame* 作為位置參數，PowerShell 不會展開成目錄；已改在 Assembly-CSharp 真實目錄下使用 -g 篩選。不得把查無結果當成來源不存在。

StreetDrink01 Development 編譯失敗 C4458：局部 Role 名稱遮蔽 AActor::Role；改為 Participant，不關閉警告。喝飲料相機也改用雙人頭部中心及負向側移，避免左側 UI 遮住男角。

StreetDrink02 編譯：全檔 Role 字詞替換誤改既有 D.Role 成 D.Participant；還原既有分支資料欄位，之後改名必須限制在新增作用域並檢查全部命中。

StreetDrinkSmoke01：數值 probe 可通過但實際畫面男角 ks_m_action_02 蹲下，不是飲料動作；playeraction28 是取得果汁的動作，不能以項目名稱判定姿勢。未發布。來源需再核對實際動作。原飲料幾何根掛點為瓶底（子節點 UnityY +0.1066，Z長軸旋轉到Y），目前中心放掛點造成手在瓶頂，須修正為中心沿掛點向上約10cm。

Source Studio info 追加確認：ks_m_action_02 = 自販機；f_tachi_00_09 = 飲む待機，f_tachi_00_10 = 飲む。修正曲線使用 actual drink10；原作無已確認男角專屬喝飲料曲線，改為 common standing drink curve 在男女私有骨架各自求值並視覺核對。另一次 rg 查詢誤帶 runtime_pose* wildcard，已略過不存在參數並在 src 真實目錄查詢。

StreetDrinkSmoke02：修改飲料中心為掛點+10cm後，probe仍錯誤要求中心=掛點，報 detached。斷言改為骨骼完整 TransformPosition(0,0,10)，不放寬容差。未發布失敗證據。

Drink probe靜態審查：加入語音等待後，手部移動斷言若每幀與初始位置比較，循環回到相同姿勢會誤報。限制在第二張畫面之前的兩個指定時間點比較；其餘每幀仍驗證道具對當前手部 transform 無偏離。

StreetDrinkSmoke04 實測確認上述循環誤報：['Drink private hand/prop did not animate']。新版 StreetDrink08 已限制動作差異檢查到兩個指定時間點；未發布這筆失敗報告。

書本道具研究初次查詢有不存在的 Layout.h 及 OKCrowdRuntime* 位置參數，改用真實 Source/OpenKoikatsu 目錄與 -g。一般 read 正則會誤命中 Thread/ReadPixels，改成明確 clip 名稱及 book/reading 字詞。

StreetBookSmoke01：書本已建立並通過掛點斷言，但拍照 probe 只查男角 cm_j_head，該runtime骨架使用 cf_j_head 而失敗；與坐談相同改查兩種合法頭骨名稱，不猜測性別決定骨名。

本輪來源定位：AI.cs 位於 ActionGame.Chara 子目錄，不能假設 Assembly-CSharp 根目錄；先 rg --files 定位再讀取。

StreetSplash01 匯出：Studio f_action_00/f_action_01 有同名多資產，原始名稱不是唯一可選 label。追查 summer 控制器引用確認 throw pathID=5863301320968985774、receive=3247649526348432186，應使用 [summer] 限定名稱。刪除本輪未發布的兩條男性中途輸出後，重新完整匯出並產生來源清單；不能跳過來源驗證直接認可既有檔。
另一次 rg 誤用 anim* 位置參數；後續一律指定 src 目錄加 -g 篩選。

潑水初版水滴使用3秒循環，來源四條曲線實際均為3.333333秒，長時間會錯拍。改從已驗證來源 pathID 讀取時長寫入共用 manifest，runtime 依共同週期產生水花；原曲線沒有 AnimationEvent，水花相位仍明確標記為自製。來源時長不同即拒絕匯出，避免默默失同步。StreetSplashSmoke01 十種角色分工已通過；修改週期後重新跑 smoke/full 才發布。

StreetMinimapSmoke01 啟動斷言：小地圖路線閉環寫成 Path.Add(Path[0])，UE TArray 禁止以自身元素引用作 Add（可能重配置使引用失效）。改為先複製 First 再追加；不能因 Python 檢查通過就認定 C++ UI 可運作。另將動態 Slate 小地圖標記 volatile，避免快取 paint 導致位置/好感停更。重新建置並跑實景後才發布。

StreetMinimapSmoke02 功能通過，但截圖發現三個靠近的女孩標記/編號重疊。加入只影響地圖螢幕標記的通用排開規則，保留原始位置錨點與連線，不改人物世界座標；再次測試畫面後發布。

StreetMinimapFull01 在執行途中收到版面修訂要求，已驗證 PID/executable 精確屬於本輪 StreetMinimap04 測試後停止該測試；這筆中斷結果不作發布證據。未停止使用者Editor或其他遊戲。改為左上地圖/右側直排可捲選單後另建新版本重跑。

StreetMinimapFull02 因使用者新增姓名/開關需求而中止，已先驗證其PID精確為StreetMinimap05測試執行檔，未作正式發布證據。下一版需重新完整回歸。

StreetMinimapFull03 的新姓名/開關/左上右側probe通過，對話截圖發現右側400寬造成原標題（姓名/好感/關係/次數）截斷；補標題自動換行。已驗證並停止僅該自有測試，改用最終新版重跑，舊Full03不作發布證據。

2026-10-08 小地圖：原避讓會移動人物標記，造成位置誤導。改為固定標記，只排開文字並用引線連回；以實際字寬及矩形碰撞處理，加入五人同點/地圖左右邊緣測試。Full03/04 因需求更新中止，不作發布證據。

2026-10-08 共餐調查：PowerShell rg 位置參數誤用萬用字元、猜測不存在的 world_resources 檔名，均只影響唯讀搜尋；改以 rg --files 與 directory + -g 查證真實路徑，未改動遊戲資料。

2026-10-08 StreetMealSmoke01：流程測試通過後的實際截圖顯示餐盤與桌子同木色且沒有食物，辨識不出用餐。修為明亮餐盤＋三塊食物，餐具/桌椅/餐盤/食物總27道具建立完整才啟動；左右湯匙兩部分均檢查跟隨私有手骨，取消清理MealHandProps。01不發布，02重新實景回歸。

2026-10-08 共餐材質檢查：02誤將共用材質7視為淺色，實際原材質表是深棕；正式發布前查表改用3奶油色餐盤、2紅色配菜。只停止經PID/完整exe路徑驗證的Meal02自有測試，Full01不作發布證據。使用已有Cook共用材質，未修改卡片或原Editor。

2026-10-08 StreetWatermelonSmoke01：0筆道具update時檢查木棒造成false failure。TickPhoto第一個play frame會先啟動並return，道具下一次Tick才定位；probe改等待至少0.25秒，保留實際私有骨骼距離檢查，未放寬位置誤差。同時修正打西瓜按鈕誤沿用潑水標籤。01不發布。

2026-10-08 StreetWatermelonSmoke02：十組私有姿勢/跟手/取消流程通過，但命中0、最近距離139–157cm，不能當成打擊成功。實際圖顯示Leisure8一直高舉棒，改驗證Leisure9 ks_f_tachi_05_M（14.666667秒），保留真實碰撞而非強制命中。眼罩錯掛頭中心落到嘴部，原作Item.cs證實type15=a_n_megane，改用私有眼鏡掛點。調查時再出現rg位置參數萬用字元錯誤，已改root directory搜尋；不影響資料。02不發布。

2026-10-08 Smoke03仍0命中。比對原始木棒Mesh AABB：UnityZ中心20.056cm、長61.883cm、端點50.997cm；初版80cm自行假設錯誤。西瓜原半徑10.9cm、根offset12cm＋mesh中心0.832cm，改按原尺寸/局部軸轉換。保留最近距離及selftest六軸/最低手高診斷，不強制命中。調查又有一次rg位置參數glob錯誤，重申只用目錄＋-g。03不發布。

- Watermelon trajectory diagnostic: a repo-relative Python path was used with the Rust subdirectory as workdir; the edit failed and cargo then selected zero tests. Correct the working directory and require a nonzero executed test count; zero-test success is not evidence.

2026-10-08 Smoke04 原尺寸/六軸診斷仍十組0命中，最低手高113–132cm。舊focused gate僅檢查生命週期，曾輸出passed；這不能證明打西瓜功能成功。已把工具驗收改成十組均實際接觸（每組距離<12.4cm、總命中至少10），04不發布。

Trajectory probe first native run failed because source open does not initialize body_shaped until apply_shape. Follow existing native Animator tests: initialize shape before reading the shaped baseline. No runtime behavior or source cards changed.

2026-10-08 Smoke05已依實際duration覆蓋完整片段，仍0命中。唯讀native來源/匯出軌跡對照通過，來源本身的棒端一直高於地面。改採通用旋轉接觸約束，在Animator helpers之後、物理之前求解，並納入私有GPU pose快取identity；這是新增校正，不宣稱原作帶命中機制。初版1.3m目標對Alice約束後仍差39.6cm，原固定距離不適合這段動作/身形；新場景改80cm目標，仍以真實12.4cm碰撞及關節旋轉上限驗證，不放寬collider。不使用cf_j_waist01（非此掛點祖先）；未通過的原生probe不作成功證據。

Point-contact單元測試：48迭代＋Unity FromToRotation平行死區無法通過2mm可達目標誤差門檻。使用接觸求解器專用atan2小角旋轉、96迭代上限；保留既有FinalIK原作函式，不放寬測試。

StreetWatermelon06 build: isolated host keeps copied KKRuntimePose sources, unlike its Tools junction. Copying only six app files left an old request header, causing ContactRecipe C2039. Synced the three changed plugin files and rebuild; no failed executable staged/published. Include changed plugin files in source/host hash verification.

StreetWatermelon06實景：實際命中已出現，但分裂果肉原offset Y=3cm被綠色球殼掩埋，兩片殼X±9cm也互相重疊。改X±16cm並把紅色切面放到各自外側Y±9.5cm，維持原7道具。女孩揮棒時原固定男主鏡位遮住女孩，改雙人中心＋揮棒者外側前方鏡位；需新版完整實景確認，不以被遮住的圖作外觀通過證據。

Watermelon視覺檢查：兩片殼初版27×17×23比原整顆21.8cm更大；改原寬高、半深度的簡化切片，紅褐切面只沿用共用StreetMaterial02（原本是磚紅色，不宣稱鮮紅果肉貼圖）。未猜測不存在材質檔；唯讀查找時兩次誤猜Tools/材質實作檔路徑已改rg --files查證。

2026-10-08 最終回歸準備：查詢舊 drink_female/drink_male 檔案時收到不存在；核對 drink.manifest.json 後確認先前已改為共同 drink_standing.okanim，並非新資料遺失。後續應先讀目前 manifest/檔案清單，再查資源；不還原已淘汰的錯誤來源。

2026-10-08 回歸期間誤猜 test_street_social_minimap.py 名稱；檔案不存在。已改先 rg --files Tools/tests -g '*street*' 列出實際檔案，38項街區測試完整通過；未將不存在的測試當成成功。

2026-10-08 Contacts調查：PowerShell rg positional通配路徑再次造成os error123；改查已列出的實際檔案，後續一律目錄＋-g。舊Simulation.h不存在，已定位Agent／BlockedPoint在OKStreetSocialRules.h，未據猜測修改。

2026-10-08 ContactsSmoke01生命周期通过，但实际截图显示lover没有开场。查catalogue来源证明Introduction fallback在stage2用id0；旧AppendConditionObject把任何id0等同Speak永不播放。仅对context=introduction、rule=fallback_shuffle_pool、support=supported的id0接受，普通Speak id0仍never。新增两者区别及五人真实恋人开场完成门槛；01不发布。并在进入一对一时收起过长名單。

2026-10-08 使用者指出每人重複三操作造成UI冗長。改為可選單列名單＋單一操作卡，增加目前選人、不允許原因及收合群組；測試要求5列／3操作而非15操作，選人不得改交談對象。Full03因使用者UI修訂主動停止；已核對PID51772與StreetContacts03.stage完整exe路徑後僅停止自己的測試，無關Editor未動。該次不作發布證據。

2026-10-08 ContactsFull04視覺檢查：共用操作卡已正確，但折疊三角字元缺字、戀人仍顯示無效的友誼／告白、私人聊天結束回到看書待機。改ASCII收合標記、戀人隱藏已完成的升級入口、一對一使用共用說話／聆聽待機；經PID/exe確認只停止自己的04測試重建05，04不作最終發布證據。

Full05：打西瓜第十案例準備41.58秒、動畫播放13.04秒時被錯誤當成準備超時。Probe原本把準備與播放相加套55秒；改為只在尚未播放階段檢查準備55秒，保留實際接觸、完整播放、截圖與退出還原斷言。未發布失敗證據。工具搜尋再次誤用位置萬用字元，改回目錄加-g。

Full06截圖發現右側ScrollBox長文AutoWrap未依400寬穩定換行，補明確360文字換行寬度；核對PID/exe後只停止自有Contacts06測試。06不算通過、不發布。

設施生命週期檢查：原流程先搬人物再準備，使用也被RequestedAt60秒與20秒強制結束。改為背景prewarm後才搬入、準備與使用分離、手動退出與鼠標快照。調查原作時誤對minified catalogue使用rg造成巨量輸出，後續改Python解析並摘要，不再全文搜尋該JSON。

後續來源調查把ActionGame.Point誤拼成ActionGame/Point，讀取失敗；已停止猜路徑，後續只依rg --files回傳的實際檔名。未改任何來源或遊戲功能。

## 活動邀請列解析漏項（2026-10-08）
核對原作 Info.LoadData 發現 place_heroine_rows 漏掉朋友 Event 0..3 每組的 CreateSelect(2)，造成朋友後段及戀人列整體前移12列。不能由語音名字猜活動。已補上結構，新增來源列回歸檢查。既有公開版 catalogue 不在未重測時直接覆寫。

活動匯出第一次寫 JSON 時 asset_path 是 pathlib.Path，造成 JSON 序列化失敗；修正為字串。未產生半份 catalogue；只在全部翻譯／語音完整時輸出。

第一次邀請 runtime smoke 在載入時拒絕 category：以泛用字串取代新增 category 時誤改到 TopicCommitsFavor，ApprovedCategory 未修改。已改用完整函式錨點，恢復邀請不得提交好感度；載入失敗報告加入實際 parser warnings。另外 GameAssist 在 Manager 子目錄，應先 rg --files 再讀取，不猜檔名路徑。

共用資料全面來源核對發現 RuntimeV2 1230筆有415筆階段／用途標記因漏列不符。新建 RuntimeV3，不覆寫舊版。觸碰資料重建後原先被當作來源缺音的第二次臉頰反應其實是錯列；完整源資料848音檔、gaps=0。缺音恢復測試改為明確隔離fault fixture，不能依賴錯列造成的缺音。原 StreetInvitationFull01 在擴大發現後只終止已驗證exe路徑與PID的自有測試，不算通過；Editor不動。

## 邀請交易持久化缺口（2026-10-08）
原本 InvitationAttempts 僅存本次交談，EndTalk 就清除，且邀請未扣額度。改用 APNG 穩定身分的持久冷卻與原子提交，接受／拒絕都扣一次，取消不扣。同行不應套用吃飯的重複限制。原作 PopInit 是 NPC 配置初始化，不可誤稱純每日重置。
本輪搜尋再次誤把 OKStreetSocial*.inl 當成 rg 檔名，Windows 拒絕；後續使用目錄配合 -g 篩選。

本輪回歸進度曾把9張打西瓜截圖當成9組案例；每組有動作／接觸多張圖，已在回報中更正。最終完成數一律讀取 watermelon_cases_verified，不能由PNG檔案數推算。

## 手機活動來源查核（2026-10-08）
初查誤猜 world.json 在 StreetSocialData 根目錄，實際在 RuntimeV2；後續先 rg --files 確認。手機WaitPoint type8是左手 k_f_handL_00，不能沿用飲品右手。

手機整合腳本第一次使用不存在的回歸分支完整字串，assert中止；Scene檔案在全部替換驗證後才寫入，因此未留下部分修改。改用實際else-if分支錨點後完成。

## 自主手機開發：PowerShell 搜尋路徑
`rg Source/OpenKoikatsu/OKCrowd* -g *.h` 在 Windows 不會展開目錄參數，回傳錯誤 123；改用已存在的 Source/OpenKoikatsu 目錄並用 -g 過濾。勿把失敗搜尋當作不存在。

自主手機測試設計檢查：DwellPoint=INDEX_NONE 會被正式移動更新改成 Waiting，無法觸發 Resting/Looking 的排程。改用真實 Rest waypoint，保留正式移動／排程流程，不以測試專用強制播放繞過。

搜尋測試檔時不可猜測 test_street_social_receipt.py 存在；實際沒有此檔。先 rg --files Tools/tests -g *street* 取得現有測試清單。

自主手機整合檢查：戀人電話按鈕原以 Held 一概停用，即使 CallLover 已能中斷手機，UI 仍不可點。改成允許自主手機擁有者，其他前景預約維持禁用；重新建置並重跑，舊版回歸由主代理核對本次測試 PID／exe 後中止，不計成功或功能失敗。

## 通用自主活動整合檢查
手機在左手原點，飲料在右手上方有 10 公分偏移；不可沿用手機的道具位置等於手部原點斷言。已把檢查改為共用活動描述的 Transform 結果，並保持手機／飲料各自測試旗標與發布證據，避免把飲料通過誤記成手機通過。原作 Drink 掛點 type9、資產 p_o_koi_ei_cup00 已核對，畫面道具為本專案共用自製飲料杯。

共用排程改名預防紀錄：內部 AutoPhone 改為 Ambient 時，公開 StreetSocialAutoPhoneSelfTest 與 auto_phone_* 證據欄位維持相容；飲料使用獨立 AutoDrink 旗標／證據。不要以全域取代破壞既有 CMD／驗證工具契約。新版手機與飲料專項均已通過。

## 2026-10-08 自主自拍整合注意事項

- 從手機共用資源產生器延伸自拍時，不能沿用「男女都已驗證」的來源敘述；本項只讓女角使用原作 Shooting 站立循環 f_tachi_00_07_00。已移除未使用的 male 欄位、改正 manifest provenance，避免把共用曲線誤稱原作完整自拍控制器。
- 自拍使用原作 Shooting WaitPoint 的 type8 左手手機；不能沿用飲料 type9 右手。獨立檢查同時核對原作狀態、掛點、曲線SHA與五人實際私有GPU admission。
- 專項報告的共用計數必須配合 auto_selfie_checked／only_test；不得把自拍計數當成手機／飲料重驗通過。

- 發布檢查曾只綁定專項測試的執行檔，完整回歸報告沒有核對實際 Base Directory，可能誤混舊版回歸證據。本輪補上完整回歸 game.log 的精確執行檔路徑檢查；舊版完整證據搭配新版執行檔必須被拒絕，不寫 receipt。

- 本輪文件搜尋再次遇到 PowerShell 下將 `Docs/STREET_SOCIAL_WORLD*` 當作 rg 的位置參數，產生路徑錯誤。往後使用 `rg -n <pattern> Docs -g 'STREET_SOCIAL_WORLD*'`，避免把 shell glob 當實際檔名。未影響遊戲資料或測試。

## 2026-10-08 單次無道具活動整合

- 上輪新增完整回歸執行檔綁定後，沒有在該修改之後重跑47項測試。本輪發現4個負向測試因先讀不存在的game.log而拋出FileNotFoundError，蓋住原本應有的未完成活動證據錯誤。修正為先檢查功能完整性，再檢查來源執行檔；缺log改為明確ValueError，保留精確執行檔綁定。修改驗證工具後也必須重跑其測試，不能引用修改前的通過紀錄。
- 本輪早期搜尋又使用Windows rg位置參數glob；已更正為目錄加-g形式，後續不再使用該寫法。
- 整理儀容來源為非循環，不能硬播八秒或在無道具時索引Props[0]；共用排程需以實際片段長度／完成狀態處理，測試也必須按時長採樣。

- 整理儀容發布前另核對自然完成缺失與錯誤執行檔SHA會被拒絕且保持舊receipt；新增不依賴Saved歷史測試資料的兩項發布回歸，避免既有測試被skip而漏檢。

## 2026-10-08：移動修正查找紀錄

- 查找時猜測不存在的 Simulation 檔案，並曾將 Windows positional wildcard 傳給 rg。改為先 `rg --files` 確認檔案，目錄搜尋使用 `-g`。
- 女角限速不能只套用走路方向計算；電話、跟隨與交談也會設定方向，必須在唯一的 runtime transform 提交處限速。舊「一幀立即對齊」測試同步改為驗證最短路徑、角速度上限與已提交方向。

- 鋪面邊緣測試不能選到原本就放有長椅的位置；四邊取可通行區段，並另外驗證建築牆體仍阻擋，避免把家具碰撞誤判成地板問題。

- Movement02 原生測試 passed/errors=[]，PowerShell 卻誤判：C++ float 0.2 寫入 JSON 是 0.20000000298023224。跨語言浮點參數檢查使用 1e-5 容差，角速度使用小容差；不能直接用 `-ne 0.2`。

- 限速套在最終提交點後，雙人與設施動畫不能在目標角度尚未到達時開始，否則會側坐或穿家具。兩條活動準備流程皆等待實際提交方向對齊，再播放並顯示道具；仍保留原本 60 秒準備上限。

- Movement03Full 啟動 log 仍有 `Failed to load premade asset registry. LoadResult == 1`；已比對 StreetAmbientGroomFull01 基線也有同訊息，並非本輪鋪面或轉向改動新增。保留原始 log，不將它當作本輪已修復項目；角色載入及功能結果另依原生 validation 與 runtime JSON 判定。

## 對話表現調查
- KKS 反編譯資料夾使用 `ActionGame.Chara.Mover` 等帶點名稱，不能猜成 `ActionGame/Chara`；先 rg --files 查實際路徑。
- `f_tachi_00_02` 是看書、`_04` 是手機，不能因名稱 tachi 就當成無道具對話站姿。

- Presentation02 測試編譯曾誤用不存在的 OKStreetReadSave；既有 API 是 OKStreetParseSave / OKStreetLoadSaveFile，回傳 EOKStreetLoad 必須明確比對 Ok。
- 程式編輯搜尋不可依賴猜測的縮排空白；前兩次批次編輯在寫檔前找不到字串已中止，改以已驗證的實際 token 定位。

- Presentation05 移除滑桿暫存草稿後，名單切換仍引用 DebugDraft/DebugSlot，導致編譯失敗；已同步移除舊草稿清理，名單切換直接重新綁定目前女角。
- SSlider 在 UE5.8 使用快取的 Slate attribute；資料改動後要等下一個 Slate frame 才會更新 GetValue。專項測試分幀檢查六欄位與外部資料改動，避免在同一個遊戲 Tick 讀取舊快取造成誤判。數值本身仍於 OnValueChanged 當下保存及套用。
