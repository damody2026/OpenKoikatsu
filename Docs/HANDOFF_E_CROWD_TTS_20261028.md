# E 線交接：百人房 120 FPS（GPU rig／GPU 物理）與 TTS／ok-song／ok-asr（2026-10-28）

寫給沒看過之前對話的新 session，讀完這份就能接手。工作目錄是 `E:\OpenKoikatsu`（分支 `duo2`，推到 `origin duo2`），引擎是 `E:\PhysX\UE5.8`，腳本用 `E:\OpenKoikatsu\Tools\lua\lua.exe`。共用規則見 `AGENTS.md`、`Docs/DUAL_LINE_DEVELOPMENT.md`。

本文件分兩條線：

- **A. CrowdRoom100 120 FPS**：使用者親自交代，優先度最高。
- **B. TTS／ok-song／ok-asr**：依使用者決定暫停，只記錄現況。

---

## A. CrowdRoom100 120 FPS（優先度最高）

### A1. 使用者的需求與決定（原話照錄）

最初的任務是協調者轉達使用者的需求（2026-10-27）：

> 1. 畫人改用 StreetSocial.cmd 現在畫街頭女主的方法（runtime pose／render mesh 那條路徑，找出 StreetSocial 實際用的元件與設定），不要再用舊的 GPU 群組行走片段。
> 2. 人不要一直繞圈走。改成每個人隨機走動、隨機發呆：選隨機目標點走過去，到了就停下發呆一段隨機時間，再選下一個點。發呆時隨機擺各種不同的站立 pose（從動畫目錄挑多種站姿／待機動作），而且停下發呆的機率要比走動高。
> 3. 目標是 100 人、實際量到 120 FPS（量 FPS 時拿掉 t.MaxFPS 上限，只開一個 Unreal 遊戲，用 /fps 讀數，不要用 startfpschart）。達不到時自己判斷做 LOD、更新頻率分級、遠處降頻等通用優化，量到的數字寫進文件。

使用者在這個 session 裡的原話，依時間順序：

| 原話 | 意思／後果 |
|---|---|
| 「每個角色的骨架動畫都一樣 沒辨法共用嗎？」「或是要用時再臨時載入」「主要是我想的到未來可能會有很多動作，肯定會讓預算不足，是不是能換個方法？」 | bank 是「人數 × 動作 × 長度」，動作一多 GPU 記憶體就爆，要換方法。 |
| 「你可以請其它agent都先等你測試，你的優先度最高」 | 這條線量測時，其他三條線要讓出 GPU。 |
| 「女主的身體都是參數化生成的，有沒有可能在gpu裡面透過參數即時生成骨架？」「我覺得你可以直接做這個優化了 現階段的就停下」 | → GPU rig（GPU 上用身形參數即時解骨架，所有人共用 clip）。 |
| 「扁平 先不要處理，因為扁平版還沒做好」「跳過 CPU 扁平版，直接寫 GPU compute shader，用完整解算當對照驗證。」 | 不做 CPU 扁平版。直接寫 compute shader，用完整解算（bank）逐列比對。 |
| 「接下來試著把 StreetSocial.cmd 也改成gpu RIG」 | 街頭也接上 GPU rig，目前預設關閉。 |
| 「把物理搬到 GPU 上模擬 用原始的verlet的方法」 | 物理用 DynamicBone 原始 verlet 搬上 GPU，**不走 PhysX**。協調者曾提議改從 bank 取物理，依「照使用者的決定做」，仍照使用者的做。 |
| 「你量啊」 | 自己量，不要只推估。 |
| 「請所有人先停下，幫我錄一個 CrowdRoom100.cmd 的demo 20秒 並且要在裡面用第一人稱視角，使用 bank的方法來錄」→「改用第三人稱視角 使用 Yvonne 當主角」 | demo：20 秒、第三人稱、bank 方法、Yvonne 當主角（見 A5）。 |
| 「我發現裡面的角色走路，腳不會走動」 | 已修（f5448ee1e）。 |
| 「我要你把所有沒穿衣服的人物都移除掉」「補上其它有穿衣服的人」 | 進行中（見 A5）。 |
| 「並且現在fps沒到120 你還是回來繼續補gpu 物理好了」 | 主線回到 GPU 物理，把 100 人拉到 120 FPS。 |

### A2. 架構

#### 房間與畫人

- **入口**：`CrowdRoom100.cmd` → `Tools/run_crowd_room100.lua`。
  - 寫 manifest（`selection.json`），用最新的 StreetSocial stage（或 `--editor`）開 StreetSocial 地圖，加上 `-CrowdRoom=<manifest>`；街頭的其他東西都不啟動。
