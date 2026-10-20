# 模組來源清單通知重用 R35

1. 基準量測已完成：實際 7,316 ZIP 項目與原路徑／大小／微秒時間完全一致，單次來源查詢重掃 53–72 ms；記錄於 EditorInventoryBaselineR35.log。
2. 建立最多四個目錄、跨工作執行緒的不可變清單及既有完整簽章快照。保留原始呼叫路徑字串作 key 與輸出，另外以 canonical root 驗證相對路徑目前指向的位置，避免改變路徑別名的簽章語意。
3. 讀取前註冊檔案通知，讀取期間變更、不完整遍歷、ZIP metadata 不可讀、nested reparse／連結或通知不可用時不發布快取；保留既有資料讀取與錯誤回退。不在共享鎖內遍歷來源。
4. 原本 mods_inventory_sig 與 ensure_mods 共用該清單及簽章；SQL、優先順序、實際來源內容、其他模組與磁碟快取格式不改。
5. 驗證跨執行緒重用、舊快照穩定、來源新增／改寫／刪除、路徑別名、容量、部分讀取／載入中變更／中毒回退與實際外部 junction。重跑原生來源／卡片契約、UE 與正式 GUI。
6. 最新飾品門檻是每次 300 ms，切換後完整期間至少 60 FPS。R34 來源正確、衣物 175.6241 ms，但飾品 5186.2038 ms／最低 5.19375 FPS 仍失敗；本項不能只憑快取命中數宣稱總效能完成。

## 原生驗證

EditorInventoryRegressionR35 首次為 14 passed、1 failed、2 ignored（fixture 筆數假設，見 E116）；EditorInventoryRegressionFixR35 重跑完整 SQL 為 15 passed、0 failed、2 ignored。跨執行緒、精確路徑別名、增刪改、舊快照壽命、容量、缺失目錄恢復、部分讀取／載入中變更／鎖中毒及實際 junction 均成功。

EditorInventoryInstalledR35 正式執行 ignored 實作來源診斷：7,316 ZIP 清單精確一致；直接遍歷 44.104 ms，通知驗證暖清單 0.193–0.455 ms，實際單來源查詢 0.215–0.259 ms，原 API 全目錄複製對照仍約 138–141 ms。原實際單來源查詢 55–59 ms，已確認去除重複遍歷，不改簽章內容。

模組 17、清單 13、安裝遷移、部件資源、單飾品來源、單欄卡片各 1 通過，加上 SQL 15 及正式安裝來源診斷 1，共 50 項不同原生測試成功。其他 ignored 不計通過。單欄來源卡片首次更新 2020.970 ms，仍含冷遷移 1278.722 ms、來源擷取 487.618 ms，不能當作 300 ms 通過。

正在建置正式 DLL 及 UE，GUI 尚未計為新版成功。


## 建置與介面驗證

EditorInventoryBuildR35 建置成功（169.56 秒）；EditorInventoryIntegrationR35 的 17 項 UE 整合測試成功／退出碼 0。EditorInventoryPropertiesR35 全部 13 項成功，涵蓋顏色、飾品旋轉／縮放／綁定、假髮來源與頭部位置、另存重新讀取及半脫還原。EditorInventoryClothesFastR35 的 7 項功能／32 次切換成功，最大 198.4203979 ms，沿用既有衣物 200 ms 檢查。兩張正式截圖已檢視，髮型位於頭部。

飾品正式驗收使用每次 300 ms、完整背景預備期間至少 60 FPS；目前仍在採樣，不提前計為成功。啟動環境快照有外部 E:/PhysX UE 程序，不能宣稱隔離，也不能直接將任何失敗歸因於它。

### R35 正式驗收結果

50 項不同原生測試及 17 項 UE 整合測試成功。外觀 Properties 13 項、衣物 ClothesFast 7 項／32 次切換成功（最大 198.4203979 ms）。三次 GUI 的兩個 DLL 雜湊皆與啟動紀錄一致，未開 CPU 追蹤；截圖均已檢視，假髮位於頭部。

EditorInventoryExtraAccessoriesR35 的 8 項功能成功，但效能失敗：switch_budget_ms=300、switch_budget_pass=false、switch_300ms_pass=false；切換 5154.4246003 ms，34,396 幀最低 6.1119539 FPS、最長 163.6138000 ms、138 幀超過 16.667 ms。平均 204.6888788 FPS 不能替代持續 60 FPS。完整背景期間未排除。來源 receipt 證實只重建第 20 飾品一個部件、重用 34 部件及其他 6 套服裝；來源擷取 437.9242 ms，本身已超過 300 ms。工作執行緒原生請求 968.9758 ms，輸入另存 241.5384 ms、結果讀取 177.0170 ms，總可見切換仍 5.154 秒。

驗收啟動有外部 E:/PhysX UE，結束沒有；此為兩個時點快照，不能宣稱全程隔離或將失敗歸因於外部程序。exit.txt 為 0，但 report.valid=false，正式 PowerShell 驗收據此正確拋出失敗；不以程序退出碼掩蓋未達標。來源 receipt、交易時序、完整逐幀報告保存在 Saved/Tests/EditorAcceptance/EditorInventoryExtraAccessoriesR35。300 ms 門檻更新完成；目前效能仍未達標，不標記全部需求完成。
