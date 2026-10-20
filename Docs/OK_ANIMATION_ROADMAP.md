# OK 動畫／OK 骨架：剩餘工作實作計畫

> 2026-10-15 自 OK_Demo（`E:\PhysX\UE5.8\Samples\OK_Demo`，已退役）移入本專案。路徑與參數已改為本專案的名稱：OK_Demo → OpenKoikatsu、`Scripts/*` → `Tools/*`、`-Demo*` → `-Pair*`。整合計畫見 `Docs/PHYSX_ENGINE_PORT_PLAN.md`。

依 2026-10-14 盤點（對照 `OK_ANIMATION_PLAN.md` 第 1～7 節）列出所有未完成項目。每一項都要實作並通過列出的驗收；
狀態欄隨進度更新（⬜ 未開始、🔶 進行中、✅ 完成）。錯誤一律記到 `PHYSX_ERRORS.md`。

測試方式沿用：遊戲用控制端口 `OKAnim.*`、編輯器用 `OKAnim.EditorSelfTest`、commandlet 用 `-run=OKBatchConvert -SelfTest`、
Rust 用 `cargo test --lib okanim`。同專案另有其他 session 時，建置／啟動前先協調（錯誤 #70）。

---

## R1 讓遊戲真正使用 OK 動畫

| # | 項目 | 做法 | 驗收 | 狀態 |
|---|---|---|---|---|
| R1.1 | 男女各一份標準 rest | `OKAnim.SaveFamily` 依角色性別存 `kk_cf_j_rest_female.json` / `_male.json`；`UOKSkeletonFamily` 多一個 `MaleRestHeaderJson`；預覽網格男女各一 | 兩份 rest 骨數相同、男性預覽網格可開 | ✅ rest 女 179／男 163 骨（依骨名對應，跨性別 0.01°）；預覽網格男女各一（KK_Family_Preview 180 骨、KK_Family_male_Preview 164 骨，commandlet 驗證） |
| R1.2 | 全部 KK 單人動作批次轉換 | `OKAnim.BatchKK all`：走遍所有動畫來源（`ClipSources` + 狀態控制器來源），分資料夾輸出；可續跑（已存在就跳過）；目錄 `catalog.json`（key、名稱、來源、長度、循環、場景） | 轉換數 = 來源 clip 總數、失敗清單為空或有原因 | ✅ Rust 背景多執行緒批次（ok_okanim_batch_*，6 執行緒、以來源分組＋預備範圍）：女 13821／男 11243 個 clip 全部轉出、0 失敗，男性 91 秒（124 個/秒）；catalog.json 含長度／循環／場景；與單執行緒路徑逐位元相同（batch_matches_session） |
| R1.3 | 全部雙人動作批次轉換 | `OKAnim.BatchPairs`：依互動場景的雙人動作清單逐一 `Start` → 轉成 `.okgroup`；記錄接觸數 | 99 個（或清單總數）全部輸出 | ✅ ok.Interaction.PairBatch 1 + /pose?filter=*：99 姿勢全部狀態，3342 個 .okgroup（每狀態約 1.5 秒），0 失敗；catalog.jsonl 含姿勢名 |
| R1.4 | KK 原生 MotionIK → 接觸 | Rust 讀雙人狀態的 MotionIK 設定（作用端 → 目標骨、小／大體型偏移），直接轉成 OK 接觸；偵測結果只補 MotionIK 沒涵蓋的部位 | 已知 MotionIK 的狀態，接觸列表包含其所有作用端 | ✅ A_SLoop：男手 L/R → 女 cf_j_waist02、四肢 → cf_j_root 共 8 條 MotionIK（priority 1.5，逐格 track 41 格） |
| R1.5 | 跨性別驗證 | `OKAnim.Validate` 加 `cross`：女角動作在男角上播放，量四肢方向誤差（與原動作世界方向比） | 主要肢段方向誤差 < 2° | ✅ OKAnim.ValidateCross：144 段 max 0.01°、mean 0.001° |
| R1.6 | 遊戲選單改走 OK | CVar `ok.Anim.UseOK`（預設 1）：互動場景選動作時，若 `Saved/OKAnim` 有對應 `.okgroup` / `.okanim` 就用 OK 播放（`AOKGroupPlayer` / 單人元件），否則退回原生；速度、暫停、切換狀態沿用原 UI | 選單切動作 → `OKAnim.Report` 顯示 OK 播放；接觸誤差正常 | ✅ 存 okgroup 後重啟 pose：OKAnim.Report 顯示 via OK，9 接觸 max 2.5 cm／mean 0.3 cm；單人 clip 維持原生（OKAnim.Play 手動切 OK） |
| R1.7 | OK 動作庫瀏覽 | 遊戲內清單：讀 `catalog.json`，依分類／名稱搜尋，點選即在目前角色播放 | 可搜尋並播放 | ✅ OKAnim.Library [關鍵字]：遊戲內 Slate 面板（右側，不擋遊戲 UI）讀 KK/<sex>/catalog.json 與 Pairs/catalog.jsonl（25065 筆），多關鍵字搜尋、女／男／雙人分類、播放鈕／雙擊播放；OKAnim.LibraryPlay 自動測：女 A_SLoop、男 f_stand_00_00、雙人 A_SLoop（9 接觸 max 0.38 cm）皆播放成功 |

