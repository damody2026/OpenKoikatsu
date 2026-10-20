# 100 位女角實際場景

新增 `/Game/Scenes/CrowdRoom/CrowdRoom`，50 × 50 公尺的房間，以切開屋頂的視角觀察全部 NPC。地圖指定 `AOKCrowdGameMode`，不建立 Gallery 或預覽角色。

選角採與 Gallery 相同的 `FString::Sort` 檔名順序，先 `UserData/chara/female/*.png`，不足才補 `DefaultData/chara/female/*.png`，固定前 100 張，不因匯入失敗改選下一張。完整來源順序保存於 `Saved/Crowd/SelectedCards.txt`，資產重用前核對來源 SHA1。

每張卡只匯入第 0 套服裝，正式角色使用 `AKKImportedCharacterActor`、保存的 SkeletalMesh 和 GPU 蒙皮。各角色以 60 Hz 烘焙來源 `studio/anime/00.unity3d|f_aruki_00_00_01`，包含角色自己的頭髮、服裝、飾品骨架與來源物理結果。這是固定行走片段的烘焙物理，不是執行時對新碰撞的物理求解。匯入器可能在離線轉換時使用預覽 Actor 建置網格；新地圖的執行時不使用它，也不解析 Unity 資源或人物卡。

載入器使用軟引用、非同步逐角讀取，每幀最多提交一位角色。居民在各自保留間距的路線上持續移動，行走動畫循環且錯開相位。這是效能基準場景，尚未接上導航、避障、互動行為或遠近精度切換。

## 使用

```powershell
Tools/run_crowd_room.ps1 -Mode Build
Tools/run_crowd_room.ps1 -Mode Play
Tools/run_crowd_room.ps1 -Mode Benchmark -Seconds 60 -Tag Baseline
Tools/run_crowd_room.ps1 -Mode Benchmark -Seconds 60 -Tag Acceptance -Require120
Tools/run_crowd_room.ps1 -Mode Benchmark -Seconds 60 -Tag CPUProfile -Profile
Tools/run_crowd_room.ps1 -Mode Benchmark -Seconds 60 -Tag EightWorkers -TaskWorkers 8 -Profile
```

雙擊根目錄 `CrowdRoom.cmd` 也可啟動已建立的地圖，目前使用 8 個 Task Graph worker、幀末更新模式 1、RHI width 8。WASD 移動相機、Q/E 升降、按住右鍵轉向、Shift 加速。量測固定從同一個俯視鏡位開始，關閉 VSync 與 FPS 上限，保持原有模型、材質和陰影設定。

首次匯入需時間與磁碟空間。已完成且來源雜湊相同的角色和動畫可以接續重用；遇到錯卡、失敗或未完成的輸出時保留證據並修復，不覆寫來源或跳過選定角色。專用 commandlet 的 `-AssetsOnly -StartIndex=N -EndIndex=M` 可準備不重疊的批次，地圖組裝必須包含全部 100 位。

## 驗證與量測

`OpenKoikatsu.Crowd.Routes` 驗證全部路線有移動、房間邊界與角色間距。`OpenKoikatsu.Crowd.SavedMap` 驗證保存地圖、GameMode、100 組不同角色與存在的行走資產。功能驗證要求 `complete=true` 與全部 100 位行走；效能驗收另外要求 Development、至少 120 筆暖機後樣本、平均及 P95 幀時間皆不超過 8.333 ms。`-Require120` 未達門檻會失敗，不以功能完成代替效能達標。`-Profile` 在全員載入並暖機後擷取 20 秒 CPU trace，驗收應另跑未開詳細追蹤的測試。

`Saved/Crowd/` 保存建置和執行日誌、報表、截圖及 runner 結束碼。JSON 分開記錄載入耗時、全數載入後再暖機 10 秒的 wall frame、game thread、render thread、GPU timing、P95 和程序記憶體。GPU timing 為 Unreal RHI 回報，未回報時的 0 不應解讀為 GPU 沒有成本。Editor `-game` 的量測不是 Shipping／Cook 包的 FPS。

以第一份全員實際播放報表作為基準，再逐項優化，避免用之前雙角色測試推算 100 人 FPS。

`-TaskWorkers 8` 使用 Windows Unreal 的程序內 core reporting limit，啟動總計 8 個 Task Graph scheduler worker（預設 2 前景、6 背景），並設定 `-oversubscriptionratio=1`，避免另外啟用備用 worker。這不修改 OS affinity 或其他 Editor；game、render、RHI、IO 等專用執行緒也不計入這 8 個 worker。`-ForegroundWorkers=N` 另外調整前景／背景分配，不能把全部 worker 都指定為前景。以暖機 trace 的 thread 清單確認實際啟動數，而不是只依參數名稱判定。

