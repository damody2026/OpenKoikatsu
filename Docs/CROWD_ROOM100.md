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

  100 人驗證（10-28，打包版 `Int1027E.stage`，`--benchmark 20`，`Saved/Crowd/Room100-20261028T025627`）：啟動前排除的只有沒穿的 4 張；前 100 張可用卡包含 C100～C103，這 4 張都正常畫出。結果 `drawn 89／failed 11`，失敗的正好是上面那 11 張 studio alpha 卡（`Int1027E` 編在 `ShaderFamily` 之前），沒有新的排除；畫出的 89 人 128.9 FPS（p95 8.55 ms）。執行檔換成含 `ShaderFamily` 的建置後，這 11 張才會畫出、湊滿 100 人。Editor 版這次沒跑成：工作樹的 `GpuRig.usf` 正在改，`FOKGpuRigSkeletonCS` 編不過，Editor 卡在 shader 錯誤。

  Editor 版（10-28，含 `ShaderFamily` 的 b6a8d22be 之後重建，`--editor --benchmark 20`，`Saved/Crowd/Room100-20261028T031533`）：**drawn 100／failed 0**，130.6 秒全員畫出，119.6 FPS（p95 9.04 ms）。啟動前排除的仍只有沒穿的 4 張。

## 量測方式

- `--benchmark <秒>`：全員畫出並暖機後量測，寫 `report.json`（平均／P95／P99、game／render／GPU ms、draw 數、三角形數）與截圖，然後結束。數字與街頭 `/fps` 同源（`GAverageFPS` 之外另記每幀時間）。
- `--cases <json>`：同一個遊戲裡依序套用主控台指令、各自暖機後量測（A/B 不用重載 100 人）。`--trace`：第一段量測錄 Insights trace。
- 量測時只開這一個 Unreal 遊戲，`t.MaxFPS 0`。分析：`UnrealInsights.exe -OpenTraceFile=… -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimingEvents <csv> -threads=RenderThread* -columns=ThreadName,TimerName,StartTime,EndTime,Depth"`，再用 `Tools/summarize_crowd_events.lua`（PowerShell 呼叫；從 bash 傳引號會被吃掉）。

## Demo 錄影（`--follow`／`--record`，2026-10-28）

```powershell
.\CrowdRoom100.cmd --follow C000 --record 20            # 打包版（最新 stage），bank 方法
.\CrowdRoom100.cmd --editor --follow C000 --record 20   # 改了 C++ 還沒打包時
.\Tools\lua\lua.exe Tools\run_crowd_room100.lua --check-video <mp4> --record 20   # 只檢查一段現成的影片
```

- `--follow <卡名片段>`：那位女角一直走，鏡頭從背後跟拍，不顯示畫面上的數字。`--record <秒>`：量測一開始（全員畫出、暖機後）就錄，存成報告旁的 `demo.mp4`；量測長度自動設成「錄影秒數 + 6」，錄完遊戲自己結束。
- **抓的是這個遊戲自己的視窗**：啟動器用遊戲的 PID 查它的主視窗 handle（`Get-Process -Id <pid>` 的 `MainWindowHandle`），交給 ffmpeg 的 `gfxcapture=hwnd=<handle>`（Windows.Graphics.Capture）。不用視窗標題：這台上所有 Unreal 遊戲的標題都一樣。Windows.Graphics.Capture 拿的是視窗自己的畫面，被別的視窗蓋住也照錄，只有最小化時沒有畫面。
- 編碼：`hwdownload,format=bgra,fps=60` → `h264_nvenc`（`-preset p5 -cq 19`，yuv420p）。這台的 ffmpeg 沒有 libx264；`gdigrab` 也沒有 `hwnd=` 選項。
- **錄完自動檢查畫面有在動**，不通過就報錯：
  - 長度不短於要求 1 秒以上、每秒至少 50 格（ffprobe）。
  - `freezedetect=n=-60dB:d=1`：不能有 1 秒以上的靜止段。
  - `signalstats` 的 YDIF（相鄰兩格的平均亮度差，第一格不算）平均要大於 0.2。
  - 第 1 秒、1/4、1/2、3/4、最後 1 秒各抽一格到 `demo-frames/`，數字寫在 `video-check.json`。
  - 拿 10-27 那段靜止的 `Room100-20261027T233237/demo.mp4` 測：判定「0～20 秒靜止」、只有 786 格，報錯。