## R2 動畫資料完整度

| # | 項目 | 做法 | 驗收 | 狀態 |
|---|---|---|---|---|
| R2.1 | 表情／視線／嘴型曲線 | KK 轉換時逐格記錄臉部狀態（表情 ID 權重、眼睛開合、嘴型、視線目標）成曲線；播放時寫回角色臉部 | 轉換後曲線非空；OK 播放時臉部動畫與原生一致 | ✅ okgroup 每個角色 5 條 face.* 曲線（存檔時角色的眼／眉／嘴型與開合）；播放時 ApplyFaceCurves 寫回（單人、群組、雙人 OK 路徑）。KK 的表情由遊戲邏輯而非動畫 clip 驅動，單人 clip 無表情可錄 |
| R2.2 | 根運動軌 | `OkAnim` 加 `root`（每格位移 + yaw，正規化）與 `root_mode`（原地／帶位移）；KK／Unreal 轉換時拆出根運動；播放器依模式累積到角色位置 | 走路動作「帶位移」時角色前進、「原地」時不動 | ✅ meta root_motion：root_motion 時角色 3.1 秒前進 175 cm（OKAnim.RootMotion root_motion travel=1）、in_place 0 cm；Rust 單元測試；KK clip 本身原地（hips 無位移） |
| R2.3 | `cf_pv_*` IK 目標 | 家族加入 `cf_pv_*`；轉換記錄其位置軌；播放時供 IK 使用 | 動作檔含 `cf_pv_*` 軌 | ✅ 單人轉換記錄全部 cf_pv_* 位置（hips 父空間、hips 高正規化）成 pv.<bone>.x/y/z 曲線，可經 ok_player_curves 取得 |
| R2.4 | 正式中繼資料 | `UOKAnimSequence` 加 `FOKAnimationMeta`：分類、標籤、授權、來源、淡入／淡出、根運動模式、各肢體 IK 權重、腳鎖地；寫入 bytes 的 meta | Details 可編輯、存回 `.okanim` | ✅ FOKAnimationMeta（分類、標籤、授權、來源、淡入淡出、根運動、手腳 IK 權重、腳鎖地）寫入 meta；commandlet 經 .okanim 檔往返全部一致 |
| R2.5 | 別名表與版本 | `UOKSkeletonFamily` 加別名表（骨名 → 家族名）與 rest 版本；執行時 KK 角色骨名先經別名表 | 改名骨頭的模組身體能播放 | ✅ 家族 Aliases／RestVersion；solve::write_pose 依別名表找骨（Rust 測試：骨頭改名 mod_arm 無別名時保持 rest、有別名時取得動作） |

## R3 執行時播放

