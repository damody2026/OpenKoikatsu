# 影片動作（GVHMR）

> 2026-10-18 起，整條流程不再需要 Python：推論改由 `Tools/ok-video`（Rust、candle、CUDA）執行，見 `Docs/VIDEO_MOTION_NATIVE_PLAN.md` 與逐層規格 `Docs/VIDEO_MOTION_NATIVE_SPEC.md`。本文件以下若提到 `import_video.py`、`run_gvhmr*.py`、`pair_align.py`，指的是舊流程，只作開發參考。

把一段人物影片轉成戀活骨架動作（`.okanim`，OKCLIP01），在 Gallery 和互動模式中播放。轉換過程會自動修復並逐幀驗證。

```
影片 ─ ok-video import（Rust＋CUDA，Tools/ok-video）：YOLOv8x → ViTPose-H／HMR2 → GVHMR ─► motion.npz（SMPL-X 世界座標，30fps）
     ─ ok-extract x --video-clip（Rust）：轉換 → 自動修復 → 執行期驗證 → 調整參數，迴圈
     ─► Saved/VideoMotions/<名稱>.okanim + <名稱>.validation.json
     ─ 遊戲：「影片動作：<名稱>」／互動模式來源「影片動作」
```

## 使用：角色編輯介面

動作欄位「選擇動畫」下方有「影片動作（自行匯入）」區塊，實作在 `Source/OpenKoikatsu/OKVideoMotionPanel.*`。

| 元件 | 功能 |
|---|---|
| 影片動作下拉選單 | 只列使用者匯入的影片動作，與上方的內建動作分開；展開後可輸入文字搜尋，選取後立即播放。 |
| 選擇影片… | 開啟 Windows 選檔視窗，自動填入檔案路徑，並以檔名作為動作名稱。 |
| 刪除選取的動作 | 4 秒內按兩次才會刪除，連同 `.json`、驗證報告與工作目錄一起移除。只能刪除自行匯入的動作。 |
| 影片檔／動作名稱 | 名稱可用中文；同名會覆蓋。 |
| 鏡頭固定不動 | 預設勾選，固定機位的影片效果最好；手持或跟拍的影片請取消。 |
| 進階參數 | 已預填建議值，欄位標題和提示都有說明，一般不需修改。 |
| 開始轉換／取消轉換 | 在背景執行 `Tools/ok-video/target/release/ok-video.exe import`，狀態列逐行顯示進度（偵測、關節與特徵的幀數、GVHMR、修復輪次）。完成後自動加入清單並播放；轉換中再按一次即可取消。 |

**進階參數（預設即建議值）**

| 參數 | 預設 | 範圍 | 意義 |
|---|---|---|---|
| 手臂比例修正 | 1.0 | 0–1 | 戀活手臂比真人長約 20%。1.0 讓垂下的手落在臀側，手肘自然微彎；0 照抄角度，手會垂到大腿中段。 |
| 鎖骨幅度 | 0.5 | 0–1 | 影片中的鎖骨轉動通常比原作大，0.5 可避免肩膀拉伸；聳肩動作明顯時可提高到 0.8。 |
| 平滑程度 | 1.0 | 0–2.5 | 去除辨識抖動。仍偵測到抖動時，閉環會自動加強。 |
| 鏡頭焦距（mm） | 自動 | — | 只有知道拍攝焦距時才填，例如手機主鏡頭約 24、2 倍約 48。 |

**拍攝建議**：畫面中只有一個人、全身入鏡、光線充足、鏡頭固定。

互動模式的動作來源選單也有「影片動作」，同樣可以播放。

## 使用：命令列

```powershell
Tools\ok-video\target\release\ok-video.exe import 影片.mp4 --name 動作名稱 -s [--game <KKS 目錄>]
```

建置（只需一次）：`Tools\ok-video\cargo-cuda.cmd build --release`。需要 CUDA Toolkit 12.8 以上（RTX 50 系列）與 VS 2022 BuildTools 的 MSVC。建置後 `cudart64_12`、`cublas64_12`、`cublasLt64_12`、`curand64_10` 會複製到 exe 旁，執行時不需要 toolkit。第一次執行會把原始權重轉成 `Saved/VideoMotions/models/*.safetensors`（約 30 秒），驅動也會把 GPU 程式編譯成本機版本（約 20 秒），之後都有快取。

