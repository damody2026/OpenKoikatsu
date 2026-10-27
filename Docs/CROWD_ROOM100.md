# 百人房間（CrowdRoom100.cmd，2026-10-27，E 線）

`CrowdRoom100.cmd` → `Tools/run_crowd_room100.lua`：用最新的 StreetSocial stage 開 StreetSocial 地圖並加上 `-CrowdRoom=<manifest>`，街頭的其他東西都不啟動。角色和街頭女主是同一套 runtime（`OKStartCrowdRuntimeScene`，外部控制）：同樣的共用材質、GPU 姿勢、陰影與畫面設定（TAA、CSM、關遮擋查詢、關 CAS）。舊的 CrowdRoom 收據管線在 10-20 重開倉庫之後已經不存在（`Docs/CROWD_ROOM.md` 是當時的紀錄）。

## 行為（`Source/OpenKoikatsu/OKCrowdRoom.cpp`）

- 房間：高空 48 m，地板與四面矮牆用引擎 Cube。
- 每人站 8～25 秒。之後 3 成機率走到附近 3～10 m 的空位，7 成換一種站姿繼續站；同時在走的人約 1 成。走路速度與轉身限制同街頭（110 cm/s、145°/s）。前面有人就等，等 3 秒還被擋就換目的地。
- 站姿：manifest `idle_variants` 是站姿池（Studio 立ち 2～30、待機 1～3、看窗外／天空／瀑布、對話等待姿勢，都不拿道具）。每人依卡片雜湊挑 3 種，烘進自己的動作庫；`OKSetCrowdRuntimeIdleVariant` 切換。基本站姿用 1.33 s 的立ち1。
- 鏡頭：WASD、Q/E、右鍵轉向、Shift 加速。

## 量測方式

- `--benchmark <秒>`：全員畫出並暖機後量測，寫 `report.json`（平均／P95／P99、game／render／GPU ms、draw 數、三角形數）與截圖，然後結束。數字與街頭 `/fps` 同源（`GAverageFPS` 之外另記每幀時間）。
- `--cases <json>`：同一個遊戲裡依序套用主控台指令、各自暖機後量測（A/B 不用重載 100 人）。`--trace`：第一段量測錄 Insights trace。
- 量測時只開這一個 Unreal 遊戲，`t.MaxFPS 0`。分析：`UnrealInsights.exe -OpenTraceFile=… -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimingEvents <csv> -threads=RenderThread* -columns=ThreadName,TimerName,StartTime,EndTime,Depth"`，再用 `Tools/summarize_crowd_events.lua`（PowerShell 呼叫；從 bash 傳引號會被吃掉）。

## 結果（1920×1080，100 人，只有這一個遊戲）

| 版本／設定 | FPS | 每幀 ms | P95 ms | game | render | GPU | draw |
|---|---:|---:|---:|---:|---:|---:|---:|
| Room100D（C 另有一個遊戲） | 89.3 | 11.20 | 12.71 | 2.46 | 11.19 | 7.60 | — |
| Room100F base | 109～112 | 8.93～9.17 | 9.67～9.95 | 1.9 | 9.0 | 6.9 | 10,079 |
| 關深度預先 pass（`r.EarlyZPass 0`） | 115.8 | 8.64 | 9.26 | 1.81 | 8.63 | 6.85 | |
| Room100G 描邊 LOD 15 m（`ok.Crowd.OutlineRange 1500`） | 113.9 | 8.78 | 9.40 | 1.65 | 8.77 | 5.18 | 5,773 |
| **描邊 LOD 15 m＋關深度預先 pass（現在的預設）** | **121.6** | **8.23** | 8.89 | 1.64 | 8.22 | 5.04 | 5,793 |
| 同一輪 base 重測 | 104.7 | 9.55 | 10.30 | 1.81 | 9.54 | 7.30 | 10,083 |

- 平均已過 120 FPS，P95 還沒到 8.33 ms。
- 瓶頸是 draw call：每人約 35 個材質 section 加 21 個描邊殼。trace（Room100E2）裡 render thread 一半時間在等工作執行緒送繪製指令（`ParallelDraw`，每幀約 35 ms worker 時間）。動態陰影、半透明、命令清單大小、動態 instancing 的影響都在漂移範圍內。
- 描邊 LOD 用 `ShowMaterialSection` 隱藏描邊殼 section（不畫，不是透明），有遲滯，每幀最多切換 4 人；預設 0（街頭不變）。
- 載入：100 人約 150～185 秒全部畫出（GPU 姿勢磁碟快取熱的時候）。

## 記憶體

每個角色的動作庫是「每個 clip 用她自己的身形解算好的骨骼矩陣」（60 Hz）。照搬街頭的長 clip，每人 60～116 MB，100 人超過 4 GiB 預算；只放短 clip 後全體約 1.03 GB。動作越多，記憶體跟著「人數 × 動作數」成長，所以下一步改成 GPU 即時骨架（共用動畫曲線＋每人的形狀骨修正，每幀在 GPU 組合），見之後的文件。
