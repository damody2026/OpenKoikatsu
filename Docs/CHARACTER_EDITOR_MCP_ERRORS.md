# 人物編輯 MCP 錯誤與防止復發

## 已修正

| 問題 | 原因 | 正式修正與驗證 |
|---|---|---|
| tick 直接讀 viewport 得到空白 PNG／RHI ensure | 截圖時機早於完成渲染 | 使用引擎 screenshot callback；LiveR2／LiveR3 三視圖像素與變形驗收 |
| 角色重載時誤報沒有 PreviewActor | 非同步重載暫時清除 actor | 先等待 busy 結束；屬性 apply／角色 reopen 實機驗收 |
| 動態 UI 回呼執行兩次 | 設值與手動 broadcast 重複觸發 | 設值時暫停 delegate，還原後僅廣播一次 |
| 首次編譯 API／型別錯誤 | TObjectPtr 推導、SharedString key、缺少 EditableTextBox include | 依實際 UE 標頭修正；build-r8 成功 |
| 使用 DisableUnity 造成不必要的引擎大量重編譯 | 建置模式改變使大量引擎產物失效 | 使用專案標準建置與 Module 範圍，不任意改 unity 設定 |
| 第二階段 build-r9 被同時修改的 Rust 借用錯誤阻擋 | 其他工作中的 session.rs 尚未完成 | 確認來源已由該工作修正後重建，不回退其他工作的檔案 |
| build-r10 幾何 JSON Encode 型別不合 | TSharedPtr 不能隱式轉 TSharedRef | 檢查非空後明確 ToSharedRef；build-r11 通過 |
| 搜尋中的候選身高超出固定投影而整個最佳化中止 | 錯把不合法候選當作資料集失敗 | 初始畫面仍嚴格驗證，越界候選記錄高誤差並繼續，避免偷偷縮放鏡頭 |
| LiveR4 啟動前 module manifest 缺失而立即退出 | 共用建置產物更新時 manifest 不存在，僅 Module 建置不保證重建 metadata | 完整專案標準建置產生正式 manifest；state 查詢最多等待 10 秒，避免遊戲已退出仍等待 300 秒 |
| LiveR5／LiveR6 於屬性作業期間無回覆；起初誤判程序退出 | 混合 PowerShell 表格輸出造成觀測誤判；後續 CIM 明確確認程序仍存活 | 原因未定位前不宣稱崩潰；改用明確 Format-List／精確子程序 handle，新增 MCP heartbeat、作業 phase 及等待條件，保留失敗並重新驗收 |
| 後續定位上述長作業 | LiveR7 heartbeat 顯示 apply phase=3／busy，並持續產生資源；冷快取完整重建約 210 秒後成功回覆 | 官方 client／範例期限調為 1800 秒；state 保持短期限，progress 可獨立查詢。LiveR7 已通過屬性套用、重載、還原與保存，勿再把正常冷建置當作崩潰 |
| build-r16 被 Engine NetCore DLL 鎖定阻擋 | 引擎也有其他工作中的重編譯，執行中遊戲持有 DLL | 不任意修改引擎 manifest 或關閉無關程序；待鎖定程序自行結束後使用正常建置重試 |
| build-r18 找不到未加 suffix 的 CrowdGpuPacking import lib | 反覆混用顯式與自動 HotReload suffix，使產物／相依名稱不一致 | 沒有本引擎遊戲占用後，完整建置明確加 -NoHotReload，產生一致的正式 DLL／lib／metadata；不複製或手改 import lib 名稱 |
| build-r13 連結 DLL 被其他遊戲執行個體鎖住 | 共用專案有使用者／其他工作執行中的遊戲 | 使用 UE 原生 ModuleWithSuffix 建置獨立版本，不關閉無關遊戲；PowerShell 含逗號的 ModuleWithSuffix 參數必須整段加引號 |
| heartbeat 又出現 SharedPtr／SharedRef 編譯錯誤 | JSON helper 僅接受 SharedRef，呼叫端常持有 SharedPtr | 將 helper 統一接受 SharedPtr，內部檢查後轉 SharedRef，避免每個呼叫點重複轉型與漏改 |
| 側視圖幾何投影方向與攝影機右側視圖不一致 | 未依 ApplyOrbitCamera 的旋轉推導 right vector | 依實際攝影機座標修正，frame 明列 side；reference 的圖片路徑 side 與側別 side_view 分開，schema 測試驗證不覆蓋 key |
| 幾何量測可能重複計入 procedural 外框 | 額外 section 是膨脹外框／overlay，並非解剖表面 | procedural 僅讀 section 0；skinned 僅讀正式 Skel 部件，不納入 outline follower |
| LiveR7 只有 358 個眼部頂點、沒有身體 | 原生 GPU 顯示隱藏 CPU observer，visibility 過濾把真正身體排除 | 幾何查詢讀取身體／頭部解剖表面，不依 CPU observer visibility；原生 GPU 使用更新後 observer，傳統 skinned 使用當前 morph／bone skinning |
| 背景搜尋 job 一直 queued | Windows 在讀取 job JSON 時暫時阻止 os.replace，worker 在進入 try 前失敗 | 原子 JSON 使用唯一暫存檔與有限重試；整個 worker 包含狀態寫入都捕捉錯誤，status 檢查 future 失敗，測試包含同時輪詢 |
| 高頻同時讀寫測試仍遇到 PermissionError | Windows replace 期間新 reader 也可能看到 delete-pending | 將標準函式庫 JSON IO 共用化，讀端與寫端都做有限分享衝突重試；避免只有 writer 重試的片面修補 |

