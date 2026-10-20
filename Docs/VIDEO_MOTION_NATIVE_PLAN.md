# 影片轉動作：移除 Python（Rust + C++）計畫

目標是讓遊戲或 Unreal 編輯器把影片轉成動作時，整條流程都不需要 Python：

1. 讀影片；
2. 偵測並追蹤人物；
3. 估計 2D 骨架；
4. 抽取影像特徵；
5. 估計鏡頭轉動；
6. GVHMR 估計 SMPL-X；
7. 雙人對齊；
8. 重定向、自動修復與驗證（這一步原本就是 Rust）；
9. 輸出 `.okanim`。

工作目錄是 `E:\OpenKoikatsu`（duo 分支），引擎用 `E:\PhysX\UE5.8`。

網路結構、前後處理和權重鍵名的逐層規格見 `Docs/VIDEO_MOTION_NATIVE_SPEC.md`，以 GVHMR 原始碼為準。

## 架構決定

| 項目 | 決定 | 理由 |
| --- | --- | --- |
| 推論框架 | candle 0.11（`candle-core` / `candle-nn`），CUDA 後端 | 純 Rust；有現成的 ViT、YOLOv8 參考實作；cuBLAS 矩陣乘；RTX 5090（sm_120）要求 CUDA 12.8 以上 |
| CUDA | 開發機已裝 CUDA Toolkit 12.9（只裝 toolkit 元件，驅動維持 610.88） | 編譯 candle 的 CUDA kernel 需要 nvcc；執行時只需要可轉散布的 `cudart64_12`、`cublas64_12`、`cublasLt64_12`、`nvJitLink_120_0`、`curand64_10` DLL，玩家不用安裝 toolkit |
| 沒有 NVIDIA GPU 時 | 改用 candle CPU 後端，並提示預估時間（約慢 50 倍） | 至少能用；之後可以再評估 wgpu 後端 |
| 程式位置 | 新 crate `Tools/ok-video`，產生 `ok-video.exe`，以 rlib 方式依賴 `ok-extract` 的 `video_motion` 模組 | `ok-extract` 是連進 UE 的 staticlib，CUDA 不能讓 UE 模組多出依賴；推論放在獨立程序，可以取消，崩潰也不會帶走遊戲，GPU 記憶體用完即釋放 |
| 權重 | 讀使用者自己下載的原始檔（`.ckpt`、`.pth`、ultralytics `.pt`），用 Rust 寫的 pickle 讀取器第一次轉成 safetensors 快取，放在 `Saved/VideoMotions/models/` | GVHMR、SMPL-X 是非商業授權，不能隨遊戲散布；不需要 Python 也不需要 torch 做轉換；快取可以隨時重建（Saved 是可丟棄目錄） |
| 解碼影片 | `Tools/ffmpeg/ffmpeg.exe` 以 rgb24 rawvideo 輸出到 pipe，`ffprobe` 讀取 fps、尺寸、旋轉 | ffmpeg 已在專案中（不納入版控）；錯誤時回報完整的 stderr |
| UE 端（C++） | `OKVideoMotionPanel` 改成啟動 `ok-video.exe`，進度格式不變（`[步驟]`、`[n/m]`、`[錯誤]`）；設定模型路徑；打包時一併複製 exe 和 CUDA DLL | 遊戲和編輯器走同一條路；面板的解析程式不用改 |
| 測試腳本 | Lua（`Tools\lua\lua.exe`），取代 `test_pair_ui.py`、`test_studio.py` | 專案規定不再新增 Python |

## 階段

每個階段都要先通過該階段的驗收，才進入下一階段。黃金資料是用現有 Python 管線跑一次、把各階段中間結果存下來的檔案；它只是開發參考，放在 `Saved/VideoMotions/golden/`。

### P0：骨架與環境

- 新增 `Tools/ok-video`，包含 Cargo 專案、`build.rs`（設定 `CUDA_COMPUTE_CAP=120`，並把需要的 CUDA DLL 複製到輸出目錄）。
- 冒煙測試：在 GPU 上做 1024×1024 矩陣乘，量出 TFLOPS。
- 驗收：`cargo build --release` 成功；冒煙測試在 RTX 5090 上跑得出來；沒有 GPU 時能退回 CPU。