模型位置依序找：`--models`、環境變數 `OK_GVHMR_MODELS`、專案根目錄 `local-instance.json` 的 `"gvhmr_models"`、`ref/GVHMR/inputs/checkpoints`。需要 `gvhmr/gvhmr_siga24_release.ckpt`、`hmr2/epoch=10-step=25000.ckpt`、`vitpose/vitpose-h-multi-coco.pth`、`yolo/yolov8x.pt`、`body_models/smplx/SMPLX_NEUTRAL.npz`。

| 選項 | 說明 |
|---|---|
| `-s` | 鏡頭固定不動。鏡頭有移動時不要加，GVHMR 會改用 SimpleVO 估算鏡頭運動。 |
| `--f_mm` | 已知等效焦距（mm）時可以指定。 |
| `--arm-reach` / `--collar-scale` / `--smooth` | 同上表的進階參數。 |
| `--skip-gvhmr` | 沿用已算好的 `motion.npz`，只重跑 Rust 轉換。10 秒的影片大約 4 秒完成。 |
| `--card` | 驗證用的卡。預設是 `CrowdAPNG/C000.png`；如果它沒有獨立資源快照（`okPv`），改用 `--game` 遊戲目錄 `UserData/chara/female` 的第一張卡。遊戲卡會自動把 `--game` 傳給 ok-extract。 |
| `--game` | KKS 遊戲目錄：用於遊戲卡驗證，以及雙人的男性卡。 |
| `--models` | GVHMR checkpoints 根目錄。 |
| `--no-validate` | 只轉換加修復，不跑驗證迴圈。 |

輸出檔：

- `Saved/VideoMotions/<名稱>.okanim`：動作。
- `<名稱>.json`：摘要、參數與工作目錄。
- `<名稱>.validation.json`：逐幀驗證報告。

中間檔放在 `Saved/VideoMotions/work/<名稱>/`，名稱可用中文。實測 PyAV 與 OpenCV 都能讀寫中文路徑；這台機器的 C 槽不會為新檔案產生 8.3 短檔名，因此不依賴短檔名。

**錯誤回報**：轉換程序用前綴標示輸出，遊戲介面只顯示這些行。

| 前綴 | 意義 |
|---|---|
| `[步驟]` | 目前執行的步驟：準備模型（第一次轉換權重）→ 讀取影片資訊 → 偵測人物（YOLOv8x）→ 追蹤／分配兩人 → 2D 關節（ViTPose）與影像特徵（HMR2）→ 推論 3D 動作（GVHMR）→ 寫入 |
| `[資訊]` | 影片的解析度、fps、幀數；各步驟完成時間；偵測與特徵的幀數進度 |
| `[錯誤]` | 失敗的步驟與原因：作業系統錯誤碼與訊息、ffmpeg 的 stderr、缺少的模型檔清單、ok-extract 的錯誤原文 |

影片會先檢查：檔案是否存在、作業系統能否讀取（附系統錯誤碼）、ffprobe 能否讀出影像串流與幀數。Rust 轉換失敗時，會轉印 `ok-extract` 的錯誤原文。遊戲介面在程序結束後才讀到的最後幾行也會解析，失敗時列出全部 `[錯誤]` 行與結束代碼；無法建立管道或啟動程序時，顯示 Win32 錯誤碼。

## 雙人影片（一男一女）

介面勾選「雙人影片」（以及「男性在畫面左邊」）即可，命令列則是 `ok-video.exe import 影片.mp4 --name 名稱 --pair --left male --game <KKS 目錄>`。分配兩人、各自推論、對齊與表面距離都在 `ok-video` 內完成（`src/track.rs`、`src/pairalign.rs`、`src/smplx.rs`）。