## 本輪驗收原則

- 不以 padded component bounds 當作精確身高／圍度。
- 不以介面通過推論所有參考圖均可高精度還原；使用模型幾何與可重現合成目標評估。
- 無法分割的參考圖應回報原因，接受外部 AI 提供遮罩，不偷偷使用不可靠的背景假設。
- 所有實作／驗收錯誤追加於本檔；執行期工具錯誤另保存在信箱 `errors.md`。

## 2026-10-10：Windows 首次進階工具呼叫停在 NumPy DLL 載入

LiveR8 已通過前 15 項，但第一次 body_sections 無回覆。server-stack.txt 證實 worker 停在 NumPy 原生模組 create_module，主執行緒正在讀取 stdin；不是 Unreal 當機或截面演算法無限迴圈。修改為第一次 fit_* 呼叫先在 reader thread 初始化可選原生依賴，計算仍交給序列 worker，保留取消能力與無依賴核心工具。相同執行中的角色實測 body_sections 立即回覆、取得 2 個閉合截面。另將 worker 的未預期一般例外統一轉為 MCP 錯誤，避免 Future 吞掉例外造成客戶端永久等待。保留 LiveR8 堆疊與報告，終止本次自行建立的阻塞 server，完整重跑驗收。

## 2026-10-10：獨立實機驗收啟動等待共用 UBT 鎖

LiveR9 在人物載入前停於 AutoSDK／TargetPlatform 初始化；CIM 顯示它自行啟動的 Build.bat ValidatePlatforms 正以 ping 等待其他建置持有的批次鎖。這不是 MCP 寫入或渲染失敗。依引擎 TargetPlatformManagerModule.cpp 的正式 Multiprocess 分支，獨立驗收 renderer 加入 -Multiprocess，避免子程序重複啟動平台 SDK 設定；不修改引擎、不刪鎖、不終止其他建置。驗收仍使用相同正式遊戲模組、真實渲染與 stdio 通道。

LiveR10 已避開 AutoSDK 鎖，但啟動時其他建置正更新專案 DLL；manifest 指向的 UnrealEditor-OpenKoikatsu.dll 當時不存在，遊戲明確回報找不到模組。保留原始 log，不誤判成 MCP 回歸。改為完成正式完整建置後立即啟動驗收，避免使用建置中的產物。

後續確認所有 Build.bat 都只剩 ping 等待，無實際 UBT 子程序；批次鎖可能由繼承 handle 保留。未刪鎖或終止未知持有者，僅終止本次自行啟動的等待器，改直接呼叫已建置的 UnrealBuildTool.dll，保留 -WaitMutex 與完整正式目標。這是引擎支援的建置入口，避免批次包裝鎖阻止驗收，同時保留真正的 UBT 互斥保護。