- **行為**：`Source/OpenKoikatsu/OKCrowdRoom.cpp`。
  - 每人站 8～25 秒，之後 3 成走到 3～10 m 外的空位，7 成換一種站姿。前面有人就等，等 3 秒還被擋就換目的地。
  - 用 `OKSetCrowdRuntimeCharacterMotion`（外部控制）與 `OKSetCrowdRuntimeIdleVariant` 驅動。
  - `room.follow`：指定一位女角一直走，鏡頭從背後跟拍（demo 用）。
- **畫人**：`Source/OpenKoikatsu/OKCrowdRuntimeScene.cpp`。這是和街頭女主同一套 runtime（`OKStartCrowdRuntimeScene`）。
  - 每人載入流程：Stage 1 snapshot → 2 減面 → 3 render mesh → 5 安裝。
  - 動畫時鐘：`FOKRuntimeGpuPlayback`（`Plugins/KKCrowdGpuPacking/.../OKRuntimeGpuPlayback.cpp`）。
  - 每幀片段選擇：`Speed<.001` → 站姿；否則 → slot 1 走路（`walk_01.okanim`）。
- 房間預設 `aggregate_gpu:false`：每人一個「私有 GPU bank」，用 `LoadingRender` 與 `OKCrowdRuntimeProgressiveGpu.inl` 的 provider。
  - `--aggregate`：全員載完後合成一個總 bank。100 人時要 2×2.4 GB，超過 4 GiB 預算會當機，這是既有問題，見 ERROR_LOG。
  - `--gpu-rig` 會隱含 `--aggregate`。

#### 三種姿勢來源

| 模式 | 資料 | 程式 |
|---|---|---|
| **bank**（預設） | 每人每個 clip 用她自己的身形解好的骨骼矩陣（60 Hz），內含 CPU 解好的物理 | `Plugins/KKRuntimePose/.../KKRuntimePoseBank.cpp`（`OKPrepareRuntimePoseBankAsync`）→ `FOKResidentGpuPose`（`Plugins/KKCrowdGpuPacking/.../OKResidentGpuPose.cpp`，`Shaders/IndependentPose.usf`）→ palette（`OKGpuSkinPalette.cpp`，`SkinPalette.usf`） |
| **NativeOnly bank**（GPU rig 時） | 只解 native 骨（CPU 端的 socket、頭部看向用）；palette 別名骨固定第一幀；取樣用 `ok_session_sample_frame`，不跑動態 | `FKKRuntimePoseRequest::NativeOnly`；Rust `gpu_rig/export.rs` 的 `ok_session_sample_frame`；`KKRuntimeNativeSession.h` 的 `SampleFrame`；cache 版本 `NativeVersion=2` |
| **GPU rig**（`--gpu-rig`） | 所有人共用 clip（`OKGC`）。每人只留身形骨與「權重 × body 骨世界 × 常數 C」（`OKGR` v2，含物理段）。每幀在 GPU 解算，輸出和 bank 同格式的列 | Rust `Tools/ok-extract/src/gpu_rig/`（`export.rs`、`physics.rs`）；UE `Plugins/KKRuntimePose/.../KKRuntimeGpuRig.*`（解析）；`Plugins/KKCrowdGpuPacking/.../OKGpuRig.cpp/.h`（dispatch）；`Shaders/GpuRig.usf` |

#### GPU rig shader（`Plugins/KKCrowdGpuPacking/Shaders/GpuRig.usf`）

- **`SkeletonCS`**：每人一條執行緒，跑原生每幀管線的非物理部分，寫 `Worlds` 與 `RigRots`（每根骨的世界旋轉，物理要讀）。
- **`PhysicsCS`**：照 `Tools/ok-extract/src/dbone.rs`、`dbone/ver02.rs` 移植的 DynamicBone verlet。
  - 三種組：`kind` 2 是 body Ver02（胸、臀），1 是衣服，0 是飾品。dispatch 順序是 2 → 1 → 0。
  - 每種組分三個 stage，在 `OKGpuRig.cpp` 依序 dispatch：
    - stage 0（每組一條執行緒）：還原節點；第一次或重設時從當下姿勢起算並穩定；需要串行的組在這裡整組逐鏈 step，然後設 `PhysState[ready].y=1`。
    - stage 1（每條鏈一條執行緒）：不需要串行的組，各條鏈平行 step。
    - stage 2（每組一條執行緒）：從解好的 local 重建世界；kind 1、2 解好的 body 骨寫回 `Worlds`／`RigRots`，下面的骨跟著更新。
  - 是否串行由 Rust `physics.rs` 的 `serial()` 判斷：兩條鏈動到同一根骨，或一條鏈動到另一條鏈的祖先骨，就要串行。旗標放在 set 的 `kind | serial<<8`。每條鏈所屬的 set 寫在 `PhysChainValues[c*4+2].w`。
  - `StepChain` 處理 Ver01 與一般 DynamicBone，`StepChainV02` 處理胸、臀的 Ver02。
  - 節點有三種：body 骨別名（可帶 `local_from`，即 Ver02 參考骨）、固定 local 的私有骨、私有骨架頂層（「某根 body 骨 × 矩陣 + rot_offset」，用 4 個取樣時間搜尋求得）。
