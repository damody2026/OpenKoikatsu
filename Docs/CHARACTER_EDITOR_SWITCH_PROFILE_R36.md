# 飾品切換 5.15 秒追蹤 R36

1. 以 R35 原始交易時序及完整 log 建立時間線，區分序列階段與內含子階段，避免重複相加。
2. 使用相同 reset 卡片與實際飾品操作，啟用既有 OK_LOAD_PROFILE 及 CPU/frame/bookmark trace。此為診斷標籤，不作正式效能通過證據；不改 shader、GC、畫質或採樣門檻。
3. 以 UnrealInsights 匯出事件，定位交易、模型重新開啟、全角色顯示建立、材質／渲染就緒及最後套用。只輸出有證據的原因，未量到的成本標明未知。
4. 保存二進位雜湊、環境快照、來源 receipt、交易時序及分析腳本／報告；記錄問題到 CHARACTER_EDITOR_ERRORS.md。

初步 R35 證據：交易 snapshot 11.069、queue 0.031、input_save 241.538、native_request 968.976、output_read 177.017 ms，序列合計約 1398.631 ms。load3d log 14:44:38.903；所有部件完成 14:44:39.741；材質／渲染 ready 14:44:42.617；load3d result 14:44:42.650。部件建立後等待約 2876 ms 是待確認主要區間，不直接稱為 shader 編譯 CPU 時間。

## R35 原始 5154.425 ms 的時間線

以下為互不重疊的 wall-clock 區間；log 時間精度 1 ms，第一段含驗收 tick 邊界的少量差額。自動分析保存在 EditorInventoryExtraAccessoriesR35/switch-profile.json。

| 區間 | ms | 占總切換 |
|---|---:|---:|
| 點選至開始重新載入 3D（主要是卡片交易） | 1407.425 | 27.31% |
| 開啟新 session、預備資料並建立全部顯示部件 | 838.000 | 16.26% |
| 所有部件建立後至材質就緒 | 2876.000 | 55.80% |
| 材質就緒至最後套用完成 log | 33.000 | 0.64% |
| 總計 | 5154.425 | 100% |

第一段內部的交易計時：snapshot 11.069、排隊 0.031、輸入卡片另存 241.538、原生請求 968.976、結果卡片讀取 177.017 ms，序列合計 1398.631 ms。原生請求內含來源擷取 437.924、輸出 outfit 打包 91.485、PNG 寫入 108.398 ms，不能再將這些子階段加到 968.976 ms 之外。

程式證據：ApplyAppearanceItem 成功後一律 StartLoad3D；原生只修補一個飾品、重用其他 34 部件，但 UE 仍重新開整份 session、建立所有顯示及材質。TickPreviewPreparation 完成所有部件後才呼叫 PreparePlaybackMaterials(false)，只要任一 active material shader map 未完成便回傳 false、下一幀再查；之後才送渲染 fence，完成後換上角色。因此「非同步」讓等待期間仍能跑畫面，但不會自行縮短點選至顯示的整條序列。

OK preview material readiness 的 wait_ms=0.031 是最後一次成功輪詢花費，並非從首次查詢到就緒的 2876 ms。initially_compiling=0 也是最後成功輪詢的狀態，不能拿來推論之前沒有等待編譯。R36 trace 將分辨實際主執行緒工作、shader 背景處理及等待時間。

## R36 CPU trace 已完成

相同卡片、同一個飾品操作的診斷切換為 4593.920 ms。此輪開啟 OK_LOAD_PROFILE 與 CPU/frame/bookmark，不能替代 R35 未開追蹤的正式 5154.425 ms。兩個 DLL SHA256 均與啟動一致，啟動／結束環境快照無其他 UnrealEditor；這仍不是全程程序監控。8 項功能案例成功，效能驗收照常失敗，未放寬 300 ms／60 FPS。