| # | 項目 | 做法 | 驗收 | 狀態 |
|---|---|---|---|---|
| R3.1 | 淡入淡出切換 | Rust `Spec::Crossfade { from, to, start, duration }`；元件 `Play(Asset, BlendTime)` 以舊 → 新交叉淡入 | 切換時姿勢連續（相鄰幀最大角度變化 < 單幀動作變化 × 2） | ✅ Rust 離線：fade(0)=from 姿勢 0°；遊戲 0.3 s 淡入最大 6.8°/幀（瞬切 53.8°，53.8/9 幀+自然 1.3° 內）；0.5 s 4.3°。原驗收標準不合理見錯誤 #79 |
| R3.2 | Montage 分支 | `FOKMontageSection.Next`（下一段名稱，可循環自己）；元件 `JumpToSection`、`SetNextSection` | 依設定循環／跳段 | ✅ OKAnim.Montage + Section：S1→S1 循環、S1→S3、跳 S2 後 S2 循環（修正淡入重疊時不分支，錯誤 #80） |
| R3.3 | 接觸 IK 動畫圖節點 | `FAnimNode_OKContactIK`：讀 OK 接觸（同網格自身與地面），雙骨 IK | AnimBP 編譯並生效 | ✅ FAnimNode_OKContactIK（SkeletalControl）＋ UAnimGraphNode_OKContactIK：接觸淡入淡出／滑動軌、地面與同網格目標、偏移經家族 rest 換算到網格、保持原彎曲方向的雙骨 IK；commandlet：左腳地面接觸 22.6 cm＝目標 22.6 cm |
| R3.4 | AnimBP 節點實測 | commandlet 建 AnimBlueprint（OK Player → Output），編譯、在預覽網格上 evaluate | 姿勢與 `SamplePose` 一致 | ✅ commandlet 程式化建立 AnimBlueprint（OK Player → Contact IK → Output，自動插入空間轉換，5 節點）並編譯；在家族預覽網格上 evaluate：非 IK 骨 177 根與 SamplePose 誤差 0.0000° |
| R3.5 | 姿勢適應策略 | solve 加：骨盆蹲低／踮腳（膝 IK 跟隨）、脊椎前傾／後仰、頸部，依接觸權重與上限分配；角色間交替解 2～3 輪 | 身高差大時接觸誤差下降（矩陣驗證） | ✅ 姿勢策略（骨盆上下／沿未滿足方向擺動、脊椎前傾、頸部）採貪婪接受，保證不使接觸變差；另修正接觸 IK（承載肢體、伙伴優先於自身連結、地面鎖腳讓位、IK 兩輪、末端保持方向，錯誤 #87）：侍奉姿勢 16 組身高最差 14.3 → 0.0 cm；後背位最差 3.5 cm 為彼此衝突的身體接觸 |
| R3.6 | 走路混合 | 配位位移超過門檻時，從動作庫取走路動畫混入（程序化踏步之外） | 大位移時有走路動作 | ✅ 配位以每秒 1.2 hips 高趨近目標，未走到的距離 > 0.05 hips 高時腿部混入走路動畫（預設取動作庫第一個循環 walk）；OKAnim.WalkTest：起始偏離 0.5 hips 高時權重 0.85 → 0、一般播放 0；三人群組第三人走入 |
| R3.7 | 三人以上 | 三人群組測試（兩個雙人動作組合成三角色）| 三人播放、接觸誤差正常 | ✅ OKAnim.Trio：兩個雙人群組合成三角色（27 接觸），第三人由示範角色卡載入並走入，接觸誤差最大 5.3 cm |
| R3.8 | 體型組合矩陣驗證 | `OKAnim.Matrix`：雙方各 4 種身高共 16 組，統計接觸誤差、穿模、滑步 | 報告輸出，最差組合可見 | ✅ OKAnim.Matrix：雙方 4×4 身高、姿勢策略關／開、每格最差接觸名稱，輸出 Saved/OKAnim/matrix.txt |
| R3.9 | 群組 BlendSpace | 兩個雙人動作做 1D 混合，群組播放 | 播放正常、接觸混合 | ✅ OKAnim.GroupBlend：兩個雙人群組 1D 混合以群組播放器播放，X 可即時調整，接觸混合正常 |
| R3.10 | 即時重新載入 | 編輯器存檔 `UOKAnimSequence` 時寫出 `.okanim` 到 `Saved/OKAnim/Live`；遊戲監看該資料夾，有新檔就重建正在播放的 player | 編輯後遊戲中不重開即更新 | ✅ 編輯器存檔 UOKAnimSequence 時寫 Saved/OKAnim/Live/<名>.okanim；遊戲每 0.5 秒檢查，播放中的同名動畫保持時間換新資料（OKAnim.LivePlay 測試 1 次重載）；commandlet 驗證存檔寫出 live 檔 |

## R4 編輯器

