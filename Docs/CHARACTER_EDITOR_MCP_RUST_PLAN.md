# 人物編輯器 MCP Rust 移植計畫

日期：2026-10-10。目標：完整移植現有 31 個工具與身形擬合，使用端只需執行 Rust exe，不依賴 Python／NumPy／Pillow；保持 Unreal 的信箱契約、工具名稱及參數格式。

- [x] 官方 rmcp SDK stdio server、輸入驗證、取消、序列修改與即時診斷。
- [x] 原子 JSON／Windows 分享衝突重試、期限、圖片及幾何路徑驗證。
- [x] 純 Rust 三視圖 rasterizer、遮罩分割／對齊／比較、閉合截面。
- [x] 參數敏感度、有界搜尋、最佳值保存、人工修改保護、取消／續跑與持久化背景作業。
- [x] 原生 Rust CLI、自足發佈執行檔、建置腳本與 MCP 設定範例。
- [x] Rust 單元／協定／模擬編輯器測試；原實機 20 項案例改用 Rust server 完整驗收。
- [x] 更新使用說明、覆蓋與錯誤紀錄；Python 僅保留為歷史參考及開發驗收工具。

不改動人物形變與渲染的 Unreal 核心。幾何遮罩採固定且可重現的三角形填色規則，重新量測 Rust 輪廓誤差，不假定與 Pillow 邊界像素逐點相同。

## 實作配置

- `Tools/character_mcp/rust/src/app.rs`：官方 rmcp 3.5.1、31 工具的共用 schema、序列寫入、非阻塞作業狀態／取消、錯誤 MD。
- `io.rs`：Unreal 本機信箱、取消／期限、原子 JSON 與 Windows 分享衝突重試、輸出路徑邊界。
- `geometry.rs`：純 Rust 影像讀寫、固定投影、三角形填色、參考分割／對齊／比較、閉合截面。
- `fitting.rs`：一般數值參數的敏感度、有限次數／時間的自適應座標搜尋、最佳值 checkpoint／續跑、外部編輯保護。
- `main.rs`：stdio 入口與 `fit` 原生命令列、Ctrl+C 取消、另存新卡。
- `build.ps1`：locked release 建置，Windows 靜態 CRT；產物 `bin/ok-character-mcp.exe`。
- Unreal 端另外修正 MCP 的材質穩定等待，使用既有非阻塞準備／render fence 流程；不更動人物 morph 或遮罩排除規則。

## 驗收紀錄

Rust 16 項測試已通過（15 個核心／模擬編輯器／CLI，加 1 個真實 stdio 子程序協定測試）。涵蓋三視圖像素對齊、閉合獨立截面、分割、原子報告、輸入及路徑驗證、一般目標搜尋／續跑、敏感度恢復、取消最佳值、人工修改保護、背景取消／重開查詢、裁切候選處理、原生命令列擬合另存、官方握手／31 工具／取消／EOF。證據：`Saved/Logs/CharacterMCP-Rust-tests.log`。

Clippy 全目標零警告。31 工具的名稱、原生 action 與 inputSchema 與原版逐一相符；使用說明的 16 個 JSON 區塊有效。單一 release exe 複製至獨立目錄、PATH 只有 Windows System32 時仍完成 MCP 初始化及工具列舉；dumpbin 只列 Windows 系統 DLL，沒有 Python 或 VC runtime DLL。證據：`Saved/Tests/CharacterMCP/RustPortable.json`。

實機驗收結果完成後記錄於下方，舊失敗 log 保留於錯誤紀錄，不當作通過證據。

### RustLiveR3：20／20 通過

真實 Unreal renderer、Rust exe 的 stdio MCP、31 工具、4,300 人物參數與 49 個控制。全部 96 項原生身體／臉部參數批次寫入讀回及還原通過；骨骼、詳細屬性套用、三視圖、區域 PNG、截面、參考對齊、敏感度、背景搜尋、保存與重開通過。

相同身體參考對齊 loss=0。未知合成目標以 9 次評估，loss 由 **0.13342587168414 降至 0.001855926981424242**，下降 **98.6090%**，達 target_loss；最佳值保存與重開一致。這是固定正交投影、同姿勢、既有參數能表達的合成目標驗證，不代表任意照片皆達相同精度，也不保證參數解唯一。

證據：`Saved/Tests/CharacterMCP/RustLiveR3/report.json`、`mailbox/fitting/run-9f11af220cf241c8aa0d48edccef0ad1.json`；版本／執行檔 SHA-256／整體證據索引：`Saved/Tests/CharacterMCP/RustValidation.json`。


### 最終正常模組驗收：RustLiveR5

專案四模組正式建置 r8 與官方專案 WriteMetadata 完成；manifest 全部指向正常 DLL。RegistryR3 通過，startup 日誌確認載入正常 KKUnityBundle.dll。使用同一批正常模組再驗收 RustLiveR5，**20／20 通過**。相同身體對齊 loss=0.0；未知合成目標 9 次評估，loss=0.13342587168414 → 0.001855926981424242（下降 98.6090%），最佳值保存重開一致。

最新證據：`Saved/Tests/CharacterMCP/RustLiveR5/report.json`、`Saved/Logs/CharacterMCP-Rust-build-r8.log`、`Saved/Logs/CharacterMCP-Rust-metadata.log`、`Saved/Logs/CharacterMCP-Rust-registry-r3.log`；總索引與執行檔雜湊：`Saved/Tests/CharacterMCP/RustValidation.json`。R1/R2/R4 失敗或中止結果保留，不當作通過證據。