```
影片 ─ ok-video：逐幀分配兩人（src/track.rs）→ 各自 ViTPose／HMR2／GVHMR → motion_female.npz、motion_male.npz
     ─ 對齊（src/pairalign.rs）：各自擬合「世界→相機」剛體轉換，把男方換到女方世界，並逐幀量兩人 SMPL-X 表面最近距離（pair_gap）
     ─ ok-extract x --video-pair（Rust）：女方閉環 → 男方用女方的朝向／原點／地面／水平比例 → 身體貼合 → 男方閉環
     ─► <名稱>_女.okanim、<名稱>_男.okanim、<名稱>.pair.json；介面清單顯示「【雙人】名稱」
```

| 步驟 | 做法與原因 |
|---|---|
| 分配兩人（`ok-video` 的 `src/track.rs`） | YOLO 的追蹤 ID 在貼身時會互換，也會出現一個框包住兩人。改成逐幀取所有人物框，濾掉寬度超過單人 1.45 倍的合併框，依「左／右」與前一幀的連續性分配，缺漏幀內插。另外偵測鏡頭切換點並回報。 |
| 鏡頭 | SimpleVO 失敗時改用固定鏡頭，並在 `[資訊]` 說明原因。 |
| 對齊 | 兩人共用同一台相機：分別擬合 `R_cam = R·R_world`、`p_cam = R·p_world + t`，再把男方換到女方的世界座標，並回報擬合殘差。 |
| 共用基準 | 男方沿用女方的朝向、原點、地面，以及水平縮放 `xz_scale`（`--frame`），兩人的相對位置才不會走樣。 |
| 身體貼合 | 戀活角色的身體比真人薄，照搬骨盆距離會在兩人之間留下空隙。做法如下：<br>• 用執行期蒙皮取兩人的軀幹頂點（hips–neck 軸 0.22 m 內，排除環抱的手臂）。<br>• 逐幀沿女→男的水平方向，從遠處每次 1 cm 靠近，停在第一次接觸的位置，不會穿過去。目標距離為 `max(pair_gap, 5 mm)`；原始距離超過 5 cm 的幀不調整。<br>• 位移平滑後，在腳部鎖定之前套用。 |
| 男性卡 | 驗證與貼合都在遊戲目錄的男性卡上執行，所以需要 `--game`。男性骨架是 `data/rest_skeleton_male.json`。 |

**遊戲播放**：選取「【雙人】名稱」後，主角色播女方（若主角色是男性則播男方），已載入的配對角色播另一方。兩人設成同一個原點並同時開始。沒有配對角色時，會自動載入「配對男角」清單中選取的那張，未選取則用第一張，載入完成後兩人同步開始。男角清單包含目前資料夾的男性獨立卡；設定了遊戲目錄時，也包含 `UserData/chara/male`、`DefaultData/chara/male` 的遊戲卡（獨立卡模式下標示「（遊戲卡）」）。等待男角載入期間改選其他動作，會取消這次自動播放。刪除時，兩段動作、`.pair.json` 與工作目錄會一起刪除。

**`ref/motion.mp4` 的結果**（520 幀，男性在後方環抱）：

- 兩人的 2D 關節平均信心：男 0.63、女 0.73。
- 擬合殘差 0.9–2.2° / 11–13 cm。
- 原始資料中兩人 100% 的幀都在 3 cm 內。
- 貼合前軀幹間距 10 cm，貼合後 0.5 cm，男方平均移動 −12 cm。
- 驗證結果：男方 0/520；女方 5/520（低頭時頸部擠壓）。
- 雙人轉換約 17 秒，不含 GVHMR。

**限制**：

- 手部接觸（例如握住對方的手）尚未約束，只保證軀幹貼合。
- 被遮擋的一方品質較差。
- 有變焦的影片會影響深度估計。

**順帶修正的通用問題**：骨盆與 `cf_pv_*` 的位置原本只除以旋轉，沒有除以 `cf_n_height` 的身高縮放。所以身高縮放不是 1 的卡（例如男性卡是 0.936）會被縮放兩次，PVCopy 把手腕、腳踝往身體拉近，網格被擠壓。現在改用父骨完整的線性轉換（`Rig::to_local`）。

