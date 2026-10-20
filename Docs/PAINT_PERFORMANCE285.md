# 水槍性能續修與離線 APNG 共用動畫（E285）

## 實作

- 表面遮罩採用可重用的前後 CPU 圖塊緩衝。每次流動交換唯讀來源與輸出，避免深複製全部圖塊；仍在啟動工作執行緒之前完成 map 分配。獨立 UV 圖塊、接縫、重力投影、黏度、殘留門檻、120 Hz 掃掠／20 Hz 表面時鐘與質量守恆未修改。
- 覆蓋網格每面只計算一次法線／切線，讀取蒙皮時快取本次元件變換。無人物卡名稱特例、無增加人物三角形、無清除已噴油漆或丟棄物理時間來降低負擔。
- 碰撞候選表面只建立拓撲／BVH；只有真正沾到油漆的材質才建立兩張 Render Target 及覆蓋元件。首次建立覆蓋時強制上傳遮罩，不等下一個表面更新間隔。清除未命中表面不分配 GPU 資源。
- 增加 ballistic、mask、film、drip 與 geometry／coating 分段 CPU 日誌，使用 `OK_PAINT_PROFILE=1`。mask 與 coating 是本輪測到的主要剩餘成本。
- 離線 Session 以共用 `UKKSharedAnimation` 資產的完整 `Source|Clip` 身分解析原始曲線。曲線使用既有原生評估器、APNG 保存的人物形體與骨架，不需 KKS 安裝。不依賴共用資產的固定 Unreal 路徑或特定卡片。
- APNG 動畫來源選單直接列出已匯入的共用片段；Unreal 資產查詢在 GameThread 上執行，跳過 KKS 檔案準備 worker。預設走路在 portable 模式選擇已匯入的原始 walk 片段。
- 原始 clip 資產不含完整 Animator controller state graph；尚未匯入的控制器狀態明確提示缺少資源，保留原播放，不猜測替換動作。現有共用走路可用；不能宣稱所有 Studio／雙人控制器都已離線匯入。

## 測試標準

動畫性能 audit 必須成功選取動畫，先保存動畫第 0 幀，並證實 64 個分散身體頂點在後續播放期間改變；僅送出 SetPoseMode 不算通過。噴射後再量測八秒，並再次檢查質量守恆。一般窗口壁鐘／GFrameCounter 代表整幀時間，另外報告水槍 Actor CPU，沒有推算或捏造 GPU FPS。

完整 Unreal 5.8.3 Development Editor build；0979 與匹配依賴保存於 `Saved/Tests/PaintSpray396/ModuleSnapshots/0979`。相同依賴快照依序執行測試，未並行跑 GPU benchmarks。

`Lazy285RegressionR1` 的 Camera.KksMouse、Paint.Film、Paint.MaskParity、Paint.Scene 全部 Success。包含世界投影 oracle、工作執行緒新圖塊配置、殘留足跡、跨 UV、30／60／120 幀率、隨機精確掃掠比較、GPU 遮罩讀回、Morph／非等比縮放、逐幀附著及選擇性清除。

`Lazy285SourceMenuR1` 實際操作 APNG→KKS→APNG ComboBox、空遊戲路徑載入、共用來源／片段 ComboBox、無效片段拒絕、返回 T-pose、同一卡重用及私有副本檔案變更後重新載入。首次 6554.697 ms，重開 1.899 ms；首次仍要建立 Unreal 資源，不保證每張 APNG 首次快於已快取的 KKS 卡。

最終 0979 測量如下。March 為原 KKS 卡（48 表面／86,992 面）；APNG 為 KK_346095（46 表面／72,601 面），遊戲路徑留空。不同人物不作格式效能倍率比較。每輪都噴射十秒（前兩秒暖機），停止後量測八秒。

| 測試 | 噴射平均整幀 ms | 停止平均整幀 ms | 停止水槍 CPU 平均／P95 ms | 動畫第 0 幀後最大頂點位移 cm |
| --- | ---: | ---: | ---: | ---: |
| Lazy285NativeAnimatedR1 | 40.426 | 51.277 | 22.532／25.894 | 68.792 |
| Lazy285PortableAnimatedR1 | 48.404 | 83.863 | 59.802／74.549 | 66.255 |
| Lazy285PortableAnimatedR2 | 28.096 | 36.519 | 22.161／30.066 | 66.264 |
| Lazy285NativeStaticR1 | 6.955 | 25.399 | 17.953／39.604 | 0.000 |
| Lazy285NativeStaticR2 | 6.190 | 10.388 | 6.157／17.244 | 0.000 |
| Lazy285PortableStaticR1 | 35.462 | 27.161 | 21.475／45.404 | 0.000 |

全部 valid1／exit0；噴射與停止後質量誤差約 10⁻¹²。靜止位移為零，動畫實際移動 66–69 cm。March 靜止只建立 16 張遮罩，動畫 24 張，原本候選 48 表面全部預建是 96 張。APNG 動畫 30–32 張、靜止 28 張，原本是 92 張。

同版本重測 March 靜止 10.388–25.399 ms，APNG 動畫 36.519–83.863 ms，波動不能隱去或只報最快值。March 動畫本輪 51.277 ms；有改善，**尚不能宣稱全部動畫 60 FPS 或所有卡片無性能問題**。剩餘主要成本仍是大量已濕圖塊的表面運輸、覆蓋網格及人物動畫；此次沒有完整 GPU 薄膜求解器。0977 的中間測量只作調查紀錄。失敗 `Buffer285PortableAnimatedR1` 是錯誤證據，不作性能資料。

## 使用

重啟根目錄 `CardReaderDevelopment.cmd`。讀卡清單頂部選 APNG 或 KKS 來源；APNG 可指定自己的資料夾。APNG 3D 模式可按走路，或在動作來源選 `studio/anime/00.unity3d`，再選 `f_aruki_00_00_01`。需要其它離線動作，先使用既有共用原始動畫匯入流程匯入對應曲線資產。

交付正常 DLL 與 0979 完全相同，SHA256 `F36FD74AA782D36D6EB7F3696F5DE25BE9951A412BDA535D66AB54A17E4B516C`。完整 build `Lazy285BuildR1.log` Succeeded；啟動器原 KKS／指定 APNG 目錄 DryRun 均通過。