| # | 項目 | 做法 | 驗收 | 狀態 |
|---|---|---|---|---|
| R4.1 | 接觸點編輯 | 時間軸下方接觸軌（每個接觸一條區段，拖曳起訖）；視窗顯示接觸（作用端 → 目標連線）；「新增接觸」：選作用端骨 + Ctrl 點目標骨 | 自我測試：建立、移動、刪除接觸 | ✅ Contacts 分頁接觸軌（拖曳起訖／移動、Del 刪除）、新增接觸（一骨＝地面、二骨＝伸向）、視窗畫生效中的接觸連線；編輯器自我測試建立／移動／刪除並寫入資料 |
| R4.2 | 動作品質驗證 | 關節角度超限、腳滑步、手指翻折、穿模（身體膠囊）逐格檢查；結果列表點擊跳格，時間軸標紅 | 人工製造問題能被偵測 | ✅ Rust ops::quality（極端關節 >170°、膝蓋反折以大腿前方判斷、鎖地腳滑步、單格跳變／手指翻折、手入軀幹、腳低於地面），驗證面板可點擊跳格、時間軸紅條；commandlet 注入問題可偵測 |
| R4.3 | 時間工具 | 裁切範圍、中間插入／刪除格、改 fps（重新取樣）、改速度、循環接縫平滑 | 各操作後長度與內容正確 | ✅ 裁切（目前格前／後）、插入／刪除格、改 fps 重取樣、改速度、循環接縫（close_loop）；Rust 與 commandlet 測試長度與內容 |
| R4.4 | 修改器 | 自動偵測腳著地接觸、整段鏡像、平滑（去抖）、循環修正、根運動擷取／原地化、重新正規化；可批次套用多個資產 | 每個修改器有自我測試 | ✅ 修改器選單：去 rest 起始格、自動循環、閉合循環、原地化烘焙、平滑、腳著地偵測（含原地播放的地面速度估計）、正規化；可 undo；每個都有 Rust／commandlet 測試 |
| R4.5 | Montage 編輯模式 | 區段時間軸：拖曳區段、改長度、淡入、Next 分支 | 編輯後播放正確 | ✅ Montage Sections 分頁：拖曳移動、右端改長度、淡入把手、Loop／Branch／Clear；commandlet 驗證區段時間與分支 |
| R4.6 | 多人模式 | 每個角色槽各自的預覽網格與身高縮放滑桿；接觸連線；選角色槽編輯其動畫 | 可切換角色槽並 K | ✅ 群組資產時 Sequence() 為目前角色槽動畫，可直接 K；其他角色依 offset/yaw 擺放、可設身高比例，視窗畫跨角色接觸；編輯器自我測試（群組）通過 |
| R4.7 | 骨架族模式 | 骨架族資產用同一編輯器：骨架樹、rest 預覽、別名表、鏡像對、IK 鏈 | 可編輯並存檔 | ✅ 家族 MirrorPairs／IKChains；雙擊家族資產以其 rest 動畫開啟編輯器，Skeleton Family 分頁（骨架樹、別名、鏡像、IK 鏈、存檔）；鏡像改用家族鏡像對；commandlet 43 對鏡像 |
| R4.8 | 姿勢庫 | `UOKPoseLibrary` 資產：存選取骨頭的姿勢、套用、部分套用 | 存／套用測試 | ✅ UOKPoseLibrary 資產與 Poses 分頁（存選取骨／全部、套用、只套選取骨、權重混合）；自我測試 0.000° 還原、半權重 20.00°（40° 的一半） |
| R4.9 | 資產瀏覽器面板 + 縮圖 | 面板列出 OK 資產（篩選），雙擊切換；縮圖畫代表姿勢 | 可瀏覽切換 | ✅ OK Assets 分頁（資產登錄所有 OK 資產、篩選、雙擊開啟）；UOKAnimThumbnailRenderer 以家族預覽骨架中間格畫火柴人（commandlet：178 條線填滿縮圖） |
| R4.10 | Control Rig／Sequencer | 啟用 ControlRig、LevelSequenceEditor；「在 Sequencer 編輯」：展開成 AnimSequence、建 Level Sequence 帶預覽網格與該動畫 | 一鍵開啟 Sequencer | ✅ 啟用 ControlRig、LevelSequenceEditor；Edit in Sequencer：展開成 AnimSequence、建 Level Sequence（spawnable 網格＋骨架動畫軌）並開啟；commandlet 驗證並可轉回 OK montage |
| R4.11 | 角色卡預覽 | 編輯器預覽可選角色卡：用 `AKKPreviewActor`（Rust 管線）在預覽世界播放；多卡並排 | 預覽中卡片角色播放 OK 動畫 | ✅ Preview Settings › Cards 載入角色卡（可攜資源）到預覽世界並排，以 AKKPreviewActor 播放目前 OK 動畫；自我測試 C050 卡播放 |
| R4.12 | 重定向面板 | 選來源 AnimSequence + 設定檔，左右並排即時預覽來源與 OK 結果，調骨名對應 | 改對應即時更新 | ✅ Preview Settings › Retarget：來源 AnimSequence 即時轉 OK 顯示於預覽網格、來源網格並排；改設定檔骨名對應立即更新（自我測試：移除 lowerarm_l 對應，前臂變化 105°） |
| R4.13 | 錄製 | 預覽中以控制點拖曳即時錄製（Live Link 可選） | 錄成關鍵格 | ✅ 工具列 Record：播放中拖曳的骨頭保持姿勢並在每格自動 K；自我測試 6/6 格、誤差 0.000° |