## 2026-10-10：參考對齊遺失端點像素

LiveR11 前 16 項通過；相同身體遮罩重新對齊後 loss=0.048870609，超過門檻。原因是 Pillow 多邊形包含上下端點像素，但對齊使用連續高度四捨五入，少一列並平移細手臂，放大 IoU 誤差；奇數寬度的置中也有半像素取整差。修正為與 rasterize 相同的投影座標 floor 和含端點像素跨度，再調整左右置中。未放寬驗收門檻。原始 LiveR11 三視圖離線回歸已達三視圖 IoU=1、loss=0；新增小數高度與奇數寬度端點回歸測試，重新執行全部測試及實機驗收。

## 最終驗收：2026-10-10

正式完整建置 r21 成功；Python 16 項通過；Unreal Registry automation r3 通過；LiveR12 的 20 項真實 stdio／渲染／編輯／量測／搜尋／存檔重開驗收全部通過。參考對齊 loss=0；未知合成目標搜尋 9 次評估，loss 由 0.13281102388409247 降至 0.0019241573809563107，下降 98.5512%，最佳值保存與重新開啟一致。先前失敗報告保留，不能把它們誤認為最終結果；完整證據與精度邊界見 [覆蓋與驗收](CHARACTER_EDITOR_MCP_COVERAGE.md)。


## 2026-10-10：Rust 遷移介面與編譯差異

Rust 浮點常數需明寫 0.9，不能沿用 Python 的 .9；已修正並交由 cargo fmt/check 驗證。Tokio JoinError 沒有公開 from_panic 建構子，改直接處理巢狀工作結果。截圖契約為 images 下的相對檔名（物件欄位 image），Rust 沿用此契約並驗證 canonical parent。官方 rmcp 需完整 initialize/notifications/initialized 握手，驗收 client 已補齊。Windows 文件讀寫腳本必須明確指定 UTF-8，避免 cp950 預設編碼中斷遷移。

Rust 測試首輪發現測試目標高 152cm 卻以 140cm 模型建立固定投影，超出畫框時被正確拒絕；將測試參考框改為 160cm，保留禁止裁切規則。截面欄位沿用 closed_loops，測試誤寫 loops 已修正。

官方 SDK 的 ServerConfig 反序列化需要 protocolVersion；首輪 stdio 握手揭露缺欄位 panic。改使用 ServerConfig::new 與官方 builder，避免手寫不完整初始化結構，並新增真實子程序握手／31 工具／錯誤參數／取消／EOF 結束回歸測試。另補齊 union 型別驗證，拒絕 number/string/boolean 以外的參數值。


## 2026-10-10：Rust 實機驗收遇到漸進材質 readiness 停滯

RustLiveR1 在 control_set 的原生 phase=4 等待，busy=false、shape/mods=false、shaders=false，但 materials_ready=false。請求已被 Unreal claim，並非 Rust 沒有送出請求；先前進度描述已由此證據修正。新增漸進材質替換會使 readiness 失效，MCP Settled 只讀旗標而沒有驅動準備／fence 的非阻塞輪詢，導致永遠等待。改呼叫既有 PreparePlaybackMaterials(false)，保留材質／shader／frame 穩定條件，不移除等待或硬改 ready。RustLiveR2 則因其他建置正在更新 DLL 而明確報模組不存在；沒有修改或終止其他建置，正式重建後再驗收。驗收 client 增加 client-trace.log 記錄送出／收到時間及工具名稱。

原生重建 r1 編譯完成但連結 LNK1104：其他聊天的 AppearanceSweep 程序正在使用共用 OpenKoikatsu.dll。未終止該程序；改採引擎正式 ModuleWithSuffix 輸出新模組供後續驗收，現行程序保留既有載入模組。PowerShell 含逗號的 ModuleWithSuffix 參數需整個單引號引用。