- 錄影時只開這一個 Unreal 遊戲。
- **實測（10-28）**：打包版 Int1027E、20 人、跟拍 C006、bank：`Room100-20261028T004834/demo.mp4`，20.0 秒、1200 格、平均 YDIF 0.90、沒有靜止段，抽格看得到她在走、鏡頭在跟。那時畫面右上角還有 `stat fps` 的數字（PlayerController 開的），已改成跟拍時關掉。
- **鏡頭角度**（CVar，`--exec` 傳入）：`ok.Crowd.FollowYaw`（0 背後、180 正面）、`ok.Crowd.FollowDistance`（cm，預設 320）、`ok.Crowd.FollowHeight`（鏡頭離腳底，預設 190）、`ok.Crowd.FollowLook`（看向的高度，預設 105）。正面近拍臉：`--exec "ok.Crowd.FollowYaw 180,ok.Crowd.FollowDistance 140,ok.Crowd.FollowHeight 125,ok.Crowd.FollowLook 118"`。
- **最終 demo（10-28）**：使用者決定用 GPU 畫（原話「你等 "百人房：GPU 物理拉到 120 FPS" 做好直接用gpu畫」）。打包版 Phys1028A（patch StreetDisplay30Phys），`CrowdRoom100.cmd --gpu-rig --follow C049 --record 20`，100 人（121 張可用卡的前 100 張）、第三人稱跟拍 Yvonne：`Saved/Crowd/Room100-20261028T032909/demo.mp4`。自動檢查：20.0 秒、1200 格、平均 YDIF 1.97、沒有靜止段；100 人全部畫出（72.7 秒）、沒有失敗；錄影期間 144.8 FPS（幀 6.91 ms、P95 7.98 ms、GPU 4.51 ms；錄影本身佔一些，不當量測數字）。抽格看過：她一路在走、鏡頭在跟、腳左右交替沒有明顯打滑、沒有畫面數字。

## xukmi「…Plus」材質與眼白排序（2026-10-28，救回 11 張卡）

- CrowdAPNG 有 11 張卡（C049 C050 C051 C052 C053 C055 C062 C063 C064 C066 C068，Yvonne 是 C049／C050）以前被排除，原因都一樣：髮飾的陰影層用 `xukmi/MainItemStudioAlphaPlus`（`_alpha` 0.4～0.65、queue 2500），UE 的 `OKRuntimeSharedMaterialRecipe` 只認 `Shader Forge/main_item_studio_alpha` 這個字串。
- xukmi 的 KKShadersPlus 是用「CamelCase＋Plus」的名字取代 Shader Forge 的 shader，blend、`_alpha` 與屬性都照原本那個。Rust 早就用 `shader_family` 這樣歸類（kind 4、translucent），UE 的父材質 `M_ForgeStudioAlpha` 也已在 StreetDisplay26。所以只補 UE 的判斷：`KKRuntimeSharedMaterial.cpp` 的 `ShaderFamily()` 用同一規則，kind 4 比對家族。沒有新 shader、沒有新材質資產，**不用 cook、不用重建 patch**；打包版要畫出這 11 張只需要新的 exe。
- `_alpha` 原本會乘兩次：部件貼圖已經乘過 `_alpha`（Rust `scale_studio_item_alpha`／`studio_alpha_texture`），原作翻譯的 ForgeStudioAlpha 程式又乘一次。binding 改成對這個 recipe 綁 `_alpha=1`，照原作是「貼圖 alpha × `_alpha`」一次。Yvonne 的髮影貼圖是 25×25 黑色、alpha 96（0.6 已乘入），畫出來是瀏海下淡淡的暗影。
- 救回後發現 C049／C051 的眼睛全白：卡片把虹膜的 queue 改成 2451（眼白 2901）。遊戲裡虹膜寫深度，後畫的眼白被擋住；runtime 的半透明不寫深度，眼白蓋掉虹膜。`OKCrowdRuntimeScene.cpp` 的 Commit 排序改成：眼球白（`cf_Ohitomi_L/R`）不晚於最早的虹膜減 1；一般卡（虹膜在眼白之後）不受影響，虹膜和 2500 髮影的原作順序保留。
- 離線卡片檢查（`oklib/crowd_check.rs`）的 kind 4 同步改成比對家族；11 張都判定為有穿衣服、材質都支援。

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
- 手與前臂另有 0.4～1 cm 的差：已修，見下一節。
- 用 Rust 照遊戲流程（snapshot → `apply_anim_frame`）在 6 個片段上檢查，body 與非物理 part 的常數誤差都是 0（`gpu_rig::check_terms`）。

### 手與前臂的差：bank 的縮放組合方式不同（2026-10-28 修）

