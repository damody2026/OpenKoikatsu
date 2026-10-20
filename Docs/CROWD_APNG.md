# 前 100 張女角：完整服裝 APNG

依 `Saved/Crowd/SelectedCards.txt` 的順序轉換，每張卡保留全部 Coordinate。來源 KKS PNG 保持原樣，結果在 `output/CrowdAPNG/C000.png`～`C099.png`，manifest 對應原始檔名、SHA256、服裝數與驗證證據。

使用既有 `OKPV0003` 多服裝格式。模型、原始貼圖和骨架以內容雜湊參照 `Content/IndependentResources/v1`；卡片持有自己的參數、合成結果及各套服裝索引，相同個人資產在各服裝之間也只存一次。APNG 是本專案格式，需要一起攜帶此共用資源包。載入已轉換卡時不需要 KKS 安裝目錄。

每張 APNG 的預覽為 768 × 1024 四視角，正面頭像 2 秒、前／側／背各 1 秒，5 秒循環。預覽取第 0 套服裝，檔案內仍含全部服裝，其他服裝沒有降成圖片。四視角擷取只在離線轉檔使用 PreviewActor；100 人地圖使用保存的遊戲資產與 SkeletalMesh。

為加速載入，CRC 使用相同 IEEE 演算法的加速實作，格式相容。可編輯參數的套用及頂點計算結果預先存入每套服裝的 snapshot，避免每次開卡又做一次。`ok-normalize-pack` 逐套比對位置、法線、權重、骨索引、bind、三角形、四組 UV、顏色及主要貼圖的 SHA256；保留 APNG 原預覽、全部卡片參數與服裝。之後修改身形仍會按新參數重新計算。

首次轉換需要解析來源、烘焙與保存全部服裝，可能耗時。先完成資料準備，再擷取 APNG、逐套獨立重開及身形變更驗證。程序只重用來源雜湊相同、有完整驗證 receipt 的結果，失敗輸出保留供修復。

```powershell
Tools/convert_crowd_apng.ps1 -Tag FreshEvidenceTag
```

`-PreparationPid=N` 可接續已啟動的資料準備程序。整批結束後，`output/CrowdAPNG/manifest.json` 必須為 `complete=true`、100 張卡。`*.verified.json` 保存來源／APNG SHA256、全部服裝數及驗證路徑。

`ok-crowd-pack GAME LIST WORKER_RESOURCES OUTPUT START END PUBLISH_RESOURCES` 可準備不重疊的區間。各 worker 使用自己的 catalog，僅發布不可變的內容資產；整批完成後合併 catalog。不可讓多個程序同時修改同一份 catalog。

`ok-load-benchmark GAME ORIGINAL APNG` 比較第 0 套服裝的 native session 重建，跑三輪，報告部件數及是否獨立於遊戲。數據不包含 Unreal 物件建立、GPU 上傳或 FPS，也沒有清空 OS／磁碟快取。100 人場景另依 `Docs/CROWD_ROOM.md` 測量。

首張卡驗證：7 套服裝的轉換前後幾何、UV、貼圖一致性測試通過。`Saved/Crowd/LoadNormalizedR1.json` 記錄約 0.13～0.24 秒的 APNG 重建；同輪原卡暖快取約 0.4～0.5 秒。單一卡片及有其他匯入程序運行時的測量，不能代表全部 100 張卡。

本批交付已完成：100 張卡、720 套服裝，APNG 合計 4,411,379,916 bytes（約 4.11 GiB，不含共用資源包）。`Saved/Crowd/APNGDeliveryR1.json` 驗證 manifest 的選取順序、來源及輸出 SHA256、PNG chunk CRC、4 幀動畫尺寸／時間／循環、原始參數區塊與全部服裝索引。逐套獨立重開、身形變更和正規化前後完整幾何雜湊都通過。可再次執行 `Tools/lua/lua.exe Tools/verify_crowd_apng.lua C:/portable/OpenKoikatsu` 檢查交付。

`Saved/Crowd/CatalogMergeR1.json` 記錄合併後的共用資源 catalog；合併保留既有內建資源與各批次的不可變內容，檢查資產 magic／大小後才發布 catalog。攜帶 APNG 時必須同時攜帶 `Content/IndependentResources/v1`。

新版 native benchmark 的三個代表案例保存於 `LoadIdleC000R2.json`、`LoadIdleC030R2.json`、`LoadIdleC077R2.json`。第二／第三輪暖快取的 APNG 重建約 0.071～0.121 秒，原始 KKS 約 0.354～0.397 秒，改善約 3.16～5.12 倍。第一輪部分原卡還要建立來源快取，不能把其數十到數百秒耗時當成每次載入都能改善百倍；這些測量也不能換算成百人場景 FPS。