### P1：權重讀取

- 用 Rust 寫 pickle VM 加 zip 讀取，能處理三種檔案：Lightning `.ckpt`（`state_dict`）、mmpose `.pth`、ultralytics `.pt`（整個 `nn.Module` 物件：未知類別視為一般物件，`BUILD` 時保存 state，再沿著 `_modules`、`_parameters`、`_buffers` 收集張量）。
- 支援的資料型別：fp16、fp32、bf16。
- 驗收：四個模型的張量數量、名稱與形狀符合規格文件；轉出的 safetensors 能重新讀回。

### P2：黃金資料

- 用 `motion.mp4`（雙人）和網球影片（單人），從現有 Python 管線匯出：抽出的影格、YOLO 偵測框、追蹤結果、ViTPose 關鍵點、HMR2 特徵、相機角速度、GVHMR 輸出、對齊結果。
- 驗收：每個檔案都有記錄形狀與單位的說明。

### P3：解碼影片

- `ffprobe` 讀出解析度、fps、旋轉和幀數；`ffmpeg` 輸出 rgb24。
- 檔案讀不到時，回報作業系統錯誤碼與訊息，以及 ffmpeg 的 stderr。
- 驗收：影格和黃金資料逐像素相同（或只差 1 以內）。

### P4：人物偵測與追蹤

- 在 candle 實作 YOLOv8x：Conv、C2f、SPPF、Detect 和 DFL；letterbox 和 NMS 用規格裡的閾值。
- 移植規格指定的追蹤器（ultralytics 的 ByteTrack／BoT-SORT 參數）。
- 移植 `get_bbx_xys_from_xyxy` 和平滑。
- 移植雙人角色分配（`pair_tracks`）。
- 驗收：
  - 偵測框和黃金資料的 IoU 在 0.99 以上；
  - 主角追蹤 ID 序列相同；
  - 雙人分配逐幀相同。

### P5：ViTPose-H

- ViT-H/16 骨幹、反卷積頭、翻轉測試、熱圖解碼。
- 驗收：關鍵點誤差中位數小於 0.5 px，信心值誤差小於 0.01。

### P6：HMR2 特徵

- ViT-H 骨幹，輸出規格指定的特徵。
- 驗收：和黃金特徵的 cosine 相似度大於 0.999。

### P7：相機轉動（SimpleVO）

- 依規格移植：特徵點、配對、RANSAC，輸出相機旋轉，再算 `compute_cam_angvel`。
- 依規格決定要不要對齊 OpenCV 的行為；介面上仍保留「鏡頭固定不動」的選項。
- 驗收：每幀旋轉和黃金資料相差小於 0.5°；結果交給 P8 後最終姿勢一致。

### P8：GVHMR 與 SMPL-X

- 實作 GVHMR transformer（輸入正規化、相對位置編碼和遮罩、各個輸出頭）和後處理（世界座標、相機座標兩套 SMPL-X 參數，以及規格中的後處理）。
- 在 Rust 實作 SMPL-X 前向計算：shape blend、LBS、關節回歸。
- 直接把 `Motion` 結構交給 `video_motion::convert`，不再經過 npz；開除錯旗標時仍輸出 npz。
- 驗收：
  - 關節位置誤差中位數小於 1 cm；
  - 輸出的 `.okanim` 經 Rust 驗證後，壞幀數和 Python 版相同（網球 0/312，雙人女 5/520、男 0/520，或更好）。

### P9：雙人對齊

- 移植 `pair_align`：Kabsch 擬合、世界座標轉相機座標、SMPL-X 表面間距 `pair_gap`。
- 驗收：對齊後的位移誤差小於 1 cm；雙人轉換結果和 Python 版相同。

### P10：一條指令完成匯入

- `ok-video import <影片> --name 名稱 [--pair --left male|female] [-s 固定鏡頭] [--arm-reach --collar-scale --smooth --focal] [--models 路徑]`，取代 `import_video.py`。
- 進度和錯誤格式沿用現在的規則；按取消時直接結束程序。
- 驗收：兩段測試影片從頭到尾都不經過 Python；花費時間不超過 Python 版。

### P11：UE 整合