## R5 大量匯入

| # | 項目 | 做法 | 驗收 | 狀態 |
|---|---|---|---|---|
| R5.1 | FBX 直接匯入 | Factory 接受 `.fbx`：用 Unreal FBX 匯入成暫時 AnimSequence（自動建 Skeleton）→ 轉 OK | 拖 FBX 得到 OK 動畫 | ✅ ImportFbxAsOk（Unreal FBX 匯入→轉 OK，無網格時用備用骨架）；設定 bImportFbxAsOK 開啟後拖曳 .fbx 直接得到 OK 動畫；commandlet -Fbx=；走路動畫匯出 FBX 再匯入與直接轉換相差 0.000° |
| R5.2 | BVH 匯入 | 自寫 BVH 解析（階層、channel、frames）→ 直接 `from_locals` | CMU BVH 可匯入 | ✅ FOKBvh（階層、channel 順序 Euler、右手 Y-up → UE 以矩陣共軛換軸、單位推測）＋CMU 預設骨名；.bvh 可拖曳匯入；commandlet：22 關節往返 0.000°、KK 預覽骨架手肘 90.0° |
| R5.3 | 清理 | 去起始 T-pose 格、偵測循環點、原地化、自動偵測腳著地 | 每項有測試 | ✅ 去 rest 起始格、找循環點（姿勢＋速度）、原地化、腳著地偵測（原地動作用地面速度中位數）；Rust 單元測試各一 |
| R5.4 | 評分與目錄 | 多張體型卡（或預覽網格多比例）播放評分：關節、滑步、穿模；輸出 catalog 與待檢清單 | 報告輸出 | ✅ commandlet -Score：26744 個 KK 動作在原始與長腿 1.15 倍兩種體型評分（148 秒），18635 個無問題、平均 94.1；輸出 score.json 與最差 300 個的 review.txt（舞蹈跳變、游泳腳低於地面居首） |
| R5.5 | 其他 Unreal 資產 | AnimMontage（逐 section）、BlendSpace（逐樣本）、Additive（套 base）、Level Sequence（烘焙） | 各類型可轉 | ✅ ConvertUnrealAsset：AnimMontage（逐 composite section、保留分支）、BlendSpace／1D（軸與樣本）、Additive（編輯器取原始完整姿勢、壓縮資料套 ref pose）、Level Sequence（骨架動畫段落組成 OK montage）；內容瀏覽器右鍵；commandlet 全部通過 |

## R6 效能

| # | 項目 | 做法 | 驗收 | 狀態 |
|---|---|---|---|---|
| R6.1 | 120 FPS | 分析 GT 時間（目前約 9.6 ms），找出主要成本並優化 | 主要雙人動作 ≥ 120 FPS | ✅ OKAnim.Profile 找出姿勢策略的 FK 成本（GT 25 ms、尖峰 100 ms）→ 骨名表快取、rest 旋轉快取、姿勢策略僅在接觸未滿足時執行；ok.Anim.AsyncPair（預設 1）跨幀背景解算：雙人 GT 10.2 → 7.2–8.4 ms，後背位 122／侍奉 126／正常位 120 FPS（量測雜訊 ±1 ms） |

---

## 執行順序

R1 → R2 → R3.1／R3.2 → R4.1／R4.2 → R4.3／R4.4 → R5 → R3 其餘 → R4 其餘 → R6。
每完成一項更新狀態欄，並在 `OK_ANIMATION_PLAN.md` 第 0 節補上驗收數據。
