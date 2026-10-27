# 百人房間（CrowdRoom100.cmd，2026-10-27，E 線）

`CrowdRoom100.cmd` → `Tools/run_crowd_room100.lua`：用最新的 StreetSocial stage 開 StreetSocial 地圖並加上 `-CrowdRoom=<manifest>`，街頭的其他東西都不啟動。角色和街頭女主是同一套 runtime（`OKStartCrowdRuntimeScene`，外部控制）：同樣的共用材質、GPU 姿勢、陰影與畫面設定（TAA、CSM、關遮擋查詢、關 CAS）。舊的 CrowdRoom 收據管線在 10-20 重開倉庫之後已經不存在（`Docs/CROWD_ROOM.md` 是當時的紀錄）。

## 行為（`Source/OpenKoikatsu/OKCrowdRoom.cpp`）

- 房間：高空 48 m，地板與四面矮牆用引擎 Cube。
- 每人站 8～25 秒。之後 3 成機率走到附近 3～10 m 的空位，7 成換一種站姿繼續站；同時在走的人約 1 成。走路速度與轉身限制同街頭（110 cm/s、145°/s）。前面有人就等，等 3 秒還被擋就換目的地。
- 站姿：manifest `idle_variants` 是站姿池（Studio 立ち 2～30、待機 1～3、看窗外／天空／瀑布、對話等待姿勢，都不拿道具）。每人依卡片雜湊挑 3 種，烘進自己的動作庫；`OKSetCrowdRuntimeIdleVariant` 切換。基本站姿用 1.33 s 的立ち1。
- 鏡頭：WASD、Q/E、右鍵轉向、Shift 加速。

## 卡片：來源與排除規則（2026-10-28）

- 來源：先 `Content/CharacterCards/CrowdAPNG`（`C000.png`…，依檔名），再 `Content/CharacterCards/StreetHeroines`；取前 `--count` 張可用的。`CrowdAPNG/sources.tsv` 記每張 OK 卡的原卡（`D:/KoikatuSunshine/UserData/chara/female/…`）。卡片只留本機，不進版控。
- 排除（`run_crowd_room100.lua` 的 `check_cards`，啟動前離線判斷，不讀以前的遊戲 log）：`ok-extract <lib> --oklib crowd-check <lib> <卡>...`（`Tools/ok-extract/src/oklib/crowd_check.rs`）照 runtime 的方式開第 0 套服裝，看畫出來的部件：
  - 沒有 `clothes_top` 也沒有 `clothes_bot` 的部件 → 沒穿衣服，排除。卡片封面不是第 0 套服裝，不能拿來判斷。
  - 有部件的材質對不到共用 runtime 材質（遊戲裡會報 `Source material coverage incomplete`）→ 排除。規則照抄 `OKRuntimeSharedMaterialRecipe`（`KKRuntimeSharedMaterial.cpp`），**那邊新增材質家族時這裡也要改**。
  - 結果依檔案大小與修改時間存在 `Saved/Crowd/card-check.tsv`（可刪，會重算）。依序檢查到湊滿所需張數為止，每張約 3～6 秒（125 張全查一次 12 分鐘）；之後只開新卡或改過的卡，沒變動時 0.03 秒。`--no-card-check`：不開卡，只用快取（沒有判定的卡當可用）。`--check-cards` 只做檢查、印出可用張數。