- **現象**：`cf_j_hand`、`cf_s_hand`、`cf_j_forearm01` 這些具名 render 骨的列差 0.3～1 cm，只出現在部分角色身上，其他角色是 0。
- **分析工具**：`Tools/crowd_rig_errors.lua <報告資料夾> [骨名 pattern] [--actors] [--rows N]` 依骨名分組統計 `rig.bin`。它在 bind 空間把差拆成旋轉角、縮放／剪切與平移。
  - 手與前臂的差不是旋轉，也不是平移：兩邊線性部分的比值是行列式 1 的對稱應變（剪切到 0.15），軸心落在骨頭自己的位置。
  - 和身形、clip、時間都無關：第一幀、沒在交叉淡入時就有。只看那張卡手臂這條線有沒有非等比縮放。
- **根因**：bank 存每根原生骨的 Unity local TRS，再用 `IndependentPose.usf` 的 `NativeTRS` 組合。這是 UE FTransform 的做法：旋轉相乘、縮放逐軸相乘，不會產生剪切。GPU rig 和原生 Rust（`pose::world_of`）、Unity 一樣把完整矩陣相乘。
  - 祖先骨有非等比縮放、子骨又相對它轉動時，兩種做法的結果就不同。例如 C004 的 `cf_j_arm00` 是 (0.96, 1, 1)、`cf_j_shoulder` 是 (1, 0.988, 1.03)；前臂 0.92、手 0.9 的四肢尺寸修正也會造成。
  - Rust 測試 `gpu_rig::composition::gpu_rig_trs_composition`（ignored，要本機卡）用同一份 locals 比兩種組合：C004、C005 的腳最多差 5 cm、前臂附近 2.8 cm；C001 只差 0.14 cm。
  - 腳、身體網格走 palette 別名列，bank 那邊也是原生的完整矩陣，所以不受影響。受影響的只有每人約 34 根具名 render 骨（脊椎、手臂、手、胸、頭），其中手臂這條線最明顯。
- **修正**：`GpuRig.usf` 的 `TaskCS` 對具名原生骨（term 第一列 `.z`=1，`KKRuntimeGpuRig.cpp` 標 `Native`）改用 `NativeWorld`：沿父骨從 `Worlds` 反推每根骨的 local TRS（縮放正負號取基準 scale；`Worlds` 已含物理寫回），再照 `NativeTRS` 組合，含負縮放分支。
  - `TaskCS` 多綁 `RigBones`。
  - `ok.Crowd.GpuRigFullBones 1`：改回完整矩陣，用來做前後對照。在 rig 安裝時讀取，用 `--exec` 傳。
  - 照使用者「用完整解算當對照」的決定，rig 對齊 bank。Unity 原作是有剪切的完整矩陣；想讓兩邊都照原作，要改的是 bank 的 `NativeTRS`。
- **數字**：同一個 Editor 建置、同一組 10 張卡（C000～C010，跳過 C002），`--editor --count 10 --gpu-rig-verify 20`。修正前加 `--exec "ok.Crowd.GpuRigFullBones 1"`。逐骨數字取自 `crowd_rig_errors.lua`（第一次比對的 `rig.bin`）。

  | | 修正前 `Saved/Crowd/Room100-20261028T030418` | 修正後 `Saved/Crowd/Room100-20261028T030513` |
  |---|---:|---:|
  | 具名骨 310 列：平均 | 0.0225 cm | 0.0003 cm |
  | 具名骨 ≥0.1 cm 的列 | 12 | 0 |
  | `cf_j_hand_R` 平均／最大 | 0.146／0.956 cm | 0.0001／0.0001 cm |
  | `cf_j_hand_L` 平均／最大 | 0.113／0.860 cm | ＜0.0001 cm |
  | `cf_j_forearm01_R`／`_L` 最大 | 0.436／0.448 cm | 0.0005／0.0006 cm |
  | palette 列 19131 列（含物理件）：平均、≥10 cm | 0.284 cm、0.72% | 0.280 cm、0.77% |
  | 全部列（第一次比對）：平均、≥10 cm | 0.280 cm、0.71% | 0.275 cm、0.76% |
  | 全部列（game.log，20 次比對的平均） | 0.185 cm | 0.190 cm |

  - 具名骨只剩胸部 `cf_j_bust02`（最大 0.015 cm，跟著物理）。
  - 整體平均與 ≥10 cm 的比例由 palette 列裡的物理件決定，兩次的起伏是連續模擬與逐 clip 解算的差（見「GPU 物理」一節），和這個修正無關。

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
- **已修（10-28）**：總 bank 一次預約全部大小時，99 個私有 bank 的租約還算在同一份 4 GiB 預算裡，所以被拒。現在做法如下：
  - 總 bank 合成好之後，`MakeRoomForAggregate`（`OKCrowdRuntimeProgressiveGpu.inl`）從離玩家最遠的人開始退私有 bank，直到放得下。退掉的人在 Install 前先不顯示，和 waits for budget 一樣。總 bank 本身就超過預算時，直接報錯。
  - 總 bank 合成好之後不再收新的私有 bank，總 bank 立刻開始上傳，不再等全部私有 bank 裝完。
  - 預約被拒時（退掉的 bank 要等 GPU fence 才釋放）下一 tick 重試，不再 Fail。
  - Install 時退掉所有私有 bank，包括已上傳但還沒裝上的。之後釋放 CPU 端的私有 bank（`E->Bank`），RAM 少 2.4 GB。
  - 驗證：`--editor --aggregate --benchmark 20`（`Saved/Crowd/Room100-20261028T031839`）。總 bank 2340 MB；74 個私有 bank 共 1739 MB 留著，25 個退掉。結果 drawn 100／failed 0，101.4 秒全員畫出，149.0 FPS（p95 7.27 ms）。report.json 的 `triangles` 只有 2643，是統計沒算到 instanced skinned mesh，截圖裡 100 人都在。
  - 被拒後 `FLateSourceDepth::Draw` 存取違規的停止路徑沒有另外查：現在不會再走到被拒那一步。

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