- **`TaskCS`**：每個 pose task 一條執行緒，寫出和 bank 相同格式的列（元件矩陣 × InverseBind）。之後的 palette pass 不變。

#### shader 保護與 patch

- 新的 global shader 要 cook 才進得了打包版。開發期用 `--editor`（E 引擎的 `UnrealEditor -game`，不 cook）。
- 打包版的 GPU rig shader 由 StreetDisplay patch 提供：`Tools/rebuild_street_social_patch.lua --output Saved/StreetSocial/Build/<名稱>.patch`，再把 `Content/IndependentResources/StreetSocialData/Launch/runtime.json` 的 `"patch"` 改成新的檔名。
- **每次改 `GpuRig.usf`（包括資料排版），都要重建 patch。**
- shader 只在命令列有 `-OKGpuRig` 時編譯與驗證（`ShouldCompilePermutation`：`WITH_EDITOR` 或有這個旗標）。不在快取裡時，`OKGpuRigAvailable()` 為 false，場景改用 bank，不會當機。

#### 街頭（StreetSocial.cmd）

- `Tools/run_street_social_development.lua` 有三個選項：`-GpuRig`、`-NoGpuRig`、`-GpuRigVerify N`。開啟時啟動器加 `-OKGpuRig`、manifest 寫 `gpu_rig:true`。
- 預設關閉（`prepare_street_social_selection.lua` 的 `gpu_rig=false`）。
- 串流動作（手機、喝飲料）照舊用她自己的完整 bank，以 overlay 蓋掉她的列。

### A3. 建置、啟動、比對、量 FPS

- **Editor 建置**：`E:\PhysX\UE5.8\Engine\Build\BatchFiles\Build.bat OpenKoikatsuEditor Win64 Development -project=E:\OpenKoikatsu\OpenKoikatsu.uproject -waitmutex -NoHotReloadFromIDE`。
  - 要從 PowerShell 跑；Git Bash 會吃掉路徑裡的反斜線。
  - PreBuildSteps 會順便建 Rust 的 ok_extract。
- **exe-only stage**：`Tools\lua\lua.exe Tools\rebuild_street_social_runtime.lua --tag <新名稱> --base-dir Saved/StreetSocial/Base1027 --skip-cook --skip-base`。
  - 目前的 base 是 `Base1027`，stage 是整合後的 `Int1027E`（舊的 Demo1027A/B、Base1026c 已刪）。
  - 打包前把改過的原始檔 touch 成現在時間。host 複製原始碼時保留舊的修改時間，否則 UBT 可能判斷不用重編。
  - 新的 global shader 不在 base 的 shader 快取裡，靠 patch 提供。
- **房間選項**（`Tools/run_crowd_room100.lua`，檔頭有完整說明）：
  - `--count N`、`--editor`。
  - `--benchmark <秒>`：全員畫出、暖機後量測，寫 `report.json` 與截圖，然後結束。
  - `--cases <json>`、`--trace`。
  - `--aggregate`、`--gpu-rig`。
  - `--gpu-rig-verify N`：bank 保持完整，前 N 幀兩邊都算，逐列比對。結果寫進 `report.json` 的 `gpu_rig`，另存 `rig.bin`（兩邊的列）與 `rig.txt`（列對應的角色與骨）。
  - `--follow <卡名片段>`：那位女角一直走，鏡頭從背後跟拍，不顯示畫面上的數字。
  - `--record <秒>`：量測一開始就用 `Tools\ffmpeg\ffmpeg.exe` 錄遊戲視窗（gdigrab `title=`、`h264_nvenc`），存成報告旁的 `demo.mp4`。這台的 ffmpeg 沒有 libx264。