- 驗證（10-28）：家族比對前的版本對原本 111 張卡的判定，和 `Room100-20261027T233943` 等舊 log 完全一致：沒穿 4 張（C002、C024、C037、C079），材質不支援 11 張（C049～C053、C055、C062～C064、C066、C068，都是 `xukmi/MainItemStudioAlphaPlus`）；96 張可對照的卡，部件角色和 Unreal 的 `APNG …: outfit 0 roles …` 逐字相同。
- 補卡（10-28）：從 `D:\KoikatuSunshine\UserData\chara\female` 挑 18 張沒用過的卡，`ok-extract D:/KoikatuSunshine --oklib cards <base_v1> <清單> <輸出>` 轉 OK 卡，再 `--oklib okcard-check <base_v1> <輸出> <清單> live` 驗收。15 張 MATCH（时崎狂三、一之濑千鹤、雾切响子有 `gaps`，不用）；其中重绘佩拉的 `Koikano/main_clothes_emblem` 材質 runtime 不支援，剩 14 張裝成 `C100`～`C113`：

  | 卡 | 原卡 |
  |---|---|
  | C100 | 重绘八重神子.png |
  | C101 | 重绘申鹤.png |
  | C102 | 重绘符玄1.1.png |
  | C103 | 重绘阮梅.png |
  | C104 | 重绘流萤.png |
  | C105 | 重绘灵砂.png |
  | C106 | 重绘星见雅.png |
  | C107 | 重绘克拉拉.png |
  | C108 | 重绘五河琴里B.png |
  | C109 | 冰菓 - 千反田爱瑠.png |
  | C110 | 鸣潮 泱泱.png |
  | C111 | 椿.png |
  | C112 | 折枝.png |
  | C113 | kks长离abmx修复.png |

  可用卡：125 張中 121 張（只排除沒穿的 C002、C024、C037、C079）。這是錄影 session 讓 `xukmi/…Plus` 依 Shader Forge 家族畫出（`KKRuntimeSharedMaterial.cpp` 的 `ShaderFamily`，crowd_check 同規則）之後的規則；遊戲執行檔沒編進那個改動時，那 11 張在遊戲裡仍會報 coverage incomplete，可用的是 110 張。

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

## 街頭（StreetSocial.cmd）的 GPU rig（2026-10-27，預設關閉）

- **啟動器選項**（`Tools/run_street_social_development.lua`）：
  - `-GpuRig` 開啟；`-NoGpuRig` 關閉，用原本的 bank。
  - `-GpuRigVerify N` 同時解算完整 bank，前 N 幀逐列對照。
  - 開啟時啟動器會加上 `-OKGpuRig`，並在 manifest 寫 `gpu_rig:true`。
- **bank**：開啟時女主的 bank 改成 `NativeOnly`。native 骨照完整長度解算；palette 別名骨固定第一幀，會被合併成 1 個取樣。所以 CPU 端讀骨頭的功能照常可用：手上道具、相機、galgame 的頭部位置、看向玩家。
- **看向玩家**：GPU rig 會在最後把脖子和頭的轉動乘到 local 上，和 bank 的 `LookRotation` 相同。
- **串流動作**（手機、喝飲料等）：照舊用該角色自己的完整 bank，以 overlay 蓋掉她的列。
- **shader 與保護**：
  - GPU rig 的 shader 由 patch 的 `RuntimeGlobalShaderPatch` 提供，目前是 `StreetDisplay27Rig.patch`。
  - 只有帶 `-OKGpuRig` 的遊戲，啟動時才會驗證這兩個 shader。
  - shader 不在快取裡時，場景記一筆警告並改用原本的 bank。已在 Rig1027A 上掛舊 patch 測過，可以正常啟動。

### 為什麼還不能預設開啟：物理

Editor 版街頭驗證（11 人、30 幀，`Saved/StreetSocial/Play-20261027T200302`）：74.0% 的列誤差小於 0.01 cm，5.4% 的列誤差大於 10 cm。用三月七的卡在 Rust 端照遊戲流程逐片段檢查，誤差都落在有物理的部件上：

- 頭髮：跟著頭剛性移動，偏差 0.15～0.57 m。
- OK 卡的上衣與裙子（`top_*`／`bot*`，布料物理）：1.2～2.5 m（這個量法會把縮放放大，但畫面上也看得出來）。

打包版截圖也看得到：弓弦、緞帶在 bank 裡是往下垂的；在 GPU rig 裡停在原始的水平形狀，往兩側伸出去。

