# Stage368：共用資源包複製的一致性

修正 `Tools/build_native_resources365.py` 的共用缺陷：此前先讀來源計算 SHA，再重新開檔複製。來源在兩次讀取之間改變時，catalog 可指向錯誤內容；`target.exists()` 後直接複製也讓相同內容的平行工作同時写入目標。

現在一次串流讀取，同時寫獨立暫存副本、計算 SHA 與累計實際 bytes；複製前後核對來源的檔案 ID、長度及 nanosecond 修改時間，變動就拒絕。完整副本關檔後以同一輸出資料夾內的 exclusive hard link 原子發布，重複內容只驗證並共用已完成的副本，不覆寫。此 hard link 不連原遊戲來源，發布後移除暫存名稱，資源包持有自己的資料。

只有所有工作成功才原子發布 catalog；失敗保留部分檔案作診斷，不產生可用 catalog，也不覆寫既有輸出。CLI `--source` / `--output`、schema1、logical abdata 路徑、SHA 檔名格式保持相容。新增拒絕空 abdata，避免產生匯入器本來就不接受的空索引。未改卡片、材質公式、zipmod resolver 或 Unreal DLL。

## 驗證與失敗

`Tools/test_native_resources368.py` 使用 Saved/Tests/AssetImport368 下唯一 fixture，所有成功及失敗資料保留。

- BuilderRedR1 actual1：讀到來源後注入實際變動，舊工具沒有拒絕，仍生成 catalog；保存的 blob SHA 與索引不一致。原始失敗輸出保留。
- BuilderGreenR1 actual1：新檢查錯用 Windows Python3.12 `stat`/`fstat` 的 ctime；同一正常檔案分別得到建立時間／修改時間，被誤判。改用兩個 API 一致的 ID、長度及 mtime_ns。來源變動仍必須被測試實際注入，不能靠其他錯誤作假通過。
- BuilderGreenR2 actual0，兩項測試通過。
- BuilderGreenR3 actual1：擴充發布失敗負例時，測試區段插入點錯誤，舊正例的 owned 變數落入新函式產生 NameError。修正函式界線，產品邏輯不變；失敗 log 保留。
- 最終 BuilderGreenR4 actual0，3 項測試通過：來源變動拒絕且无 catalog；24 logical files／5 unique blobs、四個 worker 用 barrier 強制同時發布、SHA/bytes 全對且來源後續修改不影響獨立副本；注入發布 I/O 錯誤無 catalog 且保留 partial。成功與失敗輸出均拒絕再次覆寫。

既有 native_v1 不重建、不覆寫。新 Audit368 逐一讀回現有1146 catalog logical entries 的資源 SHA/bytes，並核對舊 Audit367 與歷史資產 hash。只修改離線建包工具，沒有重跑相同 Unreal DLL 的匯入／重開驗證；Stage367 的已驗收資產保留。

## 限制

支援具備同目錄 hard-link 能力的檔案系統；不支援時明確失敗，不退回有競爭的覆寫。mtime/ID 檢查不等於原遊戲停機快照，無法保證刻意復原時間戳的並行寫入或跨檔案版本一致。catalog 的 SHA 與保存 bytes 則由同一次串流產生。仍沒有所有卡片／GPU像素／完整 controller／整個應用 Shipping 零 bug 證明。
