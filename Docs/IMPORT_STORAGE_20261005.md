# KKS 匯入資產容量修正

根因有兩層：CrowdRoom 的歷次實驗版本一直留在 Content；原生 KKS 匯入的 `AnimationSnapshot` 又呼叫完整預覽序列化，把已保存為 Unreal Texture 的 albedo、normal、mask 等 PNG 再保存一次，而且快照沒有壓縮。PackedRig 等衍生 Character 複製同一份完整快照，進一步放大容量。

`ok_session_animation_snapshot` 現在走專用動畫序列化。保留原始骨架、私有 rig、蒙皮、形狀、morph、碰撞器及 dynamics，移除動畫求值不使用的重複貼圖，使用 LZ4 無損壓縮。眼睛及 item 控制器自己的資料仍保留，以免改變動畫控制。新封套 `OKAN0001` 保存原始長度和 CRC；讀取器限制解壓大小、檢查 CRC，並相容舊的未壓縮動畫快照。

完整 PNG/APNG 仍走原有 `to_preview_bytes` 與 OKPV 格式。APNG 內嵌資源和卡片的讀取路徑未改。沒有縮小 Unreal 貼圖、移除 mip、減面、改權重、刪 morph 或移除 static fallback。

## 舊資產轉換與清理

```powershell
# 預設只列出可減少的容量，不修改資產。
Tools/optimize_import_storage.ps1 -Tag StoragePlan
# 轉換既有快照，先備份，再用第二個程序重開驗證，成功後移除備份。
Tools/optimize_import_storage.ps1 -Tag StorageApply -Apply
# 如需保留原檔供額外比對，加入 -KeepBackups。

# 列出無引用的舊實驗資產。
Tools/cleanup_crowd_storage.ps1 -Tag RetiredPlan
Tools/cleanup_crowd_storage.ps1 -Tag RetiredApply -Apply
```

轉換 commandlet：`-run=KKImportIndependentCard -OptimizeStorage=/Game/Scenes/CrowdRoom/Characters -StorageReport=<absolute-json-path> [-StorageApply]`。它先開啟舊快照、匯出新快照，再比對所有 native part 的 SkinSourceHashV2 和原始 affine palette；通過後才替換。保留來源雜湊、服裝、Skeleton 與全部 Part 引用。轉換需要新版 KKUnityBundle；本機已部署新的 reader。舊版 reader 不能讀新動畫封套。

Runner 先固定四個 runtime 插件的 DLL、複製原本的 DefaultEngine 設定，建立沒有專案 game module 的獨立 host，避免另一個編譯程序更換 `.modules` 時混用 hot-reload 和 base DLL。驗證用 `-StoragePackageList=<file>` 精確選取有快照的 Character；未改寫的低模副本以檔案大小與時間戳核對，避免無意義的重載與重建。

清理依 Unreal Asset Registry 的 hard、soft、searchable、management 引用計算依賴閉包。只以 Compact、Packed、PackedCompact、Performance256、Performance256Atlas/V2、InstancingBonesR1、GameLow256R1 為淘汰候選；其餘所有資產均為保留根。被任何保留資產引用的候選仍保留，未註冊資產則拒絕清理。原始匯入、PackedRig、目前低模及近景資料均保留；不碰 APNG、IndependentCards、遊戲 stage 或來源 KKS 卡。

完整 100 角低模 batch 成功後，會執行引用清理並保留本次指定的 Variant。診斷時使用 `-KeepExperimentalAssets` 可保留舊實驗版本；單角、未完成或失敗 batch 不自動清理。清理工具也接受 `-KeepVariant` 保留指定實驗版本。動態程式路徑無法由 Asset Registry 自動發現，因此只有明列的退休版本可以被刪除；保留版本不靠猜測決定。

## 驗證

- Rust header/size-limit 測試通過；實際三月七 fixture 檢查 CRC 損毀拒絕、舊快照相容、61 個行走時間點的骨架及全部 native palette 精確一致、morph 資料一致、完整 APNG 序列化未變。
- Fixture 動畫快照：49,793,548 → 11,459,151 bytes。
- Portable 回歸：6 項通過；另有 6 項需要額外 fixture 的測試未執行。
- 實際 `Saved/APNGCards/all-outfits-edited.png` 載入與身高改動匯出骨架測試通過（2.18 秒）。
- C010 試算：兩份動畫快照合計 208,685,744 → 81,238,603 bytes，native contracts 和 palette 不變。
- C++ module 與 Rust release library 編譯、連結成功。測試記錄：`Saved/Tests/ImportStorage20261005`。
- 清理移除 36,218 包、47,904,343,748 bytes（44.614 GiB），48,944 包保留，角色目錄由 80,472,071,450 → 32,567,727,702 bytes。磁碟可用空間觀察增加 45,200,461,824 bytes；因硬連結與其他程式活動，不能把邏輯容量減少宣稱為排他性的實體回收量。依賴圖、刪除清單和核對結果：`Saved/Crowd/StorageCleanup20261005R2`。

