# BpGeneratorUltimate 啟動新聞停用

2026-10-07。使用者指定停止 Unreal 啟動時的 Ultimate Engine CoPilot 版本新聞視窗，因該 modal 卡住 GUI。

已修改本專案 `Plugins/BpGeneratorUltimate/Binaries/Win64/UnrealEditor-UECPShell.dll`。安裝包沒有 UECPShell 的 C++ 原始碼，只有預編譯 DLL；因此採取版本雜湊鎖定、可還原的局部修改，沒有停用整個插件。

原始 SHA256：`9496db41e98ad6e9a936a641a3d104c41a8ab9cfbfb05b3e91fdae48ad69b686`。
修改後 SHA256：`665a4f76516765afd514f4bd0515eae05eedf92fc5bdf5ef532714ed57d8bd3b`。

靜態查證：`.pdata` 確認 RVA `0x9DB70` 為獨立函式入口；該函式包含 `UECPWhatsNew: popup check starting`、`UECPWhatsNew: showing personal notice`，其後排程自動新聞 popup。只修改入口五個 byte 為 `ret; nop; nop; nop; nop`，直接結束這個 void 回呼；不依賴某一則新聞 ID，也不修改登入、授權、工具或主介面函式。手動 What's New 畫面與歡迎頁資源保留。

原 DLL 與 receipt：`Saved/PluginPatches/BpGeneratorUltimate-NoStartupNews/`。
工具：`Tools/lua/lua.exe Tools/disable_bpg_startup_news.lua --verify` 檢查，`--restore` 可還原。沒有參數時套用。工具拒絕未知或另經修改的 DLL，插件升級後必須重新確認版本，不能將舊位址盲目套到新 DLL。修改於下次 Unreal 啟動生效。

驗證：LLVM 反組譯確認入口立即 `ret`；`Tools/test_bpg_news_startup.ps1 -Tag News02` 通過。非 `-unattended` 的桌面 Editor、NullRHI 模式，實際載入 UECPShell，執行啟動 dialog tick，沒有 popup dispatcher／popup log，以 exit0 結束。此為啟動功能 smoke，不宣稱已做完整一般 RHI GUI 操作驗收。

錯誤紀錄：News01 被與新聞無關的 Content JSON 自動匯入對話框擋住，僅停止自有測試 Editor；News02 用命令列設定關閉該次程序的 bMonitorContentDirectories，沒有改使用者持久偏好。不能將這個匯入 dialog 當成新聞補丁失敗，也不能為測試殺掉使用者編輯器。

Grok 調查工作 `run-muxti0qs-3fcyyg`，thread `0380200a-d1c3-4d5c-9df4-e7a05f0689e6`，回傳僅有意向敘述，未提供可採用的證據；本修改由主代理獨立查證與執行。該工作 17秒、39,918 total tokens、USD0.03676352；沒有以子代理完成狀態代替驗證。