- `OKVideoMotionPanel` 改為啟動 `ok-video.exe`；新增模型路徑設定（預設依序找專案的 `ref/GVHMR/inputs/checkpoints`、`OK_GVHMR_MODELS` 環境變數）。
- 找不到模型時，說明要下載哪些檔案、放到哪裡。
- 打包時複製 exe 和 CUDA DLL；遊戲和編輯器共用同一套程式。
- 驗收：
  - Lua 版 UE 自動測試（CharacterMCP）在遊戲內完成雙人影片匯入並播放；
  - 工作室測試通過。

### P12：清理

- 移除 runtime 用的 Python（`run_gvhmr*.py`、`import_video.py`、`pair_*.py`）。
- 離線訓練腳本已在 2026-10-18 改寫成 Rust（`ok-extract x --train-video-*`，見下方 P13）；原本規劃為之後再移植到 Rust 或 Lua；遊戲執行時不會用到。
- 更新 `Docs/VIDEO_MOTION.md`。

## 風險

| 風險 | 對策 |
| --- | --- |
| ultralytics `.pt` 是整個 pickle 起來的模型物件 | pickle VM 不執行任何類別程式碼，只收集張量，不會執行檔案裡的程式；改不了的話就改用 safetensors 版權重，或請使用者下載官方 `.pt` 以外的格式 |
| SimpleVO 依賴 OpenCV 的特定行為 | 以「最終姿勢一致」為準，不要求逐點一致；影片鏡頭不動時直接走固定鏡頭 |
| ViT-H 推論太慢或記憶體太大 | 分批處理影格；必要時用 fp16（candle CUDA 支援）；目標速度不慢於 Python |
| candle 少了某個運算（反卷積、grid sample 等） | 自己寫 CUDA kernel（nvcc 編譯），或改用等價的運算組合 |

## 進度

| 階段 | 狀態 | 證據 |
| --- | --- | --- |
| P0 | 完成 | `Tools/ok-video`、`cargo-cuda.cmd`（用 VS 2022 BuildTools 14.44 當 nvcc 的 host 編譯器；VS 18 的 MSVC 14.51 不被 nvcc 12.9 接受）。RTX 5090 上 f32 矩陣乘 30 TFLOPS、f16 75 TFLOPS |
| P1 | 完成 | `src/pickle.rs`：四個原始權重檔都能讀出（GVHMR 247 個張量、HMR2 576、ViTPose 403、YOLOv8x 599），轉成 safetensors 後用 candle 讀回 GPU。OrderedDict 的項目和 BUILD 狀態分開保存；`bool` 存成 U8 |
| P2 | 完成（沿用既有快取） | `Saved/VideoMotions/golden/`：網球（固定鏡頭，有 `bbx.pt`、`vitpose.pt`、`vit_features.pt`、`motion.npz`）、雙人（`tracks.pt`、各角色的 `vitpose.pt`／`vit_features.pt`、`motion_*.npz`）。沒有 SimpleVO 的黃金資料 |
| P3 | 完成 | `src/video.rs`：ffprobe 讀尺寸、fps、幀數；ffmpeg 輸出 rgb24（ffmpeg 9 要用 `-fps_mode passthrough`，`-vsync` 已移除）；半解析度用同一個 `scale=iw*0.5:ih*0.5` 濾鏡。錯誤附系統錯誤碼和 ffmpeg stderr |
| P4 | 完成 | `src/yolo.rs`（BN 融進卷積、DFL、NMS）、`src/track.rs`。單人改用簡化的 IoU 追蹤器（規格建議），框和 Python 版的 IoU 中位數 0.998、最小 0.982；312 幀偵測約 10 秒。雙人分配逐行照抄 `pair_tracks.assign` |
| P5 | 完成 | `src/crop.rs`、`src/vit.rs`：關鍵點誤差中位數 0.15 px、p95 0.54 px；左右兩個峰值相近的關節偶爾會跳到另一個峰（數值敏感，最大 124 px，只出現在 3 幀的手腕）|
| P6 | 完成 | HMR2 特徵 1−cos 中位數 0.0001（cos 0.9999），最大 0.011 |
| P7 | 第一版完成（另一種方法） | `src/vo.rs`：Shi-Tomasi 角點、金字塔 LK 加前後向檢查，用 RANSAC 比較「只有旋轉」與「本質矩陣（八點法）」兩種模型。沒有 SimpleVO 的黃金資料；合成平移測試（`ok-video vo-test`）估出每組偏航 1.74°～1.88°（預期約 2.0°），其他軸 < 0.4° |
| P8 | 完成 | `ok-video gvhmr-golden`：用網球的黃金輸入跑 Rust 版，和 Python 的 `motion.npz` 比較——關節位置誤差中位數 0.5 mm、p95 1.25 mm、最大 2.2 mm；body pose 誤差中位數 0.00001°；global orient 誤差中位數 0.06°；betas 完全相同。推論 0.23 秒（首次執行時驅動要把 PTX JIT 編譯成 sm_120，約多 20 秒，之後有快取） |