## 批次完成結果

- 重複執行 C010 的 Apply 與獨立重開驗證通過，已壓縮的兩份快照 byte count 不變、所有 `applied=false`，不會重寫已完成的資產。報表：`Saved/Crowd/ImportStorageIdempotent20261005`。
- 100 份原始 Character + 100 份 PackedRig Character，200 份快照全部轉換；payload 合計 **11,606,744,136 → 2,440,866,128 bytes**，減少 **78.97%**。
- 獨立程序重新開啟全部 200 份快照，檢查壓縮封套 CRC、native contracts 和 palette，**0 errors、0 warnings**。保存後的快照 byte count 與轉換報表完全相同。200 份原檔備份已刪除。
- 角色資料夾最後 **80,472,071,450 → 23,401,849,694 bytes（74.95 → 21.79 GiB）**，合計減少 **53.15 GiB 邏輯容量**。
- C010：2,864,073,796 → 689,550,114 bytes；C036：2,787,607,865 → 454,822,959 bytes；C028：2,194,215,529 → 478,602,227 bytes。
- 除 200 份改寫的 Character，**48,744 份保留資產的大小與時間戳完全一致**；檔案集合仍為 48,944 包，36,218 份退休資產皆不存在。
- **778 份 APNG／IndependentCards 檔案的 SHA256 完全一致**。沒有刪除或改寫卡片、貼圖、mesh、morph、材質、服裝或來源遊戲檔案。
- 最終報表與每角色容量：`Saved/Crowd/ImportStorage20261005/FinalAudit.json`；轉換報表 `Apply.json`，最終重開驗證 `VerifyR4.json`／`VerifyR4.log`。

保留失敗的驗證紀錄：第一次重開時 live `.modules` 被另一個編譯程序改為 hot-reload DLL，造成重複 UClass 註冊；固定 DLL 的獨立 host 解決此問題。第一個 host 未複製 GPUSkin 設定，造成不必要的 mesh DDC 重建；已停止該唯讀程序並複製原設定。最後驗證只重開實際有快照的 200 包；這些驗證中止都沒有修改資產或移除備份。

## 通用流程再驗收與錯誤防再犯

- 空快照資料夾：runner 原本仍傳空 `StoragePackageList`，commandlet 會拒絕。現在先確認快照總量為零且無改寫，再成功回報無需轉換；有快照的路徑仍強制第二個程序驗證，不能略過。
- 已壓縮快照：HashMap 序列化順序可能讓再次匯出產生不同壓縮大小。不能用重新壓縮後「略小」當作再次改寫理由；讀取並驗證 `OKAN0001` 後直接保留既有 bytes。
- 動態載入資產：Asset Registry 無法發現程式字串組出的路徑。清理只能處理明列退休版本，不能將所有無引用資產視為垃圾。
- DLL 衝突：驗證必須使用固定 runtime DLL 的獨立 host，且複製來源 GPUSkin 設定；不要改動其他編譯程序的 live manifest。
- 新匯入已由 `ExportAnimationSnapshot` → `ok_session_animation_snapshot` → `to_animation_bytes` 接上專用格式，沒有依卡名或 CrowdRoom 角色编号分支。舊格式仍可讀取；APNG 完整序列化保持獨立。
- 空快照實測：`C010/GameLow30kR2` Apply 成功，0 → 0 bytes、無資產改寫；報表 `Saved/Crowd/ImportStorageEmptyAcceptance20261005`。
- 實際三月七卡重新回歸通過：49,793,548 → 11,459,975 bytes，61 幀 native palettes 與 morph 精確一致、APNG 完整序列化不變（122.61 秒）。壓縮大小的微小差異來自 map 序列化順序，沒有影響載入後資料。
- Portable 再驗收 6 passed、0 failed、8 ignored（需額外 fixture 的測試）；實際 APNG 身高修改匯出 1 passed、0 failed（2.13 秒），記錄 `PortableFinalAcceptance.log`、`APNGFinalAcceptance.log`。
- 最後完整驗收：全部 200 份快照再次獨立重開成功，0 errors、0 warnings，package set 與前次完全一致、2,440,866,128 bytes 不變、0 rewrites；報表 `Saved/Crowd/ImportStorageFinalAcceptance20261005/Verify.json`／`Verify.log`。驗收後重新核對，778 份卡片 SHA256、48,744 份其他資產大小及時間戳仍完全一致，備份剩餘 0，角色資料維持 21.79 GiB。