對應診斷區間：交易至 load3d 1213.920 ms、session 開啟至全部顯示部件完成 800.000 ms、材質等待 2557.000 ms、最後套用 23.000 ms。工作執行緒的 open 273.398 ms、texture_prepare 56.350 ms 已包含於 800 ms，不能再次加總。後處理 catalog_controls 0.024、speech 0.017、face_hand 4.937、animation_catalog 4.578 ms，顯示這次幾何切換並未重做初始化時的昂貴動作目錄／語音。

### 最大瓶頸確認：全角色 shader 重新提交及前處理

UnrealInsights 已成功匯出 534 項預覽事件、150,982 項 Shader／Material／Compile／DDC 相關事件。精確 trace 區間 28.7854817～31.3424614 秒（2556.980 ms）對應「全部部件完成至材質就緒」。此區間量到：

- GameThread 的 FMaterialShaderMap::SubmitCompileJobs 58 次，合計 230.448 ms；第一批 28.7855053 秒，最後一批到 31.2649395 秒才提交完成，批次散布約 2.479 秒。最後提交完成到就緒只剩 77.522 ms。
- GlobalBeginCompileShader 2683 次、FBaseShaderFormat_PreprocessShader 2683 次。
- ShaderJobTask 2461 項事件，分布 28 條背景工作執行緒。最後一項到 31.3391862 秒才結束，離材質 ready 僅 3.275 ms。
- 主執行緒 ProcessAsyncResults 186 次合計 479.632 ms；其內部 ProcessCompiledShaderMaps 合計 470.149 ms，兩者是包含關係，不能相加。
- KK_PreviewMaterialReady 輪詢 187 次合計 249.316 ms，其中包含 230.448 ms 的提交。不是在一個函式裡同步卡住 2.56 秒。
- ShaderJobTask 跨執行緒 inclusive 合計約 47.498 秒，前處理 inclusive 約 41.983 秒。這些是平行執行的累加區段，且有父子重疊，不能當成 wall-clock 47 秒或再加到 2.56 秒，也不是由此直接推算 CPU 使用率。

原始碼 PreparePlaybackMaterials(false) 為控制單幀負擔，每幀最多提交一個未完成材質；此規則加上重新建立約 60 個 active materials／instances，讓新 shader 的提交、前處理及完成整理跨很多幀串行延伸。追蹤中所有上述新增提交都在部件完成後的等待階段發生。這是目前可見切換延遲的主要原因，不能只解讀成磁碟載入慢，也不能把最後一次 readiness log 的 0.062 ms 當作整段材質耗時。

### 通用改善方向（本輪只做 profile，尚未實作）

1. 顯示端沿用未改變的角色部件與已就緒材質，更新目標飾品；以完整骨架、材質輸入與資源身份判斷，變更條件不相容才完整重建。目前來源端已重用 34 部件，UE 顯示端沒有沿用。
2. 可變顏色、紋理、陰影等改走各角色的材質實例；共享已編譯基底需完整 shader／狀態 key，避免每次 NewObject<UMaterial> 後重新編譯。保留陰影、表情及各角色參數隔離。
3. 重新檢討材質提交階段；不能直接一次把 58 次／230 ms 工作塞回主執行緒，那會換成明顯卡頓。應優先移除重複編譯，必要新材質才依單幀時間預算預備。
4. 即使完全消掉材質等待，R35 仍剩約 2278 ms；輸入卡片另存、原生擷取／封裝、整卡讀回及全角色 session 開啟也須縮減。非同步已存在，但目前可見更新仍等待這些工作完成；要達每次 300 ms 不能只加 Async。

完整 evidence：Saved/Tests/EditorAcceptance/EditorSwitchProfileR36 下的 cpu.utrace、shader-events.csv、preview-events.csv、switch-profile.json、shader-analysis.json、shader-summary.json、submission-analysis.json 及分析腳本。原始 R35 時間線另存原標籤 switch-profile.json。沒有修改正式載入行為、編譯設定、畫質或驗收門檻。