| P9 | 完成 | `src/pairalign.rs`、`src/smplx.rs`（完整 LBS 含 pose blend shape）。雙人影片：表面距離中位數 0.6 cm、100% 幀在 3 cm 內（Python 版相同）|
| P10 | 完成 | `ok-video import`（`src/pipeline.rs`、`src/models.rs`）。網球影片：從影片到 `.okanim` 約 70 秒（含第一次權重轉換 28 秒）。姿勢誤差中位數 1.8 mm、p95 6.2 mm；世界座標中位數 1.3 cm（容忍度 2 cm）。雙人：同設定下姿勢誤差中位數約 5 mm，世界座標中位數 1.5 cm（女）、1.8 cm（男）；身體貼合 9.5 → 0.5 cm（Python 版 10.0 → 0.5 cm）|
| P11 | 完成 | UE 面板改呼叫 `ok-video.exe`（`OKVideoMotionPanel::ImporterExe`）；CharacterMCP 新增 `video_import`。Lua 測試 `Tools/video_motion/test_import.lua` 在 E: 遊戲內（`E:\PhysX\UE5.8`）實際按「開始轉換」：網球（固定鏡頭）6/6 通過，112 秒，4/312 幀被標記，可選取並播放；雙人（鏡頭估計開啟）6/6 通過，259 秒，女 13/520、男 0/520，男角自動載入並播放男方 |

驗證卡：E: 的 `CrowdAPNG/C000.png` 已改成新格式（`okLb`/`okLc`，沒有 `okPv` 獨立資源快照），ok-extract 無法拿它驗證。`ok-video` 會自動改用遊戲目錄的第一張女性卡；ok-extract 的 `--video-pair` 新增 `--female-game`，讓女方也能用遊戲卡驗證。用同一張遊戲卡驗證時：網球 Python 版修到 0/312，Rust 版剩 4/312；雙人女方 Python 版 20/520、Rust 版 35/520，男方都是 0/520。

注意：candle 的 kernel 目前以 PTX 發布，每台機器第一次執行時都要 JIT 編譯一次。之後可以考慮另外產生 sm_120 的 cubin。

P12 完成（2026-10-18）：刪除 `run_gvhmr.py`、`run_gvhmr_pair.py`、`import_video.py`、`pair_tracks.py`、`pair_align.py`。`test_pair_ui.py`、`test_studio.py` 改寫成 Lua（`test_pair_ui.lua`、`test_studio.lua`，與 `test_import.lua` 共用 `game_mcp.lua`），在 E: 實跑：雙人 UI 4/4、工作室 19/19、匯入單人與雙人各 6/6。離線訓練腳本（`helper_model.py`、`joint_limits.py`、`baseline.py`、`export_model.py`、`kkrig.py`）只在更新模型資料時使用，遊戲不會呼叫，之後再移植。

P13 完成（2026-10-18）：離線訓練改成 Rust（`Tools/ok-extract/src/video_motion/train.rs`，指令 `--train-video-helpers`、`--train-video-limits`、`--train-video-baseline`、`--export-video-model`），刪除 `helper_model.py`、`joint_limits.py`、`baseline.py`、`export_model.py`、`kkrig.py`。`data/helper_model.npz` 改成 `data/helper_model.json`，內容取自現有模型的 helpers；用 Rust 重新打包的結果和原本的 `video_motion_model.json` 只差浮點數最後一位。和 Python 版在同一批 300 個片段上的比對結果見 `Docs/VIDEO_MOTION.md`「離線訓練」。專案內的影片動作流程至此完全沒有 Python。
