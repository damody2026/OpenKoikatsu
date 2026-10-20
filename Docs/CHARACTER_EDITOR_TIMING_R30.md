# 編輯器長幀時序定位 R30

## 計畫

1. 驗收啟動器新增選用 CPU／frame／bookmark 時序追蹤，預設不啟用；追蹤結果為診斷，不能冒充無追蹤的效能驗收。
2. 以相同額外飾品操作記錄所有建立元件、原生姿勢與背景服裝預載階段，保留原有 100 ms／60 FPS 與完整逐幀斷言。
3. 使用本機 UE 原始碼所提供的 UnrealInsights 無介面匯出命令分析事件，定位最長主執行緒區段後實作修正，再用關閉追蹤的新驗收確認。

R29 的來源目錄快取已完成並通過契約。最新無追蹤 GUI 仍不達效能門檻；本輪不得放寬或刪除長幀。

## 追蹤發現與實作

- EditorCpuDiagnosticR30 8 項功能通過，嚴格效能仍失敗。此為有追蹤診斷，不作無追蹤比較：切換 4918.3572 ms；30,028 幀最低 23.7225 FPS，21 幀超預算。
- UnrealInsights 無介面匯出及 Python 串流分析 44,431,360 個主執行緒事件。約 51.99、76.90、104.22 秒的模型預載階段，GalleryWidget Tick 自有程式分別耗時約 31.79／31.64／31.87 ms（不含子事件）。完整分析存 visible-analysis.json；23.56～106.20 秒為近似分析時間窗，正式逐幀斷言仍以 report.json 全部原始採樣為準。
- 原生 SkelBoneCount 是懶建立 ensure_skel_export，包含骨架及全部參考位置／法線／切線／權重索引；既有貼圖 worker 未呼叫它，因此主執行緒 BeginSessionLoad 首次查詢才建立。新增可選骨架預備，於獨佔 session worker、套用身形／mod 後執行，不推進動畫或動態模擬。主載入、衣物預載及男性角色載入全部接入；渲染模式在主執行緒擷取，CPU 模式保持原流程。
- 增加 NativeReferencePreparation 實卡契約測試，比較完整骨架名稱／父階／參考及目前 TRS、所有 renderer 的參考頂點／法線／切線／骨骼索引、目前頂點、PNG 和身形值，並確認工作實際位於 worker。
- 增加模型批次、setup、骨架 setup 的 CPU 事件，可區分後續瓶頸。陰影 RenderTarget 的 UpdateResourceImmediate 在本機 UE 原始碼中只排入 render command，沒有同步等待，未因函式名稱而盲改。

待建置、契約及關閉 trace 的正式 GUI 驗收；目前不宣稱 100 ms／60 FPS 通過。

## R30 第一輪無追蹤驗收

EditorNativeReferenceBuildFinalR30 建置成功（108.75 秒）；EditorNativeReferenceIntegrationR30 共 14 項 UE Success／退出碼 0，包括完整背景骨架契約。EditorNativeReferenceExtraR30：8 項功能成功、假髮截圖已檢視；整體仍 valid=false／退出碼 1。切換 7212.3536 ms，22,697 幀平均 192.9207 FPS、最低 19.8483 FPS、最長 50.3821 ms、56 幀超預算。正式預備紀錄 worker=1，骨架建立約 16.9～44.6 ms；不是把非同步當達標。

啟動環境與兩份採樣快照僅有本次 D:/UE5.8 GUI，沒有其他 UE；二進位雜湊一致，未並行本次建置／測試。不能從單次結果宣稱穩定改善。首次載入的 GPU 模式尚未被 Open 初始化，擷取既有模式可能為 false，續修使用與 Open 相同的請求判定；其他來源保存及顯示全量重建成本仍需繼續處理。
