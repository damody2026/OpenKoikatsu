# Stage389：共用 zipmod 查找與變更核對性能

2026-10-03，正式 BuildR2 actual0／Succeeded，DLL 已更新。

## 根因與通用修正

Stage388 首次配對 prefab source load 約 397ms；只取一個 override 時，ensure_mods 還複製全 catalog、GUID 及 override map。ProfileR1 安裝資源量測 inventory 約 248–250ms，原三欄位 clone 約 90–94ms。原 all_zipmods 對每個 entry 用 path.is_dir，之後再對 zipmod 用 fs::metadata，Windows 重複檔案查詢昂貴。

內部 ModsMem 改為 immutable Arc snapshot；外部 mods_catalog／guid_index／override_index 仍回傳自己可修改的 owned 欄位，僅複製所需欄位。override_bundle 用單筆 override_source，只複製目標 PathBuf；outer None 表示 SQL unavailable／empty，仍用原 TSV／scan fallback，Some(None) 表示 populated SQL index 的有效 miss。原 override 優先於 native fallback、zip entry 路徑正規化與 bundle 讀取門檻不變。

stamp_zips 直接遍歷 DirEntry，用 file_type 判斷一般目錄，一般 zip 取 enumeration metadata；link／未知 type 仍走原 path 查詢／跟隨。保持 size＋mtime microseconds、路徑排序、完整 inventory digest 與 incremental SQL 更新，IO traversal 失敗仍捨棄 partial inventory。每次查找仍核對完整 inventory，沒有以根目錄時間戳、時間 TTL 或卡名特判跳過失效。Metadata enumeration 不是資源變動的原子快照，並不新增並行寫檔全保證；原版本也沒有此保證。

本輪只優化 host resolver／cache，KKS MotionIK 的 p_cf_body_bone、cf_n_height、solver 初始化及 frame contact 順序不變。以原流程 reference oracle 驗證，沒有為圖片修改 shape、材質或資源。Factory current bytes 仍完整讀取與 full SHA256 key，分解 profile 預設關閉，wire／ABI／更新頻率不改。

## 驗證

- SqlFinalR1 actual0：7 passed、2 ignored；含原 inventory non-newest／微秒失效、path alias SQL 行為。新 fixture 驗證空 SQL fallback、nested 大寫 ZIPMOD／輸入反斜線、edit／removal／addition、有效 miss、owned index 修改隔離、舊 Arc snapshot 不變。
- NoSqlR1 actual0：獨立 OK_NOCACHE=1 process，先從 scan 取得 bundle，再把 zip 移到 mods 外、由 TSV 路徑取得相同 bundle；沒有建立 SQL cache。只修改本輪 Saved fixture，未改原遊戲／zipmod。
- ProfileR2 actual0：安裝的 7,311 個 zipmod，原遍歷與新遍歷完整 tuple Vec exact。原253.075ms、新37.660ms；target lookup三輪37.097／39.856／38.740ms，原三欄位 clone三輪95.324／95.057／93.587ms。這是局部 CPU timing，不是GUI全程或SSD固定性能。
- FactoryR1 actual0：March 真卡 shape 初始化後，height 0.72／1.02／1.28 的 all initial solver fields 與保留 original load／parse factory exact，template bytes immutable／同內容 Arc hit。IdentityR1 actual0：不同 game root／舊 sampled parser 未覆蓋的 byte 變更不同 full key。
- BuildR1／R2 actual0／Succeeded；R2包含最終 no-SQL regression fixture。
- Perf389PairR1 actual0／AutomationSuccess：女667／男681 bones、walk／skin／world parity、四筆 active IK oldsolver exact，兩 native Idle 選項每個30frames shared clock。ControlsR1 actual0／AutomationSuccess：mouse七mask、owner／gesture／clock、generation cancel。

## 實測與仍有成本

| active Idle API | 第一選項 | 第二選項 |
| --- | ---: | ---: |
| Stage388 commit | 769.308ms | 89.300ms |
| Stage389 commit | 377.255ms | 85.119ms |
| Stage389 worker prepare | 13.150秒 | 2.313秒 |
| Stage389 CPU frame平均 | 46.793ms | 46.119ms |

首次 prefab source load 48.074ms，其中 resolve 35.635ms、native file read 12.439ms；full hash／lookup等 18.705ms。Stage388 load396.534ms與此輪比較，均保留完整來源核對。第二次 prepare 舊動畫持續 40frames，max old update 111.285ms。

仍有首次 render/contact commit 與每幀 CPU 更新成本，未宣稱所有姿勢無停顿、8fps全scene已解決或全部卡零bug。測量涵蓋此雙人 fixture 與三高度 oracle，不等於 physical GUIclick／GPUsceneFPS。後續優化須繼續分段核對每幀成本，而不是減少 IK／物理／陰影更新頻率。

## 錯誤保存與封存

SqlR1 actual101：新 fixture 放5-byte bundle，原 collect_zipmod_bundle 必須 bytes.len()>16，unwrap None。加長 fixture 保留原產品門檻；SqlR2與最終SqlFinalR1通過，舊失敗 log／receipt 原樣保留。另在原測試完成前讀 receipt 得到 missing；後续應先等待 session／檢查存在，不能把工具腳本 exit0 當測試成功。

E267／HANDOFF3 §80；Saved/Tests/AnimationPerf389 保存完整成功、失敗、fixture、source diff與receipts。fresh R2 audit／snapshot逐檔核對 Stage388314項舊證據不變，保存正式 source／DLL／Rustlib，未刪原封存。

R1封存把stdout導到正在封存的seal-output.txt，該檔在print前被記錄為空、完成後唯一該項hash不符。R1.verification.txt保存實際核對失敗；保留原audit／snapshot／stdout不修改，fresh R2直接工具stdout，最後核對所有記錄及live source／binaries。封存工具自身正在增寫的輸出不得列入正在產生的封存。