### GPU 物理之後的街頭驗證（2026-10-28，打包版 Phys1028A，預設仍關閉）

`run_street_social_development.lua -Control -Sandbox handrig -GpuRig -GpuRigVerify 20`（ok-game-mcp CLI，控制埠 48070）。只開這一個遊戲，跑完關掉。報告：`Saved/StreetSocial/Play-20261028T041021`（`rig.bin`、`rig.txt`、`control_shots/`）。

- **比對**：11 人、20 幀、23176 列，沒有交叉淡入。20 幀平均位移差 0.74 cm，最大 135 cm；第 1 幀 ≥10 cm 的列占 1.44%（`crowd_rig_errors.lua`）。
  - 具名骨 341 列：平均 0.022 cm，≥10 cm 的 0 列。
    - 手最大 0.11 cm，比百人房的 0.0001 cm 大：bank 的格數算錯，見下面「街頭手與髖部的 0.11 cm」，已修。
    - 胸 `cf_j_bust02` 平均 0.19、最大 0.94 cm，跟著物理。
  - palette 列 22791 列：平均 0.86 cm，≥10 cm 占 1.46%。
    - 大的都在三月七（actor 3）的飾品 part 55～65，最大 175 cm；其他人零星幾列 12～33 cm。
    - 都是物理件：bank 逐片段從頭解算，rig 是連續模擬。
    - 這個量法是看網格原點（腳底）的位移，離原點遠的飾品轉一點角度數字就很大。
- **畫面**（`control_shots/shot.0001`～`0013.png`）：
  - 三月七正反面：頭髮、裙子、弓弦、相機都在原位，沒有部件飛出去；之前「弓弦、緞帶停在水平形狀往兩側伸出」的問題已經沒有。
  - 雷電將軍（長辮子）背面、貞德 Alter 正面也都正常。
  - 串流動作：`/play` 讓貞德 Alter 播 `phone_standing.okanim`、三月七播 `drink_standing.okanim`，姿勢換成動作（左手抬起／手到嘴邊），表示 overlay 有蓋上 rig 的列。`/play` 不會掛手機與杯子道具，道具沒驗。
  - 看向玩家：和貞德 Alter 交談時，她轉向玩家、臉朝著玩家。咖啡廳裡的其他女主外觀也都正常。
- **結論**：物理件與動作在畫面上沒看到問題，數字差異來自物理件連續模擬（量法也放大了數字）。預設仍維持關閉，要不要開由使用者決定。

### 街頭手與髖部的 0.11 cm：bank 的格數算錯（2026-10-28 修）

- **現象**：部分女主從 `cf_j_hips` 起整個身體平移約 0.1 cm（手 0.11 cm 只是其中一段），旋轉一致；百人房只有 0.004 cm。
- **查法**：
  - `--gpu-rig-verify` 另外寫出 `rig-play.txt`（每人當幀的片段槽、時間、權重）與 `rig-actors.txt`（每人的卡與各槽片段）。
  - Rust `gpu_rig::verify_time::gpu_rig_verify_time` 用這兩個檔還原當幀，比對 rig−bank 的差。
  - `gpu_rig::street_hips` 確認完整解算與原生取樣的髖部完全相同，排除了動態與互動。
- **根因**：
  - bank 的格數 `Frames = CeilToInt(Duration*60)` 是浮點運算。`walk_01` 長 2.0667 秒，乘 60 等於 124.00001，結果算成 125 格，key 間隔變成 0.0165333 秒。
  - key 和片段原本 60 Hz 的取樣點錯開，線性內插就把曲線每個轉折都切掉一點，最多接近一幀的移動量。
  - 用這個模型算出的差和 dump 完全一致（`Play-20261028T051629`，貞德 Alter `walk_01` 第 0.6 秒：(−0.0377, 0.0108, 0.0492) cm，誤差 0.0000 cm）。
  - rig 的取樣對齊 k/60，是對的；偏掉的是 bank。