後續 suffix 建置 r2/r3 揭露 UBT 重用已改寫過的 hot reload action graph，產生遺失 import lib／重複 suffix 的 rsp 路徑。正式以 -NoUBTMakefiles 重新生成 action graph，對共用三個模組指定一致新 suffix，r4 建置成功；沒有複製舊 DLL、手改連結 response 或終止其他驗收。正常原始碼修正仍是非阻塞 readiness 輪詢。

Registry 首輪 editor-context 啟動因 suffix runtime 模組與未重連結的 KKCardImporterEditor 同時載入新舊 KKUnityBundle 而觸發 UObject 類別重複，並非 Registry 測試 assertion。game-context 的 RustLiveR3 仍繼續。確認原占用程序已自然結束後，使用 NoHotReload/NoUBTMakefiles 正式完整重建回到一致的正常模組，避免留下依賴混合的發佈產物；Registry 將重新執行。

正常建置 r5 的 UBT 清理步驟要刪除正在被本次 RustLiveR3 載入的 suffix DLL，因 Windows 分享鎖而失敗；不在驗收仍運行時重試正常建置。待本次驗收自然結束後再完成一致的正式模組建置與 Registry 重驗。

其他遊戲驗收又載入 suffix DLL，正常 UBT 的清理仍會影響使用中的舊產物。將本次 editor target 的 HotReloadState.json 可逆保留為 .mcp-rust-retained-20261010，讓正式重建產生正常模組而保留既有程序持有的 suffix 檔。路徑先確認位於本工作區，未刪除 DLL 或終止其他程序。r6 等待同引擎另一個獨立 Game target 建置；確認該專案目錄及 Game／Editor 中間產物不同後，只終止本次等待器，正式 r7 使用引擎支援的 NoMutex、停用 UBA/XGE、獨立 log、NoEngineChanges 進行本地 editor 建置，避免共用 executor／log。

r7 的 NoEngineChanges 正確攔下不屬本次範圍的引擎 NetCore／引擎 metadata 重建需求。保留引擎保護，改以正式 Module 選項明列四個專案模組（OpenKoikatsu、KKUnityBundle、KKRuntimePose、KKCardImporterEditor），完成一致的正常模組與 editor importer 連結；不為 MCP 遷移重建或修改整套引擎。

原生四模組正常建置 r8 成功，但 -Module 建置省略 WriteMetadata，第一次 RegistryR2／RustLiveR4 啟動仍使用當時的 suffix manifest；Registry 的 editor importer 因此再次載入雙份 runtime。對照 manifest 的寫入時間與 InternalLoadLibrary 日誌確認此原因，不把它誤認為正常模組失效。停止本次 R4 自有驗收伺服器，執行工作區已提供且驗證專案輸出範圍的 publish_editor_project_metadata.ps1（引擎官方 WriteMetadata、Version=null、不寫引擎）後，使用正常 manifest 重新啟動 RegistryR3 與 RustLiveR5。後續建置若用 Module 選項，必須同步完成這個正式 metadata 步驟，才能驗到剛建置的 DLL。


### 最終正常模組驗收：RustLiveR5

專案四模組正式建置 r8 與官方專案 WriteMetadata 完成；manifest 全部指向正常 DLL。RegistryR3 通過，startup 日誌確認載入正常 KKUnityBundle.dll。使用同一批正常模組再驗收 RustLiveR5，**20／20 通過**。相同身體對齊 loss=0.0；未知合成目標 9 次評估，loss=0.13342587168414 → 0.001855926981424242（下降 98.6090%），最佳值保存重開一致。

最新證據：`Saved/Tests/CharacterMCP/RustLiveR5/report.json`、`Saved/Logs/CharacterMCP-Rust-build-r8.log`、`Saved/Logs/CharacterMCP-Rust-metadata.log`、`Saved/Logs/CharacterMCP-Rust-registry-r3.log`；總索引與執行檔雜湊：`Saved/Tests/CharacterMCP/RustValidation.json`。R1/R2/R4 失敗或中止結果保留，不當作通過證據。

收尾已確認：Rust 16 項測試、Clippy 全目標、31 工具 schema 相容、獨立 exe、原生 Registry 與最新正常模組 20 項實機驗收全部成功；沒有待完成的 MCP 遷移項目。