- **比對**：用 `--editor --count 10 --gpu-rig-verify 20` 之類的設定。
  - 交叉淡入中的角色不列入比較：兩邊做交叉淡入的方式不同，都不是原生的做法。
  - GPU 物理是連續模擬，bank 是每段片段各自從頭解算，物理件本來就不會逐幀一致，看平均與 ≥10 cm 的比例。
- **量 FPS 的規則**：
  - 只開這一個 Unreal 遊戲，`t.MaxFPS 0`，用報告裡的平均／P95（與 `/fps` 同源）。
  - 不用 startfpschart，用完就刪掉 trace。
  - 1920×1080，100 人。
- **通知其他線暫停**：
  - 量 FPS 或錄影前，先傳「請暫停」給以下三個 session，結束後傳「錄影結束」或「測試結束」：
    - c-conquest `local_4da5ec84-3e25-42e1-866d-91d6ac944f4b`
    - c-street-city `local_4323a4c2-c093-492b-9ce0-8fc0e47cdab1`
    - e-galgame `local_08c7c1da-db89-48c7-b2dd-b2d4f25c9271`
  - **要改程式、重建等較長的空檔，先放行，要量的時候再請它們暫停**，不要讓它們空等。
  - 用 E 引擎建置或打包前，先告訴 e-galgame。
  - 只能關自己的 PID；自己的 Unreal 最多同時開兩個。
- **協調者**：`local_237f11ad-67cc-4a05-9cd6-a0cfd56cefca`。

### A4. 已完成的 commit 與數據

| commit | 內容 |
|---|---|
| `348d0d6a0` | 百人房改用街頭畫女主的方法，隨機走動、發呆換站姿 |
| `9f6ec2675` | 遠處不畫描邊殼（`ok.Crowd.OutlineRange`）、關深度預先 pass：平均 121.6 FPS（bank，打包版，P95 仍未到 8.33 ms） |
| `18060d313` | GPU rig（`--gpu-rig`）：共用 clip，每人只留身形參數與 terms |
| `a678f04a1` | 缺 shader 時改用 bank、不當機；街頭加 `-GpuRig` 等選項；女主 bank 只留 native 骨；rig 套用看向玩家 |
| `265ac5c48` | GPU 物理：DynamicBone verlet（頭髮、飾品鏈、OK 卡布料、裙子），patch StreetDisplay28Phys |
| `b588900c9` | GPU 物理：胸、臀 Ver02（StepChainV02），patch StreetDisplay29V02 |
| `f5448ee1e` | 走路時腳不動修好；`--follow`／`--record`；物理 stage 0/1/2（每條鏈各一條執行緒）；NativeOnly 不跑動態 |

整合後已合進 master（`1636a60d5`），整合 HEAD 是 `3b8b95d8`。

**FPS 數據**（1920×1080，100 人）：

| 模式 | 環境 | FPS | 幀 ms（P95） | GPU ms | 來源 |
|---|---|---:|---:|---:|---|
| bank，描邊 LOD＋關 EarlyZ | 打包版 | 121.6 | 8.23（8.89） | 5.04 | `Docs/CROWD_ROOM100.md` |
| bank | Editor `-game` | 135.5 | 7.38（7.98） | 4.94 | 同上 |
| GPU rig，無物理 | Editor `-game` | 161.3 | 6.20（6.64） | 5.48 | 同上 |
| GPU rig＋物理，物理還沒平行化 | Editor `-game` | 72.4 | 13.81（14.91） | 13.38 | `Saved/Crowd/Room100-20261027T221436` |
| GPU rig＋物理，stage 0/1/2 平行化後 | Editor `-game` | 80.4 | 12.44（13.75） | 12.03 | `Saved/Crowd/Room100-20261027T224945`（量時其他線不一定全停，要重量） |

**比對數據**（Editor，10 人）：

| 階段 | 平均位移差 | ≥10 cm 的列 |
|---|---:|---:|
| GPU rig，無物理 | 0.76 cm | 2.4% |
| 加物理 | 0.27 cm | 0.5% |
| 加 Ver02 | 0.15～0.20 cm | 0.3% |
| 平行化後 | 0.19～0.30 cm | 約 0.6% |

**結論**：

- GPU 物理讓 100 人的 GPU 時間從 5.5 ms 漲到約 12 ms，這就是沒到 120 FPS 的原因。
- 載入也很慢：Room100-20261027T224945 全員畫出要 561 秒。之後的 `SampleFrame` 改動是為了縮短載入，還沒用 100 人重量。

### A4b. 10-28 接手後已完成（四個平行 session，細節與數字在 `Docs/CROWD_ROOM100.md`）

下面 A5 列的項目都已完成並推到 `origin duo2`（7d3aef8dc..b949b401b）：