- **修正**：
  - `OKRuntimePoseFrames`（`KKRuntimePoseBankTypes.h`）：只比整數多一點點時算成那個整數。bank 建置、GPU 磁碟快取、記憶體估算三處共用。
  - bank 的快取識別版本 4 → 5，舊格數的快取會重建：第一次載入會慢一些。
- **修正後**（同一個 Editor 建置）：
  - 街頭 `Saved/StreetSocial/Play-20261028T052135`：手與髖部最大 0.0001 cm（兩位正在走路的女主也一樣），具名骨 341 列平均 0.0051 cm，只剩跟著物理的胸部。
  - 百人房 `Saved/Crowd/Room100-20261028T052247`：具名骨平均 0.0003 cm，不變。
- **另外**：04:43 那次街頭 Editor 一啟動（第 0 幀）就 GPU 當機（`DXGI_ERROR_DEVICE_HUNG`，`Play-20261028T044328`）。
  - 當時作用中的 breadcrumbs 是街頭地圖的 Nanite pass，女主還沒載入，沒有 GpuRig pass 在跑。
  - 同設定重跑兩次（`Play-20261028T051629`、`052135`）都正常，重現不出來。

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

## GPU 物理與骨架重排：100 人 169 FPS（2026-10-28）

### 基準與瓶頸

`Saved/Crowd/Room100-20261028T004052`（Editor `-game`，100 人，`--gpu-rig`）：70.8 FPS，幀 14.1 ms（P95 16.0 ms），GPU 13.5 ms。profilegpu：

| pass | GPU ms |
|---|---:|
| `Crowd.GpuRigSkeleton` | 2.36 |
| 物理 9 個 pass 合計（`Crowd.GpuRigPhysics k.s`） | 約 7.0（飾品 stage 0、1 各約 2.0；衣服 stage 0 1.56） |

這些 pass 的工作量不大，慢在延遲：每個 dispatch 的時間等於最慢那條執行緒。
- 物理 stage 0 是「每組一條執行緒」依序還原組內幾百個節點，每個節點都要等父節點的 world 從記憶體讀回來。
- 每顆粒子要逐一檢查鏈上約 19～24 個碰撞體，每個碰撞體又要先讀索引、再讀骨頭矩陣。
- SkeletonCS 是每人一條執行緒，依序算 600 多根骨。

離線統計（`gpu_rig::physics::stats::gpu_rig_physics_stats`，40 張卡）：最長的鏈 36 顆粒子（C009），碰撞體每鏈最多 24 個，節點深度最多 28 層；需要串行的組很少（只有 C022、C026、C027、C036、C037 有）。

### 做法

- **物理每人一個 thread group，一個 dispatch**（原本是 3 種組 × 3 個 stage = 9 個 pass）。group 內依序跑她的 kind 2 → 1 → 0：
  - 節點依深度分層，每層由 64 條執行緒一起算（`OKGpuRigPhysicsGroups` 在 CPU 建表）。
  - 碰撞體每幀先算好膠囊的兩端（`PrepareCollider`），粒子每次一口氣讀 4 個（`CollideAll`）。
  - 鏈以「會互相影響」為準分成連通分量：一條鏈寫的骨頭是另一條讀的骨頭本身或其祖先時才串行。由 `OKGpuRigChainNodes` 判斷，Rust 的 `Physics::serial` 也改成同一個標準。不互相影響的鏈各自平行，不必再從 locals 重建骨頭。
  - 一個 group 內的工作依成本由大到小排序，最貴的先開始。
- **SkeletonCS 每人一個 thread group**：基準 locals、clip 每個 channel 的取樣、交叉淡入由 64 條執行緒分擔。limb lists、PV、ExpressionBone、ABMX 和看向玩家仍在第 0 條執行緒依序做。最後的 worlds 依骨骼深度分層平行算，仍是完整的 Parent × TRS(local)，TaskCS 的 `NativeWorld` 照常可用。
- 遠處物理 LOD 沒有做：工作量受延遲限制，遠處降頻縮短不了最慢那條執行緒；重排之後物理只剩 0.6 ms，已經不需要。

### 結果（1920×1080，100 人，`t.MaxFPS 0`，只開這一個 Unreal 遊戲）

