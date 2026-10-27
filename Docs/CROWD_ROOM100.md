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

## GPU 即時骨架（`--gpu-rig`，開發中）

每個女角的骨架改成每幀在 GPU 上用參數解算，不再每人每個 clip 烘一份矩陣。所有角色共用同一份動畫 clip（`OKGC`）。每個角色只留下自己的身形骨（基準 local、前臂／手修正、PV／ExpressionBone、ABMX），以及每個 render 骨的「權重 × 某根 body 骨的世界矩陣 × 常數」（`OKGR`，`Tools/ok-extract/src/gpu_rig/export.rs`）。

- Shader：`Plugins/KKCrowdGpuPacking/Shaders/GpuRig.usf`。`SkeletonCS` 每個角色一個執行緒，跑完原生每幀管線的非物理部分。`TaskCS` 每個 pose task 一個執行緒，寫出和 bank 相同格式的列，palette pass 之後的流程都不變。
- 常數的求法：
  - body 蒙皮 part 的第 j 格就是 `body_skel.bone_ids[j]`。
  - 蒙到 body 上的飾品，第 j 格就是同名的 body 骨。
  - 其他（頭部、私有骨架的飾品）以 4 個取樣時間搜尋能剛性帶動它的骨頭。頭部骨跟著 `cf_s_head`。
  - bank 的別名骨列（只留一欄、乘 3、乘 InverseBind）在建 batch 時併進常數。
- 新的 global shader 要 cook 才進得了打包版。開發期用 `--editor`（E 引擎的 `UnrealEditor -game`，不 cook）。

### 和完整解算對照（`--gpu-rig-verify N`）

bank 保持完整長度，前 N 幀兩邊都算並讀回逐列比較，結果寫進 `runtime.json` 的 `gpu_rig`。另外存兩個檔：

- `rig.bin`：兩邊的列，可用來逐列分析。
- `rig.txt`：每一列對應的角色與骨頭。

正在交叉淡入的角色不列入：bank 是把兩段各自解算後的最後矩陣混合，rig 是先混合 locals 再解算一次，兩者都不是原生的交叉淡入。

2026-10-27，10 位女角，20 幀（`Saved/Crowd/Room100-20261027T184625`）。沒在交叉淡入的 17307 列：

| 位移誤差 | 列數 | 比例 |
|---|---:|---:|
| < 0.01 cm | 13869 | 80.1% |
| 0.01～0.05 cm | 256 | 1.5% |
| 0.05～0.5 cm | 947 | 5.5% |
| 0.5～2 cm | 894 | 5.2% |
| 2～10 cm | 930 | 5.4% |
| ≥ 10 cm | 411 | 2.4% |

- 平均 0.76 cm，最大 36 cm。
- 大誤差都在物理骨：裙子的 `cf_j_sk` 類骨頭、胸部、頭髮與飾品鏈。GPU rig 目前不模擬物理，這些骨頭被當成剛性跟著父骨。
- 手與前臂另有 0.4～1 cm 的差，還沒查。
- 用 Rust 照遊戲流程（snapshot → `apply_anim_frame`）在 6 個片段上檢查，body 與非物理 part 的常數誤差都是 0（`gpu_rig::check_terms`）。

### FPS 與記憶體（2026-10-27，Editor `-game`，1920×1080，100 人，只有這一個遊戲）

兩輪都用 `UnrealEditor -game`，不 cook。新的 shader 還沒進打包版，所以拿 Editor 對 Editor 比較。

| 模式 | FPS | 幀 ms（P95） | game / render / GPU ms | draw | 姿勢資料 |
|---|---:|---:|---:|---:|---:|
| 每人解算的 bank（`aggregate_gpu:false`，房間預設） | 135.5 | 7.38（7.98） | 2.49 / 7.37 / 4.94 | 5876 | 約 2.4 GB（彙總時量到的 key bytes） |
| GPU rig（`--gpu-rig`，彙總 provider） | 161.3 | 6.20（6.64） | 2.12 / 6.19 / 5.48 | 5740 | bank 只剩 61 MB（每 clip 兩個取樣，只用來對齊資料格式）；rig 共用 clip 99 MB，加上骨架與 term 約 16 MB |

- GPU 時間多了 0.54 ms，是兩個 compute pass（`Crowd.GpuRigSkeleton`、`Crowd.GpuRigTasks`）的成本。FPS 提高主要來自 render thread：100 個 provider 合成 1 個彙總 provider 後少了 1.2 ms。
- 動作變多時：bank 是「人數 × 動作 × 長度」，GPU rig 只是「動作 × 長度」（所有人共用）加上每人固定的約 1 MB。
- 載入：100 人全部畫出 bank 126 秒、GPU rig 130 秒。rig 每人多一個 0.2～0.45 秒的 worker 準備。
- 用 bank 加 `--aggregate` 量第一輪時，彙總需要 2×2.4 GB，超過 4 GiB 預算。被拒之後當在 `FLateSourceDepth::Draw`（`OKSourceMaterialMesh.cpp:103` 釋放 shared pointer 時存取違規）。這是既有的失敗路徑，已記在 ERROR_LOG。