**驗證基準依性別分開**（`data/stretch_baseline_male.json`，用 400 個男性 H 片段在男性卡上統計）。手肘、膝蓋的「擠壓」依彎曲角度分組，相鄰組之間內插；原作資料不足的角度不判定，並且只在低於門檻 0.8 倍時才判定。

## 環境（只需一次）

`ref/GVHMR/` 已列入 `.gitignore`。GVHMR 和 SMPL-X 都只能非商業使用，模型檔與 checkpoint 不得隨程式散布。

| 項目 | 位置 |
|---|---|
| GVHMR 原始碼 | `ref/GVHMR/repo`（`repo/inputs` 是指向 `ref/GVHMR/inputs` 的 junction） |
| Python 3.10 venv（torch cu128，支援 RTX 50 系列） | `ref/GVHMR/.venv`；另需 `av==13.0.0`、`yacs` |
| pytorch3d 替代（transforms、純 torch 的 knn，renderer 為空殼） | `ref/GVHMR/p3d_shim` |
| SMPL-X v1.1（`models_smplx_v1_1.zip` 內的 npz） | `inputs/checkpoints/body_models/smplx/` |
| checkpoint：gvhmr、hmr2、vitpose-h | HF 鏡像 `camenduru/GVHMR`；sha256 與 LFS oid 一致，以 `weights_only=True` 載入 |
| yolov8x.pt | Ultralytics 官方 release v8.2.0 |

同目錄另外三個 121MB 的 `SMPLX_*.npz` 缺少 `hands_meanl` 等欄位，GVHMR 無法使用。

## 轉換與自動修復（Rust：`Tools/ok-extract/src/video_motion/`）

| 檔案 | 內容 |
|---|---|
| `retarget.rs` | SMPL 前向運算、骨架對應、各項修復、輸出 OKCLIP（沿用 `anim/shared.rs` 的結構與驗證） |
| `validate.rs` | 在真實 session 上播放並逐幀取樣（含身形、輔助骨、物理、蒙皮），比對各項檢查 |
| `mod.rs` | `convert()` 閉環；另有診斷指令 |
| `model.rs`、`rig.rs`、`npy.rs`、`math.rs` | 訓練資料、骨架與 SMPL-X 讀取、數學 |

**基本轉換**

- 座標：SMPL 是右手系，鏡射 X 軸轉成 Unity 左手系；角色的左側對應到 `_L` 骨。
- 每根對應骨：`W_b = G_j · S_b · W_b(rest)`。只有大腿、小腿、上臂、前臂做靜止方向對齊。
- SMPL-X v1.1 範本本身左右不對稱（靜止時手肘彎 22.7° 對 6.9°），所以先把左右關節做對稱平均。
- 綁定路徑用 `cf_j_root/...`，CRC32 與原作片段一致。
- 動作檔和原作 Studio 片段完全同型，三點缺一不可：
  - **場景用 Studio**：遊戲的 ExpressionBone（`cf_s_elbo`、`cf_s_elboback`、`cf_d_forearm02`、`cf_d_wrist`、膝蓋等修正骨）只在 Studio 片段上，於執行期依關節角度計算，片段本身不寫這些骨。用 Normal 時手肘內側會被擠成折線，`cf_s_elboback` 會偏 58 cm。
  - **寫入 `cf_pv_hand_L/R`、`cf_pv_leg_L/R`**：Studio 的 PVCopy 會把這些目標的世界位置、旋轉、縮放複製到手和腳踝，縮放 1 也會蓋掉手掌 0.9 的尺寸修正。不寫的話，手腳會被釘在 T-pose 位置。
  - **每個主關節寫入單位縮放**。
- 對照工具 `ok-extract x --compare-native-pose <card> <commonRoot> <keyfile> <t> <skeleton> <model> [sceneMode]`：用原生方式播放一個原作片段，再只取它的主關節、走影片流程重建一次，比對骨骼與左手肘頂點。對 `walk_01`、`idle_02` 左手肘的平均誤差約 1 mm。Demo「站立 02」屬於 Maker 場景（前臂 ×0.92、不做 PVCopy），和 Studio 規則本來就不同。