| 版本 | FPS | 幀 ms（P95） | game / render / GPU ms | draw | 報告 |
|---|---:|---:|---:|---:|---|
| 改前（9 個物理 pass） | 70.8 | 14.12（16.04） | 3.53 / 10.01 / 13.45 | 5652 | `Room100-20261028T004052` |
| **物理與骨架重排** | **169.1** | **5.91（6.38）** | 2.18 / 5.90 / 4.63 | 5398 | `Room100-20261028T030808` |

同一輪 profilegpu：Skinning 合計 0.88 ms，其中 Skeleton 0.18、Physics 0.61、Tasks 0.02 ms（改前 Skeleton 2.36 ms，物理約 7.0 ms）。100 人全部畫出 134.5 秒，開卡快取全冷（Rust 改過）。

### 精度（`--editor --count 10 --gpu-rig-verify 20`，`Saved/Crowd/Room100-20261028T030704`）

- 逐幀檢查的平均位移差 0.197～0.222 cm。
- rig.bin 整體：平均 0.273 cm，≥ 10 cm 的列 0.755%。

各卡（`Tools/crowd_rig_errors.lua` 的資料，逐列位移差）：

| 卡 | 平均 cm | ≥ 10 cm |
|---|---:|---:|
| C000 | 0.870 | 3.48% |
| C001 | 0.149 | 0 |
| C003 | 0.135 | 0 |
| C004 | 0.040 | 0 |
| C005 | 0.121 | 0 |
| C006 | 0.050 | 0 |
| C007 | 0.269 | 1.39% |
| C008 | 0.380 | 0.96% |
| C009 | 0.415 | 1.12% |
| C010 | 0.194 | 0.43% |

- 這次的卡組和 10-27 基準不同：卡片篩選改了，多了 C007～C010；C009 有 36 顆粒子的長鏈。兩次都有的卡和基準一致（C001 0.142 → 0.149、C003 0.160 → 0.135、C004、C005、C006 持平）。
- 重排後物理的數學和原本相同：節點還原的順序、碰撞體讀到的骨頭世界、鏈之間的先後都不變，只改了誰來算、何時算。
- 中途有一次 C001 量到 0.70 cm，同一份物理程式重量回到 0.149 cm，是物理連續模擬的單次波動。

### 腳步與載入

- **腳步**：房間原本固定以 110 cm/s 移動，但走路片段的步幅隨腿長不同，各人自然步速 1.36～1.58 m/s（`gpu_rig::walk::walk_speed`：腳著地時相對髖部往後的速度，取中位數）。現在每人以自己的步速移動（`FOKRuntimeCharacterStatus::WalkSpeed`，開卡時由 `ok_session_walk_speed` 算出）。
  - 跟拍錄影：`Room100-20261028T031157/demo.mp4`。鏡頭在動、地板是純色，從連續影格判斷不出腳有沒有滑，這段沒有目視確認。
- **腳步滑動用數值驗證（2026-10-28）**：
  - 量法：腳的世界位置 = 她的姿勢（原生解算；bank 與 rig 都和它一致，見 `--gpu-rig-verify`）＋她在房間裡的位置與朝向。
  - 腳骨（`cf_j_foot`、`cf_j_toes`）在它最低點 1 cm 以內算著地，著地期的水平速度就是滑動。
  - 離線：`gpu_rig::foot_slip::gpu_rig_foot_slip`，照房間的走法模擬。
  - 遊戲內：`--exec "ok.Crowd.WalkTrace 1"` 讓遊戲把每人每幀的位置、朝向、片段與畫出的片段時間寫進報告旁的 `walk.tsv`。`gpu_rig_foot_slip_trace`（`OK_WALK_TRACE=<報告資料夾>`）照它重建腳的軌跡，每段至少 1/30 秒，只算單播走路片段、沒在交叉淡入的幀。
  - 這兩個測試都是 ignored，要本機卡。
- **找到的三個原因與修正**（都是通用修法，不逐卡調數）：
  1. 步速是相對髖部量的，髖部本身會前後晃；角色是以根骨移動。C004、C005 量成 136 cm/s，實際要 162 cm/s 腳才不滑。改在根骨空間量。
  2. 等速移動本身就會滑：著地腳往後的速度在一個步態週期內會變，最好的等速也還有中位數 15～22 cm/s。
     - 改成速度曲線：`gpu_rig::walk::walk_curve` 每 1/60 秒取著地那隻腳往後的速度，5 幀平滑；`ok_session_walk_curve` 傳給執行期。
     - 房間每幀用 `OKGetCrowdRuntimeWalkSpeedNow`：她走路片段在目前時間的速度，乘上交叉淡入權重，起步時跟著淡入加速。
     - 離線模擬著地期滑動中位數 3～5 cm/s、p90 6～9 cm/s（10 張卡）。
  3. 轉彎時她朝目標方向移動，臉卻還朝別處，腳就橫向滑（中位數 55～90 cm/s）。改成：
     - 偏差超過門檻先原地轉身（起步要小於 20°，走路中超過 60° 才停下來轉）；
     - 沿她自己的面向前進；
     - 轉身時繞著地腳的腳掌（`walk_curve` 的 pivot），不繞根骨。
  - `ok.Crowd.WalkCurve 0` 回到舊走法（平均步速、直接朝目標、繞根骨轉），用來對照。