| 項目 | 結果 | commit |
|---|---|---|
| GPU 物理到 120 FPS | 物理與骨架改成每人一個 thread group。100 人 `--gpu-rig`：Editor 169.1 FPS，打包版 Phys1028A 177.5 FPS、Phys1028C 175.5 FPS（改前 70.8）。patch 換成 StreetDisplay30Phys | `66211df88`、`cae892bb9` |
| 沒穿衣服的卡 | 改成離線判斷（`ok-extract --oklib crowd-check`，結果快取在 `Saved/Crowd/card-check.tsv`），補上 C100～C113。可用 121／125 張 | `578577e60` |
| Yvonne 等 11 張 studio alpha 卡 | `xukmi/MainItemStudioAlphaPlus` 改走既有的 ForgeStudioAlpha，不用新材質，也不用 cook；眼白蓋住虹膜一併修好 | `b6a8d22be` |
| 錄影 | 用遊戲 PID 找視窗 handle，再用 ffmpeg `gfxcapture`，錄完自動檢查。最終 demo：`Saved/Crowd/Room100-20261028T032909/demo.mp4`（使用者改要求用 GPU 畫：`--gpu-rig`，跟拍 Yvonne，20 秒） | `b6a8d22be`、`f121fdd72` |
| 手與前臂的差 | 具名原生骨改用和 bank 一樣的 FTransform 組合方式，0.956 → 0.0001 cm。街頭 0.11 cm 的差是 bank 多算一格，已修 | `5a53c69a5`、`5cb155b0f` |
| 腳步滑動 | 改用逐幀步速曲線，先轉向再沿面向走，不再原地轉身。直走著地滑動 16 → 6 cm/s | `b6caeecfa`、`5cb155b0f` |
| `--aggregate` 4 GiB 當機 | 合成好之後，從離最遠的人開始退掉私有 bank，100 人畫滿 | `6937fc312` |
| 開卡 | 卡包可以同時開。單張冷開（含全新程序的第一張）都在 3 秒內 | `61f30722c`、`cba26afec`、`2147120f9` |
| 街頭 GPU rig | 在打包版驗證：外觀、物理件、手機／喝飲料 overlay、看向玩家都正常。預設仍關閉 | `1ef9b6345` |

最新的 exe-only stage 是 `Phys1028C`（base `Base1027`）。

### A5. 進行中、尚未完成（10-27 時的狀態，見上方 A4b）

1. **GPU 物理到 120 FPS（主線）**：
   - 先在其他線暫停的情況下，重量 100 人 GPU rig＋物理（`--editor --gpu-rig --benchmark 20`）。也用 `--trace` 看 `Crowd.GpuRigPhysics k.s` 各 pass 的 GPU 時間。
   - 可能的方向（還沒做）：
     - 遠處降低物理頻率或停用物理的 LOD：遠處改成剛性跟隨，或隔幀 step。
     - 減少 stage 0 串行組的長鏈。
     - 把每組一條執行緒的 stage 0/2 改成更細的平行。
     - 減少 dispatch 數：目前 3 種組 × 3 個 stage = 9 個 pass。
   - 改了 `GpuRig.usf` 就要重建 patch。
2. **沒穿衣服的卡**：
   - 已有的程式：`OKCrowdRuntimeScene.cpp` 在 Stage 1 記錄 `APNG <卡>: outfit 0 roles <部件角色>`；`run_crowd_room100.lua` 讀以前的 log，排除沒有 `clothes_top` 也沒有 `clothes_bot` 的卡。
   - 這兩處改動都**沒有提交**，但已編進整合 stage `Int1027E`，lua 的改動也在 E 的工作區。
   - 判定結果：C002、C024、C037、C079。資料來自 `Saved/Crowd/Room100-20261027T233943/game.log`。
   - **注意：拿掉這 4 張後只剩 96 張可用卡**（CrowdAPNG 101 張＋StreetHeroines 10 張，扣掉 11 張材質不支援的），`CrowdRoom100.cmd` 會報「only 96 usable cards, 100 asked for」。這是現在 E 工作區的狀態，接手後要先處理。
   - 補滿的方式：從 `D:\KoikatuSunshine\UserData\chara\female` 再轉幾張有穿衣服的卡。CrowdAPNG 的來源對照在 `Content/CharacterCards/CrowdAPNG/sources.tsv`；轉檔方式見 `Docs/CROWD_APNG.md` 與 `AGENTS.md` 的 `ok-extract --oklib card`。卡片只留本機，不要提交。
   - 卡片封面圖不是遊戲內第 0 套服裝的樣子，不能拿來判斷有沒有穿衣服。
