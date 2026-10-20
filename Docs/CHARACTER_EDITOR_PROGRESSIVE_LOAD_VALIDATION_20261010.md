# 角色編輯載入改善驗收

## 改動與邊界

- Session 與貼圖原本已在背景處理。本輪另外把雙人 metadata 的 bundle 讀取移出遊戲執行緒，以載入 generation 交付結果；失效工作不會覆蓋目前角色。
- 部件建立由固定每幀一件改為每幀 4 ms 軟預算、最多 8 步；保持原本骨架／身型／部件／最終定位順序。單件耗時無法強行截斷。
- 本輪沒有修改 KKPreviewActor.cpp、材質或 Rust 擷取演算法。調查與建置期間該檔 SHA256 為 C1DC7E76782595D893BBB20F62E4E4D9CBB18B4E754F8F72DC6F9FDA8936D180。
- 尚未實作「身體先顯示、頭髮與飾品稍後進場」；原作 Session 目前仍整體開啟。提早取消隱藏會暴露尚未完成的共同骨架與配件定位，因此未採用。

## 基準

資料：`Saved/Tests/GalleryReload381/ProgressiveBaseline45.json` 與同名 log，原作 KK_384914.png、真實 Gallery SelectIndex / OnLoad3D，兩次建立新 Actor。計時不含引擎啟動、卡片掃描，也不代表 GPU 呈現延遲。

| 項目 | 首次 | 同程序再次 |
| --- | ---: | ---: |
| GUI 載入完成 | 99.021 s | 8.740 s |
| 等 shader／四幀／render thread ready | 99.825 s | 8.929 s |
| Session.Open | 51.539 s | 0.504 s |
| 背景貼圖準備 | 0.418 s | 0.171 s |
| 遊戲執行緒 catalog_controls | 3.004 s | 2.670 s |

首次原作資源與材質編譯快取建立不可與暖快取結果混為改動效果。基準兩次各 34 個可見部件，identity／身型／新 Actor／臉部與手部 batching parity 通過。

## 建置與後測

- R4：實際 GUI 兩輪通過、各 34 部件、99 個配對 metadata；身型與 identity、新 Actor、24,190 個 face 顯示值、2,860 個手部／follower 骨骼與 clocks parity 通過。`catalog_controls` 4.580／5.961 ms；完整 GUI 12.924／14.481 秒。這輪僅證明移出主執行緒與正確性，沒有整體加速，因此繼續加入共用 metadata 快取。
- `OpenKoikatsu.Gallery.LoadSelection` actual0／Result={Success}：切卡與 outfit busy gate、同卡 shape 編輯及失效 worker metadata 不覆蓋目前 generation 通過。
- Rust `authored_catalogue_cache_tracks_source_files` release test 通過：與直接原作解析的 128 筆資料全部欄位／排序一致；新增、替換、刪除、重新加入、不同 root、solo／pair selector、多執行緒查詢通過。fixture 首次 1819.978 ms、命中 0.106 ms；不是完整 GUI 時間，也不作為每張卡保證。
- 工作區外部 render 修改使最後 source hash 已不同於初始值；本輪沒有編輯 render。最後觀察值為 200C469B6D9BF8651416B7E1783256FE641D8724034278778CFE3DE6D9107B66，以最後實際輸出另外驗證外觀。

## 最終實際驗收（完成）

正式 Gallery 載入測試 `Saved/Tests/GalleryReload381/ProgressiveFinal45.json`：exit 0、valid=true。使用一致的正式 base DLL；引擎與專案 BuildId 均為 `8700cbd9-d259-44b3-8586-8a539f7d3d88`。正式讀卡啟動器 DryRun 亦通過，未修改啟動器。

| 項目 | 本次程序首次 | 同程序再次 |
| --- | ---: | ---: |
| GUI 載入完成 | 11.712 s | 6.453 s |
| 等 shader／四幀／render thread ready | 11.970 s | 6.543 s |
| 背景配對 metadata | 3824.158 ms | 2.086 ms |
| 遊戲執行緒 catalog_controls | 4.883 ms | 4.370 ms |
| 可見部件／配對 metadata | 34／99 | 34／99 |

兩輪 identity、身型、新 Actor、24,190 個 face 顯示值、2,860 個手部／follower 骨骼與時鐘一致性檢查通過。與基準暖載入 8.740 秒相比，這次暖載入為 6.453 秒；來源快取與工作區並行變更／系統負載不同，此為單次參考，不能歸因為穩定百分比提升。首次真正冷資源與 shader 準備仍可能較慢。

四視角驗收 `Saved/Tests/Material384914Progressive45/capture-report.json`：exit 0、passed=true，正面／側面／背面／臉部四張均為 3200×2000，已逐張目視檢查。淡紫髮色、髮絲高光、黑色輪廓線、蝴蝶結與身型保留；既有小腿色帶與側邊配件沿用原 render 基準，本輪未處理。沒有 material compile error、assert 或 fatal。

測試僅在自身程序暫設引擎既有 `UE_SKIP_UBT_SDK_SETUP=1`，跳過啟動 SDK 偵測，結束後還原；沒有寫入正式入口或使用者環境。既有 CommonUI viewport 訊息在基準與後測皆存在。GUI harness 驗證真實公開載入路徑，不代表硬體滑鼠操作或 GPU 顯示延遲。

本輪完成範圍為 metadata 來源快取、背景交付與分幀提交；未實作身體先顯示再追加頭髮／飾品。相關建置失敗與並行工作區處理見 `CHARACTER_EDITOR_PROGRESSIVE_LOAD_ERRORS_20261009.md`。