- **遊戲內數字**：Editor、10 人、`--benchmark 20 --follow C000`。C000 一直走，其他人隨機走。

  | 走法 | 模式 | 報告 | 直走著地：中位數／p90 | 邊走邊轉的著地段：段數，中位數／p90 |
  |---|---|---|---:|---:|
  | 舊（`ok.Crowd.WalkCurve 0`） | bank | `Saved/Crowd/Room100-20261028T035057` | 15.9／37.8 cm/s | 357 段，80.2／190.3 cm/s |
  | 只改速度曲線 | bank | `Room100-20261028T034841` | 6.2／17.0 | 461 段，65.7／183.3 |
  | 只改速度曲線 | `--gpu-rig` | `Room100-20261028T035001` | 5.7／13.8 | 232 段，54.8／142.9 |
  | 速度曲線＋繞腳轉 | bank | `Room100-20261028T040111` | 5.6／13.7 | 674 段，47.4／178.1 |
  | 速度曲線＋繞腳轉 | `--gpu-rig` | `Room100-20261028T040232` | 5.3／13.4 | 1160 段，92.3／191.0 |
  | 最終（＋先轉向、沿面向走） | bank | `Room100-20261028T040503` | 6.1／14.9 | 28 段，4.3／11.3 |
  | 最終 | `--gpu-rig` | `Room100-20261028T040600` | 5.8／13.9 | 85 段，15.5／75.9 |

  - bank 與 rig 共用同一個時鐘和移動，結果一致。
  - 遊戲內的直走數字比離線高約 2 cm/s：畫出的姿勢每 1/60 秒才換一格，根骨卻每幀連續移動。
  - 只加「繞腳轉」沒有用，轉彎時的滑動主要來自移動方向和面向不一致；先轉向再走之後，邊走邊轉的著地段才少到原本的一成以下。
  - 原地轉身：見下一項。
- **原地轉身也會滑，改成邊走邊轉（2026-10-28）**：
  - 原地轉身時播的是待機姿勢，兩隻腳都著地，繞哪一點轉都會滑。
  - 量法：WalkTrace 的表頭改成記下每人每個槽的片段，`gpu_rig_foot_slip_trace` 依每幀實際播的片段取姿勢，另外統計「站著轉身」的著地段。
  - 實測（`ok.Crowd.WalkCurve 1`，即上一版 b6caeecfa 的走法）：站著轉身中位數 29.5、p90 37.5 cm/s。
  - 改法（`ok.Crowd.WalkCurve 2`，預設）：
    - 她不再原地轉身，而是邊走邊轉，沿面向前進。
    - 轉速跟著當下步速等比例（轉彎半徑固定，約 60 cm），起步淡入時也跟著慢慢轉；繞著地腳轉。
    - 被擋住時不轉；目標在 60 cm 內且偏差超過 60° 就算到達，免得繞著目標轉圈。
  - 數字（Editor、10 人、`--benchmark 30 --follow C000 --exec "ok.Crowd.WalkTrace 1[,ok.Crowd.WalkCurve 1]"`）：

    | 走法 | 報告 | 直走著地 中位數／p90 | 邊走邊轉 | 站著轉身 |
    |---|---|---:|---:|---:|
    | 1：原地先轉 | `Saved/Crowd/Room100-20261028T043938` | 5.7／13.5 | 6 段，18.1／20.1 | 1540 段，29.5／37.5 |
    | 2：邊走邊轉，bank | `Room100-20261028T044057` | 5.9／14.2 | 1260 段，7.7／34.6 | 0 段 |
    | 2：邊走邊轉，`--gpu-rig` | `Room100-20261028T044207` | 6.1／14.6 | 1206 段，8.4／33.6 | 0 段 |

    （單位 cm/s。）邊走邊轉的段數變多，是因為轉彎從原地改成走成弧線，中位數 8 cm/s，比原地轉身的 29.5 低很多。