`-ParallelEndFrameUpdates 1` 開啟 Unreal 原生的 `AllowAsyncRenderThreadUpdatesEditorGameWorld=1`，以 Task Graph `ParallelFor` 提交幀末渲染資料。此版本 Editor game world 的預設值為 0，會將這些更新集中於 game thread；不應把已有平行動畫求值當成所有骨骼提交也已平行。模式 2 為完全非同步版本，需另外驗證。

`Tools/Patches/ParallelVirtualShadowDraw.patch` 增加預設關閉的 `r.Shadow.Virtual.NonNanite.ParallelDraw`，將非 Nanite VSM 的 draw command 分派改用 `FRDGDispatchPassBuilder` 和既有 mesh command task；不降低陰影頁面、幾何或關閉陰影。`-ParallelShadows` 開啟該選項並使用 `r.RHICmdWidth=8`，必須搭配包含此 patch 的 private Renderer DLL。`Tools/build_crowd_renderer.ps1` 利用既有 Development 編譯 response files，將新的 obj/DLL/PDB 全部輸出到獨立的 `Saved/Crowd/RendererBuild-*`，不覆寫共享引擎的編譯輸出；`prepare_crowd_runtime.ps1 -PrivateRendererDll ...` 可在新 overlay 中使用它。

`Workers8RuntimeThreadsR1.json` 保存實際程序的執行緒名稱：2 個 Foreground Worker、6 個 Background Worker，合計 8 個，沒有 Standby Worker。`*.launch.json` 保存各輪參數，避免只比較相同標籤卻使用不同設定。