3. **demo 影片**：
   - 錄了 `E:\OpenKoikatsu\Saved\Crowd\Room100-20261027T233237\demo.mp4`（Demo1027B、100 人、跟拍 C000、20 秒），但整段畫面靜止，不能用。
   - 當時另有 C 的遊戲（標題同樣是「OpenKoikatsu (64-bit Development PCD3D_SM6) 」）和街道城市的 Editor 在跑。gdigrab 用標題抓視窗，可能抓錯視窗，或是視窗被蓋住時拿到舊畫面。
   - 要改錄影方式：用 PID 找視窗、讓遊戲自己存影格，或錄前確認只有自己的遊戲視窗。
   - 錄的時候先請其他線暫停。
   - 使用者要求：20 秒、第三人稱、bank 方法、Yvonne 當主角。
4. **Yvonne（C049/C050）要等使用者決定**：
   - 她髮飾上一塊半透明的假陰影用 `xukmi/MainItemStudioAlphaPlus`（`_alpha 0.6`、1×1 白貼圖、render_queue 2500）。`OKRuntimeSharedMaterialRecipe` 不支援這個 shader，整張卡因 `Source material coverage incomplete` 被排除。
   - 選項：補這個材質、把那塊陰影藏起來、換主角。已問過使用者，還沒回答，不要再問，回報時提一下。
5. **其他**：
   - GPU rig 預設開啟前，物理件與遠處 LOD 都要先驗過。街頭的 GPU rig 仍預設關閉。
   - 手與前臂另有 0.4～1 cm 的差，還沒查。

### A6. 踩過的坑（細節在 `Docs/ERROR_LOG.md`）

- **新增或修改 global shader 後沒重建 patch**：exe-only stage 一啟動就在 `VerifyGlobalShaders` 當機（galgame 的 Gal1027Q 受影響）。
  - 修正：加 `-OKGpuRig` 保護與 bank 後備，且每次改 `GpuRig.usf` 都重建 patch。
- **C4456 變數遮蔽**（`Flags`／`A`）：E 的打包是警告當錯誤，曾害 galgame 的打包失敗。內層變數要換名字。
- **除錯時讓其他三條線空等 30～40 分鐘**：協調者要求，長時間改程式、重建前先送「錄影結束」放行。
- **gdigrab 錄到靜止畫面**：見 A5 第 3 項。錄完一定要抽幾格確認畫面有在動。
- **走路時腳不動**：私有 GPU bank 在播放時間（取整到 1/60 秒）沒變時重用上一幀，畫面就停在同一個姿勢。
  - 修正：`OKResidentGpuPose::Evaluate` 每幀重算。
  - 查法：先用 `--aggregate` 對照，分出是 bank 資料還是播放路徑的問題；再把 GPU 元件藏起來，確認畫面上看到的是哪個元件。
- **bank 的列格式**：別名骨只留一欄、乘 3、乘 InverseBind。GPU rig 的常數要把這些併進 C；手與手指的分解要用結構對應，不能用取樣搜尋。
- **palette 槽位缺失**：每次取樣後要 `finish_paired_dynamics(0)`。
- **Lua 長字串裡有 `]]`**：用 `[==[ ]==]`。
- **ffmpeg 沒有 libx264**：用 `h264_nvenc`。
- **`--aggregate` 加 100 人的 bank 超過 4 GiB 預算**：會當機，是既有問題，還沒修。

---

## B. TTS／ok-song／ok-asr（暫停中）

### B1. 狀態

- **優化清單暫停**：2026-10-25 使用者決定 `Docs/TTS_OPTIMIZATION_PLAN_20261022.md` 剩下的優化都不做，重點放在遊戲沒 bug、效能夠用。計畫裡沒勾的項目都屬於暫停。
- **已完成並部署**：
  - 固定聲線：Base x-vector 複製聲線、卡片聲線、「换声音…」。
  - talker int8（`3477808b6`），talker 每幀一個 CUDA graph（`cfc4105d5`）。
  - 取消與快取，串流起播 0.3 秒。
  - 預設 1.7B，可選 0.6B；沒有模型時第一次啟動自動下載到 `Build/ok-tts/models/`（`a70e3ded3`、`57f1b77bd`）。
  - 多個遊戲同時跑時，可手動設 `OK_TTS_GPU_PRIORITY=5`。