**修復**（統計值都來自 1500 個原作片段）

| 項目 | 做法 |
|---|---|
| 鎖骨 | 旋轉縮為 0.5 倍。影片中位數 15°，原作 9°；這一項消除了大部分肩膀拉伸。 |
| 脊椎 | 胸口朝向不變，把整段彎曲平均分給三節。KK `spine01` 的支點只比 hips 高 5mm，SMPL spine1 約高 10cm。 |
| 關節範圍 | 依原作 0.5–99.5% 的逐軸範圍夾住，涵蓋軀幹、手腕、腳踝、腳趾（`data/joint_limits.json`）。 |
| 平滑 | 世界旋轉與來源位置做高斯平滑。 |
| 手臂比例 | 戀活手臂相對軀幹長約 20%。只把肩到腕向下的高度差依軀幹比例縮短，手就會落在臀側；水平伸直的手臂不受影響。 |
| 鉸鏈 IK | 原作的膝蓋只繞 +X 彎，手肘只繞 ±Y 彎。用兩節 IK 重解手腳，彎曲平面跟隨來源；來源彎曲小於「靜止彎曲 +10°」時不扭轉，避免照雜訊旋轉。 |
| 骨盆高度 | 由戀活自己的腿推算，讓較低那隻腳的腳踝等於來源腳踝的離地高度。 |
| 接地鎖定 | 來源腳低且慢時固定腳踝位置，過渡幀做混合，再用鉸鏈 IK 去搆。 |
| 輔助骨 | 肩、上臂扭轉、膝、臀等修正骨的回歸模型（`ok-extract x --train-video-helpers`）。 |

**驗證與閉環**（每輪約 1 秒，預設最多 4 輪，保留問題最少的那一輪）

驗證前先讓物理骨在第一個姿勢穩定 2 秒；取樣時避開循環點，因為 t=總長會繞回第 0 幀。

| 檢查 | 判定 | 自動調整 |
|---|---|---|
| floor | 網格低於地面 1.5cm，或來源接地時雙腳都懸空 | 只在那些幀抬高身體，前後漸變 |
| slide | 接地中的腳踝水平速度大於 0.15 m/s | 放寬接地判定 |
| hand | 手腕進入軀幹，或離修正後的目標超過 8cm | 只回報 |
| stretch | 各區網格邊長 p99 超過原作基準（第 99 百分位 ×1.15） | 肩膀區：再縮小鎖骨旋轉 |
| jitter | 相鄰幀折角超過原作基準（第 99.9 百分位 ×1.5） | 腿：加長鎖定過渡；手、頭：加強平滑 |

基準來自 42 個原作片段（走路、待機、ADV、美容、H），由 `ok-extract x --train-video-baseline` 產生 `data/stretch_baseline.json`。

`tennis.mp4` 的結果：第 0 輪 5 幀有問題，第 2 輪 0/312。

## 離線訓練（Rust，只在更新模型時執行）

實作在 `Tools/ok-extract/src/video_motion/train.rs`。片段的取樣直接用遊戲執行期的取樣器（`anim::resource_tracks`）。

```powershell
ok-extract <game> --export-training-clips <dir> / 1500
ok-extract x --train-video-helpers <dir> Tools/video_motion/data/rest_skeleton.json Tools/video_motion/data/helper_model.json
ok-extract x --train-video-limits <dir> Tools/video_motion/data/rest_skeleton.json Tools/video_motion/data/joint_limits.json
ok-extract x --validate-video-clip <card> <native.okanim> <dump.jsonl> [game]    （每個基準片段各跑一次）
ok-extract x --train-video-baseline Tools/video_motion/data/stretch_baseline.json <dump.jsonl ...>
ok-extract x --train-video-baseline Tools/video_motion/data/stretch_baseline_male.json <男性卡 dump.jsonl ...>
ok-extract x --export-video-model Tools/video_motion/data      → data/video_motion_model.json（執行期讀取，寫完會先用 Model::load 驗證）
```

`--export-training-clips` 的第三個參數是來源篩選字串。PowerShell 5.1 會吞掉空字串參數，所以用每個來源都包含的 `/` 代表全部。

