# 角色編輯載入改善錯誤紀錄

- 初次搜尋再次把 KKPreviewActor* 當 rg 路徑；Windows 原生命令不展開 wildcard。已改成目錄加 -g，並以 rg --files 找到實際檔案在 Plugins/KKUnityBundle；不以搜尋失敗推論功能不存在。
- 既有載入已把 Session.Open 與 PNG 預解碼放在背景，但要等所有部件完成才顯示；不可未量測就聲稱尚未非同步，亦不可把 UObject 建立移到背景執行緒。
- 來源路徑不可依模組名稱猜測：KKRustExtract.cpp 實際在 KKUnityBundle，雙人準備在 OKPairedPreparation.cpp。以 rg --files 定位後再讀檔。
- 基準程序啟動期間遇到另一個 Build.bat 正在執行，這段約 70 秒不計入角色載入。首次資源解析／材質快取建立與同程序再次載入必須分開報告，不能把冷暖快取差異當作本次改動成效。
- 共用工作區的 Editor manifest 在調查期間從 1314 更新至 1315。不得還原較舊 manifest、Rust library 或 render 原始碼；建置使用目前工作區並加 -NoEngineChanges，驗收另列當時使用的版本。
- 本次一般 Editor 建置遭 FailedDueToEngineChange 拒絕：既有 NetCore 編譯狀態要求更新引擎產物。更需注意，UBT 在拒絕前已自動清理部分熱重載 DLL 與專案 manifest，-NoEngineChanges 並不能防止這種專案產物清理。不得把此失敗當作建置成功；須等目前另一個 1316 建置完成、確認最新 DLL／receipt／manifest 與正式入口可用，再驗收。本次沒有手動刪除 DLL、沒有改引擎原始碼，也沒有停止其他人的建置。
- 後續 1316 建置的專案程式（含本次載入與測試）編譯／連結成功，但整體因正在使用的 UnrealEditor-NetCore.dll 無法覆寫而失敗，未產生有效 receipt／manifest。已備份 1316 DLL 及 HotReloadState，再以 -UsePrecompiled -NoEngineChanges 和新 suffix 1345 建置；使用目前引擎產物，不以停止使用者 Editor 作為解法。
- 1345 的 UsePrecompiled 建置 actual0（112.72 秒）且入口 dry run 通過，但實際啟動發現 shader late-load assertion，不能把 dry run 當作執行成功。dumpbin 證實主 DLL 仍匯入 base KKCard／KKCrowdGpuPacking 及 KKUnityBundle/KKRuntimePose-0001，而新 manifest 指向 -0002，導致重複載入 shader 模組。只指定主模組 suffix 不足；需依先前已記錄的通解，先替全套自有模組預映射同一 suffix，再建置，使 imports 與 manifest 一致。不得用修改 LoadingPhase 或 shader/render 遮掩版本混用。
- ProgressiveAfter45 在 SDK helper 等待其他 Build.bat 時取消了本次自有測試程序及其已驗證 parent 的 SDK 子程序；沒有角色載入結果，不算通過。R2 使用引擎既有 UE_SKIP_UBT_SDK_SETUP=1 僅跳過測試程序的啟動 SDK 偵測，隨即偵測到上述 DLL 相依錯誤；同樣不算通過，也沒有把該環境變數寫入使用者設定或正式啟動器。
- 1346 同時預映射相同 suffix 再要求同 suffix，產生 -1346-1346.dll.rsp 的 UBT 二次重寫錯誤；該輪失敗。全 target 建置不使用 -Module filter 時，不需要為避開 partial filter bug 而預映射；應在無舊 hot-reload state 的一致 base 版本上，明確指定全套自有模組同一新 suffix，並以 dumpbin 驗證完整相依圖。失敗 helper 留在 Saved 證據目錄，不作為正常建置工具。
- 建置同時遇到其他工作的 portable_hair_soft.rs 尚未落盤及暫時型別錯誤；保留 pending-rust log，沒有改動他人的頭髮實作以通過本任務。本次自己建立但未發布的 1347 映射已移存到 Saved，沒有留下指向不存在 DLL 的設定。當時另一個一般建置已完成，正式 base 引擎／專案 BuildId 更新至 8700cbd9-d259-44b3-8586-8a539f7d3d88；R4 使用其一致相依 DLL 並實際通過，未還原較舊 DLL。
- R4 的 catalog_controls 從秒降到毫秒，不代表總載入變快；完整 GUI 反而量到 12.924／14.481 秒。據此繼續處理來源清單反覆解壓，不選擇只報局部改善而忽略總時間。
- 工作區同時有人更新 KKPreviewActor.cpp；驗收期間其 hash 從 C1DC7E... 變成 200C469B...。本任務未修改該檔，不能再宣稱最後 hash 與初始一致，應以最後實際截圖檢查並記錄外部變更。
- 全套 9 個自有模組統一 suffix 1355 的建置成功（217.96 秒），dumpbin 完整相依圖亦通過，但隨後另一個直接 dotnet／NoHotReload 建置繞過 Build.bat 檔案鎖，清理了熱重載產物。該 helper 因此未完成 runtime 驗收，不能把編譯成功當作整個 helper 通過；亦不能假設 Build.bat 鎖可阻止所有 UBT 入口。未停止其他建置，未改引擎、render、LoadingPhase 或正式啟動器。
- 最後採最新一致正式 base DLL，獨立重跑 ProgressiveFinal45 實際 GUI 與 Material384914Progressive45 四視角，兩者 exit 0／valid 或 passed=true；正式讀卡 DryRun 通過。失敗 helper 與 build1355 imports 證據保留於 Saved/Tests/GalleryReload381/BuildRecovery45，不再拿失效 suffix 當正式版本。