- **唱歌**（設計見 `Docs/HEROINE_SINGING_IMPLEMENTATION_PLAN.md`，驗證見 `Docs/HEROINE_SINGING_VALIDATION.md`）：
  - 歌是完整 1 分半；觸發條件是成為戀人、戀人好感 100／150、之後每 500 句（`23322e947`）。
  - 做歌時讓出 GPU 給語音（voicelease）。
  - 顯存：HeartMuLa int8、HeartCodec fp16（`3b7c83e95`）。
- **字幕對時的修正**（`9211746a3`，已在遊戲裡驗證）：
  - 舊歌重播時不再先清空已辨識的結果（原本字幕整體慢 2～4 秒）。
  - `align_heard` 一口氣最多對 6 句（原本 3 句，整段主歌一口氣唱完會漏一句）。
  - `/transcribe_spans` 等 600 秒，晚到的結果會在唱歌中重新對時。
  - SongE13 沙盒實測：20 句全部出現，誤差約 0.2～0.5 秒。
- **只哼不唱的長段**（`d33ceb6be`）：字幕換成 ♪，不再停在上一句。
  - 在 Gal1027G 沙盒用安可 confession 做了一首新歌：做歌、辨識、走過來唱、字幕、再聽一次、站位都正常。
- **還沒驗證**：
  - 協調者的 `Docs/AGENTS_HANDOFF_20261028.md` 寫「從頭做一首新歌再唱還沒在遊戲裡跑過」，但上面 `d33ceb6be` 的實測已經跑過一次（一首）。其他女主、其他曲風還沒測。
  - 英文用中文參考聲紋說，沒有另外驗證；日文不支援。
  - 十位女主的聲線待使用者逐一確認。

### B2. 部署到 `Build/<x>/`

`Build/` 不進版控（`.gitignore`），遊戲啟動時從這裡啟動伺服器（`run_street_social_development.lua`）：

| 伺服器 | 內容 | 埠 |
|---|---|---|
| ok-tts | `Build/ok-tts/ok-tts.exe`、`voices.json`、`profiles/`、`cuda/`、`ok-tts-serve.cmd`、`package.json`（各檔 hash） | 47860（E 用自己的 local-instance 設定） |
| ok-asr | `Build/ok-asr/ok-asr.exe`、`cuda/`、`ok-asr-serve.cmd` | — |
| ok-song | `Build/ok-song/ok-song.exe`、`cuda/`、`ok-song.json` | 遊戲 base 埠 +20 |

- 伺服器帶 `--parent-pid` 跟著遊戲啟動與結束；沙盒用自己的連接埠。

步驟：

1. `cargo build --release --manifest-path Tools/<x>/Cargo.toml`。
2. 確認沒有遊戲正在用這個 `Build/<x>`：伺服器跟著遊戲進程，只能關自己的 PID。
3. 舊的 exe 改名備份（例如 `ok-tts.before-<說明>.exe`），再把 `Tools/<x>/target/release/<x>.exe` 複製進去。ok-tts 改了 `voices.json` 時要一起複製。
4. 更新 `package.json` 的 hash。
5. 下次啟動遊戲時才會換成新版。

ok-song 的權重轉換：`Build/ok-song/ok-song.exe prepare-models --seedvc E:/OkSong/seed-vc/checkpoints --out E:/OkSong/models-rs`，之後用 `doctor` 檢查。

10-28 整合時三個伺服器都重編過，和 `Build/<x>/` 裡的完全相同，沒有替換。

---

## C. OK 卡轉換與開卡速度（e-okcard，2026-10-29 暫停時的狀態）

使用者的目標：新版 OK 卡只靠卡內 `okLc`／`okLt` 與基礎庫開卡；用 zipmod 資料夾轉卡，每套服裝完整。速度目標：
- 轉卡：100 張合計 ≤ 100 秒（可以並行），每張 ≤ 10 秒，不含初始化。
- 冷開：每張 ≤ 3 秒。

方法與數字細節見 `Docs/OK_BASE_LIBRARY_PLAN.md`「批次轉換」、`Docs/OK_EXTRACT_REFACTOR_PLAN.md`「進度」、`Docs/ERROR_LOG.md`。

### C1. 已完成（都已提交；`b5ad7c31d` 之後的未推送）