和舊的 Python 腳本在同一批 300 個原生片段上比對（2026-10-18）：

- 關節範圍最大差 0.0000055°；
- 基準 61 個鍵完全相同（相對差 3e-15）；
- helper 回歸：367 條軌道中，偏移量最大差 0.0055，權重最大差 0.057。差異來自取樣，Rust 用的是執行期取樣器，Python 是簡化版。另有 2 條軌道剛好落在「比平均值好 20%」的門檻邊緣，兩邊各多保留了一條。

`rest_skeleton.json` 由 `ok-extract <game> --dump-rest-skeleton <out>` 產生。

## 遊戲端

- 金鑰格式是 `video:|<檔名>`，由 `FOkSession` 處理：`VideoClipNames`、`LoadVideoClip`、`HasAnimationClip`、`PrepareAnimationClip`、`SetAnimClip`。
- 獨立卡走 `InstallSharedClip`。遊戲 session 走 `ok_session_install_video_clip`，保留 session 原本的 game root，並和原作片段一樣清除 `OK_UE_ANIM`。
- 播放影片動作時，動畫狀態列會顯示 `t=秒 / 總長  第 N 幀`，幀號與 `motion.npz`、驗證報告的幀號一致。
- 【雙人】項目：女角播女方片段，配對男角播男方片段，兩人同一原點、同時開始。沒有配對男角時，會自動載入「配對男角」清單中選取的男角（未選取則用第一張），載入完成後兩人一起播放。
- 切換服裝狀態或服裝（同一張卡重新載入）時會保留配對男角（`KeepPartnerForReload` / `RestorePairAfterReload`）；原生雙人姿勢會在新角色上重新開始。影片雙人以男角的時鐘為準，每幀把女角的時鐘對齊男角；暫停時兩人一起暫停。換成別張卡時才結束雙人。
- `OK_UE_ANIM` 是整個程式共用的環境變數；任一角色重建中立姿勢都會把它設成 1。Rust 取樣時，金鑰開頭是 `video|` 的片段一律取樣，避免配對男角（遊戲 session）被凍結成 T-pose。

## 診斷

- `ok-extract x --dump-video-pose <card> <clip> <t> out.obj`：輸出指定時間的蒙皮身體網格。
- `ok-extract x --stretch-video-clip <card> <clip> [gameRoot]`：網格拉伸統計，用 `OK_STRETCH_TIMES=6.2,7` 指定時間點。
- `ok-extract x --probe-video-clip <card> <clip> [gameRoot]`：印出手腳的世界座標。
- UE 自動化測試 `OpenKoikatsu.VideoMotion.Playback`：遊戲卡與獨立卡的播放、回到原作片段，並以 `walk_01` 作為對照。
- UE 自動化測試 `OpenKoikatsu.VideoMotion.PairList`：確認 `.pair.json` 會出現在清單中。
- `Tools\lua\lua.exe Tools\video_motion\test_pair_ui.lua [--item 【雙人】名稱] [--game <KKS 目錄>]`：透過 CharacterMCP 啟動遊戲（畫面不顯示），載入女角後模擬點選【雙人】項目，再等男角自動載入、兩人的時鐘都前進，接著把衣服切成半脫，再確認男角仍在、兩人時鐘一致，每一步都輸出三視圖到 `Saved/Tests/VideoPairUI/<時間>/mailbox/images`。要確認男角不是 T-pose，必須看圖。
- `Tools\lua\lua.exe Tools\video_motion\test_import.lua --video <影片> [--static] [--pair] [--game <KKS 目錄>]`：在遊戲裡按「開始轉換」（`ok-video.exe`），等轉換完成後選取並確認播放。雙人時也會確認男角自動載入。共用程式在 `game_mcp.lua`。

## 已知限制

- GVHMR 一次只追蹤一個人；雙人影片需要另外處理。
- 手穿進軀幹只回報，不會自動推開；自身碰撞要另外處理，屬於 MeshRet / R2ET 類方法的範圍。
- 驗證用單一卡片（預設 C000）。身材比例極端的角色可以用 `--card` 指定再驗證。