body 與非物理部件和完整解算一致。街頭鏡頭很近，要預設開啟，必須先處理物理件：
- 一種做法是物理件仍由 bank 提供（bank 只保留 native 骨加物理骨）。
- 另一種是把彈簧骨與布料搬上 GPU。

## GPU 物理（DynamicBone verlet，2026-10-27）

使用者決定把物理搬到 GPU，用原始的 verlet（DynamicBone），不走 PhysX。

- **匯出**（`Tools/ok-extract/src/gpu_rig/physics.rs`，OKGR v2 的物理段）：
  - 每組物理（衣服一組、每個飾品各一組）的鏈、粒子（含 curve 套用後的阻尼、彈性、剛性、慣性、半徑）、碰撞體（body 的、植入的）。
  - 每組物理用到的骨頭都成為「節點」，有三種：
    - body 骨的別名（衣服鏈）。
    - 固定 local 的私有骨。
    - 私有骨架頂層：以「某根 body 骨 × 固定矩陣」掛上，用取樣分解求得；attachment 鏈不算在內。
  - 飾品的 palette 第 j 格就是該飾品骨架的第 j 根骨（節點）；OK 卡衣服的植入骨按 `rig::key` 名稱對到衣服組的節點。
  - 三月七：87 條鏈、929 個粒子、1101 個節點。靜態節點重建誤差 0，palette 全部精確分解（`gpu_rig::physics_export`）。
- **GPU**（`GpuRig.usf` 的 `PhysicsCS`）：照 `dbone.rs` 逐段移植。
  - 演算法：UpdateParticles1/2、SkipUpdate、ApplyParticlesToTransforms、膠囊碰撞（含 inside）、凍結軸、鏡像軸、Ver01。
  - 每組一個 thread。衣服組先跑，解好的 body 骨寫回 `Worlds`，再更新其下的 body 骨；飾品組後跑。
  - 粒子的 pos/prev、每鏈時間與上一幀元件位置，都存在持續的 GPU 緩衝。重設時從當下姿勢起算，先穩定 30 步。
- **Editor 比對**（10 位女角，30 幀，`Saved/Crowd/Room100-20261027T213320`）：
  - 平均位移差 0.76 cm → 0.27 cm。
  - 誤差 ≥ 10 cm 的列 2.4% → 0.5%。
  - 物理件大多只差幾公分，最大 24 cm。GPU 是連續模擬，bank 是每段片段各自從頭解算，物理對初始狀態敏感，兩邊本來不會逐幀一致。
- **畫面**：街頭近景（Editor `-GpuRig`）中，三月七的弓弦、緞帶自然往下垂（原本是水平伸出）。
- **尚未完成**：
  - 胸部的 Ver02（`dbone/ver02.rs`）仍是剛性，差 2～20 cm。
  - 遠處降低物理頻率的 LOD。
  - 100 人時物理 pass 的 GPU 成本還沒量。

### 胸部 Ver02 也在 GPU 上（2026-10-27）

- `dbone/ver02.rs` 的 body 鏈（左右胸、左右臀）匯出成第三種物理組（kind 2）。
- 粒子是 body 骨的別名節點，每步從參考骨的動畫 local 起算。
- GPU 的 `StepChainV02` 照 `Chain::prepare`／`finish` 移植：reflect speed、keep 範圍、move limit 方塊、crush 縮放、`next_length` 轉向。
- pass 順序：body（Ver02）→ 衣服 → 飾品，前兩者解好的 body 骨都寫回 `Worlds`。
- 10 人比對（`Saved/Crowd/Room100-20261027T215705`）：
  - 平均位移差 0.15～0.20 cm。
  - 誤差 ≥ 10 cm 的列 0.3%。
  - 胸部骨 bank 與 rig 只差約 0.01 cm。
- 每次改 GPU rig 的 shader（包括資料排版），都要重建 patch；目前掛 `StreetDisplay29V02.patch`。