| commit | 內容 |
|---|---|
| `3581190b2`、`b9a4cdfdd`、`6ffcdcad9`、`1f09d79ba` | `view_memo`：同一張卡的多次開卡共用基礎 bundle、清單、頭部細節、身體／臉的 rig（重構 2c、2d）；轉卡時 bundle 快取上限 160 |
| `e0736beda` | 貼圖上下翻轉以內容快取 |
| `0f7fd1add` | `view_memo` 只在開卡的 thread 上記住（修正 helper thread 換卡沿用記憶的隱患） |
| `315aff4cf` | **C032 根因**：外部 CAB 索引在預取 thread 上重建，讀遍 8,427 個 bundle 檔頭全算成缺口；改在 `okstrict::scanning` 裡跑，基礎庫模式不讀寫 `base_v1\.ok_serialized_index_v1.bin` |
| `75725edac`、`b5ad7c31d` | 卡片解析、`entry_sql_key`、身體貼圖合成的記憶 |
| `9d47c1ef2` | 安裝 OK 卡時保留被取代卡片的聲音（`Tools/ok_card_voice.lua`）；街頭女主十張的聲音已寫回 |

驗收方法（每一步都跑）：
- 6 張樣本卡轉換的摘要與 `pack-keys` 記錄和基準相同（`validate6.sh` 的做法）。
- C018、march_7th、jeanne 共 126 個 session，有記憶與 `OK_NO_VIEW_MEMO=1` 逐位元組相同。
- 21 個 session 和今天優化前的版本 `63173020b` 逐位元組相同。

### C2. 數字

- 100 張 CrowdAPNG、12 個工作程序、雙 slot：今天優化前 857 秒，`0f7fd1add` 507 秒。之後的 `315aff4cf`～`b5ad7c31d` 還沒有重量（排隊時被暫停）。
- 單張（機器空閒）：jeanne 24.8 → 約 10 秒；C001 約 16 秒。
- 批次受 CPU 限制：單張平均只用約 1.6 個核心，CrowdAPNG 每張 CPU 約 25 秒（機器空閒時）；12 個程序同時跑會把機器吃滿。
- 冷開：10-28 量過 30 張，平均 1.63 秒，最慢 3.13 秒（march_7th、C018）。之後沒有重量。

### C3. 下一步

1. 拿雙 slot 用目前版本重量 100 張批次（`ok_cards.lua convert <清單> <輸出> --workers 12`），和 507／857 秒比較。量測腳本只看 slot 與佇列，停掉後確認程序真的結束（見 ERROR_LOG 10-29 的兩條）。
2. 每張卡的 CPU 還要再降一半以上，100 張才可能 ≤ 100 秒。剩下的大多是每個項目第一次出現的工作：貼圖解碼／PNG 編碼、配件第一次上色、`outfit 0` 的初始化；換穿著狀態的重開已經只剩 60–200 ms。另外每個程序第一次開卡會掃整棵 mods 樹（GUID、ME、檔案字尾），約 45 CPU 秒。
3. 冷開 ≤ 3 秒：march_7th、C018 約 3.1 秒。遊戲裡的材質準備（`PrepareSharedRuntimeTexture`：PNG 解碼、mip、SHA1，約 1.6 秒）是 e-galgame 量到最大的一段，屬於共用的 KKUnityBundle／KKRuntimePose，要先和 e-galgame 協調。
4. 重構還剩：2d 之後的衣服／配件階段、5g（`ParsedBundle` 的 impl 分檔）。
5. 未完成的「CrowdRoom100 預設改用 GPU rig」（`CrowdRoom100.cmd`、`Tools/run_crowd_room100.lua`、`Docs/CROWD_ROOM100.md`）還沒做 FPS 實測，存成 patch：`E:\OpenKoikatsu\Saved\Crowd\e-okcard-gpu-rig-default-20261029.patch`（`git apply` 可以套回）；工作樹已還原成 HEAD。

開發方式：在匯出副本 `E:\OpenKoikatsu\Saved\okx-okcard`（`git archive HEAD Tools/ok-extract Tools/vendor`，加上編譯期 include 的檔案，`BASE_COMMIT` 記基準）搭配獨立的 `CARGO_TARGET_DIR` 開發，驗證後再寫回共用樹。改到開卡路徑的寫回前，先通知 e-galgame。暫停時匯出副本與 HEAD 相同，沒有未寫回的改動。

---

## 接手順序建議

1. 處理 A5 第 2 項的 96 張問題，讓 `CrowdRoom100.cmd` 能啟動：轉新卡補滿。
2. 請其他線暫停，重量 100 人 GPU rig＋物理（加 `--trace`），放行後做物理 LOD／降頻，直到 100 人 120 FPS。每次改 shader 都重建 patch。
3. 改好錄影方式，錄 20 秒第三人稱 demo（主角等使用者對 Yvonne 的決定）。
4. 每做完一段：更新 `Docs/CROWD_ROOM100.md`，錯誤寫進 `Docs/ERROR_LOG.md`，只提交自己的 hunk（用暫時的 index），推到 `origin duo2`。