- **rig 模式載入期間不先顯示 bank**：rig 版的 bank 只留排版，載入期間不畫任何人，全員到齊後直接以合成的 rig 出現。曾試過每人各開一個單人 rig，但畫不出人，而且每多一人 GPU 就多 1.1 ms，已撤回（見 ERROR_LOG）。
- **開卡**：帶卡包的 OK 卡原本在同一個行程裡一次只能開一張。改成每次開卡各有自己的卡包後（`61f30722c`），16 張同時開 12.3 秒（原本逐張 96.8 秒）。
  - 單張冷開（行程已暖、不用開卡快取；`cba26afec` 起，身體 bundle 解碼結果整個行程共用、身體法線圖背景轉換）：C009 1.77 s、C049 2.59 s、C020 2.67 s、C030 2.10 s，改前 5～9 s。
  - 全新行程開的第一張卡（`2147120f9` 起，程式庫的 zipmod 掃描與程式庫貼圖存在 `%LOCALAPPDATA%\OpenKoikatsu\oklib-cache\library-scans`、`derived-textures`，跨行程沿用）：C049 3.59 → 2.65 s、C020 3.66 → 2.57 s。改版後第一次執行要建這些快取（3.2／3.0 s）。
  - 10 張同時開 6.8 s；與逐張開、與改前（`OK_PNG_DEFAULT=1`）都逐位元組相同。
- **關遊戲閃退**：載入中的工作執行緒在引擎結束後還去拿已卸載的 ImageWrapper 模組（`KKRuntimeMaterial.cpp`）。改成結束中就停下。全員載完後關閉沒有閃退；載入途中關閉的情況還沒重測。

### 打包版（2026-10-28）

- exe-only stage `Phys1028A`（`Saved/StreetSocial/Base1027`）＋ patch `StreetDisplay30Phys`（`Launch/runtime.json` 已改掛）。
- `CrowdRoom100.cmd --gpu-rig --benchmark 20`（`Saved/Crowd/Room100-20261028T032553`）：
  - **177.5 FPS**，幀 5.64 ms（P95 6.04、P99 6.31），game / render / GPU 1.56 / 5.63 / 4.61 ms，draw 5391。
  - 100 人全部畫出，用時 79.8 秒，沒有載入失敗，沒有當機。
- exe-only stage `Phys1028B`（同一個 base 與 patch；加進腳步修正 `b6caeecfa` 與開卡加速 `cba26afec`、`2147120f9`）。
  - Shader 自 `StreetDisplay30Phys` 之後沒有改動（最後一次是 03:18 的 `66211df88`，patch 是 03:24 建的），所以沿用原 patch。
  - 04:28 建 exe 時工作樹裡還有別的 session 未提交的 okchara／appearance 改動（新增的模組，不影響百人房與街頭）也一起編進去了。04:31 之後才改的 `OKCrowdRoom.cpp` 走法沒有編進去。
  - `CrowdRoom100.cmd --gpu-rig --benchmark 20`（`Saved/Crowd/Room100-20261028T042851`）：
    - **177.5 FPS**，幀 5.64 ms（P95 6.07、P99 6.39），game / render / GPU 1.53 / 5.63 / 4.54 ms，draw 5391。
    - 100 人全部畫出，沒有載入失敗，沒有當機，關閉正常。
    - 全部畫出用時 108.1 秒：新 exe 第一次執行，要先建程式庫掃描與程式庫貼圖的快取。
  - 第二次加 `--exec "ok.Crowd.WalkTrace 1"`（`Room100-20261028T043133`）：
    - 全部畫出用時 **75.0 秒**（Phys1028A 是 79.8 秒）。
    - FPS 162.6：逐幀寫 walk.tsv 的字串要花時間，這個數字不拿來當效能數字。
  - 新走法有生效，以 walk.tsv 檢查（腳本只看位置與朝向，不重算姿勢）：

    | 執行 | 走法 | 走路時移動方向與面向的偏差（p99／最大） | 原地轉身的幀數 |
    |---|---|---:|---:|
    | 打包版 Phys1028B，100 人 | 新 | 0.07°／1.2° | 4457 |
    | Editor 最終（`Room100-20261028T040600`） | 新 | 0.17°／30° | 3052 |
    | Editor `ok.Crowd.WalkCurve 0`（`Room100-20261028T035057`） | 舊 | 38°／65° | 0 |

    - 偏差是扣掉角色 yaw 慣例固定的 90° 之後的數字。
    - 著地滑動（`gpu_rig_foot_slip_trace`）這次沒有重算：測試正被手部 session 改寫中，數字無法和上表比較。
  - `StreetSocial.cmd -Sandbox Phys1028B -PortBase 48160 -Control`（預設 bank，不開 GPU rig；`Saved/StreetSocial/Play-20261028T043400`）：
    - 進入街頭，10 位女角與男主全部就緒（11/11），約 311 FPS。
    - 走到貞德·Alter 旁邊，她正常畫出，影子也在。
    - 以 `game_stop` 關閉，log 沒有 Fatal 或 Assertion。