Epic 的 [Tasks System 文件](https://dev.epicgames.com/documentation/unreal-engine/tasks-systems-in-unreal-engine?lang=en-US) 說明 `UE::Tasks` 和 TaskGraph 共用 scheduler 與 worker threads。本次使用這個共用後端以及既有動畫求值、幀末 `ParallelFor` 和 mesh draw command task，並以實際執行緒清單及 trace 驗證；沒有另建一組八執行緒的 pool，也沒有將 Actor／UObject 建立任意移到背景。

詳細 trace 的完整對照：預設 scheduler 平均 49.371 ms、P95 56.531 ms；8 worker、無備用 worker 平均 31.552 ms、P95 34.855 ms。`TaskWorkersComparisonR1.json` 記錄整組設定約 36.09% 的平均幀時間改善，不能把它當成單一 worker 數因素的因果證明。另一輪未開詳細 trace、8 worker／幀末更新模式 1／平行陰影的 `BenchmarkPackedRigParallel8R1.json` 為平均 27.949 ms、P95 29.786 ms，game thread 10.689 ms、render thread 27.730 ms、GPU 6.944 ms；仍未達 120 FPS，也尚未證明平行陰影能改善整體成績。

同一已載入程序進一步測試 Windows affinity `0xFFFF`（前 16 個邏輯處理器；不能僅依 mask 推定其 CCD／SMT 拓撲）。`ControlFirst8RepeatR1.json` 在 8 worker、模式 1、width 8、原有序列陰影、沒有詳細 trace 下，2160 筆樣本平均 **23.157 ms**、P95 **25.478 ms**，game thread 10.866 ms、render thread 23.108 ms、GPU 7.083 ms；100 位全部行走，但 **120 FPS 未達標**。這個 affinity 僅用於自己的測試程序，不是 launcher 預設，也未改動其他 Editor。width 16 的案例平均 28.880 ms／P95 30.035 ms，width 32 為 31.667 ms／36.847 ms；最後回到 width 8 的 1488 筆樣本平均 33.640 ms／P95 39.163 ms。最後系統取樣另有遊戲與背景程式使用 CPU；這組環境並非獨占測試，不能只依 task 數推定提升，也不能把最佳一輪當成穩定成績。`TaskWorkers8FinalEvidenceR1.json` 彙整這四輪及限制；測試程序的 affinity 已恢復並正常結束。

`ControlFirst8ProfileR1All.csv` 的 trace 指向 mesh draw command 的 CPU 工作成本。只讀材質稽核 `MaterialsC001R1.json` 顯示三月七的可見 LOD0 有 72 個 section、72 個不同材質物件。這是物件身分統計，尚未證明材質的 shader／參數／貼圖完全相同；後續共用或合併必須保留外觀並另外稽核。俯視截圖可確認實際百人房間，角色顏色偏白，近景材質外觀仍需比對。

可選的 `-ControlFile Saved/Crowd/PerformanceControlR1.json` 讓同一個已載入的遊戲程序重測，不重載 100 位。JSON 僅接受遞增 revision、新 tag、30～600 秒 duration、三種幀末更新模式、陰影平行開關、1～32 的 RHI task width 與 trace 開關，不執行任意 console command。每個案例重新暖機 10 秒、保存自己的 JSON／截圖／可選 trace；完成後繼續行走等待下一個案例。`Tools\lua\lua.exe Tools\measure_crowd_case.lua --tag FreshTag --revision 2 --end-frame-mode 1 --seconds 60` 可提交並等待案例，`--require-120` 只允許沒有詳細 trace 的案例，且要求 100 位全員播放與至少 120 筆樣本。named event 只在受控 trace 期間啟用，完成後恢復原值。案例完成後可用較大 revision 與 `exit:true` 結束這個程序。

目前自動選擇完整的 `PackedRig` 變體，合併網格並只保留加權骨骼、所有父節點，以及未合併部件需要的骨骼。100 位角色的 immutable geometry／權重／骨骼父子關係稽核通過；原始合併網格共 61,511,946 次動畫頂點比較，位置誤差為 0、法線一致。另以三個角色的 13 個時間點驗證實際共享 pose 的精簡骨架。

`BenchmarkPackedRigR1.json` 的 Development Editor `-game` 實測：100 位全員行走，1899 筆樣本，平均 26.342 ms、P95 29.885 ms，game thread 16.436 ms、render thread 26.187 ms、GPU 7.096 ms。尚未達 120 FPS；不能因 GPU 時間低於 8.333 ms 就宣稱整體達標。

首次 Editor 遊戲載入還會建立來源貼圖的衍生資料及 PSO；冷啟動與已建立快取的進場時間應分開比較。`Saved/Crowd/GameBuildR1.log` 記錄獨立 Development 遊戲建置失敗：既有 KKUnityBundle 的材質／預覽／匯入程式碼仍呼叫 Editor 專用 API。因此本次 launcher 使用 Editor `-game`，尚未宣稱完成獨立 Cook 包。

APNG 轉換另依 `Docs/CROWD_APNG.md`，保留本批每張卡的全部服裝；房間先穿第 0 套服裝。完整服裝資料不需要同時進入 100 人場景的記憶體。

`OpenKoikatsuCrowdEditor` 是同樣 Development Editor 設定的獨立建置 target，以自己的熱重載狀態避開互動編輯器 target 的失效引擎插件連結映射。它仍編譯相同 `OpenKoikatsu` 模組，沒有改用 Shipping。修復建置紀錄為 `Saved/Crowd/Build420.log`。

已建立的 `Characters/C###/Compact` 變體保留原本全部頂點、三角形、morph 與材質，只縮小 follower 網格的骨骼參照集合；leader 保留完整私有骨架。只有全 100 組完整存在時才採用這些變體。每位角色共用自己的烘焙 pose，頭髮／衣物／飾品所需私有骨骼會加入 leader 必需集合。可用 `-CrowdFullSkinReferences` 與 `-CrowdIndependentPoses` 分別比較原始骨骼集合及各部件獨立求值。原作逐頂點稽核已驗證三月七、長髮及襪子案例，誤差與原始版本一致；這些正確性通過不代表 120 FPS 已達標。

`Tools/pack_crowd_meshes.ps1` 建立 `Characters/C###/Packed`。合併第 0 套服裝中可見、單位局部變換、不需要口型切換的骨骼網格；保留各部件的材質、四組 UV、頂點屬性、morph、原始權重與私有骨架，其他部件分開保留。Unreal 建置可能重排頂點，因此稽核以來源頂點映射及材質 section 比對：6 個行走时间點的全部動畫位置誤差不超過 0.001 cm，法線完全相同，頂點／三角形／morph 數量一致，通過後才保存。三月七首個樣本稽核 544,008 筆位置，最大誤差 0 cm。只有 100 組完整存在時場景才優先採用 Packed；`-CrowdUnpacked` 可比較 Compact 版本。

場景固定服裝和口型，可見 follower 持續提交骨骼更新；不可見 follower 停止自己的 tick，leader 仍保留及計算它需要的骨骼。報表記錄 `packed_skin_parts`、`skinned_parts` 和 `ticking_skin_parts`，便於確認使用路徑及元件成本。

`BenchmarkCompactDebugCopyR1.json` 的 Development 基準為 100 人全部播放、950 筆樣本：平均 52.707 ms、P95 70.771 ms，仍未達 120 FPS。`DebugCopyR1GT.csv` 的 20 秒追蹤顯示大量元件更新／render data 提交，支持先合併零件。這份數據包含 Development follower debug transforms 複製修正，沒有模型減面。
